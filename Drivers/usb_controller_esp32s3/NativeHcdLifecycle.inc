// Included in pinned hcd_dwc.c. One S3 port, single serialized owner executor.
static port_t *risc_pending_port;
static bool risc_pending_hal;

esp_err_t risc_usb_hcd_retry_cleanup(void)
{
    port_t *port = risc_pending_port;
    if (!port) return ESP_OK;
    // Keep the entire callback context, HAL and port slot alive on failure.
    if (port->isr_hdl) {
        esp_err_t rc = risc_usb_intr_disable(port->isr_hdl);
        if (rc != ESP_OK) return rc;
        rc = risc_usb_intr_free(port->isr_hdl);
        if (rc != ESP_OK) return rc;
        port->isr_hdl = NULL;
    }
    if (risc_pending_hal) {
        free(port->hal->channels.hdls);
        port->hal->channels.hdls = NULL;
        usb_dwc_hal_deinit(port->hal);
    }
    const int index = port->periph_idx;
    port_obj_free(port);
    risc_pending_port = NULL;
    risc_pending_hal = false;
    HCD_ENTER_CRITICAL();
    s_port_inited[index] = false;
    HCD_EXIT_CRITICAL();
    return ESP_OK;
}

esp_err_t hcd_port_deinit(hcd_port_handle_t port_hdl)
{
    port_t *port = (port_t *)port_hdl;
    HCD_CHECK(port != NULL, ESP_ERR_INVALID_ARG);
    HCD_ENTER_CRITICAL();
    HCD_CHECK_FROM_CRIT(s_port_inited[port->periph_idx]
                        && port->num_pipes_idle == 0 && port->num_pipes_queued == 0
                        && (port->state == HCD_PORT_STATE_NOT_POWERED || port->state == HCD_PORT_STATE_RECOVERY)
                        && port->task_waiting_port_notif == NULL,
                        ESP_ERR_INVALID_STATE);
    HCD_EXIT_CRITICAL();
    esp_err_t rc = risc_usb_intr_disable(port->isr_hdl);
    if (rc != ESP_OK) return rc;
    rc = risc_usb_intr_free(port->isr_hdl);
    if (rc != ESP_OK) return rc;
    HCD_ENTER_CRITICAL();
    s_port_inited[port->periph_idx] = false;
    HCD_EXIT_CRITICAL();
    free(port->hal->channels.hdls);
    usb_dwc_hal_deinit(port->hal);
    port_obj_free(port);
    return ESP_OK;
}
