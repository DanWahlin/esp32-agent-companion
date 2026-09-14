#!/usr/bin/env python3
"""Package fresh matched firmware and SD assets without rebuilding or exporting."""
import argparse
import json
from pathlib import Path
import re
import sys
import zipfile

if __package__:
    from . import firmware_artifacts
    from .flash_release import IMAGE_NAMES, WARNING, sha256, validate_identity, validate_manifest
else:
    import firmware_artifacts
    from flash_release import IMAGE_NAMES, WARNING, sha256, validate_identity, validate_manifest

ROOT = Path(__file__).resolve().parents[1]


def checksum_file(files):
    return "".join(f"{sha256(data)}  {name}\n" for name, data in sorted(files.items())).encode("ascii")


def write_zip(path, files):
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_STORED
            archive.writestr(info, data)


def package(root, version, name="esp32-agent-companion", output=None,
            repository="DanWahlin/esp32-agent-companion"):
    root = Path(root)
    validate_identity(name, version)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository, flags=re.ASCII):
        raise ValueError("Repository must be an owner/repository pair.")
    firmware_artifacts.check(root)
    openclaw_path = root / "assets/openclaw-lab.bin"
    openclaw_metadata = json.loads((root / "assets/openclaw-lab.json").read_text())
    openclaw_data = openclaw_path.read_bytes()
    if (openclaw_metadata.get("dataBytes") != len(openclaw_data)
            or openclaw_metadata.get("dataSha256") != sha256(openclaw_data)):
        raise ValueError("OpenClaw SD pack does not match its metadata.")
    openclaw_digest = sha256(openclaw_data)
    app, assets = firmware_artifacts.layout(root)
    sources = (
        "build/firmware/Copilot.ino.bootloader.bin",
        "build/firmware/Copilot.ino.partitions.bin",
        "build/firmware/boot_app0.bin",
        "build/firmware/Copilot.ino.bin",
        "assets/sprite-firmware.bin",
    )
    regions = [(0, 0x8000), (0x8000, 0x1000), (0xe000, 0x2000),
               (app["offset"], app["size"]), (assets["offset"], assets["size"])]
    files = {}
    images = []
    for filename, source, (offset, budget) in zip(IMAGE_NAMES, sources, regions):
        path = root / source
        if not 0 < path.stat().st_size <= budget:
            raise ValueError(f"Image exceeds its partition budget or is empty: {source}.")
        data = path.read_bytes()
        files[filename] = data
        images.append(dict(file=filename, offset=offset, max_size=budget, size=len(data), sha256=sha256(data)))
    manifest = dict(schema_version=1, name=name, version=version, chip="esp32s3", flash_size="16MB",
                    asset_sha256=images[-1]["sha256"], images=images)
    validate_manifest(manifest, files)
    files["manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    files["flash.py"] = (root / "tools/flash_release.py").read_bytes()
    files["requirements.txt"] = (root / "requirements-flash.txt").read_bytes()
    if files["requirements.txt"] != b"esptool==5.3.0\n":
        raise ValueError("requirements-flash.txt must pin esptool==5.3.0.")
    files["INSTALL.txt"] = (
        f"{name} v{version} — ESP32-S3 / 16MB flash\n\n"
        "Download both ZIPs and SHA256SUMS from a trusted release:\n"
        f"https://github.com/{repository}/releases/tag/v{version}\n"
        "Compare the ZIP SHA256 hashes with that release's SHA256SUMS before extracting.\n"
        "Checksums detect corruption; they are not signatures or proof of publisher identity.\n\n"
        "Extract the firmware ZIP into its own directory. Use Python 3.10 or newer.\n"
        "Open a terminal in the extracted directory (use python3 if needed):\n"
        "  python flash.py --verify-only\n"
        "  python -m venv .venv\n"
        "Activate: Windows: .venv\\Scripts\\activate\n"
        "          macOS/Linux: source .venv/bin/activate\n"
        "  python -m pip install -r requirements.txt\n"
        "  python flash.py --list-ports\n"
        "  python flash.py --port COM3\n"
        "Replace COM3 with your actual port, for example /dev/ttyACM0.\n"
        "Use a USB data cable. Type FLASH when prompted; --yes skips only this prompt.\n\n"
        f"{WARNING}\n\n"
        "Optional OpenClaw character: extract the SD-card ZIP at the root of a FAT32 card so\n"
        "the file is characters/openclaw/sprites.bin. Select OpenClaw from device Settings.\n"
        "Keep this SD payload matched to this firmware release.\n"
        "The installer does not mount, format, or write any SD card.\n"
    ).encode("utf-8")
    # Recheck after reading so changes during packaging cannot create a mismatched release.
    firmware_artifacts.check(root)
    recorded = json.loads((root / firmware_artifacts.BUNDLE).read_text())
    expected = dict(recorded["binaries"])
    expected.update(recorded["inputs"]["files"])
    for filename, source in zip(IMAGE_NAMES, sources):
        if sha256(files[filename]) != expected[source]:
            raise ValueError(f"Firmware changed while packaging: {source}.")
    if sha256(openclaw_path.read_bytes()) != openclaw_digest:
        raise ValueError("OpenClaw SD pack changed while packaging.")
    files["SHA256SUMS"] = checksum_file(files)
    output = Path(output) if output is not None else root / "build/release"
    output.mkdir(parents=True, exist_ok=True)
    firmware_zip = output / f"{name}-v{version}-firmware.zip"
    sd_zip = output / f"{name}-v{version}-sd-card.zip"
    write_zip(firmware_zip, files)
    write_zip(sd_zip, {"characters/openclaw/sprites.bin": openclaw_data})
    (output / "SHA256SUMS").write_bytes(checksum_file({
        firmware_zip.name: firmware_zip.read_bytes(), sd_zip.name: sd_zip.read_bytes(),
    }))
    return firmware_zip, sd_zip


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="esp32-agent-companion", help="Lowercase ASCII release slug")
    parser.add_argument("--version", required=True, help="Stable SemVer without a v prefix")
    parser.add_argument("--output", type=Path, default=ROOT / "build/release")
    parser.add_argument("--repository", default="DanWahlin/esp32-agent-companion",
                        help="Owner/repository for INSTALL links")
    args = parser.parse_args(argv)
    try:
        for path in package(ROOT, args.version, args.name, args.output, args.repository):
            print(path)
        print(args.output / "SHA256SUMS")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Release packaging error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
