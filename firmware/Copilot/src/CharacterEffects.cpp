#include "CharacterEffects.h"
#include "../generated/sprite_assets.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace copilot {
namespace {
uint16_t color(int r, int g, int b, double brightness = 1) {
  const uint16_t rgb = (static_cast<unsigned>(r * brightness) >> 3) << 11
      | (static_cast<unsigned>(g * brightness) >> 2) << 5
      | (static_cast<unsigned>(b * brightness) >> 3);
  return static_cast<uint16_t>((rgb << 8) | (rgb >> 8));
}
uint32_t hash(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}
}

CharacterEffects::CharacterEffects(uint16_t* firstOutput, uint16_t* secondOutput)
    : outputs_{firstOutput, secondOutput} {}

int CharacterEffects::buffer(uint16_t* frame) {
  if (!outputs_[0] || !outputs_[1] || outputs_[0] == outputs_[1]) {
    error_ = "Effects require two distinct persistent output buffers.";
    return -1;
  }
  if (frame == outputs_[0]) return 0;
  if (frame == outputs_[1]) return 1;
  error_ = "Effect output buffer is not registered.";
  return -1;
}

bool CharacterEffects::restore(uint16_t* frame) {
  error_ = nullptr;
  const int index = buffer(frame);
  if (index < 0) return false;
  for (size_t i = 0; i < counts_[index]; ++i) frame[damage_[index][i]] = 0;
  counts_[index] = 0;
  return true;
}

void CharacterEffects::pixel(int x, int y, uint16_t rgb) {
  x += kCharacterArtX;
  y += kFrameY;
  if (x < 0 || y < 0 || x >= kCharacterFrameWidth || y >= kCharacterFrameHeight || !rgb) return;
  // Protect black facial interiors as well as every non-black artwork pixel.
  const int dx = x - kCharacterFrameWidth / 2, dy = y - kDisplaySize / 2;
  if (int64_t(dx) * dx * 135 * 135 + int64_t(dy) * dy * 160 * 160
      < int64_t(160) * 160 * 135 * 135) return;
  const uint32_t index = y * kCharacterFrameWidth + x;
  if (outputs_[active_][index]) return;
  if (counts_[active_] == kDamageBudget) {
    error_ = "Effect damage budget exceeded.";
    return;
  }
  damage_[active_][counts_[active_]++] = index;
  outputs_[active_][index] = rgb;
}

void CharacterEffects::dot(int x, int y, int radius, uint16_t rgb) {
  for (int dy = -radius; dy <= radius; ++dy)
    for (int dx = -radius; dx <= radius; ++dx)
      if (dx * dx + dy * dy <= radius * radius) pixel(x + dx, y + dy, rgb);
}

void CharacterEffects::spark(int x, int y, int radius, uint16_t rgb) {
  for (int i = -radius; i <= radius; ++i) {
    pixel(x + i, y, rgb);
    pixel(x, y + i, rgb);
  }
}

void CharacterEffects::workingBits(double seconds) {
  constexpr uint8_t glyphs[2][9] = {
    {14, 17, 17, 17, 17, 17, 17, 17, 14},
    {4, 12, 4, 4, 4, 4, 4, 4, 14},
  };
  constexpr int lanes[] = {-24, -12, 10, 22};
  constexpr int top = -kFrameY - 9, bottom = 34;
  // Recycle beyond the physical top edge; fade only where digits meet the head.
  const double cycle = seconds / 4;
  for (int lane = 0; lane < 4; ++lane) {
    for (int slot = 0; slot < 2; ++slot) {
      double phase = cycle + lane * .125 + slot * .5;
      phase -= std::floor(phase);
      const double ramp = std::min(1.0, (lane < 2 ? 1 - phase : phase) / .15);
      const uint16_t rgb = color(124, 222, 242, .85 * ramp * ramp * (3 - 2 * ramp));
      if (!rgb) continue;
      const int travel = static_cast<int>(std::lround((bottom - top) * phase));
      const int y = lane < 2 ? top + travel : bottom - travel;
      const int x = kFrameWidth / 2 + lanes[lane];
      const uint8_t* glyph = glyphs[(lane + slot) % 2];
      for (int row = 0; row < 9; ++row)
        for (int column = 0; column < 5; ++column)
          if (glyph[row] & (1 << (4 - column))) pixel(x + column, y + row, rgb);
    }
  }
}

bool CharacterEffects::render(const CharacterState& state, uint16_t* frame) {
  error_ = nullptr;
  const int index = buffer(frame);
  if (index < 0) return false;
  if (counts_[index]) {
    error_ = "Restore previous effects before rendering this output buffer.";
    return false;
  }
  if (static_cast<uint8_t>(state.mode) > 4 || static_cast<uint8_t>(state.requestedMode) > 4
      || !std::isfinite(state.effectSeconds) || state.effectSeconds < 0 || state.effectSeconds > 12
      || state.pose.direction >= kSpriteDirections || state.pose.index > 23 || state.pose.blinkLevel > 4) {
    error_ = "Invalid character effect state.";
    return false;
  }
  active_ = index;
  const double t = state.effectSeconds;
  constexpr double pi = 3.14159265358979323846;
  if (state.mode == CharacterMode::Working) {
    constexpr int orbitRadius = 200;
    double x = std::cos(t * pi / 6), y = std::sin(t * pi / 6);
    constexpr double cosine = .9872272833756269, sine = .15931820661424598;
    for (int i = 0; i < 7; ++i) {
      dot(kFrameWidth / 2 + int(std::lround(orbitRadius * x)),
          kFrameHeight / 2 + int(std::lround(orbitRadius * y)), i == 6 ? 5 : 2,
          color(80, 215, 239, .30 + i * .108));
      const double nextX = x * cosine - y * sine;
      y = x * sine + y * cosine;
      x = nextX;
    }
    workingBits(t);
  } else if (state.mode == CharacterMode::Attention) {
    const double breath = .78 + .17 * std::sin(t * pi / 2);
    constexpr uint8_t question[] = {14, 17, 1, 2, 4, 0, 4};
    const uint16_t amber = color(255, 192, 86, breath);
    double ringX = 1, ringY = 0;
    for (int i = 0; i < 32; ++i) {
      dot(348 + int(std::lround(23 * ringX)), 56 + int(std::lround(23 * ringY)), 1,
          color(255, 192, 86, breath * .3));
      const double nextX = ringX * .9807852804032304 - ringY * .19509032201612825;
      ringY = ringX * .19509032201612825 + ringY * .9807852804032304;
      ringX = nextX;
    }
    for (int y = 0; y < 7; ++y)
      for (int x = 0; x < 5; ++x)
        if (question[y] & (1 << (4 - x)))
          dot(340 + x * 4, 44 + y * 4, 2, amber);
    dot(35, 70, 2, color(255, 192, 86, breath * .45));
  } else if (state.mode == CharacterMode::Complete && t < 3) {
    constexpr double xs[] = {1, .809017, .309017, -.309017, -.809017, -1, -.809017, -.309017, .309017, .809017};
    constexpr double ys[] = {0, .587785, .951057, .951057, .587785, 0, -.587785, -.951057, -.951057, -.587785};
    for (int side : {-1, 1}) {
      const double launch = (t - .3) / .5;
      if (launch >= 0 && launch < 1) {
        const int x = 200 + side * (160 - int(20 * launch));
        const int y = 278 - int(198 * (2 * launch - launch * launch));
        dot(x, y, 2, color(255, 222, 154));
        for (int tail = 3; tail < 11; ++tail)
          pixel(x, y + tail, color(190, 161, 247, (11 - tail) / 12.0));
      }
      const double age = t - .8;
      if (age >= 0 && age < 1.5) {
        const double radius = 80 * age / (1 + age);
        const double brightness = 1 - age / 1.5;
        const int centerX = 200 + side * 140, centerY = 80 + int(20 * age * age);
        for (int ray = 0; ray < 10; ++ray) {
          const int x = centerX + int(radius * xs[ray]), y = centerY + int(radius * ys[ray]);
          const uint16_t rgb = ray % 2 ? color(121, 229, 209, brightness)
              : color(247, 206, 118, brightness);
          dot(x, y, 1, rgb);
          pixel(centerX + int((radius - 4) * xs[ray]), centerY + int((radius - 4) * ys[ray]),
                color(190, 161, 247, brightness * .5));
        }
      }
    }
    const double fade = t < 1.8 ? 1 : (3 - t) / 1.2;
    for (uint32_t i = 0; i < 36; ++i) {
      const uint32_t seed = hash(state.eventId * 37 + i);
      const double age = t - (seed % 300) / 1000.0;
      if (age < 0) continue;
      const int side = i % 2 ? 1 : -1;
      const int x = kFrameWidth / 2 + side * (154 + int((seed >> 8) % 32))
          + int(side * 14 * age);
      const int y = 42 + int((seed >> 16) % 65) - int(48 * age) + int(62 * age * age);
      const uint16_t rgb = i % 3 == 0 ? color(121, 229, 209, fade)
          : i % 3 == 1 ? color(247, 206, 118, fade) : color(190, 161, 247, fade);
      if (i % 7 == 0) spark(x, y, 3, rgb);
      else {
        pixel(x, y, rgb); pixel(x + 1, y, rgb);
        pixel(x, y + 1, rgb); pixel(x + 1, y + 1, rgb);
        pixel(x + 2, y, rgb); pixel(x + 2, y + 1, rgb);
      }
    }
  }
  // Immediate, quiet acknowledgement even while a surprise waits for center.
  if (state.eventId && t < .5 && (state.requestedMode == CharacterMode::Surprise
      || state.mode == CharacterMode::Surprise)) {
    const uint16_t rgb = color(174, 217, 251, (.5 - t) * 1.6);
    spark(38 - int(t * 10), 65 - int(t * 14), 3, rgb);
    spark(362 + int(t * 10), 65 - int(t * 14), 3, rgb);
  }
  return error_ == nullptr;
}
}
