#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "..\kpi_host_ipc.h"

// ============================================================================
// KpiHost64 外部入力プラグイン（Winamp in_ / XMPlay / AIMP）
// ----------------------------------------------------------------------------
// 本体は 32bit なので 64bit の in_*.dll などを直接開けない。
// セッション ID は KPI の sessionId とは別空間（ForeignHost_* 専用マップ）。
// ============================================================================

uint32_t ForeignHost_ListExts(uint32_t kind, const std::wstring& path, std::wstring& outExts);
uint32_t ForeignHost_Open(uint32_t kind, const std::wstring& dll, const std::wstring& media, KPIHOST64_ForeignOpenReply& reply);
uint32_t ForeignHost_Render(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof);
uint32_t ForeignHost_Seek(uint32_t sessionId, uint64_t posSample);
uint32_t ForeignHost_Close(uint32_t sessionId);
