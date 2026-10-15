# Anbu v1 (R4N) – build and install

The repository `R4N-WAV/anbu` is set up with the build recipe (`.github/workflows/build.yml`).
GitHub builds the Windows VST3 on every push; you only download and install.

To start a build by hand: **Actions** tab → *Build Anbu (Windows VST3)* → **Run workflow**.

## 1. Download
1. **Actions** tab: a run called *Build Anbu (Windows VST3)* appears (yellow dot = working, 8–15 min).
2. Green tick → click the run → scroll to **Artifacts** → click **Anbu-VST3-Windows**.
3. Right-click the downloaded zip → **Properties** → tick **Unblock** → **OK**. Unzip it.

## 2. Install
1. Copy the folder **`Anbu.vst3`** into `C:\Program Files\Common Files\VST3`.
2. Optional: delete the old `Peitho MK I.vst3` from the same folder.
3. FL Studio → **Options → Manage plugins → Find installed plugins**. Load **Anbu** on a mixer insert.

## Controls
| Control | What it does |
|---|---|
| Peak reduction | LA-2A-style amount of compression. Its tone and distortion change with it (most at the top of the range). |
| Color (fader) | Lindell-80-style colour: the tone curve plus harmonics. 25 % ≈ the Lindell default, ~80 % ≈ its THD maximum, 100 % = overkill. 0 = off. |
| Input / Output | Drive into the unit / level out. More input = more compression and more colour. |
| Mix | Parallel blend; the dry signal is time-aligned. |
| Compress / Limit | Mode, as on the LA-2A. Click the round button to switch, or click the word. |
| Noise + Noise level | Optional hiss with a little 50 Hz hum, from −120 to −60 dBFS. |
| Oversampling | Off / 2× / 4× / 8×. 4× is the normal setting. |
| Phase | Low latency (almost no delay) or Linear (cleaner phase, a few ms delay). |

All controls move in whole steps.

## Using the panel
- **Start-up:** the first time a new Anbu opens, அ lights up, grows into the logo and settles into the plate while a wave of light runs across the panel. Click anywhere to skip it. It plays once per plugin instance.
- **Knobs and fader:** drag up/down. Double-click = default. Mouse wheel works. While you move one, its name and value show in the display. The ring around a knob is dark at the knob's zero; Input and Output light from 0 dB outwards.
- **Keys:** click. The pressed key sits lower and its legend lights up.
- **Needle meter:** click *GR* or *OUT* on the meter face. GR shows gain reduction, read from right to left (rests at 0 on the right, 20 dB at the left); it is zeroed at idle like the real unit's GR-zero trim, so high Peak reduction settings read 0 with no signal. OUT shows output level with VU timing (0 on the scale = −18 dBFS RMS). The choice is saved with the project.
- **Display:** beige = output spectrum, orange = the harmonics and distortion Anbu adds, at their true level (at Peak reduction 0 and Color 0 they sit about 50 dB below the signal, so you will barely see them), white line = Anbu's tone curve at the current settings.
- **Analyser window:** the arrows in the display's corner open a larger analyser in its own window (also shows input, peak hold and a 60/90 dB range). Drag its top bar to move it, the corner to resize, × to close.
- The plugin window can be resized from the bottom-right corner.

## If something goes wrong
- **Red cross in Actions:** open the run → *windows-vst3* → the red step, copy the last ~30 lines and send them to Claude.
- **FL doesn't list Anbu:** make sure the folder is exactly `C:\Program Files\Common Files\VST3\Anbu.vst3`, then *Find installed plugins* again.

## Panel artwork
The faceplate is rendered offline from a 3D description of the panel (`tools/hw`: `hw.py` describes the surfaces and materials, `render.cpp` lights them, `assets.py` writes the images to `Resources/hw` and the geometry to `Source/HwAssets.h`). To re-render: compile `render.cpp` (`g++ -O3 -std=c++17 -pthread`), then `python3 tools/hw/assets.py <renderer> <work dir>` (numpy, scipy, Pillow with libraqm).
