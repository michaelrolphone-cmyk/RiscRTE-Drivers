#!/usr/bin/env python3
"""Compile the shared native component with pinned SDK declarations and test retained I/O.

No SDK drivers are copied or re-declared. Only RTOS scheduling is replaced for the
host model. Target checks reuse the Runtime's configured Xtensa compile command,
one object at a time; no firmware build or device access occurs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'components/co5300/native'
INCLUDE = ROOT / 'components/co5300/include'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--idf', type=Path, default=ROOT.parent / 'esp-idf')
p.add_argument('--compile-commands', type=Path, default=ROOT.parent / 'runtime/idf/build/compile_commands.json')
p.add_argument('--runtime', type=Path, default=ROOT.parent / 'runtime',
               help='Actual Runtime checkout used by the configured compile commands')
p.add_argument('--output', type=Path)
p.add_argument('--cmake', default=os.environ.get('CMAKE', 'cmake'), help='CMake executable for staging selection checks')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--profile', choices=('all', 'default', 'badge', 'offset'), default='all',
               help='Exercise default 2.06, badge 1.75C, or independent offset geometry (default: all)')
group = p.add_mutually_exclusive_group()
group.add_argument('--host-only', action='store_true')
group.add_argument('--target-only', action='store_true')
a = p.parse_args()
provenance = json.loads((NATIVE / 'co5300-native-provenance.json').read_text())
revision = subprocess.check_output(['git', '-C', str(a.idf), 'rev-parse', 'HEAD'], text=True).strip()
if revision != provenance['idf_revision']:
    raise RuntimeError('The SDK revision differs from the audited native bridge')
runtime = a.runtime.resolve()
entry = next(e for e in json.loads(a.compile_commands.read_text())
             if Path(e['file']).resolve() == runtime / 'src/ports/esp32s3/NativeHardware.cpp')
base = entry.get('arguments') or shlex.split(entry['command'])
includes = [v for v in base if v.startswith('-I') and '/esp_libc/' not in v and '/newlib/' not in v]
if not any(Path(v[2:]).resolve().is_relative_to(a.idf.resolve()) for v in includes):
    raise RuntimeError('Compile commands do not reference the selected actual SDK')
if not any(Path(v[2:]).resolve() == runtime / 'src' for v in includes):
    raise RuntimeError('Compile commands do not reference the selected actual Runtime')
# Do not silently inherit a product source, forced header or third-party include.
allowed_roots = (runtime, a.idf.resolve(), Path(entry['directory']).resolve())
for value in base[1:]:
    if value == '-include' or value.startswith('-include='):
        raise RuntimeError('Use clean Runtime compile commands without a product force-include')
    if value.startswith('-I') and not any(Path(value[2:]).resolve().is_relative_to(root) for root in allowed_roots):
        raise RuntimeError('Unexpected external include in Runtime compile commands: ' + value)
owned = tempfile.TemporaryDirectory(prefix='co5300-idf6-') if not a.output else None
out = a.output.resolve() if a.output else Path(owned.name)
out.mkdir(parents=True, exist_ok=True)

def run(command, label, cwd=ROOT, rejection=None):
    (out / (label + '.command.json')).write_text(json.dumps(command, indent=2) + '\n')
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / (label + '.log')).write_text(result.stdout)
    if rejection is not None:
        if result.returncode == 0 or rejection not in result.stdout:
            raise RuntimeError(f'{label} did not fail closed with {rejection!r}; evidence: {out}')
        return result.stdout
    if result.returncode:
        print(result.stdout, flush=True)
        raise RuntimeError(f'{label} failed ({result.returncode}); evidence: {out}')
    return result.stdout

# All profiles exercise the same wire, brightness and retained-custody suite.
# The unrelated geometry with Y offset prevents a two-board special case.
profiles = {
    'default': ({}, 502, 12, 4),
    'badge': ({}, 466, 17, 3),
    'offset': ({'WIDTH': 320, 'HEIGHT': 240, 'OFFSET_X': 4, 'OFFSET_Y': 18,
                'RESET_LOW_MS': 5, 'RESET_RECOVERY_MS': 75}, 240, 12, 4),
}
profile_keys = ('WIDTH', 'HEIGHT', 'OFFSET_X', 'OFFSET_Y', 'RESET_LOW_MS', 'RESET_RECOVERY_MS',
                'PIXEL_WRITE_ADDRESS', 'RAMWR_BEFORE_PIXELS', 'INIT_COMMANDS',
                'SAFE_TOP', 'SAFE_RIGHT', 'SAFE_BOTTOM', 'SAFE_LEFT')


def target_command(target, defines, source=NATIVE / 'Co5300Display.cpp', include=INCLUDE):
    # Runtime ABI headers take precedence over the facade's copies; only the
    # build profile is supplied by this component. Staging follows that order.
    command = [*base, '-I' + str(include)]
    command[command.index('-c') + 1] = str(source)
    command[command.index('-o') + 1] = str(target)
    for option, value in (('-MF', str(target) + '.d'), ('-MT', str(target))):
        if option in command:
            command[command.index(option) + 1] = value
    return command + defines


def host_command(binary, defines, source=ROOT / 'tests/co5300_native/test.cpp', include=INCLUDE):
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', '-Wno-missing-field-initializers',
               '-DRISC_NATIVE_ESP_IDF=1', '-DRISC_CONSOLE_USB_SERIAL_JTAG=1',
               '-I' + str(ROOT / 'tests/co5300_native/rtos_shim'),
               *includes, '-I' + str(include), *defines]
    if a.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    return command + [str(source), '-o', str(binary)]


# Exercise the production constexpr guard, using actual SDK declarations rather
# than a copied validator or a substitute SPI/GPIO ABI.
invalid_programs = {
    'oversized-command': 'X(0x51,1,0,0,0,0,0) X(0x36,5,0,0,0,0,0) X(0x29,0,0,0,0,0,0)',
    'nonzero-brightness': 'X(0x51,1,255,0,0,0,0) X(0x29,0,0,0,0,0,0)',
    'display-on-before-dark': 'X(0x29,0,0,0,0,0,0) X(0x51,1,0,0,0,0,0)',
    'missing-display-on': 'X(0x51,1,0,0,0,0,0)',
}
for name, program in invalid_programs.items():
    header = out / ('invalid-init-' + name + '.h')
    header.write_text('#define RISC_CO5300_INIT_COMMANDS(X) ' + program + '\n')
    defines = ['-URISC_CO5300_' + key for key in profile_keys] + ['-include', str(header)]
    if a.target_only:
        command = target_command(out / ('invalid-init-' + name + '.o'), defines)
    else:
        command = host_command(out / ('invalid-init-' + name), defines + ['-fsyntax-only'])
    run(command, 'reject-init-' + name, entry['directory'], rejection='CO5300 init must keep brightness zero')
print('Production init guard: oversized commands, lit startup and incomplete programs rejected PASS', flush=True)
for profile, (values, height, init_count, window_packets) in profiles.items():
    if a.profile not in ('all', profile):
        continue
    profile_defines = ['-URISC_CO5300_' + key for key in profile_keys]
    profile_defines += [f'-DRISC_CO5300_{key}={value}' for key, value in values.items()]
    if profile == 'badge':
        profile_defines += ['-include', str(ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h'),
                            '-DRISC_CO5300_TEST_INIT_175C=1']
    windows = height // 2
    boot_packets = init_count + windows * window_packets
    for trace in (0, 1):
        for probe in (0, 1):
            name = ('' if profile == 'default' else profile + '-') + f'trace{trace}-probe{probe}'
            defines = profile_defines + ['-URISC_PERFORMANCE_TRACE', f'-DRISC_PERFORMANCE_TRACE={trace}', '-URISC_VENDOR_DISPLAY_PROBE']
            if probe:
                defines += ['-DRISC_VENDOR_DISPLAY_PROBE=1']
            if not a.target_only:
                binary = out / ('host-' + name)
                command = host_command(binary, defines)
                run(command, 'host-compile-' + name, entry['directory'])
                output = run([str(binary)], 'host-run-' + name)
                for evidence in (
                    'PANEL_BRIGHTNESS value=102 state=deferred frame=0 visibility=unverified',
                    'PANEL_FRAME completed=1 brightness=0 visibility=unverified',
                    'PANEL_BRIGHTNESS value=102 state=applied frame=1 visibility=unverified',
                    'PANEL_BRIGHTNESS value=163 state=applied frame=1 visibility=unverified',
                    'PANEL_BRIGHTNESS value=26 state=applied frame=1 visibility=unverified',
                    'Native brightness lifecycle: first frame at zero, selected restore, retained return and fresh reopen PASS',
                ):
                    prefix = '' if evidence.startswith('Native brightness lifecycle:') else 'DIAGNOSTIC_SINK '
                    assert prefix + evidence in output, 'Missing routed native brightness evidence: ' + evidence
                print(output.strip().splitlines()[-1], flush=True)
                assert (b'RISCRTE_VENDOR_DISPLAY_TIMING_V1' in binary.read_bytes()) == bool(trace)
                if probe:
                    for position in (boot_packets + 1, boot_packets + 2, boot_packets + 3 + window_packets,
                                     boot_packets + 4 + windows * window_packets * 3,
                                     boot_packets + 4 + windows * window_packets * 4):
                        run([str(binary), str(position)], f'host-probe-timeout-{position}-' + name)
                    print('Explicit probe colors, delays and five retained-timeout boundaries: PASS', flush=True)
            if not a.host_only:
                target = out / ('target-' + name + '.o')
                command = target_command(target, defines)
                run(command, 'target-compile-' + name, entry['directory'])
                assert (b'RISCRTE_VENDOR_DISPLAY_TIMING_V1' in target.read_bytes()) == bool(trace)
                nm = str(Path(base[0]).with_name(Path(base[0]).name.replace('g++', 'nm')))
                symbols = run([nm, '-u', str(target)], 'target-symbols-' + name)
                defined = run([nm, '--defined-only', str(target)], 'target-defined-symbols-' + name)
                assert 'risc_co5300_native_display_anchor' in defined
                assert '_ZN7RiscCpu22selectedDisplayBackendEv' in defined
                assert 'spi_device_polling_start' in symbols and 'spi_device_polling_end' in symbols
                assert 'Arduino' not in symbols and 'ESP32QSPI' not in symbols and 'Serial' not in symbols
                assert '_ZN15RiscDiagnostics4lineEPKc' in symbols, 'Native evidence must use the owned diagnostic sink'
                if not probe:
                    assert 'printf' not in {line.split()[-1] for line in symbols.splitlines()}, 'Disabled IDF stdout cannot carry native evidence'
                print('Actual pinned Xtensa SDK target ' + name + ': PASS', flush=True)

# Staging is always tested and requires only this repository and the actual SDK.
with tempfile.TemporaryDirectory(prefix='co5300-staging-') as directory:
    staging = Path(directory)
    offset_header = staging / 'arbitrary geometry.h'
    offset_header.write_text(''.join(f'#define RISC_CO5300_{key} {value}\n'
                                   for key, value in profiles['offset'][0].items()) +
                             '#include "RiscCo5300Profile.h"\n')
    selected_profiles = {
        'default': None,
        'badge': ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h',
        'offset': offset_header,
    }
    stage_tool = ROOT / 'scripts/stage_co5300_native.py'
    for profile, header in selected_profiles.items():
        staged_root = staging / ('runtime ' + profile)
        command = ['python3', str(stage_tool), '--idf', str(a.idf), '--runtime', str(staged_root)]
        if header:
            command += ['--profile', str(header)]
        run(command, 'staging-' + profile)
        staged = staged_root / 'native/co5300'
        assert not (staged_root / 'src').exists() and not (staged_root / 'lib').exists()
        proof = json.loads((staged / 'staging.json').read_text())
        assert proof['idf_revision'] == provenance['idf_revision']
        expected_files = {'Co5300Display.cpp', 'Co5300DisplayIdf6.inc', 'RiscCo5300Profile.h', 'component.cmake',
                          'co5300-native-provenance.json', 'PROVENANCE.md', 'THIRD_PARTY_NOTICES.md', 'source-map.json',
                          'licenses/Apache-2.0.txt', 'licenses/Arduino_GFX-BSD.txt'}
        if header:
            expected_files.add('SelectedProfile.h')
            assert (staged / 'SelectedProfile.h').read_bytes() == header.read_bytes()
        assert proof['sha256'].keys() == expected_files
        for name, digest in proof['sha256'].items():
            assert hashlib.sha256((staged / name).read_bytes()).hexdigest() == digest
        for name in ('PROVENANCE.md', 'THIRD_PARTY_NOTICES.md', 'source-map.json', 'licenses/Apache-2.0.txt', 'licenses/Arduino_GFX-BSD.txt'):
            assert (staged / name).read_bytes() == (NATIVE.parent / name).read_bytes()
        for name in ('Co5300Display.cpp', 'Co5300DisplayIdf6.inc', 'co5300-native-provenance.json'):
            assert (staged / name).read_bytes() == (NATIVE / name).read_bytes()
        assert (staged / 'RiscCo5300Profile.h').read_bytes() == (INCLUDE / 'RiscCo5300Profile.h').read_bytes()
        # Evaluate the actual generated CMake fragment twice: its paths must be
        # self-contained, preserve spaces and append exactly one source/anchor.
        variables = staging / (profile + '-cmake.txt')
        inspection = staging / (profile + '-inspect.cmake')
        inspection.write_text(f'include("{staged}/component.cmake")\n' * 2 +
                              f'file(WRITE "{variables}" "${{RISC_NATIVE_EXTRA_SOURCES}}\\n'
                              '${RISC_NATIVE_INCLUDE_DIRECTORIES}\\n${RISC_NATIVE_COMPILE_OPTIONS}\\n'
                              '${RISC_NATIVE_LINK_OPTIONS}\\n")\n')
        run([a.cmake, '-P', str(inspection)], 'staging-cmake-' + profile)
        source, include, options, link = variables.read_text().splitlines()
        assert source == str(staged / 'Co5300Display.cpp')
        assert include == str(staged)
        assert options == ('SHELL:-include "' + str(staged / 'SelectedProfile.h') + '"' if header else '')
        assert link == '-Wl,-u,risc_co5300_native_display_anchor'
        # CMake deduplicates ordinary options. Preserve the profile's -include
        # group when Runtime also adds its independent product settings header.
        project = staging / (profile + '-cmake-project')
        project.mkdir()
        settings = project / 'other settings.h'
        settings.write_text('#define CO5300_STAGING_SECOND_HEADER 73\n')
        width = 466 if profile == 'badge' else profiles[profile][0].get('WIDTH', 410)
        (project / 'selection.cpp').write_text(
            '#include "RiscCo5300Profile.h"\n'
            f'static_assert(RISC_CO5300_WIDTH == {width}, "selected profile");\n'
            'static_assert(CO5300_STAGING_SECOND_HEADER == 73, "second force-include");\n')
        (project / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(co5300_selection LANGUAGES CXX)\n'
            f'include("{staged}/component.cmake")\n'
            'add_library(selection OBJECT selection.cpp)\n'
            'target_include_directories(selection PRIVATE ${RISC_NATIVE_INCLUDE_DIRECTORIES})\n'
            'target_compile_options(selection PRIVATE ${RISC_NATIVE_COMPILE_OPTIONS}\n'
            f'  "SHELL:-include \\\"{settings}\\\"")\n')
        run([a.cmake, '-S', str(project), '-B', str(project / 'build'),
             '-DCMAKE_CXX_COMPILER=' + os.environ.get('CXX', 'c++')], 'staging-cmake-configure-' + profile)
        run([a.cmake, '--build', str(project / 'build'), '-j1'], 'staging-cmake-build-' + profile)
        defines = ['-URISC_CO5300_' + key for key in profile_keys]
        defines += ['-URISC_PERFORMANCE_TRACE', '-DRISC_PERFORMANCE_TRACE=0', '-URISC_VENDOR_DISPLAY_PROBE']
        if options:
            defines += shlex.split(options.removeprefix('SHELL:'))
        if profile == 'badge':
            defines += ['-DRISC_CO5300_TEST_INIT_175C=1']
        if not a.target_only:
            fixture = staging / (profile + '-staged-test.cpp')
            fixture.write_text((ROOT / 'tests/co5300_native/test.cpp').read_text().replace(
                '#include "../../components/co5300/native/Co5300Display.cpp"', f'#include "{source}"'))
            binary = out / ('host-staged-' + profile)
            run(host_command(binary, defines, fixture, Path(include)), 'staging-host-compile-' + profile, entry['directory'])
            run([str(binary)], 'staging-host-run-' + profile)
        if not a.host_only:
            target = out / ('target-staged-' + profile + '.o')
            run(target_command(target, defines, Path(source), Path(include)), 'staging-target-compile-' + profile, entry['directory'])
        before = {p.relative_to(staged): p.read_bytes() for p in staged.rglob('*') if p.is_file()}
        run(command, 'staging-repeat-' + profile, rejection='destination already exists')
        assert {p.relative_to(staged): p.read_bytes() for p in staged.rglob('*') if p.is_file()} == before
    for case, reason in (
            ('wrong-sdk', 'qualified ESP-IDF revision'),
            ('mixed-arduino', 'mixed Arduino_GFX Runtime'),
            ('competing-legacy', 'competing display backend'),
            ('competing-native', 'competing display backend'),
            ('missing-profile', 'No such file or directory')):
        bad = staging / case
        sdk = ROOT if case == 'wrong-sdk' else a.idf
        if case == 'mixed-arduino':
            (bad / 'lib/WaveshareVendorGfx').mkdir(parents=True)
        if case.startswith('competing-'):
            (bad / 'src').mkdir(parents=True)
            old = 'VendorDisplay.cpp' if case == 'competing-legacy' else 'Co5300Display.cpp'
            (bad / 'src' / old).write_text('// Existing backend must not be overwritten.\n')
        command = ['python3', str(stage_tool), '--idf', str(sdk), '--runtime', str(bad)]
        if case == 'missing-profile':
            command += ['--profile', str(staging / 'missing.h')]
        before = {p.relative_to(bad): p.read_bytes() for p in bad.rglob('*') if p.is_file()}
        run(command, 'staging-reject-' + case, rejection=reason)
        assert not (bad / 'native').exists()
        assert {p.relative_to(bad): p.read_bytes() for p in bad.rglob('*') if p.is_file()} == before
    print('Self-contained default/Badge/arbitrary-profile staging, CMake selection and fail-closed rejection: PASS', flush=True)

print('Native CO5300 contract checks passed. Evidence: ' + str(out), flush=True)
