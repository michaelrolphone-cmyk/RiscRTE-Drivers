#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "${SANITIZE:-0}" = 1 ]; then
 python scripts/build_ble_hid.py --host --sanitize
 dir=build/ble-hid-san
 extra='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie'
else
 python scripts/build_ble_hid.py --host
 dir=build/ble-hid-host
 extra=''
fi
cc -std=c11 -O1 -Wall -Wextra -Werror $extra -I Drivers/ble_hid/port -I sdk/driver -I vendor/nimble/nimble/include test/ble_hid_port_test.c "$dir"/*.o -o "$dir/port-test"
ASAN_OPTIONS=detect_leaks=0 "$dir/port-test"
for scenario in happy reject timeout late late-wrap legacy invalid-public-key tamper retained malformed storage claim-retry immediate-close backpressure-mode backpressure-refresh backpressure-control backpressure-ccc backpressure-watchdog backpressure-explicit backpressure-disconnect; do
 if [ "${SANITIZE:-0}" = 1 ]; then
  ASAN_OPTIONS=detect_leaks=0:verify_asan_link_order=0 LD_PRELOAD="$(cc -print-file-name=libasan.so)" python test/ble_hid_protocol_test.py "$dir/driver.so" "$scenario"
 else
  python test/ble_hid_protocol_test.py "$dir/driver.so" "$scenario"
 fi
done
