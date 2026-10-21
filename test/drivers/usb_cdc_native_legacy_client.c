/* Compile this translation unit ONLY against baseline a7ae399 SDK headers. */
#include "RiscUsbProviderV1.h"
#include <assert.h>
#include <string.h>
void legacy_client(const risc_driver_v2 *driver) {
    assert(driver->struct_size >= sizeof(*driver));
    assert(!strcmp(driver->driver_id,"usb-cdc-acm-v2"));
    const risc_usb_cdc_api_v1 *api = driver->capability;
    assert(api->api_version == 1 && api->struct_size >= sizeof(*api));
    uint64_t token=api->open(17); assert(token);
    assert(api->configure(token,115200,8,0,1));
    assert(api->control_lines(token,true,true));
    uint8_t data[2]; assert(api->read(token,data,2,15)==2);
    assert(api->write(token,data,2,20)==2);
    assert(api->close(token)); assert(!api->close(token));
}
