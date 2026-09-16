#include "StdAfx.h"
#include "cemu_fmmon_bind.h"
#include "fmmon_shadow.h"
#include "../cemu_catalog.h"
#include <string.h>

static int HasChip(const CEmuGameEntry* ge, int id)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->chipCount && i < 8; i++)
		if (ge->chipIds[i] == id) return 1;
	/* カタログ <name> に書かれたチップも見る。アーケードは <chip> が無く、
	   基板構成の唯一の記録であることが多い。subtype 表だけだと未知は全部
	   "OPM" ラベルになっていた。 */
	for (int i = 0; i < ge->docChipCount && i < 12; i++)
		if (ge->docChipIds[i] == id) return 1;
	return 0;
}

/* カタログが記載できるチップごとの表示名とモニタ形 */
struct FmMonChipInfo {
	int id;
	const char* label;
	int channels;   /* 表示行。ラベルに数が無いときは 0 */
	int layout;     /* このチップが暗示する OPN(A) 形。-1 = 非 OPN */
	unsigned ssgHz; /* 報告する SSG/PSG クロック。無関係なら 0 */
	unsigned keys;  /* このチップの行が要る鍵盤/パネルプロファイル */
	int isPcm;      /* サンプルプレーヤ。FM よりプロファイルを取る */
};

/* FM 部品を PCM より先に名前を付ける。モニタの行順とカタログ表記に合わせる。 */
static const FmMonChipInfo kFmMonChips[] = {
	{ CEMU_CHIP_OPNA,     "OPNA",      6,  1, 3993600u, SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_YM2610,   "YM2610",    4,  2, 2000000u, SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_OPN,      "YM2203",    3,  0, 1500000u, SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_YM2612,   "YM2612",    6,  0, 0u,       SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_OPM,      "OPM",       8, -1, 0u,       SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_OPL3,     "OPL3",     18, -1, 0u,       SASAMI_FMMON_KEYS_OPL3,    0 },
	{ CEMU_CHIP_OPL2,     "OPL2",      9, -1, 0u,       SASAMI_FMMON_KEYS_OPL2,    0 },
	{ CEMU_CHIP_OPLL,     "OPLL",      9, -1, 0u,       SASAMI_FMMON_KEYS_OPL2,    0 },
	{ CEMU_CHIP_AY,       "AY-3-8910", 3, -1, 1500000u, SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_SN76489,  "SN76496",   4, -1, 3579545u, SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_SAA1099,  "SAA1099",   6, -1, 0u,       SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_K051649,  "SCC",       5, -1, 0u,       SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_MSM5232,  "MSM5232",   8, -1, 0u,       SASAMI_FMMON_KEYS_MDX,     0 },
	{ CEMU_CHIP_QSOUND,   "QSound",   16, -1, 0u,       SASAMI_FMMON_KEYS_QSOUND,  1 },
	{ CEMU_CHIP_C352,     "C352",     32, -1, 0u,       SASAMI_FMMON_KEYS_C352,    1 },
	{ CEMU_CHIP_C140,     "C140",     24, -1, 0u,       SASAMI_FMMON_KEYS_C352,    1 },
	{ CEMU_CHIP_C30,      "CUS30",     8, -1, 0u,       SASAMI_FMMON_KEYS_C352,    1 },
	{ CEMU_CHIP_SCSP,     "SCSP",     32, -1, 0u,       SASAMI_FMMON_KEYS_RF5C,    1 },
	{ CEMU_CHIP_MULTIPCM, "MultiPCM", 28, -1, 0u,       SASAMI_FMMON_KEYS_MULTIPCM, 1 },
	{ CEMU_CHIP_SEGAPCM,  "SegaPCM",  16, -1, 0u,       SASAMI_FMMON_KEYS_SEGAPCM, 1 },
	{ CEMU_CHIP_RF5C400,  "RF5C400",  32, -1, 0u,       SASAMI_FMMON_KEYS_RF5C,    1 },
	{ CEMU_CHIP_RF5C68,   "RF5C68",    8, -1, 0u,       SASAMI_FMMON_KEYS_RF5C,    1 },
	{ CEMU_CHIP_K054539,  "K054539",   8, -1, 0u,       SASAMI_FMMON_KEYS_RF5C,    1 },
	{ CEMU_CHIP_K053260,  "K053260",   4, -1, 0u,       SASAMI_FMMON_KEYS_RF5C,    1 },
	{ CEMU_CHIP_K007232,  "K007232",   2, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_K005289,  "K005289",   2, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_X1_010,   "X1-010",   16, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_YMZ280B,  "YMZ280B",   8, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_ES5505,   "ES5505",   32, -1, 0u,       SASAMI_FMMON_KEYS_RF5C,    1 },
	{ CEMU_CHIP_GA20,     "GA20",      4, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_OKI6295,  "OKI6295",   4, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_UPD7759,  "uPD7759",   1, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_MSM5205,  "MSM5205",   1, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_VLM5030,  "VLM5030",   1, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 },
	{ CEMU_CHIP_IREM_DAC, "DAC",       1, -1, 0u,       SASAMI_FMMON_KEYS_OKI,     1 }
};

static const FmMonChipInfo* FmMonFindChipInfo(int id)
{
	for (unsigned i = 0; i < _countof(kFmMonChips); i++)
		if (kFmMonChips[i].id == id) return &kFmMonChips[i];
	return NULL;
}

/* カタログ記載チップがラベルに全部含まれるか。
   表記ではなくチップ ID で比べるので、手調整の "YM3438+MultiPCMx2" も
   記載された YM2612 系をカバーしたとみなす。 */
static int FmMonLabelCoversDocChips(const CEmuGameEntry* ge, const char* label)
{
	if (!ge || ge->docChipCount <= 0) return 1;
	int inLabel[12];
	const int nLabel = CEmuCatalogChipsFromText(label, inLabel, 12);
	for (int i = 0; i < ge->docChipCount && i < 12; i++) {
		int found = 0;
		for (int k = 0; k < nLabel; k++)
			if (inLabel[k] == ge->docChipIds[i]) { found = 1; break; }
		if (!found) return 0;
	}
	return 1;
}

/* 記載チップ集合から kFmMonChips 順でラベルを組み、先頭 FM が暗示する
   FM 形と SSG クロックを返す。 */
static int FmMonComposeDocLabel(const CEmuGameEntry* ge, char* out,
	unsigned outLen, int* layoutOut, unsigned* ssgOut, unsigned* keysOut)
{
	if (!ge || ge->docChipCount <= 0 || !out || outLen < 8) return 0;
	out[0] = 0;
	int layout = -1;
	int haveLayout = 0;
	unsigned ssg = 0;
	unsigned keys = 0;
	int keysFromPcm = 0;
	int written = 0;

	for (unsigned k = 0; k < _countof(kFmMonChips); k++) {
		const FmMonChipInfo* ci = &kFmMonChips[k];
		int doc = 0;
		for (int i = 0; i < ge->docChipCount && i < 12; i++)
			if (ge->docChipIds[i] == ci->id) { doc = 1; break; }
		if (!doc) continue;
		if (written) strncat_s(out, outLen, "+", _TRUNCATE);
		strncat_s(out, outLen, ci->label, _TRUNCATE);
		written++;
		if (!haveLayout) {
			layout = ci->layout;
			haveLayout = 1;
		}
		if (ci->ssgHz && !ssg) ssg = ci->ssgHz;
		/* OPN 形基板 (layout>=0): PCM がキー行プロファイルを持ち FM dump に重ねる。
		   OPM ハイブリッド (layout -1、keys は既に MDX) は OPM 鍵盤を残す —
		   ここで OKI/RF5C を奪うと Raizing が "Raizing  OPM+OKI" と "AC  OKI×4"
		   の間で点滅した。 */
		if (!keys) {
			keys = ci->keys;
			keysFromPcm = ci->isPcm;
		} else if (ci->isPcm && !keysFromPcm) {
			if (layout >= 0)
				keys = ci->keys;
			keysFromPcm = 1;
		}
	}
	if (!written) return 0;
	if (layoutOut) *layoutOut = layout;
	if (ssgOut) *ssgOut = ssg;
	if (keysOut) *keysOut = keys;
	return 1;
}

/* Reset 後: platform+chip ラベルと OPNA layout をシャドウへ */
void CEmuFmMonBindFromGe(const CEmuGameEntry* ge)
{
	if (!ge) return;

	char plat[24];
	char chip[48];
	plat[0] = 0;
	chip[0] = 0;
	int layout = 1; /* 既定は OPNA 形 */
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
	else if (_stricmp(pf, "raizing") == 0 || _stricmp(pf, "eighting") == 0
		|| _stricmp(sub, "mahou") == 0 || _stricmp(sub, "bgaregga") == 0
		|| _stricmp(sub, "batrider") == 0 || _stricmp(sub, "bbakraid") == 0)
		strncpy_s(plat, "Raizing", _TRUNCATE);
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

	/* チップラベル + layout — QSound / PCM バンクを先に (アーケード xml2) */
	const int isFm7 = (_stricmp(dd, "fm7") == 0 || _stricmp(pf, "fm7") == 0
		|| _stricmp(pf, "fm77av") == 0 || _stricmp(pf, "mucomfm") == 0);
	const int isMsx = (_stricmp(dd, "msx") == 0 || _stricmp(pf, "msx") == 0);
	const size_t archiveLen = strlen(ge->archive);
	/* platform が勝つ。同じ *_fm7 zip に PSG (fm7) と OPN (fm77av) 行がある。
	   カタログ chipIds が zip stem 由来の AY を残しても、fm77av セッションで
	   AY 鍵盤を選ばない (モニタが MON_DEAD になった)。 */
	const int fm7Opn = isFm7 && (
		_stricmp(pf, "fm77av") == 0 || _stricmp(sub, "opn") == 0
		|| _stricmp(sub, "ysav") == 0
		|| (_stricmp(pf, "mucomfm") == 0 && _stricmp(sub, "ys") != 0)
		|| HasChip(ge, CEMU_CHIP_OPN));
	const int fm7Ay = isFm7 && !fm7Opn;
	if (isMsx) {
		/* KSS/FMPAC は <chip type=OPLL> を省略しがち。runtime は subtype が
		   kss/opll なら常に YM2413。layout を非 OPNA にして MSX dump 経路
		   (FLAG_MSX + padHit=3) が鍵盤を持つ。OPNA layout だと Quinpl が
		   OPLL レジスタを流しても鍵盤が空になった。 */
		const int hasOpll = HasChip(ge, CEMU_CHIP_OPLL)
			|| _stricmp(sub, "kss") == 0 || _stricmp(sub, "opll") == 0
			|| _stricmp(sub, "fmpac") == 0 || _stricmp(sub, "ascii16") == 0
			|| (archiveLen >= 4 && _stricmp(ge->archive + archiveLen - 4, "_msx") == 0);
		const int hasSng = HasChip(ge, CEMU_CHIP_SN76489);
		if (hasSng) {
			strncpy_s(chip, "SN76496", _TRUNCATE);
			layout = -1;
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
			FmMonShadowSetSsgClock(3579545u);
		} else {
			strncpy_s(chip, hasOpll ? "OPLL+PSG" : "AY-3-8910", _TRUNCATE);
			layout = -1;
			FmMonShadowSetSsgClock(3579545u);
		}
	} else if (fm7Ay) {
		strncpy_s(chip, "AY-3-8910", _TRUNCATE);
		layout = -1;
		/* FM-7 AY master。CChipAy が一度半分 (hoot)。FmMon は /2 後のレート。 */
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
		seedOpm = 1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	} else if (HasChip(ge, CEMU_CHIP_K053260) || _stricmp(sub, "053260") == 0) {
		strncpy_s(chip, "K053260x4", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
	} else if (HasChip(ge, CEMU_CHIP_K054539)
		|| _stricmp(sub, "systemgx") == 0
		|| _strnicmp(sub, "054539", 6) == 0) {
		/* Dual K054539 (systemgx/054539x2) = 16 PCM 行。単発は 8。 */
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
		/* MAME namcona1: C219 のみ。Sys2 (C140+YM2151) を奪わない。 */
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
		layout = 0; /* OPN2 形 FM + RF5C PCM 行 */
		FmMonShadowSetSsgClock(7670453u);
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
	} else if (HasChip(ge, CEMU_CHIP_SEGAPCM)) {
		if (HasChip(ge, CEMU_CHIP_OPN)
			|| _stricmp(sub, "sharrier") == 0 || _stricmp(sub, "hangon") == 0) {
			strncpy_s(chip, "YM2203+SegaPCMx8", _TRUNCATE);
			layout = 0;
			FmMonShadowSetSsgClock(4000000u);
			/* OPN flush 経路を残す (KEYSONLY ではない)。タイトルは SegaOut、
			   FM 行を空にしない。プロファイルが PCM 行を SPCMx8 と付ける。 */
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_SEGAPCM);
		} else if (HasChip(ge, CEMU_CHIP_OPM)) {
			strncpy_s(chip, "OPM+SegaPCM", _TRUNCATE);
			layout = -1;
			seedOpm = 1;
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
		} else {
			strncpy_s(chip, "SegaPCMx16", _TRUNCATE);
			layout = -1;
			FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_SEGAPCM);
		}
	} else if (_stricmp(sub, "model2") == 0 || _stricmp(sub, "model1") == 0) {
		/* daytona / vf: MAME model2o → Model 1 オーディオ基板 =
		   YM3438 (OPN2: FM×6、SSG 無し) + MultiPCM 双発。 */
		strncpy_s(chip, "YM3438+MultiPCMx2", _TRUNCATE);
		layout = 3; /* OPN2 FM×6 — OPNA [10ch]+SSG ではない */
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MULTIPCM);
	} else if (_strnicmp(sub, "model2", 6) == 0 || _strnicmp(sub, "model3", 6) == 0) {
		strncpy_s(chip, "SCSPx32", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_RF5C);
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
	} else if (_strnicmp(sub, "soundorchestra", 14) == 0) {
		/* SNE SOUND ORCHESTRA: 26K 互換 YM2203 + 2 本目の FM
		   (YM3812、V/VS/LS は Y8950) @ 0x18C/0x18E。OPN layout を残し
		   3FM+3SSG 行を見せる。OPL 書きは FmMonShadowWriteOplReg でシャドウへ。
		   素の OPN と取り違えないよう基板名を付ける。 */
		strncpy_s(chip, (sub[14] == 'v' || sub[14] == 'V')
			? "YM2203+Y8950" : "YM2203+OPL2", _TRUNCATE);
		layout = 0;
		FmMonShadowSetSsgClock(3993600u);
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_OPL2);
	} else if (_stricmp(sub, "opna") == 0 || HasChip(ge, CEMU_CHIP_OPNA)) {
		strncpy_s(chip, "OPNA", _TRUNCATE);
		layout = 1;
		/* ym2608 SSG 実効クロック == YM2203@3.9936M (既定プリスケールで master/4)。
		   FmMon は master/32 なので 3993600 を渡す — 7987200 だと
		   SSG MIDI (特に SSG3) が 1 オクターブ高く読める。 */
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
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_C352);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_C352);
	} else if (HasChip(ge, CEMU_CHIP_GA20) && HasChip(ge, CEMU_CHIP_OPM)) {
		strncpy_s(chip, "OPM+GA20", _TRUNCATE);
		layout = -1;
		seedOpm = 1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
		/* 4 PCM 行を先に埋め、最初の GA20 書きの前に PcmRows() が使えるようにする。 */
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
	} else if (_stricmp(sub, "bbakraid") == 0
		|| (HasChip(ge, CEMU_CHIP_YMZ280B) && !HasChip(ge, CEMU_CHIP_OPM)
			&& (_stricmp(pf, "eighting") == 0 || _stricmp(pf, "raizing") == 0))) {
		strncpy_s(chip, "YMZ280B", _TRUNCATE);
		layout = -1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_OKI);
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_OKI);
	} else if (_stricmp(sub, "mahou") == 0 || _stricmp(sub, "bgaregga") == 0
		|| _stricmp(sub, "batrider") == 0
		|| ((HasChip(ge, CEMU_CHIP_OPM) && HasChip(ge, CEMU_CHIP_OKI6295))
			&& (_stricmp(pf, "raizing") == 0 || _stricmp(pf, "eighting") == 0))) {
		/* OPM 鍵盤を残す。OKI 行は重ねる — EnterKeysOnly(OKI) しない。 */
		strncpy_s(chip, "OPM+OKI6295", _TRUNCATE);
		layout = -1;
		seedOpm = 1;
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	} else if (_stricmp(sub, "kikikai") == 0) {
		/* カタログ xml2 は OPM。Z80 プログラムは YM2203 @C000。 */
		strncpy_s(chip, "YM2203", _TRUNCATE);
		layout = 0;
		FmMonShadowSetSsgClock(3000000u);
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
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
			/* X1 AY master 2 MHz。CChipAy が一度半分 → 1 MHz トーンクロック。
			   FmMon /16 は分周後のクロックを使わないと SSG MIDI が約 1 オクターブずれる。 */
			FmMonShadowSetSsgClock(1000000u);
		} else {
			strncpy_s(chip, "OPM", _TRUNCATE);
			layout = -1;
			seedOpm = 1;
		}
		/* X68000: OPM 鍵盤 + MSM6258 ADPCM PCM 行。 */
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
	} else if (_stricmp(sub, "68k2") == 0) {
		/* BGM 0x1x は YM2413。OPN layout だと YM2203 dump が FM キーを消す一方
		   OPLL は流れ、プローブが MON_DEAD / keys=0 のまま。 */
		strncpy_s(chip, "YM2203+OPLL", _TRUNCATE);
		layout = -1;
		FmMonShadowSetSsgClock(1500000u);
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_OPL2);
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
		/* hoot XML は GM/MT-32 行を type=beep + midiout=1 と付けることが多い。
		   再生は MPU UART → MIDI モニタで、FM パネルではない。 */
		int midiOut = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "midiout") == 0) { midiOut = 1; break; }
		}
		if (midiOut) {
			strncpy_s(chip, "MPU-401 MIDI", _TRUNCATE);
			layout = -1;
		} else {
			strncpy_s(chip, "BEEP", _TRUNCATE);
			layout = -1;
			FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_MIDI);
		}
	} else if (_stricmp(sub, "midiout") == 0 || _stricmp(sub, "midi") == 0) {
		strncpy_s(chip, "MPU-401 MIDI", _TRUNCATE);
		layout = -1;
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

	/* 上の subtype 連鎖は手メンテで全基板は知れない。未知は "OPM" ラベル
	   になっていた。カタログがチップ集合を書いて連鎖のラベルが欠けていれば
	   カタログが勝つ — 実ハードの唯一の per-archive 記録。連鎖が認識した
	   基板は調整済みラベルを残す。 */
	if (!isFm7 && !FmMonLabelCoversDocChips(ge, chip)) {
		char docLabel[48];
		int docLayout = layout;
		unsigned docSsg = 0, docKeys = 0;
		if (FmMonComposeDocLabel(ge, docLabel, sizeof(docLabel),
				&docLayout, &docSsg, &docKeys)) {
			strncpy_s(chip, docLabel, _TRUNCATE);
			layout = docLayout;
			if (docSsg) FmMonShadowSetSsgClock(docSsg);
			if (docKeys) FmMonShadowSetKeysProfile(docKeys);
			/* 組み立てた集合はもう OPM 形とは限らないので、
			   それを前提にした OPM snapshot 種まきを落とす。 */
			if (docLayout != -1) seedOpm = 0;
		}
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
	} else if (_stricmp(sub, "68k2") == 0) {
		FmMonShadowSetMsxDevices(SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_OPLL);
	}
	/* Open が既にレジスタを積んでいるときは空 snapshot で消さない。
	   layout=-1 + KEYS_MDX だけで Flush が OPNA 形に落ちない。 */
	if (seedOpm)
		FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	/* 確定した identity/layout で 1 枚必ず出す。最初の Render 待ちにすると
	   その間 UI が前曲または既定 OPNA の shell を出す（type 不一致の窓） */
	FmMonShadowFlush(1);
}

void CEmuFmMonBeginOpen(const CEmuGameEntry* ge, const wchar_t* zipPath, int sampleRate)
{
	(void)ge;
	if (FmMonShadowIsHeld())
		return;
	FmMonShadowReset();
	if (zipPath && zipPath[0])
		FmMonShadowSetSource(zipPath);
	FmMonShadowSetSampleRate((uint32_t)(sampleRate > 0 ? sampleRate : 44100));
}
