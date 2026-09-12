#include "StdAfx.h"
#include "cemu_hard_fm7.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_ay.h"
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

enum {
	FM7_CPU_HZ = 2000000,
	/* Match PC-88 OPN master; FM77AV board clocks vary — audible with ymfm @ /4. */
	FM7_OPN_HZ = 1228800, /* FM-7 / FM77AV YM2203 (not 3.9936MHz — that raced ys2) */
	/* CEmuChipAy applies hoot's standalone-PSG /2 divider internally.
	   Feed the FM-7 2.4576 MHz source so the rendered PSG clock is
	   1.2288 MHz; passing 1.2288 MHz here made Jikochu one octave slow. */
	FM7_AY_HZ = 2457600,
	FM7_FD_PSG_ADDR = 0xFD0D,
	FM7_FD_PSG_DATA = 0xFD0E,
	FM7_FD_OPN_ADDR = 0xFD15,
	FM7_FD_OPN_DATA = 0xFD16,
	FM7_FD_IRQEN = 0xFD02,
	FM7_FD_IRQST = 0xFD03,
	FM7_FD_SUBINTF = 0xFD05, /* bit7: sub-CPU busy (R) / halt req (W) */
	FM7_FD_PLAY_CMD = 0xFD58,
	FM7_FD_PLAY_SONG = 0xFD59,
	FM7_FD_PLAY_A = 0xFD5A,
	FM7_FD_PLAY_B = 0xFD5B,
	FM7_FD_PLAY_C = 0xFD5C,
	FM7_FD_FALCOM_CMD = 0xFD80,
	FM7_FD_FALCOM_SONG = 0xFD82
};

static CHardFm7* s_activeFm7 = NULL;

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

static int IsFm7Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "fm7") == 0 || _stricmp(ge->platform, "fm77av") == 0
		|| _stricmp(ge->platform, "mucomfm") == 0)
		return 1;
	if (_stricmp(ge->dataDir, "fm7") == 0) return 1;
	return 0;
}

static int PreferOpn(const CEmuGameEntry* ge)
{
	if (!ge) return 1;
	if (_stricmp(ge->platform, "fm77av") == 0) return 1;
	if (_stricmp(ge->platform, "fm7") == 0) return 0;
	if (_stricmp(ge->subtype, "opn") == 0 || _stricmp(ge->subtype, "ysav") == 0
		|| _stricmp(ge->subtype, "xanadu") == 0)
		return 1;
	if (_stricmp(ge->subtype, "psg") == 0 || _stricmp(ge->subtype, "ys") == 0
		|| _stricmp(ge->subtype, "xanadu2") == 0)
		return 0;
	/* Archive stem: *_fmav → OPN, *_fm7 → PSG when platform unset. */
	if (ge->archive[0]) {
		const char* a = ge->archive;
		const size_t n = strlen(a);
		if (n >= 5 && _stricmp(a + n - 5, "_fmav") == 0) return 1;
		if (n >= 4 && _stricmp(a + n - 4, "_fm7") == 0) return 0;
	}
	/* mucomfm / dataDir=fm7 without platform: prefer OPN (AV titles dominate). */
	return 1;
}

static int IsFalcomSubtype(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "mucomfm") == 0) return 1;
	const char* s = ge->subtype;
	/* asteka2 is Falcom-published but uses classic $FD58 mailbox + APRG banks,
	   not Ys/Xanadu prog/$FD80 — keep it on the non-Falcom TriggerPlay path. */
	if (_stricmp(s, "xanadu") == 0 || _stricmp(s, "xanadu2") == 0
		|| _stricmp(s, "ys") == 0 || _stricmp(s, "ysav") == 0)
		return 1;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "prog") == 0)
			return 1;
	}
	return 0;
}

static mc6809byte__t Fm7CpuRead(mc6809__t* cpu, mc6809addr__t addr, bool /*iscode*/)
{
	CHardFm7* hw = (CHardFm7*)(cpu ? cpu->user : NULL);
	if (!hw) return 0xff;
	return hw->MemRead((uint16_t)addr);
}

static void Fm7CpuWrite(mc6809__t* cpu, mc6809addr__t addr, mc6809byte__t data)
{
	CHardFm7* hw = (CHardFm7*)(cpu ? cpu->user : NULL);
	if (!hw) return;
	hw->MemWrite((uint16_t)addr, (uint8_t)data);
}

static void Fm7CpuFault(mc6809__t* cpu, mc6809fault__t fault)
{
	if (cpu)
		longjmp(cpu->err, (int)fault);
}

CHardFm7::CHardFm7()
	: cpuHz_(FM7_CPU_HZ)
	, opnHz_(FM7_OPN_HZ)
	, ayHz_(FM7_AY_HZ)
	, useOpn_(1)
	, initPc_(0)
	, patchTableBase_(0)
	, mdataAddr_(0x3000)
	, mdataSize_(0x1000)
	, titleCode_(0)
	, playCmdLatch_(0)
	, playSongLatch_(0)
	, playParamA_(0)
	, playParamB_(0)
	, playParamC_(0)
	, playCmdHold_(0)
	, falcomCmdLatch_(0)
	, falcomSongLatch_(0)
	, falcomCmdHold_(0)
	, fd02_(0)
	, fd03_(0)
	, fd05_(0)
	, fd05HaltSticky_(0)
	, ymIrqSeen_(0)
	, fd03VsyncSet_(0)
	, fd03VsyncClr_(0x08)
	, fd03VsyncPhase_(0)
	, opnDataLatch_(0)
	, psgDataLatch_(0)
	, opnCmd_(0)
	, psgCmd_(0)
	, falcomMode_(0)
	, vdataAddr_(-1)
	, vdataSize_(0)
	, codeHighWater_(0)
	, chipOpn_(NULL)
	, chipAy_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, mmrSeg_(0)
	, mmrWin_(0)
	, mmrMode_(0)
	, mmrAvail_(0)
	, mmrOn_(0)
	, mmrTouched_(0)
	, albatrssMode_(0)
	, albatrssPoll_(0)
	, xana2Tick_(0)
	, xana2Tempo_(0)
{
	hardKind = KIND_FM7;
	memset(mem_, 0, sizeof(mem_));
	memset(mmrRam_, 0, sizeof(mmrRam_));
	memset(mmrBank_, 0, sizeof(mmrBank_));
	memset(&cpu_, 0, sizeof(cpu_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
	memset(progBank_, 0, sizeof(progBank_));
	memset(progBankSize_, 0, sizeof(progBankSize_));
	memset(progPresent_, 0, sizeof(progPresent_));
	memset(voiceBank_, 0, sizeof(voiceBank_));
	memset(voiceBankSize_, 0, sizeof(voiceBankSize_));
	memset(voicePresent_, 0, sizeof(voicePresent_));
}

CHardFm7::~CHardFm7()
{
	Shutdown();
}

void CHardFm7::BindCpuCallbacks()
{
	cpu_.user = this;
	cpu_.read = Fm7CpuRead;
	cpu_.write = Fm7CpuWrite;
	cpu_.fault = Fm7CpuFault;
}

int CHardFm7::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsFm7Platform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = FM7_CPU_HZ;
	opnHz_ = FM7_OPN_HZ;
	ayHz_ = FM7_AY_HZ;
	useOpn_ = PreferOpn(ge);
	initPc_ = 0;
	patchTableBase_ = 0;
	mdataAddr_ = 0x3000;
	mdataSize_ = 0x1000;
	falcomMode_ = 0;
	vdataAddr_ = -1;
	vdataSize_ = 0;
	codeHighWater_ = 0;
	xana2Tick_ = 0;
	xana2Tempo_ = 0;
	chipOpn_ = NULL;
	chipAy_ = NULL;
	if (useOpn_) {
		/* YM2203 via ymfm/fmgen wrapper (opnaMode=0). */
		chipOpn_ = CEmuChipYm2608Create((uint32_t)opnHz_, 0, sampleRate_);
		if (!chipOpn_) return 0;
	} else {
		chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
		if (!chipAy_) return 0;
	}
	BindCpuCallbacks();
	return 1;
}

void CHardFm7::FreeBanks()
{
	for (int i = 0; i < 128; i++) {
		if (bgmBank_[i]) {
			free(bgmBank_[i]);
			bgmBank_[i] = NULL;
		}
		bgmBankSize_[i] = 0;
		bgmPresent_[i] = 0;
		if (voiceBank_[i]) {
			free(voiceBank_[i]);
			voiceBank_[i] = NULL;
		}
		voiceBankSize_[i] = 0;
		voicePresent_[i] = 0;
	}
	for (int i = 0; i < PROG_BANKS; i++) {
		if (progBank_[i]) {
			free(progBank_[i]);
			progBank_[i] = NULL;
		}
		progBankSize_[i] = 0;
		progPresent_[i] = 0;
	}
}

int CHardFm7::HasProgBanks() const
{
	for (int i = 0; i < PROG_BANKS; i++)
		if (progPresent_[i]) return 1;
	return 0;
}

void CHardFm7::Shutdown()
{
	if (s_activeFm7 == this)
		s_activeFm7 = NULL;
	FreeBanks();
	if (chipOpn_) {
		CEmuChipYm2608Destroy(chipOpn_);
		chipOpn_ = NULL;
	}
	if (chipAy_) {
		CEmuChipAyDestroy(chipAy_);
		chipAy_ = NULL;
	}
}

void CHardFm7::StageBgm(uint8_t index)
{
	/* Exact bank only — no bank0 guess. */
	uint8_t use = index;
	if (use >= 128 || !bgmPresent_[use] || !bgmBank_[use])
		return;
	unsigned n = bgmBankSize_[use];
	/* Prefer full bank when catalog mdata_size is only a header window.
	   laydock XML window is $4000+$200 but MUS is 8K — the DRIVER binary
	   merely *ends* at $42B8 because that tail is the song workspace
	   (PATCH's $5E74 tables live in the same window). Expanding into a
	   short overlap is required; relics-sized $E000 overlays stay capped. */
	unsigned cap = mdataSize_;
	if (n > cap && mdataAddr_ + n <= 0x10000u
		&& (initPc_ < mdataAddr_ || initPc_ >= mdataAddr_ + n)) {
		const unsigned winEnd = mdataAddr_ + cap;
		const int shortTail = (codeHighWater_ > mdataAddr_
			&& codeHighWater_ <= mdataAddr_ + 0x1000u);
		if (!(codeHighWater_ > winEnd && codeHighWater_ > mdataAddr_) || shortTail) {
			/* laydock: DRIVER BSS at $5E74 is the live channel table.
			   Expanding the 8K MUS over it plants data bytes as pointers
			   and the ISR's LDU #$5E74 / LDY ,U never sees real voices
			   (AR/MUL stay 0, monitor keys, peak 0). Stop before $5E74. */
			if (shortTail && mdataAddr_ < 0x5E74u)
				cap = 0x5E74u - mdataAddr_;
			else
				cap = n;
			if (n < cap) cap = n;
		}
	}
	/* Ys PATCH t0 is a header page (MANPR $08=2K, END/OMAKE $10=4K) but
	   the banks are 3K/8K. Phrase-2 pointers sit past that window
	   (MUSD10B $64A0, ENDMUS $1F60/$3D4A). codeHighWater_ is PATCH at
	   $FFxx so the generic expand above stays capped. */
	if (falcomMode_ && mdataAddr_ >= 0x3000u && mdataAddr_ < 0xE000u
		&& n > cap && mdataAddr_ + n <= 0xFE00u
		&& (initPc_ >= 0xFE00 || initPc_ < mdataAddr_))
		cap = n;
	if (n > cap) n = cap;
	if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
	if (mdataAddr_ + n > 0x10000) return;
	if (n > mdataSize_)
		mdataSize_ = n;
	unsigned clearN = cap;
	if (mdataAddr_ + clearN > 0x10000)
		clearN = 0x10000u - mdataAddr_;
	/* daiva OP.BIN @0: zeroing the full 16K window wipes INITIATE.ROM
	   under the song. Only clear the bytes we overwrite. */
	if (mdataAddr_ == 0 && clearN > n)
		clearN = n;
	if (clearN)
		memset(mem_ + mdataAddr_, 0, clearN);
	memcpy(mem_ + mdataAddr_, bgmBank_[use], n);
	/* daiva FLEET/BATL are already relocated to $E000 (JMP $E4xx). Stage
	   at $0000 for the PATCH copy loop, and mirror so JSR $E000 works even
	   if that copy never runs. OP.BIN starts ORCC — do not mirror. */
	if (mdataAddr_ == 0 && n >= 3 && mem_[0] == 0x7E && mem_[1] >= 0xE0) {
		unsigned m = n;
		if (m > 0x2000u) m = 0x2000u;
		memcpy(mem_ + 0xE000, mem_, m);
	}
	/* daiva BATL starts with a word table, not JMP $E4xx, so the 7E
	   mirror above misses it. PATCH song 2 copies $0000→$E000 / JSR $E0BE. */
	else if (IsDaivaPatch() && mdataAddr_ == 0 && n >= 3 && mem_[0] != 0x1A) {
		unsigned m = n;
		if (m > 0x2000u) m = 0x2000u;
		memcpy(mem_ + 0xE000, mem_, m);
	}
	/* Ys MANPR also peeks 0x4D00 — mirror when primary window is 0x5C00.
	   Do NOT stage at 0x3000 here: YMUS 30xx headers are post-reloc addrs;
	   overlaying $3000 would clobber MANPR (loader JSR $317A etc.). */
	if (falcomMode_ && mdataAddr_ == 0x5c00 && n > 0) {
		unsigned mn = n;
		if (mn > 0x800u) mn = 0x800u;
		if (0x4d00 + mn <= 0x5c00)
			memcpy(mem_ + 0x4d00, bgmBank_[use], mn);
	}
}

void CHardFm7::StageProg(uint8_t index)
{
	/* Exact prog bank only — no low-nibble / first-present guess. */
	uint8_t use = index;
	if (use >= PROG_BANKS || !progPresent_[use] || !progBank_[use])
		return;
	unsigned n = progBankSize_[use];
	/* Falcom PATCH table t0 is the load page (TTLPRG $10, MANPR $08).
	   Applies to both PSG (FED0) and OPN (FF00 TITLEP). Xanadu tables are
	   address lists, not 08/10 rows — those stay at $0000. */
	unsigned loadBase = 0;
	if (falcomMode_ && patchTableBase_ >= 0xE000 && use >= 1) {
		const unsigned ent = (unsigned)patchTableBase_ + (unsigned)(use - 1) * 8u;
		if (ent + 1u < 0x10000u) {
			const uint8_t page = mem_[ent];
			const uint8_t win = mem_[ent + 1];
			if ((page == 0x08 || page == 0x10)
				&& (win == 0x00 || win == 0x30 || win == 0x4F || win == 0x5C))
				loadBase = (unsigned)page << 8;
		}
		if (!loadBase && patchTableBase_ == 0xFED0)
			loadBase = 0x0800u;
	}
	/* Keep high PATCH (FEE0/FF00) intact. */
	unsigned cap = 0xFE00u;
	if (initPc_ >= 0xE000 && (unsigned)initPc_ < cap)
		cap = (unsigned)initPc_;
	if (mdataAddr_ > 0 && mdataAddr_ < cap && bgmPresent_[0])
		cap = mdataAddr_;
	if (loadBase >= cap) return;
	if (n > cap - loadBase) n = cap - loadBase;
	if (n > 0)
		memcpy(mem_ + loadBase, progBank_[use], n);
}

void CHardFm7::StageVoice(uint8_t index)
{
	if (vdataAddr_ < 0) return;
	uint8_t use = index;
	if (use >= 128 || !voicePresent_[use] || !voiceBank_[use])
		return;
	unsigned n = voiceBankSize_[use];
	if (vdataSize_ > 0 && (unsigned)vdataSize_ < n)
		n = (unsigned)vdataSize_;
	if (vdataAddr_ + (int)n > 0x10000)
		n = (unsigned)(0x10000 - vdataAddr_);
	if (n > 0)
		memcpy(mem_ + vdataAddr_, voiceBank_[use], n);
}

void CHardFm7::MmrInit()
{
	mmrAvail_ = (useOpn_ && !falcomMode_) ? 1 : 0;
	mmrOn_ = 0;
	mmrSeg_ = 0;
	mmrWin_ = 0;
	mmrMode_ = 0;
	mmrTouched_ = 0;
	memset(mmrRam_, 0, sizeof(mmrRam_));
	for (int s = 0; s < 8; s++) {
		for (int i = 0; i < 16; i++)
			mmrBank_[s][i] = (uint8_t)(0x30 + i);
	}
	if (!mmrAvail_)
		return;
	/* Snapshot the ripped 64K into the AV's default $30000 view and an
	   identity copy at $00000 so enabling MMR with page 0-F is a no-op. */
	memcpy(mmrRam_ + 0x30000, mem_, 0x10000);
	memcpy(mmrRam_, mem_, 0x10000);
}

uint8_t* CHardFm7::MmrPhys(uint16_t addr)
{
	if (addr >= 0xFC00 || !mmrAvail_)
		return &mem_[addr];
	uint8_t page;
	if (!mmrOn_)
		page = (uint8_t)(0x30 + (addr >> 12));
	else if ((mmrMode_ & 0x40) && addr >= 0x7C00 && addr < 0x8000) {
		const unsigned win = ((unsigned)mmrWin_ << 8) + 0x7C00u;
		const unsigned phys = (win + (unsigned)(addr - 0x7C00u)) & 0xffffu;
		return &mmrRam_[phys];
	} else {
		page = (uint8_t)(mmrBank_[mmrSeg_ & 7][addr >> 12] & (MMR_PAGES - 1));
	}
	return &mmrRam_[((unsigned)page << 12) | (addr & 0x0fffu)];
}

void CHardFm7::MmrCommit(uint16_t addr, unsigned n)
{
	if (!mmrAvail_ || n == 0)
		return;
	for (unsigned i = 0; i < n; i++) {
		const uint16_t a = (uint16_t)(addr + i);
		if (a < addr && i)
			break;
		if (a >= 0xFC00)
			break;
		*MmrPhys(a) = mem_[a];
		/* Keep identity pages in sync while MMR is off so a later enable
		   with bank=i still sees the staged image. */
		if (!mmrOn_)
			mmrRam_[a] = mem_[a];
	}
}

uint8_t CHardFm7::MmrReadReg(uint16_t addr)
{
	const unsigned off = (unsigned)addr - 0xFD80u;
	if (!mmrTouched_)
		return 0xFF;
	if (off < 0x10)
		return mmrBank_[mmrSeg_ & 7][off];
	if (off == 0x13)
		return mmrMode_;
	return 0xFF;
}

void CHardFm7::MmrWriteReg(uint16_t addr, uint8_t data)
{
	const unsigned off = (unsigned)addr - 0xFD80u;
	mmrTouched_ = 1;
	if (off < 0x10) {
		mmrBank_[mmrSeg_ & 7][off] = data;
		return;
	}
	if (off == 0x10) {
		mmrSeg_ = (uint8_t)(data & 7);
		return;
	}
	if (off == 0x12) {
		mmrWin_ = data;
		return;
	}
	if (off == 0x13) {
		mmrMode_ = data;
		mmrOn_ = (data & 0x80) ? 1 : 0;
	}
}

void CHardFm7::SeedTandeFmVoices()
{
	/* T&E YM2203 (laydock DRIVER $30EE, daiva OP.BIN $2FA8): 34-byte
	   voices at $6C00. INITIATE.ROM stores the same bank at $0C00
	   (I-ROM111851002). Copy now — daiva's 16K OP.BIN at $0000 wipes $0C00. */
	if (!useOpn_ || falcomMode_)
		return;
	if (memcmp(mem_ + 0x0B00, "I-ROM", 5) != 0)
		return;
	if (mem_[0x0C00] > 0x1F && mem_[0x0C04] > 0x7F)
		return;
	memcpy(mem_ + 0x6C00, mem_ + 0x0C00, 0x0C00);
}

void CHardFm7::ArmLaydockChannels()
{
	/* 2E55 BRA $2EB9 copies song ptr +8 → +0 and sets delay=1. If play
	   returned after STA $5E6F but skipped that tail, 2F3B LDY ,U is 0. */
	if (mem_[0x2EB9] != 0x8E || mem_[0x2EBA] != 0x5E || mem_[0x2EBB] != 0x74)
		return;
	if (mem_[0x5E6F] == 0)
		return;
	for (int ch = 0; ch < 3; ch++) {
		uint8_t* c = mem_ + 0x5E74 + ch * 16;
		const uint16_t song = (uint16_t)(((uint16_t)c[8] << 8) | c[9]);
		const uint16_t cur = (uint16_t)(((uint16_t)c[0] << 8) | c[1]);
		if (song < 0x4000 || song >= 0x5E74)
			continue;
		if (cur != 0)
			continue;
		c[0] = c[8];
		c[1] = c[9];
		c[10] = c[8];
		c[11] = c[9];
		c[2] = 0x30;
		c[3] = 0x00;
		c[4] = 0x01;
		c[5] = 0x18;
		c[6] = 0x10;
		if (!c[7])
			c[7] = 1;
	}
}

void CHardFm7::FinishDaivaOpPlay()
{
	/* PATCH song 0: JSR $1BBD (IRQ $2B3A) / $2B68 / $2BCD. $1BBD also
	   pokes MMR $FD90; without a live mapper the JSRs still plant the
	   $2C6A channel table onto MUS00 at $6000. Only OP.BIN starts
	   ORCC #$50 at $0000 — FLEET/BATL/ED overwrite $1BBD or leave the
	   $2B3A signature in leftover OP and this would JSR into the wrong
	   bank. */
	if (mem_[0] != 0x1A || mem_[1] != 0x50)
		return;
	if (mem_[0x2B3A] != 0xB6 || mem_[0x2B3B] != 0xFD || mem_[0x2B3C] != 0x03)
		return;
	if (mem_[0x2BCD] != 0x8E || mem_[0x2BCE] != 0x2C || mem_[0x2BCF] != 0x6A)
		return;
	cpu_.dp = 0xFD;
	RunSubroutine(0x1BBD);
	RunSubroutine(0x2BCD);
}

void CHardFm7::FinishDaivaEdPlay()
{
	/* PATCH song 3: plant FIRQ $0CC9 / JSR $0CD9 / $0D36 against ED.BIN
	   at $0000. Native poll can miss that and leave PC in $FCxx. */
	if (!IsDaivaPatch())
		return;
	if ((titleCode_ & 0xffu) != 3)
		return;
	if (mem_[0] != 0x7E || mem_[1] != 0x00 || mem_[2] != 0x0A)
		return;
	mem_[0x0C4F] = playParamA_;
	mem_[0xFFF8] = 0x00;
	mem_[0xFFF9] = 0x2C;
	mem_[0x50F4] = 0xBD;
	mem_[0x50F5] = 0x0C;
	mem_[0x50F6] = 0xC9;
	cpu_.dp = 0xFD;
	cpu_.cc.i = true;
	RunSubroutine(0x0CD9, 400000, 1);
	RunSubroutine(0x0D36, 400000, 1);
	cpu_.pc.w = 0x5021;
	cpu_.cc.i = false;
	cpu_.cwai = false;
}

void CHardFm7::FinishAlbatrssPlay()
{
	/* PATCH play: JSR $F002 (stop) never RTS — a YM busy mute-write storm
	   (88k writes, peak 0). F004 itself is LDX PCR / STA $FF / RTS and only
	   arms OP.BIN $87CA. NOP the live play JSRs, host-call $F000 so F069
	   plants channel BSS, then F004. */
	if (!albatrssMode_)
		return;
	unsigned poll = albatrssPoll_, jsrStop = 0, jsrPlay = 0;
	const unsigned lim = (unsigned)initPc_ + 96u;
	for (unsigned a = initPc_; a + 3u < 0x10000u && a < lim; a++) {
		if (!poll && mem_[a] == 0xB6 && mem_[a + 1] == 0xFD && mem_[a + 2] == 0x58)
			poll = a;
		if (mem_[a] == 0xBD && mem_[a + 1] == 0xF0 && mem_[a + 2] == 0x02)
			jsrStop = a;
		if (mem_[a] == 0xBD && mem_[a + 1] == 0xF0 && mem_[a + 2] == 0x04)
			jsrPlay = a;
	}
	if (!poll || !jsrPlay)
		return;
	albatrssPoll_ = (uint16_t)poll;
	cpu_.d.b[1] = playSongLatch_;
	{
		const uint16_t song = mdataAddr_ ? mdataAddr_ : 0x2000;
		cpu_.index[0].w = song;
		mem_[0xF4E2] = (uint8_t)(song >> 8);
		mem_[0xF4E3] = (uint8_t)(song & 0xff);
		cpu_.cc.i = true;
		/* PATCH JSRs are NOP'd so $F002 cannot re-enter this. */
		RunSubroutine(0xF000, 400000, 0);
		if (mem_[0xF880] == 0)
			mem_[0xF880] = mem_[song + 3] ? mem_[song + 3] : 5;
		/* DRIVER ROM defaults $F4D9 to 2 (AY $FD0D). $F4BF would CLR it
		   after a YM readback; without that, $F000/$F002 mute via $F0A8. */
		if (useOpn_)
			mem_[0xF4D9] = 0;
		else
			mem_[0xF4D9] = 1;
		/* F111: $A1 loads a voice, then $00 nn mm is a rest of nn F0DA
		   ticks. Love & tears starts A1 00 B4..C0 on every FM channel —
		   ~15s of silence at F4DA=5, so 4 probe windows stay peak 0 with
		   kon=00 after the voice burst. Only shrink those long lead rests.
		   Rising Up ch1 A1 00 30 (short delayed harmony) is left alone. */
		{
			const uint16_t lim = (uint16_t)(song + 0x2000);
			for (int ch = 0; ch < 3; ch++) {
				const unsigned obj = 0xF4E8u + (unsigned)ch * 24u;
				uint16_t p = ((uint16_t)mem_[obj + 1] << 8) | mem_[obj + 2];
				if (p < song || (unsigned)p + 3u >= (unsigned)lim)
					continue;
				const uint8_t op = mem_[p];
				if (op > 0x2A && op != 0xFE && (op & 0x60) == 0x20)
					p++;
				if ((unsigned)p + 2u < (unsigned)lim
					&& mem_[p] == 0 && mem_[p + 1] >= 0x80) {
					mem_[p + 1] = 1;
					mem_[p + 2] = 0;
					MmrCommit(p, 3);
				}
			}
		}
	}
	RunSubroutine(0xF004);
	/* PATCH planted these before the NOP'd boot JSRs; keep SWI on $F819. */
	if (mem_[0xF819] == 0x33 || mem_[0xF819] == 0x1A || mem_[0xF819] == 0x34) {
		mem_[0xFFFA] = 0xF8;
		mem_[0xFFFB] = 0x19;
	}
	if (mem_[0x87CA] == 0xB6 && mem_[0x87CB] == 0xFD && mem_[0x87CC] == 0x03) {
		mem_[0xFFF8] = 0x87;
		mem_[0xFFF9] = 0xCA;
	}
	if (jsrStop && mem_[jsrStop] == 0xBD) {
		mem_[jsrStop] = 0x12;
		mem_[jsrStop + 1] = 0x12;
		mem_[jsrStop + 2] = 0x12;
		MmrCommit((uint16_t)jsrStop, 3);
	}
	if (mem_[jsrPlay] == 0xBD) {
		mem_[jsrPlay] = 0x12;
		mem_[jsrPlay + 1] = 0x12;
		mem_[jsrPlay + 2] = 0x12;
		MmrCommit((uint16_t)jsrPlay, 3);
	}
	/* Play $003A JSR $F002 is a second copy of the boot stop; a single
	   last-match can miss it. NOP every DRIVER call in PATCH. */
	for (unsigned a = initPc_; a + 3u < 0x10000u && a < (unsigned)initPc_ + 128u; a++) {
		if (mem_[a] == 0xBD && mem_[a + 1] == 0xF0
			&& (mem_[a + 2] == 0x00 || mem_[a + 2] == 0x02 || mem_[a + 2] == 0x04)) {
			mem_[a] = mem_[a + 1] = mem_[a + 2] = 0x12;
			MmrCommit((uint16_t)a, 3);
		}
	}
	unsigned land = jsrPlay + 3;
	if (mem_[land] != 0x1C)
		land = poll;
	cpu_.pc.w = (uint16_t)land;
	cpu_.cc.i = false;
	cpu_.cc.f = true;
	cpu_.cwai = false;
	cpu_.sync = false;
	/* OP.BIN $87CA returns immediately while $8505 is zero. */
	if (mem_[0x8505] == 0) {
		mem_[0x8505] = 1;
		MmrCommit(0x8505, 1);
	}
}

void CHardFm7::ParkAlbatrssIfStuck()
{
	const uint16_t irq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
	if (irq != 0x87CA)
		return;
	const uint16_t pc = cpu_.pc.w;
	/* $F000/$F002 init+stop and $F0A8 AY mute. Leave $F445–$F4BE (write
	   helper) and $F819 (SWI tick) alone so a live tick can finish. */
	int stuck = 0;
	if ((pc >= 0xF000 && pc <= 0xF068) || (pc >= 0xF0A8 && pc < 0xF0D0))
		stuck = 1;
	if (stuck) {
		cpu_.pc.w = albatrssPoll_ ? albatrssPoll_ : 0x002B;
		cpu_.cc.i = false;
		cpu_.cc.f = true;
		cpu_.cwai = false;
		cpu_.sync = false;
	}
}

int CHardFm7::UnwindMissingBios()
{
	/* daiva OP.BIN JSRs F-BASIC / initiator ($Axxx/$Bxxx/$Dxxx) that the
	   rip does not include. Treat a still-zero page as RTS. Only while
	   the OP.BIN ISR at $2B3A is mounted — a global empty-page RTS
	   diverted kohaku's opening into MUS. */
	if (falcomMode_ || mdataAddr_ != 0)
		return 0;
	const uint16_t irq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
	if (irq != 0x2B3A)
		return 0;
	const uint16_t pc = cpu_.pc.w;
	if (pc < 0x8000 || pc >= 0xFC00)
		return 0;
	if (MemRead(pc) | MemRead((uint16_t)(pc + 1))
		| MemRead((uint16_t)(pc + 2)) | MemRead((uint16_t)(pc + 3)))
		return 0;
	const uint16_t sp = cpu_.index[3].w;
	const uint16_t ret = (uint16_t)(((uint16_t)MemRead(sp) << 8)
		| MemRead((uint16_t)(sp + 1)));
	cpu_.index[3].w = (uint16_t)(sp + 2);
	cpu_.pc.w = ret;
	cpu_.cc.i = false;
	cpu_.cwai = false;
	cpu_.sync = false;
	return 1;
}

uint8_t CHardFm7::PortIn(uint16_t /*port*/)
{
	return 0xff;
}

void CHardFm7::PortOut(uint16_t /*port*/, uint8_t /*data*/)
{
}

static void Fm7BusCmd(CChip* chip, uint8_t* dataLatch, uint8_t cmd)
{
	if (!chip || !dataLatch) return;
	switch (cmd & 0x0f) {
	case 0x00: /* high-Z */
		break;
	case 0x01: /* data read */
		*dataLatch = chip->ReadData();
		break;
	case 0x02: /* data write */
		chip->Write(1, *dataLatch);
		break;
	case 0x03: /* address latch */
		chip->Write(0, *dataLatch);
		break;
	case 0x04: /* status read → data latch (MAME fm7_update_psg) */
		/* YM2203 busy (bit7) stays set if the guest polls in the same
		   instruction burst as a write. albatrss DRIVER $F489 BITA #$80
		   / BNE waits on this latch via LDA 1,Y after STA #$04. */
		*dataLatch = (uint8_t)(chip->ReadStatus() & 0x7F);
		break;
	case 0x09: /* joystick — return neutral */
		*dataLatch = 0xff;
		break;
	default:
		break;
	}
}

uint8_t CHardFm7::MemRead(uint16_t addr)
{
	if (addr == FM7_FD_PLAY_CMD) {
		uint8_t v = playCmdLatch_;
		if ((v == 0x01 || v == 0x02) && playCmdHold_ > 0) {
			if (--playCmdHold_ <= 0)
				playCmdLatch_ = 0;
		}
		return v;
	}
	if (addr == FM7_FD_PLAY_SONG)
		return playSongLatch_;
	if (addr == FM7_FD_PLAY_A)
		return playParamA_;
	if (addr == FM7_FD_PLAY_B)
		return playParamB_;
	if (addr == FM7_FD_PLAY_C)
		return playParamC_;
	/* Falcom PATCH polls $FD80; on the real FM77AV that range is MMR.
	   Only overlay the mailbox for Falcom rips — generic titles must see
	   open-bus $FF, not a cleared latch. */
	if (falcomMode_) {
		if (addr == FM7_FD_FALCOM_CMD) {
			uint8_t v = falcomCmdLatch_;
			if (v != 0 && falcomCmdHold_ > 0) {
				if (--falcomCmdHold_ <= 0)
					falcomCmdLatch_ = 0;
			} else if (v != 0) {
				falcomCmdLatch_ = 0;
			}
			return v;
		}
		if (addr == FM7_FD_FALCOM_SONG)
			return falcomSongLatch_;
		if (addr == (FM7_FD_FALCOM_CMD + 1))
			return 0; /* FD81 handshake */
		if (addr == 0xFD85)
			return mem_[0xFD85];
	}
	if (addr == FM7_FD_IRQST) {
		/* Reading $FD03 clears pending bits so laydock bit2 cannot IRQ-storm. */
		uint8_t v = fd03_;
		fd03_ = (uint8_t)(fd03_ & (uint8_t)~0x0F);
		return v;
	}
	if (addr == FM7_FD_SUBINTF)
		return (uint8_t)(fd05_ | (fd05HaltSticky_ ? 0x80 : 0));

	if (addr >= 0xFD00 && addr <= 0xFDFF) {
		/* Keyboard: bit0 of $FD00 is 2MHz; $FD01 is scancode (idle 0). */
		if (addr == 0xFD00)
			return 0x01;
		if (addr == 0xFD01)
			return 0x00;
		/* $FD02 read is cassette/printer status, not the IRQ-mask latch. */
		if (addr == FM7_FD_IRQEN)
			return 0xF0;
		if (addr == 0xFD04)
			return 0xFF;
		if (addr == 0xFD0B)
			return 0xFF; /* AV boot mode: bit0 clear = DOS */
		/* $FD0F read enables F-BASIC ROM on hardware; music rips live in
		   RAM at $8000+ so do not bank-switch. Value is 0 as on MAME. */
		if (addr == 0xFD0F)
			return 0x00;
		/* YM2203 IRQ flag (active-low bit3). Sticky until this read so a
		   main-loop poll still sees the pulse after DeliverIrqs acks. */
		if (addr == 0xFD17) {
			uint8_t v = 0xFF;
			if (useOpn_ && (ymIrqSeen_ || (chipOpn_ && chipOpn_->Irq())))
				v = (uint8_t)(v & ~0x08);
			ymIrqSeen_ = 0;
			return v;
		}
		/* FDC: music images are already staged. Not-ready (bit7) hangs
		   BIOS helpers; report idle/no-error. Ys MANPR also wants DRQ. */
		if (addr == 0xFD18)
			return 0;
		if (addr == 0xFD1F)
			return (!useOpn_ && patchTableBase_ == 0xFED0) ? 0x40 : 0x00;
		if (addr >= 0xFD19 && addr <= 0xFD1E)
			return 0x00;
		/* OPN (YM2203): $FD15/$FD16 plus the AY-compatible $FD0D/$FD0E
		   alias used on the real FM77AV (both drive the same chip). */
		if (useOpn_ && chipOpn_) {
			if (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA)
				return opnDataLatch_;
			if (addr == FM7_FD_OPN_ADDR)
				return chipOpn_->ReadStatus();
			if (addr == FM7_FD_PSG_ADDR)
				return 0xFF;
		}
		if (!useOpn_ && chipAy_ && albatrssMode_) {
			/* albatrss DRIVER hardcodes $FD15/$FD16 (AV OPN ports). On
			   the PSG twin those ports are empty and open-bus $FF keeps
			   BITA #$80 spinning; alias them to the AY bus. */
			if (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA)
				return psgDataLatch_;
			if (addr == FM7_FD_OPN_ADDR || addr == FM7_FD_PSG_ADDR)
				return 0xFF;
		}
		if (!useOpn_ && chipAy_) {
			if (addr == FM7_FD_PSG_DATA)
				return psgDataLatch_;
			if (addr == FM7_FD_PSG_ADDR)
				return 0xFF;
		}
		return 0xFF;
	}
	return mem_[addr];
}

void CHardFm7::MemWrite(uint16_t addr, uint8_t data)
{
	if (addr == FM7_FD_PLAY_CMD) {
		playCmdLatch_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_PLAY_SONG) {
		playSongLatch_ = data;
		mem_[addr] = data;
		/* Laydock writes $FF here when play setup finishes; drop $FD58
		   so the BRA-to-poll loop cannot re-trigger forever. */
		if (data == 0xFF && (playCmdLatch_ == 0x01 || playCmdLatch_ == 0x02))
			playCmdLatch_ = 0;
		return;
	}
	if (addr == FM7_FD_PLAY_A) {
		playParamA_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_PLAY_B) {
		playParamB_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_PLAY_C) {
		playParamC_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_FALCOM_CMD) {
		falcomCmdLatch_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_FALCOM_SONG) {
		falcomSongLatch_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_IRQEN) {
		fd02_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_IRQST) {
		fd03_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_SUBINTF) {
		/* bit7 halt request → assert busy so TST/BPL handshake completes;
		   clear/other → not busy for BMI wait-while-busy loops.
		   Keep halt sticky across intervening CLRs from rogue IRQs so the
		   main-CPU BPL wait after STB #$80 cannot spin forever. */
		if (data & 0x80) {
			fd05_ = 0x80;
			fd05HaltSticky_ = 1;
		} else {
			fd05_ = 0x00;
			fd05HaltSticky_ = 0;
		}
		mem_[addr] = data;
		return;
	}
	if (useOpn_ && chipOpn_) {
		if (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA) {
			opnDataLatch_ = data;
			mem_[addr] = data;
			return;
		}
		if (addr == FM7_FD_OPN_ADDR) {
			opnCmd_ = data;
			Fm7BusCmd(chipOpn_, &opnDataLatch_, data);
			mem_[addr] = data;
			return;
		}
		/* FM77AV $FD0D is the AY-compatible BDIR/BC1 nibble of the YM2203. */
		if (addr == FM7_FD_PSG_ADDR) {
			opnCmd_ = (uint8_t)(data & 0x03);
			Fm7BusCmd(chipOpn_, &opnDataLatch_, opnCmd_);
			mem_[addr] = data;
			return;
		}
	}
	if (!useOpn_ && chipAy_) {
		if (albatrssMode_ && (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA)) {
			psgDataLatch_ = data;
			mem_[addr] = data;
			return;
		}
		if (addr == FM7_FD_PSG_DATA) {
			psgDataLatch_ = data;
			mem_[addr] = data;
			return;
		}
		if (albatrssMode_ && (addr == FM7_FD_OPN_ADDR || addr == FM7_FD_PSG_ADDR)) {
			psgCmd_ = data;
			Fm7BusCmd(chipAy_, &psgDataLatch_, data);
			mem_[addr] = data;
			return;
		}
		if (addr == FM7_FD_PSG_ADDR) {
			psgCmd_ = data;
			Fm7BusCmd(chipAy_, &psgDataLatch_, data);
			mem_[addr] = data;
			return;
		}
	}
	mem_[addr] = data;
}

void CHardFm7::UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut)
{
	const unsigned b0 = titleCode & 0xffu;
	const unsigned b1 = (titleCode >> 8) & 0xffu;
	const unsigned b2 = (titleCode >> 16) & 0xffu;
	const unsigned b3 = (titleCode >> 24) & 0xffu;
	uint8_t song = 0, bank = 0;
	/* Falcom packed: bits12.. = prog family, lo = track (0x2010 → prog2/song10). */
	if (b3 == 0 && b2 == 0 && b1 >= 0x10 && (b1 & 0x0f) == 0) {
		song = (uint8_t)b0;
		bank = (uint8_t)(b1 >> 4);
		if (!bank) bank = (uint8_t)(b1 ? b1 : 1);
	} else if (b3 != 0 && b1 == 0 && b2 == 0 && b0 != 0) {
		/* 0xNN000000 style */
		song = (uint8_t)b3;
		bank = (uint8_t)b0;
	} else if (titleCode != 0) {
		/* Generic FM-7: FD59=b0, FD5A=b1 — prefer nonzero song index. */
		if (b0)
			song = (uint8_t)b0;
		else if (b1)
			song = (uint8_t)b1;
		else if (b3)
			song = (uint8_t)b3;
		else
			song = (uint8_t)(titleCode & 0xff);
		bank = song;
		if (b1 && b1 != song && b1 < 128)
			bank = (uint8_t)b1;
	}
	(void)b2;
	if (songOut) *songOut = song;
	if (bankOut) *bankOut = bank;
}

void CHardFm7::RefreshFd03Polarity()
{
	/* Default: psyoblde/daiva/luxsor — BITA #$08 / BNE skip → clear bit3. */
	fd03VsyncSet_ = 0;
	fd03VsyncClr_ = 0x08;

	/* kohaku: $2C1D ORCC / JSR $D000 / $2C2C LDA $FD03. The first LDA $FD03
	   in the image is $2C2C, so the work-table scan below would skip SOUND. */
	const int kohakuIrq = (mem_[0x2C12] == 0x1A && mem_[0x2C13] == 0x10
		&& mem_[0x2C14] == 0x8E && mem_[0x2C15] == 0x2C && mem_[0x2C16] == 0x1D
		&& mem_[0x2C17] == 0xBF && mem_[0x2C18] == 0xFF && mem_[0x2C19] == 0xF8
		&& mem_[0x2C29] == 0xBD && mem_[0x2C2A] == 0xD0 && mem_[0x2C2B] == 0x00
		&& mem_[0xD000] == 0x86 && mem_[0xD001] == 0x01);
	if (kohakuIrq) {
		mem_[0xFFF8] = 0x2C;
		mem_[0xFFF9] = 0x1D;
	}

	/* XA2PSGPATCH already aims IRQ/FIRQ at $FF94. A later scan through
	   PR.NO2 finds a false $FD03 BITA and rips the vector off the tick. */
	const int xana2PsgIrq = IsXana2PsgPlayer()
		&& mem_[0xFF94] == 0x34 && mem_[0xFF96] == 0x10
		&& mem_[0xFF97] == 0x8E;
	if (xana2PsgIrq) {
		mem_[0xFFF8] = 0xFF;
		mem_[0xFFF9] = 0x94;
		mem_[0xFFF6] = 0xFF;
		mem_[0xFFF7] = 0x94;
	}

	/* Some images plant a work-table address; locate their FD03 ISR. */
	{
		auto looksFd03 = [&](uint16_t a) -> int {
			if (a < 0x0100 || a >= 0xFE00) return 0;
			const uint8_t* p = mem_ + a;
			if (p[0] == 0xB6 && p[1] == 0xFD && p[2] == 0x03) return 1;
			if (p[0] == 0x96 && p[1] == 0x03) return 1;
			return 0;
		};
		uint16_t hwIrq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
		if (!kohakuIrq && !xana2PsgIrq && !looksFd03(hwIrq)) {
			for (unsigned a = 0x0100; a + 4u < 0xF000u; a++) {
				if (looksFd03((uint16_t)a) && mem_[a + 3] == 0x85) {
					mem_[0xFFF8] = (uint8_t)(a >> 8);
					mem_[0xFFF9] = (uint8_t)(a & 0xff);
					break;
				}
			}
		}
	}

	const uint16_t irq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
	if (irq == 0 || irq == 0xFFFF)
		return;

	/* Scan a short window of the IRQ ISR for FD03 bit tests. */
	int hasBit0Beq = 0;   /* 85 01 27 / 85 01 10 27 — need bit0 set */
	int hasBit0Bne = 0;   /* 85 01 26 — luxsor alternate path */
	int hasBit3Beq = 0;   /* 85 08 27 / 10 27 — need bit3 set */
	int hasBit3Bne = 0;   /* 85 08 26 — need bit3 clear */
	int bit2BnePos = 0;   /* 85 04 26 with +disp (laydock music gate) */
	int bit2BneNeg = 0;   /* 85 04 26 with -disp (busy spin) */
	int bit2Beq = 0;      /* 85 04 27 — music when bit2 clear (albatrss) */

	for (int i = 0; i < 48; i++) {
		const unsigned a = (unsigned)irq + (unsigned)i;
		if (a + 4 >= 0x10000) break;
		const uint8_t* p = mem_ + a;
		if (p[0] != 0x85) continue;
		const uint8_t mask = p[1];
		const uint8_t op = p[2];
		if (mask == 0x01) {
			if (op == 0x27) hasBit0Beq = 1;
			else if (op == 0x10 && p[3] == 0x27) hasBit0Beq = 1;
			else if (op == 0x26) hasBit0Bne = 1;
		} else if (mask == 0x08) {
			if (op == 0x27) hasBit3Beq = 1;
			else if (op == 0x10 && p[3] == 0x27) hasBit3Beq = 1;
			else if (op == 0x26) hasBit3Bne = 1;
		} else if (mask == 0x04 && op == 0x26) {
			const int8_t disp = (int8_t)p[3];
			if (disp >= 8) bit2BnePos = 1;
			else if (disp < 0) bit2BneNeg = 1;
		} else if (mask == 0x04 && op == 0x27) {
			bit2Beq = 1;
		}
	}

	/* laydock: BITA #4 / BNE music; then BITA #8 / BNE skip.
	   Pulse bit2 every other vsync so housekeeping (bit2 clear) also runs. */
	if (bit2BnePos && hasBit3Bne) {
		fd03VsyncSet_ = 0x04;
		fd03VsyncClr_ = 0x08;
		return;
	}
	/* ys_fm7 MANPR bit2 BEQ is NOT "need bit2" — that path only samples
	   $FD00/$FD01. Music countdown is the bit2-clear branch; keep default. */
	/* jikochu_fm7: BITA #4 / BNE skip — music runs only when bit2 clear. */
	if (bit2BnePos && !hasBit3Bne && !hasBit0Beq) {
		fd03VsyncSet_ = 0;
		fd03VsyncClr_ = 0x04;
		return;
	}
	/* reviver: need bit0+bit3; bit2 spin if set. */
	if (hasBit0Beq && hasBit3Beq) {
		fd03VsyncSet_ = 0x09;
		fd03VsyncClr_ = 0x04;
		return;
	}
	/* wibarm: need bit0; bit2 spin if set (no bit3 gate). */
	if (hasBit0Beq && bit2BneNeg && !hasBit0Bne) {
		fd03VsyncSet_ = 0x01;
		fd03VsyncClr_ = 0x04;
		return;
	}
	/* asteka2 APRG: BITA #1 / LBEQ + BITA #4 / BEQ skip-RTI. Pulse bit0
	   and keep bit2 clear. A bit2Beq-only return muted this title. */
	if (hasBit0Beq && !hasBit0Bne) {
		fd03VsyncSet_ = 0x01;
		fd03VsyncClr_ = (uint8_t)(0x08 | (bit2Beq ? 0x04 : 0));
		return;
	}
	/* albatrss OP.BIN $87CA: BITA #4 / BEQ music; RTI when bit2 is set. */
	if (bit2Beq) {
		fd03VsyncSet_ = 0;
		fd03VsyncClr_ = 0x04;
		return;
	}
}

void CHardFm7::ApplyFd03Vsync()
{
	fd03_ = (uint8_t)((fd03_ | fd03VsyncSet_) & (uint8_t)~fd03VsyncClr_);
	/* Do not re-arm jikochu $0614 every vsync — that retriggered PSG writes
	   and garbled SSG. Gate is armed once at TriggerPlay / Open settle. */
}

void CHardFm7::TriggerPlay(unsigned titleCode)
{
	const uint8_t b0 = (uint8_t)(titleCode & 0xffu);
	const uint8_t b1 = (uint8_t)((titleCode >> 8) & 0xffu);
	const uint8_t b2 = (uint8_t)((titleCode >> 16) & 0xffu);
	const uint8_t b3 = (uint8_t)((titleCode >> 24) & 0xffu);
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank);

	if (falcomMode_ || HasProgBanks()) {
		/* Falcom: prog index in bits12..15 (0x1000→1, 0x2010→2, 0x50a0→5). */
		uint8_t prog = (uint8_t)((titleCode >> 12) & 0xffu);
		if (!prog) prog = bank ? bank : song;
		if (!prog) prog = 1;
		/* BGM bank in bits4..11 (0x2010→1 MUSD10A, 0x50a0→0x0A YMUS06).
		   Low nibble is phrase index within that bank.
		   Bank 0 is real (ys2 TTLMS1). Do not replace a zero bank with b0:
		   0x1001 is TTLMS1 phrase 1, not bank 1 (TTLMS2). */
		uint8_t track = (uint8_t)((titleCode >> 4) & 0xffu);
		/* Ys2 (MUSPRG, no prog banks): same bitfield; prog mirrors track. */
		if (!HasProgBanks()) {
			prog = track ? track : 1;
		}
		StageVoice(track);

		/* Ys AV PATCH table (8 bytes/prog at PATCH load = init_pc):
		   t0 t1 | patchHi patchLo | callHi callLo | irqHi irqLo
		   t0<<8 = window size, t1<<8 = BGM window (10 30→$3000, 08 5C→$5C00).
		   Loader stores $FD85 at patchAddr, JSR callAddr, STD irq→$FFE2. */
		uint16_t callAddr = 0, irqAddr = 0, patchAddr = 0;
		const unsigned tableBase = (patchTableBase_ >= 0xE000) ? (unsigned)patchTableBase_
			: ((initPc_ >= 0xE000) ? (unsigned)initPc_ : 0xFF00u);
		if (HasProgBanks() && prog >= 1) {
			const unsigned ent = tableBase + (unsigned)(prog - 1) * 8u;
			if (ent + 8u <= 0x10000u) {
				const uint8_t t0 = mem_[ent];
				const uint8_t t1 = mem_[ent + 1];
				/* Ys-style size/page pair; xana PATCHes are code, not this table.
				   Ys FM-7 row 0 is TTLPRG: t0=$10, t1=$00, call=$147A,
				   irq=$11B7.  The zero page means no external MUS window; it
				   does not make the row padding. */
				if ((t0 == 0x08 || t0 == 0x10)
					&& (t1 == 0x00 || t1 == 0x30 || t1 == 0x4F || t1 == 0x5C)) {
					patchAddr = (uint16_t)(((uint16_t)mem_[ent + 2] << 8) | mem_[ent + 3]);
					callAddr = (uint16_t)(((uint16_t)mem_[ent + 4] << 8) | mem_[ent + 5]);
					irqAddr = (uint16_t)(((uint16_t)mem_[ent + 6] << 8) | mem_[ent + 7]);
					if (t1 != 0) {
						mdataAddr_ = (uint16_t)((uint16_t)t1 << 8);
						mdataSize_ = (unsigned)t0 << 8;
					}
				}
			}
		}

		StageProg(prog);

		if (mdataAddr_ > 0 && bgmPresent_[track])
			StageBgm(track);
		else if (mdataAddr_ > 0 && bgmPresent_[song])
			StageBgm(song);
		falcomSongLatch_ = prog;
		falcomCmdLatch_ = 0x01;
		/* Keep cmd visible so PATCH poll at $FD80 can JSR call / install FFE2.
		   hold=0 cleared on first read and made PATCH take the stop path (FFE2=FFDD).
		   Enough for the play handler to re-read; not so high that play re-arms
		   thousands of times (broke ys2_fmav Timer/IRQ). */
		falcomCmdHold_ = (callAddr == 0 && HasProgBanks()) ? 512 : 64;
		mem_[FM7_FD_FALCOM_SONG] = prog;
		mem_[FM7_FD_FALCOM_CMD] = 0x01;
		/* Phrase is the low nibble (Y00MUS(1)=0x1031). ys2 MUSPRG has no
		   prog banks so the HasProgBanks loader never ran; PATCH still
		   does LDA 5,X ($FD85) / STA $FFB1 / JSR $85DC. */
		mem_[0xFD85] = (uint8_t)(titleCode & 0x0fu);
		if (!HasProgBanks())
			cpu_.dp = 0xFD;
		/* Ys AV (prog banks) loader LDA $FD85 / STA onto patchAddr mailbox. */
		if (HasProgBanks()) {
			/* Song/param at $FD85 (PATCH LDA 5,X with X=$FD80). */
			/* FD85 is the phrase selector within the staged BGM bank. */
			uint8_t param = mem_[0xFD85];
			/* ys2 PATCH sets DP=$FD; ys1 PATCH does not — DP-relative
			   I/O in MANPR (FD03/FD15/FD16) needs it. */
			cpu_.dp = 0xFD;
			/* OPN helpers TST $2D12 / BNE skip — nonzero mutes all writes. */
			mem_[0x2D12] = 0;
			/* Mirror PATCH loader: poke $FD85 at patchAddr (song mailbox). */
			if (patchAddr >= 0x0100 && patchAddr < 0xFE00)
				mem_[patchAddr] = mem_[0xFD85];
			unsigned progBytes = 0;
			if (prog < PROG_BANKS && progPresent_[prog])
				progBytes = progBankSize_[prog];
			/* YMUSPR init at $1785 skips JSR $18B6 when $131B≠0
			   (ROM byte leftover); clear so first arm runs. */
			if (callAddr == 0x1785)
				mem_[0x131B] = 0;
			/* Follow PATCH table only when call target looks like 6809 code.
			   MANPR $317A is a song-header block (page3/data); real entry is
			   later — leave JSR to the live PATCH poll, do not plantJsr.
			   Still install soft IRQ when the table lists one. */
			int callLooksCode = 0;
			const unsigned progLoadBase = (!useOpn_ && patchTableBase_ == 0xFED0
				&& prog == 1) ? 0x1000u : 0x0800u;
			if (callAddr >= 0x0100 && callAddr < 0xFE00
				&& (progBytes == 0
					|| ((unsigned)callAddr >= progLoadBase
						&& (unsigned)callAddr < progLoadBase + progBytes))) {
				const uint8_t op = mem_[callAddr];
				/* PSHS/LDA/LDX/ORCC/JSR/JMP/NOP/BRA/BSR — not page3/data. */
				if (op == 0x34 || op == 0x86 || op == 0x8E || op == 0x1A
					|| op == 0xBD || op == 0x7E || op == 0x12 || op == 0x20
					|| op == 0xB6 || op == 0x10 || op == 0xCC || op == 0x8D
					|| op == 0xAD || op == 0x6E || op == 0x32 || op == 0x33)
					callLooksCode = 1;
				/* Table sometimes points at a short param block immediately
				   before the PSHS prologue (MANPR $317A → $318C). */
				if (!callLooksCode) {
					for (int d = 1; d < 24; d++) {
						const unsigned a = (unsigned)callAddr + (unsigned)d;
						if (a >= 0xFE00) break;
						if (mem_[a] == 0x34) {
							callAddr = (uint16_t)a;
							callLooksCode = 1;
							break;
						}
					}
				}
			}
			/* TTLPRG's $147A loader performs a resident handoff through the
			   live PATCH foreground.  Keep that path native; bounded host JSR
			   returns before its final song arm. */
			if (prog == 1 && !useOpn_ && patchTableBase_ == 0xFED0)
				callLooksCode = 0;
			/* OPN Falcom (ys_fmav TITLEP, xana AV): the live PATCH poll at
			   $FD80 loads the table itself. Host JSR + clearing the mailbox
			   left TITLEP waiting in CWAI with no native loader. */
			/* OMAKEP (prog 5, call $17AA) is the YMUS player. It talks to
			   YM2203 at $FD15: wait-ready does LDA #$04 / STA ,X / LDA 1,X
			   / BMI. On the PSG twin $FD16 is open-bus $FF so that BMI
			   never exits and every YSM00x row stays silent. */
			if (callLooksCode && prog == 5 && patchTableBase_ == 0xFED0) {
				callLooksCode = 0;
				if (!chipOpn_) {
					chipOpn_ = CEmuChipYm2608Create((uint32_t)opnHz_, 0, sampleRate_);
					if (chipOpn_)
						chipOpn_->Reset();
				}
				if (chipOpn_)
					useOpn_ = 1;
				mem_[0x133E] = mem_[0xFD85];
				mem_[0x133F] = 0;
				mem_[0x1340] = 0;
				mem_[0x1341] = 0;
				cpu_.d.b[1] = 1;
				cpu_.cc.i = true;
				RunSubroutine(0x17AA, 400000, 1);
				/* $10F1: STX $FFF8=$1101, STA $FD02=1, ANDCC #$EF. $1101 is
				   the FD03 wrapper; table irq $1345 is the music tick it
				   JSRs, not the hardware vector. */
				RunSubroutine(0x10F1, 20000, 0);
				mem_[0xFFF8] = 0x11;
				mem_[0xFFF9] = 0x01;
				mem_[0xFFE2] = 0x13;
				mem_[0xFFE3] = 0x45;
				mem_[0xFC00] = 0x20;
				mem_[0xFC01] = 0xFE;
				cpu_.pc.w = 0xFC00;
				cpu_.cc.i = false;
				cpu_.cc.f = true;
				cpu_.cwai = false;
				falcomCmdLatch_ = 0;
				falcomCmdHold_ = 0;
				mem_[FM7_FD_FALCOM_CMD] = 0;
			}
			if (callLooksCode && !useOpn_) {
				/* MANPR play dispatch: CMPA #2 at prologue; YMUSPR wants #1.
				   Prefer FD85 param when it matches a known arm; else #2. */
				uint8_t arm = mem_[0xFD85];
				if (prog == 1 && !useOpn_ && patchTableBase_ == 0xFED0)
					arm = 0; /* TTLPRG's PATCH row passes phrase zero. */
				else if (arm != 1 && arm != 2 && arm != 9)
					arm = 2;
				cpu_.d.b[1] = arm;
				if (!useOpn_ && patchTableBase_ == 0xFED0)
					mem_[0xFFE5] = 0; /* MANPR hardware-output mode */
				RunSubroutine(callAddr);
				if (!useOpn_ && patchTableBase_ == 0xFED0) {
					const int man2 = (prog == 3) ? 1 : 0;
					const uint16_t shadowMixer = man2 ? 0x2EB4 : 0x2D4F;
					const uint16_t shadowVolume = man2 ? 0x2EB5 : 0x2D50;
					const uint16_t outputEntry = man2 ? 0x2F70 : 0x2DCA;
					/* PATCH finishes a successful load with ANDCC #$EF:
					   IRQ enabled, FIRQ still masked.  The stripped BIOS path
					   can miss that epilogue, so preserve the real line state. */
					/* Its absent BIOS foreground would otherwise resume in
					   the staged loader, mask IRQ, and wait forever.  Park
					   that foreground below the $FC80 stack while MANPR's
					   real hardware IRQ owns playback. */
					mem_[0xFC00] = 0x20; /* BRA $FC00 */
					mem_[0xFC01] = 0xFE;
					/* Complete MANPR's native shadow-register restore after
					   the absent disk BIOS makes the bounded loader return
					   before its $2DCA unmute epilogue. */
					if (mem_[shadowVolume] == 0 && mem_[shadowVolume + 1] == 0
						&& mem_[shadowVolume + 2] == 0) {
						/* The ripped BIOS cannot complete the final volume
						   transfer.  Seed only its three PSG shadow volumes;
						   MANPR still supplies all notes, timing and IRQs. */
						mem_[shadowVolume] = 0x0C;
						mem_[shadowVolume + 1] = 0x0C;
						mem_[shadowVolume + 2] = 0x0C;
					}
					/* The ripped BIOS doesn't run TTLPRG to set the ISR vector
					   if we jump straight to MANPR. Set it manually. */
					if (mem_[0xFFE2] == 0xFF && mem_[0xFFE3] == 0xFF) {
						const uint16_t tick = man2 ? 0x29EC : 0x28EA;
						mem_[0xFFE2] = (uint8_t)(tick >> 8);
						mem_[0xFFE3] = (uint8_t)(tick & 0xFF);
					}
					/* The missing BIOS restore also leaves AY mixer R7 at
					   zero, enabling zero-period noise on all three audible
					   channels. MANPR's music is tone-based; start with only
					   the three tone gates enabled and let later writes take
					   ownership normally. */
					if (mem_[shadowMixer] == 0)
						mem_[shadowMixer] = 0x38;
					if (mem_[outputEntry] == 0x34 && mem_[outputEntry + 1] == 0x01)
						RunSubroutine(outputEntry);
					/* MANPR2 $2CFE returns before STA $3040 (ripped AY helper).
					   Channels stay 0 so the host tick never sees a stream —
					   one leftover smash tone, or silence once F-cmds are
					   skipped. Finish the phrase arm the play routine wrote. */
					if (man2 && mem_[0x3040] == 0 && mdataAddr_ == 0x4F00) {
						uint16_t u = (uint16_t)((mem_[0x4F00] << 8) | mem_[0x4F01]);
						u = (uint16_t)(((u & 0xFFu) << 8) | (u >> 8));
						u = (uint16_t)(u + 0x200u - 13u);
						uint8_t phrase = mem_[0x2982];
						u = (uint16_t)(u + (uint16_t)(phrase + 1u) * 13u + 1u);
						static const uint16_t kCur[3] = { 0x3042, 0x3067, 0x308C };
						static const uint16_t kLoop[3] = { 0x3044, 0x3069, 0x308E };
						static const uint16_t kCnt[3] = { 0x3040, 0x3065, 0x308A };
						for (int ch = 0; ch < 3; ++ch) {
							uint16_t w = (uint16_t)((mem_[u] << 8) | mem_[(uint16_t)(u + 1)]);
							u = (uint16_t)(u + 2);
							w = (uint16_t)(((w & 0xFFu) << 8) | (w >> 8));
							w = (uint16_t)(w + 0x200u);
							mem_[kCur[ch]] = (uint8_t)(w >> 8);
							mem_[kCur[ch] + 1] = (uint8_t)w;
							w = (uint16_t)((mem_[u] << 8) | mem_[(uint16_t)(u + 1)]);
							u = (uint16_t)(u + 2);
							w = (uint16_t)(((w & 0xFFu) << 8) | (w >> 8));
							w = (uint16_t)(w + 0x200u);
							mem_[kLoop[ch]] = (uint8_t)(w >> 8);
							mem_[kLoop[ch] + 1] = (uint8_t)w;
							mem_[kCnt[ch]] = 1;
						}
						mem_[0x3047] = 0x08;
						mem_[0x306C] = 0x09;
						mem_[0x3091] = 0x0A;
						mem_[0x3048] = 0x00;
						mem_[0x306D] = 0x02;
						mem_[0x3092] = 0x04;
						mem_[0x2983] = 1;
					}
					cpu_.pc.w = 0xFC00;
					mem_[0x28E4] = 1;
					mem_[0xFFE5] = 0;
					cpu_.cc.i = false;
					cpu_.cc.f = true;
				}
				/* PATCH already consumed this play — drop latch so it cannot
				   JSR the raw table address a second time during settle. */
				falcomCmdLatch_ = 0;
				falcomCmdHold_ = 0;
				mem_[FM7_FD_FALCOM_CMD] = 0;
			}
			/* Do NOT fallback JSR $0000 when table call is missing: TTLPRG
			   (title 0x1000) starts with B6xx and would clear $FD80 before the
			   live PATCH poll can see the play command. */
			/* Soft IRQ slot (PATCH STD $FFE2). Table irq field is often a
			   work-RAM trampoline addr (ys 28EA/28A2) — only plant $FFF8 when
			   the target already looks like an FD03 ISR. */
			if (!(prog == 1 && !useOpn_ && patchTableBase_ == 0xFED0)
				&& irqAddr >= 0x0100 && irqAddr < 0xFE00
				&& (progBytes == 0
					|| ((unsigned)irqAddr >= progLoadBase
						&& (unsigned)irqAddr < progLoadBase + progBytes)
					|| irqAddr >= 0x2C00)) {
				mem_[0xFFE2] = (uint8_t)(irqAddr >> 8);
				mem_[0xFFE3] = (uint8_t)(irqAddr & 0xff);
			}
			/* Some AV MANPR images plant a work address rather than an ISR. */
			{
				auto looksFd03 = [&](uint16_t a) -> int {
					if (a < 0x0100 || a >= 0xFE00) return 0;
					const uint8_t* p = mem_ + a;
					if (p[0] == 0xB6 && p[1] == 0xFD && p[2] == 0x03) return 1;
					if (p[0] == 0x96 && p[1] == 0x03) return 1;
					return 0;
				};
				uint16_t hwIrq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
				if (!looksFd03(hwIrq)) {
					uint16_t found = 0;
					for (unsigned a = 0x0100; a + 4u < 0xF000u; a++) {
						if (looksFd03((uint16_t)a) && mem_[a + 3] == 0x85) {
							found = (uint16_t)a;
							break;
						}
					}
					if (found) {
						mem_[0xFFF8] = (uint8_t)(found >> 8);
						mem_[0xFFF9] = (uint8_t)(found & 0xff);
					}
				}
			}
			/* Ys FM-7's ripped image has no BIOS foreground to return to.
			   Once PATCH has installed MANPR's IRQ vectors, keep the main
			   CPU out of the staged disk-loader code: it masks IRQ/FIRQ and
			   leaves only the initial zero-period PSG register image, heard
			   as a short high-frequency pattern repeating forever. */
			if (!useOpn_ && patchTableBase_ == 0xFED0 && callLooksCode) {
				mem_[0xFC00] = 0x20; /* BRA $FC00 */
				mem_[0xFC01] = 0xFE;
				cpu_.pc.w = 0xFC00;
				mem_[0x28E4] = 1;
				mem_[0xFFE5] = 0;
				cpu_.cc.i = false;
				cpu_.cc.f = true;
			}
		}
		/* Also mirror classic mailbox for hybrids. */
		playSongLatch_ = HasProgBanks() ? (uint8_t)(titleCode & 0x0fu) : track;
		if (!playSongLatch_ && track) playSongLatch_ = track;
		playParamA_ = b1;
		playParamB_ = b2;
		playParamC_ = b3;
		playCmdLatch_ = 0x01;
		playCmdHold_ = 8;
		mem_[FM7_FD_PLAY_SONG] = playSongLatch_;
		mem_[FM7_FD_PLAY_A] = playParamA_;
		mem_[FM7_FD_PLAY_B] = playParamB_;
		mem_[FM7_FD_PLAY_C] = playParamC_;
		mem_[FM7_FD_PLAY_CMD] = 0x01;
		FinishXana2PsgPlay();
		return;
	}

	/* Bank file vs song-in-bank:
	   - high byte (b1) selects a bgm bank when present (sharrier 0x0100)
	   - low byte (b0) is a bank index when that bank exists (ishtar 0x0007)
	   - asteka2: FD59=b0 is APRG/ENDPRG/TTLPRG, FD5A=b1 is the phrase.
	     0x0002 is TTLPRG (empty in the rip), not a song inside APRG.
	   ishtar packs the opposite of sharrier: 0x0807 = bank 7 (ISSD) + cmd 8,
	   0x0100 = bank 0 (IBGM1) + cmd 1. MUSIC.P at $5000 is the tell. */
	auto bankOk = [&](unsigned i) -> int {
		return (i < 128 && bgmPresent_[i] && bgmBank_[i] && bgmBankSize_[i] > 0) ? 1 : 0;
	};
	const int musicP = (mem_[0x5000] == 0x7E && mem_[0x5001] == 0x58) ? 1 : 0;
	uint8_t stage = 0xff;
	if (IsDaivaPatch()) {
		/* FD59 selects OP/FLEET/BATL/ED. 0x0101 is FLEET phrase 1
		   (b0=1); 0x0102 is BATL phrase 1 (b0=2). b1-as-bank would
		   stage FLEET for 0x0102 and then PATCH song 2 would copy it. */
		if (bankOk(b0) || (b0 == 0 && bankOk(0)))
			stage = (uint8_t)b0;
	} else if (IsAsteka2Patch()) {
		/* FD59=b0 is APRG/ENDPRG/TTLPRG, FD5A=b1 is the phrase.
		   0x0101 Castillo is file 1 phrase 1; 0x0100 is file 0 phrase 1.
		   Do not treat b1 as a bank — that staged ENDPRG for 0x0100 and
		   then ran APRG's $A3AF against the wrong image. Missing TTLPRG
		   (file 2) is salvaged in FinishAsteka2Play, not by copying APRG
		   onto $0100. */
		if (bankOk(b0) || (b0 == 0 && bankOk(0)))
			stage = (uint8_t)b0;
	} else if (IsTelenetMusFile()) {
		/* valis/dds/yakyufan: 0x01xx is play-cmd 1 + MUS file in b0.
		   Staging b1 (always 1, and MUS01 exists) made every 0x01xx
		   title play Running Star / Fantasm Soldier. */
		if (bankOk(b0) || (b0 == 0 && bankOk(0)))
			stage = (uint8_t)b0;
	} else if (IsSharrierPatch()) {
		/* 0xNNPP: MUS file in b1, $B040 phrase in b0 (1-based below).
		   b0==0 must not fall through to bank 0 (MUS00). */
		if (b1 && bankOk(b1))
			stage = (uint8_t)b1;
		else if (bankOk(b0))
			stage = (uint8_t)b0;
	} else if (musicP) {
		if (bankOk(b0))
			stage = (uint8_t)b0;
		else if (b0 == 0 && bankOk(0))
			stage = 0;
		else if (b1 && bankOk(b1))
			stage = (uint8_t)b1;
	} else if (b1 && bankOk(b1))
		stage = (uint8_t)b1;
	else if (b0 && bankOk(b0))
		stage = (uint8_t)b0;
	else if (b0 && bankOk(0))
		stage = 0;
	else if (bankOk(bank))
		stage = bank;
	else if (bankOk(song))
		stage = song;
	else if (bankOk(0))
		stage = 0;

	if (stage < 128 && bankOk(stage))
		StageBgm(stage);

	playSongLatch_ = b0;
	playParamA_ = b1;
	playParamB_ = b2;
	playParamC_ = b3;
	playCmdLatch_ = 0x01;
	/* jikochu: IRQ/main can poll $FD58 many times before the play path;
	   hold=8 cleared the cmd and left boot JSR $C000 as the only song. */
	playCmdHold_ = (!useOpn_ && mdataAddr_ == 0xC000) ? 256
		: ((mdataAddr_ == 0) ? 256 : 8);
	mem_[FM7_FD_PLAY_SONG] = playSongLatch_;
	mem_[FM7_FD_PLAY_A] = playParamA_;
	mem_[FM7_FD_PLAY_B] = playParamB_;
	mem_[FM7_FD_PLAY_C] = playParamC_;
	mem_[FM7_FD_PLAY_CMD] = 0x01;
	if (IsSharrierPatch()) {
		/* PATCH LDA $FD5A / JSR $847B. A * $0E + $B040 is the phrase
		   table; slot 0 is $FF so XML 0x0500 (phrase 0) is table index 1.
		   0x0607's slot 8 is packed voice bytes, not a row — play MUS06
		   slot 1 voices against header word 7 (0171). */
		playParamA_ = (uint8_t)(b0 + 1);
		if ((titleCode & 0xffffu) == 0x0607u)
			playParamA_ = 1;
		mem_[FM7_FD_PLAY_A] = playParamA_;
	}

	/* jikochu_fm7: OPEN1 PSG writes are gated by TST $0614 / BPL; PATCH clears
	   $0614 after play setup so IRQ ticks never reach FD0D/FD0E. Keep bit7. */
	if (!useOpn_ && chipAy_ && mdataAddr_ == 0xC000
		&& mem_[0xC19D] == 0x7D && mem_[0xC19E] == 0x06 && mem_[0xC19F] == 0x14)
		mem_[0x0614] = 0x80;

	FinishAlbatrssPlay();
	FinishAsteka2Play();
	FinishWibarmPlay();
	FinishDaivaOpPlay();
	FinishDaivaEdPlay();
	FinishSharrierPlay();
	ArmLaydockChannels();
}

void CHardFm7::FinishXana2PsgPlay()
{
	/* XA2PSGPATCH word table: (play,tick)×4 then LDS at $FF00. Each PR.NO*
	   bank is the same player at a different origin. Tick is reached only
	   through IRQ $FF94; the PSG board has no YM timer and RefreshFd03
	   used to steal $FFF8, so the 1s probe heard a clipped AY burst. */
	xana2Tick_ = 0;
	xana2Tempo_ = 0;
	if (useOpn_)
		return;
	if (mem_[0xFF00] != 0x10 || mem_[0xFF01] != 0xCE
		|| mem_[0xFF02] != 0xFC || mem_[0xFF03] != 0x80)
		return;
	uint8_t prog = (uint8_t)((titleCode_ >> 12) & 0xffu);
	if (!prog)
		prog = 1;
	if (prog < 1 || prog > 4)
		return;
	const unsigned ent = 0xFEF0u + (unsigned)(prog - 1) * 4u;
	const uint16_t play = (uint16_t)(((uint16_t)mem_[ent] << 8) | mem_[ent + 1]);
	const uint16_t tick = (uint16_t)(((uint16_t)mem_[ent + 2] << 8) | mem_[ent + 3]);
	if (play < 0x1000 || tick < 0x1000 || play >= 0xE000 || tick >= 0xE000)
		return;
	if (mem_[play] != 0x34 || mem_[play + 1] != 0x01)
		return;

	uint16_t header = 0;
	for (unsigned a = play; a + 7u < 0x10000u && a < (unsigned)play + 96u; a++) {
		if (mem_[a] == 0x8E && mem_[a + 1] == 0x5C && mem_[a + 2] == 0x00
			&& mem_[a + 3] == 0x10 && mem_[a + 4] == 0x8E) {
			header = (uint16_t)(((uint16_t)mem_[a + 5] << 8) | mem_[a + 6]);
			break;
		}
	}
	if (!header)
		return;
	int hasLive = 0;
	for (unsigned a = play; a + 3u < 0x10000u && a < (unsigned)play + 96u; a++) {
		if (mem_[a] == 0xA7 && mem_[a + 1] == 0x88 && mem_[a + 2] == 0x1B) {
			hasLive = 1;
			break;
		}
	}
	const uint16_t live = (uint16_t)(hasLive ? (header + 27u) : header);
	uint16_t tempo = 0;
	for (unsigned a = tick; a + 3u < 0x10000u && a < (unsigned)tick + 24u; a++) {
		if (mem_[a] != 0x7A)
			continue;
		const uint16_t addr = (uint16_t)(((uint16_t)mem_[a + 1] << 8) | mem_[a + 2]);
		if (addr != 0x617B && addr != 0x606A && addr >= 0x1000 && addr < 0x8000) {
			tempo = addr;
			break;
		}
	}
	if (!tempo)
		return;

	mem_[0xF000] = 0x39;
	const uint8_t ph = (uint8_t)(titleCode_ & 0x0fu);
	mem_[0x617B] = 0;
	mem_[0x606A] = 0;
	mem_[0x607D] = (uint8_t)(ph & 1u);
	mem_[0x60A5] = (uint8_t)(((ph >> 1) & 1u) ^ 1u);

	cpu_.dp = 0xFD;
	cpu_.index[0].w = 0xFD80;
	RunSubroutine(play, 400000, 1);
	/* $416F-class copies ptrs into live records but leaves +6 as BSS $10
	   (AY envelope). Header +6 is the 4-bit volume. */
	if (hasLive) {
		for (int ch = 0; ch < 3; ch++) {
			const unsigned src = (unsigned)header + (unsigned)ch * 9u + 6u;
			const unsigned dst = (unsigned)live + (unsigned)ch * 9u + 6u;
			if (src < 0x10000u && dst < 0x10000u)
				mem_[dst] = (uint8_t)(mem_[src] & 0x0Fu);
		}
	}

	if (mem_[0xFF96] == 0x10 && mem_[0xFF97] == 0x8E) {
		mem_[0xFF98] = (uint8_t)(tick >> 8);
		mem_[0xFF99] = (uint8_t)tick;
	}
	mem_[0xFFF8] = 0xFF;
	mem_[0xFFF9] = 0x94;
	mem_[0xFFF6] = 0xFF;
	mem_[0xFFF7] = 0x94;
	/* Native tempo reload is 8 or 12 for a ~488 Hz AV timer. 60 Hz vsync
	   needs a /1 divider so the play-time count=9 expires inside 1s. */
	for (int i = 0; i < 12; i++) {
		mem_[tempo] = 1;
		RunSubroutine(tick, 80000, 1);
	}
	mem_[tempo] = 1;
	xana2Tick_ = tick;
	xana2Tempo_ = tempo;
	mem_[0xFC00] = 0x20;
	mem_[0xFC00 + 1] = 0xFE;
	cpu_.pc.w = 0xFC00;
	cpu_.cc.i = false;
	cpu_.cc.f = true;
	cpu_.cwai = false;
}

int CHardFm7::IsXana2PsgPlayer() const
{
	if (useOpn_)
		return 0;
	if (mem_[0xFF00] != 0x10 || mem_[0xFF01] != 0xCE
		|| mem_[0xFF02] != 0xFC || mem_[0xFF03] != 0x80)
		return 0;
	if (mem_[0xFEF0] < 0x10 || mem_[0xFEF2] < 0x10)
		return 0;
	return 1;
}

void CHardFm7::FinishSharrierPlay()
{
	if (!IsSharrierPatch())
		return;
	const uint8_t file = (uint8_t)((titleCode_ >> 8) & 0xff);
	if (file < 128 && bgmPresent_[file] && bgmBank_[file])
		StageBgm(file);
	if ((titleCode_ & 0xffffu) == 0x0607u) {
		playParamA_ = 1;
		mem_[FM7_FD_PLAY_A] = 1;
	}
	ArmSharrierSeq();
	/* Slot 1 only. Host $847B on bosses (A>=2) left IDA's BSS at
	   $8D00=FE and peak 0. Native PATCH already plays those rows. */
	if (playParamA_ != 1)
		return;
	cpu_.cc.i = true;
	RunSubroutine(0x8400, 400000, 0);
	cpu_.d.b[1] = playParamA_;
	RunSubroutine(0x847B, 400000, 1);
	cpu_.cc.i = false;
}

void CHardFm7::ArmSharrierSeq()
{
	/* IRQ $866E: LDU #$B000 / LDU A,U / ADDD #$B100. Default header
	   00FA/00C8 then lands on a shared $B1xx scale table. Point word 0
	   at (slot_ptr - $100) so the add reaches that slot's $B000 sequence.
	   00 06 rows only — BATTLE FIELD's $B040 slot 9 is not that format. */
	uint16_t seq = 0;
	if ((titleCode_ & 0xffffu) == 0x0607u) {
		seq = (uint16_t)(((uint16_t)mem_[0xB00E] << 8) | mem_[0xB00F]);
	} else {
		const unsigned slot = playParamA_;
		if (slot < 1 || slot > 12)
			return;
		const uint16_t row = (uint16_t)(0xB040u + slot * 14u);
		if (mem_[row] != 0x00 || mem_[row + 1] != 0x06)
			return;
		seq = (uint16_t)(((uint16_t)mem_[row + 2] << 8) | mem_[row + 3]);
	}
	if (seq == 0xFFFF || seq < 0x100 || seq >= 0x0F00)
		return;
	const uint16_t h = (uint16_t)(seq - 0x100);
	mem_[0xB000] = (uint8_t)(h >> 8);
	mem_[0xB001] = (uint8_t)(h & 0xff);
}

void CHardFm7::FinishAsteka2Play()
{
	if (!IsAsteka2Patch())
		return;
	/* TTLPRG is 0 bytes. PATCH song 2 copies $8000→$0100 / JSR $0855 into
	   empty RAM. APRG phrase 1 (table $A5E2+3 → $A603) is a second
	   3-channel sequence, not Mundo at phrase 0. Retarget the mailbox
	   at APRG+phrase 1 so native $A3AF runs the same path as Mundo. */
	if ((titleCode_ & 0xffu) != 2 || ((titleCode_ >> 8) & 0xffu) != 0)
		return;
	if (bgmPresent_[2] && bgmBankSize_[2] > 0)
		return;
	if (!bgmPresent_[0] || bgmBankSize_[0] == 0)
		return;
	StageBgm(0);
	playSongLatch_ = 0;
	playParamA_ = 1;
	playCmdLatch_ = 0x01;
	playCmdHold_ = 8;
	mem_[FM7_FD_PLAY_SONG] = 0;
	mem_[FM7_FD_PLAY_A] = 1;
	mem_[FM7_FD_PLAY_CMD] = 0x01;
	mem_[0x7EEC] = 1;
	mem_[0x7EEB] = 0;
}

void CHardFm7::FinishWibarmPlay()
{
	if (!IsWibarmPatch())
		return;
	/* XML 0x0102 戦闘: PATCH LDA $FD5A / JSR $0D00, and $0D00 LSLA /
	   LEAX A,X from $ED00. MUS02 phrase 1's word is $9604 (off the
	   bank). The commented title was 0x0100 = MUS00 phrase 1, whose
	   word $002E lands inside MUS00. */
	if ((titleCode_ & 0xffu) != 2 || ((titleCode_ >> 8) & 0xffu) != 1)
		return;
	if (!bgmPresent_[0] || bgmBankSize_[0] == 0)
		return;
	StageBgm(0);
	playSongLatch_ = 0;
	playParamA_ = 1;
	playCmdLatch_ = 0x01;
	playCmdHold_ = 8;
	mem_[FM7_FD_PLAY_SONG] = 0;
	mem_[FM7_FD_PLAY_A] = 1;
	mem_[FM7_FD_PLAY_CMD] = 0x01;
}

int CHardFm7::IsAsteka2Patch() const
{
	/* PATCH $202D: LDA $FD59; $203B: CMPA #2 — file in FD59, phrase in FD5A. */
	return (initPc_ == 0x2000 && mdataAddr_ == 0x8000
		&& mem_[0x202D] == 0xB6 && mem_[0x202E] == 0xFD && mem_[0x202F] == 0x59
		&& mem_[0x203B] == 0x81 && mem_[0x203C] == 0x02) ? 1 : 0;
}

int CHardFm7::IsDaivaPatch() const
{
	/* PATCH $5032 LDA $FD59 / $5044 CMPA #3 — OP/FLEET/BATL/ED. */
	return (initPc_ == 0x5000 && mdataAddr_ == 0
		&& mem_[0x5032] == 0xB6 && mem_[0x5033] == 0xFD && mem_[0x5034] == 0x59
		&& mem_[0x5044] == 0x81 && mem_[0x5045] == 0x03) ? 1 : 0;
}

int CHardFm7::IsWibarmPatch() const
{
	/* PATCH $403A LDA $FD5A / $403D JSR $0D00. */
	return (initPc_ == 0x4000 && mdataAddr_ == 0xED00
		&& mem_[0x403A] == 0xB6 && mem_[0x403B] == 0xFD && mem_[0x403C] == 0x5A
		&& mem_[0x403D] == 0xBD && mem_[0x403E] == 0x0D && mem_[0x403F] == 0x00) ? 1 : 0;
}

int CHardFm7::IsTelenetMusFile() const
{
	/* valis: LDA $FD59 / LDX #$C000 / LDB $FD5A / JSR DRIVER. */
	if (initPc_ == 0 && mdataAddr_ == 0xC000
		&& mem_[0x30] == 0xB6 && mem_[0x31] == 0xFD && mem_[0x32] == 0x59
		&& mem_[0x36] == 0x8E && mem_[0x37] == 0xC0 && mem_[0x38] == 0x00)
		return 1;
	/* dds: LDA $FD5A / STA $F8C0 / LDA $FD59 / LDD #$0100. */
	if (initPc_ == 0x2000 && mdataAddr_ == 0x0100
		&& mem_[0x203E] == 0xB6 && mem_[0x203F] == 0xFD && mem_[0x2040] == 0x59
		&& mem_[0x2044] == 0xCC && mem_[0x2045] == 0x01 && mem_[0x2046] == 0x00)
		return 1;
	/* yakyufan: LDA $FD59 / LDD #$2000 / JSR $B824. Not albatrss
	   (init $0000, same mdata). */
	if (initPc_ == 0x1000 && mdataAddr_ == 0x2000
		&& mem_[0x103A] == 0xB6 && mem_[0x103B] == 0xFD && mem_[0x103C] == 0x59
		&& mem_[0x1040] == 0xCC && mem_[0x1041] == 0x20 && mem_[0x1042] == 0x00)
		return 1;
	return 0;
}

int CHardFm7::IsSharrierPatch() const
{
	/* PATCH $103B LDA $FD5A / $103E JSR $847B. A indexes $B040. */
	return (initPc_ == 0x1000 && mdataAddr_ == 0xB000
		&& mem_[0x103B] == 0xB6 && mem_[0x103C] == 0xFD && mem_[0x103D] == 0x5A
		&& mem_[0x103E] == 0xBD && mem_[0x103F] == 0x84 && mem_[0x1040] == 0x7B) ? 1 : 0;
}

unsigned CHardFm7::OpnWrites() const
{
	if (!chipOpn_) return 0;
	unsigned w = 0, k = 0, f = 0, s = 0, m = 0;
	CEmuChipYm2608GetPlayMetrics(chipOpn_, &w, &k, &f, &s, &m);
	return w;
}

unsigned CHardFm7::AyWrites() const
{
	return chipAy_ ? CEmuChipAyWriteCount(chipAy_) : 0;
}

uint64_t CHardFm7::RunCpu(uint64_t cycles)
{
	if (cycles == 0) return 0;
	BindCpuCallbacks();
	const unsigned long start = cpu_.cycles;
	const unsigned long target = start + (unsigned long)cycles;
	int guard = 0;
	while (cpu_.cycles < target && guard++ < 4000000) {
		const int rc = mc6809_step(&cpu_);
		if (rc != 0)
			break;
	}
	const uint64_t ran = (uint64_t)(cpu_.cycles - start);
	cpuCycles_ += ran;
	return ran;
}

void CHardFm7::RunSubroutine(uint16_t addr, int maxSteps, int clockChips)
{
	if (addr == 0 || addr == 0xFFFF) return;
	if (maxSteps < 1) maxSteps = 200000;
	BindCpuCallbacks();
	const uint16_t retPc = cpu_.pc.w;
	const uint16_t sp0 = cpu_.index[3].w;
	cpu_.index[3].w = (uint16_t)(sp0 - 2);
	mem_[cpu_.index[3].w] = (uint8_t)(retPc >> 8);
	mem_[(uint16_t)(cpu_.index[3].w + 1)] = (uint8_t)(retPc & 0xff);
	cpu_.pc.w = addr;
	int guard = 0;
	while (guard++ < maxSteps) {
		const uint16_t before = cpu_.pc.w;
		const unsigned long c0 = cpu_.cycles;
		if (mc6809_step(&cpu_) != 0)
			break;
		const unsigned long dc = cpu_.cycles - c0;
		if (dc && clockChips) {
			if (chipOpn_ && cpuHz_ > 0)
				chipOpn_->AdvanceClocks(((uint64_t)dc * (uint64_t)opnHz_) / (uint64_t)cpuHz_ + 1u);
			if (chipAy_ && cpuHz_ > 0)
				chipAy_->AdvanceClocks(((uint64_t)dc * (uint64_t)ayHz_) / (uint64_t)cpuHz_ + 1u);
			cpuCycles_ += dc;
		}
		if (before != retPc && cpu_.pc.w == retPc)
			break;
	}
	cpu_.index[3].w = sp0;
	/* This is a host-side synchronous call.  A missing BIOS helper can make
	   a ripped program miss its RTS; never leave the foreground CPU running
	   through the stack/RAM after the bounded call returns to the host. */
	cpu_.pc.w = retPc;
}

void CHardFm7::UnwindStuckBootJsr()
{
	/* Falcom high PATCH is a table, not a boot-JSR wrapper. */
	if (falcomMode_ && initPc_ >= 0xE000)
		return;
	const unsigned base = initPc_;
	if (base >= 0xFE00)
		return;
	unsigned jsrAt = 0, jsrTgt = 0, after = 0, poll = 0;
	for (unsigned a = base; a + 3u < 0x10000u && a < base + 96u; a++) {
		if (mem_[a] == 0xB6 && mem_[a + 1] == 0xFD && mem_[a + 2] == 0x58) {
			poll = a;
			break;
		}
		if (mem_[a] == 0xBD && jsrAt == 0) {
			jsrTgt = ((unsigned)mem_[a + 1] << 8) | mem_[a + 2];
			jsrAt = a;
			after = a + 3;
		}
	}
	if (!jsrAt || !poll || jsrTgt == 0)
		return;
	const uint16_t pc = cpu_.pc.w;
	int inPatch = (pc >= base && pc < poll + 16u) ? 1 : 0;
	if (inPatch)
		return;
	int inCallee = 0;
	if (pc >= jsrTgt && pc < jsrTgt + 0x2800u)
		inCallee = 1;
	if (jsrTgt >= 0xF000 && pc >= 0xF000)
		inCallee = 1;
	if (!inCallee)
		return;
	unsigned land = after;
	while (land + 3u <= poll) {
		if (mem_[land] == 0xBD) {
			land += 3;
			continue;
		}
		break;
	}
	for (unsigned a = land; a + 1u < poll && a < land + 20u; a++) {
		if (mem_[a] == 0x1C && mem_[a + 1] == 0xEF) {
			land = a;
			break;
		}
	}
	cpu_.pc.w = (uint16_t)land;
	cpu_.cc.i = false;
	cpu_.cc.f = true;
	cpu_.cwai = false;
	cpu_.sync = false;
}

int CHardFm7::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	titleCode_ = titleCode;
	memset(mem_, 0, sizeof(mem_));
	FreeBanks();
	codeHighWater_ = 0;
	mmrAvail_ = 0;
	mmrOn_ = 0;
	mmrTouched_ = 0;
	albatrssMode_ = 0;
	albatrssPoll_ = 0;
	playCmdLatch_ = 0;
	playSongLatch_ = 0;
	playParamA_ = 0;
	playParamB_ = 0;
	playParamC_ = 0;
	playCmdHold_ = 0;
	falcomCmdLatch_ = 0;
	falcomSongLatch_ = 0;
	falcomCmdHold_ = 0;
	fd02_ = 0;
	fd03_ = 0x08; /* bit3 set = no vsync pending (PATCH BITA #$08 / BNE skip) */
	fd05_ = 0; /* sub-CPU not busy */
	fd05HaltSticky_ = 0;
	ymIrqSeen_ = 0;
	fd03VsyncSet_ = 0;
	fd03VsyncClr_ = 0x08;
	fd03VsyncPhase_ = 0;
	opnDataLatch_ = 0;
	psgDataLatch_ = 0;
	opnCmd_ = 0;
	psgCmd_ = 0;
	falcomMode_ = IsFalcomSubtype(ge);
	vdataAddr_ = CEmuParseOptHex(ge, "vdata_addr", -1);
	vdataSize_ = CEmuParseOptHex(ge, "vdata_size", 0);
	if (vdataSize_ <= 0) vdataSize_ = CEmuParseOptHex(ge, "vfile_size", 0);
	int loadedCode = 0;

	int defMdata = 0x3000;
	int defMsize = 0x1000;
	if (falcomMode_) {
		defMdata = 0x5c00; /* xana2 */
		defMsize = 0x2800;
		if (_stricmp(ge->subtype, "ys") == 0 || _stricmp(ge->subtype, "ysav") == 0
			|| _stricmp(ge->platform, "mucomfm") == 0) {
			/* Table entries embed page at t1; MANPR also refs 4D00 — prefer 5C00 for AV. */
			defMdata = 0x5c00;
			int hasProgRom = 0;
			for (int i = 0; i < ge->romCount; i++) {
				if (_stricmp(ge->rom[i].type, "prog") == 0) { hasProgRom = 1; break; }
			}
			if (!hasProgRom)
				defMdata = 0x4d00;
			/* ys_fm7 PATCH@FED0 table uses an $4F00 active window. */
			if (_stricmp(ge->subtype, "ys") == 0
				|| (ge->archive[0] && _stricmp(ge->archive, "ys_fm7") == 0))
				defMdata = 0x4f00;
		}
		if (ge->archive[0] && _strnicmp(ge->archive, "ys2", 3) == 0)
			defMdata = 0x8c00; /* Ys2 MUSPRG window */
		/* Detect ys2-style: MUSPRG code + bgm, no prog banks. */
		int hasMusPrg = 0, hasProgRom = 0;
		for (int i = 0; i < ge->romCount; i++) {
			if (_stricmp(ge->rom[i].type, "prog") == 0) hasProgRom = 1;
			if (_strnicmp(ge->rom[i].name, "MUSPRG", 6) == 0) hasMusPrg = 1;
		}
		if (hasMusPrg && !hasProgRom)
			defMdata = 0x8c00;
	}

	/* Resolve mdata early for Falcom prog clamp. */
	mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", defMdata);
	{
		int ms = CEmuParseOptHex(ge, "mdata_size", defMsize);
		int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
		if (mfs > ms) ms = mfs;
		if (ms <= 0 || ms > BGM_SIZE) ms = defMsize;
		mdataSize_ = (unsigned)ms;
	}
	if (falcomMode_ && vdataAddr_ < 0)
		vdataAddr_ = 0x0100;
	if (falcomMode_ && vdataSize_ <= 0)
		vdataSize_ = (int)mdataAddr_ > vdataAddr_ ? ((int)mdataAddr_ - vdataAddr_) : 0x4c00;

	/* Pass0: irom/voice underlay. Pass1: code/prog/bgm (code wins over irom). */
	unsigned codeLo[24], codeHi[24];
	int nCode = 0;
	for (int pass = 0; pass < 2; pass++) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		const int isIrom = (_stricmp(r->type, "irom") == 0);
		const int isVoice = (_stricmp(r->type, "voice") == 0 || _stricmp(r->type, "vdata") == 0);
		const int isCode = (_stricmp(r->type, "code") == 0);
		const int isProg = (_stricmp(r->type, "prog") == 0);
		const int isBgm = (_stricmp(r->type, "bgm") == 0);
		if (pass == 0 && !isIrom && !isVoice) continue;
		if (pass == 1 && (isIrom || isVoice)) continue;

		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		if (isIrom || isCode) {
			int off = r->offset;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			/* INITIATE.ROM is underlay only — never clobber PATCH/DRIVER. */
			if (isIrom && pass == 0) {
				memcpy(mem_ + off, data, n);
				continue;
			}
			if (isCode) {
				memcpy(mem_ + off, data, n);
				loadedCode++;
				const unsigned end = (unsigned)off + n;
				if (end > codeHighWater_)
					codeHighWater_ = (uint16_t)(end > 0xffffu ? 0xffffu : end);
				if (nCode < 24) {
					codeLo[nCode] = (unsigned)off;
					codeHi[nCode] = end;
					nCode++;
				}
			}
		} else if (isBgm) {
			int idx = r->offset;
			if (idx < 0 || idx >= 128) continue;
			unsigned n = sz;
			/* Empty zip members (asteka2 TTLPRG) must not look "present" —
			   TriggerPlay would StageBgm them and wipe mdata. */
			if (n == 0) continue;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (bgmBank_[idx]) free(bgmBank_[idx]);
			bgmBank_[idx] = buf;
			bgmBankSize_[idx] = n;
			bgmPresent_[idx] = 1;
		} else if (isProg) {
			int idx = r->offset;
			if (idx < 0 || idx >= PROG_BANKS) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (progBank_[idx]) free(progBank_[idx]);
			progBank_[idx] = buf;
			progBankSize_[idx] = n;
			progPresent_[idx] = 1;
			falcomMode_ = 1;
		} else if (isVoice) {
			int idx = r->offset;
			if (idx < 0 || idx >= 128) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (voiceBank_[idx]) free(voiceBank_[idx]);
			voiceBank_[idx] = buf;
			voiceBankSize_[idx] = n;
			voicePresent_[idx] = 1;
			/* Selected-track underlay at vdata (Falcom). */
			uint8_t song = 0, bank = 0;
			UnpackTitle(titleCode, &song, &bank);
			if (idx == (int)song || idx == (int)bank || idx == (int)(titleCode & 0xff)) {
				if (vdataAddr_ >= 0) {
					unsigned vn = n;
					if (vdataSize_ > 0 && (unsigned)vdataSize_ < vn)
						vn = (unsigned)vdataSize_;
					if (vdataAddr_ + (int)vn > 0x10000)
						vn = (unsigned)(0x10000 - vdataAddr_);
					if (vn > 0)
						memcpy(mem_ + vdataAddr_, buf, vn);
				}
			}
		}
	}
	}

	if (!loadedCode) return 0;

	{
		int hasInit = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "init_pc") == 0) { hasInit = 1; break; }
		}
		if (hasInit) {
			initPc_ = (uint16_t)CEmuParseOptHex(ge, "init_pc", 0);
		} else if (initPc_ == 0) {
			int patchOff = -1, firstCode = -1;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (firstCode < 0) firstCode = r->offset;
				if (_strnicmp(r->name, "PATCH", 5) == 0
					|| _strnicmp(r->name, "XA1", 3) == 0
					|| _strnicmp(r->name, "XA2", 3) == 0)
					patchOff = r->offset;
			}
			if (patchOff >= 0)
				initPc_ = (uint16_t)patchOff;
			else if (firstCode >= 0)
				initPc_ = (uint16_t)firstCode;
			else
				initPc_ = 0;
		}
		/* Re-read mdata opts after falcom defaults (already set above). */
		if (!falcomMode_) {
			mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", 0x3000);
			int ms = CEmuParseOptHex(ge, "mdata_size", 0x1000);
			int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
			if (mfs > ms) ms = mfs;
			if (ms <= 0 || ms > BGM_SIZE) ms = 0x1000;
			mdataSize_ = (unsigned)ms;
		} else {
			int ma = CEmuParseOptHex(ge, "mdata_addr", -1);
			if (ma >= 0) mdataAddr_ = (uint16_t)ma;
		}
	}

	/* Reset vector → init_pc, then mc6809_reset (reads FFFE/FFFF). */
	/* Falcom Ys PSG: PATCH@FED0 is an 8-byte-per-prog table; code follows.
	   Booting at FED0 executes table bytes (stuck ~FED5, irqPulses=0). */
	patchTableBase_ = initPc_;
	if (falcomMode_ && HasProgBanks() && initPc_ >= 0xE000) {
		const unsigned base = initPc_;
		int n = 0;
		if (base + 16u <= 0x10000u) {
			const uint8_t n0 = mem_[base + 8];
			const uint8_t n1 = mem_[base + 9];
			if ((n0 == 0x08 || n0 == 0x10) && n1 != 0 && n1 < 0x80)
				n = 1; /* entry0 = stop/pad; real rows start at +8 */
		}
		while (n < 12 && base + (unsigned)n * 8u + 8u <= 0x10000u) {
			const uint8_t t0 = mem_[base + (unsigned)n * 8u];
			const uint8_t t1 = mem_[base + (unsigned)n * 8u + 1u];
			if (!((t0 == 0x08 || t0 == 0x10) && t1 != 0 && t1 < 0x80))
				break;
			n++;
		}
		if (n >= 2) {
			patchTableBase_ = (uint16_t)base;
			initPc_ = (uint16_t)(base + (unsigned)n * 8u);
		}
	}
	/* Xanadu AV PATCH begins with a word table, not 08/10 Ys rows.
	   Booting at FEE0/FEF0 executes those bytes (pc stuck, I masked,
	   $FD80 never polled). Skip to the live LDS #$FC80. */
	if (falcomMode_ && initPc_ >= 0xE000) {
		unsigned found = 0;
		const unsigned lim = (unsigned)initPc_ + 80u;
		for (unsigned a = initPc_; a + 4u <= 0x10000u && a < lim; a++) {
			if (mem_[a] == 0x10 && mem_[a + 1] == 0xCE
				&& mem_[a + 2] == 0xFC && mem_[a + 3] == 0x80) {
				found = a;
				break;
			}
		}
		if (found && found != (unsigned)initPc_)
			initPc_ = (uint16_t)found;
	}
	mem_[0xFFFE] = (uint8_t)(initPc_ >> 8);
	mem_[0xFFFF] = (uint8_t)(initPc_ & 0xff);
	BindCpuCallbacks();
	mc6809_reset(&cpu_);
	cpuCycles_ = 0;
	if (chipOpn_) chipOpn_->Reset();
	if (chipAy_) chipAy_->Reset();
	/* ishtar MUSIC.P IRQ $517F ends in JMP $518F (no RTI) — freeze on first tick. */
	if (mem_[0x518F] == 0x7E && mem_[0x5190] == 0x51 && mem_[0x5191] == 0x8F
		&& mem_[0x517F] == 0xB6 && mem_[0x5180] == 0xFD && mem_[0x5181] == 0x03) {
		mem_[0x518F] = 0x3B;
		mem_[0x5190] = 0x12;
		mem_[0x5191] = 0x12;
	}
	/* T&E voice bank: copy INITIATE $0C00 → $6C00 before OP.BIN/MUS stage. */
	SeedTandeFmVoices();
	/* Pre-stage bank 0 (or the title's bank) when the mdata window does not
	   swallow other code. albatrss JSR $F000 needs MUS at $2000 during boot;
	   relics $0C00+$E000 is excluded by the size cap. */
	if (!falcomMode_ && mdataSize_ > 0 && mdataSize_ <= 0x4000u) {
		const unsigned m0 = mdataAddr_;
		unsigned m1 = mdataAddr_ + mdataSize_;
		if (m1 > 0x10000u) m1 = 0x10000u;
		int overlap = 0;
		for (int i = 0; i < nCode; i++) {
			const unsigned s = codeLo[i] > m0 ? codeLo[i] : m0;
			const unsigned e = codeHi[i] < m1 ? codeHi[i] : m1;
			if (e > s && (e - s) > 0x1000u)
				overlap = 1;
		}
		if (!overlap) {
			uint8_t song = 0, bank = 0;
			uint8_t st = 0xFF;
			if (IsAsteka2Patch()) {
				/* Exact FD59 file only — do not fall back to APRG when
				   TTLPRG is missing (that copies APRG into $0100). */
				const uint8_t file = (uint8_t)(titleCode & 0xff);
				if (file < 128 && bgmPresent_[file] && bgmBankSize_[file] > 0)
					st = file;
			} else if (IsSharrierPatch()) {
				const uint8_t file = (uint8_t)((titleCode >> 8) & 0xff);
				if (file < 128 && bgmPresent_[file] && bgmBankSize_[file] > 0)
					st = file;
			} else {
				UnpackTitle(titleCode, &song, &bank);
				if (song < 128 && bgmPresent_[song])
					st = song;
				else if (bank < 128 && bgmPresent_[bank])
					st = bank;
				else if (bgmPresent_[0])
					st = 0;
			}
			if (st < 128)
				StageBgm(st);
		}
	}
	/* albatrss PATCH: LDX #$2000 / JSR $F000 / JSR $F002.
	   Native $F000 spends the 1s boot in the $F48F write helper and never
	   plants F069 channel BSS. NOP both boot JSRs; FinishAlbatrssPlay
	   host-calls $F000 after the song is staged. */
	{
		const unsigned lim = (unsigned)initPc_ + 80u;
		for (unsigned a = initPc_; a + 8u < 0x10000u && a < lim; a++) {
			if (mem_[a] == 0x8E && mem_[a + 3] == 0xBD
				&& mem_[a + 4] == 0xF0 && mem_[a + 5] == 0x00
				&& mem_[a + 6] == 0xBD && mem_[a + 7] == 0xF0
				&& mem_[a + 8] == 0x02) {
				mem_[a + 3] = mem_[a + 4] = mem_[a + 5] = 0x12;
				mem_[a + 6] = mem_[a + 7] = mem_[a + 8] = 0x12;
				albatrssMode_ = 1;
				for (unsigned p = initPc_; p + 3u < 0x10000u && p < initPc_ + 96u; p++) {
					if (mem_[p] == 0xB6 && mem_[p + 1] == 0xFD && mem_[p + 2] == 0x58) {
						albatrssPoll_ = (uint16_t)p;
						break;
					}
				}
				break;
			}
		}
	}
	MmrInit();
	return 1;
}

void CEmuHardFm7SetActive(CHardFm7* hw)
{
	s_activeFm7 = hw;
}

CHardFm7* CEmuHardFm7GetActive()
{
	return s_activeFm7;
}
