#pragma once
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
struct esp_netif_t;
struct esp_netif_ip_info_t { esp_ip4_addr_t ip, netmask, gw; };
esp_netif_t *esp_netif_get_default_netif();
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
int esp_netif_get_netif_impl_index(esp_netif_t *);
typedef bool (*esp_netif_find_predicate_t)(esp_netif_t *netif, void *ctx);
esp_netif_t *esp_netif_find_if(esp_netif_find_predicate_t fn, void *ctx);
#if LWIP_IPV6
typedef ip6_addr_t esp_ip6_addr_t;  // same layout in ESP-IDF: addr[4] + zone
typedef enum {
  ESP_IP6_ADDR_IS_UNKNOWN, ESP_IP6_ADDR_IS_GLOBAL, ESP_IP6_ADDR_IS_LINK_LOCAL,
  ESP_IP6_ADDR_IS_SITE_LOCAL, ESP_IP6_ADDR_IS_UNIQUE_LOCAL, ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6
} esp_ip6_addr_type_t;
esp_ip6_addr_type_t esp_netif_ip6_get_addr_type(const esp_ip6_addr_t *);
int esp_netif_get_all_preferred_ip6(esp_netif_t *, esp_ip6_addr_t if_ip6[]);
#endif
