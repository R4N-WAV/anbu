"""Anbu v1 faceplate: scene description + render driver (offline asset tool).

Builds height / material maps for the panel (design units: 1000 x 700), renders them with the
C++ height-field renderer (render.cpp) and writes the plugin's image assets.

usage: python3 hw.py <renderer binary> <work dir> <out dir> [what ...]
   what: test | bg | knobs | keys | lamps | fader | meter | all
"""
import json, math, os, subprocess, sys, zlib
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, '..', '..', 'Resources', 'ui')
F_BAR5 = os.path.join(UI, 'barlow500.ttf')
F_BAR6 = os.path.join(UI, 'barlow600.ttf')
F_BAR7 = os.path.join(UI, 'barlow700.ttf')
F_CAT8 = os.path.join(UI, 'catamaran800.ttf')
F_CAT6 = os.path.join(UI, 'catamaran600.ttf')

K_OUT = 2          # output pixels per design unit
SS = 2             # supersampling
K = K_OUT * SS     # render pixels per design unit

# ------------------------------------------------------------------ colours
def lin(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def hexc(h):
    h = h.lstrip('#')
    return lin([int(h[i:i + 2], 16) for i in (0, 2, 4)])

PLATE_TOP, PLATE_BOT = hexc('9a2a26'), hexc('771b1d')
CHASSIS = hexc('1c0707')
GOLD = hexc('e8c46e')
CREAM = hexc('e9d9b8')
FACE_INK = hexc('e6dcc8')
RECESS = hexc('4f0f10')
LAMP = hexc('ffb341')
KEYON = hexc('ffe08a')

PLATE_H = 3.0

# ------------------------------------------------------------------ layout (same as the editor)
MODULES = [(24, 220), (256, 598), (866, 110)]
MOD_TOP, MOD_H = 24, 652
KNOBS = {   # id: cx, cy, D, rr, bipolar, frames, (min, max), scale
    'pr':     dict(cx=134, cy=226, D=98, rr=62, bip=False, n=101),
    'input':  dict(cx=921, cy=112, D=54, rr=36, bip=True,  n=49),
    'output': dict(cx=921, cy=238, D=54, rr=36, bip=True,  n=49),
    'mix':    dict(cx=921, cy=364, D=54, rr=36, bip=False, n=101),
    'noise':  dict(cx=921, cy=556, D=40, rr=28, bip=False, n=61),
}
FADER_X, FADER_Y0, FADER_Y1 = 134, 350, 528
LAMP_X, LAMP_YC, LAMP_YL = 56.5, 608.5, 645.0
MODE_BTN = (204, 626.5)
BEZEL = (280, 48, 550, 252)
FACE = (288, 56, 534, 236)
PIV = (555, 356)
METER_R = 222
SPAN = math.radians(38)
DISP = (280, 318, 550, 220)
SCREEN = (285, 323, 540, 210)
BANK_OS = (280, 580, 280, 40)
BANK_PH = (590, 580, 240, 40)
NOISE_KEY = (889, 462, 64, 32)
KEY_TEXT = ['OFF', '2×', '4×', '8×', 'LOW LATENCY', 'LINEAR', 'NOISE']

def key_rect(i):
    if i < 4: return (BANK_OS[0] + 4 + 69 * i, BANK_OS[1] + 4, 65, 32)
    if i < 6: return (BANK_PH[0] + 4 + 118 * (i - 4), BANK_PH[1] + 4, 114, 32)
    return NOISE_KEY

def vu_frac(d): return (10 ** (d / 20) - 0.1) / (10 ** (3 / 20) - 0.1)
def meter_angle(f): return -SPAN + 2 * SPAN * f
def gr_angle(d): return meter_angle(1 - d / 20.0)          # GR reads right to left: 0 at the right

# ------------------------------------------------------------------ scene maps
class Scene:
    def __init__(self, region, k=K, seed=1):
        self.x0, self.y0, self.w, self.h = region
        self.k = k
        self.W = int(round(self.w * k)); self.H = int(round(self.h * k))
        W, H = self.W, self.H
        self.hgt = np.zeros((H, W), np.float32)
        self.alb = np.zeros((H, W, 3), np.float32)
        self.rough = np.full((H, W), 0.6, np.float32)
        self.metal = np.zeros((H, W), np.float32)
        self.emis = np.zeros((H, W, 3), np.float32)
        self.aniso = np.zeros((H, W), np.float32)
        self.tx = np.zeros((H, W), np.float32)
        self.ty = np.zeros((H, W), np.float32)
        self.rng = np.random.default_rng(seed)

    # pixel grid of a design-unit bbox clipped to the region; returns (slices, X, Y) or None
    def grid(self, x0, y0, x1, y1):
        k = self.k
        px0 = max(0, int(math.floor((x0 - self.x0) * k))); px1 = min(self.W, int(math.ceil((x1 - self.x0) * k)))
        py0 = max(0, int(math.floor((y0 - self.y0) * k))); py1 = min(self.H, int(math.ceil((y1 - self.y0) * k)))
        if px1 <= px0 or py1 <= py0: return None
        xs = self.x0 + (np.arange(px0, px1) + 0.5) / k
        ys = self.y0 + (np.arange(py0, py1) + 0.5) / k
        X, Y = np.meshgrid(xs.astype(np.float32), ys.astype(np.float32))
        return (slice(py0, py1), slice(px0, px1)), X, Y

    def cov(self, d):   # coverage from a signed distance (units, negative inside)
        return np.clip(0.5 - d * self.k, 0, 1).astype(np.float32)

    def put(self, sl, m, h=None, alb=None, rough=None, metal=None, emis=None, aniso=None, t=None):
        if m is None: return
        if h is not None: self.hgt[sl] = self.hgt[sl] * (1 - m) + h * m
        mm = m[..., None]
        if alb is not None:
            a = np.asarray(alb, np.float32)
            self.alb[sl] = self.alb[sl] * (1 - mm) + (a if a.ndim == 3 else a[None, None, :]) * mm
        if rough is not None: self.rough[sl] = self.rough[sl] * (1 - m) + rough * m
        if metal is not None: self.metal[sl] = self.metal[sl] * (1 - m) + metal * m
        if emis is not None:
            e = np.asarray(emis, np.float32)
            self.emis[sl] = self.emis[sl] * (1 - mm) + (e if e.ndim == 3 else e[None, None, :]) * mm
        if aniso is not None: self.aniso[sl] = self.aniso[sl] * (1 - m) + aniso * m
        if t is not None:
            self.tx[sl] = self.tx[sl] * (1 - m) + t[0] * m
            self.ty[sl] = self.ty[sl] * (1 - m) + t[1] * m

    # ---- text rasterised into a coverage mask (region-sized)
    def text_mask(self, items):
        img = Image.new('L', (self.W, self.H), 0)
        d = ImageDraw.Draw(img)
        for it in items:
            font, size, s, x, y = it['font'], it['size'], it['text'], it['x'], it['y']
            anchor = it.get('anchor', 'm')     # l / m / r ; y is the cap-height centre for Latin
            track = it.get('track', 0.0)
            f = ImageFont.truetype(font, max(1, int(round(size * self.k))), layout_engine=ImageFont.Layout.RAQM)
            if it.get('baseline'):
                by = (y - self.y0) * self.k
            else:
                by = (y - self.y0 + 0.35 * size) * self.k      # Barlow cap height ~0.70 em
            px = (x - self.x0) * self.k
            if track == 0:
                d.text((px, by), s, font=f, fill=255, anchor={'l': 'ls', 'm': 'ms', 'r': 'rs'}[anchor])
            else:
                tr = track * size * self.k
                widths = [f.getlength(c) for c in s]
                total = sum(widths) + tr * (len(s) - 1)
                cx = px - (0 if anchor == 'l' else total / 2 if anchor == 'm' else total)
                for c, w in zip(s, widths):
                    d.text((cx, by), c, font=f, fill=255, anchor='ls')
                    cx += w + tr
        return np.asarray(img, np.float32) / 255.0

    def ink(self, mask, alb, raise_h=0.03, rough=0.42, metal=0.0, emis=None):
        m = mask
        if m.max() <= 0: return
        full = (slice(0, self.H), slice(0, self.W))
        self.hgt += raise_h * m
        self.put(full, m, alb=alb, rough=rough, metal=metal, emis=emis)

    def save(self, d):
        os.makedirs(d, exist_ok=True)
        for name, a in [('h', self.hgt), ('alb', self.alb), ('rough', self.rough), ('metal', self.metal),
                        ('emis', self.emis), ('aniso', self.aniso), ('tx', self.tx), ('ty', self.ty)]:
            np.ascontiguousarray(a, np.float32).tofile(os.path.join(d, name + '.f32'))

# ------------------------------------------------------------------ SDF helpers
def sd_rrect(X, Y, x, y, w, h, r):
    cx, cy = x + w / 2, y + h / 2
    qx = np.abs(X - cx) - (w / 2 - r); qy = np.abs(Y - cy) - (h / 2 - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r

def sd_seg(X, Y, ax, ay, bx, by, r):
    px, py = X - ax, Y - ay; dx, dy = bx - ax, by - ay
    t = np.clip((px * dx + py * dy) / (dx * dx + dy * dy + 1e-9), 0, 1)
    return np.hypot(px - dx * t, py - dy * t) - r

_NOISE = {}
def noise(sc, sl, name, sigma_u, amp):
    """noise field defined over the whole panel (same values in every crop)"""
    key = (name, sc.k)
    if key not in _NOISE:
        rng = np.random.default_rng(zlib.crc32(name.encode()))
        H, W = int(round(700 * sc.k)), int(round(1000 * sc.k))
        n = ndimage.gaussian_filter(rng.standard_normal((H, W)).astype(np.float32), sigma_u * sc.k)
        _NOISE[key] = n / (n.std() + 1e-9)
    ox, oy = int(round(sc.x0 * sc.k)), int(round(sc.y0 * sc.k))
    f = _NOISE[key]
    return f[sl[0].start + oy:sl[0].stop + oy, sl[1].start + ox:sl[1].stop + ox] * amp

# ------------------------------------------------------------------ parts
def chassis(sc):
    g = sc.grid(sc.x0, sc.y0, sc.x0 + sc.w, sc.y0 + sc.h)
    sl, X, Y = g
    m = np.ones_like(X)
    tint = CHASSIS[None, None, :] * (1 + noise(sc, sl, 'chassis_c', 1.5, 0.03)[..., None])
    sc.put(sl, m, h=noise(sc, sl, 'chassis_h', 0.6, 0.004), alb=tint, rough=0.85)

def plate(sc, x, w):
    y, h = MOD_TOP, MOD_H
    g = sc.grid(x - 1, y - 1, x + w + 1, y + h + 1)
    if g is None: return
    sl, X, Y = g
    d = sd_rrect(X, Y, x, y, w, h, 4)
    bev = 1.4
    e = np.clip(-d / bev, 0, 1)
    prof = PLATE_H * (1 - (1 - e) ** 2)
    m = sc.cov(d)
    tcol = np.clip((Y - y) / h, 0, 1)[..., None]
    col = PLATE_TOP[None, None, :] * (1 - tcol) + PLATE_BOT[None, None, :] * tcol
    col = col * (1 + noise(sc, sl, 'plate_c', 1.2, 0.025)[..., None])
    peel = noise(sc, sl, 'plate_h', 0.35, 0.012)
    sc.put(sl, m, h=prof + peel * e, alb=col, rough=0.5, metal=0.0)

def zari(sc, x, w):
    y, h = MOD_TOP, MOD_H
    g = sc.grid(x, y, x + w, y + h)
    if g is None: return None
    sl, X, Y = g
    full = np.zeros((sc.H, sc.W), np.float32)
    for inset, wid in ((7.5, 1.0), (10.5, 0.6)):
        d = np.abs(sd_rrect(X, Y, x + inset, y + inset, w - 2 * inset, h - 2 * inset, 2.0 if inset < 9 else 1.0)) - wid / 2
        full[sl] = np.maximum(full[sl], sc.cov(d))
    return full

def screw(sc, cx, cy, slot_deg):
    r = 5.0
    g = sc.grid(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1)
    if g is None: return
    sl, X, Y = g
    dx, dy = X - cx, Y - cy
    rr = np.hypot(dx, dy)
    m = sc.cov(rr - r)
    dome = PLATE_H + 0.3 + 1.3 * np.sqrt(np.clip(1 - (rr / r) ** 2, 0, 1))
    a = math.radians(slot_deg)
    u = dx * math.cos(a) + dy * math.sin(a); v = -dx * math.sin(a) + dy * math.cos(a)
    slot = np.clip(0.5 - (np.abs(v) - 0.6) * sc.k, 0, 1) * (np.abs(u) < r - 0.8)
    h = dome - 0.9 * slot
    sc.put(sl, m, h=h, alb=hexc('2b2626'), rough=0.38, metal=0.85)
    sc.put(sl, m * slot, alb=hexc('0c0a0a'), rough=0.7, metal=0.3)

def lamp_ring(sc, cx, cy, rr, lit=False):
    g = sc.grid(cx - rr - 4, cy - rr - 4, cx + rr + 4, cy + rr + 4)
    if g is None: return
    sl, X, Y = g
    rad = np.hypot(X - cx, Y - cy)
    d = np.abs(rad - rr) - 1.4
    m = sc.cov(d)
    groove = sc.cov(np.abs(rad - rr) - 1.9)
    sc.put(sl, groove, h=PLATE_H - 0.55, alb=hexc('150404'), rough=0.6)
    sc.put(sl, m, h=PLATE_H - 0.25, alb=hexc('3a1408'), rough=0.12, metal=0.0, emis=LAMP * 0.8 if lit else np.zeros(3))

def knob(sc, cx, cy, D, angle_deg, ridges):
    R = D / 2.0
    Rt = R * 0.80
    Hs = 4.0 + 0.9 * (R - Rt)      # skirt top above the plate (steep foot, then a 45 degree knurled cone)
    Ht = Hs + 0.025 * D + 0.8
    g = sc.grid(cx - R - 1, cy - R - 1, cx + R + 1, cy + R + 1)
    if g is None: return
    sl, X, Y = g
    dx, dy = X - cx, Y - cy
    r = np.hypot(dx, dy)
    th = np.arctan2(dx, -dy)                 # clockwise from 12 o'clock
    a = math.radians(angle_deg)
    m = sc.cov(r - R)
    # skirt: cone with knurling (ridges rotate with the knob)
    s = np.clip((R - r) / (R - Rt), 0, 1)
    foot = 1 - (1 - np.clip((R - r) / 0.9, 0, 1)) ** 2
    hs = PLATE_H + (4.0 + (Hs - 4.0) * (1 - (1 - s) ** 2)) * foot
    band = np.clip(s * 6, 0, 1) * np.clip((1 - s) * 6, 0, 1)
    kn = np.abs(np.sin(ridges / 2.0 * (th - a)))
    hs = hs + 0.45 * band * (kn ** 0.6 - 0.6)
    # top: chamfer ring, spun dish, engraved pointer
    c = 0.10 * Rt
    e = np.clip((r - (Rt - c)) / c, 0, 1)
    ht = PLATE_H + Ht - (Ht - Hs) * e ** 2 - 0.25 * (r / Rt) ** 2 * (1 - e)
    top = sc.cov(r - Rt)
    h = hs * (1 - top) + ht * top
    # pointer groove (in the knob frame)
    u = dx * math.sin(a) - dy * math.cos(a)
    v = dx * math.cos(a) + dy * math.sin(a)
    pw = max(1.4, 0.035 * D)
    pmask = sc.cov(np.maximum(np.abs(v) - pw / 2, np.maximum(0.18 * Rt - u, u - 0.86 * Rt)))
    h = h - 0.35 * pmask
    sc.put(sl, m, h=h, alb=lin([78, 79, 86]), rough=0.3, metal=1.0)
    # spun top (anisotropic, grooves run around the centre)
    tm = top * (1 - np.clip((r - (Rt - c * 1.05)) * sc.k, 0, 1))
    sc.put(sl, tm, alb=lin([118, 120, 128]), rough=0.3, metal=1.0, aniso=1.0, t=(-dy / (r + 1e-6), dx / (r + 1e-6)))
    # polished chamfer
    ring = top * np.clip((r - (Rt - c * 1.05)) * sc.k, 0, 1)
    sc.put(sl, ring, alb=lin([200, 200, 206]), rough=0.14, metal=1.0, aniso=0.0)
    sc.put(sl, pmask, alb=CREAM, rough=0.5, metal=0.0, aniso=0.0)
    # fine spin rings in the height (gives the circular sheen)
    spin = 0.006 * np.sin(r * sc.k * 2.1) * (top * (1 - e))
    sc.hgt[sl] += spin

def fader_slot(sc):
    x, y0, y1 = FADER_X, FADER_Y0 - 8, FADER_Y1 + 8
    g = sc.grid(x - 6, y0 - 3, x + 6, y1 + 3)
    if g is None: return
    sl, X, Y = g
    d = sd_rrect(X, Y, x - 3, y0, 6, y1 - y0, 3)
    m = sc.cov(d)
    depth = PLATE_H - 7 * np.clip(-d / 1.2, 0, 1)
    sc.put(sl, m, h=depth, alb=hexc('0a0303'), rough=0.8)

def fader_cap(sc, yc):
    x = FADER_X
    g = sc.grid(x - 24, yc - 18, x + 24, yc + 18)
    if g is None: return None
    sl, X, Y = g
    d = sd_rrect(X, Y, x - 22, yc - 16, 44, 32, 4)
    m = sc.cov(d)
    edge = 1 - (1 - np.clip(-d / 2.6, 0, 1)) ** 2
    Ht = 9.5
    concave = 1.1 * (1 - ((Y - yc) / 16) ** 2)
    h = PLATE_H + 0.5 + (Ht - concave) * edge
    # grip ridges across the top (not on the centre line)
    rid = 0.18 * (np.abs(np.sin((Y - yc) * math.pi / 2.6)) ** 2) * (np.abs(Y - yc) > 4) * np.clip(-d / 3, 0, 1)
    h = h - rid
    line = sc.cov(sd_rrect(X, Y, x - 15, yc - 1, 30, 2, 1))
    h = h - 0.25 * line
    sc.put(sl, m, h=h, alb=lin([16, 16, 19]), rough=0.36, metal=0.0)
    sc.put(sl, line, alb=CREAM, rough=0.5)
    full = np.zeros((sc.H, sc.W), np.float32); full[sl] = m
    return full

def bank(sc, rect):
    x, y, w, h = rect
    g = sc.grid(x - 2, y - 2, x + w + 2, y + h + 2)
    if g is None: return
    sl, X, Y = g
    d = sd_rrect(X, Y, x, y, w, h, 5)
    m = sc.cov(d)
    wall = np.clip(-d / 1.0, 0, 1)
    sc.put(sl, m, h=PLATE_H - 2.6 * wall, alb=RECESS, rough=0.7)

def key(sc, i, down, legend_lit):
    x, y, w, h = key_rect(i)
    g = sc.grid(x - 1, y - 1, x + w + 1, y + h + 1)
    if g is None: return
    sl, X, Y = g
    d = sd_rrect(X, Y, x, y, w, h, 3)
    m = sc.cov(d)
    top = (PLATE_H - 0.6) if down else (PLATE_H + 1.1)
    base = PLATE_H - 2.6
    e = 1 - (1 - np.clip(-d / 1.6, 0, 1)) ** 2
    dome = 0.25 * (1 - ((X - (x + w / 2)) / (w / 2)) ** 2) * (1 - ((Y - (y + h / 2)) / (h / 2)) ** 2)
    hh = base + (top - base + dome) * e
    t = np.clip((Y - y) / h, 0, 1)[..., None]
    up0, up1 = hexc('a8352f'), hexc('8a2424')
    dn0, dn1 = hexc('6e1a1b'), hexc('7a2021')
    c0, c1 = (dn0, dn1) if down else (up0, up1)
    col = c0[None, None, :] * (1 - t) + c1[None, None, :] * t
    sc.put(sl, m, h=hh, alb=col, rough=0.3, metal=0.0)
    # legend
    legm = sc.text_mask([dict(font=F_BAR6, size=11.5, text=KEY_TEXT[i], x=x + w / 2, y=y + h / 2 + (0.3 if down else 0), track=0.08)])
    if legend_lit:
        sc.ink(legm, KEYON * 0.6, raise_h=0.0, rough=0.4, emis=KEYON * 1.15)
    else:
        sc.ink(legm, hexc('e3a596') * 0.85, raise_h=0.0, rough=0.4)

def jewel(sc, cx, cy, lit):
    g = sc.grid(cx - 8, cy - 8, cx + 8, cy + 8)
    if g is None: return
    sl, X, Y = g
    r = np.hypot(X - cx, Y - cy)
    bez = sc.cov(r - 6.6)
    sc.put(sl, bez, h=PLATE_H + 0.9 * (1 - (1 - np.clip((6.6 - r) / 1.4, 0, 1)) ** 2), alb=lin([60, 58, 60]), rough=0.22, metal=1.0)
    lens = sc.cov(r - 4.6)
    hl = PLATE_H + 0.6 + 1.3 * np.sqrt(np.clip(1 - (r / 4.6) ** 2, 0, 1))
    if lit:
        sc.put(sl, lens, h=hl, alb=LAMP * 0.6, rough=0.08, emis=LAMP[None, None, :] * (0.45 + 0.5 * np.clip(1 - r / 4.6, 0, 1))[..., None])
    else:
        sc.put(sl, lens, h=hl, alb=hexc('5a1c0c'), rough=0.08, emis=np.zeros(3))

def mode_button(sc):
    cx, cy = MODE_BTN
    g = sc.grid(cx - 21, cy - 21, cx + 21, cy + 21)
    if g is None: return
    sl, X, Y = g
    r = np.hypot(X - cx, Y - cy)
    collar = sc.cov(r - 19)
    sc.put(sl, collar, h=PLATE_H + 1.2 * (1 - (1 - np.clip((19 - r) / 1.5, 0, 1)) ** 2), alb=lin([44, 42, 46]), rough=0.3, metal=1.0)
    hole = sc.cov(r - 14.8)
    sc.put(sl, hole, h=PLATE_H - 1.0, alb=hexc('0a0303'), rough=0.8)
    cap = sc.cov(r - 13.6)
    hc = PLATE_H + 1.0 + 3.4 * np.sqrt(np.clip(1 - (r / 13.6) ** 2, 0, 1)) ** 0.6
    sc.put(sl, cap, h=hc, alb=lin([20, 20, 24]), rough=0.28, metal=0.0)

def bezel(sc, outer, inner, face_h, face_alb, face_rough):
    x, y, w, h = outer
    g = sc.grid(x - 2, y - 2, x + w + 2, y + h + 2)
    if g is None: return
    sl, X, Y = g
    d = sd_rrect(X, Y, x, y, w, h, 6)
    m = sc.cov(d)
    e = 1 - (1 - np.clip(-d / 2.4, 0, 1)) ** 2
    sc.put(sl, m, h=PLATE_H + 2.6 * e, alb=lin([22, 20, 22]), rough=0.4, metal=0.0)
    ix, iy, iw, ih = inner
    di = sd_rrect(X, Y, ix, iy, iw, ih, 3)
    mi = sc.cov(di)
    wall = np.clip(-di / 0.8, 0, 1)
    hf = face_h + (PLATE_H + 2.6 - face_h) * (1 - wall)
    fa = face_alb(X, Y) if callable(face_alb) else face_alb
    sc.put(sl, mi, h=hf, alb=fa, rough=face_rough, metal=0.0)

def face_albedo(X, Y):
    cx, cy = FACE[0] + FACE[2] / 2, FACE[1] + FACE[3]
    rx, ry = 0.7 * FACE[2], 0.85 * FACE[3]
    t = np.clip(np.hypot((X - cx) / rx, (Y - cy) / ry), 0, 1)[..., None]
    c0, c1, c2 = hexc('2c1a12'), hexc('1a0f09'), hexc('110905')
    a = np.where(t < 0.6, c0 * (1 - t / 0.6) + c1 * (t / 0.6), c1 * (1 - (t - 0.6) / 0.4) + c2 * ((t - 0.6) / 0.4))
    return a.astype(np.float32)

def meter_face(sc, out_mode):
    FACE_H = PLATE_H - 1.6
    bezel(sc, BEZEL, FACE, FACE_H, face_albedo, 0.95)
    g = sc.grid(*FACE[:2], FACE[0] + FACE[2], FACE[1] + FACE[3])
    if g is None: return
    sl, X, Y = g
    px, py = PIV
    inside = sc.cov(sd_rrect(X, Y, *FACE, 3))
    ink = np.zeros_like(X)
    def seg(a, r0, r1, w):
        ax, ay = px + r0 * math.sin(a), py - r0 * math.cos(a)
        bx, by = px + r1 * math.sin(a), py - r1 * math.cos(a)
        return sc.cov(sd_seg(X, Y, ax, ay, bx, by, w / 2))
    rr = np.hypot(X - px, Y - py); ang = np.arctan2(X - px, -(Y - py))
    arc = sc.cov(np.maximum(np.abs(rr - METER_R) - 0.6, (np.abs(ang) - SPAN) * METER_R))
    ink = np.maximum(ink, arc * 0.9)
    labels = []
    if not out_mode:
        for d in range(21):
            if d % 4 == 0: ink = np.maximum(ink, seg(gr_angle(d), METER_R - 14, METER_R, 1.5))
            else: ink = np.maximum(ink, seg(gr_angle(d), METER_R - 7, METER_R, 1.0) * 0.75)
        for d in (0, 4, 8, 12, 16, 20):
            a = gr_angle(d); labels.append((str(d), px + (METER_R + 18) * math.sin(a), py - (METER_R + 18) * math.cos(a)))
        cap = 'GAIN REDUCTION'
    else:
        for d in (-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3): ink = np.maximum(ink, seg(meter_angle(vu_frac(d)), METER_R - 14, METER_R, 1.5))
        for d in (-15, -6, -4, -0.5, 0.5, 1.5, 2.5): ink = np.maximum(ink, seg(meter_angle(vu_frac(d)), METER_R - 7, METER_R, 1.0) * 0.75)
        for d in (-20, -10, -5, -3, 0, 3):
            a = meter_angle(vu_frac(d)); t = ('+' + str(d)) if d > 0 else str(d).replace('-', '−')
            labels.append((t, px + (METER_R + 18) * math.sin(a), py - (METER_R + 18) * math.cos(a)))
        cap = 'OUTPUT LEVEL'
    full = np.zeros((sc.H, sc.W), np.float32); full[sl] = ink * inside
    items = [dict(font=F_BAR6, size=16, text=t, x=x, y=y) for t, x, y in labels]
    items.append(dict(font=F_BAR5, size=10, text=cap, x=FACE[0] + 28, y=FACE[1] + 206, anchor='l', track=0.14))
    items.append(dict(font=F_BAR5, size=10, text='dB', x=FACE[0] + FACE[2] - 28, y=FACE[1] + 206, anchor='r', track=0.06))
    full = np.maximum(full, sc.text_mask(items))
    sc.ink(full, FACE_INK, raise_h=0.0, rough=0.7)
    # warm backlight behind the face (strongest low in the middle, like a lamp under the scale)
    cx, cy = FACE[0] + FACE[2] / 2, FACE[1] + FACE[3] * 0.78
    glow = np.exp(-(((X - cx) / 230) ** 2 + ((Y - cy) / 120) ** 2))
    sc.emis[sl] += (hexc('ff9a4a') * 0.035)[None, None, :] * (glow * inside)[..., None]
    # GR / OUT legends: the active one is lit from behind
    for t, x, on in (('GR', 750, not out_mode), ('OUT', 790, out_mode)):
        mk = sc.text_mask([dict(font=F_BAR6, size=11, text=t, x=x, y=77, track=0.1)])
        if on: sc.ink(mk, FACE_INK, raise_h=0.0, rough=0.6, emis=hexc('ffe9c4') * 0.55)
        else: sc.ink(mk, FACE_INK * 0.22, raise_h=0.0, rough=0.7)
    # pivot dome
    g2 = sc.grid(px - 76, FACE[1] + FACE[3] - 14, px + 76, FACE[1] + FACE[3] + 1)
    if g2 is not None:
        sl2, X2, Y2 = g2
        r2 = np.hypot(X2 - px, Y2 - py)
        dm = sc.cov(r2 - 74) * sc.cov(sd_rrect(X2, Y2, *FACE, 3))
        hd = FACE_H + 6.0 * np.sqrt(np.clip(1 - (r2 / 74) ** 2, 0, 1))
        sc.put(sl2, dm, h=hd, alb=lin([8, 6, 5]), rough=0.22)

def display(sc):
    bezel(sc, DISP, SCREEN, PLATE_H - 1.2, hexc('130506'), 0.3)

def scale_ticks(sc, k):
    """gold scale around a knob: ticks outside the lamp ring and end/centre numbers"""
    cx, cy, rr = k['cx'], k['cy'], k['rr']
    g = sc.grid(cx - rr - 14, cy - rr - 14, cx + rr + 14, cy + rr + 14)
    if g is None: return None, []
    sl, X, Y = g
    m = np.zeros_like(X)
    def tick(pos, r0, r1, w):
        a = math.radians(-135 + 270 * pos)
        return sc.cov(sd_seg(X, Y, cx + r0 * math.sin(a), cy - r0 * math.cos(a), cx + r1 * math.sin(a), cy - r1 * math.cos(a), w / 2))
    items = []
    big = rr > 50
    r0 = rr + 4.5
    if big:
        for i in range(21):
            maj = i % 2 == 0
            m = np.maximum(m, tick(i / 20, r0, r0 + (5.5 if maj else 3.2), 1.1 if maj else 0.8))
        for v in range(0, 101, 20):
            a = math.radians(-135 + 270 * v / 100); rn = rr + 17
            items.append(dict(font=F_BAR6, size=9.5, text=str(v), x=cx + rn * math.sin(a), y=cy - rn * math.cos(a)))
    else:
        ticks = {'input': 9, 'output': 9, 'mix': 11, 'noise': 7}
        nt = ticks[k['id']]
        for i in range(nt):
            maj = i in (0, nt - 1, (nt - 1) // 2)
            m = np.maximum(m, tick(i / (nt - 1), r0, r0 + (4.2 if maj else 2.8), 1.0 if maj else 0.75))
        ends = {'input': ('−24', '0', '+24'), 'output': ('−24', '0', '+24'), 'mix': ('0', '50', '100'),
                'noise': ('−120', '−90', '−60')}[k['id']]
        rn = rr + 9.5
        for pos, t, side in ((0.0, ends[0], 'r'), (1.0, ends[2], 'l')):
            a = math.radians(-135 + 270 * pos)
            ex, ey = cx + rn * math.sin(a), cy - rn * math.cos(a)
            items.append(dict(font=F_BAR6, size=8.5, text=t, x=ex + (2.0 if side == 'r' else -2.0), y=ey + 6.5, anchor=side))
        items.append(dict(font=F_BAR6, size=8.5, text=ends[1], x=cx, y=cy - rr - 14))
    full = np.zeros((sc.H, sc.W), np.float32); full[sl] = m
    return full, items

# ------------------------------------------------------------------ whole panel
def default_state():
    return dict(knob_pos={'pr': 0.0, 'input': 0.5, 'output': 0.5, 'mix': 0.0, 'noise': 0.0},
                keys_down=[False] * 7, keys_lit=[False] * 7, lamps=[False, False], lamp_rings={},
                out_mode=False, fader=None, knobs=True)

def build(region, state, k=K):
    sc = Scene(region, k)
    chassis(sc)
    for x, w in MODULES: plate(sc, x, w)
    gold = np.zeros((sc.H, sc.W), np.float32)
    for x, w in MODULES:
        z = zari(sc, x, w)
        if z is not None: gold = np.maximum(gold, z)
    items = []
    # logo, debossed
    logo = sc.text_mask([dict(font=F_CAT8, size=44, text='அன்பு', x=134, y=88.52, baseline=True),
                         dict(font=F_CAT6, size=13, text='by R4N', x=134, y=116.3, baseline=True)])
    if logo.max() > 0 and state.get('logo', True):
        lb = ndimage.gaussian_filter(logo, 0.3 * sc.k)
        sc.hgt -= 0.9 * lb
        sc.alb *= (1 - 0.18 * lb)[..., None]
    # knob scales + legends
    for kid, kk in KNOBS.items():
        kk = dict(kk, id=kid)
        m, its = scale_ticks(sc, kk)
        if m is not None: gold = np.maximum(gold, m)
        items += its
    items += [dict(font=F_BAR6, size=12, text='PEAK REDUCTION', x=134, y=312, track=0.16),
              dict(font=F_BAR6, size=10.5, text='INPUT', x=921, y=166, track=0.16),
              dict(font=F_BAR6, size=10.5, text='OUTPUT', x=921, y=292, track=0.16),
              dict(font=F_BAR6, size=10.5, text='MIX', x=921, y=418, track=0.16),
              dict(font=F_BAR6, size=10.5, text='NOISE LEVEL', x=921, y=600, track=0.1),
              dict(font=F_BAR6, size=12, text='COLOR', x=134, y=556, track=0.16),
              dict(font=F_BAR6, size=11, text='COMPRESS', x=71, y=LAMP_YC, anchor='l', track=0.14),
              dict(font=F_BAR6, size=11, text='LIMIT', x=71, y=LAMP_YL, anchor='l', track=0.14),
              dict(font=F_BAR6, size=10.5, text='OVERSAMPLING', x=420, y=566, track=0.16),
              dict(font=F_BAR6, size=10.5, text='PHASE', x=710, y=566, track=0.16)]
    # fader scale
    g = sc.grid(150, 330, 210, 545)
    if g is not None:
        sl, X, Y = g
        m = np.zeros_like(X)
        for i in range(11):
            y = FADER_Y1 - (FADER_Y1 - FADER_Y0) * i / 10
            maj = i % 5 == 0
            m = np.maximum(m, sc.cov(sd_rrect(X, Y, FADER_X + 28, y - 0.5, 12 if maj else 7, 1.0 if maj else 0.8, 0.3)))
        gold[sl] = np.maximum(gold[sl], m)
    for i in (0, 5, 10):
        y = FADER_Y1 - (FADER_Y1 - FADER_Y0) * i / 10
        items.append(dict(font=F_BAR6, size=9.5, text=str(i * 10), x=FADER_X + 46, y=y, anchor='l'))
    # section brackets around the key banks + the right-hand divider
    g = sc.grid(270, 555, 840, 580)
    if g is not None:
        sl, X, Y = g
        m = np.zeros_like(X)
        for (bx, bw, cx, half) in ((280, 280, 420, 48), (590, 240, 710, 24)):
            yl = 566
            m = np.maximum(m, sc.cov(sd_rrect(X, Y, bx, yl - 0.5, cx - half - bx, 1.0, 0.2)))
            m = np.maximum(m, sc.cov(sd_rrect(X, Y, cx + half, yl - 0.5, bx + bw - cx - half, 1.0, 0.2)))
            m = np.maximum(m, sc.cov(sd_rrect(X, Y, bx, yl - 0.5, 1.0, 7, 0.2)))
            m = np.maximum(m, sc.cov(sd_rrect(X, Y, bx + bw - 1, yl - 0.5, 1.0, 7, 0.2)))
        gold[sl] = np.maximum(gold[sl], m)
    g = sc.grid(880, 440, 962, 449)
    if g is not None:
        sl, X, Y = g
        gold[sl] = np.maximum(gold[sl], sc.cov(sd_rrect(X, Y, 884, 444, 74, 1.0, 0.3)) * 0.8)
    gold = np.maximum(gold, sc.text_mask(items))
    sc.ink(gold, GOLD, raise_h=0.04, rough=0.38, metal=0.35)

    # screws
    ang = [118, 37, 152, 64, 97, 21, 140, 75]
    i = 0
    for x, w in MODULES:
        xs = [x + 36, x + w - 36] if w > 300 else [x + w / 2]
        for sx in xs:
            for sy in (36, 664):
                screw(sc, sx, sy, ang[i % len(ang)]); i += 1
    # lamp rings, knobs
    for kid, kk in KNOBS.items():
        lamp_ring(sc, kk['cx'], kk['cy'], kk['rr'], lit=state['lamp_rings'].get(kid, False))
    if state['knobs']:
        for kid, kk in KNOBS.items():
            pos = state['knob_pos'][kid]
            knob(sc, kk['cx'], kk['cy'], kk['D'], -135 + 270 * pos, 90 if kk['D'] > 80 else 60)
    fader_slot(sc)
    if state['fader'] is not None:
        fader_cap(sc, FADER_Y1 - (FADER_Y1 - FADER_Y0) * state['fader'])
    jewel(sc, LAMP_X, LAMP_YC, state['lamps'][0])
    jewel(sc, LAMP_X, LAMP_YL, state['lamps'][1])
    mode_button(sc)
    meter_face(sc, state['out_mode'])
    display(sc)
    bank(sc, BANK_OS); bank(sc, BANK_PH)
    for i in range(7):
        key(sc, i, state['keys_down'][i], state['keys_lit'][i])
    return sc

# ------------------------------------------------------------------ render + post
def render(sc, binary, work, samples=24):
    d = os.path.join(work, 'maps')
    sc.save(d)
    subprocess.run([binary, d, str(sc.W), str(sc.H), str(sc.k), str(samples)], check=True)
    out = np.fromfile(os.path.join(d, 'out.f32'), np.float32).reshape(sc.H, sc.W, 3)
    # light from emitters spills onto the surroundings
    if sc.emis.max() > 0:
        e = sc.emis
        spill = np.stack([ndimage.gaussian_filter(e[..., c], 2.2 * sc.k) for c in range(3)], -1) * 0.7 \
              + np.stack([ndimage.gaussian_filter(e[..., c], 7.0 * sc.k) for c in range(3)], -1) * 0.4
        out = out + spill * (0.35 + sc.alb) + np.stack([ndimage.gaussian_filter(e[..., c], 1.0 * sc.k) for c in range(3)], -1) * 0.25
    return out

def light_falloff(sc, img):
    # the studio key is a large softbox up and to the left: a gentle fall-off across the panel
    ys = sc.y0 + (np.arange(sc.H) + 0.5) / sc.k
    xs = sc.x0 + (np.arange(sc.W) + 0.5) / sc.k
    X, Y = np.meshgrid(xs, ys)
    d = np.hypot((X + 150) / 1000.0, (Y + 150) / 700.0)
    f = 1.10 - 0.16 * np.clip(d - 0.2, 0, 1.4)
    return img * f[..., None].astype(np.float32)

def to_srgb8(lin_img, ss=SS):
    a = lin_img
    if ss > 1:
        H, W = a.shape[0] // ss * ss, a.shape[1] // ss * ss
        a = a[:H, :W].reshape(H // ss, ss, W // ss, ss, 3).mean(axis=(1, 3))
    knee = 0.82
    a = np.where(a > knee, knee + (1 - knee) * np.tanh((a - knee) / (1 - knee)), a)
    a = np.clip(a, 0, 1)
    s = np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1 / 2.4) - 0.055)
    return (np.clip(s, 0, 1) * 255 + 0.5).astype(np.uint8)

if __name__ == '__main__':
    binary, work, outdir = sys.argv[1:4]
    what = sys.argv[4:] or ['test']
    os.makedirs(outdir, exist_ok=True)
    if 'test' in what:
        st = default_state()
        st['knob_pos'].update(pr=0.42, output=27 / 48, mix=1.0, noise=0.4)
        st['fader'] = 0.35
        st['lamps'] = [True, False]
        st['keys_down'][2] = st['keys_lit'][2] = True
        st['keys_down'][4] = st['keys_lit'][4] = True
        if os.environ.get('RINGS'): st['lamp_rings'] = {k: True for k in KNOBS}
        region = tuple(float(v) for v in os.environ.get('REGION', '0,0,1000,700').split(','))
        kk = float(os.environ.get('K', K))
        sc = build(region, st, k=kk)
        out = light_falloff(sc, render(sc, binary, work, int(os.environ.get('SAMPLES', 16))))
        Image.fromarray(to_srgb8(out, int(os.environ.get('SSD', SS)))).save(os.path.join(outdir, 'test.png'))
        print('test done', sc.W, sc.H)
