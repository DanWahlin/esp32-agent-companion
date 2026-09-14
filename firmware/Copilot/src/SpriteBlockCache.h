#pragma once

#include <cstddef>
#include <cstdint>

namespace copilot {

constexpr size_t kSpriteCacheSlots = 8;
constexpr size_t kSpriteCachePageBytes = 32768;
constexpr size_t kSpriteCacheWindowBytes = 65536;

// All metadata access requires the caller's lock. The caller owns the memory
// and must keep it alive until all pins and outstanding worker writes finish.
class SpriteBlockCache {
 public:
  struct Lookup {
    const uint8_t* data;
    int slot;
    bool load;
  };

  struct Job {
    uint8_t* data;
    size_t offset;
    size_t size;
  };

  SpriteBlockCache(uint8_t* memory, size_t bytes, size_t fileSize);
  SpriteBlockCache(const SpriteBlockCache&) = delete;
  SpriteBlockCache& operator=(const SpriteBlockCache&) = delete;

  Lookup acquire(size_t offset, size_t size);
  Job job(int slot) const;
  // Success means the worker verified the entire job against compiled flash.
  // Complete each issued job once, after its writes have finished.
  void complete(int slot, bool success);
  // Release each successful acquire once; invalid indices/unpinned slots are safe.
  void release(int slot);
  // Terminal: does not modify buffers, cancel worker writes, or revoke pins.
  void disable();
  bool valid() const;

 private:
  enum class State : uint8_t { Empty, Loading, Ready };
  struct Slot {
    State state = State::Empty;
    size_t offset = 0;
    size_t size = 0;
    size_t pins = 0;
  };

  bool contains(const Slot& slot, size_t offset, size_t size) const;
  bool validSlot(int slot) const;
  void touch(int slot);

  uint8_t* memory_;
  size_t fileSize_;
  bool enabled_;
  Slot slots_[kSpriteCacheSlots];
  int recency_[kSpriteCacheSlots];
};

}
