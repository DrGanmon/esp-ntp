#pragma once
#include <stdint.h>

// Timing guard for a continuously received 9600-baud, 8N1 NMEA sentence.
// The correction removes wire time only, not the receiver's internal latency.
class NmeaTiming {
 public:
  void service(uint32_t now, bool overflow) {
    timelyService = serviced && !overflow && uint32_t(now - lastService) <= 10000;
    if (!timelyService) timelyFrame = false;
    serviced = true; lastService = now;
  }
  void observe(char c, uint32_t now) {
    if (c == '$') {
      length = 1; start = now; timelyFrame = timelyService;
      for (char &v : prefix) v = 0;
      prefix[0] = '$';
    } else if (length) {
      if (length < sizeof(prefix)) prefix[length] = c;
      if (length < 97) ++length;
      if (length > 96) timelyFrame = false;
    }
  }
  bool isRmc() const {
    return length >= 7 && prefix[1] == 'G' && (prefix[2] == 'P' || prefix[2] == 'N') &&
           prefix[3] == 'R' && prefix[4] == 'M' && prefix[5] == 'C';
  }
  bool usable(uint32_t now) const {
    if (!isRmc() || !timelyFrame || length < 16 || length > 96) return false;
    uint32_t expected = wireTime(length - 1);
    uint32_t elapsed = now - start;
    // Reject buffered bursts, gaps within a sentence and delayed loop processing.
    return elapsed + 8000 >= expected && elapsed <= expected + 8000;
  }
  uint32_t correctionUs() const { return wireTime(length); }
  static uint32_t wireTime(unsigned bytes) { return (bytes * 10000000UL + 4800) / 9600; }
 private:
  bool serviced = false, timelyService = false, timelyFrame = false;
  uint32_t lastService = 0, start = 0;
  unsigned length = 0;
  char prefix[6] = {};
};
