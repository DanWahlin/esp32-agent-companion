#include "OpenClawSpriteRenderer.h"
#include "Character.h"
#include "Config.h"
#include "SpriteStorage.h"
#include "../generated/openclaw_assets.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace copilot {
namespace {
constexpr size_t kPixels = static_cast<size_t>(kCharacterFrameWidth) * kFrameHeight;
}

bool OpenClawSpriteRenderer::decode(unsigned blockIndex, uint16_t* output) {
  if (blockIndex >= sizeof(kOpenClawFrames) / sizeof(kOpenClawFrames[0])) {
    error_ = "OpenClaw sprite block index is invalid.";
    return false;
  }
  const auto block = kOpenClawFrames[blockIndex];
  const auto source = acquireOpenClawBlock(block.offset, block.size);
  if (!source.data) {
    error_ = "OpenClaw SD sprite source is unavailable.";
    return false;
  }
  const bool decoded = block.size && inflate_(
      reinterpret_cast<uint8_t*>(output), kPixels * sizeof(uint16_t),
      source.data, block.size, kCharacterFrameWidth, kCharacterFrameWidth);
  releaseOpenClawBlock(source);
  if (!decoded) error_ = "OpenClaw sprite decompression failed.";
  return decoded;
}

void OpenClawSpriteRenderer::prefetch(
    unsigned direction, unsigned index, unsigned blinkLevel) {
  if (direction >= kOpenClawDirections || index >= kOpenClawSteps
      || blinkLevel >= kOpenClawBlinkLevels) return;
  const auto block = kOpenClawFrames[
      (direction * kOpenClawSteps + index) * kOpenClawBlinkLevels + blinkLevel];
  prefetchOpenClawBlock(block.offset, block.size);
}

bool OpenClawSpriteRenderer::render(
    const SpritePose& pose, float effectSeconds, uint16_t* frame) {
  error_ = nullptr;
  if (!scratch_ || !cached_ || !inflate_ || !frame) {
    error_ = "OpenClaw renderer requires initialized buffers.";
    return false;
  }
  if (pose.direction >= kOpenClawDirections || pose.index >= kOpenClawSteps
      || pose.blinkLevel >= kOpenClawBlinkLevels
      || (pose.blinkBlend && pose.blinkLevel + 1 >= kOpenClawBlinkLevels)
      || !std::isfinite(effectSeconds) || effectSeconds < 0) {
    error_ = "Invalid OpenClaw sprite pose.";
    return false;
  }
  constexpr int top = (kCharacterFrameHeight - kFrameHeight) / 2;
  unsigned index = pose.index;
  if (pose.direction == 9)
    index = 1 + static_cast<unsigned>(std::fmod(effectSeconds * kOpenClawWalkFps, 23));
  const unsigned frameIndex = pose.direction * kOpenClawSteps + index;
  const unsigned blockIndex = frameIndex * kOpenClawBlinkLevels + pose.blinkLevel;
  const uint32_t key = blockIndex * 256 + pose.blinkBlend;
  const auto prefetchUpcomingBlink = [&]() {
    if (!pose.blinkLevel && !pose.blinkBlend) {
      const unsigned last = index == 0 ? kOpenClawBlinkLevels - 1 : 1;
      for (unsigned blink = 1; blink <= last; ++blink)
        prefetch(pose.direction, index, blink);
      return;
    }
    prefetch(pose.direction, index, std::min<unsigned>(
        kOpenClawBlinkLevels - 1, pose.blinkLevel + (pose.blinkBlend ? 2 : 1)));
  };
  if (cacheKey_ == key) {
    std::memcpy(frame, cached_,
                kCharacterFrameWidth * kCharacterFrameHeight * sizeof(uint16_t));
    prefetchUpcomingBlink();
    return true;
  }
  uint16_t* cachedArt = cached_ + top * kCharacterFrameWidth;
  if (!decode(blockIndex, cachedArt)) return false;
  std::memset(cached_, 0, top * kCharacterFrameWidth * sizeof(uint16_t));
  std::memset(cached_ + (top + kFrameHeight) * kCharacterFrameWidth, 0,
              top * kCharacterFrameWidth * sizeof(uint16_t));
  if (pose.blinkBlend) {
    if (!decode(blockIndex + 1, scratch_)) return false;

    int first = kFrameHeight, last = -1;
    for (int y = 0; y < kFrameHeight; ++y) {
      if (std::memcmp(cachedArt + y * kCharacterFrameWidth,
                      scratch_ + y * kCharacterFrameWidth,
                      kCharacterFrameWidth * sizeof(uint16_t)) != 0) {
        first = std::min(first, y);
        last = y;
      }
    }
    if (last >= first) {
      const int height = last - first + 1;
      const int revealHalfHeight = std::max(
          1, static_cast<int>((static_cast<unsigned>(pose.blinkBlend)
              * (height + 1) / 2 + 254) / 255));
      const int center = (first + last) / 2;
      for (int y = first; y <= last; ++y) {
        if (std::abs(y - center) >= revealHalfHeight)
          std::memcpy(cachedArt + y * kCharacterFrameWidth,
                      scratch_ + y * kCharacterFrameWidth,
                      kCharacterFrameWidth * sizeof(uint16_t));
      }
    }
  }
  cacheKey_ = key;
  std::memcpy(frame, cached_,
              kCharacterFrameWidth * kCharacterFrameHeight * sizeof(uint16_t));
  prefetchUpcomingBlink();
  return true;
}
}
