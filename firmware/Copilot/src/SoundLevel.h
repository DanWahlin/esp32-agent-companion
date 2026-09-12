#pragma once
#include <cstdint>

namespace copilot {
enum class SoundLevel : uint8_t {
  Off = 0,
  Quiet = 1,
  Normal = 2,
};

inline bool isValidSoundLevel(uint8_t value) {
  return value <= static_cast<uint8_t>(SoundLevel::Normal);
}
}
