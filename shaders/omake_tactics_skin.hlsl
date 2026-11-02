float3 HueRotate(float3 c, float a)
{
	const float3 k = float3(0.57735, 0.57735, 0.57735);
	float ca = cos(a);
	return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1. - ca);
}

// 選んだ色を軸に、元の明暗と色相の差を残す。2色・3色の服が単色に潰れない。
float3 ClothDye(float3 src, float3 tint)
{
	float y = dot(src, float3(0.30, 0.59, 0.11));
	float y2 = saturate((y - 0.45) * 2.2 + 0.5);
	float3 lo = tint * 0.16;
	float3 hi = saturate(lerp(tint, float3(1, 1, 1), 0.85));
	float3 dyed = y2 < 0.5 ? lerp(lo, tint, y2 * 2) : lerp(tint, hi, (y2 - 0.5) * 2);
	float3 chr = src - y;
	float amp = length(chr);
	float ang = atan2(chr.g - chr.b, chr.r);
	return saturate(HueRotate(dyed, ang * saturate(amp * 3.5) * 0.30));
}

// アバターと人間のスキニング。胸の揺れ、顔、染め、髪色まで GPU で済ませ、CPU に頂点を戻さない。
cbuffer SkinBones : register(b0)
{
	row_major float4x4 Bones[64];
};
cbuffer SkinParm : register(b1)
{
	float4 Nip0;     // 髪だけ。x=髪型 y=頭ボーン z=時刻
	float4 Nip1;
	float4 Bust;     // x=胸の揺れの倍率（髪は負） y=カップ 0=A〜1=H z=色相 w=髪ボーン
	float4 Info;     // x=顔頂点数 y=ボーン数 z=顔の先頭 w=身長
	uint4 Extra;     // x=髪色 y=頂点数 z=服の色 w=服の色を使う
	float4 Jig;      // xyz=胸の遅れ（モデル空間） w=肌 0無し 1白人 2アジア 3黒人
};

struct SkinIn
{
	float4 p;    // xyz, w=布
	float4 n;
	float4 bx;
	float4 bn;
	uint4 pack;  // x=色 y=ボーン z=重み
};
struct FaceIn
{
	float4 d;
	float4 n;    // w=色を asfloat
};

StructuredBuffer<SkinIn> Vin : register(t0);
StructuredBuffer<FaceIn> Fin : register(t1);
RWByteAddressBuffer Vout : register(u0);
// 髪を服の背中より外へ出す曲線。x=高さの始点 y=間隔 z=本数 w=隙間。続く 8 本に Z を 32 個。
// SkullC は描いている体の頭蓋の中心（w=1 で有効）。SkullR は方位 16 × 仰角 8 の半径。
cbuffer HairCloth : register(b2)
{
	float4 Cloth[9];
	float4 SkullC;
	float4 SkullR[32];
};

float SkullAt(int e, int a)
{
	int i = clamp(e, 0, 7) * 16 + (a & 15);
	float4 q = SkullR[i >> 2];
	int c = i & 3;
	return c == 0 ? q.x : (c == 1 ? q.y : (c == 2 ? q.z : q.w));
}

[numthreads(64, 1, 1)]
void CSSkin(uint id : SV_DispatchThreadID)
{
	if (id >= Extra.y) return;
	SkinIn s = Vin[id];
	// 髪型の付け替えと揺れ。Bust.x が負のときだけ。体の胸には入らない。
	if (Bust.x < 0) {
		float3 p = mul(float4(s.p.xyz, 1), Bones[63]).xyz;
		float3 nrm = mul(s.n.xyz, (float3x3)Bones[63]);
		uint hj = (uint)Nip0.y;
		float3 origin = (hj < 64u) ? mul(float4(0, 0, 0, 1), Bones[hj]).xyz : float3(0, 0, 0);
		if (abs(s.n.w - Nip0.x) > 0.5) {
			p = origin;
			nrm = float3(0, 1, 0);
		} else {
			// 型の髪は頭の形と合わず、頭頂や分け目で頭蓋がカードの間から出る。
			// 頭蓋の内側に入った髪だけ、中心から外へ 6mm 浮かせた面まで出す。
			// 顔の下半分（前で中心より 3cm 下）は前髪が鼻の前へ出るので触らない。
			if (SkullC.w > 0.5) {
				float3 d = p - SkullC.xyz;
				float r = length(d);
				if (r > 1e-5 && !(d.z < -0.04 && d.y < -0.03)) {
					float el = asin(clamp(d.y / r, -1, 1));
					if (el > -0.6 && el < 1.35) {
						float fa = (atan2(d.x, d.z) + 3.14159265) * (16.0 / 6.2831853) - 0.5;
						float fe = (el + 0.6) * (8.0 / 2.1707963) - 0.5;
						int a0 = (int)floor(fa), e0 = (int)floor(fe);
						float ta = fa - a0, te = fe - e0;
						float R = lerp(lerp(SkullAt(e0, a0), SkullAt(e0, a0 + 1), ta),
							lerp(SkullAt(e0 + 1, a0), SkullAt(e0 + 1, a0 + 1), ta), te);
						if (R > 0.03 && r < R + 0.008) p = SkullC.xyz + d * ((R + 0.008) / r);
					}
				}
			}
			if (hj < 64u) {
				p = mul(float4(p, 1), Bones[hj]).xyz;
				nrm = mul(nrm, (float3x3)Bones[hj]);
			}
			// 毛先の遅れ・体への当たりは CSHair が書く。ここは頭に付いた形だけ。
			float nl = dot(nrm, nrm);
			if (nl > 1e-8) nrm *= rsqrt(nl);
		}
		uint o = id * 36u;
		Vout.Store3(o, asuint(p));
		Vout.Store3(o + 12, asuint(nrm));
		Vout.Store2(o + 24, asuint(s.bx.xy));
		Vout.Store(o + 32, 0xFFFFFFFFu);
		return;
	}
	// 胸の形はバインドへ焼いてある。bx は山の差分。胸壁と谷間は動かさず、頂ほど遅れ、下側は重力で落ちる。
	float3 p = s.p.xyz;
	float3 nrm = s.n.xyz;
	uint color = s.pack.x;
	float amt = Bust.x;
	float cup = saturate(Bust.y);
	float3 mound = s.bx.xyz;
	float mh = length(mound);
	float hang = saturate(mh * lerp(36.0, 12.0, cup));
	uint fn = (uint)Info.x;
	uint fs = (uint)Info.z;
	if (fn > 0 && id >= fs && id < fs + fn) {
		FaceIn f = Fin[id - fs];
		// w=0 は対象外。1 は位置だけ。2 は虹彩の塗り。3 はパック通り法線と色も差し替える。
		if (f.d.w > 0.5) p += f.d.xyz;
		if (f.d.w > 2.5) {
			nrm = f.n.xyz;
			color = asuint(f.n.w);
		} else if (f.d.w > 1.5) color = asuint(f.n.w);
	}
	// まぶた。Nip0.xyz 左目、Nip0.w 閉じ量、Nip1.xyz 右目、Nip1.w 半径。髪パスでは使わない。
	if (Bust.x >= 0 && Nip0.w > 0.001 && Nip1.w > 0.006) {
		float blink = saturate(Nip0.w);
		float rad = Nip1.w;
		float3 eyeP[2] = { Nip0.xyz, Nip1.xyz };
		[unroll] for (int e = 0; e < 2; e++) {
			float3 d = p - eyeP[e];
			float nx = d.x / (rad * 1.25);
			float ny = d.y / (rad * 0.95);
			float e2 = nx * nx + ny * ny;
			if (e2 < 1.0 && abs(d.z) < rad * 2.2) {
				float w = 1.0 - e2;
				w = w * w;
				p.y = lerp(p.y, eyeP[e].y, blink * w);
			}
		}
	}
	uint jp = s.pack.y;
	uint wp = s.pack.z;
	uint nj = min((uint)Info.y, 64u);
	float4x4 acc = (float4x4)0;
	[unroll] for (int c = 0; c < 4; c++) {
		uint j = (jp >> (c * 8)) & 255u;
		float w = ((wp >> (c * 8)) & 255u) / 255.0;
		if (w > 0 && j < nj) acc += Bones[j] * w;
	}
	p = mul(float4(p, 1), acc).xyz;
	if (hang > 1e-4 && amt > 1e-4) {
		float3 outN = mh > 1e-5 ? mound / mh : float3(0, -0.18, -1);
		outN = mul(outN, (float3x3)acc);
		float ol = length(outN);
		if (ol > 1e-5) outN /= ol;
		float3 lag = Jig.xyz * amt;
		float apex = hang * hang;
		float along = dot(lag, outN);
		p += outN * (along * apex);
		p += float3(lag.x, 0, lag.z) * apex * 0.42;
		p += float3(0, min(lag.y, 0) * 0.72 + max(lag.y, 0) * 0.28, 0) * hang;
	}
	{
		float hs = Info.w;
		if (hs < 0.05) hs = 1;
		p *= hs;
	}
	nrm = mul(nrm, (float3x3)acc);
	float nl = dot(nrm, nrm);
	if (nl > 1e-8) nrm *= rsqrt(nl);
	if (s.p.w > 0.5 && (Extra.w != 0 || abs(Bust.z) > 0.001)) {
		float3 col = float3(color & 255, (color >> 8) & 255, (color >> 16) & 255) / 255.0;
		if (Extra.w != 0) {
			float3 tint = float3(Extra.z & 255, (Extra.z >> 8) & 255, (Extra.z >> 16) & 255) / 255.0;
			col = ClothDye(col, tint);
		}
		// 白や灰の服は色相を回しても白のままなので、角度が付いたときだけ色を乗せる。
		float sat = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
		if (abs(Bust.z) > 0.05 && sat < 0.08) {
			float y = dot(col, float3(0.30, 0.59, 0.11));
			float3 base = saturate(HueRotate(float3(0.80, 0.22, 0.18), Bust.z));
			col = lerp(base * 0.25, lerp(base, float3(1, 1, 1), 0.75), saturate(y));
		} else if (abs(Bust.z) > 0.001) col = saturate(HueRotate(col, Bust.z));
		uint r = (uint)(col.r * 255.0 + 0.5);
		uint g = (uint)(col.g * 255.0 + 0.5);
		uint b = (uint)(col.b * 255.0 + 0.5);
		color = r | (g << 8) | (b << 16) | (color & 0xFF000000u);
	}
	if (s.p.w < 0.5 && Jig.w > 0.5) {
		int tone = (int)(Jig.w + 0.5);
		float3 col = float3(color & 255, (color >> 8) & 255, (color >> 16) & 255) / 255.0;
		float y = dot(col, float3(0.30, 0.59, 0.11));
		float mx = max(col.r, max(col.g, col.b));
		float mn = min(col.r, min(col.g, col.b));
		// 白人は明るい地肌だけ塗る。茶色の地毛まで肌色にすると、髪型なしがハゲに見える。
		float yMin = tone <= 1 ? 0.55 : 0.20;
		if (y > yMin && y < 0.90 && (mx - mn) < 0.42 && col.r + 0.03 > col.b && col.r + 0.02 > col.g * 0.92) {
			// 白人・アジア・黒人・ラテン・中東・南アジア・ポリネシア。fit_humans.py の TONE と同じ。
			float3 tgt = float3(0.93, 0.76, 0.66);
			if (tone == 2) tgt = float3(0.84, 0.64, 0.46);
			else if (tone == 3) tgt = float3(0.42, 0.27, 0.18);
			else if (tone == 4) tgt = float3(0.80, 0.59, 0.43);
			else if (tone == 5) tgt = float3(0.88, 0.68, 0.53);
			else if (tone == 6) tgt = float3(0.60, 0.41, 0.28);
			else if (tone >= 7) tgt = float3(0.68, 0.47, 0.32);
			float k = saturate(0.42 + (y - 0.35) * 1.25);
			col = saturate(lerp(tgt * 0.58, lerp(tgt, float3(1, 1, 1), 0.22), k));
			uint r = (uint)(saturate(col.r) * 255.0 + 0.5);
			uint g = (uint)(saturate(col.g) * 255.0 + 0.5);
			uint b = (uint)(saturate(col.b) * 255.0 + 0.5);
			color = r | (g << 8) | (b << 16) | (color & 0xFF000000u);
		}
	}
	int hairJ = (int)floor(Bust.w + 0.5);
	if (hairJ >= 0) {
		uint hw = 0;
		[unroll] for (int c = 0; c < 4; c++) {
			uint j = (jp >> (c * 8)) & 255u;
			uint w = (wp >> (c * 8)) & 255u;
			if (j == (uint)hairJ && w > hw) hw = w;
		}
		if (hw > 10) color = Extra.x;
	}
	uint o = id * 36u;
	Vout.Store3(o, asuint(p));
	Vout.Store3(o + 12, asuint(nrm));
	Vout.Store2(o + 24, asuint(float2(0, 0)));
	Vout.Store(o + 32, color);
}
