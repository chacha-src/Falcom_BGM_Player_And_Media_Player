#pragma once
// CEmu — hoot アーカイブ互換サウンドエミュレーション (exe 内完結、オープンソース・期限なし)
// mode/sub 帯: MODE_CEMU_BASE (-1000) から CPU / チップ / ドライバを割当

#include <stdint.h>

/* playlistdata.sub — 既存 -30(VST) 等と衝突しない帯 */
enum {
	MODE_CEMU_BASE = -1000,
	MODE_CEMU = -1000
};

/* チップ ID: MODE_CEMU_BASE + 1 .. +99 (音源) */
enum {
	CEMU_CHIP_OPNA = 1,
	CEMU_CHIP_OPN = 2,
	CEMU_CHIP_OPM = 3,
	CEMU_CHIP_OPL2 = 4,
	CEMU_CHIP_OPL3 = 5,
	CEMU_CHIP_AY = 6,
	CEMU_CHIP_YM2612 = 7,
	CEMU_CHIP_QSOUND = 8,
	CEMU_CHIP_SEGAPCM = 9,
	CEMU_CHIP_RF5C68 = 10,
	CEMU_CHIP_C352 = 11,
	CEMU_CHIP_SN76489 = 12,
	CEMU_CHIP_OPLL = 13,
	CEMU_CHIP_PCM86 = 14,
	CEMU_CHIP_PPZ = 15,
	CEMU_CHIP_NEO_PCM = 16,
	CEMU_CHIP_K053260 = 17,
	CEMU_CHIP_K054539 = 18,
	CEMU_CHIP_YM2610 = 19,
	CEMU_CHIP_ES5505 = 20,
	CEMU_CHIP_OKI6295 = 21,
	CEMU_CHIP_GA20 = 22,
	CEMU_CHIP_IREM_DAC = 23,
	CEMU_CHIP_C140 = 24,
	CEMU_CHIP_C30 = 25, /* Namco CUS30 / 15XX wavetable */
	CEMU_CHIP_MAX = 99
};

/* CPU / マシン ID: MODE_CEMU_BASE + 100 .. +199 */
enum {
	CEMU_CPU_Z80 = 100,
	CEMU_CPU_I286 = 101,
	CEMU_CPU_M68000 = 102,
	CEMU_CPU_M6809 = 103,
	CEMU_CPU_H6280 = 104,
	CEMU_CPU_V30 = 105,
	CEMU_CPU_MAX = 199
};

/* ドライバ subtype 層: MODE_CEMU_BASE + 200 .. (hoot driver type 文字列と対応) */
enum {
	CEMU_DRV_BASE = 200
};

enum {
	CEMU_CATALOG_MAX = 16384,
	CEMU_ARCHIVE_NAME = 64,
	CEMU_GAME_NAME = 160,
	CEMU_DRIVER_NAME = 32,
	CEMU_DRIVER_TYPE = 32,
	CEMU_DATA_DIR = 16,
	CEMU_ROM_NAME = 128,
	/* Fixed slot budget: parse prefers code/bgm/voice over adpcm when full.
	   arcus2 OPNA has 103 rows — without priority, late BGM banks are dropped. */
	CEMU_ROM_MAX = 128,
	CEMU_TITLE_MAX = 256,
	CEMU_OPTION_MAX = 32,
	CEMU_ZIP_PATH = 520
};

#pragma pack(push, 1)
struct CEmuRomEntry {
	char type[16];
	char name[CEMU_ROM_NAME];
	int offset;
};

struct CEmuOptionEntry {
	char name[32];
	char value[32];
};

struct CEmuTitleEntry {
	unsigned code;
	wchar_t label[CEMU_GAME_NAME];
};
#pragma pack(pop)

/* rom/opt/title は実件数分だけヒープ確保（固定 100KB/件だと 8k 件でハングする） */
struct CEmuGameEntry {
	wchar_t name[CEMU_GAME_NAME];
	wchar_t driverAlias[CEMU_GAME_NAME];
	char platform[CEMU_DRIVER_NAME];
	char subtype[CEMU_DRIVER_TYPE];
	char dataDir[CEMU_DATA_DIR];
	char archive[CEMU_ARCHIVE_NAME];
	int romCount;
	int optCount;
	int titleCount;
	CEmuRomEntry* rom;
	CEmuOptionEntry* opt;
	CEmuTitleEntry* title;
	int cpuId;
	int chipIds[8];
	int chipCount;
};

inline int CEmuModeFromChip(int chipId)
{
	return MODE_CEMU_BASE - chipId;
}

inline int CEmuModeFromCpu(int cpuId)
{
	return MODE_CEMU_BASE - cpuId;
}

inline bool CEmuIsCemuMode(int sub)
{
	return sub <= MODE_CEMU_BASE && sub > (MODE_CEMU_BASE - 256);
}
