#pragma once
#include <cstdint>
#include "RiscUsbVbusV1.h"

// Executed only by the existing owner-task poll. No worker, app pointers,
// package reloads or input queues. Port owns every physical side effect.
class UsbRoleSwitch {
 public:
  enum class State { Off, Sense, Host, Cleanup, Failed };
  void begin(uint32_t now) {
    state_ = State::Sense; since_ = now;
    absent_ = external_ = false; readFailures_ = startFailures_ = 0;
    settling_ = false; cleanupStops_ = false;
    error_ = nullptr;
  }
  void stop() { state_ = State::Off; }
  State state() const { return state_; }
  const char *diagnostic() const {
    if (state_ == State::Failed) return error_;
    if (state_ == State::Cleanup)
      return "ROLE: HOST CLEANUP PENDING; RESOURCES RETAINED";
    if (state_ == State::Sense)
      return external_ ? "EXTERNAL POWER; USB SERIAL AVAILABLE" : "SOURCE OFF; CHECKING USB INPUT POWER";
    return "USB ROLE OFF";
  }
  template <class Port> void poll(Port &port, uint32_t now) {
    if (state_ == State::Off || state_ == State::Failed) return;
    if (state_ == State::Cleanup) {
      // A failed teardown may have consumed some handles already. Retain the
      // remaining ownership and retry the same idempotent cleanup path without
      // pumping a client that may no longer exist or restarting the host.
      if (static_cast<uint32_t>(now - since_) < 250u) return;
      since_ = now;
      if (!port.park()) return;
      if (cleanupStops_) {
        fail(port, "ROLE: HOST START FAILED THREE TIMES");
        return;
      }
      state_ = State::Sense;
      since_ = port.now();
      absent_ = external_ = settling_ = false;
      port.report(diagnostic());
      return;
    }
    if (state_ == State::Host) {
      // Include the root port's physical attach bit, queued events and claims,
      // not just the published device count: enumeration may be in progress.
      if (port.busy()) { since_ = now; return; }
      const bool probe = port.idle_probe_required();
      if (static_cast<uint32_t>(now - since_) < (probe ? 2000u : 500u)) return;
      // Independent detectors can observe incoming power with the host on.
      // Only boards declaring this limitation need idle power-off probes.
      if (!probe && port.input() == RISC_USB_POWER_SOURCE) { since_ = now; return; }
      if (!port.park()) {
        state_ = State::Cleanup;
        since_ = port.now();
        cleanupStops_ = false;
        port.report(diagnostic());
        return;
      }
      state_ = State::Sense; since_ = port.now(); absent_ = external_ = false;
      settling_ = false;
      port.report(diagnostic());
      return;
    }
    // These are consumer polling/debounce intervals, not electrical settling
    // delays. The power provider reports SETTLING until its detector is ready.
    const uint32_t wait = startFailures_ ? (1000u << startFailures_) : 500u;
    if (static_cast<uint32_t>(now - since_) < wait) return;
    since_ = now;
    const int32_t input = port.input();
    if (input == RISC_USB_POWER_SETTLING) {
      absent_ = external_ = false; readFailures_ = 0;
      if (!settling_) { settling_ = true; settlingSince_ = now; }
      // Generic liveness budget, independent of a particular power chip.
      if (static_cast<uint32_t>(now - settlingSince_) >= 10000)
        fail(port, "ROLE: POWER DETECTION TIMED OUT");
      return;
    }
    if (input == RISC_USB_POWER_EXTERNAL) {
      settling_ = false;
      if (!external_) { external_ = true; port.report(diagnostic()); }
      absent_ = false; readFailures_ = startFailures_ = 0;
      return; // Never probe the host or touch the PHY on external power.
    }
    if (input != RISC_USB_POWER_ABSENT) {
      absent_ = false;
      if (++readFailures_ >= 3) fail(port, "ROLE: INPUT POWER UNKNOWN; HOST BLOCKED");
      return; // Unknown is not evidence that it is safe to source VBUS.
    }
    settling_ = false; readFailures_ = 0; external_ = false;
    if (!absent_) { absent_ = true; return; }
    if (port.start()) {
      state_ = State::Host; since_ = port.now(); startFailures_ = 0;
      cleanupStops_ = false;
      port.report("USB HOST; CONTROLLER DISCOVERY");
      return;
    }
    // A failed startup can still own DMA, PHY or a partial power lease.
    // Prove cleanup before restoring serial or attempting anything else.
    ++startFailures_;
    cleanupStops_ = startFailures_ >= 3;
    if (!port.park()) {
      state_ = State::Cleanup;
      since_ = port.now();
      port.report(diagnostic());
      return;
    }
    if (cleanupStops_) { fail(port, "ROLE: HOST START FAILED THREE TIMES"); return; }
    since_ = port.now(); absent_ = false;
    port.report("HOST START FAILED; BOUNDED RETRY");
  }
 private:
  template <class Port> void fail(Port &port, const char *reason) {
    state_ = State::Failed;
    error_ = reason;
    port.report(diagnostic());
  }
  State state_ = State::Off;
  uint32_t since_ = 0;
  uint8_t readFailures_ = 0, startFailures_ = 0;
  bool absent_ = false, external_ = false;
  bool settling_ = false, cleanupStops_ = false;
  uint32_t settlingSince_ = 0;
  const char *error_ = nullptr;
};
