#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="$(cd "${1:?exact pinned Runtime checkout required}" && pwd)"
shift
expected=6a3b3d46f5b2857eacc6e9863799dec1f466b036
[[ "$(git -C "$runtime" rev-parse HEAD)" == "$expected" ]]
# Compile actual unmodified pinned source; reject dirty runtime inputs.
[[ -z "$(git -C "$runtime" status --porcelain -- src sdk lib test/drivers/stubs)" ]]
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers
       -Wno-unused-private-field -Wno-unused-but-set-variable -fno-access-control
       -rdynamic -I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/sdk/driver"
       -I"$runtime/sdk/hardware" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs")
if [[ ${SANITIZE:-0} == 1 ]]; then
 flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
fi
"${CXX:-c++}" "${flags[@]}" \
 "$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp" \
 "$runtime/src/runtime/streams/AppStreamSessions.cpp" "$runtime/src/runtime/streams/ProviderQueueHost.cpp" \
 "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp" \
 "$repo/test/msp_numeric_manifest_test.cpp" -ldl -o "$build/test"
"$build/test" "$repo/Drivers/usb_msp/manifest.json" "$repo/Drivers/program_msp/manifest.json" "$@"
