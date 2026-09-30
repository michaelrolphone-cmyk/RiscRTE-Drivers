#include "../../Drivers/usb_controller_esp32s3/StartupDiagnostic.h"
#include "../../sdk/driver/RiscUsbVbusV1.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

// IDF boundary fakes. The implementation under test is the production header.
using esp_err_t = int;
using TickType_t = unsigned;
using usb_phy_handle_t = void *;
using usb_host_client_handle_t = void *;
struct usb_transfer_t {};
enum { ESP_OK, ESP_ERR_NOT_FOUND = 0x105, USB_PHY_CTRL_OTG = 2,
       USB_PHY_TARGET_INT, USB_OTG_MODE_HOST, USB_PHY_SPEED_UNDEFINED,
       USB_PHY_ACTION_HOST_FORCE_DISCONN, USB_PHY_ACTION_HOST_ALLOW_CONN,
       ESP_INTR_FLAG_LOWMED = 14 };
struct usb_phy_config_t {
    int controller, target, otg_mode, otg_speed;
    void *gpio_conf;
};
struct usb_host_config_t { bool skip_phy_setup; int intr_flags; };
struct usb_host_client_event_msg_t {};
using EventCallback = void (*)(const usb_host_client_event_msg_t *, void *);
struct usb_host_client_config_t {
    bool is_synchronous;
    size_t max_num_event_msg;
    struct { EventCallback client_event_callback; void *callback_arg; } async;
};
constexpr size_t kEvents = 16, kBuffer = 1032;
static usb_phy_handle_t phy;
static usb_host_client_handle_t client;
static usb_transfer_t *transfer;
static bool installed;
static StartupDiagnostic startupError;
static usb_transfer_t dma;
static int step, fail_at, delete_calls;
static bool disconnected, delayed, present, attach_edge;
static bool host_role, partial_power_failure, zero_power_lease;
static uint64_t powerLease;
static unsigned power_calls;
static bool external_callback_called;
static unsigned tick_ms = 1;
struct RtcRegisters {
    struct { bool sw_hw_usb_phy_sel, sw_usb_phy_sel; } usb_conf;
} RTCCNTL;
#include "../../Drivers/usb_controller_esp32s3/PhyRoute.h"

static void client_event(const usb_host_client_event_msg_t *, void *) {}
static bool start_failure(const char *stage, int code) {
    startupError.failure(stage, code);
    return false;
}
static esp_err_t operation(int expected_step) {
    assert(++step == expected_step);
    return step == fail_at ? ESP_ERR_NOT_FOUND : ESP_OK;
}
static esp_err_t usb_new_phy(const usb_phy_config_t *config, usb_phy_handle_t *out) {
    assert(config->controller == USB_PHY_CTRL_OTG && config->target == USB_PHY_TARGET_INT);
    assert(config->otg_mode == USB_OTG_MODE_HOST && config->otg_speed == USB_PHY_SPEED_UNDEFINED);
    assert(!config->gpio_conf && !*out && !powerLease && !power_calls);
    const auto rc = operation(1);
    if (rc == ESP_OK) {
        *out = &dma; host_role = true;
        RTCCNTL.usb_conf.sw_hw_usb_phy_sel = true;
        RTCCNTL.usb_conf.sw_usb_phy_sel = true;
    }
    return rc;
}
static esp_err_t usb_phy_action(usb_phy_handle_t handle, int action) {
    assert(handle && handle == phy);
    if (action == USB_PHY_ACTION_HOST_FORCE_DISCONN) {
        assert(!installed && !client && !transfer);
        const auto rc = operation(2);
        if (rc == ESP_OK) disconnected = true;
        return rc;
    }
    assert(action == USB_PHY_ACTION_HOST_ALLOW_CONN);
    assert(disconnected && installed && client && transfer && delayed && powerLease);
    const auto rc = operation(7);
    if (rc == ESP_OK) {
        disconnected = false;
        // A pre-attached device gets the same detector edge as a hotplug.
        // This models the PHY action's contract, not ESP32 electrical timing.
        attach_edge = present;
    }
    return rc;
}
static esp_err_t usb_host_install(const usb_host_config_t *config) {
    assert(disconnected && phy && !installed && config->skip_phy_setup);
    assert(config->intr_flags == ESP_INTR_FLAG_LOWMED);
    return operation(3);
}
static esp_err_t usb_host_client_register(const usb_host_client_config_t *config,
                                         usb_host_client_handle_t *out) {
    assert(installed && disconnected && !*out);
    assert(!config->is_synchronous && config->max_num_event_msg == kEvents);
    assert(config->async.client_event_callback == client_event && !config->async.callback_arg);
    const auto rc = operation(4);
    if (rc == ESP_OK) *out = &dma;
    return rc;
}
static esp_err_t usb_host_transfer_alloc(size_t bytes, int flags, usb_transfer_t **out) {
    assert(client && disconnected && !*out && bytes == kBuffer && flags == 0);
    const auto rc = operation(5);
    if (rc == ESP_OK) *out = &dma;
    return rc;
}
static TickType_t pdMS_TO_TICKS(unsigned ms) { return ms / tick_ms; }
static void vTaskDelay(TickType_t ticks) {
    assert(step == 5 && disconnected && installed && client && transfer);
    assert(ticks >= 1 && host_role && !power_calls && !powerLease);
    delayed = true;
}
static esp_err_t usb_del_phy(usb_phy_handle_t handle) {
    assert(handle && handle == phy && !installed && !client && !transfer);
    ++delete_calls;
    return fail_at == 8 ? ESP_ERR_NOT_FOUND : ESP_OK;
}
static bool acquire_power(void *, uint32_t milliamps, uint64_t *lease) {
    ++power_calls;
    // The receiver must NEVER power up against boot USB Serial/JTAG's device
    // role. All host resources must exist before this side effect is possible.
    assert(host_role && disconnected && installed && client && transfer && delayed);
    assert(milliamps == 500 && !*lease);
    assert(phyRouteCaptured && RTCCNTL.usb_conf.sw_hw_usb_phy_sel && RTCCNTL.usb_conf.sw_usb_phy_sel);
    const auto rc = operation(6);
    if ((rc == ESP_OK && !zero_power_lease) || partial_power_failure) *lease = 42;
    return rc == ESP_OK;
}
static bool release_power(void *, uint64_t lease) {
    assert(lease == 42 && !phy && !installed && !client && !transfer);
    return true;
}
static const risc_usb_vbus_api_v1 powerApi = {
    RISC_USB_VBUS_API_V1, sizeof(risc_usb_vbus_api_v1), nullptr,
    acquire_power, release_power, nullptr
};
static const risc_usb_vbus_api_v1 *power = &powerApi;
#include "../../Drivers/usb_controller_esp32s3/HostStartup.h"

static void reset(bool already_present, int failure) {
    assert(!phy && !client && !transfer && !installed && !powerLease && !phyRouteCaptured);
    step = delete_calls = 0; fail_at = failure;
    disconnected = delayed = attach_edge = false;
    host_role = partial_power_failure = zero_power_lease = false;
    power_calls = 0;
    RTCCNTL.usb_conf = {true, false}; // Explicit boot serial/JTAG route.
    present = already_present;
    startupError.clear();
}
static void teardown_host() {
    // Production quiesce drains DMA, deregisters the client and uninstalls
    // the host before calling release_host_phy. Check each ownership guard.
    if (transfer || client || installed) assert(!release_host_phy());
    transfer = nullptr;
    if (client || installed) assert(!release_host_phy());
    client = nullptr;
    if (installed) assert(!release_host_phy());
    installed = false;
}
static void release_power_after_phy() {
    if (powerLease) {
        assert(power->release_host(power->context, powerLease));
        powerLease = 0;
    }
    assert(phyRouteCaptured);
    restore_phy_route();
    assert(!phyRouteCaptured && RTCCNTL.usb_conf.sw_hw_usb_phy_sel && !RTCCNTL.usb_conf.sw_usb_phy_sel);
}
int main() {
    for (unsigned period : {1u, 100u}) {
        tick_ms = period;
        reset(true, 0);
        assert(start_host_controller() && step == 7 && attach_edge && power_calls == 1);
        assert(phy && installed && client && transfer && !disconnected);
        teardown_host();
        fail_at = 8;
        assert(!release_host_phy() && phy && delete_calls == 1 && powerLease == 42);
        fail_at = 0;
        assert(release_host_phy() && !phy && delete_calls == 2);
        assert(release_host_phy() && delete_calls == 2);
        release_power_after_phy();
    }
    // External power callback occupies the SAME ordered acquisition boundary.
    reset(true,0);
    assert(start_host_controller([](void *ctx,uint32_t ma,uint64_t *lease){
        external_callback_called=true;
        return power->acquire_host(ctx,ma,lease);
    }));
    assert(attach_edge && power_calls==1 && external_callback_called);
    teardown_host();assert(release_host_phy());release_power_after_phy();
    reset(false, 0);
    assert(start_host_controller() && !attach_edge && !disconnected);
    teardown_host();
    assert(release_host_phy());
    release_power_after_phy();
    const char *stages[] = {"", "phy-create", "phy-hold-disconnected",
        "usb-host-install/IRQ-unavailable", "client-register", "transfer-alloc",
        "vbus-acquire", "phy-allow-connection"};
    for (int failure = 1; failure <= 7; ++failure) {
        reset(true, failure);
        assert(!start_host_controller() && step == failure && !attach_edge);
        assert(bool(phy) == (failure > 1));
        assert(installed == (failure > 3));
        assert(bool(client) == (failure > 4));
        assert(bool(transfer) == (failure > 5));
        assert(power_calls == (failure >= 6 ? 1u : 0u));
        assert(bool(powerLease) == (failure > 6));
        char error[112] = {};
        assert(startupError.copy(error, sizeof(error)) && strstr(error, stages[failure]));
        teardown_host();
        fail_at = 0;
        assert(release_host_phy() && !phy);
        release_power_after_phy();
    }
    // A power provider can fail after an uncertain write and retain a lease.
    // Conversely a malformed success without a lease must not enable attach.
    for (bool partial : {true, false}) {
        reset(true, partial ? 6 : 0);
        partial_power_failure = partial;
        zero_power_lease = !partial;
        assert(!start_host_controller() && step == 6 && !attach_edge);
        assert(bool(powerLease) == partial);
        assert(phy && installed && client && transfer && disconnected);
        char error[112] = {};
        assert(startupError.copy(error, sizeof(error)) && strstr(error, "vbus-acquire"));
        assert(strstr(error, "requested-ma=500") && !strstr(error, "timeout=500ms"));
        teardown_host();
        assert(release_host_phy());
        release_power_after_phy();
    }
    // Also preserve the boot ROM's automatic (eFuse-controlled) selection.
    reset(false, 0);
    RTCCNTL.usb_conf = {false, false};
    assert(start_host_controller());
    teardown_host();
    assert(release_host_phy());
    assert(power->release_host(power->context, powerLease)); powerLease = 0;
    restore_phy_route();
    assert(!phyRouteCaptured && !RTCCNTL.usb_conf.sw_hw_usb_phy_sel && !RTCCNTL.usb_conf.sw_usb_phy_sel);
    puts("USB host role before receiver power; startup failures and retained ownership: PASS");
}
