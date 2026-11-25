/* 自己解凍の本体。書庫は実行ファイルのうしろに連結する。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../OxzCore/OxzCore.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

static uint32_t Get32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t Get64(const uint8_t* p)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
	return v;
}

struct Vol {
	uint32_t index, digits, flags, parts;
	uint64_t payload, payOff;
};

static std::wstring SelfPath()
{
	wchar_t buf[32768];
	GetModuleFileNameW(NULL, buf, 32768);
	return buf;
}

static int ReadVol(const std::wstring& path, Vol* v)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	uint8_t b[64];
	LARGE_INTEGER sz;
	DWORD got = 0;
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 80) { CloseHandle(h); return 0; }
	if (!ReadFile(h, b, 8, &got, NULL) || got != 8) { CloseHandle(h); return 0; }
	if (b[0] == 'M' && b[1] == 'Z') {
		uint8_t foot[16];
		LARGE_INTEGER li;
		li.QuadPart = sz.QuadPart - 16;
		if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !ReadFile(h, foot, 16, &got, NULL) || got != 16) { CloseHandle(h); return 0; }
		if (memcmp(foot, "OXZSFX1", 7) != 0) { CloseHandle(h); return 0; }
		li.QuadPart = (LONGLONG)Get64(foot + 8);
		if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !ReadFile(h, b, 64, &got, NULL) || got != 64) { CloseHandle(h); return 0; }
		v->payOff = Get64(foot + 8) + 64;
	} else {
		LARGE_INTEGER li;
		li.QuadPart = 0;
		if (memcmp(b, "OXZVOL1", 7) != 0) { CloseHandle(h); return 0; }
		if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !ReadFile(h, b, 64, &got, NULL) || got != 64) { CloseHandle(h); return 0; }
		v->payOff = 64;
	}
	CloseHandle(h);
	if (memcmp(b, "OXZVOL1", 7) != 0 || Get32(b + 8) != 1) return 0;
	v->index = Get32(b + 12);
	v->digits = Get32(b + 16);
	v->flags = Get32(b + 20);
	v->payload = Get64(b + 24);
	v->parts = Get32(b + 48);
	if (!v->parts || v->digits < 2 || v->index >= v->parts) return 0;
	return 1;
}

static std::wstring StemOf(const std::wstring& path)
{
	size_t dot = path.find_last_of(L'.');
	if (dot == std::wstring::npos) return path;
	return path.substr(0, dot);
}

static std::wstring PartPath(const std::wstring& stem, uint32_t index, uint32_t digits, int sfx)
{
	wchar_t ext[24];
	if (index == 0) return stem + (sfx ? L".exe" : L".oxz");
	_snwprintf_s(ext, _TRUNCATE, L".o%0*u", digits, index);
	return stem + ext;
}

static int Join(const std::wstring& opened, std::wstring& out)
{
	Vol first, base;
	std::wstring stem;
	wchar_t dir[MAX_PATH], file[MAX_PATH];
	HANDLE outH;
	uint32_t i;
	if (!ReadVol(opened, &first)) return 0;
	stem = StemOf(opened);
	if (!ReadVol(PartPath(stem, 0, first.digits, first.flags & 1), &base)) base = first;
	if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"oxs", 0, file)) return 0;
	out = file;
	outH = CreateFileW(out.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (outH == INVALID_HANDLE_VALUE) return 0;
	uint8_t* buf = (uint8_t*)malloc(1 << 20);
	if (!buf) { CloseHandle(outH); DeleteFileW(out.c_str()); return 0; }
	for (i = 0; i < base.parts; ++i) {
		Vol part;
		std::wstring name = (i == 0) ? opened : PartPath(stem, i, base.digits, base.flags & 1);
		if (i == 0 && (first.flags & 1)) name = opened;
		else name = PartPath(stem, i, base.digits, base.flags & 1);
		HANDLE in;
		LARGE_INTEGER li;
		uint64_t left;
		if (!ReadVol(name, &part)) { free(buf); CloseHandle(outH); DeleteFileW(out.c_str()); return 0; }
		in = CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (in == INVALID_HANDLE_VALUE) { free(buf); CloseHandle(outH); DeleteFileW(out.c_str()); return 0; }
		li.QuadPart = (LONGLONG)part.payOff;
		left = part.payload;
		if (!SetFilePointerEx(in, li, NULL, FILE_BEGIN)) { free(buf); CloseHandle(in); CloseHandle(outH); DeleteFileW(out.c_str()); return 0; }
		while (left) {
			DWORD want = left > (1u << 20) ? (DWORD)(1u << 20) : (DWORD)left;
			DWORD got = 0, done = 0;
			if (!ReadFile(in, buf, want, &got, NULL) || got != want) { free(buf); CloseHandle(in); CloseHandle(outH); DeleteFileW(out.c_str()); return 0; }
			while (done < got) {
				DWORD w = 0;
				if (!WriteFile(outH, buf + done, got - done, &w, NULL) || !w) { free(buf); CloseHandle(in); CloseHandle(outH); DeleteFileW(out.c_str()); return 0; }
				done += w;
			}
			left -= got;
		}
		CloseHandle(in);
	}
	free(buf);
	CloseHandle(outH);
	return 1;
}

static HWND gBar, gStatus;
static int gPct;

static void OnProg(const wchar_t*, unsigned, unsigned, int pct, void*)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	gPct = pct;
	if (gBar) SendMessageW(gBar, PBM_SETPOS, pct * 10, 0);
}

static int PickFolder(HWND owner, std::wstring& dest)
{
	BROWSEINFOW bi;
	wchar_t path[MAX_PATH];
	LPITEMIDLIST id;
	memset(&bi, 0, sizeof(bi));
	bi.hwndOwner = owner;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpszTitle = L"展開先";
	id = SHBrowseForFolderW(&bi);
	if (!id) return 0;
	path[0] = 0;
	if (!SHGetPathFromIDListW(id, path)) { CoTaskMemFree(id); return 0; }
	CoTaskMemFree(id);
	dest = path;
	return dest.empty() ? 0 : 1;
}

static std::wstring gArc, gDest;

static LRESULT CALLBACK SfxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE: {
		CreateWindowExW(0, L"STATIC", L"展開先", WS_CHILD | WS_VISIBLE, 16, 16, 80, 22, hwnd, NULL, NULL, NULL);
		CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", gDest.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 100, 14, 340, 24, hwnd, (HMENU)101, NULL, NULL);
		CreateWindowExW(0, L"BUTTON", L"参照", WS_CHILD | WS_VISIBLE, 450, 12, 80, 28, hwnd, (HMENU)102, NULL, NULL);
		gBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, 16, 56, 514, 22, hwnd, (HMENU)103, NULL, NULL);
		SendMessageW(gBar, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
		gStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 88, 400, 22, hwnd, (HMENU)104, NULL, NULL);
		CreateWindowExW(0, L"BUTTON", L"展開", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 430, 84, 100, 30, hwnd, (HMENU)105, NULL, NULL);
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == 102) {
			std::wstring d;
			if (PickFolder(hwnd, d)) SetWindowTextW(GetDlgItem(hwnd, 101), d.c_str());
			return 0;
		}
		if (LOWORD(wp) == 105) {
			wchar_t dest[1024];
			std::wstring joined, errText;
			GetWindowTextW(GetDlgItem(hwnd, 101), dest, 1024);
			if (!dest[0]) return 0;
			CreateDirectoryW(dest, NULL);
			SetWindowTextW(gStatus, L"展開しています。");
			if (!Join(gArc, joined)) { SetWindowTextW(gStatus, L"書庫を読めません。"); return 0; }
			if (OxzCoreExtract(joined.c_str(), dest, NULL, NULL, OnProg, NULL) != OXZ_CORE_OK) {
				DeleteFileW(joined.c_str());
				SetWindowTextW(gStatus, L"展開に失敗しました。");
				return 0;
			}
			DeleteFileW(joined.c_str());
			SendMessageW(gBar, PBM_SETPOS, 1000, 0);
			SetWindowTextW(gStatus, L"展開しました。");
			return 0;
		}
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int)
{
	WNDCLASSW wc;
	HWND hwnd;
	MSG msg;
	std::wstring self = SelfPath();
	size_t slash;
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
	Vol v;
	gArc = self;
	if (!ReadVol(self, &v)) {
		MessageBoxW(NULL, L"自己解凍の書庫が見つかりません。", L"oxz", MB_ICONWARNING);
		return 1;
	}
	gDest = self;
	slash = gDest.find_last_of(L'\\');
	if (slash != std::wstring::npos) gDest.resize(slash);
	InitCommonControlsEx(&icc);
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = SfxProc;
	wc.hInstance = hi;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = L"OxzSfx";
	RegisterClassW(&wc);
	hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OxzSfx", L"oxz 自己解凍", WS_CAPTION | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, 560, 170, NULL, NULL, hi, NULL);
	if (!hwnd) return 1;
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	while (GetMessageW(&msg, NULL, 0, 0)) {
		if (!IsDialogMessageW(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	return 0;
}
