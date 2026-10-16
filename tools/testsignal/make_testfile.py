"""Anbu supplementary test file (v1).

Writes a 48 kHz stereo 32-bit float WAV plus a segment map (CSV + Markdown).
Levels: sines in dBFS per AES17 (0 dBFS = a full-scale sine, peak 1.0); noise in dBFS RMS.
Every segment is followed by silence so the opto cell can recover before the next one.

usage: python3 make_testfile.py <out dir>
"""
import csv, math, os, sys, wave
import numpy as np

FS = 48000
rng = np.random.default_rng(20261016)

def db2a(db): return 10 ** (db / 20)

def sine(f, dur, db, phase=0.0):
    t = np.arange(int(round(dur * FS))) / FS
    return db2a(db) * np.sin(2 * np.pi * f * t + phase)

def fade(x, ms=5):
    n = min(len(x) // 2, int(FS * ms / 1000))
    if n > 0:
        w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
        x[:n] *= w; x[-n:] *= w[::-1]
    return x

def pink(n, rms_db):
    w = rng.standard_normal(n)
    W = np.fft.rfft(w); f = np.fft.rfftfreq(n, 1 / FS)
    W[1:] /= np.sqrt(f[1:]); W[0] = 0
    W[f < 10] = 0                                  # no infrasonic content
    W[f > 22000] *= np.exp(-(f[f > 22000] - 22000) / 500)
    mag = np.abs(W)
    p = np.fft.irfft(W, n)
    # lower the crest factor to about 10.5 dB without changing the spectrum: clip, restore the
    # magnitudes, repeat (only the phases change, so the noise stays exactly pink and distortion-free)
    for _ in range(40):
        lim = 3.35 * np.sqrt(np.mean(p ** 2))
        P = np.fft.rfft(np.clip(p, -lim, lim))
        p = np.fft.irfft(mag * np.exp(1j * np.angle(P)), n)
    return p / np.sqrt(np.mean(p ** 2)) * db2a(rms_db)

def log_sweep(f1, f2, dur, db):
    """exponential sine sweep (Farina); returns the sweep and its inverse filter"""
    n = int(round(dur * FS)); t = np.arange(n) / FS
    L = dur / np.log(f2 / f1)
    s = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1))
    s = fade(s.copy(), 20) * db2a(db)
    inv = s[::-1] * np.exp(-t / L)                 # amplitude-compensated time reverse
    return s, inv

segments = []      # (name, start s, end s, description)
out = []           # list of (L, R) blocks
pos = [0]

def add(name, x, desc, stereo=None, gap=2.0):
    L = np.asarray(x, dtype=np.float64)
    R = L if stereo is None else np.asarray(stereo, dtype=np.float64)
    start = pos[0] / FS
    out.append(np.stack([L, R], 1)); pos[0] += len(L)
    segments.append((name, round(start, 4), round(pos[0] / FS, 4), desc))
    if gap > 0:
        g = np.zeros((int(round(gap * FS)), 2)); out.append(g); pos[0] += len(g)

def click(db=-6.0):
    x = np.zeros(int(0.25 * FS)); x[0] = db2a(db); return x

# 0. sync
out.append(np.zeros((int(0.5 * FS), 2))); pos[0] += int(0.5 * FS)
add('sync_start', click(), 'single-sample click at -6 dBFS (alignment, latency)', gap=1.0)

# 1. static curve: 1 kHz staircase, -42 .. 0 dBFS in 3 dB steps, 2 s per step (last 0.5 s of each step = steady state)
st = []
for lv in range(-42, 1, 3):
    st.append(sine(1000, 2.0, lv, phase=0))
x = np.concatenate(st)
# continuous phase across steps: rebuild from one oscillator with stepped amplitude
t = np.arange(len(x)) / FS
amp = np.repeat([db2a(lv) for lv in range(-42, 1, 3)], int(2.0 * FS))
amp = np.convolve(amp, np.ones(96) / 96, mode='same')      # 2 ms ramps between steps
x = amp * np.sin(2 * np.pi * 1000 * t)
add('static_staircase', fade(x), '1 kHz, -42 to 0 dBFS in 3 dB steps of 2 s (15 steps); read the last 0.5 s of each step', gap=4.0)

# 2. attack / release: 1 kHz at -30 dBFS with level jumps (+10, +20 dB) for 2 s, then back for 5 s
def step_burst(base, jump, hi_dur, lo_dur, f=1000.0):
    n_lo1, n_hi, n_lo2 = int(1.0 * FS), int(hi_dur * FS), int(lo_dur * FS)
    a = np.concatenate([np.full(n_lo1, db2a(base)), np.full(n_hi, db2a(base + jump)), np.full(n_lo2, db2a(base))])
    t = np.arange(len(a)) / FS
    return fade(a * np.sin(2 * np.pi * f * t))
add('attack_release_10dB', step_burst(-30, 10, 2.0, 5.0), '1 kHz -30 dBFS, jump +10 dB at 1.0 s for 2 s, back for 5 s', gap=3.0)
add('attack_release_20dB', step_burst(-30, 20, 2.0, 5.0), '1 kHz -30 dBFS, jump +20 dB at 1.0 s for 2 s, back for 5 s', gap=3.0)
# short bursts: 10, 50, 200 ms at -10 dBFS over a -30 dBFS bed (transient handling)
bb = []
for ms in (10, 50, 200):
    n_lo, n_hi = int(0.8 * FS), int(ms / 1000 * FS)
    bb.append(np.concatenate([np.full(n_lo, db2a(-30)), np.full(n_hi, db2a(-10))]))
bb.append(np.full(int(1.5 * FS), db2a(-30)))
a = np.concatenate(bb); t = np.arange(len(a)) / FS
add('short_bursts', fade(a * np.sin(2 * np.pi * 1000 * t)), '1 kHz bed at -30 dBFS with bursts to -10 dBFS of 10, 50 and 200 ms (0.8 s apart)', gap=4.0)

# 3. exponential sweeps 20 Hz - 20 kHz, 10 s: low level (linear response) and high level (compression + harmonics)
sw_lo, inv = log_sweep(20, 20000, 10.0, -30)
sw_hi, _ = log_sweep(20, 20000, 10.0, -10)
np.save(os.path.join(sys.argv[1] if len(sys.argv) > 1 else '.', 'sweep_inverse.npy'), inv)
add('sweep_-30dBFS', sw_lo, 'exponential sine sweep 20 Hz-20 kHz, 10 s, -30 dBFS (deconvolve with sweep_inverse.npy)', gap=3.0)
add('sweep_-10dBFS', sw_hi, 'exponential sine sweep 20 Hz-20 kHz, 10 s, -10 dBFS (harmonic impulse responses separate in time)', gap=4.0)

# 4. stepped tones: harmonics vs frequency and level
for lv in (-18, -6):
    tones = [fade(sine(f, 1.0, lv)) for f in (50, 100, 200, 500, 1000, 2000, 5000)]
    gapped = []
    for tn in tones: gapped += [tn, np.zeros(int(0.25 * FS))]
    add(f'stepped_tones_{-lv}', np.concatenate(gapped), f'50, 100, 200, 500 Hz, 1, 2, 5 kHz at {lv} dBFS, 1 s each, 0.25 s apart', gap=3.0)

# 5. intermodulation
smpte = sine(60, 3.0, -12 + 20 * math.log10(0.8)) + sine(7000, 3.0, -12 + 20 * math.log10(0.2))
add('imd_smpte', fade(smpte), 'SMPTE IMD: 60 Hz + 7 kHz, 4:1, -12 dBFS combined peak', gap=3.0)
ccif = sine(19000, 3.0, -18) + sine(20000, 3.0, -18)
add('imd_ccif', fade(ccif), 'CCIF IMD: 19 kHz + 20 kHz, -18 dBFS each (difference tone at 1 kHz)', gap=3.0)

# 6. aliasing: linear sweep 2 kHz -> 23 kHz over 8 s at -6 dBFS (aliases fold down and move the other way)
n = int(8 * FS); t = np.arange(n) / FS
ph = 2 * np.pi * (2000 * t + 0.5 * (21000 / 8) * t ** 2)
add('alias_sweep', fade(db2a(-6) * np.sin(ph), 20), 'linear sweep 2-23 kHz over 8 s at -6 dBFS (aliasing shows as falling lines)', gap=3.0)

# 7. pink noise (mono) at three levels
for lv in (-24, -18, -12):
    add(f'pink_mono_{-lv}', fade(pink(5 * FS, lv), 50), f'pink noise, mono (L = R), {lv} dBFS RMS, 5 s', gap=3.0)

# 8. uncorrelated stereo pink noise (3D / side behaviour)
add('pink_stereo_uncorr', fade(pink(6 * FS, -18), 50), 'pink noise, uncorrelated L/R, -18 dBFS RMS each, 6 s',
    stereo=fade(pink(6 * FS, -18), 50), gap=3.0)

# 9. vocal proxy: glottal pulses through formants, gliding pitch with vibrato, syllables and sibilants
def vocal(dur):
    n = int(dur * FS); t = np.arange(n) / FS
    f0 = 170 + 60 * np.sin(2 * np.pi * 0.23 * t) + 4 * np.sin(2 * np.pi * 5.5 * t)
    ph = np.cumsum(f0) / FS
    frac = ph % 1.0
    # Rosenberg-style flow pulse: smooth opening, faster closing that snaps shut (the closure gives the voice its brightness)
    glott = np.where(frac < 0.55, 0.5 - 0.5 * np.cos(np.pi * frac / 0.55),
                     np.where(frac < 0.75, np.cos(0.5 * np.pi * (frac - 0.55) / 0.20), 0.0))
    src = np.diff(glott, prepend=0) * FS / 2000.0              # derivative: flow -> pressure
    vowels = [(730, 1090, 2440), (530, 1840, 2480), (270, 2290, 3010), (570, 840, 2410), (300, 870, 2240)]
    seg = int(0.6 * FS)
    y = np.zeros(n)
    from scipy.signal import lfilter
    for k in range(0, n, seg):
        F = vowels[(k // seg) % len(vowels)]
        part = src[k:k + seg].copy()
        acc = np.zeros_like(part)
        for f, bw, g in zip(F, (80, 100, 140), (1.0, 0.8, 0.6)):
            r = np.exp(-np.pi * bw / FS); th = 2 * np.pi * f / FS
            acc += g * 2 * np.sin(th) * lfilter([1 - r], [1, -2 * r * np.cos(th), r * r], part)   # unit peak gain per formant
        y[k:k + seg] = acc
    # syllable envelope with phrase dynamics (soft and loud phrases) and sibilants
    syl = 0.5 - 0.5 * np.cos(2 * np.pi * np.clip((t % 0.6) / 0.6, 0, 1))
    phrase = np.where((t % 4.0) < 2.0, 1.0, db2a(-10))
    y = y * (0.25 + 0.75 * syl) * phrase
    from scipy.signal import butter
    b, a = butter(4, 4500 / (FS / 2), 'high')
    hiss = lfilter(b, a, rng.standard_normal(n)) * ((t % 1.2) > 1.05)
    y = y / np.sqrt(np.mean(y ** 2)) * db2a(-18) + hiss * db2a(-34)
    return fade(y, 30)
add('vocal_proxy', vocal(12.0), 'synthetic voice: glottal pulses, 5 rotating vowels, pitch glide + vibrato, loud/soft phrases (10 dB apart), sibilants; about -18 dBFS RMS', gap=4.0)

# 10. drum proxy: kick + snare pattern (transients for attack behaviour)
def drums(dur):
    n = int(dur * FS); y = np.zeros(n)
    t = np.arange(int(0.35 * FS)) / FS
    kick = np.sin(2 * np.pi * (50 * t + 60 * (1 - np.exp(-t * 30)) / 30)) * np.exp(-t * 9)
    snare = rng.standard_normal(len(t)) * np.exp(-t * 22) * 0.6 + np.sin(2 * np.pi * 190 * t) * np.exp(-t * 18) * 0.5
    beat = int(0.5 * FS)
    for i in range(0, n - len(t), beat):
        y[i:i + len(t)] += kick if (i // beat) % 2 == 0 else snare
    return fade(y / np.max(np.abs(y)) * db2a(-6), 10)
add('drum_proxy', drums(8.0), 'kick/snare pattern at 120 BPM, peaks -6 dBFS', gap=3.0)

# 11. silence (noise floor, Noise generator) and end sync
add('silence', np.zeros(int(4 * FS)), '4 s digital silence (noise floor / Noise level measurements)', gap=0.5)
add('sync_end', click(), 'single-sample click at -6 dBFS', gap=0.5)

audio = np.concatenate(out).astype(np.float32)
od = sys.argv[1] if len(sys.argv) > 1 else '.'
os.makedirs(od, exist_ok=True)

# 32-bit float WAV (WAVE_FORMAT_IEEE_FLOAT)
def write_float_wav(path, x):
    data = x.astype('<f4').tobytes()
    with open(path, 'wb') as f:
        ch = x.shape[1]
        f.write(b'RIFF'); f.write((4 + 26 + 12 + 8 + len(data)).to_bytes(4, 'little')); f.write(b'WAVE')
        f.write(b'fmt '); f.write((18).to_bytes(4, 'little'))
        f.write((3).to_bytes(2, 'little')); f.write(ch.to_bytes(2, 'little')); f.write(FS.to_bytes(4, 'little'))
        f.write((FS * ch * 4).to_bytes(4, 'little')); f.write((ch * 4).to_bytes(2, 'little')); f.write((32).to_bytes(2, 'little'))
        f.write((0).to_bytes(2, 'little'))
        f.write(b'fact'); f.write((4).to_bytes(4, 'little')); f.write(len(x).to_bytes(4, 'little'))
        f.write(b'data'); f.write(len(data).to_bytes(4, 'little')); f.write(data)

write_float_wav(os.path.join(od, 'Anbu_testfile_v1.wav'), audio)
audio.tofile(os.path.join(od, 'Anbu_testfile_v1.f32'))
with open(os.path.join(od, 'Anbu_testfile_v1_segments.csv'), 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['segment', 'start_s', 'end_s', 'description'])
    for s in segments: w.writerow(s)
with open(os.path.join(od, 'Anbu_testfile_v1_segments.md'), 'w') as f:
    f.write('# Anbu test file v1 – segment map\n\n48 kHz, stereo, 32-bit float. Sines in dBFS per AES17 (0 dBFS = full-scale sine); noise in dBFS RMS.\n\n')
    f.write('| # | Segment | Start (s) | End (s) | Content |\n|---|---|---|---|---|\n')
    for i, s in enumerate(segments): f.write(f'| {i} | {s[0]} | {s[1]:.2f} | {s[2]:.2f} | {s[3]} |\n')
    f.write(f'\nTotal length {len(audio) / FS:.1f} s. Peak {20 * np.log10(np.max(np.abs(audio))):.1f} dBFS.\n')
print(f'{len(audio) / FS:.1f} s, {len(segments)} segments, peak {20 * np.log10(np.max(np.abs(audio))):.2f} dBFS')
