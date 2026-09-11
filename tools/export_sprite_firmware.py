#!/usr/bin/env python3
"""Export display-ready GPT Image sprites as big-endian RGB565 zlib blocks.

Run with the project's Python environment, then run tools/embed_sprites.py.
Only open poses are stored in full. Blink levels replace one shared rectangle,
the union of their changed RGB565 pixels; unchanged poses use no patch data.
Identical uncompressed blocks share storage, including every center pose.
The fixed 5-bit bilinear scaler reproduces the former firmware scaler exactly,
including RGB565 truncation before scaling and float32 pixel-center mapping.
Outputs are deterministic and unchanged files retain their modification times.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import zlib

import numpy as np
from PIL import Image
from embed_sprites import EXPRESSION_DIRECTIONS, EXPRESSION_MANIFEST, read_assets_partition

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT, STEPS = 240, 224, 24
DISPLAY_WIDTH, DISPLAY_HEIGHT, DRAW_WIDTH = 400, 352, 396
PROFILE = "display-ready"
ENCODING = "zlib-rgb565-be"
RESAMPLING = {
    "algorithm": "rgb565-bilinear-5bit-v1",
    "sourceWidth": WIDTH, "sourceHeight": HEIGHT, "drawWidth": DRAW_WIDTH,
    "coordinatePrecision": "float32", "coordinateOrigin": "pixel-center",
    "weightRounding": "lround", "weightDenominator": 32,
    "channelRounding": "floor-horizontal-then-vertical",
}
DIRECTIONS = ("right", "left", "up", "down", "up_right", "up_left",
              "down_right", "down_left")
BLINK_LEVELS = [1, 0.75, 0.5, 0.25, 0]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def write_if_changed(path, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def contained_path(directory, name):
    if not isinstance(name, str) or not name or Path(name).is_absolute():
        raise ValueError(f"Expected a relative asset path: {name!r}")
    path = (directory / name).resolve()
    if not path.is_relative_to(directory.resolve()):
        raise ValueError(f"Asset path escapes its directory: {name!r}")
    if not path.is_file():
        raise ValueError(f"Missing asset: {path}")
    return path


def read_png(path, expected_size=None):
    data = path.read_bytes()
    with Image.open(io.BytesIO(data)) as image:
        if image.format != "PNG":
            raise ValueError(f"Expected PNG: {path}")
        if expected_size is not None and image.size != expected_size:
            raise ValueError(f"Invalid image dimensions for {path}: {image.size}")
        if "A" in image.getbands() and image.getchannel("A").getextrema() != (255, 255):
            raise ValueError(f"Transparent pixels are not supported: {path}")
        if "transparency" in image.info:
            raise ValueError(f"PNG transparency is not supported: {path}")
        pixels = np.asarray(image.convert("RGB")).copy()
    return pixels, sha256(data)


def rgb565(pixels):
    channels = pixels.astype(np.uint16)
    return ((channels[:, :, 0] >> 3) << 11 |
            (channels[:, :, 1] >> 2) << 5 |
            channels[:, :, 2] >> 3).astype("<u2")


def display_pixels(source):
    """Resample already-truncated RGB565 pixels into SPI wire-order pixels."""
    if source.shape != (HEIGHT, WIDTH):
        raise ValueError(f"Expected {WIDTH}x{HEIGHT} source pixels.")
    factor = np.float32(DRAW_WIDTH) / np.float32(WIDTH)
    offset_x = np.float32(DISPLAY_WIDTH - DRAW_WIDTH) / np.float32(2)
    offset_y = (np.float32(DISPLAY_HEIGHT) - np.float32(HEIGHT) * factor) / np.float32(2)

    def coordinates(length, source_length, offset):
        position = ((np.arange(length, dtype=np.float32) + np.float32(0.5) - offset)
                    / factor - np.float32(0.5))
        position = np.clip(position, np.float32(0), np.float32(source_length - 1))
        lower = np.floor(position).astype(np.int32)
        upper = np.minimum(lower + 1, source_length - 1)
        # Coordinates are nonnegative, so floor(x + .5) implements C++ lround.
        weight = np.floor((position - lower.astype(np.float32)) * np.float32(32)
                          + np.float32(0.5)).astype(np.uint32)
        return lower, upper, weight

    x0, x1, wx = coordinates(DISPLAY_WIDTH, WIDTH, offset_x)
    y0, y1, wy = coordinates(DISPLAY_HEIGHT, HEIGHT, offset_y)
    result = np.zeros((DISPLAY_HEIGHT, DISPLAY_WIDTH), dtype=np.uint32)
    for shift, mask in ((11, 31), (5, 63), (0, 31)):
        channel = (source.astype(np.uint32) >> shift) & mask
        horizontal = (channel[:, x0] * (32 - wx) + channel[:, x1] * wx) // 32
        interpolated = (horizontal[y0] * (32 - wy[:, None])
                        + horizontal[y1] * wy[:, None]) // 32
        result |= interpolated << shift
    xx = np.arange(DISPLAY_WIDTH, dtype=np.float32) + np.float32(0.5)
    result[:, (xx < offset_x) | (xx >= offset_x + np.float32(DRAW_WIDTH))] = 0
    return result.astype(">u2")


def patch_bounds(base, blinks):
    changed = np.logical_or.reduce([blink != base for blink in blinks])
    yy, xx = np.nonzero(changed)
    if not len(xx):
        return 0, 0, 0, 0
    x, y = int(xx.min()), int(yy.min())
    return x, y, int(xx.max()) + 1 - x, int(yy.max()) + 1 - y


def validate_bounds(bounds, width, height, label):
    if (not isinstance(bounds, (list, tuple)) or len(bounds) != 4
            or any(type(value) is not int for value in bounds)):
        raise ValueError(f"Invalid {label} bounds: {bounds!r}")
    x0, y0, x1, y1 = bounds
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError(f"Out-of-range {label} bounds: {bounds!r}")


def validate_manifest(manifest, directions=DIRECTIONS, shared_center=True):
    if not isinstance(manifest, dict):
        raise ValueError("Animation metadata must be an object.")
    for key, expected in (("width", WIDTH), ("height", HEIGHT), ("count", STEPS)):
        if type(manifest.get(key)) is not int or manifest[key] != expected:
            raise ValueError(f"Expected animation {key}={expected}.")
    if manifest.get("blinkLevels") != BLINK_LEVELS:
        raise ValueError(f"Expected blinkLevels={BLINK_LEVELS}.")
    tracks = manifest.get("directions")
    if not isinstance(tracks, dict) or set(tracks) != set(directions):
        raise ValueError(f"Expected exactly these tracks: {', '.join(directions)}.")
    center = manifest.get("centerSha256")
    if ((shared_center or center is not None) and
            (not isinstance(center, str) or len(center) != 64
             or any(c not in "0123456789abcdef" for c in center))):
        raise ValueError("Expected a lowercase SHA256 centerSha256.")
    for direction in directions:
        track = tracks[direction]
        if not isinstance(track, dict) or track.get("provider") != "Azure GPT Image":
            raise ValueError(f"Unsupported provider for {direction}; require Azure GPT Image.")
        for key, expected in (("width", WIDTH), ("height", HEIGHT), ("count", STEPS)):
            if type(track.get(key)) is not int or track[key] != expected:
                raise ValueError(f"Invalid {direction} {key}; expected {expected}.")
        frames = track.get("frames")
        if not isinstance(frames, list) or len(frames) != STEPS:
            raise ValueError(f"Expected {STEPS} frames for {direction}.")
        for index, frame in enumerate(frames):
            if not isinstance(frame, dict):
                raise ValueError(f"Invalid {direction} frame {index}.")
            if not isinstance(frame.get("blinks"), list) or len(frame["blinks"]) != 4:
                raise ValueError(f"Expected four blink files for {direction} frame {index}.")
            for filename in [frame.get("file"), *frame["blinks"]]:
                if not isinstance(filename, str) or not filename.endswith(".png"):
                    raise ValueError(f"Invalid PNG filename for {direction} frame {index}.")
            if not isinstance(frame.get("eyes"), list):
                raise ValueError(f"Invalid eye metadata for {direction} frame {index}.")
            for bounds in frame["eyes"]:
                validate_bounds(bounds, WIDTH, HEIGHT, "eye")


class BlockStore:
    def __init__(self):
        self.data = bytearray()
        self.blocks = {}
        self.referenced_compressed_bytes = 0
        self.referenced_raw_bytes = 0
        self.references = 0

    def add(self, raw):
        if not raw:
            return {"offset": 0, "size": 0}
        self.references += 1
        self.referenced_raw_bytes += len(raw)
        if raw not in self.blocks:
            compressed = zlib.compress(raw, level=9)
            self.blocks[raw] = {"offset": len(self.data), "size": len(compressed)}
            self.data.extend(compressed)
        block = self.blocks[raw]
        self.referenced_compressed_bytes += block["size"]
        return dict(block)


def enforce_budget(data_size, budget):
    if type(budget) is not int or budget <= 0:
        raise ValueError("Asset partition budget must be a positive integer.")
    if data_size > budget:
        raise ValueError(
            f"Sprite assets exceed partition budget: {data_size:,} compressed > {budget:,} bytes. "
            "Optimize compression/deduplication; do not reduce image quality."
        )


def load_manifests(manifest_path, root=ROOT):
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    validate_manifest(manifest)
    inputs = [(manifest_path, manifest_bytes, manifest, DIRECTIONS)]
    expression_path = root / EXPRESSION_MANIFEST
    if expression_path.exists():
        raw = expression_path.read_bytes()
        expressions = json.loads(raw)
        validate_manifest(expressions, EXPRESSION_DIRECTIONS)
        if expressions["centerSha256"] != manifest["centerSha256"]:
            raise ValueError("Expression tracks must share the approved idle center.")
        if ("approvedSpriteManifestSha256" in expressions
                and expressions["approvedSpriteManifestSha256"] != sha256(manifest_bytes)):
            raise ValueError("Expression tracks reference a stale approved idle manifest.")
        inputs.append((expression_path, raw, expressions, EXPRESSION_DIRECTIONS))
    return inputs


def build_assets(manifest_path, root=ROOT):
    partition = read_assets_partition(root)
    inputs = load_manifests(manifest_path, root)
    store = BlockStore()
    frames, sources, manifests = [], {}, {}
    centers = None
    source_centers = None
    max_patch_pixels = 0
    tracks = []
    for path, raw, manifest, directions in inputs:
        manifests[path.relative_to(root).as_posix()] = {
            "sha256": sha256(raw), "sourcePngSha256": {},
        }
        for direction in directions:
            tracks.append((direction, path, manifest))
    for direction, track_manifest_path, manifest in tracks:
        track = manifest["directions"][direction]
        source_directory = (track_manifest_path.parent if direction in EXPRESSION_DIRECTIONS
                            else root / "assets/generated-sprites")
        source = contained_path(source_directory, track.get("source"))
        sheet, source_hash = read_png(source)
        if "sourceSha256" in track and track["sourceSha256"] != source_hash:
            raise ValueError(f"Source PNG hash mismatch for {direction}.")
        provenance = {"provider": track["provider"], "source": source.relative_to(root).as_posix(),
                      "sourceSha256": source_hash}
        if "generationProvenance" in track:
            path = contained_path(root, track["generationProvenance"])
            raw = path.read_bytes()
            if json.loads(raw).get("sha256") != source_hash:
                raise ValueError(f"Generation provenance hash mismatch for {direction}.")
            provenance.update(generationManifest=path.relative_to(root).as_posix(),
                              generationManifestSha256=sha256(raw))
        sources[direction] = provenance
        png_hashes = manifests[track_manifest_path.relative_to(root).as_posix()]["sourcePngSha256"]
        for step, frame in enumerate(track["frames"]):
            validate_bounds(frame.get("sourceBounds"), sheet.shape[1], sheet.shape[0], "source")
            packed = []
            source_hashes = []
            for filename in [frame["file"], *frame["blinks"]]:
                path = contained_path(track_manifest_path.parent, filename)
                pixels, digest = read_png(path, (WIDTH, HEIGHT))
                png_hashes[filename] = digest
                if not packed and "sha256" in frame and frame["sha256"] != digest:
                    raise ValueError(f"Frame PNG hash mismatch for {direction}/{step}.")
                source_hashes.append(sha256(pixels.tobytes()))
                if (step == 0 and len(packed) == 0 and manifest.get("centerSha256") is not None
                        and sha256(pixels.tobytes()) != manifest["centerSha256"]):
                    raise ValueError(f"Center RGB hash mismatch for {direction}.")
                packed.append(display_pixels(rgb565(pixels)))
            if step == 0:
                hashes = [sha256(pixels.tobytes()) for pixels in packed]
                if centers is None:
                    centers = hashes
                    source_centers = source_hashes
                elif hashes != centers or source_hashes != source_centers:
                    raise ValueError(f"Center RGB565 blink levels differ for {direction}.")
            base, *blinks = packed
            x, y, width, height = patch_bounds(base, blinks)
            if width and height:
                validate_bounds([x, y, x + width, y + height], DISPLAY_WIDTH, DISPLAY_HEIGHT, "patch")
                if width == DISPLAY_WIDTH and height == DISPLAY_HEIGHT:
                    raise ValueError(f"Blink patch cannot be a copied full image: {direction}/{step}.")
            max_patch_pixels = max(max_patch_pixels, width * height)
            frames.append({
                "direction": direction, "step": step,
                "base": store.add(base.tobytes()),
                "patchX": x, "patchY": y, "patchWidth": width, "patchHeight": height,
                "blinks": [store.add(blink[y:y + height, x:x + width].tobytes()) for blink in blinks],
            })
    enforce_budget(len(store.data), partition["size"])
    frame_table_bytes = len(frames) * 48
    metadata_bytes = frame_table_bytes + 4 + 32
    metadata = {
        "formatVersion": 3, "profile": PROFILE, "encoding": ENCODING,
        "storage": "flash-partition", "partition": partition,
        "displayReady": True, "resampling": RESAMPLING,
        "width": DISPLAY_WIDTH, "height": DISPLAY_HEIGHT,
        "steps": STEPS, "directions": [direction for direction, _, _ in tracks],
        "frameCount": len(frames), "blinkLevels": BLINK_LEVELS,
        "patchBounds": "union of differences after exact RGB565 conversion and display resampling",
        "maxPatchPixels": max_patch_pixels, "maxPatchBytes": max_patch_pixels * 2,
        "assetBudgetBytes": partition["size"], "dataBytes": len(store.data),
        "frameTableBytes": frame_table_bytes, "metadataBytes": metadata_bytes,
        "metadataStorage": "application", "totalAssetBytes": len(store.data) + metadata_bytes,
        "budgetRemainingBytes": partition["size"] - len(store.data),
        "uniqueBlocks": len(store.blocks), "blockReferences": store.references,
        "deduplicatedBytesSaved": store.referenced_compressed_bytes - len(store.data),
        "rawReferencedBytes": store.referenced_raw_bytes,
        "dataSha256": sha256(store.data),
        "sourceManifests": manifests,
        "centerRgbSha256": inputs[0][2]["centerSha256"], "centerRgb565Sha256": centers,
        "sources": sources, "frames": frames,
    }
    return bytes(store.data), metadata


def render_header(metadata):
    return f"""// Generated by tools/export_sprite_firmware.py; do not edit.
#pragma once
#include <stdint.h>

namespace copilot {{
constexpr int kSpriteWidth = {metadata['width']};
constexpr int kSpriteHeight = {metadata['height']};
constexpr int kSpriteDirections = {len(metadata['directions'])};
constexpr int kSpriteSteps = {STEPS};
constexpr int kSpriteFrameCount = {metadata['frameCount']};
constexpr int kSpriteBlinkLevels = {len(BLINK_LEVELS)};
constexpr int kSpriteMaxPatchPixels = {metadata['maxPatchPixels']};
constexpr bool kSpriteDisplayReady = true;

struct SpriteBlock {{
    uint32_t offset;
    uint32_t size;
}};
struct SpriteFrame {{
    SpriteBlock base;
    uint16_t patchX, patchY, patchWidth, patchHeight;
    SpriteBlock blinks[4];
}};
static_assert(sizeof(SpriteFrame) == 48, "Sprite metadata budget requires 48-byte frames");
extern const SpriteFrame kSpriteFrames[kSpriteFrameCount];
extern const uint8_t* kSpriteData;
extern const uint32_t kSpriteDataSize;
extern const uint8_t kSpriteDataSha256[32];
}}  // namespace copilot
"""


def render_cpp(metadata):
    def block(value):
        return "{" + f"{value['offset']}, {value['size']}" + "}"
    lines = ['// Generated by tools/export_sprite_firmware.py; do not edit.',
             '#include "../generated/sprite_assets.h"', '', 'namespace copilot {',
             'const SpriteFrame kSpriteFrames[kSpriteFrameCount] = {']
    for frame in metadata["frames"]:
        rectangle = ", ".join(str(frame[key]) for key in
                              ("patchX", "patchY", "patchWidth", "patchHeight"))
        blinks = ", ".join(block(value) for value in frame["blinks"])
        lines.append(f"    {{{block(frame['base'])}, {rectangle}, {{{blinks}}}}},")
    digest = ", ".join(f"0x{byte:02x}" for byte in bytes.fromhex(metadata["dataSha256"]))
    lines.extend(['};', f"const uint32_t kSpriteDataSize = {metadata['dataBytes']};",
                  f"const uint8_t kSpriteDataSha256[32] = {{{digest}}};",
                  '}  // namespace copilot', ''])
    return "\n".join(lines)


def export(root=ROOT):
    data, metadata = build_assets(root / "web/generated-sprites/animation.json", root)
    write_if_changed(root / "assets/sprite-firmware.bin", data)
    write_if_changed(root / "assets/sprite-firmware.json", json.dumps(metadata, indent=2) + "\n")
    write_if_changed(root / "firmware/Copilot/generated/sprite_assets.h", render_header(metadata))
    write_if_changed(root / "firmware/Copilot/src/sprite_data.cpp", render_cpp(metadata))
    print(f"Sprite data: {len(data):,} bytes ({len(data) / 1024**2:.3f} MiB); "
          f"partition: {len(data):,} / {metadata['assetBudgetBytes']:,} bytes; "
          f"application metadata: {metadata['metadataBytes']:,} bytes")
    print(f"Unique blocks: {metadata['uniqueBlocks']}; dedup saved: "
          f"{metadata['deduplicatedBytesSaved']:,} bytes; "
          f"max patch: {metadata['maxPatchPixels']:,} pixels / {metadata['maxPatchBytes']:,} bytes")
    print(f"SHA256 {metadata['dataSha256']}\nNext: python tools/embed_sprites.py")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    try:
        export()
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f"Sprite export failed: {error}\n")


if __name__ == "__main__":
    main()
