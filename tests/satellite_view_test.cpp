#include "satellite_view.h"
#include <assert.h>
#include <stdio.h>
#include <string>

void send(SatelliteView &view, const char *payload, uint32_t now, bool corrupt = false) {
  unsigned checksum = 0;
  for (const char *p = payload; *p; ++p) checksum ^= uint8_t(*p);
  char suffix[8]; snprintf(suffix, sizeof(suffix), "*%02X\r\n", checksum ^ (corrupt ? 1 : 0));
  std::string sentence = std::string("$") + payload + suffix;
  for (char c : sentence) view.encode(c, now);
}
int main() {
  SatelliteView view;
  assert(!view.fresh(0));
  send(view, "GPGSV,2,1,05,01,90,000,45,02,45,090,30,03,00,180,,04,30,270,00", 1000);
  assert(!view.fresh(1000)); // incomplete cycles must not be published
  send(view, "GPGSV,2,2,05,05,20,359,22", 1100);
  assert(view.fresh(1100) && view.count() == 5);
  assert(view.at(0).id == 1 && view.at(0).elevation == 90 && view.at(0).azimuth == 0);
  assert(view.at(2).snr == -1 && view.at(3).snr == 0);
  send(view, "GPGSV,1,1,01,06,20,120,33", 1200, true);
  assert(view.count() == 5); // bad checksums leave the last complete snapshot alone
  send(view, "GPGSV,1,1,01,06,20,120,33", 1300);
  assert(view.count() == 1 && view.at(0).id == 6); // disappeared satellites removed
  send(view, "GPGSV,1,1,01,06,91,120,33", 1400);
  assert(view.at(0).elevation == 20); // out-of-range geometry rejected
  send(view, "GPGSV,1,1,01,06,,,", 1500);
  assert(view.at(0).elevation == -1 && view.at(0).azimuth == -1 && view.at(0).snr == -1);
  assert(!view.fresh(11500));
  send(view, "GPGSV,2,1,05,01,90,000,45,02,45,090,30,03,00,180,,04,30,270,00", 12000);
  send(view, "GPGSV,2,2,05,05,20,359,22", 18000);
  assert(!view.fresh(18000)); // multipart timeout
  send(view, "GNGSV,1,1,00", 0xFFFFFF00UL);
  assert(view.fresh(1000) && view.count() == 0); // empty cycle and millis rollover
  puts("Satellite view tests passed");
}
