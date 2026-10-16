/* KPI (x86/x64) -> kbsasami_host32 -> c86ctl.dll.
   host and c86ctl are searched from this module's folder through the exe folder, including one child level. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "kbs_gimic.h"

#define KBS_GIMIC_MAGIC 0x43494D47u
#define KBS_GIMIC_Q 65536
#define KBS_GIMIC_MAP L"Local\\kbsasami_gimic"
#define KBS_GIMIC_MX  L"Local\\kbsasami_gimic_host32"
#define KBS_GIMIC_PROD L"Local\\kbsasami_gimic_prod"
#define KBS_GIMIC_LAUNCH L"Local\\kbsasami_gimic_launch"

#pragma pack(push, 8)
struct KbsGimicOp {
	int64_t sample;
	uint32_t addr;
	uint8_t data;
	uint8_t pad0;
	uint16_t pad1;
};

struct KbsGimicShm {
	uint32_t magic;
	volatile long initLock;
	volatile long seq;
	int32_t enable;
	int32_t active;
	int32_t devOk;
	int32_t chip;
	int32_t burst;
	int32_t force;
	int32_t resetReq;
	int32_t allOffReq; /* 停止・終了。残っているノートを切ってからキューを捨てる */
	int32_t wrote;
	uint32_t clockHz;
	uint32_t sampleRate;
	uint32_t moduleType;
	volatile long w;
	volatile long r;
	volatile LONG64 gen;
	int64_t playSample;
	int64_t lagSamples;
	int64_t qpc;
	int64_t qpcFreq;
	uint32_t prodTick;
	uint32_t hostTick;
	KbsGimicOp q[KBS_GIMIC_Q];
};
#pragma pack(pop)

static HANDLE g_map = NULL;
static KbsGimicShm* g_shm = NULL;
static HANDLE g_prodMx = NULL;
static int g_enable = 0;
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_t0;
static int g_clock = 0;
static int64_t g_origin = 0;
static int64_t g_lastDec = 0;
static int64_t g_extraLag = 0;
static int g_queueWaitMs = 40;

static int ExistsFile(const wchar_t* p)
{
	if (!p || !p[0]) return 0;
	DWORD a = GetFileAttributesW(p);
	return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0) ? 1 : 0;
}

static int JoinName(wchar_t* out, int n, const wchar_t* dir, const wchar_t* name)
{
	if (!dir || !dir[0] || !name || !name[0]) return 0;
	size_t L = wcslen(dir);
	int slash = (L && (dir[L - 1] == L'\\' || dir[L - 1] == L'/')) ? 1 : 0;
	if (slash)
		_snwprintf_s(out, n, _TRUNCATE, L"%s%s", dir, name);
	else
		_snwprintf_s(out, n, _TRUNCATE, L"%s\\%s", dir, name);
	return ExistsFile(out);
}

static int ChildHas(const wchar_t* dir, const wchar_t* name, wchar_t* out, int n)
{
	wchar_t pat[MAX_PATH];
	_snwprintf_s(pat, _TRUNCATE, L"%s*", dir);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	int ok = 0;
	do {
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
		if (fd.cFileName[0] == L'.') continue;
		wchar_t sub[MAX_PATH];
		_snwprintf_s(sub, _TRUNCATE, L"%s%s\\", dir, fd.cFileName);
		if (JoinName(out, n, sub, name)) { ok = 1; break; }
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return ok;
}

static void ParentOf(wchar_t* dir)
{
	size_t L = wcslen(dir);
	if (L && (dir[L - 1] == L'\\' || dir[L - 1] == L'/')) dir[--L] = 0;
	wchar_t* s = wcsrchr(dir, L'\\');
	if (!s) { dir[0] = 0; return; }
	s[1] = 0;
}

/* プレイヤー exe の直下には無い。KPI と同じ Plugins\kbsasami を先に見る。 */
static int FindBesidePlayer(const wchar_t* name, wchar_t* out, int outN)
{
	wchar_t exe[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 0;
	wchar_t* sl = wcsrchr(exe, L'\\');
	if (!sl) return 0;
	sl[1] = 0;
	const wchar_t* rel[2] = {
		L"Plugins\\kbsasami\\",
		L"Plugins\\kbsasami\\x64\\"
	};
	for (int i = 0; i < 2; i++) {
		wchar_t dir[MAX_PATH];
		_snwprintf_s(dir, _TRUNCATE, L"%s%s", exe, rel[i]);
		if (JoinName(out, outN, dir, name)) return 1;
	}
	return 0;
}

/* From this module's folder up to the exe folder. Each directory, then one level of children. */
static int FindBetween(const wchar_t* name, wchar_t* out, int outN)
{
	if (FindBesidePlayer(name, out, outN)) return 1;
	wchar_t mod[MAX_PATH];
	HMODULE hm = NULL;
	mod[0] = 0;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)(void*)&FindBetween, &hm) && hm)
		GetModuleFileNameW(hm, mod, MAX_PATH);
	{
		wchar_t* sl = wcsrchr(mod, L'\\');
		if (sl) sl[1] = 0;
		else mod[0] = 0;
	}
	wchar_t exe[MAX_PATH];
	exe[0] = 0;
	if (GetModuleFileNameW(NULL, exe, MAX_PATH)) {
		wchar_t* e = wcsrchr(exe, L'\\');
		if (e) e[1] = 0;
		else exe[0] = 0;
	}
	wchar_t cur[MAX_PATH];
	wcsncpy_s(cur, mod[0] ? mod : exe, _TRUNCATE);
	wchar_t stop[MAX_PATH];
	stop[0] = 0;
	if (exe[0] && mod[0] && _wcsnicmp(mod, exe, wcslen(exe)) == 0)
		wcsncpy_s(stop, exe, _TRUNCATE);
	/* host32 は自分自身がプロセス exe。c86ctl はプレイヤー exe 側にある。 */
	const int self = (mod[0] && exe[0] && _wcsicmp(mod, exe) == 0) ? 1 : 0;
	for (int depth = 0; depth < 8 && cur[0]; ++depth) {
		if (JoinName(out, outN, cur, name)) return 1;
		if (ChildHas(cur, name, out, outN)) return 1;
		int atStop = (stop[0] && _wcsicmp(cur, stop) == 0) ? 1 : 0;
		ParentOf(cur);
		if (atStop && !self) break;
	}
	if (self) {
		wcsncpy_s(cur, exe, _TRUNCATE);
		ParentOf(cur);
		for (int depth = 0; depth < 8 && cur[0]; ++depth) {
			if (JoinName(out, outN, cur, name)) return 1;
			if (ChildHas(cur, name, out, outN)) return 1;
			ParentOf(cur);
		}
	} else if (exe[0] && (!mod[0] || !stop[0])) {
		wcsncpy_s(cur, exe, _TRUNCATE);
		for (int depth = 0; depth < 8 && cur[0]; ++depth) {
			if (JoinName(out, outN, cur, name)) return 1;
			if (ChildHas(cur, name, out, outN)) return 1;
			ParentOf(cur);
		}
	}
	out[0] = 0;
	return 0;
}

static void MapShm(void)
{
	if (g_shm) return;
	g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
		0, sizeof(KbsGimicShm), KBS_GIMIC_MAP);
	if (!g_map) return;
	g_shm = (KbsGimicShm*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KbsGimicShm));
	if (!g_shm) {
		CloseHandle(g_map);
		g_map = NULL;
		return;
	}
	if (g_shm->magic != KBS_GIMIC_MAGIC) {
		if (InterlockedCompareExchange(&g_shm->initLock, 1, 0) == 0) {
			LONG lock = g_shm->initLock;
			memset(g_shm, 0, sizeof(*g_shm));
			g_shm->initLock = lock;
			g_shm->magic = KBS_GIMIC_MAGIC;
		} else {
			for (int i = 0; i < 50 && g_shm->magic != KBS_GIMIC_MAGIC; ++i)
				Sleep(1);
		}
	}
}

static void PublishClock(int64_t playSample, uint32_t rate)
{
	if (!g_shm) return;
	if (rate < 8000) rate = 44100;
	if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
	if (!g_clock || playSample + (int64_t)rate / 5 < g_lastDec) {
		QueryPerformanceCounter(&g_t0);
		g_origin = playSample;
		g_lastDec = playSample;
		g_clock = 1;
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	int64_t wall = g_origin;
	if (g_freq.QuadPart)
		wall += (now.QuadPart - g_t0.QuadPart) * (int64_t)rate / g_freq.QuadPart;
	int64_t lead = playSample - wall;
	if (lead < 0) lead = 0;
	/* シークで playSample が大きく跳ねたときだけ原点を付け直す。
	   出力バッファ分（数十 ms）で付け直すと、溜まったレジスタが一気に出てテンポが揺れる。 */
	if (lead > g_extraLag + (int64_t)rate) {
		QueryPerformanceCounter(&g_t0);
		g_origin = playSample;
		wall = playSample;
		lead = 0;
	}
	lead += g_extraLag;
	g_lastDec = playSample;
	long s = g_shm->seq;
	g_shm->seq = s + 1;
	MemoryBarrier();
	g_shm->playSample = playSample;
	g_shm->lagSamples = lead;
	g_shm->qpc = now.QuadPart;
	g_shm->qpcFreq = g_freq.QuadPart;
	g_shm->sampleRate = rate;
	g_shm->prodTick = GetTickCount();
	MemoryBarrier();
	g_shm->seq = s + 2;
}

static int Host32Alive(void)
{
	HANDLE mx = OpenMutexW(SYNCHRONIZE, FALSE, KBS_GIMIC_MX);
	if (!mx) return 0;
	CloseHandle(mx);
	return 1;
}

/* raira=1 does not open the monitor pipe. This still starts host32 for c86ctl.
   The launch mutex stops a second process from clearing devOk. */
static void StartHost32(void)
{
	if (Host32Alive()) return;
	HANDLE lock = CreateMutexW(NULL, FALSE, KBS_GIMIC_LAUNCH);
	if (!lock) return;
	if (WaitForSingleObject(lock, 0) != WAIT_OBJECT_0) {
		CloseHandle(lock);
		return;
	}
	if (!Host32Alive()) {
		wchar_t exe[MAX_PATH];
		if (FindBetween(L"kbsasami_host32.exe", exe, MAX_PATH)) {
			wchar_t dir[MAX_PATH];
			wcsncpy_s(dir, exe, _TRUNCATE);
			wchar_t* sl = wcsrchr(dir, L'\\');
			if (sl) sl[1] = 0;
			wchar_t cmd[MAX_PATH + 8];
			_snwprintf_s(cmd, _TRUNCATE, L"\"%s\"", exe);
			STARTUPINFOW si;
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi;
			memset(&pi, 0, sizeof(pi));
			if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
				dir[0] ? dir : NULL, &si, &pi)) {
				CloseHandle(pi.hThread);
				CloseHandle(pi.hProcess);
				for (int i = 0; i < 20 && !Host32Alive(); ++i)
					Sleep(10);
			}
		}
	}
	ReleaseMutex(lock);
	CloseHandle(lock);
}

static void PushOp(uint32_t addr, uint8_t data, int64_t sample, int pace)
{
	if (!g_shm || !g_shm->enable || !g_shm->active) return;
	/* 溢れたら force で時刻を無視して吐き出すと、先読み分がチップ上で早回しになる。
	   ホストが実時間で1件消費するまで待つ。ホストが居なければ捨てる。
	   ADPCM 転送中は待ちを長くし、後ろの FM/SSG 音量を落とさない。 */
	int limit = g_queueWaitMs;
	if (pace && limit < 100) limit = 100;
	if (limit < 1) limit = 1;
	if (limit > 20000) limit = 20000;
	/* ホストが一件も取らないまま待ち続けると、一件ごとの待ちが積もって演奏と終了が固まる。
	   止まっている間は待たずに捨てる。 */
	static long s_stallR = -1;
	if (s_stallR >= 0) {
		if (g_shm->r != s_stallR) s_stallR = -1;
		else if ((unsigned long)(g_shm->w - g_shm->r) >= (unsigned long)(KBS_GIMIC_Q - 1)) return;
	}
	const long r0 = g_shm->r;
	for (int waited = 0; waited < limit; ++waited) {
		if (waited >= 250 && g_shm->r == r0) {
			s_stallR = r0;
			return;
		}
		for (int spin = 0; spin < 8; ++spin) {
			long w = g_shm->w;
			long r = g_shm->r;
			if ((unsigned long)(w - r) < (unsigned long)(KBS_GIMIC_Q - 1)) {
				KbsGimicOp* op = &g_shm->q[(unsigned long)w % (unsigned)KBS_GIMIC_Q];
				op->sample = sample;
				op->addr = addr;
				op->data = data;
				op->pad0 = pace ? 1 : 0;
				op->pad1 = 0;
				MemoryBarrier();
				if (InterlockedCompareExchange(&g_shm->w, w + 1, w) == w) {
					g_shm->prodTick = GetTickCount();
					g_shm->wrote = 1;
					return;
				}
				continue;
			}
			break;
		}
		if (!Host32Alive()) return;
		Sleep(1);
	}
}

static void HoldProd(void)
{
	if (!g_prodMx)
		g_prodMx = CreateMutexW(NULL, TRUE, KBS_GIMIC_PROD);
}

void KbsGimicSetEnable(int on)
{
	g_enable = on ? 1 : 0;
	MapShm();
	if (!g_shm) return;
	g_shm->enable = g_enable;
	if (g_enable) {
		HoldProd();
		g_shm->prodTick = GetTickCount();
		StartHost32();
	} else {
		g_shm->w = g_shm->r;
		g_shm->active = 0;
		g_shm->allOffReq = 1;
	}
}

void KbsGimicBegin(int chip, uint32_t clockHz, uint32_t sampleRate)
{
	if (!g_enable) return;
	MapShm();
	if (!g_shm) return;
	uint32_t rate = (sampleRate >= 8000) ? sampleRate : g_shm->sampleRate;
	HoldProd();
	StartHost32();
	if (g_shm->active && g_shm->chip == chip && g_shm->clockHz == clockHz && g_shm->sampleRate == rate)
		return;
	g_shm->enable = 1;
	g_shm->chip = chip;
	g_shm->clockHz = clockHz;
	if (sampleRate >= 8000) g_shm->sampleRate = sampleRate;
	g_shm->active = 1;
	/* 古いキューを空にしてからリセットを頼む。ホストが r=w で消すと、
	   この直後の音色ロード（ch1 以降・SSG・リズム）まで落ちる。 */
	g_shm->w = g_shm->r;
	g_shm->resetReq = 1;
	g_shm->burst = 0;
	g_shm->wrote = 0;
	g_clock = 0;
	InterlockedExchange64(&g_shm->gen, 0);
	g_shm->prodTick = GetTickCount();
	/* c86ctl の初回 Open は 200ms では足りない。devOk まで待たないと
	   先頭のレジスタがリセットに消える／ソフト側の初音がスピーカーに漏れる。 */
	for (int i = 0; i < 3000; ++i) {
		if (!g_shm->resetReq && g_shm->devOk)
			break;
		if (!g_shm->resetReq && g_shm->moduleType && !g_shm->devOk)
			break;
		Sleep(1);
	}
}

void KbsGimicWrite(uint32_t addr, uint8_t data, int64_t sample)
{
	if (!g_enable) {
		MapShm();
		if (!g_shm || !g_shm->enable) return;
	} else {
		MapShm();
	}
	PushOp(addr, data, sample, 0);
}

void KbsGimicWritePaced(uint32_t addr, uint8_t data, int64_t sample)
{
	if (!g_enable) {
		MapShm();
		if (!g_shm || !g_shm->enable) return;
	} else {
		MapShm();
	}
	PushOp(addr, data, sample, 1);
}

void KbsGimicSetQueueWait(int ms)
{
	if (ms < 40) ms = 40;
	if (ms > 20000) ms = 20000;
	g_queueWaitMs = ms;
}

void KbsGimicAddSamples(uint32_t n)
{
	MapShm();
	if (!g_shm || !g_shm->enable || !g_shm->active || !n) return;
	InterlockedExchangeAdd64(&g_shm->gen, (LONG64)n);
}

void KbsGimicSetSample(int64_t sample)
{
	MapShm();
	if (!g_shm || sample < 0) return;
	InterlockedExchange64(&g_shm->gen, sample);
}

void KbsGimicWriteNow(uint32_t addr, uint8_t data)
{
	MapShm();
	int64_t sample = 0;
	if (g_shm) sample = (int64_t)g_shm->gen;
	KbsGimicWrite(addr, data, sample);
}

void KbsGimicSetBurst(int on)
{
	MapShm();
	if (g_shm) g_shm->burst = on ? 1 : 0;
}

void KbsGimicSetExtraLag(int64_t samples)
{
	if (samples < 0) samples = 0;
	g_extraLag = samples;
}

void KbsGimicDropPending(void)
{
	g_clock = 0;
	MapShm();
	if (!g_shm) return;
	g_shm->w = g_shm->r;
}

void KbsGimicWaitDrained(int ms)
{
	MapShm();
	if (!g_shm) return;
	if (ms < 1) ms = 1;
	if (ms > 20000) ms = 20000;
	for (int i = 0; i < ms; i++) {
		if (g_shm->w == g_shm->r) return;
		Sleep(1);
	}
}

void KbsGimicEnd(void)
{
	g_extraLag = 0;
	MapShm();
	if (!g_shm) return;
	/* 未送信のキーオンは捨てる。ホストがノートオフを出してから終わる。 */
	g_shm->w = g_shm->r;
	g_shm->active = 0;
	g_shm->burst = 0;
	g_shm->wrote = 0;
	g_shm->allOffReq = 1;
	g_clock = 0;
}

int KbsGimicDevOk(void)
{
	MapShm();
	if (!g_shm || !g_shm->enable || !g_shm->active) return 0;
	return g_shm->devOk ? 1 : 0;
}

int KbsGimicSilence(void* interleavedS16, uint32_t frames, int64_t playSample, uint32_t sampleRate)
{
	if (!g_enable) return 0;
	MapShm();
	if (!g_shm || !g_shm->enable || !g_shm->active) return 0;
	PublishClock(playSample, sampleRate);
	/* 初期化中（resetReq）もソフトを出さない。devOk 待ちの間に先頭が漏れる。 */
	if (!g_shm->devOk && !g_shm->resetReq) return 0;
	/* 全ゼロは本体が無音と見て数秒で止める。16bit の判定は ±32。
	   48 の直流はそれを超え、周波数がないのでスピーカーからは聞こえない。 */
	if (interleavedS16 && frames) {
		int16_t* s = (int16_t*)interleavedS16;
		const uint32_t n = frames * 2u;
		for (uint32_t i = 0; i < n; ++i)
			s[i] = 48;
	}
	return 1;
}

#ifndef _WIN64

#include <objbase.h>
#pragma comment(lib, "ole32.lib")

/* GUIDs present in the 2013 c86ctl.dll. IRealChip3 is not in that DLL. */
static const GUID IID_IRealChipBase =
{ 0x5c457918, 0xe66d, 0x4ac1, { 0x8c, 0xb5, 0xb9, 0x1c, 0x47, 0x04, 0xdf, 0x79 } };
static const GUID IID_IRealChip =
{ 0xf959c007, 0x6b4d, 0x46f3, { 0xbb, 0x60, 0x9b, 0x08, 0x97, 0xc7, 0xe6, 0x42 } };
static const GUID IID_IRealChip2 =
{ 0xbefa830a, 0x0df3, 0x46e4, { 0xa7, 0x9e, 0xfa, 0xbb, 0x78, 0xe8, 0x03, 0x57 } };
static const GUID IID_IGimic =
{ 0x175c7da0, 0x8aa5, 0x4173, { 0x96, 0xda, 0xbb, 0x43, 0xb8, 0xeb, 0x8f, 0x17 } };
static const GUID IID_IGimic2 =
{ 0x47141a01, 0x15f5, 0x4bf5, { 0x95, 0x54, 0xca, 0x7a, 0xac, 0xd5, 0x4b, 0xb8 } };

struct IRealChipBase : public IUnknown {
	virtual int __stdcall initialize(void) = 0;
	virtual int __stdcall deinitialize(void) = 0;
	virtual int __stdcall getNumberOfChip(void) = 0;
	virtual HRESULT __stdcall getChipInterface(int id, REFIID riid, void** ppi) = 0;
};
struct IRealChip : public IUnknown {
	virtual int __stdcall reset(void) = 0;
	virtual void __stdcall out(UINT addr, UCHAR data) = 0;
	virtual UCHAR __stdcall in(UINT addr) = 0;
};
struct IRealChip2 : public IRealChip {
	virtual int __stdcall getChipStatus(UINT addr, UCHAR* status) = 0;
	virtual void __stdcall directOut(UINT addr, UCHAR data) = 0;
};
struct Devinfo { char Devname[16]; char Rev; char Serial[15]; };
struct IGimic : public IUnknown {
	virtual int __stdcall getFWVer(UINT* major, UINT* minor, UINT* rev, UINT* build) = 0;
	virtual int __stdcall getMBInfo(Devinfo* info) = 0;
	virtual int __stdcall getModuleInfo(Devinfo* info) = 0;
	virtual int __stdcall setSSGVolume(UCHAR vol) = 0;
	virtual int __stdcall getSSGVolume(UCHAR* vol) = 0;
	virtual int __stdcall setPLLClock(UINT clock) = 0;
	virtual int __stdcall getPLLClock(UINT* clock) = 0;
};
struct IGimic2 : public IGimic {
	virtual int __stdcall getModuleType(UINT* type) = 0;
};

static HMODULE g_dll = NULL;
static IRealChipBase* g_base = NULL;
static IRealChip* g_chip = NULL;
static IRealChip2* g_chip2 = NULL;
static IGimic* g_gimic = NULL;
static IGimic2* g_gimic2 = NULL;
static HANDLE g_stop = NULL;
static HANDLE g_thread = NULL;
static HANDLE g_mx = NULL;
static uint32_t g_iniClock = 7987200;
static uint32_t g_iniSsg = 60;
static uint32_t g_modType = 0;
static int g_opened = 0;
static uint32_t g_appliedClock = 0;

static int ChipMatches(int song, uint32_t mod)
{
	if (song == KBS_GIMIC_CHIP_OPM)
		return mod == 0x00002u;
	if (song == KBS_GIMIC_CHIP_OPL)
		return mod == 0x00004u || mod == 0x0000au || mod == 0x0000bu;
	if (song == KBS_GIMIC_CHIP_OPLL)
		return mod == 0x00005u;
	/* Unknown module type is treated as OPNA (ini PLL is the OPNA clock). */
	if (song == KBS_GIMIC_CHIP_OPNA)
		return mod == 0 || mod == 0x00001u || mod == 0x10001u || mod == 0x00003u
			|| mod == 0x00008u || mod == 0x00009u || mod == 0x00021u;
	return 0;
}

static void ReadIniBesideDll(const wchar_t* dllPath)
{
	wchar_t ini[MAX_PATH];
	wcsncpy_s(ini, dllPath, _TRUNCATE);
	wchar_t* sl = wcsrchr(ini, L'\\');
	if (!sl) return;
	sl[1] = 0;
	wcscat_s(ini, L"c86ctl.ini");
	g_iniSsg = GetPrivateProfileIntW(L"C86CTL", L"m0_gimic_ssgvol", 60, ini);
	g_iniClock = GetPrivateProfileIntW(L"C86CTL", L"m0_gimic_pllclk", 7987200, ini);
	if (g_iniSsg > 127u) g_iniSsg = 127u;
	if (g_iniClock < 1000000u || g_iniClock > 16000000u) g_iniClock = 7987200;
}

static void ReleaseChip(void)
{
	if (g_gimic2) { g_gimic2->Release(); g_gimic2 = NULL; }
	if (g_gimic) { g_gimic->Release(); g_gimic = NULL; }
	if (g_chip2) { g_chip2->Release(); g_chip2 = NULL; }
	if (g_chip) { g_chip->Release(); g_chip = NULL; }
	if (g_base) {
		if (g_opened) g_base->deinitialize();
		g_base->Release();
		g_base = NULL;
	}
	g_opened = 0;
	if (g_dll) { FreeLibrary(g_dll); g_dll = NULL; }
}

static int OpenChip(void)
{
	if (g_opened) return g_chip ? 1 : 0;
	wchar_t dll[MAX_PATH];
	if (!FindBetween(L"c86ctl.dll", dll, MAX_PATH)) return 0;
	ReadIniBesideDll(dll);
	g_dll = LoadLibraryW(dll);
	if (!g_dll) return 0;
	typedef HRESULT (WINAPI *FnCreate)(REFIID, void**);
	FnCreate create = (FnCreate)GetProcAddress(g_dll, "CreateInstance");
	if (!create) { ReleaseChip(); return 0; }
	void* pv = NULL;
	if (FAILED(create(IID_IRealChipBase, &pv)) || !pv) { ReleaseChip(); return 0; }
	g_base = (IRealChipBase*)pv;
	if (g_base->initialize() != 0) { ReleaseChip(); return 0; }
	g_opened = 1;
	if (g_base->getNumberOfChip() <= 0) { ReleaseChip(); return 0; }
	pv = NULL;
	if (SUCCEEDED(g_base->getChipInterface(0, IID_IGimic2, &pv)) && pv) {
		g_gimic2 = (IGimic2*)pv;
		g_gimic = g_gimic2;
		g_gimic->AddRef();
		UINT t = 0;
		if (g_gimic2->getModuleType(&t) == 0) g_modType = t;
	} else {
		pv = NULL;
		if (SUCCEEDED(g_base->getChipInterface(0, IID_IGimic, &pv)) && pv)
			g_gimic = (IGimic*)pv;
	}
	pv = NULL;
	if (SUCCEEDED(g_base->getChipInterface(0, IID_IRealChip2, &pv)) && pv) {
		g_chip2 = (IRealChip2*)pv;
		g_chip = g_chip2;
		g_chip->AddRef();
	} else {
		pv = NULL;
		if (SUCCEEDED(g_base->getChipInterface(0, IID_IRealChip, &pv)) && pv)
			g_chip = (IRealChip*)pv;
	}
	if (!g_chip) { ReleaseChip(); return 0; }
	if (g_gimic) {
		g_gimic->setSSGVolume((UCHAR)g_iniSsg);
		g_gimic->setPLLClock(g_iniClock);
		g_appliedClock = g_iniClock;
	}
	g_chip->reset();
	return 1;
}

static void ChipOut(uint32_t addr, uint8_t data)
{
	/* directOut は連続するとファームの FIFO から落ち、先に来た ch0 だけ残る。
	   out は DLL 側でバス待ちして全部書く。 */
	if (g_chip) g_chip->out(addr, data);
}

/* 停止・終了で最後の音を残さない。out の末尾に置くので、先に積んだキーオンより後に効く。 */
static void SilenceChip(void)
{
	if (!g_chip || !g_shm) return;
	const int song = g_shm->chip;
	if (song == KBS_GIMIC_CHIP_OPM) {
		for (int ch = 0; ch < 8; ++ch) ChipOut(0x08, (uint8_t)ch);
		return;
	}
	if (song == KBS_GIMIC_CHIP_OPL) {
		for (int ch = 0; ch < 9; ++ch) {
			ChipOut((uint32_t)(0xB0 + ch), 0);
			ChipOut((uint32_t)(0x1B0 + ch), 0);
		}
		ChipOut(0xBD, 0);
		return;
	}
	if (song == KBS_GIMIC_CHIP_OPLL) {
		for (int ch = 0; ch < 9; ++ch) ChipOut((uint32_t)(0x20 + ch), 0);
		return;
	}
	for (int s = 0; s < 3; ++s) {
		ChipOut((uint32_t)(0x80 + s), 0xFF);
		ChipOut((uint32_t)(0x84 + s), 0xFF);
		ChipOut((uint32_t)(0x88 + s), 0xFF);
		ChipOut((uint32_t)(0x8C + s), 0xFF);
		ChipOut((uint32_t)(0x180 + s), 0xFF);
		ChipOut((uint32_t)(0x184 + s), 0xFF);
		ChipOut((uint32_t)(0x188 + s), 0xFF);
		ChipOut((uint32_t)(0x18C + s), 0xFF);
	}
	ChipOut(0x28, 0x00);
	ChipOut(0x28, 0x01);
	ChipOut(0x28, 0x02);
	ChipOut(0x28, 0x04);
	ChipOut(0x28, 0x05);
	ChipOut(0x28, 0x06);
	ChipOut(0x08, 0);
	ChipOut(0x09, 0);
	ChipOut(0x0A, 0);
	ChipOut(0x07, 0x3F);
	ChipOut(0x10, 0xBF);
	ChipOut(0x100, 0xBF);
}

static void Drain(void)
{
	if (!g_shm || g_shm->magic != KBS_GIMIC_MAGIC) return;
	g_shm->hostTick = GetTickCount();
	if (!g_opened) {
		if (g_shm->enable && g_shm->active) OpenChip();
		if (!g_opened) {
			g_shm->devOk = 0;
			return;
		}
	}
	if (g_shm->allOffReq) {
		g_shm->allOffReq = 0;
		g_shm->r = g_shm->w;
		SilenceChip();
	}
	int justReset = 0;
	if (g_shm->resetReq) {
		g_shm->resetReq = 0;
		if (g_chip) g_chip->reset();
		g_appliedClock = 0;
		justReset = 1;
	}
	int song = g_shm->chip;
	int ok = ChipMatches(song, g_modType);
	g_shm->moduleType = g_modType;
	g_shm->devOk = (g_shm->enable && g_shm->active && ok) ? 1 : 0;
	if (!ok) {
		g_shm->r = g_shm->w;
		return;
	}
	uint32_t clk = g_shm->clockHz ? g_shm->clockHz : g_iniClock;
	if (g_gimic && clk && clk != g_appliedClock) {
		g_gimic->setPLLClock(clk);
		g_appliedClock = clk;
	}
	int64_t playSample = 0, lagSamples = 0, qpc = 0, qpcFreq = 0;
	uint32_t sampleRate = 0;
	for (int i = 0; i < 4; ++i) {
		long a = g_shm->seq;
		if (a & 1) continue;
		MemoryBarrier();
		playSample = g_shm->playSample;
		lagSamples = g_shm->lagSamples;
		qpc = g_shm->qpc;
		qpcFreq = g_shm->qpcFreq;
		sampleRate = g_shm->sampleRate;
		MemoryBarrier();
		if (g_shm->seq == a) break;
	}
	int64_t heard = playSample - lagSamples;
	if (heard < 0) heard = 0;
	int rate = (sampleRate >= 8000) ? (int)sampleRate : 44100;
	if (qpcFreq > 0 && qpc > 0) {
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		int64_t dt = now.QuadPart - qpc;
		if (dt < 0) dt = 0;
		heard += dt * (int64_t)rate / qpcFreq;
	}
	if (heard > playSample) heard = playSample;
	/* リセット直後は時計がまだ 0。先頭のキーオンを時刻待ちで落とさない。 */
	int blast = (g_shm->burst || g_shm->force || justReset) ? 1 : 0;
	long r = g_shm->r;
	long w = g_shm->w;
	int n = 0;
	while (r != w && n < 4096) {
		KbsGimicOp* op = &g_shm->q[(unsigned long)r % (unsigned)KBS_GIMIC_Q];
		if (!blast && op->sample > heard) break;
		const uint32_t addr = op->addr;
		const uint8_t data = op->data;
		r++;
		/* バッチの終わりまで r を出さないと、64KB でキューが詰まり以降の ADPCM が落ちる。 */
		g_shm->r = r;
		/* ADPCM も状態は読まない。USB 往復が 1 バイトごとに入り、転送が止まる。 */
		ChipOut(addr, data);
		n++;
		w = g_shm->w;
	}
	if (g_shm->r == g_shm->w) g_shm->force = 0;
}

static DWORD WINAPI PumpProc(void*)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	for (;;) {
		if (WaitForSingleObject(g_stop, 1) == WAIT_OBJECT_0) break;
		MSG msg;
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		Drain();
	}
	if (g_shm) g_shm->allOffReq = 0;
	SilenceChip();
	Sleep(30);
	if (g_chip) g_chip->reset();
	ReleaseChip();
	if (g_shm) g_shm->devOk = 0;
	CoUninitialize();
	return 0;
}

void KbsGimicHostStart(void)
{
	if (g_thread) return;
	MapShm();
	g_mx = CreateMutexW(NULL, FALSE, KBS_GIMIC_MX);
	g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!g_stop) return;
	g_thread = CreateThread(NULL, 0, PumpProc, NULL, 0, NULL);
}

void KbsGimicHostStop(void)
{
	if (g_stop) SetEvent(g_stop);
	if (g_thread) {
		WaitForSingleObject(g_thread, 2000);
		CloseHandle(g_thread);
		g_thread = NULL;
	}
	if (g_stop) { CloseHandle(g_stop); g_stop = NULL; }
	if (g_mx) { CloseHandle(g_mx); g_mx = NULL; }
}

int KbsGimicHostBusy(void)
{
	if (!g_shm || g_shm->magic != KBS_GIMIC_MAGIC) return 0;
	if (!g_shm->enable) return 0;
	HANDLE mx = OpenMutexW(SYNCHRONIZE, FALSE, KBS_GIMIC_PROD);
	if (mx) { CloseHandle(mx); return 1; }
	if (!g_shm->active) return 0;
	uint32_t t = g_shm->prodTick;
	if (!t) return 0;
	return (GetTickCount() - t < 2000u) ? 1 : 0;
}

#else

void KbsGimicHostStart(void) {}
void KbsGimicHostStop(void) {}
int KbsGimicHostBusy(void) { return 0; }

#endif
