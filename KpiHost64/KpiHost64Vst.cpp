// KpiHost64 VST MIDI session bridge — two song slots for live crossfade
#include "kpihost_stdafx.h"
#include "../kpi_host_ipc.h"
#include "../VstMidiEngine.h"

#include <vector>
#include <string>

static int g_vstOpen[2] = { 0, 0 };

static int ClampSlot(int slot)
{
	return (slot == 1) ? 1 : 0;
}

uint32_t VstHost64_Open(int slot, const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath)
{
	slot = ClampSlot(slot);
	if (!midPath || !midPath[0]) return KPIHOST64_STATUS_BAD_REQUEST;
	int rescan = 0;
	if (extraScanPath && extraScanPath[0]) {
		if (_wcsicmp(savedata.vstExtraPath, extraScanPath) != 0) {
			wcsncpy_s(savedata.vstExtraPath, _countof(savedata.vstExtraPath), extraScanPath, _TRUNCATE);
			rescan = 1;
		}
	} else {
		savedata.vstExtraPath[0] = 0;
	}
	// GS may be empty when only XG is set; VstMidiOpen peeks the SMF and picks.
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
		return KPIHOST64_STATUS_FAIL;
	if (!VstMidiHasPluginAudio()) {
		VstMidiClose();
		return KPIHOST64_STATUS_FAIL;
	}
	g_vstOpen[slot] = 1;
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_Render(int slot, uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof)
{
	slot = ClampSlot(slot);
	out.clear();
	eof = 0;
	if (!g_vstOpen[slot]) return KPIHOST64_STATUS_FAIL;
	if (bytesWanted == 0) return KPIHOST64_STATUS_OK;
	VstMidiSetIoSlot(slot);
	out.resize(bytesWanted);
	int got = VstMidiRead(out.data(), (int)bytesWanted);
	if (got < 0) got = 0;
	if ((uint32_t)got < bytesWanted) {
		out.resize((size_t)got);
		eof = 1;
	}
	return KPIHOST64_STATUS_OK;
}

int VstHost64_SongActive()
{
	return (g_vstOpen[0] || g_vstOpen[1]) ? 1 : 0;
}

uint32_t VstHost64_Seek(int slot, uint64_t posSample)
{
	slot = ClampSlot(slot);
	if (!g_vstOpen[slot]) return KPIHOST64_STATUS_FAIL;
	VstMidiSetIoSlot(slot);
	return (VstMidiSeekSamples((__int64)posSample) == 0) ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
}

uint32_t VstHost64_Close(int slot)
{
	slot = ClampSlot(slot);
	if (g_vstOpen[slot]) {
		VstMidiCloseSlot(slot);
		g_vstOpen[slot] = 0;
	}
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_CloseAll()
{
	VstHost64_Close(0);
	VstHost64_Close(1);
	return KPIHOST64_STATUS_OK;
}

int VstHost64_Rate(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetRate();
}
int VstHost64_Channels(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetChannels();
}
int VstHost64_Bits(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetBits();
}
uint64_t VstHost64_Length(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return (uint64_t)VstMidiGetLengthSamples();
}
int VstHost64_Latency(int slot)
{
	VstMidiSetIoSlot(ClampSlot(slot));
	return VstMidiGetLatencySamples();
}
