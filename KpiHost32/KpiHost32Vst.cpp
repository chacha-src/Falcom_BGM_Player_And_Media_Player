// KpiHost32 曲ファイル用 VST MIDI — クロスフェードのためスロット 2 本
#include "kpihost_stdafx.h"
#include "../kpi_host_ipc.h"
#include "../VstMidiEngine.h"

#include <string>

static int g_vstOpen[2] = { 0, 0 }; // スロットが SMF+VSTi を開いていれば 1

static int ClampSlot(int slot)
{
	return (slot == 1) ? 1 : 0;
}

uint32_t VstHost32_Open(int slot, const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath)
{
	slot = ClampSlot(slot);
	if (!midPath || !midPath[0]) return KPIHOST32_STATUS_BAD_REQUEST;
	int rescan = 0;
	if (extraScanPath && extraScanPath[0]) {
		if (_wcsicmp(savedata.vstExtraPath, extraScanPath) != 0) {
			wcsncpy_s(savedata.vstExtraPath, _countof(savedata.vstExtraPath), extraScanPath, _TRUNCATE);
			rescan = 1;
		}
	} else {
		savedata.vstExtraPath[0] = 0;
	}
	// GS は XG だけ指定のとき空でよい。VstMidiOpen が SMF を覗いて選ぶ。
	if (vstDllPath && vstDllPath[0]) {
		wcsncpy_s(savedata.vstMultiDll, _countof(savedata.vstMultiDll), vstDllPath, _TRUNCATE);
		savedata.vstMultiName[0] = 0;
		const wchar_t* slash = wcsrchr(vstDllPath, L'\\');
		const wchar_t* leaf = slash ? slash + 1 : vstDllPath;
		wcsncpy_s(savedata.vstMultiName, _countof(savedata.vstMultiName), leaf, _TRUNCATE);
	} else {
		savedata.vstMultiDll[0] = 0;
		savedata.vstMultiName[0] = 0;
	}
	if (rescan) VstScanInvalidate();
	wchar_t hints[1][128] = {};
	VstMidiSetIoSlot(slot);
	if (VstMidiOpen(midPath, hints, 0, NULL) != 0)
		return KPIHOST32_STATUS_FAIL;
	if (!VstMidiHasPluginAudio()) {
		VstMidiClose();
		return KPIHOST32_STATUS_FAIL;
	}
	g_vstOpen[slot] = 1;
	return KPIHOST32_STATUS_OK;
}

// PCM を読む。短い読み・MIDI 未消化・この塊の SysEx/CC を eof ビットに載せる。
uint32_t VstHost32_Render(int slot, uint32_t bytesWanted, uint8_t* dest, uint32_t destCap, uint32_t& gotBytes, uint32_t& eof)
{
	slot = ClampSlot(slot);
	gotBytes = 0;
	eof = 0;
	if (!g_vstOpen[slot]) return KPIHOST32_STATUS_FAIL;
	if (bytesWanted == 0) return KPIHOST32_STATUS_OK;
	if (!dest) return KPIHOST32_STATUS_BAD_REQUEST;
	uint32_t want = bytesWanted;
	if (want > destCap) want = destCap;
	VstMidiSetIoSlot(slot);
	int got = VstMidiRead(dest, (int)want);
	if (got < 0) got = 0;
	gotBytes = (uint32_t)got;
	if (gotBytes < want)
		eof |= KPIHOST32_EOF_SHORT;
	if (VstMidiEventsPending())
		eof |= KPIHOST32_EOF_MIDI_PENDING;
	if (VstMidiTakeKeepAlive())
		eof |= KPIHOST32_EOF_MIDI_KEEPALIVE;
	return KPIHOST32_STATUS_OK;
}

int VstHost32_SongActive()
{
	return (g_vstOpen[0] || g_vstOpen[1]) ? 1 : 0; // アイドル終了を止める
}

uint32_t VstHost32_Seek(int slot, uint64_t posSample)
{
	slot = ClampSlot(slot);
	if (!g_vstOpen[slot]) return KPIHOST32_STATUS_FAIL;
	VstMidiSetIoSlot(slot);
	return (VstMidiSeekSamples((__int64)posSample) == 0) ? KPIHOST32_STATUS_OK : KPIHOST32_STATUS_FAIL;
}

uint32_t VstHost32_Close(int slot)
{
	slot = ClampSlot(slot);
	if (g_vstOpen[slot]) {
		VstMidiCloseSlot(slot); // 片方だけ。クロスフェード中のもう片方は残す
		g_vstOpen[slot] = 0;
	}
	return KPIHOST32_STATUS_OK;
}

uint32_t VstHost32_CloseAll()
{
	VstHost32_Close(0);
	VstHost32_Close(1);
	return KPIHOST32_STATUS_OK;
}

int VstHost32_Rate(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetRate();
}
int VstHost32_Channels(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetChannels();
}
int VstHost32_Bits(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetBits();
}
uint64_t VstHost32_Length(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return (uint64_t)VstMidiGetLengthSamples();
}
int VstHost32_Latency(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetLatencySamples();
}
