#pragma once
#include <cstdint>
#include "SoundLevel.h"

namespace copilot {
enum class SettingsAction : uint8_t {
  None,
  BrightnessDown,
  BrightnessUp,
  SoundOff,
  SoundQuiet,
  SoundNormal,
  Idle,
  Surprise,
  Working,
  Complete,
  Attention,
  Close
};

class SettingsMenu {
 public:
  explicit SettingsMenu(uint8_t brightness, SoundLevel soundLevel = SoundLevel::Quiet)
      : brightness_(brightness), soundLevel_(soundLevel) {}

  bool isOpen() const { return open_; }
  void open() { open_ = true; }
  void close() { open_ = false; }
  uint8_t brightness() const { return brightness_; }
  SoundLevel soundLevel() const { return soundLevel_; }
  void setSoundLevel(SoundLevel soundLevel) { soundLevel_ = soundLevel; }

  SettingsAction tap(int16_t x, int16_t y) {
    if (!open_) return SettingsAction::None;
    if (inside(x, y, 150, 108, 48, 38)) {
      brightness_ = brightness_ > 55 ? brightness_ - 25 : 30;
      return SettingsAction::BrightnessDown;
    }
    if (inside(x, y, 268, 108, 48, 38)) {
      brightness_ = brightness_ < 230 ? brightness_ + 25 : 255;
      return SettingsAction::BrightnessUp;
    }
    if (inside(x, y, 86, 162, 90, 38)) {
      soundLevel_ = SoundLevel::Off;
      return SettingsAction::SoundOff;
    }
    if (inside(x, y, 184, 162, 98, 38)) {
      soundLevel_ = SoundLevel::Quiet;
      return SettingsAction::SoundQuiet;
    }
    if (inside(x, y, 290, 162, 90, 38)) {
      soundLevel_ = SoundLevel::Normal;
      return SettingsAction::SoundNormal;
    }
    if (inside(x, y, 58, 230, 165, 42)) return SettingsAction::Idle;
    if (inside(x, y, 243, 230, 165, 42)) return SettingsAction::Working;
    if (inside(x, y, 58, 282, 165, 42)) return SettingsAction::Complete;
    if (inside(x, y, 243, 282, 165, 42)) return SettingsAction::Attention;
    if (inside(x, y, 58, 334, 165, 42)) return SettingsAction::Surprise;
    if (inside(x, y, 243, 334, 165, 42)) return SettingsAction::Close;
    return SettingsAction::None;
  }

 private:
  static bool inside(int16_t x, int16_t y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
  }

  uint8_t brightness_;
  SoundLevel soundLevel_;
  bool open_ = false;
};
}
