# -*- coding: utf-8 -*-
"""Avatar costumes on the MakeHuman body. Writes res/tactics/tac_ava.bin (TACV).

All meshes are in the canonical MH render space; the game moves them onto each human
model with that model's per-segment matrices, then skins them on the segment's joint.

TACV
  u32 nSeg, name[24] * nSeg               (canonical segment names)
  u32 nModel
  per model: name[32], u32 gender, f32 M[nSeg][12] (row-major 3x4, canonical -> bind),
    name[24] * nSeg (that model's joint for each segment; finger numbering differs by rig)
  per gender (0 male, 1 female): u32 nFace, then MESH (the body), u32 nBodyTri, u32 nExtraTri,
    u16 bodyTri[nBodyTri*3], u16 extraTri[nExtraTri*3]
  u32 nAvatar
  per avatar: name[32], u32 gender, MESH (clothes), u32 nTri, u16 tri[nTri*3],
    u8 bodyKeep[(nBodyTri+7)/8]
MESH: u32 nv, f32 emin[3], f32 emax[3], i16 pos[nv*3], i8 nrm[nv*4], u8 rgba[nv*4] (a: 255=dyeable cloth),
  u8 seg[nv*4], u8 w[nv*4], u32 hasBust, (if hasBust) i16 dmax[nv*3], i16 dmin[nv*3] (metres * 32767 / BSCALE)
"""
import os, sys, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_mon as pm
import mh_avatar as M
from pack_face import HUMANS

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "tactics", "tac_ava.bin")
BSCALE = 0.1


def q16(a, lo, hi):
    mid = (lo + hi) * 0.5
    half = np.maximum((hi - lo) * 0.5, 1e-6)
    return np.clip(np.round((a - mid) / half * 32767), -32767, 32767).astype("<i2")


def write_mesh(fo, pos, nrm, col, cloth, sj, sw, dmax=None, dmin=None):
    nv = len(pos)
    assert nv <= 65535, nv
    lo = pos.min(0); hi = pos.max(0)
    fo.write(struct.pack("<I", nv))
    fo.write(lo.astype("<f4").tobytes()); fo.write(hi.astype("<f4").tobytes())
    fo.write(q16(pos, lo, hi).tobytes())
    n4 = np.zeros((nv, 4), np.int8)
    n4[:, :3] = np.clip(np.round(nrm * 127), -127, 127)
    fo.write(n4.tobytes())
    c4 = np.zeros((nv, 4), np.uint8)
    c4[:, :3] = np.clip(np.round(np.clip(col, 0, 1) * 255), 0, 255)
    c4[:, 3] = np.where(cloth, 255, 0)
    fo.write(c4.tobytes())
    fo.write(np.ascontiguousarray(sj, np.uint8).tobytes())
    w8 = np.round(sw * 255).astype(np.int32)
    w8[:, 0] += 255 - w8.sum(1)
    fo.write(np.clip(w8, 0, 255).astype(np.uint8).tobytes())
    if dmax is None:
        fo.write(struct.pack("<I", 0))
    else:
        fo.write(struct.pack("<I", 1))
        for d in (dmax, dmin):
            fo.write(np.clip(np.round(d / BSCALE * 32767), -32767, 32767).astype("<i2").tobytes())


def complete_pose(rec):
    """指や肩を落とした体でも、衣装の区間が計算できるように関節を補う。"""
    qj, tip = M.qjoints(rec)
    names = list(rec["names"])
    ibm = np.asarray(rec["ibm"], np.float64).reshape(-1, 16)

    def axes(jn):
        i = names.index(jn)
        G = np.linalg.inv(ibm[i].reshape(4, 4).T)
        ax = G[:3, :3].copy()
        ax[2, :] *= -1
        return ax

    if "Neck" not in qj and "Head" in qj and "Chest" in qj:
        qj["Neck"] = qj["Chest"] * 0.35 + qj["Head"] * 0.65
    for side in ("L", "R"):
        if "Shoulder." + side not in qj and "UpperArm." + side in qj and "Chest" in qj:
            qj["Shoulder." + side] = (qj["Chest"] + qj["UpperArm." + side]) * 0.5
        if "Hand." + side not in qj and "LowerArm." + side in qj and "UpperArm." + side in qj:
            d = qj["LowerArm." + side] - qj["UpperArm." + side]
            qj["Hand." + side] = qj["LowerArm." + side] + d * 0.55
        if "Hand." + side not in qj:
            continue
        ax = axes("Hand." + side)
        along = ax[:, 1].copy()
        lat = ax[:, 0].copy()
        if np.linalg.norm(along) < 1e-6:
            along = np.array([0.0, -1.0, 0.0])
        along /= np.linalg.norm(along)
        lat /= max(np.linalg.norm(lat), 1e-6)
        hand = qj["Hand." + side]
        # 付け根の番号は M.seg_joint_names と同じ判定。1 番が無い体（付け根が 2 番）へ偽の 1 番を足すと、そちらへ指が付く。
        kb = 2 if ("Index2." + side in qj and ("Index1." + side not in qj or float(np.linalg.norm(qj["Index1." + side] - hand))
                                              <= 0.5 * float(np.linalg.norm(qj["Index2." + side] - qj["Index1." + side])))) else 1
        for i, name in enumerate(("Index", "Middle", "Ring", "Pinky")):
            basep = hand + along * 0.02 + lat * ((i - 1.5) * 0.012)
            for k in range(kb, kb + 3):
                key = "%s%d.%s" % (name, k, side)
                if key not in qj:
                    qj[key] = basep + along * (0.022 * (k - kb + 1))
        for k in (2, 3):
            key = "Thumb%d.%s" % (k, side)
            if key not in qj:
                qj[key] = hand + lat * (0.02 * (k - 1)) + along * 0.015
    return qj, tip


def main():
    gbs = [M.build_body(False), M.build_body(True)]
    with open(OUT, "wb") as fo:
        fo.write(b"TACV")
        fo.write(struct.pack("<I", len(M.SEGS)))
        for s in M.SEGS:
            fo.write(pm.pack_name(s, 24))
        news = ["zhum_Hum%s%d.glb" % (g, i) for g in ("F", "M") for i in range(7)]
        models = list(HUMANS) + news
        donors = {}
        fo.write(struct.pack("<I", len(models)))
        for fn in models:
            r = pm.load_one(os.path.join(pm.SRC, fn))
            female = fn.startswith("zhum_HumF") or ("Witch" in fn) or ("Medieval" in fn)
            qj, tip = complete_pose(r)
            hx = r.get("head_xf")
            if hx is None:
                donor_fn = "zhum_HumWitch.glb" if female else "zhum_HumKing.glb"
                if donor_fn not in donors:
                    donors[donor_fn] = pm.load_one(os.path.join(pm.SRC, donor_fn))
                d = donors[donor_fn]
                dq, _ = M.qjoints(d)
                src = d["head_xf"]
                delta = qj["Head"] - dq["Head"]
                hx = dict(yaw=np.array(src["yaw"], np.float64), mid=np.array(src["mid"], np.float64),
                          sc=float(src["sc"]), off=np.array(src["off"], np.float64) + delta)
            Ms = M.model_segments(gbs[1 if female else 0], qj, hx, tip)
            fo.write(pm.pack_name(pm.disp_name(fn), 32))
            fo.write(struct.pack("<I", 1 if female else 0))
            fo.write(Ms[:, :3, :].astype("<f4").tobytes())
            real = set(r["names"])
            for jn in M.seg_joint_names(qj):
                use = jn if jn in real else ""
                if not use:
                    side = jn[-1] if jn.endswith(".L") or jn.endswith(".R") else ""
                    hand = "Hand." + side
                    if side and any(jn.startswith(p) for p in ("Index", "Middle", "Ring", "Pinky", "Thumb")) and hand in real:
                        use = hand
                    elif jn.startswith("Shoulder") and ("UpperArm." + side) in real:
                        use = "UpperArm." + side
                    elif jn == "Neck" and "Head" in real:
                        use = "Head"
                    else:
                        use = "Hips" if "Hips" in real else jn
                fo.write(pm.pack_name(use, 24))
            print("model", pm.disp_name(fn))
        for gb in gbs:
            sj, sw = M.seg_weights(gb.WB, gb.names)
            fo.write(struct.pack("<I", gb.nface))
            write_mesh(fo, gb.pos, gb.nrm, gb.col, np.zeros(len(gb.pos), bool), sj, sw,
                       getattr(gb, "dmax", None), getattr(gb, "dmin", None))
            fo.write(struct.pack("<II", len(gb.body_tri), len(gb.extra_tri)))
            fo.write(gb.body_tri.astype("<u2").tobytes())
            fo.write(gb.extra_tri.astype("<u2").tobytes())
            print("body", "F" if gb.female else "M", "v", len(gb.pos), "tri", len(gb.body_tri), "+", len(gb.extra_tri))
        L = [(0, k, p) for k, p in M.AVATARS_M] + [(1, k, p) for k, p in M.AVATARS_F]
        fo.write(struct.pack("<I", len(L)))
        for g, key, parts in L:
            gb = gbs[g]
            c = M.build_costume(gb, parts)
            assert len(gb.pos) + len(c["pos"]) <= 65535, key
            sj, sw = M.seg_weights(c["W"], gb.names)
            fo.write(pm.pack_name(key, 32))
            fo.write(struct.pack("<I", g))
            write_mesh(fo, c["pos"], c["nrm"], c["col"], np.ones(len(c["pos"]), bool), sj, sw, c.get("dmax"), c.get("dmin"))
            fo.write(struct.pack("<I", len(c["tri"])))
            fo.write(c["tri"].astype("<u2").tobytes())
            fo.write(np.packbits(c["body_keep"].astype(np.uint8), bitorder="little").tobytes())
            print("avatar", "F" if g else "M", key, "v", len(c["pos"]), "tri", len(c["tri"]))
    print("bytes", os.path.getsize(OUT))


if __name__ == "__main__":
    main()
