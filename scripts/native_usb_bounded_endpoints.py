"""Fail-closed bounded endpoint adaptations for the pinned private esp-usb source.

Call adapt(name, source) before the existing native lifecycle transformations.
This does not change the public host APIs or replace normal daemon processing.
"""
from pathlib import Path

DRIVER = Path(__file__).resolve().parents[1] / 'Drivers' / 'usb_controller_esp32s3'


def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('bounded endpoint source anchor changed: ' + old)
    return source.replace(old, new, 1)


def adapt(name, source):
    if name in ('hcd_dwc.c', 'usbh.c') and (
            'risc_halt_pending' in source or 'NativeBoundedEndpoint.h' in source):
        raise ValueError('bounded endpoint source already adapted')
    if name == 'usbh.c':
        source = once(source, '#include "hcd.h"',
                      '#include "hcd.h"\n#include "NativeBoundedEndpoint.h"')
        # The last insertion point follows both handle_ep0_dequeue and all of
        # the endpoint object lookup helpers used by our private entrypoints.
        return once(source, 'esp_err_t usbh_ep_command(usbh_ep_handle_t ep_hdl, usbh_ep_cmd_t command)\n{',
                    (DRIVER / 'NativeBoundedEndpointUsbh.inc').read_text() +
                    '\nesp_err_t usbh_ep_command(usbh_ep_handle_t ep_hdl, usbh_ep_cmd_t command)\n{')
    if name != 'hcd_dwc.c':
        return source
    source = once(source, '            uint32_t reserved29: 29;',
                  '            uint32_t risc_halt_pending: 1;\n'
                  '            uint32_t risc_halt_invalidated: 1;\n'
                  '            uint32_t reserved27: 27;')
    source = once(source, '// ----------------------------------------------- Interrupt Handling --------------------------------------------------',
                  (DRIVER / 'NativeBoundedEndpointHcd.inc').read_text() +
                  '\n// ----------------------------------------------- Interrupt Handling --------------------------------------------------')
    # The stale channel interrupt is consumed, but may not parse a DMA buffer
    # which the documented port-invalidation path has already retired.
    source = once(source, '    usb_dwc_hal_chan_event_t chan_event = usb_dwc_hal_chan_decode_intr(chan_obj);',
                  '    usb_dwc_hal_chan_event_t chan_event = usb_dwc_hal_chan_decode_intr(chan_obj);\n'
                  '    if (pipe->cs_flags.risc_halt_invalidated) return HCD_PIPE_EVENT_NONE;')
    # HAL errors take precedence over HALT_REQ and leave halt_requested set.
    # They still prove the channel halted, so acknowledge private cancellation
    # and notify only if a legacy synchronous HALT has joined the request.
    source = once(source, '        pipe->state = HCD_PIPE_STATE_HALTED;\n        // Mark the buffer as done with an error',
                  '        pipe->state = HCD_PIPE_STATE_HALTED;\n'
                  '        if (pipe->cs_flags.risc_halt_pending) {\n'
                  '            pipe->cs_flags.risc_halt_pending = 0;\n'
                  '            pipe->cs_flags.waiting_halt = 0;\n'
                  '            chan_obj->flags.halt_requested = 0;\n'
                  '            if (pipe->task_waiting_pipe_notif)\n'
                  '                *yield |= _internal_pipe_event_notify(pipe, true);\n'
                  '        }\n        // Mark the buffer as done with an error')
    source = once(source, '        // Notify the task waiting for the pipe halt\n        *yield |= _internal_pipe_event_notify(pipe, true);',
                  '        // Private polling has no waiter; a legacy HALT may join it.\n'
                  '        if (pipe->cs_flags.risc_halt_pending) {\n'
                  '            pipe->cs_flags.risc_halt_pending = 0;\n'
                  '            pipe->cs_flags.waiting_halt = 0;\n'
                  '            if (pipe->task_waiting_pipe_notif)\n'
                  '                *yield |= _internal_pipe_event_notify(pipe, true);\n'
                  '        } else {\n'
                  '            *yield |= _internal_pipe_event_notify(pipe, true);\n'
                  '        }')
    # Preserve the public synchronous HALT contract without issuing a second
    # channel request. A disconnected port cannot produce a halt IRQ.
    source = once(source, 'static esp_err_t _pipe_cmd_halt(pipe_t *pipe)\n{\n    esp_err_t ret;',
                  'static esp_err_t _pipe_cmd_halt(pipe_t *pipe)\n{\n    esp_err_t ret;\n'
                  '    if (pipe->cs_flags.risc_halt_pending) {\n'
                  '        if (pipe->port->flags.conn_dev_ena) {\n'
                  '            _internal_pipe_event_wait(pipe);\n'
                  '            assert(pipe->state == HCD_PIPE_STATE_HALTED);\n'
                  '        } else {\n'
                  '            (void)risc_hcd_halt_poll_locked(pipe);\n'
                  '        }\n'
                  '        return ESP_OK;\n'
                  '    }')
    source = once(source, 'static esp_err_t _pipe_cmd_flush(pipe_t *pipe)\n{\n    esp_err_t ret;',
                  'static esp_err_t _pipe_cmd_flush(pipe_t *pipe)\n{\n    esp_err_t ret;\n'
                  '    if (pipe->cs_flags.risc_halt_pending) return ESP_ERR_NOT_FINISHED;')
    source = once(source, 'static esp_err_t _pipe_cmd_clear(pipe_t *pipe)\n{\n    esp_err_t ret;',
                  'static esp_err_t _pipe_cmd_clear(pipe_t *pipe)\n{\n    esp_err_t ret;\n'
                  '    if (pipe->cs_flags.risc_halt_pending) return ESP_ERR_NOT_FINISHED;\n'
                  '    if (pipe->cs_flags.risc_halt_invalidated) return ESP_ERR_INVALID_STATE;')
    source = once(source, '    HCD_CHECK_FROM_CRIT(!pipe->multi_buffer_control.buffer_is_executing\n                        && !pipe->cs_flags.has_urb,',
                  '    HCD_CHECK_FROM_CRIT(!pipe->multi_buffer_control.buffer_is_executing\n'
                  '                        && !pipe->cs_flags.has_urb\n'
                  '                        && !pipe->cs_flags.risc_halt_pending\n'
                  '                        && !pipe->cs_flags.pipe_cmd_processing\n'
                  '                        && !pipe->task_waiting_pipe_notif,')
    # No new URB may enter an asynchronously halting or invalidated pipe.
    source = once(source, 'static inline bool _check_port_pipe_state(pipe_t *pipe, bool *submit_urb)\n{',
                  'static inline bool _check_port_pipe_state(pipe_t *pipe, bool *submit_urb)\n{\n'
                  '    if (pipe->cs_flags.risc_halt_pending || pipe->cs_flags.risc_halt_invalidated)\n'
                  '        return false;')
    return source
