#pragma once
#include <windows.h>

#define WM_OXZ_UPDATE (WM_APP + 8)
#define WM_OXZ_UPDATE_FAIL (WM_APP + 9)

struct OxzUpdHit {
	wchar_t url[1024];
	wchar_t leaf[32];
	long long modified;
	int oxz;
};

void OxzUpdateCheckAsync(HWND hwnd);
void OxzUpdateBegin(HWND hwnd, const OxzUpdHit* hit, const wchar_t* reopen, const wchar_t* browse);
