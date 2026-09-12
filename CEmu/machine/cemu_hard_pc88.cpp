#include "StdAfx.h"
#include "cemu_hard_pc88.h"
#include "../cemu_zipfs.h"
#include "../cemu_rhythm.h"
#include "../chip/cemu_chip_opna.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include <stdlib.h>
#include <string.h>

/* PC-88 sound-only model (hoot-compatible):
   - Main RAM is a flat 64KB image (mem_[0x10000]). Sub-RAM is unused.
   - Catalog <rom type="code" offset="…"> is copied verbatim into that map.
   - Song data: mdata_addr / mfile_size / title bits; when the music file is
     also mapped as code@mdata, keep the full image and let the guest select
     the song (do not invent host trampolines or per-sample vector guards).
   - IM2 vectors are fixed: VRTC=02, RTC=04, SOUND=08 at page (I<<8).
   - Deliver use_rtc / use_vrtc from catalog; OPN Timer → SOUND. */

static CHardPc88* g_pc88Active = NULL;

static int CEmuParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal)
{
	if (!ge || !name) return defVal;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) != 0) continue;
		const char* v = ge->opt[i].value;
		if (!v || !v[0]) return defVal;
		return (int)strtoul(v, NULL, 0);
	}
	return defVal;
}

/* Local bpoint.zip ships DRIVER.BIN / MDAT* / MDATN* / BURNPCM while catalog
   bpoint88 still lists MUCO3 / B0xx / ADPCM. Remap so the Enix player can load. */
static const unsigned char* CEmuPc88ZipFind(const CEmuZipFs* fs, const char* name,
	unsigned* outSize, int bgmOff, int preferMdatN)
{
	if (outSize) *outSize = 0;
	if (!fs || !name || !name[0]) return NULL;
	const unsigned char* data = CEmuZipFsFind(fs, name, outSize);
	if (data && (!outSize || *outSize > 0))
		return data;
	if (_stricmp(name, "MUCO3") == 0) {
		data = CEmuZipFsFind(fs, "DRIVER.BIN", outSize);
		if (data) return data;
	}
	if (_stricmp(name, "ADPCM") == 0) {
		data = CEmuZipFsFind(fs, "BURNPCM", outSize);
		if (data) return data;
	}
	if (bgmOff >= 0 && bgmOff < 256) {
		char alt[32];
		const int n = bgmOff + 1; /* catalog 0x00 → MDAT01 */
		const char* first = preferMdatN ? "MDATN%02X" : "MDAT%02X";
		const char* second = preferMdatN ? "MDAT%02X" : "MDATN%02X";
		_snprintf_s(alt, _TRUNCATE, first, n);
		data = CEmuZipFsFind(fs, alt, outSize);
		if (data) return data;
		_snprintf_s(alt, _TRUNCATE, second, n);
		data = CEmuZipFsFind(fs, alt, outSize);
		if (data) return data;
	}
	return NULL;
}

CHardPc88::CHardPc88()
	: cmd(0)
	, param(0)
	, song(0)
	, soundIrqMasked(0)
	, useRtc(0)
	, useVrtc(0)
	, opnaMode(0)
	, cpuHz_(4000000)
	, cpu_(NULL)
	, chip_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, mdataAddr_(-1)
	, mdataSize_(0)
	, mfileSize_(0)
	, vdataAddr_(-1)
	, vfileSize_(0)
	, wolfteamMode_(0)
	, mucomBankCopy_(0)
	, mdataAddrDefaulted_(0)
	, packedKoei_(0)
	, initPc_(0)
	, forcePlayEi_(0)
	, mirrorSoundToRtc_(0)
	, armGineidenTimer_(0)
	, armLizardTimer_(0)
	, longPlayDrain_(0)
	, yaksaPatch2_(0)
	, armNavituneTimer_(0)
	, naviSongAddr_(0)
	, deferRtcAfterPlay_(0)
	, n88RtcIsr_(0)
	, n88RtcThrottleAddr_(0)
	, schemeMode_(0)
	, falcomType_(0)
	, playKickBase_(0)
	, playKickInitOff_(0)
	, playKickEi_(1)
	, titleCode_(0)
{
	hardKind = KIND_PC88;
	memset(mem_, 0, sizeof(mem_));
	memset(ioPorts_, 0, sizeof(ioPorts_));
	textWinHi_ = 0x80; /* window closed = plain main RAM at 8000-83FF */
	memset(textWinShadow_, 0, sizeof(textWinShadow_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(progBank_, 0, sizeof(progBank_));
	memset(progBankSize_, 0, sizeof(progBankSize_));
	memset(voiceBank_, 0, sizeof(voiceBank_));
	memset(voiceBankSize_, 0, sizeof(voiceBankSize_));
}

CHardPc88::~CHardPc88()
{
	Shutdown();
}

void CHardPc88::FreeBanks()
{
	for (int i = 0; i < 256; i++) {
		if (bgmBank_[i]) { free(bgmBank_[i]); bgmBank_[i] = NULL; }
		if (progBank_[i]) { free(progBank_[i]); progBank_[i] = NULL; }
		if (voiceBank_[i]) { free(voiceBank_[i]); voiceBank_[i] = NULL; }
		bgmBankSize_[i] = 0;
		progBankSize_[i] = 0;
		voiceBankSize_[i] = 0;
	}
}

static int CEmuPc88HasSongBank(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		const char* t = ge->rom[i].type;
		if (!t || !t[0]) continue;
		if (_stricmp(t, "bgm") == 0 || _stricmp(t, "voice") == 0 || _stricmp(t, "song") == 0)
			return 1;
	}
	return 0;
}

/* Catalog omitted mdata_addr but title bits 16..31 encode the absolute load
   page (ashe 0x1000/0x1100, meltdown/refight MMLEX 0x9400/0xa900, galfstrm
   0x6800/0xe000). Default 0x4000 then stages the bank where HL never looks.
   Skip wolfteam/mfile (vdata or packed offset) and KOEI-style mid bytes.
   ashe DRIVER@7800 only references 0x1000 — titles 0x11xxxxxx share that
   page (high byte is play/mode, not a second load window). */
static int CEmuPc88TitleEncodedMdata(unsigned titleCode, int mdataDefaulted, int wolfteam)
{
	if (!mdataDefaulted || wolfteam)
		return -1;
	if ((titleCode & 0xff00u) != 0)
		return -1; /* bits 8..15 used (KOEI file offset / other packed) */
	unsigned hi = (titleCode >> 16) & 0xffffu;
	if (hi < 0x1000 || hi > 0xE000 || (hi & 0xffu) != 0)
		return -1;
	if (hi >= 0x1000 && hi < 0x1200)
		hi = 0x1000;
	return (int)hi;
}

/* ashe-class MUS: leading channel count then absolute ptr words whose
   high bytes cluster on the link page (MUS09→0x93xx). Title hi16 0x10/0x11
   is play/mode, not the link address — staging there leaves ptrs dangling. */
static int CEmuPc88InferMusLinkAddr(const unsigned char* data, unsigned len)
{
	if (!data || len < 8)
		return -1;
	const unsigned nch = data[0];
	if (nch < 1 || nch > 16 || 1 + nch * 4 > len)
		return -1;
	unsigned hist[256];
	memset(hist, 0, sizeof(hist));
	unsigned hits = 0;
	unsigned minP = 0xFFFF, maxP = 0;
	for (unsigned i = 0; i < nch; i++) {
		const unsigned ptr = (unsigned)data[1 + i * 4]
			| ((unsigned)data[2 + i * 4] << 8);
		if (ptr < 0x2000 || ptr >= 0xE000)
			return -1;
		if (ptr < minP) minP = ptr;
		if (ptr > maxP) maxP = ptr;
		hist[ptr >> 8]++;
		hits++;
	}
	if (hits < 3)
		return -1;
	/* MUS00 spreads six ch ptrs across A0/A1/A2 (2 each) so a per-page
	   majority never forms; the phrases still sit in one 8K window.
	   Align to 256B (MUS10 is linked at 9300, not 4K-rounded 9000). */
	if (maxP >= minP && (maxP - minP) < 0x2000)
		return (int)(minP & 0xFF00);
	unsigned bestPage = 0, bestCount = 0;
	for (unsigned p = 0; p < 256; p++) {
		if (hist[p] > bestCount) {
			bestCount = hist[p];
			bestPage = p;
		}
	}
	if (bestCount * 2 < hits)
		return -1;
	return (int)(bestPage << 8);
}

/* ashe 93xx banks stage at titleMdata (DRIVER occupies native 9000). Header
   reloc fixes the 4-byte ch ptrs. 8188 then reads a word at that ptr:
   bit7-of-hi = in-place command stream; else a phrase-list (absolute 93xx
   at the record, or a small offset to a list). Reloc the list (and FFFF
   loop word) too. MUS00 is native A000 — skipped when loadAddr==linkBase. */
static void CEmuPc88AsheRelocPhrases(uint8_t* mem, int loadAddr, unsigned n,
	unsigned linkBase)
{
	if (!mem || loadAddr < 0 || n < 8 || linkBase < 0x2000
		|| linkBase >= 0xE000)
		return;
	const unsigned nch = mem[loadAddr];
	if (nch < 1 || nch > 16 || 1 + nch * 4 > n)
		return;
	unsigned hdrEnd = 1 + nch * 4;
	if (n >= 0x19 + 5
		&& mem[loadAddr + 0x19] >= 3 && mem[loadAddr + 0x19] <= 16) {
		const unsigned n2 = mem[loadAddr + 0x19];
		const unsigned e2 = 0x19 + 1 + n2 * 4;
		if (e2 > hdrEnd && e2 <= n)
			hdrEnd = e2;
	}
	unsigned chPtr[16];
	unsigned chOff[16];
	unsigned nList = 0;
	for (unsigned c = 0; c < nch && nList < 16; c++) {
		const unsigned po = 1 + c * 4;
		unsigned w = (unsigned)mem[loadAddr + (int)po]
			| ((unsigned)mem[loadAddr + (int)po + 1] << 8);
		if (w >= (unsigned)loadAddr && w < (unsigned)loadAddr + n) {
			chOff[nList] = po;
			chPtr[nList] = w;
			nList++;
		}
	}
	for (unsigned i = 0; i < nList; i++) {
		const unsigned p = chPtr[i];
		if (p + 1 >= (unsigned)loadAddr + n)
			continue;
		unsigned w = (unsigned)mem[p]
			| ((unsigned)mem[p + 1] << 8);
		/* 93xx/94xx phrase ptrs have bit15 set — that is not an in-place
		   C1..FE command stream (those sit outside the link window). */
		unsigned list = 0;
		if (w >= linkBase && w < linkBase + n) {
			/* Channel record is already the phrase list (MUS09/10 @ 9300). */
			list = p;
		} else if (w >= hdrEnd && w < n) {
			const unsigned dest = (unsigned)loadAddr + w;
			if (dest + 1 >= (unsigned)loadAddr + n)
				continue;
			const unsigned peek = (unsigned)mem[dest]
				| ((unsigned)mem[dest + 1] << 8);
			if (peek >= linkBase && peek < linkBase + n) {
				list = dest;
				mem[loadAddr + (int)chOff[i]] = (uint8_t)(dest & 0xff);
				mem[loadAddr + (int)chOff[i] + 1] = (uint8_t)((dest >> 8) & 0xff);
			} else if (p + 5 < (unsigned)loadAddr + n) {
				mem[p] = (uint8_t)(dest & 0xff);
				mem[p + 1] = (uint8_t)((dest >> 8) & 0xff);
				mem[p + 2] = 0xFF;
				mem[p + 3] = 0xFF;
				mem[p + 4] = (uint8_t)(p & 0xff);
				mem[p + 5] = (uint8_t)((p >> 8) & 0xff);
				continue;
			}
		}
		if (!list)
			continue;
		for (unsigned q = list; q + 1 < (unsigned)loadAddr + n; q += 2) {
			unsigned v = (unsigned)mem[q]
				| ((unsigned)mem[q + 1] << 8);
			if (v == 0)
				break;
			if (v == 0xFFFF) {
				if (q + 3 < (unsigned)loadAddr + n) {
					unsigned lp = (unsigned)mem[q + 2]
						| ((unsigned)mem[q + 3] << 8);
					if (lp >= linkBase && lp < linkBase + n)
						lp = lp - linkBase + (unsigned)loadAddr;
					else if (lp >= hdrEnd && lp < n)
						lp = (unsigned)loadAddr + lp;
					if (lp + 1 < (unsigned)loadAddr + n) {
						const unsigned peek = (unsigned)mem[lp]
							| ((unsigned)mem[lp + 1] << 8);
						const int ok = (peek >= (unsigned)loadAddr
								&& peek < (unsigned)loadAddr + n)
							|| (peek >= linkBase && peek < linkBase + n);
						if (!ok)
							lp = list;
					}
					mem[q + 2] = (uint8_t)(lp & 0xff);
					mem[q + 3] = (uint8_t)((lp >> 8) & 0xff);
				}
				break;
			}
			if (v >= linkBase && v < linkBase + n) {
				v = v - linkBase + (unsigned)loadAddr;
				mem[q] = (uint8_t)(v & 0xff);
				mem[q + 1] = (uint8_t)((v >> 8) & 0xff);
			}
		}
	}
}

static int CEmuPc88PatchSongShift8(const uint8_t* mem);
static int CEmuPc88PatchLdirFrom4000(const uint8_t* mem);
static int CEmuPc88PatchFalcomAndF0Cp10(const uint8_t* mem);
static int CEmuPc88PatchFalcomLdirFromC000(const uint8_t* mem);
static unsigned CEmuPc88FalcomYs2MusDest(const uint8_t* mem, unsigned bank);
static void CEmuPc88FalcomYs2PlantTable(uint8_t* mem, unsigned bank);
static int CEmuPc88AshePlayHi(const uint8_t* mem);
static void CEmuPc88PlantAsheSongTable(uint8_t* mem, unsigned songNum, int mdataAddr);
static int CEmuPc88PatchManreq(const uint8_t* mem);
static int CEmuPc88PatchGandhara(const uint8_t* mem);
static int CEmuPc88PatchGinei2(const uint8_t* mem);
static int CEmuPc88PatchXzrA4(const uint8_t* mem);
static int CEmuPc88PatchXzr2VoiceF000(const uint8_t* mem);
static int CEmuPc88PatchSmd8A00(const uint8_t* mem);
static int CEmuPc88PatchAfHl4400(const uint8_t* mem);
static int CEmuPc88PatchRomanciaSr(const uint8_t* mem, int initPc);
static int CEmuPc88PatchRobowr(const uint8_t* mem);
static void CEmuPc88PlantIceclimbTitleLoop(uint8_t* mem);

/* JR/JR cc displacement 0xFB is not the EI opcode — p1demo/castle poll with
   `JR Z,$` encodes FB as the offset and was aborting NeedsBootEiPulse. */
static int CEmuPc88IsJrDisp(const uint8_t* mem, int at)
{
	if (!mem || at <= 0) return 0;
	const uint8_t prev = mem[at - 1];
	return prev == 0x18 || prev == 0x20 || prev == 0x28
		|| prev == 0x30 || prev == 0x38;
}

int CHardPc88::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	/* 8801-10 = Sound Board II (OPNA); specialty Falcom types alias to OPN in catalog. */
	opnaMode = (_stricmp(ge->subtype, "opna") == 0
		|| _stricmp(ge->subtype, "8801-10") == 0) ? 1 : 0;
	mdataAddr_ = CEmuParseOptHex(ge, "mdata_addr", -1);
	mdataSize_ = CEmuParseOptHex(ge, "mdata_size", 0);
	mfileSize_ = CEmuParseOptHex(ge, "mfile_size", 0);
	vdataAddr_ = CEmuParseOptHex(ge, "vdata_addr", -1);
	vfileSize_ = CEmuParseOptHex(ge, "vfile_size", 0);
	if (vfileSize_ <= 0 && vdataAddr_ >= 0)
		vfileSize_ = CEmuParseOptHex(ge, "vdata_size", 0x1000);
	wolfteamMode_ = (vdataAddr_ >= 0 || mfileSize_ > 0) ? 1 : 0;
	mucomBankCopy_ = 0;
	if (_stricmp(ge->subtype, "muco") == 0 || _stricmp(ge->subtype, "mucom88") == 0)
		mucomBankCopy_ = 1;
	else if (ge->driverAlias[0]) {
		char aliasA[CEMU_GAME_NAME];
		WideCharToMultiByte(CP_ACP, 0, ge->driverAlias, -1, aliasA, (int)sizeof(aliasA), NULL, NULL);
		if (_strnicmp(aliasA, "mucom", 5) == 0)
			mucomBankCopy_ = 1;
	}
	/* Catalog often omits mdata_addr; PATCH still loads HL=0x4000 (hoot/wolfteam,
	   and most silent-FAIL sets). Without a default LoadSongData no-ops.
	   Falcom type=prog + bgm (xana2): song banks are linked for ~0x5C00
	   (m.* headers point into 5Cxx). Default 0x4000 sits under the prog
	   image and is wiped by OUT(02) map. */
	mdataAddrDefaulted_ = 0;
	if (mdataAddr_ < 0) {
		int hasProg = 0;
		for (int i = 0; i < ge->romCount; i++) {
			if (_stricmp(ge->rom[i].type, "prog") == 0) {
				hasProg = 1;
				break;
			}
		}
		if (hasProg && CEmuPc88HasSongBank(ge)) {
			mdataAddr_ = 0x5C00;
			mdataAddrDefaulted_ = 1;
		} else if (wolfteamMode_ || CEmuPc88HasSongBank(ge)) {
			mdataAddr_ = 0x4000;
			mdataAddrDefaulted_ = 1;
		}
	}
	/* KOEI FMDRV.SYS family: packed CIM, default mdata 0x4000. Play uses
	   E=0 (raw pointer); the low title byte is the bank, not FMDRV song.
	   carmine88 shares mfile/mdata sizes but has MUSIC@A000 that needs the
	   title low byte on port 01 — do not treat those as KOEI. */
	{
		int musicA000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0xa000 && _stricmp(r->name, "MUSIC") == 0)
				musicA000 = 1;
		}
		packedKoei_ = (!mucomBankCopy_ && mdataAddrDefaulted_ && mfileSize_ > 0
			&& mdataSize_ > 0 && !musicA000) ? 1 : 0;
	}
	initPc_ = CEmuParseOptHex(ge, "init_pc", 0);
	schemeMode_ = 0;
	falcomType_ = 0;
	yaksaPatch2_ = 0;
	armLizardTimer_ = 0;
	longPlayDrain_ = 0;
	/* Detect BOTHTEC Scheme OPNA: MUS2 + ADR_ (+ INT2). Catalog often parks
	   PATCH at 0000 which stack-clobbers under SP=0100; hoot uses 0x9000. */
	{
		int hasMus2 = 0, hasAdr = 0, hasInt2 = 0, patchAt0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_stricmp(r->name, "MUS2") == 0) hasMus2 = 1;
			if (_stricmp(r->name, "ADR_") == 0 || _stricmp(r->name, "ADR") == 0)
				hasAdr = 1;
			if (_stricmp(r->name, "INT2") == 0) hasInt2 = 1;
			if (_stricmp(r->name, "PATCH") == 0 && r->offset == 0)
				patchAt0 = 1;
		}
		if (opnaMode && hasMus2 && hasAdr) {
			schemeMode_ = 1;
			if (patchAt0 && initPc_ == 0)
				initPc_ = 0x9000;
			/* hoot scheme.cpp leaves TIMER_INT commented out. */
			useRtc = 0;
			/* Catalog aliases scheme under mucom88, but hoot scheme.cpp
			   raises sound IRQ on Timer A and B (unlike mucom Timer-B-only).
			   Also port-0 is BGM bank load, not mucom [5C/5D] bank-copy. */
			mucomBankCopy_ = 0;
		}
		(void)hasInt2;
	}
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "use_rtc") == 0 && strtoul(ge->opt[i].value, NULL, 0))
			useRtc = 1;
		if (_stricmp(ge->opt[i].name, "use_vrtc") == 0 && strtoul(ge->opt[i].value, NULL, 0))
			useVrtc = 1;
	}
	/* Game Arts THEXDER family — interrupt page must match what the music
	   image plants (no full game/OS; layout is everything):
	     thexder88: DEMOM LD (F302),HL + catalog use_vrtc
	     thexder:   DEMOM LD (F304),HL + N88_3@6000 + catalog use_rtc
	     bokosuka:  MUSIC LD (F304),HL + N88_3@6000 + catalog use_rtc
	   Do NOT flip N88 packs to VRTC — that left F302 on the PATCH stub while
	   the real player never ran. DEMOM@9000 alone (no N88) still prefers
	   VRTC like thexder88. N88 packs need host EI: bokosuka poll JR uses
	   disp=FBh (not opcode EI) and play can leave DI. */
	{
		int hasN88 = 0, hasDemom9000 = 0, hasMusicC000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_strnicmp(r->name, "N88", 3) == 0 || _strnicmp(r->name, "n88", 3) == 0)
				hasN88 = 1;
			if (r->offset == 0x9000 && (_stricmp(r->name, "DEMOM") == 0
				|| _stricmp(r->name, "demom") == 0))
				hasDemom9000 = 1;
			if (r->offset == 0xC000 && (_stricmp(r->name, "MUSIC") == 0
				|| _stricmp(r->name, "music") == 0))
				hasMusicC000 = 1;
		}
		if (hasN88 && (hasDemom9000 || hasMusicC000)) {
			/* Keep RTC off through boot/play so DEMOM/MUSIC can LD (F304),HL
			   to the real player (C1D1 / C183) before the first tick. Early
			   RTC landed on the E80E stub and wedged PC there (wr≈0). */
			useRtc = 0;
			useVrtc = 0;
			deferRtcAfterPlay_ = 1;
			forcePlayEi_ = 1;
			/* Port-cmd play on these packs wanders into N88/MUSIC tail before
			   the F304 plant. Host-kick the DEMOM/MUSIC play entry directly
			   (thexder88's PATCH does the same via CALL C000 after fmdtex). */
			if (hasDemom9000) {
				playKickBase_ = 0xC000; /* DEMOM image spans into C000 */
				playKickInitOff_ = 0;
				playKickEi_ = 0; /* plant under DI; RTC+EI after defer */
				n88RtcIsr_ = 0xC1D1;
				n88RtcThrottleAddr_ = 0xD1D2;
			} else if (hasMusicC000) {
				playKickBase_ = 0xC00F; /* skip C000 RET Z on (8FCE) */
				playKickInitOff_ = 0;
				playKickEi_ = 0;
				n88RtcIsr_ = 0xC183;
				n88RtcThrottleAddr_ = 0xC5B0;
			}
		} else if (useRtc && !useVrtc && hasDemom9000 && !hasN88) {
			useRtc = 0;
			useVrtc = 1;
			forcePlayEi_ = 1;
		}
	}
	/* castle/castleex: PROG2 plants the music ISR at I:04 (RTC), not OPN
	   vector 08. Catalog use_rtc must stay on — clearing it left I=$1A
	   armed with no tick source (MUSIC@F800 intact, key=0). */
	/* Scheme: catalog may set use_rtc; keep it off (see above). */
	if (schemeMode_)
		useRtc = 0;
	/* Hoot mucom88 TIMER_INT (RTC) is commented out for Sorcerian. Catalog
	   pc88 conversion added use_rtc; delivering it vectors into the BIOS
	   disk wait at 05EE and stays silent. OK siblings omit use_rtc. */
	if (useRtc) {
		int hasBios = 0, hasSedat = 0, patchC000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_stricmp(r->name, "BIOS") == 0 && r->offset == 0) hasBios = 1;
			if (strstr(r->name, "SEDAT") || strstr(r->name, "sedat")) hasSedat = 1;
			if (_stricmp(r->name, "PATCH") == 0 && r->offset == 0xc000) patchC000 = 1;
		}
		if (hasBios && hasSedat && patchC000)
			useRtc = 0;
	}
	/* Game Arts (solitair / jikochu*): PATCH plants the player ISR at
	   IM2 vector 04 (RTC), not 08 (OPN). Catalog use_rtc must stay on —
	   clearing it leaves play armed under EI with no tick source.
	   EXCEPT: RTC during CALL into PLAY88/C000 re-enters the ISR and
	   mutes. Keep RTC off; DirectPlayKick runs init(+6) then base under
	   OPN-free host pacing instead. */
	/* jikochu*: sole music image is both code@mdata and a bgm bank. */
	if (useRtc && mdataAddr_ >= 0) {
		const char* codeAtMdata = NULL;
		int bgmSame = 0, codeRoms = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") == 0) {
				codeRoms++;
				if (r->offset == mdataAddr_ && r->name)
					codeAtMdata = r->name;
			}
		}
		if (codeAtMdata && codeRoms <= 2) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "bgm") == 0 && r->name
					&& _stricmp(r->name, codeAtMdata) == 0)
					bgmSame = 1;
			}
			if (bgmSame)
				useRtc = 0;
		}
	}
	/* jikochu3: music is code@C000 only (no bgm). Same I=0 PATCH. */
	if (useRtc) {
		int codeC000 = 0, codeRoms = 0, bgmRoms = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") == 0) {
				codeRoms++;
				if (r->offset == 0xc000) codeC000 = 1;
			} else if (_stricmp(r->type, "bgm") == 0)
				bgmRoms++;
		}
		if (codeC000 && codeRoms <= 2 && bgmRoms == 0
			&& (mdataAddr_ < 0 || mdataAddrDefaulted_))
			useRtc = 0;
	}
	/* solitair: PLAY88@6000 — RTC re-entry mutes; kick path replaces it. */
	if (useRtc && !useVrtc) {
		int hasPlay88 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_strnicmp(r->name, "PLAY88", 6) == 0)
				hasPlay88 = 1;
		}
		if (hasPlay88)
			useRtc = 0;
	}
	/* rogueal: catalog use_rtc. RTC during boot CALL 0868's page-clear
	   LDIR (BC=5BB5 @08B0) never finishes (PC sticks at 08B3). Defer RTC
	   until after play loads MUS* @8000 — vec04=0091 is the player ISR. */
	if (useRtc && !useVrtc && initPc_ == 0xf000) {
		int prog0 = 0, patchF000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0 && _strnicmp(r->name, "PROG", 4) == 0)
				prog0 = 1;
			if (r->offset == 0xf000 && _strnicmp(r->name, "PATCH", 5) == 0)
				patchF000 = 1;
		}
		if (prog0 && patchF000 && mdataAddr_ == 0x8000
			&& CEmuPc88HasSongBank(ge)) {
			useRtc = 0;
			deferRtcAfterPlay_ = 1;
			forcePlayEi_ = 1;
		}
	}
	if (useRtc && !useVrtc) {
		int demo100 = 0, driver81 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x0100 && _strnicmp(r->name, "DEMO", 4) == 0)
				demo100 = 1;
			if (r->offset == 0x8100 && _strnicmp(r->name, "DRIVER", 6) == 0)
				driver81 = 1;
		}
		if (demo100 && driver81) {
			/* p1demo1: DEMO1A@0100 is bitmap; PATCH CALL 01AE is unusable.
			   Prefer VRTC+EI so I=0 does not vector RTC into the bitmap.
			   LoadRoms plants CALL 96EE→OPN@84EE when the binary matches;
			   song/channel RAM @9A5B–A07F is still missing from the rip
			   (DRIVER EOF @9A00) — see .cursor/_cemu_p1demo1_blocker.txt. */
			useRtc = 0;
			useVrtc = 1;
			forcePlayEi_ = 1;
		}
	}
	/* byouin_88: MUSIC.SYS@9C00 player; PATCH poll has EI but the driver
	   plants I=F3 and needs RTC ticks. Catalog omits use_rtc. */
	if (!useRtc && !useVrtc) {
		int music9c = 0, patch0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x9c00 && (_strnicmp(r->name, "MUSIC", 5) == 0
				|| _stricmp(r->name, "MUSIC.SYS") == 0))
				music9c = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patch0 = 1;
		}
		if (music9c && patch0)
			useRtc = 1;
	}
	/* gineiden: DEMO@0100 + AMAIN@0480. Play plants IM2 sound ISR into
	   AMAIN (vec08) but leaves vec02/04 empty — VRTC/RTC would no-op.
	   Use RTC and mirror sound→RTC after play (see FixupIm2AfterPlay). */
	if (!useRtc && !useVrtc) {
		int demo100 = 0, amain480 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x0100 && _strnicmp(r->name, "DEMO", 4) == 0)
				demo100 = 1;
			if (r->offset == 0x0480 && _strnicmp(r->name, "AMAIN", 5) == 0)
				amain480 = 1;
		}
		if (demo100 && amain480) {
			useRtc = 0;
			useVrtc = 0;
			mirrorSoundToRtc_ = 0;
			/* PATCH CALL 0080 → 4E2F clears timers; 4E00 enables them.
			   Host re-arms Timer B after play (see ArmGineidenOpnTimer). */
			armGineidenTimer_ = 1;
		}
	}
	/* navitune-class: PATCH@init_pc + music code@mdata + matching bgm bank.
	   Title bits 8..23 = song offset inside that image (ApplyNavituneTitleSong). */
	if (!armGineidenTimer_ && initPc_ == 0x0100 && mdataAddr_ == 0x7700) {
		int hasNavi = 0, hasPrg = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!r->name) continue;
			if (_strnicmp(r->name, "navimus", 7) == 0) hasNavi = 1;
			if (_strnicmp(r->name, "naviprg", 7) == 0) hasPrg = 1;
		}
		if (hasNavi && hasPrg)
			armNavituneTimer_ = 1;
	}
	/* lizard88: no catalog RTC/VRTC — player advances on OPN Timer B @vec08.
	   PATCH boot arms timers then play CALL 9F0F may clear them; re-arm after
	   cmd=1 like gineiden. */
	if (!armGineidenTimer_ && !useRtc && !useVrtc) {
		int main9f = 0, patch0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x9f00 && _strnicmp(r->name, "MAIN", 4) == 0)
				main9f = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patch0 = 1;
		}
		if (main9f && patch0)
			armLizardTimer_ = 1;
	}
	/* yaksa PATCH2: play under DI; vdata+voiceBank would zero port01 — force
	   EI and keep song id on param (see PlaySongIndex). */
	if (useVrtc) {
		int patch2 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0 && _stricmp(r->name, "PATCH2") == 0)
				patch2 = 1;
		}
		if (patch2) {
			forcePlayEi_ = 1;
			yaksaPatch2_ = 1;
		}
	}
	/* 1942_88: ADEE LDIR needs a long cmd=1 drain before A343 arms I+Timer. */
	if (!useRtc && !useVrtc) {
		int progA3 = 0, musicF0 = 0, patch0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0xa300 && _strnicmp(r->name, "PROG", 4) == 0)
				progA3 = 1;
			if (r->offset == 0xf000 && _strnicmp(r->name, "MUSIC", 5) == 0)
				musicF0 = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patch0 = 1;
		}
		if (progA3 && musicF0 && patch0) {
			longPlayDrain_ = 1;
			forcePlayEi_ = 1; /* Timer IM2 after A343 needs host EI */
		}
	}
	/* A title that asks for both RTC and VRTC generally means it, and which
	   of the two actually drives the player is decided by its IM2 table, not
	   by a rom name — see PruneDeadTickSources, run once the stub has planted
	   the table. (yokosuka: vec02→$0052 CALL SOUND+$0B0A, vec04→$B7EE; the
	   old "exact SOUND @≥B000 ⇒ drop VRTC" rule killed the first and the song
	   died a second in.) */
	/* spitfl88 / tf88sr: PROG-only + PATCH@0 + no init_pc. IM2 table parks
	   the real player on RTC (vec04); VRTC/SOUND slots are RET stubs
	   (spitfl A820=C9, tf88 409E=ack+RET). Keep catalog RTC and force EI —
	   flipping to VRTC was a guaranteed mute. hangon88/plazmasr keep init_pc
	   so they never match this filter. */
	if (useRtc && !useVrtc) {
		int codeRoms = 0, bgmRoms = 0, hasProg = 0, patchAt0 = 0;
		int hasInitPc = 0, patchAtInit = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "init_pc") == 0)
				hasInitPc = 1;
		}
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "bgm") == 0) bgmRoms++;
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			codeRoms++;
			if (_strnicmp(r->name, "PROG", 4) == 0) hasProg = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patchAt0 = 1;
			if (hasInitPc && initPc_ >= 0 && r->offset == initPc_
				&& _strnicmp(r->name, "PATCH", 5) == 0)
				patchAtInit = 1;
		}
		if (!hasInitPc && hasProg && patchAt0 && bgmRoms == 0 && codeRoms <= 2) {
			/* Keep useRtc; PATCH has no EI before poll. */
			forcePlayEi_ = 1;
		}
		/* hangon88: PATCH@init_pc (8F00) IM2+poll, zero EI opcodes — RTC
		   ISR never runs under DI. Same contract as spitfl forcePlayEi. */
		if (patchAtInit && hasProg && bgmRoms == 0)
			forcePlayEi_ = 1;
	}
	/* Direct play kick: Game Arts JR PATCH calls player+6 then relies on
	   RTC ISR (vec04) to CALL player+0. Without RTC, host does the same
	   init(+6) then base CALL. castle/castleex: PROG2@1000 is the working
	   entry (PATCH's CALL 1033 alone stays silent). */
	{
		int play88 = 0, codeC000 = 0, codeRoms = 0, voiceF000 = 0, prog1000 = 0;
		int patchJr = 0, soundKick = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			codeRoms++;
			if (_strnicmp(r->name, "PLAY88", 6) == 0 && r->offset == 0x6000)
				play88 = 1;
			if (r->offset == 0xc000) codeC000 = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patchJr = 1; /* confirmed JR in LoadRoms if needed */
			if (r->offset == 0xf000 && _strnicmp(r->name, "VOICE", 5) == 0)
				voiceF000 = 1;
			if (r->offset == 0x1000 && _strnicmp(r->name, "PROG", 4) == 0)
				prog1000 = 1;
			/* Exact "SOUND" only — SOUND1@D800 (makai88) must NOT kick B780
			   (empty RAM); that left song mailbox 0274=0 forever. */
			if (r->offset >= 0xb000 && r->offset < 0xe000
				&& _stricmp(r->name, "SOUND") == 0)
				soundKick = (int)r->offset + 0x1BD; /* B5C3+0x1BD → B780 */
		}
		if (play88) {
			playKickBase_ = 0x6000;
			playKickInitOff_ = 6;
			playKickEi_ = 1;
		} else if (codeC000 && patchJr && codeRoms <= 2) {
			/* jikochu*: PATCH@0 + music image at C000 (C3 jump table). */
			playKickBase_ = 0xC000;
			playKickInitOff_ = 6;
			playKickEi_ = 1;
		} else if (voiceF000 && prog1000) {
			/* castle/castleex: PROG2@1000. Must EI so OPN Timer IM2 runs —
			   playKickEi=0 left iff1=0 forever (SSG noise only, key=0). */
			playKickBase_ = 0x1000;
			playKickInitOff_ = 0;
			playKickEi_ = 1;
			forcePlayEi_ = 1;
		} else if (soundKick && (useRtc || useVrtc)) {
			/* yokosuka: PATCH play is DI; CALL SOUND+0x1BD then RTC ticks. */
			playKickBase_ = soundKick;
			playKickInitOff_ = 0;
			playKickEi_ = 1;
		}
	}
	/* catalog uses both clockmul and clock_mul */
	int clockmul = CEmuParseOptHex(ge, "clockmul", 0);
	if (clockmul <= 0)
		clockmul = CEmuParseOptHex(ge, "clock_mul", 1);
	if (clockmul < 1 || clockmul > 64) clockmul = 1;
	cpuHz_ = 4000000 * clockmul;
	const uint32_t clk = opnaMode ? 7987200u : 3993600u;
	chip_ = CEmuChipYm2608Create(clk, opnaMode, sampleRate_);
	/* hoot mucom88: Timer A overflow must not raise the Z80 sound IRQ
	   (only Timer B). Other PC88 drivers (KOEI/wolfteam/…) use A+B.
	   Also treat SEDAT+BIOS boots as mucom even if subtype is plain opn. */
	if (chip_) {
		/* scheme OPNA: always A+B (see schemeMode_ above). */
		int timerBOnly = (!schemeMode_ && mucomBankCopy_) ? 1 : 0;
		if (!timerBOnly && !schemeMode_) {
			int hasBios = 0, hasSedat = 0;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0 || !r->name) continue;
				if (_stricmp(r->name, "BIOS") == 0 && r->offset == 0) hasBios = 1;
				if (strstr(r->name, "SEDAT") || strstr(r->name, "sedat")) hasSedat = 1;
			}
			if (hasBios && hasSedat)
				timerBOnly = 1;
		}
		if (timerBOnly)
			chip_->SetTimerIrqPolicy(0);
	}
	cpu_ = new Ay_Cpu();
	return chip_ && cpu_ ? 1 : 0;
}

void CHardPc88::Shutdown()
{
	if (g_pc88Active == this)
		g_pc88Active = NULL;
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	FreeBanks();
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chip_) { CEmuChipYm2608Destroy(chip_); chip_ = NULL; }
}

void CHardPc88::StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	FreeBanks();
	const int preferMdatN = opnaMode || CEmuParseOptHex(ge, "use_pcmx8", 0);
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const int bgmOff = (_stricmp(r->type, "bgm") == 0) ? r->offset : -1;
		const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz, bgmOff, preferMdatN);
		if (!data || !sz) continue;
		if (_stricmp(r->type, "bgm") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			bgmBank_[idx] = (unsigned char*)malloc(sz);
			if (bgmBank_[idx]) {
				memcpy(bgmBank_[idx], data, sz);
				bgmBankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "voice") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			voiceBank_[idx] = (unsigned char*)malloc(sz);
			if (voiceBank_[idx]) {
				memcpy(voiceBank_[idx], data, sz);
				voiceBankSize_[idx] = sz;
			}
		}
		/* Falcom specialty: type=prog banks (APRG/SOUND/PR.NO*). Title bits
		   8..15 select the bank; OUT (02),song asks the host to map it in. */
		else if (_stricmp(r->type, "prog") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			progBank_[idx] = (unsigned char*)malloc(sz);
			if (progBank_[idx]) {
				memcpy(progBank_[idx], data, sz);
				progBankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "adpcm") == 0 && chip_ && r->offset >= 0) {
			/* hoot type=adpcm → YM2608 ADPCM-B external RAM (scheme V_* up
			   to ~0x3e000, valis2 ~235KB blob). Never put these in ADPCM-A:
			   A is the fixed 8KiB rhythm ROM (ym2608_adpcm_rom.bin). */
			if (!getenv("CEMU_SKIP_ADPCM_B"))
				chip_->SetAdpcmB(data, sz, (unsigned)r->offset);
		}
	}
	/* Catalog bgm offset 0 can miss the first ZipFind. Fill empty slots. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "bgm") != 0 || r->offset < 0 || r->offset >= 256)
			continue;
		if (bgmBank_[r->offset] && bgmBankSize_[r->offset] >= 16)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz, r->offset, preferMdatN);
		if (!data || sz < 16) continue;
		if (bgmBank_[r->offset]) {
			free(bgmBank_[r->offset]);
			bgmBank_[r->offset] = NULL;
		}
		bgmBank_[r->offset] = (unsigned char*)malloc(sz);
		if (bgmBank_[r->offset]) {
			memcpy(bgmBank_[r->offset], data, sz);
			bgmBankSize_[r->offset] = sz;
		}
	}
	DeriveMicrocabinVdata(ge);
}

/* xak_88 lists a type=voice blob per song but no vdata_addr, so the FM
   instrument bank never reached RAM and every channel keyed on with whatever
   was left in the operator registers — audible as "the FM part is missing"
   while SSG carried the tune, ~4x down on its sibling xak2_88.

   Microcabin's FM88/MMD driver keeps all of its state inside its own image
   and takes only the sequence pointer, so the voice bank has to be planted at
   the address the driver hardcodes: the block directly below mdata_addr.
   xak2_88 spells that layout out (mdata $F800, vdata $F400) and xak_88 needs
   the same relative placement ($F400 / $F000, confirmed by sweep).

   Deliberately keyed off the driver binary rather than applied as general
   arithmetic: across the catalog, entries that do declare vdata_addr put the
   voice somewhere driver-specific, and only this family lands directly below
   the sequence. */
void CHardPc88::DeriveMicrocabinVdata(const CEmuGameEntry* ge)
{
	if (vdataAddr_ >= 0 || mdataAddr_ <= 0)
		return;

	int isMicrocabin = 0;
	for (int i = 0; i < ge->romCount && !isMicrocabin; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		isMicrocabin = (_stricmp(r->name, "FM88.COM") == 0
			|| _stricmp(r->name, "MMD.COM") == 0);
	}
	if (!isMicrocabin)
		return;

	unsigned voiceSize = 0;
	for (int i = 0; i < 256; i++) {
		if (voiceBank_[i] && voiceBankSize_[i] > voiceSize)
			voiceSize = voiceBankSize_[i];
	}
	if (!voiceSize || (int)voiceSize > mdataAddr_)
		return;

	vdataAddr_ = mdataAddr_ - (int)voiceSize;
	if (vfileSize_ <= 0)
		vfileSize_ = (int)voiceSize;
}

int CHardPc88::ShouldRestageSong() const
{
	if (mdataAddr_ < 0)
		return 0;
	/* Scheme OPNA: C000 is live driver (MS0A) + BGM window; restaging the
	   song over it from Render clobbers MUS2. BGM loads via OUT (0),bank. */
	if (schemeMode_)
		return 0;
	/* Falcom E000: prog image occupies 0000..5FFF; restaging mdata@5C00
	   before ApplyFalcomPlay is wiped, and OUT(02) / Apply copies BGM. */
	if (falcomType_)
		return 0;
	/* PATCH at 0 + SP=0x0100: restaging mdata into page 0 clobbers the stack
	   and the poll loop (albatrss mdata=0x100). High init_pc (PATCH elsewhere)
	   can keep mdata at 0 safely. */
	if (mdataAddr_ < 0x200 && initPc_ < 0x200)
		return 0;
	return 1;
}

/* True when mdata sits on a soft-overlay page (SEDAT / TRPSCR). Those pages
   must stay intact through boot; restage-on-play puts the song back. Generic
   PROG overlaps (smariosp) still preload — they already ran that way OK. */
static int CEmuPc88SongOverlapsCode(CEmuZipFs* fs, const CEmuGameEntry* ge, int mdataAddr, int mdataSize)
{
	if (!fs || !ge || mdataAddr < 0)
		return 0;
	int mend = mdataAddr + (mdataSize > 0 ? mdataSize : 0x1000);
	if (mend > 0x10000) mend = 0x10000;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		const char* nm = r->name;
		if (!nm) continue;
		int soft = 0;
		if (strstr(nm, "SEDAT") || strstr(nm, "sedat")
			|| strstr(nm, "TRPSCR") || strstr(nm, "trpscr")
			|| strstr(nm, "Trpscr"))
			soft = 1;
		const int off = r->offset;
		if (off < 0 || off >= 0x10000) continue;
		/* jikochu*: same image is code@mdata and bgm — keep code through boot. */
		if (off == mdataAddr)
			soft = 1;
		if (!soft) continue;
		unsigned sz = 0;
		if (!CEmuPc88ZipFind(fs, r->name, &sz, -1, 0) || !sz) continue;
		const int end = off + (int)sz;
		if (off < mend && mdataAddr < end)
			return 1;
	}
	return 0;
}

/* can1_88 family: DRIVER.BIN is catalogued at 0x0100 but PATCH calls 0x20xx
   (I-page 0x20) after writing the IM2 word at 0x2008. Mirror the 4K image. */
static void CEmuPc88MirrorDriverPage20(uint8_t* mem)
{
	int calls20 = 0;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] == 0xCD && mem[i + 2] == 0x20) {
			calls20 = 1;
			break;
		}
	}
	if (!calls20)
		return;
	int occupied = 0;
	for (int i = 0; i < 16; i++) {
		if (mem[0x2000 + i]) {
			occupied = 1;
			break;
		}
	}
	if (occupied)
		return;
	if (mem[0x0100] == 0 && mem[0x0101] == 0)
		return;
	memcpy(mem + 0x2000, mem + 0x0100, 0x1000);
}

void CHardPc88::LoadSongData(unsigned titleCode)
{
	const unsigned songNum = titleCode & 0xff;
	/* KOEI / packed-bank titles encode a file offset in bits 8..23. */
	unsigned fileOff = (titleCode >> 8) & 0xffff;
	const int titleMdata = CEmuPc88TitleEncodedMdata(titleCode, mdataAddrDefaulted_, wolfteamMode_);
	if (titleMdata >= 0) {
		mdataAddr_ = titleMdata;
		/* hi16 is the absolute load page (ashe/galfstrm/MMLEX). Using it as
		   fileOff skips the whole bank (MUS09@0x1100 on a 0xC00 file). */
		fileOff = 0;
	}
	/* ashe MUS00 is catalog offset 0; if that slot is empty or not the
	   A000-linked opening (ch0 ptr A0A1), recover the bank by signature. */
	if (CEmuPc88AshePlayHi(mem_) && songNum == 0
		&& (!bgmBank_[0] || bgmBankSize_[0] < 25
			|| ((unsigned)bgmBank_[0][1] | ((unsigned)bgmBank_[0][2] << 8)) != 0xA0A1u)) {
		for (int i = 1; i < 256; i++) {
			if (!bgmBank_[i] || bgmBankSize_[i] < 25 || bgmBank_[i][0] != 6)
				continue;
			const unsigned p = (unsigned)bgmBank_[i][1]
				| ((unsigned)bgmBank_[i][2] << 8);
			if (p != 0xA0A1u)
				continue;
			unsigned char* copy = (unsigned char*)malloc(bgmBankSize_[i]);
			if (!copy)
				break;
			memcpy(copy, bgmBank_[i], bgmBankSize_[i]);
			if (bgmBank_[0])
				free(bgmBank_[0]);
			bgmBank_[0] = copy;
			bgmBankSize_[0] = bgmBankSize_[i];
			break;
		}
	}
	/* Voice before BGM: ys2 END/TTL images end on the music staging page
	   (ENDPRG@0100..20FF overlaps mus@2000; TTLPRG overlaps @3000). Planting
	   voice last used to clobber the mirrored *MUS blob. */
	/* ginei2: OPENING0 at vdata 0400 wipes ITEST (CALL 52FA). GEDS uses the
	   same 06-channel player as BGM_* — keep ITEST and skip the overlay. */
	if (CEmuPc88PatchGinei2(mem_) && vdataAddr_ == 0x400)
		;
	else if (voiceBank_[songNum] && vdataAddr_ >= 0) {
		unsigned n = voiceBankSize_[songNum];
		if (vfileSize_ > 0 && (unsigned)vfileSize_ < n)
			n = (unsigned)vfileSize_;
		if (vdataAddr_ + (int)n > 0x10000)
			n = (unsigned)(0x10000 - vdataAddr_);
		if (n > 0)
			memcpy(mem_ + vdataAddr_, voiceBank_[songNum], n);
		/* manreq88 A9C: LD A,$D0 / LD (A2C0),A. D0=RET NC returns from the
		   live trampoline when carry is clear (M's BOOGIE silent). Voice
		   banks 3–6 plant D1 there from PATCH; do the same in the image. */
		if (CEmuPc88PatchManreq(mem_) && vdataAddr_ >= 0
			&& vdataAddr_ + 7 < 0x10000
			&& mem_[vdataAddr_ + 6] == 0x3E && mem_[vdataAddr_ + 7] == 0xD0)
			mem_[vdataAddr_ + 7] = 0xD1;
	}
	if (bgmBank_[songNum] && mdataAddr_ >= 0) {
		unsigned avail = bgmBankSize_[songNum];
		/* DUMMY/tiny bgm (gra88 MDAT=1, castle DUMMY=0): music lives in
		   code ROMs; poking a stub into mdata only risks clobber. */
		if (avail < 16)
			avail = 0;
		/* When the same image is already mapped as code@mdata (navitune
		   navimus@7700), keep the full file — title bits 8..23 select the
		   song header via PATCH LD BC (ApplyNavituneTitleSong), not a slice. */
		unsigned stageOff = fileOff;
		if (armNavituneTimer_ && mdataAddr_ >= 0 && fileOff < avail) {
			naviSongAddr_ = (uint16_t)((unsigned)mdataAddr_ + fileOff);
			stageOff = 0;
		}
		if (stageOff < avail) {
			unsigned n = avail - stageOff;
			/* mdata_size = window placed at mdata_addr; mfile_size is the
			   on-demand BankCopy length and must not inflate this copy. */
			if (mdataSize_ > 0 && (unsigned)mdataSize_ < n)
				n = (unsigned)mdataSize_;
			else if (mdataSize_ <= 0 && mfileSize_ > 0 && (unsigned)mfileSize_ < n)
				n = (unsigned)mfileSize_;
			/* Table-index PATCH (ashe only): MUS is linked for a high page that
			   often overlaps DRIVER (0x7800). Stage at title mdata and
			   relocate absolute channel ptrs down to that base.
			   Must not run for other titleMdata peers (MMLEX/song<<8 cousins)
			   — InferMusLinkAddr + even-word reloc silenced ~30 OK titles. */
			int loadAddr = mdataAddr_;
			int linkBase = -1;
			const int asheHi = CEmuPc88AshePlayHi(mem_);
			if (asheHi && titleMdata >= 0) {
				linkBase = CEmuPc88InferMusLinkAddr(
					bgmBank_[songNum] + stageOff, avail - stageOff);
				/* MUS00/01 are linked at A000, flush against DRIVER@7800.
				   Staging at titleMdata 1000 leaves 1xxx ptrs (bit7 clear)
				   on a page the PATCH stack/IM2 can clobber. 93xx banks
				   overlap DRIVER — park them in the same A000 window. */
				if (linkBase >= 0xA000) {
					unsigned native = (unsigned)linkBase;
					if (native + n > 0x10000u)
						n = 0x10000u - native;
					if (n > 0)
						loadAddr = (int)native;
				} else if (linkBase >= 0x7800 && linkBase < 0xA000) {
					loadAddr = 0xA000;
					if ((unsigned)loadAddr + n > 0x10000u)
						n = 0x10000u - (unsigned)loadAddr;
				}
			}
			if (loadAddr + (int)n > 0x10000)
				n = (unsigned)(0x10000 - loadAddr);
			if (n > 0 && loadAddr >= 0)
				memcpy(mem_ + loadAddr, bgmBank_[songNum] + stageOff, n);
			/* ys2_88: also plant at PATCH LDIR dest (4D00/3000/2000) so
			   MANPR/TTL absolute phrase ptrs resolve before/without relying
			   solely on the guest C000→dest copy. */
			if (n > 0 && loadAddr == 0xc000
				&& CEmuPc88PatchFalcomLdirFromC000(mem_)) {
				const unsigned dest = CEmuPc88FalcomYs2MusDest(mem_, songNum);
				if (dest != (unsigned)loadAddr && dest < 0x10000u) {
					unsigned n2 = n;
					if (dest + n2 > 0x10000u)
						n2 = 0x10000u - dest;
					if (n2 > 0)
						memcpy(mem_ + dest, bgmBank_[songNum] + stageOff, n2);
				}
				CEmuPc88FalcomYs2PlantTable(mem_, songNum);
			}
			/* ApplyNavituneTitleSong runs after the first port-play (driver). */
			if (asheHi && n > 0 && loadAddr >= 0 && linkBase >= 0x2000
				&& linkBase != loadAddr) {
				/* Relocate only the ashe header(s) at the staged base.
				   Walking every offset + even-word phrase scans hit
				   attr_hi||next_ptr_lo (false 0x9A01) and A0xx note
				   bytes (MUS00 6.5K silent). MUS09 survived by luck. */
				const unsigned lb = (unsigned)linkBase;
				unsigned heads[2] = { 0, 0 };
				unsigned nHeads = 1;
				if (n >= 0x19 + 5
					&& mem_[loadAddr + 0x19] >= 3
					&& mem_[loadAddr + 0x19] <= 16)
					heads[nHeads++] = 0x19;
				for (unsigned h = 0; h < nHeads; h++) {
					const unsigned off = heads[h];
					const unsigned nch = mem_[loadAddr + (int)off];
					if (nch < 1 || nch > 16 || off + 1 + nch * 4 > n)
						continue;
					int ok = 1;
					for (unsigned c = 0; c < nch; c++) {
						const unsigned po = off + 1 + c * 4;
						const unsigned w = (unsigned)mem_[loadAddr + (int)po]
							| ((unsigned)mem_[loadAddr + (int)po + 1] << 8);
						if (w < lb || w >= lb + n) { ok = 0; break; }
					}
					if (!ok) continue;
					for (unsigned c = 0; c < nch; c++) {
						const unsigned po = off + 1 + c * 4;
						unsigned w = (unsigned)mem_[loadAddr + (int)po]
							| ((unsigned)mem_[loadAddr + (int)po + 1] << 8);
						if (w < lb || w >= lb + n) continue;
						w = w - lb + (unsigned)loadAddr;
						mem_[loadAddr + (int)po] = (uint8_t)(w & 0xff);
						mem_[loadAddr + (int)po + 1] = (uint8_t)((w >> 8) & 0xff);
					}
				}
				CEmuPc88AsheRelocPhrases(mem_, loadAddr, n, lb);
			}
			/* song<<8 + HL=4000: LDIR copies 4000 → titlepage. Mirror bank. */
			if (n > 0 && titleMdata >= 0 && CEmuPc88PatchLdirFrom4000(mem_)) {
				unsigned n4 = n;
				if (0x4000 + n4 > 0x10000)
					n4 = 0x10000 - 0x4000;
				if (n4 > 0)
					memcpy(mem_ + 0x4000, bgmBank_[songNum] + fileOff, n4);
			}
			/* ashe table-index: plant ptr + mirror relocated image to 0x4000.
			   DRIVER@80EE rejects song < 0x10; title high byte is the index. */
			if (asheHi && titleMdata >= 0) {
				unsigned plantIdx = (titleCode >> 24) & 0xffu;
				int plantAddr = loadAddr;
				/* Title high 0x11 selects the second ashe-style header
				   inside the bank (MUS09@+0x19); 0x10 uses the first. */
				if (plantIdx == 0x11 && n >= 0x19 + 5
					&& mem_[loadAddr + 0x19] >= 3
					&& mem_[loadAddr + 0x19] <= 16)
					plantAddr = loadAddr + 0x19;
				CEmuPc88PlantAsheSongTable(mem_, plantIdx, plantAddr);
				unsigned n4 = n;
				if (n4 > 0x2000) n4 = 0x2000;
				if (0x4000 + n4 > 0x10000) n4 = 0x10000 - 0x4000;
				if (n4 > 0)
					memcpy(mem_ + 0x4000, mem_ + loadAddr, n4);
			}
		}
	}
	/* manreq88/kissof88: vdata sits inside the mdata window (A9C @AD4E is
	   in A9A's 8D77..CA77 copy), so the BGM memcpy wipes the overlay that
	   actually selects M's BOOGIE / SILVER KNIFE. Voice-first is still
	   required for ys2 (voice BELOW mdata; music must win the overlap). */
	if (voiceBank_[songNum] && vdataAddr_ >= 0 && mdataAddr_ >= 0
		&& vdataAddr_ >= mdataAddr_) {
		const int mend = mdataAddr_
			+ (mdataSize_ > 0 ? mdataSize_
				: (mfileSize_ > 0 ? mfileSize_ : 0x1000));
		if (vdataAddr_ < mend) {
			unsigned n = voiceBankSize_[songNum];
			if (vfileSize_ > 0 && (unsigned)vfileSize_ < n)
				n = (unsigned)vfileSize_;
			if (vdataAddr_ + (int)n > 0x10000)
				n = (unsigned)(0x10000 - vdataAddr_);
			if (n > 0)
				memcpy(mem_ + vdataAddr_, voiceBank_[songNum], n);
			if (CEmuPc88PatchManreq(mem_) && vdataAddr_ + 7 < 0x10000
				&& mem_[vdataAddr_ + 6] == 0x3E && mem_[vdataAddr_ + 7] == 0xD0)
				mem_[vdataAddr_ + 7] = 0xD1;
		}
	}
}

/* FE19 DRIVER @ E000 (arugies/schwarz): arugies PATCH parks IM2 words at
   0004/0008 → E265/E2A6. schwarz/schwarz2 set I=01 but never write page-I
   vectors, so RTC/sound IRQs land on 0000 and playback stays silent. */
static void CEmuPc88InstallFe19Im2(uint8_t* mem, Ay_Cpu* cpu)
{
	if (!mem || !cpu)
		return;
	if (mem[0xE000] != 0xFE || mem[0xE001] != 0x19)
		return;
	const unsigned iPage = ((unsigned)cpu->r.i) << 8;
	if (iPage + 0x0A > 0x10000)
		return;
	static const uint8_t kVecs[] = { 0x02, 0x04, 0x08 };
	static const uint16_t kHandlers[] = { 0xE265, 0xE265, 0xE2A6 };
	for (int n = 0; n < 3; n++) {
		const unsigned p = iPage + kVecs[n];
		if (mem[p] == 0 && mem[p + 1] == 0) {
			mem[p] = (uint8_t)(kHandlers[n] & 0xff);
			mem[p + 1] = (uint8_t)(kHandlers[n] >> 8);
		}
	}
}

/* yokosuka: cmd=1 + param==FF → IN (80); LD (E23C),A (effect request).
   param!=FF → DI; CALL SOUND+0x1BD (BGM). Effect titles use low=FF and
   hi24 = effect id on port 80. */
static int CEmuPc88PatchYokosukaEff(const uint8_t* mem)
{
	if (!mem) return 0;
	for (int i = 0; i + 4 < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x3C && mem[i + 4] == 0xE2)
			return 1;
	}
	return 0;
}

/* smariosp: IN (80); OR A; JR NZ → CALL play; else IN (01); LD (mailbox),A.
   Port80 must be 0 for BGM so param lands in the song mailbox (makai-like). */
static int CEmuPc88PatchPort80GateParam(const uint8_t* mem)
{
	if (!mem) return 0;
	for (int i = 0; i + 12 < 0x80; i++) {
		if (mem[i] != 0xDB || mem[i + 1] != 0x80 || mem[i + 2] != 0xB7)
			continue;
		if (mem[i + 3] != 0x20)
			continue;
		/* XOR A; LD (nn),A; IN A,(01); LD (nn),A */
		int k = i + 5;
		if (k + 7 >= 0x80) continue;
		if (mem[k] == 0xAF && mem[k + 1] == 0x32
			&& mem[k + 4] == 0xDB && mem[k + 5] == 0x01
			&& mem[k + 6] == 0x32
			&& mem[k + 2] == mem[k + 7] && mem[k + 3] == mem[k + 8])
			return 1;
	}
	return 0;
}

/* 100yen4 (onion split@9000): IN A,(80); LD (9006),A; CALL 9000.
   Title hi24 is the in-bank song ('1'..); low byte is the MUS bank.
   HootCmd01 does not match (CALL 9007 sits between JR NZ and IN (01)),
   so PlaySongIndex used to return the bank id and every god2 title
   played song 3. The IN (80) lands at $25, not $26. */
static int CEmuPc88PatchOnion9006(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3)
		return 0;
	for (int i = 0; i + 7 < 0x40; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x06 && mem[i + 4] == 0x90
			&& mem[i + 5] == 0xCD && mem[i + 6] == 0x00 && mem[i + 7] == 0x90)
			return 1;
	}
	return 0;
}

/* pwmajan2: PATCH@F000 plants C9 over PROG4's EI at 9038 so CALL 9019
   returns before the disk loader. Port 01 is the MUS bank, port 80 the
   in-bank song (0 is a real opening). Poll stays DI — host must EI.
   LoadRoms NOPs CALL 9019; keep matching that form so PlaySongIndex,
   NeedsPlayEi and ArmPwmajan2 still see the title after the plant. */
static int CEmuPc88PatchPwmajan2(const uint8_t* mem)
{
	if (!mem || mem[0xF000] != 0xF3)
		return 0;
	if (!(mem[0xF00D] == 0x3E && mem[0xF00E] == 0xC9
		&& mem[0xF00F] == 0x32 && mem[0xF010] == 0x38 && mem[0xF011] == 0x90))
		return 0;
	if (mem[0xF012] == 0xCD && mem[0xF013] == 0x19 && mem[0xF014] == 0x90)
		return 1;
	if (mem[0xF012] == 0x00 && mem[0xF013] == 0x00 && mem[0xF014] == 0x00)
		return 1;
	return 0;
}

/* gandhara: IN (01) indexes 8-byte rows at 00BD; IN (80)==1 is a special
   LDIR and ==2 skips CALL 8C3D (muted). Port 80 must stay the title high
   byte (0 for 01/02/03 BGM) so every row takes LDDR + CALL 8C3D. */
static int CEmuPc88PatchGandhara(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3)
		return 0;
	return (mem[6] == 0x3E && mem[7] == 0x86
		&& mem[0x21] == 0xCD && mem[0x22] == 0x1E && mem[0x23] == 0x8C
		&& mem[0x2F] == 0x11 && mem[0x30] == 0xBD && mem[0x31] == 0x00) ? 1 : 0;
}

static int CEmuPc88PatchHootCmd01At(const uint8_t* mem, unsigned base)
{
	/* IN A,(0); OR A; JR Z,poll; CP 1; JR NZ,stop; IN A,(1)
	   rouge88, hchaser, blmnstry and pias88 hoist LD C,0 in front of the
	   port 01 read so the command latch can be cleared with OUT (C),C.
	   Accept that one insertion and nothing looser: the preamble alone is
	   near-universal on PC-88 rips (220 of them), and matching it loosely
	   would let this rule shadow every game-specific rule below it. */
	for (unsigned i = base; i < base + 0x60 && i + 12 < 0x10000; i++) {
		if (mem[i] != 0xDB || mem[i+1] != 0x00 || mem[i+2] != 0xB7
			|| mem[i+3] != 0x28 || mem[i+5] != 0xFE || mem[i+6] != 0x01
			|| mem[i+7] != 0x20)
			continue;
		if (mem[i+9] == 0xDB && mem[i+10] == 0x01)
			return 1;
		if (mem[i+9] == 0x0E && mem[i+10] == 0x00
			&& mem[i+11] == 0xDB && mem[i+12] == 0x01)
			return 1;
	}
	return 0;
}

/* iceclimb88: JR $10 PATCH, CALL B323, mailbox B816. */
static int CEmuPc88PatchIceclimb(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0x18 && mem[0x10] == 0xF3 && mem[0x11] == 0xED
		&& mem[0x16] == 0xCD && mem[0x17] == 0x23 && mem[0x18] == 0xB3
		&& mem[0x35] == 0x32 && mem[0x36] == 0x16 && mem[0x37] == 0xB8) ? 1 : 0;
}

/* Title (song 5) shares phrase bytes with looping boss (song 4) but the
   5-byte table flag is 00 so it plays once. NES title loops — same flag as
   通常BGM/boss. Round intro (song 1) stays finite. */
static void CEmuPc88PlantIceclimbTitleLoop(uint8_t* mem)
{
	if (!mem || !CEmuPc88PatchIceclimb(mem))
		return;
	if (mem[0xBB41] == 0x5F && mem[0xBB42] == 0xBB
		&& mem[0xBB55] == 0xE7 && mem[0xBB56] == 0xBD
		&& mem[0xBB59] == 0x00)
		mem[0xBB59] = 0x01;
}

/* manreq88: plant RET over disk hooks at 8892/92EA, I=BF. */
static int CEmuPc88PatchManreq(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0xF3 && mem[0x0B] == 0x3E && mem[0x0C] == 0xBF
		&& mem[0x0F] == 0x3E && mem[0x10] == 0xC9
		&& mem[0x11] == 0x32 && mem[0x12] == 0x92 && mem[0x13] == 0x88
		&& mem[0x14] == 0x32 && mem[0x15] == 0xEA && mem[0x16] == 0x92) ? 1 : 0;
}

/* d': IN (80) is 0-based phrase index; CALL 17B3. PATCH plants mdata at (808e). */
static int CEmuPc88PatchDprime(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0xF3 && mem[0x11] == 0x22
		&& mem[0x12] == 0x8E && mem[0x13] == 0x80
		&& mem[0x3F] == 0xCD && mem[0x40] == 0xB3 && mem[0x41] == 0x17) ? 1 : 0;
}

/* adrnalin: IN (80); CALL 9106. Driver DEC A then indexes word[9e00]. Title
   hi24 is the 1-based play id; the low byte only selects MUS0n. */
static int CEmuPc88PatchAdrnalin(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0x18 && mem[0x10] == 0xF3
		&& mem[0x2C] == 0xDB && mem[0x2D] == 0x80
		&& mem[0x2E] == 0xCD && mem[0x2F] == 0x06 && mem[0x30] == 0x91) ? 1 : 0;
}

static int CEmuPc88PatchHootCmd01(const uint8_t* mem, int initPc)
{
	/* The stock hoot PC-88 PATCH prologue, shared by ~134 rips (triton2,
	   goonies88, valis, sorc88…). Its three ports are fixed:
	     IN A,(00)  command, 1 = play (anything else takes the stop path)
	     IN A,(01)  song / bank number, echoed with OUT (01),A
	     IN A,(80)  the byte handed to the driver's play entry in A
	   So port 01 is the title low byte and port 80 the title high byte;
	   the signature says nothing about which song was asked for.
	   Half of these rips stage PATCH at init_pc rather than page 0
	   (sorc88 at C000, the F000 family, rouge88 at 1000), so the prologue
	   has to be looked for there too or they all fall through to the
	   generic rules and every song asks the driver for the same byte. */
	if (!mem) return 0;
	if (CEmuPc88PatchHootCmd01At(mem, 0))
		return 1;
	if (initPc > 0)
		return CEmuPc88PatchHootCmd01At(mem, (unsigned)initPc);
	return 0;
}

/* HootCmd01 requires IN (01) immediately after JR NZ. Many rips CALL stop
   first (xzr/xzr2/jikochu2/vaxol/wibarm/romanciasr). Port 01 is still the
   bank and port 80 the in-file index (title high byte). Without this, both
   ports got the low byte and disk-twin titles (00000001 vs 01000001) were
   SAMESONG / silent OOB seeks. */
static int CEmuPc88PatchPort01Then80(const uint8_t* mem, int initPc)
{
	if (!mem) return 0;
	auto scan = [](const uint8_t* mem, unsigned base) -> int {
		for (unsigned i = base; i + 12 < base + 0x70 && i + 12 < 0x10000u; i++) {
			if (mem[i] != 0xDB || mem[i + 1] != 0x00 || mem[i + 2] != 0xB7
				|| mem[i + 3] != 0x28 || mem[i + 5] != 0xFE || mem[i + 6] != 0x01
				|| mem[i + 7] != 0x20)
				continue;
			int saw01 = 0;
			const unsigned end = i + 8 + 56;
			for (unsigned k = i + 8; k + 1 < end && k + 1 < 0x10000u; k++) {
				if (mem[k] == 0xDB && mem[k + 1] == 0x01)
					saw01 = 1;
				if (saw01 && mem[k] == 0xDB && mem[k + 1] == 0x80)
					return 1;
			}
		}
		return 0;
	};
	if (scan(mem, 0))
		return 1;
	if (initPc > 0)
		return scan(mem, (unsigned)initPc);
	return 0;
}

/* mule: IN (01) indexes page table 004E (B0/B9/BB/BC); IN (80) is only
   RRA selecting ISR 8FE5 vs 8B51. High-byte-only titles handed port80=1
   (MAIN 01000000) and every song took the 8B51 path / same page. */
static int CEmuPc88PatchMulePages(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	if (!(mem[0x1C] == 0xDB && mem[0x1D] == 0x01
		&& mem[0x1E] == 0xFE && mem[0x1F] == 0x05))
		return 0;
	return (mem[0x25] == 0xDB && mem[0x26] == 0x80 && mem[0x27] == 0x1F) ? 1 : 0;
}

/* blmnstry/hchaser/rouge88/pias88: PATCH plants HL=4000 at the PMD mailbox
   (xx0F). Each bgm file is one MML staged at 4000; port 01 must stay 0 so
   PMD plays that file instead of seeking an in-file song index. */
static int CEmuPc88PatchPmdHl4000(const uint8_t* mem, int initPc)
{
	if (!mem) return 0;
	int pc = initPc;
	if (pc < 0) pc = 0;
	if (pc + 1 < 0x10000 && mem[pc] == 0x18)
		pc = pc + 2 + (int)(int8_t)mem[pc + 1];
	for (int i = pc; i + 5 < pc + 0x40 && i + 5 < 0x10000; i++) {
		if (mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0x40
			&& mem[i + 3] == 0x22 && mem[i + 4] == 0x0F)
			return 1;
	}
	return 0;
}

/* lvaccus: XOR A; CALL 9800 (stop); IN (01)=bank; IN (80)=in-bank id;
   CALL 9800 play. PATCH never EI — VRTC must come from the host. */
static int CEmuPc88PatchLvaccus9800(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	return (mem[0x18] == 0xCD && mem[0x19] == 0x00 && mem[0x1A] == 0x98
		&& mem[0x1B] == 0xDB && mem[0x1C] == 0x01
		&& mem[0x1F] == 0xDB && mem[0x20] == 0x80
		&& mem[0x21] == 0xCD && mem[0x22] == 0x00 && mem[0x23] == 0x98) ? 1 : 0;
}

/* xzr/xzr2: I=A4, IN (80); CALL A410. Each MA/MB file is one song staged at
   4000. Port01Then80 would hand the title high byte as the in-file index
   (MA000 01000015 → A=1 OOB; MA* headers are 01 42 = 1 song). */
static int CEmuPc88PatchXzrA4(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	if (!(mem[6] == 0x3E && mem[7] == 0xA4 && mem[8] == 0xED && mem[9] == 0x47))
		return 0;
	for (int i = 0x20; i + 4 < 0x50; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0xCD && mem[i + 3] == 0x10 && mem[i + 4] == 0xA4)
			return 1;
	}
	return 0;
}

/* xzr2: LDIR F000 → (A449) copies the 0x200 voice bank before CALL A410.
   Catalog has no vdata_addr, so VD* never reached F000 and MA* peaked 0. */
static int CEmuPc88PatchXzr2VoiceF000(const uint8_t* mem)
{
	if (!mem || !CEmuPc88PatchXzrA4(mem))
		return 0;
	for (int i = 0x20; i + 10 < 0x50; i++) {
		if (mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0xF0
			&& mem[i + 3] == 0xED && mem[i + 4] == 0x5B
			&& mem[i + 9] == 0xED && mem[i + 10] == 0xB0)
			return 1;
	}
	return 0;
}

/* ginei2: CP 40 / CP 20 select ITEST opening vs BGM. Each GEDS/BGM file is
   one song at 6390 with the same 06-channel header. Openings passed 0x21
   into CALL 159F after OPENING0 wiped ITEST@0400. Param 0 keeps CALL 52FA. */
static int CEmuPc88PatchGinei2(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	return (mem[0x3D] == 0xFE && mem[0x3E] == 0x40
		&& mem[0x41] == 0xFE && mem[0x42] == 0x20
		&& mem[0xB5] == 0x21 && mem[0xB6] == 0x90 && mem[0xB7] == 0x63) ? 1 : 0;
}

/* hardrank SMD-88: IN (01); CALL 8A00. Driver reads the header at 9300, not
   the catalog mdata 9200. Port 01 is unused (one file per song). */
static int CEmuPc88PatchSmd8A00(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3)
		return 0;
	if (!(mem[0x13] == 0xDB && mem[0x14] == 0x01
		&& mem[0x18] == 0xCD && mem[0x19] == 0x00 && mem[0x1A] == 0x8A))
		return 0;
	return (mem[0x8A00] == 0xF3) ? 1 : 0;
}

/* af ENDING/OPENING: param>=0x20 copies opdrv then IN (80); OR A; LD HL,4400
   / LD H,A. Title 00000021 put 0x21 on both ports → play at 2100. */
static int CEmuPc88PatchAfHl4400(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	if (!(mem[0x5D] == 0xFE && mem[0x5E] == 0x20))
		return 0;
	return (mem[0xB1] == 0xDB && mem[0xB2] == 0x80
		&& mem[0xB3] == 0xB7
		&& mem[0xB4] == 0x21 && mem[0xB5] == 0x00 && mem[0xB6] == 0x44) ? 1 : 0;
}

/* romanciasr: JP NZ (not JR) so Port01Then80 misses it. IN (80); LD (HL),A
   is the in-bank song id (00000001 vs 01000001). */
static int CEmuPc88PatchRomanciaSr(const uint8_t* mem, int initPc)
{
	if (!mem || initPc != 0xf000)
		return 0;
	if (!(mem[0xF024] == 0xDB && mem[0xF025] == 0x01))
		return 0;
	return (mem[0xF04D] == 0xDB && mem[0xF04E] == 0x80
		&& mem[0xF04F] == 0x77) ? 1 : 0;
}

/* robowr88: JR $10, IN (C=01), 6-byte rows at 005F. PROG2's play (CB5A)
   gates on (000A)==1; PATCH leftover is 02 so game-start stays mute. */
static int CEmuPc88PatchRobowr(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	return (mem[0x29] == 0x0E && mem[0x2A] == 0x01
		&& mem[0x2B] == 0xED && mem[0x2C] == 0x78
		&& mem[0x51] == 0xCD && mem[0x52] == 0x4A && mem[0x53] == 0xBA) ? 1 : 0;
}

/* arcus88demo (wolfteam 88/87 @B000): PATCH plants HL=4000 at (B000) and
   IN A,(80) into (B003). ISR ticks only while (B003)==0 or (B007)==0;
   FFFF phrase-end sets (B007)=FF, so a nonzero port 80 (title low byte)
   kills the player after the first loop — C5 silent after a short pass,
   C6 STOPS ~40s. Each bgm file is one song; port 80 must stay 0. */
static int CEmuPc88PatchArcusB000(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	if (!(mem[0x19] == 0x21 && mem[0x1A] == 0x00 && mem[0x1B] == 0x40
		&& mem[0x1C] == 0x22 && mem[0x1D] == 0x00 && mem[0x1E] == 0xB0))
		return 0;
	for (int i = 0x20; i + 5 < 0x50; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x03 && mem[i + 4] == 0xB0)
			return 1;
	}
	return 0;
}

/* yaksa PATCH2: plant (0575)=0x21; play does IN (01)/OR A/JR Z skip.
   Falcom-style vdata+voiceBank path must NOT force port01=0 (mute). */
int CEmuPc88PatchYaksa2(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	return (mem[0x10] == 0xF3 && mem[0x18] == 0x3E && mem[0x19] == 0x21
		&& mem[0x1A] == 0x32 && mem[0x1B] == 0x75 && mem[0x1C] == 0x05) ? 1 : 0;
}

/* lizard88: MAIN@9F00 decrypt + page0 Timer ISR @009C (PUSH AF). */
static int CEmuPc88PatchLizardTimer(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18 || mem[8] != 0x9C || mem[9] != 0x00)
		return 0;
	return (mem[0x9C] == 0xF5 && mem[0x9F00] == 0xC3) ? 1 : 0;
}

/* Three channels of 4-byte period/dur/wait events, FF-terminated, table
   at A576. One event per channel is a one-shot; two+ distinct periods on
   two+ channels is BGM. Same-pitch stings (0x41) must not JR 0051 or they
   become a held-tone drone. */
static int CEmuPc88LizardSongIsBgm(const uint8_t* mem, unsigned song)
{
	if (!mem || song > 0x4Cu)
		return 0;
	const unsigned tab = 0xA576u + song * 6u;
	if (tab + 6u >= 0x10000u)
		return 0;
	int variedCh = 0;
	for (int ch = 0; ch < 3; ch++) {
		unsigned p = (unsigned)mem[tab + (unsigned)ch * 2u]
			| ((unsigned)mem[tab + (unsigned)ch * 2u + 1u] << 8);
		if (p < 0x9F00u || p >= 0xC000u)
			continue;
		int n = 0, nSeen = 0;
		unsigned seen[8];
		while (n + 3 < 256 && p + (unsigned)n + 3u < 0x10000u
			&& mem[p + (unsigned)n] != 0xFF) {
			const unsigned per = (unsigned)mem[p + (unsigned)n]
				| ((unsigned)mem[p + (unsigned)n + 1u] << 8);
			int k = 0;
			for (; k < nSeen; k++) {
				if (seen[k] == per)
					break;
			}
			if (k == nSeen && nSeen < 8)
				seen[nSeen++] = per;
			n += 4;
		}
		if (n > 4 && nSeen >= 2)
			variedCh++;
	}
	return variedCh >= 2;
}

/* 1942_88: cmd=1 → CALL ADEE (LDIR MUSIC) then CALL A343 (I=80+Timer). */
static int CEmuPc88Patch1942LongPlay(const uint8_t* mem)
{
	if (!mem)
		return 0;
	/* PATCH plants C9 at A375 then CALL 0034 (ADEE) / CALL A343. */
	int sawRetPlant = 0, sawAdeee = 0, sawA343 = 0;
	for (int i = 0; i + 3 < 0x40; i++) {
		if (mem[i] == 0x3E && mem[i + 1] == 0xC9
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x75 && mem[i + 4] == 0xA3)
			sawRetPlant = 1;
		if (mem[i] == 0xCD && mem[i + 1] == 0x34 && mem[i + 2] == 0x00)
			sawAdeee = 1;
		if (mem[i] == 0xCD && mem[i + 1] == 0x43 && mem[i + 2] == 0xA3)
			sawA343 = 1;
	}
	return sawRetPlant && sawAdeee && sawA343;
}

/* makai88: LD IX,0274 / LD IY,0276. Play does IN A,(80); OR A; IN A,(01);
   JR NZ → (IY)=param (SE), else (IX)=param (BGM). IN A,(n) leaves flags
   alone, so port80 selects the mailbox and port01 is the song id.
   Title hi24 = SE(nonzero)/BGM(0); low byte = song number. */
static int CEmuPc88PatchMakaiIxIy(const uint8_t* mem)
{
	if (!mem) return 0;
	int sawIx = 0, sawIy = 0;
	for (int i = 0; i + 3 < 0x80; i++) {
		if (mem[i] == 0xDD && mem[i + 1] == 0x21
			&& mem[i + 2] == 0x74 && mem[i + 3] == 0x02)
			sawIx = 1;
		if (mem[i] == 0xFD && mem[i + 1] == 0x21
			&& mem[i + 2] == 0x76 && mem[i + 3] == 0x02)
			sawIy = 1;
	}
	if (!sawIx || !sawIy)
		return 0;
	for (int i = 0; i + 12 < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0xB7
			&& mem[i + 3] == 0xDB && mem[i + 4] == 0x01
			&& mem[i + 5] == 0x20
			&& mem[i + 7] == 0xDD && mem[i + 8] == 0x77
			&& mem[i + 10] == 0xC9
			&& mem[i + 11] == 0xFD && mem[i + 12] == 0x77)
			return 1;
	}
	return 0;
}

/* PATCH play: IN A,(80); LD D,A; LD E,0; … LDIR uses DE = song<<8 as the
   far address (galfstrm/meltdown HL=4000→DE, duel HL=1000→DE). Port 80 must
   be the page high byte from title hi16. */
static int CEmuPc88PatchSongShift8(const uint8_t* mem)
{
	if (!mem) return 0;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] != 0xDB || mem[i + 1] != 0x80)
			continue;
		int sawDe = 0, sawLdir = 0;
		for (int k = i; k < i + 32 && k + 2 < 0x100; k++) {
			if (mem[k] == 0x57 && mem[k + 1] == 0x1E && mem[k + 2] == 0x00)
				sawDe = 1; /* LD D,A; LD E,0 */
			if (mem[k] == 0xED && mem[k + 1] == 0xB0)
				sawLdir = 1;
		}
		if (sawDe && sawLdir)
			return 1;
	}
	return 0;
}

/* galfstrm/meltdown: LDIR source is HL=4000 (dst DE=titlepage). */
static int CEmuPc88PatchLdirFrom4000(const uint8_t* mem)
{
	if (!mem || !CEmuPc88PatchSongShift8(mem))
		return 0;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] != 0xDB || mem[i + 1] != 0x80)
			continue;
		for (int k = i; k < i + 32 && k + 2 < 0x100; k++) {
			if (mem[k] == 0x21 && mem[k + 1] == 0x00 && mem[k + 2] == 0x40)
				return 1;
		}
	}
	return 0;
}

/* Falcom YS/Ys2 PATCH: IN A,(01); AND F0; CP 10 → TTL / MANPR / END. */
static int CEmuPc88PatchFalcomAndF0Cp10(const uint8_t* mem)
{
	if (!mem)
		return 0;
	for (int i = 0; i + 3 < 0x80; i++) {
		if (mem[i] == 0xE6 && mem[i + 1] == 0xF0
			&& mem[i + 2] == 0xFE && mem[i + 3] == 0x10)
			return 1;
	}
	return 0;
}

/* ys2_88: LD HL,C000 / LD BC,1000 / LDIR with DE = bank table+6
   (TTL→3000, MANPR→4D00, END→2000). Song files are linked for that
   dest; staging only at catalog mdata=C000 leaves MANPR reading empty
   4D00 until PATCH runs — and a wrong param never reaches that LDIR. */
static int CEmuPc88PatchFalcomLdirFromC000(const uint8_t* mem)
{
	if (!mem)
		return 0;
	for (int i = 0; i + 7 < 0x80; i++) {
		if (mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0xC0
			&& mem[i + 3] == 0x01 && mem[i + 4] == 0x00 && mem[i + 5] == 0x10
			&& mem[i + 6] == 0xED && mem[i + 7] == 0xB0)
			return 1;
	}
	return 0;
}

static unsigned CEmuPc88FalcomYs2MusDest(const uint8_t* mem, unsigned bank)
{
	const unsigned nibble = bank & 0xf0u;
	int table = 0x61;
	if (nibble == 0x10u)
		table = 0x69;
	else if (nibble > 0x10u)
		table = 0x71;
	if (mem && table + 7 < 0x100) {
		const unsigned d = (unsigned)mem[table + 6]
			| ((unsigned)mem[table + 7] << 8);
		if (d >= 0x200u && d < 0xF000u)
			return d;
	}
	if (nibble == 0x10u)
		return 0x4d00u;
	if (nibble > 0x10u)
		return 0x2000u;
	return 0x3000u;
}

/* PATCH always CALL (0079) stop before re-reading param. The ROM image
   leaves TTL vectors there; ENDPRG only covers ~0100..20FF so a TTL stop
   at 2D9E runs leftover TTLPRG and corrupts the END driver. */
static void CEmuPc88FalcomYs2PlantTable(uint8_t* mem, unsigned bank)
{
	if (!mem || !CEmuPc88PatchFalcomLdirFromC000(mem))
		return;
	const unsigned nibble = bank & 0xf0u;
	int table = 0x61;
	if (nibble == 0x10u)
		table = 0x69;
	else if (nibble > 0x10u)
		table = 0x71;
	memcpy(mem + 0x79, mem + table, 6);
}

/* ashe DRIVER@80EE: CP 10 / JP NC — request bytes in 7803..05 must be
   >= 0x10. Title high byte (0x10/0x11) is the 9152 table index; low byte
   only selects which MUS* bank was staged. */
static int CEmuPc88AshePlayHi(const uint8_t* mem)
{
	if (!mem || mem[0x7800] != 0xC3)
		return 0;
	if (mem[0x80EE] != 0x36 || mem[0x80EF] != 0x00
		|| mem[0x80F0] != 0xFE || mem[0x80F1] != 0x10)
		return 0;
	/* 80F6: LD L,A / LD H,0 / ADD HL,HL / LD DE,9152 */
	return (mem[0x80FA] == 0x11 && mem[0x80FB] == 0x52
		&& mem[0x80FC] == 0x91) ? 1 : 0;
}

/* ashe PATCH/DRIVER: song*2 indexes a word table (LD DE,9152), then LDIR
   that pointer → 0x4000. ROM table is junk; host must plant the staged
   mdata address for the selected song. */
static void CEmuPc88PlantAsheSongTable(uint8_t* mem, unsigned songNum, int mdataAddr)
{
	if (!mem || mdataAddr < 0 || mdataAddr >= 0x10000 || songNum > 0x1e)
		return;
	int table = -1;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80) {
			for (int k = i; k < i + 40 && k + 2 < 0x100; k++) {
				if (mem[k] == 0x11) {
					const unsigned de = (unsigned)mem[k + 1]
						| ((unsigned)mem[k + 2] << 8);
					/* ashe uses 9152; accept any table in DRIVER ROM page. */
					if (de >= 0x7800 && de < 0xA000) {
						table = (int)de;
						break;
					}
				}
			}
		}
		if (table >= 0) break;
	}
	/* DRIVER mirror of the same table (ashe @80FB). */
	if (table < 0) {
		for (int i = 0x8000; i < 0x8200; i++) {
			if (mem[i] == 0x11) {
				const unsigned de = (unsigned)mem[i + 1]
					| ((unsigned)mem[i + 2] << 8);
				if (de >= 0x7800 && de < 0xA000
					&& mem[i - 2] == 0x29 && mem[i - 3] == 0x00)
					table = (int)de;
			}
			if (table >= 0) break;
		}
	}
	if (table < 0 || table + (int)songNum * 2 + 1 >= 0x10000)
		return;
	const int p = table + (int)songNum * 2;
	mem[p] = (uint8_t)(mdataAddr & 0xff);
	mem[p + 1] = (uint8_t)((mdataAddr >> 8) & 0xff);
}

uint8_t CHardPc88::PlaySongIndex() const
{
	/* yaksa PATCH2: IN (01)/OR A/JR Z skips play when param==0. Title
	   0xPP00BB uses mid byte as play id (0x030002→03); never return 0. */
	if (yaksaPatch2_ || CEmuPc88PatchYaksa2(mem_)) {
		const unsigned mid = (titleCode_ >> 16) & 0xff;
		const unsigned songNum = titleCode_ & 0xff;
		if (mid != 0)
			return (uint8_t)mid;
		if (songNum != 0)
			return (uint8_t)songNum;
		return 1;
	}
	/* mule: IN (80)/RRA patches CALL 8D05. Odd (MAIN 01000000) keeps
	   CALL 8B51; even leaves CALL 8FE5 which OR 80's port 32 (IRQ mask).
	   Page select is port 01 = title low byte, not this. */
	if (CEmuPc88PatchMulePages(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* PMD@4000: one MML per file. */
	if (CEmuPc88PatchPmdHl4000(mem_, initPc_))
		return 0;
	if (CEmuPc88PatchXzrA4(mem_))
		return 0;
	if (CEmuPc88PatchGinei2(mem_))
		return 0;
	if (CEmuPc88PatchAfHl4400(mem_) || CEmuPc88PatchRomanciaSr(mem_, initPc_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchLvaccus9800(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* Stock hoot PATCH: port 80 is the byte passed to the driver's play
	   entry, which these rips encode as the title high byte (triton2 effect
	   id, goonies88 song, valis opening variant). */
	if (CEmuPc88PatchHootCmd01(mem_, initPc_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchDprime(mem_) || CEmuPc88PatchAdrnalin(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchOnion9006(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchPwmajan2(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchGandhara(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* makai88: port80 = BGM/SE select (title hi24), not the song number. */
	if (CEmuPc88PatchMakaiIxIy(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* smariosp: port80!=0 skips mailbox plant (CALL play without song id). */
	if (CEmuPc88PatchPort80GateParam(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* yokosuka effects: low=FF, hi24 = id written to (E23C) via port 80. */
	if (CEmuPc88PatchYokosukaEff(mem_) && (titleCode_ & 0xffu) == 0xffu)
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* gineiden: each BGM_n.COM is one song staged at mdata; AMAIN indexes
	   with B*6 from IN (80). Title low byte selected the bank — port 80
	   must be 0 or play skips into the channel table and stays mute. */
	if (armGineidenTimer_)
		return 0;
	if (CEmuPc88PatchArcusB000(mem_))
		return 0;
	/* yakyufan: each MUS* is one bank staged at 0x4000; (010B)/port80 is an
	   in-file song index. Passing the bank id (0..0x0A) seeks past the only
	   phrase table → key-ons with muted TL. */
	if (NeedsYakyufanArm())
		return 0;
	if (CEmuPc88PatchPort01Then80(mem_, initPc_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (packedKoei_) {
		/* valis2 OPNA: PATCH routes param>=0xE0 to ADPCM voice (PCM00..).
		   BGM still plays with index 0 into the packed CIM at 0x4000. */
		const unsigned songNum = titleCode_ & 0xff;
		if (songNum >= 0xE0)
			return (uint8_t)songNum;
		return 0;
	}
	const unsigned songNum = titleCode_ & 0xff;
	const int titleMdata = CEmuPc88TitleEncodedMdata(titleCode_, mdataAddrDefaulted_, wolfteamMode_);
	/* song<<8 LDIR PATCHes: port 80 is the page high byte (title hi16). */
	if (CEmuPc88PatchSongShift8(mem_)) {
		unsigned hi = 0;
		if (titleMdata >= 0)
			hi = (unsigned)titleMdata;
		else {
			const unsigned tHi = (titleCode_ >> 16) & 0xffffu;
			if (tHi >= 0x1000 && tHi <= 0xE000 && (tHi & 0xffu) == 0
				&& (titleCode_ & 0xff00u) == 0)
				hi = tHi;
		}
		if (hi != 0)
			return (uint8_t)((hi >> 8) & 0xff);
	}
	/* ashe: high byte is the 9152/7803 play index (>=0x10); low = MUS bank. */
	if (CEmuPc88AshePlayHi(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* Title hi16 was the load address (MMLEX/galfstrm). Other table-index
	   PATCHes keep the low-byte bank index. */
	if (titleMdata >= 0)
		return (uint8_t)songNum;
	/* FE19/kogado (arugies/schwarz): title high byte is the in-bank song
	   index; low byte only selects which MUS* bank was staged at mdata. */
	if (mem_[0xE000] == 0xFE && mem_[0xE001] == 0x19)
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* Falcom YS/Ys2-style (vdata voice + MUS bank, no mfile_size): each MUS
	   is one song; title hi24 = play/param, lo = bank already staged.
	   Returning the bank id as play index seeks nonsense phrases (ch1-only /
	   SSG mismatch). manreq88 keeps low-byte songs via mfile_size>0.
	   Exception: ssymphony PATCH does CP 1 / IN (01) and skips CALL when
	   param==0 — bare titles 0x01/0x02 need the bank id on port 01. */
	if (vdataAddr_ >= 0 && mfileSize_ <= 0 && songNum < 256 && voiceBank_[songNum]) {
		const unsigned hi = (titleCode_ >> 24) & 0xff;
		if (hi != 0)
			return (uint8_t)hi;
		if (mem_) {
			for (int i = 0; i + 8 < 0x50; i++) {
				if (mem_[i] != 0xFE || mem_[i + 1] != 0x01)
					continue;
				/* CP 1; JR NZ; …; IN A,(01) — ssymphony leaves DI then IN. */
				for (int k = i + 2; k + 1 < i + 12 && k + 1 < 0x50; k++) {
					if (mem_[k] == 0xDB && mem_[k + 1] == 0x01)
						return (uint8_t)songNum;
				}
			}
		}
		return 0;
	}
	/* Pointer-table banks (Herzog/Tecnosoft, tenchi, triton2): word[mdata+2]
	   is an absolute pointer into the staged bank. Play index is the title
	   high byte. ys3_88 also has in-bank pointers but plays by low-byte song
	   number when high==0 and there is no mfile_size. */
	if (mdataAddr_ >= 0 && mdataAddr_ + 3 < 0x10000) {
		const unsigned p = (unsigned)mem_[mdataAddr_ + 2]
			| ((unsigned)mem_[mdataAddr_ + 3] << 8);
		const unsigned mend = (unsigned)mdataAddr_
			+ (unsigned)(mdataSize_ > 0 ? mdataSize_ : 0x1000);
		/* Strict > mdata: lyrane/OP images have word[2]==mdata (ld sp,mdata). */
		if (p > (unsigned)mdataAddr_ && p < mend) {
			const unsigned hi = (titleCode_ >> 24) & 0xff;
			/* Prefer title high-byte play index when set (Herzog/Tecnosoft).
			   manreq88: mfile_size>0 + in-bank pointers but songs are the
			   low byte — returning hi=0 muted every title. */
			if (hi != 0)
				return (uint8_t)hi;
			return (uint8_t)songNum;
		}
	}
	/* Title whose only non-zero field is the high byte (adrnalin MUS00-02…,
	   dione, prontis EFFECT #nn): the low byte names the staged bank, which
	   is bank 0, and the high byte is the play index the PATCH hands the
	   driver via port 80. Returning the low byte asks every such title for
	   song 0, and these drivers read song 0 as stop. */
	if (songNum == 0 && ((titleCode_ >> 8) & 0xffffu) == 0
		&& ((titleCode_ >> 24) & 0xffu) != 0)
		return (uint8_t)((titleCode_ >> 24) & 0xffu);
	return (uint8_t)songNum;
}

uint8_t CHardPc88::PlayParamIndex() const
{
	if (CEmuPc88PatchMulePages(mem_) || CEmuPc88PatchLvaccus9800(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* PMD mailbox at 4000: catalog low byte already selected the staged file. */
	if (CEmuPc88PatchPmdHl4000(mem_, initPc_))
		return 0;
	if (CEmuPc88PatchGinei2(mem_) || CEmuPc88PatchXzrA4(mem_))
		return 0;
	if (CEmuPc88PatchAfHl4400(mem_) || CEmuPc88PatchRomanciaSr(mem_, initPc_))
		return (uint8_t)(titleCode_ & 0xff);
	/* Stock hoot PATCH: port 01 is the song/bank number it echoes to the
	   driver mailbox. Falling through to PlaySongIndex() here handed every
	   one of those rips the port-80 play byte instead of the song. */
	if (CEmuPc88PatchHootCmd01(mem_, initPc_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchPort01Then80(mem_, initPc_))
		return (uint8_t)(titleCode_ & 0xff);
	/* 1942_88: IN A,(01) → (829C) song id. HootCmd01 does not match
	   (CALL ADEE between JR NZ and IN 01). */
	if (CEmuPc88Patch1942LongPlay(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchDprime(mem_) || CEmuPc88PatchAdrnalin(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchOnion9006(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchPwmajan2(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchGandhara(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* makai88: port01 = song id written to (0274) BGM or (0276) SE. */
	if (CEmuPc88PatchMakaiIxIy(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* smariosp: port01 = song id for LD (mailbox),A when port80==0. */
	if (CEmuPc88PatchPort80GateParam(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* yokosuka: low=FF selects the effect mailbox path (param must stay FF). */
	if (CEmuPc88PatchYokosukaEff(mem_) && (titleCode_ & 0xffu) == 0xffu)
		return 0xff;
	/* gineiden: param!=0 selects AMAIN vs DEMO jump patches; keep title. */
	if (armGineidenTimer_)
		return (uint8_t)(titleCode_ & 0xff);
	/* Falcom YS/Ys2 PATCH (AND F0 / CP 10): port 01 is the bank id that
	   selects TTL (<10) / MANPR (==10) / END (>10). Title hi24 is the
	   in-bank page on port 80 only. Returning the page as param sent every
	   *MUS bank down the TTL jump → silent or wrong driver. */
	if (CEmuPc88PatchFalcomAndF0Cp10(mem_) && vdataAddr_ >= 0 && mfileSize_ <= 0) {
		const unsigned bank = titleCode_ & 0xffu;
		if (bank < 256u && voiceBank_[bank])
			return (uint8_t)bank;
	}
	/* navitune-class: low byte is bank 0, bits 8..23 file offset, bits 24..31
	   play mode on port 01 (PATCH IN A,(01) after LDIR). */
	if ((titleCode_ & 0xffu) == 0 && ((titleCode_ >> 8) & 0xffffu) != 0
		&& mdataAddr_ >= 0 && mfileSize_ > 0)
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* song<<8: port80 = page, port01 = title low byte (duel gates on param). */
	if (CEmuPc88PatchSongShift8(mem_)) {
		const int titleMdata = CEmuPc88TitleEncodedMdata(
			titleCode_, mdataAddrDefaulted_, wolfteamMode_);
		const unsigned tHi = (titleCode_ >> 16) & 0xffffu;
		if (titleMdata >= 0
			|| (tHi >= 0x1000 && tHi <= 0xE000 && (tHi & 0xffu) == 0
				&& (titleCode_ & 0xff00u) == 0))
			return (uint8_t)(titleCode_ & 0xff);
	}
	/* High-byte-only titles: PlaySongIndex() hands the high byte to port 80,
	   but port 01 stays the bank id. goonies88's PATCH takes its stop branch
	   unless port 01 is 0, and adrnalin only echoes port 01 back out. */
	if ((titleCode_ & 0xffu) == 0 && ((titleCode_ >> 8) & 0xffffu) == 0)
		return 0;
	return PlaySongIndex();
}

void CHardPc88::FixupIm2AfterBoot()
{
	CEmuPc88InstallFe19Im2(mem_, cpu_);
}

/* The catalog says which clocks the machine offers; the stub's IM2 table says
   which ones the player actually listens to. Once both are known, drop a
   catalog clock whose vector is empty — delivering it would only cost the
   guest spurious ISR entries — but never drop the last one standing, and
   never guess when the table has not been planted yet. */
void CHardPc88::PruneDeadTickSources()
{
	if (!mem_ || !cpu_ || cpu_->r.im != 2)
		return;
	if (!useVrtc || !useRtc)
		return;
	const unsigned iBase = ((unsigned)cpu_->r.i) << 8;
	if (iBase + 9 >= 0x10000)
		return;
	const unsigned vrtc = (unsigned)mem_[iBase + 2] | ((unsigned)mem_[iBase + 3] << 8);
	const unsigned rtc = (unsigned)mem_[iBase + 4] | ((unsigned)mem_[iBase + 5] << 8);
	if (vrtc == 0 && rtc != 0)
		useVrtc = 0;
	else if (rtc == 0 && vrtc != 0)
		useRtc = 0;
}

void CHardPc88::FixupIm2AfterPlay()
{
	/* lvaccus play plants I=1 / vec02=$9803. VRTC was held off through
	   settle so I=0 would not fetch 0002 (ED 5E) and wander into PROG. */
	if (CEmuPc88PatchLvaccus9800(mem_) && cpu_ && cpu_->r.i == 1)
		useVrtc = 1;
	/* mule 8B01 plants I=5F / vec02=8EB5 (SSG) / vec08=8B9E (FM). Timer B
	   is the FM clock; VRTC still drives 8EB5. Held off through settle. */
	if (CEmuPc88PatchMulePages(mem_) && cpu_ && cpu_->r.i == 0x5F)
		useVrtc = 1;
	if (CEmuPc88PatchXzrA4(mem_) && cpu_ && cpu_->r.i == 0xA4)
		useVrtc = 1;
	if (!mirrorSoundToRtc_ || !cpu_ || !mem_)
		return;
	const unsigned iBase = ((unsigned)cpu_->r.i) << 8;
	if (iBase + 9 >= 0x10000)
		return;
	const unsigned snd = (unsigned)mem_[iBase + 8]
		| ((unsigned)mem_[iBase + 9] << 8);
	const unsigned rtc = (unsigned)mem_[iBase + 4]
		| ((unsigned)mem_[iBase + 5] << 8);
	if (snd < 0x40 || snd >= 0x10000)
		return;
	if (rtc != 0)
		return;
	mem_[iBase + 4] = (uint8_t)(snd & 0xff);
	mem_[iBase + 5] = (uint8_t)(snd >> 8);
}

void CHardPc88::ArmN88RtcPlayer()
{
	/* No full game body: DEMOM/MUSIC still know the RTC ISR address they
	   plant at I:04 (F304 with I=F3). Port-play often misses that plant or
	   leaves the E80E stop stub — put the layout back before the first tick. */
	if (!mem_ || !cpu_ || n88RtcIsr_ == 0)
		return;
	if (cpu_->r.i != 0xF3)
		return;
	if (n88RtcIsr_ >= 0x10000 || mem_[n88RtcIsr_] != 0xF5)
		return; /* ISR image missing — do not invent a vector */
	mem_[0xF304] = (uint8_t)(n88RtcIsr_ & 0xff);
	mem_[0xF305] = (uint8_t)(n88RtcIsr_ >> 8);
	if (n88RtcThrottleAddr_ && n88RtcThrottleAddr_ < 0x10000)
		mem_[n88RtcThrottleAddr_] = 0x01;
	/* Keep SP off the IM2 page — play paths that leave SP near F3xx let the
	   next IRQ push smash F304 back to a junk ISR address. */
	if (cpu_->r.sp >= 0xF000 || cpu_->r.sp < 0x0100)
		cpu_->r.sp = 0x0200;
	cpu_->r.iff1 = 1;
}

void CHardPc88::ArmGineidenOpnTimer()
{
	if (!armGineidenTimer_ || !chip_ || !cpu_)
		return;
	/* Match AMAIN@4E1B: OUT 44,27 / OUT 45,2A then unmask port 32. */
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x2A);
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
	cpu_->r.iff1 = 1;
}

void CHardPc88::ArmFallbackOpnTimer()
{
	if (!chip_ || !cpu_)
		return;
	/* Watchdog last resort: a rip whose boot never programmed any tick source
	   still has a vector 08 handler, so give it a ~71Hz Timer B (the rate the
	   stock PC-88 players use) and open the port 32 gate. */
	PortOut(0x44, 0x26);
	PortOut(0x45, 0xCF);
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x3A);
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
}

void CHardPc88::ArmLizardOpnTimer()
{
	if (!armLizardTimer_ || !chip_ || !cpu_)
		return;
	/* PATCH@002A (after XOR-decrypt MAIN) plants JP 00B2 over A3DF so
	   the duration loop waits on Timer B via (00B1). Without that JP
	   A3B0 burns the whole song in one CALL and PATCH hits STOP. */
	if (mem_ && mem_[0x9F00] == 0xC3 && mem_[0x00B2] == 0xF3
		&& mem_[0x00B3] == 0x3E) {
		mem_[0xA3DF] = 0xC3;
		mem_[0xA3E0] = 0xB2;
		mem_[0xA3E1] = 0x00;
	}
	/* PATCH play: CALL 9F0F then a busy delay and CALL 9F12 STOP.
	   BGM streams end with FF so A3B0 returns after one pass — JR 0051
	   at 0061 (over LD B,2 delay) replays them. One-tick SFX (4 bytes
	   per channel) must keep STOP or they become a held-tone drone. */
	if (mem_ && mem_[0x0051] == 0xD5 && mem_[0x0052] == 0xCD
		&& mem_[0x0061] == 0x06 && mem_[0x0062] == 0x02
		&& CEmuPc88LizardSongIsBgm(mem_, titleCode_ & 0xffu)) {
		mem_[0x0061] = 0x18;
		mem_[0x0062] = 0xEE; /* JR 0051 */
	}
	/* Match PATCH@0077: Timer B load 0x69, mode 0x3A, unmask port32, EI. */
	PortOut(0x44, 0x26);
	PortOut(0x45, 0x69);
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x3A);
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
	/* A572 must stay 0. It is the "old BASIC ROM" flag A4E7 plants, and the
	   stop routine reads it: zero takes A4D3, which writes OPN 07-0E from the
	   FF 00 00 … table (mixer off, all volumes down); anything else takes
	   A4CD, which only pokes port 40h and leaves the tone sounding. Forcing
	   it to 1 here is what left every lizard88 title droning after its last
	   note — 63 of the 211 stuck-note failures in the sweep. */
	cpu_->r.iff1 = 1;
}

void CHardPc88::ArmYaksaPlay()
{
	if (!mem_ || !(yaksaPatch2_ || CEmuPc88PatchYaksa2(mem_)))
		return;
	/* 0C6A: LD A,1 / LD (37D1),A — sequencer enable for CALL 3556. */
	mem_[0x37D1] = 1;
}

void CHardPc88::ArmPwmajan2()
{
	if (!mem_ || !cpu_ || !CEmuPc88PatchPwmajan2(mem_))
		return;
	/* 9019: I=0, IM2 word 0004 = 2060 (PROG2 ISR), IRQ level 1. */
	mem_[0x0004] = 0x60;
	mem_[0x0005] = 0x20;
	cpu_->r.i = 0;
	cpu_->r.im = 2;
	cpu_->r.iff1 = 1;
	/* 9019 OR 80 masks sound IRQ for the disk loader; we need it live.
	   Plant the same ISR on the OPN slot — 1072 never arms Timer B. */
	mem_[0x0008] = 0x60;
	mem_[0x0009] = 0x20;
	PortOut(0x32, (uint8_t)(PortIn(0x32) & 0x5F));
	PortOut(0xE6, 1);
	PortOut(0xE4, 3);
	PortOut(0x44, 0x26);
	PortOut(0x45, 0xCF);
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x2A);
	soundIrqMasked = 0;
}

void CHardPc88::PrepareNavitunePatch()
{
	/* Hoot PATCH gaps (sound-only, no game body):
	   1) DI before play / never EI — OPN IM2 never runs.
	   2) Stock play path is cmd07+cmd0E; title needs cmd10 after cmd07.
	   3) Poll counter is LD HL,017D / INC (HL) — move to 01FF before
	      expanding play bytes, then relocate the stop stub + JR NZ. */
	if (!armNavituneTimer_ || !mem_)
		return;
	const unsigned base = (initPc_ > 0) ? (unsigned)initPc_ : 0;
	const unsigned end = base + 0x100u;
	for (unsigned a = base; a + 2u < end && a + 2u < 0x10000u; a++) {
		if (mem_[a] == 0xF3 && mem_[a + 1] == 0xDB && mem_[a + 2] == 0x80) {
			mem_[a] = 0xFB;
			break;
		}
	}
	/* Play LDIR DE=4D00 BC=3000 copies C000/1000 onto 4D00-7CFF. naviprg is
	   2C00 bytes so dest 7700-78FF is the driver/music overlap and 7900-7CFF
	   is unmapped source — BGM 00/01 headers and 79xx phrases die, BGM 03
	   (813A) survives. Stop at 7700 (navimus). */
	for (unsigned a = base; a + 8u < end && a + 8u < 0x10000u; a++) {
		if (mem_[a] == 0x11 && mem_[a + 1] == 0x00 && mem_[a + 2] == 0x4D
			&& mem_[a + 3] == 0x01 && mem_[a + 4] == 0x00 && mem_[a + 5] == 0x30
			&& mem_[a + 6] == 0xED && mem_[a + 7] == 0xB0) {
			mem_[a + 4] = 0x00;
			mem_[a + 5] = 0x2A;
			break;
		}
	}
	/* F2 with empty return stack (5752) zeros (52CF) and the tick at 4EC0
	   skips the sequencer. BGM 01's list points a channel at a bare F2
	   (7777, one byte before the next phrase) so the whole song dies after
	   the initial IX+6 delay. End that channel only. Patch both copies;
	   play LDIR refreshes 4D00 from C000. */
	if (mem_[0x576E] == 0x21 && mem_[0x576F] == 0x00 && mem_[0x5770] == 0x00
		&& mem_[0x5771] == 0x22 && mem_[0x5772] == 0xCF && mem_[0x5773] == 0x52) {
		memset(mem_ + 0x576E, 0x00, 6);
		if (mem_[0xCA6E] == 0x21)
			memset(mem_ + 0xCA6E, 0x00, 6);
		if (mem_[0x1A6E] == 0x21)
			memset(mem_ + 0x1A6E, 0x00, 6);
	}
	for (unsigned a = base; a + 2u < end && a + 2u < 0x10000u; a++) {
		if (mem_[a] == 0x21 && mem_[a + 1] == 0x7D && mem_[a + 2] == 0x01) {
			mem_[a + 1] = 0xFF;
			break;
		}
	}
	ApplyNavituneTitleSong();
	/* After dedicated cmd10 play, JR poll without clearing port 00 retriggers
	   play every tight loop and the song never stays in the ISR. Plant XOR A;
	   OUT (00),A then JR poll in the old 017D counter byte (INC moved to 01FF). */
	for (unsigned a = base; a + 6u < end && a + 6u < 0x10000u; a++) {
		if (!(mem_[a] == 0x3E && mem_[a + 1] == 0x10
			&& mem_[a + 2] == 0xCD && mem_[a + 3] == 0x00 && mem_[a + 4] == 0x4D
			&& mem_[a + 5] == 0x18))
			continue;
		const unsigned cave = base + 0x7Du;
		if (cave + 5u >= 0x10000u)
			break;
		const int toCave = (int)cave - (int)(a + 5u + 2u);
		const int toPoll = (int)(base + 0x29u) - (int)(cave + 5u);
		if (toCave < -128 || toCave > 127 || toPoll < -128 || toPoll > 127)
			break;
		mem_[a + 6] = (uint8_t)toCave;
		mem_[cave] = 0xAF;
		mem_[cave + 1] = 0xD3;
		mem_[cave + 2] = 0x00;
		mem_[cave + 3] = 0x18;
		mem_[cave + 4] = (uint8_t)toPoll;
		break;
	}
	/* Play path already LD A,10 / CALL 4D00 then JR poll. Splicing cmd10
	   into init (cmd07 + LD BC,song + cmd0E) starts the song and cmd0E
	   immediately stops it — the only audible result was a few-ms click.
	   DirectPlayKick of that same init is also skipped (NavituneRetargetPc). */
	for (unsigned a = base; a + 4u < end && a + 4u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x10
			&& mem_[a + 2] == 0xCD && mem_[a + 3] == 0x00 && mem_[a + 4] == 0x4D)
			return;
	}
	for (unsigned a = base; a + 20u < end && a + 20u < 0x10000u; a++) {
		if (!(mem_[a] == 0x3E && mem_[a + 1] == 0x07 && mem_[a + 2] == 0x01
			&& mem_[a + 5] == 0xCD && mem_[a + 6] == 0x00 && mem_[a + 7] == 0x4D))
			continue;
		const unsigned p = a + 8u;
		if (mem_[p] == 0x3E && mem_[p + 1] == 0x10
			&& mem_[p + 5] == 0x3E && mem_[p + 6] == 0x0E)
			return;
		if (!(mem_[p] == 0x3E && mem_[p + 1] == 0x0E
			&& mem_[p + 2] == 0xCD && mem_[p + 3] == 0x00 && mem_[p + 4] == 0x4D
			&& mem_[p + 5] == 0xC9))
			continue;
		uint8_t stop[12];
		memcpy(stop, mem_ + p + 6, sizeof(stop));
		static const uint8_t kCmd10Then0E[] = {
			0x3E, 0x10, 0xCD, 0x00, 0x4D,
			0x3E, 0x0E, 0xCD, 0x00, 0x4D,
			0xC9
		};
		memcpy(mem_ + p, kCmd10Then0E, sizeof(kCmd10Then0E));
		const unsigned stopAt = p + (unsigned)sizeof(kCmd10Then0E);
		memcpy(mem_ + stopAt, stop, sizeof(stop));
		if (stop[10] == 0x18) {
			const unsigned jrAt = stopAt + 10u;
			const unsigned poll = base + 0x29u;
			const int rel = (int)poll - (int)(jrAt + 2u);
			if (rel >= -128 && rel <= 127)
				mem_[jrAt + 1] = (uint8_t)rel;
		}
		for (unsigned j = base; j + 3u < end && j + 3u < 0x10000u; j++) {
			if (mem_[j] != 0xFE || mem_[j + 1] != 0x01 || mem_[j + 2] != 0x20)
				continue;
			const int oldRel = (int)(int8_t)mem_[j + 3];
			const unsigned oldTgt = j + 4u + (unsigned)oldRel;
			if (oldTgt != p + 6u)
				continue;
			const int newRel = (int)stopAt - (int)(j + 4u);
			if (newRel >= -128 && newRel <= 127)
				mem_[j + 3] = (uint8_t)newRel;
			break;
		}
		return;
	}
}

void CHardPc88::ApplyNavituneTitleSong()
{
	if (!armNavituneTimer_ || !mem_)
		return;
	if (!naviSongAddr_ && mdataAddr_ >= 0) {
		const unsigned fileOff = (titleCode_ >> 8) & 0xffffu;
		naviSongAddr_ = (uint16_t)((unsigned)mdataAddr_ + fileOff);
	}
	if (!naviSongAddr_)
		return;
	/* Header is 06 77 F5 F5 F2 00 then id/ptr triplets ending FF.
	   cmd07 stores BC to (52D9); cmd10/532E does word[(52D9)+A*2] as the
	   list pointer. Pointing BC at the header makes every song read 7706
	   (the first word of every header) and play BGM 00. Plant the list
	   address at mdata (7700) so stock LD BC,$7700 still works. Do not
	   use $01E0 — PATCH SP=$0200 grows down into that cell. */
	const unsigned body = (unsigned)naviSongAddr_ + 6u;
	if (body < 6u || body >= 0x10000u)
		return;
	const unsigned slot = (mdataAddr_ >= 0) ? (unsigned)mdataAddr_ : 0x7700u;
	mem_[slot] = (uint8_t)(body & 0xff);
	mem_[slot + 1] = (uint8_t)((body >> 8) & 0xff);
}

unsigned CHardPc88::NavituneRetargetPc() const
{
	if (!armNavituneTimer_ || !mem_)
		return 0;
	const unsigned base = (initPc_ > 0) ? (unsigned)initPc_ : 0;
	const unsigned end = base + 0x100u;
	/* Dedicated cmd10 play already ran via port 1. Kicking cmd07+cmd0E
	   after that stops the song (cmd0E) — both titles then look like a
	   2s click of song 0. */
	for (unsigned a = base; a + 4u < end && a + 4u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x10
			&& mem_[a + 2] == 0xCD && mem_[a + 3] == 0x00 && mem_[a + 4] == 0x4D)
			return 0;
	}
	for (unsigned a = base; a + 8u < end && a + 8u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x07
			&& mem_[a + 2] == 0x01
			&& mem_[a + 5] == 0xCD && mem_[a + 6] == 0x00 && mem_[a + 7] == 0x4D)
			return a;
	}
	return 0;
}

void CHardPc88::FinishNavitunePlay()
{
	/* PATCH ends play under DI; keep IM2 sound IRQ deliverable. Do not
	   rewrite Timer B (host 0x27/0x2A freezes driver reload).
	   Tick @4EC0 EI's while (4D59) is set — nested SOUND IRQs need headroom
	   below SP. Stock SP=0200 overflows into the PATCH page; park at 7000
	   (below navimus@7700 / naviprg@4D00). */
	if (!armNavituneTimer_ || !cpu_)
		return;
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
	cpu_->r.iff1 = 1;
	if (cpu_->r.sp < 0x4000 || cpu_->r.sp >= 0x7700)
		cpu_->r.sp = 0x7000;
	if (mem_ && naviSongAddr_) {
		const unsigned body = (unsigned)naviSongAddr_ + 6u;
		if (body < 0x10000u) {
			const unsigned slot = (mdataAddr_ >= 0) ? (unsigned)mdataAddr_ : 0x7700u;
			mem_[slot] = (uint8_t)(body & 0xff);
			mem_[slot + 1] = (uint8_t)((body >> 8) & 0xff);
			mem_[0x52D9] = (uint8_t)(slot & 0xff);
			mem_[0x52DA] = (uint8_t)((slot >> 8) & 0xff);
		}
	}
	if (mem_[0x576E] == 0x21 && mem_[0x5771] == 0x22
		&& mem_[0x5772] == 0xCF && mem_[0x5773] == 0x52)
		memset(mem_ + 0x576E, 0x00, 6);
}

int CHardPc88::NeedsYakyufanArm() const
{
	if (!mem_ || initPc_ != 0xc000)
		return 0;
	return (mem_[0xc000] == 0xf3 && mem_[0xc001] == 0x31
		&& mem_[0xc004] == 0xcd && mem_[0xc005] == 0x00
		&& mem_[0xc006] == 0x01 && mem_[0x100] == 0xc3) ? 1 : 0;
}

void CHardPc88::ArmYakyufanPlay()
{
	if (!NeedsYakyufanArm() || !mem_)
		return;
	/* Mute@0C5D clears (0118); VRTC @0BE4 RET Z without it so phrase/voice
	   fetch never runs (key-ons with TL=7F → peak 0). */
	mem_[0x115] = 1;
	mem_[0x118] = 1;
	/* ISR@0453 reads per-channel voice ids from 086A. That address overlaps
	   code (7F/FE/FF…) so (0123) becomes ≥0x10 and CALL 0717 voice load is
	   skipped — no AR/MUL. Plant FM voice ids 0/1/2 for ch0-2. */
	mem_[0x86a] = 0x00;
	mem_[0x86b] = 0x01;
	mem_[0x86c] = 0x02;
	for (int i = 3; i < 9; i++)
		mem_[0x86a + i] = 0xff;
	/* DRIVER never reaches 0717 from the 0453 path (voice table overlap left
	   the ISR without a working CALL). Seed a basic FM patch on ch0-2 so
	   sequencer key-ons/fnums become audible; live TL/fnum still come from
	   the guest. */
	if (chip_) {
		auto opn = [this](uint8_t a, uint8_t d) {
			PortOut(0x44, a);
			PortOut(0x45, d);
		};
		for (int ch = 0; ch < 3; ch++) {
			const uint8_t c = (uint8_t)ch;
			opn((uint8_t)(0x30 + c), 0x71);
			opn((uint8_t)(0x34 + c), 0x0d);
			opn((uint8_t)(0x38 + c), 0x33);
			opn((uint8_t)(0x3c + c), 0x01);
			opn((uint8_t)(0x40 + c), 0x23);
			opn((uint8_t)(0x44 + c), 0x2d);
			opn((uint8_t)(0x48 + c), 0x26);
			opn((uint8_t)(0x4c + c), 0x00);
			opn((uint8_t)(0x50 + c), 0x5f);
			opn((uint8_t)(0x54 + c), 0x99);
			opn((uint8_t)(0x58 + c), 0x5f);
			opn((uint8_t)(0x5c + c), 0x94);
			opn((uint8_t)(0x60 + c), 0x05);
			opn((uint8_t)(0x64 + c), 0x05);
			opn((uint8_t)(0x68 + c), 0x05);
			opn((uint8_t)(0x6c + c), 0x07);
			opn((uint8_t)(0x70 + c), 0x02);
			opn((uint8_t)(0x74 + c), 0x02);
			opn((uint8_t)(0x78 + c), 0x02);
			opn((uint8_t)(0x7c + c), 0x02);
			opn((uint8_t)(0x80 + c), 0x11);
			opn((uint8_t)(0x84 + c), 0x11);
			opn((uint8_t)(0x88 + c), 0x11);
			opn((uint8_t)(0x8c + c), 0xa6);
			opn((uint8_t)(0xb0 + c), 0x32);
			opn((uint8_t)(0xb4 + c), 0xc0);
		}
	}
}

int CHardPc88::NeedsFe19PlayEi() const
{
	/* arugies keeps I=0 with vectors in page 0 and never DI's on play.
	   schwarz/schwarz2 set I=01 + DI around the play CALL. */
	if (!cpu_ || mem_[0xE000] != 0xFE || mem_[0xE001] != 0x19)
		return 0;
	return cpu_->r.i != 0;
}

/* Broader: PATCH parked in IM2 with a sound vector but left DI after the
   play trigger (Falcom E000 PATCH, some Wing paths). Without EI, OPN/RTC
   IRQs never run and key-ons stay silent. */
int CHardPc88::NeedsPlayEi() const
{
	if (!cpu_)
		return 0;
	if (NeedsFe19PlayEi())
		return 1;
	if (forcePlayEi_)
		return 1;
	/* navitune-class PATCH: DI before play, no EI before poll — OPN IM2
	   never runs unless the host keeps IFF1 (same contract as FE19). */
	if (armNavituneTimer_)
		return 1;
	/* ashe DRIVER@7800: OPN write path DIs; without host EI, key-ons stay silent. */
	if (CEmuPc88AshePlayHi(mem_))
		return 1;
	if (CEmuPc88PatchPwmajan2(mem_))
		return 1;
	/* Falcom specialty PATCH at E000: JR + IM2 table, DI on play. */
	if (mem_[0xE000] == 0x18 && mem_[0xE017] == 0xF3 && mem_[0xE018] == 0xED)
		return 1;
	/* yaksa PATCH2: play leaves DI; VRTC needs host EI (forcePlayEi_ also). */
	if (yaksaPatch2_ || CEmuPc88PatchYaksa2(mem_))
		return 1;
	/* lizard88/gineiden: JR PATCH returns to poll under DI; I-page sound
	   vector lands on a PUSH AF ISR. Host EI lets Timer B keep playing.
	   lizard parks the ISR at 009C on I=0 (page0 table in PATCH). */
	if (mem_[0] == 0x18 && cpu_->r.im == 2 && !cpu_->r.iff1) {
		const unsigned iBase = ((unsigned)cpu_->r.i) << 8;
		const unsigned slot = iBase + 8;
		if (slot + 1 < 0x10000) {
			const unsigned isr = (unsigned)mem_[slot]
				| ((unsigned)mem_[slot + 1] << 8);
			if (isr >= 0x40 && isr < 0x10000 && mem_[isr] == 0xF5
				&& (isr < 0x200 || cpu_->r.i == 0))
				return 1;
		}
	}
	if (armLizardTimer_)
		return 1;
	return 0;
}

/* Wing/Konami PATCHes do IM2 then CALL high DRIVER init under DI; init
   spins on OPN IRQs (hadou DRIVER1@B166, gra88 DRIVER@8980). herzog's
   CALL 81DE has no EI in-PATCH but finishes boot with iff1=1 — do not
   match mid-page calls. */
int CHardPc88::NeedsBootEiPulse() const
{
	if (!cpu_)
		return 0;
	int pc = initPc_;
	if (pc < 0) pc = 0;
	if (pc + 8 >= 0x10000)
		return 0;
	if (mem_[pc] == 0x18) {
		const int rel = (int)(int8_t)mem_[pc + 1];
		pc = pc + 2 + rel;
	}
	if (pc + 8 >= 0x10000)
		return 0;
	/* F3 … ED 5E (IM2). scheme OPNA is F3 / LD SP / ED 5E (not contiguous). */
	if (mem_[pc] != 0xF3)
		return 0;
	int im2At = -1;
	for (int k = 1; k < 16 && pc + k + 1 < 0x10000; k++) {
		if (mem_[pc + k] == 0xED && mem_[pc + k + 1] == 0x5E) {
			im2At = k;
			break;
		}
		if (mem_[pc + k] == 0xFB && !CEmuPc88IsJrDisp(mem_, pc + k))
			return 0;
	}
	if (im2At < 0)
		return 0;
	int sawHiCall = 0;
	for (int k = im2At + 2; k < 48 && pc + k + 2 < 0x10000; k++) {
		if (mem_[pc + k] == 0xFB && !CEmuPc88IsJrDisp(mem_, pc + k))
			break; /* first real EI — CALL after this is play-path */
		if (mem_[pc + k] == 0xCD) {
			const unsigned t = (unsigned)mem_[pc + k + 1]
				| ((unsigned)mem_[pc + k + 2] << 8);
			/* gra88 DRIVER@8980; keep ≥0x8900 (herzog CALL 81DE settles iff1=1).
			   p1demo CALL 811D stays below this on purpose — broader matches
			   mass-regressed recover3 greens via unwanted boot EI pulses. */
			if (t >= 0x8900)
				sawHiCall = 1;
		}
	}
	return sawHiCall;
}

void CHardPc88::SchemePlayTrigger(unsigned titleCode)
{
	if (!schemeMode_)
		return;
	const uint8_t bank = (uint8_t)(titleCode & 0xff);
	/* bothtec PATCH@9000: IN (00) poll, then IN (80) / LD (C000),A / CALL MUS2.
	   C000 must keep the BGM image (MS0A header starts 00 A8…); writing the
	   bank number (0x0A) over byte0 mutes FM. Port 0x80 echoes mem[C000] so
	   that store is a no-op. Still memcpy the requested bank like hoot OUT(0). */
	const int livePoll = (mem_[0x9011] == 0xDB && mem_[0x9012] == 0x00);
	if (bank < 128 && bgmBank_[bank] && bgmBankSize_[bank]) {
		unsigned n = bgmBankSize_[bank];
		if (n > 8 * 1024u) n = 8 * 1024u;
		if (0xc000 + n > 0x10000)
			n = 0x10000 - 0xc000;
		memcpy(mem_ + 0xc000, bgmBank_[bank], n);
		if (!livePoll) {
			/* Stock hoot PATCH uses 9010.. as PLAY_* mailbox (not opcodes). */
			mem_[0x9012] = 0xff;
			mem_[0x9010] = 0x01;
			mem_[0x9011] = bank;
			mem_[0x9013] = (uint8_t)((titleCode >> 8) & 0xff);
		}
	}
}

void CHardPc88::DirectPlayKick(unsigned addr, int ei)
{
	if (!cpu_ || !mem_ || addr == 0 || addr >= 0x10000)
		return;
	if (cpu_->r.sp < 2)
		cpu_->r.sp = 0x0100;
	cpu_->r.sp = (uint16_t)(cpu_->r.sp - 2);
	mem_[cpu_->r.sp] = (uint8_t)(cpu_->r.pc & 0xff);
	mem_[cpu_->r.sp + 1] = (uint8_t)(cpu_->r.pc >> 8);
	cpu_->r.pc = (uint16_t)addr;
	cpu_->r.iff1 = ei ? 1 : 0;
}

int CHardPc88::SkipUnwedge() const
{
	if (!cpu_ || !mem_)
		return 0;
	/* mule CALL 8B01 lives in PROG@6000. Yanking PC back to page-0 poll
	   aborts Timer/I plant and leaves MAIN silent. */
	if (CEmuPc88PatchMulePages(mem_)
		&& cpu_->r.pc >= 0x6000 && cpu_->r.pc < 0xA000)
		return 1;
	return 0;
}

int CHardPc88::IgnoreSoundIrqMask() const
{
	/* Falcom OUT (32),OR 80 around JP (HL) into type=prog at 0000. */
	if (!cpu_ || mem_[0xE000] != 0x18 || mem_[0xE017] != 0xF3)
		return 0;
	return cpu_->r.pc < 0xE000 ? 1 : 0;
}

int CHardPc88::CmdPollPc() const
{
	if (!mem_)
		return -1;
	/* Falcom specialty PATCH: IN A,(00) / OR A / JR Z at E027. */
	if (mem_[0xE000] == 0x18 && mem_[0xE027] == 0xDB && mem_[0xE028] == 0x00
		&& mem_[0xE029] == 0xB7 && mem_[0xE02A] == 0x28)
		return 0xE027;
	auto findAt = [this](unsigned base) -> int {
		const unsigned end = base + 0x70;
		for (unsigned i = base; i + 4 < end && i + 4 < 0x10000u; i++) {
			if (mem_[i] == 0xDB && mem_[i + 1] == 0x00
				&& mem_[i + 2] == 0xB7 && mem_[i + 3] == 0x28)
				return (int)i;
		}
		return -1;
	};
	int p = findAt(0);
	if (p >= 0)
		return p;
	/* blmnstry/hchaser/rouge88/pias88: PATCH lives at init_pc (often $1000
	   after a JR), not page 0. mappy88 is at $F000. */
	if (initPc_ >= 0x80 && initPc_ < 0xFF80) {
		unsigned pc = (unsigned)initPc_;
		p = findAt(pc);
		if (p >= 0)
			return p;
		if (mem_[pc] == 0x18) {
			const int rel = (int)(int8_t)mem_[pc + 1];
			const unsigned jr = (unsigned)((int)pc + 2 + rel);
			if (jr >= 0x80 && jr < 0xFF80) {
				p = findAt(jr);
				if (p >= 0)
					return p;
			}
		}
	}
	return -1;
}

/* hoot oldfalcom.cpp sndadr[] — planted at E00E/E010/E012/E014 before play.
   Index: Romancia 0-1, Xanadu 2-7, Xanadu2 8-12, Asteka2 13. */
enum {
	FALCOM_NONE = 0,
	FALCOM_XANADU = 1,
	FALCOM_XANADU2 = 2,
	FALCOM_ASTEKA2 = 3
};

static const uint16_t kFalcomSndadr[][5] = {
	{0x1056, 0x103a, 0xa000, 0x0000, 0x1043}, /*  0 ROMANCIA Opening */
	{0x3a39, 0x3bee, 0x3b98, 0x3db5, 0x0000}, /*  1 |        Main */
	{0x1b67, 0x1c41, 0x1c0a, 0x0000, 0x1c49}, /*  2 XANADU Training */
	{0x4351, 0x443b, 0x43f5, 0x0000, 0x4443}, /*  3 |      Main */
	{0x168a, 0x1762, 0x172b, 0x0000, 0x176a}, /*  4 |      Boss */
	{0x16b5, 0x178d, 0x1756, 0x0000, 0x1795}, /*  5 |      King Dragon */
	{0x03d6, 0x0495, 0x0489, 0x0000, 0x049d}, /*  6 |      Ending 1 */
	{0x03d6, 0x053d, 0x0489, 0x0000, 0x0545}, /*  7 |      Ending 2 */
	{0x0160, 0x03dc, 0xa000, 0x0000, 0x0000}, /*  8 XANADU2 Opening */
	{0x431c, 0x4466, 0x4417, 0x6067, 0x0000}, /*  9 |       Main */
	{0x1563, 0x16a9, 0x165a, 0x6067, 0x0000}, /* 10 |       Boss */
	{0x159c, 0x16e2, 0x1693, 0x6067, 0x0000}, /* 11 |       King Dragon */
	{0x03d5, 0x05d6, 0xa000, 0x0000, 0x0000}, /* 12 |       Ending */
	{0xaeb7, 0xb103, 0xb05b, 0x7eec, 0x0000}, /* 13 ASTEKA2 APRG */
	{0x029a, 0x0119, 0xa000, 0x0000, 0x0000}, /* 14 XANADU2 IPL (drv 6); keep I=01 */
	{0xb02a, 0xb00c, 0xb02a, 0x0000, 0x0000}, /* 15 ASTEKA2 SOUND (drv 2) */
};

static void CEmuPc88Poke16(uint8_t* mem, unsigned addr, uint16_t v)
{
	mem[addr] = (uint8_t)(v & 0xff);
	mem[addr + 1] = (uint8_t)((v >> 8) & 0xff);
}

static int CEmuPc88DetectFalcom(const CEmuGameEntry* ge, const uint8_t* mem)
{
	if (!ge || !mem)
		return FALCOM_NONE;
	if (mem[0xE000] != 0x18 || mem[0xE017] != 0xF3 || mem[0xE027] != 0xDB)
		return FALCOM_NONE;
	int hasAprg = 0, hasSound = 0, hasPrno = 0, hasBgm = 0, hasIpl = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const char* t = ge->rom[i].type;
		const char* n = ge->rom[i].name;
		if (!t)
			continue;
		if (_stricmp(t, "bgm") == 0)
			hasBgm = 1;
		if (_stricmp(t, "prog") != 0 || !n)
			continue;
		if (_stricmp(n, "APRG") == 0)
			hasAprg = 1;
		else if (_stricmp(n, "SOUND") == 0)
			hasSound = 1;
		else if (_stricmp(n, "IPL") == 0)
			hasIpl = 1;
		else if (_strnicmp(n, "PR.NO", 5) == 0)
			hasPrno = 1;
	}
	if (hasAprg || hasSound)
		return FALCOM_ASTEKA2;
	if (hasPrno && hasBgm)
		return FALCOM_XANADU2;
	if (hasPrno || hasIpl)
		return FALCOM_XANADU;
	return FALCOM_NONE;
}

void CHardPc88::ApplyFalcomPlay()
{
	if (!falcomType_)
		return;
	/* Title 0x12xx / 0x13xx / 0x14xx: same driver as 0x02/03/04, food empty. */
	const unsigned drvHi = (titleCode_ >> 8) & 0xffu;
	const int foodEmpty = (drvHi & 0xF0) != 0;
	const unsigned drv = drvHi & 0x0Fu;
	int ind = -1;
	unsigned load = 0;

	switch (falcomType_) {
	case FALCOM_XANADU:
		ind = (int)drv + 1; /* drv 1..6 → rows 2..7 */
		if (ind < 2 || ind > 7)
			ind = -1;
		load = 0;
		break;
	case FALCOM_XANADU2:
		if (drv == 6)
			ind = 14; /* IPL arranged opening */
		else
			ind = (int)drv + 7; /* drv 1..5 → rows 8..12 */
		if (drv != 6 && (ind < 8 || ind > 12))
			ind = -1;
		load = 0;
		break;
	case FALCOM_ASTEKA2:
		if (drv == 2) {
			ind = 15; /* SOUND @ B000 */
			load = 0xB000;
		} else {
			ind = 13;
			load = 0x8000;
		}
		break;
	default:
		break;
	}
	/* Prog first — volume/food sit inside the 0000..5FFF window. */
	if (drv < 256 && progBank_[drv] && progBankSize_[drv] > 0) {
		unsigned n = progBankSize_[drv];
		if (load + n > 0xE000)
			n = 0xE000 - load;
		if (n > 0)
			memcpy(mem_ + load, progBank_[drv], n);
	}
	if (falcomType_ == FALCOM_XANADU) {
		mem_[0x617a] = 0x09; /* volume */
		if (!foodEmpty && (drv == 2 || drv == 3 || drv == 4)) {
			mem_[0x60a7] = 0xff; /* food != 0 */
			mem_[0x607d] = 0x00;
			mem_[0x6170] = 0x00;
		}
		if (drv == 6) {
			CEmuPc88Poke16(mem_, 0x06af, 0x0001);
			CEmuPc88Poke16(mem_, 0x06b8, 0x0001);
			CEmuPc88Poke16(mem_, 0x06c1, 0x0001);
			CEmuPc88Poke16(mem_, 0x06b1, 0x09e2);
			CEmuPc88Poke16(mem_, 0x06b3, 0x09e2);
			CEmuPc88Poke16(mem_, 0x06ba, 0x0b2f);
			CEmuPc88Poke16(mem_, 0x06bc, 0x0b2f);
			CEmuPc88Poke16(mem_, 0x06c3, 0x0ca3);
			CEmuPc88Poke16(mem_, 0x06c5, 0x0ca3);
			mem_[0x0226] = 0x0c;
			mem_[0x0227] = 0xaa;
		}
	}
	if (ind >= 0 && ind < (int)(sizeof(kFalcomSndadr) / sizeof(kFalcomSndadr[0]))) {
		CEmuPc88Poke16(mem_, 0xE00E, kFalcomSndadr[ind][0]);
		CEmuPc88Poke16(mem_, 0xE010, kFalcomSndadr[ind][1]);
		CEmuPc88Poke16(mem_, 0xE012, kFalcomSndadr[ind][2]);
		CEmuPc88Poke16(mem_, 0xE014, kFalcomSndadr[ind][3]);
		const uint16_t iffadr = kFalcomSndadr[ind][4];
		if (iffadr)
			mem_[iffadr] = 0xE0;
	}
	if (falcomType_ == FALCOM_XANADU2) {
		const unsigned song = titleCode_ & 0xffu;
		if (song < 256 && bgmBank_[song] && bgmBankSize_[song] >= 16) {
			unsigned n = bgmBankSize_[song];
			if (n > 13u * 1024u)
				n = 13u * 1024u;
			if (0x5C00 + n > 0xE000)
				n = 0xE000 - 0x5C00;
			memcpy(mem_ + 0x5C00, bgmBank_[song], n);
		}
		if (drv != 5 && !foodEmpty)
			mem_[0x60a5] = 0xff;
	}
}

void CHardPc88::BankCopyBgm(uint8_t songIndex)
{
	if (!bgmBank_[songIndex] || mfileSize_ <= 0) return;
	const int dst = ((int)mem_[0x5d] << 8) | (int)mem_[0x5c];
	/* Destination is armed by the driver (mucom88). dst==0 would clobber
	   PATCH when port 0 is used as a cmd/stop latch (KOEI OUT 0,A=0). */
	if (dst < 0x100 || dst >= 0x10000) return;
	unsigned n = bgmBankSize_[songIndex];
	if ((unsigned)mfileSize_ < n)
		n = (unsigned)mfileSize_;
	if (dst + (int)n > 0x10000)
		n = (unsigned)(0x10000 - dst);
	memcpy(mem_ + dst, bgmBank_[songIndex], n);
	mem_[0x5e] = 0xff;
}

void CHardPc88::SetSoundIrqPort(uint8_t data)
{
	ioPorts_[0x32] = data;
	soundIrqMasked = (data & 0x80) != 0;
}

/* PC-8801 text window: the 1KB at 8000-83FF is a movable view of main RAM
   whose base is (port 70h << 8); the documented way to close it again is to
   write 80h, which puts the real 8000-83FF back. Port 78h steps the base.
   The Z80 core indexes mem_ directly, so materialize the view by copying on
   base changes — page flips are far rarer than the reads made through it.
   yokosuka's byte fetcher is `OUT (70),H / LD H,80 / LD A,(HL)`, one flip per
   fetch at worst, and without this every note it reads comes from page F0. */
void CHardPc88::SetTextWindow(uint8_t hi)
{
	if (hi == textWinHi_)
		return;
	const unsigned oldBase = (unsigned)textWinHi_ << 8;
	const unsigned newBase = (unsigned)hi << 8;
	/* A base near the top of memory exposes only what fits below 64K. */
	unsigned oldLen = (oldBase + 0x400 <= 0x10000) ? 0x400 : (0x10000 - oldBase);
	unsigned newLen = (newBase + 0x400 <= 0x10000) ? 0x400 : (0x10000 - newBase);
	if (textWinHi_ != 0x80)
		memmove(mem_ + oldBase, mem_ + 0x8000, oldLen);
	else
		memcpy(textWinShadow_, mem_ + 0x8000, 0x400);
	textWinHi_ = hi;
	if (hi != 0x80)
		memmove(mem_ + 0x8000, mem_ + newBase, newLen);
	else
		memcpy(mem_ + 0x8000, textWinShadow_, 0x400);
}


uint8_t CHardPc88::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (p) {
	case 0x00: { uint8_t v = cmd; cmd = 0; return v; }
	case 0x01: return param;
	case 0x80:
		/* Scheme bothtec PATCH: LD (C000),A after IN (80). Echoing the live
		   BGM header byte avoids clobbering MS0A (bank# as song → mute). */
		if (schemeMode_)
			return mem_[0xc000];
		return song;
	case 0x40: {
		const uint64_t hz = cpuHz_ > 0 ? (uint64_t)cpuHz_ : 4000000ull;
		const uint64_t frame = hz / 60;
		const uint64_t vblank = frame / 12;
		return ((cpuCycles_ % frame) < vblank) ? 0x20 : 0x00;
	}
	/* hoot: 0x32 and alias 0xAA share ioport[0x32] (default 0) */
	case 0x32: case 0xAA: return ioPorts_[0x32];
	/* Drivers save/restore the window base around their ISR. */
	case 0x70: return textWinHi_;
	/* A007 probes both 0x44-47 and alternate 0xA8-AD (firehawk/hoot) */
	case 0x44: case 0xA8: return chip_ ? chip_->ReadStatus() : 0xff;
	case 0x45: case 0xA9: return chip_ ? chip_->ReadData() : 0xff;
	case 0x46: case 0xAC: return chip_ ? chip_->ReadStatusHi() : 0xff;
	case 0x47: case 0xAD: return chip_ ? chip_->ReadDataHi() : 0xff;
	default: return ioPorts_[p];
	}
}

void CHardPc88::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (p) {
	case 0x00:
		cmd = data;
		/* mucom88: OUT (0),song copies bank to [0x5d:0x5c]. KOEI uses
		   port 0 as cmd/stop only — never bank-copy there. Non-mucom
		   mfile sets (shnghai2) also OUT 0 as a latch with live 5C/5D.
		   The stock hoot PATCH only ever writes 0 here to clear the command
		   latch (triton2 0037, sorc88 0038/0042), so it is not a bank
		   strobe: treating it as one restaged MUS00 over every song. */
		if (mucomBankCopy_ && mfileSize_ > 0)
			BankCopyBgm(data);
		/* hoot scheme.cpp: OUT (0),bank → memcpy C000 + LOAD_FLAG@9012. */
		/* hoot scheme.cpp: OUT (0),bank → memcpy C000 + LOAD_FLAG@9012.
		   bothtec PATCH keeps an IN (00) poll at 9011 — guest OUT (0) is not
		   the bank-load strobe there (it would clobber live MML at C000). */
		else if (schemeMode_ && data < 128 && bgmBank_[data] && bgmBankSize_[data]
			&& !(mem_[0x9011] == 0xDB && mem_[0x9012] == 0x00)) {
			unsigned n = bgmBankSize_[data];
			if (n > 8 * 1024u) n = 8 * 1024u;
			if (0xc000 + n > 0x10000)
				n = 0x10000 - 0xc000;
			memcpy(mem_ + 0xc000, bgmBank_[data], n);
			mem_[0x9012] = 0xff;
		}
		break;
	case 0x01: param = data; break;
	case 0x02:
		/* hoot oldfalcom: OUT (02) is Xanadu2 BGM window only. Prog and
		   E00E..E014 were planted by ApplyFalcomPlay — rewriting them here
		   clobbered the start address after PATCH had already copied E00E
		   (still 0) into the IM2 sound vector. */
		if (falcomType_ == FALCOM_XANADU2) {
			if (data < 256 && bgmBank_[data] && bgmBankSize_[data] >= 16) {
				unsigned n = bgmBankSize_[data];
				if (n > 13u * 1024u)
					n = 13u * 1024u;
				if (0x5C00 + n > 0xE000)
					n = 0xE000 - 0x5C00;
				memcpy(mem_ + 0x5C00, bgmBank_[data], n);
			}
			const unsigned drv = (titleCode_ >> 8) & 0x0Fu;
			const int foodEmpty = ((titleCode_ >> 8) & 0xF0) != 0;
			if (drv != 5 && !foodEmpty)
				mem_[0x60a5] = 0xff;
			break;
		}
		if (falcomType_)
			break;
		/* Other type=prog PATCHes: OUT (02),song maps the bank
		   (title bits 8..15). Load address follows the bank's own
		   `LD A,n; LD I,A` (asteka2 APRG→8000, SOUND→B000); plain
		   xana PR.NO* fall back to 0000.
		   Header words at +2/+4 are IM2/play-ISR addresses (SOUND B02A),
		   NOT the host JP target — PATCH pushes E06C and JP (E010), so
		   E010 must be the JR/JP init stub (LD I / CALL … / EI RET). */
		{
			const unsigned prog = (titleCode_ >> 8) & 0xff;
			if (prog < 256 && progBank_[prog] && progBankSize_[prog] > 0) {
				const unsigned char* img = progBank_[prog];
				unsigned n = progBankSize_[prog];
				unsigned load = 0;
				unsigned entry = 0;
				int entryHasLdI = 0;
				const int jrAt0 = (img[0] == 0x18);
				const int jpAt0 = (img[0] == 0xC3 && n >= 3);
				if (jrAt0) {
					entry = (unsigned)(2 + (int)(int8_t)img[1]);
				} else if (jpAt0) {
					entry = (unsigned)img[1] | ((unsigned)img[2] << 8);
					if (entry >= n) entry = 0;
				} else if (n > 0x120 && img[0x100] == 0x18) {
					/* xana PR.NO*: file header then Falcom JR stub @0100.
					   Image is linked for load=0000; LD I,A later only sets
					   the IM2 page (not a relocate base). */
					entry = 0x100;
				}
				if (jrAt0 || jpAt0) {
					for (unsigned k = entry; k + 4 <= n && k < entry + 64; k++) {
						if (img[k] == 0x3E && img[k + 2] == 0xED
							&& img[k + 3] == 0x47) {
							load = (unsigned)img[k + 1] << 8;
							entryHasLdI = 1;
							break;
						}
					}
				}
				/* xana stub@100: JR lands on C9; real init is LD SP after the
				   vector RET bytes (same layout as E000 PATCH / C9 / F3…). */
				if (!entryHasLdI && entry == 0x100) {
					for (unsigned k = entry; k + 1 < n && k < entry + 32; k++) {
						if (img[k] == 0x31) { /* LD SP,nn */
							entry = k;
							break;
						}
					}
				}
				if (load >= 0xE000) load = 0;
				if (load + n > 0xE000)
					n = 0xE000 - load; /* keep PATCH at E000 */
				if (load + n > 0x10000)
					n = 0x10000 - load;
				if (n > 0)
					memcpy(mem_ + load, img, n);
				unsigned play = load + entry;
				unsigned stop = play;
				/* Header +2/+4 are IM2/ISR addresses when a JR init stub
				   exists (SOUND B02A). Only use them if we found no stub. */
				if (n >= 6 && entry == 0 && !entryHasLdI) {
					const unsigned w2 = (unsigned)img[2] | ((unsigned)img[3] << 8);
					const unsigned w4 = (unsigned)img[4] | ((unsigned)img[5] << 8);
					if (w2 != 0)
						play = w2;
					if (w4 != 0)
						stop = w4;
					else
						stop = play;
				}
				mem_[0xE010] = (uint8_t)(play & 0xff);
				mem_[0xE011] = (uint8_t)((play >> 8) & 0xff);
				mem_[0xE012] = (uint8_t)(stop & 0xff);
				mem_[0xE013] = (uint8_t)((stop >> 8) & 0xff);
				/* xana2 PR.NO* stub@0100: LD I,A sets the IM2 page. If the
				   guest JP (E010) returns before that insn (or RETI-restores
				   I=E0), sound vec E008 stays 0 from the pre-OUT LDI wipe and
				   music dies. Plant I + E0 sound vector from the stub header. */
				if (entry == 0x100 || (play >= 0x100 && play < 0x140)) {
					for (unsigned k = 0x100; k + 4 <= n && k < 0x140; k++) {
						if (img[k] == 0x3E && img[k + 2] == 0xED
							&& img[k + 3] == 0x47) {
							if (cpu_)
								cpu_->r.i = img[k + 1];
							break;
						}
					}
					if (n >= 0x10A) {
						mem_[0xE008] = img[0x108];
						mem_[0xE009] = img[0x109];
						mem_[0xE00E] = img[0x108];
						mem_[0xE00F] = img[0x109];
					}
				}
				/* xana2: prog image covers 0000..5FFF; re-apply bgm bank on
				   top at mdata (0x5C00) after the map. */
				if (mdataAddr_ >= 0 && data < 256 && bgmBank_[data]
					&& bgmBankSize_[data] >= 16) {
					unsigned bn = bgmBankSize_[data];
					if (mdataSize_ > 0 && (unsigned)mdataSize_ < bn)
						bn = (unsigned)mdataSize_;
					if (mdataAddr_ + (int)bn > 0xE000)
						bn = (unsigned)(0xE000 - mdataAddr_);
					if (bn > 0)
						memcpy(mem_ + mdataAddr_, bgmBank_[data], bn);
				}
			}
		}
		break;
	case 0x32: case 0xAA: SetSoundIrqPort(data); break;
	case 0x44: case 0xA8: if (chip_) chip_->Write(0, data); break;
	case 0x46: case 0xAC: if (chip_) chip_->Write(0x100, data); break;
	case 0x45: case 0xA9: if (chip_) chip_->Write(1, data); break;
	case 0x47: case 0xAD: if (chip_) chip_->Write(0x101, data); break;
	case 0x70: SetTextWindow(data); break;
	case 0x78: SetTextWindow((uint8_t)(textWinHi_ + 1)); break;
	/* 5Ch-5Fh (GVRAM plane select over C000-FFFF) stay unimplemented on
	   purpose: 237 rips strobe them and 89 keep code up there, so honouring
	   them would swap a driver out from under itself. hoot ignores the ports,
	   these rips were patched against hoot, and emulating the real mapping
	   changed nothing measurable (arka88, 84 strobes, byte-identical). */
	case 0xE4: if (chip_) chip_->AckIrq(); break;
	case 0xE6: break; /* PC-88 IRQ level — ignored by hoot */
	default: ioPorts_[p] = data; break;
	}
}

int CHardPc88::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	const unsigned songNum = titleCode & 0xff;
	const int isMucom = (_stricmp(ge->subtype, "muco") == 0 || _stricmp(ge->subtype, "mucom88") == 0);
	titleCode_ = titleCode;
	memset(mem_, 0, sizeof(mem_));
	textWinHi_ = 0x80;
	memset(textWinShadow_, 0, sizeof(textWinShadow_));
	StageBanks(fs, ge);
	const int preferMdatN = opnaMode || CEmuParseOptHex(ge, "use_pcmx8", 0);
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		const int off = r->offset;
		if (off < 0 || off >= 0x10000) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz, -1, preferMdatN);
		if (!data || !sz || off + (int)sz > 0x10000) continue;
		memcpy(mem_ + off, data, sz);
		/* Scheme OPNA: mirror PATCH@0 → 0x9000 only (don't blank page 0 — some
		   paths still peek low memory; stack smash is avoided by init@9000). */
		if (schemeMode_ && _stricmp(r->name, "PATCH") == 0 && off == 0) {
			unsigned n = sz;
			if (0x9000 + n > 0x10000)
				n = 0x10000 - 0x9000;
			if (n > 0)
				memcpy(mem_ + 0x9000, data, n);
			if (initPc_ == 0)
				initPc_ = 0x9000;
		}
		/* Falcom specialty: PATCH alone at E000 — start there (do not rely
		   on NOP-slide from 0000; song preload at 4000/5C00 can interrupt). */
		if (initPc_ == 0 && _stricmp(r->name, "PATCH") == 0 && off == 0xE000)
			initPc_ = 0xE000;
	}
	falcomType_ = CEmuPc88DetectFalcom(ge, mem_);
	CEmuPc88MirrorDriverPage20(mem_);
	/* Incomplete rips (e.g. p1demo1 DRIVER EOF before song RAM) are not
	   repaired here — inventing trampolines hides missing payload. */
	/* yakyufan: play@02A0 does XOR A; LD (0115),A then never sets the flag.
	   Key-on @110A and tempo @0874 both RET when (0115)==0 — leaves fnum
	   writes from the timer ISR but peak/key=0. Replace the 11-byte clear
	   +INC with LD A,1; LD (0115/16/19),A (A stays 1 for the following
	   LD (0129),A).
	   Also: PATCH sets SP=0100 while DRIVER init plants I=0 IM2 in page0 —
	   VRTC during boot pushes onto the vector table and RET lands in junk
	   (PC≈8Bxx). Raise SP to FF00 before any IRQ.
	   Mute@0C5D clears (0118); VRTC tick @0BE4 RET Z — ArmYakyufanPlay
	   re-asserts both flags after the play CALL. */
	if (initPc_ == 0xc000 && mem_[0xc000] == 0xf3 && mem_[0xc001] == 0x31
		&& mem_[0xc004] == 0xcd && mem_[0xc005] == 0x00 && mem_[0xc006] == 0x01
		&& mem_[0x100] == 0xc3) {
		if (mem_[0xc002] == 0x00 && mem_[0xc003] == 0x01)
			mem_[0xc003] = 0xff; /* LD SP,0xFF00 */
		if (mem_[0x2b3] == 0xaf && mem_[0x2b4] == 0x32
			&& mem_[0x2b5] == 0x15 && mem_[0x2b6] == 0x01 && mem_[0x2b7] == 0x32
			&& mem_[0x2b8] == 0x16 && mem_[0x2b9] == 0x01 && mem_[0x2ba] == 0x32
			&& mem_[0x2bb] == 0x19 && mem_[0x2bc] == 0x01 && mem_[0x2bd] == 0x3c) {
			static const uint8_t kArm115[11] = {
				0x3e, 0x01, 0x32, 0x15, 0x01, 0x32, 0x16, 0x01, 0x32, 0x19, 0x01
			};
			memcpy(mem_ + 0x2b3, kArm115, sizeof(kArm115));
		}
	}
	/* Apply title-encoded mdata before overlap / preload decisions. */
	{
		const int titleMdata = CEmuPc88TitleEncodedMdata(titleCode, mdataAddrDefaulted_, wolfteamMode_);
		if (titleMdata >= 0)
			mdataAddr_ = titleMdata;
	}
	if (CEmuPc88PatchXzr2VoiceF000(mem_)) {
		vdataAddr_ = 0xF000;
		if (vfileSize_ <= 0)
			vfileSize_ = 0x200;
	}
	const int overlapsCode = CEmuPc88SongOverlapsCode(fs, ge, mdataAddr_, mdataSize_);
	/* Overlapping mdata (sorc88 SEDAT, kbreed TRPSCR, …): keep code intact
	   through boot; LoadSongData runs again on the play trigger. */
	const int preloadSong = !(overlapsCode && ShouldRestageSong());
	/* navitune-class: title bits 8..23 are a byte offset into bgm that also
	   sits as code@mdata. Overlap defer would leave the offset-0 code image
	   in place forever if restage ever no-ops — always apply fileOff here. */
	const unsigned titleFileOff = (titleCode >> 8) & 0xffffu;
	if (titleFileOff != 0 && mdataAddr_ >= 0 && bgmBank_[songNum]
		&& titleFileOff < bgmBankSize_[songNum])
		LoadSongData(titleCode);
	else if (preloadSong && (wolfteamMode_ || mdataAddr_ >= 0 || vdataAddr_ >= 0))
		LoadSongData(titleCode);
	else if (preloadSong && !isMucom) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "bgm") != 0 || r->offset != (int)songNum) continue;
			if (mdataAddr_ < 0) continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz,
				r->offset, preferMdatN);
			if (!data || !sz || mdataAddr_ < 0) continue;
			unsigned n = sz;
			if (mdataAddr_ + (int)n > 0x10000)
				n = (unsigned)(0x10000 - mdataAddr_);
			if (n > 0)
				memcpy(mem_ + mdataAddr_, data, n);
		}
	}
	/* harakiri: boot CALL C009 is FMDRV init. MPLAY/FMDRV2 voice overlays
	   leave C009 as data (NOP/RST) — CALL C000, the JP table's init. */
	if (mem_[0x0B] == 0xCD && mem_[0x0C] == 0x09 && mem_[0x0D] == 0xC0
		&& mem_[0xC000] == 0xC3 && mem_[0xC009] != 0xC3)
		mem_[0x0C] = 0x00;
	if (CEmuPc88PatchRobowr(mem_) && (titleCode & 0xffu) == 1
		&& mem_[0xCB5A] == 0xF3 && mem_[0xCB63] == 0xFE && mem_[0xCB64] == 0x01
		&& mem_[0xCB65] == 0x20) {
		mem_[0x0A] = 1;
		mem_[0xCB65] = 0x00;
		mem_[0xCB66] = 0x00;
	}
	if (armNavituneTimer_)
		PrepareNavitunePatch();
	if (isMucom) {
		mem_[0xEEA7] = 0xAF;
		mem_[0xEEA8] = 0xC3;
		mem_[0xEEA9] = 0x00;
		mem_[0xEEAA] = 0xB0;
	}
	/* spitfl88: boot CALL A826 reads (79D7) and clears A824 (player enable)
	   when <0x34. That cell sits outside PROG (mid-RAM); without a plant the
	   ISR skips AD0C forever even after play arms A6A9=0x20. */
	if (forcePlayEi_ && mem_[0xA826] == 0xF3 && mem_[0xA830] == 0xFE
		&& mem_[0xA831] == 0x34)
		mem_[0x79D7] = 0x40;
	/* byouin_88 MUSIC.SYS@9C00: entry (JP 9EFA) does
	   LD A,(79D7) / CP 34 / RET C — without a plant every CALL 9C00
	   no-ops, IM2 page I=F3 stays empty, and RTC/OPN never run. */
	if (mem_[0x9C00] == 0xC3) {
		const unsigned ent = (unsigned)mem_[0x9C01]
			| ((unsigned)mem_[0x9C02] << 8);
		if (ent + 5 < 0x10000
			&& mem_[ent] == 0xF5 && mem_[ent + 1] == 0x3A
			&& mem_[ent + 2] == 0xD7 && mem_[ent + 3] == 0x79
			&& mem_[ent + 4] == 0xFE && mem_[ent + 5] == 0x34
			&& mem_[0x79D7] < 0x34)
			mem_[0x79D7] = 0x40;
	}
	/* lizard88: MAIN@9F00 XOR-decrypts at boot; song load A3B0 gates on
	   (79D7)>=0x34 (same mid-RAM cell as spitfl/byouin). Without a plant
	   A3B0 bails to A4E7, A572 stays sticky, and 9F12 skips OPN writes. */
	if (mem_[0x79D7] < 0x34 && mem_[0] == 0x18 && mem_[0x10] == 0xF3) {
		int main9f = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x9f00 && _strnicmp(r->name, "MAIN", 4) == 0)
				main9f = 1;
		}
		if (main9f)
			mem_[0x79D7] = 0x40;
		/* A572 song-gate is planted after XOR-decrypt in the driver —
		   LoadRoms runs before PATCH decrypts MAIN@9F00. */
	}
	/* pwmajan2: PATCH CALL 9019 inits I/IRQ then CALL 0184/0178 and was
	   meant to RET at the C9 planted over EI+disk. Boot never reached poll.
	   Skip 9019; ArmPwmajan2 plants the IM2 vec + unmask after settle. */
	if (CEmuPc88PatchPwmajan2(mem_)
		&& mem_[0xF012] == 0xCD && mem_[0xF013] == 0x19 && mem_[0xF014] == 0x90) {
		mem_[0xF012] = 0x00;
		mem_[0xF013] = 0x00;
		mem_[0xF014] = 0x00;
	}
	CEmuPc88PlantIceclimbTitleLoop(mem_);
	if (CEmuPc88PatchYaksa2(mem_))
		mem_[0x37D1] = 1;
	/* mule 8B01 EI's while I is still 0, then LD I,5F. Catalog VRTC in
	   that window vectors through 0004 into the stack page. OPN Timer B
	   is armed by 8B01 itself. */
	if (CEmuPc88PatchMulePages(mem_))
		useVrtc = 0;
	/* lvaccus: PATCH is DI/IM2 with I=0. Catalog VRTC during settle reads
	   vec 0002 (ED 5E) and runs into empty RAM then PROG. Play plants
	   I=1 / (0102)=$9803 and EI's — re-enable VRTC after that. */
	if (CEmuPc88PatchLvaccus9800(mem_))
		useVrtc = 0;
	/* xzr I=A4 vectors are planted at boot, but catalog VRTC during settle
	   still hits A86F (SP swap to BD4B) before A416 EI's. */
	if (CEmuPc88PatchXzrA4(mem_))
		useVrtc = 0;
	if (falcomType_)
		ApplyFalcomPlay();
	/* f_crisis MMLEX@9A00 + bare DI PATCH: EI alone does not unlock audio
	   (play wanders into MMLEX). Keep forcePlayEi_ clear until the MUSIC.OBJ
	   protocol is understood. castle keeps forcePlayEi_ from Init (OPN IM2).
	   Preserve Init-time forcePlayEi_ for spitfl88/tf88sr PROG-only. */
	{
		const int keepForceEi = forcePlayEi_;
		forcePlayEi_ = 0;
		if (keepForceEi)
			forcePlayEi_ = 1;
	}
	int initPc = initPc_;
	cpu_->reset(mem_);
	cpu_->r.pc = (uint16_t)initPc;
	/* IM2 without LD SP (herzog/gra88): SP=0 makes the first IRQ smash page 0.
	   Hoot PATCHes almost always use SP=0x0100; OK peers without LD SP still
	   work with that default. */
	{
		int pc = initPc_;
		if (pc < 0) pc = 0;
		if (mem_[pc] == 0x18) {
			const int rel = (int)(int8_t)mem_[pc + 1];
			pc = pc + 2 + rel;
		}
		int hasIm2 = 0, hasLdSp = 0;
		for (int k = 0; k < 32 && pc + k + 1 < 0x10000; k++) {
			if (mem_[pc + k] == 0x31) hasLdSp = 1;
			if (mem_[pc + k] == 0xED && mem_[pc + k + 1] == 0x5E) hasIm2 = 1;
		}
		if (hasIm2 && !hasLdSp)
			cpu_->r.sp = 0x0100;
	}
	/* Packed KOEI: port 80/01 are FMDRV play index (0 = table[0]), not bank. */
	song = PlaySongIndex();
	param = PlayParamIndex();
	cpuCycles_ = 0;
	memset(ioPorts_, 0, sizeof(ioPorts_));
	/* Hoot inits ioport[0..0x0E]=0xFF. Zero breaks mugen3/yakyudou-class
	   drivers that IN unused low ports; wing2 is the known counterexample
	   (stays silent with 0xFF — accepted vs multi-title gain). */
	for (int p = 0x02; p <= 0x0E; p++)
		ioPorts_[p] = 0xff;
	soundIrqMasked = 0;
	/* OPNA: stock ADPCM-A rhythm ROM. Game type=adpcm is ADPCM-B (above).
	   Scheme still needs rhythm ROM for ADPCM-A percussion. */
	if (opnaMode && chip_)
		CEmuLoadExternalYm2608Adpcm(chip_);
	/* Scheme MUS2 keeps OPN port mailbox at F0BB.. (32/44/46). */
	if (schemeMode_) {
		mem_[0xf0bb] = 0x32;
		mem_[0xf0bc] = 0x44;
		mem_[0xf0bd] = 0x46;
	}
	return 1;
}

void CEmuHardPc88SetActive(CHardPc88* hw)
{
	g_pc88Active = hw;
	CEmuZ80BusSetActive(hw);
}
