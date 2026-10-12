#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build=$(mktemp -d); trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie); fi
"${CC:-cc}" "${flags[@]}" -I"$root/lib/StorageFatFs/fatfs" "$root/test/fatfs_native_capacity_test.c" "$root/lib/StorageFatFs/fatfs/ff.c" "$root/lib/StorageFatFs/fatfs/ffunicode.c" -o "$build/test"
"$build/test"
