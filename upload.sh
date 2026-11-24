#!/bin/zsh
# Builds and uploads the firmware. A running monitor.py (rf433-sniffer) lends the port.
cd "$(dirname "$0")/rgb-remote"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FLAG="$PWD/../tools/rf433-sniffer/.uploading"
B=esp32:esp32:esp32:UploadSpeed=115200
PORT=$(ls /dev/cu.usbserial* 2>/dev/null | head -1)
[ -z "$PORT" ] && { echo "no ESP32 found"; exit 1 }
"$CLI" compile --warnings all -b $B --build-path /tmp/rgb-remote-build . || exit 1
touch $FLAG
trap 'rm -f $FLAG' EXIT
for i in 1 2 3 4 5 6 7 8; do lsof "$PORT" >/dev/null 2>&1 || break; sleep 0.5; done
for i in 1 2 3; do "$CLI" upload -b $B -p "$PORT" --input-dir /tmp/rgb-remote-build . && exit 0; sleep 1; done
exit 1
