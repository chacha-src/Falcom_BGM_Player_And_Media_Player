#include "stdafx.h"
#include "WrdEngine.h"
#include <vector>
#include <algorithm>
#include <math.h>

static unsigned WrdRgb4(unsigned rgb12)
{
	unsigned r = (rgb12 >> 8) & 15;
	unsigned g = (rgb12 >> 4) & 15;
	unsigned b = rgb12 & 15;
	auto exp4 = [](unsigned v) -> unsigned {
		return v ? ((v << 4) | 0x0F) : 0;
	};
	return (exp4(r) << 16) | (exp4(g) << 8) | exp4(b);
}

static COLORREF WrdPc98Color(int attr)
{
	/* PC-98 テキストはデジタル 8 色（+輝度）。中間 170 だとマゼンタが紫に潰れる */
	static const COLORREF pal[16] = {
		RGB(0, 0, 0), RGB(0, 0, 255), RGB(255, 0, 0), RGB(255, 0, 255),
		RGB(0, 255, 0), RGB(0, 255, 255), RGB(255, 255, 0), RGB(255, 255, 255),
		RGB(0, 0, 0), RGB(0, 0, 255), RGB(255, 0, 0), RGB(255, 0, 255),
		RGB(0, 255, 0), RGB(0, 255, 255), RGB(255, 255, 0), RGB(255, 255, 255)
	};
	return pal[attr & 15];
}

static int WrdLineWaitTicks(int wmode, int measT)
{
	/* MIMPI 省略時は @WMODE(11)＝1小節=96 のうち 12 tick＝4/4 の8分。
	   1小節待ちにするとカラオケが約1〜2秒刻みで曲に付かない。 */
	if (measT < 1) measT = 192;
	if (wmode < 0) wmode = 11;
	int t = (wmode + 1) * measT / 96;
	return t < 1 ? 1 : t;
}

static void WrdSjisToWide(const char* s, wchar_t* o, int oN)
{
	if (!o || oN < 2) return;
	o[0] = 0;
	if (!s || !s[0]) return;
	MultiByteToWideChar(932, 0, s, -1, o, oN);
	o[oN - 1] = 0;
}

static int WrdICmpCmd(const char* s, const char* cmd)
{
	while (*cmd) {
		char a = *s++;
		char b = *cmd++;
		if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
		if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
		if (a != b) return 0;
	}
	return *s == 0;
}

static int WrdMeasTicks(int num, int den)
{
	if (num < 1) num = 4;
	if (den < 1) den = 4;
	return 48 * 4 * num / den;
}

void WrdEngineInit(WrdEngine* e)
{
	if (!e) return;
	e->cmds.clear();
	e->tempo.clear();
	MagImageFree(&e->mag);
	e->loaded = 0;
	e->cur = 0;
	e->lastTick = -1;
	e->tsNum = 4;
	e->tsDen = 4;
	e->wmode = 11;
	e->wmodeChar = 0;
	e->offsetMeas = 0;
	e->col40 = 0;
	e->textOn = 1;
	e->gfxOn = 1;
	e->curX = 0;
	e->curY = 0;
	e->saveX = 0;
	e->saveY = 0;
	e->saveColor = 7;
	e->color = 7;
	e->activePage = 0;
	e->dispPage = 0;
	e->gplane = 0;
	e->dir[0] = 0;
	e->wrdPath[0] = 0;
	e->magOk = 0;
	e->lineStyle = 0;
	e->fontMecha = 0;
	e->vsCount = 0;
	e->textDot = 0;
	e->magSearchDir[0] = 0;
	e->fadeActive = 0;
	e->fadeFromBank = e->fadeToBank = 0;
	e->fadeStartTick = e->fadeDurTicks = 0;
	e->smfDiv = 48;
	e->smfOk = 0;
	e->sr = 44100;
	e->usedAbsWait = 0;
	e->usedBs = 0;
	e->karaSynced = 0;
	e->karaNotes.clear();
	e->karaMarks.clear();
	memset(e->cells, 0x20, sizeof(e->cells));
	memset(e->attr, 7, sizeof(e->attr));
	memset(e->gfxPage, 0, sizeof(e->gfxPage));
	memset(e->fadePalFrom, 0, sizeof(e->fadePalFrom));
	memset(e->fadePalTo, 0, sizeof(e->fadePalTo));
	for (int p = 0; p < 20; ++p) {
		static const unsigned defc[16] = {
			0x000000, 0x0000AA, 0xAA0000, 0xAA00AA,
			0x00AA00, 0x00AAAA, 0xAA5500, 0xAAAAAA,
			0x555555, 0x5555FF, 0xFF5555, 0xFF55FF,
			0x55FF55, 0x55FFFF, 0xFFFF55, 0xFFFFFF
		};
		for (int i = 0; i < 16; ++i)
			e->pal[p][i] = defc[i];
	}
}

void WrdEngineFree(WrdEngine* e)
{
	if (!e) return;
	MagImageFree(&e->mag);
	e->cmds.clear();
	e->tempo.clear();
	e->loaded = 0;
}

void WrdEngineResetScreen(WrdEngine* e)
{
	if (!e) return;
	memset(e->cells, 0x20, sizeof(e->cells));
	memset(e->attr, 7, sizeof(e->attr));
	memset(e->gfxPage, 0, sizeof(e->gfxPage));
	e->curX = 0;
	e->curY = 0;
	e->saveX = 0;
	e->saveY = 0;
	e->saveColor = 7;
	e->color = 7;
	e->col40 = 0;
	e->textOn = 1;
	e->gfxOn = 1;
	e->activePage = 0;
	e->dispPage = 0;
	e->gplane = 0;
	e->lineStyle = 0;
	e->fontMecha = 0;
	e->vsCount = 0;
	e->textDot = 0;
	e->fadeActive = 0;
	static const unsigned defc[16] = {
		0x000000, 0x0000AA, 0xAA0000, 0xAA00AA,
		0x00AA00, 0x00AAAA, 0xAA5500, 0xAAAAAA,
		0x555555, 0x5555FF, 0xFF5555, 0xFF55FF,
		0x55FF55, 0x55FFFF, 0xFFFF55, 0xFFFFFF
	};
	for (int p = 0; p < 20; ++p)
		for (int i = 0; i < 16; ++i)
			e->pal[p][i] = defc[i];
}

static int WrdParseInt(const char*& p)
{
	while (*p == ' ' || *p == '\t' || *p == ',' || *p == '<' || *p == '>')
		p++;
	int sign = 1;
	if (*p == '-') { sign = -1; p++; }
	int v = 0, any = 0;
	if (*p == '#') p++;
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (*p - '0');
		p++;
		any = 1;
	}
	(void)any;
	return v * sign;
}

static int WrdParseHex3(const char*& p)
{
	while (*p == ' ' || *p == '\t' || *p == ',' || *p == '<' || *p == '>') p++;
	if (*p == '#') p++;
	auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'F') return 10 + c - 'A';
		if (c >= 'a' && c <= 'f') return 10 + c - 'a';
		return -1;
	};
	int r = hex(*p); if (r < 0) return 0; p++;
	int g = hex(*p); if (g < 0) return (r << 8); p++;
	int b = hex(*p); if (b < 0) return (r << 8) | (g << 4); p++;
	return (r << 8) | (g << 4) | b;
}

static void WrdParseArgs(const char* inside, int* a, int n)
{
	for (int i = 0; i < n; ++i) a[i] = 0;
	const char* p = inside;
	for (int i = 0; i < n && p && *p && *p != ')'; ++i)
		a[i] = WrdParseInt(p);
}

static void WrdExtractParen(const char* s, char* out, int outN, const char** rest)
{
	out[0] = 0;
	if (rest) *rest = s;
	const char* p = s;
	if (*p != '(') {
		if (rest) *rest = s;
		return;
	}
	p++;
	int n = 0;
	int depth = 1;
	while (*p && depth) {
		if (*p == '(') depth++;
		else if (*p == ')') {
			depth--;
			if (!depth) break;
		}
		if (n + 1 < outN)
			out[n++] = *p;
		p++;
	}
	out[n] = 0;
	if (*p == ')') p++;
	if (rest) *rest = p;
}

static int WrdIsSjisLead(unsigned char c)
{
	return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

static int WrdIsSjisTrail(unsigned char c)
{
	return (c >= 0x40 && c <= 0x7E) || (c >= 0x80 && c <= 0xFC);
}

/* PC-98 テキストVRAMは常に80桁。@TON(2)の40桁はCRTの倍角表示で、
   LOCATE 座標は80セルのまま（KIDA078B は TON(2) と TON(1) で同じ x=29 を使う）。 */
static int WrdTextCols(const WrdEngine* e)
{
	(void)e;
	return 80;
}

static void WrdClsText(WrdEngine* e, int x1, int y1, int x2, int y2, int color, int ch);

static void WrdScrollUp(WrdEngine* e)
{
	memmove(e->cells[0], e->cells[1], sizeof(e->cells[0]) * 24);
	memmove(e->attr[0], e->attr[1], sizeof(e->attr[0]) * 24);
	memset(e->cells[24], 0x20, sizeof(e->cells[0]));
	memset(e->attr[24], 7, sizeof(e->attr[0]));
	if (e->saveY > 0) e->saveY--;
}

static void WrdClampCursor(WrdEngine* e)
{
	const int cols = WrdTextCols(e);
	if (e->curX < 0) e->curX = 0;
	if (e->curY < 0) e->curY = 0;
	if (e->curX >= cols) {
		e->curX = 0;
		e->curY++;
	}
	while (e->curY > 24) {
		WrdScrollUp(e);
		e->curY = 24;
	}
}

static const char* WrdDoEsc(WrdEngine* e, const char* s)
{
	if (!s || !*s) return s;
	if (*s == 's' || *s == '7') {
		e->saveX = e->curX;
		e->saveY = e->curY;
		e->saveColor = e->color;
		return s + 1;
	}
	if (*s == 'u' || *s == '8') {
		e->curX = e->saveX;
		e->curY = e->saveY;
		e->color = e->saveColor;
		WrdClampCursor(e);
		return s + 1;
	}
	if (*s != '[') return s;
	s++;
	int n[8] = {};
	int nn = 0;
	int val = 0;
	int got = 0;
	while (*s) {
		if (*s >= '0' && *s <= '9') {
			val = val * 10 + (*s - '0');
			got = 1;
			s++;
			continue;
		}
		if (*s == ';') {
			if (nn < 8) n[nn++] = got ? val : 0;
			val = 0;
			got = 0;
			s++;
			continue;
		}
		break;
	}
	if (nn < 8) n[nn++] = got ? val : 0;
	const char cmd = *s;
	if (cmd) s++;
	switch (cmd) {
	case 'm': {
		static const int ansiPc98[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
		for (int i = 0; i < nn; ++i) {
			const int v = n[i];
			if (v == 0) e->color = 7;
			else if (v == 1) e->color |= 8;
			else if (v == 7) e->color |= 0x10;
			else if (v >= 30 && v <= 37)
				e->color = (e->color & ~7) | ansiPc98[v - 30];
			else if (v >= 90 && v <= 97)
				e->color = 8 | ansiPc98[v - 90];
			else if (v == 39) e->color = (e->color & ~7) | 7;
		}
		break;
	}
	case 's':
		e->saveX = e->curX;
		e->saveY = e->curY;
		e->saveColor = e->color;
		break;
	case 'u':
		e->curX = e->saveX;
		e->curY = e->saveY;
		e->color = e->saveColor;
		WrdClampCursor(e);
		break;
	case 'H':
	case 'f': {
		int row = n[0] > 0 ? n[0] : 1;
		int col = (nn > 1 && n[1] > 0) ? n[1] : 1;
		e->curY = row - 1;
		e->curX = col - 1;
		WrdClampCursor(e);
		break;
	}
	case 'J':
		if (n[0] == 2)
			WrdClsText(e, 1, 1, 80, 25, 0, 32);
		break;
	case 'K': {
		const int cols = WrdTextCols(e);
		int x1 = e->curX + 1;
		if (n[0] == 1) x1 = 1;
		int x2 = (n[0] == 1) ? e->curX + 1 : cols;
		if (n[0] == 2) { x1 = 1; x2 = cols; }
		WrdClsText(e, x1, e->curY + 1, x2, e->curY + 1, e->color, 32);
		break;
	}
	case 'A': e->curY -= n[0] > 0 ? n[0] : 1; WrdClampCursor(e); break;
	case 'B': e->curY += n[0] > 0 ? n[0] : 1; WrdClampCursor(e); break;
	case 'C': e->curX += n[0] > 0 ? n[0] : 1; WrdClampCursor(e); break;
	case 'D': e->curX -= n[0] > 0 ? n[0] : 1; WrdClampCursor(e); break;
	default:
		break;
	}
	return s;
}

static int WrdGlyphBounds(const WrdEngine* e, int y, int x, int* x0, int* x1)
{
	const int cols = WrdTextCols(e);
	if (y < 0 || y > 24 || x < 0 || x >= cols) {
		if (x0) *x0 = x;
		if (x1) *x1 = x;
		return 1;
	}
	int i = 0;
	while (i < cols) {
		unsigned char c = (unsigned char)e->cells[y][i];
		int n = 1;
		if (WrdIsSjisLead(c) && i + 1 < cols
			&& WrdIsSjisTrail((unsigned char)e->cells[y][i + 1]))
			n = 2;
		if (i <= x && x < i + n) {
			if (x0) *x0 = i;
			if (x1) *x1 = i + n - 1;
			return n;
		}
		i += n;
	}
	if (x0) *x0 = x;
	if (x1) *x1 = x;
	return 1;
}

static void WrdSmashCell(WrdEngine* e, int y, int x)
{
	const int cols = WrdTextCols(e);
	if (y < 0 || y > 24 || x < 0 || x >= cols) return;
	const unsigned char at = (unsigned char)(e->color & 0x1F);
	int x0 = x, x1 = x;
	WrdGlyphBounds(e, y, x, &x0, &x1);
	for (int i = x0; i <= x1 && i < cols; ++i) {
		e->cells[y][i] = 0x20;
		e->attr[y][i] = at;
	}
}

static void WrdPutText(WrdEngine* e, const char* sjis)
{
	if (!e || !sjis) return;
	const int cols = WrdTextCols(e);
	while (*sjis) {
		unsigned char c = (unsigned char)*sjis;
		if (c == 0x1B) {
			sjis = WrdDoEsc(e, sjis + 1);
			continue;
		}
		if (c == '\r') { sjis++; continue; }
		if (c == '\n') {
			e->curX = 0;
			e->curY++;
			WrdClampCursor(e);
			sjis++;
			continue;
		}
		const int lead = WrdIsSjisLead(c) && (unsigned char)sjis[1] && WrdIsSjisTrail((unsigned char)sjis[1]);
		if (lead && e->curX >= cols - 1) {
			e->curX = 0;
			e->curY++;
		}
		if (e->curX >= cols) {
			e->curX = 0;
			e->curY++;
		}
		WrdClampCursor(e);
		const int x = e->curX;
		const int y = e->curY;
		WrdSmashCell(e, y, x);
		if (lead)
			WrdSmashCell(e, y, x + 1);
		e->cells[y][x] = (char)c;
		e->attr[y][x] = (unsigned char)(e->color & 0x1F);
		e->curX++;
		sjis++;
		if (lead) {
			e->cells[y][x + 1] = *sjis;
			e->attr[y][x + 1] = (unsigned char)(e->color & 0x1F);
			e->curX++;
			sjis++;
		}
	}
}

static void WrdClsText(WrdEngine* e, int x1, int y1, int x2, int y2, int color, int ch)
{
	if (x1 < 1) x1 = 1;
	if (y1 < 1) y1 = 1;
	if (x2 > 80) x2 = 80;
	if (y2 > 25) y2 = 25;
	if (ch <= 0) ch = 32;
	for (int y = y1; y <= y2; ++y) {
		WrdSmashCell(e, y - 1, x1 - 1);
		WrdSmashCell(e, y - 1, x2 - 1);
		for (int x = x1; x <= x2; ++x) {
			e->cells[y - 1][x - 1] = (char)ch;
			e->attr[y - 1][x - 1] = (unsigned char)color;
		}
	}
}

static void WrdScroll(WrdEngine* e, int x1, int y1, int x2, int y2, int mode, int color, int ch)
{
	if (x1 < 1) x1 = 1;
	if (y1 < 1) y1 = 1;
	if (x2 > 80) x2 = 80;
	if (y2 > 25) y2 = 25;
	if (ch <= 0) ch = 32;
	for (int y = y1; y <= y2; ++y) {
		WrdSmashCell(e, y - 1, x1 - 1);
		WrdSmashCell(e, y - 1, x2 - 1);
	}
	if (mode == 0) {
		for (int y = y1; y < y2; ++y) {
			memcpy(e->cells[y - 1] + (x1 - 1), e->cells[y] + (x1 - 1), (size_t)(x2 - x1 + 1));
			memcpy(e->attr[y - 1] + (x1 - 1), e->attr[y] + (x1 - 1), (size_t)(x2 - x1 + 1));
		}
		for (int x = x1; x <= x2; ++x) {
			e->cells[y2 - 1][x - 1] = (char)ch;
			e->attr[y2 - 1][x - 1] = (unsigned char)color;
		}
	} else if (mode == 1) {
		for (int y = y2; y > y1; --y) {
			memcpy(e->cells[y - 1] + (x1 - 1), e->cells[y - 2] + (x1 - 1), (size_t)(x2 - x1 + 1));
			memcpy(e->attr[y - 1] + (x1 - 1), e->attr[y - 2] + (x1 - 1), (size_t)(x2 - x1 + 1));
		}
		for (int x = x1; x <= x2; ++x) {
			e->cells[y1 - 1][x - 1] = (char)ch;
			e->attr[y1 - 1][x - 1] = (unsigned char)color;
		}
	} else if (mode == 2) {
		for (int y = y1; y <= y2; ++y) {
			if (x2 > x1) {
				memmove(e->cells[y - 1] + x1, e->cells[y - 1] + (x1 - 1), (size_t)(x2 - x1));
				memmove(e->attr[y - 1] + x1, e->attr[y - 1] + (x1 - 1), (size_t)(x2 - x1));
			}
			e->cells[y - 1][x1 - 1] = (char)ch;
			e->attr[y - 1][x1 - 1] = (unsigned char)color;
		}
	} else if (mode == 3) {
		for (int y = y1; y <= y2; ++y) {
			if (x2 > x1) {
				memmove(e->cells[y - 1] + (x1 - 1), e->cells[y - 1] + x1, (size_t)(x2 - x1));
				memmove(e->attr[y - 1] + (x1 - 1), e->attr[y - 1] + x1, (size_t)(x2 - x1));
			}
			e->cells[y - 1][x2 - 1] = (char)ch;
			e->attr[y - 1][x2 - 1] = (unsigned char)color;
		}
	}
}

static int WrdPageOk(int p)
{
	return p >= 0 && p < WRD_PAGE_MAX;
}

static int WrdWriteMask(const WrdEngine* e)
{
	int m = e->gplane & 15;
	return m ? m : 15;
}

static void WrdPlotIdx(WrdEngine* e, int page, int x, int y, int col)
{
	if (!e || !WrdPageOk(page)) return;
	if ((unsigned)x >= 640 || (unsigned)y >= 400) return;
	const int mask = WrdWriteMask(e);
	unsigned char* d = &e->gfxPage[page][y * 640 + x];
	*d = (unsigned char)((*d & ~mask) | (col & mask));
}

static void WrdFillPagePlanes(WrdEngine* e, int page, int planeBits)
{
	if (!e || !WrdPageOk(page)) return;
	int mask = planeBits & 15;
	if (mask == 0) mask = 15;
	unsigned char* p = e->gfxPage[page];
	for (int i = 0; i < 640 * 400; ++i)
		p[i] = (unsigned char)(p[i] & ~mask);
}

static void WrdCopyRect(WrdEngine* e, int sp, int dp,
	int x1, int y1, int x2, int y2, int xd, int yd)
{
	if (!e || !WrdPageOk(sp) || !WrdPageOk(dp)) return;
	if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
	if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
	const int w = x2 - x1 + 1;
	const int h = y2 - y1 + 1;
	if (w < 1 || h < 1) return;
	std::vector<unsigned char> tmp((size_t)w * (size_t)h);
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			int sx = x1 + i, sy = y1 + j;
			unsigned char v = 0;
			if ((unsigned)sx < 640 && (unsigned)sy < 400)
				v = e->gfxPage[sp][sy * 640 + sx];
			tmp[(size_t)j * w + i] = v;
		}
	}
	const int mask = WrdWriteMask(e);
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			int dx = xd + i, dy = yd + j;
			if ((unsigned)dx >= 640 || (unsigned)dy >= 400) continue;
			unsigned char* d = &e->gfxPage[dp][dy * 640 + dx];
			*d = (unsigned char)((*d & ~mask) | (tmp[(size_t)j * w + i] & mask));
		}
	}
}

static void WrdSwapRect(WrdEngine* e, int sp, int dp,
	int x1, int y1, int x2, int y2, int xd, int yd)
{
	if (!e || !WrdPageOk(sp) || !WrdPageOk(dp)) return;
	if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
	if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
	const int w = x2 - x1 + 1;
	const int h = y2 - y1 + 1;
	if (w < 1 || h < 1) return;
	std::vector<unsigned char> a((size_t)w * (size_t)h), b((size_t)w * (size_t)h);
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			int sx = x1 + i, sy = y1 + j;
			int dx = xd + i, dy = yd + j;
			a[(size_t)j * w + i] = ((unsigned)sx < 640 && (unsigned)sy < 400)
				? e->gfxPage[sp][sy * 640 + sx] : 0;
			b[(size_t)j * w + i] = ((unsigned)dx < 640 && (unsigned)dy < 400)
				? e->gfxPage[dp][dy * 640 + dx] : 0;
		}
	}
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			int sx = x1 + i, sy = y1 + j;
			int dx = xd + i, dy = yd + j;
			if ((unsigned)sx < 640 && (unsigned)sy < 400)
				e->gfxPage[sp][sy * 640 + sx] = b[(size_t)j * w + i];
			if ((unsigned)dx < 640 && (unsigned)dy < 400)
				e->gfxPage[dp][dy * 640 + dx] = a[(size_t)j * w + i];
		}
	}
}

static unsigned WrdMixRgb(unsigned from, unsigned to, int step, int maxs)
{
	if (maxs <= 0 || step >= maxs) return to;
	if (step <= 0) return from;
	auto ch = [&](int sh) -> unsigned {
		int a = (int)((from >> sh) & 255);
		int b = (int)((to >> sh) & 255);
		int v = a + (b - a) * step / maxs;
		if (v < 0) v = 0;
		if (v > 255) v = 255;
		return (unsigned)v;
	};
	return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

static void WrdApplyFadeNow(WrdEngine* e, int tick48)
{
	if (!e || !e->fadeActive) return;
	int t = tick48 - e->fadeStartTick;
	if (t < 0) t = 0;
	if (e->fadeDurTicks <= 0 || t >= e->fadeDurTicks) {
		for (int i = 0; i < 16; ++i)
			e->pal[0][i] = e->fadePalTo[i];
		e->fadeActive = 0;
		return;
	}
	for (int i = 0; i < 16; ++i)
		e->pal[0][i] = WrdMixRgb(e->fadePalFrom[i], e->fadePalTo[i], t, e->fadeDurTicks);
}

static int WrdResolveImagePath(const WrdEngine* e, const wchar_t* name, wchar_t* out, int outN)
{
	if (!e || !name || !name[0] || !out || outN < 8) return 0;
	if (name[1] == L':' || name[0] == L'\\' || name[0] == L'/') {
		wcsncpy_s(out, outN, name, _TRUNCATE);
		return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
	}
	const wchar_t* dirs[3] = { e->dir, e->magSearchDir, L"" };
	for (int i = 0; i < 3; ++i) {
		if (i < 2 && (!dirs[i] || !dirs[i][0])) continue;
		if (i < 2)
			_snwprintf_s(out, outN, _TRUNCATE, L"%s%s", dirs[i], name);
		else
			wcsncpy_s(out, outN, name, _TRUNCATE);
		if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES)
			return 1;
	}
	return 0;
}

/* PC-98 PHO: 4 プレーン（B,R,G,E）× 640×400。TiMidity pho_load_pixel と同じ */
static int WrdLoadPhoToPage(WrdEngine* e, const unsigned char* data, unsigned size, int page)
{
	if (!e || !data || !WrdPageOk(page)) return 0;
	unsigned palOff = 0;
	if (size >= 128048 && size < 128000 + 256)
		palOff = 48;
	else if (size >= 128000 + 256 && memcmp(data, "MAKI02  ", 8) != 0 && size < 128000 + 400)
		palOff = (unsigned)(size - 128000);
	if (size < palOff + 128000) return 0;
	if (palOff == 48) {
		for (int i = 0; i < 16; ++i) {
			unsigned g = data[i * 3 + 0], r = data[i * 3 + 1], b = data[i * 3 + 2];
			if (g <= 15 && r <= 15 && b <= 15) {
				r = r ? ((r << 4) | 0x0F) : 0;
				g = g ? ((g << 4) | 0x0F) : 0;
				b = b ? ((b << 4) | 0x0F) : 0;
			}
			e->pal[0][i] = (r << 16) | (g << 8) | b;
			e->pal[17][i] = e->pal[0][i];
		}
	}
	const unsigned char* pl = data + palOff;
	static const int shift[4] = { 0, 2, 1, 3 }; /* B,R,G,E */
	memset(e->gfxPage[page], 0, 640 * 400);
	for (int plane = 0; plane < 4; ++plane) {
		const unsigned char* src = pl + plane * (640 / 8 * 400);
		const unsigned char bitv = (unsigned char)(1 << shift[plane]);
		for (int y = 0; y < 400; ++y) {
			for (int xb = 0; xb < 80; ++xb) {
				unsigned char by = src[y * 80 + xb];
				for (int k = 0; k < 8; ++k) {
					if (by & (0x80 >> k))
						e->gfxPage[page][y * 640 + xb * 8 + k] |= bitv;
				}
			}
		}
	}
	return 1;
}

static void WrdBlitMag(WrdEngine* e, const MagImage* mag, int x, int y, int scale, int palMode);

static int WrdLoadImageFile(WrdEngine* e, const wchar_t* name, int page, int palMode)
{
	if (!e || !name || !name[0]) return 0;
	if (!WrdPageOk(page)) page = e->activePage;
	wchar_t full[MAX_PATH];
	if (!WrdResolveImagePath(e, name, full, MAX_PATH))
		return 0;
	HANDLE f = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 16 || size > 16 * 1024 * 1024) { CloseHandle(f); return 0; }
	std::vector<unsigned char> buf(size);
	const BOOL ok = ReadFile(f, buf.data(), size, &got, NULL);
	CloseHandle(f);
	if (!ok || got != size) return 0;
	if (size >= 8 && memcmp(buf.data(), "MAKI02  ", 8) == 0) {
		MagImage mag = {};
		if (!MagImageLoadMem(buf.data(), size, &mag)) return 0;
		const int savePage = e->activePage;
		e->activePage = page;
		WrdBlitMag(e, &mag, mag.x0, mag.y0, 1, palMode);
		e->activePage = savePage;
		MagImageFree(&mag);
		return 1;
	}
	return WrdLoadPhoToPage(e, buf.data(), size, page);
}

static void WrdGLine(WrdEngine* e, int x1, int y1, int x2, int y2, int col, int sw, int fill)
{
	auto plot = [&](int x, int y, int c) {
		WrdPlotIdx(e, e->activePage, x, y, c);
	};
	if (sw == 0) {
		int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
		int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
		int err = dx + dy, x = x1, y = y1;
		for (;;) {
			plot(x, y, col);
			if (x == x2 && y == y2) break;
			int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x += sx; }
			if (e2 <= dx) { err += dx; y += sy; }
		}
	} else {
		int xa = x1 < x2 ? x1 : x2, xb = x1 < x2 ? x2 : x1;
		int ya = y1 < y2 ? y1 : y2, yb = y1 < y2 ? y2 : y1;
		if (sw == 2) {
			const int c = (fill ? fill : col) & 15;
			for (int y = ya; y <= yb; ++y)
				for (int x = xa; x <= xb; ++x)
					plot(x, y, c);
		} else {
			WrdGLine(e, xa, ya, xb, ya, col, 0, 0);
			WrdGLine(e, xa, yb, xb, yb, col, 0, 0);
			WrdGLine(e, xa, ya, xa, yb, col, 0, 0);
			WrdGLine(e, xb, ya, xb, yb, col, 0, 0);
		}
	}
}

static void WrdGCircle(WrdEngine* e, int cx, int cy, int r, int col, int sw, int fill)
{
	if (r < 1) r = 1;
	int x = r, y = 0, err = 0;
	auto plot = [&](int px, int py) {
		WrdPlotIdx(e, e->activePage, px, py, col);
	};
	while (x >= y) {
		plot(cx + x, cy + y); plot(cx + y, cy + x);
		plot(cx - y, cy + x); plot(cx - x, cy + y);
		plot(cx - x, cy - y); plot(cx - y, cy - x);
		plot(cx + y, cy - x); plot(cx + x, cy - y);
		y++;
		if (err <= 0) { err += 2 * y + 1; }
		if (err > 0) { x--; err -= 2 * x + 1; }
	}
	if (sw == 2) {
		const int c = (fill ? fill : col) & 15;
		for (int yy = -r; yy <= r; ++yy) {
			int w2 = (int)sqrt((double)(r * r - yy * yy));
			for (int xx = -w2; xx <= w2; ++xx)
				WrdPlotIdx(e, e->activePage, cx + xx, cy + yy, c);
		}
	}
}

static void WrdBlitMag(WrdEngine* e, const MagImage* mag, int x, int y, int scale, int palMode)
{
	if (!e || !mag || !mag->px) return;
	if (scale < 1) scale = 1;
	if (palMode == 0 || palMode == 2) {
		for (int i = 0; i < 16 && i < mag->nCol; ++i) {
			e->pal[17][i] = mag->pal[i];
			e->pal[0][i] = mag->pal[i];
		}
	}
	if (palMode == 2) return;
	const int page = e->activePage;
	for (int j = 0; j < mag->h; ++j) {
		int dy = y + j / scale;
		if ((unsigned)dy >= 400) continue;
		for (int i = 0; i < mag->w; ++i) {
			int dx = x + i / scale;
			if ((unsigned)dx >= 640) continue;
			WrdPlotIdx(e, page, dx, dy, (int)(mag->px[j * mag->w + i] & 15));
		}
	}
}

static unsigned char WrdGetIdx(const WrdEngine* e, int page, int x, int y)
{
	if (!e || !WrdPageOk(page) || (unsigned)x >= 640 || (unsigned)y >= 400) return 0;
	return e->gfxPage[page][y * 640 + x];
}

static void WrdPutIdx(WrdEngine* e, int page, int x, int y, unsigned char v)
{
	if (!e || !WrdPageOk(page) || (unsigned)x >= 640 || (unsigned)y >= 400) return;
	e->gfxPage[page][y * 640 + x] = v;
}

/* ^XCOPY。TMIDI は method=0,1,8,10。TiMidity x_XCopy は 0–10 全部 */
static void WrdXCopy(WrdEngine* e, const int* a, int narg)
{
	if (!e || !a || narg < 9) return;
	int sx1 = a[0], sy1 = a[1], sx2 = a[2], sy2 = a[3];
	int tx = a[4], ty = a[5], ss = a[6], ts = a[7], method = a[8];
	const int* opt = a + 9;
	const int nopt = narg - 9;
	if (sx2 < sx1) { int t = sx1; sx1 = sx2; sx2 = t; }
	if (sy2 < sy1) { int t = sy1; sy1 = sy2; sy2 = t; }
	const int w = sx2 - sx1 + 1;
	const int h = sy2 - sy1 + 1;
	if (w < 1 || h < 1 || w > 640 || h > 400) return;
	if (!WrdPageOk(ss) || !WrdPageOk(ts)) return;
	std::vector<unsigned char> src((size_t)w * (size_t)h), dst((size_t)w * (size_t)h);
	auto grab = [&](int page, int x0, int y0, std::vector<unsigned char>& o) {
		for (int j = 0; j < h; ++j)
			for (int i = 0; i < w; ++i)
				o[(size_t)j * w + i] = WrdGetIdx(e, page, x0 + i, y0 + j);
	};
	auto store = [&](int page, int x0, int y0, const std::vector<unsigned char>& o) {
		for (int j = 0; j < h; ++j)
			for (int i = 0; i < w; ++i)
				WrdPutIdx(e, page, x0 + i, y0 + j, o[(size_t)j * w + i]);
	};
	grab(ss, sx1, sy1, src);
	grab(ts, tx, ty, dst);
	switch (method) {
	case 1: /* パレット 0 以外をコピー */
		for (int i = 0; i < w * h; ++i)
			if (src[i] != 0) dst[i] = src[i];
		store(ts, tx, ty, dst);
		break;
	case 2: /* XOR */
		for (int i = 0; i < w * h; ++i) dst[i] ^= src[i];
		store(ts, tx, ty, dst);
		break;
	case 3: /* AND */
		for (int i = 0; i < w * h; ++i) dst[i] &= src[i];
		store(ts, tx, ty, dst);
		break;
	case 4: /* OR */
		for (int i = 0; i < w * h; ++i) dst[i] |= src[i];
		store(ts, tx, ty, dst);
		break;
	case 5: /* 左右反転 */
		for (int j = 0; j < h; ++j)
			for (int i = 0; i < w / 2; ++i) {
				unsigned char t = src[(size_t)j * w + i];
				src[(size_t)j * w + i] = src[(size_t)j * w + (w - 1 - i)];
				src[(size_t)j * w + (w - 1 - i)] = t;
			}
		store(ts, tx, ty, src);
		break;
	case 6: /* 上下反転 */
		for (int j = 0; j < h / 2; ++j)
			for (int i = 0; i < w; ++i) {
				unsigned char t = src[(size_t)j * w + i];
				src[(size_t)j * w + i] = src[(size_t)(h - 1 - j) * w + i];
				src[(size_t)(h - 1 - j) * w + i] = t;
			}
		store(ts, tx, ty, src);
		break;
	case 7: /* 180 度 */
		for (int i = 0, k = w * h - 1; i < k; ++i, --k) {
			unsigned char t = src[i]; src[i] = src[k]; src[k] = t;
		}
		store(ts, tx, ty, src);
		break;
	case 8: /* パレット 0 以外。マスク元座標は opt[0],opt[1] */
		if (nopt < 2) break;
		grab(ts, opt[0], opt[1], dst);
		for (int i = 0; i < w * h; ++i)
			if (src[i] != 0) dst[i] = src[i];
		store(ts, tx, ty, dst);
		break;
	case 9: { /* マスクコピー opt[0..3]=4ラインのビット、opt[4]=色 */
		if (nopt < 5) break;
		const int fillc = opt[4];
		for (int j = 0; j < h; ++j) {
			int m = opt[j & 3] & 0xff;
			for (int i = 0; i < w; ++i) {
				if ((1 << (i & 7)) & m) {
					if (fillc == 16) continue;
					dst[(size_t)j * w + i] = (unsigned char)(fillc & 15);
				} else {
					dst[(size_t)j * w + i] = src[(size_t)j * w + i];
				}
			}
		}
		store(ts, tx, ty, dst);
		break;
	}
	case 10: { /* ラインコピー: opt[0] 行コピー、opt[1] 行スキップ */
		if (nopt < 2) break;
		int cp = opt[0], sk = opt[1];
		if (cp < 0 || sk < 0 || cp + sk == 0) break;
		grab(ts, tx, ty, dst);
		int y = 0;
		while (y < h) {
			for (int k = 0; k < cp && y < h; ++k, ++y)
				memcpy(&dst[(size_t)y * w], &src[(size_t)y * w], (size_t)w);
			y += sk;
		}
		store(ts, tx, ty, dst);
		break;
	}
	default:
	case 0:
		WrdCopyRect(e, ss, ts, sx1, sy1, sx2, sy2, tx, ty);
		break;
	}
}

static int WrdKindFromName(const char* name)
{
	if (WrdICmpCmd(name, "WAIT") || WrdICmpCmd(name, "INKEY")) return WRD_WAIT;
	if (WrdICmpCmd(name, "REST")) return WRD_WAIT;
	if (WrdICmpCmd(name, "LOCATE")) return WRD_LOCATE;
	if (WrdICmpCmd(name, "COLOR")) return WRD_COLOR;
	if (WrdICmpCmd(name, "TON")) return WRD_TON;
	if (WrdICmpCmd(name, "ESC")) return WRD_ESC;
	if (WrdICmpCmd(name, "TCLS")) return WRD_TCLS;
	if (WrdICmpCmd(name, "SCROLL")) return WRD_SCROLL;
	if (WrdICmpCmd(name, "GINIT")) return WRD_GINIT;
	if (WrdICmpCmd(name, "GCLS")) return WRD_GCLS;
	if (WrdICmpCmd(name, "GSCREEN")) return WRD_GSCREEN;
	if (WrdICmpCmd(name, "GON")) return WRD_GON;
	if (WrdICmpCmd(name, "GLINE")) return WRD_GLINE;
	if (WrdICmpCmd(name, "GCIRCLE")) return WRD_GCIRCLE;
	if (WrdICmpCmd(name, "PALREV")) return WRD_PALREV;
	if (WrdICmpCmd(name, "PALCHG")) return WRD_PALCHG;
	if (WrdICmpCmd(name, "PAL")) return WRD_PAL;
	if (WrdICmpCmd(name, "FADE")) return WRD_FADE;
	if (WrdICmpCmd(name, "GMODE")) return WRD_GMODE;
	if (WrdICmpCmd(name, "GMOVE")) return WRD_GMOVE;
	if (WrdICmpCmd(name, "MAG")) return WRD_MAG;
	if (WrdICmpCmd(name, "WMODE")) return WRD_WMODE;
	if (WrdICmpCmd(name, "END")) return WRD_END;
	if (WrdICmpCmd(name, "PLOAD")) return WRD_PLOAD;
	if (WrdICmpCmd(name, "PATH")) return WRD_PATH;
	if (WrdICmpCmd(name, "EXEC")) return WRD_EXEC;
	if (WrdICmpCmd(name, "REM") || WrdICmpCmd(name, "REMARK")) return WRD_REM;
	if (WrdICmpCmd(name, "STARTUP")) return WRD_STARTUP;
	if (WrdICmpCmd(name, "STOP")) return WRD_STOP;
	if (WrdICmpCmd(name, "MIDI")) return WRD_MIDI;
	if (WrdICmpCmd(name, "LOOP")) return WRD_LOOP;
	if (WrdICmpCmd(name, "SCREEN")) return WRD_SCREEN;
	if (WrdICmpCmd(name, "OFFSET")) return WRD_NOP; /* パース時に消費 */
	if (WrdICmpCmd(name, "FONTM")) return WRD_FONTM;
	if (WrdICmpCmd(name, "FONTP")) return WRD_FONTP;
	if (WrdICmpCmd(name, "FONTR")) return WRD_FONTR;
	if (WrdICmpCmd(name, "XCOPY")) return WRD_XCOPY;
	if (WrdICmpCmd(name, "VCOPY")) return WRD_VCOPY;
	if (WrdICmpCmd(name, "VSRES")) return WRD_VSRES;
	if (WrdICmpCmd(name, "VSGET")) return WRD_VSGET;
	if (WrdICmpCmd(name, "GSC")) return WRD_EGSC;
	if (WrdICmpCmd(name, "LINE")) return WRD_ELINE;
	if (WrdICmpCmd(name, "TEXTDOT")) return WRD_ETEXTDOT;
	if (WrdICmpCmd(name, "TMODE")) return WRD_ETMODE;
	if (WrdICmpCmd(name, "TSCRL")) return WRD_ETSCRL;
	if (WrdICmpCmd(name, "REGSAVE")) return WRD_EREGSAVE;
	return WRD_NOP;
}

static int WrdIsCaretCommand(const char* name)
{
	int k = WrdKindFromName(name);
	return k == WRD_FONTM || k == WRD_FONTP || k == WRD_FONTR
		|| k == WRD_XCOPY || k == WRD_VCOPY || k == WRD_VSRES || k == WRD_VSGET
		|| k == WRD_EPAL || k == WRD_PAL || k == WRD_EGSC || k == WRD_ELINE
		|| k == WRD_ETEXTDOT || k == WRD_ETMODE || k == WRD_ETSCRL
		|| k == WRD_EREGSAVE || k == WRD_ESCROLL || k == WRD_SCROLL;
}

static int WrdPeekCaretCommand(const char* q)
{
	if (!q || *q != '^') return 0;
	q++;
	char name[32];
	int ni = 0;
	while (*q && ni < 30 && ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z')
		|| (*q >= '0' && *q <= '9')))
		name[ni++] = *q++;
	name[ni] = 0;
	return ni > 0 && WrdIsCaretCommand(name);
}

static void WrdStartFade(WrdEngine* e, int p1, int p2, int speed, int tick)
{
	if (!e) return;
	if (p1 < 0 || p1 > 19) p1 = 0;
	if (p2 < 0 || p2 > 19) p2 = 0;
	if (speed < 1) {
		for (int i = 0; i < 16; ++i)
			e->pal[0][i] = e->pal[p2][i];
		e->fadeActive = 0;
		return;
	}
	e->fadeActive = 1;
	e->fadeFromBank = p1;
	e->fadeToBank = p2;
	e->fadeStartTick = tick;
	e->fadeDurTicks = speed;
	for (int i = 0; i < 16; ++i) {
		e->fadePalFrom[i] = e->pal[p1][i];
		e->fadePalTo[i] = e->pal[p2][i];
		e->pal[0][i] = e->fadePalFrom[i];
	}
}

static void WrdApplyPalText(WrdEngine* e, const char* args)
{
	const char* p = args ? args : "";
	int bank = 0;
	if (*p == '#') {
		p++;
		bank = WrdParseInt(p);
	}
	if (bank < 0 || bank > 19) bank = 0;
	for (int i = 0; i < 16; ++i) {
		if (!*p) break;
		int rgb = WrdParseHex3(p);
		e->pal[bank][i] = WrdRgb4((unsigned)rgb);
	}
	if (bank == 0)
		e->fadeActive = 0;
}

static void WrdExecVisual(WrdEngine* e, const char* args)
{
	if (!e || !args) return;
	int page = e->activePage;
	int gon = -1;
	char fn[200] = {};
	const char* p = args;
	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;
		if (p[0] == '-' && (p[1] == 'g' || p[1] == 'G')) {
			p += 2;
			page = WrdParseInt(p);
			continue;
		}
		if (p[0] == '-' && (p[1] == 'd' || p[1] == 'D')) {
			p += 2;
			while (*p == ' ' || *p == '\t') p++;
			if (_strnicmp(p, "off", 3) == 0) { gon = 0; p += 3; }
			else if (_strnicmp(p, "on", 2) == 0) { gon = 1; p += 2; }
			continue;
		}
		if (*p == '-') {
			while (*p && *p != ' ' && *p != '\t') p++;
			continue;
		}
		int fi = 0;
		while (*p && *p != ' ' && *p != '\t' && fi < 198)
			fn[fi++] = *p++;
		fn[fi] = 0;
	}
	if (gon >= 0) e->gfxOn = gon;
	if (!fn[0]) return;
	wchar_t wfn[MAX_PATH];
	WrdSjisToWide(fn, wfn, MAX_PATH);
	const wchar_t* ext = wcsrchr(wfn, L'.');
	int isImg = 0;
	if (ext && (_wcsicmp(ext, L".MAG") == 0 || _wcsicmp(ext, L".PHO") == 0
		|| _wcsicmp(ext, L".KDD") == 0 || _wcsicmp(ext, L".PI") == 0))
		isImg = 1;
	if (!isImg && ext && (_wcsicmp(ext, L".EXE") == 0 || _wcsicmp(ext, L".COM") == 0)) {
		wchar_t stem[MAX_PATH];
		wcsncpy_s(stem, wfn, _TRUNCATE);
		wchar_t* d = wcsrchr(stem, L'.');
		if (d) {
			wcscpy_s(d, MAX_PATH - (d - stem), L".MAG");
			if (WrdLoadImageFile(e, stem, page, 0)) return;
			wcscpy_s(d, MAX_PATH - (d - stem), L".PHO");
			WrdLoadImageFile(e, stem, page, 0);
		}
		return;
	}
	if (isImg)
		WrdLoadImageFile(e, wfn, page, 0);
}

static void WrdApplyCmd(WrdEngine* e, const WrdCmd& c)
{
	switch (c.kind) {
	case WRD_LOCATE:
		if (c.g) { /* semicolon form y;x */
			e->curY = c.a - 1;
			e->curX = c.b - 1;
		} else {
			e->curX = c.a - 1;
			e->curY = c.b - 1;
		}
		if (e->curX < 0) e->curX = 0;
		if (e->curY < 0) e->curY = 0;
		WrdClampCursor(e);
		/* TMIDI: LOCATE はカーソル移動のみ。白プレビューの上に色を順に重ねる */
		break;
	case WRD_COLOR: {
		if (c.a >= 30) {
			char esc[24];
			_snprintf_s(esc, _TRUNCATE, "[%dm", c.a);
			WrdDoEsc(e, esc);
		} else {
			e->color = c.a;
		}
		break;
	}
	case WRD_TON:
		e->textOn = (c.a != 0);
		e->col40 = (c.a == 2);
		break;
	case WRD_ESC: {
		char buf[260];
		buf[0] = 0x1B;
		const char* es = c.text;
		if (es[0] == '[') {
			strncpy_s(buf + 1, 259, es, _TRUNCATE);
		} else {
			buf[1] = '[';
			strncpy_s(buf + 2, 258, es, _TRUNCATE);
		}
		WrdPutText(e, buf);
		break;
	}
	case WRD_TCLS:
		WrdClsText(e, c.a ? c.a : 1, c.b ? c.b : 1, c.c ? c.c : 80, c.d ? c.d : 25, c.e, c.f ? c.f : 32);
		break;
	case WRD_SCROLL:
		WrdScroll(e, c.a ? c.a : 1, c.b ? c.b : 1, c.c ? c.c : 80, c.d ? c.d : 25, c.e, c.f, c.g ? c.g : 32);
		break;
	case WRD_GINIT:
		memset(e->gfxPage, 0, sizeof(e->gfxPage));
		e->activePage = e->dispPage = 0;
		e->gfxOn = 1;
		e->gplane = 0;
		break;
	case WRD_GCLS:
		WrdFillPagePlanes(e, e->activePage, c.a);
		break;
	case WRD_GSCREEN:
		if (WrdPageOk(c.a)) e->activePage = c.a;
		if (WrdPageOk(c.b)) e->dispPage = c.b;
		break;
	case WRD_GON:
		e->gfxOn = (c.a != 0);
		break;
	case WRD_GMODE:
		e->gplane = c.a;
		break;
	case WRD_GLINE:
		WrdGLine(e, c.a, c.b, c.c, c.d, c.e, c.f, c.g);
		break;
	case WRD_GCIRCLE:
		WrdGCircle(e, c.a, c.b, c.c, c.d, c.e, c.f);
		break;
	case WRD_PAL:
		WrdApplyPalText(e, c.text);
		break;
	case WRD_PALCHG:
		if (c.path[0])
			WrdLoadImageFile(e, c.path, e->activePage, 2);
		else
			WrdApplyPalText(e, c.text);
		break;
	case WRD_PALREV: {
		int bank = c.a;
		if (bank < 0 || bank > 19) bank = 0;
		for (int i = 0; i < 16; ++i) {
			unsigned v = e->pal[bank][i];
			e->pal[bank][i] = (0xFFFFFFu ^ v) & 0xFFFFFFu;
		}
		if (bank == 0)
			e->fadeActive = 0;
		break;
	}
	case WRD_FADE:
		WrdStartFade(e, c.a, c.b, c.narg >= 3 ? c.c : 1, c.tick48);
		break;
	case WRD_MAG: {
		wchar_t full[MAX_PATH];
		if (!WrdResolveImagePath(e, c.path, full, MAX_PATH))
			_snwprintf_s(full, _TRUNCATE, L"%s%s", e->dir, c.path);
		MagImageFree(&e->mag);
		e->magOk = MagImageLoadPath(full, &e->mag);
		if (e->magOk) {
			int x = c.a, y = c.b;
			if (c.narg < 2) {
				x = e->mag.x0;
				y = e->mag.y0;
			}
			WrdBlitMag(e, &e->mag, x, y, c.c > 0 ? c.c : 1, c.d);
		}
		break;
	}
	case WRD_PLOAD:
		WrdLoadImageFile(e, c.path[0] ? c.path : NULL, e->activePage, 0);
		if (!c.path[0] && c.text[0]) {
			wchar_t wfn[MAX_PATH];
			WrdSjisToWide(c.text, wfn, MAX_PATH);
			WrdLoadImageFile(e, wfn, e->activePage, 0);
		}
		break;
	case WRD_PATH:
		if (c.path[0]) {
			wcsncpy_s(e->magSearchDir, c.path, _TRUNCATE);
			size_t n = wcslen(e->magSearchDir);
			if (n > 0 && e->magSearchDir[n - 1] != L'\\' && e->magSearchDir[n - 1] != L'/')
				wcsncat_s(e->magSearchDir, L"\\", _TRUNCATE);
		} else {
			e->magSearchDir[0] = 0;
		}
		break;
	case WRD_EXEC:
		WrdExecVisual(e, c.text);
		break;
	case WRD_GMOVE: {
		int vs = c.narg >= 7 ? c.arg[6] : e->activePage;
		int vd = c.narg >= 8 ? c.arg[7] : e->activePage;
		int sw = c.narg >= 9 ? c.arg[8] : 0;
		if (sw == 1)
			WrdSwapRect(e, vs, vd, c.a, c.b, c.c, c.d, c.e, c.f);
		else
			WrdCopyRect(e, vs, vd, c.a, c.b, c.c, c.d, c.e, c.f);
		break;
	}
	case WRD_XCOPY:
		WrdXCopy(e, c.arg, c.narg);
		break;
	case WRD_VCOPY: {
		int sx1 = c.arg[0], sy1 = c.arg[1], sx2 = c.arg[2], sy2 = c.arg[3];
		int tx = c.arg[4], ty = c.arg[5], ss = c.arg[6], ts = c.arg[7], mode = c.arg[8];
		int vpg = (mode != 0) ? ss : ts;
		int rpg = (mode != 0) ? ts : ss;
		int sp = 2 + vpg;
		int dp = rpg;
		if (mode == 0) { int t = sp; sp = dp; dp = t; }
		if (!WrdPageOk(sp) || !WrdPageOk(dp)) break;
		sx1 &= ~7; /* TMIDI: VCOPY の X は 8 ドット境界 */
		WrdCopyRect(e, sp, dp, sx1, sy1, sx2, sy2, tx, ty);
		break;
	}
	case WRD_VSGET:
		e->vsCount = c.a;
		if (e->vsCount < 0) e->vsCount = 0;
		if (e->vsCount > WRD_PAGE_MAX - 2) e->vsCount = WRD_PAGE_MAX - 2;
		for (int p = 2; p < 2 + e->vsCount && p < WRD_PAGE_MAX; ++p)
			memset(e->gfxPage[p], 0, 640 * 400);
		break;
	case WRD_VSRES:
		e->vsCount = 0;
		for (int p = 2; p < WRD_PAGE_MAX; ++p)
			memset(e->gfxPage[p], 0, 640 * 400);
		break;
	case WRD_FONTM:
		e->fontMecha = (c.a != 0);
		break;
	case WRD_FONTP:
	case WRD_FONTR:
		e->fontMecha = 1;
		break;
	case WRD_EPAL:
		if (c.a >= 0 && c.a < 20 && c.b >= 0 && c.b < 20) {
			for (int i = 0; i < 16; ++i)
				e->pal[c.b][i] = e->pal[c.a][i];
			if (c.b == 0) e->fadeActive = 0;
		}
		break;
	case WRD_EGSC:
		if (WrdPageOk(c.a)) e->dispPage = c.a;
		break;
	case WRD_ELINE:
		e->lineStyle = c.a;
		break;
	case WRD_ESCROLL: {
		int dx = c.a, dy = c.b;
		int page = e->dispPage;
		if (!WrdPageOk(page)) break;
		std::vector<unsigned char> tmp(640 * 400);
		memcpy(tmp.data(), e->gfxPage[page], 640 * 400);
		memset(e->gfxPage[page], 0, 640 * 400);
		for (int y = 0; y < 400; ++y) {
			int sy = y - dy;
			if ((unsigned)sy >= 400) continue;
			for (int x = 0; x < 640; ++x) {
				int sx = x - dx;
				if ((unsigned)sx >= 640) continue;
				e->gfxPage[page][y * 640 + x] = tmp[sy * 640 + sx];
			}
		}
		break;
	}
	case WRD_ETEXTDOT:
		e->textDot = (c.a != 0);
		break;
	case WRD_ETMODE:
		e->col40 = (c.a == 2);
		e->textOn = (c.a != 0);
		break;
	case WRD_ETSCRL:
		WrdScroll(e, 1, 1, 80, 25, c.a >= 0 ? 0 : 1, e->color, 32);
		break;
	case WRD_EREGSAVE:
		if (c.narg >= 2 && c.a >= 0 && c.a < 20 && c.b >= 0 && c.b < 20) {
			for (int i = 0; i < 16; ++i)
				e->pal[c.b][i] = e->pal[c.a][i];
		}
		break;
	case WRD_TEXT:
		WrdPutText(e, c.text);
		if (e->textDot && c.text[0] && c.text[0] != '\n') {
			/* 文字セルをグラフィックにも塗る（演出君メカ TEXTDOT） */
			const int x = e->curX * 8;
			const int y = e->curY * 16;
			for (int j = 0; j < 16; ++j)
				for (int i = 0; i < 8; ++i)
					WrdPlotIdx(e, e->activePage, x + i, y + j, e->color & 15);
		}
		break;
	case WRD_WMODE:
		e->wmode = c.a;
		e->wmodeChar = c.b;
		break;
	case WRD_STARTUP:
		WrdEngineResetScreen(e);
		break;
	case WRD_STOP:
	case WRD_END:
	case WRD_REM:
	case WRD_MIDI:
	case WRD_LOOP:
	case WRD_SCREEN:
		break;
	default:
		break;
	}
}

static void WrdPush(WrdEngine* e, const WrdCmd& c)
{
	e->cmds.push_back(c);
}

static void WrdPushTextCmd(WrdEngine* e, int tick, const char* tbuf, int g)
{
	if (!e || !tbuf || !tbuf[0]) return;
	WrdCmd c = {};
	c.tick48 = tick;
	c.kind = WRD_TEXT;
	c.g = g;
	strncpy_s(c.text, tbuf, _TRUNCATE);
	WrdSjisToWide(tbuf, c.wtext, 128);
	WrdPush(e, c);
}

static int WrdLineIsWaitish(int kind)
{
	return kind == WRD_WAIT || kind == WRD_END;
}

int WrdEngineLoad(WrdEngine* e, const wchar_t* wrdPath)
{
	if (!e || !wrdPath || !wrdPath[0]) return 0;
	const int keepTsN = e->tsNum, keepTsD = e->tsDen;
	const int keepSmf = e->smfOk, keepDiv = e->smfDiv, keepSr = e->sr;
	std::vector<WrdEngine::TempoPt> keepTempo = std::move(e->tempo);
	std::vector<int> keepNotes = std::move(e->karaNotes);
	std::vector<WrdEngine::KaraMark> keepMarks = std::move(e->karaMarks);
	WrdEngineFree(e);
	WrdEngineInit(e);
	if (keepSmf) {
		e->tsNum = keepTsN;
		e->tsDen = keepTsD > 0 ? keepTsD : 4;
		e->smfOk = 1;
		e->smfDiv = keepDiv > 0 ? keepDiv : 48;
		e->sr = keepSr > 0 ? keepSr : 44100;
		e->tempo = std::move(keepTempo);
		e->karaNotes = std::move(keepNotes);
		e->karaMarks = std::move(keepMarks);
	}
	wcsncpy_s(e->wrdPath, wrdPath, _TRUNCATE);
	wcsncpy_s(e->dir, wrdPath, _TRUNCATE);
	wchar_t* sl = wcsrchr(e->dir, L'\\');
	if (!sl) sl = wcsrchr(e->dir, L'/');
	if (sl) sl[1] = 0;
	else e->dir[0] = 0;

	HANDLE f = CreateFileW(wrdPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 1 || size > 4 * 1024 * 1024) { CloseHandle(f); return 0; }
	std::vector<char> raw(size + 2);
	if (!ReadFile(f, raw.data(), size, &got, NULL)) { CloseHandle(f); return 0; }
	CloseHandle(f);
	raw[got] = 0;
	for (DWORD i = 0; i < got; ++i) {
		if ((unsigned char)raw[i] == 0x1A) {
			raw[i] = 0;
			break;
		}
	}

	int tick = 0;
	int wmode = 11;
	int wmodeChar = 0;
	int measT = WrdMeasTicks(e->tsNum, e->tsDen);
	int offset = 0;
	int inStartup = 0;
	int prevEndedBs = 0;
	char* p = raw.data();
	while (*p) {
		char* lineEnd = p;
		while (*lineEnd && *lineEnd != '\n' && *lineEnd != '\r')
			lineEnd++;
		char save = *lineEnd;
		*lineEnd = 0;
		char* line = p;

		int sawWait = 0;
		int sawText = 0;
		int sawBareAt = 0;
		int sawSemiCont = 0;
		int charStepped = 0;
		int isPreview = 0;
		for (const char* s = line; *s; ++s) {
			if ((unsigned char)*s == 0x1B && (s[1] == 's' || s[1] == '7'
				|| (s[1] == '[' && (s[2] == 's' || s[2] == '7'))))
				isPreview = 1;
		}
		if (isPreview && prevEndedBs && !inStartup) {
			WrdCmd cr = {};
			cr.tick48 = tick;
			cr.kind = WRD_TEXT;
			cr.text[0] = '\n';
			cr.g = 3;
			WrdPush(e, cr);
		}
		const char* q = line;
		const char* peek = line;
		while (*peek == ' ' || *peek == '\t') peek++;
		if (*peek == '*' || *peek == '\'') {
			/* comment */
		} else {
			/* 行頭スペースは歌詞（消去・桁揃え）。コメント判定だけ peek する */
			while (*q) {
				if (*q == ';') {
					sawSemiCont = 1;
					q++;
					continue;
				}
				/* @ コマンドの間の空白だけ飛ばす。; 前の空白は画面に出す */
				if (*q == ' ' || *q == '\t') {
					const char* nsp = q;
					while (*nsp == ' ' || *nsp == '\t') nsp++;
					if (*nsp == '@' || (*nsp == '^' && WrdPeekCaretCommand(nsp))) {
						q = nsp;
						continue;
					}
				}
				if (!*q) break;
				if (*q == '@' || (*q == '^' && WrdPeekCaretCommand(q))) {
					const int caret = (*q == '^');
					q++;
					if (!*q || *q == ';' || *q == ' ' || *q == '\t') {
						sawBareAt = 1;
						if (*q) q++;
						continue;
					}
					char name[32];
					int ni = 0;
					while (*q && ni < 30 && ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z')
						|| (*q >= '0' && *q <= '9'))) {
						name[ni++] = *q++;
					}
					name[ni] = 0;
					char args[512];
					const char* rest = q;
					WrdExtractParen(q, args, 512, &rest);
					q = rest;
					if (WrdICmpCmd(name, "STARTUP")) {
						inStartup = 1;
						WrdCmd su = {};
						su.tick48 = 0;
						su.kind = WRD_STARTUP;
						const char* ap = args;
						su.a = WrdParseInt(ap);
						WrdPush(e, su);
						continue;
					}
					if (WrdICmpCmd(name, "OFFSET")) {
						const char* ap = args;
						offset = WrdParseInt(ap);
						e->offsetMeas = offset;
						continue;
					}
					if (WrdICmpCmd(name, "REM") || WrdICmpCmd(name, "REMARK")) {
						/* 括弧なし @REM　//…; の本文は歌詞にしない（TiMidity と同じ） */
						while (*q && *q != ';') q++;
						continue;
					}
					if (WrdICmpCmd(name, "WMODE")) {
						int a[4] = {};
						WrdParseArgs(args, a, 4);
						wmode = a[0];
						wmodeChar = a[1];
						WrdCmd c = {};
						c.tick48 = tick;
						c.kind = WRD_WMODE;
						c.a = a[0];
						c.b = a[1];
						c.arg[0] = a[0]; c.arg[1] = a[1];
						c.narg = 2;
						WrdPush(e, c);
						continue;
					}
					if (WrdICmpCmd(name, "WAIT") || WrdICmpCmd(name, "INKEY") || WrdICmpCmd(name, "REST")) {
						int a[4] = {};
						WrdParseArgs(args, a, 4);
						inStartup = 0;
						sawWait = 1;
						e->usedAbsWait = 1;
						if (WrdICmpCmd(name, "REST")) {
							tick += a[0] * measT + a[1];
						} else {
							int m = a[0] + offset;
							if (m < 1) m = 1;
							const int dest = (m - 1) * measT + a[1];
							/* WMODE 行待ちが次の絶対 WAIT を越えると、消しスペースが
							   次フレーズの LOCATE のあとに当たって「の」が残る */
							if (tick > dest) {
								for (size_t i = 0; i < e->cmds.size(); ++i) {
									if (e->cmds[i].tick48 > dest)
										e->cmds[i].tick48 = dest;
								}
							}
							tick = dest;
						}
						continue;
					}
					WrdCmd c = {};
					c.tick48 = inStartup ? 0 : tick;
					c.kind = WrdKindFromName(name);
					if (caret && WrdICmpCmd(name, "PAL"))
						c.kind = WRD_EPAL;
					if (caret && WrdICmpCmd(name, "SCROLL"))
						c.kind = WRD_ESCROLL;
					if (WrdICmpCmd(name, "LOCATE")) {
						const char* ap = args;
						int hasSemi = 0;
						for (const char* s = args; *s; ++s)
							if (*s == ';') hasSemi = 1;
						c.a = WrdParseInt(ap);
						while (*ap == ' ' || *ap == '\t') ap++;
						if (*ap == ';' || *ap == ',') ap++;
						c.b = WrdParseInt(ap);
						c.g = hasSemi;
						c.arg[0] = c.a; c.arg[1] = c.b;
						c.narg = 2;
					} else if (WrdICmpCmd(name, "PAL") && !caret) {
						strncpy_s(c.text, args, _TRUNCATE);
					} else if (WrdICmpCmd(name, "MAG") || WrdICmpCmd(name, "PLOAD")
						|| WrdICmpCmd(name, "PATH") || WrdICmpCmd(name, "PALCHG")) {
						const char* ap = args;
						char fn[200];
						int fi = 0;
						while (*ap == ' ' || *ap == '\t') ap++;
						while (*ap && *ap != ',' && *ap != '<' && fi < 198)
							fn[fi++] = *ap++;
						fn[fi] = 0;
						while (fi > 0 && (fn[fi - 1] == ' ' || fn[fi - 1] == '\t'))
							fn[--fi] = 0;
						WrdSjisToWide(fn, c.path, MAX_PATH);
						strncpy_s(c.text, args, _TRUNCATE);
						if (*ap == ',' || *ap == '<') ap++;
						int vals[4] = {};
						int field = 0;
						while (field < 4) {
							while (*ap == ' ' || *ap == '\t') ap++;
							if (!*ap && field == 0) break;
							int sign = 1, v = 0, any = 0;
							if (*ap == '-') { sign = -1; ap++; }
							while (*ap >= '0' && *ap <= '9') {
								v = v * 10 + (*ap - '0');
								ap++;
								any = 1;
							}
							vals[field++] = v * sign;
							(void)any;
							while (*ap == ' ' || *ap == '\t') ap++;
							if (*ap == ',' || *ap == '<' || *ap == '>') { ap++; continue; }
							break;
						}
						c.a = vals[0]; c.b = vals[1]; c.c = vals[2]; c.d = vals[3];
						c.narg = field;
						c.arg[0] = c.a; c.arg[1] = c.b; c.arg[2] = c.c; c.arg[3] = c.d;
					} else if (WrdICmpCmd(name, "EXEC") || WrdICmpCmd(name, "ESC")
						|| WrdICmpCmd(name, "MIDI")) {
						strncpy_s(c.text, args, _TRUNCATE);
					} else {
						int a[16] = {};
						WrdParseArgs(args, a, 16);
						c.a = a[0]; c.b = a[1]; c.c = a[2]; c.d = a[3];
						c.e = a[4]; c.f = a[5]; c.g = a[6];
						for (int i = 0; i < 16; ++i) c.arg[i] = a[i];
						c.narg = 0;
						{
							const char* ap = args;
							while (*ap && c.narg < 16) {
								const char* before = ap;
								WrdParseInt(ap);
								if (ap == before) break;
								c.narg++;
							}
						}
					}
					if (c.kind != WRD_NOP)
						WrdPush(e, c);
					continue;
				}
				if (wmodeChar) {
					/* @WMODE(n,1): _ 待ち、| | 一括表示、\ は | _ \ を出す。
					   雲=89 5F のように trail が '_' でも、対の2バイトは文字として取る。 */
					unsigned char c0 = (unsigned char)*q;
					if (c0 == ';') {
						sawSemiCont = 1;
						break;
					}
					if (c0 == '_' && !WrdIsSjisLead((unsigned char)q[0])) {
						if (!inStartup)
							tick += WrdLineWaitTicks(wmode, measT);
						charStepped = 1;
						sawBareAt = 1;
						q++;
						continue;
					}
					if (c0 == '|') {
						q++;
						char tbuf[256];
						int ti = 0;
						while (*q && ti < 254) {
							unsigned char c = (unsigned char)*q;
							if (c == '|' && !WrdIsSjisLead(c))
								break;
							const int lead = WrdIsSjisLead(c) && q[1]
								&& WrdIsSjisTrail((unsigned char)q[1]);
							tbuf[ti++] = *q++;
							if (lead && ti < 254)
								tbuf[ti++] = *q++;
						}
						if (*q == '|') q++;
						tbuf[ti] = 0;
						if (ti > 0) {
							sawText = 1;
							WrdPushTextCmd(e, inStartup ? 0 : tick, tbuf, 2);
						}
						if (!inStartup)
							tick += WrdLineWaitTicks(wmode, measT);
						charStepped = 1;
						sawBareAt = 1;
						continue;
					}
					if (c0 == '\\') {
						q++;
						char tbuf[4] = {};
						int ti = 0;
						if (*q) {
							unsigned char c = (unsigned char)*q;
							const int lead = WrdIsSjisLead(c);
							tbuf[ti++] = *q++;
							if (lead && *q)
								tbuf[ti++] = *q++;
						}
						tbuf[ti] = 0;
						if (ti > 0) {
							sawText = 1;
							WrdPushTextCmd(e, inStartup ? 0 : tick, tbuf, 2);
						}
						if (!inStartup)
							tick += WrdLineWaitTicks(wmode, measT);
						charStepped = 1;
						sawBareAt = 1;
						continue;
					}
					{
						char tbuf[4] = {};
						int ti = 0;
						const int lead = WrdIsSjisLead(c0);
						tbuf[ti++] = *q++;
						if (lead && *q)
							tbuf[ti++] = *q++;
						tbuf[ti] = 0;
						sawText = 1;
						WrdPushTextCmd(e, inStartup ? 0 : tick, tbuf, 2);
						if (!inStartup)
							tick += WrdLineWaitTicks(wmode, measT);
						charStepped = 1;
						continue;
					}
				}
				if (*q == '\\') {
					sawBareAt = 1;
					e->usedBs = 1;
					q++;
					continue;
				}
				/* text remainder。SJIS 2バイトを跨いで \ @ を切らない */
				char tbuf[256];
				int ti = 0;
				while (*q && ti < 254) {
					unsigned char c = (unsigned char)*q;
					const int lead = WrdIsSjisLead(c);
					if (!lead) {
						if (c == '@') break;
						if (c == '^' && WrdPeekCaretCommand(q)) break;
						if (c == '\\') break;
						if (c == ';' && (q[1] == 0 || q[1] == '@')) {
							sawSemiCont = 1;
							break;
						}
					}
					tbuf[ti++] = *q++;
					if (lead && *q && ti < 254)
						tbuf[ti++] = *q++;
				}
				tbuf[ti] = 0;
				if (ti > 0) {
					sawText = 1;
					WrdPushTextCmd(e, inStartup ? 0 : tick, tbuf, isPreview ? 1 : 2);
				}
			}
		}

		int lineWait = 0;
		/* @WAIT は絶対小節へジャンプするだけ。その行に WMODE を足すと歌詞が遅れる */
		if (!inStartup && !sawWait) {
			if (sawText || sawBareAt || (line[0] && line[0] != '*' && line[0] != '\'')) {
				int empty = 1;
				for (char* s = line; *s; ++s) {
					if (*s != ' ' && *s != '\t' && *s != '\r') { empty = 0; break; }
				}
				if (!empty || sawBareAt || sawText)
					lineWait = 1;
				if (empty && !sawBareAt && !sawText)
					lineWait = 1; /* 空行も WMODE 1 歩 */
			}
		}
		/* 行末 \ はカラオケ待ち（カーソル維持）。それ以外のテキスト行は画面改行 */
		if (sawText && !sawBareAt && !sawWait && !sawSemiCont) {
			WrdCmd cr = {};
			cr.tick48 = inStartup ? 0 : tick;
			cr.kind = WRD_TEXT;
			cr.text[0] = '\n';
			cr.g = 3;
			WrdPush(e, cr);
		}
		if (lineWait && !sawSemiCont && !charStepped) {
			tick += WrdLineWaitTicks(wmode, measT);
		}
		prevEndedBs = (sawBareAt && !sawWait) ? 1 : 0;
		if (sawText && !sawBareAt && !sawWait && !sawSemiCont)
			prevEndedBs = 0;
		/* STARTUP は @WAIT/@INKEY/@REST まで続く。行末で切ると MAG と歌詞が小節から外れる */

		*lineEnd = save;
		p = lineEnd;
		if (*p == '\r') p++;
		if (*p == '\n') p++;
	}

	std::stable_sort(e->cmds.begin(), e->cmds.end(), [](const WrdCmd& a, const WrdCmd& b) {
		return a.tick48 < b.tick48;
	});
	e->loaded = 1;
	e->cur = 0;
	e->lastTick = -1;
	WrdEngineResetScreen(e);
	/* apply startup (tick 0) immediately */
	WrdEngineSeek(e, 0);
	return 1;
}

static int WrdRcpU16(const unsigned char* p)
{
	return p[0] | (p[1] << 8);
}

int WrdEngineLoadRcpKaraoke(WrdEngine* e, const wchar_t* rcpPath)
{
	if (!e || !rcpPath || !rcpPath[0]) return 0;
	HANDLE f = CreateFileW(rcpPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 0x586 || size > 8 * 1024 * 1024) { CloseHandle(f); return 0; }
	std::vector<unsigned char> buf(size);
	if (!ReadFile(f, buf.data(), size, &got, NULL) || got != size) {
		CloseHandle(f); return 0;
	}
	CloseHandle(f);
	const unsigned char* d = buf.data();
	if (memcmp(d, "RCM-PC98V2.0", 12) != 0) return 0;
	unsigned tickQ = d[0x1C0];
	if (size > 0x1E7) tickQ |= ((unsigned)d[0x1E7] << 8);
	if (tickQ < 1) tickQ = 48;
	unsigned trkN = d[0x1E6];
	if (trkN == 0) trkN = 18;
	if (trkN > 36) trkN = 36;
	e->karaMarks.clear();
	unsigned pos = 0x586;
	const unsigned hdrSize = 0x2Cu, evSize = 4u;
	int bestF6 = 0;
	std::vector<WrdEngine::KaraMark> marks;
	for (unsigned ti = 0; ti < trkN && pos + hdrSize <= size; ++ti) {
		unsigned trkLen = (unsigned)WrdRcpU16(d + pos);
		if (trkLen < hdrSize) trkLen = hdrSize;
		if (pos + trkLen > size) trkLen = size - pos;
		int midiCh = d[pos + 4];
		int mute = d[pos + 7];
		int stPlus = (signed char)d[pos + 6];
		unsigned tick = (stPlus > 0) ? (unsigned)stPlus : 0;
		unsigned ip = pos + hdrSize;
		const unsigned dataEnd = pos + trkLen;
		struct Loop { unsigned startOff; int remain; };
		Loop loops[12];
		int loopN = 0;
		unsigned sameRet[8];
		int sameN = 0;
		int nf6 = 0;
		int bomb = 0;
		std::vector<WrdEngine::KaraMark> local;
		while (ip + evSize <= dataEnd && bomb < 80000) {
			bomb++;
			const unsigned thisOff = ip;
			unsigned char cmd = d[ip];
			unsigned delay = d[ip + 1], p1 = d[ip + 2], p2 = d[ip + 3];
			ip += evSize;
			if (cmd == 0xFD) {
				if (sameN > 0) ip = sameRet[--sameN];
				continue;
			}
			if (cmd == 0xFE) break;
			if (cmd == 0xF9) {
				if (loopN < 12) { loops[loopN].startOff = ip; loops[loopN].remain = -1; loopN++; }
				continue;
			}
			if (cmd == 0xF8) {
				int cnt = (int)delay;
				if (loopN > 0) {
					Loop& L = loops[loopN - 1];
					if (L.remain < 0) {
						if (cnt == 0) cnt = 2;
						if (cnt > 16) cnt = 16;
						L.remain = cnt;
					}
					L.remain--;
					if (L.remain > 0) ip = L.startOff;
					else loopN--;
				}
				continue;
			}
			if (cmd == 0xFC) {
				unsigned tgt = pos + (p1 | (p2 << 8));
				if (tgt >= pos + hdrSize && tgt < dataEnd && tgt != thisOff && sameN < 8) {
					sameRet[sameN++] = ip;
					ip = tgt;
				}
				continue;
			}
			if (cmd == 0xF6) {
				WrdEngine::KaraMark km = {};
				km.tick48 = (int)((unsigned long long)tick * 48ull / tickQ);
				km.sjis[0] = (char)p1;
				km.sjis[1] = (char)p2;
				if (km.sjis[0] && km.sjis[0] != ' ' && km.sjis[0] != '['
					&& !((unsigned char)km.sjis[0] == 0x81 && (unsigned char)km.sjis[1] == 0x40))
					local.push_back(km);
				nf6++;
			}
			if (cmd < 0xF0)
				tick += delay;
			(void)midiCh; (void)mute;
		}
		if (nf6 > bestF6) {
			bestF6 = nf6;
			marks.swap(local);
		}
		pos += trkLen;
	}
	e->karaMarks = std::move(marks);
	return bestF6 > 0 ? 1 : 0;
}

static int WrdNoteAtOrAfter(const std::vector<int>& n, int tick, int from)
{
	for (int i = from; i < (int)n.size(); ++i)
		if (n[i] >= tick) return i;
	return (int)n.size();
}

void WrdEngineSyncKaraoke(WrdEngine* e)
{
	if (!e || !e->loaded || e->karaSynced) return;
	if (e->usedAbsWait || !e->usedBs) return;
	if (e->karaNotes.size() < 8) return;

	struct Verse { std::vector<int> preview; std::vector<int> mora; std::vector<int> nl; };
	std::vector<Verse> verses;
	Verse* cur = NULL;
	for (int i = 0; i < (int)e->cmds.size(); ++i) {
		WrdCmd& c = e->cmds[i];
		if (c.kind != WRD_TEXT) continue;
		if (c.g == 1) {
			verses.push_back(Verse());
			cur = &verses.back();
			cur->preview.push_back(i);
		} else if (!cur) {
			continue;
		} else if (c.g == 3) {
			cur->nl.push_back(i);
		} else if (c.g == 2) {
			cur->mora.push_back(i);
		}
	}
	if (verses.empty()) return;

	const std::vector<int>& notes = e->karaNotes;
	int prevStart = -1;
	for (size_t vi = 0; vi < verses.size(); ++vi) {
		Verse& v = verses[vi];
		if (v.mora.empty() && v.preview.empty()) continue;
		char glyph[4] = {};
		const char* src = NULL;
		if (!v.mora.empty()) src = e->cmds[v.mora[0]].text;
		else if (!v.preview.empty()) src = e->cmds[v.preview[0]].text;
		if (src) {
			const char* s = src;
			while (*s) {
				if ((unsigned char)*s == 0x1B) {
					s++;
					if (*s == '[') {
						s++;
						while (*s && !(*s >= '@' && *s <= '~')) s++;
						if (*s) s++;
					} else if (*s) {
						s++;
					}
					continue;
				}
				if (*s == ' ' || *s == '\t') { s++; continue; }
				glyph[0] = *s++;
				if (WrdIsSjisLead((unsigned char)glyph[0]) && *s)
					glyph[1] = *s;
				break;
			}
		}
		int startTick = -1;
		if (glyph[0]) {
			for (size_t mi = 0; mi < e->karaMarks.size(); ++mi) {
				const WrdEngine::KaraMark& m = e->karaMarks[mi];
				if (m.tick48 <= prevStart) continue;
				int hit = 0;
				if ((unsigned char)m.sjis[0] == (unsigned char)glyph[0]
					&& (unsigned char)m.sjis[1] == (unsigned char)glyph[1])
					hit = 1;
				else if (m.sjis[0] == '[' && m.sjis[1] == glyph[0])
					hit = 1;
				if (hit) { startTick = m.tick48; break; }
			}
		}
		int n0;
		if (startTick >= 0)
			n0 = WrdNoteAtOrAfter(notes, startTick, 0);
		else if (vi == 0)
			n0 = WrdNoteAtOrAfter(notes, WrdMeasTicks(e->tsNum, e->tsDen) * 2, 0);
		else
			n0 = WrdNoteAtOrAfter(notes, prevStart + 1, 0);
		int n1 = (int)notes.size();
		if (vi + 1 < verses.size()) {
			Verse& nx = verses[vi + 1];
			char ng[4] = {};
			const char* nsrc = NULL;
			if (!nx.mora.empty()) nsrc = e->cmds[nx.mora[0]].text;
			else if (!nx.preview.empty()) nsrc = e->cmds[nx.preview[0]].text;
			if (nsrc) {
				const char* s = nsrc;
				while (*s) {
					if ((unsigned char)*s == 0x1B) {
						s++;
						if (*s == '[') {
							s++;
							while (*s && !(*s >= '@' && *s <= '~')) s++;
							if (*s) s++;
						} else if (*s) s++;
						continue;
					}
					if (*s == ' ' || *s == '\t') { s++; continue; }
					ng[0] = *s++;
					if (WrdIsSjisLead((unsigned char)ng[0]) && *s) ng[1] = *s;
					break;
				}
			}
			int nt = -1;
			if (ng[0]) {
				for (size_t mi = 0; mi < e->karaMarks.size(); ++mi) {
					const WrdEngine::KaraMark& m = e->karaMarks[mi];
					if (m.tick48 <= (startTick >= 0 ? startTick : prevStart)) continue;
					int hit = 0;
					if ((unsigned char)m.sjis[0] == (unsigned char)ng[0]
						&& (unsigned char)m.sjis[1] == (unsigned char)ng[1])
						hit = 1;
					else if (m.sjis[0] == '[' && m.sjis[1] == ng[0])
						hit = 1;
					if (hit) { nt = m.tick48; break; }
				}
			}
			if (nt > 0)
				n1 = WrdNoteAtOrAfter(notes, nt, n0);
		}
		if (n0 >= (int)notes.size()) break;
		if (n1 <= n0) n1 = n0 + (int)v.mora.size();
		if (n1 > (int)notes.size()) n1 = (int)notes.size();
		const int N = n1 - n0;
		const int M = (int)v.mora.size();
		for (int j = 0; j < M; ++j) {
			int ni = n0;
			if (M > 1 && N > 1)
				ni = n0 + (int)((long long)j * (N - 1) / (M - 1));
			if (ni >= n1) ni = n1 - 1;
			if (ni < 0) ni = 0;
			e->cmds[v.mora[j]].tick48 = notes[ni];
		}
		const int t0 = (M > 0) ? e->cmds[v.mora[0]].tick48
			: (n0 < (int)notes.size() ? notes[n0] : 0);
		for (int idx : v.preview)
			e->cmds[idx].tick48 = t0;
		const int t1 = (M > 0) ? e->cmds[v.mora.back()].tick48 : t0;
		for (int idx : v.nl)
			e->cmds[idx].tick48 = t1;
		prevStart = t0;
	}

	std::stable_sort(e->cmds.begin(), e->cmds.end(), [](const WrdCmd& a, const WrdCmd& b) {
		return a.tick48 < b.tick48;
	});
	e->karaSynced = 1;
	e->lastTick = -1;
	e->cur = 0;
}

static unsigned SmfBe(const unsigned char* p, int n)
{
	unsigned v = 0;
	for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
	return v;
}
static int SmfVar(const unsigned char*& q, const unsigned char* end, unsigned& out)
{
	out = 0;
	for (int i = 0; i < 4; ++i) {
		if (q >= end) return 0;
		unsigned char b = *q++;
		out = (out << 7) | (b & 0x7f);
		if (!(b & 0x80)) return 1;
	}
	return 1;
}

int WrdEngineLoadSmfClock(WrdEngine* e, const wchar_t* midPath, int sampleRate)
{
	if (!e) return 0;
	e->tempo.clear();
	e->karaMarks.clear();
	e->karaNotes.clear();
	e->smfOk = 0;
	e->sr = (sampleRate >= 8000) ? sampleRate : 44100;
	e->smfDiv = 48;
	if (!midPath || !midPath[0]) return 0;
	HANDLE f = CreateFileW(midPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 14 || size > 8 * 1024 * 1024) { CloseHandle(f); return 0; }
	std::vector<unsigned char> buf(size);
	if (!ReadFile(f, buf.data(), size, &got, NULL) || got != size) {
		CloseHandle(f); return 0;
	}
	CloseHandle(f);
	const unsigned char* d = buf.data();
	unsigned smfOff = 0, smfSz = size;
	if (size >= 20 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "RMID", 4) == 0) {
		unsigned off = 12;
		while (off + 8 <= size) {
			unsigned cksz = d[off + 4] | ((unsigned)d[off + 5] << 8)
				| ((unsigned)d[off + 6] << 16) | ((unsigned)d[off + 7] << 24);
			if (memcmp(d + off, "data", 4) == 0) {
				smfOff = off + 8; smfSz = cksz; break;
			}
			off += 8 + cksz + (cksz & 1);
		}
		d = buf.data() + smfOff;
	}
	if (smfSz < 14 || memcmp(d, "MThd", 4) != 0) return 0;
	unsigned hlen = SmfBe(d + 4, 4);
	int div = (int)SmfBe(d + 12, 2);
	if (div & 0x8000) div = 48;
	if (div < 1) div = 48;
	e->smfDiv = div;
	const unsigned char* p = d + 8 + hlen;
	const unsigned char* end = d + smfSz;
	struct Tmp { unsigned tick; int usec; };
	std::vector<Tmp> evs;
	std::vector<unsigned> chOn[16];
	int chN[16] = {}, chMid[16] = {};
	while (p + 8 <= end && memcmp(p, "MTrk", 4) == 0) {
		unsigned tsz = SmfBe(p + 4, 4);
		p += 8;
		const unsigned char* te = p + tsz;
		if (te > end) te = end;
		unsigned tick = 0;
		unsigned char run = 0;
		while (p < te) {
			unsigned dt = 0;
			if (!SmfVar(p, te, dt)) break;
			tick += dt;
			if (p >= te) break;
			unsigned char st = *p;
			if (st < 0x80) st = run;
			else { run = st; p++; }
			if (st == 0xff) {
				if (p >= te) break;
				unsigned char type = *p++;
				unsigned ln = 0;
				SmfVar(p, te, ln);
				if (type == 0x51 && ln >= 3 && p + 3 <= te) {
					int usec = (int)SmfBe(p, 3);
					Tmp t; t.tick = tick; t.usec = usec;
					evs.push_back(t);
				} else if ((type == 0x05 || type == 0x01) && ln >= 1 && p + ln <= te) {
					WrdEngine::KaraMark km = {};
					km.tick48 = (int)((unsigned long long)tick * 48ull / (unsigned)(div > 0 ? div : 48));
					int n = ln > 2 ? 2 : (int)ln;
					memcpy(km.sjis, p, (size_t)n);
					if (km.sjis[0] && km.sjis[0] != ' '
						&& !((unsigned char)km.sjis[0] == 0x81 && (unsigned char)km.sjis[1] == 0x40))
						e->karaMarks.push_back(km);
				} else if (type == 0x58 && ln >= 2 && p + 2 <= te) {
					e->tsNum = p[0];
					e->tsDen = 1 << p[1];
					if (e->tsDen < 1) e->tsDen = 4;
				}
				p += ln;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned ln = 0;
				SmfVar(p, te, ln);
				p += ln;
			} else {
				int need = 2;
				if ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) need = 1;
				if ((st & 0xf0) == 0xf0) need = 0;
				if (need > 0 && p + need <= te) {
					unsigned char d1 = p[0];
					unsigned char d2 = (need == 2) ? p[1] : 0;
					if ((st & 0xf0) == 0x90 && d2 > 0) {
						const int ch = st & 0x0f;
						if (ch != 9) {
							chOn[ch].push_back(tick);
							chN[ch]++;
							if (d1 >= 55 && d1 <= 84) chMid[ch]++;
						}
					}
				}
				p += need;
			}
		}
		p = te;
	}
	std::sort(evs.begin(), evs.end(), [](const Tmp& a, const Tmp& b) { return a.tick < b.tick; });
	WrdEngine::TempoPt tp;
	tp.sample = 0; tp.tick = 0; tp.usecQn = 500000;
	e->tempo.push_back(tp);
	__int64 sample = 0;
	unsigned lastTick = 0;
	int usec = 500000;
	for (size_t i = 0; i < evs.size(); ++i) {
		unsigned dt = evs[i].tick - lastTick;
		sample += (__int64)dt * (__int64)usec * (__int64)e->sr / (1000000ll * div);
		usec = evs[i].usec;
		lastTick = evs[i].tick;
		tp.sample = sample;
		tp.tick = lastTick;
		tp.usecQn = usec;
		e->tempo.push_back(tp);
	}
	e->karaNotes.clear();
	{
		int moraEst = e->usedBs > 8 ? e->usedBs : 64;
		int best = -1, bestDiff = 1 << 30;
		for (int ch = 0; ch < 16; ++ch) {
			if (chN[ch] < 16) continue;
			if (chMid[ch] * 2 < chN[ch]) continue;
			int d = chN[ch] - moraEst;
			if (d < 0) d = -d;
			if (d < bestDiff) { bestDiff = d; best = ch; }
		}
		if (best >= 0) {
			for (size_t i = 0; i < chOn[best].size(); ++i) {
				int t48 = (int)((unsigned long long)chOn[best][i] * 48ull / (unsigned)div);
				if (e->karaNotes.empty() || e->karaNotes.back() != t48)
					e->karaNotes.push_back(t48);
			}
		}
	}
	e->smfOk = 1;
	return 1;
}

int WrdEngineTickFromSample(const WrdEngine* e, __int64 playb)
{
	if (!e || playb < 0) return 0;
	if (!e->smfOk || e->tempo.empty()) {
		/* 120 BPM, 48 PPQN */
		const int sr = e->sr > 0 ? e->sr : 44100;
		return (int)(playb * 48 * 2 / sr); /* 120bpm: 2 quarter per second * 48 */
	}
	const WrdEngine::TempoPt* hit = &e->tempo[0];
	for (size_t i = 1; i < e->tempo.size(); ++i) {
		if (e->tempo[i].sample > playb) break;
		hit = &e->tempo[i];
	}
	const int div = e->smfDiv > 0 ? e->smfDiv : 48;
	const int sr = e->sr > 0 ? e->sr : 44100;
	__int64 ds = playb - hit->sample;
	if (ds < 0) ds = 0;
	__int64 extra = ds * 1000000ll * div / ((__int64)hit->usecQn * sr);
	unsigned smfTick = hit->tick + (unsigned)extra;
	return (int)((unsigned long long)smfTick * 48ull / (unsigned)div);
}

void WrdEngineSeek(WrdEngine* e, int tick48)
{
	if (!e || !e->loaded) return;
	if (tick48 < 0) tick48 = 0;
	if (tick48 < e->lastTick || e->lastTick < 0) {
		WrdEngineResetScreen(e);
		e->cur = 0;
	}
	while (e->cur < (int)e->cmds.size() && e->cmds[e->cur].tick48 <= tick48) {
		WrdApplyCmd(e, e->cmds[e->cur]);
		e->cur++;
	}
	WrdApplyFadeNow(e, tick48);
	e->lastTick = tick48;
}

void WrdEnginePaint(WrdEngine* e, HDC hdc, const RECT* rc, int capH)
{
	if (!e || !hdc || !rc || !e->loaded) return;
	const int x0 = rc->left;
	const int y0 = rc->top + capH;
	const int cw = rc->right - rc->left;
	const int ch = rc->bottom - rc->top - capH;
	if (cw <= 8 || ch <= 8) return;

	/* 80×25 は 8×16 ドット。縦横別ストレッチだと漢字が欠け、色帯がずれる */
	int dw = cw, dh = ch;
	if (cw * 400 > ch * 640) {
		dw = ch * 640 / 400;
		dh = ch;
	} else {
		dw = cw;
		dh = cw * 400 / 640;
	}
	if (dw < 80) dw = cw;
	if (dh < 25) dh = ch;
	const int ox = x0 + (cw - dw) / 2;
	const int oy = y0 + (ch - dh) / 2;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = 640;
	bmi.bmiHeader.biHeight = -400;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	/* pal / MAG はインデックス。A=0 の 32bpp を DWM に渡すと窓が完全透過する */
	std::vector<unsigned> fb(640 * 400, 0xFF000000u);
	if (e->gfxOn) {
		WrdApplyFadeNow(e, e->lastTick);
		const int page = WrdPageOk(e->dispPage) ? e->dispPage : 0;
		const unsigned char* src = e->gfxPage[page];
		for (int i = 0; i < 640 * 400; ++i)
			fb[i] = e->pal[0][src[i] & 15] | 0xFF000000u;
	}

	StretchDIBits(hdc, ox, oy, dw, dh, 0, 0, 640, 400,
		fb.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

	if (!e->textOn) return;

	const int cols = WrdTextCols(e);
	const int cellW = dw / cols;
	const int cellH = dh / 25;
	if (cellW < 4 || cellH < 8) return;

	/* ^FONTM(1) は演出君メカフォント。ファイル未指定時は TMIDI と同じく Windows 日本語フォント */
	HFONT font = CreateFontW(cellH, 0, 0, 0, FW_NORMAL, 0, 0, 0,
		SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
		FIXED_PITCH | FF_MODERN, e->fontMecha ? L"MS Mincho" : L"MS Gothic");
	HGDIOBJ oldF = SelectObject(hdc, font);
	SetBkMode(hdc, TRANSPARENT);
	SetTextAlign(hdc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
	for (int y = 0; y < 25; ++y) {
		int x = 0;
		while (x < cols) {
			unsigned char at = e->attr[y][x];
			unsigned char c0 = (unsigned char)e->cells[y][x];
			char buf[4] = {};
			int cells = 1;
			buf[0] = (char)c0;
			if (WrdIsSjisLead(c0) && x + 1 < cols
				&& WrdIsSjisTrail((unsigned char)e->cells[y][x + 1])) {
				buf[1] = e->cells[y][x + 1];
				cells = 2;
			}
			if (c0 == 0x20 || (cells == 2 && (unsigned char)buf[0] == 0x81
				&& (unsigned char)buf[1] == 0x40)) {
				x += cells;
				continue;
			}
			if (cells == 1 && WrdIsSjisLead(c0)) {
				x += 1;
				continue;
			}
			wchar_t wbuf[4];
			WrdSjisToWide(buf, wbuf, 4);
			if (!wbuf[0]) { x += cells; continue; }
			COLORREF fg = WrdPc98Color(at);
			COLORREF bg = RGB(0, 0, 0);
			if (at & 0x10) {
				COLORREF t = fg; fg = bg; bg = t;
			}
			const int px = ox + x * cellW;
			const int py = oy + y * cellH;
			RECT tr;
			tr.left = px;
			tr.top = py;
			tr.right = px + cells * cellW;
			tr.bottom = py + cellH;
			if (at & 0x10) {
				HBRUSH br = CreateSolidBrush(bg);
				FillRect(hdc, &tr, br);
				DeleteObject(br);
			}
			SetTextColor(hdc, fg);
			ExtTextOutW(hdc, px, py, ETO_CLIPPED, &tr, wbuf, (UINT)wcslen(wbuf), NULL);
			x += cells;
		}
	}
	SelectObject(hdc, oldF);
	DeleteObject(font);
}

int WrdEngineSelfTest()
{
	int fail = 0;
	WrdEngine* e = new WrdEngine();
	WrdEngineInit(e);

	/* パレット番号で描いて @PAL 後に色が変わる */
	e->activePage = 0;
	WrdPlotIdx(e, 0, 10, 10, 1);
	if (e->gfxPage[0][10 * 640 + 10] != 1) fail |= 1;
	e->pal[0][1] = 0x00FF00u;
	if ((e->pal[0][e->gfxPage[0][10 * 640 + 10]] & 0xFFFFFF) != 0x00FF00) fail |= 2;

	/* GMOVE コピー（重なり）と交換 */
	WrdPlotIdx(e, 0, 0, 0, 3);
	WrdPlotIdx(e, 1, 5, 5, 7);
	WrdCopyRect(e, 0, 1, 0, 0, 0, 0, 5, 5);
	if (e->gfxPage[1][5 * 640 + 5] != 3) fail |= 4;
	WrdPlotIdx(e, 0, 2, 2, 4);
	WrdPlotIdx(e, 1, 8, 8, 9);
	WrdSwapRect(e, 0, 1, 2, 2, 2, 2, 8, 8);
	if (e->gfxPage[0][2 * 640 + 2] != 9 || e->gfxPage[1][8 * 640 + 8] != 4) fail |= 8;

	/* @FADE 補間。speed=0 は即時 */
	e->pal[1][0] = 0x000000;
	e->pal[2][0] = 0x0000FF;
	WrdStartFade(e, 1, 2, 0, 0);
	if ((e->pal[0][0] & 0xFF) != 0xFF) fail |= 16;
	e->pal[1][0] = 0x000000;
	e->pal[2][0] = 0x0000F0;
	WrdStartFade(e, 1, 2, 10, 100);
	WrdApplyFadeNow(e, 105);
	unsigned mid = e->pal[0][0] & 0xFF;
	if (mid < 0x70 || mid > 0x80) fail |= 32;
	WrdApplyFadeNow(e, 120);
	if ((e->pal[0][0] & 0xFF) != 0xF0) fail |= 64;

	/* PHO 4 プレーン（B,R,G,E）→ インデックス 15 */
	std::vector<unsigned char> pho(128000, 0);
	memset(pho.data(), 0xFF, 80); /* 先頭 640 ドット、プレーン B */
	memset(pho.data() + 32000, 0xFF, 80);
	memset(pho.data() + 64000, 0xFF, 80);
	memset(pho.data() + 96000, 0xFF, 80);
	if (!WrdLoadPhoToPage(e, pho.data(), 128000, 0)) fail |= 128;
	if (e->gfxPage[0][0] != 15) fail |= 256;

	/* XCOPY method=1 は色 0 を抜く */
	memset(e->gfxPage[0], 0, 640 * 400);
	memset(e->gfxPage[1], 5, 100);
	e->gfxPage[0][0] = 0;
	e->gfxPage[0][1] = 2;
	int xa[16] = { 0, 0, 1, 0, 0, 0, 0, 1, 1 };
	WrdXCopy(e, xa, 9);
	if (e->gfxPage[1][0] != 5 || e->gfxPage[1][1] != 2) fail |= 512;

	/* LOCATE は消さない。白プレビューの上に1文字だけ色を重ねる */
	WrdEngineResetScreen(e);
	e->cmds.clear();
	e->cur = 0;
	e->lastTick = -1;
	e->loaded = 1;
	{
		WrdCmd c = {};
		c.kind = WRD_COLOR; c.a = 7; e->cmds.push_back(c);
		c = {}; c.kind = WRD_TEXT; strncpy_s(c.text, "ABCD", _TRUNCATE); e->cmds.push_back(c);
		c = {}; c.kind = WRD_LOCATE; c.a = 1; c.b = 1; e->cmds.push_back(c);
		c = {}; c.kind = WRD_COLOR; c.a = 5; e->cmds.push_back(c);
		c = {}; c.kind = WRD_TEXT; strncpy_s(c.text, "X", _TRUNCATE); e->cmds.push_back(c);
	}
	WrdEngineSeek(e, 0);
	if (e->cells[0][0] != 'X') fail |= 1024;
	if (e->cells[0][1] != 'B' || e->cells[0][2] != 'C' || e->cells[0][3] != 'D') fail |= 2048;
	if ((e->attr[0][0] & 15) != 5) fail |= 4096;
	if ((e->attr[0][1] & 15) != 7) fail |= 8192;

	/* 空白歌詞は消去。; 前のスペースを捨てると前の行が残る */
	WrdEngineResetScreen(e);
	e->cmds.clear();
	e->cur = 0;
	e->lastTick = -1;
	e->loaded = 1;
	{
		WrdCmd c = {};
		c.kind = WRD_TEXT; strncpy_s(c.text, "ABCD", _TRUNCATE); e->cmds.push_back(c);
		c = {}; c.kind = WRD_LOCATE; c.a = 1; c.b = 1; e->cmds.push_back(c);
		c = {}; c.kind = WRD_TEXT; strncpy_s(c.text, "  ", _TRUNCATE); e->cmds.push_back(c);
	}
	WrdEngineSeek(e, 0);
	if (e->cells[0][0] != ' ' || e->cells[0][1] != ' ') fail |= 16384;
	if (e->cells[0][2] != 'C' || e->cells[0][3] != 'D') fail |= 32768;

	WrdEngineFree(e);
	delete e;
	return fail;
}
