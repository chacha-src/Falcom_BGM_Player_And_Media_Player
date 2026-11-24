#include "OxzMenu.h"
#include "OxzLook.h"
#include "OxzLang.h"
#include <windowsx.h>
#include <cmath>
#include <cstring>

static const int kRowH = 28;
static const int kSepH = 12;

struct Host {
	int closed;
	UINT result;
};

struct Pop {
	Host* host;
	Pop* root;
	Pop* parent;
	Pop* child;
	const std::vector<OxzMi>* items;
	HWND hwnd;
	HFONT font;
	HDC mem;
	HBITMAP dib;
	HBITMAP oldBmp;
	DWORD* bits;
	int dibW, dibH;
	int menuW, viewH, contentH, pad, scroll, scrollMax;
	int screenX, screenY;
	int hot;
	int childIndex;
	int asSub;
	int phase;
	ULONGLONG animStart;
	ULONGLONG hotSince;
	int style;
	int alive;
};

static int Stag(int base, int n)
{
	if (n <= 1) return base;
	if (base * (n - 1) <= 260) return base;
	int s = 260 / (n - 1);
	return s < 1 ? 1 : s;
}

static int InDur(int style, int asSub)
{
	if (style == 5 || style == 6) return asSub ? 220 : 280;
	if (style == 7 || style == 9) return asSub ? 200 : 260;
	if (style == 8) return asSub ? 180 : 240;
	return asSub ? 150 : 210;
}

static int OutDur(int style)
{
	if (style == 5 || style == 6) return 200;
	if (style == 7 || style == 9) return 180;
	return 160;
}

static int Radial(int style) { return style == 4 || style == 5 || style == 6; }
static int Indexed(int style) { return style == 2 || style == 3 || style == 7 || style == 8 || style == 9; }

static float EaseOut(float t)
{
	if (t <= 0.f) return 0.f;
	if (t >= 1.f) return 1.f;
	float u = 1.f - t;
	return 1.f - u * u * u;
}
static float EaseBack(float t)
{
	if (t <= 0.f) return 0.f;
	if (t >= 1.f) return 1.f;
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.f;
	float u = t - 1.f;
	return 1.f + c3 * u * u * u + c1 * u * u;
}
static float EaseSine(float t)
{
	if (t <= 0.f) return 0.f;
	if (t >= 1.f) return 1.f;
	return 0.5f * (1.f - cosf(t * 3.14159265f));
}
static int FadeIn(float t)
{
	if (t <= 0.f) return 0;
	if (t >= 0.40f) return 255;
	int f = (int)(255.f * (t / 0.40f));
	if (f < 0) f = 0;
	if (f > 255) f = 255;
	return f;
}
static int FadeOut(float t)
{
	if (t <= 0.f) return 255;
	if (t >= 1.f) return 0;
	int f = (int)(255.f * (1.f - t));
	if (f < 0) f = 0;
	if (f > 255) f = 255;
	return f;
}

static int RowH(const OxzMi& it) { return it.kind == OXMI_SEP ? kSepH : kRowH; }

static int ContentH(const std::vector<OxzMi>& items)
{
	int h = 8;
	for (size_t i = 0; i < items.size(); ++i) h += RowH(items[i]);
	return h;
}

static int RowTop(const std::vector<OxzMi>& items, int idx)
{
	int y = 4;
	for (int i = 0; i < idx && i < (int)items.size(); ++i) y += RowH(items[i]);
	return y;
}

static void Scatter(int i, int origin, float* sx, float* sy)
{
	float ang = (float)i * 2.3999632f + (float)origin * 0.41f;
	float rad = 110.f + (float)((i * 47 + origin * 19) % 110);
	*sx = cosf(ang) * rad;
	*sy = sinf(ang) * rad;
}

static int Motion(const Pop* p, int idx, int rowCenter, int* ox, int* oy, int* fade)
{
	*ox = 0;
	*oy = 0;
	*fade = 255;
	if (!p || p->phase == 0) return 1;
	int style = p->style;
	int n = p->items ? (int)p->items->size() : 0;
	int elapsed = (int)(GetTickCount64() - p->animStart);
	if (elapsed < 0) elapsed = 0;
	int origin = 0;
	if (style == 0) {
		int dur = p->phase == 1 ? 210 : 160;
		float t = (float)elapsed / (float)dur;
		if (t > 1.f) t = 1.f;
		*fade = p->phase == 1 ? (int)(255.f * EaseOut(t)) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 1) {
		int dur = p->phase == 1 ? 210 : 160;
		float t = (float)elapsed / (float)dur;
		if (t > 1.f) t = 1.f;
		float e = EaseOut(t);
		int band = (int)((p->viewH / 2) * (p->phase == 1 ? e : (1.f - e)));
		if (band < 8) band = 8;
		int mid = p->viewH / 2;
		if (rowCenter < mid - band || rowCenter > mid + band) return 0;
		float pull = (float)(mid - rowCenter);
		if (pull > 10.f) pull = 10.f;
		if (pull < -10.f) pull = -10.f;
		*oy = (int)(pull * (p->phase == 1 ? (1.f - e) : e));
		*fade = p->phase == 1 ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	int stag = Stag(p->phase == 1 ? (p->asSub ? 10 : 14) : 11, n);
	int dur = p->phase == 1 ? InDur(style, p->asSub) : OutDur(style);
	int dist = idx < 0 ? 0 : idx;
	int delay;
	if (p->phase == 1) {
		if (Indexed(style)) delay = idx * stag;
		else delay = dist * stag;
	} else {
		int maxDist = n > 0 ? n - 1 : 0;
		if (Indexed(style)) delay = (n - 1 - idx) * stag;
		else delay = (maxDist - dist) * stag;
		if (delay < 0) delay = 0;
	}
	if (elapsed < delay) {
		*fade = p->phase == 1 ? 0 : 255;
		return p->phase != 1;
	}
	float t = (float)(elapsed - delay) / (float)(dur > 1 ? dur : 1);
	if (t < 0.f) t = 0.f;
	if (t > 1.f) t = 1.f;
	int enter = p->phase == 1;
	float e = EaseOut(t);
	if (style == 5) {
		float sx, sy;
		Scatter(idx, origin, &sx, &sy);
		float k = enter ? (1.f - EaseBack(t)) : (EaseOut(t) * 1.25f);
		*ox = (int)(sx * k);
		*oy = (int)(sy * k);
		*fade = enter ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 6) {
		float eb = enter ? EaseBack(t) : t;
		float ang0 = (float)idx * 2.3999632f;
		float spin = enter ? (1.f - (eb > 1.f ? 1.f : eb)) * 2.6f : eb * 3.1f;
		float ang = ang0 + spin;
		float rad = (125.f + (float)((idx * 37) % 90)) * (enter ? (1.f - (eb > 1.f ? 1.f : eb)) : e);
		*ox = (int)(cosf(ang) * rad);
		*oy = (int)(sinf(ang) * rad);
		*fade = enter ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 7) {
		float es = enter ? EaseSine(t) : e;
		float sway = sinf((float)idx * 1.71f + t * 2.4f) * (enter ? 24.f * (1.f - es) : 32.f * e);
		*ox = (int)sway;
		*oy = enter ? (int)(52.f * (1.f - es)) : (int)(-44.f * e);
		*fade = enter ? FadeIn(t * 0.9f) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 8) {
		float side = (idx & 1) ? 1.f : -1.f;
		float k = enter ? (1.f - (EaseBack(t) > 1.f ? 1.f : EaseBack(t))) : e;
		*ox = (int)(side * (enter ? 96.f : 88.f) * k);
		*oy = (int)(side * (enter ? -10.f : 12.f) * k);
		*fade = enter ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 9) {
		float es = enter ? EaseSine(t) : e;
		float wave = sinf((float)idx * 0.62f + t * (enter ? 3.6f : 4.f)) * (enter ? 30.f : 36.f);
		*ox = (int)((enter ? (-68.f + wave) : (72.f + wave)) * (enter ? (1.f - es) : e));
		*oy = (int)(sinf((float)idx * 0.95f) * (enter ? 18.f : 22.f) * (enter ? (1.f - es) : e));
		*fade = enter ? FadeIn(t * 0.85f) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 2) {
		*oy = (int)((enter ? -56.f : -48.f) * (enter ? (1.f - e) : e));
		*fade = enter ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 3) {
		*ox = (int)((enter ? -72.f : 64.f) * (enter ? (1.f - e) : e));
		*fade = enter ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	if (style == 4) {
		float pull = (float)(kRowH / 2 - rowCenter);
		if (pull > 48.f) pull = 48.f;
		if (pull < -48.f) pull = -48.f;
		*oy = (int)(pull * (enter ? (1.f - e) : e));
		*fade = enter ? FadeIn(t) : FadeOut(t);
		return *fade > 4;
	}
	*fade = enter ? FadeIn(t) : FadeOut(t);
	return *fade > 4;
}

static int PadFor(int)
{
	return 0;
}

static void EnsureDib(Pop* p, int w, int h)
{
	if (p->dib && p->dibW == w && p->dibH == h) return;
	if (p->mem && p->oldBmp) SelectObject(p->mem, p->oldBmp);
	if (p->dib) DeleteObject(p->dib);
	p->dib = NULL;
	p->oldBmp = NULL;
	p->bits = NULL;
	HDC scr = GetDC(NULL);
	if (!p->mem) p->mem = CreateCompatibleDC(scr);
	p->dib = CreateCompatibleBitmap(scr, w > 0 ? w : 1, h > 0 ? h : 1);
	ReleaseDC(NULL, scr);
	p->oldBmp = (HBITMAP)SelectObject(p->mem, p->dib);
	p->dibW = w;
	p->dibH = h;
}

static void Present(Pop* p);

static void CloseChild(Pop* p)
{
	if (!p || !p->child) return;
	Pop* c = p->child;
	p->child = NULL;
	if (c->child) CloseChild(c);
	if (c->hwnd) DestroyWindow(c->hwnd);
	delete c;
}

static void CloseNow(Pop* p)
{
	Pop* root = p && p->root ? p->root : p;
	if (root && root->hwnd) DestroyWindow(root->hwnd);
}

static void BeginExit(Pop* root)
{
	if (!root) return;
	ULONGLONG now = GetTickCount64();
	for (Pop* p = root; p; p = p->child) {
		if (p->phase == 2) continue;
		p->phase = 2;
		p->animStart = now;
	}
}

static int ThemeUnder(Pop* p)
{
	if (!p || !p->items || p->hot < 0 || p->hot >= (int)p->items->size()) return -1;
	const OxzMi& it = (*p->items)[p->hot];
	if (it.kind != OXMI_CMD) return -1;
	if (it.id < ID_LOOK_THEME0 || it.id >= ID_LOOK_THEME0 + OXZ_THEME_COUNT) return -1;
	return (int)(it.id - ID_LOOK_THEME0);
}

static void SyncTheme(Pop* any)
{
	Pop* root = any && any->root ? any->root : any;
	Pop* leaf = root;
	while (leaf && leaf->child) leaf = leaf->child;
	int id = ThemeUnder(leaf);
	if (id < 0 && leaf != root) id = ThemeUnder(root);
	OxzHoverTheme(id);
}

static int Hit(Pop* p, int x, int y)
{
	if (!p || !p->items) return -1;
	if (x < p->pad || x >= p->pad + p->menuW) return -1;
	if (y < p->pad || y >= p->pad + p->viewH) return -1;
	int rel = y - p->pad + p->scroll;
	int cy = 4;
	for (int i = 0; i < (int)p->items->size(); ++i) {
		int h = RowH((*p->items)[i]);
		if (rel >= cy && rel < cy + h) return (*p->items)[i].kind == OXMI_SEP ? -1 : i;
		cy += h;
	}
	return -1;
}

static void OpenChild(Pop* p, int idx);
static LRESULT CALLBACK PopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static void Layout(Pop* p, int x, int y)
{
	const OxzPal& pal = OxzLook();
	(void)pal;
	p->style = OxzMenuAnim();
	if (p->style < 0 || p->style >= OXZ_ANIM_COUNT) p->style = OXZ_ANIM_AURORA;
	p->pad = PadFor(p->style);
	HDC dc = GetDC(NULL);
	HGDIOBJ old = SelectObject(dc, p->font);
	int maxT = 80;
	for (size_t i = 0; i < p->items->size(); ++i) {
		if ((*p->items)[i].kind == OXMI_SEP) continue;
		SIZE sz = {};
		GetTextExtentPoint32W(dc, (*p->items)[i].text.c_str(), (int)(*p->items)[i].text.size(), &sz);
		if (sz.cx + 8 > maxT) maxT = sz.cx + 8;
	}
	SelectObject(dc, old);
	ReleaseDC(NULL, dc);
	p->menuW = maxT + 36 + 28;
	if (p->menuW < 220) p->menuW = 220;
	if (p->menuW > 460) p->menuW = 460;
	p->contentH = ContentH(*p->items);
	RECT work;
	work.left = 0; work.top = 0; work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN);
	POINT at;
	at.x = x;
	at.y = y;
	HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
	int maxV = work.bottom - work.top - 24;
	if (maxV < kRowH * 3) maxV = kRowH * 3;
	p->viewH = p->contentH;
	if (p->viewH > maxV) p->viewH = maxV;
	p->scrollMax = p->contentH - p->viewH;
	if (p->scrollMax < 0) p->scrollMax = 0;
	p->scroll = 0;
	int wx = x - p->pad;
	int wy = y - p->pad;
	if (wx + p->pad + p->menuW > work.right) wx = work.right - p->menuW - p->pad;
	if (wy + p->pad + p->viewH > work.bottom) wy = work.bottom - p->viewH - p->pad;
	if (wx + p->pad < work.left) wx = work.left - p->pad;
	if (wy + p->pad < work.top) wy = work.top - p->pad;
	p->screenX = wx;
	p->screenY = wy;
}

static void Present(Pop* p)
{
	if (!p || !p->hwnd || !p->items || !p->alive) return;
	int w = p->menuW + p->pad * 2;
	int h = p->viewH + p->pad * 2;
	if (w < 4) w = 4;
	if (h < 4) h = 4;
	EnsureDib(p, w, h);
	if (!p->dib || !p->mem) return;
	const OxzPal& pal = OxzLook();
	RECT all = { 0, 0, w, h };
	HBRUSH wash = CreateSolidBrush(pal.bg0);
	FillRect(p->mem, &all, wash);
	DeleteObject(wash);
	int rtl = OxzLangIsRtl();
	HGDIOBJ oldF = SelectObject(p->mem, p->font);
	SetBkMode(p->mem, TRANSPARENT);
	int panelFade = 255;
	if (p->phase != 0 && p->style == 0) {
		int ox, oy, fade;
		Motion(p, 0, 0, &ox, &oy, &fade);
		panelFade = fade;
	} else if (p->phase != 0 && p->style >= 2) {
		panelFade = 210;
	}
	if (panelFade > 8) {
		RECT panel = { p->pad, p->pad, p->pad + p->menuW, p->pad + p->viewH };
		HBRUSH bg = CreateSolidBrush(pal.bg0);
		FillRect(p->mem, &panel, bg);
		DeleteObject(bg);
		HPEN pen = CreatePen(PS_SOLID, 1, pal.edge);
		HGDIOBJ op = SelectObject(p->mem, pen);
		HGDIOBJ ob = SelectObject(p->mem, GetStockObject(NULL_BRUSH));
		Rectangle(p->mem, panel.left, panel.top, panel.right, panel.bottom);
		SelectObject(p->mem, ob);
		SelectObject(p->mem, op);
		DeleteObject(pen);
		if (OxzSoft()) {
			RECT gloss = panel;
			gloss.bottom = gloss.top + 3;
			HBRUSH hi = CreateSolidBrush(pal.borderLite);
			FillRect(p->mem, &gloss, hi);
			DeleteObject(hi);
		}
		(void)panelFade;
	}
	for (int i = 0; i < (int)p->items->size(); ++i) {
		const OxzMi& it = (*p->items)[i];
		int top = RowTop(*p->items, i);
		int rh = RowH(it);
		int center = top + rh / 2 - p->scroll;
		int ox = 0, oy = 0, fade = 255;
		if (!Motion(p, i, center, &ox, &oy, &fade)) continue;
		int y = p->pad + top - p->scroll + oy;
		int x = p->pad + ox;
		if (y + rh < 0 || y > h || x + p->menuW < 0 || x > w) continue;
		RECT rr = { x, y, x + p->menuW, y + rh };
		int hot = (p->phase == 0 && p->hot == i && it.kind != OXMI_SEP);
		COLORREF bg = hot ? pal.hotTop : pal.face;
		if (it.kind != OXMI_SEP) {
			HBRUSH br = CreateSolidBrush(bg);
			FillRect(p->mem, &rr, br);
			DeleteObject(br);
			RECT rib = rr;
			if (rtl) rib.left = rib.right - 7;
			else rib.right = rib.left + 7;
			HBRUSH rb = CreateSolidBrush(hot ? pal.accent : pal.ribbon0);
			FillRect(p->mem, &rib, rb);
			DeleteObject(rb);
			if (it.checked) {
				HPEN ck = CreatePen(PS_SOLID, 2, pal.accent);
				HGDIOBJ op = SelectObject(p->mem, ck);
				int cx = rtl ? rr.right - 22 : rr.left + 14;
				int cy = rr.top + rh / 2;
				MoveToEx(p->mem, cx - 4, cy, NULL);
				LineTo(p->mem, cx - 1, cy + 4);
				LineTo(p->mem, cx + 5, cy - 4);
				SelectObject(p->mem, op);
				DeleteObject(ck);
			}
			SetTextColor(p->mem, OxzInk(bg));
			RECT tr = rr;
			if (rtl) { tr.right -= 28; tr.left += (it.kind == OXMI_SUB ? 22 : 10); }
			else { tr.left += 28; tr.right -= (it.kind == OXMI_SUB ? 22 : 10); }
			DrawTextW(p->mem, it.text.c_str(), -1, &tr, (rtl ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			if (it.kind == OXMI_SUB) {
				SetTextColor(p->mem, pal.textDim);
				RECT ar = rr;
				if (rtl) ar.right = ar.left + 18;
				else ar.left = ar.right - 18;
				DrawTextW(p->mem, rtl ? L"‹" : L"›", -1, &ar, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
		} else {
			RECT ln = rr;
			ln.top = rr.top + rh / 2;
			ln.bottom = ln.top + 1;
			ln.left += 12;
			ln.right -= 12;
			HBRUSH sb = CreateSolidBrush(pal.sep);
			FillRect(p->mem, &ln, sb);
			DeleteObject(sb);
		}
		(void)fade;
	}
	SelectObject(p->mem, oldF);
	InvalidateRect(p->hwnd, NULL, FALSE);
}

static void OpenChild(Pop* p, int idx)
{
	if (!p || !p->items || idx < 0 || idx >= (int)p->items->size()) return;
	if ((*p->items)[idx].kind != OXMI_SUB) return;
	if (p->child && p->childIndex == idx) return;
	CloseChild(p);
	const OxzMi& it = (*p->items)[idx];
	if (it.sub.empty()) return;
	Pop* c = new Pop();
	memset(c, 0, sizeof(Pop));
	c->host = p->host;
	c->root = p->root ? p->root : p;
	c->parent = p;
	c->items = &it.sub;
	c->asSub = 1;
	c->phase = 1;
	c->animStart = GetTickCount64();
	c->hot = -1;
	c->childIndex = -1;
	c->alive = 1;
	c->font = OxzCreateUiFont(p->hwnd);
	int top = RowTop(*p->items, idx);
	int sx = OxzLangIsRtl() ? p->screenX + p->pad : p->screenX + p->pad + p->menuW;
	int sy = p->screenY + p->pad + top - p->scroll;
	Layout(c, sx, sy);
	c->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"OxzPop", L"", WS_POPUP | WS_VISIBLE,
		c->screenX, c->screenY, c->menuW + c->pad * 2, c->viewH + c->pad * 2, GetParent(p->hwnd) ? GetParent(p->hwnd) : p->hwnd, NULL, GetModuleHandleW(NULL), c);
	if (!c->hwnd) { delete c; return; }
	p->child = c;
	p->childIndex = idx;
	SetTimer(c->hwnd, 1, 15, NULL);
	Present(c);
}

static LRESULT CALLBACK PopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	Pop* p = (Pop*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (msg == WM_NCCREATE) {
		CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
	if (!p) return DefWindowProcW(hwnd, msg, wp, lp);
	switch (msg) {
	case WM_MOUSEMOVE: {
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&tme);
		int settling = (p->phase == 1);
		if (settling) p->phase = 0;
		int hit = Hit(p, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		if (hit != p->hot || settling) {
			p->hot = hit;
			p->hotSince = (hit >= 0 && (*p->items)[hit].kind == OXMI_SUB) ? GetTickCount64() : 0;
			if (p->child && p->childIndex != hit) CloseChild(p);
			SyncTheme(p);
			Present(p);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		if (p->child) return 0;
		if (p->hot != -1) {
			p->hot = -1;
			SyncTheme(p);
			Present(p);
		}
		return 0;
	case WM_MOUSEWHEEL: {
		int d = (short)HIWORD(wp);
		p->scroll += d > 0 ? -36 : 36;
		if (p->scroll < 0) p->scroll = 0;
		if (p->scroll > p->scrollMax) p->scroll = p->scrollMax;
		Present(p);
		return 0;
	}
	case WM_LBUTTONUP: {
		if (p->phase == 1) p->phase = 0;
		int hit = Hit(p, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		if (hit < 0) return 0;
		const OxzMi& it = (*p->items)[hit];
		if (it.kind == OXMI_SUB) OpenChild(p, hit);
		else if (it.kind == OXMI_CMD && it.id && p->root && p->root->host) {
			p->root->host->result = it.id;
			CloseNow(p);
		}
		return 0;
	}
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) {
			if (p->root && p->root->host) p->root->host->result = 0;
			CloseNow(p);
			return 0;
		}
		if (wp == VK_DOWN || wp == VK_UP) {
			int n = (int)p->items->size();
			int i = p->hot;
			for (int k = 0; k < n; ++k) {
				i = wp == VK_DOWN ? i + 1 : i - 1;
				if (i < 0) i = n - 1;
				if (i >= n) i = 0;
				if ((*p->items)[i].kind != OXMI_SEP) { p->hot = i; break; }
			}
			int top = p->hot >= 0 ? RowTop(*p->items, p->hot) : 0;
			if (top < p->scroll) p->scroll = top;
			if (top + kRowH > p->scroll + p->viewH) p->scroll = top + kRowH - p->viewH;
			if (p->scroll < 0) p->scroll = 0;
			if (p->scroll > p->scrollMax) p->scroll = p->scrollMax;
			SyncTheme(p);
			Present(p);
			return 0;
		}
		if (wp == VK_RIGHT || wp == VK_RETURN) {
			if (p->hot >= 0 && (*p->items)[p->hot].kind == OXMI_SUB) OpenChild(p, p->hot);
			else if (wp == VK_RETURN && p->hot >= 0 && (*p->items)[p->hot].kind == OXMI_CMD && p->root && p->root->host) {
				p->root->host->result = (*p->items)[p->hot].id;
				CloseNow(p);
			}
			return 0;
		}
		if (wp == VK_LEFT && p->parent) {
			CloseChild(p->parent);
			SetFocus(p->parent->hwnd);
			return 0;
		}
		return 0;
	case WM_ERASEBKGND: {
		RECT rc;
		GetClientRect(hwnd, &rc);
		HBRUSH br = CreateSolidBrush(OxzLook().bg0);
		FillRect((HDC)wp, &rc, br);
		DeleteObject(br);
		return 1;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		if (p->mem && p->dibW > 0 && p->dibH > 0)
			BitBlt(dc, 0, 0, p->dibW, p->dibH, p->mem, 0, 0, SRCCOPY);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_TIMER:
		if (p->phase == 1) {
			int n = p->items ? (int)p->items->size() : 1;
			int total = InDur(p->style, p->asSub) + Stag(14, n) * (n > 0 ? n : 1) + 40;
			if (p->style == 0 || p->style == 1) total = 240;
			if ((int)(GetTickCount64() - p->animStart) > total) { p->phase = 0; Present(p); }
		}
		if (p == p->root && p->phase == 2) {
			int n = p->items ? (int)p->items->size() : 1;
			int total = OutDur(p->style) + Stag(11, n) * (n > 0 ? n : 1) + 40;
			if (p->style == 0 || p->style == 1) total = 200;
			if (total > 700) total = 700;
			if ((int)(GetTickCount64() - p->animStart) > total) {
				CloseChild(p);
				DestroyWindow(hwnd);
				return 0;
			}
		}
		if (p->phase == 0 && !p->child && p->hot >= 0 && p->hotSince && (*p->items)[p->hot].kind == OXMI_SUB) {
			if (GetTickCount64() - p->hotSince > 180) OpenChild(p, p->hot);
		}
		if (p->phase) Present(p);
		return 0;
	case WM_DESTROY:
		p->alive = 0;
		if (p->mem && p->oldBmp) SelectObject(p->mem, p->oldBmp);
		if (p->dib) DeleteObject(p->dib);
		if (p->mem) DeleteDC(p->mem);
		p->dib = NULL;
		p->mem = NULL;
		p->bits = NULL;
		if (p->font) DeleteObject(p->font);
		p->font = NULL;
		p->hwnd = NULL;
		if (p == p->root && p->host) p->host->closed = 1;
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}

static int Ours(Pop* p, HWND h)
{
	for (; p; p = p->child) if (p->hwnd == h) return 1;
	return 0;
}

UINT OxzTrackPopup(HWND owner, int x, int y, const std::vector<OxzMi>& items)
{
	if (items.empty()) return 0;
	static int reg = 0;
	if (!reg) {
		WNDCLASSW wc;
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = PopProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = L"OxzPop";
		RegisterClassW(&wc);
		reg = 1;
	}
	if (x == -1 && y == -1) {
		POINT pt;
		GetCursorPos(&pt);
		x = pt.x;
		y = pt.y;
	}
	Host host;
	host.closed = 0;
	host.result = 0;
	Pop* p = new Pop();
	memset(p, 0, sizeof(Pop));
	p->host = &host;
	p->root = p;
	p->items = &items;
	p->phase = 1;
	p->animStart = GetTickCount64();
	p->hot = -1;
	p->childIndex = -1;
	p->alive = 1;
	p->font = OxzCreateUiFont(owner);
	Layout(p, x, y);
	p->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"OxzPop", L"", WS_POPUP | WS_VISIBLE,
		p->screenX, p->screenY, p->menuW + p->pad * 2, p->viewH + p->pad * 2, owner, NULL, GetModuleHandleW(NULL), p);
	if (!p->hwnd) { delete p; return 0; }
	SetTimer(p->hwnd, 1, 15, NULL);
	{
		POINT cur;
		RECT wr;
		GetCursorPos(&cur);
		wr.left = p->screenX;
		wr.top = p->screenY;
		wr.right = p->screenX + p->menuW + p->pad * 2;
		wr.bottom = p->screenY + p->viewH + p->pad * 2;
		if (PtInRect(&wr, cur)) p->phase = 0;
	}
	Present(p);
	UpdateWindow(p->hwnd);
	SetFocus(p->hwnd);
	MSG msg;
	while (!host.closed && GetMessageW(&msg, NULL, 0, 0)) {
		if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
		if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
			&& !Ours(p, msg.hwnd)) {
			host.result = 0;
			CloseNow(p);
			continue;
		}
		if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && !Ours(p, msg.hwnd)) {
			host.result = 0;
			CloseNow(p);
			continue;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	UINT id = host.result;
	if (p->hwnd) DestroyWindow(p->hwnd);
	CloseChild(p);
	delete p;
	if (owner && IsWindow(owner)) SetForegroundWindow(owner);
	return id;
}
