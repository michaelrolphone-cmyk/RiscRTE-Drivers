#!/usr/bin/env bash
# Preserve the original legacy USB MSP fixture against its immutable producer.
# Current checked-consumer behavior has separate positive and negative coverage.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
ref=a7ae39994603c4545a3c014f09051ecfc12084d2
git -C "$repo" archive "$ref" sdk/driver Drivers/usb_msp/driver.c | tar -x -C "$build"
flags=(-std=c11 -Wall -Wextra -Werror -I"$build/sdk/driver")
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CC:-cc}" "${flags[@]}" -fPIC -fvisibility=hidden -shared \
  "$build/Drivers/usb_msp/driver.c" -o "$build/usb-msp.so"
"${CC:-cc}" "${flags[@]}" "$repo/test/drivers/usb_msp_test.c" -ldl -o "$build/test"
"$build/test" "$build/usb-msp.so"
