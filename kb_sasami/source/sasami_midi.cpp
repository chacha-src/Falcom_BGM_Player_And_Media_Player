#include "sasami_midi.h"
#include "sasami_misao.h"

#include <windows.h>
#include <algorithm>
#include <string.h>
#include <stdio.h>

namespace {

struct MidiEv {
	uint32_t tick;
	int port;
	int seq;
	uint8_t len;
	uint8_t bytes[128];
};

struct MidiTrackState {
	uint32_t addr;
	int count;
	int part;
	int port;
	int note;
	int vel;
	enum { MIDI_LOOP_NEST = 16 };
	int loopStack[MIDI_LOOP_NEST];
	int loopSp;
	int drum;
	int backJumps;
	int loopSafety;
	int alive;
	int everJump;
	int pedal;
	uint32_t loopStartTick;
	uint32_t loopEndTick;
};

enum { SASAMI_MAX_EV = 49152, SASAMI_MAX_FIRST = 16384 };
enum { TRK0_CAP = 256 * 1024, TRK1_CAP = 512 * 1024, TRK2_CAP = 512 * 1024 };

static MidiEv* s_evs;
static int s_evCount;
static struct { uint64_t key; uint32_t tick; } s_first[SASAMI_MAX_FIRST];
static int s_firstCount;
static uint8_t* s_trk0;
static uint8_t* s_trk1;
static uint8_t* s_trk2;
static int s_trkLen[3];
static uint8_t* s_smfWork;
static int s_evSeq;
static int s_midiReady;

static int EnsureMidiWork()
{
	if (s_midiReady) return 1;
	s_evs = (MidiEv*)VirtualAlloc(NULL, sizeof(MidiEv) * (SIZE_T)SASAMI_MAX_EV, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_trk0 = (uint8_t*)VirtualAlloc(NULL, TRK0_CAP, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_trk1 = (uint8_t*)VirtualAlloc(NULL, TRK1_CAP, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_trk2 = (uint8_t*)VirtualAlloc(NULL, TRK2_CAP, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_smfWork = (uint8_t*)VirtualAlloc(NULL, SASAMI_MAX_SMF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!s_evs || !s_trk0 || !s_trk1 || !s_trk2 || !s_smfWork)
		return 0;
	s_midiReady = 1;
	return 1;
}

static void PutBe32(uint8_t* buf, int* len, int cap, uint32_t v)
{
	if (*len + 4 > cap) return;
	buf[(*len)++] = (uint8_t)(v >> 24);
	buf[(*len)++] = (uint8_t)(v >> 16);
	buf[(*len)++] = (uint8_t)(v >> 8);
	buf[(*len)++] = (uint8_t)v;
}

static void PutBe16(uint8_t* buf, int* len, int cap, uint16_t v)
{
	if (*len + 2 > cap) return;
	buf[(*len)++] = (uint8_t)(v >> 8);
	buf[(*len)++] = (uint8_t)v;
}

static void PutVlq(uint8_t* buf, int* len, int cap, uint32_t v)
{
	uint8_t b[5];
	int n = 0;
	b[n++] = (uint8_t)(v & 0x7F);
	v >>= 7;
	while (v) {
		b[n++] = (uint8_t)((v & 0x7F) | 0x80);
		v >>= 7;
	}
	while (n--) {
		if (*len >= cap) return;
		buf[(*len)++] = b[n];
	}
}

static void PushEv(uint32_t tick, int port, const uint8_t* d, int n)
{
	if (!d || n <= 0 || n > 128) return;
	if (s_evCount >= SASAMI_MAX_EV) return;
	MidiEv& e = s_evs[s_evCount++];
	e.tick = tick;
	e.port = port;
	e.seq = s_evSeq++;
	e.len = (uint8_t)n;
	memcpy(e.bytes, d, (size_t)n);
}

static void PushShort(uint32_t tick, int port, uint8_t st, uint8_t a, uint8_t b)
{
	uint8_t d[3] = { st, a, b };
	const int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 2 : 3;
	PushEv(tick, port, d, n);
}

// MMODE1J / MMJ4: per-channel init (pitch bend range ±2 octaves, GS bank, volumes).
static void PushMmodeChannelInit(uint32_t tick, int port, int ch, SasamiMidiMap map, int gsBankLsb, int flg88)
{
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x65, 0);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x64, 0);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, 0x18);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x01, 0);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x00, 0);
	if (flg88 != 2) {
		uint8_t cc32 = 0;
		if (map == SASAMI_MAP_GS55)
			cc32 = 2;
		else if (map == SASAMI_MAP_GS88) {
			if (gsBankLsb == 3) cc32 = 3;
			else if (gsBankLsb == 4) cc32 = 4;
			else cc32 = 1;
		}
		if (cc32)
			PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, cc32);
	}
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, 100);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, 64);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 11, 127);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0);
	PushShort(tick, port, (uint8_t)(0xE0 | ch), 0x00, 0x40);
}

static void PushGs(uint32_t tick, int port, uint8_t a, uint8_t b, uint8_t c, uint8_t v)
{
	uint8_t sx[11] = { 0xF0, 0x41, 0x10, 0x42, 0x12, a, b, c, v, 0, 0xF7 };
	int s = (int)a + (int)b + (int)c + (int)v;
	sx[9] = (uint8_t)((128 - (s % 128)) & 0x7F);
	PushEv(tick, port, sx, 11);
}

static void PushExclBody(uint32_t tick, int port, int xg, uint8_t devId, const uint8_t* body, int bodyLen)
{
	if (!body || bodyLen <= 0 || bodyLen > 110) return;
	uint8_t sx[128];
	int n = 0;
	int sum = 0;
	if (xg) {
		sx[n++] = 0xF0;
		sx[n++] = 0x43;
		sx[n++] = 0x10;
		sx[n++] = devId;
	} else {
		sx[n++] = 0xF0;
		sx[n++] = 0x41;
		sx[n++] = 0x10;
		sx[n++] = devId;
		sx[n++] = 0x12;
	}
	for (int i = 0; i < bodyLen; i++) {
		sx[n] = body[i];
		sum += sx[n];
		n++;
	}
	if (!xg)
		sx[n++] = (uint8_t)((128 - (sum % 128)) & 0x7F);
	sx[n++] = 0xF7;
	PushEv(tick, port, sx, n);
}

static int GsPartIdx(int ch)
{
	if (ch == 9) return 0;
	if (ch < 9) return ch + 1;
	return ch;
}

static uint32_t ReadJump(const SasamiSong& s, uint32_t addr, int ver, uint32_t* nextOff)
{
	if (ver == 1) {
		const uint16_t a = SasamiGet16(s, addr + 1);
		*nextOff = addr + 3;
		return (a >= 0x1000) ? (uint32_t)(a - 0x1000) : a;
	}
	const uint32_t a = SasamiGet24(s, addr + 1);
	*nextOff = addr + 4;
	return (a >= 0x1000) ? (a - 0x1000) : a;
}

static uint8_t* TrkPtr(int t)
{
	if (t == 0) return s_trk0;
	if (t == 1) return s_trk1;
	return s_trk2;
}

static int TrkCap(int t)
{
	if (t == 0) return TRK0_CAP;
	if (t == 1) return TRK1_CAP;
	return TRK2_CAP;
}

static void WriteSmf(int nports, uint8_t* out, int outCap, int* outSize)
{
	std::stable_sort(s_evs, s_evs + s_evCount, [](const MidiEv& a, const MidiEv& b) {
		if (a.tick != b.tick) return a.tick < b.tick;
		return a.seq < b.seq;
	});

	if (nports < 1) nports = 1;
	if (nports > 2) nports = 2;
	const int ntr = 1 + nports;
	s_trkLen[0] = s_trkLen[1] = s_trkLen[2] = 0;
	uint32_t last[3] = { 0, 0, 0 };

	for (int p = 0; p < nports; p++) {
		const int tr = 1 + p;
		uint8_t* tb = TrkPtr(tr);
		int* tl = &s_trkLen[tr];
		const int tc = TrkCap(tr);
		PutVlq(tb, tl, tc, 0);
		if (*tl + 4 <= tc) {
			tb[(*tl)++] = 0xFF;
			tb[(*tl)++] = 0x21;
			tb[(*tl)++] = 0x01;
			tb[(*tl)++] = (uint8_t)p;
		}
	}

	for (int i = 0; i < s_evCount; i++) {
		const MidiEv& e = s_evs[i];
		if (e.len == 0) continue;
		int tr;
		if (e.len >= 2 && e.bytes[0] == 0xFF)
			tr = 0;
		else
			tr = 1 + (e.port ? 1 : 0);
		if (tr < 0 || tr >= ntr) continue;
		uint8_t* tb = TrkPtr(tr);
		int* tl = &s_trkLen[tr];
		const int tc = TrkCap(tr);
		PutVlq(tb, tl, tc, e.tick - last[tr]);
		last[tr] = e.tick;
		if (e.bytes[0] == 0xF0) {
			if (*tl + 1 > tc) continue;
			tb[(*tl)++] = 0xF0;
			PutVlq(tb, tl, tc, (uint32_t)(e.len - 1));
			const int rest = e.len - 1;
			if (*tl + rest > tc) continue;
			memcpy(tb + *tl, e.bytes + 1, (size_t)rest);
			*tl += rest;
		} else {
			if (*tl + e.len > tc) continue;
			memcpy(tb + *tl, e.bytes, e.len);
			*tl += e.len;
		}
	}
	for (int t = 0; t < ntr; t++) {
		uint8_t* tb = TrkPtr(t);
		int* tl = &s_trkLen[t];
		const int tc = TrkCap(t);
		PutVlq(tb, tl, tc, 0);
		if (*tl + 3 <= tc) {
			tb[(*tl)++] = 0xFF;
			tb[(*tl)++] = 0x2F;
			tb[(*tl)++] = 0x00;
		}
	}

	int olen = 0;
	if (outCap < 14) {
		if (outSize) *outSize = 0;
		return;
	}
	out[olen++] = 'M'; out[olen++] = 'T'; out[olen++] = 'h'; out[olen++] = 'd';
	PutBe32(out, &olen, outCap, 6);
	PutBe16(out, &olen, outCap, 1);
	PutBe16(out, &olen, outCap, (uint16_t)ntr);
	PutBe16(out, &olen, outCap, (uint16_t)SASAMI_PPQN);
	for (int t = 0; t < ntr; t++) {
		if (olen + 8 + s_trkLen[t] > outCap) {
			if (outSize) *outSize = 0;
			return;
		}
		out[olen++] = 'M'; out[olen++] = 'T'; out[olen++] = 'r'; out[olen++] = 'k';
		PutBe32(out, &olen, outCap, (uint32_t)s_trkLen[t]);
		memcpy(out + olen, TrkPtr(t), (size_t)s_trkLen[t]);
		olen += s_trkLen[t];
	}
	if (outSize) *outSize = olen;
}

} // namespace

static uint64_t SasamiCacheHashPathW(const wchar_t* path)
{
	uint64_t h = 14695981039346656037ULL;
	if (!path) return h;
	for (const wchar_t* p = path; *p; ++p) {
		wchar_t c = *p;
		if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
		if (c == L'/') c = L'\\';
		h ^= (uint64_t)(unsigned short)c;
		h *= 1099511628211ULL;
	}
	return h;
}

void SasamiMapForceToSel(int mapForce, SasamiMidiMap* map, int* gsBankLsb)
{
	SasamiMidiMap m = SASAMI_MAP_GS88;
	int lsb = 2;
	switch (mapForce) {
	case 1: m = SASAMI_MAP_GS88; lsb = 2; break;
	case 2: m = SASAMI_MAP_XG; lsb = 0; break;
	case 3: m = SASAMI_MAP_GS55; lsb = 1; break;
	case 4: m = SASAMI_MAP_GS88; lsb = 2; break;
	case 5: m = SASAMI_MAP_GS88; lsb = 3; break;
	case 6: m = SASAMI_MAP_GS88; lsb = 4; break;
	case 7: m = SASAMI_MAP_GM; lsb = 0; break;
	case 8: m = SASAMI_MAP_GM; lsb = 0; break;
	case 9: m = SASAMI_MAP_GM; lsb = 0; break;
	default:
		if (mapForce >= 10) { m = SASAMI_MAP_GM; lsb = 0; }
		break;
	}
	if (map) *map = m;
	if (gsBankLsb) *gsBankLsb = lsb;
}

int SasamiReadMidMapForceW(const wchar_t* fol, int* outForce)
{
	if (!fol || !fol[0] || !outForce) return 0;
	*outForce = 0;
	wchar_t base[MAX_PATH] = {};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) || !base[0])
		return 0;
	wchar_t path[MAX_PATH] = {};
	_snwprintf_s(path, _TRUNCATE, L"%s\\oggYSED\\midflag\\%016I64X",
		base, (unsigned long long)SasamiCacheHashPathW(fol));
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	BYTE b[8] = {};
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, b, 8, &rd, NULL);
	CloseHandle(h);
	if (!ok || rd < 5 || b[0] != 1) return 0;
	*outForce = (int)b[4];
	return 1;
}

int SasamiReadFmForceW(const wchar_t* fol, int* outForce)
{
	if (!fol || !fol[0] || !outForce) return 0;
	*outForce = -1;
	wchar_t base[MAX_PATH] = {};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) || !base[0])
		return 0;
	wchar_t path[MAX_PATH] = {};
	_snwprintf_s(path, _TRUNCATE, L"%s\\oggYSED\\midflag\\%016I64X",
		base, (unsigned long long)SasamiCacheHashPathW(fol));
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	BYTE b[8] = {};
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, b, 8, &rd, NULL);
	CloseHandle(h);
	if (!ok || rd < 6 || b[0] != 1) return 0;
	if (b[5] > 2) return 0;
	*outForce = (int)b[5];
	return 1;
}

int SasamiResolveMapForceW(const wchar_t* fol, int globalDefault)
{
	int pf = 0;
	if (SasamiReadMidMapForceW(fol, &pf) && pf > 0)
		return pf;
	if (globalDefault > 0)
		return globalDefault;
	return 4;
}

int SasamiResolveFmModeW(const wchar_t* fol, int globalDefault)
{
	int pf = -1;
	if (SasamiReadFmForceW(fol, &pf) && pf >= 0 && pf <= 2)
		return pf;
	if (globalDefault >= 0 && globalDefault <= 2)
		return globalDefault;
	return 2;
}

static int SasamiRegistryMapDefault()
{
	HKEY hKey = NULL;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
		L"Software\\Kobarin's Soft\\oggYSEDbgm\\KpiV5Config\\kbsasami\\kbsasami",
		0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return 4;
	wchar_t buf[32] = {};
	DWORD sz = sizeof(buf);
	DWORD type = 0;
	int v = -1;
	if (RegQueryValueExW(hKey, L"midimode", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
		v = _wtoi(buf);
	else {
		sz = sizeof(buf);
		type = 0;
		if (RegQueryValueExW(hKey, L"map", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
			v = _wtoi(buf);
	}
	RegCloseKey(hKey);
	return (v >= 0 && v <= 19) ? v : 4;
}

static void SasamiTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars);

struct SasamiTempCache {
	wchar_t src[MAX_PATH];
	FILETIME srcWrite;
	DWORD srcSize;
	int mapForce;
};
static SasamiTempCache s_tempCache;

static int SasamiReadSourceStamp(const wchar_t* src, FILETIME* writeTime, DWORD* size)
{
	if (!src || !src[0] || !writeTime || !size) return 0;
	HANDLE h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > 0x7FFFFFFF) {
		CloseHandle(h);
		return 0;
	}
	FILETIME c, a, w;
	if (!GetFileTime(h, &c, &a, &w)) {
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	*writeTime = w;
	*size = (DWORD)li.QuadPart;
	return 1;
}

static int SasamiTempCacheValid(const wchar_t* src, int mapForce, const wchar_t* dest)
{
	if (!src || !src[0] || !dest || !dest[0] || !s_tempCache.src[0]) return 0;
	if (_wcsicmp(s_tempCache.src, src) != 0 || s_tempCache.mapForce != mapForce) return 0;
	FILETIME wt;
	DWORD sz = 0;
	if (!SasamiReadSourceStamp(src, &wt, &sz)) return 0;
	if (CompareFileTime(&s_tempCache.srcWrite, &wt) != 0 || s_tempCache.srcSize != sz) return 0;
	const DWORD attr = GetFileAttributesW(dest);
	return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

void SasamiInvalidateTempMidi(const wchar_t* src)
{
	if (!src || !src[0] || !SasamiExtIsMidi(src)) return;
	wchar_t dest[MAX_PATH] = {};
	SasamiTempMidiPath(src, dest, MAX_PATH);
	DeleteFileW(dest);
	if (s_tempCache.src[0] && _wcsicmp(s_tempCache.src, src) == 0)
		memset(&s_tempCache, 0, sizeof(s_tempCache));
}

bool SasamiConvertToSmf(const SasamiSong& song, SasamiMidiMap map, int gsBankLsb, uint8_t* out, int outCap, int* outSize)
{
	if (!out || !outSize || outCap <= 0) return false;
	*outSize = 0;
	if (!EnsureMidiWork()) return false;
	if (song.kind != SASAMI_KIND_MPY && song.kind != SASAMI_KIND_MPW2 && song.kind != SASAMI_KIND_MPW3) return false;
	if (song.trackCount <= 0) return false;

	const int ver = song.mpyVersion;
	const int flg88 = (map == SASAMI_MAP_GS88) ? 1 : ((map == SASAMI_MAP_XG) ? 2 : 0);
	const int isGm = (map == SASAMI_MAP_GM) ? 1 : 0;

	s_evSeq = 0;
	s_evCount = 0;
	s_firstCount = 0;
	MidiTrackState tr[64];
	memset(tr, 0, sizeof(tr));
	int nAlive = 0;
	for (int i = 0; i < song.trackCount && i < 64; i++) {
		tr[i].addr = song.tracks[i].fileOff;
		tr[i].count = 0;
		tr[i].part = song.tracks[i].part & 0x0F;
		tr[i].port = 0;
		tr[i].note = 0;
		tr[i].vel = 100;
		tr[i].loopSp = 0;
		tr[i].drum = (tr[i].part == 9) ? 1 : 0;
		tr[i].backJumps = 0;
		tr[i].loopSafety = 0;
		tr[i].alive = song.tracks[i].unused ? 0 : 1;
		tr[i].everJump = 0;
		tr[i].pedal = 0;
		tr[i].loopStartTick = 0xFFFFFFFFu;
		tr[i].loopEndTick = 0;
		if (tr[i].alive && tr[i].addr == 0xF0) tr[i].alive = 0;
		if (tr[i].alive) nAlive++;
	}
	/* Empty score / all-unused tracks: still emit a minimal SMF so VST preview
	   (.mpsmv with binds only) can open instead of "MIDI not found". */
	if (nAlive == 0) {
		const unsigned T = SASAMI_DEFAULT_T;
		const uint32_t mpqn = (uint32_t)((500ull * T) / 13ull);
		uint8_t d[6] = { 0xFF, 0x51, 0x03, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
		PushEv(0, 0, d, 6);
		WriteSmf(1, out, outCap, outSize);
		return *outSize > 22;
	}

	int nports = song.dualPort ? 2 : 1;
	int port1Ready = (nports >= 2) ? 1 : 0;
	uint8_t chOn[2][16][128];
	memset(chOn, 0, sizeof(chOn));

	{
		const unsigned T = SASAMI_DEFAULT_T;
		const uint32_t mpqn = (uint32_t)((500ull * T) / 13ull);
		uint8_t d[6] = { 0xFF, 0x51, 0x03, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
		PushEv(0, 0, d, 6);
	}
	if (song.titleSjis[0]) {
		size_t n = strlen(song.titleSjis);
		const int m = (n > 120) ? 120 : (int)n;
		uint8_t d[128];
		d[0] = 0xFF; d[1] = 0x03; d[2] = (uint8_t)m;
		memcpy(d + 3, song.titleSjis, (size_t)m);
		PushEv(0, 0, d, 3 + m);
	}

	static const uint8_t kGsReset[11] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
	static const uint8_t kXgOn[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };
	static const uint8_t kGmOn[6] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
	for (int p = 0; p < nports; p++) {
		if (map == SASAMI_MAP_XG) PushEv(0, p, kXgOn, 9);
		else if (map == SASAMI_MAP_GM) PushEv(0, p, kGmOn, 6);
		else PushEv(0, p, kGsReset, 11);
		for (int ch = 0; ch < 16; ch++)
			PushMmodeChannelInit(0, p, ch, map, gsBankLsb, flg88);
	}

	uint32_t tick = 0;
	unsigned curT = SASAMI_DEFAULT_T;
	(void)curT;
	uint32_t gLoopStart = 0xFFFFFFFFu;
	uint32_t gLoopEnd = 0;
	int stopLoopers = 0;

	auto releaseTrack = [&](int i) {
		const int ch = tr[i].part;
		const int port = tr[i].port;
		if (tr[i].note) {
			PushShort(tick, port, (uint8_t)(0x80 | ch), (uint8_t)tr[i].note, 0);
			chOn[port ? 1 : 0][ch][tr[i].note & 127] = 0;
		}
		tr[i].note = 0;
		if (tr[i].pedal) {
			PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0);
			tr[i].pedal = 0;
		}
	};

	auto killTrack = [&](int i) {
		if (!tr[i].alive) return;
		releaseTrack(i);
		tr[i].alive = 0;
	};

	auto ensurePort1 = [&]() {
		if (port1Ready) return;
		port1Ready = 1;
		nports = 2;
		if (map == SASAMI_MAP_XG) PushEv(tick, 1, kXgOn, 9);
		else if (map == SASAMI_MAP_GM) PushEv(tick, 1, kGmOn, 6);
		else PushEv(tick, 1, kGsReset, 11);
		for (int ch = 0; ch < 16; ch++)
			PushMmodeChannelInit(tick, 1, ch, map, gsBankLsb, flg88);
	};

	while (tick < SASAMI_MAX_TICKS) {
		int any = 0;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (!tr[i].alive) continue;
			any = 1;
			int guard = 0;
			while (tr[i].alive && tr[i].count < 1 && guard++ < 4096) {
				const uint32_t addr = tr[i].addr;
				if (!SasamiOffOk(song, addr, 1) || addr == 0xF0) {
					killTrack(i);
					break;
				}
				const int cmd = SasamiGet(song, addr);
				const int ch = tr[i].part;
				const int port = tr[i].port;
				const uint8_t b1 = SasamiGet(song, addr + 1);
				const uint8_t b2 = SasamiGet(song, addr + 2);
				const uint8_t b3 = SasamiGet(song, addr + 3);
				const uint64_t akey = ((uint64_t)i << 32) | addr;
				{
					int found = 0;
					for (int fi = 0; fi < s_firstCount; fi++) {
						if (s_first[fi].key == akey) { found = 1; break; }
					}
					if (!found && s_firstCount < SASAMI_MAX_FIRST) {
						s_first[s_firstCount].key = akey;
						s_first[s_firstCount].tick = tick;
						s_firstCount++;
					}
				}
				int again = 1;

				switch (cmd) {
				case 1: { // note: original always note-off previous then note-on
					const int note = b1;
					if (tr[i].note) {
						PushShort(tick, port, (uint8_t)(0x80 | ch), (uint8_t)tr[i].note, 0);
						chOn[port ? 1 : 0][ch][tr[i].note & 127] = 0;
					} else if (note < 128 && chOn[port ? 1 : 0][ch][note])
						PushShort(tick, port, (uint8_t)(0x80 | ch), (uint8_t)note, 0);
					PushShort(tick, port, (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)tr[i].vel);
					if (note < 128)
						chOn[port ? 1 : 0][ch][note] = 1;
					tr[i].note = note;
					tr[i].count = b2;
					tr[i].addr = addr + 3;
					if (tr[i].count != 0) again = 0;
					break;
				}
				case 2: { // program 88 / 55
					if (!(flg88 == 2 && song.versionWord >= 36000)) {
						if (flg88 == 1)
							PushShort(tick, port, (uint8_t)(0xC0 | ch), b1, 0);
						else if (!isGm || song.versionWord < 50000)
							PushShort(tick, port, (uint8_t)(0xC0 | ch), b2, 0);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 3: // tie
					tr[i].count = b2;
					tr[i].addr = addr + 3;
					if (tr[i].count != 0) again = 0;
					break;
				case 4: { // master vol: LCD 10 00 16 + GS 40 00 04 + GM master
					const uint8_t lcd[4] = { 0x10, 0x00, 0x16, b1 };
					PushExclBody(tick, port, (flg88 == 2) ? 1 : 0, 0x42, lcd, 4);
					if (!isGm && flg88 != 2)
						PushGs(tick, port, 0x40, 0x00, 0x04, b1);
					{
						uint8_t sx[8] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, b1, 0xF7 };
						PushEv(tick, port, sx, 8);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 5: { // track vol: M58CHK 88=b1, 55=b2
					if (!(flg88 == 2 && song.versionWord >= 36000)) {
						const uint8_t vol = (flg88 == 1) ? b1 : b2;
						if (flg88 == 1)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, vol);
						else if (!isGm || song.versionWord < 50000)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, vol);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 6:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 1, b1);
					tr[i].addr = addr + 3;
					break;
				case 7: // bank
					if (!(flg88 == 2 && song.versionWord >= 36000)) {
						if (flg88 == 0 && (!isGm || song.versionWord < 50000))
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b2);
						if (flg88 == 1)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b1);
						if (flg88 == 2)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, b1);
					}
					tr[i].addr = addr + 3;
					break;
				case 8: // rest
					if (tr[i].note) {
						PushShort(tick, port, (uint8_t)(0x80 | ch), (uint8_t)tr[i].note, 0);
						chOn[port ? 1 : 0][ch][tr[i].note & 127] = 0;
					}
					tr[i].note = 0;
					tr[i].count = b2;
					tr[i].addr = addr + 3;
					if (tr[i].count != 0) again = 0;
					break;
				case 9: {
					unsigned T = (unsigned)b1 + ((unsigned)b2 << 8);
					if (T == 0) T = SASAMI_DEFAULT_T;
					curT = T;
					{
						const uint32_t mpqn = (uint32_t)((500ull * T) / 13ull);
						uint8_t d[6] = { 0xFF, 0x51, 0x03, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
						PushEv(tick, 0, d, 6);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 10: {
					uint32_t nxt = 0;
					const uint32_t dest = ReadJump(song, addr, ver, &nxt);
					if (dest == 0xF0) {
						killTrack(i);
						again = 0;
						break;
					}
					if (dest < addr) {
						tr[i].everJump = 1;
						tr[i].backJumps++;
						if (tr[i].backJumps == 1) {
							// LookupDestTick inlined
							uint32_t destTick = 0xFFFFFFFFu;
							{
								const uint64_t lo = ((uint64_t)(unsigned)i) << 32;
								for (int fi = 0; fi < s_firstCount; fi++) {
									if (s_first[fi].key == (lo | dest)) {
										destTick = s_first[fi].tick;
										break;
									}
								}
								if (destTick == 0xFFFFFFFFu) {
									uint32_t bestA = 0xFFFFFFFFu, bestT = 0xFFFFFFFFu;
									for (int fi = 0; fi < s_firstCount; fi++) {
										if ((s_first[fi].key >> 32) != (uint64_t)(unsigned)i) continue;
										const uint32_t a = (uint32_t)s_first[fi].key;
										if (a >= dest && (a - dest) < 16 && a < bestA) {
											bestA = a;
											bestT = s_first[fi].tick;
										}
									}
									if (bestT != 0xFFFFFFFFu)
										destTick = bestT;
								}
								if (destTick == 0xFFFFFFFFu) {
									for (int d = 1; d <= 3; d++) {
										const uint64_t kPlus = lo | (dest + (uint32_t)d);
										for (int fi = 0; fi < s_firstCount; fi++) {
											if (s_first[fi].key == kPlus) {
												destTick = s_first[fi].tick;
												break;
											}
										}
										if (destTick != 0xFFFFFFFFu) break;
										if (dest >= (uint32_t)d) {
											const uint64_t kMinus = lo | (dest - (uint32_t)d);
											for (int fi = 0; fi < s_firstCount; fi++) {
												if (s_first[fi].key == kMinus) {
													destTick = s_first[fi].tick;
													break;
												}
											}
											if (destTick != 0xFFFFFFFFu) break;
										}
									}
								}
							}
							tr[i].loopStartTick = destTick;
							tr[i].loopEndTick = tick;
							if (tick > gLoopEnd) gLoopEnd = tick;
						}
						if (tr[i].note) {
							PushShort(tick, port, (uint8_t)(0x80 | ch), (uint8_t)tr[i].note, 0);
							chOn[port ? 1 : 0][ch][tr[i].note & 127] = 0;
							tr[i].note = 0;
						}
						if (stopLoopers && gLoopEnd > 0 && tick >= gLoopEnd) {
							tr[i].alive = 0;
							again = 0;
							break;
						}
					}
					tr[i].addr = dest;
					break;
				}
				case 11:
					PushShort(tick, port, (uint8_t)(0xE0 | ch), b1, b2);
					tr[i].addr = addr + 3;
					break;
				case 12:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, (flg88 == 1) ? b2 : b1);
					tr[i].addr = addr + 3;
					break;
				case 13: { // LAREV: EXCLOUT + 10 00 01 b1 b2 b3 + EXCLEND
					const uint8_t body[6] = { 0x10, 0x00, 0x01, b1, b2, b3 };
					PushExclBody(tick, port, (flg88 == 2) ? 1 : 0, 0x42, body, 6);
					tr[i].addr = addr + 4;
					break;
				}
				case 14: { // GS/XG reverb
					if (flg88 == 2) {
						uint8_t msb = 2, lsb = 0;
						switch (b1) {
						case 0: msb = 2; lsb = 0; break;
						case 1: msb = 2; lsb = 1; break;
						case 2: msb = 2; lsb = 2; break;
						case 3: msb = 1; lsb = 0; break;
						case 4: msb = 1; lsb = 1; break;
						case 5: msb = 4; lsb = 0; break;
						default: msb = 0; lsb = 0; break;
						}
						{
							uint8_t sx[10] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x00, msb, lsb, 0xF7 };
							PushEv(tick, port, sx, 10);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x02, b2, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x04, b3, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
					} else if (!isGm) {
						PushGs(tick, port, 0x40, 0x01, 0x30, b1);
						PushGs(tick, port, 0x40, 0x01, 0x33, b2);
						PushGs(tick, port, 0x40, 0x01, 0x34, b3);
					}
					tr[i].addr = addr + 4;
					break;
				}
				case 15: {
					if (flg88 == 2) {
						uint8_t msb = 41, lsb = 0;
						switch (b1) {
						case 0: msb = 41; lsb = 0; break;
						case 1: msb = 41; lsb = 1; break;
						case 2: msb = 41; lsb = 2; break;
						case 3: msb = 41; lsb = 3; break;
						case 4: msb = 42; lsb = 0; break;
						case 5: msb = 43; lsb = 0; break;
						case 6: msb = 44; lsb = 0; break;
						case 7: msb = 48; lsb = 0; break;
						}
						{
							uint8_t sx[10] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x20, msb, lsb, 0xF7 };
							PushEv(tick, port, sx, 10);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x22, b2, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x25, b3, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
					} else if (!isGm) {
						PushGs(tick, port, 0x40, 0x01, 0x38, b1);
						PushGs(tick, port, 0x40, 0x01, 0x3A, b2);
						PushGs(tick, port, 0x40, 0x01, 0x3E, b3);
					}
					tr[i].addr = addr + 4;
					break;
				}
				case 16: // WHAT1: 原版は内部用スタブ（MIDI 出力なし）
					tr[i].addr = addr + 3;
					break;
				case 17:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x5B, b1);
					tr[i].addr = addr + 3;
					break;
				case 18:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x5D, b1);
					tr[i].addr = addr + 3;
					break;
				case 19:
					tr[i].vel = b1;
					tr[i].addr = addr + 3;
					break;
				case 20:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0x7F);
					tr[i].pedal = 1;
					tr[i].addr = addr + 3;
					break;
				case 21:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0x00);
					tr[i].pedal = 0;
					tr[i].addr = addr + 3;
					break;
				case 22: // WHAT2: 原版は内部ワーク書き込みのみ（MIDI 出力なし）
					tr[i].addr = addr + 3;
					break;
				case 23:
					if (tr[i].loopSp < MidiTrackState::MIDI_LOOP_NEST)
						tr[i].loopStack[tr[i].loopSp++] = b1;
					else
						tr[i].loopStack[MidiTrackState::MIDI_LOOP_NEST - 1] = b1;
					tr[i].addr = addr + 3;
					break;
				case 24: {
					uint32_t nxt = 0;
					const uint32_t dest = ReadJump(song, addr, ver, &nxt);
					tr[i].loopSafety++;
					if (tr[i].loopSp <= 0 || tr[i].loopSafety > 4096) {
						tr[i].addr = nxt;
						break;
					}
					int* lp = &tr[i].loopStack[tr[i].loopSp - 1];
					(*lp)--;
					if (*lp == 0) {
						tr[i].loopSp--;
						tr[i].addr = nxt;
					} else {
						tr[i].addr = dest;
					}
					break;
				}
				case 25:
					tr[i].addr = addr + 3;
					break;
				case 26: // PICH2 → WHAT4  fall-through（原版どおり一体）
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x65, 0);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x64, 0);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, 0);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, b1);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x26, 0);
					tr[i].addr = addr + 3;
					break;
				case 27: // WHAT4 (PICH2 続き): CC6 + RPN null
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, b1);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x26, 0);
					tr[i].addr = addr + 3;
					break;
				case 28: { // TITL: GS LCD 11 文字 (M_MUSIC+0C0)
					uint8_t body[14];
					body[0] = 0x10;
					body[1] = 0;
					body[2] = 0;
					for (int k = 0; k < 11; k++) {
						const uint32_t titOff = 0xC0u + (uint32_t)k;
						body[3 + k] = SasamiOffOk(song, titOff, 1) ? SasamiGet(song, titOff) : 0;
					}
					PushExclBody(tick, port, 0, 0x45, body, 14);
					tr[i].addr = addr + 3;
					break;
				}
				case 29: // KAKU1
					tr[i].drum = b1;
					if (flg88 == 2) {
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
						tr[i].drum = 0;
					} else if (!isGm) {
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x15, b1);
					}
					tr[i].addr = addr + 3;
					break;
				case 30: // KAKU2
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x30, b1);
					tr[i].addr = addr + 3;
					break;
				case 31: // KAKU3
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x36, b1);
					tr[i].addr = addr + 3;
					break;
				case 32: // KAKU4
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x32, b1);
					tr[i].addr = addr + 3;
					break;
				case 33: // KAKU5
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x33, b1);
					tr[i].addr = addr + 3;
					break;
				case 34:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 11, b1);
					tr[i].addr = addr + 3;
					break;
				case 35: // MD5588: 原版は XG のみ CC32/CC0
					if (flg88 == 2) {
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, (uint8_t)(b1 + 1));
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, 0);
					}
					tr[i].addr = addr + 3;
					break;
				case 36: {
					uint8_t buf[128];
					int n = 0;
					uint32_t p = addr + 1;
					if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xF0) {
						while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && n < 127) {
							buf[n++] = SasamiGet(song, p++);
							if (buf[n - 1] == 0xF7) break;
						}
						if (n > 0 && buf[n - 1] != 0xF7 && n < 128)
							buf[n++] = 0xF7;
					} else {
						int sum = 0;
						buf[n++] = 0xF0; buf[n++] = 0x41; buf[n++] = 0x10; buf[n++] = 0x42; buf[n++] = 0x12;
						while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && n < 120) {
							buf[n] = SasamiGet(song, p);
							sum += buf[n];
							n++;
							p++;
						}
						buf[n++] = (uint8_t)((128 - (sum % 128)) & 0x7F);
						buf[n++] = 0xF7;
					}
					PushEv(tick, port, buf, n);
					tr[i].addr = p + 1;
					break;
				}
				case 37:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x63, b1);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x62, b2);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, b3);
					tr[i].addr = addr + 4;
					break;
				case 38:
					ensurePort1();
					tr[i].port = 1;
					tr[i].addr = addr + 3;
					break;
				case 39:
					if (b1 == flg88 || (b1 == 3 && flg88 == 0 && isGm)) {
						killTrack(i);
						again = 0;
					} else {
						tr[i].addr = addr + 3;
					}
					break;
				case 40: {
					uint32_t p = addr + 1;
					int match = 0;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFE) {
						const uint8_t tag = SasamiGet(song, p);
						if (tag == (uint8_t)flg88) match = 1;
						else if (tag == 3 && flg88 == 0 && isGm) match = 1;
						p++;
					}
					if (!match) {
						while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF) p++;
						tr[i].addr = p + 1;
						break;
					}
					p++;
					uint8_t buf[128];
					int n = 0;
					int sum = 0;
					int sumOn = 0;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFE && n < 110) {
						buf[n++] = SasamiGet(song, p);
						p++;
					}
					if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xFE) p++;
					sumOn = 1;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && n < 120) {
						buf[n] = SasamiGet(song, p);
						if (sumOn) sum += buf[n];
						n++;
						p++;
					}
					if (flg88 != 2)
						buf[n++] = (uint8_t)((128 - (sum % 128)) & 0x7F);
					buf[n++] = 0xF7;
					if (n > 0 && buf[0] != 0xF0) {
						uint8_t withF0[130];
						withF0[0] = 0xF0;
						memcpy(withF0 + 1, buf, (size_t)n);
						PushEv(tick, port, withF0, n + 1);
					} else {
						PushEv(tick, port, buf, n);
					}
					tr[i].addr = p + 1;
					break;
				}
				case 41:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), b1, b2);
					tr[i].addr = addr + 3;
					break;
				case 42:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xC0 | ch), b1, 0);
					if (isGm && song.versionWord >= 50000)
						PushShort(tick, port, (uint8_t)(0xC0 | ch), b2, 0);
					tr[i].addr = addr + 3;
					break;
				case 43:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, b1);
					if (isGm && song.versionWord >= 50000)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b2);
					tr[i].addr = addr + 3;
					break;
				case 44:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, b1);
					if (isGm && song.versionWord >= 50000)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, b2);
					tr[i].addr = addr + 3;
					break;
				case 45:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, b1);
					else if (isGm)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, b2);
					tr[i].addr = addr + 3;
					break;
				default:
					if (cmd == 0) {
						killTrack(i);
						again = 0;
					} else {
						tr[i].addr = addr + 3;
					}
					break;
				}
				if (!again) break;
			}
		}
		if (!any) break;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (tr[i].alive && tr[i].count > 0) tr[i].count--;
		}
		tick++;
		int finiteAlive = 0, loopAlive = 0;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (!tr[i].alive) continue;
			if (tr[i].everJump) loopAlive++;
			else finiteAlive++;
		}
		if (finiteAlive == 0 && loopAlive > 0)
			stopLoopers = 1;
	}

	gLoopStart = 0xFFFFFFFFu;
	gLoopEnd = 0;
	for (int i = 0; i < song.trackCount && i < 64; i++) {
		if (!tr[i].everJump || tr[i].loopEndTick == 0) continue;
		if (tr[i].loopEndTick > gLoopEnd) gLoopEnd = tr[i].loopEndTick;
	}
	auto pickDest = [&](int longestOnly, int allowZero) -> uint32_t {
		uint32_t best = 0xFFFFFFFFu;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (!tr[i].everJump || tr[i].loopEndTick == 0) continue;
			if (longestOnly && tr[i].loopEndTick != gLoopEnd) continue;
			const uint32_t st = tr[i].loopStartTick;
			if (st == 0xFFFFFFFFu) continue;
			if (!allowZero && st == 0) continue;
			if (st < best) best = st;
		}
		return best;
	};
	gLoopStart = pickDest(1, 0);
	if (gLoopStart == 0xFFFFFFFFu)
		gLoopStart = pickDest(0, 0);
	if (gLoopStart == 0xFFFFFFFFu)
		gLoopStart = pickDest(1, 1);

	const int haveLoop = (gLoopStart != 0xFFFFFFFFu && gLoopEnd > gLoopStart) ? 1 : 0;
	if (haveLoop) {
		int keepN = 0;
		for (int i = 0; i < s_evCount; i++) {
			const MidiEv& e = s_evs[i];
			int take = 0;
			if (e.tick < gLoopEnd)
				take = 1;
			else if (e.tick == gLoopEnd && e.len > 0) {
				const uint8_t st = e.bytes[0];
				const int type = st & 0xF0;
				if (type == 0x90) {
					const uint8_t v = (e.len > 2) ? e.bytes[2] : 0;
					if (!v) take = 1;
				} else
					take = 1;
			}
			if (take) {
				if (keepN != i) s_evs[keepN] = e;
				keepN++;
			}
		}
		s_evCount = keepN;

		uint8_t holdNote[2][16][128];
		int holdPed[2][16];
		memset(holdNote, 0, sizeof(holdNote));
		memset(holdPed, 0, sizeof(holdPed));
		for (int i = 0; i < s_evCount; i++) {
			const MidiEv& e = s_evs[i];
			if (e.len == 0) continue;
			const uint8_t st = e.bytes[0];
			const int p = e.port ? 1 : 0;
			const int ch = st & 0x0F;
			const int type = st & 0xF0;
			if (type == 0x90 || type == 0x80) {
				const uint8_t n = (e.len > 1) ? e.bytes[1] : 0;
				if (n >= 128) continue;
				if (type == 0x80)
					holdNote[p][ch][n] = 0;
				else {
					const uint8_t v = (e.len > 2) ? e.bytes[2] : 0;
					holdNote[p][ch][n] = v ? 1 : 0;
				}
			} else if (type == 0xB0 && e.len >= 3 && e.bytes[1] == 0x40) {
				holdPed[p][ch] = e.bytes[2];
			}
		}
		for (int p = 0; p < 2; p++) {
			for (int ch = 0; ch < 16; ch++) {
				for (int n = 0; n < 128; n++) {
					if (holdNote[p][ch][n])
						PushShort(gLoopEnd, p, (uint8_t)(0x80 | ch), (uint8_t)n, 0);
				}
				if (holdPed[p][ch])
					PushShort(gLoopEnd, p, (uint8_t)(0xB0 | ch), 0x40, 0);
			}
		}
		{
			static const char kLoopStart[] = "loopStart";
			const int n = (int)(sizeof(kLoopStart) - 1);
			uint8_t d[128];
			d[0] = 0xFF; d[1] = 0x06; d[2] = (uint8_t)n;
			memcpy(d + 3, kLoopStart, (size_t)n);
			PushEv(gLoopStart, 0, d, 3 + n);
		}
		{
			static const char kLoopEnd[] = "loopEnd";
			const int n = (int)(sizeof(kLoopEnd) - 1);
			uint8_t d[128];
			d[0] = 0xFF; d[1] = 0x06; d[2] = (uint8_t)n;
			memcpy(d + 3, kLoopEnd, (size_t)n);
			PushEv(gLoopEnd, 0, d, 3 + n);
		}
		PushShort(gLoopStart, 0, 0xB0, 111, 0);
		PushShort(gLoopEnd, 0, 0xB0, 111, 127);
	} else {
		for (int i = 0; i < song.trackCount && i < 64; i++)
			killTrack(i);
		for (int p = 0; p < nports; p++) {
			for (int ch = 0; ch < 16; ch++) {
				PushShort(tick, p, (uint8_t)(0xB0 | ch), 0x40, 0);
				PushShort(tick, p, (uint8_t)(0xB0 | ch), 0x7B, 0);
			}
		}
	}

	for (int i = 0; i < s_evCount; i++) {
		if (s_evs[i].port >= nports) nports = s_evs[i].port + 1;
	}

	if (SasamiMisaoActive(song)) {
		enum { kMisaoEvMax = 8192 };
		static SasamiMisaoEv s_misaoEv[kMisaoEvMax];
		unsigned misaoTicks = 0;
		const int nMisao = SasamiMisaoBuildEvents(song, s_misaoEv, kMisaoEvMax, &misaoTicks);
		for (int i = 0; i < nMisao; i++) {
			const SasamiMisaoEv& me = s_misaoEv[i];
			PushEv(me.tick, me.port, me.bytes, me.len);
			if (me.port + 1 > nports) nports = me.port + 1;
		}
		(void)misaoTicks;
	}

	WriteSmf(nports, out, outCap, outSize);
	return *outSize > 22;
}

int SasamiPathIsMidi(const wchar_t* path)
{
	return SasamiExtIsMidi(path) ? 1 : 0;
}

int SasamiPathIsFm(const wchar_t* path)
{
	return SasamiExtIsFm(path) ? 1 : 0;
}

static void SasamiTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	wchar_t dir[MAX_PATH];
	_snwprintf_s(dir, _TRUNCATE, L"%sogg_kbsasami", tmp);
	CreateDirectoryW(dir, NULL);
	const wchar_t* name = src ? src : L"";
	for (const wchar_t* p = name; *p; p++) {
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	}
	wchar_t stem[MAX_PATH];
	wcsncpy_s(stem, name, _TRUNCATE);
	wchar_t* dot = wcsrchr(stem, L'.');
	if (dot && dot != stem)
		*dot = 0;
	if (!stem[0])
		wcsncpy_s(stem, L"sasami", _TRUNCATE);
	_snwprintf_s(dest, destChars, _TRUNCATE, L"%s\\%s.mid", dir, stem);
}

int SasamiConvertPathToMidiFile(const wchar_t* src, wchar_t* dest, int destChars)
{
	if (!src || !dest || destChars < 8) return 0;
	dest[0] = 0;
	if (!SasamiExtIsMidi(src)) return 0;
	if (!EnsureMidiWork()) return 0;
	SasamiTempMidiPath(src, dest, destChars);
	const int force = SasamiResolveMapForceW(src, SasamiRegistryMapDefault());
	if (SasamiTempCacheValid(src, force, dest))
		return 1;
	static SasamiSong s_song;
	if (!SasamiLoadFileW(src, &s_song)) return 0;
	SasamiMidiMap map = SASAMI_MAP_GS88;
	int gsLsb = 2;
	SasamiMapForceToSel(force, &map, &gsLsb);
	int sz = 0;
	if (!SasamiConvertToSmf(s_song, map, gsLsb, s_smfWork, SASAMI_MAX_SMF, &sz)) return 0;
	HANDLE h = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD w = 0;
	const BOOL ok = WriteFile(h, s_smfWork, (DWORD)sz, &w, NULL);
	CloseHandle(h);
	if (!ok || (int)w != sz) return 0;
	{
		FILETIME wt;
		DWORD fsz = 0;
		if (SasamiReadSourceStamp(src, &wt, &fsz)) {
			wcsncpy_s(s_tempCache.src, src, _TRUNCATE);
			s_tempCache.srcWrite = wt;
			s_tempCache.srcSize = fsz;
			s_tempCache.mapForce = force;
		} else {
			memset(&s_tempCache, 0, sizeof(s_tempCache));
		}
	}
	return 1;
}
