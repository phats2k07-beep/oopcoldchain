#include "crow.h"
#include <pqxx/pqxx>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <cstdlib>
#include <unordered_set>

#define main domain_model_demo_main
#include "../domain/DomainModel.cpp"
#undef main

using namespace std;
using namespace pqxx;

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
#endif
struct TelemetryData {
    string deviceId;
    string shipmentId;
    double temperature;
    double humidity;
    double latitude;
    double longitude;
    string timestamp;
    string status; 
};

//    3 Observers: TemperatureAlertHandler, TelemetryLogger, QualityEvaluator
class ISensorObserver {
public:
    virtual ~ISensorObserver() = default;
    virtual void onSensorData(const TelemetryData& data) = 0;
    virtual string name() const = 0;
};

class SensorDataPublisher {
private:
    vector<shared_ptr<ISensorObserver>> observers_;
public:
    void subscribe(shared_ptr<ISensorObserver> obs) {
        observers_.push_back(obs);
        cout << "[SUBSCRIBE] Observer da dang ky: " << obs->name() << "\n";
    }
    void publish(const TelemetryData& data) {
        for (auto& obs : observers_) obs->onSensorData(data);
    }
};
// REPOSITORY  POSTGRESQL 

class PostgresShipmentRepository : public IShipmentRepository {
private:
    string conn_str_;
public:
    PostgresShipmentRepository(string conn_str) : conn_str_(conn_str) {}

    // is_compromised (bool trong Domain) -> cột shipments.status
    void save(const Shipment& s) override {
        try {
            connection C(conn_str_);
            work W(C);
            W.exec(
                "UPDATE shipments SET status = $1 WHERE shipment_id = $2",
                pqxx::params{
                    s.getStatus() ? string("compromised") : string("InTransit"),
                    s.getId()
                }
            );
            W.commit();
        } catch (const exception& e) {
            cerr << "[REPO ERROR] save: " << e.what() << endl;
        }
    }

    Shipment* findById(const string& id) override {
        try {
            connection C(conn_str_);
            work W(C);
            result R = W.exec(
                "SELECT c.min_temperature, c.max_temperature "
                "FROM shipments sh JOIN containers c ON sh.container_id = c.container_id "
                "WHERE sh.shipment_id = $1",
                pqxx::params{id}
            );
            if (R.empty()) return nullptr;

            double min_t = R[0][0].as<double>();
            double max_t = R[0][1].as<double>();

            // Nạp lại đối tượng (static để trả về con trỏ an toàn)
            static Shipment loadedShipment(id, TemperatureRange(min_t, max_t));
            loadedShipment = Shipment(id, TemperatureRange(min_t, max_t));

            // Nạp các device thành Sensor để recordTemperature() hoạt động
            // (DB: devices.shipment_id trỏ tới shipments.shipment_id)
            result D = W.exec(
                "SELECT device_id FROM devices WHERE shipment_id = $1",
                pqxx::params{id}
            );
            for (auto row : D) {
                loadedShipment.addSensor(Sensor(row[0].as<string>(), Temperature(0.0)));
            }
            return &loadedShipment;
        } catch (const exception& e) {
            cerr << "[REPO ERROR] findById: " << e.what() << endl;
            return nullptr;
        }
    }
};

// OBSERVER 1: Cảnh báo đỏ -> ghi temperature_alerts + đánh dấu shipments.status
class TemperatureAlertHandler : public ISensorObserver {
private:
    IShipmentRepository& repo_;
    string conn_str_;
public:
    TemperatureAlertHandler(IShipmentRepository& repo, string conn_str)
        : repo_(repo), conn_str_(conn_str) {}
    string name() const override { return "TemperatureAlertHandler"; }

    void onSensorData(const TelemetryData& d) override {
        if (d.status != "VIOLATED") return;

        cout << "[ALERT] Shipment " << d.shipmentId
             << " | Device " << d.deviceId
             << " | Temperature " << d.temperature << " C\n";

        Shipment* shipment = repo_.findById(d.shipmentId);
        if (!shipment) {
            cerr << "[ALERT ERROR] Shipment not found: " << d.shipmentId << '\n';
            return;
        }

        TemperatureRange range = shipment->getRequiredRange();
        shipment->recordTemperature(d.deviceId, d.temperature, d.timestamp);
        repo_.save(*shipment);

        try {
            connection C(conn_str_);
            work W(C);
            W.exec(
                "INSERT INTO temperature_alerts "
                "(device_id, temperature, min_temperature, max_temperature, alert_type, occurred_at, status) "
                "VALUES ($1, $2, $3, $4, 'VIOLATION', $5, 'active')",
                pqxx::params{
                    d.deviceId, d.temperature, range.getMin(), range.getMax(), d.timestamp
                }
            );
            W.commit();
        } catch (const exception& e) {
            cerr << "[ALERT DB ERROR] " << e.what() << '\n';
        }
    }
};

// OBSERVER 2: Ghi log lịch sử cảm biến vào telemetry_readings
class TelemetryLogger : public ISensorObserver {
private:
    string conn_str_;
public:
    TelemetryLogger(string conn_str) : conn_str_(conn_str) {}
    string name() const override { return "TelemetryLogger"; }

    void onSensorData(const TelemetryData& d) override {
        try {
            connection C(conn_str_);
            work W(C);
            W.exec(
                "INSERT INTO telemetry_readings "
                "(device_id, temperature, humidity, latitude, longitude, recorded_at) "
                "VALUES ($1, $2, $3, $4, $5, $6)",
                pqxx::params{
                    d.deviceId, d.temperature, d.humidity,
                    d.latitude, d.longitude, d.timestamp
                }
            );
            W.commit();
            cout << "    [DB INSERT] Da luu telemetry cho device " << d.deviceId << "\n";
        } catch (const exception& e) {
            cerr << "[LOGGER ERROR] " << e.what() << endl;
        }
    }
};

// OBSERVER 3: Đánh giá độ tươi / chất lượng mặt hàng
class QualityEvaluator : public ISensorObserver {
private:
    IShipmentRepository& repo_;
public:
    QualityEvaluator(IShipmentRepository& repo) : repo_(repo) {}
    string name() const override { return "QualityEvaluator"; }

    void onSensorData(const TelemetryData& d) override {
        Shipment* s = repo_.findById(d.shipmentId);
        if (!s) return;

        TemperatureRange range = s->getRequiredRange();
        double mid = (range.getMin() + range.getMax()) / 2.0;
        double halfSpan = (range.getMax() - range.getMin()) / 2.0;
        double dev = fabs(d.temperature - mid);

        double quality = 100.0 - (dev / halfSpan) * 50.0;
        if (quality < 0) quality = 0;
        string fresh = (quality >= 80) ? "TUOI" : ((quality >= 50) ? "TRUNG BINH" : "KEM");

        cout << "    [QUALITY] Don " << d.shipmentId << " | Do tuoi: "
             << fixed << setprecision(1) << quality << "% (" << fresh << ")\n";
    }
};

// =========================================================================
// 5. [APPLICATION LAYER] APPLICATION SERVICES / USE CASES
// =========================================================================
class ShipmentApplicationService {
private:
    IShipmentRepository& repo_;
    SensorDataPublisher& publisher_;
    string connStr_;

public:
    ShipmentApplicationService(IShipmentRepository& repo, SensorDataPublisher& pub, string connStr)
        : repo_(repo), publisher_(pub), connStr_(connStr) {}

    // USE CASE 1: CreateShipment — tạo container (chứa ngưỡng nhiệt) rồi gắn vào shipment
    enum class CreateShipmentResult {
        Created,
        UnknownContainerType,
        Failed
    };
    CreateShipmentResult createShipment(
        const string& containerType,
        const string& origin,
        const string& destination,
        string& createdShipmentId
    ) {
        try {
            connection C(connStr_);
            work W(C);

            result typeResult = W.exec(
                "SELECT min_temperature, max_temperature "
                "FROM containers "
                "WHERE container_type = $1 "
                "ORDER BY container_id "
                "LIMIT 1",
                pqxx::params{containerType}
            );

            if (typeResult.empty()) {
                return CreateShipmentResult::UnknownContainerType;
            }

            const double minTemp = typeResult[0][0].as<double>();
            const double maxTemp = typeResult[0][1].as<double>();

            result idResult = W.exec("SELECT txid_current()::text");
            const string id = idResult[0][0].as<string>();
            const string shipmentId = "SHP-" + id;
            const string containerId = shipmentId + "-CT";
            const string deviceId = "DEV-" + id;

            W.exec(
                "INSERT INTO containers "
                "(container_id, container_type, min_temperature, max_temperature) "
                "VALUES ($1, $2, $3, $4)",
                pqxx::params{containerId, containerType, minTemp, maxTemp}
            );

            W.exec(
                "INSERT INTO shipments "
                "(shipment_id, container_id, origin, destination, status, started_at) "
                "VALUES ($1, $2, $3, $4, 'InTransit', NOW())",
                pqxx::params{shipmentId, containerId, origin, destination}
            );

            W.exec(
                "INSERT INTO devices (device_id, device_type, name, shipment_id) "
                "VALUES ($1, 'sensor', $2, $3)",
                pqxx::params{deviceId, deviceId, shipmentId}
            );

            W.commit();
            createdShipmentId = shipmentId;
            return CreateShipmentResult::Created;
        } catch (const exception& e) {
            cerr << "[USE CASE ERROR] CreateShipment: " << e.what() << "\n";
            return CreateShipmentResult::Failed;
        }
    }

    // USE CASE 2: ProcessSensorData (Kích hoạt Event Bus cho 3 Observers)
    void processSensorData(const TelemetryData& data) {
        publisher_.publish(data);
    }

    // USE CASE 3: GetShipmentTelemetry
    crow::json::wvalue getShipmentTelemetry(const string& shipmentId) {
        crow::json::wvalue result_json;
        try {
            connection C(connStr_);
            work W(C);
            result R = W.exec(
                "SELECT t.device_id, t.temperature, t.humidity, t.latitude, t.longitude, t.recorded_at "
                "FROM telemetry_readings t JOIN devices d ON t.device_id = d.device_id "
                "WHERE d.shipment_id = $1 ORDER BY t.recorded_at DESC LIMIT 50",
                pqxx::params{shipmentId}
            );

            int i = 0;
            for (auto row : R) {
                result_json[i]["device_id"]   = row[0].as<string>();
                result_json[i]["temperature"] = row[1].as<double>();
                result_json[i]["humidity"]    = row[2].as<double>();
                result_json[i]["latitude"]    = row[3].as<double>();
                result_json[i]["longitude"]   = row[4].as<double>();
                result_json[i]["timestamp"]   = row[5].as<string>();
                i++;
            }
        } catch (const exception& e) {
            cerr << "[USE CASE ERROR] GetShipmentTelemetry: " << e.what() << "\n";
        }
        return result_json;
    }

    bool getShipmentOptions(crow::json::wvalue& options) {
        try {
            connection C(connStr_);
            work W(C);

            result types = W.exec(
                "SELECT DISTINCT container_type "
                "FROM containers "
                "WHERE container_type IS NOT NULL AND btrim(container_type) <> '' "
                "ORDER BY container_type"
            );

            int i = 0;
            for (const auto& row : types) {
                options["container_types"][i++] = row[0].as<string>();
            }

            result warehouses = W.exec(
                "SELECT warehouse FROM ("
                "  SELECT origin AS warehouse FROM shipments "
                "  UNION "
                "  SELECT destination AS warehouse FROM shipments"
                ") AS locations "
                "WHERE warehouse IS NOT NULL AND btrim(warehouse) <> '' "
                "ORDER BY warehouse"
            );

            i = 0;
            for (const auto& row : warehouses) {
                options["warehouses"][i++] = row[0].as<string>();
            }

            return true;
        } catch (const exception& e) {
            cerr << "[USE CASE ERROR] GetShipmentOptions: " << e.what() << "\n";
            return false;
        }
    }
};

//  CROW SERVER & ROUTES
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001); // UTF-8
#endif
    crow::SimpleApp app;
    // Co the ghi de chuoi ket noi bang bien moi truong COLDCHAIN_DSN
    const char* dsnEnv = getenv("COLDCHAIN_DSN");
    string conn_str = dsnEnv ? string(dsnEnv)
        : string("dbname=coldchain user=postgres password=phatpg hostaddr=127.0.0.1 port=5432");

    mutex ws_mtx;
    unordered_set<crow::websocket::connection*> connected_clients;

    // 1. Khởi tạo Repo & Publisher
    PostgresShipmentRepository repo(conn_str);
    SensorDataPublisher publisher;

    // 2. Đăng ký đủ 3 Observers vào Publisher
    publisher.subscribe(make_shared<TemperatureAlertHandler>(repo, conn_str));
    publisher.subscribe(make_shared<TelemetryLogger>(conn_str));
    publisher.subscribe(make_shared<QualityEvaluator>(repo));

    // 3. Khởi tạo Application Service (Chứa các Use Cases)
    ShipmentApplicationService appService(repo, publisher, conn_str);

    // ROUTE 1: WEBSOCKET REAL-TIME
    CROW_WEBSOCKET_ROUTE(app, "/ws")
    .onopen([&](crow::websocket::connection& conn) {
        lock_guard<mutex> _(ws_mtx);
        connected_clients.insert(&conn);
        cout << "[WS] Client Dashboard da ket noi!\n";
    })
    .onclose([&](crow::websocket::connection& conn, const string& reason, uint16_t code) {
        (void)reason; (void)code;
        lock_guard<mutex> _(ws_mtx);
        connected_clients.erase(&conn);
    });

    // ROUTE 2: API CreateShipment
    CROW_ROUTE(app, "/api/shipments").methods(crow::HTTPMethod::Post)
    ([&](const crow::request& req) {
        auto json = crow::json::load(req.body);
        if (!json ||
            !json.has("container_type") ||
            !json.has("origin") ||
            !json.has("destination")) {
            return crow::response(400, "Missing container_type, origin or destination");
        }

        const string containerType = json["container_type"].s();
        const string origin = json["origin"].s();
        const string destination = json["destination"].s();

        if (containerType.empty() || origin.empty() || destination.empty()) {
            return crow::response(400, "Fields must not be empty");
        }

        string shipmentId;
        const auto result = appService.createShipment(
            containerType, origin, destination, shipmentId
        );

        if (result == ShipmentApplicationService::CreateShipmentResult::UnknownContainerType) {
            return crow::response(400, "Unknown container_type");
        }
        if (result == ShipmentApplicationService::CreateShipmentResult::Failed) {
            return crow::response(500, "Failed to create shipment");
        }

        crow::json::wvalue body;
        body["shipment_id"] = shipmentId;
        body["status"] = "InTransit";
        return crow::response(201, body);
    });

    // ROUTE 3: API ProcessSensorData (Gọi Use Case -> Kích hoạt 3 Observers -> Bắn WebSocket)
    CROW_ROUTE(app, "/api/sensor-data").methods(crow::HTTPMethod::Post)
    ([&](const crow::request& req) {
        auto json = crow::json::load(req.body);
        if (!json) return crow::response(400, "Invalid JSON");

        TelemetryData data{
            json["sensor_id"].s(), json["shipment_id"].s(),
            json["temperature"].d(), json["humidity"].d(),
            json.has("latitude")  ? json["latitude"].d()  : 0.0,
            json.has("longitude") ? json["longitude"].d() : 0.0,
            json["timestamp"].s(), json["status"].s()
        };

        // Kích hoạt Use Case
        appService.processSensorData(data);

        // Bắn WebSocket xuống UI
        {
            lock_guard<mutex> _(ws_mtx);
            for (auto client : connected_clients) {
                client->send_text(req.body);
            }
        }
        return crow::response(201, "Processed");
    });

    // ROUTE 4: API GetShipmentTelemetry
    CROW_ROUTE(app, "/api/shipments/<string>/telemetry").methods(crow::HTTPMethod::Get)
    ([&](const crow::request&, string shipmentId) {
        auto telemetryData = appService.getShipmentTelemetry(shipmentId);
        return crow::response(telemetryData);
    });

    // ROUTE 5: API GetShipmentOptions
    CROW_ROUTE(app, "/api/shipment-options").methods(crow::HTTPMethod::Get)
    ([&]() {
        crow::json::wvalue options;
        if (!appService.getShipmentOptions(options)) {
            return crow::response(500, "Failed to load shipment options");
        }
        return crow::response(options);
    });

    cout << " C++ Backend  port 8080...\n";
    app.port(8080).multithreaded().run();
}