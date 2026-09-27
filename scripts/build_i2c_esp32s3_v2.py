#!/usr/bin/env python3
import hashlib, json, os, shutil, subprocess
from pathlib import Path
from normalize_xtensa_relocations import normalize

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/i2c_esp32s3_v2"
OUT=ROOT/"dist/i2c-esp32s3-v2"
BRIDGE="risc_fw_i2c_transact_v1"
EXPECTED_IMPORTS={
    BRIDGE,
    "vQueueDelete",
    "xQueueCreateMutex",
    "xQueueGenericSend",
    "xQueueSemaphoreTake",
}
CANONICAL_SIZE=24496
CANONICAL_SHA256="7b8f51f62da71e99949b093b6cdc531435a0cec01f87740921f9436544f8bd9c"

manifest=json.loads((SRC/"manifest.json").read_text())
required={
 "type":"driver","id":"i2c-esp32s3-v2","version":"0.1.5","driver_abi":2,
 "architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[],
 "provides":[{"capability":"i2c.bus","api":1}],
 "status":"experimental-unpublished","board":"t5s3-pro"
}
if manifest!=required: raise SystemExit("i2c-esp32s3-v2 manifest mismatch")
cc=os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc: cc=str(Path.home()/".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file(): raise SystemExit(f"missing Xtensa compiler: {cc}")
OUT.mkdir(parents=True,exist_ok=True)
elf=OUT/"driver.elf"
subprocess.run([cc,"-std=c11","-Os","-fPIC","-mtext-section-literals","-mlongcalls",
 "-ffunction-sections","-fdata-sections","-fvisibility=hidden","-nostdlib","-nostartfiles","-shared",
 "-I"+str(ROOT/"sdk/driver"),"-I"+str(ROOT/"scripts/xtensa_stubs"),
 "-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL","-Wl,-Bsymbolic",
 "-Wl,--gc-sections","-Wl,-T,"+str(SRC/"loader_sections.ld"),
 "-Wl,--version-script,"+str(SRC/"exports.map"),str(SRC/"driver.c"),"-lgcc","-o",str(elf)],check=True)
normalize(elf)
readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
nm=str(Path(cc).with_name(Path(cc).name.replace("gcc","nm")))
symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
if exports!={"t5_driver_get"}: raise SystemExit(f"unexpected exports: {sorted(exports)}")
undefined=subprocess.check_output([nm,"-u",str(elf)],text=True)
imports={line.split()[-1] for line in undefined.splitlines() if line.split()}
forbidden=sorted(name for name in imports if name.startswith(("i2c_","gpio_","rtc_gpio_","rtc_io_","periph_module_","t5_","usb_")))
unexpected=sorted(imports-EXPECTED_IMPORTS)
missing=sorted(EXPECTED_IMPORTS-imports)
if forbidden or unexpected or missing:
    raise SystemExit(f"invalid privileged imports: forbidden={forbidden} unexpected={unexpected} missing={missing}")
atomic_helpers=sorted(name for name in imports if name.startswith("__atomic") or name.startswith("__sync"))
if atomic_helpers: raise SystemExit(f"atomic helper imports are forbidden in 0.1.5: {atomic_helpers}")
data=elf.read_bytes()
if not 52<=len(data)<=256*1024 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
    raise SystemExit("invalid Xtensa shared driver")
digest=hashlib.sha256(data).hexdigest()
meta=dict(manifest)
meta.update(size_bytes=len(data),sha256=digest,
            canonical_size_bytes=CANONICAL_SIZE,canonical_sha256=CANONICAL_SHA256,
            byte_parity=(len(data)==CANONICAL_SIZE and digest==CANONICAL_SHA256))
(OUT/"manifest.json").write_text(json.dumps(meta,indent=2)+"\n")
(OUT/"unresolved-symbols.txt").write_text(undefined)
print(f"built i2c-esp32s3-v2 v{manifest['version']} {len(data)} bytes {digest} byte_parity={meta['byte_parity']}")
