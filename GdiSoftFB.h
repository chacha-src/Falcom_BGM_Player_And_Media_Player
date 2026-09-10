#pragma once
// Soft GDI shared ARGB32 + Z framebuffer (no D3D/D2D runtime).
#include <afxwin.h>
#include <cstring>
#include <new>

namespace GdiSoftFB
{
	// Pixel: 0xAARRGGBB in memory as DWORD (Windows DIB is BGRA layout in bytes).
	inline DWORD PackBGRA(BYTE a, BYTE r, BYTE g, BYTE b)
	{
		return ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
	}
	inline DWORD PackColorref(COLORREF c, BYTE a = 255)
	{
		return PackBGRA(a, GetRValue(c), GetGValue(c), GetBValue(c));
	}
	inline BYTE A(DWORD p) { return (BYTE)((p >> 24) & 255); }
	inline BYTE R(DWORD p) { return (BYTE)((p >> 16) & 255); }
	inline BYTE G(DWORD p) { return (BYTE)((p >> 8) & 255); }
	inline BYTE B(DWORD p) { return (BYTE)(p & 255); }

	inline DWORD BlendSrcOver(DWORD dst, DWORD src)
	{
		const int sa = (int)A(src);
		if (sa >= 255) return src;
		if (sa <= 0) return dst;
		const int inv = 255 - sa;
		const int da = (int)A(dst);
		const int outA = sa + (da * inv + 127) / 255;
		const int outR = (R(src) * sa + R(dst) * inv + 127) / 255;
		const int outG = (G(src) * sa + G(dst) * inv + 127) / 255;
		const int outB = (B(src) * sa + B(dst) * inv + 127) / 255;
		return PackBGRA((BYTE)outA, (BYTE)outR, (BYTE)outG, (BYTE)outB);
	}

	struct Framebuffer {
		int w = 0;
		int h = 0;
		DWORD* color = nullptr;   // top-down BGRA
		float* z = nullptr;       // larger = farther (camera-space depth)
		HDC hdc = nullptr;
		HBITMAP dib = nullptr;
		HBITMAP oldBmp = nullptr;
		DWORD* colorOwned = nullptr;
		float* zOwned = nullptr;
		int colorCap = 0;
		int zCap = 0;
		bool useDib = true;

		~Framebuffer() { Destroy(); }

		void Destroy()
		{
			if (hdc && oldBmp) {
				::SelectObject(hdc, oldBmp);
				oldBmp = nullptr;
			}
			if (dib) { ::DeleteObject(dib); dib = nullptr; }
			if (hdc) { ::DeleteDC(hdc); hdc = nullptr; }
			color = nullptr;
			z = nullptr;
			delete[] colorOwned;
			colorOwned = nullptr;
			colorCap = 0;
			delete[] zOwned;
			zOwned = nullptr;
			zCap = 0;
			w = h = 0;
		}

		bool Resize(int width, int height, bool withZ = true, bool preferDib = true)
		{
			if (width < 1) width = 1;
			if (height < 1) height = 1;
			if (w == width && h == height && color && (!withZ || z) && (preferDib == useDib || !preferDib))
				return true;

			if (hdc && oldBmp) {
				::SelectObject(hdc, oldBmp);
				oldBmp = nullptr;
			}
			if (dib) { ::DeleteObject(dib); dib = nullptr; }
			if (hdc) { ::DeleteDC(hdc); hdc = nullptr; }
			color = nullptr;
			z = nullptr;
			w = width;
			h = height;
			useDib = preferDib;

			const size_t n = (size_t)w * (size_t)h;
			if (withZ) {
				if ((int)n > zCap) {
					int cap = zCap > 0 ? zCap : 4096;
					while ((size_t)cap < n) {
						if (cap > (INT_MAX / 2)) { cap = (int)n; break; }
						cap *= 2;
					}
					float* nz = new (std::nothrow) float[cap];
					if (!nz) return false;
					delete[] zOwned;
					zOwned = nz;
					zCap = cap;
				}
				z = zOwned;
				for (size_t i = 0; i < n; ++i) z[i] = 1e9f;
			}

			if (preferDib) {
				BITMAPINFO bi = {};
				bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bi.bmiHeader.biWidth = w;
				bi.bmiHeader.biHeight = -h; // top-down
				bi.bmiHeader.biPlanes = 1;
				bi.bmiHeader.biBitCount = 32;
				bi.bmiHeader.biCompression = BI_RGB;
				void* bits = nullptr;
				hdc = ::CreateCompatibleDC(nullptr);
				dib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				if (!hdc || !dib || !bits) {
					if (dib) { ::DeleteObject(dib); dib = nullptr; }
					if (hdc) { ::DeleteDC(hdc); hdc = nullptr; }
					preferDib = false;
					useDib = false;
				} else {
					oldBmp = (HBITMAP)::SelectObject(hdc, dib);
					color = (DWORD*)bits;
					return true;
				}
			}
			if ((int)n > colorCap) {
				int cap = colorCap > 0 ? colorCap : 4096;
				while ((size_t)cap < n) {
					if (cap > (INT_MAX / 2)) { cap = (int)n; break; }
					cap *= 2;
				}
				DWORD* nc = new (std::nothrow) DWORD[cap];
				if (!nc) return false;
				delete[] colorOwned;
				colorOwned = nc;
				colorCap = cap;
			}
			color = colorOwned;
			for (size_t i = 0; i < n; ++i) color[i] = 0;
			return color != nullptr;
		}

		void Clear(DWORD bg, float zFar = 1e9f)
		{
			if (!color || w <= 0 || h <= 0) return;
			const size_t n = (size_t)w * (size_t)h;
			for (size_t i = 0; i < n; ++i) color[i] = bg;
			if (z) {
				for (size_t i = 0; i < n; ++i) z[i] = zFar;
			}
		}

		void ClearColorref(COLORREF bg, BYTE a = 255, float zFar = 1e9f)
		{
			Clear(PackColorref(bg, a), zFar);
		}

		DWORD* Row(int y) { return color + (size_t)y * (size_t)w; }
		float* ZRow(int y) { return z ? z + (size_t)y * (size_t)w : nullptr; }

		void Put(int x, int y, DWORD p)
		{
			if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
			color[(size_t)y * (size_t)w + (size_t)x] = p;
		}

		void PutBlend(int x, int y, DWORD p)
		{
			if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
			DWORD& d = color[(size_t)y * (size_t)w + (size_t)x];
			d = BlendSrcOver(d, p);
		}

		bool DepthTestWrite(int x, int y, float depth, bool test, bool write)
		{
			if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return false;
			if (!z) return true;
			float& zd = z[(size_t)y * (size_t)w + (size_t)x];
			// nearer = smaller depth
			if (test && !(depth < zd)) return false;
			if (write) zd = depth;
			return true;
		}

		BOOL Present(HDC dst, int dx, int dy) const
		{
			if (!color || w <= 0 || h <= 0 || !dst) return FALSE;
			if (hdc && dib) {
				return ::BitBlt(dst, dx, dy, w, h, hdc, 0, 0, SRCCOPY);
			}
			BITMAPINFO bi = {};
			bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bi.bmiHeader.biWidth = w;
			bi.bmiHeader.biHeight = -h;
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bi.bmiHeader.biCompression = BI_RGB;
			return ::SetDIBitsToDevice(dst, dx, dy, w, h, 0, 0, 0, h, color, &bi, DIB_RGB_COLORS) != 0;
		}

		BOOL Present(CDC& dc, int dx, int dy) const
		{
			return Present(dc.GetSafeHdc(), dx, dy);
		}

		BOOL PresentAlpha(HDC dst, int dx, int dy, BYTE constAlpha = 255) const
		{
			if (!hdc || !dib || !dst) return Present(dst, dx, dy);
			BLENDFUNCTION bf = { AC_SRC_OVER, 0, constAlpha, AC_SRC_ALPHA };
			return ::GdiAlphaBlend(dst, dx, dy, w, h, hdc, 0, 0, w, h, bf);
		}
	};
}
