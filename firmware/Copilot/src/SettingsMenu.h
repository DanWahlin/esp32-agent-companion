#pragma once
#include <cstdint>

namespace copilot {
enum class SettingsAction : uint8_t {
  None,
  BrightnessDown,
  BrightnessUp,
  SoundDown,
  SoundUp,
  ToggleTilt,
  CharacterCopilot,
  CharacterOpenClaw,
  Idle,
  Surprise,
  Working,
  Complete,
  Attention,
  Close
};

class SettingsMenu {
 public:
  explicit SettingsMenu(uint8_t brightness, uint8_t soundVolume = 50,
                        bool tiltEnabled = false)
      : brightness_(brightness), soundVolume_(soundVolume), tiltEnabled_(tiltEnabled) {}

  bool isOpen() const { return open_; }
  void open() { open_ = true; }
  void close() { open_ = false; }
  uint8_t brightness() const { return brightness_; }
  uint8_t soundVolume() const { return soundVolume_; }
  bool tiltEnabled() const { return tiltEnabled_; }
  void setSoundVolume(uint8_t soundVolume) {
    soundVolume_ = soundVolume > 100 ? 100 : soundVolume;
  }
  void setTiltEnabled(bool enabled) { tiltEnabled_ = enabled; }

  SettingsAction tap(int16_t x, int16_t y) {
    if (!open_) return SettingsAction::None;
    if (inside(x, y, 150, 116, 48, 38)) {
      brightness_ = brightness_ > 55 ? brightness_ - 25 : 30;
      return SettingsAction::BrightnessDown;
    }
    if (inside(x, y, 268, 116, 48, 38)) {
      brightness_ = brightness_ < 230 ? brightness_ + 25 : 255;
      return SettingsAction::BrightnessUp;
    }
    if (inside(x, y, 150, 180, 48, 38)) {
      soundVolume_ = soundVolume_ > 25 ? soundVolume_ - 25 : 0;
      return SettingsAction::SoundDown;
    }
    if (inside(x, y, 268, 180, 48, 38)) {
      soundVolume_ = soundVolume_ < 75 ? soundVolume_ + 25 : 100;
      return SettingsAction::SoundUp;
    }
    if (inside(x, y, 58, 232, 165, 32)) return SettingsAction::CharacterCopilot;
    if (inside(x, y, 243, 232, 165, 32)) return SettingsAction::CharacterOpenClaw;
    if (inside(x, y, 268, 270, 140, 30)) {
      tiltEnabled_ = !tiltEnabled_;
      return SettingsAction::ToggleTilt;
    }
    if (inside(x, y, 58, 324, 165, 28)) return SettingsAction::Idle;
    if (inside(x, y, 243, 324, 165, 28)) return SettingsAction::Working;
    if (inside(x, y, 58, 354, 165, 28)) return SettingsAction::Complete;
    if (inside(x, y, 243, 354, 165, 28)) return SettingsAction::Attention;
    if (inside(x, y, 58, 384, 165, 28)) return SettingsAction::Surprise;
    if (inside(x, y, 243, 384, 165, 28)) return SettingsAction::Close;
    return SettingsAction::None;
  }

 private:
  static bool inside(int16_t x, int16_t y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
  }

  uint8_t brightness_;
  uint8_t soundVolume_;
  bool tiltEnabled_;
  bool open_ = false;
};
}
