// おまけタクティクス: 高画質と超綺麗のテッセレーション。
// 標準以下はバインドしない。高画質はフォン補間だけ。超綺麗は法線方向の変位を足す。
// フレーム定数は omake_tactics.hlsl の Frame / Obj と同じ並び。

cbuffer Frame : register(b0)
{
	row_major float4x4 VP;
	row_major float4x4 LightVP;
	float4 Eye;
	float4 LightDir;
	float4 Fog;
	float4 FogColor;
	float4 Screen;
	float4 SkyAmb;
	float4 GndAmb;
	float4 Q; // x=段 y=係数 z=変位 w=被写界
};
cbuffer Obj : register(b1)
{
	row_major float4x4 World;
	float4 Tint;
	float4 Params;
	float4 Extra;
	float4 OutlineColor;
};

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct TC { float3 w : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float4 c : COLOR0; };
struct PSIn { float4 pos : SV_POSITION; float3 w : TEXCOORD0; float3 n : TEXCOORD1; float2 uv : TEXCOORD2; float4 c : TEXCOORD3; };
struct TF { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };

float hash21(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float vnoise(float2 p)
{
	float2 i = floor(p), f = frac(p);
	float a = hash21(i), b = hash21(i + float2(1, 0)), c = hash21(i + float2(0, 1)), d = hash21(i + float2(1, 1));
	float2 u = f * f * (3. - 2. * f);
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float fbm2(float2 p) { float s = 0, a = .5; [unroll] for (int i = 0; i < 4; i++) { s += a * vnoise(p); p *= 2.03; a *= .5; } return s; }

float GustAmtT()
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

float3 FallLeanT(float3 w, float3 n, float amount)
{
	float g = GustAmtT();
	float hgt = saturate((w.y + 9.0) / 30.0);
	float2 dir = float2(cos(Eye.w * 0.23), sin(Eye.w * 0.23));
	w.x += dir.x * g * hgt * amount;
	w.z += dir.y * g * hgt * amount * 0.82;
	float flutter = sin(w.y * 1.85 + Eye.w * 3.4 + w.x * 0.45);
	w += n * flutter * (0.03 + g * 0.11) * saturate(amount * 1.4);
	return w;
}

float3 Sway(float3 w, float amt)
{
	if (amt <= 0.001) return w;
	float g = GustAmtT();
	float k = amt * (1.0 + g * 2.8);
	float t = Eye.w;
	float s = sin(t * 1.7 + w.x * 0.9 + w.z * 0.7) * 0.5 + sin(t * 2.9 + w.x * 1.7) * 0.25;
	w.x += s * k * 0.07 + cos(t * 0.23) * g * amt * 0.16;
	w.z += cos(t * 1.3 + w.z * 1.1 + w.x * 0.3) * k * 0.05 + sin(t * 0.23) * g * amt * 0.12;
	return w;
}

TC VST(VSIn v)
{
	TC o;
	float3 w = mul(float4(v.p, 1), World).xyz;
	float3 n = normalize(mul(v.n, (float3x3)World));
	w = Sway(w, (1. - v.c.a) * 4. + Extra.w);
	if (OutlineColor.a < 1.5 && Params.y > 0.5 && Params.y < 1.5) {
		if (n.y > 0.45) {
			float t = Eye.w;
			float th = FogColor.a;
			float amp = 0.045;
			if (th > 2.5 && th < 3.5) amp = 0.012;
			else if (th > 5.5 && th < 6.5) amp = 0.022;
			else if (th > 8.5 && th < 9.5) amp = 0.08;
			else if (th > 7.5 && th < 8.5) amp = 0.016;
			else if (th > 6.5) amp = 0.012;
			amp *= 1.0 + GustAmtT() * 1.8;
			float2 flow = v.c.rg * 2.0 - 1.0;
			float rush = saturate(v.c.b * 1.35);
			float spd = lerp(0.45, 2.6, rush);
			float2 xz = w.xz - flow * t * spd;
			w.y += sin(xz.x * 1.35 + xz.y * 0.34 - t * 1.15) * amp;
			w.y += sin(xz.x * -0.84 + xz.y * 2.4 - t * 1.7) * amp * 0.55;
			w.y += sin(xz.x * 0.82 - xz.y * 4.1 - t * 2.3) * amp * 0.28 * (1.0 + rush);
		} else {
			w = FallLeanT(w, n, 0.74);
			w.x += sin(w.y * 2.6 - Eye.w * 4.2) * (0.04 + GustAmtT() * 0.08);
		}
	} else if (OutlineColor.a < 1.5 && Params.y > 1.5 && Params.y < 2.5) {
		if (n.y > 0.45) {
			float t = Eye.w;
			w.y += sin(w.x * 0.7 + w.z * 0.55 + t * 0.55) * (0.018 + GustAmtT() * 0.016);
		} else {
			w = FallLeanT(w, n, 0.16);
			w.x += sin(w.y * 0.45 - Eye.w * 0.32 + w.z * 0.2) * 0.11;
			w.z += cos(w.y * 0.38 - Eye.w * 0.28) * 0.07;
			w.y -= 0.04 * sin(Eye.w * 0.4 + w.x);
		}
	}
	o.w = w; o.n = n; o.uv = v.uv; o.c = v.c;
	return o;
}

float EdgeFactor(float3 a, float3 b)
{
	float d = distance((a + b) * 0.5, Eye.xyz);
	float tf = max(Q.y, 1);
	return max(lerp(tf, 1, saturate((d - 8) / 42)), 1);
}

bool PatchOut(float4 a, float4 b, float4 c)
{
	if (a.w <= 0 && b.w <= 0 && c.w <= 0) return true;
	if (a.w <= 0 || b.w <= 0 || c.w <= 0) return false;
	float ax = a.x / a.w, ay = a.y / a.w, az = a.z / a.w;
	float bx = b.x / b.w, by = b.y / b.w, bz = b.z / b.w;
	float cx = c.x / c.w, cy = c.y / c.w, cz = c.z / c.w;
	const float m = 1.2;
	if (ax < -m && bx < -m && cx < -m) return true;
	if (ax > m && bx > m && cx > m) return true;
	if (ay < -m && by < -m && cy < -m) return true;
	if (ay > m && by > m && cy > m) return true;
	if (az < -0.05 && bz < -0.05 && cz < -0.05) return true;
	if (az > 1.05 && bz > 1.05 && cz > 1.05) return true;
	return false;
}

TF HPC(InputPatch<TC, 3> p)
{
	TF o;
	float4 c0 = mul(float4(p[0].w, 1), VP);
	float4 c1 = mul(float4(p[1].w, 1), VP);
	float4 c2 = mul(float4(p[2].w, 1), VP);
	if (PatchOut(c0, c1, c2)) {
		o.e[0] = o.e[1] = o.e[2] = o.i = 0;
		return o;
	}
	o.e[0] = EdgeFactor(p[1].w, p[2].w);
	o.e[1] = EdgeFactor(p[2].w, p[0].w);
	o.e[2] = EdgeFactor(p[0].w, p[1].w);
	o.i = (o.e[0] + o.e[1] + o.e[2]) * (1. / 3.);
	return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HPC")]
TC HST(InputPatch<TC, 3> p, uint i : SV_OutputControlPointID)
{
	return p[i];
}

[domain("tri")]
PSIn DST(TF factors, float3 b : SV_DomainLocation, const OutputPatch<TC, 3> p)
{
	float3 flat = p[0].w * b.x + p[1].w * b.y + p[2].w * b.z;
	float3 n0 = p[0].n, n1 = p[1].n, n2 = p[2].n;
	float3 q0 = flat - n0 * dot(flat - p[0].w, n0);
	float3 q1 = flat - n1 * dot(flat - p[1].w, n1);
	float3 q2 = flat - n2 * dot(flat - p[2].w, n2);
	float3 curved = q0 * b.x + q1 * b.y + q2 * b.z;
	float ph = (Q.z > 0.001) ? 0.7 : 0.42;
	if (Params.y > 0.5) ph *= 0.3;
	float3 w = lerp(flat, curved, ph);
	float3 n = normalize(n0 * b.x + n1 * b.y + n2 * b.z);
	if (OutlineColor.a < 1.5 && Params.y > 0.5 && Params.y < 1.5 && n.y < 0.45) {
		w = FallLeanT(w, n, 0.74);
		w.x += sin(w.y * 2.6 - Eye.w * 4.2) * (0.04 + GustAmtT() * 0.08);
	} else if (OutlineColor.a < 1.5 && Params.y > 1.5 && Params.y < 2.5 && n.y < 0.45) {
		w = FallLeanT(w, n, 0.16);
		w.x += sin(w.y * 0.45 - Eye.w * 0.32 + w.z * 0.2) * 0.11;
		w.z += cos(w.y * 0.38 - Eye.w * 0.28) * 0.07;
		w.y -= 0.04 * sin(Eye.w * 0.4 + w.x);
	}
	if (Q.z > 0.001) {
		float h = fbm2(w.xz * 2.4 + w.y * 1.3) - 0.5;
		float h2 = fbm2(w.xz * 7.5) - 0.5;
		w += n * (h * 0.65 + h2 * 0.35) * Q.z * saturate(n.y * 0.65 + 0.35);
		if (n.y > 0.72 && Params.y < 0.5) {
			float th = FogColor.a;
			float freq = 3.1, amp = 0.55, drift = 0.05;
			if (th < 0.5) { freq = 4.4; amp = 0.42; drift = 0.12; }
			else if (th < 1.5) { freq = 1.5; amp = 0.9; drift = 0.04; }
			else if (th < 2.5) { freq = 4.2; amp = 0.4; drift = 0.03; }
			else if (th < 3.5) { freq = 6.0; amp = 0.32; drift = 0.08; }
			else if (th < 4.5) { freq = 2.4; amp = 0.7; drift = 0.14; }
			else if (th < 5.5) { freq = 2.1; amp = 1.05; drift = 0.06; }
			else if (th < 6.5) { freq = 3.6; amp = 0.48; drift = 0.04; }
			else if (th < 7.5) { freq = 2.8; amp = 0.5; drift = 0.02; }
			else if (th < 8.5) { freq = 2.6; amp = 0.28; drift = 0.07; }
			else if (th < 9.5) { freq = 1.8; amp = 0.62; drift = 0.16; }
			else if (th < 10.5) { freq = 5.2; amp = 0.22; drift = 0.01; }
			else if (th < 11.5) { freq = 7.0; amp = 0.18; drift = 0.0; }
			else if (th < 12.5) { freq = 3.4; amp = 0.38; drift = 0.02; }
			else { freq = 2.2; amp = 0.45; drift = 0.05; }
			float g = fbm2(w.xz * freq + float2(Eye.w * drift, Eye.w * drift * 0.6)) - 0.5;
			w += n * g * Q.z * amp;
			float g2 = fbm2(w.xz * freq * 2.4 + Eye.w * drift * 1.6) - 0.5;
			w += n * g2 * Q.z * amp * 0.35;
		}
	}
	PSIn o;
	o.w = w;
	o.n = n;
	o.uv = p[0].uv * b.x + p[1].uv * b.y + p[2].uv * b.z;
	o.c = p[0].c * b.x + p[1].c * b.y + p[2].c * b.z;
	o.pos = mul(float4(w, 1), VP);
	return o;
}
