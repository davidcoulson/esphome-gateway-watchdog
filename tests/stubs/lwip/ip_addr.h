#pragma once
// lwIP address types, in the two shapes the component is built against:
//
//   STUB_IPV6=0  IPv4-only lwIP (network: enable_ipv6 off): ip_addr_t IS the
//                IPv4 struct, as in the real lwIP with LWIP_IPV6 == 0.
//   STUB_IPV6=1  dual-stack lwIP: ip_addr_t is a tagged union with a zone on
//                the IPv6 half, as with LWIP_IPV6 && LWIP_IPV6_SCOPES.
//
// tests/run.sh builds and runs the suite once in each shape.
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef STUB_IPV6
#define STUB_IPV6 0
#endif
#define LWIP_IPV6 STUB_IPV6

struct ip4_addr_t { uint32_t addr; };
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

#if !LWIP_IPV6

typedef ip4_addr_t ip_addr_t;
#define IPADDR_STRLEN_MAX 16
#define ipaddr_aton(s, out) ip4addr_aton((s), (out))
#define ip_addr_set_ip4_u32_val(ipaddr, val) ((ipaddr).addr = (val))
#define ip_addr_cmp(a, b) ((a)->addr == (b)->addr)
inline char *ipaddr_ntoa_r(const ip_addr_t *a, char *buf, int len) {
  std::snprintf(buf, len, IPSTR, IP2STR(a));
  return buf;
}

#else  // LWIP_IPV6

struct ip6_addr_t { uint32_t addr[4]; uint8_t zone; };
enum { IPADDR_TYPE_V4 = 0, IPADDR_TYPE_V6 = 6 };
struct ip_addr_t {
  union { ip6_addr_t ip6; ip4_addr_t ip4; } u_addr;
  uint8_t type;
};
#define IPADDR_STRLEN_MAX 46
#define LWIP_IPV6_NUM_ADDRESSES 3
#define PP_HTONL(x) htonl(x)
#define IP_IS_V6(a) ((a)->type == IPADDR_TYPE_V6)
#define ip_2_ip6(a) (&(a)->u_addr.ip6)
// fe80::/10, as lwIP tests it: first 16 bits masked to 0xffc0 == 0xfe80.
#define ip6_addr_islinklocal(a) ((ntohl((a)->addr[0]) & 0xffc00000UL) == 0xfe800000UL)
#define ip_addr_set_ip4_u32_val(ipaddr, val) \
  do { (ipaddr) = ip_addr_t{}; (ipaddr).u_addr.ip4.addr = (val); (ipaddr).type = IPADDR_TYPE_V4; } while (0)
#define ip_addr_copy_from_ip6(dest, src) \
  do { (dest) = ip_addr_t{}; (dest).u_addr.ip6 = (src); (dest).type = IPADDR_TYPE_V6; } while (0)

// Like lwIP's ip_addr_eq(): same family, same address, and for IPv6 the same
// zone - fe80::1 on one link is not fe80::1 on another.
inline bool ip_addr_cmp(const ip_addr_t *a, const ip_addr_t *b) {
  if (a->type != b->type) return false;
  if (a->type == IPADDR_TYPE_V4) return a->u_addr.ip4.addr == b->u_addr.ip4.addr;
  return std::memcmp(a->u_addr.ip6.addr, b->u_addr.ip6.addr, 16) == 0 && a->u_addr.ip6.zone == b->u_addr.ip6.zone;
}
// ipaddr_aton(): IPv4 dotted quad or IPv6 literal. Like lwIP, a "%name" zone
// suffix is accepted; lwIP looks the name up among its own netifs ("st1",
// "en1") and parses the address unzoned when nothing matches - which is what
// this models.
inline int ipaddr_aton(const char *s, ip_addr_t *out) {
  *out = ip_addr_t{};
  if (ip4addr_aton(s, &out->u_addr.ip4)) { out->type = IPADDR_TYPE_V4; return 1; }
  char literal[64];
  std::snprintf(literal, sizeof(literal), "%s", s);
  if (char *zone = std::strchr(literal, '%')) *zone = '\0';
  if (inet_pton(AF_INET6, literal, out->u_addr.ip6.addr) == 1) { out->type = IPADDR_TYPE_V6; return 1; }
  return 0;
}
inline char *ipaddr_ntoa_r(const ip_addr_t *a, char *buf, int len) {
  if (a->type == IPADDR_TYPE_V4) std::snprintf(buf, len, IPSTR, IP2STR(&a->u_addr.ip4));
  else inet_ntop(AF_INET6, a->u_addr.ip6.addr, buf, len);
  return buf;
}

#endif  // LWIP_IPV6
