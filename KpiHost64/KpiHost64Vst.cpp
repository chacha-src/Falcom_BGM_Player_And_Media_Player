// KpiHost64 VST MIDI session bridge
#include "kpihost_stdafx.h"
#include "../kpi_host_ipc.h"
#include "../VstMidiEngine.h"

#include <vector>
#include <string>

static int g_vstOpen = 0;

uint32_t VstHost64_Open(const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath)
{
	if (!midPath || !midPath[0]) return KPIHOST64_STATUS_BAD_REQUEST;
	int rescan = 0;
	if (extraScanPath && extraScanPath[0]) {
		if (_wcsicmp(savedata.vstExtraPath, extraScanPath) != 0) {
			wcsncpy_s(savedata.vstExtraPath, _countof(savedata.vstExtraPath), extraScanPath, _TRUNCATE);
			rescan = 1;
		}
	}
	if (!vstDllPath || !vstDllPath[0])
		return KPIHOST64_STATUS_FAIL;
	wcsncpy_s(savedata.vstMultiDll, _countof(savedata.vstMultiDll), vstDllPath, _TRUNCATE);
	savedata.vstMultiName[0] = 0;
	{
		const wchar_t* slash = wcsrchr(vstDllPath, L'\\');
		const wchar_t* leaf = slash ? slash + 1 : vstDllPath;
		wcsncpy_s(savedata.vstMultiName, _countof(savedata.vstMultiName), leaf, _TRUNCATE);
	}
	if (rescan) VstScanInvalidate();
	wchar_t hints[1][128] = {};
	if (VstMidiOpen(midPath, hints, 0, NULL) != 0)
		return KPIHOST64_STATUS_FAIL;
	if (!VstMidiHasPluginAudio()) {
		VstMidiClose();
		return KPIHOST64_STATUS_FAIL;
	}
	g_vstOpen = 1;
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_Render(uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof)
{
	out.clear();
	eof = 0;
	if (!g_vstOpen) return KPIHOST64_STATUS_FAIL;
	if (bytesWanted == 0) return KPIHOST64_STATUS_OK;
	out.resize(bytesWanted);
	int got = VstMidiRead(out.data(), (int)bytesWanted);
	if (got < 0) got = 0;
	if ((uint32_t)got < bytesWanted) {
		out.resize((size_t)got);
		eof = 1;
	}
	return KPIHOST64_STATUS_OK;
}

// The app may pause for longer than the idle timeout; tearing the session down
// underneath it would leave playback permanently silent on resume.
int VstHost64_SongActive() { return g_vstOpen ? 1 : 0; }

uint32_t VstHost64_Seek(uint64_t posSample)
{
	if (!g_vstOpen) return KPIHOST64_STATUS_FAIL;
	return (VstMidiSeekSamples((__int64)posSample) == 0) ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
}

uint32_t VstHost64_Close()
{
	if (g_vstOpen) {
		VstMidiClose();
		g_vstOpen = 0;
	}
	return KPIHOST64_STATUS_OK;
}

int VstHost64_Rate() { return VstMidiGetRate(); }
int VstHost64_Channels() { return VstMidiGetChannels(); }
int VstHost64_Bits() { return VstMidiGetBits(); }
uint64_t VstHost64_Length() { return (uint64_t)VstMidiGetLengthSamples(); }
