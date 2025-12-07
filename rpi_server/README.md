# Raspberry Pi Radar Dashboard Setup

## Installation

1. Install Python dependencies:
```bash
pip3 install flask flask-socketio
```

2. Find your Raspberry Pi's IP address:
```bash
hostname -I
```

3. Update ESP32 code with your WiFi credentials and RPi IP:
```c
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
#define RPI_SERVER_URL "http://192.168.1.100:5000/api/radar"  // Change to your RPi IP
```

## Running the Server

```bash
cd rpi_server
python3 radar_server.py
```

The dashboard will be available at:
- Local: http://localhost:5000
- Network: http://[YOUR_RPI_IP]:5000

## ESP32 Connection

The ESP32 connects to your WiFi network and sends HTTP POST requests with JSON data:
```json
{"angle":270,"distance":45.3}
```

To endpoint: `http://[YOUR_RPI_IP]:5000/api/radar`

## Troubleshooting

### ESP32 Won't Connect to WiFi
- Double-check SSID and password in `main/radar_sensor.c`
- Ensure your WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
- Check serial monitor for WiFi connection logs

### No Data on Dashboard
- Verify RPi and ESP32 are on the same network
- Check ESP32 is using correct RPi IP address
- Look for HTTP POST errors in ESP32 serial output
- Test the endpoint: `curl -X POST http://[RPI-IP]:5000/api/radar -H "Content-Type: application/json" -d '{"angle":270,"distance":50}'`

### Firewall Issues
```bash
sudo ufw allow 5000/tcp
```
