#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_sei80bu.h"
#include "cemu_m68k_bus.h"
#include "../z80/cemu_z80_bus.h"
#include "../chip/cemu_chip_opl.h"
#include "../chip/cemu_chip_oki6295.h"
#include "../chip/cemu_chip_ay.h"
#include "../chip/cemu_chip_scsp.h"
#include "../chip/cemu_chip_ym2612.h"
#include "../chip/cemu_chip_multipcm.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
#include "../vendor/m6803/m6803.h"
}
#include <string.h>
#include <stdlib.h>

static int CEmuAcIsCodeRomType(const char* t)
{
	if (!t || !t[0]) return 0;
	return (_stricmp(t, "code") == 0
		|| _stricmp(t, "sound") == 0
		|| _stricmp(t, "audiocpu") == 0
		|| _strnicmp(t, "code", 4) == 0) ? 1 : 0;
}

/* ---- SEI80BU helpers ---- */

void CHardAc::SeibuSetBank(unsigned bank)
{
	seibuBank_ = (int)(bank & 1);
	SeibuRefreshOpcodes();
}

void CHardAc::SeibuRefreshOpcodes()
{
	if (!soundRom_) return;
	if (!seibuEnc_) {
		const unsigned bankOff = seibuBank_ ? 0x18000u : 0x10000u;
		for (unsigned a = 0; a < 0x10000u; a++) {
			if (a >= 0x2000u && a <= 0x27ffu) continue; /* RAM */
			unsigned phys;
			if (a < 0x8000u)
				phys = a;
			else
				phys = bankOff + (a & 0x7fffu);
			mem_[a] = (phys < soundRomSize_) ? soundRom_[phys] : 0xff;
		}
		return;
	}
	const unsigned bankOff = seibuBank_ ? 0x18000u : 0x10000u;
	for (unsigned a = 0; a < 0x10000u; a++) {
		if (a >= 0x2000u && a <= 0x27ffu) continue; /* RAM */
		unsigned phys;
		if (a < 0x8000u)
			phys = a;
		else
			phys = bankOff + (a & 0x7fffu);
		if (phys >= soundRomSize_) {
			mem_[a] = 0xff;
			continue;
		}
		mem_[a] = CEmuSei80buOpcode((uint16_t)a, soundRom_[phys]);
	}
}

static unsigned CEmuSeibuPhys(CHardAc* hw, uint16_t addr)
{
	(void)hw;
	if (addr < 0x8000)
		return addr;
	return 0x10000u + (addr & 0x7fffu);
}

int CHardAc::LoadRomsSeibu(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge || !cpu_ || !chip_) return 0;
	memset(mem_, 0, sizeof(mem_));
	if (soundRom_) { free(soundRom_); soundRom_ = NULL; soundRomSize_ = 0; }
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }

	/* Code ROM → 0x20000 MAME layout. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!CEmuAcIsCodeRomType(r->type)) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		uint8_t* p = (uint8_t*)calloc(1, 0x20000);
		if (!p) return 0;
		if (sz >= 0x20000u) {
			memcpy(p, data, 0x20000);
		} else if (sz >= 0x10000u) {
			memcpy(p, data, 0x8000);
			memcpy(p + 0x10000, data + 0x8000, 0x8000);
			memcpy(p + 0x18000, p, 0x8000);
		} else {
			memcpy(p, data, sz < 0x8000u ? sz : 0x8000u);
			if (sz > 0x8000u)
				memcpy(p + 0x10000, data + 0x8000, sz - 0x8000u);
			memcpy(p + 0x18000, p, 0x8000);
		}
		soundRom_ = p;
		soundRomSize_ = 0x20000;
		break;
	}
	if (!soundRomSize_) return 0;

	/* OKI PCM */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!r->type || (_stricmp(r->type, "pcm") != 0 && _stricmp(r->type, "oki") != 0
			&& _strnicmp(r->type, "pcm", 3) != 0))
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		uint8_t* p = (uint8_t*)realloc(pcmRom_, sz);
		if (!p) return 0;
		pcmRom_ = p;
		pcmRomSize_ = sz;
		memcpy(pcmRom_, data, sz);
		break;
	}
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);

	seibuEnc_ = 1;
	seibuBank_ = 0;
	seibuMain2Sub_[0] = seibuMain2Sub_[1] = 0;
	seibuSub2Main_[0] = seibuSub2Main_[1] = 0;
	seibuMainPending_ = 0;
	seibuSubPending_ = 0;
	seibuRst10_ = 0;
	seibuRst18_ = 0;
	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	/* Local raiden/heatbrl dumps are plaintext (JP @$04 / LD SP @$70). */
	seibuEnc_ = !(soundRom_[4] == 0xc3 || soundRom_[0x70] == 0x31);
	SeibuRefreshOpcodes();
	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();
	cpu_->reset(mem_);
	CEmuZ80BusSetActive(this);
	return 1;
}

/* ---- M62 / M6803 ---- */

static uint8_t M62BusRead(struct m6800* cpu, uint16_t addr)
{
	CHardAc* hw = (CHardAc*)cpu->ctx;
	if (!hw) return 0xff;
	addr = (uint16_t)(addr & hw->M62BusMask());
	/* M62/M52-large: ROM 4000-FFFF. M52-small (mask 7FFF): ROM 2000-7FFF. */
	if (addr >= 0x2000)
		return hw->Mem()[addr];
	/* Internal 6803 IRAM / IO handled by m6800_do_read before this; open bus. */
	return 0xff;
}

static void M62IrqAck(struct m6800* cpu, CHardAc* hw)
{
	/* MAME sound_irq_ack_w: clear IRQ only while latch bit7 is set. */
	if ((hw->SoundCommand() & 0x80) != 0)
		m6800_clear_interrupt(cpu, IRQ_IRQ1);
}

static void M62BusWrite(struct m6800* cpu, uint16_t addr, uint8_t val)
{
	CHardAc* hw = (CHardAc*)cpu->ctx;
	if (!hw) return;
	addr = (uint16_t)(addr & hw->M62BusMask());
	/* M62: 0800 irq ack (mirror f7fc); 0801/0802 ADPCM (AY BGM ignores). */
	if ((addr & 0xf7fc) == 0x0800) {
		M62IrqAck(cpu, hw);
		return;
	}
	/* M52-small: irq ack 1000-1fff; M52-large: 2000-3fff. */
	if ((addr >= 0x1000 && addr <= 0x1fff) || (addr >= 0x2000 && addr <= 0x3fff)) {
		M62IrqAck(cpu, hw);
		return;
	}
	(void)val;
}

static uint8_t M62PortIn(struct m6800* cpu, int port)
{
	CHardAc* hw = (CHardAc*)cpu->ctx;
	if (!hw) return 0xff;
	if (port == 1) {
		/* MAME m6803_port1_r: PSG data bus when selected. */
		if ((hw->M62Port2() & 0x08) && hw->SoundChip())
			return hw->SoundChip()->ReadData();
		if ((hw->M62Port2() & 0x10) && hw->Chip2())
			return hw->Chip2()->ReadData();
		return 0xff;
	}
	if (port == 2)
		return 0x00; /* tied high via resistor — MAME returns 0 */
	return 0xff;
}

static void M62PortOut(struct m6800* cpu, int port, uint8_t val)
{
	CHardAc* hw = (CHardAc*)cpu->ctx;
	if (!hw) return;
	if (port == 1) {
		hw->M62SetPort1(val);
		return;
	}
	if (port == 2)
		hw->M62OnPort2Write(val);
}

void CHardAc::M62OnPort2Write(uint8_t val)
{
	/* MAME irem.cpp m6803_port2_w: BC1/PSG selects come from the PREVIOUS
	   port2 value; only bit0 falling-edge is the strobe from `val`. */
	const uint8_t prev = m62Port2_;
	if ((prev & 0x01) && !(val & 0x01)) {
		CChip* ayM = chip_;
		CChip* ayL = chip2_;
		if (prev & 0x04) {
			if ((prev & 0x08) && ayM) {
				ayM->Write(0, m62Port1_);
				m62AyMAddr_ = (uint8_t)(m62Port1_ & 0x0f);
			}
			if ((prev & 0x10) && ayL) ayL->Write(0, m62Port1_);
		} else {
			if ((prev & 0x08) && ayM) {
				ayM->Write(1, m62Port1_);
				opmWrites_++;
				/* AY#0 port B bit0 = MSM5205 #1 reset (MAME ay8910_45M_portb_w). */
				if (m62AyMAddr_ == 0x0f)
					m62MsmReset_ = (m62Port1_ & 0x01) ? 1 : 0;
			}
			if ((prev & 0x10) && ayL) {
				ayL->Write(1, m62Port1_);
				opmWrites_++;
			}
		}
	}
	m62Port2_ = val;
}

int CHardAc::LoadRomsM62(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge || !chip_ || !m6803_) return 0;
	memset(mem_, 0xff, sizeof(mem_));
	/* MAME m52_small_sound_map uses global_mask 0x7fff (mpatrol/travrusa). */
	m62BusMask_ = 0xffffu;
	if (ge->archive && (!_stricmp(ge->archive, "mpatrol")
		|| !_stricmp(ge->archive, "travrusa")))
		m62BusMask_ = 0x7fffu;
	int loaded = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!CEmuAcIsCodeRomType(r->type)) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz || data == (const unsigned char*)1) continue;
		int off = r->offset;
		if (off < 0) off = 0;
		/* XML F000 + 15-bit bus → physical 7000 (vectors at 7FFx). */
		if (m62BusMask_ == 0x7fffu && (unsigned)off > m62BusMask_)
			off = (int)((unsigned)off & m62BusMask_);
		if ((unsigned)off >= 0x10000u) continue;
		unsigned n = sz;
		if ((unsigned)off + n > 0x10000u) n = 0x10000u - (unsigned)off;
		memcpy(mem_ + (unsigned)off, data, n);
		loaded++;
	}
	if (!loaded) {
		/* Fallback: place members at high addresses in name order. */
		unsigned off = 0x10000u;
		for (int i = fs->fileCount - 1; i >= 0 && off > 0x4000u; i--) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x1000u || sz > 0x4000u) continue;
			if (!fs->files[i].data || fs->files[i].data == (unsigned char*)1)
				continue;
			if (off < sz) break;
			off -= sz;
			memcpy(mem_ + off, fs->files[i].data, sz);
			loaded++;
		}
	}
	if (!loaded) return 0;

	m62Port1_ = 0;
	m62Port2_ = 0;
	m62AyMAddr_ = 0;
	m62MsmReset_ = 1;
	/* Idle latch with bit7 set so ack can clear; IRQ starts clear so init
	   (SEI/CLI around FA22) can finish before the host posts a song. */
	soundCmd_ = 0x80;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();
	if (chip2_) chip2_->Reset();
	if (chip_) CEmuChipAySetPortA(chip_, 0x80);

	/* m6800_reset memset()s the CPU — install bus hooks after reset. */
	m6800_reset(m6803_, CPU_6803, INTIO_6803, 2);
	m6803_->ctx = this;
	m6803_->read = M62BusRead;
	m6803_->write = M62BusWrite;
	m6803_->port_in = M62PortIn;
	m6803_->port_out = M62PortOut;
	m6803_->pc = ((uint16_t)M62BusRead(m6803_, 0xFFFE) << 8)
		| M62BusRead(m6803_, 0xFFFF);
	m6803_->s = 0x00ff; /* until firmware LDS */
	m6803_->debug = 0;
	/* $BC is the latched song id; 0 means "stop" and memset'd IRAM would
	   make the first main-loop pass kill BGM before the host posts a title. */
	if (m6803_->iram_base <= 0xbc && 0xbc <= 0xff)
		m6803_->iram[0xbc - m6803_->iram_base] = 0xff;
	return 1;
}

/* ---- Sega Model1 / early Model2 MultiPCM + 68000 MIDI host ---- */

void CHardAc::SegaMidiPush(uint8_t b)
{
	const int next = (segaMidiTail_ + 1) & 63;
	if (next == segaMidiHead_) return;
	segaMidiFifo_[segaMidiTail_] = b;
	segaMidiTail_ = next;
	segaMidiIrq_ = 1;
}

void CHardAc::SegaMidiInjectSong(uint16_t cmd)
{
	segaMidiHead_ = segaMidiTail_ = 0;
	segaMidiIrq_ = 0;
	/* Hoot model2 / Model 1 audio: pre_cmd 0xA0 then 16-bit title (lo, hi).
	   The 68000 firmware owns MultiPCM + YM3438 — do not poke chips or invent
	   MIDI note-ons here (that produced the daytona rail-to-rail buzz). */
	SegaMidiPush(0xa0);
	SegaMidiPush((uint8_t)(cmd & 0xff));
	SegaMidiPush((uint8_t)((cmd >> 8) & 0xff));
}

uint8_t CHardAc::SegaUartRead(unsigned reg)
{
	if (reg & 1) {
		/* 8251 status: TxRDY always, RxRDY if FIFO has data. */
		uint8_t st = 0x01; /* TxRDY */
		if (segaMidiHead_ != segaMidiTail_) st |= 0x02; /* RxRDY */
		return st;
	}
	if (segaMidiHead_ == segaMidiTail_) return 0x00;
	const uint8_t b = segaMidiFifo_[segaMidiHead_];
	segaMidiHead_ = (segaMidiHead_ + 1) & 63;
	if (segaMidiHead_ == segaMidiTail_) segaMidiIrq_ = 0;
	return b;
}

void CHardAc::SegaUartWrite(unsigned reg, uint8_t data)
{
	(void)reg;
	(void)data; /* TX / mode ignored */
}

unsigned CHardAc::Sega68Read16(unsigned addr)
{
	addr &= 0xfffffeu;
	if (addr < 0x040000u || (addr >= 0x080000u && addr <= 0x09fffeu)) {
		unsigned o = addr;
		if (o >= 0x080000u) o = 0x020000u + (o & 0x1ffffu);
		if (!ms1Rom_ || o + 1u >= ms1RomSize_) return 0xffff;
		return ((unsigned)ms1Rom_[o] << 8) | ms1Rom_[o + 1];
	}
	if (addr >= 0xc20000u && addr <= 0xc20003u)
		return (unsigned)SegaUartRead((addr >> 1) & 1u);
	if (addr >= 0xc40000u && addr <= 0xc40007u)
		return pcm_ ? pcm_->ReadStatus() : 0;
	if (addr >= 0xc60000u && addr <= 0xc60007u)
		return pcm2_ ? pcm2_->ReadStatus() : 0;
	if (addr >= 0xd00000u && addr <= 0xd00007u)
		return chip_ ? chip_->ReadStatus() : 0;
	if (addr >= 0xf00000u && addr <= 0xf0ffffu) {
		if (!ms1Ram_) return 0xffff;
		const unsigned o = addr & 0xffffu;
		return ((unsigned)ms1Ram_[o] << 8) | ms1Ram_[o + 1];
	}
	return 0xffff;
}

unsigned CHardAc::Sega68Read8(unsigned addr)
{
	if (addr < 0x040000u || (addr >= 0x080000u && addr <= 0x09ffffu)) {
		unsigned o = addr;
		if (o >= 0x080000u) o = 0x020000u + (o & 0x1ffffu);
		if (!ms1Rom_ || o >= ms1RomSize_) return 0xff;
		return ms1Rom_[o];
	}
	if (addr >= 0xc20000u && addr <= 0xc20003u)
		return SegaUartRead((addr >> 1) & 1u);
	if (addr >= 0xc40000u && addr <= 0xc40007u && pcm_)
		return pcm_->ReadStatus();
	if (addr >= 0xc60000u && addr <= 0xc60007u && pcm2_)
		return pcm2_->ReadStatus();
	if (addr >= 0xd00000u && addr <= 0xd00007u && chip_) {
		const unsigned r = (addr >> 1) & 3u;
		if (r == 0) return chip_->ReadStatus();
		if (r == 1) return chip_->ReadData();
		if (r == 2) return chip_->ReadStatusHi();
		return chip_->ReadDataHi();
	}
	if (addr >= 0xf00000u && addr <= 0xf0ffffu) {
		if (!ms1Ram_) return 0xff;
		return ms1Ram_[addr & 0xffffu];
	}
	return 0xff;
}

void CHardAc::Sega68Write16(unsigned addr, uint16_t v)
{
	addr &= 0xfffffeu;
	if (addr >= 0xf00000u && addr <= 0xf0ffffu) {
		if (!ms1Ram_) return;
		const unsigned o = addr & 0xffffu;
		ms1Ram_[o] = (uint8_t)(v >> 8);
		ms1Ram_[o + 1] = (uint8_t)v;
		return;
	}
	Sega68Write8(addr, (uint8_t)(v >> 8));
	Sega68Write8(addr + 1, (uint8_t)v);
}

void CHardAc::Sega68Write8(unsigned addr, uint8_t v)
{
	if (addr >= 0xf00000u && addr <= 0xf0ffffu) {
		if (ms1Ram_) ms1Ram_[addr & 0xffffu] = v;
		return;
	}
	if (addr >= 0xc20000u && addr <= 0xc20003u) {
		SegaUartWrite((addr >> 1) & 1u, v);
		return;
	}
	/* MAME segam1audio_map maps both MultiPCMs and the YM3438 with
	   umask16(0x00ff), so only the odd (low) byte lane reaches them. Serving
	   the even lane as well made every word store hit the register twice -
	   once with the high half as bogus data - which is what turned daytona
	   and vf into a rail-to-rail buzz. */
	if (addr >= 0xc40000u && addr <= 0xc40007u) {
		if ((addr & 1u) && pcm_) pcm_->Write((addr >> 1) & 3u, v);
		return;
	}
	if (addr >= 0xc50000u && addr <= 0xc50001u) {
		/* m1_snd_mpcm_bnk1_w takes the whole word; bits are in the low half. */
		if (addr & 1u) CEmuChipMultiPcmSetBank(pcm_, v & 3u);
		return;
	}
	if (addr >= 0xc60000u && addr <= 0xc60007u) {
		if ((addr & 1u) && pcm2_) pcm2_->Write((addr >> 1) & 3u, v);
		return;
	}
	if (addr >= 0xc70000u && addr <= 0xc70001u) {
		if (addr & 1u) CEmuChipMultiPcmSetBank(pcm2_, v & 3u);
		return;
	}
	if (addr >= 0xd00000u && addr <= 0xd00007u) {
		if ((addr & 1u) && chip_) {
			chip_->Write((addr >> 1) & 3u, v);
			if ((addr & 2) == 2) opmWrites_++;
		}
		return;
	}
}

int CHardAc::LoadRomsSegaM1(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge || !segaM1Audio_) return 0;
	if (ms1Rom_) { free(ms1Rom_); ms1Rom_ = NULL; ms1RomSize_ = 0; }
	if (ms1Ram_) { free(ms1Ram_); ms1Ram_ = NULL; }
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
	if (pcmRom2_) { free(pcmRom2_); pcmRom2_ = NULL; pcmRom2Size_ = 0; }

	/* MAME M1AUDIO_CPU_REGION: ROM_LOAD16_WORD_SWAP at absolute offsets
	   (daytona epr-16720 @0, epr-16721 @0x20000). This is NOT odd/even byte
	   interleave — that scramble left the 68K executing garbage and only the
	   old MultiPCM host-poke path made noise. */
	unsigned romNeed = 0x40000u;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!CEmuAcIsCodeRomType(r->type)) continue;
		unsigned off = (unsigned)(r->offset < 0 ? 0 : r->offset);
		unsigned sz = 0;
		if (!CEmuZipFsFind(fs, r->name, &sz) || !sz) continue;
		if (off + sz > romNeed) romNeed = off + sz;
	}
	if (romNeed < 0x40000u) romNeed = 0x40000u;
	uint8_t* p = (uint8_t*)calloc(1, romNeed);
	if (!p) return 0;
	int anyCode = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!CEmuAcIsCodeRomType(r->type)) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		unsigned off = (unsigned)(r->offset < 0 ? 0 : r->offset);
		if (off >= romNeed) continue;
		unsigned n = sz;
		if (off + n > romNeed) n = romNeed - off;
		memcpy(p + off, data, n);
		/* ROM_LOAD16_WORD_SWAP: swap each 16-bit word in the loaded span. */
		for (unsigned j = 0; j + 1u < n; j += 2u) {
			const uint8_t t = p[off + j];
			p[off + j] = p[off + j + 1u];
			p[off + j + 1u] = t;
		}
		anyCode = 1;
	}
	if (!anyCode) { free(p); return 0; }
	ms1Rom_ = p;
	ms1RomSize_ = romNeed;

	ms1Ram_ = (uint8_t*)calloc(1, 0x10000);
	if (!ms1Ram_) return 0;

	/* pcm0 / pcm1 wave ROMs (up to 4MB each). */
	auto loadPcm = [&](const char* type, uint8_t** dst, unsigned* dstSz) {
		unsigned need = 0x400000u;
		uint8_t* buf = (uint8_t*)calloc(1, need);
		if (!buf) return;
		int any = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!r->type || _stricmp(r->type, type) != 0) continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			unsigned off = (unsigned)(r->offset < 0 ? 0 : r->offset);
			if (off >= need) continue;
			unsigned n = sz;
			if (off + n > need) n = need - off;
			memcpy(buf + off, data, n);
			any = 1;
		}
		if (!any) { free(buf); return; }
		*dst = buf;
		*dstSz = need;
	};
	loadPcm("pcm0", &pcmRom_, &pcmRomSize_);
	loadPcm("pcm1", &pcmRom2_, &pcmRom2Size_);
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
	if (pcm2_ && pcmRom2Size_) pcm2_->SetPcmRom(pcmRom2_, pcmRom2Size_);

	segaMidiHead_ = segaMidiTail_ = 0;
	segaMidiIrq_ = 0;
	soundCmd_ = 0;
	soundCmdWord_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();
	if (pcm2_) pcm2_->Reset();

	CEmuM68kBusSetMs1(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_set_int_ack_callback(NULL);
	m68k_pulse_reset();
	return 1;
}
