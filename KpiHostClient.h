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

	bool ForeignListExts(uint32_t pluginKind, const std::wstring& dllPath, std::wstring& outSupportExts);
	bool ForeignOpen(uint32_t pluginKind, const std::wstring& dllPath, const std::wstring& mediaPath, KPIHOST64_ForeignOpenReply& out);
	bool ForeignRender(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& outPcm, bool& outEof);
	bool ForeignSeek(uint32_t sessionId, uint64_t posSample);
	bool ForeignClose(uint32_t sessionId);

	bool VstOpen(const std::wstring& midPath, const std::wstring& vstDllPath,
		const std::wstring& extraScanPath, KPIHOST64_ForeignOpenReply& out, uint32_t slot = 0);
	bool VstRender(uint32_t bytesWanted, std::vector<uint8_t>& outPcm, bool& outEof,
		const uint8_t* injPorts = nullptr, const uint32_t* injMsgs = nullptr,
		const int32_t* injOfs = nullptr, uint32_t injCount = 0,
		uint32_t slot = 0);
	bool VstSeek(uint64_t posSample, uint32_t slot = 0);
	bool VstClose(uint32_t slot = 0);
	bool VstCloseAll();

	// VST Live parts. Notes and audio travel through shared memory; only the
	// lifecycle calls below go over the pipe.
	bool VstLiveLoad(uint32_t part1to32, const std::wstring& pluginPath, bool isVst3);
	bool VstLiveUnload(uint32_t part1to32);
	bool VstLiveUnloadAll();
	bool VstLiveMidi(uint32_t port, uint32_t msg);
	bool VstLiveSysex(uint32_t port, const uint8_t* data, uint32_t bytes);
	bool VstLiveAudioStart();
	bool VstLiveAudioStop();
	bool VstLiveEditorOpen(uint32_t part1to32);
	bool VstLiveEditorClose(uint32_t part1to32);
	bool VstLiveSetSendChannel(uint32_t part1to32, int32_t sendCh);
	bool VstLivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
		uint32_t& outTotal, uint32_t& outCurrent, std::vector<std::wstring>& outNames);
	bool VstLiveSetProgram(uint32_t part1to32, uint32_t index);

private:
	HANDLE m_hPipe = INVALID_HANDLE_VALUE;
	uint32_t m_reqId = 1;
	CRITICAL_SECTION m_cs{};

	bool ConnectPipe(bool waitForHost = true);
	bool StartHostProcess();

	bool SendRequest(uint32_t cmd, const void* payload, uint32_t payloadBytes, std::vector<uint8_t>& outReplyPayload, uint32_t& outStatus);
	bool SendSimple(uint32_t cmd, const void* payload, uint32_t payloadBytes);
	bool SyncHostLang();

	int m_sentLang = -1;
	int m_syncingLang = 0;
};

