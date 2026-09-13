#pragma once

#ifdef USE_ESP32

#include <cstdint>
#include <cinttypes>

#include "esphome/core/component.h"
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

#ifdef USE_SENSOR
  void set_packet_loss_sensor(sensor::Sensor *s) { this->packet_loss_sensor_ = s; }
  void set_round_trip_time_sensor(sensor::Sensor *s) { this->round_trip_time_sensor_ = s; }
#endif

  // Called from the esp_ping task.
  void on_reply(uint32_t elapsed_ms);
  void on_timeout();

 protected:
  uint32_t resolve_target_();
  bool start_session_(uint32_t addr);
  void stop_session_();

  const char *target_str_{nullptr};
  uint32_t reboot_window_{120000};
  uint32_t ping_interval_{5000};
  uint32_t ping_timeout_{2000};
  bool reboot_enabled_{true};

  esp_ping_handle_t handle_{nullptr};
  uint32_t target_addr_{0};

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

#ifdef USE_SENSOR
  sensor::Sensor *packet_loss_sensor_{nullptr};
  sensor::Sensor *round_trip_time_sensor_{nullptr};
#endif
};

}  // namespace gateway_watchdog
}  // namespace esphome

#endif  // USE_ESP32
