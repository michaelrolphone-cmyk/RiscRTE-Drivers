#include "RiscUsbVbusV1.h"
#include <stddef.h>
#include <assert.h>
#define PREFIX_VALUES {sizeof(risc_usb_vbus_api_v1),sizeof(risc_usb_vbus_monitor_api_v1),sizeof(risc_usb_vbus_external_api_v1),offsetof(risc_usb_vbus_api_v1,acquire_host),offsetof(risc_usb_vbus_api_v1,release_host),offsetof(risc_usb_vbus_api_v1,quiesce),offsetof(risc_usb_vbus_monitor_api_v1,input_status),offsetof(risc_usb_vbus_monitor_api_v1,flags),offsetof(risc_usb_vbus_external_api_v1,acquire_external_host),offsetof(risc_usb_vbus_external_api_v1,external_host_valid)}
#ifdef OLD_VIEW
const size_t old_prefix_layout[]=PREFIX_VALUES;
#else
_Static_assert(offsetof(risc_usb_vbus_charger_api_v1,input_status)==offsetof(risc_usb_vbus_monitor_api_v1,input_status),"monitor prefix");
_Static_assert(offsetof(risc_usb_vbus_charger_api_v1,flags)==offsetof(risc_usb_vbus_monitor_api_v1,flags),"flags prefix");
_Static_assert(offsetof(risc_usb_vbus_charger_api_v1,acquire_external_host)==offsetof(risc_usb_vbus_external_api_v1,acquire_external_host),"external host prefix");
_Static_assert(offsetof(risc_usb_vbus_charger_api_v1,external_host_valid)==offsetof(risc_usb_vbus_external_api_v1,external_host_valid),"external valid prefix");
_Static_assert(offsetof(risc_usb_vbus_charger_api_v1,read_charger)==sizeof(risc_usb_vbus_external_api_v1),"append only charger tail");
static bool admit(const risc_usb_vbus_api_v1 *p) {
 return p && p->api_version==RISC_USB_VBUS_API_V1 && p->struct_size>=offsetof(risc_usb_vbus_charger_api_v1,request_shutdown)+sizeof(((risc_usb_vbus_charger_api_v1*)0)->request_shutdown);
}
extern const size_t old_prefix_layout[];
int main(void) {
 const size_t new_layout[]=PREFIX_VALUES;for(size_t i=0;i<sizeof(new_layout)/sizeof(new_layout[0]);++i)assert(new_layout[i]==old_prefix_layout[i]);
 risc_usb_vbus_api_v1 base={.api_version=RISC_USB_VBUS_API_V1,.struct_size=sizeof(base)};assert(!admit(&base));
 risc_usb_vbus_charger_api_v1 extended={.base={.api_version=RISC_USB_VBUS_API_V1,.struct_size=sizeof(extended)}};
 assert(admit(&extended.base));extended.base.struct_size--;assert(!admit(&extended.base));extended.base.struct_size=sizeof(extended);extended.base.api_version++;assert(!admit(&extended.base));assert(!admit(NULL));
}
#endif
