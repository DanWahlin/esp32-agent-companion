#pragma once

#include "SpriteRenderer.h"
#include <cstdint>

namespace copilot {
class OpenClawSpriteRenderer {
 public:
  OpenClawSpriteRenderer(uint16_t* scratch, uint16_t* cached, InflateSprite inflate)
      : scratch_(scratch), cached_(cached), inflate_(inflate) {}
  bool render(const SpritePose& pose, float effectSeconds, uint16_t* frame);
  void invalidate() { cacheKey_ = UINT32_MAX; }
  const char* error() const { return error_; }

 private:
  bool decode(unsigned blockIndex, uint16_t* output);
  void prefetch(unsigned direction, unsigned index, unsigned blinkLevel);
  uint16_t* scratch_;
  uint16_t* cached_;
  InflateSprite inflate_;
  uint32_t cacheKey_ = UINT32_MAX;
  const char* error_ = nullptr;
};
}
