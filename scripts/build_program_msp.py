#!/usr/bin/env python3
import hashlib, json, os, shutil, subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/program_msp"
OUT=ROOT/"dist/program-msp"
CANONICAL_SIZE=9128
CANONICAL_SHA256="19b0999f41e45522e877097addf3cfd55651b2fd00ae925fa6084b769b66527f"
manifest=json.loads((SRC/"manifest.json").read_text())
required={
 "type":"driver","id":"program-msp","version":"0.1.0","driver_abi":2,
 "architecture":"xtensa-esp32s3","file_name":"driver.elf",
 "requires":[{"capability":"debug.vendor.msp","api":1}],
 "provides":[{"capability":"program.msp","api":1}],
 "status":"experimental-unpublished"
}
if manifest!=required: raise SystemExit("program-msp manifest mismatch")
cc=os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc: cc=str(Path.home()/".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file(): raise SystemExit(f"missing Xtensa compiler: {cc}")
OUT.mkdir(parents=True,exist_ok=True)
elf=OUT/"driver.elf"
subprocess.run([cc,"-std=c11","-Os","-fPIC","-mtext-section-literals","-mlongcalls",
 "-fvisibility=hidden","-nostdlib","-nostartfiles","-shared",
 "-I"+str(ROOT/"sdk/driver"),"-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
 str(SRC/"driver.c"),"-lgcc","-o",str(elf)],check=True)
readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
if exports!={"t5_driver_get"}: raise SystemExit(f"unexpected exports: {sorted(exports)}")
imports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[6]=="UND"}
if imports!={"memcmp","memcpy","memset"}:
    raise SystemExit(f"unexpected imports: {sorted(imports)}")
data=elf.read_bytes()
if not 52<=len(data)<=256*1024 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
    raise SystemExit("invalid Xtensa shared driver")
digest=hashlib.sha256(data).hexdigest()
if len(data)!=CANONICAL_SIZE or digest!=CANONICAL_SHA256:
    raise SystemExit(f"canonical byte parity mismatch: got {len(data)} {digest}, expected {CANONICAL_SIZE} {CANONICAL_SHA256}")
meta=dict(manifest)
meta.update(size_bytes=len(data),sha256=digest,byte_parity=True)
(OUT/"manifest.json").write_text(json.dumps(meta,indent=2)+"\n")
print(f"built program-msp v{manifest['version']} {len(data)} bytes {digest} byte_parity=true")
