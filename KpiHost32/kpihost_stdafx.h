#pragma once
// ============================================================================
// KpiHost32 用の最小 stdafx
// ----------------------------------------------------------------------------
// 本体の VstMidiEngine.cpp は MFC の save 構造体を参照する。ホストには MFC が
// 無いので、再生に必要なフィールドだけここに置く。言語は本体が PING で送る。
// ============================================================================
#ifndef KPIHOST32_STDAFX_H
#define KPIHOST32_STDAFX_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <shlobj.h>
struct save {
	int lang; // 0=ja … 13=tr。本体の savedata.lang と同じ。PING で上書き
	int midPlayPrefer; // 本体と同名。ホスト側ではほぼ未使用
	wchar_t vstExtraPath[520]; // XG 明示 VSTi。空でもよい
	int vstHostMainLock;
	int vstHostWinX, vstHostWinY, vstHostWinW, vstHostWinH;
	wchar_t vstMultiDll[520];  // GS 明示 VSTi。両方空なら MIDI マッパー
	wchar_t vstMultiName[128];
	wchar_t midiOutName[32];
	wchar_t vstGsSoundFont[520];
	wchar_t vstXgSoundFont[520];
	int laMapperMt32; /* 0=SC bank127 1=実機MT-32。ホストの VST 経路では使わない */
#ifdef KBSASAMI_HOST_BUILD
	/* 本体の save と同じ名前。モニタ dlg が読む分だけ。末尾追加。 */
	DWORD samples;
	int aero;
	int q4;
	int popupMenuTheme;
	int popupMenuAnim;
	TCHAR popupMenuFace[32];
	int popupMenuPoint;
	int popupMenuBold;
	int popupMenuItalic;
	int popupMenuSoftBoost;
	int dsvol;
	int mpDetectedBpm;
	int mpLoopbackScore;
	int soft3dTourSeen;
	int soft3dPerfHintDismiss;
	int midimonviewmode;
	int midimontopmost;
	int midimonx, midimony, midimonw, midimonh;
	int fmmonx, fmmony, fmmonw, fmmonh;
	int midimonwindow;
	int fmmonwindow;
	int midimonMainLock;
	int midimon3dyaw, midimon3dpitch, midimon3dzoom;
	int eqMainLock, eqx, eqy;
	int pianorollMainLock, pianorollx, pianorolly, pianorollw, pianorollh;
	int analyzerMainLock, analyzerx, analyzery;
	int playlistMainLock;
	RECT p;
	int mpPromptMainLock, mpPromptX, mpPromptY, mpPromptW, mpPromptH, mpPromptHasPos;
	int prTuneMainLock, prTunex, prTuney;
	int mpCmdRollMainLock, mpCmdRollX, mpCmdRollY, mpCmdRollW, mpCmdRollH, mpCmdRollHasPos;
#endif
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
