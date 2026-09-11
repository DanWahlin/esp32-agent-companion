#pragma once

namespace copilot {
constexpr int kDisplaySize = 466;
constexpr int kFrameWidth = 400;
constexpr int kFrameHeight = 352;
constexpr int kFrameX = (kDisplaySize - kFrameWidth) / 2;
constexpr int kFrameY = (kDisplaySize - kFrameHeight) / 2;
constexpr int kTargetFps = 30;
constexpr int kBrightness = 155;
constexpr int kSpiFrequency = 80000000;
constexpr float kHeadDelay = 0.09f;
constexpr float kMoveMin = 1.15f;
constexpr float kMoveMax = 1.65f;
constexpr float kHoldMin = 0.85f;
constexpr float kHoldMax = 2.8f;
constexpr float kBlinkMin = 2.6f;
constexpr float kBlinkMax = 6.2f;
constexpr float kBlinkClose = 0.065f;
constexpr float kBlinkHold = 0.028f;
constexpr float kBlinkOpen = 0.125f;
constexpr float kBlinkDuration = kBlinkClose + kBlinkHold + kBlinkOpen;
static_assert(kTargetFps > 0 && kTargetFps <= 60, "Frame rate must be between 1 and 60.");
static_assert(kBrightness >= 0 && kBrightness <= 255, "Brightness must be between 0 and 255.");
}
