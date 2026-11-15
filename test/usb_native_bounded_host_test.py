#!/usr/bin/env python3
"""Compile exact pinned-source private release/close clones with fault injection."""
from pathlib import Path
import argparse,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from native_usb_bounded_host import adapt_host,extract
ap=argparse.ArgumentParser();ap.add_argument('--usb-root',required=True,type=Path);a=ap.parse_args()
s=(a.usb_root/'host/usb/src/usb_host.c').read_text(); adapted=adapt_host(s)
functions='\n'.join(extract(adapted,'esp_err_t risc_usb_'+n+'_poll(') for n in ('interface_release','device_close'))
assert 'portMAX_DELAY' not in functions and 'vTaskDelay(' not in functions
# The per-client pump performs one finite event-batch and never invokes daemon work.
f=extract(adapted,'esp_err_t risc_usb_client_transfer_events(')
assert 'while (1)' not in f and 'hub_process' not in f and 'usbh_process' not in f
for signature in ('esp_err_t usb_host_interface_release(', 'esp_err_t usb_host_device_close('):
 try:adapt_host(s+s[s.index(signature):])
 except ValueError:pass
 else:raise AssertionError('accepted duplicate function')
preamble=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/queue.h>
typedef int esp_err_t; typedef void *usb_device_handle_t; typedef void *usb_host_client_handle_t;
enum {ESP_OK,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_NOT_FOUND,ESP_ERR_NOT_FINISHED};
#define pdTRUE 1
#define HOST_CHECK(c,e) do{if(!(c))return e;}while(0)
#define HOST_CHECK_FROM_CRIT(c,e) HOST_CHECK(c,e)
#define HOST_ENTER_CRITICAL() ((void)0)
#define HOST_EXIT_CRITICAL() ((void)0)
#define ESP_ERROR_CHECK(c) assert((c)==ESP_OK)
typedef struct interface {struct {void *dev_hdl;}constant;struct {TAILQ_ENTRY(interface) tailq_entry;}mux_protected;} interface_t;
TAILQ_HEAD(interfaces,interface);TAILQ_HEAD(pending,interface);
typedef struct {struct {struct {unsigned taking_mux,num_intf_claimed;}flags;struct pending pending_ep_tailq;}dynamic;struct {struct interfaces interface_tailq;}mux_protected;}client_t;
static struct {struct {int mux_lock;}constant;} host_obj;
static __typeof__(host_obj) *p_host_lib_obj=&host_obj;
static bool lock_available,opened,release_ok,close_ok;static unsigned locks,gives,releases,closes;
static int xSemaphoreTake(int lock,int ticks){(void)lock;assert(ticks==0);++locks;return lock_available;}
static void xSemaphoreGive(int lock){(void)lock;++gives;}
static int usbh_dev_get_addr(void *d,uint8_t *addr){assert(d);*addr=1;return ESP_OK;}
static bool _check_client_opened_device(client_t*c,uint8_t addr){(void)c;assert(addr==1);return opened;}
static void _clear_client_opened_device(client_t*c,uint8_t addr){(void)c;assert(addr==1&&opened&&close_ok);opened=false;}
static int interface_release(client_t*c,void*d,uint8_t i){(void)c;assert(d&&i==0);++releases;return release_ok?ESP_OK:ESP_ERR_INVALID_STATE;}
static int usbh_dev_close(void*d){assert(d&&opened);++closes;return close_ok?ESP_OK:ESP_ERR_INVALID_STATE;}
'''
main=r'''
int main(void){client_t c={0};TAILQ_INIT(&c.dynamic.pending_ep_tailq);TAILQ_INIT(&c.mux_protected.interface_tailq);
 opened=true;c.dynamic.flags.num_intf_claimed=1;void*d=&c;
 assert(risc_usb_interface_release_poll(&c,d,0)==ESP_ERR_NOT_FINISHED&&!c.dynamic.flags.taking_mux&&!releases);
 lock_available=true;interface_t ep={0};TAILQ_INSERT_HEAD(&c.dynamic.pending_ep_tailq,&ep,mux_protected.tailq_entry);
 assert(risc_usb_interface_release_poll(&c,d,0)==ESP_ERR_NOT_FINISHED&&!c.dynamic.flags.taking_mux&&!releases);
 TAILQ_REMOVE(&c.dynamic.pending_ep_tailq,&ep,mux_protected.tailq_entry);
 assert(risc_usb_interface_release_poll(&c,d,0)==ESP_ERR_INVALID_STATE&&c.dynamic.flags.num_intf_claimed==1);
 release_ok=true;assert(risc_usb_interface_release_poll(&c,d,0)==ESP_OK&&!c.dynamic.flags.num_intf_claimed);
 lock_available=false;assert(risc_usb_device_close_poll(&c,d)==ESP_ERR_NOT_FINISHED&&opened&&!closes);
 lock_available=true;assert(risc_usb_device_close_poll(&c,d)==ESP_ERR_INVALID_STATE&&opened&&closes==1);
 close_ok=true;assert(risc_usb_device_close_poll(&c,d)==ESP_OK&&!opened&&closes==2);
 assert(risc_usb_device_close_poll(&c,d)==ESP_ERR_NOT_FOUND&&closes==2);
 puts("PASS actual private host clones: try-lock, pending callback, release failure, close failure retains open record, retry");}
'''
flags=['-std=gnu11','-Wall','-Wextra','-Werror','-g']
if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='usb-bounded-host-') as t:
 p=Path(t);(p/'test.c').write_text(preamble+functions+main)
 subprocess.run(['cc',*flags,str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
