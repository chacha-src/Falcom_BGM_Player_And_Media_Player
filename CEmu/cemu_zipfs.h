#pragma once
#include "cemu_types.h"

/* minizip による ZIP 仮想 FS (メモリ展開) */
struct CEmuZipFile {
	wchar_t path[CEMU_ZIP_PATH];
	unsigned char* data;
	unsigned size;
};

struct CEmuZipFs {
	wchar_t zipPath[CEMU_ZIP_PATH];
	int fileCount;
	CEmuZipFile files[512];
	int namesOnly; /* 1 = パス/サイズのみ。Find は data=NULL でも size は有効 */
};

int CEmuZipFsOpen(CEmuZipFs* fs, const wchar_t* zipPath);
/* カタログ順位付け用: 展開せずメンバ一覧だけ（高速・低 RAM） */
int CEmuZipFsOpenNames(CEmuZipFs* fs, const wchar_t* zipPath);
/* 既に開いた fs へ別 zip のメンバを追加（カンマ同伴 zip） */
int CEmuZipFsMergeZip(CEmuZipFs* fs, const wchar_t* zipPath);
void CEmuZipFsClose(CEmuZipFs* fs);
const unsigned char* CEmuZipFsFind(const CEmuZipFs* fs, const char* name, unsigned* outSize);
/* サイズだけの照会。namesOnly でも size>0 ならダミー非 NULL を返す */
int CEmuZipFsHas(const CEmuZipFs* fs, const char* name, unsigned* outSize);
/* ベース名 / フルパス一致のみ — 数字コアのあいまい一致なし（カタログ順位用） */
int CEmuZipFsHasExact(const CEmuZipFs* fs, const char* name, unsigned* outSize);
int CEmuZipFsExtractOne(const wchar_t* zipPath, const char* innerName,
	unsigned char* buf, unsigned bufCap, unsigned* outSize);
