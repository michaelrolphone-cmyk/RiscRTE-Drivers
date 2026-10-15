// Executes the actual private host lifecycle with fault-injected layer boundaries.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2, ESP_ERR_NOT_SUPPORTED=3, ESP_ERR_NO_MEM=4, FAULT=55 };
#define MALLOC_CAP_DEFAULT 0
#define HOST_CHECK(c,r) do { if (!(c)) return (r); } while(0)
#define HOST_CHECK_FROM_CRIT(c,r) HOST_CHECK(c,r)
#define HOST_ENTER_CRITICAL() ((void)0)
#define HOST_EXIT_CRITICAL() ((void)0)
#define TAILQ_INIT(p) (*(p)=0)
typedef struct {
 struct { unsigned process_pending_flags,lib_event_flags; struct {unsigned val;} flags; } dynamic;
 struct { int client_tailq; } mux_protected;
 struct { void *event_sem,*mux_lock,*enum_client,*hub_client; } constant;
} host_lib_t;
static host_lib_t *p_host_lib_obj;
typedef struct {bool skip_phy_setup; unsigned peripheral_map; bool root_port_unpowered,fsls_only; struct {unsigned rx_fifo_lines,nptx_fifo_lines,ptx_fifo_lines;} fifo_settings_custom; int intr_flags;} usb_host_config_t;
typedef void (*callback_t)(void);
typedef struct {callback_t proc_req_cb; void *proc_req_cb_arg; callback_t event_cb; void *event_cb_arg;} usbh_config_t;
typedef struct {callback_t proc_req_cb; void *proc_req_cb_arg; callback_t enum_event_cb; void *enum_event_cb_arg;} enum_config_t;
typedef struct {unsigned port_map; callback_t proc_req_cb; void *proc_req_cb_arg; callback_t event_cb; void *event_cb_arg; int intr_flags; void *fifo_config; bool fsls_only;} hub_config_t;
static void proc_req_callback(void) {} static void usbh_event_callback(void) {}
static void enum_event_callback(void) {} static void hub_event_callback(void) {}
enum {ALLOC, SEM1, SEM2, USBH_INSTALL, ENUM_INSTALL, HUB_INSTALL, ROOT_START, ROOT_STOP, HUB_UNINSTALL, HUB_RETRY, ENUM_UNINSTALL, USBH_UNINSTALL, NBOUND};
static int fail[NBOUND],calls[NBOUND],frees,sem_frees,sem_count;
static bool irq_live, failed_hub_irq;
static int boundary(int which) { ++calls[which]; return fail[which]?FAULT:ESP_OK; }
static void *heap_caps_calloc(size_t n,size_t s,int cap) { (void)cap; return boundary(ALLOC)?NULL:calloc(n,s); }
static void heap_caps_free(void *p) { ++frees; free(p); }
static void *new_sem(void) { int which=sem_count++?SEM2:SEM1; return boundary(which)?NULL:malloc(1); }
#define xSemaphoreCreateBinary() new_sem()
#define xSemaphoreCreateMutex() new_sem()
static void vSemaphoreDelete(void *p) { ++sem_frees; free(p); }
static int usbh_install(const usbh_config_t *c) { (void)c;return boundary(USBH_INSTALL); }
static int enum_install(enum_config_t *c,void **p) { (void)c;(void)p;return boundary(ENUM_INSTALL); }
static int hub_install(hub_config_t *c,void **p) { (void)c;(void)p;int r=boundary(HUB_INSTALL); if(r&&failed_hub_irq)irq_live=true; return r; }
static int hub_root_start(void) { return boundary(ROOT_START); }
static int hub_root_stop(void) { return boundary(ROOT_STOP); }
static int hub_uninstall(void) { return boundary(HUB_UNINSTALL); }
static int risc_usb_hub_retry_cleanup(void) { int r=boundary(HUB_RETRY); if(!r&&failed_hub_irq)irq_live=false;return r; }
static bool risc_usb_irq_live(void) { return irq_live; }
static int enum_uninstall(void) {return boundary(ENUM_UNINSTALL);}
static int usbh_uninstall(void) {return boundary(USBH_UNINSTALL);}
#include "../../Drivers/usb_controller_esp32s3/NativeHostLifecycle.inc"
static usb_host_config_t config={.skip_phy_setup=true,.peripheral_map=1};
static void reset(void) {
 assert(!risc_host&&!p_host_lib_obj&&!risc_cleanup&&!risc_usbh_owned&&!risc_enum_owned&&!risc_hub_owned);
 memset(fail,0,sizeof(fail));memset(calls,0,sizeof(calls)); frees=sem_frees=sem_count=0;irq_live=failed_hub_irq=false;
}
int main(void) {
 // Allocation failures and every install stage roll back only owned layers.
 for(int stage=ALLOC;stage<=ROOT_START;stage++) {
  reset();fail[stage]=1;
  assert(usb_host_install(&config)!=ESP_OK);
  assert(!risc_usb_cleanup_pending()&&!risc_host&&!p_host_lib_obj);
  assert(frees==(stage!=ALLOC));
  assert(calls[ENUM_UNINSTALL]==(stage>ENUM_INSTALL));
  assert(calls[USBH_UNINSTALL]==(stage>USBH_INSTALL));
 }
 // Every teardown boundary can fail repeatedly without losing parent ownership.
 for(int stage=ROOT_STOP;stage<NBOUND;stage++) {
  reset();assert(usb_host_install(&config)==ESP_OK);
  host_lib_t *owned=risc_host;fail[stage]=1;
  assert(usb_host_uninstall()==FAULT);
  assert(risc_usb_cleanup_pending()&&risc_host==owned&&p_host_lib_obj==owned&&frees==0);
  int hub_done=calls[HUB_UNINSTALL],enum_done=calls[ENUM_UNINSTALL];
  assert(usb_host_uninstall()==FAULT);
  if(stage>HUB_UNINSTALL)assert(calls[HUB_UNINSTALL]==hub_done);
  if(stage>ENUM_UNINSTALL)assert(calls[ENUM_UNINSTALL]==enum_done);
  fail[stage]=0;assert(risc_usb_retry_cleanup()==ESP_OK);
  assert(!risc_usb_cleanup_pending()&&!risc_host&&!p_host_lib_obj&&frees==1&&sem_frees==2);
  assert(risc_usb_retry_cleanup()==ESP_OK&&frees==1);
 }
 // Failed install never published p_host_lib_obj, but its latch retains mapping.
 reset();fail[HUB_INSTALL]=fail[HUB_RETRY]=1;failed_hub_irq=true;
 assert(usb_host_install(&config)==FAULT);
 assert(!p_host_lib_obj&&risc_host&&irq_live&&risc_usb_cleanup_pending()&&frees==0);
 assert(usb_host_install(&config)==ESP_ERR_INVALID_STATE);
 assert(risc_usb_retry_cleanup()==FAULT&&frees==0);
 fail[HUB_RETRY]=0;assert(risc_usb_retry_cleanup()==ESP_OK&&!irq_live&&frees==1);
 // A live IRQ with no normal layer handle independently prevents unloading.
 reset();assert(usb_host_install(&config)==ESP_OK);irq_live=true;
 assert(usb_host_uninstall()==ESP_ERR_INVALID_STATE&&risc_usb_cleanup_pending()&&frees==0);
 irq_live=false;assert(risc_usb_retry_cleanup()==ESP_OK&&frees==1);
 // Active event processing is rejected before teardown begins.
 reset();assert(usb_host_install(&config)==ESP_OK);p_host_lib_obj->dynamic.process_pending_flags=1;
 assert(usb_host_uninstall()==ESP_ERR_INVALID_STATE&&!risc_usb_cleanup_pending());
 p_host_lib_obj->dynamic.process_pending_flags=0;assert(usb_host_uninstall()==ESP_OK);
 reset();config.skip_phy_setup=false;assert(usb_host_install(&config)==ESP_ERR_NOT_SUPPORTED&&calls[ALLOC]==0);
 puts("native USB host lifecycle: PASS");
}
