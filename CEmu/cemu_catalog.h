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

/* 同一アーカイブの候補を zip メンバ一致で順位付け。open/play の再試行用 */
int CEmuCatalogCollectArchiveForZip(const CEmuCatalog* cat,
	const char* archive, const CEmuZipFs* zipFs,
	const CEmuGameEntry** out, int outCap);

int CEmuCatalogParseFile(CEmuCatalog* cat, const wchar_t* xmlPath, const char* dataDirHint);
int CEmuCatalogParseBuffer(CEmuCatalog* cat, const char* xmlText, const char* dataDirHint);

void CEmuCatalogAssignHwIds(CEmuGameEntry* ge);
/* ge->name に書かれたチップ名から ge->docChipIds を埋める */
void CEmuCatalogAssignDocChips(CEmuGameEntry* ge);
/* テキスト中のチップ ID。rip 側の表記ゆれを全部見る。書いた件数を返す */
int CEmuCatalogChipsFromText(const char* text, int* ids, int maxIds);
/* 同一アーカイブの重複行で、チップ名がある行の docChip を共有する */
void CEmuCatalogShareDocChips(CEmuCatalog* cat);

int CEmuArchiveStemFromPath(const wchar_t* zipPath, char* out, int outCap);

int CEmuGameTitleCount(const CEmuGameEntry* ge);
int CEmuGameTitleAt(const CEmuGameEntry* ge, int index0, unsigned* outCode,
	wchar_t* outLabel, int outLabelChars);
unsigned CEmuGameTitleCodeForIndex(const CEmuGameEntry* ge, unsigned titleIndex1);
/* hoot titlelist: SE / 効果音 / SFX / Sound Effect. STOP は含めない。 */
int CEmuTitleLooksLikeSfx(const wchar_t* label);
int CEmuGameTitleLooksLikeSfx(const CEmuGameEntry* ge, unsigned titleIndex1);
