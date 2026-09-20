#pragma once
#include <cstdint>

namespace copilot {
struct TiltLookState {
  bool active = false;
  uint8_t direction = 0;
  float depth = 0;
};

class TiltLook {
 public:
  static constexpr uint16_t kCalibrationSamples = 180;
  static constexpr float kEnterThreshold = 0.12f;
  static constexpr float kExitThreshold = 0.08f;
  static constexpr float kFullDepth = 0.65f;
  static constexpr float kSmoothingSeconds = 0.09f;

  void resetCalibration();
  bool addCalibrationSample(float sensorX, float sensorY);
  bool calibrated() const { return calibrationCount_ == kCalibrationSamples; }
  TiltLookState update(float sensorX, float sensorY, float deltaSeconds);
  float biasX() const { return biasX_; }
  float biasY() const { return biasY_; }
  float screenX() const { return smoothedX_; }
  float screenY() const { return smoothedY_; }
  float magnitude() const { return magnitude_; }
  TiltLookState state() const { return state_; }

 private:
  static uint8_t direction(float screenX, float screenY);
  float calibrationSumX_ = 0;
  float calibrationSumY_ = 0;
  float biasX_ = 0;
  float biasY_ = 0;
  float smoothedX_ = 0;
  float smoothedY_ = 0;
  float magnitude_ = 0;
  uint16_t calibrationCount_ = 0;
  uint8_t direction_ = 0;
  uint8_t candidateDirection_ = 0;
  uint8_t candidateSamples_ = 0;
  bool active_ = false;
  TiltLookState state_;
};
}
