/* Read-only AXP2101 telemetry. Register semantics retained from the pinned
 * T-Watch-S3 851b8998 driver read_sample, never its rail/charger initialization.
 * No gauge enable/reset, IRQ ACK, charging policy or rail write is authorized.
 * A fresh scoped transaction set is required for every result; no stale cache.
 */
#include "DriverSupport.h"
#include "RiscBatteryGaugeV1.h"
#include "TWatchHardwareV1.h"
#include <string.h>
static const risc_i2c_bus_api_v1 *bus;
static const tw_hw_i2c_device_v1 *config;
static uint64_t token;
static bool active,closing;
static bool close_session(void) {
    if (!token) return true;
    if (!bus || !bus->release_device(bus->context,token)) { closing=true; return false; }
    token=0; return true;
}
static bool reg(uint8_t address,void *out,size_t count) {
    return bus->transact(bus->context,token,&address,1,out,count,20);
}
static bool read_sample(void *context,risc_battery_sample_v1 *out) {
    (void)context;
    if (!out) return false;
    *out=(risc_battery_sample_v1){0,255,RISC_BATTERY_PROFILE_MISSING};
    if (!active || closing || token) return false;
    bool claimed=bus->claim_device(bus->context,config->address,&token);
    /* A rejected zero-token claim is ordinary scoped-owner contention.
     * Only a retained token or an invalid success requires fail-closed cleanup. */
    if (!claimed || !token) { if (claimed || token) closing=true; return false; }
    uint8_t id=0,raw[2]={0},status[2]={0},gauge[2]={0},detect=0,control=0,percent=255;
    bool ok=reg(3,&id,1) && id==config->chip_id &&
        reg(0x34,raw,2) && reg(0,status,2) && reg(0x17,gauge,2) &&
        reg(0x68,&detect,1) && reg(0xa2,&control,1) && reg(0xa4,&percent,1);
    bool released=close_session();
    if (!ok || !released) return false;
    risc_battery_sample_v1 sample={0,255,RISC_BATTERY_PROFILE_MISSING};
    /* VBAT is a PMIC ADC observation, not externally measured cell voltage. */
    sample.millivolts=(uint16_t)(((uint16_t)(raw[0]&0x3f)<<8)|raw[1]);
    if ((status[1]&0x60)==0x20) sample.flags|=RISC_BATTERY_CHARGING;
    if ((status[0]&8) && (detect&1) && (gauge[1]&8) && !(gauge[0]&12) && !(control&1) && percent<=100) {
        sample.percent=percent;sample.flags&=(uint8_t)~RISC_BATTERY_PROFILE_MISSING;
    }
    /* Layout-preserving flags from PortablePowerStatus at Watch 605d5e7d.
     * Bit2 gates status meaning; these are observations, never charge controls. */
    if (detect&1) sample.flags|=1u<<2;
    if (status[0]&0x20) sample.flags|=1u<<3;
    if (gauge[1]&2) sample.flags|=1u<<4;
    if ((status[0]&0x28)==0x28 && (status[1]&7)==4) sample.flags|=1u<<5;
    if (status[0]&8) sample.flags|=1u<<6;
    if (status[0]&2) sample.flags|=1u<<7;
    *out=sample;return true;
}
static bool quiesce(void) { closing=true;return close_session(); }
static void stop(void) {
    if (!quiesce()) return;
    active=closing=false;bus=NULL;config=NULL;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
    if (bus || token || active || closing) return false;
    const tw_hw_i2c_device_v1 *c=tw_config(deps,count,"x-powers,axp2101","peripheral.i2c",sizeof(*c));
    const risc_i2c_bus_api_v1 *b=tw_dep(deps,count,"i2c.bus",sizeof(*b));
    if (!c || c->address!=0x34 || c->chip_id!=0x4a || !tw_bus_limit(&c->bus,RISC_HW_BUS_I2C,400000) || !tw_i2c_valid(b)) return false;
    config=c;bus=b;active=true;
    risc_battery_sample_v1 sample;
    return read_sample(NULL,&sample);
}
static const risc_battery_gauge_api_v1 api={1,sizeof(api),NULL,read_sample};
static const risc_driver_v2 driver={2,sizeof(driver),"axp2101-observer",RISC_BATTERY_GAUGE_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) {return abi==2?&driver:NULL;}
