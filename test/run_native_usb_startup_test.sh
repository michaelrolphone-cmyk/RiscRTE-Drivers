#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"; out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -g -I"$root/sdk/driver")
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
for version in 0x040407 0x060100; do
 c++ "${flags[@]}" -DTEST_IDF_VERSION="$version" "$root/test/drivers/controller_host_startup_test.cpp" -o "$out/startup"
 "$out/startup"
done
c++ "${flags[@]}" "$root/test/drivers/controller_role_switch_test.cpp" -o "$out/role"
"$out/role"
