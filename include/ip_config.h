#pragma once
#include <stdint.h>

namespace IpConfig {
inline bool unicast(uint32_t ip) {
  unsigned first = ip >> 24;
  return first > 0 && first < 224 && first != 127 && ip != 0xFFFFFFFFUL;
}
inline bool valid(uint32_t ip, uint32_t gateway, uint32_t mask, uint32_t dns) {
  uint32_t hosts = ~mask;
  if (!mask || hosts < 3 || (hosts & (hosts + 1)) || !unicast(ip) || !unicast(gateway) || !unicast(dns)) return false;
  if ((ip & mask) != (gateway & mask) || ip == gateway) return false;
  if (!(ip & hosts) || (ip & hosts) == hosts || !(gateway & hosts) || (gateway & hosts) == hosts) return false;
  // The setup AP reserves 192.168.4.0/24; overlapping primary networks cannot route reliably.
  return (ip & mask) != (0xC0A80401UL & mask);
}
}
