#include <initializer_list>
#include <cassert>
#include <cstdio>
#include "../../Drivers/usb_controller_esp32s3/NativePhyGate.h"
#if ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(6,1,0)
#include "../../Drivers/usb_controller_esp32s3/NativeDmaAlignment.h"
UsbWrap USB_WRAP={}; unsigned signal_calls,enable_calls;
int main(){
 auto handle=reinterpret_cast<void *>(1);
 USB_WRAP.test_conf.val=0xabcdef00;
 assert(risc_usb_phy_connection(nullptr,false)==ESP_ERR_INVALID_ARG);
 assert(USB_WRAP.test_conf.val==0xabcdef00&&!signal_calls&&!enable_calls);
 assert(risc_usb_phy_connection(handle,false)==ESP_OK);
 assert(USB_WRAP.test_conf.val==3&&signal_calls==1&&enable_calls==1);
 USB_WRAP.test_conf.val|=0x80000000;
 assert(risc_usb_phy_connection(handle,true)==ESP_OK);
 assert(USB_WRAP.test_conf.val==0x80000002&&signal_calls==1&&enable_calls==2);
 assert(risc_usb_phy_connection(handle,false)==ESP_OK&&USB_WRAP.test_conf.val==3&&signal_calls==2);
 size_t result=99;
 constexpr unsigned caps=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL|MALLOC_CAP_CACHE_ALIGNED;
 assert(risc_usb_internal_dma_alignment(caps,&result)==ESP_OK&&result==0);
 result=99;
 for(unsigned bad:{0u,MALLOC_CAP_DMA,MALLOC_CAP_INTERNAL,caps|MALLOC_CAP_SPIRAM}) {
  assert(risc_usb_internal_dma_alignment(bad,&result)==ESP_ERR_INVALID_ARG&&result==99);
 }
 assert(risc_usb_internal_dma_alignment(caps,nullptr)==ESP_ERR_INVALID_ARG);
 puts("native USB PHY/DMA adapters: PASS");
}
#else
static int action=-1,calls;
esp_err_t usb_phy_action(usb_phy_handle_t h,int a){++calls;action=a;return h?ESP_OK:ESP_ERR_INVALID_ARG;}
int main(){auto h=reinterpret_cast<void *>(1);assert(risc_usb_phy_connection(h,false)==ESP_OK&&action==USB_PHY_ACTION_HOST_FORCE_DISCONN);assert(risc_usb_phy_connection(h,true)==ESP_OK&&action==USB_PHY_ACTION_HOST_ALLOW_CONN);assert(risc_usb_phy_connection(nullptr,false)==ESP_ERR_INVALID_ARG&&calls==3);puts("legacy USB PHY adapter: PASS");}
#endif
