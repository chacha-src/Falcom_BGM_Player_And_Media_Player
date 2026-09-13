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
	/* HD63701V0: 128B at $80-FF (MAME hd6801_mem). CUS60 also RAM-tests
	   through $017F on song-start (F2E3 from [AE+33]); keep $40-$1FF as
	   internal RAM so that compare-back does not fall into F33F/F0DC. */
	if (addr >= 0x0040u && addr <= 0x01ffu)
		return hd63701Ram_[addr];
	if ((addr & 0xfc00u) == 0x1000u) {
		const unsigned off = addr & 0x3ffu;
		/* CUS60 F0DC: wait $1181=$A6 after $1180=$A6 (main 6809 doorbell).
		   No host CPU here — return A6 so the wait completes. IRQ nesting
		   was what turned F33F's follow-up JSR [AE+4] into a reboot storm. */
		if (off == 0x181u && namcoCus30_[0x180u] == 0xa6u)
			return 0xa6u;
		return namcoCus30_[off];
	}
	if (wsg63701_ && addr <= 0x03ffu)
		return chip_ ? CEmuChipC30Read(chip_, addr) : namcoCus30_[addr];

	/* skykid.cpp mcu_map: work RAM $C000-C7FF (drgnbstr/pacland/skykid). */
	if (wsg63701_ && addr >= 0xc000u && addr <= 0xc7ffu && hd63701Rom_)
		return hd63701Rom_[addr];

	if (!wsg63701_ && addr >= 0x1400u && addr <= 0x1fffu)
		return hd63701Ram_[0x200u + (addr - 0x1400u)];
	/* MAME hopmappy YM $2000, genpeitd $2800, wndrmomo $3800, roishtar $6000.
	   CUS60 STA $2000 stub stays on non-roishtar maps. Do not decode $6000
	   as YM when that byte is MCU ROM ($4000-$BFFF on expanded games). */
	if (!wsg63701_) {
		/* MAME: one YM pair per game. FBNeo's write handler lists all four
		   but MapMemory ROM at $4000-7FFF wins, so wndrmomo checksums
		   $4000-$BFFF as ROM (including $6000). Decoding every pair here
		   made F124 sum fail (fault 3) and 80A9 reboot forever. CUS60's
		   STA $2000 stub stays live except on roishtar (ROM $2000-3FFF). */
		const uint16_t ymEven = (uint16_t)(addr & 0xfffeu);
		const uint16_t ymBase = hd63701YmBase_ ? hd63701YmBase_ : 0x2000u;
		const int roishtarRom = (hd63701MapKind_ == 1
			&& addr >= 0x2000u && addr <= 0x3fffu);
		if (!roishtarRom && (ymEven == ymBase || ymEven == 0x2000u))
			return chip_ ? chip_->ReadStatus() : 0;
		if (!roishtarRom) {
			const uint16_t blk = (uint16_t)(addr & 0xff00u);
			const uint16_t off = (uint16_t)(addr & 0x00ffu);
			if ((blk == ymBase || blk == 0x2000u)
				&& (off == 0x20u || off == 0x21u || off == 0x30u || off == 0x31u))
				return 0xff;
		}
	}
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
	if (addr >= 0x0040u && addr <= 0x01ffu) {
		hd63701Ram_[addr] = v;
		return;
	}
	if ((addr & 0xfc00u) == 0x1000u) {
		/* $1182=$A6 is required: IRQ vector [AE+8] only runs the AE+20..+28
		   music chain while the doorbell is A6 (else RTI after AA/+2C). */
		const unsigned off = addr & 0x3ffu;
		namcoCus30_[off] = v;
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) c30->Write(off, v);
		if (wsg63701_) opmWrites_++;
		return;
	}
	/* pacland/skykid: some MCU builds also poke 15XX via low amap. */
	if (wsg63701_ && addr <= 0x03ffu) {
		namcoCus30_[addr] = v;
		if (chip_) { chip_->Write(addr, v); opmWrites_++; }
		return;
	}
	if (wsg63701_ && addr >= 0xc000u && addr <= 0xc7ffu && hd63701Rom_) {
		hd63701Rom_[addr] = v;
		return;
	}
	if (!wsg63701_ && addr >= 0x1400u && addr <= 0x1fffu) {
		/* 8259 packs DSW/IN at $1400 (32 bytes). If $C8 is left at the
		   F20A dest ($14F0) the bit-unpack at 8287 walks into the vector
		   table (14F8=478F → TRAP → FF78 fault 8). Keep $14F0-$156B for
		   F14A/F20A (PC $F364) and the 813E wipe (PC $814D). */
		if (addr >= 0x14f0u && addr < 0x156cu && hd63701_) {
			const uint16_t pc = HD63701Pc(hd63701_);
			if (pc >= 0x8240u && pc < 0x82f0u)
				return;
		}
		hd63701Ram_[0x200u + (addr - 0x1400u)] = v;
		return;
	}
	if (!wsg63701_) {
		const uint16_t ymEven = (uint16_t)(addr & 0xfffeu);
		const uint16_t ymBase = hd63701YmBase_ ? hd63701YmBase_ : 0x2000u;
		const int roishtarRom = (hd63701MapKind_ == 1
			&& addr >= 0x2000u && addr <= 0x3fffu);
		if (!roishtarRom && (ymEven == ymBase || ymEven == 0x2000u)) {
			if (chip_) {
				chip_->Write(addr & 1u, v);
				if (addr & 1u) {
					opmWrites_++;
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
	} else {
		/* Host doorbell: $1183=cmd, $B0=0, $1182=$A6. F4B1 consumes A6 and
		   starts the song (then SEI). Sys86RunCycles re-asserts A6 + CLI and
		   HOLDs a 60 Hz IRQ so [AE+8] runs the music chain. */
		hd63701Ram_[0xb0u] = 0;
		namcoCus30_[0x183] = cmd;
		namcoCus30_[0x182] = 0xa6;
		/* $1191 is the CUS60 SFX table (1-7). Index 6 JSRs RESET (F4AE).
		   BGM stays in $1183 for F4B1; do not mirror the song id here. */
		CChip* c30 = pcm_ ? pcm_ : (wsg63701_ ? chip_ : NULL);
		if (c30) {
			c30->Write(0x183, cmd);
			c30->Write(0x182, 0xa6);
			c30->Write(0x380, cmd); /* $1380: YM request latch (846B) */
		}
		namcoCus30_[0x380u] = cmd;
		if (wsg63701_ && chip_)
			CEmuChipC30SetEnable(chip_, 1);
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
		if (hd63701MapKind_ == 1 && sz >= 0x8000u) {
			/* MAME roishtar_mcu_map: mcusub+2000 at $2000, mcusub+4000 at $8000.
			   YM @6000 overlays; do not paint the 32K image from $4000. */
			memcpy(hd63701Rom_ + 0x2000, data + 0x2000, 0x2000);
			memcpy(hd63701Rom_ + 0x8000, data + 0x4000, 0x4000);
		} else {
			memcpy(hd63701Rom_ + base, data, n);
			/* FBNeo memcpy(DrvMCUROM, +0x4000, 0x4000) fills the image only —
			   CPU reads at $0000-$3FFF go through the YM/port handler, not ROM.
			   Painting that mirror into the executable image made $2000/$2800
			   look like program bytes (wndrmomo $FF → YM busy-wait forever). */
		}
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
	if (pcm_)
		CEmuChipC30SetEnable(pcm_, 1);
	return 1;
}

int CHardAc::LoadRomsWsg63701(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* pacland / skykid / hopmappy / metrocrs: HD63701 + CUS30 MAPPY (no YM).
	   Reuse Sys86 ROM loader (CUS60/63 @F000 + external @8000) then attach
	   wave PROM to the C30 chip. */
	if (!hd63701_ || !chip_ || !fs || !ge) return 0;
	if (!LoadRomsSys86(fs, ge)) return 0;
	/* CUS30 wave RAM is filled by the MCU. Do not attach the first 256-byte
	   member (usually a color PROM) as a waveform table. */
	CEmuChipC30SetEnable(chip_, 1);
	return 1;
}
