#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math

OUT = Path(__file__).resolve().parents[1] / "assets"
OUT.mkdir(exist_ok=True)

def scene(size=(640, 400)):
    w, h = size
    im = Image.new("RGB", size, (2, 4, 12))
    d = ImageDraw.Draw(im)
    horizon = int(h * .71)
    for i in range(70):
        x, y = (i * 137 + 17) % w, (i * 71 + 23) % (horizon - 12)
        c = (120, 220, 245) if i % 9 == 0 else (30, 70, 105)
        d.point((x, y), fill=c)
    d.rectangle((0, horizon, w, h), fill=(5, 8, 18))
    for yy in range(horizon + 6, h, 12): d.line((0, yy, w, yy), fill=(25, 42, 72), width=2)
    for bx in range(-120, w + 121, 60): d.line((w//2, horizon, bx, h), fill=(18, 34, 62), width=2)
    cx, cy, r = w//2, int(h*.42), int(h*.25)
    d.ellipse((cx-r, horizon+14-r//7, cx+r, horizon+14+r//7), fill=(1, 3, 8))
    pix = im.load()
    glyph = (
        "1110110011101110111", "1010101010000010001",
        "1110110010101110111", "1000101010100010100",
        "1000101011101110111")
    for py in range(-r, r+1):
        for px in range(-r, r+1):
            q = r*r-px*px-py*py
            if q < 0: continue
            z = math.sqrt(q)
            u = 52 + int(px*96/(z+r+1)); v = py+24
            wrapped = (u + 64) % 128 - 64
            gx, gy = (wrapped + 38) // 4, (v + 10) // 4
            logo = 0 <= gx < 19 and 0 <= gy < 5 and glyph[gy][gx] == "1"
            checker = ((u//16) ^ (v//16)) & 1
            base = (238,244,255) if logo else ((244,52,108) if checker else (26,210,244))
            light = max(58,min(255,int(120+(-px-py+2*z)*90/(r*4))))
            pix[cx+px,cy+py] = tuple(c*light//256 for c in base)
    font = ImageFont.load_default()
    d.text((12, 10), "POING / REAL-TIME PRG32", font=font, fill=(210,250,255))
    return im

shot = scene()
shot.save(OUT / "screenshot.png", optimize=True)
icon = scene((400, 400)).resize((256, 256), Image.Resampling.LANCZOS)
icon.save(OUT / "icon.png", optimize=True)
print("wrote", OUT / "icon.png", "and", OUT / "screenshot.png")
