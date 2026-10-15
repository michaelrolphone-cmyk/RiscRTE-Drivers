#pragma once
#include <esp_err.h>
typedef void *usb_phy_handle_t;
enum {USB_PHY_ACTION_HOST_ALLOW_CONN=0,USB_PHY_ACTION_HOST_FORCE_DISCONN=1};
esp_err_t usb_phy_action(usb_phy_handle_t,int);
