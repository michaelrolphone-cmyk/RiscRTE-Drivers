#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -g)
if [[ ${SANITIZE:-0} == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$root/sdk/driver" "$root/Drivers/usb_cdc_native/driver.c" -o "$build/cdc.so"
cc -std=c11 "${flags[@]}" -I"$root/sdk/driver" "$root/test/drivers/usb_cdc_native_test.c" -ldl -o "$build/test"
for mode in invalid budget-open budget-exhaust budget-before-claims clock-error missing-ack delayed-ack partial-close close-budget close-after-release-budget partial-queue-close finish-failure partial-publish partial-publish-clean failed-config failed-claim dirty-failed-claim detach-reconnect inventory-failure partial-data poll-budget queue-budget stale-native-prefix legacy wide-clock; do "$build/test" "$build/cdc.so" "$mode"; done
# Compose the actual checked host with the actual native CDC class.
cc -std=c11 "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$root/sdk/driver" "$root/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
cc -std=c11 "${flags[@]}" -DUSE_REAL_HOST -I"$root/sdk/driver" "$root/test/drivers/usb_cdc_native_test.c" -ldl -o "$build/composed"
for mode in real-retained real-dirty-claim real-dirty-claim-clean real-detach real-data; do "$build/composed" "$build/cdc.so" "$mode" "$build/host.so"; done
# Independently compiled deployed ABI-prefix consumer, not a new-header cast.
git -C "$root" archive a7ae39994603c4545a3c014f09051ecfc12084d2 sdk/driver | tar -x -C "$build"
cc -std=c11 "${flags[@]}" -I"$build/sdk/driver" -c "$root/test/drivers/usb_cdc_native_legacy_client.c" -o "$build/legacy.o"
cc -std=c11 "${flags[@]}" -DLEGACY_CLIENT -I"$root/sdk/driver" "$root/test/drivers/usb_cdc_native_test.c" "$build/legacy.o" -ldl -o "$build/legacy"
"$build/legacy" "$build/cdc.so" old-header
python3 "$root/test/usb_cdc_native_negative_test.py"
