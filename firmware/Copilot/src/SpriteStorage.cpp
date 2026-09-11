#include "SpriteStorage.h"
#include "../generated/sprite_assets.h"
#ifdef ARDUINO_ARCH_ESP32
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <cstring>
#endif

namespace copilot {
namespace {
const char* error = nullptr;
#ifdef ARDUINO_ARCH_ESP32
esp_partition_mmap_handle_t mapping;
#endif
}

#ifdef ARDUINO_ARCH_ESP32
const uint8_t* kSpriteData = nullptr;
#else
extern const uint8_t kSpriteDataBlob[];
const uint8_t* kSpriteData = kSpriteDataBlob;
#endif

const char* spriteStorageError() { return error; }

bool initializeSpriteStorage() {
  error = nullptr;
#ifdef ARDUINO_ARCH_ESP32
  if (kSpriteData) return true;
  const auto* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "assets");
  if (!partition || !kSpriteDataSize || kSpriteDataSize > partition->size) {
    error = "Sprite asset partition is missing or too small. Perform a complete firmware upload.";
    return false;
  }
  const void* data = nullptr;
  if (esp_partition_mmap(partition, 0, kSpriteDataSize, ESP_PARTITION_MMAP_DATA, &data, &mapping) != ESP_OK) {
    error = "Read-only sprite flash mapping failed.";
    return false;
  }
  uint8_t digest[32];
  const int status = mbedtls_sha256(static_cast<const uint8_t*>(data), kSpriteDataSize, digest, 0);
  if (status != 0 || std::memcmp(digest, kSpriteDataSha256, sizeof(digest)) != 0) {
    esp_partition_munmap(mapping);
    error = "Sprite asset SHA256 mismatch. Upload firmware and its matching assets together.";
    return false;
  }
  kSpriteData = static_cast<const uint8_t*>(data);
#endif
  return true;
}
}
