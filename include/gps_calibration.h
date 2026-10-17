#pragma once
#include <stdint.h>

// Five-sample window, median of the three samples with the shortest network delay.
// This estimates serial GPS bias; it cannot detect persistent network asymmetry.
class GpsCalibration {
 public:
  static constexpr uint32_t MAX_AGE = 180000;
  static void correctedAnchor(uint32_t seconds, uint32_t micros, int32_t offset,
                              uint32_t &resultSeconds, uint32_t &resultMicros) {
    int64_t corrected = int64_t(seconds) * 1000000 + offset;
    resultSeconds = corrected / 1000000;
    resultMicros = micros - uint32_t(corrected % 1000000);
  }
  void reset() { count = next = 0; valid = false; }
  bool add(int64_t offset, uint32_t delay, uint32_t uncertainty, uint8_t stratum,
           uint32_t now, uint32_t source = 0, uint8_t leap = 0) {
    if (offset < -1000000 || offset > 1000000 || delay > 100000 || uncertainty > 100000 ||
        stratum < 2 || stratum > 15) return false;
    samples[next] = {int32_t(offset), delay, uncertainty, now, source, stratum, leap};
    next = (next + 1) % 5; if (count < 5) ++count;
    Entry chosen[5]; unsigned n = 0;
    for (unsigned i = 0; i < count; ++i)
      if (uint32_t(now - samples[i].at) < MAX_AGE) chosen[n++] = samples[i];
    valid = false;
    if (n < 3) return true;
    for (unsigned i = 0; i < n; ++i)
      for (unsigned j = i + 1; j < n; ++j)
        if (chosen[j].delay < chosen[i].delay) { Entry tmp = chosen[i]; chosen[i] = chosen[j]; chosen[j] = tmp; }
    int32_t offsets[3] = {chosen[0].offset, chosen[1].offset, chosen[2].offset};
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned j = i + 1; j < 3; ++j)
        if (offsets[j] < offsets[i]) { int32_t tmp = offsets[i]; offsets[i] = offsets[j]; offsets[j] = tmp; }
    spreadUs = offsets[2] - offsets[0];
    if (spreadUs > 20000) return true;
    offsetUs = offsets[1]; uncertaintyUs = 0; this->stratum = 1;
    for (unsigned i = 0; i < 3; ++i)
      if (chosen[i].offset == offsetUs) { referenceId = chosen[i].source; leapIndicator = chosen[i].leap; break; }
    uint32_t oldestAge = 0;
    for (unsigned i = 0; i < 3; ++i) {
      if (chosen[i].leap != leapIndicator) return true;
      if (chosen[i].uncertainty > uncertaintyUs) uncertaintyUs = chosen[i].uncertainty;
      if (chosen[i].stratum > this->stratum) this->stratum = chosen[i].stratum;
      uint32_t age = now - chosen[i].at;
      if (age > oldestAge) oldestAge = age;
    }
    uncertaintyUs += spreadUs + 8000; // UART processing allowance, not measured UTC error
    oldestAt = now - oldestAge;
    valid = true; return true;
  }
  bool ready(uint32_t now) const { return valid && uint32_t(now - oldestAt) < MAX_AGE; }
  uint32_t age(uint32_t now) const { return now - oldestAt; }
  unsigned sampleCount() const { return count; }
  int32_t offsetUs = 0;
  uint32_t uncertaintyUs = 1000000, spreadUs = 0;
  uint8_t stratum = 1;
  uint32_t referenceId = 0;
  uint8_t leapIndicator = 0;
 private:
  struct Entry { int32_t offset; uint32_t delay, uncertainty, at, source; uint8_t stratum, leap; };
  Entry samples[5] = {};
  unsigned count = 0, next = 0;
  bool valid = false;
  uint32_t oldestAt = 0;
};
