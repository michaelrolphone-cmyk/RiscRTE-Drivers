"""Source-bound native5 package preflight, not runtime execution authority."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
from core_binding import validate_core_source, BINDING_SHA256

MAX_IMAGE = 8 * 1024 * 1024
C_SOURCES = ('esp_elf_validate.c', 'esp_privileged_imports.c',
             'esp_privileged_manifest_imports.c')
HEADERS = ('elf_types.h', 'esp_elf_data_layout.h', 'esp_elf_internal_v1.h',
           'esp_privileged_imports.h', 'esp_privileged_manifest_imports.h')
INVENTORIES = tuple('privileged_os_cpu_symbols_v%d.def' % i for i in (1, 2, 5)) + (
    'privileged_public_libc_compat_v1.def', 'privileged_public_libc_compat_v2.def')
CLOSURE = tuple('lib/elf_loader/src/' + n for n in C_SOURCES) + tuple(
    'lib/elf_loader/include/private/' + n for n in HEADERS + INVENTORIES)


def execution_environment(work):
    return {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'LANG': 'C', 'TMPDIR': str(work)}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def snapshot(path, limit=MAX_IMAGE):
    """One bounded regular-file descriptor; reject symlinks and observed drift."""
    path = Path(path)
    fd = os.open(str(path), os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            raise ValueError('bounded regular file required: ' + str(path))
        chunks = []; remaining = limit + 1
        while remaining:
            chunk = os.read(fd, min(65536, remaining))
            if not chunk: break
            chunks.append(chunk); remaining -= len(chunk)
        raw = b''.join(chunks); after = os.fstat(fd)
        fields = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
        if len(raw) != before.st_size or any(getattr(before, k) != getattr(after, k) for k in fields):
            raise ValueError('input changed while reading: ' + str(path))
        return raw
    finally:
        os.close(fd)


def json_object(raw):
    def pairs(items):
        value = {}
        for k, v in items:
            if k in value: raise ValueError('duplicate JSON key: ' + k)
            value[k] = v
        return value
    value = json.loads(raw.decode('utf-8'), object_pairs_hook=pairs)
    if not isinstance(value, dict): raise ValueError('JSON object required')
    return value


def pinned(path, expected, limit=MAX_IMAGE):
    raw = snapshot(path, limit)
    if digest(raw) != expected: raise ValueError('input pin mismatch: ' + str(path))
    return raw


def build_validator(source, compiler, compiler_sha256, work, *, host_sdk):
    """Compile the actual current C rules, not a success-receipt surrogate."""
    binding = validate_core_source(source)
    members = binding['files']
    if set(members) != set(CLOSURE): raise ValueError('Core C closure mismatch')
    source, work = Path(source), Path(work)
    work.mkdir(parents=True, exist_ok=False)
    selected = {}
    for name in CLOSURE:
        if name not in members: raise ValueError('missing admission dependency: ' + name)
        raw = pinned(source / name, members[name])
        target = work / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(raw)
        selected[name] = digest(raw)
    # These C translation units do not consume port settings. An explicit
    # empty host sdkconfig prevents ambient hardware configuration from leaking
    # into the metadata-only build; it is not target placement qualification.
    host = work / 'host'; host.mkdir()
    (host / 'sdkconfig.h').write_text('/* Host structural/import preflight only. */\n')
    wrapper = Path(__file__).resolve().with_name('package_preflight.c')
    wrapper_raw = snapshot(wrapper, 65536)
    owned_wrapper = work / 'package_preflight.c'; owned_wrapper.write_bytes(wrapper_raw)
    cc = Path(compiler).resolve(strict=True)
    pinned(cc, compiler_sha256, 512 * 1024 * 1024)
    sdk = Path(host_sdk).resolve(strict=True)
    if not sdk.is_dir(): raise ValueError('explicit observed host SDK directory required')
    binary = work / 'package-preflight'
    command = [str(cc), '-isysroot', str(sdk), '-std=c11', '-D_POSIX_C_SOURCE=200809L',
               '-DRISC_NATIVE_ESP_IDF=1', '-DRISC_CPU_SCOPE_V5_CANDIDATE=1',
               '-Wall', '-Wextra', '-Werror', '-I' + str(host),
               '-I' + str(work / 'source/lib/elf_loader/include'),
               str(owned_wrapper)] + [str(work / 'source/lib/elf_loader/src' / n)
                                      for n in C_SOURCES] + ['-o', str(binary)]
    env = execution_environment(work)
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, timeout=120)
    (work / 'build.stdout').write_text(result.stdout)
    (work / 'build.stderr').write_text(result.stderr)
    if result.returncode:
        raise ValueError('actual C preflight build failed: ' + result.stderr[-4000:])
    pinned(cc, compiler_sha256, 512 * 1024 * 1024)
    for name, value in selected.items():
        pinned(source / name, value)
        pinned(work / 'source' / name, value)
    if validate_core_source(source) != binding: raise ValueError('Core binding changed during build')
    return {'binary': binary, 'binary_sha256': digest(snapshot(binary)),
            'core_binding_sha256': BINDING_SHA256, 'core_binding': binding,
            'consumed_sources': selected,
            'wrapper_sha256': digest(wrapper_raw), 'compiler_sha256': compiler_sha256,
            'host_sdk': str(sdk),
            'candidate_definitions': ['RISC_NATIVE_ESP_IDF=1', 'RISC_CPU_SCOPE_V5_CANDIDATE=1'],
            'host_config': 'explicit empty sdkconfig; no port execution'}


def structural_preflight(validator, image, expected_image):
    """Run C bounds checks before even importing the Python ELF extractor."""
    pinned(image, expected_image)
    pinned(validator['binary'], validator['binary_sha256'])
    result = subprocess.run([str(validator['binary']), str(image), '--structure'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=30,
                            env=execution_environment(Path(validator['binary']).parent))
    pinned(image, expected_image)
    pinned(validator['binary'], validator['binary_sha256'])
    if result.returncode != 0 or result.stdout != 'RISC_STRUCTURE_V1 PASS\n':
        raise ValueError('C structural admission refused (%s): %s' % (
            result.returncode, result.stderr.strip()))


def preflight(validator, image, declaration, expected_image, expected_declaration):
    """Mandatory C structural -> actual import authorization/exact set order."""
    pinned(image, expected_image); pinned(declaration, expected_declaration, 16384)
    pinned(validator['binary'], validator['binary_sha256'])
    result = subprocess.run([str(validator['binary']), str(image), str(declaration)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, timeout=30,
                            env=execution_environment(Path(validator['binary']).parent))
    pinned(image, expected_image); pinned(declaration, expected_declaration, 16384)
    pinned(validator['binary'], validator['binary_sha256'])
    if result.returncode != 0 or result.stdout != 'RISC_NATIVE5_PREFLIGHT_V1 PASS\n':
        raise ValueError('C admission refused (%s): %s' % (result.returncode, result.stderr.strip()))
    return {'structural': True, 'candidate5_import_allowlist_compatible': True,
            'manifest_import_set_exact': True, 'target_execution': False}
