# OOP Cold Chain

## Requirements

- Windows with MSYS2 MinGW 64-bit installed at `C:\msys64`
- PostgreSQL running with a database named `coldchain`

Update MSYS2 and install the matching compiler and libraries from the **MSYS2 MinGW 64-bit** environment:

```text
pacman -Syu
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-asio mingw-w64-x86_64-libpqxx mingw-w64-x86_64-postgresql mingw-w64-x86_64-curl
```

If `pacman -Syu` asks you to close the terminal, reopen the MinGW 64-bit terminal and run it again before installing packages. Build and run the executables with the same MSYS2 installation so Windows loads compatible runtime DLLs.

## Configure PostgreSQL

From PowerShell at the project root, set the connection string for the current terminal. Replace the placeholder with your PostgreSQL password; do not put real credentials in source files or commit them:

```powershell
$env:COLDCHAIN_DSN = 'host=127.0.0.1 port=5432 dbname=coldchain user=postgres password=<YOUR_PASSWORD>'
```

Initialize the database schema and sample data once:

```powershell
& 'C:\msys64\mingw64\bin\psql.exe' $env:COLDCHAIN_DSN -v ON_ERROR_STOP=1 -f 'BE\oopcoldchaian.sql'
```

The schema script creates the tables and inserts sample vehicles, containers, shipments, and devices. Do not rerun it on a database that already contains these tables.

## Build

Run these commands from the project root in PowerShell:

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O2 -Wall -Wextra -IC:\msys64\mingw64\include -LC:\msys64\mingw64\lib 'BE\BE.cpp' -o 'BE\output\BE.exe' -lpqxx -lpq -lws2_32 -lmswsock
& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O2 -Wall -Wextra -IC:\msys64\mingw64\include -LC:\msys64\mingw64\lib 'Simulator\Simulator.cpp' -o 'Simulator\output\Simulator.exe' -lpqxx -lpq -lcurl -lws2_32 -lmswsock
& 'C:\msys64\mingw64\bin\g++.exe' -std=c++20 -O2 -Wall -Wextra 'domain\DomainModel.cpp' -o 'domain\DomainModel.exe'
```

## Run

Keep the same PowerShell terminal open so `COLDCHAIN_DSN` and the MSYS2 DLL path remain set. Start the backend:

```powershell
& '.\BE\output\BE.exe'
```

In a second PowerShell terminal, set `COLDCHAIN_DSN` and the MSYS2 DLL path there too, then start the simulator:

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
$env:COLDCHAIN_DSN = 'host=127.0.0.1 port=5432 dbname=coldchain user=postgres password=<YOUR_PASSWORD>'
& '.\Simulator\output\Simulator.exe'
```

The backend listens on port `8080`; the simulator posts sensor readings to it. The API includes `GET /api/shipments`, `GET /api/shipment-options`, and `GET /api/shipments/{shipment_id}/telemetry`.

The UI can create shipments with `POST /api/shipments` using a JSON body containing string fields `container_type`, `origin`, and `destination`. Sensor readings are accepted at `POST /api/sensor-data` with `sensor_id`, `shipment_id`, `timestamp`, `status`, `temperature`, and `humidity`; `latitude` and `longitude` are optional. The backend checks that the sensor belongs to the shipment and evaluates temperature violations against the container's configured range.

## Dashboard

Rebuild and restart the backend after changes so its local-origin CORS headers and WebSocket telemetry broadcast are active. A `200` response from `/api/shipments` alone does not confirm that the running executable has CORS enabled; the response must include `Access-Control-Allow-Origin: http://127.0.0.1:5500` when requested from the dashboard origin. From the project root, serve `index.html` over localhost (rather than opening it as a `file://` URL):

```powershell
python -m http.server 5500
```

Open `http://127.0.0.1:5500` in a browser. Keep the backend running on port `8080`. The dashboard loads shipment and telemetry history from the API and receives every accepted sensor reading over `/ws`.
