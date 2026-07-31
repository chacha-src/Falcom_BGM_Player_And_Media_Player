#include "stdafx.h"
#include "DecodeProgress.h"
#include <algorithm>

namespace {
MpDecodeProgressCb g_cb = nullptr;
void* g_user = nullptr;
double g_expectSec = 0;
double g_doneSec = 0;
int g_lastPct = -1;
int g_basePct = 0;
int g_spanPct = 100;
int g_pcmCap = 95;
}

void MpDecodeProgressSetCb(MpDecodeProgressCb cb, void* user)
{
	g_cb = cb;
	g_user = user;
}

void MpDecodeProgressClearCb()
{
	g_cb = nullptr;
	g_user = nullptr;
}

void MpDecodeProgressReset()
{
	g_doneSec = 0;
	g_lastPct = -1;
	g_basePct = 0;
	g_spanPct = 100;
	g_pcmCap = 95;
	g_expectSec = 0;
}

void MpDecodeProgressSetExpectedSec(double sec)
{
	if (sec < 0) sec = 0;
	if (sec > 60 * 60) sec = 60 * 60;
	g_expectSec = sec;
	g_doneSec = 0;
	g_lastPct = -1;
}

void MpDecodeProgressSetSegment(int basePct, int spanPct)
{
	if (basePct < 0) basePct = 0;
	if (basePct > 100) basePct = 100;
	if (spanPct < 1) spanPct = 1;
	if (basePct + spanPct > 100) spanPct = 100 - basePct;
	g_basePct = basePct;
	g_spanPct = spanPct;
	g_doneSec = 0;
	g_lastPct = -1;
}

void MpDecodeProgressSetPcmCap(int maxPct)
{
	if (maxPct < 10) maxPct = 10;
	if (maxPct > 99) maxPct = 99;
	g_pcmCap = maxPct;
}

void MpDecodeProgressReport(int pct, LPCTSTR status)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	const int mapped = g_basePct + (int)((pct * g_spanPct) / 100.0 + 0.5);
	int out = mapped;
	if (out < 0) out = 0;
	if (out > 100) out = 100;
	if (out != 0 && out != 100 && out == g_lastPct) return;
	// ステータス無しの連続更新は2%刻み。ステータス付き(エンコード中など)は1%刻みを通す
	const BOOL hasStatus = (status != nullptr && status[0] != 0);
	if (!hasStatus && out != 0 && out != 100 && g_lastPct >= 0 && out < g_lastPct + 2)
		return;
	g_lastPct = out;
	if (g_cb)
		g_cb(out, status ? status : _T(""), g_user);
}

void MpDecodeProgressBumpAfterPcm(LPCTSTR status)
{
	int pct = g_pcmCap + 4;
	if (pct < 10) pct = 10;
	if (pct > 99) pct = 99;
	MpDecodeProgressReport(pct, status);
}

void MpDecodeProgressOnPcm(UINT nbytes, int rate, int ch, int bits)
{
	if (!g_cb || nbytes == 0) return;
	if (rate < 8000) rate = 44100;
	if (ch < 1) ch = 2;
	int bps = 2;
	if (bits == 24) bps = 3;
	else if (bits == 32) bps = 4;
	const int frameBytes = bps * ch;
	if (frameBytes <= 0) return;
	const double sec = ((double)nbytes / (double)frameBytes) / (double)rate;
	g_doneSec += sec;
	const int span = (std::max)(1, g_pcmCap - 5);
	int pct = 5;
	if (g_expectSec > 0.5)
		pct = 5 + (int)((std::min)(1.0, g_doneSec / g_expectSec) * span + 0.5);
	else
		pct = 5 + (int)((std::min)((double)span, g_doneSec * 2.0) + 0.5);
	if (pct > g_pcmCap) pct = g_pcmCap;
	MpDecodeProgressReport(pct, LL14(
		L"書き出し中…", L"Exporting...", L"Export...", L"Esportazione...", L"Exportando...",
		L"내보내는 중…", L"导出中…", L"Exporting...", L"Экспорт...", L"Exportiere...",
		L"Exportando...", L"Exporteren...", L"Eksport...", L"Aktariliyor..."));
}
