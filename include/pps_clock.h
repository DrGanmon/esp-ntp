#pragma once
#include <stdint.h>

// Assumes factory 1 Hz UTC-aligned rising TIMEPULSE and following same-second RMC.
class PpsClock {
 public:
  void pulse(uint32_t edge, uint32_t sequence) {
    if (seen && sequence == pulseSequence) return;
    bool contiguous = seen && uint32_t(sequence - pulseSequence) == 1 &&
      uint32_t(edge - pulseMicros) >= 998000 && uint32_t(edge - pulseMicros) <= 1002000;
    if (!contiguous) { matches = 0; matched = false; }
    pulseMicros = edge; pulseSequence = sequence; seen = true;
  }
  bool associate(uint32_t epoch, uint32_t sentenceStart, unsigned centiseconds) {
    if (!seen || centiseconds || uint32_t(sentenceStart - pulseMicros) < 10000 ||
        uint32_t(sentenceStart - pulseMicros) > 800000) { matches = 0; matched = false; return false; }
    if (matched && pulseSequence == matchedSequence) {
      if (epoch != matchedEpoch) { reset(); return false; }
      return locked(sentenceStart);
    }
    uint32_t delay = sentenceStart - pulseMicros;
    bool consecutive = matched && uint32_t(pulseSequence - matchedSequence) == 1 &&
      epoch == matchedEpoch + 1 && (delay > lastDelay ? delay - lastDelay : lastDelay - delay) <= 20000;
    matches = consecutive ? (matches < 3 ? matches + 1 : 3) : 1;
    matched = true; matchedSequence = pulseSequence; matchedEpoch = epoch; lastDelay = delay;
    referenceSeconds = epoch; referenceMicros = pulseMicros;
    return matches >= 3;
  }
  void reset() { matches = 0; matched = false; }
  bool locked(uint32_t now) const { return matched && matches >= 3 && uint32_t(now - referenceMicros) < 1500000; }
  bool detected(uint32_t now) const { return seen && uint32_t(now - pulseMicros) < 2000000; }
  uint32_t referenceSeconds = 0, referenceMicros = 0;
 private:
  bool seen = false, matched = false;
  unsigned matches = 0;
  uint32_t pulseMicros = 0, pulseSequence = 0, matchedSequence = 0, matchedEpoch = 0, lastDelay = 0;
};
