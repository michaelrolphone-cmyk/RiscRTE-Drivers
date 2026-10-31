#!/usr/bin/env python3
"""Build current BQ25896 independently of immutable historical release replay."""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import shutil
from pathlib import Path
from normalize_xtensa_relocations import normalize
from native_driver_toolchain import compiler
# Reuse the already-qualified generic staging/hash helpers; no clock build runs.
from build_platform_clock_v1 import file_sha256, publish_outputs
ROOT = Path(__file__).resolve().parents[1]
HISTORICAL_TREE = '17c99241a5606380a3ce5755e35f2f4a28b1ab1a'
HISTORICAL_BUILD = 'a7ae39994603c4545a3c014f09051ecfc12084d2'
CANONICAL_SIZE = 12660
CANONICAL_SHA256 = '5f50b5eb048085e3128939b6bed3693a9a44ebe27c9e2cae3b40dd725aa6a4c4'
HEADERS = {'RiscBq25896ProfileV1.h': '04e33c1b5a3385c2bedf275b85aa62a5dbfbec8cb38c2f3879ebb413e70a0c98', 'RiscUsbVbusV1.h': '27efb8b327d38c3ca6a97d5f2936094659f67d69e89b49afcba9fc4ee5db6a6a', 'RiscPlatformClockV1.h': '41a12f6529f38731ee874f8bc166e1cf89fe376a1448fe5a4dd4c9abac90e5e2', 'RiscProviderV2.h': '358ac93f86a3e228c2a9c9cfca0b758571db43453bce1e4c5e38402096064ea0', 'RiscI2cBusV1.h': 'a6b769efbbf7f3837a6a348689b0aee84bf687ae8e18a31d0296f3bfa6ac2004'}
NORMALIZER = 'ea36ed078221b7f29b3d6f84dac2768e249942c7325a232a6115596f507f1456'


def select_source(root, current, temporary):
    if current:
        return root/'Drivers/bq25896', {'mode':'current-source','expected_version':'0.1.9'}
    source=temporary/'source';source.mkdir()
    for name in ('driver.c','manifest.json'):
        (source/name).write_bytes(subprocess.check_output(['git','-C',str(root),'show',HISTORICAL_TREE+':'+name]))
    return source, {'mode':'historical','expected_version':'0.1.6','source_tree':HISTORICAL_TREE}


def select_output(root, current, requested):
    historical=(root/'dist/board-power-t5s3-v2').resolve()
    output=Path(requested).resolve() if requested else (root/('dist/board-power-t5s3-v2-current' if current else 'dist/board-power-t5s3-v2')).resolve()
    if current and (output==historical or historical in output.parents):
        raise ValueError('current-source output must not overwrite historical output')
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-source',action='store_true')
    parser.add_argument('--require-byte-parity',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.current_source and args.require_byte_parity:parser.error('current source is not historical release replay')
    try:output=select_output(ROOT,args.current_source,args.output)
    except ValueError as exc:parser.error(str(exc))
    with tempfile.TemporaryDirectory(prefix='board-power-t5s3-build-') as temp:
        temp=Path(temp);source,provenance=select_source(ROOT,args.current_source,temp)
        staged=temp/'output';staged.mkdir()
        build(source,staged,provenance,args.require_byte_parity,temp)
        publish_outputs(staged,output)


def build(SRC,OUT,provenance,require_byte_parity,temporary):
    includes=ROOT/'sdk/driver'
    if require_byte_parity:
        includes=temporary/'historical-sdk';includes.mkdir()
        for name,pin in HEADERS.items():
            data=subprocess.check_output(['git','-C',str(ROOT),'show',HISTORICAL_BUILD+':sdk/driver/'+name])
            if hashlib.sha256(data).hexdigest()!=pin:raise ValueError('historical header mismatch: '+name)
            (includes/name).write_bytes(data)
        if file_sha256(ROOT/'scripts/normalize_xtensa_relocations.py')!=NORMALIZER:raise ValueError('historical normalizer changed')
    manifest = json.loads((SRC / "manifest.json").read_text())
    required = {
        "type": "driver",
        "id": "board-power-t5s3-v2",
        "version": provenance["expected_version"],
        "driver_abi": 2,
        "architecture": "xtensa-esp32s3",
        "file_name": "driver.elf",
        "requires": [{"capability": "i2c.bus", "api": 1}, {"capability": "platform.clock", "api": 1}, {"capability": "board.power.bq25896.profile", "api": 1}],
        "provides": [{"capability": "board.power.vbus", "api": 1}],
        "status": "experimental-unpublished",
    }
    if manifest != required:
        raise SystemExit("board-power-t5s3-v2 manifest mismatch")

    cc = compiler()
    provenance=dict(provenance)
    provenance['source_files']={p.name:file_sha256(p) for p in sorted(SRC.iterdir()) if p.is_file()}
    provenance['selected_sdk_inputs']={str(p.relative_to(includes)):file_sha256(p) for p in sorted(includes.rglob('*')) if p.is_file()}
    provenance['historical_build_commit']=HISTORICAL_BUILD if require_byte_parity else None
    provenance['compiler']=cc
    provenance['compiler_sha256']=file_sha256(cc)
    provenance['compiler_version']=subprocess.check_output([cc,'--version'],text=True)
    libgcc=subprocess.check_output([cc,'-print-libgcc-file-name'],text=True).strip()
    provenance['libgcc']={'path':libgcc,'sha256':file_sha256(libgcc)}
    provenance['build_inputs']={str(p.relative_to(ROOT)):file_sha256(p) for p in sorted((ROOT/'sdk/driver').rglob('*')) if p.is_file()}
    for name in ('build_board_power_t5s3_v2.py','build_platform_clock_v1.py','native_driver_toolchain.py','normalize_xtensa_relocations.py'):
        provenance['build_inputs']['scripts/'+name]=file_sha256(ROOT/'scripts'/name)
    provenance['scope']='run provenance, not hardware/runtime equivalence; temporary paths vary'
    provenance['toolchain_closure']='compiler/libgcc/readelf pinned; implicit subprocess/system-library closure not exhaustively enumerated' 

    OUT.mkdir(parents=True, exist_ok=True)
    elf = OUT / "driver.elf"
    command = [
        cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
        "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(includes),
        "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL",
        str(SRC / "driver.c"), "-lgcc", "-o", str(elf)
    ]
    provenance['command']=command
    subprocess.run(command,check=True)
    normalize(elf)

    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    provenance['readelf']={'path':readelf,'sha256':file_sha256(readelf)}
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exports = {
        f[7] for line in symbols.splitlines()
        if len((f := line.split())) >= 8 and f[4] == "GLOBAL" and f[6] != "UND" and f[3] == "FUNC"
    }
    if exports != {"t5_driver_get"}:
        raise SystemExit(f"unexpected exports: {sorted(exports)}")

    imports = {
        f[7] for line in symbols.splitlines()
        if len((f := line.split())) >= 8 and f[6] == "UND"
    }
    # Preserve historical builder behavior; report actual imports without inventing a new ABI allowlist.
    provenance['undefined_symbols']=sorted(imports)

    data = elf.read_bytes()
    if not 52 <= len(data) <= 256 * 1024:
        raise SystemExit("unexpected ELF size")
    if data[:7] != b"\x7fELF\x01\x01\x01":
        raise SystemExit("not ELF32 little-endian")
    if int.from_bytes(data[16:18], "little") != 3:
        raise SystemExit("expected ET_DYN")
    if int.from_bytes(data[18:20], "little") != 94:
        raise SystemExit("expected Xtensa machine")

    digest = hashlib.sha256(data).hexdigest()
    provenance.update(actual_sha256=digest,actual_size=len(data),accepted_historical_sha256=CANONICAL_SHA256,accepted_historical_size=CANONICAL_SIZE,byte_parity=(len(data)==CANONICAL_SIZE and digest==CANONICAL_SHA256))
    receipt=json.dumps(provenance,indent=2)+'\n'
    if require_byte_parity and not provenance['byte_parity']:
        print('BQ25896_BUILD_FAILURE '+json.dumps(provenance,sort_keys=True),file=sys.stderr)
        raise SystemExit('historical BQ25896 accepted-byte mismatch; lock unchanged')
    (OUT/('build-provenance-'+hashlib.sha256(receipt.encode()).hexdigest()+'.json')).write_text(receipt)
    meta = dict(manifest)
    meta.update(
        size_bytes=len(data),
        sha256=digest,
        canonical_size_bytes=CANONICAL_SIZE,
        canonical_sha256=CANONICAL_SHA256,
        byte_parity=(len(data) == CANONICAL_SIZE and digest == CANONICAL_SHA256),
    )
    (OUT / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(
        f"built board-power-t5s3-v2 v{manifest['version']} {len(data)} bytes "
        f"{digest} byte_parity={meta['byte_parity']}"
    )


if __name__=='__main__':main()
