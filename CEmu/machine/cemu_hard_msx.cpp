#include "StdAfx.h"
#include "cemu_hard_msx.h"
#include "../cemu_mgr.h"
#include "../cemu_zipfs.h"
#include "../chip/cemu_chip_ay.h"
#include "../chip/cemu_chip_scc.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../chip/cemu_chip_opl.h"
#include "../fmmon/fmmon_shadow.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include "../s98/device/emu2413/emu2413.h"
#include <string.h>
#include <stdlib.h>

enum {
	MSX_CPU_HZ = 3579545,
	MSX_AY_HZ = 3579545 / 2,
	MSX_OPLL_HZ = 3579545
};

/* hoot kss.cpp IPL */
static const uint8_t kKssIpl[] = {
	0xd7,0xd3,0xa0,0xf5,0x7b,0xd3,0xa1,0xf1,0xc9,0xd3,0xa0,0xdb,0xa2,0xc9,0xff,0xff,
	0xed,0x56,0x31,0x80,0xf3,0xf3,0xdb,0x00,0xcd,0x00,0x00,0xfb,0xdb,0x01,0x18,0xfb,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xf3,0xcd,0x00,0x00,0xfb,0xc9,
};

static uint16_t Rd16(const uint8_t* p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* hoot ds4.cpp IPL — IM2, map banks 2/3, CALL $48F2, poll play/skip. */
static const uint8_t kDs4Ipl[] = {
	0xed,0x56,0x31,0x80,0xf3,0x3e,0x02,0x32,0x00,0x70,0x3c,0x32,0x00,0x78,0xcd,0xf2,
	0x48,0xdb,0x00,0xb7,0x20,0x04,0xdb,0x02,0x18,0xf7,0xf3,0x3e,0x02,0x32,0x00,0x70,
	0x3c,0x32,0x00,0x78,0xdb,0x01,0x32,0x94,0xc0,0xcd,0x9d,0x72,0xfb,0x18,0xe2,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xf3,0xf5,0xc5,0xd5,0xe5,0xdd,0xe5,0xfd,
	0xe5,0x3e,0x02,0x32,0x00,0x70,0x3c,0x32,0x00,0x78,0xcd,0x36,0x72,0xfd,0xe1,0xdd,
	0xe1,0xe1,0xd1,0xc1,0xf1,0xfb,0xc9,
};

static void PlantPsgTrampoline(uint8_t* mem)
{
	if (!mem) return;
	if (mem[0x0090] == 0x00)
		mem[0x0090] = 0xC9; /* GICINI */
	mem[0x0093] = 0xC3; mem[0x0094] = 0xC0; mem[0x0095] = 0x00; /* JP 00C0 */
	mem[0x0096] = 0xC3; mem[0x0097] = 0xD0; mem[0x0098] = 0x00; /* JP 00D0 */
	mem[0x00C0] = 0xD3; mem[0x00C1] = 0xA0;
	mem[0x00C2] = 0x7B;
	mem[0x00C3] = 0xD3; mem[0x00C4] = 0xA1;
	mem[0x00C5] = 0xC9;
	mem[0x00D0] = 0xD3; mem[0x00D1] = 0xA0;
	mem[0x00D2] = 0xDB; mem[0x00D3] = 0xA2;
	mem[0x00D4] = 0xC9;
}

/* archive="game,fmpac_msx": FMPAC.ROM lives in the companion zip. */
static void CEmuMsxMergeCompanions(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge || !ge->archive[0] || !strchr(ge->archive, ','))
		return;
	CEmuMgr* mgr = CEmuMgrGet();
	if (!mgr || !mgr->dataRoot[0]) return;

	char buf[CEMU_ARCHIVE_NAME];
	strncpy_s(buf, ge->archive, _TRUNCATE);
	char* ctx = NULL;
	char* tok = strtok_s(buf, ",", &ctx);
	int first = 1;
	while (tok) {
		while (*tok == ' ' || *tok == '\t') tok++;
		char* end = tok + strlen(tok);
		while (end > tok && (end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;
		if (!first && tok[0]) {
			wchar_t path[MAX_PATH];
			const char* dir = ge->dataDir[0] ? ge->dataDir : "msx";
			_snwprintf_s(path, _TRUNCATE, L"%s\\%hs\\%hs.zip", mgr->dataRoot, dir, tok);
			if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
				_snwprintf_s(path, _TRUNCATE, L"%s\\msx\\%hs.zip", mgr->dataRoot, tok);
			if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
				CEmuZipFsMergeZip(fs, path);
		}
		first = 0;
		tok = strtok_s(NULL, ",", &ctx);
	}
}

static const unsigned char* FindMsxCodeRom(CEmuZipFs* fs, const char* name, unsigned* sz)
{
	if (sz) *sz = 0;
	if (!fs || !name || !name[0]) return NULL;
	const unsigned char* data = CEmuZipFsFind(fs, name, sz);
	if (data && sz && *sz) return data;
	/* dssp1 lists ran/BSRAND.OBJ but the zip ships ran/DRIVER.BIN. */
	const char* slash = strrchr(name, '/');
	if (!slash) slash = strrchr(name, '\\');
	if (!slash) {
		data = CEmuZipFsFind(fs, "DRIVER.BIN", sz);
		if (data && sz && *sz) return data;
		return NULL;
	}
	char alt[CEMU_ROM_NAME];
	const int n = (int)(slash - name + 1);
	if (n <= 0 || n >= (int)sizeof(alt) - 12) return NULL;
	memcpy(alt, name, (size_t)n);
	strcpy_s(alt + n, sizeof(alt) - (size_t)n, "DRIVER.BIN");
	data = CEmuZipFsFind(fs, alt, sz);
	if (data && sz && *sz) return data;
	strcpy_s(alt + n, sizeof(alt) - (size_t)n, "DRIVER");
	return CEmuZipFsFind(fs, alt, sz);
}

static int IsMsxPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "msx") == 0) return 1;
	if (_stricmp(ge->dataDir, "msx") == 0) return 1;
	if (_stricmp(ge->subtype, "kss") == 0 || _stricmp(ge->subtype, "opll") == 0) return 1;
	if (_stricmp(ge->subtype, "generic") == 0 && _stricmp(ge->platform, "msx") == 0) return 1;
	if (_stricmp(ge->subtype, "ds4") == 0 || _stricmp(ge->subtype, "ascii16") == 0) return 1;
	if (_stricmp(ge->subtype, "dq1") == 0 || _stricmp(ge->subtype, "dq2") == 0) return 1;
	return 0;
}

int CHardMsx::ParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal)
{
	if (!ge || !name) return defVal;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) != 0) continue;
		const char* s = ge->opt[i].value;
		if (!s || !s[0]) return defVal;
		return (int)strtoul(s, NULL, 0);
	}
	return defVal;
}

CHardMsx::CHardMsx()
	: cpuHz_(MSX_CPU_HZ)
	, ayHz_(MSX_AY_HZ)
	, opllHz_(MSX_OPLL_HZ)
	, chips_(0)
	, playing_(0)
	, bank_(NULL)
	, bankBytes_(0)
	, cpu_(NULL)
	, chipAy_(NULL)
	, chipScc_(NULL)
	, chipSng_(NULL)
	, chipOpl_(NULL)
	, chipOpll_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, idle_(0)
	, loadAdr_(0)
	, loadSize_(0)
	, initAdr_(0)
	, intAdr_(0)
	, bankOfs_(0)
	, bankNum_(0)
	, bank8k_(0)
	, sccEnable_(0)
	, sccMapped_(0)
	, sccAccessed_(0)
	, ayWriteCount_(0)
	, opllWriteCount_(0)
	, opllLatch_(0)
	, genericMode_(0)
	, initPc_(0x400)
	, mdataAddr_(0xA400)
	, mdataSize_(0x800)
	, titleCode_(0)
	, ge_(NULL)
	, playCmdPending_(0)
	, mapper_(MAP_NONE)
	, cart_(NULL)
	, cartBytes_(0)
	, ttlPrgBytes_(0)
	, ttlPrgAddr_(0)
{
	ascii16Bank_[0] = ascii16Bank_[1] = 0;
	ascii8Bank_[0] = ascii8Bank_[1] = ascii8Bank_[2] = ascii8Bank_[3] = 0;
	ds4Bank_[0] = ds4Bank_[1] = 0;
	hardKind = KIND_MSX;
	memset(mem_, 0, sizeof(mem_));
	memset(rom_, 0, sizeof(rom_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
	memset(ttlPrg_, 0, sizeof(ttlPrg_));
}

CHardMsx::~CHardMsx()
{
	Shutdown();
}

int CHardMsx::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsMsxPlatform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = MSX_CPU_HZ;
	ayHz_ = MSX_AY_HZ;
	opllHz_ = MSX_OPLL_HZ;
	chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
	chipScc_ = CEmuChipSccCreate((uint32_t)MSX_CPU_HZ, sampleRate_);
	cpu_ = new Ay_Cpu();
	return (cpu_ && chipAy_) ? 1 : 0;
}

void CHardMsx::FreeBanks()
{
	for (int i = 0; i < BGM_BANKS; i++) {
		if (bgmBank_[i]) {
			free(bgmBank_[i]);
			bgmBank_[i] = NULL;
		}
		bgmBankSize_[i] = 0;
		bgmPresent_[i] = 0;
	}
}

void CHardMsx::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	FreeBanks();
	if (bank_) { free(bank_); bank_ = NULL; bankBytes_ = 0; }
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chipAy_) { CEmuChipAyDestroy(chipAy_); chipAy_ = NULL; }
	if (chipScc_) { CEmuChipSccDestroy(chipScc_); chipScc_ = NULL; }
	if (chipSng_) { CEmuChipSn76489Destroy(chipSng_); chipSng_ = NULL; }
	if (chipOpl_) { CEmuChipYm3812Destroy(chipOpl_); chipOpl_ = NULL; }
	if (chipOpll_) {
		OPLL_delete((OPLL*)chipOpll_);
		chipOpll_ = NULL;
	}
	if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
}

unsigned CHardMsx::AyWrites() const
{
	return chipAy_ ? CEmuChipAyWriteCount(chipAy_) : ayWriteCount_;
}

unsigned CHardMsx::OpllWrites() const
{
	return opllWriteCount_;
}

void CHardMsx::EnsureOpll(int force)
{
	if (chipOpll_) return;
	if (!force && !(chips_ & CHIP_FMPAC)) return;
	OPLL* o = OPLL_new((uint32_t)opllHz_, (uint32_t)sampleRate_);
	if (o) {
		OPLL_set_quality(o, 1);
		OPLL_reset_patch(o, 0);
		chipOpll_ = (void*)o;
		memset(opllRegs_, 0, sizeof(opllRegs_));
	}
}

void CHardMsx::EnsureSng()
{
	if (chipSng_) return;
	chipSng_ = CEmuChipSn76489Create((uint32_t)MSX_CPU_HZ, sampleRate_);
}

void CHardMsx::EnsureMsxAudio()
{
	if (chipOpl_) return;
	if (!(chips_ & CHIP_MSXAUDIO)) return;
	chipOpl_ = CEmuChipYm3812Create((uint32_t)MSX_OPLL_HZ, sampleRate_);
}

void CHardMsx::PlantBiosStubs()
{
	/* Named MSX BIOS entries only — a blanket RET-fill of 0000-03FF
	   smashed undead's OPLLDRV @0100. Each slot is a 3-byte JP/RET
	   that the patch may overwrite during init. */
	static const uint16_t kRet[] = {
		0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030,
		0x0024, /* ENASLT */
		0x001C, /* CALSLT */
		0x0041, 0x0044, 0x0047, 0x004A, 0x004D,
		0x0050, 0x0053, 0x0056, 0x0059, 0x005C,
		0x005F, 0x0062, 0x0066, 0x0069, 0x006C,
		0x0090, /* GICINI — genghis/saziri/tantexr CALL $0090 */
		0x0099, 0x009C, 0x009F, 0x00A2, 0x00A5,
		0x00A8, 0x00AB, 0x00AE, 0x00B1, 0x00B4,
		0x00D5  /* herzog ISR CALL $00D5 after RDPSG RET @00D4; NOP-slide
		           into the $00E0 trampoline nested the vblank. */
		/* Do not plant $0100+ — Compile/Nichibutsu DRIVER.BIN loads @0100
		   (dsdx1/seiha). A RET at RSLREG $0138 smashed those images. */
	};
	for (unsigned i = 0; i < sizeof(kRet) / sizeof(kRet[0]); i++) {
		const uint16_t a = kRet[i];
		if (mem_[a] == 0x00)
			mem_[a] = 0xC9;
	}
	/* RDSLT @000C: byte from HL, ignore slot in A. Bodies @00F0 stay
	   below DRIVER.BIN @0100. Slot0-page1=$FF (and even $4018-$401F)
	   made gokudo PARTIAL→DEAD — TITLE.COM at $4000 uses RDSLT. */
	if (mem_[0x000C] == 0x00) {
		mem_[0x000C] = 0xC3; mem_[0x000D] = 0xF0; mem_[0x000E] = 0x00;
		mem_[0x00F0] = 0x7E; /* LD A,(HL) */
		mem_[0x00F1] = 0xC9;
	}
	/* WRSLT @0014: (HL)=E */
	if (mem_[0x0014] == 0x00) {
		mem_[0x0014] = 0xC3; mem_[0x0015] = 0xF2; mem_[0x0016] = 0x00;
		mem_[0x00F2] = 0x73; /* LD (HL),E */
		mem_[0x00F3] = 0xC9;
	}
	/* CP/M BDOS @0005: ROOT.COM (genghis) CALL 5. XOR A / RET = success.
	   Body at $00B7 sits between BIOS RETs and the PSG trampoline @00C0. */
	if (mem_[0x0005] == 0x00 || mem_[0x0005] == 0xC9) {
		if (mem_[0x00B7] == 0x00 && mem_[0x00B8] == 0x00) {
			mem_[0x0005] = 0xC3; mem_[0x0006] = 0xB7; mem_[0x0007] = 0x00;
			mem_[0x00B7] = 0xAF; /* XOR A */
			mem_[0x00B8] = 0xC9;
		} else if (mem_[0x0005] == 0x00)
			mem_[0x0005] = 0xC9;
	}
}

void CHardMsx::MapAscii16(int page, uint8_t bank)
{
	if (!cart_ || cartBytes_ == 0) return;
	unsigned nBanks = (cartBytes_ + 0x3FFFu) / 0x4000u;
	if (nBanks == 0) nBanks = 1;
	const unsigned b = (unsigned)bank % nBanks;
	const unsigned off = b * 0x4000u;
	const uint16_t dest = page ? 0x8000 : 0x4000;
	unsigned n = 0x4000u;
	if (off >= cartBytes_) return;
	if (off + n > cartBytes_) n = cartBytes_ - off;
	memcpy(mem_ + dest, cart_ + off, n);
	if (n < 0x4000u)
		memset(mem_ + dest + n, 0xFF, 0x4000u - n);
	ascii16Bank_[page & 1] = (uint8_t)b;
}

void CHardMsx::MapAscii8(int page, uint8_t bank)
{
	if (!cart_ || cartBytes_ == 0) return;
	unsigned nBanks = (cartBytes_ + 0x1FFFu) / 0x2000u;
	if (nBanks == 0) nBanks = 1;
	const unsigned b = (unsigned)bank % nBanks;
	const unsigned off = b * 0x2000u;
	const uint16_t dest = (uint16_t)(0x4000u + (unsigned)(page & 3) * 0x2000u);
	unsigned n = 0x2000u;
	if (off >= cartBytes_) return;
	if (off + n > cartBytes_) n = cartBytes_ - off;
	memcpy(mem_ + dest, cart_ + off, n);
	if (n < 0x2000u)
		memset(mem_ + dest + n, 0xFF, 0x2000u - n);
	ascii8Bank_[page & 3] = (uint8_t)b;
}

void CHardMsx::MapDs4(int page, uint8_t bank)
{
	if (!cart_ || cartBytes_ == 0) return;
	unsigned nBanks = (cartBytes_ + 0x1FFFu) / 0x2000u;
	if (nBanks == 0) nBanks = 1;
	const unsigned b = (unsigned)bank % nBanks;
	const unsigned off = b * 0x2000u;
	const uint16_t dest = page ? 0xA000 : 0x8000;
	unsigned n = 0x2000u;
	if (off >= cartBytes_) return;
	if (off + n > cartBytes_) n = cartBytes_ - off;
	memcpy(mem_ + dest, cart_ + off, n);
	ds4Bank_[page & 1] = (uint8_t)b;
}

int CHardMsx::LoadCartRom(CEmuZipFs* fs, const CEmuGameEntry* ge, const char* type)
{
	if (!fs || !ge || !type) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, type) != 0) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, ge->rom[i].name, &sz);
		if (!data || sz < 16) continue;
		if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
		cart_ = (uint8_t*)malloc(sz);
		if (!cart_) return 0;
		memcpy(cart_, data, sz);
		cartBytes_ = sz;
		return 1;
	}
	int best = -1;
	unsigned bestSz = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		if (_stricmp(pathA, "patch") == 0 || _stricmp(pathA, "fmpatch") == 0)
			continue;
		if (fs->files[i].size > bestSz) { bestSz = fs->files[i].size; best = i; }
	}
	if (best < 0 || bestSz < 16) return 0;
	if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
	cart_ = (uint8_t*)malloc(bestSz);
	if (!cart_) return 0;
	memcpy(cart_, fs->files[best].data, bestSz);
	cartBytes_ = bestSz;
	return 1;
}

void CHardMsx::ApplyBank(uint8_t bankSel)
{
	if (!bank_ || bankNum_ == 0) return;
	const int bankno = (int)bankSel - (int)bankOfs_;
	if (bankno < 0 || bankno >= (int)bankNum_)
		return;
	/* hoot kss.cpp port $FE always maps 16K at $8000, even in 8K mode
	   (8K pages are switched from $9000/$B000). */
	const unsigned off = (unsigned)bankno * 0x4000u;
	if (off + 0x4000u <= bankBytes_)
		memcpy(mem_ + 0x8000, bank_ + off, 0x4000);
}

void CHardMsx::MapDefault()
{
}

uint8_t CHardMsx::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (mapper_ == MAP_DS4) {
		if (p == 0x00) {
			const uint8_t ret = ioport_[0];
			ioport_[0] = 0;
			return ret;
		}
		if (p == 0x02) {
			idle_ = 1;
			return 0;
		}
	}
	if (p == SKIP_PORT) {
		idle_ = 1;
		return 0;
	}
	/* BirdySoft/Compile/Enix patches poll IN A,(2) for play. Use a one-shot
	   edge armed after init settle — sticky-high re-triggers and clears PSG. */
	if (genericMode_ && p == 0x02) {
		if (playCmdPending_ > 0) {
			playCmdPending_--;
			return 0x01;
		}
		return 0;
	}
	if (genericMode_ && p == 0x04)
		return (uint8_t)(titleCode_ & 0xff);
	if (p == 0xa0 || p == 0xa1 || p == 0xa2) {
		if (chipAy_) return chipAy_->ReadData();
		return 0xff;
	}
	/* PPI port B (keyboard). Active-low; $00 looks like every key down.
	   TTLPRG's vblank ISR samples row 8 via IN A,($AA)/($A9). */
	if (p == 0xa9)
		return 0xff;
	if ((p == 0xc0 || p == 0xc1) && chipOpl_) {
		/* Y8950 drivers spin on status bit7 (timer IRQ). YM3812's timers
		   are not clocked here; report ready so CALL 6009 can issue FM. */
		return (uint8_t)(chipOpl_->ReadStatus() | 0x80);
	}
	/* MSX2+ system timer at E6/E7. Illusion City DRIVER.BIN spaces OPLL
	   writes with IN A,($E6); SUB C; CP 6; JR C — a constant port left it
	   spinning at $80DE with zero OPLL keys. Bump on each IN: Ay_Cpu::run
	   may issue several INs before cpuCycles_ is updated. */
	if (p == 0xE6)
		return ++ioport_[0xE6];
	if (p == 0xE7)
		return ioport_[0xE7];
	return ioport_[p];
}

void CHardMsx::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (p == 0xc0 || p == 0xc1) {
		if (chips_ & CHIP_MSXAUDIO)
			EnsureMsxAudio();
		if (chipOpl_) {
			chipOpl_->Write((uint32_t)(p & 1), data);
			ioport_[p] = data;
			return;
		}
	}
	if (p == 0x7c || p == 0x7d || p == 0xc0 || p == 0xc1 || p == 0xf0 || p == 0xf1) {
		/* Generic patches often OUT to 7C without a catalog use_opll bit.
		   KSS must not force-create OPLL — a stray poke would leave a drone
		   on top of an AY/SCC tune (sorc_msx NOSEQ regression). */
		if (genericMode_)
			EnsureOpll(1);
		else
			EnsureOpll(0);
		if (chipOpll_) {
			OPLL* o = (OPLL*)chipOpll_;
			if ((p & 1) == 0)
				opllLatch_ = data;
			else {
				OPLL_writeReg(o, opllLatch_, data);
				opllRegs_[opllLatch_ & 0x3f] = data;
				FmMonShadowApplyOpllRegs(opllRegs_);
				opllWriteCount_++;
			}
		}
		ioport_[p] = data;
		return;
	}
	if (p == 0xa0 || p == 0xa1) {
		if (chipAy_) {
			chipAy_->Write((uint32_t)(p & 1), data);
			ayWriteCount_++;
		}
		ioport_[p] = data;
		return;
	}
	if (p == 0x7e || p == 0x7f) {
		EnsureSng();
		if (chipSng_)
			chipSng_->Write(0, data);
		ioport_[p] = data;
		return;
	}
	if (p == 0xfe) {
		ApplyBank(data);
		ioport_[p] = data;
		return;
	}
	/* Generic BirdySoft/Compile patches: OUT (0) may stage BGM index. */
	if (genericMode_ && p == 0x00 && data < BGM_BANKS && bgmPresent_[data])
		StageBgm(data);
	ioport_[p] = data;
}

void CHardMsx::MemWrite(uint16_t addr, uint8_t data)
{
	/* FMPAC / MSX-MUSIC memory-mapped OPLL (same latch/data as ports 7C/7D).
	   Generic cartridge patches only — KSS drivers talk OPLL via 7C/7D, and
	   treating 7FF4/5 as OPLL on KSS steals ordinary RAM writes (sorc NOSEQ). */
	if ((genericMode_ || mapper_ != MAP_NONE) && (addr == 0x7ff4 || addr == 0x7ff5)) {
		EnsureOpll(1);
		if (chipOpll_) {
			OPLL* o = (OPLL*)chipOpll_;
			if (addr == 0x7ff4)
				opllLatch_ = data;
			else {
				OPLL_writeReg(o, opllLatch_, data);
				opllRegs_[opllLatch_ & 0x3f] = data;
				FmMonShadowApplyOpllRegs(opllRegs_);
				opllWriteCount_++;
			}
		}
		return;
	}

	if (mapper_ == MAP_ASCII16) {
		if (addr >= 0x6000 && addr < 0x7000) { MapAscii16(0, data); return; }
		if (addr >= 0x7000 && addr < 0x8000) { MapAscii16(1, data); return; }
		if (addr >= 0x4000 && addr < 0xC000) return;
	} else if (mapper_ == MAP_ASCII8) {
		if (addr >= 0x6000 && addr < 0x6800) { MapAscii8(0, data); return; }
		if (addr >= 0x6800 && addr < 0x7000) { MapAscii8(1, data); return; }
		if (addr >= 0x7000 && addr < 0x7800) { MapAscii8(2, data); return; }
		if (addr >= 0x7800 && addr < 0x8000) { MapAscii8(3, data); return; }
		if (addr >= 0x4000 && addr < 0xC000) return;
	} else if (mapper_ == MAP_DS4) {
		if (addr == 0x7000) { MapDs4(0, data); return; }
		if (addr == 0x7800) { MapDs4(1, data); return; }
		if (addr >= 0x4000 && addr < 0xC000) return;
	}

	/* Konami SCC: KSS maps both 9800 and B800 onto the same 0x90-byte file
	   via (addr & 0xDFFF) ^ 0x9800. On cartridge hardware the window only
	   appears after the mapper is written with 0x3F (page2) — without that
	   gate, ordinary RAM at 9800 would be stolen on generic titles. */
	if (chipScc_ && (sccEnable_ || sccMapped_)) {
		if (addr == 0x9000) {
			sccMapped_ = ((data & 0x3fu) == 0x3fu) ? 1 : 0;
			/* Fall through to the bank handler. */
		} else if (addr == 0xb000) {
			/* SCC-I / some MegaROMs expose the mirror via page3; treat the
			   high-bit form as an enable, otherwise clear. */
			if (data == 0x80 || (data & 0x3fu) == 0x3fu)
				sccMapped_ = 1;
			/* Fall through to the bank handler. */
		} else {
			const unsigned sccAddr = (unsigned)((addr & 0xdfffu) ^ 0x9800u);
			if (sccAddr < 0x90u) {
				CEmuChipSccWriteReg(chipScc_, sccAddr, data);
				sccAccessed_ = 1;
				return;
			}
		}
	} else if (chipScc_ && (addr == 0x9000 || addr == 0xb000)) {
		if (addr == 0x9000)
			sccMapped_ = ((data & 0x3fu) == 0x3fu) ? 1 : 0;
		else if (data == 0x80 || (data & 0x3fu) == 0x3fu)
			sccMapped_ = 1;
	}

	if (bank8k_ && bank_ && bankNum_) {
		if (addr == 0x9000 || addr == 0xb000) {
			const int bankno = (int)data - (int)bankOfs_;
			if (bankno >= 0 && bankno < (int)bankNum_) {
				const unsigned off = (unsigned)bankno * 0x2000u;
				const uint16_t base = (addr == 0x9000) ? 0x8000 : 0xa000;
				if (off + 0x2000u <= bankBytes_)
					memcpy(mem_ + base, bank_ + off, 0x2000);
			}
			return;
		}
		/* hoot: fetch/read the bank, writes go to a hidden RAM. Overwriting
		   mem_ here corrupted the mapped ROM (labyr/shiryo 8K KSS). */
		if (addr >= 0x8000 && addr < 0xC000)
			return;
	}
	mem_[addr] = data;
}

uint8_t CHardMsx::MemRead(uint16_t addr)
{
	return mem_[addr];
}

int CHardMsx::IsKssMagic(const unsigned char* data, unsigned sz) const
{
	if (!data || sz < 16) return 0;
	if (data[0] == 'K' && data[1] == 'S' && data[2] == 'C' && data[3] == 'C') return 1;
	if (data[0] == 'K' && data[1] == 'S' && data[2] == 'S' && data[3] == 'X') return 1;
	return 0;
}

void CHardMsx::StageBgm(unsigned index)
{
	if (index >= BGM_BANKS || !bgmPresent_[index] || !bgmBank_[index]) return;
	unsigned n = bgmBankSize_[index];
	if (n > mdataSize_) n = mdataSize_;
	if (mdataAddr_ + n > 0x10000)
		n = 0x10000u - mdataAddr_;
	memcpy(mem_ + mdataAddr_, bgmBank_[index], n);
	if (n < mdataSize_ && mdataAddr_ + mdataSize_ <= 0x10000)
		memset(mem_ + mdataAddr_ + n, 0, mdataSize_ - n);
}

int CHardMsx::LoadGeneric(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge || !cpu_) return 0;
	titleCode_ = titleCode;
	ge_ = ge;
	genericMode_ = 1;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	memset(opllRegs_, 0, sizeof(opllRegs_));
	idle_ = 0;
	playing_ = 0;
	chips_ = 0;
	ttlPrgBytes_ = 0;
	ttlPrgAddr_ = 0;

	CEmuMsxMergeCompanions(fs, ge);

	int loadedCode = 0;
	int useOpll = ParseOptHex(ge, "use_opll", 0);
	int useMsxa = ParseOptHex(ge, "use_msxa", 0);
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = (_stricmp(r->type, "code") == 0)
			? FindMsxCodeRom(fs, r->name, &sz)
			: CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		if (_stricmp(r->type, "code") == 0 || _stricmp(r->type, "fmbios") == 0
			|| _stricmp(r->type, "rom") == 0) {
			int off = r->offset;
			/* type=rom is a cartridge image — keep out of page0 BIOS. Do not
			   flatten type=sccp here: Konami SCC+ dumps overlay GRA.BIN @4000
			   and kgc* already PLAYS from the patch alone. */
			if (_stricmp(r->type, "rom") == 0 && off <= 0)
				off = 0x4000;
			/* FMPAC.ROM @0 is a slot-1 cartridge image, not page0. Mapping it
			   at 0 wipes BIOS stubs and the patch @0400 (yosikon/winsltn).
			   Some XML rows type it as code @4000 (laplace) — still clip. */
			const int isFmpac = (_stricmp(r->type, "fmbios") == 0
				|| _stricmp(r->name, "FMPAC.ROM") == 0) ? 1 : 0;
			if (isFmpac && off <= 0)
				off = 0x4000;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			/* Padded 64K FMPAC.ROM @4000 must not wipe later code (yosikon
			   DRIVER @$D400, winsltn ALL.BIN @$B9B9, rona MUSDRV @$CE00). */
			if (isFmpac) {
				for (int j = 0; j < ge->romCount; j++) {
					if (j == i) continue;
					const CEmuRomEntry* o = &ge->rom[j];
					if (_stricmp(o->type, "code") != 0 && _stricmp(o->type, "rom") != 0)
						continue;
					int ooff = o->offset;
					if (_stricmp(o->type, "rom") == 0 && ooff <= 0)
						ooff = 0x4000;
					if (ooff > off && ooff < off + (int)n)
						n = (unsigned)(ooff - off);
				}
			}
			memcpy(mem_ + off, data, n);
			loadedCode++;
			if (isFmpac)
				chips_ |= CHIP_FMPAC;
			if (_strnicmp(r->name, "TTLPRG", 6) == 0 && n > 0 && n <= sizeof(ttlPrg_)) {
				memcpy(ttlPrg_, data, n);
				ttlPrgBytes_ = n;
				ttlPrgAddr_ = (uint16_t)off;
			}
		} else if (_stricmp(r->type, "bgm") == 0) {
			int idx = r->offset;
			if (idx < 0 || idx >= BGM_BANKS) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (bgmBank_[idx]) free(bgmBank_[idx]);
			bgmBank_[idx] = buf;
			bgmBankSize_[idx] = n;
			bgmPresent_[idx] = 1;
		}
	}

	/* Zip-only fallback when catalog roms missing: prefer real drivers over patch. */
	if (!loadedCode) {
		int best = -1;
		unsigned bestSz = 0;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 256) continue;
			if (_stricmp(pathA, "patch") == 0) continue;
			if (sz > bestSz) { bestSz = sz; best = i; }
		}
		if (best >= 0) {
			unsigned n = fs->files[best].size;
			if (n > 0xC000) n = 0xC000;
			memcpy(mem_ + 0x4000, fs->files[best].data, n);
			loadedCode = 1;
		}
		/* Always map tiny patch @0400 when present. */
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (_stricmp(pathA, "patch") != 0) continue;
			unsigned n = fs->files[i].size;
			if (n > 0x200) n = 0x200;
			memcpy(mem_ + 0x400, fs->files[i].data, n);
			loadedCode++;
			break;
		}
	}
	if (!loadedCode) return 0;

	/* Hoot generic patches CALL MSX BIOS (KOEI: 0090 GICINI, 0093 WRTPSG,
	   0096 RDPSG). CALL 0090 into a zero NOP-sled re-enters the patch
	   (pc≈01AB). Plant RET at GICINI and WRTPSG/RDPSG trampolines only —
	   do NOT RET-fill all of 0..0x3FF (breaks undead ENASLT CALL 0024).
	   Impl bodies @00C0/00D0 stay below undead OPLLDRV@0100. */
	if (mem_[0x0090] == 0x00)
		mem_[0x0090] = 0xC9; /* GICINI */
	mem_[0x0093] = 0xC3; mem_[0x0094] = 0xC0; mem_[0x0095] = 0x00; /* JP 00C0 */
	mem_[0x0096] = 0xC3; mem_[0x0097] = 0xD0; mem_[0x0098] = 0x00; /* JP 00D0 */
	mem_[0x00C0] = 0xD3; mem_[0x00C1] = 0xA0;
	mem_[0x00C2] = 0x7B;
	mem_[0x00C3] = 0xD3; mem_[0x00C4] = 0xA1;
	mem_[0x00C5] = 0xC9;
	mem_[0x00D0] = 0xD3; mem_[0x00D1] = 0xA0;
	mem_[0x00D2] = 0xDB; mem_[0x00D3] = 0xA2;
	mem_[0x00D4] = 0xC9;
	PlantBiosStubs();

	initPc_ = (uint16_t)ParseOptHex(ge, "init_pc", 0x400);
	mdataAddr_ = (uint16_t)ParseOptHex(ge, "mdata_addr", 0xA400);
	{
		int ms = ParseOptHex(ge, "mdata_size", 0x800);
		int mfs = ParseOptHex(ge, "mfile_size", 0);
		if (mfs > ms) ms = mfs;
		if (ms <= 0) ms = 0x800;
		if (ms > BGM_SIZE) ms = BGM_SIZE;
		mdataSize_ = (unsigned)ms;
	}
	/* Tokuma MSX·FAN / msfield: catalog mdata_addr=0x9ff9 + size 0x2000
	   overflows the 64K map (StageBgm would copy ~7 bytes). FMPAC patch
	   play path uses HL=A000 — stage songs there. */
	if ((unsigned)mdataAddr_ + mdataSize_ > 0x10000u) {
		mdataAddr_ = 0xA000;
		if (mdataSize_ > 0x6000u)
			mdataSize_ = 0x6000u;
	}

	if (useOpll)
		chips_ |= CHIP_FMPAC;
	if (useMsxa)
		chips_ |= CHIP_MSXAUDIO;
	EnsureOpll(useOpll ? 1 : 0);
	if (useMsxa)
		EnsureMsxAudio();
	{
		const int useScc = ParseOptHex(ge, "use_scc", 0);
		if (useScc)
			sccEnable_ = 1;
	}
	/* Generic: don't force SCC into 9800 — wait for mapper 0x3F. KSS sets
	   sccEnable_ from the chip byte below in LoadKssImage. */
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	if (chipScc_) chipScc_->Reset();

	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

int CHardMsx::LoadKssImage(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	unsigned sz = 0;
	const unsigned char* data = NULL;

	/* Prefer real KSS magic over tiny hoot "patch" stubs. */
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "code") != 0) continue;
		unsigned s = 0;
		const unsigned char* d = CEmuZipFsFind(fs, ge->rom[i].name, &s);
		if (!d || s < 0x10) continue;
		if (IsKssMagic(d, s)) { data = d; sz = s; break; }
	}
	if (!data) {
		for (int i = 0; i < ge->romCount; i++) {
			if (_stricmp(ge->rom[i].type, "code") != 0) continue;
			if (_stricmp(ge->rom[i].name, "patch") == 0) continue;
			data = CEmuZipFsFind(fs, ge->rom[i].name, &sz);
			if (data && sz >= 0x10) break;
			data = NULL; sz = 0;
		}
	}
	if (!data) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			size_t n = strlen(pathA);
			if (n < 4) continue;
			if (_stricmp(pathA + n - 4, ".kss") != 0) continue;
			data = fs->files[i].data;
			sz = fs->files[i].size;
			break;
		}
	}
	if (!data || sz < 0x10) return 0;
	if (!IsKssMagic(data, sz) && sz < 256) return 0;

	if (sz > sizeof(rom_)) sz = (unsigned)sizeof(rom_);
	memcpy(rom_, data, sz);

	loadAdr_ = Rd16(rom_ + 4);
	loadSize_ = Rd16(rom_ + 6);
	initAdr_ = Rd16(rom_ + 8);
	intAdr_ = Rd16(rom_ + 10);
	bankOfs_ = rom_[0x0c];
	bankNum_ = (uint8_t)(rom_[0x0d] & 0x7f);
	bank8k_ = (rom_[0x0d] & 0x80) ? 1 : 0;
	chips_ = rom_[0x0f];
	/* Do not steal $9800 as SCC until the mapper is written 0x3F.
	   hoot maps SCC from the chip byte, but several KSCC (replcart) keep
	   work RAM there; Konami SCC titles still write 0x3F first. */
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	if (chipScc_) chipScc_->Reset();

	if (bankNum_) {
		bankBytes_ = 0x4000u * (unsigned)bankNum_;
		bank_ = (uint8_t*)malloc(bankBytes_);
		if (bank_) {
			memset(bank_, 0, bankBytes_);
			const unsigned src = 0x10u + (unsigned)loadSize_;
			if (src < sz) {
				unsigned n = bankBytes_;
				if (src + n > sz) n = sz - src;
				memcpy(bank_, rom_ + src, n);
			}
		}
	}

	EnsureOpll(0);
	if (chips_ & CHIP_SNG)
		EnsureSng();
	if (chips_ & CHIP_MSXAUDIO)
		EnsureMsxAudio();
	MapDefault();
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

int CHardMsx::LoadKss(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge || !cpu_) return 0;
	memset(mem_, 0, sizeof(mem_));
	memset(rom_, 0, sizeof(rom_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(opllRegs_, 0, sizeof(opllRegs_));
	if (bank_) { free(bank_); bank_ = NULL; bankBytes_ = 0; }
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	idle_ = 0;
	playing_ = 0;
	genericMode_ = 0;
	ge_ = ge;
	titleCode_ = titleCode;

	if (chipOpll_) {
		OPLL_delete((OPLL*)chipOpll_);
		chipOpll_ = NULL;
	}
	if (chipOpl_) {
		CEmuChipYm3812Destroy(chipOpl_);
		chipOpl_ = NULL;
	}
	if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
	mapper_ = MAP_NONE;

	if (_stricmp(ge->subtype, "ds4") == 0)
		return LoadDs4(fs, ge, titleCode);
	if (_stricmp(ge->subtype, "ascii16") == 0)
		return LoadAscii16(fs, ge, titleCode);
	if (_stricmp(ge->subtype, "dq1") == 0 || _stricmp(ge->subtype, "dq2") == 0)
		return LoadDq(fs, ge, titleCode);

	const int hasInitPc = ParseOptHex(ge, "init_pc", -1) >= 0
		|| ParseOptHex(ge, "mdata_addr", -1) >= 0;
	const int isKssSub = (_stricmp(ge->subtype, "kss") == 0
		|| _stricmp(ge->subtype, "opll") == 0);

	/* Offset romlists (hoot generic/BirdySoft/Compile/...) — not KSS. */
	int hasOffsetCode = 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "code") != 0) continue;
		if (ge->rom[i].offset > 0) { hasOffsetCode = 1; break; }
	}

	if ((!isKssSub && (hasInitPc || hasOffsetCode || _stricmp(ge->subtype, "generic") == 0))
		|| (hasOffsetCode && hasInitPc)) {
		if (LoadGeneric(fs, ge, titleCode))
			return 1;
	}

	if (LoadKssImage(fs, ge)) {
		genericMode_ = 0;
		return 1;
	}

	/* Last resort: generic without catalog opts (zip-local heuristics). */
	return LoadGeneric(fs, ge, titleCode);
}

int CHardMsx::StartSongKss(unsigned titleCode)
{
	if (!cpu_) return 0;
	memset(mem_, 0xc9, 0x4000);
	memset(mem_ + 0x4000, 0x00, 0xc000);
	if (loadSize_ && loadAdr_ < 0x10000) {
		unsigned n = loadSize_;
		if ((unsigned)loadAdr_ + n > 0x10000)
			n = 0x10000u - loadAdr_;
		if (0x10u + n <= sizeof(rom_))
			memcpy(mem_ + loadAdr_, rom_ + 0x10, n);
	}
	memcpy(mem_, kKssIpl, sizeof(kKssIpl));
	mem_[INIT_ADR] = (uint8_t)(initAdr_ & 0xff);
	mem_[INIT_ADR + 1] = (uint8_t)(initAdr_ >> 8);
	mem_[INT_ADR] = (uint8_t)(intAdr_ & 0xff);
	mem_[INT_ADR + 1] = (uint8_t)(intAdr_ >> 8);
	mem_[0x93] = 0xc3;
	mem_[0x94] = 0x01;
	mem_[0x95] = 0x00;
	mem_[0x96] = 0xc3;
	mem_[0x97] = 0x09;
	mem_[0x98] = 0x00;

	/* Labyrinth chip-byte 0x08 is MSX-AUDIO; titles 0x80/0x81 set BIT 7 so
	   the loader takes the Y8950 path. Without a timer-ticking Y8950 that
	   path never keys the FM. Bit7-clear is the PSG arrangement. */
	{
		uint8_t play = (uint8_t)(titleCode & 0xff);
		if ((chips_ & CHIP_MSXAUDIO) && !(chips_ & CHIP_FMPAC) && (play & 0x80))
			play = (uint8_t)(play & 0x7f);
		ioport_[PLAY_CODE_PORT] = play;
	}
	PortOut(0xfe, 0);
	if (bank8k_ && bank_ && bankBytes_ >= 0x2000u) {
		memcpy(mem_ + 0x8000, bank_, 0x2000);
		if (bankNum_ > 1 && bankBytes_ >= 0x4000u)
			memcpy(mem_ + 0xA000, bank_ + 0x2000, 0x2000);
	}

	/* Unset I + $C9 fill made IM2 vector $C9C9. Point it at the IPL ISR. */
	if (mem_[0x00FF] == 0xC9 && mem_[0x0100] == 0xC9) {
		mem_[0x00FF] = 0x38;
		mem_[0x0100] = 0x00;
	}

	cpu_->reset(mem_);
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 0;
	sccAccessed_ = 0;
	if (chipScc_) chipScc_->Reset();
	if (chipSng_) chipSng_->Reset();
	if (chipAy_) chipAy_->Reset();

	CEmuHardMsxSetActive(this);
	/* hoot Play() emulates with the 60 Hz timer live until SKIP. Init
	   that EI/HALTs (or waits on vblank) never returns without IRQs. */
	int guard = 0;
	uint64_t nextIrq = (uint64_t)MSX_CPU_HZ / 60u;
	while (!idle_ && guard++ < 4000000) {
		uint8_t* m = cpu_->get_mem();
		if (m && m[cpu_->r.pc] == 0x76) {
			cpu_->irqDelay = 0;
			if (cpu_->r.iff1) {
				if (cpu_->r.im != 2 || !Ay_CpuIm2Interrupt(cpu_, 0xff))
					Ay_CpuIm1Interrupt(cpu_);
			}
			cpuCycles_ += 16;
			cpu_->adjust_time(16);
			if (cpuCycles_ >= nextIrq)
				nextIrq += (uint64_t)MSX_CPU_HZ / 60u;
			continue;
		}
		const int cyc = Ay_CpuRunOne(cpu_);
		if (cyc <= 0) break;
		cpuCycles_ += (uint64_t)cyc;
		if (cpuCycles_ >= nextIrq) {
			nextIrq += (uint64_t)MSX_CPU_HZ / 60u;
			if (cpu_->r.iff1) {
				if (cpu_->r.im != 2 || !Ay_CpuIm2Interrupt(cpu_, 0xff))
					Ay_CpuIm1Interrupt(cpu_);
			}
		}
	}
	playing_ = 1;
	idle_ = 0;
	return 1;
}

int CHardMsx::StartSongGeneric(unsigned titleCode)
{
	if (!cpu_) return 0;
	titleCode_ = titleCode;
	/* Catalog codes pack up to three bytes and the width says which is which:
	     one byte  (angelus 0x05)   — picks the bgm rom, and is the song.
	     two bytes (aleste2 0x0115, — middle byte picks the bgm rom, low byte
	                gshogi 0x0501)    is the song inside it. Handing the rom
	                                  number to the driver instead made every
	                                  such title render one song.
	     three     (ys2 0x010112)   — low picks the rom, middle is the track,
	                                  top is the engine read from port 5.
	   The low byte cannot be used to tell these apart: gshogi's 0x01 track is
	   also a valid bgm index. */
	const unsigned low = titleCode & 0xff;
	const unsigned mid = (titleCode >> 8) & 0xff;
	const unsigned top = (titleCode >> 16) & 0xff;
	unsigned song = low;   /* bgm rom to stage */
	unsigned sel3 = low;   /* port 3 mailbox */
	unsigned sel4 = low;   /* port 4 mailbox */

	/* f1sp3d 0x0d09/0x0e09: same file in the low byte, track in the middle
	   that is not itself a bgm rom. herzog/ys3/ds32 also share lows but
	   their mids ARE files — treating those as tracks silenced ds32. */
	int extraCode = 0;
	if (ge_) {
		for (int i = 0; i < ge_->romCount; i++) {
			if (_stricmp(ge_->rom[i].type, "code") != 0)
				continue;
			if (ge_->rom[i].offset == (int)initPc_)
				continue;
			extraCode = 1;
			break;
		}
	}
	int lowFilePack = 0;
	if (ge_) {
		for (int i = 0; i < ge_->titleCount && !lowFilePack; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			const unsigned li = ci & 0xff;
			const unsigned mi = (ci >> 8) & 0xff;
			if (mi < 8u || li >= BGM_BANKS || !bgmPresent_[li])
				continue;
			if (mi < BGM_BANKS && bgmPresent_[mi])
				continue;
			lowFilePack = 1;
		}
	}
	/* yajiuma/arugies: several 0xMM00 titles mean "track 0 of file MM".
	   dios 0x0100 plus SE 0x00 is only two such codes — keep low as file. */
	int n00 = 0;
	if (ge_) {
		for (int i = 0; i < ge_->titleCount; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu || (ci & 0xff) != 0)
				continue;
			const unsigned mi = (ci >> 8) & 0xff;
			if (mi < BGM_BANKS && bgmPresent_[mi])
				n00++;
		}
	}
	const int fileInMid = (extraCode && n00 >= 3) ? 1 : 0;
	/* dios 0x0100-0x0107: mid=1 is BGM vs SE, low is the file. A=0 stops. */
	int nMid1 = 0;
	if (ge_) {
		for (int i = 0; i < ge_->titleCount; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			if (((ci >> 8) & 0xff) != 1)
				continue;
			const unsigned li = ci & 0xff;
			if (li < BGM_BANKS && bgmPresent_[li])
				nMid1++;
		}
	}
	const int classInMid = (extraCode && !fileInMid && nMid1 >= 6) ? 1 : 0;
	/* wingsp 0x0206: extraCode=0, mid is OUT-command 1/2, low is the .COM.
	   gshogi 0x0501 has mid>2; sgolveli 0x0126 has a low that is not a file. */
	int cmdInMid = 0;
	if (!extraCode && ge_) {
		int ok = 1, n2 = 0, nCmd = 0;
		for (int i = 0; i < ge_->titleCount; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			const unsigned li = ci & 0xff;
			const unsigned mi = (ci >> 8) & 0xff;
			if (mi == 0)
				continue;
			n2++;
			if (mi > 2u || li >= BGM_BANKS || !bgmPresent_[li])
				ok = 0;
			if (mi >= 1u && mi <= 2u && li != mi)
				nCmd++;
		}
		cmdInMid = (ok && n2 >= 2 && nCmd >= 1) ? 1 : 0;
	}
	/* sgolveli 0x0101 / 0x0001: extraCode=0 (DRIVER is type=bgm @0100),
	   init $6000 / mdata $0100. runemst3/randar3/gshogi/gokudo miss this
	   gate (DRIVER is type=code, or init is $0400). */
	int sameLowCmd = 0;
	if (!extraCode && !cmdInMid && initPc_ == 0x6000 && mdataAddr_ == 0x0100
		&& ge_) {
		for (int i = 0; i < ge_->titleCount && !sameLowCmd; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			const unsigned li = ci & 0xff;
			const unsigned mi = (ci >> 8) & 0xff;
			if (li >= BGM_BANKS || !bgmPresent_[li])
				continue;
			for (int j = i + 1; j < ge_->titleCount; j++) {
				const unsigned cj = ge_->title[j].code;
				if (cj > 0xFFFFu || (cj & 0xff) != li)
					continue;
				if (((cj >> 8) & 0xff) != mi) {
					sameLowCmd = 1;
					break;
				}
			}
		}
	}
	/* ultima4 0x0104: file in low, 1-based track in mid. MUSICMSX play
	   does A-1 into the staged file's pointer table. */

	/* dquiz 0x0001/0x0101 and m123 0xC00000/0xC00011: patch IN A,(4)
	   indexes a pointer table, then IN A,(3) is the song. dquiz even/odd
	   share low=1 so song=low staged the same MUSIC01. ds32 is mdata $2000
	   — keep that gate. 3-byte 0xC000xx would otherwise look like addrBox. */
	const int compilePtr = (extraCode && initPc_ == 0xF000
		&& mdataAddr_ == 0xC000) ? 1 : 0;
	/* saziri 0x4000xxxx: high word is an address inside mdata.
	   lenam 0xFF000A is a command+file, not an address (hi=0xFF00). */
	const unsigned hiWordEarly = titleCode >> 16;
	const int addrBoxEarly = (mdataSize_ > 0
		&& hiWordEarly >= mdataAddr_
		&& hiWordEarly < (unsigned)mdataAddr_ + mdataSize_) ? 1 : 0;
	/* silviana/feedback/sbp/xanadus: IN A,(4); CP 1; JR Z,se — exclusive
	   SE path. Title 0x01 is BGM file 1, so port4=1 never reaches play.
	   JR offset >= $13 skips algowars/famicle2 (still PLAYS with 0x01).
	   ninja/ginei have no bgm rom 1. ff_msx 0x01 is the PLAYS smoke. */
	int port4Se = 0;
	if (titleCode <= 0xFFu && low == 1 && bgmPresent_[1]
		&& (unsigned)initPc_ + 80u < 0x10000u) {
		for (unsigned i = 0; i + 6u < 80u; i++) {
			const unsigned a = (unsigned)initPc_ + i;
			if (mem_[a] == 0xDB && mem_[a + 1] == 0x04
				&& mem_[a + 2] == 0xFE && mem_[a + 3] == 0x01
				&& mem_[a + 4] == 0x28 && mem_[a + 5] >= 0x13) {
				port4Se = 1;
				break;
			}
		}
	}
	/* ds00: LD HL,$7228 (DATA2); IN A,(4); CP 1; JR NZ; LD HL,$443B (DATA1).
	   0x0001 is DATA1 (port4=1). 0x0101 kept port4=low=1 so both picks
	   staged DATA1 song 1. 0x01xx must take DATA2 with port3=low. */
	int ds00Data = 0;
	if (initPc_ == 0x6000
		&& mem_[0x6016] == 0x21 && mem_[0x6017] == 0x28 && mem_[0x6018] == 0x72
		&& mem_[0x6019] == 0xDB && mem_[0x601A] == 0x04
		&& mem_[0x601B] == 0xFE && mem_[0x601C] == 0x01) {
		ds00Data = 1;
	}
	/* ps8/kubikiri/quinplf: IN A,(4); INC A stores a 1-based song-in-file.
	   Title 0x01 is file 1, so port4=song silenced SIM GIRL / FM01. */
	int port4Inc = 0;
	if ((unsigned)initPc_ + 80u < 0x10000u) {
		for (unsigned i = 0; i + 3u < 80u; i++) {
			const unsigned a = (unsigned)initPc_ + i;
			if (mem_[a] == 0xDB && mem_[a + 1] == 0x04
				&& mem_[a + 2] == 0x3C) {
				port4Inc = 1;
				break;
			}
		}
	}
	/* tantexr: IN A,(6); LD H,A; IN A,(5); LD L,A then CALL $C000.
	   4-byte 0x239D0000 is CALL $239D in the LDIR dest, not an mdata addr. */
	int port56Hl = 0;
	if ((unsigned)initPc_ + 80u < 0x10000u) {
		for (unsigned i = 0; i + 6u < 80u; i++) {
			const unsigned a = (unsigned)initPc_ + i;
			if (mem_[a] == 0xDB && mem_[a + 1] == 0x06
				&& mem_[a + 2] == 0x67 && mem_[a + 3] == 0xDB
				&& mem_[a + 4] == 0x05 && mem_[a + 5] == 0x6F) {
				port56Hl = 1;
				break;
			}
		}
	}

	if (lowFilePack && low < BGM_BANKS && bgmPresent_[low]) {
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (compilePtr) {
		song = (mid < BGM_BANKS && bgmPresent_[mid]) ? mid : 0;
		sel3 = low;
		sel4 = mid;
	} else if (addrBoxEarly) {
		song = low;
		sel3 = low;
		sel4 = low;
	} else if (port4Se) {
		song = low;
		sel3 = low;
		sel4 = 0;
	} else if (ds00Data) {
		song = low;
		sel3 = low;
		sel4 = (mid == 0) ? 1 : 0;
	} else if (port4Inc) {
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (fileInMid && mid < BGM_BANKS && bgmPresent_[mid]) {
		song = mid;
		sel3 = low;
		sel4 = low;
	} else if (fileInMid && mid == 0 && bgmPresent_[0]) {
		song = 0;
		sel3 = low;
		sel4 = low;
	} else if (classInMid && mid == 1 && low < BGM_BANKS && bgmPresent_[low]) {
		/* Only the 0x01xx BGM class. Forcing A=mid on one-byte titles
		   set A=0 and silenced mbsp (feedback happened to auto-play). */
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (cmdInMid && mid >= 1 && mid <= 2 && bgmPresent_[low]) {
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (sameLowCmd && low < BGM_BANKS && bgmPresent_[low]) {
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (mdataAddr_ == 0xCEB1 && mid >= 1 && low < BGM_BANKS
		&& bgmPresent_[low]) {
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (initPc_ == 0x3000 && mdataAddr_ == 0x0300) {
		/* ys3 0x15/0x1A: IN A,(4) is track-in-file at $1D00, file is low.
		   Port4=song=0x15 is not a track in AF7MUS. */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (((initPc_ == 0x4D00 && mdataAddr_ == 0x6000)
		|| (initPc_ == 0x1000 && mdataAddr_ == 0x0300)
		|| mdataAddr_ == 0x8FF9) && top && top != 0xFF) {
		/* ys/ys2: port4 is track-in-file, port5 is engine, low is the rom.
		   0x010005 used sel4=low=5 as a track and stayed silent.
		   daiva5 0x010003: IN A,(3) is the file; IN A,(4) is the track
		   into CALL $ACCC after the MSX.BIN LDDR. */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (initPc_ == 0x400 && mdataAddr_ == 0x3500 && top && top != 0xFF) {
		/* Hertz psywrld 0x010000/0x010001: file in low, track in mid.
		   `top { sel4 = mid ? mid : low }` set port4=1 on VISUAL01. */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (initPc_ == 0x400 && mdataAddr_ == 0x6000 && mdataSize_ == 0x4000
		&& top && top != 0xFF && mid == 0 && low < BGM_BANKS && bgmPresent_[low]) {
		/* yakyufan 0x600001: 0x6000 is mdata start, mid=0, so
		   `top { sel4 = low }` set port4=1 on MUS.DAT track 0.
		   0x76B300 has mid=0xB3 and must keep the PLAYS path. */
		song = low;
		sel3 = low;
		sel4 = 0;
	} else if (top == 0xFF) {
		/* lenam 0xFF000A: Hertz BGMDRV CALL $0B06 does LD A,C; OR A;
		   JP Z skip. C comes from IN A,(5). Port5=0 was a hard stop.
		   File is low, track in mid (0 is タイトル). */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (top) {
		sel4 = mid ? mid : low;
	} else if (mid && !bgmPresent_[low] && bgmPresent_[mid]) {
		/* aleste2 0x0115: middle byte picks the bgm rom, low is the song
		   inside it. gshogi 0x0501 keeps low as the selector because 0x01
		   is also a valid rom — staging the middle file silenced 0x0201. */
		song = mid;
	}

	/* Defer StageBgm until after init settle when mdata overlays a code
	   image the patch still needs (Tokuma FMPAC @A000; Telenet alba2/valis2
	   TSTI/IPL89 @4000 with mdata_addr=4000 — early StageBgm zeroed the
	   LDIR source and left CALL AC06 in a NOP sled → pc≈F91F).
	   ys3: mdata $0300+$0E00 overlays EFCDAT @0B00.
	   Also defer every $4000–$E000 window: gshogi mdata @A000 is workspace
	   during init, and staging the song first silenced both picks. */
	int deferBgm = (mdataAddr_ >= 0x4000 && mdataAddr_ < 0xE000);
	if (!deferBgm && ge_ && mdataSize_ > 0) {
		const unsigned m0 = mdataAddr_;
		const unsigned m1 = mdataAddr_ + mdataSize_;
		for (int i = 0; i < ge_->romCount; i++) {
			if (_stricmp(ge_->rom[i].type, "code") != 0
				&& _stricmp(ge_->rom[i].type, "fmbios") != 0)
				continue;
			const unsigned off = (unsigned)(ge_->rom[i].offset < 0 ? 0 : ge_->rom[i].offset);
			if (off >= m0 && off < m1) { deferBgm = 1; break; }
		}
	}

	if (!deferBgm) {
		if (song < BGM_BANKS && bgmPresent_[song])
			StageBgm(song);
		else {
			for (unsigned i = 0; i < BGM_BANKS; i++) {
				if (bgmPresent_[i]) { StageBgm(i); break; }
			}
		}
	}

	/* Mailboxes used by hoot MSX patches:
	   - BirdySoft/Compile: port2=play, port4=song (Compile copies 4→3)
	   - Enix/Falcom-ish:   port2=play, port3=song (angelus/can3/jngolf)
	   - Compile/jngolf:    port7 bit0 = OPLL present
	   - ys2/arcus:         port5 = engine/bank selector (code's top byte) */
	ioport_[0x00] = (uint8_t)(song & 0xff);
	ioport_[0x02] = 0x01; /* play command (seen via playCmdPending after settle) */
	ioport_[0x03] = (uint8_t)(sel3 & 0xff);
	ioport_[0x04] = (uint8_t)(sel4 & 0xff);
	ioport_[0x05] = (top == 0xFF) ? 0 : (uint8_t)(top & 0xff);
	ioport_[0x07] = (chips_ & CHIP_FMPAC) ? 0x01 : 0x00;
	/* KOEI 4-byte (genghis 0x01990010): patch IN A,(4)/IN A,(5) as HL into
	   MMLDATA @8000. High word is the offset; no separate bgm roms. */
	int anyBgm = 0;
	for (unsigned i = 0; i < BGM_BANKS; i++) {
		if (bgmPresent_[i]) { anyBgm = 1; break; }
	}
	if (!anyBgm && titleCode > 0xFFFFFFu) {
		uint16_t hl = (uint16_t)((titleCode >> 16) + 0x8000u);
		ioport_[0x04] = (uint8_t)(hl & 0xff);
		ioport_[0x05] = (uint8_t)(hl >> 8);
		ioport_[0x06] = (uint8_t)(mid & 0xff);
		sel4 = ioport_[0x04];
	}
	/* saziri 0x44730002 / tantexr 0x239d0000: IN A,(5)/IN A,(6) as HL
	   into the staged file. High word is an absolute address when it
	   sits inside mdata, else an offset. Skip 0x01xxxx engine bytes. */
	const unsigned hiWord = hiWordEarly;
	const int addrBox = addrBoxEarly ? 1 : 0;
	if (addrBox) {
		uint16_t hl = (uint16_t)hiWord;
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	} else if (port56Hl && titleCode > 0xFFFFu) {
		uint16_t hl = (uint16_t)(titleCode >> 16);
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	}
	ioport_[PLAY_CODE_PORT] = (uint8_t)(sel3 & 0xff);
	playCmdPending_ = 0; /* arm only after settle */
	titleCode_ = sel4;

	/* MSX BIOS IRQ @0038 → CALL H.TIMI (FD9F) → EI;RET.
	   Plant the trampoline *before* init runs, because the patches split into
	   two camps and both write 0038 themselves:
	     - jesus/ankoku/columns/... store C3 + their own ISR address, so a
	       later plant here would erase the driver's handler and leave 60 Hz
	       interrupts calling a bare RET (music init'd, then never advanced).
	     - ys2/arcus store only the 0039 operand, assuming 0038 already holds
	       C3, so the byte has to be there up front.
	   Either way our operand is only the default: whoever claims 0038 wins,
	   and titles that claim neither reach their late H.TIMI hook via FD9F.
	   The body starts inert (EI;RET). The Tokuma MSX-FAN patches hook
	   H.TIMI at FMPAC BIOS 411F in their first instructions, so calling it
	   during the settle would run the BIOS player over an unstaged song and
	   leave it stopped; it goes live once the song is in place. */
	if (mem_[0xFD9F] == 0x00)
		mem_[0xFD9F] = 0xC9;
	mem_[0x0038] = 0xC3;
	mem_[0x0039] = 0xE0;
	mem_[0x003A] = 0x00; /* JP 00E0 */
	mem_[0x00E0] = 0xFB;             /* EI */
	mem_[0x00E1] = 0xC9;             /* RET */

	/* ys2: init LDIR $2000→$B000 wipes TTLPRG before the engine-1 play
	   path (port5=1) can CALL $D48B. Engine 0 recopies MUSPRG at $102E, so
	   skipping the first overlay is safe for both engines. The play
	   path always CALL $106A first; $107D starts at 0 so that stop goes
	   to $BFC3 (MUSPRG). With TTLPRG still mapped that call is poison
	   and engine 1 never reaches $D48B. First-play stop is a no-op. */
	if (initPc_ == 0x1000 && mdataAddr_ == 0x0300
		&& mem_[0x1006] == 0x21 && mem_[0x1007] == 0x00 && mem_[0x1008] == 0x20
		&& mem_[0x1009] == 0x11 && mem_[0x100A] == 0x00 && mem_[0x100B] == 0xB0) {
		memset(mem_ + 0x1006, 0x00, 11);
		if (mem_[0x101C] == 0xCD && mem_[0x101D] == 0x6A && mem_[0x101E] == 0x10)
			memset(mem_ + 0x101C, 0x00, 3);
	}

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpu_->r.sp = 0xF380;
	cpu_->r.iff1 = 1;
	cpu_->r.im = 1;
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 1;
	sccAccessed_ = 0;
	sccMapped_ = 0;
	if (chipScc_) chipScc_->Reset();

	/* Brief settle so init installs handlers before play edge. */
	CEmuHardMsxSetActive(this);
	int guard = 0;
	while (guard++ < 200000) {
		uint8_t* m = cpu_->get_mem();
		if (m && m[cpu_->r.pc] == 0x76) {
			cpu_->irqDelay = 0;
			if (!cpu_->r.iff1) {
				cpu_->r.pc = (uint16_t)(cpu_->r.pc + 1);
				cpuCycles_ += 4;
				continue;
			}
			/* yosikon CALL $D400 waits EI;HALT; settle has no Render IRQ. */
			if (cpu_->r.im != 2 || !Ay_CpuIm2Interrupt(cpu_, 0xff))
				Ay_CpuIm1Interrupt(cpu_);
			cpuCycles_ += 16;
			continue;
		}
		const int cyc = Ay_CpuRunOne(cpu_);
		if (cyc <= 0) break;
		cpuCycles_ += (uint64_t)cyc;
		if (idle_) break;
	}
	if (deferBgm) {
		if (song < BGM_BANKS && bgmPresent_[song])
			StageBgm(song);
		else {
			for (unsigned i = 0; i < BGM_BANKS; i++) {
				if (bgmPresent_[i]) { StageBgm(i); break; }
			}
		}
	}
	/* yosikon: FMPAC ID match on slot 0 stores A=0 in $D50E; play/ISR
	   treat 0 as "not found". Detect did succeed (OPLL @401C). */
	if (mem_[0xD400] == 0xC3 && mem_[0xD401] == 0x78 && mem_[0xD402] == 0xD5
		&& mem_[0xD50E] == 0)
		mem_[0xD50E] = 1;
	/* rona H.TIMI at $049D pushes 6 regs then CALL $D2F0 / RET, leaking
	   the frame so the first vblank RET jumps off the player. */
	if (mem_[0x041B] == 0xCD && mem_[0x041C] == 0x18 && mem_[0x041D] == 0xD0
		&& mem_[0x04AE] == 0xCD && mem_[0x04AF] == 0xF0 && mem_[0x04B0] == 0xD2
		&& mem_[0x04B1] == 0xC9) {
		mem_[0x04AE] = 0xC3; mem_[0x04AF] = 0xD5; mem_[0x04B0] = 0x04;
		static const uint8_t kRonaTimi[] = {
			0xCD, 0xF0, 0xD2,
			0xFD, 0xE1, 0xDD, 0xE1, 0xE1, 0xD1, 0xC1, 0xF1,
			0xFB, 0xC9
		};
		memcpy(mem_ + 0x04D5, kRonaTimi, sizeof kRonaTimi);
		/* Play HALT at $0416 waits for vblank before CALL $D018; ISR RET
		   lands back on HALT so D02F never runs. */
		if (mem_[0x0416] == 0x76 && mem_[0x0417] == 0xAF)
			mem_[0x0416] = 0x00;
		/* INIOPL LDIR trampoline to IY. PUSH AF/POP IY with A=$88 copies
		   over $00E0; a second CALL on play would wipe the IM1 body
		   we plant below. Skip CALSLT — ROM WRTOPL at $4110 is enough. */
		if (mem_[0xD02B] == 0xCD && mem_[0xD02C] == 0x1C && mem_[0xD02D] == 0x00) {
			mem_[0xD02B] = 0x00; mem_[0xD02C] = 0x00; mem_[0xD02D] = 0x00;
		}
	}
	/* ys2 title engine (port5=1): init LDIR MUSPRG→$B000 wipes TTLPRG. */
	if (ttlPrgBytes_ && ttlPrgAddr_ && (top == 1 || ioport_[0x05] == 1)) {
		unsigned n = ttlPrgBytes_;
		if ((unsigned)ttlPrgAddr_ + n > 0x10000u)
			n = 0x10000u - ttlPrgAddr_;
		if (n > sizeof(ttlPrg_)) n = sizeof(ttlPrg_);
		memcpy(mem_ + ttlPrgAddr_, ttlPrg_, n);
		/* TTLPRG ISR @D105 PUSH AF/DE then JP $0000 (BIOS chain). $0000 is
		   empty so the first vblank hits BDOS XOR A;RET, leaves IFF1 off,
		   and PulseVblankIrq never fires again. */
		if (mem_[0xD105] == 0xF5 && mem_[0xD116] == 0xC3
			&& mem_[0xD117] == 0x00 && mem_[0xD118] == 0x00
			&& mem_[0x0000] == 0x00) {
			mem_[0x0000] = 0xD1; /* POP DE */
			mem_[0x0001] = 0xF1; /* POP AF */
			mem_[0x0002] = 0xFB; /* EI */
			mem_[0x0003] = 0xC9; /* RET */
		}
	}
	/* Song is staged: let the trampoline body reach H.TIMI. 0038 itself is
	   left to whoever claimed it during init. Titles that hook H.TIMI only
	   after the play edge overwrite the RET below and start ticking then. */
	if (mem_[0xFD9F] == 0x00)
		mem_[0xFD9F] = 0xC9;
	mem_[0x00E0] = 0xF5;             /* PUSH AF */
	mem_[0x00E1] = 0xCD; mem_[0x00E2] = 0x9F; mem_[0x00E3] = 0xFD; /* CALL FD9F */
	mem_[0x00E4] = 0xF1;             /* POP AF */
	mem_[0x00E5] = 0xFB;             /* EI */
	mem_[0x00E6] = 0xC9;             /* RET */
	/* ultima4 MUSICMSX: play stores ($0105) into CE48; ISR skips if 0.
	   Compile DRIVER.BIN lives @0100 so this is empty only here. */
	if (mdataAddr_ == 0xCEB1 && mem_[0xCA80] == 0x18 && mem_[0x0105] == 0)
		mem_[0x0105] = 1;
	/* tantexr PSGDRV: BIOS/ISR falls into $0100-$03FF NOP sled (pc≈01DC)
	   and never reaches the play LDIR. Driver lives @D000, patch @0400,
	   LDIR dest is $1000 — this window is empty. */
	if (mdataAddr_ == 0x8000 && mem_[0xD000] == 0xC3
		&& mem_[0xD001] == 0x12 && mem_[0xD002] == 0xD0
		&& mem_[0x0400] == 0xF3) {
		for (unsigned a = 0x0100; a < 0x0400; a++) {
			if (mem_[a] == 0x00)
				mem_[a] = 0xC9;
		}
	}
	/* dssp1: zip ships ran/DRIVER.BIN (ORG $4000) but XML loads
	   BSRAND.OBJ at $3EF9. CALL $5921 then hits a pointer table and the
	   play wrapper's extra POPs smash SP ($3F5B / pc $F12A). Slide the
	   16K image so $5921 is the song indexer and patch CALL $589C /
	   $579A / $4AEA land on real code. Do not add 0x107 to the patch
	   addresses — those $58xx/$4AEA sites are already the $4000 entries. */
	if (mem_[0x0441] == 0xCD && mem_[0x0442] == 0x9A && mem_[0x0443] == 0x57
		&& mem_[0x579A] == 0xF3 && mem_[0x579B] == 0xCD
		&& mem_[0x579C] == 0x21 && mem_[0x579D] == 0x59
		&& mem_[0x5921] == 0x81) {
		uint8_t tmp[0x4000];
		memcpy(tmp, mem_ + 0x3EF9, 0x4000);
		memset(mem_ + 0x3EF9, 0, 0x107);
		memcpy(mem_ + 0x4000, tmp, 0x4000);
	}
	/* One-shot play after handlers exist (port2/3/4 mailboxes). */
	playCmdPending_ = 8; /* a few edges; not sticky-forever */
	/* daiva5 MSX.BIN: play CALL $049D does LDDR $B74F→$BF4F. A second
	   edge copies the already-relocated image and wipes $ACCC.
	   Do not apply this to every ED B8 patch: gokudo 0x01 needs later
	   edges to plant H.TIMI (pending=1 made the live pick silent). */
	if (mdataAddr_ == 0x8FF9)
		playCmdPending_ = 1;
	ioport_[0x02] = 0x01;
	if (!anyBgm && titleCode > 0xFFFFFFu) {
		uint16_t hl = (uint16_t)((titleCode >> 16) + 0x8000u);
		ioport_[0x04] = (uint8_t)(hl & 0xff);
		ioport_[0x05] = (uint8_t)(hl >> 8);
		titleCode_ = ioport_[0x04];
	} else if (addrBox) {
		uint16_t hl = (uint16_t)hiWord;
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	} else if (port56Hl && titleCode > 0xFFFFu) {
		uint16_t hl = (uint16_t)(titleCode >> 16);
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	} else if (top == 0xFF) {
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = 1; /* Hertz: C=0 skips play */
	} else if (top) {
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = (uint8_t)(top & 0xff);
	} else {
		ioport_[0x03] = (uint8_t)(compilePtr ? sel3 : song);
		ioport_[0x04] = (uint8_t)((lowFilePack || fileInMid || classInMid
			|| cmdInMid || sameLowCmd || mdataAddr_ == 0xCEB1
			|| compilePtr || port4Se || port4Inc || ds00Data
			|| (initPc_ == 0x3000 && mdataAddr_ == 0x0300)) ? sel4 : song);
	}
	ioport_[0x07] = (chips_ & CHIP_FMPAC) ? 0x01 : 0x00;
	idle_ = 0;
	return 1;
}

int CHardMsx::LoadDs4(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!LoadCartRom(fs, ge, "code"))
		return 0;
	mapper_ = MAP_DS4;
	genericMode_ = 0;
	chips_ = 0;
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	memset(mem_, 0, sizeof(mem_));
	/* Fixed 16K window at $4000 plus Konami 8K pages at $8000/$A000. */
	{
		unsigned n = cartBytes_;
		if (n > 0x4000u) n = 0x4000u;
		memcpy(mem_ + 0x4000, cart_, n);
	}
	MapDs4(0, 2);
	MapDs4(1, 3);
	memcpy(mem_, kDs4Ipl, sizeof(kDs4Ipl));
	mem_[0x93] = 0xc3; mem_[0x94] = 0x02; mem_[0x95] = 0x11;
	mem_[0x96] = 0xc3; mem_[0x97] = 0x0e; mem_[0x98] = 0x11;
	mem_[0x1102] = 0xf3; mem_[0x1103] = 0xd3; mem_[0x1104] = 0xa0;
	mem_[0x1105] = 0xf5; mem_[0x1106] = 0x7b; mem_[0x1107] = 0xd3;
	mem_[0x1108] = 0xa1; mem_[0x1109] = 0xfb; mem_[0x110a] = 0xf1;
	mem_[0x110b] = 0xc9;
	mem_[0x110e] = 0xd3; mem_[0x110f] = 0xa0;
	mem_[0x1110] = 0xdb; mem_[0x1111] = 0xa2; mem_[0x1112] = 0xc9;
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 0;
	if (chipAy_) chipAy_->Reset();
	CEmuHardMsxSetActive(this);
	/* Warmup must ignore SKIP/idle — IPL polls port 2 until Play. */
	{
		int guard = 0;
		uint64_t cyc = 0;
		while (cyc < 10000u && guard++ < 400000) {
			const int c = Ay_CpuRunOne(cpu_);
			if (c <= 0) break;
			cyc += (uint64_t)c;
			cpuCycles_ += (uint64_t)c;
		}
	}
	idle_ = 0;
	mem_[0xc042] = 0x02;
	mem_[0xc043] = 0x03;
	mem_[0xc095] = 0xff;
	mem_[0xc098] = 0xbf;
	return 1;
}

int CHardMsx::LoadAscii16(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	titleCode_ = titleCode;
	ge_ = ge;
	genericMode_ = 1;
	mapper_ = MAP_ASCII16;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	chips_ = CHIP_FMPAC;
	idle_ = 0;
	playing_ = 0;
	CEmuMsxMergeCompanions(fs, ge);
	if (!LoadCartRom(fs, ge, "rom"))
		return 0;
	MapAscii16(0, 0);
	MapAscii16(1, 1);
	int loadedCode = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "rom") == 0) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		int off = r->offset;
		if (off < 0) off = 0x400;
		if (off >= 0x10000) continue;
		unsigned n = sz;
		if (off + (int)n > 0x10000)
			n = (unsigned)(0x10000 - off);
		memcpy(mem_ + off, data, n);
		loadedCode++;
	}
	if (!loadedCode) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (_stricmp(pathA, "fmpatch") != 0 && _stricmp(pathA, "patch") != 0)
				continue;
			unsigned n = fs->files[i].size;
			if (n > 0x200) n = 0x200;
			memcpy(mem_ + 0x400, fs->files[i].data, n);
			loadedCode++;
			break;
		}
	}
	if (!loadedCode) return 0;
	PlantPsgTrampoline(mem_);
	PlantBiosStubs();
	initPc_ = (uint16_t)ParseOptHex(ge, "init_pc", 0x400);
	mdataAddr_ = (uint16_t)ParseOptHex(ge, "mdata_addr", 0xA400);
	mdataSize_ = 0x800;
	EnsureOpll(1);
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

int CHardMsx::LoadDq(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	titleCode_ = titleCode;
	ge_ = ge;
	genericMode_ = 1;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	chips_ = 0;
	idle_ = 0;
	playing_ = 0;
	if (!LoadCartRom(fs, ge, "code"))
		return 0;
	/* DQ1 = ASCII8 128K; DQ2 = ASCII16 256K (MAME software list). */
	if (_stricmp(ge->subtype, "dq2") == 0 || cartBytes_ > 0x20000u) {
		mapper_ = MAP_ASCII16;
		MapAscii16(0, 0);
		MapAscii16(1, 1);
	} else {
		mapper_ = MAP_ASCII8;
		MapAscii8(0, 0);
		MapAscii8(1, 1);
		MapAscii8(2, 2);
		MapAscii8(3, 3);
	}
	PlantPsgTrampoline(mem_);
	PlantBiosStubs();
	/* Cart header "AB" + init at $4002. Hoot's missing dq1.cpp used a
	   mailbox; until we have that IPL, boot the ROM init and feed song
	   codes through the generic ports. */
	initPc_ = 0x400;
	if (mem_[0x4000] == 'A' && mem_[0x4001] == 'B') {
		const uint16_t init = (uint16_t)(mem_[0x4002] | ((uint16_t)mem_[0x4003] << 8));
		if (init >= 0x4000 && init < 0xC000)
			initPc_ = init;
	}
	if (initPc_ != 0x400) {
		mem_[0x400] = 0xFB; /* EI */
		mem_[0x401] = 0xC3;
		mem_[0x402] = (uint8_t)(initPc_ & 0xff);
		mem_[0x403] = (uint8_t)(initPc_ >> 8);
		initPc_ = 0x400;
	}
	mdataAddr_ = 0xC000;
	mdataSize_ = 0x800;
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

int CHardMsx::StartSongDs4(unsigned titleCode)
{
	if (!cpu_) return 0;
	titleCode_ = titleCode;
	idle_ = 0;
	playing_ = 1;
	sccAccessed_ = 0;
	if ((titleCode & 0x80u) == 0) {
		ioport_[0x00] = 0x01;
		ioport_[0x01] = (uint8_t)(titleCode & 0xff);
	} else {
		mem_[0xc095] = (uint8_t)(titleCode & 0x7fu);
	}
	CEmuHardMsxSetActive(this);
	return 1;
}

int CHardMsx::StartSong(unsigned titleCode)
{
	if (mapper_ == MAP_DS4)
		return StartSongDs4(titleCode);
	if (genericMode_)
		return StartSongGeneric(titleCode);
	return StartSongKss(titleCode);
}

void CEmuHardMsxSetActive(CHardMsx* hw)
{
	CEmuZ80BusSetActive(hw);
}
