#pragma once

/* Included in the controller's private namespace, after its state and
 * callbacks. Configure the host PHY/pull-downs and event client BEFORE
 * supplying VBUS. RiscRTE boots with USB Serial/JTAG on the internal PHY;
 * powering a receiver first lets it boot against the previous device role.
 * Holding the host's receive detector disconnected cannot correct what the
 * receiver already saw. No periodic resets or power cycling are needed. */
bool start_host_controller() {
    capture_phy_route();
    usb_phy_config_t phyConfig = {};
    phyConfig.controller = USB_PHY_CTRL_OTG;
    phyConfig.target = USB_PHY_TARGET_INT;
    phyConfig.otg_mode = USB_OTG_MODE_HOST;
    phyConfig.otg_speed = USB_PHY_SPEED_UNDEFINED;
    esp_err_t rc = usb_new_phy(&phyConfig, &phy);
    if (rc != ESP_OK) return start_failure("phy-create", rc);
    rc = usb_phy_action(phy, USB_PHY_ACTION_HOST_FORCE_DISCONN);
    if (rc != ESP_OK) return start_failure("phy-hold-disconnected", rc);
    std::printf("USBCTRL stage=phy-held-disconnected\n");

    usb_host_config_t config = {};
    // The ELF owns the PHY across host install/uninstall, including failures.
    config.skip_phy_setup = true;
    // IDF's C-callable non-shared default permits levels 1, 2 and 3.
    // Do not share the USB source with another owner to bypass contention.
    config.intr_flags = ESP_INTR_FLAG_LOWMED;
    rc = usb_host_install(&config);
    if (rc != ESP_OK) {
        start_failure(rc == ESP_ERR_NOT_FOUND
                          ? "usb-host-install/IRQ-unavailable" : "usb-host-install", rc);
        startupError.number(" flags=0x", static_cast<uint32_t>(config.intr_flags), 16);
        return false;
    }
    installed = true;
    std::printf("USBCTRL stage=usb-host-installed\n");
    usb_host_client_config_t registration = {};
    registration.is_synchronous = false;
    registration.max_num_event_msg = kEvents;
    registration.async.client_event_callback = client_event;
    registration.async.callback_arg = nullptr;
    rc = usb_host_client_register(&registration, &client);
    if (rc != ESP_OK) return start_failure("client-register", rc);
    std::printf("USBCTRL stage=client-registered\n");
    rc = usb_host_transfer_alloc(kBuffer, 0, &transfer);
    if (rc != ESP_OK) return start_failure("transfer-alloc", rc);

    // Settle the role/pull-down handoff with the receiver still unpowered.
    const TickType_t ticks = pdMS_TO_TICKS(20);
    vTaskDelay(ticks ? ticks : 1);
    const bool acquired = power->acquire_host(power->context, 500, &powerLease);
    if (!acquired || !powerLease) {
        startupError.text("vbus-acquire");
        startupError.number(" returned=", acquired);
        startupError.number(" lease=", powerLease != 0);
        startupError.text(" requested-ma=500");
        std::printf("USBCTRL start-failed stage=vbus-acquire rc=0 lease=%u\n",
                    powerLease ? 1u : 0u);
        // start() quiesces the partially installed host and then releases
        // any retained power lease. Never release VBUS under live DMA/IRQs.
        return false;
    }
    std::printf("USBCTRL stage=vbus-acquired-after-host-ready\n");
    rc = usb_phy_action(phy, USB_PHY_ACTION_HOST_ALLOW_CONN);
    if (rc != ESP_OK) return start_failure("phy-allow-connection", rc);
    std::printf("USBCTRL stage=phy-connection-enabled\n");
    return true;
}

bool release_host_phy() {
    if (!phy) return true;
    // Never remove the PHY while the host or DMA can still use it.
    if (installed || client || transfer) return false;
    const esp_err_t rc = usb_del_phy(phy);
    if (rc != ESP_OK) {
        std::printf("USBCTRL cleanup-failed stage=phy-delete rc=%d\n", static_cast<int>(rc));
        return false; // Retain ownership and VBUS so teardown can be retried.
    }
    phy = nullptr;
    std::printf("USBCTRL stage=phy-released\n");
    return true;
}
