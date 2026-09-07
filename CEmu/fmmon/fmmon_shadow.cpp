#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "fmmon_shadow.h"

/* KPI一覧の [FMmon]/[MIDmon] 検出用。Flush 経路から参照してリンク残す。 */
extern "C" const char g_oggKpiFmMonTag[] = "ogg.FMmon";
extern "C" const char g_oggKpiMidMonTag[] = "ogg.MIDmon";
/* ProbeMonitorCaps が PE 文字列検索するので残す（先頭1B参照だけだと削られる） */
static void FmMonKeepProbeTags()
{
	volatile size_t n = 0;
	n += strlen(g_oggKpiFmMonTag);
	n += strlen(g_oggKpiMidMonTag);
	(void)n;
}

static CRITICAL_SECTION s_cs;
static LONG s_once = 0;
static uint8_t s_regs[0x200];
static uint8_t s_bits[64];
static uint8_t s_keyFm[6], s_hitFm[6], s_midiFm[6];
static uint8_t s_keyEx[3], s_hitEx[3], s_midiEx[3];
static uint8_t s_ssg[3], s_hitSsg[3], s_midiSsg[3];
static uint8_t s_rhyKey, s_rhyPulse, s_hitRhy[6];
static uint8_t s_adpcmOn, s_adpcmMidi, s_adpcmHit; /* OPNA ADPCM-B (delta-T) */
static uint8_t s_midiChOn[16], s_midiChNote[16], s_midiHit[16];
static uint8_t s_pcmOn[SASAMI_FMMON_PCM_MAX], s_pcmNote[SASAMI_FMMON_PCM_MAX];
static uint8_t s_pcmCount = 0;
static uint8_t s_pcmHit[SASAMI_FMMON_PCM_MAX];
static uint32_t s_sr = 44100;
static uint64_t s_cur = 0;
static uint64_t s_lastWrite = 0;
static int s_dirty = 0;
/* note-on/off 立ち上がりは minDirty を無視して即リングへ（16分の取りこぼし防止） */
static int s_flushUrgent = 0;
static int s_keysOnly = 0;
static unsigned s_keysProfile = 0;
static int s_opmRegsValid = 0;
static int s_ga20Seen = 0; /* OPM+GA20: regs live at dump+$100 */
static int s_aySeen = 0;   /* OPM+AY (X1): AY shadow owns $00-$0F */
static int s_arcRegsValid = 0;
static int s_auxRegsValid = 0; /* PC/AT BEEP PIT / GameBlaster SAA / … */
static unsigned s_ssgClock = 7987200; /* OPNA default; MSX AY → 3579545 */
static unsigned s_msxDevMask = 0;
static unsigned s_oplMode = 0; /* 0=off 1=OPL2 2=OPL3 3=DualOPL2 */
static int s_oplSuppress = 0; /* PC/AT BEEP/CMS/MIDI: ignore AdLib probe writes */
static unsigned s_snLatch = 0;
static unsigned s_snTone[3] = { 0, 0, 0 };
static unsigned s_snNoise = 0;
static unsigned s_snVol[4] = { 0xF, 0xF, 0xF, 0xF };
static int s_snMode = 0; /* SN76489 / SC-3000 keys+regs as SSG-shaped dump */
static int s_opnaLayout = 1; /* 1=OPNA 6ch  0=OPN 3ch  2=YM2610 4ch  -1=other */
static int s_adpcmSeen = 0; /* ADPCM-B ever keyed */
static int s_adpcmASeen = 0; /* YM2610 ADPCM-A ever keyed */
static char s_identPlat[24];
static char s_identChip[40];
static unsigned s_qsPitch[16];
static unsigned s_rf5cCh;
static unsigned s_rf5cPitch[8];
static unsigned s_rf5cVol[8];
static unsigned s_rf5cEnable;
static unsigned s_c352Pitch[32];
static unsigned s_c352Clock = 25401600;
static unsigned s_segaChRegs[16][8];
static unsigned s_okiCmd;
static unsigned s_okiChBits;
static wchar_t s_src[260];

static const int kFnumTbl[12] = {
	0x026a, 0x028f, 0x02b6, 0x02df, 0x030b, 0x0339,
	0x036a, 0x039e, 0x03d5, 0x0410, 0x044e, 0x048f
};

static void RefreshAdpcmMidi(void);
static void RefreshOpmKeysFromRegs(void);
static int ArcIsProfile(unsigned profile);

static void EnsureCs(void)
{
	if (InterlockedCompareExchange(&s_once, 1, 0) == 0)
		InitializeCriticalSection(&s_cs);
}

/* SegaPCM freq (addr delta/tick) → MIDI in piano range A0–C8.
   Absolute Hz mapping pushed notes to O10+ so DrawPiano108 blanked them. */
static int SegaPcmFreqToMidi(unsigned freq)
{
	if (freq < 1u) freq = 1u;
	/* ~0x28 ≈ unity for speech/drum loops @ clock/64 */
	const double midi = 60.0 + 12.0 * (log((double)freq / 40.0) / log(2.0));
	int m = (int)floor(midi + 0.5);
	if (m < 21) m = 21;
	if (m > 108) m = 108;
	return m;
}

static void BlockCalc(int* block, int* fnum)
{
	int cx = (*block & 7) << 11;
	int ax = *fnum;
	while (ax >= 0x26a) {
		if (ax < (0x26a * 2)) break;
		cx += 0x800;
		if (cx != 0x4000) ax -= 0x26a;
		else { cx = 0x3800; if (ax >= 0x800) ax = 0x7ff; break; }
	}
	while (ax < 0x26a) {
		cx -= 0x800;
		if (cx >= 0) ax += 0x26a;
		else { cx = 0; if (ax < 8) ax = 8; break; }
	}
	*block = (cx >> 11) & 7;
	*fnum = ax;
}

static int DegFromFnum(int fnum)
{
	const int kHi = 0x26a * 2;
	for (int i = 0; i < 11; i++) {
		const int mid = (kFnumTbl[i] + kFnumTbl[i + 1]) / 2;
		if (fnum < mid) return i;
	}
	if (fnum < (kFnumTbl[11] + kHi) / 2) return 11;
	return 11;
}

static int MidiFromA4A0(uint8_t a4, uint8_t a0)
{
	int block = (a4 >> 3) & 7;
	int fnum = ((a4 & 7) << 8) | a0;
	if (fnum <= 0) return -1;
	BlockCalc(&block, &fnum);
	int midi = block * 12 + DegFromFnum(fnum) + 12;
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	return midi;
}

static int MidiFromSsgPeriod(uint16_t period)
{
	period &= 0x0FFF;
	if (period == 0) return -1;
	/* OPN(A) SSG: master/32; stand-alone AY/MSX: clock/16. */
	const double div = (s_msxDevMask != 0 || s_opnaLayout < 0) ? 16.0 : 32.0;
	const double freq = (double)s_ssgClock / (div * (double)period);
	if (freq < 8.0) return -1;
	int n = (int)(69.0 + 12.0 * log(freq / 440.0) / log(2.0) + 0.5);
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

static int MidiFromHz(double freq)
{
	if (freq < 8.0) return -1;
	int n = (int)(69.0 + 12.0 * log(freq / 440.0) / log(2.0) + 0.5);
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

static int MidiFromOpll(unsigned fnum, unsigned block)
{
	fnum &= 0x1FF;
	block &= 7;
	if (fnum == 0) return -1;
	/* YM2413: Fout = fnum * clock * 2^block / (72 * 2^19) */
	const double freq = (double)fnum * (double)s_ssgClock * (double)(1u << block)
		/ (72.0 * 524288.0);
	return MidiFromHz(freq);
}

static void MarkBit(unsigned addr)
{
	addr &= 0x1FF;
	s_bits[addr >> 3] |= (uint8_t)(1u << (addr & 7));
}

void FmMonShadowReset(void)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	memset(s_regs, 0, sizeof(s_regs));
	memset(s_bits, 0, sizeof(s_bits));
	memset(s_keyFm, 0, sizeof(s_keyFm));
	memset(s_hitFm, 0, sizeof(s_hitFm));
	memset(s_midiFm, 0xFF, sizeof(s_midiFm));
	memset(s_keyEx, 0, sizeof(s_keyEx));
	memset(s_hitEx, 0, sizeof(s_hitEx));
	memset(s_midiEx, 0xFF, sizeof(s_midiEx));
	memset(s_ssg, 0, sizeof(s_ssg));
	memset(s_hitSsg, 0, sizeof(s_hitSsg));
	memset(s_midiSsg, 0xFF, sizeof(s_midiSsg));
	memset(s_hitRhy, 0, sizeof(s_hitRhy));
	s_adpcmOn = 0;
	s_adpcmMidi = 0xFF;
	s_adpcmHit = 0;
	memset(s_midiChOn, 0, sizeof(s_midiChOn));
	memset(s_midiChNote, 0xFF, sizeof(s_midiChNote));
	memset(s_midiHit, 0, sizeof(s_midiHit));
	memset(s_pcmOn, 0, sizeof(s_pcmOn));
	memset(s_pcmNote, 0xFF, sizeof(s_pcmNote));
	memset(s_pcmHit, 0, sizeof(s_pcmHit));
	s_pcmCount = 0;
	s_rhyKey = s_rhyPulse = 0;
	s_cur = 0;
	s_lastWrite = 0;
	s_dirty = 1;
	s_flushUrgent = 0;
	s_keysOnly = 0;
	s_keysProfile = 0;
	s_opmRegsValid = 0;
	s_ga20Seen = 0;
	s_aySeen = 0;
	s_arcRegsValid = 0;
	s_auxRegsValid = 0;
	s_ssgClock = 7987200;
	s_msxDevMask = 0;
	s_oplMode = 0;
	s_oplSuppress = 0;
	s_snLatch = 0;
	s_snTone[0] = s_snTone[1] = s_snTone[2] = 0;
	s_snNoise = 0;
	s_snVol[0] = s_snVol[1] = s_snVol[2] = s_snVol[3] = 0xF;
	s_snMode = 0;
	s_opnaLayout = 1;
	s_adpcmSeen = 0;
	s_adpcmASeen = 0;
	s_identPlat[0] = 0;
	s_identChip[0] = 0;
	memset(s_qsPitch, 0, sizeof(s_qsPitch));
	s_rf5cCh = 0;
	memset(s_rf5cPitch, 0, sizeof(s_rf5cPitch));
	memset(s_rf5cVol, 0, sizeof(s_rf5cVol));
	s_rf5cEnable = 0;
	memset(s_c352Pitch, 0, sizeof(s_c352Pitch));
	memset(s_segaChRegs, 0, sizeof(s_segaChRegs));
	s_okiCmd = 0;
	s_okiChBits = 0;
	s_src[0] = 0;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetIdentity(const char* platform, const char* chip)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (platform && platform[0])
		strncpy_s(s_identPlat, platform, _TRUNCATE);
	else
		s_identPlat[0] = 0;
	if (chip && chip[0])
		strncpy_s(s_identChip, chip, _TRUNCATE);
	else
		s_identChip[0] = 0;
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowGetIdentity(char* platform, unsigned platformLen,
	char* chip, unsigned chipLen)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (platform && platformLen)
		strncpy_s(platform, platformLen, s_identPlat, _TRUNCATE);
	if (chip && chipLen)
		strncpy_s(chip, chipLen, s_identChip, _TRUNCATE);
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetOpnaLayout(int layout)
{
	/* 1=OPNA(6ch)  0=OPN(3ch)  2=YM2610(4ch)  -1=non-OPN(A) e.g. OPM / SN */
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (layout == 2) s_opnaLayout = 2;
	else if (layout > 0) s_opnaLayout = 1;
	else if (layout < 0) s_opnaLayout = -1;
	else s_opnaLayout = 0;
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetKeysProfile(unsigned profile)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysProfile = profile & 0xFFu;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetOpmRegSnapshot(const unsigned char* regs256)
{
	/* Reg image only — do NOT derive key gates from $08 here.
	   $08 is a write strobe for one channel; re-reading it on every KC/TL
	   write collapsed ys368snd to a single stuck key. */
	FmMonShadowSetOpmRegSnapshotEx(regs256, -1);
}

void FmMonShadowSetOpmRegSnapshotEx(const unsigned char* regs256, int keyRegOrNeg1)
{
	if (!regs256) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	/* Preserve GA20 shadow in bank1 ($100+) across OPM snapshot refreshes. */
	uint8_t ga20Keep[0x20];
	uint8_t ga20BitsKeep[4];
	const int keepGa20 = s_ga20Seen;
	if (keepGa20) {
		memcpy(ga20Keep, s_regs + 0x100, sizeof(ga20Keep));
		memcpy(ga20BitsKeep, s_bits + 32, sizeof(ga20BitsKeep));
	}
	/* Same for the X1's separate AY: its shadow lives at $00-$0F, which is
	   also where OPM $00-$0F land. Letting the snapshot win there fed the SSG
	   gate/note math OPM control bytes (reg $08 key-on read as channel A
	   volume), so the SSG rows keyed at random. OPM only uses $01/$08/$0F in
	   that range, none of which the register panel shows. */
	uint8_t ayKeep[0x10];
	uint8_t ayBitsKeep[2];
	const int keepAy = s_aySeen;
	if (keepAy) {
		memcpy(ayKeep, s_regs, sizeof(ayKeep));
		memcpy(ayBitsKeep, s_bits, sizeof(ayBitsKeep));
	}
	memcpy(s_regs, regs256, 256);
	memset(s_regs + 256, 0, 0x200 - 256);
	memset(s_bits, 0, sizeof(s_bits));
	if (keepGa20) {
		memcpy(s_regs + 0x100, ga20Keep, sizeof(ga20Keep));
		memcpy(s_bits + 32, ga20BitsKeep, sizeof(ga20BitsKeep));
	}
	if (keepAy) {
		memcpy(s_regs, ayKeep, sizeof(ayKeep));
		memcpy(s_bits, ayBitsKeep, sizeof(ayBitsKeep));
	}
	s_opmRegsValid = 1;
	s_opnaLayout = -1;
	s_keysProfile = SASAMI_FMMON_KEYS_MDX;
	if (keyRegOrNeg1 >= 0) {
		const uint8_t k = (uint8_t)(keyRegOrNeg1 & 0xff);
		const int ch = k & 7;
		const int on = (k & 0x78) != 0;
		if (on && !s_midiChOn[ch]) s_midiHit[ch]++;
		s_midiChOn[ch] = (uint8_t)(on ? 1 : 0);
	}
	RefreshOpmKeysFromRegs();
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetSource(const wchar_t* path)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (path && path[0])
		wcsncpy_s(s_src, path, _TRUNCATE);
	else
		s_src[0] = 0;
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetSampleRate(uint32_t sr)
{
	if (sr >= 8000) s_sr = sr;
}

void FmMonShadowAddSamples(uint32_t n)
{
	s_cur += n;
}

void FmMonShadowWriteReg(unsigned addr, unsigned data)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 0;
	addr &= 0x1FF;
	data &= 0xFF;
	if (s_regs[addr] == (uint8_t)data
		&& addr != 0x28 && addr != 0x10 && addr != 0x100) {
		/* 同一値の再書き込みは dirty にしない（KSS の毎バッファ PSG ポーリング対策）
		   KEYON(0x28)/リズム(0x10)/ADPCM-B ctrl(0x100) は同一値でも意味があるので通す */
		LeaveCriticalSection(&s_cs);
		return;
	}
	s_regs[addr] = (uint8_t)data;
	MarkBit(addr);

	if (addr == 0x28) {
		/* KEY ON: bits 0-2 = slot ch, bit4-7 = slots; ch 0-2 bank0, 4-6 bank1 */
		const int ch = data & 0x07;
		const int on = (data & 0xF0) != 0;
		int fm = -1;
		if (s_opnaLayout == 2) {
			/* YM2610 mask 0x36: channels 1,2,4,5 → display FM1..FM4 */
			if (ch == 1) fm = 0;
			else if (ch == 2) fm = 1;
			else if (ch == 4) fm = 2;
			else if (ch == 5) fm = 3;
		} else if (ch <= 2) {
			fm = ch;
		} else if (ch >= 4 && ch <= 6) {
			fm = ch - 1; /* 4,5,6 -> 3,4,5 */
		}
		if (fm >= 0 && fm < 6) {
			if (on && !s_keyFm[fm]) s_hitFm[fm]++;
			s_keyFm[fm] = (uint8_t)(on ? 1 : 0);
			if (on) {
				int bank, slot;
				if (s_opnaLayout == 2) {
					static const int kB[4] = { 0, 0, 0x100, 0x100 };
					static const int kS[4] = { 1, 2, 0, 1 };
					bank = kB[fm];
					slot = kS[fm];
				} else {
					bank = (fm < 3) ? 0 : 0x100;
					slot = (fm < 3) ? fm : (fm - 3);
				}
				const int mid = MidiFromA4A0(s_regs[bank + 0xA4 + slot], s_regs[bank + 0xA0 + slot]);
				if (mid >= 0) s_midiFm[fm] = (uint8_t)mid;
			} else {
				s_midiFm[fm] = 0xFF;
			}
		}
	} else if (s_opnaLayout == 2 && addr == 0x10) {
		/* YM2610 bank0 $10 = ADPCM-B control (not OPNA rhythm). */
		const int on = (data & 0x80) != 0;
		const int rst = (data & 0x01) != 0;
		if (rst) {
			if (s_adpcmOn) s_dirty = 1;
			s_adpcmOn = 0;
			s_adpcmMidi = 0xFF;
			s_flushUrgent = 1;
		} else if (on) {
			s_adpcmHit++;
			s_adpcmSeen = 1;
			s_adpcmOn = 1;
			RefreshAdpcmMidi();
			s_dirty = 1;
			s_flushUrgent = 1;
		} else if (s_adpcmOn) {
			s_adpcmOn = 0;
			s_adpcmMidi = 0xFF;
			s_dirty = 1;
			s_flushUrgent = 1;
		}
	} else if (s_opnaLayout == 2 && (addr == 0x19 || addr == 0x1A)) {
		if (s_adpcmOn)
			RefreshAdpcmMidi();
	} else if (s_opnaLayout != 2 && addr == 0x10) {
		const uint8_t rising = (uint8_t)(data & ~s_rhyKey);
		s_rhyKey = (uint8_t)(data & 0x3F);
		s_rhyPulse = (uint8_t)(s_rhyPulse | rising);
		for (int i = 0; i < 6; i++) {
			if (rising & (1 << i)) s_hitRhy[i]++;
		}
	} else if (s_opnaLayout == 2 && addr == 0x100) {
		/* YM2610 bank1 $00 = ADPCM-A key-on bits 0..5 */
		for (int i = 0; i < 6; i++) {
			const int on = (data >> i) & 1;
			if (on && !s_pcmOn[i]) s_pcmHit[i]++;
			s_pcmOn[i] = (uint8_t)(on ? 1 : 0);
			s_pcmNote[i] = on ? (uint8_t)60 : (uint8_t)0xFF;
			if (on) s_adpcmASeen = 1;
		}
		if (s_adpcmASeen && s_pcmCount < 6) s_pcmCount = 6;
		s_dirty = 1;
		s_flushUrgent = 1;
	} else if (s_opnaLayout != 2 && addr >= 0x100 && addr <= 0x10F) {
		/* Bank1 0x00-0x0F = OPNA ADPCM-B (delta-T). Reg0 bit7 = execute. */
		if (addr == 0x100) {
			const int on = (data & 0x80) != 0;
			const int rst = (data & 0x01) != 0;
			if (rst) {
				if (s_adpcmOn) s_dirty = 1;
				s_adpcmOn = 0;
				s_adpcmMidi = 0xFF;
				s_flushUrgent = 1;
			} else if (on) {
				/* Re-key with same 0x80/0xA0 still counts — drivers often
				   rewrite execute without clearing first. */
				s_adpcmHit++;
				s_adpcmSeen = 1;
				s_adpcmOn = 1;
				RefreshAdpcmMidi();
				s_dirty = 1;
				s_flushUrgent = 1;
			} else if (s_adpcmOn) {
				s_adpcmOn = 0;
				s_adpcmMidi = 0xFF;
				s_dirty = 1;
				s_flushUrgent = 1;
			}
		} else if (addr == 0x109 || addr == 0x10A) {
			if (s_adpcmOn)
				RefreshAdpcmMidi();
		}
	} else if (addr < 0x10) {
		/* SSG: gate needs tone/noise enable AND level (vol>0 or envelope).
		   Period-only made SSG3 look stuck-on when drivers mute via R8-RA. */
		for (int i = 0; i < 3; i++) {
			const uint16_t per = (uint16_t)(s_regs[i * 2] | ((s_regs[i * 2 + 1] & 0x0F) << 8));
			const int mid = MidiFromSsgPeriod(per);
			const int toneOff = (s_regs[7] >> i) & 1;
			const int noiseOff = (s_regs[7] >> (3 + i)) & 1;
			const unsigned amp = s_regs[8 + i] & 0x1F;
			const int env = (amp & 0x10) != 0;
			const int level = env || ((amp & 0x0F) != 0);
			const int on = (((!toneOff && per != 0) || !noiseOff) && level) ? 1 : 0;
			if (on && !s_ssg[i]) s_hitSsg[i]++;
			s_ssg[i] = (uint8_t)on;
			s_midiSsg[i] = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
		}
	} else if ((addr & 0xFF) >= 0xA0 && (addr & 0xFF) <= 0xAE) {
		/* fnum update while keyed — refresh midi */
		const int fmMax = (s_opnaLayout == 2) ? 4 : 6;
		for (int fm = 0; fm < fmMax; fm++) {
			if (!s_keyFm[fm]) continue;
			int bank, slot;
			if (s_opnaLayout == 2) {
				static const int kB[4] = { 0, 0, 0x100, 0x100 };
				static const int kS[4] = { 1, 2, 0, 1 };
				bank = kB[fm];
				slot = kS[fm];
			} else {
				bank = (fm < 3) ? 0 : 0x100;
				slot = (fm < 3) ? fm : (fm - 3);
			}
			if ((int)(addr & ~0x100) != 0xA0 + slot && (int)(addr & ~0x100) != 0xA4 + slot)
				continue;
			const int mid = MidiFromA4A0(s_regs[bank + 0xA4 + slot], s_regs[bank + 0xA0 + slot]);
			if (mid >= 0) s_midiFm[fm] = (uint8_t)mid;
		}
	}
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowEnterKeysOnly(unsigned profile)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 1;
	s_oplMode = 0;
	s_oplSuppress = 1;
	s_msxDevMask = 0;
	s_snMode = 0;
	s_opmRegsValid = 0;
	s_opnaLayout = -1;
	s_keysProfile = profile & 0xFFu;
	s_auxRegsValid = 1; /* enable VIEW_REGS/PANELS for PC/AT soft modes */
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowWriteAuxReg(unsigned addr, unsigned data)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	addr &= 0x1FFu;
	data &= 0xFFu;
	if (s_regs[addr] != (uint8_t)data) {
		s_regs[addr] = (uint8_t)data;
		MarkBit(addr);
		s_dirty = 1;
	}
	s_auxRegsValid = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowMidiNote(int ch, int midiNote, int on)
{
	if (ch < 0 || ch >= 16) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 1;
	int urgent = 0;
	if (on && midiNote >= 0 && midiNote <= 127) {
		const uint8_t n = (uint8_t)midiNote;
		if (!s_midiChOn[ch] || s_midiChNote[ch] != n) {
			s_midiHit[ch]++;
			s_dirty = 1;
			/* 立ち上がり／ノート変更は即書き（minDirty 内の on→off で ON が消えないように） */
			s_flushUrgent = 1;
			urgent = 1;
		}
		s_midiChOn[ch] = 1;
		s_midiChNote[ch] = n;
	} else {
		if (s_midiChOn[ch]) {
			s_dirty = 1;
			s_flushUrgent = 1;
			urgent = 1;
		}
		s_midiChOn[ch] = 0;
		s_midiChNote[ch] = 0xFF;
	}
	LeaveCriticalSection(&s_cs);
	if (urgent)
		FmMonShadowFlushKeysOnly(0);
}

void FmMonShadowPcmNote(int ch, int midiNote, int on)
{
	if (ch < 0 || ch >= SASAMI_FMMON_PCM_MAX) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	/* Ensure arcade keys-only path stays armed (GX Reset+bind race).
	   Skip if s_opnaLayout >= 0 (hybrid OPN+PCM like YM2203+SegaPCM) to keep FM rows. */
	if (s_opnaLayout < 0 && (s_keysProfile == SASAMI_FMMON_KEYS_RF5C
		|| s_keysProfile == SASAMI_FMMON_KEYS_C352
		|| s_keysProfile == SASAMI_FMMON_KEYS_QSOUND
		|| s_keysProfile == SASAMI_FMMON_KEYS_SEGAPCM
		|| s_keysProfile == SASAMI_FMMON_KEYS_OKI)) {
		s_keysOnly = 1;
		if (s_pcmCount < (uint8_t)(ch + 1))
			s_pcmCount = (uint8_t)(ch + 1);
	}
	if (on && midiNote >= 0 && midiNote <= 127) {
		const uint8_t nm = (uint8_t)midiNote;
		if (!s_pcmOn[ch] || s_pcmNote[ch] != nm) {
			if (!s_pcmOn[ch]) s_pcmHit[ch]++;
			s_dirty = 1;
			s_flushUrgent = 1;
		}
		s_pcmOn[ch] = 1;
		s_pcmNote[ch] = nm;
		if (s_pcmCount < (uint8_t)(ch + 1))
			s_pcmCount = (uint8_t)(ch + 1);
	} else {
		if (s_pcmOn[ch]) {
			s_dirty = 1;
			s_flushUrgent = 1;
		}
		s_pcmOn[ch] = 0;
		s_pcmNote[ch] = 0xFF;
	}
	LeaveCriticalSection(&s_cs);
}

int FmMonShadowPitchRateToMidi(unsigned pitchRate)
{
	/* SPC/PS1/PS2 SPU: pitch は 14bit。0x1000 = 1.0× ネイティブ再生レート。
	   サンプルのルート音は不明なので相対レートとして C4(60) を基準にする。 */
	pitchRate &= 0x3FFFu;
	if (pitchRate < 1u) return -1;
	const double midi = 60.0 + 12.0 * (log((double)pitchRate / 4096.0) / log(2.0));
	int m = (int)floor(midi + 0.5);
	if (m < 0) m = 0;
	if (m > 127) m = 127;
	return m;
}

int FmMonShadowHzToMidi(double freqHz)
{
	/* Standard MIDI: A4=440Hz → note 69 (O5A with FmFormatNoteName).
	   Do NOT use nsfplay's 0x69 encoding here — that sits 3 octaves high
	   on the FM monitor piano (O8 instead of O5). */
	return MidiFromHz(freqHz);
}

static int MidiFromAdpcmDeltaN(unsigned deltaN)
{
	/* Relative playback rate (same idea as PitchRateToMidi):
	   ΔN 0x49BA ≈ 8 kHz @ 8 MHz master = unity → C4 (midi 60).
	   Absolute 440*ΔN/0x556A puts speech/SFX rates around O7–O8, which is
	   musically meaningless and sits at the far right of the piano. */
	if (deltaN < 16) return -1;
	const double midi = 60.0 + 12.0 * (log((double)deltaN / 18874.0) / log(2.0));
	int m = (int)floor(midi + 0.5);
	if (m < 21) m = 21;
	if (m > 108) m = 108;
	return m;
}

static void RefreshAdpcmMidi(void)
{
	unsigned dn;
	if (s_opnaLayout == 2)
		dn = (unsigned)s_regs[0x19] | ((unsigned)s_regs[0x1A] << 8);
	else
		dn = (unsigned)s_regs[0x109] | ((unsigned)s_regs[0x10A] << 8);
	const int mid = MidiFromAdpcmDeltaN(dn);
	s_adpcmMidi = (mid >= 0) ? (uint8_t)mid : (uint8_t)60;
}

static int ShouldWrite(int force)
{
	if (force) return 1;
	if (s_flushUrgent) return 1;
	if (s_lastWrite == 0) return 1;
	const uint64_t elapsed = s_cur - s_lastWrite;
	/* keys-only (PSF/SPC 等): 64sample 採取＋変化は ≥2ms で書き、心拍 10ms。
	   RING=512 で ~750ms ラグ分の履歴を確保しつつ解像度を上げる。
	   note 端点は s_flushUrgent で minDirty をバイパス。 */
	if (s_keysOnly) {
		const uint64_t minDirty = (uint64_t)s_sr * 2u / 1000u;
		const uint64_t heartbeat = (uint64_t)s_sr * 10u / 1000u;
		if (elapsed < minDirty) return 0;
		if (s_dirty) return 1;
		return (elapsed >= heartbeat) ? 1 : 0;
	}
	/* MSX / OPL: dirty だけだと張り付き → 心拍。通常 OPNA は dirty+4ms */
	const unsigned ms = (s_msxDevMask || s_oplMode) ? 10u : 4u;
	const uint64_t minStep = (uint64_t)s_sr * ms / 1000u;
	if (elapsed < minStep)
		return 0;
	if (s_msxDevMask || s_oplMode)
		return 1;
	return s_dirty ? 1 : 0;
}

static void FillCommon(SasamiFmMonDump* d)
{
	FmMonInitDump(d);
	d->sampleRate = s_sr;
	d->curSample = s_cur;
	d->seq = (uint32_t)(s_cur & 0xFFFFFFFFu);
	if (s_opnaLayout < 0) {
		d->padHit = 2;
		d->fm10 = 0;
	} else if (s_opnaLayout == 0) {
		d->padHit = 1; /* OPN FM×3+SSG×3 */
		d->fm10 = 0;
	} else if (s_opnaLayout == 2) {
		d->padHit = 6; /* YM2610 FM×4+SSG×3+ADPCM */
		d->fm10 = 0;
	} else {
		d->padHit = 2;
		d->fm10 = 1;
	}
	if (s_src[0])
		wcsncpy_s(d->sourcePath, s_src, _TRUNCATE);
}

static void FillIdentityTitle(SasamiFmMonDump* d, const char* chipExtras)
{
	if (!s_identPlat[0] && !s_identChip[0]) return;
	char buf[64];
	if (s_identPlat[0] && s_identChip[0])
		_snprintf_s(buf, _TRUNCATE, "%s  %s%s", s_identPlat, s_identChip,
			chipExtras ? chipExtras : "");
	else if (s_identPlat[0])
		_snprintf_s(buf, _TRUNCATE, "%s%s", s_identPlat, chipExtras ? chipExtras : "");
	else
		_snprintf_s(buf, _TRUNCATE, "%s%s", s_identChip, chipExtras ? chipExtras : "");
	strncpy_s(d->titleSjis, buf, _TRUNCATE);
}

static void RefreshOpmKeysFromRegs(void)
{
	/* YM2151: key $08 (ch + slots), KC $28+ch, KF $30+ch → midi approx */
	static const int kKcToNote[16] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 11, 11, 11, 11
	};
	for (int ch = 0; ch < 8; ch++) {
		const uint8_t kc = s_regs[0x28 + ch];
		const int oct = (kc >> 4) & 7;
		const int note = kKcToNote[kc & 0x0F];
		int mid = (oct + 1) * 12 + note;
		if (mid < 0) mid = 0;
		if (mid > 127) mid = 127;
		const int on = s_midiChOn[ch];
		if (on)
			s_midiChNote[ch] = (uint8_t)mid;
	}
}

void FmMonShadowFlush(int force)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (s_keysOnly) {
		LeaveCriticalSection(&s_cs);
		FmMonShadowFlushKeysOnly(force);
		return;
	}
	/* CEmu OPM (X1/X68k/AC): never fall through to OPNA-shaped dump.
	   Bind seeds empty snapshot; writes refresh via SetOpmRegSnapshot. */
	if (s_opnaLayout < 0 && !s_oplMode && !s_msxDevMask && !s_snMode
		&& (s_opmRegsValid || s_keysProfile == SASAMI_FMMON_KEYS_MDX)) {
		s_keysOnly = 1;
		s_keysProfile = SASAMI_FMMON_KEYS_MDX;
		if (s_opmRegsValid)
			RefreshOpmKeysFromRegs();
		LeaveCriticalSection(&s_cs);
		FmMonShadowFlushKeysOnly(force);
		EnsureCs();
		EnterCriticalSection(&s_cs);
		s_keysOnly = 0;
		LeaveCriticalSection(&s_cs);
		return;
	}
	if (!ShouldWrite(force)) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	SasamiFmMonDump d;
	FillCommon(&d);
	memcpy(d.regs, s_regs, sizeof(d.regs));
	memcpy(d.regWriteBits, s_bits, sizeof(d.regWriteBits));
	memcpy(d.keyOnFm, s_keyFm, 6);
	memcpy(d.keyOnHitCnt, s_hitFm, 6);
	memcpy(d.keyMidi, s_midiFm, 6);
	memcpy(d.keyOnEx, s_keyEx, 3);
	memcpy(d.keyOnExHitCnt, s_hitEx, 3);
	memcpy(d.exMidi, s_midiEx, 3);
	memcpy(d.ssgOn, s_ssg, 3);
	memcpy(d.ssgHitCnt, s_hitSsg, 3);
	memcpy(d.ssgMidi, s_midiSsg, 3);
	d.rhythmKey = s_rhyKey;
	d.rhythmPulse = s_rhyPulse;
	memcpy(d.rhythmHitCnt, s_hitRhy, 6);
	d.pcmCount = s_pcmCount;
	memcpy(d.pcmOn, s_pcmOn, sizeof(d.pcmOn));
	memcpy(d.pcmNote, s_pcmNote, sizeof(d.pcmNote));

	char extras[40];
	extras[0] = 0;
	int anyEx = 0;
	for (int i = 0; i < 3; i++)
		if (s_keyEx[i]) anyEx = 1;
	if (s_opnaLayout != 2 && (anyEx || (s_regs[0x27] & 0xC0) != 0)) {
		d.dumpFlags = (uint8_t)(d.dumpFlags | SASAMI_FMMON_FLAG_FM3EX);
		if (s_opnaLayout > 0)
			strncat_s(extras, "+EX", _TRUNCATE);
	}
	if (s_opnaLayout == 2) {
		/* YM2610: ADPCM-A (pcm[0..5]) + ADPCM-B (pcm[6] when active). */
		if (s_adpcmASeen) {
			if (s_pcmCount < 6) s_pcmCount = 6;
			d.pcmCount = s_pcmCount;
			memcpy(d.pcmOn, s_pcmOn, sizeof(d.pcmOn));
			memcpy(d.pcmNote, s_pcmNote, sizeof(d.pcmNote));
			strncat_s(extras, "+ADPCM-A", _TRUNCATE);
		}
		if (s_adpcmSeen) {
			d.dumpFlags = (uint8_t)(d.dumpFlags | SASAMI_FMMON_FLAG_ADPCM);
			const int bi = s_adpcmASeen ? 6 : 0;
			if (s_pcmCount <= bi) s_pcmCount = bi + 1;
			d.pcmCount = s_pcmCount;
			d.pcmOn[bi] = s_adpcmOn;
			d.pcmNote[bi] = s_adpcmOn ? s_adpcmMidi : (uint8_t)0xFF;
			strncat_s(extras, "+ADPCM-B", _TRUNCATE);
		}
		/* no OPNA rhythm row */
		d.rhythmKey = 0;
		d.rhythmPulse = 0;
	} else if (s_opnaLayout > 0 && s_adpcmSeen && !s_oplMode && !s_msxDevMask && !s_snMode) {
		/* OPNA ADPCM-B only when actually used (not every OPNA dump). */
		d.dumpFlags = (uint8_t)(d.dumpFlags | SASAMI_FMMON_FLAG_ADPCM);
		if (d.pcmCount < 1) d.pcmCount = 1;
		d.pcmOn[0] = s_adpcmOn;
		d.pcmNote[0] = s_adpcmOn ? s_adpcmMidi : (uint8_t)0xFF;
		strncat_s(extras, "+ADPCM", _TRUNCATE);
	}
	FmMonKeepProbeTags();
	if (s_oplMode) {
		d.dumpFlags = 0;
		d.pad6[0] = (uint8_t)s_oplMode;
		d.pad6[1] = (uint8_t)((s_oplMode >= 2)
			? SASAMI_FMMON_KEYS_OPL3 : SASAMI_FMMON_KEYS_OPL2);
		d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
			| SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS);
		d.fm10 = 0;
		d.padHit = 4; /* OPL marker */
		if (s_oplMode >= 2 && d.pcmCount < 9)
			d.pcmCount = 9;
		extras[0] = 0;
	} else if (s_msxDevMask) {
		d.dumpFlags = (uint8_t)(SASAMI_FMMON_FLAG_MSX
			| ((s_msxDevMask & SASAMI_FMMON_DEV_OPLL) ? SASAMI_FMMON_FLAG_FM3EX : 0));
		d.pad6[0] = (uint8_t)s_msxDevMask;
		d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
			| ((s_msxDevMask & (SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_OPLL
				| SASAMI_FMMON_DEV_SCC | SASAMI_FMMON_DEV_HES))
				? SASAMI_FMMON_VIEW_REGS : 0)
			| ((s_msxDevMask & SASAMI_FMMON_DEV_OPLL)
				? SASAMI_FMMON_VIEW_PANELS : 0));
		d.fm10 = 0;
		d.padHit = 3; /* MSX marker for head */
		extras[0] = 0;
	} else if (s_snMode) {
		d.dumpFlags = 0;
		d.fm10 = 0;
		d.padHit = 5; /* SN76489 */
		d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS | SASAMI_FMMON_VIEW_REGS);
		if (d.pcmCount < 1) d.pcmCount = 1;
		extras[0] = 0;
	} else {
		d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
			| SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS);
		/* Hybrid OPN+SegaPCM: tag profile so UI labels SPCM not CH×16. */
		if (s_keysProfile == SASAMI_FMMON_KEYS_SEGAPCM) {
			d.pad6[1] = (uint8_t)SASAMI_FMMON_KEYS_SEGAPCM;
			if (s_pcmCount > 0 && s_pcmCount <= 8)
				d.pcmCount = s_pcmCount;
			else if (s_opnaLayout == 0 && d.pcmCount < 8)
				d.pcmCount = 8;
		}
	}
	FillIdentityTitle(&d, extras);
	s_rhyPulse = 0;
	s_dirty = 0;
	s_lastWrite = s_cur;
	/* 区間内の書込ビットはダンプへ渡したらクリア。残すと UI が全レジスタ常時フェードになる */
	memset(s_bits, 0, sizeof(s_bits));
	LeaveCriticalSection(&s_cs);
	FmMonWriteDump(&d);
}

void FmMonShadowFlushKeysOnly(int force)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (!ShouldWrite(force)) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	SasamiFmMonDump d;
	FillCommon(&d);
	d.fm10 = 0;
	d.pad6[1] = (uint8_t)s_keysProfile;

	const unsigned prof = s_keysProfile;
	int pcmN = 16;
	switch (prof) {
	case SASAMI_FMMON_KEYS_SPC: pcmN = 8; break;
	case SASAMI_FMMON_KEYS_SID: pcmN = 3; break;
	case SASAMI_FMMON_KEYS_GSF: pcmN = 4; break;
	case SASAMI_FMMON_KEYS_MDX: pcmN = 8; break;
	case SASAMI_FMMON_KEYS_RF5C: pcmN = 8; break;
	case SASAMI_FMMON_KEYS_OKI: pcmN = 4; break;
	case SASAMI_FMMON_KEYS_QSOUND: pcmN = 16; break;
	case SASAMI_FMMON_KEYS_C352: pcmN = 32; break;
	case SASAMI_FMMON_KEYS_SEGAPCM:
		pcmN = (s_pcmCount > 0 && s_pcmCount <= 8) ? (int)s_pcmCount : 16;
		break;
	default: pcmN = 16; break;
	}
	const int arcade = ArcIsProfile(prof);

	/* Clear OPNA-shaped slots; non-MDX profiles use PCM rows only. */
	memset(d.keyOnFm, 0, sizeof(d.keyOnFm));
	memset(d.keyOnHitCnt, 0, sizeof(d.keyOnHitCnt));
	memset(d.keyMidi, 0xFF, sizeof(d.keyMidi));
	memset(d.keyOnEx, 0, sizeof(d.keyOnEx));
	memset(d.keyOnExHitCnt, 0, sizeof(d.keyOnExHitCnt));
	memset(d.exMidi, 0xFF, sizeof(d.exMidi));
	memset(d.ssgOn, 0, sizeof(d.ssgOn));
	memset(d.ssgHitCnt, 0, sizeof(d.ssgHitCnt));
	memset(d.ssgMidi, 0xFF, sizeof(d.ssgMidi));

	if (prof == SASAMI_FMMON_KEYS_MDX) {
		/* OPM ×8 → FM1-6 + EX1-2 (UI labels OPM1-8). Regs when snapshotted. */
		if (s_opmRegsValid) {
			d.dumpFlags = SASAMI_FMMON_FLAG_OPM;
			/* Include bank1 when GA20 (or other hybrid PCM) shadowed at $100+. */
			memcpy(d.regs, s_regs, s_ga20Seen ? 0x200 : 256);
			memcpy(d.regWriteBits, s_bits, sizeof(d.regWriteBits));
			d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
				| SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS);
		} else {
			d.dumpFlags = (uint8_t)(SASAMI_FMMON_FLAG_KEYSONLY | SASAMI_FMMON_FLAG_OPM);
			d.pad6[2] = SASAMI_FMMON_VIEW_KEYS;
			if (s_ga20Seen) {
				memcpy(d.regs + 0x100, s_regs + 0x100, 0x20);
				d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
					| SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS);
			}
		}
		for (int i = 0; i < 6; i++) {
			d.keyOnFm[i] = s_midiChOn[i];
			d.keyMidi[i] = s_midiChOn[i] ? s_midiChNote[i] : (uint8_t)0xFF;
			d.keyOnHitCnt[i] = s_midiHit[i];
		}
		for (int i = 0; i < 2; i++) {
			const int ch = 6 + i;
			d.keyOnEx[i] = s_midiChOn[ch];
			d.exMidi[i] = s_midiChOn[ch] ? s_midiChNote[ch] : (uint8_t)0xFF;
			d.keyOnExHitCnt[i] = s_midiHit[ch];
		}
		/* X1 OPM+AY: keep SSG gates from AY shadow */
		for (int i = 0; i < 3; i++) {
			d.ssgOn[i] = s_ssg[i];
			d.ssgMidi[i] = s_ssg[i] ? s_midiSsg[i] : (uint8_t)0xFF;
			d.ssgHitCnt[i] = s_hitSsg[i];
		}
		/* PDX / SegaPCM / GA20 keys alongside OPM */
		int anyPdx = 0;
		int pcmCap = 8;
		if (s_ga20Seen && s_pcmCount < 4) s_pcmCount = 4;
		if (s_pcmCount > 0 && s_pcmCount <= 8) pcmCap = (int)s_pcmCount;
		if (s_pcmCount > 8) pcmCap = (s_pcmCount > 16) ? 16 : (int)s_pcmCount;
		for (int i = 0; i < pcmCap; i++) {
			d.pcmOn[i] = s_pcmOn[i];
			d.pcmNote[i] = s_pcmOn[i] ? s_pcmNote[i] : (uint8_t)0xFF;
			if (d.pcmOn[i]) anyPdx = 1;
		}
		if (anyPdx || s_pcmCount > 0)
			d.pcmCount = (uint8_t)pcmCap;
	} else {
		d.dumpFlags = SASAMI_FMMON_FLAG_KEYSONLY;
		const int softRegs = (s_auxRegsValid || (arcade && s_arcRegsValid)) ? 1 : 0;
		d.pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
			| (softRegs ? (SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS) : 0));
		if (softRegs) {
			memcpy(d.regs, s_regs, sizeof(d.regs));
			memcpy(d.regWriteBits, s_bits, sizeof(d.regWriteBits));
		}
		for (int i = 0; i < pcmN; i++) {
			if (arcade) {
				d.pcmOn[i] = s_pcmOn[i];
				d.pcmNote[i] = s_pcmOn[i] ? s_pcmNote[i] : (uint8_t)0xFF;
			} else {
				d.pcmOn[i] = s_midiChOn[i];
				d.pcmNote[i] = s_midiChOn[i] ? s_midiChNote[i] : (uint8_t)0xFF;
			}
		}
		d.pcmCount = (uint8_t)pcmN;
		/* Mirror first 6 MIDI ch into FM key slots so OPNA-shell panels
		   (PC/AT BEEP/CMS) light up when VIEW_PANELS is set. */
		if (s_auxRegsValid && !arcade) {
			for (int i = 0; i < 6; i++) {
				d.keyOnFm[i] = s_midiChOn[i];
				d.keyMidi[i] = s_midiChOn[i] ? s_midiChNote[i] : (uint8_t)0xFF;
				d.keyOnHitCnt[i] = s_midiHit[i];
			}
			for (int i = 0; i < 3; i++) {
				const int ch = 6 + i;
				d.keyOnEx[i] = s_midiChOn[ch];
				d.exMidi[i] = s_midiChOn[ch] ? s_midiChNote[ch] : (uint8_t)0xFF;
				d.keyOnExHitCnt[i] = s_midiHit[ch];
			}
		}
		/* PCM 行に hit 欄が無いので既存カウンタへパッキング。
		   UI は KEYSONLY 時にこれを fadePcm へマップ（短い 16 分の取りこぼし防止）。
		   0-5→keyOnHitCnt  6-8→ssgHitCnt  9-11→keyOnExHitCnt  12-15→rhythmHitCnt */
		for (int i = 0; i < pcmN && i < 16; i++) {
			const uint8_t hit = arcade ? s_pcmHit[i] : s_midiHit[i];
			if (i < 6)
				d.keyOnHitCnt[i] = hit;
			else if (i < 9)
				d.ssgHitCnt[i - 6] = hit;
			else if (i < 12)
				d.keyOnExHitCnt[i - 9] = hit;
			else
				d.rhythmHitCnt[i - 12] = hit;
		}
	}

	FmMonKeepProbeTags();
	FillIdentityTitle(&d, "");
	s_dirty = 0;
	s_flushUrgent = 0;
	s_lastWrite = s_cur;
	if ((arcade && s_arcRegsValid) || s_auxRegsValid)
		memset(s_bits, 0, sizeof(s_bits));
	LeaveCriticalSection(&s_cs);
	FmMonWriteDump(&d);
}

void FmMonShadowSetSsgClock(unsigned clockHz)
{
	if (clockHz >= 1000000 && clockHz <= 20000000)
		s_ssgClock = clockHz;
}

void FmMonShadowDebugSsg(unsigned char regs16[16], unsigned char ssgOn[3],
	unsigned char ssgMidi[3])
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (regs16) memcpy(regs16, s_regs, 16);
	if (ssgOn) memcpy(ssgOn, s_ssg, 3);
	if (ssgMidi) memcpy(ssgMidi, s_midiSsg, 3);
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowWriteAyReg(unsigned reg, unsigned data)
{
	/* AY-3-8910 / YM2149 → same SSG shadow as OPNA bank0 $00-$0F */
	reg &= 0x0Fu;
	data &= 0xFFu;
	s_aySeen = 1;
	FmMonShadowWriteReg(reg, data);
}

void FmMonShadowSetMsxDevices(unsigned deviceMask)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	const unsigned m = deviceMask & 0xFFu;
	if (s_msxDevMask != m) {
		s_msxDevMask = m;
		s_dirty = 1;
	}
	s_keysOnly = 0;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyOpllRegs(const unsigned char* reg64)
{
	if (!reg64) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 0;
	/* Keep MSX device mask alive across incidental Reset/profile changes so
	   OPLL key dumps stay on the MSX keyboard path (quinpl_msx). */
	if (!(s_msxDevMask & SASAMI_FMMON_DEV_OPLL)) {
		s_msxDevMask |= (unsigned)(SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_OPLL);
		s_dirty = 1;
	}
	if (s_ssgClock == 0 || s_ssgClock == 7987200u)
		s_ssgClock = 3579545u;
	int changed = 0;
	for (int i = 0; i < 64; i++) {
		if (s_regs[0x40 + i] == reg64[i]) continue;
		s_regs[0x40 + i] = reg64[i];
		MarkBit(0x40 + i);
		changed = 1;
	}
	const int rhythm = (reg64[0x0e] & 0x20) != 0;
	for (int ch = 0; ch < 9; ch++) {
		const unsigned r20 = reg64[0x20 + ch];
		const unsigned fnum = (unsigned)reg64[0x10 + ch] | ((r20 & 1u) << 8);
		const unsigned block = (r20 >> 1) & 7u;
		int on = (r20 & 0x10) != 0;
		if (rhythm && ch >= 6)
			on = 0;
		const int mid = on ? MidiFromOpll(fnum, block) : -1;
		if (ch < 6) {
			const uint8_t nk = (uint8_t)(on ? 1 : 0);
			const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
			if (nk != s_keyFm[ch] || nm != s_midiFm[ch]) changed = 1;
			if (on && !s_keyFm[ch]) s_hitFm[ch]++;
			s_keyFm[ch] = nk;
			s_midiFm[ch] = nm;
		} else {
			const int ex = ch - 6;
			const uint8_t nk = (uint8_t)(on ? 1 : 0);
			const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
			if (nk != s_keyEx[ex] || nm != s_midiEx[ex]) changed = 1;
			if (on && !s_keyEx[ex]) s_hitEx[ex]++;
			s_keyEx[ex] = nk;
			s_midiEx[ex] = nm;
		}
	}
	if (rhythm) {
		const uint8_t rk = (uint8_t)(reg64[0x0e] & 0x1F);
		const uint8_t rising = (uint8_t)(rk & ~s_rhyKey);
		if (rk != s_rhyKey || rising) changed = 1;
		s_rhyKey = rk;
		s_rhyPulse = (uint8_t)(s_rhyPulse | rising);
		for (int i = 0; i < 5; i++) {
			if (rising & (1 << i)) s_hitRhy[i]++;
		}
	}
	if (changed) s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyScc(const unsigned* freq12, const unsigned* vol4, unsigned enableMask)
{
	if (!freq12 || !vol4) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 0;
	s_pcmCount = 5;
	int changed = 0;
	/* Konami SCC control mirror @ dump+$80 (waveforms omitted; freq/vol/on only) */
	for (int i = 0; i < 5; i++) {
		const unsigned per = freq12[i] & 0xFFFu;
		const unsigned vol = vol4[i] & 0xFu;
		const uint8_t lo = (uint8_t)(per & 0xFF);
		const uint8_t hi = (uint8_t)((per >> 8) & 0x0F);
		const uint8_t vv = (uint8_t)vol;
		if (s_regs[0x80 + i * 2] != lo) {
			s_regs[0x80 + i * 2] = lo;
			MarkBit(0x80 + i * 2);
			changed = 1;
		}
		if (s_regs[0x81 + i * 2] != hi) {
			s_regs[0x81 + i * 2] = hi;
			MarkBit(0x81 + i * 2);
			changed = 1;
		}
		if (s_regs[0x8A + i] != vv) {
			s_regs[0x8A + i] = vv;
			MarkBit(0x8A + i);
			changed = 1;
		}
		const int en = (enableMask & (1u << i)) != 0;
		const int on = (en && vol > 0 && per > 8) ? 1 : 0;
		int mid = -1;
		if (on) {
			const double hz = (double)s_ssgClock / ((double)(per + 1) * 32.0);
			mid = MidiFromHz(hz);
		}
		const uint8_t nk = (uint8_t)(on ? 1 : 0);
		const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
		if (nk != s_pcmOn[i] || nm != s_pcmNote[i]) changed = 1;
		if (on && !s_pcmOn[i]) s_pcmHit[i]++;
		s_pcmOn[i] = nk;
		s_pcmNote[i] = nm;
	}
	{
		const uint8_t en = (uint8_t)(enableMask & 0x1Fu);
		if (s_regs[0x8F] != en) {
			s_regs[0x8F] = en;
			MarkBit(0x8F);
			changed = 1;
		}
	}
	for (int i = 5; i < SASAMI_FMMON_PCM_MAX; i++) {
		if (s_pcmOn[i] || s_pcmNote[i] != 0xFF) changed = 1;
		s_pcmOn[i] = 0;
		s_pcmNote[i] = 0xFF;
	}
	if (changed) s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyHes(const unsigned* period12, const unsigned* vol5, const unsigned* control)
{
	if (!period12 || !vol5 || !control) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 0;
	if (s_ssgClock == 0 || s_ssgClock == 7987200u)
		s_ssgClock = 3579545u;
	const unsigned want = SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_HES;
	if (s_msxDevMask != want) {
		s_msxDevMask = want;
		s_dirty = 1;
	}
	int changed = 0;
	unsigned mix = 0x38;
	for (int i = 0; i < 3; i++) {
		const unsigned per = period12[i] & 0xFFFu;
		const unsigned vol = vol5[i] & 0x1Fu;
		const int on = ((control[i] & 0x80) != 0 && vol > 0 && per > 0) ? 1 : 0;
		const uint8_t lo = (uint8_t)(per & 0xFF);
		const uint8_t hi = (uint8_t)((per >> 8) & 0x0F);
		if (s_regs[i * 2] != lo || s_regs[i * 2 + 1] != hi) {
			s_regs[i * 2] = lo;
			s_regs[i * 2 + 1] = hi;
			changed = 1;
		}
		const uint8_t vv = (uint8_t)(vol & 0x1F);
		if (s_regs[8 + i] != vv) {
			s_regs[8 + i] = vv;
			MarkBit(8 + i);
			changed = 1;
		}
		int mid = -1;
		if (on) {
			const double hz = (double)s_ssgClock / ((double)(per + 1) * 32.0);
			mid = MidiFromHz(hz);
		}
		const uint8_t nk = (uint8_t)(on ? 1 : 0);
		const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
		if (nk != s_ssg[i] || nm != s_midiSsg[i]) changed = 1;
		if (on && !s_ssg[i]) s_hitSsg[i]++;
		s_ssg[i] = nk;
		s_midiSsg[i] = nm;
		if (!on) mix |= (1u << i);
	}
	if (s_regs[7] != (uint8_t)mix) {
		s_regs[7] = (uint8_t)mix;
		changed = 1;
	}
	s_pcmCount = 3;
	for (int i = 0; i < 3; i++) {
		const int ch = i + 3;
		const unsigned per = period12[ch] & 0xFFFu;
		const unsigned vol = vol5[ch] & 0x1Fu;
		const int on = ((control[ch] & 0x80) != 0 && vol > 0 && per > 0) ? 1 : 0;
		const uint8_t lo = (uint8_t)(per & 0xFF);
		const uint8_t hi = (uint8_t)((per >> 8) & 0x0F);
		const uint8_t vv = (uint8_t)(vol & 0x1F);
		const uint8_t ctl = (uint8_t)(control[ch] & 0xFF);
		/* HuC ch4-6 mirror @+$90 */
		if (s_regs[0x90 + i * 2] != lo) {
			s_regs[0x90 + i * 2] = lo;
			MarkBit(0x90 + i * 2);
			changed = 1;
		}
		if (s_regs[0x91 + i * 2] != hi) {
			s_regs[0x91 + i * 2] = hi;
			MarkBit(0x91 + i * 2);
			changed = 1;
		}
		if (s_regs[0x96 + i] != vv) {
			s_regs[0x96 + i] = vv;
			MarkBit(0x96 + i);
			changed = 1;
		}
		if (s_regs[0x99 + i] != ctl) {
			s_regs[0x99 + i] = ctl;
			MarkBit(0x99 + i);
			changed = 1;
		}
		int mid = -1;
		if (on) {
			const double hz = (double)s_ssgClock / ((double)(per + 1) * 32.0);
			mid = MidiFromHz(hz);
		}
		const uint8_t nk = (uint8_t)(on ? 1 : 0);
		const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
		if (nk != s_pcmOn[i] || nm != s_pcmNote[i]) changed = 1;
		if (on && !s_pcmOn[i]) s_pcmHit[i]++;
		s_pcmOn[i] = nk;
		s_pcmNote[i] = nm;
	}
	for (int i = 3; i < SASAMI_FMMON_PCM_MAX; i++) {
		if (s_pcmOn[i] || s_pcmNote[i] != 0xFF) changed = 1;
		s_pcmOn[i] = 0;
		s_pcmNote[i] = 0xFF;
	}
	if (changed) s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplySn76489(const unsigned* tonePeriod10, unsigned noisePeriod,
	const unsigned* vol4, unsigned onMask)
{
	if (!tonePeriod10 || !vol4) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	s_keysOnly = 0;
	s_snMode = 1;
	s_opnaLayout = -1;
	if (s_ssgClock == 0 || s_ssgClock == 7987200u)
		s_ssgClock = 3579545u;
	int changed = 0;
	unsigned mix = 0x38;
	for (int i = 0; i < 3; i++) {
		const unsigned per = tonePeriod10[i] & 0x3FFu;
		const unsigned atten = vol4[i] & 0xFu;
		const int on = ((onMask & (1u << i)) != 0 && atten < 15 && per > 0) ? 1 : 0;
		const uint8_t lo = (uint8_t)(per & 0xFF);
		const uint8_t hi = (uint8_t)((per >> 8) & 0x0F);
		if (s_regs[i * 2] != lo || s_regs[i * 2 + 1] != hi) {
			s_regs[i * 2] = lo;
			s_regs[i * 2 + 1] = hi;
			changed = 1;
		}
		const uint8_t v = on ? (uint8_t)(15 - atten) : (uint8_t)0;
		if (s_regs[8 + i] != v) { s_regs[8 + i] = v; changed = 1; }
		if (!on) mix |= (1u << i);
		int mid = -1;
		if (on) {
			const double hz = (double)s_ssgClock / (16.0 * (double)per);
			mid = MidiFromHz(hz);
		}
		const uint8_t nk = (uint8_t)(on ? 1 : 0);
		const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
		if (nk != s_ssg[i] || nm != s_midiSsg[i]) changed = 1;
		if (on && !s_ssg[i]) s_hitSsg[i]++;
		s_ssg[i] = nk;
		s_midiSsg[i] = nm;
	}
	if (s_regs[7] != (uint8_t)mix) { s_regs[7] = (uint8_t)mix; changed = 1; }
	/* noise → pcm[0] */
	{
		const unsigned atten = vol4[3] & 0xFu;
		const unsigned per = noisePeriod & 0x3FFu;
		const int on = ((onMask & 8) != 0 && atten < 15) ? 1 : 0;
		int mid = -1;
		if (on && per > 0) {
			const double hz = (double)s_ssgClock / (16.0 * (double)(per ? per : 1));
			mid = MidiFromHz(hz);
		} else if (on) {
			mid = 36; /* kick-ish placeholder for periodic noise */
		}
		const uint8_t nk = (uint8_t)(on ? 1 : 0);
		const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
		if (nk != s_pcmOn[0] || nm != s_pcmNote[0]) changed = 1;
		if (on && !s_pcmOn[0]) s_pcmHit[0]++;
		s_pcmOn[0] = nk;
		s_pcmNote[0] = nm;
		if (s_pcmCount < 1) s_pcmCount = 1;
	}
	if (changed) s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowWriteSnByte(unsigned data)
{
	data &= 0xFFu;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (data & 0x80) {
		s_snLatch = (data >> 4) & 7u;
		const unsigned ch = s_snLatch >> 1;
		if (s_snLatch & 1) {
			/* volume */
			if (ch < 4) s_snVol[ch] = data & 0x0Fu;
		} else if (ch < 3) {
			s_snTone[ch] = (s_snTone[ch] & 0x3F0u) | (data & 0x0Fu);
		} else {
			s_snNoise = (s_snNoise & 0x3F0u) | (data & 0x0Fu);
		}
	} else {
		const unsigned ch = s_snLatch >> 1;
		if (!(s_snLatch & 1)) {
			if (ch < 3)
				s_snTone[ch] = (s_snTone[ch] & 0x0Fu) | ((data & 0x3Fu) << 4);
			else
				s_snNoise = (s_snNoise & 0x0Fu) | ((data & 0x3Fu) << 4);
		} else if (ch < 4) {
			s_snVol[ch] = data & 0x0Fu;
		}
	}
	unsigned onMask = 0;
	for (int i = 0; i < 3; i++)
		if (s_snVol[i] < 15) onMask |= (1u << i);
	if (s_snVol[3] < 15) onMask |= 8u;
	unsigned tone[3] = { s_snTone[0], s_snTone[1], s_snTone[2] };
	unsigned vol[4] = { s_snVol[0], s_snVol[1], s_snVol[2], s_snVol[3] };
	const unsigned noisePer = s_snNoise;
	LeaveCriticalSection(&s_cs);
	FmMonShadowApplySn76489(tone, noisePer, vol, onMask);
}

static int MidiFromOpl(unsigned fnum, unsigned block)
{
	/* YM3812/YMF262: Fout ≈ fnum * clock * 2^block / (72 * 2^20) */
	fnum &= 0x3FFu;
	block &= 7u;
	if (fnum == 0) return -1;
	const double freq = (double)fnum * (double)s_ssgClock * (double)(1u << block)
		/ (72.0 * 1048576.0);
	return MidiFromHz(freq);
}

static void OplApplyChannelSlot(int idx, int on, int mid)
{
	const uint8_t nk = (uint8_t)(on ? 1 : 0);
	const uint8_t nm = (on && mid >= 0) ? (uint8_t)mid : (uint8_t)0xFF;
	if (idx < 6) {
		if (on && !s_keyFm[idx]) s_hitFm[idx]++;
		s_keyFm[idx] = nk;
		s_midiFm[idx] = nm;
	} else if (idx < 9) {
		const int ex = idx - 6;
		if (on && !s_keyEx[ex]) s_hitEx[ex]++;
		s_keyEx[ex] = nk;
		s_midiEx[ex] = nm;
	} else if (idx < 18) {
		const int pcm = idx - 9;
		if (on && !s_pcmOn[pcm]) s_pcmHit[pcm]++;
		s_pcmOn[pcm] = nk;
		s_pcmNote[pcm] = nm;
		if (s_pcmCount < (uint8_t)(pcm + 1))
			s_pcmCount = (uint8_t)(pcm + 1);
	}
}

static void OplRefreshKeysFromRegs(void)
{
	const int banks = (s_oplMode >= 2) ? 2 : 1;
	for (int b = 0; b < banks; b++) {
		const unsigned base = (unsigned)b * 0x100u;
		const uint8_t bd = s_regs[base + 0xBD];
		const int rhythm = (b == 0) && ((bd & 0x20) != 0);
		for (int ch = 0; ch < 9; ch++) {
			const uint8_t a0 = s_regs[base + 0xA0 + ch];
			const uint8_t b0 = s_regs[base + 0xB0 + ch];
			const unsigned fnum = (unsigned)a0 | ((unsigned)(b0 & 3) << 8);
			const unsigned block = (unsigned)(b0 >> 2) & 7u;
			int on = (b0 & 0x20) != 0;
			if (rhythm && ch >= 6)
				on = 0;
			const int mid = on ? MidiFromOpl(fnum, block) : -1;
			OplApplyChannelSlot(b * 9 + ch, on, mid);
		}
		if (rhythm) {
			const uint8_t rk = (uint8_t)(bd & 0x1F);
			const uint8_t rising = (uint8_t)(rk & ~s_rhyKey);
			s_rhyKey = rk;
			s_rhyPulse = (uint8_t)(s_rhyPulse | rising);
			for (int i = 0; i < 5; i++) {
				if (rising & (1 << i)) s_hitRhy[i]++;
			}
		}
	}
	if (s_oplMode >= 2 && s_pcmCount < 9)
		s_pcmCount = 9;
}

void FmMonShadowSetOplMode(unsigned mode)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	/* 1=OPL2 2=OPL3 3=DualOPL2 */
	if (mode > 3) mode = 0;
	if (s_oplMode != mode) {
		s_oplMode = mode;
		s_dirty = 1;
	}
	if (mode) {
		s_keysOnly = 0;
		s_oplSuppress = 0;
		s_opmRegsValid = 0;
		s_msxDevMask = 0;
		s_auxRegsValid = 0;
		s_ssgClock = 3579545;
		s_keysProfile = (mode >= 2)
			? SASAMI_FMMON_KEYS_OPL3 : SASAMI_FMMON_KEYS_OPL2;
	}
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowWriteOplReg(unsigned addr, unsigned data)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	if (s_oplSuppress) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	if (!s_oplMode) {
		s_oplMode = 1; /* default YM3812 / AdLib-SB FM */
		s_keysOnly = 0;
		s_ssgClock = 3579545;
		s_keysProfile = SASAMI_FMMON_KEYS_OPL2;
	}
	addr &= 0x1FF;
	data &= 0xFF;
	const unsigned r = addr & 0xFF;
	if (s_regs[addr] == (uint8_t)data && r != 0xBD
		&& !(r >= 0xB0 && r <= 0xB8)) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	s_regs[addr] = (uint8_t)data;
	MarkBit(addr);
	if ((r >= 0xA0 && r <= 0xB8) || r == 0xBD)
		OplRefreshKeysFromRegs();
	s_dirty = 1;
	LeaveCriticalSection(&s_cs);
}

/* --- Arcade PCM (QSound / RF5C / C352 / SegaPCM / OKI) ------------------- */

static void ArcEnterKeys(unsigned profile, int pcmN)
{
	/* Composite boards (Sys32 YM2612+RF5C68) already have an FM layout.
	   Overlay PCM rows instead of wiping the OPN dump into keys-only. */
	if (s_opnaLayout >= 0) {
		s_keysProfile = profile;
		if (s_pcmCount < (uint8_t)pcmN)
			s_pcmCount = (uint8_t)pcmN;
		s_dirty = 1;
		return;
	}
	s_keysOnly = 1;
	s_opnaLayout = -1;
	s_oplMode = 0;
	s_msxDevMask = 0;
	s_snMode = 0;
	s_keysProfile = profile;
	if (s_pcmCount < (uint8_t)pcmN)
		s_pcmCount = (uint8_t)pcmN;
}

static int ArcIsProfile(unsigned profile)
{
	return (profile == SASAMI_FMMON_KEYS_QSOUND
		|| profile == SASAMI_FMMON_KEYS_RF5C
		|| profile == SASAMI_FMMON_KEYS_C352
		|| profile == SASAMI_FMMON_KEYS_SEGAPCM
		|| profile == SASAMI_FMMON_KEYS_OKI) ? 1 : 0;
}

static void ArcMarkReg(unsigned idx, uint8_t data)
{
	idx &= 0x1FFu;
	s_regs[idx] = data;
	MarkBit(idx);
	s_arcRegsValid = 1;
	s_dirty = 1;
}

static void ArcMarkReg16HiLo(unsigned idx, unsigned data16)
{
	idx &= 0x1FFu;
	ArcMarkReg(idx, (uint8_t)((data16 >> 8) & 0xFFu));
	ArcMarkReg((idx + 1u) & 0x1FFu, (uint8_t)(data16 & 0xFFu));
}

static void ArcMarkReg16LoHi(unsigned idx, unsigned data16)
{
	idx &= 0x1FFu;
	ArcMarkReg(idx, (uint8_t)(data16 & 0xFFu));
	ArcMarkReg((idx + 1u) & 0x1FFu, (uint8_t)((data16 >> 8) & 0xFFu));
}

static void ArcSetPcm(int ch, int on, int midi)
{
	if (ch < 0 || ch >= SASAMI_FMMON_PCM_MAX) return;
	const uint8_t nk = (uint8_t)(on ? 1 : 0);
	const uint8_t nm = (on && midi >= 0) ? (uint8_t)midi : (uint8_t)0xFF;
	if (on && (!s_pcmOn[ch] || s_pcmNote[ch] != nm)) s_pcmHit[ch]++;
	if (ch < 16 && on && (!s_midiChOn[ch] || s_midiChNote[ch] != nm))
		s_midiHit[ch]++;
	if (nk != s_pcmOn[ch] || nm != s_pcmNote[ch]) s_dirty = 1;
	s_pcmOn[ch] = nk;
	s_pcmNote[ch] = nm;
	if (ch < 16) {
		s_midiChOn[ch] = nk;
		s_midiChNote[ch] = nm;
	}
	s_flushUrgent = 1;
}

void FmMonShadowApplyQSoundReg(unsigned ofs, unsigned data16)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	/* Force keys-only QSound panel — bind sets profile but may leave
	   s_keysOnly clear until the first DSP write (ts2 Open). */
	s_keysOnly = 1;
	s_opnaLayout = -1;
	s_oplMode = 0;
	s_msxDevMask = 0;
	s_snMode = 0;
	ArcEnterKeys(SASAMI_FMMON_KEYS_QSOUND, 16);
	ofs &= 0xFFu;
	data16 &= 0xFFFFu;
	if (ofs >= 0x80u)
		ArcMarkReg16HiLo((ofs * 2u) & 0x1FFu, data16);
	if (ofs < 0x80) {
		const int ch = (int)(ofs >> 3);
		const int r = (int)(ofs & 7);
		if (ch < 16) {
			const unsigned idx = ((unsigned)ch * 16u + (unsigned)r * 2u) & 0x1FFu;
			ArcMarkReg16HiLo(idx, data16);
			if (r == 2) {
				s_qsPitch[ch] = data16;
				if (s_pcmOn[ch]) {
					const int mid = FmMonShadowPitchRateToMidi(data16 >> 2);
					if (mid >= 0) s_pcmNote[ch] = (uint8_t)mid;
					s_dirty = 1;
				}
			} else if (r == 3) {
				/* Phase: bit15 often marks key-on; 0 = off in many cores */
				const int on = (data16 & 0x8000) != 0 || data16 != 0;
				int mid = 60;
				if (s_qsPitch[ch]) {
					const int m = FmMonShadowPitchRateToMidi(s_qsPitch[ch] >> 2);
					if (m >= 0) mid = m;
				}
				ArcSetPcm(ch, on ? 1 : 0, mid);
			} else if (r == 6) {
				/* Volume 0 → treat as note-off hint when already silent-ish */
				if (data16 == 0 && s_pcmOn[ch])
					ArcSetPcm(ch, 0, -1);
			}
		}
	}
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyRf5cReg(unsigned ofs, unsigned data8)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	ArcEnterKeys(SASAMI_FMMON_KEYS_RF5C, 8);
	ofs &= 0x7Fu;
	data8 &= 0xFFu;
	ArcMarkReg(ofs, (uint8_t)data8);
	if (ofs == 0x07) {
		/* low nibble = channel when bit6 clear; also stores enable bank */
		if (!(data8 & 0x40))
			s_rf5cCh = data8 & 7u;
	} else if (ofs == 0x08) {
		s_rf5cEnable = data8;
		for (int ch = 0; ch < 8; ch++) {
			const int on = (s_rf5cEnable & (1u << ch)) != 0 && s_rf5cVol[ch] > 0;
			int mid = 60;
			if (s_rf5cPitch[ch]) {
				const int m = FmMonShadowPitchRateToMidi(s_rf5cPitch[ch]);
				if (m >= 0) mid = m;
			}
			ArcSetPcm(ch, on, mid);
		}
	} else if (ofs <= 0x06) {
		const int ch = (int)s_rf5cCh;
		if (ch < 8) {
			if (ofs == 0x02 || ofs == 0x03) {
				/* pitch low/high */
				if (ofs == 0x02)
					s_rf5cPitch[ch] = (s_rf5cPitch[ch] & 0xFF00u) | data8;
				else
					s_rf5cPitch[ch] = (s_rf5cPitch[ch] & 0x00FFu) | ((unsigned)data8 << 8);
				if (s_pcmOn[ch]) {
					const int m = FmMonShadowPitchRateToMidi(s_rf5cPitch[ch]);
					if (m >= 0) { s_pcmNote[ch] = (uint8_t)m; s_dirty = 1; }
				}
			} else if (ofs == 0x00) {
				s_rf5cVol[ch] = data8;
				const int on = (s_rf5cEnable & (1u << ch)) != 0 && data8 > 0;
				int mid = 60;
				if (s_rf5cPitch[ch]) {
					const int m = FmMonShadowPitchRateToMidi(s_rf5cPitch[ch]);
					if (m >= 0) mid = m;
				}
				ArcSetPcm(ch, on, mid);
			}
		}
	}
	LeaveCriticalSection(&s_cs);
}

/* Dual MultiPCM (daytona): chip0 → PCM rows 0-7, chip1 → 8-15. */
void FmMonShadowApplyMultiPcm(int chipId, unsigned port, unsigned data8)
{
	static uint8_t s_slot[2];
	static uint8_t s_reg[2];
	static uint8_t s_regs[2][28][8];
	if (chipId < 0 || chipId > 1) return;
	EnsureCs();
	EnterCriticalSection(&s_cs);
	ArcEnterKeys(SASAMI_FMMON_KEYS_RF5C, 16);
	port &= 3u;
	data8 &= 0xFFu;
	if (port == 1) {
		s_slot[chipId] = (uint8_t)(data8 & 0x1fu);
		LeaveCriticalSection(&s_cs);
		return;
	}
	if (port == 2) {
		s_reg[chipId] = (uint8_t)((data8 > 7u) ? 7u : data8);
		LeaveCriticalSection(&s_cs);
		return;
	}
	if (port != 0) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	const int slot = (int)s_slot[chipId];
	const int reg = (int)s_reg[chipId];
	if (slot >= 28) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	s_regs[chipId][slot][reg] = (uint8_t)data8;
	if (reg == 4) {
		const int pcmCh = chipId * 8 + (slot & 7);
		const int on = (data8 & 0x80) != 0;
		/* Octave in reg3[7:4], coarse pitch in reg2/3 — map to a rough MIDI. */
		const unsigned oct = (s_regs[chipId][slot][3] >> 4) & 0x0fu;
		const unsigned fns = ((unsigned)(s_regs[chipId][slot][3] & 0x0f) << 6)
			| ((unsigned)s_regs[chipId][slot][2] >> 2);
		int mid = 48 + (int)oct * 12 + (int)(fns / 85u);
		if (mid < 12) mid = 12;
		if (mid > 108) mid = 108;
		ArcSetPcm(pcmCh, on, mid);
	}
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowSetC352Clock(unsigned clockHz)
{
	if (clockHz >= 1000000u && clockHz <= 50000000u)
		s_c352Clock = clockHz;
}

int FmMonShadowC352PitchToMidi(unsigned pitch)
{
	/* C352 frequency is a 16-bit phase increment; sample root is unknown.
	   Unity 0x1000 spreads Sys11/12 voices that often sit well below 0x2000
	   (with 0x2000-unity they all clamped to the same low C). */
	pitch &= 0xFFFFu;
	if (pitch == 0) return -1;
	const double midi = 60.0 + 12.0 * (log((double)pitch / 4096.0) / log(2.0));
	int m = (int)floor(midi + 0.5);
	if (m < 12) m = 12;
	if (m > 108) m = 108;
	return m;
}

void FmMonShadowApplyC352Reg(unsigned ofs, unsigned data16)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	ArcEnterKeys(SASAMI_FMMON_KEYS_C352, 32);
	ofs &= 0xFFFFu;
	data16 &= 0xFFFFu;
	ArcMarkReg16LoHi((ofs & 0xFFu) * 2u, data16);
	/* Voice registers: bank of 8 words per voice. All 32 voices. */
	const unsigned vbase = ofs & ~7u;
	const int ch = (int)(vbase / 8u);
	const int r = (int)(ofs & 7u);
	if (ch >= 0 && ch < 32) {
		if (r == 2) {
			/* C352 voice word 2 is the complete 16-bit frequency. */
			s_c352Pitch[ch] = data16;
			if (s_pcmOn[ch]) {
				const int m = FmMonShadowC352PitchToMidi(data16);
				if (m >= 0) s_pcmNote[ch] = (uint8_t)m;
				s_dirty = 1;
			}
		}
	}
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplySegaPcmMem(unsigned addr, unsigned data8)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	/*
	 * Never ArcEnterKeys here when OPN/OPM is already bound:
	 * KEYSONLY dumps wipe FM rows and swap the header to "SegaPCM×16"
	 * (SegaOut title flicker). Only pure SegaPCM boards enter keys-only.
	 */
	const int hybridOpn = (s_opnaLayout >= 0);
	const int hybridOpm = (s_keysProfile == SASAMI_FMMON_KEYS_MDX || s_opmRegsValid);
	if (!hybridOpn && !hybridOpm)
		ArcEnterKeys(SASAMI_FMMON_KEYS_SEGAPCM, 16);
	else if (hybridOpm && s_pcmCount < 16)
		s_pcmCount = 16;

	addr &= 0xFFu;
	data8 &= 0xFFu;
	ArcMarkReg(addr, (uint8_t)data8);

	int ch = -1;
	int isDiscrete = 0;
	int isCtrl = 0;
	int isFreq = 0;
	int isVol = 0;
	const unsigned base = addr & ~0x38u;
	/* Discrete Hang-On / Space Harrier: 0x42..0x47 / 0xC4..0xC6 + ch<<3 */
	if (base == 0x42 || base == 0x43 || base == 0x44 || base == 0x45
		|| base == 0x46 || base == 0x47 || base == 0xc4 || base == 0xc5 || base == 0xc6) {
		isDiscrete = 1;
		ch = (int)((addr & 0x38u) >> 3);
		if (ch < 0 || ch >= 8) {
			LeaveCriticalSection(&s_cs);
			return;
		}
		if (s_pcmCount < 8) s_pcmCount = 8;
		/* Pack into s_segaChRegs like 315-5218 slots for panel readout:
		   2=lvol 3=rvol 4/5=loop 6=end 7=freq; ctrl kept separately in [0]. */
		if (base == 0x42) s_segaChRegs[ch][2] = (uint8_t)data8;
		else if (base == 0x43) s_segaChRegs[ch][3] = (uint8_t)data8;
		else if (base == 0x44) s_segaChRegs[ch][4] = (uint8_t)data8;
		else if (base == 0x45) s_segaChRegs[ch][5] = (uint8_t)data8;
		else if (base == 0x46) s_segaChRegs[ch][6] = (uint8_t)data8; /* end — not gate */
		else if (base == 0x47) {
			s_segaChRegs[ch][7] = (uint8_t)data8;
			isFreq = 1;
		} else if (base == 0xc6) {
			s_segaChRegs[ch][0] = (uint8_t)data8; /* ctrl */
			isCtrl = 1;
		}
		if (base == 0x42 || base == 0x43) isVol = 1;
	} else {
		/* 315-5218: 8 bytes × 16ch; ctrl is 0x86+8*ch only */
		ch = (int)((addr >> 3) & 15u);
		const int r = (int)(addr & 7u);
		if (ch < 0 || ch >= 16) {
			LeaveCriticalSection(&s_cs);
			return;
		}
		if (s_pcmCount < 16) s_pcmCount = 16;
		s_segaChRegs[ch][r] = (uint8_t)data8;
		if ((addr & 0x87u) == 0x86u) isCtrl = 1;
		else if (r == 0x07) isFreq = 1;
		else if (r == 0x02 || r == 0x03) isVol = 1;
	}

	if (ch >= 0 && isCtrl) {
		const uint8_t ctrl = isDiscrete
			? (uint8_t)s_segaChRegs[ch][0]
			: (uint8_t)s_segaChRegs[ch][6];
		const int on = (ctrl & 1) == 0;
		const unsigned freq = s_segaChRegs[ch][7] ? s_segaChRegs[ch][7] : 0x40u;
		int mid = 60;
		const int m = SegaPcmFreqToMidi(freq);
		if (m >= 0) mid = m;
		ArcSetPcm(ch, on, mid);
	} else if (ch >= 0 && (isFreq || isVol) && s_pcmOn[ch]) {
		const unsigned freq = s_segaChRegs[ch][7] ? s_segaChRegs[ch][7] : 0x40u;
		const int m = SegaPcmFreqToMidi(freq);
		if (m >= 0) { s_pcmNote[ch] = (uint8_t)m; s_dirty = 1; }
	}
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyK054539Reg(unsigned ofs, unsigned data8)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	ArcEnterKeys(SASAMI_FMMON_KEYS_RF5C, 8);
	ofs &= 0x3FFu;
	data8 &= 0xFFu;
	/* Pack into 512-byte shadow window (regs panel). */
	ArcMarkReg(ofs & 0x1FFu, (uint8_t)data8);
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyOki6295(unsigned data8)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	ArcEnterKeys(SASAMI_FMMON_KEYS_OKI, 4);
	data8 &= 0xFFu;
	ArcMarkReg(0, (uint8_t)data8);
	if (s_okiCmd) {
		/* Second byte: channel mask in low nibble for start */
		const unsigned mask = data8 & 0x0Fu;
		ArcMarkReg(1, (uint8_t)data8);
		for (int ch = 0; ch < 4; ch++) {
			if (mask & (1u << ch)) {
				ArcSetPcm(ch, 1, 60 + ch * 3);
				ArcMarkReg(0x10u + (unsigned)ch, 1);
			}
		}
		s_okiCmd = 0;
	} else if (data8 & 0x80) {
		/* Start command — next write has channel bits */
		s_okiCmd = 1;
		s_okiChBits = data8;
		ArcMarkReg(1, (uint8_t)s_okiChBits);
	} else {
		/* Stop: bits select channels to silence */
		for (int ch = 0; ch < 4; ch++) {
			if (data8 & (1u << ch)) {
				ArcSetPcm(ch, 0, -1);
				ArcMarkReg(0x10u + (unsigned)ch, 0);
			}
		}
	}
	LeaveCriticalSection(&s_cs);
}

void FmMonShadowApplyGa20Reg(unsigned ofs, unsigned data8)
{
	EnsureCs();
	EnterCriticalSection(&s_cs);
	/*
	 * Hybrid OPM+GA20 (M92): never ArcEnterKeys(OKI) — that would wipe OPM
	 * rows. Mirror GA20 into bank1 ($100+) and light 4 PCM keys.
	 */
	const int hybridOpm = (s_keysProfile == SASAMI_FMMON_KEYS_MDX || s_opmRegsValid);
	if (!hybridOpm)
		ArcEnterKeys(SASAMI_FMMON_KEYS_OKI, 4);
	else if (s_pcmCount < 4)
		s_pcmCount = 4;

	ofs &= 0x1Fu;
	data8 &= 0xFFu;
	s_ga20Seen = 1;
	s_arcRegsValid = 1;
	ArcMarkReg(0x100u + ofs, (uint8_t)data8);

	const int ch = (int)(ofs >> 3);
	const int r = (int)(ofs & 7);
	if (ch < 0 || ch >= 4) {
		LeaveCriticalSection(&s_cs);
		return;
	}
	const uint8_t ctrl = s_regs[0x100u + (unsigned)(ch << 3) + 6u];
	const unsigned rate = s_regs[0x100u + (unsigned)(ch << 3) + 4u];
	const unsigned vol = s_regs[0x100u + (unsigned)(ch << 3) + 5u];
	const int on = (ctrl & 2) != 0;
	/* Refresh MIDI on key/rate/vol so the keyboard shows notes, not blank. */
	if (r == 4 || r == 5 || r == 6 || on) {
		int mid = 48 + ch * 3;
		if (rate) {
			/* GA20 rate byte → rough Hz for piano label */
			const int m = MidiFromHz(110.0 * (1.0 + (double)rate / 32.0));
			if (m >= 0) mid = m;
		} else if (vol)
			mid = 60 + (int)(vol & 15);
		ArcSetPcm(ch, on, on ? mid : -1);
	}
	LeaveCriticalSection(&s_cs);
}
