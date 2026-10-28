#!/usr/bin/env python3
"""Host test of the production scoped CST9217 provider and reset ownership.
Requires the pinned Runtime SDK used by the Badge graph. No firmware or physical
hardware qualification is claimed. --sanitize enables ASan and UBSan.
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
    parser.add_argument("--runtime", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    drivers = args.drivers.resolve()
    runtime = args.runtime.resolve()
    sources = [root / "tests/badge_touch_composition_test.c",
               drivers / "Drivers/cst9217_touch/driver.c",
               drivers / "test/drivers/cst9217_diagnostic_stub.c"]
    import sys
    sys.path.insert(0, str(drivers / "scripts"))
    from cst9217_sdk import stage_sdk
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-pthread"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                  "-fno-omit-frame-pointer", "-no-pie"]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "detect_leaks=1")
        environment["ASAN_OPTIONS"] += ":halt_on_error=1"
        environment["UBSAN_OPTIONS"] = environment.get("UBSAN_OPTIONS", "") + ":halt_on_error=1:print_stacktrace=1"
    with tempfile.TemporaryDirectory(prefix="badge-touch-composition-") as directory:
        sdk = stage_sdk(drivers, runtime, Path(directory) / "sdk")
        includes = [sdk, drivers / "components/co5300/include", drivers / "test/drivers"]
        binary = Path(directory) / "badge-touch-composition"
        command = shlex.split(os.environ.get("CC", "cc")) + flags
        command += ["-I" + str(path) for path in includes]
        command += [str(path) for path in sources] + ["-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], env=environment, check=True)
    print("Badge touch composition: " + ("ASan/UBSan" if args.sanitize else "normal") + " PASS")


if __name__ == "__main__":
    main()
