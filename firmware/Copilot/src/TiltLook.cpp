#include "TiltLook.h"
#include <algorithm>
#include <cmath>

namespace copilot {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr uint8_t kStableDirectionSamples = 3;
}

void TiltLook::resetCalibration() {
  calibrationSumX_ = calibrationSumY_ = 0;
  biasX_ = biasY_ = 0;
  smoothedX_ = smoothedY_ = magnitude_ = 0;
  calibrationCount_ = 0;
  direction_ = candidateDirection_ = candidateSamples_ = 0;
  active_ = false;
  state_ = {};
}

bool TiltLook::addCalibrationSample(float sensorX, float sensorY) {
  if (calibrated() || !std::isfinite(sensorX) || !std::isfinite(sensorY)) return false;
  calibrationSumX_ += sensorX;
  calibrationSumY_ += sensorY;
  ++calibrationCount_;
  if (calibrated()) {
    biasX_ = calibrationSumX_ / kCalibrationSamples;
    biasY_ = calibrationSumY_ / kCalibrationSamples;
  }
  return true;
}

TiltLookState TiltLook::update(float sensorX, float sensorY, float deltaSeconds) {
  if (!calibrated() || !std::isfinite(sensorX) || !std::isfinite(sensorY)
      || !std::isfinite(deltaSeconds) || deltaSeconds <= 0) {
    return state_;
  }
  const float screenX = sensorY - biasY_;
  const float screenY = -(sensorX - biasX_);
  const float alpha = 1 - std::exp(-std::min(deltaSeconds, 0.1f) / kSmoothingSeconds);
  smoothedX_ += (screenX - smoothedX_) * alpha;
  smoothedY_ += (screenY - smoothedY_) * alpha;
  magnitude_ = std::hypot(smoothedX_, smoothedY_);
  if (active_ ? magnitude_ < kExitThreshold : magnitude_ < kEnterThreshold) {
    active_ = false;
    candidateSamples_ = 0;
    state_ = {};
    return state_;
  }

  const uint8_t candidate = direction(smoothedX_, smoothedY_);
  if (!active_ || candidate != candidateDirection_) {
    candidateDirection_ = candidate;
    candidateSamples_ = 1;
  } else if (candidateSamples_ < kStableDirectionSamples) {
    ++candidateSamples_;
  }
  if (!active_ || candidateSamples_ >= kStableDirectionSamples) {
    direction_ = candidateDirection_;
    active_ = true;
  }
  const float normalized = std::clamp(
      (magnitude_ - kEnterThreshold) / (kFullDepth - kEnterThreshold), 0.0f, 1.0f);
  state_ = {active_, direction_, 0.4f + normalized * 0.6f};
  return state_;
}

uint8_t TiltLook::direction(float screenX, float screenY) {
  int octant = static_cast<int>(std::lround(std::atan2(screenY, screenX) / (kPi / 4)));
  if (octant < 0) octant += 8;
  static constexpr uint8_t tracks[] = {0, 6, 3, 7, 1, 5, 2, 4};
  return tracks[octant % 8];
}
}
