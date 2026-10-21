"""Fail-closed adaptations of the immutable, provider-private USB1.6.0 sources."""
from pathlib import Path
from native_usb_bounded_host import adapt_host
from native_usb_bounded_endpoints import adapt as adapt_bounded_endpoints
ROOT=Path(__file__).resolve().parents[1]
DRIVER=ROOT/'Drivers/usb_controller_esp32s3'


def once(source, old, new):
    if source.count(old)!=1: raise ValueError('native USB source anchor changed: '+old)
    return source.replace(old,new,1)


def function(source, signature, replacement):
    """Replace exactly one C function, lexing comments/strings while balancing braces."""
    if source.count(signature)!=1: raise ValueError('function anchor changed: '+signature)
    begin=source.index(signature); pos=source.index('{',begin); depth=0; state='code'; i=pos
    while i<len(source):
        c=source[i]; nxt=source[i:i+2]
        if state=='code':
            if nxt=='//': state='line'; i+=2; continue
            if nxt=='/*': state='comment'; i+=2; continue
            if c in ('"',"'"): state=c
            elif c=='{': depth+=1
            elif c=='}':
                depth-=1
                if depth==0: return source[:begin]+replacement+source[i+1:]
        elif state=='line':
            if c=='\n': state='code'
        elif state=='comment':
            if nxt=='*/': state='code'; i+=2; continue
        elif c=='\\': i+=2; continue
        elif c==state: state='code'
        i+=1
    raise ValueError('unterminated function '+signature)


def adapt(name, source):
    source = adapt_bounded_endpoints(name, source)
    if name=='esp_err_to_name.c':
        # Preserve the exact immutable SDK lookup; the unused reentrant helper
        # would unnecessarily import strerror_r/strlcpy/snprintf.
        return function(source,'const char *esp_err_to_name_r(esp_err_t code, char *buf, size_t buflen)','')
    if name in ('hcd_dwc.c','usb_private.c'):
        source=once(source,'#include "sdkconfig.h"','#include "sdkconfig.h"\n#include "NativeDmaAlignment.h"')
        caps='XFER_DESC_LIST_CAPS' if name=='hcd_dwc.c' else 'DATA_BUFFER_CAPS'
        source=once(source,f'    esp_cache_get_alignment({caps}, &cache_align);',
            f'    ESP_ERROR_CHECK(risc_usb_internal_dma_alignment({caps}, &cache_align));')
    if name not in ('hcd_dwc.c','hub.c','usb_host.c'): return source
    source=once(source,'#include "sdkconfig.h"','#include "sdkconfig.h"\n#include "NativeUsbLifecycle.h"\n#include "NativeIrqLifecycle.h"\n#if !CONFIG_IDF_TARGET_ESP32S3 || CONFIG_USB_HOST_HUBS_SUPPORTED\n#error "Unreviewed native USB lifecycle"\n#endif')
    if name=='hcd_dwc.c':
        # All HCD IRQ operations use the checked pinned-owner wrapper.
        for api in ('alloc','enable','disable','free'):
            source=source.replace('esp_intr_'+api+'(', 'risc_usb_intr_'+api+'(')
        source=once(source,'    risc_usb_intr_enable(port_obj->isr_hdl);',
                    '    err_ret = risc_usb_intr_enable(port_obj->isr_hdl);\n    if (err_ret != ESP_OK) goto clean_up;')
        source=once(source, '    risc_usb_intr_disable(port->isr_hdl);\n    usb_dwc_hal_core_soft_reset(port->hal);',
            '    esp_err_t irq_rc = risc_usb_intr_disable(port->isr_hdl);\n    HCD_CHECK_FROM_CRIT(irq_rc == ESP_OK, irq_rc);\n    usb_dwc_hal_core_soft_reset(port->hal);')
        source=once(source, '    risc_usb_intr_enable(port->isr_hdl);\n    HCD_EXIT_CRITICAL();',
            '    irq_rc = risc_usb_intr_enable(port->isr_hdl);\n    if (irq_rc != ESP_OK) {\n        port->state = HCD_PORT_STATE_RECOVERY;\n        HCD_EXIT_CRITICAL();\n        return irq_rc;\n    }\n    HCD_EXIT_CRITICAL();')
        start=source.index('clean_up:\n',source.index('esp_err_t hcd_port_init('))
        end=source.index('\nesp_err_t hcd_port_deinit(',start)
        source=source[:start]+'''clean_up:
    if (port_obj) {
        if (!isr_allocated) port_obj->isr_hdl = NULL;
        risc_pending_port = port_obj;
        risc_pending_hal = hal_inited;
        (void)risc_usb_hcd_retry_cleanup();
    } else {
        HCD_ENTER_CRITICAL();
        s_port_inited[port_number] = false;
        HCD_EXIT_CRITICAL();
    }
    return err_ret;
}
'''+source[end:]
        source=function(source,'esp_err_t hcd_port_deinit(hcd_port_handle_t port_hdl)','')
        # Helper needs private HCD types/functions and must precede port_init.
        return once(source,'esp_err_t hcd_port_init(', (DRIVER/'NativeHcdLifecycle.inc').read_text()+'\nesp_err_t hcd_port_init(')
    if name=='hub.c':
        source=function(source,'esp_err_t hub_uninstall(void)','')
        source=function(source,'esp_err_t hub_root_stop(void)','')
        source=once(source,'esp_err_t hub_install(', (DRIVER/'NativeHubLifecycle.inc').read_text()+'\nesp_err_t hub_install(')
        start=source.index('err:\n',source.index('esp_err_t hub_install('))
        end=source.index('\nesp_err_t hub_root_start(',start)
        source=source[:start]+'''err:
    risc_pending_hub = hub_driver_obj;
    (void)risc_usb_hub_retry_cleanup();
    return ret;
}
'''+source[end:]
        # Disable new installation while a failed one still owns callback data.
        return once(source,'    // Allocate Hub driver object','    HUB_DRIVER_CHECK(risc_pending_hub == NULL, ESP_ERR_INVALID_STATE);\n    // Allocate Hub driver object')
    # Removing the unexposed timer is stronger than trusting it never to fire.
    source=once(source,'        TimerHandle_t auto_suspend_timer;             // Freertos timer used for automatic suspend of the root port\n','')
    for signature in ('static inline void reset_auto_suspend_timer(void)', 'static void stop_auto_suspend_timer(void)',
                      'static void auto_suspend_timer_cb(TimerHandle_t xTimer)'):
        source=function(source,signature,'')
    source=source.replace('        reset_auto_suspend_timer();','').replace('        stop_auto_suspend_timer();','')
    source=function(source,'esp_err_t usb_host_install(const usb_host_config_t *config)',(DRIVER/'NativeHostLifecycle.inc').read_text())
    # There are now two uninstall definitions; remove the original tail only.
    anchor='esp_err_t usb_host_uninstall(void)'; first=source.index(anchor); second=source.index(anchor,first+1)
    source=source[:second]+function(source[second:],anchor,'')
    source=function(source,'esp_err_t usb_host_lib_set_auto_suspend(usb_host_lib_auto_suspend_tmr_t timer_type, size_t timer_interval_ms)',
        '''esp_err_t usb_host_lib_set_auto_suspend(usb_host_lib_auto_suspend_tmr_t timer_type, size_t timer_interval_ms)
{
    (void)timer_type; (void)timer_interval_ms;
    return ESP_ERR_NOT_SUPPORTED;
}''')
    if any(x in source for x in ('xTimerCreate(', 'xTimerDelete(', 'xTimerReset(', 'xTimerStop(', 'xTimerIsTimerActive(', 'xTimerChangePeriod(', 'vTimerSetReloadMode(','auto_suspend_timer;', 'auto_suspend_timer)', 'TimerHandle_t')):
        raise ValueError('native USB timer ownership remains')
    return adapt_host(source)
