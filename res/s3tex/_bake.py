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
    "forrest_ground_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/forrest_ground_01/forrest_ground_01_diff_1k.jpg",
    "large_sandstone_blocks": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/large_sandstone_blocks/large_sandstone_blocks_diff_1k.jpg",
    "rusty_metal_03": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rusty_metal_03/rusty_metal_03_diff_1k.jpg",
    "asphalt_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/asphalt_02/asphalt_02_diff_1k.jpg",
    "coast_sand_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/coast_sand_01/coast_sand_01_diff_1k.jpg",
    "aerial_grass_rock": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/aerial_grass_rock/aerial_grass_rock_diff_1k.jpg",
    "rock_face": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rock_face/rock_face_diff_1k.jpg",
    "snow_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/snow_01/snow_01_diff_1k.jpg",
    "painted_plaster_wall": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/painted_plaster_wall/painted_plaster_wall_diff_1k.jpg",
    "bark_willow": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/bark_willow/bark_willow_diff_1k.jpg",
    "moss_wood": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/moss_wood/moss_wood_diff_1k.jpg",
    "rock_wall_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/rock_wall_02/rock_wall_02_diff_1k.jpg",
    "cobblestone_floor_04": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/cobblestone_floor_04/cobblestone_floor_04_diff_1k.jpg",
    "pebbles": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/pebbles/pebbles_diff_1k.jpg",
    "concrete_wall_008": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/concrete_wall_008/concrete_wall_008_diff_1k.jpg",
    "aerial_rocks_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/aerial_rocks_02/aerial_rocks_02_diff_1k.jpg",
    "dry_ground_01": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/dry_ground_01/dry_ground_01_diff_1k.jpg",
    "brick_wall_02": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/brick_wall_02/brick_wall_02_diff_1k.jpg",
    "wood_table_001": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/wood_table_001/wood_table_001_diff_1k.jpg",
    "red_brick": "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/red_brick/red_brick_diff_1k.jpg",
}

HDRI = {
    "kloofendal": "https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/kloofendal_43d_clear_puresky.jpg",
    "syferfontein": "https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/syferfontein_1d_clear_puresky.jpg",
    "autoshop": "https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/autoshop_01.jpg",
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


def fetch_hdri(name, url):
    os.makedirs(SRC, exist_ok=True)
    path = os.path.join(SRC, name + ".jpg")
    if os.path.isfile(path) and os.path.getsize(path) > 200000:
        return path
    print("GET HDRI", name, flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    last = None
    for _ in range(3):
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                data = r.read()
            if len(data) < 100000:
                raise RuntimeError("too small %d" % len(data))
            with open(path, "wb") as f:
                f.write(data)
            return path
        except Exception as e:
            last = e
    raise last


def equirect_to_cube_strip(im, size=256):
    """D3D cubemap faces +X -X +Y -Y +Z -Z stacked vertically (size x size*6)."""
    im = im.convert("RGB")
    ew, eh = im.size
    src = im.load()
    strip = Image.new("RGBA", (size, size * 6))
    dst = strip.load()
    faces = (
        lambda u, v: (1.0, -v, -u),
        lambda u, v: (-1.0, -v, u),
        lambda u, v: (u, 1.0, v),
        lambda u, v: (u, -1.0, -v),
        lambda u, v: (u, -v, 1.0),
        lambda u, v: (-u, -v, -1.0),
    )
    inv_pi = 1.0 / math.pi
    for f, dirfn in enumerate(faces):
        yo = f * size
        for y in range(size):
            v = 1.0 - 2.0 * (y + 0.5) / size
            for x in range(size):
                u = 2.0 * (x + 0.5) / size - 1.0
                dx, dy, dz = dirfn(u, v)
                inv = 1.0 / math.sqrt(dx * dx + dy * dy + dz * dz)
                dx *= inv
                dy *= inv
                dz *= inv
                lon = math.atan2(dz, dx)
                lat = math.asin(max(-1.0, min(1.0, dy)))
                fx = (lon * 0.5 * inv_pi + 0.5) * ew
                fy = (0.5 - lat * inv_pi) * eh
                ix = int(fx) % ew
                iy = int(fy)
                if iy < 0:
                    iy = 0
                if iy >= eh:
                    iy = eh - 1
                r, g, b = src[ix, iy]
                dst[x, yo + y] = (r, g, b, 255)
    return strip


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


def lum_alpha(im, lo=70, hi=210, invert=False):
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, _ = px[x, y]
            lum = int(0.3 * r + 0.59 * g + 0.11 * b)
            a = int(255 * max(0.0, min(1.0, (lum - lo) / float(max(1, hi - lo)))))
            if invert:
                a = 255 - a
            if a < 12:
                a = 12
            px[x, y] = (r, g, b, a)
    return im


def _n2(x, y):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    u = fx * fx * (3.0 - 2.0 * fx)
    v = fy * fy * (3.0 - 2.0 * fy)

    def h(i, j):
        n = math.sin(i * 127.1 + j * 311.7) * 43758.5453
        return n - math.floor(n)

    n00, n10 = h(ix, iy), h(ix + 1, iy)
    n01, n11 = h(ix, iy + 1), h(ix + 1, iy + 1)
    return n00 + u * (n10 - n00) + v * (n01 - n00) + u * v * (n00 - n10 - n01 + n11)


def _fbm(x, y, octaves=5):
    a, f, s = 0.5, 0.0, 0.0
    for _ in range(octaves):
        s += a
        f += a * _n2(x, y)
        x *= 2.03
        y *= 2.03
        a *= 0.5
    return f / s if s else 0.0


def cloud_sprite(seed=1, cirrus=False):
    """Fluffy / cirrus billboard with true transparent empty space (not an HDRI crop)."""
    im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = im.load()
    lobes = []
    rng = (seed * 1103515245 + 12345) & 0x7FFFFFFF

    def rnd():
        nonlocal rng
        rng = (rng * 1103515245 + 12345) & 0x7FFFFFFF
        return rng / 2147483647.0

    nL = 5 if cirrus else 6
    for _ in range(nL):
        cx = 0.22 + rnd() * 0.56
        cy = 0.34 + rnd() * 0.32
        rx = (0.16 + rnd() * 0.22) * (1.35 if cirrus else 1.0)
        ry = (0.11 + rnd() * 0.14) * (0.72 if cirrus else 1.0)
        lobes.append((cx, cy, rx, ry))
    for y in range(SIZE):
        v = y / (SIZE - 1.0)
        for x in range(SIZE):
            u = x / (SIZE - 1.0)
            cover = 0.0
            for cx, cy, rx, ry in lobes:
                dx = (u - cx) / rx
                dy = (v - cy) / ry
                d = 1.0 - (dx * dx + dy * dy)
                if d > cover:
                    cover = d
            if cover <= 0.0:
                continue
            warp = _fbm(u * 4.4 + seed * 0.17, v * 4.8 + seed * 0.09)
            w2 = _fbm(u * 9.5 + 8.0, v * 8.2 + seed)
            dens = max(0.0, cover) ** 0.82
            dens *= 0.32 + 0.82 * warp
            dens *= 0.50 + 0.72 * (1.0 - abs(w2 * 2.0 - 1.0))
            edge = max(0.0, 1.0 - abs(u - 0.5) * 1.85) * max(0.0, 1.0 - abs(v - 0.5) * 2.05)
            dens *= edge ** 0.55
            a = dens - 0.07
            if a <= 0.0:
                continue
            a = 1.0 - pow(1.0 - min(1.0, a * 2.2), 1.25)
            ai = int(255.0 * a)
            if ai < 8:
                continue
            shade = 0.78 + 0.22 * warp + 0.10 * (1.0 - v)
            if cirrus:
                r = min(255, int(230 * shade + 16))
                g = min(255, int(238 * shade + 10))
                b = min(255, int(248 * shade + 6))
            else:
                r = min(255, int(236 * shade))
                g = min(255, int(242 * shade))
                b = min(255, int(250 * shade))
            px[x, y] = (r, g, b, ai)
    return im


def cloud_from_hdri(im, y0=0.02, y1=0.42, ox=0):
    # Kept for reference; race sky cards use cloud_sprite() (true alpha, cloud-shaped).
    im = im.convert("RGB")
    w, h = im.size
    y0i, y1i = int(h * y0), int(h * y1)
    crop = im.crop((0, y0i, w, max(y0i + 8, y1i)))
    if ox:
        crop = wrap_tile(crop.convert("RGBA"), ox, 0, crop.size[0], crop.size[1]).convert("RGB")
    crop = crop.resize((SIZE, SIZE), Image.Resampling.LANCZOS).convert("RGBA")
    crop = grade(crop, 1.05, 1.04, 1.08, radd=8, gadd=8, badd=14, sat=0.85, contrast=1.12)
    return lum_alpha(crop, 90, 200)


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
    save_png(grade(height_alpha(atlas4(w1), 100, 0.48), 1.18, 1.22, 1.30, radd=18, gadd=22, badd=32, contrast=1.05), "m_wall1.png")

    w2 = []
    for vid in range(16):
        ox, oy = (vid * 29) & 191, (vid * 31) & 191
        src = rsheet if vid < 6 else (rcorr if vid < 11 else rmet)
        w2.append((src, ox, oy))
    save_png(grade(height_alpha(atlas4(w2), 110, 0.4), 1.14, 1.12, 1.10, radd=20, gadd=14, badd=8, contrast=1.06), "m_wall2.png")

    w3 = []
    for vid in range(16):
        ox, oy = (vid * 43) & 191, (vid * 17) & 191
        src = volc if (vid & 1) == 0 else burn
        w3.append((src, ox, oy))
    save_png(grade(height_alpha(atlas4(w3), 105, 0.5), 1.12, 1.08, 1.06, radd=22, gadd=8, badd=4, contrast=1.05), "m_wall3.png")

    save_png(mud, "m_floor0.png")
    save_png(grade(cobble, 1.16, 1.20, 1.28, radd=14, gadd=18, badd=28, contrast=1.06), "m_floor1.png")
    save_png(grade(metal, 1.12, 1.12, 1.14, radd=12, gadd=10, badd=8, contrast=1.08), "m_floor2.png")
    save_png(grade(burn, 1.10, 1.06, 1.04, radd=18, gadd=6, badd=2, contrast=1.05), "m_floor3.png")

    mirw = grade(blue, 0.85, 0.95, 1.15, radd=20, gadd=28, badd=40, sat=0.7, contrast=1.2)
    mirf = grade(blue, 0.9, 1.0, 1.18, radd=28, gadd=32, badd=48, sat=0.65, contrast=1.15)
    save_png(height_alpha(mirw, 160, 0.28), "m_mirwall.png")
    save_png(mirf, "m_mirfloor.png")

    # --- second set: high-frequency detail / water / obstacles / env cube ---
    forest_d = load256("forrest_ground_01")
    ruins_d = load256("large_sandstone_blocks")
    oil_d = grade(load256("rusty_metal_03"), 0.92, 0.9, 0.88, contrast=1.18)
    night_d = grade(load256("asphalt_02"), 0.55, 0.58, 0.82, badd=28, sat=0.75, contrast=1.12)
    under_d = grade(load256("coast_sand_01"), 0.55, 0.95, 1.18, gadd=18, badd=36, sat=1.15)
    grass_d = load256("aerial_grass_rock")
    mesa_d = load256("rock_face")
    cloud_d = grade(load256("snow_01"), 0.94, 0.96, 1.04, radd=12, gadd=8, badd=18, sat=0.65)
    craft_d = grade(load256("painted_plaster_wall"), 0.88, 0.92, 1.05, contrast=1.2)
    water = grade(blend(load256("coast_sand_01"), load256("snow_01"), 0.45),
                  0.35, 0.85, 1.22, gadd=22, badd=48, sat=1.2, contrast=1.1)
    obs = blend(load256("bark_willow"), load256("rock_wall_02"), 0.4)
    mossw = load256("moss_wood")
    cob4 = load256("cobblestone_floor_04")
    pebb = load256("pebbles")
    conc = load256("concrete_wall_008")
    arock = lava_cracks(load256("aerial_rocks_02"))
    dryg = lava_cracks(load256("dry_ground_01"))
    brk2 = load256("brick_wall_02")

    save_png(forest_d, "r_forest_d.png")
    save_png(ruins_d, "r_ruins_d.png")
    save_png(oil_d, "r_oil_d.png")
    save_png(night_d, "r_night_d.png")
    save_png(under_d, "r_under_d.png")
    save_png(grass_d, "r_grass_d.png")
    save_png(mesa_d, "r_mesa_d.png")
    save_png(cloud_d, "r_cloud_d.png")
    save_png(craft_d, "r_craft_d.png")
    save_png(water, "r_water.png")
    save_png(obs, "r_obs.png")

    w0d = []
    for vid in range(16):
        ox, oy = (vid * 53) & 191, (vid * 27) & 191
        src = mossw if (vid & 1) else brk2
        w0d.append((src, ox, oy))
    save_png(height_alpha(atlas4(w0d), 92, 0.48), "m_wall0_d.png")
    w1d = []
    for vid in range(16):
        ox, oy = (vid * 47) & 191, (vid * 33) & 191
        src = cob4 if vid < 8 else load256("rock_wall_02")
        w1d.append((src, ox, oy))
    save_png(height_alpha(atlas4(w1d), 100, 0.45), "m_wall1_d.png")
    w2d = []
    rust3 = load256("rusty_metal_03")
    for vid in range(16):
        ox, oy = (vid * 31) & 191, (vid * 39) & 191
        src = rust3 if vid < 8 else conc
        w2d.append((src, ox, oy))
    save_png(height_alpha(atlas4(w2d), 110, 0.38), "m_wall2_d.png")
    w3d = []
    for vid in range(16):
        ox, oy = (vid * 59) & 191, (vid * 21) & 191
        src = arock if (vid & 1) == 0 else dryg
        w3d.append((src, ox, oy))
    save_png(height_alpha(atlas4(w3d), 105, 0.5), "m_wall3_d.png")
    save_png(pebb, "m_floor0_d.png")
    save_png(cob4, "m_floor1_d.png")
    save_png(conc, "m_floor2_d.png")
    save_png(arock, "m_floor3_d.png")

    hdri = Image.open(fetch_hdri("kloofendal", HDRI["kloofendal"]))
    # downsample equirect first so cube sample stays sharp and bake is fast
    hdri = hdri.resize((2048, 1024), Image.Resampling.LANCZOS)
    save_png(equirect_to_cube_strip(hdri, SIZE), "r_env.png")

    snow = load256("snow_01")
    bluep = load256("blue_metal_plate")
    grass2 = load256("leafy_grass")
    rust3 = load256("rusty_metal_03")
    asph = load256("asphalt_02")
    woodt = load256("wood_table_001")
    rbrk = load256("red_brick")

    crystal = lum_alpha(grade(blend(snow, bluep, 0.4), 0.72, 0.95, 1.22, gadd=12, badd=36, sat=1.15, contrast=1.25), 40, 200)
    save_png(crystal, "r_item.png")
    save_png(grade(crystal, 1.05, 1.02, 1.08, radd=8, gadd=4, badd=10), "m_item.png")
    glass = lum_alpha(grade(bluep, 0.55, 0.82, 1.15, gadd=20, badd=40, sat=0.7, contrast=1.2), 30, 190)
    save_png(glass, "m_glass.png")

    slime = lum_alpha(grade(grass2, 0.45, 1.15, 0.55, gadd=28, sat=1.25, contrast=1.1), 20, 180)
    ice = lum_alpha(grade(snow, 0.62, 0.92, 1.22, gadd=16, badd=40, sat=0.8, contrast=1.18), 50, 210)
    spike = grade(rust3, 1.05, 0.72, 0.62, radd=40, contrast=1.15)
    darkp = grade(asph, 0.55, 0.42, 0.85, badd=40, sat=0.7)
    gim = Image.new("RGBA", (SIZE, SIZE))
    gim.paste(slime.resize((SIZE // 2, SIZE // 2), Image.Resampling.LANCZOS), (0, 0))
    gim.paste(spike.resize((SIZE // 2, SIZE // 2), Image.Resampling.LANCZOS), (SIZE // 2, 0))
    gim.paste(ice.resize((SIZE // 2, SIZE // 2), Image.Resampling.LANCZOS), (0, SIZE // 2))
    gim.paste(darkp.resize((SIZE // 2, SIZE // 2), Image.Resampling.LANCZOS), (SIZE // 2, SIZE // 2))
    save_png(gim, "m_gimmick.png")
    save_png(woodt, "r_wood.png")
    save_png(rbrk, "m_brick2.png")

    save_png(cloud_sprite(3, False), "r_sky.png")
    save_png(cloud_sprite(11, True), "r_sky2.png")

    hdri2 = Image.open(fetch_hdri("syferfontein", HDRI["syferfontein"]))
    hdri2 = hdri2.resize((2048, 1024), Image.Resampling.LANCZOS)
    save_png(equirect_to_cube_strip(hdri2, SIZE), "r_env2.png")
    hdri3 = Image.open(fetch_hdri("autoshop", HDRI["autoshop"]))
    hdri3 = hdri3.resize((2048, 1024), Image.Resampling.LANCZOS)
    save_png(equirect_to_cube_strip(hdri3, SIZE), "m_env.png")

    print("done", flush=True)


if __name__ == "__main__":
    import sys
    if "--sky-only" in sys.argv:
        save_png(cloud_sprite(3, False), "r_sky.png")
        save_png(cloud_sprite(11, True), "r_sky2.png")
        print("sky-only done", flush=True)
    else:
        main()
