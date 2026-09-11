#include "SpriteBlockCache.h"

#include <limits>

namespace copilot {

SpriteBlockCache::SpriteBlockCache(uint8_t* memory, size_t bytes, size_t fileSize)
    : memory_(memory),
      fileSize_(fileSize),
      enabled_(memory && bytes >= kSpriteCacheSlots * kSpriteCacheWindowBytes && fileSize) {
  for (size_t i = 0; i < kSpriteCacheSlots; ++i) recency_[i] = static_cast<int>(i);
}

bool SpriteBlockCache::valid() const {
  return enabled_;
}

bool SpriteBlockCache::validSlot(int slot) const {
  return slot >= 0 && static_cast<size_t>(slot) < kSpriteCacheSlots;
}

bool SpriteBlockCache::contains(const Slot& slot, size_t offset, size_t size) const {
  return offset >= slot.offset && offset - slot.offset <= slot.size
      && size <= slot.size - (offset - slot.offset);
}

void SpriteBlockCache::touch(int slot) {
  size_t index = 0;
  while (recency_[index] != slot) ++index;
  while (index) {
    recency_[index] = recency_[index - 1];
    --index;
  }
  recency_[0] = slot;
}

SpriteBlockCache::Lookup SpriteBlockCache::acquire(size_t offset, size_t size) {
  const Lookup miss{nullptr, -1, false};
  if (!enabled_ || !size || size > kSpriteCachePageBytes
      || offset >= fileSize_ || size > fileSize_ - offset) return miss;

  for (size_t i = 0; i < kSpriteCacheSlots; ++i) {
    Slot& slot = slots_[i];
    if (slot.state != State::Ready || !contains(slot, offset, size)) continue;
    if (slot.pins == std::numeric_limits<size_t>::max()) return miss;
    ++slot.pins;
    touch(static_cast<int>(i));
    return {memory_ + i * kSpriteCacheWindowBytes + (offset - slot.offset),
            static_cast<int>(i), false};
  }
  for (const Slot& slot : slots_) {
    if (slot.state == State::Loading && contains(slot, offset, size)) return miss;
  }

  int selected = -1;
  for (size_t i = 0; i < kSpriteCacheSlots; ++i) {
    if (slots_[i].state == State::Empty) {
      selected = static_cast<int>(i);
      break;
    }
  }
  if (selected < 0) {
    for (size_t i = kSpriteCacheSlots; i > 0; --i) {
      const int candidate = recency_[i - 1];
      if (slots_[candidate].state == State::Ready && !slots_[candidate].pins) {
        selected = candidate;
        break;
      }
    }
  }
  if (selected < 0) return miss;

  Slot& slot = slots_[selected];
  slot.state = State::Loading;
  slot.offset = offset - offset % kSpriteCachePageBytes;
  const size_t remaining = fileSize_ - slot.offset;
  slot.size = remaining < kSpriteCacheWindowBytes ? remaining : kSpriteCacheWindowBytes;
  slot.pins = 0;
  return {nullptr, selected, true};
}

SpriteBlockCache::Job SpriteBlockCache::job(int slot) const {
  if (!enabled_ || !validSlot(slot) || slots_[slot].state != State::Loading)
    return {nullptr, 0, 0};
  return {memory_ + static_cast<size_t>(slot) * kSpriteCacheWindowBytes,
          slots_[slot].offset, slots_[slot].size};
}

void SpriteBlockCache::complete(int slot, bool success) {
  if (!validSlot(slot) || slots_[slot].state != State::Loading) return;
  slots_[slot].state = success && enabled_ ? State::Ready : State::Empty;
  if (slots_[slot].state == State::Ready) touch(slot);
}

void SpriteBlockCache::release(int slot) {
  if (validSlot(slot) && slots_[slot].pins) --slots_[slot].pins;
}

void SpriteBlockCache::disable() {
  enabled_ = false;
}

}
