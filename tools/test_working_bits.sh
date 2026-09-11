#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p build
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/test_working_bits.cpp firmware/Copilot/src/CharacterEffects.cpp \
  -o build/test-working-bits
build/test-working-bits
python3 <<'PY'
from pathlib import Path
import re
import zlib

records = [tuple(map(int, match)) for match in re.findall(
    r'^\s*\{\{(\d+),\s*(\d+)\},',
    Path('firmware/Copilot/src/sprite_data.cpp').read_text(), re.M)]
blob = Path('assets/sprite-firmware.bin').read_bytes()
for direction in (0, 1, 9):
    for pose in range(24):
        offset, size = records[direction * 24 + pose]
        frame = zlib.decompress(blob[offset:offset + size])
        assert len(frame) == 400 * 352 * 2
        for y in range(0, 33):
            for lane in (176, 188, 210, 222):
                for x in range(lane, lane + 4):
                    offset = (y * 400 + x) * 2
                    assert frame[offset:offset + 2] == b'\0\0', (
                        f'Artwork hides binary lane: direction={direction}, pose={pose}, x={x}, y={y}')
print('All 72 focused/unfocus/left/right exported poses leave the binary lanes unobstructed')
PY
