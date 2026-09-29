#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pedantic -pthread \
  -I"$ROOT/test/drivers/stub_idf_i2c" -I"$ROOT/sdk/driver" \
  "$ROOT/Drivers/i2c_esp32s3_v2/driver.c" \
  "$ROOT/test/drivers/i2c_esp32s3_v2_test.c" -o "$OUT/i2c-v2-test"
"$OUT/i2c-v2-test"
