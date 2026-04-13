#!/usr/bin/env python3
"""
Raspberry Pi Flask Server for Ultrasonic Radar Visualization
Receives angle and distance data from ESP32 via WiFi HTTP POST and displays it on a web dashboard.
"""

from flask import Flask, render_template, jsonify, request
from flask_socketio import SocketIO
import time
import threading
import logging

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")
logger = logging.getLogger(__name__)

# Global data storage
radar_data = {
    'angle': 180,
    'distance': -1.0,
    'timestamp': time.time()
}
radar_data_lock = threading.Lock()


def _error_response(message, code, status):
    return jsonify({'status': 'error', 'code': code, 'message': message}), status


def _validate_radar_payload(data):
    if data is None:
        raise ValueError("Request body must be valid JSON.")
    if not isinstance(data, dict):
        raise ValueError("JSON payload must be an object.")
    if 'angle' not in data or 'distance' not in data:
        raise ValueError("JSON payload must include 'angle' and 'distance'.")

    try:
        angle = int(data['angle'])
    except (TypeError, ValueError):
        raise ValueError("'angle' must be an integer.")

    try:
        distance = float(data['distance'])
    except (TypeError, ValueError):
        raise ValueError("'distance' must be a number.")

    if angle < 0 or angle > 180:
        raise ValueError("'angle' must be between 0 and 180.")
    if distance < -1.0:
        raise ValueError("'distance' must be -1.0 (no echo) or a non-negative value.")

    return angle, distance

@app.route('/api/radar', methods=['POST'])
def receive_radar_data():
    """API endpoint to receive radar data from ESP32 via WiFi."""
    data = request.get_json(silent=True)
    try:
        angle, distance = _validate_radar_payload(data)
        with radar_data_lock:
            radar_data['angle'] = angle
            radar_data['distance'] = distance
            radar_data['timestamp'] = time.time()
            snapshot = dict(radar_data)

    except ValueError as e:
        return _error_response(str(e), 'invalid_payload', 400)
    except Exception:
        logger.exception("Unexpected error while processing radar data")
        return _error_response("Failed to process radar data.", 'internal_error', 500)

    # Broadcast outside lock; data is already stored even if broadcast fails.
    try:
        socketio.emit('radar_update', snapshot)
    except Exception:
        logger.exception("Radar data saved, but failed to broadcast update")
        return jsonify({
            'status': 'success',
            'warning': 'data_saved_broadcast_failed'
        }), 202

    return jsonify({'status': 'success'}), 200

@app.route('/')
def index():
    """Serve the main dashboard page."""
    return render_template('index.html')

@app.route('/api/data')
def get_data():
    """API endpoint to get current radar data."""
    with radar_data_lock:
        snapshot = dict(radar_data)
    return jsonify(snapshot)

if __name__ == '__main__':
    # Start Flask server
    print("Starting Radar Dashboard Server...")
    print("Open http://localhost:5000 in your browser")
    print("ESP32 should POST data to http://[YOUR_IP]:5000/api/radar")
    socketio.run(app, host='0.0.0.0', port=5000, debug=False)
