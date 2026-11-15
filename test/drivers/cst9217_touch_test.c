#include "cst9217_test_model.h"
#include <stdio.h>
extern const risc_driver_v2 *t5_driver_get(uint32_t);
static risc_touch_event_v1 event(const risc_touch_api_v1 *a,uint64_t token,unsigned kind) {
    risc_touch_event_v1 out;assert(a->next(NULL,token,&out)==1&&out.kind==kind);return out;
}
int main(void) {
    const risc_driver_v2 *d=t5_driver_get(2);assert(d&&!t5_driver_get(1));
    const risc_touch_api_v1 *a=d->capability;cst_test_model m;cst_test_setup(&m);m.touch=a;
    assert(!a->subscribe(NULL));assert(d->start(m.dependencies,6));assert(!d->start(m.dependencies,6));
    uint64_t s=a->subscribe(NULL),second=a->subscribe(NULL);assert(s&&second&&s!=second);
    assert(!d->quiesce());d->stop();cst_test_owned(&m);
    risc_touch_snapshot_v1 snap;assert(a->snapshot(NULL,&snap)&&snap.width==466&&snap.height==466&&!snap.contact_count);
    assert(!a->poll(NULL,0)&&!a->poll(NULL,17));
    cst_test_report(&m,465,465,true);m.reenter=true;assert(a->poll(NULL,1));
    risc_touch_event_v1 e=event(a,s,RISC_TOUCH_EVENT_DOWN);assert(e.x==465&&e.y==465);
    event(a,second,RISC_TOUCH_EVENT_DOWN);assert(a->snapshot(NULL,&snap)&&snap.contact_count==1&&snap.contacts[0].x==465);
    assert(a->poll(NULL,16));assert(a->next(NULL,s,&e)==0);
    cst_test_report(&m,123,234,true);assert(a->poll(NULL,1));e=event(a,s,RISC_TOUCH_EVENT_MOVE);assert(e.x==123&&e.y==234);
    cst_test_report(&m,0,0,false);assert(a->poll(NULL,1));e=event(a,s,RISC_TOUCH_EVENT_UP);assert(e.x==123&&e.y==234);
    for(unsigned bad=0;bad<5;bad++) {
        cst_test_report(&m,20,30,true);
        if(bad==0)m.raw[6]=0;
        if(bad==1)m.raw[5]=2;
        if(bad==2)cst_test_report(&m,466,30,true);
        if(bad==3)cst_test_fail_at(&m,CST_TEST_TRANSFER,2,CST_TEST_FAIL);
        if(bad==4)cst_test_fail_at(&m,CST_TEST_CLOCK,1,CST_TEST_FAIL);
        assert(!a->poll(NULL,1));assert(!a->snapshot(NULL,&snap));assert(a->next(NULL,s,&e)==-1);
        m.fault=CST_TEST_OK;cst_test_report(&m,20,30,true);assert(a->poll(NULL,1));while(a->next(NULL,s,&e)>0){}
    }
    for(unsigned n=0;n<RISC_TOUCH_QUEUE_LENGTH+4;n++){cst_test_report(&m,n,20,true);assert(a->poll(NULL,1));}
    assert(a->next(NULL,s,&e)==-1);assert(a->snapshot(NULL,&snap)&&snap.contact_count==1);
    assert(a->unsubscribe(NULL,s)&&a->unsubscribe(NULL,second));
    m.fail_bus_release=true;assert(!d->quiesce());d->stop();cst_test_owned(&m);assert(m.bus_token);
    assert(!a->snapshot(NULL,&snap)&&!a->poll(NULL,1)&&!a->subscribe(NULL));
    m.fail_bus_release=false;assert(d->quiesce());d->stop();cst_test_clean(&m);
    assert(d->start(m.dependencies,6));uint64_t fresh=a->subscribe(NULL);assert(fresh>second);assert(!a->unsubscribe(NULL,s));
    assert(a->unsubscribe(NULL,fresh));assert(d->quiesce());d->stop();cst_test_clean(&m);
    puts("CST9217: scoped reset lifetime, STOP/delay protocol, coordinates, fanout/gaps, owner reentrancy and stream lifecycle PASS");
}
