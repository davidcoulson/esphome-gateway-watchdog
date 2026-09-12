"""Gateway reachability watchdog for ESPHome.

See README.md for the rationale; the short version is that ESPHome's
``wifi: reboot_timeout:`` is an *association* watchdog, not a
*reachability* one, so a node that stays associated to its AP while its
VLAN/gateway/uplink is dead will never recover on its own.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@davidcoulson"]
DEPENDENCIES = ["wifi"]
MULTI_CONF = False

gateway_watchdog_ns = cg.esphome_ns.namespace("gateway_watchdog")
GatewayWatchdog = gateway_watchdog_ns.class_("GatewayWatchdog", cg.PollingComponent)

CONF_GATEWAY_WATCHDOG_ID = "gateway_watchdog_id"
CONF_TARGET = "target"
CONF_REBOOT_WINDOW = "reboot_window"
CONF_PING_INTERVAL = "ping_interval"
CONF_PING_TIMEOUT = "ping_timeout"
CONF_REBOOT = "reboot"


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


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GatewayWatchdog),
            # Omit to track the DHCP-supplied default gateway, which is
            # what makes one include work unmodified across every VLAN.
            cv.Optional(CONF_TARGET): cv.ipv4address,
            cv.Optional(
                CONF_REBOOT_WINDOW, default="120s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_PING_INTERVAL, default="5s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_PING_TIMEOUT, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_REBOOT, default=True): cv.boolean,
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
