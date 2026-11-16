#include "ClaimReleasePolicy.h"
#include <cassert>
#include <cstdio>

int main() {
    using RiscUsbController::releaseClaim;
    bool released = false;
    int interfaces = 0, closes = 0;
    bool interfaceFails = true, closeFails = true;
    auto release = [&]() -> bool { ++interfaces; return !interfaceFails; };
    auto close = [&]() -> bool { ++closes; return !closeFails; };

    // An interface error cannot advance the transaction to device close.
    assert(!releaseClaim(released, true, false, release, close));
    assert(!released && interfaces == 1 && closes == 0);
    interfaceFails = false;
    assert(!releaseClaim(released, true, false, release, close));
    assert(released && interfaces == 2 && closes == 1);
    // Physical close failed after interface release: retry only close, not a
    // second usb_host_interface_release on an already released interface.
    closeFails = false;
    assert(releaseClaim(released, true, false, release, close));
    assert(released && interfaces == 2 && closes == 2);

    // Attached devices and detached devices with another live claim must
    // release only their interface; their device handle remains owned.
    released = false; interfaces = closes = 0;
    assert(releaseClaim(released, false, false, release, close));
    assert(released && interfaces == 1 && closes == 0);
    released = false; interfaces = closes = 0;
    assert(releaseClaim(released, true, true, release, close));
    assert(released && interfaces == 1 && closes == 0);

    std::puts("USB controller staged claim release and retry: PASS");
}
