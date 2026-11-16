#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="${1:?Runtime checkout required}"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
python3 "$repo/scripts/cst9217_sdk.py" --drivers "$repo" --runtime "$runtime" --output "$tmp/sdk"
for mode in normal sanitized; do
 flags=()
 if [[ "$mode" == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
 cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation "${flags[@]}" \
  -I"$tmp/sdk" -I"$runtime/sdk/app" -I"$repo/components/co5300/include" \
  "$repo/tests/test_qmi8658_idle.c" -o "$tmp/test"
 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$tmp/test"
done
