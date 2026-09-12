#include "StdAfx.h"
#include "cemu_hard_x1.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum {
	X1_CPU_HZ = 4000000,
	X1_OPM_HZ = 4000000,
	X1_AY_HZ = 2000000
};

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

static int IsX1Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "x1") == 0) return 1;
	if (_stricmp(ge->dataDir, "x1") == 0) return 1;
	if (_stricmp(ge->subtype, "x1") == 0 || _stricmp(ge->subtype, "x1psg") == 0) return 1;
	return 0;
}

/* KOEI YDOS3X: require the load+10 "OVL-1" marker at D200 or D000.
   Do NOT key off F800 stub alone — many mucom titles leave 0xF37C there
   and must keep hoot IM2 vec 0/6 (F37C-only detection silenced them with
   high opmW / peak=0). */
static int MemHasYdos(const uint8_t* mem)
{
	if (!mem) return 0;
	if (mem[0xD20A] == 'O' && mem[0xD20B] == 'V' && mem[0xD20C] == 'L'
		&& mem[0xD20D] == '-' && mem[0xD20E] == '1')
		return 1;
	if (mem[0xD00A] == 'O' && mem[0xD00B] == 'V' && mem[0xD00C] == 'L'
		&& mem[0xD00D] == '-' && mem[0xD00E] == '1')
		return 1;
	return 0;
}

/* Gen1 sangoku ships YDOS3X.SYS without a readable OVL-1 at load (ciphered
   until PATCH runs). Catalog rom name enables CIM mirror in LoadRoms. */
static int GeHasYdosRom(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		const char* n = ge->rom[i].name;
		if (!n || !n[0]) continue;
		if (_strnicmp(n, "YDOS", 4) == 0) return 1;
	}
	return 0;
}

static int X1IsYdos(CHardX1* hw)
{
	return (hw && (hw->ydosRom_ || MemHasYdos(hw->Mem()))) ? 1 : 0;
}

/* Tecnosoft kugyoku: `CP FF; JR Z; LD B,A; AND 0F; CALL drv`. Port 1/F
   carry the 0x80/0x81/0xFF command; AND 0F is the track nibble. Using lo
   as the latch made 0x80000003 call drv with A=3 (no such track). */
static int CEmuX1TecnoCmdHi(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 8u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0xFE || p[1] != 0xFF)
			continue;
		for (unsigned j = 2; j < 12u && i + j + 1u < patchLen; j++) {
			if (p[j] == 0xE6 && p[j + 1] == 0x0F)
				return 1;
		}
	}
	return 0;
}

/* PATCH `LD HL,4000 / LD (drv+0x0A),HL` — Telenet dds/luxsor/yakyufan. The
   CTC ISR does `LD A,(drv+0x0F); OR A; RET Z` and skips TL programming when
   that byte stays 0 (high keyOn, hist60=0, peak=0). */
static uint16_t CEmuX1TelenetPlayGate(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 6u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 6u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0x21 || p[1] != 0x00 || p[2] != 0x40 || p[3] != 0x22)
			continue;
		const unsigned nn = (unsigned)p[4] | ((unsigned)p[5] << 8);
		if ((nn & 0xFFu) != 0x0Au || nn < 0xE000u)
			continue;
		return (uint16_t)(nn + 5u);
	}
	return 0;
}

/* Enix JESUS: `CP 09; JR NC` (port 1 must be 0-8) then later `CP 72`
   (port 0F is the OPMTBL index, which is the global music id). */
static int CEmuX1JesusSplit(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	int saw09 = 0, saw72 = 0;
	for (unsigned i = 0; i + 2u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0xFE && p[1] == 0x09 && i + 2u < patchLen && p[2] == 0x30)
			saw09 = 1;
		if (p[0] == 0xFE && p[1] == 0x72)
			saw72 = 1;
	}
	return (saw09 && saw72) ? 1 : 0;
}

/* Herzog OPMX1: `LD DE,2802; LD L,A; ADD HL,HL; ADD HL,DE` indexes a
   pointer table at mdata+2. revo2 DEMO: `LD A,(F5F8); CP 03; JP NC` treats
   song >= 3 as a mute/init path (MUS103 lo=3 hit that and wrote TL=7F). */
static int CEmuX1SongIdFromHi(const uint8_t* mem)
{
	if (!mem) return 0;
	for (unsigned a = 0; a + 5u < 0x10000u; a++) {
		if (mem[a] == 0x11 && mem[a + 1] == 0x02 && mem[a + 2] == 0x28)
			return 1;
		if (mem[a] == 0x3A && mem[a + 1] == 0xF8 && mem[a + 2] == 0xF5
			&& mem[a + 3] == 0xFE && mem[a + 4] == 0x03)
			return 1;
	}
	return 0;
}

/* Falcom xana2: `IN A,(0F); SUB 2` — family hi is always 0x02, track in lo.
   Ungated, that steal turned euphory 0x02000004 (song 2, bank 4) into track 4. */
static int CEmuX1FalcomLoTrack(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 4u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 4u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0xDB && p[1] == 0x0F && p[2] == 0xD6 && p[3] == 0x02)
			return 1;
		if ((p[0] == 0x0E && p[1] == 0x0F)
			|| (p[0] == 0x01 && p[1] == 0x0F && p[2] == 0x00)) {
			for (unsigned j = 2; j < 12u && i + j + 1u < patchLen; j++) {
				if (p[j] == 0xD6 && p[j + 1] == 0x02)
					return 1;
			}
		}
	}
	return 0;
}

/* ys2: `LD HL,C000; LD DE,4000; LD BC,1000; LDIR` — songs >= $20 copy BGM
   to $4000; songs < $20 still index a table there (mode 0). */
static int CEmuX1Ys2Mirror4000(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 11u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 11u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0x21 && p[1] == 0x00 && p[2] == 0xC0
			&& p[3] == 0x11 && p[4] == 0x00 && p[5] == 0x40
			&& p[6] == 0x01 && p[7] == 0x00 && p[8] == 0x10
			&& p[9] == 0xED && p[10] == 0xB0)
			return 1;
	}
	return 0;
}

/* Laplace: `LD C,0F; IN E,(C)` then `LD A,D5; OUT (1FA3); OUT (C),E` —
   port F is the CTC time constant, not a track index. */
static int CEmuX1LaplaceCtcF(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 4u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0x0E || p[1] != 0x0F || p[2] != 0xED || p[3] != 0x58)
			continue;
		for (unsigned j = 4; j + 1u < 16u && i + j + 1u < patchLen; j++) {
			if (p[j] == 0x3E && p[j + 1] == 0xD5)
				return 1;
		}
	}
	return 0;
}

/* wibarm: `LD BC,000F; IN A,(C); CP FF; CALL Z` — port F is the in-file
   track (0 = field) or $FF for the ending overlay, not a copy of port 1. */
static int CEmuX1WibarmPortF(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 8u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0x01 || p[1] != 0x0F || p[2] != 0x00)
			continue;
		for (unsigned j = 3; j + 4u < 12u && i + j + 4u < patchLen; j++) {
			if (p[j] == 0xFE && p[j + 1] == 0xFF && p[j + 2] == 0xCC)
				return 1;
		}
	}
	return 0;
}

/* ametruck: `IN A,(1); CP 03; JR NC` — port 1 is OPENING/HISCORE/ENDING.
   `IN A,(0F); CALL play` — hi selects the in-file variant (ROUTE 333). */
static int CEmuX1AmetruckPortF(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 10u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	int sawCp03 = 0, sawInF = 0;
	for (unsigned i = 0; i + 5u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0xFE && p[1] == 0x03 && p[2] == 0x30)
			sawCp03 = 1;
		if (p[0] == 0x01 && p[1] == 0x0F && p[2] == 0x00
			&& p[3] == 0xED && p[4] == 0x78)
			sawInF = 1;
	}
	return (sawCp03 && sawInF) ? 1 : 0;
}

static uint16_t CEmuX1TelenetPlayTempo(const uint8_t* mem, uint16_t gate)
{
	if (!mem || !gate) return 0;
	const uint8_t glo = (uint8_t)gate;
	const uint8_t ghi = (uint8_t)(gate >> 8);
	for (unsigned a = 0xE000u; a + 10u < 0x10000u; a++) {
		if (mem[a] == 0x3A && mem[a + 1] == glo && mem[a + 2] == ghi
			&& mem[a + 3] == 0xB7 && mem[a + 4] == 0xC8
			&& mem[a + 5] == 0x21 && mem[a + 8] == 0x35 && mem[a + 9] == 0xC0) {
			const unsigned nn = (unsigned)mem[a + 6] | ((unsigned)mem[a + 7] << 8);
			if (nn >= 0xE000u && nn < 0x10000u)
				return (uint16_t)nn;
		}
	}
	return 0;
}

void CHardX1::ArmTelenetPlayGate()
{
	if (!opmPlayGate_) return;
	const unsigned ptr = (unsigned)opmPlayGate_ - 5u;
	const unsigned bgm = (unsigned)mem_[ptr] | ((unsigned)mem_[ptr + 1] << 8);
	if (bgm < 0x100u || bgm >= 0xE000u) return;
	mem_[opmPlayGate_] = 1;
	/* ISR `DEC (tempo); RET NZ` from a zeroed BSS byte waits 256 ticks
	   (~5s) before the first TL write. Prime so the first IRQ runs. */
	if (opmPlayTempo_ && mem_[opmPlayTempo_] == 0)
		mem_[opmPlayTempo_] = 1;
}

CHardX1::CHardX1()
	: cpuHz_(X1_CPU_HZ)
	, opmHz_(X1_OPM_HZ)
	, ayHz_(X1_AY_HZ)
	, psgOnly_(0)
	, opnMode_(0)
	, initPc_(0xC000)
	, mdataAddr_(0x4000)
	, mdataSize_((unsigned)BGM_SIZE)
	, titleCode_(0)
	, playCmdLatch_(0)
	, playSongLatch_(0)
	, playSongLatchF_(0)
	, playCmdHoldIrqs_(0)
	, ydosCmdSeen_(0)
	, ydosInhibitReentry_(0)
	, ydosRom_(0)
	, opmPlayGate_(0)
	, opmPlayTempo_(0)
	, tecnoCmdHi_(0)
	, jesusSplitPorts_(0)
	, songIdFromHi_(0)
	, skipPrestageRam_(0)
	, skipTriggerStage_(0)
	, psgStatToggle_(0)
	, ys2Mirror4000_(0)
	, laplaceCtcF_(0)
	, wibarmPortF_(0)
	, falcomPortF_(0)
	, ametruckPortF_(0)
	, marsHoldIrq_(0)
	, marsSeenProg_(0)
	, marsPlayReady_(0)
	, cpu_(NULL)
	, chipOpm_(NULL)
	, chipOpn_(NULL)
	, chipAy_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, stageLimit_(0x10000u)
	, bgmStageOff_(0)
	, ctcVectorBase_(0)
	, ctcVectorProgrammed_(0)
{	hardKind = KIND_X1;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
	memset(ctcIe_, 0, sizeof(ctcIe_));
	memset(ctcExpectTc_, 0, sizeof(ctcExpectTc_));
	memset(ctcControl_, 0, sizeof(ctcControl_));
	memset(ctcTc_, 0, sizeof(ctcTc_));
	memset(ctcTcValid_, 0, sizeof(ctcTcValid_));
	for (int i = 0; i < 4; i++)
		xmlCtcVec_[i] = -1;
}

CHardX1::~CHardX1()
{
	Shutdown();
}

int CHardX1::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsX1Platform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = X1_CPU_HZ;
	opmHz_ = X1_OPM_HZ;
	ayHz_ = X1_AY_HZ;
	psgOnly_ = (_stricmp(ge->subtype, "psg") == 0 || _stricmp(ge->subtype, "x1psg") == 0) ? 1 : 0;
	opnMode_ = (_stricmp(ge->subtype, "opn") == 0) ? 1 : 0;
	/* init_pc / mdata_* finalized in LoadRoms after roms+options are known. */
	initPc_ = 0xC000;
	mdataAddr_ = 0x4000;
	mdataSize_ = (unsigned)BGM_SIZE;
	if (opnMode_)
		chipOpn_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0, sampleRate_);
	else if (!psgOnly_)
		chipOpm_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
	chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
	cpu_ = new Ay_Cpu();
	return (cpu_ && chipAy_ && (psgOnly_ || chipOpm_ || chipOpn_)) ? 1 : 0;
}

void CHardX1::FreeBanks()
{
	for (int i = 0; i < 128; i++) {
		if (bgmBank_[i]) {
			free(bgmBank_[i]);
			bgmBank_[i] = NULL;
		}
		bgmBankSize_[i] = 0;
		bgmPresent_[i] = 0;
	}
}

void CHardX1::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	FreeBanks();
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chipOpm_) {
		CEmuChipYm2151Destroy(chipOpm_);
		chipOpm_ = NULL;
	}
	if (chipOpn_) {
		CEmuChipYm2608Destroy(chipOpn_);
		chipOpn_ = NULL;
	}
	if (chipAy_) {
		CEmuChipAyDestroy(chipAy_);
		chipAy_ = NULL;
	}
}

void CHardX1::StageBgm(uint8_t index)
{
	/* Exact bank only — no YDOS bank0 guess. */
	uint8_t use = index;
	if (use >= 128 || !bgmPresent_[use] || !bgmBank_[use])
		return;
	unsigned n = bgmBankSize_[use];
	if (n > mdataSize_) n = mdataSize_;
	if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
	/* Clamp so BGM cannot clobber code loaded above mdata (crimson OP@7A00). */
	if (stageLimit_ > mdataAddr_) {
		unsigned room = (unsigned)stageLimit_ - (unsigned)mdataAddr_;
		if (n > room) n = room;
	}
	if (n == 0) return;
	unsigned srcOff = bgmStageOff_;
	if (srcOff >= bgmBankSize_[use])
		return;
	unsigned avail = bgmBankSize_[use] - srcOff;
	if (n > avail) n = avail;
	const uint8_t* src = bgmBank_[use] + srcOff;
	/* OUT 0 path: stage into IO window @5000 (hoot mucomx1 uses 8K window). */
	unsigned ioN = n;
	if (ioN > 0x2000u) ioN = 0x2000u;
	memcpy(ioport_ + 0x5000, src, ioN);
	if (ioN < 0x2000u)
		memset(ioport_ + 0x5000 + ioN, 0, 0x2000u - ioN);
	/* Mirror into RAM at mdata_addr (per-title option).
	   Never memset the full mdataSize_ window: sphari mdata@A000 + default
	   32K wiped OPMDRV@E000 (opmW=0); hayato mdata@C000 + 32K wiped top RAM.
	   Clear only the staged bytes + a short pad (same idea as YDOS). */
	if (mdataAddr_ + n <= 0x10000) {
		memcpy(mem_ + mdataAddr_, src, n);
		unsigned pad = 0x100;
		if (n + pad > mdataSize_) pad = (mdataSize_ > n) ? (mdataSize_ - n) : 0;
		if (pad && mdataAddr_ + n + pad <= 0x10000)
			memset(mem_ + mdataAddr_ + n, 0, pad);
	}
	/* ENDING's tick returns NZ while a note is held; the in-file loop
	   treats that as song-end (`JR NZ,$10E6` → JP $FE00). PSG I/O port A
	   bit5 also drops out of the wait (`JR NZ,$1095`). OPENING's player
	   returns Z and keeps bit5. */
	if (ametruckPortF_ && mem_[0x1098] == 0x20 && mem_[0x1099] == 0x4C) {
		mem_[0x1098] = mem_[0x1099] = 0x00;
		if (mem_[0x10C3] == 0x20 && mem_[0x10C4] == 0xD0)
			mem_[0x10C3] = 0x18;
	}
	/* ys2 mode 0 indexes a pointer table at $4000 without the C000 LDIR.
	   Ending (lo==$20) LDIR C000→$2800; mirror here so $16C6 sees data
	   even if the first PATCH stop returned late. */
	if (ys2Mirror4000_ && n) {
		unsigned n4 = n;
		if (n4 > 0x1000u) n4 = 0x1000u;
		memcpy(mem_ + 0x4000, src, n4);
		if ((titleCode_ & 0xffu) == 0x20u)
			memcpy(mem_ + 0x2800, src, n4);
	}
	mem_[LOAD_FLAG] = 0xff;
}

/* CEMU_X1_CTC_TRACE=1 dumps the guest's CTC programming (both the main-board
   CTC at 1FA0 and the CZ-8BS1 sound-board CTC at 0704) so the tick source and
   its divider can be read off instead of guessed. */
static void X1CtcTrace(CHardX1* hw, uint16_t port, uint8_t data)
{
	static int mode = -1;
	static int left = 0;
	if (mode < 0) {
		const char* e = getenv("CEMU_X1_CTC_TRACE");
		mode = (e && *e && *e != '0') ? 1 : 0;
		left = 64;
	}
	if (!mode || left <= 0) return;
	left--;
	Ay_Cpu* cpu = hw ? hw->Cpu() : NULL;
	printf("[ctc] port=%04X data=%02X pc=%04X%s\n", port, data,
		cpu ? cpu->r.pc : 0,
		(data & 0x01) ? "  ctrl" : "  vec/tc");
}

void CHardX1::CtcReset()
{
	ctcVectorBase_ = 0;
	ctcVectorProgrammed_ = 0;
	memset(ctcIe_, 0, sizeof(ctcIe_));
	memset(ctcExpectTc_, 0, sizeof(ctcExpectTc_));
	memset(ctcControl_, 0, sizeof(ctcControl_));
	memset(ctcTc_, 0, sizeof(ctcTc_));
	memset(ctcTcValid_, 0, sizeof(ctcTcValid_));
}

void CHardX1::CtcWrite(int channel, uint8_t data)
{
	if (channel < 0 || channel > 3) return;
	if (ctcExpectTc_[channel]) {
		/* Time constant: 0 means 256 (Zilog CTC). */
		ctcTc_[channel] = data;
		ctcTcValid_[channel] = 1;
		ctcExpectTc_[channel] = 0;
		return;
	}
	if ((data & 0x01) == 0) {
		/* Interrupt vector load (Zilog: channel 0 only; bits7-3 = base). */
		if (channel == 0) {
			ctcVectorBase_ = (uint8_t)(data & 0xf8);
			ctcVectorProgrammed_ = 1;
		}
		return;
	}
	/* Control word: bit7=IE, bit6=counter, bit5=prescale /256, bit2=TC follows. */
	ctcControl_[channel] = data;
	ctcIe_[channel] = (data & 0x80) ? 1 : 0;
	ctcExpectTc_[channel] = (data & 0x04) ? 1 : 0;
	if (ctcExpectTc_[channel])
		ctcTcValid_[channel] = 0;
}

unsigned CHardX1::CtcTimerPeriodCycles(int channel) const
{
	if (channel < 0 || channel > 3) return 0;
	if (!ctcTcValid_[channel]) return 0;
	/* Counter mode (bit6): host still uses vsync/default — not a free timer. */
	if (ctcControl_[channel] & 0x40) return 0;
	unsigned tc = ctcTc_[channel] ? (unsigned)ctcTc_[channel] : 256u;
	const unsigned prescale = (ctcControl_[channel] & 0x20) ? 256u : 16u;
	return tc * prescale;
}

unsigned CHardX1::CtcCounterTc(int channel) const
{
	if (channel < 0 || channel > 3) return 0;
	if (!ctcTcValid_[channel]) return 0;
	/* Counter mode only (bit6): the channel divides its trigger input. */
	if (!(ctcControl_[channel] & 0x40)) return 0;
	return ctcTc_[channel] ? (unsigned)ctcTc_[channel] : 256u;
}

uint8_t CHardX1::CtcVector(int channel) const
{
	if (channel < 0 || channel > 3)
		return 0;
	/* Guest-programmed CTC base wins (manreq OPMDRV writes 0x18 → ch3=0x1E). */
	if (ctcVectorProgrammed_)
		return (uint8_t)(ctcVectorBase_ + (uint8_t)(channel * 2));
	/* XML ctc0/ctc3 = hoot use_ctcN vector override. */
	if (xmlCtcVec_[channel] >= 0)
		return (uint8_t)(xmlCtcVec_[channel] & 0xff);
	/* hoot mucomx1 defaults: TIMER ch0→0, VSYNC ch3→6. */
	return (uint8_t)(channel * 2);
}

uint8_t CHardX1::PortIn(uint16_t port)
{
	const uint16_t p = port;
	/* Command / song mailbox — level-readable. Cleared by playCmdHoldIrqs_
	   decay (and optionally OUT0). YDOS PATCH returns to the IN-wait loop
	   after dispatch; latch must stay high long enough for OUT0 StageBgm.
	   Mark ydosCmdSeen_ so accidental IRQ OUT0,0 before the wait-loop IN
	   cannot drop the play edge. */
	if (p == 0x0000) {
		/* After YDOS pointer-build OUT0, keep latch for hold accounting but
		   return 0 so PATCH cannot re-enter 0x91 before/after 0x90. */
		if (ydosInhibitReentry_)
			return 0;
		if (playCmdLatch_ && X1IsYdos(this) && cpu_
			&& cpu_->r.pc >= 0x0020 && cpu_->r.pc < 0x0070)
			ydosCmdSeen_ = 1;
		return playCmdLatch_;
	}
	if (p == 0x0001)
		return playSongLatch_;
	/* Falcom xana2 PATCH: IN A,(0F); SUB 2 indexes the play/IRQ vector
	   table. Mirror the song latch (hoot music id base 2).
	   Herzog/revo2: port 1 is a non-zero play command; port 0F is the
	   in-file track (0 is valid — herzog PATCH `OR A; JR Z` skips play). */
	if (p == 0x000f)
		return (jesusSplitPorts_ || songIdFromHi_ || laplaceCtcF_ || ys2Mirror4000_
			|| wibarmPortF_ || falcomPortF_ || ametruckPortF_
			|| (initPc_ == 0xE900 && opmPlayGate_))
			? playSongLatchF_ : playSongLatch_;
	if (opnMode_ && chipOpn_ && (p & 0xff) == 0xe0)
		return chipOpn_->ReadStatus();
	if (opnMode_ && chipOpn_ && (p & 0xff) == 0xe1)
		return chipOpn_->ReadData();
	if (!psgOnly_ && chipOpm_ && (p == 0x0700 || p == 0x0701))
		return chipOpm_->ReadStatus();
	/* No CZ-8BS1 in psg xml: OPM status bit7 must read clear.
	   Square PROG $1A40 `IN A,(0700); BIT 7; JP NZ` and T&E
	   $8A58 `IN A,(0701); JP M` otherwise spin forever. */
	if (psgOnly_ && (p == 0x0700 || p == 0x0701))
		return 0;
	/* OUT 0704,$47 then $5A (or $47) echoes through ioport, so the
	   "OPM board present" probe succeeds on a PSG-only machine.
	   KING' KNIGHT then stores ($000C)=1 and ISR $1139 CALL $169A;
	   PSY-O-BLADE stores ($00A3)=1 and $0082 JP $8A66. Both write
	   0700 which psgOnly_ ignores. Returning 0 fails the probe so
	   those drivers take the AY path. CTC OUTs still program the
	   live timer — only the echo is suppressed. */
	if (psgOnly_ && p >= 0x0704 && p <= 0x0707)
		return 0;
	if (p == 0x1a01) {
		/* Bit2 = ready (Laplace / Dempa BIT 2). Bit7 must alternate:
		   mars ISR `IN A,(1A01); JP P` waits for S=1 then `JP M` for S=0.
		   A constant 0x04 (S=0) never leaves the first wait; 0x80 fails BIT 2. */
		psgStatToggle_ ^= 1;
		return psgStatToggle_ ? 0x84 : 0x04;
	}
	/* Microcabin msnk: busy-wait IN A,(0FF8); AND 81; JP NZ — clear = ready. */
	if (p == 0x0ff8 || p == 0x0ff9 || p == 0x0ffc)
		return 0x00;
	/* X1 PSG is decoded on the high byte. DRIVER `OUT (C),A` keeps the
	   data in C, so the port is 1C<data> / 1B<data> not 1C00/1B00. */
	{
		const uint16_t ph = (uint16_t)(p & 0xff00);
		if (ph == 0x1c00 || ph == 0x1b00 || ph == 0x1900
			|| (ph == 0x1a00 && (p & 0xff) != 0x01)) {
			if (chipAy_) return chipAy_->ReadData();
			return 0xff;
		}
	}
	return ioport_[p];
}

void CHardX1::PortOut(uint16_t port, uint8_t data)
{
	const uint16_t p = port;
	if (p == 0x0000) {
		int ydos = X1IsYdos(this);
		/* Mucom intentionally OUTs the BGM bank index here. KOEI YDOS PATCH
		   does `IN A,(1); DEC C; OUT (C),C` while building a CIM pointer —
		   that accidental OUT 0,0 must NOT StageBgm/clobber the CIM.
		   Arm re-entry inhibit so 0x90 still runs, then wait-loop INs see 0. */
		if (ydos && data == 0) {
			int patchPtr = (cpu_ && cpu_->r.pc >= 0x0040 && cpu_->r.pc < 0x0050);
			if (ydosCmdSeen_ && patchPtr)
				ydosInhibitReentry_ = 1;
		} else {
			uint8_t bank = data;
			StageBgm(bank);
			playCmdLatch_ = 0;
			playCmdHoldIrqs_ = 0;
			ydosCmdSeen_ = 0;
			ydosInhibitReentry_ = 0;
		}
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		return;
	}
	/* Telenet PATCH: IN A,(1); OUT (C),A echoes the song then CALL play.
	   The cmd latch is level-high for ~1.5s; play itself takes ~1s of DI
	   so the wait loop sees cmd=1 again and re-enters. luxsor's play
	   XOR-clears the ISR gate and LDIRs the work RAM — GAPPY/STOPS.
	   Consume the edge here so play is one-shot. */
	if (p == 0x0001 && opmPlayGate_) {
		playCmdLatch_ = 0;
		playCmdHoldIrqs_ = 0;
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		ioport_[p] = data;
		return;
	}
	/* produce: PATCH `CALL stop` at PROG1 $0285, then OUT 1,song, then
	   `CALL play` at the overlay's $0AB8/$29AE. Overlay here, not at
	   TriggerPlay (that would wipe stop before it runs). */
	if (p == 0x0001 && skipTriggerStage_) {
		StageBgm(data);
		playCmdLatch_ = 0;
		playCmdHoldIrqs_ = 0;
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		ioport_[p] = data;
		return;
	}
	/* Laplace / wibarm: wait-loop re-entry. OUT 1 echoes then CALL play;
	   a level-high cmd re-inits (DI) every pass. wibarm's $FF path LDDRs
	   the song header — a second pass slides it twice and goes silent. */
	if (p == 0x0001 && (laplaceCtcF_ || wibarmPortF_)) {
		playCmdLatch_ = 0;
		playCmdHoldIrqs_ = 0;
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		ioport_[p] = data;
		return;
	}
	if (opnMode_ && chipOpn_ && ((p & 0xff) == 0xe0 || (p & 0xff) == 0xe1)) {
		chipOpn_->Write((uint32_t)(p & 1), data);
		return;
	}
	if (!psgOnly_ && chipOpm_ && (p == 0x0700 || p == 0x0701)) {
		chipOpm_->Write((uint32_t)(p & 1), data);
		return;
	}
	/* CZ-8BS1 carries its own Z80 CTC at 0704-0707 next to the OPM.
	   Same 4 channels as the main-board 1FA0 map — program the live CTC. */
	if (p >= 0x0704 && p <= 0x0707) {
		X1CtcTrace(this, p, data);
		CtcWrite((int)(p - 0x0704), data);
		ioport_[p] = data;
		return;
	}
	{
		const uint16_t ph = (uint16_t)(p & 0xff00);
		if (ph == 0x1b00 || (ph == 0x1a00 && (p & 0xff) != 0x01)) {
			/* hoot passes port>>8 to ssAY8910: 1B is odd, therefore data.
			   mars also clocks data at 1A00 next to status 1A01.
			   Ignore C in the low byte — Laplace 8613 leaves data there. */
			if (chipAy_) chipAy_->Write(1, data);
			ioport_[p] = data;
			return;
		}
		if (ph == 0x1c00 || ph == 0x1900) {
			/* 1C is even, therefore the AY address latch. mars uses 1900. */
			if (chipAy_) chipAy_->Write(0, data);
			ioport_[p] = data;
			return;
		}
	}
	/* Z80 CTC: MAME maps 1FA0-1FA3 and mirror 1FA8-1FAB. */
	if ((p >= 0x1fa0 && p <= 0x1fa3) || (p >= 0x1fa8 && p <= 0x1fab)) {
		X1CtcTrace(this, p, data);
		CtcWrite((int)(p & 3), data);
		ioport_[p] = data;
		return;
	}
	ioport_[p] = data;
}

void CHardX1::MemWrite(uint16_t addr, uint8_t data)
{
	mem_[addr] = data;
}

uint8_t CHardX1::MemRead(uint16_t addr)
{
	return mem_[addr];
}

void CHardX1::UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut,
	int ydos)
{
	/* hoot X1/NCS: 0xSS0000BB → song=SS bank=BB; 0xSS000000 → song=SS bank=0;
	   low-only 0x000000NN → song=NN bank=NN (legacy StageBgm(song)).
	   Extended mid!=0 codes (ishtar 0x00028408) keep lo as song/bank.
	   Humming Bird Laplace uses hi as a flags byte (0x68..0xC0) with the
	   real song/bank in lo — treating hi as song yielded song=0x80 and silent
	   DRIVER calls. Threshold is 0x60 so Falcom xanaopm 0x48000001 / 0x2C000002
	   still unpack as song=hi (MML id) bank=lo.
	   KOEI YDOS: 0x010000SS = looping, 0x000000SS = one-shot. hi is a play
	   flag, not the track. Using hi as song made every 0x01****** title
	   play track 1 (SAMESONG on sangoku/suiko/sangoku2). NCS 0x01000000
	   really is song 1 — do not apply this unless ydos. */
	const unsigned lo = titleCode & 0xffu;
	const unsigned hi = (titleCode >> 24) & 0xffu;
	const unsigned mid = (titleCode >> 8) & 0xffffu;
	uint8_t song = 0, bank = 0;
	if (ydos && mid == 0 && hi == 1) {
		song = (uint8_t)lo;
		bank = 0;
	} else if (mid == 0 && hi >= 0x60) {
		song = (uint8_t)lo;
		bank = (uint8_t)lo;
	} else if (mid == 0 && (hi != 0 || lo != 0)) {
		/* Standard hoot pack — song may be 0 (main theme). */
		song = (uint8_t)hi;
		bank = (uint8_t)lo;
		if (hi == 0 && lo != 0) {
			song = (uint8_t)lo;
			bank = (uint8_t)lo;
		}
	} else if (lo != 0) {
		song = (uint8_t)lo;
		bank = (uint8_t)lo;
	} else if (hi != 0 && hi < 0x40) {
		song = (uint8_t)hi;
		bank = 0;
	} else {
		song = 0;
		bank = 0;
	}
	if (songOut) *songOut = song;
	if (bankOut) *bankOut = bank;
}

void CHardX1::PrestageBgm(unsigned titleCode)
{
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank, ydosRom_);
	bgmStageOff_ = 0;
	if (laplaceCtcF_) {
		const unsigned lo = titleCode & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (lo < 128 && bgmPresent_[lo] && bgmBank_[lo])
			bank = (uint8_t)lo;
		bgmStageOff_ = mid;
	}
	/* hyd2: lo is flags:bank nibbles (0x12 = flag1 bank2). Staging 0x12
	   misses PROG2/PROG3. The $4000 mirror also smashes the $81xx player. */
	if (initPc_ == 0xFE00) {
		const unsigned nibble = (titleCode & 0x0fu);
		if (nibble < 128 && bgmPresent_[nibble] && bgmBank_[nibble])
			bank = (uint8_t)nibble;
	}
	uint8_t stage = bank;
	if (!(stage < 128 && bgmPresent_[stage] && bgmBank_[stage])) {
		if (song < 128 && bgmPresent_[song] && bgmBank_[song])
			stage = song;
		else
			stage = 0xff;
	}
	if (stage < 128 && bgmPresent_[stage] && bgmBank_[stage]) {
		if ((mdataAddr_ == 0x4000 || mdataAddr_ == 0) && initPc_ != 0xFE00) {
			unsigned n = bgmBankSize_[stage];
			if (n > mdataSize_) n = mdataSize_;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			memset(mem_ + 0x4000, 0, n);
			memcpy(mem_ + 0x4000, bgmBank_[stage], n);
		}
		/* Overlay-on-play: keep boot code (produce PROG1, xanaopm PR.NO0)
		   until PATCH has inited. TriggerPlay still StageBgm. */
		if (!skipPrestageRam_)
			StageBgm(stage);
	}
	/* Keep play mailbox idle through DRIVER boot. */
	playCmdLatch_ = 0;
	playSongLatch_ = 0;
	playSongLatchF_ = 0;
	playCmdHoldIrqs_ = 0;
	ydosCmdSeen_ = 0;
	ydosInhibitReentry_ = 0;
	mem_[PLAY_FLAG] = 0;
	mem_[PLAY_CODE] = 0;
}

void CHardX1::TriggerPlay(unsigned titleCode)
{
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank, ydosRom_);
	/* Dual mailbox: port latch (Falcom) + C010/C011 when free (hoot Play).
	   Stage IO@5000 always; RAM mirror only when StageBgm deems safe. */
	playCmdLatch_ = 0x01;
	/* Falcom xana2: fixed family hi=0x02, track id in lo (port0F / CP 1Ah). */
	{
		const unsigned lo = titleCode & 0xffu;
		const unsigned hi = (titleCode >> 24) & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (mid == 0 && hi == 2 && falcomPortF_)
			playSongLatch_ = lo ? (uint8_t)lo : (uint8_t)hi;
		else
			playSongLatch_ = song;
		/* Tecnosoft: hi is 0x80/0x81/0xFF (play / skip-intro / with-intro).
		   Bank is already `lo` from UnpackTitle's hi>=0x40 path. */
		if (tecnoCmdHi_ && mid == 0 && hi >= 0x80)
			playSongLatch_ = (uint8_t)hi;
		/* JESUS: port 1 = copy descriptor 0-8, port 0F = OPMTBL index.
		   Global ids >= 9 used to share port 1 and were skipped (CP 09). */
		if (jesusSplitPorts_) {
			playSongLatchF_ = song;
			playSongLatch_ = (song < 9) ? song : bank;
		}
		if (songIdFromHi_ && mid == 0 && hi < 0x80 && !tecnoCmdHi_ && !falcomPortF_) {
			playSongLatchF_ = (uint8_t)hi;
			/* Herzog PATCH `IN A,(1); OR A; JR Z` skips CALL play when A=0.
			   Track 0 still has to reach 1800 with port 0F = 0. */
			playSongLatch_ = hi ? (uint8_t)hi : 1;
		}
		bgmStageOff_ = 0;
		if (laplaceCtcF_) {
			if (lo < 128 && bgmPresent_[lo] && bgmBank_[lo])
				bank = (uint8_t)lo;
			bgmStageOff_ = mid;
			/* Port F is CTC TC (0 = 256), not the track. */
			playSongLatchF_ = 0;
		}
		/* ys2: port 1 is the PATCH command (CP $20 selects mode 0/1);
		   port F is the in-file track. TTLMSn (lo>=$30) PLAYS as
		   in-game MANPR1; title-engine (port1>=$20) STOPS on part 0. */
		if (ys2Mirror4000_ && mid == 0 && !laplaceCtcF_) {
			if (lo >= 0x30u) {
				playSongLatch_ = 1;
				playSongLatchF_ = (uint8_t)hi;
			} else {
				playSongLatch_ = hi ? (uint8_t)hi : (uint8_t)lo;
				playSongLatchF_ = (uint8_t)hi;
			}
		}
		if (wibarmPortF_ && mid == 0 && !laplaceCtcF_ && !ys2Mirror4000_) {
			/* 0x00000002 field = track 0; 0x01000002 battle = track 1;
			   0xFF000003 ending = overlay + track 0. Port 1 must stay NZ. */
			playSongLatchF_ = (uint8_t)hi;
			playSongLatch_ = (hi && hi != 0xFFu) ? (uint8_t)hi : 1;
		}
		/* xana2 PSG/OPM: port F is the PR.NOx family (hi), SUB 2 indexes
		   the play/IRQ vectors. lo is only the staged m.000x bank. */
		if (falcomPortF_ && mid == 0 && hi && !wibarmPortF_ && !laplaceCtcF_
			&& !ys2Mirror4000_ && !jesusSplitPorts_) {
			playSongLatchF_ = (uint8_t)hi;
		}
		if (ametruckPortF_ && mid == 0 && !wibarmPortF_ && !falcomPortF_
			&& !laplaceCtcF_ && !ys2Mirror4000_) {
			/* Port 1 = file 0-2 (must stay < 3); port F = variant in hi. */
			playSongLatch_ = (uint8_t)lo;
			playSongLatchF_ = (uint8_t)hi;
		}
		/* Telenet luxsor/yakyufan PATCH `IN A,(0F); CALL play`. Play
		   stores A in $EA1D/$EA18 then copies it to ix+23. 0 = restart
		   at table+2 (infinite loop). Hoot's ioport[0x0F] is never
		   written, so it stays 0. Returning the song id here wrapped
		   BGM N times then STOPS (seq=2). */
		if (initPc_ == 0xE900 && opmPlayGate_ && !ametruckPortF_
			&& !wibarmPortF_ && !falcomPortF_ && !laplaceCtcF_
			&& !ys2Mirror4000_)
			playSongLatchF_ = 0;
		if (initPc_ == 0xFE00) {
			playSongLatch_ = (uint8_t)lo;
			const unsigned nibble = lo & 0x0fu;
			if (nibble < 128 && bgmPresent_[nibble] && bgmBank_[nibble])
				bank = (uint8_t)nibble;
		}
		/* x1sc EFC00P / euphory PSG: each staged MUS/DEM is one song.
		   Unpack of 0x000000NN sets song=NN, which is OOB. Play 0. */
		if (psgOnly_ && mid == 0 && hi == 0) {
			if ((initPc_ == 0xF000 && mdataAddr_ == 0x5000 && mem_[0xEB00] != 0)
				|| (initPc_ == 0xF000 && mdataAddr_ == 0x9000
					&& mdataSize_ == 0x1000))
				playSongLatch_ = 0;
		}
		/* x1sc DRIVER $3E5B treats a 0704 CTC readback as "OPM board"
		   and stores 1 at $0003. ISR/play then take the FM path and
		   write 0700, which psgOnly_ ignores. EFC00P starts $48. */
		if (psgOnly_ && initPc_ == 0xF000 && mdataAddr_ == 0x5000
			&& mem_[0xEB00] == 0x48)
			mem_[0x0003] = 0;
	}
	playCmdHoldIrqs_ = 90; /* ~1.5s hold so slow PATCH polls see cmd before OUT0/clear */
	ydosCmdSeen_ = 0;
	ydosInhibitReentry_ = 0;
	{
		int mailboxFree = 1;
		if (initPc_ >= 0xC000 && initPc_ < 0xC100)
			mailboxFree = 0;
		/* gaia/hayato: mdata_addr=0xC000 — poking C010/C011 corrupts BGM
		   headers (music writes C0/1A back over the mailbox). Port latch only. */
		if (mdataAddr_ <= PLAY_FLAG
			&& (unsigned)mdataAddr_ + mdataSize_ > (unsigned)PLAY_FLAG)
			mailboxFree = 0;
		if (mailboxFree) {
			mem_[PLAY_FLAG] = 0x01;
			mem_[PLAY_CODE] = playSongLatch_;
		}
	}

	uint8_t stage = bank;
	if (!(stage < 128 && bgmPresent_[stage] && bgmBank_[stage])) {
		if (song < 128 && bgmPresent_[song] && bgmBank_[song])
			stage = song;
		else
			stage = 0xff;
	}
	if (stage < 128 && bgmPresent_[stage] && bgmBank_[stage]) {
		if ((mdataAddr_ == 0x4000 || mdataAddr_ == 0) && initPc_ != 0xFE00) {
			unsigned n = bgmBankSize_[stage];
			if (n > mdataSize_) n = mdataSize_;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			memset(mem_ + 0x4000, 0, n);
			memcpy(mem_ + 0x4000, bgmBank_[stage], n);
		}
		if (!skipTriggerStage_)
			StageBgm(stage);
	}
	ArmTelenetPlayGate();
}

unsigned CHardX1::OpmWrites() const
{
	if (chipOpn_) {
		unsigned w = 0;
		CEmuChipYm2608GetPlayMetrics(chipOpn_, &w, NULL, NULL, NULL, NULL);
		return w;
	}
	return chipOpm_ ? CEmuChipYm2151WriteCount(chipOpm_) : 0;
}

unsigned CHardX1::AyWrites() const
{
	return chipAy_ ? CEmuChipAyWriteCount(chipAy_) : 0;
}

int CHardX1::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge || !cpu_) return 0;
	titleCode_ = titleCode;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	CtcReset();
	psgStatToggle_ = 0;
	for (int i = 0; i < 4; i++)
		xmlCtcVec_[i] = -1;
	/* hoot X1 driver use_ctcN / game option ctcN = IM2 vector for that channel. */
	{
		static const char* kNames[4] = { "ctc0", "ctc1", "ctc2", "ctc3" };
		static const char* kUseNames[4] = { "use_ctc0", "use_ctc1", "use_ctc2", "use_ctc3" };
		for (int ch = 0; ch < 4; ch++) {
			int v = CEmuParseOptHex(ge, kNames[ch], -1);
			if (v < 0) v = CEmuParseOptHex(ge, kUseNames[ch], -1);
			/* use_ctcN value 0 means disabled in hoot — keep default. */
			if (v > 0)
				xmlCtcVec_[ch] = v & 0xff;
		}
	}
	int loadedCode = 0;
	ydosRom_ = (uint8_t)(GeHasYdosRom(ge) ? 1 : 0);
	opmPlayGate_ = 0;
	opmPlayTempo_ = 0;
	tecnoCmdHi_ = 0;
	jesusSplitPorts_ = 0;
	songIdFromHi_ = 0;

	/* Resolve mdata window early so oversized code (Falcom PR.NO2 @0 with
	   mdata@5c00) cannot spill into the music region before Prestage. */
	mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", 0x4000);
	{
		int ms = CEmuParseOptHex(ge, "mdata_size", (int)BGM_SIZE);
		int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
		if (mfs > ms) ms = mfs;
		if (ms <= 0 || ms > BGM_SIZE) ms = BGM_SIZE;
		mdataSize_ = (unsigned)ms;
	}
	int vdataAddr = CEmuParseOptHex(ge, "vdata_addr", -1);
	int vdataSize = CEmuParseOptHex(ge, "vdata_size", 0);
	if (vdataSize <= 0) vdataSize = CEmuParseOptHex(ge, "vfile_size", 0);
	int hasMdataOpt = 0, hasBgmRom = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "mdata_addr") == 0) {
			hasMdataOpt = 1;
			break;
		}
	}
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "bgm") == 0) {
			hasBgmRom = 1;
			break;
		}
	}

	for (int pass = 0; pass < 2; pass++) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		const int isVoice = (_stricmp(r->type, "voice") == 0 || _stricmp(r->type, "vdata") == 0);
		const int isCode = (_stricmp(r->type, "code") == 0);
		/* Pass0: voice underlay (Falcom). Pass1: code/data/bgm — code wins. */
		if (pass == 0 && !isVoice) continue;
		if (pass == 1 && isVoice) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		if (isCode) {
			int off = r->offset;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			/* Clamp code below mdata_addr when the blob would invade it
			   (xana2 PR.NO2 24K@0 vs mdata@5c00). Prefer vdata_size when
			   voice window starts at the same offset.
			   Default mdata $4000 with no catalog window and no bgm
			   (aspic PSG PROG @0FD0) must keep the player at $581E. */
			if ((hasMdataOpt || hasBgmRom) && (int)mdataAddr_ > off) {
				unsigned cap = (unsigned)((int)mdataAddr_ - off);
				if (vdataAddr == off && vdataSize > 0 && (unsigned)vdataSize < cap)
					cap = (unsigned)vdataSize;
				if (n > cap) n = cap;
			}
			memcpy(mem_ + off, data, n);
			loadedCode++;
		} else if (_stricmp(r->type, "data") == 0) {
			/* Catalog "data" is the hoot IO window. YDOS also LD the CIM as
			   Z80 RAM — mirror when safe. Unconditional mirrors at the mucom
			   0x4000 IO window broke jesus/sghost/zeliard (high opmW, peak=0).
			   Gen1 sangoku parks OPMDAT.CIM at B400 without an OVL-1 marker
			   at load time (decrypt later), so key off empty dest + non-4000. */
			int off = r->offset;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			memcpy(ioport_ + off, data, n);
			int ydos = X1IsYdos(this);
			int highCim = (off != 0x4000);
			if (ydos || highCim) {
				int unused = 1;
				unsigned probe = n < 64u ? n : 64u;
				for (unsigned i = 0; i < probe; i++) {
					if (mem_[off + i]) { unused = 0; break; }
				}
				if (unused)
					memcpy(mem_ + off, data, n);
			}
			if (ydos && !bgmPresent_[0] && n > 0) {
				unsigned bn = n;
				if (bn > (unsigned)BGM_SIZE) bn = (unsigned)BGM_SIZE;
				unsigned char* buf = (unsigned char*)malloc(BGM_SIZE);
				if (buf) {
					memset(buf, 0, BGM_SIZE);
					memcpy(buf, data, bn);
					bgmBank_[0] = buf;
					bgmBankSize_[0] = bn;
					bgmPresent_[0] = 1;
				}
			}
		} else if (_stricmp(r->type, "bgm") == 0) {
			int idx = r->offset;
			if (idx < 0 || idx >= 128) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(BGM_SIZE);
			if (!buf) continue;
			memset(buf, 0, BGM_SIZE);
			memcpy(buf, data, n);
			if (bgmBank_[idx]) free(bgmBank_[idx]);
			bgmBank_[idx] = buf;
			bgmBankSize_[idx] = n;
			bgmPresent_[idx] = 1;
		} else if (isVoice) {
			/* Falcom: voice underlay at vdata_addr for the selected track only.
			   Code pass then overlays PR.NO0/etc on top (xana2opm). */
			int vaddr = vdataAddr;
			int vsize = vdataSize;
			if (vsize <= 0) vsize = CEmuParseOptHex(ge, "vfile_size", (int)sz);
			if (vaddr < 0) continue;
			uint8_t song = 0, bank = 0;
			UnpackTitle(titleCode, &song, &bank, ydosRom_);
			int idx = r->offset;
			if (idx < 0) idx = 0;
			if (idx != (int)song && idx != (int)bank)
				continue;
			int dest = vaddr;
			if (dest < 0 || dest >= 0x10000) continue;
			unsigned n = sz;
			if (vsize > 0 && (unsigned)vsize < n) n = (unsigned)vsize;
			if (dest + (int)n > 0x10000) n = (unsigned)(0x10000 - dest);
			if ((int)mdataAddr_ > dest) {
				unsigned cap = (unsigned)((int)mdataAddr_ - dest);
				if (n > cap) n = cap;
			}
			memcpy(mem_ + dest, data, n);
		}
	}
	} /* pass */

	if (!loadedCode) return 0;

	/* Resolve start PC: explicit init_pc → PATCH code offset → first code → C000. */
	{
		int hasInit = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "init_pc") == 0) { hasInit = 1; break; }
		}
		if (hasInit) {
			initPc_ = (uint16_t)CEmuParseOptHex(ge, "init_pc", 0xC000);
		} else {
			int patchOff = -1, firstCode = -1;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (firstCode < 0) firstCode = r->offset;
				if (_stricmp(r->name, "PATCH") == 0)
					patchOff = r->offset;
			}
			if (patchOff >= 0)
				initPc_ = (uint16_t)patchOff;
			else if (firstCode >= 0)
				initPc_ = (uint16_t)firstCode;
			else
				initPc_ = 0xC000;
		}
		mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", 0x4000);
		{
			int ms = CEmuParseOptHex(ge, "mdata_size", (int)BGM_SIZE);
			int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
			if (mfs > ms) ms = mfs;
			if (ms <= 0 || ms > BGM_SIZE) ms = BGM_SIZE;
			mdataSize_ = (unsigned)ms;
		}
		/* Nearest code blob above mdata_addr caps StageBgm writes.
		   Code sitting inside the mdata window when mdata_addr==0 is an
		   overlay stub (xanaopm PR.NO0 @1000) — music is meant to replace
		   it after PATCH copies the player to $F000. crimson VOICE@6200
		   with mdata@4000 must still cap. */
		stageLimit_ = 0x10000u;
		skipPrestageRam_ = 0;
		skipTriggerStage_ = 0;
		ys2Mirror4000_ = 0;
		laplaceCtcF_ = 0;
		wibarmPortF_ = 0;
		falcomPortF_ = 0;
		ametruckPortF_ = 0;
		marsHoldIrq_ = 0;
		marsSeenProg_ = 0;
		marsPlayReady_ = 0;
		bgmStageOff_ = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0) continue;
			if (_stricmp(r->name, "PATCH") == 0) {
				if (r->offset > (int)mdataAddr_ && r->offset < (int)stageLimit_)
					stageLimit_ = (uint32_t)r->offset;
				continue;
			}
			if (r->offset == (int)mdataAddr_ && mdataAddr_ != 0) {
				/* produce: PROG1 at mdata is the player, not a music
				   overlay target. Staging PROG4 here wipes JP $0285. */
				skipPrestageRam_ = 1;
				skipTriggerStage_ = 1;
			}
			if (mdataAddr_ == 0 && r->offset > 0 && r->offset < (int)mdataSize_) {
				skipPrestageRam_ = 1;
				continue; /* overlay stub inside the music window */
			}
			if (r->offset > (int)mdataAddr_ && r->offset < (int)stageLimit_)
				stageLimit_ = (uint32_t)r->offset;
		}
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0) continue;
			if (_stricmp(r->name, "PATCH") != 0) continue;
			if (r->offset < 0 || r->offset >= 0x10000) break;
			unsigned plen = 256u;
			if (r->offset + (int)plen > 0x10000)
				plen = (unsigned)(0x10000 - r->offset);
			opmPlayGate_ = CEmuX1TelenetPlayGate(mem_, (unsigned)r->offset, plen);
			opmPlayTempo_ = CEmuX1TelenetPlayTempo(mem_, opmPlayGate_);
			tecnoCmdHi_ = (uint8_t)CEmuX1TecnoCmdHi(mem_, (unsigned)r->offset, plen);
			jesusSplitPorts_ = (uint8_t)CEmuX1JesusSplit(mem_, (unsigned)r->offset, plen);
			ys2Mirror4000_ = (uint8_t)CEmuX1Ys2Mirror4000(mem_, (unsigned)r->offset, plen);
			laplaceCtcF_ = (uint8_t)CEmuX1LaplaceCtcF(mem_, (unsigned)r->offset, plen);
			wibarmPortF_ = (uint8_t)CEmuX1WibarmPortF(mem_, (unsigned)r->offset, plen);
			falcomPortF_ = (uint8_t)CEmuX1FalcomLoTrack(mem_, (unsigned)r->offset, plen);
			ametruckPortF_ = (uint8_t)CEmuX1AmetruckPortF(mem_, (unsigned)r->offset, plen);
			/* OPENING lives at mdata $1000, so the produce-style
			   skipTriggerStage_ latch fires. Each file IS the player
			   — HISCORE/ENDING must overlay $1000. Also NOP the
			   `LD A,C9; LD ($107C),A` so CALL play runs the in-file
			   loop (CTC3 never ticks the IM2 ISR here). */
			if (ametruckPortF_) {
				skipTriggerStage_ = 0;
				if (mem_[0x0068] == 0x3E && mem_[0x0069] == 0xC9
					&& mem_[0x006A] == 0x32)
					memset(mem_ + 0x0068, 0x00, 5);
				/* OPENING's in-file JR never returns here. HISCORE already
				   has C9 at $1321 and ENDING RET NZ — both need the CTC3
				   IM2 tick, which this guest never gets (ch0 has no TC so
				   ZC0→TRG3 is idle). Poll the PATCH ISR instead. */
				if (mem_[0x0075] == 0x21 && mem_[0x0076] == 0x38
					&& mem_[0x0077] == 0x07) {
					mem_[0x0075] = 0xF3; /* DI */
					mem_[0x0076] = 0xCD;
					mem_[0x0077] = 0xA3;
					mem_[0x0078] = 0x00; /* CALL $00A3 */
					mem_[0x0079] = 0x18;
					mem_[0x007A] = 0xFB; /* JR $0076 */
				}
			}
			break;
		}
		/* ys2 PSG is PATCH2 (same C000→4000 LDIR). The PATCH-only scan
		   above never sees it, so TTLMS packing and the $2F0E NOP never
		   armed — port1 became $30 (title) and SSG volumes stayed 0. */
		if (!ys2Mirror4000_ && psgOnly_) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (_stricmp(r->name, "PATCH2") != 0) continue;
				if (r->offset < 0 || r->offset >= 0x10000) break;
				unsigned plen = 256u;
				if (r->offset + (int)plen > 0x10000)
					plen = (unsigned)(0x10000 - r->offset);
				ys2Mirror4000_ = (uint8_t)CEmuX1Ys2Mirror4000(mem_,
					(unsigned)r->offset, plen);
				break;
			}
		}
		/* ASPIC SPECIAL PSG PATCH2: ISR $0089 CALL $0071 (stop) every
		   VSYNC while $0097=0. NOP the per-tick stop so play can arm.
		   PROG play $581E ends `POP AF; OR A; RET Z` but PATCH2 never
		   pushes — that pops the return and RET Z back to $0000. RET. */
		if (psgOnly_ && initPc_ == 0
			&& mem_[0x0000] == 0xF3 && mem_[0x0001] == 0xED
			&& mem_[0x0013] == 0x32 && mem_[0x0014] == 0x32
			&& mem_[0x0015] == 0x58
			&& mem_[0x0089] == 0xCD && mem_[0x008A] == 0x71
			&& mem_[0x008B] == 0x00)
			memset(mem_ + 0x0089, 0x00, 3);
		if (psgOnly_ && initPc_ == 0
			&& mem_[0x581E] == 0x26 && mem_[0x581F] == 0x00
			&& mem_[0x5835] == 0xF1 && mem_[0x5836] == 0xB7
			&& mem_[0x5837] == 0xC8) {
			mem_[0x5835] = 0xC9;
			mem_[0x5836] = 0x00;
			mem_[0x5837] = 0x00;
		}
		songIdFromHi_ = (uint8_t)CEmuX1SongIdFromHi(mem_);
		/* hyd2: flag 0 (intro+loop) times out to CALL $FF46, which
		   CALL $FEAB = JP $81F2 and never reaches JP $8170 (loop).
		   Skip-intro already CALLs $8170 and PLAYS. */
		if (initPc_ == 0xFE00
			&& mem_[0xFF46] == 0xCD && mem_[0xFF47] == 0xAB
			&& mem_[0xFF48] == 0xFE && mem_[0xFF4F] == 0xC3
			&& mem_[0xFF50] == 0x70 && mem_[0xFF51] == 0x81) {
			/* CALL $FEAB = JP $81F2 never returns to JP $8170. Leave the
			   $0744 countdown alone — shrinking it JP $8170 from the ISR
			   muted flag 0. Intro wrap has to carry the loop. */
			memset(mem_ + 0xFF46, 0x00, 3);
		}
		/* luxsor OPMDRV/PSGDRV play LDIRs work RAM then `XOR A; LD ($EA0F),A`
		   clearing the ISR gate ArmTelenetPlayGate just primed. ISR
		   `LD A,($EA0F); OR A; RET Z` then never ticks. */
		if (initPc_ == 0xE900 && opmPlayGate_ == 0xEA0F) {
			if (mem_[0xECCD] == 0x32 && mem_[0xECCE] == 0x0F
				&& mem_[0xECCF] == 0xEA)
				memset(mem_ + 0xECCD, 0x00, 3);
			if (mem_[0xEAE9] == 0xAF && mem_[0xEAEA] == 0x32
				&& mem_[0xEAEB] == 0x0F && mem_[0xEAEC] == 0xEA)
				memset(mem_ + 0xEAEA, 0x00, 3);
			/* `LD A,($EA1D/$EA18); LD (IX+23),A` — 0 = infinite loop at
			   table+2. Port F used to feed the song id so BGM wrapped N
			   times then STOPS. Force A=0 even if IN (F) still sees the id. */
			if (mem_[0xED29] == 0x3A && mem_[0xED2A] == 0x1D
				&& mem_[0xED2B] == 0xEA && mem_[0xED2C] == 0xDD
				&& mem_[0xED2D] == 0x77 && mem_[0xED2E] == 0x17) {
				mem_[0xED29] = 0xAF;
				mem_[0xED2A] = 0x00;
				mem_[0xED2B] = 0x00;
			}
			if (mem_[0xEB41] == 0x3A && mem_[0xEB42] == 0x18
				&& mem_[0xEB43] == 0xEA && mem_[0xEB44] == 0xDD
				&& mem_[0xEB45] == 0x77 && mem_[0xEB46] == 0x17) {
				mem_[0xEB41] = 0xAF;
				mem_[0xEB42] = 0x00;
				mem_[0xEB43] = 0x00;
			}
			/* ISR `INC ($EA1E)` is a 128-tick end flag for the game, but
			   $EC53 `LD A,($EA1E); ADD A,D` uses the same byte as a TL
			   addend. After ~128 music ticks D clamps to $7F (mute) and
			   stays there — STOPS/GAPPY with keys still moving. Hoot
			   never increments it (ioport BSS stays 0). */
			if (mem_[0xEEE5] == 0x21 && mem_[0xEEE6] == 0x1E
				&& mem_[0xEEE7] == 0xEA && mem_[0xEEE8] == 0x34)
				mem_[0xEEE8] = 0x00;
			/* `DEC (IX+7)` is the TL envelope index. $E1 would reload it
			   but ISR skips $E1 while $EA0F is NZ (ArmTelenet). After 15
			   music ticks the index hits the table's 127 slot and BGM
			   goes inaudible with keys still moving (STOPS/GAPPY MON_OK). */
			if (mem_[0xEF0A] == 0xDD && mem_[0xEF0B] == 0x35
				&& mem_[0xEF0C] == 0x07)
				memset(mem_ + 0xEF0A, 0x00, 3);
			/* PSGDRV `$EA72` computes AY volume from ix+7 then
			   `LD A,($EA0F); OR A; RET NZ` — ArmTelenet keeps the gate
			   at 1 so the write never happens (ayW>0, peak=0). OPMDRV
			   does not have this sequence. */
			if (mem_[0xEA79] == 0x3A && mem_[0xEA7A] == 0x0F
				&& mem_[0xEA7B] == 0xEA && mem_[0xEA7C] == 0xB7
				&& mem_[0xEA7D] == 0xC0 && mem_[0xEA7E] == 0xDD
				&& mem_[0xEA7F] == 0x72 && mem_[0xEA80] == 0x13)
				mem_[0xEA7D] = 0x00;
			/* Same driver's `$ED07` envelope `DEC D; JP P; LD D,0`
			   decays ix+19 to mute in ~2.5s. Hold the key-on level. */
			if (mem_[0xED1C] == 0x15 && mem_[0xED1D] == 0xF2
				&& mem_[0xED1E] == 0x22 && mem_[0xED1F] == 0xED
				&& mem_[0xED20] == 0x16 && mem_[0xED21] == 0x00
				&& mem_[0xED22] == 0xDD && mem_[0xED23] == 0x72
				&& mem_[0xED24] == 0x13)
				memset(mem_ + 0xED1C, 0x00, 6);
		}
		/* TTLPRG is 13K; mode 0 pokes $3762 in the 20K in-game player.
		   Keep MANPR1's tail under the title program. */
		if (ys2Mirror4000_ && vdataAddr >= 0) {
			unsigned msz = 0;
			const unsigned char* man = CEmuZipFsFind(fs, "MANPR1", &msz);
			if (man && msz) {
				unsigned n = msz;
				if (vdataSize > 0 && (unsigned)vdataSize < n)
					n = (unsigned)vdataSize;
				if (vdataAddr + (int)n > 0x10000)
					n = (unsigned)(0x10000 - vdataAddr);
				memcpy(mem_ + vdataAddr, man, n);
				/* Mode 0 (cmd < $20) needs the full in-game player.
				   TTLPRG overlay is only for title-screen mode 1.
				   TTLMSn (lo>=$30) is played as in-game, so keep MANPR1.
				   Ending (lo==$20) patches CALL $16C6 / JP $1A8F and
				   LDIR C000→$2800 — those land in ENDPRG, not TTLPRG. */
				const unsigned hi = (titleCode >> 24) & 0xffu;
				const unsigned lo = titleCode & 0xffu;
				const unsigned cmd = hi ? hi : lo;
				if (lo == 0x20u) {
					unsigned esz = 0;
					const unsigned char* endp = CEmuZipFsFind(fs, "ENDPRG", &esz);
					if (endp && esz) {
						unsigned en = esz;
						if (vdataSize > 0 && (unsigned)vdataSize < en)
							en = (unsigned)vdataSize;
						if (vdataAddr + (int)en > 0x10000)
							en = (unsigned)(0x10000 - vdataAddr);
						memcpy(mem_ + vdataAddr, endp, en);
					}
					/* First command CALLs $00AE/$00C2 = JP $2F91 before
					   the ending table rewrites that stub to $1A8F.
					   $2F91 sits inside the $2800 BGM window, so a
					   planted RET is wiped by StageBgm. NOP the CALL
					   in PATCH (no previous song to stop). */
					if (mem_[0x0026] == 0xCD && mem_[0x0027] == 0xAE
						&& mem_[0x0028] == 0x00)
						memset(mem_ + 0x0026, 0x00, 3);
					if (mem_[0x002B] == 0xCD && mem_[0x002C] == 0xC2
						&& mem_[0x002D] == 0x00)
						memset(mem_ + 0x002B, 0x00, 3);
				} else if (cmd >= 0x20u && lo < 0x30u) {
					for (int i = 0; i < ge->romCount; i++) {
						const CEmuRomEntry* r = &ge->rom[i];
						if (_stricmp(r->type, "code") != 0) continue;
						if (_stricmp(r->name, "TTLPRG") != 0) continue;
						unsigned tsz = 0;
						const unsigned char* ttl = CEmuZipFsFind(fs, r->name, &tsz);
						if (!ttl || !tsz) break;
						int off = r->offset;
						if (off < 0) off = 0;
						unsigned tn = tsz;
						if (off + (int)tn > 0x10000)
							tn = (unsigned)(0x10000 - off);
						memcpy(mem_ + off, ttl, tn);
						break;
					}
				}
			}
		}
		/* ys2 PATCH2 plants `LD A,$C9; LD ($2F0E),A` at boot and again on
		   the title path. $2F0E is `POP AF; RET`; replacing it with RET
		   leaks the pushed AF and the SSG volume path never runs
		   (ayW>0, mixer=$38, vols=0). Keep the in-game `$39D4=C9`
		   (OPM port 0700). OPM PATCH does not have this store. */
		if (psgOnly_ && ys2Mirror4000_) {
			if (mem_[0x0010] == 0x3E && mem_[0x0011] == 0xC9
				&& mem_[0x0012] == 0x32 && mem_[0x0013] == 0x0E
				&& mem_[0x0014] == 0x2F)
				memset(mem_ + 0x0012, 0x00, 3);
			if (mem_[0x0050] == 0x3E && mem_[0x0051] == 0xC9
				&& mem_[0x0052] == 0x32 && mem_[0x0053] == 0x0E
				&& mem_[0x0054] == 0x2F)
				memset(mem_ + 0x0052, 0x00, 3);
		}
		/* Unpatched mars ISR: IN (1A01); JP P,$4208; IN; JP M,$420D. */
		if (mem_[0x420A] == 0xF2 && mem_[0x420B] == 0x08 && mem_[0x420C] == 0x42
			&& mem_[0x420F] == 0xFA && mem_[0x4210] == 0x0D && mem_[0x4211] == 0x42)
			marsHoldIrq_ = 1;
	}

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpuCycles_ = 0;
	if (chipOpm_) chipOpm_->Reset();
	if (chipOpn_) chipOpn_->Reset();
	if (chipAy_) chipAy_->Reset();
	/* Do NOT poke C010-C012 here — reserved for Play()/TriggerPlay. */
	return 1;
}

void CEmuHardX1SetActive(CHardX1* hw)
{
	CEmuZ80BusSetActive(hw);
}
