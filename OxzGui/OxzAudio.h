#pragma once
#include <windows.h>
#include <string>
#include <vector>

void OxzShowTagEdit(HWND owner, const std::vector<std::wstring>& paths);
void OxzShowConvert(HWND owner, const std::vector<std::wstring>& paths);
