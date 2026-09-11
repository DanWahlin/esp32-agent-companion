#include "../firmware/Copilot/src/TouchInput.h"
#include <cassert>
#include <iostream>

int main() {
  copilot::TouchDebounce touch;
  assert(touch.sample(true, 1));
  for (unsigned time = 2; time < 5000; ++time) assert(!touch.sample(true, time));
  assert(!touch.sample(false, 5000));
  assert(touch.sample(true, 5001));
  assert(!touch.sample(false, 5010));
  assert(!touch.sample(true, 5011));
  assert(!touch.sample(true, 6000));
  assert(!touch.sample(false, 6010));
  assert(touch.sample(true, 6011));
  assert(!touch.sample(false, UINT64_C(5000000000)));
  assert(touch.sample(true, UINT64_C(5000000001)));
  std::cout << "PASS: initial tap, long press, bounce suppression, release and long uptime\n";
}
