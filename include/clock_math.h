#pragma once
#include <stdint.h>

struct ClockMapping {
  uint32_t seconds = 0, micros = 0;
  int32_t ratePpb = 0; // Positive: local timer runs fast.
  bool valid = false;
  int64_t unixMicrosAt(uint32_t now) const {
    int64_t elapsed = int32_t(now - micros);
    elapsed = elapsed * 1000000000LL / (1000000000LL + ratePpb);
    return int64_t(seconds) * 1000000 + elapsed;
  }
  void timestamp(uint8_t *out, uint32_t now) const {
    for (unsigned i = 0; i < 8; ++i) out[i] = 0;
    if (!valid) return;
    // A packet may have arrived before the most recently installed PPS anchor.
    int64_t utc = unixMicrosAt(now);
    uint32_t sec = uint32_t(utc / 1000000) + 2208988800UL;
    uint32_t fraction = (uint64_t(utc % 1000000) << 32) / 1000000;
    for (unsigned i = 0; i < 4; ++i) {
      out[i] = sec >> (24 - i * 8); out[i + 4] = fraction >> (24 - i * 8);
    }
  }
};

class CaptureCounter {
 public:
  uint64_t extend(uint32_t raw) {
    total += uint32_t(raw - previous); previous = raw; return total;
  }
 private:
  uint32_t previous = 0;
  uint64_t total = 0;
};
