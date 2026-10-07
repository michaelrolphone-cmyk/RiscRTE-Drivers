#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from normalize_xtensa_relocations import normalize

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"Drivers/s3_radio_iq_v1"
OUT=ROOT/"dist/s3-radio-iq-v1"

manifest=json.loads((SRC/"manifest.json").read_text())
if manifest['id'] != 's3-radio-iq-v1' or manifest['version'] != '0.1.4' or manifest['requires'] != [
    {'capability':'platform.radio.iq.resource','api':1}]:
    raise SystemExit("guarded IQ manifest/requirements differ")

cc=os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc:
    cc=str(Path.home()/".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file():
    raise SystemExit(f"missing Xtensa compiler: {cc}")

OUT.mkdir(parents=True,exist_ok=True)
elf=OUT/"driver.elf"
subprocess.run([
    cc,"-std=c11","-Os","-fPIC","-Wall","-Wextra","-Werror",
    "-mtext-section-literals","-mlongcalls","-fvisibility=hidden",
    "-ffreestanding","-fno-builtin","-nostdlib","-nostartfiles","-shared",
    "-I"+str(ROOT/"sdk/driver"),"-I"+str(SRC),
    "-Wl,--no-relax","-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
    str(SRC/"driver.c"),"-lgcc","-o",str(elf)
],check=True)
normalize(elf)

readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
if exports!={"t5_driver_get"}:
    raise SystemExit(f"unexpected exports: {sorted(exports)}")
nm=str(Path(cc).with_name(Path(cc).name.replace("gcc","nm")))
undef={line.split()[-1] for line in subprocess.check_output([nm,"-D",str(elf)],text=True).splitlines() if " U " in " "+line}
forbidden={"register_chipv7_phy","phy_init_param_set","phy_bbpll_en_usb","esp_rom_regi2c_read","esp_rom_regi2c_write","rom_pbus_rd"}
if undef - {'strcmp','memcpy','memset'}:
    raise SystemExit(f"driver imports modem symbols: {sorted(undef)}")

data=elf.read_bytes()
if len(data)<52 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
    raise SystemExit("not an ELF32 Xtensa shared driver")
record={'schema':1,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'size_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'imports':sorted(undef),
        'exports':sorted(exports),'compiler':subprocess.check_output([cc,'--version'],text=True).splitlines()[0]}
(OUT/"build-record.json").write_text(json.dumps(record,indent=2)+"\n")
shutil.copyfile(SRC/"LICENSE-eSpDR.txt",OUT/"LICENSE-eSpDR.txt")
(OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"built {manifest['id']} v{manifest['version']} {len(data)} bytes {record['sha256']}")
