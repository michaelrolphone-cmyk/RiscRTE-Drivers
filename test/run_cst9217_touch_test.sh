#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="${1:-${RISC_RUNTIME_ROOT:-}}"
if [[ ! -f "$runtime/sdk/hardware/RiscTouchI2cV2.h" ]]; then
 echo 'Usage: test/run_cst9217_touch_test.sh RUNTIME_CHECKOUT (or RISC_RUNTIME_ROOT)' >&2; exit 2
fi
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
python3 "$repo/scripts/cst9217_sdk.py" --drivers "$repo" --runtime "$runtime" --output "$tmp/sdk"
for mode in normal sanitized; do
 flags=()
 if [[ "$mode" == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
 cc -std=c11 -Wall -Wextra -Werror "${flags[@]}" \
  -I"$tmp/sdk" \
  -I"$repo/components/co5300/include" \
  "$repo/Drivers/cst9217_touch/driver.c" "$repo/test/drivers/cst9217_touch_test.c" -o "$tmp/test"
 ASAN_OPTIONS=detect_leaks=0 "$tmp/test"
done
