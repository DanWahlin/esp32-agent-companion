#pragma once
#include <cstdint>

namespace copilot {
enum class SettingsAction : uint8_t {
  None, BrightnessDown, BrightnessUp, Idle, Surprise, Working, Complete, Attention, Close
};

class SettingsMenu {
 public:
  explicit SettingsMenu(uint8_t brightness) : brightness_(brightness) {}

  bool isOpen() const { return open_; }
  void open() { open_ = true; }
  void close() { open_ = false; }
  uint8_t brightness() const { return brightness_; }

  SettingsAction tap(int16_t x, int16_t y) {
    if (!open_) return SettingsAction::None;
    if (inside(x, y, 58, 126, 90, 48)) {
      brightness_ = brightness_ > 55 ? brightness_ - 25 : 30;
      return SettingsAction::BrightnessDown;
    }
    if (inside(x, y, 318, 126, 90, 48)) {
      brightness_ = brightness_ < 230 ? brightness_ + 25 : 255;
      return SettingsAction::BrightnessUp;
    }
    if (inside(x, y, 58, 214, 165, 48)) return SettingsAction::Idle;
    if (inside(x, y, 243, 214, 165, 48)) return SettingsAction::Working;
    if (inside(x, y, 58, 274, 165, 48)) return SettingsAction::Complete;
    if (inside(x, y, 243, 274, 165, 48)) return SettingsAction::Attention;
    if (inside(x, y, 58, 334, 165, 48)) return SettingsAction::Surprise;
    if (inside(x, y, 243, 334, 165, 48)) return SettingsAction::Close;
    return SettingsAction::None;
  }

 private:
  static bool inside(int16_t x, int16_t y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
  }

  uint8_t brightness_;
  bool open_ = false;
};
}
