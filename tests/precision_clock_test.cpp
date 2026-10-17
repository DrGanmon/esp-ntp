#include "clock_math.h"
#include "frequency_discipline.h"
#include <assert.h>
#include <stdio.h>

uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
int main() {
  CaptureCounter counter;
  assert(counter.extend(0xFFFFFF00UL) == 0xFFFFFF00ULL);
  assert(counter.extend(0x100) == 0x100000100ULL);
  assert(counter.extend(0x100) == 0x100000100ULL);
  FrequencyDiscipline rate;
  uint32_t edge = 0xFFF00000UL;
  for (unsigned i = 0; i <= 16; ++i) {
    rate.observe(1792195200 + i, edge);
    if (i < 16) assert(!rate.ready(edge));
    edge += 1000020; // timer runs fast by 20 ppm; includes micros wrap
  }
  assert(rate.ready(edge) && rate.ratePpb == 20000);
  assert(rate.jitterUs == 0 && rate.updates == 1);
  ClockMapping mapping;
  mapping.seconds = 1792195200; mapping.micros = 0xFFFFFF00;
  mapping.ratePpb = rate.ratePpb; mapping.valid = true;
  assert(mapping.unixMicrosAt(mapping.micros + 1000020) == 1792195201000000LL);
  uint8_t stamp[8]; mapping.timestamp(stamp, mapping.micros + 1000020);
  assert(read32(stamp) - 2208988800UL == 1792195201 && read32(stamp + 4) == 0);
  mapping.ratePpb = 0;
  mapping.timestamp(stamp, mapping.micros - 100000); // queued packet before latest PPS
  assert(read32(stamp) - 2208988800UL == 1792195199 && read32(stamp + 4) > 0xE0000000UL);
  mapping.valid = false; mapping.timestamp(stamp, 123);
  assert(!read32(stamp) && !read32(stamp + 4));
  assert(!rate.ready(edge + 60000000));
  rate.observe(1792195300, edge); // UTC jump must discard the learned rate
  assert(!rate.ready(edge));
  rate.reset(); edge = 1000;
  for (unsigned i = 0; i <= 16; ++i) { rate.observe(1792195200 + i, edge); edge += 999970; }
  assert(rate.ready(edge) && rate.ratePpb == -30000);
  mapping.valid = true; mapping.ratePpb = rate.ratePpb;
  assert(mapping.unixMicrosAt(mapping.micros + 999970) == 1792195201000000LL);
  // Subsequent windows are smoothed rather than replacing the frequency in one step.
  for (unsigned i = 17; i <= 32; ++i) { rate.observe(1792195200 + i, edge); edge += 999930; }
  assert(rate.ready(edge) && rate.updates == 2 && rate.ratePpb == -39375);
  mapping.seconds = 2085978496UL; mapping.ratePpb = 0;
  mapping.timestamp(stamp, mapping.micros + 500000);
  assert(read32(stamp) == 0 && read32(stamp + 4) == 0x80000000UL); // NTP 2036 wrap
  rate.reset(); edge = 0;
  for (unsigned i = 0; i <= 16; ++i) { rate.observe(1792195200 + i, edge); edge += i % 2 ? 1000200 : 999800; }
  assert(!rate.ready(edge)); // noisy pulses cannot discipline the clock
  puts("Precision clock tests passed");
}
