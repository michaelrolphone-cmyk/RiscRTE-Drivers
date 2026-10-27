#!/usr/bin/env bash
# Qualification only: no installer, device access, release or app variant.
set -euo pipefail
drivers="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${BADGE_ROOT:?Set BADGE_ROOT to a fresh evidence directory}"
mkdir -p "$root/sources" "$root/evidence"
source "$IDF_PATH/export.sh" >/dev/null
clone_exact() {
  local repo="$1" revision="$2" destination="$3"
  test ! -e "$destination"
  git init --quiet "$destination"
  git -C "$destination" remote add origin "https://github.com/$repo.git"
  git -C "$destination" fetch --quiet --depth=1 --no-tags origin "$revision"
  git -C "$destination" checkout --quiet --detach FETCH_HEAD
  test "$(git -C "$destination" rev-parse HEAD)" = "$revision"
}
clone_exact michaelrolphone-cmyk/RiscRTE 13eca114cb58469cf6004fafc3da84fbd9d29beb "$root/sources/runtime"
clone_exact michaelrolphone-cmyk/RiscRTE-Watch 605d5e7d4e2b46f6b33ff2f195582db6f882535c "$root/sources/watch"
test "$(git -C "$IDF_PATH" rev-parse HEAD)" = fff9895c82d744c7237be8847347bdd1b07c6643
cmake -S "$root/sources/runtime/idf" -B "$root/sdk-build" -G Ninja -DPYTHON="$(command -v python)" >"$root/evidence/sdk-configure.log" 2>&1
cd "$drivers"
bash test/run_board_preparation_test.sh
bash test/run_cst9217_touch_test.sh "$root/sources/runtime"
python tests/test_co5300_profile.py
CC=xtensa-esp32s3-elf-gcc CXX=xtensa-esp32s3-elf-g++ python tests/test_co5300_profile.py
python tests/test_badge_display.py --watch "$root/sources/watch" --runtime "$root/sources/runtime"
python tests/test_badge_display.py --watch "$root/sources/watch" --runtime "$root/sources/runtime" --sanitize
python tests/test_badge_touch_composition.py --runtime "$root/sources/runtime"
python tests/test_badge_touch_composition.py --runtime "$root/sources/runtime" --sanitize
python tests/test_co5300_native.py --runtime "$root/sources/runtime" --idf "$IDF_PATH" --compile-commands "$root/sdk-build/compile_commands.json" --output "$root/evidence/backend"
python tests/test_co5300_native.py --runtime "$root/sources/runtime" --idf "$IDF_PATH" --compile-commands "$root/sdk-build/compile_commands.json" --host-only --sanitize --output "$root/evidence/backend-sanitized"
python scripts/build_badge_display.py --watch "$root/sources/watch" --runtime "$root/sources/runtime" --cc "$(command -v xtensa-esp32s3-elf-gcc)" --output "$root/evidence/target"
git rev-parse HEAD >"$root/evidence/drivers-source.txt"
