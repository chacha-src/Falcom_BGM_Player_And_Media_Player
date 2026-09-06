#pragma once
/* Shared OPNA/OPN2/MSX register shadow + dump writer for KPI FM monitor. */
#include "sasami_fmmon.h"
#include "fmmon_write.h"

#ifdef __cplusplus
extern "C" {
#endif

void FmMonShadowReset(void);
void FmMonShadowSetSource(const wchar_t* path);
void FmMonShadowSetSampleRate(uint32_t sr);
void FmMonShadowAddSamples(uint32_t n);
/* Platform + chip base label for FM monitor header (e.g. "PC-88","OPNA").
   Flush appends +EX/+ADPCM when applicable. Stored in dump.titleSjis. */
void FmMonShadowSetIdentity(const char* platform, const char* chip);
/* 1=OPNA(6ch+ADPCM)  0=OPN(3ch)  2=YM2610/OPNB(4ch+SSG+ADPCM-A/B)  -1=non-OPN(A). */
void FmMonShadowSetOpnaLayout(int layout);
/* addr: 0x000-0x1FF (port1 = 0x100|reg). */
void FmMonShadowWriteReg(unsigned addr, unsigned data);
/* AY-3-8910 register write → SSG shadow ($00-$0F). */
void FmMonShadowWriteAyReg(unsigned reg, unsigned data);
/* Soft MIDI / keys-only: ch 0..15, midiNote 0..127. */
void FmMonShadowMidiNote(int ch, int midiNote, int on);
/* Enter keys-only (clears OPL/MSX). Used by PC/AT BEEP / CMS / MPU. */
void FmMonShadowEnterKeysOnly(unsigned profile);
/* Shadow aux regs (PIT/SAA/…) for keys-only dumps with VIEW_REGS. */
void FmMonShadowWriteAuxReg(unsigned addr, unsigned data);
/* Extra PCM / PDX / ADPCM slots (0..PCM_MAX-1) for keys/hybrid dumps. */
void FmMonShadowPcmNote(int ch, int midiNote, int on);
/* Sample-SPU pitch rate (0x1000 = unity) → MIDI 0..127; <0 if invalid. */
int FmMonShadowPitchRateToMidi(unsigned pitchRate);
/* Hz → MIDI 0..127; <0 if invalid (NSF/SID style). */
int FmMonShadowHzToMidi(double freqHz);
/* Flush if dirty and ~4ms elapsed (or force). */
void FmMonShadowFlush(int force);
/* KEYSONLY dump from midi shadow (no regs). */
void FmMonShadowFlushKeysOnly(int force);
/* pad6[1] keys UI profile (SASAMI_FMMON_KEYS_*). Call before FlushKeysOnly. */
void FmMonShadowSetKeysProfile(unsigned profile);
/* OPM (YM2151) 256-reg snapshot for MDX — stored in dump.regs[0..255] + FLAG_OPM.
   keyRegOrNeg1: pass $08 write data to latch that channel's gate; -1 = regs only. */
void FmMonShadowSetOpmRegSnapshot(const unsigned char* regs256);
void FmMonShadowSetOpmRegSnapshotEx(const unsigned char* regs256, int keyRegOrNeg1);
void FmMonShadowApplyK054539Reg(unsigned ofs, unsigned data8);

/* MSX KSS: AY clock (Hz). Default OPNA-style until set. */
void FmMonShadowSetSsgClock(unsigned clockHz);
/* MSX profile: dumpFlags MSX + deviceMask in pad6[0]. */
void FmMonShadowSetMsxDevices(unsigned deviceMask);
/* YM2413/FMPAC: 64 regs. Maps ch0-5→FM, ch6-8→EX. */
void FmMonShadowApplyOpllRegs(const unsigned char* reg64);
/* Konami SCC: freq[5] 12bit, vol[5] 0..15, enableMask bits0-4. */
void FmMonShadowApplyScc(const unsigned* freq12, const unsigned* vol4, unsigned enableMask);
/* HuC6280 HES: 6 ch. period[6] 12bit, vol[6] 0..31, control[6] (bit7=on).
   ch0-2 → SSG, ch3-5 → pcm (UI labels SSG4-6). Sets DEV_PSG|DEV_HES. */
void FmMonShadowApplyHes(const unsigned* period12, const unsigned* vol5, const unsigned* control);

/* SN76489 / SMS PSG: tonePeriod[3] 10bit, noisePeriod 10bit (or 0),
   vol[4] 0..15 (chip scale: 0=loud), toneOnMask bits0-2 (+bit3 noise). */
void FmMonShadowApplySn76489(const unsigned* tonePeriod10, unsigned noisePeriod,
	const unsigned* vol4, unsigned onMask);
/* Stream parser for SN76489 write bytes (latch/data). */
void FmMonShadowWriteSnByte(unsigned data);

/* OPL2/3 (DRO / S98 OPL*). mode: 1=OPL2, 2=OPL3, 3=DualOPL2 (2×OPL2 as banks).
   WriteOplReg: addr bit8 = port/chip bank, low 8 = register. */
void FmMonShadowSetOplMode(unsigned mode);
void FmMonShadowWriteOplReg(unsigned addr, unsigned data);

/* Arcade / VGM PCM chips → keys-only PCM rows (set profile automatically). */
void FmMonShadowApplyQSoundReg(unsigned ofs, unsigned data16);
void FmMonShadowApplyRf5cReg(unsigned ofs, unsigned data8);
void FmMonShadowSetC352Clock(unsigned clockHz);
int FmMonShadowC352PitchToMidi(unsigned pitch);
void FmMonShadowApplyC352Reg(unsigned ofs, unsigned data16);
void FmMonShadowApplySegaPcmMem(unsigned addr, unsigned data8);
void FmMonShadowApplyOki6295(unsigned data8);
void FmMonShadowApplyGa20Reg(unsigned ofs, unsigned data8);

#ifdef __cplusplus
}
#endif
