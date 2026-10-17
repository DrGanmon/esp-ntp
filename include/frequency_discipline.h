#pragma once
#include <stdint.h>

class FrequencyDiscipline {
 public:
  void reset() { seen = learned = false; samples = updates = 0; sum = 0; ratePpb = 0; }
  void observe(uint32_t epoch, uint32_t edge) {
    if (seen && epoch == previousEpoch) return;
    uint32_t period = edge - previousEdge;
    bool contiguous = seen && epoch == previousEpoch + 1 && period >= 999500 && period <= 1000500;
    if (!contiguous) reset();
    previousEpoch = epoch; previousEdge = edge; seen = true;
    if (!contiguous) return;
    sum += period;
    if (!samples || period < minimum) minimum = period;
    if (!samples || period > maximum) maximum = period;
    if (++samples < 16) return;
    jitterUs = maximum - minimum;
    if (jitterUs > 100) { reset(); return; }
    int32_t estimate = (int64_t(sum) - 16000000LL) * 1000 / 16;
    ratePpb = learned ? ratePpb + (estimate - ratePpb) / 4 : estimate;
    learned = true; learnedAt = edge; ++updates; samples = 0; sum = 0;
  }
  bool ready(uint32_t now) const { return learned && uint32_t(now - learnedAt) < 60000000; }
  unsigned progress() const { return samples; }
  int32_t ratePpb = 0;
  uint32_t jitterUs = 0, updates = 0;
 private:
  bool seen = false, learned = false;
  unsigned samples = 0;
  uint32_t previousEpoch = 0, previousEdge = 0, learnedAt = 0, minimum = 0, maximum = 0;
  uint64_t sum = 0;
};
