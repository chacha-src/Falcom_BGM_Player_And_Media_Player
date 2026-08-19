#pragma once

#include <windows.h>

struct playlistdata;

// 列挙: DLL から拡張子を埋め、成功したら kpicnt を進める。失敗時は何も増やさない。
int PluginWinamp_TryEnum(const wchar_t* dllPath, int is64);

// 再生 Open / Close / Seek / Read（playwav は oggDlg 側）
int PluginWinamp_Open(const wchar_t* dllPath, const wchar_t* mediaPath, HWND hwndMain);
int PluginWinamp_OpenRemote(const wchar_t* dllPath, const wchar_t* mediaPath);
void PluginWinamp_Close();
int PluginWinamp_SeekMs(int timeMs);
int PluginWinamp_Read(BYTE* dst, int bytesWanted); // 返却バイト数。0=EOF/未開放
int PluginWinamp_IsOpen();
int PluginWinamp_SampleRate();
int PluginWinamp_Channels();
int PluginWinamp_Bits();
int PluginWinamp_LengthMs();
int readwinamp(BYTE* bw, int cnt);