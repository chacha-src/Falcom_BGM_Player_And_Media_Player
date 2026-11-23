#pragma once
/*
  oxz 独自書庫 (OXZPKG)。7z とは別の印。
  ファイルごとに複数の公開コーデックを試し、1 バイトでも小さい方を残す。
  512KB を超えるファイルは先頭 512KB を各コーデック同時に試し、その方式で全体を圧縮する。
  PPMd は展開が重いので 8MB を超えるファイルには使わない。それ以下でも他方式より 5% 以上小さいときだけ使う。
  同点なら展開の軽い方。拡張子では除外しない。サイズは 64 ビット。
  既存の 7z 形式 oxz は base としてそのまま持ち、変わったファイルだけ足す。

  Zstandard  https://github.com/facebook/zstd        BSD / GPLv2
  LZ4        https://github.com/lz4/lz4              BSD-2-Clause
  libdeflate https://github.com/ebiggers/libdeflate  MIT
  Brotli     https://github.com/google/brotli        MIT
  Lizard     https://github.com/inikep/lizard        BSD-2-Clause
  LZFSE      https://github.com/lzfse/lzfse          BSD
  bzip2      https://sourceware.org/bzip2/           bzip2 license
  LZMA2 / PPMd  Igor Pavlov, Dmitry Shkarin, public domain
*/
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OXZ_CORE_OK 0
#define OXZ_CORE_ERR 2
#define OXZ_CORE_EMPTY 3

typedef void (*OxzCoreProgFn)(const wchar_t* name, unsigned index, unsigned total, int pct, void* user);
/* 埋め込み 7z を dest へ展開する。0 で成功。 */
typedef int (*OxzCoreBaseFn)(const wchar_t* sevenPath, const wchar_t* destDir, void* user);

int OxzCoreIsPkg(const wchar_t* path);
void OxzCoreForget(void);
/* 展開直後の作業フォルダを覚える。archive は 7z 形式 oxz、OXZPKG、または無い。 */
int OxzCoreRemember(const wchar_t* workDir, const wchar_t* archiveOrNull);
/* 作業フォルダを書庫にする。変わっていない圧縮済みデータはコピーする。 */
int OxzCoreSave(const wchar_t* workDir, const wchar_t* archive, unsigned long long* outBytes,
    OxzCoreProgFn prog, void* progUser);
int OxzCoreExtract(const wchar_t* archive, const wchar_t* dest,
    OxzCoreBaseFn baseFn, void* baseUser, OxzCoreProgFn prog, void* progUser);

typedef struct OxzCoreItem {
	wchar_t* name;
	int isDir;
	int tomb;
	int overlay; /* ディレクトリ上の番号。overlay で無い項目は -1 */
	uint64_t unpacked;
	uint64_t mtime;
	uint16_t method;
	uint16_t flags;
	uint64_t packed;
} OxzCoreItem;

int OxzCoreReadDir(const wchar_t* archive, OxzCoreItem** items, int* count,
    uint64_t* baseOff, uint64_t* baseSize);
void OxzCoreFreeItems(OxzCoreItem* items, int count);
/* overlay を展開し、RIP も戻したバイトを malloc する。 */
int OxzCoreReadOverlay(const wchar_t* archive, int overlay, void** data, size_t* size);
void OxzCoreFree(void* p);

#ifdef __cplusplus
}
#endif
