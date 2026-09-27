#!/usr/bin/env python3
import hashlib, json, os, shutil, subprocess
from pathlib import Path
from normalize_xtensa_relocations import normalize
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/usb_xinput_gamepad"
OUT=ROOT/"dist/usb-xinput-gamepad"
CANONICAL_SIZE=11088
CANONICAL_SHA256="e2cee3355937d7815fb5685598cd2411c7d508bb36424f00636ec7495f654c26"
manifest=json.loads((SRC/"manifest.json").read_text())
required={"type":"driver","id":"usb-xinput-gamepad","version":"0.1.3","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"usb.host","api":1},{"capability":"platform.clock","api":1}],"provides":[{"capability":"usb.xinput.gamepad","api":1}],"status":"experimental-unpublished"}
if manifest!=required: raise SystemExit("usb-xinput-gamepad manifest mismatch")
cc=os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc: cc=str(Path.home()/".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file(): raise SystemExit(f"missing Xtensa compiler: {cc}")
OUT.mkdir(parents=True,exist_ok=True); elf=OUT/"driver.elf"
subprocess.run([cc,"-std=c11","-Os","-fPIC","-mtext-section-literals","-mlongcalls","-fvisibility=hidden","-nostdlib","-nostartfiles","-shared","-I"+str(ROOT/"sdk/driver"),"-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",str(SRC/"driver.c"),"-lgcc","-o",str(elf)],check=True)
normalize(elf)
readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
if exports!={"t5_driver_get"}: raise SystemExit(f"unexpected exports: {sorted(exports)}")
imports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[6]=="UND"}
if imports!={"memcpy","memset"}: raise SystemExit(f"unexpected imports: {sorted(imports)}")
data=elf.read_bytes()
if not 52<=len(data)<=256*1024 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94: raise SystemExit("invalid Xtensa shared driver")
digest=hashlib.sha256(data).hexdigest()
meta=dict(manifest);meta.update(size_bytes=len(data),sha256=digest,canonical_size_bytes=CANONICAL_SIZE,canonical_sha256=CANONICAL_SHA256,byte_parity=(len(data)==CANONICAL_SIZE and digest==CANONICAL_SHA256))
(OUT/"manifest.json").write_text(json.dumps(meta,indent=2)+"\n")
print(f"built usb-xinput-gamepad v{manifest['version']} {len(data)} bytes {digest} byte_parity={meta['byte_parity']}")
