#include "nmea_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Only timing is tested here. TinyGPSPlus still validates the NMEA checksum.
const char *rmc = "$GPRMC,123456.00,A,4807.038,N,01131.000,E,0.0,0.0,171026,,,A*00\r";
uint32_t receive(NmeaTiming &timing, uint32_t start, const char *sentence,
                 bool burst = false, bool stall = false, bool overflow = false) {
  timing.service(start - 1000, false);
  uint32_t now = start;
  for (unsigned i = 0; sentence[i]; ++i) {
    now = start + (burst ? 0 : NmeaTiming::wireTime(i));
    if (stall && i > 20) now += 50000;
    timing.service(now, overflow && i == 30);
    timing.observe(sentence[i], now);
  }
  return now;
}
int main() {
  NmeaTiming good;
  uint32_t now = receive(good, 100000, rmc);
  assert(good.isRmc() && good.usable(now));
  assert(good.correctionUs() == NmeaTiming::wireTime(strlen(rmc)));
  assert(NmeaTiming::wireTime(96) == 100000); // 960 bits at 9600 baud
  NmeaTiming wrapped;
  now = receive(wrapped, 0xFFFFF000UL, rmc);
  assert(wrapped.usable(now)); // micros() rollover during reception
  NmeaTiming buffered;
  now = receive(buffered, 100000, rmc, true);
  assert(!buffered.usable(now)); // old UART data delivered in a burst
  NmeaTiming stalled;
  now = receive(stalled, 100000, rmc, false, true);
  assert(!stalled.usable(now));
  now = receive(stalled, 1100000, rmc);
  assert(stalled.usable(now)); // next live sentence recovers
  NmeaTiming lost;
  now = receive(lost, 100000, rmc, false, false, true);
  assert(!lost.usable(now));
  NmeaTiming other;
  now = receive(other, 100000, "$GPGGA,123456.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M*00\r");
  assert(!other.isRmc() && !other.usable(now));
  char huge[140]; memset(huge, 'x', sizeof(huge)); memcpy(huge, "$GPRMC,", 7); huge[139] = 0;
  NmeaTiming oversized;
  now = receive(oversized, 100000, huge);
  assert(!oversized.usable(now));
  puts("NMEA timing tests passed");
}
