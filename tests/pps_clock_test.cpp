#include "pps_clock.h"
#include <assert.h>
#include <stdio.h>

int main() {
  PpsClock clock;
  assert(!clock.detected(0) && !clock.locked(0));
  for (unsigned i = 1; i <= 3; ++i) {
    clock.pulse(i * 1000000, i);
    assert(clock.associate(1792195200 + i, i * 1000000 + 200000, 0) == (i == 3));
  }
  assert(clock.referenceSeconds == 1792195203 && clock.referenceMicros == 3000000);
  assert(clock.locked(4499999) && !clock.locked(4500000));
  clock.pulse(4000000, 4);
  assert(!clock.associate(1792195204, 4900000, 0)); // late RMC
  assert(!clock.locked(4900000));
  clock.pulse(5000000, 5);
  assert(!clock.associate(1792195205, 5200000, 50)); // not a whole-second RMC
  clock.pulse(6000000, 6); clock.associate(1792195206, 6200000, 0);
  clock.pulse(7000000, 7); clock.associate(1792195207, 7200000, 0);
  clock.pulse(8000000, 8); assert(clock.associate(1792195208, 8200000, 0));
  clock.pulse(8001000, 9); // noise, not a one-second pulse
  assert(!clock.locked(8200000));
  PpsClock wrapped;
  uint32_t edge = 0xFFF00000UL;
  for (unsigned i = 1; i <= 3; ++i) {
    wrapped.pulse(edge, i); wrapped.associate(1792195200 + i, edge + 200000, 0);
    edge += 1000000;
  }
  assert(wrapped.locked(edge - 800000));
  assert(!wrapped.associate(1792195299, edge - 800000, 0)); // one pulse cannot identify two UTC seconds
  wrapped.reset(); assert(!wrapped.locked(edge - 800000));
  puts("PPS clock tests passed");
}
