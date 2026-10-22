"""Pinned-source clones for no-wait provider cleanup; never alter public IDF ABI."""
def extract(source, signature):
    if source.count(signature) != 1: raise ValueError('bounded host function anchor changed: '+signature)
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == '{': depth += 1
        elif source[end] == '}': depth -= 1
        end += 1
    return source[start:end]


def once(s, old, new):
    if s.count(old) != 1: raise ValueError('bounded host anchor changed: ' + old)
    return s.replace(old, new, 1)


def adapt_host(source):
    definitions = []
    for api in ('interface_claim', 'interface_release', 'device_close'):
        f = extract(source, 'esp_err_t usb_host_' + api + '(')
        f = once(f, 'usb_host_' + api + '(', 'risc_usb_' + api + '_poll(')
        fail = ''
        if api != 'device_close':
            fail = 'HOST_ENTER_CRITICAL(); client_obj->dynamic.flags.taking_mux = 0; HOST_EXIT_CRITICAL(); '
        f = once(f, 'xSemaphoreTake(p_host_lib_obj->constant.mux_lock, portMAX_DELAY);',
            'if (xSemaphoreTake(p_host_lib_obj->constant.mux_lock, 0) != pdTRUE) { ' + fail + 'return ESP_ERR_NOT_FINISHED; }')
        if api == 'interface_release':
            f = once(f, '        vTaskDelay(10);', '''        xSemaphoreGive(p_host_lib_obj->constant.mux_lock);
        HOST_ENTER_CRITICAL();
        client_obj->dynamic.flags.taking_mux = 0;
        HOST_EXIT_CRITICAL();
        return ESP_ERR_NOT_FINISHED;''')
        if api == 'device_close':
            f = once(f, '    _clear_client_opened_device(client_obj, dev_addr);\n    HOST_EXIT_CRITICAL();\n\n    ESP_ERROR_CHECK(usbh_dev_close(dev_hdl));',
                '    HOST_EXIT_CRITICAL();\n    ret = usbh_dev_close(dev_hdl);\n    if (ret != ESP_OK) goto exit;\n    HOST_ENTER_CRITICAL();\n    _clear_client_opened_device(client_obj, dev_addr);\n    HOST_EXIT_CRITICAL();')
        definitions.append(f)
    f = extract(source, 'esp_err_t usb_host_client_handle_events(')
    f = once(f, 'usb_host_client_handle_events(', 'risc_usb_client_transfer_events(')
    # The provider callbacks only enqueue events/mark completion; they never
    # recursively submit. One pass bounds work even if an IRQ re-signals it.
    f = once(f, '    while (1) {', '    do {')
    f = once(f, '        timeout_ticks = 0;\n    }', '        timeout_ticks = 0;\n    } while (0);')
    definitions.append(f)
    return source + '\n\n' + '\n\n'.join(definitions) + '\n'
