#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
FLAGS=(-std=c++17 -Wall -Wextra -Werror -pedantic -O2)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${FLAGS[@]}" -I"$ROOT/test/drivers/stub_native_usb_irq" \
  "$ROOT/Drivers/usb_controller_esp32s3/NativeIrqLifecycle.cpp" \
  "$ROOT/test/drivers/native_usb_irq_lifecycle_test.cpp" -o "$OUT/native-usb-irq-test"
"$OUT/native-usb-irq-test"
# HCD is C: verify the private boundary can be included by a C translation unit.
printf '#include "NativeIrqLifecycle.h"\n' | \
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/test/drivers/stub_native_usb_irq" \
    -I"$ROOT/Drivers/usb_controller_esp32s3" -x c -c -o "$OUT/header-c.o" -
