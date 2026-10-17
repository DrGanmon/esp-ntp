#include "ntp_fallback.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

void put32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
void reply(uint8_t *p, const uint8_t *nonce, uint32_t epoch) {
  memset(p, 0, 48);
  p[0] = 0x24; p[1] = 2;
  memcpy(p + 24, nonce, 8);
  put32(p + 40, epoch + Fallback::EPOCH);
  put32(p + 44, 0xE0000000UL); // 875 ms
  put32(p + 8, 65536); // 1 second upstream dispersion
}
int main() {
  assert(Fallback::validHost("de.pool.ntp.org"));
  assert(Fallback::validHost("192.168.1.10"));
  assert(Fallback::validHost("ntp-server.lan"));
  for (const char *host : {"", ".pool.org", "pool..org", "-ntp.org", "ntp-.org",
                           "https://ntp.org", "ntp.org:123", "ntp.org\"", "ntp.org/path"})
    assert(!Fallback::validHost(host));
  char longHost[65]; memset(longHost, 'a', 64); longHost[64] = 0;
  assert(!Fallback::validHost(longHost));
  uint8_t nonce[8] = {1,2,3,4,5,6,7,8}, packet[48];
  Fallback::Sample result;
  // Subsecond carry, propagation estimate, upstream stratum and root distance.
  reply(packet, nonce, 1792195200UL);
  assert(Fallback::decode(packet, 48, nonce, 400000, result));
  assert(result.seconds == 1792195201UL && result.fractionUs == 75000);
  assert(result.stratum == 3 && result.leap == 0 && result.uncertaintyUs == 1210000);
  // The NTP seconds field wraps in 2036; contemporary Unix seconds must survive.
  reply(packet, nonce, 2208988800UL);
  assert(Fallback::decode(packet, 48, nonce, 0, result));
  assert(result.seconds == 2208988800UL);
  packet[0] |= 0x40;
  assert(Fallback::decode(packet, 48, nonce, 0, result) && result.leap == 1);
  packet[0] = 0xE4; assert(!Fallback::decode(packet, 48, nonce, 0, result));
  reply(packet, nonce, 1792195200UL);
  assert(!Fallback::decode(packet, 47, nonce, 0, result));
  assert(!Fallback::decode(packet, 48, nonce, 1500001, result));
  packet[24] ^= 1; assert(!Fallback::decode(packet, 48, nonce, 0, result));
  packet[24] ^= 1;
  for (uint8_t stratum : {0, 15, 16}) {
    packet[1] = stratum; assert(!Fallback::decode(packet, 48, nonce, 0, result));
  }
  packet[1] = 14;
  assert(Fallback::decode(packet, 48, nonce, 0, result) && result.stratum == 15);
  packet[0] = 0x23; assert(!Fallback::decode(packet, 48, nonce, 0, result));
  packet[0] = 0x24;
  memset(packet + 40, 0, 8); assert(!Fallback::decode(packet, 48, nonce, 0, result));
  reply(packet, nonce, 0); assert(!Fallback::decode(packet, 48, nonce, 0, result));
  reply(packet, nonce, 1792195200UL);
  put32(packet + 8, 0xFFFF0000UL); assert(!Fallback::decode(packet, 48, nonce, 0, result));
  puts("Fallback validation tests passed");
}
