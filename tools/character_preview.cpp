#include "../firmware/Copilot/src/CharacterEffects.h"
#include "../firmware/Copilot/src/SpriteRenderer.h"
#include "../firmware/Copilot/src/SpriteStorage.h"
#include "HostInflate.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace copilot;

int main() {
  if (!initializeSpriteStorage()) {
    std::cerr << spriteStorageError() << '\n';
    return 1;
  }
  std::vector<uint16_t> first(kFrameWidth * kCharacterFrameHeight), second(first.size());
  std::vector<uint16_t> openFirst(kSpriteMaxPatchPixels), openSecond(kSpriteMaxPatchPixels);
  std::vector<uint16_t> patch(kSpriteMaxPatchPixels);
  SpriteRenderer renderer(openFirst.data(), openSecond.data(), patch.data(),
                          first.data() + kCharacterArtOffset, second.data() + kCharacterArtOffset, inflateAtlasHost);
  CharacterEffects effects(first.data(), second.data());
  CharacterMotion motion(20260911);
  if (motion.error()) {
    std::cerr << motion.error() << '\n';
    return 1;
  }
  bool useFirst = true;
  char line[256];
  while (std::cin.getline(line, sizeof(line))) {
    double dt;
    double requestedMode, requestedPlaying;
    int consumed = 0;
    if (std::sscanf(line, "%lf %lf %lf %n", &dt, &requestedMode, &requestedPlaying, &consumed) != 3
        || line[consumed] != '\0' || !std::isfinite(dt) || dt < 0 || dt > 86400
        || !std::isfinite(requestedMode) || requestedMode < -1 || requestedMode > 4
        || std::floor(requestedMode) != requestedMode
        || !std::isfinite(requestedPlaying) || (requestedPlaying != 0 && requestedPlaying != 1)) {
      std::cout << "ERR Invalid character command\n" << std::flush;
      continue;
    }
    const int mode = static_cast<int>(requestedMode), playing = static_cast<int>(requestedPlaying);
    if (mode >= 0 && !motion.setMode(static_cast<CharacterMode>(mode))) {
      std::cout << "ERR " << motion.error() << '\n' << std::flush;
      continue;
    }
    motion.setPlaying(playing != 0);
    motion.update(dt);
    const CharacterState state = motion.state();
    uint16_t* frame = useFirst ? first.data() : second.data();
    const auto start = std::chrono::steady_clock::now();
    if (!effects.restore(frame) || !renderer.render(state.pose, frame + kCharacterArtOffset)
        || !effects.render(state, frame)) {
      std::cout << "ERR " << (effects.error() ? effects.error() : renderer.error()) << '\n' << std::flush;
      continue;
    }
    const double renderMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << std::setprecision(9) << "OK " << first.size() * 2 << ' '
              << unsigned(state.pose.direction) << ' ' << unsigned(state.pose.index) << ' '
              << unsigned(state.pose.blinkLevel) << ' ' << unsigned(state.mode) << ' '
              << unsigned(state.requestedMode) << ' ' << state.effectSeconds << ' '
              << state.eventId << ' ' << renderMs << ' ' << kSpriteDirections << ' ' << playing << '\n';
    std::cout.write(reinterpret_cast<const char*>(frame), first.size() * 2);
    std::cout.flush();
    if (!std::cout) return 1;
    useFirst = !useFirst;
  }
  if (!std::cin.eof()) {
    std::cout << "ERR Character command exceeds 255 bytes\n" << std::flush;
    return 1;
  }
}
