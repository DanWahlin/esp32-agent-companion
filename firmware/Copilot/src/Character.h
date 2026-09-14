#pragma once

#include <cstdint>

namespace copilot {
enum class CharacterId : uint8_t {
  Copilot = 0,
  OpenClaw = 1,
};

constexpr double kOpenClawMotionSpeed = 1.3;
constexpr double kOpenClawWalkFps = 12.0;

inline const char* characterName(CharacterId character) {
  return character == CharacterId::OpenClaw ? "openclaw" : "copilot";
}
}
