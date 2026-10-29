#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <pqxx/pqxx>
#include <curl/curl.h>
#define main domain_model_demo_main
#include "../domain/DomainModel.cpp"
#undef main

using namespace std;
using namespace pqxx;
//  HTTP CLIENT

static size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    (void)ptr; (void)userdata;
    return size * nmemb;
}

class HttpClient {
public:
    static void sendPostRequest(const string& url, const string& jsonPayload) {
        CURL* curl = curl_easy_init();
        if (!curl) { cerr << "[HTTP ERROR] curl init failed\n"; return; }
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            cerr << "[HTTP ERROR] " << curl_easy_strerror(res) << "\n";
        } else {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            if (status != 200 && status != 201) {
                cerr << "[HTTP ERROR] status " << status << "\n";
            }
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
};


// 3. IOT HARDWARE ENGINE 
class IoTHardwareEngine {
private:
    mt19937 gen;
    int scenario;
    double currentTemp;
    double currentHum;
    double lat, lon;

public:
    IoTHardwareEngine() = default;
    IoTHardwareEngine(double startTemp, double startLat, double startLon)
        : currentTemp(startTemp), currentHum(60.0), lat(startLat), lon(startLon) {
        random_device rd;
        gen = mt19937(rd());
        uniform_int_distribution<> scenDis(1, 100);
        scenario = (scenDis(gen) <= 60) ? 1 : 2; // 20% ty le hong lanh
    }

    void advanceState() {
        if (scenario == 1) { // Xe an toan
            uniform_real_distribution<> dis(-0.2, 0.2);
            currentTemp += dis(gen);
        } else {             // Xe su co
            uniform_real_distribution<> dis(0.1, 0.5);
            currentTemp += dis(gen);
        }
        uniform_real_distribution<> humDis(-0.5, 0.5);
        currentHum += humDis(gen);
        uniform_real_distribution<> gpsDis(-0.002, 0.002);
        lat += gpsDis(gen);
        lon += gpsDis(gen);
    }

    double getTemp() const { return currentTemp; }
    double getHum() const { return currentHum; }
    double getLat() const { return lat; }
    double getLon() const { return lon; }
};

// Ham lay Timestamp chuan
string getIsoTimestamp() {
    auto now = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);
    tm utc;
    #ifdef _WIN32
        gmtime_s(&utc, &t);
    #else
        gmtime_r(&t, &utc);
    #endif
    ostringstream oss;
    oss << put_time(&utc, "%Y-%m-%dT%H:%M:%SZ"); // gio UTC that, khop voi chu Z
    return oss.str();
}

// Cau truc quan ly Phien theo doi
struct TrackingContext {
    Shipment shipment; // Aggregate Root
    string sensorId;   // device_id gan voi Shipment nay
};

// ==========================================
// 4. MAIN ORCHESTRATOR
// ==========================================
int main() {
    // Co the ghi de chuoi ket noi bang bien moi truong COLDCHAIN_DSN
    const char* dsnEnv = getenv("COLDCHAIN_DSN");
    string conn_str = dsnEnv ? string(dsnEnv)
        : string("dbname=coldchain user=postgres password=phatpg hostaddr=127.0.0.1 port=5432");
    string backend_url = "http://127.0.0.1:8080/api/sensor-data";

    curl_global_init(CURL_GLOBAL_DEFAULT);

    map<string, TrackingContext> activeTrackings;   // Key: shipmentId
    map<string, IoTHardwareEngine> hardwareEngines; // Key: device_id

    cout << "=== KHOI DONG IOT SIMULATOR (DDD + PQXX + libcurl) ===\n";

    while (true) {
        cout << "\n[SYNC DB] Quet don hang InTransit...\n";
        try {
            connection C(conn_str);
            work W(C);
            // Lay danh sach don InTransit: nguong nhiet (containers) + cam bien gan lien (devices)
            string sql =
                "SELECT sh.shipment_id, d.device_id, c.min_temperature, c.max_temperature "
                "FROM shipments sh "
                "JOIN devices d    ON d.shipment_id = sh.shipment_id "
                "JOIN containers c ON sh.container_id = c.container_id "
               "WHERE sh.status IN ('InTransit', 'compromised')";

            result R = W.exec(sql);
            set<string> current_db_shipments;

            for (auto row : R) {
                string sh_id = row[0].as<string>();
                string s_id  = row[1].as<string>();
                double min_t = row[2].as<double>();
                double max_t = row[3].as<double>();

                current_db_shipments.insert(sh_id);

                if (activeTrackings.find(sh_id) == activeTrackings.end()) {
                    double startTemp = (min_t + max_t) / 2.0; // khong con cot cur_temp -> khoi dong tai giua nguong
                    Shipment newShipment(sh_id, TemperatureRange(min_t, max_t));
                    newShipment.addSensor(Sensor(s_id, Temperature(startTemp)));
                    activeTrackings.insert({sh_id, {newShipment, s_id}});
                    hardwareEngines[s_id] = IoTHardwareEngine(startTemp, 21.0285, 105.8542); // toa do goc Ha Noi
                    cout << "  + Them moi Shipment: " << sh_id << " (Device: " << s_id
                         << ", nguong " << min_t << ".." << max_t << ")\n";
                }
            }

            for (auto it = activeTrackings.begin(); it != activeTrackings.end(); ) {
                if (current_db_shipments.find(it->first) == current_db_shipments.end()) {
                    cout << "  - Ngung theo doi Shipment: " << it->first << "\n";
                    hardwareEngines.erase(it->second.sensorId);
                    it = activeTrackings.erase(it);
                } else {
                    ++it;
                }
            }
        } catch (const std::exception &e) {
            cerr << "[LOI DB] " << e.what() << "\n";
        }

        if (activeTrackings.empty()) {
            this_thread::sleep_for(chrono::seconds(5));
            continue;
        }

        cout << "[TELEMETRY] Phat sinh du lieu & Kiem tra Nghiep vu DDD...\n";
        for (auto& pair : activeTrackings) {
            TrackingContext& ctx = pair.second;
            IoTHardwareEngine& hw = hardwareEngines[ctx.sensorId];

            hw.advanceState();
            string timestamp = getIsoTimestamp();

            ctx.shipment.recordTemperature(ctx.sensorId, hw.getTemp(), timestamp);

            auto events = ctx.shipment.getDomainEvents();
            string alertStatus = events.empty() ? "SAFE" : "VIOLATED";

            if (!events.empty()) {
                for (const auto& ev : events) {
                    cout << "  [CANH BAO DDD] " << ev.getShipmentId()
                         << " vi pham nguong: " << ev.getRecordedTemp() << "C!\n";
                }
                ctx.shipment.clearDomainEvents();
            }

            ostringstream json;
            json << fixed << setprecision(2);
            json << "{"
                 << "\"shipment_id\": \"" << ctx.shipment.getId() << "\", "
                 << "\"sensor_id\": \""   << ctx.sensorId << "\", "
                 << "\"timestamp\": \""   << timestamp << "\", "
                 << "\"temperature\": "   << hw.getTemp() << ", "
                 << "\"humidity\": "      << hw.getHum() << ", "
                 << "\"latitude\": "     << setprecision(6) << hw.getLat() << ", "
                 << "\"longitude\": "    << setprecision(6) << hw.getLon() << ", "
                 << "\"status\": \""      << alertStatus << "\""
                 << "}";
            json << setprecision(2);

            cout << "  -> " << json.str() << "\n";
            HttpClient::sendPostRequest(backend_url, json.str());
        }

        this_thread::sleep_for(chrono::seconds(3));
    }

    curl_global_cleanup();
    return 0;
}
