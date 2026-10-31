#!/usr/bin/env python3
"""One clock builder, separate historical replay and current-source qualification."""
import argparse
import hashlib
import json
import subprocess
import tempfile
import shutil
import sys
from pathlib import Path
from normalize_xtensa_relocations import normalize
from native_driver_toolchain import compiler, native_support_sources, is_unified

ROOT = Path(__file__).resolve().parents[1]
HISTORICAL_COMMIT = 'f208bf2e73d2e6c885269c9fc5260dc5f1f344d2'
HISTORICAL_BUILD_COMMIT = 'a1982161c4e584b1fa047030fde60836980694fb'
HISTORICAL_HEADERS = {
    'RiscPlatformClockV1.h': '41a12f6529f38731ee874f8bc166e1cf89fe376a1448fe5a4dd4c9abac90e5e2',
    'RiscProviderV2.h': '5fe8758d95e01c89ad1e4aab282be0da40e35c4537fd98c364394daf34ff28b7',
}
HISTORICAL_NORMALIZER = 'ea36ed078221b7f29b3d6f84dac2768e249942c7325a232a6115596f507f1456'
HISTORICAL_TREE = '594a3affe76fb840c81edc7fa9f09eca645a894f'
ACCEPTED_SHA256 = 'a5231d3f21931261b10ae82a3e16248d60a678aa6ebc67b61cb702491d6a0ece'
ACCEPTED_SIZE = 29544


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def source_inputs(root, current, temporary):
    if current:
        source = root / 'Drivers/platform_clock_v1'
        return source, {'mode': 'current-source', 'expected_version': '0.1.1'}
    source = temporary / 'platform_clock_v1'
    source.mkdir()
    # Immutable Git tree, never the possibly migrated working directory.
    for name in ('driver.c', 'manifest.json'):
        data = subprocess.check_output(['git', '-C', str(root), 'show', HISTORICAL_TREE + ':' + name])
        (source / name).write_bytes(data)
    return source, {'mode': 'historical', 'expected_version': '0.1.0',
                    'source_commit': HISTORICAL_COMMIT, 'source_tree': HISTORICAL_TREE}


def output_path(root, current, requested):
    historical = (root / 'dist/platform-clock-v1').resolve()
    output = Path(requested).resolve() if requested else (root / ('dist/platform-clock-v1-current' if current else 'dist/platform-clock-v1')).resolve()
    if current and (output == historical or historical in output.parents):
        raise ValueError('current-source output must not overwrite historical output')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-source', action='store_true', help='Qualify working 0.1.1 source, never historical output')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--require-byte-parity', action='store_true', help='Require exact historical 0.1.0 accepted ELF (historical mode only)')
    args = parser.parse_args()
    if args.current_source and args.require_byte_parity:
        parser.error('current source is not historical accepted-byte replay')
    try:
        output = output_path(ROOT, args.current_source, args.output)
    except ValueError as exc:
        parser.error(str(exc))
    with tempfile.TemporaryDirectory(prefix='clock-source-') as temporary:
        source, provenance = source_inputs(ROOT, args.current_source, Path(temporary))
        build(source, output, provenance, args.require_byte_parity)


def build(SRC, OUT, provenance, require_byte_parity=False):
    # Build and check away from retained outputs. Never replace a different file.
    with tempfile.TemporaryDirectory(prefix='clock-build-') as tmp:
        staged = Path(tmp)
        build_staged(SRC, staged, provenance, require_byte_parity)
        publish_outputs(staged, OUT)


def publish_outputs(staged, output):
    if output.is_symlink():
        raise ValueError('refusing symlink output directory')
    for path in staged.iterdir():
        existing = output / path.name
        if existing.is_symlink() or (existing.exists() and (not existing.is_file() or existing.read_bytes() != path.read_bytes())):
            raise ValueError('refusing to replace retained output: ' + str(existing))
    output.mkdir(parents=True, exist_ok=True)
    for path in staged.iterdir():
        shutil.copyfile(path, output / path.name)


def build_staged(SRC, OUT, provenance, require_byte_parity=False):
    include_root = ROOT / 'sdk/driver'
    if require_byte_parity:
        include_root = OUT / 'historical-sdk'
        include_root.mkdir()
        for name, expected in HISTORICAL_HEADERS.items():
            data = subprocess.check_output(['git', '-C', str(ROOT), 'show', HISTORICAL_BUILD_COMMIT + ':sdk/driver/' + name])
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError('historical SDK input mismatch: ' + name)
            (include_root / name).write_bytes(data)
        if file_sha256(ROOT / 'scripts/normalize_xtensa_relocations.py') != HISTORICAL_NORMALIZER:
            raise ValueError('historical normalizer changed; do not rewrite accepted lock')
    manifest=json.loads((SRC/"manifest.json").read_text())
    required={
        "type":"driver","id":"platform-clock-v1","version":provenance["expected_version"],
        "driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf",
        "requires":[],"provides":[{"capability":"platform.clock","api":1}],
        "status":"experimental-unpublished",
    }
    if manifest != required:
        raise SystemExit("platform-clock-v1 manifest does not match the published source contract")

    cc = compiler()

    provenance = dict(provenance)
    provenance['source_files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(SRC.iterdir()) if p.is_file()}
    provenance['historical_build_commit'] = HISTORICAL_BUILD_COMMIT if require_byte_parity else None
    provenance['selected_sdk_inputs'] = {p.name: file_sha256(p) for p in sorted(include_root.rglob('*')) if p.is_file()}
    provenance['compiler'] = cc
    provenance['compiler_version'] = subprocess.check_output([cc, '--version'], text=True)
    provenance['compiler_sha256'] = file_sha256(cc)
    libgcc = subprocess.check_output([cc, '-print-libgcc-file-name'], text=True).strip()
    provenance['libgcc'] = {'path': libgcc, 'sha256': file_sha256(libgcc)}
    provenance['receipt_scope'] = 'run provenance; temporary paths vary, not deterministic build identity'
    provenance['toolchain_closure'] = 'compiler and libgcc pinned; implicit system libraries and compiler subprocess closure not fully enumerated'
    provenance['build_inputs'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for directory in ('sdk/driver', 'sdk/compat/elf-time32', 'scripts/xtensa_support') for p in sorted((ROOT/directory).rglob('*')) if p.is_file()}
    for name in ('build_platform_clock_v1.py', 'native_driver_toolchain.py', 'normalize_xtensa_relocations.py'):
        p = ROOT / 'scripts' / name
        provenance['build_inputs']['scripts/' + name] = hashlib.sha256(p.read_bytes()).hexdigest()
    OUT.mkdir(parents=True,exist_ok=True)
    elf=OUT/"driver.elf"
    command = [
        cc, "-std=c11","-D_DEFAULT_SOURCE","-Os","-fPIC",
        "-mtext-section-literals","-mlongcalls","-fvisibility=hidden",
        "-nostdlib","-nostartfiles","-shared",
        *(["-I"+str(ROOT/"sdk/compat/elf-time32")] if is_unified(cc) else []),
        "-I"+str(include_root),"-Wl,--hash-style=sysv","-Wl,--exclude-libs,ALL",
        str(SRC/"driver.c"),*native_support_sources(cc),"-lgcc","-o",str(elf)
    ]
    provenance['command'] = command
    subprocess.run(command,check=True)
    normalize(elf)
    if require_byte_parity:
        shutil.rmtree(include_root)

    readelf=str(Path(cc).with_name(Path(cc).name.replace("gcc","readelf")))
    provenance['readelf'] = {'path': readelf, 'sha256': file_sha256(readelf)}
    symbols=subprocess.check_output([readelf,"--dyn-syms","--wide",str(elf)],text=True)
    exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=="GLOBAL" and f[6]!="UND" and f[3]=="FUNC"}
    if exports!={"t5_driver_get"}:
        raise SystemExit(f"unexpected exports: {sorted(exports)}")

    data=elf.read_bytes()
    if len(data)<52 or data[:7]!=b"\x7fELF\x01\x01\x01" or int.from_bytes(data[16:18],"little")!=3 or int.from_bytes(data[18:20],"little")!=94:
        raise SystemExit("not an ELF32 Xtensa shared driver")
    provenance['accepted_historical_sha256'] = ACCEPTED_SHA256
    provenance['accepted_historical_size'] = ACCEPTED_SIZE
    provenance['actual_size'] = len(data)
    provenance['actual_sha256'] = hashlib.sha256(data).hexdigest()
    provenance['byte_parity'] = len(data) == ACCEPTED_SIZE and hashlib.sha256(data).hexdigest() == ACCEPTED_SHA256
    receipt = json.dumps(provenance, indent=2)+'\n'
    (OUT/('build-provenance-' + hashlib.sha256(receipt.encode()).hexdigest() + '.json')).write_text(receipt)
    if require_byte_parity and not provenance['byte_parity']:
        # Survives staging cleanup in the caller/CI log; no accepted output write.
        print('CLOCK_BUILD_FAILURE ' + json.dumps(provenance, sort_keys=True), file=sys.stderr)
        raise SystemExit('historical accepted-byte mismatch; accepted lock is unchanged')
    manifest.update(size_bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    (OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(f"built {manifest['id']} v{manifest['version']} {len(data)} bytes {manifest['sha256']}")


if __name__ == '__main__':
    main()
