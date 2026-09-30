/* Reuse the same independently built HID ELFs and fake USB host used by the
 * disconnect regression, but exercise the distinct still-plugged shutdown. */
#define main hid_disconnect_test_main
#include "usb_hid_test.c"
#undef main

int main(int argc, char **argv) {
    assert(argc == 4);
    void *generic_elf = NULL, *keyboard_elf = NULL, *gamepad_elf = NULL;
    const risc_driver_v2 *generic = load(argv[1], &generic_elf);
    const risc_driver_v2 *keyboard = load(argv[2], &keyboard_elf);
    const risc_driver_v2 *gamepad = load(argv[3], &gamepad_elf);
    risc_provider_dependency_v1 host_dependency = {"usb.host", 1,
                                                  &fake_host.discovery.host};
    assert(generic->start(&host_dependency, 1));
    risc_provider_dependency_v1 hid_dependency = {"usb.hid", 1,
                                                  generic->capability};
    assert(keyboard->start(&hid_dependency, 1));
    risc_provider_dependency_v1 pad_deps[] = {hid_dependency, {"platform.clock", 1, &fake_clock}};
    assert(gamepad->start(pad_deps, 2));
    const risc_usb_keyboard_api_v1 *keys = keyboard->capability;
    const risc_usb_gamepad_api_v1 *pads = gamepad->capability;
    uint64_t key_subscription = keys->subscribe(keys->context, 0);
    uint64_t pad_subscription = pads->subscribe(pads->context, 0);
    assert(key_subscription && pad_subscription);
    assert(keys->poll(keys->context, 4));
    assert(pads->poll(pads->context, 4));
    assert(attached && claimed[0] && claimed[1] && !releases);
    assert(!keyboard->quiesce() && !gamepad->quiesce() && !generic->quiesce());
    /* A consuming app goes away while both peripherals remain attached. The
     * class must close its own interface before its parent ELF can unmap. */
    assert(keys->unsubscribe(keys->context, key_subscription));
    assert(keyboard->quiesce() && !claimed[0] && releases == 1);
    assert(!generic->quiesce()); /* The gamepad still owns its claim. */
    assert(pads->unsubscribe(pads->context, pad_subscription));
    assert(gamepad->quiesce() && !claimed[1] && releases == 2);
    assert(generic->quiesce());
    gamepad->stop(); keyboard->stop(); generic->stop();
    assert(!dlclose(gamepad_elf) && !dlclose(keyboard_elf) && !dlclose(generic_elf));
    puts("USB HID unplug-free shutdown: all claims and dependent ELFs released: PASS");
    return 0;
}
