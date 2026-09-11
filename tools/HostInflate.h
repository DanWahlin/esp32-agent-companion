#pragma once
#include <cstddef>
#include <cstdint>
#include <zlib.h>

inline bool inflateAtlasHost(uint8_t* output, size_t outputSize,
                             const uint8_t* input, size_t inputSize) {
  uLongf bytes = outputSize;
  return uncompress(output, &bytes, input, inputSize) == Z_OK && bytes == outputSize;
}
