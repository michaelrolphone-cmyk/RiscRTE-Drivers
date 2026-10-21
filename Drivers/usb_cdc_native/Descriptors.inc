/* Exact CDC parser reused from native Reader c98e4222048244b1e729869ed47aa99130025fd3. */
static bool parse(size_t length, cdc_session *out) {
    if (!out || length < 9 || length > RISC_USB_CONFIG_LIMIT ||
        config_bytes[0] != 9 || config_bytes[1] != 2) return false;
    size_t total = (size_t)config_bytes[2] | ((size_t)config_bytes[3] << 8);
    if (total < 9 || total != length) return false;
    uint8_t control = 0xff, only_data = 0xff;
    uint8_t control_count = 0, data_count = 0;
    for (size_t at = 0; at < total;) {
        if (total - at < 2) return false;
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (n < 2 || n > total - at) return false;
        if (type == 4) {
            if (n < 9) return false;
            uint8_t iface = config_bytes[at + 2];
            uint8_t alt = config_bytes[at + 3];
            uint8_t cls = config_bytes[at + 5];
            uint8_t subcls = config_bytes[at + 6];
            if (alt == 0 && cls == 2 && subcls == 2) {
                if (++control_count != 1) return false;
                control = iface;
            } else if (alt == 0 && cls == 10) {
                if (++data_count == 0) return false;
                only_data = iface;
            }
        }
        at += n;
    }
    if (control_count != 1 || !data_count) return false;
    uint8_t data = 0xff;
    bool has_union = false, has_iad = false;
    uint8_t iad_first = 0, iad_end = 0;
    for (size_t at = 0; at < total;) {
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (type == 0x24 && n >= 4 && config_bytes[at + 2] == 6 &&
            config_bytes[at + 3] == control) {
            if (has_union || n != 5) return false;
            has_union = true;
            data = config_bytes[at + 4];
        } else if (type == 11) {
            if (n < 8) return false;
            uint8_t first = config_bytes[at + 2];
            uint8_t count = config_bytes[at + 3];
            if (!count || (unsigned)first + count > 256u) return false;
            if (config_bytes[at + 4] == 2 && config_bytes[at + 5] == 2 &&
                control >= first && (unsigned)control < (unsigned)first + count) {
                if (has_iad) return false;
                has_iad = true;
                iad_first = first;
                iad_end = (uint8_t)((unsigned)first + count - 1u);
            }
        }
        at += n;
    }
    if (!has_union) {
        if (data_count != 1) return false;
        data = only_data;
    }
    if (data == control || data == 0xff ||
        (has_iad && (data < iad_first || data > iad_end))) return false;
    bool found_data = false;
    for (size_t at = 0; at < total;) {
        uint8_t n = config_bytes[at];
        if (config_bytes[at + 1] == 4 && config_bytes[at + 2] == data &&
            config_bytes[at + 3] == 0 && config_bytes[at + 5] == 10)
            found_data = true;
        at += n;
    }
    if (!found_data) return false;
    uint8_t iface = 0xff, alt = 0xff, cls = 0xff;
    uint8_t in = 0, out_ep = 0, chosen_alt = 0;
    uint8_t chosen_in = 0, chosen_out = 0;
    bool chosen = false, duplicate_endpoint = false;
    for (size_t at = 0; at < total;) {
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (type == 4) {
            if (iface == data && cls == 10) {
                if (duplicate_endpoint) return false;
                if (in && out_ep) {
                    if (chosen) return false;
                    chosen = true;
                    chosen_alt = alt;
                    chosen_in = in;
                    chosen_out = out_ep;
                }
            }
            iface = config_bytes[at + 2];
            alt = config_bytes[at + 3];
            cls = config_bytes[at + 5];
            in = out_ep = 0;
            duplicate_endpoint = false;
        } else if (type == 5 && iface == data && cls == 10) {
            if (n < 7) return false;
            uint8_t ep = config_bytes[at + 2];
            uint16_t mps = (uint16_t)config_bytes[at + 4] |
                           ((uint16_t)config_bytes[at + 5] << 8);
            if ((config_bytes[at + 3] & 3u) == 2 && mps && mps <= 512) {
                if ((ep & 0x0fu) == 0 || (ep & 0x70u)) return false;
                if (ep & 0x80u) {
                    if (in) duplicate_endpoint = true;
                    else in = ep;
                } else {
                    if (out_ep) duplicate_endpoint = true;
                    else out_ep = ep;
                }
            }
        }
        at += n;
    }
    if (iface == data && cls == 10) {
        if (duplicate_endpoint) return false;
        if (in && out_ep) {
            if (chosen) return false;
            chosen = true;
            chosen_alt = alt;
            chosen_in = in;
            chosen_out = out_ep;
        }
    }
    if (!chosen) return false;
    out->control_interface = control;
    out->data_interface = data;
    out->data_alternate = chosen_alt;
    out->ep_in = chosen_in;
    out->ep_out = chosen_out;
    return true;
}
