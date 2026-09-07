#include "StdAfx.h"
#include "cemu_hard_x1.h"
#include "../chip/cemu_chip_opm.h"
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

CHardX1::CHardX1()
	: cpuHz_(X1_CPU_HZ)
	, opmHz_(X1_OPM_HZ)
	, ayHz_(X1_AY_HZ)
	, psgOnly_(0)
	, initPc_(0xC000)
	, mdataAddr_(0x4000)
	, mdataSize_((unsigned)BGM_SIZE)
	, titleCode_(0)
	, playCmdLatch_(0)
	, playSongLatch_(0)
	, playCmdHoldIrqs_(0)
	, ydosCmdSeen_(0)
	, ydosInhibitReentry_(0)
	, ydosRom_(0)
	, cpu_(NULL)
	, chipOpm_(NULL)
	, chipAy_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, stageLimit_(0x10000)
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
	/* init_pc / mdata_* finalized in LoadRoms after roms+options are known. */
	initPc_ = 0xC000;
	mdataAddr_ = 0x4000;
	mdataSize_ = (unsigned)BGM_SIZE;
	if (!psgOnly_)
		chipOpm_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
	chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
	cpu_ = new Ay_Cpu();
	return (cpu_ && chipAy_ && (psgOnly_ || chipOpm_)) ? 1 : 0;
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
	/* OUT 0 path: stage into IO window @5000 (hoot mucomx1 uses 8K window). */
	unsigned ioN = n;
	if (ioN > 0x2000u) ioN = 0x2000u;
	memcpy(ioport_ + 0x5000, bgmBank_[use], ioN);
	if (ioN < 0x2000u)
		memset(ioport_ + 0x5000 + ioN, 0, 0x2000u - ioN);
	/* Mirror into RAM at mdata_addr (per-title option).
	   Never memset the full mdataSize_ window: sphari mdata@A000 + default
	   32K wiped OPMDRV@E000 (opmW=0); hayato mdata@C000 + 32K wiped top RAM.
	   Clear only the staged bytes + a short pad (same idea as YDOS). */
	if (mdataAddr_ + n <= 0x10000) {
		memcpy(mem_ + mdataAddr_, bgmBank_[use], n);
		unsigned pad = 0x100;
		if (n + pad > mdataSize_) pad = (mdataSize_ > n) ? (mdataSize_ - n) : 0;
		if (pad && mdataAddr_ + n + pad <= 0x10000)
			memset(mem_ + mdataAddr_ + n, 0, pad);
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
	   table. Mirror the song latch (hoot music id base 2). */
	if (p == 0x000f)
		return playSongLatch_;
	if (!psgOnly_ && chipOpm_ && (p == 0x0700 || p == 0x0701))
		return chipOpm_->ReadStatus();
	if (p == 0x1a01)
		return 0x04; /* bit2 ready — Laplace ISR / sub-CPU style polls */
	/* Microcabin msnk: busy-wait IN A,(0FF8); AND 81; JP NZ — clear = ready. */
	if (p == 0x0ff8 || p == 0x0ff9 || p == 0x0ffc)
		return 0x00;
	if (p == 0x1c00 || p == 0x1b00) {
		if (chipAy_) return chipAy_->ReadData();
		return 0xff;
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
	if (!psgOnly_ && chipOpm_ && (p == 0x0700 || p == 0x0701)) {
		chipOpm_->Write((uint32_t)(p & 1), data);
		return;
	}
	/* CZ-8BS1 carries its own Z80 CTC at 0704-0707 next to the OPM. */
	if (p >= 0x0704 && p <= 0x0707) {
		X1CtcTrace(this, p, data);
		ioport_[p] = data;
		return;
	}
	if (p == 0x1b00) {
		/* hoot passes port>>8 to ssAY8910: 1B is odd, therefore data. */
		if (chipAy_) chipAy_->Write(1, data);
		ioport_[p] = data;
		return;
	}
	if (p == 0x1c00) {
		/* 1C is even, therefore the AY address latch. */
		if (chipAy_) chipAy_->Write(0, data);
		ioport_[p] = data;
		return;
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

void CHardX1::UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut)
{
	/* hoot X1/NCS: 0xSS0000BB → song=SS bank=BB; 0xSS000000 → song=SS bank=0;
	   low-only 0x000000NN → song=NN bank=NN (legacy StageBgm(song)).
	   Extended mid!=0 codes (ishtar 0x00028408) keep lo as song/bank.
	   Humming Bird Laplace uses hi as a flags byte (0x68..0xC0) with the
	   real song/bank in lo — treating hi as song yielded song=0x80 and silent
	   DRIVER calls. */
	const unsigned lo = titleCode & 0xffu;
	const unsigned hi = (titleCode >> 24) & 0xffu;
	const unsigned mid = (titleCode >> 8) & 0xffffu;
	uint8_t song = 0, bank = 0;
	if (mid == 0 && hi >= 0x40) {
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
	UnpackTitle(titleCode, &song, &bank);
	uint8_t stage = bank;
	if (!(stage < 128 && bgmPresent_[stage] && bgmBank_[stage])) {
		if (song < 128 && bgmPresent_[song] && bgmBank_[song])
			stage = song;
		else
			stage = 0xff;
	}
	if (stage < 128 && bgmPresent_[stage] && bgmBank_[stage]) {
		if (mdataAddr_ == 0x4000 || mdataAddr_ == 0) {
			unsigned n = bgmBankSize_[stage];
			if (n > mdataSize_) n = mdataSize_;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			memset(mem_ + 0x4000, 0, n);
			memcpy(mem_ + 0x4000, bgmBank_[stage], n);
		}
		StageBgm(stage);
	}
	/* Keep play mailbox idle through DRIVER boot. */
	playCmdLatch_ = 0;
	playSongLatch_ = 0;
	playCmdHoldIrqs_ = 0;
	ydosCmdSeen_ = 0;
	ydosInhibitReentry_ = 0;
	mem_[PLAY_FLAG] = 0;
	mem_[PLAY_CODE] = 0;
}

void CHardX1::TriggerPlay(unsigned titleCode)
{
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank);
	/* Dual mailbox: port latch (Falcom) + C010/C011 when free (hoot Play).
	   Stage IO@5000 always; RAM mirror only when StageBgm deems safe. */
	playCmdLatch_ = 0x01;
	/* Falcom xana2: fixed family hi=0x02, track id in lo (port0F / CP 1Ah). */
	{
		const unsigned lo = titleCode & 0xffu;
		const unsigned hi = (titleCode >> 24) & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (mid == 0 && hi == 2)
			playSongLatch_ = lo ? (uint8_t)lo : (uint8_t)hi;
		else
			playSongLatch_ = song;
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
		if (mdataAddr_ == 0x4000 || mdataAddr_ == 0) {
			unsigned n = bgmBankSize_[stage];
			if (n > mdataSize_) n = mdataSize_;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			memset(mem_ + 0x4000, 0, n);
			memcpy(mem_ + 0x4000, bgmBank_[stage], n);
		}
		StageBgm(stage);
	}
}

unsigned CHardX1::OpmWrites() const
{
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
			   voice window starts at the same offset. */
			if ((int)mdataAddr_ > off) {
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
			UnpackTitle(titleCode, &song, &bank);
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
		/* Nearest code blob above mdata_addr caps StageBgm writes. */
		stageLimit_ = 0x10000;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0) continue;
			if (r->offset > (int)mdataAddr_ && r->offset < (int)stageLimit_)
				stageLimit_ = (uint16_t)r->offset;
		}
	}

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpuCycles_ = 0;
	if (chipOpm_) chipOpm_->Reset();
	if (chipAy_) chipAy_->Reset();
	/* Do NOT poke C010-C012 here — reserved for Play()/TriggerPlay. */
	return 1;
}

void CEmuHardX1SetActive(CHardX1* hw)
{
	CEmuZ80BusSetActive(hw);
}
