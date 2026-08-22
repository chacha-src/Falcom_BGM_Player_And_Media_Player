# -*- coding: utf-8 -*-
"""Build original CC0 32x32 Windows cursors (hand / cross / grab).

Drawn here so they stay visible on both light and dark acrylic surfaces.
Black outline + white fill, same indigo accent as the window icons.
"""
from __future__ import print_function
import os
import struct
from io import BytesIO

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FILL = (255, 255, 255, 255)
LINE = (20, 18, 28, 255)
ACCENT = (118, 98, 210, 255)
SIZE = 32


def pack_cur(path, im, hx, hy):
    im = im.convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    xor = b""
    mask = b""
    pix = im.load()
    for y in range(SIZE - 1, -1, -1):
        for x in range(SIZE):
            r, g, b, a = pix[x, y]
            xor += struct.pack("BBBB", b, g, r, a)
        row = 0
        bits = []
        for x in range(SIZE):
            a = pix[x, y][3]
            row = (row << 1) | (1 if a < 16 else 0)
            if (x & 7) == 7:
                bits.append(row & 0xFF)
                row = 0
        mask += bytes(bits)
    dib = struct.pack(
        "<IiiHHIIiiII",
        40, SIZE, SIZE * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0,
    ) + xor + mask
    hdr = struct.pack("<HHH", 0, 2, 1)
    ent = struct.pack("<BBBBHHII", SIZE, SIZE, 0, 0, hx, hy, len(dib), 6 + 16)
    with open(path, "wb") as f:
        f.write(hdr + ent + dib)


def outline_draw(draw, xy, fill, width):
    draw.line(xy, fill=LINE, width=width + 2, joint="curve")
    draw.line(xy, fill=fill, width=width, joint="curve")


def make_hand():
    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # index finger (hotspot near tip)
    d.polygon([(6, 1), (10, 1), (10, 14), (6, 14)], fill=LINE)
    d.polygon([(7, 2), (9, 2), (9, 13), (7, 13)], fill=FILL)
    # palm + other fingers
    d.rounded_rectangle((5, 12, 22, 28), radius=4, outline=LINE, width=2, fill=FILL)
    for x0 in (11, 15, 19):
        d.rectangle((x0, 8, x0 + 3, 16), fill=LINE)
        d.rectangle((x0 + 1, 9, x0 + 2, 15), fill=FILL)
    d.point((7, 2), fill=ACCENT)
    return im, 7, 1


def make_cross():
    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = 15
    for w, col in ((5, LINE), (3, FILL)):
        d.line((c, 2, c, 12), fill=col, width=w)
        d.line((c, 18, c, 28), fill=col, width=w)
        d.line((2, c, 12, c), fill=col, width=w)
        d.line((18, c, 28, c), fill=col, width=w)
    d.rectangle((c - 1, c - 1, c + 1, c + 1), fill=ACCENT)
    return im, 15, 15


def make_grab():
    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = 15

    def arrow(pts_line, pts_fill):
        d.polygon(pts_line, fill=LINE)
        d.polygon(pts_fill, fill=FILL)

    # up / down / left / right
    arrow([(c, 1), (c + 5, 9), (c - 5, 9)], [(c, 3), (c + 3, 8), (c - 3, 8)])
    arrow([(c, 29), (c + 5, 21), (c - 5, 21)], [(c, 27), (c + 3, 22), (c - 3, 22)])
    arrow([(1, c), (9, c + 5), (9, c - 5)], [(3, c), (8, c + 3), (8, c - 3)])
    arrow([(29, c), (21, c + 5), (21, c - 5)], [(27, c), (22, c + 3), (22, c - 3)])
    d.rectangle((c - 2, c - 2, c + 2, c + 2), fill=ACCENT)
    return im, 15, 15


def main():
    os.makedirs(HERE, exist_ok=True)
    with open(os.path.join(HERE, "NOTICE.txt"), "w", encoding="utf-8") as f:
        f.write(
            "Cursors in this folder are original artwork for oggYSEDbgm,\n"
            "dedicated to the public domain under CC0 1.0.\n"
            "https://creativecommons.org/publicdomain/zero/1.0/\n"
        )
    specs = (
        ("hand.cur", make_hand),
        ("cross.cur", make_cross),
        ("grab.cur", make_grab),
    )
    for name, fn in specs:
        im, hx, hy = fn()
        dest = os.path.join(HERE, name)
        pack_cur(dest, im, hx, hy)
        print("wrote", dest, os.path.getsize(dest), "bytes hotspot", hx, hy)
    print("done")


if __name__ == "__main__":
    main()
