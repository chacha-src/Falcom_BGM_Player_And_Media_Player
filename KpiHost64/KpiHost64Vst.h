#pragma once
#include <stdint.h>
#include <vector>

uint32_t VstHost64_Open(const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath);
uint32_t VstHost64_Render(uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof);
uint32_t VstHost64_Seek(uint64_t posSample);
uint32_t VstHost64_Close();
int VstHost64_Rate();
int VstHost64_Channels();
int VstHost64_Bits();
uint64_t VstHost64_Length();
