#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build
# The existing validator rejects stale art/metadata; never substitute preview PNGs.
python3 tools/embed_sprites.py
"${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  tools/character_preview.cpp firmware/Copilot/src/CharacterMotion.cpp \
  firmware/Copilot/src/CharacterEffects.cpp firmware/Copilot/src/SpriteMotion.cpp \
  firmware/Copilot/src/SpriteRenderer.cpp firmware/Copilot/src/SpriteStorage.cpp \
  firmware/Copilot/src/sprite_data.cpp build/sprite_host.S -lz -o build/character-preview
