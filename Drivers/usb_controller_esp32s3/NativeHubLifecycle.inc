// Included in pinned hub.c. Do not clear the parent pointer before child IRQ
// release succeeds. Failed installation also retains the IRQ callback argument.
static hub_driver_t *risc_pending_hub;

esp_err_t risc_usb_hub_retry_cleanup(void)
{
    esp_err_t rc = risc_usb_hcd_retry_cleanup();
    if (rc != ESP_OK) return rc;
    hub_driver_t *hub = risc_pending_hub;
    if (!hub) return ESP_OK;
    for (int i = 0; i < HCD_NUM_PORTS; ++i) {
        if (!hub->root_hub_ports[i].constant.hdl) continue;
        rc = hcd_port_deinit(hub->root_hub_ports[i].constant.hdl);
        if (rc != ESP_OK) return rc;
        hub->root_hub_ports[i].constant.hdl = NULL;
    }
    heap_caps_free(hub);
    risc_pending_hub = NULL;
    return ESP_OK;
}

esp_err_t hub_uninstall(void)
{
    HUB_DRIVER_ENTER_CRITICAL();
    HUB_DRIVER_CHECK_FROM_CRIT(p_hub_driver_obj != NULL, ESP_ERR_INVALID_STATE);
    hub_driver_t *hub = p_hub_driver_obj;
    for (int i = 0; i < HCD_NUM_PORTS; ++i) {
        if (!hub->root_hub_ports[i].constant.hdl) continue;
        HUB_DRIVER_CHECK_FROM_CRIT(hub->root_hub_ports[i].dynamic.state == ROOT_PORT_STATE_NOT_POWERED,
                                  ESP_ERR_INVALID_STATE);
    }
    HUB_DRIVER_EXIT_CRITICAL();
    for (int i = 0; i < HCD_NUM_PORTS; ++i) {
        if (!hub->root_hub_ports[i].constant.hdl) continue;
        esp_err_t rc = hcd_port_deinit(hub->root_hub_ports[i].constant.hdl);
        if (rc != ESP_OK) return rc;
        hub->root_hub_ports[i].constant.hdl = NULL;
    }
    HUB_DRIVER_ENTER_CRITICAL();
    p_hub_driver_obj = NULL;
    HUB_DRIVER_EXIT_CRITICAL();
    heap_caps_free(hub);
    return ESP_OK;
}

esp_err_t hub_root_stop(void)
{
    HUB_DRIVER_CHECK(p_hub_driver_obj != NULL, ESP_ERR_INVALID_STATE);
    for (int i = 0; i < HCD_NUM_PORTS; ++i) {
        root_hub_port_t *port = &p_hub_driver_obj->root_hub_ports[i];
        if (!port->constant.hdl || port->dynamic.state == ROOT_PORT_STATE_NOT_POWERED) continue;
        const esp_err_t rc = hcd_port_command(port->constant.hdl, HCD_PORT_CMD_POWER_OFF);
        if (rc != ESP_OK) return rc;
        HUB_DRIVER_ENTER_CRITICAL();
        port->dynamic.state = ROOT_PORT_STATE_NOT_POWERED;
        HUB_DRIVER_EXIT_CRITICAL();
    }
    return ESP_OK;
}
