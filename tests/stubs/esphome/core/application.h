#pragma once
#include "../../fake.h"
namespace esphome {
class Application {
 public:
  // The real one never returns. Here it records the reboot and returns, so a
  // test can assert on it; the watchdog's loop() returns straight after anyway.
  void safe_reboot() { fake::reboots++; }
};
extern Application App;
}  // namespace esphome
