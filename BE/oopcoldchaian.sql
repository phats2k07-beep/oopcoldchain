
-- 1. Thiet bi IoT
CREATE TABLE devices (
    device_id VARCHAR(50) PRIMARY KEY,
    device_type VARCHAR(30) NOT NULL,
    name VARCHAR(100),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 2. Xe van chuyen
CREATE TABLE vehicles (
    vehicle_id VARCHAR(50) PRIMARY KEY,
    license_plate VARCHAR(20) UNIQUE NOT NULL,
    vehicle_type VARCHAR(30),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 3. Container bao quan hang lanh
CREATE TABLE containers (
    container_id VARCHAR(50) PRIMARY KEY,
    container_type VARCHAR(30),
    min_temperature DOUBLE PRECISION NOT NULL,
    max_temperature DOUBLE PRECISION NOT NULL,
    CHECK (min_temperature < max_temperature)
);

-- 4. Chuyen hang
CREATE TABLE shipments (
    shipment_id VARCHAR(50) PRIMARY KEY,
    container_id VARCHAR(50) REFERENCES containers(container_id),
    vehicle_id VARCHAR(50) REFERENCES vehicles(vehicle_id),
    origin VARCHAR(100),
    destination VARCHAR(100),
    status VARCHAR(30) NOT NULL DEFAULT 'InTransit',
    started_at TIMESTAMPTZ,
    arrived_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 5. Lich su telemetry tu sensor
CREATE TABLE telemetry_readings (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL REFERENCES devices(device_id),
    temperature DOUBLE PRECISION,
    humidity DOUBLE PRECISION,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    door_open BOOLEAN,
    recorded_at TIMESTAMPTZ NOT NULL,
    received_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 6. Lich su canh bao nhiet do
CREATE TABLE temperature_alerts (
    alert_id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL REFERENCES devices(device_id),
    temperature DOUBLE PRECISION NOT NULL,
    min_temperature DOUBLE PRECISION NOT NULL,
    max_temperature DOUBLE PRECISION NOT NULL,
    alert_type VARCHAR(30) NOT NULL,
    occurred_at TIMESTAMPTZ NOT NULL,
    resolved_at TIMESTAMPTZ,
    status VARCHAR(20) NOT NULL DEFAULT 'active'
);

-- Index phuc vu truy van lich su
CREATE INDEX idx_telemetry_device_time
ON telemetry_readings(device_id, recorded_at DESC);

-- ============ SEED: dữ liệu mẫu ============

-- Thêm cột nối mỗi cảm biến (device) với một chuyến hàng (shipment)
ALTER TABLE devices
    ADD COLUMN IF NOT EXISTS shipment_id VARCHAR(50) REFERENCES shipments(shipment_id);

-- Xe vận chuyển
INSERT INTO vehicles (vehicle_id, license_plate, vehicle_type) VALUES
    ('VH-01', '29A-00001', 'reefer'),
    ('VH-02', '29A-00002', 'reefer')
ON CONFLICT (vehicle_id) DO NOTHING;

-- Container bảo quản (giữ ngưỡng nhiệt độ)
INSERT INTO containers (container_id, container_type, min_temperature, max_temperature) VALUES
    ('CT-VAC',    'vaccine',  2.0,   8.0),
    ('CT-FROZEN', 'frozen',  -20.0, -15.0),
    ('CT-RISK',   'vaccine',  2.0,   8.0),
    ('CT-INSULIN', 'insulin',  2.0,   8.0),
    ('CT-FOOD',   'food',     0.0,  10.0),
    ('CT-DRUG',   'drug',     2.0,   8.0),
    ('CT-OTHER',  'other',   -10.0,  15.0),
    ('CT-EMPTY',  'empty',   -50.0,  50.0),
    ('CT-TEST',   'test',     0.0,  10.0)
ON CONFLICT (container_id) DO NOTHING;

-- Chuyến hàng đang vận chuyển (status = 'InTransit' để Simulator quét)
INSERT INTO shipments (shipment_id, container_id, vehicle_id, origin, destination, status, started_at) VALUES
    ('SHP-VAC-01',    'CT-VAC',    'VH-01', 'Ha Noi', 'Da Nang',   'InTransit', NOW()),
    ('SHP-FROZEN-01', 'CT-FROZEN', 'VH-02', 'Ha Noi', 'Hai Phong', 'InTransit', NOW()),
    ('SHP-RISK-01',   'CT-RISK',   'VH-01', 'Hue',    'Da Nang',   'InTransit', NOW())
ON CONFLICT (shipment_id) DO NOTHING;

-- Thiết bị IoT (cảm biến) — mỗi đơn 1 thiết bị, đã gắn shipment_id
INSERT INTO devices (device_id, device_type, name, shipment_id) VALUES
    ('DEV-V01', 'sensor', 'Cảm biến vắc-xin 01',   'SHP-VAC-01'),
    ('DEV-F01', 'sensor', 'Cảm biến đông lạnh 01', 'SHP-FROZEN-01'),
    ('DEV-R01', 'sensor', 'Cảm biến rủi ro 01',    'SHP-RISK-01')
ON CONFLICT (device_id) DO NOTHING;
