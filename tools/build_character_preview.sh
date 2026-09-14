#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build
# The existing validator rejects stale art/metadata; never substitute preview PNGs.
python3 tools/embed_atlas.py
python3 tools/embed_sprites.py
python3 tools/embed_openclaw_lab.py
"${CXX:-clang++}" -std=c++17 -O3 -Wall -Wextra -Werror \
  tools/live_preview.cpp firmware/Copilot/src/AtlasRenderer.cpp \
  firmware/Copilot/src/Motion.cpp firmware/Copilot/src/turn_atlas.cpp \
  build/atlas_host.S -lz -o build/live-preview
"${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  tools/character_preview.cpp firmware/Copilot/src/CharacterMotion.cpp \
  firmware/Copilot/src/OpenClawSpriteRenderer.cpp \
  firmware/Copilot/src/CharacterEffects.cpp firmware/Copilot/src/SpriteMotion.cpp \
  firmware/Copilot/src/SpriteRenderer.cpp firmware/Copilot/src/SpriteStorage.cpp \
  firmware/Copilot/src/sprite_data.cpp build/sprite_host.S build/openclaw_host.S \
  -lz -o build/character-preview
