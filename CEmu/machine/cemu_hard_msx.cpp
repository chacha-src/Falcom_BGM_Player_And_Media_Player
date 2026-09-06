#include "StdAfx.h"
#include "cemu_hard_msx.h"
#include "../cemu_mgr.h"
#include "../cemu_zipfs.h"
#include "../chip/cemu_chip_ay.h"
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

static int IsMsxPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "msx") == 0) return 1;
	if (_stricmp(ge->dataDir, "msx") == 0) return 1;
	if (_stricmp(ge->subtype, "kss") == 0 || _stricmp(ge->subtype, "opll") == 0) return 1;
	if (_stricmp(ge->subtype, "generic") == 0 && _stricmp(ge->platform, "msx") == 0) return 1;
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
{
	hardKind = KIND_MSX;
	memset(mem_, 0, sizeof(mem_));
	memset(rom_, 0, sizeof(rom_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
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
	CEmuChipAySetUnmuteAssist(chipAy_, 1); /* MSX: 無音曲向けアシスト */
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
	if (chipOpll_) {
		OPLL_delete((OPLL*)chipOpll_);
		chipOpll_ = NULL;
	}
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

void CHardMsx::ApplyBank(uint8_t bankSel)
{
	if (!bank_ || bankNum_ == 0) return;
	const int bankno = (int)bankSel - (int)bankOfs_;
	if (bankno < 0 || bankno >= (int)bankNum_)
		return;
	if (bank8k_) {
		(void)bankno;
	} else {
		const unsigned off = (unsigned)bankno * 0x4000u;
		if (off + 0x4000u <= bankBytes_)
			memcpy(mem_ + 0x8000, bank_ + off, 0x4000);
	}
}

void CHardMsx::MapDefault()
{
}

uint8_t CHardMsx::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
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
	return ioport_[p];
}

void CHardMsx::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (p == 0x7c || p == 0x7d || p == 0xc0 || p == 0xc1 || p == 0xf0 || p == 0xf1) {
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

	CEmuMsxMergeCompanions(fs, ge);

	int loadedCode = 0;
	int useOpll = ParseOptHex(ge, "use_opll", 0);
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		if (_stricmp(r->type, "code") == 0 || _stricmp(r->type, "fmbios") == 0) {
			int off = r->offset;
			/* winsltn FMPAC.ROM @0 clobbers page0 BIOS stubs → silent WEAK.
			   Remap that title only; yosikon still probes the ROM near 0. */
			if (_stricmp(r->type, "fmbios") == 0 && off <= 0
				&& ge->archive[0] && _strnicmp(ge->archive, "winsltn", 7) == 0)
				off = 0x4000;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			memcpy(mem_ + off, data, n);
			loadedCode++;
			if (_stricmp(r->type, "fmbios") == 0)
				chips_ |= CHIP_FMPAC;
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
	EnsureOpll(useOpll ? 1 : 0);

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
	sccEnable_ = (!(chips_ & CHIP_SCCDISABLE)
		&& ((chips_ & (CHIP_SNG | CHIP_GGSTEREO)) != CHIP_GGSTEREO)) ? 1 : 0;

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

	ioport_[PLAY_CODE_PORT] = (uint8_t)(titleCode & 0xff);
	PortOut(0xfe, 0);

	cpu_->reset(mem_);
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 0;

	CEmuHardMsxSetActive(this);
	int guard = 0;
	while (!idle_ && guard++ < 2000000) {
		const int cyc = Ay_CpuRunOne(cpu_);
		if (cyc <= 0) break;
		cpuCycles_ += (uint64_t)cyc;
	}
	playing_ = 1;
	return 1;
}

int CHardMsx::StartSongGeneric(unsigned titleCode)
{
	if (!cpu_) return 0;
	titleCode_ = titleCode;
	unsigned song = titleCode & 0xff;
	unsigned hi = (titleCode >> 8) & 0xff;
	/* Catalog titles sometimes pack song in high byte (aleste2 0x0115). */
	if (hi && !bgmPresent_[song] && bgmPresent_[hi])
		song = hi;
	if (hi && song == 0)
		song = hi;

	/* Defer StageBgm until after init settle when mdata overlays a code
	   image the patch still needs (Tokuma FMPAC @A000; Telenet alba2/valis2
	   TSTI/IPL89 @4000 with mdata_addr=4000 — early StageBgm zeroed the
	   LDIR source and left CALL AC06 in a NOP sled → pc≈F91F). */
	const int deferBgm = (mdataAddr_ >= 0x4000 && mdataAddr_ < 0xE000);

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
	   - Compile/jngolf:    port7 bit0 = OPLL present */
	ioport_[0x00] = (uint8_t)(song & 0xff);
	ioport_[0x02] = 0x01; /* play command (seen via playCmdPending after settle) */
	ioport_[0x03] = (uint8_t)(song & 0xff);
	ioport_[0x04] = (uint8_t)(song & 0xff);
	ioport_[0x07] = (chips_ & CHIP_FMPAC) ? 0x01 : 0x00;
	ioport_[PLAY_CODE_PORT] = (uint8_t)(song & 0xff);
	playCmdPending_ = 0; /* arm only after settle */
	titleCode_ = song;

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpu_->r.sp = 0xF380;
	cpu_->r.iff1 = 1;
	cpu_->r.im = 1;
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 1;

	/* Brief settle so init installs handlers before play edge. */
	CEmuHardMsxSetActive(this);
	int guard = 0;
	while (guard++ < 200000) {
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
	/* MSX BIOS IRQ @0038 → CALL H.TIMI (FD9F) → EI;RET.
	   Some patches (rona/ryukyu/sugo8/shngh2) install the JP hook only
	   *after* the play edge, so a one-shot "wire if already C3" misses them.
	   Plant RET at FD9F when still zero (avoids undead NOP-sled), then always
	   wire 0038 — late JP overwrites the RET and IRQs start ticking. Body
	   @00E0 keeps driver@0100 free. */
	if (mem_[0xFD9F] == 0x00)
		mem_[0xFD9F] = 0xC9;
	mem_[0x0038] = 0xC3;
	mem_[0x0039] = 0xE0;
	mem_[0x003A] = 0x00; /* JP 00E0 */
	mem_[0x00E0] = 0xF5;             /* PUSH AF */
	mem_[0x00E1] = 0xCD; mem_[0x00E2] = 0x9F; mem_[0x00E3] = 0xFD; /* CALL FD9F */
	mem_[0x00E4] = 0xF1;             /* POP AF */
	mem_[0x00E5] = 0xFB;             /* EI */
	mem_[0x00E6] = 0xC9;             /* RET */
	/* One-shot play after handlers exist (port2/3/4 mailboxes). */
	playCmdPending_ = 8; /* a few edges; not sticky-forever */
	ioport_[0x02] = 0x01;
	ioport_[0x03] = (uint8_t)(song & 0xff);
	ioport_[0x04] = (uint8_t)(song & 0xff);
	ioport_[0x07] = (chips_ & CHIP_FMPAC) ? 0x01 : 0x00;
	idle_ = 0;
	return 1;
}

int CHardMsx::StartSong(unsigned titleCode)
{
	if (genericMode_)
		return StartSongGeneric(titleCode);
	return StartSongKss(titleCode);
}

void CEmuHardMsxSetActive(CHardMsx* hw)
{
	CEmuZ80BusSetActive(hw);
}
