import serial
import json
import threading
import time
from flask import Flask, request, jsonify

# ------------------------------
# CONFIGURATION
# ------------------------------
SERIAL_PORT = "COM3"       # <-- change to your Arduino COM port
BAUD_RATE = 9600
HTTP_PORT = 8081

# ------------------------------
# LATEST STATE (Matches Arduino JSON)
# ------------------------------
latest_state = {
    "floor": 0,
    "target": 0,
    "dir": 0,
    "door": 0,
    "state": "Idle",

    # Stats fields
    "totalTrips": 0,
    "stopCount": 0,
    "doorCycles": 0,
    "avgTripMs": 0,
    "avgWaitMs": 0,
    "travelDistanceFloors": 0,
    "uptimeMs": 0
}

# ------------------------------
# SERIAL CONNECTION (Auto-retry)
# ------------------------------
def connect_serial():
    while True:
        try:
            print(f"Connecting to Arduino on {SERIAL_PORT}...")
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            print("Connected!")
            return ser
        except Exception as e:
            print(f"Failed ({e}), retrying...")
            time.sleep(2)

arduino = connect_serial()


# ------------------------------
# SERIAL READER THREAD
# ------------------------------
def serial_reader():
    global latest_state
    buffer = ""

    while True:
        try:
            ch = arduino.read().decode(errors="ignore")

            if ch == "":
                time.sleep(0.01)
                continue

            if ch == "\n":
                line = buffer.strip()
                buffer = ""

                # Must be JSON
                if not (line.startswith("{") and line.endswith("}")):
                    continue

                try:
                    data = json.loads(line)

                    # Update only known keys
                    for key in latest_state:
                        if key in data:
                            latest_state[key] = data[key]

                except json.JSONDecodeError:
                    print("Invalid JSON:", line)

                continue

            buffer += ch

        except Exception as e:
            print("Serial read error:", e)
            time.sleep(1)

# Start background reader thread
threading.Thread(target=serial_reader, daemon=True).start()


# ------------------------------
# FLASK API
# ------------------------------
app = Flask(__name__)

@app.get("/state")
def get_state():
    """Return full elevator state (floor, door, direction, target, state, stats)."""
    return jsonify(latest_state)


@app.get("/stats")
def get_stats():
    """Return only statistics fields."""
    return jsonify({
        "totalTrips": latest_state["totalTrips"],
        "stopCount": latest_state["stopCount"],
        "doorCycles": latest_state["doorCycles"],
        "avgTripMs": latest_state["avgTripMs"],
        "avgWaitMs": latest_state["avgWaitMs"],
        "travelDistanceFloors": latest_state["travelDistanceFloors"],
        "uptimeMs": latest_state["uptimeMs"],
    })


@app.get("/command")
def send_command():
    """
    Send movement command to Arduino.
    Example: /command?floor=3 → sends GOTO:3\n
    """
    floor = request.args.get("floor")

    if floor is None:
        return {"error": "floor argument required"}, 400

    msg = f"GOTO:{floor}\n"

    try:
        arduino.write(msg.encode())
        return {"success": True, "sent": msg}
    except Exception as e:
        return {"error": f"failed to send command: {e}"}, 500


@app.get("/")
def root():
    return {
        "status": "bridge server running",
        "serial_port": SERIAL_PORT,
        "http_port": HTTP_PORT
    }


# ------------------------------
# START SERVER
# ------------------------------
if __name__ == "__main__":
    print(f"Bridge server running at http://localhost:{HTTP_PORT}")
    print(f"Listening to Arduino on {SERIAL_PORT}")
    app.run(host="0.0.0.0", port=HTTP_PORT, debug=False)