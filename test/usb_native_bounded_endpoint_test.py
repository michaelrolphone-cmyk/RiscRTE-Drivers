#!/usr/bin/env python3
"""Execute actual transformed pinned HCD/USBH bodies in a deterministic C harness.

This proves software ownership/retry transitions, not hardware interrupt latency
or a whole-daemon worst-case time bound. No SDK checkout is modified.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from native_usb_bounded_endpoints import adapt  # noqa: E402

PIN = '0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a'


def definition(source, signature):
    """Extract one definition, excluding the same function's prototype."""
    anchor = signature + '\n{'
    assert source.count(anchor) == 1, anchor
    start = source.index(anchor)
    pos = source.index('{', start)
    level, state, i = 0, 'code', pos
    while i < len(source):
        c, pair = source[i], source[i:i + 2]
        if state == 'code':
            if pair == '//': state, i = 'line', i + 2; continue
            if pair == '/*': state, i = 'comment', i + 2; continue
            if c in ('"', "'"): state = c
            elif c == '{': level += 1
            elif c == '}':
                level -= 1
                if level == 0: return source[start:i + 1]
        elif state == 'line':
            if c == '\n': state = 'code'
        elif state == 'comment':
            if pair == '*/': state, i = 'code', i + 2; continue
        elif c == '\\': i += 2; continue
        elif c == state: state = 'code'
        i += 1
    raise AssertionError('unterminated function')


HCD_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
typedef void *TaskHandle_t;
typedef int BaseType_t;
typedef int hcd_pipe_cmd_t;
typedef int hcd_pipe_event_t;
typedef int usb_dwc_hal_chan_event_t;
typedef int usb_dwc_hal_chan_error_t;
#define HCD_PIPE_CMD_HALT 0
#define HCD_PIPE_CMD_FLUSH 1
#define HCD_PIPE_CMD_CLEAR 2
#define HCD_PIPE_STATE_ACTIVE 0
#define HCD_PIPE_STATE_HALTED 1
#define HCD_PORT_STATE_ENABLED 0
#define HCD_PORT_STATE_SUSPENDED 1
#define HCD_PORT_STATE_RESUMING 2
#define HCD_PIPE_EVENT_NONE 0
#define HCD_PIPE_EVENT_URB_DONE 1
#define HCD_PIPE_EVENT_ERROR_XFER 2
#define USB_DWC_HAL_CHAN_EVENT_NONE 0
#define USB_DWC_HAL_CHAN_EVENT_CPLT 1
#define USB_DWC_HAL_CHAN_EVENT_HALT_REQ 2
#define USB_DWC_HAL_CHAN_EVENT_ERROR 3
#define USB_TRANSFER_STATUS_CANCELED 2
#define USB_TRANSFER_STATUS_NO_DEVICE 3
#define USB_DWC_XFER_TYPE_ISOCHRONOUS 3
#define URB_HCD_STATE_DONE 3
#define NUM_BUFFERS 2
static int critical_depth;
#define HCD_ENTER_CRITICAL() (++critical_depth)
#define HCD_EXIT_CRITICAL() assert(--critical_depth >= 0)
#define HCD_CHECK_FROM_CRIT(cond, rc) do { if (!(cond)) { HCD_EXIT_CRITICAL(); return rc; } } while (0)
typedef struct urb_s {
 TAILQ_ENTRY(urb_s) tailq_entry;
 int hcd_var;
 struct { int status, actual_num_bytes, num_isoc_packets; struct {int status,actual_num_bytes;} isoc_packet_desc[8]; } transfer;
} urb_t;
struct pipe_obj;
typedef struct port_obj {
 struct {int conn_dev_ena;} flags;
 int state,num_pipes_idle;
 void *hal;
 TAILQ_HEAD(,pipe_obj) pipes_idle_tailq;
} port_t;
typedef struct { struct { int active,halt_requested; } flags; int event; } usb_dwc_hal_chan_t;
typedef void *hcd_pipe_handle_t;
typedef struct { struct {int executing,was_canceled,stop_idx,pipe_event;} status_flags; } dma_buffer_block_t;
static dma_buffer_block_t buffers[NUM_BUFFERS];
typedef struct pipe_obj {
 TAILQ_HEAD(,urb_s) pending_urb_tailq;
 TAILQ_HEAD(,urb_s) done_urb_tailq;
 TAILQ_ENTRY(pipe_obj) tailq_entry;
 int num_urb_pending,num_urb_done;
 dma_buffer_block_t *buffers[NUM_BUFFERS];
 struct { int buffer_is_executing,buffer_num_to_exec,buffer_num_to_parse;unsigned rd_idx:1;unsigned fr_idx:1; } multi_buffer_control;
 struct { int type; } ep_char;
 usb_dwc_hal_chan_t *chan_obj;
 port_t *port;
 int state,last_event;
 volatile TaskHandle_t task_waiting_pipe_notif;
 struct {int waiting_halt,pipe_cmd_processing,has_urb,risc_halt_pending,risc_halt_invalidated;} cs_flags;
 bool (*callback)(hcd_pipe_handle_t,int,void*,bool);
 void *callback_arg;
} pipe_t;
static int requests, waits, notifications, parsed, canceled, flushes, callbacks, channels_freed;
static pipe_t *waiting_pipe;
static esp_err_t _pipe_cmd_flush(pipe_t *pipe);
static esp_err_t _pipe_cmd_clear(pipe_t *pipe);
static void _internal_pipe_event_wait(pipe_t *pipe);
static bool _internal_pipe_event_notify(pipe_t *pipe, bool from_isr);
static hcd_pipe_event_t _intr_hdlr_chan(pipe_t *, usb_dwc_hal_chan_t *, bool *);
static bool usb_dwc_hal_chan_request_halt(usb_dwc_hal_chan_t *c) {
 ++requests;
 if(c->flags.active) { c->flags.halt_requested=1; return false; }
 return true;
}
static void usb_dwc_hal_chan_mark_halted(usb_dwc_hal_chan_t *c) {c->flags.active=0;}
static int usb_dwc_hal_chan_decode_intr(usb_dwc_hal_chan_t *c) {
 int e=c->event;
 if(e==USB_DWC_HAL_CHAN_EVENT_CPLT || e==USB_DWC_HAL_CHAN_EVENT_HALT_REQ) {
  if(c->flags.halt_requested) {e=USB_DWC_HAL_CHAN_EVENT_HALT_REQ;c->flags.halt_requested=0;}
  c->flags.active=0;
 } else if(e==USB_DWC_HAL_CHAN_EVENT_ERROR) c->flags.active=0;
 c->event=USB_DWC_HAL_CHAN_EVENT_NONE;
 return e;
}
static int usb_dwc_hal_chan_get_error(usb_dwc_hal_chan_t *c) {(void)c;return 1;}
static int usb_dwc_hal_chan_get_qtd_idx(usb_dwc_hal_chan_t *c) {(void)c;return 0;}
static int pipe_decode_error_event(int error) {(void)error;return HCD_PIPE_EVENT_ERROR_XFER;}
static bool _buffer_check_done(pipe_t *p) {(void)p;return true;}
static void _buffer_exec_cont(pipe_t *p) {(void)p;abort();}
static bool _buffer_can_exec(pipe_t *p) {(void)p;return false;}
static bool _buffer_can_fill(pipe_t *p) {(void)p;return false;}
static void _buffer_exec(pipe_t *p) {(void)p;abort();}
static void _buffer_fill(pipe_t *p) {(void)p;abort();}
static inline void _buffer_done(pipe_t *p,int idx,int event,bool cancel);
static bool _buffer_flush_all(pipe_t *p,bool cancel);
static void _buffer_parse(pipe_t *p) {
 assert(p->multi_buffer_control.buffer_num_to_parse>0);
 canceled+=p->buffers[p->multi_buffer_control.fr_idx]->status_flags.was_canceled;
 ++p->multi_buffer_control.fr_idx;--p->multi_buffer_control.buffer_num_to_parse;
 ++parsed;++p->num_urb_done;
}
static bool cb(hcd_pipe_handle_t h,int e,void *arg,bool isr) {(void)h;(void)e;(void)arg;(void)isr;return false;}
static void buffer_block_free(void *p) {(void)p;}
static void usb_dwc_hal_chan_free(void *hal,usb_dwc_hal_chan_t *c) {(void)hal;(void)c;++channels_freed;}
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return (void*)1;}
static void vTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t *w) {assert(t);++notifications;*w=pdFALSE;}
static void xTaskNotifyGive(TaskHandle_t t) {assert(t);++notifications;}
static unsigned ulTaskNotifyTake(int clear,unsigned timeout) {
 assert(!critical_depth && clear==pdTRUE && timeout==portMAX_DELAY);
 ++waits;waiting_pipe->chan_obj->event=USB_DWC_HAL_CHAN_EVENT_CPLT;
 HCD_ENTER_CRITICAL();bool yield=false;
 (void)_intr_hdlr_chan(waiting_pipe,waiting_pipe->chan_obj,&yield);
 HCD_EXIT_CRITICAL();return 1;
}
'''

HCD_MAIN = r'''
static void setup(pipe_t *p,port_t *port,usb_dwc_hal_chan_t *chan,bool active) {
 memset(p,0,sizeof(*p));memset(port,0,sizeof(*port));memset(chan,0,sizeof(*chan));
 port->flags.conn_dev_ena=1;port->state=HCD_PORT_STATE_ENABLED;
 TAILQ_INIT(&p->pending_urb_tailq);TAILQ_INIT(&p->done_urb_tailq);TAILQ_INIT(&port->pipes_idle_tailq);
 memset(buffers,0,sizeof(buffers));for(int i=0;i<NUM_BUFFERS;++i)p->buffers[i]=&buffers[i];
 p->chan_obj=chan;p->port=port;p->callback=cb;p->state=HCD_PIPE_STATE_ACTIVE;
 p->multi_buffer_control.buffer_num_to_exec=active;
 p->multi_buffer_control.buffer_is_executing=active;p->cs_flags.has_urb=active;chan->flags.active=active;
 requests=waits=notifications=parsed=canceled=flushes=callbacks=channels_freed=0;waiting_pipe=p;
 assert(critical_depth==0);
}
static int irq(pipe_t *p,int event) {
 p->chan_obj->event=event;bool yield=false;HCD_ENTER_CRITICAL();
 int e=_intr_hdlr_chan(p,p->chan_obj,&yield);HCD_EXIT_CRITICAL();return e;
}
static void client_pump(pipe_t *p) {callbacks+=p->num_urb_done;p->num_urb_done=0;p->cs_flags.has_urb=0;}
int main(void) {
 pipe_t p;port_t port;usb_dwc_hal_chan_t chan;bool submit;
 /* Delayed and missing IRQs retain the channel and DMA without a waiter. */
 setup(&p,&port,&chan,true);
 assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 for(int i=0;i<1000;++i) assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 assert(requests==1 && waits==0 && callbacks==0 && p.multi_buffer_control.buffer_is_executing);
 assert(!_check_port_pipe_state(&p,&submit));
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_ERR_NOT_FINISHED);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_CLEAR)==ESP_ERR_NOT_FINISHED);
 assert(hcd_pipe_free(&p)==ESP_ERR_INVALID_STATE && channels_freed==0);
 assert(irq(&p,USB_DWC_HAL_CHAN_EVENT_CPLT)==HCD_PIPE_EVENT_URB_DONE);
 assert(parsed==1 && canceled==1 && notifications==0 && callbacks==0);
 assert(risc_usb_hcd_halt_poll(&p)==ESP_OK);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_OK);
 assert(hcd_pipe_free(&p)==ESP_ERR_INVALID_STATE); /* callback still owns URB */
 client_pump(&p);assert(callbacks==1);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_CLEAR)==ESP_OK);
 assert(_check_port_pipe_state(&p,&submit) && submit && waits==0);
 /* Completion before cancellation is already retired, not canceled twice. */
 setup(&p,&port,&chan,true);assert(irq(&p,USB_DWC_HAL_CHAN_EVENT_CPLT)==HCD_PIPE_EVENT_URB_DONE);
 assert(risc_usb_hcd_halt_poll(&p)==ESP_OK && canceled==0 && parsed==1);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_OK && p.num_urb_done==1);
 /* Error beats HALT_REQ. Clear stale HAL request state before resuming. */
 setup(&p,&port,&chan,true);assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 assert(irq(&p,USB_DWC_HAL_CHAN_EVENT_ERROR)==HCD_PIPE_EVENT_ERROR_XFER);
 assert(!p.cs_flags.risc_halt_pending && !chan.flags.halt_requested && parsed==1 && notifications==0);
 assert(risc_usb_hcd_halt_poll(&p)==ESP_OK);
 client_pump(&p);assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_CLEAR)==ESP_OK);
 chan.flags.active=1;p.multi_buffer_control.buffer_is_executing=1;p.cs_flags.has_urb=1;
 p.multi_buffer_control.buffer_num_to_exec=1;
 assert(irq(&p,USB_DWC_HAL_CHAN_EVENT_CPLT)==HCD_PIPE_EVENT_URB_DONE && canceled==0);
 /* Disconnection proves channel invalidation; late IRQ must not double parse. */
 setup(&p,&port,&chan,true);assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 port.flags.conn_dev_ena=0;assert(risc_usb_hcd_halt_poll(&p)==ESP_OK);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_OK && p.num_urb_done==1);
 assert(irq(&p,USB_DWC_HAL_CHAN_EVENT_CPLT)==HCD_PIPE_EVENT_NONE && parsed==1);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_CLEAR)==ESP_ERR_INVALID_STATE);
 port.flags.conn_dev_ena=1;assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_INVALID_STATE);
 assert(!_check_port_pipe_state(&p,&submit));
 /* Preserve legacy synchronous calls, including joining a private request. */
 setup(&p,&port,&chan,true);assert(hcd_pipe_command(&p,HCD_PIPE_CMD_HALT)==ESP_OK);
 assert(waits==1 && notifications==1 && requests==1);
 setup(&p,&port,&chan,true);assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 assert(hcd_pipe_command(&p,HCD_PIPE_CMD_HALT)==ESP_OK);
 assert(waits==1 && notifications==1 && requests==1);
 /* Daemon halt after invalidation must not join a missing IRQ waiter. */
 setup(&p,&port,&chan,true);assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 port.flags.conn_dev_ena=0;assert(hcd_pipe_command(&p,HCD_PIPE_CMD_HALT)==ESP_OK);
 assert(waits==0 && requests==1 && p.cs_flags.risc_halt_invalidated);
 /* Filled and software-pending URBs are retired exactly once by real flush. */
 setup(&p,&port,&chan,true);assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED);
 p.multi_buffer_control.buffer_num_to_exec=2;
 urb_t deferred={0};TAILQ_INSERT_TAIL(&p.pending_urb_tailq,&deferred,tailq_entry);p.num_urb_pending=1;
 assert(irq(&p,USB_DWC_HAL_CHAN_EVENT_CPLT)==HCD_PIPE_EVENT_URB_DONE);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_OK);
 assert(p.num_urb_done==3 && p.num_urb_pending==0 && parsed==2 && canceled==2);
 assert(deferred.hcd_var==URB_HCD_STATE_DONE && deferred.transfer.status==USB_TRANSFER_STATUS_CANCELED);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_OK && p.num_urb_done==3);
 /* A live legacy waiter is never stolen by private polling. */
 setup(&p,&port,&chan,true);p.task_waiting_pipe_notif=(void*)1;
 assert(risc_usb_hcd_halt_poll(&p)==ESP_ERR_NOT_FINISHED && requests==0);
 assert(risc_usb_hcd_endpoint_command(&p,HCD_PIPE_CMD_FLUSH)==ESP_ERR_NOT_FINISHED);
 assert(critical_depth==0);
 puts("transformed HCD: delayed/missing IRQ, races, invalidation, retry, callback ownership and legacy wait passed");
 return 0;
}
'''

USBH_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/queue.h>
typedef int esp_err_t;
typedef void *usb_device_handle_t;
typedef void *hcd_pipe_handle_t;
typedef int hcd_pipe_cmd_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_NOT_FOUND 4
#define pdTRUE 1
#define pdFALSE 0
#define HCD_PIPE_CMD_FLUSH 1
#define HCD_PIPE_CMD_CLEAR 2
#define USBH_EVENT_CTRL_XFER 7
#define USBH_TAG "test"
#define ESP_LOGD(...) ((void)0)
#define USBH_ENTER_CRITICAL() ((void)0)
#define USBH_EXIT_CRITICAL() ((void)0)
typedef struct urb_s {int id;} urb_t;
typedef struct {int event;struct {usb_device_handle_t dev_hdl;urb_t *urb;} ctrl_xfer_data;} usbh_event_data_t;
typedef struct {struct {hcd_pipe_handle_t pipe_hdl;} constant;} endpoint_t;
typedef struct {
 struct {struct {int enum_lock;} flags;unsigned action_flags;int num_ctrl_xfers_inflight;} dynamic;
 struct {hcd_pipe_handle_t default_pipe;int address;} constant;
 endpoint_t *ep;
} device_t;
typedef struct {struct {int mux_lock;void (*event_cb)(usbh_event_data_t*,void*);void *event_cb_arg;} constant;} usbh_t;
static usbh_t usbh;
static usbh_t *p_usbh_obj=&usbh;
static int lock_available=1,takes,gives,hcd_calls,dequeued,queued_callbacks;
static unsigned last_timeout=999;
static hcd_pipe_handle_t last_pipe;
static int last_command;
static urb_t urb={1};
static int xSemaphoreTake(int lock,unsigned ticks) {(void)lock;++takes;last_timeout=ticks;return lock_available;}
static void xSemaphoreGive(int lock) {(void)lock;++gives;}
static endpoint_t *get_ep_from_addr(device_t *d,uint8_t ep) {return ep==0x81 ? d->ep : NULL;}
static urb_t *hcd_urb_dequeue(hcd_pipe_handle_t pipe) {(void)pipe;if(!dequeued++)return &urb;return NULL;}
static void completion(usbh_event_data_t *event,void *arg) {(void)arg;assert(event->event==USBH_EVENT_CTRL_XFER);++queued_callbacks;}
esp_err_t risc_usb_hcd_halt_poll(hcd_pipe_handle_t handle) {++hcd_calls;last_pipe=handle;return ESP_ERR_NOT_FINISHED;}
esp_err_t risc_usb_hcd_endpoint_command(hcd_pipe_handle_t handle,hcd_pipe_cmd_t cmd) {++hcd_calls;last_pipe=handle;last_command=cmd;return ESP_OK;}
'''

USBH_MAIN = r'''
int main(void) {
 endpoint_t ep={.constant={.pipe_hdl=(void*)2}};
 device_t d={.constant={.default_pipe=(void*)1},.ep=&ep};
 usbh.constant.event_cb=completion;
 assert(risc_usb_endpoint_halt_poll(&d,0x81)==ESP_ERR_NOT_FINISHED && last_pipe==(void*)2 && last_timeout==0);
 assert(risc_usb_endpoint_flush(&d,0x81)==ESP_OK && last_command==HCD_PIPE_CMD_FLUSH);
 assert(risc_usb_endpoint_clear(&d,0x81)==ESP_OK && last_command==HCD_PIPE_CMD_CLEAR);
 assert(risc_usb_endpoint_halt_poll(&d,0)==ESP_ERR_NOT_FINISHED && last_pipe==(void*)1);
 d.dynamic.flags.enum_lock=1;
 assert(risc_usb_endpoint_halt_poll(&d,0)==ESP_ERR_INVALID_STATE);
 assert(risc_usb_device_control_dequeue(&d)==ESP_ERR_INVALID_STATE && queued_callbacks==0);
 d.dynamic.flags.enum_lock=0;
 int previous=hcd_calls;lock_available=0;
 assert(risc_usb_endpoint_halt_poll(&d,0x81)==ESP_ERR_NOT_FINISHED && hcd_calls==previous);
 assert(risc_usb_endpoint_flush(&d,0x81)==ESP_ERR_NOT_FINISHED);
 assert(risc_usb_endpoint_clear(&d,0x81)==ESP_ERR_NOT_FINISHED);
 assert(risc_usb_device_control_dequeue(&d)==ESP_ERR_NOT_FINISHED);
 lock_available=1;
 assert(risc_usb_endpoint_halt_poll(&d,0x82)==ESP_ERR_NOT_FOUND);
 assert(risc_usb_endpoint_halt_poll(&d,0x80)==ESP_ERR_INVALID_ARG);
 assert(risc_usb_endpoint_halt_poll(&d,0x71)==ESP_ERR_INVALID_ARG);
 d.dynamic.action_flags=0xabcdef;d.dynamic.num_ctrl_xfers_inflight=1;
 assert(risc_usb_device_control_dequeue(&d)==ESP_OK && queued_callbacks==1);
 assert(d.dynamic.action_flags==0xabcdef && d.dynamic.num_ctrl_xfers_inflight==0);
 assert(risc_usb_device_control_dequeue(&d)==ESP_OK && queued_callbacks==1);
 assert(last_timeout==0 && gives==takes-4);
 puts("transformed USBH: try-lock, bulk/interrupt lookup, EP0 enumeration exclusion and daemon-free dequeue passed");
 return 0;
}
'''


def compile_run(code, label, directory):
    cfile = directory / (label + '.c')
    binary = directory / label
    cfile.write_text(code)
    sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.environ.get('SANITIZE') == '1' else []
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-g', '-O0', *sanitize, str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--usb-root', type=Path, default=ROOT.parent / 'esp-usb')
    args = parser.parse_args()
    sources = {}
    for name in ('hcd_dwc.c', 'usbh.c'):
        original = subprocess.check_output(['git', '-C', str(args.usb_root), 'show',
                                            f'{PIN}:host/usb/src/{name}'], text=True)
        sources[name] = adapt(name, original)
        drift = original.replace('            uint32_t reserved29: 29;', '            uint32_t reserved28: 28;') if name == 'hcd_dwc.c' else original.replace('#include "hcd.h"', '#include "changed_hcd.h"')
        try:
            adapt(name, drift)
        except ValueError:
            pass
        else:
            raise AssertionError('changed pinned source anchor must fail closed')
        try:
            adapt(name, sources[name])
        except ValueError:
            pass
        else:
            raise AssertionError('double adaptation must fail closed')
    hcd = sources['hcd_dwc.c']
    # Exercise actual transformed definitions, not hand-copied replacement logic.
    signatures = [
        'static inline void _buffer_done(pipe_t *pipe, int stop_idx, hcd_pipe_event_t pipe_event, bool canceled)',
        'static bool _buffer_flush_all(pipe_t *pipe, bool canceled)',
        'static esp_err_t risc_hcd_halt_poll_locked(pipe_t *pipe)',
        'esp_err_t risc_usb_hcd_halt_poll(hcd_pipe_handle_t handle)',
        'esp_err_t risc_usb_hcd_endpoint_command(hcd_pipe_handle_t handle, hcd_pipe_cmd_t command)',
        'static void _internal_pipe_event_wait(pipe_t *pipe)',
        'static bool _internal_pipe_event_notify(pipe_t *pipe, bool from_isr)',
        'static hcd_pipe_event_t _intr_hdlr_chan(pipe_t *pipe, usb_dwc_hal_chan_t *chan_obj, bool *yield)',
        'static esp_err_t _pipe_cmd_halt(pipe_t *pipe)',
        'static esp_err_t _pipe_cmd_flush(pipe_t *pipe)',
        'static esp_err_t _pipe_cmd_clear(pipe_t *pipe)',
        'esp_err_t hcd_pipe_command(hcd_pipe_handle_t pipe_hdl, hcd_pipe_cmd_t command)',
        'esp_err_t hcd_pipe_free(hcd_pipe_handle_t pipe_hdl)',
        'static inline bool _check_port_pipe_state(pipe_t *pipe, bool *submit_urb)',
    ]
    hcd_code = HCD_PREFIX + '\n'.join(definition(hcd, s) for s in signatures) + HCD_MAIN
    usbh = sources['usbh.c']
    usbh_code = USBH_PREFIX + definition(usbh, 'static inline void handle_ep0_dequeue(device_t *dev_obj)')
    # The inserted private helper source is exactly the transformed source span.
    begin = usbh.index('static esp_err_t risc_usb_endpoint_operation(')
    end = usbh.index('\nesp_err_t usbh_ep_command(', begin)
    usbh_code += '\n' + usbh[begin:end] + USBH_MAIN
    with tempfile.TemporaryDirectory(prefix='bounded-endpoint-') as tmp:
        compile_run(hcd_code, 'hcd', Path(tmp))
        compile_run(usbh_code, 'usbh', Path(tmp))
    print('bounded endpoint adapter: pinned source anchors and duplicate-transform rejection passed')


if __name__ == '__main__':
    main()
