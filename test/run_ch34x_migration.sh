#!/usr/bin/env bash
# Actual retained host + new class and shared helper; simulated controller only.
set -euo pipefail
mode=${1:-normal}
flags=()
case "$mode" in
  normal) ;;
  sanitized) flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer) ;;
  *) echo "usage: $0 [normal|sanitized]" >&2; exit 2 ;;
esac
echo "CH34x migration mode: $mode"
r=$(cd "$(dirname "$0")/.." && pwd);t=$(mktemp -d);trap 'rm -rf "$t"' EXIT
"${CC:-cc}" "${flags[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I "$r/sdk/driver" "$r/Drivers/usb_ch34x_v2/driver.c" -o "$t/class.so"
"${CC:-cc}" "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I "$r/sdk/driver" "$r/test/drivers/usb_ch34x_stream_migration_test.c" -ldl -o "$t/class-test"
"$t/class-test" "$t/class.so"
"${CC:-cc}" "${flags[@]}" -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -I "$r/sdk/driver" "$r/Drivers/usb_host_v2/driver.c" -o "$t/host.so"
"${CC:-cc}" "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I "$r/sdk/driver" "$r/test/drivers/usb_ch34x_actual_host_test.c" -ldl -o "$t/host-test"
"$t/host-test" "$t/host.so" "$t/class.so"
"${CC:-cc}" "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I "$r/sdk/driver" "$r/test/drivers/serial_stream_pump_test.c" -o "$t/pump-test"
"$t/pump-test"
# A negative mutation must lose pending bytes and be caught by the same test.
mkdir -p "$t/test/drivers" "$t/Drivers/common"
cp "$r/test/drivers/serial_stream_pump_test.c" "$t/test/drivers/serial_stream_pump_test.c"
sed 's/s->rx_offset += accepted;/s->rx_offset += remaining;/' "$r/Drivers/common/SerialStreamPump.inc" > "$t/Drivers/common/SerialStreamPump.inc"
"${CC:-cc}" "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I "$r/sdk/driver" "$t/test/drivers/serial_stream_pump_test.c" -o "$t/bad-pump"
set +e
(ulimit -c 0; "$t/bad-pump") > "$t/negative.log" 2>&1
status=$?
set -e
cat "$t/negative.log"
[[ $status -ne 0 ]] || { echo 'backpressure mutation escaped'; exit 1; }

grep -q "Assertion.*rx_size==2" "$t/negative.log" || { echo "mutation failed for unexpected reason"; exit 1; }
