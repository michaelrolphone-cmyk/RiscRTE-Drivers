#!/usr/bin/env python3
"""Build only the provider-private USB source closure for the pinned native SDK.

No PlatformIO, downloads, firmware build, release-package mutation, or publication.
Unknown imports and unsupported loader mappings fail qualification after evidence
is written. The generated ELF is an experimental native candidate, not v0.1.19.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'Drivers/usb_controller_esp32s3'
IDF_COMMIT = 'fff9895c82d744c7237be8847347bdd1b07c6643'
USB_COMMIT = '0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a'
NATIVE_COHORT = 'esp32s3-idf6.1-gcc15.2.0-20251204-v1'
COMPILER_DISTRIBUTION = 'esp-15.2.0_20251204'
REQUIRES = [{'capability':'board.power.vbus','api':1}]
PROVIDES = [{'capability':'usb.controller','api':1}]
ABI_PROFILE = {
    'freertos_cores':2, 'tick_hz':1000, 'notification_slots':1,
    'use_16_bit_ticks':0, 'use_task_notifications':1, 'use_mutexes':1,
    'dynamic_allocation':1, 'unicore':0, 'single_core_mode':0, 'smp_kernel':0,
    'base_type_size':4, 'ubase_type_size':4, 'tick_type_size':4,
    'task_handle_size':4, 'queue_handle_size':4, 'stack_type_size':1,
    'stack_alignment':16, 'port_max_delay':0xffffffff,
    'portmux_size':8, 'portmux_alignment':4, 'portmux_owner_offset':0,
    'portmux_count_offset':4, 'portmux_free_value':0xB33FFFFF,
    'log_config_size':4, 'log_config_alignment':4, 'log_config_data_offset':0,
    'log_version':1, 'log_default_level':3, 'log_maximum_level':3,
    'log_timestamp_rtos':1, 'internal_cache_level':0, 'external_cache_level':1,
    'internal_memory_cached':0,
}
ABI_EXPRESSIONS = dict(zip(ABI_PROFILE, (
    'configNUMBER_OF_CORES','configTICK_RATE_HZ','configTASK_NOTIFICATION_ARRAY_ENTRIES',
    'configUSE_16_BIT_TICKS','configUSE_TASK_NOTIFICATIONS','configUSE_MUTEXES',
    'configSUPPORT_DYNAMIC_ALLOCATION','CONFIG_FREERTOS_UNICORE',
    'CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE','CONFIG_FREERTOS_SMP',
    'sizeof(BaseType_t)','sizeof(UBaseType_t)','sizeof(TickType_t)',
    'sizeof(TaskHandle_t)','sizeof(QueueHandle_t)','sizeof(StackType_t)',
    'portBYTE_ALIGNMENT','portMAX_DELAY',
    'sizeof(portMUX_TYPE)','alignof(portMUX_TYPE)','offsetof(portMUX_TYPE,owner)',
    'offsetof(portMUX_TYPE,count)','portMUX_FREE_VAL',
    'sizeof(esp_log_config_t)','alignof(esp_log_config_t)','offsetof(esp_log_config_t,data)',
    'CONFIG_LOG_VERSION','CONFIG_LOG_DEFAULT_LEVEL','CONFIG_LOG_MAXIMUM_LEVEL',
    'CONFIG_LOG_TIMESTAMP_SOURCE_RTOS','CACHE_LL_LEVEL_INT_MEM','CACHE_LL_LEVEL_EXT_MEM',
    'SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE',
), strict=True))
HOST_SOURCES = ('hcd_dwc.c', 'enum.c', 'hub.c', 'usb_helpers.c', 'usb_host.c', 'usb_private.c', 'usbh.c')
SDK_SOURCES = ('esp_common/src/esp_err_to_name.c', 'esp_hw_support/usb_phy/usb_phy.c', 'esp_hal_usb/usb_dwc_hal.c',
               'esp_hal_usb/usb_wrap_hal.c', 'esp_hal_usb/usb_serial_jtag_hal.c',
               'esp_hal_usb/esp32s3/usb_dwc_periph.c', 'soc/esp32s3/gpio_periph.c')
USB_CONFIG = {'CONTROL_TRANSFER_MAX_SIZE':256, 'HW_BUFFER_BIAS_BALANCED':1,
              'DEBOUNCE_DELAY_MS':250, 'RESET_HOLD_MS':30, 'RESET_RECOVERY_MS':30,
              'SET_ADDR_RECOVERY_MS':10, 'RESUME_HOLD_MS':30,
              'RESUME_RECOVERY_MS':20, 'SUSPEND_ENTRY_MS':20}
FORBIDDEN = ('usb_', '_usb_', 'usbh_', 'hcd_', 'hub_', 'enum_', 'urb_', 'risc_usb_',
             'nativeUsb', 'NativeUsbBridge', 't5_usb_', 'gpio_')
MMIO = ('GPIO', 'RTCCNTL', 'SYSTEM', 'USB_DWC', 'USB_SERIAL_JTAG', 'USB_WRAP')
IMPORT_FILE = DRIVER / 'idf61-imports.txt'


def run(args, **kwargs):
    return subprocess.check_output(list(map(str,args)), text=True, **kwargs)


def pin(path, commit):
    if run(['git','-C',path,'rev-parse','HEAD']).strip() != commit:
        raise ValueError(f'unreviewed SDK/component pin: {path}')
    if run(['git','-C',path,'status','--porcelain','--untracked-files=no']).strip():
        raise ValueError(f'modified SDK/component: {path}')


def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('pinned enumeration anchor changed: ' + old)
    return source.replace(old,new,1)


def instrument(name, source):
    if name == 'hub.c':
        source=once(source, '#include "sdkconfig.h"', '#include "sdkconfig.h"\nextern void risc_usb_enum_reset(void);')
        return once(source, 'case HCD_PORT_EVENT_CONNECTION: {',
                    'case HCD_PORT_EVENT_CONNECTION: {\n        risc_usb_enum_reset();')
    if name == 'enum.c':
        source=once(source, '#include "esp_log.h"', '#include "esp_log.h"\nextern void risc_usb_enum_stage(const char *);\nextern void risc_usb_enum_error(const char *, ...);')
        source=once(source, 'enum_stage_t stage = p_enum_driver->single_thread.stage;\n\n    switch (stage)',
                    'enum_stage_t stage = p_enum_driver->single_thread.stage;\n    risc_usb_enum_stage(enum_stage_strings[stage]);\n\n    switch (stage)')
        # Optional string descriptors may fail successfully; record only fatal
        # state transitions, before cleanup advances to CANCEL/IDLE.
        anchor='ESP_LOGE(ENUM_TAG, "%s FAILED", enum_stage_strings[last_stage]);'
        return once(source,anchor,'risc_usb_enum_error("%s FAILED", enum_stage_strings[last_stage]);\n                '+anchor)
    return source


def config_header():
    text='#pragma once\n#include "sdkconfig.h"\n#include "esp_idf_version.h"\n'
    text+='#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6,1,0)\n#error "Unreviewed native USB target"\n#endif\n'
    for key,value in USB_CONFIG.items():
        key='CONFIG_USB_HOST_'+key
        text+=f'#if defined({key}) && {key} != {value}\n#error "USB config mismatch: {key}"\n#endif\n#ifndef {key}\n#define {key} {value}\n#endif\n'
    # Disabled bools must stay undefined, not 0: upstream uses both #ifdef/#if.
    for key in ('HUBS_SUPPORTED','HUB_MULTI_LEVEL','EXT_HUB_SUPPORT','HW_BUFFER_BIAS_IN',
                'HW_BUFFER_BIAS_PERIODIC_OUT','AUTO_PM_LIGHT_SLEEP','ENABLE_ENUM_FILTER_CALLBACK',
                'DWC_DMA_CAP_MEMORY_IN_PSRAM'):
        text+=f'#ifdef CONFIG_USB_HOST_{key}\n#error "Unreviewed USB feature: {key}"\n#endif\n'
    return text


def abi_header():
    text='#pragma once\n#include <cstddef>\n#include <esp_log.h>\n#include <freertos/FreeRTOS.h>\n#include <freertos/task.h>\n#include <freertos/queue.h>\n#include "NativeDmaAlignment.h"\n'
    for name in ('CONFIG_FREERTOS_UNICORE','CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE','CONFIG_FREERTOS_SMP',
                 'SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE'):
        text+=f'#ifndef {name}\n#define {name} 0\n#endif\n'
    for key,value in ABI_PROFILE.items():
        text+=f'static_assert(({ABI_EXPRESSIONS[key]}) == {value}U, "native USB ABI profile: {key}");\n'
    return text


def compile_command(entry, source, output, config, usb, idf):
    argv=entry.get('arguments') or shlex.split(entry['command'])
    # Expand the generated exact toolchain response file (keeps the PSRAM
    # hardware-atomic workaround). Do not import Runtime source/include paths.
    expanded=[]
    for arg in argv:
        expanded += shlex.split(Path(arg[1:]).read_text()) if arg.startswith('@') else [arg]
    cmd=[]; skip=False
    for arg in expanded:
        if skip: skip=False; continue
        if arg in ('-o','-MF','-MT','-MQ'): skip=True; continue
        if arg == entry['file'] or arg in ('-c','-MD','-MMD','-MP'): continue
        if arg.startswith(('-std=','-DRISC_','-DMBEDTLS','-DTF_PSA','-DESP_PSA')): continue
        if arg.startswith('-I') and '/runtime/' in arg and not arg.endswith('/build/config'): continue
        cmd.append(arg)
    cpp=source.suffix == '.cpp'
    if not cpp:
        cmd[0]=cmd[0][:-3]+'gcc'
        cmd=[a for a in cmd if a not in ('-fno-rtti','-fno-exceptions','-fuse-cxa-atexit')]
    cmd += ['-std=gnu++17' if cpp else '-std=gnu17', '-fPIC', '-fvisibility=hidden',
            '-include',str(config), '-I'+str(ROOT/'sdk/driver'), '-I'+str(DRIVER),
            '-I'+str(usb/'include'), '-I'+str(usb/'private_include'),
            '-I'+str(idf/'components/esp_mm/include'),
            '-I'+str(idf/'components/esp_mm/port/esp32s3'),
            '-ffile-prefix-map='+str(ROOT)+'=/RiscRTE-Drivers',
            '-c',str(source),'-o',str(output)]
    return cmd


def normalize_sections(obj, compiler):
    # Native firmware IRAM attributes are not a provider placement guarantee.
    # This provider deliberately requests a non-IRAM IRQ, which IDF masks while
    # caches are unavailable. Merge both code and literal input sections into
    # the existing loader-mapped .text instead of silently orphaning them.
    tool=lambda name: str(compiler)[:-3]+name
    sections=run([tool('readelf'),'-SW',obj])
    names=re.findall(r'\] (\.iram1\.[^ ]+)',sections)
    args=[]
    for name in names:
        if name.endswith('.literal'):
            dest='.literal.risc_'+name[1:-8].replace('.','_')
        else:
            dest='.text.risc_'+name[1:].replace('.','_')
        args+=['--rename-section',name+'='+dest]
    if args: subprocess.run([tool('objcopy'),*args,str(obj)],check=True)
    return names


def audit(elf, compiler):
    tool=lambda name: str(compiler)[:-3]+name
    raw=run([tool('nm'),'-u',elf])
    imports=sorted(line.split()[-1] for line in raw.splitlines() if line.split())
    syms=run([tool('readelf'),'--dyn-syms','--wide',elf])
    exports={f[7] for line in syms.splitlines() if len(f:=line.split())>=8 and f[4]=='GLOBAL' and f[6]!='UND'}
    exports -= {'__bss_start','_edata','_end'}
    if exports != {'t5_driver_get'}: raise ValueError('unexpected exports: '+str(exports))
    if 'TEXTREL' in run([tool('readelf'),'-d',elf]): raise ValueError('TEXTREL')
    return imports


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compile-commands',required=True,type=Path)
    ap.add_argument('--idf',required=True,type=Path)
    ap.add_argument('--usb',required=True,type=Path,help='Official esp-usb checkout root')
    ap.add_argument('--output',required=True,type=Path)
    a=ap.parse_args(); idf=a.idf.resolve(); usbroot=a.usb.resolve(); usb=usbroot/'host/usb'; out=a.output.resolve()
    # Never place this local candidate over an accepted/historical output.
    isolated = ROOT/'dist/usb-controller-esp32s3-native-stream'
    if out == ROOT or (ROOT/'release') == out or (ROOT/'release') in out.parents or ((ROOT/'dist') in out.parents and out != isolated and isolated not in out.parents):
        raise ValueError('native-stream candidate requires a separate output directory')
    pin(idf,IDF_COMMIT); pin(usbroot,USB_COMMIT)
    manifest=json.loads((ROOT/'candidates/usb-native-stream/usb-controller-esp32s3.json').read_text())
    if (manifest['id']!='usb-controller-esp32s3' or manifest['version']!='0.1.27' or
        manifest['driver_abi']!=2 or manifest['architecture']!='xtensa-esp32s3' or
        manifest['requires']!=REQUIRES or manifest['provides']!=PROVIDES):
        raise ValueError('unreviewed source provider identity or dependencies')
    out.mkdir(parents=True,exist_ok=True)
    compile_database=a.compile_commands.read_bytes()
    entries=json.loads(compile_database)
    matches=[e for e in entries if e['file'].endswith('/NativeFailureEvidence.cpp')]
    if len(matches)!=1: raise ValueError('need unique native Runtime compile command')
    entry=matches[0]; config=out/'native-usb-config.h'; config.write_text(config_header())
    sources=[DRIVER/'driver.cpp', DRIVER/'NativeIrqLifecycle.cpp',*(usb/'src'/n for n in HOST_SOURCES),*(idf/'components'/n for n in SDK_SOURCES)]
    inputs=[ROOT/'candidates/usb-native-stream/usb-controller-esp32s3.json',*sources,*(p for p in sorted(DRIVER.glob('*')) if p.is_file()),
            *sorted((ROOT/'sdk/driver').glob('*.h')),
            Path(__file__).resolve(),ROOT/'scripts/native_usb_idf61_sources.py', ROOT/'scripts/native_usb_bounded_host.py', ROOT/'scripts/native_usb_bounded_endpoints.py',
            ROOT/'test/drivers/native_usb_sdk_abi_probe.cpp',
            a.compile_commands.resolve(),Path(entry['directory'])/'config/sdkconfig.h']
    argv=entry.get('arguments') or shlex.split(entry['command'])
    inputs += [Path(arg[1:]) for arg in argv if arg.startswith('@')]
    inputs += [Path(argv[i+1]) for i,arg in enumerate(argv[:-1]) if arg=='-include']
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    if hashes[str(a.compile_commands.resolve())]!=hashlib.sha256(compile_database).hexdigest():
        raise ValueError('compile database changed while selecting native command')
    objs=[]; commands=[]; normalized={}
    abi=out/'native-usb-abi-profile.h'; abi.write_text(abi_header())
    probe=compile_command(entry,ROOT/'test/drivers/native_usb_sdk_abi_probe.cpp',
                          out/'native-usb-sdk-abi-probe.o',config,usb,idf)
    probe+=['-include',str(abi)]
    (out/'native-usb-sdk-abi-probe-command.json').write_text(json.dumps(probe,indent=2)+'\n')
    subprocess.run(probe,cwd=entry['directory'],check=True)
    for i,original in enumerate(sources):
        source=original
        if original.name in ('enum.c','hub.c','hcd_dwc.c','usb_host.c','usbh.c','usb_private.c','esp_err_to_name.c'):
            from native_usb_idf61_sources import adapt
            source=out/original.name
            source.write_text(adapt(original.name,instrument(original.name,original.read_text())))
        obj=out/f'{i:02}-{original.name}.o'; cmd=compile_command(entry,source,obj,config,usb,idf)
        if [arg for arg in cmd if re.fullmatch(r'-O(?:[0-3sgz]|fast)',arg)]!=['-Os']:
            raise ValueError('native import cohort requires reviewed release-size optimization -Os')
        commands.append(cmd)
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        subprocess.run(cmd,cwd=entry['directory'],check=True)
        normalized[original.name]=normalize_sections(obj,Path(commands[0][0]))
        objs.append(obj)
    for path,digest in hashes.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest:
            raise ValueError('native build input changed during compilation: '+path)
    compiler=Path(commands[0][0]); version=run([compiler,'-dumpfullversion']).strip()
    if version!='15.2.0': raise ValueError('unreviewed compiler '+version)
    banner=run([compiler,'--version']).splitlines()[0]
    if '(crosstool-NG '+COMPILER_DISTRIBUTION+') '+version not in banner:
        raise ValueError('unreviewed compiler distribution '+banner)
    values=dict(re.findall(r'PROVIDE\s*\(\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*\)',(idf/'components/soc/esp32s3/ld/esp32s3.peripherals.ld').read_text()))
    elf=out/'driver.elf'
    cmd=[compiler,'-shared','-nostdlib','-nostartfiles','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL',
         '-Wl,-Bsymbolic','-Wl,--version-script,'+str(DRIVER/'exports.map'),
         *[f'-Wl,--defsym,{name}={values[name]}' for name in MMIO],
         '-Wl,--defsym,RISC_USB_DWC_HOST='+hex(int(values['USB_DWC'],16)+0x400),*objs,'-lgcc','-o',elf]
    subprocess.run(list(map(str,cmd)),check=True)
    imports=audit(elf,compiler)
    (out/'imports.txt').write_text('\n'.join(imports)+'\n')
    metadata={'schema_version':1,'status':'experimental-native-candidate-not-hardware-qualified',
              'driver_id':'usb-controller-esp32s3','version':'0.1.27','file_name':'driver.elf',
              'architecture':'xtensa-esp32s3','target':'esp32s3',
              'requires':REQUIRES,'provides':PROVIDES,
              'idf_commit':IDF_COMMIT,
              'usb_commit':USB_COMMIT,'usb_version':'1.6.0','compiler':version,
              'compiler_distribution':COMPILER_DISTRIBUTION,
              'optimization':'-Os',
              'abi_profile':ABI_PROFILE,
              'abi_profile_sha256':hashlib.sha256(json.dumps(ABI_PROFILE,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
              'provider_abi':2,'os_cpu_abi':2,'native_cohort':NATIVE_COHORT,
              'sdkconfig_sha256':hashes[str(Path(entry['directory'])/'config/sdkconfig.h')],
              'compile_commands_sha256':hashes[str(a.compile_commands.resolve())],
              'source_hashes':hashes,'usb_config':USB_CONFIG, 'normalized_iram_sections':normalized,
              'size':elf.stat().st_size,'sha256':hashlib.sha256(elf.read_bytes()).hexdigest(), 'imports':imports}
    (out/'qualification.json').write_text(json.dumps(metadata,indent=2)+'\n')
    private=[n for n in imports if n.startswith(FORBIDDEN)]
    if private: raise ValueError('USB/private ownership leak: '+str(private))
    allowed=set(IMPORT_FILE.read_text().split()) if IMPORT_FILE.exists() else set()
    if set(imports)!=allowed: raise ValueError('unreviewed import delta: '+str(sorted(set(imports)^allowed)))
    from verify_usb_controller_loader_map import audit_loader_map
    metadata['loader_map']=audit_loader_map(elf)
    if any(metadata['loader_map'][k] for k in ('unmapped_relocations','unmapped_relative_values','unmapped_executable_sections')):
        raise ValueError('provider loader-map qualification failed: '+str(metadata['loader_map']))
    (out/'qualification.json').write_text(json.dumps(metadata,indent=2)+'\n')
    package=dict(manifest)
    package.update(size_bytes=metadata['size'],sha256=metadata['sha256'])
    (out/'manifest.json').write_text(json.dumps(package,indent=2)+'\n')
    print(json.dumps(metadata,indent=2))

if __name__=='__main__': main()
