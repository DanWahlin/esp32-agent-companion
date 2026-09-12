#include "../firmware/Copilot/src/SettingsMenu.h"
#include <cassert>
#include <iostream>

int main() {
  using copilot::SettingsAction;
  using copilot::SoundLevel;
  assert(copilot::isValidSoundLevel(0));
  assert(copilot::isValidSoundLevel(2));
  assert(!copilot::isValidSoundLevel(3));
  copilot::SettingsMenu menu(155);
  assert(!menu.isOpen());
  assert(menu.tap(100, 230) == SettingsAction::None);
  menu.open();
  assert(menu.isOpen());
  assert(menu.soundLevel() == SoundLevel::Quiet);
  assert(menu.tap(170, 130) == SettingsAction::BrightnessDown);
  assert(menu.brightness() == 130);
  for (int i = 0; i < 10; ++i) menu.tap(170, 130);
  assert(menu.brightness() == 30);
  for (int i = 0; i < 20; ++i) menu.tap(290, 130);
  assert(menu.brightness() == 255);
  assert(menu.tap(110, 180) == SettingsAction::SoundOff);
  assert(menu.soundLevel() == SoundLevel::Off);
  assert(menu.tap(220, 180) == SettingsAction::SoundQuiet);
  assert(menu.soundLevel() == SoundLevel::Quiet);
  assert(menu.tap(330, 180) == SettingsAction::SoundNormal);
  assert(menu.soundLevel() == SoundLevel::Normal);
  menu.setSoundLevel(SoundLevel::Off);
  assert(menu.soundLevel() == SoundLevel::Off);
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
