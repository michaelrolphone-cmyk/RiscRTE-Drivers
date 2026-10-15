#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
output="$(mktemp -d)"; trap 'rm -rf "$output"' EXIT
flags=(-std=c17 -Wall -Wextra -Werror -g)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" "$root/test/drivers/native_usb_host_lifecycle_test.c" -o "$output/test"
"$output/test"
