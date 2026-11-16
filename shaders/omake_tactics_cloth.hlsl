// アバターの衣装。腰の付け根だけ骨に残し、それより下は布。
// 足は体積の外へ押すだけ。戻りは重力と摩擦と風。足では引き戻さない。
// 裾ほどよく流れ、ミニスカートはロングの同じ高さ（無い布の分）だけ軽く揺れる。
// 状態はキャラごと。影と本描画で二度積分しない。
cbuffer ClothParm : register(b0)
{
	row_major float4x4 Delta;  // 前のフレームのモデル空間から今のモデル空間へ（移動と向きの慣性）
	row_major float4x4 Hips;   // 腰の骨（バインドから今の姿勢へ）
	row_major float4x4 HipInv; // その逆
	float4 Seg[24];            // 脚の形 6 本（太もも・すね・足 × 左右）。並びは旧 4 本と同じ
	row_major float4x4 Pal[6];
	row_major float4x4 Inv[6];
	uint4 Cnt;                 // x=格子の点の数 y=脚の形の数 z=動かす頂点の数
	float4 Sim;                // x=dt y=60fps 換算の歩数 z=1 で目標へ置き直す w=身長
	float4 Mode;               // x=座る割合 y,z=スカートの中心軸（バインド） w=時刻
	float4 Wind;               // xyz=強風・暴風と移動の向かい風 w=宙に浮いている割合
	float4 Air;                // xyz=そよ風（別方向） w=上下。上が正。モデル空間
	float4 Limb[6];            // その足の、このフレームの移動（モデル空間）。w=長さ
	float4 Grav;               // xyz=モデル空間の重力加速度 w=スカート上端のバインド Y
	float4 Hand[4];            // 手のAABB。0,2=最小 xyz（w=1 で有効）、1,3=最大 xyz（w=このフレームの移動量）
};

cbuffer SkinBone : register(b1)
{
	row_major float4x4 Bones[64];
};

#define NMAX 512
#define PT 10
#define PF 16
// 生の断面の後ろ。胴 16、足 4 × 高さ 13 × 角度 16、その最大半径 4。CPU が同じ式で埋める。
#define NSLICE 13
#define RAD0 (6 * PT * PF + 16)

struct ClothN
{
	float4 x;   // バインドの位置
	float4 p;   // x=目標から離れてよい距離 y=1 歩で目標へ寄る割合 z=重力 w=1 は腰に固定（最上段）
	uint4 nb;   // 上・下・左・右（無ければ範囲外）
	uint4 j;    // 脚に付いていくときの骨（格子の上でならした重み）
	float4 w;   // その重み
	float4 q;   // x=脚に付いていく割合 y=静止時に脚の形へ潜っている深さ z=両脇の開いた布の前 1・後ろ 2 w=別に垂らす割合
};

struct ClothV
{
	uint4 a;    // x=メッシュの頂点 y,z=格子の点 4 つ（16bit ずつ） w=下 2bit 前 1・後ろ 2、上の 8bit 別に垂らす割合
	float4 w;   // 4 点の重み
	float4 p;   // バインドの位置 w=動く割合（腰の上 0 → 下 1）
	float4 n;   // バインドの法線 w=静止時に脚の形へ潜っている深さ
};

StructuredBuffer<ClothN> Cn : register(t0);
StructuredBuffer<ClothV> Cv : register(t1);
StructuredBuffer<float> Prof : register(t2);
RWByteAddressBuffer Vout : register(u0);
// [0,N) 今 [N,2N) 前 [2N,3N) 作業 [3N,4N) 腰に固定した形からのずれ
RWStructuredBuffer<float4> St : register(u1);

groupshared float3 gP[NMAX];
// 足の位置と最大半径。点ごとに行列を掛け直さない。結果は同じ。
groupshared float3 gDown, gEhx, gEhz, gWaist, gHipUp;
groupshared float3 gPA[6], gPB[6], gPU[6], gPW[6];
groupshared float gRmax[6];
groupshared uint gNLeg;
groupshared float gFloor, gSpread;
// 複数人の拘束を同時に走らせるとき、スロット分だけバッファをずらす。1 人のときは 0。
groupshared uint gCn0, gSt0, gProf0;
struct ClothParmS
{
	row_major float4x4 Delta;
	row_major float4x4 Hips;
	row_major float4x4 HipInv;
	float4 Seg[24];
	row_major float4x4 Pal[6];
	row_major float4x4 Inv[6];
	uint4 Cnt;
	float4 Sim;
	float4 Mode;
	float4 Wind;
	float4 Air;
	float4 Limb[6];
	float4 Grav;
	float4 Hand[4];
};
groupshared ClothParmS gPrm;
cbuffer ClothGate : register(b2) { uint BatchOn; };
ByteAddressBuffer ParmRaw : register(t3);

void LoadClothCB(uint gid)
{
	if (gid == 0) {
		gCn0 = 0;
		gSt0 = 0;
		gProf0 = 0;
		gPrm.Delta = Delta;
		gPrm.Hips = Hips;
		gPrm.HipInv = HipInv;
		[unroll] for (int i = 0; i < 24; i++) gPrm.Seg[i] = Seg[i];
		[unroll] for (int k = 0; k < 6; k++) {
			gPrm.Pal[k] = Pal[k];
			gPrm.Inv[k] = Inv[k];
			gPrm.Limb[k] = Limb[k];
		}
		[unroll] for (int h = 0; h < 4; h++) gPrm.Hand[h] = Hand[h];
		gPrm.Cnt = Cnt;
		gPrm.Sim = Sim;
		gPrm.Mode = Mode;
		gPrm.Wind = Wind;
		gPrm.Air = Air;
		gPrm.Grav = Grav;
	}
	GroupMemoryBarrierWithGroupSync();
}

float4 ParmAt(uint slot, uint off)
{
	return asfloat(ParmRaw.Load4(slot * 1600u + off));
}

void LoadClothBatch(uint gid, uint slot)
{
	if (gid == 0) {
		gCn0 = slot * (uint)NMAX;
		gSt0 = slot * (uint)NMAX * 8u;
		gProf0 = slot * 2048u;
		gPrm.Delta = float4x4(ParmAt(slot, 0), ParmAt(slot, 16), ParmAt(slot, 32), ParmAt(slot, 48));
		gPrm.Hips = float4x4(ParmAt(slot, 64), ParmAt(slot, 80), ParmAt(slot, 96), ParmAt(slot, 112));
		gPrm.HipInv = float4x4(ParmAt(slot, 128), ParmAt(slot, 144), ParmAt(slot, 160), ParmAt(slot, 176));
		[unroll] for (int i = 0; i < 24; i++) gPrm.Seg[i] = ParmAt(slot, 192u + (uint)i * 16u);
		[unroll] for (int k = 0; k < 6; k++) {
			uint o = 576u + (uint)k * 64u;
			gPrm.Pal[k] = float4x4(ParmAt(slot, o), ParmAt(slot, o + 16u), ParmAt(slot, o + 32u), ParmAt(slot, o + 48u));
			uint q = 960u + (uint)k * 64u;
			gPrm.Inv[k] = float4x4(ParmAt(slot, q), ParmAt(slot, q + 16u), ParmAt(slot, q + 32u), ParmAt(slot, q + 48u));
			gPrm.Limb[k] = ParmAt(slot, 1424u + (uint)k * 16u);
		}
		[unroll] for (int h = 0; h < 4; h++) gPrm.Hand[h] = ParmAt(slot, 1536u + (uint)h * 16u);
		gPrm.Cnt = asuint(ParmAt(slot, 1344u));
		gPrm.Sim = ParmAt(slot, 1360u);
		gPrm.Mode = ParmAt(slot, 1376u);
		gPrm.Wind = ParmAt(slot, 1392u);
		gPrm.Air = ParmAt(slot, 1408u);
		gPrm.Grav = ParmAt(slot, 1520u);
	}
	GroupMemoryBarrierWithGroupSync();
}

#define Delta gPrm.Delta
#define Hips gPrm.Hips
#define HipInv gPrm.HipInv
#define Seg gPrm.Seg
#define Pal gPrm.Pal
#define Inv gPrm.Inv
#define Cnt gPrm.Cnt
#define Sim gPrm.Sim
#define Mode gPrm.Mode
#define Wind gPrm.Wind
#define Air gPrm.Air
#define Limb gPrm.Limb
#define Grav gPrm.Grav
#define Hand gPrm.Hand

// カットは時刻を負にして渡す。複数人の同時拘束は骨パレットを持たないので、位相だけ絶対値を使う。
float ClothTime()
{
	return abs(Mode.w);
}

float InCut()
{
	return Mode.w < 0.0 ? 1.0 : 0.0;
}

// しゃがみ。カットでも通常でも同じ物理ルートを通す。
float WearSkirt(float y)
{
	return saturate(Mode.x + y * 0.0);
}

float3 GravDown()
{
	float gl = length(Grav.xyz);
	return gl > 1e-4 ? Grav.xyz / gl : float3(0, -1, 0);
}

// 腰の左右・前後を、重力に垂直な向きへ。横たわりで片方が重力に沿うと成分が消え、
// スカートの輪が足の中へ潰れる。消えた軸はもう一方と重力から作り直す。
void FlatAxes(out float3 ehx, out float3 ehz, out float3 down)
{
	down = GravDown();
	float3 hx = Hips[0].xyz;
	float3 hz = Hips[2].xyz;
	ehx = hx - down * dot(hx, down);
	ehz = hz - down * dot(hz, down);
	float exl = length(ehx), ezl = length(ehz);
	if (exl < 0.20) {
		ehx = cross(down, ezl > 0.20 ? (ehz / ezl) : Hips[1].xyz);
		exl = length(ehx);
	}
	ehx = exl > 1e-4 ? ehx / exl : float3(1, 0, 0);
	if (ezl < 0.20) {
		ehz = cross(ehx, down);
		if (dot(ehz, hz) < 0) ehz = -ehz;
		ezl = length(ehz);
	}
	ehz = ezl > 1e-4 ? ehz / ezl : cross(ehx, down);
}

void BuildLegs(uint gid)
{
	if (gid == 0) {
		float3 ehx, ehz, down;
		FlatAxes(ehx, ehz, down);
		float3 waistC = mul(float4(Mode.y, Grav.w, Mode.z, 1), Hips).xyz;
		float3 hUp = Hips[1].xyz;
		float hul = length(hUp);
		gHipUp = hul > 1e-4 ? (hUp / hul) : (-down);
		gDown = down;
		gEhx = ehx;
		gEhz = ehz;
		gWaist = waistC;
		gNLeg = min(Cnt.y, 6u);
	}
	GroupMemoryBarrierWithGroupSync();
	if (gid < 6) {
		// 表に入っている半径の最大。補間した足の半径はこれを超えない。細かい表は太ももとすねの 4 本。
		if (gid < 4) gRmax[gid] = Prof[gProf0 + RAD0 + 4 * NSLICE * PF + gid];
		else gRmax[gid] = 0.095;
		if (gid < gNLeg) {
			float4 A = Seg[gid * 4];
			float3 ax = Seg[gid * 4 + 1].xyz;
			float len = max(A.w, 1e-4);
			gPA[gid] = mul(float4(A.xyz, 1), Pal[gid]).xyz - gWaist;
			gPB[gid] = mul(float4(A.xyz + ax * len, 1), Pal[gid]).xyz - gWaist;
			gPU[gid] = mul(Seg[gid * 4 + 2].xyz, (float3x3)Pal[gid]);
			gPW[gid] = mul(Seg[gid * 4 + 3].xyz, (float3x3)Pal[gid]);
		}
	}
	GroupMemoryBarrierWithGroupSync();
	if (gid == 0) {
		float sc = max(Sim.w, 1e-4);
		float dMax = dot(gWaist, gDown) + 0.095;
		[loop] for (uint s = 0; s < gNLeg; s++) {
			float rS = s < 2u ? 0.078 : (s < 4u ? 0.062 : 0.024);
			dMax = max(dMax, max(dot(gWaist + gPA[s], gDown), dot(gWaist + gPB[s], gDown)) + rS);
		}
		// 飛行中かつ直立時は床で切らない。地上・倒れ時は腰と脚の最下面を床とする。
		float lie = 1.0 - smoothstep(0.30, 0.96, abs(dot(gHipUp, gDown)));
		gFloor = (Wind.w > 0.5 && lie < 0.25) ? 1e5 : (dMax * sc);
		// 開脚時は折り畳まれたプリーツ・ドレープが開く分だけ横の周囲長上限を緩め、ハの字の太ももで腰へたくし上がるのを防ぐ。
		float sp = 0.0;
		if (gNLeg >= 2u) {
			float hipSpan = max(length(gPA[0] - gPA[1]), 0.16);
			float kneeSpan = length(gPB[0] - gPB[1]);
			sp = clamp((kneeSpan - hipSpan - 0.03) / 0.34, 0.0, 0.65);
		}
		gSpread = sp;
	}
	GroupMemoryBarrierWithGroupSync();
}

float ProfR(uint s, float t, float phi)
{
	float ft = saturate((t + 0.08) / 1.16) * (PT - 1);
	uint t0 = min((uint)ft, (uint)(PT - 2));
	float a = ft - t0;
	float fp = (phi / 6.2831853 + 0.5) * PF;
	float fl = floor(fp);
	float b = fp - fl;
	uint p0 = (uint)(fl + PF) % PF;
	uint p1 = (p0 + 1) % PF;
	uint o = s * PT * PF;
	float r0 = lerp(Prof[gProf0 + o + t0 * PF + p0], Prof[gProf0 + o + t0 * PF + p1], b);
	float r1 = lerp(Prof[gProf0 + o + (t0 + 1) * PF + p0], Prof[gProf0 + o + (t0 + 1) * PF + p1], b);
	return lerp(r0, r1, a);
}

// どれかの脚の形の中か。allow は作りの段階で潜っていた深さ（静止しているのに布が膨らまないように）。
// 負のときは脚の形をその分太く見る（頂点の間の面が脚の丸みに食い込まない余白）。
int Inside(float3 p, float allow)
{
	uint ns = min(Cnt.y, 6u);
	for (uint s = 0; s < ns; s++) {
		float4 A = Seg[s * 4];
		float3 ax = Seg[s * 4 + 1].xyz;
		float3 q = mul(float4(p / Sim.w, 1), Inv[s]).xyz - A.xyz;
		float t = dot(q, ax) / A.w;
		if (t < Seg[s * 4 + 1].w || t > 1.15) continue;
		float3 rv = q - ax * (t * A.w);
		float k = Seg[s * 4 + 2].w > 0 ? smoothstep(0, 1, (t - Seg[s * 4 + 1].w) / Seg[s * 4 + 2].w) : 1;
		float R = ProfR(s, t, atan2(dot(rv, Seg[s * 4 + 3].xyz), dot(rv, Seg[s * 4 + 2].xyz))) * k - allow;
		if (R > 0 && dot(rv, rv) < R * R) return (int)s;
	}
	return -1;
}

// 脚の中だけを、面の外へ少し出す。厚く押すと裾が袋になる。1 回は 6.5cm まで、数回に分ける。
// useFeet が 0 のときは靴のカプセルを見ない。離した裾を靴の表面へ戻さないため。
// pan が 1（前布）のときは脚の前面（+u）側へ、2（後ろ布）のときは後面（-u）側へ出し、布がすねを前後に貫かないようにする。
float3 CollideBody(float3 p, float allow, float useFeet, uint pan)
{
	// 腰の帯は足の障害物にしない。股関節の上端付近は滑らかに立ち上げ、腰に四角い膨らみを作らない。
	float3 hip = mul(float4(p / max(Sim.w, 1e-4), 1), HipInv).xyz;
	if (hip.y > Grav.w - 0.035) return p;
	float wHip = smoothstep(0.035, 0.13, Grav.w - hip.y);
	uint ns = min(Cnt.y, useFeet > 0.5 ? 6u : 4u);
	// その角度の肌まで出す。外の布は動かさない。左右が重なる所は、もう一方の足へ押し込まず前後へ出す。
	[loop] for (int rep = 0; rep < 4; rep++) {
		float bestPen = 0;
		float3 bestN = float3(0, 0, -1);
		[loop] for (uint s = 0; s < ns; s++) {
			float4 A = Seg[s * 4];
			float3 q = mul(float4(p / Sim.w, 1), Inv[s]).xyz - A.xyz;
			float3 ax = Seg[s * 4 + 1].xyz;
			float len = max(A.w, 1e-4);
			float tRaw = dot(q, ax) / len;
			float t0 = s < 2u ? 0.06 : -0.15;
			float t1 = s >= 4u ? 1.85 : 1.35;
			if (tRaw < t0 || tRaw > t1) continue;
			float tc = tRaw < 0 ? 0 : (tRaw > 1 ? 1 : tRaw);
			float3 rv = (tRaw < 0) ? q : ((tRaw > 1) ? (q - ax * len) : (q - ax * (tc * len)));
			float3 u = Seg[s * 4 + 2].xyz;
			float3 w = Seg[s * 4 + 3].xyz;
			float fu = dot(rv, u);
			float fw = dot(rv, w);
			float dist = length(rv);
			// 真上・真下は横成分が 0 で atan2 が決まらない。角度 0 の半径を使う。全周の最大は使わない。
			float perp = length(float2(fu, fw));
			float phi = perp > 1e-4 ? atan2(fw, fu) : 0;
			float pad = allow < 0.f ? max(-allow - 0.010, 0.004) : 0.004;
			pad = max(pad, s >= 4u ? 0.012 : 0.006);
			// 付け根だけ弱くする。太ももの途中まで弱めると、そこを境に布が落ちて足が出る。
			float wTop = s < 2u ? smoothstep(0.03, 0.14, tRaw) : 1.0;
			// 今の角度で測った肌。ProfR にはすでに布厚 1.2cm が入っているので二重に膨らませない。
			float R = ProfR(s, tc, phi) + pad * wTop;
			if (R < 0.032) R = 0.032;
			if (dist >= R) continue;
			float3 dirLocal = rv;
			if (dist < 0.004) {
				if (pan == 1u) dirLocal = u;
				else if (pan == 2u) dirLocal = -u;
				else dirLocal = abs(fw) >= abs(fu) ? (fw >= 0 ? w : -w) : (fu >= 0 ? u : -u);
			}
			float dl = length(dirLocal);
			dirLocal = dl > 1e-4 ? dirLocal / dl : u;
			float3 worldD = mul(dirLocal * R - rv, (float3x3)Pal[s]) * (Sim.w * wHip * wTop);
			float move = length(worldD);
			if (move > bestPen) { bestPen = move; bestN = worldD; }
		}
		if (bestPen <= 0) break;
		p += bestN;
	}
	return p;
}
float3 Collide(float3 p, float allow)
{
	return CollideBody(p, allow, 1, 0u);
}

// 前面の平面まで引きずると三角形が裂け、開脚で裾が床に広がる。半径の外へ出すのは CollideBody。
float3 SheetSide(float3 p, float allow, float sign)
{
	return p + (allow - allow) * sign * float3(0, 0, 0);
}

// 立っていると 0。仰向け・横向きは 1。傾き始め（約16度）から滑らかに立ち上げる。
float LieAmt()
{
	return 1.0 - smoothstep(0.30, 0.96, abs(dot(gHipUp, gDown)));
}

// 横たわりと起き上がり。足の断面の中にいる布を、重力と逆（上）の表面まで出す。
// 横方向の押し出しだけでは、腿の上に落ちた裾が中に残る。
float3 LiftTop(float3 p, float pad, float lie)
{
	if (lie < 0.02) return p;
	float sc = max(Sim.w, 1e-4);
	float3 up = -gDown;
	uint ns = min(Cnt.y, 6u);
	[loop] for (int rep = 0; rep < 2; rep++) {
		[loop] for (uint s = 0; s < ns; s++) {
			float4 A = Seg[s * 4];
			float3 ax = Seg[s * 4 + 1].xyz;
			float len = max(A.w, 1e-4);
			float3 q = mul(float4(p / sc, 1), Inv[s]).xyz - A.xyz;
			float tRaw = dot(q, ax) / len;
			if (tRaw < -0.12 || tRaw > 1.20) continue;
			float tc = saturate(tRaw);
			float3 rv = q - ax * (tc * len);
			float3 u = Seg[s * 4 + 2].xyz;
			float3 wv = Seg[s * 4 + 3].xyz;
			float3 upL = mul(up, (float3x3)Inv[s]);
			float upLen = length(upL);
			upL = upLen > 1e-4 ? upL / upLen : u;
			float h = dot(rv, upL);
			float side2 = max(dot(rv, rv) - h * h, 0);
			float phi = atan2(dot(upL, wv), dot(upL, u));
			float R = ProfR(s, tc, phi) + pad + 0.010;
			if (R < 0.03) R = 0.03;
			if (side2 >= R * R) continue;
			float half = sqrt(R * R - side2);
			if (h >= half) continue;
			p += up * ((half - h) * lie * sc);
		}
	}
	return p;
}

// 正面の股。腰から約 22cm まではほぼ動かさず、そこから下は普通に流す。
float GroinHold(float x, float y, float z)
{
	float az = abs(atan2(x - Mode.y, z - Mode.z));
	float front = smoothstep(1.15, 2.20, az);
	float drop = max(Grav.w - y, 0);
	return front * (1.0 - smoothstep(0.05, 0.22, drop));
}

// 筒は頂点ごと表面へ出す。スリットはシートのまま押したあとなので、ここで割らない。
float3 ClothFinish(float3 p, float pad, float sheet)
{
	if (sheet < 0.5) p = CollideBody(p, -pad, 1, 0u);
	p = LiftTop(p, pad, LieAmt());
	return p;
}

// その高さの肌の半径。角度は押し出す向きではなく、骨から布を見た向き。
float LegRadius(uint s, float t, float3 dirW)
{
	float3 ax = gPB[s] - gPA[s];
	float axl = length(ax);
	float3 axN = axl > 1e-4 ? ax / axl : float3(0, -1, 0);
	float3 perp = dirW - axN * dot(dirW, axN);
	float pl = length(perp);
	float3 dirP = pl > 1e-4 ? perp / pl : gPU[s];
	float phi = atan2(dot(dirP, gPW[s]), dot(dirP, gPU[s]));
	float fp = (phi / 6.2831853 + 0.5) * PF;
	fp = fmod(fp, (float)PF);
	if (fp < 0) fp += PF;
	float fl = floor(fp);
	uint i0 = (uint)fl % PF;
	uint i1 = (i0 + 1u) % PF;
	float ft = saturate(t) * (float)(NSLICE - 1);
	uint t0 = min((uint)ft, (uint)(NSLICE - 2));
	float a = ft - (float)t0;
	uint o0 = RAD0 + s * NSLICE * PF;
	float r0 = lerp(Prof[gProf0 + o0 + t0 * PF + i0], Prof[gProf0 + o0 + t0 * PF + i1], fp - fl);
	float r1 = lerp(Prof[gProf0 + o0 + (t0 + 1u) * PF + i0], Prof[gProf0 + o0 + (t0 + 1u) * PF + i1], fp - fl);
	return lerp(r0, r1, a);
}

float DistSeg(float3 p, float3 a, float3 b)
{
	float3 ab = b - a;
	float L2 = dot(ab, ab);
	float t = L2 > 1e-8 ? saturate(dot(p - a, ab) / L2) : 0.0;
	return length(p - (a + ab * t));
}

// 太もも・すねは細かい断面。足は骨に沿った半径。どちらも今の向きの肌。
float SegRadius(uint s, float t, float3 dirW)
{
	if (s < 4u) return LegRadius(s, t, dirW);
	float3 ax = gPB[s] - gPA[s];
	float axl = length(ax);
	float3 axN = axl > 1e-4 ? ax / axl : float3(0, -1, 0);
	float3 perp = dirW - axN * dot(dirW, axN);
	float pl = length(perp);
	float3 dirP = pl > 1e-4 ? perp / pl : gPU[s];
	float phi = atan2(dot(dirP, gPW[s]), dot(dirP, gPU[s]));
	return ProfR(s, saturate(t), phi);
}

// 左右の足が重なる向きは、相手の足へ押し込まず前後の隙間へ滑らせる。外向きはそのまま。
float3 SlideGap(float3 dir, uint s, uint kind, float3 O, float3 axN)
{
	uint pair = kind == 0u ? (s ^ 1u) : (kind == 1u ? ((s - 2u) ^ 1u) + 2u : ((s - 4u) ^ 1u) + 4u);
	if (pair >= gNLeg) return dir;
	float3 AB = gPB[pair] - gPA[pair];
	float L2 = dot(AB, AB);
	float tp = L2 > 1e-8 ? saturate(dot(O - gPA[pair], AB) / L2) : 0.0;
	float3 across = gPA[pair] + AB * tp - O;
	across -= axN * dot(across, axN);
	float al = length(across);
	if (al < 0.02) return dir;
	float3 aN = across / al;
	float into = dot(dir, aN);
	if (into <= 0.20) return dir;
	float near = al < 0.16 ? 1.0 : saturate((0.22 - al) / 0.06);
	if (near <= 0.0) return dir;
	float3 gap = cross(axN, aN);
	float gl = length(gap);
	if (gl < 1e-4) return dir;
	gap /= gl;
	if (dot(gap, dir) < 0.0) gap = -gap;
	float3 slid = dir + (gap - aN) * (into * near);
	float sl = length(slid);
	return sl > 1e-4 ? slid / sl : dir;
}

// 基準位置（初期配置・rest）や前布が脚の芯の逆側へ抜けないよう、バインド時の前後左右方向で脚の外へ出す。
void PushBindSide(inout float3 p, float3 bindPos, float pad, float sc)
{
	float lieFade = 1.0 - smoothstep(0.05, 0.40, LieAmt());
	if (lieFade <= 1e-4) return;
	float drop = max(Grav.w - bindPos.y, 0.0);
	if (drop <= 0.025) return;
	float wWaist = smoothstep(0.025, 0.10, drop) * lieFade;
	float3 dpm = p / sc - gWaist;
	float bindFront = Mode.z - bindPos.z;
	float bindLat = bindPos.x - Mode.y;
	float3 fwdB = -gEhz;
	float3 prefer = fwdB * bindFront + gEhx * bindLat;
	uint ns = min(gNLeg, 6u);
	float pMin = max(pad, 0.008);
	[loop] for (uint s = 0; s < ns; s++) {
		float3 AB = gPB[s] - gPA[s];
		float axl = length(AB);
		if (axl < 1e-4) continue;
		float3 axN = AB / axl;
		float t0 = s < 2u ? 0.02 : -0.10;
		float t1 = s >= 4u ? 1.50 : 1.18;
		float tRaw = dot(dpm - gPA[s], axN) / axl;
		if (tRaw < t0 || tRaw > t1) continue;
		float wt = smoothstep(t0, t0 + 0.08, tRaw) * (1.0 - smoothstep(t1 - 0.12, t1, tRaw));
		if (wt <= 1e-4) continue;
		float t = saturate(tRaw);
		float3 O = gPA[s] + axN * (tRaw * axl);
		float3 pr = prefer - axN * dot(prefer, axN);
		float pl = length(pr);
		if (pl < 1e-4) continue;
		pr /= pl;
		pr = SlideGap(pr, s, s < 2u ? 0u : (s < 4u ? 1u : 2u), gPA[s] + AB * t, axN);
		pr -= axN * dot(pr, axN);
		pl = length(pr);
		if (pl < 1e-4) continue;
		pr /= pl;
		float3 rel = dpm - O;
		rel -= axN * dot(rel, axN);
		float fwd = dot(rel, pr);
		float3 side = rel - pr * fwd;
		float lat = length(side);
		float3 sideDir = lat > 0.004 ? (side / lat) : gEhx;
		sideDir -= axN * dot(sideDir, axN);
		float sdl = length(sideDir);
		sideDir = sdl > 1e-4 ? sideDir / sdl : gEhx;
		float Rfwd = max(SegRadius(s, t, pr) + pMin, s >= 4u ? 0.048 : 0.060);
		float Rside = max(SegRadius(s, t, sideDir) + pMin + 0.014, s >= 4u ? 0.048 : 0.060);
		float nx = lat / Rside;
		if (nx >= 1.0) continue;
		float wx = smoothstep(1.0, 0.70, nx);
		float surf = Rfwd * sqrt(max(1.0 - nx * nx, 0.0));
		if (fwd >= surf) continue;
		float3 goal = O + side + pr * surf;
		float3 mv = (goal - dpm) * (wWaist * wt * wx);
		float ml = length(mv);
		if (ml > 0.25) mv *= 0.25 / ml;
		p += mv * sc;
		dpm = p / sc - gWaist;
	}
}

// 骨盤〜下腹部〜股間（パンツ）の3Dカプセルドーム。腰骨に固定され、前布が骨盤やパンツを貫通して背中側へ抜けるのを完全に防ぐ。
// 下半分は丸いドーム法線を持つため、仰向けで重力が掛かるとパンツの表面に沿って足先側（足と足の間）へ自然に滑り落ちる。
void CollidePelvis(inout float3 p, float3 bindPos, float pad, float sc)
{
	float cz = asfloat(Cnt.w);
	float top = Grav.w;
	float drop = max(top - bindPos.y, 0.0);
	float wDrop = smoothstep(0.004, 0.038, drop);
	if (wDrop <= 1e-4) return;
	float3 hp = mul(float4(p / max(sc, 1e-4), 1.0), HipInv).xyz;
	float yCap = top - 0.015;
	float Ry = 0.105;
	if (hp.y > top + 0.055 || hp.y < yCap - Ry - 0.010) return;
	float wTop = 1.0 - smoothstep(0.015, 0.055, hp.y - top);
	if (wTop <= 1e-4) return;

	float dx = hp.x - Mode.y;
	float dz = hp.z - cz;
	// バインドで前側（または後ろ側）の腰〜股間布が、中心軸を越えて逆側へ貫通しようとしても必ず元の面側へ押し返す。
	float bindFront = Mode.z - bindPos.z;
	if (drop < 0.24) {
		if (bindFront > 0.020 && dz > -0.025) dz = -0.025;
		else if (bindFront < -0.020 && dz < 0.025) dz = 0.025;
	}
	float rl = length(float2(dx, dz));
	float2 hDir = rl > 1e-4 ? (float2(dx, dz) / rl) : float2(0.0, bindFront >= 0.0 ? -1.0 : 1.0);
	float phi = atan2(hDir.x, hDir.y);
	float fp = (phi / 6.2831853 + 0.5) * 16.0;
	float fl = floor(fp);
	uint p0 = (uint)(fl + 16.0) % 16;
	uint p1 = (p0 + 1) % 16;
	float ring = lerp(Prof[gProf0 + 6 * PT * PF + p0], Prof[gProf0 + 6 * PT * PF + p1], fp - fl);
	float Rh = max(ring, 0.110) + max(pad, 0.016);
	Ry = 0.112;

	float3 goalL = hp;
	bool inside = false;
	if (hp.y >= yCap) {
		float curZ = hp.z - cz;
		float proj = dx * hDir.x + curZ * hDir.y;
		if (proj < Rh) {
			goalL.x = Mode.y + hDir.x * Rh;
			goalL.z = cz + hDir.y * Rh;
			inside = true;
		}
	} else {
		float dy = hp.y - yCap;
		float curZ = hp.z - cz;
		float hProj = dx * hDir.x + curZ * hDir.y;
		// 中心を逆側へ越えていた場合もドーム内部として扱い、自分の側のドーム表面へ出す。
		float qh = max(hProj, 0.0) / Rh;
		float qy = dy / Ry;
		float q2 = qh * qh + qy * qy;
		if (hProj < 0.0 || q2 < 1.0) {
			float sH = (hProj <= 0.005) ? (0.005 / Rh) : qh;
			float sY = dy / Ry;
			float sLen = length(float2(sH, sY));
			float2 u = sLen > 1e-4 ? (float2(sH, sY) / sLen) : float2(1.0, 0.0);
			goalL.x = Mode.y + hDir.x * (u.x * Rh);
			goalL.y = yCap + u.y * Ry;
			goalL.z = cz + hDir.y * (u.x * Rh);
			inside = true;
		}
	}
	if (!inside) return;
	float3 mvL = (goalL - hp) * (wDrop * wTop);
	p += mul(mvL, (float3x3)Hips) * sc;
}

// 脚のカプセル衝突。股関節付け根から足先まで隙間なく覆い、倒れ時は太ももの上を滑らせてウエストへのめくれ上がりを防ぐ。
void CollideLegs(inout float3 p, float3 prevP, float3 bindPos, float pad, float sc, bool allowWrap, int maxRep, out bool hitLeg, out float3 hitN)
{
	hitLeg = false;
	hitN = float3(0, 0, -1);
	float drop = max(Grav.w - bindPos.y, 0.0);
	if (drop <= 0.005) return;
	float wWaist = smoothstep(0.005, 0.042, drop);
	uint ns = min(gNLeg, 6u);
	float3 dpm = p / sc - gWaist;
	float3 prevDpm = prevP / sc - gWaist;
	float pMin = max(pad, 0.008);
	float lie = LieAmt();
	float standGuide = 1.0 - smoothstep(0.08, 0.38, lie);
	float wrapFade = allowWrap ? standGuide : 0.0;
	float bindFront = Mode.z - bindPos.z;
	float bindLat = bindPos.x - Mode.y;
	float3 prefer = (-gEhz) * bindFront + gEhx * bindLat;
	float prefLen = length(prefer);

	for (int rep = 0; rep < maxRep; rep++) {
		bool hitL = false, hitR = false;
		float bestPen = 0.0;
		float3 bestPush = 0;
		float3 sumPush = 0;
		[loop] for (uint s = 0; s < ns; s++) {
			float3 AB = gPB[s] - gPA[s];
			float axl = length(AB);
			if (axl < 1e-4) continue;
			float3 axN = AB / axl;
			float t0 = s < 2u ? -0.02 : -0.14;
			float t1 = s >= 4u ? 1.80 : 1.25;
			float tRaw = dot(dpm - gPA[s], axN) / axl;
			if (tRaw < t0 || tRaw > t1) continue;
			float tc = saturate(tRaw);
			float3 segPt = (tRaw < 0) ? gPA[s] : ((tRaw > 1) ? gPB[s] : (gPA[s] + axN * (tc * axl)));
			float3 rv = dpm - segPt;
			// 太もも・すねの途中ではカプセル軸に直交する方向へ押し出し、ハの字開脚でウエスト側へ押し上げないようにする。
			if (s < 4u && tRaw >= 0.0 && tRaw <= 1.0) rv -= axN * dot(rv, axN);
			float dist = length(rv);
			float rFloor = s >= 4u ? 0.046 : (s >= 2u ? 0.052 : 0.058);
			float maxR = max(gRmax[s] + pMin, rFloor);
			if (dist >= maxR) continue;
			float3 rDir = dist > 1e-4 ? (rv / dist) : gPU[s];
			float R = max(SegRadius(s, tc, rDir) + pMin, rFloor);
			if (dist >= R) continue;
			float3 dir = rDir;

			// 前布が前に突き出た膝・太ももを左右に股裂きして穴が開くのを防ぐため、腰基準の表側（前布は腹側 -Hips[2]、後ろ布は背側 +Hips[2]）へ揃えて乗せる。
			float faceKeep = (1.0 - smoothstep(0.55, 0.92, lie));
			if (s < 4u && abs(bindFront) > 0.030 && faceKeep > 0.02) {
				float3 hipFwd = -Hips[2].xyz;
				float hfl = length(hipFwd);
				hipFwd = hfl > 1e-4 ? (hipFwd / hfl) : (-gEhz);
				float3 facePref = hipFwd * (bindFront >= 0.0 ? 1.0 : -1.0) + Hips[0].xyz * (bindLat * 0.35);
				facePref -= axN * dot(facePref, axN);
				float fpl = length(facePref);
				if (fpl > 1e-4) {
					facePref /= fpl;
					facePref = SlideGap(facePref, s, s < 2u ? 0u : 1u, gPA[s] + AB * tc, axN);
					facePref -= axN * dot(facePref, axN);
					fpl = length(facePref);
					if (fpl > 1e-4) {
						facePref /= fpl;
						float sideDot = dot(dir, facePref);
						float guideW = max(smoothstep(0.35, -0.15, sideDot), smoothstep(R * 0.70, R * 0.30, dist)) * faceKeep;
						if (guideW > 1e-3) {
							if (sideDot < -0.45 && guideW > 0.45) dir = facePref;
							else {
								float3 bl = lerp(dir, facePref, guideW);
								float bll = length(bl);
								dir = bll > 1e-4 ? (bl / bll) : facePref;
							}
						}
					}
				}
			} else if (wrapFade > 0.01 && Limb[s].w > 0.004 * sc) {
				float3 prevSeg = segPt - Limb[s].xyz / sc;
				float3 prevRv = prevDpm - prevSeg;
				prevRv -= axN * dot(prevRv, axN);
				float prl = length(prevRv);
				if (prl > 0.006) {
					float3 prevN = prevRv / prl;
					float cs = dot(rDir, prevN);
					float wrapW = max(smoothstep(0.45, 0.05, cs), smoothstep(R * 0.78, R * 0.42, dist)) * wrapFade;
					if (wrapW > 1e-3) {
						float3 blDir = (cs < -0.55 && wrapW > 0.5) ? prevN : lerp(dir, prevN, wrapW);
						float bll = length(blDir);
						dir = bll > 1e-4 ? (blDir / bll) : prevN;
					}
				}
			}

			// 倒れ時、ハの字に開いた太ももからウエスト方向（+gHipUp）へクサビのようにたくし上げる成分を除去し、横・足の間へ滑らせる。
			if (lie > 0.10 && s < 4u) {
				float upComp = dot(dir, gHipUp);
				if (upComp > 0.0) {
					float3 trimmed = dir - gHipUp * (upComp * smoothstep(0.10, 0.40, lie));
					float tl = length(trimmed);
					if (tl > 1e-4) dir = trimmed / tl;
				}
			}

			float pen = (R - dist) * wWaist;
			if (pen <= 1e-5) continue;
			float3 push = dir * pen;
			if ((s & 1u) == 0u) hitL = true; else hitR = true;
			sumPush += push;
			if (pen > bestPen) { bestPen = pen; bestPush = push; }
		}
		if (bestPen <= 1e-5) break;
		if (hitL && hitR && gNLeg >= 2u) {
			float3 mid = 0.5 * (gPA[0] + gPA[1]);
			float3 fwdEsc = (prefLen > 0.02 && standGuide > 0.3) ? normalize(prefer) : gEhz;
			float sgn = (prefLen > 0.02 && standGuide > 0.3) ? 1.0 : clamp(dot(dpm - mid, fwdEsc) / 0.025, -1.0, 1.0);
			if (abs(sgn) < 0.25) sgn = sgn >= 0.0 ? 0.25 : -0.25;
			float3 esc = fwdEsc * sgn;
			float3 mixed = sumPush * 0.45 + esc * (bestPen * 0.65);
			float ml = length(mixed);
			bestPush = ml > 1e-5 ? mixed * (bestPen / ml) : esc * bestPen;
		}
		p += bestPush * sc;
		dpm = p / sc - gWaist;
		hitLeg = true;
		float bl = length(bestPush);
		if (bl > 1e-5) hitN = bestPush / bl;
	}
}

// 左手と右手。手に入り込まないよう外へ出し、接触時は摩擦と風の接線成分で手と腰の間をすり抜ける。
void HandCollide(inout float3 p, inout float3 vel, float3 windVec, float dt, float sc, out bool hitHand, out float3 hitN)
{
	hitHand = false;
	hitN = float3(0, 0, -1);
	float pad = 0.003 * sc;
	float3 waist = gWaist * sc;
	[unroll] for (int i = 0; i < 2; i++) {
		float4 a = Hand[i * 2];
		float4 b = Hand[i * 2 + 1];
		if (a.w < 0.5) continue;
		float3 c = (a.xyz + b.xyz) * 0.5;
		float3 e = max((b.xyz - a.xyz) * 0.5, float3(0.010, 0.010, 0.010) * sc);
		float3 d = p - c;
		float3 q = abs(d) - e;
		float outside = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
		if (outside >= pad) continue;
		hitHand = true;
		float3 n;
		if (max(q.x, max(q.y, q.z)) < 0.0) {
			float dl = length(d);
			n = dl > 1e-5 ? (d / dl) : float3(0.0, 0.0, -1.0);
		} else {
			float3 g = max(q, 0.0) * float3(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0, d.z >= 0.0 ? 1.0 : -1.0);
			float gl = length(g);
			n = gl > 1e-5 ? (g / gl) : normalize(d + float3(0.0, 0.0, 1e-4));
		}
		// 手が太ももの前〜横にあって布を脚の奥側へ押し込みそうなときは、手と腰・風のすき間側へ滑らかに逃がす。
		float3 toWaist = waist - p;
		toWaist -= gDown * dot(toWaist, gDown);
		float twl = length(toWaist);
		if (twl > 1e-4) {
			float3 inDir = toWaist / twl;
			float inDot = dot(n, inDir);
			float steer = smoothstep(0.35, 0.75, inDot);
			if (steer > 1e-3) {
				float3 sideH = cross(inDir, gDown);
				float sl = length(sideH);
				if (sl > 1e-4) sideH /= sl;
				float sAmt = clamp(dot(d, sideH) / (0.018 * sc), -1.0, 1.0);
				float3 n2 = n + sideH * (sAmt * 0.65 * steer);
				float n2l = length(n2);
				if (n2l > 1e-4) n = n2 / n2l;
			}
		}
		p += n * (pad - outside);
		hitN = n;
		if (dt > 0.0) {
			float vn = dot(vel, n);
			if (vn < 0.0) vel -= n * vn;
			float3 velT = vel - n * dot(vel, n);
			velT *= exp(-3.5 * dt);
			float3 windT = windVec - n * dot(windVec, n);
			vel = n * dot(vel, n) + velT + windT * (0.45 * dt);
		}
	}
}

void FloorCollide(inout float3 p, out bool hitFloor)
{
	hitFloor = false;
	if (gFloor > 1e4) return;
	float d = dot(p, gDown);
	if (d > gFloor) {
		p -= gDown * (d - gFloor);
		hitFloor = true;
	}
}

float3 HandAir(float3 p)
{
	float3 v = 0, hn = 0;
	bool h = false;
	HandCollide(p, v, 0, 0.0, max(Sim.w, 1e-4), h, hn);
	return p;
}

float3 LegPush(float3 p, float bindX, float bindY, float bindZ, float slitCode, float pad, float carry)
{
	float sc = max(Sim.w, 1e-4) + (slitCode + carry) * 0.0;
	float3 bp = float3(bindX, bindY, bindZ);
	CollidePelvis(p, bp, pad, sc);
	if (max(Grav.w - bindY, 0.0) <= 0.005) return p;
	bool hl = false, hh = false, hf = false;
	float3 nl = 0, nh = 0, v = 0;
	CollideLegs(p, p, bp, pad, sc, false, 3, hl, nl);
	HandCollide(p, v, 0, 0.0, sc, hh, nh);
	if (hh) CollideLegs(p, p, bp, pad, sc, false, 2, hl, nl);
	FloorCollide(p, hf);
	return p;
}

// 脚の最小半径は 2.6cm。1 回の移動を 1.2cm にすると、表面から中心までは越えられない。
float3 Approach(float3 p, float3 goal)
{
	float3 d = goal - p;
	float L = length(d);
	float cap = 0.012 * max(Sim.w, 0.2);
	if (L > cap) d *= cap / L;
	return p + d;
}

// カットで骨に着せた短いスカート。スキニング位置は肌の内側に残ることがあるので、外向きへ 8mm だけ離す。
// すでに外へ出ている布をさらに膨らませない。立ちの長い裾は wear が 0 なのでここを通らない。
float3 WearClear(float3 p, float3 nrm, float wear)
{
	if (wear < 0.05) return p;
	float sc = max(Sim.w, 1e-4);
	float3 radial = p / sc - gWaist;
	radial -= gDown * dot(radial, gDown);
	float rl = length(radial);
	float3 outD = rl > 1e-4 ? radial / rl : float3(0, 0, 1);
	float3 n = nrm;
	float nl = length(n);
	n = nl > 1e-4 ? n / nl : outD;
	if (dot(n, outD) < 0.25) n = outD;
	else n = normalize(lerp(outD, n, 0.65));
	return p + n * (0.008 * wear * sc);
}

float3 HipOut(float3 p)
{
	float sc = max(Sim.w, 1e-4);
	float3 hp = mul(float4(p / sc, 1.0), HipInv).xyz;
	CollidePelvis(p, hp, 0.010, sc);
	return p;
}

// 中にいる点だけ、今いる側の面へ出す。外にいる点は動かさない。
float3 Safe(float3 p, ClothN c)
{
	float fly = saturate(Wind.w);
	float allow = fly > 0.5 ? -0.022 : min(c.q.y, -0.012);
	uint pan = c.q.z > 1.5 ? 2u : (c.q.z > 0.5 ? 1u : 0u);
	return HipOut(CollideBody(p, allow, 1, pan));
}

// 体より外へ出して着せる。元の大きさ（重力＋風）へ縮め、縮めた先が中なら表面に載せる。
float3 StepOut(float3 p, float3 goal, ClothN c)
{
	p = Safe(p, c);
	p = Approach(p, goal);
	return Safe(p, c);
}

float3 HipPos(float3 x)
{
	return mul(float4(x, 1), Hips).xyz * Sim.w;
}

// 格子の点を、頂点と同じ骨の重みでスキニングする。腰の輪には置き換えない。
float3 BonePos(ClothN c)
{
	float4x4 acc = (float4x4)0;
	[unroll] for (int k = 0; k < 4; k++) acc += Bones[min(c.j[k], 63u)] * c.w[k];
	return mul(float4(c.x.xyz, 1), acc).xyz;
}

// 飛行中、前の裾が靴の骨に乗って遠くまで運ばれていたら、腰から垂れた位置へ戻す。
// 後ろと横は触らない。差が 3.5cm 未満（脚に沿っているだけ）のときは戻さない。
float3 HipHung(ClothN c)
{
	float3 h = mul(float4(c.x.xyz, 1), Hips).xyz;
	if (c.p.w < 0.5) {
		float below = max(Grav.w - c.x.y, 0);
		float sag = saturate((below - 0.04) / 0.50) * 0.040;
		h += GravDown() * sag;
	}
	return h * Sim.w;
}

// 両脇スリットの前布。腰の輪の位置を今の腰へ置き、そこから重力の向きへ上端からの丈だけ垂らす。
// バインドの脚は足首ほど後ろへ傾いているのでその分を前へ戻し、飛行停止で脚が前へ出ているときは脚より奥へ引かない。
// 強い風とそよ風と移動を足して、布全体を同じ向きへ。点ごとに向きを変えると一枚の布が皺になる。
// TacWind で 0.48 倍したあとの長さ。単位はメートル。裾（fall=1）は
// そよ風 0.15 → 8cm、強風 0.65 → 16cm、暴風 1.30 → 22cm。
// 1.55 を超えるのは舞台側（作成プレビューはここまで来ない）。暴風で裾が約 50cm。
float3 AirPush(float fall)
{
	if (fall <= 0.001) return 0;
	float3 w = Wind.xyz + Air.xyz;
	float amp = length(w);
	float3 r = 0;
	if (amp > 0.02) {
		float reach = min(amp, 0.15) * (0.095 / 0.15);
		if (amp > 0.15) reach += min(amp - 0.15, 0.50) * ((0.195 - 0.095) / 0.50);
		if (amp > 0.65) reach += min(amp - 0.65, 0.65) * ((0.275 - 0.195) / 0.65);
		if (amp > 1.55) reach += min(amp - 1.55, 2.2) * ((0.64 - 0.275) / 2.2);
		r = (w / amp) * reach * fall;
	}
	r *= 0.94 + 0.06 * sin(ClothTime() * 2.4);
	float ly = abs(Air.w);
	float yReach = min(ly, 0.55) * (0.09 / 0.55);
	if (ly > 0.62) yReach += min(ly - 0.62, 1.1) * 0.18;
	r.y += (Air.w >= 0 ? yReach : -yReach) * fall;
	return r;
}

// 風のまわりの乱れ。周と丈で位相が違うので、裾は一枚板では動かない。
// 周波数は低く、フレームごとの乱数にはしない。布の慣性で波が送られる。
float3 GustRipple(ClothN c, float3 dir, float3 side, float3 up, float reach, float flow, float free)
{
	float drop = max(Grav.w - c.x.y, 0);
	float az = atan2(c.x.x - Mode.y, c.x.z - Mode.z);
	float seed = az * 1.7 + drop * 8.0;
	float t = ClothTime();
	float across = sin(t * 0.95 + seed) * 0.50 + sin(t * 2.15 + seed * 1.3) * 0.32 + sin(t * 0.41 + seed * 0.35) * 0.18;
	float along = sin(t * 0.72 + seed * 0.8 + 1.4) * 0.62 + sin(t * 1.65 + az * 2.0 + drop * 6.0) * 0.38;
	float lift = sin(t * 1.35 + seed * 1.1);
	float amp = reach * flow * free;
	return side * (across * 0.28 * amp) + dir * (along * 0.16 * amp) + up * (lift * 0.10 * amp);
}

float3 FrontHang(float3 x, float3 skinned)
{
	float drop = max(Grav.w - x.y, 0);
	// 前布は脚の前へ。0.038 だと飛行で腿に貼り付いたまま垂れない。
	float3 a = float3(x.x, Grav.w, x.z - 0.082 * saturate(drop / 0.45));
	float3 hung = (mul(float4(a, 1), Hips).xyz + GravDown() * drop) * Sim.w;
	return hung;
}

// 両脇スリットの後ろ布。上端はスキニングした位置のまま繋ぎ、そこから下は布自身の丈だけ垂らす。
// スキニング位置へさらに足すと、前より後ろが長くなり、裾に段も出る。中央は脚と脚の間へ寄せる。
float3 BackHang(float3 x, float ySlit, float3 skinned)
{
	float drop = max(ySlit - x.y, 0);
	float t = smoothstep(0.0, 0.22, drop);
	float mid = 1.0 - smoothstep(0.02, 0.16, abs(x.x - Mode.y));
	float3 a = float3(lerp(x.x, Mode.y, 0.18 * t * mid), ySlit, x.z - 0.018 * mid * t);
	float3 hung = (mul(float4(a, 1), Hips).xyz + GravDown() * drop) * Sim.w;
	return lerp(skinned, hung, t);
}

// 腰の傾きでスカート全体が横や後ろへ振れないよう、腰の中心から水平の向きだけを保ち、縦は重力の向きへ垂らす。
// 横たわりで前後軸が重力に重なっても、輪の幅は水平のまま残す。
float3 LevelHang(float3 x)
{
	float drop = Grav.w - x.y;
	float3 ehx, ehz, down;
	FlatAxes(ehx, ehz, down);
	float3 waistC = mul(float4(Mode.y, Grav.w, Mode.z, 1), Hips).xyz;
	float3 rx = ehx * length(Hips[0].xyz);
	float3 rz = ehz * length(Hips[2].xyz);
	return (waistC + rx * (x.x - Mode.y) + rz * (x.z - Mode.z) + down * drop) * Sim.w;
}

// 腰の帯から滑らかに垂らし、倒れているときは腰骨に沿った筒から重力で落とす。
float3 HangPose(float3 x)
{
	float lie = LieAmt();
	float3 lvl = LevelHang(x);
	float3 rig = mul(float4(x, 1.0), Hips).xyz * Sim.w;
	return lerp(lvl, rig, lie);
}

// 腰の付け根（3cm）だけ固定する。膝上からすぐ自由に動くようにする。
float HangIn(float y)
{
	return smoothstep(0.0, 0.055, Grav.w - y);
}

// 腰の帯だけ固定する。太ももの付け根から布は動いてよい。
float PelvisCover(float y)
{
	return 1.0 - smoothstep(0.015, 0.055, Grav.w - y);
}

// 膝上からしっかり風を受け、ミニスカートの延長部も含めて自然になびく。
float Flow(float y)
{
	float drop = max(Grav.w - y, 0.0);
	return smoothstep(0.015, 0.14, drop) * (0.76 + 0.24 * saturate((drop - 0.14) / 0.36));
}

// 正面の股間は自由布として重力と脚の当たりで自然に覆い、風では膝上から靡かせる。
float FrontVeil(ClothN c)
{
	return c.x.x * 0.0;
}

float ShoePin(float3 p)
{
	float best = 0;
	uint ns = min(Cnt.y, 6u);
	[loop] for (uint s = 4u; s < ns; s++) {
		float4 A = Seg[s * 4];
		float3 ax = Seg[s * 4 + 1].xyz;
		float len = max(A.w, 1e-4);
		float3 q = mul(float4(p / max(Sim.w, 1e-4), 1), Inv[s]).xyz - A.xyz;
		float t = dot(q, ax) / len;
		if (t < -0.08 || t > 1.35) continue;
		float tc = clamp(t, 0, 1);
		float3 rv = q - ax * (tc * len);
		float dist = length(rv);
		float k = Seg[s * 4 + 2].w > 0 ? smoothstep(0, 1, (tc - Seg[s * 4 + 1].w) / max(Seg[s * 4 + 2].w, 1e-4)) : 1;
		float phi = atan2(dot(rv, Seg[s * 4 + 3].xyz), dot(rv, Seg[s * 4 + 2].xyz));
		float R = ProfR(s, tc, phi) * k;
		if (R < 0.03) R = 0.05;
		float gap = dist - R;
		float pin = 1 - smoothstep(0.0, 0.12, gap);
		if (pin > best) best = pin;
	}
	return best;
}

// 体に沿った位置が先。脚から離れた余り布だけ、前後同じだけ真下へ足す。
float Clearance(float3 p)
{
	float best = 1e3;
	uint ns = min(Cnt.y, 6u);
	[loop] for (uint s = 0; s < ns; s++) {
		float4 A = Seg[s * 4];
		float3 ax = Seg[s * 4 + 1].xyz;
		float len = max(A.w, 1e-4);
		float3 q = mul(float4(p / max(Sim.w, 1e-4), 1), Inv[s]).xyz - A.xyz;
		float t = dot(q, ax) / len;
		if (t < Seg[s * 4 + 1].w - 0.02 || t > 1.15) continue;
		float tc = clamp(t, 0, 1);
		float3 rv = q - ax * (tc * len);
		float dist = length(rv);
		float k = Seg[s * 4 + 2].w > 0 ? smoothstep(0, 1, (tc - Seg[s * 4 + 1].w) / max(Seg[s * 4 + 2].w, 1e-4)) : 1;
		float phi = atan2(dot(rv, Seg[s * 4 + 3].xyz), dot(rv, Seg[s * 4 + 2].xyz));
		float R = ProfR(s, tc, phi) * k + 0.01;
		if (R < 0.03) R = 0.03;
		float gap = dist - R;
		if (gap < best) best = gap;
	}
	return best;
}

// 目標は腰から垂らすので、脚に貼る余り垂れは使わない。残しておくと二重に落ちて裂ける。
float FlyDrape(ClothN c, float3 skinned)
{
	return 0 * c.p.w * skinned.x;
}
float3 NodeTarget(ClothN c)
{
	float3 bone = BonePos(c) * Sim.w;
	if (c.p.w > 0.5) return bone;
	float cover = PelvisCover(c.x.y);
	// 腰の中心から滑らかな円錐として垂らし、太ももや骨盤との接触はすべて連続した衝突処理に任せる（目標自体に段差を作らない）。
	float hang = HangIn(c.x.y) * (1.0 - saturate(Mode.x)) * (1.0 - cover);
	float3 hung = HangPose(c.x.xyz);
	float lie = LieAmt();
	hung += GravDown() * (saturate((Grav.w - c.x.y) / 0.45) * 0.015 * (1.0 - lie) * Sim.w);
	return lerp(bone, hung, hang);
}

// 風下へ裾を寄せる。強い風は揃えて傾け、そよ風は点ごとにずらす。上下も足す。脚の中はあとで押し出す。
float3 WindPush(ClothN c)
{
	if (c.p.w > 0.5) return 0;
	float wgt = saturate(c.p.z) * (1.0 - PelvisCover(c.x.y));
	float3 r = AirPush(wgt);
	return r * Sim.w;
}

// 腰の軸から少し外へ膨らませ、素体の外へ出す。着せる最初の形。
float3 LooseOf(float3 t)
{
	float3 ph = mul(float4(t / Sim.w, 1), HipInv).xyz;
	float2 rd = float2(ph.x - Mode.y, ph.z - Mode.z);
	float rl = length(rd);
	if (rl > 1e-4) {
		ph.x += rd.x / rl * 0.016;
		ph.z += rd.y / rl * 0.016;
	}
	return Collide(mul(float4(ph, 1), Hips).xyz * Sim.w, -0.012);
}

// 足が前へ出るとき、表面の少し上にある布だけを、その移動の一部だけ連れていく。
// 足が布から離れる向きのときはほとんど連れていかない。戻りは重力と風に任せる。
float3 LimbDrag(float3 p, ClothN c)
{
	if (c.p.w > 0.5) return float3(0, 0, 0);
	float3 best = float3(0, 0, 0);
	float bestW = 0;
	uint ns = min(Cnt.y, 6u);
	[loop] for (uint s = 0; s < ns; s++) {
		float dl = Limb[s].w;
		if (dl < 1e-5) continue;
		float3 disp = Limb[s].xyz;
		float4 A = Seg[s * 4];
		float3 ax = Seg[s * 4 + 1].xyz;
		float3 q = mul(float4(p / Sim.w, 1), Inv[s]).xyz - A.xyz;
		float t = dot(q, ax) / max(A.w, 1e-4);
		if (t < Seg[s * 4 + 1].w - 0.08 || t > 1.35) continue;
		float tc = clamp(t, 0, 1);
		float3 rv = q - ax * (tc * A.w);
		float dist = length(rv);
		float tk = Seg[s * 4 + 2].w > 0 ? smoothstep(0, 1, (tc - Seg[s * 4 + 1].w) / Seg[s * 4 + 2].w) : 1;
		float phi = atan2(dot(rv, Seg[s * 4 + 3].xyz), dot(rv, Seg[s * 4 + 2].xyz));
		float R = ProfR(s, tc, phi) * tk;
		if (R < 0.02) R = 0.045;
		float gap = dist - R;
		float near = 1 - smoothstep(0.0, s >= 4u ? 0.16 : 0.12, gap);
		if (near <= 0.001) continue;
		float3 local = A.xyz + ax * (tc * A.w);
		float3 closest = mul(float4(local, 1), Pal[s]).xyz * Sim.w;
		float3 ph = mul(float4(p / Sim.w, 1), HipInv).xyz;
		float3 ch = mul(float4(closest / Sim.w, 1), HipInv).xyz;
		float rise = ph.y - ch.y;
		// 接触の少し上。足は靴の下から甲の上まで、前に出した布を連れていく。
		float band = s >= 4u
			? smoothstep(0.00, 0.04, rise) * (1 - smoothstep(0.18, 0.34, rise))
			: smoothstep(-0.02, 0.035, rise) * (1 - smoothstep(0.12, 0.26, rise));
		if (band <= 0.001) continue;
		float3 outDir = p - closest;
		float ol = length(outDir);
		outDir = ol > 1e-4 ? outDir / ol : float3(0, 0, 1);
		float sep = dot(outDir, disp / dl);
		// 足が布へ食い込むときだけ押す。離れる向きは連れて戻さず、重力と風に任せる。
		if (sep <= 0.02) continue;
		float couple = lerp(0.10, 0.28, saturate(sep));
		float g = near * band * couple;
		if (g > bestW) { bestW = g; best = disp * g; }
	}
	return best;
}

// 歩き・攻撃・飛行の一瞬を、直前 3 フレームの移動でならす。1 フレームだけ大きい差は速度にしない。
float3 Trail(float3 d0, float3 d1, float3 d2)
{
	float3 v = d0 * 0.50 + d1 * 0.30 + d2 * 0.20;
	float sp = length(d0);
	float vl = max(length(v), 1e-5);
	if (sp > vl * 1.75) v *= (vl * 1.75) / sp;
	return v;
}

float Rush()
{
	float m = 0;
	[unroll] for (int s = 0; s < 6; s++) m = max(m, Limb[s].w);
	return saturate(m / (0.055 * max(Sim.w, 0.2)));
}

// 飛行中（止まっていても翼は羽ばたく）。前からの微風乱気流で前後・左右・上下に揺れる。
// 前（角度が ±π）と後ろ（0）で位相と強さをずらし、飛行移動中は重さと上下動で裾がびよんびよん波打つ。
// 後ろスカートも脚が後ろへ流れたとき、重力で股の間（足と足の間）へ自然に垂れ落ちる成分を持つ。
float3 FlightGust(ClothN c, float flow)
{
	float az = atan2(c.x.x - Mode.y, c.x.z - Mode.z);
	float front = smoothstep(1.05, 2.30, abs(az));
	float backMid = (1.0 - smoothstep(0.0, 1.05, abs(az))) * (1.0 - smoothstep(0.02, 0.14, abs(c.x.x - Mode.y)));
	float drop = max(Grav.w - c.x.y, 0);
	float ph = lerp(1.35, 0.05, front) + sin(az) * 0.45 - drop * 6.5;
	float t = ClothTime();
	float flyMove = saturate(Wind.w - 1.0);
	float boost = 1.0 + 1.15 * flyMove;
	float gZ = sin(t * 3.8 + ph) * 0.65 + sin(t * 7.6 + ph * 1.3 + 1.1) * 0.35;
	float gX = sin(t * 2.9 + az * 1.5 + 0.7) * 0.55 + cos(t * 5.4 - ph) * 0.25;
	float gY = sin(t * 6.8 + ph * 0.8) * 0.60 + sin(t * 10.5 - drop * 9.0 + az * 0.5) * (0.40 + 0.55 * front * flyMove);
	float3 down = GravDown();
	float3 wave = float3(gX * 0.32, 0, gZ) - down * (gY * (0.45 + 0.45 * front * flyMove));
	float3 between = ( -gEhz * (0.38 * flyMove) + down * (0.20 * flyMove) ) * backMid;
	return (wave * (0.00060 * boost) + between * 0.00075) * saturate(flow) * Sim.w;
}

[numthreads(64, 1, 1)]
void CSClothStep(uint id : SV_DispatchThreadID, uint gid : SV_GroupIndex)
{
	LoadClothCB(gid);
	BuildLegs(gid);
	if (id >= min(Cnt.x, (uint)NMAX)) return;
	ClothN c = Cn[gCn0 + id];
	float sc = max(Sim.w, 0.2);
	float3 rest = NodeTarget(c);
	float3 prevPos = mul(St[gSt0 + id].xyz, (float3x3)Delta);
	float3 prevRest = mul(St[gSt0 + NMAX + id].xyz, (float3x3)Delta);
	float3 vel = mul(St[gSt0 + 2 * NMAX + id].xyz, (float3x3)Delta);
	float flow = Flow(c.x.y);
	float dt = clamp(Sim.x, 1e-4, 1.0 / 24.0);
	float lie = LieAmt();
	if (Sim.z > 0.5 || c.p.w > 0.5) {
		float3 p0 = rest;
		if (Sim.z > 0.5 && c.p.w <= 0.5) {
			CollidePelvis(p0, c.x.xyz, 0.014, sc);
			bool hl0 = false, hf0 = false;
			float3 nl0 = 0;
			CollideLegs(p0, p0, c.x.xyz, 0.016, sc, false, 3, hl0, nl0);
			FloorCollide(p0, hf0);
		}
		St[gSt0 + id] = float4(p0, 0);
		St[gSt0 + NMAX + id] = float4(rest, 0);
		St[gSt0 + 2 * NMAX + id] = float4(0, 0, 0, 0);
		St[gSt0 + 3 * NMAX + id] = float4(p0 - rest, 0);
		St[gSt0 + 4 * NMAX + id] = float4(p0, 0);
		St[gSt0 + 5 * NMAX + id] = float4(mul(float4(p0 / sc, 1.0), HipInv).xyz, 0);
		St[gSt0 + 6 * NMAX + id] = float4(0, 0, 0, 0);
		return;
	}
	// 腰骨の並進・回転に追従した位置 prevHipWorld を基準とし、ワールド空間の慣性遅れ lag を全ノード滑らかに上限クランプする。
	// これで歩行・風では自然に揺れつつ、倒れる・起き上がる急回転でも布が引き裂かれたり骨盤の反対側へ取り残されない。
	float3 prevHipWorld = mul(float4(St[gSt0 + 5 * NMAX + id].xyz, 1.0), Hips).xyz * sc;
	float3 prevVelHip = mul(St[gSt0 + 6 * NMAX + id].xyz, (float3x3)Hips) * sc;
	float3 rawLag = prevPos - prevHipWorld;
	float rawLen = length(rawLag);
	float inertiaKeep = (0.28 + 0.72 * flow) * (1.0 - 0.55 * smoothstep(0.015 * sc, 0.075 * sc, rawLen));
	float3 lag = rawLag * inertiaKeep;
	float lagLen = length(lag);
	float maxLag = 0.018 * sc;
	if (lagLen > maxLag) lag *= maxLag / lagLen;
	float3 posStart = prevHipWorld + lag;
	float velMix = smoothstep(0.012 * sc, 0.055 * sc, rawLen);
	vel = lerp(vel, prevVelHip * 0.45, velMix);

	float mob = flow * (0.66 + 0.34 * saturate(c.p.z));
	float drop = max(Grav.w - c.x.y, 0.0);
	bool isSlit = c.q.z > 0.5;
	float hemOpen = smoothstep(0.04, 0.24, drop);

	float3 gust = Wind.xyz + Air.xyz;
	float wAmp = length(gust);
	float3 gDir = wAmp > 0.02 ? (gust / wAmp) : float3(0, 0, 1);
	float3 up = -GravDown();
	float3 side = cross(gDir, up);
	float sl = length(side);
	side = sl > 1e-4 ? (side / sl) : float3(1, 0, 0);

	// 空気の力（風方向・乱流・ふわっと広がる成分）。非伸張チェーンの上で 30〜65 度程よく靡く力にする。
	float windGain = 3.6 * (1.0 - exp(-wAmp * 0.65)) + min(wAmp, 4.0) * 0.55;
	float slitBoost = isSlit ? (1.0 + 0.28 * hemOpen) : (1.0 + 0.10 * hemOpen);
	if (c.q.z > 1.5) slitBoost *= lerp(0.75, 1.02, hemOpen);
	float3 fWind = (wAmp > 0.02 ? gDir * windGain : float3(0, 0, 0)) * slitBoost * sc;
	fWind.y += Air.w * 2.2 * sc;
	float3 ripple = GustRipple(c, gDir, side, up, windGain * 0.12, isSlit ? 1.25 : 0.90, 1.0) * sc;
	float3 bindLocal = float3(c.x.x - Mode.y, 0.0, c.x.z - Mode.z);
	float3 bindFlat = gEhx * bindLocal.x + gEhz * bindLocal.z;
	float3 bindRig = mul(bindLocal, (float3x3)Hips);
	float3 bindOut = lerp(bindFlat, bindRig, lie);
	float bol = length(bindOut);
	bindOut = bol > 1e-4 ? (bindOut / bol) : side;
	float flareWave = 0.65 + 0.35 * sin(ClothTime() * 1.9 + atan2(c.x.x - Mode.y, c.x.z - Mode.z) * 2.0 + drop * 5.0);
	float flare = (windGain * (isSlit ? 0.20 : 0.12) + (Wind.w > 0.5 ? (isSlit ? 0.9 : 0.45) : 0.0)) * hemOpen * flareWave * sc;
	fWind += ripple + bindOut * flare;
	if (Wind.w > 0.5) fWind += FlightGust(c, flow) * (isSlit ? 900.0 : 650.0);

	// 重力と形状復元。倒れ時は重力方向(gDown)には柔らかくして太ももの上から足の間・横へ自然に滑り落とさせ、
	// 腰の頭〜足軸(gHipUp)方向には本来の丈を保ってウエストへのめくれ上がりや尻裏への折り返しを防ぐ。
	float3 fGrav = Grav.xyz * sc;
	float kAxial = lerp(14.0, 7.5, flow);
	float kLat = lerp(10.0, 2.4, flow) * (1.0 - 0.40 * lie);
	float kDown = lerp(10.0, 1.4, flow) * (1.0 - 0.85 * lie);
	float3 downOrtho = gDown - gHipUp * dot(gDown, gHipUp);
	float dol = length(downOrtho);
	downOrtho = dol > 1e-4 ? (downOrtho / dol) : float3(0, 0, 0);
	float rush = Rush();
	float drag = lerp(2.5, 1.75, saturate(wAmp * 0.4 + rush * 0.4));
	if (lie > 0.25) drag = max(drag, 3.5);
	float3 pos = posStart;
	float h = dt * 0.5;
	[unroll] for (int sub = 0; sub < 2; sub++) {
		float3 toRest = rest - pos;
		float3 toAx = gHipUp * dot(toRest, gHipUp);
		float3 rem = toRest - toAx;
		float3 toDn = downOrtho * dot(rem, downOrtho);
		float3 toLat = rem - toDn;
		float3 fShape = toAx * lerp(kDown, kAxial, lie) + toDn * kDown + toLat * kLat;
		float3 fSub = (fGrav + fShape + fWind * mob) * saturate(0.28 + 0.72 * flow);
		vel = vel * exp(-drag * h) + fSub * h;
		float vl = length(vel);
		float maxV = 1.8 * sc;
		if (vl > maxV) vel *= maxV / vl;
		pos += vel * h;
		float3 pBefore = pos;
		CollidePelvis(pos, c.x.xyz, 0.014, sc);
		bool hh = false;
		float3 hn = 0;
		HandCollide(pos, vel, fWind * mob, h, sc, hh, hn);
		bool hl = false, hf = false;
		float3 nl = 0;
		CollideLegs(pos, posStart, c.x.xyz, 0.014, sc, false, 2, hl, nl);
		FloorCollide(pos, hf);
		float3 corr = pos - pBefore;
		float cl = length(corr);
		if (cl > 1e-6) {
			float3 cn = corr / cl;
			float vn = dot(vel, cn);
			if (vn < 0.0) vel -= cn * vn;
		}
	}

	float wear = WearSkirt(c.x.y);
	if (wear > 0.001) {
		pos = lerp(pos, rest, wear);
		vel *= (1.0 - wear);
	}
	St[gSt0 + id] = float4(pos, 0);
	St[gSt0 + NMAX + id] = float4(rest, 0);
	St[gSt0 + 2 * NMAX + id] = float4(vel, 0);
	St[gSt0 + 3 * NMAX + id] = float4(pos - rest, 0);
	St[gSt0 + 4 * NMAX + id] = float4(posStart, 0);
}

// 骨の位置からのずれ。ログ用。描画は St の位置をそのまま使う。
void HoldCloth(uint id, ClothN c, float3 p)
{
	if (c.p.w > 0.5) {
		St[gSt0 + 3 * NMAX + id] = float4(0, 0, 0, 0);
		return;
	}
	St[gSt0 + 3 * NMAX + id] = float4(p - BonePos(c) * Sim.w, 0);
}

// 1 グループで全部の点。辺長拘束＋段順の非伸張チェーン投影＋脚・手・床との衝突（摩擦付き）を反復する。
[numthreads(NMAX, 1, 1)]
void CSClothRelax(uint id : SV_GroupIndex, uint3 grp : SV_GroupID)
{
	if (BatchOn != 0) LoadClothBatch(id, grp.x);
	else LoadClothCB(id);
	BuildLegs(id);
	const uint n = min(Cnt.x, (uint)NMAX);
	const bool on = id < n;
	ClothN c = (ClothN)0;
	float3 rest = 0, p = 0, posStart = 0;
	float sc = max(Sim.w, 0.2);
	float dt = clamp(Sim.x, 1e-4, 1.0 / 24.0);
	if (on) {
		c = Cn[gCn0 + id];
		rest = St[gSt0 + NMAX + id].xyz;
		posStart = St[gSt0 + 4 * NMAX + id].xyz;
		p = (c.p.w > 0.5 || Sim.z > 0.5) ? St[gSt0 + id].xyz : St[gSt0 + id].xyz;
	}
	float flow = on ? Flow(c.x.y) : 0.0;
	float3 windVec = (Wind.xyz + Air.xyz) * 3.2 * flow * sc;
	bool anyLeg = false, anyHand = false, anyFloor = false;
	float3 lastLegN = float3(0, 0, -1), lastHandN = float3(0, 0, -1);
	// 4近傍のレスト辺長・シェア・平均重みをループ前に1回だけロードしてキャッシュする。
	uint nbId[4] = { n, n, n, n };
	float nbL0[4] = { 0.0, 0.0, 0.0, 0.0 };
	float nbShare[4] = { 0.0, 0.0, 0.0, 0.0 };
	float nbAvgW[4] = { 0.0, 0.0, 0.0, 0.0 };
	float wavg = 0.0;
	float upL0 = 0.0, up2L0 = 0.0;
	uint upId = n, up2Id = n;
	if (on && c.p.w <= 0.5 && Sim.z <= 0.5) {
		[unroll] for (int e = 0; e < 4; e++) {
			uint j = c.nb[e];
			if (j < n) {
				nbId[e] = j;
				ClothN cn = Cn[gCn0 + j];
				float l0 = length(c.x.xyz - cn.x.xyz) * sc;
				nbL0[e] = l0;
				if (l0 > 1e-4) nbShare[e] = (cn.p.w > 0.5) ? 1.0 : 0.5;
				if (cn.p.w <= 0.5) {
					float w = e < 2 ? 2.0 : 1.0;
					nbAvgW[e] = w;
					wavg += w;
				}
				if (e == 0) {
					upId = j; upL0 = l0;
					uint j2 = cn.nb[0];
					if (j2 < n) {
						up2Id = j2;
						up2L0 = length(c.x.xyz - Cn[gCn0 + j2].x.xyz) * sc;
					}
				}
			}
		}
	}
	const uint maxRow = min(16u, (n + 31u) / 32u);
	const uint myRow = id / 32u;
	[loop] for (int it = 0; it < 5; it++) {
		gP[id] = on ? p : float3(0, 0, 0);
		GroupMemoryBarrierWithGroupSync();
		if (on && c.p.w <= 0.5 && Sim.z <= 0.5) {
			float3 corr = 0;
			float3 avg = 0;
			[unroll] for (int e = 0; e < 4; e++) {
				if (nbId[e] >= n) continue;
				float3 gp = gP[nbId[e]];
				if (nbShare[e] > 0.0) {
					float3 diff = p - gp;
					float dl = length(diff);
					if (dl > 1e-5) {
						float lTarget = nbL0[e];
						if (e >= 2) {
							// 開脚時はプリーツが開く余裕（gSpread）まで横の自然長上限を広げ、太もものクサビで腰へたくし上がるのを防ぐ。
							float lMax = lTarget * (1.0 + gSpread * saturate(flow * 1.25));
							if (dl > lTarget) lTarget = min(dl, lMax);
						}
						float err = dl - lTarget;
						float stiff = (e < 2) ? (err < 0.0 ? 0.60 : 0.96) : 0.65;
						if (e >= 2 && err < 0.0) err *= 0.35;
						corr += (-err * stiff * nbShare[e]) * (diff / dl);
					}
				}
				if (nbAvgW[e] > 0.0) avg += gp * nbAvgW[e];
			}
			// 2段上の極端な折り返しを抑えつつ、太もも屈曲や股の間への垂れは自然に曲げる。
			if (up2Id < n && up2L0 > 1e-4) {
				float3 diff2 = p - gP[up2Id];
				float dl2 = length(diff2);
				float minBend = up2L0 * 0.52;
				if (dl2 < minBend && dl2 > 1e-5) {
					corr += ((minBend - dl2) * 0.30) * (diff2 / dl2);
				}
			}
			p += corr * 0.68;
			if (wavg > 0.0) p = lerp(p, avg / wavg, 0.03);
			CollidePelvis(p, c.x.xyz, 0.014, sc);
			bool hl = false, hh = false, hf = false;
			float3 nl = 0, nh = 0, dv = 0;
			CollideLegs(p, posStart, c.x.xyz, 0.014, sc, true, 2, hl, nl);
			HandCollide(p, dv, windVec, 0.0, sc, hh, nh);
			if (hh) CollideLegs(p, posStart, c.x.xyz, 0.014, sc, true, 1, hl, nl);
			FloorCollide(p, hf);
			if (hl) { anyLeg = true; lastLegN = nl; }
			if (hh) { anyHand = true; lastHandN = nh; }
			if (hf) anyFloor = true;
		}
		// 中間(it==2)と最終のみで段順の非伸張クランプ（Gauss-Seidel）を掛け、伸びだけを厳密に防ぐ。
		if (it == 2) {
			gP[id] = on ? p : float3(0, 0, 0);
			[loop] for (uint row = 1u; row < maxRow; row++) {
				GroupMemoryBarrierWithGroupSync();
				if (on && c.p.w <= 0.5 && Sim.z <= 0.5 && myRow == row && upId < n && upL0 > 1e-4) {
					float3 upP = gP[upId];
					float3 d = p - upP;
					float dl = length(d);
					if (dl > upL0 * 1.02 && dl > 1e-5) {
						p = upP + d * ((upL0 * 1.02) / dl);
					}
					gP[id] = p;
				}
			}
		}
	}
	// 最終パスで段順の非伸張投影を掛通し、その後に骨盤・脚・床の外へ出して長さ・衝突ともに厳密化する。
	gP[id] = on ? p : float3(0, 0, 0);
	[loop] for (uint row = 1u; row < maxRow; row++) {
		GroupMemoryBarrierWithGroupSync();
		if (on && c.p.w <= 0.5 && Sim.z <= 0.5 && myRow == row && upId < n && upL0 > 1e-4) {
			float3 upP = gP[upId];
			float3 d = p - upP;
			float dl = length(d);
			if (dl > upL0 * 1.02 && dl > 1e-5) {
				p = upP + d * ((upL0 * 1.02) / dl);
			}
			gP[id] = p;
		}
	}
	GroupMemoryBarrierWithGroupSync();
	if (on && c.p.w <= 0.5 && Sim.z <= 0.5) {
		// 倒れて床や脚に押された布がウエスト上端より頭側（胸・背中側）へたくし上がらないよう腰軸上端をクランプする。
		if (LieAmt() > 0.15) {
			float3 hpF = mul(float4(p / sc, 1.0), HipInv).xyz;
			float maxY = Grav.w - max(Grav.w - c.x.y, 0.0) * 0.25;
			if (hpF.y > maxY) {
				p -= gHipUp * ((hpF.y - maxY) * sc);
			}
		}
		CollidePelvis(p, c.x.xyz, 0.014, sc);
		bool hl = false, hf = false;
		float3 nl = 0;
		CollideLegs(p, posStart, c.x.xyz, 0.014, sc, true, 3, hl, nl);
		FloorCollide(p, hf);
		if (hl) { anyLeg = true; lastLegN = nl; }
		if (hf) anyFloor = true;
	}
	if (!on) return;
	if (c.p.w > 0.5 || Sim.z > 0.5) {
		float3 pKeep = (c.p.w > 0.5) ? rest : p;
		St[gSt0 + 3 * NMAX + id] = float4(pKeep - rest, 0);
		St[gSt0 + 2 * NMAX + id] = float4(0, 0, 0, 0);
		St[gSt0 + id] = float4(pKeep, 0);
		St[gSt0 + 5 * NMAX + id] = float4(mul(float4(pKeep / sc, 1.0), HipInv).xyz, 0);
		St[gSt0 + 6 * NMAX + id] = float4(0, 0, 0, 0);
		return;
	}
	float3 velNew = (p - posStart) / dt;
	float wear = WearSkirt(c.x.y);
	if (wear > 0.001) {
		p = lerp(p, rest, wear);
		velNew *= (1.0 - wear);
	}
	float lieR = LieAmt();
	if (anyLeg) {
		float3 vt = velNew - lastLegN * dot(velNew, lastLegN);
		velNew -= vt * min(0.45, 7.0 * dt);
		// 倒れ時、脚との接触反力でウエスト方向（+gHipUp）へポンプ加速してめくれ上がるのを防ぐ。
		if (lieR > 0.15) {
			float vUp = dot(velNew, gHipUp);
			if (vUp > 0.0) velNew -= gHipUp * (vUp * smoothstep(0.15, 0.45, lieR));
		}
	}
	if (anyHand) {
		float3 vt = velNew - lastHandN * dot(velNew, lastHandN);
		velNew -= vt * min(0.30, 4.5 * dt);
	}
	if (anyFloor) {
		float dn = dot(velNew, gDown);
		if (dn > 0.0) velNew -= gDown * dn;
		velNew *= exp(-7.0 * dt);
	}
	float vl = length(velNew);
	float maxV = 1.8 * sc;
	if (vl > maxV) velNew *= maxV / vl;
	St[gSt0 + 2 * NMAX + id] = float4(velNew, 0);
	St[gSt0 + 3 * NMAX + id] = float4(p - rest, 0);
	St[gSt0 + id] = float4(p, 0);
	St[gSt0 + 5 * NMAX + id] = float4(mul(float4(p / sc, 1.0), HipInv).xyz, 0);
	St[gSt0 + 6 * NMAX + id] = float4(mul(velNew / sc, (float3x3)HipInv), 0);
}

[numthreads(64, 1, 1)]
void CSClothWrite(uint id : SV_DispatchThreadID, uint gid : SV_GroupIndex)
{
	LoadClothCB(gid);
	BuildLegs(gid);
	if (id >= Cnt.z) return;
	ClothV c = Cv[id];
	uint o = c.a.x * 36u;
	float3 s = asfloat(Vout.Load3(o));
	float3 sn = asfloat(Vout.Load3(o + 12));
	uint id4[4];
	id4[0] = c.a.y & 0xFFFFu;
	id4[1] = c.a.y >> 16;
	id4[2] = c.a.z & 0xFFFFu;
	id4[3] = c.a.z >> 16;
	float w4[4] = { c.w.x, c.w.y, c.w.z, c.w.w };
	float3 pGrid = 0;
	float3 xGrid = 0;
	float wsum = 0;
	[unroll] for (int k = 0; k < 4; k++) {
		if (w4[k] <= 1e-5 || id4[k] >= (uint)NMAX) continue;
		pGrid += St[gSt0 + id4[k]].xyz * w4[k];
		xGrid += Cn[gCn0 + id4[k]].x.xyz * w4[k];
		wsum += w4[k];
	}
	float sc = max(Sim.w, 1e-4);
	float lie = LieAmt();
	float3 ehx, ehz, down;
	FlatAxes(ehx, ehz, down);
	float3 nFlat = ehx * c.n.x - down * c.n.y + ehz * c.n.z;
	float3 nRig = mul(c.n.xyz, (float3x3)Hips);
	float3 nHang = lerp(nFlat, nRig, lie);

	// 物理拘束済みの格子曲面 pGrid を直接補間し、衣装固有のプリーツ細部 (c.p.xyz - xGrid) だけを現在の腰基底で乗せる。
	// rest との差分を経由しないため、太もも境界での非線形な段差や三角形のギザギザ割れが起きない。
	float3 pCloth = s;
	if (wsum > 1e-5) {
		pGrid /= wsum;
		xGrid /= wsum;
		float3 dBind = c.p.xyz - xGrid;
		float3 dFlat = (ehx * dBind.x - down * dBind.y + ehz * dBind.z) * sc;
		float3 dRig = mul(dBind, (float3x3)Hips) * sc;
		pCloth = pGrid + lerp(dFlat, dRig, lie);
	}
	float coverV = PelvisCover(c.p.y);
	float wear = WearSkirt(c.p.y);
	float hangV = (Cnt.x > 1u ? 1.0 : 0.0) * HangIn(c.p.y) * (1.0 - wear) * (1.0 - coverV) * saturate(c.p.w);
	float3 p = lerp(s, pCloth, hangV);
	p = lerp(p, s, wear);
	float skin = c.n.w < 0.0 ? -c.n.w : 0.016;
	CollidePelvis(p, c.p.xyz, skin, sc);
	if (max(Grav.w - c.p.y, 0.0) > 0.005) {
		bool hl = false, hf = false;
		float3 nl = 0;
		CollideLegs(p, p, c.p.xyz, skin, sc, false, 2, hl, nl);
		FloorCollide(p, hf);
	}
	p = WearClear(p, sn, wear);
	float3 hungV = HangPose(c.p.xyz);
	float3 def = (p - hungV) / sc;
	float dropV = max(Grav.w - c.p.y, 0.12);
	nHang -= down * clamp(dot(nHang, def) / dropV * 0.35, -0.35, 0.35);
	float3 nr = lerp(sn, nHang, hangV * 0.85);
	float l = dot(nr, nr);
	if (l > 1e-8) nr *= rsqrt(l);
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nr));
}
