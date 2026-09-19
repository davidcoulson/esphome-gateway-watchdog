#pragma once
namespace esphome { namespace sensor {
class Sensor {
 public:
  void publish_state(float v) { state = v; publishes++; }
  float state{-1.0f};
  int publishes{0};
};
} }  // namespace esphome::sensor
