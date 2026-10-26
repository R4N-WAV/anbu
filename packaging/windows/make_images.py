"""Artwork for the Windows setup wizard, cut from Anbu's own rendered panel.

usage: python3 packaging/windows/make_images.py <panel.png> [scale]
  panel.png: the panel rendered by the snapshot tool at that scale (default 2.5):
      AnbuSnapshot panel.png 2.5 panel
  (needs Pillow with libraqm for the Tamil shaping)
writes next to this script: wizard.png (the tall side image), wizard-small.png (the corner badge) and anbu.ico
"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, '..', '..', 'Resources', 'ui')
STRIP = 36                      # the preset strip above the panel (design units)
BG = (22, 8, 8)                 # the wizard's background (the chassis, a touch darker)
GOLD = (232, 196, 110)
LAMP = (255, 179, 65)
DIM = (176, 136, 120)
LOGO = 'அன்பு'                  # அன்பு


def font(name, size):
    return ImageFont.truetype(os.path.join(UI, name), size, layout_engine=ImageFont.Layout.RAQM)


def spaced(s):
    return ' '.join(s)


def centred(d, y, text, f, fill, w):
    b = d.textbbox((0, 0), text, font=f)
    d.text(((w - (b[2] - b[0])) / 2 - b[0], y - b[1]), text, font=f, fill=fill)


def tall(panel, s):
    """164 x 314 (x3): the left module (logo, Peak reduction knob with its lit ring), fading into the chassis,
    and the product line underneath"""
    W, H = 164 * 3, 314 * 3
    x0, x1 = 14, 254                      # design units: the module with a little chassis either side
    k = W / (x1 - x0)                     # output px per design unit
    y0 = 14
    y1 = y0 + 0.80 * H / k
    crop = panel.crop((int(x0 * s), int((y0 + STRIP) * s), int(x1 * s), int((y1 + STRIP) * s)))
    crop = crop.resize((W, int(round((y1 - y0) * k))), Image.LANCZOS)
    img = Image.new('RGB', (W, H), BG)
    img.paste(crop, (0, 0))
    # fade the module into the dark below
    fade = Image.new('L', (W, H), 0)
    fd = ImageDraw.Draw(fade)
    top = int(crop.height * 0.62)
    for y in range(top, H):
        fd.line((0, y, W, y), fill=int(255 * min(1.0, ((y - top) / (crop.height - top)) ** 1.6)))
    img = Image.composite(Image.new('RGB', (W, H), BG), img, fade)
    # a soft amber glow behind the captions, like lamp light on the chassis
    glow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(glow).ellipse((-W * 0.3, H * 0.80, W * 1.3, H * 1.25), fill=40)
    glow = glow.filter(ImageFilter.GaussianBlur(W * 0.18))
    img = Image.composite(Image.new('RGB', (W, H), LAMP), img, glow)
    d = ImageDraw.Draw(img)
    centred(d, int(H * 0.855), spaced('ANALOG WARM COLOR STAGE'), font('barlow600.ttf', 23), GOLD, W)
    centred(d, int(H * 0.905), 'Anbu v1  ·  by R4N', font('barlow500.ttf', 27), DIM, W)
    centred(d, int(H * 0.945), spaced('VST3  ·  64-BIT'), font('barlow600.ttf', 22), (130, 96, 86), W)
    return img


def badge(panel, s, px=174):
    """the corner badge: the real Peak reduction knob and its lit ring, in a round window on the dark"""
    cx, cy, r = 134.0, 226.0 + STRIP, 74.0          # design units: knob centre, enough to hold the lamp ring
    crop = panel.crop((int((cx - r) * s), int((cy - r) * s), int((cx + r) * s), int((cy + r) * s))).resize((px, px), Image.LANCZOS)
    img = Image.new('RGB', (px, px), BG)
    m = Image.new('L', (px * 4, px * 4), 0)
    ImageDraw.Draw(m).ellipse((6, 6, px * 4 - 6, px * 4 - 6), fill=255)
    m = m.resize((px, px), Image.LANCZOS)
    img.paste(crop, (0, 0), m)
    return img


def icon(px=256):
    """the program icon: a dark disc, the amber lamp arc and the Tamil 'a' in gold (clean at 16 px too)"""
    S = 4
    w = px * S
    img = Image.new('RGBA', (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((w * 0.02, w * 0.02, w * 0.98, w * 0.98), fill=(118, 30, 30, 255))
    d.ellipse((w * 0.07, w * 0.07, w * 0.93, w * 0.93), fill=(30, 10, 10, 255))
    lw = int(w * 0.075)
    box = (w * 0.16, w * 0.16, w * 0.84, w * 0.84)
    d.arc(box, 135, 405, fill=(60, 24, 14, 255), width=lw)
    glow = Image.new('L', (w, w), 0)
    ImageDraw.Draw(glow).arc(box, 135, 330, fill=255, width=lw)
    glow = glow.filter(ImageFilter.GaussianBlur(w * 0.025))
    img = Image.alpha_composite(img, Image.merge('RGBA', (*Image.new('RGB', (w, w), LAMP).split(), glow.point(lambda v: int(v * 0.8)))))
    d = ImageDraw.Draw(img)
    d.arc(box, 135, 330, fill=LAMP + (255,), width=lw)
    f = font('catamaran800.ttf', int(w * 0.40))
    b = d.textbbox((0, 0), 'அ', font=f)
    d.text(((w - (b[2] - b[0])) / 2 - b[0], (w - (b[3] - b[1])) / 2 - b[1] + w * 0.01), 'அ', font=f, fill=GOLD + (255,))
    return img.resize((px, px), Image.LANCZOS)


if __name__ == '__main__':
    panel = Image.open(sys.argv[1]).convert('RGB')
    s = float(sys.argv[2]) if len(sys.argv) > 2 else 2.5
    tall(panel, s).save(os.path.join(HERE, 'wizard.png'), optimize=True)
    badge(panel, s).save(os.path.join(HERE, 'wizard-small.png'), optimize=True)
    icon(256).save(os.path.join(HERE, 'anbu.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print('done')
