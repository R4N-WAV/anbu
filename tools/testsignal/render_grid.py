"""Render the Anbu test files through the real plugin processor (AnbuRender) at a grid of settings.

usage: python3 render_grid.py <AnbuRender> <test file .f32> <master dry .f32> <out dir>
Writes <out>/supp/<name>.f32 and <out>/master/<name>.f32 (stereo float32, latency-compensated)
and <out>/manifest.json with the settings of every render.
"""
import json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

R, SUPP, MASTER, OUT = sys.argv[1:5]

# defaults of the plugin: PR 40, Color 25, In/Out 0, Mix 100, Compress, Noise off, OS 4x, Low latency
def cfg(**kw):
    base = dict(peakReduction=40, color=25, inputGain=0, outputGain=0, mix=100, mode=0, noiseOn=0,
                noiseLevel=-96, oversampling=2, osPhase=0, bypass=0)
    base.update(kw); return base

supp = {
    'default':        cfg(),
    'bypass':         cfg(bypass=1),
    # Peak reduction (Color off, Compress)
    **{f'pr{p}': cfg(peakReduction=p, color=0) for p in (0, 25, 50, 75, 100)},
    # Limit
    'lim50':          cfg(peakReduction=50, color=0, mode=1),
    'lim100':         cfg(peakReduction=100, color=0, mode=1),
    # Color alone (no compression), and with compression
    **{f'c{c}': cfg(peakReduction=0, color=c) for c in (10, 25, 50, 80, 100)},
    'pr50_c25':       cfg(peakReduction=50, color=25),
    'pr50_c80':       cfg(peakReduction=50, color=80),
    # drive into the unit
    'pr50_c25_in12':  cfg(peakReduction=50, color=25, inputGain=12, outputGain=-12),
    # oversampling / phase (lots of harmonics so aliasing shows)
    'os_off':         cfg(peakReduction=50, color=100, oversampling=0),
    **{f'os{1 << i}x_{ph}': cfg(peakReduction=50, color=100, oversampling=i, osPhase=1 if ph == 'lin' else 0)
       for i in (1, 2, 3) for ph in ('low', 'lin')},
    # parallel
    'pr75_c25_mix50': cfg(peakReduction=75, color=25, mix=50),
    # noise generator
    'noise_m60':      cfg(peakReduction=0, color=0, noiseOn=1, noiseLevel=-60),
    'noise_m96':      cfg(peakReduction=0, color=0, noiseOn=1, noiseLevel=-96),
}

# the original master file at the settings of its reference renders (Gray PR 0..100, Limit 50/100, Lindell default/THD max)
master = {
    **{f'pr{p}': cfg(peakReduction=p, color=0) for p in (0, 25, 50, 75, 100)},
    'lim50':  cfg(peakReduction=50, color=0, mode=1),
    'lim100': cfg(peakReduction=100, color=0, mode=1),
    'c25':    cfg(peakReduction=0, color=25),      # vs Lindell default
    'c80':    cfg(peakReduction=0, color=80),      # vs Lindell THD max
    'default': cfg(),
}

def render(job):
    src, dst, settings = job
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    t0 = time.time()
    args = [R, src, dst] + [f'{k}={v}' for k, v in settings.items()]
    p = subprocess.run(args, capture_output=True, text=True)
    return dst, p.returncode, round(time.time() - t0, 1), p.stderr.strip().splitlines()[-1:] if p.stderr else []

jobs = [(SUPP, os.path.join(OUT, 'supp', n + '.f32'), s) for n, s in supp.items()]
jobs += [(MASTER, os.path.join(OUT, 'master', n + '.f32'), s) for n, s in master.items()]
with ThreadPoolExecutor(2) as ex:
    for dst, rc, sec, msg in ex.map(render, jobs):
        print(f'{"ok " if rc == 0 else "ERR"} {os.path.relpath(dst, OUT):28s} {sec:5.1f} s  {" ".join(msg)}', flush=True)
json.dump({'supp': supp, 'master': master, 'format': 'stereo float32 little-endian, 48 kHz, latency-compensated'},
          open(os.path.join(OUT, 'manifest.json'), 'w'), indent=1)
