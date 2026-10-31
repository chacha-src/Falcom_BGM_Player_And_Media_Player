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
// 液体の上にいるユニット。xyz=位置、w=強さ。0 なら波紋なし。
cbuffer Liquid : register(b3)
{
	float4 Rip[4];
};

float RippleH(float2 xz, float t, out float2 dh)
{
	float h = 0;
	dh = 0;
	[unroll] for (int i = 0; i < 4; i++) {
		float s = Rip[i].w;
		if (s < 0.01) continue;
		float2 d = xz - Rip[i].xz;
		float dist = length(d);
		float amp = s * exp(-dist * 2.15);
		float ph = dist * 16.0 - t * 7.4;
		h += sin(ph) * amp * 0.055;
		dh += d / max(dist, 0.05) * cos(ph) * amp * 0.55;
	}
	return h;
}

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
	if (Params.y > 0.5 && Params.y < 1.5 && n.y > 0.45) {
		float t = Eye.w;
		float th = FogColor.a;
		float amp = 0.045;
		if (th > 2.5 && th < 3.5) amp = 0.012;
		else if (th > 5.5 && th < 6.5) amp = 0.022;
		else if (th > 8.5 && th < 9.5) amp = 0.08;
		else if (th > 7.5 && th < 8.5) amp = 0.016;
		else if (th > 6.5) amp = 0.01;
		float2 flow = v.c.rg * 2.0 - 1.0;
		float rush = saturate(v.c.b * 1.35);
		float spd = lerp(0.7, 2.8, rush);
		float along = dot(w.xz, flow);
		w.y += sin(w.x * 1.6 + w.z * 0.7 + t * 1.25) * amp + cos(w.z * 2.0 - t * 0.8) * amp * 0.7;
		w.y += sin(along * 3.1 - t * spd * 2.4) * amp * (0.85 + rush * 2.4);
		w.y += sin(along * 7.4 - t * spd * 5.1) * amp * rush * 1.15;
		float2 rdh;
		w.y += RippleH(w.xz, t, rdh);
	} else if (Params.y > 1.5 && Params.y < 2.5) {
		float t = Eye.w;
		float2 flow = v.c.rg * 2.0 - 1.0;
		w.y += sin(dot(w.xz, flow) * 1.6 - t * 0.9) * 0.028;
		float2 rdh;
		w.y += RippleH(w.xz, t, rdh) * 0.7;
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
	const float2 k[16] = {
		float2(-0.326212, -0.405810), float2(-0.840144, -0.073580), float2(-0.695914, 0.457137),
		float2(-0.203345, 0.620716), float2(0.962340, -0.194983), float2(0.473434, -0.480026),
		float2(0.519456, 0.767022), float2(0.185461, -0.893124), float2(0.507431, 0.064425),
		float2(0.896420, 0.412458), float2(-0.321940, -0.932615), float2(-0.791559, -0.597710),
		float2(0.089842, 0.323164), float2(-0.461230, 0.218612), float2(0.248110, 0.553812),
		float2(-0.118392, -0.210418)
	};
	// 軽量と標準は12。高は半径を絞る。超綺麗は16。最高は24、限界突破は32。超軽量は1。
	int taps = 12;
	float rad = 2.2;
	if (Q.x > 3.5 && Q.x < 4.5) { taps = 1; rad = 0; }
	else if (Q.x > 5.5) { taps = 16; rad = 0.72; }
	else if (Q.x > 4.5) { taps = 16; rad = 0.9; }
	else if (Q.x > 2.5) { taps = 16; rad = 1.05; }
	else if (Q.x > 1.5) { taps = 12; rad = 1.5; }
	float t = Fog.w * rad;
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
	} else {
		float spec = pow(ndh, lerp(16., 72., dew)) * ndl;
		float wet = pow(1. - ndv, lerp(3.6, 2.1, dew));
		col += float3(1, 0.97, 0.92) * spec * dew * coat * 0.24;
		col += lerp(albedo, float3(1, 0.95, 0.9), 0.3) * wet * dew * coat * 0.2;
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

float3 FogAt(float3 col, float3 w)
{
	float d = length(Eye.xyz - w);
	float fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
	fg = saturate(fg + saturate(-w.y * Fog.z));
	return lerp(col, FogColor.rgb, fg * fg * 0.9);
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
	float3 n = normalize(i.n);
	n = DetailN(n, i.w);
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
	float rimK = 1;
	if (Q.x > 3.5 && Q.x < 4.5) rimK = 0;
	else if (Q.x < 0.5) rimK = 1;
	else if (Q.x > 5.5) rimK = 1.7;
	else if (Q.x > 4.5) rimK = 1.5;
	else if (Q.x > 2.5) rimK = 1.35;
	else if (Q.x > 1.5) rimK = 1.12;
	col += rim * rimK * float3(1, 0.95, 0.85) * (0.35 + 0.65 * sh);
	col += emis;
	float hair = (Params.y > 6.5 && Params.y < 7.5) ? 1 : 0;
	col = DewShade(col, albedo, n, v, sh, skin, hair);
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

float WaveAmp(float th)
{
	float amp = 0.16;
	if (th > 2.5 && th < 3.5) amp = 0.04;
	else if (th > 5.5 && th < 6.5) amp = 0.07;
	else if (th > 0.5 && th < 1.5) amp = 0.1;
	else if (th > 4.5 && th < 5.5) amp = 0.13;
	else if (th > 8.5 && th < 9.5) amp = 0.22;
	else if (th > 7.5 && th < 8.5) amp = 0.05;
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

GOut FallSheet(PSIn i, float thw, float3 albedo, float alpha)
{
	float t = Eye.w;
	float2 flow = float2(i.w.x * 7.0 + i.w.z * 2.2, i.w.y * -3.1 + t * 2.6);
	float streak = pow(saturate(sin(flow.x) * 0.5 + 0.5), 2);
	float band = pow(saturate(sin(flow.y * 2.7) * 0.5 + 0.5), 5);
	float3 body = lerp(albedo * 0.45, float3(0.78, 0.92, 1), 0.62 + streak * 0.3);
	if (thw > 4.5 && thw < 5.5) body = lerp(body, float3(1, 0.42, 0.1), 0.4);
	else if (thw > 5.5 && thw < 6.5) body = lerp(body, float3(0.4, 0.62, 0.32), 0.28);
	else if (thw > 2.5 && thw < 3.5) body = lerp(body, float3(0.86, 0.94, 1), 0.22);
	else if (thw > 7.5 && thw < 8.5) body = lerp(body, float3(0.12, 0.4, 0.5), 0.5);
	body += float3(1, 1, 1) * band * 0.5;
	float mist = saturate(0.85 - i.w.y * 0.22);
	body = lerp(body, float3(0.92, 0.96, 1), mist * 0.35);
	float a = saturate(alpha * (0.58 + streak * 0.32));
	return Emit(float4(FogAt(body, i.w), a), i.w);
}

GOut PSWater(PSIn i)
{
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
	else if (thw > 8.5 && thw < 9.5) {
		float rush = pow(saturate(sin(i.w.z * 2.4 - t * 3.4) * 0.5 + 0.5), 2);
		col = lerp(col, float3(0.9, 0.97, 1), rush * 0.45);
		see = 0.26;
	} else if (thw > 7.5 && thw < 8.5) {
		col = lerp(col, float3(0.04, 0.22, 0.32), 0.5);
		col += float3(0.45, 0.85, 1) * pow(caus, 2) * 0.4;
		see = 0.58;
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
	float3 hot = lerp(float3(1, 0.72, 0.18), float3(1, 0.22, 0.02), saturate(vg.y * 8));
	float3 col = lerp(float3(0.12, 0.035, 0.02), hot * (1.5 * pulse), crack);
	float3 H = normalize(normalize(LightDir.xyz) + v);
	col += hot * pow(saturate(dot(float3(0, 1, 0), H)), 16) * crack * 0.35;
	return col;
}

GOut PSLava(PSIn i)
{
	float3 v = normalize(Eye.xyz - i.w);
	float t = Eye.w;
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
	return Emit(float4(col, 1), i.w);
}

GOut GroundPS(PSIn i, float theme)
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
		float2 rdh;
		RippleH(i.w.xz, Eye.w, rdh);
		uv += (dh + rdh) * 0.16;
		float2 c1 = i.w.xz * 0.8 + Eye.w * float2(0.15, 0.05);
		float2 c2 = i.w.xz * 1.3 - Eye.w * float2(0.07, 0.12);
		bedCaus = pow(saturate(sin(c1.x * 6.2) * sin(c1.y * 5.4) + sin(c2.x * 4.8) * sin(c2.y * 5.1)), 2);
	}
	// 配列は 0..7 がアルベド、8..15 が法線。海底と滝は既存スライスを染める。
	float texTheme = theme;
	if (theme > 7.5 && theme < 8.5) texTheme = 4;
	else if (theme > 8.5) texTheme = 1;
	float4 alb = Ground.Sample(SL, float3(uv, texTheme));
	float4 nr = Ground.Sample(SL, float3(uv, texTheme + 8));
	float3 albedo = alb.rgb * lerp(0.78, 1, alb.a) * i.c.rgb;
	if (cliff > 0.5) albedo *= 0.8;
	float3 nLit = DetailN(Bump(n, nr.rgb), i.w);
	float rough = 0.7;
	float3 glow = 0;
	float3 f0 = float3(0.035, 0.035, 0.035);
	if (theme < 0.5) {
		rough = 0.82;
		float3 gdir = float3(0.85, 0, 0.52);
		glow += float3(0.75, 0.9, 0.55) * Aniso(gdir, normalize(normalize(LightDir.xyz) + v), saturate(dot(nLit, normalize(normalize(LightDir.xyz) + v))), 14) * 0.04;
		float wind = 0.5 + 0.5 * sin(i.w.x * 0.42 + i.w.z * 0.16 - Eye.w * 1.55);
		float gust = pow(saturate(sin(i.w.x * 0.22 - Eye.w * 0.85)), 3);
		albedo *= 0.9 + wind * 0.16;
		glow += float3(0.95, 1.0, 0.62) * gust * 0.07 * (1 - cliff);
	} else if (theme < 1.5) {
		rough = 0.58;
		if (cliff > 0.5) {
			float groove = 1 - smoothstep(0.012, 0.05, abs(frac(i.w.y * 1.15) - 0.5));
			albedo *= 1 - groove * 0.5;
			nLit = normalize(nLit + float3(0, cos(i.w.y * 7.2) * 0.4, 0));
		}
		float dust = fbm2(i.w.xz * 0.12 + float2(Eye.w * 0.03, Eye.w * 0.01));
		albedo = lerp(albedo, albedo * float3(1.22, 0.86, 0.62), dust * 0.22);
		glow += float3(0.85, 0.62, 0.32) * pow(dust, 2) * 0.08;
	} else if (theme < 2.5) {
		rough = 0.38;
		f0 = float3(0.05, 0.055, 0.045);
		float dapple = pow(saturate(sin(i.w.x * 0.85 + Eye.w * 0.35) * sin(i.w.z * 0.7 - Eye.w * 0.22)), 2);
		glow += float3(1.0, 0.92, 0.45) * dapple * 0.16;
		albedo *= 0.92 + dapple * 0.12;
	} else if (theme < 3.5) {
		rough = 0.22;
		f0 = float3(0.06, 0.065, 0.07);
		float spark = step(0.96, hash21(floor(i.w.xz * 14 + i.w.y))) * pow(saturate(dot(nLit, normalize(normalize(LightDir.xyz) + v))), 28);
		glow += float3(0.9, 0.96, 1) * spark * 0.38;
		float drift = pow(0.5 + 0.5 * sin(i.w.x * 0.7 + Eye.w * 0.6), 4);
		glow += float3(0.8, 0.9, 1) * drift * 0.08;
		albedo = lerp(albedo, float3(0.92, 0.95, 1), 0.08);
	} else if (theme < 4.5) {
		rough = 0.74;
		nLit = normalize(nLit + float3(cos(i.w.x * 1.7 + Eye.w * 0.6) * 0.16, 0.04, sin(Eye.w * 1.1 + i.w.z) * 0.1));
		float sand = pow(0.5 + 0.5 * sin(i.w.x * 0.8 + i.w.z * 0.25 - Eye.w * 1.8), 3);
		albedo = lerp(albedo, albedo * float3(1.25, 1.05, 0.7), sand * 0.22);
		glow += float3(1, 0.82, 0.4) * sand * 0.06;
	} else if (theme < 5.5) {
		rough = 0.55;
		float2 vg = VoronoiGap(i.w.xz * 0.55 + float2(Eye.w * 0.03, Eye.w * 0.012));
		float crack = 1 - smoothstep(0.02, 0.09, vg.y);
		albedo *= 1 - crack * 0.35;
		glow += float3(1.0, 0.28, 0.02) * crack * (cliff > 0.5 ? 0.28 : 0.55);
		float pulse = 0.75 + 0.25 * sin(Eye.w * 2.2 + vg.x * 6);
		glow *= pulse;
	} else if (theme < 6.5) {
		rough = 0.28;
		f0 = float3(0.05, 0.06, 0.04);
		float2 c1 = i.w.xz * 0.31 + Eye.w * float2(0.05, 0.02);
		float2 c2 = i.w.xz * 0.47 - Eye.w * float2(0.03, 0.06);
		float caus = saturate(sin(c1.x * 9.1) * sin(c1.y * 8.3) + sin(c2.x * 7.4) * sin(c2.y * 6.2));
		glow += float3(0.35, 1.0, 0.45) * pow(caus, 2) * 0.2;
		albedo = lerp(albedo, albedo * float3(0.7, 0.85, 0.55), 0.1);
	} else if (theme < 7.5) {
		rough = 0.46;
		f0 = float3(0.05, 0.05, 0.055);
		float2 vg = VoronoiGap(i.w.xz * 0.42);
		float moss = 1 - smoothstep(0.015, 0.07, vg.y);
		albedo = lerp(albedo, albedo * float3(0.45, 0.72, 0.4), moss * 0.32);
		float rune = pow(0.5 + 0.5 * sin(i.w.x * 1.3 + Eye.w * 0.7), 8) * pow(0.5 + 0.5 * sin(i.w.z * 1.1 - Eye.w * 0.4), 8);
		glow += float3(0.45, 0.7, 1) * rune * (1 - cliff) * 0.38;
		glow += float3(0.7, 0.85, 1) * step(0.97, hash21(floor(i.w.xz * 3))) * (0.4 + 0.3 * sin(Eye.w * 2));
	} else if (theme < 8.5) {
		rough = 0.3;
		f0 = float3(0.05, 0.07, 0.09);
		albedo *= lerp(float3(0.62, 0.82, 0.74), float3(0.45, 0.7, 0.88), 0.4);
		float rip = 0.5 + 0.5 * sin(i.w.x * 2.6 + i.w.z * 0.5 + Eye.w * 0.4);
		albedo *= 0.82 + rip * 0.2;
		float2 c1 = i.w.xz * 0.55 + Eye.w * float2(0.08, 0.03);
		float2 c2 = i.w.xz * 0.9 - Eye.w * float2(0.04, 0.07);
		float caus = saturate(sin(c1.x * 8.2) * sin(c1.y * 7.1) + sin(c2.x * 5.6) * sin(c2.y * 6.4));
		glow += float3(0.35, 0.85, 1) * pow(caus, 2) * 0.35 * saturate(n.y);
		float ray = pow(saturate(0.5 + 0.5 * sin(i.w.x * 0.35 + i.w.z * 0.12 + Eye.w * 0.35)), 5);
		ray *= pow(saturate(0.5 + 0.5 * sin(i.w.z * 0.22 - Eye.w * 0.18)), 3);
		glow += float3(0.45, 0.85, 1) * ray * saturate(n.y) * 0.42;
	} else {
		rough = 0.42;
		f0 = float3(0.06, 0.065, 0.07);
		albedo *= float3(0.78, 0.86, 0.9);
		float spray = pow(saturate(sin(i.w.x * 1.4 + Eye.w * 2.2) * sin(i.w.z * 0.9 - Eye.w * 1.4)), 2);
		glow += float3(0.85, 0.95, 1) * spray * 0.26;
		float sheet = pow(0.5 + 0.5 * sin(i.w.z * 0.8 - Eye.w * 2.6), 3);
		glow += float3(0.7, 0.88, 1) * sheet * 0.12;
		albedo = lerp(albedo, float3(0.75, 0.86, 0.92), spray * 0.16);
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
		else if (theme > 7.5 && theme < 8.5) causCol = float3(0.35, 0.75, 1);
		else if (theme > 8.5) causCol = float3(0.85, 0.95, 1);
		col += causCol * bedCaus * 0.35;
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
		float micro = fbm2(i.w.xz * (Q.x > 5.5 ? 14. : 8.));
		col *= 0.94 + micro * (Q.x > 5.5 ? 0.12 : 0.07);
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

GOut PSProp(PSIn i)
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
	nLit = DetailN(nLit, i.w);
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	return Emit(float4(FogAt(col, i.w), Tint.a), i.w);
}

GOut PSO(PSIn i)
{
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		clip(nz - (Extra.x * 1.1 - 0.05));
	}
	return Emit(float4(lerp(OutlineColor.rgb, float3(1, 1, 1), Extra.y), OutlineColor.a * Tint.a), i.w);
}

GOut PSOV(PSIn i)
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
	return EmitZ(float4(c, 1), 1);
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
	if (th < 0) shaftK = 0;
	else if (th < 0.5) { grade = float3(1.02, 1.03, 0.97); shaftK = 0.85; }
	else if (th < 1.5) { grade = float3(1.04, 0.98, 0.92); shaftK = 0.45; }
	else if (th < 2.5) { grade = float3(0.97, 1.03, 0.97); shaftK = 0.7; }
	else if (th < 3.5) { grade = float3(0.98, 0.99, 1.03); shaftK = 0.35; }
	else if (th < 4.5) { grade = float3(1.04, 1.0, 0.92); shaftK = 0.75; }
	else if (th < 5.5) { grade = float3(1.03, 0.95, 0.9); shaftK = 0.25; }
	else if (th < 6.5) { grade = float3(0.97, 1.02, 0.95); shaftK = 0.3; }
	else if (th < 7.5) { grade = float3(0.97, 0.98, 1.03); shaftK = 0.55; }
	else if (th < 8.5) { grade = float3(0.92, 0.98, 1.03); shaftK = 0.8; }
	else { grade = float3(0.98, 0.99, 1.02); shaftK = 0.9; }
	// 筋は太陽が空にあるとき、空の側だけ。盤面へ放射状に広げない。
	float sky = saturate((0.46 - i.uv.y) * 2.6);
	float sunUp = saturate((0.4 - suv.y) * 3.2);
	float3 shaftAdd = shaft / max(wsum, 1e-3) * Extra.w * saturate(0.7 - dist) * 0.26 * shaftK * sky * sunUp;
	float gt = Eye.w;
	float2 px = Screen.zw;
	float vig = 1;
	float skyM = saturate((0.34 - i.uv.y) * 2.4);
	// 0平原 1峡谷 2森 3雪 4砂漠 5火山 6沼 7廃墟 8海底 9滝。画面はずらさない。粒は空側だけ。
	if (th < 0) {
	} else if (th < 0.5) {
		col += Mote(i.uv, gt, float2(0.55, 0.04), 22, 0.9, 0.07) * float3(1, 0.95, 0.55) * 0.45 * skyM;
	} else if (th < 1.5) {
		col += Mote(i.uv, gt, float2(0.35, -0.12), 20, 0.9, 0.07) * float3(0.9, 0.62, 0.32) * 0.4 * skyM;
	} else if (th < 2.5) {
		col += Mote(i.uv, gt, float2(0.08, 0.35), 24, 0.92, 0.06) * float3(0.75, 1, 0.45) * 0.4 * skyM;
	} else if (th < 3.5) {
		col += Mote(i.uv, gt, float2(0.15, 0.9), 26, 0.9, 0.05) * float3(0.95, 0.97, 1) * 0.55 * skyM;
	} else if (th < 4.5) {
		col += Mote(i.uv, gt, float2(0.7, 0.04), 20, 0.9, 0.06) * float3(1, 0.82, 0.4) * 0.4 * skyM;
	} else if (th < 5.5) {
		col += Mote(i.uv, gt, float2(0.08, -0.7), 22, 0.9, 0.06) * float3(1, 0.4, 0.08) * 0.5 * skyM;
	} else if (th < 6.5) {
		col += Mote(i.uv, gt, float2(0.04, -0.18), 26, 0.93, 0.05) * float3(0.75, 1, 0.4) * 0.4 * skyM;
	} else if (th < 7.5) {
		col += Mote(i.uv, gt, float2(0.02, -0.08), 18, 0.92, 0.06) * float3(0.65, 0.78, 1) * 0.45 * skyM;
	} else if (th < 8.5) {
		float ray = pow(0.5 + 0.5 * sin(i.uv.x * 40. + gt * 0.6), 8);
		col += float3(0.45, 0.8, 1) * ray * skyM * 0.08;
		col += Mote(i.uv, gt, float2(0.02, -0.4), 18, 0.92, 0.05) * float3(0.6, 0.9, 1) * 0.35 * skyM;
	} else {
		col += Mote(i.uv, gt, float2(0.04, 0.55), 20, 0.91, 0.05) * float3(0.9, 0.96, 1) * 0.4 * skyM;
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
		float spread = (Q.x > 5.5) ? 3.4 : ((Q.x > 4.5) ? 2.8 : 2.2);
		float mixk = (Q.x > 5.5) ? 0.34 : ((Q.x > 4.5) ? 0.28 : 0.22);
		float2 ca = (i.uv - 0.5) * px * spread;
		float3 fr = float3(Tex.Sample(SL, i.uv + ca).r, col.g, Tex.Sample(SL, i.uv - ca).b);
		col = lerp(col, fr, mixk);
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
	return float4(saturate(col), 1);
}

float4 PSGradeTiny(QOut i) : SV_Target
{
	float3 scene = Tex.Sample(SL, i.uv).rgb;
	float y = dot(scene, float3(0.25, 0.5, 0.25));
	scene = lerp(y.xxx, scene, 1.04);
	float2 q = (i.uv - 0.5) * float2(Screen.x * Screen.w, 1);
	scene *= 1. - saturate(dot(q, q) * 0.35) * 0.08;
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
