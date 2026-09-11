#!/usr/bin/env python3
"""Bounded serial health/performance check and optional on-device framebuffer capture."""
import argparse
from pathlib import Path
import re
import time
import os
if os.name == "posix":
    import termios

import serial


class NonResettingSerial(serial.Serial):
    """Observe the ESP32 without pulsing its DTR/RTS auto-reset signals."""

    def _update_dtr_state(self):
        pass

    def _update_rts_state(self):
        pass

    def _reconfigure_port(self, force_update=False):
        super()._reconfigure_port(force_update)
        attributes = termios.tcgetattr(self.fd)
        attributes[2] &= ~termios.HUPCL
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)


def read_frame_end(port, deadline):
    pending = bytearray()
    while time.monotonic() < deadline:
        pending.extend(port.read_until(b"\n"))
        while b"\n" in pending:
            line, _, remainder = pending.partition(b"\n")
            pending = bytearray(remainder)
            token = line.strip()
            if not token:
                continue
            if token == b"END_FRAME":
                return
            raise RuntimeError(f"Unexpected framebuffer terminator: {token!r}")
    raise TimeoutError("Missing framebuffer terminator")


def capture(port, destination):
    import numpy as np
    from PIL import Image

    port.write(b"s")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        line = port.readline()
        if line.startswith(b"FRAME_BE "):
            _, width, height, size = line.decode("ascii").split()
            width, height, size = int(width), int(height), int(size)
            if (width, height, size) != (400, 352, 281600):
                raise RuntimeError(f"Unexpected framebuffer header: {line!r}")
            raw = bytearray()
            while len(raw) < size and time.monotonic() < deadline:
                raw.extend(port.read(size - len(raw)))
            if len(raw) != size:
                raise TimeoutError(f"Incomplete frame: {len(raw)}/{size} bytes")
            read_frame_end(port, deadline)
            pixels = np.frombuffer(raw, dtype=">u2").reshape(height, width).astype(np.uint32)
            rgb = np.stack([
                (pixels >> 11) * 255 // 31,
                ((pixels >> 5) & 63) * 255 // 63,
                (pixels & 31) * 255 // 31,
            ], axis=-1).astype(np.uint8)
            image = Image.new("RGB", (466, 466))
            image.paste(Image.fromarray(rgb), (33, 57))
            image.save(destination)
            print(f"Captured device framebuffer: {destination}")
            return
        if b"FATAL" in line or b"Guru Meditation" in line:
            raise RuntimeError(line.decode("utf-8", errors="replace"))
    raise TimeoutError("Device did not send a framebuffer header")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodem2101")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--min-fps", type=float, default=29)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    if args.seconds < 6:
        parser.error("--seconds must be at least 6 to receive a PERF report")
    if os.name != "posix":
        parser.error("Non-resetting observation currently supports macOS and Linux.")
    port = NonResettingSerial(port=None, baudrate=115200, timeout=0.5)
    port.port = args.port
    lines, rates = [], []
    try:
        port.open()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            print(line, flush=True)
            lines.append(line)
            if any(marker in line for marker in ("FATAL", "Guru Meditation", "assert failed", "Task watchdog")):
                raise RuntimeError(f"Device runtime error: {line}")
            match = re.search(r"PERF fps=([\d.]+)", line)
            if match:
                rates.append(float(match.group(1)))
        if not rates:
            raise RuntimeError("No PERF reports received; device operation is not verified.")
        if min(rates) < args.min_fps:
            raise RuntimeError(f"Measured {min(rates):.1f} fps, below {args.min_fps:.1f} fps threshold.")
        print(f"PASS: {len(rates)} reports, {min(rates):.1f}-{max(rates):.1f} fps")
        if args.capture:
            capture(port, args.capture)
    finally:
        port.close()
        if args.log:
            args.log.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
