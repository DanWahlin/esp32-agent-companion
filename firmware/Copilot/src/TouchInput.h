#pragma once
#include "Config.h"
#include <cstdint>

namespace copilot {
struct TouchTap { int16_t x, y; };

class TouchDebounce {
 public:
  bool sample(bool pressed, uint64_t milliseconds) {
    const bool rising = pressed && !pressed_;
    pressed_ = pressed;
    if (!rising || (seen_ && milliseconds - previous_ < kTouchDebounceMs)) return false;
    seen_ = true;
    previous_ = milliseconds;
    return true;
  }

 private:
  bool pressed_ = false, seen_ = false;
  uint64_t previous_ = 0;
};

bool initializeTouchInput();
bool pollTouchTap(TouchTap& tap);
const char* touchInputError();
}
