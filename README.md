#  Desmo-Telemetry System

**High-Frequency Distributed IoT Telemetry for Automotive Fleets.**

![Status](https://img.shields.io/badge/status-active-success.svg)

**Desmo** is an end-to-end telemetry pipeline designed to capture, decode, and visualize real-time vehicle physics data. It simulates a fleet of vehicles using a C++ physics engine, transmits data via a custom binary protocol over MQTT, and ingests it using a concurrent Go backend into a Time-Series Database.



##  Architecture



The system follows a decoupled **Edge-to-Cloud** architecture:

1.  **The Edge (C++):** A physics simulation engine that models engine RPM, torque curves, thermal dynamics, and battery stress. It packs data into a strict 32-byte binary struct.
2.  **The Transport (MQTT):** Uses **Eclipse Mosquitto** as the broker. Data is sent as raw binary payloads (QoS 1) to minimize bandwidth.
3.  **The Ingestor (Go):** A high-concurrency backend service.
    * **Worker Pool Pattern:** Decouples network reading from processing.
    * **Binary Decoder:** Manually unpacks Big-Endian bytes into Go structs.
    * **Backpressure:** Drops packets gracefully if the database write queue fills up.
4.  **The Storage (InfluxDB):** Time-series storage optimized for high-write workloads.
5.  **The Visualization (Grafana):** Real-time dashboards monitoring Speed, RPM, and Critical Alerts (Overheat, ABS, Panic Stops).


##  Tech Stack

* **Simulation:** C++17 (STL, Multithreading, Sockets)
* **Ingestion:** Go 1.21+ (Goroutines, Channels, Paho MQTT)
* **Broker:** Eclipse Mosquitto
* **Database:** InfluxDB v2
* **Visualization:** Grafana
* **Infrastructure:** Docker & Docker Compose


##  Features

* **Custom Binary Protocol:** 32-byte fixed-size packets. No JSON overhead.
* **Stochastic Simulation:** Vehicles exhibit "Personality" (Aggressive, City Cruising, Panic Braking, Highway Sprint) using non-deterministic state machines.
* **Concurrency Safe:** Go backend handles multiple vehicle streams simultaneously using a fan-out worker pool.
* **Fault Tolerance:**
    * **Auto-Reconnect:** Services survive broker restarts.
    * **Graceful Shutdown:** Context-aware signal handling ensures DB writes are flushed before exit.
    * **Environment Security:** Secrets managed via `.env` and Docker secrets.


##  Getting Started

### Prerequisites
* Docker & Docker Compose
* Go 1.21+
* C++ Compiler
* Git

### 1. Start Infrastructure
Spin up the Broker, Database, and Dashboard.
```bash
docker-compose up -d
```
### 2. Configure Environment
Create a `.env` file in the `backend/` directory:

``` ini
BROKER_URL=tcp://127.0.0.1:1883
TOPIC=fleet/+/telemetry
INFLUX_URL=http://localhost:8086
INFLUX_ORG=DesmoTelemetry
INFLUX_BUCKET=Telemetry
INFLUX_TOKEN=your-token-from-docker-compose-or-influx-ui
WORKER_COUNT=50
```

### 3. Run the Backend (Ingestor)
```Bashcd backend
go mod tidy
go run main.go
```
You should see: "Connected to Broker & Database"
### 4. Launch the Fleet (Edge Simulation)
Open a new terminal to compile and run the simulation.
```Bash
# Compile (Example using g++)
g++ -o fleet_sim src/main.cpp src/vehicle.cpp src/mqtt_forge.cpp -I include -lpthread

# Run Vehicle 101
./fleet_sim 101

# (Optional) Run Vehicle 102 in another terminal
./fleet_sim 102
```
## Protocol Specification
The system uses a custom 32-Byte Big-Endian packet structure.

|Offset|Field|Type|Description|
|---|---|---|---|
|0x00|Magic|``````uint16``````|Protocol ID (0xD350)|
|0x02|VehicleID|```uint16```|Unique Fleet ID|
|0x04|SeqID|```uint32```|Packet Sequence (Loss detection)|
|0x08|Timestamp|```uint64```|Unix Epoch (ms)|
|0x10|RPM|```uint16```|Engine Speed|
|0x12|Speed|```uint16```|Velocity (km/h)|
|0x14|Jerk|```int8```|Derivative of Accel (G-Force)|
|0x16|Temp|```uint8```|Engine Temp (°C)|
|0x17|Battery|```uint8```|State of Charge (%)|
|0x18|Gear|```uint8```|Current Gear (1-6)|
|0x19|Flags|```uint8```|"Bitmask (ABS, Overheat, etc.)"|
|0x1A|Version|```uint8```|Protocol Version|
|0x1B|CPULoad|```uint8```|ECU Load %|
|0x1C|CRC16|```uint16```|Data Integrity Checksum|
|0x1E|Padding|```uint8```[2]|Alignment|



## Author

Built with ☕ and C++.
