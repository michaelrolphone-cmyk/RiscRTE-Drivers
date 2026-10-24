#!/usr/bin/env bash
# Offline exact-source qualification. No USB/device/power operation or target build.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
reader="$(cd "${1:?Reader checkout required}" && pwd)"
mode="${2:?plain or sanitized required}"
out="${3:?new output directory required}"
[[ "$mode" == plain || "$mode" == sanitized ]]
[[ "$(git -C "$reader" rev-parse HEAD)" == 34d8e694d89a1e72d8854403d8592c289fae3ddc ]]
[[ -z "$(git -C "$reader" status --porcelain --untracked-files=no)" ]]
[[ ! -e "$out" ]] || { echo 'Output already exists' >&2; exit 1; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"
flags=(-Wall -Wextra -Werror)
if [[ "$mode" == sanitized ]]; then
  flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  export ASAN_OPTIONS=detect_leaks=0
  echo 'ASan/UBSan enabled; LeakSanitizer excluded because this executor is ptraced.'
fi
cc --version | head -n 1
printf 'drivers=%s reader=%s mode=%s\n' "$(git -C "$root" rev-parse HEAD)" "$(git -C "$reader" rev-parse HEAD)" "$mode"
count=0
run() { (cd "$reader"; "$@"); count=$((count + 1)); printf 'CHECK %02d PASS: %s\n' "$count" "$1"; }
for repo in "$root" "$reader"; do
  label=drivers; [[ "$repo" == "$reader" ]] && label=reader
  for name in usb_cdc_v2 usb_ch34x_v2 usb_cp210x_v2 usb_ftdi; do
    cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver" \
      "$repo/Drivers/$name/driver.c" -o "$out/$label-$name.so"
    cc -std=c11 "${flags[@]}" -I"$repo/sdk/driver" "$repo/test/drivers/${name}_test.c" \
      -ldl -o "$out/$label-$name-test"
    run "$out/$label-$name-test" "$out/$label-$name.so"
  done
done
cc -std=c11 "${flags[@]}" -I"$root/sdk/driver" "$root/test/drivers/usb_cdc_test.c" -o "$out/cdc-protocol"
run "$out/cdc-protocol"
for name in serial_monitor_test serial_monitor_no_device_test serial_monitor_baud_test; do
  cc -std=c11 "${flags[@]}" -I"$reader/lib/NativeApps/include" "$reader/Apps/serial_monitor.c" \
    "$reader/test/native_apps/$name.c" -o "$out/$name"
  run "$out/$name"
done
cc -std=c11 "${flags[@]}" "$reader/test/native_apps/serial_monitor_launch_contract_test.c" -o "$out/monitor-launch"
run "$out/monitor-launch"
includes=(-I"$reader/lib/NativeApps/include" -I"$reader/src" -I"$reader/sdk/driver")
for name in serial_provider_devices serial_provider_registry serial_structured_diagnostic serial_failed_acquire; do
  c++ -std=c++17 "${flags[@]}" "${includes[@]}" "$reader/test/streams/${name}_test.cpp" -o "$out/$name"
  run "$out/$name"
done
c++ -std=c++17 "${flags[@]}" "${includes[@]}" "$reader/test/drivers/installed_serial_session_test.cpp" -o "$out/installed-session"
run "$out/installed-session"
c++ -std=c++17 "${flags[@]}" "${includes[@]}" -DRISCRTE_TEST_INSTALLED_SERIAL_PATH \
  "$reader/src/native/NativeSerialPortBridge.cpp" "$reader/test/streams/installed_serial_bridge_test.cpp" -o "$out/installed-bridge"
run "$out/installed-bridge"
c++ -std=c++17 "${flags[@]}" "${includes[@]}" \
  "$root/docs/evidence/usb-serial-monitor-20261021/legacy_inventory_probe.cpp" -ldl -o "$out/legacy-inventory"
run "$out/legacy-inventory" "$out"/drivers-usb_{cdc_v2,ch34x_v2,cp210x_v2,ftdi}.so
[[ "$count" == 20 ]]
echo '20 host checks passed. No target build, physical USB, power operation or full-product pass.'
