#!/usr/bin/env python3
"""Qualify unchanged Home 0.19.4 against the Badge's four-capability profile.

Links the production Home, CO5300 facade and CST9217 provider. The native
transport/hardware and Runtime invocation boundaries are host models, not an
ELF loader or firmware boot. Run again with --sanitize for ASan/UBSan.
Leak detection defaults on. Hosts using ptrace may explicitly set
ASAN_OPTIONS=detect_leaks=0; the evidence records this exclusion.
"""
import argparse
import ast
import hashlib
import importlib.util
import json
import os
import shutil
from pathlib import Path
from cst9217_sdk import stage_sdk
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PINS = {"watch": "605d5e7d4e2b46f6b33ff2f195582db6f882535c",
        "system": "bb0cadf3cec3445a64e11513b47b2676f3fdd92d",
        "utilities": "4fdcad2495c33fe63f5f6faca9b11ced3c739a5b"}


def run(command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in (("watch", "badge-display-watch"), ("system", "badge-home-system"),
                          ("utilities", "badge-home-utilities"), ("runtime", "badge-display-runtime")):
        parser.add_argument("--" + name, type=Path, default=ROOT.parent / default)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path, help="write machine-readable evidence JSON")
    args = parser.parse_args()
    paths = {name: getattr(args, name).resolve() for name in ("watch", "system", "utilities", "runtime")}
    def revision(path):
        return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()

    def verify_pins():
        for name, pin in PINS.items():
            actual = revision(paths[name])
            if actual != pin:
                parser.error(f"{name} must be pinned at {pin}, found {actual}")
            run(["git", "-C", str(paths[name]), "diff", "--exit-code", "HEAD", "--"], stdout=subprocess.DEVNULL)

    verify_pins()
    watch, system, utilities, runtime = (paths[n] for n in ("watch", "system", "utilities", "runtime"))
    tree = ast.parse((watch / "scripts/build.py").read_text())
    expression = next(node.value for node in tree.body if isinstance(node, ast.Assign)
                      and any(isinstance(target, ast.Name) and target.id == "FLAGS" for target in node.targets))
    # Read the pinned production expression, without executing the build script.
    flags = eval(compile(ast.Expression(expression), "production FLAGS", "eval"), {"__builtins__": {}})
    assert all(isinstance(flag, str) for flag in flags)
    assert json.loads((watch / "apps/clock/default.json").read_text())["version"] == "0.19.4"
    spec = importlib.util.spec_from_file_location("badge_home_merged_sdk", watch / "scripts/merged_sdk.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    common = ["-O1", "-g", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        common += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-no-pie"]
    cc, cxx = (shlex.split(os.environ.get(name, default)) for name, default in (("CC", "cc"), ("CXX", "c++")))
    sources = [ROOT / "tests/badge_home_behavior.c", watch / "apps/clock/viewport.c",
               watch / "apps/clock/capability_clients.c", watch / "apps/clock/quick_radios.c",
               watch / "apps/clock/nova/nova.c", watch / "apps/clock/points_projection.c",
               watch / "apps/clock/effects/divdi3.c",
               *[system / "lib/PortableApps/src" / name for name in ("quick_actions.c", "quick_render.c", "quick_session.c")]]
    production = [watch / "apps/clock/crown.c", watch / "scripts/build.py", watch / "scripts/merged_sdk.py",
                  watch / "apps/clock/default.json", watch / "apps/clock/effects/boot.cpp",
                  ROOT / "components/co5300/facade/driver.c", ROOT / "Drivers/cst9217_touch/driver.c",
                  ROOT / "test/drivers/cst9217_test_model.h", ROOT / "tests/provider_sync_mock.h",
                  ROOT / "scripts/cst9217_sdk.py",
                  ROOT / "boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h",
                  utilities / "Apps/alarm_writer.h",
                  Path(__file__).resolve(), *sources,
                  *sorted((ROOT / "components/co5300/include").rglob("*.h"))]
    # Fingerprint the complete source/header/asset closure, including staged SDK
    # inputs and test boundaries. Re-enumerate afterward to catch additions as
    # well as edits/deletions. Application/dependency source roots must also be
    # clean at their pinned commits; generated build directories are not inputs.
    source_roots = [watch / "apps/clock", watch / "apps/runtime_features", watch / "include", watch / "sdk",
                    system / "lib/PortableApps", utilities / "lib/Alarm/include", utilities / "lib/Contexts/include",
                    ROOT / "components/co5300/include", ROOT / "sdk/driver",
                    runtime / "sdk/app", runtime / "sdk/hardware", runtime / "sdk/driver"]
    for name, relatives in (("watch", ["apps/clock", "apps/runtime_features", "include", "sdk"]),
                            ("system", ["lib/PortableApps"]),
                            ("utilities", ["lib/Alarm/include", "lib/Contexts/include", "Apps/alarm_writer.h"])):
        status = subprocess.check_output(["git", "-C", str(paths[name]), "status", "--porcelain",
                                          "--untracked-files=all", "--", *relatives], text=True)
        if status:
            parser.error(f"{name} source inputs are not clean: {status.strip()}")
    runtime_revision = revision(runtime)

    def digest_inputs():
        files = set(production)
        for root in source_roots:
            files.update(path for path in root.rglob("*") if path.is_file())
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(files)}
    digests = digest_inputs()
    with tempfile.TemporaryDirectory(prefix="badge-home-behavior-") as tmp:
        out = Path(tmp)
        sdk = module.headers(out, system, utilities, watch)
        provider_sdk = stage_sdk(ROOT, runtime, out / "provider-sdk")
        # The current CO5300 facade publishes the additive round-shape flag.
        # Its complete capability header comes from its owning Drivers tree;
        # the pinned legacy Home retains its own SDK and ignores unknown bits.
        shutil.copyfile(ROOT / "sdk/driver/RiscDisplayOutputV1.h",
                        provider_sdk / "RiscDisplayOutputV1.h")
        includes = [sdk, watch / "include", watch, system / "lib/PortableApps/src",
                    ROOT / "components/co5300/include", runtime / "sdk/hardware", runtime / "sdk/driver",
                    ROOT / "sdk/driver", ROOT / "test/drivers"]
        inc = ["-I" + str(path) for path in includes]
        objects = []
        for number, source in enumerate(sources):
            obj = out / f"home-{number}.o"
            run([*cc, "-std=c11", *common, *flags, *inc, "-c", str(source), "-o", str(obj)])
            objects.append(obj)
        for name, source, extra, provider_includes in (
            ("display", ROOT / "components/co5300/facade/driver.c",
             ["-include", str(ROOT / "boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h")],
             ["-I" + str(provider_sdk), *inc]),
            ("touch", ROOT / "Drivers/cst9217_touch/driver.c", [],
             ["-I" + str(path) for path in (provider_sdk, ROOT / "components/co5300/include")])):
            obj = out / f"{name}.o"
            run([*cc, "-std=c11", *common, *provider_includes, *extra,
                 f"-Dt5_driver_get=badge_{name}_driver_get", "-c", str(source), "-o", str(obj)])
            objects.append(obj)
        boot = out / "boot.o"
        run([*cxx, "-std=c++11", *common, *inc, "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
             "-c", str(watch / "apps/clock/effects/boot.cpp"), "-o", str(boot)])
        binary = out / "badge-home-behavior"
        run([*cxx, *common, *flags, *map(str, objects), str(boot), "-o", str(binary)])
        env = os.environ.copy()
        if args.sanitize:
            env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "detect_leaks=1") + ":halt_on_error=1"
            env["UBSAN_OPTIONS"] = env.get("UBSAN_OPTIONS", "") + ":halt_on_error=1:print_stacktrace=1"
        result = subprocess.run([str(binary)], text=True, capture_output=True, env=env, timeout=180)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        result.check_returncode()
    assert digest_inputs() == digests, "Production inputs changed during qualification"
    verify_pins()
    assert revision(runtime) == runtime_revision, "Runtime revision changed during qualification"
    evidence = {"schema": 1, "result": "PASS", "sanitizers": "address,undefined" if args.sanitize else None,
                "asan_options": env.get("ASAN_OPTIONS") if args.sanitize else None,
                "leak_detection": args.sanitize and "detect_leaks=0" not in env.get("ASAN_OPTIONS", ""),
                "production_pins": PINS, "runtime_header_revision": runtime_revision,
                "compiler": subprocess.check_output([*cc, "--version"], text=True).splitlines()[0],
                "display_sync": "independent provider-scoped owner-only platform.sync@1 model; create/destroy and native-call guard asserted",
                "input_freshness_verified": True, "home_version": "0.19.4", "home_production_flags": flags,
                "home_source_modified": False, "width": 466, "height": 466, "safe_insets": [69] * 4,
                "available_app_capabilities": ["display.output@1", "input.touch.raw@1",
                                               "runtime.realtime-control@1", "runtime.provider-promotion@1"],
                "native_realtime": "UNSET; never seeded", "production_input_sha256": digests,
                "evidence_depth": "host-linked maintained Home, CO5300 facade and CST9217; modeled Runtime invocation and native hardware boundaries",
                "firmware_boot_tested": False, "elf_loader_tested": False, "physical_hardware_tested": False,
                "stdout": result.stdout}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(evidence, indent=2) + "\n")
    print("Badge Home behavior: " + ("ASan/UBSan" if args.sanitize else "normal") + " PASS")


if __name__ == "__main__":
    main()
