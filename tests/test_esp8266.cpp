// Host tests for the ESP8266 backend of GatewayWatchdog.
//
// The ESP32 suite (test_gateway_watchdog.cpp) covers the reboot policy, which
// both builds share. This one covers what only the ESP8266 has: echo requests
// sent from loop() over a raw lwIP socket, replies picked out of every ICMP
// packet the stack receives, and timeouts the component times itself.
//
// Same fleet timings as the ESP32 suite: 5 s interval, 2 s timeout, 300 s
// reboot window, 60 s arm delay. loop() runs once a simulated second.
//
//   tests/run.sh

#include <cmath>
#include <cstdio>

#include "../components/gateway_watchdog/gateway_watchdog.h"
#include "lwip/icmp.h"
#include "lwip/raw.h"
#include "stubs/esphome/components/sensor/sensor.h"
#include "stubs/fake.h"
#include "stubs/fake8266.h"

using esphome::gateway_watchdog::GatewayWatchdog;

namespace {

struct Probe : GatewayWatchdog {
  using GatewayWatchdog::armed_;
  using GatewayWatchdog::echo_id_;
  using GatewayWatchdog::reboots_used_;
};

enum class Gw { REPLIES, SILENT };

constexpr uint32_t GW = 0x0101a8c0;   // 192.168.1.1 in lwIP byte order
constexpr uint32_t GW2 = 0xfe01a8c0;  // 192.168.1.254
constexpr uint32_t OTHER = 0x0501a8c0;
constexpr uint32_t ARM = 60000;
constexpr uint32_t WINDOW = 300000;

struct Sim {
  Probe wd;
  esphome::sensor::Sensor loss, rtt, used;

  explicit Sim(const char *target = nullptr) {
    fake::reset_all(false);
    fake8266::reset();
    fake8266::set_gateway(GW);
    fake::now_ms = 300;
    if (target != nullptr) wd.set_target_str(target);
    wd.set_ping_interval(5000);
    wd.set_ping_timeout(2000);
    wd.set_reboot_window(WINDOW);
    wd.set_arm_delay(ARM);
    wd.set_max_reboots(2);
    wd.set_budget_reset_after(3600000);
    wd.set_packet_loss_sensor(&loss);
    wd.set_round_trip_time_sensor(&rtt);
    wd.set_reboots_used_sensor(&used);
    wd.setup();
  }

  // One loop() a second. When the gateway replies, it does so 7 ms after
  // each request goes out.
  void run(uint32_t ms, Gw gw) {
    const int reboots_before = fake::reboots;
    for (uint32_t t = 0; t < ms; t += 1000) {
      fake::now_ms += 1000;
      const int sends = fake8266::sends;
      wd.loop();
      if (gw == Gw::REPLIES && fake8266::sends != sends) {
        fake::now_ms += 7;
        fake8266::reply_to_last();
        fake::now_ms -= 7;
      }
      if (fake::reboots != reboots_before) return;
    }
  }
  void arm() { run(ARM + 20000, Gw::REPLIES); }
};

int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

}  // namespace

int main() {
  std::printf("== sending\n");
  {
    Sim s;
    s.run(ARM - 5000, Gw::REPLIES);
    check(fake8266::pcbs_created == 0 && fake8266::sends == 0, "nothing is opened or sent during the arm delay");
    s.run(10000, Gw::SILENT);
    check(fake8266::pcb_open, "a raw ICMP socket is open once armed");
    check(fake8266::sends >= 1, "echo requests go out");
    check(fake8266::last.checksum_ok, "the echo request's ICMP checksum is valid");
    check(fake8266::last.len == 40, "8-byte echo header + 32 bytes of payload, as esp_ping sends");
    check(fake8266::last.to.addr == GW, "the request goes to the default gateway");
  }
  {
    Sim s;
    s.run(ARM + 1000, Gw::SILENT);  // session opens on this loop
    const int base = fake8266::sends;
    s.run(30000, Gw::REPLIES);
    check(fake8266::sends - base == 6, "one request per ping interval (6 in 30 s at 5 s)");
    check(fake8266::last.seq == fake8266::sends, "sequence numbers count up from 1");
  }

  std::printf("== replies\n");
  {
    Sim s;
    s.arm();
    check(s.wd.armed_, "a reply arms the watchdog");
    s.wd.update();  // drain the arm-up period
    s.run(60000, Gw::REPLIES);
    s.wd.update();
    check(s.loss.state == 0.0f, "replies to every request read as 0% loss");
    check(s.rtt.state == 7.0f, "round-trip time is the request-to-reply gap (7 ms)");
    check(fake8266::pbufs_live == 0, "every pbuf is freed, sent and received");
  }
  {
    Sim s;
    s.arm();
    check(fake8266::deliver(ICMP_ECHO, s.wd.echo_id_, 1, GW) == 0,
          "an echo REQUEST to this node is passed on, so it still answers pings");
    check(fake8266::deliver(ICMP_ER, static_cast<uint16_t>(s.wd.echo_id_ + 1), 1, GW) == 0,
          "a reply carrying another identifier is passed on");
    check(fake8266::deliver(ICMP_ER, s.wd.echo_id_, fake8266::last.seq, OTHER) == 0,
          "a reply from another host is passed on");
    check(fake8266::pbufs_live == 0, "passed-on packets are left for lwIP to free");
  }
  {
    // A reply that turns up after its request has timed out.
    Sim s;
    s.arm();
    s.wd.update();
    fake::now_ms += 1000;
    s.wd.loop();  // may or may not send; make sure one is outstanding
    s.run(5000, Gw::SILENT);
    const uint16_t late_seq = fake8266::last.seq;
    s.run(3000, Gw::SILENT);  // past the 2 s timeout
    check(fake8266::deliver(ICMP_ER, s.wd.echo_id_, late_seq, GW) == 1,
          "a late reply to our own request is consumed");
    s.wd.update();
    check(std::isnan(s.rtt.state), "but it does not count as a reply");
    check(fake8266::pbufs_live == 0, "and its pbuf is freed");
  }

  std::printf("== timeouts\n");
  {
    Sim s;
    s.arm();
    s.wd.update();
    s.run(60000, Gw::SILENT);
    s.wd.update();
    check(s.loss.state == 100.0f, "no replies read as 100% loss");
    check(std::isnan(s.rtt.state), "and no round-trip time");
  }
  {
    Sim s;
    s.arm();
    s.wd.update();
    fake8266::send_err = ERR_RTE;
    s.run(30000, Gw::SILENT);
    s.wd.update();
    check(s.loss.state == 100.0f, "a request that cannot be sent counts as lost, as on esp_ping");
    fake8266::send_err = ERR_OK;
    fake8266::fail_alloc = true;
    s.run(30000, Gw::SILENT);
    s.wd.update();
    check(s.loss.state == 100.0f, "so does one there was no memory to build");
    check(fake::count_logs("no ping callbacks") == 0, "neither looks like a dead session");
  }

  std::printf("== sessions\n");
  {
    Sim s;
    s.arm();
    const uint16_t old_id = s.wd.echo_id_;
    const uint16_t old_seq = fake8266::last.seq;
    fake8266::set_gateway(GW2);
    s.run(2000, Gw::SILENT);
    check(fake8266::pcbs_removed == 1 && fake8266::pcb_open, "a new gateway closes the socket and opens another");
    check(s.wd.echo_id_ != old_id, "the new session has a new echo identifier");
    check(fake8266::deliver(ICMP_ER, old_id, old_seq, GW) == 0, "so a reply to the old session is not taken for it");
    s.run(10000, Gw::SILENT);
    check(fake8266::last.to.addr == GW2, "requests now go to the new gateway");
  }
  {
    Sim s("10.2.4.1");
    s.run(ARM + 10000, Gw::SILENT);
    check(fake8266::last.to.addr == 0x0104020a, "a static target is pinged instead of the gateway");
  }
  {
    Sim s;
    fake8266::set_no_netif();
    s.run(ARM + 30000, Gw::SILENT);
    check(fake8266::pcbs_created == 0, "no default netif: nothing to watch, nothing opened");
    fake8266::set_gateway(0);
    s.run(30000, Gw::SILENT);
    check(fake8266::pcbs_created == 0, "a netif with no gateway yet: the same");
  }

  std::printf("== reboot policy over the ESP8266 backend\n");
  {
    Sim s;
    s.arm();
    s.run(WINDOW + 10000, Gw::SILENT);
    check(fake::reboots == 0 && fake8266::pcbs_removed == 1, "the first lost window rebuilds the session, no reboot");
    s.run(15000, Gw::REPLIES);
    s.run(WINDOW + 10000, Gw::SILENT);
    check(fake::reboots == 1, "a second lost window, on the rebuilt session, reboots");
    check(s.wd.reboots_used_ == 1 && fake::nvs_syncs > 0, "and the reboot is charged to the saved budget");
  }
  {
    Sim s;
    s.run(ARM + WINDOW * 3, Gw::SILENT);
    check(fake::reboots == 0, "a gateway never reached is never rebooted for");
  }

  std::printf("\n%s\n", failures == 0 ? "all passed" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
