// おまけタクティクス HLSL
// fxc: shaders\compile_shaders.cmd → res\cso\tac_*.cso（埋め込み）。無ければ実行時にこのファイルを D3DCompile。

cbuffer Frame : register(b0)
{
	row_major float4x4 VP;
	row_major float4x4 LightVP;
	float4 Eye;      // xyz=視点 w=時間
	float4 LightDir; // xyz=光源方向(面→光) w=影有効
	float4 Fog;      // x=開始 y=終了 z=高さ霧 w=影テクセル
	float4 FogColor;
	float4 Screen;   // w,h,1/w,1/h
	float4 SkyAmb;   // rgb=空の環境光 w=強さ
	float4 GndAmb;   // rgb=地面の環境光 w=トゥーン段階の硬さ
	float4 Q;        // x=段(0軽 1標準 2高 3超綺麗 4超軽量 5最高 6限界突破) y=テッセ係数 z=変位 w=被写界
	float4 Sight;    // x=明瞭な半径 y=ステージ番号 z=人数 w=有効
	float4 Ally[16]; // xy=味方のワールドxz z=有効
	float4 CamNear;  // x=視線方向で消す深さ y=ここから残す。0 なら無効
	float4 CamFwd;   // xyz=今のカメラ前方
	float4 Wx;       // x=天候 y=時刻。地面の反射に使う
	row_major float4x4 ReflectVP;
	float4 ReflectInfo; // x=映す y=床の高さ z=反射パス(1=上面を捨てる 2=上面も残す) w=ぼかし幅
	row_major float4x4 InvVP;
};
cbuffer Obj : register(b1)
{
	row_major float4x4 World;
	float4 Tint;     // rgb=乗算 a=不透明度
	float4 Params;   // x=輪郭幅 y=モード z=発光 w=色相回転
	float4 Extra;    // x=ディゾルブ y=白フラッシュ z=リム w=揺れ
	float4 OutlineColor;
};
cbuffer Bones : register(b2)
{
	row_major float4x4 Bones[64];
};
// 残った波紋。x=位置 z=位置 y=発生時刻 w=強さ。0 ならなし。リングは時間で広がる。
cbuffer Liquid : register(b3)
{
	float4 Rip[48];
};

float GustAmt()
{
	float burst = 0;
	float s = sin(Eye.w * 0.85);
	if (s > 0.72) {
		float u = (s - 0.72) / 0.28;
		burst = u * u * (3.0 - 2.0 * u);
	}
	float s2 = sin(Eye.w * 0.37 + 2.1);
	if (s2 > 0.82) {
		float u = (s2 - 0.82) / 0.18;
		burst = max(burst, u);
	}
	return burst;
}

float3 FallLean(float3 w, float3 n, float amount)
{
	float g = GustAmt();
	float hgt = saturate((w.y + 9.0) / 30.0);
	float2 dir = float2(cos(Eye.w * 0.23), sin(Eye.w * 0.23));
	w.x += dir.x * g * hgt * amount;
	w.z += dir.y * g * hgt * amount * 0.82;
	float flutter = sin(w.y * 1.85 + Eye.w * 3.4 + w.x * 0.45);
	w += n * flutter * (0.03 + g * 0.11) * saturate(amount * 1.4);
	return w;
}

float RippleH(float2 xz, float t, out float2 dh)
{
	float h = 0;
	dh = 0;
	[loop] for (int i = 0; i < 48; i++) {
		float s = Rip[i].w;
		if (s < 0.01) continue;
		float age = t - Rip[i].y;
		if (age < 0 || age > 3.8) continue;
		float2 d = xz - float2(Rip[i].x, Rip[i].z);
		float dist = length(d);
		float rad = age * 1.75;
		float ring = exp(-abs(dist - rad) * 3.4);
		float amp = s * ring * exp(-age * 0.42);
		float ph = (dist - rad) * 10.0;
		h += sin(ph) * amp * 0.085;
		dh += d / max(dist, 0.04) * cos(ph) * amp * 0.9;
	}
	return h;
}

Texture2D Tex : register(t0);
Texture2D ShadowMap : register(t1);
Texture2D DepthMap : register(t2); // グレード専用。近いほど 0、空は 1。
Texture2DArray Ground : register(t3); // 0..7 地面アルベド(a=AO) 8..15 法線。スロット表の外。
Texture2DArray Mat : register(t4);    // 樹皮・葉・岩・木材。各アルベドと法線。
	Texture2DArray Photo : register(t5);  // 0..13 実写アルベド(a>0.5で有効) 14..27 法線
Texture2D SphereMap : register(t6);   // 反射ベクトルの球体マップ
TextureCube EnvMap : register(t7);    // ステージの環境キューブ
Texture2D LiveRef : register(t8);     // キャラ・敵・上位レイヤーを映した反射
SamplerState SL : register(s0);
SamplerComparisonState SC : register(s1);
SamplerState CL : register(s2);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct VKIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; uint4 bi : BLENDINDICES; float4 bw : BLENDWEIGHT; };
struct PSIn { float4 pos : SV_POSITION; float3 w : TEXCOORD0; float3 n : TEXCOORD1; float2 uv : TEXCOORD2; float4 c : TEXCOORD3; };

float hash21(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float2 hash22(float2 p)
{
	float2 q = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
	return frac(sin(q) * 43758.5453);
}
float vnoise(float2 p)
{
	float2 i = floor(p), f = frac(p);
	float a = hash21(i), b = hash21(i + float2(1, 0)), c = hash21(i + float2(0, 1)), d = hash21(i + float2(1, 1));
	float2 u = f * f * (3. - 2. * f);
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float fbm2(float2 p) { float s = 0, a = .5; [unroll] for (int i = 0; i < 6; i++) { s += a * vnoise(p); p *= 2.03; a *= .5; } return s; }

float2 Curl2(float2 p)
{
	float e = 0.11;
	float n0 = fbm2(p);
	float nx = fbm2(p + float2(e, 0));
	float ny = fbm2(p + float2(0, e));
	return float2(n0 - ny, nx - n0);
}

// 画面の位置から、カメラの世界視線を作る。空・奈落・ポストの模様はすべてこれに乗せる。
float3 WorldRay(float2 uv)
{
	float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.99, 1.0);
	float4 wp = mul(clip, InvVP);
	float3 p = wp.xyz / max(abs(wp.w), 1e-5);
	float3 d = p - Eye.xyz;
	float l = length(d);
	return l > 1e-5 ? d / l : float3(0, 0, 1);
}

// 視線が高さ h の水平面を貫く世界座標 xz。カメラを回しても地面や空の層に固定される。
float2 PlaneXZ(float3 rd, float h, float minSlope)
{
	float dy = rd.y;
	float s = abs(dy) < minSlope ? (dy < 0 ? -minSlope : minSlope) : dy;
	float dist = clamp((h - Eye.y) / s, -220.0, 220.0);
	return Eye.xz + rd.xz * dist;
}

float Sparkle(float2 xz, float t, float dens, float cut)
{
	float2 cell = floor(xz * dens);
	float h = hash21(cell);
	return step(cut, h) * (0.5 + 0.5 * sin(t * (6.2 + h * 9.0) + h * 40.0));
}

float CausAt(float2 xz, float t)
{
	float2 c1 = xz * 1.7 + t * float2(0.22, 0.08);
	float2 c2 = xz * 2.6 - t * float2(0.11, 0.18);
	float2 c3 = xz * 4.2 + t * float2(-0.14, 0.16);
	float a = saturate(sin(c1.x * 6.2) * sin(c1.y * 5.4) + sin(c2.x * 4.8) * sin(c2.y * 5.1));
	float b = saturate(sin(c3.x * 7.1) * sin(c3.y * 6.4));
	return pow(a, 2) + pow(b, 2) * 0.45;
}

float WaveAmp(float th)
{
	float amp = 0.16;
	if (th > 2.5 && th < 3.5) amp = 0.04;
	else if (th > 5.5 && th < 6.5) amp = 0.07;
	else if (th > 0.5 && th < 1.5) amp = 0.1;
	else if (th > 4.5 && th < 5.5) amp = 0.13;
	else if (th > 8.5 && th < 9.5) amp = 0.24;
	else if (th > 7.5 && th < 8.5) amp = 0.055;
	else if (th > 9.5 && th < 10.5) amp = 0.018;
	else if (th > 10.5 && th < 11.5) amp = 0.028;
	else if (th > 12.5) amp = 0.05;
	else if (th > 6.5) amp = 0.038;
	return amp;
}

void WaterWave(float2 xz, float t, float amp, out float h, out float2 dh)
{
	float2 d1 = normalize(float2(1.0, 0.25));
	float2 d2 = normalize(float2(-0.35, 1.0));
	float2 d3 = normalize(float2(0.2, -1.0));
	float k1 = 1.35, k2 = 2.4, k3 = 4.1;
	float f1 = k1 * dot(d1, xz) - t * 1.15;
	float f2 = k2 * dot(d2, xz) - t * 1.7;
	float f3 = k3 * dot(d3, xz) - t * 2.3;
	h = amp * (sin(f1) + 0.55 * sin(f2) + 0.28 * sin(f3));
	dh = amp * (d1 * k1 * cos(f1) + d2 * k2 * 0.55 * cos(f2) + d3 * k3 * 0.28 * cos(f3));
	dh += float2(sin(t * 0.65 + xz.x * 0.85), cos(t * 0.5 + xz.y * 0.75)) * amp * 1.4;
	float gust = GustAmt();
	h *= 1.0 + gust * 0.35;
	dh *= 1.0 + gust * 0.35;
	if (Q.x > 1.5 && !(Q.x > 3.5 && Q.x < 4.5)) {
		float2 d4 = normalize(float2(0.72, 0.69));
		float2 d5 = normalize(float2(-0.88, 0.42));
		float k4 = 6.6, k5 = 10.2;
		float f4 = k4 * dot(d4, xz) - t * 2.9;
		float f5 = k5 * dot(d5, xz) - t * 3.7;
		float a4 = amp * 0.16, a5 = amp * 0.09;
		h += a4 * sin(f4) + a5 * sin(f5);
		dh += d4 * k4 * a4 * cos(f4) + d5 * k5 * a5 * cos(f5);
		if (Q.x > 2.5) {
			float2 p = xz * 3.4 + t * 0.35;
			h += (fbm2(p) - 0.5) * amp * 0.55;
			float e = 0.08;
			dh += float2(fbm2(p + float2(e, 0)) - fbm2(p), fbm2(p + float2(0, e)) - fbm2(p)) * amp * 5.0;
		}
	}
}

// amt: 頂点αに焼いた「根元からの高さ」重み（0=揺れない）
float3 Sway(float3 w, float amt)
{
	if (amt <= 0.001) return w;
	float g = GustAmt();
	float k = amt * (1.0 + g * 2.8);
	float t = Eye.w;
	float s = sin(t * 1.7 + w.x * 0.9 + w.z * 0.7) * 0.5 + sin(t * 2.9 + w.x * 1.7) * 0.25;
	w.x += s * k * 0.07 + cos(t * 0.23) * g * amt * 0.16;
	w.z += cos(t * 1.3 + w.z * 1.1 + w.x * 0.3) * k * 0.05 + sin(t * 0.23) * g * amt * 0.12;
	return w;
}

PSIn ShadeStatic(VSIn v, row_major float4x4 WM)
{
	PSIn o;
	float3 w = mul(float4(v.p, 1), WM).xyz;
	float3 n = normalize(mul(v.n, (float3x3)WM));
	w = Sway(w, (1. - v.c.a) * 4. + Extra.w);
	// 体は noTess で OutlineColor.a=2。滝の mode が定数に残っても、人や敵の頂点は流さない。
	if (OutlineColor.a < 1.5 && Params.y > 0.5 && Params.y < 1.5) {
		if (n.y > 0.45) {
			float t = Eye.w;
			float th = FogColor.a;
			float amp = WaveAmp(th);
			float2 flow = v.c.rg * 2.0 - 1.0;
			float rush = saturate(v.c.b * 1.35);
			float spd = lerp(0.45, 2.6, rush);
			float h;
			float2 dh;
			WaterWave(w.xz - flow * t * spd, t, amp * (1.0 + rush * 0.85), h, dh);
			w.y += h;
			float2 rdh;
			w.y += RippleH(w.xz, t, rdh);
			w += float3(-dh.x, 0, -dh.y) * 0.04;
		} else {
			w = FallLean(w, n, 0.74);
			w.x += sin(w.y * 2.6 - Eye.w * 4.2) * (0.04 + GustAmt() * 0.08);
		}
	} else if (OutlineColor.a < 1.5 && Params.y > 1.5 && Params.y < 2.5) {
		if (n.y > 0.45) {
			float t = Eye.w;
			float2 flow = v.c.rg * 2.0 - 1.0;
			w.y += sin(dot(w.xz, flow) * 1.6 - t * 0.9) * (0.028 + GustAmt() * 0.02);
			float2 rdh;
			w.y += RippleH(w.xz, t, rdh) * 0.7;
		} else {
			w = FallLean(w, n, 0.16);
			w.x += sin(w.y * 0.45 - Eye.w * 0.32 + w.z * 0.2) * 0.11;
			w.z += cos(w.y * 0.38 - Eye.w * 0.28) * 0.07;
			w.y -= 0.04 * sin(Eye.w * 0.4 + w.x);
		}
	}
	w += n * Params.x;
	o.w = w; o.n = n; o.uv = v.uv; o.c = v.c;
	o.pos = mul(float4(w, 1), VP);
	return o;
}
PSIn VSS(VSIn v) { return ShadeStatic(v, World); }
PSIn VSSB(VSIn v, float4 r0 : TEXCOORD4, float4 r1 : TEXCOORD5, float4 r2 : TEXCOORD6, float4 r3 : TEXCOORD7)
{
	row_major float4x4 WM = float4x4(r0, r1, r2, r3);
	return ShadeStatic(v, WM);
}
float4 ShadowPos(float3 p, float3 n, float a)
{
	float3 w = p;
	w = Sway(w, (1. - a) * 4. + Extra.w);
	w += n * Params.x;
	return mul(float4(w, 1), VP);
}
float4 VSSSh(VSIn v) : SV_Position
{
	float3 n = normalize(mul(v.n, (float3x3)World));
	return ShadowPos(mul(float4(v.p, 1), World).xyz, n, v.c.a);
}
float4 VSSBSh(VSIn v, float4 r0 : TEXCOORD4, float4 r1 : TEXCOORD5, float4 r2 : TEXCOORD6, float4 r3 : TEXCOORD7) : SV_Position
{
	row_major float4x4 WM = float4x4(r0, r1, r2, r3);
	float3 n = normalize(mul(v.n, (float3x3)WM));
	return ShadowPos(mul(float4(v.p, 1), WM).xyz, n, v.c.a);
}
float4 VSKSh(VKIn v) : SV_Position
{
	float4x4 m = Bones[v.bi.x] * v.bw.x + Bones[v.bi.y] * v.bw.y + Bones[v.bi.z] * v.bw.z + Bones[v.bi.w] * v.bw.w;
	float3 lp = mul(float4(v.p, 1), m).xyz;
	return mul(float4(mul(float4(lp, 1), World).xyz, 1), VP);
}

PSIn VSK(VKIn v)
{
	float4x4 m = Bones[v.bi.x] * v.bw.x + Bones[v.bi.y] * v.bw.y + Bones[v.bi.z] * v.bw.z + Bones[v.bi.w] * v.bw.w;
	float3 lp = mul(float4(v.p, 1), m).xyz;
	float3 ln = mul(v.n, (float3x3)m);
	PSIn o;
	float3 w = mul(float4(lp, 1), World).xyz;
	float3 n = normalize(mul(ln, (float3x3)World));
	w += n * Params.x;
	o.w = w; o.n = n; o.uv = v.uv; o.c = float4(1, 1, 1, 1);
	o.pos = mul(float4(w, 1), VP);
	return o;
}

float ShadowAt(float3 w, float3 n)
{
	if (LightDir.w < 0.5) return 1;
	float3 l = normalize(LightDir.xyz);
	float ndl = saturate(dot(n, l));
	w += n * (0.03 + (1. - ndl) * 0.05);
	float4 sp = mul(float4(w, 1), LightVP);
	float2 uv = sp.xy / sp.w * float2(.5, -.5) + .5;
	float z = sp.z / sp.w - 0.0015;
	if (any(uv < 0) || any(uv > 1) || z >= 1) return 1;
	// 12 タップの円盤。影の縁を 3x3 よりなだらかにする。
	const float2 k[16] = {
		float2(-0.326212, -0.405810), float2(-0.840144, -0.073580), float2(-0.695914, 0.457137),
		float2(-0.203345, 0.620716), float2(0.962340, -0.194983), float2(0.473434, -0.480026),
		float2(0.519456, 0.767022), float2(0.185461, -0.893124), float2(0.507431, 0.064425),
		float2(0.896420, 0.412458), float2(-0.321940, -0.932615), float2(-0.791559, -0.597710),
		float2(0.089842, 0.323164), float2(-0.461230, 0.218612), float2(0.248110, 0.553812),
		float2(-0.118392, -0.210418)
	};
	// 標準から円盤。高い段ほど接触は鋭く、離れた遮蔽だけ広げる。超軽量は1。
	int taps = 12;
	float rad = 1.45;
	if (Q.x > 3.5 && Q.x < 4.5) { taps = 1; rad = 0; }
	else if (Q.x > 5.5) { taps = 16; rad = 0.42; }
	else if (Q.x > 4.5) { taps = 16; rad = 0.58; }
	else if (Q.x > 2.5) { taps = 16; rad = 0.78; }
	else if (Q.x > 1.5) { taps = 12; rad = 1.05; }
	float t = Fog.w * rad;
	if (taps > 1 && Q.x > 1.5) {
		int nb = (Q.x > 5.5) ? 16 : ((Q.x > 2.5) ? 8 : 4);
		float search = Fog.w * ((Q.x > 5.5) ? 14.0 : ((Q.x > 2.5) ? 8.0 : 5.0));
		float avg = 0;
		float cnt = 0;
		[loop] for (int b = 0; b < nb; b++) {
			float d = ShadowMap.SampleLevel(SL, uv + k[b] * search, 0).r;
			if (d > 0.001 && d < z) { avg += d; cnt += 1; }
		}
		if (cnt > 0.5) {
			float pen = saturate((z - avg / cnt) * ((Q.x > 5.5) ? 36.0 : 22.0));
			float wide = (Q.x > 5.5) ? 3.2 : ((Q.x > 2.5) ? 2.2 : 1.6);
			t *= lerp(0.42, wide, pen);
		}
	}
	float s = 0;
	int ntap = taps;
	[loop] for (int i = 0; i < taps; i++)
		s += ShadowMap.SampleCmpLevelZero(SC, uv + k[i] * t, z);
	if (Q.x > 4.5) {
		float t2 = t * (Q.x > 5.5 ? 2.35 : 1.75);
		int extra = (Q.x > 5.5) ? 16 : 8;
		[loop] for (int j = 0; j < extra; j++)
			s += ShadowMap.SampleCmpLevelZero(SC, uv + k[j] * t2, z);
		ntap += extra;
	}
	s /= ntap;
	return saturate(s);
}

float3 HueRotate(float3 c, float a)
{
	const float3 k = float3(0.57735, 0.57735, 0.57735);
	float ca = cos(a);
	return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1. - ca);
}

float SkinMask(float3 c)
{
	float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
	float sat = (mx - mn) / max(mx, 1e-3);
	float warm = step(c.b, c.g) * step(c.g, c.r);
	return warm * smoothstep(0.5, 0.62, mx) * (1. - smoothstep(0.38, 0.55, sat)) * smoothstep(0.12, 0.2, sat);
}

// 標準で薄く、限界突破まで段々。超軽量と軽量は無し。テッセレーションは使わない。
float DewAmt()
{
	if (Q.x > 3.5 && Q.x < 4.5) return 0;
	if (Q.x < 0.5) return 0;
	if (Q.x < 1.5) return 0.22;
	if (Q.x < 2.5) return 0.42;
	if (Q.x < 3.5) return 0.64;
	if (Q.x < 5.5) return 0.84;
	return 1;
}

float3 DewShade(float3 col, float3 albedo, float3 n, float3 v, float sh, float flesh, float hair)
{
	float dew = DewAmt();
	if (dew < 0.001) return col;
	float mx = max(albedo.r, max(albedo.g, albedo.b));
	float mn = min(albedo.r, min(albedo.g, albedo.b));
	float sat = (mx - mn) / max(mx, 1e-3);
	float gray = 1. - smoothstep(0.04, 0.16, sat);
	float hide = smoothstep(0.07, 0.22, mx) * (1. - smoothstep(0.9, 0.98, mx)) * (1. - gray * 0.82);
	float body = max(flesh, hide * 0.7);
	float coat = max(body, hair);
	if (coat < 0.02) return col;
	float3 l = normalize(LightDir.xyz);
	float3 h = normalize(l + v);
	float ndl = saturate(dot(n, l));
	float ndv = saturate(dot(n, v));
	float ndh = saturate(dot(n, h));
	float wrap = saturate(dot(n, l) * 0.5 + 0.5);
	float3 sub = lerp(albedo, albedo * float3(1.18, 0.52, 0.4), flesh);
	col += sub * wrap * dew * coat * 0.18 * sh;
	if (hair > 0.5) {
		float3 up = abs(n.y) > 0.92 ? float3(1, 0, 0) : float3(0, 1, 0);
		float3 tng = normalize(cross(up, n));
		float ah = dot(tng, h);
		float spec = pow(saturate(sqrt(saturate(1. - ah * ah)) * ndh), lerp(28., 96., dew));
		col += float3(1, 0.98, 0.94) * spec * ndl * dew * 0.62;
		col += albedo * pow(1. - ndv, 3.) * dew * 0.28;
		if (Q.x > 4.5) {
			float pore = fbm2(albedo.rg * 18.0 + n.xz * 9.0);
			col += float3(1, 0.96, 0.9) * pow(ndh, 90.) * ndl * dew * pore * 0.18;
		}
	} else {
		float spec = pow(ndh, lerp(16., 72., dew)) * ndl;
		float wet = pow(1. - ndv, lerp(3.6, 2.1, dew));
		col += float3(1, 0.97, 0.92) * spec * dew * coat * 0.24;
		col += lerp(albedo, float3(1, 0.95, 0.9), 0.3) * wet * dew * coat * 0.2;
		if (Q.x > 2.5 && flesh > 0.4) {
			float pore = fbm2(albedo.rb * 22.0 + n.xy * 11.0);
			col += lerp(albedo, float3(1, 0.55, 0.42), 0.35) * wrap * dew * flesh * (0.06 + pore * 0.08);
			col += float3(1, 0.97, 0.94) * pow(ndh, lerp(48., 110., dew)) * ndl * dew * flesh * 0.12;
		}
	}
	return col;
}

// ボロノイの最近点と、次点との差（溝の幅）。
float2 VoronoiGap(float2 p)
{
	float2 cell = floor(p);
	float2 f = frac(p);
	float d1 = 8, d2 = 8;
	[unroll] for (int j = -1; j <= 1; ++j)
	[unroll] for (int i = -1; i <= 1; ++i) {
		float2 g = float2(i, j);
		float2 o = hash22(cell + g);
		float d = length(g + o - f);
		if (d < d1) { d2 = d1; d1 = d; }
		else if (d < d2) d2 = d;
	}
	return float2(d1, d2 - d1);
}

float Aniso(float3 t, float3 h, float ndh, float power)
{
	float td = dot(t, h);
	return pow(saturate(sqrt(saturate(1 - td * td)) * ndh), power);
}

float3 Bump(float3 nGeo, float3 tn)
{
	tn = tn * 2 - 1;
	tn.y = -tn.y;
	float3 ref = abs(nGeo.y) < 0.98 ? float3(0, 1, 0) : float3(1, 0, 0);
	float3 T = normalize(cross(ref, nGeo));
	float3 B = cross(nGeo, T);
	return normalize(T * tn.x + B * tn.y + nGeo * tn.z);
}

float3 Tri(Texture2DArray tex, float slice, float3 p, float3 n, float scale)
{
	float3 w = abs(n);
	w /= max(w.x + w.y + w.z, 1e-3);
	float3 x = tex.Sample(SL, float3(p.zy * scale, slice)).rgb;
	float3 y = tex.Sample(SL, float3(p.xz * scale, slice)).rgb;
	float3 z = tex.Sample(SL, float3(p.xy * scale, slice)).rgb;
	return x * w.x + y * w.y + z * w.z;
}

float3 LitCore(float3 albedo, float3 n, float3 v, float rough, float3 f0, float sh, float skin)
{
	float3 l = normalize(LightDir.xyz);
	float nd = dot(n, l);
	float ndl = lerp(saturate(nd), saturate((nd + 0.4) / 1.4), skin);
	float3 h = normalize(l + v);
	float ndh = saturate(dot(n, h));
	float ndv = saturate(dot(n, v));
	float a = max(rough, 0.045);
	a *= a;
	float a2 = a * a;
	float den = ndh * ndh * (a2 - 1.) + 1.;
	float D = a2 / max(3.14159 * den * den, 1e-4);
	float3 F = f0 + (1. - f0) * pow(1. - saturate(dot(h, v)), 5.);
	float3 amb = lerp(GndAmb.rgb, SkyAmb.rgb, saturate(n.y * .5 + .5)) * SkyAmb.w;
	float3 sun = float3(1.0, 0.96, 0.88);
	float3 diff = albedo * (amb + sun * ndl * sh) * (1. - f0);
	float3 spec = sun * D * F * ndl * sh * 0.08;
	diff += SkyAmb.rgb * pow(1. - ndv, 5.) * 0.08;
	return diff + spec;
}

float3 LitTiny(float3 albedo, float3 n, float sh)
{
	float3 l = normalize(LightDir.xyz);
	float ndl = saturate(dot(n, l));
	float3 amb = lerp(GndAmb.rgb, SkyAmb.rgb, saturate(n.y * .5 + .5)) * SkyAmb.w * 0.85;
	return albedo * (amb + ndl * sh);
}

float3 Lit(float3 albedo, float3 n, float3 v, float rough, float3 f0, float sh, float skin)
{
	if (Q.x > 3.5 && Q.x < 4.5) return LitTiny(albedo, n, sh);
	float3 c = LitCore(albedo, n, v, rough, f0, sh, skin);
	if (Q.x < 1.5) {
		// 標準だけ、法線の折れに薄い接触陰。軽量は従来のまま。
		if (Q.x > 0.5) {
			float g = length(fwidth(n));
			c *= lerp(1., saturate(1. - g * 2.0), 0.28);
		}
		return c;
	}
	float3 l = normalize(LightDir.xyz);
	float3 h = normalize(l + v);
	float ndl = saturate(dot(n, l));
	float ndh = saturate(dot(n, h));
	float sharp = (Q.x > 2.5) ? 56. : 32.;
	float gain = (Q.x > 2.5) ? 0.1 : 0.05;
	c += float3(1, 0.97, 0.9) * pow(ndh, sharp) * ndl * sh * gain;
	if (Q.x > 2.5) {
		float wrap = saturate(dot(n, l) * 0.5 + 0.5);
		c += albedo * SkyAmb.rgb * wrap * sh * 0.06;
		c += albedo * pow(saturate(dot(n, h)), 6.) * ndl * sh * 0.045;
	}
	if (Q.x > 4.5) {
		float pwr = (Q.x > 5.5) ? 88. : 64.;
		float gn = (Q.x > 5.5) ? 0.14 : 0.09;
		c += float3(1, 0.98, 0.92) * pow(ndh, pwr) * ndl * sh * gn;
		c += albedo * pow(saturate(1. - saturate(dot(n, v))), 4.) * (Q.x > 5.5 ? 0.06 : 0.035);
	}
	return c;
}

float SightAmt(float3 w)
{
	if (Sight.w < 0.5) return 0;
	float best = 1e4;
	[unroll] for (int i = 0; i < 16; i++) {
		float2 d = abs(w.xz - Ally[i].xy);
		float man = d.x + d.y;
		best = min(best, lerp(1e4, man, step(0.5, Ally[i].z)));
	}
	float r1 = max(Sight.x, 1);
	float r0 = r1 * 0.72;
	return smoothstep(r0, r1, best);
}

float3 FogAt(float3 col, float3 w)
{
	float d = length(Eye.xyz - w);
	float fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
	fg = saturate(fg + saturate(-w.y * Fog.z));
	col = lerp(col, FogColor.rgb, fg * fg * 0.9);
	float sh = SightAmt(w);
	float3 dim = lerp(FogColor.rgb, float3(0.05, 0.06, 0.09), 0.82);
	col = lerp(col, dim, sh * 0.84);
	return col;
}

struct GOut { float4 c : SV_Target0; float z : SV_Target1; };
GOut Emit(float4 c, float3 w)
{
	GOut o;
	o.c = c;
	o.z = saturate(length(Eye.xyz - w) / max(Fog.y, 1.));
	return o;
}
GOut EmitZ(float4 c, float z) { GOut o; o.c = c; o.z = z; return o; }

// カメラ前方で、いちばん近いキャラより手前の面だけ抜く。奥はそのまま。
void CamShell(float4 spos, float3 w)
{
	float a0 = CamNear.x;
	float a1 = CamNear.y;
	if (a1 <= a0 + 0.001) return;
	float d = dot(w - Eye.xyz, CamFwd.xyz);
	// 視点が岩にめり込んだときのレンズ周りの殻だけ消す。寄りカットの体は奥にあるので残す。
	if (d < a0 && length(w - Eye.xyz) < 0.28) { clip(-1); return; }
	float keep = saturate((d - a0) / (a1 - a0));
	keep = keep * keep;
	if (keep > 0.985) return;
	float h = frac(sin(dot(spos.xy, float2(12.9898, 78.233))) * 43758.5453);
	clip(keep - h);
}

float3 DetailN(float3 n, float3 w)
{
	float s = 0;
	if (Q.x > 1.5 && Q.x < 2.5) s = 0.32;
	else if (Q.x > 2.5 && Q.x < 3.5) s = 0.85;
	else if (Q.x > 4.5 && Q.x < 5.5) s = 1.15;
	else if (Q.x > 5.5) s = 1.5;
	else return n;
	float e = (Q.x > 5.5) ? 0.08 : 0.14;
	float2 p = w.xz * (s > 1.2 ? 7.4 : (s > 0.5 ? 5.2 : 3.2));
	float h0 = fbm2(p);
	float hx = fbm2(p + float2(e, 0));
	float hz = fbm2(p + float2(0, e));
	float3 t = normalize(cross(abs(n.y) < 0.92 ? float3(0, 1, 0) : float3(1, 0, 0), n));
	float3 b = cross(n, t);
	float3 nn = n + (t * (h0 - hx) + b * (h0 - hz)) * s;
	if (Q.x > 4.5) {
		float2 q = p * (Q.x > 5.5 ? 2.7 : 1.8);
		float g0 = fbm2(q);
		float gx = fbm2(q + float2(e, 0));
		float gz = fbm2(q + float2(0, e));
		nn += (t * (g0 - gx) + b * (g0 - gz)) * (Q.x > 5.5 ? 0.55 : 0.32);
	}
	return normalize(nn);
}

GOut PSM(PSIn i)
{
	// 人の体・服・髪（輪郭色 a>1.5）。地形の細かい法線は肌と布をまだらにし、浅い角度の縁の光は紺を灰色に飛ばす。
	const float person = OutlineColor.a > 1.5 ? 1. : 0.;
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	// 人は両面で描く。襟やリボンは裏表 2 枚が同じ位置に重なり、裏の 1 枚が勝つと体側を向いた法線で灰色に沈む。
	// はっきり裏を向いた法線だけ返す（輪郭付近の 0 前後は触らない）。
	if (person > 0.5 && dot(n, v) < -0.1) n = -n;
	if (person < 0.5) n = DetailN(n, i.w);
	float4 tx = Tex.Sample(SL, i.uv);
	float3 albedo = tx.rgb * i.c.rgb;
	if (abs(Params.w) > 0.001) {
		float m = 1. - SkinMask(albedo);
		albedo = lerp(albedo, saturate(HueRotate(albedo, Params.w)), m);
	}
	albedo *= Tint.rgb;
	float alpha = Tint.a;
	float3 emis = albedo * Params.z;
	float skin = SkinMask(albedo);
	float sh = ShadowAt(i.w, n);
	float rough = lerp(0.46, 0.66, skin);
	float3 f0 = lerp(float3(0.04, 0.04, 0.04), float3(0.03, 0.02, 0.015), skin);
	float3 col = Lit(albedo, n, v, rough, f0, sh, skin * 0.85);
	float rim = pow(1. - saturate(dot(n, v)), 3.) * Extra.z;
	float rimK = 1;
	if (Q.x > 3.5 && Q.x < 4.5) rimK = 0;
	else if (Q.x < 0.5) rimK = 1;
	else if (Q.x > 5.5) rimK = 1.7;
	else if (Q.x > 4.5) rimK = 1.5;
	else if (Q.x > 2.5) rimK = 1.35;
	else if (Q.x > 1.5) rimK = 1.12;
	col += rim * rimK * float3(1, 0.95, 0.85) * (0.35 + 0.65 * sh);
	{
		float thc = FogColor.a;
		float3 tr = float3(1, 0.96, 0.82);
		if (thc < 0.5) tr = float3(1, 0.95, 0.62);
		else if (thc < 1.5) tr = float3(1, 0.62, 0.32);
		else if (thc < 2.5) tr = float3(0.62, 1, 0.5);
		else if (thc < 3.5) tr = float3(0.72, 0.9, 1);
		else if (thc < 4.5) tr = float3(1, 0.78, 0.38);
		else if (thc < 5.5) tr = float3(1, 0.42, 0.12);
		else if (thc < 6.5) tr = float3(0.5, 1, 0.42);
		else if (thc < 7.5) tr = float3(0.5, 0.72, 1);
		else if (thc < 8.5) tr = float3(0.32, 0.78, 1);
		else tr = float3(0.72, 0.92, 1);
		float frC = pow(1.0 - saturate(dot(n, v)), 2.4);
		float rimA = 0.1;
		if (Q.x > 0.5 && Q.x < 1.5) rimA = 0.14;
		else if (Q.x > 1.5 && Q.x < 2.5) rimA = 0.18;
		else if (Q.x > 2.5 && Q.x < 3.5) rimA = 0.2;
		else if (Q.x > 4.5 && Q.x < 5.5) rimA = 0.24;
		else if (Q.x > 5.5) rimA = 0.28;
		else if (Q.x > 3.5 && Q.x < 4.5) rimA = 0.08;
		col += tr * frC * rimA * (0.5 + 0.5 * sh) * (1. - person * 0.8);
	}
	col += emis;
	float hair = (Params.y > 6.5 && Params.y < 7.5) ? 1 : 0;
	col = lerp(col, DewShade(col, albedo, n, v, sh, skin, hair), 1. - person * 0.6);
	col = FogAt(col, i.w);
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		float cut = Extra.x * 1.1 - 0.05;
		clip(nz - cut);
		col += float3(1, 0.6, 0.25) * smoothstep(0.08, 0., nz - cut) * 2.;
	}
	col = lerp(col, float3(1, 1, 1), Extra.y);
	return Emit(float4(col, alpha), i.w);
}

GOut FallSheet(PSIn i, float thw, float3 albedo, float alpha)
{
	CamShell(i.pos, i.w);
	float t = Eye.w;
	float g = GustAmt();
	float fromSky = step(i.c.a, 0.82);
	float lean = g * saturate((i.w.y + 9.0) / 28.0);
	float spd = lerp(2.4, 1.55, fromSky);
	float2 flow = float2(i.w.x * 5.4 + i.w.z * 1.8 + lean * 3.2, i.w.y * -2.6 + t * spd);
	float streak = pow(saturate(sin(flow.x * lerp(1.0, 0.72, fromSky)) * 0.5 + 0.5), lerp(1.6, 1.25, fromSky));
	float band = pow(saturate(sin(flow.y * lerp(2.2, 1.6, fromSky)) * 0.5 + 0.5), 3.4);
	float braid = pow(saturate(sin(flow.x * 1.8 + flow.y * 0.55 + lean) * 0.5 + 0.5), 2.2);
	float sheet = pow(saturate(sin(i.uv.x * 12.0 + i.w.y * 0.35 - t * spd) * 0.5 + 0.5), 1.4);
	float3 body = lerp(albedo * 0.52, float3(0.74, 0.9, 1), 0.42 + streak * 0.3);
	if (thw > 4.5 && thw < 5.5) body = lerp(body, float3(1, 0.42, 0.1), 0.4);
	else if (thw > 5.5 && thw < 6.5) body = lerp(body, float3(0.4, 0.62, 0.32), 0.28);
	else if (thw > 2.5 && thw < 3.5) body = lerp(body, float3(0.86, 0.94, 1), 0.22);
	else if (thw > 7.5 && thw < 8.5) body = lerp(body, float3(0.12, 0.4, 0.5), 0.5);
	else if (thw > 9.5 && thw < 10.5) body = lerp(body, float3(0.55, 0.72, 1), 0.28);
	else if (thw > 11.5 && thw < 12.5) body = lerp(body, float3(0.72, 0.62, 0.95), 0.22);
	else if (thw > 12.5) body = lerp(body, float3(1, 0.88, 0.62), 0.18);
	body += float3(1, 1, 1) * band * 0.26;
	body += float3(0.86, 0.95, 1) * braid * 0.2;
	body += float3(0.78, 0.9, 1) * sheet * 0.1;
	float mist = fromSky ? saturate(0.22 + i.uv.y * 0.65) : saturate(0.7 - i.w.y * 0.12);
	body = lerp(body, float3(0.94, 0.97, 1), mist * lerp(0.28, 0.42, fromSky));
	float splash = fromSky * saturate((i.uv.y - 0.76) * 8.0);
	float lip = (1.0 - fromSky) * saturate((0.16 - i.uv.y) * 8.0);
	float land = (1.0 - fromSky) * saturate((i.uv.y - 0.88) * 9.0);
	body = lerp(body, float3(0.96, 0.99, 1), max(splash, max(lip, land)) * 0.5);
	float3 vn = normalize(i.n);
	float fr = pow(1.0 - saturate(dot(vn, normalize(Eye.xyz - i.w))), 2.6);
	body += float3(0.88, 0.95, 1) * fr * 0.2;
	if (Q.x > 2.5) {
		float spark = Sparkle(i.w.xz + i.w.y, t * (1.0 + g * 0.8), 14.0, 0.965);
		body += float3(1, 1, 1) * spark * (0.08 + (splash + lip + land) * 0.22);
	}
	float cut = fromSky ? saturate((0.98 - i.uv.y) * 16.0) : 1.0;
	float a = saturate(alpha * (0.58 + streak * 0.28 + braid * 0.08 + mist * 0.08 + (splash + lip + land) * 0.18) * cut);
	return Emit(float4(FogAt(body, i.w), a), i.w);
}

float StageTier()
{
	if (Q.x > 3.5 && Q.x < 4.5) return 0;
	if (Q.x < 0.5) return 1;
	if (Q.x < 1.5) return 2;
	if (Q.x < 2.5) return 3;
	if (Q.x < 3.5) return 4;
	if (Q.x < 5.5) return 5;
	return 6;
}

float ThemeWet(float theme)
{
	if (theme < 0.5) return 0.14;
	if (theme < 1.5) return 0.24;
	if (theme < 2.5) return 0.11;
	if (theme < 3.5) return 0.62;
	if (theme < 4.5) return 0.16;
	if (theme < 5.5) return 0.30;
	if (theme < 6.5) return 0.50;
	if (theme < 7.5) return 0.34;
	if (theme < 8.5) return 0.78;
	if (theme < 9.5) return 0.70;
	if (theme < 10.5) return 0.08;
	if (theme < 11.5) return 0.22;
	if (theme < 12.5) return 0.30;
	return 0.20;
}

float3 SphereLookup(float3 r)
{
	float m = 2.0 * sqrt(max(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0), 1e-5));
	return SphereMap.SampleLevel(CL, r.xy / m + 0.5, 0).rgb;
}

float RainFilm()
{
	float w = Wx.x;
	if (w > 0.5 && w < 1.5) return 1.0;
	if (w > 4.5 && w < 5.5) return 1.0;
	return 0.0;
}

float FrostFilm()
{
	float w = Wx.x;
	if (w > 1.5 && w < 2.5) return 1.0;
	if (w > 6.5 && w < 7.5) return 0.7;
	return 0.0;
}

// 上を向いた面ほど映す。雪と氷は素地、雨は濡れた面を常に、沼や磨いた石もそれぞれの強さ。
float FloorGloss(float theme, float face, float2 xz)
{
	float g = 0.12;
	if (theme < 0.5) g = 0.14;
	else if (theme < 1.5) g = 0.22;
	else if (theme < 2.5) g = 0.16;
	else if (theme < 3.5) {
		float ice = smoothstep(0.48, 0.74, fbm2(xz * 0.33 + 1.7));
		g = lerp(0.52, 0.94, ice);
	} else if (theme < 4.5) g = 0.06;
	else if (theme < 5.5) g = 0.04;
	else if (theme < 6.5) {
		float pud = smoothstep(0.42, 0.72, fbm2(xz * 0.55));
		g = lerp(0.18, 0.64, pud);
	} else if (theme < 7.5) g = 0.28;
	else if (theme < 8.5) g = 0.6;
	else if (theme < 9.5) g = 0.48;
	else if (theme < 10.5) g = 0.26;
	else if (theme < 11.5) g = 0.46;
	else if (theme < 12.5) g = 0.52;
	else g = 0.24;
	float rain = RainFilm();
	if (rain > 0.0 && !(theme > 4.5 && theme < 5.5)) {
		float pud = smoothstep(0.34, 0.7, fbm2(xz * 0.62 + 2.4));
		g = max(g, lerp(0.5, 0.84, pud));
	}
	float frost = FrostFilm();
	if (frost > 0.0 && !(theme > 4.5 && theme < 5.5)) {
		float sheet = smoothstep(0.34, 0.68, fbm2(xz * 0.48 + 4.0));
		g = max(g, lerp(0.36, 0.8, sheet) * frost);
	}
	if (theme > 3.5 && theme < 4.5 && rain < 0.5 && frost < 0.5) g = 0.05;
	return g * lerp(0.2, 1.0, saturate(face));
}

// 標準から環境キューブ。軽量と超軽量はサンプルしない。
bool ReflectMap(float3 n, float3 v, float rough, out float3 env)
{
	float tier = StageTier();
	if (tier < 1.5) { env = 0; return false; }
	n = normalize(n);
	float3 r = reflect(-v, n);
	float mip = saturate(rough) * (tier > 4.5 ? 2.6 : (tier > 2.5 ? 3.6 : 4.6));
	env = EnvMap.SampleLevel(CL, r, mip).rgb;
	if (tier < 3.5)
		env = lerp(SphereLookup(r), env, tier < 2.5 ? 0.74 : 0.88);
	return true;
}

void ApplyReflect(inout float3 col, float3 n, float3 v, float3 w, float rough, float gloss)
{
	if (gloss < 0.04) return;
	float3 env;
	if (!ReflectMap(n, v, rough, env)) return;
	if (ReflectInfo.x > 0.5) {
		float3 p = w;
		p.y = ReflectInfo.y;
		float4 rp = mul(float4(p, 1), ReflectVP);
		if (rp.w > 0.001) {
			float2 uv = rp.xy / rp.w * float2(0.5, -0.5) + 0.5;
			float2 box = saturate(min(uv, 1.0 - uv) * 8.0);
			float keep = min(box.x, box.y);
			if (keep > 0.02) {
				float3 hit;
				float b = ReflectInfo.w;
				if (b > 0.00005) {
					hit = LiveRef.SampleLevel(CL, uv, 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv + float2(b, 0), 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv - float2(b, 0), 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv + float2(0, b), 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv - float2(0, b), 0).rgb;
					hit *= 0.2;
				} else hit = LiveRef.SampleLevel(CL, uv, 0).rgb;
				env = lerp(env, hit, keep);
			}
		}
	}
	float fres = pow(1.0 - saturate(dot(normalize(n), v)), 2.1);
	float facing = 0.4;
	float cap = lerp(0.46, 0.62, saturate(gloss));
	if (ReflectInfo.x > 0.5) {
		if (Q.x > 5.5) { facing = 0.9; cap = lerp(0.74, 0.88, saturate(gloss)); }
		else if (Q.x > 4.5) { facing = 0.82; cap = lerp(0.64, 0.78, saturate(gloss)); }
		else if (Q.x > 2.5) { facing = 0.76; cap = lerp(0.56, 0.72, saturate(gloss)); }
		else { facing = 0.68; cap = lerp(0.5, 0.66, saturate(gloss)); }
	}
	float amt = min(gloss * lerp(facing, 0.94, fres), cap);
	col = lerp(col, env, amt);
}

GOut PSWater(PSIn i)
{
	CamShell(i.pos, i.w);
	// カットインは寄りなので、滝や淵の縦幕が体を横切ると胸や裾が抜けて見える。水面だけ残す。
	if (i.n.y < 0.45 && CamNear.y > CamNear.x + 0.001) discard;
	if (ReflectInfo.z > 0.5 && ReflectInfo.z < 1.5 && i.n.y > 0.45) discard;
	float3 v = normalize(Eye.xyz - i.w);
	float4 tx = Tex.Sample(SL, i.uv);
	float3 albedo = tx.rgb * i.c.rgb * Tint.rgb;
	float alpha = Tint.a;
	float t = Eye.w;
	float thw = FogColor.a;
	if (i.n.y < 0.35)
		return FallSheet(i, thw, albedo, alpha);
	float amp = WaveAmp(thw);
	float2 flow = i.c.rg * 2.0 - 1.0;
	float rush = saturate(i.c.b * 1.35);
	float spd = lerp(0.45, 2.6, rush);
	float2 xz = i.w.xz - flow * t * spd;
	float h;
	float2 dh;
	WaterWave(xz, t, amp * (1.0 + rush * 0.85), h, dh);
	float2 rdh;
	RippleH(i.w.xz, t, rdh);
	dh += rdh;
	h += dot(rdh, rdh) * 0.15;
	float3 wn = DetailN(normalize(float3(-dh.x, 1, -dh.y)), i.w);
	float ndv = saturate(dot(wn, v));
	float fr = pow(1. - ndv, 4.);
	float crest = smoothstep(amp * 0.55, amp * 1.35, h);
	float3 deep = albedo * 0.38;
	float3 shallow = albedo * 1.15;
	float3 col = lerp(deep, shallow, saturate(h / max(amp, 1e-3) * 0.5 + 0.5));
	float3 skyRef = lerp(FogColor.rgb, SkyAmb.rgb, 0.75);
	col = lerp(col, skyRef, fr * 0.62);
	float3 L = normalize(LightDir.xyz);
	float3 H = normalize(L + v);
	float sp = pow(saturate(dot(wn, H)), thw > 2.5 && thw < 3.5 ? 140. : 48.) * saturate(dot(wn, L) * 0.85 + 0.15);
	float specK = 1;
	if (Q.x > 3.5 && Q.x < 4.5) specK = 0;
	else if (Q.x > 5.5) specK = 1.85;
	else if (Q.x > 4.5) specK = 1.6;
	else if (Q.x > 2.5) specK = 1.4;
	else if (Q.x > 1.5) specK = 1.15;
	col += float3(1, 0.97, 0.88) * sp * (thw > 2.5 && thw < 3.5 ? 1.6 : 0.9) * specK;
	ApplyReflect(col, wn, v, i.w, 0.05, 0.96);
	col = lerp(col, float3(0.9, 0.95, 0.98), crest * (thw > 5.5 && thw < 6.5 ? 0.15 : 0.42));
	float caus = CausAt(i.w.xz, t);
	col += albedo * caus * 0.22;
	float wrap = saturate(dot(wn, L) * 0.35 + 0.65);
	col += albedo * float3(0.15, 0.45, 0.55) * wrap * (1.0 - ndv) * 0.18;
	if (Q.x > 2.5) {
		float cap = fbm2(i.w.xz * 9.5 + t * 0.55);
		col += float3(0.85, 0.95, 1) * pow(cap, 4) * crest * 0.16;
		col += float3(1, 1, 1) * Sparkle(i.w.xz, t, 36.0, 0.96) * crest * 0.2;
	}
	float see = 0.16;
	if (thw > 2.5 && thw < 3.5) { col = lerp(col, float3(0.82, 0.92, 1), 0.28); see = 0.12; }
	else if (thw > 0.5 && thw < 1.5) { col = lerp(col, albedo * float3(0.95, 0.72, 0.45), 0.22); see = 0.34; }
	else if (thw > 1.5 && thw < 2.5) { col *= float3(0.82, 0.96, 0.78); see = 0.26; }
	else if (thw > 3.5 && thw < 4.5) { col = lerp(col, albedo * float3(0.7, 0.9, 0.88), 0.18); see = 0.2; }
	else if (thw > 4.5 && thw < 5.5) { col = lerp(col, albedo * float3(0.55, 0.32, 0.22), 0.3); see = 0.4; }
	else if (thw > 5.5 && thw < 6.5) { col = lerp(col, albedo * float3(0.45, 0.62, 0.38), 0.35); see = 0.42; }
	else if (thw > 8.5 && thw < 9.5) {
		float boil = pow(saturate(sin(i.w.z * 2.4 - t * 3.4) * 0.5 + 0.5), 2);
		col = lerp(col, float3(0.9, 0.97, 1), boil * 0.45);
		col += float3(1, 1, 1) * crest * 0.18;
		see = 0.26;
	} else if (thw > 7.5 && thw < 8.5) {
		col = lerp(col, float3(0.04, 0.22, 0.32), 0.35);
		col += float3(0.45, 0.85, 1) * caus * 0.22;
		float bio = Sparkle(i.w.xz, t, 8.0, 0.91);
		col += float3(0.2, 0.9, 0.75) * bio * 0.16;
		see = 0.42;
	} else if (thw > 12.5) {
		col = lerp(col, float3(0.55, 0.62, 0.82), 0.18);
		col += float3(1, 0.82, 0.45) * caus * 0.08;
		see = 0.28;
	} else if (thw > 9.5 && thw < 10.5) {
		col = lerp(col, float3(0.28, 0.38, 0.62), 0.28);
		col += float3(0.7, 0.82, 1) * fr * 0.32;
		see = 0.38;
	} else if (thw > 6.5) { col = lerp(col, SkyAmb.rgb, fr * 0.22); see = 0.38; }
	if (rush > 0.28) {
		float streak = pow(saturate(sin(dot(i.w.xz, flow) * 10.5 - t * spd * 7.2) * 0.5 + 0.5), 2.4);
		float foam = pow(saturate(sin(dot(i.w.xz, float2(-flow.y, flow.x)) * 6.0 + t * 3.1) * 0.5 + 0.5), 4);
		col = lerp(col, float3(0.82, 0.93, 1), streak * rush * 0.72);
		col += float3(1, 1, 1) * foam * rush * 0.35;
		see = lerp(see, 0.16, rush * 0.55);
	}
	return Emit(float4(FogAt(col, i.w), lerp(see, 0.78, fr) * alpha), i.w);
}

float3 LavaColor(float2 xz, float t, float3 v)
{
	float2 vg = VoronoiGap(xz * 0.72 + float2(t * 0.06, t * 0.02));
	float crack = 1 - smoothstep(0.015, 0.11, vg.y);
	float pulse = 0.82 + 0.18 * sin(t * 2.2 + vg.x * 9);
	float crust = smoothstep(0.08, 0.22, vg.y);
	float3 hot = lerp(float3(1, 0.72, 0.18), float3(1, 0.22, 0.02), saturate(vg.y * 8));
	float3 col = lerp(float3(0.12, 0.035, 0.02), hot * (1.5 * pulse), crack);
	col = lerp(col, float3(0.08, 0.04, 0.03), crust * 0.55);
	float3 H = normalize(normalize(LightDir.xyz) + v);
	col += hot * pow(saturate(dot(float3(0, 1, 0), H)), 16) * crack * 0.35;
	float bubble = pow(saturate(sin(vg.x * 18.0 - t * 3.4) * 0.5 + 0.5), 8) * crack;
	col += float3(1, 0.85, 0.35) * bubble * 0.45;
	if (Q.x > 2.5) {
		float ember = Sparkle(xz, t, 12.0, 0.93);
		col += float3(1, 0.4, 0.05) * ember * 0.22;
		col += float3(1, 0.15, 0.02) * pow(1.0 - saturate(dot(float3(0, 1, 0), v)), 3) * crack * 0.18;
	}
	return col;
}

GOut LavaFall(PSIn i)
{
	float t = Eye.w;
	float g = GustAmt();
	float u = i.uv.x;
	float v = i.uv.y;
	float rope = pow(saturate(sin(u * 14.0 + i.w.y * 0.28 - t * 0.48) * 0.5 + 0.5), 1.25);
	float drip = pow(saturate(sin(u * 7.5 - t * 0.22 + i.w.y * 0.16) * 0.5 + 0.5), 3.4);
	float blob = exp(-pow(frac(v * 1.8 - t * 0.16 + u * 2.4) - 0.62, 2) * 28.0);
	float sag = saturate(v * 1.08);
	float3 hot = lerp(float3(0.42, 0.06, 0.01), float3(1.0, 0.42, 0.04), rope);
	hot = lerp(hot, float3(1.0, 0.78, 0.18), drip * 0.62);
	hot += float3(1.0, 0.32, 0.02) * blob * 0.85;
	hot += float3(1.0, 0.18, 0.01) * g * sag * 0.12;
	float edge = saturate(abs(u - 0.5) * 2.05);
	hot = lerp(hot, float3(0.16, 0.04, 0.015), edge * 0.5);
	float3 vn = normalize(i.n);
	float fr = pow(1.0 - saturate(dot(vn, normalize(Eye.xyz - i.w))), 2.2);
	hot += float3(1, 0.55, 0.12) * fr * 0.22;
	float a = saturate((0.88 + rope * 0.18) * (0.62 + sag * 0.38) * (1.0 - edge * 0.22));
	return Emit(float4(FogAt(hot, i.w), a), i.w);
}

GOut PSLava(PSIn i)
{
	CamShell(i.pos, i.w);
	if (i.n.y < 0.45 && CamNear.y > CamNear.x + 0.001) discard;
	if (ReflectInfo.z > 0.5 && ReflectInfo.z < 1.5 && i.n.y > 0.72) discard;
	float3 v = normalize(Eye.xyz - i.w);
	float t = Eye.w;
	if (i.n.y < 0.35)
		return LavaFall(i);
	float3 col = LavaColor(i.w.xz, t, v);
	if (Q.x > 4.5) {
		float2 o = (Q.x > 5.5) ? float2(0.03, 0.014) : float2(0.04, 0.018);
		col += LavaColor(i.w.xz + o, t, v);
		col += LavaColor(i.w.xz - o, t, v);
		col += LavaColor(i.w.xz + float2(-o.y, o.x), t, v);
		col += LavaColor(i.w.xz + float2(o.y, -o.x), t, v);
		if (Q.x > 5.5) {
			col += LavaColor(i.w.xz + o * 2.2, t, v);
			col += LavaColor(i.w.xz - o * 2.2, t, v);
			col *= 1. / 7.;
		} else col *= 0.2;
	} else if (Q.x < 3.5) {
		float2 o = float2(0.045, 0.02);
		col += LavaColor(i.w.xz + o, t, v);
		col += LavaColor(i.w.xz - o, t, v);
		col += LavaColor(i.w.xz + float2(-o.y, o.x), t, v);
		col *= 0.25;
	}
	float2 flow = i.c.rg * 2.0 - 1.0;
	float rush = saturate(i.c.b);
	float streak = pow(saturate(sin(dot(i.w.xz, flow) * 4.2 - t * lerp(0.4, 1.6, rush)) * 0.5 + 0.5), 2);
	col = lerp(col, float3(1, 0.42, 0.05), streak * (0.18 + rush * 0.45));
	float2 rdh;
	float rh = RippleH(i.w.xz, t, rdh);
	col += float3(1, 0.55, 0.08) * saturate(abs(rh) * 28.0);
	ApplyReflect(col, float3(0, 1, 0), v, i.w, 0.62, 0.12);
	if (Q.x > 1.5) {
		float heat = pow(0.5 + 0.5 * sin(i.w.x * 3.2 + t * 1.6) * sin(i.w.z * 2.7 - t * 1.1), 2);
		col += float3(1, 0.25, 0.02) * heat * 0.08;
	}
	return Emit(float4(col, 1), i.w);
}

// 14マップそれぞれの地表エフェクト。キャラと移動マスは別で描くので、地面の上はある程度重なってもよい。
void GroundSurfaceLayer(float theme, float3 w, float3 n, float cliff, float t, float gust, inout float3 albedo, inout float3 nLit, inout float rough, inout float3 glow)
{
	float up = saturate((n.y - 0.55) * 3.0) * (1.0 - cliff);
	if (up < 0.02) return;
	float2 xz = w.xz;
	if (theme < 0.5) {
		float2 windP = xz * float2(0.55, 0.22) - float2(t * (1.6 + gust * 1.4), t * 0.35);
		float wave = fbm2(windP) * fbm2(windP * 2.1 + 3.7);
		float2 bladeG = xz * 18.0 + Curl2(xz * 0.9 - t * 0.4) * 1.6;
		float blade = smoothstep(0.58, 0.88, fbm2(bladeG));
		float2 petG = xz * 2.8 - float2(t * 0.9, t * 0.25);
		float2 petC = floor(petG);
		float2 petF = frac(petG) - 0.5 - (hash22(petC) - 0.5) * 0.45;
		float petal = smoothstep(0.11, 0.02, length(petF)) * step(0.82, hash21(petC));
		albedo = lerp(albedo, float3(0.46, 0.72, 0.26), blade * up * 0.38);
		albedo = lerp(albedo, float3(0.78, 0.92, 0.48), smoothstep(0.28, 0.62, wave) * up * 0.42);
		albedo = lerp(albedo, float3(1.0, 0.92, 0.58), petal * up * 0.65);
		glow += float3(0.65, 0.88, 0.32) * smoothstep(0.38, 0.72, wave) * up * 0.22;
		glow += float3(1.0, 0.95, 0.62) * petal * up * 0.38;
	} else if (theme < 1.5) {
		float2 dp = xz * float2(0.48, 0.95) - float2(t * (1.2 + gust * 1.1), t * 0.28);
		float3 c = float3(fbm2(dp), fbm2(dp * 2.3 + 5.1), fbm2(xz * 6.5 - t * 2.2));
		float devil = smoothstep(0.42, 0.78, c.x * 0.65 + c.y * 0.55);
		float2 v = VoronoiGap(xz * 3.4);
		float crack = 1.0 - smoothstep(0.015, 0.055, v.y);
		albedo = lerp(albedo, float3(0.32, 0.18, 0.1), crack * up * 0.55);
		albedo = lerp(albedo, float3(0.92, 0.64, 0.38), devil * up * 0.52);
		glow += float3(1.0, 0.66, 0.32) * devil * (0.28 + c.z * 0.24) * up;
	} else if (theme < 2.5) {
		float2 rootV = VoronoiGap(xz * 1.35 + Curl2(xz * 0.4) * 0.35);
		float root = 1.0 - smoothstep(0.03, 0.11, rootV.y);
		float2 canP = xz * 0.72 + float2(sin(t * 0.35) * 0.4, cos(t * 0.28) * 0.4);
		float sunPatch = pow(saturate(fbm2(canP) * 1.35 - 0.25), 2.2);
		float2 spG = xz * 3.6 + float2(sin(t * 0.4 + xz.y), cos(t * 0.35 - xz.x)) * 0.35;
		float2 spC = floor(spG);
		float spore = smoothstep(0.14, 0.02, length(frac(spG) - 0.5 - (hash22(spC) - 0.5) * 0.4)) * step(0.76, hash21(spC));
		albedo = lerp(albedo, float3(0.18, 0.34, 0.14), root * up * 0.55);
		albedo = lerp(albedo, float3(0.58, 0.84, 0.34), sunPatch * up * 0.38);
		glow += float3(0.82, 1.0, 0.42) * sunPatch * up * 0.28;
		glow += float3(0.55, 1.0, 0.48) * spore * up * 0.52;
	} else if (theme < 3.5) {
		float2 sp = xz * float2(0.85, 0.32) - float2(t * (1.8 + gust * 1.6), t * 0.4);
		float spindrift = smoothstep(0.38, 0.76, fbm2(sp) * 0.65 + fbm2(sp * 2.7) * 0.5);
		float2 v1 = VoronoiGap(xz * 1.8);
		float2 v2 = VoronoiGap(xz * 4.2 + 2.3);
		float fracLine = (1.0 - smoothstep(0.01, 0.045, v1.y)) + (1.0 - smoothstep(0.012, 0.05, v2.y)) * 0.6;
		albedo = lerp(albedo, float3(0.52, 0.78, 0.96), saturate(fracLine) * up * 0.48);
		albedo = lerp(albedo, float3(0.96, 0.98, 1.0), spindrift * up * 0.54);
		rough = lerp(rough, 0.06, saturate(fracLine * 0.5) * up);
		glow += float3(0.78, 0.92, 1.0) * (spindrift * 0.26 + saturate(fracLine) * 0.22) * up;
	} else if (theme < 4.5) {
		float2 rp = xz * float2(6.2, 1.4) + sin(xz.yx * 1.8) * 0.65;
		float ridge = 0.5 + 0.5 * sin(rp.x - t * 0.8 + fbm2(xz * 2.0) * 3.0);
		float2 streamP = xz * float2(0.55, 2.2) - float2(t * (2.4 + gust * 1.8), t * 0.4);
		float stream = smoothstep(0.42, 0.78, fbm2(streamP) * 0.7 + fbm2(streamP * 2.4) * 0.45);
		albedo = lerp(albedo, float3(0.96, 0.78, 0.46), ridge * up * 0.34);
		albedo = lerp(albedo, float3(1.0, 0.88, 0.58), stream * up * 0.52);
		nLit = normalize(nLit + float3(cos(rp.x) * 0.22, 0, sin(rp.x) * 0.08) * up);
		glow += float3(1.0, 0.8, 0.38) * stream * up * 0.26;
	} else if (theme < 5.5) {
		float2 v1 = VoronoiGap(xz * 1.15 + Curl2(xz * 0.35) * 0.25);
		float2 v2 = VoronoiGap(xz * 2.8 - float2(t * 0.04, 0));
		float magma = (1.0 - smoothstep(0.018, 0.085, v1.y)) + (1.0 - smoothstep(0.015, 0.06, v2.y)) * 0.55;
		float ash = smoothstep(0.36, 0.74, fbm2(xz * 0.9 - float2(t * 0.45, t * 0.22)));
		float throb = 0.72 + 0.28 * sin(t * 2.6 + v1.x * 8.0);
		albedo = lerp(albedo, float3(0.12, 0.09, 0.08), ash * up * 0.58);
		albedo = lerp(albedo, float3(1.0, 0.32, 0.04), saturate(magma) * up * 0.62);
		glow += float3(1.0, 0.36, 0.04) * saturate(magma) * throb * up * 0.78;
	} else if (theme < 6.5) {
		float2 mp = xz * 0.68 + Curl2(xz * 0.4 + t * 0.08) * 0.85;
		float miasma = smoothstep(0.34, 0.72, fbm2(mp + float2(t * 0.22, -t * 0.14)));
		float2 rg = xz * 2.4;
		float2 rc = floor(rg);
		float2 rf = frac(rg) - 0.5 - (hash22(rc) - 0.5) * 0.35;
		float ph = frac(t * 0.45 + hash21(rc));
		float ring = exp(-abs(length(rf) - ph * 0.42) * 28.0) * (1.0 - ph) * step(0.58, hash21(rc + 4.1));
		albedo = lerp(albedo, float3(0.24, 0.44, 0.18), miasma * up * 0.52);
		rough = lerp(rough, 0.08, miasma * up * 0.6);
		glow += float3(0.38, 0.92, 0.28) * (miasma * 0.26 + ring * 0.85) * up;
	} else if (theme < 7.5) {
		float2 tile = frac(xz) - 0.5;
		float2 tid = floor(xz);
		float th = hash21(tid);
		float r = length(tile);
		float circ = exp(-abs(r - 0.36) * 42.0) + exp(-abs(r - 0.22) * 48.0) * 0.7;
		float spoke = exp(-abs( sin(atan2(tile.y, tile.x) * 4.0 + th * 6.0) ) * 16.0) * step(r, 0.36);
		float sigil = saturate(circ + spoke * 0.65) * step(0.62, th) * (0.55 + 0.45 * sin(t * 1.8 + th * 12.0));
		float dust = smoothstep(0.42, 0.76, fbm2(xz * 0.85 + float2(t * 0.18, -t * 0.12)));
		albedo = lerp(albedo, float3(0.52, 0.78, 1.0), sigil * up * 0.48);
		glow += float3(0.42, 0.76, 1.0) * sigil * up * 0.72;
		glow += float3(0.55, 0.7, 0.95) * dust * up * 0.18;
	} else if (theme < 8.5) {
		float c1 = CausAt(xz * 1.35 + Curl2(xz * 0.5 + t * 0.1) * 0.35, t * 1.2);
		float c2 = CausAt(xz * 2.6 - float2(t * 0.18, t * 0.11), t * 0.85);
		float net = saturate(c1 * 0.7 + c2 * 0.55);
		float2 bg = xz * 3.8 + float2(sin(t * 0.6 + xz.y), cos(t * 0.5 - xz.x)) * 0.25;
		float2 bc = floor(bg);
		float bio = smoothstep(0.12, 0.015, length(frac(bg) - 0.5 - (hash22(bc) - 0.5) * 0.4)) * step(0.68, hash21(bc));
		albedo = lerp(albedo, float3(0.28, 0.78, 0.95), net * up * 0.45);
		glow += float3(0.24, 0.82, 1.0) * net * up * 0.42;
		glow += float3(0.15, 1.0, 0.78) * bio * (0.6 + 0.4 * sin(t * 3.0 + hash21(bc) * 20.0)) * up * 0.65;
	} else if (theme < 9.5) {
		float2 sp = xz * float2(1.4, 0.65) - float2(0, t * 1.8);
		float mist = smoothstep(0.32, 0.72, fbm2(sp) * 0.65 + fbm2(sp * 2.4 + 1.9) * 0.5);
		float2 rg = xz * 3.2;
		float2 rc = floor(rg);
		float ph = frac(t * 0.85 + hash21(rc));
		float ring = exp(-abs(length(frac(rg) - 0.5) - ph * 0.44) * 32.0) * (1.0 - ph);
		albedo = lerp(albedo, float3(0.86, 0.95, 1.0), mist * up * 0.5);
		rough = lerp(rough, 0.08, up * 0.55);
		glow += float3(0.78, 0.92, 1.0) * (mist * 0.28 + ring * 0.48) * up;
	} else if (theme < 10.5) {
		float2 np = xz * 0.52 + Curl2(xz * 0.28 - t * 0.05) * 0.6;
		float neb = smoothstep(0.34, 0.74, fbm2(np + t * 0.08));
		float2 hex = abs(frac(xz * 2.0) - 0.5);
		float grid = smoothstep(0.45, 0.495, max(hex.x, hex.y));
		float pulse = 0.5 + 0.5 * sin(t * 2.2 - length(xz - 32.0) * 0.45);
		float3 nCol = lerp(float3(0.52, 0.18, 0.95), float3(0.15, 0.65, 1.0), fbm2(np * 1.9 + 3.0));
		albedo = lerp(albedo, nCol, neb * up * 0.46);
		glow += nCol * neb * up * 0.38;
		glow += float3(0.45, 0.82, 1.0) * grid * (0.35 + pulse * 0.65) * up * 0.55;
	} else if (theme < 11.5) {
		float2 g1 = abs(frac(xz) - 0.5);
		float2 g4 = abs(frac(xz * 4.0) - 0.5);
		float seam = smoothstep(0.465, 0.495, max(g1.x, g1.y));
		float trace = smoothstep(0.45, 0.49, min(g4.x, g4.y)) * step(0.55, hash21(floor(xz * 4.0)));
		float wave = pow(0.5 + 0.5 * sin(xz.x * 0.55 + xz.y * 0.35 - t * 2.4), 6.0);
		float steam = smoothstep(0.46, 0.78, fbm2(xz * 0.9 + float2(t * 0.35, -t * 0.18)));
		albedo = lerp(albedo, float3(0.18, 0.24, 0.3), seam * up * 0.55);
		glow += float3(0.2, 0.88, 1.0) * (trace * (0.4 + wave * 0.8) + seam * wave * 0.45) * up * 0.65;
		glow += float3(0.65, 0.8, 0.92) * steam * up * 0.2;
	} else if (theme < 12.5) {
		float2 tile = frac(xz * 0.5) - 0.5;
		float2 tid = floor(xz * 0.5);
		float r = length(tile);
		float tracery = exp(-abs(r - 0.38) * 36.0) + exp(-abs(abs(tile.x) - abs(tile.y)) * 42.0) * step(r, 0.38);
		float glass = smoothstep(0.38, 0.74, fbm2(xz * 0.65 + float2(0.08, 0.05) * t));
		float3 gCol = lerp(float3(0.75, 0.32, 1.0), float3(1.0, 0.72, 0.28), fbm2(xz * 0.9 + 4.2));
		albedo = lerp(albedo, gCol, glass * up * 0.36);
		glow += gCol * glass * up * 0.32;
		glow += float3(0.88, 0.68, 1.0) * saturate(tracery) * (0.55 + 0.45 * sin(t * 1.6 + hash21(tid) * 8.0)) * up * 0.52;
	} else {
		float2 g = xz * 1.25 + float2(sin(t * 0.25 + xz.y * 0.2), cos(t * 0.2 - xz.x * 0.2)) * 0.25;
		float2 lc = floor(g);
		float2 lf = frac(g) - 0.5 - (hash22(lc) - 0.5) * 0.35;
		float lh = hash21(lc);
		float pool = exp(-dot(lf, lf) * 14.0) * step(0.52, lh);
		float2 mistP = xz * 0.62 + Curl2(xz * 0.35 + t * 0.08) * 0.55;
		float mist = smoothstep(0.36, 0.74, fbm2(mistP - float2(t * 0.16, t * 0.08)));
		albedo = lerp(albedo, float3(1.0, 0.76, 0.36), pool * up * 0.45);
		albedo = lerp(albedo, float3(0.72, 0.78, 0.96), mist * up * 0.38);
		glow += float3(1.0, 0.74, 0.32) * pool * (0.75 + 0.25 * sin(t * 2.4 + lh * 15.0)) * up * 0.72;
		glow += float3(0.95, 0.82, 0.55) * mist * up * 0.22;
	}
}

GOut GroundPS(PSIn i, float theme)
{
	CamShell(i.pos, i.w);
	if (ReflectInfo.z > 0.5 && ReflectInfo.z < 1.5 && i.n.y > 0.72) discard;
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	float cliff = n.y > 0.72 ? 0 : 1;
	float2 uv = i.w.xz * (cliff > 0.5 ? 0.26 : 0.4);
	// 水底だけ頂点αを 254 にしてある。水面と同じ波でテクスチャをずらし、底がゆれて見える。
	float bedCaus = 0;
	float2 bedDh = 0;
	if (cliff < 0.5) {
		float g = GustAmt();
		if (i.c.a < 0.999) {
			float h;
			float2 dh;
			WaterWave(i.w.xz, Eye.w, WaveAmp((float)theme), h, dh);
			float2 rdh;
			RippleH(i.w.xz, Eye.w, rdh);
			uv += (dh + rdh) * 0.16;
			bedCaus = CausAt(i.w.xz, Eye.w);
			bedDh = dh + rdh;
		}
		if (theme > 7.5 && theme < 8.5) {
			float hG;
			float2 dhG;
			WaterWave(i.w.xz - float2(0.31, -0.19) * Eye.w * (0.32 + g * 0.7), Eye.w, 0.028 + g * 0.03, hG, dhG);
			uv += dhG * (0.055 + g * 0.05);
			bedDh += dhG * (0.45 + g * 0.4);
			bedCaus = max(bedCaus, CausAt(i.w.xz - float2(0.18, -0.1) * Eye.w * 0.4, Eye.w) * (0.35 + g * 0.25));
		}
	}
	// 配列は 0..7 がアルベド、8..15 が法線。海底・滝・追加4テーマは実写を床にする。
	bool dedicated = theme > 7.5;
	float texTheme = theme;
	float4 alb = dedicated ? float4(0.42, 0.42, 0.44, 1) : Ground.Sample(SL, float3(uv, texTheme));
	float4 nr = dedicated ? float4(0.5, 0.5, 1, 1) : Ground.Sample(SL, float3(uv, texTheme + 8));
	float3 albedo = alb.rgb * lerp(0.78, 1, alb.a) * i.c.rgb;
	if (cliff > 0.5) albedo *= 0.8;
	float3 nLit = DetailN(Bump(n, nr.rgb), i.w);
	float rough = 0.7;
	float3 glow = 0;
	float3 f0 = float3(0.035, 0.035, 0.035);
	float burst = GustAmt();
	float3 Hsun = normalize(normalize(LightDir.xyz) + v);
	float ndhG = saturate(dot(nLit, Hsun));
	float2 curl = Curl2(i.w.xz * 0.35 + Eye.w * 0.08);
	if (theme < 0.5) {
		rough = 0.8;
		float sway = sin(i.w.x * 0.48 + i.w.z * 0.27 - Eye.w * 0.52);
		float roll = 0.5 + 0.5 * sin(i.w.x * 0.16 - Eye.w * 0.28 + i.w.z * 0.11);
		albedo *= 0.96 + sway * 0.05 * (1 - cliff);
		albedo = lerp(albedo, albedo * float3(0.9, 0.96, 0.72), roll * burst * 0.1 * (1 - cliff));
		nLit = normalize(nLit + float3(sway * 0.1 + curl.x * 0.05, 0.03, curl.y * 0.05) * (1 - cliff) * (1 + burst * 0.35));
		float pore = fbm2(i.w.xz * 4.2);
		albedo *= 0.95 + pore * 0.08;
		glow += float3(0.92, 0.88, 0.55) * Sparkle(i.w.xz, Eye.w * 0.22, 5.5, 0.992) * (1 - cliff) * 0.06;
	} else if (theme < 1.5) {
		rough = 0.58;
		if (cliff > 0.5) {
			float groove = 1 - smoothstep(0.012, 0.05, abs(frac(i.w.y * 1.15) - 0.5));
			albedo *= 1 - groove * 0.5;
			nLit = normalize(nLit + float3(0, cos(i.w.y * 7.2) * 0.4, 0));
			float strata = 0.5 + 0.5 * sin(i.w.y * 4.6);
			albedo = lerp(albedo, albedo * float3(1.15, 0.82, 0.58), strata * 0.22);
		}
		float dust = fbm2(i.w.xz * 0.12 + float2(Eye.w * 0.03, Eye.w * 0.01) + curl * 0.2);
		albedo = lerp(albedo, albedo * float3(1.22, 0.86, 0.62), dust * 0.24);
		glow += float3(0.85, 0.62, 0.32) * pow(dust, 2) * 0.1;
		glow += float3(1, 0.78, 0.42) * Sparkle(i.w.xz, Eye.w, 16.0, 0.97) * 0.18;
		nLit = normalize(nLit + float3(curl.x, 0, curl.y) * 0.08);
		float heat = pow(0.5 + 0.5 * sin(i.w.x * 1.1 + Eye.w * 1.4), 3);
		glow += float3(1, 0.55, 0.22) * heat * (1 - cliff) * 0.05;
	} else if (theme < 2.5) {
		rough = 0.38;
		f0 = float3(0.05, 0.055, 0.045);
		float dapple = pow(saturate(sin(i.w.x * 0.85 + Eye.w * 0.35) * sin(i.w.z * 0.7 - Eye.w * 0.22)), 2);
		float dapple2 = pow(saturate(sin(i.w.x * 1.55 - Eye.w * 0.18) * sin(i.w.z * 1.25 + Eye.w * 0.14)), 3);
		glow += float3(1.0, 0.92, 0.45) * dapple * 0.18;
		glow += float3(0.7, 1.0, 0.4) * dapple2 * 0.08;
		albedo *= 0.9 + dapple * 0.14;
		float moss = 1 - smoothstep(0.02, 0.08, VoronoiGap(i.w.xz * 0.7).y);
		albedo = lerp(albedo, albedo * float3(0.55, 0.78, 0.42), moss * 0.22 * (1 - cliff));
		glow += float3(1, 0.95, 0.55) * Sparkle(i.w.xz, Eye.w, 20.0, 0.97) * 0.12 * dapple;
		nLit = normalize(nLit + float3(curl.x, 0.04, curl.y) * 0.07 * (1 - cliff));
	} else if (theme < 3.5) {
		rough = 0.2;
		f0 = float3(0.065, 0.07, 0.078);
		float spark = Sparkle(i.w.xz + i.w.y, Eye.w, 14.0, 0.955) * pow(ndhG, 28);
		glow += float3(0.9, 0.96, 1) * spark * 0.42;
		float drift = pow(0.5 + 0.5 * sin(i.w.x * 0.7 + Eye.w * 0.6 + burst), 4);
		glow += float3(0.8, 0.9, 1) * drift * 0.1;
		albedo = lerp(albedo, float3(0.92, 0.95, 1), 0.1);
		float sastrugi = 0.5 + 0.5 * sin(i.w.x * 2.4 + i.w.z * 0.35);
		nLit = normalize(nLit + float3(sastrugi * 0.22, 0.05, 0.08) * (1 - cliff));
		float ice = pow(1.0 - saturate(dot(nLit, v)), 2.6);
		glow += float3(0.7, 0.88, 1) * ice * 0.12;
		float sheet = smoothstep(0.48, 0.74, fbm2(i.w.xz * 0.33 + 1.7)) * saturate(n.y);
		albedo = lerp(albedo, float3(0.66, 0.82, 0.94), sheet * 0.62);
		rough = lerp(rough, 0.04, sheet);
		float2 vgIce = VoronoiGap(i.w.xz * 0.9);
		float crack = 1.0 - smoothstep(0.008, 0.04, vgIce.y);
		albedo = lerp(albedo, albedo * float3(0.5, 0.68, 0.82), crack * sheet * 0.7);
		nLit = normalize(nLit + float3(sin(i.w.x * 6.2), 0.04, cos(i.w.z * 5.4)) * sheet * 0.16);
		albedo *= 0.94 + fbm2(i.w.xz * 6.2) * 0.1;
	} else if (theme < 4.5) {
		rough = 0.74;
		nLit = normalize(nLit + float3(cos(i.w.x * 1.7 + Eye.w * (0.6 + burst * 1.4)) * 0.18, 0.04, sin(Eye.w * 1.1 + i.w.z) * 0.12) + float3(curl.x, 0, curl.y) * 0.1);
		float sand = pow(0.5 + 0.5 * sin(i.w.x * 0.8 + i.w.z * 0.25 - Eye.w * 1.8), 3);
		float ripple = pow(0.5 + 0.5 * sin(i.w.x * 4.8 + i.w.z * 0.9), 4);
		albedo = lerp(albedo, albedo * float3(1.25, 1.05, 0.7), sand * 0.24);
		albedo = lerp(albedo, albedo * float3(1.12, 0.95, 0.62), ripple * 0.12);
		glow += float3(1, 0.82, 0.4) * sand * 0.07;
		glow += float3(1, 0.9, 0.55) * Sparkle(i.w.xz, Eye.w, 18.0, 0.96) * 0.16;
		float heat = pow(0.5 + 0.5 * sin(i.w.z * 1.6 + Eye.w * 1.8), 2);
		glow += float3(1, 0.55, 0.18) * heat * (1 - cliff) * 0.05;
	} else if (theme < 5.5) {
		rough = 0.55;
		float2 vg = VoronoiGap(i.w.xz * 0.55 + float2(Eye.w * 0.03, Eye.w * 0.012));
		float crack = 1 - smoothstep(0.02, 0.09, vg.y);
		albedo *= 1 - crack * 0.35;
		glow += float3(1.0, 0.28, 0.02) * crack * (cliff > 0.5 ? 0.32 : 0.6);
		float pulse = 0.75 + 0.25 * sin(Eye.w * 2.2 + vg.x * 6);
		glow *= pulse;
		glow += float3(1, 0.45, 0.05) * Sparkle(i.w.xz, Eye.w, 10.0, 0.94) * 0.2;
		float ash = fbm2(i.w.xz * 0.28 + Eye.w * 0.04);
		albedo = lerp(albedo, albedo * float3(0.55, 0.48, 0.42), ash * 0.18);
		nLit = normalize(nLit + float3(0, crack * 0.25, 0));
	} else if (theme < 6.5) {
		rough = 0.26;
		f0 = float3(0.05, 0.06, 0.04);
		float caus = CausAt(i.w.xz * 0.55, Eye.w);
		glow += float3(0.35, 1.0, 0.45) * caus * 0.24;
		albedo = lerp(albedo, albedo * float3(0.7, 0.85, 0.55), 0.12);
		float film = pow(0.5 + 0.5 * sin(i.w.x * 3.2 + i.w.z * 2.1 + Eye.w * 0.5), 3);
		albedo = lerp(albedo, albedo * float3(0.45, 0.7, 0.38), film * 0.2 * (1 - cliff));
		glow += float3(0.55, 1, 0.4) * Sparkle(i.w.xz, Eye.w, 9.0, 0.93) * 0.14;
		nLit = normalize(nLit + float3(curl.x, 0.06, curl.y) * 0.09);
	} else if (theme < 7.5) {
		rough = 0.46;
		f0 = float3(0.05, 0.05, 0.055);
		float2 vg = VoronoiGap(i.w.xz * 0.42);
		float moss = 1 - smoothstep(0.015, 0.07, vg.y);
		albedo = lerp(albedo, albedo * float3(0.45, 0.72, 0.4), moss * 0.34);
		float rune = pow(0.5 + 0.5 * sin(i.w.x * 1.3 + Eye.w * 0.7), 8) * pow(0.5 + 0.5 * sin(i.w.z * 1.1 - Eye.w * 0.4), 8);
		glow += float3(0.45, 0.7, 1) * rune * (1 - cliff) * 0.42;
		glow += float3(0.7, 0.85, 1) * step(0.97, hash21(floor(i.w.xz * 3))) * (0.4 + 0.3 * sin(Eye.w * 2));
		float dust = fbm2(i.w.xz * 0.22 + Eye.w * 0.02);
		albedo = lerp(albedo, albedo * float3(0.78, 0.76, 0.7), dust * 0.14);
		glow += float3(0.85, 0.8, 0.7) * Sparkle(i.w.xz, Eye.w, 14.0, 0.97) * 0.1;
	} else if (theme < 8.5) {
		rough = 0.28;
		f0 = float3(0.05, 0.075, 0.1);
		albedo *= lerp(float3(0.62, 0.82, 0.74), float3(0.45, 0.7, 0.88), 0.4);
		float rip = 0.5 + 0.5 * sin(i.w.x * 2.6 + i.w.z * 0.5 + Eye.w * 0.4);
		albedo *= 0.8 + rip * 0.22;
		float caus = CausAt(i.w.xz, Eye.w);
		glow += float3(0.35, 0.85, 1) * caus * 0.18 * saturate(n.y);
		float ray = pow(saturate(0.5 + 0.5 * sin(i.w.x * 0.35 + i.w.z * 0.12 + Eye.w * 0.35)), 5);
		ray *= pow(saturate(0.5 + 0.5 * sin(i.w.z * 0.22 - Eye.w * 0.18)), 3);
		glow += float3(0.45, 0.85, 1) * ray * saturate(n.y) * 0.1;
		float bio = Sparkle(i.w.xz, Eye.w, 7.0, 0.9);
		glow += float3(0.15, 0.95, 0.7) * bio * 0.16 * saturate(n.y);
		nLit = normalize(nLit + float3(curl.x - bedDh.x, 0.04, curl.y - bedDh.y) * 0.14);
		float sand = fbm2(i.w.xz * 1.8);
		albedo = lerp(albedo, albedo * float3(0.7, 0.62, 0.4), sand * 0.12);
		albedo *= 0.97 + saturate(dot(bedDh, bedDh) * 8.0) * 0.06;
	} else if (theme < 9.5) {
		rough = 0.38;
		f0 = float3(0.07, 0.075, 0.08);
		albedo *= float3(0.78, 0.86, 0.9);
		float spray = pow(saturate(sin(i.w.x * 1.4 + Eye.w * 2.2) * sin(i.w.z * 0.9 - Eye.w * 1.4)), 2);
		glow += float3(0.85, 0.95, 1) * spray * 0.3;
		float sheet = pow(0.5 + 0.5 * sin(i.w.z * 0.8 - Eye.w * 2.6), 3);
		glow += float3(0.7, 0.88, 1) * sheet * 0.14;
		albedo = lerp(albedo, float3(0.75, 0.86, 0.92), spray * 0.18);
		glow += float3(1, 1, 1) * Sparkle(i.w.xz, Eye.w, 24.0, 0.95) * 0.22;
		float wetR = 0.5 + 0.5 * sin(i.w.x * 5.0 + Eye.w * 1.1);
		nLit = normalize(nLit + float3(wetR * 0.12, 0.06, spray * 0.1));
		albedo *= 0.9 + fbm2(i.w.xz * 5.5) * 0.14;
	} else if (theme < 10.5) {
		rough = 0.28;
		f0 = float3(0.12, 0.13, 0.18);
		albedo *= float3(0.78, 0.84, 1.05);
		float panel = step(0.88, frac(sin(floor(i.w.x * 2.0) * 12.3 + floor(i.w.z * 2.0) * 7.1)));
		glow += float3(0.55, 0.78, 1) * panel * 0.22;
		glow += float3(0.82, 0.92, 1) * pow(ndhG, 22) * 0.16;
		float score = 1 - smoothstep(0.01, 0.05, abs(frac(i.w.x * 3.4 + i.w.z * 0.2) - 0.5));
		albedo *= 1 - score * 0.14;
		float star = Sparkle(i.w.xz, Eye.w, 5.0, 0.9);
		glow += float3(0.85, 0.92, 1) * star * 0.28;
		float frost = fbm2(i.w.xz * 4.2);
		albedo = lerp(albedo, albedo * float3(0.9, 0.94, 1.08), frost * 0.16);
		float neb = 0.5 + 0.5 * sin(i.w.x * 0.35 + Eye.w * 0.12) * sin(i.w.z * 0.28);
		glow += lerp(float3(0.45, 0.22, 0.85), float3(0.22, 0.55, 1), neb) * neb * (1 - cliff) * 0.12;
	} else if (theme < 11.5) {
		rough = 0.24;
		f0 = float3(0.13, 0.14, 0.15);
		albedo *= float3(0.72, 0.78, 0.84);
		float seam = pow(0.5 + 0.5 * sin(i.w.x * 8.0), 16) + pow(0.5 + 0.5 * sin(i.w.z * 8.0), 16);
		albedo *= 1 - seam * 0.28;
		float lamp = step(0.96, hash21(floor(i.w.xz * 1.5)));
		glow += float3(1, 0.45, 0.15) * lamp * 0.5 + float3(0.2, 0.7, 1) * lamp * 0.24;
		float scan = 0.5 + 0.5 * sin(i.w.z * 22.0 + Eye.w * 4.0);
		glow += float3(0.25, 0.85, 1) * scan * 0.05 * (1 - cliff);
		float frost = pow(0.5 + 0.5 * sin(i.w.x * 12.0), 10);
		albedo = lerp(albedo, float3(0.85, 0.9, 0.95), frost * 0.12);
		nLit = normalize(nLit + float3(seam * 0.15, 0, 0));
	} else if (theme < 12.5) {
		rough = 0.18;
		f0 = float3(0.11, 0.1, 0.13);
		albedo = lerp(albedo, albedo * float3(0.95, 0.9, 1.05), 0.42);
		float vein = pow(0.5 + 0.5 * sin(i.w.x * 1.7 + i.w.z * 0.6), 6);
		albedo = lerp(albedo, float3(0.82, 0.74, 0.92), vein * 0.22);
		glow += float3(0.62, 0.38, 0.92) * (1 - cliff) * 0.18;
		float moon = pow(ndhG, 28);
		glow += float3(0.88, 0.8, 1) * moon * 0.18;
		float polish = fbm2(i.w.xz * 8.0);
		albedo *= 0.96 + polish * 0.1;
		glow += float3(1, 0.86, 0.52) * Sparkle(i.w.xz, Eye.w, 9.0, 0.96) * 0.2;
		float win = step(0.975, hash21(floor(i.w.xz * 1.8))) * saturate(n.y);
		glow += float3(1, 0.78, 0.38) * win * 0.55;
		float rune = pow(0.5 + 0.5 * sin(i.w.x * 2.4 + Eye.w * 0.55), 10) * pow(0.5 + 0.5 * sin(i.w.z * 1.8 - Eye.w * 0.4), 10);
		glow += float3(0.7, 0.45, 1) * rune * (1 - cliff) * 0.28;
		float torch = exp(-length(frac(i.w.xz * 0.42) - 0.5) * 10.0);
		glow += float3(1, 0.55, 0.18) * torch * 0.16 * saturate(n.y);
	} else {
		rough = 0.36;
		f0 = float3(0.07, 0.08, 0.1);
		albedo = lerp(albedo, float3(0.82, 0.86, 0.95), 0.35);
		float mist = 0.5 + 0.5 * sin(i.w.x * 0.4 + Eye.w * 0.3);
		glow += float3(1, 0.9, 0.7) * mist * saturate(n.y) * 0.14;
		float lantern = step(0.97, hash21(floor(i.w.xz * 2.0)));
		glow += float3(1, 0.78, 0.4) * lantern * 0.62;
		float pool = exp(-length(frac(i.w.xz * 0.55) - 0.5) * 8.0);
		glow += float3(1, 0.72, 0.35) * pool * 0.12 * saturate(n.y);
		glow += float3(1, 0.9, 0.55) * Sparkle(i.w.xz, Eye.w, 13.0, 0.96) * 0.16;
		nLit = normalize(nLit + float3(curl.x, 0.03, curl.y) * 0.06);
	}
	float tier = StageTier();
	GroundSurfaceLayer(theme, i.w, n, cliff, Eye.w, burst, albedo, nLit, rough, glow);
	if (tier > 0.5 || dedicated) {
		float rep = dedicated ? (cliff > 0.5 ? 0.36 : 0.5) : (cliff > 0.5 ? 0.22 : 0.15);
		float2 puv = i.w.xz * rep;
		if (!dedicated && cliff < 0.5 && tier > 3.5 && Photo.SampleLevel(SL, float3(puv, theme), 0).a > 0.5) {
			int steps = (tier > 5.5) ? 14 : ((tier > 4.5) ? 8 : 4);
			float scale = (tier > 5.5) ? 0.05 : ((tier > 4.5) ? 0.036 : 0.022);
			float3 viewTo = normalize(i.w - Eye.xyz);
			float2 dir = viewTo.xz / max(abs(viewTo.y), 0.45) * scale;
			float2 st = dir / steps;
			float h = 1;
			[loop] for (int s = 0; s < steps; s++) {
				float lum = dot(Photo.SampleLevel(SL, float3(puv, theme + 14), 0).rgb, float3(0.30, 0.59, 0.11));
				if (lum >= h) break;
				h -= 1.0 / steps;
				puv -= st;
			}
		}
		float4 ph = Photo.Sample(SL, float3(puv, theme));
		if (ph.a > 0.5) {
			float3 alb = ph.rgb;
			bool tri = dedicated || (tier > 1.5 && cliff > 0.5);
			if (tri && cliff > 0.5) {
				float3 w = abs(n);
				w /= max(w.x + w.y + w.z, 1e-3);
				float3 sx = Photo.Sample(SL, float3(i.w.zy * rep, theme)).rgb;
				float3 sz = Photo.Sample(SL, float3(i.w.xy * rep, theme)).rgb;
				alb = sx * w.x + alb * w.y + sz * w.z;
			}
			if (dedicated) {
				albedo = alb;
				if (theme < 8.5) albedo = lerp(albedo, albedo * float3(0.45, 0.78, 0.95), 0.5);
				else if (theme < 9.5) albedo *= float3(0.9, 0.96, 0.92);
				else if (theme < 10.5) albedo = saturate(pow(max(albedo, 0.001), 0.55) * 1.32);
				else if (theme < 11.5) albedo = saturate(albedo * 1.75 + 0.07);
				else if (theme < 12.5) albedo = saturate(albedo * 1.38 + 0.07);
			} else {
				if (tier > 3.5) {
					float3 far = Photo.Sample(SL, float3(puv * 0.23, theme)).rgb;
					alb = lerp(alb, far, 0.26);
				}
				if (tier > 4.5) {
					float3 grit = Photo.Sample(SL, float3(i.w.xz * 0.46, theme)).rgb;
					alb = lerp(alb, grit, 0.2);
				}
				float photoMix = tier < 1.5 ? 0.58 : (tier < 2.5 ? 0.74 : (tier < 3.5 ? 0.86 : 0.93));
				albedo = lerp(albedo, alb, photoMix);
				if (theme > 5.5 && theme < 6.5) albedo *= lerp(float3(1, 1, 1), float3(0.7, 0.88, 0.62), 0.24);
				else if (theme > 4.5 && theme < 5.5) albedo *= lerp(float3(1, 1, 1), float3(0.9, 0.62, 0.5), 0.34);
			}
			if (tier > 2.5 || dedicated) {
				float3 pn = Photo.Sample(SL, float3(puv, theme + 14)).rgb;
				if (tier > 4.5) {
					float3 pn2 = Photo.Sample(SL, float3(i.w.xz * 0.46, theme + 14)).rgb;
					pn = lerp(pn, pn2, 0.35);
				}
				nLit = DetailN(Bump(n, pn), i.w);
			}
		}
	}
	{
		float skin = DewAmt();
		float wet = ThemeWet(theme);
		float pore = fbm2(i.w.xz * 11.0 + i.w.y * 3.0);
		albedo *= lerp(1.0, 0.9 + pore * 0.16, skin * 0.55);
		float ndvS = saturate(dot(nLit, v));
		glow += SkyAmb.rgb * pow(1.0 - ndvS, 3.2) * wet * (0.08 + skin * 0.12) * (1.0 - cliff * 0.4);
		rough = saturate(rough - wet * 0.16 * skin);
		f0 = lerp(f0, f0 + 0.028, wet * skin);
		if (skin > 0.4) {
			float wrap = saturate(dot(nLit, normalize(LightDir.xyz)) * 0.45 + 0.55);
			glow += albedo * wrap * skin * 0.045;
		}
	}
	if (StageTier() > 1.5) {
		float face = saturate(n.y);
		float rain = RainFilm();
		if (rain > 0.0 && !(theme > 4.5 && theme < 5.5)) {
			float pud = smoothstep(0.34, 0.7, fbm2(i.w.xz * 0.62 + 2.4));
			float wet = face * lerp(0.55, 1.0, pud);
			albedo *= lerp(1.0, 0.7, wet * 0.7);
			rough = lerp(rough, 0.07, wet * 0.9);
			f0 = lerp(f0, float3(0.045, 0.05, 0.055), wet);
			float h;
			float2 dh;
			WaterWave(i.w.xz, Eye.w, 0.04, h, dh);
			nLit = normalize(nLit + float3(-dh.x, 0.0, -dh.y) * wet * 2.0);
		}
		float frost = FrostFilm();
		if (frost > 0.0 && !(theme > 4.5 && theme < 5.5)) {
			float sheetW = smoothstep(0.34, 0.68, fbm2(i.w.xz * 0.48 + 4.0));
			float iceW = face * sheetW * frost;
			albedo = lerp(albedo, float3(0.8, 0.9, 1.0), iceW * 0.4);
			rough = lerp(rough, 0.05, iceW);
			nLit = normalize(nLit + float3(sin(i.w.x * 7.0), 0.03, cos(i.w.z * 6.2)) * iceW * 0.12);
		}
	}
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	col += glow * (0.7 + StageTier() * 0.22);
	if (bedCaus > 0) {
		float3 causCol = float3(0.65, 0.85, 1);
		if (theme > 4.5 && theme < 5.5) causCol = float3(1, 0.55, 0.25);
		else if (theme > 5.5 && theme < 6.5) causCol = float3(0.45, 0.85, 0.4);
		else if (theme > 2.5 && theme < 3.5) causCol = float3(0.85, 0.93, 1);
		else if (theme > 0.5 && theme < 1.5) causCol = float3(0.9, 0.7, 0.4);
		else if (theme > 7.5 && theme < 8.5) causCol = float3(0.35, 0.75, 1);
		else if (theme > 8.5 && theme < 9.5) causCol = float3(0.85, 0.95, 1);
		else if (theme > 12.5) causCol = float3(1, 0.82, 0.45);
		col += causCol * bedCaus * 0.62;
	}
	if (cliff < 0.5) {
		float2 g = abs(frac(i.w.xz) - 0.5);
		float edge = smoothstep(0.475, 0.5, max(g.x, g.y));
		col *= 1. - edge * (theme > 2.5 && theme < 4.5 ? 0.08 : 0.16);
	}
	if (Q.x > 4.5) {
		float3 L = normalize(LightDir.xyz);
		float3 H = normalize(L + v);
		float sp = pow(saturate(dot(nLit, H)), Q.x > 5.5 ? 52. : 30.);
		col += float3(1, 0.98, 0.92) * sp * saturate(dot(nLit, L)) * (Q.x > 5.5 ? 0.09 : 0.045);
	}
	{
		float microF = (Q.x > 5.5) ? 28.0 : ((Q.x > 2.5) ? 18.0 : 11.0);
		float micro = fbm2(i.w.xz * microF + i.w.y * 2.0);
		col *= 0.95 + micro * ((Q.x > 4.5) ? 0.09 : 0.05);
	}
	if (StageTier() > 1.5) {
		float gloss = FloorGloss(theme, nLit.y, i.w.xz);
		ApplyReflect(col, nLit, v, i.w, rough, gloss);
		if (cliff < 0.5 && gloss > 0.18) {
			float2 ge = abs(frac(i.w.xz) - 0.5);
			float edge2 = smoothstep(0.468, 0.5, max(ge.x, ge.y));
			col *= 1.0 - edge2 * 0.3;
		}
	}
	return Emit(float4(FogAt(col, i.w), 1), i.w);
}

GOut PSG0(PSIn i) { return GroundPS(i, 0); }
GOut PSG1(PSIn i) { return GroundPS(i, 1); }
GOut PSG2(PSIn i) { return GroundPS(i, 2); }
GOut PSG3(PSIn i) { return GroundPS(i, 3); }
GOut PSG4(PSIn i) { return GroundPS(i, 4); }
GOut PSG5(PSIn i) { return GroundPS(i, 5); }
GOut PSG6(PSIn i) { return GroundPS(i, 6); }
GOut PSG7(PSIn i) { return GroundPS(i, 7); }
GOut PSG8(PSIn i) { return GroundPS(i, 8); }
GOut PSG9(PSIn i) { return GroundPS(i, 9); }
GOut PSGX(PSIn i) { return GroundPS(i, FogColor.a); }

GOut PSProp(PSIn i)
{
	CamShell(i.pos, i.w);
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	float mat = Params.z;
	float leaf = saturate((1 - i.c.a) * 4);
	float3 albedo;
	float3 nLit = n;
	float rough = 0.55;
	float3 f0 = float3(0.04, 0.04, 0.04);
	if (mat < 0.5) {
		albedo = lerp(Tri(Mat, 0, i.w, n, 0.85), Tri(Mat, 2, i.w, n, 0.7), leaf);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.8, leaf > 0.5 ? 3 : 1)).rgb);
		rough = lerp(0.72, 0.5, leaf);
	} else if (mat < 1.5) {
		albedo = Tri(Mat, 4, i.w, n, 0.32);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.32, 5)).rgb);
		rough = 0.66;
		if (FogColor.a > 2.5 && FogColor.a < 3.5)
			albedo = lerp(albedo, float3(0.86, 0.9, 0.95), saturate(n.y) * 0.6);
		else if (FogColor.a > 4.5 && FogColor.a < 5.5)
			albedo *= float3(0.72, 0.62, 0.58);
	} else if (mat < 2.5) {
		albedo = Tri(Mat, 6, i.w, n, 0.65);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.65, 7)).rgb);
		rough = 0.42;
		f0 = float3(0.05, 0.045, 0.03);
	} else if (mat < 3.5) {
		albedo = Tex.Sample(SL, i.uv).rgb * i.c.rgb;
		rough = 0.5;
	} else {
		albedo = Tri(Ground, 7, i.w, n, 0.45);
		nLit = Bump(n, Ground.Sample(SL, float3(i.w.xz * 0.45, 15)).rgb);
		rough = 0.46;
		f0 = float3(0.05, 0.05, 0.055);
	}
	albedo *= Tint.rgb;
	nLit = DetailN(nLit, i.w);
	if (StageTier() > 1.5 && RainFilm() > 0.5 && n.y > 0.45 && !(FogColor.a > 4.5 && FogColor.a < 5.5)) {
		albedo *= lerp(1.0, 0.78, saturate(n.y));
		rough = lerp(rough, 0.12, saturate(n.y) * (leaf > 0.5 ? 0.35 : 0.8));
	}
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	if (StageTier() > 1.5) {
		float gloss = FloorGloss(FogColor.a, nLit.y, i.w.xz);
		gloss *= leaf > 0.5 ? 0.35 : 0.82;
		ApplyReflect(col, nLit, v, i.w, rough, gloss);
	}
	if (Q.x > 2.5) {
		float pore = fbm2(i.w.xz * 7.5 + i.w.y * 2.2);
		col *= 0.94 + pore * 0.1;
		col += SkyAmb.rgb * pow(1.0 - saturate(dot(nLit, v)), 3.0) * ThemeWet(FogColor.a) * 0.08;
	}
	return Emit(float4(FogAt(col, i.w), Tint.a), i.w);
}

GOut PSO(PSIn i)
{
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		clip(nz - (Extra.x * 1.1 - 0.05));
	}
	float oa = OutlineColor.a > 1.5 ? 1.0 : OutlineColor.a;
	return Emit(float4(lerp(OutlineColor.rgb, float3(1, 1, 1), Extra.y), oa * Tint.a), i.w);
}

GOut PSOV(PSIn i)
{
	// 移動・攻撃範囲。セル座標と世界座標の二重模様。タイル自体は透けて読める。
	float2 g = abs(i.uv - 0.5) * 2.;
	float edge = max(g.x, g.y);
	float t = Eye.w;
	float pulse = 0.92 + 0.08 * sin(t * 4.2 + (i.w.x + i.w.z) * 0.8);
	float2 cell = abs(frac(i.w.xz * 12.0) - 0.5);
	float hair = smoothstep(0.41, 0.49, max(cell.x, cell.y));
	float2 fine = abs(frac(i.w.xz * 28.0) - 0.5);
	float grain = smoothstep(0.45, 0.5, max(fine.x, fine.y)) * 0.32;
	float2 d2 = abs(frac(float2(i.w.x + i.w.z, i.w.x - i.w.z) * 5.0) - 0.5);
	float fil = smoothstep(0.45, 0.495, min(d2.x, d2.y)) * 0.25;
	float corner = smoothstep(0.68, 0.74, g.x) * smoothstep(0.68, 0.74, g.y);
	float border = smoothstep(0.89, 0.96, edge) * (1.0 - smoothstep(0.985, 1.0, edge));
	float innerLine = smoothstep(0.76, 0.81, edge) * (1.0 - smoothstep(0.82, 0.86, edge)) * 0.45;
	float inner = 0.24 + 0.16 * smoothstep(0.18, 0.9, edge);
	float a = (inner + hair * 0.2 + grain + fil + innerLine + border * 0.88 + corner * border * 0.25) * i.c.a * Tint.a * pulse;
	float3 c = i.c.rgb * Tint.rgb * (0.84 + hair * 0.26 + fil * 0.35 + (border + innerLine) * 0.58);
	if (Params.y > 0.5) {
		float cnr = step(0.7, g.x) * step(0.7, g.y);
		float frame = smoothstep(0.89, 0.95, edge) * (1.0 - smoothstep(0.985, 1.0, edge));
		a = (frame * (0.72 + cnr * 0.28) + hair * 0.1 + fil * 0.12) * i.c.a * Tint.a;
		c = i.c.rgb * Tint.rgb * (1.16 + cnr * 0.28 + hair * 0.22);
	}
	return Emit(float4(c, saturate(a)), i.w);
}

struct HIn { float2 p : POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct HOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; };
HOut VSH(HIn v)
{
	HOut o;
	o.pos = float4(v.p.x * Screen.z * 2. - 1., 1. - v.p.y * Screen.w * 2., 0, 1);
	o.uv = v.uv; o.c = v.c;
	return o;
}
float4 PSH(HOut i) : SV_Target
{
	// テクスチャは乗算済みα（D2D）。ブレンドは ONE / INV_SRC_ALPHA
	float4 t = Tex.Sample(SL, i.uv);
	return float4(t.rgb * i.c.rgb * i.c.a, t.a * i.c.a);
}

struct PIn { float3 p : POSITION; float3 uvs : TEXCOORD0; float4 c : COLOR0; };
struct POut { float4 pos : SV_POSITION; float3 uvs : TEXCOORD0; float4 c : TEXCOORD1; };
struct PinP { float3 p : POSITION; float4 vel : TEXCOORD0; float4 sz : TEXCOORD1; float4 c : COLOR0; };
PinP VSPT(PinP v) { return v; }
[maxvertexcount(6)]
void GSP(point PinP i[1], inout TriangleStream<POut> s)
{
	float3 right = float3(World._11, World._12, World._13);
	float3 up = float3(World._21, World._22, World._23);
	float3 fwd = float3(World._31, World._32, World._33);
	float3 pos = i[0].p;
	float3 vel = i[0].vel.xyz;
	float stretch = i[0].vel.w;
	float sz = max(i[0].sz.x, 0);
	float shape = i[0].sz.y;
	float3 ax = right * sz;
	float3 ay = up * sz;
	if (stretch > 0) {
		float sp = length(vel);
		if (sp > 0.001) {
			float3 vd = vel / sp;
			float3 side = cross(vd, fwd);
			float sl = length(side);
			if (sl > 0.001) side /= sl;
			ay = vd * (sz + sp * stretch);
			ax = side * (sz * 0.5);
		}
	}
	float3 corn[4] = { pos - ax - ay, pos - ax + ay, pos + ax + ay, pos + ax - ay };
	float2 uv[4] = { float2(0, 1), float2(0, 0), float2(1, 0), float2(1, 1) };
	[unroll] for (int t = 0; t < 3; t++) {
		POut o;
		o.pos = mul(float4(corn[t], 1), VP);
		o.uvs = float3(uv[t], shape);
		o.c = i[0].c;
		s.Append(o);
	}
	s.RestartStrip();
	int tri[3] = { 0, 2, 3 };
	[unroll] for (int u = 0; u < 3; u++) {
		POut o;
		o.pos = mul(float4(corn[tri[u]], 1), VP);
		o.uvs = float3(uv[tri[u]], shape);
		o.c = i[0].c;
		s.Append(o);
	}
}

POut VSP(PIn v)
{
	POut o;
	o.pos = mul(float4(v.p, 1), VP);
	o.uvs = v.uvs; o.c = v.c;
	return o;
}
GOut PSP(POut i)
{
	float2 q = i.uvs.xy * 2. - 1.;
	float r = length(q);
	float shape = i.uvs.z;
	float a;
	if (shape < 0.5) a = saturate(1. - r) * saturate(1. - r);
	else if (shape < 1.5) a = saturate(1. - abs(r - 0.7) * 5.);
	else if (shape < 2.5) {
		float s = saturate(1. - abs(q.x * q.y) * 18.) * saturate(1. - r);
		a = max(s, saturate(1. - r * 2.2));
	}
	else if (shape < 3.5) a = saturate(1. - abs(q.y) * 1.2) * saturate(1. - abs(q.x));
	else if (shape < 4.5) {
		float arc = saturate(1. - abs(r - 0.8) * 7.) * step(0, q.y) * saturate(q.y * 3.);
		a = arc;
	} else if (shape < 5.5) {
		float tongue = saturate(1. - abs(q.x) * (1.4 + max(q.y, 0) * 2.2));
		a = tongue * saturate(0.85 - q.y) * step(-1.0, q.y);
	} else if (shape < 6.5) {
		float jag = q.x - sin(q.y * 22.0) * 0.28;
		a = saturate(1. - abs(jag) * 7.) * saturate(1. - abs(q.y));
	} else {
		a = saturate(1. - (abs(q.x) + abs(q.y)) * 1.2);
	}
	float hot = saturate(1. - r);
	return EmitZ(float4(i.c.rgb * (a * i.c.a) * (1. + hot * hot * 0.7), a * i.c.a), 0.22);
}

struct QOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
QOut VSQ(uint id : SV_VertexID)
{
	QOut o;
	float2 p = float2((id << 1) & 2, id & 2);
	o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
	o.uv = p;
	return o;
}
// 空の天井と、盤面の下に広がる奈落。すべてカメラの視線 rd の世界座標で描く。
void AbyssAndSky(inout float3 c, float3 rd, float ths, float t, float gust)
{
	float yaw = atan2(rd.x, rd.z);
	float up = saturate(rd.y * 2.2 + 0.15);
	float down = saturate(-rd.y * 2.4 + 0.2);
	float rim = exp(-abs(rd.y) * 5.5);
	float2 skyXZ = rd.xz / max(abs(rd.y) + 0.16, 0.16) * 14.0;
	float2 pit1 = PlaneXZ(rd, -8.0, 0.09) * 0.14;
	float2 pit2 = PlaneXZ(rd, -22.0, 0.09) * 0.07;
	float2 pit3 = PlaneXZ(rd, -44.0, 0.09) * 0.035;
	if (ths < 0.5) {
		float c1 = fbm2(skyXZ * 0.22 + float2(-t * (0.18 + gust * 0.22), t * 0.04));
		float c2 = fbm2(skyXZ * 0.52 + float2(-t * 0.32, -t * 0.08) + 3.1);
		float cloud = smoothstep(0.36, 0.76, c1 * 0.6 + c2 * 0.5);
		c = lerp(c, float3(0.98, 0.99, 1.0), cloud * up * 0.55);
		float sea1 = fbm2(pit1 + float2(-t * 0.25, t * 0.08));
		float sea2 = fbm2(pit2 * 1.8 + float2(-t * 0.14, -t * 0.05));
		float billow = smoothstep(0.3, 0.74, sea1 * 0.6 + sea2 * 0.5);
		float3 abyss = lerp(float3(0.32, 0.52, 0.74), float3(0.88, 0.95, 1.0), billow);
		float shaft = pow(0.5 + 0.5 * sin(yaw * 9.0 - t * 0.35 + sea1 * 3.0), 8.0);
		abyss += float3(1.0, 0.96, 0.78) * shaft * 0.28;
		c = lerp(c, abyss, down * 0.88);
		c += float3(0.85, 0.94, 1.0) * rim * 0.22;
	} else if (ths < 1.5) {
		float mesa = fbm2(float2(yaw * 3.5, 1.2)) * 0.18;
		float ridge = smoothstep(mesa + 0.06, mesa - 0.02, abs(rd.y + 0.04));
		c = lerp(c, float3(0.36, 0.16, 0.08), ridge * rim * 0.65);
		float d1 = fbm2(pit1 * 1.4 - float2(t * 0.38, t * 0.12));
		float d2 = fbm2(pit2 * 2.6 + float2(t * 0.22, -t * 0.18));
		float strata = 0.5 + 0.5 * sin(rd.y * 48.0 + d1 * 5.0);
		float3 gorge = lerp(float3(0.18, 0.07, 0.03), float3(0.78, 0.38, 0.16), saturate(d1 * 0.7 + strata * 0.35));
		gorge += float3(1.0, 0.58, 0.22) * pow(saturate(d2), 2.4) * 0.42;
		c = lerp(c, gorge, down * 0.9);
		c += float3(1.0, 0.56, 0.24) * rim * 0.28;
	} else if (ths < 2.5) {
		float can = fbm2(skyXZ * 0.38 + float2(t * 0.04, 0));
		float ray = pow(0.5 + 0.5 * sin(yaw * 14.0 + can * 2.5 - t * 0.18), 10.0);
		c += float3(0.72, 1.0, 0.45) * ray * (up + rim * 0.6) * 0.32;
		float2 rv1 = VoronoiGap(pit1 * 2.2);
		float2 rv2 = VoronoiGap(pit2 * 3.4 + 1.7);
		float roots = (1.0 - smoothstep(0.02, 0.09, rv1.y)) + (1.0 - smoothstep(0.025, 0.1, rv2.y)) * 0.6;
		float mist = fbm2(pit1 * 1.2 + float2(t * 0.12, -t * 0.09));
		float2 sp = floor(pit1 * 9.0 + float2(0, t * 0.8));
		float fly = step(0.95, hash21(sp)) * (0.5 + 0.5 * sin(t * 4.0 + hash21(sp) * 20.0));
		float3 deep = lerp(float3(0.03, 0.1, 0.05), float3(0.16, 0.42, 0.22), mist);
		deep = lerp(deep, float3(0.22, 0.16, 0.09), saturate(roots) * 0.65);
		deep += float3(0.62, 1.0, 0.38) * (ray * 0.22 + fly * 0.65);
		c = lerp(c, deep, down * 0.92);
	} else if (ths < 3.5) {
		float2 ap = float2(yaw * 2.2 + t * 0.06, rd.y * 3.4);
		float a1 = fbm2(ap + float2(0, fbm2(ap * 1.8) * 0.8));
		float a2 = fbm2(ap * 2.1 - float2(t * 0.04, 1.7));
		float aur = pow(saturate(a1 * 0.62 + a2 * 0.52), 1.4) * up;
		c += lerp(float3(0.12, 0.92, 0.58), float3(0.38, 0.45, 1.0), a2) * aur * 0.55;
		float2 cv1 = VoronoiGap(pit1 * 2.6);
		float2 cv2 = VoronoiGap(pit2 * 4.0);
		float rift = (1.0 - smoothstep(0.015, 0.07, cv1.y)) + (1.0 - smoothstep(0.02, 0.08, cv2.y)) * 0.65;
		float3 crev = lerp(float3(0.03, 0.09, 0.18), float3(0.22, 0.58, 0.86), fbm2(pit1 * 1.6));
		crev += float3(0.55, 0.88, 1.0) * saturate(rift) * 0.62;
		c = lerp(c, crev, down * 0.92);
	} else if (ths < 4.5) {
		float mir = fbm2(float2(yaw * 4.0 - t * 0.22, rd.y * 12.0));
		c += float3(1.0, 0.78, 0.38) * (rim * 0.32 + mir * up * 0.16);
		float w1 = fbm2(pit1 * 1.8 - float2(t * 0.55, t * 0.18));
		float w2 = fbm2(pit2 * 3.2 - float2(t * 0.85, -t * 0.24));
		float swirl = sin(atan2(pit1.y, pit1.x) * 3.0 + length(pit1) * 1.4 - t * 1.6) * 0.5 + 0.5;
		float3 sea = lerp(float3(0.26, 0.16, 0.06), float3(0.88, 0.66, 0.32), saturate(w1 * 0.65 + w2 * 0.45));
		sea += float3(1.0, 0.82, 0.42) * pow(swirl * w1, 2.0) * 0.38;
		c = lerp(c, sea, down * 0.9);
	} else if (ths < 5.5) {
		float ash = fbm2(skyXZ * 0.35 - float2(t * 0.12, t * 0.28));
		c = lerp(c, float3(0.14, 0.05, 0.03), ash * up * 0.55);
		c += float3(1.0, 0.3, 0.04) * pow(ash, 2.4) * (up * 0.35 + rim * 0.45);
		float2 mv1 = VoronoiGap(pit1 * 1.9 + float2(t * 0.05, -t * 0.03));
		float2 mv2 = VoronoiGap(pit2 * 3.4 - float2(t * 0.08, t * 0.04));
		float vein1 = 1.0 - smoothstep(0.015, 0.09, mv1.y);
		float vein2 = 1.0 - smoothstep(0.018, 0.08, mv2.y);
		float3 cald = float3(0.07, 0.02, 0.01);
		cald += float3(1.0, 0.28, 0.02) * vein1 * 1.15;
		cald += float3(1.0, 0.65, 0.12) * vein2 * 0.65;
		cald += float3(1.0, 0.22, 0.02) * pow(fbm2(pit3 * 2.0 - t * 0.2), 2.2) * 0.55;
		c = lerp(c, cald, down * 0.95);
	} else if (ths < 6.5) {
		float gas = fbm2(skyXZ * 0.32 + Curl2(skyXZ * 0.18 + t * 0.05) * 0.6);
		c += float3(0.38, 0.88, 0.28) * pow(gas, 2.0) * (up * 0.28 + rim * 0.35);
		float m1 = fbm2(pit1 * 1.5 + Curl2(pit1 * 0.8 + t * 0.1));
		float m2 = fbm2(pit2 * 2.8 - float2(t * 0.16, t * 0.12));
		float2 wc = floor(pit1 * 4.5);
		float2 wf = frac(pit1 * 4.5) - 0.5 - (hash22(wc) - 0.5) * 0.4;
		float orb = exp(-dot(wf, wf) * 18.0) * step(0.72, hash21(wc)) * (0.5 + 0.5 * sin(t * 2.4 + hash21(wc) * 16.0));
		float3 bog = lerp(float3(0.03, 0.08, 0.03), float3(0.22, 0.48, 0.16), saturate(m1 * 0.65 + m2 * 0.45));
		bog += float3(0.48, 1.0, 0.35) * orb * 0.95;
		c = lerp(c, bog, down * 0.92);
	} else if (ths < 7.5) {
		float r = length(pit1);
		float a = atan2(pit1.y, pit1.x);
		float ring1 = exp(-abs(frac(r * 0.35 - t * 0.05) - 0.5) * 22.0);
		float ring2 = exp(-abs(frac(r * 0.7 + t * 0.04) - 0.5) * 26.0);
		float glyph = step(0.65, sin(a * 12.0 + floor(r * 0.7) * 2.3 + t * 0.4));
		float3 sanc = lerp(float3(0.04, 0.05, 0.12), float3(0.14, 0.22, 0.44), fbm2(pit2 * 1.6));
		sanc += float3(0.45, 0.78, 1.0) * (ring1 * 0.55 + ring2 * glyph * 0.75);
		c = lerp(c, sanc, down * 0.92);
		float skyR = exp(-abs(rd.y - 0.32) * 18.0) * (0.5 + 0.5 * sin(yaw * 8.0 - t * 0.3));
		c += float3(0.45, 0.72, 1.0) * skyR * up * 0.38;
	} else if (ths < 8.5) {
		float2 cp = skyXZ * 0.42 + float2(t * 0.14, -t * 0.09);
		float ca = CausAt(cp * 0.6, t * 0.8);
		float shaft = pow(0.5 + 0.5 * sin(yaw * 10.0 + fbm2(cp) * 3.0 - t * 0.25), 8.0);
		c += float3(0.2, 0.75, 1.0) * (ca * 0.38 + shaft * 0.32) * (up + rim * 0.5);
		float2 bc1 = floor(pit1 * 6.0);
		float2 bc2 = floor(pit2 * 11.0);
		float b1 = exp(-length(frac(pit1 * 6.0) - 0.5) * 9.0) * step(0.74, hash21(bc1));
		float b2 = exp(-length(frac(pit2 * 11.0) - 0.5) * 10.0) * step(0.78, hash21(bc2));
		float3 trench = lerp(float3(0.01, 0.05, 0.12), float3(0.04, 0.24, 0.42), fbm2(pit1 * 1.2 + t * 0.08));
		trench += float3(0.15, 0.95, 0.82) * (b1 * 0.75 + b2 * 0.55);
		trench += float3(0.22, 0.68, 1.0) * CausAt(pit1 * 0.8, t) * 0.28;
		c = lerp(c, trench, down * 0.94);
	} else if (ths < 9.5) {
		float fall = fbm2(float2(yaw * 18.0, rd.y * 6.0 - t * 1.8));
		float sheet = pow(0.5 + 0.5 * sin(yaw * 22.0 + fall * 2.0), 4.0);
		float mist = fbm2(pit1 * 1.4 - float2(0, t * 0.65));
		float bowA = exp(-pow((rd.y + 0.18) * 5.5, 2.0));
		float3 bow = lerp(float3(1, 0.38, 0.38), lerp(float3(0.38, 1, 0.52), float3(0.38, 0.58, 1), saturate(rd.y * 3.0 + 0.8)), saturate(rd.y * 2.0 + 0.5));
		float3 plunge = lerp(float3(0.14, 0.32, 0.48), float3(0.82, 0.94, 1.0), saturate(mist * 0.65 + sheet * 0.45));
		plunge += bow * bowA * 0.38;
		c = lerp(c, plunge, down * 0.9);
		c += bow * exp(-pow((rd.y - 0.18) * 6.0, 2.0)) * up * 0.28;
	} else if (ths < 10.5) {
		float3 sd = normalize(rd);
		float2 u1 = float2(atan2(sd.x, sd.z) * 1.8, sd.y * 2.4);
		float n1 = fbm2(u1 + t * 0.02);
		float n2 = fbm2(u1 * 2.3 - t * 0.015 + 3.4);
		float neb = smoothstep(0.28, 0.78, n1 * 0.58 + n2 * 0.52);
		float3 cosmos = float3(0.02, 0.02, 0.06);
		cosmos += lerp(float3(0.55, 0.12, 0.88), float3(0.12, 0.58, 1.0), n2) * neb * 0.72;
		float2 sg1 = float2(yaw * 95.0, rd.y * 140.0);
		float2 sg2 = float2(yaw * 210.0, rd.y * 310.0);
		float s1 = step(0.988, hash21(floor(sg1))) * saturate(1.0 - length(frac(sg1) - 0.5) * 5.2);
		float s2 = step(0.992, hash21(floor(sg2))) * saturate(1.0 - length(frac(sg2) - 0.5) * 6.0);
		cosmos += float3(0.92, 0.96, 1.0) * (s1 + s2 * 0.8);
		float vortex = exp(-abs(length(pit1) * 0.18 - 1.2) * 2.5) * fbm2(pit1 * 0.8 - t * 0.2);
		cosmos += float3(0.42, 0.78, 1.0) * vortex * down * 0.55;
		c = lerp(c, cosmos, saturate(up * 0.75 + down * 0.96));
	} else if (ths < 11.5) {
		float2 g1 = abs(frac(pit1 * 2.0) - 0.5);
		float2 g2 = abs(frac(pit2 * 4.0) - 0.5);
		float strut1 = smoothstep(0.43, 0.49, max(g1.x, g1.y));
		float strut2 = smoothstep(0.45, 0.495, max(g2.x, g2.y));
		float pulse1 = step(0.75, hash21(floor(pit1 * 2.0))) * (0.4 + 0.6 * sin(t * 3.0 + hash21(floor(pit1 * 2.0)) * 12.0));
		float scan = pow(0.5 + 0.5 * sin(pit1.x * 1.5 + pit1.y * 1.5 - t * 2.2), 8.0);
		float3 core = float3(0.03, 0.05, 0.09);
		core += float3(0.18, 0.72, 0.95) * (strut1 * 0.55 + strut2 * 0.45 + scan * 0.35);
		core += float3(1.0, 0.48, 0.14) * pulse1 * strut1 * 0.85;
		c = lerp(c, core, down * 0.94);
		float2 sg = abs(frac(skyXZ * 0.45) - 0.5);
		c += float3(0.22, 0.82, 1.0) * smoothstep(0.46, 0.495, max(sg.x, sg.y)) * up * 0.32;
	} else if (ths < 12.5) {
		float2 a1 = abs(frac(pit1 * 1.5) - 0.5);
		float arch = smoothstep(0.42, 0.49, max(a1.x, a1.y));
		float rose = exp(-abs(length(frac(pit1 * 0.5) - 0.5) - 0.32) * 28.0);
		float candle = step(0.84, hash21(floor(pit1 * 3.0))) * exp(-length(frac(pit1 * 3.0) - 0.5) * 9.0);
		float3 nave = lerp(float3(0.06, 0.03, 0.11), float3(0.24, 0.12, 0.38), fbm2(pit2 * 1.4));
		nave += float3(0.78, 0.52, 1.0) * (arch * 0.48 + rose * 0.65);
		nave += float3(1.0, 0.76, 0.34) * candle * 0.9;
		c = lerp(c, nave, down * 0.94);
	} else {
		float2 l1 = pit1 * 1.8 + float2(sin(t * 0.25), cos(t * 0.2)) * 0.4;
		float2 l2 = pit2 * 3.2 - float2(cos(t * 0.18), sin(t * 0.22)) * 0.5;
		float2 c1 = floor(l1), c2 = floor(l2);
		float2 f1 = frac(l1) - 0.5 - (hash22(c1) - 0.5) * 0.4;
		float2 f2 = frac(l2) - 0.5 - (hash22(c2) - 0.5) * 0.4;
		float o1 = exp(-dot(f1, f1) * 16.0) * step(0.55, hash21(c1));
		float o2 = exp(-dot(f2, f2) * 18.0) * step(0.58, hash21(c2));
		float3 sea = lerp(float3(0.06, 0.05, 0.14), float3(0.22, 0.18, 0.38), fbm2(pit1 * 0.9 + t * 0.05));
		sea += float3(1.0, 0.74, 0.32) * (o1 * 1.05 + o2 * 0.75);
		c = lerp(c, sea, down * 0.94);
		float2 sL = skyXZ * 0.45 + float2(t * 0.06, -t * 0.03);
		float2 sc = floor(sL);
		float2 sf = frac(sL) - 0.5 - (hash22(sc) - 0.5) * 0.4;
		float skyOrb = exp(-dot(sf, sf) * 16.0) * step(0.58, hash21(sc));
		c += float3(1.0, 0.74, 0.34) * skyOrb * up * 0.65;
	}
}

GOut PSSKY(QOut i)
{
	float3 top = Tint.rgb, bot = Extra.rgb;
	float y = i.uv.y;
	float3 c = lerp(top, bot, smoothstep(0, 1, y));
	float t = Eye.w;
	if (Params.y > 0.5) {
		// カットイン: 集中線
		float2 q = (i.uv - 0.5) * float2(Screen.x * Screen.w, 1);
		float ang = atan2(q.y, q.x);
		float r = length(q);
		float ln = step(0.86, frac(sin(floor(ang * 40.) * 91.7) * 437.5 + t * 3.)) * smoothstep(0.15, 0.6, r);
		c = lerp(c, OutlineColor.rgb, ln * 0.55);
	} else {
		float2 suv = float2(Params.z, Params.w);
		float2 q = i.uv - suv;
		q.x *= Screen.x * Screen.w;
		float sd = length(q);
		float vis = saturate(Extra.w);
		float3 sunCol = float3(1.0, 0.93, 0.72);
		c += sunCol * exp(-sd * 7.5) * 0.62 * vis;
		c += sunCol * smoothstep(0.05, 0.0, sd) * 1.35 * vis;
		c += sunCol * exp(-abs(y - 0.62) * 9.0) * 0.16 * vis;
		float ths = FogColor.a;
		float cl = fbm2(float2(i.uv.x * 2.1, y * 1.5 + t * 0.03));
		cl *= smoothstep(0.95, 0.28, y) * smoothstep(0.05, 0.22, y);
		float clAmt = 0.22;
		if (ths > 2.5 && ths < 3.5) clAmt = 0.06;
		else if (ths > 4.5 && ths < 5.5) clAmt = 0;
		else if (ths > 7.5 && ths < 8.5) clAmt = 0;
		else if (ths > 9.5 && ths < 13.0) clAmt = 0;
		c = lerp(c, float3(0.95, 0.97, 1), cl * clAmt);
		float night = 1. - saturate(dot(Tint.rgb, float3(0.25, 0.5, 0.25)) * 3.0);
		float2 cell = floor(i.uv * float2(240, 135));
		float hs = hash21(cell);
		float star = step(0.992, hs) * smoothstep(0.78, 0.12, y) * night;
		if (ths < 2.5 || (ths > 5.5 && ths < 7.5) || (ths > 8.5 && ths < 9.5) || ths > 12.5)
			c += (0.55 + 0.45 * sin(t * 1.8 + hs * 30.)) * star;
		float horz = smoothstep(0.18, 0.52, y) * smoothstep(0.92, 0.58, y);
		float skyA = smoothstep(0.88, 0.05, y);
		float gustS = GustAmt();
		if (ths < 0.5) {
			float streak = pow(0.5 + 0.5 * sin(i.uv.x * 4.2 - t * (0.18 + gustS * 0.25) + y * 1.4), 4);
			c += float3(1.0, 0.96, 0.78) * streak * skyA * 0.11;
			c = lerp(c, float3(0.8, 0.9, 1.0), horz * 0.06);
		} else if (ths < 1.5) {
			c = lerp(c, float3(1.0, 0.72, 0.42), horz * 0.14);
			c += float3(1, 0.55, 0.2) * exp(-abs(y - 0.58) * 10.0) * 0.12;
		} else if (ths < 2.5) {
			float ray = pow(0.5 + 0.5 * sin(i.uv.x * 9.0 + t * 0.2), 12);
			c += float3(0.75, 1, 0.5) * ray * skyA * 0.1;
			c = lerp(c, float3(0.55, 0.78, 0.62), horz * 0.08);
		} else if (ths < 3.5) {
			c = lerp(c, float3(0.78, 0.88, 1.0), horz * 0.14);
			float2 ap = float2(i.uv.x * 2.6 + t * 0.035, y * 1.7);
			float a1 = fbm2(ap);
			float a2 = fbm2(ap * 1.65 + float2(3.1, t * 0.025));
			float curtain = saturate(a1 * 0.55 + a2 * 0.45);
			curtain *= saturate(1.0 - abs(y - 0.22) * 2.6) * skyA;
			c += lerp(float3(0.14, 0.72, 0.52), float3(0.32, 0.4, 1.0), a2) * pow(curtain, 1.55) * 0.22;
			c += float3(0.7, 0.84, 1) * pow(skyA, 2) * 0.035;
		} else if (ths < 4.5) {
			c = lerp(c, float3(1.0, 0.82, 0.45), horz * 0.16);
			c += float3(1, 0.7, 0.3) * exp(-abs(y - 0.6) * 8.0) * 0.1;
		} else if (ths < 5.5) {
			c = lerp(c, float3(0.4, 0.1, 0.04), horz * 0.3);
			float heat = fbm2(float2(i.uv.x * 1.5, y * 3.2 - t * 0.07));
			c += float3(1.0, 0.26, 0.04) * heat * heat * skyA * 0.14;
			c += float3(1, 0.42, 0.08) * exp(-abs(y - 0.54) * 6.5) * 0.1;
		} else if (ths < 6.5) {
			c = lerp(c, float3(0.42, 0.55, 0.32), horz * 0.16);
			float wisp = pow(saturate(sin(i.uv.x * 6.0 + t * 0.2) * sin(y * 9.0 - t * 0.15)), 8.0);
			c += float3(0.45, 0.9, 0.35) * wisp * skyA * 0.16;
		} else if (ths < 7.5) {
			c = lerp(c, float3(0.45, 0.38, 0.7), horz * 0.12);
			c += float3(0.4, 0.6, 1) * skyA * 0.05;
		} else if (ths < 8.5) {
			c = lerp(c, float3(0.02, 0.16, 0.3), 0.38);
			float2 cp = float2(i.uv.x * 3.4 + t * 0.06, y * 4.0 - t * 0.04);
			float ca = saturate(fbm2(cp) * fbm2(cp.yx * 1.25 + 2.0));
			c += float3(0.12, 0.5, 0.82) * ca * skyA * 0.16;
			float colx = i.uv.x * 5.5 + fbm2(float2(i.uv.x * 2.0, t * 0.04)) * 0.7;
			float ray = pow(saturate(sin(colx * 6.28318) * 0.5 + 0.32), 14.0);
			c += float3(0.18, 0.62, 1.0) * ray * (0.12 + y * 0.32) * 0.09;
		} else if (ths < 9.5) {
			c = lerp(c, float3(0.82, 0.9, 0.98), horz * 0.14);
			c += float3(0.9, 0.96, 1) * skyA * 0.04;
		} else if (ths < 10.5) {
			c = lerp(c, float3(0.05, 0.06, 0.14), 0.32);
			float2 np = float2(i.uv.x * 1.7 + t * 0.018, y * 1.35);
			float n1 = fbm2(np);
			float n2 = fbm2(np * 2.05 + float2(4.0, -t * 0.012));
			float neb = saturate(n1 * 0.58 + n2 * 0.48);
			c += lerp(float3(0.36, 0.1, 0.68), float3(0.08, 0.36, 0.88), n2) * neb * neb * skyA * 0.4;
			float2 sg = i.uv * float2(380, 220);
			float2 scell = floor(sg);
			float2 sf = frac(sg) - 0.5;
			float sh = hash21(scell);
			float star = step(0.994, sh) * saturate(1.0 - length(sf) * 5.8);
			c += star * skyA * (0.5 + 0.5 * sin(t * 1.3 + sh * 18.0));
		} else if (ths < 11.5) {
			c = lerp(c, float3(0.07, 0.09, 0.13), 0.26);
			float2 bp = (i.uv - float2(0.5, 0.28)) * float2(Screen.x * Screen.w, 1);
			float ring = pow(0.5 + 0.5 * sin(length(bp) * 26.0 - t * 0.32), 16.0);
			c += float3(0.22, 0.68, 0.9) * ring * skyA * 0.07;
			float hx = abs(frac(i.uv.x * 7.0) - 0.5);
			c += float3(0.45, 0.6, 0.75) * smoothstep(0.47, 0.5, hx) * skyA * 0.035;
			float2 lg = i.uv * float2(110, 62);
			float2 lf = frac(lg) - 0.5;
			float lh = hash21(floor(lg) + floor(t * 1.4));
			c += float3(0.28, 0.88, 1.0) * step(0.991, lh) * saturate(1.0 - length(lf) * 6.2) * skyA * 0.32;
		} else if (ths < 12.5) {
			c = lerp(c, float3(0.26, 0.12, 0.4), horz * 0.28);
			float2 md = (float2(i.uv.x, y) - float2(0.72, 0.18)) * float2(1.8, 2.4);
			float moon = exp(-dot(md, md) * 7.0);
			c += float3(0.9, 0.84, 1.0) * moon * 0.72;
			c += float3(0.52, 0.32, 0.82) * exp(-dot(md, md) * 1.5) * skyA * 0.16;
			float beam = pow(saturate(1.0 - abs(i.uv.x - 0.72) * 16.0), 3.2) * saturate(0.48 - y);
			c += float3(0.7, 0.52, 1.0) * beam * 0.09;
		} else {
			c = lerp(c, float3(0.35, 0.32, 0.55), horz * 0.12);
			float2 lg = i.uv * float2(5.0, 3.0);
			float2 lc = floor(lg);
			float2 lf = frac(lg) - 0.5;
			float lh = hash21(lc);
			float orb = exp(-dot(lf, lf) * 10.0) * step(0.62, lh);
			c += float3(1.0, 0.72, 0.38) * orb * skyA * 0.45;
		}
		if (ths > -0.5)
			AbyssAndSky(c, WorldRay(i.uv), ths, t, gustS);
	}
	return EmitZ(float4(c, 1), 1);
}

float3 WeatherSample(float2 uv, float2 ofs, float3 col)
{
	return Tex.Sample(SL, saturate(uv + ofs)).rgb;
}

float Mote(float2 uv, float t, float2 vel, float density, float cutoff, float rad)
{
	float2 g = uv * density + t * vel;
	float2 cell = floor(g);
	float2 f = frac(g) - 0.5;
	float2 j = hash22(cell) - 0.5;
	return smoothstep(rad, 0, length(f - j * 0.55)) * step(cutoff, hash21(cell));
}

void HourFx(inout float3 col, float2 uv, float hour, float t)
{
	if (hour < 0.5) return;
	if (hour < 1.5) col = lerp(col, col * float3(1.06, 0.9, 0.74), 0.16);
	else if (hour < 2.5) col = lerp(col, col * float3(1.03, 0.98, 0.9), 0.08);
	else if (hour < 3.5) col = lerp(col, col * float3(1.08, 0.8, 0.58), 0.18);
	else if (hour < 4.5) col = lerp(col, col * float3(0.92, 0.95, 1.04), 0.1);
	else if (hour < 5.5) col = lerp(col, col * float3(0.9, 0.93, 1.03), 0.12);
	else if (hour < 6.5) col = lerp(col, col * float3(0.9, 0.93, 1.02), 0.1);
	else if (hour < 7.5) {
		col = lerp(col, col * float3(0.86, 0.9, 1.08), 0.18);
	} else if (hour < 8.5) {
		col = lerp(col, col * float3(0.82, 0.88, 1.06), 0.16);
		float2 sg = uv * float2(280, 160);
		float2 sf = frac(sg) - 0.5;
		float star = step(0.996, hash21(floor(sg) + floor(t * 0.2))) * saturate(1.0 - length(sf) * 5.5);
		col += star * float3(0.85, 0.9, 1) * saturate(0.4 - uv.y) * 0.8;
	} else col = lerp(col, col * float3(0.84, 0.9, 1.06), 0.18);
}

void WeatherFx(inout float3 col, float2 uv, float wx, float t)
{
	// 粒そのものはワールド側。ここは色味と、レンズに付いた水滴と、稲妻だけ。
	if (wx < 0.5) return;
	float wet = (wx > 0.5 && wx < 1.5) ? 1.0 : ((wx > 4.5 && wx < 5.5) ? 1.0 : 0.0);
	if (wet > 0.0) {
		col = lerp(col, col * float3(0.9, 0.94, 1.0), 0.08);
		float2 grid = float2(14, 8);
		float2 cell = floor(uv * grid);
		float2 f = frac(uv * grid);
		float h = hash21(cell + 3.1);
		float2 ctr = 0.5 + (hash22(cell) - 0.5) * 0.45;
		float d = length(f - ctr);
		float drop = smoothstep(0.16, 0.04, d) * step(0.78, h);
		float2 nrm = (f - ctr) / max(d, 1e-3);
		float3 bent = WeatherSample(uv, nrm * drop * 0.012, col);
		col = lerp(col, bent, drop * 0.45);
		if (wx > 4.5 && frac(t * 0.17) > 0.97) col += float3(0.45, 0.5, 0.62) * 0.35;
	} else if (wx < 2.5) col = lerp(col, col * float3(0.94, 0.96, 1), 0.06);
	else if (wx < 3.5) col = lerp(col, col * float3(0.9, 0.78, 0.66), 0.06);
	else if (wx < 4.5) {
		float fog = smoothstep(0.42, 0.0, uv.y) * (0.25 + 0.1 * sin(uv.x * 8.0 + t * 0.2));
		col = lerp(col, float3(0.78, 0.82, 0.86), saturate(fog) * 0.12);
	} else if (wx < 6.5) col = lerp(col, col * float3(0.98, 0.9, 0.72), 0.06);
	else if (wx < 7.5) col = lerp(col, col * float3(0.94, 0.96, 1), 0.05);
	else if (wx > 8.5) col = lerp(col, col * float3(1.03, 0.98, 0.9), 0.05);
}

// 近い画素（キャラと足元）はずらさない。陽炎や塵は空と遠い地面だけ。
float3 FarBend(float3 col, float2 uv, float t, float mask, float amp)
{
	if (mask < 0.02 || amp < 0.00005) return col;
	float yb = uv.y * 96.0 + uv.x * 8.0;
	float w1 = sin(yb + t * 2.8);
	float w2 = sin(yb * 0.43 - t * 1.35 + uv.x * 14.0);
	float2 ofs = float2(w1 * 0.8 + w2 * 0.45, w2 * 0.2) * amp * mask;
	float3 bent = Tex.SampleLevel(SL, saturate(uv + ofs), 0).rgb;
	float3 base = Tex.SampleLevel(SL, uv, 0).rgb;
	col += (bent - base) * saturate(mask);
	return col;
}

// マップごとに一つの空間技法。すべて世界座標の視線と平面に乗せる。近い画素（キャラ・移動マス）には掛けない。
void StageSignature(inout float3 col, float2 uv, float th, float t, float z, float sky, float far, float presence)
{
	if (th < 0) return;
	float room = 0.62;
	if (z > 0.02) {
		float2 px = Screen.zw * 2.2;
		float zr = DepthMap.SampleLevel(SL, uv + float2(px.x, 0), 0).r;
		float zl = DepthMap.SampleLevel(SL, uv - float2(px.x, 0), 0).r;
		float zu = DepthMap.SampleLevel(SL, uv + float2(0, px.y), 0).r;
		float zd = DepthMap.SampleLevel(SL, uv - float2(0, px.y), 0).r;
		float front = saturate((max(max(zr, zl), max(zu, zd)) - z) * 14.0);
		float nearC = 1.0 - smoothstep(0.05, 0.22, z);
		room = saturate(1.0 - max(front, nearC * 0.94));
	}
	float clear = smoothstep(0.16, 0.44, z);
	float veil = max(sky * clear, far * room) * saturate(presence);
	if (veil < 0.02) return;
	float gust = GustAmt();
	float3 rd = WorldRay(uv);
	float yaw = atan2(rd.x, rd.z);
	float3 wpos = Eye.xyz + rd * (max(z, 0.45) * max(Fog.y, 60.0));
	float2 wxz = wpos.xz;
	if (th < 0.5) {
		float2 p = wxz * float2(0.18, 0.07) - float2(t * (0.9 + gust * 0.8), t * 0.2);
		float rib = smoothstep(0.42, 0.78, fbm2(p) * 0.65 + fbm2(p * 2.3 + 1.9) * 0.5);
		float2 sg = wxz * 0.85 - float2(t * 1.4, t * 0.35);
		float2 sc = floor(sg);
		float seed = smoothstep(0.12, 0.02, length(frac(sg) - 0.5 - (hash22(sc) - 0.5) * 0.4)) * step(0.78, hash21(sc));
		col += float3(0.95, 0.98, 0.72) * (rib * 0.38 + seed * 0.55) * veil;
	} else if (th < 1.5) {
		float2 dp = wxz * float2(0.14, 0.28) - float2(t * 0.75, t * 0.18);
		float colm = smoothstep(0.4, 0.78, fbm2(dp) * 0.65 + fbm2(dp * 2.4) * 0.5);
		col = FarBend(col, uv, t * 0.7, veil * 0.85, 0.006);
		col += float3(1.0, 0.48, 0.16) * colm * veil * 0.44;
	} else if (th < 2.5) {
		float ray = pow(0.5 + 0.5 * sin(yaw * 16.0 + fbm2(wxz * 0.08 + t * 0.05) * 2.5 - t * 0.2), 8.0);
		float2 fg = wxz * 0.65 + float2(sin(t * 0.5), cos(t * 0.4)) * 0.5;
		float2 fc = floor(fg);
		float spore = smoothstep(0.14, 0.02, length(frac(fg) - 0.5 - (hash22(fc) - 0.5) * 0.4)) * step(0.75, hash21(fc));
		col += float3(0.72, 1.0, 0.48) * (ray * 0.42 + spore * 0.55) * veil;
	} else if (th < 3.5) {
		float2 ap = float2(yaw * 2.6 + t * 0.05, rd.y * 3.2);
		float a = fbm2(ap);
		float b = fbm2(ap * 1.9 + 4.0);
		float curtain = pow(saturate(a * 0.6 + b * 0.5), 1.5);
		float2 dg = wxz * 1.1 - float2(t * 1.6, t * 0.4);
		float diamond = step(0.94, hash21(floor(dg))) * (0.5 + 0.5 * sin(t * 6.0 + hash21(floor(dg)) * 30.0));
		col += lerp(float3(0.15, 0.88, 0.58), float3(0.38, 0.45, 1.0), b) * curtain * veil * 0.52;
		col += float3(0.88, 0.96, 1.0) * diamond * veil * 0.45;
	} else if (th < 4.5) {
		col = FarBend(col, uv, t * 1.15, veil, 0.012);
		float2 sp = wxz * float2(0.16, 0.45) - float2(t * 1.3, t * 0.25);
		float veilS = smoothstep(0.38, 0.76, fbm2(sp) * 0.65 + fbm2(sp * 2.3) * 0.5);
		col += float3(1.0, 0.78, 0.35) * veilS * veil * 0.42;
	} else if (th < 5.5) {
		float2 eg = float2(yaw * 18.0 + sin(t * 0.6 + wpos.y * 0.2), wpos.y * 0.45 - t * 1.1);
		float2 ec = floor(eg);
		float2 ef = frac(eg) - 0.5 - (hash22(ec) - 0.5) * 0.4;
		float ember = smoothstep(0.14, 0.02, length(ef)) * step(0.7, hash21(ec));
		float plume = smoothstep(0.4, 0.78, fbm2(wxz * 0.12 - float2(t * 0.2, t * 0.35)));
		col += float3(1.0, 0.32, 0.04) * (ember * 0.85 + plume * 0.32) * veil;
	} else if (th < 6.5) {
		float2 mp = wxz * 0.14 + Curl2(wxz * 0.08 + t * 0.06) * 0.8;
		float sheet = smoothstep(0.34, 0.74, fbm2(mp + float2(t * 0.14, -t * 0.09)));
		float2 wg = wxz * 0.45;
		float2 wc = floor(wg);
		float wisp = exp(-length(frac(wg) - 0.5 - (hash22(wc) - 0.5) * 0.35) * 10.0) * step(0.74, hash21(wc));
		col = lerp(col, float3(0.26, 0.48, 0.2), saturate(sheet * far * room) * 0.52);
		col += float3(0.45, 1.0, 0.35) * wisp * veil * 0.55;
	} else if (th < 7.5) {
		float2 p = PlaneXZ(rd, 18.0, 0.12) - float2(32.0, 32.0);
		float r = length(p) * 0.045;
		float ring = exp(-abs(frac(r * 2.5 - t * 0.06) - 0.5) * 24.0);
		float spin = 0.5 + 0.5 * sin(atan2(p.y, p.x) * 8.0 - t * 0.45);
		col += float3(0.45, 0.75, 1.0) * ring * spin * veil * 0.58;
	} else if (th < 8.5) {
		float c1 = CausAt(wxz * 0.24 + float2(t * 0.12, -t * 0.08), t);
		float shaft = pow(0.5 + 0.5 * sin(yaw * 12.0 + t * 0.2), 10.0);
		col += float3(0.18, 0.72, 1.0) * (c1 * 0.45 + shaft * 0.35) * veil;
	} else if (th < 9.5) {
		float ang = atan2(rd.y + 0.08, sin(yaw - 0.6));
		float rad = length(float2(sin(yaw - 0.6), rd.y + 0.08));
		float arc = exp(-pow((rad - 0.42) * 9.0, 2.0));
		float3 bow = lerp(float3(1, 0.35, 0.35), lerp(float3(0.4, 1, 0.45), float3(0.35, 0.55, 1), saturate(ang)), saturate(ang));
		float spray = smoothstep(0.38, 0.76, fbm2(wxz * 0.18 - float2(0, t * 0.8)));
		col += (bow * arc * 0.55 + float3(0.82, 0.94, 1.0) * spray * 0.32) * veil;
	} else if (th < 10.5) {
		float2 u = float2(yaw * 2.0, rd.y * 2.6);
		float n = fbm2(u + t * 0.02) * 0.6 + fbm2(u * 2.2 - t * 0.015) * 0.5;
		float2 sg = float2(yaw * 120.0, rd.y * 180.0);
		float star = step(0.99, hash21(floor(sg))) * saturate(1.0 - length(frac(sg) - 0.5) * 5.5);
		col += lerp(float3(0.48, 0.12, 0.82), float3(0.1, 0.45, 1.0), saturate(n)) * n * veil * 0.58;
		col += float3(0.92, 0.96, 1.0) * star * veil * 0.85;
	} else if (th < 11.5) {
		float2 g = abs(frac(wxz * 0.35) - 0.5);
		float grid = smoothstep(0.45, 0.495, max(g.x, g.y));
		float scan = pow(0.5 + 0.5 * sin(wpos.y * 2.5 - t * 2.4), 10.0);
		float2 bc = floor(wxz * 0.35);
		float blink = step(0.8, hash21(bc)) * (0.4 + 0.6 * sin(t * 3.2 + hash21(bc) * 14.0));
		col += float3(0.25, 0.88, 1.0) * (grid * 0.45 + scan * 0.28 + blink * grid * 0.55) * veil;
	} else if (th < 12.5) {
		float3 moonDir = normalize(float3(0.45, 0.38, 0.8));
		float md = saturate(dot(rd, moonDir));
		float moon = pow(md, 180.0) + pow(md, 16.0) * 0.35;
		float ray = pow(0.5 + 0.5 * sin(yaw * 14.0 + t * 0.15), 10.0);
		col += (float3(0.92, 0.86, 1.0) * moon * 0.75 + float3(0.68, 0.48, 1.0) * ray * 0.35) * veil;
	} else {
		float2 lg = wxz * 0.28 + float2(sin(t * 0.2), cos(t * 0.18)) * 0.35;
		float2 lc = floor(lg);
		float2 lf = frac(lg) - 0.5 - (hash22(lc) - 0.5) * 0.4;
		float lh = hash21(lc);
		float orb = exp(-dot(lf, lf) * 16.0) * step(0.56, lh);
		float bob = 0.55 + 0.45 * sin(t * 1.4 + lh * 12.0);
		col += float3(1.0, 0.74, 0.36) * orb * bob * veil * 0.82;
	}
}

void ThemeWorld(inout float3 col, float2 uv, float th, float t, float z)
{
	if (th < 0) return;
	float tier = StageTier();
	float presence = (tier < 0.5) ? 0.28 : (0.42 + tier * 0.12);
	float sky = saturate((0.4 - uv.y) * 2.5);
	float hasZ = step(0.02, z);
	float far = hasZ * smoothstep(0.5, 0.84, z);
	StageSignature(col, uv, th, t, z, sky, far, max(presence, 0.72));
	if (tier > 2.5 && hasZ > 0.5) {
		float2 px = Screen.zw * (tier > 5.5 ? 2.4 : 1.8);
		float zr = DepthMap.SampleLevel(SL, uv + float2(px.x, 0), 0).r;
		float zl = DepthMap.SampleLevel(SL, uv - float2(px.x, 0), 0).r;
		float zu = DepthMap.SampleLevel(SL, uv + float2(0, px.y), 0).r;
		float zd = DepthMap.SampleLevel(SL, uv - float2(0, px.y), 0).r;
		float zMax = max(max(zr, zl), max(zu, zd));
		float zMin = min(min(zr, zl), min(zu, zd));
		float front = saturate((zMax - z) * (tier > 5.5 ? 12.0 : 9.0));
		float back = saturate((z - zMin) * 6.0);
		float3 edge = float3(1, 0.97, 0.92);
		float rimK = (tier > 5.5) ? 0.16 : ((tier > 4.5) ? 0.12 : 0.08);
		col += edge * front * rimK;
		col *= 1.0 - back * (tier > 5.5 ? 0.14 : 0.1);
	}
}

// HUD の前。明るいところを抜き、ぼかして、太陽方向へ筋を足す。
float4 PSBloom(QOut i) : SV_Target
{
	float3 color = min(Tex.Sample(SL, i.uv).rgb, 4);
	float brightness = dot(color, float3(0.2126, 0.7152, 0.0722));
	float knee = 0.16;
	float thr = Params.y;
	float soft = saturate((brightness - thr + knee) / max(1e-3, knee * 2.));
	float contrib = max(soft * soft * knee, brightness - thr);
	contrib = max(contrib, 0) / max(brightness, 1e-4);
	return float4(color * contrib, 1);
}

float4 PSBlur(QOut i) : SV_Target
{
	float2 step = Params.xy * Params.z * max(Params.w, 1);
	float3 c = Tex.Sample(SL, i.uv).rgb * 0.227027;
	c += (Tex.Sample(SL, i.uv + step).rgb + Tex.Sample(SL, i.uv - step).rgb) * 0.1945946;
	c += (Tex.Sample(SL, i.uv + step * 2).rgb + Tex.Sample(SL, i.uv - step * 2).rgb) * 0.1216216;
	c += (Tex.Sample(SL, i.uv + step * 3).rgb + Tex.Sample(SL, i.uv - step * 3).rgb) * 0.054054;
	c += (Tex.Sample(SL, i.uv + step * 4).rgb + Tex.Sample(SL, i.uv - step * 4).rgb) * 0.016216;
	if (Q.x > 2.5) {
		c += (Tex.Sample(SL, i.uv + step * 5.5).rgb + Tex.Sample(SL, i.uv - step * 5.5).rgb) * (Q.x > 5.5 ? 0.02 : 0.012);
		c += (Tex.Sample(SL, i.uv + step * 7.5).rgb + Tex.Sample(SL, i.uv - step * 7.5).rgb) * (Q.x > 5.5 ? 0.012 : 0.006);
		c *= (Q.x > 5.5) ? 0.94 : 0.97;
	}
	return float4(c, 1);
}

float4 PSGrade(QOut i) : SV_Target
{
	float3 scene = Tex.Sample(SL, i.uv).rgb;
	float3 bloom = ShadowMap.Sample(SL, i.uv).rgb;
	float3 col = scene + bloom * Params.y;
	float2 suv = float2(Params.z, Params.w);
	float2 toSun = suv - i.uv;
	float2 asp = float2(Screen.x * Screen.w, 1);
	float dist = length(toSun * asp);
	float3 shaft = 0;
	float wsum = 0;
	int shaftN = (Q.x > 5.5) ? 16 : ((Q.x > 2.5) ? 12 : 8);
	[loop] for (int k = 1; k <= shaftN; k++) {
		float t = k / (float)shaftN;
		float w = 1. - t;
		shaft += ShadowMap.SampleLevel(SL, i.uv + toSun * t * 0.85, 0).rgb * w;
		wsum += w;
	}
	float th = FogColor.a;
	float3 grade = float3(1, 1, 1);
	float shaftK = 1;
	if (th < 0) shaftK = 0;
	else if (th < 0.5) { grade = float3(1.02, 1.03, 0.97); shaftK = 0.85; }
	else if (th < 1.5) { grade = float3(1.04, 0.98, 0.92); shaftK = 0.45; }
	else if (th < 2.5) { grade = float3(0.97, 1.03, 0.97); shaftK = 0.7; }
	else if (th < 3.5) { grade = float3(0.98, 0.99, 1.03); shaftK = 0.1; }
	else if (th < 4.5) { grade = float3(1.04, 1.0, 0.92); shaftK = 0.75; }
	else if (th < 5.5) { grade = float3(1.03, 0.95, 0.9); shaftK = 0.08; }
	else if (th < 6.5) { grade = float3(0.97, 1.02, 0.95); shaftK = 0.3; }
	else if (th < 7.5) { grade = float3(0.97, 0.98, 1.03); shaftK = 0.55; }
	else if (th < 8.5) { grade = float3(0.92, 0.98, 1.03); shaftK = 0.12; }
	else if (th < 9.5) { grade = float3(0.98, 0.99, 1.02); shaftK = 0.9; }
	else if (th < 10.5) { grade = float3(0.96, 0.98, 1.14); shaftK = 0.08; }
	else if (th < 11.5) { grade = float3(0.9, 0.95, 1.04); shaftK = 0.08; }
	else if (th < 12.5) { grade = float3(1.08, 0.96, 1.14); shaftK = 0.1; }
	else { grade = float3(0.96, 0.97, 1.06); shaftK = 0.55; }
	// 筋は太陽が空にあるとき、空の側だけ。盤面へ放射状に広げない。
	float sky = saturate((0.46 - i.uv.y) * 2.6);
	float sunUp = saturate((0.4 - suv.y) * 3.2);
	float gt = Eye.w;
	float zpix = DepthMap.SampleLevel(SL, i.uv, 0).r;
	float shaftAmt = 0.22 + saturate(StageTier() / 6.0) * 0.28;
	float3 shaftAdd = shaft / max(wsum, 1e-3) * Extra.w * saturate(0.7 - dist) * shaftAmt * shaftK * sky * sunUp;
	float2 px = Screen.zw;
	float vig = 1;
	ThemeWorld(col, i.uv, th, gt, zpix);
	if (StageTier() > 1.5 && zpix > 0.32) {
		float airZ = smoothstep(0.32, 0.86, zpix);
		float3 scatter = FogColor.rgb;
		if (th < 0.5) scatter = float3(0.7, 0.85, 1);
		else if (th < 1.5) scatter = float3(1, 0.72, 0.42);
		else if (th < 3.5) scatter = float3(0.78, 0.88, 1);
		else if (th < 4.5) scatter = float3(1, 0.8, 0.45);
		else if (th < 5.5) scatter = float3(1, 0.4, 0.15);
		else if (th < 8.5) scatter = float3(0.3, 0.7, 0.95);
		else if (th < 10.5) scatter = float3(0.35, 0.28, 0.7);
		col = lerp(col, col + scatter * 0.1, airZ * 0.42);
	}
	col += shaftAdd;
	float2 q = (i.uv - 0.5) * asp;
	col *= 1. - saturate(dot(q, q) * 1.15) * Extra.z * 0.28 * vig;
	float y = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lerp(y.xxx, col, 1.06);
	col *= grade;
	// 標準は輪郭を少し立てる。高画質は弱く、超綺麗は色ずれを足す。軽量はこの分岐に入らない。
	if (Q.x > 0.5 && Q.x < 3.5) {
		float k = (Q.x < 1.5) ? 0.16 : ((Q.x < 2.5) ? 0.08 : 0.04);
		float3 nb = (Tex.Sample(SL, i.uv + px).rgb + Tex.Sample(SL, i.uv - px).rgb
			+ Tex.Sample(SL, i.uv + float2(px.x, -px.y)).rgb + Tex.Sample(SL, i.uv + float2(-px.x, px.y)).rgb) * 0.25;
		col += (col - nb) * k;
	}
	if ((Q.x > 2.5 && Q.x < 3.5) || Q.x > 4.5) {
		float spread = (Q.x > 5.5) ? 2.4 : ((Q.x > 4.5) ? 2.0 : 1.6);
		float mixk = (Q.x > 5.5) ? 0.16 : ((Q.x > 4.5) ? 0.12 : 0.1);
		float nearKeep = (zpix > 0.02) ? lerp(0.22, 1.0, smoothstep(0.28, 0.62, zpix)) : 0.35;
		float2 ca = (i.uv - 0.5) * px * spread;
		float3 fr = float3(Tex.Sample(SL, i.uv + ca).r, col.g, Tex.Sample(SL, i.uv - ca).b);
		col = lerp(col, fr, mixk * nearKeep);
	}
	if (Q.x > 4.5) {
		float rad = (Q.x > 5.5) ? 1.7 : 1.2;
		float3 nb = 0;
		[unroll] for (int s = 0; s < 8; s++) {
			float a = s * 0.785398;
			nb += Tex.Sample(SL, i.uv + float2(cos(a), sin(a)) * px * rad).rgb;
		}
		col += (col - nb * 0.125) * (Q.x > 5.5 ? 0.22 : 0.14);
	}
	WeatherFx(col, i.uv, Extra.x, gt);
	HourFx(col, i.uv, Extra.y, gt);
	return float4(saturate(col), 1);
}

float4 PSGradeTiny(QOut i) : SV_Target
{
	float3 scene = Tex.Sample(SL, i.uv).rgb;
	float y = dot(scene, float3(0.25, 0.5, 0.25));
	scene = lerp(y.xxx, scene, 1.04);
	float2 q = (i.uv - 0.5) * float2(Screen.x * Screen.w, 1);
	scene *= 1. - saturate(dot(q, q) * 0.35) * 0.08;
	ThemeWorld(scene, i.uv, FogColor.a, Eye.w, 0);
	WeatherFx(scene, i.uv, Extra.x, Eye.w);
	HourFx(scene, i.uv, Extra.y, Eye.w);
	return float4(saturate(scene), 1);
}

// パネル。uv は矩形内 0..1。頂点色が塗り、Tint が縁。
float4 PSUI(HOut i) : SV_Target
{
	float2 size = max(Params.xy, float2(2, 2));
	float rad = max(Params.z, 1);
	float2 p = (i.uv - 0.5) * size;
	float2 q = abs(p) - (size * 0.5 - rad);
	float d = length(max(q, 0)) + min(max(q.x, q.y), 0) - rad;
	float aa = 1.25;
	float body = saturate(0.5 - d / aa);
	float border = saturate(1.4 - abs(d) / aa) * body;
	float sheen = pow(saturate(1 - i.uv.y), 2.2) * saturate(1 - abs(i.uv.x - 0.5) * 1.4);
	float inner = smoothstep(0, 10, -d) * (1 - smoothstep(0, 18, -d));
	float3 fill = i.c.rgb;
	float3 col = fill * (0.92 + sheen * 0.22);
	col *= 1 - inner * 0.18;
	col = lerp(col, Tint.rgb, border * 0.85);
	float a = body * Params.w;
	return float4(col * a, a);
}
