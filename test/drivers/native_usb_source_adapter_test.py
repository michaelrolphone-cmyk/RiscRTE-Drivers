#!/usr/bin/env python3
"""Structural mutation tests against the immutable official USB host sources."""
import argparse
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from native_usb_idf61_sources import adapt,function
from build_usb_controller_esp32s3_idf61 import instrument,config_header,pin,USB_COMMIT,abi_header,ABI_PROFILE,ABI_EXPRESSIONS
ap=argparse.ArgumentParser();ap.add_argument('--usb',type=Path,required=True);a=ap.parse_args()
pin(a.usb,USB_COMMIT)
sources={n:(a.usb/'host/usb/src'/n).read_text() for n in ('hcd_dwc.c','hub.c','enum.c','usb_host.c')}
out={n:adapt(n,instrument(n,s)) for n,s in sources.items()}
for token in ('xTimerCreate(', 'xTimerDelete(', 'xTimerReset(', 'xTimerStop(', 'xTimerChangePeriod(', 'vTimerSetReloadMode(', 'TimerHandle_t'):
 assert token not in out['usb_host.c'], token
setter=out['usb_host.c'].split('esp_err_t usb_host_lib_set_auto_suspend(',1)[1].split('}',1)[0]
assert 'return ESP_ERR_NOT_SUPPORTED;' in setter
assert out['usb_host.c'].count('esp_err_t usb_host_install(')==1
assert out['usb_host.c'].count('esp_err_t usb_host_uninstall(')==1
assert out['hub.c'].count('esp_err_t hub_uninstall(')==1
assert out['hub.c'].count('esp_err_t hub_root_stop(')==1
assert out['hcd_dwc.c'].count('esp_err_t hcd_port_deinit(')==1
for api in ('alloc','disable','enable','free'):
 assert 'esp_intr_'+api+'(' not in out['hcd_dwc.c']
assert out['hcd_dwc.c'].count('irq_rc = risc_usb_intr_enable(port->isr_hdl);')==1
assert out['enum.c'].count('risc_usb_enum_error("%s FAILED"')==1
assert out['enum.c'].count('ESP_LOGE(ENUM_TAG, ')==sources['enum.c'].count('ESP_LOGE(ENUM_TAG, ')==16
assert 'risc_usb_enum_stage(enum_stage_strings[stage]);' in out['enum.c']
assert out['hub.c'].count('risc_usb_enum_reset();')==1
# Optional string descriptor errors are not promoted into permanent enum failure.
assert 'risc_usb_enum_error("LANGID' not in out['enum.c']
for name,anchor in [('enum.c','#include "esp_log.h"'),('enum.c','ESP_LOGE(ENUM_TAG, "%s FAILED", enum_stage_strings[last_stage]);'),('hub.c','case HCD_PORT_EVENT_CONNECTION: {')]:
 bad=sources[name].replace(anchor,anchor+'\n'+anchor)
 try: instrument(name,bad)
 except ValueError: pass
 else: raise AssertionError('accepted duplicate anchor '+anchor)
for name in ('hcd_dwc.c','hub.c','usb_host.c'):
 bad=sources[name].replace('#include "sdkconfig.h"','/* changed include */',1)
 try: adapt(name,bad)
 except ValueError: pass
 else: raise AssertionError('accepted changed SDK source '+name)
assert function('int f() { const char *s="}"; /* } */ return 1; }\nint g() {}','int f()','REPLACED')=='REPLACED\nint g() {}'
for name in ('AUTO_PM_LIGHT_SLEEP','HUBS_SUPPORTED','DWC_DMA_CAP_MEMORY_IN_PSRAM'):
 assert '#ifdef CONFIG_USB_HOST_'+name in config_header()
assert len(ABI_PROFILE)==33 and ABI_PROFILE.keys()==ABI_EXPRESSIONS.keys()
assert abi_header().count('static_assert(')==len(ABI_PROFILE)
for key,value in ABI_PROFILE.items():
 assert f'({ABI_EXPRESSIONS[key]}) == {value}U' in abi_header()
print('native USB source transformations: PASS')
