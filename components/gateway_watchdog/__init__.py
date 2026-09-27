"""Gateway reachability watchdog for ESPHome.

Works on both WiFi and Ethernet nodes: it depends on ``network`` rather
than ``wifi``, resolves the gateway from whichever netif is currently the
default route, and uses ``network::is_connected()`` for link state.

See README.md for the rationale; the short version is that ESPHome's
``wifi: reboot_timeout:`` is an *association* watchdog, not a
*reachability* one, so a node that stays associated to its AP while its
VLAN/gateway/uplink is dead will never recover on its own.
"""

from ipaddress import IPv6Address

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ENABLE_IPV6, CONF_ID
import esphome.final_validate as fv

CODEOWNERS = ["@davidcoulson"]
DEPENDENCIES = ["network"]
MULTI_CONF = False

gateway_watchdog_ns = cg.esphome_ns.namespace("gateway_watchdog")
GatewayWatchdog = gateway_watchdog_ns.class_("GatewayWatchdog", cg.PollingComponent)

CONF_GATEWAY_WATCHDOG_ID = "gateway_watchdog_id"
CONF_TARGET = "target"
CONF_REBOOT_WINDOW = "reboot_window"
CONF_PING_INTERVAL = "ping_interval"
CONF_PING_TIMEOUT = "ping_timeout"
CONF_REBOOT = "reboot"
CONF_MAX_REBOOTS = "max_reboots"
CONF_BUDGET_RESET_AFTER = "budget_reset_after"
CONF_ARM_DELAY = "arm_delay"


def _validate(config):
    """Reject combinations that would make the watchdog misbehave."""
    timeout_ms = config[CONF_PING_TIMEOUT].total_milliseconds
    interval_ms = config[CONF_PING_INTERVAL].total_milliseconds
    window_ms = config[CONF_REBOOT_WINDOW].total_milliseconds

    if timeout_ms >= interval_ms:
        raise cv.Invalid(
            f"{CONF_PING_TIMEOUT} ({timeout_ms}ms) must be shorter than "
            f"{CONF_PING_INTERVAL} ({interval_ms}ms), otherwise requests overlap "
            f"and a merely slow gateway looks like a dead one."
        )
    # Demand real evidence before a reboot: at least three echo requests
    # must have had the chance to fail inside the window.
    if window_ms < interval_ms * 3:
        raise cv.Invalid(
            f"{CONF_REBOOT_WINDOW} ({window_ms}ms) must be at least 3x "
            f"{CONF_PING_INTERVAL} ({interval_ms}ms) so a reboot is never "
            f"triggered by one or two dropped echo requests."
        )
    return config


def _target(value):
    """An IPv4 address, or an IPv6 literal without a %zone suffix.

    One validator rather than cv.Any(ipv4, ipv6): cv.Any reports the first
    alternative's error, so every IPv6 mistake came back as "not a valid IPv4
    address".
    """
    value = cv.string_strict(value)
    if ":" not in value:
        return cv.ipv4address(value)
    return _ipv6_target(value)


def _ipv6_target(value):
    """An IPv6 literal without a %zone suffix.

    lwIP's ipaddr_aton() does accept "fe80::1%name", but it resolves the name
    with netif_find() against lwIP's own interface names - ESP-IDF's "st1",
    "en1", "ap2" - not the "wlan0"/"eth0" anyone would write. So a suffix
    either silently does nothing or binds the target to an interface nobody
    meant. It is never needed: IPv6 sessions are always pinged out of the
    default interface, which is what a link-local target has to be on.
    """
    address = cv.ipv6address(value)
    if address.scope_id:
        raise cv.Invalid(
            f"{CONF_TARGET}: drop the %{address.scope_id} zone suffix - IPv6 "
            f"targets are always pinged out of the default interface"
        )
    return address


def _final_validate(config):
    """An IPv6 target needs lwIP built with IPv6.

    Without network: enable_ipv6: true, ESPHome builds lwIP IPv4-only, where
    ipaddr_aton() is ip4addr_aton(): the IPv6 literal fails to parse at
    runtime and the watchdog never starts a session - no error, no pings.
    """
    target = config.get(CONF_TARGET)
    if isinstance(target, IPv6Address):
        network = fv.full_config.get().get("network", {})
        if not network.get(CONF_ENABLE_IPV6, False):
            raise cv.Invalid(
                f"{CONF_TARGET} {target} is an IPv6 address, which needs "
                f"'network: {CONF_ENABLE_IPV6}: true'"
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GatewayWatchdog),
            # Omit to track the default gateway (the DHCPv4 one, or on an
            # IPv6-only network the RA default router), which is what makes
            # one include work unmodified across every VLAN.
            cv.Optional(CONF_TARGET): _target,
            cv.Optional(
                CONF_REBOOT_WINDOW, default="300s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_PING_INTERVAL, default="5s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_PING_TIMEOUT, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_REBOOT, default=True): cv.boolean,
            # Cap on reboots - the backstop that makes a reboot loop
            # impossible even if every other safeguard misjudges the cause.
            #
            #   N > 0  cap at N, then stay up and report (recommended)
            #   0      UNLIMITED - keep rebooting until the gateway returns
            #
            # Use reboot: false to disable rebooting entirely. Unlimited is
            # the right choice when an unreachable node is useless anyway and
            # you would rather it kept trying; the cost is that a wrong
            # diagnosis becomes an unbounded loop, which is exactly what took
            # out ~40 nodes on 2026-09-12.
            cv.Optional(CONF_MAX_REBOOTS, default=2): cv.int_range(min=0, max=100),
            cv.Optional(
                CONF_BUDGET_RESET_AFTER, default="1h"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_ARM_DELAY, default="60s"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.polling_component_schema("60s")),
    _validate,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_TARGET in config:
        cg.add(var.set_target_str(str(config[CONF_TARGET])))
    cg.add(var.set_reboot_window(config[CONF_REBOOT_WINDOW]))
    cg.add(var.set_ping_interval(config[CONF_PING_INTERVAL]))
    cg.add(var.set_ping_timeout(config[CONF_PING_TIMEOUT]))
    cg.add(var.set_reboot_enabled(config[CONF_REBOOT]))
    cg.add(var.set_max_reboots(config[CONF_MAX_REBOOTS]))
    cg.add(var.set_budget_reset_after(config[CONF_BUDGET_RESET_AFTER]))
    cg.add(var.set_arm_delay(config[CONF_ARM_DELAY]))
