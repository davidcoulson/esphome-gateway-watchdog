#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "../../fake.h"
namespace esphome {
class ESPPreferenceObject {
 public:
  ESPPreferenceObject() = default;
  explicit ESPPreferenceObject(uint32_t key) : key_(key), valid_(true) {}
  template<typename T> bool save(const T *src) {
    if (!valid_) return false;
    auto &b = fake::nvs[key_];
    b.assign(reinterpret_cast<const uint8_t *>(src), reinterpret_cast<const uint8_t *>(src) + sizeof(T));
    return true;
  }
  template<typename T> bool load(T *dst) {
    if (!valid_) return false;
    auto it = fake::nvs.find(key_);
    if (it == fake::nvs.end() || it->second.size() != sizeof(T)) return false;
    std::memcpy(dst, it->second.data(), sizeof(T));
    return true;
  }
 private:
  uint32_t key_{0};
  bool valid_{false};
};
class ESPPreferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t key) { return ESPPreferenceObject(key); }
  bool sync() { fake::nvs_syncs++; return true; }
};
extern ESPPreferences *global_preferences;
}  // namespace esphome
