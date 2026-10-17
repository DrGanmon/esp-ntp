#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

class SatelliteView {
 public:
  struct Satellite { int id, elevation, azimuth, snr; };
  static constexpr unsigned CAPACITY = 40;
  void invalidateCycle() { expected = 0; }
  bool fresh(uint32_t now) const { return published && uint32_t(now - updated) < 10000; }
  unsigned count() const { return visibleCount; }
  const Satellite &at(unsigned i) const { return visible[i]; }
  void encode(char c, uint32_t now) {
    if (c == '$') { length = 0; receiving = true; }
    else if (receiving && (c == '\r' || c == '\n')) {
      receiving = false; line[length] = 0; parse(now);
    } else if (receiving) {
      if (length + 1 < sizeof(line)) line[length++] = c;
      else { receiving = false; invalidateCycle(); }
    }
  }
 private:
  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }
  static int number(const char *s, int maximum) {
    if (!*s) return -1;
    int value = 0;
    for (; *s; ++s) {
      if (*s < '0' || *s > '9') return -2;
      value = value * 10 + *s - '0';
      if (value > maximum) return -2;
    }
    return value;
  }
  void parse(uint32_t now) {
    char *star = strchr(line, '*');
    if (!star || strlen(star + 1) != 2) return;
    unsigned checksum = 0;
    for (char *p = line; p < star; ++p) checksum ^= uint8_t(*p);
    int hi = hex(star[1]), lo = hex(star[2]);
    if (hi < 0 || lo < 0 || checksum != unsigned(hi * 16 + lo)) return;
    *star = 0;
    char *fields[24]; unsigned n = 1; fields[0] = line;
    for (char *p = line; *p; ++p) if (*p == ',') {
      *p = 0; if (n == 24) return; fields[n++] = p + 1;
    }
    if (n < 4 || (strcmp(fields[0], "GPGSV") && strcmp(fields[0], "GNGSV"))) return;
    int total = number(fields[1], 10), part = number(fields[2], 10), sats = number(fields[3], CAPACITY);
    if (total < 1 || part < 1 || part > total || sats < 0 || total != (sats ? (sats + 3) / 4 : 1)) { invalidateCycle(); return; }
    if (part == 1) {
      expected = 1; parts = total; announced = sats; pendingCount = 0; cycleAt = now;
      talker = fields[0][1];
    }
    if (part != int(expected) || total != parts || sats != announced || talker != fields[0][1] ||
        uint32_t(now - cycleAt) >= 5000) { invalidateCycle(); return; }
    int entries = sats - (part - 1) * 4;
    if (entries > 4) entries = 4;
    if (entries < 0 || n < unsigned(4 + entries * 4)) { invalidateCycle(); return; }
    for (int i = 0; i < entries; ++i) {
      Satellite sat = {number(fields[4 + i * 4], 999), number(fields[5 + i * 4], 90),
                       number(fields[6 + i * 4], 359), number(fields[7 + i * 4], 99)};
      if (sat.id < 1 || sat.elevation < -1 || sat.azimuth < -1 || sat.snr < -1) { invalidateCycle(); return; }
      pending[pendingCount++] = sat;
    }
    ++expected;
    if (part == total) {
      memcpy(visible, pending, pendingCount * sizeof(Satellite));
      visibleCount = pendingCount; updated = now; published = true; invalidateCycle();
    }
  }
  char line[100] = {};
  unsigned length = 0, expected = 0, pendingCount = 0, visibleCount = 0;
  int parts = 0, announced = 0;
  char talker = 0;
  bool receiving = false, published = false;
  uint32_t cycleAt = 0, updated = 0;
  Satellite pending[CAPACITY] = {}, visible[CAPACITY] = {};
};
