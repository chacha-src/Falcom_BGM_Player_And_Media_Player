#include "StdAfx.h"

#include "cemu_driver_neogeo.h"

#include "../z80/Ay_Cpu.h"

#include <string.h>



CDriverNeo::CDriverNeo()

	: hw_(NULL)

	, hostRate_(44100)

	, cpuHz_(4000000)

	, ymHz_(8000000)

	, songCmd_(0x01)

	, ymResidual_(0)

	, cpuAcc_(0)

	, injected_(0)

	, injectAt_(0)

	, reinjected_(0)

{

}



CDriverNeo::~CDriverNeo()

{

	Close();

}



int CDriverNeo::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)

{

	if (!hw || !ge || !fs || hw->hardKind != CHard::KIND_NEO) return 0;

	hw_ = (CHardNeo*)hw;

	hostRate_ = hw_->SampleRate();

	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 4000000;

	ymHz_ = hw_->ymHz_ > 0 ? hw_->ymHz_ : 8000000;

	ymResidual_ = 0;

	cpuAcc_ = 0;

	injected_ = 0;

	reinjected_ = 0;



	/*

	 * Fixed pick: host title if in M1 range, else first catalog code 0x01..0x3F

	 * (prefer 0x21+). No try-table / peak hunt.

	 */

	songCmd_ = 0x20;

	{

		const uint8_t t = (uint8_t)(titleCode & 0xff);

		if (titleCode && t >= 0x01 && t <= 0x3f)

			songCmd_ = t;

		else {

			uint8_t pick = 0;

			for (int i = 0; i < ge->titleCount; i++) {

				const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

				if (c >= 0x21 && c <= 0x3f) { pick = c; break; }

			}

			if (!pick) {

				for (int i = 0; i < ge->titleCount; i++) {

					const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

					if (c >= 0x01 && c <= 0x3f) { pick = c; break; }

				}

			}

			if (pick)

				songCmd_ = pick;

		}

	}



	/* Catalog prefer is often SE 0x01 or empty fanfare 0x20. One fixed pass

	   onto the first 0x21..0x3F BGM when present - keep 0x02 logo (mslug). */

	if (songCmd_ == 0x01 || songCmd_ == 0x20) {

		uint8_t pick = 0;

		/* Prefer stage BGM 0x22+ over short title jingles at 0x21 (kof95). */

		for (int i = 0; i < ge->titleCount; i++) {

			const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

			if (c >= 0x22 && c <= 0x3f) { pick = c; break; }

		}

		if (!pick) {

			for (int i = 0; i < ge->titleCount; i++) {

				const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

				if (c >= 0x21 && c <= 0x3f) { pick = c; break; }

			}

		}

		if (pick) songCmd_ = pick;

	}


	if (!hw_->LoadRoms(fs, ge, titleCode))

		return 0;



	CEmuHardNeoSetActive(hw_);

	/* Early SNK: type table at (0173) — entries 1=SE, 2+=BGM. Prefer first

	   type>=2 with a live song pointer so titles don't share the flat SE

	   chirp from cmd 0x02 or land on an empty 0x20 fanfare slot. */

	{

		const uint8_t* m = hw_->Mem();

		if (m && m[0x172] == 0x21) {

			const uint16_t tab = (uint16_t)(m[0x173] | ((uint16_t)m[0x174] << 8));

			if (tab >= 0x8000u && tab < 0xf800u) {

				uint16_t songTab = 0;

				for (unsigned a = 0x0f00; a + 8u < 0x1100u; a++) {

					if (m[a] == 0xd6 && m[a + 1] == 0x20 && m[a + 5] == 0x21) {

						songTab = (uint16_t)(m[a + 6] | ((uint16_t)m[a + 7] << 8));

						break;

					}

				}

				auto songLive = [&](uint8_t cmd) -> int {

					if (!songTab || cmd < 0x20) return 1;

					const unsigned idx = (unsigned)(cmd - 0x20u) * 2u;

					const uint16_t p = (uint16_t)(m[(uint16_t)(songTab + idx)]

						| ((uint16_t)m[(uint16_t)(songTab + idx + 1)] << 8));

					return p != 0;

				};

				uint8_t bgm = 0;

				for (unsigned i = 0x21; i < 0x40; i++) {

					const uint8_t ty = m[(uint16_t)(tab + i)];

					if (ty >= 2 && ty <= 5 && songLive((uint8_t)i)) {

						bgm = (uint8_t)i;

						break;

					}

				}

				if (!bgm) {

					const uint8_t ty20 = m[(uint16_t)(tab + 0x20)];

					if (ty20 >= 2 && ty20 <= 5 && songLive(0x20))

						bgm = 0x20;

				}

				if (!bgm) {

					for (unsigned i = 2; i < 0x20; i++) {

						const uint8_t ty = m[(uint16_t)(tab + i)];

						if (ty >= 2 && ty <= 5) {

							bgm = (uint8_t)i;

							break;

						}

					}

				}

				const uint8_t curTy = (songCmd_ < 0x40)

					? m[(uint16_t)(tab + songCmd_)] : 0;

				const int curLive = (songCmd_ >= 0x20) ? songLive(songCmd_) : 0;

				if (bgm && (curTy < 2 || curTy > 5 || !curLive))

					songCmd_ = bgm;

				else if (bgm && titleCode && ((titleCode & 0xff) == 0x02u))

					songCmd_ = bgm;

			}

		}

	}

	/* Boot until NMI enable (OUT $08) or ~1s — KOF M1 enables after bank init. */

	RunUntil((uint64_t)cpuHz_ / 4);

	{

		Ay_Cpu* cpu = hw_->Cpu();

		for (int i = 0; i < 120 && cpu && !hw_->NmiEnabled(); i++)

			RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 60);

	}

	/*

	 * KOF-family M1 only: cold boot sets FE34=0xFF and song entry aborts while

	 * FE34!=0. Clear with $08/$07. Early drivers (mslug/bstars/…) never touch

	 * FE34 — sending $08 there queues a bogus song and leaves them SILENT.

	 */

	if (hw_->PeekRam(0xFE34) != 0) {

		SendZ80Command(0x08, 90);

		Ay_Cpu* cpu = hw_->Cpu();

		for (int i = 0; i < 120 && cpu; i++) {

			RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 60);

			if (hw_->PeekRam(0xFE34) == 0 && !hw_->SoundCmdPending()

				&& hw_->PeekRam(0xFEB0) == hw_->PeekRam(0xFEB1)

				&& hw_->PeekRam(0xFE47) == hw_->PeekRam(0xFE46))

				break;

		}

		if (hw_->PeekRam(0xFE34) != 0)

			SendZ80Command(0x07, 90);

		for (int i = 0; i < 60 && cpu && hw_->PeekRam(0xFE34) != 0; i++)

			RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 60);

	}

	/* Early SNK: boot latches FE30=FF as a BGM lock; type-2+ song start at

	   $0F04 does JP NZ,$119F (POP/RET) while set. 68K clears it in-game -

	   host poke so real BGM slots (0x20+) can run. Do not require the

	   (0172)==LD HL marker - viewpoin/mutnat/pbobblen use other prologues

	   but still park the FF lock at FE30. */

	/* Early SNK: boot latches FE30=FF as a BGM lock; type-2+ song start at

	   \ does JP NZ,\ (POP/RET) while set. 68K clears it in-game -

	   host poke so real BGM slots (0x20+) can run. Do not require the

	   (0172)==LD HL marker - viewpoin/mutnat/pbobblen use other prologues

	   but still park the FF lock at FE30. */

	if (hw_->PeekRam(0xFE34) == 0 && hw_->PeekRam(0xFE30) == 0xff) {

		uint8_t* m = hw_->Mem();

		if (m) {

			m[0xFE30] = 0;

			m[0xFE31] = 0;

		}

	}

	/* AOF3/KOF95-class: boot DEC A;LD (FE21)/(FE22),A leaves FF. BGM entry

	   at \ does OR A;RET NZ on FE21 - without a 68K enable every 0x20+

	   command returns before bank/song setup (only cmd 0x02 logo still plays). */

	if (hw_->PeekRam(0xFE21) == 0xff) {

		uint8_t* m = hw_->Mem();

		if (m) {

			m[0xFE21] = 0;

			m[0xFE22] = 0;

		}

	}

	/* Mag Drop 3-class: same boot DEC A lock at FE1C/FE1D; BGM entry OR A;RET NZ. */

	if (hw_->PeekRam(0xFE1C) == 0xff) {

		uint8_t* m = hw_->Mem();

		if (m) {

			m[0xFE1C] = 0;

			m[0xFE1D] = 0;

		}

	}


	SendZ80Command(songCmd_, 120);

	injectAt_ = 0;

	injected_ = 1;

	return 1;

}



void CDriverNeo::WaitQueueIdle(int maxFrames60)

{

	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;

	if (!cpu || cpuHz_ < 1) return;

	const int early = (hw_->PeekRam(0xFE34) == 0

		&& hw_->Mem() && hw_->Mem()[0x172] == 0x21) ? 1 : 0;

	int sawCmd = 0;

	for (int i = 0; i < maxFrames60; i++) {

		RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 60);

		if (hw_->SoundCmdPending()) {

			sawCmd = 1;

			continue;

		}

		const uint8_t kofW = hw_->PeekRam(0xFEB0);

		const uint8_t kofR = hw_->PeekRam(0xFEB1);

		if (early) {

			if (sawCmd && i >= 30)

				return;

			continue;

		}

		const uint8_t earlyW = hw_->PeekRam(0xFE47);

		const uint8_t earlyR = hw_->PeekRam(0xFE46);

		if (kofW == kofR && earlyW == earlyR)

			return;

	}

}



void CDriverNeo::SendZ80Command(uint8_t cmd, int maxFrames60)

{

	if (!hw_ || !cmd) return;

	WaitQueueIdle(maxFrames60 > 0 ? maxFrames60 : 60);

	hw_->SetSoundCommand(cmd);

	WaitQueueIdle(maxFrames60 > 0 ? maxFrames60 : 60);

}



void CDriverNeo::Close()

{

	hw_ = NULL;

}



void CDriverNeo::TickYm(uint64_t cpuCycles)

{

	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;

	ymResidual_ += cpuCycles * (uint64_t)ymHz_;

	const uint64_t ticks = ymResidual_ / (uint64_t)cpuHz_;

	ymResidual_ %= (uint64_t)cpuHz_;

	if (ticks)

		hw_->SoundChip()->AdvanceClocks(ticks);

}



void CDriverNeo::DeliverIrqs()

{

	if (!hw_ || !hw_->Cpu()) return;

	Ay_Cpu* cpu = hw_->Cpu();

	CChip* chip = hw_->SoundChip();

	if (hw_->TakeNmiPulse())

		Ay_CpuNmi(cpu);

	/* YM2610 timer IRQ → Z80 IM1. Prefer Irq(); also accept status timer

	   flags so a missed ymfm_update_irq edge cannot stall the sequencer.

	   Only interrupt when IFF1 is set — ForceIm1 during DI nests/breaks

	   early M1 busy-waits (nam1975 @2282). */

	if (!chip || cpu->r.im != 1 || !cpu->r.iff1)

		return;

	const int st = (chip->ReadStatus() & 0x03) != 0;

	if (chip->Irq() || st)

		Ay_CpuIm1Interrupt(cpu);

}



void CDriverNeo::RunUntil(uint64_t endCycle)

{

	if (!hw_ || !hw_->Cpu()) return;

	Ay_Cpu* cpu = hw_->Cpu();

	CEmuHardNeoSetActive(hw_);

	int guard = 0;

	while ((uint64_t)cpu->time() < endCycle && guard++ < 2000000) {

		DeliverIrqs();

		const int cycles = Ay_CpuRunOne(cpu);

		if (cycles <= 0) {

			cpu->adjust_time(4);

			hw_->AddCpuCycles(4);

			TickYm(4);

			continue;

		}

		hw_->AddCpuCycles((uint64_t)cycles);

		TickYm((uint64_t)cycles);

	}

}



void CDriverNeo::InjectSongCommand()

{

	if (!hw_ || !songCmd_) return;

	hw_->SetSoundCommand(songCmd_);

	injected_ = 1;

}



int CDriverNeo::Render(int16_t* stereo, int frames)

{

	if (!hw_ || !stereo || frames <= 0) return 0;

	Ay_Cpu* cpu = hw_->Cpu();

	CChip* chip = hw_->SoundChip();

	if (!cpu || !chip || hostRate_ < 1 || cpuHz_ < 1) return 0;

	CEmuHardNeoSetActive(hw_);



	for (int i = 0; i < frames; i++) {

		const uint64_t now = (uint64_t)cpu->time();

		if (!injected_)

			InjectSongCommand();

		/* One re-send if latch unread after ~0.25s (late handler). */

		if (injected_ && !reinjected_ && hw_->SoundCmdPending()

			&& now >= (uint64_t)cpuHz_ / 4) {

			hw_->SetSoundCommand(songCmd_);

			reinjected_ = 1;

		}

		cpuAcc_ += (int64_t)cpuHz_;

		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);

		cpuAcc_ %= (int64_t)hostRate_;

		if (cyclesPerSample < 1) cyclesPerSample = 1;

		const uint64_t end = now + (uint64_t)cyclesPerSample;

		while ((uint64_t)cpu->time() < end) {

			DeliverIrqs();

			const int cycles = Ay_CpuRunOne(cpu);

			if (cycles <= 0) {

				cpu->adjust_time(4);

				hw_->AddCpuCycles(4);

				TickYm(4);

				continue;

			}

			hw_->AddCpuCycles((uint64_t)cycles);

			TickYm((uint64_t)cycles);

		}

		chip->Render(stereo + i * 2, 1);

	}

	return frames;

}



int CDriverNeo::Seek(uint64_t sample)

{

	(void)sample;

	return 0;

}



CDriver* CDriverNeoCreate()

{

	return new CDriverNeo();

}


