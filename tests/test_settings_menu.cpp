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
  assert(menu.tap(100, 250) == SettingsAction::Idle);
  assert(menu.tap(300, 250) == SettingsAction::Working);
  assert(menu.tap(100, 300) == SettingsAction::Complete);
  assert(menu.tap(300, 300) == SettingsAction::Attention);
  assert(menu.tap(100, 350) == SettingsAction::Surprise);
  assert(menu.tap(300, 350) == SettingsAction::Close);
  assert(menu.tap(230, 230) == SettingsAction::None);
  menu.close();
  assert(!menu.isOpen());
  std::cout << "PASS: settings geometry, brightness, sound, modes and close action\n";
}
