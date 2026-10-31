#!/usr/bin/env python3
"""Build a separate unpublished native CDC candidate, never replay/accepted bytes."""
import hashlib, json, os, re, shutil, subprocess, shlex
from pathlib import Path
from normalize_xtensa_relocations import normalize
from native_driver_toolchain import compiler
from build_platform_clock_v1 import file_sha256

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/usb_cdc_native"
OUT=ROOT/"dist/usb-cdc-acm-native-stream"
CANONICAL_SIZE=None
CANONICAL_SHA256=None
def input_snapshot():
    paths={p for directory in (SRC,ROOT/"sdk/driver") for p in directory.rglob("*") if p.is_file()}
    paths.update(ROOT/"scripts"/name for name in ("build_usb_cdc_native.py","build_platform_clock_v1.py","native_driver_toolchain.py","normalize_xtensa_relocations.py"))
    return {str(p.relative_to(ROOT)):file_sha256(p) for p in sorted(paths)}


def main():
    manifest=json.loads((SRC/"manifest.json").read_text())
    required={
     "type":"driver","id":"usb-cdc-acm","driver_abi":2,
     "architecture":"xtensa-esp32s3","file_name":"driver.elf",
     "requires":[{"capability":"usb.host","api":1},{"capability":"platform.clock","api":1}],
     "provides":[{"capability":"serial.port","api":1}],
     "status":"experimental-local-native-stream"
    }
    # The functional ABI-2 implementation succeeds the canonical lineage, not a
    # second -v2 product. Reader source/release 9c8783d6/19bb3dcb publish 0.1.8.
    version=manifest.get("version", "")
    if not isinstance(version,str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+",version):
        raise SystemExit("native CDC requires a numeric package version")
    if tuple(map(int,version.split("."))) <= (0,1,8):
        raise SystemExit("native CDC must be newer than canonical published 0.1.8")
    if {k:v for k,v in manifest.items() if k!="version"}!=required:
        raise SystemExit("native CDC candidate manifest mismatch")
    cc = compiler()
    inputs = input_snapshot()
    readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
    libgcc=subprocess.check_output([cc,"-print-libgcc-file-name"],text=True).strip()
    tools={"compiler":{"path":cc,"sha256":file_sha256(cc),"version":subprocess.check_output([cc,"--version"],text=True)},
           "libgcc":{"path":libgcc,"sha256":file_sha256(libgcc)},
           "readelf":{"path":readelf,"sha256":file_sha256(readelf)}}
    OUT.mkdir(parents=True,exist_ok=True)
    elf=OUT/"driver.elf"
    command=[cc, "-std=c11","-Os","-fPIC","-mtext-section-literals","-mlongcalls",
     "-fvisibility=hidden","-nostdlib","-nostartfiles","-shared",
     "-I"+str(ROOT/"sdk/driver"),"-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
     str(SRC/"driver.c"),"-lgcc","-o",str(elf)]
    # Separate dependency-only pass: original object-build command is unchanged.
    scan_command=command[:command.index("-Wl,--hash-style=sysv")]+["-M","-MT","native_cdc_dependencies",str(SRC/"driver.c")]
    scan_raw=subprocess.check_output(scan_command,text=True)
    if not scan_raw.startswith("native_cdc_dependencies:"):
        raise SystemExit("unexpected native CDC dependency scan format")
    dependency_paths=shlex.split(scan_raw.split(":",1)[1].replace("\\\n"," "))
    if not dependency_paths or str(SRC/"driver.c") not in dependency_paths:
        raise SystemExit("native CDC dependency scan omitted translation unit")
    dependencies={str(Path(name).resolve()):file_sha256(Path(name)) for name in dependency_paths}
    subprocess.run(command,check=True)
    normalize(elf)
    readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
    symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
    exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
    if exports!={"t5_driver_get"}: raise SystemExit(f"unexpected exports: {sorted(exports)}")
    imports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[6]=="UND"}
    if not imports.issubset({"memcpy","memset","strcmp"}):
        raise SystemExit(f"unexpected imports: {sorted(imports)}")
    data=elf.read_bytes()
    if not 52<=len(data)<=256*1024 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
        raise SystemExit("invalid Xtensa shared driver")
    digest=hashlib.sha256(data).hexdigest()
    meta=dict(manifest)
    meta.update(size_bytes=len(data),sha256=digest,
                canonical_size_bytes=CANONICAL_SIZE,canonical_sha256=CANONICAL_SHA256,
                candidate_only=True, byte_parity=False)
    (OUT/"manifest.json").write_text(json.dumps(meta,indent=2)+"\n")
    print(f"built usb-cdc-acm v{manifest['version']} {len(data)} bytes {digest} byte_parity={meta['byte_parity']}")

    if inputs != input_snapshot() or any(file_sha256(v["path"]) != v["sha256"] for v in tools.values()) or any(file_sha256(name) != digest for name,digest in dependencies.items()):
        raise SystemExit("native CDC build inputs changed during build; no provenance receipt issued")
    receipt={"schema":1,"scope":"current native CDC candidate build; not historical accepted bytes, product selection, hardware or behavioral equivalence",
        "inputs":inputs,"tools":tools,"command":command,
        "compiled_translation_units":[str(SRC/"driver.c")],
        "dependency_scan":{"command":scan_command,"raw":scan_raw,"files":dependencies,"scope":"separate dependency-only compiler pass under matching preprocessing options; reported header selection, not trace instrumentation of the object build"},
        "input_coverage":"complete native source directory and all SDK/driver files (SDK snapshot is an over-approximation, not proof every header was consumed); builder/helper bytes pinned",
        "closure_limits":"implicit compiler subprocesses, standard headers, system libraries and environment are not exhaustively captured",
        "output":{"elf_sha256":digest,"elf_bytes":len(data),"manifest_sha256":file_sha256(OUT/"manifest.json")},
        "candidate_identity":{"id":manifest["id"],"version":manifest["version"],"source_path":"Drivers/usb_cdc_native"},
        "receipt_identity":"run provenance includes invocation paths; not a deterministic build identifier"}
    raw=json.dumps(receipt,indent=2,sort_keys=True)+"\n"
    (OUT/("build-provenance-"+hashlib.sha256(raw.encode()).hexdigest()+".json")).write_text(raw)
    return receipt

if __name__ == "__main__":
    main()
