#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
cc -D_POSIX_C_SOURCE=200809L -pthread -I"$repo/test/drivers/stub_idf_i2c" -std=c11 -Wall -Wextra -Werror   -fsanitize=address,undefined -fno-omit-frame-pointer   -I"$repo/sdk/driver"   "$repo/Drivers/gt911_touch/driver.c"   "$repo/test/drivers/gt911_touch_test.c" -o "$binary"
"$binary"
