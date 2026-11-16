// おまけタクティクス: 被写界深度と接触陰影。
// PSDofH は高画質（遠いところだけ、8 タップ）。PSDofU は超綺麗（近い側も、16 タップ）。
// ピント付近は元の色をそのまま返す。ループ回数は段ごとに固定。

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
	float4 Q;
};
cbuffer Obj : register(b1)
{
	row_major float4x4 World;
	float4 Tint;
	float4 Params; // z=ぼかし半径(px) w=ピント(線形深度)
	float4 Extra;
	float4 OutlineColor;
};

Texture2D Tex : register(t0);
Texture2D Depth : register(t1);
SamplerState SL : register(s0);

struct QOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

static const float2 k8[8] = {
	float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
	float2(0.7, 0.7), float2(-0.7, 0.7), float2(0.7, -0.7), float2(-0.7, -0.7)
};
static const float2 k16[16] = {
	float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
	float2(0.707, 0.707), float2(-0.707, 0.707), float2(0.707, -0.707), float2(-0.707, -0.707),
	float2(0.35, 0.94), float2(-0.35, 0.94), float2(0.94, 0.35), float2(-0.94, 0.35),
	float2(0.35, -0.94), float2(-0.35, -0.94), float2(0.94, -0.35), float2(-0.94, -0.35)
};

float DofHash(float2 p)
{
	return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float Ao4(float2 uv, float z, float reach)
{
	float ang = DofHash(floor(uv * Screen.xy)) * 6.2831853;
	float cs = cos(ang), sn = sin(ang);
	float ao = 0;
	[unroll] for (int i = 0; i < 8; i++) {
		float2 d = float2(k8[i].x * cs - k8[i].y * sn, k8[i].x * sn + k8[i].y * cs);
		float zs = Depth.SampleLevel(SL, uv + d * Screen.zw * reach, 0).r;
		float dz = z - zs;
		ao += saturate(dz * 44. - 0.015) * saturate(1.0 - dz * 3.5);
	}
	return ao * 0.125;
}

float Ao12(float2 uv, float z, float reach)
{
	float ang = DofHash(floor(uv * Screen.xy)) * 6.2831853;
	float cs = cos(ang), sn = sin(ang);
	float ao = 0;
	[unroll] for (int i = 0; i < 12; i++) {
		float2 d = float2(k16[i].x * cs - k16[i].y * sn, k16[i].x * sn + k16[i].y * cs);
		float zs = Depth.SampleLevel(SL, uv + d * Screen.zw * reach, 0).r;
		float dz = z - zs;
		ao += saturate(dz * 38. - 0.012) * saturate(1.0 - dz * 3.0);
	}
	return ao * (1. / 12.);
}

float ContactShadow(float2 uv, float z, int steps)
{
	if (LightDir.w < 0.5 || z <= 0.005 || z >= 0.96) return 1.0;
	float3 ld = normalize(LightDir.xyz);
	float3 lp = Eye.xyz + ld * 10.0;
	float4 sp = mul(float4(lp, 1), VP);
	float2 suv = (sp.w > 0.05) ? (sp.xy / sp.w * float2(0.5, -0.5) + 0.5) : float2(0.5, 0.15);
	float2 dir = suv - uv;
	float dl = length(dir);
	if (dl < 1e-4) return 1.0;
	dir /= dl;
	float occ = 0.0;
	float j = 0.65 + 0.35 * DofHash(floor(uv * Screen.xy) + 11.3);
	[loop] for (int s = 1; s <= steps; s++) {
		float t = (float)s / (float)steps;
		float2 u = uv + dir * Screen.zw * (t * 11.0 * j);
		float zs = Depth.SampleLevel(SL, u, 0).r;
		float dz = z - zs;
		if (dz > 0.0025 && dz < 0.045) occ = max(occ, saturate(1.0 - dz * 22.0) * (1.0 - t * 0.4));
	}
	return 1.0 - occ * 0.38;
}

float3 ReflectTap(float2 uv, float z)
{
	if (z <= 0.005 || z >= 0.96) return 0;
	float zr = Depth.SampleLevel(SL, uv + float2(Screen.z * 2.0, 0), 0).r;
	float zl = Depth.SampleLevel(SL, uv - float2(Screen.z * 2.0, 0), 0).r;
	float zu = Depth.SampleLevel(SL, uv + float2(0, Screen.w * 2.0), 0).r;
	float zd = Depth.SampleLevel(SL, uv - float2(0, Screen.w * 2.0), 0).r;
	float2 grad = float2(zr - zl, zu - zd);
	float2 dir = normalize(float2(-grad.x * 16.0 + (uv.x - 0.5) * 0.35, -0.85 - grad.y * 10.0));
	float3 hit = 0;
	float w = 0;
	float2 step = float2(dir.x * Screen.z * 7.5, dir.y * Screen.w * 9.0);
	[unroll] for (int i = 2; i <= 7; i++) {
		float2 u = uv + step * i;
		if (any(u < 0.01) || any(u > 0.99)) break;
		float zs = Depth.SampleLevel(SL, u, 0).r;
		if (zs > 0.015 && z - zs > 0.012 && z - zs < 0.24) {
			hit = Tex.SampleLevel(SL, u, 0).rgb;
			w = saturate((0.24 - (z - zs)) * 4.5) * (1.0 - (float)(i - 2) * 0.09);
			break;
		}
	}
	return hit * w;
}

// 限界突破の Q.w=1.55 では従来どおり 1。上の段だけ、ぼかしの混ざりを滑らかに足す。
float DofGain()
{
	float g = saturate(Q.w);
	return g * lerp(1.0, 1.4, saturate((Q.w - 1.55) / 1.2));
}

float4 PSDofH(QOut i) : SV_Target
{
	float3 scene = Tex.SampleLevel(SL, i.uv, 0).rgb;
	// 作成プレビューは Q.w を負にする。人物が画面の大半なので、空だけでなく全面で被写界と遮蔽を省く。
	if (Q.w < 0) return float4(scene, 1);
	float z = Depth.SampleLevel(SL, i.uv, 0).r;
	float focus = Params.w;
	float coc = saturate((z - focus) * 2.4) * DofGain();
	scene += ReflectTap(i.uv, z) * (Q.x > 1.5 ? 0.24 : 0.18);
	scene *= ContactShadow(i.uv, z, Q.x > 1.5 ? 5 : 4);
	scene *= 1. - min(Ao4(i.uv, z, Q.x > 1.5 ? 8.5 : 5.5), 0.5) * (Q.x > 1.5 ? 0.30 : 0.22);
	float2 rad = Screen.zw * Params.z * coc;
	float3 acc = scene;
	float wsum = 1;
	if (coc > 0.002) {
		[unroll] for (int k = 0; k < 8; k++) {
			float2 u = i.uv + k8[k] * rad;
			float zs = Depth.SampleLevel(SL, u, 0).r;
			float w = 0.55 + 0.45 * saturate(1. - abs(zs - z) * 8.);
			acc += Tex.SampleLevel(SL, u, 0).rgb * w;
			wsum += w;
		}
	}
	return float4(lerp(scene, acc / wsum, saturate(coc * 1.35)), 1);
}

float4 PSDofU(QOut i) : SV_Target
{
	float3 scene = Tex.SampleLevel(SL, i.uv, 0).rgb;
	// 作成プレビューは Q.w を負にする。マップのポストはそのまま、この画面だけ色を返す。
	if (Q.w < 0) return float4(scene, 1);
	float z = Depth.SampleLevel(SL, i.uv, 0).r;
	float focus = Params.w;
	float farC = saturate((z - focus) * 1.7);
	float nearC = saturate((focus * 0.42 - z) * 6.0);
	float coc = max(farC, nearC) * DofGain();
	scene += ReflectTap(i.uv, z) * (Q.x > 5.5 ? 0.38 : 0.28);
	scene *= ContactShadow(i.uv, z, Q.x > 5.5 ? 7 : 5);
	{
		float ao = Ao12(i.uv, z, 7.0);
		float farReach = (Q.x > 5.5) ? 24.0 : ((Q.x > 4.5) ? 18.0 : 12.0);
		ao = max(ao, Ao12(i.uv, z, farReach) * 0.74);
		float k = (Q.x > 5.5) ? 0.44 : ((Q.x > 4.5) ? 0.36 : 0.30);
		scene *= 1. - min(ao, 0.5) * k;
	}
	float2 rad = Screen.zw * Params.z * coc;
	float3 acc = scene;
	float wsum = 1;
	if (coc > 0.002) {
		[unroll] for (int k = 0; k < 16; k++) {
			float2 u = i.uv + k16[k] * rad;
			float zs = Depth.SampleLevel(SL, u, 0).r;
			float w = 0.55 + 0.45 * saturate(1. - abs(zs - z) * 8.);
			acc += Tex.SampleLevel(SL, u, 0).rgb * w;
			wsum += w;
		}
		if (Q.x > 4.5) {
			int extra = (Q.x > 5.5) ? 16 : 8;
			[loop] for (int e = 0; e < extra; e++) {
				float2 u2 = i.uv + k16[e] * rad * 1.85;
				float zs = Depth.SampleLevel(SL, u2, 0).r;
				float w = 0.4 + 0.35 * saturate(1. - abs(zs - z) * 8.);
				acc += Tex.SampleLevel(SL, u2, 0).rgb * w;
				wsum += w;
			}
		}
	}
	return float4(lerp(scene, acc / wsum, saturate(coc * 1.35)), 1);
}
