#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <miniz.h>
#include <algorithm>
#include <cstring>
#include <cstdarg>
#include <atomic>
#include "src/SpriteRenderer.h"
#include "src/SpritePredictor.h"
#include "src/SpriteStorage.h"
#include "src/CharacterMotion.h"
#include "src/AudioPlayer.h"
#include "src/CharacterEffects.h"
#include "src/DeviceCommands.h"
#include "src/TouchInput.h"
#include "src/SettingsMenu.h"
#include "src/Motion.h"

using namespace copilot;

namespace {
Arduino_ESP32QSPI displayBus(12, 38, 4, 5, 6, 7);
Arduino_CO5300 display(&displayBus, 39, 0, 466, 466, 6, 0, 0, 0);
QueueHandle_t freeFrames, readyFrames, commands;
TaskHandle_t renderTask;
SpriteRenderer* renderer;
CharacterEffects* effects;
tinfl_decompressor inflater;
alignas(4) uint8_t inflateHistory[TINFL_LZ_DICT_SIZE];
SpritePredictor spritePredictor;
uint32_t inflateTimeUs = 0, predictTimeUs = 0;
constexpr size_t kTransferBytes = 4096;
uint8_t* transferBuffer;
std::atomic<uint32_t> droppedLogs{0};
uint32_t worstPresentationGap = 0;
bool captureInterrupted = false;
SettingsMenu settings(kBrightness);
constexpr char kPreferencesNamespace[] = "agent-companion";
constexpr char kSoundPreference[] = "sound";
constexpr char kSoundVolumePreference[] = "volume";

struct ModeRequest {
  CharacterMode mode = CharacterMode::Idle;
  bool returnToIdle = false;
};

void logMessage(const char* format, ...) {
  char message[384];
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(message) || !Serial
      || Serial.availableForWrite() < length) {
    ++droppedLogs;
    return;
  }
  if (Serial.write(reinterpret_cast<const uint8_t*>(message), length) != static_cast<size_t>(length)) ++droppedLogs;
}

struct Frame {
  uint16_t* pixels;
  uint32_t renderUs, motionUs, decodeUs, compositeUs, eyesUs, effectsUs, inflateUs, predictUs;
  CharacterState state;
};
Frame frames[2];

[[noreturn]] void fatal(const char* message) {
  for (;;) {
    logMessage("FATAL: %s\n", message);
    delay(2000);
  }
}

void* allocate(size_t bytes, uint32_t capabilities, const char* error) {
  void* result = heap_caps_malloc(bytes, capabilities | MALLOC_CAP_8BIT);
  if (!result) fatal(error);
  return result;
}

bool inflatePose(uint8_t* output, size_t outputSize, const uint8_t* input, size_t inputSize, size_t width, size_t stride) {
  if (!spritePredictor.reset(output, outputSize, width, stride)) return false;
  tinfl_init(&inflater);
  size_t consumed = 0;
  for (;;) {
    size_t inBytes = inputSize - consumed, outBytes = sizeof(inflateHistory);
    const int64_t started = esp_timer_get_time();
    // Keep filtered history intact: inverse prediction writes only to the destination.
    const auto status = tinfl_decompress(&inflater, input + consumed, &inBytes,
        inflateHistory, inflateHistory, &outBytes, TINFL_FLAG_PARSE_ZLIB_HEADER);
    const int64_t inflated = esp_timer_get_time();
    inflateTimeUs += inflated - started;
    consumed += inBytes;
    if (status < TINFL_STATUS_DONE || !spritePredictor.consume(inflateHistory, outBytes)) return false;
    predictTimeUs += esp_timer_get_time() - inflated;
    if (status == TINFL_STATUS_DONE) return consumed == inputSize && spritePredictor.complete();
    if (status != TINFL_STATUS_HAS_MORE_OUTPUT || outBytes != sizeof(inflateHistory)) return false;
  }
}

void animate(void*) {
  CharacterMotion motion(esp_random());
  if (motion.error()) fatal(motion.error());
  int64_t previous = esp_timer_get_time();
  for (;;) {
    Frame* frame;
    xQueueReceive(freeFrames, &frame, portMAX_DELAY);
    const int64_t start = esp_timer_get_time();
    ModeRequest command;
    while (xQueueReceive(commands, &command, 0) == pdTRUE) {
      if (command.mode == CharacterMode::Surprise) {
        if (command.returnToIdle) motion.surpriseToIdle();
        else motion.surprise();
      } else if (!motion.setMode(command.mode)) {
        fatal(motion.error());
      }
      if (motion.error()) fatal(motion.error());
    }
    motion.update((start - previous) / 1000000.0);
    frame->motionUs = esp_timer_get_time() - start;
    previous = start;
    frame->state = motion.state();
    const int64_t restoreStart = esp_timer_get_time();
    if (!effects->restore(frame->pixels)) fatal(effects->error());
    const uint32_t restoreUs = esp_timer_get_time() - restoreStart;
    inflateTimeUs = predictTimeUs = 0;
    if (!renderer->render(frame->state.pose, frame->pixels)) {
      fatal(renderer->error());
    }
    const int64_t effectStart = esp_timer_get_time();
    if (!effects->render(frame->state, frame->pixels)) fatal(effects->error());
    frame->effectsUs = restoreUs + esp_timer_get_time() - effectStart;
    frame->renderUs = esp_timer_get_time() - start;
    frame->decodeUs = renderer->decodeUs;
    frame->compositeUs = renderer->compositeUs;
    frame->eyesUs = renderer->eyesUs;
    frame->inflateUs = inflateTimeUs;
    frame->predictUs = predictTimeUs;
    xQueueSend(readyFrames, &frame, portMAX_DELAY);
  }
}

void waitUntil(int64_t deadline) {
  int64_t remaining = deadline - esp_timer_get_time();
  if (remaining <= 0) return;
  const TickType_t ticks = pdMS_TO_TICKS(remaining / 1000);
  if (ticks > 1) vTaskDelay(ticks - 1);
  remaining = deadline - esp_timer_get_time();
  if (remaining > 0) delayMicroseconds(static_cast<uint32_t>(remaining));
}

bool writeCapture(const uint8_t* data, size_t bytes) {
  size_t sent = 0;
  int64_t progress = esp_timer_get_time();
  while (sent < bytes) {
    const int available = Serial.availableForWrite();
    const size_t count = available > 0
        ? Serial.write(data + sent, std::min<size_t>(available, bytes - sent)) : 0;
    sent += count;
    if (count) progress = esp_timer_get_time();
    else if (esp_timer_get_time() - progress > 5000000) {
      logMessage("\nCAPTURE_ERROR USB write timed out\n");
      return false;
    }
    if (!count) delay(1);
  }
  return true;
}

bool captureFrame(const Frame& frame) {
  captureInterrupted = true;
  constexpr size_t bytes = kCharacterFrameWidth * kCharacterFrameHeight * 2;
  char header[256];
  const int length = snprintf(header, sizeof(header),
      "CAPTURE_POSE direction=%u frame=%u blink=%u\n"
      "CAPTURE_STATE mode=%u requested=%u seconds=%.9g event=%u\n"
      "FRAME_BE %d %d %u\n",
      static_cast<unsigned>(frame.state.pose.direction), static_cast<unsigned>(frame.state.pose.index),
      static_cast<unsigned>(frame.state.pose.blinkLevel), static_cast<unsigned>(frame.state.mode),
      static_cast<unsigned>(frame.state.requestedMode), frame.state.effectSeconds, frame.state.eventId,
      kCharacterFrameWidth, kCharacterFrameHeight, static_cast<unsigned>(bytes));
  if (length < 0 || static_cast<size_t>(length) >= sizeof(header)) {
    logMessage("CAPTURE_ERROR header formatting failed\n");
    return false;
  }
  const char end[] = "\nEND_FRAME\n";
  return writeCapture(reinterpret_cast<const uint8_t*>(header), length)
      && writeCapture(reinterpret_cast<const uint8_t*>(frame.pixels), bytes)
      && writeCapture(reinterpret_cast<const uint8_t*>(end), sizeof(end) - 1);
}

const char* modeName(CharacterMode mode) {
  switch (mode) {
    case CharacterMode::Idle: return "idle";
    case CharacterMode::Surprise: return "surprise";
    case CharacterMode::Working: return "working";
    case CharacterMode::Complete: return "complete";
    case CharacterMode::Attention: return "attention";
  }
  return "invalid";
}

void queueMode(DeviceCommand command) {
  ModeRequest request;
  switch (command) {
    case DeviceCommand::Idle: request.mode = CharacterMode::Idle; break;
    case DeviceCommand::Surprise: request.mode = CharacterMode::Surprise; break;
    case DeviceCommand::Working: request.mode = CharacterMode::Working; break;
    case DeviceCommand::Complete: request.mode = CharacterMode::Complete; break;
    case DeviceCommand::Attention: request.mode = CharacterMode::Attention; break;
    default: logMessage("COMMAND_ERROR unknown mode\n"); return;
  }
  if (xQueueSend(commands, &request, 0) != pdTRUE) {
    logMessage("COMMAND_ERROR mode queue full\n");
    return;
  }
  logMessage("COMMAND accepted=%s\n", commandName(command));
}

void queueTouchSurprise() {
  const ModeRequest request{CharacterMode::Surprise, true};
  if (xQueueSend(commands, &request, 0) != pdTRUE) {
    logMessage("COMMAND_ERROR mode queue full\n");
    return;
  }
  logMessage("COMMAND accepted=surprise source=touch return=idle\n");
}

uint8_t loadSoundVolume() {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    logMessage("SETTINGS_ERROR sound preference open failed\n");
    return kDefaultSoundVolume;
  }
  uint8_t stored;
  if (preferences.isKey(kSoundVolumePreference)) {
    stored = preferences.getUChar(kSoundVolumePreference, kDefaultSoundVolume);
  } else {
    const uint8_t legacy = preferences.getUChar(kSoundPreference, 1);
    stored = legacy == 0 ? 0 : legacy == 2 ? 100 : kDefaultSoundVolume;
    if (preferences.putUChar(kSoundVolumePreference, stored) != 1)
      logMessage("SETTINGS_ERROR sound preference migration failed\n");
  }
  preferences.end();
  if (stored > 100) {
    logMessage("SETTINGS_ERROR invalid sound volume=%u\n", static_cast<unsigned>(stored));
    return kDefaultSoundVolume;
  }
  return stored;
}

void saveSoundVolume(uint8_t volume) {
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    logMessage("SETTINGS_ERROR sound preference open failed\n");
    return;
  }
  if (preferences.putUChar(kSoundVolumePreference, volume) != 1)
    logMessage("SETTINGS_ERROR sound preference write failed\n");
  preferences.end();
}

void queueModeCue(CharacterMode mode) {
  switch (mode) {
    case CharacterMode::Working: queueAudioCue(AudioCue::Working); break;
    case CharacterMode::Attention: queueAudioCue(AudioCue::Attention); break;
    case CharacterMode::Complete: queueAudioCue(AudioCue::Complete); break;
    case CharacterMode::Surprise: queueAudioCue(AudioCue::Surprise); break;
    case CharacterMode::Idle: break;
  }
}

void drawSettingsButton(int x, int y, int width, const char* label, bool selected,
                        uint8_t textSize = 2, int height = 48) {
  constexpr uint16_t border = 0x5D19;
  constexpr uint16_t normal = 0x18E7;
  constexpr uint16_t active = 0x2372;
  constexpr uint16_t text = 0xE73F;
  display.fillRoundRect(x, y, width, height, 10, selected ? active : normal);
  display.drawRoundRect(x, y, width, height, 10, selected ? 0x867F : border);
  display.setTextColor(text);
  display.setTextSize(textSize);
  const int characterWidth = 6 * textSize;
  display.setCursor(x + std::max(10, (width - static_cast<int>(std::strlen(label)) * characterWidth) / 2),
                    y + (height - 8 * textSize) / 2);
  display.print(label);
}

void drawSettingsTitle(uint16_t color) {
  constexpr char title[] = "Settings";
  constexpr int y[] = {62, 53, 47, 44, 44, 47, 53, 62};
  display.setTextColor(color);
  display.setTextSize(3);
  for (unsigned i = 0; i < sizeof(title) - 1; ++i) {
    display.setCursor(153 + static_cast<int>(i) * 20, y[i]);
    display.print(title[i]);
  }
}

void drawSettingsMenu(CharacterMode selected) {
  constexpr uint16_t background = 0x0842;
  constexpr uint16_t panel = 0x10A5;
  constexpr uint16_t text = 0xE73F;
  constexpr uint16_t muted = 0x8C71;
  display.fillScreen(0);
  drawSettingsTitle(text);
  display.fillRoundRect(38, 92, 390, 312, 28, panel);
  display.drawRoundRect(38, 92, 390, 312, 28, 0x31CC);
  display.setTextColor(muted);
  display.setTextSize(1);
  display.setCursor(203, 99);
  display.print("Brightness");
  drawSettingsButton(150, 108, 48, "-", false, 3, 38);
  drawSettingsButton(268, 108, 48, "+", false, 3, 38);
  display.fillRoundRect(204, 108, 58, 38, 10, background);
  display.setTextColor(text);
  display.setTextSize(2);
  char brightness[8];
  snprintf(brightness, sizeof(brightness), "%u%%",
           static_cast<unsigned>((settings.brightness() * 100 + 127) / 255));
  display.setCursor(211, 119);
  display.print(brightness);
  display.setTextColor(muted);
  display.setTextSize(1);
  display.setCursor(218, 151);
  display.print("Sound");
  drawSettingsButton(150, 162, 48, "-", false, 3, 38);
  drawSettingsButton(268, 162, 48, "+", false, 3, 38);
  display.fillRoundRect(204, 162, 58, 38, 10, background);
  display.setTextColor(text);
  display.setTextSize(2);
  char volume[8];
  if (settings.soundVolume() == 0) {
    snprintf(volume, sizeof(volume), "Off");
    display.setCursor(215, 173);
  } else {
    snprintf(volume, sizeof(volume), "%u%%",
             static_cast<unsigned>(settings.soundVolume()));
    display.setCursor(211, 173);
  }
  display.print(volume);
  display.setTextColor(text);
  display.setTextSize(2);
  display.setCursor(58, 207);
  display.print("Character state");
  drawSettingsButton(58, 230, 165, "Idle", selected == CharacterMode::Idle, 2, 42);
  drawSettingsButton(243, 230, 165, "Working", selected == CharacterMode::Working, 2, 42);
  drawSettingsButton(58, 282, 165, "Complete", selected == CharacterMode::Complete, 2, 42);
  drawSettingsButton(243, 282, 165, "Needs attention",
                     selected == CharacterMode::Attention, 1, 42);
  drawSettingsButton(58, 334, 165, "Surprise", selected == CharacterMode::Surprise, 2, 42);
  drawSettingsButton(243, 334, 165, "Close", false, 2, 42);
}

void clearCharacterMargins() {
  display.fillRect(0, 0, kCharacterFrameX, kDisplaySize, 0);
  display.fillRect(kCharacterFrameX + kCharacterFrameWidth, 0,
                   kDisplaySize - kCharacterFrameX - kCharacterFrameWidth, kDisplaySize, 0);
}

void handleTouchGesture(const TouchGesture& gesture, const Frame& frame) {
  if (gesture.kind == TouchGestureKind::SwipeUp && !settings.isOpen()) {
    settings.open();
    queueAudioCue(AudioCue::Settings);
    drawSettingsMenu(frame.state.requestedMode);
    logMessage("SETTINGS opened\n");
    return;
  }
  if (gesture.kind == TouchGestureKind::SwipeDown && settings.isOpen()) {
    settings.close();
    queueAudioCue(AudioCue::Settings);
    clearCharacterMargins();
    logMessage("SETTINGS closed=swipe\n");
    return;
  }
  if (gesture.kind != TouchGestureKind::Tap) return;
  if (!settings.isOpen()) {
    const int x = gesture.endX - kCharacterFrameX, y = gesture.endY;
    if (x >= 0 && y >= 0 && x < kCharacterFrameWidth && y < kCharacterFrameHeight
        && frame.pixels[y * kCharacterFrameWidth + x] != 0) {
      logMessage("TOUCH tap x=%d y=%d\n", gesture.endX, gesture.endY);
      queueTouchSurprise();
    }
    return;
  }
  const SettingsAction action = settings.tap(gesture.endX, gesture.endY);
  switch (action) {
    case SettingsAction::BrightnessDown:
    case SettingsAction::BrightnessUp:
      display.setBrightness(settings.brightness());
      queueAudioCue(AudioCue::Settings);
      drawSettingsMenu(frame.state.requestedMode);
      logMessage("SETTINGS brightness=%u\n", static_cast<unsigned>(settings.brightness()));
      break;
    case SettingsAction::SoundDown:
    case SettingsAction::SoundUp:
      setSoundVolume(settings.soundVolume());
      saveSoundVolume(settings.soundVolume());
      queueAudioCue(AudioCue::Settings);
      drawSettingsMenu(frame.state.requestedMode);
      logMessage("SETTINGS sound_volume=%u\n",
                 static_cast<unsigned>(settings.soundVolume()));
      break;
    case SettingsAction::Idle:
      settings.close(); clearCharacterMargins(); queueMode(DeviceCommand::Idle); break;
    case SettingsAction::Surprise:
      settings.close(); clearCharacterMargins(); queueMode(DeviceCommand::Surprise); break;
    case SettingsAction::Working:
      settings.close(); clearCharacterMargins(); queueMode(DeviceCommand::Working); break;
    case SettingsAction::Complete:
      settings.close(); clearCharacterMargins(); queueMode(DeviceCommand::Complete); break;
    case SettingsAction::Attention:
      settings.close(); clearCharacterMargins(); queueMode(DeviceCommand::Attention); break;
    case SettingsAction::Close:
      settings.close();
      queueAudioCue(AudioCue::Settings);
      clearCharacterMargins();
      logMessage("SETTINGS closed=button\n");
      break;
    case SettingsAction::None: break;
  }
}

void logSdStatus(const SdSpriteStatus& status) {
  logMessage("SD state=%s card=%s capacity_bytes=%llu cache_bytes=%u hits=%u misses=%u\n",
             status.state, status.cardType, static_cast<unsigned long long>(status.capacityBytes),
             status.cacheBytes, status.hits, status.misses);
  logMessage("SD_DETAIL %s\n", status.detail);
}

void processCommand(DeviceCommand command, const Frame& frame) {
  switch (command) {
    case DeviceCommand::None: break;
    case DeviceCommand::Invalid: logMessage("COMMAND_ERROR invalid or incomplete packet\n"); break;
    case DeviceCommand::Capture: captureFrame(frame); break;
    case DeviceCommand::Heap:
      if (!heap_caps_check_integrity_all(true)) fatal("Heap integrity check failed.");
      logMessage("HEAP integrity=ok\n");
      break;
    case DeviceCommand::Info:
      logMessage("INFO protocol=%u uptime_ms=%llu reset_reason=%u mode=%s requested=%s assets=%u "
                 "max_gap_us=%u dropped_logs=%u audio_ready=%u sound_volume=%u\n",
                    kDeviceProtocol, static_cast<unsigned long long>(esp_timer_get_time() / 1000),
                    static_cast<unsigned>(esp_reset_reason()), modeName(frame.state.mode),
                    modeName(frame.state.requestedMode), kSpriteDataSize, worstPresentationGap,
                    droppedLogs.load(std::memory_order_relaxed), static_cast<unsigned>(audioReady()),
                    static_cast<unsigned>(soundVolume()));
      logSdStatus(sdSpriteStatus());
      break;
    default: queueMode(command); break;
  }
}
}

void setup() {
  static_assert(kSpriteDirections == 13, "Export eight idle tracks and five expression tracks, including alternate attention.");
  const bool serialBufferReady = Serial.setTxBufferSize(2048) == 2048;
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  if (!serialBufferReady) fatal("USB transmit buffer allocation failed.");
  Serial.setDebugOutput(true);
  if (!psramFound()) fatal("8 MB OPI PSRAM not detected.");
  logMessage("\nCopilot generated sprites / Waveshare AMOLED 1.75-B\nPSRAM: %u bytes\n", ESP.getPsramSize());
  if (!display.begin(kSpiFrequency)) fatal("CO5300 initialization failed.");
  display.setBrightness(0);
  display.fillScreen(0);
  if (!initializeSpriteStorage()) fatal(spriteStorageError());
  if (!initializeTouchInput()) fatal(touchInputError());
  const uint8_t storedSoundVolume = loadSoundVolume();
  settings.setSoundVolume(storedSoundVolume);
  setSoundVolume(storedSoundVolume);
  beginAudio();
  transferBuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      16, kTransferBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!transferBuffer) fatal("DMA staging allocation failed.");
  constexpr size_t patchBytes = std::max<size_t>(1, kSpriteMaxPatchPixels) * sizeof(uint16_t);
  auto* firstOpenPatch = static_cast<uint16_t*>(allocate(patchBytes, MALLOC_CAP_INTERNAL,
      "First open-eye cache SRAM allocation failed."));
  auto* secondOpenPatch = static_cast<uint16_t*>(allocate(patchBytes, MALLOC_CAP_INTERNAL,
      "Second open-eye cache SRAM allocation failed."));
  auto* patch = static_cast<uint16_t*>(allocate(patchBytes, MALLOC_CAP_INTERNAL,
      "Blink patch SRAM allocation failed."));
  void* memory = allocate(sizeof(SpriteRenderer), MALLOC_CAP_INTERNAL, "Renderer allocation failed.");
  freeFrames = xQueueCreate(2, sizeof(Frame*));
  readyFrames = xQueueCreate(2, sizeof(Frame*));
  commands = xQueueCreate(8, sizeof(ModeRequest));
  if (!freeFrames || !readyFrames || !commands) fatal("Frame or command queue allocation failed.");
  for (auto& frame : frames) {
    frame.pixels = static_cast<uint16_t*>(allocate(kCharacterFrameWidth * kCharacterFrameHeight * 2,
        MALLOC_CAP_SPIRAM, "Framebuffer PSRAM allocation failed."));
    std::memset(frame.pixels, 0, kCharacterFrameWidth * kCharacterFrameHeight * 2);
    Frame* pointer = &frame;
    xQueueSend(freeFrames, &pointer, portMAX_DELAY);
  }
  renderer = new (memory) SpriteRenderer(firstOpenPatch, secondOpenPatch, patch,
                                        frames[0].pixels, frames[1].pixels, inflatePose,
                                        kCharacterFrameWidth, kCharacterFrameHeight);
  void* effectMemory = allocate(sizeof(CharacterEffects), MALLOC_CAP_INTERNAL, "Effects allocation failed.");
  effects = new (effectMemory) CharacterEffects(frames[0].pixels, frames[1].pixels);
  // Warm both frame caches before starting the presentation clock and brightness fade.
  for (auto& frame : frames)
    if (!renderer->render({0, 0, 0}, frame.pixels)) fatal(renderer->error());
  initializeSdSpriteStorage();
  if (xTaskCreatePinnedToCore(animate, "copilot-render", 16384, nullptr, 1,
                              &renderTask, 0) != pdPASS) fatal("Render task creation failed.");
  logMessage("READY: %dx%d, target %d fps, sprites=%u bytes, %d poses, %d tracks\n",
                kDisplaySize, kDisplaySize, kTargetFps, kSpriteDataSize, kSpriteFrameCount, kSpriteDirections);
}

void loop() {
  static uint64_t lastReport = esp_timer_get_time();
  static uint64_t renderTotal = 0, transferTotal = 0;
  static uint32_t frameCount = 0, maxRender = 0, fadeFrame = 0;
  static int64_t nextPresentation = 0, previousPresentation = 0;
  static uint32_t minGap = UINT32_MAX, maxGap = 0;
  static DeviceCommands commandParser;
  static uint32_t previousEvent = UINT32_MAX;
  static CharacterMode previousMode = CharacterMode::Idle;
  Frame* frame;
  if (xQueueReceive(readyFrames, &frame, pdMS_TO_TICKS(3000)) != pdTRUE) fatal("Renderer stalled.");
  static uint32_t sdRevision = UINT32_MAX;
  const auto storage = sdSpriteStatus();
  if (storage.revision != sdRevision) {
    logSdStatus(storage);
    sdRevision = storage.revision;
  }
  // Pace presentation, not decoding: cached frames must not arrive earlier than newly decoded poses.
  waitUntil(nextPresentation);
  const int64_t start = esp_timer_get_time();
  nextPresentation = start + 1000000 / kTargetFps;
  if (previousPresentation) {
    const uint32_t gap = start - previousPresentation;
    minGap = std::min(minGap, gap);
    maxGap = std::max(maxGap, gap);
    worstPresentationGap = std::max(worstPresentationGap, gap);
  }
  previousPresentation = start;
  uint32_t transferUs = 0;
  if (!settings.isOpen()) {
    display.startWrite();
    display.writeAddrWindow(kCharacterFrameX, 0, kCharacterFrameWidth, kCharacterFrameHeight);
    const auto* bytes = reinterpret_cast<const uint8_t*>(frame->pixels);
    constexpr size_t frameBytes = kCharacterFrameWidth * kCharacterFrameHeight * 2;
    for (size_t offset = 0; offset < frameBytes; offset += kTransferBytes) {
      const size_t count = std::min(kTransferBytes, frameBytes - offset);
      std::memcpy(transferBuffer, bytes + offset, count);
      displayBus.writeBytes(transferBuffer, count);
    }
    display.endWrite();
    transferUs = esp_timer_get_time() - start;
  }
  if (fadeFrame <= 40) {
    display.setBrightness(static_cast<uint8_t>(settings.brightness() * smoother(fadeFrame / 40.0f)));
    ++fadeFrame;
  }
  TouchGesture gesture;
  if (pollTouchGesture(gesture)) handleTouchGesture(gesture, *frame);
  if (touchInputError()) fatal(touchInputError());
  processCommand(commandParser.expire(esp_timer_get_time() / 1000), *frame);
  for (unsigned read = 0; read < 8 && Serial.available(); ++read) {
    processCommand(commandParser.feed(static_cast<char>(Serial.read()), esp_timer_get_time() / 1000), *frame);
  }
  if (frame->state.eventId != previousEvent || frame->state.mode != previousMode) {
    logMessage("STATE mode=%s requested=%s event=%u\n", modeName(frame->state.mode),
                  modeName(frame->state.requestedMode), frame->state.eventId);
    if (frame->state.mode != previousMode) queueModeCue(frame->state.mode);
    previousEvent = frame->state.eventId;
    previousMode = frame->state.mode;
  }
  if (captureInterrupted) {
    previousPresentation = nextPresentation = 0;
    captureInterrupted = false;
  }
  renderTotal += frame->renderUs;
  transferTotal += transferUs;
  maxRender = std::max(maxRender, frame->renderUs);
  ++frameCount;
  const auto timing = *frame;
  xQueueSend(freeFrames, &frame, portMAX_DELAY);
  const uint64_t now = esp_timer_get_time();
  if (now - lastReport >= 5000000) {
    if (Serial) {
      logMessage("PERF fps=%.1f render=%.2fms transfer=%.2fms max_render=%.2fms free_psram=%u dropped_logs=%u\n",
                    frameCount * 1000000.0 / (now - lastReport),
                    renderTotal / (1000.0 * frameCount), transferTotal / (1000.0 * frameCount),
                    maxRender / 1000.0, ESP.getFreePsram(), droppedLogs.load(std::memory_order_relaxed));
      logMessage("STAGES motion=%uus decode=%uus composite=%uus eyes=%uus effects=%uus inflate=%uus predict=%uus\n",
                    timing.motionUs, timing.decodeUs, timing.compositeUs, timing.eyesUs, timing.effectsUs,
                    timing.inflateUs, timing.predictUs);
      logMessage("PACING min_gap_us=%u max_gap_us=%u\n",
                    minGap == UINT32_MAX ? 0 : minGap, maxGap);
      constexpr uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
      // ESP-IDF reports stack high-water marks in bytes, unlike vanilla FreeRTOS.
      logMessage("MEM free_internal=%u min_internal=%u largest_internal=%u free_psram=%u "
                    "render_stack_free=%u display_stack_free=%u\n",
                    static_cast<unsigned>(heap_caps_get_free_size(internalCaps)),
                    static_cast<unsigned>(heap_caps_get_minimum_free_size(internalCaps)),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(internalCaps)), ESP.getFreePsram(),
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(renderTask)),
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      logMessage("POSE direction=%u frame=%u blink=%u\n", static_cast<unsigned>(timing.state.pose.direction),
                    static_cast<unsigned>(timing.state.pose.index), static_cast<unsigned>(timing.state.pose.blinkLevel));
    }
    frameCount = maxRender = 0;
    minGap = UINT32_MAX;
    maxGap = 0;
    renderTotal = transferTotal = 0;
    lastReport = now;
  }
}
