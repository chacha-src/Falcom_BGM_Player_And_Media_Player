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
	, armNavituneTimer_(0)
	, naviSongAddr_(0)
	, deferRtcAfterPlay_(0)
	, n88RtcIsr_(0)
	, n88RtcThrottleAddr_(0)
	, schemeMode_(0)
	, playKickBase_(0)
	, playKickInitOff_(0)
	, playKickEi_(1)
	, titleCode_(0)
{
	hardKind = KIND_PC88;
	memset(mem_, 0, sizeof(mem_));
	memset(ioPorts_, 0, sizeof(ioPorts_));
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
	for (unsigned i = 0; i < nch; i++) {
		const unsigned ptr = (unsigned)data[1 + i * 4]
			| ((unsigned)data[2 + i * 4] << 8);
		if (ptr < 0x2000 || ptr >= 0xE000)
			return -1;
		hist[ptr >> 8]++;
		hits++;
	}
	if (hits < 3)
		return -1;
	unsigned bestPage = 0, bestCount = 0;
	for (unsigned p = 0; p < 256; p++) {
		if (hist[p] > bestCount) {
			bestCount = hist[p];
			bestPage = p;
		}
	}
	if (bestCount * 2 < hits)
		return -1;
	/* Align down to 4K so MUS00 (0xA0xx) and MUS09 (0x93xx) share a window. */
	return (int)((bestPage << 8) & 0xF000);
}

static int CEmuPc88PatchSongShift8(const uint8_t* mem);
static int CEmuPc88PatchLdirFrom4000(const uint8_t* mem);
static int CEmuPc88PatchFalcomAndF0Cp10(const uint8_t* mem);
static int CEmuPc88PatchFalcomLdirFromC000(const uint8_t* mem);
static unsigned CEmuPc88FalcomYs2MusDest(const uint8_t* mem, unsigned bank);
static void CEmuPc88FalcomYs2PlantTable(uint8_t* mem, unsigned bank);
static int CEmuPc88AshePlayHi(const uint8_t* mem);
static void CEmuPc88PlantAsheSongTable(uint8_t* mem, unsigned songNum, int mdataAddr);

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
		if (prog0 && patchF000) {
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
	/* yokosuka: catalog sets use_rtc AND use_vrtc; dual delivery starves
	   the SOUND vector. Prefer VRTC (same family as thexder88). */
	if (useRtc && useVrtc) {
		int hasSoundHigh = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset >= 0xb000 && (_strnicmp(r->name, "SOUND", 5) == 0
				|| _strnicmp(r->name, "sound", 5) == 0))
				hasSoundHigh = 1;
		}
		if (hasSoundHigh)
			useRtc = 0;
	}
	/* spitfl88 / tf88sr: PROG-only + PATCH@0 + no init_pc. IM2 table parks
	   the real player on RTC (vec04); VRTC/SOUND slots are RET stubs
	   (spitfl A820=C9, tf88 409E=ack+RET). Keep catalog RTC and force EI —
	   flipping to VRTC was a guaranteed mute. hangon88/plazmasr keep init_pc
	   so they never match this filter. */
	if (useRtc && !useVrtc) {
		int codeRoms = 0, bgmRoms = 0, hasProg = 0, patchAt0 = 0;
		int hasInitPc = 0;
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
		}
		if (!hasInitPc && hasProg && patchAt0 && bgmRoms == 0 && codeRoms <= 2) {
			/* Keep useRtc; PATCH has no EI before poll. */
			forcePlayEi_ = 1;
		}
	}
	/* Direct play kick: Game Arts JR PATCH calls player+6 then relies on
	   RTC ISR (vec04) to CALL player+0. Without RTC, host does the same
	   init(+6) then base CALL. castle/castleex: PROG2@1000 is the working
	   entry (PATCH's CALL 1033 alone stays silent). */
	{
		int play88 = 0, codeC000 = 0, codeRoms = 0, voiceF000 = 0, prog1000 = 0;
		int patchJr = 0, soundB5 = 0;
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
			if (r->offset >= 0xb000 && (_strnicmp(r->name, "SOUND", 5) == 0
				|| _strnicmp(r->name, "sound", 5) == 0))
				soundB5 = r->offset;
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
		} else if (soundB5 && useVrtc && !useRtc) {
			/* yokosuka: PATCH play is DI; CALL B780 (SOUND+0x1BD). */
			playKickBase_ = 0xB780;
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
}

int CHardPc88::ShouldRestageSong() const
{
	if (mdataAddr_ < 0)
		return 0;
	/* Scheme OPNA: C000 is live driver (MS0A) + BGM window; restaging the
	   song over it from Render clobbers MUS2. BGM loads via OUT (0),bank. */
	if (schemeMode_)
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
	/* Voice before BGM: ys2 END/TTL images end on the music staging page
	   (ENDPRG@0100..20FF overlaps mus@2000; TTLPRG overlaps @3000). Planting
	   voice last used to clobber the mirrored *MUS blob. */
	if (voiceBank_[songNum] && vdataAddr_ >= 0) {
		unsigned n = voiceBankSize_[songNum];
		if (vfileSize_ > 0 && (unsigned)vfileSize_ < n)
			n = (unsigned)vfileSize_;
		if (vdataAddr_ + (int)n > 0x10000)
			n = (unsigned)(0x10000 - vdataAddr_);
		if (n > 0)
			memcpy(mem_ + vdataAddr_, voiceBank_[songNum], n);
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
				/* Relocate absolute ptrs. Blind every-byte scans corrupt
				   ashe MUS headers (attr_hi||next_ptr_lo → false 0x9A01).
				   1) ashe-style headers (nch + ptr/attr×nch)
				   2) remaining even offsets only (phrase tables). */
				const unsigned lb = (unsigned)linkBase;
				for (unsigned off = 0; off + 5 < n; off++) {
					const unsigned nch = mem_[loadAddr + (int)off];
					if (nch < 3 || nch > 16 || off + 1 + nch * 4 > n)
						continue;
					unsigned pageHits[256];
					memset(pageHits, 0, sizeof(pageHits));
					int ok = 1;
					for (unsigned c = 0; c < nch; c++) {
						const unsigned po = off + 1 + c * 4;
						const unsigned w = (unsigned)mem_[loadAddr + (int)po]
							| ((unsigned)mem_[loadAddr + (int)po + 1] << 8);
						if (w < lb || w >= lb + n) { ok = 0; break; }
						pageHits[w >> 8]++;
					}
					if (!ok) continue;
					unsigned best = 0;
					for (unsigned p = 0; p < 256; p++)
						if (pageHits[p] > best) best = pageHits[p];
					if (best * 2 < nch) continue;
					for (unsigned c = 0; c < nch; c++) {
						const unsigned po = off + 1 + c * 4;
						unsigned w = (unsigned)mem_[loadAddr + (int)po]
							| ((unsigned)mem_[loadAddr + (int)po + 1] << 8);
						if (w < lb || w >= lb + n) continue;
						w = w - lb + (unsigned)loadAddr;
						mem_[loadAddr + (int)po] = (uint8_t)(w & 0xff);
						mem_[loadAddr + (int)po + 1] = (uint8_t)((w >> 8) & 0xff);
					}
					off += nch * 4;
				}
				for (unsigned i = 0; i + 1 < n; i += 2) {
					unsigned w = (unsigned)mem_[loadAddr + (int)i]
						| ((unsigned)mem_[loadAddr + (int)i + 1] << 8);
					if (w < lb || w >= lb + n) continue;
					w = w - lb + (unsigned)loadAddr;
					mem_[loadAddr + (int)i] = (uint8_t)(w & 0xff);
					mem_[loadAddr + (int)i + 1] = (uint8_t)((w >> 8) & 0xff);
				}
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
	/* gineiden: each BGM_n.COM is one song staged at mdata; AMAIN indexes
	   with B*6 from IN (80). Title low byte selected the bank — port 80
	   must be 0 or play skips into the channel table and stays mute. */
	if (armGineidenTimer_)
		return 0;
	/* yakyufan: each MUS* is one bank staged at 0x4000; (010B)/port80 is an
	   in-file song index. Passing the bank id (0..0x0A) seeks past the only
	   phrase table → key-ons with muted TL. */
	if (NeedsYakyufanArm())
		return 0;
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
	return (uint8_t)songNum;
}

uint8_t CHardPc88::PlayParamIndex() const
{
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
	return PlaySongIndex();
}

void CHardPc88::FixupIm2AfterBoot()
{
	CEmuPc88InstallFe19Im2(mem_, cpu_);
}

void CHardPc88::FixupIm2AfterPlay()
{
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
	for (unsigned a = base; a + 2u < end && a + 2u < 0x10000u; a++) {
		if (mem_[a] == 0x21 && mem_[a + 1] == 0x7D && mem_[a + 2] == 0x01) {
			mem_[a + 1] = 0xFF;
			break;
		}
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
	if (!armNavituneTimer_ || !mem_ || !naviSongAddr_)
		return;
	const uint8_t lo = (uint8_t)(naviSongAddr_ & 0xff);
	const uint8_t hi = (uint8_t)(naviSongAddr_ >> 8);
	const unsigned base = (initPc_ > 0) ? (unsigned)initPc_ : 0;
	const unsigned end = base + 0x100u;
	for (unsigned a = base; a + 8u < end && a + 8u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x07
			&& mem_[a + 2] == 0x01
			&& mem_[a + 5] == 0xCD && mem_[a + 6] == 0x00 && mem_[a + 7] == 0x4D) {
			mem_[a + 3] = lo;
			mem_[a + 4] = hi;
			return;
		}
	}
}

unsigned CHardPc88::NavituneRetargetPc() const
{
	if (!armNavituneTimer_ || !mem_)
		return 0;
	const unsigned base = (initPc_ > 0) ? (unsigned)initPc_ : 0;
	const unsigned end = base + 0x100u;
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
	/* Falcom specialty PATCH at E000: JR + IM2 table, DI on play. */
	if (mem_[0xE000] == 0x18 && mem_[0xE017] == 0xF3 && mem_[0xE018] == 0xED)
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

int CHardPc88::IgnoreSoundIrqMask() const
{
	/* Falcom OUT (32),OR 80 around JP (HL) into type=prog at 0000. */
	if (!cpu_ || mem_[0xE000] != 0x18 || mem_[0xE017] != 0xF3)
		return 0;
	return cpu_->r.pc < 0xE000 ? 1 : 0;
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
		   mfile sets (shnghai2) also OUT 0 as a latch with live 5C/5D. */
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
		/* Falcom E000 PATCH: OUT (02),song maps type=prog bank
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
	}
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
