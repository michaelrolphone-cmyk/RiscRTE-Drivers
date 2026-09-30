#include "../../Drivers/usb_controller_esp32s3/RoleSwitch.h"
#include <cassert>
#include <cstdio>

struct Port {
    uint32_t clock = 0;
    int32_t status = RISC_USB_POWER_ABSENT;
    bool occupied = false, parkOK = true, startOK = true, powered = false;
    bool probeRequired = true, externalSupport=false, validExternal=true;
    unsigned externalStarts=0, disconnects=0;
    unsigned starts = 0, parks = 0, reads = 0, reports = 0;
    uint32_t now() const { return clock; }
    bool busy() const { return occupied; }
    bool idle_probe_required() const { return probeRequired; }
    int32_t input() { ++reads; assert(!powered || !probeRequired || externalSupport); return status; }
    bool external_supported() const {return externalSupport;}
    bool external_valid() const {return validExternal;}
    bool disconnect_power_loss() {++disconnects;return true;}
    bool start_external() {++externalStarts;powered=true;return startOK;}
    bool park() { ++parks; if (parkOK) powered = false; return parkOK; }
    bool start() { ++starts; assert(!powered); powered = true; return startOK; }
    void report(const char *) { ++reports; }
    void advance(UsbRoleSwitch &role, uint32_t ms) { clock += ms; role.poll(*this, clock); }
};
using State = UsbRoleSwitch::State;

int main() {
    UsbRoleSwitch role;
    Port port;
    port.status = RISC_USB_POWER_EXTERNAL;
    role.begin(port.clock);
    // Charger/PC already connected at launch: no PHY takeover or source.
    for (unsigned i = 0; i < 120; ++i) port.advance(role, 500);
    assert(!port.starts && !port.parks && port.reports == 1);
    assert(role.state() == State::Sense);
    // Cable removal is debounced; a bounced external reading resets absence.
    port.status = RISC_USB_POWER_ABSENT;
    port.advance(role, 500); assert(!port.starts);
    port.status = RISC_USB_POWER_EXTERNAL;
    port.advance(role, 500); assert(!port.starts);
    port.status = RISC_USB_POWER_ABSENT;
    port.advance(role, 500); assert(!port.starts);
    port.advance(role, 500); assert(port.starts == 1 && port.powered);
    // Attached/enumerating device, pending detach event or outstanding claim
    // keeps the host intact. No power polling/probing during active gameplay.
    port.occupied = true;
    const auto reads = port.reads;
    for (unsigned i = 0; i < 3600; ++i) port.advance(role, 1000);
    assert(!port.parks && port.starts == 1 && reads == port.reads);
    port.occupied = false;
    port.advance(role, 1999); assert(!port.parks);
    port.advance(role, 1); assert(port.parks == 1 && !port.powered);
    // Source-off window discovers a PC which appeared while we supplied VBUS.
    port.status = RISC_USB_POWER_EXTERNAL;
    port.advance(role, 499); assert(port.reads == reads);
    port.advance(role, 1); assert(port.reads == reads + 1);
    for (unsigned i = 0; i < 120; ++i) port.advance(role, 500);
    assert(port.starts == 1 && !port.powered);
    // A receiver plugged after that PC is removed starts without reloading
    // the provider or invalidating its consumer's API/grants.
    port.status = RISC_USB_POWER_ABSENT;
    port.advance(role, 500); port.advance(role, 500);
    assert(port.starts == 2 && role.state() == State::Host);
    // A failed idle cleanup retains ownership and retries cleanup without
    // restarting the host or requiring a reboot. Once cleanup succeeds, normal
    // source-off sensing can start a fresh host for a later attachment.
    port.parkOK = false;
    port.advance(role, 2000);
    assert(role.state() == State::Cleanup && port.powered);
    auto starts = port.starts;
    auto parks = port.parks;
    port.advance(role, 249);
    assert(port.parks == parks && port.starts == starts);
    port.advance(role, 1);
    assert(port.parks == parks + 1 && port.starts == starts);
    port.parkOK = true;
    port.advance(role, 250);
    assert(role.state() == State::Sense && !port.powered);
    port.advance(role, 500);
    port.advance(role, 500);
    assert(role.state() == State::Host && port.starts == starts + 1 && port.powered);

    Port qi;qi.externalSupport=true;qi.status=RISC_USB_POWER_EXTERNAL;
    role.begin(qi.clock);qi.advance(role,500);
    assert(qi.externalStarts==1 && !qi.starts && role.state()==State::Host);
    qi.occupied=true;
    for(unsigned i=0;i<20;++i)qi.advance(role,500);
    assert(!qi.parks && !qi.disconnects);
    qi.validExternal=false;qi.advance(role,500);
    assert(qi.disconnects && !qi.parks); // claims keep owner alive while draining
    qi.occupied=false;qi.advance(role,500);
    assert(qi.parks==1 && role.state()==State::Sense);
    qi.status=RISC_USB_POWER_ABSENT;
    qi.advance(role,500);qi.advance(role,500);
    assert(qi.starts==1); // source acquisition only after Qi is gone
    qi.status=RISC_USB_POWER_SOURCE;qi.occupied=true;qi.advance(role,500);
    assert(role.state()==State::Host);
    qi.status=RISC_USB_POWER_EXTERNAL;qi.advance(role,500);
    qi.occupied=false;qi.advance(role,500);qi.validExternal=true;
    qi.advance(role,500);
    assert(qi.externalStarts==2 && role.state()==State::Host);

    // A persistently unstable external source gets a finite attempt budget.
    Port unstable;unstable.externalSupport=true;unstable.status=RISC_USB_POWER_EXTERNAL;
    unstable.validExternal=false;role.begin(0);
    for(unsigned i=0;i<40;++i)unstable.advance(role,500);
    assert(unstable.externalStarts==3 && !unstable.starts && role.state()==State::Sense);

    Port pc;pc.externalSupport=true;pc.status=RISC_USB_POWER_EXTERNAL;
    role.begin(pc.clock);pc.advance(role,500);
    pc.advance(role,2000);assert(pc.parks==1 && role.state()==State::Sense);
    for(unsigned i=0;i<120;++i)pc.advance(role,500);
    assert(pc.externalStarts==1 && !pc.starts); // serial stays restored

    Port badExternal;badExternal.externalSupport=true;badExternal.status=RISC_USB_POWER_EXTERNAL;
    badExternal.startOK=false;badExternal.parkOK=false;
    role.begin(0);badExternal.advance(role,500);
    assert(role.state()==State::Cleanup && badExternal.powered);
    badExternal.parkOK=true;badExternal.advance(role,250);
    for(unsigned i=0;i<10;++i)badExternal.advance(role,500);
    assert(badExternal.externalStarts==1 && role.state()==State::Sense);

    Port unknown;
    unknown.status = RISC_USB_POWER_UNKNOWN;
    role.begin(unknown.clock);
    for (unsigned i = 0; i < 10; ++i) unknown.advance(role, 500);
    assert(role.state() == State::Failed && unknown.reads == 3 && !unknown.starts);

    // Different board implementation: detector takes longer than the T5S3.
    // The controller waits for the provider instead of applying a chip delay
    // or treating legitimate settling as three failed I2C reads.
    Port slow;
    slow.status = RISC_USB_POWER_SETTLING;
    role.begin(slow.clock);
    for (unsigned i = 0; i < 8; ++i) slow.advance(role, 500);
    assert(role.state() == State::Sense && !slow.starts);
    slow.status = RISC_USB_POWER_ABSENT;
    slow.advance(role, 500); slow.advance(role, 500);
    assert(slow.starts == 1);

    Port stalled;
    stalled.status = RISC_USB_POWER_SETTLING;
    role.begin(stalled.clock);
    for (unsigned i = 0; i < 30; ++i) stalled.advance(role, 500);
    assert(role.state() == State::Failed && !stalled.starts);

    // Another board has an independent role/VBUS detector: observe it while
    // an empty host is powered, without periodic source-off probing.
    Port independent;
    independent.probeRequired = false;
    role.begin(independent.clock);
    independent.advance(role, 500); independent.advance(role, 500);
    assert(independent.starts == 1);
    independent.status = RISC_USB_POWER_SOURCE;
    for (unsigned i = 0; i < 60; ++i) independent.advance(role, 500);
    assert(independent.powered && !independent.parks);
    independent.status = RISC_USB_POWER_EXTERNAL;
    independent.advance(role, 500);
    assert(!independent.powered && independent.parks == 1);
    for (unsigned i = 0; i < 60; ++i) independent.advance(role, 500);
    assert(independent.starts == 1 && role.state() == State::Sense);

    Port failed;
    failed.startOK = false;
    role.begin(failed.clock);
    for (unsigned i = 0; i < 60; ++i) failed.advance(role, 500);
    assert(role.state() == State::Failed && failed.starts == 3 && failed.parks == 3);
    assert(!failed.powered);
    // Tick rollover retains intervals, and a disabled role does no work.
    Port wrap;
    wrap.clock = UINT32_MAX - 400;
    role.begin(wrap.clock);
    wrap.advance(role, 500); wrap.advance(role, 500);
    assert(wrap.starts == 1);
    role.stop();
    wrap.advance(role, 10000); assert(!wrap.parks);
    puts("USB charging/serial role switching: PASS");
}
