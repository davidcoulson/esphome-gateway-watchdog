#pragma once
#include <cstdint>
// Just enough of lwIP's netif for the ND6 router table: its index.
struct netif { uint8_t num; };
#define netif_get_index(n) ((uint8_t) ((n)->num + 1))
