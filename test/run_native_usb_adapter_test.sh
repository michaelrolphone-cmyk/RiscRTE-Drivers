#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"; out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -g -I"$root/test/drivers/stub_native_usb_adapters")
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
for version in 0x060100 0x040407; do
 c++ "${flags[@]}" -DTEST_IDF_VERSION="$version" "$root/test/drivers/native_usb_adapter_test.cpp" -o "$out/test"; "$out/test"
done
if c++ "${flags[@]}" -DTEST_IDF_VERSION=0x060101 -c "$root/test/drivers/native_usb_adapter_test.cpp" -o "$out/rejected.o" 2>"$out/rejected.log"; then
 echo 'unreviewed SDK accepted' >&2; exit 1
fi
grep -q 'requires reviewed IDF' "$out/rejected.log"
