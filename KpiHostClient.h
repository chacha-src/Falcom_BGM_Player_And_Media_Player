#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "kpi_decoder.h" // KPI_MEDIAINFO
#include "kpi_host_ipc.h"

struct KpiHost64Session
{
	uint32_t sessionId = 0;
	KPI_MEDIAINFO mediaInfo{};
	DWORD openedSongCount = 0;
};

class KpiHost64Client
{
public:
	KpiHost64Client();
	~KpiHost64Client();

	bool EnsureConnected();
	void Disconnect();

	bool Ping();
	bool ListExts(const std::wstring& kpiPath, uint32_t& outKpiVer, std::wstring& outSupportExts);
	bool Open(const std::wstring& kpiPath, const std::wstring& mediaPath, const KPI_MEDIAINFO& request, uint32_t songNo, KpiHost64Session& outSession);
	bool RenderBytes(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& outPcm, bool& outEof);
	bool Seek(uint32_t sessionId, uint64_t posSample, uint32_t flag, uint64_t& outNewPosSample);
	bool Close(uint32_t sessionId);

private:
	HANDLE m_hPipe = INVALID_HANDLE_VALUE;
	uint32_t m_reqId = 1;

	bool ConnectPipe();
	bool StartHostProcess();

	bool SendRequest(uint32_t cmd, const void* payload, uint32_t payloadBytes, std::vector<uint8_t>& outReplyPayload, uint32_t& outStatus);
};

