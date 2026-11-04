// 髪の束。頭のまわり 24 × 毛先へ 8 段の案内点を、カードの頂点が混ぜて受ける。
// 根元は頭に固定。毛先ほど遅れ、空気の抵抗が強く、重力は弱い（洗ったあとのふわふわ）。
// 首・肩・胴・腕に入った毛は外へ出し、服の厚みの上に載せる。頭蓋は半径の外へ出す。
// 速度は直前 3 フレームの平均。攻撃や飛行の 1 フレームと、当たりの押し出しはなびかせない。微風だけ残す。
// 状態はキャラごと。影と本描画で二度積分しない。
cbuffer HairParm : register(b0)
{
	row_major float4x4 Delta;
	row_major float4x4 Head;   // 髪のローカルから、今のモデル空間へ
	row_major float4x4 Pal;    // 頭の骨（バインドから今の姿勢へ）。頭蓋の当たり用
	row_major float4x4 PalInv;
	float4 Sim;                // x=dt y=歩数 z=1 で置き直し w=時刻
	float4 Wind;               // xyz=風 w=1 で背中にマントがある
	float4 Body;               // xyz=胴の中心 w=身長
	float4 CapA[8];            // カプセル始点 xyz、w=半径。w<0 で無効
	float4 CapB[8];            // 終点 xyz、w=終点の半径
	uint4 Cnt;                 // x=書く頂点の先頭 y=髪型 z=カプセルの数 w=書く頂点の数
	float4 Grav;               // xyz=モデル空間の重力加速度（世界の下） w=胴の太さ（体型。標準 1）
};

// 頭蓋。omake_tactics_skin.hlsl の HairCloth と同じ並び。
cbuffer HairCloth : register(b1)
{
	float4 Cloth[9];
	float4 SkullC;
	float4 SkullR[32];
};

#define HA 24
#define HL 8
#define HG 192

struct Guide
{
	float4 p; // ローカル位置、w=頭の中心からの長さ（m）。短い殻は揺れない
};

struct HairV
{
	uint4 a;  // x=メッシュ頂点 y,z=案内点 4 つ（16bit。髪型内の番号）
	float4 w;
	float4 p; // ローカル位置 w=毛先ほど 1
};

StructuredBuffer<Guide> Gd : register(t0);
StructuredBuffer<HairV> Hv : register(t1);
RWStructuredBuffer<float4> St : register(u0); // [0,HG) 今 [HG,2HG) 前
RWByteAddressBuffer Vout : register(u1);

groupshared float3 gP[HG];
groupshared float3 gR[HG];
groupshared float3 gH[HG]; // 胴の外へ出した静止位置。ここへ戻す。中を通る元の形へは戻さない。

float SkullAt(int e, int a)
{
	int i = clamp(e, 0, 7) * 16 + (a & 15);
	float4 q = SkullR[i >> 2];
	int c = i & 3;
	return c == 0 ? q.x : (c == 1 ? q.y : (c == 2 ? q.z : q.w));
}

float Flop(float hang)
{
	return saturate((hang - 0.04) / 0.22);
}

float3 RestOf(uint id)
{
	Guide g = Gd[Cnt.y * HG + id];
	return mul(float4(g.p.xyz, 1), Head).xyz;
}

float3 Slide(float3 p, float3 a, float3 b, float r0, float r1)
{
	float3 ab = b - a;
	float L2 = dot(ab, ab);
	float t = L2 > 1e-8 ? saturate(dot(p - a, ab) / L2) : 0;
	float3 c = a + ab * t;
	float R = lerp(r0, r1, t);
	if (R < 0.005) return p;
	float3 d = p - c;
	float dist = length(d);
	if (dist >= R) return p;
	float3 n = dist > 1e-4 ? d / dist : float3(1, 0, 0);
	return c + n * (R + 0.016);
}

float3 PushBall(float3 p, float3 c, float R)
{
	float3 d = p - c;
	float dist = length(d);
	if (R < 0.02 || dist >= R || dist < 1e-4) return p;
	return c + d * (R / dist);
}

int HairAxes(out float3 side, out float3 up, out float3 fwd, out float3 head)
{
	side = float3(1, 0, 0);
	up = float3(0, 1, 0);
	fwd = float3(0, 0, -1);
	head = float3(0, 0, 0);
	side = Pal[0].xyz;
	up = Pal[1].xyz;
	float3 back = Pal[2].xyz;
	float bl = length(back), sl = length(side), ul = length(up);
	if (bl < 1e-4 || sl < 1e-4 || ul < 1e-4) return 0;
	back /= bl; side /= sl; up /= ul;
	fwd = -back;
	head = Pal[3].xyz;
	// パレットの平行移動はスキニングの原点で、待機ではほぼ 0。胸から頭の高さへ戻す。
	if (dot(head, head) < 0.04) head = Body.xyz + up * 0.34;
	return 1;
}

// 束の根が顔側なら正、背中側なら負。天辺で根が中心に寄っていても、束の番号で前後を分ける。
float StrandFace(uint guide)
{
	float3 side, up, fwd, head;
	if (!HairAxes(side, up, fwd, head)) return 1;
	uint ia = guide % HA;
	float3 rd = RestOf(ia) - head;
	float alongF = dot(rd, fwd);
	float alongS = dot(rd, side);
	if (abs(alongF) >= 0.012 || abs(alongS) >= 0.012) return alongF;
	float ang = ((float)ia + 0.5) * (6.2831853 / (float)HA) - 3.14159265;
	return -cos(ang);
}

// 胸の球。法線が胴の中を向いていたら、前か横へ出す。谷間へ押し戻さない。
float3 BustOut(float3 p, float3 c, float R, float3 fwd, float3 side)
{
	float3 d = p - c;
	float dist = length(d);
	if (R < 0.02 || dist >= R || dist < 1e-4) return p;
	float3 n = d / dist;
	if (dot(n, fwd) < 0.20) {
		float sd = dot(d, side) >= 0 ? 1 : -1;
		n = normalize(fwd * 0.82 + side * (sd * 0.42) + float3(0, -0.12, 0));
	}
	return p + n * (R - dist + 0.006);
}

// 胴の中に入った毛だけ、今いる方向のまま外へ出す。前の面へ集めない。頭は頭蓋が受ける。
float3 Part(float3 p, float pick)
{
	float3 side, up, fwd, head;
	if (!HairAxes(side, up, fwd, head)) return p;
	float3 chest = Body.xyz;
	float3 d = p - chest;
	float dy = dot(d, up);
	// 胸の関節より 6cm 上は顔と頭頂。ここに断面を掛けると、毛が頭の上で前後へ平らに広がる。
	if (dy > 0.06 || dy < -0.75) return p;
	float3 radial = d - up * dy;
	float f = dot(radial, fwd);
	float s = dot(radial, side);
	float t = saturate(-dy / 0.50);
	// 体型で胴は太る。ふくよかな体ほど断面を広げ、毛が胴の中を通らないようにする。
	float gz = Grav.w > 0.1 ? Grav.w : 1.0;
	// HumF0。胸の関節から、最大の胸が前 19cm、ドレスの前が 21cm。
	float front = lerp(0.200, 0.175, t) * lerp(1.0, gz, 0.7);
	// 背中の 16〜18cm はマントの布。マントが無いときは胴の後ろだけにする。
	float rear = lerp(lerp(0.100, 0.118, t) * gz, lerp(0.160, 0.185, t) * lerp(1.0, gz, 0.5), step(0.5, Wind.w));
	float halfS = lerp(0.185, 0.200, t) * gz;
	float rf = f >= 0.0 ? front : rear;
	float e = (s * s) / (halfS * halfS) + (f * f) / (rf * rf);
	if (e < 1.0 && e > 1e-6) {
		radial *= rsqrt(e);
		float rl = length(radial);
		if (rl > 1e-4) radial += radial * (0.014 / rl);
		p = chest + up * dy + radial;
	} else if (e <= 1e-6) {
		p = chest + up * dy + fwd * ((pick >= 0.0 ? 1.0 : -1.0) * (front + 0.014));
	} else {
		p = chest + up * dy + radial;
	}
	if (pick >= 0.0 && f < front * 0.35 && dy < 0.02) {
		float need = front + 0.020;
		float3 rel = p - chest;
		float pf = dot(rel - up * dot(rel, up), fwd);
		if (pf < need) p += fwd * (need - pf);
	}
	if (dy < 0.06 && dy > -0.20) {
		float gb = lerp(1.0, gz, 0.7);
		p = BustOut(p, chest + fwd * (0.108 * gb) + side * (0.070 * gz) - up * 0.038, 0.132 * gb, fwd, side);
		p = BustOut(p, chest + fwd * (0.108 * gb) - side * (0.070 * gz) - up * 0.038, 0.132 * gb, fwd, side);
	}
	return p;
}

float3 SkullPush(float3 p)
{
	if (SkullC.w < 0.5) return p;
	float3 b = mul(float4(p, 1), PalInv).xyz;
	float3 d = b - SkullC.xyz;
	float r = length(d);
	if (r < 1e-5) return p;
	// 顔の下半分（前髪）は鼻の前へ出るので、頭蓋では押さない。首はカプセルが受ける。
	if (d.z < -0.04 && d.y < -0.03) return p;
	float el = asin(clamp(d.y / r, -1, 1));
	if (el <= -0.55) return p;
	float fa = (atan2(d.x, d.z) + 3.14159265) * (16.0 / 6.2831853) - 0.5;
	float fe = (el + 0.6) * (8.0 / 2.1707963) - 0.5;
	int a0 = (int)floor(fa), e0 = (int)floor(fe);
	float ta = fa - a0, te = fe - e0;
	float R = lerp(lerp(SkullAt(e0, a0), SkullAt(e0, a0 + 1), ta),
		lerp(SkullAt(e0 + 1, a0), SkullAt(e0 + 1, a0 + 1), ta), te);
	// 8mm 足すと頭頂の輪が外へ割れ、渦の穴から地肌が出る。めり込んだ分だけ戻す。
	if (R > 0.03 && r < R + 0.002) b = SkullC.xyz + d * ((R + 0.002) / r);
	return mul(float4(b, 1), Pal).xyz;
}

// 首から下だけ。頭蓋は触らない（根元は CSSkin が頭に付けたまま）。
float3 BodyHit(float3 p, float pick)
{
	if (SkullC.w > 0.5) {
		float3 b = mul(float4(p, 1), PalInv).xyz;
		if (b.y > SkullC.y - 0.02) return p;
	}
	uint nc = min(Cnt.z, 8u);
	[loop] for (uint s = 0; s < nc; s++) {
		if (CapA[s].w < 0) continue;
		p = Slide(p, CapA[s].xyz, CapB[s].xyz, CapA[s].w, CapB[s].w);
	}
	[loop] for (uint s2 = 0; s2 < min(nc, 2u); s2++) {
		if (CapA[s2].w < 0) continue;
		p = Slide(p, CapA[s2].xyz, CapB[s2].xyz, CapA[s2].w, CapB[s2].w);
	}
	return Part(p, pick);
}

[numthreads(HG, 1, 1)]
void CSHair(uint id : SV_GroupIndex)
{
	if (id >= HG) return;
	float3 rest = RestOf(id);
	float hang = Gd[Cnt.y * HG + id].p.w;
	float flop = Flop(hang);
	gR[id] = rest;
	float pick = StrandFace(id);
	float3 home = (id < HA || flop < 0.08) ? rest : BodyHit(rest, pick);
	// 毛先を風下へ置く。移動は前から後ろ、突風はその倍率。根元は動かさない。
	float amp = length(Wind.xyz);
	float3 lean = (amp > 0.02 && flop >= 0.08) ? (Wind.xyz / amp) * min(amp, 2.2) * flop * 0.10 : 0;
	home += lean;
	gH[id] = home;
	// 根元と短い殻は、頭に付けた位置のまま。押すと頭頂が割れる。
	if (Sim.z > 0.5 || flop < 0.08 || id < HA) {
		float3 held = rest + lean;
		gP[id] = held;
		St[id] = float4(held, 0);
		St[HG + id] = float4(held, 0);
		St[2 * HG + id] = float4(held, 0);
		St[3 * HG + id] = float4(held, 0);
	} else {
		float3 pos = mul(float4(St[id].xyz, 1), Delta).xyz;
		float3 s0 = mul(float4(St[HG + id].xyz, 1), Delta).xyz;
		float3 s1 = mul(float4(St[2 * HG + id].xyz, 1), Delta).xyz;
		float3 s2 = mul(float4(St[3 * HG + id].xyz, 1), Delta).xyz;
		float3 d0 = s0 - s1;
		float3 v = d0 * 0.62 + (s1 - s2) * 0.38;
		float sp = length(d0);
		float vl0 = max(length(v), 1e-5);
		if (sp > vl0 * 1.65) v *= (vl0 * 1.65) / sp;
		v *= pow(lerp(0.80, 0.88, flop), Sim.y);
		float vl = length(v);
		float vm = lerp(0.010, 0.028, flop) * max(Body.w, 0.2);
		if (vl > vm) v *= vm / vl;
		float dt = Sim.x;
		float gust = 0.90 + 0.10 * sin(Sim.w * 0.37 + rest.x * 1.7 + rest.z * 0.8);
		float3 p = pos + v;
		p += Grav.xyz * (dt * dt) * flop;
		// 加速度として足す。普段の風はそのまま、それを超えた突風だけ毛先を大きく流す。
		float amp = length(Wind.xyz);
		float gustK = 1.0 + saturate((amp - 0.35) / 0.55) * 2.4;
		p += Wind.xyz * (dt * dt) * flop * gust * 14.0 * gustK;
		float ks = lerp(0.22, 0.012, flop);
		float k = 1 - pow(saturate(1 - ks), Sim.y);
		p = lerp(p, home, k);
		St[3 * HG + id] = float4(s1, 0);
		St[2 * HG + id] = float4(s0, 0);
		St[HG + id] = float4(p, 0);
		gP[id] = p;
	}
	GroupMemoryBarrierWithGroupSync();
	[loop] for (int it = 0; it < 6; it++) {
		float3 p = gP[id];
		uint ia = id % HA;
		uint il = id / HA;
		if (flop >= 0.08 && Sim.z < 0.5) {
			if (il > 0) {
				uint parent = (il - 1) * HA + ia;
				float3 q = gP[parent];
				float3 d = p - q;
				float L0 = length(gR[id] - gR[parent]);
				float L = length(d);
				if (L0 > 1e-4 && L > 1e-5) {
					float lo = L0 * 0.78;
					float hi = L0 * 1.06;
					float L1 = clamp(L, lo, hi);
					p = q + d * (L1 / L);
				}
			}
			if (il == 0) p = rest;
			else {
				float3 home = gH[id];
				float3 acc = p - home;
				float cnt = 1;
				uint iL = il * HA + (ia + HA - 1) % HA;
				uint iR = il * HA + (ia + 1) % HA;
				acc += gP[iL] - gH[iL];
				acc += gP[iR] - gH[iR];
				cnt += 2;
				p = home + lerp(p - home, acc / cnt, 0.28);
			}
		}
		if (flop >= 0.08 && il > 0) p = BodyHit(p, pick);
		GroupMemoryBarrierWithGroupSync();
		gP[id] = p;
		GroupMemoryBarrierWithGroupSync();
	}
	St[id] = float4(gP[id], 0);
}

[numthreads(64, 1, 1)]
void CSHairWrite(uint id : SV_DispatchThreadID)
{
	if (id >= Cnt.w) return;
	HairV c = Hv[Cnt.x + id];
	uint o = c.a.x * 36u;
	float3 rest = mul(float4(c.p.xyz, 1), Head).xyz;
	uint4 k4 = uint4(c.a.y & 0xFFFFu, c.a.y >> 16, c.a.z & 0xFFFFu, c.a.z >> 16);
	float3 d = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 4; k++) {
		uint gi = min(k4[k], (uint)(HG - 1));
		float3 gr = RestOf(gi);
		d += (St[gi].xyz - gr) * c.w[k];
	}
	float w = saturate((c.p.w - 0.05) / 0.22);
	float3 p = rest + d * w;
	// 根元寄りの頂点も、案内点と案内点の間の面も、首・肩・胴の外へ出す。頭蓋より上は BodyHit が触らない。
	p = BodyHit(p, StrandFace(k4[0]));
	float3 nrm = asfloat(Vout.Load3(o + 12));
	float nl = dot(nrm, nrm);
	if (nl > 1e-8) nrm *= rsqrt(nl);
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nrm));
}
