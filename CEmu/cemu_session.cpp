#include "StdAfx.h"
#include "cemu_session.h"
#include "cemu_mgr.h"
#include "cemu_zipfs.h"
#include "cemu_mdx.h"
#include "cemu_modepref.h"
#include "machine/cemu_hard_pcat.h"
#include <string.h>
#include <stdlib.h>

void CEmuSessionInit(CEmuSession* s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->channels = 2;
}

void CEmuSessionClose(CEmuSession* s)
{
	if (!s) return;
	CEmuS98Close(&s->s98);
	CEmuMdxClose(&s->mdx);
	CEmuPc88Close(&s->pc88);
	CEmuPc98Close(&s->pc98);
	CEmuAcClose(&s->ac);
	CEmuX68kClose(&s->x68k);
	CEmuSg1000Close(&s->sg1000);
	CEmuX1Close(&s->x1);
	CEmuPcatClose(&s->pcat);
	CEmuF3Close(&s->f3);
	CEmuMsxClose(&s->msx);
	CEmuFm7Close(&s->fm7);
	memset(s, 0, sizeof(*s));
	s->channels = 2;
}

static int CEmuSessionIsMdxName(const char* fn)
{
	size_t n = fn ? strlen(fn) : 0;
	if (n < 4) return 0;
	if (_stricmp(fn + n - 4, ".mdx") == 0) return 1;
	if (_stricmp(fn + n - 4, ".mdc") == 0) return 1;
	if (_stricmp(fn + n - 4, ".fmx") == 0) return 1;
	return 0;
}

static void CEmuSessionZipBaseName(const char* path, char* out, int outCap)
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

static const unsigned char* CEmuSessionFindPdxInZip(CEmuZipFs* fs, const char* pdxName, unsigned* outSize)
{
	if (outSize) *outSize = 0;
	if (!fs || !pdxName || !pdxName[0]) return NULL;
	char tryName[CEMU_ZIP_PATH];
	strncpy_s(tryName, pdxName, _TRUNCATE);
	const unsigned char* data = CEmuZipFsFind(fs, tryName, outSize);
	if (data) return data;
	size_t n = strlen(tryName);
	if (n < 4 || _stricmp(tryName + n - 4, ".pdx") != 0) {
		strncat_s(tryName, ".pdx", _TRUNCATE);
		data = CEmuZipFsFind(fs, tryName, outSize);
		if (data) return data;
	}
	return NULL;
}

/* MDD shared PCM.DAT: 64×(BE32 start, BE32 end) + ADPCM payload @512.
   Convert to MXDRV PDX (96× ptr+len) so portable_mdx can bind the bank. */
static unsigned char* CEmuSessionConvertPcmDatToPdx(const unsigned char* pcm, unsigned pcmSz,
	unsigned* outSz)
{
	DWORD sz = 0;
	BYTE* p = CEmuMdxConvertPcmDatToPdx(pcm, pcmSz, &sz);
	if (outSz) *outSz = (unsigned)sz;
	return p;
}

/* Extract PDX filename after title / 0x1A terminator (plain MDX; mirrors mdx_util). */
static int CEmuSessionExtractPdxName(const unsigned char* mdx, unsigned mdxSz, char* out, int outCap)
{
	if (!mdx || !mdxSz || !out || outCap <= 0) return 0;
	out[0] = 0;
	unsigned i = 0;
	unsigned char c = 0;
	while (i < mdxSz) {
		c = mdx[i++];
		if (c == 0x0d || c == 0x0a) break;
		if (c < 0x20 && c != 0x1b && c != 0x09) return 0;
	}
	if (i >= mdxSz) return 0;
	i += (i & 1); /* 2-byte align */
	if (i >= mdxSz) return 0;
	if (c != 0x0d) {
		while (i < mdxSz && mdx[i++] != 0x0d) {}
		if (i >= mdxSz) return 0;
	}
	while (i < mdxSz && mdx[i++] != 0x1a) {}
	if (i >= mdxSz) return 0;
	int j = 0;
	while (i < mdxSz && mdx[i] && j + 1 < outCap)
		out[j++] = (char)mdx[i++];
	out[j] = 0;
	return out[0] ? 1 : 0;
}

static int CEmuSessionIsToneMdxName(const char* base)
{
	return base && _strnicmp(base, "TONE", 4) == 0;
}

static int CEmuSessionIsMdxPackName(const char* base)
{
	if (!base || !base[0]) return 0;
	if (_stricmp(base, "MDALL.DAT") == 0) return 1;
	if (_stricmp(base, "FMALL.DAT") == 0) return 1;
	if (_strnicmp(base, "MSCALL.MD", 9) == 0) return 1;
	if (_stricmp(base, "MSCALL.FMX") == 0) return 1;
	return 0;
}

/* MDD MDALL/MSCALL pack: u16le count, then count×(name[14]+u32le ofs+u32le size). */
static int CEmuSessionExtractMdxPackSong(const unsigned char* pack, unsigned packSz,
	unsigned songIndex1, const unsigned char** out, unsigned* outSz)
{
	if (!pack || packSz < 24 || !out || !outSz) return 0;
	*out = NULL;
	*outSz = 0;
	unsigned count = (unsigned)pack[0] | ((unsigned)pack[1] << 8);
	if (count == 0 || count > 512) return 0;
	if (2u + count * 22u > packSz) return 0;
	unsigned idx = songIndex1 ? songIndex1 - 1u : 0u;
	if (idx >= count) idx = 0;
	const unsigned char* rec = pack + 2 + idx * 22;
	unsigned ofs = (unsigned)rec[14] | ((unsigned)rec[15] << 8)
		| ((unsigned)rec[16] << 16) | ((unsigned)rec[17] << 24);
	unsigned sz = (unsigned)rec[18] | ((unsigned)rec[19] << 8)
		| ((unsigned)rec[20] << 16) | ((unsigned)rec[21] << 24);
	if (sz < 64 || ofs >= packSz || ofs + sz > packSz) return 0;
	*out = pack + ofs;
	*outSz = sz;
	return 1;
}

static int CEmuSessionOpenMdxBytes(CEmuSession* s, const unsigned char* mdx, unsigned mdxSz,
	CEmuZipFs* fs, const wchar_t* path, DWORD sampleRate)
{
	if (!s || !mdx || !mdxSz) return 0;
	const unsigned char* pdx = NULL;
	unsigned pdxSz = 0;
	char pdxName[512];
	pdxName[0] = 0;

	/* MDC: PDX name lives at BE32 offset 0x1c (mdc2mdx), not in a title header. */
	if (mdxSz >= 0x20
		&& mdx[0] == 'M' && mdx[1] == 'D' && mdx[2] == 'C' && mdx[3] == 0x1a) {
		unsigned pdxOfs = ((unsigned)mdx[0x1c] << 24) | ((unsigned)mdx[0x1d] << 16)
			| ((unsigned)mdx[0x1e] << 8) | (unsigned)mdx[0x1f];
		if (pdxOfs > 0 && pdxOfs < mdxSz) {
			int j = 0;
			while (pdxOfs < mdxSz && mdx[pdxOfs] && j + 1 < (int)sizeof(pdxName))
				pdxName[j++] = (char)mdx[pdxOfs++];
			pdxName[j] = 0;
		}
	}
	if (!pdxName[0])
		CEmuSessionExtractPdxName(mdx, mdxSz, pdxName, (int)sizeof(pdxName));
	if (pdxName[0])
		pdx = CEmuSessionFindPdxInZip(fs, pdxName, &pdxSz);

	/* Loose packs: PCMDATA.PDX, shared MDD PCM.DAT bank, or first *.pdx. */
	unsigned char* convertedPdx = NULL;
	if (!pdx) {
		static const char* kFallback[] = {
			"PCMDATA.PDX", "PCM.PDX", "VOICE.PDX", "PCM.DAT", NULL
		};
		for (int i = 0; kFallback[i]; i++) {
			unsigned rawSz = 0;
			const unsigned char* raw = CEmuSessionFindPdxInZip(fs, kFallback[i], &rawSz);
			if (!raw) continue;
			if (_stricmp(kFallback[i], "PCM.DAT") == 0) {
				/* FMX bodies (tone @ 0x140) are FM-only — skip MDD PCM.DAT. */
				unsigned first = ((unsigned)mdx[0] << 24) | ((unsigned)mdx[1] << 16)
					| ((unsigned)mdx[2] << 8) | (unsigned)mdx[3];
				if (first > 0x40) continue;
				convertedPdx = CEmuSessionConvertPcmDatToPdx(raw, rawSz, &pdxSz);
				if (!convertedPdx) continue;
				pdx = convertedPdx;
			} else {
				pdx = raw;
				pdxSz = rawSz;
			}
			if (!pdxName[0])
				strncpy_s(pdxName, kFallback[i], _TRUNCATE);
			break;
		}
		if (!pdx) {
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_UTF8, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				char base[CEMU_ZIP_PATH];
				CEmuSessionZipBaseName(pathA, base, (int)sizeof(base));
				size_t n = strlen(base);
				if (n >= 4 && _stricmp(base + n - 4, ".pdx") == 0) {
					pdx = fs->files[i].data;
					pdxSz = fs->files[i].size;
					if (!pdxName[0])
						strncpy_s(pdxName, base, _TRUNCATE);
					break;
				}
			}
		}
	} else if (pdxName[0] && _stricmp(pdxName, "PCM.DAT") == 0) {
		convertedPdx = CEmuSessionConvertPcmDatToPdx(pdx, pdxSz, &pdxSz);
		if (convertedPdx) pdx = convertedPdx;
	}

	/* Bake name into headerless wrap inside OpenBuffer (after BE32→BE16). */
	int ok = CEmuMdxOpenBuffer(&s->mdx, mdx, mdxSz, pdx, pdxSz, sampleRate, path,
			pdxName[0] ? pdxName : NULL);
	if (!ok && pdx) {
		ok = CEmuMdxOpenBuffer(&s->mdx, mdx, mdxSz, NULL, 0, sampleRate, path, NULL);
	}
	if (convertedPdx) free(convertedPdx);
	if (!ok) return 0;
	s->kind = CEMU_KIND_MDX;
	s->lengthSamples = CEmuMdxLengthSamples(&s->mdx);
	return 1;
}

static int CEmuSessionTryMdxInZip(CEmuSession* s, CEmuZipFs* fs, const wchar_t* path,
	DWORD sampleRate, unsigned titleIdx)
{
	int best = -1;
	int tonePick = -1;
	int fmxPick[64], mdxPick[64];
	int fmxCount = 0, mdxCount = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_UTF8, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		char base[CEMU_ZIP_PATH];
		CEmuSessionZipBaseName(pathA, base, (int)sizeof(base));
		if (CEmuSessionIsMdxPackName(base)) continue;
		if (!CEmuSessionIsMdxName(base)) continue;
		if (_stricmp(base, "MAIN.MDX") == 0 || _stricmp(base, "MAIN.MDC") == 0)
			best = i;
		else if (CEmuSessionIsToneMdxName(base)) {
			if (tonePick < 0) tonePick = i;
		} else {
			size_t n = strlen(base);
			int isFmx = (n >= 4 && _stricmp(base + n - 4, ".fmx") == 0);
			if (isFmx && fmxCount < (int)_countof(fmxPick))
				fmxPick[fmxCount++] = i;
			else if (!isFmx && mdxCount < (int)_countof(mdxPick))
				mdxPick[mdxCount++] = i;
		}
	}

	if (best >= 0) {
		if (CEmuSessionOpenMdxBytes(s, fs->files[best].data, fs->files[best].size, fs, path, sampleRate))
			return 1;
	}
	/* FMX first — 38k/yami MDX bodies are PCM-bank songs; FMX is FM-only. */
	for (int n = 0; n < fmxCount; n++) {
		int i = fmxPick[n];
		if (CEmuSessionOpenMdxBytes(s, fs->files[i].data, fs->files[i].size, fs, path, sampleRate))
			return 1;
	}
	for (int n = 0; n < mdxCount; n++) {
		int i = mdxPick[n];
		if (CEmuSessionOpenMdxBytes(s, fs->files[i].data, fs->files[i].size, fs, path, sampleRate))
			return 1;
	}

	/* MDD packs: prefer FMALL / MSCALL.FMX over MDALL / MSCALL.MD* */
	int packOrder[64];
	int packCount = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_UTF8, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		char base[CEMU_ZIP_PATH];
		CEmuSessionZipBaseName(pathA, base, (int)sizeof(base));
		if (!CEmuSessionIsMdxPackName(base)) continue;
		if (packCount < (int)_countof(packOrder))
			packOrder[packCount++] = i;
	}
	for (int pass = 0; pass < 2; pass++) {
		for (int p = 0; p < packCount; p++) {
			int i = packOrder[p];
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_UTF8, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			char base[CEMU_ZIP_PATH];
			CEmuSessionZipBaseName(pathA, base, (int)sizeof(base));
			int isFm = (_stricmp(base, "FMALL.DAT") == 0 || _stricmp(base, "MSCALL.FMX") == 0);
			if (pass == 0 && !isFm) continue;
			if (pass == 1 && isFm) continue;
			const unsigned char* pack = fs->files[i].data;
			unsigned packSz = fs->files[i].size;
			if (!pack || packSz < 24) continue;
			unsigned count = (unsigned)pack[0] | ((unsigned)pack[1] << 8);
			if (count == 0 || count > 512) continue;
			unsigned start = titleIdx ? titleIdx - 1u : 0u;
			if (start >= count) start = 0;
			for (unsigned n = 0; n < count; n++) {
				unsigned idx = (start + n) % count;
				const unsigned char* song = NULL;
				unsigned songSz = 0;
				if (!CEmuSessionExtractMdxPackSong(pack, packSz, idx + 1, &song, &songSz))
					continue;
				if (CEmuSessionOpenMdxBytes(s, song, songSz, fs, path, sampleRate))
					return 1;
			}
		}
	}

	if (tonePick >= 0) {
		if (CEmuSessionOpenMdxBytes(s, fs->files[tonePick].data, fs->files[tonePick].size, fs, path, sampleRate))
			return 1;
	}
	return 0;
}

static int CEmuSessionTryS98InZip(CEmuSession* s, CEmuZipFs* fs, const wchar_t* path, DWORD sampleRate)
{
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_UTF8, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const char* fn = pathA;
		size_t n = strlen(fn);
		if (n < 4) continue;
		if (_stricmp(fn + n - 4, ".s98") != 0) continue;
		const unsigned char* data = fs->files[i].data;
		unsigned sz = fs->files[i].size;
		if (!data || !sz) continue;
		if (CEmuS98OpenBuffer(&s->s98, data, sz, sampleRate, path)) {
			s->kind = CEMU_KIND_S98;
			s->lengthSamples = CEmuS98LengthSamples(&s->s98);
			return 1;
		}
	}
	return 0;
}

static int CEmuSessionTryHardGe(CEmuSession* s, const CEmuGameEntry* ge,
	const wchar_t* zipPath, unsigned titleCode)
{
	if (!s || !ge || !zipPath) return 0;
	/* Non-MDX X68k ROM zips (BOOT.BIN / Musashi hard) after MDX miss. */
	if (_stricmp(ge->platform, "x68k") == 0 || _stricmp(ge->dataDir, "x68k") == 0) {
		if (CEmuX68kOpen(&s->x68k, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_X68K;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if ((_stricmp(ge->platform, "pc88") == 0 || _stricmp(ge->platform, "pc80sr") == 0)
		&& _stricmp(ge->platform, "pc88va") != 0
		&& _stricmp(ge->platform, "pc88vados") != 0) {
		if (CEmuPc88Open(&s->pc88, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_PC88;
			s->lengthSamples = 0;
			return 1;
		}
	}
	if (_strnicmp(ge->platform, "pc98", 4) == 0 || _stricmp(ge->platform, "pc98dos") == 0
		|| _stricmp(ge->platform, "pc9821") == 0 || _stricmp(ge->platform, "pc98vx") == 0
		|| _stricmp(ge->platform, "pc88va") == 0
		|| _stricmp(ge->platform, "pc88vados") == 0) {
		if (CEmuPc98Open(&s->pc98, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_PC98;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if (_stricmp(ge->subtype, "sg1000") == 0 || _stricmp(ge->dataDir, "sc3000") == 0
		|| _stricmp(ge->platform, "sg1000") == 0
		|| _strnicmp(ge->archive, "sc_", 3) == 0) {
		if (CEmuSg1000Open(&s->sg1000, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_SG1000;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if (_stricmp(ge->platform, "x1") == 0 || _stricmp(ge->dataDir, "x1") == 0
		|| _stricmp(ge->subtype, "x1") == 0 || _stricmp(ge->subtype, "x1psg") == 0) {
		if (CEmuX1Open(&s->x1, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_X1;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if (_stricmp(ge->platform, "fm7") == 0 || _stricmp(ge->platform, "fm77av") == 0
		|| _stricmp(ge->dataDir, "fm7") == 0) {
		if (CEmuFm7Open(&s->fm7, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_FM7;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if ((_stricmp(ge->platform, "pcatdos") == 0 || _stricmp(ge->platform, "pcat") == 0)
		&& (_stricmp(ge->subtype, "adlib") == 0 || _stricmp(ge->subtype, "opl2") == 0
			|| _stricmp(ge->subtype, "opl") == 0 || _stricmp(ge->subtype, "opl3") == 0
			|| _stricmp(ge->subtype, "soundblaster16") == 0
			|| _stricmp(ge->subtype, "sb16") == 0
			|| _stricmp(ge->subtype, "soundblaster") == 0
			|| _stricmp(ge->subtype, "sbpro") == 0
			|| _stricmp(ge->subtype, "sb") == 0
			|| _stricmp(ge->subtype, "gameblaster") == 0
			|| _stricmp(ge->subtype, "cms") == 0
			|| _stricmp(ge->subtype, "beep") == 0
			|| _stricmp(ge->subtype, "midiout") == 0)) {
		/* midiout uses MPU-401 UART capture → SMF for KPI/VST when possible. */
		if (CEmuPcatOpen(&s->pcat, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_PCAT;
			if (s->pcat.sampleRate > 0)
				s->sampleRate = s->pcat.sampleRate;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if (_stricmp(ge->subtype, "f3system") == 0) {
		if (CEmuF3Open(&s->f3, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_F3;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	/* MSX/KSS — must run before dataDir=ac catch-all (ac\ に置かれた MSX zip). */
	if (_stricmp(ge->platform, "msx") == 0 || _stricmp(ge->dataDir, "msx") == 0
		|| _stricmp(ge->subtype, "kss") == 0 || _stricmp(ge->subtype, "opll") == 0) {
		if (CEmuMsxOpen(&s->msx, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_MSX;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	if (_strnicmp(ge->platform, "capcom", 6) == 0 || _stricmp(ge->platform, "sega") == 0
		|| _stricmp(ge->platform, "videosystem") == 0
		|| _stricmp(ge->platform, "namco") == 0
		|| _strnicmp(ge->platform, "konami", 6) == 0
		|| _stricmp(ge->platform, "taito") == 0
		|| _stricmp(ge->platform, "irem") == 0
		|| _stricmp(ge->platform, "dataeast") == 0
		|| _stricmp(ge->platform, "neogeo") == 0
		|| _stricmp(ge->platform, "jaleco") == 0
		|| _stricmp(ge->platform, "technos") == 0
		|| _stricmp(ge->platform, "toaplan") == 0
		|| _stricmp(ge->platform, "snk") == 0
		|| _stricmp(ge->platform, "nichibutsu") == 0
		|| _stricmp(ge->platform, "seibu") == 0
		|| _stricmp(ge->platform, "tecmo") == 0
		|| _stricmp(ge->platform, "banpresto") == 0
		|| _stricmp(ge->platform, "cave") == 0
		|| _stricmp(ge->platform, "psikyo") == 0
		|| _stricmp(ge->platform, "nmk") == 0
		|| _stricmp(ge->dataDir, "ac") == 0
		|| _strnicmp(ge->subtype, "cps", 3) == 0
		|| _strnicmp(ge->subtype, "system16", 8) == 0
		|| _strnicmp(ge->subtype, "system18", 8) == 0
		|| _strnicmp(ge->subtype, "system24", 8) == 0
		|| _strnicmp(ge->subtype, "system32", 8) == 0
		|| _stricmp(ge->subtype, "systemgx") == 0
		|| _stricmp(ge->subtype, "m72") == 0 || _stricmp(ge->subtype, "m92") == 0
		|| _stricmp(ge->subtype, "rtype") == 0 || _stricmp(ge->subtype, "rtype2") == 0
		|| _stricmp(ge->subtype, "zn") == 0
		|| _stricmp(ge->subtype, "outrun") == 0
		|| _stricmp(ge->subtype, "aburner") == 0
		|| _stricmp(ge->subtype, "sharrier") == 0
		|| _stricmp(ge->subtype, "hangon") == 0
		|| _stricmp(ge->subtype, "toutrun") == 0
		|| _stricmp(ge->subtype, "aerofgt") == 0) {
		if (CEmuAcOpen(&s->ac, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_AC;
			s->lengthSamples = 0;
			return 1;
		}
		return 0;
	}
	return 0;
}

int CEmuSessionOpen(CEmuSession* s, const wchar_t* path, unsigned titleCode, DWORD sampleRate)
{
	if (!s || !path) return 0;
	CEmuSessionClose(s);
	wchar_t physical[CEMU_ZIP_PATH];
	unsigned titleIdx = 1;
	CEmuParseVirtualPath(path, physical, (int)_countof(physical), &titleIdx);
	const wchar_t* openPath = physical[0] ? physical : path;
	wcsncpy_s(s->path, openPath, _TRUNCATE);
	s->sampleRate = sampleRate ? sampleRate : 44100;

	wchar_t zipOut[CEMU_ZIP_PATH];
	char dataDir[CEMU_DATA_DIR];
	const CEmuGameEntry* ge = CEmuMgrResolveZip(CEmuMgrGet(), openPath, zipOut, (int)_countof(zipOut), dataDir, (int)sizeof(dataDir));
	const wchar_t* zipPath = zipOut[0] ? zipOut : openPath;
	s->game = ge;

	CEmuZipFs fs;
	memset(&fs, 0, sizeof(fs));
	if (!CEmuZipFsOpen(&fs, zipPath)) return 0;

	if (CEmuSessionTryS98InZip(s, &fs, zipPath, s->sampleRate)) {
		CEmuZipFsClose(&fs);
		return 1;
	}
	if (CEmuSessionTryMdxInZip(s, &fs, zipPath, s->sampleRate, titleIdx)) {
		CEmuZipFsClose(&fs);
		return 1;
	}

	/* Same archive name may have multiple xml rows — try ranked until one opens.
	   When the user picked a CEmu sound mode (preferTag), do NOT fall through to
	   a sibling subtype (sorc_at AdLib absorbing GameBlaster/Beep/MIDI). */
	const CEmuGameEntry* cands[32];
	int nc = 0;
	char stem[CEMU_ARCHIVE_NAME] = {};
	if (CEmuArchiveStemFromPath(zipPath, stem, (int)sizeof(stem)))
		nc = CEmuCatalogCollectArchiveForZip(&CEmuMgrGet()->catalog, stem, &fs, cands, (int)_countof(cands));
	if (nc <= 0 && ge) {
		cands[0] = ge;
		nc = 1;
	} else if (ge && nc > 0) {
		int has = 0;
		for (int i = 0; i < nc; i++) {
			if (cands[i] == ge) { has = 1; break; }
		}
		if (!has && nc < (int)_countof(cands)) {
			for (int i = nc; i > 0; i--)
				cands[i] = cands[i - 1];
			cands[0] = ge;
			nc++;
		} else if (has && cands[0] != ge) {
			for (int i = 0; i < nc; i++) {
				if (cands[i] != ge) continue;
				const CEmuGameEntry* t = cands[0];
				cands[0] = ge;
				cands[i] = t;
				break;
			}
		}
	}

	char preferTag[CEMU_MODE_TAG] = {};
	CEmuModePrefGet(zipPath, preferTag, (int)sizeof(preferTag));
	if (preferTag[0] && nc > 0) {
		const CEmuGameEntry* filtered[32];
		int nf = 0;
		for (int i = 0; i < nc && nf < (int)_countof(filtered); i++) {
			char tag[CEMU_MODE_TAG];
			if (!cands[i] || !CEmuModeTagFromEntry(cands[i], tag, (int)sizeof(tag)))
				continue;
			if (_stricmp(tag, preferTag) != 0) continue;
			filtered[nf++] = cands[i];
		}
		if (nf > 0) {
			for (int i = 0; i < nf; i++)
				cands[i] = filtered[i];
			nc = nf;
		}
	}

	for (int i = 0; i < nc; i++) {
		if (!CEmuSessionTryHardGe(s, cands[i], zipPath, titleCode))
			continue;
		s->game = cands[i];
		CEmuZipFsClose(&fs);
		return 1;
	}

	CEmuZipFsClose(&fs);
	return 0;
}

/* hard エミュ (PC88/98/AC/X68k): カタログに長さが無い。
   一度鳴ったあと連続無音が続けば非ループ曲として終了。永久ループ曲は鳴り続けるので止まらない。 */
static void CEmuSessionWatchHardSilence(CEmuSession* s, short* stereo, int frames)
{
	if (!s || !stereo || frames <= 0) return;
	if (s->kind != CEMU_KIND_PC88 && s->kind != CEMU_KIND_PC98
		&& s->kind != CEMU_KIND_AC && s->kind != CEMU_KIND_X68K
		&& s->kind != CEMU_KIND_SG1000 && s->kind != CEMU_KIND_X1
		&& s->kind != CEMU_KIND_PCAT && s->kind != CEMU_KIND_F3
		&& s->kind != CEMU_KIND_MSX && s->kind != CEMU_KIND_FM7)
		return;
	if (s->lengthSamples > 0 || s->endedBySilence)
		return;

	const int rate = s->sampleRate > 0 ? s->sampleRate : 44100;
	/* 起動・曲頭の無音を誤判定しない。鳴った後 5 秒無音で終了。
	   一度も鳴らない壊れたタイトルは 25 秒で打ち切り。 */
	const uint32_t settleNeed = (uint32_t)rate * 4u;
	const uint32_t silenceNeed = (uint32_t)rate * 5u;
	const uint32_t neverHeardNeed = (uint32_t)rate * 25u;
	const int thresh = 80; /* ~ -52 dBFS。ノイズ床は無視 */

	s->silenceFrames += (uint32_t)frames;

	int peak = 0;
	const int n = frames * 2;
	for (int i = 0; i < n; i++) {
		int v = (int)stereo[i];
		if (v < 0) v = -v;
		if (v > peak) peak = v;
	}

	if (peak >= thresh) {
		s->silenceHeard = 1;
		s->silenceRun = 0;
		return;
	}

	if (!s->silenceHeard) {
		if (s->silenceFrames >= neverHeardNeed) {
			s->endedBySilence = 1;
			s->lengthSamples = s->curSample + (UINT64)frames;
			if (s->lengthSamples == 0)
				s->lengthSamples = 1;
		}
		return;
	}

	if (s->silenceFrames < settleNeed)
		return;

	s->silenceRun += (uint32_t)frames;
	if (s->silenceRun >= silenceNeed) {
		s->endedBySilence = 1;
		s->lengthSamples = s->curSample + (UINT64)frames;
		if (s->lengthSamples == 0)
			s->lengthSamples = 1;
		/* PC/AT: drivers often leave KEY ON; mute so notes do not hang. */
		if (s->kind == CEMU_KIND_PCAT && s->pcat.hard
			&& s->pcat.hard->hardKind == CHard::KIND_PCAT)
			((CHardPcat*)s->pcat.hard)->MuteAllSound();
	}
}

int CEmuSessionRender(CEmuSession* s, short* stereo, int frames)
{
	if (!s || !stereo || frames <= 0) return 0;
	int got = 0;
	switch (s->kind) {
	case CEMU_KIND_S98: got = CEmuS98Render(&s->s98, stereo, frames); break;
	case CEMU_KIND_MDX: got = CEmuMdxRender(&s->mdx, stereo, frames); break;
	case CEMU_KIND_PC88: got = CEmuPc88Render(&s->pc88, stereo, frames); break;
	case CEMU_KIND_PC98: got = CEmuPc98Render(&s->pc98, stereo, frames); break;
	case CEMU_KIND_AC: got = CEmuAcRender(&s->ac, stereo, frames); break;
	case CEMU_KIND_X68K: got = CEmuX68kRender(&s->x68k, stereo, frames); break;
	case CEMU_KIND_SG1000: got = CEmuSg1000Render(&s->sg1000, stereo, frames); break;
	case CEMU_KIND_X1: got = CEmuX1Render(&s->x1, stereo, frames); break;
	case CEMU_KIND_PCAT: got = CEmuPcatRender(&s->pcat, stereo, frames); break;
	case CEMU_KIND_F3: got = CEmuF3Render(&s->f3, stereo, frames); break;
	case CEMU_KIND_MSX: got = CEmuMsxRender(&s->msx, stereo, frames); break;
	case CEMU_KIND_FM7: got = CEmuFm7Render(&s->fm7, stereo, frames); break;
	default: return 0;
	}
	if (got > 0)
		CEmuSessionWatchHardSilence(s, stereo, got);
	return got;
}

int CEmuSessionSeek(CEmuSession* s, UINT64 sample)
{
	if (!s) return 0;
	s->curSample = sample;
	/* ループ先頭へ戻すときは無音ウォッチをリセット（再トリガは各 Seek 実装任せ） */
	if (sample == 0 && !s->endedBySilence) {
		s->silenceFrames = 0;
		s->silenceRun = 0;
		s->silenceHeard = 0;
	}
	switch (s->kind) {
	case CEMU_KIND_S98: return CEmuS98Seek(&s->s98, sample, 0);
	case CEMU_KIND_MDX: return CEmuMdxSeek(&s->mdx, sample, 0);
	case CEMU_KIND_PC88: return CEmuPc88Seek(&s->pc88, sample);
	case CEMU_KIND_PC98: return CEmuPc98Seek(&s->pc98, sample);
	case CEMU_KIND_AC: return CEmuAcSeek(&s->ac, sample);
	case CEMU_KIND_X68K: return CEmuX68kSeek(&s->x68k, sample);
	case CEMU_KIND_SG1000: return CEmuSg1000Seek(&s->sg1000, sample);
	case CEMU_KIND_X1: return CEmuX1Seek(&s->x1, sample);
	case CEMU_KIND_PCAT: return CEmuPcatSeek(&s->pcat, sample);
	case CEMU_KIND_F3: return CEmuF3Seek(&s->f3, sample);
	case CEMU_KIND_MSX: return CEmuMsxSeek(&s->msx, sample);
	case CEMU_KIND_FM7: return CEmuFm7Seek(&s->fm7, sample);
	default: return 0;
	}
}
