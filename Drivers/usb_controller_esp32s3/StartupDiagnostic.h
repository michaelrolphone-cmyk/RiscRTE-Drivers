#pragma once
#include <cstddef>
#include <cstdint>

// Fixed storage and local formatting: no new firmware/libc import is needed.
class StartupDiagnostic {
 public:
  void clear() { used_ = 0; text_[0] = 0; }
  void text(const char* value) {
    for (size_t i = 0; value && value[i] && used_ + 1 < sizeof(text_); ++i)
      text_[used_++] = value[i];
    text_[used_] = 0;
  }
  void number(const char* label, uint32_t value, uint32_t radix = 10) {
    if (radix != 10 && radix != 16) return;
    text(label);
    char digits[32];
    size_t count = 0;
    do {
      digits[count++] = "0123456789abcdef"[value % radix];
      value /= radix;
    } while (value && count < sizeof(digits));
    while (count && used_ + 1 < sizeof(text_)) text_[used_++] = digits[--count];
    text_[used_] = 0;
  }
  void failure(const char* stage, int32_t code) {
    clear(); text(stage); text(" rc=");
    if (code < 0) text("-");
    number("", code < 0 ? 0u - static_cast<uint32_t>(code) : static_cast<uint32_t>(code));
    number(" (0x", static_cast<uint32_t>(code), 16); text(")");
  }
  bool copy(char* destination, size_t capacity) const {
    if (!destination || !capacity) return false;
    size_t i = 0;
    for (; i < used_ && i + 1 < capacity; ++i) destination[i] = text_[i];
    destination[i] = 0;
    return used_ != 0;
  }
 private:
  char text_[112]{};
  size_t used_ = 0;
};
