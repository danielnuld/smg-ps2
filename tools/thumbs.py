# YouTube thumbnails for the Good Egg videos: the demo (1280 x 720) and the short (1080 x 1920), English text.
# usage: python tools/thumbs.py <demo frame png> <comparison frame png (1444 x 456: Wii 836 | PS2 608)> <out dir>
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

IMPACT = "C:/Windows/Fonts/impact.ttf"
BLACK = "C:/Windows/Fonts/ariblk.ttf"


def text(d, xy, s, size, fill, stroke=8, font=IMPACT, anchor="la", stroke_fill=(0, 0, 0)):
    f = ImageFont.truetype(font, size)
    d.text(xy, s, font=f, fill=fill, stroke_width=stroke, stroke_fill=stroke_fill, anchor=anchor)


def badge(d, cx, cy, s, size, bg, fg):
    f = ImageFont.truetype(BLACK, size)
    l, t, r, b = d.textbbox((cx, cy), s, font=f, anchor="mm")
    pad = size // 4
    d.rounded_rectangle((l - pad, t - pad, r + pad, b + pad), radius=pad, fill=bg, outline=(0, 0, 0), width=5)
    d.text((cx, cy), s, font=f, fill=fg, anchor="mm")


def demo(frame, out):
    src = Image.open(frame).convert("RGB")
    src = src.crop((0, 0, src.width, src.height - 60))  # the section label off the bottom
    k = 1280 / src.width
    src = src.resize((1280, round(src.height * k)), Image.LANCZOS)
    top = (src.height - 720) // 2
    im = src.crop((0, top, 1280, top + 720))
    shade = Image.new("L", (1280, 720))  # dark on the left, under the title
    sd = ImageDraw.Draw(shade)
    for x in range(1280):
        sd.line((x, 0, x, 720), fill=int(max(0, 200 * (1 - x / 760))))
    im = Image.composite(Image.new("RGB", im.size), im, shade)
    d = ImageDraw.Draw(im)
    text(d, (48, 60), "SUPER MARIO", 120, (255, 255, 255))
    text(d, (48, 180), "GALAXY", 150, (255, 255, 255))
    text(d, (48, 360), "ON PS2", 190, (255, 214, 0), stroke=12, stroke_fill=(150, 0, 0))
    badge(d, 230, 640, "FAN PORT", 54, (230, 30, 40), (255, 255, 255))
    im.save(out)


def short(frame, out):
    src = Image.open(frame).convert("RGB")
    wii, ps2 = src.crop((0, 0, 836, 456)), src.crop((836, 0, 1444, 456))
    im = Image.new("RGB", (1080, 1920), (12, 14, 40))
    wii = wii.resize((1080, round(456 * 1080 / 836)), Image.LANCZOS)
    ps2 = ps2.resize((1080, 810), Image.LANCZOS)
    im.paste(wii, (0, 330))
    im.paste(ps2, (0, 330 + wii.height + 24))
    d = ImageDraw.Draw(im)
    text(d, (540, 40), "MARIO GALAXY", 150, (255, 255, 255), anchor="ma")
    text(d, (540, 190), "ON A PS2?!", 130, (255, 214, 0), stroke=10, stroke_fill=(150, 0, 0), anchor="ma")
    badge(d, 110, 330 + 70, "WII", 64, (255, 255, 255), (0, 0, 0))
    badge(d, 110, 330 + wii.height + 24 + 70, "PS2", 64, (40, 90, 230), (255, 255, 255))
    y = 330 + wii.height + 24 + 810
    text(d, (540, y + 40), "SAME INPUTS", 110, (255, 255, 255), anchor="ma")
    im.save(out)


if __name__ == "__main__":
    demo(sys.argv[1], sys.argv[3] + "/thumb_demo.png")
    short(sys.argv[2], sys.argv[3] + "/thumb_short.png")
