#include "SpriteStorage.h"
#include "Config.h"
#include "../generated/sprite_assets.h"
#ifdef ARDUINO_ARCH_ESP32
#include "SpriteBlockCache.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <new>
#include <algorithm>
#endif

namespace copilot {
namespace {
const char* error = nullptr;
SdSpriteStatus sdStatus{"not_initialized", "Using built-in flash assets.", "none", 0, 0, 0, 0, 0};
#ifdef ARDUINO_ARCH_ESP32
esp_partition_mmap_handle_t mapping;
portMUX_TYPE cacheLock = portMUX_INITIALIZER_UNLOCKED;
SpriteBlockCache* cache = nullptr;
alignas(SpriteBlockCache) uint8_t cacheObject[sizeof(SpriteBlockCache)];
uint8_t* cacheMemory = nullptr;
uint8_t* readScratch = nullptr;
QueueHandle_t readJobs = nullptr;
File packFile;
bool sdInitialized = false;

void storageState(const char* state, const char* detail) {
  portENTER_CRITICAL(&cacheLock);
  sdStatus.state = state;
  sdStatus.detail = detail;
  ++sdStatus.revision;
  portEXIT_CRITICAL(&cacheLock);
}

void failReads(const char* reason) {
  portENTER_CRITICAL(&cacheLock);
  if (cache) cache->disable();
  portEXIT_CRITICAL(&cacheLock);
  packFile.close();
  storageState("read_error", reason);
}

void loadPages(void*) {
  for (;;) {
    int slot;
    xQueueReceive(readJobs, &slot, portMAX_DELAY);
    portENTER_CRITICAL(&cacheLock);
    const auto job = cache->job(slot);
    portEXIT_CRITICAL(&cacheLock);
    if (!job.data) continue;
    bool good = packFile.seek(job.offset);
    for (size_t done = 0; good && done < job.size;) {
      const size_t bytes = std::min<size_t>(kSdReadChunkBytes, job.size - done);
      good = packFile.read(readScratch, bytes) == bytes;
      // Recheck after boot validation: card removal or changed files must never corrupt a frame.
      good = good && std::memcmp(readScratch, kSpriteData + job.offset + done, bytes) == 0;
      if (!good) break;
      std::memcpy(job.data + done, readScratch, bytes);
      done += bytes;
      vTaskDelay(1);
    }
    portENTER_CRITICAL(&cacheLock);
    cache->complete(slot, good);
    portEXIT_CRITICAL(&cacheLock);
    if (!good) failReads("Card read failed or pack changed; using verified flash. Restart to retry.");
  }
}
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

SdSpriteStatus sdSpriteStatus() {
#ifdef ARDUINO_ARCH_ESP32
  portENTER_CRITICAL(&cacheLock);
  const auto result = sdStatus;
  portEXIT_CRITICAL(&cacheLock);
  return result;
#else
  return sdStatus;
#endif
}

void initializeSdSpriteStorage() {
#ifdef ARDUINO_ARCH_ESP32
  if (sdInitialized) return;
  sdInitialized = true;
  if (!kSpriteData) {
    storageState("unavailable", "Initialize verified flash assets before microSD.");
    return;
  }
  if (!SD_MMC.setPins(kSdClock, kSdCommand, kSdData)) {
    storageState("unavailable", "SD_MMC pin configuration failed; using flash.");
    return;
  }
  if (!SD_MMC.begin("/sdcard", true, false, kSdFrequencyKhz, 2)) {
    storageState("mount_failed", "Card absent or filesystem could not mount; no formatting attempted.");
    return;
  }
  const auto type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    storageState("mount_failed", "No card detected; using flash.");
    return;
  }
  sdStatus.cardType = type == CARD_MMC ? "mmc" : type == CARD_SD ? "sdsc" : "sdhc_sdxc";
  sdStatus.capacityBytes = SD_MMC.cardSize();
  packFile = SD_MMC.open(kSdSpritePath, FILE_READ);
  if (!packFile) {
    storageState("pack_missing", "Card mounted; copy the matching sprite pack to /copilot/sprite-firmware.bin.");
    return;
  }
  if (packFile.isDirectory() || packFile.size() != kSpriteDataSize) {
    packFile.close();
    storageState("pack_invalid", "Pack size does not match this firmware; using flash.");
    return;
  }
  readScratch = static_cast<uint8_t*>(heap_caps_malloc(kSdReadChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!readScratch) {
    packFile.close();
    storageState("unavailable", "Cannot allocate SD read buffer; using flash.");
    return;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool good = mbedtls_sha256_starts(&hash, 0) == 0;
  const uint32_t started = millis();
  size_t read = 0;
  while (good && read < kSpriteDataSize) {
    const size_t bytes = std::min<size_t>(kSdReadChunkBytes, kSpriteDataSize - read);
    good = packFile.read(readScratch, bytes) == bytes;
    good = good && mbedtls_sha256_update(&hash, readScratch, bytes) == 0;
    good = good && millis() - started <= kSdVerifyTimeoutMs;
    read += bytes;
    yield();
  }
  uint8_t digest[32];
  good = good && mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  good = good && std::memcmp(digest, kSpriteDataSha256, sizeof(digest)) == 0;
  if (!good) {
    packFile.close();
    heap_caps_free(readScratch);
    readScratch = nullptr;
    storageState("pack_invalid", "Pack read, timeout, or SHA256 check failed; using flash.");
    return;
  }
  constexpr size_t cacheBytes = kSpriteCacheSlots * kSpriteCacheWindowBytes;
  cacheMemory = static_cast<uint8_t*>(heap_caps_malloc(cacheBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (cacheMemory) cache = new (cacheObject) SpriteBlockCache(cacheMemory, cacheBytes, kSpriteDataSize);
  if (cache && cache->valid()) readJobs = xQueueCreate(kSpriteCacheSlots, sizeof(int));
  if (!readJobs || xTaskCreatePinnedToCore(loadPages, "copilot-sd", 4096, nullptr, 0, nullptr, 1) != pdPASS) {
    if (readJobs) vQueueDelete(readJobs);
    readJobs = nullptr;
    if (cache) cache->~SpriteBlockCache();
    cache = nullptr;
    heap_caps_free(cacheMemory);
    cacheMemory = nullptr;
    heap_caps_free(readScratch);
    readScratch = nullptr;
    packFile.close();
    storageState("unavailable", "Cannot allocate SD cache or reader task; using flash.");
    return;
  }
  sdStatus.cacheBytes = cacheBytes;
  storageState("ready", "Verified SD pack; cached reads with identical flash fallback. No card writes.");
#else
  sdStatus.state = "host";
  sdStatus.detail = "Native preview uses the verified embedded assets.";
  ++sdStatus.revision;
#endif
}

SpriteBlockLease acquireSpriteBlock(size_t offset, size_t bytes) {
  if (!kSpriteData || !bytes || offset > kSpriteDataSize || bytes > kSpriteDataSize - offset)
    return {nullptr, -1};
#ifdef ARDUINO_ARCH_ESP32
  if (cache) {
    portENTER_CRITICAL(&cacheLock);
    const auto result = cache->acquire(offset, bytes);
    if (result.data) ++sdStatus.hits;
    else ++sdStatus.misses;
    portEXIT_CRITICAL(&cacheLock);
    if (result.data) return {result.data, result.slot};
    if (result.load && xQueueSend(readJobs, &result.slot, 0) != pdTRUE) {
      portENTER_CRITICAL(&cacheLock);
      cache->complete(result.slot, false);
      portEXIT_CRITICAL(&cacheLock);
      storageState("queue_full", "SD prefetch queue full; current frame uses verified flash.");
    }
  }
#endif
  return {kSpriteData + offset, -1};
}

void releaseSpriteBlock(const SpriteBlockLease& block) {
#ifdef ARDUINO_ARCH_ESP32
  if (cache && block.slot >= 0) {
    portENTER_CRITICAL(&cacheLock);
    cache->release(block.slot);
    portEXIT_CRITICAL(&cacheLock);
  }
#else
  (void)block;
#endif
}
}
