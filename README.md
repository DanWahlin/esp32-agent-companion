<p align="center">
  <img src="images/logo.png" alt="A thin round ESP32 display with a friendly face and microcontroller graphic" width="400">
</p>

# ESP32 Agent Companion

A small, expressive companion for your desk. Its first character is inspired by
GitHub Copilot and comes to life on a round AMOLED touchscreen: it looks around,
blinks, reacts when tapped, and can display Working, Complete, and Needs attention
states. After two uninterrupted Idle minutes, it sleeps for one minute and then
repeats the Idle/Sleep cycle.

It runs locally on the **Waveshare ESP32-S3-Touch-AMOLED-1.75-B or 1.75-C**.
No Wi-Fi, cloud account, subscription, or microSD card is needed to run the
built-in character.

## What it does

- **Natural motion:** smooth 30 FPS playback, eight looking directions, and occasional blinks.
- **Touch reactions:** tap the character for a quick spring-like recoil and widened eyes,
  then return to Idle.
- **On-device settings:** swipe up to adjust brightness, sound, character, or state.
- **Subtle sound cues:** original local cues accompany Working, Needs attention, Complete,
  Surprise, and settings actions when a speaker is attached.
- **Agent states:** focused eyes and orbiting dots for Working, a celebration for Complete,
  and curious head tilts for Needs attention.
- **Automatic sleep:** after two Idle minutes, the character sleeps for one minute
  with subtle movement, drowsy eyelids, and drifting Zs.
- **Compact graphics:** lossless sprite compression preserves the artwork while keeping
  the current sprite pack around 9.53 MB.
- **Optional OpenClaw character:** a validated, read-only microSD sprite pack can be
  selected from Settings, with built-in Copilot assets as the automatic fallback.

The character works immediately in automatic Idle mode. Agent states can be
controlled through USB serial commands, the swipe-up settings menu, or the
included Copilot CLI daemon. Wireless daemon transports are not implemented.

### Optional: Copilot CLI companion daemon

The TypeScript daemon connects Copilot CLI lifecycle hooks to the device over USB.
It supports macOS and Linux directly, plus Windows through WSL 2. Install
[Node.js 22.12 or newer](https://nodejs.org/) and flash the device first.

**macOS or Linux**

```bash
cd daemon
npm ci
npm run install:daemon
npm run status
```

The installer creates and starts a macOS LaunchAgent or Linux systemd user
service. If Linux reports a serial-port permission error, add your user to the
`dialout` group, then sign out and back in:

```bash
sudo usermod -aG dialout "$USER"
```

**Windows through WSL 2**

Run **Copilot CLI and the daemon inside the same WSL 2 distribution**; hooks
installed in WSL cannot control a Copilot CLI process running natively on
Windows. Install Node.js inside WSL. If systemd is not enabled, add the following
to `/etc/wsl.conf`, then run `wsl --shutdown` from PowerShell and reopen WSL:

```ini
[boot]
systemd=true
```

Windows does not expose USB devices to WSL automatically, so install
[`usbipd-win`](https://learn.microsoft.com/windows/wsl/connect-usb), connect the
device, and open PowerShell:

```powershell
usbipd list
# Run once in an Administrator PowerShell, using the ESP32's BUSID:
usbipd bind --busid <BUSID>
# Run whenever the device needs to be attached to WSL:
usbipd attach --wsl --busid <BUSID>
```

In WSL, confirm that `/dev/ttyACM*` or `/dev/ttyUSB*` exists, then run the same
Linux installation commands shown above. While attached to WSL, the device is
not available to native Windows applications.

Restart Copilot CLI after installation so it loads the user-level hooks.
`npm run status` should report `"connected": true`. The daemon maps active
main-agent or subagent work to Working, permission and
elicitation prompts to Needs attention, verified tool-using turns to Complete,
and inactive sessions to Idle. Multiple CLI sessions are aggregated rather than
overwriting one another. Timestamped leases discard abandoned work, while a
private local state file restores still-active sessions after a daemon restart.

<p align="center">
  <img src="preview/agent-companion-demo.gif" alt="ESP32 Agent Companion cycling through Idle, Surprise, Working, Needs attention, and Complete states" width="400">
</p>

## Install

### 1. Get the device and download a release

You need:

- **Waveshare ESP32-S3-Touch-AMOLED-1.75-B or 1.75-C:** [Buy from Waveshare](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm?sku=31262)
  or [Amazon](https://www.amazon.com/dp/B0FBWDL117).
- A **USB data cable** and a macOS, Windows, or Linux computer.
- **[Python 3.10 or newer](https://www.python.org/downloads/)**. On Windows, include
  the Python launcher when installing.

From [Releases](https://github.com/DanWahlin/esp32-agent-companion/releases/latest), download
the file ending in **`-firmware.zip`** and extract it. You do **not** need Arduino IDE,
Arduino CLI, or the source repository to install a release.

Prefer to build it yourself? Follow the [source-build guide](docs/build-from-source.md).

### 2. Install the flashing software

Open a terminal **inside the extracted firmware folder**—the folder containing
`flash.py`, `manifest.json`, and `requirements.txt`.

**macOS / Linux**

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

**Windows PowerShell**

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

This installs the pinned `esptool` flashing utility into a local environment.
Some Linux distributions also require their `python3-venv` package.

### 3. Connect and flash

Connect the device using the USB data cable. Close any serial monitor using it.
List the ports, then flash the port corresponding to your device:

**macOS / Linux**

```bash
.venv/bin/python flash.py --list-ports
.venv/bin/python flash.py --port /dev/cu.usbmodem2101
```

**Windows PowerShell**

```powershell
.\.venv\Scripts\python.exe flash.py --list-ports
.\.venv\Scripts\python.exe flash.py --port COM5
```

Replace the example port with the one listed for your board. Linux ports commonly
look like `/dev/ttyACM0`; macOS ports commonly look like `/dev/cu.usbmodem...`.

**Confirm when prompted.** The installer checks the bundle's hashes and programs
the matching bootloader, partition table, application, and sprite `.bin` files at
their correct addresses. Do not upload the application `.bin` alone or mix files
from different releases.

**Flashing replaces the device's existing firmware and partition table.** Back up
anything needed from its previous firmware first. The installer does not run a
whole-chip erase or modify your microSD card.

When flashing finishes, the device restarts and the character begins looking
around. Tap it to try the touch reaction; it returns to Idle when the spring
finishes. Swipe up to open settings. The menu adjusts display brightness and
sound volume from Off through 100%, and chooses Idle, Surprise, Working, Complete,
or Needs attention. The 50% default and subsequent volume changes persist across
restarts. Swipe down or tap Close to return to the character.

Sound uses the board's ES8311 codec and two-pin speaker output. Connect a compatible
speaker to the board's speaker connector if your device or enclosure does not
include one. Audio stays local to the device and is not read from or written to
the microSD card.

If the device is not listed, check that the cable supports data. For connection or
download-mode problems, see the [Waveshare instructions](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75).

### Optional: add OpenClaw on microSD

The built-in Copilot character works without a card. OpenClaw uses microSD so
both full-resolution packs do not compete for internal flash.

1. Insert a **FAT32** microSD card. A 64 GB FAT32 card has been tested.
2. Connect the device to the Mac or Linux computer over USB.
3. From the repository root, run:

   ```bash
   npm --prefix daemon run install:openclaw
   ```

   Set `AGENT_COMPANION_PORT` or append
   `-- --port /dev/cu.usbmodem2101` when automatic discovery is ambiguous.
   An explicitly selected port must exist; the installer will not silently use
   a different connected ESP32.
   The Node installer pauses the local daemon, streams the pack to a temporary
   SD file, verifies it on the device, atomically installs it, reboots, and
   restarts the daemon.
4. Swipe up on the display and choose **OpenClaw** under **Character**.

The firmware never formats the card or writes during normal operation, and the
device does not expose it as a USB drive. It writes only during an explicit Node installation. The
transfer uses a temporary file and replaces the active pack only after validating
its exact size and SHA-256. At boot the firmware validates the pack again before
enabling its bounded PSRAM cache. Missing, incompatible, slow, or removed cards
leave or return the device to built-in Copilot.

As a manual fallback, download **`-sd-card.zip` from the same release**, extract
its `characters` folder at the FAT32 card root, and reinsert the card while the
device is powered off.

## Develop and customize

The native browser preview uses the same C++ motion and rendering code as the
device. Source artwork, blink generation, and firmware export tools are included.

### Character Lab

Use Character Lab to review every character state and animation locally before
uploading firmware to the device. Switch between Copilot and OpenClaw while
preserving the same native 13-track motion contract, blink levels, and effects.
OpenClaw is built offline from a procedural 3D model based on the official SVG
and runs from a validated microSD pack on the device.
Character Lab requires Python 3.10+,
`clang++`, and zlib; see the [source-build guide](docs/build-from-source.md) for
platform prerequisites.

From the repository root, run:

```bash
python3 tools/serve_preview.py
```

Then open [http://127.0.0.1:8765/character-preview.html](http://127.0.0.1:8765/character-preview.html).
Each browser tab runs an isolated native renderer, with controls for character
selection, pausing, playback speed, touch reactions, and each character state.

To regenerate the OpenClaw sprites after changing its 3D source model:

```bash
npm ci --prefix tools/openclaw
npm run render --prefix tools/openclaw
python3 tools/export_openclaw_lab.py
```

The export produces the committed, losslessly compressed RGB565 OpenClaw pack
and firmware metadata. The individual intermediate PNG frames are generated
locally and ignored.

<p align="center">
  <img src="images/character-lab.webp" alt="Character Lab showing the native Copilot character preview and state controls" width="1000">
</p>

- [Build from source](docs/build-from-source.md)
- [Development, preview, hardware, and serial commands](docs/development.md)
- [Tagging and publishing releases](docs/releases.md)

This is an independent project, not an official GitHub or Waveshare product.
GitHub Copilot artwork and product names belong to their respective owners.
