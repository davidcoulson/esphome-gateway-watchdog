// Shared state behind the stub headers. Tests drive the clock, the network,
// the ping session and NVS through this, and read back logs and reboots.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fake {
struct LogLine { char level; std::string text; };

extern uint32_t now_ms;                                  // millis()
extern bool connected;                                   // network::is_connected()
extern uint32_t gateway;                                 // default route's gw, 0 = none
extern bool have_default_netif;
extern std::vector<LogLine> logs;
extern int reboots;                                      // App.safe_reboot() calls
extern std::map<uint32_t, std::vector<uint8_t>> nvs;     // survives a simulated reboot
extern int nvs_syncs;

// esp_ping
extern bool fail_new_session, fail_start;
extern int sessions_created, sessions_deleted;
extern uint32_t session_target;                          // target of the live session
extern uint32_t next_rtt_ms;
void ping_reply();                                       // fire the live session's success cb
void ping_timeout();                                     // fire its timeout cb
bool session_live();

void reset_all(bool keep_nvs);
int count_logs(const char *needle);
}  // namespace fake
