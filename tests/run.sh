#!/usr/bin/env bash
# Host tests: the real gateway_watchdog.cpp compiled against tests/stubs/.
# Needs only a C++17 compiler. Run from anywhere.
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d)/test_gateway_watchdog
# -Wno-unused-parameter: the esp_ping callbacks must match ESP-IDF's
# signature, so ping_timeout_cb has an unused `hdl`. ESP-IDF builds do not
# use -Wextra; everything else stays an error.
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
  -DUSE_ESP32 -DUSE_SENSOR \
  -Itests/stubs \
  components/gateway_watchdog/gateway_watchdog.cpp \
  tests/stubs/fake.cpp \
  tests/test_gateway_watchdog.cpp \
  -o "$out"
"$out"
