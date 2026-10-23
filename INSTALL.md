# Anbu v1 (R4N) – install, use and build

Anbu is an optical-style leveling amplifier with a colour stage, made mainly for vocals.

- **Windows** (10 or 11): VST3, 64-bit. Developed and tested in FL Studio; works in any VST3 host.
- **Mac** (macOS 10.13 or newer, Apple Silicon and Intel): VST3 and Audio Unit (AU), for Logic Pro,
  GarageBand, Ableton Live, FL Studio, Cubase, Studio One, Reaper, Bitwig and other hosts.

Downloads: `Anbu-VST3-Windows.zip` and `Anbu-macOS.zip`, from the
[latest release](https://github.com/R4N-WAV/anbu/releases/tag/latest) on GitHub.

## Install on Windows
1. Right-click the zip → **Properties** → tick **Unblock** → **OK**. Unzip it.
2. Copy the folder **`Anbu.vst3`** into `C:\Program Files\Common Files\VST3`.
3. In your host, rescan plugins (FL Studio: **Options → Manage plugins → Find installed plugins**) and load **Anbu** on an insert.

## Install on Mac
Anbu isn't notarized by Apple (that needs a paid Apple developer account), so the first time macOS
asks you to allow it. This is a one-time step.

1. Double-click `Anbu-macOS.zip` to unzip it, then double-click **Install Anbu.pkg**.
2. macOS says it can't verify the installer: click **Done** (or **OK**). Open **System Settings → Privacy & Security**,
   scroll down to *“Install Anbu.pkg” was blocked* and click **Open Anyway**, then confirm with your password.
   (macOS 14 Sonoma and older: you can instead right-click the pkg → **Open** → **Open**.)
3. Click through the installer. It installs both formats for all users:
   `/Library/Audio/Plug-Ins/VST3/Anbu.vst3` and `/Library/Audio/Plug-Ins/Components/Anbu.component`
   (under **Customize** you can leave one out).
4. Restart your host or rescan plugins and load **Anbu** (listed under **R4N**) on an insert.
   - Logic Pro / GarageBand: restart the app. Still missing: Logic Pro → Settings → Plug-in Manager → select Anbu → **Reset & Rescan Selection**.
   - FL Studio: **Options → Manage plugins → Find installed plugins**.
   - Ableton Live: Settings → Plug-Ins → turn on **Use VST3 Plug-In System Folders** → **Rescan**.

To uninstall, delete those two files (Finder: **Go → Go to Folder…**, `/Library/Audio/Plug-Ins`).

## Controls
| Control | What it does |
|---|---|
| Peak reduction | Amount of compression, opto style. Tone and distortion change with it (most at the top of the range). |
| Color (fader) | Colour stage: a console tone curve plus harmonics. Up to 25 % it is the classic console character; above that a smooth, level-dependent saturation takes over (2nd harmonic first, each higher one much quieter, quiet details stay clean). 100 % = full character. 0 = off. |
| Input / Output | Drive into the unit / level out (±24 dB). More input = more compression and more colour. |
| Mix | Parallel blend with the dry signal (time-aligned). |
| Compress / Limit | Mode. Click the round button to switch, or click the word. |
| Noise + Noise level | Optional hiss with a little 50 Hz hum, from −120 to −60 dBFS. Every instance hisses differently. |
| Oversampling | Off / 2× / 4× / 8×. 4× is the normal setting. |
| Bypass | The small key under Noise level (also linked to your host's bypass). Passes the input through with the same delay, crossfaded so it never clicks. |
| Phase | Low latency (a few samples of delay) or Linear (cleaner phase, about 1.3 ms of delay at 48 kHz). |
| Auto Gain | The rocker under Phase. Matches what comes out to what went in, in loudness (measured like a loudness meter, ITU-R BS.1770), so you hear what Anbu does, not that it is louder or quieter. It re-matches about half a second after you change something and then holds still, so it does not undo the compression. Output still works as a trim on top. The display shows the correction. |

All controls move in whole steps. The plugin reports its delay to the host, also when bypassed, so tracks stay in time.

## Presets
- The strip at the top: **‹ ›** step through the presets, click the **display** for the menu, **SAVE** to save the current sound under a name (type it in the display, Enter to save, Esc to cancel).
- The menu has the factory presets by category (Vocals, Instruments, Mix & Master, Color, Creative), your own presets, *Save as…*, *Save* / *Delete* for your own preset, and *Show preset folder*.
- A `*` after the name means you changed something since loading it.
- A preset holds the sound (Peak reduction, Color, Input, Output, Mix, mode, noise). Oversampling, phase, Auto Gain and Bypass stay as they are.
- The factory presets are set up for typical levels (a vocal around −18 LUFS): Output is set so a preset comes out about as loud as it went in.
- Your presets are files in your Documents folder, `R4N/Anbu/Presets` (Windows: `Documents\R4N\Anbu\Presets`, Mac: `~/Documents/R4N/Anbu/Presets`), one `*.anbupreset` file each. Sub-folders there show up as categories; copy the files to share them (they work on both systems).

## Using the panel
- **Start-up:** the first time a new Anbu opens, அ lights up, grows into the logo and settles into the plate while a wave of light runs across the panel. Click anywhere to skip it. It plays once per plugin instance.
- **Knobs and fader:** drag up/down. Double-click = default. Mouse wheel works. While you move one, its name and value show in the display. The lamp ring around a knob follows its value and is dark at the knob's zero; Input and Output light from 0 dB outwards.
- **Keys:** click. The pressed key sits lower and its legend lights up.
- **Bypass:** the colour drains out of the panel into the Bypass key; switching back pours it out again (under a second each way). It works on a still of the panel, so it costs only a few milliseconds per frame.
- **Needle meter:** click *GR* or *OUT* on the meter face. GR shows gain reduction, read from right to left (rests at 0 on the right, 20 dB at the left). The scale is progressive like on studio GR meters: every dB from 0 to 6 is numbered and wide, so 2, 3 or 4 dB of reduction reads at a glance. It is zeroed at idle, so high Peak reduction settings read 0 with no signal. OUT shows output level with VU timing (0 on the scale = −18 dBFS RMS). The choice is saved with the project.
- **Display:** beige = output spectrum; orange = the harmonics and distortion Anbu adds, at their true level (at Peak reduction 0 and Color 0 they sit about 50 dB below the signal); white line = Anbu's tone curve at the current settings.
- **Analyser window:** the arrows in the display's corner open a larger analyser in its own window (also input, peak hold and a 60/90 dB range). Drag its top bar to move it, the corner to resize, × to close.
- The plugin window can be resized from the bottom-right corner.

## Troubleshooting
**Windows**
- **The host doesn't list Anbu:** check that the folder is exactly `C:\Program Files\Common Files\VST3\Anbu.vst3`, that the zip was unblocked before unzipping, then rescan.
- **Windows blocks the plugin:** the plugin is not code-signed; unblocking the zip (step 1) avoids the warning.

**Mac**
- **The host doesn't list Anbu:** restart the host (Logic Pro and GarageBand only look for new Audio Units at start-up), then rescan.
  Still missing: log out of macOS and back in, which refreshes the Audio Unit list.
- **No “Open Anyway” button:** it shows up in Privacy & Security for about an hour after macOS blocked the installer.
  Double-click the pkg again, then look again.
- **“Anbu is damaged and can't be opened” or “Apple could not verify Anbu”:** this shouldn't happen with the installer.
  If it does, run in Terminal:
  `sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Anbu.vst3 /Library/Audio/Plug-Ins/Components/Anbu.component`

## Building from source
Requirements: CMake 3.22+, a C++17 compiler (Visual Studio 2022 on Windows, Xcode or its command line tools on Mac).
JUCE 8.0.8 is fetched automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DANBU_BUILD_TOOLS=ON
cmake --build build --config Release --target Anbu_VST3 AnbuStress        # Mac: add Anbu_AU
build/AnbuStress_artefacts/Release/AnbuStress      # robustness checks (sample rates, block sizes, latency, state ...)
packaging/mac/package.sh build                     # Mac: Anbu-macOS.zip with the installer
```

On Mac the build is universal (Apple Silicon and Intel) and runs on macOS 10.13 and newer.

GitHub Actions builds both platforms on every push (`.github/workflows/build.yml`): it runs the robustness checks,
validates the Windows VST3 with pluginval, and on Mac installs the package and validates the VST3 and AU with
pluginval and Apple's auval, on an Apple Silicon and on an Intel Mac. Builds from `main` that pass everything are
published as the `latest` release. Other tools (`-DANBU_BUILD_TOOLS=ON`, `-DANBU_BUILD_SNAPSHOT=ON`):
`AnbuRender` runs the processor over a raw float32 stereo file; `AnbuSnapshot` renders the panel to PNG.

## Source layout
- `Source/DSP/AnbuCore.h` – the DSP (control path, audio path, colour stage, 3D side lift, noise); `AnbuConstants.h` holds the fitted model constants.
- `Source/PluginProcessor.*` – parameters, oversampling, mix, meters, state.
- `Source/DSP/AutoGain.h` – Auto Gain (loudness match). `Source/Presets.*` – the factory bank and user presets.
- `Source/PluginEditor.*` – the panel; `Source/HwAssets.h` – geometry of the rendered art.
- `Resources/hw` – rendered panel art; `Resources/ui` – fonts (Catamaran and Barlow Semi Condensed, SIL Open Font License, see `Resources/FONT_LICENSE_*.txt`).
- `tools/hw` – the panel renderer (below). `tools/presets/makeup.py` – calibrates the factory presets (Peak reduction for a target amount of leveling, Output for equal loudness).
- `packaging/mac` – the Mac installer (`package.sh` builds `Anbu-macOS.zip`).

## Panel artwork
The faceplate is rendered offline from a 3D description of the panel: `tools/hw/hw.py` describes the surfaces and materials,
`render.cpp` lights them (soft shadows, ambient occlusion, studio reflections), and `assets.py` writes the images to
`Resources/hw` and the geometry to `Source/HwAssets.h`. To re-render: compile `render.cpp` (`g++ -O3 -std=c++17 -pthread`),
then `python3 tools/hw/assets.py <renderer> <work dir>` (needs numpy, scipy and Pillow with libraqm). A full render takes about 10 minutes.
