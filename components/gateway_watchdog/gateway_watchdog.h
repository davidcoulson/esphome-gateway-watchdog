#pragma once

#ifdef USE_ESP32

#include <cstdint>
#include <cinttypes>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/hal.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

namespace esphome {
namespace gateway_watchdog {

class GatewayWatchdog : public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_target_str(const char *target) { this->target_str_ = target; }
  void set_reboot_window(uint32_t ms) { this->reboot_window_ = ms; }
  void set_ping_interval(uint32_t ms) { this->ping_interval_ = ms; }
  void set_ping_timeout(uint32_t ms) { this->ping_timeout_ = ms; }
  void set_reboot_enabled(bool enabled) { this->reboot_enabled_ = enabled; }
  void set_max_reboots(uint32_t n) { this->max_reboots_ = n; }
  void set_budget_reset_after(uint32_t ms) { this->budget_reset_after_ = ms; }
  void set_arm_delay(uint32_t ms) { this->arm_delay_ = ms; }
  void set_prefix_router(bool enabled) { this->prefix_router_ = enabled; }

#ifdef USE_SENSOR
  void set_packet_loss_sensor(sensor::Sensor *s) { this->packet_loss_sensor_ = s; }
  void set_round_trip_time_sensor(sensor::Sensor *s) { this->round_trip_time_sensor_ = s; }
  void set_reboots_used_sensor(sensor::Sensor *s) { this->reboots_used_sensor_ = s; }
#endif

  // Called from the esp_ping task.
  void on_reply(uint32_t elapsed_ms);
  void on_timeout();

 protected:
  // Fills *out with the address to watch - the static target if configured,
  // else the IPv4 default gateway, else (IPv6-only network) the ND6 default
  // router - and *ifindex with the lwIP netif index the echo requests must
  // leave through (0 = let the stack route, which is what IPv4 does). False
  // when there is nothing to watch yet.
  bool resolve_target_(ip_addr_t *out, uint32_t *ifindex);
  void save_budget_();
  bool start_session_(const ip_addr_t &addr, uint32_t ifindex);
  void stop_session_();

  const char *target_str_{nullptr};
  uint32_t reboot_window_{120000};
  uint32_t ping_interval_{5000};
  uint32_t ping_timeout_{2000};
  bool reboot_enabled_{true};
  // Hard cap on how many times this component may reboot the node before it
  // gives up and just reports. A watchdog that keeps firing has stopped being
  // recovery and become the outage.
  uint32_t max_reboots_{2};
  uint32_t budget_reset_after_{3600000};
  uint32_t arm_delay_{60000};
  // Watch <prefix>::1 of the node's own /64 instead of a link-local router
  // address, and derive it even when no default router is advertised. See
  // resolve_target_().
  bool prefix_router_{false};

  esp_ping_handle_t handle_{nullptr};
  ip_addr_t target_addr_{};
  // Part of the target's identity: the same link-local router address on a
  // different interface is a different router.
  uint32_t target_ifindex_{0};

  // Written from the ping task, read from the main loop. Both are 32-bit
  // scalars, which are atomic on this target; the counters are only ever
  // incremented by the single ping task and zeroed by the main loop, so
  // the worst case is a sample landing either side of a publish boundary.
  volatile uint32_t last_reply_ms_{0};
  volatile uint32_t replies_{0};
  volatile uint32_t timeouts_{0};
  volatile uint32_t rtt_sum_ms_{0};
  volatile bool armed_{false};

  uint32_t last_loop_ms_{0};

  // Updated on EVERY callback, success or timeout. This is what separates
  // "the gateway is not answering" (timeouts keep arriving - a real signal)
  // from "the ping session itself is dead" (no callbacks at all - our
  // problem, not the network's).
  volatile uint32_t last_callback_ms_{0};

  // Set when we rebuild the session in response to a window expiring, so a
  // reboot needs a fresh session to fail too - not just the first one.
  bool session_rebuilt_for_window_{false};
  // When that rebuild happened. The flag belongs to one outage: once the
  // gateway has answered for a full reboot window past this point, the outage
  // is over and the next one earns its own rebuild.
  uint32_t rebuilt_at_ms_{0};

  // NVS-backed so the budget survives the very reboots it is counting -
  // otherwise every reboot resets the counter and the cap does nothing.
  ESPPreferenceObject pref_;
  uint32_t reboots_used_{0};
  bool budget_reset_done_{false};

  // Last time the gateway was demonstrably unreachable - a reboot window
  // expiring, whatever was done about it. The budget comes back only after
  // budget_reset_after_ with none of that, which is what "healthy" means;
  // uptime alone gave a flapping node two more reboots every hour.
  uint32_t last_trouble_ms_{0};

  // Latched once the arm delay has passed. millis() is 32-bit and wraps
  // after 49.7 days; comparing now < arm_delay_ on every loop made the
  // watchdog go deaf for arm_delay_ each time it did.
  bool past_arm_delay_{false};

#ifdef USE_SENSOR
  sensor::Sensor *packet_loss_sensor_{nullptr};
  sensor::Sensor *round_trip_time_sensor_{nullptr};
  sensor::Sensor *reboots_used_sensor_{nullptr};
#endif
};

}  // namespace gateway_watchdog
}  // namespace esphome

#endif  // USE_ESP32
