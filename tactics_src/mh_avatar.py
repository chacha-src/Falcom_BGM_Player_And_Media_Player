# -*- coding: utf-8 -*-
"""MakeHuman hm08 body + CC0 mhclo clothes, retargeted onto the game humans."""
import os, re, json, zipfile, posixpath, io
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MHB = os.path.join(HERE, "mh_base")
AVS = os.path.join(HERE, "avatar_src")


# ------------------------------------------------------------ base mesh
class Base:
    pass


def load_base():
    b = Base()
    V, VT, F, FT, G = [], [], [], [], []
    grp = ""
    gid = {}
    with open(os.path.join(MHB, "base.obj"), "r", encoding="utf-8", errors="ignore") as f:
        for l in f:
            if l.startswith("v "):
                V.append([float(x) for x in l.split()[1:4]])
            elif l.startswith("vt "):
                VT.append([float(x) for x in l.split()[1:3]])
            elif l.startswith("g "):
                grp = l.split(None, 1)[1].strip()
                gid.setdefault(grp, len(gid))
            elif l.startswith("f "):
                vs = l.split()[1:]
                F.append([int(x.split("/")[0]) - 1 for x in vs])
                FT.append([int(x.split("/")[1]) - 1 if "/" in x and x.split("/")[1] else -1 for x in vs])
                G.append(gid[grp])
    b.v = np.array(V, np.float64)
    b.vt = np.array(VT, np.float64)
    b.f = F
    b.ft = FT
    b.fg = np.array(G, np.int32)
    b.groups = {v: k for k, v in gid.items()}
    b.gid = gid
    return b


def load_target(name):
    p = os.path.join(MHB, name if name.endswith(".target") else name + ".target")
    I, D = [], []
    with open(p, "r", encoding="utf-8", errors="ignore") as f:
        for l in f:
            if not l or l[0] == "#":
                continue
            s = l.split()
            if len(s) == 4:
                I.append(int(s[0]))
                D.append([float(s[1]), float(s[2]), float(s[3])])
    return np.array(I, np.int64), np.array(D, np.float64)


def apply_targets(v, tlist):
    out = v.copy()
    for name, w in tlist:
        if abs(w) < 1e-6:
            continue
        i, d = load_target(name)
        out[i] += d * w
    return out


ETH = ("african", "asian", "caucasian")

# 顔の型。"lr:" は左右両方の target。eye=虹彩色(sRGB) brow=眉色 iris=虹彩の角半径
_SMALL_MOUTH = [("mouth-scale-depth-decr", 0.70), ("mouth-lowerlip-volume-decr", 0.55), ("mouth-upperlip-volume-decr", 0.35)]
FACES_F = [
    dict(key="cute", eye=(0.42, 0.24, 0.12), brow=(0.42, 0.29, 0.20), iris=0.62, t=[
        ("lr:eye-scale-incr", 0.95), ("nose-scale-horiz-decr", 0.45), ("nose-scale-vert-decr", 0.40), ("nose-volume-decr", 0.30),
        ("chin-prominent-decr", 0.50), ("chin-width-decr", 0.45), ("mouth-scale-horiz-decr", 0.45), ("head-round", 0.30),
        ("lr:cheek-volume-incr", 0.30), ("forehead-scale-vert-incr", 0.20), ("head-fat-decr", 0.20), ("head-age-decr", 0.90)] + _SMALL_MOUTH),
    dict(key="baby", eye=(0.20, 0.12, 0.08), brow=(0.36, 0.25, 0.18), iris=0.68, t=[
        ("lr:eye-scale-incr", 1.0), ("lr:eye-trans-down", 0.30), ("head-round", 0.70), ("forehead-scale-vert-incr", 0.50),
        ("nose-scale-horiz-decr", 0.60), ("nose-scale-vert-decr", 0.60), ("nose-volume-decr", 0.50), ("chin-height-decr", 0.50),
        ("chin-prominent-decr", 0.50), ("lr:cheek-volume-incr", 0.50), ("mouth-scale-horiz-decr", 0.55), ("head-age-decr", 1.0)] + _SMALL_MOUTH),
    dict(key="cool", eye=(0.36, 0.44, 0.55), brow=(0.20, 0.15, 0.12), iris=0.56, t=[
        ("lr:eye-scale-incr", 0.45), ("lr:eye-height2-decr", 0.40), ("lr:eye-trans-out", 0.20), ("eyebrows-angle-up", 0.60),
        ("head-oval", 0.70), ("lr:cheek-bones-incr", 0.35), ("nose-point-width-decr", 0.45), ("nose-scale-horiz-decr", 0.30),
        ("chin-width-decr", 0.50), ("mouth-scale-horiz-decr", 0.30), ("head-age-decr", 0.50)] + _SMALL_MOUTH),
    dict(key="gentle", eye=(0.48, 0.34, 0.16), brow=(0.46, 0.33, 0.22), iris=0.64, t=[
        ("lr:eye-scale-incr", 0.80), ("lr:eye-height2-incr", 0.35), ("lr:eye-trans-down", 0.20), ("eyebrows-angle-down", 0.70),
        ("head-round", 0.45), ("lr:cheek-volume-incr", 0.35), ("mouth-cupidsbow-incr", 0.30), ("nose-scale-horiz-decr", 0.40),
        ("nose-scale-vert-decr", 0.35), ("chin-prominent-decr", 0.40), ("head-age-decr", 0.80)] + _SMALL_MOUTH),
    dict(key="elf", eye=(0.24, 0.52, 0.32), brow=(0.66, 0.54, 0.34), iris=0.58, t=[
        ("lr:ear-shape-pointed", 1.0), ("lr:ear-scale-vert-incr", 0.70), ("head-invertedtriangular", 0.60), ("head-oval", 0.30),
        ("lr:eye-scale-incr", 0.70), ("eyebrows-angle-up", 0.30), ("nose-point-width-decr", 0.50), ("nose-scale-horiz-decr", 0.40),
        ("chin-width-decr", 0.60), ("chin-prominent-decr", 0.30), ("mouth-scale-horiz-decr", 0.40), ("head-age-decr", 0.70)] + _SMALL_MOUTH),
    dict(key="lively", eye=(0.66, 0.38, 0.10), brow=(0.40, 0.25, 0.14), iris=0.62, t=[
        ("lr:eye-scale-incr", 0.90), ("lr:eye-height1-incr", 0.30), ("eyebrows-trans-up", 0.40), ("head-invertedtriangular", 0.40),
        ("mouth-cupidsbow-incr", 0.40), ("lr:cheek-inner-incr", 0.30), ("nose-trans-up", 0.30), ("nose-scale-vert-decr", 0.40),
        ("nose-scale-horiz-decr", 0.40), ("chin-width-decr", 0.40), ("head-age-decr", 0.85)] + _SMALL_MOUTH),
]
FACES_M = [
    dict(key="std", eye=(0.30, 0.20, 0.12), brow=(0.18, 0.13, 0.10), iris=0.50, t=[]),
    dict(key="rugged", eye=(0.22, 0.15, 0.10), brow=(0.14, 0.10, 0.08), iris=0.48, t=[
        ("head-square", 0.60), ("chin-prominent-incr", 0.50), ("chin-width-incr", 0.40), ("chin-bones-incr", 0.30),
        ("eyebrows-angle-up", 0.40), ("eyebrows-trans-down", 0.30), ("lr:cheek-bones-incr", 0.40), ("lr:eye-scale-decr", 0.20)]),
    dict(key="pretty", eye=(0.34, 0.42, 0.52), brow=(0.30, 0.22, 0.16), iris=0.54, t=[
        ("head-oval", 0.60), ("lr:eye-scale-incr", 0.45), ("head-age-decr", 0.55), ("nose-scale-horiz-decr", 0.30),
        ("chin-prominent-decr", 0.25), ("eyebrows-angle-down", 0.30), ("mouth-scale-horiz-decr", 0.20)]),
    dict(key="elf", eye=(0.24, 0.50, 0.30), brow=(0.62, 0.52, 0.34), iris=0.52, t=[
        ("lr:ear-shape-pointed", 1.0), ("lr:ear-scale-vert-incr", 0.70), ("head-oval", 0.50), ("lr:eye-scale-incr", 0.40),
        ("nose-point-width-decr", 0.40), ("chin-width-decr", 0.30), ("head-age-decr", 0.40)]),
    dict(key="boy", eye=(0.36, 0.22, 0.10), brow=(0.26, 0.18, 0.12), iris=0.56, t=[
        ("head-round", 0.50), ("head-age-decr", 1.0), ("lr:eye-scale-incr", 0.60), ("nose-scale-horiz-decr", 0.40),
        ("nose-scale-vert-decr", 0.40), ("chin-height-decr", 0.30), ("forehead-scale-vert-incr", 0.30), ("chin-prominent-decr", 0.30)]),
    dict(key="wild", eye=(0.62, 0.40, 0.12), brow=(0.16, 0.11, 0.08), iris=0.48, t=[
        ("head-diamond", 0.60), ("lr:cheek-bones-incr", 0.60), ("eyebrows-angle-up", 0.50), ("lr:eye-height2-decr", 0.30),
        ("mouth-scale-horiz-incr", 0.30), ("chin-bones-incr", 0.40)]),
]


def face_def(female, face):
    L = FACES_F if female else FACES_M
    return L[max(0, min(len(L) - 1, face))]


def shape(base, female, face=0, bust=0.0):
    """bust: -1 = mincup, 0 = average, +1 = maxcup"""
    g = "female" if female else "male"
    mix = {"african": 0.2, "asian": 0.4, "caucasian": 0.4} if female else {"african": 1 / 3., "asian": 1 / 3., "caucasian": 1 / 3.}
    tl = [("%s-%s-young" % (e, g), mix[e]) for e in ETH]
    if female:
        if bust > 0:
            tl.append(("female-young-averagemuscle-averageweight-maxcup-averagefirmness", bust))
        elif bust < 0:
            tl.append(("female-young-averagemuscle-averageweight-mincup-averagefirmness", -bust))
    for n, w in face_def(female, face)["t"]:
        if n.startswith("lr:"):
            tl += [("l-" + n[3:], w), ("r-" + n[3:], w)]
        else:
            tl.append((n, w))
    return apply_targets(base.v, tl)


# ------------------------------------------------------------ skeleton / weights
def load_skel(v):
    j = json.load(open(os.path.join(MHB, "default.mhskel"), "r", encoding="utf-8"))
    jp = {}
    for k, ids in j["joints"].items():
        jp[k] = v[np.array(ids, np.int64)].mean(0)
    bones = {}
    for name, d in j["bones"].items():
        h, t = d["head"], d["tail"]
        bones[name] = dict(parent=d.get("parent"), head=jp[h] if isinstance(h, str) else np.array(h, np.float64),
                           tail=jp[t] if isinstance(t, str) else np.array(t, np.float64))
    return bones


_W = None


def load_weights():
    global _W
    if _W is None:
        j = json.load(open(os.path.join(MHB, "default_weights.mhw"), "r", encoding="utf-8"))
        _W = {k: (np.array([a for a, _ in v], np.int64), np.array([b for _, b in v], np.float64)) for k, v in j["weights"].items()}
    return _W


def bone_weight_matrix(nv, bone_names):
    """dense (nv, nb) weights over the given bone list"""
    W = load_weights()
    M = np.zeros((nv, len(bone_names)), np.float64)
    for bi, n in enumerate(bone_names):
        if n in W:
            i, w = W[n]
            ok = i < nv
            M[i[ok], bi] += w[ok]
    return M


def bone_children():
    j = json.load(open(os.path.join(MHB, "default.mhskel"), "r", encoding="utf-8"))
    kids, par = {}, {}
    for n, d in j["bones"].items():
        kids.setdefault(d.get("parent"), []).append(n)
        par[n] = d.get("parent")
    return kids, par


def descendants(root):
    kids, _ = bone_children()
    out, st = [], [root]
    while st:
        x = st.pop()
        out.append(x)
        st += kids.get(x, [])
    return out


# ------------------------------------------------------------ geometry helpers
def to_render(v, ground):
    """MH decimetres, +Z forward -> game render space metres, -Z forward, feet on 0"""
    p = np.empty_like(v)
    p[:, 0] = -v[:, 0] * 0.1
    p[:, 1] = (v[:, 1] - ground) * 0.1
    p[:, 2] = -v[:, 2] * 0.1
    return p


def tris_of(faces):
    T = []
    for f in faces:
        for k in range(1, len(f) - 1):
            T.append((f[0], f[k], f[k + 1]))
    return np.array(T, np.int64).reshape(-1, 3)


def vnormals(pos, tri):
    n = np.zeros_like(pos)
    a, b, c = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    fn = np.cross(b - a, c - a)
    for k in range(3):
        np.add.at(n, tri[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def compact(pos, tri, *extra):
    used = np.unique(tri)
    mp = -np.ones(len(pos), np.int64)
    mp[used] = np.arange(len(used))
    return (pos[used], mp[tri]) + tuple(e[used] for e in extra) + (used,)


def uv_sphere(center, radius, fwd, up, nu=40, nv=28):
    fwd = fwd / np.linalg.norm(fwd)
    side = np.cross(up, fwd); side /= np.linalg.norm(side)
    up2 = np.cross(fwd, side)
    P, T, TH, PH = [], [], [], []
    for i in range(nv + 1):
        th = np.pi * i / nv          # 0 = front pole
        for k in range(nu):
            ph = 2 * np.pi * k / nu
            d = fwd * np.cos(th) + (side * np.cos(ph) + up2 * np.sin(ph)) * np.sin(th)
            P.append(center + d * radius)
            TH.append(th); PH.append(ph)
    for i in range(nv):
        for k in range(nu):
            a = i * nu + k; b = i * nu + (k + 1) % nu
            c = (i + 1) * nu + k; d = (i + 1) * nu + (k + 1) % nu
            T += [(a, c, b), (b, c, d)]
    P = np.array(P); T = np.array(T, np.int64)
    # 外向きに揃える
    a, b, c = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    if float(np.sum(np.cross(b - a, c - a) * (a - center))) < 0:
        T = T[:, ::-1].copy()
    return P, T, np.array(TH), np.array(PH), side, up2


def srgb_to_game(rgb):
    """same conversion the pack uses for zhum materials: sRGB colour divided by the lit factor"""
    lit = np.array([1.421, 1.336, 1.219])
    return np.clip(np.asarray(rgb, np.float64) / lit, 0, 1)


CUTE_SKIN = srgb_to_game([1.00, 0.86, 0.75])
MALE_SKIN = srgb_to_game([0.94, 0.76, 0.63])
LIP = srgb_to_game([0.93, 0.55, 0.55])
BLUSH = srgb_to_game([1.00, 0.68, 0.64])
BROW = srgb_to_game([0.42, 0.29, 0.20])
LASH = srgb_to_game([0.07, 0.05, 0.05])


def build_face(base, v, female, skin, fd=None):
    fd = fd or face_def(female, 0)
    """head + neck of the hm08 body, own eyeballs and lashes. render space, metres.
    kind: 1 head, 2 eye, 3 neck, 4 lash"""
    ground = float(v[:, 1].min())
    body_f = [f for f, g in zip(base.f, base.fg) if base.groups[g] == "body"]
    tri = tris_of(body_f)
    names = list(load_weights().keys())
    Wm = bone_weight_matrix(len(v), names)
    dom = np.argmax(Wm, 1)
    hb = set(descendants("head")) - {"eye.L", "eye.R"} - {n for n in names if n.startswith("tongue")}
    head_b = np.array([n in hb for n in names])
    neck_b = np.array([n in ("neck01", "neck02", "neck03") for n in names])
    hw = Wm[:, head_b].sum(1)
    kind = np.zeros(len(v), np.uint8)
    kind[neck_b[dom]] = 3
    kind[head_b[dom]] = 1
    # 首の付け根より上だけ
    keep = kind > 0
    t = tri[keep[tri].all(1)]
    pos = to_render(v, ground)
    col = np.tile(np.asarray(skin, np.float64), (len(v), 1))
    if female:
        def wsum(prefix):
            m = np.array([n.startswith(prefix) for n in names])
            return Wm[:, m].sum(1)
        lip = np.clip((wsum("oris") - 0.68) / 0.22, 0, 1) ** 1.5 * 0.85
        col = col * (1 - lip[:, None]) + LIP * lip[:, None]
    P, T, C, K, HW, used = compact(pos, t, col, kind, np.clip(hw, 0, 1))
    N = vnormals(P, T)
    parts = [(P, N, C, T, K)]
    hw_out = [HW]
    # 眼球。補助の球の中心と半径から作り直す（虹彩を大きく、ハイライト付き）
    for side in ("l", "r"):
        ef = [f for f, g in zip(base.f, base.fg) if base.groups[g] == "helper-%s-eye" % side]
        ids = np.unique(np.array(ef).reshape(-1))
        ep = pos[ids]
        c0 = ep.mean(0)
        rad = float(np.linalg.norm(ep - c0, axis=1).mean())
        sp, st, th, ph, sdv, upv = uv_sphere(c0, rad * 0.98, np.array([0.0, 0.0, -1.0]), np.array([0.0, 1.0, 0.0]))
        iris = fd["iris"]
        ec = np.tile(srgb_to_game([0.95, 0.94, 0.92]), (len(sp), 1))
        tt = th / iris
        irc = srgb_to_game(fd["eye"])
        irc2 = irc * 0.38
        m = tt < 1
        g = np.clip(tt, 0, 1)[:, None]
        ec[m] = (irc2 * g + irc * (1 - g))[m]
        # 上側を暗く
        upper = (np.sin(ph) > 0.2) & m
        ec[upper] *= 0.75
        ec[tt < 0.42] = srgb_to_game([0.05, 0.03, 0.02])
        if female:
            # キャッチライト
            dvec = sp - c0
            hl = np.array([0.45, 0.55, -0.70]); hl /= np.linalg.norm(hl)
            ang = np.arccos(np.clip((dvec / np.linalg.norm(dvec, axis=1, keepdims=True)) @ hl, -1, 1))
            ec[ang < 0.16] = srgb_to_game([1.0, 1.0, 1.0])
        en = (sp - c0) / np.linalg.norm(sp - c0, axis=1, keepdims=True)
        parts.append((sp, en, ec, st, np.full(len(sp), 2, np.uint8)))
    # まつ毛
    lf = [f for f, g in zip(base.f, base.fg) if "eyelashes" in base.groups[g]]
    if lf:
        lt = tris_of(lf)
        LP, LT, used_l = compact(pos, lt)
        LN = vnormals(LP, LT)
        parts.append((LP, LN, np.tile(LASH, (len(LP), 1)), LT, np.full(len(LP), 4, np.uint8)))
    out_p, out_n, out_c, out_t, out_k = [], [], [], [], []
    base_i = 0
    for p_, n_, c_, t_, k_ in parts:
        out_p.append(p_); out_n.append(n_); out_c.append(c_); out_t.append(t_ + base_i); out_k.append(k_)
        base_i += len(p_)
        if len(hw_out) < len(out_p):
            hw_out.append(np.ones(len(p_)))
    return dict(pos=np.concatenate(out_p), nrm=np.concatenate(out_n), col=np.concatenate(out_c),
                tri=np.concatenate(out_t), kind=np.concatenate(out_k), names=names, hw=np.concatenate(hw_out),
                body_ids=used)


def paint_face(face, female, fd=None):
    """brows and blush, from the eye positions"""
    fd = fd or face_def(female, 0)
    brow_c = srgb_to_game(fd["brow"])
    P, C, K = face["pos"], face["col"], face["kind"]
    eyes = P[K == 2]
    if not len(eyes):
        return
    head = K == 1
    for sgn in (-1, 1):
        e = eyes[(eyes[:, 0] * sgn) > 0]
        c0 = e.mean(0)
        r = float(np.abs(e[:, 1] - c0[1]).max())
        dx = (P[:, 0] - c0[0]) * sgn          # 外側が正
        dy = P[:, 1] - c0[1]
        front = P[:, 2] < c0[2] + r * 0.6
        # 眉: 目の上、外へ向かって少し下がる弧
        u = dx / (r * 1.9)
        arc = r * (2.05 + 0.35 * (1 - (u + 0.15) ** 2))
        thick = r * ((0.22 if female else 0.34) - 0.10 * np.clip(u, 0, 1))
        bw = np.clip(1.25 - np.abs(dy - arc) / thick, 0, 1) * np.clip((1 - u) * 4, 0, 1) * np.clip((u + 0.70) * 5, 0, 1)
        bw[~(head & front)] = 0
        bw = bw * bw * (3 - 2 * bw) * (0.8 if female else 0.9)
        C[:] = C * (1 - bw[:, None]) + brow_c * bw[:, None]
        if not female:
            continue
        # 頬
        cc = c0 + np.array([sgn * r * 0.35, -r * 2.1, -r * 0.2])
        d = np.linalg.norm((P - cc) / np.array([r * 1.5, r * 0.95, r * 2.5]), axis=1)
        bl = np.clip(1 - d, 0, 1) ** 1.5 * 0.55
        bl[~(head & front)] = 0
        C[:] = C * (1 - bl[:, None]) + BLUSH * bl[:, None]


# ------------------------------------------------------------ clothes (mhclo)
class Clo:
    pass


def _zip_for(item):
    for fn in sorted(os.listdir(AVS)):
        if not fn.endswith(".zip"):
            continue
        z = zipfile.ZipFile(os.path.join(AVS, fn))
        for n in z.namelist():
            if n.endswith("/" + item + ".mhclo"):
                return z, n
    raise KeyError(item)


def load_clo(item):
    z, n = _zip_for(item)
    low = {x.lower(): x for x in z.namelist()}
    d = posixpath.dirname(n)
    t = z.read(n).decode("utf-8", "ignore").splitlines()
    c = Clo()
    c.name = item
    lic = [l for l in t if re.match(r"^#\s*license", l, re.I)]
    c.license = lic[0].split(None, 2)[2].strip() if lic else "?"
    lic = re.sub(r"[^A-Z0-9]", "", c.license.upper())
    # 「CC BY 4.0」も CC-BY。AGPL の靴やスカートは使わない。
    assert lic.startswith(("CC0", "CCBY", "MIT")), (item, c.license)
    c.scale = {}
    ref, wts, off, dele = [], [], [], []
    mode = None
    objf = mat = None
    for l in t:
        s = l.split()
        if not s or s[0].startswith("#"):
            continue
        if re.match(r"^[a-z_]", s[0]):
            key = s[0]
            mode = None
            if key == "obj_file":
                objf = s[1]
            elif key == "material":
                mat = s[1]
            elif key in ("x_scale", "y_scale", "z_scale"):
                c.scale[key[0]] = (int(s[1]), int(s[2]), float(s[3]))
            elif key == "verts":
                mode = "v"
            elif key == "delete_verts":
                mode = "d"
            continue
        if mode == "v":
            if len(s) >= 9:
                ref.append([int(s[0]), int(s[1]), int(s[2])])
                wts.append([float(s[3]), float(s[4]), float(s[5])])
                off.append([float(s[6]), float(s[7]), float(s[8])])
            elif len(s) == 1:
                ref.append([int(s[0])] * 3); wts.append([1.0, 0, 0]); off.append([0.0, 0, 0])
        elif mode == "d":
            k = 0
            while k < len(s):
                if k + 2 < len(s) and s[k + 1] == "-":
                    dele += range(int(s[k]), int(s[k + 2]) + 1); k += 3
                else:
                    dele.append(int(s[k])); k += 1
    c.ref = np.array(ref, np.int64); c.w = np.array(wts); c.off = np.array(off)
    c.delete = np.array(sorted(set(dele)), np.int64)
    V, VT, F, FT = [], [], [], []
    for l in z.read(low[posixpath.join(d, objf).lower()]).decode("utf-8", "ignore").splitlines():
        if l.startswith("v "):
            V.append(1)
        elif l.startswith("vt "):
            VT.append([float(x) for x in l.split()[1:3]])
        elif l.startswith("f "):
            vs = l.split()[1:]
            F.append([int(x.split("/")[0]) - 1 for x in vs])
            FT.append([int(x.split("/")[1]) - 1 if len(x.split("/")) > 1 and x.split("/")[1] else -1 for x in vs])
    assert len(V) == len(c.ref), (item, len(V), len(c.ref))
    c.vt = np.array(VT) if VT else np.zeros((1, 2))
    c.f, c.ft = F, FT
    # 拡散テクスチャ
    tex = None
    if mat and posixpath.join(d, mat).lower() in low:
        for l in z.read(low[posixpath.join(d, mat).lower()]).decode("utf-8", "ignore").splitlines():
            s = l.split(None, 1)
            if len(s) == 2 and s[0] == "diffuseTexture":
                p = posixpath.join(d, posixpath.basename(s[1].strip())).lower()
                if p in low:
                    tex = low[p]
    if tex is None:
        cand = [x for x in z.namelist() if posixpath.dirname(x) == d and x.lower().endswith((".png", ".jpg"))
                and not re.search(r"(?i)norm|nrm|spec|bump|rough|height|metal|disp", posixpath.basename(x))]
        tex = cand[0] if cand else None
    c.img = None
    if tex:
        from PIL import Image
        im = Image.open(io.BytesIO(z.read(tex))).convert("RGBA")
        if max(im.size) > 1024:
            im = im.resize((im.size[0] * 1024 // max(im.size), im.size[1] * 1024 // max(im.size)))
        c.img = np.asarray(im, np.float64) / 255.0
    return c


def clo_positions(c, v):
    sc = np.ones(3)
    for k, ax in (("x", 0), ("y", 1), ("z", 2)):
        if k in c.scale:
            i, j, den = c.scale[k]
            sc[ax] = abs(v[i, ax] - v[j, ax]) / den
    return (v[c.ref] * c.w[:, :, None]).sum(1) + c.off * sc


def clo_colors(c):
    """per obj vertex sRGB colour + alpha, averaged over the face corners"""
    n = len(c.ref)
    acc = np.zeros((n, 4)); cnt = np.zeros(n)
    if c.img is None:
        return np.tile([0.7, 0.7, 0.7, 1.0], (n, 1))
    H, W = c.img.shape[:2]
    for f, ft in zip(c.f, c.ft):
        for vi, ti in zip(f, ft):
            if ti < 0 or ti >= len(c.vt):
                continue
            u, vv = c.vt[ti]
            x = int(np.clip((u % 1.0) * (W - 1), 0, W - 1)); y = int(np.clip((1 - (vv % 1.0)) * (H - 1), 0, H - 1))
            acc[vi] += c.img[y, x]; cnt[vi] += 1
    out = acc / np.maximum(cnt, 1)[:, None]
    out[cnt == 0] = [0.7, 0.7, 0.7, 1.0]
    return out


def tint(rgb, how):
    if not how:
        return rgb
    lum = rgb @ np.array([0.30, 0.59, 0.11])
    rel = lum / max(float(np.median(lum)), 1e-3)
    if how == "white":
        return np.clip(np.array([0.97, 0.96, 0.94]) * (0.80 + 0.20 * np.clip(rel, 0, 1.25))[:, None], 0, 1)
    if how == "black":
        return np.clip(np.array([0.085, 0.075, 0.095]) * (0.75 + 0.25 * np.clip(rel, 0, 1.6))[:, None], 0, 1)
    if isinstance(how, tuple):
        return np.clip(np.array(how) * (0.65 + 0.35 * np.clip(rel, 0, 1.6))[:, None], 0, 1)
    return rgb


# 衣装。parts = (mhclo 名, 色) 。性別ごとに 1 から番号。
AVATARS_F = [
    ("wedding", [("toigo_camisole_dress_with_full_skirt", "white"), ("toigo_ballet_flats_with_bows", "white")]),
    ("gothic", [("toigo_bodice_dress_with_lace_ruffle_skirt", "black"), ("toigo_ankle_boots_female", "black")]),
    ("ruffle", [("toigo_strapless_ruffle_top_dress", None), ("toigo_ballet_flats", None)]),
    ("longhalter", [("toigo_halter_dress_with_fluted_skirt", None), ("toigo_ballet_flats", None)]),
    ("tiered", [("toigo_dress_with_tiered_skirt", None), ("toigo_ballet_flats_with_flowers", None)]),
    ("kimono", [("mindfront_kimono", None), ("toigo_mj_cloth_shoes", None)]),
    ("flapper", [("aethelraed_flapper_dress", None), ("toigo_ballet_flats", None)]),
    ("keyhole", [("toigo_keyhole_neck_dress", None), ("toigo_ankle_boots_female", None)]),
    ("midi", [("toigo_halter_dress_midi", None), ("toigo_ballet_flats_with_bows", None)]),
    ("cutout", [("toigo_cut_out_dress", None), ("toigo_ankle_boots_female", None)]),
    ("shift", [("toigo_shift_dress", None), ("toigo_ballet_flats", None)]),
    ("suit", [("toigo_female_suit", None), ("toigo_ankle_boots_female", None)]),
    ("dbsuit", [("toigo_female_double-breasted_suit", None), ("toigo_ankle_boots_female", None)]),
    ("suit2", [("toigo_female_suit_2", None), ("toigo_ankle_boots_female", None)]),
    ("lolita", [("toigo_bodice-style_top", None), ("toigo_skirt_with_lace_ruffle", None), ("culturalibre_heroine_boots_3", None)]),
    ("longskirt", [("toigo_camisole_top", None), ("toigo_long_full_skirt", None), ("toigo_ballet_flats", None)]),
    ("mini", [("toigo_turtleneck_halter_top", None), ("toigo_tiered_mini_skirt", None), ("culturalibre_heroine_boots_2", None)]),
    ("harem", [("joepal_crude_t-shirt_female", None), ("toigo_harem_pants", None), ("toigo_flats", None)]),
    # Elvaerwyn, CC-BY（dress02）。暗いフリルドレスの上に白いエプロン。
    ("maid", [("elvs_frilled_party_dress", None), ("elvs_ladies_apron", "white"), ("toigo_ballet_flats", "black")]),
    ("qipao", [("elvs_fashion_stylized_qipoa", None), ("toigo_ballet_flats", "black")]),
    ("goddess", [("elvs_goddess_dress1", None), ("toigo_ballet_flats", "white")]),
    # ブラウスは MistressSaeko CC-BY。襟と胸リボンは CC0 の単体、スカートは MTKnife のプリーツ。
    ("sailor", [("saeko_sailor_top", "white"), ("ogg_sailor_collar", None), ("ogg_sailor_ribbon", None),
                ("mtknife_pleated_mini_skirt", (0.10, 0.13, 0.32)), ("toigo_ballet_flats", "black")]),
    # 白いセーターと青のプリーツチェック。穴の開いたニットは裾が欠けるので使わない。
    ("jk", [("janexx_old_female_sweater", "white"), ("elvs_pleated_plaid_mini_skirt", None), ("toigo_ballet_flats", "black")]),
    # ブレザーのジャケットはパックに無い。学校の上着に見えるニットとカーディガンを 4 種。
    ("knitpencil", [("mindfront_knitted_sweater_01", (0.20, 0.24, 0.38)), ("elvs_pencil_skirt", (0.16, 0.17, 0.22)), ("toigo_ballet_flats", "black")]),
    ("knitmidi", [("mindfront_knitted_sweater_01", (0.28, 0.30, 0.34)), ("elvs_gored_midi_skirt", None), ("toigo_ballet_flats", "black")]),
    ("coat", [("punkduck_winter_coat", None), ("toigo_ballet_flats", "black")]),
    ("cardjean", [("mindfront_cardigan_long_open_front", None), ("punkduck_female_tight_jeans", None), ("toigo_ankle_boots_female", "black")]),
    # ギャル。厚底ブーツは AGPL なので、CC0 のブーティにする。
    ("galcrop", [("punkduck_sleeveless_crop_top", None), ("punkduck_black_mini_skirt", None), ("toigo_stiletto_booties", "black")]),
    ("galoff", [("punkduck_off-shoulder_long-sleeve_top", None), ("punkduck_female_tight_jeans", None), ("toigo_stiletto_booties", None)]),
    ("swim", [("mindfront_f_one-piece_swimsuit_01", None)]),
    ("tube", [("punkduck_tube_dress", None), ("toigo_stiletto_booties", "black")]),
    ("cocktail", [("punkduck_black_cocktail_dress", None), ("toigo_stiletto_booties", "black")]),
]
AVATARS_M = [
    ("tuxedo", [("toigo_suit_with_dinner_jacket", None), ("toigo_ankle_boots_male", "black")]),
    ("bowtie", [("toigo_suit_with_jacket_and_bowtie", None), ("toigo_ankle_boots_male", "black")]),
    ("dbsuit", [("toigo_male_double-breasted_suit", None), ("toigo_ankle_boots_male", None)]),
    ("suit3", [("toigo_male_suit_3", None), ("toigo_ankle_boots_male", None)]),
    ("tiesuit", [("toigo_male_suit_tie_and_jacket", None), ("toigo_ankle_boots_male", None)]),
    ("kimono", [("mindfront_kimono", (0.22, 0.26, 0.38)), ("toigo_mj_cloth_shoes", None)]),
    ("sweater", [("toigo_fisherman_sweater", None), ("toigo_wool_pants", None), ("toigo_ankle_boots_male", (0.30, 0.19, 0.11))]),
    ("sweaterharem", [("toigo_fisherman_sweater", None), ("toigo_harem_pants", None), ("culturalibre_male_boots", None)]),
    ("tee", [("toigo_basic_tucked_t-shirt", None), ("toigo_wool_pants", None), ("toigo_mj_cloth_shoes", None)]),
    ("teeharem", [("toigo_basic_tucked_t-shirt", None), ("toigo_harem_pants", None), ("toigo_ankle_boots_male", None)]),
    ("crude", [("elvs_crude_t-shirt_male", None), ("toigo_wool_pants", None), ("toigo_ankle_boots_male", (0.30, 0.19, 0.11))]),
    ("crudeharem", [("elvs_crude_t-shirt_male", None), ("toigo_harem_pants", None), ("culturalibre_male_boots", None)]),
    # Elvaerwyn, CC-BY（shirts02）の白シャツに、黒いズボン。
    ("butler", [("elvs_male_shirt_tie_tucked1", "white"), ("toigo_wool_pants", "black"), ("toigo_ankle_boots_male", "black")]),
    # ワイシャツにネクタイ、紺のスラックス。学ランのジャケットはパックに無い。
    ("school", [("elvs_male_shirt_tie_tucked1", "white"), ("mindfront_male_trousers_1", (0.15, 0.17, 0.28)), ("toigo_ankle_boots_male", "black")]),
    ("swim", [("mindfront_male_swimming_trunks_01", None)]),
    ("polo", [("namuhekam_male_polo_shirt", None), ("punkduck_male_classic_jeans", None), ("toigo_ankle_boots_male", (0.30, 0.19, 0.11))]),
    ("untuck", [("elvs_male_shirt_untucked_bd1", None), ("mindfront_male_trousers_2", None), ("toigo_mj_cloth_shoes", None)]),
    ("tankslack", [("elvs_male_tankshirt1", None), ("toigo_wool_pants", (0.22, 0.24, 0.30)), ("toigo_mj_cloth_shoes", None)]),
    ("tankjean", [("elvs_male_tankshirt1", None), ("punkduck_male_classic_jeans", None), ("toigo_ankle_boots_male", (0.30, 0.19, 0.11))]),
]

# MH の骨 -> 区間。区間はゲームの骨 1 本に対応する。
FINGERS = ["Index", "Middle", "Ring", "Pinky"]  # MH finger2..5
SEGS = ["Hips", "Abdomen", "Torso", "Chest", "Neck", "Head",
        "Shoulder.L", "UpperArm.L", "LowerArm.L", "Hand.L", "Shoulder.R", "UpperArm.R", "LowerArm.R", "Hand.R",
        "UpperLeg.L", "LowerLeg.L", "Foot.L", "UpperLeg.R", "LowerLeg.R", "Foot.R"]
# 指は関節ごとの区間（名前は 1 始まりの正規名。モデルごとの実名は seg_joint_names）。
SEGS += [f"{f}{i}.{s}" for s in ("L", "R") for f in FINGERS for i in (1, 2, 3)] + [f"Thumb{i}.{s}" for s in ("L", "R") for i in (2, 3)]


def seg_joint_names(qj):
    """正規の区間名 -> そのモデルの関節名。指の付け根が 1 番の体と、1 番が無いか手首にある中手骨の根で 2 番の体がある。"""
    base = 2
    if "Index1.R" in qj and "Hand.R" in qj:
        d1 = float(np.linalg.norm(qj["Index1.R"] - qj["Hand.R"]))
        d2 = float(np.linalg.norm(qj["Index2.R"] - qj["Index1.R"])) if "Index2.R" in qj else 0.0
        base = 1 if d1 > 0.5 * d2 else 2
    out = []
    for s in SEGS:
        m = re.match(r"(Index|Middle|Ring|Pinky)(\d)\.([LR])$", s)
        out.append(f"{m.group(1)}{int(m.group(2)) - 1 + base}.{m.group(3)}" if m else s)
    return out


def seg_of(bone, head_set):
    if bone in head_set:
        return "Head"
    side = bone[-1] if bone[-2:] in (".L", ".R") else None
    b = bone[:-2] if side else bone
    if b.startswith("neck"):
        return "Neck"
    if b in ("spine01", "spine02", "breast"):
        return "Chest"
    if b == "spine03":
        return "Torso"
    if b == "spine04":
        return "Abdomen"
    if b in ("spine05", "root", "pelvis"):
        return "Hips"
    if not side:
        return "Hips"
    if b in ("clavicle", "shoulder01"):
        return "Shoulder." + side
    if b.startswith("upperarm"):
        return "UpperArm." + side
    if b.startswith("lowerarm"):
        return "LowerArm." + side
    m = re.match(r"finger(\d)-(\d)$", b)
    if m:
        k, i = int(m.group(1)), int(m.group(2))
        if k == 1:
            return ("Thumb2." if i == 1 else "Thumb3.") + side
        return f"{FINGERS[k - 2]}{i}.{side}"
    if b.startswith(("wrist", "metacarpal")):
        return "Hand." + side
    if b.startswith("upperleg"):
        return "UpperLeg." + side
    if b.startswith("lowerleg"):
        return "LowerLeg." + side
    if b.startswith(("foot", "toe")):
        return "Foot." + side
    return "Hips"


def seg_weights(Wm, names):
    hs = set(descendants("head"))
    sidx = np.array([SEGS.index(seg_of(n, hs)) for n in names])
    S = np.zeros((len(Wm), len(SEGS)))
    for bi in range(len(names)):
        S[:, sidx[bi]] += Wm[:, bi]
    order = np.argsort(-S, 1)[:, :4]
    w = np.take_along_axis(S, order, 1)
    w = w / np.maximum(w.sum(1, keepdims=True), 1e-9)
    w[w.sum(1) < 1e-6, 0] = 1.0
    return order.astype(np.uint8), w


class GenderBody:
    """the shared avatar body for one gender: face block first (same order as the face presets)"""
    pass


def build_body(female):
    b = base_cached()
    v = shape(b, female, 0)
    ground = float(v[:, 1].min())
    skin = CUTE_SKIN if female else MALE_SKIN
    f = face_source(female, skin, 0)
    names = f["names"]
    nb_face = len(f["body_ids"])
    nface = len(f["pos"])
    body_f = [ff for ff, g in zip(b.f, b.fg) if b.groups[g] == "body"]
    tri = tris_of(body_f)
    ids = np.unique(tri)
    rest = np.setdiff1d(ids, f["body_ids"])
    # 出力の並び: 顔ブロック (体の頭頂点 + 目 + まつ毛) -> 残りの体
    mp = -np.ones(len(v), np.int64)
    mp[f["body_ids"]] = np.arange(nb_face)
    mp[rest] = nface + np.arange(len(rest))
    Wm = bone_weight_matrix(len(v), names)
    gb = GenderBody()
    gb.female = female
    gb.ground = ground
    gb.v = v
    gb.names = names
    gb.Wm = Wm
    gb.base_index = np.concatenate([f["body_ids"], -np.ones(nface - nb_face, np.int64), rest])
    gb.nface = nface
    P = np.concatenate([f["pos"], to_render(v[rest], ground)])
    gb.pos = P
    gb.col = np.concatenate([f["col"], np.tile(skin, (len(rest), 1))])
    # 体の三角形 (base 頂点で保持。衣装ごとに削除頂点を含む面を外す)
    gb.body_tri_base = tri
    gb.body_tri = mp[tri]
    extra = f["tri"][(f["kind"][f["tri"]] == 2).any(1) | (f["kind"][f["tri"]] == 4).any(1)]
    gb.extra_tri = extra
    allt = np.concatenate([gb.body_tri, extra])
    gb.nrm = vnormals(P, allt)
    gb.nrm[nb_face:nface] = f["nrm"][nb_face:nface]
    # 重み
    WB = np.zeros((len(P), len(names)))
    WB[:nb_face] = Wm[f["body_ids"]]
    WB[nface:] = Wm[rest]
    hi = names.index("head")
    WB[nb_face:nface, hi] = 1.0
    gb.WB = WB
    if female:
        vp = shape(b, female, 0, bust=1.0)
        vn = shape(b, female, 0, bust=-1.0)
        gb.vp, gb.vn = vp, vn
        def bodyd(vx):
            d = np.zeros_like(P)
            d[:nb_face] = (to_render(vx[f["body_ids"]], ground) - to_render(v[f["body_ids"]], ground))
            d[nface:] = to_render(vx[rest], ground) - to_render(v[rest], ground)
            return d
        gb.dmax, gb.dmin = bodyd(vp), bodyd(vn)
    return gb


def build_costume(gb, parts):
    """clothes for one costume on the gender body. returns dict with canonical render-space arrays."""
    P, N, C, T, W, DX, DN = [], [], [], [], [], [], []
    dele = set()
    base = 0
    for item, how in parts:
        c = load_clo(item)
        dele.update(int(x) for x in c.delete)
        # 隠す頂点が書かれていない服は、布から 3.5cm 以内の素体を消す。開いたカーディガンは胸を残す。
        if len(c.delete) == 0 and item in (
                "saeko_sailor_top", "saeko_sailor_skirt", "mtknife_pleated_mini_skirt",
                "elvs_pencil_skirt", "elvs_gored_midi_skirt",
                "punkduck_black_mini_skirt", "punkduck_off-shoulder_long-sleeve_top", "janexx_old_female_sweater",
                "namuhekam_male_polo_shirt", "elvs_male_athletic_tank1", "elvs_male_tankshirt1",
                "toigo_stiletto_booties"):
            from scipy.spatial import cKDTree
            cloth = clo_positions(c, gb.v)
            dist, _ = cKDTree(cloth).query(gb.v, distance_upper_bound=0.035)
            dele.update(int(i) for i in np.flatnonzero(np.isfinite(dist) & (dist < 0.035)))
        p = to_render(clo_positions(c, gb.v), gb.ground)
        rgba = clo_colors(c)
        rgb = srgb_to_game(tint(rgba[:, :3], how))
        tr = tris_of([ff for ff in c.f])
        # 透過（レースの抜き）は面ごとに捨てる
        a = rgba[:, 3]
        tr = tr[a[tr].mean(1) > 0.35]
        w = (gb.Wm[c.ref] * c.w[:, :, None]).sum(1)
        w = w / np.maximum(w.sum(1, keepdims=True), 1e-9)
        P.append(p); C.append(rgb); T.append(tr + base); W.append(w)
        if gb.female:
            DX.append(to_render(clo_positions(c, gb.vp), gb.ground) - p)
            DN.append(to_render(clo_positions(c, gb.vn), gb.ground) - p)
        base += len(p)
    # 下衣（ズボン・スカート）は上衣と重なる高さで上衣の内側へ入れる。入れないと腰帯が上衣を突き抜ける。
    spans = []
    s0 = 0
    for p_ in P:
        spans.append((s0, s0 + len(p_))); s0 += len(p_)
    role = []
    for it, _ in parts:
        if re.search(r"apron", it):
            role.append("apron")
        elif re.search(r"pants|skirt|trouser|jean|short", it):
            role.append("bottom")
        elif re.search(r"shoe|boot|flats|stiletto", it):
            role.append("shoes")
        else:
            role.append("top")
    Pall = np.concatenate(P)
    from scipy.spatial import cKDTree
    # 上衣は、重なるズボンやスカートを内側へ入れる。エプロンは殻ではないので、
    # 半径でドレスを潰すと穴が開く。少し外へ出してドレスの上に乗せる。
    for ti, rt in enumerate(role):
        if rt != "apron":
            continue
        b0, b1 = spans[ti]
        bp = Pall[b0:b1]
        rad = np.sqrt(bp[:, 0] ** 2 + bp[:, 2] ** 2)
        push = np.where(rad > 1e-4, 0.012 / np.maximum(rad, 1e-4), 0.0)
        bp[:, 0] += bp[:, 0] * push
        bp[:, 2] += bp[:, 2] * push
        Pall[b0:b1] = bp
    layers = ((("top",), ("bottom",)),)
    for outer_roles, inner_roles in layers:
      for ti, rt in enumerate(role):
        if rt not in outer_roles:
            continue
        a0, a1 = spans[ti]
        top = Pall[a0:a1]
        tree = cKDTree(top)
        y0, y1 = float(top[:, 1].min()), float(top[:, 1].max())
        for bi, rb in enumerate(role):
            if rb not in inner_roles or bi == ti:
                continue
            b0, b1 = spans[bi]
            bp = Pall[b0:b1]
            m = (bp[:, 1] > y0) & (bp[:, 1] < y1 + 0.01)
            if not m.any():
                continue
            dist, nn = tree.query(bp[m], distance_upper_bound=0.05)
            ok = np.isfinite(dist)
            idx = np.flatnonzero(m)[ok]
            tv = top[nn[ok]]
            # 胴の中心軸（その高さの上衣の中心）からの半径で比べる
            cz = np.array([float(top[np.abs(top[:, 1] - y) < 0.02, 2].mean()) if np.any(np.abs(top[:, 1] - y) < 0.02) else float(top[:, 2].mean()) for y in bp[idx, 1]])
            cx = np.zeros(len(idx))
            dvb = np.stack([bp[idx, 0] - cx, bp[idx, 2] - cz], 1)
            dvt = np.stack([tv[:, 0] - cx, tv[:, 2] - cz], 1)
            rb_ = np.linalg.norm(dvb, axis=1); rt_ = np.linalg.norm(dvt, axis=1)
            want = rt_ - 0.005
            fix = rb_ > want
            k = np.where(fix, np.clip(want / np.maximum(rb_, 1e-6), 0.6, 1.0), 1.0)
            bp[idx, 0] = cx + dvb[:, 0] * k
            bp[idx, 2] = cz + dvb[:, 1] * k
            Pall[b0:b1] = bp
    P = Pall
    Wall = np.concatenate(W)
    # 袖の位置なのに胸ウェイトだと、腕を上げたとき袖が肩に残り素体が飛び出す。
    sj_c, sw_c = seg_weights(Wall, gb.names)
    cdom = sj_c[np.arange(len(P)), np.argmax(sw_c, 1)]
    sj_b, sw_b = seg_weights(gb.WB, gb.names)
    bdom = sj_b[np.arange(len(gb.pos)), np.argmax(sw_b, 1)]
    arm_si = [SEGS.index(n) for n in SEGS if n.startswith(("UpperArm", "LowerArm", "Hand", "Shoulder"))]
    src_si = [SEGS.index(n) for n in SEGS if n.startswith(("UpperArm", "LowerArm", "Hand"))]
    arm_body = np.flatnonzero(np.isin(bdom, src_si))
    if len(arm_body):
        from scipy.spatial import cKDTree
        atree = cKDTree(gb.pos[arm_body])
        arm_w = np.zeros(len(P))
        for k in range(sw_c.shape[1]):
            arm_w += sw_c[:, k] * np.isin(sj_c[:, k], arm_si)
        weak = (np.abs(P[:, 0]) > 0.20) & (P[:, 1] > 0.92) & (P[:, 1] < 1.48) & (arm_w < 0.45)
        if weak.any():
            dist, nn = atree.query(P[weak], distance_upper_bound=0.022)
            hit = np.isfinite(dist)
            wi = np.flatnonzero(weak)[hit]
            src = arm_body[nn[hit]]
            Wall[wi] = gb.WB[src]
            Wall[wi] /= np.maximum(Wall[wi].sum(1, keepdims=True), 1e-9)
            sj_c, sw_c = seg_weights(Wall, gb.names)
            cdom = sj_c[np.arange(len(P)), np.argmax(sw_c, 1)]
    def _axis_r(Q, A, B):
        ab = B - A
        L2 = float(ab @ ab)
        if L2 < 1e-10:
            return np.full(len(Q), 1e9), np.zeros(len(Q))
        t = np.clip((Q - A) @ ab / L2, 0.0, 1.0)
        d = Q - (A + np.outer(t, ab))
        return np.linalg.norm(d, axis=1), t
    for side in ("L", "R"):
        ids = [SEGS.index("UpperArm." + side), SEGS.index("LowerArm." + side), SEGS.index("Hand." + side)]
        pts = []
        for si in ids:
            m = bdom == si
            pts.append(gb.pos[m].mean(0) if m.any() else None)
        if any(q is None for q in pts):
            continue
        for si, A, B in ((ids[0], pts[0], pts[1]), (ids[1], pts[1], pts[2])):
            bm = bdom == si
            cm = cdom == si
            if not bm.any() or not cm.any():
                continue
            br, bt = _axis_r(gb.pos[bm], A, B)
            okb = br < 0.062
            if okb.sum() < 8:
                continue
            br, bt = br[okb], bt[okb]
            rl, tt = _axis_r(P[cm], A, B)
            r_loc = np.empty(len(rl))
            for i in range(len(rl)):
                same = np.abs(bt - tt[i]) < 0.14
                sample = br[same] if same.sum() >= 4 else br
                r_loc[i] = float(np.percentile(sample, 50))
            want = r_loc + 0.008
            fix = rl < r_loc + 0.003
            if not fix.any():
                continue
            ax = A + np.outer(tt, B - A)
            rad = np.maximum(rl, 1e-5)
            sc = np.where(fix, want / rad, 1.0)
            P[cm] = ax + (P[cm] - ax) * sc[:, None]
    T = np.concatenate(T)
    out = dict(pos=P, col=np.concatenate(C), tri=T, W=Wall, nrm=vnormals(P, T))
    if gb.female:
        out["dmax"] = np.concatenate(DX); out["dmin"] = np.concatenate(DN)
    dl = np.zeros(len(gb.v), bool)
    if dele:
        dl[np.array(sorted(dele))] = True
    out["body_keep"] = ~dl[gb.body_tri_base].any(1)
    return out


# ------------------------------------------------------------ retarget onto a game skeleton
def _rot_from_to(a, b):
    a = np.asarray(a, np.float64); b = np.asarray(b, np.float64)
    na = float(np.linalg.norm(a)); nb = float(np.linalg.norm(b))
    if na < 1e-8 or nb < 1e-8:
        return np.eye(3)
    a = a / na; b = b / nb
    v = np.cross(a, b); c = float(a @ b)
    if np.linalg.norm(v) < 1e-9:
        return np.eye(3) if c > 0 else -np.eye(3)
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    den = 1.0 + c
    if abs(den) < 1e-8:
        return -np.eye(3)
    return np.eye(3) + K + K @ K * (1.0 / den)


def _seg_mat(A, B, a, b, s_lat, s_dep, length_clip=None):
    den = float(np.linalg.norm(B - A))
    if den < 1e-6:
        M = np.eye(4)
        M[:3, 3] = np.asarray(a, np.float64)
        return M
    D = (B - A) / den
    tlen = float(np.linalg.norm(np.asarray(b - a, np.float64)))
    if tlen < 1e-6:
        M = np.eye(4)
        M[:3, 3] = np.asarray(a, np.float64)
        return M
    L = tlen / den
    # 胸の頂点は骨の根元（バスト）に集まり、襟は骨の先にある。
    # 相手の Chest→Neck が短いと、骨の長さ比のまま掛けるとバストごと縦に潰れる。
    # 腹は逆に元の区間が 7cm しかなく、2 倍へ伸ばすと腰の前へ腹が張り出す。
    # 白人の体で形が残る範囲（身長比の 0.60〜1.55）へ揃える。腕と脚は長さを守るのでここには入れない。
    if length_clip is not None:
        lo, hi = length_clip
        if L < lo:
            L = lo
        elif L > hi:
            L = hi
    ex = np.array([1.0, 0, 0]) - D[0] * D
    if np.linalg.norm(ex) < 1e-6:
        ex = np.array([0, 0, 1.0]) - D[2] * D
    ex /= np.linalg.norm(ex)
    ez = np.cross(D, ex)
    S = L * np.outer(D, D) + s_lat * np.outer(ex, ex) + s_dep * np.outer(ez, ez)
    R = _rot_from_to(D, b - a)
    M = np.eye(4)
    M[:3, :3] = R @ S
    M[:3, 3] = a - R @ S @ A
    return M


def _unit(v, fallback):
    v = np.asarray(v, np.float64)
    n = float(np.linalg.norm(v))
    if n >= 1e-6:
        return v / n
    f = np.asarray(fallback, np.float64)
    n = float(np.linalg.norm(f))
    if n >= 1e-6:
        return f / n
    return np.array([1.0, 0.0, 0.0])


def _seg_mat_framed(A, B, LA, a, b, la, s_lat, s_dep):
    """A->B を a->b へ。横軸 LA を la へ合わせてロールも決める（手のひらと指）。"""
    # 指が一点に潰れていると横軸が消えて行列が非数になり、アバター選択で描画が落ちる。
    D = _unit(B - A, np.array([0.0, -1.0, 0.0]))
    d = _unit(b - a, D)
    E = LA - D * float(np.dot(LA, D))
    E = _unit(E, np.array([1.0, 0.0, 0.0]) - D * D[0])
    e = la - d * float(np.dot(la, d))
    e = _unit(e, np.array([1.0, 0.0, 0.0]) - d * d[0])
    F0 = np.stack([D, E, np.cross(D, E)], 1)
    F1 = np.stack([d, e, np.cross(d, e)], 1)
    R = F1 @ F0.T
    den = float(np.linalg.norm(np.asarray(B - A, np.float64)))
    L = float(np.linalg.norm(np.asarray(b - a, np.float64))) / den if den > 1e-6 else 1.0
    S = F0 @ np.diag([L, s_lat, s_dep]) @ F0.T
    M = np.eye(4)
    M[:3, :3] = R @ S
    M[:3, 3] = a - R @ S @ A
    return M


def model_segments(gb, qj, head_xf, foot_tip_z):
    """qj: game joint name -> bind position (render space). returns (len(SEGS), 4, 4)"""
    sk = load_skel(gb.v)
    J = lambda n, part="head": to_render(sk[n][part][None], gb.ground)[0]
    neck_mh = float(J("neck01")[1])
    g = float(qj["Neck"][1]) / neck_mh if abs(neck_mh) > 1e-4 else 1.0
    if not np.isfinite(g) or g < 0.4 or g > 2.5:
        g = 1.0
    M = {}
    lin = head_xf["sc"] * np.asarray(head_xf["yaw"], np.float64)
    Mh = np.eye(4); Mh[:3, :3] = lin; Mh[:3, 3] = head_xf["off"] - head_xf["sc"] * head_xf["mid"]
    M["Head"] = Mh
    hx = lambda p: (Mh @ np.append(p, 1))[:3]
    ua_mh = abs(J("upperarm01.L")[0]); ul_mh = abs(J("upperleg01.L")[0])
    ua_q = abs(qj["UpperArm.L"][0]); ul_q = abs(qj["UpperLeg.L"][0])
    rs = max(ua_q / ua_mh, 0.85 * g)
    rh = max(ul_q / ul_mh, 0.92 * g)
    da = rs * ua_mh - ua_q
    dh = rh * ul_mh - ul_q
    tors = [("Hips", "spine05", "spine04", "Hips", "Abdomen", rh),
            ("Abdomen", "spine04", "spine03", "Abdomen", "Torso", 0.6 * rh + 0.4 * rs),
            ("Torso", "spine03", "spine02", "Torso", "Chest", 0.3 * rh + 0.7 * rs),
            ("Chest", "spine02", "neck01", "Chest", "Neck", rs)]
    # 胴の区間は関節を結ぶ斜めの軸で伸ばすと、腹の骨が後ろへ傾いた体で
    # バスト（腹の骨に乗っている）が奥へ引き込まれ、胸が潰れて腰が前へ残る。
    # 長さは上下の差だけを使い、前後と左右は傾きを混ぜない。
    def _upright(p, q):
        q = np.array(q, np.float64)
        q[0] = p[0]
        q[2] = p[2]
        return q
    clip = (0.60 * g, 1.55 * g)
    for s, a0, b0, qa, qb, lat in tors:
        A, B = J(a0), J(b0)
        a, b = qj[qa], qj[qb]
        M[s] = _seg_mat(A, _upright(A, B), a, _upright(a, b), lat, 0.5 * (lat + g), clip)
    nc = 0.5 * (g + head_xf["sc"])
    M["Neck"] = _seg_mat(J("neck01"), J("head"), qj["Neck"], hx(J("head")), nc, nc)
    # 歩きの途中で止めた骨は、左右の足の Z が 5cm 以上ずれる。
    # 両方のつま先を全足頂点の最小 Z へ向けると、後ろの靴だけが 1m の板になる。
    # ずれたときだけ、腰の下へ揃えて短い靴を前へ出す。揃っている体はそのまま。
    zL = float(qj["Foot.L"][2]); zR = float(qj["Foot.R"][2])
    split = abs(zL - zR) > 0.05
    z_plant = (zL + zR) * 0.5
    def _stand(p):
        p = np.array(p, np.float64)
        if split:
            p[2] = z_plant
        return p
    for side, sg in (("L", -1.0), ("R", 1.0)):
        off_a = np.array([sg * da, 0, 0]); off_h = np.array([sg * dh, 0, 0])
        M["Shoulder." + side] = _seg_mat(J("clavicle." + side), J("upperarm01." + side), qj["Shoulder." + side] + off_a * 0.5, qj["UpperArm." + side] + off_a, rs, rs)
        M["UpperArm." + side] = _seg_mat(J("upperarm01." + side), J("lowerarm01." + side), qj["UpperArm." + side] + off_a, qj["LowerArm." + side] + off_a, g * 0.95, g * 0.95)
        # 手と指はゲームの関節そのものへ置く（武器の握りと拳を一致させる）。腕の外への寄せは前腕で戻す。
        M["LowerArm." + side] = _seg_mat(J("lowerarm01." + side), J("wrist." + side), qj["LowerArm." + side] + off_a, qj["Hand." + side], g * 0.95, g * 0.95)
        names = dict(zip(SEGS, seg_joint_names(qj)))
        G = lambda canon: qj[names[canon + "." + side]]
        kn_mh = {f: J(f"finger{k + 2}-1." + side) for k, f in enumerate(FINGERS)}
        kn_q = {f: G(f + "1") for f in FINGERS}
        lat_mh = kn_mh["Pinky"] - kn_mh["Index"]
        lat_q = kn_q["Pinky"] - kn_q["Index"]
        lat_n = float(np.linalg.norm(lat_mh))
        wl = float(np.linalg.norm(lat_q) / lat_n) if lat_n > 1e-6 else 1.0
        if not np.isfinite(wl) or wl < 0.25 or wl > 2.5:
            wl = 1.0
        M["Hand." + side] = _seg_mat_framed(J("wrist." + side), kn_mh["Middle"], lat_mh,
                                            qj["Hand." + side], kn_q["Middle"], lat_q, wl, g)
        for k, f in enumerate(FINGERS):
            mh = [J(f"finger{k + 2}-{i}." + side) for i in (1, 2, 3)] + [J(f"finger{k + 2}-3." + side, "tail")]
            q = [G(f"{f}{i}") for i in (1, 2, 3)]
            den_f = float(np.linalg.norm(mh[2] - mh[1]))
            ext = (float(np.linalg.norm(mh[3] - mh[2])) / den_f) if den_f > 1e-6 else 1.0
            if not np.isfinite(ext) or ext > 3.0:
                ext = 1.0
            q.append(q[2] + (q[2] - q[1]) * ext)
            for i in range(3):
                M[f"{f}{i + 1}." + side] = _seg_mat_framed(mh[i], mh[i + 1], lat_mh, q[i], q[i + 1], lat_q, wl, g)
        t_mh = [J("finger1-1." + side), J("finger1-2." + side), J("finger1-3." + side, "tail")]
        t2, t3 = qj["Thumb2." + side], qj["Thumb3." + side]
        den_t = float(np.linalg.norm(t_mh[1] - t_mh[0]))
        ext_t = min(1.0, float(np.linalg.norm(t_mh[2] - t_mh[1])) / den_t) if den_t > 1e-6 else 1.0
        if not np.isfinite(ext_t):
            ext_t = 1.0
        t4 = t3 + (t3 - t2) * ext_t
        M["Thumb2." + side] = _seg_mat_framed(t_mh[0], t_mh[1], lat_mh, t2, t3, lat_q, wl, g)
        M["Thumb3." + side] = _seg_mat_framed(t_mh[1], t_mh[2], lat_mh, t3, t4, lat_q, wl, g)
        ank_mh = J("foot." + side)
        toe_mh = J("toe3-1." + side, "tail")
        if split:
            ank = _stand(qj["Foot." + side])
            ank[1] = float(ank_mh[1]) * g
            shoe = float(np.linalg.norm(toe_mh - ank_mh))
            if not np.isfinite(shoe) or shoe < 0.06 or shoe > 0.18:
                shoe = 0.13
            drop = (float(toe_mh[1]) - float(ank_mh[1])) * g
            if not np.isfinite(drop) or drop > 0.02 or drop < -0.06:
                drop = -0.01
            toe = ank + np.array([0.0, drop, -shoe])
            hip = _stand(qj["UpperLeg." + side] + off_h)
            knee = _stand(qj["LowerLeg." + side] + off_h)
        else:
            ank = np.array([qj["Foot." + side][0], ank_mh[1] * g, qj["Foot." + side][2]])
            toe = np.array([qj["Foot." + side][0], toe_mh[1] * g, foot_tip_z])
            hip = qj["UpperLeg." + side] + off_h
            knee = qj["LowerLeg." + side] + off_h
        M["UpperLeg." + side] = _seg_mat(J("upperleg01." + side), J("lowerleg01." + side), hip, knee, g, g)
        # すねの正面は -Z、靴の甲は +Y。方向だけ合わせるとロールが自由で、片足が内側へ寝て見える。
        knee_mh = J("lowerleg01." + side)
        M["LowerLeg." + side] = _seg_mat_framed(knee_mh, ank_mh, knee_mh + np.array([0.0, 0.0, -1.0]),
                                                knee, ank + off_h, knee + np.array([0.0, 0.0, -1.0]), g, g)
        M["Foot." + side] = _seg_mat_framed(ank_mh, toe_mh, ank_mh + np.array([0.0, 1.0, 0.0]),
                                            ank + off_h, toe + off_h, ank + off_h + np.array([0.0, 1.0, 0.0]), g, g)
    return np.array([M[s] for s in SEGS])


def qjoints(rec):
    ibm = np.asarray(rec["ibm"], np.float64).reshape(-1, 16)
    qj = {}
    for i, n in enumerate(rec["names"]):
        t = np.linalg.inv(ibm[i].reshape(4, 4).T)[:3, 3].copy()
        t[2] *= -1
        qj[n] = t
    pos = rec["pos"].astype(np.float64).copy(); pos[:, 2] *= -1
    dom = rec["jn"][np.arange(len(pos)), np.argmax(rec["w8"], 1)]
    fi = [rec["names"].index(n) for n in ("Foot.L", "Foot.R") if n in rec["names"]]
    fm = np.isin(dom, fi)
    tip = float(pos[fm, 2].min()) if fm.any() else qj["Foot.L"][2] - 0.12
    return qj, tip


def apply_segments(Ms, pos, nrm, sj, sw):
    hp = np.concatenate([pos, np.ones((len(pos), 1))], 1)
    P = np.zeros_like(pos); N = np.zeros_like(nrm)
    for k in range(4):
        Mk = Ms[sj[:, k]]
        P += np.einsum("nij,nj->ni", Mk[:, :3, :], hp) * sw[:, k, None]
        N += np.einsum("nij,nj->ni", Mk[:, :3, :3], nrm) * sw[:, k, None]
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    return P, N


_BASE = None


def base_cached():
    global _BASE
    if _BASE is None:
        _BASE = load_base()
    return _BASE


def face_source(female, skin, face=0):
    """for _hi_splice: the head the game shows on every human of this gender"""
    b = base_cached()
    fd = face_def(female, face)
    v = shape(b, female, face)
    f = build_face(b, v, female, skin, fd)
    paint_face(f, female, fd)
    return f


def raster(pos, nrm, col, tri, view="front", W=520, H=520, focus=None, light=(0.3, 0.5, -0.8)):
    """z-buffered flat-ish raster for probes. view: front (looks +Z from -Z), side, back, q34"""
    p = pos.copy()
    n = nrm.copy()
    if view == "side":
        R = np.array([[0, 0, 1], [0, 1, 0], [-1, 0, 0]], np.float64)
    elif view == "back":
        R = np.array([[-1, 0, 0], [0, 1, 0], [0, 0, -1]], np.float64)
    elif view == "q34":
        a = np.radians(35)
        R = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    else:
        R = np.eye(3)
    p = p @ R.T
    n = n @ R.T
    L = np.array(light, np.float64); L /= np.linalg.norm(L)
    sh = 0.35 + 0.65 * np.abs(n @ L)
    if focus is None:
        lo, hi = p.min(0), p.max(0)
    else:
        lo, hi = np.array(focus[0]) @ R.T, np.array(focus[1]) @ R.T
        lo, hi = np.minimum(lo, hi), np.maximum(lo, hi)
    span = max(hi[0] - lo[0], hi[1] - lo[1]) * 1.08
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    X = (p[:, 0] - cx) / span * W + W / 2
    Y = H / 2 - (p[:, 1] - cy) / span * H
    Z = p[:, 2]
    img = np.full((H, W, 3), 48, np.float64)
    zb = np.full((H, W), 1e9)
    rgb = np.clip(col * sh[:, None] * 1.25, 0, 1) * 255
    for a, b, c in tri:
        xs = (X[a], X[b], X[c]); ys = (Y[a], Y[b], Y[c])
        x0, x1 = int(max(0, np.floor(min(xs)))), int(min(W - 1, np.ceil(max(xs))))
        y0, y1 = int(max(0, np.floor(min(ys)))), int(min(H - 1, np.ceil(max(ys))))
        if x1 < x0 or y1 < y0:
            continue
        den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
        l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
        l2 = 1 - l0 - l1
        m = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not m.any():
            continue
        z = l0 * Z[a] + l1 * Z[b] + l2 * Z[c]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= z < sub
        if not m.any():
            continue
        sub[m] = z[m]
        cc = l0[..., None] * rgb[a] + l1[..., None] * rgb[b] + l2[..., None] * rgb[c]
        img[y0:y1 + 1, x0:x1 + 1][m] = cc[m]
    return img.astype(np.uint8)


def save_png(path, img):
    from PIL import Image
    Image.fromarray(img).save(path)


def probe_face():
    for female in (1, 0):
        rows = []
        L = FACES_F if female else FACES_M
        for fi in range(len(L)):
            f = face_source(female, CUTE_SKIN if female else MALE_SKIN, fi)
            P = f["pos"]
            hd = P[f["kind"] != 3]
            lo, hi = hd.min(0), hd.max(0)
            lo2 = lo.copy(); lo2[1] = hi[1] - (hi[1] - lo[1]) * 0.75
            print("face", "F" if female else "M", L[fi]["key"], "v", len(P), "bbox", np.round(lo, 3), np.round(hi, 3))
            imgs = [raster(P, f["nrm"], f["col"], f["tri"], vw, 260, 260, focus=(lo2, hi)) for vw in ("front", "q34")]
            rows.append(np.concatenate(imgs, 1))
        grid = np.concatenate([np.concatenate(rows[i:i + 2], 1) for i in range(0, len(rows), 2)], 0)
        save_png(os.path.join(HERE, "hi_glb", "faces_%s.png" % ("f" if female else "m")), grid)


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "face":
        probe_face()
        raise SystemExit
    b = load_base()
    print("verts", len(b.v), "faces", len(b.f), "groups", len(b.gid))
    cnt = {}
    for gi in b.fg:
        cnt[b.groups[gi]] = cnt.get(b.groups[gi], 0) + 1
    for k in ("body", "helper-tights", "helper-skirt", "helper-l-eye", "helper-r-eye", "helper-l-eyelashes-1", "helper-hair"):
        print(" ", k, cnt.get(k))
    for female in (1, 0):
        v = shape(b, female)
        body = sorted({i for f, g in zip(b.f, b.fg) if b.groups[g] == "body" for i in f})
        bv = v[body]
        print("female" if female else "male", "body bbox", np.round(bv.min(0), 3), np.round(bv.max(0), 3))
        sk = load_skel(v)
        for n in ("root", "spine05", "spine04", "spine03", "spine02", "spine01", "neck01", "head", "clavicle.L", "shoulder01.L", "upperarm01.L",
                  "lowerarm01.L", "wrist.L", "finger3-3.L", "pelvis.L", "upperleg01.L", "lowerleg01.L", "foot.L", "toe3-1.L", "breast.L"):
            if n in sk:
                print("   %-14s h %s t %s" % (n, np.round(sk[n]["head"], 3), np.round(sk[n]["tail"], 3)))
    W = load_weights()
    print("weight bones", len(W))
