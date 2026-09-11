#include "SpriteRenderer.h"
#include <cstring>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_timer.h>
#endif

namespace copilot {
static_assert(kSpriteDisplayReady && kSpriteWidth == kFrameWidth && kSpriteHeight == kFrameHeight,
              "Export display-ready sprites before building the firmware.");
namespace {
uint64_t microsNow() {
#ifdef ARDUINO_ARCH_ESP32
  return esp_timer_get_time();
#else
  return 0;
#endif
}
}

SpriteRenderer::SpriteRenderer(uint16_t* firstOpenPatch, uint16_t* secondOpenPatch, uint16_t* patch,
                               uint16_t* firstOutput, uint16_t* secondOutput, InflateSprite inflate)
    : openPatches_{firstOpenPatch, secondOpenPatch}, patch_(patch),
      outputs_{firstOutput, secondOutput}, inflate_(inflate) {}

bool SpriteRenderer::decode(const SpriteBlock& block, uint16_t* output, size_t pixels) {
  if (!block.size || block.offset > kSpriteDataSize || block.size > kSpriteDataSize - block.offset) {
    error_ = "Sprite block lies outside the embedded asset data.";
    return false;
  }
  if (!inflate_(reinterpret_cast<uint8_t*>(output), pixels * sizeof(uint16_t),
                kSpriteData + block.offset, block.size)) {
    error_ = "Sprite asset decompression failed.";
    return false;
  }
  return true;
}

bool SpriteRenderer::render(const SpritePose& pose, uint16_t* frame) {
  error_ = nullptr;
  decodeUs = compositeUs = eyesUs = 0;
  if (!kSpriteData || !openPatches_[0] || !openPatches_[1] || !patch_ || !outputs_[0] || !outputs_[1]
      || outputs_[0] == outputs_[1] || openPatches_[0] == openPatches_[1]
      || patch_ == openPatches_[0] || patch_ == openPatches_[1] || !inflate_ || !frame) {
    error_ = "Sprite renderer requires initialized storage and distinct persistent buffers.";
    return false;
  }
  if (pose.direction >= kSpriteDirections || pose.index >= kSpriteSteps || pose.blinkLevel >= kSpriteBlinkLevels) {
    error_ = "Invalid discrete sprite pose.";
    return false;
  }
  const int outputIndex = frame == outputs_[0] ? 0 : frame == outputs_[1] ? 1 : -1;
  if (outputIndex < 0) {
    error_ = "Sprite output buffer is not registered.";
    return false;
  }
  const unsigned index = pose.direction * kSpriteSteps + pose.index;
  const uint32_t key = index * kSpriteBlinkLevels + pose.blinkLevel;
  if (outputKeys_[outputIndex] == key) return true;
  const SpriteFrame& entry = kSpriteFrames[index];
  const size_t patchPixels = static_cast<size_t>(entry.patchWidth) * entry.patchHeight;
  if ((entry.patchWidth == 0) != (entry.patchHeight == 0)
      || entry.patchX > kFrameWidth || entry.patchY > kFrameHeight
      || entry.patchWidth > kFrameWidth - entry.patchX
      || entry.patchHeight > kFrameHeight - entry.patchY
      || patchPixels > kSpriteMaxPatchPixels) {
    error_ = "Sprite blink patch lies outside its frame or buffer.";
    return false;
  }

  // Each physical framebuffer owns its cache; the other core may be transmitting the other one.
  outputKeys_[outputIndex] = UINT32_MAX;
  bool sameBase = baseKeys_[outputIndex] != UINT32_MAX;
  if (sameBase) {
    const SpriteFrame& cached = kSpriteFrames[baseKeys_[outputIndex]];
    sameBase = cached.base.offset == entry.base.offset && cached.base.size == entry.base.size
        && cached.patchX == entry.patchX && cached.patchY == entry.patchY
        && cached.patchWidth == entry.patchWidth && cached.patchHeight == entry.patchHeight;
  }
  if (!sameBase) {
    baseKeys_[outputIndex] = UINT32_MAX;
    uint64_t started = microsNow();
    if (!decode(entry.base, frame, kFrameWidth * kFrameHeight)) return false;
    decodeUs = microsNow() - started;
    started = microsNow();
    for (int y = 0; y < entry.patchHeight; ++y) {
      std::memcpy(openPatches_[outputIndex] + y * entry.patchWidth,
                  frame + (entry.patchY + y) * kFrameWidth + entry.patchX,
                  entry.patchWidth * sizeof(uint16_t));
    }
    compositeUs = microsNow() - started;
  }
  baseKeys_[outputIndex] = index;

  const uint64_t started = microsNow();
  if (patchPixels) {
    const uint16_t* pixels = openPatches_[outputIndex];
    if (pose.blinkLevel) {
      if (!decode(entry.blinks[pose.blinkLevel - 1], patch_, patchPixels)) return false;
      pixels = patch_;
    }
    for (int y = 0; y < entry.patchHeight; ++y) {
      std::memcpy(frame + (entry.patchY + y) * kFrameWidth + entry.patchX,
                  pixels + y * entry.patchWidth, entry.patchWidth * sizeof(uint16_t));
    }
  }
  eyesUs = microsNow() - started;
  outputKeys_[outputIndex] = key;
  return true;
}
}
