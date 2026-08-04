import os
from pathlib import Path

Import("env")


def parse_env_file(path):
    values = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        values[key] = value

    return values


def c_string(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


project_dir = Path(env.subst("$PROJECT_DIR"))
values = parse_env_file(project_dir / ".env")

macro_map = {
    "WIFI_SSID": "WIFI_SSID",
    "WIFI_PASSWORD": "WIFI_PASSWORD",
    "MQTT_BROKER": "MQTT_BROKER_ADDRESS",
    "MQTT_BROKER_ADDRESS": "MQTT_BROKER_ADDRESS",
    "MQTT_BROKER_ADRESS": "MQTT_BROKER_ADDRESS",
    "MQTT_PORT": "MQTT_PORT",
    "MQTT_USER": "MQTT_USER",
    "MQTT_PASSWORD": "MQTT_PASSWORD",
    "MQTT_CLIENT_ID": "MQTT_CLIENT_ID",
    "DEVICE_ID": "DEVICE_ID",
    "NTP_SERVER_1": "NTP_SERVER_1",
    "NTP_SERVER_2": "NTP_SERVER_2",
    "NTP_SYNC_INTERVAL_MIN": "NTP_SYNC_INTERVAL_MIN",
}

build_flags = []
seen_macros = set()

for env_key, macro_name in macro_map.items():
    if env_key not in values or macro_name in seen_macros:
        continue

    value = values[env_key]
    if macro_name in ("MQTT_PORT", "NTP_SYNC_INTERVAL_MIN"):
        build_flags.append(f"-D{macro_name}={value}")
    else:
        build_flags.append(f"-D{macro_name}={c_string(value)}")
    seen_macros.add(macro_name)

if build_flags:
    env.Append(BUILD_FLAGS=build_flags)
