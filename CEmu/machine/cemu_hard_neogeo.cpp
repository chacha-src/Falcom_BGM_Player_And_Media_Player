#include "StdAfx.h"
#include "cemu_hard_neogeo.h"
#include "../z80/cemu_z80_bus.h"
#include "../chip/cemu_chip_ym2610.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include <string.h>
#include <stdlib.h>

enum {
	NEO_Z80_HZ = 4000000,
	NEO_YM2610_HZ = 8000000
};

/* Window sizes / base offsets into M1 (wiki.neogeodev Z80 bankswitching). */
static const unsigned kNeoWinSize[4] = { 0x0800, 0x1000, 0x2000, 0x4000 };
static const uint16_t kNeoWinBase[4] = { 0xF000, 0xE000, 0xC000, 0x8000 };

/* ---- CMC50 M1 decrypt (MAME prot_cmc.cpp cmc50_m1_decrypt) ---- */
static const uint8_t kCmc50M1Addr8_15Xor[256] = {
	0x0a,0x72,0xb7,0xaf,0x67,0xde,0x1d,0xb1,0x78,0xc4,0x4f,0xb5,0x4b,0x18,0x76,0xdd,
	0x11,0xe2,0x36,0xa1,0x82,0x03,0x98,0xa0,0x10,0x5f,0x3f,0xd6,0x1f,0x90,0x6a,0x0b,
	0x70,0xe0,0x64,0xcb,0x9f,0x38,0x8b,0x53,0x04,0xca,0xf8,0xd0,0x07,0x68,0x56,0x32,
	0xae,0x1c,0x2e,0x48,0x63,0x92,0x9a,0x9c,0x44,0x85,0x41,0x40,0x09,0xc0,0xc8,0xbf,
	0xea,0xbb,0xf7,0x2d,0x99,0x21,0xf6,0xba,0x15,0xce,0xab,0xb0,0x2a,0x60,0xbc,0xf1,
	0xf0,0x9e,0xd5,0x97,0xd8,0x4e,0x14,0x9d,0x42,0x4d,0x2c,0x5c,0x2b,0xa6,0xe1,0xa7,
	0xef,0x25,0x33,0x7a,0xeb,0xe7,0x1b,0x6d,0x4c,0x52,0x26,0x62,0xb6,0x35,0xbe,0x80,
	0x01,0xbd,0xfd,0x37,0xf9,0x47,0x55,0x71,0xb4,0xf2,0xff,0x27,0xfa,0x23,0xc9,0x83,
	0x17,0x39,0x13,0x0d,0xc7,0x86,0x16,0xec,0x49,0x6f,0xfe,0x34,0x05,0x8f,0x00,0xe6,
	0xa4,0xda,0x7b,0xc1,0xf3,0xf4,0xd9,0x75,0x28,0x66,0x87,0xa8,0x45,0x6c,0x20,0xe9,
	0x77,0x93,0x7e,0x3c,0x1e,0x74,0xf5,0x8c,0x3e,0x94,0xd4,0xc2,0x5a,0x06,0x0e,0xe8,
	0x3d,0xa9,0xb2,0xe3,0xe4,0x22,0xcf,0x24,0x8e,0x6b,0x8a,0x8d,0x84,0x4a,0xd2,0x91,
	0x88,0x79,0x57,0xa5,0x0f,0xcd,0xb9,0xac,0x3b,0xaa,0xb3,0xd1,0xee,0x31,0x81,0x7c,
	0xd7,0x89,0xd3,0x96,0x43,0xc5,0xc6,0xc3,0x69,0x7f,0x46,0xdf,0x30,0x5b,0x6e,0xe5,
	0x08,0x95,0x9b,0xfb,0xb8,0x58,0x0c,0x61,0x50,0x5d,0x3a,0xa2,0x29,0x12,0xfc,0x51,
	0x7d,0x1a,0x02,0x65,0x54,0x5e,0x19,0xcc,0xdc,0xdb,0x73,0xed,0xad,0x59,0x2f,0xa3
};
static const uint8_t kCmc50M1Addr0_7Xor[256] = {
	0xf4,0xbc,0x02,0xf7,0x2c,0x3d,0xe8,0xd9,0x50,0x62,0xec,0xbd,0x53,0x73,0x79,0x61,
	0x00,0x34,0xcf,0xa2,0x63,0x28,0x90,0xaf,0x44,0x3b,0xc5,0x8d,0x3a,0x46,0x07,0x70,
	0x66,0xbe,0xd8,0x8b,0xe9,0xa0,0x4b,0x98,0xdc,0xdf,0xe2,0x16,0x74,0xf1,0x37,0xf5,
	0xb7,0x21,0x81,0x01,0x1c,0x1b,0x94,0x36,0x09,0xa1,0x4a,0x91,0x30,0x92,0x9b,0x9a,
	0x29,0xb1,0x38,0x4d,0x55,0xf2,0x56,0x18,0x24,0x47,0x9d,0x3f,0x80,0x1f,0x22,0xa4,
	0x11,0x54,0x84,0x0d,0x25,0x48,0xee,0xc6,0x59,0x15,0x03,0x7a,0xfd,0x6c,0xc3,0x33,
	0x5b,0xc4,0x7b,0x5a,0x05,0x7f,0xa6,0x40,0xa9,0x5d,0x41,0x8a,0x96,0x52,0xd3,0xf0,
	0xab,0x72,0x10,0x88,0x6f,0x95,0x7c,0xa8,0xcd,0x9c,0x5f,0x32,0xae,0x85,0x39,0xac,
	0xe5,0xd7,0xfb,0xd4,0x08,0x23,0x19,0x65,0x6b,0xa7,0x93,0xbb,0x2b,0xbf,0xb8,0x35,
	0xd0,0x06,0x26,0x68,0x3e,0xdd,0xb9,0x69,0x2a,0xb2,0xde,0x87,0x45,0x58,0xff,0x3c,
	0x9e,0x7d,0xda,0xed,0x49,0x8c,0x14,0x8e,0x75,0x2f,0xe0,0x6e,0x78,0x6d,0x20,0xd2,
	0xfa,0x2d,0x51,0xcc,0xc7,0xe7,0x1d,0x27,0x97,0xfc,0x31,0xdb,0xf8,0x42,0xe3,0x99,
	0x5e,0x83,0x0e,0xb4,0x2e,0xf6,0xc0,0x0c,0x4c,0x57,0xb6,0x64,0x0a,0x17,0xa3,0xc1,
	0x77,0x12,0xfe,0xe6,0x8f,0x13,0x71,0xe4,0xf9,0xad,0x9f,0xce,0xd5,0x89,0x7e,0x0f,
	0xc2,0x86,0xf3,0x67,0xba,0x60,0x43,0xc9,0x04,0xb3,0xb0,0x1e,0xb5,0xc8,0xeb,0xa5,
	0x76,0xea,0x5c,0x82,0x1a,0x4f,0xaa,0xca,0xe1,0x0b,0x4e,0xcb,0x6a,0xef,0xd1,0xd6
};

static unsigned CEmuNeoBitSwap16(unsigned v, const int* o)
{
	unsigned r = 0;
	for (int i = 0; i < 16; i++)
		if (v & (1u << o[i])) r |= 1u << (15 - i);
	return r;
}

static int CEmuNeoCmc50M1Scramble(int address, uint16_t key)
{
	static const int p1[8][16] = {
		{15,14,10,7,1,2,3,8,0,12,11,13,6,9,5,4},
		{7,1,8,11,15,9,2,3,5,13,4,14,10,0,6,12},
		{8,6,14,3,10,7,15,1,4,0,2,5,13,11,12,9},
		{2,8,15,9,3,4,11,7,13,6,0,10,1,12,14,5},
		{1,13,6,15,14,3,8,10,9,4,7,12,5,2,0,11},
		{11,15,3,4,7,0,9,2,6,14,12,1,8,5,10,13},
		{10,5,13,8,6,15,1,14,11,9,3,0,12,7,4,2},
		{9,3,7,0,2,12,4,11,14,10,5,8,15,13,1,6},
	};
	static const int kKeyOrder[16] = {12,0,2,4,8,15,7,13,10,1,3,6,11,9,14,5};
	static const int kFinalOrder[16] = {7,15,14,6,5,13,12,4,11,3,10,2,9,1,8,0};
	const int block = (address >> 16) & 7;
	unsigned aux = (unsigned)address & 0xffffu;
	aux ^= CEmuNeoBitSwap16(key, kKeyOrder);
	{
		int ord[16];
		for (int i = 0; i < 16; i++)
			ord[i] = p1[block][15 - i];
		aux = CEmuNeoBitSwap16(aux, ord);
	}
	aux ^= kCmc50M1Addr0_7Xor[(aux >> 8) & 0xff];
	aux ^= (unsigned)kCmc50M1Addr8_15Xor[aux & 0xff] << 8;
	aux = CEmuNeoBitSwap16(aux, kFinalOrder);
	return (block << 16) | (int)aux;
}

/* In-place CMC50 address scramble for encrypted 512K M1 (mslug5 etc.).
   MAME cmc50_m1_decrypt: key = byte sum of the first 64KiB (not word sum). */
static void CEmuNeoCmc50M1Decrypt(uint8_t* rom, unsigned size)
{
	if (!rom || size < 0x80000u) return;
	/* Skip if already looks like a Z80 reset vector (decrypted dumps). */
	if (rom[0] == 0xc3 || rom[0] == 0xf3 || rom[0] == 0x31)
		return;
	uint16_t key = 0;
	for (unsigned i = 0; i < 0x10000u; i++)
		key = (uint16_t)(key + rom[i]);
	uint8_t* buf = (uint8_t*)malloc(0x80000u);
	if (!buf) return;
	for (unsigned i = 0; i < 0x80000u; i++)
		buf[i] = rom[CEmuNeoCmc50M1Scramble((int)i, key)];
	memcpy(rom, buf, 0x80000u);
	free(buf);
}

CHardNeo::CHardNeo()
	: cpuHz_(NEO_Z80_HZ)
	, ymHz_(NEO_YM2610_HZ)
	, m1Rom_(NULL)
	, m1Size_(0)
	, cpu_(NULL)
	, chip_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, soundCmd_(0)
	, soundCmdPending_(0)
	, nmiPulse_(0)
	, nmiEnabled_(0)
	, nmiDelivered_(0)
	, adpcmA_(NULL)
	, adpcmASize_(0)
	, adpcmB_(NULL)
	, adpcmBSize_(0)
{
	hardKind = KIND_NEO;
	memset(mem_, 0, sizeof(mem_));
	memset(bank_, 0, sizeof(bank_));
}

CHardNeo::~CHardNeo()
{
	Shutdown();
}

static int CEmuNeoHasYm2610(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->chipCount; i++)
		if (ge->chipIds[i] == CEMU_CHIP_YM2610)
			return 1;
	return 0;
}

static int IsNeoPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "neogeo") == 0 || _stricmp(ge->subtype, "neogeo") == 0)
		return 1;
	/* Mis-tagged MVS sets: platform=snk subtype=generic + YM2610. */
	if (_stricmp(ge->platform, "snk") == 0
		&& (_stricmp(ge->subtype, "generic") == 0 || ge->subtype[0] == 0)
		&& CEmuNeoHasYm2610(ge))
		return 1;
	return 0;
}

int CHardNeo::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsNeoPlatform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = NEO_Z80_HZ;
	ymHz_ = NEO_YM2610_HZ;
	chip_ = CEmuChipYm2610Create((uint32_t)ymHz_, sampleRate_);
	cpu_ = new Ay_Cpu();
	return (chip_ && cpu_) ? 1 : 0;
}

void CHardNeo::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chip_) {
		CEmuChipYm2610Destroy(chip_);
		chip_ = NULL;
	}
	if (m1Rom_) { free(m1Rom_); m1Rom_ = NULL; m1Size_ = 0; }
	if (adpcmA_) { free(adpcmA_); adpcmA_ = NULL; adpcmASize_ = 0; }
	if (adpcmB_) { free(adpcmB_); adpcmB_ = NULL; adpcmBSize_ = 0; }
}

uint8_t CHardNeo::ReadM1(uint32_t off) const
{
	if (!m1Rom_ || m1Size_ == 0) return 0xff;
	if (off >= m1Size_) return 0xff;
	return m1Rom_[off];
}

void CHardNeo::SetBankWindow(int window, uint8_t bank)
{
	if (window < 0 || window > 3) return;
	bank_[window] = bank;
	const unsigned winSize = kNeoWinSize[window];
	const uint16_t winBase = kNeoWinBase[window];
	const uint32_t romOff = (uint32_t)bank * winSize;
	for (unsigned i = 0; i < winSize; i++)
		mem_[winBase + i] = ReadM1(romOff + i);
}

void CHardNeo::SetSoundCommand(uint8_t cmd)
{
	soundCmd_ = cmd;
	soundCmdPending_ = 1;
	/* Hardware: 68K latch write asserts NMI only while the Z80 has enabled
	   it via OUT $08. Do not force-enable — early M1 boots with OUT $18 and
	   a forced NMI storm corrupts SP/RAM (mslug nmiN thousands). */
	if (nmiEnabled_)
		nmiPulse_ = 1;
}

uint8_t CHardNeo::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	const uint8_t hi = (uint8_t)(port >> 8);

	/* Sound latch — also mirrors on $C0 (decode mask $0C). */
	if ((p & 0x0c) == 0x00 && (p & 0x03) == 0x00) {
		soundCmdPending_ = 0;
		return soundCmd_;
	}
	if (p >= 0x04 && p <= 0x07) {
		if (!chip_) return 0x00;
		switch (p) {
		case 0x04: return chip_->ReadStatus();
		case 0x05: return chip_->ReadData();
		case 0x06: return chip_->ReadStatusHi();
		default: return chip_->ReadDataHi();
		}
	}
	/* NEO-ZMC bank select: IN with bank in A15..A8 */
	if (p >= 0x08 && p <= 0x0b) {
		SetBankWindow(p - 0x08, hi);
		return 0x00;
	}
	/* $18..$1B mirror bank reads on some docs; ignore */
	return 0xff;
}

void CHardNeo::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if ((p & 0x0c) == 0x00 && (p & 0x03) == 0x00) {
		/* Clear sound code from Z80 side */
		soundCmd_ = 0;
		soundCmdPending_ = 0;
		return;
	}
	if (p >= 0x04 && p <= 0x07) {
		if (chip_) chip_->Write(p - 0x04, data);
		return;
	}
	if ((p & 0x1c) == 0x08) {
		/* Enable NMIs. Only the rising edge of enable (with a pending latch)
		   asserts NMI — mslug's NMI prologue OUT $08 while pending would
		   otherwise re-pulse every instruction → nested NMI storm. */
		const int was = nmiEnabled_;
		nmiEnabled_ = 1;
		if (!was && soundCmdPending_)
			nmiPulse_ = 1;
		return;
	}
	if ((p & 0x1c) == 0x18) {
		nmiEnabled_ = 0;
		return;
	}
	if (p == 0x0c) {
		/* Reply byte to 68K — unused for playback */
		(void)data;
		return;
	}
}

void CHardNeo::MemWrite(uint16_t addr, uint8_t data)
{
	/* Work RAM is $F800-$FFFF. Early SNK M1 also parks channel/timer scratch
	   in $FC00-$FCFF (inside bank window 0) — allow those writes so song
	   setup sticks; ROM tables in $F000-$FBFF stay read-only from identity. */
	if (addr >= 0xF800 || (addr >= 0xFC00 && addr <= 0xFCFF))
		mem_[addr] = data;
}

uint8_t CHardNeo::MemRead(uint16_t addr)
{
	return mem_[addr];
}

static void CEmuNeoZipBaseName(const char* name, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!name) return;
	const char* base = name;
	for (const char* p = name; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}
	strncpy_s(out, (size_t)outCap, base, _TRUNCATE);
}

static int CEmuNeoContainsI(const char* s, const char* needle)
{
	if (!s || !needle || !needle[0]) return 0;
	const size_t nl = strlen(needle);
	for (const char* p = s; *p; p++)
		if (_strnicmp(p, needle, nl) == 0)
			return 1;
	return 0;
}

static int CEmuNeoZ80Score(const char* name, const char* type, unsigned sz)
{
	/* M1 can be up to 4MiB (banked). */
	if (!name || !sz || sz > 0x400000u) return -1000;
	char base[CEMU_ZIP_PATH];
	CEmuNeoZipBaseName(name, base, (int)sizeof(base));
	int score = 0;
	if (type && (_stricmp(type, "code") == 0 || _stricmp(type, "sub") == 0)) score += 40;
	if (CEmuNeoContainsI(base, "m1")) score += 100;
	if (CEmuNeoContainsI(base, "z80") || CEmuNeoContainsI(base, "sound")) score += 30;
	if (CEmuNeoContainsI(base, "v1") || CEmuNeoContainsI(base, "v2")
		|| CEmuNeoContainsI(base, "v3") || CEmuNeoContainsI(base, "v4"))
		score -= 100;
	return score;
}

static int CEmuNeoAdpcmScore(const char* name, const char* type, unsigned sz)
{
	if (!name || !sz) return -1000;
	char base[CEMU_ZIP_PATH];
	CEmuNeoZipBaseName(name, base, (int)sizeof(base));
	int score = 0;
	if (type && (_stricmp(type, "voice") == 0 || _stricmp(type, "adpcm") == 0
		|| _stricmp(type, "adpcma") == 0 || _stricmp(type, "adpcmb") == 0
		|| _stricmp(type, "pcm") == 0))
		score += 80;
	if (CEmuNeoContainsI(base, "v1") || CEmuNeoContainsI(base, "v2")
		|| CEmuNeoContainsI(base, "v3") || CEmuNeoContainsI(base, "v4"))
		score += 120;
	if (CEmuNeoContainsI(base, "adpcm") || CEmuNeoContainsI(base, "pcm") || CEmuNeoContainsI(base, "voice"))
		score += 80;
	if (CEmuNeoContainsI(base, "m1")) score -= 120;
	return score ? score : -100;
}

static int CEmuNeoPlace(uint8_t** dst, unsigned* dstSize, unsigned offset,
	const uint8_t* data, unsigned size)
{
	if (!dst || !dstSize || !data || !size) return 0;
	const unsigned need = offset + size;
	if (need < offset) return 0;
	if (need > *dstSize) {
		uint8_t* p = (uint8_t*)realloc(*dst, need);
		if (!p) return 0;
		if (need > *dstSize)
			memset(p + *dstSize, 0, need - *dstSize);
		*dst = p;
		*dstSize = need;
	}
	memcpy(*dst + offset, data, size);
	return 1;
}

int CHardNeo::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge || !cpu_ || !chip_) return 0;
	memset(mem_, 0, sizeof(mem_));
	memset(bank_, 0, sizeof(bank_));
	if (m1Rom_) { free(m1Rom_); m1Rom_ = NULL; m1Size_ = 0; }
	if (adpcmA_) { free(adpcmA_); adpcmA_ = NULL; adpcmASize_ = 0; }
	if (adpcmB_) { free(adpcmB_); adpcmB_ = NULL; adpcmBSize_ = 0; }

	int loaded = 0;
	int best = -1, bestScore = -1000;
	if (!ge->rom && ge->romCount > 0) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || data == (const unsigned char*)1 || !sz) continue;
		const int zsc = CEmuNeoZ80Score(r->name, r->type, sz);
		if (zsc > bestScore) {
			bestScore = zsc;
			best = i;
		}
		const int asc = CEmuNeoAdpcmScore(r->name, r->type, sz);
		if (asc > 0) {
			const unsigned off = (r->offset > 0) ? (unsigned)r->offset : 0u;
			const int isB = (r->type[0] && _stricmp(r->type, "adpcmb") == 0) ? 1 : 0;
			if (isB)
				CEmuNeoPlace(&adpcmB_, &adpcmBSize_, off, data, sz);
			else
				CEmuNeoPlace(&adpcmA_, &adpcmASize_, off, data, sz);
		}
	}
	if (best >= 0 && bestScore > 0) {
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, ge->rom[best].name, &sz);
		if (data && data != (const unsigned char*)1 && sz) {
			m1Rom_ = (uint8_t*)malloc(sz);
			if (m1Rom_) {
				memcpy(m1Rom_, data, sz);
				m1Size_ = sz;
				loaded = 1;
			}
		}
	}
	if (!loaded) {
		best = -1;
		bestScore = -1000;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const int sc = CEmuNeoZ80Score(pathA, "", fs->files[i].size);
			if (sc <= bestScore || !fs->files[i].data) continue;
			bestScore = sc;
			best = i;
		}
		if (best >= 0 && bestScore > 0 && fs->files[best].data) {
			m1Size_ = fs->files[best].size;
			m1Rom_ = (uint8_t*)malloc(m1Size_);
			if (m1Rom_) {
				memcpy(m1Rom_, fs->files[best].data, m1Size_);
				loaded = 1;
			} else {
				m1Size_ = 0;
			}
		}
	}
	if (!adpcmASize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (CEmuNeoAdpcmScore(pathA, "", fs->files[i].size) <= 0 || !fs->files[i].data)
				continue;
			CEmuNeoPlace(&adpcmA_, &adpcmASize_, adpcmASize_, fs->files[i].data, fs->files[i].size);
		}
	}
	if (!loaded && !adpcmASize_) return 0;

	/* CMC50 encrypted M1 (mslug5 / kof2000-class 512K): descramble before map. */
	if (m1Rom_ && m1Size_ >= 0x80000u)
		CEmuNeoCmc50M1Decrypt(m1Rom_, m1Size_);

	/* Fixed bank $0000-$7FFF = first 32KiB of M1 */
	const unsigned fix = m1Size_ < 0x8000u ? m1Size_ : 0x8000u;
	if (m1Rom_ && fix)
		memcpy(mem_, m1Rom_, fix);
	/*
	 * Identity-map $8000-$FFFF from M1 (bank = addr/windowSize).
	 * Early SNK drivers (bstars/cyberlip/nam1975/…) never IN $08-$0B — they
	 * expect a flat 64KiB view so song tables at $E314/$F4DA resolve. Later
	 * KOF-family code rebanks via IN immediately after boot.
	 */
	if (m1Rom_ && m1Size_ > 0x8000u) {
		const unsigned tail = m1Size_ < 0x10000u ? (m1Size_ - 0x8000u) : 0x8000u;
		memcpy(mem_ + 0x8000, m1Rom_ + 0x8000, tail);
		if (tail < 0x8000u)
			memset(mem_ + 0x8000 + tail, 0xff, 0x8000u - tail);
	} else {
		memset(mem_ + 0x8000, 0xff, 0x8000);
	}
	bank_[0] = 0x1e; /* $F000 / 2KiB */
	bank_[1] = 0x0e; /* $E000 / 4KiB */
	bank_[2] = 0x06; /* $C000 / 8KiB */
	bank_[3] = 0x02; /* $8000 / 16KiB */
	memset(mem_ + 0xF800, 0, 0x800);

	chip_->Reset();
	if (adpcmASize_) chip_->SetAdpcmRom(adpcmA_, adpcmASize_, 0);
	if (adpcmBSize_) {
		chip_->SetPcmRom(adpcmB_, adpcmBSize_);
		chip_->SetAdpcmB(adpcmB_, adpcmBSize_, 0);
	} else if (adpcmASize_) {
		chip_->SetPcmRom(adpcmA_, adpcmASize_);
	}
	cpu_->reset(mem_);
	cpu_->r.pc = 0;
	cpuCycles_ = 0;
	soundCmd_ = 0;
	soundCmdPending_ = 0;
	nmiPulse_ = 0;
	nmiEnabled_ = 0;
	nmiDelivered_ = 0;
	return 1;
}

void CEmuHardNeoSetActive(CHardNeo* hw)
{
	CEmuZ80BusSetActive(hw);
}
