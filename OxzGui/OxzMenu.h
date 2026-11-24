#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum { OXMI_CMD = 0, OXMI_SEP = 1, OXMI_SUB = 2 };

struct OxzMi {
	int kind;
	UINT id;
	std::wstring text;
	int checked;
	std::vector<OxzMi> sub;
};

UINT OxzTrackPopup(HWND owner, int x, int y, const std::vector<OxzMi>& items);
