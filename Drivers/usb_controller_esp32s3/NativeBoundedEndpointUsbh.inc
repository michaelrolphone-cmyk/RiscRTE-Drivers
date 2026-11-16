/* Included after pinned USBH endpoint helpers and EP0 completion handler.
 * No SDK object escapes the provider's existing serialized executor. */
extern esp_err_t risc_usb_hcd_halt_poll(hcd_pipe_handle_t handle);
extern esp_err_t risc_usb_hcd_endpoint_command(hcd_pipe_handle_t handle, hcd_pipe_cmd_t command);

static esp_err_t risc_usb_endpoint_operation(usb_device_handle_t handle,
                                            uint8_t endpoint, int operation)
{
    if (!p_usbh_obj || !handle || (endpoint & 0x70) || endpoint == 0x80)
        return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(p_usbh_obj->constant.mux_lock, 0) != pdTRUE)
        return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    hcd_pipe_handle_t pipe = NULL;
    esp_err_t rc = ESP_ERR_NOT_FOUND;
    /* Enumeration exclusively owns EP0. Do not cancel its transfer or advance
     * its state machine through a provider control-transfer cleanup call. */
    if (!endpoint) {
        if (dev->dynamic.flags.enum_lock) {
            rc = ESP_ERR_INVALID_STATE;
        } else {
            pipe = dev->constant.default_pipe;
        }
    } else {
        endpoint_t *ep = get_ep_from_addr(dev, endpoint);
        if (ep) pipe = ep->constant.pipe_hdl;
    }
    if (pipe) {
        rc = operation == 0 ? risc_usb_hcd_halt_poll(pipe) :
             risc_usb_hcd_endpoint_command(pipe, operation == 1 ? HCD_PIPE_CMD_FLUSH : HCD_PIPE_CMD_CLEAR);
    }
    xSemaphoreGive(p_usbh_obj->constant.mux_lock);
    return rc;
}

esp_err_t risc_usb_endpoint_halt_poll(usb_device_handle_t device, uint8_t endpoint)
{
    return risc_usb_endpoint_operation(device, endpoint, 0);
}

esp_err_t risc_usb_endpoint_flush(usb_device_handle_t device, uint8_t endpoint)
{
    return risc_usb_endpoint_operation(device, endpoint, 1);
}

esp_err_t risc_usb_endpoint_clear(usb_device_handle_t device, uint8_t endpoint)
{
    return risc_usb_endpoint_operation(device, endpoint, 2);
}

esp_err_t risc_usb_device_control_dequeue(usb_device_handle_t handle)
{
    if (!p_usbh_obj || !handle) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(p_usbh_obj->constant.mux_lock, 0) != pdTRUE)
        return ESP_ERR_NOT_FINISHED;
    device_t *dev = (device_t *)handle;
    esp_err_t rc = ESP_ERR_INVALID_STATE;
    if (!dev->dynamic.flags.enum_lock) {
        handle_ep0_dequeue(dev);
        rc = ESP_OK;
    }
    xSemaphoreGive(p_usbh_obj->constant.mux_lock);
    return rc;
}
