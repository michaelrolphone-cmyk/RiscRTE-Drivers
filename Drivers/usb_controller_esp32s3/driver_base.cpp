/* Physical ESP32-S3 USB controller ELF. Hardware ownership stays in the ELF;
 * firmware supplies only transport-neutral privileged CPU/OS primitives.
 * Provider calls and IDF callbacks are serialized on one executor. */
#include "RiscUsbControllerV1.h"
#include "RiscUsbVbusV1.h"
#include "StartupDiagnostic.h"
#include "EnumerationDiagnostic.h"
#include "RoleSwitch.h"
#include <usb/usb_host.h>
#include <esp_intr_alloc.h>
#include <esp_private/usb_phy.h>
#include <soc/rtc_cntl_struct.h>
#include <soc/usb_dwc_struct.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdint>
#include <cstdio>

namespace {
#include "PhyRoute.h"
constexpr size_t kEvents = 16;
constexpr size_t kBuffer = RISC_USB_CONFIG_LIMIT + 8;
constexpr uint32_t kTeardownTicks = 500;
struct Device {
    usb_device_handle_t handle;
    uint64_t token;
    uint16_t vid, pid;
    bool attached;
};
struct Claim {
    uint64_t token, physical_device;
    uint8_t number, alternate;
};
struct Event {
    uint8_t kind, address;
    usb_device_handle_t handle;
};
static const risc_usb_vbus_api_v1 *power;
static const risc_usb_vbus_monitor_api_v1 *powerMonitor;
static UsbRoleSwitch role;
// The HID extension installs its DMA drain before activating this controller.
static bool (*drainRoleInterrupts)();
void service_role();
static uint64_t powerLease, serial;
static bool running, installed, fault;
static EnumerationDiagnostic enumerationDiagnostic;
static usb_phy_handle_t phy;
static StartupDiagnostic startupError;
static usb_host_client_handle_t client;
static usb_transfer_t *transfer;
static bool inFlight, completed;
static bool noClientsObserved;
static Event queue[kEvents];
static size_t queueHead, queueTail, queueCount;
static Device devices[RISC_USB_HOST_MAX_DEVICES];
static Claim claims[RISC_USB_HOST_MAX_CLAIMS];

bool equals(const char *a, const char *b) {
    return a && b && std::strcmp(a, b) == 0;
}
uint64_t token() {
    if (serial == UINT64_MAX) { fault = true; return 0; }
    return ++serial;
}
Device *device(uint64_t id) {
    if (!id || fault) return nullptr;
    for (auto &d : devices) if (d.handle && d.token == id) return &d;
    return nullptr;
}
Claim *claim(uint64_t id) {
    if (!id || fault) return nullptr;
    for (auto &c : claims) if (c.token == id) return &c;
    return nullptr;
}
bool claimed(uint64_t id) {
    for (const auto &c : claims) if (c.token && c.physical_device == id) return true;
    return false;
}
bool enqueue(uint8_t kind, uint8_t address, usb_device_handle_t handle) {
    if (queueCount == kEvents) { fault = true; return false; }
    queue[queueTail] = {kind, address, handle};
    queueTail = (queueTail + 1) % kEvents;
    ++queueCount;
    return true;
}
void client_event(const usb_host_client_event_msg_t *msg, void *) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
        (void)enqueue(1, msg->new_dev.address, nullptr);
    else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
        (void)enqueue(2, 0, msg->dev_gone.dev_hdl);
}
void complete_transfer(usb_transfer_t *) { inFlight = false; completed = true; }
bool start_failure(const char *stage, int code) {
    startupError.failure(stage, code);
    std::printf("USBCTRL start-failed stage=%s rc=%d\n", stage, code);
    return false;
}
#include "HostStartup.h"
bool pump(TickType_t delay) {
    if (!installed || !client) return false;
    uint32_t flags = 0;
    esp_err_t a = usb_host_lib_handle_events(0, &flags);
    esp_err_t b = usb_host_client_handle_events(client, delay);
    if ((a != ESP_OK && a != ESP_ERR_TIMEOUT) ||
        (b != ESP_OK && b != ESP_ERR_TIMEOUT)) { fault = true; return false; }
    return !fault;
}
/* A software read timeout does NOT cancel its asynchronous IDF transfer.
 * Halt/flush the claimed bulk endpoint, then pump until the callback has
 * returned ownership of the DMA buffer. Never free/unmap an in-flight transfer.
 * Restore a cleared endpoint only when the caller intends to keep using it. */
bool drain_bulk(bool resume) {
    if (!inFlight) return true;
    if (!transfer || !transfer->device_handle || !transfer->bEndpointAddress) {
        std::printf("USBCTRL cleanup-failed stage=bulk-drain reason=not-bulk\n");
        return false;
    }
    const usb_device_handle_t handle = transfer->device_handle;
    const uint8_t endpoint = transfer->bEndpointAddress;
    const esp_err_t halt = usb_host_endpoint_halt(handle, endpoint);
    const esp_err_t flush = usb_host_endpoint_flush(handle, endpoint);
    if (flush != ESP_OK) {
        std::printf("USBCTRL cleanup-failed stage=bulk-flush halt=%d flush=%d ep=%02x\n",
                    static_cast<int>(halt), static_cast<int>(flush),
                    static_cast<unsigned>(endpoint));
        return false;
    }
    const TickType_t begun = xTaskGetTickCount();
    while (inFlight) {
        if (!pump(1) || static_cast<TickType_t>(xTaskGetTickCount() - begun) >=
                             pdMS_TO_TICKS(kTeardownTicks)) {
            std::printf("USBCTRL cleanup-failed stage=bulk-callback ep=%02x\n",
                        static_cast<unsigned>(endpoint));
            return false;
        }
    }
    completed = false;
    if (resume && usb_host_endpoint_clear(handle, endpoint) != ESP_OK) {
        std::printf("USBCTRL cleanup-failed stage=bulk-clear ep=%02x\n",
                    static_cast<unsigned>(endpoint));
        return false;
    }
    std::printf("USBCTRL stage=bulk-drained ep=%02x resume=%u\n",
                static_cast<unsigned>(endpoint), static_cast<unsigned>(resume));
    return true;
}
bool wait_completion(uint32_t milliseconds) {
    TickType_t begun = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(milliseconds);
    if (!budget) budget = 1;
    while (!completed) {
        if (fault || !pump(1)) return false;
        if ((TickType_t)(xTaskGetTickCount() - begun) >= budget) {
            /* A quiet serial device frequently leaves bulk IN submitted.
             * Cancel it NOW rather than stranding the claim and VBUS on exit. */
            if (transfer && transfer->bEndpointAddress && !drain_bulk(true))
                std::printf("USBCTRL failure=bulk-timeout-undrained\n");
            return false;
        }
    }
    return transfer && transfer->status == USB_TRANSFER_STATUS_COMPLETED;
}
/* Never free a submitted transfer: teardown must obtain its callback first. */
bool idle_transfer() {
    if (inFlight) return false;
    if (completed) completed = false;
    return transfer != nullptr;
}

bool endpoint_mps(Device *d, uint8_t iface, uint8_t alt,
                  uint8_t endpoint, uint16_t *mps) {
    const usb_config_desc_t *config = nullptr;
    if (!d || !d->attached || !mps ||
        usb_host_get_active_config_descriptor(d->handle, &config) != ESP_OK ||
        !config || config->wTotalLength < 9 ||
        config->wTotalLength > RISC_USB_CONFIG_LIMIT) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(config);
    size_t length = config->wTotalLength;
    bool selected = false;
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        const uint8_t n = bytes[pos], type = bytes[pos + 1];
        if (n < 2 || n > length - pos) return false;
        if (type == 4) {
            if (n < 9) return false;
            selected = bytes[pos + 2] == iface && bytes[pos + 3] == alt;
        } else if (type == 5 && selected) {
            if (n < 7) return false;
            const uint16_t packet = static_cast<uint16_t>(bytes[pos + 4] |
                                                     (bytes[pos + 5] << 8));
            if (bytes[pos + 2] == endpoint && (bytes[pos + 3] & 3u) == 2u &&
                packet && packet <= 512) { *mps = packet; return true; }
        }
        pos += n;
    }
    return false;
}

int32_t next_event(void *, risc_usb_controller_event_v1 *out) {
    if (!out || fault || role.state() == UsbRoleSwitch::State::Off) return -1;
    if (role.state() == UsbRoleSwitch::State::Failed) return 0;

    // Cleanup can legitimately be partially complete: client may already be
    // deregistered while the IDF host/PHY/VBUS are still retained. Retry that
    // state before calling pump(), which requires a live client and would
    // otherwise turn safe retained cleanup into a provider event fault.
    if (role.state() == UsbRoleSwitch::State::Cleanup) {
        service_role();
        return 0;
    }

    // A parked provider may restart the physical host here. Do not require a
    // client until the role transition has had a chance to create one.
    if (role.state() == UsbRoleSwitch::State::Sense) {
        service_role();
        if (role.state() != UsbRoleSwitch::State::Host) return 0;
    }

    if (installed && !pump(0)) return -1;
    service_role();
    if (role.state() != UsbRoleSwitch::State::Host) return 0;
    if (!queueCount) return 0;
    Event e = queue[queueHead];
    queueHead = (queueHead + 1) % kEvents;
    --queueCount;
    if (e.kind == 1) {
        Device *freeSlot = nullptr;
        for (auto &d : devices) if (!d.handle) { freeSlot = &d; break; }
        if (!freeSlot) { fault = true; return -1; }
        usb_device_handle_t handle = nullptr;
        if (usb_host_device_open(client, e.address, &handle) != ESP_OK || !handle)
            return -1;
        const usb_device_desc_t *descriptor = nullptr;
        if (usb_host_get_device_descriptor(handle, &descriptor) != ESP_OK ||
            !descriptor) {
            if (usb_host_device_close(client, handle) != ESP_OK) fault = true;
            return -1;
        }
        uint64_t id = token();
        if (!id) { fault = true; return -1; }
        *freeSlot = {handle, id, descriptor->idVendor, descriptor->idProduct, true};
        *out = {1, id};
        return 1;
    }
    for (auto &d : devices) {
        if (d.handle != e.handle) continue;
        if (!d.attached) { fault = true; return -1; }
        d.attached = false;
        *out = {2, d.token};
        return 1;
    }
    fault = true;
    return -1;
}
bool configuration(void *, uint64_t id, uint8_t *bytes, size_t *size,
                   uint16_t *vid, uint16_t *pid) {
    Device *d = device(id);
    if (!running || !d || !d->attached || !bytes || !size || !vid || !pid)
        return false;
    const usb_config_desc_t *descriptor = nullptr;
    if (usb_host_get_active_config_descriptor(d->handle, &descriptor) != ESP_OK ||
        !descriptor || descriptor->wTotalLength < 9 ||
        descriptor->wTotalLength > RISC_USB_CONFIG_LIMIT ||
        *size < descriptor->wTotalLength) return false;
    *size = descriptor->wTotalLength;
    std::memcpy(bytes, descriptor, *size);
    *vid = d->vid;
    *pid = d->pid;
    return true;
}
bool claim_interface(void *, uint64_t id, uint8_t iface, uint8_t alt,
                     uint64_t *out) {
    Device *d = device(id);
    if (!running || !d || !d->attached || !out) return false;
    Claim *slot = nullptr;
    for (auto &c : claims) {
        if (c.token && c.physical_device == id && c.number == iface)
            return false;
        if (!c.token && !slot) slot = &c;
    }
    if (!slot) return false;
    if (usb_host_interface_claim(client, d->handle, iface, alt) != ESP_OK)
        return false;
    uint64_t assigned = token();
    if (!assigned) { fault = true; return false; }
    *slot = {assigned, id, iface, alt};
    *out = assigned;
    return true;
}
bool release_interface(void *, uint64_t id) {
    /* Cleanup must still find an existing physical claim after event parsing
     * has faulted. New operations remain blocked by claim()/device(). */
    Claim *c = nullptr;
    for (auto &candidate : claims)
        if (id && candidate.token == id) { c = &candidate; break; }
    if (!running || !c) return false;
    Device *d = nullptr;
    for (auto &candidate : devices)
        if (candidate.handle && candidate.token == c->physical_device) {
            d = &candidate; break;
        }
    if (!d) return false;
    /* An idle USB-UART can have an unanswered bulk IN when its app exits.
     * Drain ONLY the transfer belonging to this claimed interface. */
    if (inFlight) {
        uint16_t mps = 0;
        if (!transfer || transfer->device_handle != d->handle ||
            !transfer->bEndpointAddress ||
            !endpoint_mps(d, c->number, c->alternate,
                          transfer->bEndpointAddress, &mps) || !drain_bulk(false))
            return false;
    }
    const esp_err_t released = usb_host_interface_release(client, d->handle, c->number);
    if (released != ESP_OK) {
        std::printf("USBCTRL cleanup-failed stage=interface-release rc=%d\n",
                    static_cast<int>(released));
        return false;
    }
    *c = {};
    if (!d->attached && !claimed(d->token)) {
        const esp_err_t closed = usb_host_device_close(client, d->handle);
        if (closed != ESP_OK) {
            /* Interface release has already succeeded. Returning failure
             * here would make usb.host retain a claim that can NEVER be
             * released again. Keep the device handle for verified quiesce. */
            std::printf("USBCTRL stage=device-close-deferred rc=%d\n",
                        static_cast<int>(closed));
            return true;
        }
        *d = {};
    }
    return true;
}
int32_t control(void *, uint64_t id, uint8_t type, uint8_t request,
                uint16_t value, uint16_t index, uint8_t *payload,
                uint16_t length, uint32_t timeout) {
    Device *d = device(id);
    if (!running || !d || !d->attached || !timeout ||
        length > RISC_USB_CONFIG_LIMIT || (length && !payload) ||
        !idle_transfer()) return -1;
    uint8_t *b = transfer->data_buffer;
    b[0] = type; b[1] = request;
    b[2] = static_cast<uint8_t>(value); b[3] = static_cast<uint8_t>(value >> 8);
    b[4] = static_cast<uint8_t>(index); b[5] = static_cast<uint8_t>(index >> 8);
    b[6] = static_cast<uint8_t>(length); b[7] = static_cast<uint8_t>(length >> 8);
    if ((type & 0x80u) == 0 && length) std::memcpy(b + 8, payload, length);
    transfer->device_handle = d->handle;
    transfer->bEndpointAddress = 0;
    transfer->num_bytes = length + 8;
    transfer->callback = complete_transfer;
    transfer->context = nullptr;
    completed = false;
    inFlight = true;
    if (usb_host_transfer_submit_control(client, transfer) != ESP_OK) {
        inFlight = false; return -1;
    }
    if (!wait_completion(timeout)) return -1;
    int32_t n = transfer->actual_num_bytes;
    if (n >= 8) n -= 8;
    if (n < 0 || n > length) return -1;
    if ((type & 0x80u) && n) std::memcpy(payload, b + 8, n);
    return (type & 0x80u) ? n : length;
}
int32_t bulk(void *, uint64_t id, uint8_t endpoint,
             uint8_t *dst, const uint8_t *src, size_t length,
             uint32_t timeout, bool reading) {
    Claim *c = claim(id);
    Device *d = c ? device(c->physical_device) : nullptr;
    uint16_t mps = 0;
    if (!running || !d || !d->attached || !length ||
        length > RISC_USB_CONFIG_LIMIT || !timeout ||
        (reading ? (!dst || !(endpoint & 0x80u)) : (!src || (endpoint & 0x80u))) ||
        !endpoint_mps(d, c->number, c->alternate, endpoint, &mps) ||
        !idle_transfer()) return -1;
    size_t transaction = length;
    if (reading) {
        transaction = ((length + mps - 1) / mps) * mps;
        if (transaction > RISC_USB_CONFIG_LIMIT) return -1;
    } else std::memcpy(transfer->data_buffer, src, length);
    transfer->device_handle = d->handle;
    transfer->bEndpointAddress = endpoint;
    transfer->num_bytes = static_cast<int>(transaction);
    transfer->callback = complete_transfer;
    transfer->context = nullptr;
    completed = false;
    inFlight = true;
    if (usb_host_transfer_submit(transfer) != ESP_OK) {
        inFlight = false; return -1;
    }
    if (!wait_completion(timeout)) return -1;
    int32_t n = transfer->actual_num_bytes;
    if (n < 0 || static_cast<size_t>(n) > length) return -1;
    if (reading && n) std::memcpy(dst, transfer->data_buffer, n);
    return n;
}
int32_t bulk_read(void *ctx, uint64_t id, uint8_t ep, uint8_t *dst,
                  size_t len, uint32_t timeout) {
    return bulk(ctx, id, ep, dst, nullptr, len, timeout, true);
}
int32_t bulk_write(void *ctx, uint64_t id, uint8_t ep, const uint8_t *src,
                   size_t len, uint32_t timeout) {
    return bulk(ctx, id, ep, nullptr, src, len, timeout, false);
}

bool quiesce_host() {
    if (inFlight && !drain_bulk(false)) return false;
    /* A discovery/event fault blocks new work, not verified cleanup. We may
     * release VBUS only after claims, DMA, devices, callbacks and IDF host
     * have all been independently drained. An in-flight transfer still fails
     * above if callbacks cannot be pumped safely. */
    if (fault) std::printf("USBCTRL stage=faulted-controller-quiesce\n");
    if (claimed(0)) {
        std::printf("USBCTRL cleanup-failed stage=invalid-claim\n");
        return false;
    }
    for (const auto &c : claims) if (c.token) {
        std::printf("USBCTRL cleanup-failed stage=outstanding-claim\n");
        return false;
    }
    /* Do not release VBUS while DMA or IDF callbacks still own memory. */
    for (auto &d : devices) if (d.handle) {
        const esp_err_t closed = usb_host_device_close(client, d.handle);
        if (closed != ESP_OK) {
            std::printf("USBCTRL cleanup-failed stage=device-close rc=%d\n",
                        static_cast<int>(closed));
            return false;
        }
        d = {};
    }
    if (transfer) {
        const esp_err_t freed = usb_host_transfer_free(transfer);
        if (freed != ESP_OK) {
            std::printf("USBCTRL cleanup-failed stage=transfer-free rc=%d\n",
                        static_cast<int>(freed));
            return false;
        }
        transfer = nullptr;
    }
    if (client) {
        const esp_err_t deregistered = usb_host_client_deregister(client);
        if (deregistered != ESP_OK) {
            std::printf("USBCTRL cleanup-failed stage=client-deregister rc=%d\n",
                        static_cast<int>(deregistered));
            return false;
        }
        client = nullptr;
    }
    if (installed) {
        /* IDF requires the last-client deregistration to be processed by the
         * host-library event loop before device_free_all() is legal. Preserve
         * the observation across cleanup retries because NO_CLIENTS is a
         * one-shot event and the client handle has already been consumed. */
        if (!noClientsObserved) {
            const TickType_t begun = xTaskGetTickCount();
            for (uint32_t i = 0; !noClientsObserved && i < kTeardownTicks; ++i) {
                if (static_cast<TickType_t>(xTaskGetTickCount() - begun) >=
                    pdMS_TO_TICKS(kTeardownTicks)) break;
                uint32_t flags = 0;
                const esp_err_t events = usb_host_lib_handle_events(1, &flags);
                if (events != ESP_OK && events != ESP_ERR_TIMEOUT) {
                    std::printf("USBCTRL cleanup-failed stage=no-clients-events rc=%d\n",
                                static_cast<int>(events));
                    return false;
                }
                if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
                    noClientsObserved = true;
                if (!noClientsObserved) vTaskDelay(1);
            }
            if (!noClientsObserved) {
                std::printf("USBCTRL cleanup-failed stage=no-clients-timeout\n");
                return false;
            }
            std::printf("USBCTRL stage=no-clients\n");
        }

        esp_err_t rc = usb_host_device_free_all();
        if (rc != ESP_OK && rc != ESP_ERR_NOT_FINISHED) {
            std::printf("USBCTRL cleanup-failed stage=device-free-all rc=%d\n",
                        static_cast<int>(rc));
            return false;
        }
        bool freed = rc == ESP_OK;
        if (!freed) {
            const TickType_t begun = xTaskGetTickCount();
            for (uint32_t i = 0; !freed && i < kTeardownTicks; ++i) {
                if (static_cast<TickType_t>(xTaskGetTickCount() - begun) >=
                    pdMS_TO_TICKS(kTeardownTicks)) break;
                uint32_t flags = 0;
                rc = usb_host_lib_handle_events(1, &flags);
                if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) {
                    std::printf("USBCTRL cleanup-failed stage=device-free-events rc=%d\n",
                                static_cast<int>(rc));
                    return false;
                }
                if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) freed = true;
                if (!freed) vTaskDelay(1);
            }
        }
        if (!freed) {
            std::printf("USBCTRL cleanup-failed stage=device-free-timeout\n");
            return false;
        }
        rc = usb_host_uninstall();
        if (rc != ESP_OK) {
            std::printf("USBCTRL cleanup-failed stage=host-uninstall rc=%d\n",
                        static_cast<int>(rc));
            return false;
        }
        installed = false;
        noClientsObserved = false;
    }
    if (!release_host_phy()) return false;
    if (powerLease) {
        if (!power || !power->release_host(power->context, powerLease)) {
            std::printf("USBCTRL cleanup-failed stage=vbus-release\n");
            return false;
        }
        powerLease = 0;
        std::printf("USBCTRL stage=vbus-released\n");
    }
    // A failed acquire may retain an internal source lease even when the
    // caller's token is zero. Do not expose the boot PHY until source-off is
    // independently observed. Keep the power chip claimed while monitoring.
    if (powerMonitor) {
        const int32_t status = powerMonitor->input_status(power->context);
        if (status != RISC_USB_POWER_ABSENT && status != RISC_USB_POWER_EXTERNAL &&
            status != RISC_USB_POWER_SETTLING)
            return false;
    }
    restore_phy_route();
    running = false;
    queueHead = queueTail = queueCount = 0;
    return true;
}
bool quiesce(void *) {
    role.stop();
    // Repeated graph cleanup may already have released the chip's I2C claim.
    if (running || installed || phy || phyRouteCaptured || client || transfer || powerLease) {
        if (!quiesce_host()) return false;
    }
    return !power || power->quiesce(power->context);
}
void stop() {
    if (!quiesce(nullptr)) return;
    power = nullptr;
    powerMonitor = nullptr;
    queueHead = queueTail = queueCount = 0;
    fault = false;
}
bool startup_error(char *destination, size_t capacity) {
    return startupError.copy(destination, capacity);
}
bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    startupError.clear();
    enumerationDiagnostic.clear();
    if (role.state() != UsbRoleSwitch::State::Off || running || installed || phy || phyRouteCaptured || client || transfer || powerLease || fault ||
        !deps || count != 1 || !equals(deps[0].capability_id, "board.power.vbus") ||
        deps[0].api_version != RISC_USB_VBUS_API_V1 || !deps[0].api) {
        startupError.text("dependency/state");
        startupError.number(" count=", static_cast<uint32_t>(count));
        startupError.number(" deps=", deps != nullptr);
        startupError.number(" run=", running);
        startupError.number(" host=", installed);
        startupError.number(" phy=", phy != nullptr);
        startupError.number(" client=", client != nullptr);
        startupError.number(" dma=", transfer != nullptr);
        startupError.number(" lease=", powerLease != 0);
        startupError.number(" fault=", fault);
        std::printf("USBCTRL start-failed stage=dependency-or-state rc=0\n");
        return false;
    }
    const auto *api = static_cast<const risc_usb_vbus_api_v1 *>(deps[0].api);
    if (api->api_version != RISC_USB_VBUS_API_V1 ||
        api->struct_size < sizeof(risc_usb_vbus_monitor_api_v1) || !api->acquire_host ||
        !api->release_host || !api->quiesce) {
        startupError.text("board.power.vbus: input-monitor extension required;");
        startupError.number(" api=", api->api_version);
        startupError.number(" size=", api->struct_size);
        startupError.number(" acquire=", api->acquire_host != nullptr);
        startupError.number(" release=", api->release_host != nullptr);
        startupError.number(" quiesce=", api->quiesce != nullptr);
        std::printf("USBCTRL start-failed stage=vbus-abi rc=0\n");
        return false;
    }
    power = api;
    powerMonitor = reinterpret_cast<const risc_usb_vbus_monitor_api_v1 *>(api);
    if (!powerMonitor->input_status) return start_failure("vbus-monitor-abi", 0);
    role.begin(static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS));
    std::printf("USBCTRL stage=role-monitor-started\n");
    return true;
}
struct RolePort {
    uint32_t now() const { return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS); }
    int32_t input() const { return powerMonitor->input_status(power->context); }
    bool idle_probe_required() const {
        return (powerMonitor->flags & RISC_USB_POWER_IDLE_PROBE_REQUIRED) != 0;
    }
    bool busy() const {
        if (queueCount || inFlight || (USB_DWC.hprt_reg.val & 1u)) return true;
        for (const auto &d : devices) if (d.attached) return true;
        for (const auto &c : claims) if (c.token) return true;
        return false;
    }
    bool park() const {
        return (!drainRoleInterrupts || drainRoleInterrupts()) && quiesce_host();
    }
    bool start() const {
        startupError.clear(); enumerationDiagnostic.clear();
        noClientsObserved = false;
        if (!start_host_controller()) return false;
        running = true;
        return true;
    }
    void report(const char *state) const { std::printf("USBCTRL role=%s\n", state); }
};
void service_role() { RolePort port; role.poll(port, port.now()); }
static const risc_usb_controller_api_v1 interface = {
    RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_api_v1), nullptr,
    next_event, configuration, claim_interface, release_interface,
    control, bulk_read, bulk_write, quiesce
};
static const risc_driver_diagnostics_v2 driver = {{
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2),
    "usb-controller-esp32s3", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &interface, start, stop, []() -> bool { return quiesce(nullptr); }
}, startup_error};
} // namespace
extern "C" __attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.base : nullptr;
}
