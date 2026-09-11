#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python3 tools/embed_sprites.py
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/test_sprite_predictor.cpp -lz -o build/test-sprite-predictor
build/test-sprite-predictor
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/test_sprite_renderer.cpp firmware/Copilot/src/SpriteRenderer.cpp \
  firmware/Copilot/src/sprite_data.cpp firmware/Copilot/src/SpriteStorage.cpp build/sprite_host.S -lz \
  -o build/test-sprite-renderer
build/test-sprite-renderer
