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

float Ao4(float2 uv, float z, float reach)
{
	float ao = 0;
	[unroll] for (int i = 0; i < 8; i++) {
		float zs = Depth.SampleLevel(SL, uv + k8[i] * Screen.zw * reach, 0).r;
		ao += saturate((z - zs) * 42. - 0.02);
	}
	return ao * 0.125;
}

float Ao12(float2 uv, float z, float reach)
{
	float ao = 0;
	[unroll] for (int i = 0; i < 12; i++) {
		float zs = Depth.SampleLevel(SL, uv + k16[i] * Screen.zw * reach, 0).r;
		ao += saturate((z - zs) * 36. - 0.015);
	}
	return ao * (1. / 12.);
}

float3 ReflectTap(float2 uv, float z)
{
	float3 hit = 0;
	float w = 0;
	float2 step = float2(Screen.z * 5.5, -Screen.w * 7.0);
	[unroll] for (int i = 2; i <= 6; i++) {
		float2 u = uv + step * i;
		float zs = Depth.SampleLevel(SL, u, 0).r;
		if (zs > 0.015 && z - zs > 0.02 && z - zs < 0.28) {
			hit = Tex.SampleLevel(SL, u, 0).rgb;
			w = saturate((0.28 - (z - zs)) * 4.);
			break;
		}
	}
	return hit * w;
}

float4 PSDofH(QOut i) : SV_Target
{
	float z = Depth.SampleLevel(SL, i.uv, 0).r;
	float focus = Params.w;
	float coc = saturate((z - focus) * 2.4) * saturate(Q.w);
	float3 scene = Tex.SampleLevel(SL, i.uv, 0).rgb;
	scene += ReflectTap(i.uv, z) * (Q.x > 1.5 ? 0.22 : 0.16);
	scene *= 1. - min(Ao4(i.uv, z, Q.x > 1.5 ? 7.5 : 5.0), 0.5) * (Q.x > 1.5 ? 0.26 : 0.2);
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
	float z = Depth.SampleLevel(SL, i.uv, 0).r;
	float focus = Params.w;
	float farC = saturate((z - focus) * 1.7);
	float nearC = saturate((focus * 0.42 - z) * 6.0);
	float coc = max(farC, nearC) * saturate(Q.w);
	float3 scene = Tex.SampleLevel(SL, i.uv, 0).rgb;
	scene += ReflectTap(i.uv, z) * (Q.x > 5.5 ? 0.34 : 0.26);
	{
		float ao = Ao12(i.uv, z, 6.5);
		float farReach = (Q.x > 5.5) ? 22.0 : ((Q.x > 4.5) ? 16.0 : 11.0);
		ao = max(ao, Ao12(i.uv, z, farReach) * 0.72);
		float k = (Q.x > 5.5) ? 0.4 : ((Q.x > 4.5) ? 0.34 : 0.28);
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
