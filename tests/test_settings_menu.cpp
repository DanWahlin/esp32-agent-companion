#include "../firmware/Copilot/src/SettingsMenu.h"
#include <cassert>
#include <iostream>

int main() {
  using copilot::SettingsAction;
  copilot::SettingsMenu menu(155);
  assert(!menu.isOpen());
  assert(menu.tap(100, 230) == SettingsAction::None);
  menu.open();
  assert(menu.isOpen());
  assert(menu.soundVolume() == 50);
  assert(!menu.tiltEnabled());
  assert(menu.tap(170, 130) == SettingsAction::BrightnessDown);
  assert(menu.brightness() == 130);
  for (int i = 0; i < 10; ++i) menu.tap(170, 130);
  assert(menu.brightness() == 30);
  for (int i = 0; i < 20; ++i) menu.tap(290, 130);
  assert(menu.brightness() == 255);
  assert(menu.tap(170, 180) == SettingsAction::SoundDown);
  assert(menu.soundVolume() == 25);
  assert(menu.tap(170, 180) == SettingsAction::SoundDown);
  assert(menu.soundVolume() == 0);
  assert(menu.tap(170, 180) == SettingsAction::SoundDown);
  assert(menu.soundVolume() == 0);
  for (int i = 0; i < 5; ++i) {
    assert(menu.tap(290, 180) == SettingsAction::SoundUp);
  }
  assert(menu.soundVolume() == 100);
  menu.setSoundVolume(255);
  assert(menu.soundVolume() == 100);
  assert(menu.tap(100, 248) == SettingsAction::CharacterCopilot);
  assert(menu.tap(300, 248) == SettingsAction::CharacterOpenClaw);
  assert(menu.tap(300, 285) == SettingsAction::ToggleTilt);
  assert(menu.tiltEnabled());
  assert(menu.tap(300, 285) == SettingsAction::ToggleTilt);
  assert(!menu.tiltEnabled());
  menu.setTiltEnabled(true);
  assert(menu.tiltEnabled());
  assert(menu.tap(100, 338) == SettingsAction::Idle);
  assert(menu.tap(300, 338) == SettingsAction::Working);
  assert(menu.tap(100, 368) == SettingsAction::Complete);
  assert(menu.tap(300, 368) == SettingsAction::Attention);
  assert(menu.tap(100, 398) == SettingsAction::Surprise);
  assert(menu.tap(300, 398) == SettingsAction::Close);
  assert(menu.tap(230, 230) == SettingsAction::None);
  menu.close();
  assert(!menu.isOpen());
  std::cout << "PASS: settings geometry, brightness, sound, tilt, modes and close action\n";
}
