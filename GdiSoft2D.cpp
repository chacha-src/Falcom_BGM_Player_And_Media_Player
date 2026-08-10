#include "stdafx.h"
#include "GdiSoft2D.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GdiSoft2D
{
	void Context::PushClipRect(int l, int t, int r, int b)
	{
		ClipRect c{ l, t, r, b };
		if (!clipStack.empty()) {
			const ClipRect& p = clipStack.back();
			c.l = (std::max)(c.l, p.l);
			c.t = (std::max)(c.t, p.t);
			c.r = (std::min)(c.r, p.r);
			c.b = (std::min)(c.b, p.b);
		} else {
			c.l = (std::max)(0, c.l);
			c.t = (std::max)(0, c.t);
			c.r = (std::min)(fb.w, c.r);
			c.b = (std::min)(fb.h, c.b);
		}
		clipStack.push_back(c);
	}

	void Context::PopClip()
	{
		if (!clipStack.empty()) clipStack.pop_back();
	}

	ClipRect Context::CurrentClip() const
	{
		if (!clipStack.empty()) return clipStack.back();
		return ClipRect{ 0, 0, fb.w, fb.h };
	}

	static inline void PutClip(GdiSoftFB::Framebuffer& fb, const ClipRect& clip, int x, int y, DWORD p, bool blend)
	{
		if (x < clip.l || y < clip.t || x >= clip.r || y >= clip.b) return;
		if (blend) fb.PutBlend(x, y, p);
		else fb.Put(x, y, p);
	}

	void Context::FillRect(int x, int y, int w, int h, COLORREF c, BYTE a)
	{
		if (w <= 0 || h <= 0 || !fb.color) return;
		const ClipRect clip = CurrentClip();
		const DWORD p = GdiSoftFB::PackColorref(c, a);
		const int x0 = (std::max)(x, clip.l);
		const int y0 = (std::max)(y, clip.t);
		const int x1 = (std::min)(x + w, clip.r);
		const int y1 = (std::min)(y + h, clip.b);
		const bool blend = a < 255;
		for (int yy = y0; yy < y1; ++yy) {
			DWORD* row = fb.Row(yy);
			for (int xx = x0; xx < x1; ++xx) {
				if (blend) row[xx] = GdiSoftFB::BlendSrcOver(row[xx], p);
				else row[xx] = p;
			}
		}
	}

	void Context::FillEllipse(int cx, int cy, int rx, int ry, COLORREF c, BYTE a)
	{
		if (rx <= 0 || ry <= 0 || !fb.color) return;
		const ClipRect clip = CurrentClip();
		const DWORD p = GdiSoftFB::PackColorref(c, a);
		const bool blend = a < 255;
		const int y0 = (std::max)(cy - ry, clip.t);
		const int y1 = (std::min)(cy + ry + 1, clip.b);
		const float rx2 = (float)rx * (float)rx;
		const float ry2 = (float)ry * (float)ry;
		for (int y = y0; y < y1; ++y) {
			const float dy = (float)(y - cy);
			const float t = 1.f - (dy * dy) / ry2;
			if (t < 0.f) continue;
			const int half = (int)floorf(sqrtf(t * rx2) + 0.5f);
			const int x0 = (std::max)(cx - half, clip.l);
			const int x1 = (std::min)(cx + half + 1, clip.r);
			DWORD* row = fb.Row(y);
			for (int x = x0; x < x1; ++x) {
				if (blend) row[x] = GdiSoftFB::BlendSrcOver(row[x], p);
				else row[x] = p;
			}
		}
	}

	void Context::DrawLine(int x0, int y0, int x1, int y1, COLORREF c, BYTE a, int thickness)
	{
		if (!fb.color) return;
		if (thickness < 1) thickness = 1;
		const ClipRect clip = CurrentClip();
		const DWORD p = GdiSoftFB::PackColorref(c, a);
		const bool blend = a < 255;
		int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
		int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		const int r = thickness / 2;
		for (;;) {
			for (int oy = -r; oy <= r; ++oy)
				for (int ox = -r; ox <= r; ++ox)
					PutClip(fb, clip, x0 + ox, y0 + oy, p, blend);
			if (x0 == x1 && y0 == y1) break;
			const int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}

	void Context::DrawRect(int x, int y, int w, int h, COLORREF c, BYTE a, int thickness)
	{
		if (w <= 0 || h <= 0) return;
		DrawLine(x, y, x + w - 1, y, c, a, thickness);
		DrawLine(x, y + h - 1, x + w - 1, y + h - 1, c, a, thickness);
		DrawLine(x, y, x, y + h - 1, c, a, thickness);
		DrawLine(x + w - 1, y, x + w - 1, y + h - 1, c, a, thickness);
	}

	void Context::DrawEllipse(int cx, int cy, int rx, int ry, COLORREF c, BYTE a, int thickness)
	{
		if (rx <= 0 || ry <= 0) return;
		const int steps = (std::max)(32, (rx + ry) * 2);
		int px = cx + rx, py = cy;
		for (int i = 1; i <= steps; ++i) {
			const float ang = (float)(2.0 * M_PI * (double)i / (double)steps);
			const int x = cx + (int)floorf(cosf(ang) * (float)rx + 0.5f);
			const int y = cy + (int)floorf(sinf(ang) * (float)ry + 0.5f);
			DrawLine(px, py, x, y, c, a, thickness);
			px = x; py = y;
		}
	}

	static bool GrabSrc(HDC src, int sx, int sy, int sw, int sh, std::vector<DWORD>& out)
	{
		if (!src || sw <= 0 || sh <= 0) return false;
		out.assign((size_t)sw * (size_t)sh, 0);
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
		::BitBlt(tmp, 0, 0, sw, sh, src, sx, sy, SRCCOPY);
		memcpy(out.data(), bits, (size_t)sw * (size_t)sh * 4);
		// force opaque if source had 0 alpha
		for (DWORD& p : out) {
			if (GdiSoftFB::A(p) == 0)
				p = GdiSoftFB::PackBGRA(255, GdiSoftFB::R(p), GdiSoftFB::G(p), GdiSoftFB::B(p));
		}
		::SelectObject(tmp, old);
		::DeleteObject(dib);
		::DeleteDC(tmp);
		return true;
	}

	void Context::DrawBitmap(HDC src, int sx, int sy, int sw, int sh,
		int dx, int dy, int dw, int dh)
	{
		DrawBitmapAlpha(src, sx, sy, sw, sh, dx, dy, dw, dh, 255);
	}

	void Context::DrawBitmapAlpha(HDC src, int sx, int sy, int sw, int sh,
		int dx, int dy, int dw, int dh, BYTE constAlpha)
	{
		if (!fb.color || dw <= 0 || dh <= 0) return;
		std::vector<DWORD> srcPix;
		if (!GrabSrc(src, sx, sy, sw, sh, srcPix)) return;
		const ClipRect clip = CurrentClip();
		for (int y = 0; y < dh; ++y) {
			const int dyi = dy + y;
			if (dyi < clip.t || dyi >= clip.b) continue;
			const int syi = y * sh / dh;
			DWORD* dstRow = fb.Row(dyi);
			const DWORD* srcRow = srcPix.data() + (size_t)syi * (size_t)sw;
			for (int x = 0; x < dw; ++x) {
				const int dxi = dx + x;
				if (dxi < clip.l || dxi >= clip.r) continue;
				const int sxi = x * sw / dw;
				DWORD sp = srcRow[sxi];
				BYTE a = GdiSoftFB::A(sp);
				a = (BYTE)((a * constAlpha) / 255);
				sp = GdiSoftFB::PackBGRA(a, GdiSoftFB::R(sp), GdiSoftFB::G(sp), GdiSoftFB::B(sp));
				if (a >= 255) dstRow[dxi] = sp;
				else if (a > 0) dstRow[dxi] = GdiSoftFB::BlendSrcOver(dstRow[dxi], sp);
			}
		}
	}

	void Context::DrawBitmapAffine(HDC src, int sw, int sh, const POINT p[3], BYTE constAlpha)
	{
		if (!fb.color || !p || sw <= 0 || sh <= 0) return;
		std::vector<DWORD> srcPix;
		if (!GrabSrc(src, 0, 0, sw, sh, srcPix)) return;
		const ClipRect clip = CurrentClip();
		// Bounding box of parallelogram
		const int xA = p[0].x, yA = p[0].y;
		const int xB = p[1].x, yB = p[1].y;
		const int xC = p[2].x, yC = p[2].y;
		const int xD = xA + (xB - xA) + (xC - xA);
		const int yD = yA + (yB - yA) + (yC - yA);
		const int minX = (std::min)({ xA, xB, xC, xD });
		const int maxX = (std::max)({ xA, xB, xC, xD });
		const int minY = (std::min)({ yA, yB, yC, yD });
		const int maxY = (std::max)({ yA, yB, yC, yD });
		const float e1x = (float)(xB - xA), e1y = (float)(yB - yA);
		const float e2x = (float)(xC - xA), e2y = (float)(yC - yA);
		const float det = e1x * e2y - e1y * e2x;
		if (fabsf(det) < 1e-4f) return;
		const float invDet = 1.f / det;
		for (int y = (std::max)(minY, clip.t); y <= (std::min)(maxY, clip.b - 1); ++y) {
			DWORD* dstRow = fb.Row(y);
			for (int x = (std::max)(minX, clip.l); x <= (std::min)(maxX, clip.r - 1); ++x) {
				const float vx = (float)(x - xA), vy = (float)(y - yA);
				const float u = (vx * e2y - vy * e2x) * invDet;
				const float v = (vy * e1x - vx * e1y) * invDet;
				if (u < 0.f || v < 0.f || u > 1.f || v > 1.f) continue;
				int sx = (int)(u * (sw - 1));
				int sy = (int)(v * (sh - 1));
				if (sx < 0) sx = 0; if (sy < 0) sy = 0;
				if (sx >= sw) sx = sw - 1; if (sy >= sh) sy = sh - 1;
				DWORD sp = srcPix[(size_t)sy * (size_t)sw + (size_t)sx];
				BYTE a = (BYTE)((GdiSoftFB::A(sp) * constAlpha) / 255);
				sp = GdiSoftFB::PackBGRA(a, GdiSoftFB::R(sp), GdiSoftFB::G(sp), GdiSoftFB::B(sp));
				if (a >= 255) dstRow[x] = sp;
				else if (a > 0) dstRow[x] = GdiSoftFB::BlendSrcOver(dstRow[x], sp);
			}
		}
	}

	void Context::Fade(BYTE mulA)
	{
		if (!fb.color) return;
		const size_t n = (size_t)fb.w * (size_t)fb.h;
		for (size_t i = 0; i < n; ++i) {
			DWORD p = fb.color[i];
			BYTE a = (BYTE)((GdiSoftFB::A(p) * mulA) / 255);
			fb.color[i] = GdiSoftFB::PackBGRA(a, GdiSoftFB::R(p), GdiSoftFB::G(p), GdiSoftFB::B(p));
		}
	}

	void Context::BlurRect(int x, int y, int w, int h, int radius)
	{
		if (!fb.color || radius <= 0 || w <= 0 || h <= 0) return;
		const ClipRect clip = CurrentClip();
		const int x0 = (std::max)(x, clip.l);
		const int y0 = (std::max)(y, clip.t);
		const int x1 = (std::min)(x + w, clip.r);
		const int y1 = (std::min)(y + h, clip.b);
		if (x1 <= x0 || y1 <= y0) return;
		const int bw = x1 - x0, bh = y1 - y0;
		std::vector<DWORD> tmp((size_t)bw * (size_t)bh);
		std::vector<DWORD> src((size_t)bw * (size_t)bh);
		for (int yy = 0; yy < bh; ++yy)
			memcpy(src.data() + (size_t)yy * bw, fb.Row(y0 + yy) + x0, (size_t)bw * 4);

		auto blurPass = [&](const DWORD* in, DWORD* out, bool horiz) {
			for (int yy = 0; yy < bh; ++yy) {
				for (int xx = 0; xx < bw; ++xx) {
					int sumA = 0, sumR = 0, sumG = 0, sumB = 0, cnt = 0;
					for (int d = -radius; d <= radius; ++d) {
						int sx = xx, sy = yy;
						if (horiz) sx = xx + d; else sy = yy + d;
						if (sx < 0 || sy < 0 || sx >= bw || sy >= bh) continue;
						DWORD p = in[(size_t)sy * bw + sx];
						sumA += GdiSoftFB::A(p); sumR += GdiSoftFB::R(p);
						sumG += GdiSoftFB::G(p); sumB += GdiSoftFB::B(p);
						++cnt;
					}
					if (cnt < 1) cnt = 1;
					out[(size_t)yy * bw + xx] = GdiSoftFB::PackBGRA(
						(BYTE)(sumA / cnt), (BYTE)(sumR / cnt), (BYTE)(sumG / cnt), (BYTE)(sumB / cnt));
				}
			}
		};
		blurPass(src.data(), tmp.data(), true);
		blurPass(tmp.data(), src.data(), false);
		for (int yy = 0; yy < bh; ++yy)
			memcpy(fb.Row(y0 + yy) + x0, src.data() + (size_t)yy * bw, (size_t)bw * 4);
	}

	void Context::GradientFillRectH(int x, int y, int w, int h, COLORREF c0, COLORREF c1, BYTE a)
	{
		if (w <= 0 || h <= 0) return;
		for (int i = 0; i < w; ++i) {
			const float t = (w <= 1) ? 0.f : (float)i / (float)(w - 1);
			COLORREF c = RGB(
				(int)(GetRValue(c0) + (GetRValue(c1) - GetRValue(c0)) * t + 0.5f),
				(int)(GetGValue(c0) + (GetGValue(c1) - GetGValue(c0)) * t + 0.5f),
				(int)(GetBValue(c0) + (GetBValue(c1) - GetBValue(c0)) * t + 0.5f));
			FillRect(x + i, y, 1, h, c, a);
		}
	}

	void Context::GradientFillRectV(int x, int y, int w, int h, COLORREF c0, COLORREF c1, BYTE a)
	{
		if (w <= 0 || h <= 0) return;
		for (int i = 0; i < h; ++i) {
			const float t = (h <= 1) ? 0.f : (float)i / (float)(h - 1);
			COLORREF c = RGB(
				(int)(GetRValue(c0) + (GetRValue(c1) - GetRValue(c0)) * t + 0.5f),
				(int)(GetGValue(c0) + (GetGValue(c1) - GetGValue(c0)) * t + 0.5f),
				(int)(GetBValue(c0) + (GetBValue(c1) - GetBValue(c0)) * t + 0.5f));
			FillRect(x, y + i, w, 1, c, a);
		}
	}

	void Context::Vignette(float strength)
	{
		if (!fb.color || strength <= 0.f) return;
		if (strength > 1.f) strength = 1.f;
		const float cx = (fb.w - 1) * 0.5f, cy = (fb.h - 1) * 0.5f;
		const float maxR = sqrtf(cx * cx + cy * cy);
		for (int y = 0; y < fb.h; ++y) {
			DWORD* row = fb.Row(y);
			for (int x = 0; x < fb.w; ++x) {
				const float dx = (x - cx) / maxR, dy = (y - cy) / maxR;
				float d = sqrtf(dx * dx + dy * dy);
				float m = 1.f - strength * d * d;
				if (m < 0.15f) m = 0.15f;
				DWORD p = row[x];
				row[x] = GdiSoftFB::PackBGRA(GdiSoftFB::A(p),
					(BYTE)(GdiSoftFB::R(p) * m + 0.5f),
					(BYTE)(GdiSoftFB::G(p) * m + 0.5f),
					(BYTE)(GdiSoftFB::B(p) * m + 0.5f));
			}
		}
	}

	void Context::Posterize(int levels)
	{
		if (!fb.color || levels < 2) return;
		if (levels > 32) levels = 32;
		const int step = 255 / (levels - 1);
		auto q = [&](int v) -> BYTE {
			int n = (v + step / 2) / step * step;
			if (n < 0) n = 0; if (n > 255) n = 255;
			return (BYTE)n;
		};
		const size_t n = (size_t)fb.w * (size_t)fb.h;
		for (size_t i = 0; i < n; ++i) {
			DWORD p = fb.color[i];
			fb.color[i] = GdiSoftFB::PackBGRA(GdiSoftFB::A(p), q(GdiSoftFB::R(p)), q(GdiSoftFB::G(p)), q(GdiSoftFB::B(p)));
		}
	}

	void Context::Saturate(float amount)
	{
		if (!fb.color || fabsf(amount - 1.f) < 1e-3f) return;
		const size_t n = (size_t)fb.w * (size_t)fb.h;
		for (size_t i = 0; i < n; ++i) {
			DWORD p = fb.color[i];
			float r = GdiSoftFB::R(p), g = GdiSoftFB::G(p), b = GdiSoftFB::B(p);
			float gray = 0.299f * r + 0.587f * g + 0.114f * b;
			r = gray + (r - gray) * amount;
			g = gray + (g - gray) * amount;
			b = gray + (b - gray) * amount;
			auto clip = [](float v) -> BYTE {
				if (v < 0.f) v = 0.f; if (v > 255.f) v = 255.f;
				return (BYTE)(v + 0.5f);
			};
			fb.color[i] = GdiSoftFB::PackBGRA(GdiSoftFB::A(p), clip(r), clip(g), clip(b));
		}
	}

	void Context::GlowBloom(int radius, float amount)
	{
		if (!fb.color || radius < 1 || amount <= 0.f) return;
		std::vector<DWORD> src((size_t)fb.w * (size_t)fb.h);
		memcpy(src.data(), fb.color, src.size() * 4);
		BlurRect(0, 0, fb.w, fb.h, radius);
		for (size_t i = 0; i < src.size(); ++i) {
			DWORD a = src[i], b = fb.color[i];
			int br = GdiSoftFB::R(b), bg = GdiSoftFB::G(b), bb = GdiSoftFB::B(b);
			int lum = (br + bg + bb) / 3;
			if (lum < 40) { fb.color[i] = a; continue; }
			float t = amount * (lum / 255.f);
			auto mix = [&](int s, int g) -> BYTE {
				float v = s + (g - s) * t + (g > s ? (g - s) * t * 0.5f : 0.f);
				if (v < 0.f) v = 0.f; if (v > 255.f) v = 255.f;
				return (BYTE)(v + 0.5f);
			};
			fb.color[i] = GdiSoftFB::PackBGRA(GdiSoftFB::A(a),
				mix(GdiSoftFB::R(a), br), mix(GdiSoftFB::G(a), bg), mix(GdiSoftFB::B(a), bb));
		}
	}

	void Context::Emboss(float amount)
	{
		if (!fb.color || amount <= 0.f) return;
		std::vector<DWORD> src((size_t)fb.w * (size_t)fb.h);
		memcpy(src.data(), fb.color, src.size() * 4);
		for (int y = 1; y < fb.h - 1; ++y) {
			for (int x = 1; x < fb.w - 1; ++x) {
				DWORD a = src[(size_t)(y - 1) * fb.w + (x - 1)];
				DWORD b = src[(size_t)(y + 1) * fb.w + (x + 1)];
				int d = ((int)GdiSoftFB::R(b) + GdiSoftFB::G(b) + GdiSoftFB::B(b)
					- (int)GdiSoftFB::R(a) - GdiSoftFB::G(a) - GdiSoftFB::B(a)) / 3;
				int v = 128 + (int)(d * amount);
				if (v < 0) v = 0; if (v > 255) v = 255;
				DWORD o = src[(size_t)y * fb.w + x];
				fb.color[(size_t)y * fb.w + x] = GdiSoftFB::PackBGRA(GdiSoftFB::A(o), (BYTE)v, (BYTE)v, (BYTE)v);
			}
		}
	}

	void Context::Scanlines(float strength, int period)
	{
		if (!fb.color || strength <= 0.f) return;
		if (period < 1) period = 1;
		if (strength > 1.f) strength = 1.f;
		const int dark = (int)(255.f * (1.f - strength) + 0.5f);
		for (int y = 0; y < fb.h; ++y) {
			if ((y % period) != 0) continue;
			DWORD* row = fb.Row(y);
			for (int x = 0; x < fb.w; ++x) {
				DWORD p = row[x];
				BYTE r = (BYTE)((GdiSoftFB::R(p) * dark) / 255);
				BYTE g = (BYTE)((GdiSoftFB::G(p) * dark) / 255);
				BYTE b = (BYTE)((GdiSoftFB::B(p) * dark) / 255);
				row[x] = GdiSoftFB::PackBGRA(GdiSoftFB::A(p), r, g, b);
			}
		}
	}

	void Context::ChromaticAberration(int shiftPx)
	{
		if (!fb.color || shiftPx == 0) return;
		if (shiftPx < 0) shiftPx = -shiftPx;
		if (shiftPx > 8) shiftPx = 8;
		std::vector<DWORD> src((size_t)fb.w * (size_t)fb.h);
		memcpy(src.data(), fb.color, src.size() * 4);
		for (int y = 0; y < fb.h; ++y) {
			for (int x = 0; x < fb.w; ++x) {
				const int xl = (std::max)(0, x - shiftPx);
				const int xr = (std::min)(fb.w - 1, x + shiftPx);
				DWORD cL = src[(size_t)y * fb.w + xl];
				DWORD cC = src[(size_t)y * fb.w + x];
				DWORD cR = src[(size_t)y * fb.w + xr];
				fb.color[(size_t)y * fb.w + x] = GdiSoftFB::PackBGRA(
					GdiSoftFB::A(cC),
					GdiSoftFB::R(cR),
					GdiSoftFB::G(cC),
					GdiSoftFB::B(cL));
			}
		}
	}
}
