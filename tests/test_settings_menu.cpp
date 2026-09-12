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
  assert(menu.tap(100, 150) == SettingsAction::BrightnessDown);
  assert(menu.brightness() == 130);
  for (int i = 0; i < 10; ++i) menu.tap(100, 150);
  assert(menu.brightness() == 30);
  for (int i = 0; i < 20; ++i) menu.tap(350, 150);
  assert(menu.brightness() == 255);
  assert(menu.tap(100, 230) == SettingsAction::Idle);
  assert(menu.tap(300, 230) == SettingsAction::Working);
  assert(menu.tap(100, 290) == SettingsAction::Complete);
  assert(menu.tap(300, 290) == SettingsAction::Attention);
  assert(menu.tap(100, 350) == SettingsAction::Surprise);
  assert(menu.tap(300, 350) == SettingsAction::Close);
  assert(menu.tap(230, 230) == SettingsAction::None);
  menu.close();
  assert(!menu.isOpen());
  std::cout << "PASS: settings geometry, brightness bounds, modes and close action\n";
}
