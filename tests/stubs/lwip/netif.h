#pragma once
#include <cstdint>
#include "lwip/ip_addr.h"
// Just enough of lwIP's netif: its index, for the ND6 router table (ESP32),
// and the default interface's IPv4 gateway (ESP8266).
struct netif { uint8_t num; ip_addr_t gw; };
#define netif_get_index(n) ((uint8_t) ((n)->num + 1))
extern struct netif *netif_default;
#if !LWIP_IPV6
#define netif_ip4_gw(n) ((const ip4_addr_t *) &(n)->gw)
#else
#define netif_ip4_gw(n) ((const ip4_addr_t *) &(n)->gw.u_addr.ip4)
#endif
