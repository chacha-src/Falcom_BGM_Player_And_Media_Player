#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "..\kpi_host_ipc.h"

uint32_t ForeignHost_ListExts(uint32_t kind, const std::wstring& path, std::wstring& outExts);
uint32_t ForeignHost_Open(uint32_t kind, const std::wstring& dll, const std::wstring& media, KPIHOST64_ForeignOpenReply& reply);
uint32_t ForeignHost_Render(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof);
uint32_t ForeignHost_Seek(uint32_t sessionId, uint64_t posSample);
uint32_t ForeignHost_Close(uint32_t sessionId);
