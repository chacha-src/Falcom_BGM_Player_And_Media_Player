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
	float4 Sight;    // x=明瞭な半径 y=ステージ番号 z=人数 w=有効
	float4 Ally[16]; // xy=味方のワールドxz z=有効
	float4 CamNear;  // x=視線方向で消す深さ y=ここから残す。0 なら無効 w=不透明帯の上端
	float4 CamFwd;   // xyz=今のカメラ前方
	float4 Wx;       // x=天候 y=時刻。地面の反射に使う。z=画質の連続段(rank)
	row_major float4x4 ReflectVP;
	float4 ReflectInfo; // x=映す y=床の高さ z=反射パス(1=上面を捨てる 2=上面も残す) w=ぼかし幅
	row_major float4x4 InvVP;
	float4 Hole0; // xyz=キャラ w=半径（0なら部分くり抜きなし）
	float4 Hole1; // xyz=カーソル w=有効
	float4 Hole2; // x=不透明帯の下端 y=他レイヤー透かし z=ゴーストパス w=周辺半径
};
cbuffer Obj : register(b1)
{
	row_major float4x4 World;
	float4 Tint;     // rgb=乗算 a=不透明度
	float4 Params;   // x=輪郭幅 y=モード z=発光 w=色相回転
	float4 Extra;    // x=ディゾルブ y=白フラッシュ z=リム w=揺れ（Skin スロットは OutlineColor.a）
	float4 OutlineColor;
};
cbuffer Bones : register(b2)
{
	row_major float4x4 Bones[64];
};
// 残った波紋。x=位置 z=位置 y=発生時刻 w=強さ。0 ならなし。リングは時間で広がる。
cbuffer Liquid : register(b3)
{
	float4 Rip[48];
};

// 画質の連続段。Q.x は粗段(整数)だが、Wx.z は 0.25 刻みの連続段で、段差をまたいでも滑らかに動く。
// 0超軽量 1軽量 2標準 3高 4超綺麗 5最高 6限界突破。限界突破は +10(=8.5)より上も伸びる。超軽量より下は負。
float QRank() { return Wx.z; }
float QLerp(float rank, float v0, float v1, float v2, float v3, float v4, float v5, float v6, float vHi)
{
	if (rank <= 0.0) return v0 * saturate(1.0 + rank * 0.5);
	if (rank <= 1.0) return lerp(v0, v1, rank);
	if (rank <= 2.0) return lerp(v1, v2, rank - 1.0);
	if (rank <= 3.0) return lerp(v2, v3, rank - 2.0);
	if (rank <= 4.0) return lerp(v3, v4, rank - 3.0);
	if (rank <= 5.0) return lerp(v4, v5, rank - 4.0);
	if (rank <= 6.0) return lerp(v5, v6, rank - 5.0);
	return v6 + (vHi - v6) * ((rank - 6.0) / 2.5);
}

float TacGate(float s, float th)
{
	if (s <= th) return 0;
	float u = saturate((s - th) / (1.0 - th));
	return u * u * (3.0 - 2.0 * u);
}

// そよ風・強風・暴風・上下。式は以前の 8 サンプル平均のまま。
// 頂点と画素のたびに sin を回さず、フレーム定数を読む。ホーム・戦闘・デモで同じ。
cbuffer AirNow : register(b4)
{
	float4 AirB; // xy=そよ風 zw=強風+暴風
	float4 AirL; // x=上下 y=GustAmt
	float4 AirHide0; // 折れて飛んだ木などの xz。x>=900 は空き
	float4 AirHide1;
};
float2 AirBreeze() { return AirB.xy; }
float2 AirMain() { return AirB.zw; }
float AirLift() { return AirL.x; }
float GustAmt() { return AirL.y; }
float RainFilm();

float3 FallLean(float3 w, float3 n, float amount)
{
	float2 dir = AirMain() + AirBreeze();
	float dl = length(dir);
	if (dl > 1e-4) dir /= dl;
	float g = saturate(length(AirMain()) / 2.70);
	float br = saturate(length(AirBreeze()) / 0.40);
	float hgt = saturate((w.y + 9.0) / 30.0);
	w.x += dir.x * (g * 1.25 + br * 0.35) * hgt * amount;
	w.z += dir.y * (g * 1.05 + br * 0.28) * hgt * amount;
	w.y += AirLift() * hgt * amount * 0.28;
	float flutter = sin(w.y * 1.85 + Eye.w * 3.4 + w.x * 0.45);
	w += n * flutter * (0.03 + g * 0.11) * saturate(amount * 1.4);
	return w;
}

float RippleH(float2 xz, float t, out float2 dh)
{
	float h = 0;
	dh = 0;
	[loop] for (int i = 0; i < 48; i++) {
		float s = Rip[i].w;
		if (s < 0.01) continue;
		float age = t - Rip[i].y;
		if (age < 0 || age > 3.8) continue;
		float2 d = xz - float2(Rip[i].x, Rip[i].z);
		float dist = length(d);
		float rad = age * 1.75;
		float ring = exp(-abs(dist - rad) * 3.4);
		float amp = s * ring * exp(-age * 0.42);
		float ph = (dist - rad) * 10.0;
		h += sin(ph) * amp * 0.085;
		dh += d / max(dist, 0.04) * cos(ph) * amp * 0.9;
	}
	return h;
}

Texture2D Tex : register(t0);
Texture2D ShadowMap : register(t1);
Texture2D DepthMap : register(t2); // グレード専用。近いほど 0、空は 1。
Texture2DArray Ground : register(t3); // 0..7 地面アルベド(a=AO) 8..15 法線。スロット表の外。
Texture2DArray Mat : register(t4);    // 樹皮・葉・岩・木材。各アルベドと法線。
	Texture2DArray Photo : register(t5);  // 0..13 実写アルベド(a>0.5で有効) 14..27 法線
Texture2D SphereMap : register(t6);   // 反射ベクトルの球体マップ
TextureCube EnvMap : register(t7);    // ステージの環境キューブ
Texture2D LiveRef : register(t8);     // キャラ・敵・上位レイヤーを映した反射
Texture2DArray Skin : register(t9);   // 敵・武具のアルベド。頂点色に乗せる
Texture2DArray GroundX : register(t10); // テーマごとの二枚目の地表
SamplerState SL : register(s0);
SamplerComparisonState SC : register(s1);
SamplerState CL : register(s2);

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
// 世界・気候のピクセル模様。最高〜限界突破ともこの4段で止める。上の段はテッセ・変位・被写界へ回す。
// 振幅和は 6段ぶんに揃える（0.9375 * 1.05）。頂点変位はテッセシェーダ側の別関数なので輪郭はここを変えない。
float fbm2(float2 p)
{
	float s = 0, a = .5;
	[unroll] for (int i = 0; i < 4; i++) { s += a * vnoise(p); p *= 2.03; a *= .5; }
	return s * 1.05;
}
// 平原・森・遺跡。形は残して細かい3段だけ省く。
float fbm3(float2 p)
{
	float s = 0, a = .5;
	[unroll] for (int i = 0; i < 3; i++) { s += a * vnoise(p); p *= 2.03; a *= .5; }
	return s * 1.125;
}
// 空と奈落用。大きな形は残し、細かい段は捨てる。重みを戻して濃さは揃える。
float fbmSky(float2 p)
{
	// 6段のうち、形を決める低い2段だけ。濃さは3段のときと同じに戻す。
	float s = 0, a = .5;
	[unroll] for (int i = 0; i < 2; i++) { s += a * vnoise(p); p *= 2.03; a *= .5; }
	return s * (0.875 / 0.75);
}
float2 CurlSky(float2 p)
{
	float e = 0.11;
	float n0 = fbmSky(p);
	float nx = fbmSky(p + float2(e, 0));
	float ny = fbmSky(p + float2(0, e));
	return float2(n0 - ny, nx - n0);
}

float2 Curl2(float2 p)
{
	float e = 0.11;
	// カールは法線の小さな揺れ。低い形の振幅は fbm2 と同じ。細さは最高で止める。
	float n0 = fbm3(p);
	float nx = fbm3(p + float2(e, 0));
	float ny = fbm3(p + float2(0, e));
	return float2(n0 - ny, nx - n0);
}

// 画面の位置から、カメラの世界視線を作る。空・奈落・ポストの模様はすべてこれに乗せる。
float3 WorldRay(float2 uv)
{
	float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.99, 1.0);
	float4 wp = mul(clip, InvVP);
	float3 p = wp.xyz / max(abs(wp.w), 1e-5);
	float3 d = p - Eye.xyz;
	float l = length(d);
	return l > 1e-5 ? d / l : float3(0, 0, 1);
}

// 視線が高さ h の水平面を貫く世界座標 xz。カメラを回しても地面や空の層に固定される。
float2 PlaneXZ(float3 rd, float h, float minSlope)
{
	float dy = rd.y;
	float s = abs(dy) < minSlope ? (dy < 0 ? -minSlope : minSlope) : dy;
	float dist = clamp((h - Eye.y) / s, -220.0, 220.0);
	return Eye.xz + rd.xz * dist;
}

float Sparkle(float2 xz, float t, float dens, float cut)
{
	float2 cell = floor(xz * dens);
	float h = hash21(cell);
	return step(cut, h) * (0.5 + 0.5 * sin(t * (6.2 + h * 9.0) + h * 40.0));
}

float CausAt(float2 xz, float t)
{
	float2 c1 = xz * 1.7 + t * float2(0.22, 0.08);
	float2 c2 = xz * 2.6 - t * float2(0.11, 0.18);
	float2 c3 = xz * 4.2 + t * float2(-0.14, 0.16);
	float a = saturate(sin(c1.x * 6.2) * sin(c1.y * 5.4) + sin(c2.x * 4.8) * sin(c2.y * 5.1));
	float b = saturate(sin(c3.x * 7.1) * sin(c3.y * 6.4));
	return pow(a, 2) + pow(b, 2) * 0.45;
}

float WaveAmp(float th)
{
	float amp = 0.16;
	if (th > 2.5 && th < 3.5) amp = 0.04;
	else if (th > 5.5 && th < 6.5) amp = 0.07;
	else if (th > 0.5 && th < 1.5) amp = 0.1;
	else if (th > 4.5 && th < 5.5) amp = 0.13;
	else if (th > 8.5 && th < 9.5) amp = 0.24;
	else if (th > 7.5 && th < 8.5) amp = 0.055;
	else if (th > 9.5 && th < 10.5) amp = 0.018;
	else if (th > 10.5 && th < 11.5) amp = 0.028;
	else if (th > 12.5) amp = 0.05;
	else if (th > 6.5) amp = 0.038;
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
	float gust = GustAmt();
	h *= 1.0 + gust * 1.15;
	dh *= 1.0 + gust * 1.15;
	if (RainFilm() > 0.5) {
		h *= 1.65;
		dh *= 1.85;
		h += sin(xz.x * 9.2 + t * 7.4) * sin(xz.y * 8.1 - t * 6.2) * amp * 0.42;
		dh += float2(cos(xz.x * 9.2 + t * 7.4), -cos(xz.y * 8.1 - t * 6.2)) * amp * 1.6;
	}
	if (Q.x > 1.5 && !(Q.x > 3.5 && Q.x < 4.5)) {
		float2 d4 = normalize(float2(0.72, 0.69));
		float2 d5 = normalize(float2(-0.88, 0.42));
		float k4 = 6.6, k5 = 10.2;
		float f4 = k4 * dot(d4, xz) - t * 2.9;
		float f5 = k5 * dot(d5, xz) - t * 3.7;
		float a4 = amp * 0.16, a5 = amp * 0.09;
		h += a4 * sin(f4) + a5 * sin(f5);
		dh += d4 * k4 * a4 * cos(f4) + d5 * k5 * a5 * cos(f5);
		if (Q.x > 2.5) {
			float2 p = xz * 3.4 + t * 0.35;
			h += (fbm2(p) - 0.5) * amp * 0.55;
			float e = 0.08;
			dh += float2(fbm2(p + float2(e, 0)) - fbm2(p), fbm2(p + float2(0, e)) - fbm2(p)) * amp * 5.0;
		}
	}
}

// amt: 頂点αの根元からの重み + Extra.w。Extra.w は岩まで滑らせないよう除く。
// そよ風は常時、強風と暴風と上下は C++ の TacAir と同じ位相。
float3 Sway(float3 w, float amt)
{
	float vert = amt - Extra.w;
	if (vert < 0) vert = 0;
	float bend = saturate(vert) + saturate(vert - 1.0) * 0.22;
	if (bend <= 0.001) return w;
	float2 m = AirMain();
	float2 b = AirBreeze();
	float lift = AirLift();
	float g = saturate(length(m) / 2.70);
	float t = Eye.w;
	float s = sin(t * 1.7 + w.x * 0.9 + w.z * 0.7) * 0.55 + sin(t * 2.9 + w.x * 1.7) * 0.28;
	float rain = RainFilm();
	if (rain > 0.5) { bend *= 1.45; g = saturate(g + 0.35); }
	float flick = bend * (0.06 + g * 0.18) * (1.0 + rain * 0.85);
	w.x += s * flick + b.x * bend * 0.38 + m.x * bend * 0.26;
	w.z += cos(t * 1.3 + w.z * 1.1 + w.x * 0.3) * flick * 0.85 + b.y * bend * 0.32 + m.y * bend * 0.22;
	w.y += lift * bend * 0.16;
	if (rain > 0.5) w.y -= bend * 0.045 * sin(t * 2.2 + w.x);
	return w;
}

float GustGone(float3 w)
{
	float2 p = w.xz;
	float2 h0 = AirHide0.xy, h1 = AirHide0.zw, h2 = AirHide1.xy, h3 = AirHide1.zw;
	if (h0.x < 900.0 && dot(p - h0, p - h0) < 0.36) return 1;
	if (h1.x < 900.0 && dot(p - h1, p - h1) < 0.36) return 1;
	if (h2.x < 900.0 && dot(p - h2, p - h2) < 0.36) return 1;
	if (h3.x < 900.0 && dot(p - h3, p - h3) < 0.36) return 1;
	return 0;
}

// 足は残し、頭の高さだけ同じ風で倒す。量は頭蓋も髪の頭もこの式だけ。
float3 WindLean(float localY)
{
	float h = saturate(localY / 1.55);
	h = h * h;
	float3 o = 0;
	if (h <= 0.001) return o;
	// 視点が 4m を超えると倒れを 1.55 まで増していた。背中の頂点だけ動き、骨に付いた翼の根元が外れて羽が扇になる。
	// 近いときに付いて見えていた倒れのまま、どの距離でも同じにする。
	float reach = 0.42;
	float2 m = AirMain();
	float2 b = AirBreeze();
	float lift = AirLift();
	float k = h * reach;
	float flutter = sin(Eye.w * 1.6 + localY * 2.2) * (0.02 + length(m) * 0.025);
	o.x = (b.x * 0.22 + m.x * 0.11) * k + flutter * k;
	o.z = (b.y * 0.18 + m.y * 0.10) * k;
	o.y = lift * 0.08 * k;
	return o;
}

// 舞台の人・敵・武具。sway=1 の描画だけ。mode 7 の髪はここを通らない。
void BodyWind(inout float3 w, float localY)
{
	if (Extra.w < 0.75) return;
	if (OutlineColor.a < 1.5) return;
	if (Params.y > 0.5) return;
	w += WindLean(localY);
}

PSIn ShadeStatic(VSIn v, row_major float4x4 WM)
{
	PSIn o;
	float3 w = mul(float4(v.p, 1), WM).xyz;
	float3 n = normalize(mul(v.n, (float3x3)WM));
	// 移動マス・範囲・カーソルは頂点αが半透明。Sway は (1-α) を草の重みにするので、そのままだとマスが風で倒れる。
	// DrawOverlayQuads が Extra.x < 0 を入れる。水面の頂点変位（mode 1）もマスには掛けない。
	if (Extra.x > -0.5) {
	w = Sway(w, (1. - v.c.a) * 4. + Extra.w);
	BodyWind(w, v.p.y);
	// 髪は mode 7 なので上の BodyWind に入らない。頭蓋の頂点だけ倒れると、髪の頭が骨に残って禿げる。
	// 体は頂点を縮尺済み、髪はワールド側で縮尺する。高さを揃えてから、同じ倒れを足す。
	if (Params.y > 6.5 && Params.y < 7.5 && Extra.w >= 0.75)
		w += WindLean(v.p.y * length(WM[1].xyz));
	// 体は noTess で OutlineColor.a=2。滝の mode が定数に残っても、人や敵の頂点は流さない。
	if (OutlineColor.a < 1.5 && Params.y > 0.5 && Params.y < 1.5) {
		if (n.y > 0.45) {
			float t = Eye.w;
			float th = FogColor.a;
			float amp = WaveAmp(th);
			float2 flow = v.c.rg * 2.0 - 1.0;
			float rush = saturate(v.c.b * 1.35);
			float spd = lerp(0.45, 2.6, rush);
			float h;
			float2 dh;
			WaterWave(w.xz - flow * t * spd, t, amp * (1.0 + rush * 0.85), h, dh);
			w.y += h;
			float2 rdh;
			w.y += RippleH(w.xz, t, rdh);
			w += float3(-dh.x, 0, -dh.y) * 0.04;
		} else {
			// 長い滝は上端だけ風でずらすと、下へ続く柱が途中から折れて見える。
			// 風は柱ごと、さざめきは面の向きだけ。高さでは横に動かさない。
			float2 dir = AirMain() + AirBreeze();
			float dl = length(dir);
			if (dl > 1e-4) dir /= dl;
			float g = saturate(length(AirMain()) / 2.70);
			float br = saturate(length(AirBreeze()) / 0.40);
			float sway = g * 0.16 + br * 0.05;
			w.x += dir.x * sway;
			w.z += dir.y * sway;
			float ripple = sin(w.y * 1.65 - Eye.w * 3.4);
			w += n * ripple * (0.018 + g * 0.04);
		}
	} else if (OutlineColor.a < 1.5 && Params.y > 1.5 && Params.y < 2.5) {
		if (n.y > 0.45) {
			float t = Eye.w;
			float2 flow = v.c.rg * 2.0 - 1.0;
			w.y += sin(dot(w.xz, flow) * 1.6 - t * 0.9) * (0.028 + GustAmt() * 0.02);
			float2 rdh;
			w.y += RippleH(w.xz, t, rdh) * 0.7;
		} else {
			w = FallLean(w, n, 0.16);
			w.x += sin(w.y * 0.45 - Eye.w * 0.32 + w.z * 0.2) * 0.11;
			w.z += cos(w.y * 0.38 - Eye.w * 0.28) * 0.07;
			w.y -= 0.04 * sin(Eye.w * 0.4 + w.x);
		}
	}
	}
	w += n * Params.x;
	o.w = w; o.n = n; o.uv = v.uv; o.c = v.c;
	o.pos = mul(float4(w, 1), VP);
	return o;
}
PSIn VSS(VSIn v) { return ShadeStatic(v, World); }
PSIn VSSB(VSIn v, float4 r0 : TEXCOORD4, float4 r1 : TEXCOORD5, float4 r2 : TEXCOORD6, float4 r3 : TEXCOORD7)
{
	row_major float4x4 WM = float4x4(r0, r1, r2, r3);
	return ShadeStatic(v, WM);
}
float4 ShadowPos(float3 p, float3 n, float a, float localY)
{
	float3 w = p;
	w = Sway(w, (1. - a) * 4. + Extra.w);
	BodyWind(w, localY);
	w += n * Params.x;
	return mul(float4(w, 1), VP);
}
float4 VSSSh(VSIn v) : SV_Position
{
	float3 n = normalize(mul(v.n, (float3x3)World));
	return ShadowPos(mul(float4(v.p, 1), World).xyz, n, v.c.a, v.p.y);
}
float4 VSSBSh(VSIn v, float4 r0 : TEXCOORD4, float4 r1 : TEXCOORD5, float4 r2 : TEXCOORD6, float4 r3 : TEXCOORD7) : SV_Position
{
	row_major float4x4 WM = float4x4(r0, r1, r2, r3);
	float3 n = normalize(mul(v.n, (float3x3)WM));
	return ShadowPos(mul(float4(v.p, 1), WM).xyz, n, v.c.a, v.p.y);
}
float4 VSKSh(VKIn v) : SV_Position
{
	float4x4 m = Bones[v.bi.x] * v.bw.x + Bones[v.bi.y] * v.bw.y + Bones[v.bi.z] * v.bw.z + Bones[v.bi.w] * v.bw.w;
	float3 lp = mul(float4(v.p, 1), m).xyz;
	float3 w = mul(float4(lp, 1), World).xyz;
	BodyWind(w, v.p.y);
	return mul(float4(w, 1), VP);
}

PSIn VSK(VKIn v)
{
	float4x4 m = Bones[v.bi.x] * v.bw.x + Bones[v.bi.y] * v.bw.y + Bones[v.bi.z] * v.bw.z + Bones[v.bi.w] * v.bw.w;
	float3 lp = mul(float4(v.p, 1), m).xyz;
	float3 ln = mul(v.n, (float3x3)m);
	PSIn o;
	float3 w = mul(float4(lp, 1), World).xyz;
	float3 n = normalize(mul(ln, (float3x3)World));
	BodyWind(w, v.p.y);
	w += n * Params.x;
	o.w = w; o.n = n; o.uv = v.uv; o.c = float4(1, 1, 1, 1);
	o.pos = mul(float4(w, 1), VP);
	return o;
}

// 影の芯を 0 にすると全マップで床も体も黒く潰れる。日向よりはっきり暗く、模様は残す。
float ShadowVis(float s)
{
	return lerp(0.46, 1.0, pow(saturate(s), 1.35));
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
	// 単位円盤。地面の位置で回して、模様がカメラに追従して流れないようにする。
	const float2 k[16] = {
		float2(-0.326212, -0.405810), float2(-0.840144, -0.073580), float2(-0.695914, 0.457137),
		float2(-0.203345, 0.620716), float2(0.962340, -0.194983), float2(0.473434, -0.480026),
		float2(0.519456, 0.767022), float2(0.185461, -0.893124), float2(0.507431, 0.064425),
		float2(0.896420, 0.412458), float2(-0.321940, -0.932615), float2(-0.791559, -0.597710),
		float2(0.089842, 0.323164), float2(-0.461230, 0.218612), float2(0.248110, 0.553812),
		float2(-0.118392, -0.210418)
	};
	float ang = frac(dot(w.xz, float2(0.1031, 0.11369))) * 6.2831853;
	float cs = cos(ang), sn = sin(ang);
	// 超軽量は1点。軽量と標準は円盤の大きさだけ広げ、枚数は変えない。
	if (Q.x < 1.5 || (Q.x > 3.5 && Q.x < 4.5)) {
		if (Q.x > 3.5 && Q.x < 4.5) return ShadowVis(ShadowMap.SampleCmpLevelZero(SC, uv, z));
		float t0 = Fog.w * (Q.x > 0.5 ? 7.6 : 5.0);
		float s0 = 0;
		[loop] for (int i = 0; i < 12; i++) {
			float2 o = k[i];
			float2 r = float2(o.x * cs - o.y * sn, o.x * sn + o.y * cs);
			s0 += ShadowMap.SampleCmpLevelZero(SC, uv + r * t0, z);
		}
		return ShadowVis(s0 * (1.0 / 12.0));
	}
	// 自然光。足元は細く、遮蔽が遠いほど太陽の視半径ぶん縁が広がる。
	// 広がりは探索円の内側まで。影の中は探索だけで打ち切るので、外側の追加サンプルは足さない。
	int nb = (Q.x > 5.5) ? 16 : ((Q.x > 2.5) ? 8 : 4);
	float searchTex = (Q.x > 5.5) ? 14.0 : ((Q.x > 2.5) ? 8.0 : 5.0);
	float search = Fog.w * searchTex;
	float avg = 0;
	float cnt = 0;
	[loop] for (int b = 0; b < nb; b++) {
		float d = ShadowMap.SampleLevel(SL, uv + k[b] * search, 0).r;
		if (d > 0.001 && d < z) { avg += d; cnt += 1; }
	}
	if (cnt < 0.5) return 1;
	// 近い遮蔽は細いまま。遠いほど縁だけ大きく広げる。
	float pen = saturate((z - avg / cnt) * ((Q.x > 5.5) ? 7.5 : 5.5));
	float contact = (Q.x > 5.5) ? 1.55 : ((Q.x > 2.5) ? 1.25 : 1.05);
	float sun = (Q.x > 5.5) ? 20.0 : ((Q.x > 2.5) ? 13.0 : 8.0);
	float radTex = lerp(contact, sun, pen);
	float capTex = searchTex * 0.92;
	if (radTex > capTex) radTex = capTex;
	if (cnt >= (float)nb - 0.5) return ShadowVis(0);
	float t = Fog.w * radTex;
	float zb = z - t * 0.2;
	int taps = (Q.x > 2.5) ? 16 : 12;
	float s = 0;
	[loop] for (int i = 0; i < taps; i++) {
		float2 o = k[i];
		float2 r = float2(o.x * cs - o.y * sn, o.x * sn + o.y * cs);
		s += ShadowMap.SampleCmpLevelZero(SC, uv + r * t, zb);
	}
	// 芯は日向の半分弱。縁のぼかしだけ、サンプルの混ざりを残す。
	return ShadowVis(s / taps);
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
	// 無印の各段の量は従来どおり。世界の濡れは限界突破で止める。上の段は地形へ回す。
	return QLerp(QRank(), 0.0, 0.0, 0.22, 0.42, 0.64, 0.84, 1.0, 1.0);
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
		if (Q.x > 4.5) {
			float pore = fbm2(albedo.rg * 18.0 + n.xz * 9.0);
			col += float3(1, 0.96, 0.9) * pow(ndh, 90.) * ndl * dew * pore * 0.18;
		}
	} else {
		float spec = pow(ndh, lerp(16., 72., dew)) * ndl;
		float wet = pow(1. - ndv, lerp(3.6, 2.1, dew));
		col += float3(1, 0.97, 0.92) * spec * dew * coat * 0.24;
		col += lerp(albedo, float3(1, 0.95, 0.9), 0.3) * wet * dew * coat * 0.2;
		if (Q.x > 2.5 && flesh > 0.4) {
			float pore = fbm2(albedo.rb * 22.0 + n.xy * 11.0);
			col += lerp(albedo, float3(1, 0.55, 0.42), 0.35) * wrap * dew * flesh * (0.06 + pore * 0.08);
			col += float3(1, 0.97, 0.94) * pow(ndh, lerp(48., 110., dew)) * ndl * dew * flesh * 0.12;
		}
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
	// 背を向けた面も 0 にしない。向きは試合で変わるので、陰でも服と顔が読める明るさにする。
	float soft = lerp(saturate(nd), saturate((nd + 0.4) / 1.4), skin);
	float ndl = lerp(0.42, 1.0, soft);
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
	float ndl = lerp(0.42, 1.0, saturate(dot(n, l) * 0.5 + 0.5));
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

float SightAmt(float3 w)
{
	if (Sight.w < 0.5) return 0;
	float best = 1e4;
	[unroll] for (int i = 0; i < 16; i++) {
		float2 d = abs(w.xz - Ally[i].xy);
		float man = d.x + d.y;
		best = min(best, lerp(1e4, man, step(0.5, Ally[i].z)));
	}
	float r1 = max(Sight.x, 1);
	float r0 = r1 * 0.72;
	return smoothstep(r0, r1, best);
}

// カット中、味方・敵の足元まわりの物を透かす。床は残す。
float AllyGhost(float3 w)
{
	if (CamNear.z < 0.5) return 0;
	float g = 0;
	[unroll] for (int i = 0; i < 16; i++) {
		if (Ally[i].z < 0.5) continue;
		float d = length(w.xz - Ally[i].xy);
		g = max(g, saturate(1.85 - d * 0.95));
	}
	g *= saturate((w.y - 0.06) * 5.0);
	return g;
}

float HoleCover(float3 w, float ny, float3 focus, float r)
{
	if (w.y < focus.y + 0.32) return 0;
	float dist = length(w.xz - focus.xz);
	float fall = 1.0 - saturate(dist / max(r, 0.2));
	fall = fall * fall * (3.0 - 2.0 * fall);
	if (fall < 0.01) return 0;
	float dFrag = dot(w - Eye.xyz, CamFwd.xyz);
	float dFocus = dot(focus - Eye.xyz, CamFwd.xyz);
	float inFront = dFocus - dFrag;
	if (inFront < 0.10) return 0;
	float above = saturate((w.y - focus.y - 0.28) * 2.4);
	if (above < 0.2 && ny < 0.55) return 0;
	float v = fall * max(above, saturate(inFront * 1.2) * above);
	if (v > 0.28) return 1;
	return v;
}

float OpaqueLo() { return Hole2.x; }
float OpaqueHi()
{
	float hi = CamNear.w;
	float lo = Hole2.x;
	return hi >= lo ? hi : (lo + 0.55);
}

// カーソル／キャラの層＋移動帯は残す。上の階だけ透かす。下の階は残す。
float OtherLayer(float3 w, float ny)
{
	if (max(Hole2.y, Wx.w) < 0.5) return 0;
	float yHi = OpaqueHi();
	float rad = Hole2.w > 1.0 ? Hole2.w : 18.0;
	float xz = length(w.xz - Hole0.xz);
	if (Hole1.w > 0.5) xz = min(xz, length(w.xz - Hole1.xz));
	if (xz >= rad) return 0;
	// 上の階だけ透かす。下の階は見えていても邪魔にならないので残す。
	if (w.y > yHi + 0.04) return 1;
	return 0;
}

// 同一帯の床は残す。別層が画面上で焦点を隠すときだけ層として透かす。
float ViewOccludeAt(float3 w, float ny, float4 spos, float3 focus)
{
	float yLo = OpaqueLo();
	float yHi = OpaqueHi();
	if (w.y >= yLo - 0.06 && w.y <= yHi + 0.06) return 0;
	if (w.y < yLo - 0.06) return 0;
	float dFrag = dot(w - Eye.xyz, CamFwd.xyz);
	float dFocus = dot(focus - Eye.xyz, CamFwd.xyz);
	if (dFocus - dFrag < 0.06) return 0;
	float4 cf = mul(float4(focus, 1), VP);
	if (cf.w < 0.05) return 0;
	float2 nf = cf.xy / cf.w;
	float2 nw;
	if (abs(spos.w) > 0.05) nw = spos.xy / spos.w;
	else {
		float4 cw = mul(float4(w, 1), VP);
		if (cw.w < 0.05) return 0;
		nw = cw.xy / cw.w;
	}
	float2 dlt = nw - nf;
	dlt.x *= Screen.x / max(Screen.y, 1);
	if (length(dlt) > 0.72) return 0;
	return 1;
}

float ViewOcclude(float3 w, float ny, float4 spos)
{
	if (max(Hole2.y, Wx.w) < 0.5) return 0;
	float g = ViewOccludeAt(w, ny, spos, Hole0.xyz);
	if (Hole1.w > 0.5) g = max(g, ViewOccludeAt(w, ny, spos, Hole1.xyz));
	return g;
}

static float sGhost = 0;

float TerrainGhost(float3 w, float ny, float4 spos)
{
	// ReflectInfo.x は本描画で「反射をサンプルする」フラグ。ここを見ると水面マップで透かしが死ぬ。
	if (ReflectInfo.z > 0.5) return 0;
	float g = 0;
	float r = Hole0.w;
	float yLo = OpaqueLo();
	float yHi = OpaqueHi();
	bool sameBand = (w.y >= yLo - 0.06 && w.y <= yHi + 0.06);
	if (r > 0.2 && !sameBand) {
		g = max(g, HoleCover(w, ny, Hole0.xyz, r));
		if (Hole1.w > 0.5) g = max(g, HoleCover(w, ny, Hole1.xyz, r));
	}
	g = max(g, OtherLayer(w, ny));
	g = max(g, ViewOcclude(w, ny, spos));
	if (CamNear.y > CamNear.x + 0.001) {
		float d = dot(w - Eye.xyz, CamFwd.xyz);
		if (d < CamNear.y)
			g = max(g, saturate((CamNear.y - d) / max(0.22, CamNear.y - CamNear.x)));
	}
	float ag = AllyGhost(w);
	if (ny > 0.62) ag *= saturate((w.y - 0.18) * 6.0);
	if (ag > 0.4) g = max(g, saturate((ag - 0.22) * 1.5));
	return saturate(g);
}

// 水面も同一帯は残す。別高さの水だけ層ごと透かす（足元だけの残しはしない）。
float LiquidAway(float3 w)
{
	if (ReflectInfo.z > 0.5) return 0;
	if (max(Hole2.y, Wx.w) < 0.5) return 0;
	float yHi = OpaqueHi();
	float rad = Hole2.w > 1.0 ? Hole2.w : 18.0;
	float xz = length(w.xz - Hole0.xz);
	if (Hole1.w > 0.5) xz = min(xz, length(w.xz - Hole1.xz));
	if (xz >= rad) return 0;
	if (w.y > yHi + 0.04) return 1;
	return 0;
}

float LiquidGhost(float3 w, float ny, float4 spos)
{
	// 下へ落ちる滝は、層が違うだけでは消さない。
	// キャラの周囲か、カメラの直前か、画面上で焦点を隠すときだけ透かす。
	if (ny < 0.45) {
		if (ReflectInfo.z > 0.5) return 0;
		float g = ViewOcclude(w, ny, spos);
		float dxz = length(w.xz - Hole0.xz);
		if (Hole1.w > 0.5) dxz = min(dxz, length(w.xz - Hole1.xz));
		float y0 = min(Hole0.y, OpaqueLo());
		float y1 = max(Hole0.y + 2.2, OpaqueHi());
		if (dxz < 3.4 && w.y > y0 - 0.35 && w.y < y1 + 0.35) g = max(g, 1);
		if (CamNear.y > CamNear.x + 0.001) {
			float d = dot(w - Eye.xyz, CamFwd.xyz);
			if (d > 0.02 && d < CamNear.y)
				g = max(g, saturate((CamNear.y - d) / max(0.22, CamNear.y - CamNear.x)));
		}
		return saturate(g);
	}
	return saturate(max(TerrainGhost(w, ny, spos), LiquidAway(w)));
}

void ClipGhost(float4 spos)
{
	if (Hole2.z < 0.5) {
		if (sGhost > 0.35) clip(-1 - abs(spos.x) * 0);
	} else {
		if (sGhost < 0.05) clip(-1 - abs(spos.x) * 0);
	}
}

void BeginGhost(float4 spos, float3 w, float ny)
{
	sGhost = TerrainGhost(w, ny, spos);
	ClipGhost(spos);
}

void BeginLiquidGhost(float4 spos, float3 w, float ny)
{
	sGhost = LiquidGhost(w, ny, spos);
	ClipGhost(spos);
}

float GhostA(float a)
{
	if (Hole2.z < 0.5) return a;
	return a * lerp(0.38, 0.22, saturate(sGhost));
}

void SoftGhost(float4 spos, float3 w, float ny)
{
	sGhost = TerrainGhost(w, ny, spos);
}

float FadeA(float a)
{
	return a * lerp(1.0, 0.22, saturate(sGhost));
}

// きらめき・反射・コースティクスは α だけ落とすと白く残る。色も落とす。
float3 GhostCol(float3 col)
{
	float g = saturate(sGhost);
	if (g < 0.02) return col;
	if (Hole2.z > 0.5)
		return col * lerp(0.50, 0.16, g);
	return col * lerp(1.0, 0.28, g);
}

void CutGhost(float4 spos, float3 w, float ny)
{
	BeginGhost(spos, w, ny);
}

float3 FogAt(float3 col, float3 w)
{
	float d = length(Eye.xyz - w);
	float fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
	fg = saturate(fg + saturate(-w.y * Fog.z));
	col = lerp(col, FogColor.rgb, fg * fg * 0.9);
	float sh = SightAmt(w);
	float3 dim = lerp(FogColor.rgb, float3(0.05, 0.06, 0.09), 0.82);
	col = lerp(col, dim, sh * 0.84);
	return col;
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

// カメラ前方の地形は消さず、BeginGhost で半透明に残す。
void CamShell(float4 spos, float3 w)
{
	BeginGhost(spos, w, 0.0);
}

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
	float th = FogColor.a;
	bool ease = th < 0.5 || (th > 1.5 && th < 2.5) || (th > 6.5 && th < 7.5);
	bool slim = th > 9.5 && th < 12.5;
	float h0, hx, hz;
	if (slim) { h0 = fbmSky(p); hx = fbmSky(p + float2(e, 0)); hz = fbmSky(p + float2(0, e)); }
	else if (ease) { h0 = fbm3(p); hx = fbm3(p + float2(e, 0)); hz = fbm3(p + float2(0, e)); }
	else { h0 = fbm2(p); hx = fbm2(p + float2(e, 0)); hz = fbm2(p + float2(0, e)); }
	float3 t = normalize(cross(abs(n.y) < 0.92 ? float3(0, 1, 0) : float3(1, 0, 0), n));
	float3 b = cross(n, t);
	float3 nn = n + (t * (h0 - hx) + b * (h0 - hz)) * s;
	// 2段目は細かい法線。遠い画素では既にエイリアスするので、近い所だけ回す。段を上げても世界側は増やさない。
	float distN = distance(w, Eye.xyz);
	if (Q.x > 4.5 && !slim && distN < 36.0) {
		float2 q = p * (Q.x > 5.5 ? 2.7 : 1.8);
		float g0, gx, gz;
		if (ease) { g0 = fbm3(q); gx = fbm3(q + float2(e, 0)); gz = fbm3(q + float2(0, e)); }
		else { g0 = fbm2(q); gx = fbm2(q + float2(e, 0)); gz = fbm2(q + float2(0, e)); }
		nn += (t * (g0 - gx) + b * (g0 - gz)) * (Q.x > 5.5 ? 0.55 : 0.32);
	}
	return normalize(nn);
}

float RainAmt()
{
	float w = Wx.x;
	if (w > 0.5 && w < 1.5) return 1.0;
	if (w > 4.5 && w < 5.5) return 1.28;
	return 0.0;
}

// 雨のとき、服・肌・髪に水膜と、重力に沿って滑る滴。上面は膜、縦の面は筋になって落ちる。
float3 RainSlide(float3 col, float3 albedo, float3 n, float3 v, float3 w, float sh, float hairAmt, float flesh)
{
	float rain = RainAmt();
	if (rain < 0.01) return col;
	float3 up = float3(0, 1, 0);
	float3 slopeV = up - n * dot(n, up);
	float sl = length(slopeV);
	slopeV = sl > 1e-3 ? slopeV / sl : float3(0, 0, 1);
	float3 across = cross(n, slopeV);
	float al = length(across);
	across = al > 1e-3 ? across / al : float3(1, 0, 0);
	float side = dot(w, across);
	float down = dot(w, slopeV);
	float sky = saturate(n.y);
	float wall = saturate(sl);
	float shelter = saturate(n.y * 0.85 + 0.35);
	float laneN = lerp(7.5, 16.0, saturate(hairAmt + flesh * 0.25));
	float lane = abs(frac(side * laneN + fbm2(w.xz * 2.4 + w.y * 0.15) * 0.3) - 0.5);
	float path = smoothstep(0.42, 0.5, 0.5 - lane);
	float spd = lerp(1.15, 2.35, hairAmt) * lerp(0.55, 1.0, wall);
	float flow = down * lerp(10.0, 22.0, hairAmt) - Eye.w * spd * rain;
	float bead = pow(saturate(sin(flow) * 0.5 + 0.5), lerp(5.5, 9.0, hairAmt));
	float run = pow(saturate(sin(flow * 0.62 + side * 28.0) * 0.5 + 0.5), 4.5) * path;
	float drip = path * bead * wall;
	float film = sky * (0.35 + 0.65 * fbm2(w.xz * 5.5 + float2(0.0, -Eye.w * 0.12)));
	float wet = saturate((film * 0.9 + drip * 1.35 + run * 0.55 + wall * 0.22) * shelter) * saturate(rain);
	col *= lerp(1.0, lerp(float3(0.62, 0.70, 0.78), float3(0.72, 0.78, 0.84), flesh), saturate(wet * 0.8));
	float3 l = normalize(LightDir.xyz);
	float3 h = normalize(l + v);
	float ndl = saturate(dot(n, l));
	float ndh = saturate(dot(n, h));
	float gloss = pow(ndh, lerp(40.0, 88.0, max(hairAmt, flesh)));
	col += float3(0.72, 0.86, 1.0) * gloss * ndl * sh * wet * 0.7;
	col += float3(0.84, 0.93, 1.0) * (drip * 0.62 + run * 0.38) * rain * shelter;
	col += albedo * pow(1.0 - saturate(dot(n, v)), 2.4) * wet * 0.16;
	return col;
}

float3 FamSkin(float3 painted, float3 n, float3 w, float slice)
{
	float scale = slice < 24.0 ? 8.6 : (slice < 52.0 ? 5.8 : 4.6);
	float3 skin = Tri(Skin, slice, w, n, scale);
	float sl = max(dot(skin, float3(0.30, 0.50, 0.20)), 0.04);
	float3 sN = (sl < 0.22) ? saturate(skin * (0.28 / sl)) : skin;
	float3 p = w * (slice < 24.0 ? 9.2 : 6.1);
	if (slice < 8.0) {
		float g = 0.52 + 0.48 * sin(p.y * 16.0 + fbm2(p.xz) * 5.0);
		sN *= lerp(float3(0.78, 0.62, 0.42), float3(1.12, 0.98, 0.78), saturate(g));
	} else if (slice < 16.0) {
		float sc = pow(saturate(sin(p.y * 42.0 + p.x * 3.5)), 9.0);
		float band = 0.82 + 0.18 * sin(p.y * 24.0);
		sN = (sN * band + sc * 0.22) * float3(1.05, 1.06, 1.08);
	} else if (slice < 24.0) {
		float wr = fbm3(p.xz * 0.85);
		sN *= lerp(0.78, 1.16, wr);
	} else if (slice < 40.0) {
		float st = abs(sin(p.x * 20.0 + p.z * 7.0 + fbm2(p.xz) * 2.5));
		sN *= lerp(0.62, 1.22, st);
	} else if (slice < 52.0) {
		float2 vg = VoronoiGap(p.xz * 3.4 + p.y * 2.2);
		sN *= lerp(0.55, 1.22, saturate(vg.y * 11.0));
		sN += 0.05 * step(vg.x, 0.07);
	} else if (slice < 64.0) {
		float2 t = p.xz * float2(4.4, 6.8);
		float sca = abs(sin(t.x) * sin(t.y + 0.5 * sin(t.x)));
		sN *= lerp(0.64, 1.26, saturate(sca));
	} else if (slice < 72.0) {
		float f = abs(sin(p.z * 22.0 + p.x * 5.0));
		sN *= lerp(0.7, 1.18, f);
	} else if (slice < 80.0) {
		sN = lerp(sN, sN.gbr * float3(0.7, 1.1, 1.15), 0.28 + 0.22 * sin(p.y * 9.0 + Eye.w));
	} else if (slice < 88.0) {
		sN *= 0.82 + 0.28 * fbm2(p.xz * 5.5);
	} else if (slice < 96.0) {
		float2 vg = VoronoiGap(p.xz * 1.55);
		sN *= lerp(0.78, 1.14, saturate(vg.y * 6.0));
	} else if (slice < 104.0) {
		sN *= 0.74 + 0.32 * fbm3(p.xz * 0.55);
	} else if (slice < 112.0) {
		sN *= 0.86 + 0.22 * fbm2(p.xz * 3.2);
	} else if (slice < 120.0) {
		float c = CausAt(p.xz, Eye.w * 0.28);
		sN = lerp(sN, sN * float3(0.45, 0.9, 1.15), c * 0.4);
	} else {
		sN = lerp(sN, sN * float3(1.45, 0.42, 0.18), 0.32 + 0.22 * sin(Eye.w * 3.2 + p.y));
	}
	float3 mat = saturate(sN * 0.48 + skin * 0.52);
	float pl = dot(painted, float3(0.30, 0.50, 0.20));
	float useP = smoothstep(0.08, 0.42, pl);
	float wep = step(slice, 23.5);
	float3 mixed = lerp(mat, saturate(painted * 0.38 + mat * 0.78), useP);
	return lerp(mixed, saturate(mat * 0.82 + painted * 0.28), wep);
}

GOut PSM(PSIn i)
{
	// 人・モンスター・服。カットの手前フェード（CamShell）は掛けない。
	// 人の体・服・髪（輪郭色 a>1.5）。地形の細かい法線は肌と布をまだらにし、浅い角度の縁の光は紺を灰色に飛ばす。
	const float person = OutlineColor.a > 1.5 ? 1. : 0.;
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	// 人は両面で描く。襟やリボンは裏表 2 枚が同じ位置に重なり、裏の 1 枚が勝つと体側を向いた法線で灰色に沈む。
	// はっきり裏を向いた法線だけ返す（輪郭付近の 0 前後は触らない）。
	if (person > 0.5 && dot(n, v) < -0.1) n = -n;
	if (person < 0.5) n = DetailN(n, i.w);
	float4 tx = Tex.Sample(SL, i.uv);
	float3 albedo = tx.rgb * i.c.rgb;
	float matSlice = -1.0;
	// OutlineColor.a >= 3 が Skin スロット+3。武具・輸入敵は UV 無しで頂点色がアルベド。
	if (person > 0.5 && OutlineColor.a > 2.5 && OutlineColor.a < 131.5) {
		matSlice = OutlineColor.a - 3.0;
		float3 painted = albedo;
		float3 fam = FamSkin(painted, n, i.w, matSlice);
		float uv0 = step(abs(i.uv.x) + abs(i.uv.y), 1e-4);
		albedo = lerp(lerp(painted, fam, 0.48), fam, uv0);
	}
	if (abs(Params.w) > 0.001) {
		float m = 1. - SkinMask(albedo);
		albedo = lerp(albedo, saturate(HueRotate(albedo, Params.w)), m);
	}
	albedo *= Tint.rgb;
	float alpha = Tint.a;
	float3 emis = albedo * Params.z;
	float skin = SkinMask(albedo);
	float hair = (Params.y > 6.5 && Params.y < 7.5) ? 1 : 0;
	float sh = ShadowAt(i.w, n);
	float rough = lerp(0.46, 0.66, skin);
	float3 f0 = lerp(float3(0.04, 0.04, 0.04), float3(0.03, 0.02, 0.015), skin);
	if (matSlice >= 0.0 && matSlice < 16.0) {
		rough = lerp(rough, 0.16, 0.72);
		f0 = lerp(f0, float3(0.68, 0.70, 0.74), 0.58);
	} else if (matSlice >= 22.0 && matSlice < 24.0) {
		rough = lerp(rough, 0.14, 0.65);
		f0 = lerp(f0, float3(0.90, 0.72, 0.22), 0.62);
	} else if (matSlice >= 40.0 && matSlice < 52.0) {
		rough = lerp(rough, 0.22, 0.45);
		f0 = lerp(f0, float3(0.09, 0.08, 0.06), 0.4);
	} else if (matSlice >= 72.0 && matSlice < 80.0) {
		rough = lerp(rough, 0.08, 0.7);
		f0 = lerp(f0, float3(0.06, 0.08, 0.09), 0.5);
	}
	{
		float rain = RainAmt();
		float coat = saturate(person * 0.88 + hair * 1.05);
		if (rain > 0.01 && coat > 0.02) {
			float up = saturate(n.y * 0.65 + 0.35);
			float drop = fbm2(i.w.xz * 14.0 + i.w.y * 9.0);
			float k = saturate(rain * coat * saturate(0.55 + drop * 0.45) * up);
			albedo *= lerp(1.0, float3(0.62, 0.70, 0.78), k * 0.72);
			rough = lerp(rough, lerp(0.12, 0.06, hair), k * 0.9);
			f0 = lerp(f0, lerp(float3(0.055, 0.06, 0.07), float3(0.08, 0.085, 0.09), hair), k);
		}
	}
	float3 col = Lit(albedo, n, v, rough, f0, sh, skin * 0.85);
	float rim = pow(1. - saturate(dot(n, v)), 3.) * Extra.z;
	// 段ごとの差を連続段で繋ぐ。無印の各段の値は従来どおり。限界突破より上は世界を上げない。
	float rimK = QLerp(QRank(), 0.0, 1.0, 1.0, 1.12, 1.35, 1.5, 1.7, 1.7);
	col += rim * rimK * float3(1, 0.95, 0.85) * (0.35 + 0.65 * sh);
	{
		float thc = FogColor.a;
		float3 tr = float3(1, 0.96, 0.82);
		if (thc < 0.5) tr = float3(1, 0.95, 0.62);
		else if (thc < 1.5) tr = float3(1, 0.62, 0.32);
		else if (thc < 2.5) tr = float3(0.62, 1, 0.5);
		else if (thc < 3.5) tr = float3(0.72, 0.9, 1);
		else if (thc < 4.5) tr = float3(1, 0.78, 0.38);
		else if (thc < 5.5) tr = float3(1, 0.42, 0.12);
		else if (thc < 6.5) tr = float3(0.5, 1, 0.42);
		else if (thc < 7.5) tr = float3(0.5, 0.72, 1);
		else if (thc < 8.5) tr = float3(0.32, 0.78, 1);
		else tr = float3(0.72, 0.92, 1);
		float frC = pow(1.0 - saturate(dot(n, v)), 2.4);
		float rimA = QLerp(QRank(), 0.08, 0.1, 0.14, 0.18, 0.2, 0.24, 0.28, 0.28);
		col += tr * frC * rimA * (0.5 + 0.5 * sh) * (1. - person * 0.8);
	}
	col += emis;
	{
		float rain = RainAmt();
		float coat = saturate(person * 0.88 + hair * 1.05);
		if (rain > 0.01 && coat > 0.02) {
			float3 l = normalize(LightDir.xyz);
			float3 hh = normalize(l + v);
			float ndl = saturate(dot(n, l));
			float ndv = saturate(dot(n, v));
			float ndh = saturate(dot(n, hh));
			float k = saturate(rain * coat * saturate(n.y * 0.65 + 0.35));
			col += float3(0.72, 0.88, 1.0) * pow(ndh, lerp(36.0, 72.0, hair)) * ndl * sh * k * 0.55;
			col += float3(0.55, 0.72, 0.9) * pow(1.0 - ndv, 2.2) * k * 0.28;
			if (hair > 0.5) {
				float3 tng = normalize(cross(abs(n.y) > 0.92 ? float3(1, 0, 0) : float3(0, 1, 0), n));
				float ah = dot(tng, hh);
				float spec = pow(saturate(sqrt(saturate(1. - ah * ah)) * ndh), 64.0);
				col += float3(0.85, 0.95, 1.0) * spec * ndl * k * 0.7;
				float strk = pow(saturate(sin(i.w.y * 28.0 + i.w.x * 6.0) * 0.5 + 0.5), 4.0);
				col += float3(0.7, 0.85, 1.0) * strk * k * 0.18 * sh;
			}
		}
	}
	col = lerp(col, DewShade(col, albedo, n, v, sh, skin, hair), RainAmt() > 0.01 ? 1.0 : (1. - person * 0.6));
	if (person > 0.5 || hair > 0.5)
		col = RainSlide(col, albedo, n, v, i.w, sh, hair, skin);
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

GOut FallSheet(PSIn i, float thw, float3 albedo, float alpha)
{
	alpha = FadeA(alpha);
	float t = Eye.w;
	float g = GustAmt();
	float fromSky = step(i.c.a, 0.82);
	float lean = g * saturate((i.w.y + 9.0) / 28.0);
	float spd = lerp(2.4, 1.55, fromSky);
	float2 flow = float2(i.w.x * 5.4 + i.w.z * 1.8 + lean * 3.2, i.w.y * -2.6 + t * spd);
	float streak = pow(saturate(sin(flow.x * lerp(1.0, 0.72, fromSky)) * 0.5 + 0.5), lerp(1.6, 1.25, fromSky));
	float band = pow(saturate(sin(flow.y * lerp(2.2, 1.6, fromSky)) * 0.5 + 0.5), 3.4);
	float braid = pow(saturate(sin(flow.x * 1.8 + flow.y * 0.55 + lean) * 0.5 + 0.5), 2.2);
	float sheet = pow(saturate(sin(i.uv.x * 12.0 + i.w.y * 0.35 - t * spd) * 0.5 + 0.5), 1.4);
	float3 body = lerp(albedo * 0.52, float3(0.74, 0.9, 1), 0.42 + streak * 0.3);
	if (thw > 4.5 && thw < 5.5) body = lerp(body, float3(1, 0.42, 0.1), 0.4);
	else if (thw > 5.5 && thw < 6.5) body = lerp(body, float3(0.4, 0.62, 0.32), 0.28);
	else if (thw > 2.5 && thw < 3.5) body = lerp(body, float3(0.86, 0.94, 1), 0.22);
	else if (thw > 7.5 && thw < 8.5) body = lerp(body, float3(0.12, 0.4, 0.5), 0.5);
	else if (thw > 9.5 && thw < 10.5) body = lerp(body, float3(0.55, 0.72, 1), 0.28);
	else if (thw > 11.5 && thw < 12.5) body = lerp(body, float3(0.72, 0.62, 0.95), 0.22);
	else if (thw > 12.5) body = lerp(body, float3(1, 0.88, 0.62), 0.18);
	body += float3(1, 1, 1) * band * 0.26;
	body += float3(0.86, 0.95, 1) * braid * 0.2;
	body += float3(0.78, 0.9, 1) * sheet * 0.1;
	float mist = fromSky ? saturate(0.22 + i.uv.y * 0.65) : saturate(0.7 - i.w.y * 0.12);
	body = lerp(body, float3(0.94, 0.97, 1), mist * lerp(0.28, 0.42, fromSky));
	float splash = fromSky * saturate((i.uv.y - 0.76) * 8.0);
	float lip = (1.0 - fromSky) * saturate((0.16 - i.uv.y) * 8.0);
	float land = (1.0 - fromSky) * saturate((i.uv.y - 0.88) * 9.0);
	body = lerp(body, float3(0.96, 0.99, 1), max(splash, max(lip, land)) * 0.5);
	float3 vn = normalize(i.n);
	float fr = pow(1.0 - saturate(dot(vn, normalize(Eye.xyz - i.w))), 2.6);
	body += float3(0.88, 0.95, 1) * fr * 0.2;
	if (Q.x > 2.5) {
		float spark = Sparkle(i.w.xz + i.w.y, t * (1.0 + g * 0.8), 14.0, 0.965);
		body += float3(1, 1, 1) * spark * (0.08 + (splash + lip + land) * 0.22);
	}
	float cut = fromSky ? saturate((0.98 - i.uv.y) * 16.0) : 1.0;
	float a = saturate(alpha * (0.58 + streak * 0.28 + braid * 0.08 + mist * 0.08 + (splash + lip + land) * 0.18) * cut);
	return Emit(float4(GhostCol(FogAt(body, i.w)), a), i.w);
}

float StageTier()
{
	if (Q.x > 3.5 && Q.x < 4.5) return 0;
	if (Q.x < 0.5) return 1;
	if (Q.x < 1.5) return 2;
	if (Q.x < 2.5) return 3;
	if (Q.x < 3.5) return 4;
	if (Q.x < 5.5) return 5;
	return 6;
}

float ThemeWet(float theme)
{
	if (theme < 0.5) return 0.14;
	if (theme < 1.5) return 0.24;
	if (theme < 2.5) return 0.11;
	if (theme < 3.5) return 0.62;
	if (theme < 4.5) return 0.16;
	if (theme < 5.5) return 0.30;
	if (theme < 6.5) return 0.50;
	if (theme < 7.5) return 0.34;
	if (theme < 8.5) return 0.78;
	if (theme < 9.5) return 0.70;
	if (theme < 10.5) return 0.08;
	if (theme < 11.5) return 0.22;
	if (theme < 12.5) return 0.30;
	return 0.20;
}

float3 SphereLookup(float3 r)
{
	float m = 2.0 * sqrt(max(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0), 1e-5));
	return SphereMap.SampleLevel(CL, r.xy / m + 0.5, 0).rgb;
}

float RainFilm()
{
	float w = Wx.x;
	if (w > 0.5 && w < 1.5) return 1.0;
	if (w > 4.5 && w < 5.5) return 1.0;
	return 0.0;
}

float FrostFilm()
{
	float w = Wx.x;
	if (w > 1.5 && w < 2.5) return 1.0;
	if (w > 6.5 && w < 7.5) return 0.7;
	return 0.0;
}

// 上を向いた面ほど映す。雪と氷は素地、雨は濡れた面を常に、沼や磨いた石もそれぞれの強さ。
float FloorGloss(float theme, float face, float2 xz)
{
	float g = 0.12;
	if (theme < 0.5) g = 0.14;
	else if (theme < 1.5) g = 0.22;
	else if (theme < 2.5) g = 0.16;
	else if (theme < 3.5) {
		float ice = smoothstep(0.48, 0.74, fbm2(xz * 0.33 + 1.7));
		g = lerp(0.52, 0.94, ice);
	} else if (theme < 4.5) g = 0.06;
	else if (theme < 5.5) g = 0.04;
	else if (theme < 6.5) {
		float pud = smoothstep(0.42, 0.72, fbm2(xz * 0.55));
		g = lerp(0.18, 0.64, pud);
	} else if (theme < 7.5) g = 0.28;
	else if (theme < 8.5) g = 0.6;
	else if (theme < 9.5) g = 0.48;
	else if (theme < 10.5) g = 0.26;
	else if (theme < 11.5) g = 0.46;
	else if (theme < 12.5) g = 0.52;
	else g = 0.24;
	float rain = RainFilm();
	if (rain > 0.0 && !(theme > 4.5 && theme < 5.5)) {
		float pud = smoothstep(0.34, 0.7, fbm2(xz * 0.62 + 2.4));
		g = max(g, lerp(0.5, 0.84, pud));
	}
	float frost = FrostFilm();
	if (frost > 0.0 && !(theme > 4.5 && theme < 5.5)) {
		float sheet = smoothstep(0.34, 0.68, fbm2(xz * 0.48 + 4.0));
		g = max(g, lerp(0.36, 0.8, sheet) * frost);
	}
	if (theme > 3.5 && theme < 4.5 && rain < 0.5 && frost < 0.5) g = 0.05;
	return g * lerp(0.2, 1.0, saturate(face));
}

// 標準から環境キューブ。軽量と超軽量はサンプルしない。
bool ReflectMap(float3 n, float3 v, float rough, out float3 env)
{
	float tier = StageTier();
	if (tier < 1.5) { env = 0; return false; }
	n = normalize(n);
	float3 r = reflect(-v, n);
	float mip = saturate(rough) * (tier > 4.5 ? 2.6 : (tier > 2.5 ? 3.6 : 4.6));
	env = EnvMap.SampleLevel(CL, r, mip).rgb;
	if (tier < 3.5)
		env = lerp(SphereLookup(r), env, tier < 2.5 ? 0.74 : 0.88);
	return true;
}

void ApplyReflect(inout float3 col, float3 n, float3 v, float3 w, float rough, float gloss)
{
	if (gloss < 0.04) return;
	float3 env;
	if (!ReflectMap(n, v, rough, env)) return;
	if (ReflectInfo.x > 0.5) {
		float3 p = w;
		p.y = ReflectInfo.y;
		float4 rp = mul(float4(p, 1), ReflectVP);
		if (rp.w > 0.001) {
			float2 uv = rp.xy / rp.w * float2(0.5, -0.5) + 0.5;
			float2 box = saturate(min(uv, 1.0 - uv) * 8.0);
			float keep = min(box.x, box.y);
			if (keep > 0.02) {
				float3 hit;
				float b = ReflectInfo.w;
				if (b > 0.00005) {
					hit = LiveRef.SampleLevel(CL, uv, 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv + float2(b, 0), 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv - float2(b, 0), 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv + float2(0, b), 0).rgb;
					hit += LiveRef.SampleLevel(CL, uv - float2(0, b), 0).rgb;
					hit *= 0.2;
				} else hit = LiveRef.SampleLevel(CL, uv, 0).rgb;
				env = lerp(env, hit, keep);
			}
		}
	}
	float fres = pow(1.0 - saturate(dot(normalize(n), v)), 2.1);
	float facing = 0.4;
	float cap = lerp(0.46, 0.62, saturate(gloss));
	if (ReflectInfo.x > 0.5) {
		if (Q.x > 5.5) { facing = 0.9; cap = lerp(0.74, 0.88, saturate(gloss)); }
		else if (Q.x > 4.5) { facing = 0.82; cap = lerp(0.64, 0.78, saturate(gloss)); }
		else if (Q.x > 2.5) { facing = 0.76; cap = lerp(0.56, 0.72, saturate(gloss)); }
		else { facing = 0.68; cap = lerp(0.5, 0.66, saturate(gloss)); }
	}
	float amt = min(gloss * lerp(facing, 0.94, fres), cap);
	col = lerp(col, env, amt);
}

GOut PSWater(PSIn i)
{
	BeginLiquidGhost(i.pos, i.w, i.n.y);
	if (ReflectInfo.z > 0.5 && ReflectInfo.z < 1.5 && i.n.y > 0.45) discard;
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
	float specK = QLerp(QRank(), 0.0, 1.0, 1.0, 1.15, 1.4, 1.6, 1.85, 1.85);
	col += float3(1, 0.97, 0.88) * sp * (thw > 2.5 && thw < 3.5 ? 1.6 : 0.9) * specK;
	ApplyReflect(col, wn, v, i.w, 0.05, 0.96);
	col = lerp(col, float3(0.9, 0.95, 0.98), crest * (thw > 5.5 && thw < 6.5 ? 0.15 : 0.42));
	float caus = CausAt(i.w.xz, t);
	col += albedo * caus * 0.22;
	float wrap = saturate(dot(wn, L) * 0.35 + 0.65);
	col += albedo * float3(0.15, 0.45, 0.55) * wrap * (1.0 - ndv) * 0.18;
	if (Q.x > 2.5) {
		float cap = fbm2(i.w.xz * 9.5 + t * 0.55);
		col += float3(0.85, 0.95, 1) * pow(cap, 4) * crest * 0.16;
		col += float3(1, 1, 1) * Sparkle(i.w.xz, t, 36.0, 0.96) * crest * 0.2;
	}
	float see = 0.16;
	if (thw > 2.5 && thw < 3.5) { col = lerp(col, float3(0.82, 0.92, 1), 0.28); see = 0.12; }
	else if (thw > 0.5 && thw < 1.5) { col = lerp(col, albedo * float3(0.95, 0.72, 0.45), 0.22); see = 0.34; }
	else if (thw > 1.5 && thw < 2.5) { col *= float3(0.82, 0.96, 0.78); see = 0.26; }
	else if (thw > 3.5 && thw < 4.5) { col = lerp(col, albedo * float3(0.7, 0.9, 0.88), 0.18); see = 0.2; }
	else if (thw > 4.5 && thw < 5.5) { col = lerp(col, albedo * float3(0.55, 0.32, 0.22), 0.3); see = 0.4; }
	else if (thw > 5.5 && thw < 6.5) { col = lerp(col, albedo * float3(0.45, 0.62, 0.38), 0.35); see = 0.42; }
	else if (thw > 8.5 && thw < 9.5) {
		float boil = pow(saturate(sin(i.w.z * 2.4 - t * 3.4) * 0.5 + 0.5), 2);
		col = lerp(col, float3(0.9, 0.97, 1), boil * 0.45);
		col += float3(1, 1, 1) * crest * 0.18;
		see = 0.26;
	} else if (thw > 7.5 && thw < 8.5) {
		col = lerp(col, float3(0.04, 0.22, 0.32), 0.35);
		col += float3(0.45, 0.85, 1) * caus * 0.22;
		float bio = Sparkle(i.w.xz, t, 8.0, 0.91);
		col += float3(0.2, 0.9, 0.75) * bio * 0.16;
		see = 0.42;
	} else if (thw > 12.5) {
		col = lerp(col, float3(0.55, 0.62, 0.82), 0.18);
		col += float3(1, 0.82, 0.45) * caus * 0.08;
		see = 0.28;
	} else if (thw > 9.5 && thw < 10.5) {
		col = lerp(col, float3(0.28, 0.38, 0.62), 0.28);
		col += float3(0.7, 0.82, 1) * fr * 0.32;
		see = 0.38;
	} else if (thw > 6.5) { col = lerp(col, SkyAmb.rgb, fr * 0.22); see = 0.38; }
	if (rush > 0.28) {
		float streak = pow(saturate(sin(dot(i.w.xz, flow) * 10.5 - t * spd * 7.2) * 0.5 + 0.5), 2.4);
		float foam = pow(saturate(sin(dot(i.w.xz, float2(-flow.y, flow.x)) * 6.0 + t * 3.1) * 0.5 + 0.5), 4);
		col = lerp(col, float3(0.82, 0.93, 1), streak * rush * 0.72);
		col += float3(1, 1, 1) * foam * rush * 0.35;
		see = lerp(see, 0.16, rush * 0.55);
	}
	return Emit(float4(GhostCol(FogAt(col, i.w)), FadeA(lerp(see, 0.78, fr) * alpha)), i.w);
}

float3 LavaColor(float2 xz, float t, float3 v)
{
	float2 vg = VoronoiGap(xz * 0.72 + float2(t * 0.06, t * 0.02));
	float crack = 1 - smoothstep(0.015, 0.11, vg.y);
	float pulse = 0.82 + 0.18 * sin(t * 2.2 + vg.x * 9);
	float crust = smoothstep(0.08, 0.22, vg.y);
	float3 hot = lerp(float3(1, 0.72, 0.18), float3(1, 0.22, 0.02), saturate(vg.y * 8));
	float3 col = lerp(float3(0.12, 0.035, 0.02), hot * (1.5 * pulse), crack);
	col = lerp(col, float3(0.08, 0.04, 0.03), crust * 0.55);
	float3 H = normalize(normalize(LightDir.xyz) + v);
	col += hot * pow(saturate(dot(float3(0, 1, 0), H)), 16) * crack * 0.35;
	float bubble = pow(saturate(sin(vg.x * 18.0 - t * 3.4) * 0.5 + 0.5), 8) * crack;
	col += float3(1, 0.85, 0.35) * bubble * 0.45;
	if (Q.x > 2.5) {
		float ember = Sparkle(xz, t, 12.0, 0.93);
		col += float3(1, 0.4, 0.05) * ember * 0.22;
		col += float3(1, 0.15, 0.02) * pow(1.0 - saturate(dot(float3(0, 1, 0), v)), 3) * crack * 0.18;
	}
	return col;
}

GOut LavaFall(PSIn i)
{
	float t = Eye.w;
	float g = GustAmt();
	float u = i.uv.x;
	float v = i.uv.y;
	float rope = pow(saturate(sin(u * 14.0 + i.w.y * 0.28 - t * 0.48) * 0.5 + 0.5), 1.25);
	float drip = pow(saturate(sin(u * 7.5 - t * 0.22 + i.w.y * 0.16) * 0.5 + 0.5), 3.4);
	float blob = exp(-pow(frac(v * 1.8 - t * 0.16 + u * 2.4) - 0.62, 2) * 28.0);
	float sag = saturate(v * 1.08);
	float3 hot = lerp(float3(0.42, 0.06, 0.01), float3(1.0, 0.42, 0.04), rope);
	hot = lerp(hot, float3(1.0, 0.78, 0.18), drip * 0.62);
	hot += float3(1.0, 0.32, 0.02) * blob * 0.85;
	hot += float3(1.0, 0.18, 0.01) * g * sag * 0.12;
	float edge = saturate(abs(u - 0.5) * 2.05);
	hot = lerp(hot, float3(0.16, 0.04, 0.015), edge * 0.5);
	float3 vn = normalize(i.n);
	float fr = pow(1.0 - saturate(dot(vn, normalize(Eye.xyz - i.w))), 2.2);
	hot += float3(1, 0.55, 0.12) * fr * 0.22;
	float a = saturate((0.88 + rope * 0.18) * (0.62 + sag * 0.38) * (1.0 - edge * 0.22));
	return Emit(float4(GhostCol(FogAt(hot, i.w)), GhostA(a)), i.w);
}

GOut PSLava(PSIn i)
{
	BeginLiquidGhost(i.pos, i.w, i.n.y);
	if (ReflectInfo.z > 0.5 && ReflectInfo.z < 1.5 && i.n.y > 0.72) discard;
	float3 v = normalize(Eye.xyz - i.w);
	float t = Eye.w;
	if (i.n.y < 0.35)
		return LavaFall(i);
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
	ApplyReflect(col, float3(0, 1, 0), v, i.w, 0.62, 0.12);
	if (Q.x > 1.5) {
		float heat = pow(0.5 + 0.5 * sin(i.w.x * 3.2 + t * 1.6) * sin(i.w.z * 2.7 - t * 1.1), 2);
		col += float3(1, 0.25, 0.02) * heat * 0.08;
	}
	return Emit(float4(GhostCol(col), GhostA(1)), i.w);
}

// 14マップそれぞれの地表エフェクト。キャラと移動マスは別で描くので、地面の上はある程度重なってもよい。
void GroundSurfaceLayer(float theme, float3 w, float3 n, float cliff, float t, float gust, inout float3 albedo, inout float3 nLit, inout float rough, inout float3 glow)
{
	float up = saturate((n.y - 0.55) * 3.0) * (1.0 - cliff);
	if (up < 0.02) return;
	float2 xz = w.xz;
	[branch] if (theme < 0.5) {
		float2 windP = xz * float2(0.55, 0.22) - float2(t * (1.6 + gust * 1.4), t * 0.35);
		float wave = fbm3(windP) * fbm3(windP * 2.1 + 3.7);
		float2 bladeG = xz * 18.0 + Curl2(xz * 0.9 - t * 0.4) * 1.6;
		float blade = smoothstep(0.58, 0.88, fbm3(bladeG));
		float2 petG = xz * 2.8 - float2(t * 0.9, t * 0.25);
		float2 petC = floor(petG);
		float2 petF = frac(petG) - 0.5 - (hash22(petC) - 0.5) * 0.45;
		float petal = smoothstep(0.11, 0.02, length(petF)) * step(0.82, hash21(petC));
		albedo = lerp(albedo, float3(0.46, 0.72, 0.26), blade * up * 0.38);
		albedo = lerp(albedo, float3(0.78, 0.92, 0.48), smoothstep(0.28, 0.62, wave) * up * 0.42);
		albedo = lerp(albedo, float3(1.0, 0.92, 0.58), petal * up * 0.65);
		glow += float3(0.65, 0.88, 0.32) * smoothstep(0.38, 0.72, wave) * up * 0.22;
		glow += float3(1.0, 0.95, 0.62) * petal * up * 0.38;
	} else if (theme < 1.5) {
		float2 dp = xz * float2(0.48, 0.95) - float2(t * (1.2 + gust * 1.1), t * 0.28);
		float3 c = float3(fbm2(dp), fbm2(dp * 2.3 + 5.1), fbm2(xz * 6.5 - t * 2.2));
		float devil = smoothstep(0.42, 0.78, c.x * 0.65 + c.y * 0.55);
		float2 v = VoronoiGap(xz * 3.4);
		float crack = 1.0 - smoothstep(0.015, 0.055, v.y);
		albedo = lerp(albedo, float3(0.32, 0.18, 0.1), crack * up * 0.55);
		albedo = lerp(albedo, float3(0.92, 0.64, 0.38), devil * up * 0.52);
		glow += float3(1.0, 0.66, 0.32) * devil * (0.28 + c.z * 0.24) * up;
	} else if (theme < 2.5) {
		float2 rootV = VoronoiGap(xz * 1.35 + Curl2(xz * 0.4) * 0.35);
		float root = 1.0 - smoothstep(0.03, 0.11, rootV.y);
		float2 canP = xz * 0.72 + float2(sin(t * 0.35) * 0.4, cos(t * 0.28) * 0.4);
		float sunPatch = pow(saturate(fbm3(canP) * 1.35 - 0.25), 2.2);
		float2 spG = xz * 3.6 + float2(sin(t * 0.4 + xz.y), cos(t * 0.35 - xz.x)) * 0.35;
		float2 spC = floor(spG);
		float spore = smoothstep(0.14, 0.02, length(frac(spG) - 0.5 - (hash22(spC) - 0.5) * 0.4)) * step(0.76, hash21(spC));
		albedo = lerp(albedo, float3(0.18, 0.34, 0.14), root * up * 0.55);
		albedo = lerp(albedo, float3(0.58, 0.84, 0.34), sunPatch * up * 0.38);
		glow += float3(0.82, 1.0, 0.42) * sunPatch * up * 0.28;
		glow += float3(0.55, 1.0, 0.48) * spore * up * 0.52;
	} else if (theme < 3.5) {
		float2 sp = xz * float2(0.85, 0.32) - float2(t * (1.8 + gust * 1.6), t * 0.4);
		float spindrift = smoothstep(0.38, 0.76, fbm2(sp) * 0.65 + fbm2(sp * 2.7) * 0.5);
		float2 v1 = VoronoiGap(xz * 1.8);
		float2 v2 = VoronoiGap(xz * 4.2 + 2.3);
		float fracLine = (1.0 - smoothstep(0.01, 0.045, v1.y)) + (1.0 - smoothstep(0.012, 0.05, v2.y)) * 0.6;
		albedo = lerp(albedo, float3(0.52, 0.78, 0.96), saturate(fracLine) * up * 0.48);
		albedo = lerp(albedo, float3(0.96, 0.98, 1.0), spindrift * up * 0.54);
		rough = lerp(rough, 0.06, saturate(fracLine * 0.5) * up);
		glow += float3(0.78, 0.92, 1.0) * (spindrift * 0.26 + saturate(fracLine) * 0.22) * up;
	} else if (theme < 4.5) {
		float2 rp = xz * float2(6.2, 1.4) + sin(xz.yx * 1.8) * 0.65;
		float ridge = 0.5 + 0.5 * sin(rp.x - t * 0.8 + fbm2(xz * 2.0) * 3.0);
		float2 streamP = xz * float2(0.55, 2.2) - float2(t * (2.4 + gust * 1.8), t * 0.4);
		float stream = smoothstep(0.42, 0.78, fbm2(streamP) * 0.7 + fbm2(streamP * 2.4) * 0.45);
		albedo = lerp(albedo, float3(0.96, 0.78, 0.46), ridge * up * 0.34);
		albedo = lerp(albedo, float3(1.0, 0.88, 0.58), stream * up * 0.52);
		nLit = normalize(nLit + float3(cos(rp.x) * 0.22, 0, sin(rp.x) * 0.08) * up);
		glow += float3(1.0, 0.8, 0.38) * stream * up * 0.26;
	} else if (theme < 5.5) {
		float2 v1 = VoronoiGap(xz * 1.15 + Curl2(xz * 0.35) * 0.25);
		float2 v2 = VoronoiGap(xz * 2.8 - float2(t * 0.04, 0));
		float magma = (1.0 - smoothstep(0.018, 0.085, v1.y)) + (1.0 - smoothstep(0.015, 0.06, v2.y)) * 0.55;
		float ash = smoothstep(0.36, 0.74, fbm2(xz * 0.9 - float2(t * 0.45, t * 0.22)));
		float throb = 0.72 + 0.28 * sin(t * 2.6 + v1.x * 8.0);
		albedo = lerp(albedo, float3(0.12, 0.09, 0.08), ash * up * 0.58);
		albedo = lerp(albedo, float3(1.0, 0.32, 0.04), saturate(magma) * up * 0.62);
		glow += float3(1.0, 0.36, 0.04) * saturate(magma) * throb * up * 0.78;
	} else if (theme < 6.5) {
		float2 mp = xz * 0.68 + Curl2(xz * 0.4 + t * 0.08) * 0.85;
		float miasma = smoothstep(0.34, 0.72, fbm2(mp + float2(t * 0.22, -t * 0.14)));
		float2 rg = xz * 2.4;
		float2 rc = floor(rg);
		float2 rf = frac(rg) - 0.5 - (hash22(rc) - 0.5) * 0.35;
		float ph = frac(t * 0.45 + hash21(rc));
		float ring = exp(-abs(length(rf) - ph * 0.42) * 28.0) * (1.0 - ph) * step(0.58, hash21(rc + 4.1));
		albedo = lerp(albedo, float3(0.24, 0.44, 0.18), miasma * up * 0.52);
		rough = lerp(rough, 0.08, miasma * up * 0.6);
		glow += float3(0.38, 0.92, 0.28) * (miasma * 0.26 + ring * 0.85) * up;
	} else if (theme < 7.5) {
		float2 tile = frac(xz) - 0.5;
		float2 tid = floor(xz);
		float th = hash21(tid);
		float r = length(tile);
		float circ = exp(-abs(r - 0.36) * 42.0) + exp(-abs(r - 0.22) * 48.0) * 0.7;
		float spoke = exp(-abs( sin(atan2(tile.y, tile.x) * 4.0 + th * 6.0) ) * 16.0) * step(r, 0.36);
		float sigil = saturate(circ + spoke * 0.65) * step(0.62, th) * (0.55 + 0.45 * sin(t * 1.8 + th * 12.0));
		float dust = smoothstep(0.42, 0.76, fbm3(xz * 0.85 + float2(t * 0.18, -t * 0.12)));
		albedo = lerp(albedo, float3(0.52, 0.78, 1.0), sigil * up * 0.48);
		glow += float3(0.42, 0.76, 1.0) * sigil * up * 0.72;
		glow += float3(0.55, 0.7, 0.95) * dust * up * 0.18;
	} else if (theme < 8.5) {
		float c1 = CausAt(xz * 1.35 + Curl2(xz * 0.5 + t * 0.1) * 0.35, t * 1.2);
		float c2 = CausAt(xz * 2.6 - float2(t * 0.18, t * 0.11), t * 0.85);
		float net = saturate(c1 * 0.7 + c2 * 0.55);
		float2 bg = xz * 3.8 + float2(sin(t * 0.6 + xz.y), cos(t * 0.5 - xz.x)) * 0.25;
		float2 bc = floor(bg);
		float bio = smoothstep(0.12, 0.015, length(frac(bg) - 0.5 - (hash22(bc) - 0.5) * 0.4)) * step(0.68, hash21(bc));
		albedo = lerp(albedo, float3(0.28, 0.78, 0.95), net * up * 0.45);
		glow += float3(0.24, 0.82, 1.0) * net * up * 0.42;
		glow += float3(0.15, 1.0, 0.78) * bio * (0.6 + 0.4 * sin(t * 3.0 + hash21(bc) * 20.0)) * up * 0.65;
	} else if (theme < 9.5) {
		float2 sp = xz * float2(1.4, 0.65) - float2(0, t * 1.8);
		float mist = smoothstep(0.32, 0.72, fbm2(sp) * 0.65 + fbm3(sp * 2.4 + 1.9) * 0.5);
		float streak = pow(0.5 + 0.5 * sin(xz.x * 7.0 - t * 3.4), 6.0);
		float2 rg = xz * 3.2;
		float2 rc = floor(rg);
		float ph = frac(t * 0.85 + hash21(rc));
		float ring = exp(-abs(length(frac(rg) - 0.5) - ph * 0.44) * 32.0) * (1.0 - ph);
		float ph2 = frac(t * 1.15 + hash21(rc + 2.0));
		float ring2 = exp(-abs(length(frac(rg * 1.65) - 0.5) - ph2 * 0.36) * 26.0) * (1.0 - ph2);
		albedo = lerp(albedo, float3(0.86, 0.95, 1.0), mist * up * 0.5);
		rough = lerp(rough, 0.08, up * 0.55);
		glow += float3(0.78, 0.92, 1.0) * (mist * 0.36 + ring * 0.48 + ring2 * 0.32) * up;
		glow += float3(0.92, 0.98, 1.0) * streak * mist * up * 0.28;
	} else if (theme < 10.5) {
		float2 np = xz * 0.52 + Curl2(xz * 0.28 - t * 0.05) * 0.6;
		float neb = smoothstep(0.34, 0.74, fbmSky(np + t * 0.08));
		float2 hex = abs(frac(xz * 2.0) - 0.5);
		float grid = smoothstep(0.45, 0.495, max(hex.x, hex.y));
		float pulse = 0.5 + 0.5 * sin(t * 2.2 - length(xz - 32.0) * 0.45);
		float3 nCol = lerp(float3(0.52, 0.18, 0.95), float3(0.15, 0.65, 1.0), fbmSky(np * 1.9 + 3.0));
		albedo = lerp(albedo, nCol, neb * up * 0.46);
		glow += nCol * neb * up * 0.38;
		glow += float3(0.45, 0.82, 1.0) * grid * (0.35 + pulse * 0.65) * up * 0.55;
	} else if (theme < 11.5) {
		float2 g1 = abs(frac(xz) - 0.5);
		float2 g4 = abs(frac(xz * 4.0) - 0.5);
		float seam = smoothstep(0.465, 0.495, max(g1.x, g1.y));
		float trace = smoothstep(0.45, 0.49, min(g4.x, g4.y)) * step(0.55, hash21(floor(xz * 4.0)));
		float wave = pow(0.5 + 0.5 * sin(xz.x * 0.55 + xz.y * 0.35 - t * 2.4), 6.0);
		float steam = smoothstep(0.46, 0.78, fbmSky(xz * 0.9 + float2(t * 0.35, -t * 0.18)));
		albedo = lerp(albedo, float3(0.18, 0.24, 0.3), seam * up * 0.55);
		glow += float3(0.2, 0.88, 1.0) * (trace * (0.4 + wave * 0.8) + seam * wave * 0.45) * up * 0.65;
		glow += float3(0.65, 0.8, 0.92) * steam * up * 0.2;
	} else if (theme < 12.5) {
		float2 tile = frac(xz * 0.5) - 0.5;
		float2 tid = floor(xz * 0.5);
		float r = length(tile);
		float tracery = exp(-abs(r - 0.38) * 36.0) + exp(-abs(abs(tile.x) - abs(tile.y)) * 42.0) * step(r, 0.38);
		float glass = smoothstep(0.38, 0.74, fbmSky(xz * 0.65 + float2(0.08, 0.05) * t));
		float3 gCol = lerp(float3(0.75, 0.32, 1.0), float3(1.0, 0.72, 0.28), fbmSky(xz * 0.9 + 4.2));
		albedo = lerp(albedo, gCol, glass * up * 0.36);
		glow += gCol * glass * up * 0.32;
		glow += float3(0.88, 0.68, 1.0) * saturate(tracery) * (0.55 + 0.45 * sin(t * 1.6 + hash21(tid) * 8.0)) * up * 0.52;
	} else {
		float2 g = xz * 1.25 + float2(sin(t * 0.25 + xz.y * 0.2), cos(t * 0.2 - xz.x * 0.2)) * 0.25;
		float2 lc = floor(g);
		float2 lf = frac(g) - 0.5 - (hash22(lc) - 0.5) * 0.35;
		float lh = hash21(lc);
		float pool = exp(-dot(lf, lf) * 14.0) * step(0.52, lh);
		float2 mistP = xz * 0.62 + Curl2(xz * 0.35 + t * 0.08) * 0.55;
		float mist = smoothstep(0.36, 0.74, fbm2(mistP - float2(t * 0.16, t * 0.08)));
		albedo = lerp(albedo, float3(1.0, 0.76, 0.36), pool * up * 0.45);
		albedo = lerp(albedo, float3(0.72, 0.78, 0.96), mist * up * 0.38);
		glow += float3(1.0, 0.74, 0.32) * pool * (0.75 + 0.25 * sin(t * 2.4 + lh * 15.0)) * up * 0.72;
		glow += float3(0.95, 0.82, 0.55) * mist * up * 0.22;
	}
}

GOut GroundPS(PSIn i, float theme)
{
	BeginGhost(i.pos, i.w, i.n.y);
	if (ReflectInfo.z > 0.5 && ReflectInfo.z < 1.5 && i.n.y > 0.72) discard;
	// 水底のコースティクスは氷と同じ高さでも、足元以外は水面と同様に透かす。
	if (i.c.a < 0.999) {
		float gBed = LiquidAway(i.w);
		if (gBed > sGhost) { sGhost = gBed; ClipGhost(i.pos); }
	}
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	float cliff = n.y > 0.72 ? 0 : 1;
	float2 uv = i.w.xz * (cliff > 0.5 ? 0.26 : 0.4);
	// 水底だけ頂点αを 254 にしてある。水面と同じ波でテクスチャをずらし、底がゆれて見える。
	float bedCaus = 0;
	float2 bedDh = 0;
	if (cliff < 0.5) {
		float g = GustAmt();
		if (i.c.a < 0.999) {
			float h;
			float2 dh;
			WaterWave(i.w.xz, Eye.w, WaveAmp((float)theme), h, dh);
			float2 rdh;
			RippleH(i.w.xz, Eye.w, rdh);
			uv += (dh + rdh) * 0.16;
			bedCaus = CausAt(i.w.xz, Eye.w);
			bedDh = dh + rdh;
		}
		if (theme > 7.5 && theme < 8.5) {
			float hG;
			float2 dhG;
			WaterWave(i.w.xz - float2(0.31, -0.19) * Eye.w * (0.32 + g * 0.7), Eye.w, 0.028 + g * 0.03, hG, dhG);
			uv += dhG * (0.055 + g * 0.05);
			bedDh += dhG * (0.45 + g * 0.4);
			bedCaus = max(bedCaus, CausAt(i.w.xz - float2(0.18, -0.1) * Eye.w * 0.4, Eye.w) * (0.35 + g * 0.25));
		}
	}
	// 配列は 0..7 がアルベド、8..15 が法線。海底・滝・追加4テーマは実写を床にする。
	bool dedicated = theme > 7.5;
	float texTheme = theme;
	float4 alb = dedicated ? float4(0.42, 0.42, 0.44, 1) : Ground.Sample(SL, float3(uv, texTheme));
	float4 nr = dedicated ? float4(0.5, 0.5, 1, 1) : Ground.Sample(SL, float3(uv, texTheme + 8));
	float3 albedo = alb.rgb * lerp(0.78, 1, alb.a) * i.c.rgb;
	if (cliff > 0.5) albedo *= 0.8;
	float3 nLit = DetailN(Bump(n, nr.rgb), i.w);
	float rough = 0.7;
	float3 glow = 0;
	float3 f0 = float3(0.035, 0.035, 0.035);
	float burst = GustAmt();
	float3 Hsun = normalize(normalize(LightDir.xyz) + v);
	float ndhG = saturate(dot(nLit, Hsun));
	float2 curl = Curl2(i.w.xz * 0.35 + Eye.w * 0.08);
	if (theme < 0.5) {
		rough = 0.8;
		float sway = sin(i.w.x * 0.48 + i.w.z * 0.27 - Eye.w * 0.52);
		float roll = 0.5 + 0.5 * sin(i.w.x * 0.16 - Eye.w * 0.28 + i.w.z * 0.11);
		albedo *= 0.96 + sway * 0.05 * (1 - cliff);
		albedo = lerp(albedo, albedo * float3(0.9, 0.96, 0.72), roll * burst * 0.1 * (1 - cliff));
		nLit = normalize(nLit + float3(sway * 0.1 + curl.x * 0.05, 0.03, curl.y * 0.05) * (1 - cliff) * (1 + burst * 0.35));
		float pore = fbm3(i.w.xz * 4.2);
		albedo *= 0.95 + pore * 0.08;
		glow += float3(0.92, 0.88, 0.55) * Sparkle(i.w.xz, Eye.w * 0.22, 5.5, 0.992) * (1 - cliff) * 0.06;
	} else if (theme < 1.5) {
		rough = 0.58;
		if (cliff > 0.5) {
			float groove = 1 - smoothstep(0.012, 0.05, abs(frac(i.w.y * 1.15) - 0.5));
			albedo *= 1 - groove * 0.5;
			nLit = normalize(nLit + float3(0, cos(i.w.y * 7.2) * 0.4, 0));
			float strata = 0.5 + 0.5 * sin(i.w.y * 4.6);
			albedo = lerp(albedo, albedo * float3(1.15, 0.82, 0.58), strata * 0.22);
		}
		float dust = fbm2(i.w.xz * 0.12 + float2(Eye.w * 0.03, Eye.w * 0.01) + curl * 0.2);
		albedo = lerp(albedo, albedo * float3(1.22, 0.86, 0.62), dust * 0.24);
		glow += float3(0.85, 0.62, 0.32) * pow(dust, 2) * 0.1;
		glow += float3(1, 0.78, 0.42) * Sparkle(i.w.xz, Eye.w, 16.0, 0.97) * 0.18;
		nLit = normalize(nLit + float3(curl.x, 0, curl.y) * 0.08);
		float heat = pow(0.5 + 0.5 * sin(i.w.x * 1.1 + Eye.w * 1.4), 3);
		glow += float3(1, 0.55, 0.22) * heat * (1 - cliff) * 0.05;
	} else if (theme < 2.5) {
		rough = 0.38;
		f0 = float3(0.05, 0.055, 0.045);
		float dapple = pow(saturate(sin(i.w.x * 0.85 + Eye.w * 0.35) * sin(i.w.z * 0.7 - Eye.w * 0.22)), 2);
		float dapple2 = pow(saturate(sin(i.w.x * 1.55 - Eye.w * 0.18) * sin(i.w.z * 1.25 + Eye.w * 0.14)), 3);
		glow += float3(1.0, 0.92, 0.45) * dapple * 0.18;
		glow += float3(0.7, 1.0, 0.4) * dapple2 * 0.08;
		albedo *= 0.9 + dapple * 0.14;
		float moss = 1 - smoothstep(0.02, 0.08, VoronoiGap(i.w.xz * 0.7).y);
		albedo = lerp(albedo, albedo * float3(0.55, 0.78, 0.42), moss * 0.22 * (1 - cliff));
		glow += float3(1, 0.95, 0.55) * Sparkle(i.w.xz, Eye.w, 20.0, 0.97) * 0.12 * dapple;
		nLit = normalize(nLit + float3(curl.x, 0.04, curl.y) * 0.07 * (1 - cliff));
	} else if (theme < 3.5) {
		rough = 0.2;
		f0 = float3(0.065, 0.07, 0.078);
		float spark = Sparkle(i.w.xz + i.w.y, Eye.w, 14.0, 0.955) * pow(ndhG, 28);
		glow += float3(0.9, 0.96, 1) * spark * 0.42;
		float drift = pow(0.5 + 0.5 * sin(i.w.x * 0.7 + Eye.w * 0.6 + burst), 4);
		glow += float3(0.8, 0.9, 1) * drift * 0.1;
		albedo = lerp(albedo, float3(0.92, 0.95, 1), 0.1);
		float sastrugi = 0.5 + 0.5 * sin(i.w.x * 2.4 + i.w.z * 0.35);
		nLit = normalize(nLit + float3(sastrugi * 0.22, 0.05, 0.08) * (1 - cliff));
		float ice = pow(1.0 - saturate(dot(nLit, v)), 2.6);
		glow += float3(0.7, 0.88, 1) * ice * 0.12;
		float sheet = smoothstep(0.48, 0.74, fbm2(i.w.xz * 0.33 + 1.7)) * saturate(n.y);
		albedo = lerp(albedo, float3(0.66, 0.82, 0.94), sheet * 0.62);
		rough = lerp(rough, 0.04, sheet);
		float2 vgIce = VoronoiGap(i.w.xz * 0.9);
		float crack = 1.0 - smoothstep(0.008, 0.04, vgIce.y);
		albedo = lerp(albedo, albedo * float3(0.5, 0.68, 0.82), crack * sheet * 0.7);
		nLit = normalize(nLit + float3(sin(i.w.x * 6.2), 0.04, cos(i.w.z * 5.4)) * sheet * 0.16);
		albedo *= 0.94 + fbm2(i.w.xz * 6.2) * 0.1;
	} else if (theme < 4.5) {
		rough = 0.74;
		nLit = normalize(nLit + float3(cos(i.w.x * 1.7 + Eye.w * (0.6 + burst * 1.4)) * 0.18, 0.04, sin(Eye.w * 1.1 + i.w.z) * 0.12) + float3(curl.x, 0, curl.y) * 0.1);
		float sand = pow(0.5 + 0.5 * sin(i.w.x * 0.8 + i.w.z * 0.25 - Eye.w * 1.8), 3);
		float ripple = pow(0.5 + 0.5 * sin(i.w.x * 4.8 + i.w.z * 0.9), 4);
		albedo = lerp(albedo, albedo * float3(1.25, 1.05, 0.7), sand * 0.24);
		albedo = lerp(albedo, albedo * float3(1.12, 0.95, 0.62), ripple * 0.12);
		glow += float3(1, 0.82, 0.4) * sand * 0.07;
		glow += float3(1, 0.9, 0.55) * Sparkle(i.w.xz, Eye.w, 18.0, 0.96) * 0.16;
		float heat = pow(0.5 + 0.5 * sin(i.w.z * 1.6 + Eye.w * 1.8), 2);
		glow += float3(1, 0.55, 0.18) * heat * (1 - cliff) * 0.05;
	} else if (theme < 5.5) {
		rough = 0.55;
		float2 vg = VoronoiGap(i.w.xz * 0.55 + float2(Eye.w * 0.03, Eye.w * 0.012));
		float crack = 1 - smoothstep(0.02, 0.09, vg.y);
		albedo *= 1 - crack * 0.35;
		glow += float3(1.0, 0.28, 0.02) * crack * (cliff > 0.5 ? 0.32 : 0.6);
		float pulse = 0.75 + 0.25 * sin(Eye.w * 2.2 + vg.x * 6);
		glow *= pulse;
		glow += float3(1, 0.45, 0.05) * Sparkle(i.w.xz, Eye.w, 10.0, 0.94) * 0.2;
		float ash = fbm2(i.w.xz * 0.28 + Eye.w * 0.04);
		albedo = lerp(albedo, albedo * float3(0.55, 0.48, 0.42), ash * 0.18);
		nLit = normalize(nLit + float3(0, crack * 0.25, 0));
	} else if (theme < 6.5) {
		rough = 0.26;
		f0 = float3(0.05, 0.06, 0.04);
		float caus = CausAt(i.w.xz * 0.55, Eye.w);
		glow += float3(0.35, 1.0, 0.45) * caus * 0.24;
		albedo = lerp(albedo, albedo * float3(0.7, 0.85, 0.55), 0.12);
		float film = pow(0.5 + 0.5 * sin(i.w.x * 3.2 + i.w.z * 2.1 + Eye.w * 0.5), 3);
		albedo = lerp(albedo, albedo * float3(0.45, 0.7, 0.38), film * 0.2 * (1 - cliff));
		glow += float3(0.55, 1, 0.4) * Sparkle(i.w.xz, Eye.w, 9.0, 0.93) * 0.14;
		nLit = normalize(nLit + float3(curl.x, 0.06, curl.y) * 0.09);
	} else if (theme < 7.5) {
		rough = 0.46;
		f0 = float3(0.05, 0.05, 0.055);
		float2 vg = VoronoiGap(i.w.xz * 0.42);
		float moss = 1 - smoothstep(0.015, 0.07, vg.y);
		albedo = lerp(albedo, albedo * float3(0.45, 0.72, 0.4), moss * 0.34);
		float rune = pow(0.5 + 0.5 * sin(i.w.x * 1.3 + Eye.w * 0.7), 8) * pow(0.5 + 0.5 * sin(i.w.z * 1.1 - Eye.w * 0.4), 8);
		glow += float3(0.45, 0.7, 1) * rune * (1 - cliff) * 0.42;
		glow += float3(0.7, 0.85, 1) * step(0.97, hash21(floor(i.w.xz * 3))) * (0.4 + 0.3 * sin(Eye.w * 2));
		float dust = fbm3(i.w.xz * 0.22 + Eye.w * 0.02);
		albedo = lerp(albedo, albedo * float3(0.78, 0.76, 0.7), dust * 0.14);
		glow += float3(0.85, 0.8, 0.7) * Sparkle(i.w.xz, Eye.w, 14.0, 0.97) * 0.1;
	} else if (theme < 8.5) {
		rough = 0.28;
		f0 = float3(0.05, 0.075, 0.1);
		albedo *= lerp(float3(0.62, 0.82, 0.74), float3(0.45, 0.7, 0.88), 0.4);
		float rip = 0.5 + 0.5 * sin(i.w.x * 2.6 + i.w.z * 0.5 + Eye.w * 0.4);
		albedo *= 0.8 + rip * 0.22;
		float caus = CausAt(i.w.xz, Eye.w);
		glow += float3(0.35, 0.85, 1) * caus * 0.18 * saturate(n.y);
		float ray = pow(saturate(0.5 + 0.5 * sin(i.w.x * 0.35 + i.w.z * 0.12 + Eye.w * 0.35)), 5);
		ray *= pow(saturate(0.5 + 0.5 * sin(i.w.z * 0.22 - Eye.w * 0.18)), 3);
		glow += float3(0.45, 0.85, 1) * ray * saturate(n.y) * 0.1;
		float bio = Sparkle(i.w.xz, Eye.w, 7.0, 0.9);
		glow += float3(0.15, 0.95, 0.7) * bio * 0.16 * saturate(n.y);
		nLit = normalize(nLit + float3(curl.x - bedDh.x, 0.04, curl.y - bedDh.y) * 0.14);
		float sand = fbm2(i.w.xz * 1.8);
		albedo = lerp(albedo, albedo * float3(0.7, 0.62, 0.4), sand * 0.12);
		albedo *= 0.97 + saturate(dot(bedDh, bedDh) * 8.0) * 0.06;
	} else if (theme < 9.5) {
		rough = 0.38;
		f0 = float3(0.07, 0.075, 0.08);
		albedo *= float3(0.78, 0.86, 0.9);
		float spray = pow(saturate(sin(i.w.x * 1.4 + Eye.w * 2.2) * sin(i.w.z * 0.9 - Eye.w * 1.4)), 2);
		glow += float3(0.85, 0.95, 1) * spray * 0.3;
		float sheet = pow(0.5 + 0.5 * sin(i.w.z * 0.8 - Eye.w * 2.6), 3);
		glow += float3(0.7, 0.88, 1) * sheet * 0.14;
		albedo = lerp(albedo, float3(0.75, 0.86, 0.92), spray * 0.18);
		glow += float3(1, 1, 1) * Sparkle(i.w.xz, Eye.w, 24.0, 0.95) * 0.22;
		float wetR = 0.5 + 0.5 * sin(i.w.x * 5.0 + Eye.w * 1.1);
		nLit = normalize(nLit + float3(wetR * 0.12, 0.06, spray * 0.1));
		albedo *= 0.9 + fbm2(i.w.xz * 5.5) * 0.14;
	} else if (theme < 10.5) {
		rough = 0.28;
		f0 = float3(0.12, 0.13, 0.18);
		albedo *= float3(0.78, 0.84, 1.05);
		float panel = step(0.88, frac(sin(floor(i.w.x * 2.0) * 12.3 + floor(i.w.z * 2.0) * 7.1)));
		glow += float3(0.55, 0.78, 1) * panel * 0.22;
		glow += float3(0.82, 0.92, 1) * pow(ndhG, 22) * 0.16;
		float score = 1 - smoothstep(0.01, 0.05, abs(frac(i.w.x * 3.4 + i.w.z * 0.2) - 0.5));
		albedo *= 1 - score * 0.14;
		float star = Sparkle(i.w.xz, Eye.w, 5.0, 0.9);
		glow += float3(0.85, 0.92, 1) * star * 0.28;
		float frost = fbmSky(i.w.xz * 4.2);
		albedo = lerp(albedo, albedo * float3(0.9, 0.94, 1.08), frost * 0.16);
		float neb = 0.5 + 0.5 * sin(i.w.x * 0.35 + Eye.w * 0.12) * sin(i.w.z * 0.28);
		glow += lerp(float3(0.45, 0.22, 0.85), float3(0.22, 0.55, 1), neb) * neb * (1 - cliff) * 0.12;
	} else if (theme < 11.5) {
		rough = 0.24;
		f0 = float3(0.13, 0.14, 0.15);
		albedo *= float3(0.72, 0.78, 0.84);
		float seam = pow(0.5 + 0.5 * sin(i.w.x * 8.0), 16) + pow(0.5 + 0.5 * sin(i.w.z * 8.0), 16);
		albedo *= 1 - seam * 0.28;
		float lamp = step(0.96, hash21(floor(i.w.xz * 1.5)));
		glow += float3(1, 0.45, 0.15) * lamp * 0.5 + float3(0.2, 0.7, 1) * lamp * 0.24;
		float scan = 0.5 + 0.5 * sin(i.w.z * 22.0 + Eye.w * 4.0);
		glow += float3(0.25, 0.85, 1) * scan * 0.05 * (1 - cliff);
		float frost = pow(0.5 + 0.5 * sin(i.w.x * 12.0), 10);
		albedo = lerp(albedo, float3(0.85, 0.9, 0.95), frost * 0.12);
		nLit = normalize(nLit + float3(seam * 0.15, 0, 0));
	} else if (theme < 12.5) {
		rough = 0.18;
		f0 = float3(0.11, 0.1, 0.13);
		albedo = lerp(albedo, albedo * float3(0.95, 0.9, 1.05), 0.42);
		float vein = pow(0.5 + 0.5 * sin(i.w.x * 1.7 + i.w.z * 0.6), 6);
		albedo = lerp(albedo, float3(0.82, 0.74, 0.92), vein * 0.22);
		glow += float3(0.62, 0.38, 0.92) * (1 - cliff) * 0.18;
		float moon = pow(ndhG, 28);
		glow += float3(0.88, 0.8, 1) * moon * 0.18;
		float polish = fbmSky(i.w.xz * 8.0);
		albedo *= 0.96 + polish * 0.1;
		glow += float3(1, 0.86, 0.52) * Sparkle(i.w.xz, Eye.w, 9.0, 0.96) * 0.2;
		float win = step(0.975, hash21(floor(i.w.xz * 1.8))) * saturate(n.y);
		glow += float3(1, 0.78, 0.38) * win * 0.55;
		float rune = pow(0.5 + 0.5 * sin(i.w.x * 2.4 + Eye.w * 0.55), 10) * pow(0.5 + 0.5 * sin(i.w.z * 1.8 - Eye.w * 0.4), 10);
		glow += float3(0.7, 0.45, 1) * rune * (1 - cliff) * 0.28;
		float torch = exp(-length(frac(i.w.xz * 0.42) - 0.5) * 10.0);
		glow += float3(1, 0.55, 0.18) * torch * 0.16 * saturate(n.y);
	} else {
		rough = 0.36;
		f0 = float3(0.07, 0.08, 0.1);
		albedo = lerp(albedo, float3(0.82, 0.86, 0.95), 0.35);
		float mist = 0.5 + 0.5 * sin(i.w.x * 0.4 + Eye.w * 0.3);
		glow += float3(1, 0.9, 0.7) * mist * saturate(n.y) * 0.14;
		float lantern = step(0.97, hash21(floor(i.w.xz * 2.0)));
		glow += float3(1, 0.78, 0.4) * lantern * 0.62;
		float pool = exp(-length(frac(i.w.xz * 0.55) - 0.5) * 8.0);
		glow += float3(1, 0.72, 0.35) * pool * 0.12 * saturate(n.y);
		glow += float3(1, 0.9, 0.55) * Sparkle(i.w.xz, Eye.w, 13.0, 0.96) * 0.16;
		nLit = normalize(nLit + float3(curl.x, 0.03, curl.y) * 0.06);
	}
	float tier = StageTier();
	GroundSurfaceLayer(theme, i.w, n, cliff, Eye.w, burst, albedo, nLit, rough, glow);
	if (tier > 0.5 || dedicated) {
		float rep = dedicated ? (cliff > 0.5 ? 0.36 : 0.5) : (cliff > 0.5 ? 0.22 : 0.15);
		float2 puv = i.w.xz * rep;
		if (!dedicated && cliff < 0.5 && tier > 3.5 && Photo.SampleLevel(SL, float3(puv, theme), 0).a > 0.5) {
			int steps = (tier > 5.5) ? 14 : ((tier > 4.5) ? 8 : 4);
			if (theme < 0.5 || (theme > 1.5 && theme < 2.5) || (theme > 6.5 && theme < 7.5))
				steps = (tier > 5.5) ? 8 : 4;
			float scale = (tier > 5.5) ? 0.05 : ((tier > 4.5) ? 0.036 : 0.022);
			float3 viewTo = normalize(i.w - Eye.xyz);
			float2 dir = viewTo.xz / max(abs(viewTo.y), 0.45) * scale;
			float2 st = dir / steps;
			float h = 1;
			[loop] for (int s = 0; s < steps; s++) {
				float lum = dot(Photo.SampleLevel(SL, float3(puv, theme + 14), 0).rgb, float3(0.30, 0.59, 0.11));
				if (lum >= h) break;
				h -= 1.0 / steps;
				puv -= st;
			}
		}
		float4 ph = Photo.Sample(SL, float3(puv, theme));
		if (ph.a > 0.5) {
			float3 alb = ph.rgb;
			bool tri = dedicated || (tier > 1.5 && cliff > 0.5);
			if (tri && cliff > 0.5) {
				float3 w = abs(n);
				w /= max(w.x + w.y + w.z, 1e-3);
				float3 sx = Photo.Sample(SL, float3(i.w.zy * rep, theme)).rgb;
				float3 sz = Photo.Sample(SL, float3(i.w.xy * rep, theme)).rgb;
				alb = sx * w.x + alb * w.y + sz * w.z;
			}
			if (dedicated) {
				albedo = alb;
				if (theme < 8.5) albedo = lerp(albedo, albedo * float3(0.45, 0.78, 0.95), 0.5);
				else if (theme < 9.5) albedo *= float3(0.9, 0.96, 0.92);
				else if (theme < 10.5) albedo = saturate(pow(max(albedo, 0.001), 0.55) * 1.32);
				else if (theme < 11.5) albedo = saturate(albedo * 1.75 + 0.07);
				else if (theme < 12.5) albedo = saturate(albedo * 1.38 + 0.07);
			} else {
				if (tier > 3.5) {
					float3 far = Photo.Sample(SL, float3(puv * 0.23, theme)).rgb;
					alb = lerp(alb, far, 0.26);
				}
				if (tier > 4.5) {
					float3 grit = Photo.Sample(SL, float3(i.w.xz * 0.46, theme)).rgb;
					alb = lerp(alb, grit, 0.2);
				}
				float photoMix = tier < 1.5 ? 0.58 : (tier < 2.5 ? 0.74 : (tier < 3.5 ? 0.86 : 0.93));
				albedo = lerp(albedo, alb, photoMix);
				if (theme > 5.5 && theme < 6.5) albedo *= lerp(float3(1, 1, 1), float3(0.7, 0.88, 0.62), 0.24);
				else if (theme > 4.5 && theme < 5.5) albedo *= lerp(float3(1, 1, 1), float3(0.9, 0.62, 0.5), 0.34);
			}
			if (tier > 2.5 || dedicated) {
				float3 pn = Photo.Sample(SL, float3(puv, theme + 14)).rgb;
			if (tier > 4.5 && !(theme > 9.5 && theme < 12.5)) {
				float3 pn2 = Photo.Sample(SL, float3(i.w.xz * 0.46, theme + 14)).rgb;
				pn = lerp(pn, pn2, 0.35);
			}
				nLit = DetailN(Bump(n, pn), i.w);
			}
		}
	}
	{
		float4 gx = GroundX.Sample(SL, float3(i.w.xz * (cliff > 0.5 ? 0.17 : 0.29), theme));
		if (gx.a > 0.45) {
			float mixX = dedicated ? 0.22 : 0.36;
			albedo = lerp(albedo, gx.rgb * i.c.rgb, mixX);
			float3 gx2 = GroundX.Sample(SL, float3(i.w.xz * 0.51, theme)).rgb;
			albedo = lerp(albedo, gx2 * i.c.rgb, mixX * 0.35);
		}
	}
	{
		float skin = DewAmt();
		float wet = ThemeWet(theme);
		if (!(theme > 9.5 && theme < 12.5)) {
			float pore = fbm2(i.w.xz * 11.0 + i.w.y * 3.0);
			albedo *= lerp(1.0, 0.9 + pore * 0.16, skin * 0.55);
		}
		float ndvS = saturate(dot(nLit, v));
		glow += SkyAmb.rgb * pow(1.0 - ndvS, 3.2) * wet * (0.08 + skin * 0.12) * (1.0 - cliff * 0.4);
		rough = saturate(rough - wet * 0.16 * skin);
		f0 = lerp(f0, f0 + 0.028, wet * skin);
		if (skin > 0.4) {
			float wrap = saturate(dot(nLit, normalize(LightDir.xyz)) * 0.45 + 0.55);
			glow += albedo * wrap * skin * 0.045;
		}
	}
	if (StageTier() > 1.5) {
		float face = saturate(n.y);
		float rain = RainFilm();
		if (rain > 0.0 && !(theme > 4.5 && theme < 5.5)) {
			float pud = smoothstep(0.34, 0.7, fbm2(i.w.xz * 0.62 + 2.4));
			float wet = face * lerp(0.55, 1.0, pud);
			albedo *= lerp(1.0, 0.7, wet * 0.7);
			rough = lerp(rough, 0.07, wet * 0.9);
			f0 = lerp(f0, float3(0.045, 0.05, 0.055), wet);
			float h;
			float2 dh;
			WaterWave(i.w.xz, Eye.w, 0.04, h, dh);
			nLit = normalize(nLit + float3(-dh.x, 0.0, -dh.y) * wet * 2.0);
		}
		float frost = FrostFilm();
		if (frost > 0.0 && !(theme > 4.5 && theme < 5.5)) {
			float sheetW = smoothstep(0.34, 0.68, fbm2(i.w.xz * 0.48 + 4.0));
			float iceW = face * sheetW * frost;
			albedo = lerp(albedo, float3(0.8, 0.9, 1.0), iceW * 0.4);
			rough = lerp(rough, 0.05, iceW);
			nLit = normalize(nLit + float3(sin(i.w.x * 7.0), 0.03, cos(i.w.z * 6.2)) * iceW * 0.12);
		}
	}
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	col += glow * (0.7 + StageTier() * 0.22);
	if (bedCaus > 0) {
		float3 causCol = float3(0.65, 0.85, 1);
		if (theme > 4.5 && theme < 5.5) causCol = float3(1, 0.55, 0.25);
		else if (theme > 5.5 && theme < 6.5) causCol = float3(0.45, 0.85, 0.4);
		else if (theme > 2.5 && theme < 3.5) causCol = float3(0.85, 0.93, 1);
		else if (theme > 0.5 && theme < 1.5) causCol = float3(0.9, 0.7, 0.4);
		else if (theme > 7.5 && theme < 8.5) causCol = float3(0.35, 0.75, 1);
		else if (theme > 8.5 && theme < 9.5) causCol = float3(0.85, 0.95, 1);
		else if (theme > 12.5) causCol = float3(1, 0.82, 0.45);
		col += causCol * bedCaus * 0.62;
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
	}
	if (!(theme > 9.5 && theme < 12.5)) {
		float microF = (Q.x > 5.5) ? 28.0 : ((Q.x > 2.5) ? 18.0 : 11.0);
		float micro = fbm2(i.w.xz * microF + i.w.y * 2.0);
		col *= 0.95 + micro * ((Q.x > 4.5) ? 0.09 : 0.05);
	}
	if (StageTier() > 1.5) {
		float gloss = FloorGloss(theme, nLit.y, i.w.xz);
		gloss *= lerp(1.0, 0.10, saturate(sGhost));
		ApplyReflect(col, nLit, v, i.w, rough, gloss);
		if (cliff < 0.5 && gloss > 0.18) {
			float2 ge = abs(frac(i.w.xz) - 0.5);
			float edge2 = smoothstep(0.468, 0.5, max(ge.x, ge.y));
			col *= 1.0 - edge2 * 0.3;
		}
	}
	return Emit(float4(GhostCol(FogAt(col, i.w)), GhostA(1)), i.w);
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
GOut PSGX(PSIn i) { return GroundPS(i, FogColor.a); }

GOut PSProp(PSIn i)
{
	// mode 9 は足元の輪。別階でも捨てず、視界の外でも色を残す。
	if (!(Params.y > 8.5 && Params.y < 9.5)) {
		if (GustGone(i.w) > 0.5) clip(-1);
		BeginGhost(i.pos, i.w, i.n.y);
	}
	float3 n = normalize(i.n);
	float3 v = normalize(Eye.xyz - i.w);
	float mat = Params.z;
	float leaf = saturate((1 - i.c.a) * 4);
	float3 albedo;
	float3 nLit = n;
	float rough = 0.55;
	float3 f0 = float3(0.04, 0.04, 0.04);
	if (mat < 0.5) {
		float bAlb = 0, bNrm = 1;
		if ((FogColor.a > 2.5 && FogColor.a < 3.5) || (FogColor.a > 4.5 && FogColor.a < 5.5)) { bAlb = 8; bNrm = 9; }
		albedo = lerp(Tri(Mat, bAlb, i.w, n, 0.85), Tri(Mat, 2, i.w, n, 0.7), leaf);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.8, leaf > 0.5 ? 3 : bNrm)).rgb);
		rough = lerp(0.72, 0.5, leaf);
	} else if (mat < 1.5) {
		float rAlb = 4, rNrm = 5;
		if ((FogColor.a > 3.5 && FogColor.a < 5.5) || (FogColor.a > 0.5 && FogColor.a < 1.5)) { rAlb = 10; rNrm = 11; }
		albedo = Tri(Mat, rAlb, i.w, n, 0.32);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.32, rNrm)).rgb);
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
	} else if (mat < 4.5) {
		albedo = Tri(Ground, 7, i.w, n, 0.45);
		nLit = Bump(n, Ground.Sample(SL, float3(i.w.xz * 0.45, 15)).rgb);
		rough = 0.46;
		f0 = float3(0.05, 0.05, 0.055);
	} else if (mat < 5.5) {
		albedo = Tri(Mat, 12, i.w, n, 0.9);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.9, 13)).rgb);
		rough = 0.62;
	} else if (mat < 6.5) {
		albedo = Tri(Mat, 14, i.w, n, 1.1);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 1.1, 15)).rgb);
		rough = 0.28;
		f0 = float3(0.08, 0.08, 0.09);
	} else if (mat < 7.5) {
		albedo = Tri(Mat, 12, i.w, n, 0.7) * float3(1.15, 0.72, 0.28);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.7, 13)).rgb);
		rough = 0.48;
	} else {
		albedo = Tri(Mat, 2, i.w, n, 0.55) * float3(0.65, 0.9, 0.7);
		nLit = Bump(n, Mat.Sample(SL, float3(i.w.xz * 0.55, 3)).rgb);
		rough = 0.38;
		f0 = float3(0.05, 0.06, 0.05);
	}
	albedo *= Tint.rgb;
	nLit = DetailN(nLit, i.w);
	if (StageTier() > 1.5 && RainFilm() > 0.5 && n.y > 0.45 && !(FogColor.a > 4.5 && FogColor.a < 5.5)) {
		albedo *= lerp(1.0, 0.78, saturate(n.y));
		rough = lerp(rough, 0.12, saturate(n.y) * (leaf > 0.5 ? 0.35 : 0.8));
	}
	float sh = ShadowAt(i.w, n);
	float3 col = Lit(albedo, nLit, v, rough, f0, sh, 0);
	if (StageTier() > 1.5) {
		float gloss = FloorGloss(FogColor.a, nLit.y, i.w.xz);
		gloss *= leaf > 0.5 ? 0.35 : 0.82;
		ApplyReflect(col, nLit, v, i.w, rough, gloss);
	}
	if (Q.x > 2.5) {
		float pore = fbm2(i.w.xz * 7.5 + i.w.y * 2.2);
		col *= 0.94 + pore * 0.1;
		col += SkyAmb.rgb * pow(1.0 - saturate(dot(nLit, v)), 3.0) * ThemeWet(FogColor.a) * 0.08;
	}
	if (Params.y > 8.5 && Params.y < 9.5) {
		float d = length(Eye.xyz - i.w);
		float fg = saturate((d - Fog.x) / max(0.01, Fog.y - Fog.x));
		col = lerp(col, FogColor.rgb, fg * fg * 0.25);
		return Emit(float4(col, Tint.a), i.w);
	}
	return Emit(float4(GhostCol(FogAt(col, i.w)), GhostA(Tint.a)), i.w);
}

GOut PSO(PSIn i)
{
	if (Extra.x > 0.001) {
		float nz = fbm2(i.w.xz * 7. + i.w.y * 5.);
		clip(nz - (Extra.x * 1.1 - 0.05));
	}
	float oa = OutlineColor.a > 1.5 ? 1.0 : OutlineColor.a;
	return Emit(float4(lerp(OutlineColor.rgb, float3(1, 1, 1), Extra.y), oa * Tint.a), i.w);
}

GOut PSOV(PSIn i)
{
	// 移動・攻撃範囲。セル座標と世界座標の二重模様。タイル自体は透けて読める。
	float2 g = abs(i.uv - 0.5) * 2.;
	float edge = max(g.x, g.y);
	float t = Eye.w;
	float pulse = 0.92 + 0.08 * sin(t * 4.2 + (i.w.x + i.w.z) * 0.8);
	float2 cell = abs(frac(i.w.xz * 12.0) - 0.5);
	float hair = smoothstep(0.41, 0.49, max(cell.x, cell.y));
	float2 fine = abs(frac(i.w.xz * 28.0) - 0.5);
	float grain = smoothstep(0.45, 0.5, max(fine.x, fine.y)) * 0.32;
	float2 d2 = abs(frac(float2(i.w.x + i.w.z, i.w.x - i.w.z) * 5.0) - 0.5);
	float fil = smoothstep(0.45, 0.495, min(d2.x, d2.y)) * 0.25;
	float corner = smoothstep(0.68, 0.74, g.x) * smoothstep(0.68, 0.74, g.y);
	float border = smoothstep(0.89, 0.96, edge) * (1.0 - smoothstep(0.985, 1.0, edge));
	float innerLine = smoothstep(0.76, 0.81, edge) * (1.0 - smoothstep(0.82, 0.86, edge)) * 0.45;
	float inner = 0.24 + 0.16 * smoothstep(0.18, 0.9, edge);
	float a = (inner + hair * 0.2 + grain + fil + innerLine + border * 0.88 + corner * border * 0.25) * i.c.a * Tint.a * pulse;
	float3 c = i.c.rgb * Tint.rgb * (0.84 + hair * 0.26 + fil * 0.35 + (border + innerLine) * 0.58);
	if (Params.y > 0.5) {
		float cnr = step(0.7, g.x) * step(0.7, g.y);
		float frame = smoothstep(0.89, 0.95, edge) * (1.0 - smoothstep(0.985, 1.0, edge));
		a = (frame * (0.72 + cnr * 0.28) + hair * 0.1 + fil * 0.12) * i.c.a * Tint.a;
		c = i.c.rgb * Tint.rgb * (1.16 + cnr * 0.28 + hair * 0.22);
	}
	return Emit(float4(c, saturate(a)), i.w);
}

struct HIn { float2 p : POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; float4 p0 : TEXCOORD1; float4 p1 : TEXCOORD2; };
struct HOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 c : TEXCOORD1; float4 p0 : TEXCOORD2; float4 p1 : TEXCOORD3; };
HOut VSH(HIn v)
{
	HOut o;
	o.pos = float4(v.p.x * Screen.z * 2. - 1., 1. - v.p.y * Screen.w * 2., 0, 1);
	o.uv = v.uv; o.c = v.c; o.p0 = v.p0; o.p1 = v.p1;
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
struct PinP { float3 p : POSITION; float4 vel : TEXCOORD0; float4 sz : TEXCOORD1; float4 c : COLOR0; };
PinP VSPT(PinP v) { return v; }
[maxvertexcount(6)]
void GSP(point PinP i[1], inout TriangleStream<POut> s)
{
	if (i[0].sz.x < 0.0008 || i[0].c.a < 0.012) return;
	float3 right = float3(World._11, World._12, World._13);
	float3 up = float3(World._21, World._22, World._23);
	float3 fwd = float3(World._31, World._32, World._33);
	float3 pos = i[0].p;
	float3 vel = i[0].vel.xyz;
	float stretch = i[0].vel.w;
	float sz = max(i[0].sz.x, 0);
	float shape = i[0].sz.y;
	float3 ax = right * sz;
	float3 ay = up * sz;
	if (stretch > 0) {
		float sp = length(vel);
		if (sp > 0.001) {
			float3 vd = vel / sp;
			float3 side = cross(vd, fwd);
			float sl = length(side);
			if (sl > 0.001) side /= sl;
			ay = vd * (sz + sp * stretch);
			ax = side * (sz * 0.5);
		}
	}
	float3 corn[4] = { pos - ax - ay, pos - ax + ay, pos + ax + ay, pos + ax - ay };
	float2 uv[4] = { float2(0, 1), float2(0, 0), float2(1, 0), float2(1, 1) };
	[unroll] for (int t = 0; t < 3; t++) {
		POut o;
		o.pos = mul(float4(corn[t], 1), VP);
		o.uvs = float3(uv[t], shape);
		o.c = i[0].c;
		s.Append(o);
	}
	s.RestartStrip();
	int tri[3] = { 0, 2, 3 };
	[unroll] for (int u = 0; u < 3; u++) {
		POut o;
		o.pos = mul(float4(corn[tri[u]], 1), VP);
		o.uvs = float3(uv[tri[u]], shape);
		o.c = i[0].c;
		s.Append(o);
	}
}

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
	o.pos = float4(p * float2(2, -2) + float2(-1, 1), 1, 1);
	o.uv = p;
	return o;
}
// 空の天井と、盤面の下に広がる奈落。すべてカメラの視線 rd の世界座標で描く。
void AbyssAndSky(inout float3 c, float3 rd, float ths, float t, float gust)
{
	float yaw = atan2(rd.x, rd.z);
	float up = saturate(rd.y * 2.2 + 0.15);
	float down = saturate(-rd.y * 2.4 + 0.2);
	float rim = exp(-abs(rd.y) * 5.5);
	float2 skyXZ = rd.xz / max(abs(rd.y) + 0.16, 0.16) * 14.0;
	float2 pit1 = PlaneXZ(rd, -8.0, 0.09) * 0.14;
	float2 pit2 = PlaneXZ(rd, -22.0, 0.09) * 0.07;
	float2 pit3 = PlaneXZ(rd, -44.0, 0.09) * 0.035;
	[branch] if (ths < 0.5) {
		float c1 = fbmSky(skyXZ * 0.22 + float2(-t * (0.18 + gust * 0.22), t * 0.04));
		float c2 = vnoise(skyXZ * 0.52 + float2(-t * 0.32, -t * 0.08) + 3.1);
		float cloud = smoothstep(0.36, 0.76, c1 * 0.6 + c2 * 0.5);
		c = lerp(c, float3(0.98, 0.99, 1.0), cloud * up * 0.55);
		float sea1 = fbmSky(pit1 + float2(-t * 0.25, t * 0.08));
		float sea2 = vnoise(pit2 * 1.8 + float2(-t * 0.14, -t * 0.05));
		float billow = smoothstep(0.3, 0.74, sea1 * 0.6 + sea2 * 0.5);
		float3 abyss = lerp(float3(0.32, 0.52, 0.74), float3(0.88, 0.95, 1.0), billow);
		float shaft = pow(0.5 + 0.5 * sin(yaw * 9.0 - t * 0.35 + sea1 * 3.0), 8.0);
		abyss += float3(1.0, 0.96, 0.78) * shaft * 0.28;
		c = lerp(c, abyss, down * 0.88);
		c += float3(0.85, 0.94, 1.0) * rim * 0.22;
	} else if (ths < 1.5) {
		float mesa = fbmSky(float2(yaw * 3.5, 1.2)) * 0.18;
		float ridge = smoothstep(mesa + 0.06, mesa - 0.02, abs(rd.y + 0.04));
		c = lerp(c, float3(0.36, 0.16, 0.08), ridge * rim * 0.65);
		float d1 = fbmSky(pit1 * 1.4 - float2(t * 0.38, t * 0.12));
		float d2 = fbmSky(pit2 * 2.6 + float2(t * 0.22, -t * 0.18));
		float strata = 0.5 + 0.5 * sin(rd.y * 48.0 + d1 * 5.0);
		float3 gorge = lerp(float3(0.18, 0.07, 0.03), float3(0.78, 0.38, 0.16), saturate(d1 * 0.7 + strata * 0.35));
		gorge += float3(1.0, 0.58, 0.22) * pow(saturate(d2), 2.4) * 0.42;
		c = lerp(c, gorge, down * 0.9);
		c += float3(1.0, 0.56, 0.24) * rim * 0.28;
	} else if (ths < 2.5) {
		float can = fbmSky(skyXZ * 0.38 + float2(t * 0.04, 0));
		float ray = pow(0.5 + 0.5 * sin(yaw * 14.0 + can * 2.5 - t * 0.18), 10.0);
		c += float3(0.72, 1.0, 0.45) * ray * (up + rim * 0.6) * 0.32;
		float2 rv1 = VoronoiGap(pit1 * 2.2);
		float2 rv2 = VoronoiGap(pit2 * 3.4 + 1.7);
		float roots = (1.0 - smoothstep(0.02, 0.09, rv1.y)) + (1.0 - smoothstep(0.025, 0.1, rv2.y)) * 0.6;
		float mist = fbmSky(pit1 * 1.2 + float2(t * 0.12, -t * 0.09));
		float2 sp = floor(pit1 * 9.0 + float2(0, t * 0.8));
		float fly = step(0.95, hash21(sp)) * (0.5 + 0.5 * sin(t * 4.0 + hash21(sp) * 20.0));
		float3 deep = lerp(float3(0.03, 0.1, 0.05), float3(0.16, 0.42, 0.22), mist);
		deep = lerp(deep, float3(0.22, 0.16, 0.09), saturate(roots) * 0.65);
		deep += float3(0.62, 1.0, 0.38) * (ray * 0.22 + fly * 0.65);
		c = lerp(c, deep, down * 0.92);
	} else if (ths < 3.5) {
		float2 ap = float2(yaw * 2.2 + t * 0.06, rd.y * 3.4);
		float a1 = fbmSky(ap + float2(0, fbmSky(ap * 1.8) * 0.8));
		float a2 = fbmSky(ap * 2.1 - float2(t * 0.04, 1.7));
		float aur = pow(saturate(a1 * 0.62 + a2 * 0.52), 1.4) * up;
		c += lerp(float3(0.12, 0.92, 0.58), float3(0.38, 0.45, 1.0), a2) * aur * 0.55;
		float2 cv1 = VoronoiGap(pit1 * 2.6);
		float2 cv2 = VoronoiGap(pit2 * 4.0);
		float rift = (1.0 - smoothstep(0.015, 0.07, cv1.y)) + (1.0 - smoothstep(0.02, 0.08, cv2.y)) * 0.65;
		float3 crev = lerp(float3(0.03, 0.09, 0.18), float3(0.22, 0.58, 0.86), fbmSky(pit1 * 1.6));
		crev += float3(0.55, 0.88, 1.0) * saturate(rift) * 0.62;
		c = lerp(c, crev, down * 0.92);
	} else if (ths < 4.5) {
		float mir = fbmSky(float2(yaw * 4.0 - t * 0.22, rd.y * 12.0));
		c += float3(1.0, 0.78, 0.38) * (rim * 0.32 + mir * up * 0.16);
		float w1 = fbmSky(pit1 * 1.8 - float2(t * 0.55, t * 0.18));
		float w2 = fbmSky(pit2 * 3.2 - float2(t * 0.85, -t * 0.24));
		float swirl = sin(atan2(pit1.y, pit1.x) * 3.0 + length(pit1) * 1.4 - t * 1.6) * 0.5 + 0.5;
		float3 sea = lerp(float3(0.26, 0.16, 0.06), float3(0.88, 0.66, 0.32), saturate(w1 * 0.65 + w2 * 0.45));
		sea += float3(1.0, 0.82, 0.42) * pow(swirl * w1, 2.0) * 0.38;
		c = lerp(c, sea, down * 0.9);
	} else if (ths < 5.5) {
		float ash = fbmSky(skyXZ * 0.35 - float2(t * 0.12, t * 0.28));
		c = lerp(c, float3(0.14, 0.05, 0.03), ash * up * 0.55);
		c += float3(1.0, 0.3, 0.04) * pow(ash, 2.4) * (up * 0.35 + rim * 0.45);
		float2 mv1 = VoronoiGap(pit1 * 1.9 + float2(t * 0.05, -t * 0.03));
		float2 mv2 = VoronoiGap(pit2 * 3.4 - float2(t * 0.08, t * 0.04));
		float vein1 = 1.0 - smoothstep(0.015, 0.09, mv1.y);
		float vein2 = 1.0 - smoothstep(0.018, 0.08, mv2.y);
		float3 cald = float3(0.07, 0.02, 0.01);
		cald += float3(1.0, 0.28, 0.02) * vein1 * 1.15;
		cald += float3(1.0, 0.65, 0.12) * vein2 * 0.65;
		cald += float3(1.0, 0.22, 0.02) * pow(fbmSky(pit3 * 2.0 - t * 0.2), 2.2) * 0.55;
		c = lerp(c, cald, down * 0.95);
	} else if (ths < 6.5) {
		float gas = fbmSky(skyXZ * 0.32 + CurlSky(skyXZ * 0.18 + t * 0.05) * 0.6);
		c += float3(0.38, 0.88, 0.28) * pow(gas, 2.0) * (up * 0.28 + rim * 0.35);
		float m1 = fbmSky(pit1 * 1.5 + CurlSky(pit1 * 0.8 + t * 0.1));
		float m2 = fbmSky(pit2 * 2.8 - float2(t * 0.16, t * 0.12));
		float2 wc = floor(pit1 * 4.5);
		float2 wf = frac(pit1 * 4.5) - 0.5 - (hash22(wc) - 0.5) * 0.4;
		float orb = exp(-dot(wf, wf) * 18.0) * step(0.72, hash21(wc)) * (0.5 + 0.5 * sin(t * 2.4 + hash21(wc) * 16.0));
		float3 bog = lerp(float3(0.03, 0.08, 0.03), float3(0.22, 0.48, 0.16), saturate(m1 * 0.65 + m2 * 0.45));
		bog += float3(0.48, 1.0, 0.35) * orb * 0.95;
		c = lerp(c, bog, down * 0.92);
	} else if (ths < 7.5) {
		float r = length(pit1);
		float a = atan2(pit1.y, pit1.x);
		float ring1 = exp(-abs(frac(r * 0.35 - t * 0.05) - 0.5) * 22.0);
		float ring2 = exp(-abs(frac(r * 0.7 + t * 0.04) - 0.5) * 26.0);
		float glyph = step(0.65, sin(a * 12.0 + floor(r * 0.7) * 2.3 + t * 0.4));
		float3 sanc = lerp(float3(0.04, 0.05, 0.12), float3(0.14, 0.22, 0.44), fbmSky(pit2 * 1.6));
		sanc += float3(0.45, 0.78, 1.0) * (ring1 * 0.55 + ring2 * glyph * 0.75);
		c = lerp(c, sanc, down * 0.92);
		float skyR = exp(-abs(rd.y - 0.32) * 18.0) * (0.5 + 0.5 * sin(yaw * 8.0 - t * 0.3));
		c += float3(0.45, 0.72, 1.0) * skyR * up * 0.38;
	} else if (ths < 8.5) {
		float2 cp = skyXZ * 0.42 + float2(t * 0.14, -t * 0.09);
		float ca = CausAt(cp * 0.6, t * 0.8);
		float shaft = pow(0.5 + 0.5 * sin(yaw * 10.0 + fbmSky(cp) * 3.0 - t * 0.25), 8.0);
		c += float3(0.2, 0.75, 1.0) * (ca * 0.38 + shaft * 0.32) * (up + rim * 0.5);
		float2 bc1 = floor(pit1 * 6.0);
		float2 bc2 = floor(pit2 * 11.0);
		float b1 = exp(-length(frac(pit1 * 6.0) - 0.5) * 9.0) * step(0.74, hash21(bc1));
		float b2 = exp(-length(frac(pit2 * 11.0) - 0.5) * 10.0) * step(0.78, hash21(bc2));
		float3 trench = lerp(float3(0.01, 0.05, 0.12), float3(0.04, 0.24, 0.42), fbmSky(pit1 * 1.2 + t * 0.08));
		trench += float3(0.15, 0.95, 0.82) * (b1 * 0.75 + b2 * 0.55);
		trench += float3(0.22, 0.68, 1.0) * CausAt(pit1 * 0.8, t) * 0.28;
		c = lerp(c, trench, down * 0.94);
	} else if (ths < 9.5) {
		float fall = fbmSky(float2(yaw * 18.0, rd.y * 6.0 - t * 1.8));
		float sheet = pow(0.5 + 0.5 * sin(yaw * 22.0 + fall * 2.0), 4.0);
		float mist = fbmSky(pit1 * 1.4 - float2(0, t * 0.65));
		float bowA = exp(-pow((rd.y + 0.18) * 5.5, 2.0));
		float3 bow = lerp(float3(1, 0.38, 0.38), lerp(float3(0.38, 1, 0.52), float3(0.38, 0.58, 1), saturate(rd.y * 3.0 + 0.8)), saturate(rd.y * 2.0 + 0.5));
		float3 plunge = lerp(float3(0.14, 0.32, 0.48), float3(0.82, 0.94, 1.0), saturate(mist * 0.65 + sheet * 0.45));
		plunge += bow * bowA * 0.38;
		c = lerp(c, plunge, down * 0.9);
		c += bow * exp(-pow((rd.y - 0.18) * 6.0, 2.0)) * up * 0.28;
	} else if (ths < 10.5) {
		float3 sd = normalize(rd);
		float2 u1 = float2(atan2(sd.x, sd.z) * 1.8, sd.y * 2.4);
		float n1 = fbmSky(u1 + t * 0.02);
		float n2 = fbmSky(u1 * 2.3 - t * 0.015 + 3.4);
		float neb = smoothstep(0.28, 0.78, n1 * 0.58 + n2 * 0.52);
		float3 cosmos = float3(0.02, 0.02, 0.06);
		cosmos += lerp(float3(0.55, 0.12, 0.88), float3(0.12, 0.58, 1.0), n2) * neb * 0.72;
		float2 sg1 = float2(yaw * 95.0, rd.y * 140.0);
		float2 sg2 = float2(yaw * 210.0, rd.y * 310.0);
		float s1 = step(0.988, hash21(floor(sg1))) * saturate(1.0 - length(frac(sg1) - 0.5) * 5.2);
		float s2 = step(0.992, hash21(floor(sg2))) * saturate(1.0 - length(frac(sg2) - 0.5) * 6.0);
		cosmos += float3(0.92, 0.96, 1.0) * (s1 + s2 * 0.8);
		float vortex = exp(-abs(length(pit1) * 0.18 - 1.2) * 2.5) * fbmSky(pit1 * 0.8 - t * 0.2);
		cosmos += float3(0.42, 0.78, 1.0) * vortex * down * 0.55;
		c = lerp(c, cosmos, saturate(up * 0.75 + down * 0.96));
	} else if (ths < 11.5) {
		float2 g1 = abs(frac(pit1 * 2.0) - 0.5);
		float2 g2 = abs(frac(pit2 * 4.0) - 0.5);
		float strut1 = smoothstep(0.43, 0.49, max(g1.x, g1.y));
		float strut2 = smoothstep(0.45, 0.495, max(g2.x, g2.y));
		float pulse1 = step(0.75, hash21(floor(pit1 * 2.0))) * (0.4 + 0.6 * sin(t * 3.0 + hash21(floor(pit1 * 2.0)) * 12.0));
		float scan = pow(0.5 + 0.5 * sin(pit1.x * 1.5 + pit1.y * 1.5 - t * 2.2), 8.0);
		float3 core = float3(0.03, 0.05, 0.09);
		core += float3(0.18, 0.72, 0.95) * (strut1 * 0.55 + strut2 * 0.45 + scan * 0.35);
		core += float3(1.0, 0.48, 0.14) * pulse1 * strut1 * 0.85;
		c = lerp(c, core, down * 0.94);
		float2 sg = abs(frac(skyXZ * 0.45) - 0.5);
		c += float3(0.22, 0.82, 1.0) * smoothstep(0.46, 0.495, max(sg.x, sg.y)) * up * 0.32;
	} else if (ths < 12.5) {
		float2 a1 = abs(frac(pit1 * 1.5) - 0.5);
		float arch = smoothstep(0.42, 0.49, max(a1.x, a1.y));
		float rose = exp(-abs(length(frac(pit1 * 0.5) - 0.5) - 0.32) * 28.0);
		float candle = step(0.84, hash21(floor(pit1 * 3.0))) * exp(-length(frac(pit1 * 3.0) - 0.5) * 9.0);
		float3 nave = lerp(float3(0.06, 0.03, 0.11), float3(0.24, 0.12, 0.38), fbmSky(pit2 * 1.4));
		nave += float3(0.78, 0.52, 1.0) * (arch * 0.48 + rose * 0.65);
		nave += float3(1.0, 0.76, 0.34) * candle * 0.9;
		c = lerp(c, nave, down * 0.94);
	} else {
		float2 l1 = pit1 * 1.8 + float2(sin(t * 0.25), cos(t * 0.2)) * 0.4;
		float2 l2 = pit2 * 3.2 - float2(cos(t * 0.18), sin(t * 0.22)) * 0.5;
		float2 c1 = floor(l1), c2 = floor(l2);
		float2 f1 = frac(l1) - 0.5 - (hash22(c1) - 0.5) * 0.4;
		float2 f2 = frac(l2) - 0.5 - (hash22(c2) - 0.5) * 0.4;
		float o1 = exp(-dot(f1, f1) * 16.0) * step(0.55, hash21(c1));
		float o2 = exp(-dot(f2, f2) * 18.0) * step(0.58, hash21(c2));
		float3 sea = lerp(float3(0.06, 0.05, 0.14), float3(0.22, 0.18, 0.38), fbmSky(pit1 * 0.9 + t * 0.05));
		sea += float3(1.0, 0.74, 0.32) * (o1 * 1.05 + o2 * 0.75);
		c = lerp(c, sea, down * 0.94);
		float2 sL = skyXZ * 0.45 + float2(t * 0.06, -t * 0.03);
		float2 sc = floor(sL);
		float2 sf = frac(sL) - 0.5 - (hash22(sc) - 0.5) * 0.4;
		float skyOrb = exp(-dot(sf, sf) * 16.0) * step(0.58, hash21(sc));
		c += float3(1.0, 0.74, 0.34) * skyOrb * up * 0.65;
	}
}

GOut SkyPS(QOut i, float ths)
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
	} else if (ths >= 0) {
		float2 suv = float2(Params.z, Params.w);
		float2 q = i.uv - suv;
		q.x *= Screen.x * Screen.w;
		float sd = length(q);
		float vis = saturate(Extra.w);
		float3 sunCol = float3(1.0, 0.93, 0.72);
		c += sunCol * exp(-sd * 7.5) * 0.62 * vis;
		c += sunCol * smoothstep(0.05, 0.0, sd) * 1.35 * vis;
		c += sunCol * exp(-abs(y - 0.62) * 9.0) * 0.16 * vis;
		float cl = fbmSky(float2(i.uv.x * 2.1, y * 1.5 + t * 0.03));
		cl *= smoothstep(0.95, 0.28, y) * smoothstep(0.05, 0.22, y);
		float clAmt = 0.22;
		if (ths > 2.5 && ths < 3.5) clAmt = 0.06;
		else if (ths > 4.5 && ths < 5.5) clAmt = 0;
		else if (ths > 7.5 && ths < 8.5) clAmt = 0;
		else if (ths > 9.5 && ths < 13.0) clAmt = 0;
		c = lerp(c, float3(0.95, 0.97, 1), cl * clAmt);
		float night = 1. - saturate(dot(Tint.rgb, float3(0.25, 0.5, 0.25)) * 3.0);
		float2 cell = floor(i.uv * float2(240, 135));
		float hs = hash21(cell);
		float star = step(0.992, hs) * smoothstep(0.78, 0.12, y) * night;
		if (ths < 2.5 || (ths > 5.5 && ths < 7.5) || (ths > 8.5 && ths < 9.5) || ths > 12.5)
			c += (0.55 + 0.45 * sin(t * 1.8 + hs * 30.)) * star;
		float horz = smoothstep(0.18, 0.52, y) * smoothstep(0.92, 0.58, y);
		float skyA = smoothstep(0.88, 0.05, y);
		float gustS = GustAmt();
		[branch] if (ths < 0.5) {
			float streak = pow(0.5 + 0.5 * sin(i.uv.x * 4.2 - t * (0.18 + gustS * 0.25) + y * 1.4), 4);
			c += float3(1.0, 0.96, 0.78) * streak * skyA * 0.11;
			c = lerp(c, float3(0.8, 0.9, 1.0), horz * 0.06);
		} else if (ths < 1.5) {
			c = lerp(c, float3(1.0, 0.72, 0.42), horz * 0.14);
			c += float3(1, 0.55, 0.2) * exp(-abs(y - 0.58) * 10.0) * 0.12;
		} else if (ths < 2.5) {
			float ray = pow(0.5 + 0.5 * sin(i.uv.x * 9.0 + t * 0.2), 12);
			c += float3(0.75, 1, 0.5) * ray * skyA * 0.1;
			c = lerp(c, float3(0.55, 0.78, 0.62), horz * 0.08);
		} else if (ths < 3.5) {
			c = lerp(c, float3(0.78, 0.88, 1.0), horz * 0.14);
			float2 ap = float2(i.uv.x * 2.6 + t * 0.035, y * 1.7);
			float a1 = fbmSky(ap);
			float a2 = fbmSky(ap * 1.65 + float2(3.1, t * 0.025));
			float curtain = saturate(a1 * 0.55 + a2 * 0.45);
			curtain *= saturate(1.0 - abs(y - 0.22) * 2.6) * skyA;
			c += lerp(float3(0.14, 0.72, 0.52), float3(0.32, 0.4, 1.0), a2) * pow(curtain, 1.55) * 0.22;
			c += float3(0.7, 0.84, 1) * pow(skyA, 2) * 0.035;
		} else if (ths < 4.5) {
			c = lerp(c, float3(1.0, 0.82, 0.45), horz * 0.16);
			c += float3(1, 0.7, 0.3) * exp(-abs(y - 0.6) * 8.0) * 0.1;
		} else if (ths < 5.5) {
			c = lerp(c, float3(0.4, 0.1, 0.04), horz * 0.3);
			float heat = fbmSky(float2(i.uv.x * 1.5, y * 3.2 - t * 0.07));
			c += float3(1.0, 0.26, 0.04) * heat * heat * skyA * 0.14;
			c += float3(1, 0.42, 0.08) * exp(-abs(y - 0.54) * 6.5) * 0.1;
		} else if (ths < 6.5) {
			c = lerp(c, float3(0.42, 0.55, 0.32), horz * 0.16);
			float wisp = pow(saturate(sin(i.uv.x * 6.0 + t * 0.2) * sin(y * 9.0 - t * 0.15)), 8.0);
			c += float3(0.45, 0.9, 0.35) * wisp * skyA * 0.16;
		} else if (ths < 7.5) {
			c = lerp(c, float3(0.45, 0.38, 0.7), horz * 0.12);
			c += float3(0.4, 0.6, 1) * skyA * 0.05;
		} else if (ths < 8.5) {
			c = lerp(c, float3(0.02, 0.16, 0.3), 0.38);
			float2 cp = float2(i.uv.x * 3.4 + t * 0.06, y * 4.0 - t * 0.04);
			float ca = saturate(fbmSky(cp) * fbmSky(cp.yx * 1.25 + 2.0));
			c += float3(0.12, 0.5, 0.82) * ca * skyA * 0.16;
			float colx = i.uv.x * 5.5 + fbmSky(float2(i.uv.x * 2.0, t * 0.04)) * 0.7;
			float ray = pow(saturate(sin(colx * 6.28318) * 0.5 + 0.32), 14.0);
			c += float3(0.18, 0.62, 1.0) * ray * (0.12 + y * 0.32) * 0.09;
		} else if (ths < 9.5) {
			c = lerp(c, float3(0.82, 0.9, 0.98), horz * 0.14);
			c += float3(0.9, 0.96, 1) * skyA * 0.04;
		} else if (ths < 10.5) {
			c = lerp(c, float3(0.05, 0.06, 0.14), 0.32);
			float2 np = float2(i.uv.x * 1.7 + t * 0.018, y * 1.35);
			float n1 = fbmSky(np);
			float n2 = fbmSky(np * 2.05 + float2(4.0, -t * 0.012));
			float neb = saturate(n1 * 0.58 + n2 * 0.48);
			c += lerp(float3(0.36, 0.1, 0.68), float3(0.08, 0.36, 0.88), n2) * neb * neb * skyA * 0.4;
			float2 sg = i.uv * float2(380, 220);
			float2 scell = floor(sg);
			float2 sf = frac(sg) - 0.5;
			float sh = hash21(scell);
			float star = step(0.994, sh) * saturate(1.0 - length(sf) * 5.8);
			c += star * skyA * (0.5 + 0.5 * sin(t * 1.3 + sh * 18.0));
		} else if (ths < 11.5) {
			c = lerp(c, float3(0.07, 0.09, 0.13), 0.26);
			float2 bp = (i.uv - float2(0.5, 0.28)) * float2(Screen.x * Screen.w, 1);
			float ring = pow(0.5 + 0.5 * sin(length(bp) * 26.0 - t * 0.32), 16.0);
			c += float3(0.22, 0.68, 0.9) * ring * skyA * 0.07;
			float hx = abs(frac(i.uv.x * 7.0) - 0.5);
			c += float3(0.45, 0.6, 0.75) * smoothstep(0.47, 0.5, hx) * skyA * 0.035;
			float2 lg = i.uv * float2(110, 62);
			float2 lf = frac(lg) - 0.5;
			float lh = hash21(floor(lg) + floor(t * 1.4));
			c += float3(0.28, 0.88, 1.0) * step(0.991, lh) * saturate(1.0 - length(lf) * 6.2) * skyA * 0.32;
		} else if (ths < 12.5) {
			c = lerp(c, float3(0.26, 0.12, 0.4), horz * 0.28);
			float2 md = (float2(i.uv.x, y) - float2(0.72, 0.18)) * float2(1.8, 2.4);
			float moon = exp(-dot(md, md) * 7.0);
			c += float3(0.9, 0.84, 1.0) * moon * 0.72;
			c += float3(0.52, 0.32, 0.82) * exp(-dot(md, md) * 1.5) * skyA * 0.16;
			float beam = pow(saturate(1.0 - abs(i.uv.x - 0.72) * 16.0), 3.2) * saturate(0.48 - y);
			c += float3(0.7, 0.52, 1.0) * beam * 0.09;
		} else {
			c = lerp(c, float3(0.35, 0.32, 0.55), horz * 0.12);
			float2 lg = i.uv * float2(5.0, 3.0);
			float2 lc = floor(lg);
			float2 lf = frac(lg) - 0.5;
			float lh = hash21(lc);
			float orb = exp(-dot(lf, lf) * 10.0) * step(0.62, lh);
			c += float3(1.0, 0.72, 0.38) * orb * skyA * 0.45;
		}
		AbyssAndSky(c, WorldRay(i.uv), ths, t, gustS);
	}
	return EmitZ(float4(c, 1), 1);
}
GOut PSSKY(QOut i) { return SkyPS(i, FogColor.a); }
GOut PSSKY0(QOut i) { return SkyPS(i, 0); }
GOut PSSKY1(QOut i) { return SkyPS(i, 1); }
GOut PSSKY2(QOut i) { return SkyPS(i, 2); }
GOut PSSKY3(QOut i) { return SkyPS(i, 3); }
GOut PSSKY4(QOut i) { return SkyPS(i, 4); }
GOut PSSKY5(QOut i) { return SkyPS(i, 5); }
GOut PSSKY6(QOut i) { return SkyPS(i, 6); }
GOut PSSKY7(QOut i) { return SkyPS(i, 7); }
GOut PSSKY8(QOut i) { return SkyPS(i, 8); }
GOut PSSKY9(QOut i) { return SkyPS(i, 9); }

float3 WeatherSample(float2 uv, float2 ofs, float3 col)
{
	return Tex.Sample(SL, saturate(uv + ofs)).rgb;
}

float Mote(float2 uv, float t, float2 vel, float density, float cutoff, float rad)
{
	float2 g = uv * density + t * vel;
	float2 cell = floor(g);
	float2 f = frac(g) - 0.5;
	float2 j = hash22(cell) - 0.5;
	return smoothstep(rad, 0, length(f - j * 0.55)) * step(cutoff, hash21(cell));
}

void HourFx(inout float3 col, float2 uv, float hour, float t)
{
	if (hour < 0.5) return;
	if (hour < 1.5) col = lerp(col, col * float3(1.06, 0.9, 0.74), 0.16);
	else if (hour < 2.5) col = lerp(col, col * float3(1.03, 0.98, 0.9), 0.08);
	else if (hour < 3.5) col = lerp(col, col * float3(1.08, 0.8, 0.58), 0.18);
	else if (hour < 4.5) col = lerp(col, col * float3(0.92, 0.95, 1.04), 0.1);
	else if (hour < 5.5) col = lerp(col, col * float3(0.9, 0.93, 1.03), 0.12);
	else if (hour < 6.5) col = lerp(col, col * float3(0.9, 0.93, 1.02), 0.1);
	else if (hour < 7.5) {
		col = lerp(col, col * float3(0.86, 0.9, 1.08), 0.18);
	} else if (hour < 8.5) {
		col = lerp(col, col * float3(0.82, 0.88, 1.06), 0.16);
		float2 sg = uv * float2(280, 160);
		float2 sf = frac(sg) - 0.5;
		float star = step(0.996, hash21(floor(sg) + floor(t * 0.2))) * saturate(1.0 - length(sf) * 5.5);
		col += star * float3(0.85, 0.9, 1) * saturate(0.4 - uv.y) * 0.8;
	} else col = lerp(col, col * float3(0.84, 0.9, 1.06), 0.18);
}

void WeatherFx(inout float3 col, float2 uv, float wx, float t)
{
	// 粒そのものはワールド側。ここは色味と、レンズに付いた水滴と、稲妻だけ。
	if (wx < 0.5) return;
	float wet = (wx > 0.5 && wx < 1.5) ? 1.0 : ((wx > 4.5 && wx < 5.5) ? 1.0 : 0.0);
	if (wet > 0.0) {
		col = lerp(col, col * float3(0.9, 0.94, 1.0), 0.08);
		float2 grid = float2(14, 8);
		float2 cell = floor(uv * grid);
		float2 f = frac(uv * grid);
		float h = hash21(cell + 3.1);
		float2 ctr = 0.5 + (hash22(cell) - 0.5) * 0.45;
		float d = length(f - ctr);
		float drop = smoothstep(0.16, 0.04, d) * step(0.78, h);
		float2 nrm = (f - ctr) / max(d, 1e-3);
		float3 bent = WeatherSample(uv, nrm * drop * 0.012, col);
		col = lerp(col, bent, drop * 0.45);
		if (wx > 4.5 && frac(t * 0.17) > 0.97) col += float3(0.45, 0.5, 0.62) * 0.35;
	} else if (wx < 2.5) col = lerp(col, col * float3(0.94, 0.96, 1), 0.06);
	else if (wx < 3.5) col = lerp(col, col * float3(0.9, 0.78, 0.66), 0.06);
	else if (wx < 4.5) {
		float fog = smoothstep(0.42, 0.0, uv.y) * (0.25 + 0.1 * sin(uv.x * 8.0 + t * 0.2));
		col = lerp(col, float3(0.78, 0.82, 0.86), saturate(fog) * 0.12);
	} else if (wx < 6.5) col = lerp(col, col * float3(0.98, 0.9, 0.72), 0.06);
	else if (wx < 7.5) col = lerp(col, col * float3(0.94, 0.96, 1), 0.05);
	else if (wx > 8.5) col = lerp(col, col * float3(1.03, 0.98, 0.9), 0.05);
}

// 近い画素（キャラと足元）はずらさない。陽炎や塵は空と遠い地面だけ。
float3 FarBend(float3 col, float2 uv, float t, float mask, float amp)
{
	if (mask < 0.02 || amp < 0.00005) return col;
	float yb = uv.y * 96.0 + uv.x * 8.0;
	float w1 = sin(yb + t * 2.8);
	float w2 = sin(yb * 0.43 - t * 1.35 + uv.x * 14.0);
	float2 ofs = float2(w1 * 0.8 + w2 * 0.45, w2 * 0.2) * amp * mask;
	float3 bent = Tex.SampleLevel(SL, saturate(uv + ofs), 0).rgb;
	float3 base = Tex.SampleLevel(SL, uv, 0).rgb;
	col += (bent - base) * saturate(mask);
	return col;
}

// マップごとに一つの空間技法。すべて世界座標の視線と平面に乗せる。近い画素（キャラ・移動マス）には掛けない。
void StageSignature(inout float3 col, float2 uv, float th, float t, float z, float sky, float far, float presence, float zMax)
{
	if (th < 0) return;
	float clear = smoothstep(0.16, 0.44, z);
	float pres = saturate(presence);
	if (max(sky * clear, far) * pres < 0.02) return;
	float room = 0.62;
	if (z > 0.02) {
		float front = saturate((zMax - z) * 14.0);
		float nearC = 1.0 - smoothstep(0.05, 0.22, z);
		room = saturate(1.0 - max(front, nearC * 0.94));
	}
	float veil = max(sky * clear, far * room) * pres;
	if (veil < 0.02) return;
	float gust = GustAmt();
	float3 rd = WorldRay(uv);
	float yaw = atan2(rd.x, rd.z);
	float3 wpos = Eye.xyz + rd * (max(z, 0.45) * max(Fog.y, 60.0));
	float2 wxz = wpos.xz;
	[branch] if (th < 0.5) {
		float2 p = wxz * float2(0.18, 0.07) - float2(t * (0.9 + gust * 0.8), t * 0.2);
		float rib = smoothstep(0.42, 0.78, fbmSky(p) * 0.65 + vnoise(p * 2.3 + 1.9) * 0.5);
		float2 sg = wxz * 0.85 - float2(t * 1.4, t * 0.35);
		float2 sc = floor(sg);
		float seed = smoothstep(0.12, 0.02, length(frac(sg) - 0.5 - (hash22(sc) - 0.5) * 0.4)) * step(0.78, hash21(sc));
		col += float3(0.95, 0.98, 0.72) * (rib * 0.38 + seed * 0.55) * veil;
	} else if (th < 1.5) {
		float2 dp = wxz * float2(0.14, 0.28) - float2(t * 0.75, t * 0.18);
		float colm = smoothstep(0.4, 0.78, fbmSky(dp) * 0.65 + fbmSky(dp * 2.4) * 0.5);
		col = FarBend(col, uv, t * 0.7, veil * 0.85, 0.006);
		col += float3(1.0, 0.48, 0.16) * colm * veil * 0.44;
	} else if (th < 2.5) {
		float ray = pow(0.5 + 0.5 * sin(yaw * 16.0 + fbmSky(wxz * 0.08 + t * 0.05) * 2.5 - t * 0.2), 8.0);
		float2 fg = wxz * 0.65 + float2(sin(t * 0.5), cos(t * 0.4)) * 0.5;
		float2 fc = floor(fg);
		float spore = smoothstep(0.14, 0.02, length(frac(fg) - 0.5 - (hash22(fc) - 0.5) * 0.4)) * step(0.75, hash21(fc));
		col += float3(0.72, 1.0, 0.48) * (ray * 0.42 + spore * 0.55) * veil;
	} else if (th < 3.5) {
		float2 ap = float2(yaw * 2.6 + t * 0.05, rd.y * 3.2);
		float a = fbmSky(ap);
		float b = fbmSky(ap * 1.9 + 4.0);
		float curtain = pow(saturate(a * 0.6 + b * 0.5), 1.5);
		float2 dg = wxz * 1.1 - float2(t * 1.6, t * 0.4);
		float diamond = step(0.94, hash21(floor(dg))) * (0.5 + 0.5 * sin(t * 6.0 + hash21(floor(dg)) * 30.0));
		col += lerp(float3(0.15, 0.88, 0.58), float3(0.38, 0.45, 1.0), b) * curtain * veil * 0.52;
		col += float3(0.88, 0.96, 1.0) * diamond * veil * 0.45;
	} else if (th < 4.5) {
		col = FarBend(col, uv, t * 1.15, veil, 0.012);
		float2 sp = wxz * float2(0.16, 0.45) - float2(t * 1.3, t * 0.25);
		float veilS = smoothstep(0.38, 0.76, fbmSky(sp) * 0.65 + fbmSky(sp * 2.3) * 0.5);
		col += float3(1.0, 0.78, 0.35) * veilS * veil * 0.42;
	} else if (th < 5.5) {
		float2 eg = float2(yaw * 18.0 + sin(t * 0.6 + wpos.y * 0.2), wpos.y * 0.45 - t * 1.1);
		float2 ec = floor(eg);
		float2 ef = frac(eg) - 0.5 - (hash22(ec) - 0.5) * 0.4;
		float ember = smoothstep(0.14, 0.02, length(ef)) * step(0.7, hash21(ec));
		float plume = smoothstep(0.4, 0.78, fbmSky(wxz * 0.12 - float2(t * 0.2, t * 0.35)));
		col += float3(1.0, 0.32, 0.04) * (ember * 0.85 + plume * 0.32) * veil;
	} else if (th < 6.5) {
		float2 mp = wxz * 0.14 + CurlSky(wxz * 0.08 + t * 0.06) * 0.8;
		float sheet = smoothstep(0.34, 0.74, fbmSky(mp + float2(t * 0.14, -t * 0.09)));
		float2 wg = wxz * 0.45;
		float2 wc = floor(wg);
		float wisp = exp(-length(frac(wg) - 0.5 - (hash22(wc) - 0.5) * 0.35) * 10.0) * step(0.74, hash21(wc));
		col = lerp(col, float3(0.26, 0.48, 0.2), saturate(sheet * far * room) * 0.52);
		col += float3(0.45, 1.0, 0.35) * wisp * veil * 0.55;
	} else if (th < 7.5) {
		float2 p = PlaneXZ(rd, 18.0, 0.12) - float2(32.0, 32.0);
		float r = length(p) * 0.045;
		float ring = exp(-abs(frac(r * 2.5 - t * 0.06) - 0.5) * 24.0);
		float spin = 0.5 + 0.5 * sin(atan2(p.y, p.x) * 8.0 - t * 0.45);
		col += float3(0.45, 0.75, 1.0) * ring * spin * veil * 0.58;
	} else if (th < 8.5) {
		float c1 = CausAt(wxz * 0.24 + float2(t * 0.12, -t * 0.08), t);
		float shaft = pow(0.5 + 0.5 * sin(yaw * 12.0 + t * 0.2), 10.0);
		col += float3(0.18, 0.72, 1.0) * (c1 * 0.45 + shaft * 0.35) * veil;
	} else if (th < 9.5) {
		float ang = atan2(rd.y + 0.08, sin(yaw - 0.6));
		float rad = length(float2(sin(yaw - 0.6), rd.y + 0.08));
		float arc = exp(-pow((rad - 0.42) * 9.0, 2.0));
		float3 bow = lerp(float3(1, 0.35, 0.35), lerp(float3(0.4, 1, 0.45), float3(0.35, 0.55, 1), saturate(ang)), saturate(ang));
		float spray = smoothstep(0.38, 0.76, fbmSky(wxz * 0.18 - float2(0, t * 0.8)));
		col += (bow * arc * 0.55 + float3(0.82, 0.94, 1.0) * spray * 0.32) * veil;
	} else if (th < 10.5) {
		float2 u = float2(yaw * 2.0, rd.y * 2.6);
		float n = fbmSky(u + t * 0.02) * 0.6 + fbmSky(u * 2.2 - t * 0.015) * 0.5;
		float2 sg = float2(yaw * 120.0, rd.y * 180.0);
		float star = step(0.99, hash21(floor(sg))) * saturate(1.0 - length(frac(sg) - 0.5) * 5.5);
		col += lerp(float3(0.48, 0.12, 0.82), float3(0.1, 0.45, 1.0), saturate(n)) * n * veil * 0.58;
		col += float3(0.92, 0.96, 1.0) * star * veil * 0.85;
	} else if (th < 11.5) {
		float2 g = abs(frac(wxz * 0.35) - 0.5);
		float grid = smoothstep(0.45, 0.495, max(g.x, g.y));
		float scan = pow(0.5 + 0.5 * sin(wpos.y * 2.5 - t * 2.4), 10.0);
		float2 bc = floor(wxz * 0.35);
		float blink = step(0.8, hash21(bc)) * (0.4 + 0.6 * sin(t * 3.2 + hash21(bc) * 14.0));
		col += float3(0.25, 0.88, 1.0) * (grid * 0.45 + scan * 0.28 + blink * grid * 0.55) * veil;
	} else if (th < 12.5) {
		float3 moonDir = normalize(float3(0.45, 0.38, 0.8));
		float md = saturate(dot(rd, moonDir));
		float moon = pow(md, 180.0) + pow(md, 16.0) * 0.35;
		float ray = pow(0.5 + 0.5 * sin(yaw * 14.0 + t * 0.15), 10.0);
		col += (float3(0.92, 0.86, 1.0) * moon * 0.75 + float3(0.68, 0.48, 1.0) * ray * 0.35) * veil;
	} else {
		float2 lg = wxz * 0.28 + float2(sin(t * 0.2), cos(t * 0.18)) * 0.35;
		float2 lc = floor(lg);
		float2 lf = frac(lg) - 0.5 - (hash22(lc) - 0.5) * 0.4;
		float lh = hash21(lc);
		float orb = exp(-dot(lf, lf) * 16.0) * step(0.56, lh);
		float bob = 0.55 + 0.45 * sin(t * 1.4 + lh * 12.0);
		col += float3(1.0, 0.74, 0.36) * orb * bob * veil * 0.82;
	}
}

void ThemeWorld(inout float3 col, float2 uv, float th, float t, float z)
{
	if (th < 0) return;
	float tier = StageTier();
	float presence = (tier < 0.5) ? 0.28 : (0.42 + tier * 0.12);
	float sky = saturate((0.4 - uv.y) * 2.5);
	float hasZ = step(0.02, z);
	float far = hasZ * smoothstep(0.5, 0.84, z);
	float zMax = z, zMin = z;
	bool needEdge = (tier > 2.5 && hasZ > 0.5);
	bool needRoom = (far * max(presence, 0.72) >= 0.02 && hasZ > 0.5);
	if (needEdge || needRoom) {
		float2 px = Screen.zw * (tier > 5.5 ? 2.4 : (needEdge ? 1.8 : 2.2));
		float zr = DepthMap.SampleLevel(SL, uv + float2(px.x, 0), 0).r;
		float zl = DepthMap.SampleLevel(SL, uv - float2(px.x, 0), 0).r;
		float zu = DepthMap.SampleLevel(SL, uv + float2(0, px.y), 0).r;
		float zd = DepthMap.SampleLevel(SL, uv - float2(0, px.y), 0).r;
		zMax = max(max(zr, zl), max(zu, zd));
		zMin = min(min(zr, zl), min(zu, zd));
	}
	StageSignature(col, uv, th, t, z, sky, far, max(presence, 0.72), zMax);
	if (needEdge) {
		float front = saturate((zMax - z) * (tier > 5.5 ? 12.0 : 9.0));
		float back = saturate((z - zMin) * 6.0);
		float3 edge = float3(1, 0.97, 0.92);
		float rimK = (tier > 5.5) ? 0.16 : ((tier > 4.5) ? 0.12 : 0.08);
		col += edge * front * rimK;
		col *= 1.0 - back * (tier > 5.5 ? 0.14 : 0.1);
	}
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
	if (Q.x > 2.5) {
		c += (Tex.Sample(SL, i.uv + step * 5.5).rgb + Tex.Sample(SL, i.uv - step * 5.5).rgb) * (Q.x > 5.5 ? 0.02 : 0.012);
		c += (Tex.Sample(SL, i.uv + step * 7.5).rgb + Tex.Sample(SL, i.uv - step * 7.5).rgb) * (Q.x > 5.5 ? 0.012 : 0.006);
		c *= (Q.x > 5.5) ? 0.94 : 0.97;
	}
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
	int shaftN = (Q.x > 5.5) ? 16 : ((Q.x > 2.5) ? 12 : 8);
	// 筋は太陽が空にあるとき、空の側だけ。盤面へ放射状に広げない。
	float sky = saturate((0.46 - i.uv.y) * 2.6);
	float sunUp = saturate((0.4 - suv.y) * 3.2);
	float shaftMask = Extra.w * saturate(0.7 - dist) * sky * sunUp;
	[branch] if (shaftMask > 1e-4) {
		[loop] for (int k = 1; k <= shaftN; k++) {
			float t = k / (float)shaftN;
			float w = 1. - t;
			shaft += ShadowMap.SampleLevel(SL, i.uv + toSun * t * 0.85, 0).rgb * w;
			wsum += w;
		}
	}
	float th = FogColor.a;
	float3 grade = float3(1, 1, 1);
	float shaftK = 1;
	if (th < 0) shaftK = 0;
	else if (th < 0.5) { grade = float3(1.02, 1.03, 0.97); shaftK = 0.85; }
	else if (th < 1.5) { grade = float3(1.04, 0.98, 0.92); shaftK = 0.45; }
	else if (th < 2.5) { grade = float3(0.97, 1.03, 0.97); shaftK = 0.7; }
	else if (th < 3.5) { grade = float3(0.98, 0.99, 1.03); shaftK = 0.1; }
	else if (th < 4.5) { grade = float3(1.04, 1.0, 0.92); shaftK = 0.75; }
	else if (th < 5.5) { grade = float3(1.03, 0.95, 0.9); shaftK = 0.08; }
	else if (th < 6.5) { grade = float3(0.97, 1.02, 0.95); shaftK = 0.3; }
	else if (th < 7.5) { grade = float3(0.97, 0.98, 1.03); shaftK = 0.55; }
	else if (th < 8.5) { grade = float3(0.92, 0.98, 1.03); shaftK = 0.12; }
	else if (th < 9.5) { grade = float3(0.98, 0.99, 1.02); shaftK = 0.9; }
	else if (th < 10.5) { grade = float3(0.96, 0.98, 1.14); shaftK = 0.08; }
	else if (th < 11.5) { grade = float3(0.9, 0.95, 1.04); shaftK = 0.08; }
	else if (th < 12.5) { grade = float3(1.08, 0.96, 1.14); shaftK = 0.1; }
	else { grade = float3(0.96, 0.97, 1.06); shaftK = 0.55; }
	float gt = Eye.w;
	float zpix = DepthMap.SampleLevel(SL, i.uv, 0).r;
	float shaftAmt = 0.22 + saturate(StageTier() / 6.0) * 0.28;
	float3 shaftAdd = shaft / max(wsum, 1e-3) * shaftMask * shaftAmt * shaftK;
	float2 px = Screen.zw;
	float vig = 1;
	ThemeWorld(col, i.uv, th, gt, zpix);
	if (StageTier() > 1.5 && zpix > 0.32) {
		float airZ = smoothstep(0.32, 0.86, zpix);
		float3 scatter = FogColor.rgb;
		if (th < 0.5) scatter = float3(0.7, 0.85, 1);
		else if (th < 1.5) scatter = float3(1, 0.72, 0.42);
		else if (th < 3.5) scatter = float3(0.78, 0.88, 1);
		else if (th < 4.5) scatter = float3(1, 0.8, 0.45);
		else if (th < 5.5) scatter = float3(1, 0.4, 0.15);
		else if (th < 8.5) scatter = float3(0.3, 0.7, 0.95);
		else if (th < 10.5) scatter = float3(0.35, 0.28, 0.7);
		col = lerp(col, col + scatter * 0.1, airZ * 0.42);
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
		float spread = (Q.x > 5.5) ? 2.4 : ((Q.x > 4.5) ? 2.0 : 1.6);
		float mixk = (Q.x > 5.5) ? 0.16 : ((Q.x > 4.5) ? 0.12 : 0.1);
		float nearKeep = (zpix > 0.02) ? lerp(0.22, 1.0, smoothstep(0.28, 0.62, zpix)) : 0.35;
		float2 ca = (i.uv - 0.5) * px * spread;
		float3 fr = float3(Tex.Sample(SL, i.uv + ca).r, col.g, Tex.Sample(SL, i.uv - ca).b);
		col = lerp(col, fr, mixk * nearKeep);
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
	WeatherFx(col, i.uv, Extra.x, gt);
	HourFx(col, i.uv, Extra.y, gt);
	return float4(saturate(col), 1);
}

float4 PSGradeTiny(QOut i) : SV_Target
{
	float3 scene = Tex.Sample(SL, i.uv).rgb;
	float y = dot(scene, float3(0.25, 0.5, 0.25));
	scene = lerp(y.xxx, scene, 1.04);
	float2 q = (i.uv - 0.5) * float2(Screen.x * Screen.w, 1);
	scene *= 1. - saturate(dot(q, q) * 0.35) * 0.08;
	ThemeWorld(scene, i.uv, FogColor.a, Eye.w, 0);
	WeatherFx(scene, i.uv, Extra.x, Eye.w);
	HourFx(scene, i.uv, Extra.y, Eye.w);
	return float4(saturate(scene), 1);
}

// パネル。uv は矩形内 0..1。頂点色が塗り、p0/p1（または定数）が縁と寸法。
float4 PSUI(HOut i) : SV_Target
{
	float4 prm = i.p0.x > 0.5 ? i.p0 : Params;
	float3 brd = i.p0.x > 0.5 ? i.p1.rgb : Tint.rgb;
	float2 size = max(prm.xy, float2(2, 2));
	float rad = max(prm.z, 1);
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
	col = lerp(col, brd, border * 0.85);
	float a = body * prm.w;
	return float4(col * a, a);
}

// 天候・濡れ飛び散り。CPU の m_parts を通さず、CS で進めて既存 GSP で広げる。
struct WxGpu
{
	float4 p;     // xyz
	float4 vel;   // xyz 速度 w=伸び
	float4 sz;    // x=大きさ y=形 z=寿命 w=最大寿命
	float4 c;
	float4 extra; // x=重力 y=抵抗 z=種別 w=終端サイズ
};
StructuredBuffer<WxGpu> WxDraw : register(t11);
RWStructuredBuffer<WxGpu> WxSim : register(u0);
StructuredBuffer<float4> WxAct : register(t12);
StructuredBuffer<float> WxH : register(t13);
cbuffer WxParm : register(b5)
{
	float4 WxFwd;
	float4 WxSide;
	float4 WxWind; // xyz 風 w=突風
	float4 WxCnt;  // x=dt y=人数 z=粒数 w=作り直し
	float4 WxMix;  // x=飛び散り枠 y=天候
};

uint WxPcg(uint v)
{
	v = v * 747796405u + 2891336453u;
	uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
	return (w >> 22u) ^ w;
}
float WxRnd(inout uint s)
{
	s = WxPcg(s);
	return (s & 0xFFFFFFu) / 16777216.0;
}

float WxHeight(float x, float z)
{
	int ix = (int)floor(x);
	int iz = (int)floor(z);
	if (ix < 0 || iz < 0 || ix > 63 || iz > 63) return -99.0;
	return WxH[iz * 64 + ix];
}

void WxBirthSky(inout WxGpu o, uint seed, float wx, float3 eye, float3 fwd, float3 side, float3 wind, float gust)
{
	float along = 3.4 + WxRnd(seed) * 12.6;
	float lat = (WxRnd(seed) * 2.0 - 1.0) * 9.0;
	float up = -1.5 + WxRnd(seed) * 8.0;
	o.p.xyz = eye + fwd * along + side * lat + float3(0, up, 0);
	o.p.w = 1;
	o.extra.z = wx;
	if (wx > 0.5 && wx < 1.5) {
		float sp = 10.0 + WxRnd(seed) * 5.0;
		float w = 1.6 * (1.0 + gust * 1.2);
		o.vel = float4(wind * w + float3(WxRnd(seed) * 0.8 - 0.4, -sp, WxRnd(seed) * 0.8 - 0.4), 0.04);
		o.sz = float4(0.028, 0, 1.6, 1.6);
		o.c = float4(0.72, 0.84, 1, 0.9);
		o.extra.xyw = float3(16.0, 0.15, 0.018);
	} else if (wx > 4.5 && wx < 5.5) {
		float sp = 14.0 + WxRnd(seed) * 6.0;
		float w = 5.2 * (1.0 + gust * 1.2);
		o.vel = float4(wind * w + float3(WxRnd(seed) * 0.8 - 0.4, -sp, WxRnd(seed) * 0.8 - 0.4), 0.05);
		o.sz = float4(0.024, 0, 1.5, 1.5);
		o.c = float4(0.72, 0.84, 1, 0.92);
		o.extra.xyw = float3(16.0, 0.15, 0.016);
		o.extra.z = 2;
	} else if (wx > 1.5 && wx < 2.5) {
		o.vel = float4(wind * 0.7 + float3(WxRnd(seed) * 0.6 - 0.3, -1.8 + WxRnd(seed) * 1.0, WxRnd(seed) * 0.6 - 0.3), 0);
		o.sz = float4(0.055, 2, 4.0, 4.0);
		o.c = float4(0.96, 0.98, 1, 1);
		o.extra.xyw = float3(0.8, 0.35, 0.04);
		o.extra.z = 3;
	} else if (wx > 2.5 && wx < 3.5) {
		o.vel = float4(wind * 0.5 + float3(WxRnd(seed) * 0.5 - 0.25, -2.6 + WxRnd(seed) * 1.4, WxRnd(seed) * 0.5 - 0.25), 0.02);
		o.sz = float4(0.04, 0, 3.2, 3.2);
		o.c = float4(0.85, 0.4, 0.18, 1);
		o.extra.xyw = float3(1.6, 0.25, 0.025);
		o.extra.z = 4;
	} else if (wx > 6.5 && wx < 7.5) {
		o.vel = float4(wind * 0.8 + float3(WxRnd(seed) * 0.4 - 0.2, -18.0 + WxRnd(seed) * 5.0, WxRnd(seed) * 0.4 - 0.2), 0.02);
		o.sz = float4(0.048, 7, 1.4, 1.4);
		o.c = float4(0.9, 0.95, 1, 1);
		o.extra.xyw = float3(22.0, 0.05, 0.04);
		o.extra.z = 5;
	} else if (wx > 5.5 && wx < 6.5) {
		o.vel = float4(wind * (5.0 + WxRnd(seed) * 3.0) + float3(0, -2.2 + WxRnd(seed) * 1.6, 0), 0.04);
		o.sz = float4(0.032, 0, 1.8, 1.8);
		o.c = float4(0.92, 0.74, 0.38, 1);
		o.extra.xyw = float3(3.0, 0.2, 0.02);
		o.extra.z = 6;
	} else if (wx > 7.5 && wx < 8.5) {
		o.vel = float4(wind * 0.9 + float3(WxRnd(seed) * 0.6 - 0.3, -1.1 + WxRnd(seed) * 0.7, WxRnd(seed) * 0.6 - 0.3), 0.01);
		o.sz = float4(0.06, 2, 4.5, 4.5);
		o.c = float4(1, 0.72, 0.8, 1);
		o.extra.xyw = float3(0.45, 0.3, 0.04);
		o.extra.z = 7;
	} else if (wx > 3.5 && wx < 4.5) {
		o.vel = float4(wind * 0.35 + float3(WxRnd(seed) * 0.4 - 0.2, WxRnd(seed) * 0.35 - 0.15, WxRnd(seed) * 0.4 - 0.2), 0);
		o.sz = float4(0.14, 0, 3.5, 3.5);
		o.c = float4(0.78, 0.82, 0.86, 0.22);
		o.extra.xyw = float3(0, 0.15, 0.22);
		o.extra.z = 8;
	} else {
		float gy = WxHeight(o.p.x, o.p.z);
		if (gy > -90.0) o.p.y = gy + 0.1 + WxRnd(seed) * 0.5;
		o.vel = float4(WxRnd(seed) * 0.4 - 0.2, 0.8 + WxRnd(seed) * 1.0, WxRnd(seed) * 0.4 - 0.2, 0.02);
		o.sz = float4(0.05, 0, 1.6, 1.6);
		o.c = float4(1, 0.7, 0.35, 0.45);
		o.extra.xyw = float3(-0.4, 0.2, 0.08);
		o.extra.z = 9;
	}
}

void WxBirthDrip(inout WxGpu o, uint seed, float3 wind, float gust, float storm)
{
	uint n = (uint)max(WxCnt.y, 1.0);
	float4 a = WxAct[seed % n];
	if (a.w < 0.05) {
		WxBirthSky(o, seed, WxMix.y, Eye.xyz, WxFwd.xyz, WxSide.xyz, wind, gust);
		return;
	}
	float fromHair = WxRnd(seed) < 0.45 ? 1.0 : 0.0;
	float ht = a.w;
	float3 pos = a.xyz + float3(WxRnd(seed) * 0.24 - 0.12, ht * (fromHair ? (0.78 + WxRnd(seed) * 0.24) : (0.38 + WxRnd(seed) * 0.34)), WxRnd(seed) * 0.24 - 0.12);
	float w = lerp(2.4, 6.5, storm) * (0.7 + gust * 1.4);
	o.p = float4(pos, 1);
	o.vel = float4(wind * w + float3(WxRnd(seed) * 1.6 - 0.8, 0.4 + WxRnd(seed) * 1.8, WxRnd(seed) * 1.6 - 0.8), 0.03);
	o.sz = float4(0.016, 0, 0.28 + WxRnd(seed) * 0.27, 0.45);
	o.sz.w = o.sz.z;
	o.c = float4(0.78, 0.88, 1, 0.85);
	o.extra = float4(14.0, 0.45, 10.0, 0.008);
}

[numthreads(64, 1, 1)]
void CSWx(uint3 dtid : SV_DispatchThreadID)
{
	uint id = dtid.x;
	uint n = (uint)WxCnt.z;
	if (id >= n) return;
	float dt = min(WxCnt.x, 0.05);
	float wx = WxMix.y;
	float3 wind = WxWind.xyz;
	float gust = WxWind.w;
	uint seed = id * 1973u + (uint)(Eye.w * 4096.0) * 17u + 1u;
	WxGpu o = WxSim[id];
	if (wx < 0.5) {
		o.sz.x = 0; o.c.a = 0; o.sz.z = 0;
		WxSim[id] = o;
		return;
	}
	uint dripN = (uint)WxMix.x;
	int reset = WxCnt.w > 0.5;
	if (reset || o.sz.z <= 0.0 || o.sz.x < 0.0005) {
		if (id < dripN && ((wx > 0.5 && wx < 1.5) || (wx > 4.5 && wx < 5.5)) && WxCnt.y > 0.5)
			WxBirthDrip(o, seed, wind, gust, wx > 4.5 ? 1.0 : 0.0);
		else
			WxBirthSky(o, seed, wx, Eye.xyz, WxFwd.xyz, WxSide.xyz, wind, gust);
		WxSim[id] = o;
		return;
	}
	float3 prev = o.p.xyz;
	float2 air = AirMain() + AirBreeze();
	o.vel.xz += air * dt * 4.2;
	o.vel.y += AirLift() * dt * 3.2;
	o.vel.y -= o.extra.x * dt;
	o.vel.xyz *= exp(-o.extra.y * dt);
	o.p.xyz += o.vel.xyz * dt;
	o.sz.z -= dt;
	float t = 1.0 - o.sz.z / max(o.sz.w, 1e-4);
	o.sz.x = lerp(o.sz.x, o.extra.w, saturate(t * 0.35));
	float a = o.c.a;
	a = saturate(o.sz.z / max(o.sz.w, 1e-4) * 3.0) * saturate(t * 8.0 + 0.1);
	if (o.extra.z > 7.5 && o.extra.z < 8.5) a *= 0.22;
	o.c.a = a;
	int kind = (int)(o.extra.z + 0.1);
	if (kind >= 1 && kind <= 7 && o.vel.y < 0.0) {
		float gy = WxHeight(o.p.x, o.p.z);
		if (gy > -90.0 && prev.y > gy + 0.02 && o.p.y <= gy + 0.03) {
			if (kind == 1 || kind == 2 || kind == 10) {
				o.p.y = gy + 0.04;
				float ang = WxRnd(seed) * 6.2831853;
				float sp = 0.8 + WxRnd(seed) * 1.8;
				o.vel = float4(cos(ang) * sp, 1.2 + WxRnd(seed) * 1.4, sin(ang) * sp, 0.03);
				o.sz = float4(0.02, 0, 0.22, 0.22);
				o.c = float4(0.75, 0.86, 1, 0.8);
				o.extra = float4(18.0, 0.3, 11.0, 0.008);
			} else if (kind == 3) {
				o.p.y = gy + 0.03;
				o.vel = 0; o.extra.x = 0; o.sz.z = min(o.sz.z, 0.35);
			} else if (kind == 5 && o.sz.y > 6.5) {
				o.p.y = gy + 0.03;
				o.vel.y = -o.vel.y * 0.42;
				o.vel.xz *= 0.7;
				o.sz.y = 0;
			} else {
				o.sz.z = 0; o.sz.x = 0;
			}
		}
	}
	if (o.sz.z <= 0.0) o.sz.x = 0;
	WxSim[id] = o;
}

PinP VSPWX(uint id : SV_VertexID)
{
	WxGpu s = WxDraw[id];
	PinP o;
	o.p = s.p.xyz;
	o.vel = s.vel;
	o.sz = s.sz;
	o.c = s.c;
	return o;
}
