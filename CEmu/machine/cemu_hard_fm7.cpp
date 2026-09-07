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
{
	hardKind = KIND_FM7;
	memset(mem_, 0, sizeof(mem_));
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
	/* Prefer full bank when catalog mdata_size is only a header window,
	   but never expand into the tail of a code image that overlaps the
	   mdata window (laydock: DRIVER ends at $42B8, mdata is $4000+$200 —
	   the DRIVER tail holds FM voice tables; copying 8K MUS wiped them). */
	unsigned cap = mdataSize_;
	if (n > cap && mdataAddr_ + n <= 0x10000u
		&& (initPc_ < mdataAddr_ || initPc_ >= mdataAddr_ + n)) {
		const unsigned winEnd = mdataAddr_ + cap;
		if (!(codeHighWater_ > winEnd && codeHighWater_ > mdataAddr_))
			cap = n;
	}
	if (n > cap) n = cap;
	if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
	if (mdataAddr_ + n > 0x10000) return;
	unsigned clearN = cap;
	if (mdataAddr_ + clearN > 0x10000)
		clearN = 0x10000u - mdataAddr_;
	if (clearN)
		memset(mem_ + mdataAddr_, 0, clearN);
	memcpy(mem_ + mdataAddr_, bgmBank_[use], n);
	/* Ys MANPR also peeks 0x4D00 — mirror when primary window is 0x5C00.
	   Do NOT stage at 0x3000 here: YMUS 30xx headers are post-reloc addrs;
	   overlaying $3000 would clobber MANPR (loader JSR $317A etc.). */
	if (falcomMode_ && mdataAddr_ == 0x5c00 && n > 0 && 0x4d00 + n <= 0x5c00)
		memcpy(mem_ + 0x4d00, bgmBank_[use], n);
}

void CHardFm7::StageProg(uint8_t index)
{
	/* Exact prog bank only — no low-nibble / first-present guess. */
	uint8_t use = index;
	if (use >= PROG_BANKS || !progPresent_[use] || !progBank_[use])
		return;
	unsigned n = progBankSize_[use];
	/* Ys FM-7's PATCH table gives each program's load page in t0:
	   TTLPRG uses $1000 while MANPR/ENDPRG use $0800. */
	unsigned loadBase = 0;
	if (!useOpn_ && patchTableBase_ == 0xFED0 && use >= 1) {
		const unsigned ent = (unsigned)patchTableBase_ + (unsigned)(use - 1) * 8u;
		const uint8_t page = (ent < 0x10000u) ? mem_[ent] : 0;
		loadBase = (page == 0x08 || page == 0x10)
			? ((unsigned)page << 8) : 0x0800u;
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
	case 0x04: /* status read → data latch */
		*dataLatch = chip->ReadStatus();
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
	if (addr == FM7_FD_IRQEN)
		return fd02_;
	if (addr == FM7_FD_IRQST) {
		/* Reading $FD03 clears pending bits so laydock bit2 cannot IRQ-storm. */
		uint8_t v = fd03_;
		fd03_ = (uint8_t)(fd03_ & (uint8_t)~0x0F);
		return v;
	}
	/* FM-7 FDC status used by the resident BIOS helper in Ys MANPR.
	   Bit 6 is the transfer-ready handshake; a permanently zero open-bus
	   value traps initialization in its $0992 BITA/BNE wait loop. */
	if (!useOpn_ && patchTableBase_ == 0xFED0 && addr == 0xFD1F)
		return (uint8_t)(mem_[addr] | 0x40);
	/* The FDC command port reads back controller status, not the last
	   command byte written.  Report no error (bit 4 clear) for the staged
	   in-memory transfer path. */
	if (!useOpn_ && patchTableBase_ == 0xFED0 && addr == 0xFD18)
		return 0;
	if (addr == FM7_FD_SUBINTF)
		return (uint8_t)(fd05_ | (fd05HaltSticky_ ? 0x80 : 0));
	/* OPN bus: FD16=data latch (after cmd 1/4), FD15 unused for read. */
	if (useOpn_ && chipOpn_) {
		if (addr == FM7_FD_OPN_DATA)
			return opnDataLatch_;
		if (addr == FM7_FD_OPN_ADDR)
			return chipOpn_->ReadStatus();
	}
	if (!useOpn_ && chipAy_) {
		if (addr == FM7_FD_PSG_DATA)
			return psgDataLatch_;
		if (addr == FM7_FD_PSG_ADDR)
			return 0xff;
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
		if (addr == FM7_FD_OPN_DATA) {
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
	}
	if (!useOpn_ && chipAy_) {
		if (addr == FM7_FD_PSG_DATA) {
			psgDataLatch_ = data;
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
		if (!looksFd03(hwIrq)) {
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
	/* asteka2 / sharrier-style: BITA #1 / BEQ skip — pulse bit0 on vsync. */
	if (hasBit0Beq && !hasBit0Bne) {
		fd03VsyncSet_ = 0x01;
		fd03VsyncClr_ = 0x08;
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
		   Low nibble is phrase index within that bank. */
		uint8_t track = (uint8_t)((titleCode >> 4) & 0xffu);
		if (!track) track = b0;
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
				if (t0 == 0x08 || t0 == 0x10) {
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
		/* Ys AV (prog banks) loader LDA $FD85 / STA onto patchAddr mailbox. */
		if (HasProgBanks()) {
			/* Song/param at $FD85 (PATCH LDA 5,X with X=$FD80). */
			/* FD85 is the phrase selector within the staged BGM bank. */
			uint8_t param = (uint8_t)(titleCode & 0x0fu);
			mem_[0xFD85] = param;
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
			if (callLooksCode) {
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
		return;
	}

	/* Bank file vs song-in-bank:
	   - high byte (b1) selects a bgm bank when present (sharrier 0x0100)
	   - low byte (b0) is a bank index when that bank exists (ishtar 0x0007)
	   - else low byte is a song index into primary bank 0 (asteka2 0x0002/APRG) */
	auto bankOk = [&](unsigned i) -> int {
		return (i < 128 && bgmPresent_[i] && bgmBank_[i] && bgmBankSize_[i] > 0) ? 1 : 0;
	};
	uint8_t stage = 0xff;
	if (b1 && bankOk(b1))
		stage = (uint8_t)b1;
	else if (b0 && bankOk(b0))
		stage = (uint8_t)b0;
	else if (b0 && bankOk(0))
		stage = 0;
	else if (bankOk(bank))
		stage = bank;
	else if (bankOk(song))
		stage = song;

	if (stage < 128 && bankOk(stage))
		StageBgm(stage);

	playSongLatch_ = b0;
	playParamA_ = b1;
	playParamB_ = b2;
	playParamC_ = b3;
	playCmdLatch_ = 0x01;
	/* jikochu: IRQ/main can poll $FD58 many times before the play path;
	   hold=8 cleared the cmd and left boot JSR $C000 as the only song. */
	playCmdHold_ = (!useOpn_ && mdataAddr_ == 0xC000) ? 256 : 8;
	mem_[FM7_FD_PLAY_SONG] = playSongLatch_;
	mem_[FM7_FD_PLAY_A] = playParamA_;
	mem_[FM7_FD_PLAY_B] = playParamB_;
	mem_[FM7_FD_PLAY_C] = playParamC_;
	mem_[FM7_FD_PLAY_CMD] = 0x01;

	/* jikochu_fm7: OPEN1 PSG writes are gated by TST $0614 / BPL; PATCH clears
	   $0614 after play setup so IRQ ticks never reach FD0D/FD0E. Keep bit7. */
	if (!useOpn_ && chipAy_ && mdataAddr_ == 0xC000
		&& mem_[0xC19D] == 0x7D && mem_[0xC19E] == 0x06 && mem_[0xC19F] == 0x14)
		mem_[0x0614] = 0x80;
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

void CHardFm7::RunSubroutine(uint16_t addr)
{
	if (addr == 0 || addr == 0xFFFF) return;
	BindCpuCallbacks();
	const uint16_t retPc = cpu_.pc.w;
	const uint16_t sp0 = cpu_.index[3].w;
	cpu_.index[3].w = (uint16_t)(sp0 - 2);
	mem_[cpu_.index[3].w] = (uint8_t)(retPc >> 8);
	mem_[(uint16_t)(cpu_.index[3].w + 1)] = (uint8_t)(retPc & 0xff);
	cpu_.pc.w = addr;
	int guard = 0;
	while (guard++ < 200000) {
		const uint16_t before = cpu_.pc.w;
		if (mc6809_step(&cpu_) != 0)
			break;
		if (before != retPc && cpu_.pc.w == retPc)
			break;
	}
	cpu_.index[3].w = sp0;
	/* This is a host-side synchronous call.  A missing BIOS helper can make
	   a ripped program miss its RTS; never leave the foreground CPU running
	   through the stack/RAM after the bounded call returns to the host. */
	cpu_.pc.w = retPc;
}

int CHardFm7::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	titleCode_ = titleCode;
	memset(mem_, 0, sizeof(mem_));
	FreeBanks();
	codeHighWater_ = 0;
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
	mem_[0xFFFE] = (uint8_t)(initPc_ >> 8);
	mem_[0xFFFF] = (uint8_t)(initPc_ & 0xff);
	BindCpuCallbacks();
	mc6809_reset(&cpu_);
	cpuCycles_ = 0;
	if (chipOpn_) chipOpn_->Reset();
	if (chipAy_) chipAy_->Reset();
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
