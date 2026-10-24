#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
for mode in normal sanitized; do
 flags=()
 if [[ "$mode" == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
 cc -D_POSIX_C_SOURCE=200809L -pthread -I"$repo/test/drivers/stub_idf_i2c" -std=c11 -Wall -Wextra -Werror "${flags[@]}" -I"$repo/sdk/driver" "$repo/Drivers/cst9217_touch/driver.c" "$repo/test/drivers/cst9217_touch_test.c" -o "$tmp/test"
 ASAN_OPTIONS=detect_leaks=0 "$tmp/test"
done
