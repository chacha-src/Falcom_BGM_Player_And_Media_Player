// 胸と袖、それにリボン・衿・エプロン・上着の裾・ワンピースの上半身。スカートの格子とは別で、点は 96 まで。
// タイトな服はここに来ない（体へスキンしたまま）。ゆるい胸は下側ほど胸の球から離れ、
// 重力と空気で隙間が残る。手を動かすと、その少し上の袖だけが遅れて付いてくる。
// 手を戻すときは吸い付かせず、重力と風で戻る。腕の中には入れない。
// p.w が 2 の点はかざりと上着。胸の球では膨らませず、胴の外へ出すだけ。
cbuffer SoftParm : register(b0)
{
	row_major float4x4 Delta;
	row_major float4x4 Bone[6]; // 0 胸 1 上腕左 2 前腕左 3 上腕右 4 前腕右 5 未使用
	float4 Sim;                 // x=dt y=歩数 z=置き直し w=身長
	float4 Wind;
	float4 Air;                 // xyz=そよ風 w=上下。上が正
	float4 Bust[2];             // 胸の球。xyz=中心 w=半径（体の胸。空気の隙間は下側で足す）
	float4 ArmA[4];             // 腕のカプセル始点 xyz、w=半径。0 上腕左 1 前腕左 2 上腕右 3 前腕右
	float4 ArmB[4];             // 終点。w<0 はそのカプセルを使わない
	float4 Limb[2];             // 左手・右手のこのフレームの移動。w=長さ
	float4 Body;                // xyz=胴の中心（横へ避ける基準） w=時刻
	uint4 Cnt;                  // x=点の数 y=頂点の数
	float4 Grav;                // モデル空間の重力加速度（世界の下）
};

#define SMAX 96

struct SoftN
{
	float4 x; // バインド位置、w=1 で骨に固定
	float4 p; // x=離れてよい距離 y=戻る割合 z=重力 w=0 胸 / 1 袖 / 2 かざり・上着
	uint4 nb;
	float4 q; // x=下側の空気の隙間（m） y=骨番号 z=引きずり 0 無し 1 左 2 右
};

struct SoftV
{
	uint4 a;  // x=メッシュ頂点 y,z=点 4 つ（16bit） w=頂点の骨番号
	float4 w;
	float4 p; // バインド位置 w=格子へ移る割合
};

StructuredBuffer<SoftN> Sn : register(t0);
StructuredBuffer<SoftV> Sv : register(t1);
RWByteAddressBuffer Vout : register(u0);
RWStructuredBuffer<float4> St : register(u1); // [0,S) 今 [S,2S) [2S,3S) [3S,4S) は引きずりの前（新しい順）

groupshared float3 gP[SMAX];
groupshared float3 gT[SMAX];

float3 BonePos(float3 x, float bone)
{
	uint b = (uint)clamp(bone + 0.5, 0, 5);
	return mul(float4(x, 1), Bone[b]).xyz * Sim.w;
}

float3 BustPush(float3 p, float air)
{
	[unroll] for (int i = 0; i < 2; i++) {
		float R = Bust[i].w;
		if (R < 0.01) continue;
		float3 d = p - Bust[i].xyz;
		// 縦に長い楕円。下側だけ空気の隙間。球で潰さない。
		float under = saturate(-d.y / max(R, 0.02));
		float Ry = R * 1.18 + air * under;
		float Rx = R * 0.92;
		float Rz = R * 1.05 + air * under * 0.35;
		float e = (d.x * d.x) / (Rx * Rx) + (d.y * d.y) / (Ry * Ry) + (d.z * d.z) / (Rz * Rz);
		if (e >= 1.0 || e < 1e-6) continue;
		p = Bust[i].xyz + d * rsqrt(e);
		float3 n = float3(d.x / (Rx * Rx), d.y / (Ry * Ry), d.z / (Rz * Rz));
		float nl = length(n);
		if (nl > 1e-5) n /= nl;
		else n = float3(0, -0.15, -1);
		if (n.z > 0.05) {
			float side = p.x >= Body.x ? 1 : -1;
			n = normalize(float3(n.x + side * 0.28, min(n.y, -0.12), -0.88));
		}
		p += n * 0.002;
	}
	return p;
}

float3 ArmPush(float3 p)
{
	[unroll] for (int i = 0; i < 4; i++) {
		if (ArmB[i].w < 0) continue;
		float3 a = ArmA[i].xyz;
		float3 b = ArmB[i].xyz;
		float3 ab = b - a;
		float L2 = dot(ab, ab);
		float t = L2 > 1e-8 ? saturate(dot(p - a, ab) / L2) : 0;
		float3 c = a + ab * t;
		float R = lerp(ArmA[i].w, ArmB[i].w, t);
		float3 d = p - c;
		float dist = length(d);
		if (dist >= R) continue;
		float3 n = dist > 1e-4 ? d / dist : float3(0, -1, 0);
		p += n * (R - dist + 0.006);
	}
	return p;
}

// 腰から下腹と背中。測った断面の中にいる布だけ外へ出す。前も後ろも。外はそのまま。
// Bone[5] は腰。回転が取れないときは動かさない（服を飛ばさない）。
float3 TorsoKeep(float3 p)
{
	if (Cnt.x >= (uint)(SMAX - 1)) return p;
	SoftN tor = Sn[Cnt.x];
	if (tor.q.w < 0.5) return p;
	float sc = max(Sim.w, 1e-4);
	float3 posed = p / sc;
	float3 r0 = Bone[5][0].xyz, r1 = Bone[5][1].xyz, r2 = Bone[5][2].xyz;
	float3 q = posed - Bone[5][3].xyz;
	float3 c0 = cross(r1, r2), c1 = cross(r2, r0), c2 = cross(r0, r1);
	float det = dot(r0, c0);
	if (abs(det) < 1e-6) return p;
	float3 ph = float3(dot(q, c0), dot(q, c1), dot(q, c2)) / det;
	float3 back = ph.x * r0 + ph.y * r1 + ph.z * r2 + Bone[5][3].xyz;
	if (length(back - posed) > 0.02) return p;
	float2 d = float2(ph.x - tor.q.x, ph.z - tor.q.z);
	float rl = length(d);
	if (ph.y > tor.q.y + 0.28 || ph.y < tor.q.y - 0.12 || rl < 1e-4) return p;
	float rv[8] = { tor.x.x, tor.x.y, tor.x.z, tor.x.w, tor.p.x, tor.p.y, tor.p.z, tor.p.w };
	float phi = atan2(d.x, d.y);
	float fp = (phi / 6.2831853 + 0.5) * 8.0;
	float fl = floor(fp);
	uint i0 = (uint)(fl + 8.0) % 8;
	float R = lerp(rv[i0], rv[(i0 + 1) % 8], fp - fl);
	if (R < 0.05 || rl >= R) return p;
	float need = R - rl;
	if (need > 0.025) need = 0.025;
	d *= (rl + need) / rl;
	ph.x = tor.q.x + d.x;
	ph.z = tor.q.z + d.y;
	return mul(float4(ph, 1), Bone[5]).xyz * sc;
}

float3 HandDrag(float3 p, SoftN c)
{
	int side = (int)(c.q.z + 0.5);
	if (side < 1 || side > 2) return float3(0, 0, 0);
	float4 L = Limb[side - 1];
	if (L.w < 1e-5) return float3(0, 0, 0);
	int i0 = side == 1 ? 0 : 2;
	float best = 0;
	float3 pull = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 2; k++) {
		int i = i0 + k;
		if (ArmB[i].w < 0) continue;
		float3 a = ArmA[i].xyz;
		float3 b = ArmB[i].xyz;
		float3 ab = b - a;
		float L2 = dot(ab, ab);
		float t = L2 > 1e-8 ? saturate(dot(p - a, ab) / L2) : 0;
		float3 cp = a + ab * t;
		float R = lerp(ArmA[i].w, ArmB[i].w, t);
		float gap = length(p - cp) - R;
		float near = 1 - smoothstep(0.0, 0.10, gap);
		float along = dot(normalize(ab + float3(0, 1e-5, 0)), p - cp);
		// 手の少し上（肩側）。手より先はほとんど引きずらない。
		float band = smoothstep(-0.02, 0.03, -along) * (1 - smoothstep(0.10, 0.22, -along));
		float3 outDir = p - cp;
		float ol = length(outDir);
		outDir = ol > 1e-4 ? outDir / ol : float3(0, -1, 0);
		float sep = dot(outDir, L.xyz / L.w);
		float couple = lerp(0.62, 0.08, smoothstep(-0.2, 0.55, sep));
		float g = near * band * couple;
		if (g > best) { best = g; pull = L.xyz * g; }
	}
	return pull;
}

[numthreads(SMAX, 1, 1)]
void CSSoft(uint id : SV_GroupIndex)
{
	const uint n = min(Cnt.x, (uint)SMAX);
	const bool on = id < n;
	SoftN c = (SoftN)0;
	float3 t = float3(0, 0, 0);
	if (on) {
		c = Sn[id];
		t = BonePos(c.x.xyz, c.q.y);
		if (c.q.x > 0.001 && (c.p.w < 0.5 || c.p.w > 1.5)) {
			float gl = length(Grav.xyz);
			float3 down = gl > 1e-4 ? Grav.xyz / gl : float3(0, -1, 0);
			t += down * (c.q.x * saturate(c.p.z));
		}
	}
	const bool pinned = !on || c.x.w > 0.5 || Sim.z > 0.5;
	if (on && (Sim.z > 0.5 || c.x.w > 0.5)) {
		St[id] = float4(t, 0);
		St[SMAX + id] = float4(t, 0);
		St[2 * SMAX + id] = float4(t, 0);
		St[3 * SMAX + id] = float4(t, 0);
		St[4 * SMAX + id] = float4(0, 0, 0, 0);
	}
	float3 p = t;
	if (on && !pinned) {
		float3 pos = mul(float4(St[id].xyz, 1), Delta).xyz;
		float3 s0 = mul(float4(St[SMAX + id].xyz, 1), Delta).xyz;
		float3 s1 = mul(float4(St[2 * SMAX + id].xyz, 1), Delta).xyz;
		float3 s2 = mul(float4(St[3 * SMAX + id].xyz, 1), Delta).xyz;
		float3 d0 = s0 - s1;
		float3 v = d0 * 0.55 + (s1 - s2) * 0.45;
		float sp = length(d0);
		float vl0 = max(length(v), 1e-5);
		if (sp > vl0 * 1.7) v *= (vl0 * 1.7) / sp;
		float rush = saturate(max(Limb[0].w, Limb[1].w) / (0.050 * max(Sim.w, 0.2)));
		v *= pow(lerp(0.84, 0.68, rush), Sim.y);
		float vl = length(v), vm = lerp(0.035, 0.018, rush) * Sim.w;
		if (vl > vm) v *= vm / vl;
		float dt = Sim.x;
		float gust = 0.88 + 0.12 * sin(Body.w * 0.40 + c.x.y * 0.30);
		float breeze = lerp(1.0, 0.82, rush);
		float amp = length(Wind.xyz + Air.xyz);
		float3 lean = 0;
		if (c.x.w < 0.5 && amp > 0.04) {
			float3 w = Wind.xyz + Air.xyz;
			float wgt = saturate(c.p.z);
			float reach = min(amp * 0.45, c.p.x);
			lean = (w / amp) * reach * wgt;
			lean.y += clamp(Air.w, -0.70, 0.70) * wgt * 0.08;
			if (c.p.w > 1.5) lean.y *= 0.15;
			// 飛行中だけ。止まっていても翼は羽ばたくので、服全体へ弱い乱れ。歩行はマップの風だけ。
			if (Wind.w > 0.5) {
				float ph = c.x.x * 4.0 + c.x.z * 2.5;
				float g = sin(Body.w * 3.5 + ph) * 0.70 + sin(Body.w * 7.1 + ph * 1.2) * 0.30;
				float3 axis = float3(sin(Body.w * 1.6 + ph) * 0.18, 0.04, 1);
				float al = length(axis);
				axis = al > 1e-4 ? axis / al : float3(0, 0, 1);
				lean += axis * (g * min(c.p.x, 0.003) * wgt);
			}
		}
		float3 prevOff = mul(St[4 * SMAX + id].xyz, (float3x3)Delta);
		if (Sim.z > 0.5) prevOff = 0;
		float3 dlt = lean - prevOff;
		float maxStep = 0.12 * dt * Sim.w;
		float dl = length(dlt);
		if (dl > maxStep && dl > 1e-6) dlt *= maxStep / dl;
		float3 off = prevOff + dlt;
		// 袖はスキニングした腕に残す。風のずれは戻らないので、ゆるい袖が腕から離れた位置で止まる。
		if (c.p.w > 0.5 && c.p.w < 1.5) off = 0;
		p = t + off;
		St[4 * SMAX + id] = float4(off, 0);
		St[3 * SMAX + id] = float4(s1, 0);
		St[2 * SMAX + id] = float4(s0, 0);
		St[SMAX + id] = float4(p, 0);
		if (c.p.w > 0.5 && c.p.w < 1.5) p += HandDrag(p, c);
	}
	gT[id] = t;
	gP[id] = p;
	GroupMemoryBarrierWithGroupSync();
	[loop] for (int it = 0; it < 8; it++) {
		p = gP[id];
		if (on && !pinned) {
			float3 d = p - t;
			float3 acc = float3(0, 0, 0);
			float cnt = 0;
			[unroll] for (int e = 0; e < 4; e++) {
				uint j = c.nb[e];
				if (j >= n) continue;
				acc += gP[j] - gT[j];
				cnt += 1;
			}
			if (cnt > 0) d = lerp(d, acc / cnt, 0.45);
			float r = max(c.p.x, 0.01) * Sim.w;
			float l = length(d);
			if (l > r) d *= r / l;
			p = t + d;
			// 袖の点は袖の輪の重心で腕の軸の上にある。腕のカプセルで押すと軸から半径ぶん飛び、袖ごとこぶになる。
			// 袖の当たりは頂点ごとに CSSoftWrite で取る。
			if (c.p.w < 0.5) p = TorsoKeep(BustPush(p, c.q.x));
			else if (c.p.w > 1.5) p = TorsoKeep(p);
		}
		GroupMemoryBarrierWithGroupSync();
		gP[id] = p;
		GroupMemoryBarrierWithGroupSync();
	}
	if (!on) return;
	St[id] = float4(gP[id], 0);
}

[numthreads(64, 1, 1)]
void CSSoftWrite(uint id : SV_DispatchThreadID)
{
	if (id >= Cnt.y) return;
	SoftV c = Sv[id];
	uint o = c.a.x * 36u;
	float3 s = asfloat(Vout.Load3(o));
	float3 sn = asfloat(Vout.Load3(o + 12));
	uint4 k4 = uint4(c.a.y & 0xFFFFu, c.a.y >> 16, c.a.z & 0xFFFFu, c.a.z >> 16);
	float3 d = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 4; k++) {
		uint j = min(k4[k], (uint)(SMAX - 1));
		d += St[4 * SMAX + j].xyz * c.w[k];
	}
	// 形はスキニングした位置のまま、点の遅れと垂れだけを足す。1 本の骨で動かし直すと、
	// 肘の上下で上腕と前腕に分かれて袖が折れ、肘に角が立つ。
	float3 p = s + d * saturate(c.p.w);
	SoftN n0 = Sn[min(k4[0], (uint)(SMAX - 1))];
	if (n0.p.w < 0.5) p = TorsoKeep(BustPush(p, n0.q.x));
	else if (n0.p.w < 1.5) p = ArmPush(p);
	else p = TorsoKeep(p);
	float3 nr = sn;
	float l = dot(nr, nr);
	if (l > 1e-8) nr *= rsqrt(l);
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nr));
}
