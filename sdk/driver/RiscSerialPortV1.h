#pragma once
/* Generic semantic serial.port@1 provider ABI. Hardware-specific protocols,
 * host enumeration and transport identity belong to the installed provider.
 * The runtime can inspect this interface without including USB headers. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_SERIAL_PORT_API_V1 1u
#define RISC_SERIAL_INVENTORY_MAX_DEVICES 8u

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t (*open)(uint64_t provider_device);
    bool (*configure)(uint64_t session, uint32_t baud, uint8_t data_bits,
                      uint8_t parity, uint8_t stop_bits);
    bool (*control_lines)(uint64_t session, bool dtr, bool rts);
    int32_t (*read)(uint64_t session, uint8_t *dst, size_t capacity,
                    uint32_t timeout_ms);
    int32_t (*write)(uint64_t session, const uint8_t *src, size_t length,
                     uint32_t timeout_ms);
    bool (*close)(uint64_t session);
} risc_serial_port_api_v1;

/* Optional append-only read-only probe. 1=match, 0=definite nonmatch,
 * negative=uncertain; an uncertain result MUST NOT be treated as detach. */
typedef struct {
    risc_serial_port_api_v1 serial;
    int32_t (*probe)(uint64_t provider_device);
} risc_serial_port_discovery_v1;

/* Transport values match the versioned generic device-registry vocabulary.
 * A protocol ELF, not the framework, identifies its actual transport. */
enum {
    RISC_SERIAL_TRANSPORT_INTERNAL = 0,
    RISC_SERIAL_TRANSPORT_UART = 1,
    RISC_SERIAL_TRANSPORT_USB = 2,
    RISC_SERIAL_TRANSPORT_BLE = 3,
    RISC_SERIAL_TRANSPORT_I2C = 4,
    RISC_SERIAL_TRANSPORT_SPI = 5,
    RISC_SERIAL_TRANSPORT_GPIO = 6,
    RISC_SERIAL_TRANSPORT_LORA = 7,
    RISC_SERIAL_TRANSPORT_IP = 8
};

typedef struct {
    /* Both tokens are provider-owned, nonzero and stable while connected.
     * generation changes on each physical reconnect, even at a reused port.
     * Never publish these raw tokens as application device handles. */
    uint64_t provider_device;
    uint64_t generation;
    uint8_t transport;
    uint8_t reserved[7];
} risc_serial_device_v1;

/* Provider-originated bounded inventory; no direct host/device work in core.
 * On success, *inout_count is the exact number of complete records written.
 * Insufficient capacity returns false and required count, WITHOUT writing a
 * partial inventory. Failed enumeration/identity is UNKNOWN: the runtime must
 * neither publish a healthy empty set nor silently revoke existing leases.
 * Implementations must perform no claims, controls or data transfers here.
 * The provider grant must remain pinned while any announced device is live. */
typedef struct {
    risc_serial_port_discovery_v1 discovery;
    bool (*snapshot)(risc_serial_device_v1 *out, size_t *inout_count);
} risc_serial_port_inventory_v1;
/* Optional provider-owned stream endpoints for an already-open session.
 * The provider owns publication, bounded physical pumping, and close. A
 * false return fails acquisition; core must not fall back to raw read/write.
 * Consumer grants are issued separately against the exact capability lease. */
typedef struct {
    risc_serial_port_inventory_v1 inventory;
    bool (*endpoints)(uint64_t session, uint32_t *rx, uint32_t *tx);
} risc_serial_port_streams_v1;
#ifdef __cplusplus
}
#endif
