#!/usr/bin/env python3
import hashlib, json, os, shutil, subprocess
from pathlib import Path
from normalize_xtensa_relocations import normalize

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/gt911_touch"
OUT=ROOT/"dist/gt911-touch"
manifest=json.loads((SRC/"manifest.json").read_text())
required={
 "type":"driver","id":"gt911-touch","version":"0.1.1","driver_abi":2,
 "architecture":"xtensa-esp32s3","file_name":"driver.elf",
 "requires":[{"capability":"i2c.bus","api":1},{"capability":"platform.clock","api":1}],
 "provides":[{"capability":"input.touch.raw","api":1}],
 "status":"experimental-unpublished"
}
if manifest!=required: raise SystemExit("gt911-touch manifest mismatch")
cc=os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc: cc=str(Path.home()/".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file(): raise SystemExit(f"missing Xtensa compiler: {cc}")
OUT.mkdir(parents=True,exist_ok=True)
elf=OUT/"driver.elf"
subprocess.run([cc,"-std=c11","-Os","-fPIC","-mtext-section-literals","-mlongcalls",
 "-fvisibility=hidden","-nostdlib","-nostartfiles","-shared",
 "-I"+str(ROOT/"sdk/driver"),"-I"+str(ROOT/"scripts/xtensa_stubs"),
 "-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
 str(SRC/"driver.c"),"-lgcc","-o",str(elf)],check=True)
normalize(elf)
readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
if exports!={"t5_driver_get"}: raise SystemExit(f"unexpected exports: {sorted(exports)}")
data=elf.read_bytes()
if not 52<=len(data)<=256*1024 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
    raise SystemExit("invalid Xtensa shared driver")
manifest.update(size_bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
(OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"built gt911-touch {len(data)} bytes {manifest['sha256']}")\nif os.environ.get("GITHUB_ACTIONS") == "true" and (len(data) != 42976 or manifest["sha256"] != "44d753b736a2a433549ab500a3cae52f1e2844f79332fd119fc8df8d57cd11f4"):\n    raise SystemExit("gt911-touch canonical release byte parity failed")
