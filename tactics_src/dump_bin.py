# tac_assets.bin の中身確認（寸法・パーツ名・頭部範囲）
import struct, numpy as np

f = open('res/tactics/tac_assets.bin', 'rb').read()
o = 0
def rd(fmt):
    global o
    v = struct.unpack_from('<' + fmt, f, o); o += struct.calcsize('<' + fmt); return v
def nm(n):
    global o
    s = f[o:o + n].split(b'\0')[0].decode(); o += n; return s
assert f[:4] == b'TAC1'; o = 4
ver, nTex, nRig, nMod, nAtt, nSt = rd('6I')
print('ver', ver, nTex, nRig, nMod, nAtt, nSt)
for i in range(nTex):
    (l,) = rd('I'); o += l + ((4 - l % 4) % 4)
rigs = []
for r in range(nRig):
    (nj,) = rd('I')
    names = []
    for j in range(nj):
        names.append(nm(24)); rd('i')
    (nc,) = rd('I')
    cl = []
    for c in range(nc):
        cn = nm(32); nf, fps, hips = rd('IfI')
        o += nf * nj * 8; o += nf * 12
        cl.append((cn, nf))
    rigs.append(names)
    print('rig', r, nj, cl)
for m in range(nMod):
    name = nm(32); rig, tex = rd('II')
    nj = len(rigs[rig])
    rest = np.frombuffer(f, '<f4', nj * 10, o).reshape(nj, 10); o += nj * 40
    top = np.frombuffer(f, '<f4', 16, o); o += 64
    o += nj * 64
    (np_,) = rd('I')
    hj = rigs[rig].index('head')
    parts = []
    for p in range(np_):
        pn = nm(32); glow, nv, ni = rd('III')
        v = np.frombuffer(f, np.uint8, nv * 40, o).reshape(nv, 40); o += nv * 40
        o += ni * 2 + (2 if ni & 1 else 0)
        pos = v[:, :12].copy().view('<f4').reshape(nv, 3)
        jn = v[:, 32:36]; wt = v[:, 36:40]
        head = (jn[:, 0] == hj) & (wt[:, 0] > 200)
        hb = (pos[head].min(0).round(2), pos[head].max(0).round(2)) if head.any() else None
        parts.append((pn, nv, glow, hb))
    print('model', name, 'tex', tex, 'top', top[12:15], parts)
for a in range(nAtt):
    name = nm(32); jn = nm(24); ti, nv, ni = rd('III')
    pos = np.frombuffer(f, '<f4', nv * 8, o).reshape(nv, 8)[:, :3]; o += nv * 32
    o += ni * 2 + (2 if ni & 1 else 0)
    print('att', a, name, jn, 'tex', ti, nv, pos.min(0).round(2), pos.max(0).round(2))
for s in range(nSt):
    name = nm(32); ti, nv, ni = rd('III')
    bb = rd('6f')
    o += nv * 32 + ni * 2 + (2 if ni & 1 else 0)
    print('st', s, name, 'tex', ti, nv, [round(x, 2) for x in bb])
print('end', o, len(f))
