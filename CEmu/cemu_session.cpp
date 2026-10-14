#include "StdAfx.h"
#include "cemu_session.h"
#include "cemu_mgr.h"
#include "cemu_catalog.h"
#include "cemu_zipfs.h"
#include "cemu_mdx.h"
#include "pmd/cemu_pmd.h"
#include "cemu_modepref.h"
#include "driver/cemu_driver.h"
#include "machine/cemu_hard_pcat.h"
#include "machine/cemu_hard_pc98.h"
#include "machine/cemu_hard_ac.h"
#include "machine/cemu_hard_x1.h"
#include "machine/cemu_hard_msx.h"
#include "fmmon/fmmon_shadow.h"
#include "fmmon/cemu_fmmon_bind.h"
#include "chip/cemu_chip_opna.h"
#include "fmmon/kbs_gimic.h"

void CEmuGimicStop(void);
static int s_gimicPushed;
static int s_gimicNeedBegin;
static uint64_t s_adpcmGuardUntil;
static int s_adpcmLoading;
static int s_adpcmHold;
static int s_adpcmRelease;
static unsigned s_adpcmBytes;
static int s_paceOn;
static LARGE_INTEGER s_paceT0;
static int64_t s_paceOrigin;
enum { CEMU_GIMIC_CAP = 16384 };
static uint16_t s_capA[CEMU_GIMIC_CAP];
static uint8_t s_capD[CEMU_GIMIC_CAP];
static unsigned s_capS[CEMU_GIMIC_CAP];
static int s_capN;
static int s_capR;
extern unsigned CEmuPc88LeadMark(void);
extern unsigned CEmuPc98LeadMark(void);
#include <string.h>
#include <stdlib.h>

/* セッションをゼロ初期化。既定はステレオ */
void CEmuSessionInit(CEmuSession* s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->channels = 2;
}

/* 全 kind のプレーヤを閉じ、セッションを空に戻す */
void CEmuSessionClose(CEmuSession* s)
{
	if (!s) return;
	CEmuS98Close(&s->s98);
	CEmuMdxClose(&s->mdx);
	CEmuPmdClose(&s->pmd);
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
	CEmuPicoClose(&s->pico);
	CEmuGimicStop();
	memset(s, 0, sizeof(*s));
	s->channels = 2;
}

/* MDX 系拡張子 (.mdx/.mdc/.fmx) か */
static int CEmuSessionIsMdxName(const char* fn)
{
	size_t n = fn ? strlen(fn) : 0;
	if (n < 4) return 0;
	if (_stricmp(fn + n - 4, ".mdx") == 0) return 1;
	if (_stricmp(fn + n - 4, ".mdc") == 0) return 1;
	if (_stricmp(fn + n - 4, ".fmx") == 0) return 1;
	return 0;
}

/* zip 内パスからベース名だけ取る */
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

/* 指定 PDX 名を zip から探す。拡張子無しなら .pdx を付ける */
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

/* MDD 共用 PCM.DAT: 64×(BE32 start, BE32 end) + ADPCM 本体 @512。
   MXDRV PDX (96× ptr+len) に変換して portable_mdx がバンクを掴めるようにする。 */
static unsigned char* CEmuSessionConvertPcmDatToPdx(const unsigned char* pcm, unsigned pcmSz,
	unsigned* outSz)
{
	DWORD sz = 0;
	BYTE* p = CEmuMdxConvertPcmDatToPdx(pcm, pcmSz, &sz);
	if (outSz) *outSz = (unsigned)sz;
	return p;
}

/* タイトル / 0x1A 終端のあとから PDX ファイル名を取る（素の MDX。mdx_util と同じ） */
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
	i += (i & 1); /* 2 バイトアライン */
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

/* TONE*.MDX は音色バンク。曲より後回し */
static int CEmuSessionIsToneMdxName(const char* base)
{
	return base && _strnicmp(base, "TONE", 4) == 0;
}

/* MDD パック本体 (MDALL/FMALL/MSCALL) か */
static int CEmuSessionIsMdxPackName(const char* base)
{
	if (!base || !base[0]) return 0;
	if (_stricmp(base, "MDALL.DAT") == 0) return 1;
	if (_stricmp(base, "FMALL.DAT") == 0) return 1;
	if (_strnicmp(base, "MSCALL.MD", 9) == 0) return 1;
	if (_stricmp(base, "MSCALL.FMX") == 0) return 1;
	return 0;
}

/* MDD MDALL/MSCALL パック: u16le 曲数、続けて count×(name[14]+u32le ofs+u32le size) */
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

/* MDX バイト列を開き、PDX を zip から結び付ける */
static int CEmuSessionOpenMdxBytes(CEmuSession* s, const unsigned char* mdx, unsigned mdxSz,
	CEmuZipFs* fs, const wchar_t* path, DWORD sampleRate)
{
	if (!s || !mdx || !mdxSz) return 0;
	const unsigned char* pdx = NULL;
	unsigned pdxSz = 0;
	char pdxName[512];
	pdxName[0] = 0;

	/* MDC: PDX 名は BE32 オフセット 0x1c (mdc2mdx)。タイトルヘッダには無い */
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

	/* ばら置き: PCMDATA.PDX、共用 MDD PCM.DAT、または先頭の *.pdx */
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
				/* FMX 本体 (音色 @ 0x140) は FM のみ — MDD PCM.DAT を付けない */
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

	/* OpenBuffer 内のヘッダ無しラップへ名前を焼く（BE32→BE16 のあと） */
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

/* zip 内の MDX/FMX/MDC/パックから曲を選んで開く */
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
	/* FMX 優先 — 38k/yami の MDX 本体は PCM バンク曲。FMX は FM のみ */
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

	/* MDD パック: FMALL / MSCALL.FMX を MDALL / MSCALL.MD* より先に */
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

/* zip 内の .s98 を順に試す */
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

/* カタログ行から hard / PMD を開く。platform/subtype で kind を決める */
static int CEmuSessionTryHardGe(CEmuSession* s, const CEmuGameEntry* ge,
	const wchar_t* zipPath, unsigned titleCode, CEmuZipFs* fs)
{
	if (!s || !ge || !zipPath) return 0;
	if (CEmuPmdOpen(&s->pmd, ge, zipPath, titleCode, s->sampleRate, fs)) {
		s->kind = CEMU_KIND_PMD;
		if (s->pmd.sampleRate > 0)
			s->sampleRate = s->pmd.sampleRate;
		s->lengthSamples = CEmuPmdLengthSamples(&s->pmd);
		return 1;
	}
	/* MDX が無い X68k ROM zip (BOOT.BIN / Musashi hard) は MDX 失敗後 */
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
	if (_stricmp(ge->subtype, "pico") == 0 || _stricmp(ge->dataDir, "pico") == 0) {
		if (CEmuPicoOpen(&s->pico, ge, zipPath, titleCode, s->sampleRate)) {
			s->kind = CEMU_KIND_PICO;
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
			|| _stricmp(ge->subtype, "tandy") == 0
			|| _stricmp(ge->subtype, "midiout") == 0)) {
		/* midiout は可能なら MPU-401 UART 捕捉 → SMF を KPI/VST へ */
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
	/* MSX/KSS — dataDir=ac の catch-all より先 (ac\ に置かれた MSX zip)。 */
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

static void CEmuSessionApplyTogglePrefs(CEmuSession* s)
{
	if (!s || !s->path[0] || !s->game) return;
	CEmuArchiveToggle list[CEMU_TOGGLE_MAX];
	const int n = CEmuCatalogListArchiveToggles(s->game, list, CEMU_TOGGLE_MAX);
	for (int i = 0; i < n; i++) {
		const int on = CEmuTogglePrefHas(s->path, list[i].code);
		if (s->kind == CEMU_KIND_X1 && s->x1.hard)
			((CHardX1*)s->x1.hard)->ApplyCatalogToggle(list[i].code, on);
		else if (s->kind == CEMU_KIND_MSX && s->msx.hard)
			((CHardMsx*)s->msx.hard)->ApplyCatalogToggle(list[i].code, on);
	}
}

int CEmuSessionKindKeepsEngine(int kind)
{
	return (kind != 0
		&& kind != CEMU_KIND_S98
		&& kind != CEMU_KIND_MDX
		&& kind != CEMU_KIND_PMD) ? 1 : 0;
}

int CEmuSessionSameZip(const CEmuSession* s, const wchar_t* path)
{
	if (!s || !s->path[0] || !path || !path[0])
		return 0;
	wchar_t physical[CEMU_ZIP_PATH];
	unsigned titleIdx = 1;
	CEmuParseVirtualPath(path, physical, (int)_countof(physical), &titleIdx);
	const wchar_t* openPath = physical[0] ? physical : path;
	return (_wcsicmp(s->path, openPath) == 0) ? 1 : 0;
}

static unsigned CEmuSessionFirstPlayableTitle(const CEmuGameEntry* ge)
{
	if (!ge || ge->titleCount <= 0 || !ge->title)
		return 0;
	for (int i = 0; i < ge->titleCount; i++) {
		wchar_t lab[CEMU_GAME_NAME];
		lab[0] = 0;
		CEmuGameTitleAt(ge, i, NULL, lab, (int)_countof(lab));
		if (CEmuTitleLooksLikeToggle(lab) || CEmuTitleLooksLikeSfx(lab))
			continue;
		int isStop = 0;
		for (const wchar_t* p = lab; *p; p++) {
			wchar_t c0 = p[0], c1 = p[1], c2 = p[2], c3 = p[3];
			if (c0 >= L'a' && c0 <= L'z') c0 = (wchar_t)(c0 - L'a' + L'A');
			if (c1 >= L'a' && c1 <= L'z') c1 = (wchar_t)(c1 - L'a' + L'A');
			if (c2 >= L'a' && c2 <= L'z') c2 = (wchar_t)(c2 - L'a' + L'A');
			if (c3 >= L'a' && c3 <= L'z') c3 = (wchar_t)(c3 - L'a' + L'A');
			if (c0 == L'S' && c1 == L'T' && c2 == L'O' && c3 == L'P') {
				isStop = 1;
				break;
			}
		}
		if (isStop)
			continue;
		return ge->title[i].code;
	}
	return CEmuGameTitleCodeForIndex(ge, 1);
}

/* zip / 仮想パスを開き、S98→MDX→hard の順で kind を決める */
int CEmuSessionOpen(CEmuSession* s, const wchar_t* path, unsigned titleCode, DWORD sampleRate)
{
	if (!s || !path) return 0;
	wchar_t physical[CEMU_ZIP_PATH];
	unsigned titleIdx = 1;
	CEmuParseVirtualPath(path, physical, (int)_countof(physical), &titleIdx);
	const wchar_t* openPath = physical[0] ? physical : path;
	const DWORD rate = sampleRate ? sampleRate : 44100;

	const int loadOnly = CEmuDriverIsNextOpenLoadOnly();
	if (loadOnly)
		s->loadOnly = 1;

	/* 同じ zip なら LoadRoms/boot しない。トグル／SE／曲切替は OverlayTitle。
	   ロード専用はブートし直してロゴを出すので Overlay しない。
	   音源モード (GAMEBLASTER vs AdLib 等) が変わったら Overlay せず boot し直す。 */
	char overlayPref[CEMU_MODE_TAG] = {};
	int overlaySameMode = 1;
	if (CEmuModePrefGet(openPath, overlayPref, (int)sizeof(overlayPref)) && overlayPref[0]) {
		char curTag[CEMU_MODE_TAG] = {};
		if (!s->game || !CEmuModeTagFromEntry(s->game, curTag, (int)sizeof(curTag))
			|| !CEmuModeTagsEqual(curTag, overlayPref))
			overlaySameMode = 0;
	}
	if (!loadOnly && overlaySameMode && CEmuSessionKindKeepsEngine(s->kind)
		&& CEmuSessionSameZip(s, openPath)
		&& (s->sampleRate == 0 || s->sampleRate == (int)rate)) {
		const int tog = (s->game && CEmuGameTitleCodeIsToggle(s->game, titleCode)) ? 1 : 0;
		int mix = 0;
		if (tog)
			CEmuTogglePrefFlip(s->path, titleCode);
		else
			mix = CEmuSessionCodeIsOverlay(s, titleCode);
		if (!CEmuSessionOverlayTitle(s, titleCode, mix))
			return 0;
		return 1;
	}

	CEmuSessionClose(s);
	if (loadOnly)
		s->loadOnly = 1;
	wcsncpy_s(s->path, openPath, _TRUNCATE);
	s->sampleRate = sampleRate ? sampleRate : 44100;
	s->titleCode = titleCode;

	wchar_t zipOut[CEMU_ZIP_PATH];
	char dataDir[CEMU_DATA_DIR];
	const CEmuGameEntry* ge = CEmuMgrResolveZip(CEmuMgrGet(), openPath, zipOut, (int)_countof(zipOut), dataDir, (int)sizeof(dataDir));
	const wchar_t* zipPath = zipOut[0] ? zipOut : openPath;
	s->game = ge;
	if (ge && CEmuGameTitleCodeIsToggle(ge, titleCode)) {
		if (!loadOnly && !CEmuTogglePrefHas(zipPath, titleCode))
			CEmuTogglePrefFlip(zipPath, titleCode);
		titleCode = CEmuSessionFirstPlayableTitle(ge);
		s->titleCode = titleCode;
	}

	CEmuZipFs fs;
	memset(&fs, 0, sizeof(fs));
	if (!CEmuZipFsOpen(&fs, zipPath)) return 0;

	if (CEmuSessionTryS98InZip(s, &fs, zipPath, s->sampleRate)) {
		CEmuZipFsClose(&fs);
		return 1;
	}
	if (CEmuSessionTryMdxInZip(s, &fs, zipPath, s->sampleRate, titleIdx)) {
		CEmuZipFsClose(&fs);
		if (s->game)
			CEmuFmMonBindFromGe(s->game);
		return 1;
	}

	/* 同一アーカイブ名に xml 行が複数ある — 順位付けして開くまで試す。
	   ユーザが音源モード (preferTag) を選んだら兄弟 subtype へ落とさない
	   (sorc_at の AdLib が GameBlaster/Beep/MIDI を飲み込むのを防ぐ)。 */
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
			if (!CEmuModeTagsEqual(tag, preferTag)) continue;
			filtered[nf++] = cands[i];
		}
		if (nf > 0) {
			for (int i = 0; i < nf; i++)
				cands[i] = filtered[i];
			nc = nf;
		}
	}

	for (int i = 0; i < nc; i++) {
		if (!CEmuSessionTryHardGe(s, cands[i], zipPath, titleCode, &fs))
			continue;
		s->game = cands[i];
		CEmuZipFsClose(&fs);
		s->togglePrefDelay = (s->sampleRate > 0 ? s->sampleRate : 44100) / 20;
		if (s->togglePrefDelay < 1)
			s->togglePrefDelay = 1;
		return 1;
	}

	CEmuZipFsClose(&fs);
	return 0;
}

/* hard エミュ (PC88/98/AC/X68k): カタログに長さが無い。
   一度鳴ったあと連続無音が続けば非ループ曲として終了。永久ループ曲は鳴り続けるので止まらない。
   Model1 MultiPCM は曲終了後に超低音がキーオンのまま残ることがある → 実曲レベル未満は無音扱い。 */
static void CEmuSessionWatchHardSilence(CEmuSession* s, short* stereo, int frames)
{
	if (!s || !stereo || frames <= 0) return;
	if (s->kind != CEMU_KIND_PC88 && s->kind != CEMU_KIND_PC98
		&& s->kind != CEMU_KIND_AC && s->kind != CEMU_KIND_X68K
		&& s->kind != CEMU_KIND_SG1000 && s->kind != CEMU_KIND_X1
		&& s->kind != CEMU_KIND_PCAT && s->kind != CEMU_KIND_F3
		&& s->kind != CEMU_KIND_MSX && s->kind != CEMU_KIND_FM7
		&& s->kind != CEMU_KIND_PICO)
		return;
	if (s->endedBySilence)
		return;

	const int rate = s->sampleRate > 0 ? s->sampleRate : 44100;
	const int loadOnly = s->loadOnly ? 1 : 0;
	/* 起動・曲頭の無音を誤判定しない。鳴った後 5 秒無音で終了。
	   一度も鳴らない壊れたタイトルは 25 秒で打ち切り。
	   クロスフェードは終端が先に要るので、2 秒無音が続いた時点で
	   残り無音分を length として公開する（曲が戻れば取り消す）。
	   ロード専用はロゴだけ。固定秒数では切らず、鳴ったあと 2 秒無音で止める。
	   一度も鳴らない起動は 20 秒で打ち切り。 */
	const uint32_t settleNeed = loadOnly ? 0u : (uint32_t)rate * 4u;
	const uint32_t silenceNeed = loadOnly ? (uint32_t)rate * 2u : (uint32_t)rate * 5u;
	const uint32_t neverHeardNeed = loadOnly ? (uint32_t)rate * 20u : (uint32_t)rate * 25u;
	const uint32_t xfadeArmNeed = loadOnly ? (uint32_t)rate / 2u : (uint32_t)rate * 2u;
	const int heardThresh = 500; /* 実曲。超低音ハミングはこれ未満 */
	const int noiseFloor = 80;

	s->silenceFrames += (uint32_t)frames;

	int peak = 0;
	if (KbsGimicDevOk()) {
		/* 実チップ再生は PCM が直流ダミーなので、キーオン／F-num の動きで終端を見る。
		   ここを無条件に「鳴っている」にすると、曲が終わっても次の同 zip へ進まない。 */
		CChip* chip = NULL;
		if (s->kind == CEMU_KIND_PC88 && s->pc88.hard)
			chip = s->pc88.hard->SoundChip();
		else if (s->kind == CEMU_KIND_PC98 && s->pc98.hard)
			chip = s->pc98.hard->SoundChip();
		unsigned w = 0, k = 0, f = 0, ss = 0, m = 0;
		if (chip)
			CEmuChipYm2608GetPlayMetrics(chip, &w, &k, &f, &ss, &m);
		const unsigned motion = k + f + ss;
		static CEmuSession* s_wdS;
		static unsigned s_wdMotion;
		if (s_wdS != s) {
			s_wdS = s;
			s_wdMotion = motion;
		}
		if (motion != s_wdMotion)
			peak = 1000;
		s_wdMotion = motion;
	} else {
		const int n = frames * 2;
		for (int i = 0; i < n; i++) {
			int v = (int)stereo[i];
			if (v < 0) v = -v;
			if (v > peak) peak = v;
		}
	}

	if (peak >= heardThresh) {
		s->silenceHeard = 1;
		s->silenceRun = 0;
		if (s->lengthSamples > 0)
			s->lengthSamples = 0;
		return;
	}

	if (!s->silenceHeard) {
		if (peak >= noiseFloor) {
			/* 起動ノイズ程度 — 実曲のアタックには数えない */
		}
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

	/* 実曲が鳴ったあと: ほぼ無音、または超低音ハミングが張り付いた */
	s->silenceRun += (uint32_t)frames;
	if (s->lengthSamples == 0 && s->silenceRun >= xfadeArmNeed) {
		uint32_t remain = (silenceNeed > s->silenceRun)
			? (silenceNeed - s->silenceRun) : (uint32_t)(rate / 2);
		if (remain < (uint32_t)rate / 2)
			remain = (uint32_t)rate / 2;
		s->lengthSamples = s->curSample + (UINT64)remain;
	}
	if (s->silenceRun >= silenceNeed) {
		s->endedBySilence = 1;
		s->lengthSamples = s->curSample + (UINT64)frames;
		if (s->lengthSamples == 0)
			s->lengthSamples = 1;
		/* PC/AT: ドライバが KEY ON のまま残しがち。ミュートして音を切る */
		if (s->kind == CEMU_KIND_PCAT && s->pcat.hard
			&& s->pcat.hard->hardKind == CHard::KIND_PCAT)
			((CHardPcat*)s->pcat.hard)->MuteAllSound();
		/* Model1 MultiPCM: Stop を送りチップをリセットして超低音を止める */
		if (s->kind == CEMU_KIND_AC && s->ac.hard
			&& s->ac.hard->hardKind == CHard::KIND_AC) {
			CHardAc* ac = (CHardAc*)s->ac.hard;
			if (ac->SegaM1Audio()) {
				ac->SegaMidiInjectSong(0x1000);
				if (ac->PcmChip()) ac->PcmChip()->Reset();
				if (ac->Oki(1)) ac->Oki(1)->Reset();
			}
		}
	}
}

static CDriver* CEmuSessionDriver(CEmuSession* s);

/* kind に応じて描画。overlayPend があれば同一インスタンスへ OverlayTitle */
int CEmuSessionRender(CEmuSession* s, short* stereo, int frames)
{
	if (!s || !stereo || frames <= 0) return 0;
	if (InterlockedExchange((LONG*)&s->overlayPend, 0)) {
		s->loadOnly = 0;
		const int tog = (s->game && CEmuGameTitleCodeIsToggle(s->game, s->overlayCode)) ? 1 : 0;
		CDriver* drv = CEmuSessionDriver(s);
		if (drv) {
			drv->loadOnly = 0;
			drv->overlayMix = s->overlayMix ? 1 : 0;
			if (!tog && !s->overlayMix) {
				/* 前曲のキーを残したまま次曲の先読みを積むと、実チップが固まり次が始まらない。 */
				KbsGimicDropPending();
				KbsGimicEnd();
				s_gimicNeedBegin = 1;
				s_gimicPushed = 0;
				s_adpcmHold = 0;
				s_adpcmGuardUntil = 0;
				s_adpcmRelease = 0;
				s_capN = 0;
				s_capR = 0;
				s_paceOn = 0;
			}
			drv->OverlayTitle(s->overlayCode);
			drv->overlayMix = 0;
			if (!tog && !s->overlayMix && s->overlayCode)
				s->titleCode = s->overlayCode;
			if (!tog) {
				s->endedBySilence = 0;
				s->silenceHeard = 0;
				s->silenceRun = 0;
				s->silenceFrames = 0;
				s->lengthSamples = 0;
				s->curSample = 0;
			}
			if (tog) {
				/* X1 は PATCH XOR。即 SET すると XOR と打ち消し合う。
				   MSX TO BOSS は RAM/IO フラグなので pref を今載せる。 */
				if (s->kind == CEMU_KIND_MSX)
					CEmuSessionApplyTogglePrefs(s);
			} else if (s->overlayMix) {
				CEmuSessionApplyTogglePrefs(s);
			} else {
				s->togglePrefDelay = (s->sampleRate > 0 ? s->sampleRate : 44100) / 20;
				if (s->togglePrefDelay < 1)
					s->togglePrefDelay = 1;
			}
		}
	}
	if (s->togglePrefDelay > 0) {
		s->togglePrefDelay -= frames;
		if (s->togglePrefDelay <= 0) {
			s->togglePrefDelay = 0;
			CEmuSessionApplyTogglePrefs(s);
		}
	}
	int got = 0;
	switch (s->kind) {
	case CEMU_KIND_S98: got = CEmuS98Render(&s->s98, stereo, frames); break;
	case CEMU_KIND_MDX: got = CEmuMdxRender(&s->mdx, stereo, frames); break;
	case CEMU_KIND_PMD: got = CEmuPmdRender(&s->pmd, stereo, frames); break;
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
	case CEMU_KIND_PICO: got = CEmuPicoRender(&s->pico, stereo, frames); break;
	default: return 0;
	}
	if (got > 0 && !s->seekRender)
		CEmuSessionWatchHardSilence(s, stereo, got);
	return got;
}

/* hard 系のドライバ。S98/MDX/PMD は NULL */
static CDriver* CEmuSessionDriver(CEmuSession* s)
{
	if (!s) return NULL;
	switch (s->kind) {
	case CEMU_KIND_PC88: return s->pc88.driver;
	case CEMU_KIND_PC98: return s->pc98.driver;
	case CEMU_KIND_AC: return s->ac.driver;
	case CEMU_KIND_X68K: return s->x68k.driver;
	case CEMU_KIND_SG1000: return s->sg1000.driver;
	case CEMU_KIND_X1: return s->x1.driver;
	case CEMU_KIND_PCAT: return s->pcat.driver;
	case CEMU_KIND_F3: return s->f3.driver;
	case CEMU_KIND_MSX: return s->msx.driver;
	case CEMU_KIND_FM7: return s->fm7.driver;
	case CEMU_KIND_PICO: return s->pico.driver;
	default: return NULL;
	}
}

/* PC-88/98/X68/AC 等: Seek が空だったのでスライダーで 26 秒へ進めても
   エミュは頭のまま、FM モニタの seq/レジスタが歯抜けになっていた。
   巻き戻しは OverlayTitle で曲を撃ち直し、目標までダミー Render する。 */
static int CEmuSessionSeekHard(CEmuSession* s, UINT64 sample)
{
	if (!s || s->kind == 0)
		return 0;
	if (sample < s->curSample) {
		CDriver* drv = CEmuSessionDriver(s);
		const unsigned tc = s->titleCode ? s->titleCode : s->overlayCode;
		if (drv)
			drv->OverlayTitle(tc);
		s->curSample = 0;
		s->silenceFrames = 0;
		s->silenceRun = 0;
		s->silenceHeard = 0;
		s->endedBySilence = 0;
	}
	if (sample <= s->curSample) {
		FmMonShadowSetCurSample(s->curSample);
		FmMonShadowFlush(1);
		return 1;
	}
	short junk[4096 * 2];
	s->seekRender = 1;
	CEmuGimicSuppress(1);
	while (s->curSample < sample) {
		UINT64 left = sample - s->curSample;
		int want = (left > 4096ull) ? 4096 : (int)left;
		int got = CEmuSessionRender(s, junk, want);
		if (got <= 0)
			break;
		s->curSample += (UINT64)got;
		FmMonShadowSetCurSample(s->curSample);
		FmMonShadowFlush(0);
	}
	s->seekRender = 0;
	CEmuGimicSuppress(0);
	/* 実チップの ADPCM RAM はシークで消えない。読み直しはしない。 */
	s_gimicPushed = 0;
	s_adpcmGuardUntil = 0;
	s_adpcmRelease = 0;
	KbsGimicSetQueueWait(40);
	FmMonShadowFlush(1);
	return 1;
}

/* S98/MDX はネイティブ Seek。hard はダミー Render。時計をシャドウへ渡す */
int CEmuSessionSeek(CEmuSession* s, UINT64 sample)
{
	if (!s) return 0;
	switch (s->kind) {
	case CEMU_KIND_S98:
		s->curSample = sample;
		if (!CEmuS98Seek(&s->s98, sample, 0))
			return 0;
		FmMonShadowSetCurSample(sample);
		FmMonShadowFlush(1);
		return 1;
	case CEMU_KIND_MDX:
		s->curSample = sample;
		if (!CEmuMdxSeek(&s->mdx, sample, 0))
			return 0;
		FmMonShadowSetCurSample(sample);
		FmMonShadowFlush(1);
		return 1;
	case CEMU_KIND_PMD:
		s->curSample = sample;
		return CEmuPmdSeek(&s->pmd, sample, 0);
	case CEMU_KIND_PC88:
	case CEMU_KIND_PC98:
	case CEMU_KIND_AC:
	case CEMU_KIND_X68K:
	case CEMU_KIND_SG1000:
	case CEMU_KIND_X1:
	case CEMU_KIND_PCAT:
	case CEMU_KIND_F3:
	case CEMU_KIND_MSX:
	case CEMU_KIND_FM7:
	case CEMU_KIND_PICO:
		return CEmuSessionSeekHard(s, sample);
	default:
		s->curSample = sample;
		return 0;
	}
}

/* クロスフェード可否。NP2 はプロセスグローバルだが RAM/CPU は hard ごと */
int CEmuSessionUsesGlobalNp2(const CEmuSession* s)
{
	if (!s) return 0;
	/* NP2 コアはプロセス共通のまま。hard ごとに RAM/CPU スナップショットを
	   持つので、PC-98/PC-AT 同士のクロスフェードが可能。 */
	return (s->kind == CEMU_KIND_PC98 || s->kind == CEMU_KIND_PCAT) ? 1 : 0;
}

int CEmuSessionCodeIsOverlay(CEmuSession* s, unsigned titleCode)
{
	if (!s) return 0;
	if (s->game && CEmuGameTitleCodeIsToggle(s->game, titleCode))
		return 0;
	if (CDriver* drv = CEmuSessionDriver(s)) {
		const int r = drv->CodeIsOverlay(titleCode);
		if (r >= 0)
			return r;
	}
	return CEmuGameTitleCodeIsMixOverlay(s->game, titleCode);
}

/* 同一 zip SE: Render スレッドで OverlayTitle するようフラグを立てる */
int CEmuSessionOverlayTitle(CEmuSession* s, unsigned titleCode, int mixSfx)
{
	if (!s || s->kind == 0) return 0;
	if (s->kind == CEMU_KIND_S98 || s->kind == CEMU_KIND_MDX || s->kind == CEMU_KIND_PMD)
		return 0;
	s->overlayCode = titleCode;
	s->overlayMix = mixSfx ? 1 : 0;
	InterlockedExchange((LONG*)&s->overlayPend, 1);
	return 1;
}

/* ステレオ加算。クリップして 16bit に収める */
void CEmuSessionMixStereo(short* dst, const short* add, int frames)
{
	if (!dst || !add || frames <= 0) return;
	const int n = frames * 2;
	for (int i = 0; i < n; i++) {
		int v = (int)dst[i] + (int)add[i];
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		dst[i] = (short)v;
	}
}

/* 実機へ出すチップ。1=OPNA 系 2=OPM 3=OPL 4=OPLL。コンストラクタがクロックを残す。 */
static int s_gimicLive[5];
static int s_gimicCount[5];
static int s_gimicYm2612;
static int s_gimicCat2612;
static uint32_t s_gimicClk[5];
static int s_gimicKind;
static int s_gimicSuppress;
static int s_gimicMix;
static uint32_t s_gimicClock;

extern __int64 g_dsWrittenBytes;
extern __int64 g_heardBytes;
extern int g_outBytesPerFrame;
extern save savedata;

void CEmuGimicNoteChip(int kind, uint32_t clockHz, int on)
{
	if (kind < 1 || kind > 4) return;
	if (on) {
		s_gimicCount[kind]++;
		s_gimicLive[kind] = 1;
		if (clockHz)
			s_gimicClk[kind] = clockHz;
	} else if (s_gimicCount[kind] > 0) {
		s_gimicCount[kind]--;
		if (s_gimicCount[kind] == 0 && !(kind == KBS_GIMIC_CHIP_OPNA && s_gimicYm2612 > 0)) {
			s_gimicLive[kind] = 0;
			s_gimicClk[kind] = 0;
		}
	}
}

void CEmuGimicNoteYm2612(uint32_t clockHz, int on)
{
	if (on) {
		if (s_gimicCount[KBS_GIMIC_CHIP_OPNA] == 0) {
			s_gimicLive[KBS_GIMIC_CHIP_OPNA] = 1;
			if (clockHz)
				s_gimicClk[KBS_GIMIC_CHIP_OPNA] = clockHz;
		}
		s_gimicYm2612++;
	} else if (s_gimicYm2612 > 0) {
		s_gimicYm2612--;
		if (s_gimicYm2612 == 0 && s_gimicCount[KBS_GIMIC_CHIP_OPNA] == 0) {
			s_gimicLive[KBS_GIMIC_CHIP_OPNA] = 0;
			s_gimicClk[KBS_GIMIC_CHIP_OPNA] = 0;
		}
	}
}

static int CEmuGimicCovered(int id, int kind)
{
	if (kind == KBS_GIMIC_CHIP_OPNA)
		return id == CEMU_CHIP_OPNA || id == CEMU_CHIP_OPN || id == CEMU_CHIP_YM2612
			|| id == CEMU_CHIP_AY;
	if (kind == KBS_GIMIC_CHIP_OPM)
		return id == CEMU_CHIP_OPM;
	if (kind == KBS_GIMIC_CHIP_OPL)
		return id == CEMU_CHIP_OPL2 || id == CEMU_CHIP_OPL3;
	if (kind == KBS_GIMIC_CHIP_OPLL)
		return id == CEMU_CHIP_OPLL;
	return 0;
}

static int CEmuGimicListHasExtra(const int* ids, int n, int kind)
{
	if (!ids) return 0;
	for (int i = 0; i < n; i++) {
		if (!ids[i]) continue;
		if (!CEmuGimicCovered(ids[i], kind))
			return 1;
	}
	return 0;
}

int CEmuGimicWriteOk(int kind)
{
	/* ADPCM を載せ終わるまで FM を出さない。先読み中の数音が実チップに乗ると、
	   そのあと転送で演奏が止まる。 */
	return (s_gimicKind == kind && s_adpcmHold && !s_gimicSuppress && savedata.cemuGimic) ? 1 : 0;
}

int64_t CEmuGimicStamp(void)
{
	return (int64_t)FmMonShadowGetCurSample();
}

void CEmuGimicAdpcmGuard(uint32_t samples)
{
	if (!samples) samples = 1;
	s_adpcmGuardUntil = FmMonShadowGetCurSample() + samples;
}

int CEmuGimicAdpcmDrop(unsigned addr, unsigned data)
{
	if (!s_adpcmGuardUntil) return 0;
	if (FmMonShadowGetCurSample() >= s_adpcmGuardUntil) {
		s_adpcmGuardUntil = 0;
		return 0;
	}
	/* 載せた直後の CPU 再転送（記録・外部メモリ・データポート）だけ捨てる。 */
	if (addr == 0x108u) return 1;
	if (addr == 0x100u && (data & 0x60u)) return 1;
	return 0;
}

int CEmuGimicAdpcmPrep(unsigned addr, unsigned data)
{
	if (addr == 0x108u)
		return 2;
	if (addr == 0x100u && (data & 0x40u))
		return 2;
	/* 先読み中の FM/SSG は実チップへ出さず残す。ADPCM のあと曲頭として流す。 */
	if (!s_adpcmHold && s_gimicKind && !s_gimicSuppress && savedata.cemuGimic) {
		if (s_capN < CEMU_GIMIC_CAP) {
			unsigned mark = CEmuPc98LeadMark();
			if (mark == 0 || mark == 0xFFFFFFFFu)
				mark = CEmuPc88LeadMark();
			s_capA[s_capN] = (uint16_t)addr;
			s_capD[s_capN] = (uint8_t)data;
			s_capS[s_capN] = mark;
			s_capN++;
		}
		return 2;
	}
	return 0;
}

int CEmuGimicMonitorLive(void)
{
	/* 実チップがそのまま鳴る曲は、モニタを PCM 用の遅れに載せない。 */
	return (s_gimicKind && !s_gimicMix && !s_gimicSuppress
		&& savedata.cemuGimic && KbsGimicDevOk()) ? 1 : 0;
}

void CEmuGimicAdpcmRelease(void)
{
	if (!s_adpcmRelease) return;
	s_adpcmRelease = 0;
	FmMonShadowPushGimic();
	FmMonShadowPushAdpcmPlayback();
}

int CEmuGimicOwns(int kind)
{
	return (CEmuGimicWriteOk(kind) && KbsGimicDevOk()) ? 1 : 0;
}

int CEmuGimicOpnaIsYm2612(void)
{
	if (s_gimicCount[KBS_GIMIC_CHIP_OPNA] > 0) return 0;
	return (s_gimicYm2612 > 0 || s_gimicCat2612) ? 1 : 0;
}

static int64_t CEmuGimicDsLead(void)
{
	int bpf = g_outBytesPerFrame > 0 ? g_outBytesPerFrame : 4;
	int64_t live = 0;
	if (g_dsWrittenBytes > g_heardBytes)
		live = (g_dsWrittenBytes - g_heardBytes) / bpf;
	const int64_t cap = (int64_t)30720 * 5 / bpf;
	if (live < 0) live = 0;
	if (live > cap) live = cap;
	/* バッファがまだ空の立ち上がりは、埋まったときの遅れに合わせる。
	   途中から遅らせると先に出た音だけ速く聞こえる。 */
	if (live < cap / 2)
		live = cap;
	return live;
}

void CEmuGimicStop(void)
{
	s_gimicKind = 0;
	s_gimicMix = 0;
	s_gimicClock = 0;
	s_gimicPushed = 0;
	s_gimicNeedBegin = 0;
	s_gimicCat2612 = 0;
	s_adpcmGuardUntil = 0;
	s_adpcmLoading = 0;
	s_adpcmHold = 0;
	s_adpcmRelease = 0;
	s_adpcmBytes = 0;
	s_capN = 0;
	s_capR = 0;
	s_paceOn = 0;
	KbsGimicSetQueueWait(40);
	KbsGimicSetBurst(0);
	KbsGimicSetExtraLag(0);
	KbsGimicEnd();
	KbsGimicSetEnable(0);
}

static void CEmuGimicArm(CEmuSession* s)
{
	if (!s || !s->kind || !savedata.cemuGimic || s->kind == CEMU_KIND_PMD) {
		if (s_gimicKind)
			CEmuGimicStop();
		return;
	}
	int kind = 0;
	if (s_gimicLive[KBS_GIMIC_CHIP_OPNA]) kind = KBS_GIMIC_CHIP_OPNA;
	else if (s_gimicLive[KBS_GIMIC_CHIP_OPM]) kind = KBS_GIMIC_CHIP_OPM;
	else if (s_gimicLive[KBS_GIMIC_CHIP_OPL]) kind = KBS_GIMIC_CHIP_OPL;
	else if (s_gimicLive[KBS_GIMIC_CHIP_OPLL]) kind = KBS_GIMIC_CHIP_OPLL;
	s_gimicCat2612 = 0;
	if (!kind && s->game) {
		int seen2612 = 0;
		int seenOpn = 0;
		const int* lists[2] = { s->game->chipIds, s->game->docChipIds };
		const int ns[2] = { s->game->chipCount, s->game->docChipCount };
		for (int li = 0; li < 2; li++) {
			if (!lists[li]) continue;
			for (int i = 0; i < ns[li]; i++) {
				const int id = lists[li][i];
				if (id == CEMU_CHIP_YM2612) seen2612 = 1;
				if (id == CEMU_CHIP_OPNA || id == CEMU_CHIP_OPN) seenOpn = 1;
				int k = 0;
				if (id == CEMU_CHIP_OPNA || id == CEMU_CHIP_OPN || id == CEMU_CHIP_YM2612)
					k = KBS_GIMIC_CHIP_OPNA;
				else if (id == CEMU_CHIP_OPM) k = KBS_GIMIC_CHIP_OPM;
				else if (id == CEMU_CHIP_OPL2 || id == CEMU_CHIP_OPL3) k = KBS_GIMIC_CHIP_OPL;
				else if (id == CEMU_CHIP_OPLL) k = KBS_GIMIC_CHIP_OPLL;
				if (k == KBS_GIMIC_CHIP_OPNA) kind = k;
				else if (!kind && k) kind = k;
				else if (kind != KBS_GIMIC_CHIP_OPNA && k == KBS_GIMIC_CHIP_OPM) kind = k;
				else if (kind != KBS_GIMIC_CHIP_OPNA && kind != KBS_GIMIC_CHIP_OPM
					&& k == KBS_GIMIC_CHIP_OPL) kind = k;
			}
		}
		if (kind == KBS_GIMIC_CHIP_OPNA && seen2612 && !seenOpn)
			s_gimicCat2612 = 1;
	}
	if (!kind) {
		if (s_gimicKind)
			CEmuGimicStop();
		return;
	}
	uint32_t clk = s_gimicClk[kind];
	if (!clk) {
		if (kind == KBS_GIMIC_CHIP_OPNA) clk = 7987200u;
		else if (kind == KBS_GIMIC_CHIP_OPM) clk = 4000000u;
		else clk = 3579545u;
	}
	/* YM2203 は ~4MHz・内部 ÷72。実 OPNA(YM2608) は ~8MHz・内部 ÷144。
	   同じ F 番号を 3993600 のまま PLL に渡すと音程が半分になる。 */
	if (kind == KBS_GIMIC_CHIP_OPNA && clk > 0 && clk < 7000000u)
		clk *= 2u;
	int mix = 0;
	/* 純FMはソフトウェア PCM を混ぜない。PC-98/X68k/MDX も OPNA/OPM だけなら
	   PC-88 と同じく実チップに任せる。OPL や PCM が並ぶときだけ下の extra で mix。 */
	if (s->kind == CEMU_KIND_AC || s->kind == CEMU_KIND_F3 || s->kind == CEMU_KIND_PCAT)
		mix = 1;
	if (s->game) {
		if (CEmuGimicListHasExtra(s->game->chipIds, s->game->chipCount, kind)
			|| CEmuGimicListHasExtra(s->game->docChipIds, s->game->docChipCount, kind))
			mix = 1;
	}
	/* YM2612 は AY/SSG を持たない。カタログの AY は別チップ。 */
	if (kind == KBS_GIMIC_CHIP_OPNA && clk > 7000000u && clk < 7800000u && s->game) {
		for (int i = 0; i < s->game->chipCount; i++)
			if (s->game->chipIds[i] == CEMU_CHIP_AY)
				mix = 1;
	}
	const int rate = s->sampleRate > 0 ? s->sampleRate : 44100;
	if (s_gimicNeedBegin || s_gimicKind != kind || s_gimicClock != clk) {
		s_gimicNeedBegin = 0;
		KbsGimicSetEnable(1);
		KbsGimicBegin(kind, clk, (uint32_t)rate);
		s_gimicKind = kind;
		s_gimicClock = clk;
		s_gimicPushed = 0;
		/* リセットで RAM が消える。先に置いたデータは次の発音前に載せ直す。 */
		s_adpcmHold = 0;
		s_paceOn = 0;
		s_capN = 0;
		s_capR = 0;
	}
	s_gimicMix = mix;
	/* 純FMはホストで時刻を付け直さない。テンポは CEmu のスライス（約 4ms）に任せる。 */
	KbsGimicSetBurst(mix ? 0 : 1);
	KbsGimicSetExtraLag(mix ? CEmuGimicDsLead() : 0);
}

void CEmuGimicOnRender(CEmuSession* s, int16_t* stereo, int frames)
{
	CEmuGimicArm(s);
	CEmuGimicAdpcmRelease();
	if (KbsGimicDevOk() && !s_gimicPushed) {
		/* 音色 → 実チップへ届くのを待つ → ADPCM → キーオン。
		   キーオンを音色・ADPCM と同じ束で出すと、初回だけ壊れた音色になる。 */
		s_adpcmHold = 1;
		FmMonShadowPushGimic();
		KbsGimicWaitDrained(5000);
		if (s_gimicKind == KBS_GIMIC_CHIP_OPNA && CEmuChipOpnaFlushAdpcmB(1))
			KbsGimicWaitDrained(20000);
		FmMonShadowPushAdpcmPlayback();
		s_gimicPushed = 1;
		if (!s_gimicMix) {
			QueryPerformanceCounter(&s_paceT0);
			s_paceOrigin = (int64_t)FmMonShadowGetCurSample();
			s_paceOn = 1;
		}
	}
	if (s_gimicPushed && s_capR < s_capN) {
		unsigned until = CEmuPc98LeadMark();
		if (until == 0 || until == 0xFFFFFFFFu) {
			const unsigned a = CEmuPc88LeadMark();
			if (until == 0 || (a != 0 && a != 0xFFFFFFFFu))
				until = a;
		}
		if (until == 0)
			until = 0xFFFFFFFFu;
		while (s_capR < s_capN && s_capS[s_capR] <= until) {
			KbsGimicWrite(s_capA[s_capR], s_capD[s_capR], 0);
			s_capR++;
		}
	}
	if (!s_gimicKind || !stereo || frames <= 0) return;
	KbsGimicSetBurst(s_gimicMix ? 0 : 1);
	KbsGimicSetExtraLag(s_gimicMix ? CEmuGimicDsLead() : 0);
	const int rate = s && s->sampleRate > 0 ? s->sampleRate : 44100;
	const int64_t pos = (int64_t)FmMonShadowGetCurSample();
	if (!s_gimicMix && KbsGimicDevOk())
		KbsGimicSilence(stereo, (uint32_t)frames, pos, (uint32_t)rate);
	else
		KbsGimicSilence(NULL, (uint32_t)frames, pos, (uint32_t)rate);
	/* 純FMはバッファ先読みでエミュを走らせない。実時間に合わせるとチップとモニタが同じテンポになる。 */
	if (s_paceOn && !s_gimicMix && KbsGimicDevOk() && frames > 0) {
		LARGE_INTEGER freq, now;
		QueryPerformanceFrequency(&freq);
		const int64_t want = pos + (int64_t)frames - s_paceOrigin;
		const int64_t slack = (int64_t)rate / 40;
		for (;;) {
			QueryPerformanceCounter(&now);
			int64_t wall = 0;
			if (freq.QuadPart)
				wall = (now.QuadPart - s_paceT0.QuadPart) * (int64_t)rate / freq.QuadPart;
			const int64_t ahead = want - slack - wall;
			if (ahead <= 0) break;
			int ms = (int)(ahead * 1000 / (int64_t)rate);
			if (ms < 1) ms = 1;
			if (ms > 8) ms = 8;
			Sleep(ms);
		}
	}
}

void CEmuGimicSuppress(int on)
{
	s_gimicSuppress = on ? 1 : 0;
	if (!on)
		KbsGimicDropPending();
}

void CEmuGimicPrepare(CEmuSession* s)
{
	CEmuGimicArm(s);
}
