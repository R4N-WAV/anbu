# Anbu test file v1 – segment map

48 kHz, stereo, 32-bit float. Sines in dBFS per AES17 (0 dBFS = full-scale sine); noise in dBFS RMS.

| # | Segment | Start (s) | End (s) | Content |
|---|---|---|---|---|
| 0 | sync_start | 0.50 | 0.75 | single-sample click at -6 dBFS (alignment, latency) |
| 1 | static_staircase | 1.75 | 31.75 | 1 kHz, -42 to 0 dBFS in 3 dB steps of 2 s (15 steps); read the last 0.5 s of each step |
| 2 | attack_release_10dB | 35.75 | 43.75 | 1 kHz -30 dBFS, jump +10 dB at 1.0 s for 2 s, back for 5 s |
| 3 | attack_release_20dB | 46.75 | 54.75 | 1 kHz -30 dBFS, jump +20 dB at 1.0 s for 2 s, back for 5 s |
| 4 | short_bursts | 57.75 | 61.91 | 1 kHz bed at -30 dBFS with bursts to -10 dBFS of 10, 50 and 200 ms (0.8 s apart) |
| 5 | sweep_-30dBFS | 65.91 | 75.91 | exponential sine sweep 20 Hz-20 kHz, 10 s, -30 dBFS (deconvolve with sweep_inverse.npy) |
| 6 | sweep_-10dBFS | 78.91 | 88.91 | exponential sine sweep 20 Hz-20 kHz, 10 s, -10 dBFS (harmonic impulse responses separate in time) |
| 7 | stepped_tones_18 | 92.91 | 101.66 | 50, 100, 200, 500 Hz, 1, 2, 5 kHz at -18 dBFS, 1 s each, 0.25 s apart |
| 8 | stepped_tones_6 | 104.66 | 113.41 | 50, 100, 200, 500 Hz, 1, 2, 5 kHz at -6 dBFS, 1 s each, 0.25 s apart |
| 9 | imd_smpte | 116.41 | 119.41 | SMPTE IMD: 60 Hz + 7 kHz, 4:1, -12 dBFS combined peak |
| 10 | imd_ccif | 122.41 | 125.41 | CCIF IMD: 19 kHz + 20 kHz, -18 dBFS each (difference tone at 1 kHz) |
| 11 | alias_sweep | 128.41 | 136.41 | linear sweep 2-23 kHz over 8 s at -6 dBFS (aliasing shows as falling lines) |
| 12 | pink_mono_24 | 139.41 | 144.41 | pink noise, mono (L = R), -24 dBFS RMS, 5 s |
| 13 | pink_mono_18 | 147.41 | 152.41 | pink noise, mono (L = R), -18 dBFS RMS, 5 s |
| 14 | pink_mono_12 | 155.41 | 160.41 | pink noise, mono (L = R), -12 dBFS RMS, 5 s |
| 15 | pink_stereo_uncorr | 163.41 | 169.41 | pink noise, uncorrelated L/R, -18 dBFS RMS each, 6 s |
| 16 | vocal_proxy | 172.41 | 184.41 | synthetic voice: glottal pulses, 5 rotating vowels, pitch glide + vibrato, loud/soft phrases (10 dB apart), sibilants; about -18 dBFS RMS |
| 17 | drum_proxy | 188.41 | 196.41 | kick/snare pattern at 120 BPM, peaks -6 dBFS |
| 18 | silence | 199.41 | 203.41 | 4 s digital silence (noise floor / Noise level measurements) |
| 19 | sync_end | 203.91 | 204.16 | single-sample click at -6 dBFS |

Total length 204.7 s. Peak 0.0 dBFS.
