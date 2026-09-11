#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <miniz.h>
#include <algorithm>
#include <cstring>
#include "src/AtlasRenderer.h"
#include "src/AnimationClock.h"

using namespace copilot;

namespace {
Arduino_ESP32QSPI displayBus(12, 38, 4, 5, 6, 7);
Arduino_CO5300 display(&displayBus, 39, 0, 466, 466, 6, 0, 0, 0);
QueueHandle_t freeFrames, readyFrames;
AtlasRenderer* renderer;
tinfl_decompressor inflater;
constexpr size_t kTransferBytes = 4096;
uint8_t* transferBuffer;

struct Frame {
  uint16_t* pixels;
  uint32_t renderUs, decodeUs, compositeUs, eyesUs;
};
Frame frames[2];

[[noreturn]] void fatal(const char* message) {
  for (;;) {
    Serial.printf("FATAL: %s\n", message);
    delay(2000);
  }
}

void* allocate(size_t bytes, uint32_t capabilities, const char* error) {
  void* result = heap_caps_malloc(bytes, capabilities | MALLOC_CAP_8BIT);
  if (!result) fatal(error);
  return result;
}

bool inflatePose(uint8_t* output, size_t outputSize, const uint8_t* input, size_t inputSize) {
  tinfl_init(&inflater);
  size_t inBytes = inputSize, outBytes = outputSize;
  const auto status = tinfl_decompress(&inflater, input, &inBytes, output, output, &outBytes,
      TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  return status == TINFL_STATUS_DONE && outBytes == outputSize && inBytes == inputSize;
}

void animate(void*) {
  Motion motion(esp_random());
  AnimationClock clock;
  const int64_t epoch = esp_timer_get_time();
  int64_t deadline = epoch;
  for (;;) {
    Frame* frame;
    xQueueReceive(freeFrames, &frame, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    if (deadline > now) {
      const uint32_t waitMs = (deadline - now) / 1000;
      if (waitMs) vTaskDelay(pdMS_TO_TICKS(waitMs));
    }
    const int64_t start = esp_timer_get_time();
    if (!renderer->render(motion.sample(clock.advance(start)), frame->pixels)) {
      fatal(renderer->error());
    }
    frame->renderUs = esp_timer_get_time() - start;
    frame->decodeUs = renderer->decodeUs;
    frame->compositeUs = renderer->compositeUs;
    frame->eyesUs = renderer->eyesUs;
    xQueueSend(readyFrames, &frame, portMAX_DELAY);
    deadline += 1000000 / kTargetFps;
    if (deadline < esp_timer_get_time()) deadline = esp_timer_get_time();
    vTaskDelay(1);
  }
}

bool captureFrame(const Frame& frame) {
  constexpr size_t bytes = kFrameWidth * kFrameHeight * 2;
  Serial.printf("FRAME_BE %d %d %u\n", kFrameWidth, kFrameHeight, static_cast<unsigned>(bytes));
  const auto* data = reinterpret_cast<const uint8_t*>(frame.pixels);
  size_t sent = 0;
  int64_t progress = esp_timer_get_time();
  while (sent < bytes) {
    const size_t count = Serial.write(data + sent, std::min<size_t>(4096, bytes - sent));
    sent += count;
    if (count) progress = esp_timer_get_time();
    else if (esp_timer_get_time() - progress > 5000000) {
      Serial.println("\nCAPTURE_ERROR USB write timed out");
      return false;
    }
    if (!count) delay(1);
  }
  Serial.println("\nEND_FRAME");
  return true;
}
}

void setup() {
  Serial.begin(115200);
  if (!psramFound()) fatal("8 MB OPI PSRAM not detected.");
  Serial.printf("\nCopilot deep turns / Waveshare AMOLED 1.75-B\nPSRAM: %u bytes\n", ESP.getPsramSize());
  if (!display.begin(kSpiFrequency)) fatal("CO5300 initialization failed.");
  display.setBrightness(0);
  display.fillScreen(0);
  transferBuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      16, kTransferBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!transferBuffer) fatal("DMA staging allocation failed.");
  auto* first = static_cast<uint8_t*>(allocate(kAtlasWidth * kAtlasHeight, MALLOC_CAP_INTERNAL,
      "First decoded-pose SRAM allocation failed."));
  auto* second = static_cast<uint8_t*>(allocate(kAtlasWidth * kAtlasHeight, MALLOC_CAP_INTERNAL,
      "Second decoded-pose SRAM allocation failed."));
  void* memory = allocate(sizeof(AtlasRenderer), MALLOC_CAP_INTERNAL, "Renderer allocation failed.");
  renderer = new (memory) AtlasRenderer(first, second, inflatePose);
  freeFrames = xQueueCreate(2, sizeof(Frame*));
  readyFrames = xQueueCreate(2, sizeof(Frame*));
  if (!freeFrames || !readyFrames) fatal("Frame queue allocation failed.");
  for (auto& frame : frames) {
    frame.pixels = static_cast<uint16_t*>(allocate(kFrameWidth * kFrameHeight * 2,
        MALLOC_CAP_SPIRAM, "Framebuffer PSRAM allocation failed."));
    std::memset(frame.pixels, 0, kFrameWidth * kFrameHeight * 2);
    Frame* pointer = &frame;
    xQueueSend(freeFrames, &pointer, portMAX_DELAY);
  }
  if (xTaskCreatePinnedToCore(animate, "copilot-render", 16384, nullptr, 1,
                              nullptr, 0) != pdPASS) fatal("Render task creation failed.");
  Serial.printf("READY: %dx%d, target %d fps, atlas=%u bytes, %d source-derived poses\n",
                kDisplaySize, kDisplaySize, kTargetFps, kAtlasDataSize, kAtlasFrameCount);
}

void loop() {
  static uint64_t lastReport = esp_timer_get_time();
  static uint64_t renderTotal = 0, transferTotal = 0;
  static uint32_t frameCount = 0, maxRender = 0, fadeFrame = 0;
  Frame* frame;
  if (xQueueReceive(readyFrames, &frame, pdMS_TO_TICKS(3000)) != pdTRUE) fatal("Renderer stalled.");
  const int64_t start = esp_timer_get_time();
  display.startWrite();
  display.writeAddrWindow(kFrameX, kFrameY, kFrameWidth, kFrameHeight);
  const auto* bytes = reinterpret_cast<const uint8_t*>(frame->pixels);
  constexpr size_t frameBytes = kFrameWidth * kFrameHeight * 2;
  for (size_t offset = 0; offset < frameBytes; offset += kTransferBytes) {
    const size_t count = std::min(kTransferBytes, frameBytes - offset);
    std::memcpy(transferBuffer, bytes + offset, count);
    displayBus.writeBytes(transferBuffer, count);
  }
  display.endWrite();
  const uint32_t transferUs = esp_timer_get_time() - start;
  if (fadeFrame <= 40) {
    display.setBrightness(static_cast<uint8_t>(kBrightness * smoother(fadeFrame / 40.0f)));
    ++fadeFrame;
  }
  if (Serial.available() && Serial.read() == 's') captureFrame(*frame);
  renderTotal += frame->renderUs;
  transferTotal += transferUs;
  maxRender = std::max(maxRender, frame->renderUs);
  ++frameCount;
  const auto timing = *frame;
  xQueueSend(freeFrames, &frame, portMAX_DELAY);
  const uint64_t now = esp_timer_get_time();
  if (now - lastReport >= 5000000) {
    if (Serial) {
      Serial.printf("PERF fps=%.1f render=%.2fms transfer=%.2fms max_render=%.2fms free_psram=%u\n",
                    frameCount * 1000000.0 / (now - lastReport),
                    renderTotal / (1000.0 * frameCount), transferTotal / (1000.0 * frameCount),
                    maxRender / 1000.0, ESP.getFreePsram());
      Serial.printf("STAGES decode=%uus composite=%uus eyes=%uus\n",
                    timing.decodeUs, timing.compositeUs, timing.eyesUs);
    }
    frameCount = maxRender = 0;
    renderTotal = transferTotal = 0;
    lastReport = now;
  }
}
