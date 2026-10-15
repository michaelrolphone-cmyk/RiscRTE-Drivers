#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
FLAGS=(-std=c11 -Wall -Wextra -Werror -pedantic -O2)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CC:-cc}" "${FLAGS[@]}" -I"$ROOT/test/drivers/stub_native_usb_irq" \
  "$ROOT/test/drivers/native_usb_hcd_hub_lifecycle_test.c" -o "$OUT/native-usb-hcd-hub-test"
"$OUT/native-usb-hcd-hub-test"
