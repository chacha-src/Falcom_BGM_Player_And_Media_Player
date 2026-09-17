cbuffer FrameCB : register(b0)
{
	float2 Screen;
	float2 InvScreen;
	float  Time;
	float  FadeGain;
	uint   PianoPass;
	uint   PianoRows;
	uint4  Reserve[8];
};

struct InstRect
{
	float4 xywh;
	float4 color;
	float4 uvRect;
	float4 extra;
};

StructuredBuffer<InstRect> Rects : register(t0);
Texture2D Atlas : register(t1);
SamplerState Samp : register(s0);

struct PSIn
{
	float4 pos : SV_POSITION;
	float4 color : COLOR0;
	float2 uv : TEXCOORD0;
	float glow : TEXCOORD1;
	float useAtlas : TEXCOORD2;
};

static float2 QuadPos(uint vid)
{
	const float2 p[6] = {
		float2(0, 0), float2(1, 0), float2(0, 1),
		float2(1, 0), float2(1, 1), float2(0, 1)
	};
	return p[vid % 6];
}

PSIn VS_Rect(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	InstRect r = Rects[iid];
	float2 q = QuadPos(vid);
	float2 p = r.xywh.xy + q * r.xywh.zw;
	PSIn o;
	o.pos = float4(p.x * InvScreen.x * 2.0 - 1.0, 1.0 - p.y * InvScreen.y * 2.0, 0, 1);
	o.color = r.color;
	o.uv = lerp(r.uvRect.xy, r.uvRect.zw, q);
	o.glow = r.extra.y;
	o.useAtlas = r.extra.x;
	return o;
}

float4 PS_Rect(PSIn i) : SV_Target
{
	float4 c = i.color;
	if (i.useAtlas > 0.5)
	{
		float4 t = Atlas.Sample(Samp, i.uv);
		c.rgb = lerp(c.rgb, t.rgb, t.a);
		c.a = max(c.a, t.a);
	}
	c.rgb += c.rgb * i.glow * FadeGain * float3(0.35, 0.85, 0.45);
	return c;
}
