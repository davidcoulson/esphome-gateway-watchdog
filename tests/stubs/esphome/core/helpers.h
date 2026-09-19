#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t fnv1_hash(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) { h *= 16777619u; h ^= (uint8_t) *s; }
  return h;
}
}  // namespace esphome
#define YESNO(b) ((b) ? "YES" : "NO")
