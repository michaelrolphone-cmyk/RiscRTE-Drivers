#pragma once

// USB Serial/JTAG is RiscRTE's boot console. The controller ELF temporarily
// takes the shared internal PHY; retain the previous mux selection until both
// the host and VBUS have safely stopped, including a failed power acquisition.
static bool phyRouteCaptured, savedPhyOverride, savedPhySelect;
void capture_phy_route() {
    savedPhyOverride = RTCCNTL.usb_conf.sw_hw_usb_phy_sel;
    savedPhySelect = RTCCNTL.usb_conf.sw_usb_phy_sel;
    phyRouteCaptured = true;
}
void restore_phy_route() {
    if (!phyRouteCaptured) return;
    RTCCNTL.usb_conf.sw_usb_phy_sel = savedPhySelect;
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel = savedPhyOverride;
    phyRouteCaptured = false;
}
