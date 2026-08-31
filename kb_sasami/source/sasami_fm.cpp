#include "sasami_fm.h"
#include "sasami_misao.h"
#include "sasami_fmmon.h"
#include <windows.h>

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
/* OPN モードも ym2608 を使い、SCH(0x29) を立てず FM3+SSG3 にする。
   ym2203 直結は SSG 出力が ch 分離で、ホスト側合算だとノイズがボソボソになる。
   SSG/ノイズは OPNA と同じ MixTo1 + psgvolume 経路を使う。 */
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

struct BeepVoice {
	int on;
	double phase;
	double step; // cycles per host sample
	int amp;
};

struct SasamiFmPlayer::Impl : public ymfm::ymfm_interface {
	ymfm::ym2608 chip;
	int playFmMode; // 0=BEEP 1=OPN(FM3+SSG3) 2=OPNA
	BeepVoice beep[10];
	int beepTdm; /* BEEP 時分割: 今スピーカーに出す ch（本家 WORKBP） */
	SasamiSong song;
	uint8_t adpcmRom[FM_ADPCM_ROM];
	int adpcmRomSize;
	uint8_t ssg[16];
	uint8_t rzm[6];
	uint8_t lrWk[16];
	int16_t detune[12];
	uint16_t pitchBase[12]; /* DETUNE 加算前（原版 FMSSGPITCH） */
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
	uint8_t rhythmPulsePend; /* 0x10 key-on の生ヒット（値不変の連打用） */
	uint8_t rhythmHitCnt[6]; /* 累積（UI は差分で取りこぼし防止） */
	uint8_t keyOnHitCnt[6];
	uint8_t ssgHitCnt[3];
	uint8_t regWritePend[64]; /* Flush 区間の regs 書き込み（00→00 含む） */
	uint32_t rhythmFlashLeft[6]; // モニタ点滅用（key-on 後しばらく残す）
	uint32_t dumpRingGen;
	SasamiFmMonDump dumpRingSlot[SASAMI_FMMON_RING];
	SasamiMisaoSynth misao;
	int misaoActive;
	uint8_t regs[0x200];
	uint8_t keyOnFm[6];
	int dumpEnable;
	int dumpDirty;
	uint64_t dumpLastFlushSample;
	uint32_t dumpSeq;
	wchar_t dumpSrc[MAX_PATH];
	wchar_t dumpNamedDone[MAX_PATH]; /* 曲名.opna は曲ごと1回だけ（毎 Flush はオーディオを詰まらせる） */

	Impl() : chip(*this), playFmMode(2)
	{
		memset(ssg, 0, sizeof(ssg));
		ssg[7] = 0xBF;
		memset(rzm, 0, sizeof(rzm));
		memset(lrWk, 0, sizeof(lrWk));
		memset(detune, 0, sizeof(detune));
		memset(pitchBase, 0, sizeof(pitchBase));
		memset(waitb, 0, sizeof(waitb));
		memset(loopCnt, 0, sizeof(loopCnt));
		memset(vol, 0, sizeof(vol));
		memset(pc, 0, sizeof(pc));
		memset(voiceOff, 0, sizeof(voiceOff));
		memset(voiceSrc, 0, sizeof(voiceSrc));
		memset(alive, 0, sizeof(alive));
		memset(backJumps, 0, sizeof(backJumps));
		memset(ssgOn, 0, sizeof(ssgOn));
		memset(regs, 0, sizeof(regs));
		memset(keyOnFm, 0, sizeof(keyOnFm));
		memset(rhythmFlashLeft, 0, sizeof(rhythmFlashLeft));
		memset(beep, 0, sizeof(beep));
		beepTdm = -1;
		regs[0xB4] = regs[0xB5] = regs[0xB6] = 0xC0;
		regs[0x1B4] = regs[0x1B5] = regs[0x1B6] = 0xC0;
		dumpEnable = 0;
		dumpDirty = 0;
		dumpLastFlushSample = 0;
		dumpSeq = 0;
		dumpSrc[0] = 0;
		dumpNamedDone[0] = 0;
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
		rhythmPulsePend = 0;
		memset(rhythmHitCnt, 0, sizeof(rhythmHitCnt));
		memset(keyOnHitCnt, 0, sizeof(keyOnHitCnt));
		memset(ssgHitCnt, 0, sizeof(ssgHitCnt));
		memset(regWritePend, 0, sizeof(regWritePend));
		dumpRingGen = 0;
		memset(dumpRingSlot, 0, sizeof(dumpRingSlot));
		misaoActive = 0;
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
		if (reg == 0x10) {
			if (!(data & 0x80)) {
				/* 同一値の連打でも WAV は再トリガされる → モニタも pulse で拾う */
				rhythmPulsePend = (uint8_t)(rhythmPulsePend | (data & 0x3F));
				rhythmkey |= (uint8_t)(data & 0x3F);
				const uint32_t flash = (hostRate > 0) ? (hostRate / 6) : 8000; // ~167ms
				for (int i = 0; i < 6; i++) {
					if (!(data & (1 << i))) continue;
					rhythmHitCnt[i]++;
					rhythmFlashLeft[i] = flash;
					if (rhythmHaveWav && rhythm[i].sampleLen)
						rhythm[i].pos = 0;
				}
			} else {
				rhythmkey = (uint8_t)(rhythmkey & ~data);
			}
			return;
		}
		if (!rhythmHaveWav) return;
		if (reg == 0x11) {
			rhythmtl = (~data) & 63;
		} else if (reg >= 0x18 && reg <= 0x1D) {
			const int i = reg - 0x18;
			rhythm[i].pan = (data >> 6) & 3;
			rhythm[i].level = (~data) & 31;
		}
	}

	void TickRhythmFlash()
	{
		for (int i = 0; i < 6; i++) {
			if (rhythmFlashLeft[i])
				rhythmFlashLeft[i]--;
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

	void NoteKeyOnReg(uint8_t data)
	{
		const int chBits = data & 7;
		int idx = -1;
		if (chBits <= 2) idx = chBits;
		else if (chBits >= 4 && chBits <= 6) idx = chBits - 1;
		if (idx < 0 || idx > 5) return;
		if (data & 0xF0) {
			keyOnHitCnt[idx]++;
			keyOnFm[idx] = 1;
		} else {
			keyOnFm[idx] = 0;
		}
	}

	void MarkDump()
	{
		dumpSeq++;
		dumpDirty = 1;
	}

	void EnsureDumpDir(wchar_t* dir, int dirChars)
	{
		wchar_t tmp[MAX_PATH];
		GetTempPathW(MAX_PATH, tmp);
		_snwprintf_s(dir, dirChars, _TRUNCATE, L"%sogg_kbsasami", tmp);
		CreateDirectoryW(dir, NULL);
	}

	void WriteBlobFile(const wchar_t* path, const void* data, DWORD bytes)
	{
		HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) return;
		DWORD wr = 0;
		WriteFile(h, data, bytes, &wr, NULL);
		CloseHandle(h);
	}

	void WriteDumpFile(const wchar_t* path, const SasamiFmMonDump& d)
	{
		WriteBlobFile(path, &d, (DWORD)sizeof(d));
	}

	void FlushDump(uint64_t curSample)
	{
		if (!dumpEnable) return;
		/* dirty のみ。レート制限しない — 同じ Render 塊の終端時刻にまとめると
		   密な FM3/4・SSG の tick 単位の変化が消え、聞こえる音とずれる。
		   履歴はリングへ積む（UI が gen 差分で読む）。 */
		if (!dumpDirty) return;
		dumpDirty = 0;
		dumpLastFlushSample = curSample;
		SasamiFmMonDump d;
		memset(&d, 0, sizeof(d));
		d.magic[0] = 'O'; d.magic[1] = 'P'; d.magic[2] = 'N'; d.magic[3] = 'A';
		d.version = SASAMI_FMMON_VERSION;
		d.seq = dumpSeq;
		d.sampleRate = hostRate;
		d.curSample = curSample;
		memcpy(d.regs, regs, sizeof(regs));
		memcpy(d.keyOnFm, keyOnFm, sizeof(keyOnFm));
		memcpy(d.ssgOn, ssgOn, sizeof(ssgOn));
		/* モニタ: 再生中 or key-on フラッシュ（短いヒットも点滅させる） */
		{
			uint8_t bits = 0;
			for (int i = 0; i < 6; i++) {
				int lit = 0;
				if (rhythmFlashLeft[i])
					lit = 1;
				else if (rhythmHaveWav && (rhythmkey & (1 << i))
					&& rhythm[i].sampleLen && rhythm[i].pos < rhythm[i].size)
					lit = 1;
				else if (!rhythmHaveWav && (rhythmkey & (1 << i)))
					lit = 1;
				if (lit) bits |= (uint8_t)(1 << i);
			}
			d.rhythmKey = bits;
		}
		d.rhythmPulse = rhythmPulsePend;
		rhythmPulsePend = 0;
		memcpy(d.rhythmHitCnt, rhythmHitCnt, sizeof(rhythmHitCnt));
		memcpy(d.keyOnHitCnt, keyOnHitCnt, sizeof(keyOnHitCnt));
		memcpy(d.ssgHitCnt, ssgHitCnt, sizeof(ssgHitCnt));
		memcpy(d.regWriteBits, regWritePend, sizeof(regWritePend));
		memset(regWritePend, 0, sizeof(regWritePend));
		d.padHit = (uint8_t)playFmMode; /* 0=BEEP 1=OPN 2=OPNA（モニタ見出し用） */
		d.fm10 = fm10 ? 1 : 0;
		d.pcmCount = 0;
		memset(d.pcmOn, 0, sizeof(d.pcmOn));
		memset(d.pcmNote, 0, sizeof(d.pcmNote));
		if (misaoActive) {
			int n = 0;
			misao.FillMonitor(d.pcmOn, d.pcmNote, SASAMI_FMMON_PCM_MAX, &n);
			d.pcmCount = (uint8_t)((n > 0 && n <= SASAMI_FMMON_PCM_MAX) ? n : 0);
		}
		strncpy_s(d.titleSjis, song.titleSjis, _TRUNCATE);
		wcsncpy_s(d.sourcePath, dumpSrc, _TRUNCATE);

		wchar_t dir[MAX_PATH];
		EnsureDumpDir(dir, MAX_PATH);
		wchar_t live[MAX_PATH];
		_snwprintf_s(live, _TRUNCATE, L"%s\\fmmon_live.opna", dir);
		WriteDumpFile(live, d);
		/* リング: 未読 live 上書きでも Flush 履歴を残す（スロット1個だけ更新） */
		{
			const uint32_t idx = dumpRingGen % SASAMI_FMMON_RING;
			dumpRingSlot[idx] = d;
			dumpRingGen++;
			wchar_t ringPath[MAX_PATH];
			_snwprintf_s(ringPath, _TRUNCATE, L"%s\\fmmon_ring.opna", dir);
			HANDLE h = CreateFileW(ringPath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (h != INVALID_HANDLE_VALUE) {
				LARGE_INTEGER sz;
				sz.QuadPart = 0;
				GetFileSizeEx(h, &sz);
				if ((ULONGLONG)sz.QuadPart < sizeof(SasamiFmMonRing)) {
					SasamiFmMonRing blank;
					memset(&blank, 0, sizeof(blank));
					blank.magic[0] = 'O'; blank.magic[1] = 'P'; blank.magic[2] = 'N'; blank.magic[3] = 'R';
					blank.version = SASAMI_FMMON_RING_VERSION;
					DWORD wr = 0;
					SetFilePointer(h, 0, NULL, FILE_BEGIN);
					WriteFile(h, &blank, sizeof(blank), &wr, NULL);
					SetEndOfFile(h);
				}
				DWORD wr = 0;
				LARGE_INTEGER off;
				off.QuadPart = (LONGLONG)offsetof(SasamiFmMonRing, slot) + (LONGLONG)idx * (LONGLONG)sizeof(SasamiFmMonDump);
				SetFilePointerEx(h, off, NULL, FILE_BEGIN);
				WriteFile(h, &d, sizeof(d), &wr, NULL);
				SasamiFmMonRing hdr;
				memset(&hdr, 0, sizeof(hdr));
				hdr.magic[0] = 'O'; hdr.magic[1] = 'P'; hdr.magic[2] = 'N'; hdr.magic[3] = 'R';
				hdr.version = SASAMI_FMMON_RING_VERSION;
				hdr.gen = dumpRingGen;
				off.QuadPart = 0;
				SetFilePointerEx(h, off, NULL, FILE_BEGIN);
				WriteFile(h, &hdr, (DWORD)offsetof(SasamiFmMonRing, slot), &wr, NULL);
				CloseHandle(h);
			}
		}
		if (dumpSrc[0] && wcscmp(dumpNamedDone, dumpSrc) != 0) {
			const wchar_t* name = dumpSrc;
			for (const wchar_t* p = dumpSrc; *p; p++)
				if (*p == L'\\' || *p == L'/') name = p + 1;
			wchar_t stem[MAX_PATH];
			wcsncpy_s(stem, name, _TRUNCATE);
			wchar_t* dot = wcsrchr(stem, L'.');
			if (dot && dot != stem) *dot = 0;
			if (stem[0]) {
				wchar_t named[MAX_PATH];
				_snwprintf_s(named, _TRUNCATE, L"%s\\%s.opna", dir, stem);
				WriteDumpFile(named, d);
				wcsncpy_s(dumpNamedDone, dumpSrc, _TRUNCATE);
			}
		}
	}

	void BeepOff(int ch)
	{
		if (ch < 0 || ch >= 10) return;
		beep[ch].on = 0;
	}

	void BeepNote(int ch, uint8_t note, int keyOn)
	{
		if (ch < 0 || ch >= 10 || IsRhythm(ch)) return;
		if (!keyOn) {
			BeepOff(ch);
			return;
		}
		const int nidx = note & 0x0F;
		int oct = (note >> 4) & 0x0F;
		if (oct == 0) oct = 1;
		uint16_t per = kPsgHz[nidx & 15];
		const int sh = oct - 1;
		if (sh > 0 && sh < 16) per = (uint16_t)(per >> sh);
		if (per == 0) per = 1;
		/* Soft PC-speaker style: SSG-like period → Hz, square mix */
		const double freq = ((double)kOpnaClock / 64.0) / (double)per;
		const double hr = hostRate ? (double)hostRate : 44100.0;
		beep[ch].step = freq / hr;
		if (beep[ch].step > 0.45) beep[ch].step = 0.45;
		beep[ch].amp = 5200; /* 時分割1声なので同時加算より大きめ */
		beep[ch].on = 1;
		if (beep[ch].phase < 0.0 || beep[ch].phase >= 1.0)
			beep[ch].phase = 0.0;
	}

	void MarkRegWrite(unsigned idx)
	{
		if (idx >= 0x200) return;
		regWritePend[idx >> 3] |= (uint8_t)(1u << (idx & 7));
	}
	void FmOut(uint8_t reg, uint8_t data)
	{
		regs[reg] = data;
		MarkRegWrite(reg);
		if (reg < 16) ssg[reg] = data;
		if (playFmMode == 0) {
			if (reg == 0x28) NoteKeyOnReg(data);
			MarkDump();
			return;
		}
		/* OPN/OPNA とも ym2608。OPN はリズム無効 */
		chip.write(0, reg);
		chip.write(1, data);
		if (reg == 0x28) NoteKeyOnReg(data);
		if (playFmMode == 2)
			RhythmReg(reg, data);
		MarkDump();
	}
	void Fm2Out(uint8_t reg, uint8_t data)
	{
		if (playFmMode != 2) return;
		chip.write(2, reg);
		chip.write(3, data);
		regs[0x100 | reg] = data;
		MarkRegWrite(0x100u | reg);
		MarkDump();
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
		if (playFmMode == 0) {
			BeepOff(ch);
			if (IsSsg(ch)) {
				const int s = ch - 3;
				ssgOn[s] = 0;
			}
			return;
		}
		const int k = OpnKey(ch);
		if (k >= 0) {
			FmOut(0x28, (uint8_t)k);
			chip.flush_fm_clock();
		}
		/* 原版 FKYU は SSG のミキサ(07h)を触らない。FMSSGKEY クリアのみ。
		   休符のたびに tone disable すると単極性 SSG/ノイズがゲートされボソボソになる。 */
		if (IsSsg(ch)) {
			const int s = ch - 3;
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

	int SsgSlot(int ch)
	{
		if (ch == 10) return 2;
		if (IsSsg(ch)) return ch - 3;
		return -1;
	}

	void WriteFnum(int ch, uint16_t fnBase)
	{
		pitchBase[ch] = fnBase;
		int32_t fn = (int32_t)fnBase + (int32_t)detune[ch];
		if (fn < 0) fn = 0;
		if (fn > 0x3FFF) fn = 0x3FFF;
		const int k = OpnKey(ch);
		if (k < 0) return;
		const uint8_t hi = (uint8_t)((uint16_t)fn >> 8);
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

	void WriteSsgPeriod(int ch, uint16_t perBase)
	{
		pitchBase[ch] = perBase;
		const int s = SsgSlot(ch);
		if (s < 0) return;
		int32_t per = (int32_t)perBase + (int32_t)detune[ch];
		if (per < 0) per = 0;
		if (per > 0x0FFF) per = 0x0FFF;
		FmOut((uint8_t)(s * 2), (uint8_t)per);
		FmOut((uint8_t)(s * 2 + 1), (uint8_t)((uint16_t)per >> 8));
	}

	void ApplyDetuneNow(int ch)
	{
		const int s = SsgSlot(ch);
		if (s >= 0) {
			WriteSsgPeriod(ch, pitchBase[ch]);
			return;
		}
		if (OpnKey(ch) >= 0)
			WriteFnum(ch, pitchBase[ch]);
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
		if (dest < addr)
			KeyOff(ch);
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
		/* SASAMI waits of 0/1 still need a hold; ymfm also needs >=1 tick of
		   key-on before the next key-off or the attack never clocks. */
		if (wait < 2) wait = 2;
		waitb[ch] = wait;
		if (playFmMode == 0) {
			BeepNote(ch, note, keyOn);
			return;
		}
		const int k = OpnKey(ch);
		if (k < 0) return;
		const int nidx = note & 0x0F;
		const uint8_t ah = (uint8_t)((note & 0xF0) >> 1);
		const uint16_t raw = kFmHz[nidx & 15];
		const uint8_t dh = (uint8_t)((uint8_t)(raw >> 8) | ah);
		const uint8_t dl = (uint8_t)raw;
		const uint16_t fn = (uint16_t)((dh << 8) | dl);
		if (keyOn) {
			/* Retrigger: key-off → clock → fnum → key-on → clock.
			   Without the post-keyon clock, looped FNOTE streams (cmd14 rewind)
			   often key-off the previous note before ymfm ever samples key-on —
			   monitor lights up, audio stays silent. */
			FmOut(0x28, (uint8_t)k);
			chip.flush_fm_clock();
			WriteFnum(ch, fn);
			ApplyTl(ch);
			FmOut(0x28, (uint8_t)(0xF0 | k));
			chip.flush_fm_clock();
		} else {
			WriteFnum(ch, fn);
		}
	}

	void NoteSsg(int ch, uint8_t note, uint8_t wait)
	{
		waitb[ch] = wait;
		if (playFmMode == 0) {
			BeepNote(ch, note, 1);
			const int s = SsgSlot(ch);
			if (s >= 0) {
				ssgHitCnt[s]++;
				ssgOn[s] = 1;
			}
			return;
		}
		const int s = SsgSlot(ch);
		if (s < 0) return;
		const int nidx = note & 0x0F;
		int oct = (note >> 4) & 0x0F;
		if (oct == 0) oct = 1;
		uint16_t per = kPsgHz[nidx & 15];
		const int sh = oct - 1;
		if (sh > 0 && sh < 16) per = (uint16_t)(per >> sh);
		WriteSsgPeriod(ch, per);
		ssg[7] = (uint8_t)(ssg[7] & ~(1 << s));
		FmOut(7, ssg[7]);
		ssgHitCnt[s]++;
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
			if (misaoActive) misao.SetTempoT(T);
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
			/* Loop end: key-off before rewind so tied/overlapped notes don't stick mute. */
			KeyOff(ch);
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
		case 15: { // PPICH: 原版 MOV AX,[DI+1]; XCHG AH,AL → (b1<<8)|b2、その後 DETUNE 加算
			const int s = SsgSlot(ch);
			if (s >= 0) {
				const uint16_t per = (uint16_t)((b1 << 8) | b2);
				WriteSsgPeriod(ch, per);
			}
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
		case 18: // FDETUN
			detune[ch] = (int16_t)(w1 - 0x8000);
			ApplyDetuneNow(ch);
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
		if (misaoActive) misao.TickOnce();
		if (playFmMode == 0)
			BeepAdvanceTdm();
	}

	void ClockFm(int outputs = 8)
	{
		// ymfm samples key-on only when FM is clocked. MED fidelity clocks
		// once per 6 output samples, so a bare keyoff+keyon write never
		// retriggers the envelope (notes decay via SR until silent).
		if (playFmMode == 0) return;
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
		const int32_t c = o.data[2 % n]; /* MixTo1 SSG（ノイズ含む） */
		curL = a + c;
		curR = b + c;
	}

	void BeepAdvanceTdm()
	{
		/* 本家 BPLYOL: ゲート中スロットを WORKBP で回し、1 tick に1音だけ PIT へ */
		const int n = (chCount > 10) ? 10 : chCount;
		if (n <= 0) { beepTdm = -1; return; }
		int start = beepTdm;
		if (start < 0 || start >= n) start = n - 1;
		for (int i = 0; i < n; i++) {
			int ch = start + 1 + i;
			if (ch >= n) ch -= n;
			if (IsRhythm(ch)) continue;
			if (beep[ch].on && beep[ch].step > 0.0) {
				beepTdm = ch;
				return;
			}
		}
		beepTdm = -1;
	}

	void HostSampleBeep(int16_t* L, int16_t* R)
	{
		if (beepTdm < 0 || beepTdm >= 10 || !beep[beepTdm].on || beep[beepTdm].step <= 0.0) {
			*L = *R = 0;
			return;
		}
		BeepVoice& v = beep[beepTdm];
		v.phase += v.step;
		if (v.phase >= 1.0) v.phase -= floor(v.phase);
		const int32_t s = (v.phase < 0.5) ? v.amp : -v.amp;
		const int16_t o = Clamp16((int)((int64_t)s * kMasterPct / 100));
		*L = o;
		*R = o;
	}

	void HostSample(int16_t* L, int16_t* R)
	{
		if (playFmMode == 0) {
			HostSampleBeep(L, R);
			return;
		}
		/* 間引きだけだと SSG ノイズ LFSR がエイリアスしてボソボソになる。
		   1 host sample 分の chip 出力を平均してから出す。 */
		int64_t sumL = 0, sumR = 0;
		int nGen = 0;
		chipAcc += (int64_t)chipRate;
		while (chipAcc >= (int64_t)hostRate) {
			chipAcc -= (int64_t)hostRate;
			ChipSample();
			sumL += curL;
			sumR += curR;
			nGen++;
		}
		if (nGen > 0) {
			curL = (int32_t)(sumL / nGen);
			curR = (int32_t)(sumR / nGen);
		}
		int32_t l = curL, r = curR;
		if (playFmMode == 2)
			MixRhythm(&l, &r);
		TickRhythmFlash();
		*L = Clamp16((int)((int64_t)l * kMasterPct / 100));
		*R = Clamp16((int)((int64_t)r * kMasterPct / 100));
	}

	void InitChip()
	{
		memset(beep, 0, sizeof(beep));
		beepTdm = -1;
		if (playFmMode == 0) {
			ssg[7] = 0xBF;
			rhythmkey = 0;
			rhythmPulsePend = 0;
			memset(rhythmHitCnt, 0, sizeof(rhythmHitCnt));
			memset(keyOnHitCnt, 0, sizeof(keyOnHitCnt));
			memset(ssgHitCnt, 0, sizeof(ssgHitCnt));
			memset(regWritePend, 0, sizeof(regWritePend));
			curL = curR = 0;
			return;
		}
		chip.reset();
		chip.setfmvolume(kFmVolume);
		chip.setpsgvolume(kPsgVolume);
		/* リズム/ADPCM-A は両モードで明示 mute（OPN でゴミが FM に混ざるとノイズに聞こえる） */
		FmOut(0x10, 0xBF);
		FmOut(0x11, 0x3F);
		if (playFmMode == 2) {
			/* OPNA: SCH で FM6ch + リズム音色レベル */
			FmOut(0x29, 0x83);
			for (int i = 0; i < 6; i++) {
				rzm[i] = kRzmDef[i];
				FmOut((uint8_t)(0x18 + i), rzm[i]);
			}
		}
		/* OPN: 0x29 を書かず FM3+SSG3（SCH オフ = YM2203 相当） */
		FmOut(0x28, 0x00);
		FmOut(0x28, 0x01);
		FmOut(0x28, 0x02);
		FmOut(0x07, 0xBF);
		ssg[7] = 0xBF;
		rhythmkey = 0;
		rhythmPulsePend = 0;
		memset(rhythmHitCnt, 0, sizeof(rhythmHitCnt));
		memset(keyOnHitCnt, 0, sizeof(keyOnHitCnt));
		memset(ssgHitCnt, 0, sizeof(ssgHitCnt));
		memset(regWritePend, 0, sizeof(regWritePend));
		dumpRingGen = 0;
		memset(dumpRingSlot, 0, sizeof(dumpRingSlot));
		memset(rhythmFlashLeft, 0, sizeof(rhythmFlashLeft));
		if (playFmMode == 2 && rhythmHaveWav) {
			for (int i = 0; i < 6; i++)
				rhythm[i].pos = rhythm[i].size;
		}
		if (playFmMode == 2) {
			FmOut(0x28, 0x04);
			FmOut(0x28, 0x05);
			FmOut(0x28, 0x06);
		}
		for (int i = 0; i < 3; i++) {
			FmOut((uint8_t)(0xB4 + i), 0xC0);
			lrWk[i] = 0xC0;
			if (playFmMode == 2) {
				Fm2Out((uint8_t)(0xB4 + i), 0xC0);
				lrWk[i + 4] = 0xC0;
			}
		}
		ChipSample();
	}

	void SetupSong()
	{
		if (playFmMode == 1) {
			/* OPN: always 6ch (FM0-2+SSG); skip bank1/rhythm */
			fm10 = 0;
			chCount = 6;
		} else {
			fm10 = song.fmOpna10ch ? 1 : 0;
			chCount = fm10 ? 10 : 6;
		}
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
		if (misaoActive) misao.Reset();
		if (misaoActive) misao.SetTempoT(T);
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

bool SasamiFmPlayer::Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* rhythmDir, int fmMode)
{
	Close();
	if (song.kind != SASAMI_KIND_FPY || song.dataSize == 0) return false;
	std::lock_guard<std::mutex> lk(m_lock);
	m = new Impl();
	m->song = song;
	m->playFmMode = (fmMode < 0 || fmMode > 2) ? 2 : fmMode;
	m->hostRate = sampleRate < 8000 ? 44100 : sampleRate;
	m_hostRate = m->hostRate;
	if (m->playFmMode == 2)
		m->LoadRhythm(rhythmDir);
	m->PrepareRhythmSteps();
	m->misaoActive = SasamiMisaoActive(song) ? 1 : 0;
	if (m->misaoActive)
		m->misaoActive = m->misao.Open(song, m->hostRate, rhythmDir, &m->T) ? 1 : 0;
	if (m->playFmMode == 1 || m->playFmMode == 2) {
		/* MAX: SSG を高レートで生成→HostSample 平均でエイリアスを抑える（ノイズ向け） */
		m->chip.set_fidelity(ymfm::OPN_FIDELITY_MAX);
		m->chipRate = m->chip.sample_rate(kOpnaClock);
		if (m->chipRate < 8000) m->chipRate = kOpnaClock / 8;
	} else {
		m->chipRate = m->hostRate;
	}
	strncpy_s(m_title, song.titleSjis, _TRUNCATE);
	{
		m->measureLen = 1;
		m->SetupSong();
		uint64_t samples = 0;
		uint64_t carry = 0;
		uint32_t guard = 0;
		while ((!m->ended || (m->misaoActive && !m->misao.Ended())) && guard++ < kMaxTicks) {
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
	if (m) {
		m->dumpEnable = 0;
		m->misao.Close();
	}
	delete m;
	m = NULL;
	m_totalSamples = 0;
	m_curSample = 0;
}

void SasamiFmPlayer::SetFmMonDump(int enable, const wchar_t* sourcePath)
{
	std::lock_guard<std::mutex> lk(m_lock);
	if (!m) return;
	/* dump は OPN/OPNA 再生時（BEEP は無効） */
	if (enable && m->playFmMode == 0)
		enable = 0;
	m->dumpEnable = enable ? 1 : 0;
	m->dumpNamedDone[0] = 0;
	if (sourcePath && sourcePath[0])
		wcsncpy_s(m->dumpSrc, sourcePath, _TRUNCATE);
	else
		m->dumpSrc[0] = 0;
	if (m->dumpEnable) {
		/* 曲切替: 古い live/ring（前曲の DO-- など）を消してから書き直す */
		wchar_t dir[MAX_PATH];
		m->EnsureDumpDir(dir, MAX_PATH);
		wchar_t live[MAX_PATH], ring[MAX_PATH];
		_snwprintf_s(live, _TRUNCATE, L"%s\\fmmon_live.opna", dir);
		_snwprintf_s(ring, _TRUNCATE, L"%s\\fmmon_ring.opna", dir);
		DeleteFileW(live);
		DeleteFileW(ring);
		m->dumpSeq = 0;
		m->dumpRingGen = 0;
		m->dumpDirty = 1;
		m->dumpLastFlushSample = 0; /* 有効化時は即1枚 */
		m->FlushDump(m_curSample);
	}
}

int SasamiFmPlayer::PlayFmMode() const
{
	return m ? m->playFmMode : 2;
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
			const int misaoDone = !m->misaoActive || m->misao.Ended();
			if (m->ended && misaoDone) {
				memset(interleavedStereo + out * 2, 0, (size_t)(want - out) * 2 * sizeof(int16_t));
				out = want;
				m->eofSent = 1;
				break;
			}
			if (m->ended && !misaoDone) {
				m->tickCarry += (uint64_t)m->hostRate * m->T;
				const uint32_t sl = (uint32_t)(m->tickCarry / kTickDen);
				m->tickCarry %= kTickDen;
				if (m->misaoActive) m->misao.TickOnce();
				m->samplesLeftInTick = sl ? sl : 1;
				continue;
			}
			m->tickCarry += (uint64_t)m->hostRate * m->T;
			const uint32_t sl = (uint32_t)(m->tickCarry / kTickDen);
			m->tickCarry %= kTickDen;
			m->TickOnce();
			/* この tick のレジスタ変化は「ここから出る PCM」と同時に聞こえる。
			   ブロック終端へまとめて stamp すると 1 Render 内の複数 tick が
			   同一 curSample になり、密な CH3/4・SSG だけ大きくずれる。 */
			if (m->dumpEnable)
				m->FlushDump(m_curSample + out);
			m->samplesLeftInTick = sl;
			/* sl==0 で TickOnce を連続すると、FNOTE の key-on が generate されず
			   次の key-off で消える（ループ内の短い音符が無音になる）。最低1sample出す。 */
			if (sl == 0) {
				m->chip.flush_fm_clock();
				m->samplesLeftInTick = 1;
			}
		}
		uint32_t take = m->samplesLeftInTick;
		if (take > want - out) take = want - out;
		for (uint32_t i = 0; i < take; i++) {
			int16_t L, R;
			m->HostSample(&L, &R);
			if (m->misaoActive) {
				double mix[2] = { 0.0, 0.0 };
				m->misao.SynthesizeMix(mix, 1);
				int l = L + (int)(mix[0] * 32767.0);
				int r = R + (int)(mix[1] * 32767.0);
				if (l > 32767) l = 32767;
				if (l < -32768) l = -32768;
				if (r > 32767) r = 32767;
				if (r < -32768) r = -32768;
				L = (int16_t)l;
				R = (int16_t)r;
			}
			interleavedStereo[(out + i) * 2] = L;
			interleavedStereo[(out + i) * 2 + 1] = R;
		}
		m->samplesLeftInTick -= take;
		out += take;
	}
	m_curSample += out;
	if (m->dumpEnable)
		m->FlushDump(m_curSample);
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
