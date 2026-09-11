"""Standard-library decoder for zlib-rgb565-word-up-be tooling."""
from array import array
import sys
import zlib


def decode_pixels(compressed_zlib_bytes, width):
    """Decode one independently predicted block to original RGB565 BE bytes."""
    if type(width) is not int or width <= 0:
        raise ValueError("Sprite decode width must be a positive integer.")
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed_zlib_bytes)
    if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("Expected exactly one complete sprite zlib stream.")
    if not raw or len(raw) % (width * 2):
        raise ValueError("Sprite residuals must contain nonempty complete RGB565 rows.")
    pixels = array("H")
    pixels.frombytes(raw)
    if sys.byteorder == "little":
        pixels.byteswap()
    for index in range(width, len(pixels)):
        pixels[index] = (pixels[index] + pixels[index - width]) & 0xFFFF
    if sys.byteorder == "little":
        pixels.byteswap()
    return pixels.tobytes()


def decode_base(compressed_zlib_bytes, metadata):
    """Place the shared base crop into a zero-filled logical RGB565 BE canvas."""
    width, height = metadata["width"], metadata["height"]
    bounds = metadata["baseBounds"]
    if (type(width) is not int or type(height) is not int or width <= 0 or height <= 0
            or not isinstance(bounds, (list, tuple)) or len(bounds) != 4
            or any(type(value) is not int for value in bounds)):
        raise ValueError("Invalid sprite canvas dimensions or baseBounds.")
    x, y, crop_width, crop_height = bounds
    if not (0 <= x < x + crop_width <= width and 0 <= y < y + crop_height <= height):
        raise ValueError("Sprite baseBounds lie outside the logical canvas.")
    crop = decode_pixels(compressed_zlib_bytes, crop_width)
    if len(crop) != crop_width * crop_height * 2:
        raise ValueError("Decoded sprite base size does not match baseBounds.")
    canvas = bytearray(width * height * 2)
    row_bytes = crop_width * 2
    for row in range(crop_height):
        offset = ((y + row) * width + x) * 2
        canvas[offset:offset + row_bytes] = crop[row * row_bytes:(row + 1) * row_bytes]
    return bytes(canvas)
