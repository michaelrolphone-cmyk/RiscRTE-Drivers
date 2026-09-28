#!/usr/bin/env python3
# Repository-independent ESP32-S3 usb.controller build and structural audit.
#
# Derived from the current upstream controller probe/audit implementation.
# It uses the exact T5S3 board definition, derives target flags from PlatformIO,
# rebuilds the pinned ESP-IDF v4.4.7 USB/PHY/SOC subset as PIC, links a provider
# ET_DYN, audits ownership/exports/relocations, and records canonical byte parity.

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Drivers/usb_controller_esp32s3/driver.cpp"
PHY_GPIO_SOURCE = ROOT / "Drivers/usb_controller_esp32s3/phy_gpio.c"
EXPORT_MAP = ROOT / "Drivers/usb_controller_esp32s3/exports.map"
MANIFEST = ROOT / "Drivers/usb_controller_esp32s3/manifest.json"
BOARD = ROOT / "boards/t5s3-pro.json"
OUT = ROOT / "dist/usb-controller-esp32s3"
PROBE = OUT / "toolchain-probe"
CACHE = ROOT / "dist/idf-usb-source/v4.4.7"
IDF_TAG = "v4.4.7"
CANONICAL_SIZE = 783576
CANONICAL_SHA256 = "f67064a9678a7b048e40cbf411d46653b69006aec07b9f2c95428597cc706e0e"
USB = ("hcd_dwc.c","hub.c","usb_helpers.c","usb_host.c","usb_private.c","usbh.c","usb_phy.c")
HAL = ("usb_hal.c","usb_phy_hal.c","usb_dwc_hal.c")
SOC = ("usb_phy_periph.c","usb_periph.c","gpio_periph.c")
MMIO = ("GPIO","RTCCNTL","SYSTEM","USB_DWC","USB_SERIAL_JTAG","USB_WRAP")
INTERNAL = ("usb_host_","usbh_","hcd_","hub_","usb_phy_","usb_new_phy","urb_","risc_usb_enum_")
EXPECTED = {
    "type":"driver","id":"usb-controller-esp32s3","version":"0.1.18",
    "driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf",
    "requires":[{"capability":"board.power.vbus","api":1}],
    "provides":[{"capability":"usb.controller","api":1}],
    "status":"experimental-hardware-port-not-yet-linkable",
}

def tool(compiler, suffix):
    name = compiler.name
    if not name.endswith(("g++","gcc")):
        raise RuntimeError("unexpected Xtensa compiler: " + str(compiler))
    return compiler.with_name(name[:-3] + suffix)

def instrument_hub(source):
    def once(old,new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError("pinned IDF enumeration anchor changed: " + old)
        source = source.replace(old,new,1)
    decl = """
extern void risc_usb_enum_reset(void);
extern void risc_usb_enum_stage(const char *stage);
extern void risc_usb_enum_error(const char *format, ...);
#define RISC_USB_ENUM_ERROR(format, ...) do { \
 risc_usb_enum_error(format, ##__VA_ARGS__); \
 ESP_LOGE(HUB_DRIVER_TAG, format, ##__VA_ARGS__); \
} while (0)
"""
    once('#include "sdkconfig.h"', '#include "sdkconfig.h"\n' + decl)
    once("case HCD_PORT_EVENT_CONNECTION: {",
         "case HCD_PORT_EVENT_CONNECTION: {\n            risc_usb_enum_reset();")
    once("enum_ctrl_t *enum_ctrl = &p_hub_driver_obj->single_thread.enum_ctrl;\n    switch (enum_ctrl->stage)",
         "enum_ctrl_t *enum_ctrl = &p_hub_driver_obj->single_thread.enum_ctrl;\n    risc_usb_enum_stage(enum_stage_strings[enum_ctrl->stage]);\n    switch (enum_ctrl->stage)")
    if source.count('ESP_LOGE(HUB_DRIVER_TAG, "') != 14:
        raise ValueError("pinned IDF enumeration error sites changed")
    return source.replace('ESP_LOGE(HUB_DRIVER_TAG, "', 'RISC_USB_ENUM_ERROR("')

def probe_entry():
    if not BOARD.is_file():
        raise FileNotFoundError("missing exact T5S3 board definition")
    (PROBE/"src").mkdir(parents=True,exist_ok=True)
    (PROBE/"src/NativeUsbBridge.cpp").write_text("int riscrte_usb_controller_toolchain_probe(void){return 0;}\n")
    (PROBE/"platformio.ini").write_text(f"""[platformio]
default_envs = probe
boards_dir = {ROOT / "boards"}
[env:probe]
platform = espressif32@6.13.0
board = t5s3-pro
framework = arduino
build_flags =
 -DBOARD_HAS_PSRAM
 -DARDUINO_LOOP_STACK_SIZE=16384
 -DCONFIG_ELF_LOADER=1
 -DCONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=1
 -DCONFIG_ELF_LOADER_LOAD_PSRAM=1
 -DCONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR=1
 -DCONFIG_ELF_LOADER_CACHE_OFFSET=1
 -DCONFIG_ELF_LOADER_LIBC_SYMBOLS=1
 -DARDUINO_USB_MODE=1
 -DARDUINO_USB_CDC_ON_BOOT=1
 -DBOARD_T5S3_PRO
 -DENABLE_SERIAL_LOG
 -DLOG_LEVEL=2
 -std=gnu++2a
 -Wno-bidi-chars
 -fno-exceptions
build_unflags =
 -std=gnu++11
 -fexceptions
""")
    subprocess.run(["pio","run","-d",str(PROBE),"-e","probe","-t","compiledb"],check=True)
    entries=json.loads((PROBE/"compile_commands.json").read_text())
    matches=[e for e in entries if Path(e["file"]).as_posix().endswith("src/NativeUsbBridge.cpp")]
    if len(matches)!=1: raise RuntimeError("expected one ESP32-S3 probe compile command")
    return matches[0]

def compile_target(argv,entry,source,output,c=False,extra=()):
    cmd=[a for a in argv if a!="-Wno-bidi-chars"]
    src=out=False
    for i,a in enumerate(cmd):
        if Path(a).as_posix().endswith("src/NativeUsbBridge.cpp"):
            cmd[i]=str(source); src=True
        elif i and cmd[i-1]=="-o":
            cmd[i]=str(output); out=True
    if not src or not out or "-c" not in cmd: raise RuntimeError("cannot derive target compile command")
    if c:
        compiler=Path(cmd[0]); cmd[0]=str(tool(compiler,"gcc"))
        cmd=[a for a in cmd if not a.startswith("-std=") and a!="-fno-rtti"]+["-std=gnu11"]
    cmd += ["-fPIC","-fvisibility=hidden","-I"+str(ROOT/"sdk/driver"),*extra]
    subprocess.run(cmd,cwd=entry.get("directory",str(PROBE)),check=True)
    if not output.is_file() or not output.stat().st_size: raise RuntimeError("compiler produced no object")

def idf_sources():
    if not (CACHE/"components/usb/usb_host.c").is_file():
        if CACHE.exists(): raise RuntimeError("incomplete IDF source checkout")
        CACHE.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(["git","clone","--depth=1","--branch",IDF_TAG,"--filter=blob:none","--sparse",
                        "https://github.com/espressif/esp-idf.git",str(CACHE)],cwd=ROOT,check=True)
    subprocess.run(["git","-C",str(CACHE),"sparse-checkout","set","components/usb","components/hal","components/soc/esp32s3"],check=True)
    commit=subprocess.check_output(["git","-C",str(CACHE),"rev-parse","HEAD"],text=True).strip()
    (OUT/"idf-usb-source.txt").write_text(f"{IDF_TAG} {commit}\n")
    usb=CACHE/"components/usb"; hal=CACHE/"components/hal"; soc=CACHE/"components/soc/esp32s3"
    paths=[*(usb/n for n in USB),*(hal/n for n in HAL),*(soc/n for n in SOC)]
    if any(not p.is_file() for p in paths): raise RuntimeError("pinned IDF source set incomplete")
    return usb,hal,soc,paths

def mmio_args(soc):
    linker=soc/"ld/esp32s3.peripherals.ld"
    vals=dict(re.findall(r"PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)",linker.read_text()))
    if any(n not in vals for n in MMIO): raise RuntimeError("pinned peripheral map incomplete")
    return [f"-Wl,--defsym,{n}={vals[n]}" for n in MMIO]

def audit(elf,compiler):
    nm=tool(compiler,"nm"); readelf=tool(compiler,"readelf")
    raw=subprocess.check_output([str(nm),"-u",str(elf)],text=True)
    (OUT/"unresolved-symbols.txt").write_text(raw)
    imports=sorted(line.split()[-1] for line in raw.splitlines() if line.split())
    offenders=[n for n in imports if any(n.startswith(p) for p in INTERNAL)]
    if offenders: raise RuntimeError("unresolved USB internals: "+",".join(offenders))
    if "gpio_set_drive_capability" in imports: raise RuntimeError("PHY GPIO ownership leaked")
    syms=subprocess.check_output([str(readelf),"--dyn-syms","--wide",str(elf)],text=True)
    exports={f[7] for line in syms.splitlines() if len((f:=line.split()))>=8 and f[3]=="FUNC" and f[4]=="GLOBAL" and f[6]!="UND"}
    if exports!={"t5_driver_get"}: raise RuntimeError("unexpected exports: "+repr(sorted(exports)))
    defined=subprocess.check_output([str(nm),"-D","--defined-only",str(elf)],text=True)
    extras={line.split()[-1] for line in defined.splitlines() if line.split()}-{"t5_driver_get","__bss_start","_edata","_end"}
    if extras: raise RuntimeError("unexpected exported ELF data: "+repr(sorted(extras)))
    if "TEXTREL" in subprocess.check_output([str(readelf),"-d",str(elf)],text=True):
        raise RuntimeError("TEXTREL present")
    data=elf.read_bytes()
    if len(data)<52 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
        raise RuntimeError("not ELF32 little-endian Xtensa ET_DYN")
    digest=hashlib.sha256(data).hexdigest()
    return imports,len(data),digest,len(data)==CANONICAL_SIZE and digest==CANONICAL_SHA256

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--require-byte-parity",action="store_true"); args=ap.parse_args()
    manifest=json.loads(MANIFEST.read_text())
    if manifest!=EXPECTED: raise SystemExit("usb-controller-esp32s3 manifest mismatch")
    OUT.mkdir(parents=True,exist_ok=True)
    entry=probe_entry(); argv=list(entry["arguments"]) if "arguments" in entry else shlex.split(entry["command"])
    controller=OUT/"controller.o"; compile_target(argv,entry,SOURCE,controller,extra=("-Wall","-Wextra","-Werror"))
    compiler=Path(argv[0]); nm=tool(compiler,"nm")
    undef=subprocess.check_output([str(nm),"-u",str(controller)],text=True)
    if "usb_host_install" not in undef or "usb_host_transfer_submit" not in undef: raise RuntimeError("not real IDF USB controller")
    if any(n in undef for n in ("nativeUsb","NativeUsbBridge","t5_usb_")): raise RuntimeError("legacy firmware USB bridge import")
    usb,hal,soc,paths=idf_sources()
    inc=("-I"+str(usb/"include"),"-I"+str(usb/"private_include"),"-I"+str(hal/"include"),"-I"+str(hal/"esp32s3/include"),"-I"+str(soc),"-I"+str(soc/"include"))
    objs=[controller]
    for i,p in enumerate(paths):
        source=p
        if p.name=="hub.c":
            source=OUT/"hub-enumeration-diagnostics.c"; source.write_text(instrument_hub(p.read_text()))
        obj=OUT/f"idf-usb-{i}-{p.name}.o"; compile_target(argv,entry,source,obj,c=True,extra=inc); objs.append(obj)
    phy=OUT/"phy-gpio.o"; compile_target(argv,entry,PHY_GPIO_SOURCE,phy,c=True,extra=(*inc,"-Wall","-Wextra","-Werror")); objs.append(phy)
    elf=OUT/"driver.elf"
    subprocess.run([str(compiler),"-shared","-nostdlib","-nostartfiles","-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
                    "-Wl,-Bsymbolic","-Wl,--version-script,"+str(EXPORT_MAP),*mmio_args(soc),*map(str,objs),"-lgcc","-o",str(elf)],
                   cwd=ROOT,check=True)
    imports,size,digest,parity=audit(elf,compiler)
    meta=dict(manifest); meta.update(size_bytes=size,sha256=digest,canonical_size_bytes=CANONICAL_SIZE,
                                     canonical_sha256=CANONICAL_SHA256,byte_parity=parity,unresolved_imports=imports)
    (OUT/"manifest.json").write_text(json.dumps(meta,indent=2)+"\n")
    print(f"built usb-controller-esp32s3 v{manifest['version']} {size} bytes {digest} byte_parity={parity}")
    if args.require_byte_parity and not parity: raise SystemExit("usb-controller-esp32s3 canonical release byte parity failed")
if __name__=="__main__": main()
