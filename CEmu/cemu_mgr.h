#pragma once
#include "cemu_types.h"
#include "cemu_catalog.h"
#include "cemu_zipfs.h"

struct CEmuMgr {
	wchar_t dataRoot[MAX_PATH];
	CEmuCatalog catalog;
	int ready;
};

CEmuMgr* CEmuMgrGet();
void CEmuMgrShutdown();

/* exe 隣 data\ を既定。Init は起動時 1 回（ルート設定のみ・カタログは遅延） */
int CEmuMgrInit(CEmuMgr* m, const wchar_t* dataRootOverride);

/* 既定ルート（exe\\data）を解決。savedPath は互換のため残すが通常は無視 */
void CEmuMgrGetEffectiveDataRoot(const wchar_t* savedPath, wchar_t* out, int outChars);

/* 初回利用時にカタログを読込。既に読込済みなら no-op */
int CEmuMgrEnsureCatalog(CEmuMgr* m);

/* 進捗付き。起動時 KPI 読込画面などから呼ぶ */
int CEmuMgrEnsureCatalogEx(CEmuMgr* m, CEmuCatalogProgressFn progress, void* progressUser);

/* データパス変更後にカタログを再読込（ディスクキャッシュは維持。更新時は Invalidate） */
int CEmuMgrReload(CEmuMgr* m, const wchar_t* dataRootOverride);

/* D&D / 再生: zip パスからゲーム特定。outZipPath=実 zip フルパス */
const CEmuGameEntry* CEmuMgrResolveZip(CEmuMgr* m, const wchar_t* droppedZip,
	wchar_t* outZipPath, int outZipChars, char* outDataDir, int outDataDirCap);

/* platform 文字列 → data サブフォルダ名 */
void CEmuMgrPlatformToDataDir(const char* platform, char* out, int outCap);

/* path::0001 — 物理 zip と 1 始まり曲番号を分離 */
int CEmuParseVirtualPath(const wchar_t* in, wchar_t* outPhysical, int outPhysChars,
	unsigned* outTitleIndex1);

/* フルパス zip + 曲番 → …\\artofwar.zip::0001 */
void CEmuFormatVirtualPath(const wchar_t* zipPhysical, unsigned titleIndex1,
	wchar_t* out, int outChars);

/* 表示用 basename::0001 */
void CEmuFormatVirtualBasename(const wchar_t* zipPhysical, unsigned titleIndex1,
	wchar_t* out, int outChars);
