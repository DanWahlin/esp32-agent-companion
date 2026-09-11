#pragma once
#include <cstddef>
#include <cstdint>

namespace copilot {
// Initialize once before starting rendering; the read-only mapping lives for the application's lifetime.
bool initializeSpriteStorage();
const char* spriteStorageError();

struct SpriteBlockLease {
  const uint8_t* data;
  int slot;
};
struct SdSpriteStatus {
  const char* state;
  const char* detail;
  const char* cardType;
  uint64_t capacityBytes;
  uint32_t cacheBytes, hits, misses, revision;
};

// Optional, read-only boot initialization. Errors leave the verified flash source active.
void initializeSdSpriteStorage();
SdSpriteStatus sdSpriteStatus();
SpriteBlockLease acquireSpriteBlock(size_t offset, size_t bytes);
void releaseSpriteBlock(const SpriteBlockLease& block);
}
