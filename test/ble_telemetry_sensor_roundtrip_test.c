/* Cross-contract qualification: actual public telemetry bytes consumed by the
 * generic passive sensor parser. No peer-selection or radio policy is added. */
#include "../Drivers/ble_telemetry/codec.h"
#include "../Drivers/ble_sensors/ble_scan_core.h"
#include <assert.h>
#include <stdio.h>

static void ready(ble_scan *scan) {
    ble_begin(scan);
    for (unsigned i=0;i<5;i++) {
        uint8_t command[16];size_t n=ble_command(scan,command,10+i);
        assert(n>=3);
        uint8_t ack[]={0x0e,4,1,command[0],command[1],0};
        ble_event(scan,4,ack,sizeof(ack),10+i);
    }
    assert(scan->phase==BLE_SCANNING&&!ble_expired(scan,100));
}
static void receive(ble_scan *scan,uint8_t peer,uint8_t type,
                    const uint8_t *ad,uint8_t length,uint32_t now) {
    assert(length<=31);
    uint8_t event[45]={0x3e,(uint8_t)(12u+length),2,1,3,type,
                      peer,2,3,4,5,0xc6,length};
    memcpy(event+13,ad,length);event[13+length]=(uint8_t)-40;
    ble_event(scan,4,event,14u+length,now);
}
static uint8_t encode(uint32_t metric,int32_t value,uint8_t ad[31]) {
    risc_telemetry_field_v1 field={37,metric,"User source label"};
    uint8_t length=0;
    assert(telemetry_encode(&field,&value,1,ad,&length));return length;
}
int main(void) {
    ble_scan scan;ready(&scan);uint8_t ad[31],length;
    for (int32_t value=0;value<=100;value++) {
        length=encode(RISC_TELEMETRY_BATTERY_PERCENT,value,ad);
        receive(&scan,1,1,ad,length,200+(uint32_t)value);
        assert(scan.count==1);
        const ble_device *d=&scan.devices[0];
        assert(d->bthome&&d->bthome_version==2&&!d->encrypted);
        assert(!d->measurement_invalid&&!d->measurement_partial);
        assert(d->has_battery&&d->battery==value&&d->reading_count==1);
        assert(d->readings[0].metric==RISC_TELEMETRY_BATTERY_PERCENT&&d->readings[0].value==value);
        assert(d->measurement_seen==200+(uint32_t)value&&d->seen==d->measurement_seen);
        assert(!d->name[0]&&!d->has_company); /* IDs and labels are not wire identity. */
    }
    length=encode(RISC_TELEMETRY_BATTERY_PERCENT,73,ad);
    receive(&scan,2,1,ad,length,400); /* New publisher session address: new peer. */
    receive(&scan,2,0,ad,length,401); /* Address type is part of observed identity. */
    assert(scan.count==3&&scan.devices[0].battery==100&&scan.devices[1].battery==73);
    assert(scan.devices[1].address_type==1&&scan.devices[2].address_type==0);
    assert(!scan.devices[1].name[0]);

    uint32_t measured=scan.devices[1].measurement_seen;
    const uint8_t flags[]={2,1,6};receive(&scan,2,1,flags,sizeof(flags),500);
    assert(scan.devices[1].seen==500&&scan.devices[1].measurement_seen==measured);
    assert(scan.devices[1].battery==73); /* Presence alone is not a new reading. */

    length=encode(RISC_TELEMETRY_TEMPERATURE_CENTIC,2150,ad);
    receive(&scan,2,1,ad,length,600);
    assert(!scan.devices[1].has_battery&&scan.devices[1].has_temperature);
    assert(scan.devices[1].temperature==2150&&scan.devices[1].measurement_seen==600);

    length=encode(RISC_TELEMETRY_BATTERY_PERCENT,73,ad);ad[length-1]=101;
    receive(&scan,2,1,ad,length,700);
    assert(scan.devices[1].measurement_invalid&&!scan.devices[1].has_battery);
    assert(scan.devices[1].reading_count==0);
    length=encode(RISC_TELEMETRY_BATTERY_PERCENT,73,ad);ad[3]--;length--;
    receive(&scan,2,1,ad,length,701);
    assert(scan.devices[1].measurement_invalid&&!scan.devices[1].has_battery);

    length=encode(RISC_TELEMETRY_BATTERY_PERCENT,73,ad);ad[7]|=1;
    receive(&scan,2,1,ad,length,702);
    assert(scan.devices[1].encrypted&&!scan.devices[1].has_battery);

    risc_telemetry_field_v1 fields[]={{11,RISC_TELEMETRY_BATTERY_PERCENT,"First"},
                                    {22,RISC_TELEMETRY_BATTERY_PERCENT,"Second"}};
    int32_t values[]={17,83};
    assert(telemetry_encode(fields,values,2,ad,&length));receive(&scan,3,1,ad,length,800);
    const ble_device *two=&scan.devices[3];
    assert(two->reading_count==2&&two->readings[0].value==17&&two->readings[1].value==83);
    assert(two->has_battery&&two->battery==17&&!two->name[0]);
    /* Multiple battery readings carry metric/value, not their source IDs or
     * labels. This test deliberately does not select one as a user's Watch. */
    /* Duplicate service-data fields must not silently choose either sample.
     * Exercise valid, encrypted and malformed payloads in both orders. */
    const uint8_t duplicate[][14]={
        {6,0x16,0xd2,0xfc,0x40,1,40,6,0x16,0xd2,0xfc,0x40,1,80},
        {6,0x16,0xd2,0xfc,0x40,1,40,6,0x16,0xd2,0xfc,0x41,1,80},
        {6,0x16,0xd2,0xfc,0x41,1,40,6,0x16,0xd2,0xfc,0x40,1,80},
        {6,0x16,0xd2,0xfc,0x40,1,40,6,0x16,0xd2,0xfc,0x40,1,101},
        {6,0x16,0xd2,0xfc,0x40,1,101,6,0x16,0xd2,0xfc,0x40,1,80}
    };
    for(unsigned i=0;i<sizeof(duplicate)/sizeof(duplicate[0]);i++) {
        receive(&scan,2,1,duplicate[i],sizeof(duplicate[i]),900+i);
        assert(scan.devices[1].measurement_invalid&&!scan.devices[1].has_battery);
        assert(!scan.devices[1].has_temperature&&!scan.devices[1].has_humidity);
        assert(scan.devices[1].reading_count==0&&scan.devices[1].measurement_seen==900+i);
    }
    /* A subsequent unambiguous sample is still accepted. */
    length=encode(RISC_TELEMETRY_BATTERY_PERCENT,61,ad);receive(&scan,2,1,ad,length,910);
    assert(!scan.devices[1].measurement_invalid&&scan.devices[1].has_battery&&scan.devices[1].battery==61);
    assert(ble_expired(&scan,16000)&&scan.phase==BLE_COMPLETE);
    puts("Telemetry-to-sensor round trip: all 101 percentages, observed identity, stale measurement, absent/invalid/encrypted data and multiple battery fields/duplicate service-data rejection passed");
    return 0;
}
