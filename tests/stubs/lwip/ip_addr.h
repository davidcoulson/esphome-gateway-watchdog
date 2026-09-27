#pragma once
#include <cstdint>
#include <cstdio>
#define LWIP_IPV6 0
struct ip4_addr_t { uint32_t addr; };
typedef ip4_addr_t ip_addr_t;
typedef ip4_addr_t esp_ip4_addr_t;
// lwIP stores addresses in network byte order; on a little-endian host that
// puts the first octet in the low byte.
inline int ip4addr_aton(const char *s, ip4_addr_t *out) {
  unsigned a, b, c, d; char extra;
  if (std::sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
    return 0;
  out->addr = a | (b << 8) | (c << 16) | (d << 24);
  return 1;
}
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) (unsigned) ((p)->addr & 0xff), (unsigned) (((p)->addr >> 8) & 0xff), \
                  (unsigned) (((p)->addr >> 16) & 0xff), (unsigned) (((p)->addr >> 24) & 0xff)

// Dual-stack helpers the component uses; here ip_addr_t is the IPv4 struct.
#define ipaddr_aton(s, out) ip4addr_aton((s), (out))
#define ip_addr_set_ip4_u32_val(ipaddr, val) ((ipaddr).addr = (val))
#define ip_addr_cmp(a, b) ((a)->addr == (b)->addr)
inline const char *ipaddr_ntoa(const ip_addr_t *a) {
  static char buf[16];
  std::snprintf(buf, sizeof buf, IPSTR, IP2STR(a));
  return buf;
}
