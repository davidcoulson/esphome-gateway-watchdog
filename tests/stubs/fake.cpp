// Implementations behind the stub headers. See fake.h.
#include "fake.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_netif.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "ping/ping_sock.h"

namespace fake {
uint32_t now_ms = 0;
bool connected = true;
uint32_t gateway = 0;
bool have_default_netif = true;
std::vector<LogLine> logs;
int reboots = 0;
std::map<uint32_t, std::vector<uint8_t>> nvs;
int nvs_syncs = 0;
bool fail_new_session = false, fail_start = false;
int sessions_created = 0, sessions_deleted = 0;
uint32_t session_target = 0;
uint32_t next_rtt_ms = 3;

namespace {
struct Session {
  esp_ping_callbacks_t cbs{};
  esp_ping_config_t cfg{};
  bool started = false;
  int token = 0;
};
Session *live = nullptr;
}  // namespace

bool session_live() { return live != nullptr && live->started; }
void ping_reply() {
  if (session_live() && live->cbs.on_ping_success) live->cbs.on_ping_success(live, live->cbs.cb_args);
}
void ping_timeout() {
  if (session_live() && live->cbs.on_ping_timeout) live->cbs.on_ping_timeout(live, live->cbs.cb_args);
}

void reset_all(bool keep_nvs) {
  now_ms = 0; connected = true; gateway = 0; have_default_netif = true;
  logs.clear(); reboots = 0; nvs_syncs = 0;
  if (!keep_nvs) nvs.clear();
  fail_new_session = fail_start = false;
  sessions_created = sessions_deleted = 0; session_target = 0; next_rtt_ms = 3;
  delete live; live = nullptr;
}
int count_logs(const char *needle) {
  int n = 0;
  for (auto &l : logs) if (l.text.find(needle) != std::string::npos) n++;
  return n;
}
}  // namespace fake

namespace esphome {
uint32_t millis() { return fake::now_ms; }
static ESPPreferences prefs_instance;
ESPPreferences *global_preferences = &prefs_instance;
Application App;
void test_log(char level, const char *, const char *fmt, ...) {
  char buf[512];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  fake::logs.push_back({level, buf});
}
}  // namespace esphome

esp_err_t esp_ping_new_session(const esp_ping_config_t *cfg, const esp_ping_callbacks_t *cbs, esp_ping_handle_t *out) {
  if (fake::fail_new_session) return ESP_FAIL;
  if (fake::live != nullptr) return ESP_FAIL;  // the component must delete before creating
  fake::live = new fake::Session{*cbs, *cfg, false, ++fake::sessions_created};
  fake::session_target = cfg->target_addr.addr;
  *out = fake::live;
  return ESP_OK;
}
esp_err_t esp_ping_start(esp_ping_handle_t h) {
  if (h != fake::live || fake::fail_start) return ESP_FAIL;
  fake::live->started = true;
  return ESP_OK;
}
esp_err_t esp_ping_stop(esp_ping_handle_t h) {
  if (h == fake::live && fake::live) fake::live->started = false;
  return ESP_OK;
}
esp_err_t esp_ping_delete_session(esp_ping_handle_t h) {
  if (h == fake::live && fake::live) { delete fake::live; fake::live = nullptr; fake::sessions_deleted++; }
  return ESP_OK;
}
esp_err_t esp_ping_get_profile(esp_ping_handle_t, esp_ping_profile_t, void *data, uint32_t) {
  *static_cast<uint32_t *>(data) = fake::next_rtt_ms;
  return ESP_OK;
}

struct esp_netif_t { int unused; };
static esp_netif_t the_netif;
esp_netif_t *esp_netif_get_default_netif() { return fake::have_default_netif ? &the_netif : nullptr; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *info) {
  std::memset(info, 0, sizeof(*info));
  info->gw.addr = fake::gateway;
  return ESP_OK;
}
