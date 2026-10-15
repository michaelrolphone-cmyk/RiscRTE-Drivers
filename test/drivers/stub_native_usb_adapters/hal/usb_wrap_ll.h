#pragma once
#include <cassert>
#include <cstdint>
struct usb_wrap_test_mode_vals_t {bool tx_enable_n,tx_dp,tx_dm,rx_dp,rx_dm,rx_rcv;};
struct UsbWrap {struct {uint32_t val;} test_conf;};
extern UsbWrap USB_WRAP;
extern unsigned signal_calls,enable_calls;
static inline void usb_wrap_ll_phy_test_mode_set_signals(UsbWrap *hw,const usb_wrap_test_mode_vals_t *v) {
 assert(hw==&USB_WRAP && hw->test_conf.val==0);
 assert(v->tx_enable_n&&!v->tx_dp&&!v->tx_dm&&!v->rx_dp&&!v->rx_dm&&!v->rx_rcv);
 hw->test_conf.val=2; ++signal_calls;
}
static inline void usb_wrap_ll_phy_enable_test_mode(UsbWrap *hw,bool enable) {
 hw->test_conf.val=(hw->test_conf.val&~1u)|(enable?1u:0u);++enable_calls;
}
