#include "StdAfx.h"
#include "cemu_zipfs.h"
#include "minizip/unzip.h"
#include "minizip/iowin32.h"
#include <string.h>

/* 大文字小文字・スラッシュを無視したパス一致 */
static int CEmuZipNameMatch(const char* a, const char* b)
{
	if (!a || !b) return 0;
	for (;;) {
		char ca = *a;
		char cb = *b;
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
		if (ca == '\\') ca = '/';
		if (cb == '\\') cb = '/';
		if (ca != cb) return 0;
		if (!ca) return 1;
		a++;
		b++;
	}
}

/* ASCII 骨格: 英数字だけ残すので epr11112 ↔ epr-11112.17 が一致する */
static void CEmuZipNameSkeleton(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	int o = 0;
	for (const unsigned char* p = (const unsigned char*)in; *p && o + 1 < outCap; p++) {
		unsigned char c = *p;
		if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			out[o++] = (char)c;
		else if (c >= 0x80)
			out[o++] = '?';
	}
	out[o] = 0;
}

/* 最長の連続数字 (例 epr-16720.7 → "16720")。カタログは "16720.epr"、
   MAME zip は "epr-16720.7" になりがち。 */
static int CEmuZipDigitRun(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return 0;
	out[0] = 0;
	if (!in) return 0;
	const char* best = NULL;
	int bestLen = 0;
	for (const char* p = in; *p; ) {
		if (*p < '0' || *p > '9') { p++; continue; }
		const char* s = p;
		while (*p >= '0' && *p <= '9') p++;
		const int n = (int)(p - s);
		if (n > bestLen) { best = s; bestLen = n; }
	}
	if (!best || bestLen < 4 || bestLen + 1 > outCap) return 0;
	memcpy(out, best, (size_t)bestLen);
	out[bestLen] = 0;
	return bestLen;
}

/* 骨格一致、だめなら数字コア一致 */
static int CEmuZipNameFuzzy(const char* a, const char* b)
{
	char sa[CEMU_ROM_NAME], sb[CEMU_ROM_NAME];
	CEmuZipNameSkeleton(a, sa, (int)sizeof(sa));
	CEmuZipNameSkeleton(b, sb, (int)sizeof(sb));
	if (!sa[0] || !sb[0]) return 0;
	if (CEmuZipNameMatch(sa, sb)) return 1;
	/* 数字コア: 16720.epr ↔ epr-16720.7, 16491.mpr ↔ mpr-16491.32 */
	char da[32], db[32];
	if (CEmuZipDigitRun(a, da, (int)sizeof(da)) && CEmuZipDigitRun(b, db, (int)sizeof(db))
		&& CEmuZipNameMatch(da, db))
		return 1;
	return 0;
}

/* 拡張子を落とす (MMD2.SYS → MMD2) */
static void CEmuZipStripExt(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	strncpy_s(out, (size_t)outCap, in, _TRUNCATE);
	char* dot = strrchr(out, '.');
	if (dot && dot != out) *dot = 0;
}

/* ディレクトリを除いたベース名 */
static void CEmuZipBaseName(const char* path, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!path) return;
	const char* slash = path;
	for (const char* p = path; *p; p++) {
		if (*p == '/' || *p == '\\') slash = p + 1;
	}
	strncpy_s(out, (size_t)outCap, slash, _TRUNCATE);
}

void CEmuZipFsClose(CEmuZipFs* fs)
{
	if (!fs) return;
	/* 未初期化スタック／malloc 生ポインタでも fileCount 暴走で delete しない */
	int n = fs->fileCount;
	if (n < 0 || n > 512) n = 0;
	for (int i = 0; i < n; i++) {
		if (fs->files[i].data) {
			delete[] fs->files[i].data;
			fs->files[i].data = NULL;
		}
		fs->files[i].path[0] = 0;
		fs->files[i].size = 0;
	}
	fs->fileCount = 0;
	fs->zipPath[0] = 0;
	fs->namesOnly = 0;
}

/* zip メンバを fs へ追加。namesOnly なら展開しない */
static int CEmuZipFsAppendEx(CEmuZipFs* fs, const wchar_t* zipPath, int namesOnly)
{
	if (!fs || !zipPath || !zipPath[0]) return 0;
	if (fs->fileCount < 0 || fs->fileCount > 512)
		fs->fileCount = 0;

	zlib_filefunc64_def ffunc;
	fill_win32_filefunc64W(&ffunc);
	unzFile uf = unzOpen2_64(zipPath, &ffunc);
	if (!uf) return 0;

	if (unzGoToFirstFile(uf) != UNZ_OK) {
		unzClose(uf);
		return 0;
	}

	int added = 0;
	do {
		if (fs->fileCount >= 512) break;
		unz_file_info64 fi;
		char fn[512];
		if (unzGetCurrentFileInfo64(uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK)
			continue;
		if (fi.uncompressed_size == 0 || fi.uncompressed_size > 64u * 1024u * 1024u)
			continue;
		const size_t fnLen = strlen(fn);
		if (fnLen > 0 && (fn[fnLen - 1] == '/' || fn[fnLen - 1] == '\\'))
			continue;

		CEmuZipFile* ent = &fs->files[fs->fileCount];
		MultiByteToWideChar(932, 0, fn, -1, ent->path, CEMU_ZIP_PATH);
		ent->size = (unsigned)fi.uncompressed_size;
		ent->data = NULL;

		if (!namesOnly) {
			if (unzOpenCurrentFile(uf) != UNZ_OK)
				continue;
			unsigned char* buf = new unsigned char[(size_t)fi.uncompressed_size + 4];
			if (!buf) {
				unzCloseCurrentFile(uf);
				continue;
			}
			int rd = unzReadCurrentFile(uf, buf, (unsigned)fi.uncompressed_size);
			unzCloseCurrentFile(uf);
			if (rd != (int)fi.uncompressed_size) {
				delete[] buf;
				continue;
			}
			buf[fi.uncompressed_size] = 0;
			ent->data = buf;
		}
		fs->fileCount++;
		added++;
	} while (unzGoToNextFile(uf) == UNZ_OK);

	unzClose(uf);
	return added > 0 ? 1 : 0;
}

static int CEmuZipFsOpenEx(CEmuZipFs* fs, const wchar_t* zipPath, int namesOnly)
{
	if (!fs || !zipPath || !zipPath[0]) return 0;
	if (fs->fileCount < 0 || fs->fileCount > 512)
		fs->fileCount = 0;
	CEmuZipFsClose(fs);
	wcsncpy_s(fs->zipPath, zipPath, _TRUNCATE);
	fs->namesOnly = namesOnly ? 1 : 0;
	return CEmuZipFsAppendEx(fs, zipPath, namesOnly);
}

int CEmuZipFsOpen(CEmuZipFs* fs, const wchar_t* zipPath)
{
	return CEmuZipFsOpenEx(fs, zipPath, 0);
}

/* カタログ順位付け用: 展開せずメンバ一覧だけ */
int CEmuZipFsOpenNames(CEmuZipFs* fs, const wchar_t* zipPath)
{
	return CEmuZipFsOpenEx(fs, zipPath, 1);
}

/* 既に開いた fs へ別 zip のメンバを追加（カンマ同伴 zip） */
int CEmuZipFsMergeZip(CEmuZipFs* fs, const wchar_t* zipPath)
{
	if (!fs || !zipPath || !zipPath[0]) return 0;
	/* 同伴 zip を足すとき、元の namesOnly を維持する */
	return CEmuZipFsAppendEx(fs, zipPath, fs->namesOnly);
}

/* フルパス優先。拡張子無し名は COM を SYS と取り違えないよう制限する */
static int CEmuZipFsFindIndex(const CEmuZipFs* fs, const char* name)
{
	if (!fs || !name) return -1;
	char base[CEMU_ROM_NAME];
	CEmuZipBaseName(name, base, (int)sizeof(base));
	char baseNoExt[CEMU_ROM_NAME];
	CEmuZipStripExt(base, baseNoExt, (int)sizeof(baseNoExt));

	for (int i = 0; i < fs->fileCount; i++) {
		char fn[CEMU_ROM_NAME];
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		CEmuZipBaseName(pathA, fn, (int)sizeof(fn));
		/* ディレクトリ付き名 (ran2/patch vs mzz/patch) は
		   zip 先頭の同名ベースに潰してはいけない。 */
		if (CEmuZipNameMatch(pathA, name))
			return i;
		if (!strchr(name, '/') && !strchr(name, '\\')
			&& CEmuZipNameMatch(fn, base))
			return i;
	}
	for (int i = 0; i < fs->fileCount; i++) {
		char fn[CEMU_ROM_NAME];
		char fnNoExt[CEMU_ROM_NAME];
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		CEmuZipBaseName(pathA, fn, (int)sizeof(fn));
		CEmuZipStripExt(fn, fnNoExt, (int)sizeof(fnNoExt));
		/* 拡張子無しフォールバックはカタログの "MMD2" 用。
		   `MMD2.SYS 4096` (CONFIG 末尾) を "MMD2" に削ると zip 先頭の
		   mmd2.com に当たり、AddFile が本物の SYS を上書きした
		   (orangerd の device INIT が CS:0 の COM: FA/CLI、pic=FF、dosmiss=intD2)。 */
		if (strchr(name, '/') || strchr(name, '\\'))
			continue;
		const int queryHasExt = (strchr(base, '.') != NULL);
		if ((!queryHasExt && baseNoExt[0] && CEmuZipNameMatch(fnNoExt, baseNoExt))
			|| CEmuZipNameFuzzy(fn, base))
			return i;
	}
	return -1;
}

const unsigned char* CEmuZipFsFind(const CEmuZipFs* fs, const char* name, unsigned* outSize)
{
	if (outSize) *outSize = 0;
	const int idx = CEmuZipFsFindIndex(fs, name);
	if (idx < 0) return NULL;
	if (outSize) *outSize = fs->files[idx].size;
	if (fs->namesOnly || !fs->files[idx].data) {
		/* names-only: ヒット数用に size>0 なら非 NULL で存在を知らせる */
		return fs->files[idx].size > 0 ? (const unsigned char*)1 : NULL;
	}
	return fs->files[idx].data;
}

int CEmuZipFsHas(const CEmuZipFs* fs, const char* name, unsigned* outSize)
{
	const int idx = CEmuZipFsFindIndex(fs, name);
	if (idx < 0) {
		if (outSize) *outSize = 0;
		return 0;
	}
	if (outSize) *outSize = fs->files[idx].size;
	return 1;
}

/* ベース名 / フルパス一致のみ — 数字コアのあいまい一致なし（カタログ順位用） */
int CEmuZipFsHasExact(const CEmuZipFs* fs, const char* name, unsigned* outSize)
{
	if (outSize) *outSize = 0;
	if (!fs || !name) return 0;
	char base[CEMU_ROM_NAME];
	CEmuZipBaseName(name, base, (int)sizeof(base));
	for (int i = 0; i < fs->fileCount; i++) {
		char fn[CEMU_ROM_NAME];
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		CEmuZipBaseName(pathA, fn, (int)sizeof(fn));
		if (CEmuZipNameMatch(fn, base) || CEmuZipNameMatch(pathA, name)) {
			if (outSize) *outSize = fs->files[i].size;
			return fs->files[i].size > 0 ? 1 : 0;
		}
	}
	return 0;
}

int CEmuZipFsExtractOne(const wchar_t* zipPath, const char* innerName,
	unsigned char* buf, unsigned bufCap, unsigned* outSize)
{
	if (outSize) *outSize = 0;
	if (!zipPath || !innerName || !buf || bufCap == 0) return 0;

	zlib_filefunc64_def ffunc;
	fill_win32_filefunc64W(&ffunc);
	unzFile uf = unzOpen2_64(zipPath, &ffunc);
	if (!uf) return 0;

	char base[CEMU_ROM_NAME];
	CEmuZipBaseName(innerName, base, (int)sizeof(base));
	int found = 0;
	if (unzGoToFirstFile(uf) == UNZ_OK) {
		do {
			unz_file_info64 fi;
			char fn[512];
			if (unzGetCurrentFileInfo64(uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK)
				continue;
			char fnBase[CEMU_ROM_NAME];
			CEmuZipBaseName(fn, fnBase, (int)sizeof(fnBase));
			if (!CEmuZipNameMatch(fnBase, base) && !CEmuZipNameMatch(fn, innerName))
				continue;
			if (fi.uncompressed_size > bufCap) break;
			if (unzOpenCurrentFile(uf) != UNZ_OK) break;
			int rd = unzReadCurrentFile(uf, buf, (unsigned)fi.uncompressed_size);
			unzCloseCurrentFile(uf);
			if (rd == (int)fi.uncompressed_size) {
				if (outSize) *outSize = (unsigned)fi.uncompressed_size;
				found = 1;
			}
			break;
		} while (unzGoToNextFile(uf) == UNZ_OK);
	}
	unzClose(uf);
	return found;
}
