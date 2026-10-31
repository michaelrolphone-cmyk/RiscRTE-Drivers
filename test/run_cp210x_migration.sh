#!/usr/bin/env bash
set -euo pipefail
r=$(cd "$(dirname "$0")/.." && pwd); t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
flags=(); case ${1:-normal} in normal);; sanitized) flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);; *)exit 2;;esac
cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I "$r/sdk/driver" "$r/Drivers/usb_cp210x_v2/driver.c" -o "$t/class.so"
cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I "$r/sdk/driver" "$r/test/drivers/usb_cp210x_stream_migration_test.c" -ldl -o "$t/class-test"
"$t/class-test" "$t/class.so"
cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I "$r/sdk/driver" "$r/Drivers/usb_host_v2/driver.c" -o "$t/host.so"
cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I "$r/sdk/driver" "$r/test/drivers/usb_cp210x_actual_host_test.c" -ldl -o "$t/host-test"
"$t/host-test" "$t/host.so" "$t/class.so"

# Ignoring checked release must fail a concrete custody assertion.
mkdir -p "$t/Drivers/usb_cp210x_v2" "$t/Drivers/common"
cp "$r/Drivers/common/SerialStreamPump.inc" "$t/Drivers/common/SerialStreamPump.inc"
sed 's/if (!discovery->release_checked(host->context, s->claim)) return false;/(void)discovery->release_checked(host->context, s->claim);/' "$r/Drivers/usb_cp210x_v2/driver.c" > "$t/Drivers/usb_cp210x_v2/driver.c"
cc "${flags[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I "$r/sdk/driver" "$t/Drivers/usb_cp210x_v2/driver.c" -o "$t/bad-class.so"
set +e
(ulimit -c 0; "$t/class-test" "$t/bad-class.so") > "$t/negative.log" 2>&1
status=$?
set -e
cat "$t/negative.log"
[[ $status -ne 0 ]] && grep -q 'Assertion.*!serial->close(first)' "$t/negative.log" || { echo 'checked release mutation did not fail expected assertion'; exit 1; }
