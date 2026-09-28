/* Included inside the controller namespace after its IDF/owner-task helpers.
 * Each endpoint owns DMA until its callback; a caller deadline never cancels
 * a quiet interrupt IN request. No app memory is retained by a transfer. */
struct PendingInterrupt {
    usb_transfer_t *dma = nullptr;
    uint64_t claim_id = 0;
    bool pending = false;
    bool ready = false;
};
static PendingInterrupt interrupts[RISC_USB_HOST_MAX_CLAIMS];

void interrupt_complete(usb_transfer_t *dma) {
    auto *slot = static_cast<PendingInterrupt *>(dma->context);
    slot->pending = false;
    slot->ready = true;
}

bool drain_interrupt(PendingInterrupt &slot) {
    if (!slot.dma) return true;
    if (slot.pending) {
        const esp_err_t halt = usb_host_endpoint_halt(slot.dma->device_handle,
                                                     slot.dma->bEndpointAddress);
        const esp_err_t flush = usb_host_endpoint_flush(slot.dma->device_handle,
                                                       slot.dma->bEndpointAddress);
        if (halt != ESP_OK || flush != ESP_OK) {
            std::printf("USBCTRL interrupt-drain-failed ep=%02x halt=%d flush=%d\n",
                        unsigned(slot.dma->bEndpointAddress), int(halt), int(flush));
            return false;
        }
        const TickType_t begun = xTaskGetTickCount();
        while (slot.pending) {
            if (!pump(1) || static_cast<TickType_t>(xTaskGetTickCount() - begun) >=
                                  pdMS_TO_TICKS(kTeardownTicks)) return false;
        }
    }
    if (usb_host_transfer_free(slot.dma) != ESP_OK) return false;
    slot = {};
    return true;
}

int32_t read_interrupt(uint64_t id, usb_device_handle_t handle, uint8_t endpoint,
                       uint16_t packet, uint8_t *dst, uint32_t timeout) {
    (void)timeout; // Caller deadline is an upper bound, never a forced wait.
    PendingInterrupt *slot = nullptr;
    for (auto &candidate : interrupts) {
        if (candidate.claim_id == id && candidate.dma &&
            candidate.dma->bEndpointAddress == endpoint) { slot = &candidate; break; }
    }
    if (!slot) {
        for (auto &candidate : interrupts) if (!candidate.dma) { slot = &candidate; break; }
        if (!slot || usb_host_transfer_alloc(packet, 0, &slot->dma) != ESP_OK) return -1;
        slot->claim_id = id;
        slot->dma->device_handle = handle;
        slot->dma->bEndpointAddress = endpoint;
        slot->dma->num_bytes = packet;
        slot->dma->callback = interrupt_complete;
        slot->dma->context = slot;
    }
    if (!slot->pending && !slot->ready) {
        slot->pending = true;
        if (usb_host_transfer_submit(slot->dma) != ESP_OK) {
            slot->pending = false;
            return -1;
        }
    }
    // Class poll() drains reports already completed; it must not accumulate
    // one blocking wait per requested report or per quiet endpoint. The app's
    // owner loop supplies scheduler cooperation between bounded poll calls.
    if (!slot->ready && !pump(0)) return -1;
    if (!slot->ready) return 0; // IDF retains the armed request across polls.
    slot->ready = false;
    const int32_t n = slot->dma->actual_num_bytes;
    // Match the standalone host: a STALL halts the endpoint until explicitly
    // cleared. The completed callback has returned DMA ownership, so the next
    // bounded read can safely resubmit after recovery.
    if (slot->dma->status == USB_TRANSFER_STATUS_STALL) {
        (void)usb_host_endpoint_clear(handle, endpoint);
        return -1;
    }
    if (slot->dma->status != USB_TRANSFER_STATUS_COMPLETED || n < 0 || n > packet)
        return -1;
    if (n) std::memcpy(dst, slot->dma->data_buffer, static_cast<size_t>(n));
    // Keep an IN request armed while the caller processes the report or sleeps.
    // Only DMA owned by this controller is retained; dst has already been copied.
    // The next callback holds one completed packet until the next bounded read.
    slot->pending = true;
    if (usb_host_transfer_submit(slot->dma) != ESP_OK) {
        slot->pending = false;
        return -1;
    }
    return n;
}
