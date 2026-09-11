#pragma once
#include "SpriteMotion.h"

namespace copilot {
enum class CharacterMode : uint8_t { Idle, Surprise, Working, Complete, Attention };
struct CharacterState {
  SpritePose pose;
  // Visible track and next destination; they can differ while retracing to center.
  CharacterMode mode = CharacterMode::Idle;
  CharacterMode requestedMode = CharacterMode::Idle;
  // Reset on accepted events/track entry; wraps at 12 s for periodic effects.
  float effectSeconds = 0;
  uint32_t eventId = 0;
};

class CharacterMotion {
 public:
  explicit CharacterMotion(uint32_t seed);
  // Explicit availability supports host fixtures; production uses generated metadata.
  CharacterMotion(uint32_t seed, uint8_t availableDirections);
  void update(double dt);
  bool setMode(CharacterMode mode);
  void surprise();
  CharacterState state() const;
  const char* error() const { return error_; }
  void setPlaying(bool value) { playing_ = value; }

 private:
  enum class Phase { Out, Hold, Micro, Returning, WorkUnfocus, WorkLookOut, WorkLookHold, WorkLookBack,
                     AttentionBack, SurpriseCenter, SurpriseLookOut, SurpriseLookHold, SurpriseLookBack };
  void enter(CharacterMode mode);
  void beginLeg(uint8_t to, double duration, Phase phase);
  void returnExpression();
  double edgeInterval();
  double range(double low, double high);
  void advanceExpression(double dt);
  void beginSurpriseLook(uint8_t direction);
  SpriteMotion idle_;
  SpritePose pose_;
  CharacterMode mode_ = CharacterMode::Idle;
  CharacterMode next_ = CharacterMode::Idle;
  CharacterMode persistent_ = CharacterMode::Idle;
  Phase phase_ = Phase::Out;
  uint8_t available_;
  uint32_t random_;
  double workWait_ = 0;
  uint8_t surpriseLooksRemaining_ = 0;
  uint8_t from_ = 0, to_ = 23;
  double progress_ = 0, duration_ = .85, hold_ = 0;
  double effectSeconds_ = 0;
  double cachedInterval_ = 0;
  int cachedIndex_ = -1;
  uint32_t eventId_ = 0;
  bool playing_ = true;
  const char* error_ = nullptr;
};
}
