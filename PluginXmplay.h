#pragma once
#include <windows.h>

int PluginXmplay_TryEnum(const wchar_t* dllPath, int is64);
int PluginXmplay_Open(const wchar_t* dllPath, const wchar_t* mediaPath);
void PluginXmplay_Close();
int PluginXmplay_SeekSec(double sec);
int PluginXmplay_Read(BYTE* dst, int bytesWanted); // float Process → int16 PCM
int PluginXmplay_IsOpen();
int PluginXmplay_SampleRate();
int PluginXmplay_Channels();
int PluginXmplay_Bits();
double PluginXmplay_LengthSec();
int readxmplay(BYTE* bw, int cnt);
