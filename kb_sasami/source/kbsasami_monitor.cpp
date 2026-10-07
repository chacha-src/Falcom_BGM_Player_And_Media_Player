/* raira=0 かつ fmmidimonitor=1: 再生位置を共有メモリへ書き、kbsasami_host にモニタを出させる。
   raira=1 は Configure(0)。fmmidimonitor は見ない。ホストへは開かない。 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "kbsasami_monitor.h"
#include "kbsasami_monshm.h"
#include "kbsasami_vst.h"

static int g_enable = 0;
static void* g_owner = NULL;
static HANDLE g_map = NULL;
static KbsMonShm* g_shm = NULL;
static int g_fm = 0;
static int g_rate = 44100;
static wchar_t g_path[520];
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_t0;
static int g_clock = 0;
static __int64 g_origin = 0;
static __int64 g_lag = 0;
static __int64 g_lastDecode = 0;
static __int64 g_qpc = 0;
static int g_holdPlay = 0;

static void ClockReset(__int64 sample)
{
	QueryPerformanceFrequency(&g_freq);
	QueryPerformanceCounter(&g_t0);
	g_origin = sample;
	g_lag = 0;
	g_lastDecode = sample;
	g_qpc = g_t0.QuadPart;
	g_clock = 1;
}

/* デコードが壁時計よりどれだけ先か。リングの実占有に合わせる。
   最大値のままだと、期限切れのあとバーストで満たしたとき針が戻る。 */
static void ClockNote(__int64 decode, int rate)
{
	if (rate < 8000) rate = 44100;
	if (!g_clock || decode + (__int64)rate / 5 < g_lastDecode)
		ClockReset(decode);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	g_qpc = now.QuadPart;
	const __int64 wall = g_origin
		+ (now.QuadPart - g_t0.QuadPart) * (__int64)rate / g_freq.QuadPart;
	__int64 lead = decode - wall;
	if (lead < 0) lead = 0;
	g_lag = lead;
	g_lastDecode = decode;
}

__int64 KbsMonHostLagSamples()
{
	if (!g_enable) return -1;
	return g_lag;
}

static void OpenShm()
{
	if (g_shm) return;
	g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
		0, sizeof(KbsMonShm), KBSMON_MAP_NAME);
	if (!g_map) return;
	g_shm = (KbsMonShm*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KbsMonShm));
	if (!g_shm) {
		CloseHandle(g_map);
		g_map = NULL;
		return;
	}
	memset(g_shm, 0, sizeof(*g_shm));
	g_shm->magic = KBSMON_MAGIC;
}

static void Publish(int show, __int64 sample)
{
	OpenShm();
	if (!g_shm) return;
	const long s = g_shm->seq;
	g_shm->seq = s + 1;
	MemoryBarrier();
	g_shm->magic = KBSMON_MAGIC;
	g_shm->show = show ? 1 : 0;
	g_shm->fm = g_fm;
	g_shm->sampleRate = g_rate;
	g_shm->playf = show ? 1 : 0;
	g_shm->playSample = sample;
	g_shm->lagSamples = g_lag;
	g_shm->qpc = g_qpc;
	g_shm->qpcFreq = g_freq.QuadPart;
	wcsncpy_s(g_shm->path, g_path, _TRUNCATE);
	MemoryBarrier();
	g_shm->seq = s + 2;
}

int KbsMonIsOn()
{
	return g_enable ? 1 : 0;
}

void KbsMonConfigure(int enable)
{
	const int on = enable ? 1 : 0;
	if (!on && g_enable)
		KbVstMonCmd(0);
	g_enable = on;
	if (!on) {
		g_owner = NULL;
		Publish(0, 0);
	}
}

void KbsMonBegin(void* owner, int fm, const wchar_t* path, const char*)
{
	if (!g_enable) return;
	const int keep = (g_path[0] && path && path[0] && _wcsicmp(g_path, path) == 0
		&& g_lastDecode > 0) ? 1 : 0;
	const __int64 keepAt = g_lastDecode;
	g_owner = owner;
	g_fm = fm ? 1 : 0;
	if (path && path[0])
		wcsncpy_s(g_path, path, _TRUNCATE);
	else if (!keep)
		g_path[0] = 0;
	if (keep)
		Publish(1, keepAt);
	else if (g_lastDecode <= 0) {
		ClockReset(0);
		Publish(1, 0);
	}
	g_holdPlay = 1;
	KbVstMonCmd(1);
}

void KbsMonForgetPos()
{
	g_lastDecode = 0;
	g_lag = 0;
	g_holdPlay = 0;
	g_clock = 0;
	g_path[0] = 0;
}

void KbsMonEnd(void* owner)
{
	if (g_owner && owner && g_owner != owner) return;
	g_owner = NULL;
	Publish(0, 0);
	if (g_enable)
		KbVstMonCmd(0);
}

void KbsMonHold(int on)
{
	g_holdPlay = on ? 1 : 0;
}

void KbsMonPlay(__int64 sample, int rate)
{
	if (!g_enable || !g_owner || g_holdPlay) return;
	if (rate >= 8000) g_rate = rate;
	ClockNote(sample, g_rate);
	Publish(1, sample);
}

void KbsMonSeek(__int64 sample, int rate)
{
	if (!g_enable || !g_owner) return;
	if (rate >= 8000) g_rate = rate;
	if (sample < 0) sample = 0;
	g_holdPlay = 0;
	ClockReset(sample);
	Publish(1, sample);
}

void KbsMonPublishHeard(__int64 heard, int rate)
{
	if (!g_enable || !g_owner) return;
	if (rate >= 8000) g_rate = rate;
	if (heard < 0) heard = 0;
	/* ホストへ渡した位置。lag はリング先行分。Hold 中でも出す（KbsMonPlay は Hold で捨てる）。 */
	ClockNote(heard, g_rate);
	Publish(1, heard);
}

void KbsMonMidi(int, unsigned int) {}
void KbsMonNotesOff() {}
