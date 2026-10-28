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

Texture2D Tex : register(t0);
Texture2D ShadowMap : register(t1);
Texture2DArray Ground : register(t3); // 0..7 地面アルベド(a=AO) 8..15 法線。スロット表の外。
Texture2DArray Mat : register(t4);    // 樹皮・葉・岩・木材。各アルベドと法線。
SamplerState SL : register(s0);
SamplerComparisonState SC : register(s1);

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
float fbm2(float2 p) { float s = 0, a = .5; [unroll] for (int i = 0; i < 4; i++) { s += a * vnoise(p); p *= 2.03; a *= .5; } return s; }

// amt: 頂点αに焼いた「根元からの高さ」重み（0=揺れない）
float3 Sway(float3 w, float amt)
{
	if (amt <= 0) return w;
	float t = Eye.w;
	float s = sin(t * 1.7 + w.x * 0.9 + w.z * 0.7) * 0.5 + sin(t * 2.9 + w.x * 1.7) * 0.25;
	w.x += s * amt * 0.07;
	w.z += cos(t * 1.3 + w.z * 1.1 + w.x * 0.3) * amt * 0.05;
	return w;
}

PSIn VSS(VSIn v)
{
	PSIn o;
	float3 w = mul(float4(v.p, 1), World).xyz;
	float3 n = normalize(mul(v.n, (float3x3)World));
	w = Sway(w, (1. - v.c.a) * 4. + Extra.w);
	if (Params.y > 0.5 && Params.y < 1.5) {
		float t = Eye.w;
		float th = FogColor.a;
		float amp = 0.045;
		if (th > 2.5 && th < 3.5) amp = 0.012;
		else if (th > 5.5 && th < 6.5) amp = 0.022;
		else if (th > 6.5) amp = 0.01;
		w.y += sin(w.x * 1.6 + w.z * 0.7 + t * 1.25) * amp + cos(w.z * 2.0 - t * 0.8) * amp * 0.7;
	} else if (Params.y > 1.5 && Params.y < 2.5) {
		float t = Eye.w;
		w.y += sin(w.x * 0.7 + w.z * 0.55 + t * 0.55) * 0.018;
	}
	w += n * Params.x;
	o.w = w; o.n = n; o.uv = v.uv; o.c = v.c;
	o.pos = mul(float4(w, 1), VP);
	return o;
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
	const float2 k[12] = {
		float2(-0.326212, -0.405810), float2(-0.840144, -0.073580), float2(-0.695914, 0.457137),
		float2(-0.203345, 0.620716), float2(0.962340, -0.194983), float2(0.473434, -0.480026),
		float2(0.519456, 0.767022), float2(0.185461, -0.893124), float2(0.507431, 0.064425),
		float2(0.896420, 0.412458), float2(-0.321940, -0.932615), float2(-0.791559, -0.597710)
	};
	float t = Fog.w * 2.2;
	float s = 0;
	[unroll] for (int i = 0; i < 12; i++)
		s += ShadowMap.SampleCmpLevelZero(SC, uv + k[i] * t, z);
	return s / 12.;
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

// ランバート＋GGX。トゥーンの段は使わない。skin>0 は顔のラップだけ。
float3 Lit(float3 albedo, float3 n, float3 v, float rough, float3 f0, float sh, float skin)
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

float3 FogAt(float3 col, float3 w)
{
	float d = length(Eye.xyz - w);
	float fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
	fg = saturate(fg + saturate(-w.y * Fog.z));
	return lerp(col, FogColor.rgb, fg * fg * 0.9);
}

float4 PSM(PSIn i) : SV_Target
{
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
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
	col += rim * float3(1, 0.95, 0.85) * (0.35 + 0.65 * sh);
	col += emis;
	col = FogAt(col, i.w);
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		float cut = Extra.x * 1.1 - 0.05;
		clip(nz - cut);
		col += float3(1, 0.6, 0.25) * smoothstep(0.08, 0., nz - cut) * 2.;
	}
	col = lerp(col, float3(1, 1, 1), Extra.y);
	return float4(col, alpha);
}

float WaveAmp(float th)
{
	float amp = 0.16;
	if (th > 2.5 && th < 3.5) amp = 0.04;
	else if (th > 5.5 && th < 6.5) amp = 0.07;
	else if (th > 0.5 && th < 1.5) amp = 0.1;
	else if (th > 4.5 && th < 5.5) amp = 0.13;
	else if (th > 6.5) amp = 0.035;
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
}

float4 PSWater(PSIn i) : SV_Target
{
	float3 v = normalize(Eye.xyz - i.w);
	float4 tx = Tex.Sample(SL, i.uv);
	float3 albedo = tx.rgb * i.c.rgb * Tint.rgb;
	float alpha = Tint.a;
	float t = Eye.w;
	float thw = FogColor.a;
	float amp = WaveAmp(thw);
	float h;
	float2 dh;
	WaterWave(i.w.xz, t, amp, h, dh);
	float3 wn = normalize(float3(-dh.x, 1, -dh.y));
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
	col += float3(1, 0.97, 0.88) * sp * (thw > 2.5 && thw < 3.5 ? 1.6 : 0.9);
	col = lerp(col, float3(0.9, 0.95, 0.98), crest * (thw > 5.5 && thw < 6.5 ? 0.15 : 0.42));
	float2 c1 = i.w.xz * 0.8 + t * float2(0.15, 0.05);
	float2 c2 = i.w.xz * 1.3 - t * float2(0.07, 0.12);
	float caus = saturate(sin(c1.x * 6.2) * sin(c1.y * 5.4) + sin(c2.x * 4.8) * sin(c2.y * 5.1));
	col += albedo * pow(caus, 2) * 0.18;
	float see = 0.22;
	if (thw > 2.5 && thw < 3.5) { col = lerp(col, float3(0.82, 0.92, 1), 0.28); see = 0.16; }
	else if (thw > 0.5 && thw < 1.5) { col = lerp(col, albedo * float3(0.95, 0.72, 0.45), 0.22); see = 0.34; }
	else if (thw > 1.5 && thw < 2.5) { col *= float3(0.82, 0.96, 0.78); see = 0.26; }
	else if (thw > 3.5 && thw < 4.5) { col = lerp(col, albedo * float3(0.7, 0.9, 0.88), 0.18); see = 0.2; }
	else if (thw > 4.5 && thw < 5.5) { col = lerp(col, albedo * float3(0.55, 0.32, 0.22), 0.3); see = 0.4; }
	else if (thw > 5.5 && thw < 6.5) { col = lerp(col, albedo * float3(0.45, 0.62, 0.38), 0.35); see = 0.42; }
	else if (thw > 6.5) { col = lerp(col, SkyAmb.rgb, fr * 0.22); see = 0.38; }
	return float4(FogAt(col, i.w), lerp(see, 0.78, fr) * alpha);
}

float3 LavaColor(float2 xz, float t, float3 v)
{
	float2 vg = VoronoiGap(xz * 0.72 + float2(t * 0.06, t * 0.02));
	float crack = 1 - smoothstep(0.015, 0.11, vg.y);
	float pulse = 0.82 + 0.18 * sin(t * 2.2 + vg.x * 9);
	float3 hot = lerp(float3(1, 0.72, 0.18), float3(1, 0.22, 0.02), saturate(vg.y * 8));
	float3 col = lerp(float3(0.12, 0.035, 0.02), hot * (1.5 * pulse), crack);
	float3 H = normalize(normalize(LightDir.xyz) + v);
	col += hot * pow(saturate(dot(float3(0, 1, 0), H)), 16) * crack * 0.35;
	return col;
}

float4 PSLava(PSIn i) : SV_Target
{
	float3 v = normalize(Eye.xyz - i.w);
	float t = Eye.w;
	float2 o = float2(0.045, 0.02);
	float3 col = LavaColor(i.w.xz, t, v);
	col += LavaColor(i.w.xz + o, t, v);
	col += LavaColor(i.w.xz - o, t, v);
	col += LavaColor(i.w.xz + float2(-o.y, o.x), t, v);
	return float4(col * 0.25, 1);
}

float4 GroundPS(PSIn i, float theme)
{
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	float cliff = n.y > 0.72 ? 0 : 1;
	float2 uv = i.w.xz * (cliff > 0.5 ? 0.26 : 0.4);
	// 水底だけ頂点αを 254 にしてある。水面と同じ波でテクスチャをずらし、底がゆれて見える。
	float bedCaus = 0;
	if (i.c.a < 0.999 && cliff < 0.5) {
		float h;
		float2 dh;
		WaterWave(i.w.xz, Eye.w, WaveAmp((float)theme), h, dh);
		uv += dh * 0.16;
		float2 c1 = i.w.xz * 0.8 + Eye.w * float2(0.15, 0.05);
		float2 c2 = i.w.xz * 1.3 - Eye.w * float2(0.07, 0.12);
		bedCaus = pow(saturate(sin(c1.x * 6.2) * sin(c1.y * 5.4) + sin(c2.x * 4.8) * sin(c2.y * 5.1)), 2);
	}
	float4 alb = Ground.Sample(SL, float3(uv, theme));
	float4 nr = Ground.Sample(SL, float3(uv, theme + 8));
	float3 albedo = alb.rgb * lerp(0.78, 1, alb.a) * i.c.rgb;
	if (cliff > 0.5) albedo *= 0.8;
	float3 nLit = Bump(n, nr.rgb);
	float rough = 0.7;
	float3 glow = 0;
	float3 f0 = float3(0.035, 0.035, 0.035);
	if (theme < 0.5) {
		rough = 0.82;
		float3 gdir = float3(0.85, 0, 0.52);
		glow += float3(0.75, 0.9, 0.55) * Aniso(gdir, normalize(normalize(LightDir.xyz) + v), saturate(dot(nLit, normalize(normalize(LightDir.xyz) + v))), 14) * 0.04;
	} else if (theme < 1.5) {
		rough = 0.58;
		if (cliff > 0.5) {
			float groove = 1 - smoothstep(0.012, 0.05, abs(frac(i.w.y * 1.15) - 0.5));
			albedo *= 1 - groove * 0.5;
			nLit = normalize(nLit + float3(0, cos(i.w.y * 7.2) * 0.4, 0));
		}
	} else if (theme < 2.5) {
		rough = 0.38;
		f0 = float3(0.05, 0.055, 0.045);
	} else if (theme < 3.5) {
		rough = 0.22;
		f0 = float3(0.06, 0.065, 0.07);
		float spark = step(0.993, hash21(floor(i.w.xz * 20 + i.w.y))) * pow(saturate(dot(nLit, normalize(normalize(LightDir.xyz) + v))), 72);
		glow += float3(0.85, 0.93, 1) * spark;
	} else if (theme < 4.5) {
		rough = 0.74;
		nLit = normalize(nLit + float3(cos(i.w.x * 1.7) * 0.18, 0, 0));
	} else if (theme < 5.5) {
		rough = 0.55;
		float2 vg = VoronoiGap(i.w.xz * 0.55 + float2(Eye.w * 0.03, Eye.w * 0.012));
		float crack = 1 - smoothstep(0.02, 0.09, vg.y);
		albedo *= 1 - crack * 0.35;
		glow += float3(1.0, 0.32, 0.05) * crack * (cliff > 0.5 ? 0.25 : 0.85);
	} else if (theme < 6.5) {
		rough = 0.28;
		f0 = float3(0.05, 0.06, 0.04);
		float2 c1 = i.w.xz * 0.31 + Eye.w * float2(0.05, 0.02);
		float2 c2 = i.w.xz * 0.47 - Eye.w * float2(0.03, 0.06);
		float caus = saturate(sin(c1.x * 9.1) * sin(c1.y * 8.3) + sin(c2.x * 7.4) * sin(c2.y * 6.2));
		glow += float3(0.45, 0.9, 0.5) * pow(caus, 2) * 0.28;
	} else {
		rough = 0.4;
		f0 = float3(0.055, 0.055, 0.06);
	}
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	col += glow;
	if (bedCaus > 0) {
		float3 causCol = float3(0.65, 0.85, 1);
		if (theme > 4.5 && theme < 5.5) causCol = float3(1, 0.55, 0.25);
		else if (theme > 5.5 && theme < 6.5) causCol = float3(0.45, 0.85, 0.4);
		else if (theme > 2.5 && theme < 3.5) causCol = float3(0.85, 0.93, 1);
		else if (theme > 0.5 && theme < 1.5) causCol = float3(0.9, 0.7, 0.4);
		col += causCol * bedCaus * 0.35;
	}
	if (cliff < 0.5) {
		float2 g = abs(frac(i.w.xz) - 0.5);
		float edge = smoothstep(0.475, 0.5, max(g.x, g.y));
		col *= 1. - edge * (theme > 2.5 && theme < 4.5 ? 0.08 : 0.16);
	}
	return float4(FogAt(col, i.w), 1);
}

float4 PSG0(PSIn i) : SV_Target { return GroundPS(i, 0); }
float4 PSG1(PSIn i) : SV_Target { return GroundPS(i, 1); }
float4 PSG2(PSIn i) : SV_Target { return GroundPS(i, 2); }
float4 PSG3(PSIn i) : SV_Target { return GroundPS(i, 3); }
float4 PSG4(PSIn i) : SV_Target { return GroundPS(i, 4); }
float4 PSG5(PSIn i) : SV_Target { return GroundPS(i, 5); }
float4 PSG6(PSIn i) : SV_Target { return GroundPS(i, 6); }
float4 PSG7(PSIn i) : SV_Target { return GroundPS(i, 7); }

float4 PSProp(PSIn i) : SV_Target
{
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
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	return float4(FogAt(col, i.w), Tint.a);
}

float4 PSO(PSIn i) : SV_Target
{
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		clip(nz - (Extra.x * 1.1 - 0.05));
	}
	return float4(lerp(OutlineColor.rgb, float3(1, 1, 1), Extra.y), OutlineColor.a * Tint.a);
}

float4 PSOV(PSIn i) : SV_Target
{
	// 移動・攻撃範囲セル。uv=セル内座標
	float2 g = abs(i.uv - 0.5) * 2.;
	float edge = max(g.x, g.y);
	float t = Eye.w;
	float pulse = 0.82 + 0.18 * sin(t * 4.0 + (i.w.x + i.w.z) * 0.6);
	float border = smoothstep(0.72, 0.96, edge);
	float inner = 0.42 + 0.25 * smoothstep(0.0, 0.9, edge);
	float a = (inner + border * 0.9) * i.c.a * Tint.a * pulse;
	float3 c = i.c.rgb * Tint.rgb * (1. + border * 0.6);
	if (Params.y > 0.5) {
		// カーソル: 角だけ光る枠
		float corner = step(0.62, g.x) * step(0.62, g.y);
		float frame = smoothstep(0.82, 0.9, edge) * (1. - smoothstep(0.98, 1.0, edge));
		a = (frame * (0.55 + corner * 0.45) + 0.12) * i.c.a * Tint.a;
		c = i.c.rgb * Tint.rgb * (1.2 + corner);
	}
	return float4(c, saturate(a));
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
POut VSP(PIn v)
{
	POut o;
	o.pos = mul(float4(v.p, 1), VP);
	o.uvs = v.uvs; o.c = v.c;
	return o;
}
float4 PSP(POut i) : SV_Target
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
	return float4(i.c.rgb * (a * i.c.a) * (1. + hot * hot * 0.7), a * i.c.a);
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
float4 PSSKY(QOut i) : SV_Target
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
		float cl = saturate(sin(i.uv.x * 7.5 + t * 0.05) * 0.5 + 0.5);
		cl *= saturate(sin(i.uv.x * 2.2 - t * 0.02 + i.uv.y * 1.4) * 0.5 + 0.55);
		cl *= smoothstep(0.95, 0.28, y) * smoothstep(0.05, 0.22, y);
		c = lerp(c, float3(0.95, 0.97, 1), cl * 0.28);
		float night = 1. - saturate(dot(Tint.rgb, float3(0.25, 0.5, 0.25)) * 3.0);
		float2 cell = floor(i.uv * float2(240, 135));
		float hs = hash21(cell);
		float star = step(0.992, hs) * smoothstep(0.78, 0.12, y) * night;
		c += (0.55 + 0.45 * sin(t * 1.8 + hs * 30.)) * star;
	}
	return float4(c, 1);
}

float Mote(float2 uv, float t, float2 vel, float density, float cutoff, float rad)
{
	float2 g = uv * density + t * vel;
	float2 cell = floor(g);
	float2 f = frac(g) - 0.5;
	float2 j = hash22(cell) - 0.5;
	return smoothstep(rad, 0, length(f - j * 0.55)) * step(cutoff, hash21(cell));
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
	[unroll] for (int k = 1; k <= 8; k++) {
		float t = k / 8.;
		float w = 1. - t;
		shaft += ShadowMap.SampleLevel(SL, i.uv + toSun * t * 0.85, 0).rgb * w;
		wsum += w;
	}
	float th = FogColor.a;
	float3 grade = float3(1, 1, 1);
	float shaftK = 1;
	if (th < 0.5) grade = float3(1.0, 1.02, 0.98);
	else if (th < 1.5) grade = float3(1.04, 0.98, 0.92);
	else if (th < 2.5) { grade = float3(0.96, 1.03, 0.96); shaftK = 1.2; }
	else if (th < 3.5) { grade = float3(0.92, 0.97, 1.06); shaftK = 0.72; }
	else if (th < 4.5) { grade = float3(1.05, 0.98, 0.88); shaftK = 0.5; }
	else if (th < 5.5) { grade = float3(1.06, 0.9, 0.8); shaftK = 0.35; }
	else if (th < 6.5) { grade = float3(0.92, 1.0, 0.9); shaftK = 0.4; }
	else { grade = float3(0.96, 0.97, 1.04); shaftK = 1.1; }
	float3 shaftAdd = shaft / max(wsum, 1e-3) * Extra.w * saturate(1.15 - dist) * 0.5 * shaftK;
	float gt = Eye.w;
	float2 px = Screen.zw;
	float vig = 1;
	// 0平原の花粉 1峡谷の砂 2森の木漏れ日 3雪 4砂漠のかげろう 5火山の熱 6沼の揺らぎ 7廃墟の塵
	if (th < 0.5) {
		col += Mote(i.uv, gt, float2(0.04, 0.015), 26, 0.94, 0.07) * float3(1, 0.96, 0.72);
	} else if (th < 1.5) {
		col += Mote(i.uv, gt, float2(0.05, -0.01), 22, 0.93, 0.09) * float3(0.86, 0.68, 0.46) * 0.8;
	} else if (th < 2.5) {
		float dap = fbm2(i.uv * 6. + float2(gt * 0.03, gt * 0.01));
		col *= lerp(0.94, 1.04, dap);
		col *= 1. - saturate(0.4 - i.uv.y) * 0.16;
	} else if (th < 3.5) {
		col += Mote(i.uv, gt, float2(0.35, 3.4), 64, 0.988, 0.05) * float3(0.94, 0.97, 1) * 1.3;
	} else if (th < 4.5) {
		float2 h = float2(sin(i.uv.y * 170. + gt * 5.5), 0) * px * 0.8;
		scene = lerp(scene, Tex.Sample(SL, i.uv + h).rgb, 0.45);
		col = scene + bloom * Params.y;
	} else if (th < 5.5) {
		float2 h = float2(sin(i.uv.y * 130. + gt * 3.6), cos(i.uv.x * 110. - gt * 2.8)) * px * 1.5;
		float3 a = Tex.Sample(SL, i.uv + h).rgb;
		float3 b = Tex.Sample(SL, i.uv - h).rgb;
		float3 c = Tex.Sample(SL, i.uv + float2(h.y, -h.x)).rgb;
		scene = (scene + a + b + c) * 0.25;
		col = scene + bloom * Params.y;
		col += Mote(i.uv, gt, float2(0.15, -2.4), 48, 0.986, 0.045) * float3(1, 0.42, 0.08) * 1.4;
	} else if (th < 6.5) {
		float ang = sin(i.uv.x * 10. + gt * 0.25) + cos(i.uv.y * 7. + gt * 0.15);
		float2 off = float2(sin(ang * 1.2), cos(ang * 0.8)) * px * 1.15;
		scene = Tex.Sample(SL, i.uv + off).rgb;
		float3 mist = (Tex.Sample(SL, i.uv + off + px).rgb + Tex.Sample(SL, i.uv + off - px).rgb) * 0.5;
		scene = lerp(scene, mist, 0.22);
		col = scene + bloom * Params.y;
	} else {
		col += Mote(i.uv, gt, float2(0.02, 0.03), 18, 0.9, 0.1) * float3(0.75, 0.78, 0.85) * 0.7;
		vig = 1.45;
	}
	col += shaftAdd;
	float2 q = (i.uv - 0.5) * asp;
	col *= 1. - saturate(dot(q, q) * 1.15) * Extra.z * 0.28 * vig;
	float y = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lerp(y.xxx, col, 1.06);
	col *= grade;
	return float4(saturate(col), 1);
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
