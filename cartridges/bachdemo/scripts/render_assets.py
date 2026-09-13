#!/usr/bin/env python3
"""Render deterministic catalog artwork without external SVG delegates."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
image = Image.new("RGB", (256, 256), "#07111f")
draw = ImageDraw.Draw(image)

cyan = "#20d8d0"
magenta = "#d943ef"
yellow = "#ffe45c"
white = "#ffffff"

for x in range(20, 237, 2):
    phase = (x - 20) % 72
    triangle = phase if phase < 36 else 72 - phase
    y1 = 150 - triangle
    y2 = 126 - triangle
    if x > 20:
        draw.line((last_x, last_y1, x, y1), fill=cyan, width=10)
        draw.line((last_x, last_y2, x, y2), fill=magenta, width=6)
    last_x, last_y1, last_y2 = x, y1, y2

draw.ellipse((33, 55, 57, 79), fill=yellow)
draw.rectangle((53, 27, 65, 87), fill=yellow)
draw.rectangle((53, 27, 91, 38), fill=yellow)

font = ImageFont.load_default(size=28)
label = "BWV 846"
box = draw.textbbox((0, 0), label, font=font)
draw.text(((256 - (box[2] - box[0])) // 2, 211), label, fill=white, font=font)
image.save(ROOT / "assets/icon.png", optimize=True)

shot = Image.new("RGB", (320, 200), "#020812")
canvas = ImageDraw.Draw(shot)
canvas.text((8, 8), "BACH / BWV 846", fill=white, font=ImageFont.load_default(size=14))
canvas.text((8, 28), "8-VOICE STEREO PRELUDE", fill=cyan)
canvas.text((8, 47), "AUDIO PLUS: STEREO", fill="#62e879")
canvas.text((8, 67), "VOLUME 192          WIDTH 100", fill=white)
colors = ("#2887ff",) * 4 + (magenta,) * 3 + (yellow,)
heights = (42, 57, 34, 69, 51, 62, 39, 73)
for channel, (color, height) in enumerate(zip(colors, heights)):
    x = 18 + channel * 37
    canvas.rectangle((x, 164 - height, x + 23, 163), fill=color)
    canvas.rectangle((x + 4, 168 - height, x + 19, 171 - height), fill=white)
    canvas.text((x + 8, 172), str(channel + 1), fill=white)
canvas.text((8, 188), "A RESTART  B RELEASE  <> WIDTH", fill=white)
shot.save(ROOT / "assets/screenshot.png", optimize=True)
