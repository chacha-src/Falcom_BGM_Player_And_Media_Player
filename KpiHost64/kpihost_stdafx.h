#pragma once
// Minimal stand-in when compiling VST engine into KpiHost64 (no MFC).
#ifndef KPIHOST64_STDAFX_H
#define KPIHOST64_STDAFX_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <shlobj.h>
struct save {
	int midPlayPrefer;
	wchar_t vstExtraPath[520];
	int vstHostMainLock;
	int vstHostWinX, vstHostWinY, vstHostWinW, vstHostWinH;
	wchar_t vstMultiDll[520];
	wchar_t vstMultiName[128];
	wchar_t midiOutName[32];
};
extern save savedata;
#endif
