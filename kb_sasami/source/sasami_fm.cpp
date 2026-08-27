#include "sasami_fm.h"

#include "ymfm.h"
#include "ymfm_opn.h"

#include <windows.h>
#include <algorithm>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <mutex>

#include "sasami_neiro.inc"

static const uint32_t kOpnaClock = 7987200;
static const uint32_t kTickDen = 1248000;
static const unsigned kDefaultT = 13000;
static const uint32_t kMaxTicks = 200000;
// ymfm FM: data * fmvolume / 32768. Default 65536 is 2x and clips with SSG+rhythm.
// Unity FM/ADPCM-A = 32768. FM 1.5x. SSG at 50%. WAV drums halved to sit with FM 1.0.
static const int32_t kFmVolume = 49152; // 32768 * 1.5
static const int32_t kPsgVolume = 32768; // 65536 * 50/100
static const int kRhythmWavNum = 1;
static const int kRhythmWavDen = 2;
static const int kMasterPct = 60;

static const uint16_t kFmHz[16] = {
	0x026A, 0x028F, 0x02B6, 0x02DF,
	0x030B, 0x0339, 0x036A, 0x039E,
	0x03D5, 0x0410, 0x044E, 0x048F,
	0, 0, 0, 0
};
static const uint16_t kPsgHz[16] = {
	0x0EE8, 0x0E12, 0x0D48, 0x0C88,
	0x0BD4, 0x0B2A, 0x0A8A, 0x09F2,
	0x0964, 0x08DC, 0x085E, 0x07E6,
	0, 0, 0, 0
};
static const uint8_t kRzmDef[6] = { 0xDC, 0xDC, 0x5F, 0x90, 0xDE, 0x99 };
static const wchar_t* kRhythmNames[6] = { L"BD", L"SD", L"TOP", L"HH", L"TOM", L"RIM" };

enum { FM_TLENTS = 128, FM_TLPOS = 32 };

static int32_t g_tltable[FM_TLENTS + FM_TLPOS];
static int g_tlReady = 0;

static void MakeTlTable()
{
	if (g_tlReady) return;
	for (int i = -FM_TLPOS; i < FM_TLENTS; i++)
		g_tltable[i + FM_TLPOS] = (int32_t)(65536.0 * pow(2.0, i * -16.0 / (double)FM_TLENTS)) - 1;
	g_tlReady = 1;
}

static int16_t Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int16_t)v;
}

enum { FM_ADPCM_ROM = 256 * 1024, FM_RHYTHM_MAX = 65536, FM_LOAD_MAX = 2 * 1024 * 1024 };

static uint8_t s_loadRaw[FM_LOAD_MAX];

struct RhythmVoice {
	int16_t sample[FM_RHYTHM_MAX];
	uint32_t sampleLen;
	uint32_t rate;
	uint32_t pos;
	uint32_t size;
	uint32_t step;
	int pan;
	int level;
};

static int LoadBinFixed(const wchar_t* path, uint8_t* out, int outCap, int* outSize)
{
	if (!out || outCap <= 0 || !outSize) return 0;
	*outSize = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > outCap) {
		CloseHandle(h);
		return 0;
	}
	DWORD n = 0;
	const BOOL ok = ReadFile(h, out, (DWORD)li.QuadPart, &n, NULL);
	CloseHandle(h);
	if (!ok || n == 0) return 0;
	*outSize = (int)n;
	return 1;
}

static int LoadWavMono16Fixed(const wchar_t* path, int16_t* pcm, int pcmCap, uint32_t* outLen, uint32_t* rate)
{
	if (!pcm || pcmCap <= 0 || !outLen || !rate) return 0;
	*outLen = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart < 44 || li.QuadPart > FM_LOAD_MAX) {
		CloseHandle(h);
		return 0;
	}
	DWORD n = 0;
	if (!ReadFile(h, s_loadRaw, (DWORD)li.QuadPart, &n, NULL)) {
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	if (n < 44) return 0;
	if (memcmp(s_loadRaw, "RIFF", 4) != 0 || memcmp(s_loadRaw + 8, "WAVE", 4) != 0) return 0;
	size_t p = 12;
	int fmtOk = 0;
	uint16_t tag = 0, nch = 0, bits = 0;
	uint32_t sr = 0;
	while (p + 8 <= n) {
		char id[5] = { 0 };
		memcpy(id, &s_loadRaw[p], 4);
		const uint32_t sz = s_loadRaw[p + 4] | ((uint32_t)s_loadRaw[p + 5] << 8) | ((uint32_t)s_loadRaw[p + 6] << 16) | ((uint32_t)s_loadRaw[p + 7] << 24);
		p += 8;
		if (p + sz > n && memcmp(id, "data", 4) != 0) break;
		if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
			tag = (uint16_t)(s_loadRaw[p] | (s_loadRaw[p + 1] << 8));
			nch = (uint16_t)(s_loadRaw[p + 2] | (s_loadRaw[p + 3] << 8));
			sr = s_loadRaw[p + 4] | ((uint32_t)s_loadRaw[p + 5] << 8) | ((uint32_t)s_loadRaw[p + 6] << 16) | ((uint32_t)s_loadRaw[p + 7] << 24);
			bits = (uint16_t)(s_loadRaw[p + 14] | (s_loadRaw[p + 15] << 8));
			fmtOk = 1;
		} else if (memcmp(id, "data", 4) == 0) {
			if (!fmtOk || tag != 1 || nch != 1 || bits != 16 || sr < 4000) return 0;
			const size_t ns = sz / 2;
			if (ns == 0 || (int)ns > pcmCap) return 0;
			memcpy(pcm, &s_loadRaw[p], ns * 2);
			*outLen = (uint32_t)ns;
			*rate = sr;
			return 1;
		}
		p += sz;
		if (sz & 1) p++;
	}
	return 0;
}

struct SasamiFmPlayer::Impl : public ymfm::ymfm_interface {
	ymfm::ym2608 chip;
	SasamiSong song;
	uint8_t adpcmRom[FM_ADPCM_ROM];
	int adpcmRomSize;
	uint8_t ssg[16];
	uint8_t rzm[6];
	uint8_t lrWk[16];
	int16_t detune[12];
	uint16_t pitch[12];
	uint8_t waitb[12];
	uint8_t loopCnt[12];
	uint8_t vol[12];
	uint32_t pc[12];
	uint32_t voiceOff[12];
	uint8_t voiceSrc[12];
	int alive[12];
	int backJumps[12];
	int chCount;
	int fm10;
	int measureLen;
	int eofSent;
	unsigned T;
	uint8_t ssgOn[3];
	uint32_t hostRate;
	uint32_t chipRate;
	uint64_t tickCarry;
	uint32_t samplesLeftInTick;
	int64_t chipAcc;
	int32_t curL, curR;
	uint32_t ticksPlayed;
	uint32_t totalTicks;
	int ended;
	RhythmVoice rhythm[6];
	int rhythmHaveWav;
	int rhythmtl;
	uint8_t rhythmkey;

	Impl() : chip(*this)
	{
		memset(ssg, 0, sizeof(ssg));
		ssg[7] = 0xBF;
		memset(rzm, 0, sizeof(rzm));
		memset(lrWk, 0, sizeof(lrWk));
		memset(detune, 0, sizeof(detune));
		memset(pitch, 0, sizeof(pitch));
		memset(waitb, 0, sizeof(waitb));
		memset(loopCnt, 0, sizeof(loopCnt));
		memset(vol, 0, sizeof(vol));
		memset(pc, 0, sizeof(pc));
		memset(voiceOff, 0, sizeof(voiceOff));
		memset(voiceSrc, 0, sizeof(voiceSrc));
		memset(alive, 0, sizeof(alive));
		memset(backJumps, 0, sizeof(backJumps));
		memset(ssgOn, 0, sizeof(ssgOn));
		chCount = 6;
		fm10 = 0;
		measureLen = 0;
		eofSent = 0;
		T = kDefaultT;
		hostRate = 44100;
		chipRate = 0;
		tickCarry = 0;
		samplesLeftInTick = 0;
		chipAcc = 0;
		curL = curR = 0;
		ticksPlayed = 0;
		totalTicks = 0;
		ended = 0;
		rhythmHaveWav = 0;
		rhythmtl = 0;
		rhythmkey = 0;
		adpcmRomSize = 0;
		for (int i = 0; i < 6; i++) {
			rhythm[i].sampleLen = 0;
			rhythm[i].rate = 8000;
			rhythm[i].pos = ~0u;
			rhythm[i].size = 0;
			rhythm[i].step = 0;
			rhythm[i].pan = 3;
			rhythm[i].level = 0;
		}
	}

	uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
	{
		if (type == ymfm::ACCESS_ADPCM_A && (int)address < adpcmRomSize)
			return adpcmRom[address];
		return 0;
	}

	void RhythmReg(uint8_t reg, uint8_t data)
	{
		if (!rhythmHaveWav) return;
		if (reg == 0x10) {
			if (!(data & 0x80)) {
				rhythmkey |= (uint8_t)(data & 0x3F);
				for (int i = 0; i < 6; i++)
					if (data & (1 << i)) rhythm[i].pos = 0;
			} else {
				rhythmkey = (uint8_t)(rhythmkey & ~data);
			}
		} else if (reg == 0x11) {
			rhythmtl = (~data) & 63;
		} else if (reg >= 0x18 && reg <= 0x1D) {
			const int i = reg - 0x18;
			rhythm[i].pan = (data >> 6) & 3;
			rhythm[i].level = (~data) & 31;
		}
	}

	void MixRhythm(int32_t* L, int32_t* R)
	{
		if (!rhythmHaveWav || rhythm[0].sampleLen == 0 || !(rhythmkey & 0x3F)) return;
		MakeTlTable();
		for (int i = 0; i < 6; i++) {
			RhythmVoice& r = rhythm[i];
			if (!(rhythmkey & (1 << i)) || r.sampleLen == 0 || r.level >= 128) continue;
			if (r.pos >= r.size) continue;
			int db = rhythmtl + r.level;
			if (db < -31) db = -31;
			if (db > 127) db = 127;
			const int vol = g_tltable[FM_TLPOS + db] >> 4;
			const int sample = ((r.sample[r.pos / 1024] * vol) >> 12) * kRhythmWavNum / kRhythmWavDen;
			r.pos += r.step;
			if (r.pan & 2) *L += sample;
			if (r.pan & 1) *R += sample;
		}
	}

	void FmOut(uint8_t reg, uint8_t data)
	{
		chip.write(0, reg);
		chip.write(1, data);
		if (reg < 16) ssg[reg] = data;
		RhythmReg(reg, data);
	}
	void Fm2Out(uint8_t reg, uint8_t data)
	{
		chip.write(2, reg);
		chip.write(3, data);
	}

	int OpnKey(int ch)
	{
		if (ch < 3) return ch;
		if (ch >= 7 && ch <= 9) return ch - 3;
		return -1;
	}
	int IsFm2(int ch) { return ch >= 7; }
	int IsSsg(int ch) { return (ch >= 3 && ch <= 5); }
	int IsRhythm(int ch) { return ch == 6; }

	void KeyOff(int ch)
	{
		const int k = OpnKey(ch);
		if (k >= 0) FmOut(0x28, (uint8_t)k);
		if (IsSsg(ch)) {
			const int s = ch - 3;
			ssg[7] |= (uint8_t)(1 << s);
			FmOut(7, ssg[7]);
			ssgOn[s] = 0;
		}
	}

	const uint8_t* VoiceBytes(int ch) const
	{
		if (voiceSrc[ch] == 2) {
			if (voiceOff[ch] < kSasamiNeiroCs) return NULL;
			const uint32_t o = voiceOff[ch] - kSasamiNeiroCs;
			if (o + 25 > sizeof(kSasamiNeiro)) return NULL;
			return kSasamiNeiro + o;
		}
		if (voiceSrc[ch] != 1) return NULL;
		if (!SasamiOffOk(song, voiceOff[ch], 25)) return NULL;
		return &song.data[voiceOff[ch]];
	}

	void ApplyTl(int ch)
	{
		const int k = OpnKey(ch);
		if (k < 0) return;
		const uint8_t* src = VoiceBytes(ch);
		if (!src) return;
		const int alg = src[0x18] & 7;
		const uint8_t tl = vol[ch];
		const int fm2 = IsFm2(ch);
		const int slot = fm2 ? (k - 4) : k;
		auto wr = [&](uint8_t r) {
			if (fm2) Fm2Out((uint8_t)(r + slot), tl);
			else FmOut((uint8_t)(r + slot), tl);
		};
		if (alg == 7) wr(0x40);
		if (alg >= 5) wr(0x44);
		if (alg >= 4) wr(0x48);
		wr(0x4C);
	}

	void WriteFnum(int ch, uint16_t fn)
	{
		fn = (uint16_t)(fn + detune[ch]);
		pitch[ch] = fn;
		const int k = OpnKey(ch);
		if (k < 0) return;
		const uint8_t hi = (uint8_t)(fn >> 8);
		const uint8_t lo = (uint8_t)fn;
		if (IsFm2(ch)) {
			const int slot = k - 4;
			Fm2Out((uint8_t)(0xA4 + slot), hi);
			Fm2Out((uint8_t)(0xA0 + slot), lo);
		} else {
			FmOut((uint8_t)(0xA4 + k), hi);
			FmOut((uint8_t)(0xA0 + k), lo);
		}
	}

	void LoadVoice(int ch, uint16_t raw)
	{
		const int k = OpnKey(ch);
		if (k < 0) return;
		const uint8_t* src = NULL;
		if (raw >= 0x1000) {
			const uint32_t off = (uint32_t)raw - 0x1000;
			if (!SasamiOffOk(song, off, 25)) return;
			src = &song.data[off];
			voiceOff[ch] = off;
			voiceSrc[ch] = 1;
		} else if (raw >= kSasamiNeiroCs) {
			const uint32_t o = (uint32_t)raw - kSasamiNeiroCs;
			if (o + 25 > sizeof(kSasamiNeiro)) return;
			src = kSasamiNeiro + o;
			voiceOff[ch] = raw;
			voiceSrc[ch] = 2;
		} else
			return;
		const int fm2 = IsFm2(ch);
		const int slot = fm2 ? (k - 4) : k;
		auto wr = [&](uint8_t r, uint8_t v) {
			if (fm2) Fm2Out(r, v); else FmOut(r, v);
		};
		uint8_t rr = (uint8_t)(0x80 + slot);
		for (int i = 0; i < 4; i++) {
			wr(rr, 15);
			rr = (uint8_t)(rr + 4);
		}
		for (int i = 0; i < 4; i++) {
			wr(rr, 0);
			rr = (uint8_t)(rr + 4);
		}
		KeyOff(ch);
		uint8_t r = (uint8_t)(0x30 + slot);
		for (int i = 0; i < 24; i++) {
			wr(r, src[i]);
			r = (uint8_t)(r + 4);
		}
		wr((uint8_t)(0xB0 + slot), src[24]);
		ApplyTl(ch);
	}

	int JumpTo(int ch, uint32_t addr, uint16_t w1)
	{
		uint32_t dest = w1;
		if (dest >= 0x1000) dest -= 0x1000;
		if (dest == 0xF0) {
			alive[ch] = 0;
			return 0;
		}
		if (measureLen && dest < addr) {
			backJumps[ch]++;
			if (backJumps[ch] >= 2) {
				alive[ch] = 0;
				return 0;
			}
		}
		pc[ch] = dest;
		return 1;
	}

	void NoteFm(int ch, uint8_t note, uint8_t wait, int keyOn)
	{
		waitb[ch] = wait;
		const int k = OpnKey(ch);
		if (k < 0) return;
		const int nidx = note & 0x0F;
		const uint8_t ah = (uint8_t)((note & 0xF0) >> 1);
		const uint16_t raw = kFmHz[nidx & 15];
		const uint8_t dh = (uint8_t)((uint8_t)(raw >> 8) | ah);
		const uint8_t dl = (uint8_t)raw;
		const uint16_t fn = (uint16_t)((dh << 8) | dl);
		if (keyOn) {
			FmOut(0x28, (uint8_t)k);
			ClockFm();
			const uint8_t saved = vol[ch];
			vol[ch] = 126;
			ApplyTl(ch);
			WriteFnum(ch, fn);
			vol[ch] = saved;
			ApplyTl(ch);
			FmOut(0x28, (uint8_t)(0xF0 | k));
		} else {
			WriteFnum(ch, fn);
		}
	}

	void NoteSsg(int ch, uint8_t note, uint8_t wait)
	{
		waitb[ch] = wait;
		int s = ch - 3;
		if (ch == 10) s = 2;
		const int nidx = note & 0x0F;
		int oct = (note >> 4) & 0x0F;
		if (oct == 0) oct = 1;
		uint16_t per = kPsgHz[nidx & 15];
		const int sh = oct - 1;
		if (sh > 0 && sh < 16) per = (uint16_t)(per >> sh);
		per = (uint16_t)(per + detune[ch]);
		pitch[ch] = per;
		FmOut((uint8_t)(s * 2), (uint8_t)per);
		FmOut((uint8_t)(s * 2 + 1), (uint8_t)(per >> 8));
		ssg[7] = (uint8_t)(ssg[7] & ~(1 << s));
		FmOut(7, ssg[7]);
		ssgOn[s] = 1;
	}

	int ExecCmd(int ch)
	{
		uint32_t addr = pc[ch];
		if (!SasamiOffOk(song, addr, 1) || addr == 0xF0) {
			alive[ch] = 0;
			return 0;
		}
		const int cmd = song.data[addr];
		const uint8_t b1 = SasamiGet(song, addr + 1);
		const uint8_t b2 = SasamiGet(song, addr + 2);
		const uint16_t w1 = SasamiGet16(song, addr + 1);

		switch (cmd) {
		case 0: { // FNOTE
			if (IsSsg(ch)) NoteSsg(ch, b1, b2);
			else if (IsRhythm(ch)) { waitb[ch] = b2; }
			else NoteFm(ch, b1, b2, 1);
			pc[ch] = addr + 3;
			return 0;
		}
		case 1: { // FKYU rest
			KeyOff(ch);
			waitb[ch] = b2;
			pc[ch] = addr + 3;
			return 0;
		}
		case 2: { // FNEIRO
			LoadVoice(ch, w1);
			pc[ch] = addr + 3;
			return 1;
		}
		case 3: // FJUMP
			return JumpTo(ch, addr, w1);
		case 4: { // PSGVOL
			int r = ch;
			if (ch != 10) r = ch + 5;
			FmOut((uint8_t)r, b1);
			pc[ch] = addr + 3;
			return 1;
		}
		case 5: // NEXTF
			pc[ch] = addr + 3;
			return 0;
		case 6: { // PSGAND
			const uint8_t reg = b1;
			uint8_t v = (reg < 16) ? ssg[reg] : 0;
			v = (uint8_t)(v & b2);
			FmOut(reg, v);
			pc[ch] = addr + 3;
			return 1;
		}
		case 7: { // PSGOR
			const uint8_t reg = b1;
			uint8_t v = (reg < 16) ? ssg[reg] : 0;
			v = (uint8_t)(v | b2);
			FmOut(reg, v);
			pc[ch] = addr + 3;
			return 1;
		}
		case 8: { // PSGOUT  AX=[DI+1] XCHG → AH=reg AL=data... wait
			// MOV AX,[DI+1]; XCHG AH,AL; CALL FMOUT  → AL=data originally AH, AH=reg originally AL
			// word at +1 is little-endian: low=b1, high=b2. AX=b1|(b2<<8), XCHG → AL=b2, AH=b1
			FmOut(b1, b2);
			pc[ch] = addr + 3;
			return 1;
		}
		case 9: { // FTMPO
			unsigned t = w1;
			if (t == 0) t = kDefaultT;
			T = t;
			pc[ch] = addr + 3;
			return 1;
		}
		case 10: // FSLR wait
			waitb[ch] = b2;
			pc[ch] = addr + 3;
			return 0;
		case 11: // FVOL: operand is YM TL (0=loud … 127=silent). FMSSGVOL only uses 127-b1.
			vol[ch] = b1;
			ApplyTl(ch);
			pc[ch] = addr + 3;
			return 1;
		case 12: { // FPICH: [DI+1]=A4, [DI+2]=A0 (not little-endian fnum)
			uint16_t fn = (uint16_t)((b1 << 8) | b2);
			WriteFnum(ch, fn);
			pc[ch] = addr + 3;
			return 1;
		}
		case 13:
			loopCnt[ch] = b1;
			pc[ch] = addr + 3;
			return 1;
		case 14: {
			uint8_t c = loopCnt[ch];
			if (c) c--;
			if (c == 0) {
				pc[ch] = addr + 3;
			} else {
				loopCnt[ch] = c;
				uint32_t dest = w1;
				if (dest >= 0x1000) dest -= 0x1000;
				pc[ch] = dest;
			}
			return 1;
		}
		case 15: { // PPICH
			int s = IsSsg(ch) ? (ch - 3) : 0;
			uint16_t per = w1;
			FmOut((uint8_t)(s * 2), (uint8_t)per);
			FmOut((uint8_t)(s * 2 + 1), (uint8_t)(per >> 8));
			pc[ch] = addr + 3;
			return 1;
		}
		case 16: { // FLR stereo B4
			const int k = OpnKey(ch);
			if (k >= 0 && fm10) {
				uint8_t v = b1;
				if (IsFm2(ch)) {
					lrWk[k] = (uint8_t)((lrWk[k] & 7) | v);
					Fm2Out((uint8_t)(0xB4 + (k - 4)), lrWk[k]);
				} else {
					lrWk[k] = (uint8_t)((lrWk[k] & 7) | v);
					FmOut((uint8_t)(0xB4 + k), lrWk[k]);
				}
			}
			pc[ch] = addr + 3;
			return 1;
		}
		case 17: // F2JP
			if (fm10)
				return JumpTo(ch, addr, w1);
			pc[ch] = addr + 3;
			return 1;
		case 18:
			detune[ch] = (int16_t)(w1 - 0x8000);
			pc[ch] = addr + 3;
			return 1;
		case 19: { // frzmvl
			const int idx = b1;
			if (idx < 6) {
				rzm[idx] = (uint8_t)((rzm[idx] & 0xC0) | (b2 & 0x3F));
				FmOut((uint8_t)(0x18 + idx), rzm[idx]);
			}
			pc[ch] = addr + 3;
			return 1;
		}
		case 20: {
			const int idx = b1;
			if (idx < 6) {
				rzm[idx] = (uint8_t)((rzm[idx] & 0x3F) | ((b2 << 6) & 0xC0));
				FmOut((uint8_t)(0x18 + idx), rzm[idx]);
			}
			pc[ch] = addr + 3;
			return 1;
		}
		case 21: { // FLFO_DP AMS/PMS in B4 low
			const int k = OpnKey(ch);
			if (k >= 0) {
				if (IsFm2(ch)) {
					lrWk[k] = (uint8_t)((lrWk[k] & 0xC0) | (b1 & 0x3F));
					Fm2Out((uint8_t)(0xB4 + (k - 4)), lrWk[k]);
				} else {
					lrWk[k] = (uint8_t)((lrWk[k] & 0xC0) | (b1 & 0x3F));
					FmOut((uint8_t)(0xB4 + k), lrWk[k]);
				}
			}
			pc[ch] = addr + 3;
			return 1;
		}
		case 22:
			FmOut(0x22, b1);
			pc[ch] = addr + 3;
			return 1;
		case 23:
			pc[ch] = addr + 3;
			return 1;
		case 24: // fhokry note without key-off
			if (IsSsg(ch)) NoteSsg(ch, b1, b2);
			else if (!IsRhythm(ch)) NoteFm(ch, b1, b2, 0);
			else waitb[ch] = b2;
			pc[ch] = addr + 3;
			return 0;
		default:
			if (cmd == 0) { alive[ch] = 0; return 0; }
			pc[ch] = addr + 3;
			return 1;
		}
	}

	void TickOnce()
	{
		int any = 0;
		for (int ch = 0; ch < chCount; ch++) {
			if (ch == 6 && !fm10) continue;
			if (!alive[ch]) continue;
			any = 1;
			if (waitb[ch] >= 2) {
				waitb[ch]--;
				continue;
			}
			int guard = 0;
			while (alive[ch] && waitb[ch] < 2 && guard++ < 4096) {
				if (!ExecCmd(ch)) break;
			}
		}
		if (!any) ended = 1;
		ticksPlayed++;
		if (measureLen && ticksPlayed >= kMaxTicks) ended = 1;
	}

	void ClockFm(int outputs = 8)
	{
		// ymfm samples key-on only when FM is clocked. MED fidelity clocks
		// once per 6 output samples, so a bare keyoff+keyon write never
		// retriggers the envelope (notes decay via SR until silent).
		ymfm::ym2608::output_data o;
		for (int i = 0; i < outputs; i++)
			chip.generate(&o, 1);
	}

	void ChipSample()
	{
		ymfm::ym2608::output_data o;
		chip.generate(&o, 1);
		const int n = (int)ymfm::ym2608::OUTPUTS;
		const int32_t a = o.data[0];
		const int32_t b = o.data[1 % n];
		const int32_t c = o.data[2 % n];
		curL = a + c;
		curR = b + c;
	}

	void HostSample(int16_t* L, int16_t* R)
	{
		chipAcc += (int64_t)chipRate;
		while (chipAcc >= (int64_t)hostRate) {
			chipAcc -= (int64_t)hostRate;
			ChipSample();
		}
		int32_t l = curL, r = curR;
		MixRhythm(&l, &r);
		*L = Clamp16((int)((int64_t)l * kMasterPct / 100));
		*R = Clamp16((int)((int64_t)r * kMasterPct / 100));
	}

	void InitChip()
	{
		chip.reset();
		chip.setfmvolume(kFmVolume);
		chip.setpsgvolume(kPsgVolume);
		FmOut(0x29, 0x83);
		FmOut(0x10, 0xDF);
		FmOut(0x11, 0x3F);
		for (int i = 0; i < 6; i++) {
			rzm[i] = kRzmDef[i];
			FmOut((uint8_t)(0x18 + i), rzm[i]);
		}
		FmOut(0x28, 0x00);
		FmOut(0x28, 0x01);
		FmOut(0x28, 0x02);
		FmOut(0x07, 0xBF);
		ssg[7] = 0xBF;
		rhythmkey = 0;
		if (rhythmHaveWav) {
			for (int i = 0; i < 6; i++)
				rhythm[i].pos = rhythm[i].size;
		}
		FmOut(0x28, 0x04);
		FmOut(0x28, 0x05);
		FmOut(0x28, 0x06);
		for (int i = 0; i < 3; i++) {
			FmOut((uint8_t)(0xB4 + i), 0xC0);
			Fm2Out((uint8_t)(0xB4 + i), 0xC0);
			lrWk[i] = 0xC0;
			lrWk[i + 4] = 0xC0;
		}
		ChipSample();
	}

	void SetupSong()
	{
		fm10 = song.fmOpna10ch ? 1 : 0;
		chCount = fm10 ? 10 : 6;
		T = kDefaultT;
		ended = 0;
		eofSent = 0;
		ticksPlayed = 0;
		tickCarry = 0;
		samplesLeftInTick = 0;
		chipAcc = 0;
		for (int ch = 0; ch < 12; ch++) {
			waitb[ch] = 0;
			loopCnt[ch] = 0;
			vol[ch] = 127;
			detune[ch] = 0;
			backJumps[ch] = 0;
			alive[ch] = 0;
			pc[ch] = 0;
			voiceOff[ch] = 0;
			voiceSrc[ch] = 0;
		}
		for (int ch = 0; ch < chCount && ch < 10; ch++) {
			pc[ch] = song.tracks[ch].fileOff;
			alive[ch] = song.tracks[ch].unused ? 0 : 1;
			if (pc[ch] == 0xF0) alive[ch] = 0;
		}
		InitChip();
	}

	uint32_t CountTicks()
	{
		const int old = measureLen;
		measureLen = 1;
		SetupSong();
		while (!ended) TickOnce();
		const uint32_t n = ticksPlayed;
		measureLen = old;
		SetupSong();
		return n ? n : 1;
	}

	int TryRom(const wchar_t* path)
	{
		int n = 0;
		if (!LoadBinFixed(path, adpcmRom, FM_ADPCM_ROM, &n)) return 0;
		if (n < 0x2000) { adpcmRomSize = 0; return 0; }
		adpcmRomSize = n;
		rhythmHaveWav = 0;
		return 1;
	}

	int TryWavDir(const wchar_t* dir)
	{
		if (!dir || !dir[0]) return 0;
		for (int i = 0; i < 6; i++) {
			wchar_t path[MAX_PATH];
			_snwprintf_s(path, _TRUNCATE, L"%s\\2608_%s.WAV", dir, kRhythmNames[i]);
			uint32_t len = 0, rate = 0;
			if (!LoadWavMono16Fixed(path, rhythm[i].sample, FM_RHYTHM_MAX, &len, &rate)) {
				if (i == 5) {
					_snwprintf_s(path, _TRUNCATE, L"%s\\2608_RYM.WAV", dir);
					if (!LoadWavMono16Fixed(path, rhythm[i].sample, FM_RHYTHM_MAX, &len, &rate))
						return 0;
				} else
					return 0;
			}
			rhythm[i].sampleLen = len;
			rhythm[i].rate = rate;
			rhythm[i].size = len * 1024;
			rhythm[i].pos = rhythm[i].size;
			rhythm[i].pan = 3;
			rhythm[i].level = 0;
			rhythm[i].step = rate * 1024 / (hostRate ? hostRate : 44100);
			if (rhythm[i].step == 0) rhythm[i].step = 1;
		}
		rhythmHaveWav = 1;
		return 1;
	}

	void PrepareRhythmSteps()
	{
		if (!rhythmHaveWav) return;
		for (int i = 0; i < 6; i++) {
			rhythm[i].step = rhythm[i].rate * 1024 / (hostRate ? hostRate : 44100);
			if (rhythm[i].step == 0) rhythm[i].step = 1;
		}
	}

	void LoadRhythm(const wchar_t* dir)
	{
		adpcmRomSize = 0;
		rhythmHaveWav = 0;
		wchar_t path[MAX_PATH];
		wchar_t exeDir[MAX_PATH];
		GetModuleFileNameW(NULL, exeDir, MAX_PATH);
		wchar_t* slash = wcsrchr(exeDir, L'\\');
		if (slash) slash[1] = 0;
		else exeDir[0] = 0;

		auto tryRomName = [&](const wchar_t* folder) {
			if (!folder || !folder[0]) return 0;
			_snwprintf_s(path, _TRUNCATE, L"%s\\ym2608_adpcm_rom.bin", folder);
			if (TryRom(path)) return 1;
			_snwprintf_s(path, _TRUNCATE, L"%s\\2608_adpcm_rom.bin", folder);
			return TryRom(path);
		};
		auto tryWavNames = [&](const wchar_t* folder) {
			if (!folder || !folder[0]) return 0;
			if (TryWavDir(folder)) return 1;
			wchar_t sub[MAX_PATH];
			_snwprintf_s(sub, _TRUNCATE, L"%s\\Rhythm", folder);
			if (TryWavDir(sub)) return 1;
			_snwprintf_s(sub, _TRUNCATE, L"%s\\wav", folder);
			return TryWavDir(sub);
		};

		if (tryRomName(dir)) return;
		if (tryWavNames(dir)) return;

		if (tryRomName(exeDir)) return;
		_snwprintf_s(path, _TRUNCATE, L"%sPlugins\\kbsasami", exeDir);
		if (tryRomName(path)) return;
		if (tryWavNames(path)) return;
		_snwprintf_s(path, _TRUNCATE, L"%sPlugins\\Kobarin\\fmpmd", exeDir);
		if (tryRomName(path)) return;
		if (tryWavNames(path)) return;
		_snwprintf_s(path, _TRUNCATE, L"%sPlugins\\Mamiya\\kpis98", exeDir);
		if (tryRomName(path)) return;
		if (tryWavNames(path)) return;
	}
};

SasamiFmPlayer::SasamiFmPlayer() : m(NULL), m_hostRate(44100), m_totalSamples(0), m_curSample(0)
{
	m_title[0] = 0;
}
SasamiFmPlayer::~SasamiFmPlayer() { Close(); }

bool SasamiFmPlayer::Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* rhythmDir)
{
	Close();
	if (song.kind != SASAMI_KIND_FPY || song.dataSize == 0) return false;
	std::lock_guard<std::mutex> lk(m_lock);
	m = new Impl();
	m->song = song;
	m->hostRate = sampleRate < 8000 ? 44100 : sampleRate;
	m_hostRate = m->hostRate;
	m->LoadRhythm(rhythmDir);
	m->PrepareRhythmSteps();
	m->chip.set_fidelity(ymfm::OPN_FIDELITY_MED);
	m->chipRate = m->chip.sample_rate(kOpnaClock);
	if (m->chipRate < 8000) m->chipRate = kOpnaClock / 144;
	strncpy_s(m_title, song.titleSjis, _TRUNCATE);
	{
		m->measureLen = 1;
		m->SetupSong();
		uint64_t samples = 0;
		uint64_t carry = 0;
		uint32_t guard = 0;
		while (!m->ended && guard++ < kMaxTicks) {
			carry += (uint64_t)m->hostRate * m->T;
			samples += carry / kTickDen;
			carry %= kTickDen;
			m->TickOnce();
		}
		m->totalTicks = m->ticksPlayed;
		m_totalSamples = samples ? samples : (uint64_t)m_hostRate;
		m->measureLen = 0;
		m->SetupSong();
	}
	m_curSample = 0;
	return true;
}

void SasamiFmPlayer::Close()
{
	std::lock_guard<std::mutex> lk(m_lock);
	delete m;
	m = NULL;
	m_totalSamples = 0;
	m_curSample = 0;
}

uint32_t SasamiFmPlayer::Render(int16_t* interleavedStereo, uint32_t frames)
{
	std::lock_guard<std::mutex> lk(m_lock);
	return RenderUnlocked(interleavedStereo, frames);
}

uint32_t SasamiFmPlayer::RenderUnlocked(int16_t* interleavedStereo, uint32_t frames)
{
	if (!m || !interleavedStereo || frames == 0) return 0;
	if (m->eofSent) return 0;
	uint32_t want = frames;
	uint32_t out = 0;
	while (out < want) {
		if (m->samplesLeftInTick == 0) {
			if (m->ended) {
				memset(interleavedStereo + out * 2, 0, (size_t)(want - out) * 2 * sizeof(int16_t));
				out = want;
				m->eofSent = 1;
				break;
			}
			m->tickCarry += (uint64_t)m->hostRate * m->T;
			const uint32_t sl = (uint32_t)(m->tickCarry / kTickDen);
			m->tickCarry %= kTickDen;
			m->TickOnce();
			m->samplesLeftInTick = sl;
			if (sl == 0) continue;
		}
		uint32_t take = m->samplesLeftInTick;
		if (take > want - out) take = want - out;
		for (uint32_t i = 0; i < take; i++) {
			int16_t L, R;
			m->HostSample(&L, &R);
			interleavedStereo[(out + i) * 2] = L;
			interleavedStereo[(out + i) * 2 + 1] = R;
		}
		m->samplesLeftInTick -= take;
		out += take;
	}
	m_curSample += out;
	return out;
}

uint64_t SasamiFmPlayer::SeekSample(uint64_t sample)
{
	std::lock_guard<std::mutex> lk(m_lock);
	if (!m) return 0;
	if (m_totalSamples == 0) {
		m->SetupSong();
		m_curSample = 0;
		return 0;
	}
	sample %= m_totalSamples;
	m->SetupSong();
	m_curSample = 0;
	if (sample == 0) return 0;
	int16_t dump[1024 * 2];
	uint64_t left = sample;
	while (left) {
		uint32_t n = (uint32_t)((left > 1024) ? 1024 : left);
		uint32_t g = RenderUnlocked(dump, n);
		if (g == 0) break;
		left -= g;
	}
	return m_curSample;
}
