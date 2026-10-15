"""Render all panel assets for the plugin and write Source/HwAssets.h.

usage: python3 assets.py <renderer> <work dir> [parts ...]
   parts: bg knobs rings lamps keys fader meter glass header   (default: all)
Assets go to Resources/hw (2 px per design unit).
"""
import math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage
import hw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'Resources', 'hw')
HDR = os.path.join(ROOT, 'Source', 'HwAssets.h')
PAD = 24          # design units of context around a crop (shadows / AO / spill)
SAMPLES = int(os.environ.get('SAMPLES', 24))
KO = hw.K_OUT

def bg_state():
    st = hw.default_state()
    return st

def render_region(binary, work, st, region):
    """render a design-unit region (integers) and return sRGB uint8 at K_OUT"""
    sc = hw.build(region, st)
    lin = hw.light_falloff(sc, hw.render(sc, binary, work, SAMPLES))
    return hw.to_srgb8(lin), sc

def padded(x0, y0, x1, y1, pad=PAD):
    a = max(0, math.floor(x0 - pad)); b = max(0, math.floor(y0 - pad))
    c = min(1000, math.ceil(x1 + pad)); d = min(700, math.ceil(y1 + pad))
    return (a, b, c - a, d - b)

def crop(img, region, rect):
    """rect (design units, integer) inside region -> pixels"""
    x, y, w, h = rect
    ox, oy = int(round((x - region[0]) * KO)), int(round((y - region[1]) * KO))
    return img[oy:oy + int(round(h * KO)), ox:ox + int(round(w * KO))]

def irect(x0, y0, x1, y1):
    a, b = math.floor(x0), math.floor(y0)
    return (a, b, math.ceil(x1) - a, math.ceil(y1) - b)

def save_jpg(a, name, q=94):
    Image.fromarray(a).save(os.path.join(OUT, name), quality=q, subsampling=0, optimize=True)

def save_png(a, name):
    Image.fromarray(a).save(os.path.join(OUT, name), optimize=True)

H = {}   # header entries: name -> (x, y, w, h) and extras

def part_bg(binary, work):
    img, _ = render_region(binary, work, bg_state(), (0, 0, 1000, 700))
    save_jpg(img, 'bg.jpg', 95)

def knob_geom(kk):
    R = kk['D'] / 2
    return irect(kk['cx'] - R - 1, kk['cy'] - R - 1, kk['cx'] + R + 1, kk['cy'] + R + 1)

def part_knobs(binary, work, only=None):
    for kid, kk in hw.KNOBS.items():
        if only and kid not in only: continue
        rect = knob_geom(kk)
        region = padded(rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3])
        frames = []
        for i in range(kk['n']):
            st = bg_state()
            st['knob_pos'] = dict(st['knob_pos']); st['knob_pos'][kid] = i / (kk['n'] - 1)
            img, _ = render_region(binary, work, st, region)
            frames.append(crop(img, region, rect))
            print(kid, i, flush=True)
        save_jpg(np.concatenate(frames, 0), f'knob_{kid}.jpg', 93)

def ring_rect(kk):
    r = kk['rr'] + 16
    return irect(kk['cx'] - r, kk['cy'] - r, kk['cx'] + r, kk['cy'] + r)

def part_rings(binary, work):
    for kid, kk in hw.KNOBS.items():
        rect = ring_rect(kk)
        region = padded(rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3], 8)
        st = bg_state(); st['lamp_rings'] = {kid: True}
        img, _ = render_region(binary, work, st, region)
        save_jpg(crop(img, region, rect), f'ring_{kid}.jpg', 95)

def lamp_rect(y):
    return irect(hw.LAMP_X - 16, y - 16, hw.LAMP_X + 16, y + 16)

def part_lamps(binary, work):
    for name, y, idx in (('c', hw.LAMP_YC, 0), ('l', hw.LAMP_YL, 1)):
        rect = lamp_rect(y)
        region = padded(rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3], 8)
        st = bg_state(); st['lamps'] = [idx == 0, idx == 1]
        img, _ = render_region(binary, work, st, region)
        save_png(crop(img, region, rect), f'lamp_{name}.png')

def key_sprite_rect(i):
    x, y, w, h = hw.key_rect(i)
    return irect(x - 2, y - 2, x + w + 2, y + h + 2)

def part_keys(binary, work):
    st = bg_state(); st['keys_down'] = [True] * 7; st['keys_lit'] = [True] * 7
    for group in ((0, 1, 2, 3, 4, 5), (6,)):
        rects = [key_sprite_rect(i) for i in group]
        x0 = min(r[0] for r in rects); y0 = min(r[1] for r in rects)
        x1 = max(r[0] + r[2] for r in rects); y1 = max(r[1] + r[3] for r in rects)
        region = padded(x0, y0, x1, y1, 10)
        img, _ = render_region(binary, work, st, region)
        for i, r in zip(group, rects):
            save_png(crop(img, region, r), f'key_{i}.png')

FADER_REF = 0.5
def fader_rect():
    yc = hw.FADER_Y1 - (hw.FADER_Y1 - hw.FADER_Y0) * FADER_REF
    return irect(hw.FADER_X - 30, yc - 24, hw.FADER_X + 34, yc + 30), yc

def part_fader(binary, work):
    rect, yc = fader_rect()
    region = padded(rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3], 16)
    st = bg_state(); st['fader'] = FADER_REF
    with_cap, sc = render_region(binary, work, st, region)
    without, _ = render_region(binary, work, bg_state(), region)
    a = crop(with_cap, region, rect).astype(np.float32) / 255
    b = crop(without, region, rect).astype(np.float32) / 255
    # cap coverage from geometry
    g = hw.Scene(region)
    m = hw.fader_cap(g, yc)
    m = m.reshape(m.shape[0] // hw.SS, hw.SS, m.shape[1] // hw.SS, hw.SS).mean(axis=(1, 3))
    mc = crop((m * 255).astype(np.uint8), region, rect).astype(np.float32) / 255
    lum = lambda x: (x[..., 0] * 0.3 + x[..., 1] * 0.59 + x[..., 2] * 0.11)
    shade = np.clip(1 - lum(a) / np.maximum(lum(b), 1e-3), 0, 1)
    alpha = np.maximum(mc, shade * (1 - mc))
    rgb = np.where(mc[..., None] > 0.02, a, 0.0)
    rgba = np.concatenate([rgb, alpha[..., None]], -1)
    save_png((rgba * 255 + 0.5).astype(np.uint8), 'fader_cap.png')
    H['faderRefY'] = yc

def part_meter(binary, work):
    face = irect(*hw.FACE[:2], hw.FACE[0] + hw.FACE[2], hw.FACE[1] + hw.FACE[3])
    region = padded(face[0], face[1], face[0] + face[2], face[1] + face[3], 10)
    st = bg_state(); st['out_mode'] = True
    img, _ = render_region(binary, work, st, region)
    save_jpg(crop(img, region, face), 'face_out.jpg', 95)
    st = bg_state()
    img, sc = render_region(binary, work, st, region)
    save_jpg(crop(img, region, face), 'face_gr.jpg', 95)
    # pivot dome with alpha
    px, py = hw.PIV
    prect = irect(px - 76, hw.FACE[1] + hw.FACE[3] - 16, px + 76, hw.FACE[1] + hw.FACE[3])
    g = hw.Scene(region)
    sl, X, Y = g.grid(*region[:2], region[0] + region[2], region[1] + region[3])
    r = np.hypot(X - px, Y - py)
    m = g.cov(r - 74) * g.cov(hw.sd_rrect(X, Y, *hw.FACE, 3))
    m = m.reshape(m.shape[0] // hw.SS, hw.SS, m.shape[1] // hw.SS, hw.SS).mean(axis=(1, 3))
    a = crop(img, region, prect)
    al = crop((m * 255).astype(np.uint8), region, prect)
    save_png(np.concatenate([a, al[..., None]], -1), 'pivot.png')
    H['pivot'] = prect

def glass(rect, name):
    x, y, w, h = rect
    W, Hh = int(w * KO), int(h * KO)
    yy, xx = np.mgrid[0:Hh, 0:W].astype(np.float32) / KO
    # broad diagonal sheen from the upper left + a soft band
    d = (xx * 0.35 + yy) / (h + w * 0.35)
    a = 0.075 * np.clip(1 - d / 0.45, 0, 1) ** 1.6
    band = np.exp(-((yy - 0.18 * h - xx * 0.12) / (0.05 * h)) ** 2) * 0.03
    a = a + band
    # inner top edge catches the light
    a += 0.10 * np.exp(-yy / 1.2) * np.clip(1 - xx / w, 0.3, 1)
    rgba = np.zeros((Hh, W, 4), np.float32)
    rgba[..., :3] = 1.0
    rgba[..., 3] = np.clip(a, 0, 1)
    save_png((rgba * 255 + 0.5).astype(np.uint8), name)

LOGO_RECT = (30, 44, 208, 86)
def part_nologo(binary, work):
    region = padded(LOGO_RECT[0], LOGO_RECT[1], LOGO_RECT[0] + LOGO_RECT[2], LOGO_RECT[1] + LOGO_RECT[3], 10)
    st = bg_state(); st['logo'] = False
    img, _ = render_region(binary, work, st, region)
    save_jpg(crop(img, region, LOGO_RECT), 'nologo.jpg', 95)

def part_glass():
    glass(hw.FACE, 'glass_meter.png')
    glass(hw.SCREEN, 'glass_disp.png')

def header():
    lines = ['#pragma once', '// generated by tools/hw/assets.py: geometry of the rendered panel assets (design units, 1000 x 700;',
             '// images are 2 px per unit)', 'namespace anbu::hw', '{', 'struct Rect { int x, y, w, h; };',
             'struct KnobArt { const char* id; const char* strip; const char* ring; float cx, cy, radius, rr; bool bipolar; int frames; Rect sprite, ringRect; };',
             'inline constexpr float scale = 2.0f;']
    ks = []
    for kid, kk in hw.KNOBS.items():
        r = knob_geom(kk); rr = ring_rect(kk)
        ks.append(f'    {{ "{kid}", "knob_{kid}_jpg", "ring_{kid}_jpg", {kk["cx"]}.0f, {kk["cy"]}.0f, {kk["D"] / 2}f, {kk["rr"]}.0f, '
                  f'{"true" if kk["bip"] else "false"}, {kk["n"]}, {{ {r[0]}, {r[1]}, {r[2]}, {r[3]} }}, {{ {rr[0]}, {rr[1]}, {rr[2]}, {rr[3]} }} }}')
    lines.append('inline constexpr KnobArt knobs[5] = {\n' + ',\n'.join(ks) + ' };')
    lc, ll = lamp_rect(hw.LAMP_YC), lamp_rect(hw.LAMP_YL)
    lines.append(f'inline constexpr Rect lampC {{ {lc[0]}, {lc[1]}, {lc[2]}, {lc[3]} }}, lampL {{ {ll[0]}, {ll[1]}, {ll[2]}, {ll[3]} }};')
    kr = [key_sprite_rect(i) for i in range(7)]
    lines.append('inline constexpr Rect keys[7] = { ' + ', '.join(f'{{ {r[0]}, {r[1]}, {r[2]}, {r[3]} }}' for r in kr) + ' };')
    fr, yc = fader_rect()
    lines.append(f'inline constexpr Rect faderCap {{ {fr[0]}, {fr[1]}, {fr[2]}, {fr[3]} }};')
    lines.append(f'inline constexpr float faderRefY = {yc}f;')
    f = hw.FACE; s = hw.SCREEN
    lines.append(f'inline constexpr Rect face {{ {f[0]}, {f[1]}, {f[2]}, {f[3]} }}, screen {{ {s[0]}, {s[1]}, {s[2]}, {s[3]} }};')
    pv = H.get('pivot') or irect(hw.PIV[0] - 76, hw.FACE[1] + hw.FACE[3] - 16, hw.PIV[0] + 76, hw.FACE[1] + hw.FACE[3])
    lines.append(f'inline constexpr Rect pivot {{ {pv[0]}, {pv[1]}, {pv[2]}, {pv[3]} }};')
    lines.append(f'inline constexpr Rect logoPatch {{ {LOGO_RECT[0]}, {LOGO_RECT[1]}, {LOGO_RECT[2]}, {LOGO_RECT[3]} }};')
    lines.append('}')
    open(HDR, 'w').write('\n'.join(lines) + '\n')

if __name__ == '__main__':
    binary, work = sys.argv[1:3]
    parts = sys.argv[3:] or ['bg', 'knobs', 'rings', 'lamps', 'keys', 'fader', 'meter', 'glass', 'nologo', 'header']
    os.makedirs(OUT, exist_ok=True)
    only = [p.split('=')[1].split(',') for p in parts if p.startswith('only=')]
    only = only[0] if only else None
    if 'bg' in parts: part_bg(binary, work)
    if 'knobs' in parts: part_knobs(binary, work, only)
    if 'rings' in parts: part_rings(binary, work)
    if 'lamps' in parts: part_lamps(binary, work)
    if 'keys' in parts: part_keys(binary, work)
    if 'fader' in parts: part_fader(binary, work)
    if 'meter' in parts: part_meter(binary, work)
    if 'glass' in parts: part_glass()
    if 'nologo' in parts: part_nologo(binary, work)
    if 'header' in parts or True: header()
    print('done')
