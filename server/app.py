import csv
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, jsonify, render_template, request


BASE_DIR = Path(__file__).resolve().parents[1]
DATA_DIR = BASE_DIR / "data"
CSV_PATH = DATA_DIR / "sensor_readings.csv"

FIELDNAMES = [
    "timestamp",
    "device_id",
    "temperature_c",
    "humidity_percent",
    "motion",
    "motion_event_count",
    "mq135_raw",
    "uptime_ms",
    "sensor_status",
]

DATA_DIR.mkdir(exist_ok=True)

app = Flask(__name__, template_folder="templates", static_folder="static")
app.config["SEND_FILE_MAX_AGE_DEFAULT"] = 0


@app.after_request
def add_no_cache_headers(response):
    response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response


def ensure_csv():
    if CSV_PATH.exists():
        with CSV_PATH.open("r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            if reader.fieldnames == FIELDNAMES:
                return
            rows = list(reader)
    else:
        rows = []

    with CSV_PATH.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in FIELDNAMES})


def parse_float(value, default=0.0):
    try:
        if value in (None, ""):
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def parse_int(value, default=0):
    try:
        if value in (None, ""):
            return default
        return int(float(value))
    except (TypeError, ValueError):
        return default


def parse_time(value):
    if not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def read_rows():
    ensure_csv()
    with CSV_PATH.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = []
        for row in reader:
            normalized = {field: row.get(field, "") for field in FIELDNAMES}
            normalized["_dt"] = parse_time(normalized["timestamp"])
            normalized["_temperature_c"] = parse_float(normalized["temperature_c"])
            normalized["_humidity_percent"] = parse_float(normalized["humidity_percent"])
            normalized["_motion"] = parse_int(normalized["motion"])
            normalized["_motion_event_count"] = parse_int(normalized["motion_event_count"])
            normalized["_mq135_raw"] = parse_int(normalized["mq135_raw"])
            normalized["_uptime_ms"] = parse_int(normalized["uptime_ms"])
            if normalized["_dt"]:
                rows.append(normalized)
        return sorted(rows, key=lambda item: item["_dt"])


def append_row(row):
    ensure_csv()
    with CSV_PATH.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES)
        writer.writerow({field: row.get(field, "") for field in FIELDNAMES})


def status_for_environment(latest):
    if not latest:
        return "collecting"

    humidity = latest["_humidity_percent"]
    temp = latest["_temperature_c"]

    if humidity >= 70 or temp >= 32:
        return "review"
    if humidity >= 60 or temp >= 28:
        return "watch"
    return "normal"


def status_for_odor(latest):
    if not latest:
        return "collecting"

    mq135_raw = latest["_mq135_raw"]
    if mq135_raw >= 3000:
        return "review"
    if mq135_raw >= 1800:
        return "watch"
    return "normal"


def build_payload():
    rows = read_rows()
    latest = rows[-1] if rows else None
    recent = rows[-30:]
    status = status_for_environment(latest)
    odor_status = status_for_odor(latest)
    has_temperature = bool(latest and latest.get("temperature_c", "") != "")
    has_humidity = bool(latest and latest.get("humidity_percent", "") != "")

    cards = {
        "temperature": {
            "title": "Temperature",
            "value": f"{latest['_temperature_c']:.1f}C" if has_temperature else "No data",
            "detail": "Near-box temperature from AMG8833 or DHT11.",
            "status": status if latest else "collecting",
        },
        "humidity": {
            "title": "Humidity",
            "value": f"{latest['_humidity_percent']:.1f}%" if has_humidity else "No data",
            "detail": "Humidity context for odor buildup.",
            "status": status if latest else "collecting",
        },
        "motion": {
            "title": "SR60 Motion",
            "value": "Detected" if latest and latest["_motion"] else "Clear",
            "detail": "Presence signal from SR60/PIR.",
            "status": "motion" if latest and latest["_motion"] else "normal",
        },
        "events": {
            "title": "Motion Events",
            "value": str(latest["_motion_event_count"]) if latest else "0",
            "detail": "Rising-edge motion count since ESP32 boot.",
            "status": "normal" if latest else "collecting",
        },
        "odor": {
            "title": "Odor / VOC",
            "value": str(latest["_mq135_raw"]) if latest else "No data",
            "detail": "Broad MQ135 analog trend, not gas-specific ammonia.",
            "status": odor_status if latest else "collecting",
        },
    }

    next_steps = []
    if not latest:
        next_steps.append("Upload the ESP32-S3 sketch and wait for the first reading.")
    else:
        if latest["_humidity_percent"] >= 60:
            next_steps.append("Humidity is elevated; odor may build up faster in this area.")
        if latest["_temperature_c"] >= 28:
            next_steps.append("Temperature is warm; watch for faster litter-box odor buildup.")
        if latest["_motion"]:
            next_steps.append("Motion is currently detected near the litter-box area.")
        if latest["_mq135_raw"] >= 1800:
            next_steps.append("MQ135 odor/VOC signal is elevated. Compare with a clean-air baseline.")
        if latest.get("sensor_status") in {"dht_read_failed", "amg_ok_dht_failed"}:
            next_steps.append("DHT11 read failed. Check DATA pin, power, ground, and pull-up.")
        if not next_steps:
            next_steps.append("Environment looks stable. Keep collecting readings.")

    return {
        "has_data": bool(latest),
        "updated_at": datetime.now(timezone.utc).isoformat(),
        "cards": cards,
        "latest": public_row(latest) if latest else None,
        "recent": [public_row(row) for row in recent],
        "next_steps": next_steps,
    }


def public_row(row):
    return {
        "timestamp": row["timestamp"],
        "device_id": row["device_id"],
        "temperature_c": row["_temperature_c"] if row.get("temperature_c", "") != "" else None,
        "humidity_percent": row["_humidity_percent"] if row.get("humidity_percent", "") != "" else None,
        "motion": row["_motion"],
        "motion_event_count": row["_motion_event_count"],
        "mq135_raw": row["_mq135_raw"],
        "uptime_ms": row["_uptime_ms"],
        "sensor_status": row.get("sensor_status", "ok"),
    }


@app.route("/")
def index():
    return render_template("dashboard.html")


@app.route("/api/state")
def api_state():
    return jsonify(build_payload())


@app.route("/api/seed-demo", methods=["POST"])
def seed_demo():
    now = datetime.now(timezone.utc)
    for index in range(8):
        append_row(
            {
                "timestamp": now.isoformat(),
                "device_id": "demo_esp32s3",
                "temperature_c": 23.5 + index * 0.4,
                "humidity_percent": 48 + index * 2,
                "motion": 1 if index in {2, 3, 6} else 0,
                "motion_event_count": index // 2,
                "mq135_raw": 850 + index * 120,
                "uptime_ms": index * 3000,
                "sensor_status": "ok",
            }
        )
    return jsonify({"ok": True, "message": "Demo data added"})


@app.route("/upload", methods=["POST"])
def upload():
    row = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "device_id": request.form.get("device_id", "esp32s3_litterbox_2"),
        "temperature_c": request.form.get("temperature_c", ""),
        "humidity_percent": request.form.get("humidity_percent", ""),
        "motion": request.form.get("motion", "0"),
        "motion_event_count": request.form.get("motion_event_count", "0"),
        "mq135_raw": request.form.get("mq135_raw", "0"),
        "uptime_ms": request.form.get("uptime_ms", "0"),
        "sensor_status": request.form.get("sensor_status", "ok"),
    }
    append_row(row)
    return jsonify({"ok": True, "row": row})


if __name__ == "__main__":
    ensure_csv()
    app.run(host="0.0.0.0", port=5052, debug=True)
