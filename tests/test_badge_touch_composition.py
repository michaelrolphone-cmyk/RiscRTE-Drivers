#!/usr/bin/env python3
"""Host composition of the real Badge prepare helper, adapter and CST9217.

Run both modes, for example:
  python3 tests/test_badge_touch_composition.py
  python3 tests/test_badge_touch_composition.py --sanitize

This Drivers checkout provides the actual SDK and its existing pthread
FreeRTOS mutex shim. This is not a firmware boot or hardware qualification test.
Leak detection defaults on; ptrace-based hosts that cannot run LeakSanitizer may
explicitly use ASAN_OPTIONS=detect_leaks=0 (ASan/UBSan still run).
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--drivers", default=Path(__file__).resolve().parents[1], type=Path,
                        help="RiscRTE-Drivers checkout containing board preparation and CST9217")
    parser.add_argument("--sanitize", action="store_true",
                        help="enable AddressSanitizer/UndefinedBehaviorSanitizer, with leak checks by default")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    drivers = args.drivers.resolve()
    board = "boards/waveshare-esp32-s3-touch-amoled-1.75c"
    sources = [root / "tests/badge_touch_composition_test.c",
               root / board / "touch-startup.c",
               drivers / "boards/common/prepare.c",
               drivers / board / "startup.c",
               drivers / "Drivers/cst9217_touch/driver.c"]
    includes = [root / board, drivers / "boards/common", drivers / board,
                drivers / "sdk/driver", drivers / "test/drivers/stub_idf_i2c"]
    required = sources + [drivers / "sdk/driver/RiscTouchV1.h",
                          drivers / "sdk/driver/RiscI2cBusV1.h",
                          drivers / "sdk/driver/RiscPlatformClockV1.h",
                          drivers / "test/drivers/stub_idf_i2c/freertos/semphr.h"]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        parser.error("required production source/SDK/shim missing: " + ", ".join(missing))
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-pthread"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                  "-fno-omit-frame-pointer"]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "detect_leaks=1")
        environment["ASAN_OPTIONS"] += ":halt_on_error=1"
        environment["UBSAN_OPTIONS"] = environment.get("UBSAN_OPTIONS", "") + ":halt_on_error=1:print_stacktrace=1"
    with tempfile.TemporaryDirectory(prefix="badge-touch-composition-") as directory:
        binary = Path(directory) / "badge-touch-composition"
        command = shlex.split(os.environ.get("CC", "cc")) + flags
        command += ["-I" + str(path) for path in includes]
        command += [str(path) for path in sources] + ["-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], env=environment, check=True)
    print("Badge touch composition: " + ("ASan/UBSan" if args.sanitize else "normal") + " PASS")


if __name__ == "__main__":
    main()
