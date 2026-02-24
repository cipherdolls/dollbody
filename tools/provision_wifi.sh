#!/bin/bash
# Flash WiFi credentials directly to NVS partition on the Watcher.
# Usage: ./provision_wifi.sh <SSID> <PASSWORD> [PORT]
# Example: ./provision_wifi.sh ffaerber mypassword /dev/ttyACM1

set -e

SSID="${1:?Usage: $0 <SSID> <PASSWORD> [PORT]}"
PASSWORD="${2:?Usage: $0 <SSID> <PASSWORD> [PORT]}"
PORT="${3:-/dev/ttyACM1}"

IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
NVS_GEN="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"

if [ ! -f "$NVS_GEN" ]; then
    echo "ERROR: nvs_partition_gen.py not found at $NVS_GEN"
    echo "Make sure IDF_PATH is set or run: source ~/esp/esp-idf/export.sh"
    exit 1
fi

TMPDIR=$(mktemp -d)
CSV="$TMPDIR/wifi_creds.csv"
BIN="$TMPDIR/nvs.bin"

cat > "$CSV" <<EOF
key,type,encoding,value
wifi,namespace,,
ssid,data,string,$SSID
password,data,string,$PASSWORD
EOF

echo "Generating NVS binary..."
python3 "$NVS_GEN" generate "$CSV" "$BIN" 0x6000

echo "Flashing NVS to $PORT at offset 0x9000..."
python3 "$IDF_PATH/components/esptool_py/esptool/esptool.py" \
    --port "$PORT" --baud 460800 \
    write_flash 0x9000 "$BIN"

rm -rf "$TMPDIR"

echo "Done! WiFi credentials written. Reset the device to connect."
