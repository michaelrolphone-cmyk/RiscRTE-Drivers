#!/usr/bin/env python3
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
from normalize_xtensa_relocations import normalize
from native_driver_toolchain import compiler
from cst9217_sdk import stage_sdk

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/cst9217_touch"
OUT=ROOT/"dist/cst9217-touch"
parser=argparse.ArgumentParser(description="Build scoped CST9217 against the selected Runtime SDK")
parser.add_argument("--runtime",required=True,type=Path)
args=parser.parse_args()
manifest=json.loads((SRC/"manifest.json").read_text())
required={
 "type":"driver","id":"cst9217-touch","version":"0.1.0","driver_abi":2,
 "architecture":"xtensa-esp32s3","file_name":"driver.elf",
 "requires":[{"capability":name,"api":1} for name in
             ("hardware.device","i2c.bus","platform.gpio","platform.clock","power.rail","platform.sync")],
 "hardware_compatibility":[{"compatible":"hynitron,cst9217","revisions":["unspecified"],
                            "config_type":"touch.i2c","config_version":2}],
 "provides":[{"capability":"input.touch.raw","api":1}],
 "status":"experimental-unpublished"
}
if manifest!=required: raise SystemExit("cst9217-touch manifest mismatch")
cc = compiler()
OUT.mkdir(parents=True,exist_ok=True)
elf=OUT/"driver.elf"
with tempfile.TemporaryDirectory(prefix="cst9217-sdk-") as directory:
 sdk=stage_sdk(ROOT,args.runtime.resolve(),Path(directory))
 subprocess.run([cc, "-std=c11","-Os","-fPIC","-mtext-section-literals","-mlongcalls",
  "-fvisibility=hidden","-nostdlib","-nostartfiles","-shared",
  "-I"+str(sdk),"-I"+str(ROOT/"components/co5300/include"),
  "-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
  str(SRC/"driver.c"),"-lgcc","-o",str(elf)],check=True)
normalize(elf)
readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
imports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[6]=="UND" and f[7]}
if not imports <= {"memcpy","memset","memcmp"}: raise SystemExit(f"unexpected imports: {sorted(imports)}")
if exports!={"t5_driver_get"}: raise SystemExit(f"unexpected exports: {sorted(exports)}")
data=elf.read_bytes()
if not 52<=len(data)<=256*1024 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
    raise SystemExit("invalid Xtensa shared driver")
manifest.update(size_bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
(OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"built cst9217-touch {len(data)} bytes {manifest['sha256']}")
