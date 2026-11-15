#include "cst9217_test_model.h"
#include <stdio.h>
extern const risc_driver_v2 *t5_driver_get(uint32_t);
static risc_touch_event_v1 event(const risc_touch_api_v1 *a,uint64_t token,unsigned kind) {
    risc_touch_event_v1 out;assert(a->next(NULL,token,&out)==1&&out.kind==kind);return out;
}
static void idle_cycles(const risc_driver_v2 *d,const risc_touch_api_v1 *a) {
    cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    const uint64_t bus=m.bus_token, reset=m.reset_token;
    for(unsigned n=0;n<24;n++) {
        uint64_t one=a->subscribe(NULL),two=a->subscribe(NULL);assert(one&&two);
        cst_test_report(&m,10,20,true);assert(a->poll(NULL,1));
        assert(a->unsubscribe(NULL,one)&&!m.asleep);
        assert(a->unsubscribe(NULL,two)&&m.asleep&&m.sleeps==n+1);
        unsigned transfers=m.counts[CST_TEST_TRANSFER];risc_touch_snapshot_v1 snap;
        assert(!a->poll(NULL,1)&&!a->snapshot(NULL,&snap));
        assert(m.counts[CST_TEST_TRANSFER]==transfers);
        assert(m.bus_token==bus&&m.reset_token==reset);
    }
    assert(m.wake_resets==23);assert(d->quiesce());cst_test_clean(&m);
    puts("CST9217: last unsubscribe sleeps, first subscribe resets/reprobes, 24 cycles retain scoped custody PASS");
}
static void idle_faults(const risc_driver_v2 *d,const risc_touch_api_v1 *a) {
    /* Every sleep transfer must fail closed, including a command that acted
     * but returned false. The last token remains the cleanup obligation. */
    for(unsigned nth=1;nth<=8;nth++)for(unsigned acted=0;acted<2;acted++) {
        if(acted&&(nth==4||nth==7))continue; /* Read-only transfers. */
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        uint64_t token=a->subscribe(NULL);assert(token);
        cst_test_fail_at(&m,CST_TEST_TRANSFER,nth,acted?CST_TEST_FALSE_RETAINED:CST_TEST_FAIL);
        if(nth<=4){
            assert(a->unsubscribe(NULL,token)&&m.asleep);assert(d->quiesce());cst_test_clean(&m);continue;
        }
        assert(!a->unsubscribe(NULL,token));assert(!d->quiesce());
        unsigned transfers=m.counts[CST_TEST_TRANSFER];risc_touch_snapshot_v1 snap;
        assert(!a->poll(NULL,1)&&!a->snapshot(NULL,&snap));
        assert(m.counts[CST_TEST_TRANSFER]==transfers&&m.bus_token&&m.reset_token);
        m.fault=CST_TEST_OK;assert(a->unsubscribe(NULL,token)&&m.asleep);
        assert(m.wake_resets==1);assert(!a->unsubscribe(NULL,token));
        assert(d->quiesce());cst_test_clean(&m);
    }
    for(unsigned which=0;which<4;which++) {
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        uint64_t token=a->subscribe(NULL);assert(token);
        if(which==0)m.wrong_mode=true;
        else if(which==1)m.stalled_delay=true;
        else cst_test_fail_at(&m,CST_TEST_CLOCK,which-1,CST_TEST_FAIL);
        assert(!a->unsubscribe(NULL,token));m.fault=CST_TEST_OK;m.wrong_mode=false;m.stalled_delay=false;
        assert(a->unsubscribe(NULL,token));assert(d->quiesce());cst_test_clean(&m);
    }
    /* Quiesce with no subscribers also sleeps, and retains all custody on
     * failure instead of silently leaving the powered controller scanning. */
    cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    cst_test_fail_at(&m,CST_TEST_TRANSFER,8,CST_TEST_FALSE_RETAINED);
    assert(!d->quiesce()&&m.asleep&&m.bus_token&&m.reset_token&&m.sync_token);
    m.fault=CST_TEST_OK;m.fail_bus_release=true;assert(!d->quiesce());
    assert(m.bus_token&&m.reset_token&&m.asleep);m.fail_bus_release=false;
    assert(d->quiesce());cst_test_clean(&m);
    cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    uint64_t retained=a->subscribe(NULL);assert(retained);m.fail_sync_unlock=true;
    assert(!a->unsubscribe(NULL,retained)&&m.asleep&&m.sync_held);
    assert(!d->quiesce());m.fail_sync_unlock=false;
    assert(a->unsubscribe(NULL,retained));assert(d->quiesce());cst_test_clean(&m);
    puts("CST9217: every sleep transfer/mode/delay fault, acted-but-false sleep and retained cleanup PASS");
}
static void wake_faults(const risc_driver_v2 *d,const risc_touch_api_v1 *a) {
    static const struct {enum cst_test_operation op;unsigned nth;enum cst_test_fault fault;} cases[]={
        {CST_TEST_RESET_WRITE,1,CST_TEST_FAIL},{CST_TEST_RESET_WRITE,2,CST_TEST_FAIL},
        {CST_TEST_RESET_WRITE,1,CST_TEST_FALSE_RETAINED},{CST_TEST_RESET_WRITE,2,CST_TEST_FALSE_RETAINED},
        {CST_TEST_RESET_READ,1,CST_TEST_FAIL},{CST_TEST_RESET_READ,2,CST_TEST_FAIL},
        {CST_TEST_RESET_READ,1,CST_TEST_WRONG},{CST_TEST_RESET_READ,2,CST_TEST_WRONG},
        {CST_TEST_RESET_DELAY,1,CST_TEST_FAIL},{CST_TEST_RESET_DELAY,2,CST_TEST_FAIL},
        {CST_TEST_CLOCK,1,CST_TEST_FAIL},{CST_TEST_CLOCK,2,CST_TEST_FAIL},
        {CST_TEST_CLOCK,3,CST_TEST_FAIL},{CST_TEST_CLOCK,4,CST_TEST_FAIL},
        {CST_TEST_CLOCK,5,CST_TEST_FAIL},
        {CST_TEST_TRANSFER,1,CST_TEST_FAIL},{CST_TEST_TRANSFER,2,CST_TEST_FAIL},
        {CST_TEST_TRANSFER,3,CST_TEST_FAIL},{CST_TEST_TRANSFER,4,CST_TEST_FAIL},
        {CST_TEST_TRANSFER,5,CST_TEST_FAIL},{CST_TEST_TRANSFER,6,CST_TEST_FAIL},
        {CST_TEST_TRANSFER,7,CST_TEST_FAIL},{CST_TEST_TRANSFER,8,CST_TEST_FAIL}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        uint64_t old=a->subscribe(NULL);assert(old&&a->unsubscribe(NULL,old)&&m.asleep);
        cst_test_fail_at(&m,cases[i].op,cases[i].nth,cases[i].fault);m.reenter=true;
        if(cases[i].op==CST_TEST_RESET_DELAY){
            uint64_t fresh=a->subscribe(NULL);assert(fresh>old&&!m.asleep);
            assert(a->unsubscribe(NULL,fresh));assert(d->quiesce());cst_test_clean(&m);continue;
        }
        assert(!a->subscribe(NULL));unsigned transfers=m.counts[CST_TEST_TRANSFER];
        risc_touch_snapshot_v1 snap;assert(!a->poll(NULL,1)&&!a->snapshot(NULL,&snap));
        assert(m.counts[CST_TEST_TRANSFER]==transfers&&m.bus_token&&m.reset_token);
        m.fault=CST_TEST_OK;uint64_t fresh=a->subscribe(NULL);assert(fresh>old&&!m.asleep);
        assert(m.counts[CST_TEST_RESET_CLAIM]==1&&m.counts[CST_TEST_TOUCH_CLAIM]==1);
        cst_test_report(&m,40,50,true);assert(a->poll(NULL,1));event(a,fresh,RISC_TOUCH_EVENT_DOWN);
        assert(a->unsubscribe(NULL,fresh));assert(d->quiesce());cst_test_clean(&m);
    }
    cst_test_model baseline;cst_test_setup(&baseline);baseline.touch=a;assert(d->start(baseline.dependencies,6));
    uint64_t old=a->subscribe(NULL);assert(old&&a->unsubscribe(NULL,old));
    const unsigned begun=baseline.counts[CST_TEST_CLOCK];uint64_t next=a->subscribe(NULL);assert(next);
    const unsigned wake_clock_calls=baseline.counts[CST_TEST_CLOCK]-begun;
    assert(a->unsubscribe(NULL,next)&&d->quiesce());cst_test_clean(&baseline);
    for(unsigned nth=1;nth<=wake_clock_calls;nth++){
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        old=a->subscribe(NULL);assert(old&&a->unsubscribe(NULL,old));
        cst_test_fail_at(&m,CST_TEST_CLOCK,nth,CST_TEST_FAIL);assert(!a->subscribe(NULL));
        assert(m.bus_token&&m.reset_token);m.fault=CST_TEST_OK;
        next=a->subscribe(NULL);assert(next>old&&a->unsubscribe(NULL,next)&&d->quiesce());cst_test_clean(&m);
    }
    printf("CST9217: every %u warm reset/probe clock observation fault retains custody and retries PASS\n",wake_clock_calls);
    puts("CST9217: warm reset/readback/timing/clock/probe faults retry without claim loss or stale reports PASS");
}
static void handoff_retries(const risc_driver_v2 *d,const risc_touch_api_v1 *a) {
    cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    const uint64_t bus=m.bus_token,reset=m.reset_token;
    for(unsigned n=0;n<24;n++) {
        uint64_t token=a->subscribe(NULL);assert(token);
        cst_test_report(&m,40,60,true);assert(a->poll(NULL,1));
        m.command_busy_transfers=1+n%2;
        const uint64_t begun=m.clock_ms;
        assert(a->unsubscribe(NULL,token)&&m.asleep);
        assert(m.clock_ms-begun>=200*(1+n%2)&&m.clock_ms-begun<1000);
        assert(m.bus_token==bus&&m.reset_token==reset);
        assert(!a->unsubscribe(NULL,token));
    }
    assert(d->quiesce());cst_test_clean(&m);
    cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    uint64_t old=a->subscribe(NULL);assert(old);m.command_busy_transfers=3;
    const uint64_t begun=m.clock_ms;
    assert(!a->unsubscribe(NULL,old)&&m.command_busy_transfers==0&&!m.asleep);
    assert(m.clock_ms-begun>=400&&m.clock_ms-begun<1000&&!d->quiesce());
    assert(m.bus_token&&m.reset_token); // Never forget the outgoing owner.
    assert(a->unsubscribe(NULL,old)&&m.asleep);assert(d->quiesce());cst_test_clean(&m);
    cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    old=a->subscribe(NULL);assert(old);
    cst_test_fail_at(&m,CST_TEST_RESET_DELAY,1,CST_TEST_FAIL);
    assert(a->unsubscribe(NULL,old)&&m.asleep);assert(d->quiesce());cst_test_clean(&m);
    puts("CST9217: 24 transient-busy handoffs; bounded exhausted retry retains original token; early scheduler wake completes measured settle PASS");
}
static void stream_fault_classification(const risc_driver_v2 *d,const risc_touch_api_v1 *a) {
    risc_touch_event_v1 e={0};
    /* No provider state is a fault, not a usable stale-handle/GAP result. */
    assert(a->next(NULL,1,&e)==-2);
    for(unsigned kind=0;kind<4;kind++) {
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        uint64_t token=a->subscribe(NULL);assert(token);
        assert(a->next(NULL,0,&e)==-1&&a->next(NULL,token+1,&e)==-1);
        assert(a->next(NULL,token,NULL)==-2);
        if(kind==0) {cst_test_report(&m,10,20,true);assert(a->poll(NULL,1));}
        if(kind==2) {cst_test_report(&m,10,20,true);m.raw[6]=0;assert(!a->poll(NULL,1));}
        /* Exercise every prior outer result: event, empty, GAP, stale token. */
        m.fail_sync_unlock=true;
        assert(a->next(NULL,kind==3?token+1:token,&e)==-2);
        assert(m.sync_held&&m.bus_token&&m.reset_token);
        unsigned counts[CST_TEST_OP_COUNT];memcpy(counts,m.counts,sizeof(counts));
        assert(a->next(NULL,token,&e)==-2);
        assert(!memcmp(counts,m.counts,sizeof(counts))); /* zero callbacks */
        m.fail_sync_unlock=false;
        /* Only the existing owner cleanup retries the retained guard. */
        assert(a->unsubscribe(NULL,token));assert(d->quiesce());cst_test_clean(&m);
        assert(a->next(NULL,token,&e)==-2);
    }
    for(unsigned malformed=0;malformed<2;malformed++) {
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        uint64_t token=a->subscribe(NULL),other=a->subscribe(NULL);assert(token&&other);
        cst_test_report(&m,20,30,true);
        if(malformed)m.raw[6]=0;
        m.fail_sync_unlock=true;
        assert(a->poll(NULL,1)==!malformed);
        unsigned counts[CST_TEST_OP_COUNT];memcpy(counts,m.counts,sizeof(counts));
        assert(a->next(NULL,token,&e)==-2);
        assert(!memcmp(counts,m.counts,sizeof(counts))&&m.sync_held&&m.bus_token&&m.reset_token);
        m.fail_sync_unlock=false;
        /* After explicit owner retry has released the guard, ready=false is
         * still a provider fault, not a transient report discontinuity. */
        assert(a->unsubscribe(NULL,token));
        assert(a->next(NULL,other,&e)==-2);
        assert(a->unsubscribe(NULL,other));assert(d->quiesce());cst_test_clean(&m);
    }
    /* Snapshot must not report success after losing custody on unlock. Its
     * boolean refusal is classified by next without any dependency retry. */
    for(unsigned invalid=0;invalid<2;invalid++) {
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        uint64_t token=a->subscribe(NULL);assert(token);
        if(invalid){cst_test_report(&m,10,20,true);m.raw[6]=0;assert(!a->poll(NULL,1));}
        risc_touch_snapshot_v1 snap;
        m.fail_sync_unlock=true;assert(!a->snapshot(NULL,&snap));
        unsigned counts[CST_TEST_OP_COUNT];memcpy(counts,m.counts,sizeof(counts));
        assert(a->next(NULL,token,&e)==-2);
        assert(!memcmp(counts,m.counts,sizeof(counts))&&m.sync_held);
        m.fail_sync_unlock=false;
        assert(a->unsubscribe(NULL,token));assert(d->quiesce());cst_test_clean(&m);
    }
    /* Subscribe/poll can have returned a value before their unlock failure.
     * Snapshot must not retry that retained guard; typed next classifies it. */
    for(unsigned subscribe_fault=0;subscribe_fault<2;subscribe_fault++) {
        cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
        if(subscribe_fault)m.fail_sync_unlock=true;
        uint64_t token=a->subscribe(NULL);assert(token);
        if(!subscribe_fault){m.fail_sync_unlock=true;(void)a->poll(NULL,1);}
        unsigned counts[CST_TEST_OP_COUNT];memcpy(counts,m.counts,sizeof(counts));
        risc_touch_snapshot_v1 snap;assert(!a->snapshot(NULL,&snap));
        assert(a->next(NULL,token,&e)==-2);
        assert(!memcmp(counts,m.counts,sizeof(counts))&&m.sync_held);
        m.fail_sync_unlock=false;
        assert(a->unsubscribe(NULL,token));assert(d->quiesce());cst_test_clean(&m);
    }
    /* A malformed report without custody failure remains recoverable GAP. */
    cst_test_model m;cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    uint64_t token=a->subscribe(NULL);assert(token);
    cst_test_report(&m,10,20,true);assert(a->poll(NULL,1));event(a,token,RISC_TOUCH_EVENT_DOWN);
    m.raw[6]=0;assert(!a->poll(NULL,1));assert(a->next(NULL,token,&e)==-1);
    assert(a->next(NULL,token,&e)==0);
    cst_test_report(&m,30,40,true);assert(a->poll(NULL,1));event(a,token,RISC_TOUCH_EVENT_MOVE);
    assert(a->next(NULL,token,&e)==0);
    assert(a->unsubscribe(NULL,token));assert(d->quiesce());cst_test_clean(&m);
    /* A failed sleep command has an explicit reset/reprobe obligation. A
     * stream read reports that invalid provider state without doing bus I/O. */
    cst_test_setup(&m);m.touch=a;assert(d->start(m.dependencies,6));
    token=a->subscribe(NULL);assert(token);m.wrong_mode=true;
    assert(!a->unsubscribe(NULL,token));unsigned transfers=m.counts[CST_TEST_TRANSFER];
    assert(a->next(NULL,token,&e)==-2&&m.counts[CST_TEST_TRANSFER]==transfers);
    assert(m.bus_token&&m.reset_token);m.wrong_mode=false;
    assert(a->unsubscribe(NULL,token));assert(d->quiesce());cst_test_clean(&m);
    puts("CST9217: provider fault -2 versus stale/GAP -1; all outer results/failed poll unlock retain custody with zero subsequent next callbacks PASS");
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
    idle_cycles(d,a);idle_faults(d,a);wake_faults(d,a);handoff_retries(d,a);
    stream_fault_classification(d,a);
    puts("CST9217: scoped reset lifetime, STOP/delay protocol, coordinates, fanout/gaps, owner reentrancy and stream lifecycle PASS");
}
