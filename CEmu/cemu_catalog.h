#pragma once
#include "cemu_types.h"

struct CEmuZipFs;

struct CEmuCatalog {
	int count;
	int capacity;
	CEmuGameEntry** entry; /* heap 上の各エントリ */
	int loaded; /* 1 = 読込試行済み（件数 0 でも再読込しない） */
};

typedef void (*CEmuCatalogProgressFn)(int pos, int max, void* user);

void CEmuCatalogInit(CEmuCatalog* cat);
void CEmuCatalogClear(CEmuCatalog* cat);

/* data ルートから xml を読込。arcdata.zip は exe 隣のみ。キャッシュがあればスキップ */
int CEmuCatalogLoad(CEmuCatalog* cat, const wchar_t* dataRoot);
int CEmuCatalogLoadEx(CEmuCatalog* cat, const wchar_t* dataRoot,
	CEmuCatalogProgressFn progress, void* progressUser);

/* %LOCALAPPDATA%\oggYSED\cemucatalog\ のキャッシュを破棄 */
void CEmuCatalogInvalidateCache(void);

/* arcdata.zip が未更新なら 1（起動 UI 用・size+flags ヘッダ照合のみ） */
int CEmuCatalogCacheIsCurrent(const wchar_t* dataRoot);

/* exe 隣の arcdata.zip パス（存在しなくてもパスを返す） */
void CEmuCatalogGetExeArcdataPath(wchar_t* out, int outChars);

const CEmuGameEntry* CEmuCatalogFindArchive(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint);

const CEmuGameEntry* CEmuCatalogFindArchiveForZip(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint, const CEmuZipFs* zipFs);

/* Ranked list of same-archive catalog rows (zip member hits). For open/play retry. */
int CEmuCatalogCollectArchiveForZip(const CEmuCatalog* cat,
	const char* archive, const CEmuZipFs* zipFs,
	const CEmuGameEntry** out, int outCap);

int CEmuCatalogParseFile(CEmuCatalog* cat, const wchar_t* xmlPath, const char* dataDirHint);
int CEmuCatalogParseBuffer(CEmuCatalog* cat, const char* xmlText, const char* dataDirHint);

void CEmuCatalogAssignHwIds(CEmuGameEntry* ge);

int CEmuArchiveStemFromPath(const wchar_t* zipPath, char* out, int outCap);

int CEmuGameTitleCount(const CEmuGameEntry* ge);
int CEmuGameTitleAt(const CEmuGameEntry* ge, int index0, unsigned* outCode,
	wchar_t* outLabel, int outLabelChars);
unsigned CEmuGameTitleCodeForIndex(const CEmuGameEntry* ge, unsigned titleIndex1);
