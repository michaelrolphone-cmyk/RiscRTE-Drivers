#!/usr/bin/env python3
"""Qualify unchanged controller bodies at a deterministic fake IDF boundary.

This deliberately reproduces a native-adapter prerequisite, not a production
fix. No target, USB, power, device, or wall-clock timing claim is made.
"""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Drivers/usb_controller_esp32s3/driver_base.cpp"
raw = SOURCE.read_bytes()
source = raw.decode()


def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end] + "\n"


constant = re.search(r"constexpr uint32_t kTeardownTicks = \d+;", source).group()
functions = "\n".join(function(s) for s in (
    "bool pump(TickType_t delay)", "bool drain_bulk(bool resume)",
    "bool wait_completion(uint32_t milliseconds)",
    "bool release_interface(void *, uint64_t id)"))
preamble = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using TickType_t = uint32_t;
using esp_err_t = int;
using usb_device_handle_t = void *;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=1, USB_TRANSFER_STATUS_COMPLETED=0;
struct Transfer { void *device_handle; uint8_t bEndpointAddress; int status; };
struct Claim { uint64_t token, physical_device; uint8_t number, alternate; };
struct Device { void *handle; uint64_t token; bool attached; };
static Claim claims[2];
static Device devices[2];
static Transfer dma;
static Transfer *transfer=&dma;
static void *client=&dma;
static bool installed, running, fault, inFlight, completed;
static uint32_t ticks, callbackAt, releaseCalls, haltCalls, flushCalls;
static TickType_t xTaskGetTickCount() { return ticks; }
static TickType_t pdMS_TO_TICKS(uint32_t n) { return n; }
static esp_err_t usb_host_lib_handle_events(unsigned, uint32_t *flags) {
    *flags=0; return ESP_OK;
}
static esp_err_t usb_host_client_handle_events(void *, TickType_t delay) {
    ticks+=delay;
    if (callbackAt && ticks>=callbackAt) { inFlight=false; completed=true; }
    return ESP_OK;
}
static esp_err_t usb_host_endpoint_halt(void *, uint8_t) { ++haltCalls; return ESP_OK; }
static esp_err_t usb_host_endpoint_flush(void *, uint8_t) { ++flushCalls; return ESP_OK; }
static esp_err_t usb_host_endpoint_clear(void *, uint8_t) { return ESP_OK; }
static bool endpoint_mps(Device *, uint8_t, uint8_t, uint8_t, uint16_t *n) {
    *n=64; return true;
}
static esp_err_t usb_host_interface_release(void *, void *, uint8_t) {
    assert(!inFlight); ++releaseCalls; return ESP_OK;
}
static bool claimed(uint64_t id) {
    for (const auto &c:claims) if(c.token && c.physical_device==id) return true;
    return false;
}
static esp_err_t usb_host_device_close(void *, void *) { return ESP_OK; }
'''
main = r'''
static void reset(uint8_t endpoint=0x81) {
    installed=running=inFlight=true; fault=completed=false;
    ticks=callbackAt=releaseCalls=haltCalls=flushCalls=0;
    dma={&dma,endpoint,USB_TRANSFER_STATUS_COMPLETED};
    devices[0]={&dma,11,true}; devices[1]={};
    claims[0]={17,11,1,0}; claims[1]={};
}
int main() {
    reset();
    const uint32_t requested=1;
    assert(!wait_completion(requested));
    assert(ticks==requested+kTeardownTicks && ticks>requested);
    assert(inFlight && claims[0].token==17 && !releaseCalls);
    std::printf("OBSERVED bulk wait: requested=%u fake-ms, elapsed=%u fake-ms, DMA retained\n",requested,ticks);
    reset();
    assert(!release_interface(nullptr,17));
    assert(ticks==kTeardownTicks && ticks>requested);
    assert(inFlight && claims[0].token==17 && !releaseCalls && haltCalls==1 && flushCalls==1);
    std::printf("OBSERVED checked physical release: elapsed=%u fake-ms, claim=17 retained; no caller budget parameter\n",ticks);
    reset(); callbackAt=3;
    assert(release_interface(nullptr,17));
    assert(ticks==3 && !inFlight && !claims[0].token && releaseCalls==1);
    std::printf("CONTROL callback delivered: elapsed=%u fake-ms, claim released only after DMA return\n",ticks);
    reset(0);
    assert(!wait_completion(requested));
    assert(ticks==requested && inFlight && !haltCalls && !releaseCalls);
    std::puts("CONTROL endpoint-zero timeout retains in-flight transfer; no bulk-drain substitution");
    std::puts("PASS: exact-source prerequisite reproduced; NOT a native integration or hardware pass");
}
'''
print("source=" + str(SOURCE), flush=True)
print("source_sha256=" + hashlib.sha256(raw).hexdigest(), flush=True)
print("extracted_function_sha256=" + hashlib.sha256(functions.encode()).hexdigest(), flush=True)
flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-g"]
if os.environ.get("SANITIZE") == "1":
    flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    print("ASan/UBSan enabled; leak detection is controlled separately", flush=True)
with tempfile.TemporaryDirectory(prefix="cdc-native-deadline-") as directory:
    cpp = Path(directory) / "probe.cpp"
    exe = Path(directory) / "probe"
    cpp.write_text(preamble + constant + "\n" + functions + main)
    subprocess.run(["c++", *flags, str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
