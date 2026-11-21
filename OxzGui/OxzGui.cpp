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
#include "OxzLang.h"
#include "OxzWork.h"
#include "OxzInst.h"

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
	ID_LIST, ID_STATUS,
	ID_SET_LANG = 1101, ID_SET_INSTALL, ID_SET_REMOVE, ID_SET_CLOSE, ID_SET_HELP, ID_SET_STAT
};
enum { JOB_OPEN = 1, JOB_SAVE, JOB_EXTRACT, JOB_PACK, JOB_ADD, JOB_EXTHERE, JOB_EXTRACTTO, JOB_EXTAT, JOB_TEST, WM_JOB = WM_APP + 1, WM_START = WM_APP + 5 };

struct Item { std::wstring rel; int dir; unsigned long long size; FILETIME ft; };
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
};
struct BtnSt { WNDPROC old; int hover; int down; };

static HWND gHwnd, gList, gStatus, gSet;
static HFONT gFont;
static HBRUSH gPink, gMint;
static std::wstring gWork, gArchive;
static std::vector<Item> gItems;
static int gOwned = 0, gBusy = 0, gExit = 0;
static DWORD gBusyTick = 0;
static std::wstring gBusyText;
static Job* gJob = NULL;
static int gStartKind = 0;
static int gStartSub = 0;
static int gResult = 0;
static std::wstring gStartA, gStartB;

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
	MoveWindow(gList, 8, top, rc.right - 16, rc.bottom - top - 48, TRUE);
	MoveWindow(gStatus, 8, rc.bottom - 40, rc.right - 16, 32, TRUE);
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
		const int ids[] = { OS_COL_NAME, OS_COL_SIZE, OS_COL_DATE };
		for (int i = 0; i < 3; ++i) {
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

static void DropWork()
{
	if (gOwned && !gWork.empty()) OxzRmTree(gWork);
	gWork.clear();
	gOwned = 0;
	gItems.clear();
}

static bool ItemLess(const Item& a, const Item& b)
{
	if (a.dir != b.dir) return a.dir > b.dir;
	return _wcsicmp(a.rel.c_str(), b.rel.c_str()) < 0;
}

static void Walk(const std::wstring& dir, const std::wstring& rel)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		Item it;
		it.rel = rel.empty() ? fd.cFileName : (rel + L"\\" + fd.cFileName);
		it.ft = fd.ftLastWriteTime;
		std::wstring child = dir + L"\\" + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			it.dir = 1;
			it.size = 0;
			gItems.push_back(it);
			Walk(child, it.rel);
		} else {
			ULARGE_INTEGER u;
			it.dir = 0;
			u.LowPart = fd.nFileSizeLow;
			u.HighPart = fd.nFileSizeHigh;
			it.size = u.QuadPart;
			gItems.push_back(it);
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static void Refresh()
{
	ListView_DeleteAllItems(gList);
	gItems.clear();
	if (!gWork.empty()) Walk(gWork, L"");
	std::sort(gItems.begin(), gItems.end(), ItemLess);
	for (size_t i = 0; i < gItems.size(); ++i) {
		LVITEMW it;
		wchar_t sz[64], when[64];
		memset(&it, 0, sizeof(it));
		it.mask = LVIF_TEXT;
		it.iItem = (int)i;
		it.pszText = (LPWSTR)gItems[i].rel.c_str();
		ListView_InsertItem(gList, &it);
		if (gItems[i].dir) wcscpy_s(sz, OxzTxt(OS_FOLDER));
		else _snwprintf_s(sz, _TRUNCATE, L"%llu", gItems[i].size);
		ListView_SetItemText(gList, (int)i, 1, sz);
		SYSTEMTIME st;
		FILETIME lt;
		when[0] = 0;
		if (FileTimeToLocalFileTime(&gItems[i].ft, &lt) && FileTimeToSystemTime(&lt, &st))
			_snwprintf_s(when, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
		ListView_SetItemText(gList, (int)i, 2, when);
	}
	if (!gBusy) {
		wchar_t msg[128];
		_snwprintf_s(msg, _TRUNCATE, OxzTxt(OS_ITEMS), (unsigned)gItems.size());
		SetStatus(msg);
	}
	SetTitle();
}

static std::wstring DirForRow(int sel)
{
	if (gWork.empty()) return L"";
	if (sel < 0 || sel >= (int)gItems.size()) return gWork;
	const Item& it = gItems[sel];
	if (it.dir) return gWork + L"\\" + it.rel;
	size_t slash = it.rel.find_last_of(L'\\');
	if (slash == std::wstring::npos) return gWork;
	return gWork + L"\\" + it.rel.substr(0, slash);
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
	if (on) {
		SetStatus(gBusyText.c_str());
		SetTimer(gHwnd, 1, 500, NULL);
	} else {
		KillTimer(gHwnd, 1);
	}
}

static std::wstring FolderOf(const std::wstring& oxz)
{
	std::wstring parent = OxzParent(oxz);
	std::wstring name = OxzBaseName(oxz);
	if (OxzIsOxz(name)) name.resize(name.size() - 4);
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
	if (OxzIsOxz(name)) name.resize(name.size() - 4);
	if (name.empty()) name = L"oxz";
	return name;
}

static DWORD WINAPI JobThread(LPVOID p)
{
	Job* j = (Job*)p;
	j->rc = 2;
	j->bytes = 0;
	if (!OxzHas7za()) {
		j->err = OxzTxt(OS_NO7ZA);
		PostMessageW(gHwnd, WM_JOB, 0, 0);
		return 0;
	}
	if (j->kind == JOB_OPEN || j->kind == JOB_EXTRACT) {
		j->rc = OxzExtractTo(j->archive.c_str(), j->dest.c_str(), j->err);
	} else if (j->kind == JOB_SAVE) {
		j->rc = OxzPackWork(j->work, j->archive, j->bytes, j->err);
	} else if (j->kind == JOB_PACK || j->kind == JOB_ADD) {
		std::wstring existing;
		if (j->kind == JOB_ADD) existing = j->archive;
		else if (GetFileAttributesW(j->archive.c_str()) != INVALID_FILE_ATTRIBUTES) existing = j->archive;
		j->merged = existing.empty() ? 0 : 1;
		j->rc = OxzMergePack(existing, j->inputs, j->archive, j->bytes, j->err);
		if (j->rc == 0) {
			std::wstring view = OxzNewWork();
			std::wstring ignore;
			if (OxzExtractTo(j->archive.c_str(), view.c_str(), ignore) == 0) j->dest = view;
			else OxzRmTree(view);
		}
	} else if (j->kind == JOB_EXTHERE || j->kind == JOB_EXTRACTTO) {
		j->rc = 0;
		int any = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			if (!OxzIsOxz(j->inputs[i])) continue;
			std::wstring dest = (j->kind == JOB_EXTHERE) ? OxzParent(j->inputs[i]) : FolderOf(j->inputs[i]);
			std::wstring e;
			int rc = OxzExtractTo(j->inputs[i].c_str(), dest.c_str(), e);
			any++;
			if (rc) { j->rc = rc; j->err = e; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_OPEN_FAIL); }
	} else if (j->kind == JOB_EXTAT) {
		j->rc = 0;
		int any = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (!OxzIsOxz(j->inputs[i]) || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			std::wstring dest = j->sub ? JoinPath(j->dest, StemOf(j->inputs[i])) : j->dest;
			std::wstring e;
			int rc = OxzExtractTo(j->inputs[i].c_str(), dest.c_str(), e);
			any++;
			if (rc) { j->rc = rc; j->err = e; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_OPEN_FAIL); }
	} else if (j->kind == JOB_TEST) {
		j->rc = 0;
		int any = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			DWORD a = GetFileAttributesW(j->inputs[i].c_str());
			if (!OxzIsOxz(j->inputs[i]) || a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			std::wstring e;
			int rc = OxzTest(j->inputs[i].c_str(), e);
			any++;
			if (rc) { j->rc = rc; j->err = e; }
		}
		if (!any) { j->rc = 2; j->err = OxzTxt(OS_TEST_FAIL); }
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

static int PickArchive(std::wstring& path, int save)
{
	wchar_t buf[MAX_PATH];
	OPENFILENAMEW ofn;
	buf[0] = 0;
	if (!gArchive.empty() && save) wcsncpy_s(buf, gArchive.c_str(), _TRUNCATE);
	std::vector<wchar_t> filt = MakeFilter(OxzTxt(OS_OXZFILTER), L"*.oxz", OxzTxt(OS_ALLFILES), L"*.*");
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
		if (i < (int)gItems.size()) rels.push_back(gItems[i].rel);
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
	if (sel >= 0) ListView_EditLabel(gList, sel);
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
	for (size_t i = 0; i < gItems.size(); ++i)
		if (!gItems[i].dir) return 1;
	return 0;
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
	j->inputs = inputs;
	if (add) j->archive = archive;
	else if (exact) j->archive = AvoidDest(archive, inputs);
	else j->archive = AvoidDest(OxzDestFor(inputs), inputs);
	if (j->archive.empty()) { delete j; SetBusy(0, NULL); Tell(gHwnd, OxzTxt(OS_SAVE_FAIL), MB_ICONWARNING); return; }
	StartJob(j);
}

static void StartExtractMany(const std::vector<std::wstring>& inputs, int toFolder)
{
	SetBusy(1, OxzTxt(OS_EXTRACTING));
	Job* j = NewJob();
	j->kind = toFolder ? JOB_EXTRACTTO : JOB_EXTHERE;
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
	for (size_t i = 0; i < inputs.size(); ++i) {
		DWORD a = GetFileAttributesW(inputs[i].c_str());
		if (OxzIsOxz(inputs[i]) && a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) n++;
	}
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
	if (!fail && (j->kind == JOB_PACK || j->kind == JOB_ADD) && !j->dest.empty()) {
		DropWork();
		gWork = j->dest;
		gOwned = 1;
		gArchive = j->archive;
		Refresh();
	} else if (j->kind == JOB_OPEN) {
		Refresh();
	}
	if (fail) {
		if (!gExit) Tell(gHwnd, j->err.empty() ? OxzTxt(OS_SAVE_FAIL) : j->err.c_str(), MB_ICONWARNING);
	} else if (j->kind == JOB_SAVE || j->kind == JOB_PACK || j->kind == JOB_ADD) {
		gArchive = j->archive;
		wchar_t msg[400];
		const wchar_t* head = (j->kind == JOB_ADD || j->merged) ? OxzTxt(OS_ADDED) : (j->kind == JOB_PACK ? OxzTxt(OS_CREATED) : OxzTxt(OS_SAVED));
		_snwprintf_s(msg, _TRUNCATE, L"%s  %llu %s   %s", head, j->bytes, OxzTxt(OS_BYTES), j->archive.c_str());
		SetStatus(msg);
		SetTitle();
	} else if (j->kind == JOB_EXTRACT || j->kind == JOB_EXTHERE || j->kind == JOB_EXTRACTTO || j->kind == JOB_EXTAT) {
		SetStatus(OxzTxt(OS_EXTRACTED));
	}
	delete j;
	if (gExit) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
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
	COLORREF bg = sel ? RGB(176, 158, 228) : (row % 2 ? RGB(230, 244, 238) : RGB(252, 236, 244));
	COLORREF fg = sel ? RGB(40, 28, 88) : RGB(0, 0, 0);
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
	wchar_t text[1024];
	text[0] = 0;
	ListView_GetItemText(gList, row, col, text, 1024);
	SetBkMode(cd->nmcd.hdc, TRANSPARENT);
	SetTextColor(cd->nmcd.hdc, fg);
	SelectObject(cd->nmcd.hdc, gFont);
	RECT tr = rc;
	tr.left += 8;
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
	if (oxzDirect && OxzIsOxz(arg) && !(GetFileAttributesW(arg.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) {
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
	IFACEMETHODIMP DragOver(DWORD, POINTL, DWORD* effect) { if (effect) *effect = gBusy ? DROPEFFECT_NONE : m_effect; return S_OK; }
	IFACEMETHODIMP DragLeave() { return S_OK; }
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
		if (paths.size() == 1 && OxzIsOxz(paths[0]) && (GetFileAttributesW(paths[0].c_str()) & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			if (!gItems.empty() && MessageBoxW(gHwnd, OxzTxt(OS_REPLACE_ASK), OxzTxt(OS_APP), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
				return S_OK;
			OnOpenPath(paths[0]);
			return S_OK;
		}
		EnsureWork();
		POINT c = { pt.x, pt.y };
		ScreenToClient(gList, &c);
		LVHITTESTINFO ht;
		memset(&ht, 0, sizeof(ht));
		ht.pt = c;
		int row = ListView_HitTest(gList, &ht);
		std::wstring dest = DirForRow(row);
		int n = 0;
		for (size_t i = 0; i < paths.size(); ++i) {
			if (UnderWork(paths[i])) continue;
			OxzImportOne(paths[i], dest);
			n++;
		}
		Refresh();
		if (n) SetStatus(OxzTxt(OS_ADDED));
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
	if (row >= 0) ListView_SetItemState(gList, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	HMENU m = CreatePopupMenu();
	AppendMenuW(m, MF_STRING, ID_ADDFILE, OxzTxt(OS_ADDFILE));
	AppendMenuW(m, MF_STRING, ID_ADDFOLDER, OxzTxt(OS_ADDFOLDER));
	AppendMenuW(m, MF_STRING, ID_DEL, OxzTxt(OS_DEL));
	AppendMenuW(m, MF_STRING, ID_RENAME, OxzTxt(OS_RENAME));
	TrackPopupMenu(m, TPM_RIGHTBUTTON, x, y, 0, gHwnd, NULL);
	DestroyMenu(m);
}

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
		col.mask = LVCF_TEXT | LVCF_WIDTH;
		col.pszText = (LPWSTR)L""; col.cx = 460;
		ListView_InsertColumn(gList, 0, &col);
		col.cx = 120; ListView_InsertColumn(gList, 1, &col);
		col.cx = 180; ListView_InsertColumn(gList, 2, &col);
		gStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, (HMENU)ID_STATUS, NULL, NULL);
		SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
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
		}
		return 0;
	case WM_DRAWITEM:
		DrawBtn((DRAWITEMSTRUCT*)lp);
		return TRUE;
	case WM_CTLCOLORSTATIC: {
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
		if (nm->idFrom == ID_LIST && nm->code == LVN_ENDLABELEDIT) ApplyRename((NMLVDISPINFOW*)lp);
		if (nm->idFrom == ID_LIST && nm->code == LVN_KEYDOWN && !gBusy && ((NMLVKEYDOWN*)lp)->wVKey == VK_DELETE) OnDelete();
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
		if (gBusy) {
			unsigned sec = (GetTickCount() - gBusyTick) / 1000;
			wchar_t buf[500];
			_snwprintf_s(buf, _TRUNCATE, L"%s  %u%s", gBusyText.c_str(), sec, OxzTxt(OS_SEC));
			SetStatus(buf);
		}
		return 0;
	case WM_JOB:
		OnJobDone();
		return 0;
	case WM_START:
		BeginStartup();
		return 0;
	case WM_APP + 2:
		Refresh();
		return 0;
	case WM_DESTROY:
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

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show)
{
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
			else if (!_wcsicmp(argv[i], L"/packlist") && i + 1 < argc) { gStartKind = 2; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/exthere") && i + 1 < argc) { gStartKind = 3; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/extractto") && i + 1 < argc) { gStartKind = 4; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/addto") && i + 1 < argc) { gStartKind = 5; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packone") && i + 1 < argc) { gStartKind = 6; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packdir") && i + 1 < argc) { gStartKind = 7; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/editlist") && i + 1 < argc) { gStartKind = 8; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/packto") && i + 1 < argc) { gStartKind = 9; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/extdest") && i + 1 < argc) { gStartKind = 10; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/extractdlg") && i + 1 < argc) { gStartKind = 11; gStartA = argv[++i]; }
			else if (!_wcsicmp(argv[i], L"/test") && i + 1 < argc) { gStartKind = 12; gStartA = argv[++i]; }
			else if (argv[i][0] != L'/' && gStartKind == 0) { gStartKind = 1; gStartA = argv[i]; }
		}
		LocalFree(argv);
	}
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
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
	ShowWindow(hwnd, gStartKind == 12 ? SW_HIDE : show);
	UpdateWindow(hwnd);
	if (gStartKind) PostMessageW(hwnd, WM_START, 0, 0);
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	OleUninitialize();
	return gResult;
}
