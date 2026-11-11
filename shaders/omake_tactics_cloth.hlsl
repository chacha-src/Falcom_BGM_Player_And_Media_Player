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
groupshared float3 gDown, gEhx, gEhz, gWaist;
groupshared float3 gPA[6], gPB[6], gPU[6], gPW[6];
groupshared float gRmax[4];
groupshared uint gNLeg;
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

// しゃがみ、またはカットの短い布。ロングの裾（腰から約 50cm より下）は垂らしたまま。
float WearSkirt(float y)
{
	float sit = saturate(Mode.x);
	float mini = (1.0 - smoothstep(0.40, 0.62, max(Grav.w - y, 0.0))) * InCut();
	return saturate(max(sit, mini));
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

// 立っていると 0。仰向け・横向きは 1。起き上がりの途中はその間。
float LieAmt()
{
	float3 upB = Hips[1].xyz;
	float ul = length(upB);
	if (ul < 1e-4) return 0;
	upB /= ul;
	return 1.0 - smoothstep(0.22, 0.78, abs(dot(upB, gDown)));
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

// 足は動く障害物、布は自由。側は今の位置ではなく、その頂点がスカート上で元からいる側。
// 骨の中心より今どっちにいるかで出すと、前布が太ももの途中から後ろへ裏返る。
// 足の幅の中で、自分の側の面より手前（足の中、またはすり抜けた後）だけ表面へ出す。
// 幅の外、およびすでに面の外にある布は、慣性と重力と風のまま。
void PushSeg(inout float3 q, uint s, float pad, uint kind, float bindFront, float bindLat, float sheet)
{
	float3 AB = gPB[s] - gPA[s];
	float axl = length(AB);
	if (axl < 1e-4) return;
	float3 axN = AB / axl;
	// 膝の少し下まで太ももを障害物にする。ここで切ると、その下の布が足に付いてこない。
	float aMin = kind == 0u ? -0.02 : -0.10;
	float aMax = kind == 2u ? axl + 0.10 : axl + 0.12;
	float along = dot(q - gPA[s], axN);
	if (along < aMin || along > aMax) return;
	float t = saturate(along / axl);
	float wSock = kind == 0u ? smoothstep(0.02, 0.08, t) : 1.0;
	if (wSock <= 0.001) return;
	float3 O = gPA[s] + axN * along;

	float3 fwdB = -gEhz;
	float3 prefer = fwdB * bindFront + gEhx * bindLat;
	if (sheet > 0.5 && sheet < 1.5) prefer += fwdB * 0.10;
	else if (sheet > 1.5) prefer -= fwdB * 0.10;
	prefer -= axN * dot(prefer, axN);
	float pl = length(prefer);
	if (pl < 1e-4) {
		prefer = sheet > 1.5 ? -fwdB : fwdB;
		prefer -= axN * dot(prefer, axN);
		pl = length(prefer);
	}
	if (pl < 1e-4) return;
	prefer /= pl;
	prefer = SlideGap(prefer, s, kind, gPA[s] + AB * t, axN);
	prefer -= axN * dot(prefer, axN);
	pl = length(prefer);
	if (pl < 1e-4) return;
	prefer /= pl;

	float3 rel = q - O;
	rel -= axN * dot(rel, axN);
	float fwd = dot(rel, prefer);
	float3 side = rel - prefer * fwd;
	float lat = length(side);
	float3 sideDir = lat > 0.004 ? (side / lat) : gEhx;
	sideDir -= axN * dot(sideDir, axN);
	float sdl = length(sideDir);
	sideDir = sdl > 1e-4 ? sideDir / sdl : gEhx;

	float face = max(pad, 0.008);
	float Rfwd = SegRadius(s, t, prefer) + face;
	float Rside = SegRadius(s, t, sideDir) + face + 0.016;
	float rFloor = kind == 2u ? 0.050 : (kind == 1u ? 0.058 : 0.062);
	if (Rfwd < rFloor) Rfwd = rFloor;
	if (Rside < rFloor) Rside = rFloor;
	if (kind == 2u) {
		float sole = saturate(dot(gDown, sideDir));
		Rfwd += sole * 0.012;
		Rside += sole * 0.018;
	}
	float nx = lat / Rside;
	if (nx >= 1.0) return;
	float surf = Rfwd * sqrt(max(1.0 - nx * nx, 0.0));
	if (fwd >= surf) return;
	float3 goal = O + side + prefer * surf;
	float3 move = goal - q;
	float ml = length(move);
	if (ml > 0.45) move *= 0.45 / ml;
	if (ml > 0.001) q += move * wSock;
}

float3 PushFaces(float3 q, float pad, float bindFront, float bindLat, float sheet)
{
	uint ns = gNLeg;
	uint nLeg = min(ns, 2u);
	[loop] for (uint leg = 0; leg < nLeg; leg++) {
		if (leg < ns) PushSeg(q, leg, pad, 0u, bindFront, bindLat, sheet);
		uint shin = leg + 2u;
		if (shin < ns) PushSeg(q, shin, pad, 1u, bindFront, bindLat, sheet);
		uint foot = leg + 4u;
		if (foot < ns) PushSeg(q, foot, pad, 2u, bindFront, bindLat, sheet);
	}
	return q;
}

// 足は動く障害物。自分の側の面より手前にいる布だけ、その高さの表面へ出す。
float3 LegCore(float3 p, float bindX, float bindY, float bindZ, float slitCode, float pad, float carry)
{
	float sc = max(Sim.w, 1e-4) + carry * 0.0;
	float3 down = gDown;
	float3 waistC = gWaist;
	if (pad < 0.008) pad = 0.016;
	if (Sim.z > 0.5) pad += 0.008;
	float3 dpm = p / sc - waistC;
	float hY0 = Grav.w - dot(dpm, down);
	if (hY0 > Grav.w + 0.02) return p;
	float drop = max(Grav.w - bindY, 0.0);
	if (drop <= 0.025) return p;
	float wWaist = smoothstep(0.025, 0.115, drop);
	bool isSlit = slitCode >= 0.5;
	float bindFront = Mode.z - bindZ;
	float bindLat = bindX - Mode.y;
	float3 p0 = p;
	float3 q = PushFaces(dpm, pad, bindFront, bindLat, slitCode);
	p += (q - dpm) * sc;
	// 筒は足の形にも沿わせる。横たわりは上へ出す。そのあと、面の外をもう一度確かめる。
	p = ClothFinish(p, pad, isSlit ? 1.0 : 0.0);
	q = p / sc - waistC;
	q = PushFaces(q, pad, bindFront, bindLat, slitCode);
	float3 outP = (q + waistC) * sc;
	return lerp(p0, outP, wWaist);
}

// 左手と右手。Hand[0],Hand[1] が左、Hand[2],Hand[3] が右。近い布を体側へふわっと寄せ、箱の中は出す。
float3 HandAir(float3 p)
{
	float sc = max(Sim.w, 1e-4);
	float3 waist = gWaist * sc;
	float3 sum = 0;
	[unroll] for (int i = 0; i < 2; i++) {
		float4 a = Hand[i * 2];
		float4 b = Hand[i * 2 + 1];
		if (a.w < 0.5) continue;
		float3 c = (a.xyz + b.xyz) * 0.5;
		float3 e = max((b.xyz - a.xyz) * 0.5, float3(0.012, 0.012, 0.012));
		float3 d = p - c;
		float3 q = abs(d) - e;
		float outside = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
		// すでに外なら動かさない。拘束の繰り返しで何回も押して穴にしない。
		float gap = 0.016;
		if (outside >= gap) continue;
		float wY = smoothstep(0.04, 0.14, dot(p - waist, gDown));
		if (wY <= 0.001) continue;
		float3 toBody = waist - c;
		toBody -= gDown * dot(toBody, gDown);
		float tl = length(toBody);
		toBody = tl > 1e-4 ? toBody / tl : ((c.x >= waist.x) ? -gEhx : gEhx);
		float need = gap - outside;
		if (outside >= 0.0) need *= saturate(b.w / (0.04 * sc));
		if (need > 0.045) need = 0.045;
		if (need <= 0.001) continue;
		float3 delta = toBody * (need * wY);
		if (outside >= 0.0) delta -= gDown * dot(delta, gDown);
		sum += delta;
	}
	float ml = length(sum);
	if (ml > 0.12) sum *= 0.12 / ml;
	return p + sum;
}

float3 LegPush(float3 p, float bindX, float bindY, float bindZ, float slitCode, float pad, float carry)
{
	float3 p1 = LegCore(p, bindX, bindY, bindZ, slitCode, pad, carry);
	if (max(Grav.w - bindY, 0.0) <= 0.04) return p1;
	float3 a = HandAir(p1);
	if (dot(a - p1, a - p1) <= 1e-8) return a;
	return LegCore(a, bindX, bindY, bindZ, slitCode, pad, carry);
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

// 腰の帯まわり。水平の向きで測った断面の中にいる布だけ、滑らかに減衰させて外へ出す。段差は作らない。
float3 HipOut(float3 p)
{
	float sc = max(Sim.w, 1e-4);
	float3 down = length(Grav.xyz) > 1e-4 ? Grav.xyz / length(Grav.xyz) : float3(0, -1, 0);
	float cz = asfloat(Cnt.w);
	float top = Grav.w;
	float3 waistC = mul(float4(Mode.y, top, cz, 1), Hips).xyz;
	float3 hx = Hips[0].xyz, hz = Hips[2].xyz;
	float3 ehx = hx - down * dot(hx, down);
	float3 ehz = hz - down * dot(hz, down);
	ehx = length(ehx) > 1e-4 ? normalize(ehx) : float3(1, 0, 0);
	ehz = length(ehz) > 1e-4 ? normalize(ehz) : float3(0, 0, 1);
	float3 dp = p / sc - waistC;
	float y = top - dot(dp, down);
	if (y > top + 0.04 || y < top - 0.06) return p;
	float wY = (1.0 - smoothstep(0.005, 0.04, y - top)) * (1.0 - smoothstep(0.015, 0.06, top - y));
	if (wY <= 1e-4) return p;
	float2 d = float2(dot(dp, ehx), dot(dp, ehz));
	float rl = length(d);
	if (rl < 1e-4) return p;
	float phi = atan2(d.x, d.y);
	float fp = (phi / 6.2831853 + 0.5) * 16.0;
	float fl = floor(fp);
	uint p0 = (uint)(fl + 16.0) % 16;
	uint p1 = (p0 + 1) % 16;
	float R = lerp(Prof[gProf0 + 6 * PT * PF + p0], Prof[gProf0 + 6 * PT * PF + p1], fp - fl) - 0.010;
	if (R < 0.05 || rl >= R) return p;
	float need = min(R - rl, 0.025) * wY;
	return p + (ehx * (d.x / rl) + ehz * (d.y / rl)) * (need * sc);
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

// 腰に付いた最上段から 12cm かけて垂らし始める。上端で折れない。
float HangIn(float y)
{
	return smoothstep(0.0, 0.12, Grav.w - y);
}

// 腰の帯だけ固定する。股のすぐ下から布は動いてよい。
float PelvisCover(float y)
{
	return 1.0 - smoothstep(0.025, 0.07, Grav.w - y);
}

// 腰からの丈。ロングの裾（約 50cm）で最大。ミニスカートは同じ曲線の腰付近なので軽く揺れる。
float Flow(float y)
{
	float drop = max(Grav.w - y, 0);
	return saturate((drop - 0.04) / 0.46);
}

// 正面の股間。カメラは前上から見るので、腰から腿のつけ根までは前布を体に残す。横と後ろ、それより下は靡かせる。
float FrontVeil(ClothN c)
{
	float az = abs(atan2(c.x.x - Mode.y, c.x.z - Mode.z));
	float front = smoothstep(1.25, 2.35, az);
	float drop = max(Grav.w - c.x.y, 0);
	float groin = 1.0 - smoothstep(0.05, 0.20, drop);
	return front * groin;
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
	// 腰の中心から重力の向きへ垂らす。前だけ足の骨へ混ぜると開脚で前布の上部にU字の段差が出るため、高さだけで滑らかに垂らす。
	float hang = HangIn(c.x.y) * (1.0 - saturate(Mode.x)) * (1.0 - cover);
	float3 hung = LevelHang(c.x.xyz);
	hung += GravDown() * (saturate((Grav.w - c.x.y) / 0.45) * 0.02 * Sim.w);
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
	float3 rest = NodeTarget(c);
	float3 off = mul(St[gSt0 + 3 * NMAX + id].xyz, (float3x3)Delta);
	float3 prevRest = mul(St[gSt0 + NMAX + id].xyz, (float3x3)Delta);
	float3 vel = mul(St[gSt0 + 2 * NMAX + id].xyz, (float3x3)Delta);
	float flow = Flow(c.x.y);
	float veil = FrontVeil(c);
	float free = 1.0 - veil * 0.90;
	float dt = max(Sim.x, 1e-4);
	if (Sim.z > 0.5 || c.p.w > 0.5) {
		off = 0;
		vel = 0;
		prevRest = rest;
	} else {
		// モーション変更や歩行・走り・飛行で腰（rest）が上下・前後に動いたとき、前のスカートの位置を残し、慣性と重力でびよんびよん波打たせる。
		// 体の向きが急に変わったフレームは布を連れて回り、足の反対側へ遅れないようにする。
		float3 dRest = rest - prevRest;
		float drl = length(dRest);
		if (drl > 0.10 * Sim.w) dRest *= (0.10 * Sim.w) / drl;
		float cth = clamp((Delta[0].x + Delta[1].y + Delta[2].z - 1.0) * 0.5, -1.0, 1.0);
		float sharp = 1.0 - smoothstep(0.34, 0.94, cth);
		float lag = lerp(0.82, 0.20, sharp);
		float3 down0 = GravDown();
		float3 dVert = down0 * dot(dRest, down0);
		float3 dHorz = dRest - dVert;
		off -= (dHorz * lag + dVert * lerp(0.95, 0.25, sharp)) * flow;
		vel -= ((dHorz * lerp(0.38, 0.08, sharp) + dVert * lerp(0.72, 0.14, sharp)) / dt) * flow;
	}
	float3 gust = Wind.xyz + Air.xyz;
	float gAmp = length(gust);
	float3 gDir = gAmp > 0.04 ? gust / gAmp : float3(0, 0, 1);
	float3 up = -GravDown();
	float3 side = cross(gDir, up);
	float sl = length(side);
	side = sl > 1e-4 ? side / sl : float3(1, 0, 0);
	float sag = saturate((Grav.w - c.x.y - 0.06) / 0.22);
	float drop = max(Grav.w - c.x.y, 0.0);
	bool isSlit = c.q.z > 0.5;
	float hemOpen = smoothstep(0.16, 0.46, drop) * (1.0 - veil);
	float slitBoost = isSlit ? (1.0 + 0.62 * hemOpen) : (1.0 + 0.18 * hemOpen);
	float fall = saturate(c.p.z) * flow * free * slitBoost;
	// スリットの前布は風をまともに受け、後ろ布も裾へ向かうほど大胆に靡かせる。
	if (c.q.z > 1.5) fall *= lerp(0.48, 0.92, hemOpen);
	float3 target = AirPush(fall) * Sim.w;
	float reach = length(target);
	target += GustRipple(c, gDir, side, up, reach, isSlit ? (1.0 + 0.45 * hemOpen) : (1.0 + 0.15 * hemOpen), 1.0);
	// 風を受けた裾が外周へふんわり広がる成分。スリットの裾は脇が開いている分さらに大きく広がる。
	float3 bindOut = gEhx * (c.x.x - Mode.y) + gEhz * (c.x.z - Mode.z);
	float bol = length(bindOut);
	bindOut = bol > 1e-4 ? bindOut / bol : side;
	float flareWave = 0.65 + 0.35 * sin(ClothTime() * 1.85 + atan2(c.x.x - Mode.y, c.x.z - Mode.z) * 2.0 + drop * 5.0);
	float flareAmp = (reach * (isSlit ? 0.36 : 0.20) + (Wind.w > 0.5 ? (isSlit ? 0.038 : 0.018) * Sim.w : 0.0)) * hemOpen * flareWave;
	target += bindOut * flareAmp;
	target += GravDown() * (sag * 0.02 * flow * Sim.w);
	if (Wind.w > 0.5) target += FlightGust(c, flow * free) * (isSlit ? lerp(80.0, 128.0, hemOpen) : lerp(80.0, 96.0, hemOpen));
	// 腰の上下動や足の蹴りで生まれた弾み（vel）を減衰ばねでびよんびよんと波打たせ、風と重力へ滑らかに戻す。
	float rush = Rush();
	float damp = exp(-dt * (Wind.w > 0.5 ? 3.2 : lerp(5.2, 3.4, rush)));
	vel = (vel - off * (15.5 * dt)) * damp;
	if (length(rest - prevRest) < 1e-5 && Wind.w < 0.5 && rush < 0.05) vel *= 0.25;
	float vl = length(vel);
	float maxV = 0.52 * max(Sim.w, 0.2);
	if (vl > maxV) vel *= maxV / vl;
	off += vel * dt * 0.42;
	float3 delta = target - off;
	float dl = length(delta);
	// 風で出るときは速く、足で押された後やモーション切り替え後に重力と摩擦で戻るときは滑らかな速度で戻す。
	bool rising = length(target) > length(off) + 0.005;
	float stepRate = rising ? 1.44 : lerp(0.65, 1.25, saturate(dl / 0.14));
	float maxStep = stepRate * dt * max(Sim.w, 0.2);
	if (dl > maxStep && dl > 1e-6) delta *= maxStep / dl;
	off += delta;
	// 上下の伸び縮みは腰の揺れや飛行・走りの弾みで ±6.5cm まで許し、垂れたスカートがびよんびよん波打つようにする。
	float vComp = dot(off, up);
	float vLim = (0.065 + 0.025 * max(saturate(Wind.w - 1.0), rush) + saturate((abs(Air.w) - 0.62) / 0.9) * 0.18) * flow * max(Sim.w, 0.2);
	if (abs(vComp) > vLim) off += up * (clamp(vComp, -vLim, vLim) - vComp);
	// 股は完全に固定化するのではなく動きを少なく抑え、股間が見えないようにした上で足の外へ出す。
	float hold = GroinHold(c.x.x, c.x.y, c.x.z) * 0.88;
	off *= (1.0 - hold);
	vel *= (1.0 - hold);
	// 両膝を曲げたときと、カットの短いスカート。垂らしたずれを足すと腰から離れて消える。
	// ずれは捨て、描画は衣装の骨へ戻す。立ちに戻るときは 0 から風と重力を溜め直す。
	float wear = WearSkirt(c.x.y);
	if (wear > 0.001) {
		off *= (1.0 - wear);
		vel *= (1.0 - wear);
	}
	float3 pushed = c.p.w > 0.5 ? rest : LegPush(rest + off, c.x.x, c.x.y, c.x.z, c.q.z, 0.016, 0.0);
	off = pushed - rest;
	St[gSt0 + id] = float4(pushed, 0);
	St[gSt0 + NMAX + id] = float4(rest, 0);
	St[gSt0 + 2 * NMAX + id] = float4(vel, 0);
	St[gSt0 + 3 * NMAX + id] = float4(off, 0);
	St[gSt0 + 4 * NMAX + id] = float4(pushed, 0);
	St[gSt0 + 5 * NMAX + id] = float4(pushed, 0);
	St[gSt0 + 6 * NMAX + id] = float4(pushed, 0);
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

// 1 グループで全部の点。上下左右の隣接でギザギザだけを消し、脚の形の外へ出す。4 回反復。
[numthreads(NMAX, 1, 1)]
void CSClothRelax(uint id : SV_GroupIndex, uint3 grp : SV_GroupID)
{
	if (BatchOn != 0) LoadClothBatch(id, grp.x);
	else LoadClothCB(id);
	BuildLegs(id);
	const uint n = min(Cnt.x, (uint)NMAX);
	const bool on = id < n;
	ClothN c = (ClothN)0;
	float3 rest = float3(0, 0, 0);
	float3 off = float3(0, 0, 0);
	if (on) {
		c = Cn[gCn0 + id];
		// 目標は直前の積分が自分の骨で入れた値。ここで NodeTarget を呼ぶと、複数人の同時拘束は
		// 最後の一人の骨パレットしか無く、他の人のスカートが他人の姿勢へ飛んで消える。
		rest = St[gSt0 + NMAX + id].xyz;
		off = c.p.w > 0.5 ? float3(0, 0, 0) : St[gSt0 + 3 * NMAX + id].xyz;
	}
	float flow = on ? Flow(c.x.y) : 0;
	// 腰とミニは軽く、ロングの裾だけ 0.55m まで。舞台の風で上段が腕まで残らない。
	float lim = lerp(0.05, 0.55, saturate(flow)) * max(Sim.w, 0.2);
	[loop] for (int it = 0; it < 4; it++) {
		// 上下を強く、左右も混ぜる。段差は上下の差なので、横の隣が同じ段だと平均が段を残す。
		// 直線の靡きは上下が打ち消し合うので残る。腰の固定点は平均に入れない。スリットは隣が無く縫わない。
		[unroll] for (int sub = 0; sub < 2; sub++) {
			gP[id] = on ? off : float3(0, 0, 0);
			GroupMemoryBarrierWithGroupSync();
			if (on && c.p.w <= 0.5) {
				float3 sum = 0;
				float wsum = 0;
				float nPin = 0;
				[unroll] for (int e = 0; e < 4; e++) {
					uint j = c.nb[e];
					if (j >= n) continue;
					if (Cn[gCn0 + j].p.w > 0.5) { nPin += 1.0; continue; }
					float w = e < 2 ? 3.0 : 1.0;
					sum += gP[j] * w;
					wsum += w;
				}
				if (wsum > 0.0) {
					float k = lerp(0.86, 0.74, saturate(flow));
					off = lerp(off, sum / wsum, k);
				}
				if (sub == 1 && nPin > 0.0) off *= 0.94;
			}
			GroupMemoryBarrierWithGroupSync();
		}
		if (on && c.p.w <= 0.5) {
			float ol = length(off);
			if (ol > lim && ol > 1e-5) off *= lim / ol;
			off *= (1.0 - GroinHold(c.x.x, c.x.y, c.x.z) * 0.35);
			// ならしたあとの最後だけ、足が動いた分を一度載せる。
			float carry = it == 3 ? 1.0 : 0.0;
			float3 before = rest + off;
			float3 pushed = LegPush(before, c.x.x, c.x.y, c.x.z, c.q.z, 0.016, carry);
			if (carry > 0.5) {
				float3 shove = pushed - before;
				float3 vel = St[gSt0 + 2 * NMAX + id].xyz + shove * 3.5;
				float vl = length(vel);
				float maxV = 0.9 * max(Sim.w, 0.2);
				if (vl > maxV) vel *= maxV / vl;
				St[gSt0 + 2 * NMAX + id] = float4(vel, 0);
			}
			off = pushed - rest;
		}
		GroupMemoryBarrierWithGroupSync();
	}
	// 足で出したあとの段差を、もう一度隣接で揃えてから外へ戻す。ここは速度に足さない。
	gP[id] = on ? off : float3(0, 0, 0);
	GroupMemoryBarrierWithGroupSync();
	if (on && c.p.w <= 0.5) {
		float3 sum = 0;
		float wsum = 0;
		[unroll] for (int e = 0; e < 4; e++) {
			uint j = c.nb[e];
			if (j >= n || Cn[gCn0 + j].p.w > 0.5) continue;
			float w = e < 2 ? 3.0 : 1.0;
			sum += gP[j] * w;
			wsum += w;
		}
		if (wsum > 0.0) off = lerp(off, sum / wsum, lerp(0.86, 0.74, saturate(flow)));
		float ol = length(off);
		if (ol > lim && ol > 1e-5) off *= lim / ol;
		off *= (1.0 - GroinHold(c.x.x, c.x.y, c.x.z) * 0.35);
		float3 before = rest + off;
		off = LegPush(before, c.x.x, c.x.y, c.x.z, c.q.z, 0.016, 0.0) - rest;
	}
	if (!on) return;
	if (c.p.w > 0.5) {
		St[gSt0 + 3 * NMAX + id] = float4(0, 0, 0, 0);
		St[gSt0 + id] = float4(rest, 0);
		return;
	}
	St[gSt0 + 3 * NMAX + id] = float4(off, 0);
	St[gSt0 + id] = float4(rest + off, 0);
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
	// 頂点自身のバインド位置を腰から重力で垂らし、格子が運んだずれ（風＋足の押し出し）を足す。足の骨で裾を引きずらない。
	uint id4[4];
	id4[0] = c.a.y & 0xFFFFu;
	id4[1] = c.a.y >> 16;
	id4[2] = c.a.z & 0xFFFFu;
	id4[3] = c.a.z >> 16;
	float w4[4] = { c.w.x, c.w.y, c.w.z, c.w.w };
	float3 disp = 0;
	float wsum = 0;
	[unroll] for (int k = 0; k < 4; k++) {
		if (w4[k] <= 1e-5 || id4[k] >= (uint)NMAX) continue;
		disp += St[gSt0 + 3 * NMAX + id4[k]].xyz * w4[k];
		wsum += w4[k];
	}
	if (wsum > 1e-5) disp /= wsum;
	float coverV = PelvisCover(c.p.y);
	float wear = WearSkirt(c.p.y);
	float hangV = (Cnt.x > 1u ? 1.0 : 0.0) * HangIn(c.p.y) * (1.0 - wear) * (1.0 - coverV) * saturate(c.p.w);
	float3 ehx, ehz, down;
	FlatAxes(ehx, ehz, down);
	float3 nHang = ehx * c.n.x - down * c.n.y + ehz * c.n.z;
	float3 hungV = LevelHang(c.p.xyz) + down * (saturate(max(Grav.w - c.p.y, 0) / 0.45) * 0.02 * Sim.w);
	// しゃがみとカットの短いスカートは、スキニングした衣装の位置に着せる。
	// 垂らした位置へ戻すとミニスカートが腰から離れて描かれない。ロングの裾は wear が 0 なので今まで通り。
	float3 p = lerp(s, hungV, hangV) + disp * saturate(c.p.w) * (1.0 - wear);
	// 股は完全に固定せず動きを少なく抑えてから、足の外だけ出す。押したあとに戻すと足の中へ入る。
	p = lerp(p, hungV, GroinHold(c.p.x, c.p.y, c.p.z) * 0.85 * (1.0 - wear));
	p = lerp(p, s, wear);
	p = HipOut(p);
	uint span = c.a.w & 3u;
	float slitCode = span == 1u ? 1.0 : (span == 2u ? 2.0 : 0.0);
	float skin = c.n.w < 0.0 ? -c.n.w : 0.016;
	p = LegPush(p, c.p.x, c.p.y, c.p.z, slitCode, skin, 0.0);
	p = WearClear(p, sn, wear);
	float3 def = (p - hungV) / max(Sim.w, 1e-4);
	float dropV = max(Grav.w - c.p.y, 0.12);
	nHang -= down * clamp(dot(nHang, def) / dropV * 0.35, -0.35, 0.35);
	float3 nr = lerp(sn, nHang, hangV * 0.85);
	float l = dot(nr, nr);
	if (l > 1e-8) nr *= rsqrt(l);
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nr));
}
