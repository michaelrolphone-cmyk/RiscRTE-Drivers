/* Exercise the production private HCD/hub lifecycle bodies with fake SDK
 * resources. Every fake checks child-before-parent destruction ordering. */
#include <esp_intr_alloc.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define HCD_NUM_PORTS 1
enum {
    HCD_PORT_STATE_NOT_POWERED,
    HCD_PORT_STATE_RECOVERY,
    HCD_PORT_STATE_ENABLED,
    ROOT_PORT_STATE_NOT_POWERED,
    ROOT_PORT_STATE_ENABLED,
    HCD_PORT_CMD_POWER_OFF
};
typedef void *hcd_port_handle_t;
typedef struct { struct { void *hdls; } channels; } usb_dwc_hal_context_t;
typedef struct {
    intr_handle_t isr_hdl;
    usb_dwc_hal_context_t *hal;
    int periph_idx, num_pipes_idle, num_pipes_queued, state;
    void *task_waiting_port_notif;
    void *callback_arg;
} port_t;
typedef struct {
    struct { hcd_port_handle_t hdl; } constant;
    struct { int state; } dynamic;
} root_hub_port_t;
typedef struct { root_hub_port_t root_hub_ports[HCD_NUM_PORTS]; } hub_driver_t;
struct intr_handle_data_t { int unused; };

static bool s_port_inited[HCD_NUM_PORTS];
static hub_driver_t *p_hub_driver_obj;
static hub_driver_t hub;
static port_t port;
static usb_dwc_hal_context_t hal;
static struct intr_handle_data_t irq;
static int channel_buffer;
static bool port_live, hal_live, channels_live, hub_live, irq_live, irq_enabled;
static unsigned critical_depth, disable_calls, irq_free_calls, channel_free_calls;
static unsigned hal_deinit_calls, port_free_calls, hub_free_calls, command_calls;
static esp_err_t disable_result, free_result, command_result;

static void enter_critical(void) { assert(!critical_depth); ++critical_depth; }
static void exit_critical(void) { assert(critical_depth == 1); --critical_depth; }
#define HCD_ENTER_CRITICAL() enter_critical()
#define HCD_EXIT_CRITICAL() exit_critical()
#define HUB_DRIVER_ENTER_CRITICAL() enter_critical()
#define HUB_DRIVER_EXIT_CRITICAL() exit_critical()
#define HCD_CHECK(condition, error) do { if (!(condition)) return (error); } while (0)
#define HUB_DRIVER_CHECK(condition, error) HCD_CHECK(condition, error)
#define HCD_CHECK_FROM_CRIT(condition, error) do { \
    if (!(condition)) { exit_critical(); return (error); } \
} while (0)
#define HUB_DRIVER_CHECK_FROM_CRIT(condition, error) HCD_CHECK_FROM_CRIT(condition, error)

static void assert_callback_parents_live(void) {
    assert(port_live && hub_live);
    assert(port.callback_arg == &hub.root_hub_ports[0]);
}
static esp_err_t risc_usb_intr_disable(intr_handle_t handle) {
    ++disable_calls;
    assert_callback_parents_live();
    assert(handle == &irq && irq_live);
    if (disable_result == ESP_OK) irq_enabled = false;
    return disable_result;
}
static esp_err_t risc_usb_intr_free(intr_handle_t handle) {
    ++irq_free_calls;
    assert_callback_parents_live();
    assert(handle == &irq && irq_live && !irq_enabled);
    if (free_result == ESP_OK) irq_live = false;
    return free_result;
}
static void free_channels(void *allocation) {
    if (!allocation) return;
    ++channel_free_calls;
    assert(allocation == &channel_buffer && channels_live && !irq_live);
    assert_callback_parents_live();
    channels_live = false;
}
static void usb_dwc_hal_deinit(usb_dwc_hal_context_t *context) {
    ++hal_deinit_calls;
    assert(context == &hal && hal_live && !channels_live && !irq_live);
    assert_callback_parents_live();
    hal_live = false;
}
static void port_obj_free(port_t *value) {
    ++port_free_calls;
    assert(value == &port && port_live && !hal_live && !channels_live && !irq_live);
    assert_callback_parents_live();
    port_live = false;
}
static void heap_caps_free(void *value) {
    ++hub_free_calls;
    assert(value == &hub && hub_live && !port_live && !irq_live);
    assert(!s_port_inited[0] && !p_hub_driver_obj);
    assert(!hub.root_hub_ports[0].constant.hdl);
    hub_live = false;
}
static esp_err_t hcd_port_command(hcd_port_handle_t handle, int command) {
    ++command_calls;
    assert(handle == &port && command == HCD_PORT_CMD_POWER_OFF && port_live);
    assert_callback_parents_live();
    if (command_result == ESP_OK) port.state = HCD_PORT_STATE_NOT_POWERED;
    return command_result;
}

// Compile the actual implementations; only SDK boundaries above are faked.
#define free free_channels
#include "../../Drivers/usb_controller_esp32s3/NativeHcdLifecycle.inc"
#undef free
#include "../../Drivers/usb_controller_esp32s3/NativeHubLifecycle.inc"

static void setup(bool pending_port, bool pending_hub, bool initialized_hal,
                  bool allocated_irq) {
    assert(!critical_depth && !port_live && !hub_live && !irq_live);
    assert(!risc_pending_port && !risc_pending_hub && !p_hub_driver_obj);
    memset(&hub, 0, sizeof(hub));
    memset(&port, 0, sizeof(port));
    memset(&hal, 0, sizeof(hal));
    port_live = hub_live = s_port_inited[0] = true;
    hal_live = channels_live = initialized_hal;
    irq_live = irq_enabled = allocated_irq;
    port.isr_hdl = allocated_irq ? &irq : NULL;
    port.hal = &hal;
    port.state = HCD_PORT_STATE_NOT_POWERED;
    port.callback_arg = &hub.root_hub_ports[0];
    hal.channels.hdls = initialized_hal ? &channel_buffer : NULL;
    hub.root_hub_ports[0].dynamic.state = ROOT_PORT_STATE_NOT_POWERED;
    hub.root_hub_ports[0].constant.hdl = pending_port ? NULL : &port;
    risc_pending_port = pending_port ? &port : NULL;
    risc_pending_hal = pending_port && initialized_hal;
    risc_pending_hub = pending_hub ? &hub : NULL;
    p_hub_driver_obj = pending_hub ? NULL : &hub;
    disable_calls = irq_free_calls = channel_free_calls = hal_deinit_calls = 0;
    port_free_calls = hub_free_calls = command_calls = 0;
    disable_result = free_result = command_result = ESP_OK;
}
static void assert_retained(bool initialized_hal) {
    assert(!critical_depth && port_live && hub_live && irq_live && s_port_inited[0]);
    assert(port.isr_hdl == &irq);
    assert(hal_live == initialized_hal && channels_live == initialized_hal);
    assert(!port_free_calls && !hub_free_calls && !channel_free_calls && !hal_deinit_calls);
    assert_callback_parents_live();
}
static void assert_released(bool initialized_hal) {
    assert(!critical_depth && !port_live && !hub_live && !irq_live && !irq_enabled);
    assert(!hal_live && !channels_live && !s_port_inited[0] && !p_hub_driver_obj);
    assert(!risc_pending_port && !risc_pending_hub && !risc_pending_hal);
    assert(port_free_calls == 1 && hub_free_calls == 1);
    assert(channel_free_calls == (unsigned)initialized_hal);
    assert(hal_deinit_calls == (unsigned)initialized_hal);
}

static void installed_uninstall_retry(void) {
    setup(false, false, true, true);
    disable_result = ESP_ERR_TIMEOUT;
    assert(hub_uninstall() == ESP_ERR_TIMEOUT);
    assert_retained(true);
    assert(p_hub_driver_obj == &hub && hub.root_hub_ports[0].constant.hdl == &port);
    assert(irq_enabled && disable_calls == 1 && !irq_free_calls);

    disable_result = ESP_OK;
    free_result = ESP_FAIL;
    assert(hub_uninstall() == ESP_FAIL);
    assert_retained(true);
    assert(p_hub_driver_obj == &hub && hub.root_hub_ports[0].constant.hdl == &port);
    assert(!irq_enabled && disable_calls == 2 && irq_free_calls == 1);

    free_result = ESP_OK;
    assert(hub_uninstall() == ESP_OK);
    assert_released(true);
    assert(disable_calls == 3 && irq_free_calls == 2);
    assert(hub_uninstall() == ESP_ERR_INVALID_STATE);
    assert(hub_root_stop() == ESP_ERR_INVALID_STATE);
    assert_released(true);
}

static void install_cleanup_retry(bool initialized_hal) {
    // hcd_port_init has retained its unpublished port; hub_install must retain
    // the containing callback root-port object until this child can be freed.
    setup(true, true, initialized_hal, true);
    disable_result = ESP_FAIL;
    assert(risc_usb_hub_retry_cleanup() == ESP_FAIL);
    assert_retained(initialized_hal);
    assert(risc_pending_port == &port && risc_pending_hub == &hub);
    assert(!p_hub_driver_obj && !hub.root_hub_ports[0].constant.hdl);
    assert(irq_enabled && !irq_free_calls);

    disable_result = ESP_OK;
    free_result = ESP_ERR_TIMEOUT;
    assert(risc_usb_hub_retry_cleanup() == ESP_ERR_TIMEOUT);
    assert_retained(initialized_hal);
    assert(risc_pending_port == &port && risc_pending_hub == &hub && !irq_enabled);

    free_result = ESP_OK;
    assert(risc_usb_hub_retry_cleanup() == ESP_OK);
    assert_released(initialized_hal);
    const unsigned disables = disable_calls, frees = irq_free_calls;
    assert(risc_usb_hub_retry_cleanup() == ESP_OK);
    assert(risc_usb_hcd_retry_cleanup() == ESP_OK);
    assert(disable_calls == disables && irq_free_calls == frees);
    assert_released(initialized_hal);
}

static void hub_cleanup_registered_child_retry(void) {
    // The HCD port succeeded but a later hub install stage failed.
    setup(false, true, true, true);
    free_result = ESP_FAIL;
    assert(risc_usb_hub_retry_cleanup() == ESP_FAIL);
    assert_retained(true);
    assert(risc_pending_hub == &hub && !risc_pending_port);
    assert(hub.root_hub_ports[0].constant.hdl == &port);
    free_result = ESP_OK;
    assert(risc_usb_hub_retry_cleanup() == ESP_OK);
    assert_released(true);
}

static void stop_failure_preserves_state(void) {
    setup(false, false, true, true);
    port.state = HCD_PORT_STATE_ENABLED;
    hub.root_hub_ports[0].dynamic.state = ROOT_PORT_STATE_ENABLED;
    assert(hub_uninstall() == ESP_ERR_INVALID_STATE);
    assert_retained(true);
    command_result = ESP_ERR_TIMEOUT;
    assert(hub_root_stop() == ESP_ERR_TIMEOUT);
    assert_retained(true);
    assert(command_calls == 1 && port.state == HCD_PORT_STATE_ENABLED);
    assert(hub.root_hub_ports[0].dynamic.state == ROOT_PORT_STATE_ENABLED);
    assert(p_hub_driver_obj == &hub && hub.root_hub_ports[0].constant.hdl == &port);
    assert(!disable_calls && !irq_free_calls);

    command_result = ESP_OK;
    assert(hub_root_stop() == ESP_OK && command_calls == 2);
    assert(port.state == HCD_PORT_STATE_NOT_POWERED);
    assert(hub.root_hub_ports[0].dynamic.state == ROOT_PORT_STATE_NOT_POWERED);
    assert(hub_root_stop() == ESP_OK && command_calls == 2);
    assert(hub_uninstall() == ESP_OK);
    assert_released(true);
}

static void deinit_preconditions(void) {
    setup(false, false, true, true);
    assert(hcd_port_deinit(NULL) == ESP_ERR_INVALID_ARG);
    port.num_pipes_idle = 1;
    assert(hcd_port_deinit(&port) == ESP_ERR_INVALID_STATE);
    port.num_pipes_idle = 0;
    port.num_pipes_queued = 1;
    assert(hcd_port_deinit(&port) == ESP_ERR_INVALID_STATE);
    port.num_pipes_queued = 0;
    port.task_waiting_port_notif = &port;
    assert(hcd_port_deinit(&port) == ESP_ERR_INVALID_STATE);
    port.task_waiting_port_notif = NULL;
    port.state = HCD_PORT_STATE_ENABLED;
    assert(hcd_port_deinit(&port) == ESP_ERR_INVALID_STATE);
    port.state = HCD_PORT_STATE_RECOVERY;
    assert_retained(true);
    assert(!disable_calls && !irq_free_calls);
    assert(hub_uninstall() == ESP_OK);
    assert_released(true);
}

int main(void) {
    installed_uninstall_retry();
    install_cleanup_retry(true);
    install_cleanup_retry(false);
    hub_cleanup_registered_child_retry();
    stop_failure_preserves_state();
    deinit_preconditions();
    // Failed before IRQ/HAL ownership: release the pending objects without
    // invoking IRQ/HAL destructors or touching an uninitialized native handle.
    setup(true, true, false, false);
    assert(risc_usb_hub_retry_cleanup() == ESP_OK);
    assert(!disable_calls && !irq_free_calls);
    assert_released(false);
    puts("native USB HCD/hub lifecycle: PASS (retention, retry, ordering, stop failure)");
    return 0;
}
