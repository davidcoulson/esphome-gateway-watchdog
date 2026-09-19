#pragma once
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
struct esp_netif_t;
struct esp_netif_ip_info_t { esp_ip4_addr_t ip, netmask, gw; };
esp_netif_t *esp_netif_get_default_netif();
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
