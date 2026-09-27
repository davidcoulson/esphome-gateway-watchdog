#pragma once
// lwIP's private ND6 tables, as esp-lwip lays them out (the fields the
// component reads). Tests fill default_router_list[] through fake.h.
#include <cstdint>
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#define LWIP_ND6_NUM_ROUTERS 3
enum nd6_neighbor_cache_entry_state { ND6_NO_ENTRY = 0, ND6_INCOMPLETE, ND6_REACHABLE, ND6_STALE };
struct nd6_neighbor_cache_entry {
  ip6_addr_t next_hop_address;
  struct netif *netif;
  uint8_t state;
};
struct nd6_router_list_entry {
  struct nd6_neighbor_cache_entry *neighbor_entry;
  uint32_t invalidation_timer;  // seconds of router lifetime left
  uint8_t flags;
};
extern struct nd6_router_list_entry default_router_list[];
