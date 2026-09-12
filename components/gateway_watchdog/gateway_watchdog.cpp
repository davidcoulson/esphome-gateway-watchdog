#include "gateway_watchdog.h"

#ifdef USE_ESP32

#include <cmath>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

#include "esp_netif.h"

namespace esphome {
namespace gateway_watchdog {

static const char *const TAG = "gateway_watchdog";

// If loop() itself is starved for longer than this multiple of the ping
// interval, the gap is credited rather than counted against the gateway.
// An OTA write blocks the main loop for seconds at a time; without this,
// the first loop() afterwards would see a > reboot_window gap and reboot
// a perfectly healthy node.
static const uint32_t STALL_FACTOR = 4;

static void ping_success_cb(esp_ping_handle_t hdl, void *args) {
  uint32_t elapsed = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
  static_cast<GatewayWatchdog *>(args)->on_reply(elapsed);
}

static void ping_timeout_cb(esp_ping_handle_t hdl, void *args) {
  static_cast<GatewayWatchdog *>(args)->on_timeout();
}

void GatewayWatchdog::on_reply(uint32_t elapsed_ms) {
  this->last_reply_ms_ = millis();
  this->replies_ = this->replies_ + 1;
  this->rtt_sum_ms_ = this->rtt_sum_ms_ + elapsed_ms;
  this->armed_ = true;
}

void GatewayWatchdog::on_timeout() { this->timeouts_ = this->timeouts_ + 1; }

uint32_t GatewayWatchdog::resolve_target_() {
  if (this->target_str_ != nullptr) {
    ip4_addr_t parsed;
    if (ip4addr_aton(this->target_str_, &parsed) == 1)
      return parsed.addr;
    return 0;
  }
  // No explicit target: follow the DHCP-supplied default gateway, so the
  // same config works on every VLAN and re-targets if the lease changes.
  //
  // esp_netif_get_default_netif() rather than a hardcoded "WIFI_STA_DEF"
  // key: that key does not exist on an Ethernet-only node, and asking for
  // the current default route is the right question on a node that has
  // both. Returns whichever interface actually carries the default route.
  esp_netif_t *netif = esp_netif_get_default_netif();
  if (netif == nullptr)
    return 0;
  esp_netif_ip_info_t info;
  if (esp_netif_get_ip_info(netif, &info) != ESP_OK)
    return 0;
  return info.gw.addr;
}

void GatewayWatchdog::stop_session_() {
  if (this->handle_ != nullptr) {
    esp_ping_stop(this->handle_);
    esp_ping_delete_session(this->handle_);
    this->handle_ = nullptr;
  }
  this->armed_ = false;
}

bool GatewayWatchdog::start_session_(uint32_t addr) {
  this->stop_session_();

  ip_addr_t target;
  memset(&target, 0, sizeof(target));
#if LWIP_IPV6
  target.type = IPADDR_TYPE_V4;
  target.u_addr.ip4.addr = addr;
#else
  target.addr = addr;
#endif

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = this->ping_interval_;
  cfg.timeout_ms = this->ping_timeout_;

  esp_ping_callbacks_t cbs;
  memset(&cbs, 0, sizeof(cbs));
  cbs.cb_args = this;
  cbs.on_ping_success = ping_success_cb;
  cbs.on_ping_timeout = ping_timeout_cb;

  if (esp_ping_new_session(&cfg, &cbs, &this->handle_) != ESP_OK) {
    this->handle_ = nullptr;
    ESP_LOGW(TAG, "esp_ping_new_session failed");
    return false;
  }
  if (esp_ping_start(this->handle_) != ESP_OK) {
    esp_ping_delete_session(this->handle_);
    this->handle_ = nullptr;
    ESP_LOGW(TAG, "esp_ping_start failed");
    return false;
  }

  this->target_addr_ = addr;
  this->last_reply_ms_ = millis();
  ESP_LOGI(TAG, "watching " IPSTR, IP2STR((esp_ip4_addr_t *) &addr));
  return true;
}

void GatewayWatchdog::setup() { this->last_loop_ms_ = millis(); }

void GatewayWatchdog::loop() {
  const uint32_t now = millis();
  const uint32_t stall_limit = this->ping_interval_ * STALL_FACTOR;

  if (this->last_loop_ms_ != 0 && (now - this->last_loop_ms_) > stall_limit) {
    ESP_LOGW(TAG, "loop starved %" PRIu32 " ms - crediting window",
             now - this->last_loop_ms_);
    this->last_reply_ms_ = now;
  }
  this->last_loop_ms_ = now;

  // A down link is the network component's own problem (and, on WiFi,
  // wifi.reboot_timeout's). Overlapping them would only make the reboot
  // reason ambiguous. network::is_connected() covers WiFi and Ethernet.
  if (!network::is_connected()) {
    this->last_reply_ms_ = now;
    return;
  }

  const uint32_t addr = this->resolve_target_();
  if (addr == 0) {
    this->last_reply_ms_ = now;
    return;
  }

  if (this->handle_ == nullptr || addr != this->target_addr_) {
    this->start_session_(addr);
    return;
  }

  // Never reboot on a target we have not reached even once. A node that
  // has never seen its gateway (wrong VLAN, config sent to the wrong
  // device) must not sit in a reboot loop.
  if (!this->armed_)
    return;

  const uint32_t since = now - this->last_reply_ms_;
  if (since > this->reboot_window_) {
    if (this->reboot_enabled_) {
      ESP_LOGE(TAG, "gateway unreachable %" PRIu32 " ms - rebooting", since);
      App.safe_reboot();
    } else {
      ESP_LOGE(TAG, "gateway unreachable %" PRIu32 " ms (reboot disabled)", since);
      this->last_reply_ms_ = now;
    }
  }
}

void GatewayWatchdog::update() {
  const uint32_t replies = this->replies_;
  const uint32_t timeouts = this->timeouts_;
  const uint32_t rtt_sum = this->rtt_sum_ms_;
  this->replies_ = 0;
  this->timeouts_ = 0;
  this->rtt_sum_ms_ = 0;

  const uint32_t total = replies + timeouts;

#ifdef USE_SENSOR
  if (this->packet_loss_sensor_ != nullptr) {
    // No echo requests completed in this window at all (session not up
    // yet, or WiFi down) - publishing 0% would read as "healthy".
    if (total == 0) {
      this->packet_loss_sensor_->publish_state(NAN);
    } else {
      this->packet_loss_sensor_->publish_state(100.0f * (float) timeouts / (float) total);
    }
  }
  if (this->round_trip_time_sensor_ != nullptr) {
    if (replies == 0) {
      this->round_trip_time_sensor_->publish_state(NAN);
    } else {
      this->round_trip_time_sensor_->publish_state((float) rtt_sum / (float) replies);
    }
  }
#endif
}

void GatewayWatchdog::dump_config() {
  ESP_LOGCONFIG(TAG, "Gateway Watchdog:");
  if (this->target_str_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Target: %s (static)", this->target_str_);
  } else {
    ESP_LOGCONFIG(TAG, "  Target: DHCP default gateway");
  }
  ESP_LOGCONFIG(TAG, "  Ping interval: %" PRIu32 " ms", this->ping_interval_);
  ESP_LOGCONFIG(TAG, "  Ping timeout: %" PRIu32 " ms", this->ping_timeout_);
  ESP_LOGCONFIG(TAG, "  Reboot window: %" PRIu32 " ms", this->reboot_window_);
  ESP_LOGCONFIG(TAG, "  Reboot enabled: %s", YESNO(this->reboot_enabled_));
}

}  // namespace gateway_watchdog
}  // namespace esphome

#endif  // USE_ESP32
