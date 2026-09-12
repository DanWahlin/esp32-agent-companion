#pragma once
#include <cstdint>
#include "SoundLevel.h"

namespace copilot {
enum class AudioCue : uint8_t {
  Working,
  Attention,
  Complete,
  Surprise,
  Settings,
};

bool beginAudio();
bool audioReady();
void setSoundLevel(SoundLevel level);
SoundLevel soundLevel();
bool queueAudioCue(AudioCue cue);
}
