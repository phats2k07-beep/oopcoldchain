#include <iostream>
#include <string>
#include <vector>

using namespace std;

// 1. VALUE OBJECTS
class Temperature {
private:
    double value;
    string unit;
public:
    Temperature(double v = 0.0, string u = "C") : value(v), unit(u) {}
    double getValue() const { return value; }
    string getUnit() const { return unit; }
};

class GPSLocation {
private:
    double latitude;
    double longitude;
public:
    GPSLocation(double lat = 0.0, double lon = 0.0) : latitude(lat), longitude(lon) {}
    double getLat() const { return latitude; }
    double getLon() const { return longitude; }
};

class TemperatureRange {
private:
    double minTemp;
    double maxTemp;
public:
    TemperatureRange(double minVal = 2.0, double maxVal = 8.0) 
        : minTemp(minVal), maxTemp(maxVal) {}
    
    double getMin() const { return minTemp; }
    double getMax() const { return maxTemp; }
    
    bool isViolated(double temp) const {
        return (temp < minTemp || temp > maxTemp);
    }
};

class SensorReading {
private:
    Temperature temp;
    GPSLocation location;
    double humidity;
    string timestamp;
public:
    SensorReading(Temperature t, GPSLocation loc, double hum, string time)
        : temp(t), location(loc), humidity(hum), timestamp(time) {}
        
    Temperature getTemp() const { return temp; }
    GPSLocation getLocation() const { return location; }
    double getHumidity() const { return humidity; }
    string getTimestamp() const { return timestamp; }
};


// 2. DOMAIN EVENTS 

class TemperatureExceededEvent {
private:
    string shipmentId;
    string sensorId;
    double recordedTemp;
    string timestamp;
public:
    TemperatureExceededEvent(string sId, string sensId, double temp, string time) 
        : shipmentId(sId), sensorId(sensId), recordedTemp(temp), timestamp(time) {}

    string getShipmentId() const { return shipmentId; }
    string getSensorId() const { return sensorId; }
    double getRecordedTemp() const { return recordedTemp; }
    string getTimestamp() const { return timestamp; }
};


// 3. ENTITIES 
class Driver {
private:
    string driverId;
    string fullName;
    string phoneNumber;
public:
    Driver(string id, string name, string phone) 
        : driverId(id), fullName(name), phoneNumber(phone) {}
    string getId() const { return driverId; }
    string getName() const { return fullName; }
};

class ColdWarehouse {
private:
    string warehouseId;
    string name;
    GPSLocation location;
public:
    ColdWarehouse(string id, string n, GPSLocation loc) 
        : warehouseId(id), name(n), location(loc) {}
    string getId() const { return warehouseId; }
    GPSLocation getLocation() const { return location; }
};

class Sensor {
private:
    string sensorId;
    Temperature currentTemp;
public:
    Sensor(string id, Temperature temp) : sensorId(id), currentTemp(temp) {}
    
    string getId() const { return sensorId; }
    Temperature getTemp() const { return currentTemp; }
    
    void updateTemp(Temperature newTemp) { 
        currentTemp = newTemp; 
    }
};

// 4. AGGREGATE ROOT
// Thùng bảo quản — nắm ngưỡng nhiệt của lô hàng
class Container {
private:
    string containerId;
    string containerType;
    TemperatureRange allowedRange;
public:
    Container(string id, string type, TemperatureRange range = TemperatureRange(2.0, 8.0))
        : containerId(id), containerType(type), allowedRange(range) {}

    string getId() const { return containerId; }
    string getType() const { return containerType; }
    TemperatureRange getAllowedRange() const { return allowedRange; }

    bool isSuitableFor(double temp) const {
        return !allowedRange.isViolated(temp);
    }
};

class Shipment {
private:
    string shipmentId;
    Container container;
    vector<Sensor> sensors;
    bool isCompromised;
    vector<TemperatureExceededEvent> domainEvents;

public:
    Shipment(string id, TemperatureRange range = TemperatureRange(2.0, 8.0))
        : shipmentId(id), container("CT-" + id, "default", range), isCompromised(false) {}

    Shipment(string id, const Container& c)
        : shipmentId(id), container(c), isCompromised(false) {}

    string getId() const { return shipmentId; }
    bool getStatus() const { return isCompromised; }
    const Container& getContainer() const { return container; }
    TemperatureRange getRequiredRange() const { return container.getAllowedRange(); }

    void addSensor(const Sensor& s) { 
        sensors.push_back(s); 
    }

    const vector<TemperatureExceededEvent>& getDomainEvents() const {
        return domainEvents;
    }

    void clearDomainEvents() {
        domainEvents.clear();
    }

    // Ghi nhận số liệu cảm biến, tự phát hiện vi phạm nhiệt độ
    void recordTemperature(string sensorId, double tempValue, string timestamp) {
        for (auto& s : sensors) {
            if (s.getId() == sensorId) {
                s.updateTemp(Temperature(tempValue));

                if (!container.isSuitableFor(tempValue)) {
                    isCompromised = true;
                    TemperatureExceededEvent event(shipmentId, sensorId, tempValue, timestamp);
                    domainEvents.push_back(event);
                }
                break;
            }
        }
    }
};


// 5. REPOSITORY INTERFACE
class IShipmentRepository {
public:
    virtual ~IShipmentRepository() = default;
    virtual void save(const Shipment& shipment) = 0;
    virtual Shipment* findById(const string& shipmentId) = 0;
};

// 6. DEMO (Application Layer)
int main() {
    cout << "=== CORE DOMAIN LAYER SAN SANG ===" << endl;
    
    // 1. Tạo lô hàng vắc-xin yêu cầu nghiêm ngặt 2.0 - 8.0 độ C
    Shipment vaccineShipment("SHIP-COVID-2026", TemperatureRange(2.0, 8.0));
    
    // 2. Gắn thiết bị giám sát
    Sensor iotSensor("IOT-NODE-99", Temperature(4.0));
    vaccineShipment.addSensor(iotSensor);
    
    // 3. Giả lập cảm biến gửi dữ liệu vi phạm
    vaccineShipment.recordTemperature("IOT-NODE-99", 10.5, "15:30:00");
    
    // 4. Role 2/3 lấy event ra xử lý
    for (const auto& ev : vaccineShipment.getDomainEvents()) {
        cout << "[DISPATCH EVENT] Lo hang: " << ev.getShipmentId()
             << " | Cam bien: " << ev.getSensorId()
             << " | Nhiet do vuot nguong: " << ev.getRecordedTemp() << "C"
             << " | Thoi gian: " << ev.getTimestamp() << endl;
    }
    
    // 5. Dọn dẹp hàng đợi sự kiện sau khi đã dispatch
    vaccineShipment.clearDomainEvents();
    
    return 0;
}