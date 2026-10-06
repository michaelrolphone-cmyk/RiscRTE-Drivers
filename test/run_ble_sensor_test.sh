#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all); fi
for t in ble_sensor_parser ble_sensors ble_telemetry ble_telemetry_codec telemetry_battery; do
 "${CC:-cc}" "${flags[@]}" -I"$root/sdk/driver" -I"$root/Drivers/ble_sensors" "$root/test/${t}_test.c" -o "$build/$t"
 "$build/$t"
done
