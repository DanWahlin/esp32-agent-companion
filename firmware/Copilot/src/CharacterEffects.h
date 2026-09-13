#pragma once
#include "CharacterMotion.h"
#include "Config.h"
#include <cstddef>

namespace copilot {
class CharacterEffects {
 public:
  static constexpr size_t kDamageBudget = 4096;
  CharacterEffects(uint16_t* firstOutput, uint16_t* secondOutput);
  // Restore before SpriteRenderer::render; overlay only after its successful render.
  bool restore(uint16_t* frame);
  bool render(const CharacterState& state, uint16_t* frame);
  const char* error() const { return error_; }

 private:
  int buffer(uint16_t* frame);
  void pixel(int x, int y, uint16_t color);
  void dot(int x, int y, int radius, uint16_t color);
  void spark(int x, int y, int radius, uint16_t color);
  void glyph(const uint8_t* rows, int rowCount, int x, int y, int scale, uint16_t color);
  void workingBits(double seconds);
  void sleepingZs(double seconds);
  uint16_t* outputs_[2];
  uint32_t damage_[2][kDamageBudget] = {};
  size_t counts_[2] = {};
  int active_ = 0;
  const char* error_ = nullptr;
};
}
