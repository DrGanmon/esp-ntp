#include "gps_calibration.h"
#include <assert.h>
#include <stdio.h>

int main() {
  uint32_t seconds, micros;
  GpsCalibration::correctedAnchor(1792195200, 5000, -120000, seconds, micros);
  assert(seconds == 1792195199 && uint32_t(55000 - micros) == 930000);
  GpsCalibration::correctedAnchor(1792195200, 5000, 120000, seconds, micros);
  assert(seconds == 1792195200 && uint32_t(55000 - micros) == 170000);
  GpsCalibration filter;
  assert(filter.add(120000, 10000, 20000, 3, 1000));
  assert(filter.add(122000, 12000, 21000, 2, 31000));
  assert(!filter.ready(31000));
  assert(filter.add(121000, 11000, 20000, 3, 61000));
  assert(filter.ready(61000) && filter.offsetUs == 121000);
  assert(filter.spreadUs == 2000 && filter.uncertaintyUs == 31000 && filter.stratum == 3);
  // A slow high-offset sample must not displace the three faster samples.
  assert(filter.add(-900000, 90000, 50000, 4, 91000));
  assert(filter.ready(91000) && filter.offsetUs == 121000);
  assert(!filter.add(2000000, 1000, 10000, 2, 92000));
  assert(!filter.add(120000, 100001, 10000, 2, 92000));
  assert(!filter.add(120000, 1000, 100001, 2, 92000));
  assert(!filter.ready(181000)); // expire based on oldest selected comparison
  filter.reset(); assert(!filter.ready(1000) && filter.sampleCount() == 0);
  filter.add(-120000, 1000, 10000, 2, 0xFFFF0000UL);
  filter.add(-121000, 1000, 10000, 2, 0xFFFF7530UL);
  filter.add(-119000, 1000, 10000, 2, 0xFFFFEA60UL);
  assert(filter.ready(1000) && filter.offsetUs == -120000); // millis rollover
  assert(!filter.ready(200000));
  filter.reset();
  filter.add(0, 1000, 10000, 2, 1000);
  filter.add(50000, 1000, 10000, 2, 31000);
  filter.add(100000, 1000, 10000, 2, 61000);
  assert(!filter.ready(61000)); // unstable GPS timing must not lower uncertainty
  puts("GPS calibration tests passed");
}
