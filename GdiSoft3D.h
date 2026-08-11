#pragma once
// GDI Soft3D — D3D-like software pipeline (no OpenGL/Direct3D).
// Framebuffer triangle raster + Z + fog/DOF hooks. Legacy Scene API kept.
#include "GdiSoftFB.h"
#include "GdiSoft2D.h"
#include <afxwin.h>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GdiSoft3D
{
	struct View {
		float cosYaw = 1.f, sinYaw = 0.f;
		float cosPitch = 1.f, sinPitch = 0.f;
		float camD = 3.2f;
		float scale = 1.f;
		float originX = 0.f;
		float originY = 0.f;
	};

	struct Cam {
		float yawDeg = -28.f;
		float pitchDeg = 32.f;
		float zoom = 1.f;
	};

	static constexpr float kZoomMin = 0.35f;
	static constexpr float kZoomMax = 4.0f;
	// Scene face は static 固定配列プール（Begin 内）。
	static constexpr int kMaxFaces = 2048;

	enum FillMode { FillSolid = 0, FillWire = 1 };
	enum FogMode { FogNone = 0, FogLinear = 1, FogExp = 2 };

	struct Vertex {
		float x, y, z;
		float u, v;
		DWORD color; // BGRA
	};

	inline COLORREF Shade(COLORREF c, float f)
	{
		int r = (int)(GetRValue(c) * f + 0.5f);
		int g = (int)(GetGValue(c) * f + 0.5f);
		int b = (int)(GetBValue(c) * f + 0.5f);
		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;
		if (r == 20 && g == 20 && b == 20) b = 23;
		return RGB(r, g, b);
	}

	// カメラ空間 Z。左右の壁はカメラ面をまたぐのでわずかに負まで許可（-camD 未満は射影不能）
	// eye が論理位置より前方 0.8 なので、マス後方〜左右の壁は z≈-1 付近まで必要
	static constexpr float kNearZ = -1.20f;
	static constexpr float kProjEps = 0.05f;

	inline void ToView(const View& v, float x, float y, float z,
		float& vx, float& vy, float& vz)
	{
		const float rx = x * v.cosYaw - z * v.sinYaw;
		const float rz = x * v.sinYaw + z * v.cosYaw;
		vx = rx;
		vy = y * v.cosPitch + rz * v.sinPitch;
		vz = rz * v.cosPitch - y * v.sinPitch;
	}

	inline void ProjectView(const View& v, float vx, float vy, float vz,
		float& sx, float& sy)
	{
		float denom = v.camD + vz;
		if (denom < kProjEps) denom = kProjEps;
		const float p = v.camD / denom;
		sx = v.originX + vx * p * v.scale;
		sy = v.originY - vy * p * v.scale;
	}

	inline void Project(const View& v, float x, float y, float z, POINT& out)
	{
		float vx, vy, vz, sx, sy;
		ToView(v, x, y, z, vx, vy, vz);
		if (vz < kNearZ) vz = kNearZ;
		ProjectView(v, vx, vy, vz, sx, sy);
		out.x = (long)floorf(sx + 0.5f);
		out.y = (long)floorf(sy + 0.5f);
	}

	inline void ProjectEx(const View& v, float x, float y, float z,
		float& sx, float& sy, float& depth)
	{
		float vx, vy, vz;
		ToView(v, x, y, z, vx, vy, vz);
		depth = vz;
		float zProj = vz;
		if (zProj < kNearZ) zProj = kNearZ;
		ProjectView(v, vx, vy, zProj, sx, sy);
	}

	inline float DepthOf(const View& v, float x, float y, float z)
	{
		float vx, vy, vz;
		ToView(v, x, y, z, vx, vy, vz);
		(void)vx; (void)vy;
		return vz;
	}

	inline void BuildView(int width, int height, const Cam& cam,
		const float boxes[][6], int boxCount, View& v)
	{
		const float yaw = cam.yawDeg * (float)(M_PI / 180.0);
		const float pit = cam.pitchDeg * (float)(M_PI / 180.0);
		v.cosYaw = cosf(yaw);
		v.sinYaw = sinf(yaw);
		v.cosPitch = cosf(pit);
		v.sinPitch = sinf(pit);
		v.camD = 3.2f;

		const float kProbeScale = 1024.0f;
		v.scale = kProbeScale;
		v.originX = 0.0f;
		v.originY = 0.0f;

		float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
		for (int b = 0; b < boxCount; ++b) {
			for (int c = 0; c < 8; ++c) {
				POINT p;
				Project(v,
					(c & 1) ? boxes[b][1] : boxes[b][0],
					(c & 2) ? boxes[b][3] : boxes[b][2],
					(c & 4) ? boxes[b][5] : boxes[b][4], p);
				const float fx = (float)p.x, fy = (float)p.y;
				if (fx < minX) minX = fx;
				if (fx > maxX) maxX = fx;
				if (fy < minY) minY = fy;
				if (fy > maxY) maxY = fy;
			}
		}
		float bw = maxX - minX; if (bw < 1.0f) bw = 1.0f;
		float bh = maxY - minY; if (bh < 1.0f) bh = 1.0f;
		float s = (float)width * 0.92f * kProbeScale / bw;
		const float sH = (float)height * 0.90f * kProbeScale / bh;
		if (sH < s) s = sH;
		if (s < 8.0f) s = 8.0f;
		float zoom = cam.zoom;
		if (zoom < kZoomMin) zoom = kZoomMin;
		if (zoom > kZoomMax) zoom = kZoomMax;
		s *= zoom;
		v.scale = s;
		v.originX = (float)width * 0.5f - (minX + maxX) * 0.5f / kProbeScale * s;
		v.originY = (float)height * 0.5f - (minY + maxY) * 0.5f / kProbeScale * s;
	}

	// ---- Device / Context (D3D-like) ----
	struct Texture {
		int w = 0, h = 0;
		std::vector<DWORD> pixels; // BGRA
		bool LoadFromHdc(HDC src, int sw, int sh);
		DWORD Sample(float u, float v) const;
	};

	struct Context {
		GdiSoftFB::Framebuffer fb;
		View view;
		Cam cam;
		FillMode fillMode = FillSolid;
		bool depthTest = true;
		bool depthWrite = true;
		bool alphaBlend = false;
		bool cullBack = false;
		FogMode fogMode = FogNone;
		COLORREF fogColor = RGB(18, 20, 28);
		float fogStart = 0.2f;
		float fogEnd = 1.6f;
		float fogDensity = 0.8f;
		bool edgeOverlay = false;
		COLORREF edgeColor = RGB(0, 0, 0);
		bool dofEnable = false;
		float dofNear = -0.2f;
		float dofFar = 1.4f;
		int dofMaxRadius = 3;
		// Soft2D post (EndFrame で適用)
		bool postVignette = false;
		float postVignetteStr = 0.4f;
		bool postGlow = false;
		bool postSaturate = false;
		float postSatAmount = 1.2f;
		const Texture* texture = nullptr;
		DWORD flatColor = 0xFFFFFFFFu;

		bool Create(int w, int h);
		void BeginFrame(COLORREF clear = RGB(18, 20, 28));
		void EndFrame(); // fog/edge/DOF post
		BOOL Present(HDC dst, int x = 0, int y = 0) const { return fb.Present(dst, x, y); }
		BOOL Present(CDC& dc, int x = 0, int y = 0) const { return fb.Present(dc, x, y); }

		void SetCamera(const Cam& c) { cam = c; }
		void SetView(const View& v) { view = v; }
		void SetViewportFit(const float boxes[][6], int boxCount);
		void SetFillMode(FillMode m) { fillMode = m; }
		void SetTexture(const Texture* t) { texture = t; }
		void SetFog(FogMode m, COLORREF col, float start, float end, float density = 0.8f);
		void SetDof(bool on, float zNear, float zFar, int maxRadius);
		void SetEdgeOverlay(bool on, COLORREF col = RGB(0, 0, 0)) { edgeOverlay = on; edgeColor = col; }

		void DrawTriangles(const Vertex* verts, int count); // count%3==0
		void DrawIndexed(const Vertex* verts, int vcount, const int* idx, int icount);

		void DrawBox(float xL, float xR, float yTop, float z0, float z1, COLORREF col, float yBase = 0.f);
		void DrawWireBox(float xL, float xR, float yTop, float z0, float z1, COLORREF col, float yBase = 0.f);
		void DrawQuad(float x0, float y0, float z0, float x1, float y1, float z1,
			float x2, float y2, float z2, float x3, float y3, float z3, COLORREF col, BYTE alpha = 255);
		void DrawTexturedQuad(float x0, float y0, float z0, float x1, float y1, float z1,
			float x2, float y2, float z2, float x3, float y3, float z3);
		// UV 明示（ワールド座標にレンガを貼る用）。透視補正は RasterTri 側
		void DrawTexturedQuadUV(
			float x0, float y0, float z0, float u0, float v0,
			float x1, float y1, float z1, float u1, float v1,
			float x2, float y2, float z2, float u2, float v2,
			float x3, float y3, float z3, float u3, float v3);
		void DrawSphere(float cx, float cy, float cz, float radius, COLORREF col, int slices = 12, int stacks = 8, BYTE alpha = 255);
		void DrawGrid(float x0, float x1, float z0, float z1, float y, int div, COLORREF col);
		void DrawLine(float x0, float y0, float z0, float x1, float y1, float z1, COLORREF col);
		void DrawStereoBars(float xMin, float xMax, int bins,
			const float* levL, const float* levR,
			float zL0, float zL1, float zR0, float zR1,
			float maxY, float gapFrac, COLORREF colL, COLORREF colR);
		// L=左半分 / R=右半分（同じ奥行き）。左右並びステレオ用。
		void DrawStereoBarsLR(float xMin, float xMax, int bins,
			const float* levL, const float* levR,
			float z0, float z1, float maxY, float gapFrac,
			COLORREF colL, COLORREF colR);
		// 滑らかな波形リボン（Y=振幅、X=時間）
		void DrawWaveRibbon(float x0, float x1, float z, float yMid, float yAmp,
			const float* samples, int n, COLORREF col, float thickness = 0.02f);
		// 床ミラー（半透明に薄く反射）
		void DrawMirrorFloor(float xL, float xR, float z0, float z1, COLORREF col, float alpha = 0.35f);
		// ネオン風エッジ付きボックス
		void DrawNeonBox(float xL, float xR, float yTop, float z0, float z1,
			COLORREF col, float yBase = 0.f);
		// トーラス（ドーナツ）
		void DrawTorus(float cx, float cy, float cz, float R, float r, COLORREF col,
			int slices = 16, int stacks = 10);

		// ---- HUD 2D（画面空間・深度無視。回転クワッド／三角／線）----
		void HudFillTri(float x0, float y0, float x1, float y1, float x2, float y2,
			COLORREF col, BYTE alpha = 220);
		void HudFillQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
			COLORREF col, BYTE alpha = 220);
		void HudLine(float x0, float y0, float x1, float y1, COLORREF col, BYTE alpha = 255);

		DWORD ApplyFog(DWORD c, float depth) const;

	private:
		void RasterTri(const Vertex& a, const Vertex& b, const Vertex& c);
		void PostFog();
		void PostEdge();
		void PostDof();
	};

	// Demo scene for engine smoke (box/sphere/wire/texture/fog/DOF)
	void PresentApiDemo(CDC& dc, const CRect& rc);

	// ---- Legacy Scene (compat) ----
	struct Face {
		POINT p[4];
		float depth;
		COLORREF fill;
		float wz[4]; // world-projected depth per corner for FB raster
	};

	struct Scene {
		// 固定配列プール（ヒープ new/delete なし・巨大スタック確保なし）。UIスレッド専用。
		Face* faces = nullptr;
		int n = 0;
		const View* view = nullptr;

		void Begin(const View& v) {
			static Face s_pool[kMaxFaces];
			faces = s_pool;
			view = &v;
			n = 0;
		}

		bool AddFace4(float x0, float y0, float z0,
			float x1, float y1, float z1,
			float x2, float y2, float z2,
			float x3, float y3, float z3, COLORREF fill)
		{
			if (!view || !faces || n >= kMaxFaces) return false;
			Face& f = faces[n++];
			Project(*view, x0, y0, z0, f.p[0]);
			Project(*view, x1, y1, z1, f.p[1]);
			Project(*view, x2, y2, z2, f.p[2]);
			Project(*view, x3, y3, z3, f.p[3]);
			f.wz[0] = DepthOf(*view, x0, y0, z0);
			f.wz[1] = DepthOf(*view, x1, y1, z1);
			f.wz[2] = DepthOf(*view, x2, y2, z2);
			f.wz[3] = DepthOf(*view, x3, y3, z3);
			f.depth = 0.25f * (f.wz[0] + f.wz[1] + f.wz[2] + f.wz[3]);
			f.fill = fill;
			return true;
		}

		void AddBox(float xL, float xR, float yTop, float zNear, float zFar,
			COLORREF col, float yBase = 0.f)
		{
			if (xR < xL) { float t = xL; xL = xR; xR = t; }
			if (zFar < zNear) { float t = zNear; zNear = zFar; zFar = t; }
			if (yTop < yBase + 0.004f) yTop = yBase + 0.004f;
			const float eps = 0.0015f;
			xL -= eps; xR += eps;
			AddFace4(xL, yTop, zNear, xR, yTop, zNear, xR, yTop, zFar, xL, yTop, zFar, col);
			AddFace4(xL, yBase, zNear, xR, yBase, zNear, xR, yTop, zNear, xL, yTop, zNear, Shade(col, 0.72f));
			AddFace4(xR, yBase, zNear, xR, yBase, zFar, xR, yTop, zFar, xR, yTop, zNear, Shade(col, 0.55f));
		}

		void AddFloor(float xL, float xR, float z0, float z1, COLORREF col)
		{
			AddBox(xL, xR, 0.008f, z0, z1, col, 0.f);
		}

		void AddStereoBars(float xMin, float xMax, int bins,
			const float* levL, const float* levR,
			float zL0, float zL1, float zR0, float zR1,
			float maxY, float gapFrac,
			COLORREF colL, COLORREF colR)
		{
			if (bins < 1 || !levL) return;
			if (gapFrac < 0.f) gapFrac = 0.f;
			if (gapFrac > 0.6f) gapFrac = 0.6f;
			const float span = xMax - xMin;
			const float step = span / (float)bins;
			for (int i = 0; i < bins; ++i) {
				float tL = levL[i];
				if (tL < 0.f) tL = 0.f; if (tL > 1.f) tL = 1.f;
				const float a = xMin + step * (float)i;
				const float b = a + step;
				const float xl = a + step * gapFrac * 0.5f;
				const float xr = b - step * gapFrac * 0.5f;
				if (xr <= xl) continue;
				if (tL >= 0.025f)
					AddBox(xl, xr, tL * maxY, zL0, zL1, colL, 0.f);
				if (levR) {
					float tR = levR[i];
					if (tR < 0.f) tR = 0.f; if (tR > 1.f) tR = 1.f;
					if (tR >= 0.025f)
						AddBox(xl, xr, tR * maxY, zR0, zR1, colR, 0.f);
				}
			}
		}
		// L 左 / R 右（同一 Z）
		void AddStereoBarsLR(float xMin, float xMax, int bins,
			const float* levL, const float* levR,
			float z0, float z1, float maxY, float gapFrac,
			COLORREF colL, COLORREF colR)
		{
			if (bins < 1 || !levL) return;
			if (gapFrac < 0.f) gapFrac = 0.f;
			if (gapFrac > 0.6f) gapFrac = 0.6f;
			const float mid = 0.5f * (xMin + xMax);
			const float gap = (xMax - xMin) * 0.04f;
			const float stepL = (mid - gap * 0.5f - xMin) / (float)bins;
			const float stepR = (xMax - (mid + gap * 0.5f)) / (float)bins;
			for (int i = 0; i < bins; ++i) {
				float tL = levL[i];
				if (tL < 0.f) tL = 0.f; if (tL > 1.f) tL = 1.f;
				const float aL = xMin + stepL * (float)i;
				const float bL = aL + stepL;
				const float xl = aL + stepL * gapFrac * 0.5f;
				const float xr = bL - stepL * gapFrac * 0.5f;
				if (xr > xl && tL >= 0.02f)
					AddBox(xl, xr, tL * maxY, z0, z1, colL, 0.f);
				if (levR) {
					float tR = levR[i];
					if (tR < 0.f) tR = 0.f; if (tR > 1.f) tR = 1.f;
					const float aR = mid + gap * 0.5f + stepR * (float)i;
					const float bR = aR + stepR;
					const float xrl = aR + stepR * gapFrac * 0.5f;
					const float xrr = bR - stepR * gapFrac * 0.5f;
					if (xrr > xrl && tR >= 0.02f)
						AddBox(xrl, xrr, tR * maxY, z0, z1, colR, 0.f);
				}
			}
		}

		void Flush(CDC& dc); // FB triangle raster + present
	};

	inline BOOL BlitBitmapQuad(CDC& dst, const View& v,
		float xL, float xR, float y, float z0, float z1,
		HDC src, int srcW, int srcH)
	{
		if (!src || srcW <= 0 || srcH <= 0) return FALSE;
		POINT q[3];
		Project(v, xL, y, z0, q[0]);
		Project(v, xR, y, z0, q[1]);
		Project(v, xL, y, z1, q[2]);
		return ::PlgBlt(dst.GetSafeHdc(), q, src, 0, 0, srcW, srcH, NULL, 0, 0);
	}

	inline BOOL BlitBitmapFace(CDC& dst, const View& v,
		float xL, float xR, float yTop, float yBot, float z,
		HDC src, int srcW, int srcH)
	{
		if (!src || srcW <= 0 || srcH <= 0) return FALSE;
		POINT q[3];
		Project(v, xL, yTop, z, q[0]);
		Project(v, xR, yTop, z, q[1]);
		Project(v, xL, yBot, z, q[2]);
		return ::PlgBlt(dst.GetSafeHdc(), q, src, 0, 0, srcW, srcH, NULL, 0, 0);
	}

	inline void DrawBox(CDC& dc, const View& v, float xL, float xR, float topY,
		float z0, float z1, COLORREF col, float /*frontShade*/ = 0.72f, float baseY = 0.f)
	{
		Scene sc; sc.Begin(v);
		sc.AddBox(xL, xR, topY, z0, z1, col, baseY);
		sc.Flush(dc);
	}
	inline void DrawBoxSolid(CDC& dc, const View& v, float xL, float xR, float topY,
		float z0, float z1, COLORREF col, float baseY = 0.f)
	{
		DrawBox(dc, v, xL, xR, topY, z0, z1, col, 0.72f, baseY);
	}

	inline void ClampCam(Cam& c)
	{
		while (c.yawDeg < -180.f) c.yawDeg += 360.f;
		while (c.yawDeg > 180.f) c.yawDeg -= 360.f;
		if (c.pitchDeg < -85.f) c.pitchDeg = -85.f;
		if (c.pitchDeg > 85.f) c.pitchDeg = 85.f;
		if (c.zoom < kZoomMin) c.zoom = kZoomMin;
		if (c.zoom > kZoomMax) c.zoom = kZoomMax;
	}

	inline void CamFromSaved(Cam& c, int yaw10, int pitch10, int zoom100)
	{
		c.yawDeg = (float)yaw10 / 10.f;
		c.pitchDeg = (float)pitch10 / 10.f;
		c.zoom = (float)zoom100 / 100.f;
		ClampCam(c);
	}

	inline void CamToSaved(const Cam& c, int& yaw10, int& pitch10, int& zoom100)
	{
		yaw10 = (int)(c.yawDeg * 10.f);
		pitch10 = (int)(c.pitchDeg * 10.f);
		zoom100 = (int)(c.zoom * 100.f + 0.5f);
		if (zoom100 < 35) zoom100 = 35;
		if (zoom100 > 400) zoom100 = 400;
	}

	inline void OrbitDrag(Cam& c, float yaw0, float pitch0, CPoint origin, CPoint now)
	{
		c.yawDeg = yaw0 + (float)(now.x - origin.x) * 0.35f;
		c.pitchDeg = pitch0 - (float)(now.y - origin.y) * 0.30f;
		ClampCam(c);
	}

	inline void WheelZoom(Cam& c, short delta)
	{
		const float f = (delta > 0) ? 1.08f : (1.f / 1.08f);
		c.zoom *= f;
		ClampCam(c);
	}

	BOOL BlitTextBoard(CDC& dst, const View& v,
		float xL, float xR, float y, float z0, float z1,
		CDC& src, int srcW, int srcH);
}
