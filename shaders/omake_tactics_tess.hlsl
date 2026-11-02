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

float3 Sway(float3 w, float amt)
{
	if (amt <= 0) return w;
	float t = Eye.w;
	float s = sin(t * 1.7 + w.x * 0.9 + w.z * 0.7) * 0.5 + sin(t * 2.9 + w.x * 1.7) * 0.25;
	w.x += s * amt * 0.07;
	w.z += cos(t * 1.3 + w.z * 1.1 + w.x * 0.3) * amt * 0.05;
	return w;
}

TC VST(VSIn v)
{
	TC o;
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
	o.w = w; o.n = n; o.uv = v.uv; o.c = v.c;
	return o;
}

float EdgeFactor(float3 a, float3 b)
{
	float d = distance((a + b) * 0.5, Eye.xyz);
	float tf = max(Q.y, 1);
	return max(lerp(tf, 1, saturate((d - 8) / 42)), 1);
}

TF HPC(InputPatch<TC, 3> p)
{
	TF o;
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
	if (Q.z > 0.001) {
		float h = fbm2(w.xz * 2.4 + w.y * 1.3) - 0.5;
		float h2 = fbm2(w.xz * 7.5) - 0.5;
		w += n * (h * 0.65 + h2 * 0.35) * Q.z * saturate(n.y * 0.65 + 0.35);
		if (n.y > 0.72 && Params.y < 0.5) {
			float th = FogColor.a;
			float freq = 3.1, amp = 0.55;
			if (th > 0.5 && th < 1.5) { freq = 1.5; amp = 0.9; }
			else if (th > 1.5 && th < 2.5) { freq = 4.2; amp = 0.4; }
			else if (th > 2.5 && th < 3.5) { freq = 6.0; amp = 0.32; }
			else if (th > 3.5 && th < 4.5) { freq = 2.4; amp = 0.7; }
			else if (th > 4.5 && th < 5.5) { freq = 2.1; amp = 1.05; }
			else if (th > 5.5 && th < 6.5) { freq = 3.6; amp = 0.48; }
			else if (th > 6.5 && th < 7.5) { freq = 2.8; amp = 0.5; }
			else if (th > 7.5 && th < 8.5) { freq = 2.6; amp = 0.28; }
			else if (th > 8.5) { freq = 1.8; amp = 0.62; }
			float g = fbm2(w.xz * freq + float2(0, Eye.w * 0.05)) - 0.5;
			w += n * g * Q.z * amp;
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
