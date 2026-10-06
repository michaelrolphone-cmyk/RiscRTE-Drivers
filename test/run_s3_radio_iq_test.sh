#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all); fi
"${CC:-cc}" "${flags[@]}" -I"$root/sdk/driver" -I"$root/test" "$root/test/s3_radio_iq_lifecycle_test.c" -o "$build/lifecycle"
"$build/lifecycle"
"${CC:-cc}" "${flags[@]}" "$root/test/s3_radio_iq_plan_test.c" -o "$build/plan"
"$build/plan"
