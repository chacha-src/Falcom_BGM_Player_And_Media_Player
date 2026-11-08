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
	float4 Wind;               // xyz=強風・暴風と移動 w=1 で背中にマントがある
	float4 Air;                // xyz=そよ風 w=上下。上が正
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
groupshared float3 gSide, gUp, gFwd, gHead;
groupshared uint gAxes;
groupshared float gReach2;

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
	if (dist >= R + 0.016) return p;
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

// 束の軸と、胴・腕が届く距離。点ごとに正規化し直さない。届かない毛先は当たりを掛けない。
void BuildHairFrame(uint gid)
{
	if (gid == 0) {
		float3 side, up, fwd, head;
		gAxes = (uint)HairAxes(side, up, fwd, head);
		gSide = side;
		gUp = up;
		gFwd = fwd;
		gHead = head;
		float gz = Grav.w > 0.1 ? Grav.w : 1.0;
		float reach = 0.80 * max(gz, 1.0) + 0.15;
		uint nc = min(Cnt.z, 8u);
		[loop] for (uint s = 0; s < nc; s++) {
			if (CapA[s].w < 0) continue;
			float r = max(CapA[s].w, CapB[s].w) + 0.020;
			float3 ca = CapA[s].xyz - Body.xyz;
			float3 cb = CapB[s].xyz - Body.xyz;
			reach = max(reach, length(ca) + r);
			reach = max(reach, length(cb) + r);
		}
		gReach2 = reach * reach;
	}
	GroupMemoryBarrierWithGroupSync();
}

// 束の根が顔側なら正、背中側なら負。天辺で根が中心に寄っていても、束の番号で前後を分ける。
float StrandFace(uint guide)
{
	if (!gAxes) return 1;
	float3 side = gSide, fwd = gFwd, head = gHead;
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
	if (!gAxes) return p;
	float3 side = gSide, up = gUp, fwd = gFwd;
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

float SkullRad(float3 d)
{
	float r = length(d);
	if (r < 1e-5 || d.y <= -0.15) return 0;
	float el = asin(clamp(d.y / r, -1, 1));
	float R = 0.0;
	// 頭頂（仰角 1.35 以上）も測った半径を使う。ここを楕円体だけにすると、どの風でも毛がその円錐へ入る。
	if (el > -0.58) {
		float fa = (atan2(d.x, d.z) + 3.14159265) * (16.0 / 6.2831853) - 0.5;
		float fe = (el + 0.6) * (8.0 / 2.1707963) - 0.5;
		int a0 = (int)floor(fa), e0 = (int)floor(fe);
		float ta = fa - a0, te = fe - e0;
		float r00 = min(SkullAt(e0, a0), 0.20);
		float r10 = min(SkullAt(e0, a0 + 1), 0.20);
		float r01 = min(SkullAt(e0 + 1, a0), 0.20);
		float r11 = min(SkullAt(e0 + 1, a0 + 1), 0.20);
		float Rlerp = lerp(lerp(r00, r10, ta), lerp(r01, r11, ta), te);
		float Rmax = max(max(r00, r10), max(r01, r11));
		// 補間の谷だけ上げる。滑らかな頭では Rlerp のまま。谷の方位から風が来ると毛が中へ入る。
		R = min(max(Rlerp, Rmax - 0.020), 0.20);
	}
	float3 u = d / r;
	float rz = u.z < 0.0 ? 0.100 : 0.094;
	float ry = u.y < 0.0 ? 0.125 : 0.100;
	float rx = 0.086;
	float e2 = (u.x * u.x) / (rx * rx) + (u.y * u.y) / (ry * ry) + (u.z * u.z) / (rz * rz);
	float faceR = rsqrt(max(e2, 1e-4)) * smoothstep(-0.15, -0.09, d.y);
	R = max(R, faceR);
	if (R < 0.03 && el > -0.55) R = 0.095;
	return R;
}

// 頭蓋と顔（額・目・鼻・頬・顎）の外へ出し、顔の正面に入り込んだ束は顔の左右・前後へ滑らかに振り分ける。
// 頭頂の毛が下へ沈んでハゲになるのも防ぐ。
float3 SkullPush(float3 p, float3 keep)
{
	if (SkullC.w < 0.5) return p;
	float3 b = mul(float4(p, 1), PalInv).xyz;
	float3 k = mul(float4(keep, 1), PalInv).xyz;
	float3 C = SkullC.xyz;
	float3 dK = k - C;
	float3 dP = b - C;
	// 頭頂に近い毛が重力や風で頭蓋の下へ沈むとハゲるので、元の高さを下回らないように支える。
	if (dK.y > 0.035 && dP.y < dK.y - 0.012) {
		dP.y = lerp(dP.y, dK.y, 0.75);
		b.y = C.y + dP.y;
	}
	float rK = length(dK), rP = length(dP);
	if (rP < 1e-5) {
		float3 side0 = rK > 1e-4 ? dK : float3(0, 1, 0);
		float Rs0 = max(SkullRad(side0), 0.095);
		b = C + side0 * ((Rs0 + 0.008) / max(length(side0), 1e-4));
		return mul(float4(b, 1), Pal).xyz;
	}
	float3 dirK = rK > 1e-4 ? dK / rK : dP / rP;
	float3 dirP = dP / rP;
	// 反対側（後ろ髪が顔の前へ、前髪が後頭へ）へ頭蓋を貫通していたら、元の側の面へ戻す。
	if (rK > 1e-4 && dot(dK.xz, dP.xz) < -0.0008 && dP.y > -0.14 && rP < 0.45) {
		dP.xz = dK.xz;
		rP = length(dP);
		dirP = rP > 1e-4 ? dP / rP : dirK;
	}
	// 目の高さ〜顎（dP.y < 0.01, dP.z < -0.02）で顔の中央に入った毛は、顔の左右（こめかみ・頬の外）へ流す。
	if (dP.z < -0.025 && dP.y < 0.015 && dP.y > -0.14) {
		float faceBand = smoothstep(0.015, -0.035, dP.y) * smoothstep(-0.14, -0.09, dP.y);
		float sgnX = abs(dK.x) > 0.005 ? (dK.x >= 0.0 ? 1.0 : -1.0) : (dP.x >= 0.0 ? 1.0 : -1.0);
		float wantX = 0.068 * faceBand;
		if (abs(dP.x) < wantX) {
			dP.x = lerp(dP.x, sgnX * wantX, 0.65 * faceBand);
			rP = length(dP);
			dirP = rP > 1e-4 ? dP / rP : dirK;
		}
	}
	// 頭蓋は半径 13cm まで。届かない毛先は測らない。反対側へ抜けた束だけ、元の側へ戻す。
	// 0.24 まで見る。上からの風で静止位置より沈んでも、ここで押し戻せる。
	if (rP > 0.50)
		return mul(float4(b, 1), Pal).xyz;
	if (rP > 0.24 && !(rK > 1e-4 && dot(dK, dP) < 0.0))
		return mul(float4(b, 1), Pal).xyz;
	if (rP > 0.16 && rK > 1e-4 && dot(dK, dP) < 0.0) {
		float Rs = max(SkullRad(dK), 0.095);
		float mkFar = (dP.z < -0.01 || dK.z < -0.01) ? 0.010 : 0.008;
		b = C + dirK * (Rs + mkFar);
		return mul(float4(b, 1), Pal).xyz;
	}
	float Rp = SkullRad(dP);
	float Rk = SkullRad(dK);
	float mk = (dP.z < -0.01 || dK.z < -0.01) ? 0.010 : 0.008;
	// 今いる方位の面まで出す。静止位置の半径で戻すと、頭が太い方位では面の内側に残る。
	if (Rp > 0.02 && rP < Rp + mk - 0.001) {
		b = C + dirP * (Rp + mk);
		return mul(float4(b, 1), Pal).xyz;
	}
	if (rK > 1e-4 && dot(dK, dP) < 0.0 && rP < 0.50) {
		float Rs = max(Rk, 0.095);
		b = C + dirK * (Rs + mk);
		return mul(float4(b, 1), Pal).xyz;
	}
	return mul(float4(b, 1), Pal).xyz;
}

// 首・肩・胸・背中。頭蓋の下端より下を、顔の前後左右や肩の上へ滑らかに振り分ける。
float3 BodyHit(float3 p, float pick)
{
	float3 fromBody = p - Body.xyz;
	if (dot(fromBody, fromBody) > gReach2) return p;
	if (SkullC.w > 0.5) {
		float3 b = mul(float4(p, 1), PalInv).xyz;
		if (b.y > SkullC.y + 0.01) return p;
	}
	uint nc = min(Cnt.z, 8u);
	[loop] for (uint s = 0; s < nc; s++) {
		if (CapA[s].w < 0) continue;
		p = Slide(p, CapA[s].xyz, CapB[s].xyz, CapA[s].w, CapB[s].w);
	}
	[loop] for (uint s2 = 0; s2 < min(nc, 4u); s2++) {
		if (CapA[s2].w < 0) continue;
		p = Slide(p, CapA[s2].xyz, CapB[s2].xyz, CapA[s2].w, CapB[s2].w);
	}
	return Part(p, pick);
}

// 頭蓋の表面に乗っている殻は風で剥がさない。表面から 2cm 以内はほぼ固定、12cm 離れた毛先だけ今まで通り流す。
float ScalpPeel(float3 restModel)
{
	if (SkullC.w < 0.5) return 1;
	float3 d = mul(float4(restModel, 1), PalInv).xyz - SkullC.xyz;
	float slack = length(d) - max(SkullRad(d), 0.09);
	return saturate((slack - 0.02) / 0.10);
}

// 0 は頭蓋の骨に固定。1 は毛先として流してよい。
// 顎より上（頭頂・側頭・後頭）は強風でも動かさない。顎より下の長さだけ流す。
float StrandFree(float3 restModel)
{
	if (SkullC.w < 0.5) return 1;
	float3 d = mul(float4(restModel, 1), PalInv).xyz - SkullC.xyz;
	return saturate((-d.y - 0.10) / 0.06);
}

[numthreads(HG, 1, 1)]
void CSHair(uint id : SV_GroupIndex)
{
	BuildHairFrame(id);
	if (id >= HG) return;
	float3 rest = RestOf(id);
	float hang = Gd[Cnt.y * HG + id].p.w;
	float flop = Flop(hang);
	gR[id] = rest;
	float pick = StrandFace(id);
	float3 keep = SkullPush(rest, rest);
	float3 home = (id < HA || flop < 0.08) ? keep : BodyHit(keep, pick);
	// 強い風は毛先を揃え、そよ風は束ごとにずらす。根元は頭に残す。
	float crown = smoothstep(0.16, 0.46, flop);
	float3 flow = Wind.xyz + Air.xyz;
	float b = length(Air.xyz);
	if (b > 0.02)
		flow += Air.xyz * (0.15 * sin(Sim.w * 1.3 + (float)(id % HA) * 0.45));
	flow.y += clamp(Air.w, -0.35, 0.35) * 0.35;
	if (abs(Air.w) > 0.62) flow.y += clamp(Air.w, -1.2, 1.2) * 0.12;
	// 作成プレビューは毛先 10cm まで。それを超える風は舞台だけ。顔の中には出さない。
	float famp = length(flow);
	float reach = min(famp, 0.15) * 0.40;
	if (famp > 0.15) reach += min(famp - 0.15, 0.50) * 0.12;
	if (famp > 0.65) reach += min(famp - 0.65, 0.70) * 0.05;
	if (famp > 2.05) reach += min(famp - 2.05, 2.6) * 0.16;
	// 頭を覆う殻と顎までの毛（中心から 42cm 以内）は、プレビューと同じ 10cm まで。
	// 舞台の強風で殻ごと後ろへ飛ばすと、どの距離から見ても頭頂が禿げて毛がずれる。
	// 胸より下の毛先だけ、今まで通り長く流す。プレビューは famp が 2.05 を超えないので従来どおり。
	float tip = saturate((hang - 0.42) / 0.28);
	float capShell = 0.10;
	float capStage = 0.10 + saturate((famp - 2.05) / 2.6) * 0.40;
	reach = min(reach, lerp(capShell, capStage, tip));
	float stepLim = 0.020 + saturate((famp - 2.05) / 2.2) * 0.040;
	reach *= 0.94 + 0.06 * sin(Sim.w * 2.1);
	// 風は体と同じモデル空間。頭を覆う毛は静止位置のまま、頭から離れた毛先だけ流す。
	float3 flowH = flow;
	float shell = StrandFree(rest);
	float3 lean = (famp > 0.02 && crown > 0.001) ? (flowH / famp) * reach * crown * shell : 0;
	if (id >= HA && flop >= 0.08) {
		float3 n = rest - mul(float4(SkullC.xyz, 1), Pal).xyz;
		float nl = length(n);
		float3 around = lean;
		if (nl > 1e-4) {
			n /= nl;
			around = lean - n * dot(lean, n);
		}
		// 表面に付いた殻は舞台の強風でも剥がさない。毛先だけ、今まで通り流す。
		float peel = ScalpPeel(rest);
		float al = length(around);
		float lim = lerp(0.006, al, peel);
		if (al > 1e-5 && al > lim) around *= lim / al;
		home = BodyHit(SkullPush(keep + around, keep), pick);
		home = SkullPush(home, keep);
	}
	gH[id] = home;
	// 根元も短い殻も、静止位置が頭蓋の中なら表面へ出す。中に置くとカードが頭をくり抜く。
	if (Sim.z > 0.5 || flop < 0.08 || id < HA) {
		float3 held = (id < HA) ? SkullPush(rest, rest) : SkullPush(BodyHit(SkullPush(rest, rest), pick), rest);
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
		float vm = 0.015 * max(Body.w, 0.2);
		if (vl > vm) v *= vm / vl;
		float dt = Sim.x;
		float gust = 0.90 + 0.10 * sin(Sim.w * 0.37 + rest.x * 1.7 + rest.z * 0.8);
		float3 p = pos + v;
		p += Grav.xyz * (dt * dt) * flop;
		// 加速度として足す。普段の風はそのまま、それを超えた突風は毛先だけ大きく流す。
		float ampW = length(flow);
		float gustK = 1.0 + saturate((ampW - 0.55) / 0.70) * 1.5;
		float crownW = smoothstep(0.16, 0.46, flop);
		// 上から吹く風の、頭蓋の中心へ向かう成分は足さない。接線方向には今まで通り流す。
		float3 pushF = flowH;
		if (SkullC.w > 0.5) {
			float3 skullM = mul(float4(SkullC.xyz, 1), Pal).xyz;
			float3 away = p - skullM;
			float al2 = dot(away, away);
			if (al2 > 1e-6) {
				float3 nd = away * rsqrt(al2);
				float into = dot(pushF, nd);
				if (into < 0) pushF -= nd * into;
			}
		}
		p += pushF * (dt * dt) * crownW * shell * ScalpPeel(rest) * gust * 14.0 * gustK;
		float ks = lerp(0.22, 0.012, flop);
		float k = 1 - pow(saturate(1 - ks), Sim.y);
		float3 delta = (home - p) * k;
		float dlh = length(delta);
		// 頭蓋の半径は約 9cm。2cm なら中心を越えない。越えると近い面が顔側になり、毛が前へ抜ける。
		if (dlh > stepLim) delta *= stepLim / dlh;
		p += delta;
		// 押し出す前の位置を速度に残すと、次のフレームがまた頭蓋の中へ引き戻す。
		p = SkullPush(p, keep);
		St[3 * HG + id] = float4(s1, 0);
		St[2 * HG + id] = float4(s0, 0);
		St[HG + id] = float4(p, 0);
		gP[id] = p;
	}
	GroupMemoryBarrierWithGroupSync();
	[loop] for (int it = 0; it < 6; it++) {
		float3 p0 = gP[id];
		float3 p = p0;
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
				float grow = StrandFree(rest);
				float lo = L0 * lerp(0.94, 0.78, grow);
				float hi = L0 * lerp(1.0, 1.06, grow);
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
		// 頭に近い束は静止位置へ戻してから、顔と胴の外へ出す。戻したあとに押さないと、毛が顔へ戻る。
		if (flop < 0.34) p = lerp(rest, p, smoothstep(0.10, 0.34, flop));
		float3 dlt = p - p0;
		float dl2 = length(dlt);
		if (dl2 > stepLim) p = p0 + dlt * (stepLim / dl2);
		// 1 フレーム前が顔側に抜けていても、静止位置（頭の付いていた側）へ戻す。p0 を残すと顔に固定される。
		if (il > 0) p = BodyHit(SkullPush(p, keep), pick);
		// 胴の押し出しが最後だと、舞台の風で肩へ寄った毛が頭蓋の中に残る。
		p = SkullPush(p, keep);
		GroupMemoryBarrierWithGroupSync();
		gP[id] = p;
		GroupMemoryBarrierWithGroupSync();
	}
	St[id] = float4(gP[id], 0);
	// 今の拘束済み位置からの差分だけを描く。前フレームの平均を足すと、頭を抜けた位置が残る。
	St[4 * HG + id] = float4(gP[id] - rest, 0);
}

[numthreads(64, 1, 1)]
void CSHairWrite(uint id : SV_DispatchThreadID, uint gid : SV_GroupIndex)
{
	BuildHairFrame(gid);
	if (id >= Cnt.w) return;
	HairV c = Hv[Cnt.x + id];
	uint o = c.a.x * 36u;
	float3 rest = mul(float4(c.p.xyz, 1), Head).xyz;
	uint4 k4 = uint4(c.a.y & 0xFFFFu, c.a.y >> 16, c.a.z & 0xFFFFu, c.a.z >> 16);
	float3 d = float3(0, 0, 0);
	[unroll] for (int k = 0; k < 4; k++) {
		uint gi = min(k4[k], (uint)(HG - 1));
		d += St[4 * HG + gi].xyz * c.w[k];
	}
	// 頭頂の殻（揺れ 0.40 未満）は案内点の風を受けない。顎から下の毛先だけ混ぜる。
	float w = saturate((c.p.w - 0.40) / 0.22) * StrandFree(rest);
	float3 p = rest + d * w;
	float3 keep = SkullPush(rest, rest);
	p = BodyHit(SkullPush(p, keep), StrandFace(k4[0]));
	p = SkullPush(p, keep);
	float3 nrm = asfloat(Vout.Load3(o + 12));
	float nl = dot(nrm, nrm);
	if (nl > 1e-8) nrm *= rsqrt(nl);
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nrm));
}
