#pragma once
#include <stdint.h>
#include <vector>

uint32_t VstHost64_Open(int slot, const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath);
uint32_t VstHost64_Render(int slot, uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof);
uint32_t VstHost64_Seek(int slot, uint64_t posSample);
uint32_t VstHost64_Close(int slot);
uint32_t VstHost64_CloseAll();
int VstHost64_SongActive();
int VstHost64_Rate(int slot);
int VstHost64_Channels(int slot);
int VstHost64_Bits(int slot);
uint64_t VstHost64_Length(int slot);
int VstHost64_Latency(int slot);
