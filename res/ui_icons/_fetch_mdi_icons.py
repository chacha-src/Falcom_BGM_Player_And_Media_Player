# -*- coding: utf-8 -*-
"""Fetch Apache-2.0 Material Design Icons (Pictogrammers @mdi/svg) and pack ICOs.

Source: https://github.com/Templarian/MaterialDesign  (Apache License 2.0)
Glyphs are recolored indigo so they stay visible on dark and light captions.
"""
from __future__ import print_function
import os
import struct
import urllib.request
from io import BytesIO

import subprocess
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
UA = {"User-Agent": "oggYSEDbgm-icon-fetch/1.0"}
SVG_URL = "https://cdn.jsdelivr.net/npm/@mdi/svg@7.4.47/svg/%s.svg"
FILL = "#7662D2"
SIZES = (16, 20, 24, 32, 48)

# out-name -> mdi svg filename (without .svg)
ICONS = {
    "analyzer": "waveform",
    "piano": "piano",
    "eq": "equalizer",
    "render": "volume-high",
    "export": "content-save",
    "tag": "tag",
    "prompt": "auto-fix",
    "mic": "microphone",
    "capture": "monitor-screenshot",
    "folder": "folder",
    "info": "information",
    "video": "movie-open",
    "keyboard": "keyboard",
    "meter": "speedometer",
    "disc": "album",
    "photo": "image",
    "maze": "gamepad-variant",
    "race": "car",
    "vst": "puzzle",
    "help": "help-circle",
    "alarm": "alarm",
    "remote": "remote",
    "viz": "chart-bubble",
    "music": "music",
    "sync": "sync",
    "audio": "headphones",
    "file": "file-document",
    "apps": "apps",
    "copy": "content-copy",
    "share": "monitor",
    "tune": "tune",
}


def http_get(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def recolor_svg(svg):
    text = svg.decode("utf-8")
    if " fill=" not in text.split(">", 1)[0]:
        text = text.replace("<svg", '<svg fill="%s"' % FILL, 1)
    else:
        text = text.replace("currentColor", FILL)
    return text.encode("utf-8")


def pack_ico(path, images):
    pngs = []
    for im in images:
        buf = BytesIO()
        im.save(buf, format="PNG")
        pngs.append(buf.getvalue())
    count = len(pngs)
    offset = 6 + 16 * count
    chunks = [struct.pack("<HHH", 0, 1, count)]
    body = b""
    for im, data in zip(images, pngs):
        w = im.size[0] if im.size[0] < 256 else 0
        h = im.size[1] if im.size[1] < 256 else 0
        chunks.append(struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset))
        body += data
        offset += len(data)
    with open(path, "wb") as f:
        f.write(b"".join(chunks) + body)


def main():
    os.makedirs(HERE, exist_ok=True)
    notice = os.path.join(HERE, "NOTICE.txt")
    with open(notice, "w", encoding="utf-8") as f:
        f.write(
            "Window icons in this folder are derived from Material Design Icons\n"
            "by Pictogrammers (https://pictogrammers.com/library/mdi/),\n"
            "licensed under the Apache License, Version 2.0.\n"
            "https://www.apache.org/licenses/LICENSE-2.0\n"
            "Upstream: https://github.com/Templarian/MaterialDesign\n"
            "Glyphs were recolored for caption contrast; no other modification.\n"
        )
    for out_name, svg_name in ICONS.items():
        print("fetch", out_name, svg_name)
        raw = recolor_svg(http_get(SVG_URL % svg_name))
        magick = os.environ.get("MAGICK", r"C:\Program Files\ImageMagick-7.1.1-Q16-HDRI\magick.EXE")
        proc = subprocess.run(
            [magick, "-background", "none", "svg:-", "-resize", "48x48", "png:-"],
            input=raw, capture_output=True, check=True)
        src = Image.open(BytesIO(proc.stdout)).convert("RGBA")
        frames = []
        for s in SIZES:
            frames.append(src.resize((s, s), Image.Resampling.LANCZOS))
        dest = os.path.join(HERE, out_name + ".ico")
        pack_ico(dest, frames)
        print("  wrote", dest, os.path.getsize(dest), "bytes")
    print("done")


if __name__ == "__main__":
    main()
