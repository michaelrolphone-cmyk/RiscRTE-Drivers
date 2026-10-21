#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
usb="${1:?exact pinned esp-usb checkout required}"
usb="$(cd "$usb" && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -g)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$root/sdk/driver" "$root/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
for name in usb_host_v2 usb_host_checked_release usb_host_control_claim usb_host_snapshot; do
 cc -std=c11 "${flags[@]}" -I"$root/sdk/driver" "$root/test/drivers/${name}_test.c" -ldl -o "$build/$name"
 "$build/$name" "$build/host.so"
done
# An independently compiled old-header consumer preserves deployed offsets.
git -C "$root" archive a7ae39994603c4545a3c014f09051ecfc12084d2 sdk/driver | tar -x -C "$build"
cc -std=c11 "${flags[@]}" -I"$build/sdk/driver" "$root/test/drivers/usb_host_v2_test.c" -ldl -o "$build/legacy"
"$build/legacy" "$build/host.so"
c++ -std=c++17 "${flags[@]}" -I"$root/Drivers/usb_controller_esp32s3" "$root/test/drivers/usb_controller_claim_release_test.cpp" -o "$build/claim-release"
"$build/claim-release"
python3 "$root/test/usb_checked_cleanup_integration.py"
for mutation in timeout-overrun halt-skip claim-forget; do NEGATIVE="$mutation" python3 "$root/test/usb_checked_cleanup_integration.py"; done
python3 "$root/test/usb_controller_open_custody_test.py"
python3 "$root/test/usb_native_bounded_endpoint_test.py" --usb-root "$usb"
python3 "$root/test/usb_native_bounded_host_test.py" --usb-root "$usb"
python3 "$root/test/drivers/native_usb_source_adapter_test.py" --usb "$usb"
python3 "$root/test/usb_serial_native_deadline_probe.py"
bash "$root/test/run_usb_msp_detach_contract_test.sh"
for suite in run_native_usb_adapter_test run_native_usb_hcd_hub_lifecycle_test run_native_usb_irq_lifecycle_test; do bash "$root/test/$suite.sh"; done
bash "$root/test/drivers/run_native_usb_host_lifecycle_test.sh"
printf 'PASS checked cleanup qualification, SANITIZE=%s; not a target build or full callback timing pass\n' "${SANITIZE:-0}"
