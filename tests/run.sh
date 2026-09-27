#!/usr/bin/env bash
# Host tests: the real gateway_watchdog.cpp compiled against tests/stubs/.
# Needs only a C++17 compiler. Run from anywhere.
#
# Builds and runs twice: against an IPv4-only lwIP (network: enable_ipv6 off,
# which is what most nodes build) and a dual-stack one (enable_ipv6: true).
# The IPv6 code only exists in the second, so a suite that ran once would
# never compile it.
set -euo pipefail
cd "$(dirname "$0")/.."
dir=$(mktemp -d)

# A node with no sensor anywhere in its config builds without USE_SENSOR, and
# the suite below needs it (it wires up the sensors). Compile the component on
# its own without it, so sensor code outside #ifdef USE_SENSOR fails here
# rather than on the first sensor-less node - which is how the budget reset's
# publish_state() once broke the README's own minimum config.
for ipv6 in 0 1; do
  ${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
    -DUSE_ESP32 -DSTUB_IPV6="$ipv6" -Itests/stubs -fsyntax-only \
    components/gateway_watchdog/gateway_watchdog.cpp
done
echo "compiles without USE_SENSOR (IPv4-only and dual-stack lwIP)"
echo

for ipv6 in 0 1; do
  label=$([ "$ipv6" = 1 ] && echo "dual-stack lwIP" || echo "IPv4-only lwIP")
  echo "######## $label"
  # -Wno-unused-parameter: the esp_ping callbacks must match ESP-IDF's
  # signature, so ping_timeout_cb has an unused `hdl`. ESP-IDF builds do not
  # use -Wextra; everything else stays an error.
  ${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
    -DUSE_ESP32 -DUSE_SENSOR -DSTUB_IPV6="$ipv6" \
    -Itests/stubs \
    components/gateway_watchdog/gateway_watchdog.cpp \
    tests/stubs/fake.cpp \
    tests/test_gateway_watchdog.cpp \
    -o "$dir/test_$ipv6"
  "$dir/test_$ipv6"
  echo
done
