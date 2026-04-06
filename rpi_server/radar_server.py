#!/usr/bin/env python3
"""
Raspberry Pi Flask Server for Ultrasonic Radar Visualization
Receives angle and distance data from ESP32 via WiFi HTTP POST and displays it on a web dashboard.
"""

from flask import Flask, render_template, jsonify, request
from flask_socketio import SocketIO
import time
import threading

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

# Global data storage
radar_data = {
    'angle': 180,
    'distance': -1.0,
    'timestamp': time.time()
}
radar_data_lock = threading.Lock()

@app.route('/api/radar', methods=['POST'])
def receive_radar_data():
    """API endpoint to receive radar data from ESP32 via WiFi."""
    try:
        data = request.get_json()
        with radar_data_lock:
            radar_data['angle'] = data.get('angle', 180)
            radar_data['distance'] = data.get('distance', -1.0)
            radar_data['timestamp'] = time.time()
            snapshot = dict(radar_data)

        # Broadcast to all connected clients (outside lock)
        socketio.emit('radar_update', snapshot)

        return jsonify({'status': 'success'}), 200
    except Exception as e:
        print(f"Error receiving data: {e}")
        return jsonify({'status': 'error', 'message': str(e)}), 400

@app.route('/')
def index():
    """Serve the main dashboard page."""
    return render_template('index.html')

@app.route('/api/data')
def get_data():
    """API endpoint to get current radar data."""
    return jsonify(radar_data)

if __name__ == '__main__':
    # Start Flask server
    print("Starting Radar Dashboard Server...")
    print("Open http://localhost:5000 in your browser")
    print("ESP32 should POST data to http://[YOUR_IP]:5000/api/radar")
    socketio.run(app, host='0.0.0.0', port=5000, debug=False)
