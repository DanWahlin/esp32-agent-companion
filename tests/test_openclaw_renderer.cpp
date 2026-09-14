#include "../firmware/Copilot/src/OpenClawSpriteRenderer.h"
#include "../firmware/Copilot/src/SpriteStorage.h"
#include "../firmware/Copilot/generated/openclaw_assets.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
uint8_t source = 0;
std::vector<std::pair<size_t, size_t>> prefetched;

bool inflate(uint8_t* output, size_t outputSize, const uint8_t*, size_t inputSize,
             size_t, size_t) {
  if (!inputSize) return false;
  std::fill(output, output + outputSize, 0);
  return true;
}
}

namespace copilot {
SpriteBlockLease acquireOpenClawBlock(size_t, size_t bytes) {
  return bytes ? SpriteBlockLease{&source, -1} : SpriteBlockLease{nullptr, -1};
}

void releaseOpenClawBlock(const SpriteBlockLease&) {}

void prefetchOpenClawBlock(size_t offset, size_t bytes) {
  prefetched.emplace_back(offset, bytes);
}
}

int main() {
  using namespace copilot;
  std::vector<uint16_t> scratch(kCharacterFrameWidth * kFrameHeight);
  std::vector<uint16_t> cached(kCharacterFrameWidth * kCharacterFrameHeight);
  std::vector<uint16_t> frame(kCharacterFrameWidth * kCharacterFrameHeight);
  OpenClawSpriteRenderer renderer(scratch.data(), cached.data(), inflate);

  SpritePose pose{};
  assert(renderer.render(pose, 0, frame.data()));
  assert(prefetched.size() == kOpenClawBlinkLevels - 1);
  for (unsigned blink = 1; blink < kOpenClawBlinkLevels; ++blink) {
    assert(prefetched[blink - 1] == std::make_pair(
        static_cast<size_t>(kOpenClawFrames[blink].offset),
        static_cast<size_t>(kOpenClawFrames[blink].size)));
  }

  prefetched.clear();
  pose.blinkLevel = 1;
  pose.blinkBlend = 128;
  assert(renderer.render(pose, 0, frame.data()));
  assert(prefetched.size() == 1);
  assert(prefetched[0] == std::make_pair(
      static_cast<size_t>(kOpenClawFrames[3].offset),
      static_cast<size_t>(kOpenClawFrames[3].size)));

  prefetched.clear();
  pose.index = 4;
  pose.blinkLevel = 0;
  pose.blinkBlend = 0;
  assert(renderer.render(pose, 0, frame.data()));
  assert(prefetched.size() == 1);
  const unsigned movingBlink = pose.index * kOpenClawBlinkLevels + 1;
  assert(prefetched[0] == std::make_pair(
      static_cast<size_t>(kOpenClawFrames[movingBlink].offset),
      static_cast<size_t>(kOpenClawFrames[movingBlink].size)));

  std::puts("PASS: OpenClaw prefetches upcoming nonresident blink levels");
}
