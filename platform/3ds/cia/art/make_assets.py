#!/usr/bin/env python3
"""Draws and synthesises the HOME-menu assets of Sanctuary Pocket.

    python3 platform/3ds/cia/art/make_assets.py

writes, next to this directory:
    ../icon.png      48x48   the SMDH icon (bannertool scales the 24x24 one from it itself)
    ../banner.png    256x128 the banner picture
    ../banner.wav    2.8 s, 16-bit stereo PCM at 32000 Hz; the HOME menu stays silent from 3 s up
    preview/*.png    enlarged copies to look at

Everything is made here from nothing: shapes drawn with numpy and Pillow, sound added up from
sines. No picture, sound, font or text of any game goes in. The only outside input is the
typeface of the title, Linux Libertine Display (Philipp H. Poll; SIL Open Font License 1.1, or
GPL 2+ with the font exception), read from the system (Debian/Ubuntu: fonts-linuxlibertine) or
from --font. Only the rendered letters end up in banner.png; the font file is not shipped.

Needs: numpy, Pillow.
"""
import argparse
import math
import os
import sys
import wave

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)
FONT_PATHS = [
    "/usr/share/fonts/opentype/linux-libertine/LinLibertine_DR.otf",
    "/usr/share/fonts/linux-libertine-fonts/LinLibertine_DR.otf",
    "/usr/share/fonts/OTF/LinLibertine_DR.otf",
]

BLACK = (9, 5, 6)
BLOOD = (112, 14, 16)
EMBER = (226, 98, 22)
GOLD = (255, 196, 84)
PALE = (255, 240, 190)


# ---------------------------------------------------------------- pictures

def ramp(t, stops):
    """Colours along t in 0..1 through `stops` [(t, rgb), ...]; t is an array."""
    t = np.clip(t, 0.0, 1.0)
    out = np.zeros(t.shape + (3,), np.float32)
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        k = np.clip((t - t0) / (t1 - t0), 0.0, 1.0)[..., None]
        m = ((t >= t0) & (t <= t1))[..., None]
        out = np.where(m, np.asarray(c0, np.float32) * (1 - k) + np.asarray(c1, np.float32) * k, out)
    return out


def rgba(rgb, alpha):
    a = np.clip(alpha, 0.0, 1.0)[..., None] * 255.0
    return Image.fromarray(np.concatenate([np.clip(rgb, 0, 255), a], -1).astype(np.uint8), "RGBA")


def arch_mask(x, y, cx, spring, half, foot):
    """A lancet arch: a rectangle from `foot` up to the spring line, and above it the lens of
    two circles of radius 2*half centred on the opposite springing points."""
    body = (np.abs(x - cx) <= half) & (y >= spring) & (y <= foot)
    r = 2 * half
    head = (y < spring) & (np.hypot(x - (cx - half), y - spring) <= r) & (np.hypot(x - (cx + half), y - spring) <= r)
    return body | head


def flame_points(cx, base, height, width, lean=0.0, n=40):
    """The outline of a flame: a teardrop whose tip curls to one side."""
    left, right = [], []
    for i in range(n + 1):
        t = i / n                                   # 0 at the base, 1 at the tip
        w = width * math.sin(math.pi * min(1.0, t * 1.15 + 0.08)) ** 0.9 * (1 - t) ** 0.55
        mid = cx + lean * width * math.sin(t * math.pi * 1.1) * t
        y = base - height * t
        left.append((mid - w, y))
        right.append((mid + w, y))
    return left + right[::-1]


def emblem(size):
    """The mark: a glowing lancet doorway with a flame burning in it. Square RGBA, `size` px,
    transparent around the doorway's glow."""
    s = size
    y, x = np.mgrid[0:s, 0:s].astype(np.float32)
    cx, spring, half, foot = s * 0.5, s * 0.565, s * 0.215, s * 0.87
    outer = arch_mask(x, y, cx, spring + s * 0.01, half + s * 0.065, foot + s * 0.03)
    inner = arch_mask(x, y, cx, spring, half, foot)

    # The opening: embers at the floor, blood red up in the vault.
    heat = np.clip(1.25 - np.hypot((x - cx) / (s * 0.30), (y - s * 0.74) / (s * 0.52)), 0, 1)
    opening = ramp(heat, [(0, (38, 4, 8)), (0.35, BLOOD), (0.7, EMBER), (1, GOLD)])
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))

    # Glow around the doorway, then the stone frame, then the opening.
    halo = Image.fromarray((outer * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(s * 0.07))
    halo_a = np.asarray(halo, np.float32) / 255.0
    img.alpha_composite(rgba(np.broadcast_to(np.asarray(EMBER, np.float32), (s, s, 3)), halo_a * 0.75))
    frame = ramp((y - s * 0.05) / (s * 0.9), [(0, GOLD), (0.5, (196, 120, 40)), (1, (120, 52, 18))])
    img.alpha_composite(rgba(frame, outer.astype(np.float32)))
    img.alpha_composite(rgba(opening, inner.astype(np.float32)))

    # The flame: a dark-red body, an ember middle and a pale heart, so it reads at 48 px.
    d = ImageDraw.Draw(img)
    base = s * 0.84
    d.polygon(flame_points(cx, base, s * 0.52, s * 0.155, 0.45), fill=(74, 6, 10, 255))
    d.polygon(flame_points(cx, base, s * 0.43, s * 0.113, 0.40), fill=EMBER + (255,))
    d.polygon(flame_points(cx, base, s * 0.31, s * 0.072, 0.30), fill=GOLD + (255,))
    d.polygon(flame_points(cx, base, s * 0.18, s * 0.036, 0.20), fill=PALE + (255,))
    # The threshold.
    d.rectangle([cx - half - s * 0.11, foot + s * 0.02, cx + half + s * 0.11, foot + s * 0.065], fill=(150, 78, 26, 255))
    return img


def backdrop(w, h, glow_at):
    """Near-black with a blood-red glow around `glow_at` (x, y in 0..1) and a faint grain."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.hypot((x / w - glow_at[0]) * (w / h), y / h - glow_at[1])
    rgb = ramp(1 - d / 0.95, [(0, BLACK), (0.55, (34, 6, 9)), (1, (92, 14, 14))])
    grain = np.random.default_rng(1996).normal(0, 2.5, (h, w, 1)).astype(np.float32)
    return rgb + grain


def rounded(w, h, r, inset=0.0):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([inset, inset, w - 1 - inset, h - 1 - inset], radius=r, fill=255)
    return m


def make_icon(scale=10):
    s = 48 * scale
    bg = rgba(backdrop(s, s, (0.5, 0.62)), np.ones((s, s), np.float32))
    plate = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    plate.paste(bg, (0, 0), rounded(s, s, s * 0.12))
    # An ember-gold rim.
    rim = ImageChops.subtract(rounded(s, s, s * 0.12), rounded(s, s, s * 0.10, s * 0.035))
    plate.paste(Image.new("RGBA", (s, s), (176, 108, 36, 255)), (0, 0), rim)
    plate.alpha_composite(emblem(s))
    # The icon is shown on an opaque tile: flatten onto black.
    flat = Image.new("RGBA", (s, s), BLACK + (255,))
    flat.alpha_composite(plate)
    return flat.resize((48, 48), Image.LANCZOS).convert("RGB")


def find_font(path):
    for p in ([path] if path else []) + FONT_PATHS:
        if p and os.path.exists(p):
            return p
    sys.exit("Linux Libertine Display not found; install fonts-linuxlibertine or pass --font LinLibertine_DR.otf")


def lettering(text, font, tracking):
    """`text` as a mask, the letters spaced `tracking` px apart."""
    widths = [font.getlength(c) for c in text]
    asc, desc = font.getmetrics()
    w = int(sum(widths) + tracking * (len(text) - 1)) + 8
    m = Image.new("L", (w, asc + desc), 0)
    d, pen = ImageDraw.Draw(m), 4.0
    for c, cw in zip(text, widths):
        d.text((pen, 0), c, font=font, fill=255)
        pen += cw + tracking
    return m.crop(m.getbbox())


def gilded(mask):
    """A mask filled pale gold at the top running to ember at the foot, over a dark edge."""
    w, h = mask.size
    pad = max(4, h // 5)
    out = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    big = Image.new("L", out.size, 0)
    big.paste(mask, (pad, pad))
    edge = big.filter(ImageFilter.MaxFilter(2 * (pad // 4) + 3)).filter(ImageFilter.GaussianBlur(pad / 5))
    out.paste(Image.new("RGBA", out.size, (30, 3, 5, 255)), (0, 0), edge)
    t = np.broadcast_to(np.linspace(0, 1, h, dtype=np.float32)[:, None], (h, w))
    fill = rgba(ramp(t, [(0, PALE), (0.45, GOLD), (1, (214, 104, 28))]), np.asarray(mask, np.float32) / 255.0)
    out.alpha_composite(fill, (pad, pad))
    return out, pad


def make_banner(font_path, scale=4):
    W, H = 256 * scale, 128 * scale
    # The HOME menu shows the picture on a tilted card in a 3D frame and loses its lower edge,
    # so the plaque stops at y=112 of 128 and nothing that matters sits below y=100.
    pw, ph = W - 12 * scale, 104 * scale
    px, py = 6 * scale, 8 * scale
    plaque = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
    bg = rgba(backdrop(pw, ph, (0.19, 0.6)), np.ones((ph, pw), np.float32))
    plaque.paste(bg, (0, 0), rounded(pw, ph, 10 * scale))
    rim = ImageChops.subtract(rounded(pw, ph, 10 * scale), rounded(pw, ph, 8.5 * scale, 1.5 * scale))
    plaque.paste(Image.new("RGBA", (pw, ph), (176, 108, 36, 255)), (0, 0), rim)
    d = ImageDraw.Draw(plaque)
    d.rounded_rectangle([3.5 * scale, 3.5 * scale, pw - 1 - 3.5 * scale, ph - 1 - 3.5 * scale],
                        radius=7 * scale, outline=(96, 30, 16, 255), width=max(1, scale // 2))

    em = emblem(104 * scale)
    plaque.alpha_composite(em, (-6 * scale, (ph - em.height) // 2 - 1 * scale))

    # The title, two lines of capitals right of the doorway, a rule and a lozenge between them.
    left, right = 88 * scale, pw - 10 * scale
    mid = (left + right) // 2
    top = lettering("SANCTUARY", ImageFont.truetype(font_path, 27 * scale), 1.2 * scale)
    low = lettering("POCKET", ImageFont.truetype(font_path, 27 * scale), 5.0 * scale)
    for mask, ytop in ((top, 22 * scale), (low, 58 * scale)):
        if mask.width > right - left:
            mask = mask.resize((right - left, mask.height), Image.LANCZOS)
        g, pad = gilded(mask)
        plaque.alpha_composite(g, (mid - mask.width // 2 - pad, ytop - pad))
    ry = 49 * scale
    d.line([left + 4 * scale, ry, mid - 7 * scale, ry], fill=(176, 108, 36, 255), width=max(1, scale // 2))
    d.line([mid + 7 * scale, ry, right - 4 * scale, ry], fill=(176, 108, 36, 255), width=max(1, scale // 2))
    r = 3.2 * scale
    d.polygon([(mid, ry - r), (mid + r * 1.4, ry), (mid, ry + r), (mid - r * 1.4, ry)], fill=GOLD + (255,))

    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # A soft shadow under the plaque, then the plaque.
    sh = Image.new("L", (W, H), 0)
    sh.paste(rounded(pw, ph, 10 * scale), (px, py + 3 * scale))
    sh = sh.filter(ImageFilter.GaussianBlur(3 * scale)).point(lambda v: v * 0.6)
    out.paste(Image.new("RGBA", (W, H), (0, 0, 0, 255)), (0, 0), sh)
    out.alpha_composite(plaque, (px, py))
    return out.resize((256, 128), Image.LANCZOS)


# ---------------------------------------------------------------- sound

RATE = 32000
SECONDS = 2.8          # the HOME menu plays no banner sound of 3 s or more


def env(t, attack, hold_until, release):
    """0 -> 1 over `attack`, 1 until `hold_until`, then a cosine fall over `release`."""
    up = np.clip(t / attack, 0, 1) ** 1.5
    down = 0.5 + 0.5 * np.cos(np.pi * np.clip((t - hold_until) / release, 0, 1))
    return up * down


def bell(t, start, f0, level):
    """A struck bell: the inharmonic partials of a church bell, the high ones dying first."""
    partials = [(0.5, 1.0, 1.1), (1.0, 0.9, 1.6), (1.19, 0.7, 2.2), (1.5, 0.45, 2.8), (2.0, 0.6, 3.2),
                (2.51, 0.3, 4.5), (3.01, 0.22, 5.5), (4.1, 0.12, 7.5)]
    tt = t - start
    on = tt >= 0
    out = np.zeros_like(t)
    for ratio, amp, decay in partials:
        out += amp * np.sin(2 * np.pi * f0 * ratio * tt + ratio) * np.exp(-decay * np.maximum(tt, 0))
    strike = 1 - np.exp(-np.maximum(tt, 0) * 900.0)        # a 1 ms rise instead of a click
    return level * out * on * strike


def comb(x, delay, gain):
    """y[n] = x[n] + gain * y[n - delay], a block of `delay` samples at a time."""
    y = x.copy()
    for i in range(delay, len(y), delay):
        seg = y[i:i + delay]
        seg += gain * y[i - delay:i - delay + len(seg)]
    return y


def make_jingle():
    n = int(RATE * SECONDS)
    t = np.arange(n) / RATE
    root = 73.42                                            # D2
    chans = []
    for side, detune in ((0, -1.0), (1, 1.0)):
        cents = lambda c: 2 ** (c * detune / 1200.0)
        # The drone: D1 + D2 + A2, each a stack of falling harmonics, beating slowly.
        drone = np.zeros(n)
        for f, a in ((root / 2, 0.8), (root * cents(6), 1.0), (root * 1.4983 * cents(-5), 0.45)):
            for h in range(1, 9):
                drone += a / h ** 1.6 * np.sin(2 * np.pi * f * h * t + h * (0.7 + side))
        drone *= env(t, 0.9, 1.9, 0.9) * (1 + 0.12 * np.sin(2 * np.pi * 0.7 * t + side))

        # The choir: a D minor chord of voices with vibrato, swelling to 1.7 s.
        choir = np.zeros(n)
        for i, f in enumerate((146.83, 220.0, 293.66, 349.23, 440.0)):
            for v in (-7, 0, 8):
                vib = 0.004 * np.sin(2 * np.pi * (4.6 + 0.3 * i) * t + i + v + side)
                ph = 2 * np.pi * f * cents(v + 3) * np.cumsum(1 + vib) / RATE
                # A voice-like tone: strong low harmonics, a hump near 800 Hz ("ah").
                for h in range(1, 7):
                    formant = math.exp(-((f * h - 800.0) / 500.0) ** 2) + 0.25 / h
                    choir += formant / (1 + 0.6 * i) * np.sin(h * ph)
        choir *= env(t - 0.25, 1.45, 1.75, 1.0) * (t > 0.25)

        # Two bell strokes, D4 then the A below it.
        bells = bell(t, 0.30 + 0.004 * side, 293.66 * cents(2), 1.0) + bell(t, 1.35 + 0.004 * (1 - side), 220.0 * cents(2), 0.7)

        dry = 0.50 * drone / np.abs(drone).max() + 0.30 * choir / np.abs(choir).max() + 0.55 * bells / np.abs(bells).max()
        # A stone room: four combs of unequal length.
        wet = sum(comb(dry, int(RATE * d * (1 + 0.03 * side)), g)
                  for d, g in ((0.0297, 0.72), (0.0371, 0.70), (0.0411, 0.68), (0.0437, 0.66))) / 4
        chans.append(0.7 * dry + 0.5 * wet)
    out = np.stack(chans, 1)
    out *= env(t, 0.02, SECONDS - 0.5, 0.5)[:, None]        # in without a click, out to silence
    out *= 10 ** (-1.5 / 20) / np.abs(out).max()
    return np.round(out * 32767).astype("<i2")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--font", help="path to LinLibertine_DR.otf")
    args = ap.parse_args()
    os.makedirs(os.path.join(HERE, "preview"), exist_ok=True)

    icon = make_icon()
    icon.save(os.path.join(OUT, "icon.png"), optimize=True)
    icon.resize((192, 192), Image.BOX).save(os.path.join(HERE, "preview", "icon_4x.png"))
    icon.resize((24, 24), Image.LANCZOS).resize((96, 96), Image.BOX).save(os.path.join(HERE, "preview", "icon_24_4x.png"))

    banner = make_banner(find_font(args.font))
    banner.save(os.path.join(OUT, "banner.png"), optimize=True)
    # The preview: the banner at 3x on mid-grey, to see the transparent margin.
    prev = Image.new("RGBA", banner.size, (70, 74, 82, 255))
    prev.alpha_composite(banner)
    prev.resize((768, 384), Image.BOX).convert("RGB").save(os.path.join(HERE, "preview", "banner_3x.png"))

    pcm = make_jingle()
    with wave.open(os.path.join(OUT, "banner.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    print(f"icon.png 48x48, banner.png 256x128, banner.wav {len(pcm) / RATE:.2f} s 16-bit stereo {RATE} Hz")


if __name__ == "__main__":
    main()
