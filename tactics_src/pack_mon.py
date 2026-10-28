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


UAL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_dl", "ual")
UAL_FILES = ("UAL1_Standard.glb", "UAL2_Standard.glb")
# ゲームの骨 → Quaternius Universal Animation Library（CC0、UE マネキンの骨名）。
# 指は体ごとの番号で retarget_human が足す。髪や装身具の骨は一番近い親に付いて動く。
UAL_BONE = {"Hips": "pelvis", "Abdomen": "spine_01", "Torso": "spine_02", "Chest": "spine_03", "Neck": "neck_01", "Head": "Head"}
# 骨の向きを決める子。無ければ次の候補、どれも無ければ親と同じ合わせ方を使う。
UAL_AIM = {"Hips": ("Abdomen", "Torso"), "Abdomen": ("Torso", "Chest"), "Torso": ("Chest", "Neck"), "Chest": ("Neck", "Head"), "Neck": ("Head",)}
for _s, _u in (("L", "l"), ("R", "r")):
    UAL_BONE.update({"Shoulder." + _s: "clavicle_" + _u, "UpperArm." + _s: "upperarm_" + _u, "LowerArm." + _s: "lowerarm_" + _u,
                     "Hand." + _s: "hand_" + _u, "UpperLeg." + _s: "thigh_" + _u, "LowerLeg." + _s: "calf_" + _u,
                     "Foot." + _s: "foot_" + _u, "PT." + _s: "ball_" + _u})
    UAL_AIM.update({"Shoulder." + _s: ("UpperArm." + _s,), "UpperArm." + _s: ("LowerArm." + _s,), "LowerArm." + _s: ("Hand." + _s,),
                    "Hand." + _s: ("Middle1." + _s, "Index1." + _s), "UpperLeg." + _s: ("LowerLeg." + _s,),
                    "LowerLeg." + _s: ("Foot." + _s,), "Foot." + _s: ("PT." + _s,)})

# 戦闘は長さの割合で動く。構え（ClipWindup）と命中（物理 0.42）・発射（射撃 0.315、魔法 0.375）を合わせて時間を伸縮する。
# 区間: (クリップ, 開始秒, 終了秒[, 左右反転, つなぎ秒])。終了がクリップより長いと最後の姿勢で止まる。
# キー: (割合, 秒) か (割合, ("rdy"|"hit", 探す開始秒, 終了秒))。rdy は武器手が最も遅い瞬間、hit は最も速い瞬間。
# fem: 女性の体で、腕を体へ寄せ足幅を狭め、歩きの腰を大きく振る。glance: 待機に首の振り向きを重ねる。
UAL_CLIPS = [
    ("Idle", [("Idle_Loop", 0, 2.5)], 2.5, 1, None, dict(fem="idle")),
    ("Idle_GlanceL", [("Idle_Loop", 0, 2.5)], 2.5, 1, None, dict(fem="idle", glance=1)),
    ("Idle_GlanceR", [("Idle_Loop", 0, 2.5)], 2.5, 1, None, dict(fem="idle", glance=-1)),
    ("Idle_Look", [("Idle_No_Loop", 0, 2.5)], 2.5, 1, None, dict(fem="idle")),
    ("Idle_Stretch", [("Idle_Talking_Loop", 0, 2.933)], 2.933, 1, None, dict(fem="idle")),
    ("Idle_Shift", [("Idle_Loop", 1.25, 2.5, 1, 0), ("Idle_Loop", 0, 1.25, 1, 0)], 2.5, 1, None, dict(fem="idle")),
    ("Idle_Cross", [("Idle_FoldArms_Loop", 0, 2.5)], 2.5, 1, None, dict(fem="idle")),
    ("Walking_A", [("Walk_Loop", 0, 1.333)], 1.0, 1, None, dict(fem="walk")),
    ("Running_A", [("Jog_Fwd_Loop", 0, 0.933)], 0.75, 1, None, dict(fem="run")),
    ("1H_Melee_Attack_Slice_Diagonal", [("Sword_Regular_A", 0, 0.433), ("Sword_Regular_A_Rec", 0, 0.967, 0, 0.05)], 1.0, 0,
     [(0.22, ("rdy", 0.0, 0.2)), (0.42, ("hit", 0.12, 0.36))], {}),
    ("1H_Melee_Attack_Stab", [("Idle_Loop", 0, 0.05), ("Sword_Regular_B", 0, 0.533, 0, 0.22), ("Sword_Regular_B_Rec", 0, 1.033, 0, 0.05)], 1.05, 0,
     [(0.22, ("rdy", 0.1, 0.3)), (0.42, ("hit", 0.2, 0.5))], {}),
    ("1H_Melee_Attack_Chop", [("Idle_Loop", 0, 0.05), ("Sword_Attack", 0, 1.25, 0, 0.2), ("Idle_Loop", 0, 0.4, 0, 0.35)], 1.0, 0,
     [(0.30, ("rdy", 0.15, 0.4)), (0.42, ("hit", 0.3, 0.55))], {}),
    ("2H_Melee_Attack_Chop", [("Idle_Loop", 0, 0.05), ("Sword_Heavy_Combo", 2.0, 3.4, 0, 0.35), ("Idle_Loop", 0, 0.5, 0, 0.4)], 1.1, 0,
     [(0.30, ("rdy", 0.3, 0.7)), (0.42, ("hit", 0.45, 0.95))], {}),
    ("2H_Melee_Attack_Spin", [("Idle_Loop", 0, 0.05), ("Sword_Regular_C", 0, 1.75, 0, 0.25)], 1.2, 0,
     [(0.16, ("rdy", 0.1, 0.5)), (0.42, ("hit", 0.45, 0.95))], {}),
    ("2H_Ranged_Shoot", [("Idle_Loop", 0, 0.05), ("Pistol_Aim_Neutral", 0, 0.4, 0, 0.3), ("Pistol_Shoot", 0, 0.633, 0, 0.04),
                         ("Idle_Loop", 0, 0.5, 0, 0.35)], 1.0, 0, [(0.26, 0.42), (0.315, ("hit", 0.44, 0.56))], {}),
    ("1H_Ranged_Shoot", [("Spell_Simple_Enter", 0, 0.533, 1), ("Spell_Simple_Shoot", 0, 0.5, 1, 0.04), ("Spell_Simple_Exit", 0, 0.433, 1, 0.04)], 1.0, 0,
     [(0.26, 0.5), (0.315, 0.6)], {}),
    ("Spellcast_Shoot", [("Spell_Simple_Enter", 0, 0.533), ("Spell_Simple_Shoot", 0, 0.5, 0, 0.04), ("Spell_Simple_Exit", 0, 0.433, 0, 0.04)], 1.3, 0,
     [(0.36, 0.53), (0.375, 0.6)], dict(hand="L")),
    ("Spellcast_Raise", [("Idle_Loop", 0, 0.05), ("Pistol_Aim_Up", 0, 0.7, 0, 0.4), ("Idle_Loop", 0, 0.5, 0, 0.45)], 1.5, 0,
     [(0.32, 0.45), (0.375, 0.55)], {}),
    ("Spellcast_Long", [("Spell_Simple_Enter", 0, 0.533), ("Spell_Simple_Idle_Loop", 0, 2.1, 0, 0.04), ("Spell_Simple_Shoot", 0, 0.5, 0, 0.04),
                        ("Spell_Simple_Exit", 0, 0.433, 0, 0.04)], 2.2, 0, [(0.36, 2.55), (0.375, 2.7)], dict(hand="L")),
    ("Hit_A", [("Hit_Chest", 0, 0.333), ("Idle_Loop", 0, 0.3, 0, 0.25)], 0.5, 0, None, dict(fem="idle")),
    ("Hit_B", [("Hit_Head", 0, 0.433), ("Hit_Chest", 0, 0.333, 0, 0.08), ("Idle_Loop", 0, 0.35, 0, 0.3)], 0.75, 0, None, dict(fem="idle")),
    ("Death_A", [("Death01", 0, 2.4)], 1.6, 0, None, {}),
    ("Cheer", [("Yes", 0, 2.5)], 2.5, 1, None, dict(fem="idle")),
    ("2H_Melee_Idle", [("Sword_Idle", 0, 1.667)], 1.667, 1, None, {}),
    ("Throw", [("OverhandThrow", 0, 1.333)], 1.0, 0, [(0.26, ("rdy", 0.1, 0.32)), (0.315, ("hit", 0.2, 0.45))], {}),
    ("Unarmed_Melee_Attack_Punch_A", [("Idle_Loop", 0, 0.05), ("Punch_Cross", 0, 1.0, 0, 0.2), ("Idle_Loop", 0, 0.3, 0, 0.3)], 0.9, 0,
     [(0.22, ("rdy", 0.05, 0.25)), (0.42, ("hit", 0.15, 0.4))], {}),
    # 蘇生は倒れた姿勢（死亡の終わり）から起き上がる。死亡を逆に再生して腰を落とした所から待機へつなぐ。
    ("Spawn_Ground", [("Death01", 1.7, 0.75), ("Idle_Loop", 0, 0.6, 0, 0.5)], 1.3, 0, None, dict(ref=("Death01", 0.0))),
    # スキル用。物理は直線で突進、威力の大技、盾ごとの体当たり。魔法は属性で投げる・地面へ・指す・触れる。
    ("Skill_Dash", [("Idle_Loop", 0, 0.05), ("Sword_Dash", 0, 1.567, 0, 0.2)], 1.1, 0, [(0.24, ("rdy", 0.1, 0.4)), (0.42, ("hit", 0.25, 0.6))], {}),
    ("Skill_Heavy", [("Sword_Heavy_Combo", 0, 1.3), ("Idle_Loop", 0, 0.45, 0, 0.4)], 1.2, 0, [(0.24, ("rdy", 0.05, 0.3)), (0.42, ("hit", 0.2, 0.55))], {}),
    ("Skill_Bash", [("Idle_Loop", 0, 0.05), ("Shield_Dash", 0, 1.1, 0, 0.2)], 1.0, 0, [(0.24, ("rdy", 0.05, 0.3)), (0.42, ("hit", 0.15, 0.5))], dict(hand="L")),
    ("Cast_Throw", [("OverhandThrow", 0, 1.333)], 1.2, 0, [(0.36, ("rdy", 0.1, 0.32)), (0.375, ("hit", 0.2, 0.45))], {}),
    ("Cast_Ground", [("Farm_PlantSeed", 0, 2.767)], 1.6, 0, [(0.36, ("rdy", 0.8, 1.4)), (0.375, ("hit", 1.0, 1.6))], {}),
    ("Cast_Point", [("Idle_Loop", 0, 0.05), ("Pistol_Aim_Up", 0, 0.45, 0, 0.35), ("Pistol_Shoot", 0, 0.633, 0, 0.15),
                    ("Idle_Loop", 0, 0.5, 0, 0.35)], 1.2, 0, [(0.36, 0.5), (0.375, 0.58)], {}),
    ("Cast_Heal", [("Interact", 0, 2.0)], 1.5, 0, [(0.32, 0.75), (0.375, 0.93)], dict(hand="L")),
    ("Cast_Claw", [("Idle_Loop", 0, 0.05), ("Zombie_Scratch", 0, 0.72, 0, 0.2), ("Idle_Loop", 0, 0.55, 0, 0.45)], 1.2, 0,
     [(0.36, ("rdy", 0.25, 0.55)), (0.375, ("hit", 0.4, 0.7))], {}),
]
# 物を持つ手の指（Grip クリップの 1 コマずつ。順はゲームの GRIP_* − 1）。持たない手はクリップの指（捕った動き）のまま。
# 4 指は付け根・中節・末節の曲げ（度、UAL の拳 Idle_Loop の曲げ軸まわり）。小指側ほど深く、柄を斜めに握り込む。
# 親指は銃把を握る親指（Pistol_Aim_Neutral の右手、左手は鏡）へ節ごとに寄せる割合。
UAL_GRIPS = [
    # 剣・斧・槌・短剣: 径 3cm の柄。人差し指はやや浅く、親指は中指の上へ巻く。
    ("fist", (70, 66, 38), (78, 70, 40), (84, 68, 40), (90, 64, 38), (1.0, 1.0, 1.0)),
    # 杖・槍・大鎌: 径 4cm の柄を軽く。
    ("staff", (58, 56, 32), (65, 60, 34), (72, 58, 34), (78, 55, 32), (0.9, 0.85, 0.8)),
    # 短杖: 人差し指を杖に沿わせ、親指と中指で挟む。薬指と小指は掌へ。
    ("wand", (26, 24, 14), (60, 62, 38), (78, 72, 48), (86, 72, 48), (0.8, 0.75, 0.7)),
    # 弓: 握りへ斜めに。人差し指は軽く、押し手の親指は伸ばし気味。
    ("bow", (60, 58, 36), (75, 66, 40), (82, 64, 40), (88, 60, 38), (0.9, 0.7, 0.55)),
    # 弩: 人差し指を引き金に掛け、残りは銃床を握る。
    ("trigger", (18, 42, 34), (78, 70, 40), (84, 68, 40), (90, 64, 38), (1.0, 1.0, 1.0)),
    # 本: 背を掌に当て、4 指を表紙へ C の字に回し、親指は裏表紙を押さえる。
    ("book", (34, 40, 22), (36, 42, 24), (38, 42, 24), (42, 40, 22), (0.6, 0.35, 0.25)),
    # 盾: 取っ手を固く握る。
    ("shield", (82, 80, 52), (86, 82, 54), (90, 80, 54), (94, 78, 50), (1.1, 1.1, 1.0)),
    # 矢・投げ矢: 親指と人差し指の腹で摘まみ、残りは畳む。
    ("pinch", (44, 52, 30), (70, 72, 48), (80, 72, 48), (88, 70, 46), (0.75, 0.6, 0.45)),
]
_UAL = {}


def _q2m(q):
    q = np.asarray(q, np.float64)
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    R = np.empty(q.shape[:-1] + (3, 3))
    R[..., 0, 0] = 1 - 2 * (y * y + z * z); R[..., 0, 1] = 2 * (x * y - z * w); R[..., 0, 2] = 2 * (x * z + y * w)
    R[..., 1, 0] = 2 * (x * y + z * w); R[..., 1, 1] = 1 - 2 * (x * x + z * z); R[..., 1, 2] = 2 * (y * z - x * w)
    R[..., 2, 0] = 2 * (x * z - y * w); R[..., 2, 1] = 2 * (y * z + x * w); R[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return R


def _m2q(R):
    R = np.asarray(R, np.float64)
    r00, r11, r22 = R[..., 0, 0], R[..., 1, 1], R[..., 2, 2]
    q = np.empty(R.shape[:-2] + (4,))
    q[..., 3] = 0.5 * np.sqrt(np.maximum(0.0, 1.0 + r00 + r11 + r22))
    q[..., 0] = np.copysign(0.5 * np.sqrt(np.maximum(0.0, 1.0 + r00 - r11 - r22)), R[..., 2, 1] - R[..., 1, 2])
    q[..., 1] = np.copysign(0.5 * np.sqrt(np.maximum(0.0, 1.0 - r00 + r11 - r22)), R[..., 0, 2] - R[..., 2, 0])
    q[..., 2] = np.copysign(0.5 * np.sqrt(np.maximum(0.0, 1.0 - r00 - r11 + r22)), R[..., 1, 0] - R[..., 0, 1])
    return q / np.maximum(np.linalg.norm(q, axis=-1, keepdims=True), 1e-12)


def _qlerp(q0, q1, w):
    """符号を揃えた正規化線形補間。w は q の先頭次元に合わせる。"""
    d = np.sum(q0 * q1, axis=-1, keepdims=True)
    q1 = np.where(d < 0, -q1, q1)
    q = q0 * (1.0 - w) + q1 * w
    return q / np.maximum(np.linalg.norm(q, axis=-1, keepdims=True), 1e-12)


def _rot_axis(axis, ang):
    axis = np.asarray(axis, np.float64)
    axis = axis / max(float(np.linalg.norm(axis)), 1e-12)
    h = 0.5 * float(ang)
    return _q2m(np.array([axis[0] * np.sin(h), axis[1] * np.sin(h), axis[2] * np.sin(h), np.cos(h)]))


def ual_motion():
    """UAL の骨と静止姿勢、全クリップのキーを一度だけ読み、クリップごとの世界差分を作る。"""
    if _UAL:
        return _UAL
    names = None
    anims = {}
    for fn in UAL_FILES:
        path = os.path.join(UAL_DIR, fn)
        if not os.path.exists(path):
            raise SystemExit("UAL が無い: %s  (tactics_src/_dl/fetch_itch.py quaternius universal-animation-library / -2)" % path)
        g = Gltf(path)
        j = g.j
        par = parents_of(j)
        joints = j["skins"][0]["joints"]
        nm = [j["nodes"][n].get("name") or "" for n in joints]
        jset = {n: i for i, n in enumerate(joints)}
        rt = np.array([node_trs(j["nodes"][n])[0] for n in joints], np.float64)
        rq = np.array([node_trs(j["nodes"][n])[1] for n in joints], np.float64)
        if names is None:
            names = nm
            parents = [jset.get(par[n], -1) for n in joints]
            rest_t, rest_q = rt, rq
            top = node_global(j, par, par[joints[0]]) if par[joints[0]] >= 0 else np.eye(4)
        elif nm != names or float(np.abs(rt - rest_t).max()) > 1e-3:
            raise SystemExit("UAL の骨格が %s で違う" % fn)
        for a in j.get("animations") or []:
            rot, tra = {}, {}
            dur = 0.0
            for c in a["channels"]:
                ji = jset.get(c["target"].get("node"))
                if ji is None:
                    continue
                s = a["samplers"][c["sampler"]]
                t = g.acc(s["input"]).reshape(-1).astype(np.float64)
                v = g.acc(s["output"]).astype(np.float64)
                if s.get("interpolation") == "CUBICSPLINE":
                    v = v.reshape(len(t), 3, -1)[:, 1, :]
                if c["target"]["path"] == "rotation":
                    rot[ji] = (t, v)
                elif c["target"]["path"] == "translation":
                    tra[ji] = (t, v)
                dur = max(dur, float(t[-1]))
            anims[a.get("name") or ""] = (dur, rot, tra)
    order, seen = [], set()
    while len(order) < len(names):
        for ji, p in enumerate(parents):
            if ji not in seen and (p < 0 or p in seen):
                order.append(ji)
                seen.add(ji)
    sc = float(np.linalg.norm(top[:3, 0]))
    _UAL.update(names=names, idx={n: i for i, n in enumerate(names)}, parents=parents, order=order, anims=anims,
                rest_t=rest_t, rest_q=rest_q, top_R=ortho3(top[:3, :3]), top_t=top[:3, 3].copy(), top_s=sc, cache={})
    R0, P0 = _ual_chain(rest_q[None], rest_t[None])
    _UAL["R0"], _UAL["P0"] = R0[0], P0[0]
    ix = _UAL["idx"]
    # 左右の軸と前。反転は骨盤を通る矢状面で映す。前は足首→つま先の水平成分。
    lr = P0[0, ix["thigh_l"]] - P0[0, ix["thigh_r"]]
    lr[1] = 0
    _UAL["lr"] = lr / np.linalg.norm(lr)
    fwd = (P0[0, ix["ball_l"]] + P0[0, ix["ball_r"]]) - (P0[0, ix["foot_l"]] + P0[0, ix["foot_r"]])
    fwd[1] = 0
    fwd /= np.linalg.norm(fwd)
    # 体は -Z を向いて焼いてある。前をそこへ回す。
    ang = np.arctan2(fwd[0], fwd[2]) - np.arctan2(0.0, -1.0)
    _UAL["Y"] = _rot_axis((0.0, 1.0, 0.0), -ang)
    _UAL["mir"] = [ix.get(n[:-2] + ("_r" if n.endswith("_l") else "_l"), i) if n[-2:] in ("_l", "_r") else i for i, n in enumerate(names)]
    leg = np.linalg.norm(P0[0, ix["thigh_l"]] - P0[0, ix["calf_l"]]) + np.linalg.norm(P0[0, ix["calf_l"]] - P0[0, ix["foot_l"]])
    _UAL["leg"] = float(leg)
    return _UAL


def _ual_chain(q, t):
    """局所 TRS（フレーム, 骨）を世界の回転と位置へ。"""
    src = _UAL
    L = _q2m(q)
    n, nJ = q.shape[0], q.shape[1]
    R = np.empty((n, nJ, 3, 3))
    P = np.empty((n, nJ, 3))
    for ji in src["order"]:
        p = src["parents"][ji]
        if p < 0:
            R[:, ji] = src["top_R"] @ L[:, ji]
            P[:, ji] = src["top_t"] + src["top_s"] * (t[:, ji] @ src["top_R"].T)
        else:
            R[:, ji] = R[:, p] @ L[:, ji]
            P[:, ji] = P[:, p] + src["top_s"] * np.einsum("nij,nj->ni", R[:, p], t[:, ji])
    return R, P


def _ual_raw(anim, ts, mirror):
    """クリップを時刻列で引き、静止姿勢からの世界差分（四元数）と関節位置を返す。"""
    src = _UAL
    if anim not in src["anims"]:
        raise SystemExit("UAL にクリップが無い: " + anim)
    dur, rot, tra = src["anims"][anim]
    ts = np.clip(np.asarray(ts, np.float64), 0.0, dur)
    n = len(ts)
    q = np.repeat(src["rest_q"][None], n, 0)
    t = np.repeat(src["rest_t"][None], n, 0)
    for ji, (tt, v) in rot.items():
        k1 = np.clip(np.searchsorted(tt, ts, side="right"), 0, len(tt) - 1)
        k0 = np.clip(k1 - 1, 0, len(tt) - 1)
        span = np.maximum(tt[k1] - tt[k0], 1e-9)
        w = np.where(k1 == k0, 0.0, np.clip((ts - tt[k0]) / span, 0.0, 1.0))[:, None]
        q[:, ji] = _qlerp(v[k0], v[k1], w)
    for ji, (tt, v) in tra.items():
        t[:, ji] = np.stack([np.interp(ts, tt, v[:, k]) for k in range(3)], 1)
    R, P = _ual_chain(q, t)
    D = R @ np.swapaxes(src["R0"], -1, -2)[None]
    if mirror:
        lr = src["lr"]
        M = np.eye(3) - 2.0 * np.outer(lr, lr)
        mi = src["mir"]
        c = src["P0"][src["idx"]["pelvis"]]
        D = M @ D[:, mi] @ M
        P = c + (P[:, mi] - c) @ M.T
    return _m2q(D), P


def ual_source_clip(spec, nf):
    """区間をつないで時間を伸縮した、体に依らない差分（体の向きへ回し済み）と骨盤の移動。"""
    src = ual_motion()
    key = (spec[0], nf)
    if key in src["cache"]:
        return src["cache"][key]
    name, segs, dur, loop, keys, opt = spec
    segs = [(s[0], float(s[1]), float(s[2]), int(s[3]) if len(s) > 3 else 0, float(s[4]) if len(s) > 4 else 0.18) for s in segs]
    # 終了が開始より前の区間は逆再生。
    starts = np.cumsum([0.0] + [abs(s[2] - s[1]) for s in segs])
    S = float(starts[-1])
    ends = []

    def seg_eval(k, local):
        anim, t0, t1, mir, xf = segs[k]
        qd, P = _ual_raw(anim, t0 + local * (1.0 if t1 >= t0 else -1.0), mir)
        if k > 0 and xf > 1e-6:
            w = np.clip(local / xf, 0.0, 1.0)
            w = (w * w * (3.0 - 2.0 * w))[:, None]
            eq, eP = ends[k - 1]
            qd = _qlerp(np.repeat(eq[None], len(local), 0), qd, w[:, :, None])
            P = eP[None] * (1.0 - w[:, :, None]) + P * w[:, :, None]
        return qd, P

    for k, s in enumerate(segs):
        qd, P = seg_eval(k, np.array([abs(s[2] - s[1])]))
        ends.append((qd[0], P[0]))

    def sample(sv):
        sv = np.clip(np.asarray(sv, np.float64), 0.0, S)
        k = np.clip(np.searchsorted(starts, sv, side="right") - 1, 0, len(segs) - 1)
        qd = np.empty((len(sv), len(src["names"]), 4))
        P = np.empty((len(sv), len(src["names"]), 3))
        for kk in np.unique(k):
            m = k == kk
            a, b = seg_eval(int(kk), sv[m] - starts[kk])
            qd[m], P[m] = a, b
        return qd, P

    hand = src["idx"]["hand_l" if opt.get("hand") == "L" else "hand_r"]
    pel = src["idx"]["pelvis"]
    # 先に命中（最速）を決め、構えはその手前で手が最も遅い瞬間（振りかぶりの頂点）を取る。
    found = {}
    for want in ("hit", "rdy"):
        for i, (u, s) in enumerate(keys or []):
            if not isinstance(s, tuple) or s[0] != want:
                continue
            a, b = float(s[1]), float(s[2])
            if want == "rdy":
                nxt = [found.get(k, ks if not isinstance(ks, tuple) else None) for k, (uu, ks) in enumerate(keys) if k > i]
                nxt = [v for v in nxt if v is not None]
                if nxt:
                    b = min(b, nxt[0] - 0.04)
                a = max(a, 0.04)
                if b <= a + 1.0 / 90.0:
                    a = max(0.0, b - 0.2)
            dense = np.arange(a, b + 1e-9, 1.0 / 90.0)
            _, P = sample(dense)
            rel = P[:, hand] - P[:, pel]
            sp = np.linalg.norm(np.diff(rel, axis=0), axis=1)
            j = int(np.argmin(sp) if want == "rdy" else np.argmax(sp))
            found[i] = float(dense[j] + (dense[j + 1] - dense[j]) * 0.5)
    kp = [(0.0, 0.0)]
    for i, (u, s) in enumerate(keys or []):
        s = found[i] if isinstance(s, tuple) else float(s)
        kp.append((float(u), min(max(s, kp[-1][1] + 1e-3), S)))
    kp.append((1.0, S))
    ku = np.array([k[0] for k in kp])
    ks = np.array([k[1] for k in kp])
    sv = np.interp(np.linspace(0.0, 1.0, nf), ku, ks)
    qd, P = sample(sv)
    Y = src["Y"]
    D = Y[None, None] @ _q2m(qd) @ Y.T[None, None]
    p0 = P[0, pel] if not loop else src["P0"][pel]
    if opt.get("ref"):
        p0 = _ual_raw(opt["ref"][0], [opt["ref"][1]], 0)[1][0, pel]
    off = (P[:, pel] - p0) @ Y.T
    off[:, 1] = (P[:, pel, 1] - src["P0"][pel, 1])
    if loop:
        off[:, 0] = 0.0
        off[:, 2] = 0.0
    src["cache"][key] = (D, off, kp)
    return src["cache"][key]


def retarget_human(names, parents, hips, world, real_p, female):
    """UAL の動きを、この体の骨へ世界空間の差分で移す。

    静止姿勢どうしの骨の向き（子への向き＋横の基準）を合わせる回転 A を骨ごとに作り、
    フレームの姿勢 G = D(t)·A·G0（D は UAL の静止からの世界回転）からローカルを作る。
    """
    src = ual_motion()
    nJ = len(names)
    ni = {n: i for i, n in enumerate(names)}
    six = src["idx"]
    Y = src["Y"]
    P0 = src["P0"] @ Y.T
    # 指の番号は体で違う。手首から次の節までの半分より手首に近い 1 番は中手骨の根（モジュラー体は 5 本とも手首の一点）で、
    # 付け根は 2 番（bake_human と同じ判定）。付け根から 3 節を UAL の 01〜03 へ、中手骨は手の動きで UAL の手→付け根へ向ける。
    # 親指は手首に近い節から 01 へ。
    bone = dict(UAL_BONE)
    aim_of = dict(UAL_AIM)
    knuckle = {}
    for s, u in (("L", "l"), ("R", "r")):
        if "Hand." + s not in ni:
            continue
        hp = real_p[ni["Hand." + s]]
        kb = 2
        if "Index1." + s in ni:
            d1 = float(np.linalg.norm(real_p[ni["Index1." + s]] - hp))
            d2 = float(np.linalg.norm(real_p[ni["Index2." + s]] - real_p[ni["Index1." + s]])) if "Index2." + s in ni else 0.0
            kb = 1 if d1 > 0.5 * d2 else 2
        knuckle[s] = kb
        for f, uf in (("Index", "index"), ("Middle", "middle"), ("Ring", "ring"), ("Pinky", "pinky")):
            for i in range(3):
                n = "%s%d.%s" % (f, kb + i, s)
                bone[n] = "%s_%02d_%s" % (uf, i + 1, u)
                aim_of[n] = ("%s%d.%s" % (f, kb + i + 1, s),) if i < 2 else ()
            if kb == 2 and "%s1.%s" % (f, s) in ni:
                bone["%s1.%s" % (f, s)] = "hand_" + u
                aim_of["%s1.%s" % (f, s)] = ("%s2.%s" % (f, s),)
        aim_of["Hand." + s] = ("Middle%d.%s" % (kb, s), "Index%d.%s" % (kb, s))
        th = [k for k in (1, 2, 3, 4) if "Thumb%d.%s" % (k, s) in ni]
        if th:
            t0 = 1 if float(np.linalg.norm(real_p[ni["Thumb%d.%s" % (th[0], s)]] - hp)) < 0.06 else 2
            th = [k for k in th if t0 + th.index(k) <= 3]
            for m, k in enumerate(th):
                bone["Thumb%d.%s" % (k, s)] = "thumb_%02d_%s" % (t0 + m, u)
                aim_of["Thumb%d.%s" % (k, s)] = ("Thumb%d.%s" % (th[m + 1], s),) if m + 1 < len(th) else ()
    drive = [six.get(bone.get(n, ""), -1) for n in names]
    order, seen = [], set()
    while len(order) < nJ:
        for ji, p in enumerate(parents):
            if ji not in seen and (p < 0 or p in seen):
                order.append(ji)
                seen.add(ji)
    G0 = np.array([ortho3(world[ji][:3, :3]) for ji in range(nJ)])
    fwd = np.array([0.0, 0.0, -1.0])

    def tpos(n):
        return real_p[ni[n]] if n in ni else None

    def spos(n):
        s = bone.get(n)
        return P0[six[s]] if s in six else None

    def lr_of(a, b):
        t0, t1, s0, s1 = tpos(a), tpos(b), spos(a), spos(b)
        if t0 is None or t1 is None or s0 is None or s1 is None:
            return None
        return t0 - t1, s0 - s1

    lr_leg = lr_of("UpperLeg.L", "UpperLeg.R")
    if lr_leg is None:
        raise SystemExit("脚の骨が無い体は UAL を移せない")
    if float(np.dot(lr_leg[0], lr_leg[1])) <= 0.0:
        raise SystemExit("体と UAL で左右が逆: %s" % (lr_leg,))

    def frame(d, l):
        x = d / max(float(np.linalg.norm(d)), 1e-9)
        y = l - x * float(np.dot(x, l))
        if float(np.linalg.norm(y)) < 1e-6:
            y = np.cross(x, (0.0, 0.0, 1.0)) if abs(x[2]) < 0.9 else np.cross(x, (1.0, 0.0, 0.0))
        y = y / max(float(np.linalg.norm(y)), 1e-9)
        return np.stack([x, y, np.cross(x, y)], 1)

    # 腰・背骨・首・鎖骨は体ごとに関節の置き方が違う。子への向きを UAL に揃えると、静止で腹や首が 14〜26 度、
    # 鎖骨が 50 度折れ、肩と腰が人の曲がり方をしない。左右と上だけ合わせ、体の姿勢のまま UAL の回りを乗せる。
    A_body = frame(np.array([0.0, 1.0, 0.0]), lr_leg[1]) @ frame(np.array([0.0, 1.0, 0.0]), lr_leg[0]).T
    # 手と指の横は付け根の人差し指−小指、親指はそれと手の向きが張る掌の法線。
    lat_hand, nrm_hand = {}, {}
    for s, kb in knuckle.items():
        l = lr_of("Index%d.%s" % (kb, s), "Pinky%d.%s" % (kb, s))
        mt, ms = tpos("Middle%d.%s" % (kb, s)), spos("Middle%d.%s" % (kb, s))
        if l is None or mt is None or ms is None:
            continue
        lat_hand[s] = l
        nrm_hand[s] = (np.cross(mt - real_p[ni["Hand." + s]], l[0]), np.cross(ms - P0[six["hand_" + s.lower()]], l[1]))
    A = [None] * nJ
    for ji in order:
        if drive[ji] < 0:
            continue
        n = names[ji]
        if n in ("Hips", "Abdomen", "Torso", "Chest", "Neck") or n.startswith("Shoulder."):
            A[ji] = A_body
            continue
        aim = None
        for c in aim_of.get(n, ()):
            if tpos(c) is not None and spos(c) is not None:
                aim = c
                break
        if aim is None:
            p = parents[ji]
            while p >= 0 and A[p] is None:
                p = parents[p]
            A[ji] = A[p] if p >= 0 else np.eye(3)
            continue
        dt, ds = tpos(aim) - real_p[ji], spos(aim) - P0[drive[ji]]
        if n.startswith("Foot."):
            # モジュラー体の PT は IK の極で、つま先ではない（膝の上へ 0.8m 離れて立つ）。足首から前下へ短く伸びないものは
            # つま先と見なさず、静止の足裏が UAL と同じく平らとして UAL の向きをそのまま使う。
            shin = float(np.linalg.norm(real_p[ni["LowerLeg." + n[-1]]] - real_p[ji])) if ("LowerLeg." + n[-1]) in ni else 0.4
            lt, ls = float(np.linalg.norm(dt)), float(np.linalg.norm(ds))
            if lt > 0.6 * shin or float(np.dot(dt, ds)) < 0.7 * lt * ls:
                dt = ds.copy()
        if n.startswith("Thumb"):
            lat = nrm_hand.get(n[-1], (fwd, fwd))
        elif n.startswith(("Hand.", "Index", "Middle", "Ring", "Pinky")):
            lat = lat_hand.get(n[-1], (fwd, fwd))
        elif n.startswith("Foot."):
            lat = lr_leg
        else:
            lat = (fwd, fwd)
        A[ji] = frame(ds, lat[1]) @ frame(dt, lat[0]).T
    follow = list(range(nJ))
    for ji in range(nJ):
        p = ji
        while p >= 0 and drive[p] < 0:
            p = parents[p]
        follow[ji] = p
    kscale = float(np.linalg.norm(real_p[ni["UpperLeg.L"]] - real_p[ni["LowerLeg.L"]])
                   + np.linalg.norm(real_p[ni["LowerLeg.L"]] - real_p[ni["Foot.L"]])) / src["leg"]

    def subtree(root_name):
        if root_name not in ni:
            return []
        r = ni[root_name]
        out = []
        for ji in range(nJ):
            p = ji
            while p >= 0 and p != r:
                p = parents[p]
            if p == r:
                out.append(ji)
        return out

    def inward(top_name, tip_name):
        """前軸まわりで、先端を体の中心へ寄せる向き（+1/-1）。"""
        a, b = tpos(top_name), tpos(tip_name)
        if a is None or b is None:
            return 0.0
        v = b - a
        best, sg = None, 0.0
        for s in (1.0, -1.0):
            x = abs(float((a + _rot_axis(fwd, 0.05 * s) @ v)[0]))
            if best is None or x < best:
                best, sg = x, s
        return sg

    up = np.array([0.0, 1.0, 0.0])
    left_yaw = 1.0 if float(lr_leg[0][0]) < 0 else -1.0
    neck_set = subtree("Neck") or subtree("Head")
    head_set = subtree("Head")
    chest_set = subtree("Chest")
    hip_i = ni.get("Hips")
    # 前腕のねじれ。UAL は回内を前腕の骨に載せ（剣の振りで 117 度）、手首にも別に 118 度回す。ねじれ骨の無い体では
    # 肘か手首の一点で皮が絞られ、腕が雑巾のようにねじれる。肘と手首のねじれの和を半分ずつに分け直す。
    arm_tw = []
    for s in ("L", "R"):
        iu, i_f, ih = ni.get("UpperArm." + s), ni.get("LowerArm." + s), ni.get("Hand." + s)
        kn = "Middle%d.%s" % (knuckle.get(s, 1), s)
        if iu is None or i_f is None or ih is None or kn not in ni:
            continue
        af = G0[i_f].T @ (real_p[ih] - real_p[i_f])
        ah = G0[ih].T @ (real_p[ni[kn]] - real_p[ih])
        arm_tw.append((iu, i_f, ih, af / np.linalg.norm(af), ah / np.linalg.norm(ah), G0[iu].T @ G0[i_f], G0[i_f].T @ G0[ih],
                       [ji for ji in range(nJ) if follow[ji] == i_f]))
    # 握りの世界差分（体に依らない）。指のローカルを UAL の静止から作り、鎖でつなぐ。
    if "grip" not in src["cache"]:
        nS = len(src["names"])
        par = src["parents"]
        Lr = np.array([(src["top_R"] if par[k] < 0 else src["R0"][par[k]]).T @ src["R0"][k] for k in range(nS)])
        Lsrc = {}
        for anim, t, mir in (("Idle_Loop", 0.0, 0), ("Pistol_Aim_Neutral", 0.2, 0), ("Pistol_Aim_Neutral", 0.2, 1)):
            Rw = _q2m(_ual_raw(anim, [t], mir)[0])[0] @ src["R0"]
            Lsrc[(anim, mir)] = np.array([(src["top_R"] if par[k] < 0 else Rw[par[k]]).T @ Rw[k] for k in range(nS)])
        DG = np.empty((len(UAL_GRIPS), nS, 3, 3))
        for g, (gname, fi, fm, fr, fp, tw) in enumerate(UAL_GRIPS):
            Lg = Lr.copy()
            for u in ("l", "r"):
                for uf, ang in (("index", fi), ("middle", fm), ("ring", fr), ("pinky", fp)):
                    for i in range(3):
                        k = six["%s_%02d_%s" % (uf, i + 1, u)]
                        qa = _m2q(Lr[k].T @ Lsrc[("Idle_Loop", 0)][k])
                        Lg[k] = Lr[k] @ _rot_axis(qa[:3] * (1.0 if qa[3] >= 0 else -1.0), np.radians(ang[i]))
                for i in range(3):
                    k = six["thumb_%02d_%s" % (i + 1, u)]
                    qa = _m2q(Lr[k].T @ Lsrc[("Pistol_Aim_Neutral", 1 if u == "l" else 0)][k])
                    if qa[3] < 0:
                        qa = -qa
                    Lg[k] = Lr[k] @ _rot_axis(qa[:3], 2.0 * np.arctan2(float(np.linalg.norm(qa[:3])), qa[3]) * tw[i])
            Rg = np.empty((nS, 3, 3))
            for k in src["order"]:
                Rg[k] = (src["top_R"] if par[k] < 0 else Rg[par[k]]) @ Lg[k]
            DG[g] = Y[None] @ (Rg @ np.swapaxes(src["R0"], -1, -2)) @ Y.T[None]
        src["cache"]["grip"] = DG
    out = []
    for spec in UAL_CLIPS + [("Grip", None, 1.0, 0, None, {})]:
        name, segs, dur, loop, keys, opt = spec
        if segs is None:
            D = src["cache"]["grip"]
            nf = len(D)
            fps = (nf - 1) / dur
            off = np.zeros((nf, 3))
        else:
            nf = int(min(64, max(2, round(dur * 30.0) + 1)))
            fps = (nf - 1) / dur
            D, off, kp = ual_source_clip(spec, nf)
        u = np.linspace(0.0, 1.0, nf)
        q = np.zeros((nf, nJ, 4), np.float32)
        ht = np.zeros((nf, 3), np.float32)
        fem = opt.get("fem") if female else None
        glance = opt.get("glance", 0)
        for f in range(nf):
            W = np.repeat(np.eye(3)[None], nJ, 0)
            for ji in range(nJ):
                d = follow[ji]
                if d >= 0:
                    W[ji] = D[f, drive[d]] @ A[d]
            if glance:
                e = 0.0 if u[f] < 0.12 or u[f] > 0.9 else (min(1.0, (u[f] - 0.12) / 0.2) if u[f] < 0.62 else max(0.0, 1.0 - (u[f] - 0.62) / 0.28))
                e = e * e * (3.0 - 2.0 * e)
                for ids, a in ((chest_set, 0.10), (neck_set, 0.28), (head_set, 0.30)):
                    Rz = _rot_axis(up, a * e * glance * left_yaw)
                    for ji in ids:
                        W[ji] = Rz @ W[ji]
            if fem:
                arm_a = 0.07 if fem == "idle" else 0.05
                leg_a = {"idle": 0.05, "walk": 0.035, "run": 0.025}[fem]
                for side in ("L", "R"):
                    s = inward("UpperArm." + side, "Hand." + side)
                    if s:
                        Ra = _rot_axis(fwd, arm_a * s)
                        for ji in subtree("UpperArm." + side):
                            W[ji] = Ra @ W[ji]
                    s = inward("UpperLeg." + side, "Foot." + side)
                    if s:
                        Rl = _rot_axis(fwd, leg_a * s)
                        for ji in (ni.get("UpperLeg." + side), ni.get("LowerLeg." + side)):
                            if ji is not None:
                                W[ji] = Rl @ W[ji]
                if fem in ("walk", "run") and hip_i is not None:
                    lr = W[hip_i] @ (lr_leg[0] / np.linalg.norm(lr_leg[0]))
                    roll = float(np.arcsin(np.clip(lr[1], -1.0, 1.0)))
                    yaw = float(np.arctan2(-lr[2], lr[0])) - float(np.arctan2(-lr_leg[0][2], lr_leg[0][0]))
                    # 体の左が -x だと両方の角が ±180 度のそばにあり、差が 360 度近くへ飛ぶ（歩行の半周期だけ腰が 107 度ねじれた）。
                    yaw = (yaw + np.pi) % (2.0 * np.pi) - np.pi
                    W[hip_i] = _rot_axis(up, 0.3 * yaw) @ _rot_axis(fwd, -0.6 * roll * (1.0 if lr_leg[0][0] > 0 else -1.0)) @ W[hip_i]
            for iu, i_f, ih, af, ah, L0e, L0w, fol in arm_tw:
                Gf = W[i_f] @ G0[i_f]
                qe = _m2q(L0e.T @ (W[iu] @ G0[iu]).T @ Gf)
                qw = _m2q(L0w.T @ Gf.T @ (W[ih] @ G0[ih]))
                te = (2.0 * np.arctan2(float(np.dot(qe[:3], af)), qe[3]) + np.pi) % (2.0 * np.pi) - np.pi
                tw = (2.0 * np.arctan2(float(np.dot(qw[:3], ah)), qw[3]) + np.pi) % (2.0 * np.pi) - np.pi
                Rt = _rot_axis(Gf @ af, 0.5 * (te + tw) - te)
                for ji in fol:
                    W[ji] = Rt @ W[ji]
            G = W @ G0
            L = np.empty_like(G)
            for ji in range(nJ):
                p = parents[ji]
                L[ji] = G[ji] if p < 0 else G[p].T @ G[ji]
            lq = _m2q(L)
            q[f, :, 0] = -lq[:, 0]
            q[f, :, 1] = -lq[:, 1]
            q[f, :, 2] = lq[:, 2]
            q[f, :, 3] = lq[:, 3]
            t = world[hips][:3, 3] + kscale * off[f]
            hz = float(np.hypot(t[0] - world[hips][0, 3], t[2] - world[hips][2, 3]))
            if hz > 0.3:
                t[0] = world[hips][0, 3] + (t[0] - world[hips][0, 3]) * 0.3 / hz
                t[2] = world[hips][2, 3] + (t[2] - world[hips][2, 3]) * 0.3 / hz
            ht[f] = [t[0], t[1], -t[2]]
        out.append((name, nf, q, ht, float(fps)))
    return out


def bake_human(names, parents, rest, top, ibm, pos, nrm, jn, wt, hair_mask=None, eye_mask=None, female=False):
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
        # 曲げるのは付け根からの 3 節。モジュラー体の 1 番は手首にある中手骨の根で、曲げると掌が折れる。
        Gk = chain_globals(top, locals_m, parents)
        kb = 2
        if "Index1." + side in name_i and "Hand." + side in name_i:
            d1 = float(np.linalg.norm(Gk[name_i["Index1." + side]][:3, 3] - Gk[name_i["Hand." + side]][:3, 3]))
            d2 = float(np.linalg.norm(Gk[name_i["Index2." + side]][:3, 3] - Gk[name_i["Index1." + side]][:3, 3])) if "Index2." + side in name_i else 0.0
            kb = 1 if d1 > 0.5 * d2 else 2
        curls = []
        for f, a in (("Index", (-1.05, -1.15, -0.75)), ("Middle", (-1.10, -1.20, -0.80)), ("Ring", (-1.15, -1.20, -0.75)), ("Pinky", (-1.20, -1.15, -0.70))):
            curls += [("%s%d." % (f, kb + i), a[i], 0.0) for i in range(3)]
        curls += [("Thumb1.", -0.45, 0.55), ("Thumb2.", -0.70, 0.0), ("Thumb3.", -0.40, 0.0)]
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
    real_p = np.array(joints_p, np.float64).copy()
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
    # 指の骨は焼いた握りの位置のまま、+Y を次の節、+Z を手の甲に向ける。指は節ごとに曲がるので、軸の原点が
    # 本当の関節に無いと曲げた所で指が千切れる。モジュラー体の中手骨の根は 5 本とも手首の一点にある。
    for side in ("L", "R"):
        hand = name_i.get("Hand." + side)
        if hand is None:
            continue
        dors = joints_R[hand][:, 2]
        for pre in ("Index", "Middle", "Ring", "Pinky", "Thumb"):
            for k in range(1, 5):
                ji = name_i.get("%s%d.%s" % (pre, k, side))
                if ji is None:
                    continue
                nx = name_i.get("%s%d.%s" % (pre, k + 1, side))
                along = (joints_p[nx] - joints_p[ji]) if nx is not None else (joints_p[ji] - joints_p[parents[ji]])
                if float(np.linalg.norm(along)) < 1e-4:
                    along = joints_R[hand][:, 1]
                joints_R[ji] = _ortho_axes(along, dors)
    # モジュラー体は足首とつま先が Root 直下、股が Body 直下。
    # バインド位置は立って見えるが、歩行は上腿だけ回すので靴の頂点が地面に残り、すねとの間が板になる。
    # 位置は変えず、白人の体と同じ鎖（腰→腿→膝→足首→つま先）にしてからローカルを作り直す。
    def _reparent(child, parent):
        ci = name_i.get(child)
        pi = name_i.get(parent)
        if ci is None or pi is None or ci == pi:
            return
        p = pi
        guard = 0
        while p >= 0 and guard < nJ:
            if p == ci:
                return
            p = parents[p]
            guard += 1
        parents[ci] = pi
    for side in ("L", "R"):
        _reparent("UpperLeg." + side, "Hips")
        _reparent("Foot." + side, "LowerLeg." + side)
        _reparent("PT." + side, "Foot." + side)
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
    hips = 0
    for i, p in enumerate(parents):
        if p < 0:
            hips = i
            break
    # 動きは UAL のモーションキャプチャを骨ごとに移す。武器や本の握りは手のバインドのまま付いて行く。
    clips = retarget_human(names, parents, hips, world, real_p, female)
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
        bn = os.path.basename(path)
        female = bn.startswith("zhum_HumF") or "Witch" in bn or "Medieval" in bn
        baked = bake_human(names, parents, rest, top, ibm, pos, nrm, jn, wt, hair_mask, eye_mask, female)
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
        elif new_body and base[9] in "123456":
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
             "zhum_HumF4.glb", "zhum_HumM4.glb", "zhum_HumF5.glb", "zhum_HumM5.glb", "zhum_HumF6.glb", "zhum_HumM6.glb"]
    tail = [f for f in order if f in digit] + sorted(f for f in digit if f not in order)
    # 召喚用 Quaternius は一番後ろ。既存のモデル番号を動かさない。
    summon = sorted(f for f in files if f.startswith("q_Sum"))
    files = [f for f in files if f not in tail and f not in summon] + tail + summon
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
