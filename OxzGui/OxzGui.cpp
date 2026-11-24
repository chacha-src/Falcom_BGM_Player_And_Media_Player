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
#include "OxzLook.h"
#include "../OxzCore/OxzCore.h"

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
	ID_LIST, ID_STATUS, ID_PROG, ID_TESTVIEW, ID_MAP,
	ID_PROG_FILE, ID_LBL_ALL, ID_LBL_FILE, ID_PCT_ALL, ID_PCT_FILE,
	ID_COPY = 1301, ID_CUT, ID_PASTE, ID_SELECTALL, ID_REFRESH, ID_UP, ID_OPENSEL,
	ID_CONV_OXZ = 1310, ID_CONV_ZIP, ID_CONV_TAR,
	ID_SET_LANG = 1101, ID_SET_INSTALL, ID_SET_REMOVE, ID_SET_CLOSE, ID_SET_HELP, ID_SET_STAT,
	ID_SET_LEVEL = 1107, ID_SET_LEVEL_LBL, ID_SET_LEVEL_VAL, ID_SET_LEVEL_HINT, ID_SET_PPMD
};
enum { JOB_OPEN = 1, JOB_SAVE, JOB_EXTRACT, JOB_PACK, JOB_ADD, JOB_EXTHERE, JOB_EXTRACTTO, JOB_EXTAT, JOB_TEST, JOB_DROPSAVE, JOB_PACKEACH, JOB_CONVERT, WM_JOB = WM_APP + 1, WM_START = WM_APP + 5 };

struct Item {
	std::wstring rel;
	std::wstring name;
	int dir;
	int up;
	unsigned long long size;
	unsigned long long packed;
	int packedKnown;
	int method;
	int flags;
	std::wstring codec;
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
	std::vector<OxzEnt> keep;
	std::vector<OxzTestRow> report;
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

static HWND gHwnd, gList, gStatus, gProg, gProgFile, gLblAll, gLblFile, gPctAll, gPctFile, gSet;
static HACCEL gAccel = NULL;
static HFONT gFont;
static HBRUSH gPink, gMint;
static std::wstring gWork, gArchive, gBrowse;
static std::vector<Item> gItems;
static int gHasFile = 0, gSortCol = 0, gSortAsc = 1, gDropHi = -1;
static std::vector<std::wstring> gCut;
static OxzUpdHit* gUpdWait = NULL;
static int gOwned = 0, gBusy = 0, gExit = 0, gCloseWhenDone = 0, gSyncDone = 0, gRefreshing = 0, gVirtual = 0;
static std::vector<OxzEnt> gForeign;
static HIMAGELIST gIcons = NULL;
static DWORD gBusyTick = 0;
static std::wstring gBusyText;
static Job* gJob = NULL;
static int gStartKind = 0;
static int gStartSub = 0;
static int gResult = 0;
static std::wstring gStartA, gStartB, gPendingBrowse;
static std::vector<OpenWatch> gWatch;
static int gWatchPause = 0;
static void ClearWatch();
static void ShowLookMenu(HWND owner, int x, int y);
static int gInMenu = 0;
static int gCtxX, gCtxY, gCtxList, gCtxQueued;
static HWND gCtxFrom;
static void QueueMenu(HWND owner, HWND from, int x, int y, int onList)
{
	if (!owner || gCtxQueued || gInMenu) return;
	if (x == -1 && y == -1) {
		POINT pt;
		GetCursorPos(&pt);
		x = pt.x;
		y = pt.y;
	}
	gCtxX = x;
	gCtxY = y;
	gCtxList = onList;
	gCtxFrom = from ? from : owner;
	gCtxQueued = 1;
	PostMessageW(owner, WM_APP + 30, 0, 0);
}
static int LaunchWorkFile(const std::wstring& path);
static void WatchAfterSave(int fail);
static void SetBusy(int on, const wchar_t* text);

static const int kBtnId[] = { ID_OPEN, ID_NEW, ID_ADDFILE, ID_ADDFOLDER, ID_DEL, ID_RENAME, ID_EXTRACT, ID_SAVE, ID_TESTVIEW, ID_MAP, ID_SETTINGS };
static const int kBtnStr[] = { OS_OPEN, OS_NEW, OS_ADDFILE, OS_ADDFOLDER, OS_DEL, OS_RENAME, OS_EXTRACT, OS_SAVE, OS_TEST, OS_MAP, OS_SETTINGS };

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
	OxzApplyChrome(hwnd);
}

static BOOL CALLBACK ApplyFontCb(HWND child, LPARAM lp)
{
	SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
	return TRUE;
}

static void RefreshLook()
{
	const OxzPal& p = OxzLook();
	HFONT nf = OxzCreateUiFont(gHwnd ? gHwnd : gSet);
	HFONT old = gFont;
	gFont = nf;
	if (gPink) DeleteObject(gPink);
	if (gMint) DeleteObject(gMint);
	gPink = CreateSolidBrush(p.bg0);
	gMint = CreateSolidBrush(p.face);
	if (gHwnd) {
		OxzRefreshChrome(gHwnd);
		EnumChildWindows(gHwnd, ApplyFontCb, (LPARAM)gFont);
		if (gList) {
			ListView_SetBkColor(gList, p.face);
			ListView_SetTextBkColor(gList, p.face);
		}
		if (gProg) {
			SendMessageW(gProg, PBM_SETBARCOLOR, 0, (LPARAM)p.accent);
			SendMessageW(gProg, PBM_SETBKCOLOR, 0, (LPARAM)p.face);
			OxzSetBarInk(gProg, p.accent);
		}
		if (gProgFile) {
			SendMessageW(gProgFile, PBM_SETBARCOLOR, 0, (LPARAM)p.accent2);
			SendMessageW(gProgFile, PBM_SETBKCOLOR, 0, (LPARAM)p.face);
			OxzSetBarInk(gProgFile, p.accent2);
		}
		RedrawWindow(gHwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
	if (gSet) {
		OxzRefreshChrome(gSet);
		EnumChildWindows(gSet, ApplyFontCb, (LPARAM)gFont);
		RedrawWindow(gSet, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
	if (old) DeleteObject(old);
}

static void MakeFont(HWND hwnd)
{
	HFONT nf = OxzCreateUiFont(hwnd);
	HFONT old = gFont;
	gFont = nf;
	if (old) DeleteObject(old);
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
	OxzPaintButton(di, gFont);
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
	for (int i = 0; i < (int)(sizeof(kBtnId) / sizeof(kBtnId[0])); ++i) {
		HWND b = GetDlgItem(gHwnd, kBtnId[i]);
		wchar_t text[256];
		GetWindowTextW(b, text, 256);
		int w = MeasureText(gHwnd, text);
		if (x > 8 && x + w > maxX) { x = 8; y += rowH + 6; }
		MoveWindow(b, x, y, w, rowH, TRUE);
		x += w + 6;
	}
	int top = y + rowH + 8;
	int foot = gBusy ? 176 : 56;
	int listH = rc.bottom - top - foot;
	if (listH < 40) listH = 40;
	MoveWindow(gList, 8, top, rc.right - 16, listH, TRUE);
	if (gProg) {
		HWND bars[] = { gProg, gProgFile, gLblAll, gLblFile, gPctAll, gPctFile };
		if (gBusy) {
			int lab = MeasureText(gHwnd, OxzTxt(OS_PROG_FILE));
			int lab2 = MeasureText(gHwnd, OxzTxt(OS_PROG_ALL));
			int pctW = 78;
			int yAll = rc.bottom - 168;
			int yFile = rc.bottom - 140;
			int barX, barW;
			if (lab2 > lab) lab = lab2;
			if (lab < 72) lab = 72;
			if (lab > 168) lab = 168;
			barX = 12 + lab;
			barW = rc.right - barX - pctW - 16;
			if (barW < 48) barW = 48;
			MoveWindow(gLblAll, 8, yAll, lab, 22, TRUE);
			MoveWindow(gProg, barX, yAll + 3, barW, 16, TRUE);
			MoveWindow(gPctAll, barX + barW + 6, yAll, pctW, 22, TRUE);
			MoveWindow(gLblFile, 8, yFile, lab, 22, TRUE);
			MoveWindow(gProgFile, barX, yFile + 3, barW, 16, TRUE);
			MoveWindow(gPctFile, barX + barW + 6, yFile, pctW, 22, TRUE);
			for (int i = 0; i < 6; ++i) if (bars[i]) ShowWindow(bars[i], SW_SHOW);
		} else {
			for (int i = 0; i < 6; ++i) if (bars[i]) ShowWindow(bars[i], SW_HIDE);
		}
	}
	MoveWindow(gStatus, 8, gBusy ? rc.bottom - 112 : rc.bottom - 48, rc.right - 16, gBusy ? 104 : 40, TRUE);
}

static void ApplySortArrow()
{
	if (!gList) return;
	HWND hdr = ListView_GetHeader(gList);
	if (!hdr) return;
	int n = Header_GetItemCount(hdr);
	for (int i = 0; i < n; ++i) {
		HDITEMW it;
		memset(&it, 0, sizeof(it));
		it.mask = HDI_FORMAT;
		if (!Header_GetItem(hdr, i, &it)) continue;
		it.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
		if (i == gSortCol) it.fmt |= gSortAsc ? HDF_SORTUP : HDF_SORTDOWN;
		Header_SetItem(hdr, i, &it);
	}
}

static void SaveSort()
{
	DWORD col = (DWORD)gSortCol;
	DWORD asc = gSortAsc ? 1u : 0u;
	if (col > 10) col = 0;
	RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"sortCol", REG_DWORD, &col, sizeof(col));
	RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"sortAsc", REG_DWORD, &asc, sizeof(asc));
}

static void LoadSort()
{
	DWORD col = 0, asc = 1, cb = sizeof(DWORD);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"sortCol", RRF_RT_REG_DWORD, NULL, &col, &cb) != ERROR_SUCCESS || col > 10)
		col = 0;
	cb = sizeof(DWORD);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"sortAsc", RRF_RT_REG_DWORD, NULL, &asc, &cb) != ERROR_SUCCESS)
		asc = 1;
	gSortCol = (int)col;
	gSortAsc = asc ? 1 : 0;
}

static void Relabel()
{
	if (!gHwnd) return;
	for (int i = 0; i < (int)(sizeof(kBtnId) / sizeof(kBtnId[0])); ++i)
		SetWindowTextW(GetDlgItem(gHwnd, kBtnId[i]), OxzTxt(kBtnStr[i]));
	if (gList) {
		LVCOLUMNW col;
		memset(&col, 0, sizeof(col));
		col.mask = LVCF_TEXT;
		const int ids[] = { OS_COL_NAME, OS_COL_SIZE, OS_COL_HUMAN, OS_COL_PACKED, OS_COL_PACKHUMAN, OS_COL_RATIO, OS_COL_CODEC, OS_COL_TYPE, OS_COL_ATTR, OS_COL_DATE, OS_COL_CREATED };
		for (int i = 0; i < 11; ++i) {
			col.pszText = (LPWSTR)OxzTxt(ids[i]);
			ListView_SetColumn(gList, i, &col);
		}
	}
	if (gLblAll) SetWindowTextW(gLblAll, OxzTxt(OS_PROG_ALL));
	if (gLblFile) SetWindowTextW(gLblFile, OxzTxt(OS_PROG_FILE));
	SetTitle();
	ApplyRtl(gHwnd);
	ApplyCaption(gHwnd);
	Relayout();
	ApplySortArrow();
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
	OxzCoreForget();
	ClearWatch();
	if (gOwned && !gWork.empty()) OxzRmTree(gWork);
	gWork.clear();
	gOwned = 0;
	gVirtual = 0;
	gForeign.clear();
	gBrowse.clear();
	gHasFile = 0;
	gItems.clear();
}

static std::wstring LeafOf(const std::wstring& rel)
{
	size_t s = rel.find_last_of(L'\\');
	return s == std::wstring::npos ? rel : rel.substr(s + 1);
}

static void CodecLabel(const Item& it, wchar_t* out, size_t n)
{
	const wchar_t* s = L"";
	if (!out || n == 0) return;
	out[0] = 0;
	if (it.up) return;
	if (!it.codec.empty()) {
		wcsncpy_s(out, n, it.codec.c_str(), _TRUNCATE);
		return;
	}
	if (it.dir || it.method < 0) return;
	switch (it.method) {
	case 0: s = L"Store"; break;
	case 1: s = L"Zstd"; break;
	case 2: s = L"LZ4"; break;
	case 3: s = L"Deflate"; break;
	case 4: s = L"LZMA2"; break;
	case 5: s = L"Brotli"; break;
	case 8: s = L"DeflateRaw"; break;
	case 9: s = L"Lizard"; break;
	case 10: s = L"LZFSE"; break;
	case 11: s = L"bzip2"; break;
	case 12: s = L"PPMd"; break;
	case 100: s = L"7z"; break;
	default: s = L"?"; break;
	}
	wcsncpy_s(out, n, s, _TRUNCATE);
	if (it.flags & 1) wcscat_s(out, n, L"+RIP");
	if (it.flags & 2) wcscat_s(out, n, L"+d1");
	if (it.flags & 4) wcscat_s(out, n, L"+d4");
}

static void FormatAttr(DWORD attr, wchar_t* out);
static void KindText(const Item& it, wchar_t* out, size_t n);

static int CmpItem(const Item& a, const Item& b)
{
	int c = 0;
	if (gSortCol == 0 && a.dir != b.dir) return a.dir > b.dir ? -1 : 1;
	if (gSortCol == 1 || gSortCol == 2 || gSortCol == 3 || gSortCol == 4 || gSortCol == 5) {
		unsigned long long av, bv;
		if (gSortCol == 5) {
			av = (a.packedKnown && a.size) ? (a.packed * 10000ull) / a.size : 0;
			bv = (b.packedKnown && b.size) ? (b.packed * 10000ull) / b.size : 0;
		} else if (gSortCol == 1 || gSortCol == 2) {
			av = a.size;
			bv = b.size;
		} else {
			av = a.packedKnown ? a.packed : 0;
			bv = b.packedKnown ? b.packed : 0;
		}
		c = av < bv ? -1 : av > bv ? 1 : 0;
	} else if (gSortCol == 6) {
		wchar_t as[64], bs[64];
		CodecLabel(a, as, 64);
		CodecLabel(b, bs, 64);
		c = _wcsicmp(as, bs);
	} else if (gSortCol == 7) {
		wchar_t as[64], bs[64];
		KindText(a, as, 64);
		KindText(b, bs, 64);
		c = _wcsicmp(as, bs);
	} else if (gSortCol == 8) {
		wchar_t as[16], bs[16];
		FormatAttr(a.attr, as);
		FormatAttr(b.attr, bs);
		c = _wcsicmp(as, bs);
	} else if (gSortCol == 9 || gSortCol == 10) {
		const FILETIME& af = gSortCol == 10 ? a.created : a.ft;
		const FILETIME& bf = gSortCol == 10 ? b.created : b.ft;
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
		it.method = -1;
		it.flags = 0;
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
	if (gVirtual) return;
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

static void KindText(const Item& it, wchar_t* out, size_t n)
{
	if (!out || n == 0) return;
	out[0] = 0;
	if (it.up) return;
	if (it.dir) {
		wcsncpy_s(out, n, OxzTxt(OS_FOLDER), _TRUNCATE);
		return;
	}
	size_t dot = it.name.find_last_of(L'.');
	if (dot != std::wstring::npos && dot + 1 < it.name.size()) {
		wcsncpy_s(out, n, it.name.c_str() + dot + 1, _TRUNCATE);
		_wcsupr_s(out, n);
	}
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
	{
		unsigned long long sz = 0, pk = 0;
		int files = 0, known = 1;
		for (size_t k = 0; k < gItems.size(); ++k) {
			if (gItems[k].up || gItems[k].dir) continue;
			files++;
			sz += gItems[k].size;
			if (!gItems[k].packedKnown) known = 0;
			else pk += gItems[k].packed;
		}
		if (!files) {
			known = 1;
			for (size_t k = 0; k < gItems.size(); ++k) {
				if (gItems[k].up || !gItems[k].dir) continue;
				sz += gItems[k].size;
				if (!gItems[k].packedKnown) known = 0;
				else pk += gItems[k].packed;
			}
		}
		if (known && sz) {
			wchar_t b[64];
			OxzFormatRatio(pk, sz, b, 64);
			s += L"    ";
			s += b;
		}
	}
	SetStatus(s.c_str());
}

static void SumFolders(std::vector<Item>& all);

static void ApplyPack(std::vector<Item>& all)
{
	struct Less {
		bool operator()(const std::wstring& a, const std::wstring& b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
	};
	struct Info { unsigned long long packed, unpacked; int method, flags; };
	std::map<std::wstring, Info, Less> got;
	int base = 0;
	OxzCoreItem* items = NULL;
	int count = 0;
	uint64_t baseOff = 0, baseSize = 0;
	for (size_t i = 0; i < all.size(); ++i) {
		if (!all[i].dir) gHasFile = 1;
		all[i].packed = 0;
		all[i].packedKnown = 0;
		all[i].method = -1;
		all[i].flags = 0;
	}
	if (!gArchive.empty() && OxzCoreIsPkg(gArchive.c_str())
		&& OxzCoreReadDir(gArchive.c_str(), &items, &count, &baseOff, &baseSize) == OXZ_CORE_OK) {
		base = baseSize > 0;
		for (int i = 0; i < count; ++i) {
			Info inf;
			if (!items[i].name || items[i].isDir || items[i].tomb || items[i].overlay < 0) continue;
			inf.packed = items[i].packed;
			inf.unpacked = items[i].unpacked;
			inf.method = items[i].method;
			inf.flags = items[i].flags;
			got[items[i].name] = inf;
		}
		OxzCoreFreeItems(items, count);
	}
	for (size_t i = 0; i < all.size(); ++i) {
		std::map<std::wstring, Info, Less>::iterator it;
		if (all[i].dir) continue;
		it = got.find(all[i].rel);
		if (it != got.end() && it->second.unpacked == all[i].size) {
			all[i].packed = it->second.packed;
			all[i].packedKnown = 1;
			all[i].method = it->second.method;
			all[i].flags = it->second.flags;
		} else if (it == got.end() && base) {
			all[i].method = 100;
		}
	}
	SumFolders(all);
}

static void SumFolders(std::vector<Item>& all)
{
	for (size_t i = 0; i < all.size(); ++i) {
		unsigned long long sum = 0, sumSz = 0;
		int any = 0, allKnown = 1;
		std::wstring prefix;
		if (!all[i].dir) continue;
		prefix = all[i].rel + L"\\";
		for (size_t k = 0; k < all.size(); ++k) {
			if (all[k].dir) continue;
			if (all[k].rel.size() > prefix.size() && _wcsnicmp(all[k].rel.c_str(), prefix.c_str(), prefix.size()) == 0) {
				any = 1;
				sumSz += all[k].size;
				if (!all[k].packedKnown) allKnown = 0;
				else sum += all[k].packed;
			}
		}
		all[i].size = sumSz;
		if (any && allKnown) {
			all[i].packed = sum;
			all[i].packedKnown = 1;
		}
	}
	for (size_t i = 0; i < all.size(); ++i) {
		std::wstring prefix, mark;
		int any = 0, mixed = 0;
		if (!all[i].dir) continue;
		prefix = all[i].rel + L"\\";
		for (size_t k = 0; k < all.size(); ++k) {
			wchar_t label[64];
			std::wstring c;
			if (all[k].dir) continue;
			if (!(all[k].rel.size() > prefix.size() && _wcsnicmp(all[k].rel.c_str(), prefix.c_str(), prefix.size()) == 0)) continue;
			CodecLabel(all[k], label, 64);
			c = label;
			if (c.empty()) continue;
			any = 1;
			if (mark.empty()) mark = c;
			else if (_wcsicmp(mark.c_str(), c.c_str()) != 0) mixed = 1;
		}
		if (any) all[i].codec = mixed ? OxzTxt(OS_MIXED) : mark;
	}
}

static FILETIME FtU(uint64_t v)
{
	FILETIME ft;
	ft.dwLowDateTime = (DWORD)v;
	ft.dwHighDateTime = (DWORD)(v >> 32);
	return ft;
}

static int SameRel(const std::wstring& a, const std::wstring& b)
{
	return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static void FillVirtual(std::vector<Item>& all)
{
	OxzCoreItem* items = NULL;
	int count = 0, i;
	std::vector<Item> disk;
	if (OxzCoreList(&items, &count) != OXZ_CORE_OK) return;
	for (i = 0; i < count; ++i) {
		Item it;
		if (!items[i].name || items[i].tomb) continue;
		memset(&it.ft, 0, sizeof(it.ft));
		memset(&it.created, 0, sizeof(it.created));
		it.rel = items[i].name;
		it.name = LeafOf(it.rel);
		it.dir = items[i].isDir;
		it.up = 0;
		it.size = items[i].unpacked;
		it.packed = items[i].packed;
		it.packedKnown = items[i].isDir ? 0 : 1;
		it.method = items[i].method;
		it.flags = items[i].flags;
		it.ft = FtU(items[i].mtime);
		it.created = it.ft;
		it.attr = items[i].attr;
		if (!it.dir) gHasFile = 1;
		all.push_back(it);
	}
	OxzCoreFreeItems(items, count);
	if (!gWork.empty()) Walk(disk, gWork, L"");
	for (size_t k = 0; k < disk.size(); ++k) {
		size_t hit = all.size();
		for (size_t n = 0; n < all.size(); ++n) {
			if (SameRel(all[n].rel, disk[k].rel)) { hit = n; break; }
		}
		if (hit < all.size()) {
			if (!disk[k].dir && (disk[k].size != all[hit].size || CompareFileTime(&disk[k].ft, &all[hit].ft) != 0)) {
				all[hit].packedKnown = 0;
				all[hit].method = -1;
				all[hit].flags = 0;
				all[hit].codec.clear();
			}
			all[hit].size = disk[k].size;
			all[hit].ft = disk[k].ft;
			all[hit].created = disk[k].created;
			all[hit].attr = disk[k].attr;
		} else {
			if (!disk[k].dir) gHasFile = 1;
			all.push_back(disk[k]);
		}
	}
	SumFolders(all);
}

static void MergeDisk(std::vector<Item>& all, const std::vector<Item>& disk)
{
	for (size_t k = 0; k < disk.size(); ++k) {
		size_t hit = all.size();
		for (size_t n = 0; n < all.size(); ++n) {
			if (SameRel(all[n].rel, disk[k].rel)) { hit = n; break; }
		}
		if (hit < all.size()) {
			if (!disk[k].dir && (disk[k].size != all[hit].size || CompareFileTime(&disk[k].ft, &all[hit].ft) != 0)) {
				all[hit].packedKnown = 0;
				all[hit].method = -1;
				all[hit].flags = 0;
				all[hit].codec.clear();
			}
			all[hit].size = disk[k].size;
			all[hit].ft = disk[k].ft;
			all[hit].created = disk[k].created;
			all[hit].attr = disk[k].attr;
		} else {
			if (!disk[k].dir) gHasFile = 1;
			all.push_back(disk[k]);
		}
	}
}

static void FillForeign(std::vector<Item>& all)
{
	std::vector<Item> disk;
	for (size_t i = 0; i < gForeign.size(); ++i) {
		Item it;
		const OxzEnt& e = gForeign[i];
		memset(&it.ft, 0, sizeof(it.ft));
		memset(&it.created, 0, sizeof(it.created));
		it.rel = e.rel;
		it.name = LeafOf(it.rel);
		it.dir = e.dir;
		it.up = 0;
		it.size = e.size;
		it.packed = e.packed;
		it.packedKnown = e.packedKnown;
		it.method = -1;
		it.flags = 0;
		it.codec = e.codec;
		it.ft = FtU(e.mtime);
		it.created = it.ft;
		it.attr = e.dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
		if (!it.dir) gHasFile = 1;
		all.push_back(it);
	}
	if (!gWork.empty()) Walk(disk, gWork, L"");
	MergeDisk(all, disk);
	SumFolders(all);
}

static void LoadRows(std::vector<Item>& all)
{
	gHasFile = 0;
	if (!gForeign.empty()) FillForeign(all);
	else if (gVirtual && !gArchive.empty()) FillVirtual(all);
	else {
		if (!gWork.empty()) Walk(all, gWork, L"");
		ApplyPack(all);
	}
}

static void Refresh()
{
	std::vector<Item> all;
	gRefreshing = 1;
	ListView_DeleteAllItems(gList);
	gItems.clear();
	LoadRows(all);
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
		up.method = -1;
		up.flags = 0;
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
		wchar_t sz[64], human[64], pk[64], ph[64], ratio[32], codec[64], kind[64], attr[8], when[64], made[64];
		const Item& row = gItems[i];
		memset(&it, 0, sizeof(it));
		it.mask = LVIF_TEXT | LVIF_IMAGE;
		it.iItem = (int)i;
		it.iImage = IconForRow(row, neu, icons);
		it.pszText = (LPWSTR)row.name.c_str();
		ListView_InsertItem(gList, &it);
		sz[0] = human[0] = pk[0] = ph[0] = ratio[0] = codec[0] = kind[0] = attr[0] = when[0] = made[0] = 0;
		if (!row.up) {
			OxzFormatGrouped(row.size, sz, 64);
			FormatUnit(row.size, human, 64);
			if (!row.packedKnown) { wcscpy_s(pk, L"-"); wcscpy_s(ph, L"-"); wcscpy_s(ratio, L"-"); }
			else {
				OxzFormatGrouped(row.packed, pk, 64);
				FormatUnit(row.packed, ph, 64);
				if (row.size) OxzFormatRatio(row.packed, row.size, ratio, 32);
				else wcscpy_s(ratio, L"-");
			}
			if (!row.codec.empty() || (!row.dir && row.method >= 0)) CodecLabel(row, codec, 64);
			else if (!row.dir) wcscpy_s(codec, L"-");
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
		ListView_SetItemText(gList, (int)i, 6, codec);
		ListView_SetItemText(gList, (int)i, 7, kind);
		ListView_SetItemText(gList, (int)i, 8, attr);
		ListView_SetItemText(gList, (int)i, 9, when);
		ListView_SetItemText(gList, (int)i, 10, made);
	}
	HIMAGELIST old = ListView_SetImageList(gList, neu, LVSIL_SMALL);
	if (old) ImageList_Destroy(old);
	gIcons = neu;
	gRefreshing = 0;
	ApplySortArrow();
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

static void EnsureDiskRoot()
{
	if (!gWork.empty()) return;
	gWork = OxzNewWork();
	gOwned = 1;
}

static int EnsureOnDisk(const std::wstring& rel)
{
	std::wstring path, err;
	DWORD a;
	std::vector<OxzEnt> one;
	if (rel.empty()) return 0;
	EnsureDiskRoot();
	path = gWork + L"\\" + rel;
	a = GetFileAttributesW(path.c_str());
	if (a != INVALID_FILE_ATTRIBUTES) return 1;
	for (size_t i = 0; i < gForeign.size(); ++i) {
		if (gForeign[i].dir) continue;
		if (_wcsicmp(gForeign[i].rel.c_str(), rel.c_str()) == 0) { one.push_back(gForeign[i]); break; }
	}
	if (!one.empty()) {
		if (OxzMaterialize(gArchive.c_str(), gWork.c_str(), one, err, 0, 100) != 0) return 0;
		return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}
	if (!gVirtual || gArchive.empty()) return 0;
	return OxzCoreMaterialize(rel.c_str(), path.c_str()) == OXZ_CORE_OK;
}

static void EnsureTree(const std::wstring& rel, int dir)
{
	OxzCoreItem* items = NULL;
	int count = 0, i;
	if (!dir) {
		EnsureOnDisk(rel);
		return;
	}
	EnsureDiskRoot();
	CreateDirectoryW((gWork + L"\\" + rel).c_str(), NULL);
	if (!gForeign.empty()) {
		std::vector<OxzEnt> need;
		std::wstring prefix = rel + L"\\";
		std::wstring err;
		for (size_t n = 0; n < gForeign.size(); ++n) {
			const OxzEnt& e = gForeign[n];
			if (e.dir) continue;
			if (_wcsicmp(e.rel.c_str(), rel.c_str()) == 0 || (e.rel.size() > prefix.size() && _wcsnicmp(e.rel.c_str(), prefix.c_str(), prefix.size()) == 0))
				need.push_back(e);
		}
		if (!need.empty()) OxzMaterialize(gArchive.c_str(), gWork.c_str(), need, err, 0, 100);
		return;
	}
	if (OxzCoreList(&items, &count) != OXZ_CORE_OK) return;
	for (i = 0; i < count; ++i) {
		std::wstring name;
		if (!items[i].name || items[i].isDir || items[i].tomb) continue;
		name = items[i].name;
		if (_wcsicmp(name.c_str(), rel.c_str()) == 0 || (name.size() > rel.size() && _wcsnicmp(name.c_str(), rel.c_str(), rel.size()) == 0 && name[rel.size()] == L'\\'))
			EnsureOnDisk(name);
	}
	OxzCoreFreeItems(items, count);
}

static void EnterRow(int row)
{
	if (gBusy || row < 0 || row >= (int)gItems.size()) return;
	if (gItems[row].up) { GoUp(); return; }
	if (!gItems[row].dir) {
		std::wstring path;
		int ok = GetFileAttributesW((gWork.empty() ? L"" : (gWork + L"\\" + gItems[row].rel)).c_str()) != INVALID_FILE_ATTRIBUTES;
		if (!ok) {
			SetBusy(1, OxzTxt(OS_EXTRACTING));
			ok = EnsureOnDisk(gItems[row].rel);
			SetBusy(0, NULL);
			SetStatus(OxzTxt(OS_HINT));
		}
		path = gWork.empty() ? L"" : (gWork + L"\\" + gItems[row].rel);
		if (!ok || path.empty() || !LaunchWorkFile(path)) Tell(gHwnd, OxzTxt(OS_OPEN_FAIL), MB_ICONWARNING);
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

static void SetBarPos(HWND bar, int pos)
{
	int hi, cur;
	if (!bar) return;
	if (pos < 0) pos = 0;
	if (pos > 1000) pos = 1000;
	hi = (int)SendMessageW(bar, PBM_GETRANGE, FALSE, 0);
	cur = (int)SendMessageW(bar, PBM_GETPOS, 0, 0);
	if (hi != 1000) SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
	if (pos < cur) {
		SendMessageW(bar, PBM_SETRANGE32, 0, 1001);
		SendMessageW(bar, PBM_SETPOS, pos + 1, 0);
		SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
	} else if (pos >= 1000 && cur < 1000) {
		SendMessageW(bar, PBM_SETRANGE32, 0, 1001);
		SendMessageW(bar, PBM_SETPOS, 1001, 0);
		SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
	}
	if (pos == cur && hi == 1000) return;
	SendMessageW(bar, PBM_SETPOS, pos, 0);
	InvalidateRect(bar, NULL, FALSE);
}

static void SetBusy(int on, const wchar_t* text)
{
	gBusy = on;
	gBusyText = text ? text : L"";
	gBusyTick = GetTickCount();
	for (int i = 0; i < (int)(sizeof(kBtnId) / sizeof(kBtnId[0])); ++i)
		EnableWindow(GetDlgItem(gHwnd, kBtnId[i]), on ? FALSE : TRUE);
	if (gProg) SetBarPos(gProg, 0);
	if (gProgFile) SetBarPos(gProgFile, 0);
	if (gPctAll) SetWindowTextW(gPctAll, L"0.0%");
	if (gPctFile) SetWindowTextW(gPctFile, L"0.0%");
	if (on) {
		SetStatus(gBusyText.c_str());
		SetTimer(gHwnd, 1, 100, NULL);
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
	if (j->kind == JOB_OPEN && j->sub == 1) {
		OxzProgBegin(j->archive.c_str());
		j->rc = OxzListArchive(j->archive.c_str(), j->keep, j->err);
	} else if (j->kind == JOB_OPEN || j->kind == JOB_EXTRACT) {
		OxzProgBegin(j->archive.c_str());
		j->rc = OxzExtractTo(j->archive.c_str(), j->dest.c_str(), j->err);
	} else if (j->kind == JOB_SAVE) {
		OxzProgBegin(j->archive.c_str());
		if (!j->keep.empty()) {
			if (j->work.empty()) {
				j->work = OxzNewWork();
				j->sub = 4;
			}
			j->rc = OxzMaterialize(j->dest.c_str(), j->work.c_str(), j->keep, j->err, 0, 35);
		} else j->rc = 0;
		if (j->rc == 0) {
			const wchar_t* name = j->archive.c_str();
			size_t ln = wcslen(name);
			int zip = ln >= 4 && _wcsicmp(name + ln - 4, L".zip") == 0;
			int tar = ln >= 4 && _wcsicmp(name + ln - 4, L".tar") == 0;
			if (zip) j->rc = OxzWriteZip(j->work, j->archive, j->bytes, j->err, 35, 65);
			else if (tar) j->rc = OxzWriteTar(j->work, j->archive, j->bytes, j->err, 35, 65);
			else j->rc = OxzPackWork(j->work, j->archive, j->bytes, j->err, 35, 65);
		}
	} else if (j->kind == JOB_PACK || j->kind == JOB_ADD) {
		std::wstring existing;
		OxzProgBegin(j->archive.c_str());
		if (j->kind == JOB_ADD) existing = j->archive;
		else if (GetFileAttributesW(j->archive.c_str()) != INVALID_FILE_ATTRIBUTES) existing = j->archive;
		j->merged = existing.empty() ? 0 : 1;
		j->rc = OxzMergePack(existing, j->inputs, j->archive, j->bytes, j->err);
		if (j->rc == 0 && !j->closeAfter) {
			if (OxzCoreIsPkg(j->archive.c_str())) j->sub = 2;
			else {
				std::wstring view = OxzNewWork();
				std::wstring ignore;
				if (OxzExtractTo(j->archive.c_str(), view.c_str(), ignore) == 0) j->dest = view;
				else OxzRmTree(view);
			}
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
	} else if (j->kind == JOB_TEST && j->sub == 1) {
		OxzProgBegin(j->archive.c_str());
		j->rc = OxzTestArchive(j->archive.c_str(), j->report, j->err, 0, 100);
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
	} else if (j->kind == JOB_CONVERT) {
		j->rc = 0;
		int any = 0, total = 0;
		for (size_t i = 0; i < j->inputs.size(); ++i)
			if (CanOpen(j->inputs[i])) total++;
		std::vector<std::wstring> used;
		const wchar_t* ext = j->dest.c_str();
		OxzProgBegin(total ? j->inputs[0].c_str() : L"");
		for (size_t i = 0; i < j->inputs.size(); ++i) {
			if (!CanOpen(j->inputs[i])) continue;
			std::wstring work = OxzNewWork();
			std::wstring e;
			int base = 0, span = 100;
			OxzSlice(any, total ? total : 1, base, span);
			int half = span / 2;
			if (half < 1) half = span;
			int rc = OxzExtractTo(j->inputs[i].c_str(), work.c_str(), e, base, half);
			if (rc == 0) {
				std::wstring pick = JoinPath(OxzParent(j->inputs[i]), StemOf(j->inputs[i]) + ext);
				if (OxzSamePath(pick, j->inputs[i]) || GetFileAttributesW(pick.c_str()) != INVALID_FILE_ATTRIBUTES)
					used.push_back(pick);
				pick = UniqueAmong(pick, used, ext);
				used.push_back(pick);
				unsigned long long bytes = 0;
				int rest = span - half;
				if (rest < 1) rest = 1;
				if (_wcsicmp(ext, L".zip") == 0) rc = OxzWriteZip(work, pick, bytes, e, base + half, rest);
				else if (_wcsicmp(ext, L".tar") == 0) rc = OxzWriteTar(work, pick, bytes, e, base + half, rest);
				else rc = OxzPackWork(work, pick, bytes, e, base + half, rest);
				j->bytes += bytes;
				if (rc == 0) j->archive = pick;
			}
			OxzRmTree(work);
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
	std::wstring back = gPendingBrowse;
	gPendingBrowse.clear();
	if (OxzCoreIsPkg(arc.c_str())) {
		OxzCoreItem* items = NULL;
		int count = 0, files = 0, i;
		uint64_t baseOff = 0, baseSize = 0;
		if (OxzCoreReadDir(arc.c_str(), &items, &count, &baseOff, &baseSize) == OXZ_CORE_OK) {
			for (i = 0; i < count; ++i)
				if (items[i].name && !items[i].isDir && !items[i].tomb) files++;
			OxzCoreFreeItems(items, count);
			if (files > 0 || baseSize == 0) {
				std::wstring work = OxzNewWork();
				DropWork();
				ListView_DeleteAllItems(gList);
				gWork = work;
				gOwned = 1;
				gArchive = arc;
				gVirtual = 1;
				if (!back.empty()) gBrowse = back;
				if (OxzCoreRemember(gWork.c_str(), gArchive.c_str()) == OXZ_CORE_OK) {
					Refresh();
					SetStatus(OxzTxt(OS_HINT));
					return;
				}
			}
		}
	}
	SetBusy(1, OxzTxt(OS_OPEN));
	Job* j = NewJob();
	j->kind = JOB_OPEN;
	j->sub = 1;
	j->archive = arc;
	DropWork();
	if (!back.empty()) gBrowse = back;
	ListView_DeleteAllItems(gList);
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
	add(L"*.oxz;*.7z;*.zip;*.rar;*.gz;*.tgz;*.tar;*.tar.gz;*.bz2;*.tbz;*.tbz2;*.tar.bz2;*.xz;*.txz;*.tar.xz;*.zst;*.tzst;*.tar.zst;*.cab;*.iso;*.wim;*.lzh;*.lha;*.arj");
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
	std::vector<wchar_t> filt;
	if (save) {
		auto add = [&](const wchar_t* s) { while (*s) filt.push_back(*s++); filt.push_back(0); };
		add(OxzTxt(OS_OXZFILTER)); add(L"*.oxz");
		add(OxzTxt(OS_ZIPFILTER)); add(L"*.zip");
		add(OxzTxt(OS_TARFILTER)); add(L"*.tar");
		add(OxzTxt(OS_ALLFILES)); add(L"*.*");
		filt.push_back(0);
	} else filt = MakeOpenFilter();
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = gHwnd;
	ofn.lpstrFilter = &filt[0];
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = NULL;
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	if (!ok) return 0;
	path = buf;
	if (save) {
		std::wstring leaf = OxzBaseName(path);
		if (leaf.find(L'.') == std::wstring::npos) {
			if (ofn.nFilterIndex == 2) path += L".zip";
			else if (ofn.nFilterIndex == 3) path += L".tar";
			else path += L".oxz";
		}
	}
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
	if (!gWork.empty()) return;
	if (!gForeign.empty() || (gVirtual && !gArchive.empty())) {
		EnsureDiskRoot();
		return;
	}
	OnNew();
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
		std::wstring path;
		DWORD a = INVALID_FILE_ATTRIBUTES;
		if (!gForeign.empty()) {
			std::vector<OxzEnt> kept;
			std::wstring prefix = rels[k] + L"\\";
			for (size_t n = 0; n < gForeign.size(); ++n) {
				const OxzEnt& e = gForeign[n];
				if (_wcsicmp(e.rel.c_str(), rels[k].c_str()) == 0) continue;
				if (e.rel.size() > prefix.size() && _wcsnicmp(e.rel.c_str(), prefix.c_str(), prefix.size()) == 0) continue;
				kept.push_back(e);
			}
			gForeign.swap(kept);
		} else if (gVirtual) OxzCoreDrop(rels[k].c_str());
		if (!gBrowse.empty() && (_wcsicmp(gBrowse.c_str(), rels[k].c_str()) == 0 || (gBrowse.size() > rels[k].size() && _wcsnicmp(gBrowse.c_str(), rels[k].c_str(), rels[k].size()) == 0 && gBrowse[rels[k].size()] == L'\\')))
			gBrowse.clear();
		if (gWork.empty()) continue;
		path = gWork + L"\\" + rels[k];
		a = GetFileAttributesW(path.c_str());
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

static void OnTestView()
{
	std::wstring arc = gArchive;
	if (arc.empty() && !PickArchive(arc, 0)) return;
	SetBusy(1, OxzTxt(OS_TEST));
	Job* j = NewJob();
	j->kind = JOB_TEST;
	j->sub = 1;
	j->archive = arc;
	StartJob(j);
}

static void OnMap()
{
	std::vector<Item> all;
	std::vector<OxzMapNode> folders, codecs;
	std::vector<OxzMapLink> links;
	unsigned long long size = 0, packed = 0;
	int known = 1, any = 0;
	struct Bucket { std::wstring name; unsigned long long size; unsigned long long packed; };
	std::vector<Bucket> fb, cb;
	if (gArchive.empty() && gWork.empty() && gForeign.empty()) {
		Tell(gHwnd, OxzTxt(OS_NOFILES), MB_ICONINFORMATION);
		return;
	}
	LoadRows(all);
	for (size_t i = 0; i < all.size(); ++i) {
		std::wstring parent, codec;
		wchar_t label[64];
		int fi = -1, ci = -1;
		OxzMapLink link;
		if (all[i].dir) continue;
		any = 1;
		size += all[i].size;
		if (!all[i].packedKnown) known = 0;
		else packed += all[i].packed;
		parent = all[i].rel;
		{
			size_t s = parent.find_last_of(L'\\');
			if (s == std::wstring::npos) parent = L"\\";
			else parent.resize(s);
		}
		CodecLabel(all[i], label, 64);
		codec = label[0] ? label : L"-";
		for (size_t k = 0; k < fb.size(); ++k) if (_wcsicmp(fb[k].name.c_str(), parent.c_str()) == 0) { fi = (int)k; break; }
		if (fi < 0) { Bucket b; b.name = parent; b.size = 0; b.packed = 0; fb.push_back(b); fi = (int)fb.size() - 1; }
		for (size_t k = 0; k < cb.size(); ++k) if (_wcsicmp(cb[k].name.c_str(), codec.c_str()) == 0) { ci = (int)k; break; }
		if (ci < 0) { Bucket b; b.name = codec; b.size = 0; b.packed = 0; cb.push_back(b); ci = (int)cb.size() - 1; }
		fb[fi].size += all[i].size;
		cb[ci].size += all[i].size;
		if (all[i].packedKnown) { fb[fi].packed += all[i].packed; cb[ci].packed += all[i].packed; }
		link.from = fi;
		link.to = ci;
		link.packed = all[i].packedKnown ? all[i].packed : all[i].size;
		{
			int merged = 0;
			for (size_t k = 0; k < links.size(); ++k) if (links[k].from == fi && links[k].to == ci) { links[k].packed += link.packed; merged = 1; break; }
			if (!merged) links.push_back(link);
		}
	}
	if (!any) {
		Tell(gHwnd, OxzTxt(OS_NOFILES), MB_ICONINFORMATION);
		return;
	}
	for (size_t i = 0; i < fb.size(); ++i) { OxzMapNode n; n.name = fb[i].name; n.size = fb[i].size; n.packed = fb[i].packed; folders.push_back(n); }
	for (size_t i = 0; i < cb.size(); ++i) { OxzMapNode n; n.name = cb[i].name; n.size = cb[i].size; n.packed = cb[i].packed; codecs.push_back(n); }
	OxzShowMap(gHwnd, gFont, OxzTxt(OS_MAP), size, packed, known, folders, codecs, links);
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
	if (gWork.empty() && gForeign.empty() && !gVirtual) { Tell(gHwnd, OxzTxt(OS_NOFILES), MB_ICONINFORMATION); return; }
	if (!HasFiles() && gArchive.empty() && gForeign.empty()) { Tell(gHwnd, OxzTxt(OS_NOFILES), MB_ICONINFORMATION); return; }
	std::wstring arc;
	if (!PickArchive(arc, 1)) return;
	if (UnderWork(arc)) { Tell(gHwnd, OxzTxt(OS_SAVE_INSIDE), MB_ICONWARNING); return; }
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_SAVE;
	j->archive = arc;
	j->work = gWork;
	j->dest = gArchive;
	j->keep = gForeign;
	StartJob(j);
}

static void SaveCurrent()
{
	if (gBusy || (gWork.empty() && gForeign.empty()) || (!HasFiles() && gArchive.empty() && gForeign.empty())) return;
	if (!OxzIsOxz(gArchive)) { OnSave(); return; }
	if (UnderWork(gArchive)) return;
	SetBusy(1, OxzTxt(OS_COMPRESSING));
	Job* j = NewJob();
	j->kind = JOB_SAVE;
	j->archive = gArchive;
	j->work = gWork;
	j->dest = gArchive;
	j->keep = gForeign;
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
	if (gBusy || gWatchPause || (gWork.empty() && gForeign.empty()) || (!HasFiles() && gArchive.empty() && gForeign.empty())) return;
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
	j->dest = gArchive;
	j->keep = gForeign;
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
	OxzUpdateBegin(gHwnd, hit, gArchive.c_str(), gBrowse.c_str());
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

static const wchar_t* ConvExt(const std::wstring& fmt)
{
	if (_wcsicmp(fmt.c_str(), L"oxz") == 0 || _wcsicmp(fmt.c_str(), L".oxz") == 0) return L".oxz";
	if (_wcsicmp(fmt.c_str(), L"zip") == 0 || _wcsicmp(fmt.c_str(), L".zip") == 0) return L".zip";
	if (_wcsicmp(fmt.c_str(), L"tar") == 0 || _wcsicmp(fmt.c_str(), L".tar") == 0) return L".tar";
	return NULL;
}

static void StartConvert(const std::vector<std::wstring>& inputs, const std::wstring& fmt)
{
	const wchar_t* ext = ConvExt(fmt);
	std::vector<std::wstring> arcs;
	for (size_t i = 0; i < inputs.size(); ++i)
		if (CanOpen(inputs[i])) arcs.push_back(inputs[i]);
	if (!ext || arcs.empty()) {
		if (!gExit) Tell(gHwnd, OxzTxt(arcs.empty() ? OS_NOTHING : OS_SAVE_FAIL), MB_ICONINFORMATION);
		if (gExit || gCloseWhenDone) PostMessageW(gHwnd, WM_CLOSE, 0, 0);
		return;
	}
	SetBusy(1, OxzTxt(OS_CONVERTING));
	Job* j = NewJob();
	j->kind = JOB_CONVERT;
	j->closeAfter = gCloseWhenDone ? 1 : 0;
	j->inputs = arcs;
	j->dest = ext;
	j->archive = arcs[0];
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
		if (j->sub == 1) {
			if (j->rc) gResult = j->rc;
			if (j->report.empty()) {
				OxzTestRow row;
				row.name = j->archive;
				row.ok = j->rc == 0;
				row.detail = j->rc ? (j->err.empty() ? OxzTxt(OS_TEST_FAIL) : j->err) : L"OK";
				j->report.push_back(row);
			}
			if (!gExit) OxzShowTest(gHwnd, gFont, OxzTxt(OS_TEST), j->report);
			delete j;
			return;
		}
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
	if (j->kind == JOB_OPEN && j->sub == 1) {
		if (!fail) {
			gForeign.swap(j->keep);
			gVirtual = 1;
			gWork.clear();
			gOwned = 0;
			Refresh();
			SetStatus(OxzTxt(OS_HINT));
		}
	} else if (!fail && (j->kind == JOB_PACK || j->kind == JOB_ADD) && j->sub == 2 && !j->closeAfter) {
		DropWork();
		gWork = OxzNewWork();
		gOwned = 1;
		gArchive = j->archive;
		gVirtual = 1;
		if (OxzCoreRemember(gWork.c_str(), gArchive.c_str()) == OXZ_CORE_OK) Refresh();
	} else if (!fail && (j->kind == JOB_PACK || j->kind == JOB_ADD) && !j->dest.empty() && !j->closeAfter) {
		DropWork();
		gWork = j->dest;
		gOwned = 1;
		gArchive = j->archive;
		Refresh();
		OxzCoreRemember(gWork.c_str(), gArchive.c_str());
	} else if (j->kind == JOB_SAVE && !fail) {
		gWork = j->work;
		gOwned = 1;
		gArchive = j->archive;
		gForeign.clear();
		gVirtual = 0;
		OxzCoreForget();
		if (OxzCoreIsPkg(gArchive.c_str())) {
			gVirtual = 1;
			OxzCoreRemember(gWork.c_str(), gArchive.c_str());
		} else {
			std::wstring listErr;
			OxzListArchive(gArchive.c_str(), gForeign, listErr);
			if (!gForeign.empty()) gVirtual = 1;
		}
		Refresh();
	} else if (j->kind == JOB_OPEN || j->kind == JOB_DROPSAVE) {
		if (j->kind == JOB_DROPSAVE && !j->archive.empty()) gArchive = j->archive;
		Refresh();
		if (!fail) OxzCoreRemember(gWork.c_str(), gArchive.c_str());
	}
	if (fail && j->kind == JOB_SAVE && j->sub == 4 && !j->work.empty() && (gWork.empty() || !OxzSamePath(gWork, j->work)))
		OxzRmTree(j->work);
	if (fail) {
		if (!gExit) Tell(gHwnd, j->err.empty() ? OxzTxt(OS_SAVE_FAIL) : j->err.c_str(), MB_ICONWARNING);
	} else if (j->kind == JOB_SAVE || j->kind == JOB_PACK || j->kind == JOB_ADD || j->kind == JOB_DROPSAVE) {
		gArchive = j->archive;
		wchar_t msg[400];
		const wchar_t* head = (j->kind == JOB_ADD || j->kind == JOB_DROPSAVE || j->merged) ? OxzTxt(OS_ADDED) : (j->kind == JOB_PACK ? OxzTxt(OS_CREATED) : OxzTxt(OS_SAVED));
		wchar_t bytes[48];
		OxzFormatGrouped(j->bytes, bytes, 48);
		_snwprintf_s(msg, _TRUNCATE, L"%s  %s %s   %s", head, bytes, OxzTxt(OS_BYTES), j->archive.c_str());
		SetStatus(msg);
		SetTitle();
	} else if (j->kind == JOB_EXTRACT || j->kind == JOB_EXTHERE || j->kind == JOB_EXTRACTTO || j->kind == JOB_EXTAT) {
		SetStatus(OxzTxt(OS_EXTRACTED));
	} else if (j->kind == JOB_PACKEACH) {
		wchar_t msg[200];
		wchar_t bytes[48];
		OxzFormatGrouped(j->bytes, bytes, 48);
		_snwprintf_s(msg, _TRUNCATE, L"%s  %s %s", OxzTxt(OS_CREATED), bytes, OxzTxt(OS_BYTES));
		SetStatus(msg);
	} else if (j->kind == JOB_CONVERT) {
		wchar_t msg[500];
		wchar_t bytes[48];
		OxzFormatGrouped(j->bytes, bytes, 48);
		_snwprintf_s(msg, _TRUNCATE, L"%s  %s %s   %s", OxzTxt(OS_CONVERTED), bytes, OxzTxt(OS_BYTES), j->archive.c_str());
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
	std::wstring src = gWork.empty() ? L"" : (gWork + L"\\" + rel);
	int onDisk, moved = 0, rn;
	if (_wcsicmp(destRel.c_str(), rel.c_str()) == 0) return;
	onDisk = !src.empty() && GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES;
	if (onDisk) {
		if (!MoveFileW(src.c_str(), (gWork + L"\\" + destRel).c_str())) {
			Tell(gHwnd, OxzTxt(OS_RENAME_FAIL), MB_ICONWARNING);
			return;
		}
		moved = 1;
	}
	rn = 0;
	if (!gForeign.empty()) {
		int hit = 0;
		std::wstring prefix = rel + L"\\";
		for (size_t n = 0; n < gForeign.size(); ++n) {
			OxzEnt& e = gForeign[n];
			if (_wcsicmp(e.rel.c_str(), rel.c_str()) == 0) { e.rel = destRel; hit = 1; }
			else if (e.rel.size() > prefix.size() && _wcsnicmp(e.rel.c_str(), prefix.c_str(), prefix.size()) == 0)
				e.rel = destRel + e.rel.substr(rel.size());
		}
		rn = hit ? OXZ_CORE_OK : OXZ_CORE_ERR;
	} else rn = OxzCoreRename(rel.c_str(), destRel.c_str());
	if (!moved && rn != OXZ_CORE_OK) {
		Tell(gHwnd, OxzTxt(OS_RENAME_FAIL), MB_ICONWARNING);
		return;
	}
	PostMessageW(gHwnd, WM_APP + 2, 0, 0);
}

static void PaintList(NMLVCUSTOMDRAW* cd)
{
	int row = (int)cd->nmcd.dwItemSpec;
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
	COLORREF bg, fg;
	OxzRowColors(row, sel, &bg, &fg);
	if (cut && !sel) fg = OxzLook().textDim;
	RECT rowrc;
	if (!ListView_GetItemRect(gList, row, &rowrc, LVIR_BOUNDS)) return;
	if (rowrc.right <= rowrc.left || rowrc.bottom <= rowrc.top) return;
	HWND head = ListView_GetHeader(gList);
	int cols = head ? Header_GetItemCount(head) : 1;
	if (cols < 1) cols = 1;
	int saved = SaveDC(cd->nmcd.hdc);
	int x = rowrc.left;
	for (int col = 0; col < cols; ++col) {
		int w = ListView_GetColumnWidth(gList, col);
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
		tr.left += 8 + iconPad;
		if (tr.left < rc.right)
			DrawTextW(cd->nmcd.hdc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		RestoreDC(cd->nmcd.hdc, clip);
	}
	RestoreDC(cd->nmcd.hdc, saved);
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
	SetWindowTextW(GetDlgItem(gSet, ID_SET_LEVEL_LBL), OxzTxt(OS_LEVEL));
	SetWindowTextW(GetDlgItem(gSet, ID_SET_LEVEL_HINT), OxzTxt(OS_LEVEL_HINT));
	SetWindowTextW(GetDlgItem(gSet, ID_SET_PPMD), OxzTxt(OS_PPMD));
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
	case WM_PARENTNOTIFY:
		if (LOWORD(wp) == WM_CREATE) OxzBindAcrylic(hwnd);
		return 0;
	case WM_CREATE: {
		gSet = hwnd;
		HWND lang = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 16, 200, 24, hwnd, (HMENU)OS_LANG, NULL, NULL);
		HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 16, 44, 220, 280, hwnd, (HMENU)ID_SET_LANG, NULL, NULL);
		HWND help = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 252, 44, 430, 150, hwnd, (HMENU)ID_SET_HELP, NULL, NULL);
		HWND lvl = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 252, 204, 120, 22, hwnd, (HMENU)ID_SET_LEVEL_LBL, NULL, NULL);
		HWND bar = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 372, 200, 220, 32, hwnd, (HMENU)ID_SET_LEVEL, NULL, NULL);
		HWND val = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 600, 204, 36, 22, hwnd, (HMENU)ID_SET_LEVEL_VAL, NULL, NULL);
		HWND hint = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 252, 234, 380, 22, hwnd, (HMENU)ID_SET_LEVEL_HINT, NULL, NULL);
		HWND ppmd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 252, 260, 380, 24, hwnd, (HMENU)ID_SET_PPMD, NULL, NULL);
		HWND st = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 252, 294, 430, 24, hwnd, (HMENU)ID_SET_STAT, NULL, NULL);
		HWND inst = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 252, 328, 200, 34, hwnd, (HMENU)ID_SET_INSTALL, NULL, NULL);
		HWND rem = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 462, 328, 200, 34, hwnd, (HMENU)ID_SET_REMOVE, NULL, NULL);
		HWND closeb = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 462, 372, 200, 34, hwnd, (HMENU)ID_SET_CLOSE, NULL, NULL);
		HWND hs[] = { lang, list, help, lvl, bar, val, hint, ppmd, st, inst, rem, closeb };
		SendMessageW(bar, TBM_SETRANGE, TRUE, MAKELONG(1, 10));
		SendMessageW(bar, TBM_SETPOS, TRUE, OxzCoreGetLevel());
		SendMessageW(ppmd, BM_SETCHECK, OxzCoreGetPpmd() ? BST_CHECKED : BST_UNCHECKED, 0);
		{
			wchar_t num[8];
			_snwprintf_s(num, _TRUNCATE, L"%d", OxzCoreGetLevel());
			SetWindowTextW(val, num);
		}
		for (int i = 0; i < 12; ++i) SendMessageW(hs[i], WM_SETFONT, (WPARAM)gFont, TRUE);
		SubBtn(inst); SubBtn(rem); SubBtn(closeb);
		for (int i = 0; i < 14; ++i) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)OxzLangName(i));
		SendMessageW(list, LB_SETCURSEL, OxzLang(), 0);
		RelabelSet();
		return 0;
	}
	case WM_ERASEBKGND:
		return OxzHostErase(hwnd, (HDC)wp, gPink);
	case WM_PAINT:
		if (!OxzPerPixelAcrylic()) return DefWindowProcW(hwnd, msg, wp, lp);
		OxzHostPaint(hwnd);
		return 0;
	case WM_CONTEXTMENU:
		QueueMenu(hwnd, hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 0);
		return TRUE;
	case WM_APP + 30:
		gCtxQueued = 0;
		ReleaseCapture();
		ShowLookMenu(hwnd, gCtxX, gCtxY);
		return 0;
	case WM_COMMAND:
		if (OxzLookCommand(LOWORD(wp))) { RefreshLook(); return 0; }
		if (HIWORD(wp) == LBN_SELCHANGE && LOWORD(wp) == ID_SET_LANG) {
			int sel = (int)SendMessageW(GetDlgItem(hwnd, ID_SET_LANG), LB_GETCURSEL, 0, 0);
			if (sel >= 0 && sel <= 13) {
				OxzLangSet(sel);
				MakeFont(gHwnd);
				for (int i = 0; i < (int)(sizeof(kBtnId) / sizeof(kBtnId[0])); ++i) SendMessageW(GetDlgItem(gHwnd, kBtnId[i]), WM_SETFONT, (WPARAM)gFont, TRUE);
				SendMessageW(gList, WM_SETFONT, (WPARAM)gFont, TRUE);
				SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
				Relabel();
				HWND hs[] = { GetDlgItem(hwnd, OS_LANG), GetDlgItem(hwnd, ID_SET_LANG), GetDlgItem(hwnd, ID_SET_HELP), GetDlgItem(hwnd, ID_SET_LEVEL_LBL), GetDlgItem(hwnd, ID_SET_LEVEL_HINT), GetDlgItem(hwnd, ID_SET_PPMD), GetDlgItem(hwnd, ID_SET_STAT), GetDlgItem(hwnd, ID_SET_INSTALL), GetDlgItem(hwnd, ID_SET_REMOVE), GetDlgItem(hwnd, ID_SET_CLOSE) };
				for (int i = 0; i < 10; ++i) SendMessageW(hs[i], WM_SETFONT, (WPARAM)gFont, TRUE);
				RelabelSet();
				OxzRefreshMenuText();
				InvalidateRect(gHwnd, NULL, TRUE);
				InvalidateRect(hwnd, NULL, TRUE);
			}
			return 0;
		}
		if (LOWORD(wp) == ID_SET_PPMD) {
			int on = SendMessageW(GetDlgItem(hwnd, ID_SET_PPMD), BM_GETCHECK, 0, 0) == BST_CHECKED;
			DWORD v = on ? 1 : 0;
			OxzCoreSetPpmd(on);
			RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"ppmd", REG_DWORD, &v, sizeof(v));
			return 0;
		}
		if (gBusy) return 0;
		if (LOWORD(wp) == ID_SET_INSTALL) OnInstallClick(0);
		else if (LOWORD(wp) == ID_SET_REMOVE) OnInstallClick(1);
		else if (LOWORD(wp) == ID_SET_CLOSE) DestroyWindow(hwnd);
		return 0;
	case WM_HSCROLL: {
		HWND bar = GetDlgItem(hwnd, ID_SET_LEVEL);
		if ((HWND)lp == bar) {
			int v = (int)SendMessageW(bar, TBM_GETPOS, 0, 0);
			DWORD dv;
			wchar_t num[8];
			if (v < 1) v = 1;
			if (v > 10) v = 10;
			OxzCoreSetLevel(v);
			dv = (DWORD)v;
			RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"level", REG_DWORD, &dv, sizeof(dv));
			_snwprintf_s(num, _TRUNCATE, L"%d", v);
			SetWindowTextW(GetDlgItem(hwnd, ID_SET_LEVEL_VAL), num);
		}
		return 0;
	}
	case WM_DRAWITEM:
		DrawBtn((DRAWITEMSTRUCT*)lp);
		return TRUE;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
		return (LRESULT)OxzHostCtlColor((HDC)wp, msg, msg == WM_CTLCOLORSTATIC ? gPink : gMint);
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
		r.left + 40, r.top + 40, 720, 520, gHwnd, NULL, GetModuleHandleW(NULL), NULL);
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
	else if (gStartKind == 17) { TakePaths(gStartA, paths, 1); StartConvert(paths, gStartB); }
}

static void AcceptPaths(const std::vector<std::wstring>& paths, const std::wstring& into)
{
	if (paths.empty() || gBusy) return;
	if (paths.size() == 1 && CanOpen(paths[0]) && gWork.empty() && gArchive.empty()) {
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

static void ShowLookMenu(HWND owner, int x, int y)
{
	if (gInMenu) return;
	POINT pt;
	if (x == -1 && y == -1) {
		GetCursorPos(&pt);
		x = pt.x;
		y = pt.y;
	}
	if (!owner) owner = gHwnd;
	gInMenu = 1;
	UINT id = OxzOpenLookMenu(owner, x, y, NULL, 0);
	gInMenu = 0;
	if (id) SendMessageW(owner, WM_COMMAND, id, 0);
}

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
	OxzPopAdd add[20];
	int n = 0;
	int sel = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
	if (sel >= 0 && sel < (int)gItems.size() && !gItems[sel].up) {
		add[n].id = ID_OPENSEL;
		add[n].text = OxzTxt(OS_OPEN);
		add[n].sep = 0;
		n++;
	}
	UINT ids[] = { ID_COPY, ID_CUT, ID_PASTE, ID_SELECTALL, ID_ADDFILE, ID_ADDFOLDER, ID_DEL, ID_RENAME };
	int strs[] = { OS_COPY, OS_CUT, OS_PASTE, OS_SELECTALL, OS_ADDFILE, OS_ADDFOLDER, OS_DEL, OS_RENAME };
	for (int i = 0; i < 8 && n < 16; ++i) {
		add[n].id = ids[i];
		add[n].text = OxzTxt(strs[i]);
		add[n].sep = (ids[i] == ID_ADDFILE) ? 1 : 0;
		n++;
	}
	if (!gArchive.empty() && CanOpen(gArchive) && n + 3 <= 20) {
		add[n].id = ID_CONV_OXZ;
		add[n].text = OxzTxt(OS_TO_OXZ);
		add[n].sep = 1;
		n++;
		add[n].id = ID_CONV_ZIP;
		add[n].text = OxzTxt(OS_TO_ZIP);
		add[n].sep = 0;
		n++;
		add[n].id = ID_CONV_TAR;
		add[n].text = OxzTxt(OS_TO_TAR);
		add[n].sep = 0;
		n++;
	}
	if (gInMenu) return;
	gInMenu = 1;
	UINT id = OxzOpenLookMenu(gHwnd, x, y, add, n);
	gInMenu = 0;
	if (id) SendMessageW(gHwnd, WM_COMMAND, id, 0);
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
	if (gWork.empty() && gForeign.empty() && !gVirtual) return;
	std::vector<std::wstring> all;
	std::vector<int> folders;
	int row = -1;
	while ((row = ListView_GetNextItem(gList, row, LVNI_SELECTED)) >= 0) {
		if (row >= (int)gItems.size() || gItems[row].up) continue;
		if (gWork.empty() || GetFileAttributesW((gWork + L"\\" + gItems[row].rel).c_str()) == INVALID_FILE_ATTRIBUTES)
			EnsureTree(gItems[row].rel, gItems[row].dir);
		if (gWork.empty()) continue;
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
	for (size_t i = 0; i < gCut.size(); ++i) {
		if (GetFileAttributesW(gCut[i].c_str()) != INVALID_FILE_ATTRIBUTES) continue;
		if (gWork.empty() || gCut[i].size() <= gWork.size()) continue;
		if (_wcsnicmp(gCut[i].c_str(), gWork.c_str(), gWork.size()) != 0 || gCut[i][gWork.size()] != L'\\') continue;
		OxzCoreDrop(gCut[i].c_str() + gWork.size() + 1);
	}
	gCut.clear();
	KillTimer(gHwnd, 2);
	Refresh();
	SaveCurrent();
}

static void BeginListDrag()
{
	if (gBusy || (gWork.empty() && gForeign.empty() && !gVirtual)) return;
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
		OxzSetLookRefresh(RefreshLook);
		MakeFont(hwnd);
		ApplyCaption(hwnd);
		ApplyRtl(hwnd);
		for (int i = 0; i < (int)(sizeof(kBtnId) / sizeof(kBtnId[0])); ++i) {
			HWND b = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)kBtnId[i], NULL, NULL);
			SendMessageW(b, WM_SETFONT, (WPARAM)gFont, TRUE);
			SubBtn(b);
		}
		gList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
			WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | LVS_REPORT | LVS_EDITLABELS | LVS_SHOWSELALWAYS,
			0, 0, 10, 10, hwnd, (HMENU)ID_LIST, NULL, NULL);
		ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
		SetWindowTheme(gList, L"", L"");
		SendMessageW(gList, WM_SETFONT, (WPARAM)gFont, TRUE);
		ListView_SetBkColor(gList, OxzLook().face);
		ListView_SetTextBkColor(gList, OxzLook().face);
		LVCOLUMNW col;
		memset(&col, 0, sizeof(col));
		const int widths[] = { 248, 128, 88, 128, 96, 72, 112, 72, 56, 136, 136 };
		col.mask = LVCF_TEXT | LVCF_WIDTH;
		col.pszText = (LPWSTR)L"";
		for (int c = 0; c < 11; ++c) {
			col.cx = widths[c];
			ListView_InsertColumn(gList, c, &col);
		}
		gStatus = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, hwnd, (HMENU)ID_STATUS, NULL, NULL);
		SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
		gProg = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PROG, NULL, NULL);
		gProgFile = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PROG_FILE, NULL, NULL);
		gLblAll = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LBL_ALL, NULL, NULL);
		gLblFile = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_LBL_FILE, NULL, NULL);
		gPctAll = CreateWindowExW(0, L"STATIC", L"0.0%", WS_CHILD | SS_RIGHT | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PCT_ALL, NULL, NULL);
		gPctFile = CreateWindowExW(0, L"STATIC", L"0.0%", WS_CHILD | SS_RIGHT | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PCT_FILE, NULL, NULL);
		SetWindowTheme(gProg, L"", L"");
		SetWindowTheme(gProgFile, L"", L"");
		SendMessageW(gProg, PBM_SETRANGE32, 0, 1000);
		SendMessageW(gProgFile, PBM_SETRANGE32, 0, 1000);
		SendMessageW(gProg, PBM_SETBARCOLOR, 0, (LPARAM)OxzLook().accent);
		SendMessageW(gProgFile, PBM_SETBARCOLOR, 0, (LPARAM)OxzLook().accent2);
		OxzSetBarInk(gProg, OxzLook().accent);
		OxzSetBarInk(gProgFile, OxzLook().accent2);
		SendMessageW(gProg, PBM_SETBKCOLOR, 0, (LPARAM)OxzLook().face);
		SendMessageW(gProgFile, PBM_SETBKCOLOR, 0, (LPARAM)OxzLook().face);
		SendMessageW(gLblAll, WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gLblFile, WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gPctAll, WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gPctFile, WM_SETFONT, (WPARAM)gFont, TRUE);
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
		for (int i = 0; i < (int)(sizeof(kBtnId) / sizeof(kBtnId[0])); ++i) SendMessageW(GetDlgItem(hwnd, kBtnId[i]), WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gList, WM_SETFONT, (WPARAM)gFont, TRUE);
		SendMessageW(gStatus, WM_SETFONT, (WPARAM)gFont, TRUE);
		Relayout();
		Refresh();
		return 0;
	}
	case WM_ERASEBKGND:
		return OxzHostErase(hwnd, (HDC)wp, gPink);
	case WM_PAINT:
		if (!OxzPerPixelAcrylic()) return DefWindowProcW(hwnd, msg, wp, lp);
		OxzHostPaint(hwnd);
		return 0;
	case WM_PARENTNOTIFY:
		if (LOWORD(wp) == WM_CREATE) OxzBindAcrylic(hwnd);
		return 0;
	case WM_COMMAND:
		if (OxzLookCommand(LOWORD(wp))) { RefreshLook(); return 0; }
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
		case ID_CONV_OXZ: { std::vector<std::wstring> one(1, gArchive); StartConvert(one, L"oxz"); break; }
		case ID_CONV_ZIP: { std::vector<std::wstring> one(1, gArchive); StartConvert(one, L"zip"); break; }
		case ID_CONV_TAR: { std::vector<std::wstring> one(1, gArchive); StartConvert(one, L"tar"); break; }
		case ID_TESTVIEW: OnTestView(); break;
		case ID_MAP: OnMap(); break;
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
					if (gItems[rows[k]].dir) EnsureTree(gItems[rows[k]].rel, 1);
					else if (!EnsureOnDisk(gItems[rows[k]].rel)) { bad = 1; continue; }
					if (gWork.empty()) { bad = 1; continue; }
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
	case WM_CTLCOLOREDIT:
		return (LRESULT)OxzHostCtlColor((HDC)wp, msg, gPink);
	case WM_NOTIFY: {
		NMHDR* nm = (NMHDR*)lp;
		if (nm->hwndFrom == gList && nm->code == NM_CUSTOMDRAW) {
			NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
			if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
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
			if (col < 0 || col > 10) return 0;
			if (gSortCol == col) gSortAsc = !gSortAsc;
			else { gSortCol = col; gSortAsc = 1; }
			SaveSort();
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
		if (nm->idFrom == ID_LIST && nm->code == NM_RCLICK) {
			POINT pt;
			GetCursorPos(&pt);
			QueueMenu(hwnd, hwnd, pt.x, pt.y, 1);
		}
		return 0;
	}
	case WM_CONTEXTMENU: {
		HWND from = (HWND)wp;
		int onList = (from == gList || (from && GetParent(from) == gList)) ? 1 : 0;
		QueueMenu(hwnd, from ? from : hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), onList);
		return TRUE;
	}
	case WM_APP + 30:
		gCtxQueued = 0;
		ReleaseCapture();
		if (gCtxList) {
			if (gBusy) ShowLookMenu(hwnd, gCtxX, gCtxY);
			else ShowListMenu(gCtxX, gCtxY);
		} else ShowLookMenu(gCtxFrom ? gCtxFrom : hwnd, gCtxX, gCtxY);
		return 0;
	case WM_TIMER:
		if (wp == 2) { WatchCut(); return 0; }
		if (wp == 3) { PollWatch(); return 0; }
		if (wp == 1 && gBusy) {
			OxzSnap snap;
			wchar_t elapsed[16], left[16], arc[700], filePct[32], allPct[32], count[80], clock[96], body[2100];
			int ten;
			memset(&snap, 0, sizeof(snap));
			OxzProgRead(&snap);
			ten = snap.overallTen;
			if (gProg) SetBarPos(gProg, ten);
			if (gProgFile) SetBarPos(gProgFile, snap.fileTen);
			OxzFormatTenth(snap.fileTen, filePct, 32);
			OxzFormatTenth(ten, allPct, 32);
			if (gPctFile) SetWindowTextW(gPctFile, filePct);
			if (gPctAll) SetWindowTextW(gPctAll, allPct);
			DWORD now = GetTickCount();
			DWORD ms = snap.startTick ? now - snap.startTick : 0;
			unsigned sec = ms / 1000;
			unsigned hour = sec / 3600;
			if (hour > 99) hour = 99;
			sec %= 3600;
			_snwprintf_s(elapsed, _TRUNCATE, L"%02u:%02u:%02u", hour, sec / 60, sec % 60);
			if (ten > 0 && ten < 1000 && snap.startTick) {
				DWORD rem = (DWORD)((unsigned long long)ms * (unsigned)(1000 - ten) / (unsigned)ten);
				unsigned rs = rem / 1000, rh = rs / 3600;
				if (rh > 99) rh = 99;
				rs %= 3600;
				_snwprintf_s(left, _TRUNCATE, L"%02u:%02u:%02u", rh, rs / 60, rs % 60);
			} else if (ten >= 1000) {
				wcscpy_s(left, L"00:00:00");
			} else {
				wcscpy_s(left, L"--:--:--");
			}
			const wchar_t* leaf = snap.archive;
			const wchar_t* slash = wcsrchr(snap.archive, L'\\');
			if (slash && slash[1]) leaf = slash + 1;
			_snwprintf_s(arc, _TRUNCATE, OxzTxt(OS_PROG_ARC), leaf[0] ? leaf : gBusyText.c_str());
			_snwprintf_s(count, _TRUNCATE, OxzTxt(OS_PROG_COUNT), snap.index, snap.total);
			_snwprintf_s(clock, _TRUNCATE, OxzTxt(OS_PROG_TIME), elapsed, left);
			_snwprintf_s(body, _TRUNCATE, L"%s\r\n%s\r\n%s    %s", arc, snap.file, count, clock);
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

static int ResumeBrowseOk(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	if (wcsstr(s, L"..")) return 0;
	if (wcschr(s, L':') || wcschr(s, L'/')) return 0;
	if (s[0] == L'\\') return 0;
	return 1;
}

static void TakeResume()
{
	wchar_t path[32768], br[2048];
	DWORD cb = sizeof(path);
	path[0] = 0;
	br[0] = 0;
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopen", RRF_RT_REG_SZ, NULL, path, &cb) != ERROR_SUCCESS)
		path[0] = 0;
	cb = sizeof(br);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopenBrowse", RRF_RT_REG_SZ, NULL, br, &cb) != ERROR_SUCCESS)
		br[0] = 0;
	RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopen");
	RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"reopenBrowse");
	int pathOk = path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
	if (gStartKind == 0 && pathOk) {
		gStartKind = 1;
		gStartA = path;
	}
	if (pathOk && ResumeBrowseOk(br) && _wcsicmp(gStartA.c_str(), path) == 0)
		gPendingBrowse = br;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int show)
{
	OxzEscapeJob();
	OxzLangInit();
	OxzLookLoad();
	LoadSort();
	{
		DWORD level = 10, ppmd = 1, cb = sizeof(DWORD);
		if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"level", RRF_RT_REG_DWORD, NULL, &level, &cb) != ERROR_SUCCESS || level < 1 || level > 10)
			level = 10;
		cb = sizeof(DWORD);
		if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"ppmd", RRF_RT_REG_DWORD, NULL, &ppmd, &cb) != ERROR_SUCCESS)
			ppmd = 1;
		OxzCoreSetLevel((int)level);
		OxzCoreSetPpmd(ppmd ? 1 : 0);
	}
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
			else if (!_wcsicmp(argv[i], L"/convto") && i + 2 < argc) { gStartKind = 17; gCloseWhenDone = 1; gStartB = argv[++i]; gStartA = argv[++i]; }
			else if (argv[i][0] != L'/' && gStartKind == 0) { gStartKind = 1; gStartA = argv[i]; }
		}
		LocalFree(argv);
	}
	TakeResume();
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);
	if (!gPink) gPink = CreateSolidBrush(OxzLook().bg0);
	if (!gMint) gMint = CreateSolidBrush(OxzLook().face);
	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hi;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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
