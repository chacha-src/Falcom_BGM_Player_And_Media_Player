struct InstRect
{
	float4 xywh;
	float4 color;
	float4 uvRect;
	float4 extra;
};

cbuffer HexJob : register(b1)
{
	float4 Origin;
	uint   BankBase;
	uint   RowCount;
	uint   GapExtra;
	uint   HaveDump;
	uint4  Ext[8];
};

ByteAddressBuffer Dump : register(t0);
RWStructuredBuffer<InstRect> OutRects : register(u0);

static uint LoadU8(uint byteOff)
{
	uint v = Dump.Load(byteOff & ~3u);
	return (v >> ((byteOff & 3u) * 8u)) & 0xffu;
}

static float3 MixFade(float3 baseC, float3 hi, float fade)
{
	float t = saturate(fade);
	t = t * t * (3.0 - 2.0 * t);
	return lerp(baseC, hi, t);
}

static float4 GlyphUV(uint g)
{
	uint col = g & 15;
	uint row = g >> 4;
	float2 a = float2((col * 32.0) / 512.0, (row * 32.0) / 256.0);
	float2 b = a + float2(32.0 / 512.0, 32.0 / 256.0);
	return float4(a.x, a.y, b.x, b.y);
}

[numthreads(16, 16, 1)]
void CS_Hex(uint3 id : SV_DispatchThreadID)
{
	if (id.x > 15 || id.y >= RowCount) return;
	uint row = id.y;
	uint col = id.x;
	uint idx = BankBase + row * 16u + col;
	uint fade = 0;
	uint touched = 0;
	if (HaveDump)
	{
		fade = LoadU8(0x500 + idx);
		touched = LoadU8(0x800 + idx);
	}
	int opsRow = ((BankBase == 0 || (BankBase == 0x100 && RowCount >= 16)) && row >= 3 && row <= 9) ? 1 : 0;
	int inGroup = ((col % 4) != 3) ? 1 : 0;
	float3 baseDark = float3(24, 28, 32) / 255.0;
	float3 baseGroup = float3(36, 52, 44) / 255.0;
	float3 baseTouched = float3(48, 72, 58) / 255.0;
	float3 hi = float3(80, 220, 120) / 255.0;
	float3 baseC = baseDark;
	if (inGroup && opsRow)
		baseC = touched ? baseTouched : baseGroup;
	else if (touched)
		baseC = baseTouched;
	float3 rgb = MixFade(baseC, hi, fade / 255.0);
	float x = Origin.x + col * Origin.z + (col / 4) * GapExtra;
	float y = Origin.y + row * Origin.w;
	uint dst = row * 16u + col;
	InstRect r;
	r.xywh = float4(x, y, Origin.z - 1.0, Origin.w - 1.0);
	r.color = float4(rgb, 1);
	r.uvRect = float4(-1, -1, -1, -1);
	r.extra = float4(0, fade / 255.0, 0, 0);
	OutRects[dst] = r;
}

cbuffer HexGlyphJob : register(b2)
{
	float4 GOrigin;
	uint   GBankBase;
	uint   GRowCount;
	uint   GGapExtra;
	uint   GHaveDump;
	uint   GOutBase;
	uint3  GPad;
	uint4  GExt[7];
};

[numthreads(16, 16, 1)]
void CS_HexGlyph(uint3 id : SV_DispatchThreadID)
{
	if (id.x > 15 || id.y >= GRowCount) return;
	uint row = id.y;
	uint col = id.x;
	uint idx = GBankBase + row * 16u + col;
	uint val = 0;
	uint touched = 0;
	if (GHaveDump)
	{
		if (GBankBase >= 0x200)
			val = LoadU8(0x200 + (idx - 0x200));
		else
			val = LoadU8(idx);
		touched = LoadU8(0x800 + idx);
	}
	float x = GOrigin.x + col * GOrigin.z + (col / 4) * GGapExtra;
	float y = GOrigin.y + row * GOrigin.w;
	float cw = GOrigin.z - 1.0;
	float ch = GOrigin.w - 1.0;
	float gw = cw * 0.42;
	float gh = ch * 0.78;
	float gy = y + (ch - gh) * 0.5;
	float3 tc = touched ? float3(240, 250, 245) / 255.0 : float3(130, 145, 138) / 255.0;
	uint hi = (val >> 4) & 15;
	uint lo = val & 15;
	if (!GHaveDump) { hi = 16; lo = 16; }
	uint dst = GOutBase + (row * 16u + col) * 2u;
	InstRect a;
	a.xywh = float4(x + cw * 0.08, gy, gw, gh);
	a.color = float4(tc, 1);
	a.uvRect = GlyphUV(hi);
	a.extra = float4(1, 0, 0, 0);
	OutRects[dst] = a;
	InstRect b = a;
	b.xywh = float4(x + cw * 0.50, gy, gw, gh);
	b.uvRect = GlyphUV(lo);
	OutRects[dst + 1] = b;
}
