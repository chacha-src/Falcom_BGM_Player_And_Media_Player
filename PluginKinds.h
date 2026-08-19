#pragma once

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
	MODE_VST_MIDI = -30
};

// Buffwav 専用の負モード（dm < -10 一括は使わない）
inline bool IsBuffwavNegMode(int dm)
{
	return dm == -11 || dm == -12 || dm == -13 || dm == -14 || dm == -15;
}

inline bool IsForeignPluginMode(int dm)
{
	return dm == MODE_PLUGIN_WINAMP || dm == MODE_PLUGIN_XMPLAY || dm == MODE_PLUGIN_AIMP;
}
