#!/usr/bin/env python3
"""Build/link the pinned Badge native Runtime. Never flash or access a device.

The boot store and its independent loadable providers are composed separately.
This uses Runtime's existing product profile and native CPU port, not fake
providers, a copied Runtime, or native GPIO/PMIC startup choreography.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import stage_spiffs_lookup

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/native'
RUNTIME_REVISION = '13eca114cb58469cf6004fafc3da84fbd9d29beb'
IDF_REVISION = 'fff9895c82d744c7237be8847347bdd1b07c6643'
TOOLCHAIN = 'esp-15.2.0_20251204'


def output(command, **kwargs):
    return subprocess.check_output([str(x) for x in command], text=True, **kwargs).strip()


def pinned_checkout(path, revision, label):
    actual = output(['git', '-C', path, 'rev-parse', 'HEAD'])
    if actual != revision:
        raise RuntimeError(f'{label} revision must be {revision}, got {actual}')
    if output(['git', '-C', path, 'status', '--porcelain']):
        raise RuntimeError(f'{label} checkout must be clean')
    return actual


def run(command, log, env):
    with log.open('w') as stream:
        stream.write(' '.join(str(x) for x in command) + '\n')
        stream.flush()
        result = subprocess.run([str(x) for x in command], env=env,
                                stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text()[-16000:], file=sys.stderr)
        raise RuntimeError(f'Command failed ({result.returncode}); full log: {log}')


def local_sources():
    paths = sorted([p for directory in (BOARD,ROOT/'components/co5300/native',ROOT/'components/co5300/include')
                    for p in directory.rglob('*') if p.is_file()] +
                   [ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h',Path(__file__).resolve(),ROOT/'scripts/stage_spiffs_lookup.py',ROOT/'test/test_badge_native.py'])
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def verify_performance_selection(rows,runtime,performance_trace):
    runtime_rows=[r for r in rows if str(Path(runtime)/'src')+'/' in r['file']]
    if not runtime_rows:raise RuntimeError('Missing Runtime compilation evidence')
    import shlex
    for row in runtime_rows:
        args=row.get('arguments') or shlex.split(row['command'])
        selected=[x for x in args if x.startswith(('-DRISC_PERFORMANCE_TRACE=','-URISC_PERFORMANCE_TRACE'))]
        if performance_trace and selected!=['-DRISC_PERFORMANCE_TRACE=1']:raise RuntimeError('Inconsistent bounded recorder selection')
        if not performance_trace and selected not in ([],['-DRISC_PERFORMANCE_TRACE=0']):raise RuntimeError('Unexpected recorder selection')


def verify_time_sample_selection(rows,runtime,enabled):
    import shlex
    selected=[r for r in rows if str(Path(runtime)/'src')+'/' in r['file']]
    if not selected:raise RuntimeError('Missing Runtime compilation evidence')
    for row in selected:
        args=row.get('arguments') or shlex.split(row['command'])
        definitions=[x for x in args if x.startswith(('-DRISC_ENABLE_TIME_SAMPLE','-URISC_ENABLE_TIME_SAMPLE'))]
        if enabled and definitions!=['-DRISC_ENABLE_TIME_SAMPLE=1']:
            raise RuntimeError('Inconsistent native time sampler selection')
        if not enabled and definitions not in ([],['-DRISC_ENABLE_TIME_SAMPLE=0']):
            raise RuntimeError('Unexpected native time sampler selection')


def verify_stripe_reuse_selection(rows,enabled):
    import shlex
    native=[r for r in rows if r['file'].endswith('/components/co5300/native/Co5300Display.cpp')]
    if len(native)!=1:raise RuntimeError('Missing or duplicate native CO5300 compilation evidence')
    args=native[0].get('arguments') or shlex.split(native[0]['command'])
    definitions=[x for x in args if x.startswith(('-DRISC_CO5300_REUSE_UNCHANGED=','-URISC_CO5300_REUSE_UNCHANGED'))]
    expected=['-DRISC_CO5300_REUSE_UNCHANGED='+str(int(enabled))]
    if definitions!=expected:raise RuntimeError('Inconsistent exact stripe reuse selection')
    return {'enabled':enabled,'selection':'verified exact native compilation definition',
            'optional_shadow_bytes':434312 if enabled else 0,
            'allocation':'one lazy PSRAM allocation; OOM uses ordinary transfers' if enabled else 'none'}


def verify_column_reuse_selection(rows,enabled):
    import shlex
    native=[r for r in rows if r['file'].endswith('/components/co5300/native/Co5300Display.cpp')]
    if len(native)!=1:raise RuntimeError('Missing or duplicate native CO5300 compilation evidence')
    args=native[0]['arguments'] if 'arguments' in native[0] else shlex.split(native[0].get('command',''))
    definitions=[x for x in args if x.startswith(('-DRISC_CO5300_REUSE_COLUMNS=','-URISC_CO5300_REUSE_COLUMNS'))]
    if definitions!=['-DRISC_CO5300_REUSE_COLUMNS='+str(int(enabled))]:raise RuntimeError('Inconsistent exact CASET reuse selection')
    return {'enabled':enabled,'selection':'verified exact native compilation definition',
            'authority':'successful CASET completion and CS release; reset/error/unknown command/open/close invalidate',
            'allocation':'none','physical_qualified':False}


def verify_queued_selection(rows,enabled,stripe_rows):
    import shlex
    native=[r for r in rows if r['file'].endswith('/components/co5300/native/Co5300Display.cpp')]
    if len(native)!=1:raise RuntimeError('Missing or duplicate native CO5300 compilation evidence')
    args=native[0].get('arguments') or shlex.split(native[0]['command'])
    for name,value in [('RISC_CO5300_QUEUED_PIXELS',int(enabled)),('RISC_CO5300_STRIPE_ROWS',stripe_rows)]:
        selected=[x for x in args if x.startswith(('-D'+name+'=','-U'+name))]
        if selected!=['-D'+name+'='+str(value)]:raise RuntimeError('Inconsistent native '+name)
    return {'enabled':enabled,'stripe_rows':stripe_rows,'composition':'complete sequential frames only',
            'internal_dma_bytes':466*stripe_rows*2*(2 if enabled else 1),
            'fallback':'double allocation failure retries configured single, then smaller even synchronous windows down to2rows' if enabled else None,
            'fallback_min_internal_dma_bytes':1864 if enabled else None,
            'effective_profile':'PANEL_TRANSPORT reports actual open-time stripe_rows/queued; build values are maxima',
            'completion':'last row drains final queued pixel before frame completion'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--quick-actions', action='store_true', help='Explicit native preference broker; requires prototype')
    p.add_argument('--prototype', action='store_true', help='Explicit richer prototype profile and exact source lock')
    p.add_argument('--reuse-columns', action='store_true', help='Optional validated CASET register reuse; requires prototype')
    p.add_argument('--queued-pixels', action='store_true', help='Optional double-buffer queued pixels; complete-frame prototype only')
    p.add_argument('--native-stripe-rows',type=int,choices=(4,8,16,32),default=4)
    p.add_argument('--reuse-unchanged-stripes', action='store_true', help='Optional exact completed-panel shadow; requires prototype')
    p.add_argument('--time-server-sync', action='store_true', help='Read-only native network time sampler; application owns alarm-safe UTC commit')
    p.add_argument('--performance-trace', action='store_true', help='Bounded native RAM recorder, replay only on explicit perf command')
    p.add_argument('--spiffs-source', type=Path, help='Exact separately pinned SPIFFS lookup repair; requires prototype')
    p.add_argument('--runtime', required=True, type=Path)
    p.add_argument('--idf', required=True, type=Path)
    p.add_argument('--cc', required=True, type=Path)
    p.add_argument('--python', default=sys.executable, type=Path,
                   help='Python in an installed ESP-IDF 6.1 environment')
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    if a.quick_actions and not a.prototype:p.error('quick-actions requires prototype')
    if (a.queued_pixels or a.native_stripe_rows!=4) and not a.prototype:p.error('Queued/larger stripes require complete-frame prototype')
    if a.reuse_columns and not a.prototype:p.error('Column reuse requires explicit prototype composition')
    if a.reuse_unchanged_stripes and not a.prototype:p.error('Stripe reuse requires prototype')
    if a.time_server_sync and not a.prototype:p.error('Time sampler requires prototype composition')
    if a.performance_trace and not a.prototype:p.error('Performance trace requires prototype')
    if a.spiffs_source and not a.prototype:p.error('SPIFFS selection requires prototype')
    global BOARD, RUNTIME_REVISION
    if a.prototype:
        BOARD = BOARD.parent / 'prototype/native'
        RUNTIME_REVISION = json.loads((BOARD.parent/'sources.lock.json').read_text())['runtime']['commit']
    runtime, idf, cc, python, out = [getattr(a, name).absolute()
                                    for name in ('runtime', 'idf', 'cc', 'python', 'output')]
    out.mkdir(parents=True, exist_ok=True)
    # Clear stale success even when pin/compiler preflight fails.
    (out / 'build-proof.json').unlink(missing_ok=True)
    # Preserve the ESP32-S3 frontend symlink: resolving it changes GCC target.
    pinned_checkout(runtime, RUNTIME_REVISION, 'Runtime')
    pinned_checkout(idf, IDF_REVISION, 'ESP-IDF')
    version = output([cc, '--version'])
    if output([cc, '-dumpfullversion']) != '15.2.0' or TOOLCHAIN not in version:
        raise RuntimeError('Badge native build requires the exact GCC15 SDK cohort')
    macros = output([cc, '-dM', '-E', '-x', 'c', '-'], input='')
    if '#define __XTENSA_EL__ 1' not in macros or '#define __XTENSA_WINDOWED_ABI__ 1' not in macros:
        raise RuntimeError('Compiler must select the little-endian ESP32-S3 windowed ABI')
    env = dict(os.environ, IDF_PATH=str(idf), IDF_TARGET='esp32s3',
               IDF_PYTHON_ENV_PATH=str(python.parent.parent))
    env['PATH'] = os.pathsep.join((str(cc.parent), str(python.parent), env.get('PATH', '')))
    cmake = shutil.which('cmake', path=env['PATH'])
    ninja = shutil.which('ninja', path=env['PATH'])
    if not cmake or not ninja:
        raise RuntimeError('Install/export ESP-IDF CMake and Ninja before building')
    before = local_sources()
    build = out / 'build'
    profile = BOARD / 'product-profile.cmake'
    lookup = None
    if a.spiffs_source:
        lock = json.loads((BOARD.parent/'sources.lock.json').read_text())
        lookup = stage_spiffs_lookup.stage(idf, a.spiffs_source.absolute(), lock['spiffs_lookup']['commit'], out/'components/spiffs')
        (out/'spiffs-lookup-selection.json').write_text(json.dumps(lookup,indent=2)+'\n')
    run([cmake, '-S', runtime / 'idf', '-B', build, '-G', 'Ninja',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(idf / 'tools/cmake/toolchain-esp32s3.cmake'),
         '-DPYTHON=' + str(python), '-DRISC_PRODUCT_PROFILE=' + str(profile),
         '-DRISC_SOURCE_SHA=' + RUNTIME_REVISION,
         '-DRISC_BADGE_QUICK_ACTIONS=' + ('ON' if a.quick_actions else 'OFF'),
         '-DRISC_BADGE_TIME_SAMPLE=' + ('ON' if a.time_server_sync else 'OFF'),
         '-DRISC_BADGE_PERFORMANCE_TRACE=' + ('ON' if a.performance_trace else 'OFF'),
         '-DRISC_BADGE_REUSE_COLUMNS=' + ('ON' if a.reuse_columns else 'OFF'),
         '-DRISC_BADGE_QUEUED_PIXELS=' + ('ON' if a.queued_pixels else 'OFF'),
         '-DRISC_BADGE_STRIPE_ROWS='+str(a.native_stripe_rows),
         '-DRISC_BADGE_REUSE_UNCHANGED_STRIPES=' + ('ON' if a.reuse_unchanged_stripes else 'OFF'),
         *(['-DRISC_BADGE_SPIFFS_COMPONENT='+lookup['component_path']] if lookup else [])], out / 'configure.log', env)
    run([cmake, '--build', build, '--', '-j1'], out / 'build.log', env)
    if before != local_sources():raise RuntimeError('Native source bytes changed during build')
    pinned_checkout(runtime,RUNTIME_REVISION,'Runtime')
    pinned_checkout(idf,IDF_REVISION,'ESP-IDF')
    qualify(runtime, idf, cc, python, out, env, version, before, a.quick_actions, lookup, a.performance_trace, a.reuse_unchanged_stripes, a.queued_pixels, a.native_stripe_rows, a.reuse_columns, a.time_server_sync)


def qualify(runtime, idf, cc, python, out, env, version, before, quick_actions=False, lookup=None, performance_trace=False, reuse_unchanged_stripes=False, queued_pixels=False, stripe_rows=4, reuse_columns=False, time_server_sync=False):
    """Audit a completed link without rebuilding or changing its image bytes."""
    build = out / 'build'
    run([python, runtime / 'test/native_firmware_idf6_test.py', '--build', build,
         '--cxxfilt', cc.with_name('xtensa-esp-elf-c++filt')], out / 'runtime-proof.log', env)
    run([python, ROOT / 'test/test_badge_native.py', '--build', build,
         '--runtime', runtime, *(['--prototype'] if BOARD.parent.name=='prototype' else []),*(['--quick-actions'] if quick_actions else []),
         *(['--reuse-columns'] if reuse_columns else []), *(['--queued-pixels'] if queued_pixels else []), '--native-stripe-rows', str(stripe_rows)], out / 'badge-proof.log', env)
    if before != local_sources():raise RuntimeError('Native source bytes changed during qualification')
    pinned_checkout(runtime,RUNTIME_REVISION,'Runtime')
    pinned_checkout(idf,IDF_REVISION,'ESP-IDF')
    if lookup:
        lookup = dict(lookup, compiled_units=stage_spiffs_lookup.verify_compiled(build, lookup))
    rows=json.loads((build/'compile_commands.json').read_text())
    verify_performance_selection(rows,runtime,performance_trace)
    verify_time_sample_selection(rows,runtime,time_server_sync)
    reuse=verify_stripe_reuse_selection(rows,reuse_unchanged_stripes) if BOARD.parent.name=='prototype' else {'enabled':False,'optional_shadow_bytes':0}
    columns=verify_column_reuse_selection(rows,reuse_columns) if BOARD.parent.name=='prototype' else {'enabled':False}
    queued=verify_queued_selection(rows,queued_pixels,stripe_rows) if BOARD.parent.name=='prototype' else {'enabled':False}
    report = {
        'runtime_revision': RUNTIME_REVISION, 'idf_revision': IDF_REVISION,
        'quick_actions': quick_actions, 'spiffs_lookup': lookup,
        'time_sample': {'enabled':time_server_sync,'capability':'platform.time-sample@1','read_only':True,'authentication':'unauthenticated SNTP','application_clock_commit':'separate alarm-safe transaction'},
        'performance_trace': {'enabled':performance_trace,'storage':'bounded RAM','capacity':128,'replay':'explicit perf newline command only','app_spans':'requires separately selected app instrumentation'},
        'preference_store': {'enabled':quick_actions,'atomic_key_value':False,'backend':'existing namespace KV copied synchronous operations' if quick_actions else None},
        'compiler': version, 'jobs': 1, 'hardware_qualified': False,
        'boot_store_included': False, 'full_link': True,
        'sources_sha256': before,
        'artifacts': {str(p.relative_to(build)): {
            'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in (build / 'riscrte_native.elf', build / 'riscrte_native.bin',
                      build / 'bootloader/bootloader.bin', build / 'partition_table/partition-table.bin')},
        'co5300_transport': {'column_reuse':columns, 'unchanged_reuse':reuse, 'queued_pixels':queued, 'stripe_rows':stripe_rows if BOARD.parent.name=='prototype' else 2, 'selection':'verified native compile definitions; prototype complete-frame facade only'},
        'limits': 'Native image only; compose and admit the boot store separately. No flash, device access, physical qualification or release.'
    }
    (out / 'build-proof.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Pinned Badge native image linked and verified; no hardware qualification.')
    print(out / 'build-proof.json')


if __name__ == '__main__':
    main()
