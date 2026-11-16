#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
python3 "$repo/scripts/board_preparation.py" "$repo/boards/waveshare-esp32-s3-touch-amoled-1.75c/preparation.json" --header "$tmp/startup-config.h" --board-id waveshare-esp32-s3-touch-amoled-1.75c
cmp "$tmp/startup-config.h" "$repo/boards/waveshare-esp32-s3-touch-amoled-1.75c/startup-config.h"
for mode in normal sanitized; do
 flags=()
 if [[ "$mode" == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -no-pie); fi
 cc -std=c11 -Wall -Wextra -Werror "${flags[@]}" -I"$repo/sdk/driver" -I"$repo/boards/common" -I"$tmp" "$repo/boards/common/prepare.c" "$repo/test/boards/prepare_test.c" -o "$tmp/test"
 ASAN_OPTIONS=detect_leaks=0 "$tmp/test"
done
python3 -m unittest discover -s "$repo/test/boards" -p 'test_*.py' -v
