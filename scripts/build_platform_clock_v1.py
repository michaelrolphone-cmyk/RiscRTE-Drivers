#!/usr/bin/env python3
import hashlib, json, os, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/platform_clock_v1"
OUT=ROOT/"dist/platform-clock-v1"
cc=os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc:
    cc=str(Path.home()/".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file():
    raise SystemExit(f"missing Xtensa compiler: {cc}")
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
data=elf.read_bytes()
if data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[18:20],"little")!=94:
    raise SystemExit("not an ELF32 Xtensa driver")
manifest=json.loads((SRC/"manifest.json").read_text())
manifest.update(size_bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
(OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"built {manifest['id']} v{manifest['version']} {len(data)} bytes {manifest['sha256']}")
