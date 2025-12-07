# Ultrasonic Radar System

A real-time ultrasonic radar system built with **ESP32** microcontroller using **ESP-IDF (FreeRTOS)** framework.

## Features

- **HC-SR04 Ultrasonic Sensor** for distance measurement (up to 2m)
- **SSD1351 RGB OLED Display** (128x128, SPI) for local radar visualization
- **180° Ping-Pong Sweep** animation with distance blips
- **FreeRTOS Multi-tasking** architecture for smooth, non-blocking operation
- **UART Data Streaming** to Raspberry Pi for remote dashboard
- **Flask Web Dashboard** with real-time WebSocket updates

## Hardware Requirements

### ESP32 Setup
- ESP32 Development Board
- HC-SR04 Ultrasonic Sensor
  - TRIG → GPIO 5
  - ECHO → GPIO 18
- SSD1351 128x128 RGB OLED (SPI)
  - MOSI → GPIO 13
  - CLK → GPIO 14
  - CS → GPIO 15
  - DC → GPIO 27
  - RST → GPIO 26

### Raspberry Pi Setup (Optional)
- Raspberry Pi (any model with USB)
- USB cable to ESP32

## Software Requirements

- ESP-IDF v5.5+ ([Installation Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- Python 3.7+ (for Raspberry Pi dashboard)

## Project Structure

```
ultrasonic-radar-system/
├── main/
│   └── radar_sensor.c          # Main application code
├── components/
│   ├── ultrasonic/             # HC-SR04 driver
│   ├── ssd1351_driver/         # SSD1351 OLED driver
│   └── gpio_driver/            # Legacy GPIO utilities
├── rpi_server/                 # Raspberry Pi web dashboard
│   ├── radar_server.py         # Flask + WebSocket server
│   ├── templates/
│   │   └── index.html          # Web dashboard UI
│   └── README.md               # RPi setup instructions
├── CMakeLists.txt              # ESP-IDF build config
└── sdkconfig                   # ESP32 configuration
```

## Building & Flashing (ESP32)

1. **Set up ESP-IDF environment:**
   ```bash
   . $HOME/esp/esp-idf/export.sh  # Linux/Mac
   # or
   .\export.ps1                    # Windows PowerShell
   ```

2. **Build the project:**
   ```bash
   cd ultrasonic-radar-system
   idf.py build
   ```

3. **Flash to ESP32:**
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor  # Linux
   # or
   idf.py -p COM3 flash monitor          # Windows
   ```

## Raspberry Pi Dashboard (Optional)

See `rpi_server/README.md` for detailed setup instructions.

**Quick Start:**
```bash
cd rpi_server
pip3 install flask flask-socketio pyserial
python3 radar_server.py
```

Open `http://[RPi-IP]:5000` in your browser to see the live radar dashboard.

## How It Works

1. **Sensor Task** (`sensor_task`):
   - Continuously reads distance from HC-SR04 sensor
   - Updates shared global variable (`current_distance_cm`)
   - Runs at 10Hz (100ms interval)

2. **Display Task** (`display_task`):
   - Initializes SSD1351 OLED via SPI
   - Renders 180° radar grid (circles, radial lines)
   - Animates green sweep line (ping-pong, 180°-360°)
   - Draws red blips at detected distance
   - Sends JSON data to UART: `{"angle":270,"distance":45.3}`
   - Runs at ~100Hz (10ms interval)

3. **Data Flow**:
   ```
   HC-SR04 → ESP32 (FreeRTOS) → SSD1351 OLED
                ↓
            UART (JSON)
                ↓
      Raspberry Pi (Flask) → Web Dashboard
   ```

## Technical Highlights

- **Polar to Cartesian Conversion**: Uses `cos()` and `sin()` from `<math.h>` for real-time coordinate transformations
- **Resource Management**: Resolved GPIO/SPI conflicts by moving OLED to HSPI (SPI2) bus
- **Thread Safety**: Uses `volatile` keyword for shared variables between FreeRTOS tasks
- **Performance Optimization**: Lowered SPI clock to 1MHz for stable long-wire connections

## Resume Summary

**Real-Time Ultrasonic Radar System (ESP32)**
- Developed a standalone radar system using **ESP32 microcontroller** and **ESP-IDF (FreeRTOS)** framework
- Interfaced **HC-SR04 ultrasonic sensor** for distance measurement and **SSD1351 RGB OLED** (SPI) for visualization
- Engineered multi-threaded architecture using **FreeRTOS tasks** to decouple sensor acquisition from rendering
- Implemented **polar-to-cartesian coordinate conversion** for dynamic 180° radar sweep animation
- Created **Flask web dashboard** with **WebSocket** integration for remote monitoring via Raspberry Pi
- Technologies: **C**, **FreeRTOS**, **SPI**, **UART**, **Python**, **Flask**, **WebSocket**

## License

MIT License - See existing repository license

## Author

Aneel Badesha

