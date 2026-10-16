# Anbu v1 (R4N) – install, use and build

Anbu is an optical-style leveling amplifier with a colour stage, made mainly for vocals.
It is a 64-bit VST3 plugin for Windows (10 or 11). It is developed and tested in FL Studio,
and works in any VST3 host.

## Install
1. Get the plugin: on GitHub open **Actions** → the latest green *Build Anbu (Windows VST3)* run →
   **Artifacts** → **Anbu-VST3-Windows** (a zip).
2. Right-click the zip → **Properties** → tick **Unblock** → **OK**. Unzip it.
3. Copy the folder **`Anbu.vst3`** into `C:\Program Files\Common Files\VST3`.
4. In your host, rescan plugins (FL Studio: **Options → Manage plugins → Find installed plugins**) and load **Anbu** on an insert.

## Controls
| Control | What it does |
|---|---|
| Peak reduction | Amount of compression, opto style. Tone and distortion change with it (most at the top of the range). |
| Color (fader) | Colour stage: a tone curve plus harmonics. 25 % = classic, about 80 % = strong, 100 % = overkill. 0 = off. |
| Input / Output | Drive into the unit / level out (±24 dB). More input = more compression and more colour. |
| Mix | Parallel blend with the dry signal (time-aligned). |
| Compress / Limit | Mode. Click the round button to switch, or click the word. |
| Noise + Noise level | Optional hiss with a little 50 Hz hum, from −120 to −60 dBFS. Every instance hisses differently. |
| Oversampling | Off / 2× / 4× / 8×. 4× is the normal setting. |
| Bypass | The small key under Noise level (also linked to your host's bypass). Passes the input through with the same delay, crossfaded so it never clicks. |
| Phase | Low latency (a few samples of delay) or Linear (cleaner phase, about 1.3 ms of delay at 48 kHz). |

All controls move in whole steps. The plugin reports its delay to the host, also when bypassed, so tracks stay in time.

## Using the panel
- **Start-up:** the first time a new Anbu opens, அ lights up, grows into the logo and settles into the plate while a wave of light runs across the panel. Click anywhere to skip it. It plays once per plugin instance.
- **Knobs and fader:** drag up/down. Double-click = default. Mouse wheel works. While you move one, its name and value show in the display. The lamp ring around a knob follows its value and is dark at the knob's zero; Input and Output light from 0 dB outwards.
- **Keys:** click. The pressed key sits lower and its legend lights up.
- **Bypass:** the colour drains out of the panel into the Bypass key; switching back pours it out again (under a second each way).
- **Needle meter:** click *GR* or *OUT* on the meter face. GR shows gain reduction, read from right to left (rests at 0 on the right, 20 dB at the left). It is zeroed at idle, so high Peak reduction settings read 0 with no signal. OUT shows output level with VU timing (0 on the scale = −18 dBFS RMS). The choice is saved with the project.
- **Display:** beige = output spectrum; orange = the harmonics and distortion Anbu adds, at their true level (at Peak reduction 0 and Color 0 they sit about 50 dB below the signal); white line = Anbu's tone curve at the current settings.
- **Analyser window:** the arrows in the display's corner open a larger analyser in its own window (also input, peak hold and a 60/90 dB range). Drag its top bar to move it, the corner to resize, × to close.
- The plugin window can be resized from the bottom-right corner.

## Troubleshooting
- **The host doesn't list Anbu:** check that the folder is exactly `C:\Program Files\Common Files\VST3\Anbu.vst3`, that the zip was unblocked before unzipping, then rescan.
- **Windows blocks the plugin:** the plugin is not code-signed; unblocking the zip (step 2) avoids the warning.

## Building from source
Requirements: CMake 3.22+, a C++17 compiler (Visual Studio 2022 on Windows). JUCE 8.0.8 is fetched automatically.

```
cmake -S . -B build -DANBU_BUILD_TOOLS=ON
cmake --build build --config Release --target Anbu_VST3 AnbuStress
build/AnbuStress_artefacts/Release/AnbuStress      # robustness checks (sample rates, block sizes, latency, state ...)
```

GitHub Actions builds the plugin on every push, runs the robustness checks and validates the VST3 with pluginval
(`.github/workflows/build.yml`). Other tools (`-DANBU_BUILD_TOOLS=ON`, `-DANBU_BUILD_SNAPSHOT=ON`):
`AnbuRender` runs the processor over a raw float32 stereo file; `AnbuSnapshot` renders the panel to PNG.

## Source layout
- `Source/DSP/AnbuCore.h` – the DSP (control path, audio path, colour stage, 3D side lift, noise); `AnbuConstants.h` holds the fitted model constants.
- `Source/PluginProcessor.*` – parameters, oversampling, mix, meters, state.
- `Source/PluginEditor.*` – the panel; `Source/HwAssets.h` – geometry of the rendered art.
- `Resources/hw` – rendered panel art; `Resources/ui` – fonts (Catamaran and Barlow Semi Condensed, SIL Open Font License, see `Resources/FONT_LICENSE_*.txt`).
- `tools/hw` – the panel renderer (below).

## Panel artwork
The faceplate is rendered offline from a 3D description of the panel: `tools/hw/hw.py` describes the surfaces and materials,
`render.cpp` lights them (soft shadows, ambient occlusion, studio reflections), and `assets.py` writes the images to
`Resources/hw` and the geometry to `Source/HwAssets.h`. To re-render: compile `render.cpp` (`g++ -O3 -std=c++17 -pthread`),
then `python3 tools/hw/assets.py <renderer> <work dir>` (needs numpy, scipy and Pillow with libraqm). A full render takes about 10 minutes.
