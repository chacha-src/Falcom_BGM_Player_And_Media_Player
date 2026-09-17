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

struct PianoRow
{
	float4 xywh;
	uint4  litBits;
	float4 colW;
	float4 colB;
	float4 colLitW;
	float4 colLitB;
	float4 colGap;
	uint4  ext;
};

StructuredBuffer<PianoRow> Rows : register(t0);

struct PSIn
{
	float4 pos : SV_POSITION;
	float4 color : COLOR0;
	float2 uv : TEXCOORD0;
	float glow : TEXCOORD1;
	float useAtlas : TEXCOORD2;
};

static int IsBlack(int n)
{
	uint m = (uint)n % 12;
	return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) ? 1 : 0;
}

static int NoteLit(PianoRow r, int midi)
{
	int b = midi - 21;
	if (b < 0 || b > 87) return 0;
	uint word = r.litBits[b >> 5];
	return (word >> (b & 31)) & 1;
}

PSIn VS_PianoKey(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	/* pass0=白 52、pass1=黒 36。白を先に描き、黒が白に潰されないようにする */
	uint perRow = (PianoPass == 0) ? 52 : 36;
	uint row = iid / perRow;
	uint idx = iid % perRow;
	int midi = 21;
	int seen = -1;
	int k;
	if (PianoPass == 0) {
		for (k = 21; k <= 108; k++) {
			if (!IsBlack(k)) {
				seen++;
				if (seen == (int)idx) { midi = k; break; }
			}
		}
	} else {
		for (k = 21; k <= 108; k++) {
			if (IsBlack(k)) {
				seen++;
				if (seen == (int)idx) { midi = k; break; }
			}
		}
	}
	PianoRow r = Rows[row];
	int whites = 52;
	int wi = 0;
	for (k = 21; k < midi; k++)
	{
		if (!IsBlack(k)) wi++;
	}
	float ww = r.xywh.z;
	float hh = r.xywh.w;
	float x0, x1, y0, y1;
	float4 col;
	float glow = 0;
	int lit = NoteLit(r, midi);
	if (IsBlack(midi))
	{
		float xw = r.xywh.x + (wi * ww / whites);
		float bw = max(2.0, ww / whites * 0.55);
		x0 = xw - bw * 0.5;
		x1 = x0 + bw;
		y0 = r.xywh.y;
		y1 = r.xywh.y + hh * 0.62;
		col = lit ? r.colLitB : r.colB;
		glow = lit ? 0.35 : 0;
	}
	else
	{
		x0 = r.xywh.x + wi * ww / whites;
		x1 = r.xywh.x + (wi + 1) * ww / whites - 1.0;
		y0 = r.xywh.y;
		y1 = r.xywh.y + hh;
		col = lit ? r.colLitW : r.colW;
		glow = lit ? 0.22 : 0;
	}
	const float2 qtbl[6] = {
		float2(0, 0), float2(1, 0), float2(0, 1),
		float2(1, 0), float2(1, 1), float2(0, 1)
	};
	float2 q = qtbl[vid % 6];
	float2 p = float2(lerp(x0, x1, q.x), lerp(y0, y1, q.y));
	PSIn o;
	o.pos = float4(p.x * InvScreen.x * 2.0 - 1.0, 1.0 - p.y * InvScreen.y * 2.0, 0, 1);
	o.color = col;
	o.uv = float2(-1, -1);
	o.glow = glow;
	o.useAtlas = 0;
	return o;
}
