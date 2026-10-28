#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver")
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
for driver in usb_host_v2 usb_msp program_msp; do
  "${CC:-cc}" "${flags[@]}" -fPIC -fvisibility=hidden -shared \
    "$repo/Drivers/$driver/driver.c" -o "$build/$driver.so"
done
"${CC:-cc}" "${flags[@]}" "$repo/test/drivers/usb_msp_checked_consumer_test.c" -ldl -o "$build/test"
"$build/test" "$build/usb_host_v2.so" "$build/usb_msp.so" "$build/program_msp.so"

"$build/test" "$build/usb_host_v2.so" "$build/usb_msp.so" "$build/program_msp.so" detach-before-target-close
"${CC:-cc}" "${flags[@]}" "$repo/test/drivers/usb_msp_legacy_contract_test.c" -ldl -o "$build/legacy"
for mode in prefix truncated-release null-release release-only truncated-control full; do
  "$build/legacy" "$build/usb_msp.so" "$mode"
done
