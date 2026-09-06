#include "StdAfx.h"
#include "cemu_fmmon_bind.h"
#include "fmmon_shadow.h"
#include <string.h>

static int HasChip(const CEmuGameEntry* ge, int id)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->chipCount && i < 8; i++)
		if (ge->chipIds[i] == id) return 1;
	return 0;
}

void CEmuFmMonBindFromGe(const CEmuGameEntry* ge)
{
	if (!ge) return;

	char plat[24];
	char chip[48];
	plat[0] = 0;
	chip[0] = 0;
	int layout = 1; /* default OPNA-shaped */
	int seedOpm = 0;

	const char* dd = ge->dataDir[0] ? ge->dataDir : "";
	const char* pf = ge->platform[0] ? ge->platform : "";
	const char* sub = ge->subtype[0] ? ge->subtype : "";

	if (_stricmp(dd, "pc88") == 0 || _strnicmp(pf, "pc88", 4) == 0 || _stricmp(pf, "pc80sr") == 0)
		strncpy_s(plat, "PC-88", _TRUNCATE);
	else if (_stricmp(dd, "pc98") == 0 || _strnicmp(pf, "pc98", 4) == 0)
		strncpy_s(plat, "PC-98", _TRUNCATE);
	else if (_stricmp(dd, "x1") == 0 || _stricmp(pf, "x1") == 0)
		strncpy_s(plat, "X1", _TRUNCATE);
	else if (_stricmp(dd, "sc3000") == 0 || _stricmp(sub, "sg1000") == 0
		|| _stricmp(pf, "sg1000") == 0)
		strncpy_s(plat, "SC-3000", _TRUNCATE);
	else if (_stricmp(dd, "x68k") == 0 || _stricmp(pf, "x68k") == 0)
		strncpy_s(plat, "X68000", _TRUNCATE);
	else if (_stricmp(dd, "fmtowns") == 0 || _stricmp(pf, "fmtowns") == 0)
		strncpy_s(plat, "FM Towns", _TRUNCATE);
	else if (_stricmp(dd, "fm7") == 0 || _stricmp(pf, "fm7") == 0
		|| _stricmp(pf, "fm77av") == 0 || _stricmp(pf, "mucomfm") == 0)
		strncpy_s(plat, "FM-7", _TRUNCATE);
	else if (_stricmp(dd, "pcat") == 0 || _stricmp(dd, "pc") == 0
		|| _stricmp(pf, "pcat") == 0 || _stricmp(pf, "pcatdos") == 0)
		strncpy_s(plat, "PC/AT", _TRUNCATE);
	else if (_stricmp(dd, "msx") == 0 || _stricmp(pf, "msx") == 0)
		strncpy_s(plat, "MSX", _TRUNCATE);
	else if (_stricmp(pf, "megadrive") == 0)
		strncpy_s(plat, "MD", _TRUNCATE);
	else if (_stricmp(pf, "neogeo") == 0)
		strncpy_s(plat, "NeoGeo", _TRUNCATE);
	else if (_stricmp(pf, "videosystem") == 0 || _stricmp(sub, "aerofgt") == 0)
		strncpy_s(plat, "VSys", _TRUNCATE);
	else if (_stricmp(dd, "ac") == 0 || _strnicmp(pf, "capcom", 6) == 0
		|| _stricmp(pf, "sega") == 0 || _stricmp(pf, "namco") == 0
		|| _stricmp(pf, "taito") == 0 || _strnicmp(pf, "konami", 6) == 0
		|| _stricmp(pf, "irem") == 0 || _stricmp(pf, "dataeast") == 0
		|| _stricmp(pf, "videosystem") == 0) {
		if (_strnicmp(sub, "cps", 3) == 0)
			strncpy_s(plat, "CPS", _TRUNCATE);
		else if (_strnicmp(sub, "system16", 8) == 0)
			strncpy_s(plat, "Sys16", _TRUNCATE);
		else if (_strnicmp(sub, "system18", 8) == 0)
			strncpy_s(plat, "Sys18", _TRUNCATE);
		else if (_strnicmp(sub, "system32", 8) == 0)
			strncpy_s(plat, "Sys32", _TRUNCATE);
		else if (_stricmp(sub, "gng") == 0 || _stricmp(sub, "opn2") == 0)
			strncpy_s(plat, "GNG", _TRUNCATE);
		else if (_stricmp(sub, "outrun") == 0 || _stricmp(sub, "aburner") == 0
			|| _stricmp(sub, "sharrier") == 0 || _stricmp(sub, "hangon") == 0
			|| _stricmp(sub, "toutrun") == 0 || _stricmp(sub, "shangon") == 0)
			strncpy_s(plat, "SegaOut", _TRUNCATE);
		else if (_stricmp(pf, "namco") == 0)
			strncpy_s(plat, "Namco", _TRUNCATE);
		else if (_stricmp(pf, "taito") == 0)
			strncpy_s(plat, "Taito", _TRUNCATE);
		else if (_strnicmp(pf, "konami", 6) == 0)
			strncpy_s(plat, "Konami", _TRUNCATE);
		else if (_stricmp(pf, "sega") == 0)
			strncpy_s(plat, "Sega", _TRUNCATE);
		else
			strncpy_s(plat, "AC", _TRUNCATE);
	} else if (pf[0]) {
		strncpy_s(plat, pf, _TRUNCATE);
	} else if (dd[0]) {
		strncpy_s(plat, dd, _TRUNCATE);
	}

	/* Chip label + layout — QSound / PCM banks first (arcade xml2) */
	const int isFm7 = (_stricmp(dd, "fm7") == 0 || _stricmp(pf, "fm7") == 0
		|| _stricmp(pf, "fm77av") == 0 || _stricmp(pf, "mucomfm") == 0);
	const int isMsx = (_stricmp(dd, "msx") == 0 || _stricmp(pf, "msx") == 0);
	const size_t archiveLen = strlen(ge->archive);
	const int fm7Ay = isFm7 && (_stricmp(sub, "psg") == 0 || _stricmp(sub, "ys") == 0
		|| _stricmp(sub, "xanadu2") == 0
		|| (archiveLen >= 4 && _stricmp(ge->archive + archiveLen - 4, "_fm7") == 0));
	if (isMsx) {
		/* KSS/FMPAC titles often omit <chip type=OPLL>; runtime always has
		   YM2413 when subtype is kss/opll. Keep layout non-OPNA so the MSX
		   dump path (FLAG_MSX + padHit=3) owns the keyboard — OPNA layout
		   made Quinpl look blank even while OPLL regs were streaming. */
		const int hasOpll = HasChip(ge, CEMU_CHIP_OPLL)
			|| _stricmp(sub, "kss") == 0 || _stricmp(sub, "opll") == 0
			|| _stricmp(sub, "fmpac") == 0
			|| (archiveLen >= 4 && _stricmp(ge->archive + archiveLen - 4, "_msx") == 0);
		strncpy_s(chip, hasOpll ? "OPLL+PSG" : "AY-3-8910", _TRUNCATE);
		layout = -1;
		FmMonShadowSetSsgClock(3579545u);
	} else if (fm7Ay) {
		strncpy_s(chip, "AY-3-8910", _TRUNCATE);
		layout = -1;
 /* FM-7 AY master; CChipAy halves once (hoot). FmMon uses the post-/2 rate. */
		FmMonShadowSetSsgClock(1228800u);
	} else if (isFm7) {
		strncpy_s(chip, "OPN", _TRUNCATE);
		layout = 0;
		FmMonShadowSetSsgClock(1228800u);
	} else if (HasChip(ge, CEMU_CHIP_QSOUND) || _stricmp(sub, "cps1qs") == 0
		|| _stricmp(sub, "cps2") == 0 || _stricmp(sub, "zn") == 0) {
		strncpy_s(chip, "QSoundx16", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_QSOUND);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_QSOUND);
	} else if (_strnicmp(sub, "cps1", 4) == 0) {
		strncpy_s(chip, "OPM+OKIx4", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_OKI);
	} else if (HasChip(ge, CEMU_CHIP_K053260) || _stricmp(sub, "053260") == 0) {
		strncpy_s(chip, "K053260x4", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
	} else if (HasChip(ge, CEMU_CHIP_K054539)
		|| _stricmp(sub, "systemgx") == 0
		|| _strnicmp(sub, "054539", 6) == 0) {
		/* Dual K054539 (systemgx/054539x2) = 16 PCM rows; single = 8. */
		const int dual = (_stricmp(sub, "systemgx") == 0
			|| _stricmp(sub, "054539x2") == 0) ? 1 : 0;
		strncpy_s(chip, dual ? "K054539x16" : "K054539x8", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_RF5C);
	} else if (HasChip(ge, CEMU_CHIP_C352)
		|| _stricmp(sub, "system12") == 0 || _stricmp(sub, "system11") == 0
		|| _stricmp(sub, "system22") == 0 || _stricmp(sub, "nd1") == 0) {
		strncpy_s(chip, "C352x32", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_C352);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_C352);
	} else if (_stricmp(sub, "na1") == 0 || _stricmp(sub, "na2") == 0
		|| _stricmp(sub, "nb1") == 0 || _stricmp(sub, "nb2") == 0) {
		/* MAME namcona1: C219 only. Must not steal Sys2 (C140+YM2151). */
		strncpy_s(chip, "C219x24", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_RF5C);
	} else if ((HasChip(ge, CEMU_CHIP_C140) && HasChip(ge, CEMU_CHIP_OPM))
		|| (_stricmp(pf, "namco") == 0
			&& (_stricmp(sub, "system2") == 0 || _stricmp(sub, "system21") == 0
				|| _stricmp(sub, "c140") == 0))) {
		strncpy_s(chip, "OPM+C140", _TRUNCATE);
		layout = -1;
		seedOpm = 1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	} else if (HasChip(ge, CEMU_CHIP_C140)) {
		strncpy_s(chip, "C140x24", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_RF5C);
	} else if ((HasChip(ge, CEMU_CHIP_YM2612) && HasChip(ge, CEMU_CHIP_RF5C68))
		|| _strnicmp(sub, "system32", 8) == 0 || _strnicmp(sub, "system18", 8) == 0
		|| _stricmp(sub, "multi32") == 0) {
		strncpy_s(chip, "YM2612+RF5C68x8", _TRUNCATE);
		layout = 0; /* OPN2-shaped FM + RF5C PCM rows */
		FmMonShadowSetSsgClock(7670453u);
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
	} else if (HasChip(ge, CEMU_CHIP_SEGAPCM)) {
		if (HasChip(ge, CEMU_CHIP_OPN)
			|| _stricmp(sub, "sharrier") == 0 || _stricmp(sub, "hangon") == 0) {
			strncpy_s(chip, "YM2203+SegaPCMx8", _TRUNCATE);
			layout = 0;
			FmMonShadowSetSsgClock(4000000u);
			/* Keep OPN flush path (not KEYSONLY) so title stays SegaOut and FM
			   rows don't blank. Profile tags PCM rows as SPCMx8. */
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_SEGAPCM);
		} else if (HasChip(ge, CEMU_CHIP_OPM)) {
			strncpy_s(chip, "OPM+SegaPCM", _TRUNCATE);
			layout = -1;
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
		} else {
			strncpy_s(chip, "SegaPCMx16", _TRUNCATE);
			layout = -1;
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_SEGAPCM);
		}
	} else if (HasChip(ge, CEMU_CHIP_RF5C68)) {
		strncpy_s(chip, "RF5C68x8", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
	} else if (HasChip(ge, CEMU_CHIP_YM2610) || _stricmp(pf, "neogeo") == 0
		|| _stricmp(sub, "neogeo") == 0 || _stricmp(sub, "aerofgt") == 0
		|| _stricmp(pf, "videosystem") == 0
		|| _stricmp(sub, "f2system") == 0 || _stricmp(sub, "bsystem") == 0
		|| _stricmp(sub, "dual68") == 0 || _stricmp(sub, "taitoh") == 0) {
		strncpy_s(chip, "YM2610", _TRUNCATE);
		layout = 2; /* FMx4 + SSGx3 + ADPCM-A/B */
		FmMonShadowSetSsgClock(2000000u); /* YM2610 SSG = clock/4 at 8MHz */
	} else if (_stricmp(sub, "opna") == 0 || HasChip(ge, CEMU_CHIP_OPNA)) {
		strncpy_s(chip, "OPNA", _TRUNCATE);
		layout = 1;
		/* ym2608 SSG effective clock == YM2203@3.9936M (master/4 with
		   default prescale). FmMon uses master/32, so pass 3993600 — not
		   7987200 — or SSG MIDI notes (esp. SSG3) read one octave high. */
		FmMonShadowSetSsgClock(3993600u);
	} else if (_stricmp(sub, "opn") == 0 || _stricmp(sub, "opn2") == 0
		|| HasChip(ge, CEMU_CHIP_OPN) || HasChip(ge, CEMU_CHIP_YM2612)) {
		if (HasChip(ge, CEMU_CHIP_YM2612) || _stricmp(pf, "megadrive") == 0)
			strncpy_s(chip, "OPN2", _TRUNCATE);
		else
			strncpy_s(chip, "OPN", _TRUNCATE);
		layout = 0;
		FmMonShadowSetSsgClock((_stricmp(pf, "megadrive") == 0) ? 7670453u : 3993600u);
	} else if (HasChip(ge, CEMU_CHIP_ES5505) || _stricmp(sub, "f3system") == 0) {
		strncpy_s(chip, "ES5505", _TRUNCATE);
		layout = -1;
	} else if (HasChip(ge, CEMU_CHIP_GA20) && HasChip(ge, CEMU_CHIP_OPM)) {
		strncpy_s(chip, "OPM+GA20", _TRUNCATE);
		layout = -1;
		seedOpm = 1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
		/* Prefill 4 PCM rows so PcmRows() is ready before first GA20 write. */
		FmMonShadowApplyGa20Reg(0, 0);
	} else if (HasChip(ge, CEMU_CHIP_C30) && HasChip(ge, CEMU_CHIP_OPM)) {
		strncpy_s(chip, "OPM+CUS30", _TRUNCATE);
		layout = -1;
		seedOpm = 1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	} else if (HasChip(ge, CEMU_CHIP_C30)) {
		strncpy_s(chip, "CUS30x8", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_RF5C);
	} else if (_stricmp(sub, "opm") == 0 || HasChip(ge, CEMU_CHIP_OPM)
		|| _stricmp(dd, "x68k") == 0 || _stricmp(pf, "x68k") == 0
		|| _stricmp(pf, "x1") == 0 || _stricmp(dd, "x1") == 0
		|| _strnicmp(sub, "cps1", 4) == 0 || _strnicmp(sub, "system16", 8) == 0
		|| _stricmp(sub, "outrun") == 0 || _stricmp(sub, "aburner") == 0
		|| _stricmp(sub, "fullt") == 0 || _stricmp(sub, "rastan") == 0
		|| _stricmp(sub, "asuka") == 0 || _stricmp(sub, "m72") == 0
		|| _stricmp(sub, "m92") == 0 || _stricmp(sub, "megasys1") == 0) {
		if (_stricmp(sub, "psg") == 0 || _stricmp(sub, "x1psg") == 0
			|| (HasChip(ge, CEMU_CHIP_AY) && !HasChip(ge, CEMU_CHIP_OPM))) {
			strncpy_s(chip, "AY-3-8910", _TRUNCATE);
			layout = -1;
			FmMonShadowSetSsgClock(2000000u);
		} else if (HasChip(ge, CEMU_CHIP_AY) || _stricmp(pf, "x1") == 0
			|| _stricmp(dd, "x1") == 0) {
			strncpy_s(chip, "OPM+AY", _TRUNCATE);
			layout = -1;
			seedOpm = 1;
		} else {
			strncpy_s(chip, "OPM", _TRUNCATE);
			layout = -1;
			seedOpm = 1;
		}
		/* X68000: OPM keys + MSM6258 ADPCM PCM row. */
		if (_stricmp(dd, "x68k") == 0 || _stricmp(pf, "x68k") == 0)
			strncpy_s(chip, "OPM+ADPCM", _TRUNCATE);
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	} else if (HasChip(ge, CEMU_CHIP_SN76489) || _stricmp(sub, "sg1000") == 0
		|| _stricmp(dd, "sc3000") == 0
		|| _stricmp(sub, "system1") == 0 || _stricmp(sub, "system2") == 0) {
		strncpy_s(chip, (_stricmp(sub, "system1") == 0 || _stricmp(sub, "system2") == 0)
			? "SN76489x2" : "SN76489", _TRUNCATE);
		layout = -1;
		FmMonShadowSetSsgClock(3579545u);
	} else if (_stricmp(sub, "taitosj") == 0) {
		strncpy_s(chip, "AY-3-8910x3", _TRUNCATE);
		layout = -1;
		FmMonShadowSetSsgClock(1500000u);
	} else if (_stricmp(sub, "gng") == 0) {
		strncpy_s(chip, "YM2203", _TRUNCATE);
		layout = 0;
		FmMonShadowSetSsgClock(1500000u);
	} else if (_stricmp(sub, "86") == 0 || HasChip(ge, CEMU_CHIP_PCM86)) {
		strncpy_s(chip, "OPNA+86PCM", _TRUNCATE);
		layout = 1;
		FmMonShadowSetSsgClock(3993600u);
	} else if (HasChip(ge, CEMU_CHIP_OPL3) || _stricmp(sub, "opl3") == 0) {
		strncpy_s(chip, "OPL3x18", _TRUNCATE);
		layout = -1;
		FmMonShadowSetOplMode(2);
	} else if (HasChip(ge, CEMU_CHIP_OPL2) || _stricmp(sub, "adlib") == 0
		|| _stricmp(sub, "opl2") == 0 || _stricmp(sub, "opl") == 0
		|| _stricmp(sub, "soundblaster16") == 0 || _stricmp(sub, "sb16") == 0
		|| _stricmp(sub, "soundblaster") == 0 || _stricmp(sub, "sbpro") == 0
		|| _stricmp(sub, "sb") == 0) {
		if (_strnicmp(sub, "sound", 5) == 0 || _strnicmp(sub, "sb", 2) == 0)
			strncpy_s(chip, "Sound Blaster OPL2x9", _TRUNCATE);
		else
			strncpy_s(chip, "AdLib OPL2x9", _TRUNCATE);
		layout = -1;
		FmMonShadowSetOplMode(1);
	} else if (_stricmp(sub, "gameblaster") == 0 || _stricmp(sub, "cms") == 0) {
		strncpy_s(chip, "GameBlaster SAAx2", _TRUNCATE);
		layout = -1;
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_MIDI);
	} else if (_stricmp(sub, "beep") == 0) {
		strncpy_s(chip, "BEEP", _TRUNCATE);
		layout = -1;
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_MIDI);
	} else if (_stricmp(sub, "midiout") == 0 || _stricmp(sub, "midi") == 0) {
		strncpy_s(chip, "MPU-401 MIDI", _TRUNCATE);
		layout = -1;
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_MIDI);
	} else if (sub[0]) {
		char up[40];
		int n = 0;
		for (; sub[n] && n < 31; n++) {
			char c = sub[n];
			if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			up[n] = c;
		}
		up[n] = 0;
		strncpy_s(chip, up, _TRUNCATE);
	} else {
		strncpy_s(chip, "OPNA", _TRUNCATE);
		layout = 1;
	}

	FmMonShadowSetIdentity(plat, chip);
	FmMonShadowSetOpnaLayout(layout);
	if (isMsx) {
		unsigned devices = SASAMI_FMMON_DEV_PSG;
		if (HasChip(ge, CEMU_CHIP_OPLL)
			|| _stricmp(sub, "kss") == 0 || _stricmp(sub, "opll") == 0
			|| _stricmp(sub, "fmpac") == 0)
			devices |= SASAMI_FMMON_DEV_OPLL;
		FmMonShadowSetMsxDevices(devices);
	}
	/* Seed empty YM2151 snapshot so Flush never emits OPNA-shaped dumps. */
	if (seedOpm) {
		unsigned char z[256];
		memset(z, 0, sizeof(z));
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
		FmMonShadowSetOpmRegSnapshot(z);
	}
}
