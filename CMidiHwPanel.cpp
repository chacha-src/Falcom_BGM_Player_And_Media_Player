#include "stdafx.h"
#include "CMidiHwPanel.h"

/*
 * 実機 LCD:
 *   GS/SC-55/88/88Pro/8820 … 橙バックライト、16 パートバー
 *   XG (MU)               … 緑
 *   LA / MT-32            … 黄緑、20 文字 LED
 *   SD-90                 … 青白
 *   Korg NS 系            … 青
 *   GM / GM2              … 薄緑
 * SysEx:
 *   Roland 45h  10 00 00 文字（17字以上は自動スクロール）
 *               10 0p 00/40  16x16 ドット（バー位置。d00-d63、下位5bit）
 *               10 20 00 ページ / 10 20 01 表示時間（0-7.2秒。未指定は4秒でバーへ）
 *   Yamaha XG 4C 06 文字 / 4C 07 は 16x16 として扱う
 *   MT-32 16h 20 00 00  20 文字
 */

static int LcdSc(int v96, UINT dpi)
{
	return MulDiv(v96, (int)dpi, 96);
}

enum {
	kKindGsAmber = 0,
	kKindXgGreen,
	kKindLaGreen,
	kKindGmGreen,
	kKindSdBlue,
	kKindKorgBlue
};

struct LcdPal {
	COLORREF bezel, bezelHi, lcd, pixel, pixDim, barEmpty, cursor, brand;
};

static LcdPal LcdPalette(int kind)
{
	LcdPal p;
	p.bezel = RGB(32, 32, 36);
	p.bezelHi = RGB(72, 72, 78);
	p.brand = RGB(188, 188, 196);
	p.cursor = RGB(24, 10, 4);
	switch (kind) {
	case kKindXgGreen:
		p.lcd = RGB(62, 186, 78);
		p.pixel = RGB(10, 36, 12);
		p.pixDim = RGB(48, 150, 60);
		p.barEmpty = RGB(50, 158, 64);
		p.cursor = RGB(8, 28, 8);
		break;
	case kKindLaGreen:
		p.lcd = RGB(102, 186, 54);
		p.pixel = RGB(18, 40, 8);
		p.pixDim = RGB(82, 154, 42);
		p.barEmpty = RGB(86, 160, 44);
		p.cursor = RGB(12, 28, 4);
		break;
	case kKindGmGreen:
		p.lcd = RGB(148, 196, 150);
		p.pixel = RGB(28, 48, 30);
		p.pixDim = RGB(120, 168, 122);
		p.barEmpty = RGB(124, 172, 126);
		p.cursor = RGB(20, 36, 22);
		break;
	case kKindSdBlue:
		p.lcd = RGB(118, 168, 214);
		p.pixel = RGB(16, 32, 56);
		p.pixDim = RGB(92, 140, 186);
		p.barEmpty = RGB(96, 146, 192);
		p.cursor = RGB(10, 22, 40);
		p.bezel = RGB(28, 32, 40);
		break;
	case kKindKorgBlue:
		p.lcd = RGB(42, 118, 196);
		p.pixel = RGB(6, 18, 40);
		p.pixDim = RGB(32, 92, 160);
		p.barEmpty = RGB(36, 98, 168);
		p.cursor = RGB(4, 12, 28);
		p.brand = RGB(200, 210, 230);
		break;
	default: /* Roland 橙（写真の SC-88） */
		p.lcd = RGB(232, 148, 28);
		p.pixel = RGB(48, 22, 6);
		p.pixDim = RGB(204, 124, 20);
		p.barEmpty = RGB(210, 128, 22);
		p.cursor = RGB(36, 14, 4);
		break;
	}
	return p;
}

int MidiHwLcdKind(int sysMode, int gsMapKind)
{
	if (gsMapKind == 8) return kKindLaGreen;
	if (gsMapKind == 6) return kKindSdBlue;
	if (gsMapKind >= 10) return kKindKorgBlue;
	if (sysMode == 2) return kKindXgGreen;
	if (gsMapKind == 5 || gsMapKind == 9) return kKindGmGreen;
	if (sysMode == 0 && gsMapKind == 0) return kKindGmGreen;
	return kKindGsAmber;
}

const wchar_t* MidiHwLcdModelName(int sysMode, int gsMapKind)
{
	if (gsMapKind == 1) return L"SC-55";
	if (gsMapKind == 2) return L"SC-88";
	if (gsMapKind == 3) return L"SC-88Pro";
	if (gsMapKind == 4) return L"SC-8820";
	if (gsMapKind == 6) return L"SD-90";
	if (gsMapKind == 8) return L"MT-32";
	if (gsMapKind == 5) return L"GM";
	if (gsMapKind == 9) return L"GM2";
	if (gsMapKind == 10) return L"NS5R";
	if (sysMode == 2) return L"XG";
	if (sysMode == 1) return L"GS";
	return L"GM";
}

int MidiHwLcdCh32(int gs32, int heardHi)
{
	return (gs32 || heardHi) ? 1 : 0;
}

int MidiHwLcdSeg(float lev, int held)
{
	int s = (int)(lev * 16.f + 0.45f);
	if (held && s < 1) s = 1;
	if (s < 0) s = 0;
	if (s > 16) s = 16;
	return s;
}

int MidiHwLcdHeadH(UINT dpi)
{
	return LcdSc(112, dpi);
}

int MidiHwLcdScrollIdx(const MidiHwLcdState* s, DWORD nowMs)
{
	if (!s || s->letterN <= 16) return 0;
	int span = s->letterN - 16 + 1;
	if (span < 1) return 0;
	return (int)((nowMs / 280) % (DWORD)span);
}

int MidiHwLcdAnim(const MidiHwLcdState* s)
{
	return (s && (s->letterN > 16 || s->showPage > 0)) ? 1 : 0;
}

void MidiHwLcdReset(MidiHwLcdState* s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->dispTime = -1;
}

static void LcdMarkShown(MidiHwLcdState* s)
{
	if (s) s->shownMs = GetTickCount();
}

static int LcdHoldMs(const MidiHwLcdState* s)
{
	/* 実機 00-0F = 0-7.2秒（初期値 06=2.88秒）。未指定は 4秒。 */
	if (!s || s->dispTime < 0) return 4000;
	return (s->dispTime * 7200) / 15;
}

int MidiHwLcdTick(MidiHwLcdState* s, DWORD nowMs)
{
	if (!s || s->showPage <= 0 || s->shownMs == 0) return 0;
	if ((DWORD)(nowMs - s->shownMs) < (DWORD)LcdHoldMs(s)) return 0;
	s->showPage = 0;
	s->shownMs = 0;
	s->gen++;
	return 1;
}

static int LcdHasLetter(const MidiHwLcdState& st)
{
	for (int i = 0; i < st.letterN && i < 32; ++i) {
		BYTE c = st.letter[i];
		if (c > 32 && c < 127) return 1;
	}
	return 0;
}

static void LcdPutBytes(BYTE* dst, int dstN, int off, const BYTE* src, int n)
{
	if (!dst || !src || dstN <= 0) return;
	for (int i = 0; i < n; ++i) {
		const int p = off + i;
		if (p < 0 || p >= dstN) continue;
		dst[p] = src[i] & 127;
	}
}

int MidiHwLcdApplySysex(MidiHwLcdState* s, const BYTE* d, int n)
{
	if (!s || !d || n < 8 || d[0] != 0xf0) return 0;
	const int hasF7 = (n > 0 && d[n - 1] == 0xf7) ? 1 : 0;

	/* Roland Display Data (model 45h) */
	if (d[1] == 0x41 && n >= 10 && d[3] == 0x45 && d[4] == 0x12) {
		const int aa = d[5], bb = d[6], cc = d[7];
		int nval = n - 8 - hasF7 - 1;
		if (nval < 0) nval = 0;
		if (aa == 0x10 && bb == 0x00) {
			LcdPutBytes(s->letter, 32, cc, d + 8, nval);
			int end = cc + nval;
			if (end > s->letterN) s->letterN = (end > 32) ? 32 : end;
			s->mode = LcdHasLetter(*s) ? 1 : 0;
			s->gen++;
			return 1;
		}
		if (aa == 0x10 && bb >= 0x01 && bb <= 0x05) {
			const int even = (cc >= 0x40) ? 1 : 0;
			const int page = (bb - 1) * 2 + even;
			const int dOff = even ? (cc - 0x40) : cc;
			if (page >= 0 && page < 10) {
				LcdPutBytes(s->page[page], 64, dOff, d + 8, nval);
				s->pageOn[page] = 1;
				if (page == 0)
					s->showPage = 1;
				if (s->showPage == page + 1)
					LcdMarkShown(s);
				s->gen++;
				return 1;
			}
		}
		if (aa == 0x10 && bb == 0x20 && cc == 0x00 && nval >= 1) {
			int pg = d[8] & 15;
			if (pg > 10) pg = 0;
			s->showPage = pg;
			if (pg > 0) LcdMarkShown(s);
			else s->shownMs = 0;
			s->gen++;
			return 1;
		}
		if (aa == 0x10 && bb == 0x20 && cc == 0x01 && nval >= 1) {
			s->dispTime = d[8] & 15;
			s->gen++;
			return 1;
		}
		return 0;
	}

	/* MT-32 / CM-32L LCD 20 chars at 20 00 00 */
	if (d[1] == 0x41 && n >= 10 && d[3] == 0x16 && d[4] == 0x12
		&& d[5] == 0x20 && d[6] == 0x00) {
		const int cc = d[7];
		int nval = n - 8 - hasF7 - 1;
		if (nval < 0) nval = 0;
		LcdPutBytes(s->letter, 32, cc, d + 8, nval);
		int end = cc + nval;
		if (end > s->letterN) s->letterN = (end > 32) ? 32 : end;
		s->mode = LcdHasLetter(*s) ? 1 : 0;
		s->gen++;
		return 1;
	}

	/* Yamaha XG / Korg XG 互換: 4C 06 letter, 4C 07 bitmap */
	if ((d[1] == 0x43 || d[1] == 0x42) && n >= 8 && d[3] == 0x4c) {
		const int cmd = d[2] & 0xf0;
		int ah = 0, am = 0, al = 0, nd = 0;
		const BYTE* data = NULL;
		if (cmd == 0x10 || (d[1] == 0x42 && cmd == 0x30)) {
			ah = d[4] & 127;
			am = d[5] & 127;
			al = d[6] & 127;
			data = d + 7;
			nd = n - 7 - hasF7;
		} else if (cmd == 0x00 && n >= 12) {
			ah = d[6] & 127;
			am = d[7] & 127;
			al = d[8] & 127;
			data = d + 9;
			nd = n - 9 - hasF7 - 1;
		}
		if (data && nd > 0 && am == 0x00) {
			if (ah == 0x06) {
				LcdPutBytes(s->letter, 32, al, data, nd);
				int end = al + nd;
				if (end > s->letterN) s->letterN = (end > 32) ? 32 : end;
				s->mode = LcdHasLetter(*s) ? 1 : 0;
				s->gen++;
				return 1;
			}
			if (ah == 0x07) {
				LcdPutBytes(s->page[0], 64, al, data, nd);
				s->pageOn[0] = 1;
				s->showPage = 1;
				LcdMarkShown(s);
				s->gen++;
				return 1;
			}
		}
	}
	return 0;
}

int MidiHwLcdReserve(int w, UINT dpi, int ch32, CRect* outAll, CRect* outA, CRect* outB)
{
	if (outAll) outAll->SetRectEmpty();
	if (outA) outA->SetRectEmpty();
	if (outB) outB->SetRectEmpty();
	if (w < LcdSc(80, dpi)) return w;
	const int nPanel = ch32 ? 2 : 1;
	const int gap = LcdSc(3, dpi);
	const int y0 = LcdSc(2, dpi);
	const int h = LcdSc(86, dpi);
	int oneW = LcdSc(ch32 ? 230 : 252, dpi);
	int total = nPanel * oneW + (nPanel - 1) * gap;
	const int cap = w * 58 / 100;
	if (total > cap) {
		oneW = (cap - (nPanel - 1) * gap) / nPanel;
		if (oneW < LcdSc(168, dpi)) oneW = LcdSc(168, dpi);
		total = nPanel * oneW + (nPanel - 1) * gap;
	}
	if (total > w - LcdSc(36, dpi))
		total = w - LcdSc(36, dpi);
	if (total < LcdSc(140, dpi))
		total = min(w - LcdSc(8, dpi), LcdSc(140, dpi));
	const int x0 = w - LcdSc(4, dpi) - total;
	if (outAll)
		outAll->SetRect(x0, y0, x0 + total, y0 + h);
	if (outA)
		outA->SetRect(x0, y0, x0 + (ch32 ? oneW : total), y0 + h);
	if (outB && ch32)
		outB->SetRect(x0 + oneW + gap, y0, x0 + total, y0 + h);
	return x0;
}

/* 5x7 LED。列優先、bit0=上。ASCII 32-126。未定義は空白。 */
static const BYTE kLed5x7[95][5] = {
	{0,0,0,0,0}, /* space */
	{0,0,0x5f,0,0}, /* ! */
	{0,3,0,3,0},
	{0x14,0x7f,0x14,0x7f,0x14}, /* # */
	{0x24,0x2a,0x7f,0x2a,0x12},
	{0x23,0x13,0x08,0x64,0x62},
	{0x36,0x49,0x55,0x22,0x50},
	{0,5,3,0,0},
	{0,0x1c,0x22,0x41,0},
	{0,0x41,0x22,0x1c,0},
	{0x14,0x08,0x3e,0x08,0x14},
	{0x08,0x08,0x3e,0x08,0x08},
	{0,0x50,0x30,0,0},
	{0x08,0x08,0x08,0x08,0x08},
	{0,0x60,0x60,0,0},
	{0x20,0x10,0x08,0x04,0x02},
	{0x3e,0x51,0x49,0x45,0x3e}, /* 0 */
	{0,0x42,0x7f,0x40,0},
	{0x42,0x61,0x51,0x49,0x46},
	{0x21,0x41,0x45,0x4b,0x31},
	{0x18,0x14,0x12,0x7f,0x10},
	{0x27,0x45,0x45,0x45,0x39},
	{0x3c,0x4a,0x49,0x49,0x30},
	{0x01,0x71,0x09,0x05,0x03},
	{0x36,0x49,0x49,0x49,0x36},
	{0x06,0x49,0x49,0x29,0x1e},
	{0,0x36,0x36,0,0},
	{0,0x56,0x36,0,0},
	{0x08,0x14,0x22,0x41,0},
	{0x14,0x14,0x14,0x14,0x14},
	{0,0x41,0x22,0x14,0x08},
	{0x02,0x01,0x51,0x09,0x06},
	{0x32,0x49,0x79,0x41,0x3e}, /* @ */
	{0x7e,0x11,0x11,0x11,0x7e}, /* A */
	{0x7f,0x49,0x49,0x49,0x36},
	{0x3e,0x41,0x41,0x41,0x22},
	{0x7f,0x41,0x41,0x22,0x1c},
	{0x7f,0x49,0x49,0x49,0x41},
	{0x7f,0x09,0x09,0x09,0x01},
	{0x3e,0x41,0x49,0x49,0x7a},
	{0x7f,0x08,0x08,0x08,0x7f},
	{0,0x41,0x7f,0x41,0},
	{0x20,0x40,0x41,0x3f,0x01},
	{0x7f,0x08,0x14,0x22,0x41},
	{0x7f,0x40,0x40,0x40,0x40},
	{0x7f,0x02,0x0c,0x02,0x7f},
	{0x7f,0x04,0x08,0x10,0x7f},
	{0x3e,0x41,0x41,0x41,0x3e},
	{0x7f,0x09,0x09,0x09,0x06},
	{0x3e,0x41,0x51,0x21,0x5e},
	{0x7f,0x09,0x19,0x29,0x46},
	{0x46,0x49,0x49,0x49,0x31},
	{0x01,0x01,0x7f,0x01,0x01},
	{0x3f,0x40,0x40,0x40,0x3f},
	{0x1f,0x20,0x40,0x20,0x1f},
	{0x3f,0x40,0x38,0x40,0x3f},
	{0x63,0x14,0x08,0x14,0x63},
	{0x07,0x08,0x70,0x08,0x07},
	{0x61,0x51,0x49,0x45,0x43},
	{0,0x7f,0x41,0x41,0},
	{0x02,0x04,0x08,0x10,0x20},
	{0,0x41,0x41,0x7f,0},
	{0x04,0x02,0x01,0x02,0x04},
	{0x40,0x40,0x40,0x40,0x40},
	{0,0x01,0x02,0x04,0},
	{0x20,0x54,0x54,0x54,0x78}, /* a */
	{0x7f,0x48,0x44,0x44,0x38},
	{0x38,0x44,0x44,0x44,0x20},
	{0x38,0x44,0x44,0x48,0x7f},
	{0x38,0x54,0x54,0x54,0x18},
	{0x08,0x7e,0x09,0x01,0x02},
	{0x0c,0x52,0x52,0x52,0x3e},
	{0x7f,0x08,0x04,0x04,0x78},
	{0,0x44,0x7d,0x40,0},
	{0x20,0x40,0x44,0x3d,0},
	{0x7f,0x10,0x28,0x44,0},
	{0,0x41,0x7f,0x40,0},
	{0x7c,0x04,0x18,0x04,0x78},
	{0x7c,0x08,0x04,0x04,0x78},
	{0x38,0x44,0x44,0x44,0x38},
	{0x7c,0x14,0x14,0x14,0x08},
	{0x08,0x14,0x14,0x18,0x7c},
	{0x7c,0x08,0x04,0x04,0x08},
	{0x48,0x54,0x54,0x54,0x20},
	{0x04,0x3f,0x44,0x40,0x20},
	{0x3c,0x40,0x40,0x20,0x7c},
	{0x1c,0x20,0x40,0x20,0x1c},
	{0x3c,0x40,0x30,0x40,0x3c},
	{0x44,0x28,0x10,0x28,0x44},
	{0x0c,0x50,0x50,0x50,0x3c},
	{0x44,0x64,0x54,0x4c,0x44},
	{0,0x08,0x36,0x41,0},
	{0,0,0x7f,0,0},
	{0,0x41,0x36,0x08,0},
	{0x10,0x08,0x08,0x10,0x08},
};

static const BYTE* LcdGlyph(int ch)
{
	if (ch < 32 || ch > 126) return kLed5x7[0];
	return kLed5x7[ch - 32];
}

static void LcdDot(CDC& dc, int x, int y, int px, int py, COLORREF c)
{
	if (px < 1) px = 1;
	if (py < 1) py = 1;
	if (px >= 2 && py >= 2) {
		CBrush br(c);
		CPen pe(PS_SOLID, 1, c);
		CBrush* oldB = dc.SelectObject(&br);
		CPen* oldP = dc.SelectObject(&pe);
		dc.Ellipse(x, y, x + px, y + py);
		dc.SelectObject(oldB);
		dc.SelectObject(oldP);
		return;
	}
	dc.FillSolidRect(x, y, px, py, c);
}

static void LcdGlyphAt(CDC& dc, int x, int y, int px, int py, int gap, int ch, COLORREF on)
{
	const BYTE* g = LcdGlyph(ch);
	for (int col = 0; col < 5; ++col) {
		BYTE bits = g[col];
		for (int row = 0; row < 7; ++row) {
			if (bits & (1 << row))
				LcdDot(dc, x + col * (px + gap), y + row * (py + gap), px, py, on);
		}
	}
}

static int LcdGlyphW(int px, int gap)
{
	return 5 * px + 4 * gap;
}

static int LcdGlyphH(int py, int gap)
{
	return 7 * py + 6 * gap;
}

static void LcdText(CDC& dc, int x, int y, int px, int py, int gap, const char* s, COLORREF on)
{
	if (!s) return;
	const int cw = LcdGlyphW(px, gap) + px;
	for (; *s; ++s) {
		LcdGlyphAt(dc, x, y, px, py, gap, (unsigned char)*s, on);
		x += cw;
	}
}

static void LcdText16(CDC& dc, const CRect& rc, const char* s16, COLORREF on)
{
	if (!s16 || rc.Width() < 16 || rc.Height() < 7) return;
	const int cell = rc.Width() / 16;
	int px = max(1, cell / 6);
	int gap = (px >= 2) ? 1 : 0;
	while (LcdGlyphW(px, gap) + 1 > cell && px > 1) --px;
	int py = px;
	while (LcdGlyphH(py, gap) > rc.Height() && py > 1) --py;
	const int gw = LcdGlyphW(px, gap);
	const int y = rc.top;
	for (int i = 0; i < 16; ++i) {
		const unsigned char ch = (unsigned char)(s16[i] ? s16[i] : ' ');
		const int x = rc.left + i * cell + max(0, (cell - gw) / 2);
		LcdGlyphAt(dc, x, y, px, py, gap, ch, on);
	}
}

static void LcdTextFit(CDC& dc, const CRect& rc, const char* s, COLORREF on)
{
	if (!s || !s[0] || rc.Width() < 4 || rc.Height() < 6) return;
	int n = 0;
	while (s[n]) ++n;
	int px = 2, py = 2, gap = 1;
	for (;;) {
		const int cw = LcdGlyphW(px, gap) + px;
		const int ch = LcdGlyphH(py, gap);
		if (cw * n <= rc.Width() && ch <= rc.Height())
			break;
		if (px <= 1 && py <= 1 && gap <= 0) break;
		if (gap > 0) gap = 0;
		else if (px > 1) --px;
		else if (py > 1) --py;
		else break;
	}
	const int cw = LcdGlyphW(px, gap) + px;
	const int ch = LcdGlyphH(py, gap);
	int x = rc.left + max(0, (rc.Width() - cw * n) / 2);
	int y = rc.top + max(0, (rc.Height() - ch) / 2);
	LcdText(dc, x, y, px, py, gap, s, on);
}

static void LcdTextLeft(CDC& dc, int x, int y, int px, int py, int gap, const char* s, COLORREF on, int maxW)
{
	if (!s) return;
	const int cw = LcdGlyphW(px, gap) + px;
	int used = 0;
	for (; *s; ++s) {
		if (used + LcdGlyphW(px, gap) > maxW) break;
		LcdGlyphAt(dc, x + used, y, px, py, gap, (unsigned char)*s, on);
		used += cw;
	}
}

static void LcdWcsToUtf(const wchar_t* w, char* out, int outN)
{
	if (!out || outN < 1) return;
	out[0] = 0;
	if (!w) return;
	WideCharToMultiByte(CP_ACP, 0, w, -1, out, outN, "?", NULL);
	out[outN - 1] = 0;
}

/* GS 16x16: 各行 y は d[y], d[16+y], d[32+y] の bit4..0 が左から5ドット、d[48+y] の bit4 が右端。 */
static int GsDot(const BYTE* d64, int x, int y)
{
	if (!d64 || x < 0 || x > 15 || y < 0 || y > 15) return 0;
	if (x >= 15)
		return (d64[48 + y] >> 4) & 1;
	const int group = x / 5;
	const int bit = 4 - (x % 5);
	return (d64[group * 16 + y] >> bit) & 1;
}

static void LcdDrawGs16(CDC& dc, const CRect& rc, const BYTE* d64, COLORREF on, COLORREF off)
{
	if (rc.Width() < 16 || rc.Height() < 16) return;
	const int cell = min(rc.Width() / 16, rc.Height() / 16);
	if (cell < 1) return;
	const int pad = (cell >= 3) ? 1 : 0;
	const int dw = max(1, cell - pad);
	const int dh = max(1, cell - pad);
	const int ox = rc.left + (rc.Width() - cell * 16) / 2;
	const int oy = rc.top + (rc.Height() - cell * 16) / 2;
	CBrush brOn(on), brOff(off);
	CPen peOn(PS_SOLID, 1, on), peOff(PS_SOLID, 1, off);
	CBrush* oldB = dc.SelectObject(&brOff);
	CPen* oldP = dc.SelectObject(&peOff);
	int last = 0;
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const int lit = GsDot(d64, x, y);
			if (lit != last) {
				dc.SelectObject(lit ? &brOn : &brOff);
				dc.SelectObject(lit ? &peOn : &peOff);
				last = lit;
			}
			if (dw >= 2 && dh >= 2)
				dc.Ellipse(ox + x * cell, oy + y * cell, ox + x * cell + dw, oy + y * cell + dh);
			else
				dc.FillSolidRect(ox + x * cell, oy + y * cell, dw, dh, lit ? on : off);
		}
	}
	dc.SelectObject(oldB);
	dc.SelectObject(oldP);
}

static void LcdDrawMeters(CDC& dc, const CRect& rc, const MidiHwLcdPartSnap parts16[16], int sel, COLORREF on, COLORREF off, COLORREF cursor)
{
	const int nBar = 16, nSeg = 16;
	if (rc.Width() < 16 || rc.Height() < 16) return;
	const int cell = min(rc.Width() / nBar, rc.Height() / nSeg);
	if (cell < 1) return;
	const int pad = (cell >= 3) ? 1 : 0;
	const int dw = max(1, cell - pad);
	const int dh = max(1, cell - pad);
	const int ox = rc.left + (rc.Width() - cell * nBar) / 2;
	const int oy = rc.top + (rc.Height() - cell * nSeg) / 2;
	CBrush brOn(on), brOff(off);
	CPen peOn(PS_SOLID, 1, on), peOff(PS_SOLID, 1, off);
	CBrush* oldB = dc.SelectObject(&brOff);
	CPen* oldP = dc.SelectObject(&peOff);
	int last = 0;
	for (int i = 0; i < nBar; ++i) {
		const int segs = MidiHwLcdSeg(parts16[i].lev, parts16[i].held);
		for (int s = 0; s < nSeg; ++s) {
			const int lit = (s < segs) ? 1 : 0;
			if (lit != last) {
				dc.SelectObject(lit ? &brOn : &brOff);
				dc.SelectObject(lit ? &peOn : &peOff);
				last = lit;
			}
			const int x = ox + i * cell;
			const int y = oy + (nSeg - 1 - s) * cell;
			if (dw >= 2 && dh >= 2)
				dc.Ellipse(x, y, x + dw, y + dh);
			else
				dc.FillSolidRect(x, y, dw, dh, lit ? on : off);
		}
	}
	dc.SelectObject(oldB);
	dc.SelectObject(oldP);
	if (sel >= 0 && sel < nBar)
		dc.FillSolidRect(ox + sel * cell, oy + nSeg * cell, dw, 2, cursor);
}

static int LcdKeyLit(const BYTE keyBits[16], int note)
{
	if (!keyBits || note < 0 || note > 127) return 0;
	return (keyBits[note >> 3] >> (note & 7)) & 1;
}

/* SC-88 文字の下の鍵盤バー。A0-C8 を LCD ドット色で。 */
static void LcdDrawKb(CDC& dc, const CRect& rc, const BYTE keyBits[16], COLORREF on, COLORREF offW, COLORREF offB)
{
	if (rc.Width() < 24 || rc.Height() < 6) return;
	const int k0 = 21, k1 = 108;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) whites++;
	}
	if (whites < 1) return;
	const int ww = rc.Width();
	const int hh = rc.Height();
	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) continue;
		const int x0 = rc.left + wi * ww / whites;
		const int x1 = rc.left + (wi + 1) * ww / whites;
		dc.FillSolidRect(x0, rc.top, max(1, x1 - x0 - 1), hh, LcdKeyLit(keyBits, n) ? on : offW);
		wi++;
	}
	wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) { wi++; continue; }
		const int xw = rc.left + (wi * ww / whites);
		const int bw = max(2, ww / whites * 6 / 10);
		const int x0 = xw - bw / 2;
		dc.FillSolidRect(x0, rc.top, bw, hh * 6 / 10, LcdKeyLit(keyBits, n) ? on : offB);
	}
}

static void LcdPanStr(int pan, char* out, int outN)
{
	const int v = pan - 64;
	if (v == 0) strcpy_s(out, outN, "0");
	else if (v < 0) sprintf_s(out, outN, "L%d", -v);
	else sprintf_s(out, outN, "R%d", v);
}

void MidiHwLcdDraw(CDC& dc, const CRect& rc, UINT dpi,
	int kind, const wchar_t* model,
	const MidiHwLcdState& st,
	const MidiHwLcdPartSnap parts16[16],
	const BYTE keyBits[16],
	int sel, int bank)
{
	if (rc.Width() < 40 || rc.Height() < 18) return;
	const LcdPal pal = LcdPalette(kind);
	dc.FillSolidRect(rc, pal.bezel);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), 1, pal.bezelHi);
	dc.FillSolidRect(rc.left, rc.top, 1, rc.Height(), pal.bezelHi);

	const int bezel = max(2, LcdSc(3, dpi));
	const int brandH = LcdSc(8, dpi);
	CRect lcd = rc;
	lcd.DeflateRect(bezel, bezel);
	lcd.top += brandH;
	if (lcd.Height() < 12) lcd.top = rc.top + bezel;
	dc.FillSolidRect(lcd, pal.lcd);

	char brand[24];
	LcdWcsToUtf(model && model[0] ? model : L"GS", brand, 24);
	{
		const int px = 1, py = 1, gap = 0;
		const int bh = LcdGlyphH(py, gap);
		int by = rc.top + max(1, (brandH + bezel - bh) / 2);
		LcdText(dc, rc.left + bezel + 1, by, px, py, gap, brand, pal.brand);
		char side[4] = { (char)('A' + (bank ? 1 : 0)), 0, 0, 0 };
		const int cw = LcdGlyphW(px, gap) + px;
		LcdText(dc, rc.right - bezel - cw - 2, by, px, py, gap, side, pal.brand);
	}

	if (sel < 0) sel = 0;
	if (sel > 15) sel = 15;
	const MidiHwLcdPartSnap& sp = parts16[sel];

	const int barArea = max(LcdSc(86, dpi), lcd.Width() * 38 / 100);
	CRect bars = lcd;
	bars.left = lcd.right - barArea;
	bars.DeflateRect(LcdSc(2, dpi), LcdSc(2, dpi));
	CRect left = lcd;
	left.right = bars.left - LcdSc(2, dpi);
	left.DeflateRect(LcdSc(2, dpi), LcdSc(1, dpi));
	const int kbH = max(LcdSc(10, dpi), min(LcdSc(20, dpi), left.Height() * 38 / 100));
	CRect keys = left;
	keys.top = left.bottom - kbH;
	CRect text = left;
	text.bottom = keys.top - LcdSc(1, dpi);
	if (text.Height() < LcdSc(8, dpi)) {
		text = left;
		keys.SetRectEmpty();
	}

	if (st.mode == 1 && LcdHasLetter(st)) {
		char raw[33];
		int n = st.letterN;
		if (n < 0) n = 0;
		if (n > 32) n = 32;
		for (int i = 0; i < n; ++i) {
			BYTE c = st.letter[i];
			raw[i] = (c >= 32 && c < 127) ? (char)c : ' ';
		}
		raw[n] = 0;
		char win[17];
		memset(win, ' ', 16);
		win[16] = 0;
		if (n > 16) {
			const int off = MidiHwLcdScrollIdx(&st, GetTickCount());
			for (int i = 0; i < 16; ++i) {
				const int src = off + i;
				win[i] = (src < n) ? raw[src] : ' ';
			}
		} else {
			/* 実機どおり 16 桁。VG2 Wild Kitten は先頭スペース込み */
			for (int i = 0; i < n && i < 16; ++i)
				win[i] = raw[i];
		}
		LcdText16(dc, text, win, pal.pixel);
	} else if (kind == kKindLaGreen) {
		char line[28];
		char nm[24];
		LcdWcsToUtf(sp.name, nm, 24);
		sprintf_s(line, "P%02d %03d %s", sel + 1, (sp.pc & 127) + 1, nm);
		LcdTextFit(dc, text, line, pal.pixel);
	} else {
		const int px = 1;
		const int py = 1;
		const int gap = 0;
		const int lh = LcdGlyphH(py, gap) + LcdSc(1, dpi);
		int y = text.top;
		char line[40], nm[24], pan[12], chs[12];
		LcdWcsToUtf(sp.name, nm, 24);
		const char portCh = (char)('A' + (sp.port ? 1 : 0));
		if (sp.midiCh >= 16) strcpy_s(chs, "OFF");
		else sprintf_s(chs, "%c%02d", portCh, (sp.midiCh % 16) + 1);
		sprintf_s(line, "%c%02d %03d %s", (char)('A' + bank), sel + 1, (sp.pc & 127) + 1, nm);
		LcdTextLeft(dc, text.left, y, px, py, gap, line, pal.pixel, text.Width());
		y += lh;
		LcdPanStr(sp.pan, pan, 12);
		sprintf_s(line, "LEVEL %3d  PAN %s", sp.vol, pan);
		LcdTextLeft(dc, text.left, y, px, py, gap, line, pal.pixel, text.Width());
		y += lh;
		sprintf_s(line, "REVERB%3d  CHORUS%3d", sp.rev, sp.crs);
		LcdTextLeft(dc, text.left, y, px, py, gap, line, pal.pixel, text.Width());
		y += lh;
		if (y + lh <= text.bottom + 1) {
			const int ks = sp.kshift;
			if (ks < 0) sprintf_s(line, "K.SHIFT-%d  MIDI %s", -ks, chs);
			else sprintf_s(line, "K.SHIFT+%d  MIDI %s", ks, chs);
			LcdTextLeft(dc, text.left, y, px, py, gap, line, pal.pixel, text.Width());
		}
	}
	if (!keys.IsRectEmpty()) {
		const COLORREF offB = RGB(
			(GetRValue(pal.pixel) * 2 + GetRValue(pal.lcd)) / 3,
			(GetGValue(pal.pixel) * 2 + GetGValue(pal.lcd)) / 3,
			(GetBValue(pal.pixel) * 2 + GetBValue(pal.lcd)) / 3);
		LcdDrawKb(dc, keys, keyBits, pal.pixel, pal.pixDim, offB);
	}

	const int pg = st.showPage;
	if (pg >= 1 && pg <= 10 && st.pageOn[pg - 1])
		LcdDrawGs16(dc, bars, st.page[pg - 1], pal.pixel, pal.barEmpty);
	else
		LcdDrawMeters(dc, bars, parts16, sel, pal.pixel, pal.barEmpty, pal.cursor);
}
