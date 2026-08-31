#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "kpi_decoder.h" // KPI_MEDIAINFO
#include "kpi_host_ipc.h"

// ============================================================================
// 32bit 本体から KpiHost64.exe を呼ぶクライアント
// ----------------------------------------------------------------------------
// EnsureConnected が exe を起こしパイプをつなぐ。1 本のパイプを CRITICAL_SECTION
// で直列化する（ホスト側 ServeOnce も 1 接続）。
// VST ライブのノート／PCM はここを通さず共有メモリ（kpi_host_ipc.h の SHM 名）。
// ============================================================================

// KPI Open 成功時。以降の Render/Seek/Close はこの sessionId。
struct KpiHost64Session
{
	uint32_t sessionId = 0;      // ホスト側 g_sessions のキー
	KPI_MEDIAINFO mediaInfo{};   // 実際に開いたフォーマット
	DWORD openedSongCount = 0;   // マルチソング形式の曲数
};

class KpiHost64Client
{
public:
	KpiHost64Client();
	~KpiHost64Client();

	bool EnsureConnected(); // 未接続なら KpiHost64.exe を起動してパイプ接続
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
		uint32_t slot = 0, uint32_t* outMidiFlags = nullptr);
	bool VstSeek(uint64_t posSample, uint32_t slot = 0);
	bool VstClose(uint32_t slot = 0);
	bool VstCloseAll();

	// VST ライブパート。ノートと音声は共有メモリ。パイプはライフサイクルだけ。
	bool VstLiveLoad(uint32_t part1to32, const std::wstring& pluginPath, bool isVst3);
	bool VstLiveUnload(uint32_t part1to32);
	bool VstLiveUnloadAll();
	bool VstLiveMidi(uint32_t port, uint32_t msg);
	bool VstLiveSysex(uint32_t port, const uint8_t* data, uint32_t bytes);
	bool VstLiveAudioStart();
	bool VstLiveAudioStop();
	bool VstLiveEditorOpen(uint32_t part1to32);
	bool VstLiveEditorClose(uint32_t part1to32);
	bool VstLiveEditorCloseAll();
	bool VstLiveSetSendChannel(uint32_t part1to32, int32_t sendCh);
	bool VstLivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
		uint32_t& outTotal, uint32_t& outCurrent, std::vector<std::wstring>& outNames);
	bool VstLiveSetProgram(uint32_t part1to32, uint32_t index);
	bool VstLiveGetState(uint32_t part1to32, uint32_t which,
		std::vector<uint8_t>& outBytes);
	bool VstLiveSetState(uint32_t part1to32, uint32_t which,
		const uint8_t* bytes, uint32_t len);

private:
	HANDLE m_hPipe = INVALID_HANDLE_VALUE; // ホストとの名前付きパイプ
	uint32_t m_reqId = 1;                  // 応答の突き合わせ用連番
	CRITICAL_SECTION m_cs{};               // パイプ送受信の直列化

	bool ConnectPipe(bool waitForHost = true);
	bool StartHostProcess();

	bool SendRequest(uint32_t cmd, const void* payload, uint32_t payloadBytes,
		std::vector<uint8_t>& outReplyPayload, uint32_t& outStatus,
		DWORD timeoutMs = 120000);
	bool SendSimple(uint32_t cmd, const void* payload, uint32_t payloadBytes,
		DWORD timeoutMs = 120000);
	bool SyncHostLang(); // PING で savedata.lang をホストへ

	int m_sentLang = -1;   // 最後に送った lang。同じなら PING しない
	int m_syncingLang = 0; // 再入防止（PING 中に EnsureConnected が来てもループしない）
};
