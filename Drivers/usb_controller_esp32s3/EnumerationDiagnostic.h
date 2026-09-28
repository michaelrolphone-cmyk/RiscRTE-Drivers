#pragma once
#include "StartupDiagnostic.h"
#include <cstdarg>

// Used only from the serialized IDF hub event handler, never an ISR. Capture
// the first failure per physical attach so cleanup cannot erase its cause.
class EnumerationDiagnostic {
 public:
  void clear() { stage_.clear(); error_.clear(); failed_ = false; }
  void stage(const char *name) { stage_.clear(); stage_.text(name); }
  void error(const char *format, va_list args) {
    if (failed_) return;
    failed_ = true;
    // The pinned hub's messages use only %d, %x and %s. Format locally so the
    // provider needs no new firmware libc import or global logging callback.
    for (size_t i = 0; format && format[i] && i < 160; ++i) {
      if (format[i] == '%' && format[i + 1]) {
        const char kind = format[++i];
        if (kind == 's') error_.text(va_arg(args, const char *));
        else if (kind == 'd') {
          const int value = va_arg(args, int);
          if (value < 0) error_.text("-");
          error_.number("", value < 0 ? 0u - static_cast<uint32_t>(value) :
                                       static_cast<uint32_t>(value));
        } else if (kind == 'x' || kind == 'u')
          error_.number("", va_arg(args, unsigned), kind == 'x' ? 16 : 10);
        else { char literal[] = {kind, 0}; error_.text(literal); }
      } else { char literal[] = {format[i], 0}; error_.text(literal); }
    }
  }
  bool copy(uint32_t port, char *out, size_t capacity,
            const char *phy = nullptr, bool dpDown = false, bool dmDown = false) const {
    StartupDiagnostic result;
    // ESP32-S3 DWC HPRT: bit 0 connection, bit 2 enabled, bit 12 port power.
    if (!(port & (1u << 12))) result.text("PORT OFF; ");
    else result.text(port & 1u ? "ATTACHED; " : "NO ATTACH; ");
    char detail[112] = {};
    if (failed_) { result.text("ENUM FAIL: "); error_.copy(detail, sizeof(detail)); }
    else if (!stage_.copy(detail, sizeof(detail))) {
      result.text("NO ENUM EVENT");
    }
    result.text(detail);
    // Compact read-only hardware state fits Gameboy's existing 48-byte stage
    // when no enumeration has started. Do not require serial to identify a
    // later PHY handoff or lost host pull-downs on the single USB connector.
    if (phy && !failed_) {
      result.text(" PHY="); result.text(phy);
      result.text(" PD="); result.text(dpDown ? "1" : "0");
      result.text(dmDown ? "1" : "0");
    }
    return result.copy(out, capacity);
  }
 private:
  StartupDiagnostic stage_, error_;
  bool failed_ = false;
};
