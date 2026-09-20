#include "StdAfx.h"
#include "cemu_hard_pico.h"
#include "cemu_m68k_bus.h"
#include "../chip/cemu_chip_sn76489.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>

/* MAME megadriv NTSC: 53.693175/7 ≈ 7.670454 MHz。VDP PSG は 3.579545 MHz。 */
enum {
	PICO_CPU_HZ = 7670454,
	PICO_PSG_HZ = 3579545
};

static CHardPico* g_picoSelf = NULL;

/* 315-5641 / D77591: 64 バイト FIFO。実 ADPCM は出さず、SMPS の READY/空き待ちだけ通す。
   stub が常に 0x3F 空きだと世界名作劇場が 0x40 空を待って #---#### になる。 */
enum { kPcmFifoBytes = 64, kPcmCyclesPerByte = 1918 };
static uint8_t s_pcmFifo[kPcmFifoBytes];
static int s_pcmLen;
static int s_pcmAcc;

void CHardPico::PcmFifoReset()
{
	s_pcmLen = 0;
	s_pcmAcc = 0;
	memset(s_pcmFifo, 0, sizeof(s_pcmFifo));
}

void CHardPico::PcmFifoPush(uint8_t b)
{
	if (s_pcmLen >= kPcmFifoBytes) return;
	s_pcmFifo[s_pcmLen++] = b;
}

void CHardPico::TickPcm(int cycles)
{
	if (cycles <= 0 || s_pcmLen <= 0) {
		if (cycles > 0 && s_pcmLen <= 0)
			s_pcmAcc = 0;
		return;
	}
	s_pcmAcc += cycles;
	int irq = 0;
	while (s_pcmLen > 0 && s_pcmAcc >= kPcmCyclesPerByte) {
		s_pcmAcc -= kPcmCyclesPerByte;
		s_pcmLen--;
		irq = 1;
	}
	if (irq && musashiReady_)
		m68k_set_irq(M68K_IRQ_3);
}

void CEmuHardPicoSetActive(CHardPico* hw)
{
	g_picoSelf = hw;
	CEmuM68kBusSetPico(hw);
	/* F3 の IACK が残るとオートベクタが壊れる */
	if (hw)
		m68k_set_int_ack_callback(NULL);
}

CHardPico* CEmuHardPicoGetActive()
{
	return g_picoSelf ? g_picoSelf : CEmuM68kBusGetPico();
}

CHardPico::CHardPico()
	: cpuHz_(PICO_CPU_HZ)
	, psgHz_(PICO_PSG_HZ)
	, cart_(NULL)
	, cartSize_(0)
	, chip_(NULL)
	, sampleRate_(44100)
	, musashiReady_(0)
	, psgWrites_(0)
	, mailboxAddr_(0xFF0009)
	, smpsRamBase_(0xFF0000)
	, pageReg_(0x01)
	, vdpStatus_(0x8200)
	, vdpLatch_(0)
	, vdpLatchHalf_(0)
	, hvCount_(0xE000)
	, vintEnable_(0)
	, vintPending_(0)
	, pcmCtrl_(0)
{
	hardKind = KIND_PICO;
	memset(ram_, 0, sizeof(ram_));
	memset(vdpReg_, 0, sizeof(vdpReg_));
	PcmFifoReset();
}

CHardPico::~CHardPico()
{
	Shutdown();
}

int CHardPico::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = PICO_CPU_HZ;
	psgHz_ = PICO_PSG_HZ;
	chip_ = CEmuChipSn76489Create((uint32_t)psgHz_, sampleRate_);
	cart_ = (uint8_t*)malloc(kCartMax);
	if (!cart_ || !chip_) return 0;
	memset(cart_, 0xff, kCartMax);
	cartSize_ = 0;
	return 1;
}

void CHardPico::Shutdown()
{
	if (CEmuM68kBusGetPico() == this)
		CEmuM68kBusSetPico(NULL);
	if (g_picoSelf == this)
		g_picoSelf = NULL;
	musashiReady_ = 0;
	if (chip_) {
		CEmuChipSn76489Destroy(chip_);
		chip_ = NULL;
	}
	if (cart_) {
		free(cart_);
		cart_ = NULL;
	}
	cartSize_ = 0;
}

static void CEmuPicoZipBaseName(const char* name, char* out, int outCap)
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

static int CEmuPicoLooksCart(const uint8_t* p, unsigned n)
{
	if (!p || n < 0x110) return 0;
	const unsigned sp = ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16)
		| ((unsigned)p[2] << 8) | (unsigned)p[3];
	const unsigned pc = ((unsigned)p[4] << 24) | ((unsigned)p[5] << 16)
		| ((unsigned)p[6] << 8) | (unsigned)p[7];
	if ((sp & 0xFF0000u) != 0xFF0000u && (sp & 0xFF0000u) != 0xE00000u)
		return 0;
	if (pc < 8u || pc >= n) return 0;
	if (memcmp(p + 0x100, "SEGA", 4) == 0) return 1;
	return 1;
}

static void CEmuPicoMaybeByteswap(uint8_t* p, unsigned n)
{
	if (!p || n < 8) return;
	if (CEmuPicoLooksCart(p, n)) return;
	for (unsigned i = 0; i + 1 < n; i += 2) {
		const uint8_t t = p[i];
		p[i] = p[i + 1];
		p[i + 1] = t;
	}
}

/* ゲーム層: andi.b #$9F, $FFxxxx / move.b d0, $FFyyyy / rts */
static unsigned CEmuPicoDetectGameMailbox(const uint8_t* rom, unsigned sz)
{
	if (!rom || sz < 16) return 0;
	for (unsigned i = 0; i + 16 <= sz; i += 2) {
		if (rom[i] != 0x02 || rom[i + 1] != 0x39) continue;
		if (rom[i + 2] != 0x00 || rom[i + 3] != 0x9F) continue;
		if (rom[i + 4] != 0x00 || rom[i + 5] != 0xFF) continue;
		if (rom[i + 8] != 0x13 || rom[i + 9] != 0xC0) continue;
		if (rom[i + 10] != 0x00 || rom[i + 11] != 0xFF) continue;
		if (rom[i + 14] != 0x4E || rom[i + 15] != 0x75) continue;
		return 0xFF0000u | (((unsigned)rom[i + 12] << 8) | (unsigned)rom[i + 13]);
	}
	return 0;
}

/* IRQ6 先頭の jsr abs.l 先 +$2C に SMPS RAM 基点が載る */
static unsigned CEmuPicoDetectSmpsRam(const uint8_t* rom, unsigned sz)
{
	if (!rom || sz < 0x80) return 0;
	const unsigned irq6 = ((unsigned)rom[0x78] << 24) | ((unsigned)rom[0x79] << 16)
		| ((unsigned)rom[0x7a] << 8) | (unsigned)rom[0x7b];
	if (irq6 < 8u || irq6 + 40u > sz) return 0;
	for (unsigned i = irq6; i + 6u <= irq6 + 40u && i + 0x32u <= sz; i += 2) {
		if (rom[i] != 0x4E || rom[i + 1] != 0xB9) continue;
		const unsigned t = ((unsigned)rom[i + 2] << 24) | ((unsigned)rom[i + 3] << 16)
			| ((unsigned)rom[i + 4] << 8) | (unsigned)rom[i + 5];
		if (t + 0x30u > sz) continue;
		const unsigned ram = ((unsigned)rom[t + 0x2C] << 24) | ((unsigned)rom[t + 0x2D] << 16)
			| ((unsigned)rom[t + 0x2E] << 8) | (unsigned)rom[t + 0x2F];
		if ((ram & 0xFF0000u) == 0xFF0000u || (ram & 0xFF0000u) == 0xE00000u)
			return ram;
	}
	return 0;
}

void CHardPico::DetectSoundGlue()
{
	mailboxAddr_ = CEmuPicoDetectGameMailbox(cart_, cartSize_);
	smpsRamBase_ = CEmuPicoDetectSmpsRam(cart_, cartSize_);
	if (!smpsRamBase_)
		smpsRamBase_ = 0xFF0000u;
	if (!mailboxAddr_)
		mailboxAddr_ = (smpsRamBase_ + 9u) & 0xFFFFFFu;
}

int CHardPico::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge || !cart_ || !chip_) return 0;
	memset(cart_, 0xff, kCartMax);
	memset(ram_, 0, sizeof(ram_));
	cartSize_ = 0;
	int loaded = 0;

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && r->type[0]) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		unsigned n = sz;
		if (n > (unsigned)kCartMax) n = (unsigned)kCartMax;
		memcpy(cart_, data, n);
		cartSize_ = n;
		loaded++;
		break;
	}

	if (!loaded) {
		unsigned bestSz = 0;
		const unsigned char* best = NULL;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			char base[CEMU_ZIP_PATH];
			CEmuPicoZipBaseName(pathA, base, (int)sizeof(base));
			const size_t n = strlen(base);
			int ok = 0;
			if (n >= 3 && _stricmp(base + n - 3, ".md") == 0) ok = 1;
			else if (n >= 4 && _stricmp(base + n - 4, ".bin") == 0) ok = 1;
			else if (n >= 4 && _stricmp(base + n - 4, ".ic1") == 0) ok = 1;
			else if (fs->files[i].size >= 0x10000u) ok = 1;
			if (!ok) continue;
			if (fs->files[i].size > bestSz) {
				bestSz = fs->files[i].size;
				best = fs->files[i].data;
			}
		}
		if (best && bestSz) {
			unsigned n = bestSz;
			if (n > (unsigned)kCartMax) n = (unsigned)kCartMax;
			memcpy(cart_, best, n);
			cartSize_ = n;
			loaded++;
		}
	}
	if (!loaded || cartSize_ < 0x200u) return 0;
	CEmuPicoMaybeByteswap(cart_, cartSize_);
	if (!CEmuPicoLooksCart(cart_, cartSize_)) return 0;

	DetectSoundGlue();
	pageReg_ = 0x01;
	vdpStatus_ = 0x8200;
	vdpLatchHalf_ = 0;
	hvCount_ = 0xE000;
	vintEnable_ = 0;
	vintPending_ = 0;
	pcmCtrl_ = 0;
	memset(vdpReg_, 0, sizeof(vdpReg_));
	psgWrites_ = 0;
	PcmFifoReset();
	if (chip_) chip_->Reset();

	CEmuHardPicoSetActive(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	{
		const unsigned sp = (((unsigned)cart_[0] << 24) | ((unsigned)cart_[1] << 16)
			| ((unsigned)cart_[2] << 8) | (unsigned)cart_[3]) & 0xffffffu;
		const unsigned pc = (((unsigned)cart_[4] << 24) | ((unsigned)cart_[5] << 16)
			| ((unsigned)cart_[6] << 8) | (unsigned)cart_[7]) & 0xffffffu;
		m68k_set_reg(M68K_REG_SP, sp);
		m68k_set_reg(M68K_REG_PC, pc);
	}
	musashiReady_ = 1;
	return 1;
}

void CHardPico::SetVintPending()
{
	vintPending_ = 1;
	vdpStatus_ = (uint16_t)(vdpStatus_ | 0x2080);
}

void CHardPico::PulseVint()
{
	SetVintPending();
	if (musashiReady_)
		m68k_set_irq(M68K_IRQ_6);
}

uint16_t CHardPico::IoRead(unsigned off)
{
	switch (off & 15) {
	case 0: return 0x0000; /* 日本 NTSC: version nibble 00 */
	case 1: return 0xFFFF; /* ボタン全部離す */
	case 2: return 0x0000; /* pen X hi */
	case 3: return 0x00C0;
	case 4: return 0x0002; /* pen Y drawing pad */
	case 5: return 0x0020;
	case 6: return (uint16_t)((pageReg_ << 8) | pageReg_);
	case 8: {
		const unsigned sp = (unsigned)(kPcmFifoBytes - s_pcmLen);
		return (uint16_t)((sp << 8) | sp);
	}
	case 9:
		/* bit15=1 で READY。FIFO に残があると busy（SMPS が prevent-PCM を下ろす） */
		return s_pcmLen > 0 ? (uint16_t)0 : (uint16_t)0x8000;
	default: return 0x0000;
	}
}

void CHardPico::IoWrite(unsigned off, uint16_t data, uint16_t mask)
{
	off &= 15;
	if (off == 8) {
		if (mask & 0xff00)
			PcmFifoPush((uint8_t)(data >> 8));
		if (mask & 0x00ff)
			PcmFifoPush((uint8_t)(data & 0xff));
		return;
	}
	if (off == 9) {
		pcmCtrl_ = data;
		if (mask & 0xff00) {
			if (data & 0x8000)
				PcmFifoReset();
			if (!(data & 0x0800))
				PcmFifoReset();
		}
	}
}

uint16_t CHardPico::VdpRead(unsigned off)
{
	off &= 0x1f;
	if (off <= 0x03) {
		/* データポート — VRAM 無し */
		return 0;
	}
	if (off <= 0x07) {
		uint16_t st = (uint16_t)(0x8200 | (vintPending_ ? 0x2080 : 0x0080));
		vintPending_ = 0;
		vdpStatus_ = (uint16_t)(st & ~0x2000);
		vdpLatchHalf_ = 0;
		if (musashiReady_)
			m68k_set_irq(M68K_IRQ_NONE);
		return st;
	}
	if (off <= 0x09) {
		hvCount_ = (uint16_t)(hvCount_ + 0x0100);
		return hvCount_;
	}
	return 0;
}

void CHardPico::VdpWrite(unsigned off, uint16_t data)
{
	off &= 0x1f;
	if (off >= 0x10 && off <= 0x17) {
		if (chip_) {
			chip_->Write(0, (uint8_t)(data & 0xff));
			psgWrites_ = CEmuChipSn76489WriteCount(chip_);
		}
		return;
	}
	if (off <= 0x03)
		return;
	if (off <= 0x07) {
		if ((data & 0xC000) == 0x8000) {
			const unsigned r = (data >> 8) & 0x1f;
			vdpReg_[r] = (uint8_t)(data & 0xff);
			if (r == 1)
				vintEnable_ = (vdpReg_[1] & 0x20) ? 1 : 0;
			vdpLatchHalf_ = 0;
			return;
		}
		if (!vdpLatchHalf_) {
			vdpLatch_ = data;
			vdpLatchHalf_ = 1;
		} else {
			vdpLatchHalf_ = 0;
		}
	}
}

uint8_t CHardPico::Read8(unsigned addr)
{
	addr &= 0xffffffu;
	if (addr < cartSize_)
		return cart_[addr];
	if (addr < 0x400000u)
		return 0xff;
	if (addr >= 0x800000u && addr <= 0x80001fu) {
		const uint16_t w = IoRead((addr - 0x800000u) >> 1);
		return (addr & 1) ? (uint8_t)(w & 0xff) : (uint8_t)(w >> 8);
	}
	if (addr >= 0xc00000u && addr <= 0xc0001fu) {
		const unsigned off = addr & 0x1f;
		if (off >= 0x10 && off <= 0x17)
			return 0;
		const uint16_t w = VdpRead(off);
		return (addr & 1) ? (uint8_t)(w & 0xff) : (uint8_t)(w >> 8);
	}
	if (addr >= 0xe00000u)
		return ram_[addr & 0xffffu];
	return 0xff;
}

uint16_t CHardPico::Read16(unsigned addr)
{
	addr &= 0xffffffu;
	if (addr >= 0x800000u && addr <= 0x80001fu && !(addr & 1))
		return IoRead((addr - 0x800000u) >> 1);
	if (addr >= 0xc00000u && addr <= 0xc0001fu && !(addr & 1))
		return VdpRead(addr & 0x1f);
	return (uint16_t)((Read8(addr) << 8) | Read8((addr + 1) & 0xffffffu));
}

uint32_t CHardPico::Read32(unsigned addr)
{
	addr &= 0xffffffu;
	return ((uint32_t)Read16(addr) << 16) | (uint32_t)Read16((addr + 2) & 0xffffffu);
}

void CHardPico::Write8(unsigned addr, uint8_t data)
{
	addr &= 0xffffffu;
	if (addr < 0x400000u)
		return;
	if (addr >= 0x800000u && addr <= 0x80001fu) {
		const unsigned off = (addr - 0x800000u) >> 1;
		uint16_t cur = IoRead(off);
		if (addr & 1) cur = (uint16_t)((cur & 0xff00) | data);
		else cur = (uint16_t)((cur & 0x00ff) | (data << 8));
		IoWrite(off, cur, (addr & 1) ? 0x00ff : 0xff00);
		return;
	}
	if (addr >= 0xc00000u && addr <= 0xc0001fu) {
		const unsigned off = addr & 0x1f;
		if (off >= 0x11 && off <= 0x17 && (addr & 1) && chip_) {
			chip_->Write(0, data);
			psgWrites_ = CEmuChipSn76489WriteCount(chip_);
			return;
		}
		uint16_t cur = 0;
		if (addr & 1) cur = data;
		else cur = (uint16_t)(data << 8);
		VdpWrite(off, cur);
		return;
	}
	if (addr >= 0xe00000u)
		ram_[addr & 0xffffu] = data;
}

void CHardPico::Write16(unsigned addr, uint16_t data)
{
	addr &= 0xffffffu;
	if (addr >= 0x800000u && addr <= 0x80001fu && !(addr & 1)) {
		IoWrite((addr - 0x800000u) >> 1, data, 0xffff);
		return;
	}
	if (addr >= 0xc00000u && addr <= 0xc0001fu && !(addr & 1)) {
		VdpWrite(addr & 0x1f, data);
		return;
	}
	Write8(addr, (uint8_t)(data >> 8));
	Write8((addr + 1) & 0xffffffu, (uint8_t)data);
}

void CHardPico::Write32(unsigned addr, uint32_t data)
{
	addr &= 0xffffffu;
	Write16(addr, (uint16_t)(data >> 16));
	Write16((addr + 2) & 0xffffffu, (uint16_t)data);
}
