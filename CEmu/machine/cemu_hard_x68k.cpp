#include "StdAfx.h"
#include "cemu_hard_x68k.h"
#include "cemu_x68k_dos.h"
#include "cemu_m68k_bus.h"
#include "../chip/cemu_chip_opm.h"
#include "../fmmon/fmmon_shadow.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>

static int CEmuX68kIntAck(int level)
{
	(void)level;
	CHardX68k* hw = CEmuM68kBusGetX68k();
	if (hw && hw->SoundChip())
		hw->SoundChip()->AckIrq();
	/* Pulse: drop the Musashi line on ack. Holding IRQ6 through the
	   trampoline's jsr/ISR lets move #$2500,sr (or any IPL≤5) re-enter
	   immediately and walk SSP down through the DOS image @ $F08700. */
	m68k_set_irq(M68K_IRQ_NONE);
	return M68K_INT_ACK_AUTOVECTOR;
}

void CEmuHardX68kSetActive(CHardX68k* hw)
{
	CEmuM68kBusSetX68k(hw);
	if (hw)
		m68k_set_int_ack_callback(CEmuX68kIntAck);
	else
		m68k_set_int_ack_callback(NULL);
}

CHardX68k* CEmuHardX68kGetActive()
{
	return CEmuM68kBusGetX68k();
}

CHardX68k::CHardX68k()
	: cpuHz_(10000000)
	, opmHz_(4000000)
	, opmWrites_(0)
	, chip_(NULL)
	, sampleRate_(44100)
	, ymAddr_(0)
	, songFlag_(0)
	, songCode_(0)
	, pc_(0)
	, fetchCount_(0)
	, musashiReady_(0)
	, adpcmPlaying_(0)
	, adpcmAddr_(0)
	, adpcmSize_(0)
	, adpcmPos_(0)
	, adpcmSignal_(0)
	, adpcmStep_(0)
	, adpcmNibble_(0)
	, adpcmRateHz_(15600)
	, adpcmPan_(0)
	, adpcmPpi_(0x08)
	, adpcmPhase_(0)
	, dosFileCount_(0)
	, dosMbA1_(0)
	, dosMbD0_(0)
	, dosMbD1_(0)
	, dosMbResult_(0)
	, softMfp_(0)
{
	hardKind = KIND_X68K;
	memset(rom_, 0, sizeof(rom_));
	memset(ram_, 0, sizeof(ram_));
	memset(high_, 0, sizeof(high_));
	memset(mid_, 0, sizeof(mid_));
	memset(mfp_, 0, sizeof(mfp_));
	memset(dosFiles_, 0, sizeof(dosFiles_));
	memset(dosHandles_, 0, sizeof(dosHandles_));
	for (int i = 0; i < kDosHandles; i++)
		dosHandles_[i].file = -1;
}

CHardX68k::~CHardX68k()
{
	Shutdown();
}

int CHardX68k::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "x68k") != 0 && _stricmp(ge->dataDir, "x68k") != 0)
		return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = 10000000;
	opmHz_ = 4000000;
	softMfp_ = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "mfp") == 0 && strtoul(ge->opt[i].value, NULL, 0))
			softMfp_ = 1;
	}
	chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
	opmWrites_ = 0;
	musashiReady_ = 0;
	return chip_ ? 1 : 0;
}

void CHardX68k::Shutdown()
{
	if (CEmuM68kBusGetX68k() == this)
		CEmuM68kBusSetX68k(NULL);
	if (chip_) {
		CEmuChipYm2151Destroy(chip_);
		chip_ = NULL;
	}
	musashiReady_ = 0;
}

void CHardX68k::SetSongCommand(unsigned code)
{
	songCode_ = (uint16_t)(code & 0xffff);
	songFlag_ = 0x01;
}

uint8_t* CHardX68k::HighPtr(unsigned addr24)
{
	const unsigned page = addr24 & 0xff0000u;
	if (page == 0xff0000u || page == 0x1f0000u)
		return &high_[addr24 & 0xffffu];
	return NULL;
}

const uint8_t* CHardX68k::HighPtr(unsigned addr24) const
{
	const unsigned page = addr24 & 0xff0000u;
	if (page == 0xff0000u || page == 0x1f0000u)
		return &high_[addr24 & 0xffffu];
	return NULL;
}

uint8_t CHardX68k::Read8(unsigned addr)
{
	addr &= 0xffffffu;
	if (addr < (unsigned)kRomBytes)
		return rom_[addr];
	if (addr >= 0x100000u && addr < 0x100000u + (unsigned)kMidBytes)
		return mid_[addr - 0x100000u];
	if (addr >= 0xf00000u && addr <= 0xf0ffffu)
		return ram_[addr - 0xf00000u];
	if (const uint8_t* h = HighPtr(addr))
		return *h;
	if (addr == 0xe00000u)
		return songFlag_;
	if (addr == 0xe00001u)
		return (uint8_t)(songCode_ & 0xff);
	if (addr == 0xe00002u)
		return (uint8_t)((songCode_ >> 8) & 0xff);
	/* DOS file-op result mailbox $E00018..$E0001B (big-endian long). */
	if (addr >= 0xe00018u && addr <= 0xe0001bu) {
		const unsigned sh = (3u - (addr - 0xe00018u)) * 8u;
		return (uint8_t)((dosMbResult_ >> sh) & 0xffu);
	}
	/* YM2151 status (odd ports); also alias second OPM slot $E9200x. */
	if (addr == 0xe90003u || addr == 0xe90001u || addr == 0xe92003u || addr == 0xe92001u)
		return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0;
	if (addr == 0xe9a005u || addr == 0xe9a007u)
		return adpcmPpi_;
	if (addr == 0xe9a001u || addr == 0xe9a003u)
		return (uint8_t)(adpcmPlaying_ ? 0x08 : 0x00);
	if (addr == 0xe00800u) {
		if (musashiReady_)
			m68k_end_timeslice();
		return 0;
	}
	/* Soft MFP ($E88000 / $E8A000) — optional. */
	if ((addr >= 0xe88000u && addr <= 0xe88fffu) || (addr >= 0xe8a000u && addr <= 0xe8afffu)) {
		const unsigned off = addr & 0xfffu;
		uint8_t v = mfp_[off];
		if (v == 0) v = 0xff;
		/* GPIP ($E88001): arcus/others btst #4 / bne-wait until clear.
		   Soft-MFP defaults were 0xFF → eternal spin before OPM key-ons. */
		if (off == 0x001u)
			v = (uint8_t)(v & (uint8_t)~0x10);
		(void)softMfp_;
		return v;
	}
	if (addr >= 0xe80000u && addr <= 0xefffffu)
		return 0xff;
	return 0;
}

uint16_t CHardX68k::Read16(unsigned addr)
{
	addr &= 0xffffffu;
	return (uint16_t)((Read8(addr) << 8) | Read8((addr + 1) & 0xffffffu));
}

uint32_t CHardX68k::Read32(unsigned addr)
{
	addr &= 0xffffffu;
	return ((uint32_t)Read16(addr) << 16) | (uint32_t)Read16((addr + 2) & 0xffffffu);
}

void CHardX68k::Write8(unsigned addr, uint8_t data)
{
	addr &= 0xffffffu;
	if (addr < (unsigned)kRomBytes) {
		rom_[addr] = data;
		return;
	}
	if (addr >= 0x100000u && addr < 0x100000u + (unsigned)kMidBytes) {
		mid_[addr - 0x100000u] = data;
		return;
	}
	if (addr >= 0xf00000u && addr <= 0xf0ffffu) {
		ram_[addr - 0xf00000u] = data;
		return;
	}
	if (uint8_t* h = HighPtr(addr)) {
		*h = data;
		return;
	}
	if (addr == 0xe00000u) {
		songFlag_ = data;
		return;
	}
	/* Guest may poke song code bytes; keep mailbox coherent. */
	if (addr == 0xe00001u) {
		songCode_ = (uint16_t)((songCode_ & 0xff00u) | data);
		return;
	}
	if (addr == 0xe00002u) {
		songCode_ = (uint16_t)((songCode_ & 0x00ffu) | ((uint16_t)data << 8));
		return;
	}
	if ((addr >= 0xe88000u && addr <= 0xe88fffu) || (addr >= 0xe8a000u && addr <= 0xe8afffu)) {
		mfp_[addr & 0xfffu] = data;
		return;
	}
	if (addr == 0xe90001u || addr == 0xe90003u || addr == 0xe92001u || addr == 0xe92003u) {
		if (!chip_) return;
		const int a0 = (int)((addr >> 1) & 1);
		chip_->Write((uint32_t)a0, data);
		if (a0)
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		return;
	}
	/* MSM6258V / DMAC poke (hoot X68kDriver::WriteDev). */
	if (addr == 0xe840c0u) {
		if (data == 0xff)
			adpcmPlaying_ = 0;
		return;
	}
	if (addr == 0xe840cau && musashiReady_) {
		adpcmSize_ = ((unsigned)m68k_get_reg(NULL, M68K_REG_D2) & 0xffffu) << 1;
		adpcmAddr_ = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
		return;
	}
	if (addr == 0xe840ccu && musashiReady_) {
		adpcmAddr_ = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
		return;
	}
	if (addr == 0xe840c7u) {
		if (data == 0x88 && adpcmSize_ > 0) {
			adpcmPlaying_ = 1;
			adpcmPos_ = 0;
			adpcmSignal_ = 0;
			adpcmStep_ = 0;
			adpcmNibble_ = 0;
			adpcmPhase_ = 0;
			if (musashiReady_)
				adpcmAddr_ = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
			/* MSM6258 → FM monitor ADPCM key row (OPM+ADPCM). */
			{
				/* 15.6kHz is the native playback rate, not an audible
				   oscillator frequency. Display it as unity pitch (C4);
				   absolute Hz conversion incorrectly clamped to O10. */
				const unsigned rate = (unsigned)(adpcmRateHz_ > 0 ? adpcmRateHz_ : 15600);
				const int mid = FmMonShadowPitchRateToMidi(
					(unsigned)(((uint64_t)rate * 4096u + 7800u) / 15600u));
				FmMonShadowPcmNote(0, (mid >= 0) ? mid : 60, 1);
			}
		} else {
			adpcmPlaying_ = 0;
			FmMonShadowPcmNote(0, 0, 0);
		}
		return;
	}
	if (addr == 0xe9a005u || addr == 0xe9a007u) {
		adpcmPpi_ = data;
		adpcmPan_ = data & 3;
		switch ((data >> 2) & 3) {
		case 0: adpcmRateHz_ = 7800; break;
		case 1: adpcmRateHz_ = 10400; break;
		default: adpcmRateHz_ = 15600; break;
		}
		if (adpcmPlaying_) {
			const unsigned rate = (unsigned)adpcmRateHz_;
			const int mid = FmMonShadowPitchRateToMidi(
				(unsigned)(((uint64_t)rate * 4096u + 7800u) / 15600u));
			FmMonShadowPcmNote(0, (mid >= 0) ? mid : 60, 1);
		}
		return;
	}
}

/* OKI ADPCM step table (MSM6258 / similar). */
static const int kAdpcmIndexShift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static int kAdpcmDiffLut[49 * 16];
static int kAdpcmLutReady = 0;

static void CEmuX68kAdpcmInitLut()
{
	if (kAdpcmLutReady) return;
	static const int nbl2bit[16][4] = {
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};
	for (int step = 0; step <= 48; step++) {
		int stepval = 16;
		for (int i = 0; i < step; i++)
			stepval = (stepval * 11 + 5) / 10;
		for (int nib = 0; nib < 16; nib++) {
			kAdpcmDiffLut[step * 16 + nib] = nbl2bit[nib][0] *
				(stepval * nbl2bit[nib][1] +
				 stepval / 2 * nbl2bit[nib][2] +
				 stepval / 4 * nbl2bit[nib][3] +
				 stepval / 8);
		}
	}
	kAdpcmLutReady = 1;
}

void CHardX68k::MixAdpcm(int16_t* stereo, int frames)
{
	if (!stereo || frames <= 0 || !adpcmPlaying_ || adpcmSize_ < 1) return;
	CEmuX68kAdpcmInitLut();
	const int rate = adpcmRateHz_ > 0 ? adpcmRateHz_ : 15600;
	const int64_t step = ((int64_t)rate << 16) / (sampleRate_ > 0 ? sampleRate_ : 44100);
	for (int i = 0; i < frames; i++) {
		adpcmPhase_ += step;
		while (adpcmPhase_ >= 0x10000) {
			adpcmPhase_ -= 0x10000;
			if (adpcmPos_ >= adpcmSize_) {
				adpcmPlaying_ = 0;
				FmMonShadowPcmNote(0, 0, 0);
				break;
			}
			unsigned byteAddr = adpcmAddr_ + (adpcmPos_ >> 1);
			uint8_t b = 0x80;
			if (byteAddr < (unsigned)kRomBytes)
				b = rom_[byteAddr];
			else if (byteAddr >= 0x100000u && byteAddr < 0x100000u + (unsigned)kMidBytes)
				b = mid_[byteAddr - 0x100000u];
			const int nib = (adpcmPos_ & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);
			adpcmPos_++;
			int stepIdx = adpcmStep_;
			if (stepIdx < 0) stepIdx = 0;
			if (stepIdx > 48) stepIdx = 48;
			adpcmSignal_ += kAdpcmDiffLut[stepIdx * 16 + nib];
			if (adpcmSignal_ > 2047) adpcmSignal_ = 2047;
			if (adpcmSignal_ < -2048) adpcmSignal_ = -2048;
			stepIdx += kAdpcmIndexShift[nib & 7];
			if (stepIdx < 0) stepIdx = 0;
			if (stepIdx > 48) stepIdx = 48;
			adpcmStep_ = stepIdx;
		}
		if (!adpcmPlaying_) break;
		int32_t s = adpcmSignal_ << 4;
		int32_t l = s, r = s;
		if (adpcmPan_ == 1) r = 0;
		else if (adpcmPan_ == 2) l = 0;
		else if (adpcmPan_ == 3) { l = 0; r = 0; }
		int32_t ol = (int32_t)stereo[i * 2] + l;
		int32_t orr = (int32_t)stereo[i * 2 + 1] + r;
		if (ol > 32767) ol = 32767;
		if (ol < -32768) ol = -32768;
		if (orr > 32767) orr = 32767;
		if (orr < -32768) orr = -32768;
		stereo[i * 2] = (int16_t)ol;
		stereo[i * 2 + 1] = (int16_t)orr;
	}
}

void CHardX68k::Write16(unsigned addr, uint16_t data)
{
	addr &= 0xffffffu;
	/* DOS file-op trigger: fn in low byte (Human68k $3D/$3E/$3F/$4E/…). */
	if (addr == 0xe0001eu) {
		dosMbResult_ = DosFileOp(data & 0xffu, dosMbA1_, dosMbD0_, dosMbD1_);
		return;
	}
	Write8(addr, (uint8_t)(data >> 8));
	Write8((addr + 1) & 0xffffffu, (uint8_t)(data & 0xff));
}

void CHardX68k::Write32(unsigned addr, uint32_t data)
{
	addr &= 0xffffffu;
	if (addr == 0xe00014u) {
		dosMbD1_ = data;
		return;
	}
	if (addr == 0xe00018u) {
		dosMbA1_ = data;
		return;
	}
	if (addr == 0xe0001cu) {
		dosMbD0_ = data;
		return;
	}
	Write16(addr, (uint16_t)(data >> 16));
	Write16((addr + 2) & 0xffffffu, (uint16_t)(data & 0xffff));
}

static int CEmuX68kIsTrapF(const char* name)
{
	if (!name) return 0;
	const char* base = name;
	for (const char* p = name; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}
	return _stricmp(base, "trap_f.bin") == 0;
}

/* Human68k .X (HU) — NetBSD hux.h / aout2hux layout.
   Strip 64-byte header, clear BSS, apply delta-encoded relocs only. */
static int CEmuX68kLoadHumanX(uint8_t* dst, unsigned dstCap, unsigned loadAddr,
	const unsigned char* data, unsigned sz)
{
	if (!dst || !data || sz < 0x40u) return 0;
	if (data[0] != 'H' || data[1] != 'U') return 0;

	const unsigned base = ((unsigned)data[4] << 24) | ((unsigned)data[5] << 16)
		| ((unsigned)data[6] << 8) | (unsigned)data[7];
	const unsigned text = ((unsigned)data[0x0c] << 24) | ((unsigned)data[0x0d] << 16)
		| ((unsigned)data[0x0e] << 8) | (unsigned)data[0x0f];
	const unsigned datasz = ((unsigned)data[0x10] << 24) | ((unsigned)data[0x11] << 16)
		| ((unsigned)data[0x12] << 8) | (unsigned)data[0x13];
	const unsigned bss = ((unsigned)data[0x14] << 24) | ((unsigned)data[0x15] << 16)
		| ((unsigned)data[0x16] << 8) | (unsigned)data[0x17];
	const unsigned rsize = ((unsigned)data[0x18] << 24) | ((unsigned)data[0x19] << 16)
		| ((unsigned)data[0x1a] << 8) | (unsigned)data[0x1b];
	const unsigned body = text + datasz;
	if (body == 0 || 0x40u + body > sz) return 0;
	if (loadAddr >= dstCap) return 0;

	unsigned n = body;
	if (loadAddr + n > dstCap)
		n = dstCap - loadAddr;
	memcpy(dst + loadAddr, data + 0x40, n);

	if (bss && loadAddr + body < dstCap) {
		unsigned bz = bss;
		if (loadAddr + body + bz > dstCap)
			bz = dstCap - (loadAddr + body);
		memset(dst + loadAddr + body, 0, bz);
	}

	/* Delta-encoded relocs: short BE16, or 0x0001 + BE32 long delta. */
	if (rsize && 0x40u + body + rsize <= sz && n == body) {
		const unsigned char* rel = data + 0x40 + body;
		unsigned ri = 0;
		unsigned loc = 0;
		const unsigned delta = loadAddr - base;
		while (ri + 2u <= rsize) {
			unsigned w = ((unsigned)rel[ri] << 8) | (unsigned)rel[ri + 1];
			ri += 2;
			if (w == 1u) {
				if (ri + 4u > rsize) break;
				w = ((unsigned)rel[ri] << 24) | ((unsigned)rel[ri + 1] << 16)
					| ((unsigned)rel[ri + 2] << 8) | (unsigned)rel[ri + 3];
				ri += 4;
			}
			loc += w;
			if (loc + 4u > body) break;
			const unsigned at = loadAddr + loc;
			if (at + 4u > dstCap) break;
			const unsigned old = ((unsigned)dst[at] << 24) | ((unsigned)dst[at + 1] << 16)
				| ((unsigned)dst[at + 2] << 8) | (unsigned)dst[at + 3];
			const unsigned neu = old + delta;
			dst[at] = (uint8_t)(neu >> 24);
			dst[at + 1] = (uint8_t)(neu >> 16);
			dst[at + 2] = (uint8_t)(neu >> 8);
			dst[at + 3] = (uint8_t)(neu);
		}
	}
	return 1;
}

/* SD_DRV.X's IRQ path tests a Human68k resident-state byte at A5+$D28.
   In this ROM-shell use that OS-owned byte remains zero even after the BGM
   command succeeds, so the branch skips the sequencer forever. The following
   instructions immediately perform the normal channel-state checks and are
   safe for the shell; remove only this exact, version-specific gate. */
static void CEmuX68kFixSdDrvHostGate(uint8_t* ram, unsigned loadAddr, unsigned body)
{
	if (!ram || body < 0xfecu) return;
	const unsigned gate = loadAddr + 0xfe4u;
	static const uint8_t sig[] = { 0x4a, 0x2a, 0x0d, 0x28, 0x67, 0x00, 0x02, 0x26 };
	if (gate + sizeof(sig) > loadAddr + body
		|| memcmp(ram + gate, sig, sizeof(sig)) != 0)
		return;
	ram[gate + 4] = 0x4e;
	ram[gate + 5] = 0x71;
	ram[gate + 6] = 0x4e;
	ram[gate + 7] = 0x71;
}

static void CEmuX68kBasename(const char* path, char* out, int outMax)
{
	if (!out || outMax < 2) return;
	out[0] = 0;
	if (!path) return;
	const char* base = path;
	for (const char* p = path; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':')
			base = p + 1;
	}
	int n = 0;
	for (; base[n] && n < outMax - 1; n++) {
		char c = base[n];
		if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		out[n] = c;
	}
	out[n] = 0;
}

void CHardX68k::DosRegisterFile(const char* name, unsigned addr, unsigned size)
{
	if (!name || !size || dosFileCount_ >= kDosFiles) return;
	char base[32];
	CEmuX68kBasename(name, base, (int)sizeof(base));
	if (!base[0]) return;
	DosFile* f = &dosFiles_[dosFileCount_++];
	memset(f, 0, sizeof(*f));
	strncpy(f->name, base, sizeof(f->name) - 1);
	f->addr = addr & 0xffffffu;
	f->size = size;
}

int CHardX68k::DosFindFile(const char* path) const
{
	char want[32];
	CEmuX68kBasename(path, want, (int)sizeof(want));
	if (!want[0]) return -1;
	for (int i = 0; i < dosFileCount_; i++) {
		if (_stricmp(dosFiles_[i].name, want) == 0)
			return i;
	}
	return -1;
}

/* Read guest ASCIZ path, or reconstruct NAME.EXT from a Human68k namecks
   (drive@+0, path@+1, name@+66, ext@+74) when a1 points at a filled namecks. */
static int CEmuX68kReadDosPath(CHardX68k* hw, unsigned a1, char* path, int pathMax)
{
	if (!hw || !path || pathMax < 4) return 0;
	path[0] = 0;
	a1 &= 0xffffffu;
	const unsigned b0 = hw->Read8(a1);
	/* Heuristic: namecks drive is 0..26 and path[0] is '\\' or 0 — not ASCIZ. */
	const unsigned b1 = hw->Read8((a1 + 1u) & 0xffffffu);
	if (b0 <= 26u && (b1 == '\\' || b1 == '/' || b1 == 0)) {
		char name[9], ext[4];
		memset(name, 0, sizeof(name));
		memset(ext, 0, sizeof(ext));
		for (int i = 0; i < 8; i++) {
			char c = (char)hw->Read8((a1 + 66u + (unsigned)i) & 0xffffffu);
			if (c == ' ' || c == 0) break;
			name[i] = c;
		}
		for (int i = 0; i < 3; i++) {
			char c = (char)hw->Read8((a1 + 74u + (unsigned)i) & 0xffffffu);
			if (c == ' ' || c == 0) break;
			ext[i] = c;
		}
		if (!name[0]) return 0;
		if (ext[0])
			_snprintf(path, (size_t)pathMax, "%s.%s", name, ext);
		else
			_snprintf(path, (size_t)pathMax, "%s", name);
		path[pathMax - 1] = 0;
		return 1;
	}
	for (int i = 0; i < pathMax - 1; i++) {
		path[i] = (char)hw->Read8((a1 + (unsigned)i) & 0xffffffu);
		if (!path[i]) { path[i] = 0; return path[0] ? 1 : 0; }
	}
	path[pathMax - 1] = 0;
	return path[0] ? 1 : 0;
}

/* Human68k NAMECK: fill 91-byte namecks immediately after the ASCIZ path. */
static void CEmuX68kFillNamecks(CHardX68k* hw, unsigned a1, const char* path)
{
	if (!hw || !path || !path[0]) return;
	unsigned plen = 0;
	while (path[plen] && plen < 95u) plen++;
	const unsigned ncks = (a1 + plen + 1u) & 0xffffffu;
	for (unsigned i = 0; i < 91u; i++)
		hw->Write8((ncks + i) & 0xffffffu, 0);

	char drive = 0;
	const char* rest = path;
	if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
		&& path[1] == ':') {
		char d = path[0];
		if (d >= 'a' && d <= 'z') d = (char)(d - 'a' + 'A');
		drive = (char)(d - 'A' + 1);
		rest = path + 2;
	}
	hw->Write8(ncks, (uint8_t)drive);

	char base[32];
	CEmuX68kBasename(path, base, (int)sizeof(base));
	/* Path portion without basename (backslash-normalized, 65 bytes). */
	{
		char dir[66];
		memset(dir, 0, sizeof(dir));
		const char* slash = nullptr;
		for (const char* p = rest; *p; p++) {
			if (*p == '/' || *p == '\\') slash = p;
		}
		int n = 0;
		if (slash) {
			for (const char* p = rest; p <= slash && n < 64; p++) {
				char c = *p;
				if (c == '/') c = '\\';
				dir[n++] = c;
			}
		}
		for (int i = 0; i < 65; i++)
			hw->Write8((ncks + 1u + (unsigned)i) & 0xffffffu, (uint8_t)dir[i]);
	}
	/* name[8] + ext[3], space-padded Human68k style */
	{
		const char* dot = nullptr;
		for (const char* p = base; *p; p++) {
			if (*p == '.') dot = p;
		}
		char name[9], ext[4];
		memset(name, ' ', 8); name[8] = 0;
		memset(ext, ' ', 3); ext[3] = 0;
		if (dot) {
			int ni = 0;
			for (const char* p = base; p < dot && ni < 8; p++)
				name[ni++] = *p;
			int ei = 0;
			for (const char* p = dot + 1; *p && ei < 3; p++)
				ext[ei++] = *p;
		} else {
			int ni = 0;
			for (const char* p = base; *p && ni < 8; p++)
				name[ni++] = *p;
		}
		for (int i = 0; i < 8; i++)
			hw->Write8((ncks + 66u + (unsigned)i) & 0xffffffu, (uint8_t)name[i]);
		for (int i = 0; i < 3; i++)
			hw->Write8((ncks + 74u + (unsigned)i) & 0xffffffu, (uint8_t)ext[i]);
	}
}

unsigned CHardX68k::DosFileOp(unsigned fn, unsigned a1, unsigned d0, unsigned d1)
{
	fn &= 0xffu;
	if (fn == 0x4eu) {
		char path[96];
		memset(path, 0, sizeof(path));
		if (!CEmuX68kReadDosPath(this, a1, path, (int)sizeof(path)))
			return 0xffffffffu;
		if (DosFindFile(path) < 0) return 0xffffffffu;
		CEmuX68kFillNamecks(this, a1, path);
		return 0u;
	}
	if (fn == 0x3du || fn == 0x3cu) {
		char path[96];
		memset(path, 0, sizeof(path));
		if (!CEmuX68kReadDosPath(this, a1, path, (int)sizeof(path)))
			return 0xffffffffu;
		const int fi = DosFindFile(path);
		if (fi < 0) return 0xffffffffu;
		for (int h = 0; h < kDosHandles; h++) {
			if (dosHandles_[h].file < 0) {
				dosHandles_[h].file = fi;
				dosHandles_[h].pos = 0;
				return (unsigned)(5 + h);
			}
		}
		return 0xffffffffu;
	}
	if (fn == 0x3eu) {
		const int h = (int)d0 - 5;
		if (h < 0 || h >= kDosHandles) return 0xffffffffu;
		dosHandles_[h].file = -1;
		dosHandles_[h].pos = 0;
		return 0;
	}
	if (fn == 0x3fu) {
		const int h = (int)d0 - 5;
		if (h < 0 || h >= kDosHandles) return 0xffffffffu;
		const int fi = dosHandles_[h].file;
		if (fi < 0 || fi >= dosFileCount_) return 0xffffffffu;
		const DosFile* f = &dosFiles_[fi];
		unsigned pos = dosHandles_[h].pos;
		unsigned want = d1;
		if (pos >= f->size) return 0;
		if (pos + want > f->size)
			want = f->size - pos;
		for (unsigned i = 0; i < want; i++)
			Write8((a1 + i) & 0xffffffu, Read8((f->addr + pos + i) & 0xffffffu));
		dosHandles_[h].pos = pos + want;
		return want;
	}
	if (fn == 0x40u)
		return d1;
	if (fn == 0x43u) {
		const int h = (int)d0 - 5;
		if (h < 0 || h >= kDosHandles) return 0xffffffffu;
		if (dosHandles_[h].file < 0) return 0xffffffffu;
		dosHandles_[h].pos = d1;
		return d1;
	}
	if (fn == 0x4cu)
		return 0xffffffffu;
	return 0;
}

void CHardX68k::BootIplFf0b86()
{
	/* Real IPL subroutine (trap_f @ $FF0B86 → RTS @ $FF0CAC): MFP/ADPCM bring-up
	   that BOOT.BIN invokes after copying $1F0000→$FF0000. high_ already aliases
	   both pages, so the copy is a no-op; we only need the JSR. */
	if (!musashiReady_) return;
	if (high_[0x0b86] == 0 && high_[0x0b87] == 0) return;
	/* Signature: lea $E8A000,a0 */
	if (high_[0x0b86] != 0x41 || high_[0x0b87] != 0xf9) return;

	const unsigned bootSp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	const unsigned bootPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	unsigned sp = bootSp;
	if (sp < 8u || sp > 0xfffff8u)
		sp = 0x2000u; /* IPL reset SP */

	/* Return trampoline in $F0 work RAM: RTS. */
	const unsigned ret = 0xf0ffe0u;
	Write16(ret, 0x4e75);
	sp = (sp - 4u) & 0xffffffu;
	Write32(sp, ret);

	m68k_set_reg(M68K_REG_SR, 0x2700); /* supervisor, IRQs masked during IPL */
	m68k_set_reg(M68K_REG_SP, sp);
	m68k_set_reg(M68K_REG_PC, 0xff0b86u);

	/* Bound: subroutine is ~0x294 bytes; generous cycle budget. */
	for (int n = 0; n < 200000; n += 64) {
		m68k_execute(64);
		const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		if (pc == ret || pc == (ret + 2u))
			break;
		/* Ran off into the weeds — abort. */
		if (pc < 0xff0000u || pc > 0xff0fffu)
			break;
	}

	m68k_set_reg(M68K_REG_SP, bootSp);
	m68k_set_reg(M68K_REG_PC, bootPc);
	pc_ = bootPc;
}

int CHardX68k::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge) return 0;
	memset(rom_, 0, sizeof(rom_));
	memset(ram_, 0, sizeof(ram_));
	memset(high_, 0, sizeof(high_));
	memset(mid_, 0, sizeof(mid_));
	memset(mfp_, 0xff, sizeof(mfp_));
	dosFileCount_ = 0;
	memset(dosFiles_, 0, sizeof(dosFiles_));
	for (int i = 0; i < kDosHandles; i++) {
		dosHandles_[i].file = -1;
		dosHandles_[i].pos = 0;
	}
	int loaded = 0;
	int midFilled = 0;
	int trapFLoaded = 0;

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "x") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		const unsigned off = (r->offset < 0) ? 0u : (unsigned)r->offset;
		/* Human68k relocate ONLY for type=x (XML load address = body).
		   type=code is raw bytes like hoot — HU headers stay so body lands at
		   offset+0x40 when XML uses base-0x40 (dsj 01.bin @67C0 → body @6800). */
		const int isTypeX = (_stricmp(r->type, "x") == 0);

		/* trap_f / IPL high page */
		if (CEmuX68kIsTrapF(r->name) || off == 0x1f0000u || off == 0xff0000u) {
			unsigned n = sz > (unsigned)kHighBytes ? (unsigned)kHighBytes : sz;
			memcpy(high_, data, n);
			trapFLoaded = 1;
			continue;
		}

		/* Mid window $100000..$17FFFF */
		if (off >= 0x100000u && off < 0x100000u + (unsigned)kMidBytes) {
			const unsigned local = off - 0x100000u;
			if (isTypeX && CEmuX68kLoadHumanX(mid_, (unsigned)kMidBytes, local, data, sz)) {
				DosRegisterFile(r->name, off, sz > 0x40u ? sz - 0x40u : sz);
				midFilled = 1;
				loaded++;
				continue;
			}
			unsigned n = sz;
			if (local + n > (unsigned)kMidBytes)
				n = (unsigned)kMidBytes - local;
			memcpy(mid_ + local, data, n);
			DosRegisterFile(r->name, off, n);
			midFilled = 1;
			loaded++;
			continue;
		}

		/* Low RAM / BOOT image $000000..$0FFFFF */
		if (off < (unsigned)kRomBytes) {
			if (isTypeX && CEmuX68kLoadHumanX(rom_, (unsigned)kRomBytes, off, data, sz)) {
				const unsigned text = ((unsigned)data[0x0c] << 24)
					| ((unsigned)data[0x0d] << 16)
					| ((unsigned)data[0x0e] << 8) | (unsigned)data[0x0f];
				const unsigned datasz = ((unsigned)data[0x10] << 24)
					| ((unsigned)data[0x11] << 16)
					| ((unsigned)data[0x12] << 8) | (unsigned)data[0x13];
				CEmuX68kFixSdDrvHostGate(rom_, off, text + datasz);
				DosRegisterFile(r->name, off, sz > 0x40u ? sz - 0x40u : sz);
				loaded++;
				continue;
			}
			unsigned n = sz;
			if (off + n > (unsigned)kRomBytes)
				n = (unsigned)kRomBytes - off;
			memcpy(rom_ + off, data, n);
			DosRegisterFile(r->name, off, n);
			loaded++;
			continue;
		}

		/* Offset outside known windows — skip (no guessing). */
	}

	if (!loaded) return 0;

	/* X68k $10xxxx can be a separate window; if XML never filled mid_, seed
	   from rom_[0..512K] so packs that only list low RAM still alias correctly. */
	if (!midFilled) {
		int any = 0;
		for (unsigned i = 0; i < (unsigned)kMidBytes; i++) {
			if (mid_[i]) { any = 1; break; }
		}
		if (!any)
			memcpy(mid_, rom_, sizeof(mid_));
	}

	CEmuHardX68kSetActive(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	{
		const unsigned sp = (((unsigned)rom_[0] << 24) | ((unsigned)rom_[1] << 16)
			| ((unsigned)rom_[2] << 8) | (unsigned)rom_[3]) & 0xffffffu;
		const unsigned pc = (((unsigned)rom_[4] << 24) | ((unsigned)rom_[5] << 16)
			| ((unsigned)rom_[6] << 8) | (unsigned)rom_[7]) & 0xffffffu;
		m68k_set_reg(M68K_REG_SP, sp);
		m68k_set_reg(M68K_REG_PC, pc);
	}
	musashiReady_ = 1;
	pc_ = m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	/* trap_f present: run IPL $FF0B86 once before BOOT (same sub BOOT JSRs). */
	if (trapFLoaded)
		BootIplFf0b86();
	/* Coherent Human68k DOS/IOCS at $F08000 when BOOT left thin stubs. */
	CEmuX68kDosInstall(this);
	fetchCount_ = 0;
	songFlag_ = 0;
	songCode_ = 0;
	ymAddr_ = 0;
	adpcmPlaying_ = 0;
	adpcmAddr_ = 0;
	adpcmSize_ = 0;
	adpcmPos_ = 0;
	adpcmSignal_ = 0;
	adpcmStep_ = 0;
	adpcmNibble_ = 0;
	adpcmRateHz_ = 15600;
	adpcmPan_ = 0;
	adpcmPpi_ = 0x08;
	adpcmPhase_ = 0;
	if (chip_) chip_->Reset();
	opmWrites_ = 0;
	return 1;
}
