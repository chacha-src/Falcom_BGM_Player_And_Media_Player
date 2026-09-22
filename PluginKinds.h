#pragma once

#include "CEmu/cemu_types.h"

// プラグイン台帳の種別（kpif[]/ext[][] と並列の plugkind[]）
enum {
	PLUGKIND_KPI = 0,
	PLUGKIND_WINAMP = 1,
	PLUGKIND_XMPLAY = 2,
	PLUGKIND_AIMP = 3
};

// playlistdata.sub / mode
enum {
	MODE_PLUGIN_WINAMP = -20,
	MODE_PLUGIN_XMPLAY = -21,
	MODE_PLUGIN_AIMP = -22,
	MODE_ZMUSIC = -50,
	MODE_VST_MIDI = -30,
	/* lzh/zip 内シーケンス。-31/-32 は空の軌跡 The 2nd / The 3rd の予約。
	   CEmu の zip::0001（-1000 帯）ともぶつからない。 */
	MODE_MIDI_PACK = -40
	/* MODE_CEMU / MODE_CEMU_BASE: CEmu/cemu_types.h */
};

/* 旧 MODE_MIDI_PACK。プレイリスト等に残っている値は RemapLegacyPlaySub で付け替える。 */
static const int MODE_MIDI_PACK_LEGACY = -31;

/* 旧 -31 の MIDI パックを MODE_MIDI_PACK へ。
   空の軌跡 pac::曲 が誤って -31 になっていた行は 30/31 に戻す。 */
inline int RemapLegacyPlaySub(int sub, const wchar_t* fol)
{
	if (sub != MODE_MIDI_PACK_LEGACY && sub != MODE_MIDI_PACK)
		return sub;
	if (fol && fol[0]) {
		wchar_t buf[2048];
		wcsncpy_s(buf, fol, _TRUNCATE);
		_wcslwr_s(buf);
		if (wcsstr(buf, L".pac::")) {
			if (wcsstr(buf, L"trails in the sky 2nd chapter"))
				return 31;
			if (wcsstr(buf, L"trails in the sky 1st chapter"))
				return 30;
		}
	}
	if (sub == MODE_MIDI_PACK_LEGACY)
		return MODE_MIDI_PACK;
	return sub;
}

// Buffwav 専用の負モード（dm < -10 一括は使わない）
inline bool IsBuffwavNegMode(int dm)
{
	return dm == -11 || dm == -12 || dm == -13 || dm == -14 || dm == -15;
}

inline bool IsForeignPluginMode(int dm)
{
	return dm == MODE_PLUGIN_WINAMP || dm == MODE_PLUGIN_XMPLAY || dm == MODE_PLUGIN_AIMP;
}

/* VST ホストで鳴らす（単体 MID とアーカイブ展開後） */
inline bool IsVstMidiPlayMode(int dm)
{
	return dm == MODE_VST_MIDI || dm == MODE_MIDI_PACK;
}

inline bool IsCemuMode(int dm)
{
	return CEmuIsCemuMode(dm);
}
