/* oxz 書庫ツール。本体の CCustom とは別の画面。14 言語、ドロップ、右クリック登録。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <ole2.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include "OxzLang.h"
#include "OxzWork.h"
#include "OxzArc.h"
#include "OxzInst.h"
#include "OxzUpdate.h"
#include "OxzAudio.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
	ID_OPEN = 1001, ID_NEW, ID_ADDFILE, ID_ADDFOLDER, ID_DEL, ID_RENAME, ID_EXTRACT, ID_SAVE, ID_SETTINGS,
	ID_LIST, ID_STATUS, ID_PROG,
	ID_COPY = 1301, ID_CUT, ID_PASTE, ID_SELECTALL, ID_REFRESH, ID_UP, ID_OPENSEL,
	ID_SET_LANG = 1101, ID_SET_INSTALL, ID_SET_REMOVE, ID_SET_CLOSE, ID_SET_HELP, ID_SET_STAT
};
enum { JOB_OPEN = 1, JOB_SAVE, JOB_EXTRACT, JOB_PACK, JOB_ADD, JOB_EXTHERE, JOB_EXTRACTTO, JOB_EXTAT, JOB_TEST, JOB_DROPSAVE, JOB_PACKEACH, WM_JOB = WM_APP + 1, WM_START = WM_APP + 5 };

struct Item {
	std::wstring rel;
	std::wstring name;
	int dir;
	int up;
	unsigned long long size;
	unsigned long long packed;
	int packedKnown;
	FILETIME ft;
	FILETIME created;
	DWORD attr;
};
struct Job {
	int kind;
	std::wstring archive;
	std::wstring dest;
	std::wstring work;
	std::vector<std::wstring> inputs;
	int rc;
	unsigned long long bytes;
	std::wstring err;
	int merged;
	int sub;
	int closeAfter;
};
struct OpenWatch {
	std::wstring path;
	FILETIME seen;
	FILETIME base;
	unsigned long long seenSize;
	unsigned long long baseSize;
	DWORD dirtyTick;
};
struct BtnSt { WNDPROC old; int hover; int down; };

static HWND gHwnd, gList, gStatus, gProg, gSet;
static HACCEL gAccel = NULL;
static HFONT gFont;
static HBRUSH gPink, gMint;
static std::wstring gWork, gArchive, gBrowse;
static std::vector<Item> gItems;
static int gHasFile = 0, gSortCol = 0, gSortAsc = 1, gDropHi = -1;
static std::vector<std::wstring> gCut;
static OxzUpdHit* gUpdWait = NULL;
static int gOwned = 0, gBusy = 0, gExit = 0, gCloseWhenDone = 0, gSyncDone = 0, gRefreshing = 0;
static HIMAGELIST gIcons = NULL;
static DWORD gBusyTick = 0;
static std::wstring gBusyText;
static Job* gJob = NULL;
static int gStartKind = 0;
static int gStartSub = 0;
static int gResult = 0;
static std::wstring gStartA, gStartB;
static std::vector<OpenWatch> gWatch;
static int gWatchPause = 0;
static void ClearWatch();
static int LaunchWorkFile(const std::wstring& path);
static void WatchAfterSave(int fail);

static const int kBtnId[] = { ID_OPEN, ID_NEW, ID_ADDFILE, ID_ADDFOLDER, ID_DEL, ID_RENAME, ID_EXTRACT, ID_SAVE, ID_SETTINGS };
static const int kBtnStr[] = { OS_OPEN, OS_NEW, OS_ADDFILE, OS_ADDFOLDER, OS_DEL, OS_RENAME, OS_EXTRACT, OS_SAVE, OS_SETTINGS };

static void Tell(HWND owner, const wchar_t* text, UINT icon)
{
	MessageBoxW(owner, text ? text : L"", OxzTxt(OS_APP), MB_OK | icon);
}

static void SetStatus(const wchar_t* s)
{
	if (gStatus) SetWindowTextW(gStatus, s ? s : L"");
}

static void SetTitle()
{
	std::wstring t = OxzTxt(OS_APP);
	if (!gArchive.empty()) t += L"   " + OxzBaseName(gArchive);
	if (!gBrowse.empty()) t += L"  \\ " + gBrowse;
	if (gHwnd) SetWindowTextW(gHwnd, t.c_str());
}

static void ApplyRtl(HWND hwnd)
{
	if (!hwnd) return;
	LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	if (OxzLangIsRtl()) ex |= WS_EX_LAYOUTRTL;
	else ex &= ~WS_EX_LAYOUTRTL;
	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static void ApplyCaption(HWND hwnd)
{
	COLORREF cap = RGB(255, 182, 210);
	COLORREF text = RGB(80, 20, 50);
	COLORREF border = RGB(216, 132, 176);
	DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
	DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
	DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

static UINT DpiOf(HWND hwnd)
{
	typedef UINT(WINAPI* Fn)(HWND);
	Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	UINT dpi = fn && hwnd ? fn(hwnd) : 96;
	return dpi ? dpi : 96;
}

static void MakeFont(HWND hwnd)
{
	if (gFont) DeleteObject(gFont);
	int h = -MulDiv(11, (int)DpiOf(hwnd), 72);
	gFont = CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static LRESULT CALLBACK BtnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	BtnSt* st = (BtnSt*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
	if (msg == WM_MOUSEMOVE && !st->hover) {
		st->hover = 1;
		TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&t);
		InvalidateRect(hwnd, NULL, FALSE);
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

static void SubBtn(HWND b)
{
	BtnSt* st = new BtnSt();
	st->hover = 0;
	st->down = 0;
	st->old = (WNDPROC)SetWindowLongPtrW(b, GWLP_WNDPROC, (LONG_PTR)BtnProc);
	SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)st);
}

static void DrawBtn(const DRAWITEMSTRUCT* di)
{
	BtnSt* st = (BtnSt*)GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA);
	int dis = (di->itemState & ODS_DISABLED) != 0;
	int hot = st && st->hover && !dis;
	int down = ((di->itemState & ODS_SELECTED) != 0) || (st && st->down);
	COLORREF bg = dis ? RGB(214, 220, 214) : down ? RGB(60, 160, 60) : hot ? RGB(100, 200, 100) : RGB(200, 232, 190);
	COLORREF fg = down && !dis ? RGB(255, 255, 255) : RGB(20, 50, 20);
	HBRUSH br = CreateSolidBrush(bg);
	FillRect(di->hDC, &di->rcItem, br);
	DeleteObject(br);
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 140, 80));
	HGDIOBJ old = SelectObject(di->hDC, pen);
	HGDIOBJ oldB = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
	Rectangle(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
	if (!down && !dis) {
		HPEN gloss = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
		SelectObject(di->hDC, gloss);
		MoveToEx(di->hDC, di->rcItem.left + 2, di->rcItem.top + 2, NULL);
		LineTo(di->hDC, di->rcItem.right - 2, di->rcItem.top + 2);
		SelectObject(di->hDC, pen);
		DeleteObject(gloss);
	}
	SelectObject(di->hDC, oldB);
	SelectObject(di->hDC, old);
	DeleteObject(pen);
	wchar_t text[256];
	GetWindowTextW(di->hwndItem, text, 256);
	SetBkMode(di->hDC, TRANSPARENT);
	SetTextColor(di->hDC, fg);
	SelectObject(di->hDC, gFont);
	DrawTextW(di->hDC, text, -1, (RECT*)&di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static int MeasureText(HWND hwnd, const wchar_t* s)
{
	HDC dc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(dc, gFont);
	SIZE sz = {};
	GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
	SelectObject(dc, old);
	ReleaseDC(hwnd, dc);
	int w = sz.cx + 28;
	return w < 72 ? 72 : w;
}

static void Relayout()
{
	if (!gHwnd || !gList) return;
	RECT rc;
	GetClientRect(gHwnd, &rc);
	int x = 8, y = 8, rowH = 32, maxX = rc.right - 8;
	if (maxX < 200) maxX = 200;
	for (int i = 0; i < 9; ++i) {
		HWND b = GetDlgItem(gHwnd, kBtnId[i]);
		wchar_t text[256];
		GetWindowTextW(b, text, 256);
		int w = MeasureText(gHwnd, text);
		if (x > 8 && x + w > maxX) { x = 8; y += rowH + 6; }
		MoveWindow(b, x, y, w, rowH, TRUE);
		x += w + 6;
	}
	int top = y + rowH + 8;
	int foot = gBusy ? 116 : 56;
	int listH = rc.bottom - top - foot;
	if (listH < 40) listH = 40;
	MoveWindow(gList, 8, top, rc.right - 16, listH, TRUE);
	if (gProg) {
		if (gBusy) {
			MoveWindow(gProg, 8, rc.bottom - 108, rc.right - 16, 16, TRUE);
			ShowWindow(gProg, SW_SHOW);
		} else {
			ShowWindow(gProg, SW_HIDE);
		}
	}
	MoveWindow(gStatus, 8, gBusy ? rc.bottom - 88 : rc.bottom - 48, rc.right - 16, gBusy ? 80 : 40, TRUE);
}

static void Relabel()
{
	if (!gHwnd) return;
	for (int i = 0; i < 9; ++i)
		SetWindowTextW(GetDlgItem(gHwnd, kBtnId[i]), OxzTxt(kBtnStr[i]));
	if (gList) {
		LVCOLUMNW col;
		memset(&col, 0, sizeof(col));
		col.mask = LVCF_TEXT;
		const int ids[] = { OS_COL_NAME, OS_COL_SIZE, OS_COL_HUMAN, OS_COL_PACKED, OS_COL_PACKHUMAN, OS_COL_RATIO, OS_COL_TYPE, OS_COL_ATTR, OS_COL_DATE, OS_COL_CREATED };
		for (int i = 0; i < 10; ++i) {
			col.pszText = (LPWSTR)OxzTxt(ids[i]);
			ListView_SetColumn(gList, i, &col);
		}
	}
	SetTitle();
	ApplyRtl(gHwnd);
	ApplyCaption(gHwnd);
	Relayout();
	if (!gBusy) SetStatus(OxzTxt(OS_HINT));
}

static void ClearWatch()
{
	gWatch.clear();
	gWatchPause = 0;
	if (gHwnd) KillTimer(gHwnd, 3);
}

static void DropWork()
{
	ClearWatch();
	if (gOwned && !gWork.empty()) OxzRmTree(gWork);
	gWork.clear();
	gOwned = 0;
	gBrowse.clear();
	gHasFile = 0;
	gItems.clear();
}

static std::wstring LeafOf(const std::wstring& rel)
{
	size_t s = rel.find_last_of(L'\\');
	return s == std::wstring::npos ? rel : rel.substr(s + 1);
}

static int CmpItem(const Item& a, const Item& b)
{
	int c = 0;
	if (gSortCol == 0 && a.dir != b.dir) return a.dir > b.dir ? -1 : 1;
	if (gSortCol == 1 || gSortCol == 2 || gSortCol == 3 || gSortCol == 4) {
		unsigned long long av = (gSortCol == 1 || gSortCol == 2) ? a.size : a.packed;
		unsigned long long bv = (gSortCol == 1 || gSortCol == 2) ? b.size : b.packed;
		c = av < bv ? -1 : av > bv ? 1 : 0;
	} else if (gSortCol == 8 || gSortCol == 9) {
		const FILETIME& af = gSortCol == 9 ? a.created : a.ft;
		const FILETIME& bf = gSortCol == 9 ? b.created : b.ft;
		c = CompareFileTime(&af, &bf);
	} else {
		c = _wcsicmp(a.name.c_str(), b.name.c_str());
	}
	if (c == 0) c = _wcsicmp(a.name.c_str(), b.name.c_str());
	if (!gSortAsc) c = -c;
	return c;
}

static bool ItemLess(const Item& a, const Item& b)
{
	return CmpItem(a, b) < 0;
}

static void Walk(std::vector<Item>& all, const std::wstring& dir, const std::wstring& rel)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		Item it;
		memset(&it.ft, 0, sizeof(it.ft));
		memset(&it.created, 0, sizeof(it.created));
		it.rel = rel.empty() ? fd.cFileName : (rel + L"\\" + fd.cFileName);
		it.name = fd.cFileName;
		it.ft = fd.ftLastWriteTime;
		it.created = fd.ftCreationTime;
		it.attr = fd.dwFileAttributes;
		it.up = 0;
		it.packed = 0;
		it.packedKnown = 0;
		std::wstring child = dir + L"\\" + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			it.dir = 1;
			it.size = 0;
			all.push_back(it);
			Walk(all, child, it.rel);
		} else {
			ULARGE_INTEGER u;
			it.dir = 0;
			u.LowPart = fd.nFileSizeLow;
			u.HighPart = fd.nFileSizeHigh;
			it.size = u.QuadPart;
			all.push_back(it);
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static int InBrowse(const std::wstring& rel)
{
	if (gBrowse.empty()) return rel.find(L'\\') == std::wstring::npos;
	std::wstring prefix = gBrowse + L"\\";
	if (rel.size() <= prefix.size() || _wcsnicmp(rel.c_str(), prefix.c_str(), prefix.size()) != 0) return 0;
	return rel.find(L'\\', prefix.size()) == std::wstring::npos;
}

static void EnsureBrowse()
{
	while (!gBrowse.empty() && !gWork.empty()) {
		DWORD a = GetFileAttributesW((gWork + L"\\" + gBrowse).c_str());
		if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return;
		size_t s = gBrowse.find_last_of(L'\\');
		if (s == std::wstring::npos) gBrowse.clear();
		else gBrowse.resize(s);
	}
}

static void FormatWhen(const FILETIME& ft, wchar_t* when, size_t cch)
{
	SYSTEMTIME st;
	FILETIME lt;
	when[0] = 0;
	if (!ft.dwLowDateTime && !ft.dwHighDateTime) return;
	if (FileTimeToLocalFileTime(&ft, &lt) && FileTimeToSystemTime(&lt, &st))
		_snwprintf_s(when, cch, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

static void FormatUnit(unsigned long long n, wchar_t* out, size_t cch)
{
	static const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
	double v = (double)n;
	int i = 0;
	while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
	if (i == 0) _snwprintf_s(out, cch, _TRUNCATE, L"%llu B", n);
	else if (v >= 100.0) _snwprintf_s(out, cch, _TRUNCATE, L"%.0f %s", v, u[i]);
	else if (v >= 10.0) _snwprintf_s(out, cch, _TRUNCATE, L"%.1f %s", v, u[i]);
	else _snwprintf_s(out, cch, _TRUNCATE, L"%.2f %s", v, u[i]);
}

static void FormatAttr(DWORD attr, wchar_t* out)
{
	int n = 0;
	if (attr & FILE_ATTRIBUTE_ARCHIVE) out[n++] = L'A';
	if (attr & FILE_ATTRIBUTE_READONLY) out[n++] = L'R';
	if (attr & FILE_ATTRIBUTE_HIDDEN) out[n++] = L'H';
	if (attr & FILE_ATTRIBUTE_SYSTEM) out[n++] = L'S';
	out[n] = 0;
}

static int EndsName(const std::wstring& name, const wchar_t* ext)
{
	size_t n = wcslen(ext);
	return name.size() >= n && _wcsicmp(name.c_str() + name.size() - n, ext) == 0;
}

static int RealIconName(const std::wstring& name)
{
	return EndsName(name, L".exe") || EndsName(name, L".dll") || EndsName(name, L".scr") || EndsName(name, L".ico") || EndsName(name, L".cpl") || EndsName(name, L".ocx");
}

static int AddShellIcon(HIMAGELIST list, const wchar_t* path, DWORD attr, int real, std::map<std::wstring, int>& cache, const std::wstring& key)
{
	std::map<std::wstring, int>::iterator it = cache.find(key);
	if (it != cache.end()) return it->second;
	SHFILEINFOW sfi;
	memset(&sfi, 0, sizeof(sfi));
	UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
	if (!real) flags |= SHGFI_USEFILEATTRIBUTES;
	SHGetFileInfoW(path, real ? 0 : attr, &sfi, sizeof(sfi), flags);
	int idx = -1;
	if (sfi.hIcon) {
		idx = ImageList_AddIcon(list, sfi.hIcon);
		DestroyIcon(sfi.hIcon);
	}
	cache[key] = idx;
	return idx;
}

static int IconForRow(const Item& row, HIMAGELIST list, std::map<std::wstring, int>& cache)
{
	if (!list) return -1;
	if (row.up || row.dir)
		return AddShellIcon(list, L"folder", FILE_ATTRIBUTE_DIRECTORY, 0, cache, L"\\dir");
	if (RealIconName(row.name) && !gWork.empty()) {
		std::wstring path = gWork + L"\\" + row.rel;
		std::wstring key = path;
		for (size_t i = 0; i < key.size(); ++i)
			if (key[i] >= L'A' && key[i] <= L'Z') key[i] = (wchar_t)(key[i] - L'A' + L'a');
		int own = AddShellIcon(list, path.c_str(), 0, 1, cache, key);
		if (own >= 0) return own;
	}
	std::wstring key = L".";
	size_t dot = row.name.find_last_of(L'.');
	if (dot != std::wstring::npos) key = row.name.substr(dot);
	for (size_t i = 0; i < key.size(); ++i)
		if (key[i] >= L'A' && key[i] <= L'Z') key[i] = (wchar_t)(key[i] - L'A' + L'a');
	return AddShellIcon(list, row.name.c_str(), FILE_ATTRIBUTE_NORMAL, 0, cache, key);
}

static void UpdateStatus()
{
	if (gBusy) return;
	unsigned n = 0, sel = 0;
	for (size_t i = 0; i < gItems.size(); ++i) if (!gItems[i].up) n++;
	int i = -1;
	while (gList && (i = ListView_GetNextItem(gList, i, LVNI_SELECTED)) >= 0)
		if (i < (int)gItems.size() && !gItems[i].up) sel++;
	wchar_t msg[160];
	_snwprintf_s(msg, _TRUNCATE, OxzTxt(OS_ITEMS), n);
	std::wstring s = gBrowse.empty() ? msg : (gBrowse + L"    " + msg);
	if (sel) {
		wchar_t b[64];
		_snwprintf_s(b, _TRUNCATE, OxzTxt(OS_SELECTED), sel);
		s += L"    ";
		s += b;
	}
	SetStatus(s.c_str());
}

static void Refresh()
{
	std::vector<Item> all;
	gRefreshing = 1;
	ListView_DeleteAllItems(gList);
	gItems.clear();
	gHasFile = 0;
	if (!gWork.empty()) Walk(all, gWork, L"");
	unsigned long long raw = 0, arcSz = 0;
	int known = 0;
	for (size_t i = 0; i < all.size(); ++i) {
		if (all[i].dir) continue;
		raw += all[i].size;
		gHasFile = 1;
	}
	if (!gArchive.empty() && raw) {
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (GetFileAttributesExW(gArchive.c_str(), GetFileExInfoStandard, &fa)) {
			ULARGE_INTEGER u;
			u.LowPart = fa.nFileSizeLow;
			u.HighPart = fa.nFileSizeHigh;
			arcSz = u.QuadPart;
			known = 1;
		}
	}
	for (size_t i = 0; i < all.size(); ++i) {
		if (all[i].dir || !known) continue;
		all[i].packed = (all[i].size * arcSz + raw / 2) / raw;
		all[i].packedKnown = 1;
	}
	for (size_t i = 0; i < all.size(); ++i) {
		if (!all[i].dir) continue;
		unsigned long long sum = 0, sumSz = 0;
		std::wstring prefix = all[i].rel + L"\\";
		for (size_t k = 0; k < all.size(); ++k) {
			if (all[k].dir) continue;
			if (all[k].rel.size() > prefix.size() && _wcsnicmp(all[k].rel.c_str(), prefix.c_str(), prefix.size()) == 0) {
				sumSz += all[k].size;
				if (all[k].packedKnown) sum += all[k].packed;
			}
		}
		all[i].size = sumSz;
		all[i].packed = sum;
		all[i].packedKnown = known;
	}
	EnsureBrowse();
	for (size_t i = 0; i < all.size(); ++i)
		if (InBrowse(all[i].rel)) gItems.push_back(all[i]);
	std::sort(gItems.begin(), gItems.end(), ItemLess);
	if (!gBrowse.empty()) {
		Item up;
		memset(&up.ft, 0, sizeof(up.ft));
		memset(&up.created, 0, sizeof(up.created));
		up.name = L"..";
		up.rel = gBrowse;
		up.dir = 1;
		up.up = 1;
		up.size = 0;
		up.packed = 0;
		up.packedKnown = 0;
		up.attr = 0;
		gItems.insert(gItems.begin(), up);
	}
	int cx = GetSystemMetrics(SM_CXSMICON);
	int cy = GetSystemMetrics(SM_CYSMICON);
	if (cx < 16) cx = 16;
	if (cy < 16) cy = 16;
	HIMAGELIST neu = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 16, 16);
	std::map<std::wstring, int> icons;
	for (size_t i = 0; i < gItems.size(); ++i) {
		LVITEMW it;
		wchar_t sz[64], human[64], pk[64], ph[64], ratio[32], kind[64], attr[8], when[64], made[64];
		const Item& row = gItems[i];
		memset(&it, 0, sizeof(it));
		it.mask = LVIF_TEXT | LVIF_IMAGE;
		it.iItem = (int)i;
		it.iImage = IconForRow(row, neu, icons);
		it.pszText = (LPWSTR)row.name.c_str();
		ListView_InsertItem(gList, &it);
		sz[0] = human[0] = pk[0] = ph[0] = ratio[0] = kind[0] = attr[0] = when[0] = made[0] = 0;
		if (!row.up) {
			_snwprintf_s(sz, _TRUNCATE, L"%llu", row.size);
			FormatUnit(row.size, human, 64);
			if (!row.packedKnown) { wcscpy_s(pk, L"-"); wcscpy_s(ph, L"-"); wcscpy_s(ratio, L"-"); }
			else {
				_snwprintf_s(pk, _TRUNCATE, L"~%llu", row.packed);
				FormatUnit(row.packed, ph, 64);
				if (ph[0]) {
					size_t n = wcslen(ph);
					if (n + 2 < 64) { memmove(ph + 1, ph, (n + 1) * sizeof(wchar_t)); ph[0] = L'~'; }
				}
				if (row.size) _snwprintf_s(ratio, _TRUNCATE, L"%llu%%", (row.packed * 100 + row.size / 2) / row.size);
				else wcscpy_s(ratio, L"-");
			}
			if (row.dir) wcscpy_s(kind, OxzTxt(OS_FOLDER));
			else {
				size_t dot = row.name.find_last_of(L'.');
				if (dot != std::wstring::npos && dot + 1 < row.name.size()) {
					wcsncpy_s(kind, row.name.c_str() + dot + 1, _TRUNCATE);
					_wcsupr_s(kind);
				} else wcscpy_s(kind, L"-");
			}
			FormatAttr(row.attr, attr);
			FormatWhen(row.ft, when, 64);
			FormatWhen(row.created, made, 64);
		}
		ListView_SetItemText(gList, (int)i, 1, sz);
		ListView_SetItemText(gList, (int)i, 2, human);
		ListView_SetItemText(gList, (int)i, 3, pk);
		ListView_SetItemText(gList, (int)i, 4, ph);
		ListView_SetItemText(gList, (int)i, 5, ratio);
		ListView_SetItemText(gList, (int)i, 6, kind);
		ListView_SetItemText(gList, (int)i, 7, attr);
		ListView_SetItemText(gList, (int)i, 8, when);
		ListView_SetItemText(gList, (int)i, 9, made);
	}
	HIMAGELIST old = ListView_SetImageList(gList, neu, LVSIL_SMALL);
	if (old) ImageList_Destroy(old);
	gIcons = neu;
	gRefreshing = 0;
	if (!gBusy) UpdateStatus();
	SetTitle();
}

static std::wstring BrowseDir()
{
	if (gWork.empty()) return L"";
	if (gBrowse.empty()) return gWork;
	return gWork + L"\\" + gBrowse;
}

static std::wstring ParentDir()
{
	if (gWork.empty()) return L"";
	size_t s = gBrowse.find_last_of(L'\\');
	if (gBrowse.empty() || s == std::wstring::npos) return gWork;
	return gWork + L"\\" + gBrowse.substr(0, s);
}

static std::wstring DirForRow(int sel)
{
	if (gWork.empty()) return L"";
	if (sel < 0 || sel >= (int)gItems.size()) return BrowseDir();
	const Item& it = gItems[sel];
	if (it.up) return ParentDir();
	if (it.dir) return gWork + L"\\" + it.rel;
	return BrowseDir();
}

static void SelectByName(const std::wstring& name)
{
	for (int i = 0; i < (int)gItems.size(); ++i) {
		if (_wcsicmp(gItems[i].name.c_str(), name.c_str()) != 0) continue;
		ListView_SetItemState(gList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_SetItemState(gList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_EnsureVisible(gList, i, FALSE);
		return;
	}
}

static void GoUp()
{
	if (gBusy || gBrowse.empty()) return;
	size_t s = gBrowse.find_last_of(L'\\');
	std::wstring child = s == std::wstring::npos ? gBrowse : gBrowse.substr(s + 1);
	if (s == std::wstring::npos) gBrowse.clear();
	else gBrowse.resize(s);
	Refresh();
	SelectByName(child);
}

static void EnterRow(int row)
{
	if (gBusy || row < 0 || row >= (int)gItems.size()) return;
	if (gItems[row].up) { GoUp(); return; }
	if (!gItems[row].dir) {
		if (gWork.empty()) return;
		if (!LaunchWorkFile(gWork + L"\\" + gItems[row].rel)) Tell(gHwnd, OxzTxt(OS_OPEN_FAIL), MB_ICONWARNING);
		return;
	}
	gBrowse = gItems[row].rel;
	Refresh();
	int focus = (!gItems.empty() && gItems[0].up && gItems.size() > 1) ? 1 : 0;
	if (focus < (int)gItems.size()) {
		ListView_SetItemState(gList, focus, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
		ListView_EnsureVisible(gList, focus, FALSE);
	}
}

static int UnderWork(const std::wstring& path)
{
	if (gWork.empty() || path.size() < gWork.size()) return 0;
	if (_wcsnicmp(path.c_str(), gWork.c_str(), gWork.size()) != 0) return 0;
	return path.size() == gWork.size() || path[gWork.size()] == L'\\';
}

static void SetBusy(int on, const wchar_t* text)
{
	gBusy = on;
	gBusyText = text ? text : L"";
	gBusyTick = GetTickCount();
	for (int i = 0; i < 9; ++i)
		EnableWindow(GetDlgItem(gHwnd, kBtnId[i]), on ? FALSE : TRUE);
	if (gProg) SendMessageW(gProg, PBM_SETPOS, 0, 0);
	if (on) {
		SetStatus(gBusyText.c_str());
		SetTimer(gHwnd, 1, 200, NULL);
	} else {
		KillTimer(gHwnd, 1);
	}
	Relayout();
}

static std::wstring FolderOf(const std::wstring& oxz)
{
	std::wstring parent = OxzParent(oxz);
	std::wstring name = OxzBaseName(oxz);
	if (OxzArchiveKind(name.c_str())) {
		size_t dot = name.find_last_of(L'.');
		if (dot != std::wstring::npos && dot > 0) name.resize(dot);
		if (name.size() >= 4 && _wcsicmp(name.c_str() + name.size() - 4, L".tar") == 0) name.resize(name.size() - 4);
	}
	if (name.empty()) name = L"oxz";
	if (!parent.empty() && parent.back() != L'\\') parent += L"\\";
	return parent + name;
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
	if (dir.empty()) return name;
	if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
	return dir + L"\\" + name;
}

static std::wstring StemOf(const std::wstring& path)
{
	std::wstring name = OxzBaseName(path);
	if (OxzArchiveKind(name.c_str())) {
		size_t dot = name.find_last_of(L'.');
		if (dot != std::wstring::npos && dot > 0) name.resize(dot);
		if (name.size() >= 4 && _wcsicmp(name.c_str() + name.size() - 4, L".tar") == 0) name.resize(name.size() - 4);
	}
	if (name.empty()) name = L"oxz";
	return name;
}

static std::wstring OxzSaveName(const std::wstring& path)
{
	std::wstring parent = OxzParent(path);
	std::wstring name = StemOf(path);
	if (name.empty()) name = L"oxz";
	if (!parent.empty() && parent.back() != L'\\') parent += L"\\";
	return parent + name + L".oxz";
}

static void OxzSlice(int k, int n, int& base, int& span)
{
	if (n <= 1) { base = 0; span = 100; return; }
	base = (int)((long long)k * 100 / n);
	int next = (k + 1 >= n) ? 100 : (int)((long long)(k + 1) * 100 / n);
	if (next < base) next = base;
	span = next - base;
	if (span < 1 && base < 100) span = 1;
}

static int CanOpen(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) return 0;
	return OxzArchiveKind(path.c_str()) != 0;
}

static std::wstring AvoidDest(const std::wstring& dest, const std::vector<std::wstring>& inputs);

static std::wstring UniqueAmong(std::wstring dest, std::vector<std::wstring>& used, const wchar_t* ext)
{
	auto taken = [&](const std::wstring& d) {
		for (size_t i = 0; i < used.size(); ++i)
			if (OxzSamePath(used[i], d)) return 1;
		return 0;
	};
	std::wstring pick = dest;
	if (taken(pick)) {
		std::wstring stem = dest;
		size_t el = ext ? wcslen(ext) : 0;
		if (el && stem.size() >= el && _wcsicmp(stem.c_str() + stem.size() - el, ext) == 0)
			stem.resize(stem.size() - el);
		for (int n = 2; n < 100; ++n) {
			wchar_t b[16];
			_snwprintf_s(b, _TRUNCATE, L"_%d", n);
			pick = stem + b;
			if (el) pick += ext;
			if (!taken(pick)) break;
		}
	}
	used.push_back(pick);
	return pick;
}

static std::wstring OneOxzIn(const std::wstring& folder, const std::wstring& path)
{
	std::wstring name = OxzBaseName(path);
	DWORD a = GetFileAttributesW(path.c_str());
	if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
		size_t dot = name.find_last_of(L'.');
		if (dot != std::wstring::npos && dot > 0) name.resize(dot);
	}
	if (name.empty()) name = L"oxz";
	return JoinPath(folder, name + L".oxz");
}

static DWORD WINAPI JobThread(LPVOID p)
{
	Job* j = (Job*)p;
	j->rc = 2;
	j->bytes = 0;
	if ((j->kind == JOB_SAVE || j->kind == JOB_PACK || j->kind == JOB_ADD || j->kind == JOB_DROPSAVE || j->kind == JOB_PACKEACH) && !OxzHas7za()) {
		j->err = OxzTxt(OS_NO7ZA);
		PostMessageW(gHwnd, WM_JOB, 0, 0);
		return 0;
	}
	if (j->kind == JOB_OPEN || j->kind == JOB_EXTRACT) {
		OxzProgBegin(j->archive.c_str());
		j->rc = OxzExtractTo(j->archive.c_str(), j->dest.c_str(), j->err);
	} else if (j->kind == JOB_SAVE) {
		OxzProgBegin(j->archive.c_str());
		j->rc = OxzPackWork(j->work, j->archive, j->bytes, j->err);
	} else if (j->kind == JOB_PACK || j->kind == JOB_ADD) {
		std::wstring existing;
		OxzProgBegin(j->archive.c_str());
		if (j->kind == JOB_ADD) existing = j->archive;
		else if (GetFileAttributesW(j->archive.c_str()) != INVALID_FILE_ATTRIBUTES) existing = j->archive;
		j->merged = existing.empty() ? 0 : 1;
		j->rc = OxzMergePack(existing, j->inputs, j->archive, j->bytes, j->err);
		if (j->rc == 0 && !j->closeAfter) {
			std::wstring view = OxzNewWork();
			std::wstring ignore;
			if (OxzExtractTo(j->archive.c_str(), view.c_str(), ignore) == 0) j->dest = view;
			else OxzRmTree(view);
		}
	} else if (j->kind == JOB_EXTHERE || j->kind == JOB_EXTRACTTO) {
		j->rc = 0;
		int any = 0, total = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i)
			if (CanOpen(j->inputs[i])) total++;
		OxzProgBegin(total ? j->inputs[0].c_str() : L"");
		std::vector<std::wstring> usedDir;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			if (!CanOpen(j->inputs[i])) continue;
			std::wstring dest = (j->kind == JOB_EXTHERE) ? OxzParent(j->inputs[i]) : UniqueAmong(FolderOf(j->inputs[i]), usedDir, L"");
			std::wstring e;
			int base = 0, span = 100;
			OxzSlice(any, total ? total : 1, base, span);
			int rc = OxzExtractTo(j->inputs[i].c_str(), dest.c_str(), e, base, span);
			any++;
			if (rc) { j->rc = rc; j->err = e; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_OPEN_FAIL); }
	} else if (j->kind == JOB_EXTAT) {
		j->rc = 0;
		int any = 0, total = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (!CanOpen(j->inputs[i]) || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			total++;
		}
		OxzProgBegin(j->dest.c_str());
		std::vector<std::wstring> usedAt;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (!CanOpen(j->inputs[i]) || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			std::wstring dest = j->sub ? UniqueAmong(JoinPath(j->dest, StemOf(j->inputs[i])), usedAt, L"") : j->dest;
			std::wstring e;
			int base = 0, span = 100;
			OxzSlice(any, total ? total : 1, base, span);
			int rc = OxzExtractTo(j->inputs[i].c_str(), dest.c_str(), e, base, span);
			any++;
			if (rc) { j->rc = rc; j->err = e; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_OPEN_FAIL); }
	} else if (j->kind == JOB_TEST) {
		j->rc = 0;
		int any = 0, total = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (!CanOpen(j->inputs[i]) || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			total++;
		}
		OxzProgBegin(total ? j->inputs[0].c_str() : L"");
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (!CanOpen(j->inputs[i]) || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			std::wstring e;
			int base = 0, span = 100;
			OxzSlice(any, total ? total : 1, base, span);
			int rc = OxzTest(j->inputs[i].c_str(), e, base, span);
			any++;
			if (rc) { j->rc = rc; j->err = e; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_TEST_FAIL); }
	} else if (j->kind == JOB_DROPSAVE) {
		j->rc = OxzSaveAdded(j->work, j->dest, j->inputs, j->archive, j->bytes, j->err);
	} else if (j->kind == JOB_PACKEACH) {
		j->rc = 0;
		int any = 0;
		int total = (int)j->inputs.size();
		std::vector<std::wstring> used;
		OxzProgBegin(total ? j->inputs[0].c_str() : L"");
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (a == INVALID_FILE_ATTRIBUTES) continue;
			std::vector<std::wstring> one(1, j->inputs[i]);
			std::wstring dest = j->dest.empty() ? OxzDestFor(one) : OneOxzIn(j->dest, j->inputs[i]);
			dest = UniqueAmong(AvoidDest(dest, one), used, L".oxz");
			std::wstring e;
			unsigned long long bytes = 0;
			int rc = OxzMergePack(L"", one, dest, bytes, e);
			j->bytes += bytes;
			any++;
			if (rc) { j->rc = rc; j->err = e; break; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_NOTHING); }
	}
	PostMessageW(gHwnd, WM_JOB, 0, 0);
	return 0;
}

static Job* NewJob()
{
	Job* j = new Job();
	j->kind = 0;
	j->rc = 2;
	j->bytes = 0;
	j->merged = 0;
	j->sub = 0;
	j->closeAfter = 0;
	return j;
}

static void StartJob(Job* j)
{
	gJob = j;
	HANDLE th = CreateThread(NULL, 0, JobThread, j, 0, NULL);
	if (!th) {
		j->rc = 2;
		j->err = OxzTxt(OS_THREAD_FAIL);
		PostMessageW(gHwnd, WM_JOB, 0, 0);
		return;
	}
	CloseHandle(th);
}

static void OnOpenPath(const std::wstring& arc)
{
	std::wstring work = OxzNewWork();
	SetBusy(1, OxzTxt(OS_EXTRACTING));
	Job* j = NewJob();
	j->kind = JOB_OPEN;
	j->archive = arc;
	j->dest = work;
	DropWork();
	ListView_DeleteAllItems(gList);
	gWork = work;
	gOwned = 1;
	gArchive = arc;
	StartJob(j);
}

static std::vector<wchar_t> MakeFilter(const wchar_t* a, const wchar_t* as, const wchar_t* b, const wchar_t* bs)
{
	std::vector<wchar_t> v;
	auto add = [&](const wchar_t* s) { while (*s) v.push_back(*s++); v.push_back(0); };
	add(a); add(as);
	if (b) { add(b); add(bs); }
	v.push_back(0);
	return v;
}

static int PickFiles(std::vector<std::wstring>& out)
{
	wchar_t buf[65536];
	OPENFILENAMEW ofn;
	buf[0] = 0;
	std::vector<wchar_t> filt = MakeFilter(OxzTxt(OS_ALLFILES), L"*.*", NULL, NULL);
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = gHwnd;
	ofn.lpstrFilter = &filt[0];
	ofn.lpstrFile = buf;
	ofn.nMaxFile = 65536;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
	if (!GetOpenFileNameW(&ofn)) return 0;
	const wchar_t* p = buf;
	std::wstring dir = p;
	p += wcslen(p) + 1;
	if (*p == 0) { out.push_back(dir); return 1; }
	while (*p) { out.push_back(dir + L"\\" + p); p += wcslen(p) + 1; }
	return (int)out.size();
}

static std::vector<wchar_t> MakeOpenFilter()
{
	std::vector<wchar_t> v;
	auto add = [&](const wchar_t* s) { while (*s) v.push_back(*s++); v.push_back(0); };
	add(OxzTxt(OS_OPEN_ARCH));
	add(L"*.oxz;*.7z;*.zip;*.rar;*.gz;*.tgz;*.tar;*.bz2;*.xz;*.cab;*.iso;*.wim");
	add(OxzTxt(OS_OXZFILTER));
	add(L"*.oxz");
	add(OxzTxt(OS_ALLFILES));
	add(L"*.*");
	v.push_back(0);
	return v;
}

static int PickArchive(std::wstring& path, int save)
{
	wchar_t buf[MAX_PATH];
	OPENFILENAMEW ofn;
	buf[0] = 0;
	if (!gArchive.empty() && save) {
		std::wstring sug = OxzIsOxz(gArchive) ? gArchive : OxzSaveName(gArchive);
		wcsncpy_s(buf, sug.c_str(), _TRUNCATE);
	}
	std::vector<wchar_t> filt = save ? MakeFilter(OxzTxt(OS_OXZFILTER), L"*.oxz", OxzTxt(OS_ALLFILES), L"*.*") : MakeOpenFilter();
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = gHwnd;
	ofn.lpstrFilter = &filt[0];
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"oxz";
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	if (!ok) return 0;
	path = buf;
	return 1;
}

static int PickFolder(const wchar_t* title, std::wstring& path)
{
	BROWSEINFOW bi;
	wchar_t disp[MAX_PATH];
	memset(&bi, 0, sizeof(bi));
	bi.hwndOwner = gHwnd;
	bi.lpszTitle = title;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
	PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
	if (!pidl) return 0;
	disp[0] = 0;
	SHGetPathFromIDListW(pidl, disp);
	CoTaskMemFree(pidl);
	if (!disp[0]) return 0;
	path = disp;
	return 1;
}

static void OnOpen()
{
	std::wstring arc;
	if (!PickArchive(arc, 0)) return;
	OnOpenPath(arc);
}

static void OnNew()
{
	if (gBusy) return;
	DropWork();
	gWork = OxzNewWork();
	gOwned = 1;
	gArchive.clear();
	Refresh();
	SetStatus(OxzTxt(OS_NEWARCHIVE));
}

static void EnsureWork()
{
	if (gWork.empty()) OnNew();
}

static void OnAddFiles()
{
	EnsureWork();
	std::vector<std::wstring> files;
	if (!PickFiles(files)) return;
	std::wstring dest = DirForRow(ListView_GetNextItem(gList, -1, LVNI_SELECTED));
	for (size_t i = 0; i < files.size(); ++i) OxzImportOne(files[i], dest);
	Refresh();
	SetStatus(OxzTxt(OS_ADDED));
}

static void OnAddFolder()
{
	EnsureWork();
	std::wstring src;
	if (!PickFolder(OxzTxt(OS_PICK_ADD), src)) return;
	OxzImportOne(src, DirForRow(ListView_GetNextItem(gList, -1, LVNI_SELECTED)));
	Refresh();
	SetStatus(OxzTxt(OS_ADDED));
}

static void OnDelete()
{
	HWND ed = ListView_GetEditControl(gList);
	if (ed) return;
	std::vector<std::wstring> rels;
	int i = -1;
	while ((i = ListView_GetNextItem(gList, i, LVNI_SELECTED)) >= 0) {
		if (i < (int)gItems.size() && !gItems[i].up) rels.push_back(gItems[i].rel);
	}
	if (rels.empty()) return;
	if (MessageBoxW(gHwnd, OxzTxt(OS_DEL_ASK), OxzTxt(OS_DEL), MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
	for (size_t k = 0; k < rels.size(); ++k) {
		std::wstring path = gWork + L"\\" + rels[k];
		DWORD a = GetFileAttributesW(path.c_str());
		if (a == INVALID_FILE_ATTRIBUTES) continue;
		if (a & FILE_ATTRIBUTE_DIRECTORY) OxzRmTree(path);
		else DeleteFileW(path.c_str());
	}
	Refresh();
}

static void OnRename()
{
	int sel = ListView_GetNextItem(gList, -1, LVNI_FOCUSED);
	if (sel < 0) sel = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
	if (sel >= 0 && sel < (int)gItems.size() && !gItems[sel].up) ListView_EditLabel(gList, sel);
}

static void OnExtract()
{
	std::wstring arc = gArchive;
	if (arc.empty() && !PickArchive(arc, 0)) return;
	std::wstring dest;
	if (!PickFolder(OxzTxt(OS_PICK_OUT), dest)) return;
	SetBusy(1, OxzTxt(OS_EXTRACTING));
	Job* j = NewJob();
	j->kind = JOB_EXTRACT;
	j->archive = arc;
	j->dest = dest;
	StartJob(j);
}

static int HasFiles()
{
	return gHasFile;
}

static void OnSave()
{
	if (gWork.empty() || !HasFiles()) { Tell(gHwnd, OxzTxt(OS_NOFILES), MB_ICONINFORMATION); return; }
	std::wstring arc;
	if (!PickArchive(arc, 1)) return;
	if (UnderWork(arc)) { Tell(gHwnd, OxzTxt(OS_SAVE_INSIDE), MB_ICONWARNING); return; }
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_SAVE;
	j->archive = arc;
	j->work = gWork;
	StartJob(j);
}

static void SaveCurrent()
{
	if (gBusy || gWork.empty() || !HasFiles()) return;
	if (!OxzIsOxz(gArchive)) { OnSave(); return; }
	if (UnderWork(gArchive)) return;
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_SAVE;
	j->archive = gArchive;
	j->work = gWork;
	StartJob(j);
}

static int StampOf(const std::wstring& path, FILETIME* ft, unsigned long long* sz)
{
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
	if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
	ULARGE_INTEGER u;
	u.LowPart = fa.nFileSizeLow;
	u.HighPart = fa.nFileSizeHigh;
	*ft = fa.ftLastWriteTime;
	*sz = u.QuadPart;
	return 1;
}

static void RememberOpen(const std::wstring& path)
{
	if (!UnderWork(path)) return;
	FILETIME ft;
	unsigned long long sz = 0;
	if (!StampOf(path, &ft, &sz)) return;
	for (size_t i = 0; i < gWatch.size(); ++i) {
		if (!OxzSamePath(gWatch[i].path, path)) continue;
		gWatch[i].seen = gWatch[i].base = ft;
		gWatch[i].seenSize = gWatch[i].baseSize = sz;
		gWatch[i].dirtyTick = 0;
		SetTimer(gHwnd, 3, 400, NULL);
		return;
	}
	OpenWatch w;
	w.path = path;
	w.seen = w.base = ft;
	w.seenSize = w.baseSize = sz;
	w.dirtyTick = 0;
	gWatch.push_back(w);
	SetTimer(gHwnd, 3, 400, NULL);
}

static int LaunchWorkFile(const std::wstring& path)
{
	HINSTANCE opened = ShellExecuteW(gHwnd, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
	if ((INT_PTR)opened <= 32) return 0;
	RememberOpen(path);
	return 1;
}

static void FreezeWatchBase()
{
	for (size_t i = 0; i < gWatch.size(); ++i) {
		gWatch[i].base = gWatch[i].seen;
		gWatch[i].baseSize = gWatch[i].seenSize;
		gWatch[i].dirtyTick = 0;
	}
}

static void StartWatchSave()
{
	if (gBusy || gWatchPause || gWork.empty() || !HasFiles()) return;
	if (gArchive.empty()) {
		gWatchPause = 1;
		FreezeWatchBase();
		OnSave();
		return;
	}
	std::wstring arc = OxzIsOxz(gArchive) ? gArchive : OxzSaveName(gArchive);
	if (arc.empty() || UnderWork(arc)) return;
	FreezeWatchBase();
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_SAVE;
	j->archive = arc;
	j->work = gWork;
	StartJob(j);
}

static void PollWatch()
{
	if (gWatch.empty() || gBusy || gWork.empty()) return;
	DWORD now = GetTickCount();
	if (!now) now = 1;
	int due = 0;
	for (size_t i = 0; i < gWatch.size(); ++i) {
		OpenWatch& w = gWatch[i];
		FILETIME ft;
		unsigned long long sz = 0;
		if (!StampOf(w.path, &ft, &sz)) continue;
		if (CompareFileTime(&ft, &w.seen) != 0 || sz != w.seenSize) {
			w.seen = ft;
			w.seenSize = sz;
			w.dirtyTick = now;
			gWatchPause = 0;
		} else if (w.dirtyTick && now - w.dirtyTick >= 1000 && (CompareFileTime(&w.seen, &w.base) != 0 || w.seenSize != w.baseSize)) {
			due = 1;
		}
	}
	if (due) StartWatchSave();
}

static void WatchAfterSave(int fail)
{
	if (fail) {
		gWatchPause = 1;
		FreezeWatchBase();
		return;
	}
	DWORD now = GetTickCount();
	if (!now) now = 1;
	for (size_t i = 0; i < gWatch.size(); ++i) {
		OpenWatch& w = gWatch[i];
		FILETIME ft;
		unsigned long long sz = 0;
		if (!StampOf(w.path, &ft, &sz)) continue;
		w.seen = ft;
		w.seenSize = sz;
		if (CompareFileTime(&ft, &w.base) != 0 || sz != w.baseSize) w.dirtyTick = now;
		else w.dirtyTick = 0;
	}
}

static void OfferPendingUpdate()
{
	if (!gUpdWait || gBusy || gExit) return;
	OxzUpdHit* hit = gUpdWait;
	gUpdWait = NULL;
	if (MessageBoxW(gHwnd, OxzTxt(OS_UPDATE_ASK), OxzTxt(OS_UPDATE_TITLE), MB_YESNO | MB_ICONQUESTION) != IDYES) {
		delete hit;
		return;
	}
	SetBusy(1, OxzTxt(OS_UPDATING));
	OxzUpdateBegin(gHwnd, hit);
	delete hit;
}

static std::wstring AvoidDest(const std::wstring& dest, const std::vector<std::wstring>& inputs)
{
	auto clash = [&](const std::wstring& d) {
		for (size_t i = 0; i < inputs.size(); ++i)
			if (OxzSamePath(inputs[i], d)) return true;
		return false;
	};
	if (!clash(dest)) return dest;
	std::wstring stem = dest;
	if (OxzIsOxz(stem)) stem.resize(stem.size() - 4);
	for (int n = 2; n < 100; ++n) {
		wchar_t b[32];
		_snwprintf_s(b, _TRUNCATE, L"_%d.oxz", n);
		std::wstring t = stem + b;
		if (!clash(t)) return t;
	}
	return dest;
}

static void StartPack(const std::vector<std::wstring>& inputs, int add, const std::wstring& archive, int exact = 0)
{
	if (inputs.empty()) {
		if (!gExit) Tell(gHwnd, OxzTxt(OS_NOTHING), MB_ICONINFORMATION);
		if (gExit) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = add ? JOB_ADD : JOB_PACK;
	j->closeAfter = gCloseWhenDone ? 1 : 0;
	j->inputs = inputs;
	if (add) j->archive = archive;
	else if (exact) j->archive = AvoidDest(archive, inputs);
	else j->archive = AvoidDest(OxzDestFor(inputs), inputs);
	if (j->archive.empty()) { delete j; SetBusy(0, NULL); Tell(gHwnd, OxzTxt(OS_SAVE_FAIL), MB_ICONWARNING); return; }
	StartJob(j);
}

static void StartPackEach(const std::vector<std::wstring>& inputs, const std::wstring& folder)
{
	if (inputs.empty()) {
		if (!gExit) Tell(gHwnd, OxzTxt(OS_NOTHING), MB_ICONINFORMATION);
		if (gExit || gCloseWhenDone) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_PACKEACH;
	j->closeAfter = gCloseWhenDone ? 1 : 0;
	j->inputs = inputs;
	j->dest = folder;
	StartJob(j);
}

static void StartExtractMany(const std::vector<std::wstring>& inputs, int toFolder)
{
	SetBusy(1, OxzTxt(OS_EXTRACTING));
	Job* j = NewJob();
	j->kind = toFolder ? JOB_EXTRACTTO : JOB_EXTHERE;
	j->closeAfter = gCloseWhenDone ? 1 : 0;
	j->inputs = inputs;
	StartJob(j);
}

static void StartExtractAt(const std::vector<std::wstring>& inputs, const std::wstring& folder, int sub)
{
	if (folder.empty() || inputs.empty()) {
		if (!gExit) Tell(gHwnd, OxzTxt(OS_OPEN_FAIL), MB_ICONWARNING);
		PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	SetBusy(1, OxzTxt(OS_EXTRACTING));
	Job* j = NewJob();
	j->kind = JOB_EXTAT;
	j->closeAfter = gCloseWhenDone ? 1 : 0;
	j->inputs = inputs;
	j->dest = folder;
	j->sub = sub;
	StartJob(j);
}

static void StartEdit(const std::vector<std::wstring>& inputs)
{
	if (inputs.empty()) {
		if (!gExit) Tell(gHwnd, OxzTxt(OS_NOTHING), MB_ICONINFORMATION);
		PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	OnNew();
	for (size_t i = 0; i < inputs.size(); ++i) {
		if (UnderWork(inputs[i])) continue;
		OxzImportOne(inputs[i], gWork);
	}
	Refresh();
	SetStatus(OxzTxt(OS_NEWARCHIVE));
}

static int OxzFileCount(const std::vector<std::wstring>& inputs)
{
	int n = 0;
	for (size_t i = 0; i < inputs.size(); ++i)
		if (CanOpen(inputs[i])) n++;
	return n;
}

static void StartTest(const std::vector<std::wstring>& inputs)
{
	if (!OxzFileCount(inputs)) {
		if (!gExit) Tell(NULL, OxzTxt(OS_TEST_FAIL), MB_ICONWARNING);
		PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	SetBusy(1, OxzTxt(OS_TEST));
	Job* j = NewJob();
	j->kind = JOB_TEST;
	j->inputs = inputs;
	StartJob(j);
}

static void StartDropSave(const std::vector<std::wstring>& inputs, const std::wstring& into)
{
	if (inputs.empty() || gBusy) return;
	std::wstring arc = OxzIsOxz(gArchive) ? gArchive : L"";
	if (arc.empty() && !PickArchive(arc, 1)) return;
	if (UnderWork(arc)) {
		Tell(gHwnd, OxzTxt(OS_SAVE_FAIL), MB_ICONWARNING);
		return;
	}
	EnsureWork();
	std::wstring dest = into;
	if (dest.empty() || !UnderWork(dest)) dest = gWork;
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_DROPSAVE;
	j->inputs = inputs;
	j->archive = arc;
	j->work = gWork;
	j->dest = dest;
	StartJob(j);
}

static long long OxzFileUtc(const wchar_t* path)
{
	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!path || !GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return 0;
	ULARGE_INTEGER u;
	u.LowPart = fa.ftLastWriteTime.dwLowDateTime;
	u.HighPart = fa.ftLastWriteTime.dwHighDateTime;
	if (u.QuadPart < 116444736000000000ULL) return 0;
	return (long long)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static void SyncInstalledCopy()
{
	if (gSyncDone || gExit || gBusy) return;
	gSyncDone = 1;
	if (!OxzIsInstalled()) return;
	wchar_t self[32768];
	GetModuleFileNameW(NULL, self, 32768);
	std::wstring inst = OxzProgramDir() + L"\\OxzGui.exe";
	if (OxzSamePath(self, inst)) return;
	long long mine = OxzFileUtc(self);
	long long old = OxzFileUtc(inst.c_str());
	if (mine <= 0 || old <= 0 || mine <= old + 2) return;
	MessageBoxW(gHwnd, OxzTxt(OS_INSTALL_UPDATE), OxzTxt(OS_UPDATE_TITLE), MB_OK | MB_ICONINFORMATION);
	std::wstring err;
	if (OxzIsAdmin()) {
		if (!OxzInstallHere(err))
			Tell(gHwnd, err.empty() ? OxzTxt(OS_INSTALL_FAIL) : err.c_str(), MB_ICONWARNING);
	} else if (!OxzRelaunchElevated(L"/install_quiet")) {
		Tell(gHwnd, OxzTxt(OS_UAC_CANCEL), MB_ICONINFORMATION);
	} else if (OxzFileUtc(inst.c_str()) + 2 < mine) {
		Tell(gHwnd, OxzTxt(OS_INSTALL_FAIL), MB_ICONWARNING);
	}
}

static void OnJobDone()
{
	Job* j = gJob;
	gJob = NULL;
	SetBusy(0, NULL);
	if (!j) return;
	if (j->kind == JOB_TEST) {
		if (j->rc) gResult = j->rc;
		if (!gExit) {
			const wchar_t* msg = j->rc ? (j->err.empty() ? OxzTxt(OS_TEST_FAIL) : j->err.c_str()) : OxzTxt(OS_TEST_OK);
			Tell(NULL, msg, j->rc ? MB_ICONWARNING : MB_ICONINFORMATION);
		}
		delete j;
		PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	int fail = j->rc != 0;
	if (j->kind == JOB_SAVE) WatchAfterSave(fail);
	if (!fail && (j->kind == JOB_PACK || j->kind == JOB_ADD) && !j->dest.empty() && !j->closeAfter) {
		DropWork();
		gWork = j->dest;
		gOwned = 1;
		gArchive = j->archive;
		Refresh();
	} else if (j->kind == JOB_OPEN || j->kind == JOB_DROPSAVE || (j->kind == JOB_SAVE && !fail)) {
		if ((j->kind == JOB_SAVE || j->kind == JOB_DROPSAVE) && !j->archive.empty()) gArchive = j->archive;
		Refresh();
	}
	if (fail) {
		if (!gExit) Tell(gHwnd, j->err.empty() ? OxzTxt(OS_SAVE_FAIL) : j->err.c_str(), MB_ICONWARNING);
	} else if (j->kind == JOB_SAVE || j->kind == JOB_PACK || j->kind == JOB_ADD || j->kind == JOB_DROPSAVE) {
		gArchive = j->archive;
		wchar_t msg[400];
		const wchar_t* head = (j->kind == JOB_ADD || j->kind == JOB_DROPSAVE || j->merged) ? OxzTxt(OS_ADDED) : (j->kind == JOB_PACK ? OxzTxt(OS_CREATED) : OxzTxt(OS_SAVED));
		_snwprintf_s(msg, _TRUNCATE, L"%s  %llu %s   %s", head, j->bytes, OxzTxt(OS_BYTES), j->archive.c_str());
		SetStatus(msg);
		SetTitle();
	} else if (j->kind == JOB_EXTRACT || j->kind == JOB_EXTHERE || j->kind == JOB_EXTRACTTO || j->kind == JOB_EXTAT) {
		SetStatus(OxzTxt(OS_EXTRACTED));
	} else if (j->kind == JOB_PACKEACH) {
		wchar_t msg[200];
		_snwprintf_s(msg, _TRUNCATE, L"%s  %llu %s", OxzTxt(OS_CREATED), j->bytes, OxzTxt(OS_BYTES));
		SetStatus(msg);
	}
	int closeNow = !fail && j->closeAfter;
	delete j;
	if (!closeNow) SyncInstalledCopy();
	if (closeNow || gExit) {
		delete gUpdWait;
		gUpdWait = NULL;
		PostMessageW(gHwnd, WM_CLOSE, 0, 0);
	} else {
		OfferPendingUpdate();
	}
}

static void ApplyRename(NMLVDISPINFOW* di)
{
	if (!di || !di->item.pszText || !di->item.pszText[0]) return;
	int i = di->item.iItem;
	if (i < 0 || i >= (int)gItems.size()) return;
	const wchar_t* neu = di->item.pszText;
	if (wcschr(neu, L'\\') || wcschr(neu, L'/') || wcschr(neu, L':') || wcscmp(neu, L".") == 0 || wcscmp(neu, L"..") == 0) {
		Tell(gHwnd, OxzTxt(OS_BADNAME), MB_ICONWARNING);
		return;
	}
	const std::wstring& rel = gItems[i].rel;
	size_t slash = rel.find_last_of(L'\\');
	std::wstring parent = slash == std::wstring::npos ? L"" : rel.substr(0, slash);
	std::wstring destRel = parent.empty() ? neu : (parent + L"\\" + neu);
	if (_wcsicmp(destRel.c_str(), rel.c_str()) == 0) return;
	if (!MoveFileW((gWork + L"\\" + rel).c_str(), (gWork + L"\\" + destRel).c_str()))
		Tell(gHwnd, OxzTxt(OS_RENAME_FAIL), MB_ICONWARNING);
	PostMessageW(gHwnd, WM_APP + 2, 0, 0);
}

static void PaintList(NMLVCUSTOMDRAW* cd)
{
	int row = (int)cd->nmcd.dwItemSpec;
	int col = cd->iSubItem;
	int sel = (ListView_GetItemState(gList, row, LVIS_SELECTED) & LVIS_SELECTED) ? 1 : 0;
	int cut = 0;
	if (row >= 0 && row < (int)gItems.size() && !gItems[row].up && !gWork.empty()) {
		std::wstring full = gWork + L"\\" + gItems[row].rel;
		for (size_t i = 0; i < gCut.size(); ++i) {
			const std::wstring& c = gCut[i];
			if (_wcsicmp(c.c_str(), full.c_str()) == 0) { cut = 1; break; }
			if (full.size() > c.size() && _wcsnicmp(full.c_str(), c.c_str(), c.size()) == 0 && full[c.size()] == L'\\') { cut = 1; break; }
		}
	}
	COLORREF bg = sel ? RGB(176, 158, 228) : (row % 2 ? RGB(230, 244, 238) : RGB(252, 236, 244));
	COLORREF fg = sel ? RGB(40, 28, 88) : (cut ? RGB(150, 140, 150) : RGB(0, 0, 0));
	RECT rc;
	ListView_GetSubItemRect(gList, row, col, LVIR_BOUNDS, &rc);
	if (col == 0) {
		RECT next;
		ListView_GetSubItemRect(gList, row, 1, LVIR_BOUNDS, &next);
		rc.right = next.left;
		rc.left = 0;
	}
	HBRUSH br = CreateSolidBrush(bg);
	FillRect(cd->nmcd.hdc, &rc, br);
	DeleteObject(br);
	int iconPad = 0;
	if (col == 0 && gIcons) {
		LVITEMW it;
		memset(&it, 0, sizeof(it));
		it.mask = LVIF_IMAGE;
		it.iItem = row;
		if (ListView_GetItem(gList, &it) && it.iImage >= 0) {
			int icx = 0, icy = 0;
			ImageList_GetIconSize(gIcons, &icx, &icy);
			int y = rc.top + ((rc.bottom - rc.top) - icy) / 2;
			ImageList_Draw(gIcons, it.iImage, cd->nmcd.hdc, rc.left + 4, y, ILD_TRANSPARENT);
			iconPad = icx + 6;
		}
	}
	wchar_t text[1024];
	text[0] = 0;
	ListView_GetItemText(gList, row, col, text, 1024);
	SetBkMode(cd->nmcd.hdc, TRANSPARENT);
	SetTextColor(cd->nmcd.hdc, fg);
	SelectObject(cd->nmcd.hdc, gFont);
	RECT tr = rc;
	tr.left += 8;
	tr.left += iconPad;
	DrawTextW(cd->nmcd.hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void RefreshSetStatus()
{
	if (!gSet) return;
	SetWindowTextW(GetDlgItem(gSet, ID_SET_STAT), OxzIsInstalled() ? OxzTxt(OS_INSTALLED) : OxzTxt(OS_NOT_INSTALLED));
}

static void RelabelSet()
{
	if (!gSet) return;
	SetWindowTextW(gSet, OxzTxt(OS_SET_TITLE));
	SetWindowTextW(GetDlgItem(gSet, ID_SET_INSTALL), OxzTxt(OS_INSTALL));
	SetWindowTextW(GetDlgItem(gSet, ID_SET_REMOVE), OxzTxt(OS_UNINSTALL));
	SetWindowTextW(GetDlgItem(gSet, ID_SET_CLOSE), OxzTxt(OS_CLOSE));
	SetWindowTextW(GetDlgItem(gSet, ID_SET_HELP), OxzTxt(OS_HELP));
	SetWindowTextW(GetDlgItem(gSet, OS_LANG), OxzTxt(OS_LANG));
	RefreshSetStatus();
	ApplyRtl(gSet);
	ApplyCaption(gSet);
}

static void OnInstallClick(int remove)
{
	const wchar_t* arg = remove ? L"/uninstall" : L"/install";
	if (OxzIsAdmin()) {
		std::wstring err;
		bool ok = remove ? OxzRemoveInstall(err) : OxzInstallHere(err);
		Tell(gSet, ok ? OxzTxt(remove ? OS_UNINSTALL_OK : OS_INSTALL_OK) : (err.empty() ? OxzTxt(remove ? OS_UNINSTALL_FAIL : OS_INSTALL_FAIL) : err.c_str()), ok ? MB_ICONINFORMATION : MB_ICONWARNING);
	} else if (!OxzRelaunchElevated(arg)) {
		Tell(gSet, OxzTxt(OS_UAC_CANCEL), MB_ICONINFORMATION);
	}
	RefreshSetStatus();
}

static LRESULT CALLBACK SetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE: {
		gSet = hwnd;
		HWND lang = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 16, 200, 24, hwnd, (HMENU)OS_LANG, NULL, NULL);
		HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 16, 44, 220, 280, hwnd, (HMENU)ID_SET_LANG, NULL, NULL);
		HWND help = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 252, 44, 430, 220, hwnd, (HMENU)ID_SET_HELP, NULL, NULL);
		HWND st = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 252, 274, 430, 24, hwnd, (HMENU)ID_SET_STAT, NULL, NULL);
		HWND inst = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 252, 312, 200, 34, hwnd, (HMENU)ID_SET_INSTALL, NULL, NULL);
		HWND rem = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 462, 312, 200, 34, hwnd, (HMENU)ID_SET_REMOVE, NULL, NULL);
		HWND closeb = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 462, 360, 200, 34, hwnd, (HMENU)ID_SET_CLOSE, NULL, NULL);
		HWND hs[] = { lang, list, help, st, inst, rem, closeb };
		for (int i = 0; i < 7; ++i) SendMessageW(hs[i], WM_SETFONT, (WPARAM)gFont, TRUE);
		SubBtn(inst); SubBtn(rem); SubBtn(closeb);
		for (int i = 0; i < 14; ++i) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)OxzLangName(i));
		SendMessageW(list, LB_SETCURSEL, OxzLang(), 0);
		RelabelSet();
		return 0;
	}
	case WM_COMMAND:
		if (HIWORD(wp) == LBN_SELCHANGE && LOWORD(wp) == ID_SET_LANG) {
			int sel = (int)SendMessageW(GetDlgItem(hwnd, ID_SET_LANG), LB_GETCURSEL, 0, 0);
			if (sel >= 0 && sel <= 13) {
				OxzLangSet(sel);
				MakeFont(gHwnd);
				for (int i = 0; i < 9; ++i) SendMessageW(GetDlgItem(gHwnd, kBtnId[i]), WM_SETFONT, (WPARAM)gFont, TRUE);
				SendMessageW(gList, WM_SETFONT, (WPARAM)gFont, TRUE);
				SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
				Relabel();
				HWND hs[] = { GetDlgItem(hwnd, OS_LANG), GetDlgItem(hwnd, ID_SET_LANG), GetDlgItem(hwnd, ID_SET_HELP), GetDlgItem(hwnd, ID_SET_STAT), GetDlgItem(hwnd, ID_SET_INSTALL), GetDlgItem(hwnd, ID_SET_REMOVE), GetDlgItem(hwnd, ID_SET_CLOSE) };
				for (int i = 0; i < 7; ++i) SendMessageW(hs[i], WM_SETFONT, (WPARAM)gFont, TRUE);
				RelabelSet();
				OxzRefreshMenuText();
				InvalidateRect(gHwnd, NULL, TRUE);
				InvalidateRect(hwnd, NULL, TRUE);
			}
			return 0;
		}
		if (gBusy) return 0;
		if (LOWORD(wp) == ID_SET_INSTALL) OnInstallClick(0);
		else if (LOWORD(wp) == ID_SET_REMOVE) OnInstallClick(1);
		else if (LOWORD(wp) == ID_SET_CLOSE) DestroyWindow(hwnd);
		return 0;
	case WM_DRAWITEM:
		DrawBtn((DRAWITEMSTRUCT*)lp);
		return TRUE;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX: {
		HDC dc = (HDC)wp;
		SetBkColor(dc, RGB(255, 234, 238));
		SetTextColor(dc, RGB(40, 20, 40));
		return (LRESULT)gMint;
	}
	case WM_ACTIVATE:
		if (LOWORD(wp) != WA_INACTIVE) RefreshSetStatus();
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		gSet = NULL;
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowSettings()
{
	if (gSet) { SetForegroundWindow(gSet); return; }
	RECT r;
	GetWindowRect(gHwnd, &r);
	HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OxzSet", L"", WS_POPUP | WS_CAPTION | WS_SYSMENU,
		r.left + 40, r.top + 40, 720, 460, gHwnd, NULL, GetModuleHandleW(NULL), NULL);
	if (!d) return;
	ShowWindow(d, SW_SHOW);
	UpdateWindow(d);
	EnableWindow(gHwnd, FALSE);
	MSG msg;
	while (IsWindow(d) && GetMessageW(&msg, NULL, 0, 0)) {
		if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
		if (!IsDialogMessageW(d, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	EnableWindow(gHwnd, TRUE);
	SetForegroundWindow(gHwnd);
}

static void TakePaths(const std::wstring& arg, std::vector<std::wstring>& out, int oxzDirect)
{
	if (oxzDirect && CanOpen(arg)) {
		out.push_back(arg);
		return;
	}
	std::vector<std::wstring> list, ok;
	if (OxzReadList(arg, list)) {
		for (size_t i = 0; i < list.size(); ++i) {
			std::wstring s = list[i];
			if (!s.empty() && s[0] == 0xFEFF) s.erase(0, 1);
			if (!s.empty() && GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES) ok.push_back(s);
		}
	}
	if (!ok.empty()) {
		out.swap(ok);
		DeleteFileW(arg.c_str());
		return;
	}
	if (!arg.empty() && GetFileAttributesW(arg.c_str()) != INVALID_FILE_ATTRIBUTES) out.push_back(arg);
}

static void BeginStartup()
{
	std::vector<std::wstring> paths;
	if (gStartKind == 1) OnOpenPath(gStartA);
	else if (gStartKind == 2) { TakePaths(gStartA, paths, 0); StartPack(paths, 0, L""); }
	else if (gStartKind == 3) { TakePaths(gStartA, paths, 1); StartExtractMany(paths, 0); }
	else if (gStartKind == 4) { TakePaths(gStartA, paths, 1); StartExtractMany(paths, 1); }
	else if (gStartKind == 5) { TakePaths(gStartB, paths, 0); StartPack(paths, 1, gStartA); }
	else if (gStartKind == 6 || gStartKind == 7) { paths.push_back(gStartA); StartPack(paths, 0, L""); }
	else if (gStartKind == 8) { TakePaths(gStartA, paths, 0); StartEdit(paths); }
	else if (gStartKind == 9) { TakePaths(gStartB, paths, 0); StartPack(paths, 0, gStartA, 1); }
	else if (gStartKind == 10) { TakePaths(gStartB, paths, 1); StartExtractAt(paths, gStartA, gStartSub); }
	else if (gStartKind == 11) {
		TakePaths(gStartA, paths, 1);
		std::wstring dest;
		if (!PickFolder(OxzTxt(OS_PICK_OUT), dest)) { PostMessageW(gHwnd, WM_CLOSE, 0, 0); return; }
		StartExtractAt(paths, dest, OxzFileCount(paths) > 1 ? 1 : 0);
	}
	else if (gStartKind == 12) { TakePaths(gStartA, paths, 1); StartTest(paths); }
	else if (gStartKind == 13) { TakePaths(gStartA, paths, 0); OxzShowTagEdit(NULL, paths); PostMessageW(gHwnd, WM_CLOSE, 0, 0); }
	else if (gStartKind == 14) { TakePaths(gStartA, paths, 0); OxzShowConvert(NULL, paths); PostMessageW(gHwnd, WM_CLOSE, 0, 0); }
	else if (gStartKind == 15) { TakePaths(gStartA, paths, 0); StartPackEach(paths, L""); }
	else if (gStartKind == 16) { TakePaths(gStartB, paths, 0); StartPackEach(paths, gStartA); }
}

static void AcceptPaths(const std::vector<std::wstring>& paths, const std::wstring& into)
{
	if (paths.empty() || gBusy) return;
	if (paths.size() == 1 && CanOpen(paths[0])) {
		if ((gHasFile || !gItems.empty()) && MessageBoxW(gHwnd, OxzTxt(OS_REPLACE_ASK), OxzTxt(OS_APP), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
			return;
		OnOpenPath(paths[0]);
		return;
	}
	std::vector<std::wstring> use, moved;
	for (size_t i = 0; i < paths.size(); ++i) {
		if (UnderWork(paths[i])) moved.push_back(paths[i]);
		else use.push_back(paths[i]);
	}
	int did = 0;
	if (!moved.empty() && !into.empty()) {
		CreateDirectoryW(into.c_str(), NULL);
		for (size_t i = 0; i < moved.size(); ++i) {
			const std::wstring& src = moved[i];
			if (OxzSamePath(src, into)) continue;
			if (src.size() < into.size() && _wcsnicmp(into.c_str(), src.c_str(), src.size()) == 0 && into[src.size()] == L'\\') continue;
			std::wstring parent = OxzParent(src);
			if (OxzSamePath(parent, into)) continue;
			std::wstring to = into + L"\\" + OxzBaseName(src);
			if (OxzSamePath(src, to)) continue;
			DWORD a = GetFileAttributesW(to.c_str());
			if (a != INVALID_FILE_ATTRIBUTES) {
				if (a & FILE_ATTRIBUTE_DIRECTORY) OxzRmTree(to);
				else DeleteFileW(to.c_str());
			}
			if (MoveFileW(src.c_str(), to.c_str())) did++;
		}
	}
	if (!use.empty()) StartDropSave(use, into);
	else if (did) {
		Refresh();
		if (!gArchive.empty()) SaveCurrent();
		else SetStatus(OxzTxt(OS_ADDED));
	}
}

static void SetDropHi(int row)
{
	if (row >= 0 && (row >= (int)gItems.size() || (!gItems[row].dir && !gItems[row].up))) row = -1;
	if (gDropHi == row) return;
	if (gDropHi >= 0 && gDropHi < ListView_GetItemCount(gList))
		ListView_SetItemState(gList, gDropHi, 0, LVIS_DROPHILITED);
	gDropHi = row;
	if (row >= 0) ListView_SetItemState(gList, row, LVIS_DROPHILITED, LVIS_DROPHILITED);
}

class GuiDrop : public IDropTarget
{
public:
	GuiDrop() : m_ref(1), m_effect(DROPEFFECT_NONE) {}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect)
	{
		FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		m_effect = (!gBusy && data && data->QueryGetData(&fmt) == S_OK) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
		if (effect) *effect = m_effect;
		return S_OK;
	}
	IFACEMETHODIMP DragOver(DWORD, POINTL pt, DWORD* effect)
	{
		if (effect) *effect = gBusy ? DROPEFFECT_NONE : m_effect;
		POINT c = { pt.x, pt.y };
		ScreenToClient(gList, &c);
		LVHITTESTINFO ht;
		memset(&ht, 0, sizeof(ht));
		ht.pt = c;
		int row = ListView_HitTest(gList, &ht);
		SetDropHi(row);
		return S_OK;
	}
	IFACEMETHODIMP DragLeave() { SetDropHi(-1); return S_OK; }
	IFACEMETHODIMP Drop(IDataObject* data, DWORD, POINTL pt, DWORD* effect)
	{
		if (effect) *effect = m_effect;
		if (gBusy || !data) return S_OK;
		FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		STGMEDIUM stg;
		memset(&stg, 0, sizeof(stg));
		if (FAILED(data->GetData(&fmt, &stg))) return S_OK;
		std::vector<std::wstring> paths;
		HDROP h = (HDROP)GlobalLock(stg.hGlobal);
		if (h) {
			UINT n = DragQueryFileW(h, 0xFFFFFFFF, NULL, 0);
			for (UINT i = 0; i < n; ++i) {
				UINT c = DragQueryFileW(h, i, NULL, 0);
				if (!c) continue;
				std::wstring s(c + 1, L'\0');
				DragQueryFileW(h, i, &s[0], c + 1);
				s.resize(wcslen(s.c_str()));
				if (!s.empty()) paths.push_back(s);
			}
			GlobalUnlock(stg.hGlobal);
		}
		ReleaseStgMedium(&stg);
		POINT c = { pt.x, pt.y };
		ScreenToClient(gList, &c);
		LVHITTESTINFO ht;
		memset(&ht, 0, sizeof(ht));
		ht.pt = c;
		int row = ListView_HitTest(gList, &ht);
		SetDropHi(-1);
		AcceptPaths(paths, DirForRow(row));
		return S_OK;
	}
private:
	long m_ref;
	DWORD m_effect;
};

static GuiDrop* gDrop = NULL;

static void ShowListMenu(int x, int y)
{
	POINT c = { x, y };
	ScreenToClient(gList, &c);
	LVHITTESTINFO ht;
	memset(&ht, 0, sizeof(ht));
	ht.pt = c;
	int row = ListView_HitTest(gList, &ht);
	if (row >= 0 && (ListView_GetItemState(gList, row, LVIS_SELECTED) & LVIS_SELECTED) == 0) {
		ListView_SetItemState(gList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_SetItemState(gList, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	HMENU m = CreatePopupMenu();
	int sel = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
	if (sel >= 0 && sel < (int)gItems.size() && !gItems[sel].up)
		AppendMenuW(m, MF_STRING, ID_OPENSEL, OxzTxt(OS_OPEN));
	AppendMenuW(m, MF_STRING, ID_COPY, OxzTxt(OS_COPY));
	AppendMenuW(m, MF_STRING, ID_CUT, OxzTxt(OS_CUT));
	AppendMenuW(m, MF_STRING, ID_PASTE, OxzTxt(OS_PASTE));
	AppendMenuW(m, MF_STRING, ID_SELECTALL, OxzTxt(OS_SELECTALL));
	AppendMenuW(m, MF_SEPARATOR, 0, NULL);
	AppendMenuW(m, MF_STRING, ID_ADDFILE, OxzTxt(OS_ADDFILE));
	AppendMenuW(m, MF_STRING, ID_ADDFOLDER, OxzTxt(OS_ADDFOLDER));
	AppendMenuW(m, MF_STRING, ID_DEL, OxzTxt(OS_DEL));
	AppendMenuW(m, MF_STRING, ID_RENAME, OxzTxt(OS_RENAME));
	TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, gHwnd, NULL);
	DestroyMenu(m);
}

class ListFmt : public IEnumFORMATETC
{
public:
	ListFmt() : m_ref(1), m_i(0)
	{
		memset(&m_fmt, 0, sizeof(m_fmt));
		m_fmt.cfFormat = CF_HDROP;
		m_fmt.dwAspect = DVASPECT_CONTENT;
		m_fmt.lindex = -1;
		m_fmt.tymed = TYMED_HGLOBAL;
	}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) { *ppv = static_cast<IEnumFORMATETC*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP Next(ULONG celt, FORMATETC* rgelt, ULONG* fetched)
	{
		ULONG n = 0;
		if (rgelt && m_i == 0 && celt) { rgelt[0] = m_fmt; m_i = 1; n = 1; }
		if (fetched) *fetched = n;
		return n == celt ? S_OK : S_FALSE;
	}
	IFACEMETHODIMP Skip(ULONG celt)
	{
		if (m_i == 0 && celt) { m_i = 1; return celt == 1 ? S_OK : S_FALSE; }
		return S_FALSE;
	}
	IFACEMETHODIMP Reset() { m_i = 0; return S_OK; }
	IFACEMETHODIMP Clone(IEnumFORMATETC** out) { if (!out) return E_POINTER; *out = new ListFmt(); return S_OK; }
private:
	long m_ref;
	int m_i;
	FORMATETC m_fmt;
};

class ListData : public IDataObject
{
public:
	explicit ListData(const std::vector<std::wstring>& paths) : m_ref(1), m_mem(NULL)
	{
		size_t chars = 1;
		for (size_t i = 0; i < paths.size(); ++i) chars += paths[i].size() + 1;
		m_mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + chars * sizeof(wchar_t));
		if (!m_mem) return;
		BYTE* p = (BYTE*)GlobalLock(m_mem);
		DROPFILES* df = (DROPFILES*)p;
		memset(df, 0, sizeof(DROPFILES));
		df->pFiles = sizeof(DROPFILES);
		df->fWide = TRUE;
		wchar_t* w = (wchar_t*)(p + sizeof(DROPFILES));
		for (size_t i = 0; i < paths.size(); ++i) {
			memcpy(w, paths[i].c_str(), (paths[i].size() + 1) * sizeof(wchar_t));
			w += paths[i].size() + 1;
		}
		*w = 0;
		GlobalUnlock(m_mem);
	}
	~ListData() { if (m_mem) GlobalFree(m_mem); }
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP GetData(FORMATETC* fmt, STGMEDIUM* stg)
	{
		if (!fmt || !stg || !m_mem) return E_INVALIDARG;
		if (fmt->cfFormat != CF_HDROP || (fmt->tymed & TYMED_HGLOBAL) == 0) return DV_E_FORMATETC;
		SIZE_T n = GlobalSize(m_mem);
		HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, n);
		if (!copy) return E_OUTOFMEMORY;
		void* a = GlobalLock(m_mem);
		void* b = GlobalLock(copy);
		if (a && b) memcpy(b, a, n);
		if (b) GlobalUnlock(copy);
		if (a) GlobalUnlock(m_mem);
		memset(stg, 0, sizeof(*stg));
		stg->tymed = TYMED_HGLOBAL;
		stg->hGlobal = copy;
		return S_OK;
	}
	IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) { return E_NOTIMPL; }
	IFACEMETHODIMP QueryGetData(FORMATETC* fmt)
	{
		if (!fmt) return E_INVALIDARG;
		if (fmt->cfFormat == CF_HDROP && (fmt->tymed & TYMED_HGLOBAL)) return S_OK;
		return DV_E_FORMATETC;
	}
	IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC* in, FORMATETC* out)
	{
		if (!in || !out) return E_INVALIDARG;
		*out = *in;
		return DATA_S_SAMEFORMATETC;
	}
	IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) { return E_NOTIMPL; }
	IFACEMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** out)
	{
		if (!out) return E_POINTER;
		if (dir != DATADIR_GET) return E_NOTIMPL;
		*out = new ListFmt();
		return S_OK;
	}
	IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) { return OLE_E_ADVISENOTSUPPORTED; }
	IFACEMETHODIMP DUnadvise(DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
	IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) { return OLE_E_ADVISENOTSUPPORTED; }
private:
	long m_ref;
	HGLOBAL m_mem;
};

class ListSrc : public IDropSource
{
public:
	ListSrc() : m_ref(1) {}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IDropSource) { *ppv = static_cast<IDropSource*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release()
	{
		long n = InterlockedDecrement(&m_ref);
		if (n == 0) delete this;
		return (ULONG)n;
	}
	IFACEMETHODIMP QueryContinueDrag(BOOL esc, DWORD key)
	{
		if (esc) return DRAGDROP_S_CANCEL;
		if ((key & MK_LBUTTON) == 0) return DRAGDROP_S_DROP;
		return S_OK;
	}
	IFACEMETHODIMP GiveFeedback(DWORD) { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
	long m_ref;
};

static int DragInside(const std::wstring& dir, const std::wstring& path)
{
	if (path.size() <= dir.size()) return 0;
	if (_wcsnicmp(path.c_str(), dir.c_str(), dir.size()) != 0) return 0;
	return path[dir.size()] == L'\\';
}

static void CollectSelected(std::vector<std::wstring>& paths)
{
	paths.clear();
	if (gWork.empty()) return;
	std::vector<std::wstring> all;
	std::vector<int> folders;
	int row = -1;
	while ((row = ListView_GetNextItem(gList, row, LVNI_SELECTED)) >= 0) {
		if (row >= (int)gItems.size() || gItems[row].up) continue;
		all.push_back(gWork + L"\\" + gItems[row].rel);
		if (gItems[row].dir) folders.push_back((int)all.size() - 1);
	}
	for (size_t i = 0; i < all.size(); ++i) {
		int skip = 0;
		for (size_t k = 0; k < folders.size(); ++k) {
			if ((int)i == folders[k]) continue;
			if (DragInside(all[folders[k]], all[i])) { skip = 1; break; }
		}
		if (!skip) paths.push_back(all[i]);
	}
}

static HGLOBAL MakeDropMem(const std::vector<std::wstring>& paths)
{
	size_t chars = 1;
	for (size_t i = 0; i < paths.size(); ++i) chars += paths[i].size() + 1;
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + chars * sizeof(wchar_t));
	if (!mem) return NULL;
	BYTE* p = (BYTE*)GlobalLock(mem);
	DROPFILES* df = (DROPFILES*)p;
	memset(df, 0, sizeof(DROPFILES));
	df->pFiles = sizeof(DROPFILES);
	df->fWide = TRUE;
	wchar_t* w = (wchar_t*)(p + sizeof(DROPFILES));
	for (size_t i = 0; i < paths.size(); ++i) {
		memcpy(w, paths[i].c_str(), (paths[i].size() + 1) * sizeof(wchar_t));
		w += paths[i].size() + 1;
	}
	*w = 0;
	GlobalUnlock(mem);
	return mem;
}

static void PutClipboard(const std::vector<std::wstring>& paths, int move)
{
	if (paths.empty()) return;
	if (!OpenClipboard(gHwnd)) return;
	EmptyClipboard();
	HGLOBAL mem = MakeDropMem(paths);
	if (!mem || !SetClipboardData(CF_HDROP, mem)) {
		if (mem) GlobalFree(mem);
		CloseClipboard();
		return;
	}
	UINT pref = RegisterClipboardFormatW(L"Preferred DropEffect");
	HGLOBAL eff = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
	if (eff) {
		DWORD* d = (DWORD*)GlobalLock(eff);
		*d = move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
		GlobalUnlock(eff);
		if (!SetClipboardData(pref, eff)) GlobalFree(eff);
	}
	CloseClipboard();
	gCut.clear();
	if (move) {
		gCut = paths;
		SetTimer(gHwnd, 2, 400, NULL);
	} else {
		KillTimer(gHwnd, 2);
	}
	InvalidateRect(gList, NULL, FALSE);
}

static void ReadClipboard(std::vector<std::wstring>& paths)
{
	paths.clear();
	if (!OpenClipboard(gHwnd)) return;
	HDROP h = (HDROP)GetClipboardData(CF_HDROP);
	if (h) {
		UINT n = DragQueryFileW(h, 0xFFFFFFFF, NULL, 0);
		for (UINT i = 0; i < n; ++i) {
			UINT c = DragQueryFileW(h, i, NULL, 0);
			if (!c) continue;
			std::wstring s(c + 1, L'\0');
			DragQueryFileW(h, i, &s[0], c + 1);
			s.resize(wcslen(s.c_str()));
			if (!s.empty()) paths.push_back(s);
		}
	}
	CloseClipboard();
}

static void OnCopyFiles(int move)
{
	if (gBusy) return;
	std::vector<std::wstring> paths;
	CollectSelected(paths);
	PutClipboard(paths, move);
}

static void OnPasteFiles()
{
	if (gBusy) return;
	gCut.clear();
	KillTimer(gHwnd, 2);
	InvalidateRect(gList, NULL, FALSE);
	std::vector<std::wstring> paths;
	ReadClipboard(paths);
	int row = ListView_GetNextItem(gList, -1, LVNI_FOCUSED);
	if (row < 0) row = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
	AcceptPaths(paths, DirForRow(row));
}

static void OnSelectAll()
{
	ListView_SetItemState(gList, -1, 0, LVIS_SELECTED);
	int focus = -1;
	for (int i = 0; i < (int)gItems.size(); ++i) {
		if (gItems[i].up) continue;
		ListView_SetItemState(gList, i, LVIS_SELECTED, LVIS_SELECTED);
		if (focus < 0) focus = i;
	}
	if (focus >= 0) ListView_SetItemState(gList, focus, LVIS_FOCUSED, LVIS_FOCUSED);
}

static void WatchCut()
{
	if (gCut.empty()) {
		KillTimer(gHwnd, 2);
		return;
	}
	if (gBusy) return;
	int gone = 0;
	for (size_t i = 0; i < gCut.size(); ++i) {
		if (GetFileAttributesW(gCut[i].c_str()) == INVALID_FILE_ATTRIBUTES) gone = 1;
	}
	if (!gone) return;
	gCut.clear();
	KillTimer(gHwnd, 2);
	Refresh();
	SaveCurrent();
}

static void BeginListDrag()
{
	if (gBusy || gWork.empty()) return;
	std::vector<std::wstring> paths;
	CollectSelected(paths);
	if (paths.empty()) return;
	ListData* data = new ListData(paths);
	ListSrc* src = new ListSrc();
	DWORD effect = DROPEFFECT_NONE;
	DoDragDrop(data, src, DROPEFFECT_COPY, &effect);
	data->Release();
	src->Release();
}

static void OxzForceShow(HWND hwnd);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE: {
		gHwnd = hwnd;
		MakeFont(hwnd);
		ApplyCaption(hwnd);
		ApplyRtl(hwnd);
		for (int i = 0; i < 9; ++i) {
			HWND b = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)kBtnId[i], NULL, NULL);
			SendMessageW(b, WM_SETFONT, (WPARAM)gFont, TRUE);
			SubBtn(b);
		}
		gList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
			WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_EDITLABELS | LVS_SHOWSELALWAYS,
			0, 0, 10, 10, hwnd, (HMENU)ID_LIST, NULL, NULL);
		ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
		SetWindowTheme(gList, L"", L"");
		SendMessageW(gList, WM_SETFONT, (WPARAM)gFont, TRUE);
		ListView_SetBkColor(gList, RGB(255, 224, 236));
		LVCOLUMNW col;
		memset(&col, 0, sizeof(col));
		const int widths[] = { 268, 100, 90, 110, 100, 64, 72, 56, 140, 140 };
		col.mask = LVCF_TEXT | LVCF_WIDTH;
		col.pszText = (LPWSTR)L"";
		for (int c = 0; c < 10; ++c) {
			col.cx = widths[c];
			ListView_InsertColumn(gList, c, &col);
		}
		gStatus = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, hwnd, (HMENU)ID_STATUS, NULL, NULL);
		SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
		gProg = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD, 0, 0, 10, 10, hwnd, (HMENU)ID_PROG, NULL, NULL);
		SetWindowTheme(gProg, L"", L"");
		SendMessageW(gProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
		SendMessageW(gProg, PBM_SETBARCOLOR, 0, (LPARAM)RGB(60, 160, 60));
		SendMessageW(gProg, PBM_SETBKCOLOR, 0, (LPARAM)RGB(255, 234, 238));
		Relabel();
		gDrop = new GuiDrop();
		RegisterDragDrop(hwnd, gDrop);
		return 0;
	}
	case WM_SIZE:
		Relayout();
		return 0;
	case WM_DPICHANGED: {
		RECT* r = (RECT*)lp;
		SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
		MakeFont(hwnd);
		for (int i = 0; i < 9; ++i) SendMessageW(GetDlgItem(hwnd, kBtnId[i]), WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gList, WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
		Relayout();
		Refresh();
		return 0;
	}
	case WM_COMMAND:
		if (gBusy) return 0;
		switch (LOWORD(wp)) {
		case ID_OPEN: OnOpen(); break;
		case ID_NEW: OnNew(); break;
		case ID_ADDFILE: OnAddFiles(); break;
		case ID_ADDFOLDER: OnAddFolder(); break;
		case ID_DEL: OnDelete(); break;
		case ID_RENAME: OnRename(); break;
		case ID_EXTRACT: OnExtract(); break;
		case ID_SAVE: OnSave(); break;
		case ID_SETTINGS: ShowSettings(); break;
		case ID_COPY: OnCopyFiles(0); break;
		case ID_CUT: OnCopyFiles(1); break;
		case ID_PASTE: OnPasteFiles(); break;
		case ID_SELECTALL: OnSelectAll(); break;
		case ID_REFRESH: Refresh(); break;
		case ID_UP: GoUp(); break;
		case ID_OPENSEL: {
			std::vector<int> rows;
			int i = -1;
			while ((i = ListView_GetNextItem(gList, i, LVNI_SELECTED)) >= 0)
				if (i < (int)gItems.size() && !gItems[i].up) rows.push_back(i);
			if (rows.size() == 1 && gItems[rows[0]].dir) EnterRow(rows[0]);
			else {
				int bad = 0;
				for (size_t k = 0; k < rows.size(); ++k) {
					if (gItems[rows[k]].dir) {
						std::wstring path = gWork + L"\\" + gItems[rows[k]].rel;
						HINSTANCE opened = ShellExecuteW(gHwnd, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
						if ((INT_PTR)opened <= 32) bad = 1;
					} else if (!LaunchWorkFile(gWork + L"\\" + gItems[rows[k]].rel)) bad = 1;
				}
				if (bad) Tell(gHwnd, OxzTxt(OS_OPEN_FAIL), MB_ICONWARNING);
			}
			break;
		}
		}
		return 0;
	case WM_DRAWITEM:
		DrawBtn((DRAWITEMSTRUCT*)lp);
		return TRUE;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT: {
		SetBkColor((HDC)wp, RGB(255, 224, 236));
		SetTextColor((HDC)wp, RGB(80, 30, 60));
		return (LRESULT)gPink;
	}
	case WM_NOTIFY: {
		NMHDR* nm = (NMHDR*)lp;
		if (nm->hwndFrom == gList && nm->code == NM_CUSTOMDRAW) {
			NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
			if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
			if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
				PaintList(cd);
				return CDRF_SKIPDEFAULT;
			}
		}
		if (nm->idFrom == ID_LIST && nm->code == LVN_BEGINLABELEDIT) {
			int i = ((NMLVDISPINFOW*)lp)->item.iItem;
			if (i < 0 || i >= (int)gItems.size() || gItems[i].up) return TRUE;
		}
		if (nm->idFrom == ID_LIST && nm->code == LVN_ENDLABELEDIT) ApplyRename((NMLVDISPINFOW*)lp);
		if (nm->idFrom == ID_LIST && nm->code == LVN_COLUMNCLICK && !gBusy) {
			int col = ((NMLISTVIEW*)lp)->iSubItem;
			if (gSortCol == col) gSortAsc = !gSortAsc;
			else { gSortCol = col; gSortAsc = 1; }
			Refresh();
		}
		if (nm->idFrom == ID_LIST && nm->code == LVN_ITEMCHANGED && !gRefreshing && !gBusy) UpdateStatus();
		if (nm->idFrom == ID_LIST && nm->code == NM_DBLCLK && !gBusy)
			EnterRow(((NMITEMACTIVATE*)lp)->iItem);
		if (nm->idFrom == ID_LIST && nm->code == LVN_KEYDOWN && !gBusy) {
			WORD key = ((NMLVKEYDOWN*)lp)->wVKey;
			if (key == VK_DELETE && (GetKeyState(VK_SHIFT) & 0x8000) == 0) OnDelete();
			else if (key == VK_RETURN) EnterRow(ListView_GetNextItem(gList, -1, LVNI_FOCUSED));
			else if (key == VK_BACK) GoUp();
		}
		if (nm->idFrom == ID_LIST && nm->code == LVN_BEGINDRAG && !gBusy) {
			NMLISTVIEW* lv = (NMLISTVIEW*)lp;
			if (lv->iItem >= 0 && (ListView_GetItemState(gList, lv->iItem, LVIS_SELECTED) & LVIS_SELECTED) == 0)
				ListView_SetItemState(gList, lv->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			BeginListDrag();
		}
		if (nm->idFrom == ID_LIST && nm->code == NM_RCLICK && !gBusy) {
			POINT pt;
			GetCursorPos(&pt);
			ShowListMenu(pt.x, pt.y);
		}
		return 0;
	}
	case WM_CONTEXTMENU:
		if ((HWND)wp == gList && !gBusy) { ShowListMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0; }
		break;
	case WM_TIMER:
		if (wp == 2) { WatchCut(); return 0; }
		if (wp == 3) { PollWatch(); return 0; }
		if (wp == 1 && gBusy) {
			OxzSnap snap;
			wchar_t elapsed[16], left[16], arc[700], ratio[64], count[80], clock[96], body[2100];
			memset(&snap, 0, sizeof(snap));
			OxzProgRead(&snap);
			if (gProg) SendMessageW(gProg, PBM_SETPOS, snap.overall, 0);
			DWORD now = GetTickCount();
			DWORD ms = snap.startTick ? now - snap.startTick : 0;
			unsigned sec = ms / 1000;
			unsigned hour = sec / 3600;
			if (hour > 99) hour = 99;
			sec %= 3600;
			_snwprintf_s(elapsed, _TRUNCATE, L"%02u:%02u:%02u", hour, sec / 60, sec % 60);
			if (snap.overall > 0 && snap.overall < 100 && snap.startTick) {
				DWORD rem = (DWORD)((unsigned long long)ms * (unsigned)(100 - snap.overall) / (unsigned)snap.overall);
				unsigned rs = rem / 1000, rh = rs / 3600;
				if (rh > 99) rh = 99;
				rs %= 3600;
				_snwprintf_s(left, _TRUNCATE, L"%02u:%02u:%02u", rh, rs / 60, rs % 60);
			} else if (snap.overall >= 100) {
				wcscpy_s(left, L"00:00:00");
			} else {
				wcscpy_s(left, L"--:--:--");
			}
			const wchar_t* leaf = snap.archive;
			const wchar_t* slash = wcsrchr(snap.archive, L'\\');
			if (slash && slash[1]) leaf = slash + 1;
			_snwprintf_s(arc, _TRUNCATE, OxzTxt(OS_PROG_ARC), leaf[0] ? leaf : gBusyText.c_str());
			_snwprintf_s(ratio, _TRUNCATE, OxzTxt(OS_PROG_RATIO), snap.filePct, snap.overall);
			_snwprintf_s(count, _TRUNCATE, OxzTxt(OS_PROG_COUNT), snap.index, snap.total);
			_snwprintf_s(clock, _TRUNCATE, OxzTxt(OS_PROG_TIME), elapsed, left);
			_snwprintf_s(body, _TRUNCATE, L"%s\r\n%s\r\n%s    %s    %s", arc, snap.file, ratio, count, clock);
			SetStatus(body);
		}
		return 0;
	case WM_JOB:
		OnJobDone();
		return 0;
	case WM_OXZ_UPDATE: {
		OxzUpdHit* hit = (OxzUpdHit*)lp;
		if (!hit) return 0;
		if (gBusy || gExit) {
			delete gUpdWait;
			gUpdWait = hit;
			return 0;
		}
		delete gUpdWait;
		gUpdWait = hit;
		OfferPendingUpdate();
		return 0;
	}
	case WM_APP + 12:
		SyncInstalledCopy();
		return 0;
	case WM_APP + 21:
		OxzForceShow(hwnd);
		return 0;
	case WM_OXZ_UPDATE_FAIL: {
		wchar_t* text = (wchar_t*)lp;
		SetBusy(0, NULL);
		if (!gExit) Tell(gHwnd, text && text[0] ? text : OxzTxt(OS_UPDATE_FAIL), MB_ICONWARNING);
		free(text);
		if (!gBusy) SetStatus(OxzTxt(OS_HINT));
		return 0;
	}
	case WM_START:
		BeginStartup();
		return 0;
	case WM_APP + 2:
		Refresh();
		return 0;
	case WM_APPCOMMAND:
		if (GET_APPCOMMAND_LPARAM(lp) == APPCOMMAND_BROWSER_BACKWARD) { GoUp(); return TRUE; }
		break;
	case WM_DESTROY:
		delete gUpdWait;
		gUpdWait = NULL;
		if (gDrop) { RevokeDragDrop(hwnd); gDrop->Release(); gDrop = NULL; }
		DropWork();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RunInstallCommand(const wchar_t* which)
{
	if (!_wcsicmp(which, L"/install")) {
		if (!OxzIsAdmin()) {
			if (!OxzRelaunchElevated(L"/install")) Tell(NULL, OxzTxt(OS_UAC_CANCEL), MB_ICONINFORMATION);
			return;
		}
		std::wstring err;
		bool ok = OxzInstallHere(err);
		Tell(NULL, ok ? OxzTxt(OS_INSTALL_OK) : (err.empty() ? OxzTxt(OS_INSTALL_FAIL) : err.c_str()), ok ? MB_ICONINFORMATION : MB_ICONWARNING);
		return;
	}
	if (!_wcsicmp(which, L"/uninstall")) {
		if (!OxzIsAdmin()) {
			if (!OxzRelaunchElevated(L"/uninstall")) Tell(NULL, OxzTxt(OS_UAC_CANCEL), MB_ICONINFORMATION);
			return;
		}
		std::wstring err;
		bool ok = OxzRemoveInstall(err);
		Tell(NULL, ok ? OxzTxt(OS_UNINSTALL_OK) : OxzTxt(OS_UNINSTALL_FAIL), ok ? MB_ICONINFORMATION : MB_ICONWARNING);
	}
}

static void OxzEscapeJob()
{
	if (GetEnvironmentVariableW(L"OXZ_BROKE", NULL, 0) > 0) return;
	BOOL inJob = FALSE;
	if (!IsProcessInJob(GetCurrentProcess(), NULL, &inJob) || !inJob) return;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
	memset(&info, 0, sizeof(info));
	if (!QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation, &info, sizeof(info), NULL)) return;
	DWORD flags = info.BasicLimitInformation.LimitFlags;
	if ((flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) == 0) return;
	SetEnvironmentVariableW(L"OXZ_BROKE", L"1");
	std::wstring cmd = GetCommandLineW();
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWNORMAL;
	DWORD create = 0;
	if (flags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))
		create |= CREATE_BREAKAWAY_FROM_JOB;
	if (!CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, create, NULL, NULL, &si, &pi)) {
		HWND desk = GetShellWindow();
		DWORD epid = 0;
		if (desk) GetWindowThreadProcessId(desk, &epid);
		HANDLE parent = epid ? OpenProcess(PROCESS_CREATE_PROCESS, FALSE, epid) : NULL;
		if (!parent) return;
		SIZE_T bytes = 0;
		InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
		std::vector<BYTE> mem(bytes ? bytes : 1);
		auto attr = (LPPROC_THREAD_ATTRIBUTE_LIST)&mem[0];
		if (!InitializeProcThreadAttributeList(attr, 1, 0, &bytes) || !UpdateProcThreadAttribute(attr, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &parent, sizeof(parent), NULL, NULL)) {
			DeleteProcThreadAttributeList(attr);
			CloseHandle(parent);
			return;
		}
		STARTUPINFOEXW sx;
		memset(&sx, 0, sizeof(sx));
		sx.StartupInfo.cb = sizeof(sx);
		sx.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
		sx.StartupInfo.wShowWindow = SW_SHOWNORMAL;
		sx.lpAttributeList = attr;
		BOOL ok = CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &sx.StartupInfo, &pi);
		DeleteProcThreadAttributeList(attr);
		CloseHandle(parent);
		if (!ok) return;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	ExitProcess(0);
}

static void OxzForceShow(HWND hwnd)
{
	if (!hwnd || gStartKind == 12 || gStartKind == 13 || gStartKind == 14) return;
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	SetForegroundWindow(hwnd);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show)
{
	OxzEscapeJob();
	OxzLangInit();
	OleInitialize(NULL);
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv && argc >= 3 && !_wcsicmp(argv[1], L"/uninstall_wipe")) {
		Sleep(600);
		OxzRmTree(argv[2]);
		int gone = GetFileAttributesW(argv[2]) == INVALID_FILE_ATTRIBUTES;
		Tell(NULL, OxzTxt(gone ? OS_UNINSTALL_OK : OS_UNINSTALL_FAIL), gone ? MB_ICONINFORMATION : MB_ICONWARNING);
		LocalFree(argv);
		OleUninitialize();
		return 0;
	}
	if (argv && argc >= 2 && !_wcsicmp(argv[1], L"/install_quiet")) {
		int rc = 1;
		if (!OxzIsAdmin()) rc = OxzRelaunchElevated(L"/install_quiet") ? 0 : 1;
		else {
			std::wstring err;
			rc = OxzInstallHere(err) ? 0 : 1;
		}
		LocalFree(argv);
		OleUninitialize();
		return rc;
	}
	if (argv && argc >= 2 && (!_wcsicmp(argv[1], L"/install") || !_wcsicmp(argv[1], L"/uninstall"))) {
		RunInstallCommand(argv[1]);
		LocalFree(argv);
		OleUninitialize();
		return 0;
	}
	if (argv) {
		for (int i = 1; i < argc; ++i) {
			if (!_wcsicmp(argv[i], L"/exit")) gExit = 1;
			else if (!_wcsicmp(argv[i], L"/sub")) gStartSub = 1;
			else if (!_wcsicmp(argv[i], L"/list") && i + 1 < argc) gStartB = argv[++i];
			else if (!_wcsicmp(argv[i], L"/packlist") && i + 1 < argc) { gStartKind = 2; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/exthere") && i + 1 < argc) { gStartKind = 3; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/extractto") && i + 1 < argc) { gStartKind = 4; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/addto") && i + 1 < argc) { gStartKind = 5; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packone") && i + 1 < argc) { gStartKind = 6; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packdir") && i + 1 < argc) { gStartKind = 7; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/editlist") && i + 1 < argc) { gStartKind = 8; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packto") && i + 1 < argc) { gStartKind = 9; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/extdest") && i + 1 < argc) { gStartKind = 10; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/extractdlg") && i + 1 < argc) { gStartKind = 11; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/test") && i + 1 < argc) { gStartKind = 12; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/tagedit") && i + 1 < argc) { gStartKind = 13; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/convert") && i + 1 < argc) { gStartKind = 14; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packeach") && i + 1 < argc) { gStartKind = 15; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packeachto") && i + 1 < argc) { gStartKind = 16; gCloseWhenDone = 1; gStartA = argv[++i]; }
			else if (argv[i][0] != L'/' && gStartKind == 0) { gStartKind = 1; gStartA = argv[i]; }
		}
		LocalFree(argv);
	}
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
	InitCommonControlsEx(&icc);
	if (!gPink) gPink = CreateSolidBrush(RGB(255, 224, 236));
	if (!gMint) gMint = CreateSolidBrush(RGB(255, 234, 238));
	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hi;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = gPink;
	wc.lpszClassName = L"OxzGui";
	RegisterClassW(&wc);
	wc.lpfnWndProc = SetProc;
	wc.lpszClassName = L"OxzSet";
	RegisterClassW(&wc);
	HWND hwnd = CreateWindowExW(OxzLangIsRtl() ? WS_EX_LAYOUTRTL : 0, L"OxzGui", OxzTxt(OS_APP),
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 720, NULL, NULL, hi, NULL);
	int showMain = SW_SHOWNORMAL;
	if (gStartKind == 12 || gStartKind == 13 || gStartKind == 14) showMain = SW_HIDE;
	ShowWindow(hwnd, SW_HIDE);
	if (showMain != SW_HIDE) OxzForceShow(hwnd);
	UpdateWindow(hwnd);
	if (showMain != SW_HIDE) PostMessageW(hwnd, WM_APP + 21, 0, 0);
	if (gStartKind) PostMessageW(hwnd, WM_START, 0, 0);
	if (!gExit && gStartKind != 12 && !gCloseWhenDone) PostMessageW(hwnd, WM_APP + 12, 0, 0);
	if (!gExit && gStartKind != 12 && !gCloseWhenDone) OxzUpdateCheckAsync(hwnd);
	ACCEL acc[11];
	memset(acc, 0, sizeof(acc));
	acc[0].fVirt = FCONTROL | FVIRTKEY; acc[0].key = 'A'; acc[0].cmd = ID_SELECTALL;
	acc[1].fVirt = FCONTROL | FVIRTKEY; acc[1].key = 'C'; acc[1].cmd = ID_COPY;
	acc[2].fVirt = FCONTROL | FVIRTKEY; acc[2].key = 'X'; acc[2].cmd = ID_CUT;
	acc[3].fVirt = FCONTROL | FVIRTKEY; acc[3].key = 'V'; acc[3].cmd = ID_PASTE;
	acc[4].fVirt = FCONTROL | FVIRTKEY; acc[4].key = VK_INSERT; acc[4].cmd = ID_COPY;
	acc[5].fVirt = FSHIFT | FVIRTKEY; acc[5].key = VK_INSERT; acc[5].cmd = ID_PASTE;
	acc[6].fVirt = FSHIFT | FVIRTKEY; acc[6].key = VK_DELETE; acc[6].cmd = ID_CUT;
	acc[7].fVirt = FVIRTKEY; acc[7].key = VK_F2; acc[7].cmd = ID_RENAME;
	acc[8].fVirt = FVIRTKEY; acc[8].key = VK_F5; acc[8].cmd = ID_REFRESH;
	acc[9].fVirt = FVIRTKEY; acc[9].key = VK_BACK; acc[9].cmd = ID_UP;
	acc[10].fVirt = FALT | FVIRTKEY; acc[10].key = VK_UP; acc[10].cmd = ID_UP;
	gAccel = CreateAcceleratorTableW(acc, 11);
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		HWND focus = GetFocus();
		wchar_t cls[32];
		cls[0] = 0;
		if (focus) GetClassNameW(focus, cls, 32);
		int edit = _wcsicmp(cls, L"Edit") == 0;
		if (!gBusy && !edit && GetForegroundWindow() == hwnd && gAccel && TranslateAcceleratorW(hwnd, gAccel, &msg))
			continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (gAccel) DestroyAcceleratorTable(gAccel);
	OleUninitialize();
	return gResult;
}
