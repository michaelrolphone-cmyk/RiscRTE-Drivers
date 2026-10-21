#pragma once

/* Controller-private transaction. A successful interface release must never
 * be repeated if a later device-close fails. The owning claim slot remains
 * live until all applicable physical cleanup has been acknowledged. */
namespace RiscUsbController {
template <typename ReleaseInterface, typename CloseDevice>
bool releaseClaim(bool& interfaceReleased, bool detached, bool hasOtherClaims,
                  ReleaseInterface releaseInterface, CloseDevice closeDevice) {
    if (!interfaceReleased) {
        if (!releaseInterface()) return false;
        interfaceReleased = true;
    }
    if (detached && !hasOtherClaims && !closeDevice()) return false;
    return true;
}
}
