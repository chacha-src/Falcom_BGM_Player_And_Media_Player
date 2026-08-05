#pragma once
// Media Player sidecar persistence: play history + smart playlists.
// Kept out of savedata to avoid breaking old .dat layouts.

#include <windows.h>

enum {
	MP_HIST_MAX = 64,
	MP_SMART_MAX = 16
};

struct MpHistEntry {
	TCHAR path[1024];
	TCHAR name[200];
	FILETIME ft;     // local wall-clock of play start
	int plIdx;       // playlist index hint (-1 unknown)
};

// Smart rule flags (AND)
enum {
	MP_SMART_UNPLAYED   = 0x0001, // playCount == 0
	MP_SMART_MISSING    = 0x0002,
	MP_SMART_RATING_MIN = 0x0004,
	MP_SMART_ARTIST     = 0x0008,
	MP_SMART_HOUR_RANGE = 0x0010, // current local hour in [hourFrom, hourTo]
	MP_SMART_PLAY_MAX   = 0x0020, // playCount <= playCountMax
	MP_SMART_LAST_HOUR  = 0x0040  // lastPlay local hour in range (else current hour)
};

struct MpSmartRule {
	TCHAR name[64];
	int flags;
	int ratingMin;       // 1..5 when RATING_MIN
	TCHAR artist[128];   // substring, case-insensitive
	int hourFrom;        // 0..23
	int hourTo;          // 0..23 (inclusive; wrap allowed if from>to)
	int playCountMax;    // when PLAY_MAX
	int enabled;         // 1=active definition (still selectable when 0)
};

void MpHist_Init();
void MpHist_Load();
void MpHist_Save();
int  MpHist_Count();
bool MpHist_Get(int i, MpHistEntry& out);
void MpHist_Push(LPCTSTR path, LPCTSTR displayName, int plIdx);
void MpHist_SyncJumpList8(); // copy newest 8 into savedata.mpHist*

void MpSmart_Init();
void MpSmart_Load();
void MpSmart_Save();
int  MpSmart_Count();
bool MpSmart_Get(int i, MpSmartRule& out);
bool MpSmart_Set(int i, const MpSmartRule& r);
int  MpSmart_Add(const MpSmartRule& r); // -1 fail
bool MpSmart_Remove(int i);
void MpSmart_EnsureDefaults(); // seed Unplayed / Missing if empty
// 表示用ラベル（保存名 Unplayed/Missing や flags から UI 言語へ）
CString MpSmart_UiLabel(const MpSmartRule& r);
