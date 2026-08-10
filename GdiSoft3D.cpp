#include "stdafx.h"
#include "GdiSoft3D.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GdiSoft3D
{
	bool Texture::LoadFromHdc(HDC src, int sw, int sh)
	{
		w = h = 0;
		pixels.clear();
		if (!src || sw <= 0 || sh <= 0) return false;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = sw;
		bi.bmiHeader.biHeight = -sh;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		HDC tmp = ::CreateCompatibleDC(src);
		void* bits = nullptr;
		HBITMAP dib = ::CreateDIBSection(tmp, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!tmp || !dib || !bits) {
			if (dib) ::DeleteObject(dib);
			if (tmp) ::DeleteDC(tmp);
			return false;
		}
		HGDIOBJ old = ::SelectObject(tmp, dib);
		::BitBlt(tmp, 0, 0, sw, sh, src, 0, 0, SRCCOPY);
		pixels.resize((size_t)sw * (size_t)sh);
		memcpy(pixels.data(), bits, pixels.size() * 4);
		for (DWORD& p : pixels) {
			if (GdiSoftFB::A(p) == 0)
				p = GdiSoftFB::PackBGRA(255, GdiSoftFB::R(p), GdiSoftFB::G(p), GdiSoftFB::B(p));
		}
		::SelectObject(tmp, old);
		::DeleteObject(dib);
		::DeleteDC(tmp);
		w = sw; h = sh;
		return true;
	}

	DWORD Texture::Sample(float u, float v) const
	{
		if (w <= 0 || h <= 0 || pixels.empty()) return 0xFFFFFFFFu;
		if (u < 0.f) u = 0.f; if (u > 1.f) u = 1.f;
		if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f;
		const int x = (int)(u * (w - 1));
		const int y = (int)(v * (h - 1));
		return pixels[(size_t)y * (size_t)w + (size_t)x];
	}

	bool Context::Create(int w, int h)
	{
		return fb.Resize(w, h, true, true);
	}

	void Context::BeginFrame(COLORREF clear)
	{
		fb.ClearColorref(clear, 255, 1e9f);
	}

	void Context::EndFrame()
	{
		PostFog();
		PostEdge();
		PostDof();
		if ((postVignette || postGlow || postSaturate) && fb.color && fb.w > 0) {
			GdiSoft2D::Context s2;
			if (s2.Create(fb.w, fb.h, false)) {
				memcpy(s2.fb.color, fb.color, (size_t)fb.w * (size_t)fb.h * 4);
				if (postSaturate) s2.Saturate(postSatAmount);
				if (postGlow) s2.GlowBloom(2, 0.28f);
				if (postVignette) s2.Vignette(postVignetteStr);
				memcpy(fb.color, s2.fb.color, (size_t)fb.w * (size_t)fb.h * 4);
			}
		}
	}

	void Context::SetViewportFit(const float boxes[][6], int boxCount)
	{
		BuildView(fb.w, fb.h, cam, boxes, boxCount, view);
	}

	void Context::SetFog(FogMode m, COLORREF col, float start, float end, float density)
	{
		fogMode = m; fogColor = col; fogStart = start; fogEnd = end; fogDensity = density;
	}

	void Context::SetDof(bool on, float zNear, float zFar, int maxRadius)
	{
		dofEnable = on; dofNear = zNear; dofFar = zFar; dofMaxRadius = maxRadius;
	}

	DWORD Context::ApplyFog(DWORD c, float depth) const
	{
		if (fogMode == FogNone) return c;
		float f = 0.f;
		if (fogMode == FogLinear) {
			float span = fogEnd - fogStart;
			if (span < 1e-4f) span = 1e-4f;
			f = (depth - fogStart) / span;
		} else {
			f = 1.f - expf(-fogDensity * (std::max)(0.f, depth - fogStart));
		}
		if (f < 0.f) f = 0.f; if (f > 1.f) f = 1.f;
		const int fr = GetRValue(fogColor), fg = GetGValue(fogColor), fb_ = GetBValue(fogColor);
		const int r = (int)(GdiSoftFB::R(c) + (fr - GdiSoftFB::R(c)) * f + 0.5f);
		const int g = (int)(GdiSoftFB::G(c) + (fg - GdiSoftFB::G(c)) * f + 0.5f);
		const int b = (int)(GdiSoftFB::B(c) + (fb_ - GdiSoftFB::B(c)) * f + 0.5f);
		return GdiSoftFB::PackBGRA(GdiSoftFB::A(c), (BYTE)r, (BYTE)g, (BYTE)b);
	}

	void Context::RasterTri(const Vertex& a, const Vertex& b, const Vertex& c)
	{
		if (!fb.color || fb.w <= 0) return;
		float ax, ay, ad, bx, by, bd, cx, cy, cd;
		ProjectEx(view, a.x, a.y, a.z, ax, ay, ad);
		ProjectEx(view, b.x, b.y, b.z, bx, by, bd);
		ProjectEx(view, c.x, c.y, c.z, cx, cy, cd);

		if (cullBack) {
			const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
			if (area <= 0.f) return;
		}

		if (fillMode == FillWire) {
			DrawLine(a.x, a.y, a.z, b.x, b.y, b.z, RGB(GdiSoftFB::R(a.color), GdiSoftFB::G(a.color), GdiSoftFB::B(a.color)));
			DrawLine(b.x, b.y, b.z, c.x, c.y, c.z, RGB(GdiSoftFB::R(b.color), GdiSoftFB::G(b.color), GdiSoftFB::B(b.color)));
			DrawLine(c.x, c.y, c.z, a.x, a.y, a.z, RGB(GdiSoftFB::R(c.color), GdiSoftFB::G(c.color), GdiSoftFB::B(c.color)));
			return;
		}

		const int minX = (std::max)(0, (int)floorf((std::min)({ ax, bx, cx })));
		const int maxX = (std::min)(fb.w - 1, (int)ceilf((std::max)({ ax, bx, cx })));
		const int minY = (std::max)(0, (int)floorf((std::min)({ ay, by, cy })));
		const int maxY = (std::min)(fb.h - 1, (int)ceilf((std::max)({ ay, by, cy })));
		if (minX > maxX || minY > maxY) return;

		const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
		if (fabsf(area) < 1e-6f) return;
		const float invA = 1.f / area;

		for (int y = minY; y <= maxY; ++y) {
			DWORD* row = fb.Row(y);
			float* zrow = fb.ZRow(y);
			for (int x = minX; x <= maxX; ++x) {
				const float px = (float)x + 0.5f, py = (float)y + 0.5f;
				float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * invA;
				float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * invA;
				float w2 = 1.f - w0 - w1;
				if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
				const float depth = w0 * ad + w1 * bd + w2 * cd;
				if (depthTest && zrow && !(depth < zrow[x])) continue;
				if (depthWrite && zrow) zrow[x] = depth;

				DWORD col;
				if (texture && texture->w > 0) {
					const float u = w0 * a.u + w1 * b.u + w2 * c.u;
					const float v = w0 * a.v + w1 * b.v + w2 * c.v;
					col = texture->Sample(u, v);
				} else {
					const int ar = GdiSoftFB::R(a.color), ag = GdiSoftFB::G(a.color), ab = GdiSoftFB::B(a.color), aa = GdiSoftFB::A(a.color);
					const int br = GdiSoftFB::R(b.color), bg = GdiSoftFB::G(b.color), bb = GdiSoftFB::B(b.color), ba = GdiSoftFB::A(b.color);
					const int cr = GdiSoftFB::R(c.color), cg = GdiSoftFB::G(c.color), cb = GdiSoftFB::B(c.color), ca = GdiSoftFB::A(c.color);
					col = GdiSoftFB::PackBGRA(
						(BYTE)(aa * w0 + ba * w1 + ca * w2 + 0.5f),
						(BYTE)(ar * w0 + br * w1 + cr * w2 + 0.5f),
						(BYTE)(ag * w0 + bg * w1 + cg * w2 + 0.5f),
						(BYTE)(ab * w0 + bb * w1 + cb * w2 + 0.5f));
				}
				col = ApplyFog(col, depth);
				if (alphaBlend && GdiSoftFB::A(col) < 255)
					row[x] = GdiSoftFB::BlendSrcOver(row[x], col);
				else
					row[x] = col;
			}
		}
	}

	void Context::DrawLine(float x0, float y0, float z0, float x1, float y1, float z1, COLORREF col)
	{
		float sx0, sy0, d0, sx1, sy1, d1;
		ProjectEx(view, x0, y0, z0, sx0, sy0, d0);
		ProjectEx(view, x1, y1, z1, sx1, sy1, d1);
		int ix0 = (int)floorf(sx0 + 0.5f), iy0 = (int)floorf(sy0 + 0.5f);
		int ix1 = (int)floorf(sx1 + 0.5f), iy1 = (int)floorf(sy1 + 0.5f);
		int dx = abs(ix1 - ix0), sx = ix0 < ix1 ? 1 : -1;
		int dy = -abs(iy1 - iy0), sy = iy0 < iy1 ? 1 : -1;
		int err = dx + dy;
		const int steps = (std::max)(dx, -dy) + 1;
		int step = 0;
		// ワイヤーは深度書き込みのみ（テストは弱め）で点線化を防ぐ
		const bool oldTest = depthTest;
		depthTest = false;
		for (;;) {
			const float t = (steps <= 1) ? 0.f : (float)step / (float)(steps - 1);
			const float depth = d0 + (d1 - d0) * t;
			DWORD p = ApplyFog(GdiSoftFB::PackColorref(col, 255), depth);
			if (fb.DepthTestWrite(ix0, iy0, depth - 0.002f, false, depthWrite)) {
				fb.Put(ix0, iy0, p);
				// 1px 太めにして欠落を目立たなくする
				if (ix0 + 1 < fb.w) fb.Put(ix0 + 1, iy0, p);
				if (iy0 + 1 < fb.h) fb.Put(ix0, iy0 + 1, p);
			}
			if (ix0 == ix1 && iy0 == iy1) break;
			const int e2 = 2 * err;
			if (e2 >= dy) { err += dy; ix0 += sx; }
			if (e2 <= dx) { err += dx; iy0 += sy; }
			++step;
		}
		depthTest = oldTest;
	}

	void Context::DrawTriangles(const Vertex* verts, int count)
	{
		if (!verts || count < 3) return;
		for (int i = 0; i + 2 < count; i += 3)
			RasterTri(verts[i], verts[i + 1], verts[i + 2]);
	}

	void Context::DrawIndexed(const Vertex* verts, int /*vcount*/, const int* idx, int icount)
	{
		if (!verts || !idx || icount < 3) return;
		for (int i = 0; i + 2 < icount; i += 3)
			RasterTri(verts[idx[i]], verts[idx[i + 1]], verts[idx[i + 2]]);
	}

	static Vertex Vtx(float x, float y, float z, DWORD col, float u = 0, float v = 0)
	{
		Vertex o; o.x = x; o.y = y; o.z = z; o.u = u; o.v = v; o.color = col; return o;
	}

	void Context::DrawQuad(float x0, float y0, float z0, float x1, float y1, float z1,
		float x2, float y2, float z2, float x3, float y3, float z3, COLORREF col)
	{
		const DWORD c = GdiSoftFB::PackColorref(col, 255);
		Vertex t[6] = {
			Vtx(x0, y0, z0, c), Vtx(x1, y1, z1, c), Vtx(x2, y2, z2, c),
			Vtx(x0, y0, z0, c), Vtx(x2, y2, z2, c), Vtx(x3, y3, z3, c)
		};
		DrawTriangles(t, 6);
	}

	void Context::DrawTexturedQuad(float x0, float y0, float z0, float x1, float y1, float z1,
		float x2, float y2, float z2, float x3, float y3, float z3)
	{
		const DWORD c = 0xFFFFFFFFu;
		Vertex t[6] = {
			Vtx(x0, y0, z0, c, 0, 0), Vtx(x1, y1, z1, c, 1, 0), Vtx(x2, y2, z2, c, 1, 1),
			Vtx(x0, y0, z0, c, 0, 0), Vtx(x2, y2, z2, c, 1, 1), Vtx(x3, y3, z3, c, 0, 1)
		};
		DrawTriangles(t, 6);
	}

	void Context::DrawBox(float xL, float xR, float yTop, float z0, float z1, COLORREF col, float yBase)
	{
		if (xR < xL) { float t = xL; xL = xR; xR = t; }
		if (z1 < z0) { float t = z0; z0 = z1; z1 = t; }
		if (yTop < yBase + 0.004f) yTop = yBase + 0.004f;
		const COLORREF top = col;
		const COLORREF front = Shade(col, 0.72f);
		const COLORREF side = Shade(col, 0.55f);
		DrawQuad(xL, yTop, z0, xR, yTop, z0, xR, yTop, z1, xL, yTop, z1, top);
		DrawQuad(xL, yBase, z0, xR, yBase, z0, xR, yTop, z0, xL, yTop, z0, front);
		DrawQuad(xR, yBase, z0, xR, yBase, z1, xR, yTop, z1, xR, yTop, z0, side);
	}

	void Context::DrawWireBox(float xL, float xR, float yTop, float z0, float z1, COLORREF col, float yBase)
	{
		if (xR < xL) { float t = xL; xL = xR; xR = t; }
		if (z1 < z0) { float t = z0; z0 = z1; z1 = t; }
		if (yTop < yBase + 0.004f) yTop = yBase + 0.004f;
		// 直方体の12稜を実線で（三角ワイヤー化しない）
		DrawLine(xL, yBase, z0, xR, yBase, z0, col);
		DrawLine(xR, yBase, z0, xR, yBase, z1, col);
		DrawLine(xR, yBase, z1, xL, yBase, z1, col);
		DrawLine(xL, yBase, z1, xL, yBase, z0, col);
		DrawLine(xL, yTop, z0, xR, yTop, z0, col);
		DrawLine(xR, yTop, z0, xR, yTop, z1, col);
		DrawLine(xR, yTop, z1, xL, yTop, z1, col);
		DrawLine(xL, yTop, z1, xL, yTop, z0, col);
		DrawLine(xL, yBase, z0, xL, yTop, z0, col);
		DrawLine(xR, yBase, z0, xR, yTop, z0, col);
		DrawLine(xR, yBase, z1, xR, yTop, z1, col);
		DrawLine(xL, yBase, z1, xL, yTop, z1, col);
	}

	void Context::DrawSphere(float cx, float cy, float cz, float radius, COLORREF col, int slices, int stacks)
	{
		if (slices < 4) slices = 4;
		if (stacks < 2) stacks = 2;
		const DWORD c = GdiSoftFB::PackColorref(col, 255);
		for (int i = 0; i < stacks; ++i) {
			const float v0 = (float)i / (float)stacks;
			const float v1 = (float)(i + 1) / (float)stacks;
			const float phi0 = (float)(M_PI * (v0 - 0.5));
			const float phi1 = (float)(M_PI * (v1 - 0.5));
			const float y0 = cy + radius * sinf(phi0);
			const float y1 = cy + radius * sinf(phi1);
			const float r0 = radius * cosf(phi0);
			const float r1 = radius * cosf(phi1);
			for (int j = 0; j < slices; ++j) {
				const float u0 = (float)j / (float)slices;
				const float u1 = (float)(j + 1) / (float)slices;
				const float th0 = (float)(2.0 * M_PI * u0);
				const float th1 = (float)(2.0 * M_PI * u1);
				Vertex t[6] = {
					Vtx(cx + r0 * cosf(th0), y0, cz + r0 * sinf(th0), c, u0, v0),
					Vtx(cx + r0 * cosf(th1), y0, cz + r0 * sinf(th1), c, u1, v0),
					Vtx(cx + r1 * cosf(th1), y1, cz + r1 * sinf(th1), c, u1, v1),
					Vtx(cx + r0 * cosf(th0), y0, cz + r0 * sinf(th0), c, u0, v0),
					Vtx(cx + r1 * cosf(th1), y1, cz + r1 * sinf(th1), c, u1, v1),
					Vtx(cx + r1 * cosf(th0), y1, cz + r1 * sinf(th0), c, u0, v1)
				};
				DrawTriangles(t, 6);
			}
		}
	}

	void Context::DrawGrid(float x0, float x1, float z0, float z1, float y, int div, COLORREF col)
	{
		if (div < 1) div = 1;
		for (int i = 0; i <= div; ++i) {
			const float t = (float)i / (float)div;
			const float x = x0 + (x1 - x0) * t;
			const float z = z0 + (z1 - z0) * t;
			DrawLine(x, y, z0, x, y, z1, col);
			DrawLine(x0, y, z, x1, y, z, col);
		}
	}

	void Context::DrawStereoBars(float xMin, float xMax, int bins,
		const float* levL, const float* levR,
		float zL0, float zL1, float zR0, float zR1,
		float maxY, float gapFrac, COLORREF colL, COLORREF colR)
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
				DrawBox(xl, xr, tL * maxY, zL0, zL1, colL, 0.f);
			if (levR) {
				float tR = levR[i];
				if (tR < 0.f) tR = 0.f; if (tR > 1.f) tR = 1.f;
				if (tR >= 0.025f)
					DrawBox(xl, xr, tR * maxY, zR0, zR1, colR, 0.f);
			}
		}
	}

	void Context::DrawStereoBarsLR(float xMin, float xMax, int bins,
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
				DrawBox(xl, xr, tL * maxY, z0, z1, colL, 0.f);
			if (levR) {
				float tR = levR[i];
				if (tR < 0.f) tR = 0.f; if (tR > 1.f) tR = 1.f;
				const float aR = mid + gap * 0.5f + stepR * (float)i;
				const float bR = aR + stepR;
				const float xrl = aR + stepR * gapFrac * 0.5f;
				const float xrr = bR - stepR * gapFrac * 0.5f;
				if (xrr > xrl && tR >= 0.02f)
					DrawBox(xrl, xrr, tR * maxY, z0, z1, colR, 0.f);
			}
		}
	}

	void Context::DrawWaveRibbon(float x0, float x1, float z, float yMid, float yAmp,
		const float* samples, int n, COLORREF col, float thickness)
	{
		if (!samples || n < 2) return;
		for (int i = 0; i < n - 1; ++i) {
			const float u0 = (float)i / (float)(n - 1);
			const float u1 = (float)(i + 1) / (float)(n - 1);
			const float xa = x0 + (x1 - x0) * u0;
			const float xb = x0 + (x1 - x0) * u1;
			float s0 = samples[i], s1 = samples[i + 1];
			if (s0 < -1.f) s0 = -1.f; if (s0 > 1.f) s0 = 1.f;
			if (s1 < -1.f) s1 = -1.f; if (s1 > 1.f) s1 = 1.f;
			const float ya = yMid + s0 * yAmp;
			const float yb = yMid + s1 * yAmp;
			const float yLo = (std::min)(ya, yb) - thickness * 0.5f;
			const float yHi = (std::max)(ya, yb) + thickness * 0.5f;
			DrawBox(xa, xb, yHi, z, z + thickness, col, yLo);
		}
	}

	void Context::DrawMirrorFloor(float xL, float xR, float z0, float z1, COLORREF col, float alpha)
	{
		if (alpha < 0.f) alpha = 0.f;
		if (alpha > 1.f) alpha = 1.f;
		const BYTE a = (BYTE)(alpha * 200.f + 0.5f);
		const bool oldBlend = alphaBlend;
		alphaBlend = true;
		const DWORD c = GdiSoftFB::PackColorref(col, a);
		Vertex t[6] = {
			Vtx(xL, 0.002f, z0, c), Vtx(xR, 0.002f, z0, c), Vtx(xR, 0.002f, z1, c),
			Vtx(xL, 0.002f, z0, c), Vtx(xR, 0.002f, z1, c), Vtx(xL, 0.002f, z1, c)
		};
		DrawTriangles(t, 6);
		alphaBlend = oldBlend;
	}

	void Context::DrawNeonBox(float xL, float xR, float yTop, float z0, float z1, COLORREF col, float yBase)
	{
		DrawBox(xL, xR, yTop, z0, z1, col, yBase);
		const COLORREF glow = Shade(col, 1.35f);
		DrawWireBox(xL - 0.004f, xR + 0.004f, yTop + 0.004f, z0 - 0.004f, z1 + 0.004f, glow, yBase);
	}

	void Context::DrawTorus(float cx, float cy, float cz, float R, float r, COLORREF col, int slices, int stacks)
	{
		if (slices < 6) slices = 6;
		if (stacks < 6) stacks = 6;
		const DWORD c = GdiSoftFB::PackColorref(col, 255);
		for (int i = 0; i < slices; ++i) {
			const float u0 = (float)(2.0 * M_PI * i / slices);
			const float u1 = (float)(2.0 * M_PI * (i + 1) / slices);
			for (int j = 0; j < stacks; ++j) {
				const float v0 = (float)(2.0 * M_PI * j / stacks);
				const float v1 = (float)(2.0 * M_PI * (j + 1) / stacks);
				auto pt = [&](float u, float vv, Vertex& o) {
					const float x = (R + r * cosf(vv)) * cosf(u);
					const float y = r * sinf(vv);
					const float z = (R + r * cosf(vv)) * sinf(u);
					o = Vtx(cx + x, cy + y, cz + z, c);
				};
				Vertex a, b, c0, d;
				pt(u0, v0, a); pt(u1, v0, b); pt(u1, v1, c0); pt(u0, v1, d);
				Vertex tri[6] = { a, b, c0, a, c0, d };
				DrawTriangles(tri, 6);
			}
		}
	}

	void Context::PostFog()
	{
		// Fog already applied per-pixel in RasterTri; this is a no-op placeholder
		// for post-only fog modes if depth buffer should recolor unfogged paths.
		(void)0;
	}

	void Context::PostEdge()
	{
		if (!edgeOverlay || !fb.color || !fb.z) return;
		std::vector<DWORD> out((size_t)fb.w * (size_t)fb.h);
		memcpy(out.data(), fb.color, out.size() * 4);
		const DWORD ec = GdiSoftFB::PackColorref(edgeColor, 255);
		for (int y = 1; y < fb.h - 1; ++y) {
			float* zrow = fb.ZRow(y);
			float* zup = fb.ZRow(y - 1);
			float* zdn = fb.ZRow(y + 1);
			for (int x = 1; x < fb.w - 1; ++x) {
				const float z = zrow[x];
				if (z > 1e8f) continue;
				const float gx = fabsf(zrow[x + 1] - zrow[x - 1]);
				const float gy = fabsf(zdn[x] - zup[x]);
				if (gx + gy > 0.045f)
					out[(size_t)y * fb.w + x] = ec;
			}
		}
		memcpy(fb.color, out.data(), out.size() * 4);
	}

	void Context::PostDof()
	{
		if (!dofEnable || !fb.color || !fb.z || dofMaxRadius < 1) return;
		// Build 3 blur mips via Soft2D box blur, then lerp by depth
		GdiSoft2D::Context soft;
		if (!soft.Create(fb.w, fb.h, false)) return;
		memcpy(soft.fb.color, fb.color, (size_t)fb.w * (size_t)fb.h * 4);
		soft.BlurRect(0, 0, fb.w, fb.h, (std::max)(1, dofMaxRadius / 2));
		std::vector<DWORD> blur1((size_t)fb.w * (size_t)fb.h);
		memcpy(blur1.data(), soft.fb.color, blur1.size() * 4);
		soft.BlurRect(0, 0, fb.w, fb.h, (std::max)(1, dofMaxRadius));
		std::vector<DWORD> blur2((size_t)fb.w * (size_t)fb.h);
		memcpy(blur2.data(), soft.fb.color, blur2.size() * 4);

		const float span = (std::max)(1e-4f, dofFar - dofNear);
		for (int y = 0; y < fb.h; ++y) {
			DWORD* row = fb.Row(y);
			float* zrow = fb.ZRow(y);
			for (int x = 0; x < fb.w; ++x) {
				float t = (zrow[x] - dofNear) / span;
				if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
				DWORD sharp = row[x];
				DWORD b1 = blur1[(size_t)y * fb.w + x];
				DWORD b2 = blur2[(size_t)y * fb.w + x];
				DWORD mid = (t < 0.5f) ? sharp : b1;
				DWORD farp = (t < 0.5f) ? b1 : b2;
				float u = (t < 0.5f) ? (t * 2.f) : ((t - 0.5f) * 2.f);
				auto lerpC = [](DWORD a, DWORD b, float u) {
					return GdiSoftFB::PackBGRA(
						(BYTE)(GdiSoftFB::A(a) + (GdiSoftFB::A(b) - GdiSoftFB::A(a)) * u + 0.5f),
						(BYTE)(GdiSoftFB::R(a) + (GdiSoftFB::R(b) - GdiSoftFB::R(a)) * u + 0.5f),
						(BYTE)(GdiSoftFB::G(a) + (GdiSoftFB::G(b) - GdiSoftFB::G(a)) * u + 0.5f),
						(BYTE)(GdiSoftFB::B(a) + (GdiSoftFB::B(b) - GdiSoftFB::B(a)) * u + 0.5f));
				};
				row[x] = lerpC(mid, farp, u);
			}
		}
	}

	// ---- Scene Flush via FB raster ----
	static int FaceDepthCmp(const void* a, const void* b)
	{
		const Face* fa = (const Face*)a;
		const Face* fb = (const Face*)b;
		if (fa->depth < fb->depth) return 1;
		if (fa->depth > fb->depth) return -1;
		return 0;
	}

	void Scene::Flush(CDC& dc)
	{
		if (n <= 0 || !view) return;

		int ww = 0, hh = 0;
		if (CBitmap* pb = dc.GetCurrentBitmap()) {
			BITMAP bm = {};
			if (pb->GetObject(sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
				ww = bm.bmWidth; hh = abs(bm.bmHeight);
			}
		}
		if (ww <= 0 || hh <= 0) {
			CRect rc; dc.GetClipBox(&rc);
			ww = rc.Width(); hh = rc.Height();
		}
		if (ww < 8 || hh < 8) {
			// fallback GDI painter
			qsort(faces, (size_t)n, sizeof(Face), FaceDepthCmp);
			HGDIOBJ oldBrush = ::SelectObject(dc.GetSafeHdc(), ::GetStockObject(DC_BRUSH));
			HGDIOBJ oldPen = ::SelectObject(dc.GetSafeHdc(), ::GetStockObject(NULL_PEN));
			for (int i = 0; i < n; ++i) {
				::SetDCBrushColor(dc.GetSafeHdc(), faces[i].fill);
				dc.Polygon(faces[i].p, 4);
			}
			::SelectObject(dc.GetSafeHdc(), oldBrush);
			::SelectObject(dc.GetSafeHdc(), oldPen);
			n = 0;
			return;
		}

		// 毎フレーム Context をスタック生成→破棄すると DIB の Create/Delete が連発し GDI 枯渇する
		static Context s_flushCtx;
		if (!s_flushCtx.Create(ww, hh)) { n = 0; return; }
		Context& ctx = s_flushCtx;
		ctx.view = *view;
		ctx.depthTest = true;
		ctx.depthWrite = true;
		ctx.BeginFrame(RGB(0, 0, 0));
		// Keep clear transparent-ish by reading existing DC content first
		if (ctx.fb.hdc) {
			::BitBlt(ctx.fb.hdc, 0, 0, ww, hh, dc.GetSafeHdc(), 0, 0, SRCCOPY);
			// reset Z after blit
			if (ctx.fb.z) {
				const size_t nn = (size_t)ww * (size_t)hh;
				for (size_t i = 0; i < nn; ++i) ctx.fb.z[i] = 1e9f;
			}
		}

		for (int i = 0; i < n; ++i) {
			const Face& f = faces[i];
			const DWORD c = GdiSoftFB::PackColorref(f.fill, 255);
			// Direct screen-space fill with Z
			auto rasterScreen = [&](POINT p0, POINT p1, POINT p2, float d0, float d1, float d2) {
				const float ax = (float)p0.x, ay = (float)p0.y;
				const float bx = (float)p1.x, by = (float)p1.y;
				const float cx = (float)p2.x, cy = (float)p2.y;
				const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
				if (fabsf(area) < 1e-6f) return;
				const float invA = 1.f / area;
				const int minX = (std::max)(0, (int)floorf((std::min)({ ax, bx, cx })));
				const int maxX = (std::min)(ww - 1, (int)ceilf((std::max)({ ax, bx, cx })));
				const int minY = (std::max)(0, (int)floorf((std::min)({ ay, by, cy })));
				const int maxY = (std::min)(hh - 1, (int)ceilf((std::max)({ ay, by, cy })));
				for (int y = minY; y <= maxY; ++y) {
					DWORD* row = ctx.fb.Row(y);
					float* zrow = ctx.fb.ZRow(y);
					for (int x = minX; x <= maxX; ++x) {
						const float px = (float)x + 0.5f, py = (float)y + 0.5f;
						float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * invA;
						float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * invA;
						float w2 = 1.f - w0 - w1;
						if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
						const float depth = w0 * d0 + w1 * d1 + w2 * d2;
						if (zrow && !(depth < zrow[x])) continue;
						if (zrow) zrow[x] = depth;
						row[x] = c;
					}
				}
			};
			rasterScreen(f.p[0], f.p[1], f.p[2], f.wz[0], f.wz[1], f.wz[2]);
			rasterScreen(f.p[0], f.p[2], f.p[3], f.wz[0], f.wz[2], f.wz[3]);
		}

		ctx.Present(dc, 0, 0);
		n = 0;
	}

	BOOL BlitTextBoard(CDC& dst, const View& v,
		float xL, float xR, float y, float z0, float z1,
		CDC& src, int srcW, int srcH)
	{
		return BlitBitmapQuad(dst, v, xL, xR, y, z0, z1, src.GetSafeHdc(), srcW, srcH);
	}

	void PresentApiDemo(CDC& dc, const CRect& rc)
	{
		const int w = rc.Width(), h = rc.Height();
		if (w < 16 || h < 16) return;

		Context ctx;
		if (!ctx.Create(w, h)) return;
		ctx.cam.yawDeg = -28.f;
		ctx.cam.pitchDeg = 26.f;
		ctx.cam.zoom = 1.05f;
		const float boxes[1][6] = { { -1.4f, 1.4f, -0.05f, 1.1f, -0.2f, 1.4f } };
		ctx.SetViewportFit(boxes, 1);
		ctx.SetFog(FogLinear, RGB(8, 10, 18), -0.05f, 0.95f, 1.2f);
		ctx.SetEdgeOverlay(true, RGB(10, 12, 18));
		ctx.SetDof(true, -0.15f, 0.85f, 5);
		ctx.postVignette = true;
		ctx.postVignetteStr = 0.5f;
		ctx.postGlow = true;
		ctx.postSaturate = true;
		ctx.postSatAmount = 1.25f;
		ctx.BeginFrame(RGB(14, 16, 24));

		ctx.DrawGrid(-1.2f, 1.2f, 0.0f, 1.25f, 0.0f, 8, RGB(50, 56, 72));
		ctx.DrawMirrorFloor(-1.15f, 1.15f, 0.05f, 1.20f, RGB(70, 110, 180), 0.32f);
		ctx.DrawNeonBox(-1.15f, -0.55f, 0.55f, 0.10f, 0.45f, RGB(80, 170, 255), 0.f);
		ctx.DrawWireBox(0.55f, 1.15f, 0.50f, 0.15f, 0.55f, RGB(255, 200, 90), 0.f);
		ctx.DrawSphere(0.0f, 0.35f, 0.40f, 0.22f, RGB(120, 230, 160), 16, 12);
		ctx.DrawTorus(0.85f, 0.28f, 0.95f, 0.18f, 0.055f, RGB(255, 120, 180), 18, 10);
		// 奥の箱で fog/DOF を明示
		ctx.DrawBox(-0.25f, 0.25f, 0.22f, 1.05f, 1.25f, RGB(180, 90, 220), 0.f);

		// Soft2D overlay strip + texture board
		{
			GdiSoft2D::Context s2;
			s2.Create(96, 96, false);
			s2.GradientFillRectH(0, 0, 96, 96, RGB(40, 80, 180), RGB(220, 80, 140), 255);
			s2.FillEllipse(48, 48, 28, 28, RGB(255, 255, 255), 180);
			s2.DrawRect(4, 4, 88, 88, RGB(255, 255, 255), 255, 2);
			Texture tex;
			tex.LoadFromHdc(s2.fb.hdc ? s2.fb.hdc : nullptr, 96, 96);
			if (tex.w > 0) {
				ctx.SetTexture(&tex);
				ctx.DrawTexturedQuad(-0.35f, 0.70f, 0.20f, 0.35f, 0.70f, 0.20f,
					0.35f, 0.10f, 0.20f, -0.35f, 0.10f, 0.20f);
				ctx.SetTexture(nullptr);
			}
		}

		float levL[16], levR[16], wave[48];
		for (int i = 0; i < 16; ++i) {
			levL[i] = 0.2f + 0.6f * fabsf(sinf(i * 0.7f));
			levR[i] = 0.15f + 0.55f * fabsf(cosf(i * 0.55f));
		}
		for (int i = 0; i < 48; ++i)
			wave[i] = sinf(i * 0.35f) * 0.85f;
		// 左右並びステレオバー + 波形リボン
		ctx.DrawStereoBarsLR(-0.95f, 0.95f, 16, levL, levR, 0.58f, 0.78f,
			0.38f, 0.22f, RGB(80, 200, 255), RGB(255, 140, 180));
		ctx.DrawWaveRibbon(-0.95f, 0.95f, 0.22f, 0.55f, 0.12f, wave, 48, RGB(180, 255, 120), 0.015f);

		ctx.EndFrame();
		ctx.Present(dc, rc.left, rc.top);

		// Soft2D blur/alpha strip + CRT タッチ
		GdiSoft2D::Context bar;
		bar.Create(w, 28, false);
		bar.GradientFillRectH(0, 0, w, 28, RGB(30, 40, 70), RGB(90, 40, 80), 200);
		bar.BlurRect(0, 0, w, 28, 2);
		bar.Scanlines(0.18f, 2);
		bar.PresentAlpha(dc.GetSafeHdc(), rc.left, rc.bottom - 28, 220);

		GdiSoft2D::Context post;
		if (post.Create(w, h, false) && post.fb.hdc) {
			::BitBlt(post.fb.hdc, 0, 0, w, h, dc.GetSafeHdc(), rc.left, rc.top, SRCCOPY);
			post.ChromaticAberration(1);
			post.Scanlines(0.10f, 3);
			post.Present(dc, rc.left, rc.top);
		}
	}
}
