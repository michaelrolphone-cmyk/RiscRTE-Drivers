// Included in pinned usb_host.c. This provider exposes neither autosuspend nor
// external hubs. Stage ownership survives any failed install/uninstall boundary.
static host_lib_t *risc_host;
static bool risc_usbh_owned, risc_enum_owned, risc_hub_owned;
static bool risc_cleanup;

bool risc_usb_cleanup_pending(void) { return risc_cleanup; }

esp_err_t risc_usb_retry_cleanup(void)
{
    if (!risc_cleanup) return ESP_OK;
    esp_err_t rc;
    if (risc_hub_owned) {
        rc = hub_root_stop();
        if (rc != ESP_OK) return rc;
        rc = hub_uninstall();
        if (rc != ESP_OK) return rc;
        risc_hub_owned = false;
    }
    rc = risc_usb_hub_retry_cleanup();
    if (rc != ESP_OK) return rc;
    // Also covers an IRQ acquired by a failed install before any normal host
    // handle was published. Never let provider quiesce report unload-safe then.
    if (risc_usb_irq_live()) return ESP_ERR_INVALID_STATE;
    if (risc_enum_owned) {
        rc = enum_uninstall();
        if (rc != ESP_OK) return rc;
        risc_enum_owned = false;
    }
    if (risc_usbh_owned) {
        rc = usbh_uninstall();
        if (rc != ESP_OK) return rc;
        risc_usbh_owned = false;
    }
    HOST_ENTER_CRITICAL();
    if (p_host_lib_obj == risc_host) p_host_lib_obj = NULL;
    HOST_EXIT_CRITICAL();
    if (risc_host) {
        if (risc_host->constant.mux_lock) vSemaphoreDelete(risc_host->constant.mux_lock);
        if (risc_host->constant.event_sem) vSemaphoreDelete(risc_host->constant.event_sem);
        heap_caps_free(risc_host);
    }
    risc_host = NULL;
    risc_cleanup = false;
    return ESP_OK;
}

esp_err_t usb_host_install(const usb_host_config_t *config)
{
    HOST_CHECK(config != NULL, ESP_ERR_INVALID_ARG);
    HOST_CHECK(!risc_cleanup && !risc_host && !p_host_lib_obj, ESP_ERR_INVALID_STATE);
    // This private closure supports exactly the configuration our provider uses.
    HOST_CHECK(config->skip_phy_setup && config->peripheral_map == 1 &&
               !config->root_port_unpowered && !config->fsls_only &&
               !config->fifo_settings_custom.rx_fifo_lines &&
               !config->fifo_settings_custom.nptx_fifo_lines &&
               !config->fifo_settings_custom.ptx_fifo_lines, ESP_ERR_NOT_SUPPORTED);
    risc_host = heap_caps_calloc(1, sizeof(host_lib_t), MALLOC_CAP_DEFAULT);
    if (!risc_host) return ESP_ERR_NO_MEM;
    risc_host->constant.event_sem = xSemaphoreCreateBinary();
    risc_host->constant.mux_lock = xSemaphoreCreateMutex();
    esp_err_t rc = ESP_ERR_NO_MEM;
    if (!risc_host->constant.event_sem || !risc_host->constant.mux_lock) goto failed;
    TAILQ_INIT(&risc_host->mux_protected.client_tailq);
    const usbh_config_t usbh_config = {
        .proc_req_cb = proc_req_callback, .proc_req_cb_arg = NULL,
        .event_cb = usbh_event_callback, .event_cb_arg = NULL,
    };
    rc = usbh_install(&usbh_config);
    if (rc != ESP_OK) goto failed;
    risc_usbh_owned = true;
    enum_config_t enum_config = {
        .proc_req_cb = proc_req_callback, .proc_req_cb_arg = NULL,
        .enum_event_cb = enum_event_callback, .enum_event_cb_arg = NULL,
    };
    rc = enum_install(&enum_config, &risc_host->constant.enum_client);
    if (rc != ESP_OK) goto failed;
    risc_enum_owned = true;
    hub_config_t hub_config = {
        .port_map = 1, .proc_req_cb = proc_req_callback, .proc_req_cb_arg = NULL,
        .event_cb = hub_event_callback, .event_cb_arg = NULL,
        .intr_flags = config->intr_flags, .fifo_config = NULL, .fsls_only = false,
    };
    rc = hub_install(&hub_config, &risc_host->constant.hub_client);
    if (rc != ESP_OK) goto failed;
    risc_hub_owned = true;
    HOST_ENTER_CRITICAL();
    p_host_lib_obj = risc_host;
    HOST_EXIT_CRITICAL();
    rc = hub_root_start();
    if (rc != ESP_OK) goto failed;
    return ESP_OK;
failed:
    risc_cleanup = true;
    // Return the original install error; retain cleanup status independently.
    // The provider's quiesce observes the latch even though installed is false.
    (void)risc_usb_retry_cleanup();
    return rc;
}

esp_err_t usb_host_uninstall(void)
{
    if (risc_cleanup) return risc_usb_retry_cleanup();
    HOST_ENTER_CRITICAL();
    HOST_CHECK_FROM_CRIT(p_host_lib_obj != NULL, ESP_ERR_INVALID_STATE);
    HOST_CHECK_FROM_CRIT(p_host_lib_obj->dynamic.process_pending_flags == 0 &&
                        p_host_lib_obj->dynamic.lib_event_flags == 0 &&
                        p_host_lib_obj->dynamic.flags.val == 0, ESP_ERR_INVALID_STATE);
    HOST_EXIT_CRITICAL();
    risc_cleanup = true;
    return risc_usb_retry_cleanup();
}
