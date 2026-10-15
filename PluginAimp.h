#pragma once
#include <windows.h>

int PluginAimp_TryEnum(const wchar_t* dllPath, int is64);
int PluginAimp_Open(const wchar_t* dllPath, const wchar_t* mediaPath);
int PluginAimp_OpenRemote(const wchar_t* dllPath, const wchar_t* mediaPath);
int PluginAimp_SoftOpenSlot(int slot, const wchar_t* dllPath, const wchar_t* mediaPath,
	int* rate, int* ch, int* bits, INT64* sizeBytes);
void PluginAimp_BindSlot(int slot);
void PluginAimp_ClearSlot(int slot);
void PluginAimp_Close();
int PluginAimp_SeekBytes(INT64 pos);
int PluginAimp_Read(BYTE* dst, int bytesWanted);
int PluginAimp_IsOpen();
int PluginAimp_SampleRate();
int PluginAimp_Channels();
int PluginAimp_Bits();
INT64 PluginAimp_SizeBytes();
int readaimp(BYTE* bw, int cnt);
