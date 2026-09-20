#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
for TEST in touch_input settings_menu device_commands tilt_look; do
  SOURCES=("tests/test_${TEST}.cpp")
  if [[ "$TEST" == "tilt_look" ]]; then
    SOURCES+=("firmware/Copilot/src/TiltLook.cpp")
  fi
  clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${SOURCES[@]}" -o "build/test-${TEST}"
  "build/test-${TEST}"
done
