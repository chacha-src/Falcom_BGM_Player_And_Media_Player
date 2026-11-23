#include "OxzArc.h"
#include "OxzWork.h"
#include "OxzLang.h"
#include <commctrl.h>
#include <algorithm>

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
	HDC dc = BeginPaint(hwnd, &ps);
	RECT rc;
	GetClientRect(hwnd, &rc);
	HBRUSH bg = CreateSolidBrush(RGB(255, 244, 246));
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
	SetTextColor(dc, RGB(40, 20, 40));
	TextOutW(dc, 16, y, head, (int)wcslen(head));
	y += 28;
	int barX = 16, barW = rc.right - 32;
	if (barW < 40) barW = 40;
	RECT full = { barX, y, barX + barW, y + 18 };
	HBRUSH pale = CreateSolidBrush(RGB(230, 214, 220));
	FillRect(dc, &full, pale);
	DeleteObject(pale);
	if (v->known && v->size) {
		int pw = (int)((unsigned long long)barW * v->packed / v->size);
		if (pw < 2 && v->packed) pw = 2;
		if (pw > barW) pw = barW;
		RECT pk = { barX, y, barX + pw, y + 18 };
		HBRUSH ink = CreateSolidBrush(RGB(90, 150, 120));
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
		HBRUSH pale2 = CreateSolidBrush(RGB(230, 214, 220));
		FillRect(dc, &b, pale2);
		DeleteObject(pale2);
		if (n.size) {
			unsigned long long use = n.packed && n.packed < n.size ? n.packed : n.size;
			int w = (int)(180ull * use / n.size);
			if (w < 1) w = 1;
			RECT p = { left, y + 16, left + w, y + 26 };
			HBRUSH ink = CreateSolidBrush(RGB(120, 170, 150));
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
		SetTextColor(dc, RGB(40, 20, 40));
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
	EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK MapProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	MapView* v = (MapView*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	switch (msg) {
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

static LRESULT CALLBACK TestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_COMMAND:
		if (LOWORD(wp) == 1) DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}

void OxzShowTest(HWND owner, HFONT font, const wchar_t* title, const std::vector<OxzTestRow>& rows)
{
	static int reg = 0;
	if (!reg) {
		WNDCLASSW wc;
		memset(&wc, 0, sizeof(wc));
		wc.lpfnWndProc = TestProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.lpszClassName = L"OxzTest";
		RegisterClassW(&wc);
		reg = 1;
	}
	DWORD ex = (OxzLang() == 7) ? WS_EX_LAYOUTRTL : 0;
	HWND hwnd = CreateWindowExW(ex | WS_EX_DLGMODALFRAME, L"OxzTest", title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 720, 520, owner, NULL, GetModuleHandleW(NULL), NULL);
	if (!hwnd) return;
	HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
		12, 12, 680, 400, hwnd, (HMENU)2, GetModuleHandleW(NULL), NULL);
	HWND closeb = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 560, 424, 132, 32, hwnd, (HMENU)1, NULL, NULL);
	SendMessageW(list, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(closeb, WM_SETFONT, (WPARAM)font, TRUE);
	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	LVCOLUMNW col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT | LVCF_WIDTH;
	col.cx = 420;
	col.pszText = (LPWSTR)OxzTxt(OS_COL_NAME);
	ListView_InsertColumn(list, 0, &col);
	col.cx = 220;
	wchar_t result[64];
	wcsncpy_s(result, OxzTxt(OS_TEST), _TRUNCATE);
	col.pszText = result;
	ListView_InsertColumn(list, 1, &col);
	int bad = 0;
	for (size_t i = 0; i < rows.size(); ++i) {
		LVITEMW it;
		memset(&it, 0, sizeof(it));
		it.mask = LVIF_TEXT;
		it.iItem = (int)i;
		it.pszText = (LPWSTR)rows[i].name.c_str();
		ListView_InsertItem(list, &it);
		wchar_t st[160];
		if (rows[i].ok) wcsncpy_s(st, L"OK", _TRUNCATE);
		else { bad++; wcsncpy_s(st, rows[i].detail.c_str(), _TRUNCATE); }
		ListView_SetItemText(list, (int)i, 1, st);
	}
	wchar_t cap[160];
	_snwprintf_s(cap, _TRUNCATE, L"%s    %u    %d", title ? title : L"", (unsigned)rows.size(), bad);
	SetWindowTextW(hwnd, cap);
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
