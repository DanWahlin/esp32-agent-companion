#include "../firmware/Copilot/src/TiltLook.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using copilot::TiltLook;

static TiltLook calibrated(float x = 0.2f, float y = -0.3f) {
  TiltLook tilt;
  for (unsigned i = 0; i < TiltLook::kCalibrationSamples; ++i)
    assert(tilt.addCalibrationSample(x, y));
  assert(tilt.calibrated());
  assert(std::abs(tilt.biasX() - x) < 1e-5f);
  assert(std::abs(tilt.biasY() - y) < 1e-5f);
  return tilt;
}

static auto settle(TiltLook& tilt, float x, float y, int samples = 30) {
  copilot::TiltLookState state;
  for (int i = 0; i < samples; ++i) state = tilt.update(x, y, 1.0f / 30);
  return state;
}

int main() {
  {
    auto tilt = calibrated();
    assert(!settle(tilt, 0.2f, -0.3f).active);
    assert(settle(tilt, 0.2f, -0.8f).direction == 1);  // Device right -> face left
    tilt.resetCalibration();
    assert(!tilt.calibrated());
    assert(tilt.biasX() == 0 && tilt.biasY() == 0);
    for (unsigned i = 0; i < TiltLook::kCalibrationSamples; ++i)
      assert(tilt.addCalibrationSample(-0.4f, 0.7f));
    assert(std::abs(tilt.biasX() + 0.4f) < 1e-5f);
    assert(std::abs(tilt.biasY() - 0.7f) < 1e-5f);
  }
  {
    auto tilt = calibrated(0, 0);
    assert(settle(tilt, 0.6f, 0).direction == 2);   // Device down -> face up
    assert(settle(tilt, -0.6f, 0).direction == 3);  // Device up -> face down
  }
  {
    auto tilt = calibrated(0, 0);
    assert(settle(tilt, 0.5f, -0.5f).direction == 5);
    assert(settle(tilt, -0.5f, 0.5f).direction == 6);
  }
  {
    auto tilt = calibrated(0, 0);
    assert(settle(tilt, 0, -0.4f).active);
    assert(settle(tilt, 0, -0.09f, 1).active);
    assert(!settle(tilt, 0, 0, 30).active);
    const auto invalid = tilt.update(std::numeric_limits<float>::quiet_NaN(), 0, 1.0f / 30);
    assert(!invalid.active);
  }
  std::cout << "PASS: calibrated counter-tilt mapping, smoothing and hysteresis\n";
}
