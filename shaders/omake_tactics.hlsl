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
SamplerState SL : register(s0);
SamplerComparisonState SC : register(s1);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct VKIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; uint4 bi : BLENDINDICES; float4 bw : BLENDWEIGHT; };
struct PSIn { float4 pos : SV_POSITION; float3 w : TEXCOORD0; float3 n : TEXCOORD1; float2 uv : TEXCOORD2; float4 c : TEXCOORD3; };

float hash21(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
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
	w += n * Params.x;
	o.w = w; o.n = n; o.uv = v.uv; o.c = float4(v.c.rgb, 1);
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
	float t = Fog.w;
	float s = 0;
	[unroll] for (int y = -1; y <= 1; y++)
		[unroll] for (int x = -1; x <= 1; x++)
			s += ShadowMap.SampleCmpLevelZero(SC, uv + float2(x, y) * t * 1.3, z);
	return s / 9.;
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

float3 ToonLight(float3 albedo, float3 n, float3 w, float sh)
{
	float3 l = normalize(LightDir.xyz);
	float ndl = dot(n, l);
	float hard = GndAmb.w;
	float d = smoothstep(-0.02, 0.02 + 0.25 * (1. - hard), ndl) * sh;
	float3 amb = lerp(GndAmb.rgb, SkyAmb.rgb, saturate(n.y * .5 + .5)) * SkyAmb.w;
	float3 sun = float3(1.0, 0.96, 0.88);
	// 光と反対の面は環境光だけだと黒に落ちる。マントの外側など、影側にも布の色を残す。
	float bounce = saturate(-ndl) * 0.85;
	return albedo * (amb * (1.0 + bounce) + sun * d * 0.82 + sun * saturate(ndl) * sh * 0.18 + sun * bounce * 0.38);
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
	float mode = Params.y;
	float alpha = Tint.a;
	float3 emis = albedo * Params.z;
	if (mode > 0.5 && mode < 1.5) {
		// 水面
		float t = Eye.w;
		float2 q = i.w.xz;
		float wv = fbm2(q * 1.7 + float2(t * 0.25, t * 0.18)) + fbm2(q * 3.1 - float2(t * 0.2, -t * 0.31)) * 0.5;
		float3 wn = normalize(float3((wv - 0.75) * 0.6, 1, (fbm2(q * 2.3 + t * 0.2) - 0.5) * 0.6));
		float fr = pow(1. - saturate(dot(wn, v)), 3.);
		float3 deep = albedo * 0.55, shallow = albedo * 1.25;
		float3 col = lerp(deep, shallow, wv * 0.6);
		float3 h = normalize(normalize(LightDir.xyz) + v);
		float sp = pow(saturate(dot(wn, h)), 90.) * 1.6;
		col += SkyAmb.rgb * fr * 0.6 + sp;
		float foam = smoothstep(0.82, 0.95, wv);
		col = lerp(col, float3(0.9, 0.95, 1), foam * 0.35);
		float murk = saturate(albedo.g - albedo.b);
		float bub = smoothstep(0.78, 0.93, fbm2(q * 4.6 + float2(0, t * 0.35)));
		col = lerp(col, col * float3(0.62, 0.78, 0.48), murk * 0.55);
		col += float3(0.75, 0.9, 0.65) * bub * murk * 0.35;
		float caust = fbm2(q * 6.0 + float2(t * 0.15, -t * 0.1));
		col += albedo * caust * (0.12 + murk * 0.08);
		float d = length(Eye.xyz - i.w), fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
		return float4(lerp(col, FogColor.rgb, fg * fg), lerp(0.72, 0.92, fr) * alpha);
	}
	if (mode > 1.5 && mode < 2.5) {
		// 溶岩
		float t = Eye.w;
		float2 q = i.w.xz * 0.9;
		float f = fbm2(q + float2(t * 0.12, t * 0.07));
		float g = fbm2(q * 2.7 - float2(t * 0.21, 0));
		float crack = smoothstep(0.45, 0.62, abs(f - g) * 2.4);
		float3 hot = lerp(float3(1, 0.85, 0.3), float3(1, 0.3, 0.05), f);
		float vein = smoothstep(0.52, 0.74, fbm2(q * 1.25 + float2(t * 0.16, t * 0.05)));
		float3 col = lerp(hot * (1.6 + 0.4 * sin(t * 2.0 + f * 9.)), float3(0.18, 0.05, 0.03), crack * 0.8);
		col += float3(1.0, 0.45, 0.08) * vein * (1. - crack) * 0.85;
		return float4(col, 1);
	}
	float sh = ShadowAt(i.w, n);
	float3 col = ToonLight(albedo, n, i.w, sh);
	if (mode > 2.5 && mode < 3.5 && n.y > 0.8) {
		// 地形の升目線
		float2 g = abs(frac(i.w.xz) - 0.5);
		float e = smoothstep(0.455, 0.5, max(g.x, g.y));
		col *= 1. - e * 0.28;
		float grain = fbm2(i.w.xz * 3.4) - 0.5;
		float patch = fbm2(i.w.xz * 0.33 + 3.7);
		col *= 0.9 + grain * 0.1 + patch * 0.12;
		col = lerp(col, col * float3(0.78, 0.92, 0.7), saturate(patch - 0.42) * 0.4);
		col *= 1. - saturate(1. - n.y) * 0.15;
	}
	float rim = pow(1. - saturate(dot(n, v)), 3.) * Extra.z;
	col += rim * float3(1, 0.95, 0.85) * (0.4 + 0.6 * sh);
	col += emis;
	float d = length(Eye.xyz - i.w);
	float fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
	fg = saturate(fg + saturate(-i.w.y * Fog.z));
	col = lerp(col, FogColor.rgb, fg * fg * 0.9);
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		float cut = Extra.x * 1.1 - 0.05;
		clip(nz - cut);
		col += float3(1, 0.6, 0.25) * smoothstep(0.08, 0., nz - cut) * 2.;
	}
	col = lerp(col, float3(1, 1, 1), Extra.y);
	return float4(col, alpha);
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
	return float4(i.c.rgb * a * i.c.a, a * i.c.a);
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
		float cl = fbm2(float2(i.uv.x * 3. + t * 0.01, i.uv.y * 6.)) * smoothstep(0.75, 0.1, y);
		c = lerp(c, float3(1, 1, 1), cl * 0.25);
	}
	return float4(c, 1);
}
