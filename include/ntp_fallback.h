#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Fallback {
constexpr uint32_t EPOCH = 2208988800UL;
inline uint32_t read32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline bool validHost(const char *host) {
  size_t length = strlen(host);
  if (!length || length > 63) return false;
  size_t label = 0;
  char previous = '.';
  for (size_t i = 0; i < length; ++i) {
    char c = host[i];
    if (c == '.') {
      if (!label || previous == '-') return false;
      label = 0;
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || (c == '-' && label))) return false;
      if (++label > 63) return false;
    }
    previous = c;
  }
  return label && previous != '-';
}
struct Sample {
  uint32_t seconds, fractionUs, uncertaintyUs, delayUs;
  uint8_t stratum, leap;
};
// Validate the reply before it can discipline the local clock.
inline bool decode(const uint8_t *p, size_t length, const uint8_t *nonce,
                   uint32_t roundTripUs, Sample &sample) {
  if (length < 48 || roundTripUs > 1500000 || (p[0] & 7) != 4 ||
      ((p[0] >> 3) & 7) < 3 || ((p[0] >> 3) & 7) > 4 ||
      (p[0] >> 6) == 3 || p[1] < 1 || p[1] > 14 ||
      memcmp(p + 24, nonce, 8) != 0 || (!read32(p + 40) && !read32(p + 44))) return false;
  if (!read32(p + 32) && !read32(p + 36)) return false;
  uint64_t receive = (uint64_t(read32(p + 32)) << 32) | read32(p + 36);
  uint64_t transmit = (uint64_t(read32(p + 40)) << 32) | read32(p + 44);
  uint64_t processing = transmit - receive; // modulo arithmetic across an NTP era
  if (processing > (uint64_t(2) << 32)) return false;
  uint32_t processingUs = (processing * 1000000UL) >> 32;
  if (processingUs > roundTripUs + 2000) return false;
  uint32_t networkDelayUs = roundTripUs > processingUs ? roundTripUs - processingUs : 0;
  // Modulo subtraction also handles NTP's 2036 era rollover for 2024–2099.
  uint32_t seconds = read32(p + 40) - EPOCH;
  if (seconds < 1704067200UL || seconds > 4102444799UL) return false;
  uint32_t fractionUs = (uint64_t(read32(p + 44)) * 1000000UL) >> 32;
  fractionUs += networkDelayUs / 2;
  seconds += fractionUs / 1000000UL;
  // Include upstream root distance and local round trip; reject unusable distance.
  uint32_t delay = read32(p + 4);
  uint64_t distance = uint64_t(read32(p + 8)) * 1000000UL / 65536;
  if (!(delay & 0x80000000UL)) distance += uint64_t(delay) * 1000000UL / 131072;
  distance += networkDelayUs / 2 + 10000;
  if (distance > 10000000UL) return false;
  sample = {seconds, uint32_t(fractionUs % 1000000UL), uint32_t(distance), networkDelayUs, uint8_t(p[1] + 1), uint8_t(p[0] >> 6)};
  return true;
}
}
