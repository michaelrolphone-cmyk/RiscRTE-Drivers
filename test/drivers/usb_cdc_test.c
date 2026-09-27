#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../Drivers/usb_cdc/driver.c"

static void valid_configuration(uint8_t config[41]) {
    static const uint8_t descriptor[41] = {
        9, 2, 41, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 0, 0x02, 0x02, 0x01, 0,
        9, 4, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
        7, 5, 0x81, 0x02, 64, 0, 0,
        7, 5, 0x02, 0x02, 64, 0, 0
    };
    memcpy(config, descriptor, sizeof(descriptor));
}

int main(void) {
    const t5_driver_v1 *d = t5_driver_get(T5_DRIVER_ABI_VERSION);
    assert(d);
    assert(!t5_driver_get(T5_DRIVER_ABI_VERSION + 1));
    assert(d->abi_version == T5_DRIVER_ABI_VERSION);
    assert(d->struct_size == sizeof(t5_driver_v1));
    assert(strcmp(d->driver_id, "usb-cdc-acm") == 0);
    assert(strcmp(d->capability_id, T5_USB_CDC_CLASS_CAPABILITY) == 0);
    assert(d->capability_api == T5_USB_CDC_CLASS_API_VERSION);
    assert(d->start(0));
    d->stop();

    const t5_usb_cdc_class_api_v1 *api =
        (const t5_usb_cdc_class_api_v1 *)d->capability;
    assert(api);
    assert(api->api_version == T5_USB_CDC_CLASS_API_VERSION);
    assert(api->struct_size == sizeof(t5_usb_cdc_class_api_v1));

    uint8_t config[41];
    valid_configuration(config);
    t5_usb_cdc_binding_v1 binding = {0};
    assert(api->probe(config, sizeof(config), 0x1234, 0x5678, &binding));
    assert(binding.control_interface == 0);
    assert(binding.data_interface == 1);
    assert(binding.data_alternate == 0);
    assert(binding.ep_in == 0x81);
    assert(binding.ep_out == 0x02);
    assert(binding.ep_in_mps == 64);
    assert(binding.ep_out_mps == 64);

    t5_usb_cdc_binding_v1 untouched = {9, 9, 9, 9, 9, 9, 9};
    t5_usb_cdc_binding_v1 before = untouched;
    config[2] = 42;
    assert(!api->probe(config, sizeof(config), 0, 0, &untouched));
    assert(memcmp(&untouched, &before, sizeof(untouched)) == 0);
    valid_configuration(config);

    config[27] = 0;
    assert(!api->probe(config, sizeof(config), 0, 0, &binding));
    valid_configuration(config);

    config[31] = 0;
    config[32] = 0;
    assert(!api->probe(config, sizeof(config), 0, 0, &binding));
    valid_configuration(config);

    uint8_t coding[7] = {0};
    assert(api->line_coding(115200, 8, 0, 1, coding));
    const uint8_t expected_115200[7] = {0x00, 0xc2, 0x01, 0x00, 0, 0, 8};
    assert(memcmp(coding, expected_115200, sizeof(coding)) == 0);

    assert(api->line_coding(3000000, 5, 4, 2, coding));
    assert(coding[4] == 2 && coding[5] == 4 && coding[6] == 5);
    assert(!api->line_coding(299, 8, 0, 1, coding));
    assert(!api->line_coding(3000001, 8, 0, 1, coding));
    assert(!api->line_coding(9600, 4, 0, 1, coding));
    assert(!api->line_coding(9600, 9, 0, 1, coding));
    assert(!api->line_coding(9600, 8, 5, 1, coding));
    assert(!api->line_coding(9600, 8, 0, 0, coding));
    assert(!api->line_coding(9600, 8, 0, 3, coding));
    assert(!api->line_coding(9600, 8, 0, 1, 0));

    assert(api->control_lines(false, false) == 0);
    assert(api->control_lines(true, false) == 1);
    assert(api->control_lines(false, true) == 2);
    assert(api->control_lines(true, true) == 3);

    puts("USB CDC ACM descriptor/protocol provider: PASS");
    return 0;
}
