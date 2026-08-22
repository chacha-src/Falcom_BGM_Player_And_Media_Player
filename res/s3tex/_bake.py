# Bake Soft3D race/maze 256x256 PNGs from Poly Haven CC0 1K diffuse JPGs.
# Runtime never reads these sources; only the PNGs compiled into ogg.rc.
# License: Creative Commons CC0 (https://polyhaven.com/license)
import math
import os
import urllib.request
from PIL import Image, ImageEnhance

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.environ.get("TEMP", ROOT), "s3tex_src")
OUT = ROOT
SIZE = 256

# id -> 1K diffuse JPG
ASSETS = {
    "forest_leaves_03": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/forest_leaves_03/forest_leaves_03_diff_1k.jpg",
    "rabdentse_ruins_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rabdentse_ruins_wall/rabdentse_ruins_wall_diff_1k.jpg",
    "factory_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/factory_wall/factory_wall_diff_1k.jpg",
    "dark_brick_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/dark_brick_wall/dark_brick_wall_diff_1k.jpg",
    "coral_ground_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/coral_ground_02/coral_ground_02_diff_1k.jpg",
    "leafy_grass": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/leafy_grass/leafy_grass_diff_1k.jpg",
    "cracked_red_ground": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/cracked_red_ground/cracked_red_ground_diff_1k.jpg",
    "snow_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/snow_02/snow_02_diff_1k.jpg",
    "metal_plate_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/metal_plate_02/metal_plate_02_diff_1k.jpg",
    "blue_metal_plate": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/blue_metal_plate/blue_metal_plate_diff_1k.jpg",
    "brick_moss_001": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/brick_moss_001/brick_moss_001_diff_1k.jpg",
    "mossy_brick": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/mossy_brick/mossy_brick_diff_1k.jpg",
    "castle_brick_07": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/castle_brick_07/castle_brick_07_diff_1k.jpg",
    "mossy_stone_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/mossy_stone_wall/mossy_stone_wall_diff_1k.jpg",
    "rustic_stone_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rustic_stone_wall/rustic_stone_wall_diff_1k.jpg",
    "rusty_metal_sheet": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rusty_metal_sheet/rusty_metal_sheet_diff_1k.jpg",
    "rusty_corrugated_iron": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rusty_corrugated_iron/rusty_corrugated_iron_diff_1k.jpg",
    "rusty_metal": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rusty_metal/rusty_metal_diff_1k.jpg",
    "volcanic_rock_tiles": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/volcanic_rock_tiles/volcanic_rock_tiles_diff_1k.jpg",
    "burned_ground_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/burned_ground_01/burned_ground_01_diff_1k.jpg",
    "brown_mud_leaves_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/brown_mud_leaves_01/brown_mud_leaves_01_diff_1k.jpg",
    "mossy_cobblestone": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/mossy_cobblestone/mossy_cobblestone_diff_1k.jpg",
    "metal_plate": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/metal_plate/metal_plate_diff_1k.jpg",
    "red_sandstone_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/red_sandstone_wall/red_sandstone_wall_diff_1k.jpg",
}


def fetch(name, url):
    os.makedirs(SRC, exist_ok=True)
    path = os.path.join(SRC, name + ".jpg")
    if os.path.isfile(path) and os.path.getsize(path) > 20000:
        return path
    print("GET", name, flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    last = None
    for _ in range(3):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            if len(data) < 10000:
                raise RuntimeError("too small %d" % len(data))
            with open(path, "wb") as f:
                f.write(data)
            return path
        except Exception as e:
            last = e
    raise last


def load256(name):
    im = Image.open(fetch(name, ASSETS[name])).convert("RGBA")
    return im.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def save_png(im, name):
    path = os.path.join(OUT, name)
    im = im.convert("RGBA")
    im.save(path, "PNG", optimize=True, compress_level=9)
    print("WROTE", name, os.path.getsize(path), flush=True)


def grade(im, rf, gf, bf, radd=0, gadd=0, badd=0, sat=1.0, contrast=1.0):
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            r = max(0, min(255, int(r * rf + radd)))
            g = max(0, min(255, int(g * gf + gadd)))
            b = max(0, min(255, int(b * bf + badd)))
            px[x, y] = (r, g, b, a)
    if abs(sat - 1.0) > 0.01:
        rgb = im.convert("RGB")
        rgb = ImageEnhance.Color(rgb).enhance(sat)
        a = im.split()[3]
        im = Image.merge("RGBA", (*rgb.split(), a))
    if abs(contrast - 1.0) > 0.01:
        rgb = im.convert("RGB")
        rgb = ImageEnhance.Contrast(rgb).enhance(contrast)
        a = im.split()[3]
        im = Image.merge("RGBA", (*rgb.split(), a))
    return im


def height_alpha(im, base=88, scale=0.55):
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, _ = px[x, y]
            lum = int(0.3 * r + 0.59 * g + 0.11 * b)
            a = base + int(lum * scale)
            if a < 40:
                a = 40
            if a > 255:
                a = 255
            px[x, y] = (r, g, b, a)
    return im


def wrap_tile(im, ox, oy, tw, th):
    w, h = im.size
    src = im.load()
    out = Image.new("RGBA", (tw, th))
    dst = out.load()
    for y in range(th):
        sy = (y + oy) % h
        for x in range(tw):
            dst[x, y] = src[(x + ox) % w, sy]
    return out


def blend(a, b, t):
    return Image.blend(a.convert("RGBA"), b.convert("RGBA"), t)


def atlas4(sources_and_offs):
    """16 tiles of 64x64 from (image, ox, oy) list."""
    tw = SIZE // 4
    out = Image.new("RGBA", (SIZE, SIZE))
    for vid, (im, ox, oy) in enumerate(sources_and_offs):
        tx, ty = vid & 3, vid >> 2
        tile = wrap_tile(im, ox, oy, tw, tw)
        out.paste(tile, (tx * tw, ty * tw))
    return out


def lava_cracks(im):
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            lum = int(0.3 * r + 0.59 * g + 0.11 * b)
            if lum < 70:
                t = (70 - lum) / 70.0
                r = min(255, int(r + 160 * t))
                g = min(255, int(g + 40 * t))
                b = min(255, int(b * (1.0 - 0.4 * t)))
            px[x, y] = (r, g, b, a)
    return im


def band_from_metal(im):
    px = im.load()
    w, h = im.size
    for y in range(h):
        v = y / (h - 1)
        edge = (1.0 - abs(v - 0.5) * 2.0)
        edge = max(0.0, edge) ** 1.55
        for x in range(w):
            r, g, b, _ = px[x, y]
            stripe = 0.55 + 0.45 * (0.5 + 0.5 * math.sin(x * 0.196))
            m = 0.55 + 0.45 * edge * stripe
            r = min(255, int(r * 0.45 + 90 * m))
            g = min(255, int(g * 0.55 + 150 * m))
            b = min(255, int(b * 0.75 + 180 * m))
            a = int(90 + 150 * edge)
            px[x, y] = (r, g, b, a)
    return im


def night_windows(im):
    # keep photo bricks, crush to night, spark original highlights as lamps
    src = im.copy()
    im = grade(im, 0.32, 0.38, 0.72, radd=12, gadd=18, badd=58, sat=0.85, contrast=1.15)
    sp = src.load()
    dp = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r0, g0, b0, _ = sp[x, y]
            lum = int(0.3 * r0 + 0.59 * g0 + 0.11 * b0)
            if lum > 150:
                r, g, b, a = dp[x, y]
                t = (lum - 150) / 105.0
                r = min(255, int(r + 140 * t))
                g = min(255, int(g + 110 * t))
                b = min(255, int(b + 40 * t))
                dp[x, y] = (r, g, b, a)
    return im


def main():
    forest = load256("forest_leaves_03")
    ruins = load256("rabdentse_ruins_wall")
    oil = load256("factory_wall")
    night = night_windows(load256("dark_brick_wall"))
    under = grade(load256("coral_ground_02"), 0.62, 0.92, 1.12, gadd=12, badd=28, sat=1.1)
    grass = load256("leafy_grass")
    mesa = load256("cracked_red_ground")
    cloud = grade(load256("snow_02"), 0.92, 0.94, 1.02, radd=18, gadd=12, badd=22, sat=0.7)
    craft = load256("metal_plate_02")
    band = band_from_metal(load256("blue_metal_plate"))
    brick = load256("brick_moss_001")
    mossb = load256("mossy_brick")
    castle = load256("castle_brick_07")
    mstone = load256("mossy_stone_wall")
    rustic = load256("rustic_stone_wall")
    rsheet = load256("rusty_metal_sheet")
    rcorr = load256("rusty_corrugated_iron")
    rmet = load256("rusty_metal")
    volc = lava_cracks(load256("volcanic_rock_tiles"))
    burn = lava_cracks(load256("burned_ground_01"))
    mud = load256("brown_mud_leaves_01")
    cobble = load256("mossy_cobblestone")
    metal = load256("metal_plate")
    blue = load256("blue_metal_plate")
    reds = load256("red_sandstone_wall")

    save_png(forest, "r_forest.png")
    save_png(ruins, "r_ruins.png")
    save_png(grade(oil, 0.95, 0.95, 1.02, contrast=1.12), "r_oil.png")
    save_png(night, "r_night.png")
    save_png(under, "r_under.png")
    save_png(grass, "r_grass.png")
    save_png(blend(mesa, reds, 0.35), "r_mesa.png")
    save_png(cloud, "r_cloud.png")
    save_png(craft, "r_craft.png")
    save_png(band, "r_band.png")

    # maze wall atlases 4x4 (64px tiles) from real photos + wrap offsets
    veg = blend(mossb, grass, 0.45)
    dense = grade(grass, 0.85, 1.05, 0.8, contrast=1.1)
    w0 = []
    for vid in range(16):
        ox, oy = (vid * 37) & 191, (vid * 19) & 191
        if vid <= 3:
            src = brick
        elif vid <= 6:
            src = veg
        elif vid <= 9:
            src = mossb
        elif vid <= 12:
            src = brick
            ox += 64
        else:
            src = dense
        w0.append((src, ox, oy))
    save_png(height_alpha(atlas4(w0), 92, 0.5), "m_wall0.png")

    w1 = []
    for vid in range(16):
        ox, oy = (vid * 41) & 191, (vid * 23) & 191
        src = rustic if vid < 6 else (mstone if vid < 12 else castle)
        w1.append((src, ox, oy))
    save_png(height_alpha(atlas4(w1), 100, 0.48), "m_wall1.png")

    w2 = []
    for vid in range(16):
        ox, oy = (vid * 29) & 191, (vid * 31) & 191
        src = rsheet if vid < 6 else (rcorr if vid < 11 else rmet)
        w2.append((src, ox, oy))
    save_png(height_alpha(atlas4(w2), 110, 0.4), "m_wall2.png")

    w3 = []
    for vid in range(16):
        ox, oy = (vid * 43) & 191, (vid * 17) & 191
        src = volc if (vid & 1) == 0 else burn
        w3.append((src, ox, oy))
    save_png(height_alpha(atlas4(w3), 105, 0.5), "m_wall3.png")

    save_png(mud, "m_floor0.png")
    save_png(cobble, "m_floor1.png")
    save_png(metal, "m_floor2.png")
    save_png(burn, "m_floor3.png")

    mirw = grade(blue, 0.85, 0.95, 1.15, radd=20, gadd=28, badd=40, sat=0.7, contrast=1.2)
    mirf = grade(blue, 0.9, 1.0, 1.18, radd=28, gadd=32, badd=48, sat=0.65, contrast=1.15)
    save_png(height_alpha(mirw, 160, 0.28), "m_mirwall.png")
    save_png(mirf, "m_mirfloor.png")

    print("done", flush=True)


if __name__ == "__main__":
    main()
