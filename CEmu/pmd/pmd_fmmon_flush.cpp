// PMD FM monitor flush — latch every call, write on even sample grid.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pmdwincore.h"
#include "../fmmon/fmmon_write.h"
#include "sasami_fmmon.h"

extern void PmdFmMonFlushAfterRender(PMDWIN* self, uint32_t sampleRate, uint64_t curSample);

static int OnkaiToMidi(int onkai)
{
	if (onkai < 0 || onkai == 255)
		return -1;
	int oct = (onkai >> 4) & 0x0f;
	int deg = onkai & 0x0f;
	if (deg == 0x0f)
		return -1;
	while (deg >= 12) {
		deg -= 12;
		oct++;
	}
	int midi = oct * 12 + deg + 12;
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	return midi;
}

static int PmdFmMonIsAliasPath(const wchar_t* p)
{
	if (!p || !p[0]) return 1;
	return (_wcsicmp(p, L"C:\\MUSIC_DIR\\MUSIC_FILE.xxx") == 0
		|| _wcsicmp(p, L"MUSIC_FILE.xxx") == 0) ? 1 : 0;
}

static int PartGate(PMDWIN* self, int part)
{
	QQ* qq = self->getpartwork(part);
	if (!qq) return 0;
	return (qq->onkai != 255 && !(qq->keyoff_flag & 1)) ? 1 : 0;
}

static int PartMidi(PMDWIN* self, int part)
{
	QQ* qq = self->getpartwork(part);
	if (!qq) return -1;
	return OnkaiToMidi(qq->onkai);
}

void PmdFmMonFlushAfterRender(PMDWIN* self, uint32_t sampleRate, uint64_t curSample)
{
	if (!self) return;

	static uint64_t s_lastSamp = 0;
	static uint8_t s_prevFm[6], s_prevSsg[3], s_prevEx[3], s_prevPcm[8], s_prevAdp;
	static uint8_t s_midiFm[6], s_midiSsg[3], s_midiEx[3], s_midiPcm[8], s_midiAdp;
	static uint8_t s_latchFm[6], s_latchSsg[3], s_latchEx[3], s_latchPcm[8], s_latchAdp;
	static uint8_t s_hitFm[6], s_hitSsg[3], s_hitEx[3];
	static uint8_t s_latchRhy, s_hitRhy[6];
	static uint8_t s_prevChipHit[6], s_prevChipSsgHit[3];
	static uint8_t s_pubFm[6], s_pubSsg[3], s_pubEx[3], s_pubPcm[8], s_pubAdp;
	static wchar_t s_srcPath[260];
	static int s_srcOk = 0;
	static int s_inited = 0;
	if (!s_inited) {
		memset(s_midiFm, 0xFF, sizeof(s_midiFm));
		memset(s_midiSsg, 0xFF, sizeof(s_midiSsg));
		memset(s_midiEx, 0xFF, sizeof(s_midiEx));
		memset(s_midiPcm, 0xFF, sizeof(s_midiPcm));
		s_midiAdp = 0xFF;
		s_inited = 1;
	}

	uint8_t gFm[6], gSsg[3], gEx[3], gPcm[8], gAdp = 0;
	int anyHit = 0;
	for (int i = 0; i < 6; i++) {
		gFm[i] = (uint8_t)PartGate(self, i);
		const int midi = PartMidi(self, i);
		if (gFm[i]) {
			if (midi >= 0) {
				if (s_prevFm[i] && s_midiFm[i] != 0xFF && (uint8_t)midi != s_midiFm[i]) {
					s_hitFm[i]++;
					anyHit = 1;
					s_latchFm[i] = 1;
				}
				s_midiFm[i] = (uint8_t)midi;
			}
		}
		if (gFm[i] && !s_prevFm[i]) {
			s_latchFm[i] = 1;
			s_hitFm[i]++;
			anyHit = 1;
		}
		s_prevFm[i] = gFm[i];
	}
	for (int i = 0; i < 3; i++) {
		gSsg[i] = (uint8_t)PartGate(self, 6 + i);
		const int midi = PartMidi(self, 6 + i);
		if (gSsg[i]) {
			if (midi >= 0) {
				if (s_prevSsg[i] && s_midiSsg[i] != 0xFF && (uint8_t)midi != s_midiSsg[i]) {
					s_hitSsg[i]++;
					anyHit = 1;
					s_latchSsg[i] = 1;
				}
				s_midiSsg[i] = (uint8_t)midi;
			}
		}
		if (gSsg[i] && !s_prevSsg[i]) {
			s_latchSsg[i] = 1;
			s_hitSsg[i]++;
			anyHit = 1;
		}
		s_prevSsg[i] = gSsg[i];
	}
	for (int i = 0; i < 3; i++) {
		gEx[i] = (uint8_t)PartGate(self, 11 + i);
		const int midi = PartMidi(self, 11 + i);
		if (gEx[i]) {
			if (midi >= 0) {
				if (s_prevEx[i] && s_midiEx[i] != 0xFF && (uint8_t)midi != s_midiEx[i]) {
					s_hitEx[i]++;
					anyHit = 1;
					s_latchEx[i] = 1;
				}
				s_midiEx[i] = (uint8_t)midi;
			}
		}
		if (gEx[i] && !s_prevEx[i]) {
			s_latchEx[i] = 1;
			s_hitEx[i]++;
			anyHit = 1;
		}
		s_prevEx[i] = gEx[i];
	}
	for (int i = 0; i < 8; i++) {
		gPcm[i] = (uint8_t)PartGate(self, 16 + i);
		const int midi = PartMidi(self, 16 + i);
		if (gPcm[i] && midi >= 0)
			s_midiPcm[i] = (uint8_t)midi;
		if (gPcm[i] && !s_prevPcm[i]) {
			s_latchPcm[i] = 1;
			anyHit = 1;
		}
		s_prevPcm[i] = gPcm[i];
	}
	/* Part 9 = ADPCM (OPNA) or 86PCM (use_p86) — was missing from monitor */
	{
		gAdp = (uint8_t)PartGate(self, 9);
		const int midi = PartMidi(self, 9);
		if (gAdp && midi >= 0)
			s_midiAdp = (uint8_t)midi;
		if (gAdp && !s_prevAdp) {
			s_latchAdp = 1;
			anyHit = 1;
		}
		s_prevAdp = gAdp;
	}

	const uint32_t sr = (sampleRate >= 8000) ? sampleRate : 44100;
	if (s_lastSamp != 0) {
		if (curSample == s_lastSamp)
			return;
		const uint64_t elapsed = (curSample > s_lastSamp) ? (curSample - s_lastSamp) : 0;
		if (elapsed < ((uint64_t)sr * 4 / 1000))
			return;
	}

	int dirty = anyHit || (s_latchRhy != 0);
	if (memcmp(s_prevFm, s_pubFm, 6) != 0) dirty = 1;
	if (memcmp(s_prevSsg, s_pubSsg, 3) != 0) dirty = 1;
	if (memcmp(s_prevEx, s_pubEx, 3) != 0) dirty = 1;
	if (memcmp(s_prevPcm, s_pubPcm, 8) != 0) dirty = 1;
	if (s_prevAdp != s_pubAdp) dirty = 1;

	static SasamiFmMonDump s_dump;
	SasamiFmMonDump* pd = &s_dump;
	FmMonInitDump(pd);
	pd->sampleRate = sr;
	pd->curSample = curSample;
	pd->padHit = 2;
	pd->fm10 = 1;
	pd->dumpFlags = 0; /* FLAG_FM3EX set after snapshot if CH3 effect mode */
	if (!s_srcOk) {
		TCHAR music[MAX_PATH] = {};
		self->getmusicfilename(music);
#if defined(UNICODE) || defined(_UNICODE)
		if (music[0] && !PmdFmMonIsAliasPath(music)) {
			wcsncpy_s(s_srcPath, music, _TRUNCATE);
			s_srcOk = 1;
		}
#else
		if (music[0]) {
			wchar_t w[260] = {};
			MultiByteToWideChar(CP_ACP, 0, music, -1, w, 260);
			if (!PmdFmMonIsAliasPath(w)) {
				wcsncpy_s(s_srcPath, w, _TRUNCATE);
				s_srcOk = 1;
			}
		}
#endif
	}
	if (s_srcOk)
		wcsncpy_s(pd->sourcePath, s_srcPath, _TRUNCATE);

	unsigned int seq = 0;
	uint8_t chipFm[6] = {}, chipHit[6] = {}, chipSsg[3] = {}, chipSsgHit[3] = {};
	uint8_t chipRhyKey = 0, chipRhyPulse = 0, chipRhyHit[6] = {};
	self->FmMonSnapshot(pd->regs, pd->regWriteBits, chipFm, chipHit,
		chipSsg, chipSsgHit, &chipRhyKey, &chipRhyPulse, chipRhyHit, &seq);
	pd->seq = seq;
	if (chipRhyPulse != 0) dirty = 1;
	for (int i = 0; i < 6; i++)
		if (chipHit[i] != s_prevChipHit[i]) dirty = 1;
	for (int i = 0; i < 3; i++)
		if (chipSsgHit[i] != s_prevChipSsgHit[i]) dirty = 1;
	(void)dirty;
	/* 4ms グリッドでは dirty で落とさない。落とすと seq が伸びず鍵盤が歯抜けになる */
	/* 0x27 bits7-6 != 0 → CH3 multi-freq / effect (FM3EX parts exist) */
	if ((pd->regs[0x27] & 0xC0) != 0)
		pd->dumpFlags |= SASAMI_FMMON_FLAG_FM3EX;

	for (int i = 0; i < 6; i++) {
		pd->keyOnFm[i] = (uint8_t)(s_latchFm[i] | gFm[i] | chipFm[i]);
		s_hitFm[i] = (uint8_t)(s_hitFm[i] + (uint8_t)(chipHit[i] - s_prevChipHit[i]));
		s_prevChipHit[i] = chipHit[i];
		pd->keyOnHitCnt[i] = s_hitFm[i];
		pd->keyMidi[i] = s_midiFm[i];
		s_latchFm[i] = 0;
	}
	for (int i = 0; i < 3; i++) {
		pd->ssgOn[i] = (uint8_t)(s_latchSsg[i] | gSsg[i] | chipSsg[i]);
		s_hitSsg[i] = (uint8_t)(s_hitSsg[i] + (uint8_t)(chipSsgHit[i] - s_prevChipSsgHit[i]));
		s_prevChipSsgHit[i] = chipSsgHit[i];
		pd->ssgHitCnt[i] = s_hitSsg[i];
		pd->ssgMidi[i] = s_midiSsg[i];
		s_latchSsg[i] = 0;
	}
	for (int i = 0; i < 3; i++) {
		pd->keyOnEx[i] = (uint8_t)(s_latchEx[i] | gEx[i]);
		pd->keyOnExHitCnt[i] = s_hitEx[i];
		pd->exMidi[i] = s_midiEx[i];
		s_latchEx[i] = 0;
	}
	int anyPpz = 0;
	for (int i = 0; i < 8; i++) {
		pd->pcmOn[i] = (uint8_t)(s_latchPcm[i] | gPcm[i]);
		pd->pcmNote[i] = (s_midiPcm[i] != 0xFF) ? s_midiPcm[i] : 0;
		if (pd->pcmOn[i]) anyPpz = 1;
		s_latchPcm[i] = 0;
	}
	pd->pcmOn[8] = (uint8_t)(s_latchAdp | gAdp);
	pd->pcmNote[8] = (s_midiAdp != 0xFF) ? s_midiAdp : 0;
	s_latchAdp = 0;
	const int hasAdp = pd->pcmOn[8] ? 1 : 0;
	int use86 = 0;
	{
		OPEN_WORK* ow = self->getopenwork();
		if (ow && ow->use_p86) use86 = 1;
	}
	if (anyPpz) {
		pd->pcmCount = hasAdp ? 9 : 8;
		pd->dumpFlags |= SASAMI_FMMON_FLAG_PPZ;
	} else if (hasAdp) {
		/* No PPZ: put ADPCM/86 at slot 0 so UI is not 8 empty rows + one */
		pd->pcmOn[0] = pd->pcmOn[8];
		pd->pcmNote[0] = pd->pcmNote[8];
		pd->pcmOn[8] = 0;
		pd->pcmNote[8] = 0;
		pd->pcmCount = 1;
	}
	if (hasAdp)
		pd->dumpFlags |= (uint8_t)(use86 ? SASAMI_FMMON_FLAG_PCM86 : SASAMI_FMMON_FLAG_ADPCM);

	s_latchRhy = (uint8_t)(s_latchRhy | chipRhyPulse);
	for (int i = 0; i < 6; i++) {
		if (chipRhyPulse & (1 << i))
			s_hitRhy[i]++;
		/* チップ累積も取り込む */
		if (chipRhyHit[i] > s_hitRhy[i])
			s_hitRhy[i] = chipRhyHit[i];
	}
	pd->rhythmPulse = s_latchRhy;
	pd->rhythmKey = (uint8_t)((chipRhyKey & 0x3F) | s_latchRhy);
	memcpy(pd->rhythmHitCnt, s_hitRhy, 6);
	s_latchRhy = 0;

	FmMonWriteDump(pd);
	self->FmMonAckPulse();

	memcpy(s_pubFm, s_prevFm, 6);
	memcpy(s_pubSsg, s_prevSsg, 3);
	memcpy(s_pubEx, s_prevEx, 3);
	memcpy(s_pubPcm, s_prevPcm, 8);
	s_pubAdp = s_prevAdp;
	s_lastSamp = curSample;
}
