#include "StdAfx.h"
#include "cemu_zipfs.h"
#include "minizip/unzip.h"
#include "minizip/iowin32.h"
#include <string.h>

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

/* ASCII skeleton: keep letters/digits only so epr11112 ↔ epr-11112.17 match. */
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

/* Longest consecutive digit run (e.g. epr-16720.7 → "16720"). Catalog
   often ships "16720.epr" while MAME zips use "epr-16720.7". */
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

static int CEmuZipNameFuzzy(const char* a, const char* b)
{
	char sa[CEMU_ROM_NAME], sb[CEMU_ROM_NAME];
	CEmuZipNameSkeleton(a, sa, (int)sizeof(sa));
	CEmuZipNameSkeleton(b, sb, (int)sizeof(sb));
	if (!sa[0] || !sb[0]) return 0;
	if (CEmuZipNameMatch(sa, sb)) return 1;
	/* Digit-core: 16720.epr ↔ epr-16720.7, 16491.mpr ↔ mpr-16491.32 */
	char da[32], db[32];
	if (CEmuZipDigitRun(a, da, (int)sizeof(da)) && CEmuZipDigitRun(b, db, (int)sizeof(db))
		&& CEmuZipNameMatch(da, db))
		return 1;
	return 0;
}

static void CEmuZipStripExt(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	strncpy_s(out, (size_t)outCap, in, _TRUNCATE);
	char* dot = strrchr(out, '.');
	if (dot && dot != out) *dot = 0;
}

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

int CEmuZipFsOpenNames(CEmuZipFs* fs, const wchar_t* zipPath)
{
	return CEmuZipFsOpenEx(fs, zipPath, 1);
}

int CEmuZipFsMergeZip(CEmuZipFs* fs, const wchar_t* zipPath)
{
	if (!fs || !zipPath || !zipPath[0]) return 0;
	/* Keep primary namesOnly mode when merging companions. */
	return CEmuZipFsAppendEx(fs, zipPath, fs->namesOnly);
}

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
		if (CEmuZipNameMatch(fn, base) || CEmuZipNameMatch(pathA, name))
			return i;
	}
	for (int i = 0; i < fs->fileCount; i++) {
		char fn[CEMU_ROM_NAME];
		char fnNoExt[CEMU_ROM_NAME];
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		CEmuZipBaseName(pathA, fn, (int)sizeof(fn));
		CEmuZipStripExt(fn, fnNoExt, (int)sizeof(fnNoExt));
		if ((baseNoExt[0] && CEmuZipNameMatch(fnNoExt, baseNoExt))
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
		/* names-only: signal presence via non-NULL when size>0 for hit counting */
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
