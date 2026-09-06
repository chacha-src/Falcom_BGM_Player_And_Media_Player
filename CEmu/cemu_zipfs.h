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
	int namesOnly; /* 1 = path/size only; Find returns NULL data but size OK */
};

int CEmuZipFsOpen(CEmuZipFs* fs, const wchar_t* zipPath);
/* Catalog ranking: list members without decompressing (fast, low RAM). */
int CEmuZipFsOpenNames(CEmuZipFs* fs, const wchar_t* zipPath);
/* Append another zip's members into an already-open fs (comma companions). */
int CEmuZipFsMergeZip(CEmuZipFs* fs, const wchar_t* zipPath);
void CEmuZipFsClose(CEmuZipFs* fs);
const unsigned char* CEmuZipFsFind(const CEmuZipFs* fs, const char* name, unsigned* outSize);
/* Size-only lookup works for namesOnly fs (returns non-NULL dummy if size>0). */
int CEmuZipFsHas(const CEmuZipFs* fs, const char* name, unsigned* outSize);
int CEmuZipFsExtractOne(const wchar_t* zipPath, const char* innerName,
	unsigned char* buf, unsigned bufCap, unsigned* outSize);
