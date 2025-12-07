# Raspberry Pi Radar Dashboard Setup

## Installation

1. Install Python dependencies:
```bash
pip3 install flask flask-socketio pyserial
```

2. Find your USB-UART device:
```bash
ls /dev/ttyUSB*
# or
ls /dev/ttyACM*
```

3. Update `radar_server.py` if needed:
```python
SERIAL_PORT = '/dev/ttyUSB0'  # Change this to your port
```

4. Give yourself permission to access the serial port:
```bash
sudo usermod -a -G dialout $USER
# Then log out and back in
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

Connect your ESP32 USB cable to the Raspberry Pi. The ESP32 will send JSON data over UART0:
```json
{"angle":270,"distance":45.3}
```

## Troubleshooting

### Port Permission Denied
```bash
sudo chmod 666 /dev/ttyUSB0
```

### Port Not Found
Check with `dmesg | grep tty` after plugging in the ESP32.

### No Data
- Verify ESP32 is flashed with the latest code
- Check baud rate matches (115200)
- Test serial connection: `cat /dev/ttyUSB0`
