#include "ip_config.h"
#include <assert.h>
#include <stdio.h>

int main() {
  assert(IpConfig::valid(0xC0A80132, 0xC0A80101, 0xFFFFFF00, 0x08080808));
  assert(IpConfig::valid(0x0A000002, 0x0A000001, 0xFFFFFFFC, 0x01010101));
  assert(!IpConfig::valid(0xC0A80100, 0xC0A80101, 0xFFFFFF00, 0x08080808));
  assert(!IpConfig::valid(0xC0A801FF, 0xC0A80101, 0xFFFFFF00, 0x08080808));
  assert(!IpConfig::valid(0xC0A80132, 0xC0A80201, 0xFFFFFF00, 0x08080808));
  assert(!IpConfig::valid(0xC0A80132, 0xC0A80132, 0xFFFFFF00, 0x08080808));
  assert(!IpConfig::valid(0xC0A80132, 0xC0A80101, 0xFF00FF00, 0x08080808));
  assert(!IpConfig::valid(0xC0A80132, 0xC0A80101, 0xFFFFFFFF, 0x08080808));
  assert(!IpConfig::valid(0xC0A80432, 0xC0A80402, 0xFFFFFF00, 0x08080808));
  assert(!IpConfig::valid(0x7F000002, 0x7F000001, 0xFFFFFF00, 0x08080808));
  assert(!IpConfig::valid(0xC0A80132, 0xC0A80101, 0xFFFFFF00, 0xE0000001));
  puts("IP configuration tests passed");
}
