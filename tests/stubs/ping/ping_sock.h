#pragma once
#include <cstdint>
#include "lwip/ip_addr.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef void *esp_ping_handle_t;
typedef enum { ESP_PING_PROF_TIMEGAP } esp_ping_profile_t;
#define ESP_PING_COUNT_INFINITE 0
struct esp_ping_config_t {
  uint32_t count; uint32_t interval_ms; uint32_t timeout_ms; ip_addr_t target_addr;
  uint32_t interface;  // lwIP netif index to bind to (SO_BINDTODEVICE); 0 = unbound
};
#define ESP_PING_DEFAULT_CONFIG() esp_ping_config_t{5, 1000, 1000, ip_addr_t{}, 0}
struct esp_ping_callbacks_t {
  void *cb_args;
  void (*on_ping_success)(esp_ping_handle_t, void *);
  void (*on_ping_timeout)(esp_ping_handle_t, void *);
  void (*on_ping_end)(esp_ping_handle_t, void *);
};
esp_err_t esp_ping_new_session(const esp_ping_config_t *, const esp_ping_callbacks_t *, esp_ping_handle_t *);
esp_err_t esp_ping_start(esp_ping_handle_t);
esp_err_t esp_ping_stop(esp_ping_handle_t);
esp_err_t esp_ping_delete_session(esp_ping_handle_t);
esp_err_t esp_ping_get_profile(esp_ping_handle_t, esp_ping_profile_t, void *, uint32_t);
