#!/usr/bin/env python3
"""Exact-source host-native qualification, with portable hash-linked evidence.

PR40 is required. PR41 is a distinct compatibility/regression cohort, and its
false/nonzero controller-custody gap is recorded, not called a full pass.
No network, publication, ref changes, target hardware, or provider ABI changes.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
READER = HERE / "pinned_reader"
CANDIDATE = ROOT / "candidates/usb-hid-checked-cleanup"
PINS = {
    "pr40": "90e7a8faae1a6678a24c92c32f95032b2fbe1e24",
    "pr41": "501e22b5bc41d39fee6b15346a25a87ab7ad5df3",
}
CASES = ["table-gates", "raw-retry", "orphan-retained", "orphan-immediate", "claim-zero",
         "control-failures", "raw-detach", "mouse-quiesce-retry", "mouse-detach-retry",
         "mouse-setup-failure", "mouse-retained"]
SANITIZE = os.environ.get("SANITIZE", "0")
if SANITIZE not in ("0", "1"):
    raise SystemExit("SANITIZE must be 0 or 1")
MODE = "asan-ubsan" if SANITIZE == "1" else "normal"
EVIDENCE = ROOT / "dist/raw-hid-checked-cleanup-ci" / MODE
EVIDENCE.mkdir(parents=True, exist_ok=True)
summary = {
    "schema": 1,
    "mode": MODE,
    "qualification_scope": "Host-native deterministic controller boundary; not target/hardware/IDF timing qualification",
    "reader_commit": "c334919a05d393b7b3a536fceb117cfbb573a4ef",
    "required_host": PINS["pr40"],
    "distinct_regression_host": PINS["pr41"],
    "case_results": [], "mutation_results": [], "commands": [], "inputs": {}, "builds": {},
    "status": "running",
}
flags = ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-g"]
linkflags: list[str] = []
env = os.environ.copy()
if SANITIZE == "1":
    flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    linkflags += ["-no-pie"]
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0:halt_on_error=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
summary["sanitizers"] = {k: env.get(k) for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS")}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    return sha(path.read_bytes())


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def input_file(path: Path) -> None:
    summary["inputs"][path.relative_to(ROOT).as_posix()] = file_digest(path)


def compile_source(source: Path, output: Path, includes: list[Path], *, shared: bool) -> None:
    command = ["cc", *flags]
    command += ["-fPIC", "-fvisibility=hidden", "-shared"] if shared else linkflags
    command += [f"-I{p}" for p in includes]
    command += [str(source)]
    if not shared:
        command += ["-ldl"]
    command += ["-o", str(output)]
    run(command, f"build-{output.name}")
    summary["builds"][output.name] = {"sha256": file_digest(output), "bytes": output.stat().st_size}


def run(command: list[str], label: str, *, expected_assertion: str | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    output = result.stdout.replace(str(BUILD), "$BUILD").replace(str(ROOT), "$ROOT")
    (EVIDENCE / f"{label}.log").write_text(output)
    summary["commands"].append({
        "name": label,
        "command": [str(x).replace(str(BUILD), "$BUILD").replace(str(ROOT), "$ROOT") for x in command],
        "exit_code": result.returncode,
        "log": f"{MODE}/{label}.log",
        "log_sha256": sha(output.encode()),
    })
    if output:
        print(output, end="" if output.endswith("\n") else "\n", flush=True)
    if expected_assertion is not None:
        if result.returncode == 0 or expected_assertion not in output or "Assertion" not in output:
            raise RuntimeError(f"{label} did not fail the required mutation assertion: {expected_assertion}")
    elif result.returncode:
        raise RuntimeError(f"{label} failed with exit code {result.returncode}")
    return result


with tempfile.TemporaryDirectory(prefix="raw-hid-checked-") as temporary:
    BUILD = Path(temporary)
    try:
        summary["checkout_head"] = git("rev-parse", "HEAD").decode().strip()
        summary["platform"] = {"system": platform.system(), "machine": platform.machine()}
        summary["compiler"] = subprocess.check_output(["cc", "--version"], text=True).splitlines()[0]
        for path in sorted(HERE.rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts:
                input_file(path)
        for path in (CANDIDATE / "driver.c", CANDIDATE / "manifest.json"):
            input_file(path)
        summary["candidate_source_git_blob"] = git("hash-object", str(CANDIDATE / "driver.c")).decode().strip()
        run([sys.executable, str(HERE / "verify_pins.py")], "source-pins")
        for cohort, commit in PINS.items():
            destination = BUILD / cohort
            destination.mkdir()
            archive = BUILD / f"{cohort}.tar"
            archive.write_bytes(git("archive", commit, "sdk/driver", "Drivers/usb_host_v2/driver.c"))
            with tarfile.open(archive) as stream:
                stream.extractall(destination, filter="data")
            source = destination / "Drivers/usb_host_v2/driver.c"
            summary.setdefault("host_source_pins", {})[cohort] = {
                "commit": commit,
                "source_sha256": file_digest(source),
                "source_git_blob": git("rev-parse", f"{commit}:Drivers/usb_host_v2/driver.c").decode().strip(),
                "sdk_sha256": {p.relative_to(destination).as_posix(): file_digest(p)
                               for p in sorted((destination / "sdk/driver").glob("*.h"))},
            }
            compile_source(source, BUILD / f"host-{cohort}.so", [destination / "sdk/driver"], shared=True)
        compile_source(CANDIDATE / "driver.c", BUILD / "raw.so", [BUILD / "pr40/sdk/driver"], shared=True)
        compile_source(READER / "Drivers/usb_hid_mouse/driver.c", BUILD / "mouse.so", [READER / "sdk/driver"], shared=True)
        compile_source(HERE / "composition_test.c", BUILD / "composition", [READER / "sdk/driver"], shared=False)
        for cohort, commit in PINS.items():
            print(f"COHORT {cohort} {commit}, mode={MODE}", flush=True)
            cases = CASES if cohort == "pr40" else [x for x in CASES if not x.startswith("orphan-")] + [
                "pr41-orphan-retained-gap", "pr41-orphan-immediate-gap"]
            for case in cases:
                record = {"cohort": cohort, "host_commit": commit, "case": case, "status": "running"}
                summary["case_results"].append(record)
                command = [str(BUILD / "composition"), str(BUILD / f"host-{cohort}.so"),
                           str(BUILD / "raw.so"), str(BUILD / "mouse.so"), case]
                result = run(command, f"{cohort}-{case}")
                gap = case.startswith("pr41-orphan-")
                marker = "EXPECTED_HOST_CUSTODY_GAP" if gap else "PASS "
                if marker not in result.stdout:
                    raise RuntimeError(f"{cohort}/{case} missing expected result marker")
                record["status"] = "expected-host-custody-gap-observed" if gap else "pass"
        mutations = [
            ("false-success-close", "raw-retry",
             "    if (!host->release_checked(host->host.context, session->host_claim))\n        return false;",
             "    (void)host->release_checked(host->host.context, session->host_claim);",
             "!raw->close(0,token)"),
            ("false-open-discard", "orphan-retained",
             "        (void)release_session(free_slot);",
             "        *free_slot = (session_slot){0};",
             "custody && checked_calls == 1 && checked_log[0] == custody"),
            ("legacy-device-control", "control-failures",
             "host->control_claim(host->host.context,\n                   session->host_claim",
             "host->host.control(host->host.context,\n                   session->identity.device",
             "device-token control is forbidden"),
            ("shortened-checked-prefix", "table-gates",
             "api->host.struct_size < sizeof(*api)",
             "api->host.struct_size < sizeof(risc_usb_host_interrupt_v1)",
             "!raw_driver->start(&legacy_declared,1)"),
        ]
        original = (CANDIDATE / "driver.c").read_text()
        for name, case, before, after, assertion in mutations:
            if original.count(before) != 1:
                raise RuntimeError(f"Mutation anchor {name} is not unique in candidate source")
            mutant = BUILD / f"mutation-{name}.c"
            mutant.write_text(original.replace(before, after, 1))
            binary = BUILD / f"mutation-{name}.so"
            compile_source(mutant, binary, [BUILD / "pr40/sdk/driver"], shared=True)
            result = run([str(BUILD / "composition"), str(BUILD / "host-pr40.so"), str(binary),
                          str(BUILD / "mouse.so"), case], f"mutation-{name}", expected_assertion=assertion)
            summary["mutation_results"].append({
                "name": name, "case": case, "host_commit": PINS["pr40"],
                "status": "expected-assertion-rejection", "required_assertion": assertion,
                "exit_code": result.returncode, "mutated_source_sha256": file_digest(mutant),
            })
            print(f"PASS mutation rejected by intended assertion: {name}", flush=True)
        summary["status"] = "required-pr40-pass; pr41-compatible-cases-pass-with-two-host-custody-gap-witnesses"
        print("PASS required exact PR40 composition. PR41 compatibility passes with two explicitly observed host custody gaps.", flush=True)
        print(summary["qualification_scope"], flush=True)
    except Exception as exc:
        summary["status"] = "failed"
        summary["error"] = str(exc).replace(str(BUILD), "$BUILD").replace(str(ROOT), "$ROOT")
        print(f"FAIL {summary['error']}", file=sys.stderr)
    finally:
        encoded = (json.dumps(summary, indent=2, sort_keys=True) + "\n").encode()
        (EVIDENCE / "results.json").write_bytes(encoded)
        (EVIDENCE / "results.sha256").write_text(f"{sha(encoded)}  results.json\n")
        print(f"Evidence: dist/raw-hid-checked-cleanup-ci/{MODE}/results.json", flush=True)
if summary["status"] == "failed":
    raise SystemExit(1)
