#include "KpiV5ConfigStore.h"

#include <algorithm>
#include <cwctype>

static const wchar_t* KPI_V5_REG_BASE = L"Software\\Kobarin's Soft\\oggYSEDbgm\\KpiV5Config";

static std::wstring TrimWs(const std::wstring& in)
{
	size_t b = 0;
	while (b < in.size() && iswspace(in[b])) b++;
	size_t e = in.size();
	while (e > b && iswspace(in[e - 1])) e--;
	return in.substr(b, e - b);
}

std::wstring KpiV5NormalizePluginName(const std::wstring& pluginName)
{
	std::wstring out = TrimWs(pluginName);
	std::transform(out.begin(), out.end(), out.begin(), towlower);
	return out;
}

std::wstring KpiV5PluginNameFromPath(const std::wstring& kpiPath)
{
	size_t slash = kpiPath.find_last_of(L"\\/");
	std::wstring name = (slash == std::wstring::npos) ? kpiPath : kpiPath.substr(slash + 1);
	size_t dot = name.find_last_of(L'.');
	if (dot != std::wstring::npos) name = name.substr(0, dot);
	return KpiV5NormalizePluginName(name);
}

static std::wstring BuildKeyPath(const std::wstring& pluginName, const std::wstring& section)
{
	std::wstring p = KPI_V5_REG_BASE;
	p += L"\\";
	p += KpiV5NormalizePluginName(pluginName);
	p += L"\\";
	p += section.empty() ? L"General" : section;
	return p;
}

bool KpiV5SetStr(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
	if (pluginName.empty() || key.empty()) return false;
	HKEY hKey = NULL;
	DWORD disp = 0;
	LONG ret = RegCreateKeyExW(HKEY_CURRENT_USER, BuildKeyPath(pluginName, section).c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &disp);
	if (ret != ERROR_SUCCESS) return false;
	const DWORD cb = (DWORD)((value.size() + 1) * sizeof(wchar_t));
	ret = RegSetValueExW(hKey, key.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(), cb);
	RegCloseKey(hKey);
	return ret == ERROR_SUCCESS;
}

std::wstring KpiV5GetStr(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue)
{
	if (pluginName.empty() || key.empty()) return defaultValue;
	HKEY hKey = NULL;
	LONG ret = RegOpenKeyExW(HKEY_CURRENT_USER, BuildKeyPath(pluginName, section).c_str(), 0, KEY_READ, &hKey);
	if (ret != ERROR_SUCCESS) return defaultValue;
	DWORD type = 0;
	DWORD size = 0;
	ret = RegQueryValueExW(hKey, key.c_str(), NULL, &type, NULL, &size);
	if (ret != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
		RegCloseKey(hKey);
		return defaultValue;
	}
	std::vector<wchar_t> buf((size / sizeof(wchar_t)) + 1, 0);
	ret = RegQueryValueExW(hKey, key.c_str(), NULL, &type, (LPBYTE)buf.data(), &size);
	RegCloseKey(hKey);
	if (ret != ERROR_SUCCESS) return defaultValue;
	buf.back() = 0;
	return std::wstring(buf.data());
}

bool KpiV5SetBin(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, const BYTE* data, DWORD size)
{
	if (pluginName.empty() || key.empty()) return false;
	HKEY hKey = NULL;
	DWORD disp = 0;
	LONG ret = RegCreateKeyExW(HKEY_CURRENT_USER, BuildKeyPath(pluginName, section).c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &disp);
	if (ret != ERROR_SUCCESS) return false;
	ret = RegSetValueExW(hKey, key.c_str(), 0, REG_BINARY, data, size);
	RegCloseKey(hKey);
	return ret == ERROR_SUCCESS;
}

DWORD KpiV5GetBin(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, BYTE* data, DWORD size)
{
	if (pluginName.empty() || key.empty()) return 0;
	HKEY hKey = NULL;
	LONG ret = RegOpenKeyExW(HKEY_CURRENT_USER, BuildKeyPath(pluginName, section).c_str(), 0, KEY_READ, &hKey);
	if (ret != ERROR_SUCCESS) return 0;
	DWORD type = 0;
	DWORD need = 0;
	ret = RegQueryValueExW(hKey, key.c_str(), NULL, &type, NULL, &need);
	if (ret != ERROR_SUCCESS || type != REG_BINARY) {
		RegCloseKey(hKey);
		return 0;
	}
	if (data && size) {
		DWORD read = size;
		if (RegQueryValueExW(hKey, key.c_str(), NULL, &type, data, &read) != ERROR_SUCCESS) {
			RegCloseKey(hKey);
			return 0;
		}
	}
	RegCloseKey(hKey);
	return need;
}

bool KpiV5SetInt(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, INT64 value)
{
	wchar_t tmp[64] = {};
	_i64tow_s(value, tmp, _countof(tmp), 10);
	return KpiV5SetStr(pluginName, section, key, tmp);
}

INT64 KpiV5GetInt(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, INT64 defaultValue)
{
	wchar_t def[64] = {};
	_i64tow_s(defaultValue, def, _countof(def), 10);
	std::wstring s = KpiV5GetStr(pluginName, section, key, def);
	if (s.empty()) return defaultValue;
	wchar_t* endPtr = NULL;
	INT64 v = _wcstoi64(s.c_str(), &endPtr, 10);
	return (endPtr == s.c_str()) ? defaultValue : v;
}

bool KpiV5SetFloat(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, double value)
{
	wchar_t tmp[128] = {};
	swprintf_s(tmp, L"%.12g", value);
	return KpiV5SetStr(pluginName, section, key, tmp);
}

double KpiV5GetFloat(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, double defaultValue)
{
	wchar_t def[128] = {};
	swprintf_s(def, L"%.12g", defaultValue);
	std::wstring s = KpiV5GetStr(pluginName, section, key, def);
	if (s.empty()) return defaultValue;
	wchar_t* endPtr = NULL;
	double v = wcstod(s.c_str(), &endPtr);
	return (endPtr == s.c_str()) ? defaultValue : v;
}

const std::vector<KpiV5ConfigEntry>& GetKpiV5KnownEntries()
{
	static const std::vector<KpiV5ConfigEntry> entries = {
		{ L"kbpsf", L"General", L"EnableInterpreterCPU", L"0" },
		{ L"kbpsf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbpsf", L"General", L"SPUPlugin_x86", L"spuPeopsSound.dll" },
		{ L"kbpsf", L"General", L"SPUPlugin_x64", L"spuPeopsSound.dll" },
		{ L"kbpsf", L"General", L"spuEternalPath", L"" },
		{ L"kbpsf", L"General", L"UseSpuEternal", L"0" },
		{ L"kbgym", L"General", L"EnableGYM", L"1" },
		{ L"kbsnsf", L"General", L"SampleRate", L"48000" },
		{ L"kbsnsf", L"General", L"Interpolation", L"0" },
		{ L"kbsnsf", L"General", L"Resampler", L"1" },
		{ L"kbsnsf", L"General", L"DisableSurround", L"0" },
		{ L"kbsnsf", L"General", L"ReverseStereo", L"0" },
		{ L"kbsnsf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbssf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbpsf2", L"General", L"Interpolation", L"2" },
		{ L"kbpsf2", L"General", L"DisableEffects", L"0" },
		{ L"kbpsf2", L"General", L"UseDeAliasFilter", L"1" },
		{ L"kbpsf2", L"General", L"DefaultLength", L"1:55" },
		{ L"kbpsf2", L"General", L"DefaultFade", L"0:05" },
		// kbpsf2: volumeタグ!=1 のときだけ nBits=-64 になり double 経由になる。
		// minipsf2 は volume タグ付きが多く、ホスト側で扱いづらいため既定はタグ無視(16bit直)に寄せる。
		{ L"kbpsf2", L"General", L"IgnoreVolumeTag", L"1" },
		{ L"kb2sf", L"General", L"Interpolation", L"2" },
		{ L"kb2sf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbgsf", L"General", L"SampleRate", L"44100" },
		{ L"kbgsf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbusf", L"General", L"SampleRate", L"44100" },
		{ L"kbusf", L"General", L"EnableHLE", L"1" },
		{ L"kbusf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbncsf", L"General", L"SampleRate", L"44100" },
		{ L"kbncsf", L"General", L"Interpolation", L"4" },
		{ L"kbncsf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbdsf", L"General", L"IgnoreVolumeTag", L"0" },
		{ L"kbvgm", L"General", L"SampleRate", L"44100" },
		{ L"kbvgm", L"General", L"ResamplingMode", L"0" },
		{ L"kbvgm", L"General", L"ChipSampleMode", L"3" },
		{ L"kbvgm", L"General", L"ChipSampleRate", L"0" },
		{ L"kbvgm", L"General", L"PlaybackRate", L"0" },
		{ L"kbvgm", L"General", L"SurroundSound", L"0" },
		{ L"kbvgm", L"General", L"YRW801ROM_Path", L"" },
		{ L"kbvgm", L"General", L"SavePreset", L"0" },
		{ L"kbvgm", L"General", L"LoadPreset", L"0" },
		{ L"kbvgm", L"EmuCore", L"ChipInstance", L"0" },
		{ L"kbvgm", L"EmuCore", L"SN76496", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM2413", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM2612", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM2151", L"1" },
		{ L"kbvgm", L"EmuCore", L"SegaPCM", L"1" },
		{ L"kbvgm", L"EmuCore", L"RF5C68", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM2203", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM2608", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM2610", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM3812", L"1" },
		{ L"kbvgm", L"EmuCore", L"YM3526", L"1" },
		{ L"kbvgm", L"EmuCore", L"Y8950", L"1" },
		{ L"kbvgm", L"EmuCore", L"YMF262", L"1" },
		{ L"kbvgm", L"EmuCore", L"YMF278B", L"1" },
		{ L"kbvgm", L"EmuCore", L"YMF271", L"1" },
		{ L"kbvgm", L"EmuCore", L"YMZ280B", L"1" },
		{ L"kbvgm", L"EmuCore", L"PWM", L"1" },
		{ L"kbvgm", L"EmuCore", L"AY8910", L"1" },
		{ L"kbvgm", L"EmuCore", L"GameBoy", L"1" },
		{ L"kbvgm", L"EmuCore", L"NES APU", L"1" },
		{ L"kbvgm", L"EmuCore", L"YMW258", L"1" },
		{ L"kbvgm", L"EmuCore", L"uPD7759", L"1" },
		{ L"kbvgm", L"EmuCore", L"OKIM6258", L"1" },
		{ L"kbvgm", L"EmuCore", L"OKIM6295", L"1" },
		{ L"kbvgm", L"EmuCore", L"K051649", L"1" },
		{ L"kbvgm", L"EmuCore", L"K054539", L"1" },
		{ L"kbvgm", L"EmuCore", L"HuC6280", L"1" },
		{ L"kbvgm", L"EmuCore", L"C140", L"1" },
		{ L"kbvgm", L"EmuCore", L"C219", L"1" },
		{ L"kbvgm", L"EmuCore", L"K053260", L"1" },
		{ L"kbvgm", L"EmuCore", L"Pokey", L"1" },
		{ L"kbvgm", L"EmuCore", L"QSound", L"1" },
		{ L"kbvgm", L"EmuCore", L"SCSP", L"1" },
		{ L"kbvgm", L"EmuCore", L"WSwan", L"1" },
		{ L"kbvgm", L"EmuCore", L"VSU", L"1" },
		{ L"kbvgm", L"EmuCore", L"SAA1099", L"1" },
		{ L"kbvgm", L"EmuCore", L"ES5503", L"1" },
		{ L"kbvgm", L"EmuCore", L"ES5506", L"1" },
		{ L"kbvgm", L"EmuCore", L"X1-010", L"1" },
		{ L"kbvgm", L"EmuCore", L"C352", L"1" },
		{ L"kbvgm", L"EmuCore", L"GA20", L"1" },
		{ L"kbvgm", L"EmuCore", L"MIKEY", L"1" },
		{ L"kbgme", L"General", L"SampleRate", L"44100" },
		{ L"kbgme", L"General", L"EnableAccuracy", L"1" },
		{ L"kbgme", L"General", L"StereoDepth", L"0.0" },
		{ L"kbgme", L"General", L"Treble", L"0.0" },
		{ L"kbgme", L"General", L"Bass", L"90.0" },
		{ L"kbgme", L"SupportExt", L"HighPriority", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableAY", L"1" },
		{ L"kbgme", L"SupportExt", L"EnableGBS", L"1" },
		{ L"kbgme", L"SupportExt", L"EnableGYM", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableHES", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableKSS", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableNSF", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableSAP", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableSPC", L"0" },
		{ L"kbgme", L"SupportExt", L"EnableVGM", L"0" },
		{ L"kbsid", L"General", L"EnableReSID", L"1" },
		{ L"kbnsfplug", L"General", L"ShowTrack", L"0" },
		{ L"kbnsfplug", L"General", L"ShowMemory", L"0" },
		{ L"kbnsfplug", L"General", L"ShowInfo", L"0" },
		{ L"kbnsfplug", L"General", L"ConfigPosX", L"-32768" },
		{ L"kbnsfplug", L"General", L"ConfigPosY", L"-32768" },
		{ L"kbnsfplug", L"General", L"MixerPosX", L"-32768" },
		{ L"kbnsfplug", L"General", L"MixerPosY", L"-32768" },
		{ L"kbfmoplmidi", L"General", L"PatchFile", L"SB16_VXD_InsDrum.bin" },
		{ L"kbfmoplmidi", L"General", L"SampleRate", L"44100" },
		{ L"kbfmoplmidi", L"General", L"FadeTime", L"8000" },
		{ L"kbfmoplmidi", L"General", L"MaxLoops", L"2" },
		{ L"kbfmoplmidi", L"General", L"MaxLoopsCMF", L"1" },
		{ L"kbfmoplmidi", L"General", L"ChipSmplRate", L"-1" },
		{ L"kbfmoplmidi", L"General", L"VolumeCalc", L"0" },
		{ L"kbfmoplmidi", L"General", L"OPLMode", L"0" },
		{ L"kbfmoplmidi", L"General", L"OPLChips", L"2" },
		{ L"kbfmoplmidi", L"General", L"BadNoteOffs", L"0" },
		{ L"kbfmoplmidi", L"General", L"WinFM_Mode", L"0" },
		{ L"kbfmoplmidi", L"General", L"DefaultMode", L"0" },
		{ L"kbfmoplmidi", L"General", L"DrumChannel16", L"0" },
		{ L"kbfmoplmidi", L"General", L"EmuCore", L"0" },
		{ L"kbsasami", L"kbsasami", L"raira", L"1" },
		// midPlayPrefer=0(KPI/FM) → 保存1、raira で反転して実効0。VST優先時は保存0→実効1
		{ L"kbsasami", L"kbsasami", L"vst", L"1" },
	};
	return entries;
}

void KpiV5SyncKbsasamiOptions(int midPlayPrefer)
{
	KpiV5SetInt(L"kbsasami", L"kbsasami", L"raira", 1);
	// 本家: vst=0 が FM MIDI。raira=1 で入れ替えるので、希望の逆を書く
	KpiV5SetInt(L"kbsasami", L"kbsasami", L"vst", (midPlayPrefer == 1) ? 0 : 1);
}
