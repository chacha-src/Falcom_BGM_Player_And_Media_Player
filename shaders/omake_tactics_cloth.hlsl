// アバターの衣装。スカートの頂点は足の骨に乗っているので、見た目の形はそのスキニングをそのまま使う。
// 格子は形を作り直さない。裾が世界の下へ 7cm たるむ分と、風で遅れる分だけを足す。
// 頂点が脚の中に入ったときだけ、その面の外へ出す。横へ広げたり、後ろの面を脚へ引き寄せたりしない。
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
	float4 Wind;               // xyz=モデル空間の風の加速度 w=宙に浮いている割合（裾は脚を離れて世界の下へ垂れる）
	float4 Limb[6];            // その足の、このフレームの移動（モデル空間）。w=長さ
	float4 Grav;               // xyz=モデル空間の重力加速度 w=スカート上端のバインド Y
};

cbuffer SkinBone : register(b1)
{
	row_major float4x4 Bones[64];
};

#define NMAX 512
#define PT 10
#define PF 16

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
groupshared float3 gT[NMAX];
groupshared float3 gG[NMAX];

float ProfR(uint s, float t, float phi)
{
	float ft = saturate((t + 0.15) / 1.3) * (PT - 1);
	uint t0 = min((uint)ft, (uint)(PT - 2));
	float a = ft - t0;
	float fp = (phi / 6.2831853 + 0.5) * PF;
	float fl = floor(fp);
	float b = fp - fl;
	uint p0 = (uint)(fl + PF) % PF;
	uint p1 = (p0 + 1) % PF;
	uint o = s * PT * PF;
	float r0 = lerp(Prof[o + t0 * PF + p0], Prof[o + t0 * PF + p1], b);
	float r1 = lerp(Prof[o + (t0 + 1) * PF + p0], Prof[o + (t0 + 1) * PF + p1], b);
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
	uint ns = min(Cnt.y, useFeet > 0.5 ? 6u : 4u);
	[loop] for (int rep = 0; rep < 8; rep++) {
		float bestPen = 0;
		float3 bestN = float3(0, 0, -1);
		[loop] for (uint s = 0; s < ns; s++) {
			float4 A = Seg[s * 4];
			float3 ax = Seg[s * 4 + 1].xyz;
			float len = max(A.w, 1e-4);
			float3 q = mul(float4(p / Sim.w, 1), Inv[s]).xyz - A.xyz;
			float t = dot(q, ax) / len;
			float t1 = s >= 4u ? 1.35 : 1.12;
			if (t < Seg[s * 4 + 1].w - 0.02 || t > t1) continue;
			float tc = clamp(t, 0, 1);
			float3 rv = q - ax * (tc * len);
			float3 u = Seg[s * 4 + 2].xyz;
			float3 w = Seg[s * 4 + 3].xyz;
			float fu = dot(rv, u);
			float fw = dot(rv, w);
			float dist = length(rv);
			float k = Seg[s * 4 + 2].w > 0 ? smoothstep(0, 1, (tc - Seg[s * 4 + 1].w) / max(Seg[s * 4 + 2].w, 1e-4)) : 1;
			float fuPhi = (pan == 1u && s < 4u) ? max(fu, 0.01) : ((pan == 2u && s < 4u) ? min(fu, -0.01) : fu);
			float phi = atan2(fw, fuPhi);
			float R = ProfR(s, tc, phi) * k - allow + 0.008;
			if (R < 0.026) R = 0.026;
			float pen = 0;
			float3 dirLocal = rv;
			if (pan == 1u && s < 4u && fu < 0 && abs(fw) < R) {
				pen = R * sqrt(max(1.0 - (fw / R) * (fw / R), 0.0)) - fu;
				dirLocal = u * max(-fu, 0.02) + w * (fw * 0.25);
			} else if (pan == 2u && s < 4u && fu > 0 && abs(fw) < R) {
				pen = R * sqrt(max(1.0 - (fw / R) * (fw / R), 0.0)) + fu;
				dirLocal = -u * max(fu, 0.02) + w * (fw * 0.25);
			} else if (dist < R) {
				pen = R - dist;
				if (pan == 1u && s < 4u && fu < 0.015) dirLocal = u * 0.03 + w * fw;
				else if (pan == 2u && s < 4u && fu > -0.015) dirLocal = -u * 0.03 + w * fw;
			}
			if (pen <= 0) continue;
			float dl = length(dirLocal);
			float3 n = dl > 1e-4 ? mul(dirLocal / dl, (float3x3)Pal[s]) : float3(0, 0, pan == 2u ? 1 : -1);
			float nl = length(n);
			n = nl > 1e-5 ? n / nl : float3(0, 0, pan == 2u ? 1 : -1);
			if (pen > bestPen) { bestPen = pen; bestN = n; }
		}
		if (bestPen <= 0) break;
		float step = min(bestPen + 0.004, 0.065);
		p += bestN * step * Sim.w;
	}
	return p;
}
float3 Collide(float3 p, float allow)
{
	return CollideBody(p, allow, 1, 0u);
}

// 腰とお腹。脚の形は付け根を空けてあるので、そこを軸から外へ出す。
float3 HipOut(float3 p)
{
	float3 ph = mul(float4(p / Sim.w, 1), HipInv).xyz;
	float2 d = float2(ph.x - Mode.y, ph.z - Mode.z);
	float rl = length(d);
	float y = ph.y;
	float top = Grav.w;
	if (y > top + 0.04 || y < top - 0.52) return p;
	float t = saturate((top - y) / 0.36);
	float R = lerp(0.152, 0.185, t);
	if (rl >= R || rl < 1e-4) return p;
	d *= (R + 0.010) / rl;
	ph.x = Mode.y + d.x;
	ph.z = Mode.z + d.y;
	return mul(float4(ph, 1), Hips).xyz * Sim.w;
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

float3 GravDown()
{
	float gl = length(Grav.xyz);
	return gl > 1e-4 ? Grav.xyz / gl : float3(0, -1, 0);
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
float3 FrontHang(float3 x, float3 skinned)
{
	float drop = max(Grav.w - x.y, 0);
	float3 a = float3(x.x, Grav.w, x.z - 0.038 * saturate(drop / 0.45));
	float3 hung = (mul(float4(a, 1), Hips).xyz + GravDown() * drop) * Sim.w;
	float3 ph = mul(float4(hung / max(Sim.w, 1e-4), 1), HipInv).xyz;
	float3 ps = mul(float4(skinned / max(Sim.w, 1e-4), 1), HipInv).xyz;
	if (ps.z < ph.z) {
		ph.z = ps.z;
		hung = mul(float4(ph, 1), Hips).xyz * Sim.w;
	}
	return hung;
}

// 両脇スリットの後ろ布。上端はスキニングした位置のまま繋ぎ、そこから下は布自身の丈だけ垂らす。
// スキニング位置へさらに足すと、前より後ろが長くなり、裾に段も出る。中央は脚と脚の間へ寄せる。
float3 BackHang(float3 x, float ySlit, float3 skinned)
{
	float drop = max(ySlit - x.y, 0);
	float t = smoothstep(0.0, 0.22, drop);
	float mid = 1.0 - smoothstep(0.02, 0.13, abs(x.x - Mode.y));
	float3 a = float3(lerp(x.x, Mode.y, 0.55 * t * mid), ySlit, x.z - 0.045 * mid * t);
	float3 hung = (mul(float4(a, 1), Hips).xyz + GravDown() * drop) * Sim.w;
	return lerp(skinned, hung, t);
}

// 一周つながったスカート。腰の前傾で後ろだけ持ち上がると、同じ丈なのに前が長く見える。
// 裾は前後とも、腰の高さからバインドの丈だけ重力で垂らして揃える。
float3 LevelHang(float3 x)
{
	float drop = max(Grav.w - x.y, 0);
	float3 a = float3(x.x, Grav.w, x.z);
	return (mul(float4(a, 1), Hips).xyz + GravDown() * drop) * Sim.w;
}

// 腰に付いた最上段から 12cm かけて垂らし始める。上端で折れない。
float HangIn(float y)
{
	return smoothstep(0.0, 0.12, Grav.w - y);
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

// 膝より下の余り布だけ。前後同じ。太ももに乗った布は体が勝って動かさない。
float FlyDrape(ClothN c, float3 skinned)
{
	float fly = saturate(Wind.w);
	if (fly < 0.001 || Mode.x > 0.35) return 0;
	float belowKnee = saturate((Grav.w - 0.32 - c.x.y) / 0.28);
	float free = saturate(Clearance(skinned) / 0.04);
	// 前後に分かれた布は PanelHang が丈いっぱいに垂らしているので、さらに伸ばさない。
	return belowKnee * belowKnee * free * 0.10 * Sim.w * fly * (1 - saturate(c.q.w));
}
float3 NodeTarget(ClothN c)
{
	float3 h = BonePos(c);
	if (c.p.w < 0.5) {
		float below = max(Grav.w - c.x.y, 0);
		float3 g = Grav.xyz;
		float gl = length(g);
		float3 down = gl > 1e-4 ? g / gl : float3(0, -1, 0);
		float sag = saturate((below - 0.04) / 0.50) * 0.040;
		h += down * sag;
	}
	h *= Sim.w;
	float fly = saturate(Wind.w);
	if (fly > 0.001 && Mode.x < 0.35 && c.p.w < 0.5 && c.q.w > 0.001) {
		if (c.q.z < 1.5) {
			h = lerp(h, FrontHang(c.x.xyz, h), fly * saturate(c.q.w) * HangIn(c.x.y));
		} else {
			float ySlit = Grav.w - max(c.q.z - 2.0, 0.0);
			h = lerp(h, BackHang(c.x.xyz, ySlit, h), fly * saturate(c.q.w));
		}
	} else if (fly > 0.001 && Mode.x < 0.35 && c.p.w < 0.5) {
		float hem = saturate((Grav.w - 0.16 - c.x.y) / 0.22);
		if (hem > 0.001) h = lerp(h, LevelHang(c.x.xyz), hem * fly);
	}
	return h;
}

// 風下へ裾を寄せる。移動の向かい風も突風も、強さに比例して 18cm まで。
float3 WindPush(ClothN c)
{
	if (c.p.w > 0.5) return 0;
	float amp = length(Wind.xyz);
	if (amp < 0.05) return 0;
	float reach = min(amp * 0.11, 0.18);
	return (Wind.xyz / amp) * (reach * saturate(c.p.z) * Sim.w);
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
		float near = 1 - smoothstep(0.0, 0.12, gap);
		if (near <= 0.001) continue;
		float3 local = A.xyz + ax * (tc * A.w);
		float3 closest = mul(float4(local, 1), Pal[s]).xyz * Sim.w;
		float3 ph = mul(float4(p / Sim.w, 1), HipInv).xyz;
		float3 ch = mul(float4(closest / Sim.w, 1), HipInv).xyz;
		float rise = ph.y - ch.y;
		// 接触の少し上。足は靴の下から甲の上まで、前に出した布を連れていく。
		float band = s >= 4u
			? smoothstep(-0.06, 0.02, rise) * (1 - smoothstep(0.18, 0.34, rise))
			: smoothstep(-0.02, 0.035, rise) * (1 - smoothstep(0.12, 0.26, rise));
		if (band <= 0.001) continue;
		float3 outDir = p - closest;
		float ol = length(outDir);
		outDir = ol > 1e-4 ? outDir / ol : float3(0, 0, 1);
		float sep = dot(outDir, disp / dl);
		float couple = lerp(0.70, 0.08, smoothstep(-0.2, 0.55, sep));
		float g = near * band * couple * lerp(0.55, 1.0, saturate(c.q.x));
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

[numthreads(64, 1, 1)]
void CSClothStep(uint id : SV_DispatchThreadID)
{
	if (id >= min(Cnt.x, (uint)NMAX)) return;
	ClothN c = Cn[id];
	float3 t = NodeTarget(c);
	if (Sim.z > 0.5 || c.p.w > 0.5) {
		float3 q = t + WindPush(c);
		St[id] = float4(q, 0);
		St[NMAX + id] = float4(q, 0);
		St[2 * NMAX + id] = float4(q, 0);
		St[4 * NMAX + id] = float4(q, 0);
		St[5 * NMAX + id] = float4(q, 0);
		St[6 * NMAX + id] = float4(q, 0);
		return;
	}
	// 0 今の見た目。1,4,5,6 は引きずりと当たりの前の位置（新しい順）。当たりの押し出しは速度に入れない。
	float3 pos = mul(float4(St[id].xyz, 1), Delta).xyz;
	float3 s0 = mul(float4(St[NMAX + id].xyz, 1), Delta).xyz;
	float3 s1 = mul(float4(St[4 * NMAX + id].xyz, 1), Delta).xyz;
	float3 s2 = mul(float4(St[5 * NMAX + id].xyz, 1), Delta).xyz;
	float3 s3 = mul(float4(St[6 * NMAX + id].xyz, 1), Delta).xyz;
	float rush = Rush();
	float3 v = Trail(s0 - s1, s1 - s2, s2 - s3);
	v *= pow(lerp(0.86, 0.70, rush), Sim.y);
	float vl = length(v);
	float vm = lerp(0.040, 0.022, rush) * Sim.w;
	if (vl > vm) v *= vm / vl;
	float dt = Sim.x;
	// 微風。点ごとに向きをばらすと裾が波打つので、ほぼ同じ向きで裾だけ少し遅れる。
	float gust = 0.86 + 0.14 * sin(Mode.w * 0.42 + c.x.z * 0.35);
	float breeze = lerp(1.0, 0.40, rush);
	float3 p = pos + v;
	p += Wind.xyz * (dt * dt) * c.p.z * gust * breeze * 16.0;
	t += GravDown() * FlyDrape(c, t);
	t += WindPush(c);
	float k = 1 - pow(saturate(1 - max(c.p.y, 0.42)), Sim.y);
	p = lerp(p, t, k);
	float3 mot = p;
	St[6 * NMAX + id] = float4(s2, 0);
	St[5 * NMAX + id] = float4(s1, 0);
	St[4 * NMAX + id] = float4(s0, 0);
	St[NMAX + id] = float4(mot, 0);
	St[2 * NMAX + id] = float4(p, 0);
}

// 1 グループで全部の点。目標からのずれをとなりと混ぜてならし、離れすぎを抑え、脚の形の外へ出す。12 回。
[numthreads(NMAX, 1, 1)]
void CSClothRelax(uint id : SV_GroupIndex)
{
	const uint n = min(Cnt.x, (uint)NMAX);
	const bool on = id < n;
	ClothN c = (ClothN)0;
	float3 t = float3(0, 0, 0);
	if (on) {
		c = Cn[id];
		t = NodeTarget(c);
	}
	if (Sim.z > 0.5) {
		if (on) {
			float3 p = t + WindPush(c);
			St[id] = float4(p, 0);
			St[3 * NMAX + id] = float4(p - BonePos(c) * Sim.w, 0);
		}
		return;
	}
	const bool fixed = !on || c.p.w > 0.5;
	float3 down = GravDown();
	gT[id] = t;
	gG[id] = t;
	gP[id] = (on && !fixed) ? St[2 * NMAX + id].xyz : t;
	GroupMemoryBarrierWithGroupSync();
	// 余り布だけ垂らしてから、脚の中は体が勝つように押し出す。
	uint nodePan = c.q.z > 1.5 ? 2u : (c.q.z > 0.5 ? 1u : 0u);
	float nodeFeet = (nodePan > 0u && Wind.w > 0.5 && Mode.x < 0.35 && c.q.w > 0.5) ? 0 : 1;
	float3 goal = t + down * (fixed ? 0 : FlyDrape(c, t));
	if (!fixed) goal += WindPush(c);
	if (!fixed) goal = CollideBody(goal, c.q.y, nodeFeet, nodePan);
	gG[id] = goal;
	GroupMemoryBarrierWithGroupSync();
	for (int it = 0; it < 16; it++) {
		float3 p = gP[id];
		if (!fixed) {
			float3 d = p - goal;
			float3 acc = float3(0, 0, 0);
			float cnt = 0;
			[unroll] for (int e = 0; e < 4; e++) {
				uint j = c.nb[e];
				if (j >= n) continue;
				acc += gP[j] - gG[j];
				cnt += 1;
			}
			if (cnt > 0) d = lerp(d, acc / cnt, 0.72);
			// 垂れた形から横へはあまり開かない。下方向の揺れだけ残す。
			float along = dot(d, down);
			float3 perp = d - down * along;
			float side = 0.040 * Sim.w;
			float slack = 0.040 * Sim.w;
			float pl = length(perp);
			if (pl > side) perp *= side / pl;
			along = clamp(along, -0.04 * Sim.w, slack);
			d = perp + down * along;
			p = goal + d;
		}
		GroupMemoryBarrierWithGroupSync();
		gP[id] = p;
		GroupMemoryBarrierWithGroupSync();
	}
	if (!on) return;
	float3 p = gP[id];
	if (!fixed) p = HipOut(CollideBody(p, c.q.y, nodeFeet, nodePan));
	St[id] = float4(p, 0);
	St[3 * NMAX + id] = float4(p - BonePos(c) * Sim.w, 0);
}

[numthreads(64, 1, 1)]
void CSClothWrite(uint id : SV_DispatchThreadID)
{
	if (id >= Cnt.z) return;
	ClothV c = Cv[id];
	uint o = c.a.x * 36u;
	float3 s = asfloat(Vout.Load3(o));
	float3 sn = asfloat(Vout.Load3(o + 12));
	uint4 k4 = uint4(c.a.y & 0xFFFFu, c.a.y >> 16, c.a.z & 0xFFFFu, c.a.z >> 16);
	float3 d = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 4; k++) d += St[3 * NMAX + min(k4[k], (uint)(NMAX - 1))].xyz * c.w[k];
	// 描かれる頂点は足の骨の位置で来る。格子が腰に付いていると、足すずれは数センチで、裾は靴に残る。
	// 前の裾が腰の垂れから 4.5cm 以上靴側へ運ばれていて、格子がその戻りを含んでいないとき、起点を腰の垂れに置き換える。
	float3 pull = d * c.p.w;
	float3 base = s;
	float fly = saturate(Wind.w);
	float leftShoe = 0;
	uint pan = c.a.w & 3u;
	float panW = (pan > 0u && fly > 0.001 && Mode.x < 0.35) ? fly * ((float)((c.a.w >> 8) & 255u) / 255.0) : 0;
	if (pan == 1u && panW > 0.001) {
		// 両脇スリットの前布。腰から重力で真下へ垂らし、格子の揺れだけを足す。
		panW *= HangIn(c.p.y);
		float3 dyn = float3(0, 0, 0);
		[unroll] for (int kk = 0; kk < 4; kk++) {
			uint q = min(k4[kk], (uint)(NMAX - 1));
			dyn += (St[q].xyz - NodeTarget(Cn[q])) * c.w[kk];
		}
		base = lerp(s, FrontHang(c.p.xyz, s) + dyn * c.p.w - pull, panW);
		leftShoe = panW > 0.5 ? 1 : 0;
	} else if (pan == 2u && panW > 0.001) {
		// 両脇スリットの後ろ布。スリット上端から布の丈だけ垂らし、中央は脚と脚の間へ落とす。
		float ySlit = Grav.w - (float)(c.a.w >> 16) * 0.001;
		float3 dyn = float3(0, 0, 0);
		[unroll] for (int kk = 0; kk < 4; kk++) {
			uint q = min(k4[kk], (uint)(NMAX - 1));
			dyn += (St[q].xyz - NodeTarget(Cn[q])) * c.w[kk];
		}
		base = lerp(s, BackHang(c.p.xyz, ySlit, s), panW) + dyn * (c.p.w * panW) - pull * panW;
		leftShoe = panW > 0.5 ? 1 : 0;
	} else if (fly > 0.001 && Mode.x < 0.35 && c.p.w > 0.25) {
		// 裾だけ腰の丈に揃える。格子の揺れは足し、骨からのずれは二重に足さない。
		float hemK = saturate((Grav.w - 0.16 - c.p.y) / 0.22) * fly;
		if (hemK > 0.001) {
			float3 dyn = float3(0, 0, 0);
			[unroll] for (int kk = 0; kk < 4; kk++) {
				uint q = min(k4[kk], (uint)(NMAX - 1));
				dyn += (St[q].xyz - NodeTarget(Cn[q])) * c.w[kk];
			}
			base = lerp(s, LevelHang(c.p.xyz), hemK) + dyn * (c.p.w * hemK) - pull * hemK;
		}
		leftShoe = hemK > 0.45 ? 1 : 0;
	}
	// 靴から離した裾は、靴のカプセルで表面へ戻さない。太ももとすねには当たる。
	float3 p = HipOut(CollideBody(base + pull, c.n.w, leftShoe > 0.5 ? 0 : 1, pan));
	float3 nr = sn;
	float l = dot(nr, nr);
	if (l > 1e-8) nr *= rsqrt(l);
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nr));
}
