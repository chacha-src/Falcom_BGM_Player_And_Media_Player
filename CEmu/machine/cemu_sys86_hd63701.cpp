/* Sys86 HD63701 implementation helpers ? included into cemu_hard_ac.cpp context
   via direct compilation as part of hard_ac edits. Standalone translation unit. */
#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_hd63701_bus.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_c30.h"
#include "../vendor/hd63701/hd63701core.h"
#include "../vendor/hd63701/burnint.h"
#include "../vendor/hd63701/m6800.h"
#include <string.h>
#include <stdlib.h>

static int Sys86ContainsI(const char* s, const char* needle)
{
	if (!s || !needle || !needle[0]) return 0;
	for (; *s; s++) {
		const char* a = s;
		const char* b = needle;
		while (*a && *b) {
			char ca = *a, cb = *b;
			if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
			if (ca != cb) break;
			a++; b++;
		}
		if (!*b) return 1;
	}
	return 0;
}

uint8_t CHardAc::HD63701Read8(uint16_t addr)
{
	if ((addr & 0xffe0u) == 0x0000u)
		return (uint8_t)m6803_internal_registers_r((unsigned short)(addr & 0x1fu));
	if ((addr & 0xff80u) == 0x0080u)
		return hd63701Ram_[addr & 0xffu];
	if ((addr & 0xfc00u) == 0x1000u)
		return namcoCus30_[addr & 0x3ffu];
	if (wsg63701_ && addr <= 0x03ffu)
		return chip_ ? CEmuChipC30Read(chip_, addr) : namcoCus30_[addr];

	if (addr >= 0x1400u && addr <= 0x1fffu)
		return hd63701Ram_[0x100u + (addr - 0x1400u)];
	/* CUS60 library uses YM @2000; game subprograms use map-specific bases. */
	const uint16_t ym0 = hd63701YmBase_ ? hd63701YmBase_ : (uint16_t)0x2000u;
	if (addr == 0x2000u || addr == 0x2001u || addr == ym0 || addr == (uint16_t)(ym0 + 1u))
		return chip_ ? chip_->ReadStatus() : 0x80;
	/* Inputs / DSW — idle highs so attract can run. */
	if (addr == 0x2020u || addr == 0x2021u || addr == 0x2030u || addr == 0x2031u
		|| addr == (uint16_t)(ym0 + 0x20u) || addr == (uint16_t)(ym0 + 0x21u)
		|| addr == (uint16_t)(ym0 + 0x30u) || addr == (uint16_t)(ym0 + 0x31u))
		return 0xff;
	if (hd63701Rom_ && addr < 0x10000u)
		return hd63701Rom_[addr];
	return 0xff;
}

void CHardAc::HD63701Write8(uint16_t addr, uint8_t v)
{
	if ((addr & 0xffe0u) == 0x0000u) {
		m6803_internal_registers_w((unsigned short)(addr & 0x1fu), v);
		return;
	}
	if ((addr & 0xff80u) == 0x0080u) {
		hd63701Ram_[addr & 0xffu] = v;
		return;
	}
	if ((addr & 0xfc00u) == 0x1000u) {
		/* $1182=$A6 is required: IRQ vector [AE+8] only runs the AE+20..+28
		   music chain while the doorbell is A6 (else RTI after AA/+2C). */
		namcoCus30_[addr & 0x3ffu] = v;
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) c30->Write(addr & 0x3ffu, v);
		if (wsg63701_) opmWrites_++;
		return;
	}
	/* pacland/skykid: some MCU builds also poke 15XX via low amap. */
	if (wsg63701_ && addr <= 0x03ffu) {
		namcoCus30_[addr] = v;
		if (chip_) { chip_->Write(addr, v); opmWrites_++; }
		return;
	}
	if (addr >= 0x1400u && addr <= 0x1fffu) {
		hd63701Ram_[0x100u + (addr - 0x1400u)] = v;
		return;
	}
	{
		const uint16_t ym0 = hd63701YmBase_ ? hd63701YmBase_ : (uint16_t)0x2000u;
		if (addr == 0x2000u || addr == 0x2001u || addr == ym0 || addr == (uint16_t)(ym0 + 1u)) {
			if (chip_) {
				chip_->Write(addr & 1u, v);
				if (addr & 1u) {
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
					hd63701YmWrites_++;
				}
			}
			return;
		}
	}
	/* IRQ/watchdog strobes — ignore. */
}

uint8_t CHardAc::HD63701PortRead(uint16_t port)
{
	(void)port;
	return 0xff;
}

void CHardAc::HD63701PortWrite(uint16_t port, uint8_t v)
{
	(void)port;
	(void)v;
}

void CHardAc::HD63701InjectSong(uint8_t cmd)
{
	soundCmd_ = cmd;
	soundCmdPending_ = 1;
	/* CUS60 song mailbox (see F4B1 / main loop F07C):
	   - stop: $1183=0 and clear $B0; also CLR $1182 so the next start is edged
	   - start: $1183=cmd, $B0=0; main loop stores $1182=$A6 then JSRs F4B1
	   - ensure AE vector base (0x11C0) so IRQ/main call tables stay valid */
	if (hd63701Ram_[0xaeu] == 0 && hd63701Ram_[0xafu] == 0) {
		/* 6800 STX stores hi then lo — AE must be 11C0 BE. */
		hd63701Ram_[0xaeu] = 0x11;
		hd63701Ram_[0xafu] = 0xc0;
		if (hd63701Rom_ && (namcoCus30_[0x1c0] | namcoCus30_[0x1c1]) == 0)
			memcpy(namcoCus30_ + 0x1c0, hd63701Rom_ + 0xf18e, 0x7c);
	}
	if (cmd == 0) {
		hd63701Ram_[0xb0u] = 0;
		namcoCus30_[0x182] = 0;
		namcoCus30_[0x183] = 0;
		namcoCus30_[0x191] = 0;
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) {
			c30->Write(0x182, 0);
			c30->Write(0x183, 0);
			c30->Write(0x191, 0);
		}
		if (wsg63701_ && chip_) {
			static const uint16_t kSlots[] = {
				0x40, 0x41, 0x50, 0x55, 0x60, 0x80, 0x81,
				0xa0, 0xa1, 0xaf, 0xb0, 0xb1, 0xc0, 0xc8, 0xc9, 0xce
			};
			for (unsigned i = 0; i < sizeof(kSlots) / sizeof(kSlots[0]); i++)
				chip_->Write(kSlots[i], 0);
		}
	} else {
		/* Host doorbell: $1183=cmd, $B0=0, $1182=$A6. F4B1 consumes A6 and
		   starts the song (then SEI). Sys86RunCycles re-asserts A6 + CLI and
		   HOLDs a 60 Hz IRQ so [AE+8] runs the music chain. */
		hd63701Ram_[0xb0u] = 0;
		namcoCus30_[0x183] = cmd;
		namcoCus30_[0x182] = 0xa6;
		if (cmd <= 7u)
			namcoCus30_[0x191] = cmd;
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) {
			c30->Write(0x183, cmd);
			c30->Write(0x182, 0xa6);
			if (cmd <= 7u) c30->Write(0x191, cmd);
		}
		if (wsg63701_ && chip_) {
			CEmuChipC30SetEnable(chip_, 1);
			static const uint16_t kSlots[] = {
				0x40, 0x41, 0x50, 0x55, 0x60, 0x80, 0x81,
				0xa0, 0xa1, 0xaf, 0xb0, 0xb1, 0xc0, 0xc8, 0xc9, 0xce
			};
			for (unsigned i = 0; i < sizeof(kSlots) / sizeof(kSlots[0]); i++)
				chip_->Write(kSlots[i], cmd);
		}
	}
	if (hd63701_)
		HD63701SetInputLine(hd63701_, HD63701_LINE_IRQ, HD63701_CLEAR_LINE);
}

int CHardAc::LoadRomsSys86(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!hd63701_ || !fs || !ge) return 0;
	memset(hd63701Ram_, 0, sizeof(hd63701Ram_));
	memset(namcoCus30_, 0, sizeof(namcoCus30_));
	hd63701YmWrites_ = 0;

	/* Map kind from archive name (FBNeo nSubCPUConfig). */
	hd63701MapKind_ = 0;
	hd63701YmBase_ = 0x2000;
	if (_stricmp(ge->archive, "genpeitd") == 0) {
		hd63701MapKind_ = 2;
		hd63701YmBase_ = 0x2800;
	} else if (_strnicmp(ge->archive, "rthunder", 8) == 0) {
		hd63701MapKind_ = 3;
		hd63701YmBase_ = 0x2000;
	} else if (_stricmp(ge->archive, "wndrmomo") == 0) {
		hd63701MapKind_ = 4;
		hd63701YmBase_ = 0x3800;
	} else if (_stricmp(ge->archive, "roishtar") == 0) {
		hd63701MapKind_ = 1;
		hd63701YmBase_ = 0x6000;
	}

	if (hd63701Rom_) { free(hd63701Rom_); hd63701Rom_ = NULL; hd63701RomSize_ = 0; }
	hd63701Rom_ = (uint8_t*)calloc(1, 0x10000);
	if (!hd63701Rom_) return 0;
	hd63701RomSize_ = 0x10000;

	/* Internal CUS60 @ F000 (4K). */
	int gotInt = 0, gotExt = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const unsigned sz = fs->files[i].size;
		const unsigned char* data = fs->files[i].data;
		if (!data || data == (const unsigned char*)1) continue;
		if (!gotInt && sz == 0x1000u
			&& (Sys86ContainsI(pathA, "cus60") || Sys86ContainsI(pathA, "cus63")
				|| Sys86ContainsI(pathA, "mcu"))) {
			/* Match cus60-*.bin / cus63-*.bin / rt1-mcu.bin / pl1-mcu.bin. */
			memcpy(hd63701Rom_ + 0xf000, data, 0x1000);
			gotInt = 1;
		}
	}
	/* External subprogram. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sound") != 0
			&& _stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "mcu") != 0
			&& _stricmp(r->type, "sub") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x1000u || sz > 0x10000u) continue;
		if (sz == 0x1000u) continue; /* internal already handled */
		unsigned base = 0x8000u;
		if (hd63701MapKind_ == 2 || hd63701MapKind_ == 3 || hd63701MapKind_ == 4
			|| hd63701MapKind_ == 1)
			base = 0x4000u; /* FBNeo: 8K/32K external MCU code at $4000+ */
		else if (sz <= 0x4000u)
			base = 0x8000u;
		const unsigned n = (sz > (0x10000u - base)) ? (0x10000u - base) : sz;
		memcpy(hd63701Rom_ + base, data, n);
		/* Mirror first 16K like FBNeo (DrvMCUROM, DrvMCUROM+0x4000, 0x4000). */
		if (n >= 0x4000u)
			memcpy(hd63701Rom_, hd63701Rom_ + 0x4000u, 0x4000u);
		gotExt = 1;
		break;
	}
	if (!gotExt) {
		/* Heuristic: largest 8K?32K non-wave member. */
		int best = -1;
		unsigned bestSz = 0;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 0x2000u || sz > 0x8000u) continue;
			if (Sys86ContainsI(pathA, "cus60") || Sys86ContainsI(pathA, "mcu")) continue;
			if (Sys86ContainsI(pathA, "wave") || Sys86ContainsI(pathA, "pcm")) continue;
			if (sz > bestSz) { bestSz = sz; best = i; }
		}
		if (best >= 0) {
			unsigned base = (hd63701MapKind_ >= 1 && hd63701MapKind_ <= 4)
				? 0x4000u : 0x8000u;
			const unsigned n = (bestSz > (0x10000u - base)) ? (0x10000u - base) : bestSz;
			memcpy(hd63701Rom_ + base, fs->files[best].data, n);
			if (n >= 0x4000u)
				memcpy(hd63701Rom_, hd63701Rom_ + 0x4000u, 0x4000u);
			gotExt = 1;
		}
	}
	if (!gotInt) return 0;

	/* Optional 63701x PCM sample ROMs �� leave unloaded (CUS30+YM is enough for PLAY). */
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();

	CEmuHD63701BusSetAc(this);
	CEmuHD63701BusAttach(hd63701_, this);
	HD63701Reset(hd63701_);
	return 1;
}

int CHardAc::LoadRomsWsg63701(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* pacland / skykid / hopmappy / metrocrs: HD63701 + CUS30 MAPPY (no YM).
	   Reuse Sys86 ROM loader (CUS60/63 @F000 + external @8000) then attach
	   wave PROM to the C30 chip. */
	if (!hd63701_ || !chip_ || !fs || !ge) return 0;
	if (!LoadRomsSys86(fs, ge)) return 0;
	/* Wave PROM (256/512). */
	for (int i = 0; i < fs->fileCount; i++) {
		const unsigned sz = fs->files[i].size;
		if (sz != 256u && sz != 512u) continue;
		chip_->SetPcmRom(fs->files[i].data, sz);
		break;
	}
	CEmuChipC30SetEnable(chip_, 1);
	return 1;
}
