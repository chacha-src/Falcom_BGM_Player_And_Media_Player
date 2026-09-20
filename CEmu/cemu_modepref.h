#pragma once
#include "cemu_types.h"
#include "cemu_catalog.h"
#include "cemu_zipfs.h"

/* 複数 subtype アーカイブ向けのプレイリストチップ / コンテキストメニューラベル。
   サイドカーは %LOCALAPPDATA%\oggYSED\cemumode\ — playlistdata0 / save.dat は触らない。 */

enum { CEMU_MODE_MAX = 24, CEMU_MODE_TAG = 16 };

struct CEmuArchiveMode {
	char tag[CEMU_MODE_TAG];   /* OPNA / OPN / OPM / OPLL / MIDI / 86 / BEEP … */
	char subtype[CEMU_DRIVER_TYPE];
	int isMidi;
	int entryIndex;            /* CEmuCatalog::entry の index。無ければ -1 */
};

/* カタログエントリ → 表示タグ。成功で 1 */
int CEmuModeTagFromEntry(const CEmuGameEntry* e, char* tag, int tagCap);

int CEmuModeIsMidiTag(const char* tag);

/* アーカイブのユニークモード（tag で重複排除）。既定順は非 MIDI 優先 */
int CEmuCatalogListArchiveModes(const CEmuCatalog* cat, const char* archive,
	const char* dataDirHint, const CEmuZipFs* zipFs,
	CEmuArchiveMode* out, int outCap);

/* FindArchiveForZip 相当。preferTag があればそのモード、無ければ最良の非 MIDI */
const CEmuGameEntry* CEmuCatalogFindArchiveForZipMode(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint, const CEmuZipFs* zipFs,
	const char* preferTag);

/* zip 物理パスごとのモード好み。空 tag で解除 */
int CEmuModePrefGet(const wchar_t* zipPath, char* tagOut, int tagCap);
void CEmuModePrefSet(const wchar_t* zipPath, const char* tag);

/* zip ごとのカタログトグル（Food empty / TO BOSS 等）。%LOCALAPPDATA%\oggYSED\cemutoggle\ */
int CEmuTogglePrefGet(const wchar_t* zipPath, unsigned* codes, int cap);
void CEmuTogglePrefSet(const wchar_t* zipPath, const unsigned* codes, int n);
int CEmuTogglePrefHas(const wchar_t* zipPath, unsigned code);
int CEmuTogglePrefFlip(const wchar_t* zipPath, unsigned code);

/* zip 内の最初の .mid/.rmi/.smf → KPI/VST MIDI 再生用 temp */
int CEmuZipExtractFirstMidi(const wchar_t* zipPath, wchar_t* outMidPath, int outCap);

/* この title のカタログ曲ファイルが SMF なら抽出（offset は title code と一致） */
int CEmuZipExtractCatalogMidi(const wchar_t* zipPath, const CEmuGameEntry* ge,
	unsigned titleCode, wchar_t* outMidPath, int outCap);

/* PCAT midiout glue を起動し、MPU-401 UART を Type-0 SMF に捕捉して KPI/VST へ */
int CEmuCapturePcatMidiToFile(const wchar_t* zipPath, unsigned titleCode,
	wchar_t* outMidPath, int outCap);

/* リアルタイム流: CEmuMidiLive* は cemu_midi_live.h（スタブ SMF + inject） */
