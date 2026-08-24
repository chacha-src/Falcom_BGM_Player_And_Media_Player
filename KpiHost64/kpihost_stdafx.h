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
	int lang; // 0=ja … 13=tr — same codes as the player; parent sends via PING
	int midPlayPrefer;
	wchar_t vstExtraPath[520]; // XG explicit VSTi (empty ok)
	int vstHostMainLock;
	int vstHostWinX, vstHostWinY, vstHostWinW, vstHostWinH;
	wchar_t vstMultiDll[520];  // GS explicit VSTi (empty=XG if set, else Mapper)
	wchar_t vstMultiName[128];
	wchar_t midiOutName[32];
};
extern save savedata;

/* lang: 0=ja 1=en 2=fr 3=it 4=es 5=ko 6=zh 7=ar 8=ru 9=de 10=pt 11=nl 12=pl 13=tr */
const wchar_t* LangPick14(
	const wchar_t* s0, const wchar_t* s1, const wchar_t* s2, const wchar_t* s3,
	const wchar_t* s4, const wchar_t* s5, const wchar_t* s6, const wchar_t* s7,
	const wchar_t* s8, const wchar_t* s9, const wchar_t* s10, const wchar_t* s11,
	const wchar_t* s12, const wchar_t* s13);
#define LL14(ja,en,fr,it,es,ko,zh,ar,ru,de,pt,nl,pl,tr) \
	LangPick14(ja,en,fr,it,es,ko,zh,ar,ru,de,pt,nl,pl,tr)
#endif
