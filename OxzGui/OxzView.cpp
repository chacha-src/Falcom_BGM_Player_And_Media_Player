#include "OxzArc.h"
#include "OxzWork.h"
#include "OxzLang.h"
#include "OxzLook.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

static COLORREF MapColor(int i)
{
	static const COLORREF c[] = {
		RGB(70, 130, 200), RGB(60, 160, 110), RGB(210, 130, 60), RGB(160, 80, 170),
		RGB(200, 80, 100), RGB(50, 150, 170), RGB(170, 150, 50), RGB(110, 110, 190)
	};
	return c[i & 7];
}

struct MapView {
	std::wstring title;
	unsigned long long size;
	unsigned long long packed;
	int known;
	std::vector<OxzMapNode> folders;
	std::vector<OxzMapNode> codecs;
	std::vector<OxzMapLink> links;
	std::vector<int> fy;
	std::vector<int> cy;
	int scroll;
	int content;
	HFONT font;
};

static MapView* gMap;

static void MapPaint(HWND hwnd, MapView* v)
{
	PAINTSTRUCT ps;
	HDC win = BeginPaint(hwnd, &ps);
	RECT rc;
	GetClientRect(hwnd, &rc);
	void* plate = NULL;
	HDC dc = win;
	COLORREF wash = OxzPerPixelAcrylic() ? RGB(1, 1, 1) : OxzLook().bg0;
	if (OxzPerPixelAcrylic())
		dc = OxzPlateBegin(&plate, win, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1, wash);
	const OxzPal& pal = OxzLook();
	HBRUSH bg = CreateSolidBrush(wash);
	FillRect(dc, &rc, bg);
	DeleteObject(bg);
	SetBkMode(dc, TRANSPARENT);
	HFONT old = (HFONT)SelectObject(dc, v->font);
	int y = 12 - v->scroll;
	wchar_t head[320];
	if (v->known && v->size) {
		wchar_t a[48], b[48], r[32];
		OxzFormatGrouped(v->size, a, 48);
		OxzFormatGrouped(v->packed, b, 48);
		OxzFormatRatio(v->packed, v->size, r, 32);
		_snwprintf_s(head, _TRUNCATE, L"%s    %s  →  %s    %s", v->title.c_str(), a, b, r);
	} else {
		wchar_t a[48];
		OxzFormatGrouped(v->size, a, 48);
		_snwprintf_s(head, _TRUNCATE, L"%s    %s", v->title.c_str(), a);
	}
	SetTextColor(dc, pal.text);
	TextOutW(dc, 16, y, head, (int)wcslen(head));
	y += 28;
	int barX = 16, barW = rc.right - 32;
	if (barW < 40) barW = 40;
	RECT full = { barX, y, barX + barW, y + 18 };
	HBRUSH pale = CreateSolidBrush(pal.bg1);
	FillRect(dc, &full, pale);
	DeleteObject(pale);
	if (v->known && v->size) {
		int pw = (int)((unsigned long long)barW * v->packed / v->size);
		if (pw < 2 && v->packed) pw = 2;
		if (pw > barW) pw = barW;
		RECT pk = { barX, y, barX + pw, y + 18 };
		HBRUSH ink = CreateSolidBrush(pal.accent);
		FillRect(dc, &pk, ink);
		DeleteObject(ink);
	}
	y += 36;
	int left = 16;
	int right = rc.right - 220;
	if (right < left + 180) right = left + 180;
	int midL = left + 200;
	int midR = right;
	v->fy.assign(v->folders.size(), 0);
	v->cy.assign(v->codecs.size(), 0);
	int fy0 = y;
	for (size_t i = 0; i < v->folders.size(); ++i) {
		v->fy[i] = y + 10;
		const OxzMapNode& n = v->folders[i];
		TextOutW(dc, left, y, n.name.c_str(), (int)n.name.size());
		if (n.size) {
			wchar_t r[32];
			OxzFormatRatio(n.packed, n.size, r, 32);
			TextOutW(dc, left + 186, y + 14, r, (int)wcslen(r));
		}
		RECT b = { left, y + 16, left + 180, y + 26 };
		HBRUSH pale2 = CreateSolidBrush(pal.bg1);
		FillRect(dc, &b, pale2);
		DeleteObject(pale2);
		if (n.size) {
			unsigned long long use = n.packed && n.packed < n.size ? n.packed : n.size;
			int w = (int)(180ull * use / n.size);
			if (w < 1) w = 1;
			RECT p = { left, y + 16, left + w, y + 26 };
			HBRUSH ink = CreateSolidBrush(pal.accent2);
			FillRect(dc, &p, ink);
			DeleteObject(ink);
		}
		y += 36;
	}
	int cy = fy0;
	for (size_t i = 0; i < v->codecs.size(); ++i) {
		v->cy[i] = cy + 10;
		const OxzMapNode& n = v->codecs[i];
		SetTextColor(dc, MapColor((int)i));
		if (n.size) {
			wchar_t line[160], r[32];
			OxzFormatRatio(n.packed, n.size, r, 32);
			_snwprintf_s(line, _TRUNCATE, L"%s  %s", n.name.c_str(), r);
			TextOutW(dc, midR, cy, line, (int)wcslen(line));
		} else {
			TextOutW(dc, midR, cy, n.name.c_str(), (int)n.name.size());
		}
		SetTextColor(dc, pal.text);
		cy += 36;
	}
	for (size_t i = 0; i < v->links.size(); ++i) {
		const OxzMapLink& L = v->links[i];
		if (L.from < 0 || L.from >= (int)v->fy.size() || L.to < 0 || L.to >= (int)v->cy.size()) continue;
		HPEN pen = CreatePen(PS_SOLID, 2, MapColor(L.to));
		HPEN op = (HPEN)SelectObject(dc, pen);
		MoveToEx(dc, midL, v->fy[L.from], NULL);
		LineTo(dc, midR - 8, v->cy[L.to]);
		SelectObject(dc, op);
		DeleteObject(pen);
	}
	v->content = (y > cy ? y : cy) + v->scroll + 24;
	SelectObject(dc, old);
	if (plate) OxzPlateEnd(plate, win, 0, 0, 1);
	EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK MapProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	MapView* v = (MapView*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_ERASEBKGND:
		if (OxzPerPixelAcrylic()) return 1;
		return DefWindowProcW(hwnd, msg, wp, lp);
	case WM_PAINT:
		if (v) MapPaint(hwnd, v);
		return 0;
	case WM_VSCROLL: {
		if (!v) return 0;
		RECT rc;
		GetClientRect(hwnd, &rc);
		int page = rc.bottom > 40 ? rc.bottom : 40;
		int maxs = v->content - page;
		if (maxs < 0) maxs = 0;
		int cmd = LOWORD(wp);
		if (cmd == SB_LINEUP) v->scroll -= 24;
		else if (cmd == SB_LINEDOWN) v->scroll += 24;
		else if (cmd == SB_PAGEUP) v->scroll -= page;
		else if (cmd == SB_PAGEDOWN) v->scroll += page;
		else if (cmd == SB_THUMBTRACK || cmd == SB_THUMBPOSITION) v->scroll = HIWORD(wp);
		if (v->scroll < 0) v->scroll = 0;
		if (v->scroll > maxs) v->scroll = maxs;
		SetScrollPos(hwnd, SB_VERT, v->scroll, TRUE);
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}
	case WM_MOUSEWHEEL:
		if (v) {
			int d = (short)HIWORD(wp);
			SendMessageW(hwnd, WM_VSCROLL, d > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
		}
		return 0;
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}

void OxzShowMap(HWND owner, HFONT font, const wchar_t* title, unsigned long long size, unsigned long long packed, int known, const std::vector<OxzMapNode>& folders, const std::vector<OxzMapNode>& codecs, const std::vector<OxzMapLink>& links)
{
	static int reg = 0;
	if (!reg) {
		WNDCLASSW wc;
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = MapProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.lpszClassName = L"OxzMap";
		RegisterClassW(&wc);
		reg = 1;
	}
	MapView view;
	view.title = title ? title : L"";
	view.size = size;
	view.packed = packed;
	view.known = known;
	view.folders = folders;
	view.codecs = codecs;
	view.links = links;
	view.scroll = 0;
	view.content = 400;
	view.font = font;
	gMap = &view;
	DWORD ex = (OxzLang() == 7) ? WS_EX_LAYOUTRTL : 0;
	HWND hwnd = CreateWindowExW(ex | WS_EX_DLGMODALFRAME, L"OxzMap", title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VSCROLL,
		CW_USEDEFAULT, CW_USEDEFAULT, 760, 560, owner, NULL, GetModuleHandleW(NULL), NULL);
	if (!hwnd) { gMap = NULL; return; }
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&view);
	OxzApplyChrome(hwnd);
	ShowWindow(hwnd, SW_SHOW);
	EnableWindow(owner, FALSE);
	MSG msg;
	while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
	gMap = NULL;
}

struct TestBtn {
	WNDPROC old;
	int hover;
	int down;
};

struct TestUi {
	HFONT font;
	HWND list;
	HWND closeb;
	HWND summary;
	HBRUSH pink;
	WNDPROC listOld;
	int bad;
	unsigned total;
};

static LRESULT CALLBACK TestBtnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	TestBtn* st = (TestBtn*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
	if (msg == WM_MOUSEMOVE) {
		if (!st->hover) {
			TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
			st->hover = 1;
			InvalidateRect(hwnd, NULL, FALSE);
		}
	} else if (msg == WM_MOUSELEAVE) {
		st->hover = 0;
		st->down = 0;
		InvalidateRect(hwnd, NULL, FALSE);
	} else if (msg == WM_LBUTTONDOWN) {
		st->down = 1;
		InvalidateRect(hwnd, NULL, FALSE);
	} else if (msg == WM_LBUTTONUP) {
		st->down = 0;
		InvalidateRect(hwnd, NULL, FALSE);
	} else if (msg == WM_NCDESTROY) {
		WNDPROC old = st->old;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		delete st;
		return CallWindowProcW(old, hwnd, msg, wp, lp);
	}
	return CallWindowProcW(st->old, hwnd, msg, wp, lp);
}

static void TestLayout(HWND hwnd)
{
	TestUi* ui = (TestUi*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	RECT rc;
	if (!ui || !ui->list) return;
	GetClientRect(hwnd, &rc);
	int m = 12;
	int btnW = 132, btnH = 32;
	int sumH = 28;
	int btnX = rc.right - m - btnW;
	int btnY = rc.bottom - m - btnH;
	if (btnX < m) btnX = m;
	if (btnY < sumH + m) btnY = sumH + m;
	MoveWindow(ui->summary, m, 8, rc.right - m * 2, sumH, TRUE);
	MoveWindow(ui->closeb, btnX, btnY, btnW, btnH, TRUE);
	int listTop = 8 + sumH + 6;
	int listH = btnY - 8 - listTop;
	if (listH < 40) listH = 40;
	MoveWindow(ui->list, m, listTop, rc.right - m * 2, listH, TRUE);
	int nameW = rc.right - m * 2 - 280;
	if (nameW < 160) nameW = 160;
	ListView_SetColumnWidth(ui->list, 0, nameW);
}

static void TestPaintCell(TestUi* ui, NMLVCUSTOMDRAW* cd)
{
	int row = (int)cd->nmcd.dwItemSpec;
	int sel = (ListView_GetItemState(ui->list, row, LVIS_SELECTED) & LVIS_SELECTED) ? 1 : 0;
	LVITEMW it;
	memset(&it, 0, sizeof(it));
	it.mask = LVIF_PARAM;
	it.iItem = row;
	int ok = 1;
	if (ListView_GetItem(ui->list, &it)) ok = it.lParam ? 1 : 0;
	const OxzPal& pal = OxzLook();
	COLORREF bg, fg;
	OxzRowColors(row, sel, &bg, &fg);
	if (!ok && !sel) fg = pal.accent;
	RECT rowrc;
	if (!ListView_GetItemRect(ui->list, row, &rowrc, LVIR_BOUNDS)) return;
	if (rowrc.right <= rowrc.left || rowrc.bottom <= rowrc.top) return;
	HWND head = ListView_GetHeader(ui->list);
	int cols = head ? Header_GetItemCount(head) : 1;
	if (cols < 1) cols = 1;
	int saved = SaveDC(cd->nmcd.hdc);
	int x = rowrc.left;
	for (int col = 0; col < cols; ++col) {
		int w = ListView_GetColumnWidth(ui->list, col);
		if (w < 1) continue;
		RECT rc;
		rc.left = x;
		rc.top = rowrc.top;
		rc.right = x + w;
		rc.bottom = rowrc.bottom;
		x += w;
		int clip = SaveDC(cd->nmcd.hdc);
		IntersectClipRect(cd->nmcd.hdc, rc.left, rc.top, rc.right, rc.bottom);
		HBRUSH br = CreateSolidBrush(bg);
		FillRect(cd->nmcd.hdc, &rc, br);
		DeleteObject(br);
		wchar_t text[1024];
		text[0] = 0;
		ListView_GetItemText(ui->list, row, col, text, 1024);
		SetBkMode(cd->nmcd.hdc, TRANSPARENT);
		SetTextColor(cd->nmcd.hdc, col == 1 && ok && !sel ? pal.ribbonEdge : fg);
		if (ui->font) SelectObject(cd->nmcd.hdc, ui->font);
		RECT tr = rc;
		tr.left += 8;
		if (tr.left < rc.right)
			DrawTextW(cd->nmcd.hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		RestoreDC(cd->nmcd.hdc, clip);
	}
	RestoreDC(cd->nmcd.hdc, saved);
}

static void TestPaintHead(NMCUSTOMDRAW* cd)
{
	RECT rc = cd->rc;
	const OxzPal& pal = OxzLook();
	HBRUSH br = CreateSolidBrush(pal.ribbon0);
	FillRect(cd->hdc, &rc, br);
	DeleteObject(br);
	HPEN pen = CreatePen(PS_SOLID, 1, pal.ribbonEdge);
	HGDIOBJ old = SelectObject(cd->hdc, pen);
	MoveToEx(cd->hdc, rc.left, rc.bottom - 1, NULL);
	LineTo(cd->hdc, rc.right, rc.bottom - 1);
	SelectObject(cd->hdc, old);
	DeleteObject(pen);
	HWND head = cd->hdr.hwndFrom;
	HDITEMW item;
	wchar_t text[128];
	text[0] = 0;
	memset(&item, 0, sizeof(item));
	item.mask = HDI_TEXT;
	item.pszText = text;
	item.cchTextMax = 128;
	Header_GetItem(head, (int)cd->dwItemSpec, &item);
	SetBkMode(cd->hdc, TRANSPARENT);
	SetTextColor(cd->hdc, OxzInk(pal.ribbon0));
	rc.left += 8;
	DrawTextW(cd->hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static LRESULT CALLBACK TestListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	TestUi* ui = (TestUi*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
	if (msg == WM_NOTIFY && ui) {
		NMHDR* nm = (NMHDR*)lp;
		if (nm->hwndFrom == ListView_GetHeader(hwnd) && nm->code == NM_CUSTOMDRAW) {
			NMCUSTOMDRAW* cd = (NMCUSTOMDRAW*)lp;
			if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
				if (ui->font) SelectObject(cd->hdc, ui->font);
				TestPaintHead(cd);
				return CDRF_SKIPDEFAULT;
			}
		}
	}
	if (!ui || !ui->listOld) return DefWindowProcW(hwnd, msg, wp, lp);
	return CallWindowProcW(ui->listOld, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK TestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	TestUi* ui = (TestUi*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
	case WM_SIZE:
		TestLayout(hwnd);
		return 0;
	case WM_ERASEBKGND:
		return OxzHostErase(hwnd, (HDC)wp, ui ? ui->pink : NULL);
	case WM_PAINT:
		if (!OxzPerPixelAcrylic()) return DefWindowProcW(hwnd, msg, wp, lp);
		OxzHostPaint(hwnd);
		return 0;
	case WM_CTLCOLORSTATIC: {
		HBRUSH br = OxzHostCtlColor((HDC)wp, msg, ui && ui->pink ? ui->pink : (HBRUSH)GetStockObject(WHITE_BRUSH));
		if (ui && ui->bad) SetTextColor((HDC)wp, OxzLook().accent);
		return (LRESULT)br;
	}
	case WM_DRAWITEM:
		if (lp) OxzPaintButton((const DRAWITEMSTRUCT*)lp, ui ? ui->font : NULL);
		return TRUE;
	case WM_NOTIFY: {
		NMHDR* nm = (NMHDR*)lp;
		if (!ui) return DefWindowProcW(hwnd, msg, wp, lp);
		if (nm->hwndFrom == ui->list && nm->code == NM_CUSTOMDRAW) {
			NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
			if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
				TestPaintCell(ui, cd);
				return CDRF_SKIPDEFAULT;
			}
		}
		if (nm->hwndFrom == ui->list && nm->code == LVN_KEYDOWN) {
			WORD key = ((NMLVKEYDOWN*)lp)->wVKey;
			if (key == VK_ESCAPE || key == VK_RETURN) DestroyWindow(hwnd);
		}
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == 1) DestroyWindow(hwnd);
		return 0;
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE || wp == VK_RETURN) DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		if (ui && ui->pink) { DeleteObject(ui->pink); ui->pink = NULL; }
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

void OxzShowTest(HWND owner, HFONT font, const wchar_t* title, const std::vector<OxzTestRow>& rows)
{
	static int reg = 0;
	static HBRUSH klass = NULL;
	if (!klass) klass = CreateSolidBrush(OxzLook().bg0);
	if (!reg) {
		WNDCLASSW wc;
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = TestProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = klass;
		wc.lpszClassName = L"OxzTest";
		RegisterClassW(&wc);
		reg = 1;
	}
	TestUi ui;
	memset(&ui, 0, sizeof(ui));
	ui.font = font;
	ui.pink = CreateSolidBrush(OxzLook().bg0);
	ui.total = (unsigned)rows.size();
	DWORD ex = (OxzLang() == 7) ? WS_EX_LAYOUTRTL : 0;
	HWND hwnd = CreateWindowExW(ex, L"OxzTest", title ? title : L"",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 760, 540, owner, NULL, GetModuleHandleW(NULL), NULL);
	if (!hwnd) { DeleteObject(ui.pink); return; }
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&ui);
	OxzApplyChrome(hwnd);
	ui.summary = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_LEFTNOWORDWRAP,
		0, 0, 10, 10, hwnd, (HMENU)3, GetModuleHandleW(NULL), NULL);
	ui.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 10, 10, hwnd, (HMENU)2, GetModuleHandleW(NULL), NULL);
	ui.closeb = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
		0, 0, 10, 10, hwnd, (HMENU)1, GetModuleHandleW(NULL), NULL);
	{
		TestBtn* st = new TestBtn();
		st->hover = 0;
		st->down = 0;
		st->old = (WNDPROC)SetWindowLongPtrW(ui.closeb, GWLP_WNDPROC, (LONG_PTR)TestBtnProc);
		SetWindowLongPtrW(ui.closeb, GWLP_USERDATA, (LONG_PTR)st);
	}
	SendMessageW(ui.summary, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(ui.list, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(ui.closeb, WM_SETFONT, (WPARAM)font, TRUE);
	SetWindowTheme(ui.list, L"", L"");
	ui.listOld = (WNDPROC)SetWindowLongPtrW(ui.list, GWLP_WNDPROC, (LONG_PTR)TestListProc);
	ListView_SetExtendedListViewStyle(ui.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	ListView_SetBkColor(ui.list, OxzLook().face);
	ListView_SetTextBkColor(ui.list, OxzLook().face);
	LVCOLUMNW col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT | LVCF_WIDTH;
	col.cx = 420;
	col.pszText = (LPWSTR)OxzTxt(OS_COL_NAME);
	ListView_InsertColumn(ui.list, 0, &col);
	col.cx = 240;
	wchar_t result[64];
	wcsncpy_s(result, OxzTxt(OS_TEST), _TRUNCATE);
	col.pszText = result;
	ListView_InsertColumn(ui.list, 1, &col);
	for (size_t i = 0; i < rows.size(); ++i) {
		LVITEMW it;
		wchar_t st[160];
		memset(&it, 0, sizeof(it));
		it.mask = LVIF_TEXT | LVIF_PARAM;
		it.iItem = (int)i;
		it.pszText = (LPWSTR)rows[i].name.c_str();
		it.lParam = rows[i].ok ? 1 : 0;
		ListView_InsertItem(ui.list, &it);
		if (rows[i].ok) wcsncpy_s(st, L"OK", _TRUNCATE);
		else { ui.bad++; wcsncpy_s(st, rows[i].detail.empty() ? OxzTxt(OS_TEST_FAIL) : rows[i].detail.c_str(), _TRUNCATE); }
		ListView_SetItemText(ui.list, (int)i, 1, st);
	}
	{
		wchar_t items[64], line[240];
		_snwprintf_s(items, _TRUNCATE, OxzTxt(OS_ITEMS), ui.total);
		_snwprintf_s(line, _TRUNCATE, L"%s    %s", items, OxzTxt(ui.bad ? OS_TEST_FAIL : OS_TEST_OK));
		SetWindowTextW(ui.summary, line);
	}
	TestLayout(hwnd);
	OxzBindAcrylic(hwnd);
	EnableWindow(owner, FALSE);
	MSG msg;
	while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0)) {
		if (!IsDialogMessageW(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	EnableWindow(owner, TRUE);
	SetForegroundWindow(owner);
}
