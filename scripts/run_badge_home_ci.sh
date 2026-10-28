#!/usr/bin/env bash
# Exact software qualification only. No flash, merge, release or device access.
set -euo pipefail
root="${BADGE_NATIVE_ROOT:?Set the exact-checkout/evidence root}"
drivers="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$IDF_PATH/export.sh" >/dev/null
export PYTHONDONTWRITEBYTECODE=1
# Deliberate retained-module cases are not a leak-check claim.
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
cc="$(command -v xtensa-esp32s3-elf-gcc)"
out="$root/home-evidence"
mkdir -p "$out"
cd "$drivers"
python tests/test_badge_home_contract.py
bash test/run_board_preparation_test.sh
bash test/run_cst9217_touch_test.sh "$root/runtime"
python tests/test_co5300_profile.py
python tests/test_badge_display.py --watch "$root/watch" --runtime "$root/runtime"
python tests/test_badge_display.py --watch "$root/watch" --runtime "$root/runtime" --sanitize
python tests/test_badge_touch_composition.py --runtime "$root/runtime"
python tests/test_badge_touch_composition.py --runtime "$root/runtime" --sanitize
python scripts/test_scoped_i2c.py --runtime "$root/runtime" --cc "$cc" --output "$out/scoped-i2c"
python scripts/build_badge_home.py --runtime "$root/runtime" --watch "$root/watch" \
  --controllers "$root/controllers" --system "$root/system" --utilities "$root/utilities" \
  --cc "$cc" --output "$out/target"
for mode in normal sanitized; do
  extra=(); if test "$mode" = sanitized; then extra=(--sanitize); fi
  python scripts/validate_badge_home.py --runtime "$root/runtime" --controllers "$root/controllers" \
    --store "$out/target/store" --native-elf "$root/evidence/build/riscrte_native.elf" \
    --output "$out/admission-$mode" "${extra[@]}"
  python scripts/test_badge_home_behavior.py --runtime "$root/runtime" --watch "$root/watch" \
    --system "$root/system" --utilities "$root/utilities" \
    --output "$out/behavior-$mode.json" "${extra[@]}"
  python scripts/test_badge_runtime_execution.py --runtime "$root/runtime" --watch "$root/watch" \
    --controllers "$root/controllers" --system "$root/system" --utilities "$root/utilities" \
    --store "$out/target/store" --output "$out/runtime-$mode.json" "${extra[@]}"
done
git rev-parse HEAD > "$out/drivers-source.txt"
