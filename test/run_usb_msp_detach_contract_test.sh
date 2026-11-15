#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
# The fixture from 4db1ca4 is a preserved counterexample, not an assertion
# about the repaired current consumer. Both source blobs match a7ae399.
negative_ref=a7ae39994603c4545a3c014f09051ecfc12084d2
git -C "$repo" archive "$negative_ref" sdk/driver Drivers/usb_host_v2/driver.c Drivers/usb_msp/driver.c | tar -x -C "$build"
flags=(-std=c11 -Wall -Wextra -Werror -I"$build/sdk/driver")
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
for driver in usb_host_v2 usb_msp; do
  "${CC:-cc}" "${flags[@]}" -fPIC -fvisibility=hidden -shared \
    "$build/Drivers/$driver/driver.c" -o "$build/$driver.so"
done
"${CC:-cc}" "${flags[@]}" "$repo/test/drivers/usb_msp_detach_contract_test.c" \
  -ldl -o "$build/contract-test"
for outcome in successful-release failed-release; do
  "$build/contract-test" "$build/usb_host_v2.so" "$build/usb_msp.so" "$outcome"
done
