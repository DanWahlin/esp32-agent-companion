# Copilot / AMOLED

**Current visual experiment:** GPT Image-generated sprite frames, reviewed locally
at **http://127.0.0.1:8765/sprite-preview.html**. The earlier image-morphing approach
below is retained for comparison, but was rejected for unnatural deformation.
Do not treat it as an approved final animation or flash it while visual review
is ongoing.

A quietly expressive desktop companion for the **Waveshare
ESP32-S3-Touch-AMOLED-1.75-B**: the supplied blue/violet Copilot artwork, deep
directional head turns, eye-leading glances, relaxed pauses,
and occasional asymmetric-timed blinks and double blinks. No scrolling text,
UI chrome, Wi-Fi, cloud service, speaker, or microSD card is required.

## Run on this Mac

```bash
cd ~/Desktop/projects/esp32-copilot
bash tools/arduino.sh build
bash tools/arduino.sh upload /dev/cu.usbmodem2101
```

**Uploading replaces the current firmware.** It does not erase the whole flash,
but the application and partition table change. Do not assume existing factory
application data remains compatible. This project never accesses the microSD or
writes to the battery charger configuration.

The deep-turn version uses the sketch's custom **11 MiB application partition**
to hold the source-derived pose atlas. It has no OTA slot and does not mount
the remaining storage partition. This differs from the initial 3 MiB demo layout.

The scripts use the existing Arduino IDE CLI, its configuration, and Waveshare's
bundled GFX library. Overrides: `ARDUINO_CLI`, `ARDUINO_CONFIG`, `WAVESHARE_DIR`.
Do not substitute a generic display library: the vendor version includes the
CO5300 panel driver and QSPI path this board needs.

### Reproduce the toolchain

- Arduino ESP32 core **3.3.10**
- Waveshare repository `https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75`
- Vendor revision used: `e4344e70c2fa78a13e8a06566507f1ba8af6672a`
- Library: `examples/arduino/libraries/GFX_Library_for_Arduino`, version 1.6.4

On another machine, install Arduino CLI, configure the Espressif board URL,
install the pinned core, and clone the vendor repository:

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.10
git clone https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75.git .deps/waveshare
git -C .deps/waveshare checkout e4344e70c2fa78a13e8a06566507f1ba8af6672a
```

Set the three path overrides before running the build script. `ARDUINO_CONFIG`
should point to the configuration created by `arduino-cli config init`.
Python 3 is used for the core-version check; no Python packages are needed
to compile or upload the checked-in firmware assets.

### Arduino IDE settings

Open `firmware/Copilot/Copilot.ino`. Make the vendor GFX library available
to your sketchbook, then select:

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| CPU | 240 MHz |
| Flash | QIO 80 MHz, 16 MB |
| PSRAM | OPI PSRAM |
| Partition | Custom: sketch `partitions.csv` (11 MiB application) |
| USB mode | Hardware CDC and JTAG |
| USB CDC on boot | Enabled |
| Arduino core / event core | 1 / 1 |
| Erase all flash | Disabled |

Hardware: 466 x 466 CO5300 AMOLED; QSPI CS=12, CLK=38, D0..D3=4..7,
RESET=39; vendor column offset=6. The `-B` variant is the standard board
with a protective case. USB data uses GPIO19/20 and is not repurposed.

Before the first IDE build, run `python3 tools/embed_atlas.py`. The CLI build
does this automatically. The CLI workflow also supplies the 11 MiB size limit;
it is the recommended build/upload path for the custom partition layout.

## Preview and development

### GPT Image sprite-sheet experiment

`tools/generate_sprite_sheet.py` reads `AI_IMAGE_ENDPOINT`, `AI_IMAGE_API_KEY`
and `AI_IMAGE_MODEL` from `~/.env` at runtime. It sends the original supplied
artwork to the configured Azure image-edit endpoint as a visual reference.
Credentials are never copied into this project or logged.

```bash
python3 tools/generate_sprite_sheet.py
python3 tools/prepare_generated_sprites.py
```

The selected second pass uses an explicit angle/layout guide:

```bash
python3 tools/create_sprite_guide.py
python3 tools/generate_sprite_sheet.py \
  --prompt assets/sprite-prompts/right-turn-refined.txt \
  --guide assets/generated-sprites/right-turn-layout.png \
  --output assets/generated-sprites/right-turn-refined-sheet.png
python3 tools/prepare_generated_sprites.py \
  --source assets/generated-sprites/right-turn-refined-sheet.png
```

Requests use 1536 x 1024, high quality, PNG, one image per request. Original
generated sheets, prompts, provenance, and hashes are retained under
`assets/generated-sprites/` and `assets/sprite-prompts/`.

The sprite review page defaults to one generated frame at a time, using the exact
reverse sequence to return to center. It never applies image warping.
Registration only translates and uniformly scales each whole sprite to correct
camera drift; it does not reshape the head. The expanded review now includes
**right, left, up and down**, with 24 poses per direction and one identical
approved center image. Vertical tracks preserve apparent width rather than
forcing constant height, retaining pitch foreshortening.

The same preview also includes **up-left, up-right, down-left and down-right**
as newly generated, reference-guided GPT Image sprite sheets. Each has 24
combined yaw/pitch poses and localized blink variants, using the same approved
center as the cardinal tracks. The former firmware-atlas diagonals were rejected
for visible morphing and are no longer referenced by the current manifest.
Raw generations, prompts, guides and API provenance remain under `assets/`.

All eight directions are available in buttons, the queued track selector,
random playback and the review cycle. Straight up/down remains capped at 50%;
the new diagonals use their full generated sequences. Requested diagonal angles
run up to 46 degrees yaw and 23 degrees pitch; these are prompt targets, not
measured rotations. The preview displays the current track's artwork source.

The player adds randomized look-around timing, queued direction requests,
complete reverse returns, and an all-direction review cycle. Track changes
occur only at the shared center. Long stalls do not fast-forward a gesture;
playback advances at most one pose per rendered update. Pause, frame inspection,
timing changes and resume retain the selected pose.
Up/down playback uses 50% of the original travel range in random, queued and
review modes (through frame 12 of 24, rounded down to a whole sprite).
Left/right travel is unchanged. All images remain intact and available for
explicit inspection, including the deeper vertical poses.
The duration control specifies a full-range turn. Shorter looks scale duration
by the square root of their relative travel, so reduced vertical motion does
not stretch fewer poses over the same long interval. Fractional timing carries
between adjacent poses, avoiding refresh-rate-dependent rounding at every step.
Sprites are decoded before playback; unchanged telemetry and controls are not
rewritten on every animation tick.

**Crossfade between frames** is an optional, default-off browser control.
It blends the current and adjacent pose at the same eyelid level, using the
existing motion progress; toggling it never restarts or changes the timeline.
Only the current direction is blended, and vertical travel limits still apply.
Pause freezes the blend; explicit frame inspection shows an unblended original.
Screenshots capture the displayed blend, and copied feedback records its setting
and frame weights. This is opacity blending, not shape morphing: it can reduce
stepping but may introduce ghosted edges. No image assets or firmware are changed.

Generate and stage diagonal sheets without regenerating cardinal images:

```bash
python3 tools/create_diagonal_guides.py
# Repeat the generation command for up_right, up_left, down_right, down_left.
# Generation is a paid API operation; preparation/review below is local.
python3 tools/generate_sprite_sheet.py \
  --reference assets/generated-sprites/approved-center.png \
  --guide assets/generated-sprites/up_right-generated-layout.png \
  --prompt assets/sprite-prompts/up_right-generated.txt \
  --output assets/generated-sprites/up_right-generated-sheet.png
python3 tools/prepare_generated_diagonals.py
SPRITE_ASSETS=build/generated-diagonals python3 -m unittest discover -s tests -p 'test_sprite_assets.py'
# After inspecting the contact sheets and blink masks:
python3 tools/prepare_generated_diagonals.py --publish
```

Preparation isolates colored heads from neutral sheet labels, then applies only
uniform scaling and translation. Eye components are matched between adjacent
poses to avoid blinking a nearby goggle rim instead. The old image files stay
intact; new filenames are copied first and the manifest is replaced last.
The full `prepare_sprite_animation.py` pipeline now uses generated diagonals,
never the native exporter. `prepare_device_diagonals.py` is retained only as a
superseded experiment; do not run it to rebuild the current review.

All generated tracks use four pre-baked eyelid closure levels around each visible eye,
not another AI redraw of the whole head. Pixels outside the eye neighborhoods
and the outer silhouette stay unchanged. At the deepest downward angles the
eyes are hidden or clipped against the visor rim; those frames intentionally
reuse the open image rather than painting over the rim. There are occasional
double blinks, plus manual blink and automatic-blinking controls.
Blink timing is independent of head motion: a blink never pauses a turn, and
reopening displays the current pose rather than restoring an earlier one.

Rebuild the full animation assets after generating the directional sheets:

```bash
python3 tools/create_direction_guides.py
# For each direction: left, up, down. Image generation is a paid API operation.
# Left uses only the approved center and its guide; up/down also use
# --reference assets/generated-sprites/right-turn-refined-sheet.png.
python3 tools/generate_sprite_sheet.py \
  --reference assets/generated-sprites/approved-center.png \
  --guide assets/generated-sprites/left-turn-layout.png \
  --prompt assets/sprite-prompts/left-turn.txt \
  --output assets/generated-sprites/left-turn-sheet.png
python3 tools/prepare_sprite_animation.py
python3 -m unittest discover -s tests -p 'test_sprite_assets.py'
```

`web/generated-sprites/animation.json` contains direction/frame references, eye
bounds, closure images and the shared-center hash. The raw left-sheet first
attempt is retained as `left-turn-rejected-wrong-direction.png`: it turned right
despite its instructions and was not selected. Prompt angle labels describe
requested angles, not measured physical rotations.

These expanded sprites are **browser-review assets only**. The device still
runs the previous firmware; the browser PNG asset directory is not
an ESP32-ready flash package and needs a separate compact export.

GPT Image does not guarantee temporal identity or precise angular increments.
This is a quality experiment, not a claim that generative images alone solve
smooth character animation. Judge the actual neighboring frames in the preview.

### Local server and retained morph experiment

```bash
python3 tools/serve_preview.py
```

Open **http://127.0.0.1:8765** for the current generated-sprite review.
The superseded experiment remains at `/morph-studio.html`. That Motion Studio runs the actual C++ firmware
motion engine and renderer in a persistent native process; the browser displays
its RGB565 frames. It is not an approximation of the animation in JavaScript.

Controls include eight queued look directions, automatic looking, an
all-directions round-trip test, quarter/half speed, pause, single-frame stepping,
and a separate paused pose/blink inspector. Look requests complete the current
gesture and pass through center rather than cutting to the requested pose.
The turn-depth trace and largest-frame-step readout help locate discontinuities.
Replay a seed, save a screenshot, or copy feedback with the exact simulation time.
Each browser tab gets its own native animation process, so opening a second
preview cannot reset or change the first tab's motion.

The server binds only to loopback and needs Python 3, `clang++`, and the host's
standard zlib library, but no Python packages. `--port` selects another port.
Stop it with Ctrl+C. Browser preview validates motion and image transitions,
not physical AMOLED scanout or ESP32 frame throughput.

### Offline preview and asset regeneration

Open `preview/index.html` directly in a browser. The animated WebP is an
18-second **deterministic sample of the actual C++ renderer**, not a separate
JavaScript approximation. It is offline, honors reduced-motion preferences,
and includes a pause button. Device randomness is seeded from `esp_random()`,
so the device will not repeat the preview sequence.
`preview/turn-contact-sheet.png` and `preview/turn-directions.webp` show the
full pose range separately from the random animation.

To regenerate the image assets or preview:

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements-art.txt
.venv/bin/python tools/prepare_turn_atlas.py
.venv/bin/python tools/preview.py
bash tools/test.sh
```

Preview generation also requires `clang++` (Xcode Command Line Tools on macOS),
and temporarily writes approximately 152 MB of raw frames inside `build/`.
The compressed pose atlas and its metadata are included; ordinary builds do not need
asset-generation dependencies.

## Animation and rendering

- Uses the frontal, three-quarter, profile, upward and downward views from
  the supplied sheet. Left-facing views are mirrored. Pale backgrounds are
  removed while preserving the blue/violet shading and antialiased edges.
- Deep looks reveal the side shell, top dome, and underside rather than simply
  distorting a front-facing texture. Landmark-aligned image morphs synthesize
  33 poses in each of eight directions. Neighboring poses are blended at
  runtime for continuous motion between stored samples.
- This is **multi-view image interpolation**, not a fully reconstructed 3D
  model. Missing geometry is approximated between reference views; there is
  no claim of physically exact hidden surfaces or measured yaw/pitch angles.
- Independent emissive eyes follow per-pose position, size, angle, and visibility,
  so the far eye disappears during a deep side turn. Blinks close quickly, hold briefly, and reopen
  more slowly, with occasional double blinks and blink-on-turn behavior.
- Quintic easing gives head and eyes zero velocity and acceleration at the
  ends of each move. Eyes lead the head by about 90 ms. Weighted randomized
  targets include left, right, up, down and diagonals. Changes of direction
  pass naturally through center instead of abruptly switching view tracks,
  with varied dwell times rather than a metronomic loop.
- Pose images use per-frame RGB565 palettes and zlib compression. Two neighboring
  decoded index buffers and palettes live in internal SRAM. The ESP32's ROM
  decompressor and a persistent workspace avoid allocating during animation.
- Two complete, display-byte-order RGB565 frame buffers live in PSRAM
  (about 550 KiB). Core 0 decodes and composites the head and eyes.
  Core 1 uploads completed frames through a preallocated DMA staging buffer.
  The display never receives a partially rendered software buffer.
- Animation uses monotonic 64-bit time, not frame counts. The target is
  **30 fps**. Actual sustained throughput is
  reported over USB serial.
  The vendor driver does not synchronize transfers to panel TE, so software
  double buffering does not constitute a guarantee of panel-level tear-free
  scanout.
- Playback advances by at most one nominal frame per rendered image. A delayed
  frame or diagnostic capture therefore slows/pauses the gesture rather than
  skipping ahead to center when rendering resumes.
- No per-frame allocation, blocking animation delays, or flash writes.
  Brightness fades in at startup.

`firmware/Copilot/build_opt.h` enables `-O3` and the vendor QSPI chunk size for
both CLI and Arduino IDE builds. Do not remove it when copying the sketch.

Tune `firmware/Copilot/src/Config.h`: brightness (0..255), target frame rate,
move and hold ranges, blink timing, and SPI clock. Turn depth comes from the
atlas endpoints and `Motion.cpp`'s target magnitude, not a 2D warp angle.
The pose landmarks and eye locations are calibrated to this supplied artwork.
This is an animation-only app; touch and IMU are not initialized.

## Diagnostics and recovery

```bash
bash tools/arduino.sh monitor /dev/cu.usbmodem2101
```

Every five seconds, `PERF` reports displayed fps, rendering and transfer time,
maximum render time, and free PSRAM. `STAGES` breaks down the last frame's
decompression, compositing, and eye rendering time.
Allocation, panel startup,
and renderer-stall errors print `FATAL` rather than silently continuing.
If a panel is unstable at 80 MHz, lower `kSpiFrequency` to 40000000 and rebuild.

For tooling, sending the single serial byte `s` captures a completed framebuffer:
ASCII `FRAME_BE 400 352 281600\n`, followed by exactly 281600 big-endian RGB565
bytes, then `\nEND_FRAME`. Place the captured region at (33, 57) on a black
466 x 466 canvas. Diagnostic capture temporarily interrupts animation while
USB transfers the frame; do not request it continuously.

A bounded health check and PNG capture are also included:

```bash
.venv/bin/pip install -r requirements-device.txt -r requirements-art.txt
.venv/bin/python tools/device.py --seconds 60 --capture preview/device-capture.png
```

The command fails on runtime errors, missing telemetry, or five-second
frame-rate reports below 29 fps. Capturing a frame proves the MCU's rendered
output, not the physical panel's orientation, brightness, or scanout quality.
On macOS/Linux this observer leaves DTR/RTS untouched and disables hang-up
line dropping, so opening/closing it does not intentionally reset the ESP32.
Ordinary serial tools that toggle those lines can reboot the board and make
the character suddenly reappear at center.

If the board is not detected, use `arduino-cli board list` to find its new port.
Close any serial monitor before uploading. If necessary, hold BOOT while
resetting, release BOOT, and upload to the newly appearing USB port.

The existing Waveshare repository contains factory-recovery documentation and
images. Use its exact full-image recovery procedure for this board rather than
flashing a factory image through this project's application-upload command.
This project does not modify that repository or its factory image.

## Artwork

Source: [GitHub brand mascot sheet](https://brand.github.com/_next/static/media/mascots-02.51566f45.png).
The source image is preserved in `assets/copilot-source.png`.
SHA-256: `a93a701f4ce2d99129735fee1f19812646fd06d19b4ed248b89e3e83f159d31e`.

GitHub Copilot artwork and marks belong to GitHub. This project does not
relicense them or imply endorsement. Review the applicable
[GitHub brand guidance](https://brand.github.com/) before distributing or
publishing the artwork or modified animations.
