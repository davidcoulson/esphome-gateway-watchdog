// Host tests for GatewayWatchdog.
//
// These compile the REAL components/gateway_watchdog/gateway_watchdog.cpp
// against small stubs (tests/stubs/) for the ESPHome and ESP-IDF APIs it
// touches: millis(), NVS preferences, esp_ping, the default netif, logging and
// App.safe_reboot(). Nothing is transcribed, so the tests cannot drift from
// the component.
//
// The simulation mirrors the fleet config: 5 s ping interval, 300 s reboot
// window, 60 s arm delay, budget of 2 reboots returned after 1 h healthy.
// The main loop runs once a simulated second; the "ping task" fires a reply,
// a timeout or nothing every ping interval.
//
//   tests/run.sh

#include <cmath>
#include <cstdio>
#include <string>

#include "../components/gateway_watchdog/gateway_watchdog.h"
#include "stubs/esphome/components/sensor/sensor.h"
#include "stubs/fake.h"

using esphome::gateway_watchdog::GatewayWatchdog;

namespace {

// Exposes the internals a test needs to assert on.
struct Probe : GatewayWatchdog {
  using GatewayWatchdog::armed_;
  using GatewayWatchdog::reboots_used_;
  using GatewayWatchdog::session_rebuilt_for_window_;
};

enum class Ping { REPLY, TIMEOUT, SILENT };

constexpr uint32_t GW = 0x0101a8c0;         // 192.168.1.1 in lwIP byte order
constexpr uint32_t GW2 = 0xfe01a8c0;        // 192.168.1.254
constexpr uint32_t INTERVAL = 5000;
constexpr uint32_t WINDOW = 300000;
constexpr uint32_t ARM = 60000;
constexpr uint32_t BUDGET_RESET = 3600000;

struct Sim {
  Probe wd;
  esphome::sensor::Sensor loss, rtt, used;

  // A fresh boot. keep_nvs simulates a reboot: the reboot budget survives.
  explicit Sim(bool keep_nvs = false, uint32_t max_reboots = 2, bool reboot_enabled = true) {
    fake::reset_all(keep_nvs);
    fake::gateway = GW;
    fake::now_ms = 300;  // setup() runs a few hundred ms into boot on hardware
    wd.set_ping_interval(INTERVAL);
    wd.set_ping_timeout(2000);
    wd.set_reboot_window(WINDOW);
    wd.set_arm_delay(ARM);
    wd.set_max_reboots(max_reboots);
    wd.set_budget_reset_after(BUDGET_RESET);
    wd.set_reboot_enabled(reboot_enabled);
    wd.set_packet_loss_sensor(&loss);
    wd.set_round_trip_time_sensor(&rtt);
    wd.set_reboots_used_sensor(&used);
    wd.setup();
  }

  // Run the main loop for `ms`, one loop() per second, with the ping task
  // producing `mode` every interval while a session is live.
  //
  // Stops at the first reboot: on hardware safe_reboot() never returns, but
  // the stub does, and carrying on would "reboot" again every loop.
  void run(uint32_t ms, Ping mode) {
    const int reboots_before = fake::reboots;
    for (uint32_t t = 0; t < ms; t += 1000) {
      fake::now_ms += 1000;
      if (++ticks_ % (INTERVAL / 1000) == 0) {
        if (mode == Ping::REPLY) fake::ping_reply();
        else if (mode == Ping::TIMEOUT) fake::ping_timeout();
      }
      wd.loop();
      if (fake::reboots != reboots_before) return;
    }
  }
  uint32_t ticks_{0};

  // Boot, arm and get a first reply, so the watchdog is live and healthy.
  void arm() { run(ARM + 20000, Ping::REPLY); }

  // The only sequence that reaches a reboot: a window of loss, the rebuild,
  // the fresh session reaching the gateway, then a second window of loss.
  void lose_rebuild_recover_lose() {
    run(WINDOW + 10000, Ping::TIMEOUT);
    run(15000, Ping::REPLY);
    run(WINDOW + 10000, Ping::TIMEOUT);
  }
};

int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

}  // namespace

int main() {
  std::printf("== arming ==\n");
  {
    Sim s;
    s.run(ARM - 1000, Ping::REPLY);
    check(fake::sessions_created == 0, "no ping session during the arm delay");
    s.run(5000, Ping::REPLY);
    check(fake::sessions_created == 1 && fake::target_v4() == GW, "session starts on the DHCP gateway once armed");
    s.run(10000, Ping::REPLY);
    check(s.wd.armed_, "armed by the first reply");
  }
  {
    // The bug fixed alongside these tests: the arm delay itself was reported
    // as a ~59.7 s loop stall on every boot.
    Sim s;
    s.arm();
    check(fake::count_logs("loop starved") == 0, "the arm delay is not reported as a loop stall");
  }

  std::printf("\n== loop stalls (e.g. an OTA write blocking the main loop) ==\n");
  {
    Sim s;
    s.arm();
    fake::now_ms += 30000;  // 30 s with no loop() - past the 4x-interval stall limit
    s.wd.loop();
    check(fake::count_logs("loop starved") == 1, "a real stall after arming is detected");
    check(fake::reboots == 0, "and credited, not counted against the gateway");
  }
  {
    Sim s;
    s.arm();
    fake::now_ms += WINDOW + 60000;  // stalled for longer than the whole reboot window
    s.wd.loop();
    s.run(10000, Ping::REPLY);
    check(fake::reboots == 0, "a stall longer than the reboot window does not reboot a healthy node");
  }
  {
    Sim s;
    s.arm();
    fake::now_ms += 15000;  // under the 20 s stall limit
    s.wd.loop();
    check(fake::count_logs("loop starved") == 0, "a gap under the stall limit is not a stall");
  }

  std::printf("\n== things that must never reboot ==\n");
  {
    Sim s;
    s.run(20 * 60000, Ping::TIMEOUT);
    check(!s.wd.armed_ && fake::reboots == 0, "a gateway never reached even once (wrong VLAN) never reboots");
  }
  {
    Sim s;
    s.arm();
    fake::connected = false;
    s.run(20 * 60000, Ping::SILENT);
    check(fake::reboots == 0, "link down is left to the network component");
    fake::connected = true;
    s.run(30000, Ping::REPLY);
    check(fake::reboots == 0 && s.wd.armed_, "and recovers cleanly when the link returns");
  }
  {
    Sim s;
    s.arm();
    fake::gateway = 0;
    s.run(20 * 60000, Ping::SILENT);
    check(fake::reboots == 0, "no default gateway (no lease yet) never reboots");
  }
  {
    Sim s;
    s.arm();
    s.run(WINDOW + 10000, Ping::TIMEOUT);
    check(fake::reboots == 0 && fake::count_logs("rebuilding session before considering a reboot") == 1,
          "the first expired window rebuilds the session instead of rebooting");
    s.run(20 * 60000, Ping::TIMEOUT);
    check(fake::reboots == 0,
          "a gateway the fresh session cannot reach either is treated as the network, not this node");
  }

  std::printf("\n== the reboot path ==\n");
  {
    Sim s;
    s.arm();
    s.lose_rebuild_recover_lose();
    check(fake::reboots == 1, "loss, rebuild, recovery, loss again: reboots");
    check(s.wd.reboots_used_ == 1 && fake::nvs_syncs >= 1, "the budget is spent and synced to flash before rebooting");
  }
  {
    Sim s(false, 2, /*reboot_enabled=*/false);
    s.arm();
    s.lose_rebuild_recover_lose();
    check(fake::reboots == 0 && fake::count_logs("reboot disabled") >= 1, "reboot: false reports instead of rebooting");
  }

  std::printf("\n== the reboot budget ==\n");
  {
    // Two reboots are recovery; the third is a loop.
    { Sim s; s.arm(); s.lose_rebuild_recover_lose(); }                  // reboot 1
    { Sim s(true); s.arm(); s.lose_rebuild_recover_lose(); }            // reboot 2
    Sim s(true);
    check(s.wd.reboots_used_ == 2 && fake::count_logs("restored reboot budget: 2 of 2") == 1,
          "the budget survives the reboots it counts");
    s.arm();
    s.lose_rebuild_recover_lose();
    check(fake::reboots == 0 && fake::count_logs("reboot budget spent") >= 1,
          "a spent budget stays up and reports instead of rebooting a third time");
  }
  {
    { Sim s(false, 0); s.arm(); s.lose_rebuild_recover_lose(); }
    { Sim s(true, 0); s.arm(); s.lose_rebuild_recover_lose(); }
    Sim s(true, 0);
    s.arm();
    s.lose_rebuild_recover_lose();
    check(fake::reboots == 1 && s.wd.reboots_used_ == 3, "max_reboots: 0 means unlimited");
  }
  {
    { Sim s; s.arm(); s.lose_rebuild_recover_lose(); }
    Sim s(true);
    s.run(ARM + BUDGET_RESET + 60000, Ping::REPLY);
    check(s.wd.reboots_used_ == 0 && s.used.state == 0.0f, "an hour healthy returns the budget");
    Sim again(true);
    check(again.wd.reboots_used_ == 0, "and the returned budget is persisted");
  }
  {
    // Spend the budget, then boot into a gateway that keeps flapping: seven
    // minutes of loss (a window expires each time), one minute answering.
    // The node re-arms between outages, so a reset that only asked "an hour
    // of uptime and armed right now?" handed it two more reboots every hour.
    { Sim s; s.arm(); s.lose_rebuild_recover_lose(); }
    { Sim s(true); s.arm(); s.lose_rebuild_recover_lose(); }
    Sim s(true);
    s.arm();
    for (int i = 0; i < 9; i++) {           // ~72 minutes
      s.run(7 * 60000, Ping::TIMEOUT);
      s.run(60000, Ping::REPLY);
    }
    check(fake::count_logs("rebuilding session before considering a reboot") >= 4,
          "(the flapping gateway keeps expiring reboot windows)");
    check(fake::count_logs("resetting reboot budget") == 0 && fake::reboots == 0,
          "an hour of uptime with the gateway flapping does not return the budget");
    s.run(BUDGET_RESET + 60000, Ping::REPLY);
    check(s.wd.reboots_used_ == 0, "an hour of real health afterwards does");
  }

  std::printf("\n== millis() wrap (every 49.7 days) ==\n");
  {
    // millis() is 32-bit. Comparing now < arm_delay on every loop made the
    // watchdog go deaf for the arm delay each time it wrapped.
    Sim s;
    s.arm();
    fake::now_ms = 0xFFFFFFFFu - 10000;  // 10 s before the wrap
    s.run(20000, Ping::REPLY);           // across it: now 10 s after
    fake::gateway = GW2;                 // new lease 10 s after the wrap
    s.run(5000, Ping::REPLY);
    check(fake::target_v4() == GW2, "the watchdog keeps working straight through a millis() wrap");
  }

  std::printf("\n== ping session management ==\n");
  {
    Sim s;
    s.arm();
    const int before = fake::sessions_created;
    s.run(40000, Ping::SILENT);  // no callback at all: the session died, not the gateway
    check(fake::sessions_created > before && fake::count_logs("no ping callbacks") >= 1 && fake::reboots == 0,
          "a silent session is rebuilt, never treated as an outage");
  }
  {
    Sim s;
    s.arm();
    fake::gateway = GW2;  // new DHCP lease on a different gateway
    s.run(3000, Ping::REPLY);
    check(fake::target_v4() == GW2, "follows the default gateway when the lease changes");
  }
  {
    Sim s;
    fake::fail_new_session = true;
    s.run(ARM + 30000, Ping::REPLY);
    check(!fake::session_live() && fake::count_logs("esp_ping_new_session failed") >= 1 && fake::reboots == 0,
          "a failed session is logged and retried, not fatal");
    fake::fail_new_session = false;
    s.run(15000, Ping::REPLY);
    check(fake::session_live() && s.wd.armed_, "and recovers once sessions can be created");
  }
  {
    Sim s;
    s.wd.set_target_str("10.2.3.1");
    s.arm();
    check(fake::target_v4() == (10u | 2u << 8 | 3u << 16 | 1u << 24), "a static target overrides DHCP");
  }
  {
    Sim s;
    s.wd.set_target_str("not-an-ip");
    s.run(ARM + 60000, Ping::REPLY);
    check(fake::sessions_created == 0 && fake::reboots == 0, "an unparseable static target does nothing rather than guess");
  }

  std::printf("\n== sensors ==\n");
  {
    Sim s;
    s.wd.update();
    check(std::isnan(s.loss.state) && std::isnan(s.rtt.state), "no samples yet publishes unknown, not 0% loss");
    s.arm();
    s.wd.update();  // resets the counters
    fake::next_rtt_ms = 7;
    s.run(15000, Ping::REPLY);   // 3 replies
    s.run(5000, Ping::TIMEOUT);  // 1 timeout
    s.wd.update();
    check(std::fabs(s.loss.state - 25.0f) < 0.01f, "packet loss is timeouts / total for the period");
    check(std::fabs(s.rtt.state - 7.0f) < 0.01f, "RTT is the mean over replies only");
    s.wd.update();
    check(std::isnan(s.loss.state), "counters reset each period");
  }

  std::printf("\n== each outage gets its own rebuild ==\n");
  {
    // The rebuild flag used to outlive the outage it was spent on, so after
    // one rebuild-and-recover a separate outage days later rebooted after a
    // single window, with no rebuild.
    Sim s;
    s.arm();
    s.run(WINDOW + 10000, Ping::TIMEOUT);    // incident 1: rebuild...
    s.run(15000, Ping::REPLY);               // ...and recovery
    s.run(3 * 24 * 3600000u, Ping::REPLY);   // three healthy days
    s.run(WINDOW + 10000, Ping::TIMEOUT);    // incident 2
    check(fake::reboots == 0 && fake::count_logs("rebuilding session before considering a reboot") == 2,
          "a new outage long after a recovered one gets its own rebuild before any reboot");
    s.run(15000, Ping::REPLY);
    s.run(WINDOW + 10000, Ping::TIMEOUT);
    check(fake::reboots == 1, "and still reboots if the rebuilt session loses the gateway too");
  }
  {
    // Recovery shorter than a window is the same outage: no second rebuild.
    Sim s;
    s.arm();
    s.run(WINDOW + 10000, Ping::TIMEOUT);
    s.run(WINDOW - 60000, Ping::REPLY);
    s.run(WINDOW + 10000, Ping::TIMEOUT);
    check(fake::reboots == 1 && fake::count_logs("rebuilding session before considering a reboot") == 1,
          "a flapping gateway that never stays up a full window still reboots");
  }

  std::printf("\n== IPv4 sessions are unchanged by IPv6 support ==\n");
  {
    Sim s;
    s.arm();
    check(fake::target_v4() == GW && fake::session_interface == 0,
          "an IPv4 gateway session is left unbound, exactly as before");
  }

#if LWIP_IPV6
  std::printf("\n== IPv6 ==\n");
  {
    // The IPv6-only case: no DHCPv4 lease, one router learned from RAs.
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::1", 1, 1800);  // netif 1 = the default netif (index 2)
    s.arm();
    check(fake::target_is("fe80::1"), "with no IPv4 gateway the RA default router is watched");
    // The bug this guards against: esp_ping drops the zone of a link-local
    // target, and unbound, lwIP has no route for it on a multi-netif node, so
    // every echo request failed to send and a healthy router read as 100% loss.
    check(fake::session_interface == 2, "and the session is bound to the router's interface");
    check(fake::lwip_locks_taken > 0, "the ND6 router table is read under the lwIP core lock");
    check(s.wd.armed_, "a replying router arms the watchdog");
  }
  {
    Sim s;
    fake::set_router(0, "fe80::1", 1, 1800);  // dual-stack: an IPv4 gateway is present too
    s.arm();
    check(fake::target_v4() == GW, "an IPv4 gateway still wins on a dual-stack network");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::1", 1, 1800);
    s.arm();
    s.lose_rebuild_recover_lose();
    check(fake::reboots == 1, "the full rebuild-then-reboot path works over IPv6");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::1", 1, 1800);
    s.run(20 * 60000, Ping::TIMEOUT);
    check(!s.wd.armed_ && fake::reboots == 0, "a router never reached even once never reboots");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::1", 1, 0);  // router lifetime expired
    s.run(ARM + 20000, Ping::REPLY);
    check(fake::sessions_created == 0, "an expired router (lifetime 0) is not a target");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::aa", 2, 1800);  // learned on another netif (e.g. the softAP)
    fake::set_router(1, "fe80::1", 1, 1800);   // learned on the default netif
    s.arm();
    check(fake::target_is("fe80::1") && fake::session_interface == 2,
          "a router on the default interface is preferred over one on another interface");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::aa", 2, 1800);  // the only router is on another netif
    s.arm();
    check(fake::target_is("fe80::aa") && fake::session_interface == 3,
          "failing that, any live router - bound to its own interface");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(1, "fe80::1", 1, 1800);
    s.arm();
    const int before = fake::sessions_created;
    fake::set_router(0, "fe80::2", 1, 1800);  // a second router, in an earlier slot
    s.run(30000, Ping::REPLY);
    check(fake::sessions_created == before && fake::target_is("fe80::1"),
          "a second router appearing does not tear down a healthy session");
  }
  {
    Sim s;
    fake::gateway = 0;
    fake::set_router(0, "fe80::1", 1, 1800);
    s.arm();
    fake::clear_routers();
    fake::set_router(1, "fe80::2", 1, 1800);  // the old router is gone, a new one took over
    s.run(10000, Ping::REPLY);
    check(fake::target_is("fe80::2"), "follows the default router when it changes");
  }
  {
    Sim s;
    fake::gateway = 0;
    s.wd.set_target_str("fe80::1");
    s.arm();
    check(fake::target_is("fe80::1") && fake::session_interface == 2,
          "a static link-local target is bound to the default interface");
  }
  {
    Sim s;
    s.wd.set_target_str("2001:db8::1");
    s.arm();
    check(fake::target_is("2001:db8::1") && fake::session_interface == 2,
          "a static global IPv6 target overrides the IPv4 gateway");
  }
  {
    // Config validation rejects a %zone suffix, but should one arrive anyway,
    // lwIP drops a name it does not know and the session still goes out of
    // the default interface rather than nowhere.
    Sim s;
    fake::gateway = 0;
    s.wd.set_target_str("fe80::1%wlan0");
    s.arm();
    check(fake::target_is("fe80::1") && fake::session_interface == 2,
          "a zone suffix lwIP cannot match still pings out of the default interface");
  }
#endif

  std::printf("\n%s", failures ? "FAILED" : "all passed");
  std::printf("\n");
  return failures ? 1 : 0;
}
