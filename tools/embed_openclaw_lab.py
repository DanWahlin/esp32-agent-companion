#!/usr/bin/env python3
"""Validate the committed OpenClaw lab pack and expose it to the native host build."""
import hashlib
import json
from pathlib import Path
import platform

ROOT = Path(__file__).resolve().parents[1]
TRACKS = (
    "right", "left", "up", "down", "up_right", "up_left", "down_right", "down_left",
    "surprise", "working", "complete", "attention", "attention_alternate",
)


def main():
    data_path = ROOT / "assets/openclaw-lab.bin"
    metadata = json.loads((ROOT / "assets/openclaw-lab.json").read_text())
    data = data_path.read_bytes()
    if (metadata.get("formatVersion") != 1
            or metadata.get("encoding") != "zlib-rgb565-word-up-be"
            or metadata.get("width") != 412 or metadata.get("height") != 352
            or metadata.get("steps") != 24 or tuple(metadata.get("directions", [])) != TRACKS
            or metadata.get("frameBlocks") != 13 * 24 * 5
            or metadata.get("dataBytes") != len(data)
            or metadata.get("dataSha256") != hashlib.sha256(data).hexdigest()):
        raise ValueError("OpenClaw lab pack metadata is stale or invalid.")
    checks = {
        "modelSha256": ROOT / "tools/openclaw/model.js",
        "exporterSha256": ROOT / "tools/openclaw/export-sprites.mjs",
        "packExporterSha256": ROOT / "tools/export_openclaw_lab.py",
        "generatedHeaderSha256": ROOT / "firmware/Copilot/generated/openclaw_assets.h",
        "packageLockSha256": ROOT / "tools/openclaw/package-lock.json",
        "referenceSvgSha256": ROOT / "assets/openclaw/reference.svg",
    }
    for field, path in checks.items():
        if metadata.get(field) != hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError(f"OpenClaw lab pack is stale for {path}.")
    output = ROOT / "build/openclaw_host.S"
    symbol = "kOpenClawDataBlob"
    section = ".section .rodata"
    if platform.system() == "Darwin":
        symbol = "_" + symbol
        section = ".section __TEXT,__const"
    escaped = str(data_path).replace("\\", "\\\\").replace('"', '\\"')
    output.write_text(
        f'{section}\n.balign 4\n.globl {symbol}\n{symbol}:\n.incbin "{escaped}"\n')
    print(f"OpenClaw lab payload: {len(data):,} bytes, SHA256 {metadata['dataSha256']}")


if __name__ == "__main__":
    main()
