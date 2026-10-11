#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "${SANITIZE:-0}" = 1 ]; then
 python scripts/build_ble_session_setup.py --host --sanitize
 dir=build/ble-session-setup-san
else
 python scripts/build_ble_session_setup.py --host
 dir=build/ble-session-setup-host
fi
for scenario in happy clock-admission validation maximum stale-events clock-wrap short-lifetime copy expiry expiry-mid-read disconnect retained retained-cooperate retained-terminal retained-incomplete claim immediate-close fault reject timeout tamper legacy short-key just-works invalid-key; do
 if [ "${SANITIZE:-0}" = 1 ]; then
  ASAN_OPTIONS=detect_leaks=0:verify_asan_link_order=0 LD_PRELOAD="$(cc -print-file-name=libasan.so)" python test/ble_session_setup_protocol_test.py "$dir/driver.so" "$scenario"
 else
  python test/ble_session_setup_protocol_test.py "$dir/driver.so" "$scenario"
 fi
done
python test/ble_session_setup_central_test.py
