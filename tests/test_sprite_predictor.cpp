#include "../tools/HostSpriteInflate.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

using copilot::SpritePredictor;

static void check(size_t width, size_t height) {
  const size_t count = width * height;
  std::vector<uint16_t> original(count), expected(count);
  std::vector<uint8_t> filtered(count * 2);
  for (size_t i = 0; i < count; ++i) {
    original[i] = static_cast<uint16_t>(i * 977 + (i / width) * 3571);
    expected[i] = __builtin_bswap16(original[i]);
    const unsigned above = i >= width ? original[i - width] : 0;
    const uint16_t delta = static_cast<uint16_t>(original[i] - above);
    filtered[i * 2] = delta >> 8;
    filtered[i * 2 + 1] = delta & 255;
  }
  std::vector<uint16_t> guarded(count + 2, 0xa55a);
  auto* output = reinterpret_cast<uint8_t*>(guarded.data() + 1);
  SpritePredictor predictor;
  for (size_t chunk : {2u, 46u, 32768u}) {
    assert(predictor.reset(output, filtered.size(), width));
    assert(!predictor.complete());
    for (size_t offset = 0; offset < filtered.size(); offset += chunk)
      assert(predictor.consume(filtered.data() + offset, std::min(chunk, filtered.size() - offset)));
    assert(predictor.complete());
    assert(std::equal(expected.begin(), expected.end(), guarded.begin() + 1));
    assert(guarded.front() == 0xa55a && guarded.back() == 0xa55a);
    assert(!predictor.consume(filtered.data(), 2));
  }
  std::vector<uint8_t> compressed(compressBound(filtered.size()));
  uLongf size = compressed.size();
  assert(compress2(compressed.data(), &size, filtered.data(), filtered.size(), 9) == Z_OK);
  compressed.resize(size);
  assert(inflateSpriteHost(output, filtered.size(), compressed.data(), size, width, width));
  assert(std::equal(expected.begin(), expected.end(), guarded.begin() + 1));
  const size_t stride = std::min<size_t>(copilot::kCharacterFrameWidth, width + 8);
  std::vector<uint16_t> strided(stride * height + 2, 0xa55a);
  auto* spaced = reinterpret_cast<uint8_t*>(strided.data() + 1);
  assert(inflateSpriteHost(spaced, filtered.size(), compressed.data(), size, width, stride));
  for (size_t y = 0; y < height; ++y) {
    assert(std::equal(expected.begin() + y * width, expected.begin() + (y + 1) * width,
                      strided.begin() + 1 + y * stride));
    for (size_t x = width; x < stride; ++x) assert(strided[1 + y * stride + x] == 0xa55a);
  }
  assert(strided.front() == 0xa55a && strided.back() == 0xa55a);
  assert(!inflateSpriteHost(output, filtered.size(), compressed.data(), size - 1, width, width));
  compressed.push_back(0);
  assert(!inflateSpriteHost(output, filtered.size(), compressed.data(), compressed.size(), width, width));
  compressed[compressed.size() / 2] ^= 0xff;
  assert(!inflateSpriteHost(output, filtered.size(), compressed.data(), size, width, width));
  assert(!predictor.reset(output, filtered.size(), 0));
  assert(!predictor.reset(output, filtered.size(), copilot::kCharacterFrameWidth + 1));
  assert(!predictor.reset(output, filtered.size(), width, copilot::kCharacterFrameWidth + 1));
  assert(!predictor.reset(output + 1, filtered.size(), width));
  assert(!predictor.reset(output, filtered.size() - 1, width));
  assert(!predictor.consume(filtered.data(), 2));
  assert(!predictor.complete());
  assert(predictor.reset(output, filtered.size(), width));
  assert(!predictor.consume(filtered.data(), 1));
  assert(!predictor.consume(filtered.data(), filtered.size() + 2));
  std::fill(filtered.begin(), filtered.end(), 0);
  assert(predictor.reset(output, filtered.size(), width));
  assert(predictor.consume(filtered.data(), filtered.size()) && predictor.complete());
  assert(std::all_of(guarded.begin() + 1, guarded.end() - 1, [](uint16_t x) { return x == 0; }));
  assert(guarded.front() == 0xa55a && guarded.back() == 0xa55a);
}

int main() {
  for (size_t width : {1u, 2u, 21u, 39u, 412u}) check(width, 352);
  std::cout << "PASS: word prediction, row/ring boundaries, uint16 wrap, reset, guards and corrupt streams\n";
}
