#!/bin/zsh
# Builds and uploads to the classic ESP32. A running monitor.py lends the port.
cd "$(dirname "$0")"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
B=esp32:esp32:esp32:UploadSpeed=115200
PORT=$(ls /dev/cu.usbserial* 2>/dev/null | head -1)
[ -z "$PORT" ] && { echo "no ESP32 found"; exit 1 }
"$CLI" compile -b $B --build-path /tmp/rf433-build . || exit 1
touch .uploading
trap 'rm -f .uploading' EXIT
for i in 1 2 3 4 5 6 7 8; do lsof "$PORT" >/dev/null 2>&1 || break; sleep 0.5; done
for i in 1 2 3; do "$CLI" upload -b $B -p "$PORT" --input-dir /tmp/rf433-build . && exit 0; sleep 1; done
exit 1
