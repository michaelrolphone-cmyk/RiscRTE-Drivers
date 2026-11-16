#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
runtime="${1:?exact Runtime checkout required}"; system="${2:?matching System checkout required}"
[[ $(git -C "$runtime" rev-parse HEAD) == 6a3b3d46f5b2857eacc6e9863799dec1f466b036 ]]
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -g)
if [[ ${SANITIZE:-0} == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
mkdir "$build/sdk";cp "$system/lib/PortableApps/include/"*.h "$build/sdk/"
cp "$root/sdk/driver/RiscUsbProviderV1.h" "$root/sdk/driver/RiscUsbControllerV1.h" "$build/sdk/"
cp "$runtime/sdk/app/RiscRuntimeV1.h" "$runtime/sdk/app/RiscStreamClientV1.h" "$runtime/sdk/app/RiscSerialStreamSessionV1.h" "$runtime/sdk/driver/RiscStreamResultV1.h" "$runtime/sdk/driver/RiscStreamSessionProviderV1.h" "$runtime/sdk/driver/RiscProviderV2.h" "$runtime/sdk/driver/RiscStreamProviderV1.h" "$build/sdk/"
for pair in 'controller test/fixtures/cdc_runtime_controller.c' 'host Drivers/usb_host_v2/driver.c' 'serial Drivers/usb_cdc_native/driver.c'; do read -r name src <<< "$pair";cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$root/sdk/driver" "$root/$src" -o "$build/$name.elf";done
# Load exact candidate host/CDC manifests, not fixture version substitutions.
mkdir "$build/host" "$build/serial"
mv "$build/host.elf" "$build/host/driver.elf";mv "$build/serial.elf" "$build/serial/driver.elf"
cp "$root/candidates/usb-native-stream/usb-host-v2.json" "$build/host/manifest.json"
cp "$root/Drivers/usb_cdc_native/manifest.json" "$build/serial/manifest.json"
cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared -DPORTABLE_SERIAL_STREAMS -I"$build/sdk" -I"$root/sdk/driver" -I"$system/lib/NativeApps/include" "$root/test/fixtures/cdc_runtime_app.c" "$system/lib/PortableApps/src/PortableSerialClient.c" -o "$build/default.elf"
c++ -std=c++17 "${flags[@]}" -Wno-missing-field-initializers -rdynamic -DRISC_STREAM_HOST_TESTING -I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs" -I"$runtime/test" "$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp" "$runtime/src/runtime/streams/ProviderQueueHost.cpp" "$runtime/src/runtime/streams/AppStreamSessions.cpp" "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp" "$root/test/drivers/usb_cdc_runtime_test.cpp" -ldl -o "$build/test"
for activation in eager demand;do for mode in normal unknown detach close-retained open-retained;do "$build/test" "$build" "$mode" "$activation";done;done
