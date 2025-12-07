#!/usr/bin/env python3
"""
Raspberry Pi Flask Server for Ultrasonic Radar Visualization
Receives angle and distance data from ESP32 via UART and displays it on a web dashboard.
"""

from flask import Flask, render_template, jsonify
from flask_socketio import SocketIO
import serial
import json
import threading
import time

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

# Serial configuration
SERIAL_PORT = '/dev/ttyUSB0'  # Change to your USB-UART adapter port
BAUD_RATE = 115200

# Global data storage
radar_data = {
    'angle': 180,
    'distance': -1.0,
    'timestamp': time.time()
}

def read_serial():
    """Background thread to read serial data from ESP32."""
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"Connected to {SERIAL_PORT} at {BAUD_RATE} baud")
        
        while True:
            try:
                line = ser.readline().decode('utf-8').strip()
                if line:
                    # Parse JSON data: {"angle":270,"distance":45.3}
                    data = json.loads(line)
                    radar_data['angle'] = data.get('angle', 180)
                    radar_data['distance'] = data.get('distance', -1.0)
                    radar_data['timestamp'] = time.time()
                    
                    # Broadcast to all connected clients
                    socketio.emit('radar_update', radar_data)
                    
            except json.JSONDecodeError:
                print(f"Invalid JSON: {line}")
            except Exception as e:
                print(f"Serial read error: {e}")
                
    except serial.SerialException as e:
        print(f"Could not open serial port {SERIAL_PORT}: {e}")
        print("Please check your USB connection and port name.")

@app.route('/')
def index():
    """Serve the main dashboard page."""
    return render_template('index.html')

@app.route('/api/data')
def get_data():
    """API endpoint to get current radar data."""
    return jsonify(radar_data)

if __name__ == '__main__':
    # Start serial reader thread
    serial_thread = threading.Thread(target=read_serial, daemon=True)
    serial_thread.start()
    
    # Start Flask server
    print("Starting Radar Dashboard Server...")
    print("Open http://localhost:5000 in your browser")
    socketio.run(app, host='0.0.0.0', port=5000, debug=False)
