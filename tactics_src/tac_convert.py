# tac_convert.py — KayKit (CC0) glTF/GLB → res/tactics/tac_assets.bin
# 使い方: python tactics_src\tac_convert.py   （tactics_src\kk, tactics_src\nat に素材を置く）
import json, struct, os, math, hashlib, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'res', 'tactics', 'tac_assets.bin')

CT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
NP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}


class Gltf:
    def __init__(self, path):
        self.path = path
        d = open(path, 'rb').read()
        self.bins = []
        if d[:4] == b'glTF':
            off = 12
            while off < len(d):
                ln, ty = struct.unpack_from('<II', d, off); off += 8
                ch = d[off:off + ln]; off += ln
                if ty == 0x4E4F534A: self.j = json.loads(ch)
                elif ty == 0x004E4942: self.glb = ch
            self.bufs = [self.glb]
        else:
            self.j = json.loads(d)
            self.bufs = []
            for b in self.j['buffers']:
                self.bufs.append(open(os.path.join(os.path.dirname(path), b['uri']), 'rb').read())

    def acc(self, i, normalize=True):
        a = self.j['accessors'][i]
        bv = self.j['bufferViews'][a['bufferView']]
        buf = self.bufs[bv.get('buffer', 0)]
        n = NC[a['type']]
        dt = NP[a['componentType']]
        isz = np.dtype(dt).itemsize
        stride = bv.get('byteStride', 0) or isz * n
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        cnt = a['count']
        raw = np.frombuffer(buf, dtype=np.uint8, count=stride * (cnt - 1) + isz * n, offset=off)
        out = np.lib.stride_tricks.as_strided(raw, shape=(cnt, stride), strides=(stride, 1))[:, :isz * n].copy()
        arr = out.view(dt).reshape(cnt, n)
        if normalize and a.get('normalized') and dt != np.float32:
            mx = float(np.iinfo(dt).max)
            arr = arr.astype(np.float32) / mx
        return arr

    def image_bytes(self, i):
        im = self.j['images'][i]
        if 'bufferView' in im:
            bv = self.j['bufferViews'][im['bufferView']]
            o = bv.get('byteOffset', 0)
            return self.bufs[bv.get('buffer', 0)][o:o + bv['byteLength']]
        return open(os.path.join(os.path.dirname(self.path), im['uri']), 'rb').read()


def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz])


def trs_mat(t, r, s):
    x, y, z, w = r
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4)
    M[:3, :3] = R * np.array(s)[None, :]
    M[:3, 3] = t
    return M  # column-vector convention


def node_local(n):
    if 'matrix' in n:
        return np.array(n['matrix'], dtype=np.float64).reshape(4, 4).T
    return trs_mat(n.get('translation', [0, 0, 0]), n.get('rotation', [0, 0, 0, 1]), n.get('scale', [1, 1, 1]))


def node_trs(n):
    if 'matrix' in n:
        M = np.array(n['matrix'], dtype=np.float64).reshape(4, 4).T
        t = M[:3, 3]
        s = np.linalg.norm(M[:3, :3], axis=0)
        R = M[:3, :3] / s[None, :]
        q = mat_to_quat(R)
        return list(t), list(q), list(s)
    return n.get('translation', [0, 0, 0]), n.get('rotation', [0, 0, 0, 1]), n.get('scale', [1, 1, 1])


def mat_to_quat(R):
    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return [(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, 0.25 * s]
    if R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        return [0.25 * s, (R[0, 1] + R[1, 0]) / s, (R[0, 2] + R[2, 0]) / s, (R[2, 1] - R[1, 2]) / s]
    if R[1, 1] > R[2, 2]:
        s = math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        return [(R[0, 1] + R[1, 0]) / s, 0.25 * s, (R[1, 2] + R[2, 1]) / s, (R[0, 2] - R[2, 0]) / s]
    s = math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
    return [(R[0, 2] + R[2, 0]) / s, (R[1, 2] + R[2, 1]) / s, 0.25 * s, (R[1, 0] - R[0, 1]) / s]


def parents_of(j):
    par = [-1] * len(j['nodes'])
    for i, n in enumerate(j['nodes']):
        for c in n.get('children', []):
            par[c] = i
    return par


def node_global(j, par, i):
    M = node_local(j['nodes'][i])
    p = par[i]
    while p >= 0:
        M = node_local(j['nodes'][p]) @ M
        p = par[p]
    return M


class TexTable:
    def __init__(self):
        self.list = []
        self.idx = {}

    def add(self, data):
        h = hashlib.sha1(data).hexdigest()
        if h in self.idx:
            return self.idx[h]
        self.idx[h] = len(self.list)
        self.list.append(data)
        return self.idx[h]


def prim_arrays(g, pr):
    at = pr['attributes']
    pos = g.acc(at['POSITION']).astype(np.float32)
    nrm = g.acc(at['NORMAL']).astype(np.float32) if 'NORMAL' in at else np.tile([0, 1, 0], (len(pos), 1)).astype(np.float32)
    uv = g.acc(at['TEXCOORD_0']).astype(np.float32) if 'TEXCOORD_0' in at else np.zeros((len(pos), 2), np.float32)
    idx = g.acc(pr['indices'], normalize=False).reshape(-1).astype(np.uint32) if 'indices' in pr else np.arange(len(pos), dtype=np.uint32)
    jn = g.acc(at['JOINTS_0'], normalize=False).astype(np.int32) if 'JOINTS_0' in at else None
    wt = g.acc(at['WEIGHTS_0']).astype(np.float32) if 'WEIGHTS_0' in at else None
    return pos, nrm, uv, idx, jn, wt


def merge_prims(g, mesh, xf=None):
    P, N, U, I, J, W = [], [], [], [], [], []
    base = 0
    for pr in mesh['primitives']:
        if pr.get('mode', 4) != 4:
            continue
        pos, nrm, uv, idx, jn, wt = prim_arrays(g, pr)
        P.append(pos); N.append(nrm); U.append(uv); I.append(idx + base)
        if jn is not None: J.append(jn); W.append(wt)
        base += len(pos)
    pos = np.concatenate(P); nrm = np.concatenate(N); uv = np.concatenate(U); idx = np.concatenate(I)
    jn = np.concatenate(J) if J else None
    wt = np.concatenate(W) if W else None
    if xf is not None:
        h = np.concatenate([pos, np.ones((len(pos), 1), np.float32)], 1)
        pos = (h @ xf.T)[:, :3].astype(np.float32)
        nm = np.linalg.inv(xf[:3, :3]).T
        nrm = nrm @ nm.T
        nrm = (nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-8)).astype(np.float32)
    return pos, nrm, uv, idx, jn, wt


def pack_name(s, n):
    b = s.encode('utf-8')[:n - 1]
    return b + b'\0' * (n - len(b))


ADV = ['Knight', 'Barbarian', 'Mage', 'Rogue', 'Rogue_Hooded']
SKEL = ['Skeleton_Warrior', 'Skeleton_Minion', 'Skeleton_Rogue', 'Skeleton_Mage']
CLIPS = ['Idle', 'Walking_A', 'Running_A', '1H_Melee_Attack_Chop', '1H_Melee_Attack_Slice_Diagonal',
         '1H_Melee_Attack_Stab', '2H_Melee_Attack_Chop', '2H_Melee_Attack_Spin', '1H_Ranged_Shoot',
         '2H_Ranged_Shoot', 'Spellcast_Shoot', 'Spellcast_Raise', 'Spellcast_Long', 'Hit_A', 'Hit_B',
         'Death_A', 'Cheer', 'Block', 'Dodge_Backward', '2H_Melee_Idle', 'Throw',
         'Unarmed_Melee_Attack_Punch_A', 'Jump_Full_Short', 'Interact', 'Taunt', 'Spawn_Ground', 'Use_Item']
FPS = 30.0


def sample_channel(g, sampler, t):
    times = g.acc(sampler['input']).reshape(-1)
    vals = g.acc(sampler['output'])
    interp = sampler.get('interpolation', 'LINEAR')
    if interp == 'CUBICSPLINE':
        vals = vals.reshape(len(times), 3, -1)[:, 1, :]
    if t <= times[0]:
        return vals[0]
    if t >= times[-1]:
        return vals[-1]
    k = int(np.searchsorted(times, t) - 1)
    k = max(0, min(k, len(times) - 2))
    t0, t1 = times[k], times[k + 1]
    a = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
    if interp == 'STEP':
        return vals[k]
    v0, v1 = vals[k].astype(np.float64), vals[k + 1].astype(np.float64)
    if len(v0) == 4:
        if np.dot(v0, v1) < 0: v1 = -v1
        v = v0 * (1 - a) + v1 * a
        return v / max(np.linalg.norm(v), 1e-9)
    return v0 * (1 - a) + v1 * a


def build_rig(g, skin):
    j = g.j
    joints = skin['joints']
    par = parents_of(j)
    names = [j['nodes'][n]['name'] for n in joints]
    jset = {n: i for i, n in enumerate(joints)}
    parents = []
    for n in joints:
        p = par[n]
        parents.append(jset.get(p, -1))
    return names, parents


def rig_clips(g, skin, names):
    j = g.j
    joints = skin['joints']
    nodeToJ = {n: i for i, n in enumerate(joints)}
    hips = names.index('hips')
    out = []
    amap = {a['name']: a for a in j.get('animations', [])}
    for cname in CLIPS:
        a = amap.get(cname)
        if not a:
            continue
        rot = {}
        tra = {}
        dur = 0.0
        for ch in a['channels']:
            tn = ch['target'].get('node')
            if tn not in nodeToJ:
                continue
            s = a['samplers'][ch['sampler']]
            times = g.acc(s['input']).reshape(-1)
            dur = max(dur, float(times[-1]))
            if ch['target']['path'] == 'rotation':
                rot[nodeToJ[tn]] = s
            elif ch['target']['path'] == 'translation':
                tra[nodeToJ[tn]] = s
        nf = max(2, int(round(dur * FPS)) + 1)
        q = np.zeros((nf, len(joints), 4), np.float32)
        ht = np.zeros((nf, 3), np.float32)
        for f in range(nf):
            t = f / FPS
            for ji, n in enumerate(joints):
                if ji in rot:
                    q[f, ji] = sample_channel(g, rot[ji], t)
                else:
                    q[f, ji] = node_trs(j['nodes'][n])[1]
            if hips in tra:
                ht[f] = sample_channel(g, tra[hips], t)
            else:
                ht[f] = node_trs(j['nodes'][joints[hips]])[0]
        out.append((cname, nf, q, ht))
    return out


def write_clips(fo, clips, hips):
    fo.write(struct.pack('<I', len(clips)))
    for cname, nf, q, ht in clips:
        fo.write(pack_name(cname, 32))
        fo.write(struct.pack('<IfI', nf, FPS, hips))
        qi = np.clip(np.round(q * 32767.0), -32767, 32767).astype('<i2')
        fo.write(qi.tobytes())
        fo.write(ht.astype('<f4').tobytes())


def static_verts(pos, nrm, uv):
    v = np.concatenate([pos, nrm, uv], 1).astype('<f4')
    return v.tobytes()


def main():
    tex = TexTable()
    rigs = []      # (names, parents, clips)
    models = []    # dicts
    attach = []    # name, joint, tex, verts(bytes), nverts, idx
    seen_attach = set()

    def load_family(files, fam):
        rig_names = None
        for mi, mname in enumerate(files):
            g = Gltf(os.path.join(HERE, 'kk', mname + '.glb'))
            j = g.j
            skin = j['skins'][0]
            names, parents = build_rig(g, skin)
            if rig_names is None:
                rig_names = names
                clips = rig_clips(g, skin, names)
                rigs.append((names, parents, clips))
                print(' rig', fam, len(names), 'joints', len(clips), 'clips')
            elif names != rig_names:
                raise SystemExit('rig mismatch ' + mname)
            par = parents_of(j)
            img = j['materials'][0].get('pbrMetallicRoughness', {}).get('baseColorTexture', {}).get('index', 0)
            ti = tex.add(g.image_bytes(j['textures'][img]['source']))
            joints = skin['joints']
            rest = [node_trs(j['nodes'][n]) for n in joints]
            # 骨格ルートの親（Rig ノード）までの大域変換
            rootJ = joints[parents.index(-1)]
            top = np.eye(4)
            p = par[rootJ]
            chain = []
            while p >= 0:
                chain.append(p); p = par[p]
            for p in reversed(chain):
                top = top @ node_local(j['nodes'][p])
            ibm = g.acc(skin['inverseBindMatrices']).reshape(-1, 16)
            parts = []
            for ni, n in enumerate(j['nodes']):
                if 'mesh' not in n:
                    continue
                mesh = j['meshes'][n['mesh']]
                mat = mesh['primitives'][0].get('material', 0)
                glow = 1 if j['materials'][mat].get('name', '') == 'Glow' else 0
                if 'skin' in n:
                    pos, nrm, uv, idx, jn, wt = merge_prims(g, mesh)
                    wsum = wt.sum(1, keepdims=True)
                    wt = wt / np.maximum(wsum, 1e-8)
                    w8 = np.clip(np.round(wt * 255), 0, 255).astype(np.int32)
                    w8[:, 0] += 255 - w8.sum(1)
                    parts.append((n['name'], glow, pos, nrm, uv, idx, jn.astype(np.uint8), w8.astype(np.uint8)))
                else:
                    pj = par[ni]
                    if pj < 0 or pj not in joints:
                        continue
                    key = n['name']
                    if key in seen_attach:
                        continue
                    seen_attach.add(key)
                    pos, nrm, uv, idx, _, _ = merge_prims(g, mesh, node_local(n))
                    attach.append((key, j['nodes'][pj]['name'], ti, pos, nrm, uv, idx))
            models.append(dict(name=mname, rig=len(rigs) - 1, tex=ti, rest=rest, top=top, ibm=ibm, parts=parts))
            print(' model', mname, 'parts', [p[0] for p in parts])

    load_family(ADV, 'adv')
    load_family(SKEL, 'skel')

    for wn in ['Skeleton_Blade', 'Skeleton_Axe', 'Skeleton_Staff', 'Skeleton_Shield_Small_A', 'Skeleton_Shield_Large_A', 'Skeleton_Crossbow']:
        g = Gltf(os.path.join(HERE, 'kk', wn + '.gltf'))
        n0 = g.j['nodes'][0]
        pos, nrm, uv, idx, _, _ = merge_prims(g, g.j['meshes'][n0['mesh']], node_local(n0))
        ti = tex.add(g.image_bytes(0))
        joint = 'handslot.l' if 'Shield' in wn else 'handslot.r'
        attach.append((wn, joint, ti, pos, nrm, uv, idx))

    statics = []
    NAT = ['tree_single_A', 'tree_single_B', 'trees_A_medium', 'trees_B_medium', 'trees_B_large',
           'rock_single_A', 'rock_single_C', 'rock_single_E', 'mountain_A', 'mountain_B', 'mountain_C',
           'mountain_A_grass', 'hill_single_A', 'hills_A_trees', 'waterlily_A', 'waterplant_A',
           'tent', 'flag_red', 'barrel', 'crate_A_big',
           'tree_dead_large', 'tree_dead_medium', 'tree_pine_orange_large', 'tree_pine_yellow_medium',
           'gravestone', 'pumpkin_orange_jackolantern', 'lantern_standing']
    for sn in NAT:
        g = Gltf(os.path.join(HERE, 'nat', sn + '.gltf'))
        P, N, U, I = [], [], [], []
        base = 0
        par = parents_of(g.j)
        for ni, n in enumerate(g.j['nodes']):
            if 'mesh' not in n:
                continue
            pos, nrm, uv, idx, _, _ = merge_prims(g, g.j['meshes'][n['mesh']], node_global(g.j, par, ni))
            P.append(pos); N.append(nrm); U.append(uv); I.append(idx + base); base += len(pos)
        pos = np.concatenate(P); nrm = np.concatenate(N); uv = np.concatenate(U); idx = np.concatenate(I)
        ti = tex.add(g.image_bytes(0))
        statics.append((sn, ti, pos, nrm, uv, idx))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as fo:
        fo.write(b'TAC1')
        fo.write(struct.pack('<6I', 1, len(tex.list), len(rigs), len(models), len(attach), len(statics)))
        for t in tex.list:
            fo.write(struct.pack('<I', len(t)))
            fo.write(t)
            fo.write(b'\0' * ((4 - len(t) % 4) % 4))
        for names, parents, clips in rigs:
            fo.write(struct.pack('<I', len(names)))
            for nm, p in zip(names, parents):
                fo.write(pack_name(nm, 24))
                fo.write(struct.pack('<i', p))
            write_clips(fo, clips, names.index('hips'))
        for m in models:
            fo.write(pack_name(m['name'], 32))
            fo.write(struct.pack('<II', m['rig'], m['tex']))
            for t, r, s in m['rest']:
                fo.write(struct.pack('<10f', *t, *r, *s))
            fo.write(m['top'].T.astype('<f4').tobytes())  # 列優先で書き、C++ 側で行ベクトル用に転置済み扱い
            fo.write(m['ibm'].astype('<f4').tobytes())     # glTF 列優先のまま
            fo.write(struct.pack('<I', len(m['parts'])))
            for name, glow, pos, nrm, uv, idx, jn, w8 in m['parts']:
                fo.write(pack_name(name, 32))
                fo.write(struct.pack('<III', glow, len(pos), len(idx)))
                v = bytearray()
                fv = np.concatenate([pos, nrm, uv], 1).astype('<f4')
                for k in range(len(pos)):
                    v += fv[k].tobytes() + bytes(jn[k].tolist()) + bytes(w8[k].tolist())
                fo.write(bytes(v))
                fo.write(idx.astype('<u2').tobytes())
                if len(idx) & 1: fo.write(b'\0\0')
        for name, joint, ti, pos, nrm, uv, idx in attach:
            fo.write(pack_name(name, 32))
            fo.write(pack_name(joint, 24))
            fo.write(struct.pack('<III', ti, len(pos), len(idx)))
            fo.write(static_verts(pos, nrm, uv))
            fo.write(idx.astype('<u2').tobytes())
            if len(idx) & 1: fo.write(b'\0\0')
        for name, ti, pos, nrm, uv, idx in statics:
            fo.write(pack_name(name, 32))
            fo.write(struct.pack('<III', ti, len(pos), len(idx)))
            fo.write(struct.pack('<6f', *pos.min(0), *pos.max(0)))
            fo.write(static_verts(pos, nrm, uv))
            fo.write(idx.astype('<u2').tobytes())
            if len(idx) & 1: fo.write(b'\0\0')
    print('textures', len(tex.list), 'attach', [a[0] + '@' + a[1] for a in attach])
    print('statics', [(s[0], len(s[2])) for s in statics])
    print('wrote', OUT, os.path.getsize(OUT))


if __name__ == '__main__':
    main()
