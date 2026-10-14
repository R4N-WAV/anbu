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
| PEAK REDUCTION | LA-2A-style amount of compression. Its tone and distortion change with it (most at the top of the range). |
| COLOR | Lindell-80-style colour: the tone curve plus harmonics. 25 % ≈ the Lindell default, ~80 % ≈ its THD maximum, 100 % = overkill. 0 = off. |
| INPUT / OUTPUT | Drive into the unit / level out. More input = more compression and more colour. |
| MIX | Parallel blend; the dry signal is time-aligned. |
| MODE | Compress or Limit (as on the LA-2A). |
| NOISE + NOISE LEVEL | Optional hiss with a little 50 Hz hum, from −120 to −60 dBFS. |
| OVERSAMPLING | Off / 2x / 4x / 8x. 4x is the normal setting. |
| PHASE | Low Latency (almost no delay) or Linear (cleaner phase, a few ms delay). |

## Using the panel
- **Knobs:** drag up/down (or sideways). Double-click = default. Mouse wheel works. While you turn one, its name and value show in the display.
- **Switches:** click. The lamp shows what is on.
- **VU meter (OUTPUT dB):** 0 on the scale = −18 dBFS RMS, with classic VU needle timing.
- **GR strip:** one LED per dB of gain reduction, lit from the right.
- **Display:** orange = spectrum of the output. White line = Anbu's tone curve at the current settings
  (it moves with PEAK REDUCTION, COLOR and the amount of compression). The arrows in the corner make the display tall; click again to go back.
- The window can be resized from the bottom-right corner.

## If something goes wrong
- **Red cross in Actions:** open the run → *windows-vst3* → the red step, copy the last ~30 lines and send them to Claude.
- **FL doesn't list Anbu:** make sure the folder is exactly `C:\Program Files\Common Files\VST3\Anbu.vst3`, then *Find installed plugins* again.
