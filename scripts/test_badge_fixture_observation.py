#!/usr/bin/env python3
"""Prove host fixture clocks are pure and observation uses real boundaries.

Extracts actual owning C++ function bodies; models only their observer and
accounting dependencies. This is not full Runtime or target execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

NAMES = ("prototype", "hybrid", "settings", "picker", "wifi_gap", "springboard", "timecard")

def function(source, signature):
    assert source.count(signature) == 1, signature
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

def placement(source):
    clock = function(source, "bool fixtureMonotonic(uint32_t* out)")
    health = function(source, "bool health(risc_runtime_health_v1* out)")
    for body in (clock, health):
        assert "assert(!m.terminal);" in body
        assert "fixture" not in body[body.index("{"):] and "Observe" not in body
    assert "fixtureBoundaryObserve();" in function(source, "void delay(uint32_t ms)")
    row = function(source, "bool displayRow(uint32_t y,const uint16_t* row,size_t width)")
    assert row.index("if(y==465)") < row.index("alarmObserve(true)") < row.index("fixtureBoundaryObserve();")
    brightness = function(source, "bool displayBrightness(uint8_t level)")
    assert brightness.index("if(level&&!m.nativeFrameComplete)") < brightness.index("return true;") < brightness.index("fixtureBoundaryObserve();")
    assert brightness.index("m.level=level;") < brightness.index("fixtureBoundaryObserve();")
    transfer = function(source, "bool i2cTransfer(uint8_t physical,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t timeout)")
    assert transfer.index("assert(!m.terminal);") < transfer.index("if(address==0x34)")
    read = transfer.index("if(tx[0]==0x49)fixtureBoundaryObserve();")
    write = transfer.index("if(tn==2)", transfer.index("if(address==0x34)"))
    assert write < transfer.index("return true;", write) < read
    assert read < transfer.index("Keys(tx[0])", read)
    assert "++m.touchReads;fixtureBoundaryObserve();memset(rx,0,rn);" in transfer
    assert "controlExitRequested=" not in source
    return clock, health

PREFIX = '''#include <cassert>
#include <cstdint>
#include <string>
#include <algorithm>
struct { bool terminal=false,foreground=false; unsigned health=0,caseIndex=0,target=0,visits=0,rows=0;uint64_t ms=1,entered=0;std::string app="default";size_t peakApp=0,peakProviders=0;}m;
struct {bool enabled=true,done=false;}am;struct{bool enabled=false;}qm;struct{bool done=false;}tc;
std::string hybridScenario="gpio";bool settingsReturnOnly(){return true;}bool navigationTarget(){return true;}
struct Usage{size_t hostLive=2,foregroundLive=3,graphLive=4;};struct Runtime{Usage grantUsage(){return {};}} runtime;Runtime*running=&runtime;
unsigned observations=0,foreground=0,picker=0,timecard=0;void alarmObserve(bool=false){++observations;}void foregroundComplete(){++foreground;}void pickerObserve(bool=false){++picker;}void timecardObserve(bool){++timecard;}
struct risc_runtime_health_v1 {uint32_t uptime_ms;};
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix="badge-fixture-observation-") as temporary:
        out = Path(temporary)
        for name in NAMES:
            path = args.root / "tests" / f"badge_{name}_execution.cpp"
            source = path.read_text()
            clock, health = placement(source)
            code = "\n".join((function(source, "static void fixtureBoundaryObserve(){"),
                               function(source, "static bool fixtureContinue()"), clock, health))
            body = PREFIX + code + r"""
int main(){
 uint32_t tick=0;risc_runtime_health_v1 h{};
 for(unsigned i=0;i<100000;i++){assert(fixtureMonotonic(&tick)&&health(&h));}
 assert(tick==1&&h.uptime_ms==1&&!observations&&!m.health&&!foreground&&!picker&&!timecard);
 fixtureBoundaryObserve();assert(observations==1&&m.health==1);
 assert(fixtureContinue());assert(observations==2&&m.health==2);
 am.done=tc.done=true;assert(!fixtureContinue());assert(observations==3&&m.health==3);
}
"""
            cpp = out / f"{name}.cpp"
            cpp.write_text(body)
            for lane, flags in (("normal", []), ("sanitized", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
                exe = out / f"{name}-{lane}"
                command = shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++17", "-Wall", "-Wextra", "-Werror", *flags, str(cpp), "-o", str(exe)]
                subprocess.run(command, check=True)
                # LSan cannot run under the execution service's ptrace; ASan/UBSan remain enabled.
                env = dict(os.environ)
                env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
                subprocess.run([str(exe)], check=True, env=env)
                exe.unlink()
                results.append({"fixture": name, "lane": lane, "source_sha256": hashlib.sha256(source.encode()).hexdigest(), "clock_health_pairs": 100000, "passed": True})
        # Legacy fixture has no scenario observers; its lifetime bound is host-loop-only.
        legacy = (args.root / "tests/badge_runtime_execution.cpp").read_text()
        assert "fixtureContinue" not in function(legacy, "bool health(risc_runtime_health_v1* out)")
        assert "fixtureContinue" not in function(legacy, "bool fixtureMonotonic(uint32_t* out)")
    receipt = {"results": results, "legacy_clock_purity": True, "scope": "Extracted real fixture bodies with modeled observer/accounting dependencies; not full Runtime or hardware qualification."}
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2) + "\n")
    print("PASS seven fixture boundary/clock proofs, normal + ASan/UBSan; 100000 pure clock/health pairs per lane")

if __name__ == "__main__":
    main()
