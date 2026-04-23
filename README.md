# Ultrasonic Radar System

A real-time ultrasonic radar system built on the **ESP32** (ESP-IDF / FreeRTOS) with a **Flask + WebSocket** dashboard. The ESP32 sweeps an HC-SR04 ultrasonic sensor, renders a 180° radar on a local OLED, and streams samples over WiFi to a Raspberry Pi (or any host) running the web dashboard.

## Features

- **HC-SR04 Ultrasonic Sensor** for distance measurement (up to 2 m)
- **SSD1351 RGB OLED Display** (128x128, SPI) for local radar visualization
- **180° Ping-Pong Sweep** animation with distance blips
- **FreeRTOS Multi-tasking** — separate sensor, display, and HTTP-retry tasks
- **WiFi HTTP telemetry** from the ESP32 to the dashboard (JSON POST)
- **Resilient delivery** — bounded retry queue with exponential backoff, jitter, and periodic stats logging
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
│   ├── radar_sensor.c          # Main application (WiFi, tasks, HTTP retry, rendering)
│   └── CMakeLists.txt
├── components/
│   ├── ultrasonic/             # HC-SR04 driver
│   ├── ssd1351_driver/         # SSD1351 OLED driver
│   └── gpio_driver/            # Legacy GPIO utilities
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

1. **`sensor_task`** — reads the HC-SR04 at ~10 Hz, converts the measurement to centimeters, and publishes it to `current_distance_cm` under a mutex (`-1.0` on no echo).
2. **`display_task`** — runs at ~100 Hz:
   - Draws the radar grid (concentric circles, radial lines) on the SSD1351.
   - Advances a green sweep line between 0° and 180° (ping-pong).
   - Reads the latest distance under the mutex and, if valid, paints a red blip along the current sweep angle.
   - If WiFi is up, POSTs `{angle, distance}` to the dashboard. The reported angle is normalized to `[0, 180]` so it matches the server's validation domain.
   - On `5xx` or transport errors, enqueues the sample on the retry queue.
3. **`http_retry_task`** — drains the retry queue with exponential backoff (300 ms → 3000 ms cap) plus ±100 ms jitter. Drops the oldest item if the queue saturates, tracks per-outcome counters, and logs a stats line every 5 s:
   ```
   HTTP stats 2xx=… 4xx=… 5xx=… transport=… queued_retries=… exhausted=…
   ```

### Data flow

```
HC-SR04 ─► ESP32 (FreeRTOS) ─► SSD1351 OLED
                │
                ├─ WiFi HTTP POST (with retry/backoff) ─► Flask /api/radar
                │                                            │
                │                                            ▼
                │                                       Socket.IO broadcast
                │                                            │
                │                                            ▼
                └────────────────────────────────────►  Web dashboard
```

### Thread safety

- `current_distance_cm` is protected by a FreeRTOS mutex (`distance_mutex`) shared between `sensor_task` and `display_task`.
- `current_angle`, `wifi_connected`, and the HTTP telemetry counters use C11 `<stdatomic.h>` atomics.
- The retry queue is a FreeRTOS `QueueHandle_t`; WiFi connection state is signaled through an `EventGroup`.

## Technical Highlights

- **Resilient telemetry pipeline** — bounded retry queue with exponential backoff + jitter, oldest-drop overflow policy, and outcome-class metrics (2xx / 4xx / 5xx / transport / exhausted).
- **Decoupled rendering and acquisition** — dedicated FreeRTOS tasks keep the OLED animation smooth while sensor reads and network I/O run independently.
- **Validated API surface** — the Flask server rejects malformed payloads with structured 4xx errors instead of storing bad data.
- **Polar-to-Cartesian conversion** with `cos()` / `sin()` for the sweep animation and blip placement.
- **SPI bus tuning** — OLED on HSPI (SPI2) with a 1 MHz clock for stable long-wire connections; avoids conflicts with the sensor GPIOs.

## Summary

**Real-Time Ultrasonic Radar System (ESP32)**
- Built a standalone radar system on the **ESP32** using **ESP-IDF (FreeRTOS)**, interfacing an **HC-SR04** ultrasonic sensor and an **SSD1351 RGB OLED** over SPI.
- Designed a multi-task architecture (sensor / display / HTTP-retry) with mutex- and atomic-based synchronization to decouple acquisition, rendering, and network I/O.
- Implemented a **resilient WiFi telemetry pipeline** — JSON HTTP POST with a bounded retry queue, exponential backoff + jitter, and per-outcome metrics logging.
- Built a **Flask + Socket.IO dashboard** with a validated ingress API and live browser updates over WebSocket.
- Rendered a 180° ping-pong radar sweep with polar-to-cartesian coordinate math and real-time blip placement.
- Technologies: **C**, **FreeRTOS**, **ESP-IDF**, **SPI**, **WiFi / HTTP**, **Python**, **Flask**, **Socket.IO**.

## License

MIT License — see the repository license file.

## Author

Aneel Badesha
