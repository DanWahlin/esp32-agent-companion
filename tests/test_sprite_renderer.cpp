#include "../firmware/Copilot/src/SpriteRenderer.h"
#include "../tools/HostInflate.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace copilot;
static unsigned calls = 0;
static bool failInflate = false;
static bool inflate(uint8_t* output, size_t size, const uint8_t* input, size_t compressed) {
  ++calls;
  if (failInflate) {
    std::memset(output, 0xa5, std::min<size_t>(128, size));
    return false;
  }
  return inflateAtlasHost(output, size, input, compressed);
}

int main() {
  constexpr size_t sourcePixels = kSpriteWidth * kSpriteHeight;
  constexpr size_t outputPixels = kFrameWidth * kFrameHeight;
  std::vector<uint16_t> expected(sourcePixels);
  const size_t guardedPatch = std::max<size_t>(1, kSpriteMaxPatchPixels) + 2;
  std::vector<uint16_t> firstOpen(guardedPatch, 0xa55a), secondOpen(guardedPatch, 0xa55a);
  std::vector<uint16_t> patch(guardedPatch, 0xa55a);
  std::vector<uint16_t> guarded(outputPixels + 2, 0xa55a), second(outputPixels);
  uint16_t* frame = guarded.data() + 1;
  SpriteRenderer renderer(firstOpen.data() + 1, secondOpen.data() + 1, patch.data() + 1,
                          frame, second.data(), inflate);
  std::ofstream hashes("build/sprite-renderer-pixels.fnv");
  for (int index = 0; index < kSpriteFrameCount; ++index) {
    const auto& metadata = kSpriteFrames[index];
    unsigned visit = 0;
    for (int level : {0, 1, 2, 3, 4, 3, 2, 1, 0}) {
      assert(inflateAtlasHost(reinterpret_cast<uint8_t*>(expected.data()), sourcePixels * 2,
                              kSpriteData + metadata.base.offset, metadata.base.size));
      if (level && metadata.patchWidth) {
        std::vector<uint16_t> changes(metadata.patchWidth * metadata.patchHeight);
        const auto& block = metadata.blinks[level - 1];
        assert(inflateAtlasHost(reinterpret_cast<uint8_t*>(changes.data()), changes.size() * 2,
                                kSpriteData + block.offset, block.size));
        for (int y = 0; y < metadata.patchHeight; ++y) {
          std::copy_n(changes.data() + y * metadata.patchWidth, metadata.patchWidth,
                      expected.data() + (metadata.patchY + y) * kSpriteWidth + metadata.patchX);
        }
      }
      SpritePose pose{static_cast<uint8_t>(index / kSpriteSteps),
                      static_cast<uint8_t>(index % kSpriteSteps), static_cast<uint8_t>(level)};
      const auto previousSecond = second;
      assert(renderer.render(pose, frame));
      assert(second == previousSecond);
      uint64_t hash = UINT64_C(14695981039346656037);
      const auto* bytes = reinterpret_cast<const uint8_t*>(frame);
      for (size_t i = 0; i < outputPixels * 2; ++i) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
      }
      if (visit++ < kSpriteBlinkLevels) hashes << index << " " << level << " " << hash << "\n";
      assert(std::memcmp(frame, expected.data(), outputPixels * 2) == 0);
      assert(guarded.front() == 0xa55a && guarded.back() == 0xa55a);
      const unsigned before = calls;
      assert(renderer.render(pose, frame));
      assert(calls == before);
      assert(renderer.render(pose, second.data()));
      assert(std::memcmp(frame, second.data(), outputPixels * 2) == 0);
      const unsigned after = calls;
      assert(renderer.render(pose, second.data()) && calls == after);
      for (const auto* buffer : {&firstOpen, &secondOpen, &patch}) {
        assert(buffer->front() == 0xa55a && buffer->back() == 0xa55a);
      }
    }
  }
  for (int level = 0; level < kSpriteBlinkLevels; ++level) {
    assert(renderer.render({0, 0, static_cast<uint8_t>(level)}, frame));
    for (int direction = 1; direction < kSpriteDirections; ++direction) {
      assert(renderer.render({static_cast<uint8_t>(direction), 0, static_cast<uint8_t>(level)}, second.data()));
      assert(std::memcmp(frame, second.data(), outputPixels * 2) == 0);
    }
  }
  assert(renderer.render({0, 0, 0}, frame));
  const unsigned centerCalls = calls;
  assert(renderer.render({1, 0, 0}, frame));
  assert(calls == centerCalls);
  assert(!renderer.render({static_cast<uint8_t>(kSpriteDirections), 0, 0}, frame) && renderer.error());
  assert(!renderer.render({0, 24, 0}, frame) && renderer.error());
  assert(!renderer.render({0, 0, 5}, frame) && renderer.error());
  assert(!renderer.render({0, 0, 0}, nullptr) && renderer.error());
  assert(!renderer.render({0, 0, 0}, expected.data()) && renderer.error());
  assert(renderer.render({0, 0, 0}, frame));
  const std::vector<uint16_t> previous(frame, frame + outputPixels);
  failInflate = true;
  assert(!renderer.render({4, 12, 0}, frame) && renderer.error());
  failInflate = false;
  assert(renderer.render({0, 0, 0}, frame) && !renderer.error());
  assert(std::equal(previous.begin(), previous.end(), frame));
  assert(renderer.render({4, 12, 0}, frame) && !renderer.error());
  const std::vector<uint16_t> open(frame, frame + outputPixels);
  failInflate = true;
  assert(!renderer.render({4, 12, 2}, frame) && renderer.error());
  failInflate = false;
  assert(renderer.render({4, 12, 0}, frame) && !renderer.error());
  assert(std::equal(open.begin(), open.end(), frame));
  SpriteRenderer aliased(firstOpen.data() + 1, firstOpen.data() + 1, patch.data() + 1,
                         frame, second.data(), inflate);
  assert(!aliased.render({0, 0, 0}, frame) && aliased.error());
  std::ofstream image("build/sprite-host-frame.ppm", std::ios::binary);
  image << "P6\n" << kFrameWidth << " " << kFrameHeight << "\n255\n";
  for (size_t i = 0; i < outputPixels; ++i) {
    const uint16_t color = __builtin_bswap16(frame[i]);
    const char rgb[] = {static_cast<char>((color >> 11) * 255 / 31),
                        static_cast<char>(((color >> 5) & 63) * 255 / 63),
                        static_cast<char>((color & 31) * 255 / 31)};
    image.write(rgb, 3);
  }
  assert(image.good());
  assert(hashes.good());
  std::cout << "PASS: all " << kSpriteFrameCount * kSpriteBlinkLevels
            << " states, every output pixel, reopening, shared centers, independent caches and recovery\n";
}
