#pragma once
// Soft2D — D2D-like CPU API over GdiSoftFB (no Direct2D).
#include "GdiSoftFB.h"
#include <vector>

namespace GdiSoft2D
{
	struct ClipRect { int l, t, r, b; };

	struct Context {
		GdiSoftFB::Framebuffer fb;
		std::vector<ClipRect> clipStack;

		bool Create(int w, int h, bool withZ = false)
		{
			clipStack.clear();
			return fb.Resize(w, h, withZ, true);
		}
		bool Resize(int w, int h, bool withZ = false) { return Create(w, h, withZ); }

		void Clear(COLORREF c, BYTE a = 255) { fb.ClearColorref(c, a); }
		void ClearArgb(DWORD p) { fb.Clear(p); }

		BOOL Present(HDC dst, int x = 0, int y = 0) const { return fb.Present(dst, x, y); }
		BOOL Present(CDC& dc, int x = 0, int y = 0) const { return fb.Present(dc, x, y); }
		BOOL PresentAlpha(HDC dst, int x = 0, int y = 0, BYTE a = 255) const
		{
			return fb.PresentAlpha(dst, x, y, a);
		}

		void PushClipRect(int l, int t, int r, int b);
		void PopClip();
		ClipRect CurrentClip() const;

		void FillRect(int x, int y, int w, int h, COLORREF c, BYTE a = 255);
		void FillEllipse(int cx, int cy, int rx, int ry, COLORREF c, BYTE a = 255);
		void DrawLine(int x0, int y0, int x1, int y1, COLORREF c, BYTE a = 255, int thickness = 1);
		void DrawRect(int x, int y, int w, int h, COLORREF c, BYTE a = 255, int thickness = 1);
		void DrawEllipse(int cx, int cy, int rx, int ry, COLORREF c, BYTE a = 255, int thickness = 1);

		void DrawBitmap(HDC src, int sx, int sy, int sw, int sh,
			int dx, int dy, int dw, int dh);
		void DrawBitmapAlpha(HDC src, int sx, int sy, int sw, int sh,
			int dx, int dy, int dw, int dh, BYTE constAlpha);
		// Affine: dest parallelogram (p0,p1,p2) = top-left, top-right, bottom-left
		void DrawBitmapAffine(HDC src, int sw, int sh, const POINT p[3], BYTE constAlpha = 255);

		void Fade(BYTE mulA); // multiply alpha of all pixels
		void BlurRect(int x, int y, int w, int h, int radius); // box blur approx
		void GradientFillRectH(int x, int y, int w, int h, COLORREF c0, COLORREF c1, BYTE a = 255);
		void GradientFillRectV(int x, int y, int w, int h, COLORREF c0, COLORREF c1, BYTE a = 255);

		// Photoshop-ish post (CPU)
		void Vignette(float strength = 0.45f);          // 端を暗く
		void Posterize(int levels = 6);                 // 諧調削減
		void Saturate(float amount = 1.25f);            // 1=そのまま
		void GlowBloom(int radius = 2, float amount = 0.35f); // 明るい画素をぼかして加算
		void Emboss(float amount = 0.55f);              // 簡易エンボス
		void Scanlines(float strength = 0.22f, int period = 2); // CRT風スキャンライン
		void ChromaticAberration(int shiftPx = 1);      // R/B チャンネルずらす
	};
}
