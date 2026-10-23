"""Calibrate the factory presets: Peak reduction for a target amount of leveling, Output for equal loudness.

usage: python3 makeup.py <AnbuRender> <vocal.f32> <music.f32> [--write]
Both test files are first brought to a typical working level (vocal -18 LUFS, music -16 LUFS). For each preset
the Peak reduction is found (bisection, whole steps) that turns the loudness of its material down by the
preset's target; parallel presets set that target for the compressed path alone (Mix 100 %). Output is then
set so the preset (at its own Mix) comes out at the loudness it went in with, rounded to whole dB.
Loudness: ITU-R BS.1770 integrated.
"""
import re, subprocess, sys, os
import numpy as np
from scipy.signal import lfilter

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Source', 'Presets.cpp')

# name: (material, target dB of leveling; None = keep Peak reduction as written)
TARGET = {
    'Lead Vocal - Smooth': ('v', 4), 'Lead Vocal - Upfront': ('v', 7), 'Lead Vocal - Silk': ('v', 4),
    'Lead Vocal - Gentle': ('v', 2.5), 'Rap - Tight': ('v', 8), 'Rap - Dense': ('v', 11), 'Belt Control': ('v', 9),
    'Whisper Detail': ('v', 4), 'Backing Vocals - Tuck': ('v', 8), 'Vocal Parallel - Thick': ('v', 15),
    'Podcast & Voice-over': ('v', 6),
    'Bass - Round': ('m', 6), 'Bass - Growl': ('m', 9), 'Acoustic Guitar': ('m', 3.5), 'Electric Guitar - Sustain': ('m', 9),
    'Keys & Piano': ('m', 3), 'Strings & Pads': ('m', 2.5), 'Drum Bus - Glue': ('m', 3), 'Drums - Smash Parallel': ('m', 15),
    'Mix Bus - Gentle Glue': ('m', 1.5), 'Mix Bus - Warm': ('m', 2.5), 'Master - Light Touch': ('m', 1), 'Stem Leveler': ('m', 5),
    'Console Tone': ('v', None), 'Silk - Subtle': ('v', None), 'Silk - Rich': ('v', None), 'Silk - Full': ('v', None),
    'Clean Leveler': ('v', 6), 'Lo-Fi Tape Air': ('v', 10), 'Pumping Limiter': ('m', 16), 'Vintage Hiss Bed': ('v', 4),
    'Dirty Parallel': ('m', 18),
}

def kweight(x, fs=48000):
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    A = 10 ** (G / 40); w0 = 2 * np.pi * fc / fs; al = np.sin(w0) / (2 * Q); c = np.cos(w0); sa = 2 * np.sqrt(A) * al
    x = lfilter([A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa)],
                [(A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa], x, axis=0)
    Q, fc = 0.5003270373253953, 38.13547087613982; w0 = 2 * np.pi * fc / fs; al = np.sin(w0) / (2 * Q); c = np.cos(w0)
    return lfilter([1, -2, 1], [1 + al, -2 * c, 1 - al], x, axis=0)

def lufs(x, fs=48000):
    y = kweight(x, fs); bl, hop = int(0.4 * fs), int(0.1 * fs)
    z = np.array([np.mean(np.sum(y[i:i + bl] ** 2, axis=1)) for i in range(0, len(y) - bl, hop)])
    z = z[-0.691 + 10 * np.log10(z + 1e-30) > -70]; rel = -0.691 + 10 * np.log10(z.mean()) - 10
    z = z[-0.691 + 10 * np.log10(z) > rel]; return -0.691 + 10 * np.log10(z.mean())

ROW = re.compile(r'(\{ "(?P<cat>[^"]+)",\s+"(?P<name>[^"]+)",\s+)(?P<vals>[-\d.,\s]+)(\},)')
KEYS = ['peakReduction', 'color', 'inputGain', 'outputGain', 'mix', 'mode', 'noiseOn', 'noiseLevel']

def main():
    render, write = sys.argv[1], '--write' in sys.argv
    mats = {}
    for key, f, level in (('v', sys.argv[2], -18.0), ('m', sys.argv[3], -16.0)):
        x = np.fromfile(f, '<f4').reshape(-1, 2).astype(float)
        x *= 10 ** ((level - lufs(x)) / 20)
        path = f'/tmp/_mk_in_{key}.f32'; x.astype('<f4').tofile(path)
        mats[key] = (path, lufs(x))
    def delta(mat, st):
        path, l = mats[mat]
        subprocess.run([render, path, '/tmp/_mk.f32'] + [f'{k}={v:g}' for k, v in st.items()], check=True, capture_output=True)
        return lufs(np.fromfile('/tmp/_mk.f32', '<f4').reshape(-1, 2).astype(float)) - l
    src = open(SRC).read(); out = src
    for m in ROW.finditer(src):
        name = m.group('name')
        vals = [float(v) for v in m.group('vals').replace(' ', '').strip(',').split(',')]
        st = dict(zip(KEYS, vals)); st['outputGain'] = 0
        mat, tgt = TARGET[name]
        if tgt is not None:
            wet = dict(st, mix=100)
            lo, hi = 0, 100
            while hi - lo > 1:                       # leveling grows with Peak reduction
                mid = (lo + hi) // 2
                if -(delta(mat, dict(wet, peakReduction=mid)) - st['inputGain']) < tgt: lo = mid
                else: hi = mid
            a = -(delta(mat, dict(wet, peakReduction=lo)) - st['inputGain']); b = -(delta(mat, dict(wet, peakReduction=hi)) - st['inputGain'])
            st['peakReduction'] = lo if abs(a - tgt) <= abs(b - tgt) else hi
        d = delta(mat, st)
        o = int(np.clip(round(-d), -24, 24))
        print(f'{name:28s} PR {int(st["peakReduction"]):3d}  level change {d:+6.1f} dB  -> Output {o:+d}', flush=True)
        vals[0] = st['peakReduction']; vals[3] = o
        cat, nm = f'"{m.group("cat")}",', f'"{name}",'
        out = out.replace(m.group(0), f'{{ {cat:<15s} {nm:<29s} ' + ', '.join(f'{int(v):>4d}' for v in vals) + ' },', 1)
    if write:
        open(SRC, 'w').write(out); print('written')

if __name__ == '__main__':
    main()
