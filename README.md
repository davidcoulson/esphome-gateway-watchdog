# esphome-gateway-watchdog

An ESPHome external component that reboots a node when it can still see its
access point but can no longer reach anything — plus diagnostic packet-loss
and round-trip-time sensors.

## Why this exists

ESPHome already ships two reboot timers, and neither one covers this case.

`wifi:` → `reboot_timeout:` is an **association** watchdog, not a reachability
one. In `wifi_component.cpp` the check is:

```cpp
if (!this->has_ap() && this->reboot_timeout_ != 0) {
  if (now - this->last_connected_ > this->reboot_timeout_) {   // reboot
```

and `last_connected_` is refreshed on *every loop iteration* for as long as the
state machine sits in `STA_CONNECTED`. So a node that is associated to its AP
and holding a DHCP lease keeps resetting that timer forever — even if its VLAN
is misconfigured, its gateway is dead, or its uplink is black-holing traffic.
It only fires when the radio cannot associate or cannot get an IP at all.

`api:` → `reboot_timeout:` *does* catch unreachability, because "no API client
connected for N minutes" is a real end-to-end test. But it also fires during any
long Home Assistant outage — a core upgrade, a recorder migration, a Supervisor
restart that runs long — which reboots your entire fleet at once over a planned
maintenance window. Turning it off closes that problem and reopens the first one.

This component fills the gap. It watches the **default gateway**, which stays up
across a Home Assistant restart, so it distinguishes "my network is broken" from
"Home Assistant is busy".

## Install

```yaml
external_components:
  - source: github://davidcoulson/esphome-gateway-watchdog
    components: [gateway_watchdog]

gateway_watchdog:
  id: gw_wd
```

That's the whole minimum config. With no `target:`, it follows the
DHCP-supplied default gateway, so the same block works unmodified on every
VLAN and re-targets itself if the lease changes.

### Pin `refresh: always` while iterating

ESPHome caches an `external_components` clone for a day by default, **and each
build environment keeps its own cache**. If you build from more than one place
— a local add-on and a remote/CI builder, say — they can be serving different
versions of this component at the same time, with no warning beyond a quiet
`Skipping update for ...` line in the log.

That bites hardest right after an upgrade. A stale clone predating Ethernet
support will fail validation with:

```
Component gateway_watchdog requires component wifi.
```

on an Ethernet node, even though the current component has no such dependency.
Either add `refresh: always`:

```yaml
external_components:
  - source: github://davidcoulson/esphome-gateway-watchdog
    components: [gateway_watchdog]
    refresh: always
```

or clear the cache for that build host (`.esphome/external_components/`).

## Configuration

```yaml
gateway_watchdog:
  id: gw_wd
  target: 10.2.4.1        # optional; default = DHCP default gateway
  ping_interval: 5s       # how often an echo request goes out
  ping_timeout: 2s        # per-request timeout; must be < ping_interval
  reboot_window: 120s     # no reply anywhere in this window => reboot
  reboot: true            # false = report only, never reboot
  update_interval: 60s    # how often the sensors publish

sensor:
  - platform: gateway_watchdog
    gateway_watchdog_id: gw_wd
    packet_loss:
      name: "Gateway Packet Loss"
    round_trip_time:
      name: "Gateway RTT"
```

Both sensors default to **`internal: true`** — they are meant for the local web
UI and on-device debugging, not for adding two more entities per node to Home
Assistant. Set `internal: false` on either to surface it.

`packet_loss` publishes `NaN` rather than `0` when no echo request completed at
all in the window (session not up yet, WiFi down), so "no data" cannot be
misread as "healthy". `round_trip_time` publishes `NaN` when there were no
replies to average.

### Validation

The config is rejected at build time if:

- `ping_timeout >= ping_interval` — overlapping requests make a merely slow
  gateway look like a dead one.
- `reboot_window < ping_interval * 3` — at least three echo requests must have
  had the chance to fail before a reboot is on the table.

## Safety properties

This component can power-cycle whatever the node is wired into, so each of
these is deliberate:

0. **Distinguishes a dead session from an unreachable gateway.** A live
   `esp_ping` session emits a callback every interval — a reply *or* a
   timeout. Timeouts arriving means the gateway is not answering, which is
   real signal. Total silence means the session itself has stopped (socket
   error, netif rebuilt by a reconnect), which is not the network's fault, so
   the session is rebuilt rather than the node rebooted. Without this, a dead
   session freezes the "last reply" timestamp and guarantees a reboot every
   window, forever.
0b. **Rebuilds once before ever rebooting.** Even when the session looks
   alive, the first expired window rebuilds it and starts a fresh window
   instead of rebooting. Only a second full window — on a session created
   after the node had reached the gateway again — actually reboots.
1. **Never reboots on a target it has never once reached.** A node that has
   never seen its gateway — wrong VLAN, bad credentials, a config flashed to the
   wrong device — logs and stays up rather than entering a reboot loop.
2. **Never reboots while WiFi is disassociated.** That case belongs to
   `wifi.reboot_timeout`; overlapping the two would only make the reboot reason
   ambiguous.
3. **Stall credit.** If the main loop is starved for more than
   `ping_interval * 4` — an OTA write, a long blocking operation — the gap is
   credited rather than counted against the gateway. Without this, the first
   loop after an OTA would see a `> reboot_window` gap and reboot a healthy node.
4. **Re-arms from scratch if the gateway address changes.**

The sliding window is implemented as time-since-last-success rather than a ring
buffer of samples: same semantics ("no success anywhere in the last window"),
one `uint32_t` of state.

## Cost

Measured on an ESP32-C3 (ESP-IDF 6.1.0), same node with and without:

| | Flash | RAM |
|---|---|---|
| without | 964,440 B | 100,414 B |
| with | 968,978 B | 100,574 B |
| **delta** | **+4,538 B** | **+160 B** |

## Requirements

ESP32 family on the ESP-IDF framework. Uses ESP-IDF's `esp_ping`
(`lwip/apps/ping/ping_sock.h`) and `esp_netif`. Not supported on ESP8266 or
under the Arduino framework.

Works on **WiFi and Ethernet** nodes. It depends on `network` rather than
`wifi`, resolves the gateway from `esp_netif_get_default_netif()` (whichever
interface currently holds the default route) rather than a hardcoded
`WIFI_STA_DEF` key, and uses `network::is_connected()` for link state.

Ethernet nodes are the strongest case for it: they have no `wifi` component,
so `wifi.reboot_timeout` does not exist for them and they otherwise have no
link-layer watchdog at all.

## Attributing the reboot

You do not need to configure anything for this. ESPHome's `debug` component
already records which component asked for a reboot, and this one is picked up
automatically:

`App.safe_reboot()` runs the shutdown hooks without reassigning
`current_component_`, so it still points at this component (the scheduler set
it before dispatching our `loop()`). `DebugComponent::on_shutdown()` persists
that source to NVS, and on the next boot an `ESP_RST_SW` reset with a loadable
preference is reported as `Reboot request from <source>`.

So with a `debug` reset-reason sensor configured:

```yaml
text_sensor:
  - platform: debug
    reset_reason:
      name: "Reset Reason"
```

a watchdog reboot reads:

```
Reset Reason: Reboot request from gateway_watchdog
```

which distinguishes it from `Reboot request from esphome.ota`, a Restart
button, `api.reboot_timeout`, a power cycle or a panic.

## Verified

Tested on ESP32-C3 hardware against ESPHome 2026.8.2 / ESP-IDF 6.1.0, and
built for ESP32 Ethernet (W5500 / LAN8720) nodes.

**Normal operation** — auto-discovered the DHCP gateway, reported 0.0% loss at
4–16 ms RTT.

**No reboot loop on an unreachable target** — with `target: 192.0.2.1`
(RFC 5737 TEST-NET-1, unroutable) and `reboot: true`, reported 100.0% loss and
`NaN` RTT and **stayed up** across many multiples of `reboot_window`,
confirming safety property 1.

**The reboot path** — pointed one node at a second node, let it arm, then
rebooted the target to create a real outage:

```
[15:01:29][S][sensor]: 'GW Packet Loss' >> 22.2 %
[15:01:30][E][gateway_watchdog:160]: gateway unreachable 5008 ms - rebooting
[15:01:30][I][app:271]: Rebooting safely
[15:01:30][D][debug:058]: Storing reboot source: gateway_watchdog
```

and after the reboot the device reported:

```
Reset Reason: Reboot request from gateway_watchdog
```

## License

MIT
