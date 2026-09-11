#include "CharacterMotion.h"
#include "../generated/sprite_assets.h"
#include <algorithm>
#include <cmath>

namespace copilot {
namespace {
double inverse(double value) {
  if (value <= 0 || value >= 1) return value;
  double low = 0, high = 1;
  for (int i = 0; i < 30; ++i) {
    const double t = (low + high) / 2;
    if (t * t * t * (t * (6 * t - 15) + 10) < value) low = t;
    else high = t;
  }
  return (low + high) / 2;
}
bool latched(CharacterMode mode) {
  return mode == CharacterMode::Working || mode == CharacterMode::Attention;
}
bool preservesExpression(CharacterMode mode) {
  return mode == CharacterMode::Surprise || mode == CharacterMode::Complete;
}
}

CharacterMotion::CharacterMotion(uint32_t seed)
    : CharacterMotion(seed, kSpriteSteps == 24 && kSpriteBlinkLevels == 5 ? kSpriteDirections : 0) {}

CharacterMotion::CharacterMotion(uint32_t seed, uint8_t availableDirections)
    : idle_(seed), available_(availableDirections), random_(seed ? seed : 0x6d2b79f5u) {
  if (available_ < 8) error_ = "Character assets require eight idle tracks, 24 poses and five blink levels.";
}

double CharacterMotion::range(double low, double high) {
  random_ ^= random_ << 13;
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  return low + double(random_) / 4294967296.0 * (high - low);
}

bool CharacterMotion::setMode(CharacterMode mode) {
  if (static_cast<uint8_t>(mode) > static_cast<uint8_t>(CharacterMode::Attention)) {
    error_ = "Unknown character mode.";
    return false;
  }
  if (available_ < 8 || (mode != CharacterMode::Idle && available_ < 13)) {
    error_ = "Expression assets unavailable: export all 13 character tracks first.";
    return false;
  }
  error_ = nullptr;
  if ((latched(mode) || mode == CharacterMode::Idle) && next_ == mode) return true;
  if (mode != CharacterMode::Surprise) persistent_ = latched(mode) ? mode : CharacterMode::Idle;
  next_ = mode;
  ++eventId_;
  effectSeconds_ = 0;
  if (mode_ == CharacterMode::Idle) {
    idle_.returnToCenter(.65);
  } else {
    returnExpression();
  }
  return true;
}

void CharacterMotion::surprise() { setMode(CharacterMode::Surprise); }

CharacterState CharacterMotion::state() const {
  SpritePose pose = mode_ == CharacterMode::Idle ? idle_.pose() : pose_;
  pose.blinkLevel = idle_.pose().blinkLevel;
  return {pose, mode_, next_, static_cast<float>(effectSeconds_), eventId_};
}

void CharacterMotion::beginLeg(uint8_t to, double duration, Phase phase) {
  from_ = pose_.index;
  to_ = to;
  duration_ = duration;
  phase_ = phase;
  progress_ = 0;
  cachedIndex_ = -1;
}

void CharacterMotion::returnExpression() {
  if (phase_ != Phase::Returning) beginLeg(0, .65, Phase::Returning);
}

void CharacterMotion::enter(CharacterMode mode) {
  mode_ = mode;
  effectSeconds_ = 0;
  if (mode == CharacterMode::Idle) {
    idle_.setDuration(1.8);
    idle_.setAutomatic(true);
    return;
  }
  pose_.direction = 7 + static_cast<uint8_t>(mode);
  pose_.index = 0;
  if (mode == CharacterMode::Working) workWait_ = range(4, 7);
  if (mode == CharacterMode::Surprise) surpriseLooksRemaining_ = 2;
  beginLeg(23, mode == CharacterMode::Surprise ? .5 : mode == CharacterMode::Attention ? 1.4 : .95, Phase::Out);
}

double CharacterMotion::edgeInterval() {
  if (cachedIndex_ == pose_.index) return cachedInterval_;
  const int steps = std::abs(int(to_) - from_);
  const int travelled = std::abs(int(pose_.index) - from_);
  cachedInterval_ = duration_ * (inverse(double(travelled + 1) / steps) - inverse(double(travelled) / steps));
  cachedIndex_ = pose_.index;
  return cachedInterval_;
}

void CharacterMotion::beginSurpriseLook(uint8_t direction) {
  pose_.direction = direction;
  const uint8_t target = static_cast<uint8_t>(range(10, 14));
  beginLeg(target, .55, Phase::SurpriseLookOut);
}

void CharacterMotion::advanceExpression(double dt) {
  if (phase_ == Phase::Returning && pose_.index == 0) {
    if (preservesExpression(next_) && idle_.blinkPhase() != SpriteMotion::BlinkPhase::Idle) return;
    enter(next_);
    return;
  }
  if (phase_ == Phase::SurpriseCenter && pose_.index == 0) {
    beginSurpriseLook(static_cast<uint8_t>(range(0, 2)));
    return;
  }
  if (phase_ == Phase::SurpriseLookBack && pose_.index == 0) {
    if (--surpriseLooksRemaining_) {
      beginSurpriseLook(pose_.direction ^ 1);
    } else {
      next_ = persistent_;
      enter(next_);
    }
    return;
  }
  if (phase_ == Phase::SurpriseLookHold) {
    hold_ -= dt;
    if (hold_ <= 0) beginLeg(0, surpriseLooksRemaining_ == 2 ? .5 : .65, Phase::SurpriseLookBack);
    return;
  }
  if (phase_ == Phase::WorkUnfocus && pose_.index == 0) {
    pose_.direction = static_cast<uint8_t>(range(0, 2));
    const uint8_t target = static_cast<uint8_t>(range(8, 14));
    const double duration = range(1.5, 2.2);
    beginLeg(target, duration, Phase::WorkLookOut);
    return;
  }
  if (phase_ == Phase::WorkLookBack && pose_.index == 0) {
    pose_.direction = 9;
    workWait_ = range(4, 7);
    beginLeg(23, .95, Phase::Out);
    return;
  }
  if (phase_ == Phase::AttentionBack && pose_.index == 0) {
    pose_.direction = pose_.direction == 11 ? 12 : 11;
    beginLeg(23, 1.4, Phase::Out);
    return;
  }
  if (phase_ == Phase::WorkLookHold) {
    hold_ -= dt;
    if (hold_ <= 0) beginLeg(0, range(1.5, 2.2), Phase::WorkLookBack);
    return;
  }
  if (mode_ == CharacterMode::Working && pose_.direction == 9
      && (phase_ == Phase::Hold || phase_ == Phase::Micro)) {
    workWait_ -= dt;
    if (workWait_ <= 0) {
      beginLeg(0, 1.0, Phase::WorkUnfocus);
      return;
    }
  }
  if (phase_ == Phase::Hold) {
    hold_ -= dt;
    if (hold_ > 0) return;
    if (mode_ == CharacterMode::Attention) {
      beginLeg(0, 1.4, Phase::AttentionBack);
    } else if (mode_ == CharacterMode::Surprise) {
      beginLeg(0, .55, Phase::SurpriseCenter);
    } else if (latched(mode_)) {
      beginLeg(pose_.index == 23 ? 21 : 23, .45, Phase::Micro);
    } else {
      next_ = CharacterMode::Idle;
      returnExpression();
    }
    return;
  }
  const double interval = edgeInterval();
  progress_ += dt / interval;
  if (progress_ < 1) return;
  const double remainder = (progress_ - 1) * interval;
  pose_.index += to_ > from_ ? 1 : -1;
  progress_ = 0;
  if (pose_.index == to_) {
    if (phase_ == Phase::WorkLookOut) {
      phase_ = Phase::WorkLookHold;
      hold_ = range(.5, 1.2);
    } else if (phase_ == Phase::SurpriseLookOut) {
      phase_ = Phase::SurpriseLookHold;
      hold_ = .12;
    } else if (phase_ != Phase::Returning && phase_ != Phase::WorkUnfocus
               && phase_ != Phase::WorkLookBack && phase_ != Phase::AttentionBack
               && phase_ != Phase::SurpriseCenter && phase_ != Phase::SurpriseLookBack) {
      phase_ = Phase::Hold;
      hold_ = mode_ == CharacterMode::Attention ? range(2.2, 4.2)
          : latched(mode_) ? (pose_.index == 23 ? 1.4 : .9)
          : mode_ == CharacterMode::Surprise ? .25 : 1.1;
    }
  } else {
    progress_ = std::min(remainder / edgeInterval(), 1.0);
  }
}

void CharacterMotion::update(double dt) {
  if (!playing_ || available_ < 8 || !std::isfinite(dt) || dt <= 0) return;
  dt = std::min(dt, 1.0 / 30);
  idle_.setBlinks(!preservesExpression(mode_) && !preservesExpression(next_));
  effectSeconds_ = std::fmod(effectSeconds_ + dt, 12.0);
  if (mode_ == CharacterMode::Idle) {
    if (next_ != CharacterMode::Idle && idle_.pose().index == 0) {
      idle_.update(dt);
      if (preservesExpression(next_) && idle_.blinkPhase() != SpriteMotion::BlinkPhase::Idle) return;
      enter(next_);
      return;
    }
    // An explicit Idle can cancel an expression request while retracing an idle track.
    if (next_ == CharacterMode::Idle && idle_.pose().index == 0 && !idle_.automatic()) {
      idle_.setDuration(1.8);
      idle_.setAutomatic(true);
    }
    idle_.update(dt);
  } else {
    idle_.update(dt);  // Stationary center, with the original independent blink clock.
    advanceExpression(dt);
  }
}
}
