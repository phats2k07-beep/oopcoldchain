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
#include <cmath>
#include <limits>
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


// 3. TUYEN DUONG: TOI UU GIONG (mo phong, khong goi API ngoai)
constexpr double kMinLat = 8.0, kMaxLat = 24.0, kMinLon = 102.0, kMaxLon = 110.0;

struct Waypoint {
    double lat;
    double lon;
};

static double haversineKm(const Waypoint& a, const Waypoint& b) {
    constexpr double kRad = 3.14159265358979 / 180.0, kEarth = 6371.0;
    const double dLat = (b.lat - a.lat) * kRad;
    const double dLon = (b.lon - a.lon) * kRad;
    const double s = sin(dLat / 2) * sin(dLat / 2) +
        cos(a.lat * kRad) * cos(b.lat * kRad) * sin(dLon / 2) * sin(dLon / 2);
    return 2.0 * kEarth * asin(sqrt(min(1.0, s)));
}

class RouteOptimizer {
private:
    // He so "khuat" mo phong di quan: tuyen sai khac nho ha tang nhanh hon,
    // phuong an di thang doc nui cham hon -> chon tuyen co gia tri nhan nho nhat.
    static constexpr double kDirectFactor = 1.0;   // cao ngang, neu duong thang doc nui
    static constexpr double kMidFactor    = 0.85;  // tuyen cao toc qua diem trung gian

    static const map<string, Waypoint>& cities() {
        static const map<string, Waypoint> dir{
            {"ha noi",    {21.0285, 105.8542}},
            {"hai phong", {20.8449, 106.6881}},
            {"hue",       {16.4637, 107.5909}},
            {"da nang",   {16.0544, 108.2022}},
            {"vinh",      {18.6796, 105.6813}},
            {"nha trang", {12.2388, 109.2723}},
        };
        return dir;
    }

    static Waypoint lookup(const string& name, const Waypoint& fallback) {
        string key;
        key.reserve(name.size());
        for (char ch : name) {
            key += static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        }
        auto it = cities().find(key);
        return it != cities().end() ? it->second : fallback;
    }

    // Tinh tong do dai 1 tuyen di kem di trung gian, da nhan he so chat luong duong
    static double weightedLength(const Waypoint& a, const Waypoint* mid,
                                 const Waypoint& b) {
        if (!mid) return haversineKm(a, b) * kDirectFactor;
        return (haversineKm(a, *mid) + haversineKm(*mid, b)) * kMidFactor;
    }

public:
    // Chon tuyen "re" nhat trong cac phuong an (di thang, hay di va 1 diem trung gian)
    static vector<Waypoint> buildRoute(const string& originName, const string& destName) {
        const Waypoint origin = lookup(originName, {21.0285, 105.8542});
        const Waypoint dest   = lookup(destName,   {16.0544, 108.2022});

        double best = numeric_limits<double>::max();
        const Waypoint* bestMid = nullptr;
        for (const auto& kv : cities()) {
            const Waypoint& mid = kv.second;
            const double len = weightedLength(origin, &mid, dest);
            if (len < best) { best = len; bestMid = &mid; }
        }
        vector<Waypoint> route{origin, dest};
        if (haversineKm(origin, dest) * kDirectFactor < best) {
            bestMid = nullptr; // di thang tot hon di trung gian
        }
        if (bestMid) route = {origin, *bestMid, dest};
        return route;
    }
};

class IoTHardwareEngine {
private:
    mt19937 gen;
    int scenario;
    double currentTemp;
    double currentHum;
    double tempSetpoint;      // hay moc, vi nhiet duoc giam sat
    vector<Waypoint> route;   // tuyen da toi uu
    size_t leg;               // doan dang di (trong [0, route.size()-1])
    double legProgress;       // tien do doan hien tai trong [0, 1]
    double kmPerTick;         // quang duong moi tick, co dinh
    double lat, lon;          // vi tri hien tai, da "ep khang" trong bien VN

    static double clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

public:
    IoTHardwareEngine() : scenario(1), currentTemp(4.0), currentHum(60.0),
        tempSetpoint(4.0), leg(0), legProgress(0.0), kmPerTick(12.0),
        lat(21.0285), lon(105.8542) {
        route = RouteOptimizer::buildRoute("Ha Noi", "Da Nang");
    }

    IoTHardwareEngine(double startTemp, double startLat, double startLon,
                      const string& originName = "Ha Noi",
                      const string& destName = "Da Nang")
        : currentTemp(startTemp), currentHum(60.0) {
        random_device rd;
        gen = mt19937(rd());
        uniform_int_distribution<> scenDis(1, 100);
        scenario = (scenDis(gen) <= 60) ? 1 : 2; // 60% xe giu an toan
        route = RouteOptimizer::buildRoute(originName, destName);
        leg = 0;
        legProgress = 0.0;
        tempSetpoint = clamp(startTemp, -30.0, 60.0);
        kmPerTick = 12.0; // ~40km/h * 5 giay/tick, moi tick di 1 khoang co dinh
        lat = route.front().lat;
        lon = route.front().lon;
        (void)startLat; (void)startLon; // vi tri thuc do theo tuyen duong
    }

    // Mo phong 1 tick: xe chay theo tuyen toi uu, nhiet/do am deo ke giua han
    // de khong bao gio lan ra vo han hay gia tri am lung tung.
    void advanceState() {
        if (route.size() >= 2) {
            const size_t lastLeg = route.size() - 2;
            if (leg < lastLeg || legProgress < 1.0) {
                const double segKm = haversineKm(route[leg], route[leg + 1]);
                const double step = segKm > 0.0 ? clamp(kmPerTick / segKm, 0.0, 1.0) : 1.0;
                legProgress = clamp(legProgress + step, 0.0, 1.0);
                if (legProgress >= 1.0 && leg < lastLeg) {
                    leg += 1;
                    legProgress = 0.0;
                }
            }
            const Waypoint& a = route[leg];
            const Waypoint& b = route[leg + 1];
            lat = clamp(a.lat + legProgress * (b.lat - a.lat), kMinLat, kMaxLat);
            lon = clamp(a.lon + legProgress * (b.lon - a.lon), kMinLon, kMaxLon);
        }

        // --- Nhiet do ---
        if (hasArrived()) {
            tempSetpoint = currentTemp; // da toi dich: giu nhiet on dinh
        }
        if (scenario == 1) { // Xe an toan: dao dong nhe quanh setpoint, ME GIUA CANH
            uniform_real_distribution<> dis(-0.2, 0.2);
            currentTemp = clamp(currentTemp + dis(gen),
                                tempSetpoint - 1.5, tempSetpoint + 1.5);
        } else {             // Xe su co: leo do NHUNG chan toi da +15 do so voi setpoint
            uniform_real_distribution<> dis(0.1, 0.5);
            currentTemp = min(currentTemp + dis(gen), tempSetpoint + 15.0);
        }

        // --- Do ama trong pham vi vat ly ---
        uniform_real_distribution<> humDis(-0.5, 0.5);
        currentHum = clamp(currentHum + humDis(gen), 0.0, 100.0);
    }

    bool hasArrived() const {
        return route.size() >= 2 && leg >= route.size() - 2 && legProgress >= 1.0;
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
    if (!dsnEnv || *dsnEnv == '\0') {
        cerr << "[STARTUP ERROR] Set COLDCHAIN_DSN to your PostgreSQL connection string.\n";
        return 1;
    }
    string conn_str(dsnEnv);
    string backend_url = "http://127.0.0.1:8080/api/sensor-data";

    CURLcode curlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curlInitResult != CURLE_OK) {
        cerr << "[STARTUP ERROR] curl initialization failed: "
             << curl_easy_strerror(curlInitResult) << '\n';
        return 1;
    }

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
                "SELECT sh.shipment_id, d.device_id, c.min_temperature, c.max_temperature, "
                       "sh.origin, sh.destination "
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
                string originName = row[4].as<string>();
                string destName   = row[5].as<string>();

                current_db_shipments.insert(sh_id);

                if (activeTrackings.find(sh_id) == activeTrackings.end()) {
                    double startTemp = (min_t + max_t) / 2.0; // khong con cot cur_temp -> khoi dong tai giua nguong
                    Shipment newShipment(sh_id, TemperatureRange(min_t, max_t));
                    newShipment.addSensor(Sensor(s_id, Temperature(startTemp)));
                    activeTrackings.insert({sh_id, {newShipment, s_id}});
                    hardwareEngines[s_id] = IoTHardwareEngine(
                        startTemp, 21.0285, 105.8542, originName, destName);
                    cout << "  + Them moi Shipment: " << sh_id << " (Device: " << s_id
                         << ", nguong " << min_t << ".." << max_t
                         << ", tuyen " << originName << " -> " << destName << ")\n";
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

            const bool wasMoving = !hw.hasArrived();
            hw.advanceState();
            if (wasMoving && hw.hasArrived()) {
                cout << "  [DEN DICH] " << ctx.shipment.getId()
                     << " da toi kho dich. Giu te nhi on dinh.\n";
            }
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
