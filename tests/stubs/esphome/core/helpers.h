#pragma once
#include <cstdint>
namespace fake { extern int lwip_locks_taken; }
namespace esphome {
inline uint32_t fnv1_hash(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) { h *= 16777619u; h ^= (uint8_t) *s; }
  return h;
}
// RAII for the lwIP core lock. Counts acquisitions, so a test can show the
// ND6 router table is read under it.
class LwIPLock {
 public:
  LwIPLock() { fake::lwip_locks_taken++; }
  ~LwIPLock() = default;
  LwIPLock(const LwIPLock &) = delete;
  LwIPLock &operator=(const LwIPLock &) = delete;
};
}  // namespace esphome
#define YESNO(b) ((b) ? "YES" : "NO")
