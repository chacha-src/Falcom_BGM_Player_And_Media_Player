# -*- coding: utf-8 -*-
"""CC0 Quaternius glb → res/tactics/tac_mon.bin（骨格＋待機・歩行クリップ）"""
import os, sys, struct
from io import BytesIO
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tac_convert import Gltf, prim_arrays, parents_of, node_global, node_trs, node_local, trs_mat, pack_name, sample_channel, qmul, mat_to_quat

SRC = os.path.join(os.path.dirname(__file__), "mon_glb")
OUT = os.path.join(os.path.dirname(__file__), "..", "res", "tactics", "tac_mon.bin")
INC = os.path.join(os.path.dirname(__file__), "mon_species.inc")
FPS = 24.0
MAX_F = 32


def albedo_rgb(g, mat, uv, nvert):
    """ベースカラーテクスチャを頂点色へ焼く。無ければ材質色。"""
    pbr = (mat or {}).get("pbrMetallicRoughness") or {}
    factor = np.array((pbr.get("baseColorFactor") or [1, 1, 1, 1])[:3], np.float32)
    if float(np.max(factor)) < 1e-4:
        factor = np.array([0.75, 0.75, 0.75], np.float32)
    tex = pbr.get("baseColorTexture")
    if not tex or uv is None or len(uv) != nvert:
        return np.tile(factor, (nvert, 1))
    try:
        src = g.j["textures"][tex["index"]].get("source")
        raw = g.image_bytes(src)
        im = Image.open(BytesIO(raw)).convert("RGBA")
        img = np.asarray(im).astype(np.float32) / 255.0
        h, w = img.shape[0], img.shape[1]
        u = np.mod(uv[:, 0].astype(np.float32), 1.0)
        v = np.mod(1.0 - uv[:, 1].astype(np.float32), 1.0)
        x = np.clip((u * (w - 1)).astype(np.int32), 0, w - 1)
        y = np.clip((v * (h - 1)).astype(np.int32), 0, h - 1)
        return np.clip(img[y, x, :3] * factor, 0, 1)
    except Exception:
        return np.tile(factor, (nvert, 1))


def disp_name(fn):
    stem = os.path.splitext(fn)[0]
    for pre in ("gob_", "q_", "ani_", "din_", "eas_", "fis_", "cut_", "zhum_"):
        if stem.startswith(pre):
            stem = stem[len(pre):]
            break
    return stem.replace("_", " ")


def is_fly(fn, disp):
    if fn.startswith("cut_") or fn.startswith("fis_"):
        return 1
    for k in ("Dragon", "Wyvern", "Wasp", "Ghost", "Birb", "Pigeon", "Hywirl",
              "Eagle", "Hawk", "Owl", "Bat", "Bee", "Moth", "Butterfly", "Dragonfly",
              "Manta", "Gryphon", "Avian", "Crow", "Raven", "Gull", "Swallow", "Finch"):
        if k in disp:
            return 1
    return 0


def pick_anim(anims, keys):
    best = None
    for a in anims:
        short = (a.get("name") or "").split("|")[-1]
        low = short.lower()
        for k in keys:
            if k in low:
                return a
        if best is None:
            best = None
    return best


def sample_clip(g, anim, joints, node_to_j, hips, nJ):
    rot, tra = {}, {}
    dur = 0.2
    for ch in anim["channels"]:
        tn = ch["target"].get("node")
        if tn not in node_to_j:
            continue
        s = anim["samplers"][ch["sampler"]]
        times = g.acc(s["input"]).reshape(-1)
        dur = max(dur, float(times[-1]))
        path = ch["target"]["path"]
        if path == "rotation":
            rot[node_to_j[tn]] = s
        elif path == "translation" and node_to_j[tn] == hips:
            tra[hips] = s
    dur = min(dur, (MAX_F - 1) / FPS)
    nf = max(2, int(round(dur * FPS)) + 1)
    nf = min(nf, MAX_F)
    q = np.zeros((nf, nJ, 4), np.float32)
    ht = np.zeros((nf, 3), np.float32)
    j = g.j
    for f in range(nf):
        t = f / FPS
        for ji, n in enumerate(joints):
            if ji in rot:
                q[f, ji] = sample_channel(g, rot[ji], t).astype(np.float32)
            else:
                q[f, ji] = np.array(node_trs(j["nodes"][n])[1], np.float32)
        if hips in tra:
            ht[f] = sample_channel(g, tra[hips], t).astype(np.float32)
        else:
            ht[f] = np.array(node_trs(j["nodes"][joints[hips]])[0], np.float32)
    ht[:, 0] = ht[0, 0]
    ht[:, 2] = ht[0, 2]
    return nf, q, ht


def rigid_one(path):
    g = Gltf(path)
    j = g.j
    par = parents_of(j)
    P, N, C, I = [], [], [], []
    base = 0
    for ni, n in enumerate(j["nodes"]):
        if "mesh" not in n:
            continue
        mesh = j["meshes"][n["mesh"]]
        skin = j["skins"][n["skin"]] if "skin" in n else None
        glob = ibm = None
        if skin:
            joints = skin["joints"]
            glob = [node_global(j, par, ji) for ji in joints]
            ibm = g.acc(skin["inverseBindMatrices"]).reshape(-1, 4, 4)
        xf = None if skin else node_global(j, par, ni)
        for pr in mesh["primitives"]:
            if pr.get("mode", 4) != 4:
                continue
            pos, nrm, uv, idx, jn, wt = prim_arrays(g, pr)
            if skin and jn is not None:
                out = np.zeros_like(pos)
                nout = np.zeros_like(nrm)
                for ji in range(len(glob)):
                    M = (glob[ji] @ ibm[ji].T).astype(np.float64)
                    R = M[:3, :3]
                    for k in range(4):
                        w = wt[:, k]
                        sel = (jn[:, k] == ji) & (w > 1e-4)
                        if not np.any(sel):
                            continue
                        h = np.concatenate([pos[sel], np.ones((int(sel.sum()), 1), np.float32)], 1)
                        out[sel] += (h @ M.T)[:, :3].astype(np.float32) * w[sel, None]
                        nout[sel] += (nrm[sel] @ R.T).astype(np.float32) * w[sel, None]
                pos, nrm = out, nout
            elif xf is not None:
                h = np.concatenate([pos, np.ones((len(pos), 1), np.float32)], 1)
                pos = (h @ xf.T)[:, :3].astype(np.float32)
                R = np.linalg.inv(xf[:3, :3]).T
                nrm = (nrm @ R.T).astype(np.float32)
            mat = j["materials"][pr.get("material", 0)] if j.get("materials") else {}
            col = mat.get("pbrMetallicRoughness", {}).get("baseColorFactor", [0.75, 0.75, 0.75, 1])
            rgb = np.array(col[:3], np.float32)
            P.append(pos.astype(np.float32))
            N.append(nrm.astype(np.float32))
            C.append(np.tile(rgb, (len(pos), 1)))
            I.append(idx.astype(np.uint32) + base)
            base += len(pos)
    if not P:
        return None
    pos = np.concatenate(P)
    nrm = np.concatenate(N)
    col = np.concatenate(C)
    idx = np.concatenate(I)
    ymin = float(pos[:, 1].min())
    pos = pos.copy()
    pos[:, 1] -= ymin
    h = float(pos[:, 1].max())
    if h < 1e-4:
        return None
    pos *= np.float32(1.7 / h)
    nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-8)
    nJ = 1
    q = np.zeros((2, 1, 4), np.float32)
    q[:, :, 3] = 1
    ht = np.zeros((2, 3), np.float32)
    jn = np.zeros((len(pos), 4), np.uint8)
    w8 = np.zeros((len(pos), 4), np.uint8)
    w8[:, 0] = 255
    ibm = np.eye(4, dtype=np.float32).reshape(1, 16)
    top = np.eye(4)
    rest = [([0, 0, 0], [0, 0, 0, 1], [1, 1, 1])]
    return dict(names=["root"], parents=[-1], hips=0, rest=rest, top=top, ibm=ibm, clips=[("Idle", 2, q, ht)],
                pos=pos.astype(np.float32), nrm=nrm.astype(np.float32), col=col.astype(np.float32),
                idx=idx.astype(np.uint32), jn=jn, w8=w8)


def qinv(q):
    return np.array([-q[0], -q[1], -q[2], q[3]], np.float32)


def qnorm(q):
    q = np.array(q, np.float32)
    n = float(np.linalg.norm(q))
    if n < 1e-8:
        return np.array([0, 0, 0, 1], np.float32)
    return q / n


def q_from_to(a, b):
    a = np.array(a, np.float64)
    b = np.array(b, np.float64)
    a = a / max(np.linalg.norm(a), 1e-8)
    b = b / max(np.linalg.norm(b), 1e-8)
    c = np.cross(a, b)
    d = float(np.dot(a, b))
    if d > 0.9999:
        return np.array([0, 0, 0, 1], np.float64)
    if d < -0.9999:
        axis = np.cross(a, [1, 0, 0])
        if np.linalg.norm(axis) < 1e-4:
            axis = np.cross(a, [0, 0, 1])
        axis = axis / np.linalg.norm(axis)
        return np.array([axis[0], axis[1], axis[2], 0], np.float64)
    return qnorm(np.array([c[0], c[1], c[2], 1.0 + d], np.float64))


def ortho3(R):
    u, s, vt = np.linalg.svd(np.array(R, np.float64)[:3, :3])
    out = u @ vt
    if np.linalg.det(out) < 0:
        u = u.copy()
        u[:, -1] *= -1
        out = u @ vt
    return out


def chain_globals(top, locals_m, parents):
    nJ = len(locals_m)
    G = [None] * nJ
    pending = list(range(nJ))
    for _guard in range(nJ + 2):
        if not pending:
            break
        nxt = []
        for ji in pending:
            p = parents[ji]
            if p < 0:
                G[ji] = top @ locals_m[ji]
            elif G[p] is not None:
                G[ji] = G[p] @ locals_m[ji]
            else:
                nxt.append(ji)
        pending = nxt
    for ji in pending:
        G[ji] = top @ locals_m[ji]
    return G


def skin_cloud(pos, nrm, jn, wt, skinM):
    nJ = len(skinM)
    acc = np.zeros((len(pos), 3), np.float64)
    nn = np.zeros((len(pos), 3), np.float64)
    hh = np.concatenate([pos, np.ones((len(pos), 1), np.float64)], 1)
    for k in range(4):
        ji = jn[:, k]
        w = wt[:, k]
        for u in np.unique(ji):
            if u < 0 or u >= nJ:
                continue
            m = ji == u
            if not np.any(m) or float(np.max(w[m])) <= 0:
                continue
            acc[m] += (hh[m] @ skinM[u].T)[:, :3] * w[m, None]
            nn[m] += (nrm[m] @ skinM[u][:3, :3].T) * w[m, None]
    ln = np.linalg.norm(nn, axis=1, keepdims=True)
    nn = nn / np.maximum(ln, 1e-8)
    return acc, nn


def refine_eyes(pos, nrm, col, mask):
    """小さい目プリミティブだけ、表面へ出して瞳と白目に塗り分ける。"""
    sel = np.flatnonzero(np.asarray(mask).astype(bool))
    if len(sel) < 6:
        return
    xs = pos[sel, 0]
    span = float(xs.max() - xs.min())
    if span > 0.05 and len(sel) >= 12:
        med = float(np.median(xs))
        groups = (sel[xs <= med], sel[xs > med])
    else:
        groups = (sel,)
    for id2 in groups:
        if len(id2) < 4:
            continue
        c = pos[id2].mean(0)
        d = np.linalg.norm(pos[id2] - c, axis=1)
        if float(d.max()) > 0.08:
            continue
        direc = 1.0 if float(nrm[id2, 2].mean()) >= 0.0 else -1.0
        pos[id2] = pos[id2] + nrm[id2] * np.float32(0.006 * direc)
        thr = np.quantile(d, 0.45)
        pupil = d <= thr
        col[id2[pupil]] = (0.02, 0.015, 0.012)
        col[id2[~pupil]] = (0.16, 0.11, 0.08)


def smooth_human_normals(pos, nrm, idx):
    """位置は動かさない。同じ場所の法線を混ぜて面のカクつきだけ落とす。"""
    pos = np.asarray(pos, np.float64)
    nrm = np.asarray(nrm, np.float64)
    tri = np.asarray(idx, np.int64).reshape(-1, 3)
    q = np.round(pos * 4500.0).astype(np.int64)
    _, inv = np.unique(q, axis=0, return_inverse=True)
    acc = np.zeros((int(_.shape[0]), 3), np.float64)
    np.add.at(acc, inv, nrm)
    acc = acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-8)
    nrm = acc[inv]
    p0, p1, p2 = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    fn = np.cross(p1 - p0, p2 - p0)
    facc = np.zeros_like(pos)
    np.add.at(facc, tri[:, 0], fn)
    np.add.at(facc, tri[:, 1], fn)
    np.add.at(facc, tri[:, 2], fn)
    facc = facc / np.maximum(np.linalg.norm(facc, axis=1, keepdims=True), 1e-8)
    nrm = nrm * 0.4 + facc * 0.6
    nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-8)
    return nrm.astype(np.float32)


def bake_human(names, parents, rest, top, ibm, pos, nrm, jn, wt, hair_mask=None, eye_mask=None):
    """Tポーズのモジュラー人体を、腕を下ろした等身の姿勢に焼き、ローダのZ反転後にその姿勢が出るように返す。"""
    nJ = len(names)
    locals_m = [trs_mat(np.array(t, np.float64), np.array(r, np.float64), np.array(s, np.float64)) for t, r, s in rest]
    name_i = {nm: i for i, nm in enumerate(names)}
    for side in ("L", "R"):
        arm = "UpperArm." + side
        fore = "LowerArm." + side
        if arm not in name_i or fore not in name_i:
            continue
        G = chain_globals(top, locals_m, parents)
        ia, ic = name_i[arm], name_i[fore]
        direction = G[ic][:3, 3] - G[ia][:3, 3]
        outward = 0.32 if direction[0] >= 0 else -0.32
        delta = q_from_to(direction, np.array([outward, -1.0, 0.0]))
        R = trs_mat([0, 0, 0], delta, [1, 1, 1])[:3, :3]
        new_world = R @ ortho3(G[ia][:3, :3])
        ip = parents[ia]
        parent_R = np.eye(3) if ip < 0 else ortho3(G[ip][:3, :3])
        local_R = np.linalg.inv(parent_R) @ new_world
        t = locals_m[ia][:3, 3].copy()
        sc = np.maximum(np.linalg.norm(locals_m[ia][:3, :3], axis=0), 1e-8)
        locals_m[ia] = trs_mat(t, mat_to_quat(ortho3(local_R)), sc)
    for side in ("L", "R"):
        fore = "LowerArm." + side
        hand = "Hand." + side
        if fore not in name_i or hand not in name_i:
            continue
        G = chain_globals(top, locals_m, parents)
        ia, ic = name_i[fore], name_i[hand]
        direction = G[ic][:3, 3] - G[ia][:3, 3]
        outward = 0.18 if direction[0] >= 0 else -0.18
        delta = q_from_to(direction, np.array([outward, -1.0, 0.02]))
        R = trs_mat([0, 0, 0], delta, [1, 1, 1])[:3, :3]
        new_world = R @ ortho3(G[ia][:3, :3])
        ip = parents[ia]
        parent_R = np.eye(3) if ip < 0 else ortho3(G[ip][:3, :3])
        local_R = np.linalg.inv(parent_R) @ new_world
        t = locals_m[ia][:3, 3].copy()
        sc = np.maximum(np.linalg.norm(locals_m[ia][:3, :3], axis=0), 1e-8)
        locals_m[ia] = trs_mat(t, mat_to_quat(ortho3(local_R)), sc)
    # 指を握る。開いたまま武器を添えると掴んで見えない。
    def rxm(a):
        c, s = np.cos(a), np.sin(a)
        return np.array([[1.0, 0.0, 0.0], [0.0, c, -s], [0.0, s, c]], np.float64)
    def rym(a):
        c, s = np.cos(a), np.sin(a)
        return np.array([[c, 0.0, s], [0.0, 1.0, 0.0], [-s, 0.0, c]], np.float64)
    for side in ("L", "R"):
        curls = (
            ("Index1.", -1.05, 0.0), ("Index2.", -1.15, 0.0), ("Index3.", -0.75, 0.0),
            ("Middle1.", -1.10, 0.0), ("Middle2.", -1.20, 0.0), ("Middle3.", -0.80, 0.0),
            ("Ring1.", -1.15, 0.0), ("Ring2.", -1.20, 0.0), ("Ring3.", -0.75, 0.0),
            ("Pinky1.", -1.20, 0.0), ("Pinky2.", -1.15, 0.0), ("Pinky3.", -0.70, 0.0),
            ("Thumb1.", -0.45, 0.55), ("Thumb2.", -0.70, 0.0), ("Thumb3.", -0.40, 0.0),
        )
        for pre, ax, ay in curls:
            nm = pre + side
            if nm not in name_i:
                continue
            ii = name_i[nm]
            R = ortho3(locals_m[ii][:3, :3])
            scn = np.maximum(np.linalg.norm(locals_m[ii][:3, :3], axis=0), 1e-8)
            extra = rxm(ax)
            if abs(ay) > 1e-6:
                extra = rym(ay) @ extra
            t = locals_m[ii][:3, 3].copy()
            locals_m[ii] = trs_mat(t, mat_to_quat(ortho3(R @ extra)), scn)
    # モジュラー体は歩きの途中で止まっている。足とつま先（PT）の親は Root で、膝を回しても足首は動かない。
    # 靴の区間だけ揃えても、骨は歩いたままなので体型の拡縮（骨のローカル XZ）がふくらはぎをせん断する。
    # スキンの前に、両股を同じ奥行きへ寄せ、膝と足首をその真下へ下ろし、骨の Y を脚の向きに合わせる。
    # 左右の足首の Z が近い体（既に立っている）は触らない。
    def _set_world(ji, world_pos=None, rot=None):
        if ji is None:
            return
        Gnow = chain_globals(top, locals_m, parents)
        desired = Gnow[ji].copy()
        if world_pos is not None:
            desired[:3, 3] = np.asarray(world_pos, np.float64)
        if rot is not None:
            desired[:3, :3] = rot
        ip = parents[ji]
        parent = top if ip < 0 else Gnow[ip]
        locals_m[ji] = np.linalg.inv(parent) @ desired
    def _aim_y(ji, y_dir):
        if ji is None:
            return
        y_dir = np.asarray(y_dir, np.float64)
        if float(np.linalg.norm(y_dir)) < 1e-5:
            return
        Gnow = chain_globals(top, locals_m, parents)
        cur = Gnow[ji][:3, :3]
        cy = cur[:, 1]
        if float(np.linalg.norm(cy)) < 1e-5:
            return
        delta = q_from_to(cy, y_dir)
        R = trs_mat([0, 0, 0], delta, [1, 1, 1])[:3, :3]
        _set_world(ji, rot=R @ cur)
    if "Foot.L" in name_i and "Foot.R" in name_i:
        G0 = chain_globals(top, locals_m, parents)
        zL = float(G0[name_i["Foot.L"]][2, 3])
        zR = float(G0[name_i["Foot.R"]][2, 3])
        if abs(zL - zR) > 0.05:
            uls = [name_i.get("UpperLeg." + s) for s in ("L", "R")]
            if uls[0] is not None and uls[1] is not None:
                zMid = (float(G0[uls[0]][2, 3]) + float(G0[uls[1]][2, 3])) * 0.5
                for ul in uls:
                    p = G0[ul][:3, 3]
                    _set_world(ul, np.array([p[0], p[1], zMid]))
            for side in ("L", "R"):
                ul = name_i.get("UpperLeg." + side)
                ll = name_i.get("LowerLeg." + side)
                if ul is None or ll is None:
                    continue
                Gn = chain_globals(top, locals_m, parents)
                hip = Gn[ul][:3, 3]
                knee = Gn[ll][:3, 3]
                thigh = float(np.linalg.norm(knee - hip))
                _set_world(ll, hip + np.array([0.0, -thigh, 0.0]))
            for side in ("L", "R"):
                ll = name_i.get("LowerLeg." + side)
                ft = name_i.get("Foot." + side)
                if ll is None or ft is None:
                    continue
                Gn = chain_globals(top, locals_m, parents)
                knee = Gn[ll][:3, 3]
                ank = Gn[ft][:3, 3].copy()
                shin = float(np.linalg.norm(ank - knee))
                new_ank = knee + np.array([0.0, -shin, 0.0])
                delta = new_ank - ank
                _set_world(ft, new_ank)
                # 同じ側のつま先だけ。反対の足は兄弟でも動かさない。
                suf = "." + side
                for ji, nm in enumerate(names):
                    if ji == ft or not str(nm).endswith(suf):
                        continue
                    if str(nm).startswith("UpperLeg") or str(nm).startswith("LowerLeg"):
                        continue
                    p = Gn[ji][:3, 3]
                    if float(np.linalg.norm(p - ank)) < 0.45:
                        _set_world(ji, p + delta)
            for side in ("L", "R"):
                ul = name_i.get("UpperLeg." + side)
                ll = name_i.get("LowerLeg." + side)
                ft = name_i.get("Foot." + side)
                Gn = chain_globals(top, locals_m, parents)
                # 親を回すと子の位置がずれる。膝と足首は置いた場所へ戻す。足の向きはそのまま。
                if ul is not None and ll is not None:
                    knee = Gn[ll][:3, 3].copy()
                    _aim_y(ul, knee - Gn[ul][:3, 3])
                    _set_world(ll, knee)
                if ll is not None and ft is not None:
                    Gn = chain_globals(top, locals_m, parents)
                    ank = Gn[ft][:3, 3].copy()
                    _aim_y(ll, ank - Gn[ll][:3, 3])
                    _set_world(ft, ank)
    G = chain_globals(top, locals_m, parents)
    skinM = [G[ji] @ ibm[ji] for ji in range(nJ)]
    acc, nn = skin_cloud(pos, nrm, jn, wt, skinM)
    hair = np.zeros(len(acc), dtype=bool)
    if hair_mask is not None:
        hm = np.asarray(hair_mask).astype(bool)
        if hm.shape[0] == len(acc):
            hair = hm
    eye = np.zeros(len(acc), dtype=bool)
    if eye_mask is not None:
        em = np.asarray(eye_mask).astype(bool)
        if em.shape[0] == len(acc):
            eye = em
    head_i = name_i.get("Head")
    origin = G[head_i][:3, 3] if head_i is not None else acc.mean(0)
    # 髪と帽子の法線は後ろを向く。目の位置、なければ頭の肌だけで前を決める。
    if int(eye.sum()) >= 6:
        face = acc[eye].mean(0) - origin
    else:
        band = acc[:, 1] > np.quantile(acc[:, 1], 0.72)
        horiz = nn.copy()
        horiz[:, 1] = 0
        face_sel = band & (~hair) & (np.linalg.norm(horiz, axis=1) > 0.25)
        if int(face_sel.sum()) < 30:
            face = np.array([0.0, 0.0, -1.0])
        else:
            face = nn[face_sel].mean(0)
    face[1] = 0
    flen = float(np.linalg.norm(face))
    face = face / flen if flen > 1e-6 else np.array([0.0, 0.0, -1.0])
    yaw = trs_mat([0, 0, 0], q_from_to(face, np.array([0.0, 0.0, -1.0])), [1, 1, 1])[:3, :3]
    acc = (yaw @ acc.T).T
    nn = (yaw @ nn.T).T
    joints_p = np.array([(yaw @ G[ji][:3, 3]) for ji in range(nJ)], np.float64)
    joints_R = [yaw @ ortho3(G[ji][:3, :3]) for ji in range(nJ)]
    if int(eye.sum()) >= 6 and float(acc[eye, 2].mean()) > 0:
        flip = np.diag([-1.0, 1.0, -1.0])
        acc = (flip @ acc.T).T
        nn = (flip @ nn.T).T
        joints_p = (flip @ joints_p.T).T
        joints_R = [flip @ R for R in joints_R]
    dom = jn[np.arange(len(jn)), np.argmax(wt, axis=1)]
    foot_ids = [name_i[nm] for nm in name_i if nm.startswith("Foot") or nm.startswith("LowerLeg")]
    foot_v = np.isin(dom, foot_ids) if foot_ids else np.zeros(len(acc), dtype=bool)
    if int(foot_v.sum()) > 30:
        ground = float(np.quantile(acc[foot_v, 1], 0.05))
    else:
        ground = float(acc[:, 1].min())
    acc[:, 1] -= ground
    joints_p[:, 1] -= ground
    cx = float((acc[:, 0].min() + acc[:, 0].max()) * 0.5)
    cz = float((acc[:, 2].min() + acc[:, 2].max()) * 0.5)
    acc[:, 0] -= cx
    acc[:, 2] -= cz
    joints_p[:, 0] -= cx
    joints_p[:, 2] -= cz
    h = max(1e-3, float(acc[:, 1].max()))
    sc = 1.7 / h
    acc *= sc
    joints_p *= sc
    nn = nn / np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-8)
    names = list(names)
    parents = list(parents)
    jn = np.array(jn, np.int32, copy=True)
    wt = np.array(wt, np.float64, copy=True)
    nJ = len(names)
    def _ortho_axes(y_axis, z_hint):
        y = np.asarray(y_axis, np.float64)
        y = y / max(float(np.linalg.norm(y)), 1e-8)
        z = np.asarray(z_hint, np.float64)
        z = z - y * float(np.dot(y, z))
        if float(np.linalg.norm(z)) < 1e-6:
            z = np.array([0.0, 0.0, 1.0])
            z = z - y * float(np.dot(y, z))
        z = z / max(float(np.linalg.norm(z)), 1e-8)
        x = np.cross(y, z)
        x = x / max(float(np.linalg.norm(x)), 1e-8)
        z = np.cross(x, y)
        return np.stack([x, y, z], 1)
    # 武器は手の +Y を指先、+Z を甲として置く。ソースの骨軸のままでは本が腰に浮く。
    for side, sgn in (("L", -1.0), ("R", 1.0)):
        hand = name_i.get("Hand." + side)
        if hand is None:
            continue
        mid = name_i.get("Middle1." + side)
        along = (joints_p[mid] - joints_p[hand]) if mid is not None else np.array([0.0, -1.0, 0.0])
        if float(np.linalg.norm(along)) < 1e-4:
            along = np.array([0.0, -1.0, 0.0])
        outward = np.array([joints_p[hand][0], 0.0, 0.0])
        if abs(float(outward[0])) < 1e-4:
            outward[0] = sgn
        joints_R[hand] = _ortho_axes(along, outward)
    for side in ("L", "R"):
        hand = name_i.get("Hand." + side)
        if hand is None:
            continue
        R = joints_R[hand]
        origin = joints_p[hand]
        y = R[:, 1]
        x = R[:, 0]
        for i, pre in enumerate(("Index", "Middle", "Ring", "Pinky")):
            lat = (i - 1.5) * 0.014
            for k in range(1, 4):
                ji = name_i.get("%s%d.%s" % (pre, k, side))
                if ji is None:
                    continue
                joints_p[ji] = origin + y * (0.018 * k) + x * lat
                joints_R[ji] = R
        for k, lat, fwd in ((2, 0.016, 0.012), (3, 0.022, 0.020)):
            ji = name_i.get("Thumb%d.%s" % (k, side))
            if ji is None:
                continue
            joints_p[ji] = origin + y * fwd + x * lat
            joints_R[ji] = R
    world = []
    for ji in range(nJ):
        M = np.eye(4)
        M[:3, :3] = joints_R[ji]
        M[:3, 3] = joints_p[ji]
        world.append(M)
    file_rest = []
    for ji in range(nJ):
        ip = parents[ji]
        local = np.linalg.inv(world[ip]) @ world[ji] if ip >= 0 else world[ji]
        t = local[:3, 3]
        q = mat_to_quat(ortho3(local[:3, :3]))
        file_rest.append(([float(t[0]), float(t[1]), float(-t[2])], [-float(q[0]), -float(q[1]), float(q[2]), float(q[3])], [1.0, 1.0, 1.0]))
    def flipm(m):
        m = np.array(m, np.float64).copy()
        m[0, 2] *= -1
        m[1, 2] *= -1
        m[3, 2] *= -1
        m[2, 0] *= -1
        m[2, 1] *= -1
        m[2, 3] *= -1
        return m
    ibm_file = np.zeros((nJ, 16), np.float32)
    for ji in range(nJ):
        inv_row = np.linalg.inv(world[ji]).T
        ibm_file[ji] = flipm(inv_row).astype(np.float32).reshape(-1)
    # 待機は呼吸、歩行は手足を逆位相に振る。死亡はルートだけ前に倒す。
    hips = 0
    for i, p in enumerate(parents):
        if p < 0:
            hips = i
            break
    bind_R = [ortho3(world[ji][:3, :3]) for ji in range(nJ)]
    swing_j = {}
    for nm, sign in (("UpperLeg.L", 1.0), ("UpperLeg.R", -1.0), ("UpperArm.L", -1.0), ("UpperArm.R", 1.0)):
        if nm in name_i:
            swing_j[name_i[nm]] = sign

    def rot_axis(axis, angle):
        axis = np.asarray(axis, np.float64)
        nlen = float(np.linalg.norm(axis))
        if nlen < 1e-8 or abs(float(angle)) < 1e-6:
            return np.eye(3)
        x, y, z = axis / nlen
        c, s = np.cos(angle), np.sin(angle)
        C = 1.0 - c
        return np.array([
            [c + x * x * C, x * y * C - z * s, x * z * C + y * s],
            [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
            [z * x * C - y * s, z * y * C + x * s, c + z * z * C],
        ], np.float64)

    def bend(R, axis, angle):
        return rot_axis(np.asarray(R, np.float64).T @ np.asarray(axis, np.float64), angle)

    def sm(t):
        t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
        return t * t * (3.0 - 2.0 * t)

    def env(u, a, b, c, d):
        if u < a or u >= d:
            return 0.0
        if u < b:
            return sm((u - a) / (b - a))
        if u < c:
            return 1.0
        return 1.0 - sm((u - c) / (d - c))

    def swing(u, wind=0.24, hit=0.42):
        """0 から溜め(-1)、打撃(+1)、戻り(0)。打撃は戦闘のヒット時刻に合わせる。"""
        if u < wind:
            return -sm(u / max(1e-6, wind))
        if u < hit:
            return -1.0 + 2.0 * sm((u - wind) / (hit - wind))
        return 1.0 - sm((u - hit) / max(1e-6, 1.0 - hit))

    # 上腕の捻り軸（バインドの肩→肘）。
    arm_dir = {}
    for side in ("L", "R"):
        ua, la = name_i.get("UpperArm." + side), name_i.get("LowerArm." + side)
        if ua is not None and la is not None:
            d = joints_p[la] - joints_p[ua]
            arm_dir[side] = d / max(float(np.linalg.norm(d)), 1e-8)

    def pose_mix(a, b, k):
        return {key: a.get(key, 0.0) * (1.0 - k) + b.get(key, 0.0) * k for key in set(a) | set(b)}

    def keyed(u, keys):
        """keys: [(u, pose dict)] を時間で滑らかに補間する。"""
        if u <= keys[0][0]:
            return dict(keys[0][1])
        for (u0, p0), (u1, p1) in zip(keys, keys[1:]):
            if u <= u1:
                return pose_mix(p0, p1, sm((u - u0) / max(1e-6, u1 - u0)))
        return dict(keys[-1][1])

    def gesture(kind, u):
        """腕は左右とも同じ意味: x=前へ振る, z=外へ開く, t=内へ捻る, y=水平に後ろへ, e=肘, w=手首（刃を起こす）。
        胸の +X はのけぞり、chestY の + は右肩が前。"""
        z = dict(xR=0, xL=0, zR=0, zL=0, tR=0, tL=0, yR=0, yL=0, eR=0, eL=0, wR=0, wL=0,
                 chestX=0, chestY=0, chestZ=0, headX=0, headY=0, hipsY=0, hipY=0, hipZ=0)
        P = lambda **kw: kw
        R0 = P()
        if kind == "idle":
            pass
        elif kind == "glanceL":
            e = env(u, 0.06, 0.28, 0.62, 0.94)
            z.update(headY=0.58 * e, chestY=0.16 * e)
        elif kind == "glanceR":
            e = env(u, 0.06, 0.28, 0.62, 0.94)
            z.update(headY=-0.58 * e, chestY=-0.16 * e)
        elif kind == "lookup":
            e = env(u, 0.08, 0.30, 0.64, 0.94)
            z.update(headX=0.62 * e, chestX=0.18 * e)
        elif kind == "stretch":
            e = env(u, 0.06, 0.30, 0.70, 0.97)
            z.update(xR=2.75 * e, xL=2.75 * e, zR=-0.12 * e, zL=-0.12 * e, eR=0.15 * e, eL=0.15 * e,
                     headX=0.26 * e, chestX=0.07 * e, hipY=0.03 * e)
        elif kind == "shift":
            e = env(u, 0.08, 0.28, 0.66, 0.95)
            z.update(chestZ=0.22 * e, headY=0.16 * e, zR=0.62 * e, xR=-0.12 * e, yR=0.3 * e, tR=1.4 * e, eR=1.6 * e, hipY=-0.012 * e)
        elif kind == "cross":
            e = env(u, 0.08, 0.28, 0.68, 0.96)
            z.update(xR=0.42 * e, xL=0.50 * e, zR=0.22 * e, zL=0.22 * e, tR=1.15 * e, tL=1.05 * e,
                     eR=1.85 * e, eL=1.75 * e, chestX=-0.04 * e)
        elif kind == "slash":
            # 右肩の上へ振りかぶり、左腰へ斜めに斬り下ろす。
            up = P(xR=2.35, zR=0.75, eR=0.95, wR=0.5, chestY=-0.32, hipsY=-0.15, headY=0.2, chestX=0.12, xL=0.35, eL=0.6)
            hit = P(xR=0.95, zR=-0.35, tR=0.25, eR=0.12, wR=-0.35, chestY=0.36, hipsY=0.2, headY=-0.2, chestX=-0.3, hipZ=0.08, xL=-0.25, eL=0.4)
            z.update(keyed(u, [(0.0, R0), (0.24, up), (0.42, hit), (0.62, hit), (1.0, R0)]))
        elif kind == "stab":
            # 肘を引いて腰に溜め、肩から真っ直ぐ突く。
            back = P(xR=-0.45, zR=0.18, eR=1.75, wR=-0.4, chestY=-0.25, hipsY=-0.12, chestX=0.12, xL=0.6, eL=0.9)
            hit = P(xR=1.5, zR=-0.05, eR=0.05, wR=-0.35, chestY=0.28, hipsY=0.15, chestX=-0.35, hipZ=0.12, headX=-0.1, xL=-0.2, eL=0.5)
            z.update(keyed(u, [(0.0, R0), (0.30, back), (0.42, hit), (0.60, hit), (1.0, R0)]))
        elif kind in ("chop", "chop2"):
            # 頭の後ろまで振りかぶって叩きつける。両手なら手を寄せる。
            if kind == "chop2":
                up = P(xR=2.75, xL=2.75, zR=-0.1, zL=-0.1, tR=0.35, tL=0.35, eR=1.15, eL=1.15, wR=0.6, wL=0.6,
                       chestX=0.32, headX=0.15)
                hit = P(xR=0.75, xL=0.75, zR=-0.32, zL=-0.32, tR=0.45, tL=0.45, eR=0.1, eL=0.1, wR=0.55, wL=0.55,
                        chestX=-0.6, headX=-0.2, hipY=-0.03, hipZ=0.06)
            else:
                up = P(xR=2.8, zR=0.2, eR=1.2, wR=0.6, chestX=0.25, headX=0.12, chestY=-0.12, xL=0.4, zL=0.2, eL=0.5)
                hit = P(xR=0.8, zR=-0.1, eR=0.1, wR=0.4, chestX=-0.45, headX=-0.15, chestY=0.09, hipY=-0.02, hipZ=0.05, xL=-0.2, eL=0.3)
            z.update(keyed(u, [(0.0, R0), (0.30, up), (0.44, hit), (0.62, hit), (1.0, R0)]))
        elif kind == "spin":
            if u < 0.12:
                arms, yaw = sm(u / 0.12), 0.0
            elif u < 0.62:
                arms, yaw = 1.0, (u - 0.12) / 0.50 * 6.1
            else:
                t = sm((u - 0.62) / 0.38); arms, yaw = 1.0 - t, 6.1 * (1.0 - t)
            z.update(xR=1.35 * arms, xL=1.35 * arms, zR=0.75 * arms, zL=0.75 * arms, eR=0.2 * arms, eL=0.2 * arms,
                     wR=-0.3 * arms, wL=-0.3 * arms, hipsY=yaw, chestY=yaw * 0.25)
        elif kind == "bow":
            # 弓は右手。右肩を的へ向けて右腕を伸ばし、左手で弦を顎まで引く。
            # 胸の捻りは Chest・Torso・Abdomen・Hips に重なるので合計 約0.93。右腕はその分だけ外へ開くと的を向く。
            draw = P(chestY=0.42, hipsY=0.2, headY=-0.9, chestX=0.04,
                     xR=1.64, zR=0.68, eR=0.04,
                     zL=1.42, yL=0.5, eL=2.9)
            nock = P(chestY=0.3, hipsY=0.12, headY=-0.62, xR=1.25, zR=0.6, eR=0.15, xL=1.0, zL=-0.1, eL=1.3)
            rel = P(chestY=0.42, hipsY=0.2, headY=-0.9, chestX=0.02,
                    xR=1.64, zR=0.68, eR=0.04, zL=1.5, yL=0.95, eL=1.2)
            z.update(keyed(u, [(0.0, R0), (0.10, nock), (0.18, draw), (0.32, draw), (0.38, rel), (0.56, rel), (1.0, R0)]))
        elif kind == "shoot":
            # 弩・投擲は両手で前へ構えて撃つ。
            aim = P(xR=1.35, xL=1.25, zR=0.05, zL=-0.3, tL=0.3, eR=0.45, eL=0.75, headX=-0.08)
            kick = P(xR=1.55, xL=1.4, zR=0.05, zL=-0.3, tL=0.3, eR=0.35, eL=0.65, chestX=0.12, headX=-0.02)
            z.update(keyed(u, [(0.0, R0), (0.18, aim), (0.32, aim), (0.38, kick), (0.50, aim), (1.0, R0)]))
        elif kind == "cast":
            # 胸の前で両手を寄せて詠唱し、前へ突き出して放つ。
            chant = P(xR=0.95, xL=0.95, zR=0.1, zL=0.1, tR=0.7, tL=0.7, eR=1.55, eL=1.55, headX=0.12, chestX=0.06)
            peak = P(xR=1.1, xL=1.1, zR=0.18, zL=0.18, tR=0.6, tL=0.6, eR=1.45, eL=1.45, headX=0.25, chestX=0.18)
            thrust = P(xR=1.55, xL=1.55, zR=0.12, zL=0.12, tR=0.15, tL=0.15, eR=0.05, eL=0.05, wR=0.5, wL=0.5,
                       headX=-0.08, chestX=-0.28, hipZ=0.06)
            z.update(keyed(u, [(0.0, R0), (0.22, chant), (0.40, peak), (0.50, thrust), (0.66, thrust), (1.0, R0)]))
        elif kind == "pray":
            # 胸の前で手を合わせて俯き、両腕を空へ開いて祝福する。
            pray = P(xR=0.5, xL=0.5, zR=0.08, zL=0.08, tR=1.0, tL=1.0, eR=2.05, eL=2.05, headX=-0.42, chestX=-0.1)
            bless = P(xR=2.45, xL=2.45, zR=0.55, zL=0.55, eR=0.2, eL=0.2, headX=0.35, chestX=0.2)
            z.update(keyed(u, [(0.0, R0), (0.18, pray), (0.40, pray), (0.55, bless), (0.70, bless), (1.0, R0)]))
        elif kind == "hit":
            h = sm(u / 0.18) if u < 0.18 else 1.0 - sm((u - 0.18) / 0.82)
            z.update(chestX=0.5 * h, headX=0.55 * h, xR=-0.35 * h, xL=-0.35 * h, zR=0.2 * h, zL=0.2 * h, eR=0.5 * h, eL=0.5 * h, hipY=-0.02 * h)
        return z

    def clip_quats(nf, kind):
        q = np.zeros((nf, nJ, 4), np.float32)
        ht = np.zeros((nf, 3), np.float32)
        base_t = file_rest[hips][0]
        for f in range(nf):
            ph = 2.0 * np.pi * f / max(1, nf - 1)
            s = float(np.sin(ph))
            ang = 0.0
            if kind == "walk":
                ang = s * 0.48
            elif kind == "run":
                ang = s * 0.72
            for ji in range(nJ):
                nm = names[ji]
                extra = np.eye(3)
                R = bind_R[ji]
                if kind in ("walk", "run"):
                    if ji in swing_j:
                        extra = bend(R, (1.0, 0.0, 0.0), ang * swing_j[ji]) @ extra
                    if nm.startswith("LowerLeg"):
                        up = parents[ji]
                        ta = ang * swing_j.get(up, 0.0)
                        extra = bend(R, (1.0, 0.0, 0.0), -(0.18 + max(0.0, ta) * 1.35)) @ extra
                    elif nm.startswith("Foot"):
                        low = parents[ji]
                        up = parents[low] if low >= 0 else -1
                        ta = ang * swing_j.get(up, 0.0)
                        knee = -(0.18 + max(0.0, ta) * 1.35)
                        extra = bend(R, (1.0, 0.0, 0.0), -knee * 0.82) @ extra
                    elif nm == "Hair":
                        extra = bend(R, (1.0, 0.0, 0.0), np.sin(ph * 2.0) * (0.18 if kind == "walk" else 0.26)) @ bend(R, (0.0, 0.0, 1.0), s * 0.07) @ extra
                    elif nm == "Chest":
                        extra = bend(R, (1.0, 0.0, 0.0), -ang * 0.18) @ extra
                    elif nm == "Hips":
                        extra = bend(R, (0.0, 1.0, 0.0), ang * 0.14) @ extra
                elif kind in ("idle", "glanceL", "glanceR", "lookup", "stretch", "shift", "cross", "slash", "stab", "chop", "chop2", "spin", "bow", "shoot", "cast", "pray", "hit"):
                    g = gesture(kind, f / max(1, nf - 1))
                    if kind in ("idle", "glanceL", "glanceR", "lookup", "stretch", "shift", "cross"):
                        br = float(np.sin(ph * (3.0 if kind == "idle" else 1.0)))
                        g = dict(g)
                        g["chestX"] = br * 0.045 + g["chestX"]
                        g["headX"] = br * -0.02 + g["headX"]
                        if kind == "idle":
                            g["xR"] = br * 0.05 + g["xR"]
                            g["xL"] = br * -0.04 + g["xL"]
                        g["hipY"] = br * 0.007 + g["hipY"]
                    side = nm[-1] if nm[-2:] in (".L", ".R") else ""
                    sg = 1.0 if side == "R" else -1.0
                    if side:
                        ax_, az_ = g["x" + side], g["z" + side]
                        # 肩は腕が水平より上へ行った分だけ少し上がる。腕はその残り。
                        sx_ = 0.22 * max(0.0, ax_ - 1.5)
                        sz_ = 0.22 * max(0.0, az_ - 1.2)
                    if nm.startswith("UpperArm.") and side in arm_dir:
                        # 捻り→外へ開く→前へ振る→水平に後ろへ、の順に世界軸で回す。
                        extra = (bend(R, (0.0, 1.0, 0.0), g["y" + side] * -sg)
                                 @ bend(R, (1.0, 0.0, 0.0), ax_ - sx_)
                                 @ bend(R, (0.0, 0.0, 1.0), (az_ - sz_) * sg)
                                 @ bend(R, arm_dir[side], g["t" + side] * -sg) @ extra)
                    elif nm.startswith("LowerArm.") and side:
                        extra = bend(R, (1.0, 0.0, 0.0), max(0.0, g["e" + side])) @ extra
                    elif nm.startswith("Hand.") and side:
                        extra = bend(R, (1.0, 0.0, 0.0), g["w" + side]) @ extra
                    elif nm.startswith("Shoulder.") and side:
                        extra = bend(R, (1.0, 0.0, 0.0), sx_) @ bend(R, (0.0, 0.0, 1.0), sz_ * sg) @ extra
                    elif nm in ("Chest", "Torso"):
                        sc = 1.0 if nm == "Chest" else 0.55
                        extra = bend(R, (0.0, 1.0, 0.0), g["chestY"] * sc) @ bend(R, (0.0, 0.0, 1.0), g["chestZ"] * sc) @ bend(R, (1.0, 0.0, 0.0), g["chestX"] * sc) @ extra
                    elif nm == "Abdomen":
                        extra = bend(R, (1.0, 0.0, 0.0), g["chestX"] * 0.35) @ bend(R, (0.0, 1.0, 0.0), g["hipsY"] * 0.4) @ extra
                    elif nm == "Hips":
                        extra = bend(R, (0.0, 1.0, 0.0), g["hipsY"]) @ extra
                    elif nm == "Head":
                        extra = bend(R, (0.0, 1.0, 0.0), g["headY"]) @ bend(R, (1.0, 0.0, 0.0), g["headX"]) @ extra
                    elif nm == "Hair":
                        extra = bend(R, (1.0, 0.0, 0.0), -g["headX"] * 0.6) @ bend(R, (0.0, 1.0, 0.0), -g["chestY"] * 0.35) @ extra
                base = np.array(file_rest[ji][1], np.float64)
                bq = np.array([-base[0], -base[1], base[2], base[3]], np.float64)
                eq = np.array(mat_to_quat(ortho3(extra)), np.float64)
                nq = qmul(bq, eq)
                nq = nq / max(float(np.linalg.norm(nq)), 1e-8)
                q[f, ji] = [-float(nq[0]), -float(nq[1]), float(nq[2]), float(nq[3])]
            if kind in ("idle", "glanceL", "glanceR", "lookup", "stretch", "shift", "cross", "slash", "stab", "chop", "chop2", "spin", "bow", "shoot", "cast", "pray", "hit"):
                g = gesture(kind, f / max(1, nf - 1))
                if kind in ("idle", "glanceL", "glanceR", "lookup", "stretch", "shift", "cross"):
                    g["hipY"] = g["hipY"] + float(np.sin(ph * (3.0 if kind == "idle" else 1.0))) * 0.007
                ht[f] = [base_t[0], base_t[1] + g["hipY"], base_t[2] + g["hipZ"]]
            elif kind in ("walk", "run"):
                lift = 0.035 if kind == "walk" else 0.055
                ht[f] = [base_t[0], base_t[1] + lift * abs(np.sin(ph * 2.0)), base_t[2]]
            else:
                ht[f] = [base_t[0], base_t[1], base_t[2]]
        return q, ht

    idle_q, idle_h = clip_quats(20, "idle")
    gl_q, gl_h = clip_quats(18, "glanceL")
    gr_q, gr_h = clip_quats(18, "glanceR")
    look_q, look_h = clip_quats(18, "lookup")
    str_q, str_h = clip_quats(20, "stretch")
    sh_q, sh_h = clip_quats(18, "shift")
    cr_q, cr_h = clip_quats(18, "cross")
    walk_q, walk_h = clip_quats(13, "walk")
    run_q, run_h = clip_quats(13, "run")
    slash_q, slash_h = clip_quats(16, "slash")
    stab_q, stab_h = clip_quats(16, "stab")
    chop_q, chop_h = clip_quats(16, "chop")
    chop2_q, chop2_h = clip_quats(16, "chop2")
    spin_q, spin_h = clip_quats(20, "spin")
    bow_q, bow_h = clip_quats(16, "bow")
    shoot_q, shoot_h = clip_quats(16, "shoot")
    cast_q, cast_h = clip_quats(22, "cast")
    pray_q, pray_h = clip_quats(22, "pray")
    hit_q, hit_h = clip_quats(10, "hit")
    death_q, death_h = clip_quats(9, "rest")
    for f in range(9):
        a = 1.15 * f / 8.0
        base = np.array(file_rest[hips][1], np.float64)
        bq = np.array([-base[0], -base[1], base[2], base[3]], np.float64)
        eq = np.array(mat_to_quat(ortho3(bend(bind_R[hips], (1.0, 0.0, 0.0), a))), np.float64)
        nq = qmul(bq, eq)
        nq = nq / max(float(np.linalg.norm(nq)), 1e-8)
        death_q[f, hips] = [-float(nq[0]), -float(nq[1]), float(nq[2]), float(nq[3])]
    clips = [
        ("Idle", 20, idle_q, idle_h, 8.0),
        ("Idle_GlanceL", 18, gl_q, gl_h, 8.0),
        ("Idle_GlanceR", 18, gr_q, gr_h, 8.0),
        ("Idle_Look", 18, look_q, look_h, 8.0),
        ("Idle_Stretch", 20, str_q, str_h, 8.0),
        ("Idle_Shift", 18, sh_q, sh_h, 8.0),
        ("Idle_Cross", 18, cr_q, cr_h, 8.0),
        ("Walking_A", 13, walk_q, walk_h),
        ("Running_A", 13, run_q, run_h),
        ("1H_Melee_Attack_Slice_Diagonal", 16, slash_q, slash_h),
        ("1H_Melee_Attack_Stab", 16, stab_q, stab_h),
        ("1H_Melee_Attack_Chop", 16, chop_q, chop_h),
        ("2H_Melee_Attack_Chop", 16, chop2_q, chop2_h),
        ("2H_Melee_Attack_Spin", 20, spin_q, spin_h),
        ("2H_Ranged_Shoot", 16, bow_q, bow_h),
        ("1H_Ranged_Shoot", 16, shoot_q, shoot_h),
        ("Spellcast_Shoot", 22, cast_q, cast_h, 10.0),
        ("Spellcast_Raise", 22, pray_q, pray_h, 10.0),
        ("Hit_A", 10, hit_q, hit_h),
        ("Death_A", 9, death_q, death_h),
    ]
    top_i = np.eye(4, dtype=np.float32)
    pos_f = acc.astype(np.float32).copy()
    nrm_f = nn.astype(np.float32).copy()
    pos_f[:, 2] *= -1
    nrm_f[:, 2] *= -1
    return dict(names=names, parents=parents, hips=hips, rest=file_rest, top=top_i, ibm=ibm_file, clips=clips,
                pos=pos_f, nrm=nrm_f, jn=jn, wt=wt)


def borrow_human_clips(names, rest):
    """騎士の動きは静止姿勢からの差分だけ載せる。手と指は人体側の姿勢のまま。"""
    src = os.path.join(os.path.dirname(__file__), "_char_probe", "knight.glb")
    if not os.path.exists(src):
        return [], 0
    g = Gltf(src)
    skins = g.j.get("skins") or []
    if not skins:
        return [], 0
    kname = {}
    krest = {}
    for n in skins[0]["joints"]:
        nm = g.j["nodes"][n].get("name") or ""
        kname[nm] = n
        krest[nm] = np.array(node_trs(g.j["nodes"][n])[1], np.float32)
    drive = {
        "Hips", "Abdomen", "Torso", "Chest", "Neck", "Head",
        "Shoulder.L", "UpperArm.L", "LowerArm.L",
        "Shoulder.R", "UpperArm.R", "LowerArm.R",
        "UpperLeg.L", "LowerLeg.L", "Foot.L",
        "UpperLeg.R", "LowerLeg.R", "Foot.R",
    }
    want = [
        ("Idle", None),
        ("Walking_A", "Walking"),
        ("Running_A", "Run"),
        ("1H_Melee_Attack_Chop", "swordAttackJump"),
        ("Death_A", "Death"),
        ("Hit_A", "Roll"),
    ]
    by_short = {}
    for a in g.j.get("animations") or []:
        by_short[(a.get("name") or "").split("|")[-1]] = a
    nJ = len(names)
    hips = 0
    for i, nm in enumerate(names):
        if nm == "Hips":
            hips = i
            break
    rest_t = np.array(rest[hips][0], np.float32)
    rest_q = [np.array(rest[ji][1], np.float32) for ji in range(nJ)]
    out = []
    for cname, srcn in want:
        anim = by_short.get(srcn) if srcn else None
        rot, tra = {}, {}
        dur = 0.2
        if anim is not None:
            for ch in anim["channels"]:
                tn = ch["target"].get("node")
                s = anim["samplers"][ch["sampler"]]
                times = g.acc(s["input"]).reshape(-1)
                dur = max(dur, float(times[-1]))
                path = ch["target"]["path"]
                if path == "rotation":
                    rot[tn] = s
                elif path == "translation":
                    tra[tn] = s
            dur = min(dur, (MAX_F - 1) / FPS)
            nf = max(2, int(round(dur * FPS)) + 1)
            nf = min(nf, MAX_F)
        else:
            nf = 2
        q = np.zeros((nf, nJ, 4), np.float32)
        ht = np.zeros((nf, 3), np.float32)
        hip_node = kname.get("Hips")
        base = None
        if hip_node is not None and hip_node in tra:
            base = sample_channel(g, tra[hip_node], 0.0).astype(np.float32)
        for f in range(nf):
            t = 0.0 if anim is None else f / FPS
            for ji, nm in enumerate(names):
                node = kname.get(nm)
                if anim is not None and nm in drive and node is not None and node in rot and nm in krest:
                    anim_q = sample_channel(g, rot[node], t).astype(np.float32)
                    delta = qmul(qinv(krest[nm]), anim_q)
                    q[f, ji] = qnorm(qmul(rest_q[ji], delta))
                else:
                    q[f, ji] = rest_q[ji]
            ht[f] = rest_t
            if base is not None:
                cur = sample_channel(g, tra[hip_node], t).astype(np.float32)
                ht[f, 1] = rest_t[1] + (cur[1] - base[1]) * np.float32(1.86 / 5.6)
        out.append((cname, nf, q, ht))
    return out, hips


def _attach_head_xf(rec, female):
    """髪とアバターの頭区間を、このメッシュの頭蓋の箱へ合わせる。魔女の頭の変換を流用すると後頭部に垂れる。"""
    import mh_avatar
    pos = rec["pos"].astype(np.float64).copy()
    pos[:, 2] *= -1
    names = rec["names"]
    if "Head" not in names:
        return
    head_i = names.index("Head")
    w8 = np.asarray(rec["w8"])
    dom = np.asarray(rec["jn"])[np.arange(len(pos)), np.argmax(w8, axis=1)]
    hv = dom == head_i
    if int(hv.sum()) < 30:
        return
    box = pos[hv]
    y0, y1 = float(box[:, 1].min()), float(box[:, 1].max())
    skull = hv & (pos[:, 1] >= y0 + 0.30 * (y1 - y0))
    if int(skull.sum()) < 20:
        skull = hv
    box_lo = pos[skull].min(0)
    box_hi = pos[skull].max(0)
    skin = mh_avatar.CUTE_SKIN if female else mh_avatar.MALE_SKIN
    f = mh_avatar.face_source(1 if female else 0, skin)
    acc = np.asarray(f["pos"], np.float64)
    kind = np.asarray(f["kind"])
    face_pts = acc[kind > 0]
    if len(face_pts) < 8:
        return
    origin = face_pts.mean(0)
    eye = acc[kind == 2]
    fwd = (eye.mean(0) if len(eye) else face_pts[np.argmax(face_pts[:, 2])]) - origin
    fwd[1] = 0
    fl = float(np.linalg.norm(fwd))
    fwd = fwd / fl if fl > 1e-8 else np.array([0.0, 0.0, -1.0])
    yaw = trs_mat([0, 0, 0], q_from_to(fwd, np.array([0.0, 0.0, -1.0])), [1, 1, 1])[:3, :3]
    acc = (yaw @ acc.T).T
    sk = (kind != 3) & (kind != 4)
    if int(sk.sum()) < 8:
        sk = np.ones(len(acc), dtype=bool)
    lo = acc[sk].min(0)
    hi = acc[sk].max(0)
    sc = float(box_hi[1] - box_lo[1]) / max(float(hi[1] - lo[1]), 1e-4)
    # 高さだけだと男性の頭蓋より髪が細く、頭頂が禿げる。幅も足りるようにする。
    sw = float(box_hi[0] - box_lo[0]) / max(float(hi[0] - lo[0]), 1e-4)
    if sw > sc:
        sc = sw
    mid = (lo + hi) * 0.5
    acc = (acc - mid) * sc
    tgt = (box_lo + box_hi) * 0.5
    off = np.array([float(tgt[0]), float(box_hi[1]) - float(acc[sk, 1].max()), float(tgt[2])])
    rec["head_xf"] = dict(yaw=yaw, mid=mid, sc=float(sc), off=off)


def load_one(path):
    g = Gltf(path)
    j = g.j
    skins = j.get("skins") or []
    anims = j.get("animations") or []
    if not skins:
        return None
    skin = skins[0]
    joints = skin["joints"]
    nJ = len(joints)
    if nJ < 1 or nJ > 64:
        return None
    par = parents_of(j)
    jset = {n: i for i, n in enumerate(joints)}
    parents = []
    for n in joints:
        p = par[n]
        parents.append(jset.get(p, -1))
    names = []
    for n in joints:
        nm = j["nodes"][n].get("name") or "j"
        names.append(nm[:23])
    hips = 0
    for i, p in enumerate(parents):
        if p < 0:
            hips = i
            break
    rootJ = joints[hips]
    top = np.eye(4)
    p = par[rootJ]
    chain = []
    while p >= 0:
        chain.append(p)
        p = par[p]
    for p in reversed(chain):
        top = top @ node_local(j["nodes"][p])
    rest = [node_trs(j["nodes"][n]) for n in joints]
    ibm_raw = g.acc(skin["inverseBindMatrices"]).reshape(-1, 16).astype(np.float32)
    ibm = np.array([m.reshape(4, 4).T for m in ibm_raw])
    P, N, C, I, J, W, Hm, Em = [], [], [], [], [], [], [], []
    base = 0
    ref_m = None
    for ni, n in enumerate(j["nodes"]):
        if "mesh" not in n or "skin" not in n:
            continue
        mesh = j["meshes"][n["mesh"]]
        node_m = node_local(n)
        # 背嚢だけアーマチュアと縮尺が違う。体の頂点空間へ戻してからスキンする。
        fix_m = None
        if ref_m is None:
            ref_m = node_m
        else:
            s0 = float(np.linalg.norm(ref_m[:3, 0]))
            s1 = float(np.linalg.norm(node_m[:3, 0]))
            if s0 > 1e-4 and abs(s1 / s0 - 1.0) > 0.25:
                fix_m = np.linalg.inv(ref_m) @ node_m
        for pr in mesh["primitives"]:
            if pr.get("mode", 4) != 4:
                continue
            pos, nrm, uv, idx, jn, wt = prim_arrays(g, pr)
            if jn is None or wt is None:
                continue
            if fix_m is not None:
                h = np.concatenate([pos, np.ones((len(pos), 1), np.float32)], 1)
                pos = (h @ fix_m.T)[:, :3].astype(np.float32)
                n3 = np.linalg.inv(fix_m[:3, :3]).T
                nrm = (nrm @ n3.T).astype(np.float32)
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-8)
            mat = j["materials"][pr.get("material", 0)] if j.get("materials") else {}
            baked = albedo_rgb(g, mat, uv, len(pos))
            rgb = baked[0].copy()
            mat_name = (mat.get("name") or "").lower()
            node_name = (n.get("name") or "").lower()
            is_brow = "eyebrow" in mat_name
            is_eye = (not is_brow) and (mat_name in ("eye", "eyes") or ("head" in node_name and mat_name == "brown" and len(pos) <= 220))
            is_hair = (not is_brow) and ("hair" in mat_name or ("head" in node_name and mat_name == "white" and len(pos) >= 400))
            # モデルの髪は残さない。残すと選択した髪型と二重になる。髪型は頭ボーンに被せる。
            if is_hair and "zhum_" in os.path.basename(path):
                continue
            colv = None
            if "COLOR_0" in pr.get("attributes", {}):
                rawc = g.acc(pr["attributes"]["COLOR_0"]).astype(np.float32)
                if rawc.ndim == 2 and rawc.shape[0] == len(pos) and rawc.shape[1] >= 3:
                    colv = np.clip(rawc[:, :3], 0, 1)
            human_body = "zhum_" in os.path.basename(path)
            if colv is not None and human_body:
                # 焼き込み済みの sRGB。順光の白飛びだけ抑える。
                lit = np.array([1.421, 1.336, 1.219], np.float32)
                C.append(np.clip(colv / lit, 0, 1))
            elif not human_body:
                if colv is not None:
                    painted = np.clip(colv, 0, 1)
                    if float(np.std(painted)) > 0.02:
                        C.append(painted)
                    else:
                        C.append(np.clip(baked * np.maximum(painted, 0.15), 0, 1))
                else:
                    C.append(baked)
            else:
                pbr = mat.get("pbrMetallicRoughness") or {}
                rgb = np.array((pbr.get("baseColorFactor") or [0.75, 0.75, 0.75])[:3], np.float32)
                if float(rgb.max()) < 1e-4:
                    rgb = np.array([0.7, 0.7, 0.7], np.float32)
                if is_eye:
                    rgb = np.array([0.10, 0.07, 0.05], np.float32)
                elif is_brow:
                    rgb = np.array([0.045, 0.032, 0.025], np.float32)
                elif "zhum_" in os.path.basename(path):
                    # glTF の baseColorFactor はリニア。PSM の ToonLight は作成画面の環境光で
                    # 順光の面を約 (1.421, 1.336, 1.219) 倍し、描画先は sRGB ではない。
                    # sRGB 化した素材色をこの倍率で割ると、照らされた面がビューアと同じ色になる。
                    # 割らずに sRGB のまま渡すと肌 (0.73) が 1 を超えて白飛びする。
                    srgb = np.power(np.clip(rgb, 0, 1), np.float32(1.0 / 2.2))
                    lit = np.array([1.421, 1.336, 1.219], np.float32)
                    rgb = np.clip(srgb / lit, 0, 1)
                C.append(np.tile(rgb, (len(pos), 1)))
            P.append(pos.astype(np.float32))
            N.append(nrm.astype(np.float32))
            I.append(idx.astype(np.uint32) + base)
            J.append(jn.astype(np.int32))
            W.append(wt.astype(np.float32))
            Hm.append(np.full(len(pos), 1 if is_hair else 0, np.uint8))
            Em.append(np.full(len(pos), 1 if is_eye else 0, np.uint8))
            base += len(pos)
    if not P:
        return None
    pos = np.concatenate(P)
    nrm = np.concatenate(N)
    col = np.concatenate(C)
    idx = np.concatenate(I)
    jn = np.concatenate(J)
    wt = np.concatenate(W)
    if idx.max() >= len(pos) or len(pos) > 65000:
        return None
    human = "zhum_" in os.path.basename(path)
    if (not human) and len(idx) > 24000:
        idx = idx[:24000 - (24000 % 3)]
    if len(idx) > 60000:
        idx = idx[:60000 - (60000 % 3)]
    wt = np.nan_to_num(wt, nan=0.0, posinf=0.0, neginf=0.0)
    wsum = wt.sum(1, keepdims=True)
    wt = wt / np.maximum(wsum, 1e-8)
    w8 = np.clip(np.round(wt * 255.0), 0, 255).astype(np.int32)
    w8[:, 0] += 255 - w8.sum(1)
    if human:
        hair_mask = np.concatenate(Hm) if Hm else None
        eye_mask = np.concatenate(Em) if Em else None
        baked = bake_human(names, parents, rest, top, ibm, pos, nrm, jn, wt, hair_mask, eye_mask)
        if baked is None:
            return None
        jn = baked["jn"]
        wt = np.nan_to_num(baked["wt"], nan=0.0, posinf=0.0, neginf=0.0)
        pos = baked["pos"]
        nrm = baked["nrm"]
        if eye_mask is not None:
            refine_eyes(pos, nrm, col, eye_mask)
        wt = np.nan_to_num(wt, nan=0.0, posinf=0.0, neginf=0.0)
        wsum = wt.sum(1, keepdims=True)
        wt = wt / np.maximum(wsum, 1e-8)
        w8 = np.clip(np.round(wt * 255.0), 0, 255).astype(np.int32)
        w8[:, 0] += 255 - w8.sum(1)
        nrm = smooth_human_normals(pos, nrm, idx)
        rec = dict(names=baked["names"], parents=baked["parents"], hips=baked["hips"], rest=baked["rest"],
                    top=baked["top"], ibm=baked["ibm"], clips=baked["clips"],
                    pos=pos, nrm=nrm, col=col, idx=idx.astype(np.uint32),
                    jn=jn.astype(np.uint8), w8=w8.astype(np.uint8))
        base = os.path.basename(path)
        # HumF/HumM はスーパーヒーロー体の顔を残す。MakeHuman の頭は載せない。
        new_body = len(base) >= 14 and base.startswith("zhum_Hum") and base[8] in "FM" and base[9].isdigit()
        if base.startswith("zhum_Hum") and not new_body:
            import _hi_splice
            female = ("Witch" in base) or ("Medieval" in base)
            up = _hi_splice.upgrade(rec, female)
            if isinstance(up, dict) and "rec_pos" in up and len(up["rec_pos"]) <= 65000:
                rec["pos"] = up["rec_pos"]
                rec["nrm"] = up["rec_nrm"]
                rec["col"] = up["rec_col"]
                rec["idx"] = up["rec_idx"]
                rec["jn"] = up["rec_jn"]
                rec["w8"] = up["rec_w8"]
                rec["head_xf"] = up["head_xf"]
                rec["skin_col"] = up["skin_col"]
                rec["head_start"] = up["head_start"]
                rec["head_n"] = up["head_n"]
                rec["head_keep"] = up["head_keep"]
                print("hi-head", base, len(rec["pos"]))
        elif new_body and base[9] in "12":
            # モジュラー頭は十数面で、アバターに載せると顔が板になり髪が目を覆う。
            import _hi_splice
            up = _hi_splice.upgrade(rec, base[8] == "F")
            if isinstance(up, dict) and "rec_pos" in up and len(up["rec_pos"]) <= 65000:
                rec["pos"] = up["rec_pos"]
                rec["nrm"] = up["rec_nrm"]
                rec["col"] = up["rec_col"]
                rec["idx"] = up["rec_idx"]
                rec["jn"] = up["rec_jn"]
                rec["w8"] = up["rec_w8"]
                rec["head_xf"] = up["head_xf"]
                print("hi-head", base, len(rec["pos"]))
            else:
                _attach_head_xf(rec, base[8] == "F")
        elif new_body:
            _attach_head_xf(rec, base[8] == "F")
        return rec
    # バインド姿勢の身長を 1.7、足を地面へ。頂点と逆バインドはそのまま、親変換だけ合わせる。
    step = max(1, len(pos) // 800)
    sel = np.arange(0, len(pos), step)
    hh = np.concatenate([pos[sel], np.ones((len(sel), 1), np.float64)], 1)
    acc = np.zeros((len(sel), 3), np.float64)
    locals_m = [trs_mat(np.array(t, np.float64), np.array(r, np.float64), np.array(s, np.float64)) for t, r, s in rest]
    G = [None] * nJ
    pending = list(range(nJ))
    for _guard in range(nJ + 2):
        if not pending:
            break
        nxt = []
        for ji in pending:
            p = parents[ji]
            if p < 0:
                G[ji] = top @ locals_m[ji]
            elif G[p] is not None:
                G[ji] = G[p] @ locals_m[ji]
            else:
                nxt.append(ji)
        pending = nxt
    for ji in pending:
        G[ji] = top @ locals_m[ji]
    skinM = [G[ji] @ ibm[ji] for ji in range(nJ)]
    for k in range(4):
        ji = jn[sel, k]
        w = wt[sel, k]
        for u in np.unique(ji):
            if u < 0 or u >= nJ:
                continue
            m = ji == u
            acc[m] += (hh[m] @ skinM[u].T)[:, :3] * w[m, None]
    ymin = float(acc[:, 1].min())
    ymax = float(acc[:, 1].max())
    h = max(1e-3, ymax - ymin)
    s = 1.7 / h
    S = np.eye(4)
    S[0, 0] = S[1, 1] = S[2, 2] = s
    S[1, 3] = -ymin * s
    top = S @ top
    clips = []
    want = [
        ("Idle", ("idle",), ("hit", "death", "dead", "jump", "attack", "hurt")),
        ("Walking_A", ("walk", "move", "swimming_normal", "flying_idle", "swim"), ("fast", "attack")),
        ("Running_A", ("run", "gallop", "fast_flying", "swimming_fast"), ()),
        ("1H_Melee_Attack_Chop", ("attack", "bite", "punch", "headbutt", "kick", "sting", "pounce"), ("death", "dead")),
        ("Hit_A", ("hit", "hurt"), ("death", "dead")),
        ("Death_A", ("death", "dead"), ()),
    ]
    node_to_j = jset
    for cname, keys, ban in want:
        found = None
        for a in anims:
            short = (a.get("name") or "").split("|")[-1].lower()
            if any(b in short for b in ban):
                continue
            if any(k in short for k in keys):
                found = a
                break
        if found is None and cname == "Walking_A":
            for a in anims:
                short = (a.get("name") or "").split("|")[-1].lower()
                if "run" in short or "gallop" in short or "fly" in short or "swim" in short:
                    found = a
                    break
        if found is None:
            continue
        nf, q, ht = sample_clip(g, found, joints, node_to_j, hips, nJ)
        clips.append((cname, nf, q, ht))
    if not clips and "zhum_" in os.path.basename(path):
        borrowed, hip_i = borrow_human_clips(names, rest)
        if borrowed:
            clips = borrowed
            hips = hip_i
    if not clips:
        return None
    return dict(names=names, parents=parents, hips=hips, rest=rest, top=top, ibm=ibm_raw, clips=clips,
                pos=pos, nrm=nrm, col=col, idx=idx.astype(np.uint32), jn=jn.astype(np.uint8), w8=w8.astype(np.uint8))


def write_one(fo, name, rec):
    fo.write(pack_name(name, 32))
    nJ = len(rec["names"])
    fo.write(struct.pack("<I", nJ))
    for nm, p in zip(rec["names"], rec["parents"]):
        fo.write(pack_name(nm, 24))
        fo.write(struct.pack("<i", p))
    fo.write(struct.pack("<I", len(rec["clips"])))
    for clip in rec["clips"]:
        cname, nf, q, ht = clip[0], clip[1], clip[2], clip[3]
        fps = clip[4] if len(clip) > 4 else FPS
        fo.write(pack_name(cname, 32))
        fo.write(struct.pack("<IfI", nf, fps, rec["hips"]))
        qi = np.clip(np.round(q * 32767.0), -32767, 32767).astype("<i2")
        fo.write(qi.tobytes())
        fo.write(ht.astype("<f4").tobytes())
    for t, r, s in rec["rest"]:
        fo.write(struct.pack("<10f", float(t[0]), float(t[1]), float(t[2]), float(r[0]), float(r[1]), float(r[2]), float(r[3]), float(s[0]), float(s[1]), float(s[2])))
    fo.write(rec["top"].T.astype("<f4").tobytes())
    fo.write(np.asarray(rec["ibm"], np.float32).astype("<f4").tobytes())
    pos, nrm, col, idx = rec["pos"], rec["nrm"], rec["col"], rec["idx"]
    emin = pos.min(0).astype("<f4")
    emax = pos.max(0).astype("<f4")
    mid = ((emin + emax) * np.float32(0.5)).astype(np.float32)
    half = np.maximum((emax - emin) * np.float32(0.5), np.float32(1e-8))
    sp = np.clip(np.round((pos - mid) / half * 32767.0), -32767, 32767).astype("<i2")
    sn = np.clip(np.round(np.clip(nrm, -1, 1) * 32767.0), -32767, 32767).astype("<i2")
    rgb = np.clip(np.round(np.clip(col, 0, 1) * 255.0), 0, 255).astype(np.uint8)
    fo.write(struct.pack("<II", len(pos), len(idx)))
    fo.write(emin.tobytes())
    fo.write(emax.tobytes())
    blob = sp.tobytes() + sn.tobytes() + rgb.tobytes() + rec["jn"].tobytes() + rec["w8"].tobytes()
    fo.write(blob)
    fo.write(b"\0" * ((-len(blob)) & 3))
    fo.write(idx.astype("<u2").tobytes())
    if len(idx) & 1:
        fo.write(b"\0\0")


def main():
    files = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".glb") and "Horse_White" not in f)
    # 追加の人体は末尾に足す。既存モンスターの並び（セーブのモデル番号）をずらさない。
    digit = [f for f in files if len(f) >= 14 and f.startswith("zhum_Hum") and f[8] in "FM" and f[9].isdigit()]
    order = ["zhum_HumF0.glb", "zhum_HumF1.glb", "zhum_HumF2.glb", "zhum_HumF3.glb",
             "zhum_HumM0.glb", "zhum_HumM1.glb", "zhum_HumM2.glb", "zhum_HumM3.glb",
             "zhum_HumF4.glb", "zhum_HumM4.glb"]
    tail = [f for f in order if f in digit] + sorted(f for f in digit if f not in order)
    files = [f for f in files if f not in tail] + tail
    recs, names, srcn = [], [], []
    for fn in files:
        try:
            got = load_one(os.path.join(SRC, fn))
        except Exception as e:
            print("skip", fn, e)
            got = None
        if not got:
            try:
                got = rigid_one(os.path.join(SRC, fn))
            except Exception as e:
                print("rigid", fn, e)
                got = None
        if not got:
            print("empty", fn)
            continue
        recs.append(got)
        names.append(disp_name(fn))
        srcn.append(fn)
        print("ok", disp_name(fn), "j", len(got["names"]), "v", len(got["pos"]), "clips", len(got["clips"]))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as fo:
        fo.write(b"TACK")
        fo.write(struct.pack("<I", len(recs)))
        for name, rec in zip(names, recs):
            write_one(fo, name, rec)
    with open(INC, "w", encoding="utf-8") as fo:
        fo.write("static const int kImportN = %d;\n" % len(names))
        fo.write("static const wchar_t* const kImportName[kImportN] = {\n")
        for n in names:
            fo.write("\tL\"%s\",\n" % n.replace("\\", "\\\\").replace("\"", ""))
        fo.write("};\n")
        fo.write("static const unsigned char kImportFly[kImportN] = {\n")
        for fn, n in zip(srcn, names):
            fo.write("\t%d,\n" % is_fly(fn, n))
        fo.write("};\n")
    print("count", len(names), "bytes", os.path.getsize(OUT))


if __name__ == "__main__":
    main()
