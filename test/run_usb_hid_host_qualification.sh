#!/usr/bin/env bash
# Offline provider fixtures only. Does not open USB devices or build firmware.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
MODE=${1:-plain}
case "$MODE" in
  plain|sanitized) ;;
  *) echo "usage: $0 [plain|sanitized] [output-directory]" >&2; exit 2 ;;
esac
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/risc-usb-hid.XXXXXX")}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CC=${CC:-cc}
FLAGS=(-std=c11 -Wall -Wextra -Werror -I sdk/driver -g)
if [[ "$MODE" == sanitized ]]; then
  FLAGS+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
fi
printf 'Mode: %s\nOutput: %s\nASAN_OPTIONS: %s\n' "$MODE" "$OUT" "${ASAN_OPTIONS:-unset}"
"$CC" --version
set -x
for driver in usb_host_v2 usb_hid usb_hid_keyboard usb_hid_gamepad; do
  "$CC" "${FLAGS[@]}" -fPIC -fvisibility=hidden -shared \
    "Drivers/$driver/driver.c" -o "$OUT/$driver.so"
done
for fixture in usb_host_v2 usb_hid usb_hid_quiesce; do
  "$CC" "${FLAGS[@]}" "test/drivers/${fixture}_test.c" -ldl -o "$OUT/$fixture"
  if [[ "$fixture" == usb_host_v2 ]]; then
    "$OUT/$fixture" "$OUT/usb_host_v2.so"
  else
    "$OUT/$fixture" "$OUT/usb_hid.so" "$OUT/usb_hid_keyboard.so" "$OUT/usb_hid_gamepad.so"
  fi
done
# usb_hid_gamepad_descriptor_test.c is byte-identical to usb_hid_gamepad_test.c
# at the documented baseline; do not count the duplicate as separate coverage.
for fixture in usb_hid_keyboard usb_hid_gamepad usb_hid_text_input usb_ui_navigation usb_xinput_gamepad; do
  "$CC" "${FLAGS[@]}" "test/drivers/${fixture}_test.c" -o "$OUT/$fixture"
  "$OUT/$fixture"
done
set +x
printf 'PASS: 8 distinct offline USB host/HID fixture bodies (%s)\n' "$MODE"
