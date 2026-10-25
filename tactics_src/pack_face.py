# -*- coding: utf-8 -*-
"""Face presets for the human heads. Writes res/tactics/tac_face.bin (TACF).

TACF
  u32 nGender (2: male, female)
  per gender: u32 nFace, u32 nv
    per face: i16 delta[nv*3] (metres * 32767 / DSCALE), i8 nrm[nv*4], u8 rgba[nv*4]
  u32 nModel
  per model: name[32], u32 gender, u32 headStart, u32 nv, f32 lin[9] (row-major, canonical -> bind)
"""
import os, sys, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_mon as pm
import mh_avatar as M

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "tactics", "tac_face.bin")
DSCALE = 0.25
HUMANS = ("zhum_HumAdventurer.glb", "zhum_HumKing.glb", "zhum_HumMedieval.glb", "zhum_HumWitch.glb")


def gender_block(female):
    L = M.FACES_F if female else M.FACES_M
    skin = M.CUTE_SKIN if female else M.MALE_SKIN
    f0 = M.face_source(female, skin, 0)
    out = []
    for fi in range(len(L)):
        f = f0 if fi == 0 else M.face_source(female, skin, fi)
        assert len(f["pos"]) == len(f0["pos"])
        d = (f["pos"] - f0["pos"]) * f0["hw"][:, None]
        # 首の付け根は動かさない（体とつながる）
        d[f0["kind"] == 3] *= np.clip((f0["hw"][f0["kind"] == 3, None] - 0.3) / 0.7, 0, 1)
        out.append((d, f["nrm"], f["col"]))
        print("face", "F" if female else "M", L[fi]["key"], "max delta", round(float(np.abs(d).max()), 4))
    return len(f0["pos"]), out


def main():
    blocks = [gender_block(0), gender_block(1)]
    models = []
    for fn in HUMANS:
        r = pm.load_one(os.path.join(pm.SRC, fn))
        female = ("Witch" in fn) or ("Medieval" in fn)
        xf = r["head_xf"]
        lin = xf["sc"] * np.asarray(xf["yaw"], np.float64)
        nv = int(r["head_n"])
        assert nv == blocks[1 if female else 0][0], (fn, nv)
        assert int(r["head_start"]) + nv == len(r["pos"]), fn
        models.append((pm.disp_name(fn), 1 if female else 0, int(r["head_start"]), nv, lin))
        print("model", pm.disp_name(fn), "start", r["head_start"], "n", nv, "sc", round(float(xf["sc"]), 3))
    with open(OUT, "wb") as fo:
        fo.write(b"TACF")
        fo.write(struct.pack("<I", 2))
        for nv, faces in blocks:
            fo.write(struct.pack("<II", len(faces), nv))
            for d, n, c in faces:
                fo.write(np.clip(np.round(d / DSCALE * 32767), -32767, 32767).astype("<i2").tobytes())
                n4 = np.zeros((nv, 4), np.int8)
                n4[:, :3] = np.clip(np.round(n * 127), -127, 127)
                fo.write(n4.tobytes())
                c4 = np.full((nv, 4), 255, np.uint8)
                c4[:, :3] = np.clip(np.round(np.clip(c, 0, 1) * 255), 0, 255)
                fo.write(c4.tobytes())
        fo.write(struct.pack("<I", len(models)))
        for name, g, hs, nv, lin in models:
            fo.write(pm.pack_name(name, 32))
            fo.write(struct.pack("<III", g, hs, nv))
            fo.write(lin.astype("<f4").tobytes())
    print("bytes", os.path.getsize(OUT))


if __name__ == "__main__":
    main()
