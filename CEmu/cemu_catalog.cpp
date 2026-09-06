#include "StdAfx.h"
#include "cemu_catalog.h"
#include "cemu_zipfs.h"
#include "minizip/unzip.h"
#include "minizip/iowin32.h"
#include <string.h>
#include <shlobj.h>

static void CEmuTrim(char* s)
{
	if (!s) return;
	char* p = s;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (p != s) memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = 0;
}

static void CEmuDecodeXmlEntities(char* s)
{
	if (!s || !strchr(s, '&')) return;
	char* dst = s;
	for (const char* src = s; *src; ) {
		char decoded = 0;
		int used = 0;
		if (_strnicmp(src, "&amp;", 5) == 0) { decoded = '&'; used = 5; }
		else if (_strnicmp(src, "&lt;", 4) == 0) { decoded = '<'; used = 4; }
		else if (_strnicmp(src, "&gt;", 4) == 0) { decoded = '>'; used = 4; }
		else if (_strnicmp(src, "&quot;", 6) == 0) { decoded = '"'; used = 6; }
		else if (_strnicmp(src, "&apos;", 6) == 0) { decoded = '\''; used = 6; }
		if (used) {
			*dst++ = decoded;
			src += used;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = 0;
}

static const char* CEmuStrStr(const char* hay, const char* needle)
{
	if (!hay || !needle || !needle[0]) return hay;
	size_t nlen = strlen(needle);
	for (const char* p = hay; *p; p++) {
		if (_strnicmp(p, needle, nlen) == 0)
			return p;
	}
	return NULL;
}

static void CEmuAttrValue(const char* tag, const char* attr, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!tag || !attr) return;
	/* Match attr="…" and attr = "…" (xml2 often inserts spaces around '='). */
	const char* p = CEmuStrStr(tag, attr);
	while (p) {
		const char* q = p + strlen(attr);
		while (*q == ' ' || *q == '\t') q++;
		if (*q == '=') {
			q++;
			while (*q == ' ' || *q == '\t') q++;
			if (*q == '"') {
				q++;
				const char* e = strchr(q, '"');
				if (!e) return;
				int n = (int)(e - q);
				if (n >= outCap) n = outCap - 1;
				memcpy(out, q, (size_t)n);
				out[n] = 0;
				CEmuDecodeXmlEntities(out);
				return;
			}
		}
		p = CEmuStrStr(p + 1, attr);
	}
}

static void CEmuTagText(const char* block, const char* tag, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!block || !tag) return;
	char open[32];
	char close[32];
	_snprintf_s(open, _TRUNCATE, "<%s", tag);
	_snprintf_s(close, _TRUNCATE, "</%s>", tag);
	const char* p = CEmuStrStr(block, open);
	if (!p) return;
	p = strchr(p, '>');
	if (!p) return;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	const char* e = CEmuStrStr(p, close);
	if (!e) return;
	int n = (int)(e - p);
	if (n >= outCap) n = outCap - 1;
	memcpy(out, p, (size_t)n);
	out[n] = 0;
	CEmuTrim(out);
	CEmuDecodeXmlEntities(out);
}

static void CEmuDecodeXmlEntitiesW(wchar_t* s)
{
	if (!s || !wcschr(s, L'&')) return;
	wchar_t* dst = s;
	for (const wchar_t* src = s; *src; ) {
		wchar_t decoded = 0;
		int used = 0;
		if (_wcsnicmp(src, L"&amp;", 5) == 0) { decoded = L'&'; used = 5; }
		else if (_wcsnicmp(src, L"&lt;", 4) == 0) { decoded = L'<'; used = 4; }
		else if (_wcsnicmp(src, L"&gt;", 4) == 0) { decoded = L'>'; used = 4; }
		else if (_wcsnicmp(src, L"&quot;", 6) == 0) { decoded = L'"'; used = 6; }
		else if (_wcsnicmp(src, L"&apos;", 6) == 0) { decoded = L'\''; used = 6; }
		if (used) {
			*dst++ = decoded;
			src += used;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = 0;
}

static void CEmuSjisToWide(const char* sjis, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	if (!sjis || !sjis[0]) return;
	MultiByteToWideChar(932, 0, sjis, -1, out, outChars);
	CEmuDecodeXmlEntitiesW(out);
}

static unsigned CEmuParseUnum(const char* s)
{
	if (!s || !s[0]) return 0;
	while (*s == ' ' || *s == '\t') s++;
	if (_strnicmp(s, "0x", 2) == 0)
		return (unsigned)strtoul(s + 2, NULL, 16);
	return (unsigned)strtoul(s, NULL, 0);
}

static void CEmuFormatCodeLabel(const char* fmt, unsigned code, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!fmt) return;
	int oi = 0;
	for (const char* p = fmt; *p && oi < outCap - 1; ) {
		if (*p != '%') {
			out[oi++] = *p++;
			continue;
		}
		p++;
		int zeroPad = 0;
		int width = 0;
		char conv = 0;
		for (;; p++) {
			if (*p == '0' && width == 0 && !zeroPad) { zeroPad = 1; continue; }
			if (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); continue; }
			if (*p == 'x' || *p == 'X' || *p == 'd' || *p == 'u') { conv = *p; p++; break; }
			if (*p == '%') { if (oi < outCap - 1) out[oi++] = '%'; break; }
			if (oi < outCap - 1) out[oi++] = '%';
			if (*p && oi < outCap - 1) out[oi++] = *p++;
			break;
		}
		if (!conv) continue;
		char num[32];
		if (conv == 'x')
			_snprintf_s(num, _TRUNCATE, "%x", code);
		else if (conv == 'X')
			_snprintf_s(num, _TRUNCATE, "%X", code);
		else
			_snprintf_s(num, _TRUNCATE, "%u", code);
		int pad = width - (int)strlen(num);
		if (pad < 0) pad = 0;
		for (int i = 0; i < pad && oi < outCap - 1; i++)
			out[oi++] = zeroPad ? '0' : ' ';
		for (const char* n = num; *n && oi < outCap - 1; n++)
			out[oi++] = *n;
	}
	out[oi] = 0;
}

/* <titlelist> を拾わない — 先頭が <title code= / <title> のみ */
static const char* CEmuFindTitleOpenTag(const char* from)
{
	if (!from) return NULL;
	for (const char* p = from; ; ) {
		p = CEmuStrStr(p, "<title");
		if (!p) return NULL;
		const char c = p[6];
		if (c == ' ' || c == '>' || c == '\t' || c == '\r' || c == '\n')
			return p;
		p += 6;
	}
}

static int CEmuArchiveStem(const wchar_t* zipPath, char* out, int outCap)
{
	if (!out || outCap <= 0 || !zipPath) return 0;
	out[0] = 0;
	wchar_t base[MAX_PATH];
	wcsncpy_s(base, zipPath, _TRUNCATE);
	wchar_t* dot = wcsrchr(base, L'.');
	if (dot) *dot = 0;
	wchar_t* slash = wcsrchr(base, L'\\');
	if (!slash) slash = wcsrchr(base, L'/');
	const wchar_t* name = slash ? slash + 1 : base;
	WideCharToMultiByte(932, 0, name, -1, out, outCap, NULL, NULL);
	for (char* p = out; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
	}
	return out[0] != 0;
}

void CEmuCatalogInit(CEmuCatalog* cat)
{
	if (!cat) return;
	cat->count = 0;
	cat->capacity = 0;
	cat->entry = NULL;
	cat->loaded = 0;
}

static void CEmuGameEntryFree(CEmuGameEntry* ge)
{
	if (!ge) return;
	free(ge->rom);
	free(ge->opt);
	free(ge->title);
	free(ge);
}

void CEmuCatalogClear(CEmuCatalog* cat)
{
	if (!cat) return;
	if (cat->entry) {
		for (int i = 0; i < cat->count; i++)
			CEmuGameEntryFree(cat->entry[i]);
		free(cat->entry);
	}
	CEmuCatalogInit(cat);
}

/* パース用の固定枠（1 件分を再利用。最終カタログは件数分のみヒープ） */
struct CEmuGameBuild {
	wchar_t name[CEMU_GAME_NAME];
	wchar_t driverAlias[CEMU_GAME_NAME];
	char platform[CEMU_DRIVER_NAME];
	char subtype[CEMU_DRIVER_TYPE];
	char dataDir[CEMU_DATA_DIR];
	char archive[CEMU_ARCHIVE_NAME];
	int romCount;
	int optCount;
	int titleCount;
	CEmuRomEntry rom[CEMU_ROM_MAX];
	CEmuOptionEntry opt[CEMU_OPTION_MAX];
	CEmuTitleEntry title[CEMU_TITLE_MAX];
	int cpuId;
	int chipIds[8];
	int chipCount;
};

static void CEmuPushFormattedTitle(CEmuGameBuild* ge, unsigned code, const char* fmt)
{
	if (!ge || ge->titleCount >= CEMU_TITLE_MAX) return;
	CEmuTitleEntry* t = &ge->title[ge->titleCount++];
	t->code = code;
	char labA[CEMU_GAME_NAME * 2];
	if (fmt && fmt[0])
		CEmuFormatCodeLabel(fmt, code, labA, (int)sizeof(labA));
	else
		_snprintf_s(labA, _TRUNCATE, "%u", code);
	CEmuSjisToWide(labA, t->label, CEMU_GAME_NAME);
}

static CEmuGameBuild* CEmuBuildBuf()
{
	static CEmuGameBuild* s_build = NULL;
	if (!s_build)
		s_build = (CEmuGameBuild*)malloc(sizeof(CEmuGameBuild));
	return s_build;
}

static CEmuGameEntry* CEmuGameEntryFromBuild(const CEmuGameBuild* b)
{
	if (!b) return NULL;
	CEmuGameEntry* ge = (CEmuGameEntry*)calloc(1, sizeof(CEmuGameEntry));
	if (!ge) return NULL;
	wcsncpy_s(ge->name, b->name, _TRUNCATE);
	wcsncpy_s(ge->driverAlias, b->driverAlias, _TRUNCATE);
	strncpy_s(ge->platform, b->platform, _TRUNCATE);
	strncpy_s(ge->subtype, b->subtype, _TRUNCATE);
	strncpy_s(ge->dataDir, b->dataDir, _TRUNCATE);
	strncpy_s(ge->archive, b->archive, _TRUNCATE);
	ge->cpuId = b->cpuId;
	ge->chipCount = b->chipCount;
	memcpy(ge->chipIds, b->chipIds, sizeof(ge->chipIds));
	if (b->romCount > 0) {
		ge->rom = (CEmuRomEntry*)malloc(sizeof(CEmuRomEntry) * (size_t)b->romCount);
		if (ge->rom) {
			memcpy(ge->rom, b->rom, sizeof(CEmuRomEntry) * (size_t)b->romCount);
			ge->romCount = b->romCount;
		}
	}
	if (b->optCount > 0) {
		ge->opt = (CEmuOptionEntry*)malloc(sizeof(CEmuOptionEntry) * (size_t)b->optCount);
		if (ge->opt) {
			memcpy(ge->opt, b->opt, sizeof(CEmuOptionEntry) * (size_t)b->optCount);
			ge->optCount = b->optCount;
		}
	}
	if (b->titleCount > 0) {
		ge->title = (CEmuTitleEntry*)malloc(sizeof(CEmuTitleEntry) * (size_t)b->titleCount);
		if (ge->title) {
			memcpy(ge->title, b->title, sizeof(CEmuTitleEntry) * (size_t)b->titleCount);
			ge->titleCount = b->titleCount;
		}
	}
	return ge;
}

void CEmuCatalogAssignHwIds(CEmuGameEntry* ge)
{
	if (!ge) return;
	ge->cpuId = 0;
	ge->chipCount = 0;
	for (int i = 0; i < 8; i++) ge->chipIds[i] = 0;

	const char* plat = ge->platform;
	const char* sub = ge->subtype;

	if (_stricmp(plat, "pc88") == 0 || _stricmp(plat, "pc80sr") == 0) {
		ge->cpuId = CEMU_CPU_Z80;
		/* Specialty Falcom / SB2 aliases → generic OPN(A) machine. */
		if (_stricmp(sub, "opna") == 0 || _stricmp(sub, "8801-10") == 0)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPNA;
		else
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN; /* opn, asteka2, xanadu* */
	}
	else if (_stricmp(plat, "pc88va") == 0 || _stricmp(plat, "pc88vados") == 0
		|| _stricmp(plat, "pc98") == 0 || _stricmp(plat, "pc98dos") == 0
		|| _stricmp(plat, "pc9821") == 0 || _stricmp(plat, "pc98vx") == 0) {
		/* pc88va/vados share bootcs+BIOSD V30 layout with PC-98. */
		ge->cpuId = CEMU_CPU_I286;
		if (_stricmp(sub, "opna") == 0 || _stricmp(sub, "86") == 0 || _stricmp(sub, "86+otomix2") == 0)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPNA;
		else if (_stricmp(sub, "opn") == 0 || _stricmp(sub, "opn3l") == 0)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN;
		else if (_stricmp(sub, "beep") == 0)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		else
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPNA;
	}
	else if (_stricmp(plat, "x68k") == 0) {
		ge->cpuId = CEMU_CPU_M68000;
		if (_stricmp(sub, "mxdrv") == 0 || _stricmp(sub, "opm") == 0)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		else
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
	}
	else if (_stricmp(sub, "sg1000") == 0 || _stricmp(plat, "sg1000") == 0
		|| _stricmp(ge->dataDir, "sc3000") == 0) {
		/* SC-3000 / SG-1000 cartridge: Z80 + SN76489. */
		ge->cpuId = CEMU_CPU_Z80;
		ge->chipIds[ge->chipCount++] = CEMU_CHIP_SN76489;
	}
	else if (_stricmp(plat, "sega") == 0
		|| _stricmp(plat, "videosystem") == 0
		|| _strnicmp(sub, "system16", 8) == 0
		|| _strnicmp(sub, "system18", 8) == 0
		|| _strnicmp(sub, "system24", 8) == 0
		|| _strnicmp(sub, "system32", 8) == 0
		|| _stricmp(sub, "outrun") == 0 || _stricmp(sub, "aburner") == 0
		|| _stricmp(sub, "sharrier") == 0 || _stricmp(sub, "hangon") == 0
		|| _stricmp(sub, "toutrun") == 0 || _stricmp(sub, "shangon") == 0
		|| _stricmp(sub, "aerofgt") == 0
		|| (_strnicmp(sub, "cps1", 4) == 0 && _stricmp(sub, "cps1qs") != 0)) {
		ge->cpuId = CEMU_CPU_Z80;
		if (_stricmp(sub, "sharrier") == 0 || _stricmp(sub, "hangon") == 0) {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_SEGAPCM;
		} else if (_stricmp(sub, "toutrun") == 0 || _stricmp(sub, "shangon") == 0
			|| _stricmp(sub, "outrun") == 0 || _stricmp(sub, "aburner") == 0) {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_SEGAPCM;
		} else if (_strnicmp(sub, "system18", 8) == 0) {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2612;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_RF5C68;
		} else if (_strnicmp(sub, "system24", 8) == 0) {
			/* Dual 68000 host + YM2151; disk packs have no Z80/RF5C68. */
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(sub, "aerofgt") == 0 || _stricmp(plat, "videosystem") == 0) {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2610;
		} else if (_strnicmp(sub, "system32", 8) == 0
			|| _stricmp(sub, "system_multi") == 0
			|| _stricmp(sub, "multi32") == 0) {
			/* MAME segas32: YM3438 (OPN2) + RF5C68 — not OPM+SegaPCM. */
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2612;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_RF5C68;
		} else {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		}
	}
	else if (_stricmp(plat, "megadrive") == 0) {
		ge->cpuId = CEMU_CPU_M68000;
		ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2612;
		ge->chipIds[ge->chipCount++] = CEMU_CHIP_SN76489;
	}
	else if (_stricmp(plat, "neogeo") == 0 || _stricmp(sub, "neogeo") == 0) {
		ge->cpuId = CEMU_CPU_M68000;
		ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2610;
	}
	else if (_strnicmp(plat, "capcom", 6) == 0 || _strnicmp(plat, "konami", 6) == 0
		|| _stricmp(plat, "taito") == 0 || _stricmp(plat, "namco") == 0
		|| _stricmp(plat, "irem") == 0 || _stricmp(plat, "dataeast") == 0
		|| _stricmp(ge->dataDir, "ac") == 0) {
		if (_stricmp(sub, "cps1qs") == 0 || _stricmp(sub, "cps2") == 0
			|| _stricmp(sub, "zn") == 0 || _strnicmp(sub, "qs", 2) == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_QSOUND;
		} else if (_strnicmp(sub, "cps1", 4) == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(sub, "gng") == 0 || _stricmp(sub, "opn2") == 0
			|| _stricmp(sub, "tigerroad") == 0 || _stricmp(sub, "tigeroad") == 0
			|| _stricmp(sub, "rushcrsh") == 0 || _stricmp(sub, "srumbler") == 0
			|| _stricmp(sub, "commando") == 0 || _stricmp(sub, "sectionz") == 0
			|| _stricmp(sub, "trojan") == 0 || _stricmp(sub, "higemaru") == 0
			|| _stricmp(sub, "exedexes") == 0 || _stricmp(sub, "gunsmoke") == 0
			|| _stricmp(sub, "blktiger") == 0 || _stricmp(sub, "blacktiger") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN;
		} else if (_stricmp(sub, "053260") == 0 || _stricmp(sub, "054539") == 0
			|| _stricmp(sub, "054539x2") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			if (_stricmp(sub, "054539") == 0 || _stricmp(sub, "054539x2") == 0)
				ge->chipIds[ge->chipCount++] = CEMU_CHIP_K054539;
			else
				ge->chipIds[ge->chipCount++] = CEMU_CHIP_K053260;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(sub, "systemgx") == 0) {
			/* Sound 68000 + dual K054539 + K056800 (MAME konamigx gxsndmap). */
			ge->cpuId = CEMU_CPU_M68000;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_K054539;
		} else if (_stricmp(plat, "namco") == 0
			&& (_stricmp(sub, "system2") == 0 || _stricmp(sub, "system21") == 0
				|| _stricmp(sub, "c140") == 0)) {
			/* Namco System 2/21: M6809 sound (no core) + YM2151 + C140. */
			ge->cpuId = CEMU_CPU_M6809;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_C140;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(plat, "namco") == 0 && _stricmp(sub, "system1") == 0) {
			/* Namco System 1: M6809 sound (no core) + YM2151 + CUS30.
			   MAME namcos1 sound_map: YM2151 @4000, CUS30 amap @5000. */
			ge->cpuId = CEMU_CPU_M6809;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_C30;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(plat, "namco") == 0 && _stricmp(sub, "system86") == 0) {
			/* Namco System 86: HD63701 MCU sound (no core) + YM2151 + CUS30.
			   MAME namcos86: CUS30 amap @1000, YM2151 @2000 on MCU. */
			ge->cpuId = CEMU_CPU_M6809;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_C30;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(plat, "namco") == 0
			&& (_stricmp(sub, "wsg6809") == 0 || _stricmp(sub, "wsg63701") == 0
				|| _stricmp(sub, "wsgz80") == 0 || _stricmp(sub, "wsg") == 0
				|| _stricmp(sub, "c30") == 0 || _stricmp(sub, "cus30") == 0)) {
			/* Older Namco WSG / 15XX (Mappy etc.): CUS30 in MAPPY register mode. */
			ge->cpuId = CEMU_CPU_M6809;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_C30;
		} else if (_stricmp(sub, "system12") == 0
			|| _stricmp(sub, "system11") == 0 || _stricmp(sub, "system22") == 0
			|| _stricmp(sub, "nd1") == 0) {
			ge->cpuId = CEMU_CPU_M68000;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_C352;
		} else if (_stricmp(sub, "na1") == 0 || _stricmp(sub, "na2") == 0
			|| _stricmp(sub, "nb1") == 0 || _stricmp(sub, "nb2") == 0) {
			/* MAME namcona1: C219 (C140 variant), not C352. */
			ge->cpuId = CEMU_CPU_M68000;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_C140;
		} else if (_stricmp(sub, "f3system") == 0) {
			ge->cpuId = CEMU_CPU_M68000;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_ES5505;
		} else if (_stricmp(sub, "f2system") == 0 || _stricmp(sub, "bsystem") == 0
			|| _stricmp(sub, "dual68") == 0 || _stricmp(sub, "taitoh") == 0
			|| _stricmp(sub, "spacegun") == 0 || _stricmp(sub, "bshark") == 0
			|| _stricmp(sub, "warriorb") == 0 || _stricmp(sub, "fx1a") == 0
			|| _stricmp(sub, "gseeker") == 0 || _stricmp(sub, "ridingf") == 0
			|| _stricmp(sub, "ringrage") == 0) {
			/* Taito F2/B/H / Z-System sound board: Z80 + YM2610 + TC0140SYT. */
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2610;
		} else if (_stricmp(sub, "fullt") == 0 || _stricmp(sub, "rastan") == 0
			|| _stricmp(sub, "asuka") == 0
			|| _stricmp(sub, "opwolf") == 0 || _stricmp(sub, "rainbow") == 0) {
			/* Rastan/Asuka sound board: Z80 + YM2151 + PC060HA. */
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(sub, "masterw") == 0 || _stricmp(sub, "viofight") == 0
			|| _stricmp(sub, "tnzs") == 0 || _stricmp(sub, "chukatai") == 0
			|| _stricmp(sub, "extrmatn") == 0) {
			/* Taito B YM2203 (+ OKI on viofight). */
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN;
			if (_stricmp(sub, "viofight") == 0)
				ge->chipIds[ge->chipCount++] = CEMU_CHIP_OKI6295;
		} else if (_stricmp(sub, "flstory") == 0 || _stricmp(sub, "nycaptor") == 0
			|| _stricmp(sub, "tokio") == 0 || _stricmp(sub, "buggychl") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		} else if (_stricmp(sub, "scramble") == 0 || _stricmp(sub, "scobra") == 0
			|| _stricmp(sub, "frogger") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		} else if (_stricmp(sub, "timeplt") == 0 || _stricmp(sub, "pooyan") == 0
			|| _stricmp(sub, "locomotn") == 0 || _stricmp(sub, "jungler") == 0
			|| _stricmp(sub, "circusc") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		} else if (_stricmp(sub, "terracre") == 0 || _stricmp(sub, "terraf") == 0
			|| _stricmp(sub, "cclimbr2") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPL2;
		} else if (_stricmp(sub, "robokid") == 0 || _stricmp(sub, "ninjakd2") == 0
			|| _stricmp(sub, "mnight") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN;
		} else if (_stricmp(sub, "battlantis") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPL2;
		} else if (_stricmp(sub, "gunbird") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_YM2610;
		} else if (_stricmp(sub, "scontra") == 0 || _stricmp(sub, "thundercross") == 0
			|| _stricmp(sub, "crimfght") == 0 || _stricmp(sub, "twin16") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(sub, "68k2") == 0) {
			/* Alpha 68K-II: Z80 + YM2203 + YM2413 + DAC (MAME alpha68k). */
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPLL;
		} else if (_stricmp(sub, "hcastle") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPL2;
		} else if (_stricmp(sub, "salamander") == 0 || _stricmp(sub, "gx400") == 0
			|| _stricmp(sub, "nemesis") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		} else if (_stricmp(sub, "tecmo16") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OKI6295;
		} else if (_stricmp(sub, "taitosj") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		} else if (_stricmp(sub, "m72") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_IREM_DAC;
		} else if (_stricmp(sub, "m92") == 0) {
			/* Sound CPU is a NEC V35 — no core, chips listed for display. */
			ge->cpuId = CEMU_CPU_V30;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_GA20;
		} else if ((_stricmp(sub, "system1") == 0 || _stricmp(sub, "system2") == 0)
			&& _stricmp(plat, "sega") == 0) {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_SN76489;
		} else if (_stricmp(plat, "atari") == 0
			&& (_stricmp(sub, "system1") == 0 || _stricmp(sub, "atarisy1") == 0)) {
			ge->cpuId = CEMU_CPU_Z80; /* sound CPU is M6502; id is display-only */
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		} else if (_stricmp(sub, "megasys1") == 0) {
			/* Sound CPU is a second 68000; only the F3 board runs Musashi. */
			ge->cpuId = CEMU_CPU_M68000;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OKI6295;
		} else if (_stricmp(plat, "dataeast") == 0) {
			/* Data East sound CPU is a HuC6280 on cninja-class boards. */
			ge->cpuId = CEMU_CPU_H6280;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OKI6295;
		} else {
			ge->cpuId = CEMU_CPU_Z80;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
		}
	}
	else if (_stricmp(plat, "msx") == 0) {
		ge->cpuId = CEMU_CPU_Z80;
		ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPLL;
	}
	else if (_stricmp(plat, "pcatdos") == 0 || _stricmp(plat, "pcat") == 0) {
		ge->cpuId = CEMU_CPU_I286;
		if (_stricmp(sub, "opl3") == 0)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPL3;
		else if (_stricmp(sub, "adlib") == 0 || _stricmp(sub, "opl2") == 0
			|| _stricmp(sub, "opl") == 0
			|| _stricmp(sub, "soundblaster16") == 0 || _stricmp(sub, "sb16") == 0
			|| _stricmp(sub, "soundblaster") == 0 || _stricmp(sub, "sbpro") == 0
			|| _stricmp(sub, "sb") == 0)
			/* SB16 in this emu is YM3812 (SBP2FM/AdLib ports); not YMF262. */
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPL2;
	}
	else if (_stricmp(plat, "x1") == 0
		|| _stricmp(ge->dataDir, "x1") == 0
		|| _stricmp(sub, "x1") == 0 || _stricmp(sub, "x1psg") == 0) {
		ge->cpuId = CEMU_CPU_Z80;
		if (_stricmp(sub, "psg") == 0 || _stricmp(sub, "x1psg") == 0) {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		} else {
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPM;
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		}
	}
	else if (_stricmp(plat, "fm7") == 0 || _stricmp(plat, "fm77av") == 0
		|| _stricmp(plat, "mucomfm") == 0
		|| _stricmp(ge->dataDir, "fm7") == 0) {
		ge->cpuId = CEMU_CPU_M6809;
		/* mucomfm = Falcom Ys FM-7 family; *_fm7 / type=ys → PSG, else OPN. */
		int psg = 0;
		if (_stricmp(plat, "fm7") == 0 || _stricmp(sub, "psg") == 0
			|| _stricmp(sub, "ys") == 0)
			psg = 1;
		if (ge->archive[0]) {
			const size_t n = strlen(ge->archive);
			if (n >= 4 && _stricmp(ge->archive + n - 4, "_fm7") == 0)
				psg = 1;
			if (n >= 5 && _stricmp(ge->archive + n - 5, "_fmav") == 0)
				psg = 0;
		}
		if (psg)
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_AY;
		else
			ge->chipIds[ge->chipCount++] = CEMU_CHIP_OPN; /* fm77av / OPN */
	}
	else if (_stricmp(plat, "nes") == 0) {
		ge->cpuId = CEMU_CPU_V30;
	}
}

static int CEmuCatalogEnsureCapacity(CEmuCatalog* cat, int need)
{
	if (!cat || need <= cat->capacity) return 1;
	int nc = cat->capacity ? cat->capacity : 256;
	while (nc < need) {
		if (nc >= CEMU_CATALOG_MAX) break;
		int next = nc * 2;
		if (next < nc || next > CEMU_CATALOG_MAX)
			next = CEMU_CATALOG_MAX;
		nc = next;
	}
	if (nc < need) return 0;
	CEmuGameEntry** neu = (CEmuGameEntry**)realloc(cat->entry, (size_t)nc * sizeof(CEmuGameEntry*));
	if (!neu) return 0;
	cat->entry = neu;
	cat->capacity = nc;
	return 1;
}

static int CEmuCatalogPushOwned(CEmuCatalog* cat, CEmuGameEntry* slot)
{
	if (!cat || !slot || cat->count >= CEMU_CATALOG_MAX) return 0;
	if (!CEmuCatalogEnsureCapacity(cat, cat->count + 1)) return 0;
	cat->entry[cat->count++] = slot;
	return 1;
}

static int CEmuRomTypePriority(const char* type)
{
	if (!type || !type[0]) return 0;
	/* Keep shell/glue when romlist overflows (fc98v12 has 60+ song files). */
	if (_stricmp(type, "shell") == 0) return 50;
	if (_stricmp(type, "code") == 0) return 40;
	/* X68k Human68k .X (OPMDRV/ADV) — same weight as code. */
	if (_stricmp(type, "x") == 0) return 40;
	if (_stricmp(type, "file") == 0) return 35;
	if (_stricmp(type, "conin") == 0) return 34;
	if (_stricmp(type, "bgm") == 0) return 30;
	if (_stricmp(type, "voice") == 0) return 25;
	if (_stricmp(type, "adpcm") == 0) return 10;
	return 15;
}

static int CEmuRomTryPush(CEmuGameBuild* ge, const CEmuRomEntry* rom)
{
	if (!ge || !rom) return 0;
	if (ge->romCount < CEMU_ROM_MAX) {
		ge->rom[ge->romCount++] = *rom;
		return 1;
	}
	int worst = -1;
	int worstPri = 1000;
	for (int i = 0; i < ge->romCount; i++) {
		const int p = CEmuRomTypePriority(ge->rom[i].type);
		if (p < worstPri) {
			worstPri = p;
			worst = i;
		}
	}
	const int np = CEmuRomTypePriority(rom->type);
	if (worst >= 0 && np > worstPri) {
		ge->rom[worst] = *rom;
		return 1;
	}
	return 0;
}

static void CEmuCatalogParseGameBlock(CEmuCatalog* cat, const char* block, const char* dataDirHint)
{
	if (!cat || !block) return;
	CEmuGameBuild* ge = CEmuBuildBuf();
	if (!ge) return;
	memset(ge, 0, sizeof(*ge));
	if (dataDirHint)
		strncpy_s(ge->dataDir, dataDirHint, _TRUNCATE);

	char nameS[CEMU_GAME_NAME * 2];
	char platS[CEMU_DRIVER_NAME];
	char subS[CEMU_DRIVER_TYPE];
	char archiveS[CEMU_ARCHIVE_NAME];

	CEmuTagText(block, "name", nameS, (int)sizeof(nameS));
	CEmuSjisToWide(nameS, ge->name, CEMU_GAME_NAME);

	char aliasS[CEMU_GAME_NAME * 2];
	CEmuTagText(block, "driveralias", aliasS, (int)sizeof(aliasS));
	CEmuSjisToWide(aliasS, ge->driverAlias, CEMU_GAME_NAME);

	char driverTag[256];
	const char* dp = CEmuStrStr(block, "<driver");
	if (dp) {
		const char* de = strchr(dp, '>');
		int n = de ? (int)(de - dp) + 1 : 0;
		if (n >= (int)sizeof(driverTag)) n = (int)sizeof(driverTag) - 1;
		memcpy(driverTag, dp, (size_t)n);
		driverTag[n] = 0;
		CEmuAttrValue(driverTag, "type", subS, (int)sizeof(subS));
		CEmuTagText(block, "driver", platS, (int)sizeof(platS));
		strncpy_s(ge->platform, platS, _TRUNCATE);
		if (_strnicmp(platS, "pc88", 4) == 0) {
			if (_stricmp(subS, "8801-10") == 0)
				strncpy_s(subS, "opna", _TRUNCATE);
			else if (_stricmp(subS, "asteka2") == 0
				|| _stricmp(subS, "xanadu") == 0
				|| _stricmp(subS, "xanadu2") == 0)
				strncpy_s(subS, "opn", _TRUNCATE);
		}
		strncpy_s(ge->subtype, subS, _TRUNCATE);
	}

	if (ge->platform[0]) {
		if (_strnicmp(ge->platform, "pc98", 4) == 0 || _stricmp(ge->platform, "pc98dos") == 0 || _stricmp(ge->platform, "pc9821") == 0 || _stricmp(ge->platform, "pc98vx") == 0)
			strncpy_s(ge->dataDir, "pc98", _TRUNCATE);
		else if (_strnicmp(ge->platform, "pc88", 4) == 0 || _stricmp(ge->platform, "pc88va") == 0
			|| _stricmp(ge->platform, "pc88vados") == 0 || _stricmp(ge->platform, "pc80sr") == 0)
			strncpy_s(ge->dataDir, "pc88", _TRUNCATE);
		else if (_stricmp(ge->platform, "x68k") == 0)
			strncpy_s(ge->dataDir, "x68k", _TRUNCATE);
		else if (_stricmp(ge->platform, "x1") == 0)
			strncpy_s(ge->dataDir, "x1", _TRUNCATE);
		else if (_stricmp(ge->platform, "pcatdos") == 0 || _stricmp(ge->platform, "pcat") == 0)
			strncpy_s(ge->dataDir, "pc", _TRUNCATE);
		else if (_stricmp(ge->platform, "fmtowns") == 0 || _stricmp(ge->platform, "fm77av") == 0
			|| _stricmp(ge->platform, "fm7") == 0 || _stricmp(ge->platform, "mucomfm") == 0)
			strncpy_s(ge->dataDir, (_stricmp(ge->platform, "fmtowns") == 0) ? "fmtowns" : "fm7", _TRUNCATE);
		else if (_stricmp(ge->platform, "msx") == 0)
			strncpy_s(ge->dataDir, "msx", _TRUNCATE);
		else if (_stricmp(ge->subtype, "sg1000") == 0 || _stricmp(ge->platform, "sg1000") == 0)
			strncpy_s(ge->dataDir, "sc3000", _TRUNCATE);
		else if (_strnicmp(ge->platform, "capcom", 6) == 0
			|| _stricmp(ge->platform, "sega") == 0
			|| _stricmp(ge->platform, "videosystem") == 0
			|| _stricmp(ge->platform, "namco") == 0
			|| _strnicmp(ge->platform, "konami", 6) == 0
			|| _stricmp(ge->platform, "taito") == 0
			|| _stricmp(ge->platform, "irem") == 0
			|| _stricmp(ge->platform, "neogeo") == 0
			|| _stricmp(ge->platform, "dataeast") == 0
			|| _strnicmp(ge->subtype, "cps", 3) == 0
			|| _strnicmp(ge->subtype, "system16", 8) == 0
			|| _strnicmp(ge->subtype, "system18", 8) == 0
			|| _strnicmp(ge->subtype, "system24", 8) == 0
			|| _strnicmp(ge->subtype, "system32", 8) == 0
			|| _stricmp(ge->subtype, "outrun") == 0
			|| _stricmp(ge->subtype, "aburner") == 0
			|| _stricmp(ge->subtype, "sharrier") == 0
			|| _stricmp(ge->subtype, "hangon") == 0
			|| _stricmp(ge->subtype, "toutrun") == 0
			|| _stricmp(ge->subtype, "aerofgt") == 0
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
			|| _stricmp(ge->platform, "tehkan") == 0
			|| _stricmp(ge->platform, "upl") == 0
			|| _stricmp(ge->platform, "alpha") == 0
			|| _stricmp(ge->platform, "yunsung") == 0
			|| _stricmp(ge->platform, "athena") == 0
			|| _stricmp(ge->platform, "atlus") == 0
			|| _stricmp(ge->platform, "kaneko") == 0
			|| _stricmp(ge->platform, "raizing") == 0
			|| _stricmp(ge->platform, "eighting") == 0
			|| _stricmp(ge->platform, "allumer") == 0
			|| _stricmp(ge->platform, "atari") == 0
			|| _stricmp(ge->platform, "bootleg") == 0
			|| _stricmp(ge->platform, "deniam") == 0
			|| _stricmp(ge->platform, "mitchell") == 0
			|| _stricmp(ge->platform, "seta") == 0
			|| _stricmp(ge->platform, "fuuki") == 0
			|| _stricmp(ge->platform, "dooyong") == 0
			|| _stricmp(ge->platform, "tatsumi") == 0
			|| _stricmp(ge->platform, "tad") == 0
			|| _stricmp(ge->platform, "marble") == 0
			|| _stricmp(ge->platform, "technosoft") == 0
			|| _stricmp(ge->platform, "easttechnology") == 0
			|| _stricmp(ge->platform, "universal") == 0
			|| _stricmp(ge->platform, "nintendo") == 0
			|| _stricmp(ge->platform, "sunsoft") == 0
			|| _stricmp(ge->platform, "success") == 0
			|| _stricmp(ge->platform, "f2system") == 0
			|| _stricmp(ge->subtype, "megasys1") == 0
			|| _stricmp(ge->subtype, "system1") == 0
			|| _stricmp(ge->subtype, "system2") == 0
			|| _stricmp(ge->subtype, "m72") == 0
			|| _stricmp(ge->subtype, "m92") == 0
			|| _stricmp(ge->subtype, "m62") == 0
			|| _stricmp(ge->subtype, "scramble") == 0
			|| _stricmp(ge->subtype, "scobra") == 0
			|| _stricmp(ge->subtype, "frogger") == 0
			|| _stricmp(ge->subtype, "timeplt") == 0
			|| _stricmp(ge->subtype, "pooyan") == 0
			|| _stricmp(ge->subtype, "jungler") == 0
			|| _stricmp(ge->subtype, "circusc") == 0
			|| _stricmp(ge->subtype, "locomotn") == 0
			|| _stricmp(ge->subtype, "gx400") == 0
			|| _stricmp(ge->subtype, "buggychl") == 0
			|| _stricmp(ge->subtype, "ddragon2") == 0
			|| _stricmp(ge->subtype, "taitosj") == 0)
			strncpy_s(ge->dataDir, "ac", _TRUNCATE);
		else if (dataDirHint && dataDirHint[0])
			strncpy_s(ge->dataDir, dataDirHint, _TRUNCATE);
	} else if (dataDirHint && dataDirHint[0]) {
		strncpy_s(ge->dataDir, dataDirHint, _TRUNCATE);
	}

	const char* rl = CEmuStrStr(block, "<romlist");
	if (rl) {
		char rlTag[256];
		const char* re = strchr(rl, '>');
		int n = re ? (int)(re - rl) + 1 : 0;
		if (n >= (int)sizeof(rlTag)) n = (int)sizeof(rlTag) - 1;
		memcpy(rlTag, rl, (size_t)n);
		rlTag[n] = 0;
		CEmuAttrValue(rlTag, "archive", archiveS, (int)sizeof(archiveS));
		strncpy_s(ge->archive, archiveS, _TRUNCATE);
		for (const char* rp = block; ; ) {
			rp = CEmuStrStr(rp, "<rom");
			if (!rp) break;
			if (_strnicmp(rp, "<romlist", 8) == 0) {
				rp += 8;
				continue;
			}
			const char* re2 = strchr(rp, '>');
			if (!re2) break;
			char rt[128];
			int tn = (int)(re2 - rp) + 1;
			if (tn >= (int)sizeof(rt)) tn = (int)sizeof(rt) - 1;
			memcpy(rt, rp, (size_t)tn);
			rt[tn] = 0;
			CEmuRomEntry rom;
			memset(&rom, 0, sizeof(rom));
			CEmuAttrValue(rt, "type", rom.type, (int)sizeof(rom.type));
			{
				char offS[32];
				offS[0] = 0;
				CEmuAttrValue(rt, "offset", offS, (int)sizeof(offS));
				/* Signed: hoot uses offset="-1" for glue COM (silp_at.com).
				   strtoul("-1") → 0 and wrongly binds handle 0. */
				rom.offset = offS[0] ? (int)strtol(offS, NULL, 0) : 0;
			}
			const char* tc = re2 + 1;
			const char* te = CEmuStrStr(tc, "</rom>");
			if (te) {
				int ln = (int)(te - tc);
				if (ln >= CEMU_ROM_NAME) ln = CEMU_ROM_NAME - 1;
				memcpy(rom.name, tc, (size_t)ln);
				rom.name[ln] = 0;
				CEmuTrim(rom.name);
				/* HOOT sentinel for no-GTL is NULL; catalogs often write NONE. */
				if (_stricmp(rom.type, "shell") == 0 && rom.name[0]) {
					char* rp = rom.name;
					while (*rp) {
						if ((_strnicmp(rp, "NONE", 4) == 0)
							&& (rp[4] == 0 || rp[4] == ' ' || rp[4] == '\t')
							&& (rp == rom.name || rp[-1] == ' ' || rp[-1] == '\t')) {
							rp[0] = 'N'; rp[1] = 'U'; rp[2] = 'L'; rp[3] = 'L';
							rp += 4;
							continue;
						}
						rp++;
					}
				}
			}
			CEmuRomTryPush(ge, &rom);
			rp = re2 + 1;
		}
	}

	for (const char* op = block; ge->optCount < CEMU_OPTION_MAX; ) {
		op = CEmuStrStr(op, "<option");
		if (!op) break;
		if (_strnicmp(op, "<options", 8) == 0) {
			op += 8;
			continue;
		}
		const char* oe = strchr(op, '/');
		if (!oe || oe - op > 200) break;
		char ot[200];
		int on = (int)(oe - op);
		if (on >= (int)sizeof(ot)) on = (int)sizeof(ot) - 1;
		memcpy(ot, op, (size_t)on);
		ot[on] = 0;
		CEmuOptionEntry* o = &ge->opt[ge->optCount];
		CEmuAttrValue(ot, "name", o->name, (int)sizeof(o->name));
		CEmuAttrValue(ot, "value", o->value, (int)sizeof(o->value));
		if (o->name[0]) ge->optCount++;
		op = oe + 1;
	}

	for (const char* tp = block; ge->titleCount < CEMU_TITLE_MAX; ) {
		tp = CEmuFindTitleOpenTag(tp);
		if (!tp) break;
		const char* te = strchr(tp, '>');
		if (!te) break;
		char tt[128];
		int tn = (int)(te - tp) + 1;
		if (tn >= (int)sizeof(tt)) tn = (int)sizeof(tt) - 1;
		memcpy(tt, tp, (size_t)tn);
		tt[tn] = 0;
		CEmuTitleEntry* t = &ge->title[ge->titleCount];
		char codeS[32];
		CEmuAttrValue(tt, "code", codeS, (int)sizeof(codeS));
		unsigned long c = strtoul(codeS, NULL, 0);
		t->code = (unsigned)c;
		const char* tc = te + 1;
		const char* tx = CEmuStrStr(tc, "</title>");
		if (tx) {
			char lab[CEMU_GAME_NAME * 2];
			int ln = (int)(tx - tc);
			if (ln >= (int)sizeof(lab)) ln = (int)sizeof(lab) - 1;
			memcpy(lab, tc, (size_t)ln);
			lab[ln] = 0;
			CEmuTrim(lab);
			CEmuDecodeXmlEntities(lab);
			if (lab[0] && lab[0] != '<') {
				CEmuSjisToWide(lab, t->label, CEMU_GAME_NAME);
				ge->titleCount++;
			}
		}
		tp = tx ? tx + 8 : te + 1;
	}

	for (const char* rp = block; ge->titleCount < CEMU_TITLE_MAX; ) {
		rp = CEmuStrStr(rp, "<range");
		if (!rp) break;
		const char* re = strchr(rp, '>');
		if (!re) break;
		char rt[256];
		int rn = (int)(re - rp) + 1;
		if (rn >= (int)sizeof(rt)) rn = (int)sizeof(rt) - 1;
		memcpy(rt, rp, (size_t)rn);
		rt[rn] = 0;
		char minS[32], maxS[32];
		CEmuAttrValue(rt, "min", minS, (int)sizeof(minS));
		CEmuAttrValue(rt, "max", maxS, (int)sizeof(maxS));
		unsigned minC = CEmuParseUnum(minS);
		unsigned maxC = CEmuParseUnum(maxS);
		const char* tc = re + 1;
		const char* tx = CEmuStrStr(tc, "</range>");
		char fmt[CEMU_GAME_NAME * 2];
		fmt[0] = 0;
		if (tx) {
			int ln = (int)(tx - tc);
			if (ln >= (int)sizeof(fmt)) ln = (int)sizeof(fmt) - 1;
			memcpy(fmt, tc, (size_t)ln);
			fmt[ln] = 0;
			CEmuTrim(fmt);
		}
		if (maxC >= minC && (maxC - minC) < 512) {
			for (unsigned c = minC; c <= maxC && ge->titleCount < CEMU_TITLE_MAX; c++)
				CEmuPushFormattedTitle(ge, c, fmt);
		}
		rp = tx ? tx + 8 : re + 1;
	}

	if (!ge->archive[0]) return;
	CEmuGameEntry* slot = CEmuGameEntryFromBuild(ge);
	if (!slot) return;
	CEmuCatalogAssignHwIds(slot);
	if (!CEmuCatalogPushOwned(cat, slot))
		CEmuGameEntryFree(slot);
}

int CEmuCatalogParseBuffer(CEmuCatalog* cat, const char* xmlText, const char* dataDirHint)
{
	if (!cat || !xmlText || !xmlText[0]) return 0;
	const int before = cat->count;
	for (const char* p = xmlText; ; ) {
		const char* gs = CEmuStrStr(p, "<game");
		if (!gs) break;
		const char* ge = CEmuStrStr(gs, "</game>");
		if (!ge) break;
		ge += 7;
		int n = (int)(ge - gs);
		char* block = (char*)malloc((size_t)n + 1);
		if (block) {
			memcpy(block, gs, (size_t)n);
			block[n] = 0;
			CEmuCatalogParseGameBlock(cat, block, dataDirHint);
			free(block);
		}
		p = ge;
	}
	return cat->count - before;
}

int CEmuCatalogParseFile(CEmuCatalog* cat, const wchar_t* xmlPath, const char* dataDirHint)
{
	if (!cat || !xmlPath) return 0;
	HANDLE h = CreateFileW(xmlPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > 32 * 1024 * 1024) {
		CloseHandle(h);
		return 0;
	}
	const DWORD sz = (DWORD)li.QuadPart;
	char* buf = (char*)malloc((size_t)sz + 4);
	if (!buf) {
		CloseHandle(h);
		return 0;
	}
	DWORD rd = 0;
	if (!ReadFile(h, buf, sz, &rd, NULL) || rd != sz) {
		free(buf);
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	buf[sz] = 0;
	const int added = CEmuCatalogParseBuffer(cat, buf, dataDirHint);
	free(buf);
	return added;
}

static int CEmuCatalogLoadXmlDir(CEmuCatalog* cat, const wchar_t* dir, const char* dataDirHint)
{
	if (!cat || !dir) return 0;
	wchar_t spec[MAX_PATH];
	_snwprintf_s(spec, _TRUNCATE, L"%s\\*.xml", dir);
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(spec, &fd);
	if (hFind == INVALID_HANDLE_VALUE) return 0;
	int total = 0;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		wchar_t path[MAX_PATH];
		_snwprintf_s(path, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
		total += CEmuCatalogParseFile(cat, path, dataDirHint);
	} while (FindNextFileW(hFind, &fd));
	FindClose(hFind);
	return total;
}

/* Load every immediate subdirectory of root that contains at least one *.xml
   (xml / xml2 / xml3 / custom / …). Also loads *.xml sitting directly in root. */
static int CEmuCatalogLoadAllXmlDirs(CEmuCatalog* cat, const wchar_t* root)
{
	if (!cat || !root || !root[0]) return 0;
	int total = 0;
	total += CEmuCatalogLoadXmlDir(cat, root, NULL);
	wchar_t spec[MAX_PATH];
	_snwprintf_s(spec, _TRUNCATE, L"%s\\*", root);
	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(spec, &fd);
	if (hFind == INVALID_HANDLE_VALUE) return total;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0
			|| (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		wchar_t sub[MAX_PATH];
		_snwprintf_s(sub, _TRUNCATE, L"%s\\%s", root, fd.cFileName);
		wchar_t probe[MAX_PATH];
		_snwprintf_s(probe, _TRUNCATE, L"%s\\*.xml", sub);
		WIN32_FIND_DATAW xf;
		HANDLE hx = FindFirstFileW(probe, &xf);
		if (hx == INVALID_HANDLE_VALUE) continue;
		FindClose(hx);
		total += CEmuCatalogLoadXmlDir(cat, sub, NULL);
	} while (FindNextFileW(hFind, &fd));
	FindClose(hFind);
	return total;
}

static void CEmuCatalogProgress(CEmuCatalogProgressFn fn, void* user, int pos, int max)
{
	if (fn) fn(pos, max, user);
}

void CEmuCatalogGetExeArcdataPath(wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	wchar_t exe[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
	wchar_t* slash = wcsrchr(exe, L'\\');
	if (slash) *(slash + 1) = 0;
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%sarcdata.zip", exe);
}

static int CEmuCatalogLocalAppDir(wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	wchar_t base[MAX_PATH] = {};
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)) || !base[0]) {
		DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) {
			if (!GetTempPathW(MAX_PATH, base) || !base[0]) return 0;
		}
	}
	wchar_t app[MAX_PATH];
	_snwprintf_s(app, _TRUNCATE, L"%s\\oggYSED", base);
	CreateDirectoryW(app, NULL);
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%s\\oggYSED\\cemucatalog", base);
	CreateDirectoryW(out, NULL);
	return out[0] != 0;
}

static int CEmuCatalogCacheFilePath(wchar_t* out, int outChars)
{
	wchar_t dir[MAX_PATH];
	if (!CEmuCatalogLocalAppDir(dir, MAX_PATH)) return 0;
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%s\\arcdata.cache", dir);
	return 1;
}

void CEmuCatalogInvalidateCache(void)
{
	wchar_t path[MAX_PATH];
	if (!CEmuCatalogCacheFilePath(path, MAX_PATH)) return;
	DeleteFileW(path);
}

struct CEmuCatalogFp {
	ULONGLONG zipSize;
	FILETIME zipMtime;
	ULONGLONG zipPathHash; /* 旧キャッシュ互換フィールド。照合・書込とも 0 */
	DWORD flags; /* 1=xml 2=xml2 4=hootXml | 0x200=parse v3 zip-identity */
};

/* 0x300: AttrValue accepts whitespace around '=' (xml2 spaced archives). */
enum { CEMU_CAT_FP_PARSE_VER = 0x300 };

static int CEmuCatalogMakeFp(const wchar_t* arcZip, const wchar_t* dataRoot,
	const wchar_t* parent, CEmuCatalogFp* fp)
{
	if (!fp) return 0;
	memset(fp, 0, sizeof(*fp));
	fp->flags |= CEMU_CAT_FP_PARSE_VER;
	if (arcZip && arcZip[0]) {
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (GetFileAttributesExW(arcZip, GetFileExInfoStandard, &fad)) {
			ULARGE_INTEGER sz;
			sz.LowPart = fad.nFileSizeLow;
			sz.HighPart = fad.nFileSizeHigh;
			fp->zipSize = sz.QuadPart;
			fp->zipMtime = fad.ftLastWriteTime;
		}
	}
	wchar_t probe[MAX_PATH];
	if (dataRoot && dataRoot[0]) {
		_snwprintf_s(probe, _TRUNCATE, L"%s\\xml", dataRoot);
		if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) fp->flags |= 1;
		_snwprintf_s(probe, _TRUNCATE, L"%s\\xml2", dataRoot);
		if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) fp->flags |= 2;
		_snwprintf_s(probe, _TRUNCATE, L"%s\\hoot.xml", dataRoot);
		if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) fp->flags |= 4;
	}
	if (parent && parent[0]) {
		_snwprintf_s(probe, _TRUNCATE, L"%s\\xml", parent);
		if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) fp->flags |= 1;
		_snwprintf_s(probe, _TRUNCATE, L"%s\\xml2", parent);
		if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES) fp->flags |= 2;
	}
	return 1;
}

static void CEmuCatalogParentOf(const wchar_t* dataRoot, wchar_t* parent, int parentChars)
{
	if (!parent || parentChars <= 0) return;
	parent[0] = 0;
	if (!dataRoot || !dataRoot[0]) return;
	wcsncpy_s(parent, (size_t)parentChars, dataRoot, _TRUNCATE);
	wchar_t* slash = wcsrchr(parent, L'\\');
	if (!slash) slash = wcsrchr(parent, L'/');
	if (slash) *slash = 0;
}

/* arcdata.zip は実行ファイル隣のみ（data / 親ディレクトリは見ない）。 */
static int CEmuCatalogChooseArc(wchar_t* chosenArc, int chosenChars)
{
	if (!chosenArc || chosenChars <= 0) return 0;
	chosenArc[0] = 0;
	wchar_t arc[MAX_PATH];
	CEmuCatalogGetExeArcdataPath(arc, MAX_PATH);
	if (!arc[0] || GetFileAttributesW(arc) == INVALID_FILE_ATTRIBUTES)
		return 0;
	wcsncpy_s(chosenArc, (size_t)chosenChars, arc, _TRUNCATE);
	return 1;
}

static int CEmuWriteBytes(HANDLE h, const void* p, DWORD n)
{
	DWORD wr = 0;
	return WriteFile(h, p, n, &wr, NULL) && wr == n;
}

static int CEmuReadBytes(HANDLE h, void* p, DWORD n)
{
	DWORD rd = 0;
	return ReadFile(h, p, n, &rd, NULL) && rd == n;
}

static int CEmuCatalogSaveCache(const CEmuCatalog* cat, const CEmuCatalogFp* fp,
	CEmuCatalogProgressFn progress, void* progressUser)
{
	if (!cat || !fp || cat->count <= 0) return 0;
	wchar_t path[MAX_PATH], tmp[MAX_PATH];
	if (!CEmuCatalogCacheFilePath(path, MAX_PATH)) return 0;
	_snwprintf_s(tmp, _TRUNCATE, L"%s.part", path);
	DeleteFileW(tmp);
	HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	static const char kMagic[8] = { 'C','E','M','C', 2, 0, 0, 0 };
	int ok = CEmuWriteBytes(h, kMagic, 8)
		&& CEmuWriteBytes(h, fp, (DWORD)sizeof(*fp));
	DWORD count = (DWORD)cat->count;
	ok = ok && CEmuWriteBytes(h, &count, sizeof(count));
	for (int i = 0; ok && i < cat->count; i++) {
		const CEmuGameEntry* e = cat->entry[i];
		if (!e) { ok = 0; break; }
		ok = CEmuWriteBytes(h, e->name, sizeof(e->name))
			&& CEmuWriteBytes(h, e->driverAlias, sizeof(e->driverAlias))
			&& CEmuWriteBytes(h, e->platform, sizeof(e->platform))
			&& CEmuWriteBytes(h, e->subtype, sizeof(e->subtype))
			&& CEmuWriteBytes(h, e->dataDir, sizeof(e->dataDir))
			&& CEmuWriteBytes(h, e->archive, sizeof(e->archive));
		DWORD rc = (DWORD)e->romCount, oc = (DWORD)e->optCount, tc = (DWORD)e->titleCount;
		ok = ok && CEmuWriteBytes(h, &rc, 4) && CEmuWriteBytes(h, &oc, 4) && CEmuWriteBytes(h, &tc, 4);
		if (ok && rc) ok = CEmuWriteBytes(h, e->rom, (DWORD)(sizeof(CEmuRomEntry) * rc));
		if (ok && oc) ok = CEmuWriteBytes(h, e->opt, (DWORD)(sizeof(CEmuOptionEntry) * oc));
		if (ok && tc) ok = CEmuWriteBytes(h, e->title, (DWORD)(sizeof(CEmuTitleEntry) * tc));
		ok = ok && CEmuWriteBytes(h, &e->cpuId, sizeof(e->cpuId))
			&& CEmuWriteBytes(h, e->chipIds, sizeof(e->chipIds))
			&& CEmuWriteBytes(h, &e->chipCount, sizeof(e->chipCount));
		if ((i & 255) == 0)
			CEmuCatalogProgress(progress, progressUser, i, cat->count);
	}
	CloseHandle(h);
	if (!ok) {
		DeleteFileW(tmp);
		return 0;
	}
	DeleteFileW(path);
	if (!MoveFileW(tmp, path)) {
		if (!CopyFileW(tmp, path, FALSE)) {
			DeleteFileW(tmp);
			return 0;
		}
		DeleteFileW(tmp);
	}
	CEmuCatalogProgress(progress, progressUser, cat->count, cat->count);
	return 1;
}

/* 照合は size+flags のみ。mtime はコピー／別パス同内容で毎回ズレるため使わない。
   （実更新はサイズ変化、または DL 成功時の InvalidateCache で拾う） */
static int CEmuCatalogFpMatches(const CEmuCatalogFp* a, const CEmuCatalogFp* b)
{
	if (!a || !b) return 0;
	return a->zipSize == b->zipSize && a->flags == b->flags;
}

static int CEmuCatalogLoadCache(CEmuCatalog* cat, const CEmuCatalogFp* want,
	CEmuCatalogProgressFn progress, void* progressUser)
{
	if (!cat || !want) return 0;
	wchar_t path[MAX_PATH];
	if (!CEmuCatalogCacheFilePath(path, MAX_PATH)) return 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	char magic[8];
	CEmuCatalogFp fp = {};
	DWORD count = 0;
	int ok = CEmuReadBytes(h, magic, 8)
		&& magic[0] == 'C' && magic[1] == 'E' && magic[2] == 'M' && magic[3] == 'C' && magic[4] == 2
		&& CEmuReadBytes(h, &fp, (DWORD)sizeof(fp))
		&& CEmuCatalogFpMatches(&fp, want)
		&& CEmuReadBytes(h, &count, 4)
		&& count > 0 && count <= (DWORD)CEMU_CATALOG_MAX;
	if (!ok) {
		CloseHandle(h);
		return 0;
	}
	CEmuCatalogClear(cat);
	for (DWORD i = 0; ok && i < count; i++) {
		CEmuGameEntry* e = (CEmuGameEntry*)calloc(1, sizeof(CEmuGameEntry));
		if (!e) { ok = 0; break; }
		ok = CEmuReadBytes(h, e->name, sizeof(e->name))
			&& CEmuReadBytes(h, e->driverAlias, sizeof(e->driverAlias))
			&& CEmuReadBytes(h, e->platform, sizeof(e->platform))
			&& CEmuReadBytes(h, e->subtype, sizeof(e->subtype))
			&& CEmuReadBytes(h, e->dataDir, sizeof(e->dataDir))
			&& CEmuReadBytes(h, e->archive, sizeof(e->archive));
		DWORD rc = 0, oc = 0, tc = 0;
		ok = ok && CEmuReadBytes(h, &rc, 4) && CEmuReadBytes(h, &oc, 4) && CEmuReadBytes(h, &tc, 4);
		if (ok && rc > (DWORD)CEMU_ROM_MAX) ok = 0;
		if (ok && oc > (DWORD)CEMU_OPTION_MAX) ok = 0;
		if (ok && tc > (DWORD)CEMU_TITLE_MAX) ok = 0;
		if (ok && rc) {
			e->rom = (CEmuRomEntry*)malloc(sizeof(CEmuRomEntry) * rc);
			ok = e->rom && CEmuReadBytes(h, e->rom, (DWORD)(sizeof(CEmuRomEntry) * rc));
			if (ok) e->romCount = (int)rc;
		}
		if (ok && oc) {
			e->opt = (CEmuOptionEntry*)malloc(sizeof(CEmuOptionEntry) * oc);
			ok = e->opt && CEmuReadBytes(h, e->opt, (DWORD)(sizeof(CEmuOptionEntry) * oc));
			if (ok) e->optCount = (int)oc;
		}
		if (ok && tc) {
			e->title = (CEmuTitleEntry*)malloc(sizeof(CEmuTitleEntry) * tc);
			ok = e->title && CEmuReadBytes(h, e->title, (DWORD)(sizeof(CEmuTitleEntry) * tc));
			if (ok) e->titleCount = (int)tc;
		}
		ok = ok && CEmuReadBytes(h, &e->cpuId, sizeof(e->cpuId))
			&& CEmuReadBytes(h, e->chipIds, sizeof(e->chipIds))
			&& CEmuReadBytes(h, &e->chipCount, sizeof(e->chipCount));
		if (ok) {
			/* Older caches may still contain literal "&amp;" from XML. */
			CEmuDecodeXmlEntitiesW(e->name);
			CEmuDecodeXmlEntitiesW(e->driverAlias);
			for (int ti = 0; ti < e->titleCount; ++ti)
				CEmuDecodeXmlEntitiesW(e->title[ti].label);
		}
		if (!ok || !CEmuCatalogPushOwned(cat, e)) {
			CEmuGameEntryFree(e);
			ok = 0;
			break;
		}
		if ((i & 255) == 0)
			CEmuCatalogProgress(progress, progressUser, (int)i, (int)count);
	}
	CloseHandle(h);
	if (!ok) {
		CEmuCatalogClear(cat);
		return 0;
	}
	CEmuCatalogProgress(progress, progressUser, (int)count, (int)count);
	return 1;
}

static int CEmuCatalogLoadArcdataZip(CEmuCatalog* cat, const wchar_t* zipPath,
	CEmuCatalogProgressFn progress, void* progressUser)
{
	if (!cat || !zipPath) return 0;
	zlib_filefunc64_def ffunc;
	fill_win32_filefunc64W(&ffunc);
	unzFile uf = unzOpen2_64(zipPath, &ffunc);
	if (!uf) return 0;

	int xmlN = 0;
	if (unzGoToFirstFile(uf) == UNZ_OK) {
		do {
			unz_file_info64 fi;
			char fn[512];
			if (unzGetCurrentFileInfo64(uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK)
				continue;
			const size_t fl = strlen(fn);
			if (fl >= 5 && _stricmp(fn + fl - 4, ".xml") == 0)
				xmlN++;
		} while (unzGoToNextFile(uf) == UNZ_OK);
	}

	int total = 0, done = 0;
	if (unzGoToFirstFile(uf) == UNZ_OK) {
		do {
			unz_file_info64 fi;
			char fn[512];
			if (unzGetCurrentFileInfo64(uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK)
				continue;
			const size_t fl = strlen(fn);
			if (fl < 5) continue;
			if (_stricmp(fn + fl - 4, ".xml") != 0) continue;
			if (unzOpenCurrentFile(uf) != UNZ_OK) continue;
			if (fi.uncompressed_size == 0 || fi.uncompressed_size > 16 * 1024 * 1024) {
				unzCloseCurrentFile(uf);
				continue;
			}
			char* buf = (char*)malloc((size_t)fi.uncompressed_size + 4);
			if (buf) {
				int rd = unzReadCurrentFile(uf, buf, (unsigned)fi.uncompressed_size);
				if (rd == (int)fi.uncompressed_size) {
					buf[fi.uncompressed_size] = 0;
					/* dataDir from xml path only when unambiguous; else NULL
					   and platform tags in the block set ac/pc88/…. */
					const char* dd = NULL;
					if (CEmuStrStr(fn, "pc88")) dd = "pc88";
					else if (CEmuStrStr(fn, "pc98") || CEmuStrStr(fn, "pc9821"))
						dd = "pc98";
					else if (CEmuStrStr(fn, "x68k") || CEmuStrStr(fn, "x68000"))
						dd = "x68k";
					else if (CEmuStrStr(fn, "/msx") || CEmuStrStr(fn, "\\msx")
						|| _strnicmp(fn, "msx", 3) == 0)
						dd = "msx";
					total += CEmuCatalogParseBuffer(cat, buf, dd);
				}
				free(buf);
			}
			unzCloseCurrentFile(uf);
			done++;
			CEmuCatalogProgress(progress, progressUser, done, xmlN > 0 ? xmlN : 1);
		} while (unzGoToNextFile(uf) == UNZ_OK);
	}
	unzClose(uf);
	return total;
}

int CEmuCatalogCacheIsCurrent(const wchar_t* dataRoot)
{
	if (!dataRoot || !dataRoot[0]) return 0;
	wchar_t parent[MAX_PATH];
	CEmuCatalogParentOf(dataRoot, parent, MAX_PATH);
	wchar_t chosenArc[MAX_PATH] = {};
	CEmuCatalogChooseArc(chosenArc, MAX_PATH);
	CEmuCatalogFp want = {};
	CEmuCatalogMakeFp(chosenArc, dataRoot, parent, &want);

	wchar_t path[MAX_PATH];
	if (!CEmuCatalogCacheFilePath(path, MAX_PATH)) return 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	char magic[8];
	CEmuCatalogFp fp = {};
	DWORD count = 0;
	const int ok = CEmuReadBytes(h, magic, 8)
		&& magic[0] == 'C' && magic[1] == 'E' && magic[2] == 'M' && magic[3] == 'C' && magic[4] == 2
		&& CEmuReadBytes(h, &fp, (DWORD)sizeof(fp))
		&& CEmuCatalogFpMatches(&fp, &want)
		&& CEmuReadBytes(h, &count, 4)
		&& count > 0 && count <= (DWORD)CEMU_CATALOG_MAX;
	CloseHandle(h);
	return ok ? 1 : 0;
}

int CEmuCatalogLoadEx(CEmuCatalog* cat, const wchar_t* dataRoot,
	CEmuCatalogProgressFn progress, void* progressUser)
{
	if (!cat || !dataRoot || !dataRoot[0]) return 0;
	CEmuCatalogClear(cat);

	wchar_t parent[MAX_PATH];
	CEmuCatalogParentOf(dataRoot, parent, MAX_PATH);
	wchar_t chosenArc[MAX_PATH] = {};
	CEmuCatalogChooseArc(chosenArc, MAX_PATH);

	CEmuCatalogFp fp = {};
	CEmuCatalogMakeFp(chosenArc, dataRoot, parent, &fp);

	CEmuCatalogProgress(progress, progressUser, 0, 1);
	if (CEmuCatalogLoadCache(cat, &fp, progress, progressUser)) {
		cat->loaded = 1;
		return cat->count;
	}

	if (chosenArc[0])
		CEmuCatalogLoadArcdataZip(cat, chosenArc, progress, progressUser);

	/* Any directory under dataRoot (and its parent) that contains *.xml —
	   not only the historical "xml" / "xml2" names. Users add xml3/custom/…. */
	CEmuCatalogLoadAllXmlDirs(cat, dataRoot);
	if (parent[0])
		CEmuCatalogLoadAllXmlDirs(cat, parent);

	wchar_t hootXml[MAX_PATH];
	_snwprintf_s(hootXml, _TRUNCATE, L"%s\\hoot.xml", dataRoot);
	if (GetFileAttributesW(hootXml) != INVALID_FILE_ATTRIBUTES)
		CEmuCatalogParseFile(cat, hootXml, NULL);
	if (parent[0]) {
		_snwprintf_s(hootXml, _TRUNCATE, L"%s\\hoot.xml", parent);
		if (GetFileAttributesW(hootXml) != INVALID_FILE_ATTRIBUTES)
			CEmuCatalogParseFile(cat, hootXml, NULL);
	}

	if (cat->count > 0)
		CEmuCatalogSaveCache(cat, &fp, progress, progressUser);

	cat->loaded = 1;
	CEmuCatalogProgress(progress, progressUser, 1, 1);
	return cat->count;
}

int CEmuCatalogLoad(CEmuCatalog* cat, const wchar_t* dataRoot)
{
	return CEmuCatalogLoadEx(cat, dataRoot, NULL, NULL);
}

static int CEmuSubtypePreferRank(const char* subtype)
{
	/* Same archive often has OPNA then OPN (arcus2). Zip resolve must not
	   last-win to OPN — that loads PATCH2 in YM2203 mode → sparse regs / silence.
	   Also demote beep: cplay98/mars and frnunv98 otherwise pick BPLAY/PMDB over
	   FPLAY/PMD (beep ranked above opn when both shared "other"=15). Prefer opn
	   over bare 86 — PMD86 paths often need extra PCM drivers and stay silent. */
	if (!subtype || !subtype[0]) return 0;
	if (_stricmp(subtype, "opna") == 0) return 40;
	if (_stricmp(subtype, "opn") == 0) return 30;
	if (_stricmp(subtype, "opm") == 0) return 28;
	if (_stricmp(subtype, "opn2") == 0 || _stricmp(subtype, "opnb") == 0) return 25;
	if (_stricmp(subtype, "86") == 0 || _stricmp(subtype, "86+otomix2") == 0) return 20;
	if (_stricmp(subtype, "psg") == 0 || _stricmp(subtype, "x1psg") == 0) return 5;
	if (_stricmp(subtype, "beep") == 0) return 2;
	return 10;
}

static int CEmuGameHasOpt(const CEmuGameEntry* e, const char* name)
{
	if (!e || !name) return 0;
	for (int i = 0; i < e->optCount; i++) {
		if (_stricmp(e->opt[i].name, name) == 0)
			return 1;
	}
	return 0;
}

/* Score how many code/bgm/voice rom names resolve inside zipFs. */
static int CEmuCatalogZipRomHits(const CEmuGameEntry* e, const CEmuZipFs* zipFs)
{
	if (!e || !zipFs) return 0;
	int hits = 0;
	for (int i = 0; i < e->romCount; i++) {
		const char* t = e->rom[i].type;
		if (!t || !t[0]) continue;
		/* Include arcade PCM/ADPCM so Neo/CPS packs rank by real zip members. */
		if (_stricmp(t, "code") != 0 && _stricmp(t, "bgm") != 0
			&& _stricmp(t, "voice") != 0 && _stricmp(t, "song") != 0
			&& _stricmp(t, "prog") != 0 && _stricmp(t, "x") != 0
			&& _stricmp(t, "adpcma") != 0 && _stricmp(t, "adpcmb") != 0
			&& _stricmp(t, "adpcm") != 0 && _stricmp(t, "pcm") != 0
			&& _stricmp(t, "sample") != 0 && _stricmp(t, "sound") != 0
			&& _stricmp(t, "qsound") != 0 && _stricmp(t, "oki") != 0)
			continue;
		if (!e->rom[i].name[0]) continue;
		unsigned sz = 0;
		if (CEmuZipFsHas(zipFs, e->rom[i].name, &sz) && sz > 0)
			hits++;
	}
	return hits;
}

static int CEmuCatalogArchiveRank(const CEmuGameEntry* e, const CEmuZipFs* zipFs)
{
	if (!e) return -1000000;
	/* Wolfteam / similar: same archive ships FM (MU*) and midiout (MI*)
	   variants. Prefer the non-MIDI entry so 000_BOOT drives OPN, not E0Dx
	   MIDI UART traffic we don't bridge. */
	int rank = CEmuSubtypePreferRank(e->subtype) * 100000;
	/* Sharp X1 hard is OPM+AY only — never prefer catalog OPN twins (ishtar). */
	if ((_stricmp(e->platform, "x1") == 0 || _stricmp(e->dataDir, "x1") == 0)
		&& e->subtype[0] && _stricmp(e->subtype, "opn") == 0)
		rank -= 2500000;
	if (CEmuGameHasOpt(e, "midiout"))
		rank -= 800000;
	/* Wing destge/destjyo OPNA MCM1/WMC1 hang under DI during init; the OPN
	   sibling uses Y230 (same as working onryo OPN). Demote MCM1 so zip
	   resolve picks Y230. onryo OPNA (M.VA) is untouched. */
	for (int i = 0; i < e->romCount; i++) {
		if (_stricmp(e->rom[i].type, "code") != 0) continue;
		const char* nm = e->rom[i].name;
		if (!nm) continue;
		if (_stricmp(nm, "MCM1") == 0 || _stricmp(nm, "WMC1") == 0) {
			rank -= 2000000;
			break;
		}
		/* kbreed OPNA TRPSCR2 overlaps mdata@d200; OPN TRPSCR1 (digan-class)
		   does not and matches working barbatus/digan. Prefer OPN. */
		if (_stricmp(nm, "TRPSCR2.COM") == 0) {
			rank -= 2000000;
			break;
		}
		/* jesus2 OPNA (use_pcmx8) stays key-on mute; OPN sibling with the
		   same music.com path peaks. Demote pcmx8 OPNA so zip picks OPN. */
		if (_stricmp(nm, "music.com") == 0 && CEmuGameHasOpt(e, "use_pcmx8")) {
			rank -= 2000000;
			break;
		}
		/* bpoint88 OPNA (use_pcmx8+ADPCM) wanders DI; OPN sibling peaks. */
		if (_stricmp(nm, "MUCO3") == 0 && CEmuGameHasOpt(e, "use_pcmx8")) {
			rank -= 2000000;
			break;
		}
		/* scheme OPNA (MS0A) needs specialty init_pc=0x9000; OPN MD* sibling
		   plays under generic PATCH. */
		if (_stricmp(nm, "MS0A") == 0) {
			rank -= 2000000;
			break;
		}
		/* hydlide3 OPNA (BIOS@ED00) is mute; identical OPN sibling peaks. */
		if (_stricmp(nm, "BIOS") == 0 && e->rom[i].offset == 0xed00
			&& e->subtype[0] && _strnicmp(e->subtype, "opna", 4) == 0) {
			rank -= 2000000;
			break;
		}
		/* p1demo1 OPNA twin of OPN (same DEMO1A/DRIVER) stays silent. */
		if (_stricmp(nm, "DEMO1A") == 0
			&& e->subtype[0] && _strnicmp(e->subtype, "opna", 4) == 0) {
			rank -= 2000000;
			break;
		}
		/* wingsp88 OPNA lists musics.mac; OPN music.mac sibling is the
		   working GameArts path (zip ships both). */
		if (_stricmp(nm, "musics.mac") == 0) {
			rank -= 2000000;
			break;
		}
		/* af OPNA (opefc) stays silent; OPN sibling with same roms peaks. */
		if (_stricmp(nm, "opefc") == 0
			&& e->subtype[0] && _strnicmp(e->subtype, "opna", 4) == 0) {
			rank -= 2000000;
			break;
		}
		/* laptick 8801-10 (PATCH2+CAST+PROG): local zip ships PATCH (OPN). */
		if (_stricmp(nm, "PATCH2") == 0) {
			int hasCast = 0;
			for (int j = 0; j < e->romCount; j++) {
				if (_stricmp(e->rom[j].name, "CAST") == 0) { hasCast = 1; break; }
			}
			if (hasCast) {
				rank -= 2000000;
				break;
			}
		}
		/* bd98/bdp98 OPNA twin of OPN (BD_MAIN.COM) stays silent on OPNA. */
		if (_stricmp(nm, "BD_MAIN.COM") == 0
			&& e->subtype[0] && _strnicmp(e->subtype, "opna", 4) == 0) {
			rank -= 2000000;
			break;
		}
		/* gage98 OPNA pulls ADPCM.BIN; OPN sibling without it is preferred. */
		if (_stricmp(nm, "ADPCM.BIN") == 0) {
			rank -= 2000000;
			break;
		}
	}
	/* DOS file-list twins (not type=code): demote silent OPNA+PCM packs. */
	for (int i = 0; i < e->romCount; i++) {
		if (_stricmp(e->rom[i].type, "file") != 0) continue;
		const char* nm = e->rom[i].name;
		if (!nm) continue;
		/* gao1 SPB (PMDB2+PMDPCM/GAOGAO.PPC) stays silent; OPN PMD sibling peaks. */
		if (_stricmp(nm, "GAOGAO.PPC") == 0) {
			rank -= 2000000;
			break;
		}
	}
	/* xml vs xml2: same subtype, but rom names differ (MUS* vs DATA*).
	   Prefer the list that actually hits the local zip. */
	if (zipFs)
		rank += CEmuCatalogZipRomHits(e, zipFs) * 100;
	/* Tie-break: more titles / roms (range-expanded xml2, richer lists). */
	rank += e->titleCount;
	rank += e->romCount;
	return rank;
}

const CEmuGameEntry* CEmuCatalogFindArchive(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint)
{
	return CEmuCatalogFindArchiveForZip(cat, archive, dataDirHint, NULL);
}

const CEmuGameEntry* CEmuCatalogFindArchiveForZip(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint, const CEmuZipFs* zipFs)
{
	if (!cat || !archive || !archive[0]) return NULL;
	char key[CEMU_ARCHIVE_NAME];
	strncpy_s(key, archive, _TRUNCATE);
	for (char* p = key; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
	}
	const CEmuGameEntry* best = NULL;
	int bestRank = -1000000;
	/* Prefer exact archive==zip stem. Only if none: allow "stem,companion"
	   (emdr_msx,fmpac_msx). Matching comma forms in the same pass stole
	   rank from primary-only twins and dropped MSX OK 414→403. */
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < cat->count; i++) {
			const CEmuGameEntry* e = cat->entry[i];
			if (!e) continue;
			int hit = 0;
			if (pass == 0) {
				hit = (_stricmp(e->archive, key) == 0) ? 1 : 0;
			} else {
				const char* a = e->archive;
				const size_t klen = strlen(key);
				if (_strnicmp(a, key, (unsigned)klen) == 0 && a[klen] == ',')
					hit = 1;
			}
			if (!hit) continue;
			if (dataDirHint && dataDirHint[0] && _stricmp(e->dataDir, dataDirHint) != 0)
				continue;
			const int rank = CEmuCatalogArchiveRank(e, zipFs);
			if (!best || rank > bestRank) {
				best = e;
				bestRank = rank;
			}
		}
		if (best) break;
	}
	return best;
}

int CEmuCatalogCollectArchiveForZip(const CEmuCatalog* cat,
	const char* archive, const CEmuZipFs* zipFs,
	const CEmuGameEntry** out, int outCap)
{
	if (!cat || !archive || !archive[0] || !out || outCap <= 0) return 0;
	char key[CEMU_ARCHIVE_NAME];
	strncpy_s(key, archive, _TRUNCATE);
	for (char* p = key; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
	}
	int ranks[64];
	int n = 0;
	for (int pass = 0; pass < 2 && n == 0; pass++) {
		for (int i = 0; i < cat->count && n < outCap && n < (int)_countof(ranks); i++) {
			const CEmuGameEntry* e = cat->entry[i];
			if (!e) continue;
			int hit = 0;
			if (pass == 0) {
				hit = (_stricmp(e->archive, key) == 0) ? 1 : 0;
			} else {
				const char* a = e->archive;
				const size_t klen = strlen(key);
				if (_strnicmp(a, key, (unsigned)klen) == 0 && a[klen] == ',')
					hit = 1;
			}
			if (!hit) continue;
			out[n] = e;
			ranks[n] = CEmuCatalogArchiveRank(e, zipFs);
			n++;
		}
	}
	for (int a = 0; a < n; a++) {
		for (int b = a + 1; b < n; b++) {
			if (ranks[b] > ranks[a]) {
				const CEmuGameEntry* te = out[a];
				out[a] = out[b];
				out[b] = te;
				int tr = ranks[a];
				ranks[a] = ranks[b];
				ranks[b] = tr;
			}
		}
	}
	return n;
}

int CEmuArchiveStemFromPath(const wchar_t* zipPath, char* out, int outCap)
{
	return CEmuArchiveStem(zipPath, out, outCap) ? 1 : 0;
}

int CEmuGameTitleCount(const CEmuGameEntry* ge)
{
	return ge ? ge->titleCount : 0;
}

int CEmuGameTitleAt(const CEmuGameEntry* ge, int index0, unsigned* outCode,
	wchar_t* outLabel, int outLabelChars)
{
	if (!ge || index0 < 0 || index0 >= ge->titleCount) {
		if (outCode) *outCode = (unsigned)(index0 >= 0 ? (unsigned)index0 : 0u);
		if (outLabel && outLabelChars > 0) outLabel[0] = 0;
		return 0;
	}
	const CEmuTitleEntry* t = &ge->title[index0];
	if (outCode) *outCode = t->code;
	if (outLabel && outLabelChars > 0)
		wcsncpy_s(outLabel, (size_t)outLabelChars, t->label, _TRUNCATE);
	return 1;
}

static int CEmuTitleLabelLooksLikeStop(const wchar_t* label)
{
	if (!label || !label[0]) return 0;
	/* hoot titlelists often put [STOP] / STOP first (pc98dos CMD/MDRV/…). */
	for (const wchar_t* p = label; *p; p++) {
		wchar_t c0 = p[0], c1 = p[1], c2 = p[2], c3 = p[3];
		if (c0 >= L'a' && c0 <= L'z') c0 = (wchar_t)(c0 - L'a' + L'A');
		if (c1 >= L'a' && c1 <= L'z') c1 = (wchar_t)(c1 - L'a' + L'A');
		if (c2 >= L'a' && c2 <= L'z') c2 = (wchar_t)(c2 - L'a' + L'A');
		if (c3 >= L'a' && c3 <= L'z') c3 = (wchar_t)(c3 - L'a' + L'A');
		if (c0 == L'S' && c1 == L'T' && c2 == L'O' && c3 == L'P')
			return 1;
	}
	return 0;
}

unsigned CEmuGameTitleCodeForIndex(const CEmuGameEntry* ge, unsigned titleIndex1)
{
	/* No titlelist: sound CPUs usually number BGM from 0x01 — use 1-based index
	   as the command. (Old titleIndex1-1 made song1→0→board default and song2→1
	   collide on the same first BGM for QSound/Taito/CPS.) */
	if (!ge || ge->titleCount <= 0)
		return titleIndex1 ? titleIndex1 : 1u;
	if (titleIndex1 == 0) titleIndex1 = 1;
	int idx = (int)titleIndex1 - 1;
	if (idx >= ge->titleCount) idx = ge->titleCount - 1;
	if (idx < 0) idx = 0;
	/* Default open (index 1): skip leading [STOP] rows so casual zip drops
	   actually play. Explicit later indices are left alone. code 0 remains
	   valid for real first tracks (arcus2). */
	if (titleIndex1 == 1 && ge->titleCount > 1) {
		wchar_t lab[CEMU_GAME_NAME];
		lab[0] = 0;
		CEmuGameTitleAt(ge, idx, NULL, lab, (int)_countof(lab));
		if (CEmuTitleLabelLooksLikeStop(lab)) {
			for (int j = 0; j < ge->titleCount; j++) {
				lab[0] = 0;
				CEmuGameTitleAt(ge, j, NULL, lab, (int)_countof(lab));
				if (!CEmuTitleLabelLooksLikeStop(lab))
					return ge->title[j].code;
			}
		}
	}
	/* code 0 is valid (arcus2 first track). Do not coerce to idx+1. */
	return ge->title[idx].code;
}
