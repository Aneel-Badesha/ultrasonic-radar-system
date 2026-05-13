# Ultrasonic Radar System

A real-time ultrasonic radar system built on the **ESP32** (ESP-IDF / FreeRTOS) with a **Flask + WebSocket** dashboard. The ESP32 sweeps an HC-SR04 ultrasonic sensor, renders a 180° radar on a local OLED, and streams samples over WiFi to a Raspberry Pi (or any host) running the web dashboard.

## Features

- **HC-SR04 Ultrasonic Sensor** for distance measurement (up to 2 m), with plausibility checks and a 3-sample median filter
- **SSD1351 RGB OLED Display** (128x128, SPI) for local radar visualization
- **180° Ping-Pong Sweep** animation with distance blips
- **FreeRTOS Multi-tasking** — sensor, display, and HTTP-uploader tasks decoupled via a mutex and a send queue
- **Task watchdog (TWDT)** subscribed on every task with panic-on-timeout, plus stack-canary overflow detection
- **WiFi HTTP telemetry** from the ESP32 to the dashboard (JSON POST)
- **WiFi station component** with auto-reconnect on disconnect and a bring-up timeout that reboots if the first connection fails
- **Resilient delivery** — unified send queue with exponential backoff, jitter, drop-oldest overflow policy, and periodic stats logging
- **Modular component layout** — WiFi station, HTTP uploader, HC-SR04 driver, and OLED driver are each reusable ESP-IDF components
- **Flask + Socket.IO dashboard** with real-time browser updates and validated API

## Hardware Requirements

### ESP32 Setup
- ESP32 development board (2.4 GHz WiFi)
- HC-SR04 Ultrasonic Sensor
  - TRIG → GPIO 5
  - ECHO → GPIO 18
- SSD1351 128x128 RGB OLED (SPI / HSPI)
  - MOSI → GPIO 13
  - CLK  → GPIO 14
  - CS   → GPIO 15
  - DC   → GPIO 27
  - RST  → GPIO 26

### Dashboard Host
- Any host on the same network (Raspberry Pi, laptop, etc.)
- Python 3.7+

## Software Requirements

- ESP-IDF v5.5+ ([installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- Python 3.7+ with `flask` and `flask-socketio`

## Project Structure

```
ultrasonic-radar-system/
├── main/
│   ├── radar_sensor.c          # App entry point, sensor and display tasks, radar rendering
│   └── CMakeLists.txt
├── components/
│   ├── ultrasonic/             # HC-SR04 driver
│   ├── ssd1351_driver/         # SSD1351 OLED driver, includes generic line and circle primitives
│   ├── wifi_sta/               # WiFi station bring-up, event-driven reconnect, is-connected accessor
│   └── http_uploader/          # Queue-backed HTTP POST worker with exponential backoff and stats
├── rpi_server/
│   ├── radar_server.py         # Flask + Socket.IO server with validated /api/radar
│   ├── templates/index.html    # Web dashboard UI
│   └── README.md               # Dashboard setup instructions
├── CMakeLists.txt              # ESP-IDF project config
└── sdkconfig                   # ESP32 configuration
```

## Configuration

Before flashing, edit WiFi credentials and the dashboard URL at the top of [main/radar_sensor.c](main/radar_sensor.c):

```c
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
#define RPI_SERVER_URL "http://<dashboard-host-ip>:5000/api/radar"
```

> Only 2.4 GHz networks are supported — the ESP32 radio cannot join 5 GHz SSIDs.

## Building & Flashing (ESP32)

1. **Set up the ESP-IDF environment:**
   ```bash
   . $HOME/esp/esp-idf/export.sh   # Linux / macOS
   # or
   .\export.ps1                    # Windows PowerShell
   ```

2. **Build:**
   ```bash
   idf.py build
   ```

3. **Flash and monitor:**
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor   # Linux
   idf.py -p COM3 flash monitor           # Windows
   ```

## Dashboard Server

See [rpi_server/README.md](rpi_server/README.md) for the full walkthrough.

**Quick start:**
```bash
cd rpi_server
pip3 install flask flask-socketio
python3 radar_server.py
```

Then open `http://<dashboard-host-ip>:5000` in a browser.

### HTTP API

- `POST /api/radar` — ESP32 telemetry ingress. JSON body:
  ```json
  {"angle": 90, "distance": 45.3}
  ```
  `angle` is an integer in `[0, 180]`. `distance` is a float in centimeters, or `-1.0` when no echo was received. Invalid payloads return `400` with a structured error body.
- `GET /api/data` — most recent sample as JSON.
- `GET /` — dashboard HTML page. Socket.IO pushes `radar_update` events as samples arrive.

## How It Works

### Tasks (FreeRTOS)

1. **`sensor_task`** (in `main/`) — reads the HC-SR04 at ~10 Hz, classifies the driver error code, applies a plausibility window (`MIN_DISTANCE_CM` ≤ d ≤ `MAX_DISTANCE_CM`), runs a 3-sample median filter, and publishes the result to `current_sample` (distance + error class + timestamp) under a mutex. The same sample is also handed to `http_uploader_enqueue` with the angle captured at the measurement instant.
2. **`display_task`** (in `main/`) — runs at ~100 Hz, purely as a renderer:
   - Draws the radar grid (concentric circles, radial lines) on the SSD1351 via `draw_radar_grid`.
   - Advances a green sweep line between 180° and 360° (ping-pong, matching the screen's Y-down orientation).
   - Snapshots `current_sample` under the mutex, skips the frame's blip if the mutex isn't acquired within 50 ms, rejects readings older than `SENSOR_FRESHNESS_TIMEOUT_MS`, and paints a red blip along the current sweep angle if the distance is valid.
3. **`http_uploader_task`** (in the `http_uploader` component) — drains the internal queue for both first attempts (no delay) and retries (exponential backoff 300 ms → 3000 ms cap, ±100 ms jitter). Drops the oldest item on queue overflow, short-circuits when `wifi_sta_is_connected()` reports false, classifies outcomes into 2xx / 4xx / 5xx / transport (4xx is not retried), and logs a stats line every 5 s:
   ```
   HTTP stats 2xx=… 4xx=… 5xx=… transport=… enqueued=… dropped=… exhausted=…
   ```

All three tasks subscribe to the ESP-IDF task watchdog (TWDT) and feed it once per loop iteration; `CONFIG_ESP_TASK_WDT_PANIC` is enabled so a hang produces a real panic + backtrace + reset rather than a silent warning.

### Data flow

```
HC-SR04 ─► sensor_task ─► current_sample (mutex)         ─► display_task ─► SSD1351 OLED
                       │
                       └─► http_uploader_enqueue ─► http_uploader_task ─► Flask /api/radar
                                                                                │
                                                                                ▼
                                                                          Socket.IO broadcast
                                                                                │
                                                                                ▼
                                                                          Web dashboard
```

### Thread safety

- `current_sample` (distance, error class, timestamp) is protected by a FreeRTOS mutex (`distance_mutex`) shared between `sensor_task` (writer) and `display_task` (reader).
- `current_angle` and the HTTP telemetry counters use C11 `<stdatomic.h>` atomics.
- The uploader queue is a FreeRTOS `QueueHandle_t` (private to the `http_uploader` component) with non-blocking enqueue and drop-oldest overflow.
- WiFi connection state is encapsulated in the `wifi_sta` component: an `EventGroup` unblocks the initial bring-up, an internal atomic backs the public `wifi_sta_is_connected()` accessor, and the event handler auto-reconnects on disconnect.

## Technical Highlights

- **Modular component layout** — the project is split into four ESP-IDF components (`ultrasonic`, `ssd1351_driver`, `wifi_sta`, `http_uploader`), each with a clean public API in its own header so the main app stays focused on radar logic.
- **Resilient telemetry pipeline** — the `http_uploader` component handles both first attempts and retries through one queue, with exponential backoff + jitter, drop-oldest overflow, and outcome-class metrics (2xx / 4xx / 5xx / transport / enqueued / dropped / exhausted).
- **Decoupled rendering and acquisition** — display task does zero network I/O; sensor task is the single producer feeding both the OLED renderer and the HTTP uploader.
- **Sensor-side robustness** — driver-level GPIO validation, plausibility window rejecting readings outside the HC-SR04's usable range, 3-sample median filter to kill single-sample glitches, and a freshness check on the consumer to detect a stalled sensor before the TWDT fires.
- **Validated API surface** — the Flask server rejects malformed payloads with structured 4xx errors instead of storing bad data.
- **Polar-to-Cartesian conversion** with `cos()` / `sin()` for the sweep animation and blip placement.
- **SPI bus tuning** — OLED on HSPI (SPI2) with a 1 MHz clock for stable long-wire connections, avoiding conflicts with the sensor GPIOs.

## Summary

**Real-Time Ultrasonic Radar System (ESP32)**
- Built a standalone radar system on the **ESP32** using **ESP-IDF (FreeRTOS)**, interfacing an **HC-SR04** ultrasonic sensor and an **SSD1351 RGB OLED** over SPI.
- Organised the firmware as four reusable **ESP-IDF components** (ultrasonic driver, OLED driver, WiFi station, HTTP uploader) plus a slim main app for radar-specific rendering and orchestration.
- Designed a producer/consumer architecture: `sensor_task` is the sole producer feeding `display_task` (mutex-protected snapshot) and `http_uploader_task` (bounded queue), with mutex- and atomic-based synchronization throughout.
- Implemented a **resilient WiFi telemetry pipeline** — JSON HTTP POST with a unified send queue, exponential backoff + jitter, and per-outcome metrics logging.
- Hardened the firmware with **task watchdog (TWDT) panic-on-timeout, stack-canary overflow detection, WiFi bring-up timeout reboot, plausibility checks, and median filtering** to surface failures loudly instead of letting them rot silently.
- Built a **Flask + Socket.IO dashboard** with a validated ingress API and live browser updates over WebSocket.
- Rendered a 180° ping-pong radar sweep with polar-to-cartesian coordinate math and real-time blip placement.
- Technologies: **C**, **FreeRTOS**, **ESP-IDF**, **SPI**, **WiFi / HTTP**, **Python**, **Flask**, **Socket.IO**.


## Author

Aneel Badesha
