#include "StdAfx.h"
#include "cemu_driver_ac.h"
#include "../machine/cemu_m68k_bus.h"
#include <stdio.h>
#include "../machine/cemu_v35_bus.h"
#include "../machine/cemu_h6280_bus.h"
#include "../machine/cemu_h8_bus.h"
#include "../machine/cemu_hd63701_bus.h"
#include "../machine/cemu_irem_cpu_tables.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_opl.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/Ay_Cpu.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
#include "../vendor/v35/v35core.h"
#include "../vendor/h6280/h6280core.h"
#include "../vendor/h8/h8core.h"
#include "../vendor/hd63701/hd63701core.h"
#include "../vendor/mc6809/mc6809.h"
#include "../vendor/m6803/m6803.h"
#include "../vendor/m6502/m6502core.h"
#include "../vendor/m37710/m37702core.h"
}
#include "../machine/cemu_m37702_bus.h"
#include <string.h>
#include <stdlib.h>

/* Sega System16 / Capcom CPS1 sound CPUs typically wait for latch+NMI/IRQ.
   Without the main 68K we inject a short command sequence after boot.
   Keep 0x81+ first (shinobi). Cotton only accepts 0x10..0x27 ? try those after. */
static const uint8_t kSys16TryCmds[] = {
	0x81, 0x82, 0x83, 0x84, 0x85, 0x01, 0x02, 0x03, 0x40, 0x41, 0x90, 0xa0,
	0x91, 0x92, 0xb0, 0xc0, 0xc5, 0xa3,
	0x12, 0x10, 0x1A, 0x22, 0x14, 0x20, 0x18, 0x24
};
/* Avoid 0xF0/0xFF ? stop/fade on CPS1. Prefer 0x40+ (ffight BGM) before low
   SE codes that BLAST then silence. Keep 0x01 late ? it BLASTS on ver2. */
static const uint8_t kCps1TryCmds[] = {
	0x40, 0x41, 0x50, 0x42, 0x55, 0x57, 0x10, 0x12, 0x20, 0x30, 0x02, 0x03,
	0x04, 0x01, 0x80, 0x81
};
/* Capcom GNG BGM ? lead with codes that sustain on avengers/commando/gunsmoke;
   0x2b/0x23/0x1A are AUDITION (flat 32768) on several titles. */
static const uint8_t kGngTryCmds[] = {
	0x21, 0x22, 0x28, 0x29, 0x25, 0x35, 0x31, 0x2c, 0x36, 0x33, 0x2d, 0x2e
};
/* Sega OutRun / After Burner BGM (0x81+ are often SE on AB). */
static const uint8_t kOutRunTryCmds[] = {
	0x84, 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88
};
static const uint8_t kAburnerTryCmds[] = {
	0x92, 0x91, 0x95, 0x94, 0x93, 0x96, 0x90, 0x97
};
static const uint8_t kCpsQsTryCmds[] = {
	0x01, 0x02, 0x03, 0x04, 0x10, 0x11, 0x20, 0x21, 0x30, 0x40, 0x80, 0x81
};
static const uint8_t kKonamiTryCmds[] = {
	0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x20, 0x21, 0x30, 0x40, 0x80, 0x81
};
static const uint8_t kNamcoTryCmds[] = {
	/* Sys12 C76: many catalog prefers (0x01/0x10) are silent or flat drones;
	   0x20/0x08/0x30 are sustained BGM on aquarush/ehrgeiz/golgo13/�c.
	   0x01 early: kaiunqz/mdhorse sustained themes. */
	0x20, 0x08, 0x30, 0x01, 0x10, 0x18, 0x28, 0x40, 0x04, 0x02, 0x03, 0x80
};
static const uint8_t kSys18TryCmds[] = {
	0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x90, 0x91, 0x10, 0x12, 0x20, 0x22
};
/* Taito TC0140SYT games number BGM from 0x01 upward; 0x00 is "stop". */
static const uint8_t kTaitoTryCmds[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12
};
/* Irem M72/M84: BGM at 0x20+index (m99/bbmanw) or 0x30+ (imgfight/loht);
   low codes are often mode/SE. Include 0x80+ for matchit. */
static const uint8_t kIremTryCmds[] = {
	0x74, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x40, 0x41, 0x50, 0x60,
	0x70, 0x71, 0x72, 0x73, 0x75, 0x80, 0x81, 0x82, 0x01, 0x0b, 0x0a, 0x02, 0x03, 0x04, 0x10
};
/* Irem M92: same command nibble family as M72 ? BGM starts at 0x20. */
static const uint8_t kM92TryCmds[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
	0x2c, 0x2d, 0x2e, 0x2f, 0x01, 0x02, 0x03, 0x04, 0x80, 0x81
};
/* Sega System1/2 BGM commands ? prefer 0x80+ sustained themes. */
static const uint8_t kSys1TryCmds[] = {
	0x81, 0x82, 0x83, 0x80, 0x84, 0x85, 0x86, 0x88, 0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
};
/* Konami Scramble / Time Pilot ? 0x0B is a known sustained BGM.
   GX400 (gradius) uses 0x40+; try-table below is overridden per-board. */
static const uint8_t kKonamiAyTryCmds[] = {
	0x0b, 0x09, 0x0e, 0x0a, 0x08, 0x0c, 0x0d, 0x0f, 0x07, 0x06,
	0x10, 0x14, 0x1a, 0x20, 0x21, 0x01, 0x02, 0x03
};
/* GX400 ISR @0085 only accepts latch==1 (channel init); other codes RET NZ.
   Catalog 0x40+/0x80+ are 68k-side ids ? map them onto 0x01 for the latch. */
static const uint8_t kGx400TryCmds[] = {
	0x01, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0x91, 0x92, 0x93, 0xa0
};
/* Technos Double Dragon 2 / China Gate / WWF. */
static const uint8_t kDdragon2TryCmds[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
	0x0d, 0x0e, 0x0f, 0x20, 0x22
};
/* Taito flstory: prefer MSM-group songs (arm 0x90 �� C500 @08E7). */
static const uint8_t kFlstoryTryCmds[] = {
	0x15, 0x16, 0x18, 0x05, 0x03, 0x06, 0x08, 0x0e, 0x0f, 0x01, 0x04
};

CDriverAc::CDriverAc()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(4000000)
	, opmHz_(4000000)
	, booted_(0)
	, triggered_(0)
	, pinned_(0)
	, songCmd_(0x81)
	, songCmdWord_(0)
	, opmResidual_(0)
	, cpuAcc_(0)
	, cmdIndex_(0)
	, nextCmdAt_(0)
	, nextGngIrq_(0)
	, k054539TimerState_(0)
	, k054539Residual_(0)
	, nextM72Nmi_(0)
	, m72FakeNmi_(0)
	, hasCpu_(1)
	, ms1_(0)
	, ms1Acc_(0)
	, m92_(0)
	, m92Acc_(0)
	, m92OpmRes_(0)
	, deco_(0)
	, decoAcc_(0)
	, decoChipRes_(0)
	, decoNextYmIrq_(0)
	, h8Board_(0)
	, h8Acc_(0)
	, m37702Board_(0)
	, m37702Acc_(0)
	, namcoM6809_(0)
	, namcoAcc_(0)
	, sys86_(0)
	, sys86Acc_(0)
	, m62_(0)
	, m62Acc_(0)
	, sega68_(0)
	, sega68Acc_(0)
	, m92NoteOffSeen_(0)
	, m92ChannelPlayOff_(0)
	, m92NoteStuck_(0)
	, scratch_(NULL)
	, scratchFrames_(0)
	, heard_(0)
{
}

int16_t* CDriverAc::Scratch(int frames)
{
	if (frames <= 0) return NULL;
	if (scratchFrames_ < frames) {
		int16_t* p = (int16_t*)realloc(scratch_, (size_t)frames * 2 * sizeof(int16_t));
		if (!p) return NULL;
		scratch_ = p;
		scratchFrames_ = frames;
	}
	return scratch_;
}

CDriverAc::~CDriverAc()
{
	Close();
}

int CDriverAc::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardAc*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 4000000;
	opmHz_ = hw_->opmHz_ > 0 ? hw_->opmHz_ : 4000000;
	opmResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	pinned_ = 0;
	cmdIndex_ = 0;
	nextGngIrq_ = 0;
	/* Catalog title pins the song (incl. code 0 = Stop). Without a titlelist,
	   fall back to board defaults and optional try-table hunting. */
	songCmdWord_ = (uint16_t)titleCode;
	if (ge && ge->titleCount > 0) {
		songCmd_ = (uint8_t)(titleCode & 0xff);
		songCmdWord_ = (uint16_t)titleCode;
		/* Prefer a playable BGM when the playlist hands STOP / voice / empty. */
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX) {
			int bad = (titleCode == 0 || titleCode == 0x200u || (titleCode & 0xffu) == 0);
			if (!bad) {
				for (int i = 0; i < ge->titleCount; i++) {
					if (ge->title[i].code != titleCode) continue;
					if (CEmuAcGxTitleIsVoice(&ge->title[i])) bad = 1;
					break;
				}
			}
			if (bad) {
				const unsigned prefer = CEmuAcPickGxDefaultTitle(ge);
				if (prefer) {
					songCmdWord_ = (uint16_t)prefer;
					songCmd_ = (uint8_t)(prefer & 0xff);
				}
			}
		}
		pinned_ = 1;
	} else if (titleCode && (titleCode & 0xff) != 0) {
		songCmd_ = (uint8_t)(titleCode & 0xff);
		pinned_ = 1;
	} else if (hw_->board_ == CEMU_AC_BOARD_GNG)
		songCmd_ = 0x2b; /* Flatland BGM */
	else if (hw_->board_ == CEMU_AC_BOARD_ABURNER)
		songCmd_ = 0x92; /* Maximum Power */
	else if (hw_->board_ == CEMU_AC_BOARD_OUTRUN)
		songCmd_ = 0x85; /* Magical Sound Shower */
	else if (hw_->board_ == CEMU_AC_BOARD_HANGON
		&& ge && _stricmp(ge->subtype, "sharrier") == 0)
		songCmd_ = 0xad; /* Theme ? BGM is 0xa3..0xb9; 0xe7 dies mid-probe */
	else if (hw_->board_ == CEMU_AC_BOARD_HANGON)
		songCmd_ = 0x9d; /* Hang-On Main Theme */
	else if (hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		songCmd_ = 0x01;
	else if (hw_->board_ == CEMU_AC_BOARD_CPS1)
		songCmd_ = 0x40;
	else if (hw_->board_ == CEMU_AC_BOARD_FLSTORY)
		/* 0x05 arms C500 (0x90) ? MSM melody path @08E7. 0x02's arm 0x86 is inert. */
		songCmd_ = 0x05;
	else if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_SJ
		|| hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1
		|| hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
		|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
		|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
		|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
		|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS)
		songCmd_ = 0x01;
	else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT)
		/* Scramble 0x01-0x05 are short setup stubs that clear the channel;
		   0x0B is a sustained theme with note/volume motion (varying peaks).
		   GX400 uses 0x40+ catalog codes ? do not default to 0x0B. */
		songCmd_ = 0x0b;
	else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400)
		songCmd_ = 0x01; /* ISR only arms on latch==1 (channel init) */
	else if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352)
		songCmd_ = 0x20; /* Sys12: 0x20 is sustained BGM on most C76 sets */
	else if (hw_->board_ == CEMU_AC_BOARD_IREM_M72)
		songCmd_ = 0x30;
	else
		songCmd_ = 0x81;
	/* Catalog may pin scramble stub codes 0x01-0x05 (channel clears immediately).
	   Coerce onto a sustained BGM id so probes see PLAY with moving peaks.
	   GX400 uses a different command space (0x40+); leave it alone here. */
	if ((hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT)
		&& songCmd_ >= 0x01 && songCmd_ <= 0x05) {
		songCmd_ = 0x0b;
		pinned_ = 0; /* allow try-table if 0x0B is silent on a clone */
	}
	/* GX400: sound ROM ISR accepts only latch 0x01; catalog BGM ids stay in
	   songCmdWord_ for diagnostics but the latch must be the init strobe. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (songCmd_ != 0x01)
			songCmd_ = 0x01;
	}
	/* flstory: 0x02 arms with 0x86 (01F0 ignore); prefer MSM slots (0x90��C500).
	   Keep 0x15 ? it is audible (was WEAK); 0x05 is silent on this set. */
	if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		static const uint8_t kOk[] = {
			0x05, 0x03, 0x06, 0x08, 0x0e, 0x0f, 0x01, 0x04, 0x15, 0x16, 0x18,
			0x4c, 0x10, 0x21, 0x08, 0x26, 0x27, 0x28, 0x2d, 0x2e, 0x2f
		};
		int ok = 0;
		for (int i = 0; i < (int)_countof(kOk); i++) {
			if (songCmd_ == kOk[i]) { ok = 1; break; }
		}
		if (!ok || songCmd_ == 0x02) {
			songCmd_ = 0x15;
			pinned_ = 0;
		}
		/* onna34ro: 0x4C/0x15 are quiet; 0x10 sustains MSM+AY. */
		if (ge && ge->archive && _stricmp(ge->archive, "onna34ro") == 0)
			songCmd_ = 0x10;
		/* 40love/fieldday/victnine: catalog prefers are valid IDs but the
		   MSM arm path often stays mute until try-table hunting. */
		if (ge && ge->archive
			&& (_stricmp(ge->archive, "40love") == 0
				|| _stricmp(ge->archive, "fieldday") == 0
				|| _stricmp(ge->archive, "victnine") == 0))
			pinned_ = 0;
	}
	/* CPS1 catalogs often list 0xF0/0xFF (stop/fade) first (ffight/forgottn/
	   sf2ce). Provisional 0x40 ? 0x01 BLASTS on version-2 (ffight) and would
	   set heard_ so hunting never recovers. Refined after LoadRoms. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS1
		&& (songCmd_ == 0x00 || songCmd_ >= 0xf0)) {
		songCmd_ = 0x40;
		pinned_ = 0;
	}
	/* Catalog may list STOP first ? prefer known BGM range for AB only when
	   the pin is not a real catalog title (voice/SFX are often 0xA3+ / decimal
	   163+ and must not be remapped onto 0x92 Maximum Power). */
	if (hw_->board_ == CEMU_AC_BOARD_ABURNER && ge
		&& _stricmp(ge->subtype, "aburner") == 0 && !pinned_) {
		const uint8_t c = songCmd_;
		if (c < 0x90 || c > 0x97)
			songCmd_ = 0x92;
	}
	/* K054539 (bucky/moomesa/�c): song table entries are 14 bytes; a slot with
	   [0]==0 and bit6 of [1] clear is a mute/stop row (bucky prefer 0xC0).
	   Only the single-chip sets lay their table out that way. On the dual
	   K054539 mystwarr family the whole BGM range is 0xCD-0xEC, so this
	   coercion silently replaced every song with code 0x01. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4
		&& !hw_->KonamiPcm2() && songCmd_ >= 0xc0)
		songCmd_ = 0x01;
	/* OutRun/Turbo OutRun: coerce stop/credit/SE prefers to Magical Sound
	   Shower (0x85). 0x84 is Credit ? a one-shot that classified as BLAST. */
	if (hw_->board_ == CEMU_AC_BOARD_OUTRUN) {
		const uint8_t c = songCmd_;
		if (c == 0x94 || c == 0x9e || c == 0x9f || c == 0xff || c == 0x7f
			|| c < 0x81 || c == 0x84)
			songCmd_ = 0x85;
	}
	/* Taito B YM2203 (masterw/viofight): catalog/host often pins SE/handshake
	   (0x01-0x04). Sustained BGM is archive-specific. */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && hw_->MainIsYm2203() && ge) {
		const uint8_t c = songCmd_;
		if (c < 0x05 || c == 0x07) {
			if (_stricmp(ge->subtype, "viofight") == 0
				|| (ge->archive[0] && _strnicmp(ge->archive, "viofight", 8) == 0))
				songCmd_ = 0x08;
			else
				songCmd_ = 0x05; /* masterw / champwr / tetrista family */
			/* keep pinned_ ? clearing it made viofight WEAK vs direct 0x08 PLAY */
		}
	}
	/* GNG-class: archive-specific BGM. Catalog prefers are often SE/AUDITION
	   (avengers 0x23, gunsmoke 0x1F��old 0x2b, commando 0x1A). */
	if (hw_->board_ == CEMU_AC_BOARD_GNG && ge) {
		if (hw_->GngGaidenMap()) {
			/* Tecmo gaiden: catalog leads with Credit 0x01 / 0x10 / 0x42.
			   Prefer first stage BGM from the title list. */
			const uint8_t c = songCmd_;
			if (c == 0x00 || c == 0x01 || c == 0x10 || c == 0x42) {
				uint8_t best = 0x02;
				for (int i = 0; i < ge->titleCount; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t == 0x00 || t == 0x01 || t == 0x10 || t == 0x42)
						continue;
					if (t >= 0x0a && t <= 0x1f) continue; /* SE / voice band */
					best = t;
					songCmdWord_ = (uint16_t)ge->title[i].code;
					break;
				}
				songCmd_ = best;
			}
		} else {
			const char* ar = ge->archive;
			if (ar && _strnicmp(ar, "gunsmoke", 8) == 0)
				songCmd_ = 0x28;
			else if (ar && _strnicmp(ar, "avenger", 7) == 0)
				songCmd_ = 0x21;
			else if (ar && _strnicmp(ar, "commando", 8) == 0)
				songCmd_ = 0x21;
			else {
				const uint8_t c = songCmd_;
				/* 0x23/0x2b flat-clip on several boards; keep gng 0x35. */
				if (c < 0x20 || c == 0x23 || c == 0x2b || (c > 0x3a && c != 0x35)) {
					songCmd_ = 0x21;
					pinned_ = 0;
				}
			}
		}
	}
	/* Sega System1: catalog often pins short SE / blast one-shots that die
	   mid-probe (4dwarrio 0x90, tokisens 0x10, �c). Prefer sustained 0x81/0x82.
	   Do NOT touch known PLAY prefers (choplift 0xAB, imsorry 0xB8, �c). */
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1 && ge) {
		const uint8_t c = songCmd_;
		/* 0x90 sustains on spatter ? do not treat it as bad. */
		const int bad = (c == 0x91 || c == 0x95 || c == 0x8f
			|| c == 0xb3 || c == 0x97 || c == 0x87 || c == 0x10);
		if (bad) {
			uint8_t best = 0;
			uint16_t bestW = 0;
			int bestSc = 999;
			for (int i = 0; i < ge->titleCount; i++) {
				const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
				if (t == c || t == 0x00 || t == 0x10 || t == 0x87 || t == 0x97
					|| t == 0xb3 || t == 0x8f || t == 0x91 || t == 0x95)
					continue;
				int sc = 50;
				if (t == 0x81 || t == 0x82 || t == 0x90) sc = 5;
				else if (t >= 0x80 && t <= 0x8e) sc = 10;
				else if (t >= 0x01 && t <= 0x0f) sc = 20;
				if (sc < bestSc) {
					bestSc = sc;
					best = t;
					bestW = (uint16_t)ge->title[i].code;
				}
			}
			if (!best) {
				best = (ge->archive[0] && _stricmp(ge->archive, "tokisens") == 0)
					? (uint8_t)0x01 : (uint8_t)0x81;
			}
			songCmd_ = best;
			if (bestW) songCmdWord_ = bestW;
		}
		/* 4dwarrio/suprloco: catalog 0x90 is a short SE ? prefer 0x81/0x82. */
		else if (c == 0x90 && ge->archive[0]
			&& (_stricmp(ge->archive, "4dwarrio") == 0
				|| _stricmp(ge->archive, "suprloco") == 0)) {
			songCmd_ = 0x81;
		}
	}
	/* Terracre / galivan family: 0x01 is silent handshake; 0x15/0x25 sustain. */
	if (hw_->board_ == CEMU_AC_BOARD_TERRACRE) {
		const uint8_t c = songCmd_;
		if (c == 0x00 || c == 0x01 || c == 0x0e || c == 0x2e || c == 0x65) {
			songCmd_ = 0x25;
			pinned_ = 0;
		}
	}
	/* Toaplan1: catalog 0x25 on twincobr is a short SE; 0x08/0x12 sustain. */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1) {
		const uint8_t c = songCmd_;
		if (c == 0x25 || c == 0x01 || c == 0x20 || c == 0x10) {
			songCmd_ = 0x12;
			pinned_ = 0;
		}
	}
	/* m99 M72 (YM@40): BGM is 0x20+index; catalog often lists raw index/SE. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72 && hw_->M72IoAlt()
		&& songCmd_ > 0 && songCmd_ < 0x20)
		songCmd_ = (uint8_t)(0x20 + (songCmd_ & 0x1fu));
	/* Sys16B: catalog often leads with Stop/Credit/SFX ? prefer first BGM-ish
	   title (goldnaxe 0x9C credit, cotton 0x01 stop). Not a song-hunt: one
	   fixed catalog pass at Open. Mute/weak pins (0xC5/0xD7/0xFF) remap to
	   stage BGM when present (0xC0-C2, 0xD0, low bytes).
	   0x90-0x95 is BGM on 5358 sports (aceattac/suprleag); 0xA0-0xAF on
	   sonicbom-class; 0x96-0x9F title demos (hwchamp). Do NOT blanket-reject
	   0x96-0xBF ? that remapped suprleag/sonicbom onto voice SE bytes. */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16B && ge && ge->titleCount > 0) {
		const uint8_t c = songCmd_;
		int hasStage = 0, has90 = 0, hasA = 0;
		for (int i = 0; i < ge->titleCount; i++) {
			const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
			if (t >= 0xc0 && t < 0xf0) hasStage = 1;
			if (t >= 0x90 && t <= 0x95) has90 = 1;
			if (t >= 0xa0 && t <= 0xaf) hasA = 1;
		}
		const int bad = (c == 0x00 || c == 0x01 || c == 0xff || c == 0x9c
			|| c == 0xc5 || c == 0xd7
			|| (c >= 0x02 && c <= 0x26) /* speech/SE nibbles */
			|| (c >= 0x27 && c < 0x40)
			|| (c == 0x91 && (has90 || hasA)) /* Credit when real BGM exists */
			|| ((c == 0x9b || c == 0xad) && has90)
			|| (hasStage && c >= 0x90 && c < 0xc0));
		if (bad) {
			uint8_t best = 0;
			uint16_t bestW = 0;
			int bestSc = 999;
			for (int i = 0; i < ge->titleCount; i++) {
				const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
				if (t == 0x00 || t == 0x01 || t == 0xff || t == 0x9c || t == 0xc5
					|| t == 0xd7 || t == 0x91)
					continue;
				if (t >= 0x02 && t <= 0x26) continue;
				if (t >= 0x27 && t < 0x40) continue;
				if (hasStage && t >= 0x90 && t < 0xc0) continue;
				if ((t == 0x9b || t == 0xad) && has90) continue;
				int sc = 50;
				if (t == 0xc0 || t == 0xc1 || t == 0xc2 || t == 0xd0) sc = 5;
				else if (t >= 0xc0 && t < 0xf0) sc = 10;
				else if (t >= 0x90 && t <= 0x95) sc = 12;
				else if (t >= 0xa0 && t <= 0xaf) sc = 14;
				else if (t >= 0x96 && t <= 0x9f) sc = 16;
				else if (t >= 0x80 && t < 0x90) sc = 25;
				if (sc < bestSc) {
					bestSc = sc;
					best = t;
					bestW = (uint16_t)ge->title[i].code;
				}
			}
			if (best) {
				songCmd_ = best;
				songCmdWord_ = bestW;
			}
		}
	}
	/* Sys16A: catalog leads with Stop/Credit then Mission BGM @90-9F (shinobi)
	   or vehicle BGM @A8-B1 (afighter). Detect afighter by archive ? shinobi
	   also lists SFX 0xB2 which must not flip the prefer bands. */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16A && ge && ge->titleCount > 0) {
		const uint8_t c = songCmd_;
		const int afighter = (ge->archive[0]
			&& _stricmp(ge->archive, "afighter") == 0) ? 1 : 0;
		const int bad = (c == 0x00 || c == 0x01 || c == 0xff
			|| c == 0x88 || c == 0xb2 || c == 0xb3
			|| (c >= 0x40 && c < 0x50)
			|| (afighter && c >= 0x88 && c <= 0xa5)
			|| (!afighter && c >= 0xa0 && c < 0xb0));
		if (bad || c == 0) {
			uint8_t best = 0;
			uint16_t bestW = 0;
			int bestSc = 999;
			for (int i = 0; i < ge->titleCount; i++) {
				const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
				if (t == 0x00 || t == 0x01 || t == 0xff || t == 0x88 || t == 0xb2
					|| t == 0xb3)
					continue;
				if (t >= 0x40 && t < 0x50) continue;
				if (afighter && t >= 0x88 && t <= 0xa5) continue;
				if (!afighter && t >= 0xa0 && t < 0xb0) continue;
				int sc = 50;
				if (afighter && t >= 0xa8 && t <= 0xb1) sc = 5;
				else if (!afighter && t >= 0x90 && t <= 0x9f) sc = 5;
				else if (t == 0x87 || t == 0x9e || t == 0x9f) sc = 10;
				else if (t >= 0x80 && t < 0x90) sc = 25;
				else if (t >= 0x90 && t <= 0x9f) sc = 30;
				if (sc < bestSc) {
					bestSc = sc;
					best = t;
					bestW = (uint16_t)ge->title[i].code;
				}
			}
			if (best) {
				songCmd_ = best;
				songCmdWord_ = bestW;
			}
		}
	}
	/* Sys16B: 0xFF/0x00 are stop ? do not pin them. */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16B
		&& (songCmd_ == 0x00 || songCmd_ == 0xff)) {
		songCmd_ = 0x81;
		pinned_ = 0;
	}
	if (hw_->board_ == CEMU_AC_BOARD_SYS16A
		&& (songCmd_ == 0x00 || songCmd_ == 0xff)) {
		songCmd_ = 0x9a;
		pinned_ = 0;
	}
	/* Catalog may list STOP first (arabfgt 0xFF). Prefer a stage BGM. */
	if (hw_->board_ == CEMU_AC_BOARD_SYS32
		&& (songCmd_ == 0x00 || songCmd_ == 0xff) && ge && ge->titleCount > 0) {
		for (int i = 0; i < ge->titleCount; i++) {
			const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
			if (t == 0x00 || t == 0xff) continue;
			songCmd_ = t;
			songCmdWord_ = (uint16_t)ge->title[i].code;
			break;
		}
	}
	/* Sys2: first title is often an unused 16-bit demo (assault 0x213). */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2 && ge && ge->titleCount > 0) {
		int unused = 0;
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code != (unsigned)songCmdWord_
				&& ge->title[i].code != (unsigned)songCmd_)
				continue;
			if (ge->title[i].code == 0x213u
				|| wcsstr(ge->title[i].label, L"Unused")
				|| wcsstr(ge->title[i].label, L"unused"))
				unused = 1;
			break;
		}
		if (unused || songCmdWord_ == 0x213) {
			for (int i = 0; i < ge->titleCount; i++) {
				const unsigned c = ge->title[i].code;
				if (!c || c == 0x213u) continue;
				if (wcsstr(ge->title[i].label, L"Unused")
					|| wcsstr(ge->title[i].label, L"unused"))
					continue;
				songCmdWord_ = (uint16_t)c;
				songCmd_ = (uint8_t)(c & 0xffu);
				break;
			}
		}
	}
	if (hw_->board_ == CEMU_AC_BOARD_HANGON && ge
		&& _stricmp(ge->subtype, "sharrier") == 0 && !pinned_) {
		/* Unpinned / hunt only ? never rewrite catalog SFX outside BGM band. */
		if (songCmd_ < 0xa3 || songCmd_ > 0xb9)
			songCmd_ = 0xad;
	}
	if (!hw_->LoadRoms(fs, ge, titleCode)) {
		/* Soft-open boards that are expected to classify SILENT when ROMs /
		   host CPU are incomplete ? never FAIL_OPEN the catalog probe. */
		const int soft =
			(hw_->board_ == CEMU_AC_BOARD_IREM_M62
				|| hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS86
				|| hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP
				|| hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400
				|| hw_->board_ == CEMU_AC_BOARD_SYS24
				|| hw_->board_ == CEMU_AC_BOARD_SYS32
				|| hw_->board_ == CEMU_AC_BOARD_UNKNOWN
				|| hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) ? 1 : 0;
		if (!soft)
			return 0;
		booted_ = 1;
		hasCpu_ = 0;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* After ROM load, m72IoAlt_ may be sniffed ? re-apply m99 BGM bias. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72 && hw_->M72IoAlt()
		&& songCmd_ > 0 && songCmd_ < 0x20)
		songCmd_ = (uint8_t)(0x20 + (songCmd_ & 0x1fu));

	/* Capcom ZN: catalog often leads with QSound logo / mono-stereo switches
	   (sfex 0x10, techromn 0xFF04). One fixed titlelist pass ? no try-table. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS && hw_->QsZn() && ge
		&& ge->titleCount > 0) {
		const unsigned cur = songCmdWord_ ? (unsigned)songCmdWord_
			: (unsigned)songCmd_;
		const unsigned hi = (cur >> 8) & 0xffu;
		const unsigned lo = cur & 0xffu;
		const int bad = (!cur || hi == 0xffu || lo == 0
			|| (hi == 0 && (lo == 0x10u || lo >= 0x40u)));
		if (bad) {
			unsigned best = 0;
			for (int i = 0; i < ge->titleCount; i++) {
				const unsigned c = ge->title[i].code;
				const unsigned h = (c >> 8) & 0xffu;
				const unsigned l = c & 0xffu;
				if (!c || h == 0xffu || l == 0) continue;
				if (h == 0 && (l == 0x10u || l >= 0x40u)) continue;
				/* Prefer stage BGM words (techromn 0x80xx / plain 0x01+). */
				if (h == 0x80u || h == 0xb0u || (h == 0 && l >= 0x01u && l <= 0x0fu)
					|| (h == 0x04u) || (c >= 0x0100u && c < 0x8000u)) {
					best = c;
					break;
				}
				if (!best) best = c;
			}
			if (best) {
				songCmdWord_ = (uint16_t)best;
				songCmd_ = (uint8_t)(best & 0xffu);
			}
		}
		pinned_ = 1;
	}

	/* M92 channel-BGM: hoot titlelists often put stub/NULL song indices
	   (2/3) first. Those enqueue and alloc then die silent. Coerce onto a
	   known looping BGM for each set ? not a runtime song hunt. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M92 && hw_->M92ChannelBgm() && ge) {
		pinned_ = 1; /* never re-latch; retries abort an already-started BGM */
		const char* a = ge->archive;
		const uint8_t c = songCmd_;
		/* Prefer remappable indices (<0x20) when the table slot loops; else
		   raw latch (>=0x20) that the sequencer already accepts. */
		uint8_t prefer = 0;
		if (a) {
			if (!_stricmp(a, "firebarr") && c < 0x0eu) prefer = 0x0e;
			else if (!_stricmp(a, "gunforc2") && c < 0x10u) prefer = 0x10;
			else if ((!_stricmp(a, "dsoccr94j") || !_stricmp(a, "dsoccr94"))
				&& c < 0x0cu) prefer = 0x0c;
			else if (!_stricmp(a, "wpksoc") && c < 0x15u) prefer = 0x15;
			else if (!_stricmp(a, "nbbatman") && c < 0x20u) prefer = 0x70;
			else if (!_stricmp(a, "inthunt") && c < 0x20u) prefer = 0x40;
			else if (!_stricmp(a, "mysticri") && c < 0x20u) prefer = 0x70;
			else if (!_stricmp(a, "majtitl2") && c < 0x20u) prefer = 0x60;
			else if (!_stricmp(a, "ssoldier") && c < 0x0eu) prefer = 0x0e;
			else if (!_stricmp(a, "hook") && c != 0x4bu) prefer = 0x4b;
			else if (!_stricmp(a, "rtypeleo") && c != 0x40u) prefer = 0x40;
		}
		if (prefer) {
			songCmd_ = prefer;
			songCmdWord_ = prefer;
		}
	}

	/* Early CPS1 (ghouls/dynwar idle EI;JR-3 @0009): catalog often pins a short
	   jingle (ghouls 0x7 �� WEAK). Keep the preferred code as try#0 but allow the
	   try table to hunt sustained BGM.
	   Stop/fade prefers (0xF0): early + version 4+ want 0x01 (forgottn/sf2ce);
	   version 2 (ffight/1941) MUST stay on 0x40 ? 0x01/SE BLAST [32768,0,0,0]. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS1) {
		const int earlyCps = hw_->PeekMem(0x0009) == 0xfb
			&& hw_->PeekMem(0x000a) == 0x18
			&& hw_->PeekMem(0x000b) == 0xfd;
		const int ver2 = hw_->PeekMem(0x000f) == (uint8_t)'2';
		if (pinned_ && earlyCps)
			pinned_ = 0;
		if ((titleCode & 0xff) >= 0xf0u || (titleCode & 0xff) == 0
			|| songCmd_ == 0x00 || songCmd_ >= 0xf0) {
			songCmd_ = ver2 ? 0x40 : 0x01;
			pinned_ = 0;
		} else if (ver2 && (songCmd_ < 0x40 || songCmd_ >= 0x80)) {
			/* 1941 prefers 0x96 SE; low codes also BLAST ? pin BGM band. */
			songCmd_ = 0x40;
			pinned_ = 0;
		}
	}

	/* RF5C400 / Model2A�E3 SCSP: no 68K host yet ? stay SILENT (no audition).
	   MultiPCM model2 keeps the 68000 path below. M62/Seibu run real sequencers. */
	hasCpu_ = !(hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400
		|| (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP && !hw_->SegaM1Audio()));

	/* Hornet RF5C400 / Model2A�E3 SCSP: no 68K host ? open silent (no soft wave). */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_RF5C400
		|| (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP && !hw_->SegaM1Audio())) {
		hasCpu_ = 0;
		cmdIndex_ = 0;
		booted_ = 1;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Non-HuC Data East (btime/disco): board UNKNOWN ? soft-open SILENT. */
	if (hw_->board_ == CEMU_AC_BOARD_UNKNOWN) {
		booted_ = 1;
		hasCpu_ = 0;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	m62_ = (hw_->board_ == CEMU_AC_BOARD_IREM_M62) ? 1 : 0;
	if (m62_) {
		if (!hw_->M62Active()) return 0;
		m62Acc_ = 0;
		cmdIndex_ = 0;
		if (!songCmd_) songCmd_ = 0x20;
		uint8_t song = (uint8_t)(songCmd_ & 0x7fu);
		if (!song) song = 0x20;
		hw_->SetSoundCommand(0x80);
		M62RunCycles(cpuHz_);
		/* IRQ ISR stores the masked latch to a direct page mailbox that
		   varies by title ($BC ldrun/kungfum, $C6 ldrun3, $C7 kidniki).
		   Seed the common slots after boot, then pulse the real latch. */
		struct m6800* cpu = hw_->M6803Cpu();
		if (cpu) {
			const uint8_t id = (uint8_t)(song & 0x7fu);
			/* Mailboxes used by M62 IRQ ISRs across the set. */
			const uint8_t slots[] = { 0xbc, 0xc6, 0xc7, 0xcc, 0 };
			for (int i = 0; slots[i]; i++) {
				const uint8_t a = slots[i];
				if (cpu->iram_base <= a && a <= 0xff)
					cpu->iram[a - cpu->iram_base] = id;
			}
		}
		hw_->SetSoundCommand(song);
		M62RunCycles(cpuHz_ / 2);
		hw_->SetSoundCommand(0x80);
		M62RunCycles(cpuHz_ / 4);
		/* Second pulse ? covers mailboxes wiped by a late STAA #$FF. */
		if (cpu) {
			const uint8_t id = (uint8_t)(song & 0x7fu);
			const uint8_t slots[] = { 0xbc, 0xc6, 0xc7, 0xcc, 0 };
			for (int i = 0; slots[i]; i++) {
				const uint8_t a = slots[i];
				if (cpu->iram_base <= a && a <= 0xff)
					cpu->iram[a - cpu->iram_base] = id;
			}
		}
		hw_->SetSoundCommand(song);
		M62RunCycles(cpuHz_ / 2);
		hw_->SetSoundCommand(0x80);
		M62RunCycles(cpuHz_ / 4);
		booted_ = 1;
		triggered_ = 1;
		cmdIndex_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	sega68_ = (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP && hw_->SegaM1Audio()) ? 1 : 0;
	if (sega68_) {
		if (!hw_->Ms1Active()) return 0;
		sega68Acc_ = 0;
		cmdIndex_ = 0;
		if (!songCmdWord_ && songCmd_)
			songCmdWord_ = songCmd_;
		if (!songCmdWord_)
			songCmdWord_ = 0x1001;
		/* Longer settle ? MultiPCM firmware clears RAM then waits on UART. */
		Sega68RunCycles(cpuHz_);
		Sega68RunCycles(cpuHz_ / 2);
		TryInjectCommand();
		Sega68RunCycles(cpuHz_ / 2);
		TryInjectCommand();
		Sega68RunCycles(cpuHz_ / 4);
		booted_ = 1;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
		return 1;
	}

	/* Mega System 1 / System GX run a second 68000 on the shared Musashi core
	   instead of the Z80; they share the Ms1 boot/render path below. */
	ms1_ = (hw_->board_ == CEMU_AC_BOARD_MEGASYSTEM1
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX
		|| hw_->board_ == CEMU_AC_BOARD_M68K_PCM) ? 1 : 0;

	/* Data East HuC6280 / M6502, and Atari System1 JSA (same M6502 runner). */
	deco_ = (hw_->board_ == CEMU_AC_BOARD_DECO
		|| hw_->board_ == CEMU_AC_BOARD_ATARI_SYS1) ? 1 : 0;
	if (deco_) {
		if (!hw_->DecoActive()) return 0;
		decoAcc_ = 0;
		decoChipRes_ = 0;
		decoNextYmIrq_ = 0;
		cmdIndex_ = 0;
		/* Prefer BGM codes. 0x80+ are SE/volume ? strip to low 7 bits.
		   H6280 (kind 0): catalogs may list high fanfare codes ? rewrite
		   those down toward playable stage BGM when present.
		   dec0/drgninja (kind 2): catalogs lead with Credit (0x05); keep
		   host BGM >=0x1C, but lift Credit/low SE up to the first stage BGM.
		   Atari JSA (kind 5): skip voice/chip-test catalog heads. */
		if (songCmd_ >= 0x80)
			songCmd_ = (uint8_t)(songCmd_ & 0x7fu);
		if (ge && ge->titleCount > 0) {
			const int kind = hw_->DecoCpuKind();
			if (songCmd_ >= 0x1cu && kind == 0) {
				for (int i = 0; i < ge->titleCount; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t >= 0x04 && t < 0x1cu) {
						songCmd_ = t;
						songCmdWord_ = (uint16_t)ge->title[i].code;
						break;
					}
				}
			} else if (kind == 2 && songCmd_ < 0x1cu) {
				for (int i = 0; i < ge->titleCount; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t >= 0x1cu && t < 0x80u) {
						songCmd_ = t;
						songCmdWord_ = (uint16_t)ge->title[i].code;
						break;
					}
				}
			} else if (kind == 5 && (songCmd_ >= 0x60u || songCmd_ <= 0x05u)) {
				for (int i = 0; i < ge->titleCount; i++) {
					const uint8_t t = (uint8_t)(ge->title[i].code & 0xff);
					if (t >= 0x08u && t < 0x40u && t != 0x04u && t != 0x05u) {
						songCmd_ = t;
						songCmdWord_ = (uint16_t)ge->title[i].code;
						break;
					}
				}
			}
		}
		if (!songCmd_) songCmd_ = 0x08;
		DecoRunCycles(cpuHz_);
		DecoRunCycles(cpuHz_ / 2);
		TryInjectCommand();
		/* Extra settle so IRQ1 queues and IRQ2 drains the song. */
		DecoRunCycles(cpuHz_);
		DecoRunCycles(cpuHz_ / 2);
		booted_ = 1;
		triggered_ = 1;
		/* Do not schedule further injects ? repeats clear $2310. */
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Namco System 1/2: M6809 + YM2151 (+ CUS30 / C140). */
	namcoM6809_ = (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS1
		|| hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2
		|| (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()
			&& !hw_->Wsg63701())) ? 1 : 0;
	if (namcoM6809_) {
		if (!hw_->NamcoM6809Active()) return 0;
		namcoAcc_ = 0;
		cmdIndex_ = 0;
		if (!songCmd_)
			songCmd_ = 0x01;
		/* Byte stop codes 0xF0..0xFF �� 0x01. Do NOT treat Sys2 words
		   (0x0203/0x0213/�c) as stops ? songCmdWord_>=0xF0 wiped BGM. */
		if (songCmd_ >= 0xf0u
			|| (songCmdWord_ >= 0xf0u && songCmdWord_ <= 0x00ffu)) {
			songCmd_ = 0x01;
			songCmdWord_ = 0x01;
			pinned_ = 0;
		}
		/* WSG6809 catalogs often lead with Credit (gaplus 0x16, motos 0x19).
		   Prefer the first non-credit title with a sustained name, else 0x01. */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy() && ge
			&& ge->titleCount > 0 && ge->archive) {
			const char* ar = ge->archive;
			if (_stricmp(ar, "gaplus") == 0 && songCmd_ == 0x16u)
				songCmd_ = 0x05; /* Round Start Music */
			else if (_stricmp(ar, "motos") == 0 && songCmd_ == 0x19u)
				songCmd_ = 0x18; /* Attract Mode BGM */
			else if (_stricmp(ar, "superpac") == 0 && songCmd_ == 0x01u)
				songCmd_ = 0x02;
			else if (_stricmp(ar, "liblrabl") == 0 && songCmd_ == 0x01u)
				songCmd_ = 0x02;
		}
		/* Sys2 catalog often leads with unused Opening / SFX Start / Await.
		   Prefer sustain BGM: finallap 0x208 builds; 0x205-207 decay/silent.
		   Assault: 0x17 / 0x04. */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2 && ge && ge->titleCount > 0) {
			const unsigned cur = songCmdWord_ ? songCmdWord_ : (unsigned)songCmd_;
			const unsigned hi = (cur >> 8) & 0xffu;
			const unsigned lo = cur & 0xffu;
			const int curRec = hw_->Sys2SongRecType(lo);
			/* Prefer type-$20 BGM records. Type $64 fails the bit6 gate;
			   type $21 (mirninja 0x08) accepts but only spam-keys YM. */
			const int weak = (!cur || cur == 0x200u || cur == 0x220u || cur == 0x213u
				|| lo <= 1u || lo >= 0x40u
				|| curRec == 0x64 || curRec == 0x21 || curRec == 0x00
				|| (hi == 0x02u && lo >= 1u && lo <= 4u)
				|| (hi == 0x02u && lo >= 5u && lo <= 7u)
				/* Assault 0x11-0x14 are short SFX (diag: keyOn=0); prefer 0x17. */
				|| (hi == 0u && lo >= 0x11u && lo <= 0x14u));
			if (weak) {
				unsigned best = 0;
				int bestScore = 999;
				for (int i = 0; i < ge->titleCount; i++) {
					const unsigned c = ge->title[i].code;
					const unsigned h = (c >> 8) & 0xffu;
					const unsigned l = c & 0xffu;
					int sc = 50;
					if (!c || c == 0x200u || c == 0x220u || l <= 1u || l >= 0x40u) continue;
					if (h == 0x02u && l >= 1u && l <= 4u) continue;
					if (c == 0x213u) continue;
					if (h == 0x02u && l >= 5u && l <= 7u) continue;
					if (h == 0u && l >= 0x11u && l <= 0x14u) continue;
					const int rec = hw_->Sys2SongRecType(l);
					if (rec == 0x64 || rec == 0x21 || rec == 0x00) continue;
					if (rec > 0 && rec != 0x20) sc += 20;
					if (c == 0x208u) sc = 5;
					else if (l == 0x17u || l == 0x04u || l == 0x0au || l == 0x05u) sc = 15;
					else if (l == 0x08u && rec == 0x20) sc = 18;
					else if (h == 0u && ((l >= 0x12u && l <= 0x18u) || l == 2u || l == 3u || l == 0x26u))
						sc = 30;
					else if (h == 0u) sc = 40;
					if (rec == 0x20) sc = (sc > 5) ? sc - 5 : sc;
					if (sc < bestScore) {
						bestScore = sc;
						best = c;
					}
				}
				if (best) {
					songCmdWord_ = (uint16_t)best;
					songCmd_ = (uint8_t)(best & 0xffu);
					pinned_ = 1;
				}
			}
		}
		/* Finish RAM-test / CLI before injecting so IRQ does not clobber A.
		   Mappy-era sub CPUs wait on shared-RAM magic before CLI:
		     grobda/motos: $40="CK" then later "GO"
		     gaplus: $40==$11
		     pacnpal: $40==$01 (any other non-zero �� BRA *)
		     superpac: $FB!=0 after clearing $40
		   Re-seed from the *current* PC each slice so multi-phase gates and
		   soft-resets back to E000 still release ? then inject the song. */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()) {
			/* digdug2/todruaga begin with a 1KiB shared-RAM clear. Chip Reset
			   already zeroed the 15xx window; skipping the STD loop avoids a
			   long I=1 stretch where stack-clamp/IRQ edges could bounce PC. */
			{
				mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (cpu && cpu->pc.w == 0xe000u) {
					const uint8_t a0 = hw_->NamcoM6809Read8(0xe000);
					const uint8_t a1 = hw_->NamcoM6809Read8(0xe001);
					const uint8_t a3 = hw_->NamcoM6809Read8(0xe003);
					const uint8_t a6 = hw_->NamcoM6809Read8(0xe006);
					const uint8_t a9 = hw_->NamcoM6809Read8(0xe009);
					if (a0 == 0xb7 && a1 == 0x20 && a3 == 0x8e && a6 == 0xcc && a9 == 0xed) {
						cpu->pc.w = 0xe00fu; /* LDS #$0400 / checksum */
						cpu->S.w = 0x0400u;
					}
				}
			}
			for (int slice = 0; slice < 32; slice++) {
				mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (!cpu) break;
				const uint16_t pc = cpu->pc.w;
				const uint8_t o0 = hw_->NamcoM6809Read8(pc);
				const uint8_t o1 = hw_->NamcoM6809Read8((uint16_t)(pc + 1));
				const uint8_t o2 = hw_->NamcoM6809Read8((uint16_t)(pc + 2));
				const uint8_t o3 = hw_->NamcoM6809Read8((uint16_t)(pc + 3));
				const uint8_t o4 = hw_->NamcoM6809Read8((uint16_t)(pc + 4));
				const uint8_t o5 = hw_->NamcoM6809Read8((uint16_t)(pc + 5));
				const uint8_t o6 = hw_->NamcoM6809Read8((uint16_t)(pc + 6));
				if (o0 == 0xdc && o1 == 0x40 && o2 == 0x10 && o3 == 0x83) {
					/* grobda: LDD $40 / CMPD #imm ("CK" or "GO") */
					hw_->NamcoM6809Write8(0x0040, o4);
					hw_->NamcoM6809Write8(0x0041, o5);
				} else if (o0 == 0x9e && o1 == 0x40 && o2 == 0x8c) {
					/* motos: LDS $40 / CMPX #imm */
					hw_->NamcoM6809Write8(0x0040, o3);
					hw_->NamcoM6809Write8(0x0041, o4);
				} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0x81) {
					/* pacnpal: LDA $40 / CMPA #imm */
					hw_->NamcoM6809Write8(0x0040, o3);
				} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0xb7 && o5 == 0x81) {
					/* gaplus: LDA $40 / STA $3000 / CMPA #imm */
					hw_->NamcoM6809Write8(0x0040, o6);
				} else if (o0 == 0x96 && o1 == 0xfb && o2 == 0x27) {
					/* superpac: LDA $FB / BEQ wait */
					hw_->NamcoM6809Write8(0x00fb, 0x40);
				} else if (o0 == 0xec && o1 == 0x84 && o2 == 0x26 && o3 == 0xfc) {
					/* superpac: spin while word at X ($40) != 0 ? clear it */
					hw_->NamcoM6809Write8(0x0040, 0);
					hw_->NamcoM6809Write8(0x0041, 0);
				} else if (o0 == 0x91 && o1 == 0x41 && o2 == 0x27) {
					hw_->NamcoM6809Write8(0x0041, 0); /* phozon host gate */
				} else {
					/* Cold reset vector patterns (PC still near E000/F000). */
					uint8_t b[8];
					const uint16_t base = (pc >= 0xf000u) ? 0xf000u : 0xe000u;
					for (int i = 0; i < 8; i++)
						b[i] = hw_->NamcoM6809Read8((uint16_t)(base + i));
					if (b[0] == 0xdc && b[1] == 0x40 && b[2] == 0x10 && b[3] == 0x83) {
						hw_->NamcoM6809Write8(0x0040, b[4]);
						hw_->NamcoM6809Write8(0x0041, b[5]);
					} else if (b[0] == 0x9e && b[1] == 0x40 && b[2] == 0x8c) {
						hw_->NamcoM6809Write8(0x0040, b[3]);
						hw_->NamcoM6809Write8(0x0041, b[4]);
					} else if (b[0] == 0x1a && b[1] == 0xff && b[2] == 0x96 && b[3] == 0x40
						&& b[4] == 0x81) {
						hw_->NamcoM6809Write8(0x0040, b[5]); /* pacnpal */
					} else {
						for (int i = 0; i < 16; i++) {
							const uint8_t c0 = hw_->NamcoM6809Read8((uint16_t)(base + i));
							const uint8_t c1 = hw_->NamcoM6809Read8((uint16_t)(base + i + 1));
							const uint8_t c2 = hw_->NamcoM6809Read8((uint16_t)(base + i + 2));
							if (c0 == 0x81 && c1 == 0x11 && c2 == 0x26) {
								hw_->NamcoM6809Write8(0x0040, 0x11);
								break;
							}
						}
					}
				}
				NamcoM6809RunCycles(cpuHz_ / 32);
				cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				if (!cpu) break;
				/* Skip fixed ROM checksum loops ? ADDA ,X+ across $E000-$FFFF
				   can wedge under our step budget; the sums are constant. */
				{
					const uint16_t pc = cpu->pc.w;
					if (pc >= 0xe010u && pc <= 0xe018u
						&& hw_->NamcoM6809Read8(0xe008) == 0x81
						&& hw_->NamcoM6809Read8(0xe009) == 0x11
						&& hw_->NamcoM6809Read8(0xe01a) == 0x81) {
						/* gaplus: sum == 0 */
						cpu->A = 0;
						cpu->X.w = 0;
						cpu->pc.w = 0xe01au;
					} else if (pc >= 0xe014u && pc <= 0xe019u
						&& hw_->NamcoM6809Read8(0xe01b) == 0x81
						&& hw_->NamcoM6809Read8(0xe01c) == 0xddu) {
						/* liblrabl: sum == $DD */
						cpu->A = 0xddu;
						cpu->X.w = 0;
						cpu->pc.w = 0xe01bu;
					} else if ((hw_->NamcoM6809Read8(pc) == 0xb7
						&& hw_->NamcoM6809Read8((uint16_t)(pc + 1)) == 0x20
						&& hw_->NamcoM6809Read8((uint16_t)(pc + 3)) == 0x8e
						&& hw_->NamcoM6809Read8((uint16_t)(pc + 9)) == 0xed)
						|| (hw_->NamcoM6809Read8(pc) == 0x8e
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 1)) == 0x00
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 2)) == 0x00
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 3)) == 0xcc
							&& hw_->NamcoM6809Read8((uint16_t)(pc + 6)) == 0xed)) {
						/* motos/grobda post-GO RAM clear (same as digdug2). */
						const uint16_t skip = (hw_->NamcoM6809Read8(pc) == 0xb7)
							? (uint16_t)(pc + 0x0fu) : (uint16_t)(pc + 0x0cu);
						cpu->pc.w = skip;
						cpu->S.w = 0x0400u;
					}
				}
				/* CLI done �� past host handshake; safe to post BGM. */
				if (!cpu->cc.i) break;
			}
			NamcoM6809RunCycles(cpuHz_ / 4);
		}
		NamcoM6809RunCycles(cpuHz_);
		/* Blazer/rompers: command poll gated on $8119==$0E; pacmania uses $901C. */
		hw_->NamcoM6809Write8(0x8119, 0x0e);
		hw_->NamcoM6809Write8(0x811c, 0x0e);
		hw_->NamcoM6809Write8(0x901c, 0x0e);
		/* Never post BGM over an unfinished host handshake ? gaplus waits
		   forever if $40 becomes the song id before $11 is seen.
		   Sys2 (assault): ROM only ANDCC #$BF (clear F) ? I stays set for
		   life and the sequencer is FIRQ/C140-driven. Requiring !I skipped
		   every inject and left peak=0. */
		{
			mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
			if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2
				|| !cpu || !cpu->cc.i)
				TryInjectCommand();
		}
		NamcoM6809RunCycles(cpuHz_ / 2);
		/* gaplus/superpac/liblrabl finish handshake late ? warm until the
		   15XX speaks so classify chunks are not 0,0,0,peak (WEAK). */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()) {
			const int slice = hostRate_ > 0 ? hostRate_ / 10 : 4410;
			int16_t* tmp = (int16_t*)malloc((size_t)slice * 2 * sizeof(int16_t));
			if (tmp) {
				for (int w = 0; w < 80; w++) {
					mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
					if (cpu && !cpu->cc.i && cmdIndex_ < 1)
						TryInjectCommand();
					else if (hw_->NamcoM6809Read8(0x0040) == 0
						&& hw_->NamcoM6809Read8(0x0060) == 0)
						TryInjectCommand();
					NamcoM6809Render(tmp, slice);
					int peak = 0;
					for (int i = 0; i < slice * 2; i++) {
						int v = tmp[i]; if (v < 0) v = -v;
						if (v > peak) peak = v;
					}
					if (peak >= 200)
						break;
				}
				free(tmp);
			}
		}
		booted_ = 1;
		triggered_ = 1;
		/* WSG6809 mainloops clear $40 after consuming a command ? refresh
		   when the slot is empty so BGM can be (re)posted like the main CPU. */
		if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy())
			nextCmdAt_ = (uint64_t)hw_->CpuCycles() + (uint64_t)cpuHz_ / 60;
		else
			nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Namco System 86 / wsg63701: HD63701 + CUS30 (+ YM2151 on Sys86). */
	sys86_ = ((hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS86
			|| (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->Wsg63701()))
		&& hw_->HD63701Active()) ? 1 : 0;
	if (sys86_) {
		hasCpu_ = 1;
		sys86Acc_ = 0;
		cmdIndex_ = 0;
		if (!songCmd_) songCmd_ = 0x01;
		/* Boot CUS60, edged stop (clears doorbell+$B0), then start song. */
		nextCmdAt_ = (uint64_t)~0ull; /* freeze inject during boot/probe */
		Sys86RunCycles(cpuHz_);
		Sys86RunCycles(cpuHz_ / 2);
		/* If CUS60 boot skipped AE install (expanded map races), seed it. */
		if (hw_->HD63701Read8(0x00ae) == 0 && hw_->HD63701Read8(0x00af) == 0)
			hw_->SetSoundCommand(0); /* inject path also restores AE/table */
		hw_->SetSoundCommand(0);
		Sys86RunCycles(cpuHz_ / 4);
		hw_->SetSoundCommand(0); /* second stop ensures $1182 stays clear */
		Sys86RunCycles(cpuHz_ / 8);
		hw_->SetSoundCommand(songCmd_);
		cmdIndex_ = 1;
		Sys86RunCycles(cpuHz_);
		Sys86RunCycles(cpuHz_ / 2);
		booted_ = 1;
		triggered_ = 1;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Namco System 12 / ND-1: H8/3002 + C352. Sys11/22/NA1: M37702. */
	h8Board_ = (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && hw_->H8Active()) ? 1 : 0;
	m37702Board_ = (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && hw_->M37702Active()) ? 1 : 0;
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352) {
		hasCpu_ = (h8Board_ || m37702Board_) ? 1 : 0;
		if (h8Board_) {
			h8Acc_ = 0;
			cmdIndex_ = 0;
			H8RunCycles(cpuHz_);
			H8RunCycles(cpuHz_ / 2);
			TryInjectCommand();
			H8RunCycles(cpuHz_ / 4);
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		if (m37702Board_) {
			m37702Acc_ = 0;
			cmdIndex_ = 0;
			M37702RunCycles(cpuHz_);
			M37702RunCycles(cpuHz_ / 2);
			TryInjectCommand();
			M37702RunCycles(cpuHz_ / 4);
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		if (hw_->M37702Soft()) {
			/* Soft fallback: no CPU ? leave silent. */
			hasCpu_ = 0;
			cmdIndex_ = 0;
			booted_ = 1;
			triggered_ = 1;
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		booted_ = 1;
		return 1;
	}

	/* Irem M92 runs its encrypted NEC V35 instead of the Z80. */
	m92_ = (hw_->board_ == CEMU_AC_BOARD_IREM_M92) ? 1 : 0;
	if (m92_) {
		if (!hw_->M92Active()) return 0;
		m92Acc_ = 0;
		m92OpmRes_ = 0;
		m92NoteOffSeen_ = 0;
		m92ChannelPlayOff_ = 0;
		m92NoteStuck_ = 0;
		/* Prefill after a longer boot so IMC/DS/SS are ready on Rev 3.40+
		   sets (encrypted JMP FAR into low ROM). Song-gate [0316]/[0317]
		   and drain [0319] still need host priming when init is skipped. */
		cmdIndex_ = 0;
		/* Word-queue IMC (firebarr/nbbatman/�c): idle spins on [0C31]==3 until
		   the main CPU signals ready. Without that release the freelist never
		   builds and every BGM alloc returns empty. */
		if (hw_->M92WordQueue())
			hw_->M92Write8(0xa0c31u, 0x03);
		M92RunCycles(cpuHz_);
		M92RunCycles(cpuHz_ / 2);
		M92RunCycles(cpuHz_ / 2); /* Rev3.40 freelist/IMC can need ~2s */
		/* Shared Irem sound sequencer: song-gate compares [0316]/[0317] and
		   the command drain returns success only when [0319]!=0. lethalth /
		   bmaster init write FF here; gunforce/uccops often skip that path
		   under our host boot ? prime the same three bytes for every set. */
		hw_->M92Write8(0xa0316u, 0x00);
		hw_->M92Write8(0xa0317u, 0xff);
		hw_->M92Write8(0xa0319u, 0xff);
		/* lethalth/gunforce/bmaster leave the command-accept flags at
		   [0310]/[0311] set after IMC init; some sets stall with them clear
		   and drop the latch without writing the 0280 queue. */
		hw_->M92Write8(0xa0310u, 0x01);
		hw_->M92Write8(0xa0311u, 0x01);
		/* Rev 3.40+ ring dequeue returns a cmd only when (mask & cmd) != 0.
		   Byte ring: rtypeleo/hook [09EA], uccops [09EF].
		   Word ring: nbbatman/firebarr [0C31] (also the ready semaphore). */
		hw_->M92Write8(0xa09eau, 0xff);
		hw_->M92Write8(0xa09efu, 0xff);
		if (hw_->M92WordQueue()) {
			/* IMC freelist lives at [0B12]/[0B92]/[0B93] (stride 0x50 from
			   0x00A0 �~ 32). Boot builds it in CALL 0597 before the [0C31]==3
			   wait, but under host boot the list is often still empty when we
			   reach inject ? seed the same layout the ROM init writes. */
			const uint8_t* ram = hw_->M92Ram();
			if (ram && ram[0xb93] == 0 && ram[0xb92] == 0) {
				uint8_t* w = const_cast<uint8_t*>(ram);
				uint8_t wp = 0;
				for (unsigned i = 0; i < 0x20u; i++) {
					const unsigned bp = 0x00a0u + i * 0x50u;
					const unsigned off = 0xb12u + (((unsigned)wp & 0x3fu) << 1);
					w[off] = (uint8_t)(bp & 0xffu);
					w[off + 1u] = (uint8_t)(bp >> 8);
					wp = (uint8_t)((wp + 1u) & 0x3fu);
				}
				w[0xb93] = wp;
				w[0xb92] = 0;
			}
			/* Dequeue RET's ZF from CMP [0C32],#0 ? caller drops the cmd when
			   ZF set. Boot leaves [0C32]=0; handshake would raise it. */
			hw_->M92Write8(0xa0c32u, 0x01);
			hw_->M92Write8(0xa0c31u, 0xff);
		}
		TryInjectCommand();
		M92RunCycles(cpuHz_ / 4);
		booted_ = 1;
		/* One latch only ? retries abort channel-BGM mid-phrase (WEAK). */
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	if (ms1_) {
		if (!hw_->Ms1Active()) return 0;
		ms1Acc_ = 0;
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX) {
			/* Boot must finish the K054539 self-test and enable K056800 IRQs
			   before any host packet is posted ? early inject is dropped. */
			Ms1RunCycles(cpuHz_);
			Ms1RunCycles(cpuHz_ / 2);
			Ms1RunCycles(cpuHz_ / 2); /* tkmmpzdm: self-test PCM settles */
			if (!(hw_->GxSoundCtrl() & 1)) {
				hw_->Ms1Write8(0x500001u, 0xff);
				Ms1RunCycles(cpuHz_ / 4);
			}
			booted_ = 1;
			cmdIndex_ = 0;
			TryInjectCommand();
			/* Let the 68000 dequeue the K056800 packet and key K054539 before
			   the first host Render second (otherwise chunk0 is silent �� WEAK). */
			Ms1RunCycles(cpuHz_);
			nextCmdAt_ = (uint64_t)~0ull;
			return 1;
		}
		Ms1RunCycles(cpuHz_ / 2); /* boot: clear RAM, program YM2151/OKI */
		booted_ = 1;
		TryInjectCommand();
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* Seibu SEI80BU: boot is a long RAM clear (LDIR @00CC) then banked init.
	   Generic settle often leaves PC mid-clear with IFF1 clear. Force past
	   the three LDIR blocks to the CALL/EI sequence, then edge RST18. */
	if (hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		hasCpu_ = 1;
		/* Prefer catalog title; SetSoundCommand maps 0x81��table 0x8b. */
		if (!songCmd_)
			songCmd_ = 0x80;
		CEmuHardAcSetActive(hw_);
		/* Give the three boot LDIRs time to finish (~0.25s) before synthesizing. */
		RunUntil((uint64_t)cpuHz_ / 4);
		{
			Ay_Cpu* c = hw_->Cpu();
			uint8_t* m = hw_->Mem();
			if (c && m) {
				const unsigned pc = (unsigned)c->r.pc;
				/* Stuck in boot LDIRs (00CC/00D7/00E7) ? synthesize post-clear. */
				if (pc >= 0x00BFu && pc <= 0x00E8u) {
					memset(m + 0x2000, 0, 0x800);
					memcpy(m + 0x2463, m + 0x1303, 0x24);
					memcpy(m + 0x2487, m + 0x1327, 0x10);
					m[0x2021] = 0x09;
					m[0x2046] = 0xff;
					m[0x2221] = 0xff;
					m[0x2326] = 0x04;
					c->r.sp = 0x2800;
					c->r.pc = 0x00E9;
					c->r.w.bc = 0;
				}
			}
		}
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 2);
		{
			Ay_Cpu* c = hw_->Cpu();
			for (int i = 0; i < 16 && c && !c->r.iff1; i++) {
				RunUntil((uint64_t)c->time() + (uint64_t)cpuHz_ / 8);
				if ((unsigned)c->r.pc >= 0x0119u && !c->r.iff1) {
					c->r.iff1 = 1;
					c->r.iff2 = 1;
					break;
				}
				/* Still before EI ? jump to it after init CALLs settled. */
				if ((unsigned)c->r.pc >= 0x0100u && (unsigned)c->r.pc < 0x0119u
					&& i >= 4) {
					c->r.pc = 0x0119;
					c->r.iff1 = 1;
					c->r.iff2 = 1;
					break;
				}
			}
			if (c && !c->r.iff1) {
				c->r.iff1 = 1;
				c->r.iff2 = 1;
			}
		}
		booted_ = 1;
		/* Unblock main loop spin at 0126 ? normally set by YM RST10 ISR. */
		if (uint8_t* m = hw_->Mem()) {
			m[0x201c] = 0xff;
			m[0x201d] = 0xff;
		}
		hw_->SetSoundCommand(songCmd_);
		cmdIndex_ = 1;
		triggered_ = 1;
		/* Let the 0x80 scan / 054A allocate finish before Render. */
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ * 2u);
		if (uint8_t* m = hw_->Mem())
			m[0x201c] = 0xff;
		nextCmdAt_ = (uint64_t)~0ull;
		return 1;
	}

	/* MAME irem/m72.cpp: a periodic NMI at MASTER_CLOCK/8/512 = 7812.5 Hz pumps
	   one PCM byte per tick. Games with an empty NMI handler (airduel, gallop,
	   poundfor�c) get the same transfer done host-side instead (fake_nmi). */
	nextM72Nmi_ = 0;
	m72FakeNmi_ = 0;
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		const uint8_t h0 = hw_->PeekMem(0x0066);
		const uint8_t h1 = hw_->PeekMem(0x0067);
		/* MAME: R-Type NMI is DI;HALT (no samples). Others RET / RETN / NOP. */
		if (h0 == 0xc9 || h0 == 0x00 || h0 == 0xff || h0 == 0x76
			|| (h0 == 0xed && h1 == 0x45)
			|| (h0 == 0xf3 && h1 == 0x76))
			m72FakeNmi_ = 1;
	}

	/* Boot settle ~0.5s so init clears RAM and sets up OPM. */
	uint64_t bootCycles = (uint64_t)cpuHz_ / 2;
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		nextGngIrq_ = (uint64_t)cpuHz_ / 250; /* MAME: 8MHz/32000 ? 250 Hz */
	else
		nextGngIrq_ = (uint64_t)cpuHz_ / 240;
	/* CPS1/2 QSound: init spins/HALTs until shared CFFF==0xFF (68K ready).
	   Without a main CPU that wait turns Ay_Cpu HALT into a multi-minute
	   boot (RunOne advances ~4 clocks per call). Release before settle.
	   Kabuki sets (dino/wof) hit this path for real; encrypted garbage did not. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
		if (uint8_t* m = hw_->Mem()) {
			m[0xcfff] = 0xff;
			m[0xcffd] = 0x00;
		}
	}
	/* Bosco WSG: patch LD SP,$8000 �� $9F00 before any NMI can push. */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->Cpu()) {
		uint8_t* m = hw_->Mem();
		if (m && m[0] == 0x31 && m[1] == 0x00 && m[2] == 0x80) {
			m[1] = 0x00;
			m[2] = 0x9f;
			m[0x8c01] = 0;
			hw_->Cpu()->r.sp = 0x9f00;
		}
	}
	/* Toaplan1: main CPU holds shared-RAM (8001)==0xAA. Truxton/hellfire/
	   demonwld/zerowing/outzone write 0 then busy-wait for AA ? a one-shot
	   poke before RunUntil is cleared and the Z80 never leaves DI boot. */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1) {
		hw_->SetSoundCommand(0xff);
		for (int i = 0; i < 500; i++) {
			if (uint8_t* m = hw_->Mem())
				m[0x8001] = 0xaa;
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 500);
			Ay_Cpu* c = hw_->Cpu();
			if (!c) break;
			const unsigned pc = (unsigned)c->r.pc;
			const uint8_t b0 = hw_->PeekMem((uint16_t)pc);
			const uint8_t b1 = hw_->PeekMem((uint16_t)(pc + 1u));
			int idle = 0;
			if (b0 == 0xfb && b1 == 0xc3) {
				const unsigned tgt = (unsigned)hw_->PeekMem((uint16_t)(pc + 2u))
					| ((unsigned)hw_->PeekMem((uint16_t)(pc + 3u)) << 8);
				idle = (tgt == pc);
			} else if (b0 == 0xc3) {
				const unsigned tgt = (unsigned)b1
					| ((unsigned)hw_->PeekMem((uint16_t)(pc + 2u)) << 8);
				idle = (tgt == pc);
			} else if (b0 == 0x18 && b1 == 0xfe) {
				idle = 1;
			}
			if (idle && c->r.iff1)
				break;
		}
	} else {
		/* GX400: do NOT poke shared RAM during the 4000-7FFF self-test ?
		   that corrupts verify and leaves the Z80 wedged. Wait until PC
		   clears the test, then release the (7FFC)==4 main-CPU handshake. */
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
			for (int i = 0; i < 360; i++) {
				RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60);
				Ay_Cpu* c = hw_->Cpu();
				const unsigned pc = c ? (unsigned)c->r.pc : 0;
				/* Release (7FFC)==4 only after RAM self-test (PC>=0x200).
				   AY1 port A is Gx400PortA() ? do not force it clear. */
				if (pc >= 0x0200u && pc < 0x8000u) {
					if (uint8_t* m = hw_->Mem()) {
						m[0x7ffc] = 0x04;
						m[0x7ffd] = 0x02;
						m[0x7ffe] = 0x02;
					}
				}
				if (c && c->r.iff1 && pc >= 0x0290u)
					break;
			}
		} else {
			RunUntil(bootCycles);
		}
	}
	booted_ = 1;
	/* Sys16B (goldnaxe): bit7 latch cmds queue at F818 where empty=0x80.
	   Boot LDIR clears F800-FFFF to 00, so 0212 finds no free slot and drops
	   every BGM until 02C7 has run once ? by then the latch edge is gone on
	   some sets. Seed the same empty markers 02D4 writes after a drain. */
	if (hw_->board_ == CEMU_AC_BOARD_SYS16B) {
		if (uint8_t* m = hw_->Mem()) {
			if (m[0xf818] == 0x00 && m[0xf819] == 0x00) {
				m[0xf818] = 0x80;
				m[0xf819] = 0x80;
			}
		}
	}
	/* Taito PC060HA / TC0140SYT drivers gate the whole sequencer behind a
	   "sound on" control command that the 68000 issues at boot: Rastan's
	   note-start routine at 02FE returns immediately while its enable flag
	   (8F26) is 0, and Bonze Adventure's at 0413 does the same on CF2C bit 0.
	   In both families $EF sets the flag and $EE clears it. The catalog
	   carries song numbers only, so issue the enable here and let the Z80
	   consume it before the song command goes out. */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610) {
		hw_->SetSoundCommand(0xef);
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 10);
		/* Rastan/Asuka gate note-start on [8F26]; F2/Bonze use [CF2C]/[CF30].
		   Host EF can sit unread while the Z80 is in its DI;OPM window ? force
		   the enable flags. Asuka also needs the song byte in the 8F02 ring
		   (NMI enqueue at 00C0) or 0254 drains an empty queue forever. */
		if (uint8_t* m = hw_->Mem()) {
			if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM) {
				/* Rastan/Asuka: enable @8F26. masterw/viofight YM2203: EF
				   writes 0x07 to 8F25 ? note gates test that byte. */
				m[0x8f26] = 0x01;
				if (hw_->MainIsYm2203())
					m[0x8f25] = 0x07;
				if (hw_->MainIsYm2203()) {
					/* masterw: with 8F26 bit0 set, 033A only queues a handshake
					   into 8F27 ? it never CALL 0388. Clear bit0, enqueue the
					   song on the 8F02 ring, and let 033A take the <0x35 path
					   that actually starts voices. Then restore enable. */
					if (Ay_Cpu* cpu = hw_->Cpu()) {
						while (hw_->IrqPulsePending())
							hw_->TakeIrqPulse();
						m[0x8f26] = 0x00;
						const uint8_t rd = (uint8_t)(m[0x8f01] & 0x0f);
						const uint8_t wr = (uint8_t)((rd + 1) & 0x0f);
						m[0x8f00] = wr;
						m[0x8f01] = rd;
						m[0x8f02 + wr] = songCmd_;
						/* Drain from mainloop CALL 033A site. */
						const uint16_t sp0 = cpu->r.sp;
						const uint16_t sp = (uint16_t)(sp0 - 2);
						m[(sp + 0) & 0xffffu] = 0x73; /* return @0273 */
						m[(sp + 1) & 0xffffu] = 0x02;
						cpu->r.sp = sp;
						cpu->r.iff1 = 0;
						cpu->r.iff2 = 0;
						cpu->irqDelay = 0;
						cpu->r.pc = 0x033a;
					}
					RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 30);
					if (uint8_t* mm = hw_->Mem()) {
						mm[0x8f25] = 0x07;
						mm[0x8f26] = 0x01;
					}
					if (Ay_Cpu* cpu = hw_->Cpu()) {
						cpu->r.iff1 = 1;
						cpu->r.iff2 = 1;
					}
				} else {
					/* Enqueue onto the 8F02 ring the way NMI's CALL 00C0 does:
					   advance write ptr, store cmd at 8F02+ptr (low nibble). */
					const uint8_t rd = (uint8_t)(m[0x8f01] & 0x0f);
					const uint8_t wr = (uint8_t)((rd + 1) & 0x0f);
					m[0x8f00] = wr;
					m[0x8f01] = rd;
					m[0x8f02 + wr] = songCmd_;
				}
				/* Re-arm Timer A/B so the EI;DI mainloop has status&3 to
				   service. YM2151 uses 0x10-0x14; YM2203 (masterw) uses
				   0x24-0x27 ? poking OPM regs into OPN left st=00 forever. */
				if (hw_->SoundChip()) {
					CChip* ym = hw_->SoundChip();
					if (hw_->MainIsYm2203()) {
						ym->Write(0, 0x24); ym->Write(1, 0xff);
						ym->Write(0, 0x25); ym->Write(1, 0x00);
						ym->Write(0, 0x26); ym->Write(1, 0xc0);
						ym->Write(0, 0x27); ym->Write(1, 0x35);
					} else {
						ym->Write(0, 0x10); ym->Write(1, 0xff);
						ym->Write(0, 0x11); ym->Write(1, 0x00);
						ym->Write(0, 0x12); ym->Write(1, 0xc0);
						ym->Write(0, 0x14); ym->Write(1, 0x35);
					}
				}
			} else {
				m[0xcf2c] = (uint8_t)(m[0xcf2c] | 0x07);
				m[0xcf30] = (uint8_t)(m[0xcf30] | 0x07);
			}
		}
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 20);
	}
	/* Irem M72: airduel-family drops latch bytes until $00 arms ready (FF56).
	   R-Type is the opposite ? $00 is STOP and ends in DI;HALT (NMI vector is
	   already F3 76), so a pre-song $00 freezes the Z80 and every later command
	   is lost. poundfor/bbmanw (YM@40, RETN NMI) also treat $00 as STOP.
	   airduel also has RETN/empty NMI but REQUIRES the $00 arm ? only skip
	   when the ROM talks to YM at port 40. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		const uint8_t n0 = hw_->PeekMem(0x0066);
		const uint8_t n1 = hw_->PeekMem(0x0067);
		const int rtypeStopHalts = (n0 == 0xf3 && n1 == 0x76) || n0 == 0x76;
		int ym40 = 0;
		for (unsigned i = 0; i + 1 < 0x400u; i++) {
			if (hw_->PeekMem((uint16_t)i) == 0xd3
				&& hw_->PeekMem((uint16_t)(i + 1)) == 0x40) {
				ym40 = 1;
				break;
			}
		}
		if (!rtypeStopHalts && !ym40) {
			hw_->SetSoundCommand(0x00);
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 20);
		}
		/* m99 (bbmanw/poundfor): boot does XOR A at reset then PUSH AF, so the
		   command mask (F4DC or FF56) is stored as 0. Readers then discard
		   every latch byte. dynablst works because the $00 handshake primes
		   FF56; ym40 sets skip that ? poke both masks here.
		   poundfor/dynablst main loop only drains the command ring when
		   FF57/FF58 are non-zero (YM ISR increments them). Soft-kick. */
		if (ym40 || hw_->M72IoAlt()) {
			if (uint8_t* m = hw_->Mem()) {
				if (m[0xf4dc] == 0x00)
					m[0xf4dc] = 0xff;
				if (m[0xff56] == 0x00)
					m[0xff56] = 0x20;
				if (m[0xff57] == 0x00)
					m[0xff57] = 0x08;
				if (m[0xff58] == 0x00)
					m[0xff58] = 0x08;
			}
		}
	}
	/* flstory: boot LD A,(D800) at 0103 eats a pre-enable latch (clears
	   pending without NMI). hcastle: ForceIm1 during DI RAM-test nests.
	   Both need a settled EI/NMI-enable before the first song inject.
	   Toaplan1 injects after boot settle below (Timer-A ISR mailbox). */
	if (!(hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4)
		&& hw_->board_ != CEMU_AC_BOARD_SNK_OPL
		&& hw_->board_ != CEMU_AC_BOARD_FLSTORY
		&& hw_->board_ != CEMU_AC_BOARD_KONAMI_HCASTLE
		&& hw_->board_ != CEMU_AC_BOARD_TOAPLAN1
		&& !(hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3))
		TryInjectCommand();
	/* Let RST 18h drain the latch before the first host Render. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72)
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 20);
	/* Konami AY: Open inject arms irqPulse_ ? run so ForceIm1 enters 0038
	   and the music engine can claim a channel before Render hunting. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400
		|| hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16
		|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
		|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
		|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS)
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
	/* GX400 shared RAM (4000-7FFF) is owned by the missing 68000. Sound ROM
	   waits on (7FFC)==4 after self-test before EI @0291 ? release it. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		for (int i = 0; i < 240; i++) {
			Ay_Cpu* c = hw_->Cpu();
			const unsigned pc = c ? (unsigned)c->r.pc : 0;
			if (pc >= 0x0200u && pc < 0x8000u) {
				if (uint8_t* m = hw_->Mem()) {
					m[0x7ffc] = 0x04;
					m[0x7ffd] = 0x02;
					m[0x7ffe] = 0x02;
				}
			}
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60);
			c = hw_->Cpu();
			/* Mainloop @029C waits on AY timer bit2 ? no need for IFF1. */
			if (c && (unsigned)c->r.pc >= 0x0290u && (unsigned)c->r.pc < 0x0340u)
				break;
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
	}
	/* K053260: boot DI (ROM checksum via reg 2E / YM timer poll), then
	   post via K053260 ports + IRQ0 once EI is live and boot left the
	   ROM-scan / timer-wait stubs. parodius scans 8 banks (~3s). */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3) {
		for (int i = 0; i < 600; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60);
			Ay_Cpu* c = hw_->Cpu();
			if (!c) continue;
			const unsigned pc = (unsigned)c->r.pc;
			if (pc >= 0x06c0u && pc < 0x06f0u) continue;
			if (pc >= 0x00d0u && pc < 0x00e0u) continue;
			if (!c->r.iff1) continue;
			break;
		}
		/* Finish any in-progress ROM bank scan before the first song IRQ. */
		for (int i = 0; i < 600; i++) {
			Ay_Cpu* c = hw_->Cpu();
			const unsigned pc = c ? (unsigned)c->r.pc : 0;
			if (pc < 0x06c0u || pc >= 0x06f0u) break;
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60);
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
	}
	if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		/* Reach idle (DA00 NMI enable @0158) then inject so NMI queues C300. */
		for (int i = 0; i < 120 && !hw_->FlstoryNmiEn(); i++)
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
		/* Warm until MSM speaks. Clones (40love/fieldday/victnine) often keep
		   the catalog prefer mute ? walk kFlstoryTryCmds until a peak appears. */
		{
			const int slice = hostRate_ > 0 ? hostRate_ / 10 : 4410;
			int16_t* tmp = (int16_t*)malloc((size_t)slice * 2 * sizeof(int16_t));
			if (tmp) {
				int found = 0;
				for (int attempt = 0; attempt < 12 && !found; attempt++) {
					for (int w = 0; w < 40; w++) {
						Render(tmp, slice);
						int peak = 0;
						for (int i = 0; i < slice * 2; i++) {
							int v = tmp[i]; if (v < 0) v = -v;
							if (v > peak) peak = v;
						}
						if (peak >= 400) {
							found = 1;
							break;
						}
					}
					if (found) break;
					/* Next try-table entry (unpinned) or re-arm same song. */
					if (!pinned_)
						TryInjectCommand();
					else {
						cmdIndex_ = 0;
						TryInjectCommand();
					}
					RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 10);
				}
				free(tmp);
			}
		}
		cmdIndex_ = 0;
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 10);
		/* Keep re-arming the live song during Render so classify stays PLAY. */
		pinned_ = 1;
	}
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		/* Boot DI RAM-test; wait for main EI;DI poll (@03CE) before latch. */
		for (int i = 0; i < 240; i++) {
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60);
			const uint16_t pc = hw_->Cpu() ? (uint16_t)hw_->Cpu()->r.pc : 0;
			if (pc >= 0x03c0 && pc < 0x0900)
				break;
		}
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 4);
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 4);
	}
	if (hw_->board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (hw_->SnkMapKind()) {
			/* Classic SNK: latch �� IRQ0; YM timers also drive the sequencer.
			   Boot stores 0x0C at C0A8; type-2 BGM (athena 0x53) does
			   BIT 2,(C0A8);RET NZ at 063F ? main CPU clears that lock. */
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 4);
			if (uint8_t* m = hw_->Mem()) {
				/* athena/ikari: C0A8; gwar/psychos: C100 ? same 0x0C boot lock. */
				m[0xc0a8] = (uint8_t)(m[0xc0a8] & (uint8_t)~0x0cu);
				m[0xc100] = (uint8_t)(m[0xc100] & (uint8_t)~0x0cu);
			}
			TryInjectCommand();
			nextGngIrq_ = 0;
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 2);
		} else {
			/* SNK68: NMI is gated by a RAM lock (F151/F132) set during boot.
			   Injecting before the main loop clears it drops the only edge ?
			   streetsm hung silent while pow got lucky on timing. Boot first.
			   Boot also stores 0x0C at F115; type-2/3 BGM (streetsm 0x47/0xBF)
			   RET NZ on those bits and never start ? clear after settle. */
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 2);
			if (uint8_t* m = hw_->Mem())
				m[0xf115] = (uint8_t)(m[0xf115] & (uint8_t)~0x0cu);
			TryInjectCommand();
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 4);
			TryInjectCommand();
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 4);
		}
	}
	/* Re-assert masks after the first command drain. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72 && hw_->M72IoAlt()) {
		if (uint8_t* m = hw_->Mem()) {
			if (m[0xf4dc] == 0x00)
				m[0xf4dc] = 0xff;
			if (m[0xff56] == 0x00)
				m[0xff56] = 0x20;
			if (m[0xff57] < 2)
				m[0xff57] = 0x08;
			if (m[0xff58] < 2)
				m[0xff58] = 0x08;
		}
	}
	/* Bucky/Moo: finish K054539 self-test / F0 handshake before the first
	   song IRQ ? an early ForceIm1 during DI boot leaves opmWrites==0. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4) {
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 2);
		/* Boot waits on YM2151 Timer B (EC01 bit1) after programming reg 14.
		   If fmgen never raised the flag, poke the enable path once more. */
		if (hw_->SoundChip() && CEmuChipYm2151WriteCount(hw_->SoundChip()) == 0) {
			CChip* ym = hw_->SoundChip();
			ym->Write(0, 0x14); ym->Write(1, 0x20);
			ym->Write(0, 0x12); ym->Write(1, 0xf0);
			ym->Write(0, 0x14); ym->Write(1, 0x0a);
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 10);
		}
		cmdIndex_ = 0;
		TryInjectCommand();
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 5);
		if (hw_->PcmChip())
			hw_->PcmChip()->Write(0x22f, 0x01);
	}
	/* Toaplan1: shared-RAM command is polled from the YM Timer-A ISR.
	   Inject then pump IRQs until the mailbox clears (song installed). */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1) {
		TryInjectCommand();
		for (int i = 0; i < 90; i++) {
			hw_->SetToaplanTimerA(1);
			Ay_Cpu* c = hw_->Cpu();
			if (c && c->r.iff1 && c->r.im == 1)
				Ay_CpuIm1Interrupt(c);
			RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 250);
			if (hw_->PeekMem(0x8000) == 0xff)
				break;
		}
		nextCmdAt_ = (uint64_t)~0ull; /* one-shot ? re-inject clears slots */
		return 1;
	}
	/* Pinned catalog title: give Z80 time to start audio. An early silence
	   fallback used to re-inject try-table[0] and force "always song 1".
	   M72/Sys16B/VSystem catalogs often pin SE ? check sooner so the try
	   table can recover within a short probe window. */
	if (pinned_) {
		const int fast = (hw_->board_ == CEMU_AC_BOARD_IREM_M72
			|| hw_->board_ == CEMU_AC_BOARD_SYS16B
			|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM
			|| hw_->board_ == CEMU_AC_BOARD_GNG
			|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
			|| (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM
				&& (hw_->PcmKind() == 3 || hw_->PcmKind() == 4)));
		nextCmdAt_ = (uint64_t)hw_->Cpu()->time()
			+ (uint64_t)cpuHz_ * (fast ? 1ull : 3ull)
			/ ((hw_->board_ == CEMU_AC_BOARD_IREM_M72
				|| hw_->board_ == CEMU_AC_BOARD_SYS16B
				|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM) ? 2ull : 1ull);
	} else
		nextCmdAt_ = (uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60;
	/* WSG: digdug/galaga need a couple of latch refreshes after boot. */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG)
		nextCmdAt_ = (uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 4;
	/* CPS2: command write releases CFFF wait; give a short run so EI/IRQs start
	   before the first host Render callback. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS)
		RunUntil((uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 10);
	return 1;
}

void CDriverAc::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
	if (scratch_) {
		free(scratch_);
		scratch_ = NULL;
		scratchFrames_ = 0;
	}
}

unsigned CDriverAc::OpmWrites() const
{
	return hw_ ? hw_->opmWrites_ : 0;
}

void CDriverAc::TickOpm(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	if (cpuHz_ < 1 || opmHz_ < 1) return;
	opmResidual_ += cpuCycles * (uint64_t)opmHz_;
	const uint64_t opmTicks = opmResidual_ / (uint64_t)cpuHz_;
	opmResidual_ %= (uint64_t)cpuHz_;
	if (opmTicks) {
		/* Hang-On / Space Harrier: full master clocks were ~2�~ Timer-B; half
		   felt a touch slow. Use 3/4 so BGM tempo sits near cabinet rate
		   without re-doubling. Pitch stays in Render (sample-driven). */
		const uint64_t timerTicks = (hw_->board_ == CEMU_AC_BOARD_HANGON)
			? ((opmTicks * 3u) / 4u) : opmTicks;
		if (timerTicks)
			hw_->SoundChip()->AdvanceClocks(timerTicks);
		if (hw_->Chip2())
			hw_->Chip2()->AdvanceClocks(opmTicks);
		if (hw_->Chip3())
			hw_->Chip3()->AdvanceClocks(opmTicks);
	}
	/* K054539 boards derive the sound NMI from the PCM chip's own timer, so
	   it needs its 18.432 MHz clock even though MixAdd renders by sample. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4) {
		k054539Residual_ += cpuCycles * 18432000ull;
		const uint64_t kt = k054539Residual_ / (uint64_t)cpuHz_;
		k054539Residual_ %= (uint64_t)cpuHz_;
		if (kt) {
			if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks(kt);
			if (hw_->KonamiPcm2()) hw_->KonamiPcm2()->AdvanceClocks(kt);
		}
	}
}

void CDriverAc::TryInjectCommand()
{
	if (!hw_) return;
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX) {
		uint16_t cmd = songCmdWord_ ? songCmdWord_ : (uint16_t)songCmd_;
		if (cmdIndex_ == 0 && cmd) {
			hw_->SetSoundCommandWord(cmd);
			cmdIndex_++;
			triggered_ = 1;
			return;
		}
		if (cmdIndex_ == 0) {
			hw_->SetSoundCommandWord(0x0101);
			cmdIndex_++;
			triggered_ = 1;
		}
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M92) {
		/* Early sets + note-list (uccops): catalog codes are raw latch values.
		   Channel-BGM Rev 3.40 only: BGM is 0x20+index. */
		if (!songCmd_ || cmdIndex_ >= 6) return;
		uint8_t cmd = songCmd_;
		const uint8_t base = hw_->M92SongCmdBase();
		if (base && hw_->M92ChannelBgm() && cmd < 0x20)
			cmd = (uint8_t)(base + (cmd & 0x1fu));
		/* Keep the Rev3.40 mode byte clear so dequeue is not skipped.
		   Masks: byte-ring [09EA]/[09EF]; word-ring [0C31]. */
		hw_->M92Write8(0xa004fu, 0x00);
		hw_->M92Write8(0xa09eau, 0xff);
		hw_->M92Write8(0xa09efu, 0xff);
		if (hw_->M92WordQueue()) {
			/* Word ring stores AX. INTP1 leaves AH stale from the idle loop,
			   and (AH&AL)!=0 with AL<0xF0 rejects BGM. Plant AH=0 directly
			   into the 0AF0 ring ? same write the enqueue routine performs. */
			hw_->M92Write8(0xa0c31u, 0xff);
			const uint8_t* ram = hw_->M92Ram();
			if (ram) {
				uint8_t* wram = const_cast<uint8_t*>(ram);
				const uint8_t wp = wram[0xb10];
				const unsigned off = 0xaf0u + (((unsigned)wp & 0x0fu) << 1);
				wram[off] = cmd;
				wram[off + 1u] = 0x00; /* AH = 0 �� classic BGM path */
				wram[0xb10] = (uint8_t)((wp + 1u) & 0x0fu);
			}
		} else {
			hw_->SetSoundCommand(cmd);
		}
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_DECO
		|| hw_->board_ == CEMU_AC_BOARD_ATARI_SYS1) {
		/* Firmware treats a repeat of the same BGM id as "replace": it ORA #$80
		   then clears the $2310 slot ? so re-inject kills FM channels. Once only. */
		if (!songCmd_ || cmdIndex_ >= 1) return;
		hw_->SetSoundCommand(songCmd_);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS1
		|| hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		if (!songCmd_ && !songCmdWord_) return;
		if (cmdIndex_ >= 1) return;
		if (songCmdWord_ > 0xffu)
			hw_->SetSoundCommandWord(songCmdWord_);
		else
			hw_->SetSoundCommand(songCmd_ ? songCmd_ : (uint8_t)songCmdWord_);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* One-shot: re-latching mid-scan resets (200D) and aborts BGM start. */
		if (cmdIndex_ >= 1) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x80;
		if (uint8_t* m = hw_->Mem())
			m[0x201c] = 0xff; /* kick main loop */
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SCSP && hw_->SegaM1Audio()) {
		/* Re-inject a few times ? first MIDI packet can land before UART ISR
		   is ready; MultiPCM bank/program needs a sustained FIFO drain. */
		if (cmdIndex_ >= 4) return;
		const uint16_t w = songCmdWord_ ? songCmdWord_ : (uint16_t)songCmd_;
		if (!w) return;
		hw_->SetSoundCommandWord(w);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M62) {
		/* Song already posted in Open (cmd + 0x80 handshake). No re-edge:
		   leaving IRQ1 asserted while MSM VCK NMIs nest blows the 128-byte
		   IRAM stack and the sequencer dies. */
		if (cmdIndex_ >= 1) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x20;
		if (cmd & 0x80) cmd = (uint8_t)(cmd & 0x7fu);
		if (!cmd) cmd = 0x20;
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_SYS86) {
		/* One-shot mailbox: repeating $1182=$A6 while B0!=0 diverts the IRQ
		   music updater into the host-handshake path and kills KeyOn. */
		if (cmdIndex_ >= 1) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x01;
		if (!cmd) return;
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		/* Refresh path may call repeatedly ? allow sustained BGM without
		   walking a try-table past the catalog title. */
		if (!songCmd_) return;
		hw_->SetSoundCommand(songCmd_);
		if (cmdIndex_ < 1) cmdIndex_ = 1;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && (hw_->H8Active() || hw_->M37702Active())) {
		/* One-shot: catalog/default word only ? no try-table walk. */
		if (cmdIndex_ >= 1) return;
		const uint16_t w = songCmdWord_ ? songCmdWord_
			: (uint16_t)(songCmd_ ? songCmd_ : 0x20);
		if (!w) return;
		hw_->SetSoundCommandWord(w);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (hw_->board_ == CEMU_AC_BOARD_M68K_PCM) {
		/* TryInjectCommand periodically calls this for M68K_PCM. */
		if (cmdIndex_ >= 4) return;
		uint8_t cmd = songCmd_ ? songCmd_ : (uint8_t)0x01;
		if (!cmd) return;
		hw_->SetSoundCommand(cmd);
		cmdIndex_++;
		triggered_ = 1;
		return;
	}
	if (!ms1_ && !hw_->Cpu()) return;
	if (!ms1_ && !hw_->Cpu()) return;
	const uint8_t* table = kSys16TryCmds;
	int n = (int)(sizeof(kSys16TryCmds) / sizeof(kSys16TryCmds[0]));
	if (hw_->board_ == CEMU_AC_BOARD_CPS1) {
		table = kCps1TryCmds;
		n = (int)(sizeof(kCps1TryCmds) / sizeof(kCps1TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_GNG) {
		table = kGngTryCmds;
		n = (int)(sizeof(kGngTryCmds) / sizeof(kGngTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_ABURNER) {
		table = kAburnerTryCmds;
		n = (int)(sizeof(kAburnerTryCmds) / sizeof(kAburnerTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_OUTRUN || hw_->board_ == CEMU_AC_BOARD_HANGON) {
		table = kOutRunTryCmds;
		n = (int)(sizeof(kOutRunTryCmds) / sizeof(kOutRunTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
		table = kCpsQsTryCmds;
		n = (int)(sizeof(kCpsQsTryCmds) / sizeof(kCpsQsTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		table = kKonamiTryCmds;
		n = (int)(sizeof(kKonamiTryCmds) / sizeof(kKonamiTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352) {
		table = kNamcoTryCmds;
		n = (int)(sizeof(kNamcoTryCmds) / sizeof(kNamcoTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_SYS18 || hw_->board_ == CEMU_AC_BOARD_SYS32
		|| hw_->board_ == CEMU_AC_BOARD_SYS24) {
		table = kSys18TryCmds;
		n = (int)(sizeof(kSys18TryCmds) / sizeof(kSys18TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_SJ) {
		table = kTaitoTryCmds;
		n = (int)(sizeof(kTaitoTryCmds) / sizeof(kTaitoTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		table = kIremTryCmds;
		n = (int)(sizeof(kIremTryCmds) / sizeof(kIremTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_IREM_M92) {
		table = kM92TryCmds;
		n = (int)(sizeof(kM92TryCmds) / sizeof(kM92TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		table = kSys1TryCmds;
		n = (int)(sizeof(kSys1TryCmds) / sizeof(kSys1TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		table = kKonamiAyTryCmds;
		n = (int)(sizeof(kKonamiAyTryCmds) / sizeof(kKonamiAyTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		table = kGx400TryCmds;
		n = (int)(sizeof(kGx400TryCmds) / sizeof(kGx400TryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		table = kFlstoryTryCmds;
		n = (int)(sizeof(kFlstoryTryCmds) / sizeof(kFlstoryTryCmds[0]));
	} else if (hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
		|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
		|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
		|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
		|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS) {
		table = kDdragon2TryCmds;
		n = (int)(sizeof(kDdragon2TryCmds) / sizeof(kDdragon2TryCmds[0]));
	}
	uint8_t cmd;
	if (cmdIndex_ == 0 && songCmd_) {
		cmd = songCmd_;
	} else {
		const int ti = (songCmd_ ? cmdIndex_ - 1 : cmdIndex_);
		if (ti < 0 || ti >= n) return;
		cmd = table[ti];
	}
	/* ZN QSound: catalog codes are 16-bit song words consumed as a latch pair. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS && hw_->QsZn()) {
		/* One-shot FF 00 hi lo ? never walk the CPS2 try table. */
		if (cmdIndex_ >= 1) return;
		const uint16_t w = songCmdWord_ ? songCmdWord_ : (uint16_t)cmd;
		if (!w && !songCmd_) return;
		hw_->SetSoundCommandWord(w ? w : (uint16_t)songCmd_);
		cmdIndex_++;
		triggered_ = 1;
		return;
	} else if (hw_->board_ == CEMU_AC_BOARD_NAMCO_C352 && hw_->H8Active()) {
		const uint16_t w = (cmdIndex_ == 0 && songCmdWord_)
			? songCmdWord_ : (uint16_t)cmd;
		hw_->SetSoundCommandWord(w);
	} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3) {
		const uint16_t w = songCmdWord_ ? songCmdWord_ : (uint16_t)cmd;
		if (w > 0xffu)
			hw_->SetSoundCommandWord(w);
		else
			hw_->SetSoundCommand((uint8_t)w);
	} else {
		hw_->SetSoundCommand(cmd);
	}
	cmdIndex_++;
	triggered_ = 1;
}

void CDriverAc::DeliverIrqs()
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();

	/* Namco Galaga/Dig Dug/Bosco: sound CPU work is entirely NMI-driven
	   (RST $0038 is empty). Main CPU pulses NMI; we rate-limit ~240 Hz.
	   Suppress periodic NMI until first latch ? early NMI trashes galaga/
	   bosco boot checksum (only AF is saved). */
	if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		if (uint8_t* m = hw_->Mem()) {
			m[0x9101] = 0; /* galaga handshake */
			m[0x8c01] = 0; /* bosco handshake */
		}
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		if (!hw_->WsgNmiEnable())
			return;
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 240;
		if (period > 0 && now >= nextGngIrq_) {
			Ay_CpuNmi(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* GNG: Capcom uses irq0_line_hold @ 4*60 Hz ? not latch-edge.
	   Tecmo gaiden: latch �� NMI; YM2203 IRQ drives the sequencer. */
	if (hw_->board_ == CEMU_AC_BOARD_GNG) {
		if (hw_->GngGaidenMap()) {
			if (hw_->IrqPulsePending()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
			if (chip && chip->Irq()) {
				if (cpu->r.iff1)
					Ay_CpuIm1Interrupt(cpu);
				chip->AckIrq();
			}
			return;
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 240;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* CPS2 QSound: periodic IM1 @ 250 Hz (8 MHz / 32000). Shared-RAM commands
	   are picked up in the IRQ handler at 0038 ? never NMI/latch.
	   Capcom ZN (QsZn): one-shot NMI per latch write (MAME soundlatch��NMI).
	   Do NOT hold NMI every slice ? that floods and corrupts Z80 state.
	   Boot uses IM 2; vector the 250 Hz line accordingly. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
		if (hw_->QsZn()) {
			/* Follow-up latch bytes: NMI clears IFF1 for the whole handler
			   (ts2 @0066 RETN@0090, sfex @0066 RETN@0098, techromn RETN@00BD,
			   sfex2/tgmj JP 0180 RETN@0195). A fixed 0066..0091 window re-pulsed
			   mid-handler on later ZN firmware and corrupted the F000/F100 ring
			   (sfex F100=00100000, iff stuck 0). Wait for IFF1 after RETN. */
			if (cpu->r.iff1 && hw_->ZnTakeDeferredNmi())
				hw_->PulseIrq();
			if (hw_->IrqPulsePending()) {
				hw_->TakeIrqPulse();
				Ay_CpuNmi(cpu);
			}
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		if (period > 0 && now >= nextGngIrq_) {
			/* 250 Hz drives the Z80 soft-timers (ts2 RST38 INC F000..F003 ��
			   F002 unblocks boot wait @009F and paces the sequencer @0240).
			   Capcom ZN is IM1 (DI;IM 1 = ED 56). Never vector as IM2 here ?
			   an unset I register made Ay_CpuIm2Interrupt AV on ts2. CPS2
			   keeps real IM2 when the ROM programmed it. */
			if (cpu->r.iff1) {
				if (hw_->QsZn() || cpu->r.im != 2)
					Ay_CpuIm1Interrupt(cpu);
				else
					Ay_CpuIm2Interrupt(cpu, 0xff);
			}
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Sega System1/2: TIMER "soundirq" on 32V/96V/... ? 4 per frame, auto-acked.
	   The latch NMI carries the song number; the IRQ drives the sequencer. */
	if (hw_->board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 240;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Irem M72: the sample pump NMI runs at MASTER_CLOCK/8/512 = 7812.5 Hz
	   whether or not a song is playing; the YM2151 timer drives the sequencer. */
	if (hw_->board_ == CEMU_AC_BOARD_IREM_M72) {
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = ((uint64_t)cpuHz_ * 2ull) / 15625ull;
		if (period > 0 && now >= nextM72Nmi_) {
			if (m72FakeNmi_)
				hw_->M72PumpSample();
			else
				Ay_CpuNmi(cpu);
			nextM72Nmi_ = now + period;
		}
		/* MAME m72_audio_chips: the soundlatch drives rst18_w and the YM2151
		   IRQ drives rst28_w on an RST_NEG_BUFFER wired to the Z80 IRQ pin,
		   and the sound CPU runs in IM 0. The opcode presented during IACK is
		   the logical AND of the active base vectors, so latch-only lands on
		   RST 18h, YM-only on RST 28h and both together on RST 08h. Vectoring
		   these to 0038 (IM 1) hit whatever byte happened to live there. */
		hw_->TakeIrqPulse();
		const int latch = hw_->SoundCmdPending() ? 1 : 0;
		const int ymirq = (chip && chip->Irq()) ? 1 : 0;
		if ((latch || ymirq) && cpu->r.iff1) {
			unsigned vec = 0xff;
			if (latch) vec &= 0xdf; /* RST 18h */
			if (ymirq) vec &= 0xef; /* RST 28h */
			if (Ay_CpuRstInterrupt(cpu, (uint16_t)(vec & 0x38)) && ymirq)
				chip->AckIrq();
		}
		return;
	}

	/* Taito TC0140SYT / PC060HA: NMI is asserted only while a command is
	   queued and the sound CPU has enabled it (slave submode 6). */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		/* YM2610/YM2151 timer IRQ drives the sequencer. Do NOT AckIrq (same
		   as V-System): Rastan/Asuka ISR @01F9 busy-waits on status&3.
		   OPM mainloop is EI;DI ? rate-limit ForceIm1 while timer flags are set. */
		if (chip && cpu->r.im == 1) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			if (hw_->board_ == CEMU_AC_BOARD_TAITO_OPM && st) {
				const uint64_t now = (uint64_t)cpu->time();
				const uint64_t period = (uint64_t)cpuHz_ / 250;
				if (period > 0 && now >= nextGngIrq_) {
					const unsigned pc = (unsigned)cpu->r.pc;
					/* YM2203: never ForceIm1 ? nesting (even in the EI;DI
					   mainloop window) prevented 033A/0388 from finishing
					   KeyOn (ko=0, TL=7F forever). Hardware waits for EI. */
					if (cpu->r.iff1)
						Ay_CpuIm1Interrupt(cpu);
					else if (!hw_->MainIsYm2203()) {
						const int inIsr = pc >= 0x02ceu && pc < 0x033au;
						const int inSongStart = (pc >= 0x0388u && pc < 0x0500u)
							|| (pc >= 0x033au && pc < 0x0388u);
						if (!inIsr && !inSongStart)
							Ay_CpuIm1Interrupt(cpu);
					}
					nextGngIrq_ = now + period;
				}
			} else if (chip->Irq() && cpu->r.iff1) {
				Ay_CpuIm1Interrupt(cpu);
			}
		}
		return;
	}

	/* Taito SJ: latch write pulses NMI (gated by AY#4 port B bit0); the
	   sequencer runs off a 60 Hz IM1 IRQ from the video hardware. */
	if (hw_->board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 60;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Konami Scramble / Time Pilot / GX400: latch �� IRQ0 (IM1 / RST38).
	   Scramble also needs the AY timer port ticking via KonamiAyTimer().
	   Do not TakeIrqPulse until the vector is actually entered ? otherwise an
	   EI delay (irqDelay) drops the only edge and music never starts (RAM
	   stuck in the empty-channel 8001=1/8000=0 pattern �� SILENT). */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
		|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		/* Keep shared-RAM handshake released (main 68000 absent) once
		   self-test has finished writing 4000-7FFF. */
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
			const unsigned pc = (unsigned)cpu->r.pc;
			if (pc >= 0x0200u && pc < 0x8000u) {
				if (uint8_t* m = hw_->Mem())
					m[0x7ffc] = 0x04;
			}
		}
		if (hw_->IrqPulsePending()) {
			/* Hardware holds the 7474/IRQ line until the Z80 accepts it.
			   ForceIm1 requires IFF1 (same as Im1Interrupt). */
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 60;
		if (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400) {
			/* MAME gx400: screen VBLANK �� audiocpu NMI. NMI sets (7FFB)=1
			   so 0346 does not count (7FFA) to 0x34 and hang at 0361. */
			if (period > 0 && now >= nextGngIrq_) {
				Ay_CpuNmi(cpu);
				nextGngIrq_ = now + period;
			}
			return;
		}
		/* Scramble / Time Pilot: 60 Hz IM1 keeps the AY sequencer alive. */
		if (!hw_->IrqPulsePending()) {
			if (period > 0 && now >= nextGngIrq_) {
				Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
		}
		return;
	}

	/* Konami K007232-era (scontra/crimfght/twin16): latch �� IRQ0, and the
	   YM2151 timer is the sequencer timebase (same as DD2/Taito OPM). */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend) {
				const uint64_t now = (uint64_t)cpu->time();
				const uint64_t period = (uint64_t)cpuHz_ / 250;
				if (period > 0 && now >= nextGngIrq_) {
					if (cpu->r.iff1)
						Ay_CpuIm1Interrupt(cpu);
					else if (!hw_->KonamiK7232Map())
						/* map0 (scontra): EI;DI window needs ForceIm1.
						   map1 (aliens/crimfght): ForceIm1 �� FLAT KeyOn. */
						Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
		}
		return;
	}

	/* Alpha 68K-II: periodic NMI @ ~7614 Hz (MAME sound_nmi), gated by
	   YM2203 port A. Latch is polled via IN 00 from the NMI/main loop. */
	if (hw_->board_ == CEMU_AC_BOARD_ALPHA68K2) {
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 7614;
		if (period > 0 && now >= nextGngIrq_ && hw_->AlphaNmiMask()) {
			Ay_CpuNmi(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Haunted Castle: EI;DI poll @03CE + IRQ0. ISR ends in RET (iff stays
	   clear). Latch needs one ForceIm1 outside 0038; empty IRQ advances music
	   via 01BE. YM3812��NMI is RETN ? ignore OPL IRQ line (don't storm NMI). */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		const uint16_t pc = (uint16_t)cpu->r.pc;
		const int inIsr = (pc >= 0x0038 && pc < 0x00a0) ? 1 : 0;
		if (hw_->IrqPulsePending() && !inIsr) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip && chip->Irq())
			chip->AckIrq();
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		if (period > 0 && now >= nextGngIrq_ && !hw_->IrqPulsePending() && !inIsr) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Technos DD2 / Tecmo16: soundlatch �� NMI; YM2151 timer IRQ drives BGM. */
	if (hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		|| hw_->board_ == CEMU_AC_BOARD_TECMO16) {
		if (hw_->IrqPulsePending()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		if (chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend && cpu->r.iff1 && cpu->r.im == 1) {
				if (Ay_CpuIm1Interrupt(cpu)) {
					if (chip->Irq())
						chip->AckIrq();
				}
			}
		}
		return;
	}

	/* Taito flstory: soundlatch �� NMI (gated by DA00 enable); AY has no
	   timer IRQ ? soft 60 Hz IM1. MSM5232 melody advances in Render. */
	if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
		if (hw_->IrqPulsePending() && hw_->FlstoryNmiEn()) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 60;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* Nichibutsu terracre / armedf-terraf: periodic IRQ0 @ XTAL/4/512 ? 7812.5 Hz. */
	if (hw_->board_ == CEMU_AC_BOARD_TERRACRE) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 7812;
		if (period > 0 && now >= nextGngIrq_) {
			if (cpu->r.iff1)
				Ay_CpuIm1Interrupt(cpu);
			nextGngIrq_ = now + period;
		}
		return;
	}

	/* UPL robokid: latch �� IRQ0; YM2203 timer IRQ also drives the sequencer. */
	if (hw_->board_ == CEMU_AC_BOARD_ROBOKID) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip && chip->Irq() && cpu->r.iff1 && cpu->r.im == 1) {
			if (Ay_CpuIm1Interrupt(cpu))
				chip->AckIrq();
		}
		return;
	}

	/* Konami battlantis: host IRQ0 + dual YM3812 timer IRQs. */
	if (hw_->board_ == CEMU_AC_BOARD_BATTLANTIS) {
		if (hw_->IrqPulsePending()) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (chip && chip->Irq() && cpu->r.iff1 && cpu->r.im == 1) {
			if (Ay_CpuIm1Interrupt(cpu))
				chip->AckIrq();
		}
		return;
	}

	/* Konami K053260-era: latch �� IRQ0 (sound_irqtrigger). SH1��NMI wakes
	   FA00/HALT sample waits (Simpsons/Punk Shot/Escape Kids). Also feed
	   YM2151 timer IRQs when EI. Suppress IRQ while in ROM-scan (parodius). */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 3) {
		const unsigned pc = (unsigned)cpu->r.pc;
		const int romScan = (pc >= 0x06c0u && pc < 0x06f0u) ? 1 : 0;
		uint8_t* mem = hw_->Mem();
		if (hw_->KonamiSh1NmiArm() && mem && mem[pc] == 0x76) {
			hw_->ClearKonamiSh1NmiArm();
			Ay_CpuNmi(cpu);
		}
		if (!romScan && hw_->IrqPulsePending()) {
			if (cpu->r.iff1 && Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (!romScan && chip) {
			const int st = (chip->ReadStatus() & 0x03) != 0;
			const int pend = chip->Irq() || st;
			if (pend && cpu->r.iff1) {
				const uint64_t now = (uint64_t)cpu->time();
				const uint64_t period = (uint64_t)cpuHz_ / 250;
				if (period > 0 && now >= nextGngIrq_) {
					Ay_CpuIm1Interrupt(cpu);
					nextGngIrq_ = now + period;
				}
			}
		}
		return;
	}

	/* mystwarr.cpp / xexex-class K054539 boards: the sound Z80 spins with
	   interrupts off (BIT 0,(HL) / JP Z) on a RAM flag that the *NMI*
	   handler sets, and MAME's k054539_nmi_gen drives that NMI from the
	   K054539 timer gated by sound_ctrl bit 4. Feeding a maskable IM1 IRQ
	   instead left the CPU deadlocked at that loop forever. */
	if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM && hw_->PcmKind() == 4) {
		CChip* pcm = hw_->PcmChip();
		if (hw_->IrqPulsePending() && cpu->r.iff1) {
			if (Ay_CpuIm1Interrupt(cpu))
				hw_->TakeIrqPulse();
		}
		if (pcm) {
			/* Timer output is a square wave; NMI on its rising edge only. */
			const int t = pcm->Irq() ? 1 : 0;
			if (t && !k054539TimerState_)
				Ay_CpuNmi(cpu);
			k054539TimerState_ = t;
		}
		return;
	}

	/* Pending latch �� NMI (System16A / After Burner) or IM1 IRQ
	   (System16B / CPS1 / OutRun). Hold the IM1 line until IFF1 is set ?
	   never ForceIm1 under DI. After Burner latch uses NMI+RETN. */
	if (hw_->IrqPulsePending()) {
		if (hw_->board_ == CEMU_AC_BOARD_SYS16A
			|| hw_->board_ == CEMU_AC_BOARD_ABURNER
			|| hw_->board_ == CEMU_AC_BOARD_HANGON
			|| hw_->board_ == CEMU_AC_BOARD_SYS18
			|| hw_->board_ == CEMU_AC_BOARD_SYS24
			|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM
			|| hw_->board_ == CEMU_AC_BOARD_SYS32
			|| (hw_->board_ == CEMU_AC_BOARD_SNK_OPL && !hw_->SnkMapKind())) {
			hw_->TakeIrqPulse();
			Ay_CpuNmi(cpu);
		} else if (cpu->r.iff1) {
			hw_->TakeIrqPulse();
			Ay_CpuIm1Interrupt(cpu);
		}
		/* else: level stays pending until EI (Sys16B/CPS1/OutRun/�c). */
	}

	/* YM2151 timer IRQ ? CPS1.
	   Early Capcom (ghouls/dynwar�c idle EI;JR-3 @0009) and version-5
	   (megaman/sfzch: LD SP,D800) can leave timer flags sticky after Ack
	   without a rising edge ? re-arm from status on those families only.
	   Newer CPS1 (cawing/ffight) clears flags before EI; a blanket level
	   trigger re-enters until the stack explodes. */
	if (hw_->board_ == CEMU_AC_BOARD_CPS1
		&& !hw_->IrqPulsePending()
		&& chip
		&& cpu->r.iff1 && cpu->r.im == 1) {
		const int earlyCps = hw_->PeekMem(0x0009) == 0xfb
			&& hw_->PeekMem(0x000a) == 0x18
			&& hw_->PeekMem(0x000b) == 0xfd;
		const int cpsVer5 = hw_->PeekMem(0x0003) == 0x31
			&& hw_->PeekMem(0x0004) == 0x00
			&& hw_->PeekMem(0x0005) == 0xd8;
		const int ymPend = chip->Irq()
			|| ((earlyCps || cpsVer5) && ((chip->ReadStatus() & 0x03) != 0));
		if (ymPend && Ay_CpuIm1Interrupt(cpu))
			chip->AckIrq();
	}
	if (hw_->board_ == CEMU_AC_BOARD_HANGON
		&& !hw_->IrqPulsePending()
		&& chip && chip->Irq()
		&& cpu->r.iff1 && cpu->r.im == 1) {
		if (Ay_CpuIm1Interrupt(cpu))
			chip->AckIrq();
	}
	/* System16B / OutRun: YM2151 timer is a LEVEL IRQ. Deliver only while
	   IFF1 (catch EI;NOP;DI); rate-limit successful takes so sticky status
	   cannot re-enter every instruction after RETI. Never advance the period
	   while DI ? that skipped EI windows and left goldnaxe SILENT.
	   Sys16A shinobi leaves 0038 empty and polls YM status (IN A,(01);BIT0)
	   ? do not fire IM1 there. */
	if ((hw_->board_ == CEMU_AC_BOARD_SYS16B
			|| hw_->board_ == CEMU_AC_BOARD_OUTRUN)
		&& !hw_->IrqPulsePending()
		&& chip) {
		const int st = (chip->ReadStatus() & 0x03) != 0;
		const int pend = chip->Irq() || st;
		if (pend && cpu->r.iff1 && cpu->r.im == 1) {
			const uint64_t now = (uint64_t)cpu->time();
			const uint64_t period = (uint64_t)cpuHz_ / 250;
			if (period == 0 || now >= nextGngIrq_) {
				if (Ay_CpuIm1Interrupt(cpu)) {
					if (chip->Irq())
						chip->AckIrq();
					if (period > 0)
						nextGngIrq_ = now + period;
				}
			}
		}
	}
	/* V-System: MAME wires ymsnd.irq_handler() to the Z80 IRQ line, and the
	   YM2610 timer is the only timebase the sequencer has ? the soundlatch
	   NMI just queues a song number. Without this the driver booted, set up
	   both timers and then idled forever with everything keyed off.
	   Do NOT AckIrq here (same as NeoGeo): aerofgt's ISR branches Timer B
	   (status bit1) vs Timer A, and only the Timer A path increments the
	   music tick at 7804. Level-triggered re-entry after the B path clears
	   only bit1 is required so the pending A path can run; AckIrq cleared
	   the soft line early and left the sequencer stuck after the opening
	   notes. */
	if (hw_->board_ == CEMU_AC_BOARD_VSYSTEM
		&& !hw_->IrqPulsePending()
		&& chip && chip->Irq()
		&& cpu->r.iff1 && cpu->r.im == 1) {
		Ay_CpuIm1Interrupt(cpu);
	}
	/* Toaplan1: YM3812 timer IRQ is the sequencer timebase (shared-RAM
	   mailbox has no NMI). ISR @0038 polls status bit6 (Timer A) and only
	   then runs music + command poll. Soft-pulse Timer-A after Open boot so
	   mid-init EI cannot re-enter the music ISR before shared RAM is ready. */
	if (hw_->board_ == CEMU_AC_BOARD_TOAPLAN1) {
		const uint64_t now = (uint64_t)cpu->time();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		/* Idle = EI;JP self, JP self, or JR Z/$-2 wait (fshark @0400). */
		const unsigned pc = (unsigned)cpu->r.pc;
		const uint8_t b0 = hw_->PeekMem((uint16_t)pc);
		const uint8_t b1 = hw_->PeekMem((uint16_t)(pc + 1u));
		unsigned tgt = 0;
		int idle = 0;
		if (b0 == 0xfb && b1 == 0xc3) {
			tgt = (unsigned)hw_->PeekMem((uint16_t)(pc + 2u))
				| ((unsigned)hw_->PeekMem((uint16_t)(pc + 3u)) << 8);
			idle = (tgt == pc);
		} else if (b0 == 0xc3) {
			tgt = (unsigned)b1
				| ((unsigned)hw_->PeekMem((uint16_t)(pc + 2u)) << 8);
			idle = (tgt == pc);
		} else if (b0 == 0x18 && b1 == 0xfe) {
			idle = 1; /* JR $-2 */
		} else if ((b0 == 0x28 || b0 == 0x20) && b1 == 0xfe) {
			idle = 1; /* JR Z/NZ, $-2 */
		}
		/* Soft Timer-A while idle; also deliver real YM IRQ whenever EI ?
		   mid-song DI windows still rely on chip timers after RETI. */
		if (period > 0 && now >= nextGngIrq_ && idle) {
			hw_->SetToaplanTimerA(1);
			nextGngIrq_ = now + period;
			if (cpu->r.iff1 && cpu->r.im == 1)
				Ay_CpuIm1Interrupt(cpu);
		} else if (chip && (chip->Irq() || (chip->ReadStatus() & 0x80))
			&& cpu->r.iff1 && cpu->r.im == 1) {
			Ay_CpuIm1Interrupt(cpu);
		}
	}
	/* SNK68: YM3812 IRQ �� Z80 IRQ0 (music sequencer timebase).
	   Classic SNK: level IRQ while (status & 0x0B) ? cmd/YM bits (MAME). */
	if (hw_->board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (hw_->SnkMapKind()) {
			/* MAME ym*_irq_handler: ASSERT latches status bits; firmware
			   clears them via F800 (keep=data>>4). Ack the chip after latch
			   so a level line does not re-set the bit every slice. */
			if (chip && chip->Irq()) {
				hw_->SnkSetYmIrq(0, 1);
				chip->AckIrq();
			}
			if (hw_->Chip2() && hw_->Chip2()->Irq()) {
				/* Athena ISR only services status bit0 (and cmd bit3); bit1 is
				   cleared by the trailing F800 write. Mirror YM2 onto bit0 so
				   the Timer path at 0517 still sets C0A9 and advances music. */
				hw_->SnkSetYmIrq(0, 1);
				hw_->SnkSetYmIrq(1, 1);
				hw_->Chip2()->AckIrq();
			}
			/* Level IRQ0 while (status & 0x0B). Only Im1Interrupt ? ForceIm1
			   while DI (inside 04F3) nests and fills RAM with 0x39. */
			const uint64_t now = (uint64_t)cpu->time();
			const uint64_t period = (uint64_t)cpuHz_ / 250;
			if ((hw_->SnkStatus() & 0x0bu) != 0 && period > 0
				&& now >= nextGngIrq_ && cpu->r.iff1) {
				Ay_CpuIm1Interrupt(cpu);
				nextGngIrq_ = now + period;
			}
			if (hw_->IrqPulsePending())
				hw_->TakeIrqPulse();
			return;
		}
		if (!hw_->IrqPulsePending()
			&& chip && chip->Irq()
			&& cpu->r.iff1) {
			if (Ay_CpuIm1Interrupt(cpu))
				chip->AckIrq();
		}
	}
	/* Seibu: RST18 (latch) has priority over RST10 (YM3812) ? never AND the
	   IM0 vectors (that drops RST18 while the timer IRQ is live). */
	if (hw_->board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		if (chip && chip->Irq())
			hw_->SeibuSetRst10(1);
		const int r18 = hw_->SeibuRst18();
		const int r10 = hw_->SeibuRst10();
		if (cpu->r.iff1) {
			if (r18) {
				if (Ay_CpuRstInterrupt(cpu, 0x18))
					hw_->SeibuSetRst18(0);
			} else if (r10) {
				if (Ay_CpuRstInterrupt(cpu, 0x10)) {
					hw_->SeibuSetRst10(0);
					chip->AckIrq();
				}
			}
		}
		/* Main loop at 0126 waits on (201C)!=0; RST10 ISR is supposed to
		   set it each tick. If the ISR path only clears the gate, keep the
		   wait released so song updates can run (host has no VBlank). */
		if (uint8_t* m = hw_->Mem()) {
			const unsigned pc = (unsigned)cpu->r.pc;
			if (pc >= 0x0120u && pc <= 0x0130u && m[0x201c] == 0)
				m[0x201c] = 0xff;
		}
		return;
	}
}

void CDriverAc::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardAcSetActive(hw_);
	while ((uint64_t)cpu->time() < endCycle) {
		DeliverIrqs();
		/* HALT + IFF1 clear: Ay_Cpu HALT clears the run budget (s_time&=3), so
		   Open's boot settle would take minutes. Leap the clock instead. */
		if (!cpu->r.iff1 && hw_->PeekMem((uint16_t)cpu->r.pc) == 0x76) {
			const uint64_t now = (uint64_t)cpu->time();
			uint64_t step = (uint64_t)cpuHz_ / 250;
			if (step < 64) step = 64;
			if (now + step > endCycle) step = endCycle - now;
			if (step) {
				cpu->set_time((cpu_time_t)(now + step));
				hw_->AddCpuCycles(step);
				TickOpm(step);
				continue;
			}
		}
		const int cycles = Ay_CpuRunOne(cpu);
		hw_->AddCpuCycles((uint64_t)cycles);
		TickOpm((uint64_t)cycles);
	}
}

/* Mega System 1: the sound 68000's only timebase is the YM2151, so slice the
   run so its timer IRQ (and the command latch) can be sampled repeatedly ?
   Musashi clears the latched level on IACK and never re-asserts by itself. */
void CDriverAc::Ms1RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuM68kBusSetMs1(hw_);
	CChip* chip = hw_->SoundChip();
	CChip* pcm = hw_->PcmChip();
	const int gx = (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX);
	const int m68kPcm = (hw_->board_ == CEMU_AC_BOARD_M68K_PCM);
	while (cycles > 0) {
		int slice = cycles > 2000 ? 2000 : cycles;
		if (m68kPcm) hw_->M68kPcmTickVblank(slice);
		const int level = hw_->Ms1IrqLevel();
		if (level > 0) {
			m68k_set_irq(level);
			hw_->Ms1AckIrq();
		} else {
			m68k_set_irq(M68K_IRQ_NONE);
		}
		m68k_execute(slice);
		if (gx) {
			/* K054539 @ 18.432 MHz, sound 68000 @ 16 MHz. */
			const uint64_t ticks = (uint64_t)slice * 18432000ull / 16000000ull;
			if (chip) chip->AdvanceClocks(ticks);
			if (pcm) pcm->AdvanceClocks(ticks);
		} else if (m68kPcm) {
			/* The X1-010 and YMZ280B run their own sample clocks in Render. */
		} else if (chip) {
			/* YM2151 is clocked at cpuHz/2 on Mega System 1. */
			chip->AdvanceClocks((uint64_t)slice / 2u);
		}
		cycles -= slice;
	}
	m68k_set_irq(M68K_IRQ_NONE);
}

int CDriverAc::Ms1Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	const int gx = (hw_->board_ == CEMU_AC_BOARD_KONAMI_GX);
	/* Interleave CPU and mixing ? the smoke asks for whole seconds at a time
	   and rendering after the fact would freeze the chips at their end state. */
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 40) {
			if (heard_ || pinned_) {
				nextCmdAt_ = (uint64_t)~0ull;
			} else {
				TryInjectCommand();
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
			}
		}
		ms1Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		const int cycles = (int)(ms1Acc_ / (int64_t)hostRate_);
		ms1Acc_ %= (int64_t)hostRate_;
		Ms1RunCycles(cycles);
		int16_t* p = stereo + (size_t)done * 2;
		chip->Render(p, n);
		if (gx) {
			/* Dual K054539: keep each chip near full scale; MixAdd second at
			   ~3/4 so tkmmpzdm is audible without burying the first chip. */
			if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 192);
		} else if (hw_->board_ == CEMU_AC_BOARD_M68K_PCM) {
			/* chip_ is the only voice source; Render already filled p. */
		} else {
			if (hw_->Oki(0)) hw_->Oki(0)->MixAdd(p, n, 256);
			if (hw_->Oki(1)) hw_->Oki(1)->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

/* Irem M92: the V35 takes its timebase from the YM2151 (INTP0) and from the
   sound latch (INTP1), so slice finely enough that a timer edge is sampled
   before the driver's next wait loop rather than a whole frame later. */
void CDriverAc::M92RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	V35Cpu* cpu = hw_->M92Cpu();
	if (!cpu) return;
	CEmuV35BusSetM92(hw_);
	CChip* chip = hw_->SoundChip();
	while (cycles > 0) {
		const int slice = cycles > 512 ? 512 : cycles;
		hw_->M92SyncIrqs();
		{
			const int used = V35Execute(cpu, slice);
			const int step = used > 0 ? used : slice;
			hw_->AddCpuCycles((uint64_t)step);
			/* YM2151 and GA20 both sit on XTAL/4. */
			m92OpmRes_ += (uint64_t)step;
			if (chip) chip->AdvanceClocks(m92OpmRes_ / 4u);
			m92OpmRes_ %= 4u;
			cycles -= step;
		}
	}
}

int CDriverAc::M92Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		/* Periodic song retries until we hear audio (same idea as Z80 boards). */
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 40) {
			if (heard_ || pinned_) {
				nextCmdAt_ = (uint64_t)~0ull;
			} else {
				TryInjectCommand();
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
			}
		}
		m92Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(m92Acc_ / (int64_t)hostRate_);
			m92Acc_ %= (int64_t)hostRate_;
			M92RunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::DecoRunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	M6502Cpu* m6502 = hw_->DecoM6502();
	if (m6502) {
		CChip* chip = hw_->SoundChip();
		CChip* ym2203 = hw_->Chip2();
		while (cycles > 0) {
			const int slice = cycles > 512 ? 512 : cycles;
			hw_->DecoSyncIrqs();
			const int used = M6502Execute(m6502, slice);
			const int step = used > 0 ? used : slice;
			hw_->AddCpuCycles((uint64_t)step);
			/* OPL @ 3 MHz with CPU @ 1.5 �� 2x clocks; OPN shares CPU rate. */
			if (chip) chip->AdvanceClocks((uint64_t)step * 2u);
			if (ym2203) ym2203->AdvanceClocks((uint64_t)step);
			if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)step);
			cycles -= step;
		}
		return;
	}
	H6280Cpu* cpu = hw_->DecoCpu();
	if (!cpu) return;
	CEmuH6280BusSetDeco(hw_);
	CChip* chip = hw_->SoundChip();
	CChip* ym2203 = hw_->Chip2();
	/* cpuHz = XTAL/8, YM2151 = XTAL/9 �� advance YM by cycles * 8/9.
	   YM2203 / OKI1 share XTAL/8 with the CPU; OKI2 is XTAL/16 = cpu/2. */
	while (cycles > 0) {
		const int slice = cycles > 512 ? 512 : cycles;
		/* Pulse YM��IRQ2 ~250 Hz while the timer is live. Sticky level every
		   slice only cleared reg14 and starved song-slot updates (BLAST). */
		const uint64_t now = hw_->CpuCycles();
		const uint64_t period = (uint64_t)cpuHz_ / 250;
		const int ymPend = chip && (chip->Irq() || (chip->ReadStatus() & 0x03));
		if (ymPend && (period == 0 || now >= decoNextYmIrq_)) {
			hw_->DecoSyncIrqs();
			if (period > 0)
				decoNextYmIrq_ = now + period;
			/* Do not AckIrq here ? HuC6280 ISR must see timer status bits.
			   Soft line is dropped between pulses below. */
		} else {
			/* Latch/IRQ1 + MPR fix still need sync; drop YM line between pulses. */
			hw_->DecoSyncIrqs();
			H6280SetInputLine(cpu, H6280_LINE_IRQ2, H6280_CLEAR_LINE);
		}
		const int used = H6280Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		decoChipRes_ += (uint64_t)step * 8u;
		if (chip) {
			const uint64_t ym = decoChipRes_ / 9u;
			chip->AdvanceClocks(ym);
			decoChipRes_ %= 9u;
		}
		if (ym2203) ym2203->AdvanceClocks((uint64_t)step);
		if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)step);
		if (hw_->Oki(1)) hw_->Oki(1)->AdvanceClocks((uint64_t)step / 2u);
		cycles -= step;
	}
}

int CDriverAc::DecoRender(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 1) {
			/* Single inject only: repeating a BGM id clears FM channel slots. */
			TryInjectCommand();
			nextCmdAt_ = (uint64_t)~0ull;
		}
		decoAcc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(decoAcc_ / (int64_t)hostRate_);
			decoAcc_ %= (int64_t)hostRate_;
			DecoRunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->Chip2()) {
				int16_t* mix = Scratch(n);
				if (mix) {
					hw_->Chip2()->Render(mix, n);
					for (int i = 0; i < n * 2; i++) {
						int s = (int)p[i] + ((int)mix[i] * 192) / 256;
						if (s > 32767) s = 32767;
						if (s < -32768) s = -32768;
						p[i] = (int16_t)s;
					}
				}
			}
			if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 256);
			if (hw_->Oki(1)) hw_->Oki(1)->MixAdd(p, n, 192);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::NamcoM6809RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
	if (!cpu) return;
	CChip* chip = hw_->SoundChip();
	unsigned long start = cpu->cycles;
	const unsigned long target = start + (unsigned long)cycles;
	int steps = 0;
	const int kMaxSteps = cycles * 8 + 1024;
	unsigned long clocksPending = 0;
	while (cpu->cycles < target && steps < kMaxSteps) {
		hw_->NamcoM6809SyncIrqs();
		/* Keep host handshake alive while I=1 ? posting a song id into $40
		   before gaplus/grobda/motos finish their magic compares leaves the
		   sub CPU spinning at reset forever. */
		if (hw_->WsgMappy() && cpu->cc.i && (steps & 63) == 0) {
			const uint16_t pc = cpu->pc.w;
			const uint8_t o0 = hw_->NamcoM6809Read8(pc);
			const uint8_t o1 = hw_->NamcoM6809Read8((uint16_t)(pc + 1));
			const uint8_t o2 = hw_->NamcoM6809Read8((uint16_t)(pc + 2));
			const uint8_t o3 = hw_->NamcoM6809Read8((uint16_t)(pc + 3));
			const uint8_t o4 = hw_->NamcoM6809Read8((uint16_t)(pc + 4));
			const uint8_t o5 = hw_->NamcoM6809Read8((uint16_t)(pc + 5));
			const uint8_t o6 = hw_->NamcoM6809Read8((uint16_t)(pc + 6));
			/* Exact wait heads only ? STA $3000 appears in gaplus checksum/IRQ
			   and must not re-poison $40 with $11 over a live song id. */
			if (o0 == 0xdc && o1 == 0x40 && o2 == 0x10 && o3 == 0x83) {
				hw_->NamcoM6809Write8(0x0040, o4);
				hw_->NamcoM6809Write8(0x0041, o5);
			} else if (o0 == 0x9e && o1 == 0x40 && o2 == 0x8c) {
				hw_->NamcoM6809Write8(0x0040, o3);
				hw_->NamcoM6809Write8(0x0041, o4);
			} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0x81) {
				hw_->NamcoM6809Write8(0x0040, o3);
			} else if (o0 == 0x96 && o1 == 0x40 && o2 == 0xb7 && o5 == 0x81) {
				hw_->NamcoM6809Write8(0x0040, o6);
			} else if (o0 == 0x96 && o1 == 0xfb && o2 == 0x27) {
				hw_->NamcoM6809Write8(0x00fb, 0x40);
			} else if (o0 == 0xec && o1 == 0x84 && o2 == 0x26 && o3 == 0xfc) {
				hw_->NamcoM6809Write8(0x0040, 0);
				hw_->NamcoM6809Write8(0x0041, 0);
			} else if (o0 == 0x91 && o1 == 0x41 && o2 == 0x27) {
				/* phozon: CMPA $41 / BEQ ? host must change $41 from the
				   just-stored sentinel (usually $02). */
				hw_->NamcoM6809Write8(0x0041, 0);
			} else if (o0 == 0x8c && o1 == 0x00 && o2 == 0x00 && o3 == 0x26
				&& pc >= 0xe014u && pc <= 0xe019u) {
				/* liblrabl/gaplus ROM checksum CMPX #0 ? force completion. */
				if (hw_->NamcoM6809Read8(0xe01b) == 0x81
					&& hw_->NamcoM6809Read8(0xe01c) == 0xddu) {
					cpu->A = 0xddu;
					cpu->X.w = 0;
					cpu->pc.w = 0xe01bu;
				} else if (hw_->NamcoM6809Read8(0xe01a) == 0x81
					&& hw_->NamcoM6809Read8(0xe01b) == 0x00) {
					cpu->A = 0;
					cpu->X.w = 0;
					cpu->pc.w = 0xe01au;
				}
			}
		}
		const unsigned long before = cpu->cycles;
		const int rc = mc6809_step(cpu);
		steps++;
		if (rc != 0) break;
		if (cpu->cycles == before)
			cpu->cycles++; /* CWAI/SYNC safety */
		clocksPending += (unsigned long)(cpu->cycles - before);
		/* Tick YM often enough for timer��FIRQ; bulk-at-end starved BGM. */
		if (clocksPending >= 64u) {
			if (chip) chip->AdvanceClocks((uint64_t)clocksPending);
			if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)clocksPending);
			clocksPending = 0;
		}
		/* Keep S inside work RAM ? Sys1/2 ROMs never LDS.
		   Mappy-era WSG: LDS #$0400. Keep S in $03C0-$0400 so IRQ frames
		   stay above digdug2/toypop channel blocks. */
		if (hw_->WsgMappy()) {
			if (cpu->S.w < 0x03c0u || cpu->S.w > 0x0400u)
				cpu->S.w = 0x03f0;
		} else if (cpu->S.w < 0x8000u || cpu->S.w > 0x9fffu) {
			cpu->S.w = 0x9ff0;
		}
	}
	if (clocksPending) {
		if (chip) chip->AdvanceClocks((uint64_t)clocksPending);
		if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)clocksPending);
	}
	const int stepped = (int)(cpu->cycles - start);
	if (stepped > 0)
		hw_->AddCpuCycles((uint64_t)stepped);
}

int CDriverAc::NamcoM6809Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_) {
			if (hw_->board_ == CEMU_AC_BOARD_NAMCO_WSG && hw_->WsgMappy()) {
				mc6809__t* cpu = (mc6809__t*)hw_->NamcoM6809Cpu();
				/* Open may defer inject until CLI ? post BGM on first clear-I. */
				if (cpu && !cpu->cc.i && cmdIndex_ < 1)
					TryInjectCommand();
				else if (hw_->NamcoM6809Read8(0x0040) == 0
					&& hw_->NamcoM6809Read8(0x0060) == 0)
					TryInjectCommand();
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 60;
			} else if (cmdIndex_ < 1) {
				TryInjectCommand();
				nextCmdAt_ = (uint64_t)~0ull;
			}
		}
		namcoAcc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(namcoAcc_ / (int64_t)hostRate_);
			namcoAcc_ %= (int64_t)hostRate_;
			NamcoM6809RunCycles(cycles > 4096 ? 4096 : cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->PcmChip())
				hw_->PcmChip()->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::M62RunCycles(int cycles)
{
	if (!hw_ || !hw_->M6803Cpu() || cycles <= 0) return;
	struct m6800* cpu = hw_->M6803Cpu();
	CChip* chip = hw_->SoundChip();
	int left = cycles;
	int guard = cycles * 8 + 256;
	/* Drop OCF/TOF/SCI/ICF ? those vectors stub to RST on M62 ROMs.
	   MSM5205 VCK��NMI ~4 kHz: accumulate by *executed* cycles only. */
	static uint64_t s_nmiAcc;
	while (left > 0 && guard-- > 0) {
		cpu->irq &= (IRQ_NMI | IRQ_IRQ1);
		const int used = m6800_execute(cpu);
		const int step = used > 0 ? used : 1;
		left -= step;
		hw_->AddCpuCycles((uint64_t)step);
		if (chip) chip->AdvanceClocks((uint64_t)step);
		if (hw_->Chip2()) hw_->Chip2()->AdvanceClocks((uint64_t)step);
		s_nmiAcc += (uint64_t)step;
		if (s_nmiAcc >= (uint64_t)(hw_->cpuHz_ / 4000 + 1)) {
			s_nmiAcc = 0;
			m6800_raise_interrupt(cpu, IRQ_NMI);
		}
	}
}

int CDriverAc::M62Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		/* While still silent, re-seed song mailboxes + latch (same catalog
		   id only ? main-CPU style retry, not a try-table hunt). */
		if (!heard_ && hw_->CpuCycles() < (uint64_t)cpuHz_ * 3ull) {
			uint8_t song = songCmd_ ? (uint8_t)(songCmd_ & 0x7fu) : 0x20;
			if (!song) song = 0x20;
			struct m6800* cpu = hw_->M6803Cpu();
			if (cpu) {
				const uint8_t slots[] = { 0xbc, 0xc6, 0xc7, 0xcc, 0 };
				for (int i = 0; slots[i]; i++) {
					const uint8_t a = slots[i];
					if (cpu->iram_base <= a && a <= 0xff)
						cpu->iram[a - cpu->iram_base] = song;
				}
			}
			if ((hw_->CpuCycles() % (uint64_t)(cpuHz_ / 4 + 1)) < 256u) {
				hw_->SetSoundCommand(song);
			} else if (hw_->SoundCommand() && !(hw_->SoundCommand() & 0x80)) {
				hw_->SetSoundCommand(0x80);
			}
		} else if (hw_->SoundCommand() && !(hw_->SoundCommand() & 0x80)
			&& hw_->CpuCycles() > (uint64_t)cpuHz_ / 8) {
			hw_->SetSoundCommand(0x80);
		}
		m62Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(m62Acc_ / (int64_t)hostRate_);
			m62Acc_ %= (int64_t)hostRate_;
			M62RunCycles(cycles > 8192 ? 8192 : cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->Chip2()) {
				int16_t* mix = Scratch(n);
				if (mix) {
					hw_->Chip2()->Render(mix, n);
					for (int i = 0; i < n * 2; i++) {
						int s = (int)p[i] + (int)mix[i];
						if (s > 32767) s = 32767;
						if (s < -32768) s = -32768;
						p[i] = (int16_t)s;
					}
				}
			}
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::Sega68RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuM68kBusSetMs1(hw_);
	CChip* chip = hw_->SoundChip();
	CChip* pcm = hw_->PcmChip();
	CChip* pcm2 = hw_->Oki(1);
	while (cycles > 0) {
		int slice = cycles > 2000 ? 2000 : cycles;
		const int level = hw_->Ms1IrqLevel();
		if (level > 0)
			m68k_set_irq(level);
		else
			m68k_set_irq(M68K_IRQ_NONE);
		m68k_execute(slice);
		hw_->AddCpuCycles((uint64_t)slice);
		if (chip) chip->AdvanceClocks((uint64_t)slice);
		if (pcm) pcm->AdvanceClocks((uint64_t)slice);
		if (pcm2) pcm2->AdvanceClocks((uint64_t)slice);
		cycles -= slice;
	}
}

int CDriverAc::Sega68Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!stereo || frames <= 0) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		if (nextCmdAt_ != (uint64_t)~0ull
			&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 4) {
			TryInjectCommand();
			if (cmdIndex_ < 4)
				nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 4;
			else
				nextCmdAt_ = (uint64_t)~0ull;
		}
		sega68Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(sega68Acc_ / (int64_t)hostRate_);
			sega68Acc_ %= (int64_t)hostRate_;
			Sega68RunCycles(cycles > 8192 ? 8192 : cycles);
		}
		int16_t* p = stereo + (size_t)done * 2;
		memset(p, 0, (size_t)n * 2 * sizeof(int16_t));
		/* MAME segam1audio: MultiPCM route 0.5 each, YM3438 0.30.
		   Old gain 768 (3.0x) was compensating for a dead 68K image and
		   rail-clipped once firmware actually played. */
		if (hw_->PcmChip()) hw_->PcmChip()->MixAdd(p, n, 128);
		if (hw_->Oki(1)) hw_->Oki(1)->MixAdd(p, n, 128);
		if (chip) {
			int16_t* mix = Scratch(n);
			if (mix) {
				chip->Render(mix, n);
				for (int i = 0; i < n * 2; i++) {
					int s = (int)p[i] + ((int)mix[i] * 77) / 256;
					if (s > 32767) s = 32767;
					if (s < -32768) s = -32768;
					p[i] = (int16_t)s;
				}
			}
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::H8RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	H8Cpu* cpu = hw_->H8CpuPtr();
	if (!cpu) return;
	CEmuH8BusSetAc(hw_);
	CChip* chip = hw_->SoundChip();
	while (cycles > 0) {
		const int slice = cycles > 1024 ? 1024 : cycles;
		/* Keep Sys12/ND-1 busy flag clear so the main loop can run. */
		if (hw_->H8Shared()) {
			const_cast<uint8_t*>(hw_->H8Shared())[0x4050] = 0;
		}
		const int used = H8Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		/* MAME namcos12_sub_irq: screen vblank drives external IRQ1.
		   IRQ5 selects the wrong H8 vector and makes the C76 sequencer run
		   from an unrelated handler.  One request per vblank is sufficient
		   for this detached core (external requests are edge-latched). */
		if ((hw_->CpuCycles() / (uint64_t)(hw_->cpuHz_ / 60 + 1))
			!= ((hw_->CpuCycles() - (uint64_t)step) / (uint64_t)(hw_->cpuHz_ / 60 + 1)))
			H8SetInputLine(cpu, H8_LINE_IRQ1, H8_ASSERT_LINE);
		(void)chip;
		cycles -= step;
	}
}

int CDriverAc::H8Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		h8Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(h8Acc_ / (int64_t)hostRate_);
			h8Acc_ %= (int64_t)hostRate_;
			H8RunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

void CDriverAc::M37702RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	M37702Cpu* cpu = hw_->M37702CpuPtr();
	if (!cpu) return;
	CEmuM37702BusSetAc(hw_);
	CChip* chip = hw_->SoundChip();
	while (cycles > 0) {
		const int slice = cycles > 1024 ? 1024 : cycles;
		const int used = M37702Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		/* ~60 Hz IRQ0 / IRQ2 (Sys11 / NA1 host tick). */
		if ((hw_->CpuCycles() / (uint64_t)(hw_->cpuHz_ / 60 + 1))
			!= ((hw_->CpuCycles() - (uint64_t)step) / (uint64_t)(hw_->cpuHz_ / 60 + 1))) {
			M37702SetInputLine(cpu, M37710_LINE_IRQ0, M37702_HOLD_LINE);
			M37702SetInputLine(cpu, M37710_LINE_IRQ2, M37702_HOLD_LINE);
		}
		if (chip) chip->AdvanceClocks((uint64_t)step);
		cycles -= step;
	}
}

int CDriverAc::M37702Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
		m37702Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(m37702Acc_ / (int64_t)hostRate_);
			m37702Acc_ %= (int64_t)hostRate_;
			M37702RunCycles(cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}


void CDriverAc::Sys86RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	HD63701Cpu* cpu = hw_->HD63701CpuPtr();
	if (!cpu) return;
	CEmuHD63701BusSetAc(hw_);
	CChip* chip = hw_->SoundChip();
	/* FBNeo: HD63701SetIRQLine HOLD once per frame. CUS60/game IRQ at [AE+8]
	   runs the AE+20..+28 music chain ONLY while $1182==$A6; otherwise it
	   RTIs after AA/+2C. F4B1 clears A6 on song start and SEIs ? after B0
	   is live, re-assert A6 (genpeitd external loop does not) and pulse IRQ.
	   Do NOT IRQ while A6 is set and B0==0 (song-start window): that races
	   F4B1 and AE+1A can CLR $B0. */
	while (cycles > 0) {
		const int slice = cycles > 1024 ? 1024 : cycles;
		const int used = HD63701Execute(cpu, slice);
		const int step = used > 0 ? used : slice;
		hw_->AddCpuCycles((uint64_t)step);
		{
			const uint8_t b0 = hw_->HD63701Read8(0x00b0);
			const uint8_t door = hw_->HD63701Read8(0x1182);
			if (b0 && door != 0xa6u)
				hw_->HD63701Write8(0x1182, 0xa6u);
			const int frameEdge = ((hw_->CpuCycles() / (uint64_t)(hw_->cpuHz_ / 60 + 1))
				!= ((hw_->CpuCycles() - (uint64_t)step) / (uint64_t)(hw_->cpuHz_ / 60 + 1)));
			if (frameEdge && b0) {
				HD63701ClearInterruptMask(cpu);
				HD63701SetInputLine(cpu, HD63701_LINE_IRQ, HD63701_HOLD_LINE);
				{
					const int irqStep = HD63701Execute(cpu, 256);
					const int irqUsed = irqStep > 0 ? irqStep : 256;
					hw_->AddCpuCycles((uint64_t)irqUsed);
					if (chip) chip->AdvanceClocks((uint64_t)irqUsed);
					if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)irqUsed);
					cycles -= irqUsed;
				}
				HD63701SetInputLine(cpu, HD63701_LINE_IRQ, HD63701_CLEAR_LINE);
			}
		}
		if (chip) chip->AdvanceClocks((uint64_t)step);
		if (hw_->PcmChip()) hw_->PcmChip()->AdvanceClocks((uint64_t)step);
		cycles -= step;
	}
}

int CDriverAc::Sys86Render(int16_t* stereo, int frames)
{
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	enum { kChunk = 64 };
	for (int done = 0; done < frames; ) {
		int n = frames - done;
		if (n > (int)kChunk) n = (int)kChunk;
	/* Sys86: one-shot at Open ? do not re-inject (sticky $1182 kills KeyOn). */
	if (nextCmdAt_ != (uint64_t)~0ull
		&& hw_->CpuCycles() >= nextCmdAt_ && cmdIndex_ < 1
		&& hw_->board_ != CEMU_AC_BOARD_NAMCO_SYS86) {
			TryInjectCommand();
			nextCmdAt_ = hw_->CpuCycles() + (uint64_t)cpuHz_ / 2;
		}
		sys86Acc_ += (int64_t)cpuHz_ * (int64_t)n;
		{
			const int cycles = (int)(sys86Acc_ / (int64_t)hostRate_);
			sys86Acc_ %= (int64_t)hostRate_;
			Sys86RunCycles(cycles > 4096 ? 4096 : cycles);
		}
		{
			int16_t* p = stereo + (size_t)done * 2;
			chip->Render(p, n);
			if (hw_->PcmChip())
				hw_->PcmChip()->MixAdd(p, n, 256);
		}
		done += n;
	}
	for (int i = 0; !heard_ && i < frames * 2; i++)
		if (stereo[i]) heard_ = 1;
	return frames;
}

int CDriverAc::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	if (h8Board_) return H8Render(stereo, frames);
	if (m37702Board_) return M37702Render(stereo, frames);
	if (deco_) return DecoRender(stereo, frames);
	if (namcoM6809_) return NamcoM6809Render(stereo, frames);
	if (sys86_) return Sys86Render(stereo, frames);
	if (m62_) return M62Render(stereo, frames);
	if (sega68_) return Sega68Render(stereo, frames);
	if (m92_) return M92Render(stereo, frames);
	if (ms1_) return Ms1Render(stereo, frames);
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardAcSetActive(hw_);

	if (hostRate_ < 1 || cpuHz_ < 1) return 0;

	if (!hasCpu_) {
		/* No sound-CPU core for this board ? render the chips as configured
		   (silent unless something else has driven them). */
		chip->Render(stereo, frames);
		if (hw_->Chip2()) {
			int16_t* mix2 = Scratch(frames);
			if (mix2) {
				hw_->Chip2()->Render(mix2, frames);
				for (int i = 0; i < frames * 2; i++) {
					int s = (int)stereo[i] + (int)mix2[i];
					if (s > 32767) s = 32767;
					if (s < -32768) s = -32768;
					stereo[i] = (int16_t)s;
				}
			}
		}
		if (hw_->PcmChip())
			hw_->PcmChip()->MixAdd(stereo, frames, 256);
		return frames;
	}

	/* Aux voice chips (GNG dual YM2203, System1 dual SN, Taito SJ triple AY)
	   have no MixAdd, so render them into scratch and sum afterwards. */
	CChip* pcm = hw_->PcmChip();
	/* mystwarr-class boards carry a second K054539 in the same Z80 map.
	   Must be the K054539-specific accessor: pcm2_ is also used by other
	   boards (MegaSystem1/DECO second OKI) whose chips are driven elsewhere,
	   and mixing those here faulted on 57 archives. */
	CChip* pcm2 = hw_->KonamiPcm2();
	const int auxCount = (hw_->Chip3() ? 2 : (hw_->Chip2() ? 1 : 0));
	int16_t* mix2 = auxCount ? Scratch(frames * auxCount) : NULL;
	int16_t* mix3 = (mix2 && auxCount > 1) ? mix2 + (size_t)frames * 2 : NULL;

	for (int i = 0; i < frames; i++) {
		const uint64_t now = (uint64_t)cpu->time();
		/* Re-try song commands every ~0.25s until table exhausted (Sys16/CPS�c).
		   After Burner / pinned playlist title: inject once only ? re-sending
		   restarts BGM and overrides the selected track with try-table[0]. */
		if (now >= nextCmdAt_) {
			if (hw_->board_ == CEMU_AC_BOARD_ABURNER) {
				if (cmdIndex_ == 0)
					TryInjectCommand();
				nextCmdAt_ = (uint64_t)~0ull;
			} else if (pinned_) {
				if (cmdIndex_ == 0) {
					TryInjectCommand();
					const int fast = (hw_->board_ == CEMU_AC_BOARD_IREM_M72
						|| hw_->board_ == CEMU_AC_BOARD_SYS16B
						|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM
						|| hw_->board_ == CEMU_AC_BOARD_GNG
						|| (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM
							&& (hw_->PcmKind() == 3 || hw_->PcmKind() == 4))
						|| hw_->board_ == CEMU_AC_BOARD_TAITO_OPM
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_GX400
						|| hw_->board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_K7232
						|| hw_->board_ == CEMU_AC_BOARD_KONAMI_HCASTLE
						|| hw_->board_ == CEMU_AC_BOARD_ALPHA68K2
						|| hw_->board_ == CEMU_AC_BOARD_TECMO16
						|| hw_->board_ == CEMU_AC_BOARD_FLSTORY
						|| hw_->board_ == CEMU_AC_BOARD_TERRACRE
						|| hw_->board_ == CEMU_AC_BOARD_ROBOKID
						|| hw_->board_ == CEMU_AC_BOARD_BATTLANTIS);
					nextCmdAt_ = now + (uint64_t)cpuHz_ * (fast ? 1ull : 3ull)
						/ ((hw_->board_ == CEMU_AC_BOARD_IREM_M72
							|| hw_->board_ == CEMU_AC_BOARD_SYS16B
							|| hw_->board_ == CEMU_AC_BOARD_VSYSTEM) ? 2ull : 1ull);
				} else if (hw_->board_ == CEMU_AC_BOARD_FLSTORY) {
					/* MSM phrases decay inside one probe second ? re-arm the
					   same BGM so all four classify chunks stay above PEAK_MIN. */
					cmdIndex_ = 0;
					TryInjectCommand();
					nextCmdAt_ = now + (uint64_t)cpuHz_ * 4ull / 5ull;
				} else if (hw_->board_ == CEMU_AC_BOARD_KONAMI_PCM
					&& (hw_->PcmKind() == 3 || hw_->PcmKind() == 4)) {
					/* K054539: re-poke latch (DI can drop the first IRQ).
					   K053260: post the song a few times, clear the latch,
					   then only pulse IRQ0 so the sequencer ticks. */
					if (hw_->PcmKind() == 3) {
						const uint16_t w = songCmdWord_ ? songCmdWord_
							: (uint16_t)(songCmd_ ? songCmd_ : 0x80);
						/* DI + FA00/HALT / YM-timer waits: empty IRQ0 cannot
						   recover. Re-arm the same song ~2 Hz so short phrases
						   (qgakumon/vendetta) do not leave a silent probe chunk. */
						if ((cmdIndex_ % 30) == 0) {
							if (w > 0xffu)
								hw_->SetSoundCommandWord(w);
							else
								hw_->SetSoundCommand((uint8_t)w);
						} else {
							hw_->PulseIrq();
						}
						cmdIndex_++;
						nextCmdAt_ = now + (uint64_t)cpuHz_ / 60;
					} else if (cmdIndex_ < 40) {
						TryInjectCommand();
						nextCmdAt_ = now + (uint64_t)cpuHz_ / 4;
					} else {
						nextCmdAt_ = (uint64_t)~0ull;
					}
				} else if (hw_->board_ == CEMU_AC_BOARD_GNG) {
					/* Stop once audible ? further injects stomp real BGM into
					   AUDITION/clip. Unpinned Open() already picked a sustain. */
					if (!heard_ && cmdIndex_ < 40) {
						TryInjectCommand();
						nextCmdAt_ = now + (uint64_t)cpuHz_ / 4;
					} else {
						nextCmdAt_ = (uint64_t)~0ull;
					}
				} else {
					/* Keep the selected catalog/playlist command. Never hunt the
					   try table ? that restarted BGM as the first try code (sfa
					   song2��song1, same pattern on CPS/QSound/Taito/�c). */
					nextCmdAt_ = (uint64_t)~0ull;
				}
			} else if (cmdIndex_ == 0) {
				/* Unpinned: inject catalog/default once ? no try-table walk. */
				TryInjectCommand();
				nextCmdAt_ = (uint64_t)~0ull;
			} else {
				nextCmdAt_ = (uint64_t)~0ull;
			}
		}
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		const uint64_t end = now + (uint64_t)cyclesPerSample;
		while ((uint64_t)cpu->time() < end) {
			DeliverIrqs();
			const int cycles = Ay_CpuRunOne(cpu);
			hw_->AddCpuCycles((uint64_t)cycles);
			TickOpm((uint64_t)cycles);
		}
		if (hw_->board_ == CEMU_AC_BOARD_CPS_QS) {
			/* QSound's signed 8-bit source path is noticeably below the FM
			   boards. Apply a modest +1.9 dB at the board mixer, leaving the
			   chip core's standalone Render contract unchanged. */
			stereo[i * 2] = stereo[i * 2 + 1] = 0;
			chip->MixAdd(stereo + i * 2, 1, 320);
		} else {
			chip->Render(stereo + i * 2, 1);
		}
		/* Per-frame so the silence watchdog below sees PCM-only boards too;
		   every MixAdd is a plain per-frame loop, so this is equivalent. */
		if (pcm)
			pcm->MixAdd(stereo + i * 2, 1, 256);
		if (pcm2)
			pcm2->MixAdd(stereo + i * 2, 1, 256);
		if (mix2)
			hw_->Chip2()->Render(mix2 + i * 2, 1);
		if (mix3)
			hw_->Chip3()->Render(mix3 + i * 2, 1);
		if (!heard_ && (stereo[i * 2] || stereo[i * 2 + 1]
			|| (mix2 && (mix2[i * 2] || mix2[i * 2 + 1]))
			|| (mix3 && (mix3[i * 2] || mix3[i * 2 + 1]))))
			heard_ = 1;
	}

	if (mix2 && hw_->board_ == CEMU_AC_BOARD_GNG) {
		for (int i = 0; i < frames * 2; i++) {
			/* Dual YM2203 ? average to avoid hard clip. */
			int s = ((int)stereo[i] + (int)mix2[i]) / 2;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			stereo[i] = (int16_t)s;
		}
	} else if (mix2) {
		/* SN76489 / AY voices are individually quiet; sum with clamp so a
		   single active chip keeps full level (MAME routes them at ~0.5 each). */
		for (int i = 0; i < frames * 2; i++) {
			int s = (int)stereo[i] + (int)mix2[i] + (mix3 ? (int)mix3[i] : 0);
			s = s * 2 / 3;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			stereo[i] = (int16_t)s;
		}
	}
	return frames;
}

int CDriverAc::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
