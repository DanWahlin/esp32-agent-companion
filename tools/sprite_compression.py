"""Offline RGB565 word-Up prediction and cached Zopfli zlib compression.

Each block starts a new predictor: its first row is unchanged. Subsequent
rows subtract the packed pixel above modulo 65536, then emit the residual
as a big-endian 16-bit word. There are no row filter tags.
The inflater's history and Adler checksum contain residuals, not decoded art.
Firmware build/embed does not import this offline-only module.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import uuid
import zlib

import numpy as np
import zopfli
import zopfli.zlib

ENCODING = "zlib-rgb565-word-up-be"
ZOPFLI_VERSION = "0.2.3.post1"
ZOPFLI_OPTIONS = {"numiterations": 1}
CACHE_VERSION = b"sprite-word-up-zopfli-cache-v1\0"
DEFAULT_CACHE = Path(__file__).resolve().parents[1] / "build/sprite-compression"


def word_up(raw, width):
    """Return same-size BE residuals, resetting the previous row per block."""
    if type(width) is not int or width <= 0 or not raw or len(raw) % (width * 2):
        raise ValueError("RGB565 prediction requires nonempty complete rows and a positive integer width.")
    pixels = np.frombuffer(raw, dtype=">u2").reshape(-1, width).astype(np.uint16)
    residual = pixels.copy()
    residual[1:] = pixels[1:] - pixels[:-1]
    return residual.astype(">u2").tobytes()


class CompressionCache:
    def __init__(self, directory=DEFAULT_CACHE):
        self.directory = Path(directory)
        if zopfli.__version__ != ZOPFLI_VERSION:
            raise ValueError(f"Sprite export requires zopfli=={ZOPFLI_VERSION}; "
                             "install requirements-art.txt in the project environment.")

    def path_for(self, raw, width):
        settings = (f"{ENCODING}\0{ZOPFLI_VERSION}\0{ZOPFLI_OPTIONS['numiterations']}\0"
                    f"{width}\0{len(raw)}\0").encode("ascii")
        key = hashlib.sha256(CACHE_VERSION + settings + hashlib.sha256(raw).digest()).hexdigest()
        return self.directory / f"{key}.zlib"

    @staticmethod
    def _matches(compressed, predicted):
        try:
            decoder = zlib.decompressobj()
            decoded = decoder.decompress(compressed, len(predicted) + 1)
            return (decoded == predicted and decoder.eof
                    and not decoder.unused_data and not decoder.unconsumed_tail)
        except zlib.error:
            return False

    def compress(self, raw, width):
        predicted = word_up(raw, width)
        path = self.path_for(raw, width)
        try:
            # Bound both disk reads and inflate output for malformed cache files.
            with path.open("rb") as source:
                record = source.read(len(predicted) * 2 + 1025)
            prefix = len(CACHE_VERSION)
            compressed = record[prefix + 32:]
            if (record[:prefix] == CACHE_VERSION
                    and record[prefix:prefix + 32] == hashlib.sha256(compressed).digest()
                    and self._matches(compressed, predicted)):
                return compressed
        except OSError:
            pass
        compressed = zopfli.zlib.compress(predicted, **ZOPFLI_OPTIONS)
        if not self._matches(compressed, predicted):
            raise ValueError("Zopfli sprite compression failed exact residual round-trip validation.")
        record = CACHE_VERSION + hashlib.sha256(compressed).digest() + compressed
        self.directory.mkdir(parents=True, exist_ok=True)
        pending = path.with_name(f".{path.name}.{uuid.uuid4().hex}.part")
        try:
            with pending.open("xb") as output:
                output.write(record)
            os.replace(pending, path)
        finally:
            pending.unlink(missing_ok=True)
        return compressed
