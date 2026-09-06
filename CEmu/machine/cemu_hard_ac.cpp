#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_ac_board_spec.h"
#include "cemu_m68k_bus.h"
#include "cemu_v35_bus.h"
#include "cemu_h6280_bus.h"
#include "cemu_h8_bus.h"
#include "cemu_m37702_bus.h"
#include "cemu_hd63701_bus.h"
#include "cemu_irem_cpu_tables.h"
extern "C" {
#include "../vendor/m6502/m6502core.h"
}
#include "../z80/cemu_z80_bus.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_segapcm.h"
#include "../chip/cemu_chip_oki6295.h"
#include "../chip/cemu_chip_qsound.h"
#include "../chip/cemu_chip_k053260.h"
#include "../chip/cemu_chip_k054539.h"
#include "../chip/cemu_chip_c352.h"
#include "../chip/cemu_chip_c140.h"
#include "../chip/cemu_chip_c30.h"
#include "../chip/cemu_chip_rf5c68.h"
#include "../chip/cemu_chip_ym2612.h"
#include "../chip/cemu_chip_ym2610.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../chip/cemu_chip_ay.h"
#include "../chip/cemu_chip_opl.h"
#include "../chip/cemu_chip_msm5232.h"
#include "../chip/cemu_chip_ga20.h"
#include "../chip/cemu_chip_irem_dac.h"
#include "cemu_sei80bu.h"
#include "../chip/cemu_chip_scsp.h"
#include "../chip/cemu_chip_rf5c400.h"
#include "cemu_kabuki.h"
#include "../chip/cemu_chip_multipcm.h"
#include "../s98/device/emu2413/emu2413.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
#include "../vendor/v35/v35core.h"
#include "../vendor/h6280/h6280core.h"
#include "../vendor/h8/h8core.h"
#include "../vendor/m37710/m37702core.h"
#include "../vendor/hd63701/hd63701core.h"
	#include "../vendor/mc6809/mc6809.h"
	#include "../vendor/m6803/m6803.h"
}
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

static unsigned CEmuAcOptionValue(const CEmuGameEntry* ge, const char* name, unsigned dflt);

static mc6809__t* NamcoCpuRaw(struct mc6809* p) { return (mc6809__t*)p; }

CHardAc::CHardAc()
	: board_(CEMU_AC_BOARD_UNKNOWN)
	, cpuHz_(4000000)
	, opmHz_(4000000)
	, opmWrites_(0)
	, cpu_(NULL)
	, chip_(NULL)
	, chip2_(NULL)
	, chip3_(NULL)
	, pcm_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, soundCmd_(0)
	, soundCmdWord_(0)
	, soundCmdPending_(0)
	, irqPulse_(0)
	, wsgNmiEnable_(0)
	, wsgMappy_(0)
	, wsg63701_(0)
	, qsZn_(0)
	, qsKabuki_(0)
	, qsKabukiData_(NULL)
	, znQueueLen_(0)
	, znQueuePos_(0)
	, znDeferredNmi_(0)
	, ymAddr_(0)
	, gngCommandoMap_(0)
	, gngGaidenMap_(0)
	, abStatusPulseSlot_(~0ull)
	, hangYmAddr_(0)
	, pcm2_(NULL)
	, pcmRom_(NULL)
	, pcmRomSize_(0)
	, pcmRom2_(NULL)
	, pcmRom2Size_(0)
	, pcmKind_(0)
	, auxKind_(0)
	, mainIsYm2203_(0)
	, mainIsYm2610_(0)
	, mainIsYm2612_(0)
	, soundRom_(NULL)
	, soundRomSize_(0)
	, bank_(0)
	, bankLoaded_(0)
	, bankBase_(0x4000u)
	, bankSize_(0x4000u)
	, konamiOpmAddr_(0xf000u)
	, konamiPcmAddr_(0xe800u)
	, konamiBankAddr_(0)
	, konamiPcmWindow_(0x40u)
	, konamiSh1NmiArm_(0)
	, ms1Rom_(NULL)
	, ms1RomSize_(0)
	, ms1Ram_(NULL)
	, ms1LatchLevel_(4)
	, ms1LatchIrq_(0)
	, ms1LatchIn_(0)
	, ms1LatchOut_(0)
	, ms1OkiWrites_(0)
	, ms1LatchReads_(0)
	, h6280_(NULL)
	, m6502_(NULL)
	, decoRom_(NULL)
	, decoRomSize_(0)
	, decoRam_(NULL)
	, decoYm2203Addr_(0)
	, decoYm2151Addr_(0)
	, decoLatchReads_(0)
	, decoOkiWrites_(0)
	, decoChanWrites_(0)
	, v35_(NULL)
	, m92Rom_(NULL)
	, m92RomSize_(0)
	, m92Ram_(NULL)
	, m92Latch_(0)
	, m92LatchPending_(0)
	, m92Latch2_(0)
	, m92LatchReads_(0)
	, m92Ga20Writes_(0)
	, m92BomberGatePatch_(0)
	, m92EncryptedRet_(0x14)
	, m92DecryptValid_(0)
	, m92SongCmdBase_(0)
	, m92ChannelBgm_(0)
	, m92WordQueue_(0)
	, m92ReadyWait_(0)
	, h8_(NULL)
	, m37702_(NULL)
	, h8Rom_(NULL)
	, h8RomSize_(0)
	, m37702IntRom_(NULL)
	, m37702IntRomSize_(0)
	, h8Shared_(NULL)
	, m37702LocalRam_(NULL)
	, h8MapKind_(0)
	, m37702MapKind_(0)
	, h8WordSwap_(0)
	, h8C352Writes_(0)
	, h8C352Hi_(0)
	, h8C352HiValid_(0)
	, m37702Soft_(0)
	, m37702C140_(0)
	, snkMapKind_(0)
	, snkStatus_(0)
	, terracreMap_(0)
	, flstoryNmiEn_(0)
	, hd63701_(NULL)
	, hd63701Rom_(NULL)
	, hd63701RomSize_(0)
	, hd63701MapKind_(0)
	, hd63701YmBase_(0x2000)
	, hd63701YmWrites_(0)
	, namcoM6809_(NULL)
	, namcoBank_(0)
	, namcoYmAddr_(0)
	, namcoIrqAssert_(0)
	, namcoFirqAssert_(0)
	, namcoNextVblank_(0)
	, namcoMailOff_(0x100)
	, decoCpuKind_(0)
	, k056800IntEn_(0)
	, k056800Pending_(0)
	, k056800Irq_(0)
	, gxSoundCtrl_(0)
	, gxSoundIntck_(0)
	, gxPcmWrites_(0)
	, gxTmsStatus_(0x07)
	, sytMainMode_(0)
	, sytSubMode_(0)
	, sytStatus_(0)
	, sytNmiEnabled_(0)
	, m72SampleAddr_(0)
	, m72SoundRam_(0)
	, m72IoAlt_(0)
	, sys16RomBoard_(0x5797u)
	, vsIoKind_(0)
	, konamiK7232Map_(0)
	, alphaOpll_(NULL)
	, alphaYmAddr_(0)
	, alphaOpllAddr_(0)
	, alphaNmiMask_(1)
	, alphaPaLatch_(0)
	, sjLatchFlag_(0)
	, sjSemaphore2_(0)
	, sjNmiMask_(0)
	, sjNmiMaskSeen_(0)
	, toaplanTimerA_(0)
	, toaplanYmPort_(0)
	, seibuEnc_(0)
	, seibuBank_(0)
	, seibuSongOr80_(0)
	, seibuMainPending_(0)
	, seibuSubPending_(0)
	, seibuRst10_(0)
	, seibuRst18_(0)
	, m6803_(NULL)
	, m62Port1_(0)
	, m62Port2_(0)
	, m62AyMAddr_(0)
	, m62BusMask_(0xffffu)
	, m62MsmReset_(1)
	, segaM1Audio_(0)
	, segaMidiHead_(0)
	, segaMidiTail_(0)
	, segaMidiIrq_(0)
{
	hardKind = KIND_AC;
	memset(mem_, 0, sizeof(mem_));
	memset(gngYmAddr_, 0, sizeof(gngYmAddr_));
	memset(sytSlaveData_, 0, sizeof(sytSlaveData_));
	memset(sytMasterData_, 0, sizeof(sytMasterData_));
	memset(ayAddr_, 0, sizeof(ayAddr_));
	memset(k056800Host_, 0, sizeof(k056800Host_));
	memset(k056800Snd_, 0, sizeof(k056800Snd_));
	memset(seibuMain2Sub_, 0, sizeof(seibuMain2Sub_));
	memset(seibuSub2Main_, 0, sizeof(seibuSub2Main_));
	memset(segaMidiFifo_, 0, sizeof(segaMidiFifo_));
}

CHardAc::~CHardAc()
{
	Shutdown();
}

static int IsAcPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->subtype, "sg1000") == 0 || _stricmp(ge->dataDir, "sc3000") == 0)
		return 0;
	if (_stricmp(ge->platform, "megadrive") == 0 || _stricmp(ge->dataDir, "megadrive") == 0)
		return 0;
	if (_strnicmp(ge->platform, "capcom", 6) == 0) return 1;
	if (_stricmp(ge->platform, "sega") == 0) return 1;
	if (_stricmp(ge->platform, "namco") == 0) return 1;
	if (_strnicmp(ge->platform, "konami", 6) == 0) return 1;
	if (_stricmp(ge->platform, "taito") == 0) return 1;
	if (_stricmp(ge->platform, "irem") == 0) return 1;
	if (_stricmp(ge->platform, "dataeast") == 0) return 1;
	if (_stricmp(ge->dataDir, "ac") == 0) return 1;
	if (_stricmp(ge->platform, "videosystem") == 0) return 1;
	/* NeoGeo uses CHardNeo — never open as AC board=UNKNOWN (board=?/0). */
	if (_stricmp(ge->platform, "neogeo") == 0 || _stricmp(ge->subtype, "neogeo") == 0)
		return 0;
	if (_stricmp(ge->platform, "snk") == 0
		&& (_stricmp(ge->subtype, "generic") == 0 || ge->subtype[0] == 0)) {
		for (int i = 0; i < ge->chipCount; i++)
			if (ge->chipIds[i] == CEMU_CHIP_YM2610)
				return 0;
	}
	static const char* const kPlatforms[] = {
		"jaleco", "technos", "toaplan", "snk", "nichibutsu",
		"seibu", "tecmo", "banpresto", "cave", "psikyo", "nmk",
		"tehkan", "upl", "alpha", "yunsung", "athena", "atlus", "kaneko",
		"raizing", "eighting", "allumer", "atari", "bootleg", "deniam",
		"mitchell", "seta", "fuuki", "dooyong", "tatsumi", "tad", "marble",
		"technosoft", "easttechnology", "universal", "nintendo", "sunsoft",
		"success", "f2system"
	};
	for (int i = 0; i < (int)_countof(kPlatforms); i++)
		if (_stricmp(ge->platform, kPlatforms[i]) == 0)
			return 1;
	if (_stricmp(ge->subtype, "sharrier") == 0 || _stricmp(ge->subtype, "hangon") == 0
		|| _stricmp(ge->subtype, "toutrun") == 0 || _stricmp(ge->subtype, "aerofgt") == 0
		|| _strnicmp(ge->subtype, "system16", 8) == 0
		|| _strnicmp(ge->subtype, "system18", 8) == 0
		|| _strnicmp(ge->subtype, "system24", 8) == 0
		|| _strnicmp(ge->subtype, "system32", 8) == 0
		|| _stricmp(ge->subtype, "aburner") == 0 || _stricmp(ge->subtype, "outrun") == 0
		|| _strnicmp(ge->subtype, "cps", 3) == 0
		|| _stricmp(ge->subtype, "m72") == 0 || _stricmp(ge->subtype, "m92") == 0
		|| _stricmp(ge->subtype, "m62") == 0 || _stricmp(ge->subtype, "megasys1") == 0)
		return 1;
	return 0;
}

/* Data East HuC6280 + YM2151 (+ OKI) — cninja / thndzone / deco32 class.
   Older M6502/R65C02 boards are NOT listed here (see CEmuAcIsDecoM6502Sub). */
static int CEmuAcIsDecoH6280Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	static const char* const kSubs[] = {
		"deco32", "thndzone", "supbtime", "cninja", "decomlc",
		"nslasher", "midres"
	};
	for (int i = 0; i < (int)_countof(kSubs); i++)
		if (_stricmp(sub, kSubs[i]) == 0)
			return 1;
	return 0;
}

/* Data East M6502 / R65C02 sound boards (karnov / dec0 / dec8 maps). */
static int CEmuAcIsDecoDec8Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	/* MAME dec8.cpp: R65C02, YM2203@$2000, YM3812@$4000, latch@$6000.
	   Do NOT list drgninja here — Bad Dudes / Heavy Barrel / Robocop are dec0. */
	static const char* const kSubs[] = {
		"cobracom", "brkthru", "exprraid", "lastmisn", "makyosen", "oscar"
	};
	for (int i = 0; i < (int)_countof(kSubs); i++)
		if (_stricmp(sub, kSubs[i]) == 0)
			return 1;
	return 0;
}

/* MAME dec0.cpp: YM2203@$0800, YM3812@$1000, latch@$3000, OKI@$3800. */
static int CEmuAcIsDecoDec0Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	static const char* const kSubs[] = {
		"drgninja", "baddudes", "hbarrel", "hippodrm", "robocop",
		"birdtry", "stadhero", "slyspy", "secretag"
	};
	for (int i = 0; i < (int)_countof(kSubs); i++)
		if (_stricmp(sub, kSubs[i]) == 0)
			return 1;
	return 0;
}

static int CEmuAcIsDecoM6502Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	if (_stricmp(sub, "karnov") == 0 || _stricmp(sub, "triothep") == 0
		|| _stricmp(sub, "actfancr") == 0)
		return 1;
	if (CEmuAcIsDecoDec0Sub(sub)) return 1;
	return CEmuAcIsDecoDec8Sub(sub);
}

static int CEmuAcIsDecoSub(const char* sub)
{
	return CEmuAcIsDecoH6280Sub(sub) || CEmuAcIsDecoM6502Sub(sub);
}

/* decoCpuKind_: 0=H6280, 1=karnov, 2=dec0/actfancr, 4=dec8 (cobracom…). */
static int CEmuAcDecoCpuKind(const char* sub)
{
	if (!sub) return 0;
	if (_stricmp(sub, "karnov") == 0) return 1;
	if (CEmuAcIsDecoDec8Sub(sub)) return 4;
	if (CEmuAcIsDecoDec0Sub(sub)) return 2;
	if (CEmuAcIsDecoM6502Sub(sub)) return 2;
	return 0;
}

/* Taito subtypes whose sound board is Z80 + YM2610 + TC0140SYT. */
static int CEmuAcIsTaitoYm2610Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "f2system") == 0
		|| _stricmp(sub, "bsystem") == 0
		|| _stricmp(sub, "dual68") == 0
		|| _stricmp(sub, "taitoh") == 0
		|| _stricmp(sub, "spacegun") == 0
		|| _stricmp(sub, "bshark") == 0
		|| _stricmp(sub, "warriorb") == 0
		|| _stricmp(sub, "fx1a") == 0
		|| _stricmp(sub, "gseeker") == 0
		|| _stricmp(sub, "ridingf") == 0
		|| _stricmp(sub, "ringrage") == 0) ? 1 : 0;
}

/* Taito subtypes on the older Z80 + YM2151 + PC060HA board (Rastan/Asuka). */
static int CEmuAcIsTaitoOpmSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "fullt") == 0
		|| _stricmp(sub, "rastan") == 0
		|| _stricmp(sub, "asuka") == 0
		|| _stricmp(sub, "opwolf") == 0
		|| _stricmp(sub, "rainbow") == 0) ? 1 : 0;
}

/* Taito B System YM2203 + PC060HA (masterw); viofight adds OKI @B000.
   tnzs/chukatai share the same Z80+YM2203+PC060HA class. */
static int CEmuAcIsTaitoYm2203Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "masterw") == 0
		|| _stricmp(sub, "viofight") == 0
		|| _stricmp(sub, "tnzs") == 0
		|| _stricmp(sub, "chukatai") == 0
		|| _stricmp(sub, "extrmatn") == 0) ? 1 : 0;
}

/* Konami Z80 + YM2151 + K007232 (PCM stubbed; FM is the BGM path).
   salamander/lifeforce share scontra's map (latch A000, YM C000). */
static int CEmuAcIsKonamiK7232Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "scontra") == 0
		|| _stricmp(sub, "thundercross") == 0
		|| _stricmp(sub, "crimfght") == 0
		|| _stricmp(sub, "twin16") == 0
		|| _stricmp(sub, "salamander") == 0) ? 1 : 0;
}

/* Taito flstory / nycaptor-class: Z80 + AY-3-8910 + MSM5232. */
static int CEmuAcIsFlstorySub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "flstory") == 0
		|| _stricmp(sub, "nycaptor") == 0
		|| _stricmp(sub, "tokio") == 0
		|| _stricmp(sub, "buggychl") == 0) ? 1 : 0;
}

/* Nichibutsu Terra Cresta family: Z80 + YM3526 (or YM2203) via I/O. */
static int CEmuAcIsTerracreSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "terracre") == 0) ? 1 : 0;
}

/* Nichibutsu Armed F / Terra Force: Z80 + YM3812, RAM @F800, latch I/O 4/6. */
static int CEmuAcIsArmedfSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "terraf") == 0
		|| _stricmp(sub, "armedf") == 0
		|| _stricmp(sub, "cclimbr2") == 0
		|| _stricmp(sub, "kozure") == 0
		|| _stricmp(sub, "legion") == 0) ? 1 : 0;
}

/* UPL Ninja Kid II / Atomic Robo-kid: dual YM2203 on Z80 I/O. */
static int CEmuAcIsRobokidSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "robokid") == 0
		|| _stricmp(sub, "ninjakd2") == 0
		|| _stricmp(sub, "mnight") == 0) ? 1 : 0;
}

static int CEmuAcHasChip(const CEmuGameEntry* ge, int chipId)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->chipCount && i < 8; i++)
		if (ge->chipIds[i] == chipId)
			return 1;
	return 0;
}

int CEmuAcGxTitleIsVoice(const CEmuTitleEntry* t)
{
	if (!t) return 0;
	for (int i = 0; t->label[i]; i++) {
		wchar_t c0 = t->label[i];
		if (c0 >= L'A' && c0 <= L'Z') c0 = (wchar_t)(c0 - L'A' + L'a');
		if (c0 != L'v') continue;
		wchar_t c1 = t->label[i + 1];
		wchar_t c2 = t->label[i + 2];
		wchar_t c3 = t->label[i + 3];
		wchar_t c4 = t->label[i + 4];
		if (c1 >= L'A' && c1 <= L'Z') c1 = (wchar_t)(c1 - L'A' + L'a');
		if (c2 >= L'A' && c2 <= L'Z') c2 = (wchar_t)(c2 - L'A' + L'a');
		if (c3 >= L'A' && c3 <= L'Z') c3 = (wchar_t)(c3 - L'A' + L'a');
		if (c4 >= L'A' && c4 <= L'Z') c4 = (wchar_t)(c4 - L'A' + L'a');
		if (c1 == L'o' && c2 == L'i' && c3 == L'c' && c4 == L'e')
			return 1;
	}
	return 0;
}

unsigned CEmuAcPickGxDefaultTitle(const CEmuGameEntry* ge)
{
	if (!ge || ge->titleCount <= 0)
		return 0x0105u;
	/* Prefer bank 0x01 character/stage themes (0x101-0x10B). Skip select
	   fanfares (0x10C+), voice clips, speaker-check (0x12B), opening voices. */
	for (int pass = 0; pass < 6; pass++) {
		for (int i = 0; i < ge->titleCount; i++) {
			const CEmuTitleEntry* t = &ge->title[i];
			const unsigned c = t->code;
			if (!c || c == 0x200u || (c & 0xffu) == 0 || CEmuAcGxTitleIsVoice(t))
				continue;
			if (c == 0x12Bu || c == 0x0601u || c == 0x0603u)
				continue;
			const unsigned hi = (c >> 8) & 0xffu;
			const unsigned lo = c & 0xffu;
			if (pass == 0 && hi == 0x01u && lo >= 0x06u && lo <= 0x0bu) return c;
			if (pass == 1 && hi == 0x01u && lo >= 0x01u && lo <= 0x0bu) return c;
			if (pass == 2 && hi == 0x01u && lo >= 0x0du && lo < 0x20u) return c;
			if (pass == 3 && hi == 0x01u && lo < 0x20u) return c;
			if (pass == 4 && hi >= 0x02u && hi <= 0x05u) return c;
			if (pass == 5) return c;
		}
	}
	return 0x0105u;
}

static void CEmuAcDestroyMainChip(const CHardAc* hw, CChip* chip)
{
	if (!chip) return;
	if (hw && hw->mainIsYm2610_) {
		CEmuChipYm2610Destroy(chip);
		return;
	}
	if (hw && hw->mainIsYm2612_) {
		CEmuChipYm2612Destroy(chip);
		return;
	}
	if (hw && hw->mainIsYm2203_) {
		CEmuChipYm2608Destroy(chip);
		return;
	}
	if (!hw) {
		CEmuChipYm2151Destroy(chip);
		return;
	}
	switch (hw->board_) {
	case CEMU_AC_BOARD_GNG:
	case CEMU_AC_BOARD_HANGON:
		CEmuChipYm2608Destroy(chip);
		break;
	case CEMU_AC_BOARD_VSYSTEM:
	case CEMU_AC_BOARD_TAITO_YM2610:
		CEmuChipYm2610Destroy(chip);
		break;
	case CEMU_AC_BOARD_SEGA_SYS1:
		CEmuChipSn76489Destroy(chip);
		break;
	case CEMU_AC_BOARD_TAITO_SJ:
	case CEMU_AC_BOARD_KONAMI_SCRAMBLE:
	case CEMU_AC_BOARD_KONAMI_TIMEPLT:
	case CEMU_AC_BOARD_KONAMI_GX400:
	case CEMU_AC_BOARD_IREM_M62:
	case CEMU_AC_BOARD_FLSTORY:
		CEmuChipAyDestroy(chip);
		break;
	case CEMU_AC_BOARD_SYS18:
	case CEMU_AC_BOARD_SYS32:
		CEmuChipYm2612Destroy(chip);
		break;
	case CEMU_AC_BOARD_CPS_QS:
		CEmuChipQSoundDestroy(chip);
		break;
	case CEMU_AC_BOARD_KONAMI_PCM:
		if (hw && hw->pcmKind_ == 4)
			CEmuChipK054539Destroy(chip);
		else
			CEmuChipK053260Destroy(chip);
		break;
	case CEMU_AC_BOARD_KONAMI_GX:
		CEmuChipK054539Destroy(chip);
		break;
	case CEMU_AC_BOARD_NAMCO_C352:
		if (hw && hw->m37702C140_)
			CEmuChipC140Destroy(chip);
		else
			CEmuChipC352Destroy(chip);
		break;
	case CEMU_AC_BOARD_NAMCO_WSG:
		CEmuChipC30Destroy(chip);
		break;
	case CEMU_AC_BOARD_TOAPLAN1:
	case CEMU_AC_BOARD_SNK_OPL:
	case CEMU_AC_BOARD_SEIBU_OPL:
	case CEMU_AC_BOARD_KONAMI_HCASTLE:
	case CEMU_AC_BOARD_TERRACRE:
	case CEMU_AC_BOARD_BATTLANTIS:
		CEmuChipYm3812Destroy(chip);
		break;
	case CEMU_AC_BOARD_DECO:
		if (hw->DecoCpuKind() != 0)
			CEmuChipYm3812Destroy(chip);
		else
			CEmuChipYm2151Destroy(chip);
		break;
	case CEMU_AC_BOARD_SEGA_SCSP:
		CEmuChipScspDestroy(chip);
		break;
	case CEMU_AC_BOARD_KONAMI_RF5C400:
		CEmuChipRf5c400Destroy(chip);
		break;
	default:
		CEmuChipYm2151Destroy(chip);
		break;
	}
}

static void CEmuAcDestroyPcmChip(const CHardAc* hw, CChip* pcm)
{
	if (!pcm) return;
	const int kind = hw ? hw->pcmKind_ : 0;
	if (kind == 1) {
		CEmuChipSegaPcmDestroy(pcm);
		return;
	}
	if (kind == 2) {
		CEmuChipOki6295Destroy(pcm);
		return;
	}
	if (kind == 3) {
		CEmuChipK053260Destroy(pcm);
		return;
	}
	if (kind == 4) {
		CEmuChipK054539Destroy(pcm);
		return;
	}
	if (kind == 5) {
		CEmuChipRf5c68Destroy(pcm);
		return;
	}
	if (kind == 6) {
		CEmuChipIremDacDestroy(pcm);
		return;
	}
	if (kind == 7) {
		CEmuChipGa20Destroy(pcm);
		return;
	}
	if (kind == 8) {
		CEmuChipC140Destroy(pcm);
		return;
	}
	if (kind == 9) {
		CEmuChipC30Destroy(pcm);
		return;
	}
	if (kind == 10) {
		CEmuChipMultiPcmDestroy(pcm);
		return;
	}
	if (!hw) {
		CEmuChipSegaPcmDestroy(pcm);
		return;
	}
	if (hw->board_ == CEMU_AC_BOARD_CPS1)
		CEmuChipOki6295Destroy(pcm);
	else if (hw->board_ == CEMU_AC_BOARD_KONAMI_PCM)
		CEmuChipK053260Destroy(pcm);
	else if (hw->board_ == CEMU_AC_BOARD_SYS18 || hw->board_ == CEMU_AC_BOARD_SYS24)
		CEmuChipRf5c68Destroy(pcm);
	else
		CEmuChipSegaPcmDestroy(pcm);
}

static void CEmuAcDestroyAuxChip(const CHardAc* hw, CChip* aux)
{
	if (!aux) return;
	const int kind = hw ? hw->auxKind_ : 0;
	if (kind == 1) CEmuChipSn76489Destroy(aux);
	else if (kind == 2) CEmuChipAyDestroy(aux);
	else if (kind == 3) CEmuChipYm3812Destroy(aux);
	else if (kind == 4) CEmuChipMsm5232Destroy(aux);
	else if (kind == 5) CEmuChipYm2612Destroy(aux);
	else CEmuChipYm2608Destroy(aux);
}


CEmuAcBoard CEmuAcResolveBoard(const CEmuGameEntry* ge)
{
	if (!ge) return CEMU_AC_BOARD_UNKNOWN;
	CEmuAcBoard board = CEMU_AC_BOARD_UNKNOWN;
	const int hasOpm = CEmuAcHasChip(ge, CEMU_CHIP_OPM);
	const int hasQSound = CEmuAcHasChip(ge, CEMU_CHIP_QSOUND);
	const int hasSegaPcm = CEmuAcHasChip(ge, CEMU_CHIP_SEGAPCM);
	const int hasK053260 = CEmuAcHasChip(ge, CEMU_CHIP_K053260);
	const int hasK054539 = CEmuAcHasChip(ge, CEMU_CHIP_K054539);
	const int hasC352 = CEmuAcHasChip(ge, CEMU_CHIP_C352);
	const int hasC140 = CEmuAcHasChip(ge, CEMU_CHIP_C140);
	const int hasC30 = CEmuAcHasChip(ge, CEMU_CHIP_C30);
	const int naNb = (_stricmp(ge->subtype, "na1") == 0 || _stricmp(ge->subtype, "na2") == 0
		|| _stricmp(ge->subtype, "nb1") == 0 || _stricmp(ge->subtype, "nb2") == 0);
	if (_stricmp(ge->subtype, "cps2") == 0 || _stricmp(ge->subtype, "cps1qs") == 0
		|| _stricmp(ge->subtype, "zn") == 0 || hasQSound)
		board = CEMU_AC_BOARD_CPS_QS;
	else if (_strnicmp(ge->subtype, "system18", 8) == 0)
		board = CEMU_AC_BOARD_SYS18;
	else if (_strnicmp(ge->subtype, "system24", 8) == 0)
		board = CEMU_AC_BOARD_SYS24;
	else if (_strnicmp(ge->subtype, "system32", 8) == 0
		|| _stricmp(ge->subtype, "system_multi") == 0
		|| _stricmp(ge->subtype, "multi32") == 0)
		board = CEMU_AC_BOARD_SYS32;
	else if (_stricmp(ge->subtype, "systemgx") == 0
		|| _stricmp(ge->subtype, "054539x2") == 0)
		/* Dual K054539 + K056800 sound 68000 (metamrph/rungun/viostorm…).
		   Must not fall through to Z80 KONAMI_PCM. */
		board = CEMU_AC_BOARD_KONAMI_GX;
	else if ((_stricmp(ge->platform, "namco") == 0
		&& (_stricmp(ge->subtype, "system2") == 0
			|| _stricmp(ge->subtype, "system21") == 0
			|| _stricmp(ge->subtype, "c140") == 0))
		|| (hasC140 && !naNb))
		board = CEMU_AC_BOARD_NAMCO_SYS2;
	else if (_stricmp(ge->platform, "namco") == 0 && _stricmp(ge->subtype, "system1") == 0)
		board = CEMU_AC_BOARD_NAMCO_SYS1;
	else if (_stricmp(ge->platform, "namco") == 0
		&& _stricmp(ge->subtype, "system86") == 0)
		board = CEMU_AC_BOARD_NAMCO_SYS86;
	else if ((_stricmp(ge->platform, "namco") == 0
		&& (_stricmp(ge->subtype, "wsg6809") == 0
			|| _stricmp(ge->subtype, "wsg63701") == 0
			|| _stricmp(ge->subtype, "wsgz80") == 0
			|| _stricmp(ge->subtype, "wsg") == 0
			|| _stricmp(ge->subtype, "c30") == 0
			|| _stricmp(ge->subtype, "cus30") == 0))
		|| (hasC30 && !hasOpm))
		board = CEMU_AC_BOARD_NAMCO_WSG;
	else if (hasC30 && hasOpm)
		board = CEMU_AC_BOARD_NAMCO_SYS1;
	else if ((_stricmp(ge->platform, "namco") == 0
		&& (_stricmp(ge->subtype, "c352") == 0
			|| _stricmp(ge->subtype, "system11") == 0
			|| _stricmp(ge->subtype, "system12") == 0
			|| _stricmp(ge->subtype, "system22") == 0
			|| _stricmp(ge->subtype, "nb1") == 0
			|| _stricmp(ge->subtype, "na1") == 0
			|| _stricmp(ge->subtype, "nd1") == 0
			|| hasC352))
		|| (_stricmp(ge->subtype, "c352") == 0 || hasC352))
		board = CEMU_AC_BOARD_NAMCO_C352;
	else if (_stricmp(ge->subtype, "053260") == 0 || _stricmp(ge->subtype, "054539") == 0
		|| hasK053260 || hasK054539
		|| (_strnicmp(ge->platform, "konami", 6) == 0
			&& (hasK053260 || hasK054539)))
		board = CEMU_AC_BOARD_KONAMI_PCM;
	else if (_stricmp(ge->subtype, "system16a") == 0)
		board = CEMU_AC_BOARD_SYS16A;
	else if (_stricmp(ge->subtype, "system16b") == 0)
		board = CEMU_AC_BOARD_SYS16B;
	else if (_strnicmp(ge->subtype, "cps1", 4) == 0)
		board = CEMU_AC_BOARD_CPS1;
	else if (_stricmp(ge->subtype, "gng") == 0 || _stricmp(ge->subtype, "opn2") == 0
		/* Capcom pre-CPS YM2203 boards (MAME gng/srumbler/tigeroad/commando). */
		|| _stricmp(ge->subtype, "tigerroad") == 0 || _stricmp(ge->subtype, "tigeroad") == 0
		|| _stricmp(ge->subtype, "rushcrsh") == 0 || _stricmp(ge->subtype, "srumbler") == 0
		|| _stricmp(ge->subtype, "commando") == 0 || _stricmp(ge->subtype, "sectionz") == 0
		|| _stricmp(ge->subtype, "trojan") == 0 || _stricmp(ge->subtype, "higemaru") == 0
		|| _stricmp(ge->subtype, "exedexes") == 0 || _stricmp(ge->subtype, "gunsmoke") == 0
		|| _stricmp(ge->subtype, "blktiger") == 0 || _stricmp(ge->subtype, "blacktiger") == 0
		/* Tecmo dual YM2203 (gaiden / gemini / silkworm family). */
		|| _stricmp(ge->subtype, "gaiden") == 0 || _stricmp(ge->subtype, "gemini") == 0)
		board = CEMU_AC_BOARD_GNG;
	else if (_stricmp(ge->subtype, "aburner") == 0
		/* Sega X-Board / G-LOC: same Z80+YM2151+SegaPCM class as After Burner. */
		|| _stricmp(ge->subtype, "gforce") == 0)
		board = CEMU_AC_BOARD_ABURNER;
	else if (_stricmp(ge->subtype, "outrun") == 0 || _stricmp(ge->subtype, "toutrun") == 0
		|| _stricmp(ge->subtype, "shangon") == 0)
		board = CEMU_AC_BOARD_OUTRUN;
	else if (_stricmp(ge->subtype, "sharrier") == 0 || _stricmp(ge->subtype, "hangon") == 0)
		board = CEMU_AC_BOARD_HANGON;
	else if (_stricmp(ge->subtype, "aerofgt") == 0 || _stricmp(ge->platform, "videosystem") == 0
		|| _stricmp(ge->subtype, "gunbird") == 0)
		board = CEMU_AC_BOARD_VSYSTEM;
	else if (CEmuAcIsTaitoYm2610Sub(ge->subtype)
		|| (_stricmp(ge->platform, "taito") == 0 && CEmuAcHasChip(ge, CEMU_CHIP_YM2610)))
		board = CEMU_AC_BOARD_TAITO_YM2610;
	else if (CEmuAcIsTaitoOpmSub(ge->subtype)
		|| CEmuAcIsTaitoYm2203Sub(ge->subtype))
		board = CEMU_AC_BOARD_TAITO_OPM;
	else if (_stricmp(ge->subtype, "taitosj") == 0)
		board = CEMU_AC_BOARD_TAITO_SJ;
	else if (_stricmp(ge->subtype, "scramble") == 0
		|| _stricmp(ge->subtype, "scobra") == 0
		|| _stricmp(ge->subtype, "frogger") == 0)
		board = CEMU_AC_BOARD_KONAMI_SCRAMBLE;
	else if (_stricmp(ge->subtype, "timeplt") == 0
		|| _stricmp(ge->subtype, "pooyan") == 0
		|| _stricmp(ge->subtype, "locomotn") == 0
		|| _stricmp(ge->subtype, "jungler") == 0
		|| _stricmp(ge->subtype, "circusc") == 0)
		board = CEMU_AC_BOARD_KONAMI_TIMEPLT;
	else if (_stricmp(ge->subtype, "gx400") == 0
		|| _stricmp(ge->subtype, "nemesis") == 0)
		board = CEMU_AC_BOARD_KONAMI_GX400;
	else if (CEmuAcIsKonamiK7232Sub(ge->subtype))
		board = CEMU_AC_BOARD_KONAMI_K7232;
	else if (_stricmp(ge->subtype, "68k2") == 0)
		board = CEMU_AC_BOARD_ALPHA68K2;
	else if (_stricmp(ge->subtype, "hcastle") == 0)
		board = CEMU_AC_BOARD_KONAMI_HCASTLE;
	else if (_stricmp(ge->subtype, "battlantis") == 0)
		board = CEMU_AC_BOARD_BATTLANTIS;
	else if (_stricmp(ge->subtype, "tecmo16") == 0)
		board = CEMU_AC_BOARD_TECMO16;
	else if (CEmuAcIsFlstorySub(ge->subtype))
		board = CEMU_AC_BOARD_FLSTORY;
	else if (CEmuAcIsTerracreSub(ge->subtype) || CEmuAcIsArmedfSub(ge->subtype))
		board = CEMU_AC_BOARD_TERRACRE;
	else if (CEmuAcIsRobokidSub(ge->subtype))
		board = CEMU_AC_BOARD_ROBOKID;
	else if (_stricmp(ge->subtype, "ddragon2") == 0)
		board = CEMU_AC_BOARD_TECHNOS_DDRAGON2;
	else if (_stricmp(ge->subtype, "m62") == 0)
		board = CEMU_AC_BOARD_IREM_M62;
	else if (_strnicmp(ge->subtype, "model2", 6) == 0
		|| _stricmp(ge->subtype, "model3") == 0)
		board = CEMU_AC_BOARD_SEGA_SCSP;
	else if (_stricmp(ge->subtype, "hornet") == 0
		|| _stricmp(ge->subtype, "gticlub") == 0)
		board = CEMU_AC_BOARD_KONAMI_RF5C400;
	else if (_stricmp(ge->platform, "snk") == 0
		&& (_stricmp(ge->subtype, "3812") == 0
			|| _stricmp(ge->subtype, "3526") == 0
			|| _stricmp(ge->subtype, "3526x2") == 0
			|| _stricmp(ge->subtype, "3526_8950") == 0
			|| _stricmp(ge->subtype, "fitegolf") == 0
			|| _stricmp(ge->subtype, "chopper") == 0
			|| _stricmp(ge->subtype, "aso") == 0
			|| _stricmp(ge->subtype, "mainsnk") == 0))
		board = CEMU_AC_BOARD_SNK_OPL;
	else if ((_stricmp(ge->platform, "seibu") == 0
			|| _stricmp(ge->platform, "tad") == 0)
		&& (_stricmp(ge->subtype, "raiden") == 0
			|| _stricmp(ge->subtype, "raiden2") == 0
			|| _stricmp(ge->subtype, "cabal") == 0
			|| _stricmp(ge->subtype, "mustache") == 0))
		board = CEMU_AC_BOARD_SEIBU_OPL;
	else if (_stricmp(ge->subtype, "m72") == 0
		|| _stricmp(ge->subtype, "rtype") == 0
		|| _stricmp(ge->subtype, "rtype2") == 0
		|| _stricmp(ge->subtype, "m84") == 0
		|| _stricmp(ge->subtype, "m82") == 0
		|| _stricmp(ge->subtype, "airduel") == 0
		|| _stricmp(ge->subtype, "imgfight") == 0
		|| _stricmp(ge->subtype, "hharry") == 0
		|| _stricmp(ge->subtype, "gallop") == 0
		|| _stricmp(ge->subtype, "dbreed") == 0
		|| _stricmp(ge->subtype, "nspirit") == 0
		|| _stricmp(ge->subtype, "loht") == 0
		|| _stricmp(ge->subtype, "poundfor") == 0)
		board = CEMU_AC_BOARD_IREM_M72;
	else if (_stricmp(ge->subtype, "m92") == 0)
		board = CEMU_AC_BOARD_IREM_M92;
	else if ((_stricmp(ge->subtype, "system1") == 0 || _stricmp(ge->subtype, "system2") == 0)
		&& _stricmp(ge->platform, "sega") == 0)
		board = CEMU_AC_BOARD_SEGA_SYS1;
	else if (_stricmp(ge->platform, "atari") == 0
		&& (_stricmp(ge->subtype, "system1") == 0
			|| _stricmp(ge->subtype, "atarisy1") == 0))
		board = CEMU_AC_BOARD_ATARI_SYS1;
	else if (_stricmp(ge->subtype, "megasys1") == 0)
		board = CEMU_AC_BOARD_MEGASYSTEM1;
	else if (_stricmp(ge->platform, "toaplan") == 0
		&& (_stricmp(ge->subtype, "generic") == 0
			|| _stricmp(ge->subtype, "truxton") == 0
			|| _stricmp(ge->subtype, "hellfire") == 0
			|| _stricmp(ge->subtype, "zerowing") == 0
			|| _stricmp(ge->subtype, "outzone") == 0
			|| _stricmp(ge->subtype, "demonwld") == 0
			|| _stricmp(ge->subtype, "tigerh") == 0
			|| _stricmp(ge->subtype, "wardner") == 0
			|| ge->subtype[0] == 0))
		/* Classic Toaplan1 Z80+YM3812 (shared-RAM mailbox). MCU/Toaplan2
		   subtypes (tp/vimana/fireshrk/�c) stay unmapped. */
		board = CEMU_AC_BOARD_TOAPLAN1;
	else if (CEmuAcIsDecoSub(ge->subtype))
		/* Only HuC6280 Data East boards (cninja/thndzone/deco32/…). Older
		   AY boards (btime/disco) stay UNKNOWN — soft SILENT, not FAIL_OPEN. */
		board = CEMU_AC_BOARD_DECO;
	else if (_strnicmp(ge->platform, "capcom", 6) == 0
		&& (_stricmp(ge->subtype, "gng") == 0 || _stricmp(ge->subtype, "opn2") == 0))
		board = CEMU_AC_BOARD_GNG;
	else if (_strnicmp(ge->platform, "capcom", 6) == 0
		&& (_strnicmp(ge->subtype, "cps1", 4) == 0 || ge->subtype[0] == 0))
		board = CEMU_AC_BOARD_CPS1;
	return board;
}

int CHardAc::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsAcPlatform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	board_ = CEmuAcResolveBoard(ge);
	const int hasOpm = CEmuAcHasChip(ge, CEMU_CHIP_OPM);
	const int hasSegaPcm = CEmuAcHasChip(ge, CEMU_CHIP_SEGAPCM);
	const int hasK054539 = CEmuAcHasChip(ge, CEMU_CHIP_K054539);

	qsZn_ = (_stricmp(ge->subtype, "zn") == 0) ? 1 : 0;

	pcmKind_ = 0;
	auxKind_ = 0;
	m37702Soft_ = 0;
	m37702C140_ = 0;
	m37702MapKind_ = 0;
	snkMapKind_ = 0;
	snkStatus_ = 0;
	terracreMap_ = 0;
	flstoryNmiEn_ = 0;
	segaM1Audio_ = 0;
	chip3_ = NULL;
	mainIsYm2203_ = 0;
	mainIsYm2610_ = 0;
	mainIsYm2612_ = 0;
	wsgMappy_ = 0;
	wsg63701_ = 0;
	gngCommandoMap_ = 0;
	gngGaidenMap_ = 0;
	konamiPcmWindow_ = 0x40u;
	sys16RomBoard_ = (board_ == CEMU_AC_BOARD_SYS16A) ? 0x5358u : 0x5797u;

	if (board_ == CEMU_AC_BOARD_GNG) {
		/* Commando (and lookalikes): RAM 4000, latch 6000, YM 8000 ? not GNG's
		   C000/C800/E000 map. ExedExes is AY+SN on the same decode; still open. */
		if (_stricmp(ge->subtype, "commando") == 0
			|| _stricmp(ge->subtype, "exedexes") == 0
			|| _stricmp(ge->subtype, "higemaru") == 0)
			gngCommandoMap_ = 1;
		/* Tecmo gaiden/shadoww: dual YM2203 @ F810/F820 + OKI @ F800, latch NMI. */
		else if (_stricmp(ge->subtype, "gaiden") == 0)
			gngGaidenMap_ = 1;
		if (gngGaidenMap_) {
			/* MAME tecmo/gaiden: Z80+YM2203x2 @ 4 MHz, OKI6295, latch→NMI. */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0; /* chip2 OPN — Render sums via Chip2 */
			pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
			pcmKind_ = 2;
		} else {
			/* MAME: Z80 @ 3 MHz, dual YM2203 @ 1.5 MHz.
			   MVP: one OPN instance shared across E000 and E002 (still audible). */
			cpuHz_ = 3000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			chip2_ = NULL;
		}
	} else if (board_ == CEMU_AC_BOARD_HANGON) {
		cpuHz_ = 4000000;
		/* YM2203 tone generation and timers share the physical 4 MHz master.
		   Tick the chip in that same domain; halving only AdvanceClocks left
		   pitch correct but stretched Timer B and the sequencer to half speed. */
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
		mainIsYm2203_ = 1;
		chip2_ = NULL;
		/* Music tempo = Timer B IRQs; Timer A would roughly double the ISR rate. */
		if (chip_)
			chip_->SetTimerIrqPolicy(0);
		/* MAME master: SEGAPCM_DISCRETE(..., 8_MHz_XTAL / 2) �� 4 MHz.
		   Full 8 MHz made samples ~2�~ fast / thin. Stream = clock/64 = 62.5 kHz. */
		pcm_ = CEmuChipSegaPcmCreateDiscrete(4000000u, sampleRate_);
		pcmKind_ = 1;
	} else if (board_ == CEMU_AC_BOARD_SYS18) {
		cpuHz_ = 8000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		mainIsYm2612_ = 1;
		/* MAME system18: two YM3438. Second chip is a real OPN2, not a mirror. */
		chip2_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		auxKind_ = 5;
		pcm_ = CEmuChipRf5c68Create(10000000u, sampleRate_);
		pcmKind_ = 5;
	} else if (board_ == CEMU_AC_BOARD_SYS24) {
		/* Dual 68000 + YM2151 (no Z80 / RF5C68). Disk soft-open only until
		   a Musashi host path loads sound_addr / irq_addr from the image. */
		cpuHz_ = 10000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_VSYSTEM) {
		/* MAME vsystem/aerofgt: Z80 20/4 = 5 MHz, YM2610 8 MHz (both verified
		   on pcb). Sound ROM is 128K banked 8000-FFFF in four 32K windows.
		   fromanc2 runs the Z80 at 8 MHz; keep 5 MHz (close enough for BGM).
		   Psikyo gunbird: Z80+YM2610 @ 8 MHz, I/O YM@04, latch@08 (vsIoKind 3). */
		const int psikyo = (_stricmp(ge->subtype, "gunbird") == 0) ? 1 : 0;
		cpuHz_ = psikyo ? 8000000 : 5000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2610Create(8000000u, sampleRate_);
		mainIsYm2610_ = 1;
		chip2_ = NULL;
		bankBase_ = 0x8000u;
		bankSize_ = 0x8000u;
		vsIoKind_ = 0;
		if (psikyo)
			vsIoKind_ = 3;
		else if (_stricmp(ge->subtype, "turbofrc") == 0
			|| _stricmp(ge->subtype, "f1gp") == 0
			|| _stricmp(ge->subtype, "pipedrm") == 0
			|| _stricmp(ge->subtype, "spinlbrk") == 0
			|| _stricmp(ge->archive, "pspikes") == 0
			|| _stricmp(ge->archive, "karatblz") == 0
			|| _stricmp(ge->archive, "spinlbrk") == 0
			|| _stricmp(ge->archive, "turbofrc") == 0
			|| _stricmp(ge->archive, "f1gp") == 0
			|| _stricmp(ge->archive, "f1gp2") == 0
			|| _stricmp(ge->archive, "pipedrm") == 0)
			vsIoKind_ = 1;
		else if (_stricmp(ge->subtype, "fromanc2") == 0
			|| _stricmp(ge->subtype, "fromanc4") == 0
			|| _stricmp(ge->subtype, "welltris") == 0
			|| _stricmp(ge->subtype, "hatris") == 0
			|| _stricmp(ge->subtype, "inufuku") == 0
			|| _stricmp(ge->archive, "fromanc2") == 0
			|| _stricmp(ge->archive, "fromanc4") == 0
			|| _stricmp(ge->archive, "fromancr") == 0
			|| _stricmp(ge->archive, "welltris") == 0
			|| _stricmp(ge->archive, "quiz18k") == 0
			|| _stricmp(ge->archive, "hatris") == 0
			|| _stricmp(ge->archive, "inufuku") == 0)
			vsIoKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_TAITO_YM2610) {
		/* MAME taito_f2: Z80 24/6 = 4 MHz, YM2610 24/3 = 8 MHz. */
		cpuHz_ = 4000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2610Create(8000000u, sampleRate_);
		mainIsYm2610_ = 1;
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_TAITO_OPM) {
		/* MAME taito_rastan / taito_asuka: Z80 4 MHz, YM2151 4 MHz.
		   masterw/viofight (Taito B YM2203): same PC060HA map @9000/A000,
		   but OPN @ 3 MHz; viofight also has OKI @B000. */
		const int ym2203 = CEmuAcIsTaitoYm2203Sub(ge->subtype);
		/* MAME masterw: Z80B @ 24/4 = 6 MHz, YM2203 @ 24/8 = 3 MHz. */
		cpuHz_ = ym2203 ? 6000000 : 4000000;
		opmHz_ = ym2203 ? 3000000 : 4000000;
		if (ym2203) {
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			if (_stricmp(ge->subtype, "viofight") == 0) {
				pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
				pcmKind_ = 2;
			}
		} else {
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		}
		chip2_ = NULL;
		bankBase_ = 0x4000u;
		bankSize_ = 0x4000u;
	} else if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		/* MAME sega_system1: SOUND_CLOCK 8 MHz; Z80 /2, SN1 /4, SN2 /2. */
		cpuHz_ = 4000000;
		opmHz_ = 2000000;
		chip_ = CEmuChipSn76489Create(2000000u, sampleRate_);
		chip2_ = CEmuChipSn76489Create(4000000u, sampleRate_);
		auxKind_ = 1;
	} else if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		/* MAME taito_taitosj: Z80 12/4 = 3 MHz, AY-3-8910 12/8 = 1.5 MHz. */
		cpuHz_ = 3000000;
		opmHz_ = 1500000;
		chip_ = CEmuChipAyCreate(1500000u, sampleRate_);
		chip2_ = CEmuChipAyCreate(1500000u, sampleRate_);
		chip3_ = CEmuChipAyCreate(1500000u, sampleRate_);
		auxKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE) {
		/* MAME galaxian/scramble: Z80+AY @ 14318000/8 ? 1.789772 MHz.
		   Do not enable AY unmute-assist ? it forces a constant tone that
		   probe classify() rejects as FLAT (identical peaks). */
		cpuHz_ = 1789772;
		opmHz_ = 1789772;
		chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
		chip2_ = CEmuChipAyCreate(1789772u, sampleRate_);
		auxKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		/* MAME timeplt_a: Z80/AY DERIVED_CLOCK(1,8) from 18.432 MHz �� 2.304 MHz. */
		cpuHz_ = 2304000;
		opmHz_ = 2304000;
		chip_ = CEmuChipAyCreate(2304000u, sampleRate_);
		chip2_ = CEmuChipAyCreate(2304000u, sampleRate_);
		auxKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		/* MAME nemesis/gx400: Z80+AY @ 14318180/8. */
		cpuHz_ = 1789772;
		opmHz_ = 1789772;
		chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
		chip2_ = CEmuChipAyCreate(1789772u, sampleRate_);
		auxKind_ = 2;
		/* AY1 port A is nemesis_portA_r (cycle timer); do not force 0. */
	} else if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		/* MAME technos/ddragon: Z80 3.579545, YM2151 same, OKI 1.056 MHz. */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
		pcmKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_IREM_M62) {
		/* MAME irem/irem.cpp m62_audio: M6803 @ 3.579545 MHz, AY @ /4. */
		cpuHz_ = 3579545;
		opmHz_ = 894886;
		chip_ = CEmuChipAyCreate(894886u, sampleRate_);
		chip2_ = CEmuChipAyCreate(894886u, sampleRate_);
		auxKind_ = 2;
		m6803_ = (struct m6800*)calloc(1, sizeof(struct m6800));
		if (!m6803_) return 0;
	} else if (board_ == CEMU_AC_BOARD_IREM_M72) {
		/* MAME irem_m72: SOUND_CLOCK 3.579545 MHz for both Z80 and YM2151.
		   Classic M72 (rtype) = sound_ram_map (full 64K RAM with uploaded
		   program). M81/M82/M84 (rtype2) = sound_rom_map (ROM + F000 RAM).
		   Default to ROM map for dedicated sound dumps; interleaved upload
		   sets sound_ram_map in LoadRoms. */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipIremDacCreate(sampleRate_);
		pcmKind_ = 6;
		m72SoundRam_ = 0;
		m72IoAlt_ = 0;
		if (_stricmp(ge->subtype, "rtype") == 0)
			m72SoundRam_ = 1;
		if (_stricmp(ge->subtype, "poundfor") == 0
			|| _stricmp(ge->archive, "bbmanw") == 0
			|| _stricmp(ge->archive, "bbmanwj") == 0
			|| _stricmp(ge->archive, "bbmanwa") == 0)
			m72IoAlt_ = 1;
	} else if (board_ == CEMU_AC_BOARD_IREM_M92) {
		/* MAME irem_m92: V35 on the bare 14.318181 MHz XTAL, YM2151 and GA20
		   on XTAL/4. The V35 runs the encrypted sound driver (CEmu/vendor/v35);
		   the Z80 instance below is created but never stepped on this board. */
		cpuHz_ = 14318181;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipGa20Create(3579545u, sampleRate_);
		pcmKind_ = 7;
		v35_ = V35Create();
		if (!v35_) return 0;
	} else if (board_ == CEMU_AC_BOARD_ATARI_SYS1) {
		/* MAME atarisy1 sound_map: M6502 @ 14.31818/8 MHz, YM2151 @ /4,
		   latch @1810 → NMI, YM IRQ → IRQ. POKEY @1870 stubbed. */
		cpuHz_ = 1789772;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		h6280_ = NULL;
		decoCpuKind_ = 5;
		m6502_ = M6502Create();
		if (!m6502_) return 0;
	} else if (board_ == CEMU_AC_BOARD_DECO) {
		decoCpuKind_ = CEmuAcDecoCpuKind(ge->subtype);
		if (decoCpuKind_ == 0) {
			/* MAME cninja.cpp: HuC6280 @ XTAL/8 + YM2203 @ XTAL/8 + YM2151 @ XTAL/9
			   + dual OKI6295. Internal HuC6280 PSG is unused (route 0). */
			cpuHz_ = 32220000 / 8;
			opmHz_ = 32220000 / 9;
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			chip2_ = CEmuChipYm2608Create((uint32_t)(32220000 / 8), 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = CEmuChipOki6295Create(32220000u / 32u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(32220000u / 16u, sampleRate_);
			pcmKind_ = 2;
			h6280_ = H6280Create();
			if (!h6280_) return 0;
		} else if (decoCpuKind_ == 1) {
			/* MAME karnov.cpp: M6502 @ 12/8 MHz + YM2203 @ 1.5 + YM3526 @ 3.
			   YM3812 stands in for YM3526 (OPL). Latch NMI; OPL IRQ → IRQ. */
			cpuHz_ = 1500000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
			chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			h6280_ = NULL;
			m6502_ = M6502Create();
			if (!m6502_) return 0;
		} else if (decoCpuKind_ == 4) {
			/* MAME dec8 (cobracom/oscar/…): R65C02 @ 1.5 MHz + YM2203 @2000
			   + YM3812 @4000 + latch @6000 → NMI. No OKI. */
			cpuHz_ = 1500000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
			chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			h6280_ = NULL;
			m6502_ = M6502Create();
			if (!m6502_) return 0;
		} else {
			/* MAME dec0/actfancr: M6502/R65C02 @ 1.5 MHz + YM2203 + YM3812 + OKI.
			   Latch NMI; YM3812 IRQ → IRQ. */
			cpuHz_ = 1500000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
			chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = CEmuChipOki6295Create(1022727u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			h6280_ = NULL;
			m6502_ = M6502Create();
			if (!m6502_) return 0;
		}
	} else if (board_ == CEMU_AC_BOARD_MEGASYSTEM1) {
		/* MAME jaleco/megasys1: sound 68000 @ 7 MHz, YM2151 @ 3.5 MHz, two
		   OKI6295 @ 4 MHz with PIN7 high (clock / 132 = 30303 nibbles/s ?
		   CChipOki6295 takes that rate, not the raw crystal). */
		cpuHz_ = 7000000;
		opmHz_ = 3500000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(4000000u / 132u, sampleRate_);
		pcm2_ = CEmuChipOki6295Create(4000000u / 132u, sampleRate_);
		pcmKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		/* MAME konamigx gxsndmap: sound 68000 @ 16 MHz, dual K054539 @
		   18.432 MHz, K056800 mailbox, TMS57002 DASP (stubbed). */
		cpuHz_ = 16000000;
		opmHz_ = 18432000;
		chip_ = CEmuChipK054539Create(18432000u, sampleRate_);
		pcm_ = CEmuChipK054539Create(18432000u, sampleRate_);
		CEmuChipK054539SetFmMonBase(chip_, 0);
		CEmuChipK054539SetFmMonBase(pcm_, 8);
		pcmKind_ = 4;
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_CPS_QS) {
		cpuHz_ = 8000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipQSoundCreate(4000000u, sampleRate_);
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_NAMCO_C352) {
		/* Sys12 H8/3002 @ 16.9344 MHz; ND-1 @ 49.152/3 ≈ 16.384 MHz.
		   MAME clocks the Sys12 C352 at 25.4016 MHz with divider 288.
		   Sys11/22 and ND-1 use 24.576 MHz. Sys11/22 + NA-1/NB-1: real M37702
		   (C74/C76/C69) — cannot reuse H8. NA/NB use C219 (C140). */
		const int nd1 = (ge && _stricmp(ge->subtype, "nd1") == 0);
		const int naNb = (ge && (_stricmp(ge->subtype, "na1") == 0
			|| _stricmp(ge->subtype, "nb1") == 0
			|| _stricmp(ge->subtype, "na2") == 0
			|| _stricmp(ge->subtype, "nb2") == 0));
		const int sys11 = (ge && (_stricmp(ge->subtype, "system11") == 0
			|| _stricmp(ge->subtype, "sys11") == 0));
		const int sys22 = (ge && (_stricmp(ge->subtype, "system22") == 0
			|| _stricmp(ge->subtype, "sys22") == 0));
		const int sysM377 = sys11 || sys22 || naNb;
		cpuHz_ = nd1 ? 16384000 : (naNb ? 12500000 : 16934400);
		m37702Soft_ = 0;
		m37702C140_ = naNb ? 1 : 0;
		m37702MapKind_ = naNb ? 1 : (sys22 ? 2 : 0);
		if (naNb) {
			opmHz_ = 8192000;
			chip_ = CEmuChipC140Create((uint32_t)opmHz_, sampleRate_);
			if (chip_) CEmuChipC140SetType(chip_, 2); /* C219 */
		} else {
			opmHz_ = (!nd1 && !sysM377) ? 25401600 : 24576000;
			chip_ = CEmuChipC352Create((uint32_t)opmHz_, sampleRate_);
		}
		chip2_ = NULL;
		if (sysM377) {
			h8_ = NULL;
			m37702_ = M37702Create();
			if (!m37702_) return 0;
			m37702Soft_ = 1; /* cleared once LoadRomsM37702 attaches */
		} else {
			m37702_ = NULL;
			h8_ = H8Create();
			if (!h8_) return 0;
		}
	} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		/* MAME namcos2: M6809 @ 2.048 MHz + YM2151 @ 3.579545 + C140 @ 21.333 kHz. */
		cpuHz_ = 2048000;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipC140Create(21333u, sampleRate_);
		pcmKind_ = 8;
		namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
		if (!namcoM6809_) return 0;
	} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
		/* MAME namcos1: M6809 @ 49.152/32 MHz + YM2151 + CUS30. */
		cpuHz_ = 1536000;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipC30Create(24000u, sampleRate_, CEMU_C30_STEREO);
		pcmKind_ = 9;
		namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
		if (!namcoM6809_) return 0;
	} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS86) {
		/* MAME namcos86: HD63701 @ 49.152/8 MHz + CUS30 + YM2151. */
		cpuHz_ = 1536000;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipC30Create(24000u, sampleRate_, CEMU_C30_STEREO);
		pcmKind_ = 9;
		hd63701_ = HD63701Create();
		if (!hd63701_) return 0;
		/* Real CUS60 songs program KC/KeyOn; mute-refresh assist flattened
		   every title to the same clipped peak — keep raw YM traffic. */
	} else if (board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		/* Galaga / Dig Dug / Bosco: Z80 @ ~3.072 MHz + 3-voice Pac-Man WSG
		   (PROM waveforms). Mappy-era 15XX uses MAPPY for wsg6809.
		   wsg63701 (pacland/skykid): HD63701 + CUS30 MAPPY, no YM2151. */
		wsgMappy_ = (_stricmp(ge->subtype, "wsg6809") == 0
			|| _stricmp(ge->subtype, "wsg63701") == 0) ? 1 : 0;
		wsg63701_ = (_stricmp(ge->subtype, "wsg63701") == 0) ? 1 : 0;
		const int pacman = wsgMappy_ ? 0 : 1;
		cpuHz_ = pacman ? 3072000 : 1536000;
		opmHz_ = pacman ? 96000 : 24000;
		chip_ = CEmuChipC30Create((uint32_t)opmHz_, sampleRate_,
			pacman ? CEMU_C30_PACMAN : CEMU_C30_MAPPY);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		if (wsg63701_) {
			hd63701_ = HD63701Create();
			if (!hd63701_) return 0;
		} else if (wsgMappy_) {
			namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
			if (!namcoM6809_) return 0;
		}
	} else if (board_ == CEMU_AC_BOARD_TOAPLAN1) {
		/* MAME toaplan1: Z80 + YM3812 both @ 28 MHz / 8. Commands live in
		   shared RAM at 8000 (not a latch/NMI). YM I/O port differs by game:
		   truxton/rallybik=60, hellfire=70, zerowing=A8, else 00. */
		cpuHz_ = 3500000;
		opmHz_ = 3500000;
		chip_ = CEmuChipYm3812Create(3500000u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		toaplanYmPort_ = 0x00;
		if (ge && ge->archive) {
			if (_stricmp(ge->archive, "truxton") == 0
				|| _stricmp(ge->archive, "rallybik") == 0)
				toaplanYmPort_ = 0x60;
			else if (_stricmp(ge->archive, "hellfire") == 0)
				toaplanYmPort_ = 0x70;
			else if (_stricmp(ge->archive, "zerowing") == 0)
				toaplanYmPort_ = 0xa8;
		}
	} else if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		/* snk68 (3812): Z80 + YM3812 I/O 00/20, latch @ F800 → NMI.
		   Classic SNK (athena/ikari/gwar…): mem-map dual YM3526/Y8950,
		   latch @ E000 → IRQ0 (MAME snk.cpp YM3526_*_sound_map). */
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		const int classic = (ge && (_stricmp(ge->subtype, "3526x2") == 0
			|| _stricmp(ge->subtype, "3526_8950") == 0
			|| _stricmp(ge->subtype, "3526") == 0
			|| _stricmp(ge->subtype, "aso") == 0
			|| _stricmp(ge->subtype, "mainsnk") == 0
			|| _stricmp(ge->subtype, "chopper") == 0));
		snkMapKind_ = classic ? 1 : 0;
		chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
		chip2_ = classic ? CEmuChipYm3812Create(4000000u, sampleRate_) : NULL;
		auxKind_ = classic ? 3 : 0; /* destroy chip2 as YM3812 */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		/* MAME thunderx/scontra/crimfght/twin16: Z80 + YM2151 @ 3.579545.
		   K007232 PCM register window is accepted but not synthesized yet —
		   BGM is YM2151. Map variants via konamiK7232Map_. */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		konamiK7232Map_ = (_stricmp(ge->subtype, "crimfght") == 0) ? 1 : 0;
	} else if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		/* MAME alpha68k_II: Z80 @ 6 MHz, YM2203 @ ~3 MHz, YM2413 @ 3.579545,
		   DAC, latch via IN 00, bank @ C000 (16KiB). Periodic NMI @ ~7614 Hz. */
		cpuHz_ = 6000000;
		opmHz_ = 3000000;
		chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN/YM2203 */, sampleRate_);
		mainIsYm2203_ = 1;
		chip2_ = NULL;
		pcm_ = CEmuChipIremDacCreate(sampleRate_);
		pcmKind_ = 6;
		bankBase_ = 0xc000;
		bankSize_ = 0x4000;
		alphaYmAddr_ = 0;
		alphaOpllAddr_ = 0;
		alphaNmiMask_ = 1;
		alphaPaLatch_ = 0;
		if (alphaOpll_) {
			OPLL_delete((OPLL*)alphaOpll_);
			alphaOpll_ = NULL;
		}
		{
			OPLL* o = OPLL_new(3579545u, (uint32_t)sampleRate_);
			if (o) {
				OPLL_set_quality(o, 1);
				OPLL_reset_patch(o, 0);
				alphaOpll_ = (void*)o;
			}
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		/* MAME hcastle: Z80 + YM3812 @ A000 (IRQ→NMI), latch @ D000,
		   K007232 @ B000 / K051649 @ 9800 stubbed. */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_TECMO16) {
		/* MAME tecmo16: Z80 @ 4 MHz, YM2151 @ FC04, OKI @ FC00, latch @ FC08→NMI. */
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
		pcmKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_FLSTORY) {
		/* MAME flstory: Z80 @ 4 MHz, YM2149 @ 2 MHz mapped C800.
		   MSM5232 @ CA00 is the main melody; AY handles noise/FX/DAC traffic. */
		cpuHz_ = 4000000;
		opmHz_ = 2000000;
		chip_ = CEmuChipAyCreate(2000000u, sampleRate_);
		chip2_ = CEmuChipMsm5232Create(2000000u, sampleRate_);
		pcm_ = NULL;
		pcmKind_ = 0;
		auxKind_ = 4; /* MSM5232 destroy */
		flstoryNmiEn_ = 0;
	} else if (board_ == CEMU_AC_BOARD_TERRACRE) {
		/* terracre: YM3526 @4 MHz, RAM C000, latch I/O 04/06.
		   armedf/terraf: YM3812 @4 MHz, RAM F800-FFFF, same I/O ports;
		   host command is ((cmd&0x7f)<<1)|1 (MAME sound_command_w). */
		terracreMap_ = CEmuAcIsArmedfSub(ge->subtype) ? 1 : 0;
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_ROBOKID) {
		/* MAME ninjakd2/robokid: Z80 @ 5 MHz, dual YM2203 @ 1.5 MHz on
		   I/O 00/01 and 80/81; latch @ E000. */
		cpuHz_ = 5000000;
		opmHz_ = 1500000;
		chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
		mainIsYm2203_ = 1;
		chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
		auxKind_ = 0; /* chip2 is OPN — Render sums via Chip2 path */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_BATTLANTIS) {
		/* MAME battlnts: Z80 + dual YM3812 @ A000 / C000, latch @ E000→IRQ0. */
		cpuHz_ = 3579545;
		opmHz_ = 3000000;
		chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
		chip2_ = CEmuChipYm3812Create(3000000u, sampleRate_);
		auxKind_ = 3; /* OPL destroy for chip2 */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* Seibu sound: SEI80BU encrypted Z80 + YM3812 + OKI6295. */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
		pcmKind_ = 2;
		seibuEnc_ = 1;
		/* Raiden FM table is at 0x80|catalog; cupsoc/others use fixed 0x8e. */
		seibuSongOr80_ = (ge && ge->archive
			&& _strnicmp(ge->archive, "raiden", 6) == 0) ? 1 : 0;
	} else if (board_ == CEMU_AC_BOARD_SEGA_SCSP) {
		/* Early Model 2 / Model 1 sound dumps (daytona/vf) are MultiPCM+YM3438.
		   Model 2A/3 use SCSP. Detect MultiPCM via subtype model2 (not model2a). */
		const int m1 = (ge && ge->subtype && _stricmp(ge->subtype, "model2") == 0) ? 1 : 0;
		segaM1Audio_ = m1;
		if (m1) {
			cpuHz_ = 10000000;
			opmHz_ = 8000000;
			chip_ = CEmuChipYm2612Create(8000000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = CEmuChipMultiPcmCreate(10000000u, sampleRate_, 0);
			pcm2_ = CEmuChipMultiPcmCreate(10000000u, sampleRate_, 1);
			pcmKind_ = 10;
		} else {
			cpuHz_ = 22579200 / 2;
			opmHz_ = 22579200;
			chip_ = CEmuChipScspCreate(22579200u, sampleRate_);
			chip2_ = NULL;
			pcm_ = NULL;
			pcmKind_ = 0;
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		/* Hornet / GTI Club RF5C400 — wave ROM held; silent (no 68K host). */
		cpuHz_ = 16000000;
		opmHz_ = 18432000;
		chip_ = CEmuChipRf5c400Create(18432000u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		const int isK054539 = (_stricmp(ge->subtype, "054539") == 0
			|| _stricmp(ge->subtype, "054539x2") == 0 || hasK054539) ? 1 : 0;
		/* K053260 (parodius/simpsons/…): Z80+YM2151+K053260 @ 3.579545 MHz.
		   Catalog defaults YM@F800 / PCM@FC00 (FA00 on ssriders-class).
		   Bucky/Moo/X-Men K054539: YM@EC00, PCM@E000, bank@F800, K054321@F000. */
		cpuHz_ = isK054539 ? 8000000 : 3579545;
		opmHz_ = isK054539 ? 4000000 : 3579545;
		konamiOpmAddr_ = CEmuAcOptionValue(ge, "opm_addr", isK054539 ? 0xec00u : 0xf800u);
		konamiPcmAddr_ = CEmuAcOptionValue(ge, "pcm_addr", isK054539 ? 0xe000u : 0xfc00u);
		konamiBankAddr_ = CEmuAcOptionValue(ge, "bank_addr", isK054539 ? 0xf800u : 0u);
		konamiPcmWindow_ = isK054539 ? 0x230u : 0x40u;
		konamiSh1NmiArm_ = 0;
		if (isK054539 || konamiBankAddr_) {
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
		}
		if (hasOpm || isK054539) {
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			if (isK054539) {
				pcm_ = CEmuChipK054539Create(18432000u, sampleRate_);
				pcmKind_ = 4;
			} else {
				pcm_ = CEmuChipK053260Create(3579545u, sampleRate_);
				pcmKind_ = 3;
			}
		} else {
			chip_ = CEmuChipK053260Create(3579545u, sampleRate_);
			pcmKind_ = 3;
		}
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_SYS32) {
		/* MAME segas32: Z80 @ 8 MHz, dual YM3438 @80/90, RF5C68 @C000-DFFF,
		   shared RAM E000-FFFF. */
		cpuHz_ = 8000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		mainIsYm2612_ = 1;
		chip2_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		auxKind_ = 5;
		pcm_ = CEmuChipRf5c68Create(10000000u, sampleRate_);
		pcmKind_ = 5;
	} else if (board_ == CEMU_AC_BOARD_OUTRUN || board_ == CEMU_AC_BOARD_ABURNER) {
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipSegaPcmCreate(4000000u, sampleRate_, 12u, 0x70u);
		pcmKind_ = 1;
	} else {
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		if (board_ == CEMU_AC_BOARD_CPS1) {
			pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
			pcmKind_ = 2;
		} else if (hasSegaPcm) {
			pcm_ = CEmuChipSegaPcmCreate(4000000u, sampleRate_, 12u, 0x70u);
			pcmKind_ = 1;
		}
	}
	cpu_ = new Ay_Cpu();
	opmWrites_ = 0;
	return (chip_ && cpu_) ? 1 : 0;
}

void CHardAc::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	if (CEmuM68kBusGetMs1() == this)
		CEmuM68kBusSetMs1(NULL);
	if (CEmuV35BusGetM92() == this)
		CEmuV35BusSetM92(NULL);
	if (CEmuH6280BusGetDeco() == this)
		CEmuH6280BusSetDeco(NULL);
	if (CEmuH8BusGetAc() == this)
		CEmuH8BusSetAc(NULL);
	if (CEmuM37702BusGetAc() == this)
		CEmuM37702BusSetAc(NULL);
	if (CEmuHD63701BusGetAc() == this)
		CEmuHD63701BusSetAc(NULL);
	if (namcoM6809_) { free(namcoM6809_); namcoM6809_ = NULL; }
	if (m6803_) { free(m6803_); m6803_ = NULL; }
	if (hd63701_) { HD63701Destroy(hd63701_); hd63701_ = NULL; }
	if (hd63701Rom_) { free(hd63701Rom_); hd63701Rom_ = NULL; hd63701RomSize_ = 0; }
	if (h8_) { H8Destroy(h8_); h8_ = NULL; }
	if (m37702_) { M37702Destroy(m37702_); m37702_ = NULL; }
	if (h8Rom_) { free(h8Rom_); h8Rom_ = NULL; h8RomSize_ = 0; }
	if (m37702IntRom_) { free(m37702IntRom_); m37702IntRom_ = NULL; m37702IntRomSize_ = 0; }
	if (h8Shared_) { free(h8Shared_); h8Shared_ = NULL; }
	if (m37702LocalRam_) { free(m37702LocalRam_); m37702LocalRam_ = NULL; }
	if (h6280_) { H6280Destroy(h6280_); h6280_ = NULL; }
	if (m6502_) { M6502Destroy(m6502_); m6502_ = NULL; }
	if (decoRom_) { free(decoRom_); decoRom_ = NULL; decoRomSize_ = 0; }
	if (decoRam_) { free(decoRam_); decoRam_ = NULL; }
	if (v35_) { V35Destroy(v35_); v35_ = NULL; }
	if (m92Rom_) { free(m92Rom_); m92Rom_ = NULL; m92RomSize_ = 0; }
	if (m92Ram_) { free(m92Ram_); m92Ram_ = NULL; }
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (pcm2_) {
		CEmuAcDestroyPcmChip(this, pcm2_);
		pcm2_ = NULL;
	}
	if (pcm_) {
		CEmuAcDestroyPcmChip(this, pcm_);
		pcm_ = NULL;
	}
	if (chip3_) {
		CEmuAcDestroyAuxChip(this, chip3_);
		chip3_ = NULL;
	}
	if (chip2_) {
		CEmuAcDestroyAuxChip(this, chip2_);
		chip2_ = NULL;
	}
	if (chip_) {
		CEmuAcDestroyMainChip(this, chip_);
		chip_ = NULL;
	}
	if (alphaOpll_) {
		OPLL_delete((OPLL*)alphaOpll_);
		alphaOpll_ = NULL;
	}
	if (pcmRom_) {
		free(pcmRom_);
		pcmRom_ = NULL;
		pcmRomSize_ = 0;
	}
	if (pcmRom2_) {
		free(pcmRom2_);
		pcmRom2_ = NULL;
		pcmRom2Size_ = 0;
	}
	if (soundRom_) {
		free(soundRom_);
		soundRom_ = NULL;
		soundRomSize_ = 0;
	}
	if (qsKabukiData_) {
		free(qsKabukiData_);
		qsKabukiData_ = NULL;
	}
	qsKabuki_ = 0;
	if (ms1Rom_) {
		free(ms1Rom_);
		ms1Rom_ = NULL;
		ms1RomSize_ = 0;
	}
	if (ms1Ram_) {
		free(ms1Ram_);
		ms1Ram_ = NULL;
	}
}

void CHardAc::AlphaMixOpll(int16_t* stereo, int frames)
{
	if (!stereo || frames <= 0 || !alphaOpll_) return;
	OPLL* o = (OPLL*)alphaOpll_;
	for (int i = 0; i < frames; i++) {
		const int s = (int)(OPLL_calc(o) << 1);
		int l = (int)stereo[i * 2] + s;
		int r = (int)stereo[i * 2 + 1] + s;
		if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
		if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
}

/* ---- Taito TC0140SYT / PC060HA (MAME src/mame/shared/taitosnd.cpp) ---- */

uint8_t CHardAc::KonamiAyTimer() const
{
	/* MAME timeplt_a / scramble portB_r: bi-quinary /5120 from sound CPU
	   total_cycles. Prefer Z80 time() when the core is running so the music
	   wait-for-bit7 loop advances even if AddCpuCycles lags. */
	static const uint8_t kTimer[10] = {
		0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0
	};
	const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time() : cpuCycles_;
	return kTimer[(cyc / 512ull) % 10ull];
}

uint8_t CHardAc::Gx400PortA() const
{
	/* MAME gx400_state::nemesis_portA_r — bit2 toggles as the missing
	   68000 "frame" clock so the sound mainloop (wait clear→set @029C)
	   can run; bits4/6/7 stay high (0xD0). */
	const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time() : cpuCycles_;
	return (uint8_t)(((cyc / 512ull) & 0x0full) | 0xd0u);
}

void CHardAc::SytUpdateNmi()
{
	/* PORT01_FULL = 0x01, PORT23_FULL = 0x02 in MAME's status bitfield. */
	const int pending = (sytStatus_ & 0x03) != 0;
	if (pending && sytNmiEnabled_)
		irqPulse_ = 1;
}

void CHardAc::SytMasterWriteCommand(uint8_t cmd)
{
	/* Master side: port_w(0), comm_w(lo nibble), port_w(1), comm_w(hi nibble).
	   The 68000 driver in every Taito YM2610 game sends the low nibble first. */
	sytMainMode_ = 0;
	sytSlaveData_[0] = (uint8_t)(cmd & 0x0f);
	sytMainMode_ = 1;
	sytSlaveData_[1] = (uint8_t)((cmd >> 4) & 0x0f);
	sytMainMode_ = 2;
	sytStatus_ |= 0x01; /* PORT01_FULL */
	SytUpdateNmi();
}

void CHardAc::SytSlavePortW(uint8_t data)
{
	sytSubMode_ = (uint8_t)(data & 0x0f);
}

void CHardAc::SytSlaveCommW(uint8_t data)
{
	data &= 0x0f;
	switch (sytSubMode_) {
	case 0x00:
		sytMasterData_[0] = data;
		sytSubMode_ = 1;
		break;
	case 0x01:
		sytMasterData_[1] = data;
		sytSubMode_ = 2;
		sytStatus_ |= 0x04; /* PORT01_FULL_MASTER */
		break;
	case 0x02:
		sytMasterData_[2] = data;
		sytSubMode_ = 3;
		break;
	case 0x03:
		sytMasterData_[3] = data;
		sytSubMode_ = 4;
		sytStatus_ |= 0x08; /* PORT23_FULL_MASTER */
		break;
	case 0x04:
		break;
	case 0x05:
		sytNmiEnabled_ = 0;
		SytUpdateNmi();
		break;
	case 0x06:
		sytNmiEnabled_ = 1;
		SytUpdateNmi();
		break;
	default:
		break;
	}
}

uint8_t CHardAc::SytSlaveCommR()
{
	uint8_t res = 0;
	switch (sytSubMode_) {
	case 0x00:
		res = sytSlaveData_[0];
		sytSubMode_ = 1;
		break;
	case 0x01:
		res = sytSlaveData_[1];
		sytStatus_ = (uint8_t)(sytStatus_ & ~0x01);
		sytSubMode_ = 2;
		soundCmdPending_ = 0;
		SytUpdateNmi();
		break;
	case 0x02:
		res = sytSlaveData_[2];
		sytSubMode_ = 3;
		break;
	case 0x03:
		res = sytSlaveData_[3];
		sytStatus_ = (uint8_t)(sytStatus_ & ~0x02);
		sytSubMode_ = 4;
		SytUpdateNmi();
		break;
	case 0x04:
		/* MAME clears the *_FULL_MASTER bits in master_comm_r, and the Taito
		   68000 drivers poll that mailbox continuously. CEmu runs the sound
		   subsystem alone, so nothing ever drained them and they stuck set ?
		   Rastan tests bit 2 at 01E4 and bailed out of its handshake on every
		   iteration. Report the master as having already consumed them. */
		sytStatus_ = (uint8_t)(sytStatus_ & ~0x0c);
		res = sytStatus_;
		break;
	default:
		break;
	}
	return res;
}

void CHardAc::SetBank(int bank)
{
	if (!soundRom_) return;
	/* Sys16A/B bank via I/O port 40 (5358 / 5797), not this 16K window helper.
	   Default bankBase_=0x4000 would clobber fixed ROM 4000-7FFF. */
	if (board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
		return;
	/* CPS2 QSound: Z80 8000-BFFF banks from audiocpu+0x8000 (FBNeo layout;
	   MAME leaves a hole at 8000-FFFF in the region so the same bytes sit at +0x10000). */
	if (board_ == CEMU_AC_BOARD_CPS_QS) {
		if (soundRomSize_ <= 0x8000u) return;
		const unsigned banks = (soundRomSize_ - 0x8000u) / 0x4000u;
		if (banks == 0) return;
		bank_ = bank % (int)banks;
		const unsigned src = 0x8000u + (unsigned)bank_ * 0x4000u;
		unsigned n = 0x4000u;
		if (src + n > soundRomSize_) n = soundRomSize_ - src;
		memset(mem_ + 0x8000, 0xff, 0x4000);
		if (n) memcpy(mem_ + 0x8000, soundRom_ + src, n);
		return;
	}
	/* K053260 Simpsons-class: MAME audiobank entries 0..2 → +0x10000,
	   3..7 → +0x10000 + (i-2)*0x4000. */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && pcmKind_ == 3 && konamiBankAddr_) {
		if (soundRomSize_ <= 0x10000u) return;
		const int b = bank & 7;
		const unsigned src = 0x10000u
			+ ((b <= 2) ? 0u : (unsigned)(b - 2) * 0x4000u);
		if (src >= soundRomSize_) return;
		if (b == bank_ && bankLoaded_) return;
		bank_ = b;
		bankLoaded_ = 1;
		unsigned n = 0x4000u;
		if (src + n > soundRomSize_) n = soundRomSize_ - src;
		memset(mem_ + 0x8000, 0xff, 0x4000);
		if (n) memcpy(mem_ + 0x8000, soundRom_ + src, n);
		return;
	}
	/* MAME taito_f2 machine_start: configure_entry(i, base + 0x4000 * (i % banks));
	   aerofgt: soundbank->configure_entries(0, 4, base, 0x8000). Same shape,
	   different window size. */
	if (soundRomSize_ <= bankSize_) return;
	const unsigned banks = soundRomSize_ / bankSize_;
	if (banks == 0) return;
	const int want = bank % (int)banks;
	/* Rastan re-selects the same bank on almost every YM2151 reg-1B write; a
	   16K memcpy per write would dominate the run for no observable effect. */
	if (want == bank_ && bankLoaded_) return;
	bank_ = want;
	bankLoaded_ = 1;
	const unsigned src = (unsigned)bank_ * bankSize_;
	unsigned n = bankSize_;
	if (src + n > soundRomSize_) n = soundRomSize_ - src;
	memset(mem_ + bankBase_, 0xff, bankSize_);
	if (n) memcpy(mem_ + bankBase_, soundRom_ + src, n);
}

void CHardAc::M72PumpSample()
{
	if (!pcm_ || !pcmRom_ || m72SampleAddr_ >= pcmRomSize_) return;
	const uint8_t s = pcmRom_[m72SampleAddr_];
	if (!s) return; /* 00 marks end of sample */
	pcm_->Write(0, s);
	m72SampleAddr_++;
}

/* ---- Jaleco Mega System 1 sound board (MAME megasys1A/B_sound_map) ----
     000000-01FFFF  ROM              (mapped over the whole loaded image)
     040000-040001  soundlatch[0] r  (main -> sound), soundlatch[1] w on sys B
     060000-060001  soundlatch[1] w  (sound -> main), also r on sys B
     080000-080003  YM2151, umask16(0x00ff) -> low byte lane
     0A0000-0A0003  OKI6295 #0 w, 0A0001 r = status
     0C0000-0C0003  OKI6295 #1 w, 0C0001 r = status
     0E0000-0EFFFF  RAM, mirror 0x10000

   Konami System GX (MAME konamigx.cpp gxsndmap) shares Ms1* entry points:
     000000-03FFFF  ROM
     100000-10FFFF  RAM
     200000-2004FF  dual K054539 (hi=chip_#1, lo=pcm_#2)
     300001         TMS57002 data stub
     400000-40001F  K056800 sound side (umask 0x00ff �� odd bytes)
     500000-500001  TMS57002 status / control (IRQ2 clear on bit0 fall)
     580000         nop                                                      */

static unsigned CEmuGxK056800Off(unsigned addr)
{
	/* Byte offset on the low-byte lane �� device offset (addr>>1) & 7. */
	return ((addr >> 1) & 7u);
}

unsigned CHardAc::Ms1Read16(unsigned addr)
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return Sega68Read16(addr);
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		addr &= 0xfffffeu;
		if (addr < 0x040000u) {
			if (!ms1Rom_ || addr + 1 >= ms1RomSize_) return 0xffff;
			return ((unsigned)ms1Rom_[addr] << 8) | ms1Rom_[addr + 1];
		}
		if (addr >= 0x100000u && addr <= 0x10fffeu) {
			if (!ms1Ram_) return 0xffff;
			const unsigned o = addr & 0xffffu;
			return ((unsigned)ms1Ram_[o] << 8) | ms1Ram_[o + 1];
		}
		if (addr >= 0x200000u && addr <= 0x2004feu) {
			/* MAME umask16: word offset selects the register; high byte ��
			   chip_#1, low byte �� pcm_#2. */
			const unsigned off = (addr - 0x200000u) >> 1;
			const unsigned hi = chip_ ? CEmuChipK054539PeekReg(chip_, off) : 0;
			const unsigned lo = pcm_ ? CEmuChipK054539PeekReg(pcm_, off) : 0;
			return (hi << 8) | lo;
		}
		if (addr == 0x500000u)
			return gxTmsStatus_;
		/* K056800 / TMS data only visible on the low byte via Read8. */
		return 0xffff;
	}

	addr &= 0xfffffeu;
	if (addr < 0x040000u) {
		if (!ms1Rom_ || addr + 1 >= ms1RomSize_) return 0xffff;
		return ((unsigned)ms1Rom_[addr] << 8) | ms1Rom_[addr + 1];
	}
	if (addr >= 0x0e0000u) {
		if (!ms1Ram_) return 0xffff;
		const unsigned o = addr & 0xffffu;
		return ((unsigned)ms1Ram_[o] << 8) | ms1Ram_[o + 1];
	}
	const unsigned page = addr & 0xff0000u;
	if (page == 0x040000u || page == 0x060000u) {
		ms1LatchReads_++;
		return ms1LatchIn_;
	}
	if (page == 0x080000u)
		return chip_ ? chip_->ReadStatus() : 0x00;
	if (page == 0x0a0000u)
		return pcm_ ? pcm_->ReadStatus() : 0x00;
	if (page == 0x0c0000u)
		return pcm2_ ? pcm2_->ReadStatus() : 0x00;
	return 0xffff;
}

unsigned CHardAc::Ms1Read8(unsigned addr)
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return Sega68Read8(addr);
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		if (addr < 0x040000u) {
			if (!ms1Rom_ || addr >= ms1RomSize_) return 0xff;
			return ms1Rom_[addr];
		}
		if (addr >= 0x100000u && addr <= 0x10ffffu) {
			if (!ms1Ram_) return 0xff;
			return ms1Ram_[addr & 0xffffu];
		}
		if (addr >= 0x200000u && addr <= 0x2004ffu) {
			const unsigned off = (addr - 0x200000u) >> 1;
			if (addr & 1)
				return pcm_ ? CEmuChipK054539PeekReg(pcm_, off) : 0;
			return chip_ ? CEmuChipK054539PeekReg(chip_, off) : 0;
		}
		if (addr == 0x300001u)
			return 0x00; /* TMS57002 data stub */
		if (addr >= 0x400000u && addr <= 0x40001fu && (addr & 1)) {
			const unsigned r = CEmuGxK056800Off(addr);
			if (r < 4) return k056800Host_[r];
			return 0;
		}
		if (addr == 0x500000u || addr == 0x500001u)
			return gxTmsStatus_;
		return 0xff;
	}
	const unsigned w = Ms1Read16(addr & ~1u);
	return (addr & 1) ? (w & 0xff) : ((w >> 8) & 0xff);
}

void CHardAc::Ms1Write16(unsigned addr, uint16_t v)
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_) {
		Sega68Write16(addr, v);
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		addr &= 0xfffffeu;
		if (addr >= 0x100000u && addr <= 0x10fffeu) {
			if (!ms1Ram_) return;
			const unsigned o = addr & 0xffffu;
			ms1Ram_[o] = (uint8_t)(v >> 8);
			ms1Ram_[o + 1] = (uint8_t)v;
			return;
		}
		if (addr < 0x040000u) return;
		if (addr >= 0x200000u && addr <= 0x2004feu) {
			const unsigned off = (addr - 0x200000u) >> 1;
			if (chip_) chip_->Write(off, (v >> 8) & 0xff);
			if (pcm_) pcm_->Write(off, v & 0xff);
			gxPcmWrites_++;
			return;
		}
		if (addr == 0x500000u) {
			/* Low byte is the control word (umask often 0x00ff). */
			const uint8_t data = (uint8_t)(v & 0xff);
			if (!(data & 1)) {
				if (chip_) chip_->AckIrq();
			}
			gxSoundCtrl_ = data;
			return;
		}
		return;
	}

	addr &= 0xfffffeu;
	if (addr >= 0x0e0000u) {
		if (!ms1Ram_) return;
		const unsigned o = addr & 0xffffu;
		ms1Ram_[o] = (uint8_t)(v >> 8);
		ms1Ram_[o + 1] = (uint8_t)v;
		return;
	}
	if (addr < 0x040000u) return; /* ROM */
	const unsigned page = addr & 0xff0000u;
	if (page == 0x040000u || page == 0x060000u) {
		ms1LatchOut_ = v; /* to main CPU ? nothing listens here */
		return;
	}
	if (page == 0x080000u && chip_) {
		/* 080001 = address latch, 080003 = data (low byte lane). */
		chip_->Write((addr & 2) ? 1 : 0, v & 0xff);
		if (addr & 2)
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		return;
	}
	if (page == 0x0a0000u && pcm_) {
		pcm_->Write(0, v & 0xff);
		ms1OkiWrites_++;
		return;
	}
	if (page == 0x0c0000u && pcm2_) {
		pcm2_->Write(0, v & 0xff);
		ms1OkiWrites_++;
		return;
	}
}

void CHardAc::Ms1Write8(unsigned addr, uint8_t v)
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_) {
		Sega68Write8(addr, v);
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		if (addr >= 0x100000u && addr <= 0x10ffffu) {
			if (!ms1Ram_) return;
			ms1Ram_[addr & 0xffffu] = v;
			return;
		}
		if (addr < 0x040000u) return;
		if (addr >= 0x200000u && addr <= 0x2004ffu) {
			const unsigned off = (addr - 0x200000u) >> 1;
			if (addr & 1) {
				if (pcm_) pcm_->Write(off, v);
			} else {
				if (chip_) chip_->Write(off, v);
			}
			gxPcmWrites_++;
			return;
		}
		if (addr == 0x300001u)
			return; /* TMS57002 data stub */
		if (addr >= 0x400000u && addr <= 0x40001fu && (addr & 1)) {
			const unsigned r = CEmuGxK056800Off(addr);
			if (r < 2) {
				k056800Snd_[r] = v;
			} else if (r == 4) {
				k056800IntEn_ = (v & 1) != 0;
				if (k056800IntEn_) {
					if (k056800Pending_)
						k056800Irq_ = 1;
				} else {
					k056800Pending_ = 0;
					k056800Irq_ = 0;
				}
			}
			return;
		}
		if (addr == 0x500001u || addr == 0x500000u) {
			if (!(v & 1)) {
				if (chip_) chip_->AckIrq();
			}
			gxSoundCtrl_ = v;
			return;
		}
		return;
	}
	if (addr >= 0x0e0000u) {
		if (!ms1Ram_) return;
		ms1Ram_[addr & 0xffffu] = v;
		return;
	}
	if (addr < 0x040000u) return;
	/* Every peripheral here sits on the low byte lane; an even-address byte
	   write lands on D15-D8 and is decoded by nothing. */
	if (addr & 1)
		Ms1Write16(addr & ~1u, v);
}

int CHardAc::Ms1IrqLevel() const
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return segaMidiIrq_ ? 2 : 0; /* 8251 RxRDY → IRQ2 */
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		int level = 0;
		if (k056800Irq_) level = 1;
		/* Timer �� IRQ2 only while control bit0 is set (MAME k054539_irq_gen). */
		if (chip_ && chip_->Irq() && (gxSoundCtrl_ & 1) && level < 2)
			level = 2;
		return level;
	}
	int level = ms1LatchIrq_ ? ms1LatchLevel_ : 0;
	/* MAME sound_irq(): YM2151 -> set_input_line(4, HOLD_LINE). */
	if (chip_ && chip_->Irq() && level < 4) level = 4;
	return level;
}

void CHardAc::Ms1AckIrq()
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return; /* RxRDY cleared when UART FIFO drains */
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		/* IRQ1 (K056800) is cleared by sound_w(4); IRQ2 (K054539 timer) is
		   cleared when the ISR writes $500001 with bit0 clear. Do not drop
		   either line on Musashi IACK ? that stole timer IRQs while mailbox
		   IRQs were pending. */
		return;
	}
	ms1LatchIrq_ = 0;
	if (chip_ && chip_->Irq()) chip_->AckIrq();
}

/* ---- Irem M92 sound board (MAME irem/m92.cpp sound_map) ----------------
   The peripherals are all decoded on the low byte lane (umask16 0x00ff), so a
   word access reaches the device through its even address and the odd byte is
   dropped on the floor ? modelled here by ignoring odd peripheral bytes. */
uint8_t CHardAc::M92Read8(uint32_t addr)
{
	addr &= 0xfffffu;
	if (addr < 0x20000u)
		return (m92Rom_ && addr < m92RomSize_) ? m92Rom_[addr] : 0xff;
	if (addr >= 0xa0000u && addr <= 0xa3fffu)
		return m92Ram_ ? m92Ram_[addr - 0xa0000u] : 0xff;
	if (addr >= 0xa8000u && addr <= 0xa803fu) {
		/* iremga20_device::read ? offset&7 == 7 is the voice-active flag. */
		if (addr & 1) return 0xff;
		{
			const unsigned off = (addr - 0xa8000u) >> 1;
			if ((off & 7u) == 7u && pcm_)
				return (uint8_t)((pcm_->ReadStatus() >> (off >> 3)) & 1u);
		}
		return 0x00;
	}
	if (addr >= 0xa8040u && addr <= 0xa8043u) {
		if (addr & 1) return 0xff;
		return chip_ ? chip_->ReadStatus() : 0x00;
	}
	if (addr == 0xa8044u) {
		m92LatchReads_++;
		return m92Latch_;
	}
	/* FFFF0-FFFFF mirrors the top of the sound ROM so the reset vector lands
	   inside the IDB page without colliding with the SFRs. */
	if (addr >= 0xffff0u)
		return (m92Rom_ && m92RomSize_ >= 0x20000u) ? m92Rom_[0x1fff0u + (addr & 0xfu)] : 0xff;
	return 0xff;
}

void CHardAc::M92Write8(uint32_t addr, uint8_t v)
{
	addr &= 0xfffffu;
	if (addr >= 0xa0000u && addr <= 0xa3fffu) {
		if (m92Ram_) m92Ram_[addr - 0xa0000u] = v;
		return;
	}
	if (addr < 0x20000u)
		return; /* ROM */
	if (addr >= 0xa8000u && addr <= 0xa803fu) {
		if (addr & 1) return;
		if (pcm_) {
			pcm_->Write((addr - 0xa8000u) >> 1, v);
			m92Ga20Writes_++;
		}
		return;
	}
	if (addr >= 0xa8040u && addr <= 0xa8043u) {
		if (addr & 1) return;
		if (chip_) {
			chip_->Write((addr == 0xa8040u) ? 0 : 1, v);
			if (addr != 0xa8040u)
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		}
		return;
	}
	if (addr == 0xa8044u) {
		/* generic_latch_8 acknowledge_w ? drops the INTP1 request. */
		m92LatchPending_ = 0;
		if (v35_) V35SetInputLine(v35_, V35_LINE_INTP1, V35_CLEAR_LINE);
		return;
	}
	if (addr == 0xa8046u) {
		m92Latch2_ = v;
		return;
	}
}

int CHardAc::M92PatchRom(uint32_t off, uint8_t v)
{
	if (!m92Rom_ || off >= m92RomSize_) return 0;
	m92Rom_[off] = v;
	return 1;
}

void CHardAc::M92SyncIrqs()
{
	if (!v35_) return;
	/* ym2151.irq_handler -> NEC_INPUT_LINE_INTP0 as a LEVEL that tracks the
	   chip's timer status bits (not our edge latch). Acking the latch here
	   before the ISR ran made AND AL,#03 see a clear status and skip the
	   sequencer; leaving the latch stuck ASSERT'd flooded INTP0 forever. */
	if (chip_) {
		const int pending = (chip_->ReadStatus() & 0x03) != 0;
		V35SetInputLine(v35_, V35_LINE_INTP0,
			pending ? V35_ASSERT_LINE : V35_CLEAR_LINE);
		if (!pending)
			chip_->AckIrq();
	}
	/* soundlatch data_pending -> NEC_INPUT_LINE_INTP1; held until the driver
	   writes A8044 (separate_acknowledge). */
	V35SetInputLine(v35_, V35_LINE_INTP1,
		m92LatchPending_ ? V35_ASSERT_LINE : V35_CLEAR_LINE);
}

/* Peek Sys2 song-table record type (first byte at the banked pointer).
   Type $20 = bit6-mailbox BGM path; other types need status without bit6. */
static int CEmuAcSys2SongRecType(const uint8_t* rom, unsigned romSize, unsigned songLo);

void CHardAc::SetSoundCommandWord(uint16_t cmd)
{
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		/* System GX mailbox: banked titles 0x01xx..0x05xx are (lo, hi);
		   others (e.g. 0x06xx voices on tkmmpzdm) are (hi, lo). */
		const uint8_t hi = (uint8_t)(cmd >> 8);
		const uint8_t lo = (uint8_t)(cmd & 0xff);
		if (hi >= 0x01 && hi <= 0x05)
			GxHostInject(lo, hi, 0, 0);
		else
			GxHostInject(hi, lo, 0, 0);
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && H8Active()) {
		H8InjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && M37702Active()) {
		M37702InjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && m37702Soft_) {
		M37702InjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && !segaM1Audio_) {
		SoftPcmInjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		SoftPcmInjectSong(cmd);
		return;
	}
	if ((board_ == CEMU_AC_BOARD_NAMCO_SYS86
			|| (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsg63701_))
		&& HD63701Active()) {
		HD63701InjectSong((uint8_t)(cmd & 0xffu));
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 || board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		/* soundCmdWord_ already holds the full 16-bit id; SetSoundCommand
		   posts lo/hi from soundCmdWord_ (do not wipe it). */
		SetSoundCommand((uint8_t)(cmd & 0xff));
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_) {
		SegaMidiInjectSong(cmd);
		return;
	}
	/* Capcom ZN: PSX sends FF 00 (voice reset) then the BE song word. */
	if (board_ == CEMU_AC_BOARD_CPS_QS && qsZn_) {
		znQueue_[0] = 0xff;
		znQueue_[1] = 0x00;
		znQueue_[2] = (uint8_t)(cmd >> 8);
		znQueue_[3] = (uint8_t)(cmd & 0xff);
		znQueueLen_ = 4;
		znQueuePos_ = 0;
		znDeferredNmi_ = 0;
		soundCmd_ = znQueue_[0];
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && pcmKind_ == 3) {
		soundCmdWord_ = cmd;
		soundCmd_ = (uint8_t)(cmd & 0xff);
		soundCmdPending_ = 1;
		CChip* pcm = pcm_ ? pcm_ : chip_;
		if (pcm) {
			CEmuChipK053260MainWrite(pcm, 0, (uint8_t)(cmd & 0xff));
			CEmuChipK053260MainWrite(pcm, 1, (uint8_t)(cmd >> 8));
		}
		irqPulse_ = 1;
		return;
	}
	SetSoundCommand((uint8_t)(cmd & 0xff));
}

void CHardAc::GxHostInject(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
	k056800Host_[0] = b0;
	k056800Host_[1] = b1;
	k056800Host_[2] = b2;
	k056800Host_[3] = b3;
	k056800Pending_ = 1;
	if (k056800IntEn_)
		k056800Irq_ = 1;
	soundCmdPending_ = 1;
}

void CHardAc::SetSoundCommand(uint8_t cmd)
{
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		SetSoundCommandWord(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_MEGASYSTEM1) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		ms1LatchIn_ = cmd;
		ms1LatchIrq_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_IREM_M92) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		m92Latch_ = cmd;
		m92LatchPending_ = 1;
		if (v35_) V35SetInputLine(v35_, V35_LINE_INTP1, V35_ASSERT_LINE);
		return;
	}
	if (board_ == CEMU_AC_BOARD_DECO) {
		/* deco_146 / generic_latch_8: pending → HuC6280 IRQ1 or M6502 NMI. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (h6280_) H6280SetInputLine(h6280_, H6280_LINE_IRQ1, H6280_ASSERT_LINE);
		if (m6502_) M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_ASSERT_LINE);
		return;
	}
	if (board_ == CEMU_AC_BOARD_ATARI_SYS1) {
		/* atarijsa: soundlatch → 6502 NMI. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (m6502_) M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_ASSERT_LINE);
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS86 && HD63701Active()) {
		HD63701InjectSong((uint8_t)(cmd & 0xffu));
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsg63701_ && HD63701Active()) {
		HD63701InjectSong((uint8_t)(cmd & 0xffu));
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 || board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		/* TRI-RAM mailbox:
		   - blazer: cmd in $7100, status bit7 on $7101
		   - rompers: 2-byte slots; $7101 must have bits 5-6 set or IRQ
		     handler skips the D1AC path that loads cmd from $7100
		   - pacmania: similar $7101 handshake
		   - Sys2 (finallap/assault): 16-bit codes use $7100=lo, $7101=hi|0x60
		   - burnforc-class: same protocol at $7110 (namcoMailOff_=0x110) */
		if (!soundCmdWord_)
			soundCmdWord_ = cmd;
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		const uint16_t w = soundCmdWord_;
		const unsigned mail = namcoMailOff_ & 0x7feu;
		namcoTriRam_[mail] = (uint8_t)(w & 0xffu);
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && (w >> 8)) {
			/* 16-bit: $7100=lo, $7101=(hi&0x1F)|flag. Do NOT seed $7102/$7103
			   with flag — assault's slot walker treats that as song 0 and kills BGM. */
			const uint8_t fl = NamcoMailFlag();
			namcoTriRam_[mail + 1u] = (uint8_t)(((w >> 8) & 0x1fu) | fl);
		} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
			/* Status = FLAGS only (never OR song id — ids like 0x4C already
			   have bit6 and falsely trip the type-$20 BGM gate).
			   Type $20 records want bit6; type $64/other use the non-bit6 path. */
			uint8_t fl = NamcoMailFlag();
			const int rec = CEmuAcSys2SongRecType(soundRom_, soundRomSize_, w & 0xffu);
			/* Only strip bit6 for known non-$20 records (e.g. $64). If the
			   table probe fails, keep the default flag so type-$20 BGMs still
			   enter the bit6 path (phelios 0x1F). */
			if (rec == 0x64)
				fl = (uint8_t)(fl & (uint8_t)~0x40u);
			namcoTriRam_[mail + 1u] = fl;
		} else {
			namcoTriRam_[mail + 1u] = (uint8_t)((w & 0x7fu) | 0x60u);
		}
		/* Also seed Sys2 work-RAM mirror some titles poll after DPRAM copy. */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
			namcoWorkRam_[0x1016 & 0x1fff] = (uint8_t)(w & 0xffu);
			namcoWorkRam_[0x1017 & 0x1fff] = (uint8_t)(w >> 8);
			namcoWorkRam_[0x101a & 0x1fff] = (uint8_t)(w & 0xffu);
			namcoWorkRam_[0x101b & 0x1fff] = (uint8_t)(w >> 8);
			namcoWorkRam_[0x101c & 0x1fff] = (uint8_t)(w & 0xffu);
			namcoWorkRam_[0x101d & 0x1fff] = (uint8_t)(w >> 8);
		}
		/* Defer IRQ until I is clear - an early pulse during RAM-test (I=1)
		   sits pending and fires on the first CLI, wiping A before $D007. */
		if (namcoM6809_ && !NamcoCpuRaw(namcoM6809_)->cc.i) {
			namcoIrqAssert_ = 1;
			NamcoCpuRaw(namcoM6809_)->irq = true;
		} else {
			namcoIrqAssert_ = 1; /* SyncIrqs will pulse after CLI */
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && H8Active()) {
		H8InjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		/* Dig Dug: song bits in $9A80+; Galaga: main writes id at $9211 with
		   $9AA0 enable; Bosco: one-shot flags in $8Axx. Do NOT poke Bosco
		   $8Axx on Dig Dug/Galaga — those bytes are digdug work/SE state. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (!cmd) cmd = 1;
		if (wsgMappy_) {
			/* MAME mappy/superpac: main CPU posts song id into namco_15xx
			   shared RAM (amap $040-$3FF). Slot differs by title — seed the
			   common poll addresses used across the Super Pac / Mappy set. */
			/* Poll slots only — $80+ is the IRQ mailbox on digdug2/mappy;
			   stuffing song ids there corrupts the vblank path. */
			static const uint16_t kSlots[] = {
				0x40, 0x41, 0x50, 0x55, 0x60, 0x61
			};
			CEmuChipC30SetEnable(chip_, 1);
			if (chip_) {
				for (unsigned i = 0; i < sizeof(kSlots) / sizeof(kSlots[0]); i++)
					chip_->Write(kSlots[i], cmd);
			}
			wsgNmiEnable_ = 1;
			namcoIrqAssert_ = 1;
			if (namcoM6809_ && !NamcoCpuRaw(namcoM6809_)->cc.i)
				NamcoCpuRaw(namcoM6809_)->irq = true;
			return;
		}
		const int bosco = (mem_[0x80] == 0x3a && mem_[0x81] == 0x01
			&& mem_[0x82] == 0x8c) ? 1 : 0;
		const int galaga = (mem_[0x8a] == 0x11 && mem_[0x8b] == 0x01
			&& mem_[0x8c] == 0x91) ? 1 : 0;
		mem_[0x9a80] = cmd;
		mem_[0x9a81] = cmd;
		mem_[0x9a82] = 0;
		mem_[0x9a83] = 0;
		mem_[0x9a84] = 0;
		mem_[0x9a85] = 0;
		mem_[0x9a87] = 0;
		mem_[0x9a88] = 0;
		mem_[0x9a91] = 0;
		mem_[0x9a94] = 0;
		mem_[0x9101] = 0; /* galaga boot handshake */
		mem_[0x8c01] = 0; /* bosco boot handshake */
		mem_[0x9b3c] = 0; /* digdug busy */
		mem_[0x9a8c] = 0; /* galaga busy */
		mem_[0x8a01] = 0; /* digdug wait-gate */
		if (galaga) {
			mem_[0x9211] = cmd;
			mem_[0x9aa0] = 1;
		}
		if (bosco) {
			/* One-shot SE/BGM request flags scanned every NMI. */
			mem_[0x8a15] = cmd ? cmd : 1;
			mem_[0x8a1f] = 1;
			mem_[0x8a08] = 1;
			mem_[0x8a10] = 1;
			mem_[0x8a0b] = 1;
			mem_[0x8a14] = 1;
			mem_[0x8a0c] = 1;
			mem_[0x8a09] = 1;
			mem_[0x8a0a] = 1;
			mem_[0x8a0d] = 1;
			/* Voice mirror nibbles some paths copy to $6805/$680A/$680F. */
			mem_[0x8a58] = 0x0a;
			mem_[0x8a59] = 0x0a;
			mem_[0x8a5a] = 0x0a;
		}
		wsgNmiEnable_ = 1;
		irqPulse_ = 1;    /* driver turns this into NMI */
		return;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_TAITO_OPM) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		SytMasterWriteCommand(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		sjLatchFlag_ = 1;
		/* NMI is gated by AY#4 port B bit0 (active low). Until the sound
		   program has written it, MAME's merger holds NMI masked; allow it so
		   an injected command can never be lost on a game that never writes. */
		if (!sjNmiMaskSeen_ || sjNmiMask_)
			irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
		|| board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		/* Scramble: PPI PB bit3 clocks a 7474 �� Z80 INT (IM0 vector 0xFF=RST38).
		   Time Pilot / GX400: host edge �� HOLD_LINE IRQ0. Latch via AY port A
		   (scramble AY2 / timeplt AY1) or mem e001 (gx400). */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		/* soundlatch_w �� NMI; YM2151 IRQ drives the music sequencer. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		/* MAME: host soundlatch write + IRQ0 (IM1). HCastle polls D000;
		   still pulse so boot paths that wait on IF see activity. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		/* MAME: soundlatch write; Z80 reads via IN 00. Music advances on
		   periodic NMI (~7614 Hz) gated by YM2203 port A. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		/* soundlatch → NMI; YM2151 timer IRQ drives BGM. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_FLSTORY) {
		/* MAME flstory: latch pending ∧ DA00 enable → NMI (input_merger ALL_HIGH). */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (flstoryNmiEn_)
			irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE
		|| board_ == CEMU_AC_BOARD_ROBOKID
		|| board_ == CEMU_AC_BOARD_BATTLANTIS) {
		/* Periodic IRQ0 (terracre/armedf) / YM2203 IRQ (robokid) / host IRQ0
		   (battlantis). Latch is polled from the ISR.
		   armedf/terraf: MAME sound_command_w packs ((cmd&0x7f)<<1)|1. */
		if (board_ == CEMU_AC_BOARD_TERRACRE && terracreMap_)
			cmd = (uint8_t)(((cmd & 0x7fu) << 1) | 1u);
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TOAPLAN1) {
		/* Shared-RAM mailbox: (8001)==0xAA releases boot wait; (8000) is the
		   command byte (0xFF idle). No NMI ? Z80 polls 8000 from the Timer-A
		   ISR path after YM3812 interrupts run. */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		mem_[0x8001] = 0xaa;
		mem_[0x8000] = cmd;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (snkMapKind_) {
			/* Classic SNK: latch @ E000; status bit3=pending, bit2=busy.
			   IRQ0 is level-held while (status & 0x0B) != 0 (MAME).
			   Drop stale YM bits so the first IRQ after inject is the latch. */
			mem_[0xe000] = cmd;
			snkStatus_ = (uint8_t)((snkStatus_ & (uint8_t)~0x03u) | 0x0cu);
			irqPulse_ = 1;
			return;
		}
		/* MAME snk68: soundlatch write → NMI; Z80 reads latch at F800.
		   Also deposit into the NMI ring (F152/F133) so the main-loop poll
		   at 00D6 can drain even if a single NMI edge was lost during boot.
		   Lock addr is sniffed from NMI: LD A,(F1xx) at 0068. */
		irqPulse_ = 1;
		mem_[0xf800] = cmd;
		uint16_t idxAddr = 0xf152;
		if (mem_[0x68] == 0x3a && mem_[0x6a] == 0xf1) {
			const uint16_t lockAddr = (uint16_t)(0xf100u | (unsigned)mem_[0x69]);
			idxAddr = (uint16_t)(lockAddr + 1u);
		}
		const uint8_t idx = (uint8_t)((mem_[idxAddr] + 1u) & 0x0fu);
		mem_[idxAddr] = idx;
		mem_[(uint16_t)(idxAddr + idx)] = cmd;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* seibu_sound main_w: bytes 0/1 + RST18 on offset 4.
		   Clear sub2main_pending so RST18 will read the latch (boot's
		   pending_w otherwise leaves (4012)=1 and skips enqueue).
		   Firmware dispatches on D (latch byte1): 0x80 enters the song
		   scan. Table index is E (latch byte0). Catalog BGM ids are the
		   low 7 bits of the table slot — raiden 0x1B→0x9B, cupsoc
		   0x32→0xB2. Coin/invalid 0x80-0x84 fall back to 0x8e. */
		uint8_t idx = cmd;
		if (idx < 0x80)
			idx = (uint8_t)(idx | 0x80u);
		if (idx == 0x80 || idx == 0x81 || idx == 0x82 || idx == 0x84)
			idx = 0x8e;
		seibuMain2Sub_[0] = idx;
		seibuMain2Sub_[1] = 0x80;
		seibuSubPending_ = 0;
		seibuMainPending_ = 1;
		seibuRst18_ = 1;
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_IREM_M62) {
		/* MAME cmd_w: latch; IRQ assert when bit7 clear. Host later writes
		   0x80 so sound_irq_ack_w can CLEAR_LINE (main-CPU handshake). */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (chip_) CEmuChipAySetPortA(chip_, cmd);
		if (!m6803_) return;
		if ((cmd & 0x80) == 0)
			m6800_raise_interrupt(m6803_, IRQ_IRQ1);
		else
			m6800_clear_interrupt(m6803_, IRQ_IRQ1);
		return;
	}
	/* Capcom ZN (MAME zn.cpp): soundlatch → NMI, read at I/O 00.
	   Catalog byte codes expand to FF 00 00 code (same as word path). */
	if (board_ == CEMU_AC_BOARD_CPS_QS && qsZn_) {
		znQueue_[0] = 0xff;
		znQueue_[1] = 0x00;
		znQueue_[2] = 0x00;
		znQueue_[3] = cmd;
		znQueueLen_ = 4;
		znQueuePos_ = 0;
		znDeferredNmi_ = 0;
		soundCmd_ = znQueue_[0];
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	/* CPS2 QSound: 68K writes a 16-byte packet at shared RAM C000 and clears
	   C00F (!=0xFF) as the doorbell. IRQ @0038 copies it when CFFD==0x88.
	   Init spins at 00DD until CFFF==0xFF (68K ready); without a 68K we must
	   release that wait ourselves. Sequencer runs on periodic IM1 ? no NMI.
	   Packet[0] is the sound-code index into the program table at 9006
	   (see LoadRoms patch that replaces the stock code%3 collapse). */
	if (board_ == CEMU_AC_BOARD_CPS_QS) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		mem_[0xcfff] = 0xff;
		mem_[0xcffd] = 0x88;
		memset(mem_ + 0xc000, 0, 16);
		mem_[0xc000] = cmd;
		mem_[0xc00f] = 0x00;
		return;
	}
	/* K053260: main→sound ports 0/1 + sound IRQ (MAME sound_irqtrigger /
	   z80_irq_w). SH1 NMI (FA00/HALT) is separate; command handler is @0038. */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && pcmKind_ == 3) {
		soundCmd_ = cmd;
		soundCmdWord_ = cmd; /* 8-bit path: clear high byte on port 1 */
		soundCmdPending_ = 1;
		CChip* pcm = pcm_ ? pcm_ : chip_;
		if (pcm) {
			CEmuChipK053260MainWrite(pcm, 0, cmd);
			CEmuChipK053260MainWrite(pcm, 1, 0x00);
		}
		irqPulse_ = 1;
		return;
	}
	soundCmd_ = cmd;
	soundCmdPending_ = 1;
	/* Pulse the Z80 line. CheckIrq maps Sys16A→NMI (uPD7759 + latch) and
	   Sys16B→IM1 (latch via the same vector as YM; needed when timers are
	   idle at inject so IN A,(C0) still runs). */
	irqPulse_ = 1;
	/* m99 (bbmanw/poundfor): command reader ANDs latch with F4DC. */
	if (board_ == CEMU_AC_BOARD_IREM_M72 && m72IoAlt_ && mem_[0xf4dc] == 0)
		mem_[0xf4dc] = 0xff;
	/* OutRun ProcessCommand polls (F800). After Burner / Hang-On keep F800?F807 as a
	   free-slot queue (0x80 = empty); NMI writes the latch into a free slot.
	   Do not overwrite those markers. */
	if (board_ == CEMU_AC_BOARD_OUTRUN)
		mem_[0xf800] = cmd;
	/* System 32: main↔Z80 mailbox lives in shared RAM at E000 (not port C0). */
	if (board_ == CEMU_AC_BOARD_SYS32) {
		mem_[0xe000] = cmd;
		mem_[0xe001] = cmd;
		mem_[0xe00f] = 0x00;
	}
}

uint8_t CHardAc::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (board_) {
	case CEMU_AC_BOARD_SYS16A:
	case CEMU_AC_BOARD_SYS16B:
	case CEMU_AC_BOARD_SYS24:
		if (p == 0x00 || p == 0x01) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			/* hwchamp (5521) soft-waits on status bit0 after timer-control
			   writes before Timer A has a real period. Pulse for the first
			   few seconds of CPU time (writeCount alone expires mid-BGM and
			   re-traps the BIT 0,A;JR Z spin → WEAK). */
			if (board_ == CEMU_AC_BOARD_SYS16B && (p & 1) && !(st & 0x01)
				&& chip_ && cpuCycles_ < (uint64_t)cpuHz_ * 4u) {
				const uint64_t period = (uint64_t)cpuHz_ / 256;
				if (period > 0) {
					const uint64_t slot = cpuCycles_ / period;
					if (slot != abStatusPulseSlot_) {
						abStatusPulseSlot_ = slot;
						st |= 0x01;
					}
				}
			}
			return st;
		}
		if ((board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
			&& (p == 0x80 || (p >= 0x80 && p <= 0xbf)))
			return 0x00; /* uPD7759 busy_r << 7 ? not busy */
		if (p == 0xc0) {
			/* Sound latch. When idle, goldnaxe's ISR still IN A,(C0) every
			   YM tick — returning 0 makes 0212/02C7 take the OR A;JP Z,0B56
			   mute-all path (TL=7F forever). 0x80 is the firmware's empty
			   slot marker (02D4) and is ignored by 02C7 (AND #7F;RET Z).
			   Cotton treats 0x80 as a no-match and RETs. */
			if (!soundCmdPending_)
				return 0x80;
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_SYS18:
		if (p >= 0x80 && p <= 0x83)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p >= 0xa0 && p <= 0xa3)
			return chip2_ ? chip2_->ReadStatus() : (chip_ ? chip_->ReadStatus() : 0x00);
		if (p == 0xc0) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_SYS32:
		/* MAME system32_sound_portmap: YM3438 #1 @80-83 / #2 @90-93. */
		if (p >= 0x80 && p <= 0x83)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p >= 0x90 && p <= 0x93)
			return chip2_ ? chip2_->ReadStatus() : (chip_ ? chip_->ReadStatus() : 0x00);
		return 0xff;
	case CEMU_AC_BOARD_OUTRUN:
	case CEMU_AC_BOARD_ABURNER:
		/* MAME segaorun / aburner: YM2151 @ 00/01 (mirror 00-3F), latch @ 40 (mirror 40-7F). */
		if (p < 0x40) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			/* After Burner main loop: IN A,(01); BIT 0,A; JP Z,$0036.
			   Do NOT force bit0 always-on ? that free-runs the music engine at
			   CPU rate (few good notes �� pause �� ultra-fast garbage).
			   Before OPM timers are programmed, pulse ~256 Hz so boot can exit. */
			if (board_ == CEMU_AC_BOARD_ABURNER && (p & 1) && !(st & 0x01)
				&& chip_ && CEmuChipYm2151WriteCount(chip_) < 48) {
				const uint64_t period = (uint64_t)cpuHz_ / 256;
				if (period > 0) {
					const uint64_t slot = cpuCycles_ / period;
					if (slot != abStatusPulseSlot_) {
						abStatusPulseSlot_ = slot;
						st |= 0x01;
					}
				}
			}
			return st;
		}
		if (p < 0x80) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_HANGON:
		/* MAME sound_portmap_2203: only latch @ 40 (mirror 40-7F). No YM on I/O. */
		if (p >= 0x40 && p < 0x80) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_VSYSTEM:
		/* Three Video System I/O layouts share one board:
		   0 aerofgt/gstriker/taotaido: YM 00-03, bank 04, ack 08, latch 0c
		   1 spinlbrk/turbofrc/f1gp/pspikes: bank 00, latch 14, YM 18-1b
		   2 fromanc2/welltris: latch 00/04/10, YM 08-0b, bank 00/18
		   3 Psikyo gunbird: bank 00, YM 04-07, latch 08, ack 0c */
		{
			int ymOff = -1;
			if (vsIoKind_ == 1) {
				if (p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
			} else if (vsIoKind_ == 2) {
				if (p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
			} else if (vsIoKind_ == 3) {
				if (p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
			} else {
				if (p <= 0x03) ymOff = (int)(p & 3);
			}
			/* Tolerate cross-wiring: some dumps still hit the other decode. */
			if (ymOff < 0 && p <= 0x03) ymOff = (int)(p & 3);
			if (ymOff < 0 && p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
			if (ymOff < 0 && p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
			if (ymOff < 0 && p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
			if (ymOff >= 0 && chip_) {
				switch (ymOff & 3) {
				case 0: return chip_->ReadStatus();
				case 1: return chip_->ReadData();
				case 2: return chip_->ReadStatusHi();
				default: return chip_->ReadDataHi();
				}
			}
			if (p == 0x0c || p == 0x14 || p == 0x10 || (vsIoKind_ == 2 && p == 0x00)
				|| (vsIoKind_ == 3 && p == 0x08)) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (vsIoKind_ == 1 && p == 0x14) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
		}
		return 0xff;
	case CEMU_AC_BOARD_IREM_M72:
		/* MAME irem_m72 rtype/rtype2 + poundfor/m99 (YM@40) merged. */
		if (p == 0x00 || p == 0x01 || p == 0x40 || p == 0x41)
			return chip_ ? chip_->ReadStatus() : 0x00;
		/* generic_latch_8 with set_separate_acknowledge(true): reading the
		   latch leaves the RST 18h line asserted until port 06/83/42 acks it. */
		if (p == 0x02 || p == 0x80 || p == 0x42)
			return soundCmd_;
		if (p == 0x84) {
			const uint8_t v = (pcmRom_ && m72SampleAddr_ < pcmRomSize_)
				? pcmRom_[m72SampleAddr_] : 0x00;
			return v;
		}
		return 0xff;
	case CEMU_AC_BOARD_TOAPLAN1:
		/* YM3812 status only on the game's real data/addr ports — DIP/TJUMP
		   ports must return 0x00 (Japan / idle), not sticky timer flags. */
		{
			const uint8_t ym = toaplanYmPort_;
			if (p == ym || p == (uint8_t)(ym + 1u)) {
				uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
				if (toaplanTimerA_) {
					st |= 0x40; /* Timer A flag — ISR music path gate */
					toaplanTimerA_ = 0;
				}
				return st;
			}
		}
		/* DIP / TJUMP / SYSTEM / P1 / P2 — Japan territory, no coins. */
		return 0x00;
	case CEMU_AC_BOARD_SNK_OPL:
		/* I/O 00 = YM3812 status/addr; 20 = data (write-only). Classic = mem. */
		if (snkMapKind_)
			return 0xff;
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_TERRACRE:
		/* latch clear @04, latch read @06. */
		if (p == 0x04) {
			soundCmdPending_ = 0;
			return 0x00;
		}
		if (p == 0x06) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_ROBOKID:
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p == 0x80 || p == 0x81)
			return chip2_ ? chip2_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_ALPHA68K2:
		/* MAME sound_portmap: global_mask 0x0f; latch read @00 (any). */
		soundCmdPending_ = 0;
		return soundCmd_;
	case CEMU_AC_BOARD_CPS1:
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p == 0x02 || p == 0x03)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (p == 0x06) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_CPS_QS:
		/* ZN: I/O 00 = soundlatch. Do NOT pulse NMI from inside this read ?
		   the handler is mid-IN and a nested NMI would store the lo byte then
		   the outer LD (HL),A would overwrite F100 with the hi byte (0). */
		if (qsZn_ && p == 0x00) {
			const uint8_t v = soundCmd_;
			soundCmdPending_ = 0;
			znQueuePos_++;
			if (znQueuePos_ < znQueueLen_) {
				soundCmd_ = znQueue_[znQueuePos_];
				soundCmdPending_ = 1;
				znDeferredNmi_ = 1;
			}
			return v;
		}
		if (p == 0x00 || p == 0x01 || p == 0x02)
			return chip_ ? chip_->ReadStatus() : 0x80;
		if (p == 0x06 || p == 0x08) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_GNG:
		/* GNG is memory-mapped; I/O unused. */
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_PCM:
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_SCRAMBLE:
		/* MAME scramble_sound_io_map: AY1 @10/20, AY2 @40/80.
		   AY2 port A = latch, port B = timer. */
		if (p == 0x20)
			return chip_ ? chip_->ReadData() : 0xff;
		if (p == 0x80) {
			const uint8_t a = (uint8_t)(ayAddr_[1] & 0x0f);
			if (a == 0x0e) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (a == 0x0f)
				return KonamiAyTimer();
			return chip2_ ? chip2_->ReadData() : 0xff;
		}
		return 0xff;
	case CEMU_AC_BOARD_NAMCO_C352:
		return chip_ ? chip_->ReadStatus() : 0x00;
	default:
		return 0xff;
	}
}

void CHardAc::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (!chip_) return;
	switch (board_) {
	case CEMU_AC_BOARD_SYS16A:
	case CEMU_AC_BOARD_SYS16B:
	case CEMU_AC_BOARD_SYS24:
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x01) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		} else if ((board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
			&& (p == 0x40 || (p >= 0x40 && p <= 0x7f))) {
			/* Bank window 8000-DFFF — MAME segas16b upd7759_control_w per ROM board. */
			if (soundRom_ && soundRomSize_ > 0x10000u) {
				unsigned size = soundRomSize_ - 0x10000u;
				if (size == 0) size = 1;
				unsigned bankoffs = 0;
				const unsigned bt = sys16RomBoard_;
				if (bt == 0x5358u || board_ == CEMU_AC_BOARD_SYS16A) {
					/* 171-5358: D2..D5 = /CS for A8..A11 (active low). */
					if (!(data & 0x04u)) bankoffs = 0x00000u;
					if (!(data & 0x08u)) bankoffs = 0x10000u;
					if (!(data & 0x10u)) bankoffs = 0x20000u;
					if (!(data & 0x20u)) bankoffs = 0x30000u;
					bankoffs += (unsigned)(data & 0x03u) * 0x4000u;
				} else if (bt == 0x5521u || bt == 0x5704u) {
					/* 171-5521 / 5704: D3 selects A11/A12, D0-D2 = A14-A16. */
					bankoffs = ((data & 0x08u) >> 3) * 0x20000u;
					bankoffs += (unsigned)(data & 0x07u) * 0x4000u;
				} else {
					/* 171-5797 (default): D3=A11/A12, D4=A17, D0-D2=A14-A16. */
					bankoffs = ((data & 0x08u) >> 3) * 0x40000u;
					bankoffs += ((data & 0x10u) >> 4) * 0x20000u;
					bankoffs += (unsigned)(data & 0x07u) * 0x4000u;
				}
				const unsigned src = 0x10000u + (bankoffs % size);
				unsigned n = 0x6000u;
				if (src + n > soundRomSize_) n = soundRomSize_ - src;
				memset(mem_ + 0x8000, 0xff, 0x6000);
				if (n) memcpy(mem_ + 0x8000, soundRom_ + src, n);
				bank_ = (int)((bankoffs / 0x4000u) & 0xff);
				bankLoaded_ = 1;
			}
		}
		break;
	case CEMU_AC_BOARD_SYS18:
		if (p >= 0x80 && p <= 0x83) {
			chip_->Write(p & 3, data);
			if ((p & 3) == 1)
				opmWrites_++;
		} else if (p >= 0xa0 && p <= 0xa3 && chip2_) {
			chip2_->Write(p & 3, data);
			if ((p & 3) == 1)
				opmWrites_++;
		}
		break;
	case CEMU_AC_BOARD_SYS32:
		/* YM1 @80-83, YM2 @90-93. Bank @A0/B0. */
		if (p >= 0x80 && p <= 0x83) {
			chip_->Write(p & 3, data);
			if ((p & 3) == 1)
				opmWrites_++;
		} else if (p >= 0x90 && p <= 0x93) {
			CChip* ym2 = chip2_ ? chip2_ : chip_;
			if (ym2) {
				ym2->Write(p & 3, data);
				if ((p & 3) == 1)
					opmWrites_++;
			}
		} else if ((p >= 0xa0 && p <= 0xaf) || (p >= 0xb0 && p <= 0xbf)) {
			if (soundRom_ && soundRomSize_ > 0xa000u) {
				unsigned bank = (unsigned)(data & 0x1fu);
				if ((p & 0xf0) == 0xb0)
					bank |= 0x20u;
				const unsigned src = (bank * 0x2000u) % soundRomSize_;
				unsigned n = 0x2000u;
				if (src + n > soundRomSize_) n = soundRomSize_ - src;
				memset(mem_ + 0xa000, 0xff, 0x2000);
				if (n) memcpy(mem_ + 0xa000, soundRom_ + src, n);
				bank_ = (int)bank;
				bankLoaded_ = 1;
			}
		}
		break;
	case CEMU_AC_BOARD_OUTRUN:
	case CEMU_AC_BOARD_ABURNER:
		if (p < 0x40) {
			if ((p & 1) == 0) {
				chip_->Write(0, data);
			} else {
				chip_->Write(1, data);
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
		}
		break;
	case CEMU_AC_BOARD_VSYSTEM:
		{
			int ymOff = -1;
			if (vsIoKind_ == 1) {
				if (p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
				else if (p == 0x00 || p == 0x0c) SetBank(data & 0x03);
				else if (p == 0x14) ClearSoundCmdPending();
			} else if (vsIoKind_ == 2) {
				if (p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
				else if (p == 0x00 || p == 0x18) SetBank(data & 0x03);
				else if (p == 0x0c || p == 0x04) ClearSoundCmdPending();
			} else if (vsIoKind_ == 3) {
				if (p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
				else if (p == 0x00) SetBank(data & 0x0f);
				else if (p == 0x0c) ClearSoundCmdPending();
			} else {
				if (p <= 0x03) ymOff = (int)(p & 3);
				else if (p == 0x04) SetBank(data & 0x03);
				else if (p == 0x08) ClearSoundCmdPending();
			}
			if (ymOff < 0 && p <= 0x03) ymOff = (int)(p & 3);
			if (ymOff < 0 && p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
			if (ymOff < 0 && p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
			if (ymOff < 0 && p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
			if (ymOff >= 0) {
				chip_->Write(ymOff & 3, data);
				if ((ymOff & 3) == 1)
					opmWrites_++;
			} else if (p == 0x04 || p == 0x00 || p == 0x0c)
				SetBank(data & 0x0f);
			else if (p == 0x08 || p == 0x14)
				ClearSoundCmdPending();
		}
		break;
	case CEMU_AC_BOARD_CPS1:
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x01) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		}
		if ((p == 0x02 || p == 0x03) && pcm_)
			pcm_->Write(p - 0x02, data);
		else if (p == 0x04 && pcm_)
			pcm_->Write(0x100, data);
		break;
	case CEMU_AC_BOARD_CPS_QS:
		if (p <= 0x02)
			chip_->Write(p, data);
		break;
	case CEMU_AC_BOARD_IREM_M72:
		if (p == 0x00 || p == 0x40) {
			chip_->Write(0, data);
		} else if (p == 0x01 || p == 0x41) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		} else if (p == 0x06 || p == 0x83 || p == 0x42) {
			ClearSoundCmdPending(); /* latch acknowledge */
		} else if (p == 0x80 || p == 0x81 || p == 0x10 || p == 0x11
			|| p == 0x46 || p == 0x47) {
			/* rtype2_sample_addr_w / poundfor_sample_addr_w (11-bit granule). */
			m72SampleAddr_ >>= 5;
			if (p == 0x81 || p == 0x11 || p == 0x47)
				m72SampleAddr_ = (m72SampleAddr_ & 0x00ffu) | (((uint32_t)data << 8) & 0xff00u);
			else
				m72SampleAddr_ = (m72SampleAddr_ & 0xff00u) | (uint32_t)data;
			m72SampleAddr_ <<= 5;
		} else if (p == 0x82) {
			if (pcm_) pcm_->Write(0, data);
			m72SampleAddr_++;
		}
		break;
	case CEMU_AC_BOARD_TOAPLAN1:
		/* Accept all known Toaplan1 YM port pairs (writes are harmless on
		   non-native ports; reads are filtered in PortIn). */
		if (p == 0x00 || p == 0x60 || p == 0x70 || p == 0xa8
			|| p == toaplanYmPort_) {
			chip_->Write(0, data);
		} else if (p == 0x01 || p == 0x61 || p == 0x71 || p == 0xa9
			|| p == (uint8_t)(toaplanYmPort_ + 1u)) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm3812WriteCount(chip_);
		}
		break;
	case CEMU_AC_BOARD_SNK_OPL:
		if (snkMapKind_)
			break; /* classic SNK is memory-mapped */
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x20 || p == 0x01) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm3812WriteCount(chip_);
		}
		/* 0x40/0x80 = uPD7759 — ignore for FM BGM path */
		break;
	case CEMU_AC_BOARD_TERRACRE:
		/* MAME sound_3526_io_map: YM @00/01, DAC @02/03 (stub). */
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x01) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm3812WriteCount(chip_);
		}
		break;
	case CEMU_AC_BOARD_ROBOKID:
		/* MAME ninjakd2_sound_io: YM2203 #1 @00/01, #2 @80/81. */
		if (p == 0x00 || p == 0x01) {
			chip_->Write(p & 1, data);
			if (p & 1) opmWrites_++;
		} else if (p == 0x80 || p == 0x81) {
			if (chip2_) {
				chip2_->Write(p & 1, data);
				if (p & 1) opmWrites_++;
			}
		}
		break;
	case CEMU_AC_BOARD_ALPHA68K2:
		/* MAME: 00=clear latch, 08=DAC, 0a/0b=YM2413, 0c/0d=YM2203, 0e=bank. */
		{
			const uint8_t lo = (uint8_t)(p & 0x0f);
			if (lo <= 0x01) {
				soundCmdPending_ = 0;
			} else if (lo == 0x08 || lo == 0x09) {
				if (pcm_) pcm_->Write(0, data);
			} else if (lo == 0x0a) {
				alphaOpllAddr_ = data;
			} else if (lo == 0x0b) {
				if (alphaOpll_)
					OPLL_writeReg((OPLL*)alphaOpll_, alphaOpllAddr_, data);
			} else if (lo == 0x0c) {
				alphaYmAddr_ = data;
				if (chip_) chip_->Write(0, data);
			} else if (lo == 0x0d) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_++;
				}
				/* YM2203 SSG port A (reg 0x0E) gates the periodic NMI. */
				if (alphaYmAddr_ == 0x0e) {
					const uint8_t bit = (uint8_t)(data & 1);
					if (bit == 0 && alphaPaLatch_)
						alphaNmiMask_ = 1;
					if (bit != 0 && alphaPaLatch_ == 0)
						alphaNmiMask_ = 0;
					alphaPaLatch_ = bit;
				}
			} else if (lo == 0x0e || lo == 0x0f) {
				SetBank(data & 0x1f);
			}
		}
		break;
	case CEMU_AC_BOARD_KONAMI_PCM:
		if (pcm_) {
			if (p == 0x00) {
				chip_->Write(0, data);
			} else if (p == 0x01) {
				chip_->Write(1, data);
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			} else if (p < 0x40) {
				pcm_->Write(p, data);
			}
		} else if (p < 0x40) {
			chip_->Write(p, data);
		}
		break;
	case CEMU_AC_BOARD_KONAMI_SCRAMBLE:
		if (p == 0x10) {
			ayAddr_[0] = (uint8_t)(data & 0x0f);
			if (chip_) chip_->Write(0, data);
		} else if (p == 0x20) {
			if (chip_) { chip_->Write(1, data); opmWrites_++; }
		} else if (p == 0x40) {
			ayAddr_[1] = (uint8_t)(data & 0x0f);
			if (chip2_) chip2_->Write(0, data);
		} else if (p == 0x80) {
			if (chip2_) { chip2_->Write(1, data); opmWrites_++; }
		}
		break;
	case CEMU_AC_BOARD_NAMCO_C352:
		chip_->Write(p >> 1, data);
		break;
	default:
		break;
	}
}

void CHardAc::MemWrite(uint16_t addr, uint8_t data)
{
	/* MAME toaplan1 sound_map: 0000-7FFF ROM, 8000-87FF shared RAM. */
	if (board_ == CEMU_AC_BOARD_TOAPLAN1) {
		if (addr >= 0x8000 && addr <= 0x87ff)
			mem_[addr] = data;
		return;
	}
	/* MAME snk68 sound_map: 0000-EFFF ROM, F000-F7FF RAM, F800 latch.
	   Classic SNK (athena/…): C000-CFFF RAM, E000 latch, E800/EC00 YM1,
	   F000/F400 YM2, F800 status. */
	if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (snkMapKind_) {
			if (addr >= 0xc000 && addr <= 0xcfff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xe800) {
				if (chip_) chip_->Write(0, data);
				return;
			}
			if (addr == 0xec00) {
				if (chip_) { chip_->Write(1, data); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
				return;
			}
			if (addr == 0xf000) {
				if (chip2_) chip2_->Write(0, data);
				return;
			}
			if (addr == 0xf400) {
				if (chip2_) { chip2_->Write(1, data); opmWrites_++; }
				return;
			}
			if (addr == 0xf800) {
				/* MAME sound_status_w: low nibble is 0x0F; upper nibble bits
				   that are 0 clear the matching status flags (keep = data>>4). */
				snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)(data >> 4));
				return;
			}
			return;
		}
		if (addr >= 0xf000 && addr <= 0xf7ff)
			mem_[addr] = data;
		/* F800 write = soundlatch2 (ack to main) — ignore. */
		return;
	}
	/* Seibu: RAM 2000-27FF + sound I/O 4000-401B + OKI 6000. */
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		if (addr >= 0x2000 && addr <= 0x27ff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0x4000) {
			seibuMainPending_ = 0;
			seibuSubPending_ = 1;
			return;
		}
		if (addr == 0x4001) {
			seibuRst18_ = 0; /* irq_clear / RST18 EOI */
			return;
		}
		if (addr == 0x4002) { seibuRst10_ = 0; return; }
		if (addr == 0x4003) { seibuRst18_ = 0; return; }
		if (addr == 0x4007 || addr == 0x401a) {
			SeibuSetBank(data & 1);
			return;
		}
		if (addr == 0x4008 || addr == 0x4009) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1) opmWrites_ = CEmuChipYm3812WriteCount(chip_);
			}
			return;
		}
		if (addr == 0x4018 || addr == 0x4019) {
			seibuSub2Main_[addr & 1] = data;
			return;
		}
		if (addr == 0x6000) {
			if (pcm_) pcm_->Write(0, data);
			return;
		}
		return;
	}
	/* MAME taito_f2 / taito_h / asuka(bonzeadv) sound_map:
	   0000-3FFF ROM, 4000-7FFF bank, C000-DFFF RAM, E000-E003 YM2610,
	   E200 slave_port_w, E201 slave_comm_w, F200 bankswitch. */
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610) {
		if (addr >= 0xc000 && addr <= 0xdfff) {
			mem_[addr] = data;
			return;
		}
		if (addr >= 0xe000 && addr <= 0xe003) {
			if (chip_) {
				chip_->Write(addr & 3, data);
				if ((addr & 1) != 0) opmWrites_++;
			}
			return;
		}
		if (addr == 0xe200) { SytSlavePortW(data); return; }
		if (addr == 0xe201) { SytSlaveCommW(data); return; }
		if (addr == 0xf200) { SetBank(data & 7); return; }
		return; /* E400 pan / EE00 / F000 no-ops; never poke ROM */
	}
	/* MAME taito_rastan / taito_asuka base map: 0000-3FFF ROM, 4000-7FFF bank,
	   8000-8FFF RAM, 9000/9001 YM2151, A000/A001 PC060HA, B000/C000/D000
	   MSM5205 (not emulated ? those games' music is all YM2151). */
	if (board_ == CEMU_AC_BOARD_TAITO_OPM) {
		if (addr >= 0x8000 && addr <= 0x8fff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0x9000 || addr == 0x9001) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr == 0x9001) {
					if (mainIsYm2203_) {
						opmWrites_++;
						/* MAME taito_b masterw/viofight: YM2203 port A (SSG
						   reg 0x0E) selects audiobank bits 1:0. */
						if (ymAddr_ == 0x0e)
							SetBank(data & 3);
					} else {
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
						/* CT1/CT2 (reg 0x1B) drives the ROM bank here. ymfm
						   write_data: m_output_io = data >> 6, and MAME wires that
						   ACCESS_IO byte straight into sound_bankswitch_w so the
						   bank comes from bits 7-6, not 1-0. */
						if (ymAddr_ == 0x1b) SetBank((data >> 6) & 3);
					}
				} else {
					ymAddr_ = data;
				}
			}
			return;
		}
		if (addr == 0xa000) { SytSlavePortW(data); return; }
		if (addr == 0xa001) { SytSlaveCommW(data); return; }
		/* viofight: OKI6295 @ B000 (both addresses). */
		if (pcm_ && (addr == 0xb000 || addr == 0xb001)) {
			pcm_->Write(0, data);
			return;
		}
		return;
	}
	/* MAME vsystem/aerofgt sound_map: 0000-77FF ROM, 7800-7FFF RAM,
	   8000-FFFF banked. The boot code clears 0x8FF bytes from 7800, which runs
	   past the RAM into the bank window ? on hardware those writes hit ROM and
	   are dropped, so the window must be write-protected here too.
	   fromanc2: 0000-DFFF ROM, E000-FFFF RAM (no 7800 window). */
	if (board_ == CEMU_AC_BOARD_VSYSTEM) {
		if (vsIoKind_ == 2) {
			if (addr >= 0xe000)
				mem_[addr] = data;
		} else if (vsIoKind_ == 3) {
			/* gunbird: RAM 8000-81FF only; 8200-FFFF is banked ROM. */
			if (addr >= 0x8000 && addr <= 0x81ff)
				mem_[addr] = data;
		} else if (addr >= 0x7800 && addr <= 0x7fff) {
			mem_[addr] = data;
		}
		return;
	}
	/* MAME irem_m72: sound_ram_map (M72 board) is 0000-FFFF RAM, sound_rom_map
	   (M81/M82/M84) is 0000-EFFF ROM + F000-FFFF RAM. */
	if (board_ == CEMU_AC_BOARD_IREM_M72) {
		if (m72SoundRam_ || addr >= 0xf000)
			mem_[addr] = data;
		return;
	}
	/* MAME segas16b sound_map: 0000-7FFF ROM, 8000-DFFF bank, F800-FFFF RAM. */
	if (board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B) {
		if (addr >= 0xf800)
			mem_[addr] = data;
		return;
	}
	/* MAME sega_system1 sound_map: 0000-7FFF ROM, 8000-87FF RAM (mirror 1800),
	   A000 SN1 (mirror 1FFF), C000 SN2 (mirror 1FFF), E000 latch (mirror 1FFF). */
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (addr >= 0x8000 && addr <= 0x9fff) {
			mem_[0x8000 + (addr & 0x07ff)] = data;
			return;
		}
		if (addr >= 0xa000 && addr <= 0xbfff) {
			if (chip_) { chip_->Write(0, data); opmWrites_++; }
			return;
		}
		if (addr >= 0xc000 && addr <= 0xdfff) {
			if (chip2_) { chip2_->Write(0, data); opmWrites_++; }
			return;
		}
		return;
	}
	/* MAME taito_taitosj audio map: 0000-3FFF ROM, 4000-43FF RAM,
	   4800/4802/4804 AY address+data, 5000/5001 soundlatch semaphores. */
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (addr >= 0x4000 && addr <= 0x43ff) {
			mem_[addr] = data;
			return;
		}
		if (addr >= 0x4800 && addr <= 0x4fff) {
			const unsigned slot = (addr & 0x07) >> 1;
			CChip* ay = (slot == 0) ? chip_ : (slot == 1 ? chip2_ : chip3_);
			if (ay) {
				if ((addr & 1) == 0) {
					ayAddr_[slot & 3] = (uint8_t)(data & 0x0f);
					ay->Write(0, data);
				} else {
					ay->Write(1, data);
					opmWrites_++;
					/* AY#4 (slot 2 here) port B bit0 = sound NMI mask, low = on. */
					if (slot == 2 && ayAddr_[2] == 0x0f) {
						sjNmiMask_ = (uint8_t)((~data) & 1);
						sjNmiMaskSeen_ = 1;
					}
				}
			}
			return;
		}
		if (addr >= 0x5000 && addr <= 0x57ff) {
			if ((addr & 1) == 0)
				soundCmd_ = (uint8_t)(soundCmd_ & 0x7f); /* soundlatch_clear7_w */
			else
				sjSemaphore2_ = 0;
			return;
		}
		return;
	}
	/* MAME timeplt_a: ROM 0000-2FFF, RAM 3000-33FF, AY1 data/addr 4000/5000,
	   AY2 data/addr 6000/7000, filter 8000+. */
	if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		if (addr >= 0x3000 && addr <= 0x3fff) {
			mem_[0x3000 + (addr & 0x03ff)] = data;
			return;
		}
		if ((addr & 0xf000) == 0x4000) {
			if (chip_) { chip_->Write(1, data); opmWrites_++; }
			return;
		}
		if ((addr & 0xf000) == 0x5000) {
			ayAddr_[0] = (uint8_t)(data & 0x0f);
			if (chip_) chip_->Write(0, data);
			return;
		}
		if ((addr & 0xf000) == 0x6000) {
			if (chip2_) { chip2_->Write(1, data); opmWrites_++; }
			return;
		}
		if ((addr & 0xf000) == 0x7000) {
			ayAddr_[1] = (uint8_t)(data & 0x0f);
			if (chip2_) chip2_->Write(0, data);
			return;
		}
		return; /* filter / unmapped */
	}
	/* MAME nemesis sound_map / gx400_sound_map (AY + latch; K005289 stubbed). */
	if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (addr >= 0x4000 && addr <= 0x7fff) {
			mem_[addr] = data;
			return;
		}
		if (addr >= 0x8000 && addr <= 0x87ff) {
			mem_[addr] = data; /* voiceram on bubble sets */
			return;
		}
		if (addr == 0xe005) {
			ayAddr_[1] = (uint8_t)(data & 0x0f);
			if (chip2_) chip2_->Write(0, data);
			return;
		}
		if (addr == 0xe006) {
			ayAddr_[0] = (uint8_t)(data & 0x0f);
			if (chip_) chip_->Write(0, data);
			return;
		}
		if (addr == 0xe106) {
			if (chip_) { chip_->Write(1, data); opmWrites_++; }
			return;
		}
		if (addr == 0xe405) {
			if (chip2_) { chip2_->Write(1, data); opmWrites_++; }
			return;
		}
		/* e003/e004 K005289 tg, a000/c000 ld, e007 filter, e000 speech ? ignore */
		if (addr >= 0xa000)
			return;
		return;
	}
	/* MAME ddragon2_sound_map: ROM 0000-7FFF, RAM 8000-87FF,
	   YM2151 8800-8801, OKI 9800, latch A000. */
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		if (addr >= 0x8000 && addr <= 0x87ff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0x8800) {
			if (chip_) chip_->Write(0, data);
			return;
		}
		if (addr == 0x8801) {
			if (chip_) {
				chip_->Write(1, data);
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		if (addr == 0x9800) {
			if (pcm_) pcm_->Write(0, data);
			return;
		}
		return;
	}
	/* MAME thunderx/scontra (map0) / crimfght (map1): Z80 + YM2151 + K007232 stub. */
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		/* twin16 uses 8000-87FF; older dumps also touch 8800-8FFF — allow 8K. */
		if (addr >= 0x8000 && addr <= 0x8fff) {
			mem_[addr] = data;
			return;
		}
		const unsigned ym = konamiK7232Map_ ? 0xa000u : 0xc000u;
		if (addr == ym || addr == ym + 1u) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		/* K007232 / uPD7759 / bankswitch — accept writes, no PCM synth yet. */
		if (addr >= 0x9000)
			return;
		return;
	}
	/* MAME alpha68k_II sound_map: ROM 0000-7FFF, RAM 8000-87FF, bank C000-FFFF. */
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		if (addr >= 0x8000 && addr <= 0x87ff)
			mem_[addr] = data;
		return;
	}
	/* MAME hcastle sound_map: YM3812 @A000, K007232 @B000, latch @D000. */
	if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		if (addr >= 0x8000 && addr <= 0x87ff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0xa000 || addr == 0xa001) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_ = CEmuChipYm3812WriteCount(chip_);
			}
			return;
		}
		if (addr >= 0x9800)
			return;
		return;
	}
	/* MAME tecmo16 sound_map: ROM 0000-EFFF, RAM F000-FBFF,
	   OKI FC00, YM2151 FC04/05, latch FC08. */
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		if (addr >= 0xf000 && addr <= 0xfbff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0xfc00) {
			if (pcm_) pcm_->Write(0, data);
			return;
		}
		if (addr == 0xfc04 || addr == 0xfc05) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		if (addr >= 0xfc00)
			return;
		return;
	}
	/* MAME flstory sound_map: RAM C000-C7FF, AY C800, MSM CA00-CA0D,
	   volume CC00/CE00, latch D800, NMI enable DA00, clear DC00, DAC DE00.
	   E000-EFFF is optional diagnostics ROM (absent → open bus 0x00). */
	if (board_ == CEMU_AC_BOARD_FLSTORY) {
		if (addr >= 0xc000 && addr <= 0xc7ff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0xc800 || addr == 0xc801) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1) opmWrites_++;
			}
			return;
		}
		if (addr >= 0xca00 && addr <= 0xca0d) {
			if (chip2_) {
				chip2_->Write(addr - 0xca00u, data);
				opmWrites_++;
			}
			return;
		}
		if (addr == 0xda00) {
			/* soundnmi in_set<1> — enable NMI when latch is pending. */
			flstoryNmiEn_ = 1;
			if (soundCmdPending_)
				irqPulse_ = 1;
			return;
		}
		if (addr == 0xdc00) {
			flstoryNmiEn_ = 0;
			return;
		}
		if (addr == 0xde00) {
			/* 8-bit DAC — accept, no synth yet. */
			return;
		}
		if (addr >= 0xcc00)
			return;
		return;
	}
	/* MAME battlnts sound_map: RAM 8000-87FF, YM3812 @A000 + @C000, latch E000. */
	if (board_ == CEMU_AC_BOARD_BATTLANTIS) {
		if (addr >= 0x8000 && addr <= 0x87ff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0xa000 || addr == 0xa001) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_ = CEmuChipYm3812WriteCount(chip_);
			}
			return;
		}
		if (addr == 0xc000 || addr == 0xc001) {
			if (chip2_) {
				chip2_->Write(addr & 1, data);
				if (addr & 1) opmWrites_++;
			}
			return;
		}
		if (addr >= 0xa000)
			return;
		return;
	}
	/* MAME robokid/ninjakd2: RAM C000-C7FF; YM is I/O-mapped. */
	if (board_ == CEMU_AC_BOARD_ROBOKID) {
		if (addr >= 0xc000 && addr <= 0xc7ff) {
			mem_[addr] = data;
			return;
		}
		return;
	}
	/* terracre: RAM C000-CFFF; armedf/terraf: RAM F800-FFFF. YM/latch on I/O. */
	if (board_ == CEMU_AC_BOARD_TERRACRE) {
		if (terracreMap_) {
			if (addr >= 0xf800)
				mem_[addr] = data;
		} else if (addr >= 0xc000 && addr <= 0xcfff) {
			mem_[addr] = data;
		}
		return;
	}
	/* CPS1 sound map: F000/F001=YM2151, F002/F003=OKI, F004=bank, F006=?, F008=latch */
	if (board_ == CEMU_AC_BOARD_CPS1 && chip_) {
		if (addr == 0xf000) {
			ymAddr_ = data;
			chip_->Write(0, data);
			return;
		}
		if (addr == 0xf001) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		if ((addr == 0xf002 || addr == 0xf003) && pcm_) {
			pcm_->Write(addr & 1, data);
			return;
		}
		if (addr == 0xf004 && pcm_) {
			pcm_->Write(0x100, data);
			return;
		}
		/* Misc I/O ? ignore data plane so ROM image at F0xx stays unread via MemRead */
		if (addr >= 0xf002 && addr <= 0xf00f)
			return;
	}
	/* CPS2 QSound map (MAME qsound_sub_map): ROM 0000-7FFF, bank 8000-BFFF,
	   shared RAM C000-CFFF, QSound D000-D002, banksw D003, status D007,
	   work RAM F000-FFFF. Old code treated F000 as QSound and broke Z80 RAM. */
	if (board_ == CEMU_AC_BOARD_CPS_QS) {
		if (chip_ && addr >= 0xd000 && addr <= 0xd002) {
			if (addr == 0xd002)
				CEmuChipQSoundWriteCommand(chip_, data);
			else
				chip_->Write(addr & 3, data);
			return;
		}
		if (addr == 0xd003) {
			SetBank(data & 0x0f);
			return;
		}
		if ((addr >= 0xc000 && addr <= 0xcfff) || addr >= 0xf000) {
			mem_[addr] = data;
			return;
		}
		return;
	}
	if ((board_ == CEMU_AC_BOARD_OUTRUN || board_ == CEMU_AC_BOARD_ABURNER)
		&& pcm_
		&& ((addr >= 0xf000 && addr <= 0xf0ff) || (addr >= 0x1000 && addr <= 0x1fff))) {
		pcm_->Write(addr & 0xff, data);
		return;
	}
	if ((board_ == CEMU_AC_BOARD_SYS18 || board_ == CEMU_AC_BOARD_SYS24
		|| board_ == CEMU_AC_BOARD_SYS32) && pcm_) {
		if (addr >= 0xc000 && addr <= 0xcfff) {
			pcm_->Write(addr & 0xff, data);
			return;
		}
		if (addr >= 0xd000 && addr <= 0xdfff) {
			pcm_->Write(addr & 0xff, data);
			return;
		}
	}
	if (board_ == CEMU_AC_BOARD_HANGON) {
		if (addr >= 0xc000 && addr <= 0xc7ff) {
			mem_[addr] = data;
			return;
		}
		if (addr >= 0xd000 && addr <= 0xdfff && chip_) {
			if ((addr & 1) == 0) {
				hangYmAddr_ = data;
				chip_->Write(0, data);
			} else {
				chip_->Write(1, data);
				opmWrites_++;
			}
			return;
		}
		if (pcm_ && addr >= 0xe000 && addr <= 0xefff) {
			pcm_->Write(addr & 0xff, data);
			return;
		}
		if (addr >= 0x8000)
			return;
		mem_[addr] = data;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		CChip* pcm = pcm_ ? pcm_ : chip_;
		const unsigned opm = konamiOpmAddr_ ? konamiOpmAddr_ : 0xf800u;
		const unsigned pcmBase = konamiPcmAddr_ ? konamiPcmAddr_ : 0xfc00u;
		const unsigned pcmWin = konamiPcmWindow_ ? konamiPcmWindow_ : 0x40u;
		/* K054321 sound_map @F000: [0]=snd→main, [2]/[3]=main→snd latches. */
		if (pcmKind_ == 4 && addr >= 0xf000 && addr <= 0xf003) {
			if (addr == 0xf000) {
				/* sound → main latch; nothing listens in CEmu. */
				return;
			}
			return;
		}
		/* K053260: FA00 arms SH1→NMI (MAME z80_arm_nmi_w). Sound CPU does
		   LD (FA00),A / HALT; NMI (RETN) resumes past HALT. */
		if (pcmKind_ == 3 && addr == 0xfa00 && pcmBase != 0xfa00u) {
			konamiSh1NmiArm_ = 1;
			return;
		}
		if (pcm && addr >= pcmBase && addr < pcmBase + pcmWin) {
			pcm->Write(addr - pcmBase, data);
			return;
		}
		/* YM2151 @opm/@opm+1, plus Konami F81x data-port mirror (thndrx2
		   LD (F811),A after RST28 busy-wait). */
		if (chip_ && pcm_
			&& (addr == opm || addr == (opm + 1u)
				|| (pcmKind_ == 3 && (addr == (opm + 0x10u) || addr == (opm + 0x11u))))) {
			chip_->Write(addr & 1, data);
			if ((addr & 1) == 1)
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		/* Only mirror YM@F000 when catalog actually placed it there (lgtnfght
		   uses A000; F800-class boards keep F000-F7FF as work RAM). */
		if (pcmKind_ == 3 && opm == 0xf000u && chip_ && pcm_
			&& (addr == 0xf000 || addr == 0xf001)) {
			chip_->Write(addr & 1, data);
			if ((addr & 1) == 1)
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		if (konamiBankAddr_ && addr == konamiBankAddr_) {
			SetBank((int)data);
			return;
		}
		/* Work RAM (F000-F7FF typical; 8000+ on lgtnfght). Chip windows
		   already returned above — remaining high space is RAM/open. */
		if (addr >= 0x8000)
			mem_[addr] = data;
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && chip_) {
		chip_->Write(addr >> 1, data);
		return;
	}
	/* MAME namcos1 sound_map (M6809, no core yet): YM2151 @4000, CUS30 amap
	   @5000-53FF (wave 5000-50FF, regs 5100-513F). */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
		if (chip_ && (addr == 0x4000 || addr == 0x4001)) {
			chip_->Write(addr & 1, data);
			if (addr & 1) opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		if (pcm_ && addr >= 0x5000 && addr <= 0x53ff) {
			pcm_->Write(addr & 0x3ff, data);
			return;
		}
		if (addr >= 0x8000 && addr <= 0x9fff) {
			mem_[addr] = data;
			return;
		}
		return;
	}
	/* MAME namcos86 common_mcu_map: CUS30 @1000-13FF, YM2151 @2000-2001. */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS86) {
		if (pcm_ && addr >= 0x1000 && addr <= 0x13ff) {
			pcm_->Write(addr & 0x3ff, data);
			return;
		}
		if (chip_ && (addr == 0x2000 || addr == 0x2001)) {
			chip_->Write(addr & 1, data);
			if (addr & 1) opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		if (addr >= 0x1400 && addr <= 0x1fff) {
			mem_[addr] = data;
			return;
		}
		return;
	}
	/* WSG: Pac-Man/Galaga 3-voice regs @6800-681F; shared RAM 8000-9FFF;
	   $6822 sound-enable pulse. */
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && chip_) {
		if (addr >= 0x6800 && addr <= 0x681f) {
			chip_->Write(addr & 0x1f, data & 0x0f);
			opmWrites_++;
			return;
		}
		if (addr == 0x6822) {
			/* Misc latch pulse — keep WSG enabled. */
			mem_[addr] = data;
			return;
		}
		if (addr >= 0x8000 && addr <= 0x9fff) {
			mem_[addr] = data;
			return;
		}
		/* Ignore other MMIO; keep ROM intact. */
		if (addr >= 0x4000)
			return;
		return;
	}
	/* GNG: ROM 0000-7FFF, RAM C000-C7FF, latch C800, YM2203 E000-E003
	   Commando map: RAM 4000-47FF, latch 6000, YM2203 8000-8003.
	   Tecmo gaiden: ROM 0000-DFFF, RAM F000-F7FF, OKI F800, YM F810/F820,
	   latch FC20 → NMI (YM2203 IRQ drives sequencer). */
	if (board_ == CEMU_AC_BOARD_GNG) {
		if (gngGaidenMap_) {
			if (addr >= 0xf000 && addr <= 0xf7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf800) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xf810 && chip_) {
				gngYmAddr_[0] = data;
				chip_->Write(0, data);
				return;
			}
			if (addr == 0xf811 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr == 0xf820 && chip2_) {
				gngYmAddr_[1] = data;
				chip2_->Write(0, data);
				return;
			}
			if (addr == 0xf821 && chip2_) {
				chip2_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr >= 0xf000)
				return;
			return;
		}
		if (gngCommandoMap_) {
			if (addr >= 0x4000 && addr <= 0x47ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x8000 && chip_) {
				gngYmAddr_[0] = data;
				chip_->Write(0, data);
				return;
			}
			if (addr == 0x8001 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr == 0x8002 && chip_) {
				gngYmAddr_[1] = data;
				chip_->Write(0, data);
				return;
			}
			if (addr == 0x8003 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr >= 0x4000)
				return;
			return;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff) {
			mem_[addr] = data;
			return;
		}
		if (addr == 0xe000 && chip_) {
			gngYmAddr_[0] = data;
			chip_->Write(0, data);
			return;
		}
		if (addr == 0xe001 && chip_) {
			chip_->Write(1, data);
			opmWrites_++;
			return;
		}
		if (addr == 0xe002 && chip_) {
			gngYmAddr_[1] = data;
			chip_->Write(0, data);
			return;
		}
		if (addr == 0xe003 && chip_) {
			chip_->Write(1, data);
			opmWrites_++;
			return;
		}
		/* Ignore other MMIO; do not poke ROM. */
		if (addr >= 0x8000)
			return;
		return;
	}
	mem_[addr] = data;
}

uint8_t CHardAc::MemRead(uint16_t addr)
{
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		if (addr >= 0x2000 && addr <= 0x27ff)
			return mem_[addr];
		if (addr == 0x4008 || addr == 0x4009)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr == 0x4010 || addr == 0x4011)
			return seibuMain2Sub_[addr & 1];
		if (addr == 0x4012)
			return seibuSubPending_ ? 1 : 0;
		if (addr == 0x4013)
			return 0xff; /* coin */
		if (addr == 0x6000)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		/* Encrypted ROM via data decrypt (or plaintext banked image). */
		if (soundRom_) {
			unsigned phys;
			if (addr < 0x8000)
				phys = addr;
			else
				phys = (seibuBank_ ? 0x18000u : 0x10000u) + (addr & 0x7fffu);
			if (phys < soundRomSize_) {
				if (seibuEnc_)
					return CEmuSei80buData(addr, soundRom_[phys]);
				return soundRom_[phys];
			}
		}
		return 0xff;
	}
	if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (snkMapKind_) {
			if (addr >= 0xc000 && addr <= 0xcfff)
				return mem_[addr];
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)~0x08u); /* clear pending */
				return soundCmd_;
			}
			if (addr == 0xe800)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr == 0xf000)
				return chip2_ ? chip2_->ReadStatus() : 0x00;
			if (addr == 0xf800)
				return snkStatus_;
			return mem_[addr];
		}
		if (addr == 0xf800) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610) {
		if (addr >= 0xe000 && addr <= 0xe003 && chip_) {
			switch (addr & 3) {
			case 0: return chip_->ReadStatus();
			case 1: return chip_->ReadData();
			case 2: return chip_->ReadStatusHi();
			default: return chip_->ReadDataHi();
			}
		}
		if (addr == 0xe201) return SytSlaveCommR();
		if (addr >= 0xe000 && addr <= 0xf2ff) return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM) {
		if (chip_ && (addr == 0x9000 || addr == 0x9001))
			return chip_->ReadStatus();
		if (addr == 0xa001) return SytSlaveCommR();
		if (addr >= 0x9000 && addr <= 0xdfff) return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (addr >= 0x8000 && addr <= 0x9fff)
			return mem_[0x8000 + (addr & 0x07ff)];
		if (addr >= 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xa000) return 0xff; /* write-only PSG ports */
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (addr >= 0x4800 && addr <= 0x4fff) {
			const unsigned slot = (addr & 0x07) >> 1;
			CChip* ay = (slot == 0) ? chip_ : (slot == 1 ? chip2_ : chip3_);
			unsigned char regs[16];
			if (ay && CEmuChipAyPeekRegs(ay, regs))
				return regs[ayAddr_[slot & 3] & 0x0f];
			return 0xff;
		}
		if (addr >= 0x5000 && addr <= 0x57ff) {
			if ((addr & 1) == 0) {
				sjLatchFlag_ = 0;
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			/* soundlatch_flags_r: bit3 = latch full, bit2 = semaphore2. */
			return (uint8_t)((sjLatchFlag_ ? 8 : 0) | (sjSemaphore2_ ? 4 : 0) | 3);
		}
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		if (addr >= 0x3000 && addr <= 0x3fff)
			return mem_[0x3000 + (addr & 0x03ff)];
		if ((addr & 0xf000) == 0x4000) {
			const uint8_t a = (uint8_t)(ayAddr_[0] & 0x0f);
			if (a == 0x0e) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (a == 0x0f)
				return KonamiAyTimer();
			return chip_ ? chip_->ReadData() : 0xff;
		}
		if ((addr & 0xf000) == 0x6000)
			return chip2_ ? chip2_->ReadData() : 0xff;
		if (addr >= 0x4000)
			return 0xff;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (addr == 0xe001) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xe086) {
			/* AY1 data. Reg 0x0E = MAME nemesis_portA_r (cycle timer). */
			if ((ayAddr_[0] & 0x0f) == 0x0e)
				return Gx400PortA();
			return chip_ ? chip_->ReadData() : 0xff;
		}
		if (addr == 0xe205) {
			/* AY2 I/O A is a K005289 write port on hardware; reads unused. */
			return chip2_ ? chip2_->ReadData() : 0xff;
		}
		if (addr >= 0x4000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0xa000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		if (addr == 0x8800 || addr == 0x8801)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr == 0x9800)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (addr == 0xa000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0xff;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		const unsigned latch = konamiK7232Map_ ? 0xc000u : 0xa000u;
		const unsigned ym = konamiK7232Map_ ? 0xa000u : 0xc000u;
		if (addr == latch) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == ym || addr == ym + 1u)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr >= 0x8000 && addr <= 0x8fff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0x00; /* K007232 / stubs */
	}
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		/* 0000-7FFF ROM / C000-FFFF bank already live in mem_ via LoadRoms+SetBank. */
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		if (addr == 0xd000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xa000 || addr == 0xa001)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		if (addr == 0xfc00)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (addr == 0xfc04 || addr == 0xfc05)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr == 0xfc08) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xf000 && addr <= 0xfbff)
			return mem_[addr];
		if (addr >= 0xf000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_FLSTORY) {
		if (addr >= 0xc000 && addr <= 0xc7ff)
			return mem_[addr];
		if (addr == 0xd800) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xda00)
			return (uint8_t)(soundCmdPending_ ? 1 : 0);
		if (addr == 0xde00)
			return 0x00; /* DAC read unknown */
		if (addr >= 0xc800)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_BATTLANTIS) {
		if (addr == 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xa000 || addr == 0xa001)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr == 0xc000 || addr == 0xc001)
			return chip2_ ? chip2_->ReadStatus() : 0x00;
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID) {
		if (addr == 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff)
			return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE) {
		if (terracreMap_) {
			if (addr >= 0xf800)
				return mem_[addr];
		} else if (addr >= 0xc000 && addr <= 0xcfff) {
			return mem_[addr];
		}
	}
	if (board_ == CEMU_AC_BOARD_CPS1) {
		/* YM2151 status (bit7=busy). fmgen has no busy; return timer bits only. */
		if (chip_ && (addr == 0xf000 || addr == 0xf001))
			return chip_->ReadStatus();
		/* Sound command latch from main 68K (polled by Z80).
		   F008 = latch lo; F00A = latch hi / second byte. Capcom version-5
		   (megaman/sfzch) only queues a command when (F00A)==0xFF while the
		   YM pointer at D010 is live ? returning the same byte for both ports
		   skipped the queue and left every FM channel at TL=7F. */
		if (addr == 0xf008) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xf00a)
			return 0xff;
		if ((addr == 0xf002 || addr == 0xf003) && pcm_)
			return pcm_->ReadStatus();
		/* Unmapped I/O mirror ? never return ROM 0xFF (would spin on busy). */
		if (addr >= 0xf000 && addr <= 0xf00f)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_CPS_QS) {
		if (addr == 0xd007)
			return chip_ ? chip_->ReadStatus() : 0x80;
		/* Kabuki: opcode plane is in mem_ (Z80 fetch); data plane here. */
		if (qsKabuki_ && qsKabukiData_ && addr < 0x8000u)
			return qsKabukiData_[addr];
		/* C000-CFFF / F000-FFFF are RAM ? fall through to mem_[]. */
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		const unsigned opm = konamiOpmAddr_ ? konamiOpmAddr_ : 0xf800u;
		const unsigned pcmBase = konamiPcmAddr_ ? konamiPcmAddr_ : 0xfc00u;
		const unsigned pcmWin = konamiPcmWindow_ ? konamiPcmWindow_ : 0x40u;
		/* K054321: LD BC,(F002) fetches main→sound latch pair. */
		if (pcmKind_ == 4 && (addr == 0xf002 || addr == 0xf003)) {
			if (addr == 0xf002) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0x00;
		}
		if (chip_ && pcm_
			&& (addr == opm || addr == (opm + 1u)
				|| (pcmKind_ == 3 && (addr == (opm + 0x10u) || addr == (opm + 0x11u)))))
			return chip_->ReadStatus();
		if (pcmKind_ == 3 && opm == 0xf000u && chip_ && pcm_
			&& (addr == 0xf000 || addr == 0xf001))
			return chip_->ReadStatus();
		if (addr >= pcmBase && addr < pcmBase + pcmWin) {
			CChip* pcm = pcm_ ? pcm_ : chip_;
			if (!pcm) return 0x00;
			if (pcmKind_ == 3)
				return CEmuChipK053260Read(pcm, addr - pcmBase);
			return pcm->ReadStatus();
		}
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
		if (chip_ && (addr == 0x4000 || addr == 0x4001))
			return chip_->ReadStatus();
		if (pcm_ && addr >= 0x5000 && addr <= 0x53ff) {
			uint8_t regs[0x40];
			const unsigned off = addr & 0x3ff;
			if (off >= 0x100 && off < 0x140
				&& pcm_->GetRegSnapshot(regs, sizeof(regs)) > 0)
				return regs[off - 0x100];
			return 0;
		}
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS86) {
		if (chip_ && (addr == 0x2000 || addr == 0x2001))
			return chip_->ReadStatus();
		if (pcm_ && addr >= 0x1000 && addr <= 0x13ff) {
			uint8_t regs[0x40];
			const unsigned off = addr & 0x3ff;
			if (off >= 0x100 && off < 0x140
				&& pcm_->GetRegSnapshot(regs, sizeof(regs)) > 0)
				return regs[off - 0x100];
			return 0;
		}
	}
	if (board_ == CEMU_AC_BOARD_GNG) {
		if (gngGaidenMap_) {
			if (addr == 0xfc20) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xf800)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (chip_ && (addr == 0xf810 || addr == 0xf811))
				return chip_->ReadStatus();
			if (chip2_ && (addr == 0xf820 || addr == 0xf821))
				return chip2_->ReadStatus();
			if (addr >= 0xf000 && addr <= 0xf7ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (gngCommandoMap_) {
			if (addr == 0x6000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (chip_ && (addr == 0x8000 || addr == 0x8001
				|| addr == 0x8002 || addr == 0x8003))
				return chip_->ReadStatus();
			return mem_[addr];
		}
		if (addr == 0xc800) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		/* Avengers: latch ack + pending bit at E006 (MAME lwings_sound_map). */
		if (addr == 0xe006) {
			const uint8_t v = (uint8_t)(soundCmd_ | (soundCmdPending_ ? 0x80 : 0));
			soundCmdPending_ = 0;
			return v;
		}
		if (chip_ && (addr == 0xe000 || addr == 0xe001))
			return chip_->ReadStatus();
		if (chip_ && (addr == 0xe002 || addr == 0xe003))
			return chip_->ReadStatus();
	}
	if (board_ == CEMU_AC_BOARD_HANGON) {
		if (chip_ && (addr >= 0xd000 && addr <= 0xdfff) && (addr & 1) == 0)
			return chip_->ReadStatus();
		/* SegaPCM is mapped RW ? Z80 polls ctrl/end; writes never hit mem_[]. */
		if (pcm_ && addr >= 0xe000 && addr <= 0xefff) {
			uint8_t ram[256];
			if (pcm_->GetRegSnapshot(ram, 256) > 0)
				return ram[addr & 0xff];
		}
	}
	/* OutRun / After Burner: SegaPCM @ $F000 (and $1000 mirror). */
	if ((board_ == CEMU_AC_BOARD_OUTRUN || board_ == CEMU_AC_BOARD_ABURNER)
		&& pcm_
		&& ((addr >= 0xf000 && addr <= 0xf0ff) || (addr >= 0x1000 && addr <= 0x1fff))) {
		uint8_t ram[256];
		if (pcm_->GetRegSnapshot(ram, 256) > 0)
			return ram[addr & 0xff];
	}
	if ((board_ == CEMU_AC_BOARD_SYS18 || board_ == CEMU_AC_BOARD_SYS24
		|| board_ == CEMU_AC_BOARD_SYS32) && pcm_) {
		if ((addr >= 0xc000 && addr <= 0xcfff) || (addr >= 0xd000 && addr <= 0xdfff)) {
			uint8_t ram[256];
			if (pcm_->GetRegSnapshot(ram, 256) > 0)
				return ram[addr & 0xff];
		}
	}
	return mem_[addr];
}

static void CEmuAcZipBaseName(const char* name, char* out, int outCap)
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

/* Score sound-CPU candidates. cotton.zip lists speech0.rom before s-prog.rom ?
   picking PCM first yields PC=AD08 / peak=0. Prefer 16?64K Z80 DI/IM1 dumps. */
static int CEmuAcIsCodeRomType(const char* t)
{
	if (!t || !t[0]) return 0;
	if (_stricmp(t, "code") == 0 || _stricmp(t, "sub") == 0) return 1;
	if (_stricmp(t, "audiocpu") == 0 || _stricmp(t, "sound") == 0) return 1;
	/* xml2: code0/code1 (byte-split 68000) and sub0/sub1 (sound CPU). */
	if (_strnicmp(t, "code", 4) == 0) return 1;
	if (_strnicmp(t, "sub", 3) == 0) return 1;
	return 0;
}

/* Catalog tags the Sys16 uPD7751 speech MCU as type=sub @0000 (shinobi/
   alexkidd/bodyslam…). That is NOT Z80 code — loading it into mem_ wiped
   epr-11267 (F3 ED 56) with 04 20… and left PC stuck in MCU garbage. */
static int CEmuAcIsSys16SpeechMcu(const char* name, unsigned sz)
{
	if (sz > 0 && sz <= 0x400u) return 1;
	if (!name || !name[0]) return 0;
	char base[CEMU_ZIP_PATH];
	CEmuAcZipBaseName(name, base, (int)sizeof(base));
	return (strstr(base, "7751") != NULL || strstr(base, "7759") != NULL) ? 1 : 0;
}

static int CEmuAcSoundRomScore(const char* name, unsigned sz, const unsigned char* data)
{
	if (!name || !data || !sz) return -1000;
	char base[CEMU_ZIP_PATH];
	CEmuAcZipBaseName(name, base, (int)sizeof(base));
	size_t n = strlen(base);
	if (n >= 4 && (_stricmp(base + n - 4, ".18") == 0 || _stricmp(base + n - 4, ".19") == 0))
		return -1000;
	if (_strnicmp(base, "speech", 6) == 0) return -1000;
	if (_strnicmp(base, "opr", 3) == 0) return -1000;
	if (_strnicmp(base, "pcm", 3) == 0) return -1000;
	if (strstr(base, "7751") || strstr(base, "7759")) return -1000;
	if (strstr(base, "voice") || strstr(base, "VOICE")) return -500;
	if (sz > 0x20000) return -500;
	int score = 0;
	/* Prefer classic 16/32/64K sound CPUs. 128K is often main/GFX (aburner2 epr-11102). */
	if (sz == 0x8000 || sz == 0x4000 || sz == 0x10000) score += 50;
	else if (sz == 0x20000) {
		/* CPS2 Z80 is banked 128K ? only boost for obvious CPS/QSound names */
		if (strstr(base, "sfx") || strstr(base, "sz3") || strstr(base, ".01")
			|| strstr(base, "qsound"))
			score += 70;
		else
			score -= 40;
	}
	else if (sz == 0x1000 || sz == 0x2000) score += 45; /* Galaga-era WSG Z80 */
	else if (sz < 0x2000) score -= 20;
	/* Z80 boot: DI; IM 1 ? System16 sound CPUs. */
	if (sz >= 4 && data[0] == 0xf3 && data[1] == 0xed && data[2] == 0x56)
		score += 100;
	else if (sz >= 4 && data[0] == 0xf3 && data[1] == 0xed && data[2] == 0x5e)
		score += 100; /* System 32: DI; IM 2 */
	else if (sz >= 2 && data[0] == 0xed && data[1] == 0x56)
		score += 100; /* GNG gg2.bin: IM 1 first */
	else if (sz >= 1 && (data[0] == 0xf3 || data[0] == 0xc3 || data[0] == 0x31))
		score += 40;
	if (_strnicmp(base, "s-prog", 6) == 0 || _strnicmp(base, "sprog", 5) == 0)
		score += 80;
	if (_strnicmp(base, "gg2", 3) == 0) score += 80; /* Capcom GNG sound */
	if (_strnicmp(base, "epr", 3) == 0) score += 40;
	/* OutRun sound CPU is bare "10187" / epr-10187.* */
	if (strstr(base, "10187") || strstr(base, "11112")) score += 80;
	if (strstr(base, "7233") || strstr(base, "7234")) score += 80;
	if (strstr(base, "ic72") || strstr(base, "ic73")) score += 70;
	if (strstr(base, "7231") || strstr(base, "7232")) score -= 120;
	if (strstr(base, "sound") || strstr(base, "snd")) score += 30;
	/* CPS2 QSound program: sfx.01 / sz3.01 */
	if (strstr(base, ".01") || strstr(base, "sfx") || strstr(base, "sz3.01"))
		score += 90;
	/* Konami sound program naming */
	if (strstr(base, ".e01") || strstr(base, "e01") || strstr(base, "e03")
		|| strstr(base, "m05") || strstr(base, "768."))
		score += 70;
	/* shinobi.a7 / epr7535.fz style sound program names */
	if (n >= 3 && (base[n - 2] == 'a' || base[n - 2] == 'A') &&
		(base[n - 1] >= '7' && base[n - 1] <= '9'))
		score += 20;
	return score;
}

static int CEmuAcContainsI(const char* s, const char* needle)
{
	if (!s || !needle || !needle[0]) return 0;
	const size_t nl = strlen(needle);
	for (const char* p = s; *p; p++)
		if (_strnicmp(p, needle, nl) == 0)
			return 1;
	return 0;
}

static int CEmuAcPcmRomScore(const char* name, const char* type, unsigned sz)
{
	if (!name || !sz) return -1000;
	/* An explicit catalog role is authoritative. Numeric QSound filenames
	   (ts2-02, etc.) otherwise score as plausible PCM and get appended after
	   the real sample ROM, exposing Z80 opcodes as signed audio at high banks. */
	if (type && (_stricmp(type, "code") == 0 || _stricmp(type, "sub") == 0
		|| _stricmp(type, "audiocpu") == 0 || _stricmp(type, "program") == 0))
		return -1000;
	char base[CEMU_ZIP_PATH];
	CEmuAcZipBaseName(name, base, (int)sizeof(base));
	int score = 0;
	if (type && (_stricmp(type, "voice") == 0 || _stricmp(type, "adpcm") == 0
		|| _stricmp(type, "pcm") == 0 || _stricmp(type, "sample") == 0))
		score += 100;
	if (_strnicmp(base, "speech", 6) == 0) score += 120;
	if (_strnicmp(base, "pcm", 3) == 0) score += 100;
	if (_strnicmp(base, "opr", 3) == 0) score += 80;
	if (_strnicmp(base, "mpr", 3) == 0 && sz >= 0x10000) score += 90; /* Sega PCM banks */
	if (CEmuAcContainsI(base, "voice")) score += 100;
	if (CEmuAcContainsI(base, "oki")) score += 100;
	if (CEmuAcContainsI(base, "qsound") || CEmuAcContainsI(base, "qs")) score += 100;
	if (CEmuAcContainsI(base, "c352") || CEmuAcContainsI(base, "wav")
		|| CEmuAcContainsI(base, "wave"))
		score += 100;
	if (CEmuAcContainsI(base, "k053260") || CEmuAcContainsI(base, "053260")) score += 100;
	if (CEmuAcContainsI(base, "k054539") || CEmuAcContainsI(base, "054539")) score += 100;
	/* CPS2 sample banks sfx.11 / sz3.11 */
	if ((CEmuAcContainsI(base, "sfx") || CEmuAcContainsI(base, "sz3"))
		&& sz >= 0x100000)
		score += 120;
	if (CEmuAcContainsI(base, ".e17") || CEmuAcContainsI(base, "d93")
		|| CEmuAcContainsI(base, "d04") || CEmuAcContainsI(base, "955d"))
		score += 80;
	if (sz >= 0x20000) score += 40;
	else if (sz <= 0x2000) score -= 20;
	if (_strnicmp(base, "s-prog", 6) == 0 || _strnicmp(base, "sprog", 5) == 0)
		score -= 120;
	if (CEmuAcContainsI(base, ".01") || CEmuAcContainsI(base, ".02"))
		score -= 80; /* program, not sample */
	if (CEmuAcContainsI(base, "7231") || CEmuAcContainsI(base, "7232")
		|| CEmuAcContainsI(base, "snd7231") || CEmuAcContainsI(base, "snd7232"))
		score += 120;
	if (CEmuAcContainsI(base, "sound") || CEmuAcContainsI(base, "snd"))
		score -= 20;
	return score ? score : -100;
}

static int CEmuAcAppendPcm(uint8_t** dst, unsigned* dstSize, const uint8_t* data, unsigned size)
{
	if (!dst || !dstSize || !data || !size) return 0;
	if (*dstSize > 0xffffffffu - size) return 0;
	uint8_t* p = (uint8_t*)realloc(*dst, (size_t)(*dstSize + size));
	if (!p) return 0;
	memcpy(p + *dstSize, data, size);
	*dst = p;
	*dstSize += size;
	return 1;
}

/* hoot catalog <option> value, parsed as C-style hex/decimal. */
static unsigned CEmuAcOptionValue(const CEmuGameEntry* ge, const char* name, unsigned dflt)
{
	if (!ge || !ge->opt) return dflt;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) == 0)
			return (unsigned)strtoul(ge->opt[i].value, NULL, 0);
	}
	return dflt;
}

/* Irem M72 variants whose sound program lives inside the byte-interleaved
   main-CPU ROM pair instead of a separate Z80 dump (airduel, gallop). The
   romlist tags the pair with offsets 0 and 1 and carries the Z80 window in
   the "z80_offset" option; loading them as two overlapping 64K images left
   the Z80 executing V30 data. Returns a 64K image the caller owns. */
static unsigned char* CEmuAcM72InterleavedCode(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	const unsigned char* lo = NULL;
	const unsigned char* hi = NULL;
	unsigned loSize = 0, hiSize = 0;
	int codeCount = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		codeCount++;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		if (r->offset == 0) { lo = data; loSize = sz; }
		else if (r->offset == 1) { hi = data; hiSize = sz; }
	}
	if (codeCount != 2 || !lo || !hi || loSize == 0 || loSize != hiSize)
		return NULL;
	const unsigned interleaved = loSize * 2u;
	unsigned base = CEmuAcOptionValue(ge, "z80_offset", 0);
	if (base >= interleaved || base + 0x10000u > interleaved)
		base = (interleaved > 0x10000u) ? (interleaved - 0x10000u) : 0;
	unsigned char* out = (unsigned char*)malloc(0x10000);
	if (!out) return NULL;
	memset(out, 0, 0x10000);
	unsigned n = interleaved - base;
	if (n > 0x10000u) n = 0x10000u;
	for (unsigned k = 0; k < n; k++) {
		const unsigned src = base + k;
		out[k] = (src & 1u) ? hi[src >> 1] : lo[src >> 1];
	}
	return out;
}

static CChip* CEmuAcPrimaryPcmTarget(CHardAc* hw)
{
	if (!hw) return NULL;
	/* YM2610 boards take ADPCM-A/B through the dedicated loader below, not
	   through SetPcmRom (which would land everything in the ADPCM-B bank).
	   Mega System 1 has two independent OKI banks ? also loaded separately. */
	if (hw->board_ == CEMU_AC_BOARD_VSYSTEM || hw->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw->board_ == CEMU_AC_BOARD_MEGASYSTEM1 || hw->board_ == CEMU_AC_BOARD_KONAMI_GX)
		return NULL;
	if (hw->PcmChip()) return hw->PcmChip();
	if (hw->board_ == CEMU_AC_BOARD_CPS_QS || hw->board_ == CEMU_AC_BOARD_KONAMI_PCM
		|| hw->board_ == CEMU_AC_BOARD_NAMCO_C352
		|| hw->board_ == CEMU_AC_BOARD_NAMCO_WSG)
		return hw->SoundChip();
	return NULL;
}

/* YM2610 ADPCM-A / ADPCM-B banks. Catalog rom types are "adpcma" / "adpcmb"
   with per-file destination offsets (ninjaw loads three 512K ADPCM-A banks). */
static int CEmuAcLoadYm2610Adpcm(CChip* chip, CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!chip || !fs || !ge) return 0;
	int loaded = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		const int isA = (_stricmp(r->type, "adpcma") == 0);
		const int isB = (_stricmp(r->type, "adpcmb") == 0);
		if (!isA && !isB) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		const unsigned off = r->offset > 0 ? (unsigned)r->offset : 0u;
		if (isA) chip->SetAdpcmRom(data, sz, off);
		else chip->SetAdpcmB(data, sz, off);
		loaded++;
	}
	return loaded;
}

/* Mega System 1 sound ROMs are a ROM_LOAD16_BYTE pair: the catalog's first
   "code" member holds D15-D8, the second holds D7-D0. Verified against the
   reset vectors ? edf/64street give SSP=0x000FFFFE, PC=0x00000400 only in
   that order. */
static int CEmuAcLoadMs1Code(uint8_t** dst, unsigned* dstSize,
	CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	const unsigned char* half[2] = { NULL, NULL };
	unsigned halfSize[2] = { 0, 0 };
	int n = 0;
	for (int i = 0; i < ge->romCount && n < 2; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sub") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		half[n] = data;
		halfSize[n] = sz;
		n++;
	}
	/* Single pre-built 68000 image (metamrph/rungun 054539x2 packs ship one
	   256KiB audiocpu instead of even/odd halves). */
	if (n == 1 && half[0] && halfSize[0] >= 0x10000u) {
		uint8_t* p = (uint8_t*)malloc(halfSize[0]);
		if (!p) return 0;
		memcpy(p, half[0], halfSize[0]);
		*dst = p;
		*dstSize = halfSize[0];
		return 1;
	}
	if (n < 2) {
		/* No usable catalog entries: take the two equal-sized members that
		   look like 68000 program halves, in name order. */
		int idx[2] = { -1, -1 };
		int k = 0;
		for (int i = 0; i < fs->fileCount && k < 2; i++) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x8000u || sz > 0x40000u) continue;
			idx[k++] = i;
		}
		if (k < 2 || fs->files[idx[0]].size != fs->files[idx[1]].size) return 0;
		char pa[CEMU_ZIP_PATH], pb[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, fs->files[idx[0]].path, -1, pa, (int)sizeof(pa), NULL, NULL);
		WideCharToMultiByte(CP_ACP, 0, fs->files[idx[1]].path, -1, pb, (int)sizeof(pb), NULL, NULL);
		const int swap = _stricmp(pa, pb) > 0;
		half[0] = fs->files[idx[swap ? 1 : 0]].data;
		half[1] = fs->files[idx[swap ? 0 : 1]].data;
		halfSize[0] = fs->files[idx[0]].size;
		halfSize[1] = halfSize[0];
	}
	const unsigned each = halfSize[0] < halfSize[1] ? halfSize[0] : halfSize[1];
	if (!each) return 0;
	const unsigned total = each * 2u;
	uint8_t* p = (uint8_t*)malloc(total);
	if (!p) return 0;
	for (unsigned i = 0; i < each; i++) {
		p[i * 2 + 0] = half[0][i];
		p[i * 2 + 1] = half[1][i];
	}
	*dst = p;
	*dstSize = total;
	return 1;
}

/* pcm1 -> OKI #0 (0x0A0000), pcm2 -> OKI #1 (0x0C0000). */
static int CEmuAcLoadMs1Oki(uint8_t** dst, unsigned* dstSize,
	CEmuZipFs* fs, const CEmuGameEntry* ge, const char* type)
{
	int loaded = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, type) != 0) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		const unsigned off = r->offset > 0 ? (unsigned)r->offset : 0u;
		const unsigned need = off + sz;
		uint8_t* p = (uint8_t*)realloc(*dst, need > *dstSize ? need : *dstSize);
		if (!p) break;
		if (need > *dstSize) {
			memset(p + *dstSize, 0, need - *dstSize);
			*dstSize = need;
		}
		*dst = p;
		memcpy(*dst + off, data, sz);
		loaded++;
	}
	return loaded;
}

uint8_t CHardAc::DecoRead8(uint32_t phys)
{
	phys &= 0x1fffffu;
	if (phys <= 0x00ffffu) {
		if (decoRom_ && phys < decoRomSize_) return decoRom_[phys];
		return 0xff;
	}
	if (phys >= 0x100000u && phys <= 0x100001u) {
		if (!chip2_) return 0xff;
		return (phys & 1u) ? chip2_->ReadData() : chip2_->ReadStatus();
	}
	if (phys >= 0x110000u && phys <= 0x110001u) {
		if (!chip_) return 0xff;
		return chip_->ReadStatus();
	}
	if (phys >= 0x120000u && phys <= 0x120001u) {
		if (!pcm_) return 0xff;
		return pcm_->ReadStatus();
	}
	if (phys >= 0x130000u && phys <= 0x130001u) {
		if (!pcm2_) return 0xff;
		return pcm2_->ReadStatus();
	}
	if ((phys & ~1u) == 0x140000u) {
		decoLatchReads_++;
		const uint8_t v = soundCmd_;
		soundCmdPending_ = 0;
		if (h6280_) H6280SetInputLine(h6280_, H6280_LINE_IRQ1, H6280_CLEAR_LINE);
		return v;
	}
	if (phys >= 0x1f0000u && phys <= 0x1f1fffu) {
		if (!decoRam_) return 0;
		return decoRam_[phys - 0x1f0000u];
	}
	return 0xff;
}

void CHardAc::DecoWrite8(uint32_t phys, uint8_t v)
{
	phys &= 0x1fffffu;
	/* If MPR1 drifted off $F8, logical $2xxx/$3xxx land in ROM pages and
	   would be lost. Mirror sound-work RAM offsets into decoRam_. */
	if (phys <= 0x00ffffu) {
		const unsigned off = phys & 0x1fffu;
		if (decoRam_ && off >= 0x200u && off < 0x800u) {
			decoRam_[off] = v;
			if (off >= 0x310u && off <= 0x31fu && v != 0)
				decoChanWrites_++;
		}
		return;
	}
	if (phys >= 0x100000u && phys <= 0x100001u) {
		if (!chip2_) return;
		if (!(phys & 1u)) {
			decoYm2203Addr_ = v;
			chip2_->Write(0, v);
		} else {
			chip2_->Write(1, v);
		}
		return;
	}
	if (phys >= 0x110000u && phys <= 0x110001u) {
		if (!chip_) return;
		if (!(phys & 1u)) {
			decoYm2151Addr_ = v;
			chip_->Write(0, v);
		} else {
			chip_->Write(1, v);
			opmWrites_++;
			/* MAME cninja: YM2151 CT bits �� OKI2 ROM bank (256K pages). */
			if (decoYm2151Addr_ == 0x1bu && pcm2_ && pcmRom2_ && pcmRom2Size_ > 0x40000u) {
				const unsigned bank = (v >> 6) & 1u;
				const unsigned off = bank * 0x40000u;
				const unsigned n = (pcmRom2Size_ - off > 0x40000u) ? 0x40000u : (pcmRom2Size_ - off);
				pcm2_->SetPcmRom(pcmRom2_ + off, n);
			}
		}
		return;
	}
	if (phys >= 0x120000u && phys <= 0x120001u) {
		if (!pcm_) return;
		pcm_->Write(0, v);
		decoOkiWrites_++;
		return;
	}
	if (phys >= 0x130000u && phys <= 0x130001u) {
		if (!pcm2_) return;
		pcm2_->Write(0, v);
		decoOkiWrites_++;
		return;
	}
	if (phys >= 0x1f0000u && phys <= 0x1f1fffu) {
		if (!decoRam_) return;
		const unsigned off = phys - 0x1f0000u;
		decoRam_[off] = v;
		/* Count channel-slot installs for BGM bisect. */
		if (off >= 0x310u && off <= 0x31fu && v != 0)
			decoChanWrites_++;
		return;
	}
}

void CHardAc::DecoSyncIrqs()
{
	if (m6502_) {
		/* YM2151 (Atari) / YM3812 (Deco) IRQ → M6502 IRQ; latch on NMI edge. */
		int ymIrq = 0;
		if (chip_) {
			ymIrq = chip_->Irq() ? 1 : 0;
			if (!ymIrq) {
				const uint8_t st = chip_->ReadStatus();
				if (decoCpuKind_ == 5)
					ymIrq = (st & 0x03) ? 1 : 0; /* YM2151 timer A/B */
				else if (st & 0x80)
					ymIrq = 1; /* OPL IRQ flag */
			}
		}
		M6502SetInputLine(m6502_, M6502_LINE_IRQ,
			ymIrq ? M6502_ASSERT_LINE : M6502_CLEAR_LINE);
		if (!soundCmdPending_)
			M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_CLEAR_LINE);
		return;
	}
	if (!h6280_) return;
	/* cninja never remaps MPR1 after boot (always RAM page $F8). If it
	   drifts, STA $2310 hits ROM and is dropped — BGM channels never install. */
	if (H6280Mpr(h6280_, 1) != 0xF8)
		H6280SetMpr(h6280_, 1, 0xF8);
	int ymIrq = 0;
	if (chip_) {
		ymIrq = chip_->Irq() ? 1 : 0;
		if (!ymIrq && (chip_->ReadStatus() & 0x03))
			ymIrq = 1;
	}
	H6280SetInputLine(h6280_, H6280_LINE_IRQ2,
		ymIrq ? H6280_ASSERT_LINE : H6280_CLEAR_LINE);
	H6280SetInputLine(h6280_, H6280_LINE_IRQ1,
		soundCmdPending_ ? H6280_ASSERT_LINE : H6280_CLEAR_LINE);
}

uint8_t CHardAc::DecoM6502Read8(uint16_t addr)
{
	/* karnov: RAM 0000-05FF, latch 0800, YM2203 1000, YM3526 1800, ROM 8000.
	   dec0:   RAM 0000-07FF, YM2203 0800, YM3812 1000, latch 3000, OKI 3800, ROM 8000.
	   dec8:   RAM 0000-05FF, YM2203 2000, YM3812 4000, latch 6000, ROM 8000.
	   actfancr: like dec0 but ROM from 4000.
	   atari sys1: RAM 0000-0FFF (mirror 2000), YM2151 1800, latch 1810,
	   status 1820, POKEY 1870 stub, ROM 4000-FFFF. */
	const int kind = decoCpuKind_;
	if (kind == 5) {
		/* RAM 0000-0FFF mirror 2000; I/O 1800-18xx mirror 3800; ROM 4000+.
		   Do NOT fold with addr&0x1fff alone — that aliases $8000 onto RAM. */
		if (addr < 0x4000u) {
			const uint16_t io = (uint16_t)(addr & 0x1fffu);
			if (io <= 0x0fffu)
				return decoRam_ ? decoRam_[io] : 0;
			if (io >= 0x1800u && io <= 0x1801u)
				return chip_ ? chip_->ReadStatus() : 0;
			if (io == 0x1810u) {
				decoLatchReads_++;
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (io == 0x1820u) {
				/* Atari Sys1 / JSA-I rdio: bit4 = speech ready (active low → 0
				   when idle), bit6 = main→sound ready (active low), bits 2-3
				   tied high. NMI waits on bit4 clear before reading the latch. */
				uint8_t v = (uint8_t)(0x0cu | (atariJsaIo_ & 0x80u));
				if (!soundCmdPending_)
					v |= 0x40u;
				return v;
			}
			if (io >= 0x1870u && io <= 0x187fu)
				return 0;
			return 0xff;
		}
		return (decoRom_ && (unsigned)addr < decoRomSize_) ? decoRom_[addr] : 0xff;
	}
	if (kind == 1) {
		if (addr <= 0x05ffu) return decoM6502Ram_[addr];
		if (addr == 0x0800u) {
			decoLatchReads_++;
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0x1000u || addr == 0x1001u)
			return chip2_ ? chip2_->ReadStatus() : 0;
		if (addr == 0x1800u || addr == 0x1801u)
			return chip_ ? chip_->ReadStatus() : 0;
		if (addr >= 0x8000u) {
			/* 64K images are linear in CPU space; 32K images sit at $8000. */
			if (decoRom_ && decoRomSize_ >= 0x10000u)
				return decoRom_[addr];
			const unsigned off = addr - 0x8000u;
			return (decoRom_ && off < decoRomSize_) ? decoRom_[off] : 0xff;
		}
		return 0xff;
	}
	if (kind == 4) {
		if (addr <= 0x05ffu) return decoM6502Ram_[addr];
		if (addr == 0x2000u || addr == 0x2001u)
			return chip2_ ? chip2_->ReadStatus() : 0;
		if (addr == 0x4000u || addr == 0x4001u)
			return chip_ ? chip_->ReadStatus() : 0;
		if (addr == 0x6000u) {
			decoLatchReads_++;
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0x8000u) {
			const unsigned off = addr - 0x8000u;
			return (decoRom_ && off < decoRomSize_) ? decoRom_[off] : 0xff;
		}
		return 0xff;
	}
	/* dec0 / actfancr / triothep */
	if (addr <= 0x07ffu) return decoM6502Ram_[addr];
	if (addr == 0x0800u || addr == 0x0801u)
		return chip2_ ? chip2_->ReadStatus() : 0;
	if (addr == 0x1000u || addr == 0x1001u)
		return chip_ ? chip_->ReadStatus() : 0;
	if (addr == 0x3000u) {
		decoLatchReads_++;
		soundCmdPending_ = 0;
		return soundCmd_;
	}
	if (addr == 0x3800u)
		return pcm_ ? pcm_->ReadStatus() : 0;
	/* 64K sound ROMs (triothep): linear map so reset@$4000 works.
	   32K images (actfancr/cobracom): only $8000+. */
	if (decoRom_ && decoRomSize_ >= 0x10000u && addr >= 0x4000u)
		return decoRom_[addr];
	if (kind == 3 && addr >= 0x4000u) {
		const unsigned off = addr - 0x4000u;
		return (decoRom_ && off < decoRomSize_) ? decoRom_[off] : 0xff;
	}
	if (addr >= 0x8000u) {
		const unsigned off = addr - 0x8000u;
		return (decoRom_ && off < decoRomSize_) ? decoRom_[off] : 0xff;
	}
	return 0xff;
}

void CHardAc::DecoM6502Write8(uint16_t addr, uint8_t v)
{
	const int kind = decoCpuKind_;
	if (kind == 5) {
		if (addr < 0x4000u) {
			const uint16_t io = (uint16_t)(addr & 0x1fffu);
			if (io <= 0x0fffu) {
				if (decoRam_) decoRam_[io] = v;
				return;
			}
			if (io >= 0x1800u && io <= 0x1801u) {
				if (!chip_) return;
				if (!(io & 1u)) { decoYm2151Addr_ = v; chip_->Write(0, v); }
				else {
					chip_->Write(1, v);
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
				return;
			}
			if (io == 0x1820u || io == 0x2a04u) {
				/* /WRIO — bank + YM reset bit0; keep bit7 for self-test echo. */
				atariJsaIo_ = v;
				return;
			}
			if (io == 0x1824u || io == 0x2806u || io == 0x1826u) {
				/* /IRQACK (Sys1 mirrors) — drop soft YM IRQ latch. */
				if (chip_) chip_->AckIrq();
				return;
			}
			return;
		}
		return;
	}
	if (kind == 1) {
		if (addr <= 0x05ffu) { decoM6502Ram_[addr] = v; return; }
		if (addr == 0x1000u || addr == 0x1001u) {
			if (!chip2_) return;
			if (!(addr & 1u)) { decoYm2203Addr_ = v; chip2_->Write(0, v); }
			else { chip2_->Write(1, v); }
			return;
		}
		if (addr == 0x1800u || addr == 0x1801u) {
			if (!chip_) return;
			if (!(addr & 1u)) { decoYm2151Addr_ = v; chip_->Write(0, v); }
			else { chip_->Write(1, v); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
			return;
		}
		return;
	}
	if (kind == 4) {
		if (addr <= 0x05ffu) { decoM6502Ram_[addr] = v; return; }
		if (addr == 0x2000u || addr == 0x2001u) {
			if (!chip2_) return;
			if (!(addr & 1u)) { decoYm2203Addr_ = v; chip2_->Write(0, v); }
			else { chip2_->Write(1, v); }
			return;
		}
		if (addr == 0x4000u || addr == 0x4001u) {
			if (!chip_) return;
			if (!(addr & 1u)) { decoYm2151Addr_ = v; chip_->Write(0, v); }
			else { chip_->Write(1, v); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
			return;
		}
		return;
	}
	if (addr <= 0x07ffu) { decoM6502Ram_[addr] = v; return; }
	if (addr == 0x0800u || addr == 0x0801u) {
		if (!chip2_) return;
		if (!(addr & 1u)) { decoYm2203Addr_ = v; chip2_->Write(0, v); }
		else { chip2_->Write(1, v); }
		return;
	}
	if (addr == 0x1000u || addr == 0x1001u) {
		if (!chip_) return;
		if (!(addr & 1u)) { decoYm2151Addr_ = v; chip_->Write(0, v); }
		else { chip_->Write(1, v); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
		return;
	}
	if (addr == 0x3800u) {
		if (pcm_) { pcm_->Write(0, v); decoOkiWrites_++; }
		return;
	}
}

static uint8_t DecoM6502BusRead(void* ctx, uint16_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->DecoM6502Read8(addr) : 0xff;
}

static void DecoM6502BusWrite(void* ctx, uint16_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->DecoM6502Write8(addr, data);
}

int CHardAc::LoadRomsDeco(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if ((!h6280_ && !m6502_) || !fs || !ge) return 0;
	if (decoRom_) { free(decoRom_); decoRom_ = NULL; decoRomSize_ = 0; }
	if (!decoRam_) {
		decoRam_ = (uint8_t*)malloc(0x2000);
		if (!decoRam_) return 0;
	}
	memset(decoRam_, 0, 0x2000);
	memset(decoM6502Ram_, 0, sizeof(decoM6502Ram_));

	/* Prefer catalog "code"/"sub"/"audiocpu" 64K members; else score zip. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sub") != 0
			&& _stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x4000u || sz > 0x20000u) continue;
		uint8_t* p = (uint8_t*)realloc(decoRom_, sz);
		if (!p) continue;
		decoRom_ = p;
		memcpy(decoRom_, data, sz);
		decoRomSize_ = sz;
		break;
	}
	if (!decoRomSize_) {
		int best = -1, bestScore = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 0x4000u || sz > 0x20000u) continue;
			const int sc = CEmuAcSoundRomScore(pathA, sz, fs->files[i].data);
			if (sc > bestScore) { bestScore = sc; best = i; }
		}
		if (best >= 0) {
			const unsigned sz = fs->files[best].size;
			uint8_t* p = (uint8_t*)malloc(sz);
			if (p) {
				memcpy(p, fs->files[best].data, sz);
				decoRom_ = p;
				decoRomSize_ = sz;
			}
		}
	}
	if (!decoRomSize_) return 0;

	/* OKI sample ROMs (HuC6280 / dec0). */
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
	if (pcmRom2_) { free(pcmRom2_); pcmRom2_ = NULL; pcmRom2Size_ = 0; }
	if (pcm_) {
		int idx[8];
		int n = 0;
		for (int i = 0; i < fs->fileCount && n < (int)_countof(idx); i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (CEmuAcPcmRomScore(pathA, "", fs->files[i].size) <= 0) continue;
			if (fs->files[i].size == decoRomSize_
				&& decoRom_ && memcmp(fs->files[i].data, decoRom_,
					decoRomSize_ > 16u ? 16u : decoRomSize_) == 0)
				continue;
			idx[n++] = i;
		}
		for (int a = 0; a < n; a++) {
			for (int b = a + 1; b < n; b++) {
				if (fs->files[idx[a]].size > fs->files[idx[b]].size) {
					int t = idx[a]; idx[a] = idx[b]; idx[b] = t;
				}
			}
		}
		if (n >= 1)
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[idx[0]].data, fs->files[idx[0]].size);
		if (n >= 2)
			CEmuAcAppendPcm(&pcmRom2_, &pcmRom2Size_, fs->files[idx[1]].data, fs->files[idx[1]].size);
		else if (n == 1 && pcmRomSize_ > 0x40000u) {
			const unsigned half = pcmRomSize_ / 2u;
			CEmuAcAppendPcm(&pcmRom2_, &pcmRom2Size_, pcmRom_ + half, pcmRomSize_ - half);
			pcmRomSize_ = half;
		}
	}
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
	if (pcm2_ && pcmRom2Size_) pcm2_->SetPcmRom(pcmRom2_, pcmRom2Size_);

	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	decoLatchReads_ = 0;
	decoOkiWrites_ = 0;
	decoChanWrites_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	decoYm2203Addr_ = 0;
	decoYm2151Addr_ = 0;
	if (chip_) chip_->Reset();
	if (chip2_) chip2_->Reset();
	if (pcm_) pcm_->Reset();
	if (pcm2_) pcm2_->Reset();

	if (m6502_) {
		M6502SetBus(m6502_, this, DecoM6502BusRead, DecoM6502BusWrite);
		M6502Reset(m6502_);
		return 1;
	}
	CEmuH6280BusSetDeco(this);
	CEmuH6280BusAttach(h6280_, this);
	H6280Reset(h6280_);
	return 1;
}

int CHardAc::LoadRomsAtariSys1(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* Build a 64K 6502 image from catalog code ROMs at their offsets
	   (marble 8000/C000, indytemp 4000/8000/C000, …). */
	if (!m6502_ || !fs || !ge) return 0;
	if (decoRom_) { free(decoRom_); decoRom_ = NULL; decoRomSize_ = 0; }
	if (!decoRam_) {
		decoRam_ = (uint8_t*)malloc(0x2000);
		if (!decoRam_) return 0;
	}
	memset(decoRam_, 0, 0x2000);
	memset(decoM6502Ram_, 0, sizeof(decoM6502Ram_));

	uint8_t* img = (uint8_t*)calloc(1, 0x10000);
	if (!img) return 0;
	int any = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sub") != 0
			&& _stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		unsigned off = (r->offset > 0) ? (unsigned)r->offset : 0;
		if (off >= 0x10000u) continue;
		unsigned n = sz;
		if (off + n > 0x10000u) n = 0x10000u - off;
		memcpy(img + off, data, n);
		any = 1;
	}
	if (!any) {
		/* Fallback: pack zip members ascending by size into 8000+. */
		unsigned off = 0x4000u;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x1000u || sz > 0x8000u) continue;
			if (off >= 0x10000u) break;
			unsigned n = sz;
			if (off + n > 0x10000u) n = 0x10000u - off;
			memcpy(img + off, fs->files[i].data, n);
			off += n;
			any = 1;
		}
	}
	if (!any) { free(img); return 0; }
	decoRom_ = img;
	decoRomSize_ = 0x10000;

	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	decoLatchReads_ = 0;
	if (chip_) chip_->Reset();

	M6502SetBus(m6502_, this, DecoM6502BusRead, DecoM6502BusWrite);
	M6502Reset(m6502_);
	return 1;
}

static mc6809byte__t NamcoM6809BusRead(mc6809__t* cpu, mc6809addr__t addr, bool /*iscode*/)
{
	CHardAc* hw = (CHardAc*)(cpu ? cpu->user : NULL);
	return hw ? hw->NamcoM6809Read8((uint16_t)addr) : 0xff;
}

static void NamcoM6809BusWrite(mc6809__t* cpu, mc6809addr__t addr, mc6809byte__t data)
{
	CHardAc* hw = (CHardAc*)(cpu ? cpu->user : NULL);
	if (hw) hw->NamcoM6809Write8((uint16_t)addr, (uint8_t)data);
}

static void NamcoM6809BusFault(mc6809__t* cpu, mc6809fault__t fault)
{
	if (cpu) longjmp(cpu->err, (int)fault);
}

/* File-static so CHardAc layout stays ABI-stable vs prebuilt smoke objs. */
static uint8_t s_acNamcoMailFlag = 0x60;

void CHardAc::SetNamcoMailFlag(uint8_t f)
{
	s_acNamcoMailFlag = f ? f : 0x60u;
}

uint8_t CHardAc::NamcoMailFlag() const
{
	return s_acNamcoMailFlag ? s_acNamcoMailFlag : 0x60u;
}

/* Peek Sys2 song-table record type (first byte at the banked pointer).
   Type $20 = bit6-mailbox BGM path; other types need status without bit6. */
static int CEmuAcSys2SongRecType(const uint8_t* rom, unsigned romSize, unsigned songLo)
{
	if (!rom || romSize < 0x8000u) return -1;
	const uint8_t* b1 = rom + 0x4000u;
	const unsigned base = ((unsigned)b1[0] << 8) | (unsigned)b1[1];
	const unsigned off = base + (songLo & 0xffu) * 2u;
	if (off + 1u >= 0x4000u) return -1;
	const unsigned hi = b1[off];
	const unsigned lo = b1[off + 1u];
	if (!hi && !lo) return -1;
	const unsigned bankA = ((hi >> 2) & 0xf0u) + 0x10u;
	const unsigned bank = (bankA >> 4) & 0x0fu;
	const unsigned ptr = ((hi & 0x3fu) << 8) | lo;
	const unsigned addr = bank * 0x4000u + ptr;
	if (addr >= romSize) return -1;
	return (int)rom[addr];
}

int CHardAc::Sys2SongRecType(unsigned songLo) const
{
	return CEmuAcSys2SongRecType(soundRom_, soundRomSize_, songLo);
}

void CHardAc::NamcoM6809SetBank(unsigned bank)
{
	namcoBank_ = bank;
}

uint8_t CHardAc::NamcoM6809Read8(uint16_t addr)
{
	/* Mappy / Dig Dug 2 / Super Pac-Man: M6809 + namco_15xx amap @0000-03FF,
	   latch @2000, ROM @E000/F000. */
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		if (addr <= 0x03ffu)
			return chip_ ? CEmuChipC30Read(chip_, addr) : 0xff;
		if (addr >= 0xe000u) {
			if (!soundRom_ || !soundRomSize_) return 0xff;
			/* MAME: 8K at $E000; 4K titles (superpac/pacnpal) load at $F000
			   and leave $E000 open — mirror the 4K so any E000 fetch is safe. */
			if (soundRomSize_ <= 0x1000u) {
				const unsigned off = (addr - 0xf000u) & 0x0fffu;
				return soundRom_[off];
			}
			const unsigned off = addr - 0xe000u;
			return (off < soundRomSize_) ? soundRom_[off] : 0xff;
		}
		return 0xff;
	}
	if (addr <= 0x3fffu) {
		if (!soundRom_ || !soundRomSize_) return 0xff;
		const unsigned banks = soundRomSize_ / 0x4000u;
		const unsigned b = banks ? (namcoBank_ % banks) : 0u;
		const unsigned off = b * 0x4000u + (addr & 0x3fffu);
		return (off < soundRomSize_) ? soundRom_[off] : 0xff;
	}
	if (addr == 0x4000u || addr == 0x4001u)
		return chip_ ? chip_->ReadStatus() : 0x80;
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 && addr >= 0x5000u && addr <= 0x5fffu) {
		const unsigned off = addr & 0x3ffu;
		/* After RAM-test the firmware CLR $5000/$5001 then JMP $D004; the
		   MCU is expected to keep $5000=$A6 so LDD passes the alive check. */
		if (off == 0u && namcoCus30_[2] == 0xa6u)
			return 0xa6;
		/* VBlank latch: IRQ handler INCs $53F3, but level/timing quirks can
		   leave the main wait at $D02A starved. Keep it readable as pending. */
		if (off == 0x3f3u)
			return 1;
		if (pcm_ && off >= 0x100u && off < 0x140u) {
			uint8_t regs[0x40];
			if (pcm_->GetRegSnapshot(regs, sizeof(regs)) > 0)
				return regs[off - 0x100u];
		}
		return namcoCus30_[off];
	}
	/* Sys2: C140 @ $5000/$6000 (8-bit), DPRAM mailbox @ $7000. */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_
		&& ((addr >= 0x5000u && addr <= 0x5fffu)
			|| (addr >= 0x6000u && addr <= 0x6fffu))) {
		return CEmuChipC140Read(pcm_, addr & 0x1ffu);
	}
	if (addr >= 0x7000u && addr <= 0x7fffu) {
		const unsigned off = addr & 0x7ffu;
		/* berabohm-class Sys1: MCU alive at $7000=$A6; $7001 stays 0. */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 && off == 0u)
			return 0xa6;
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 && off == 1u)
			return 0x00;
		/* Sys2 wake: hold $703A=$A6 like Sys1 $5000 after MCU handshake. */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2
			&& off == 0x3au && namcoTriRam_[0x3a] == 0xa6u)
			return 0xa6;
		return namcoTriRam_[off];
	}
	if (addr >= 0x8000u && addr <= 0x9fffu)
		return namcoWorkRam_[addr & 0x1fffu];
	if (addr >= 0xc000u) {
		if (!soundRom_) return 0xff;
		const unsigned off = addr - 0xc000u;
		return (off < soundRomSize_) ? soundRom_[off] : 0xff;
	}
	return 0xff;
}

void CHardAc::NamcoM6809Write8(uint16_t addr, uint8_t v)
{
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		if (addr <= 0x03ffu) {
			if (chip_) chip_->Write(addr, v);
			opmWrites_++;
			return;
		}
		/* Mainlatch: $2000/$6000 = IRQ ack (MAME). $2003/$2007 = sound_enable
		   on several Mappy-era sets (Q3/Q7 of the LS259). */
		if (addr == 0x2000u || addr == 0x6000u) {
			if (namcoM6809_) NamcoCpuRaw(namcoM6809_)->irq = false;
			namcoIrqAssert_ = 0;
			return;
		}
		if (addr == 0x2003u || addr == 0x2007u || addr == 0x6003u || addr == 0x6007u) {
			CEmuChipC30SetEnable(chip_, (v & 1u) ? 1 : 1); /* level: any write enables */
			return;
		}
		return;
	}
	if (addr == 0x4000u || addr == 0x4001u) {
		if (!chip_) return;
		if (!(addr & 1u)) {
			namcoYmAddr_ = v;
			chip_->Write(0, v);
		} else {
			chip_->Write(1, v);
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 && addr >= 0x5000u && addr <= 0x5fffu) {
		const unsigned off = addr & 0x3ffu;
		namcoCus30_[off] = v;
		if (pcm_) pcm_->Write(off, v);
		return;
	}
	/* Sys2: C140 @ $5000/$6000; DPRAM @ $7000 (not C140). */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_
		&& ((addr >= 0x5000u && addr <= 0x5fffu)
			|| (addr >= 0x6000u && addr <= 0x6fffu))) {
		pcm_->Write(addr & 0x1ffu, v);
		return;
	}
	if (addr >= 0x7000u && addr <= 0x7fffu) {
		const unsigned off = addr & 0x7ffu;
		namcoTriRam_[off] = v;
		/* burnforc: ROM-check fail posts $77FE=$FF then waits on MCU at $77FD
		   for $A6 then $6A (C65/C68 DPRAM handshake). */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && off == 0x7feu && v == 0xffu)
			namcoTriRam_[0x7fd] = 0xa6;
		return;
	}
	if (addr >= 0x8000u && addr <= 0x9fffu) {
		namcoWorkRam_[addr & 0x1fffu] = v;
		return;
	}
	if (addr == 0xc000u || addr == 0xc001u) {
		/* MAME namcos1/2: bank = data >> 4. */
		NamcoM6809SetBank((unsigned)(v >> 4));
		return;
	}
	if (addr == 0xd001u) {
		/* Watchdog nop in MAME; soft-MCU advances $77FD A6→6A after CPU
		   sampled A6 into A (CMPA still sees register A). */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2
			&& namcoTriRam_[0x7fe] == 0xffu && namcoTriRam_[0x7fd] == 0xa6u)
			namcoTriRam_[0x7fd] = 0x6a;
		return;
	}
	if (addr == 0xe000u) {
		namcoIrqAssert_ = 0;
		if (namcoM6809_) NamcoCpuRaw(namcoM6809_)->irq = false;
		return;
	}
}

unsigned g_namcoSync = 0;
unsigned g_namcoVblank = 0;

void CHardAc::NamcoM6809SyncIrqs()
{
	if (!namcoM6809_) return;
	mc6809__t* cpu = NamcoCpuRaw(namcoM6809_);
	g_namcoSync++;
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		/* Deferred song IRQ once I is clear (SetSoundCommand may fire
		   during boot with I=1 and must not be lost). */
		if (namcoIrqAssert_ && !cpu->cc.i) {
			cpu->irq = true;
			namcoIrqAssert_ = 0;
		}
		/* MAME mappy: vblank asserts sound CPU IRQ0 when sub_irq_mask is set.
		   Soft-assert ~60 Hz; latch write at $2000 clears. */
		if ((uint64_t)cpu->cycles >= namcoNextVblank_) {
			namcoNextVblank_ = (uint64_t)cpu->cycles + (uint64_t)cpuHz_ / 60u;
			g_namcoVblank++;
			if (!cpu->cc.i)
				cpu->irq = true;
		}
		return;
	}
	/* YM2151 timer → FIRQ / Sys2 C140 INT1 → FIRQ.
	   Hold the FIRQ line while the source is live — clearing it every other
	   SyncIrqs tick (edge-only) lost the pulse before mc6809 sampled F. */
	const int ym = (chip_ && chip_->Irq()) ? 1 : 0;
	const int c140 = (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_ && pcm_->Irq()) ? 1 : 0;
	const int wantFirq = (ym || c140) ? 1 : 0;
	if (wantFirq) {
		namcoFirqAssert_ = 1;
		cpu->firq = true;
	} else {
		namcoFirqAssert_ = 0;
		cpu->firq = false;
	}
	/* Deferred command IRQ once I is clear. */
	if (namcoIrqAssert_ && !cpu->cc.i) {
		cpu->irq = true;
		namcoIrqAssert_ = 0;
	}
	/* VBlank / periodic IRQ. MAME Sys2: irq0_line_hold @ 2*60 Hz; Sys1 keeps 60. */
	if ((uint64_t)cpu->cycles >= namcoNextVblank_) {
		const unsigned hz = (board_ == CEMU_AC_BOARD_NAMCO_SYS2) ? 120u : 60u;
		namcoNextVblank_ = (uint64_t)cpu->cycles + (uint64_t)cpuHz_ / hz;
		g_namcoVblank++;
		namcoWorkRam_[0x119] = 0x0e;
		/* Pacmania polls DP+$1C (often $901C); blazer uses $8119. */
		namcoWorkRam_[0x11c] = 0x0e;
		namcoWorkRam_[0x101c & 0x1fff] = 0x0e;
		if (!cpu->cc.i)
			cpu->irq = true;
		/* Sys2: prefer C140 INT1 for FIRQ. Fall back to vblank pulse only when
		   the timer is not yet enabled so boot can reach ANDCC #$BF / idle. */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && !cpu->cc.f && !c140)
			cpu->firq = true;
		else if (!cpu->cc.f && soundRom_ && soundRomSize_ >= 0x4000u) {
			const unsigned firq = ((unsigned)soundRom_[0x3ff6u] << 8)
				| (unsigned)soundRom_[0x3ff7u];
			if (firq >= 0xc000u && firq < 0xfff0u)
				cpu->firq = true;
		}
	}
}

int CHardAc::LoadRomsNamcoM6809(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!namcoM6809_ || !fs || !ge) return 0;
	if (soundRom_) { free(soundRom_); soundRom_ = NULL; soundRomSize_ = 0; }
	memset(namcoTriRam_, 0, sizeof(namcoTriRam_));
	memset(namcoWorkRam_, 0, sizeof(namcoWorkRam_));
	memset(namcoCus30_, 0, sizeof(namcoCus30_));
	namcoBank_ = 0;
	namcoYmAddr_ = 0;
	namcoIrqAssert_ = 0;
	namcoFirqAssert_ = 0;
	namcoNextVblank_ = 0;
	namcoMailOff_ = 0x100;
	/* Sys2 mailbox hi: bit6 set (0x40/0x60) selects assault D27D channel-init
	   (writes $902B gate). bit6 clear takes the empty $9352 path → SILENT. */
	s_acNamcoMailFlag = 0x60;

	/* ---- Mappy-era WSG6809: 4K/8K sound CPU + 256B wave PROM ---- */
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		int best = -1, bestScore = -1;
		int prom = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz == 256u || sz == 512u) {
				prom = i;
				continue;
			}
			if (sz != 0x1000u && sz != 0x2000u && sz != 0x4000u) continue;
			int sc = 40;
			if (strstr(pathA, "snd") || strstr(pathA, "sound") || strstr(pathA, "1k")
				|| strstr(pathA, "3m") || strstr(pathA, ".1k"))
				sc += 30;
			if (sz == 0x2000u || sz == 0x1000u) sc += 20;
			if (sc > bestScore) { bestScore = sc; best = i; }
		}
		if (best < 0) return 0;
		const unsigned sz = fs->files[best].size;
		uint8_t* p = (uint8_t*)malloc(sz);
		if (!p) return 0;
		memcpy(p, fs->files[best].data, sz);
		soundRom_ = p;
		soundRomSize_ = sz;
		soundCmd_ = 0;
		soundCmdPending_ = 0;
		opmWrites_ = 0;
		cpuCycles_ = 0;
		if (chip_) chip_->Reset();
		if (prom >= 0 && chip_)
			chip_->SetPcmRom(fs->files[prom].data, fs->files[prom].size);
		CEmuChipC30SetEnable(chip_, 1);
		{
			mc6809__t* cpu = NamcoCpuRaw(namcoM6809_);
			cpu->user = this;
			cpu->read = NamcoM6809BusRead;
			cpu->write = NamcoM6809BusWrite;
			cpu->fault = NamcoM6809BusFault;
			mc6809_reset(cpu);
			/* Stack just under $0400 — digdug2 workspaces reach ~$03C0. */
			cpu->S.w = 0x03f0u;
			cpu->nmi_armed = true;
		}
		return 1;
	}

	/* Prefer catalog audiocpu/sound/code; else best *s0* / *snd* member.
	   Concatenate every 16K-aligned sound bank (s0+s1) — bankswitch bits 4-6
	   select 16K windows across the full region (MAME namcos1). */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0
			&& _stricmp(r->type, "code") != 0 && _stricmp(r->type, "cpu") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x4000u) continue;
		CEmuAcAppendPcm(&soundRom_, &soundRomSize_, data, sz);
	}
	/* If catalog only listed s0 (64K), still pull sibling s1/snd from the zip. */
	if (soundRomSize_ > 0 && soundRomSize_ <= 0x10000u) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 0x4000u || sz > 0x20000u) continue;
			if (soundRom_ && sz == soundRomSize_
				&& memcmp(fs->files[i].data, soundRom_, sz > 16u ? 16u : sz) == 0)
				continue;
			if (!(strstr(pathA, "s1") || strstr(pathA, "snd1") || strstr(pathA, "sound1")
				|| strstr(pathA, "_s1") || strstr(pathA, "-snd1")))
				continue;
			CEmuAcAppendPcm(&soundRom_, &soundRomSize_, fs->files[i].data, sz);
		}
	}
	if (!soundRomSize_) {
		int idxs[16];
		int nIdx = 0;
		for (int i = 0; i < fs->fileCount && nIdx < (int)_countof(idxs); i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 0x4000u || sz > 0x40000u) continue;
			int sc = CEmuAcSoundRomScore(pathA, sz, fs->files[i].data);
			if (strstr(pathA, "snd") || strstr(pathA, "s0") || strstr(pathA, "s1")
				|| strstr(pathA, "sound"))
				sc += 50;
			if (sc < 0) continue;
			idxs[nIdx++] = i;
		}
		/* Prefer name order so s0 precedes s1. */
		for (int a = 0; a < nIdx; a++) {
			for (int b = a + 1; b < nIdx; b++) {
				char pa[CEMU_ZIP_PATH], pb[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[idxs[a]].path, -1, pa, (int)sizeof(pa), NULL, NULL);
				WideCharToMultiByte(CP_ACP, 0, fs->files[idxs[b]].path, -1, pb, (int)sizeof(pb), NULL, NULL);
				if (_stricmp(pa, pb) > 0) {
					int t = idxs[a]; idxs[a] = idxs[b]; idxs[b] = t;
				}
			}
		}
		for (int k = 0; k < nIdx; k++) {
			int i = idxs[k];
			CEmuAcAppendPcm(&soundRom_, &soundRomSize_, fs->files[i].data, fs->files[i].size);
		}
	}
	/* Sys2: also load C140 voice ROMs when present.
	   Prefer *voi* / catalog pcm; never swallow the audiocpu image as samples. */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_) {
		if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
		/* Pass 1: catalog voice/pcm entries. */
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "pcm") != 0 && _stricmp(r->type, "voice") != 0
				&& _stricmp(r->type, "c140") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || sz < 0x10000u) continue;
			if (soundRom_ && sz == soundRomSize_
				&& memcmp(data, soundRom_, sz > 16u ? 16u : sz) == 0)
				continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, data, sz);
		}
		/* Pass 2: zip members named voi/c140 (skip snd/s0 sound CPU). */
		if (!pcmRomSize_) {
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				const unsigned sz = fs->files[i].size;
				if (sz < 0x10000u) continue;
				if (CEmuAcContainsI(pathA, "snd") || CEmuAcContainsI(pathA, "s0")
					|| CEmuAcContainsI(pathA, "s1") || CEmuAcContainsI(pathA, "sound"))
					continue;
				if (!CEmuAcContainsI(pathA, "voi") && !CEmuAcContainsI(pathA, "c140")
					&& !CEmuAcContainsI(pathA, "voice")
					&& CEmuAcPcmRomScore(pathA, "pcm", sz) < 100)
					continue;
				if (soundRom_ && sz == soundRomSize_
					&& memcmp(fs->files[i].data, soundRom_, sz > 16u ? 16u : sz) == 0)
					continue;
				CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, sz);
			}
		}
		if (pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
	}

	if (!soundRomSize_) return 0;

	/* Sys2: detect mailbox base — burnforc uses LDX #$7110, finallap/assault #$7100. */
	namcoMailOff_ = 0x100;
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && soundRom_ && soundRomSize_ >= 0x4000u) {
		int hit7100 = 0, hit7110 = 0;
		const unsigned n = soundRomSize_ < 0x20000u ? soundRomSize_ : 0x20000u;
		for (unsigned i = 0; i + 2u < n; i++) {
			if (soundRom_[i] == 0x8eu && soundRom_[i + 1u] == 0x71u) {
				if (soundRom_[i + 2u] == 0x00u) hit7100++;
				if (soundRom_[i + 2u] == 0x10u) hit7110++;
			}
		}
		if (hit7110 > hit7100)
			namcoMailOff_ = 0x110;
	}

	soundCmd_ = 0;
	soundCmdPending_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();

	{
		mc6809__t* cpu = NamcoCpuRaw(namcoM6809_);
		cpu->user = this;
		cpu->read = NamcoM6809BusRead;
		cpu->write = NamcoM6809BusWrite;
		cpu->fault = NamcoM6809BusFault;
		mc6809_reset(cpu);
		/* Work RAM is $8000-$9FFF only (MAME namcos1/2). $A000 is unmapped —
		   FIRQ/IRQ pushes there were lost and left Sys2 SILENT. */
		cpu->S.w = 0x9ff0u;
		cpu->nmi_armed = true;
		/* Sys1 MCU handshake variants:
		   - dspirit/pacmania: LDD $5000; A or B == $A6 ($5001=$A6)
		   - berabohm/shadowld/blastoff/mmaze: LDA $7000; A == $A6, then
		     wait until $7001 == 0 (soft-MCU mailbox at TRI-RAM base). */
		namcoCus30_[0] = 0;
		namcoCus30_[1] = 0xa6;
		namcoCus30_[2] = 0xa6; /* sticky $5000 alive for post-CLR re-check */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
			namcoTriRam_[0] = 0xa6;
			namcoTriRam_[1] = 0x00;
		}
		/* Sys2 (finallap): LDA $703A / CMPA #$A6 spin; then TST $703B==0. */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
			namcoTriRam_[0x3a] = 0xa6;
			namcoTriRam_[0x3b] = 0x00;
		}
	}
	return 1;
}

uint8_t CHardAc::H8Read8(uint32_t addr)
{
	addr &= 0xffffffu;
	if (h8Rom_ && addr < h8RomSize_)
		return h8Rom_[addr];
	if (h8MapKind_ == 0) {
		/* System 12 */
		if (addr >= 0x080000u && addr < 0x090000u && h8Shared_) {
			const unsigned o = addr - 0x080000u;
			/* Host auto-acks busy at +0x4050 so the driver wait loop exits. */
			if (o == 0x4050u) return 0;
			return h8Shared_[o];
		}
		if (addr >= 0x280000u && addr < 0x288000u && chip_) {
			/* C352 is write-mostly; status reads return 0. */
			return 0;
		}
		if (addr >= 0x300000u && addr < 0x300040u)
			return 0xff;
		return 0xff;
	}
	/* ND-1 */
	if (addr >= 0x200000u && addr < 0x210000u && h8Shared_) {
		const unsigned o = addr - 0x200000u;
		if (o == 0x4050u) return 0; /* auto-ack busy like Sys12 */
		return h8Shared_[o];
	}
	if (addr >= 0xa00000u && addr < 0xa08000u)
		return 0;
	/* DSW / inputs — open bus high keeps POST from hanging. */
	if (addr >= 0xc00000u && addr < 0xc00040u)
		return 0xff;
	return 0xff;
}

void CHardAc::H8Write8(uint32_t addr, uint8_t v)
{
	addr &= 0xffffffu;
	if (h8MapKind_ == 0) {
		if (addr >= 0x080000u && addr < 0x090000u && h8Shared_) {
			h8Shared_[addr - 0x080000u] = v;
			return;
		}
		if (addr >= 0x280000u && addr < 0x288000u && chip_) {
			/* C352 is a 16-bit device but H8 often does MOV.B to one lane.
			   Latch-only on even previously dropped vol/freq high bytes that
			   never saw a paired odd write; merge like MAME mem_mask. */
			const unsigned reg = (unsigned)((addr - 0x280000u) >> 1);
			uint16_t* shadow = &h8C352Shadow_[reg & 0x3ffu];
			if (!(addr & 1u)) {
				h8C352Hi_ = v;
				h8C352HiValid_ = 1;
				*shadow = (uint16_t)(((uint16_t)v << 8) | (*shadow & 0x00ffu));
				chip_->Write(reg, *shadow);
				h8C352Writes_++;
				return;
			}
			if (h8C352HiValid_) {
				*shadow = (uint16_t)(((uint16_t)h8C352Hi_ << 8) | v);
				h8C352HiValid_ = 0;
			} else {
				*shadow = (uint16_t)((*shadow & 0xff00u) | v);
			}
			chip_->Write(reg, *shadow);
			h8C352Writes_++;
			return;
		}
		return;
	}
	if (addr >= 0x200000u && addr < 0x210000u && h8Shared_) {
		h8Shared_[addr - 0x200000u] = v;
		return;
	}
	if (addr >= 0xa00000u && addr < 0xa08000u && chip_) {
		const unsigned reg = (unsigned)((addr - 0xa00000u) >> 1);
		uint16_t* shadow = &h8C352Shadow_[reg & 0x3ffu];
		if (!(addr & 1u)) {
			h8C352Hi_ = v;
			h8C352HiValid_ = 1;
			*shadow = (uint16_t)(((uint16_t)v << 8) | (*shadow & 0x00ffu));
			chip_->Write(reg, *shadow);
			h8C352Writes_++;
			return;
		}
		if (h8C352HiValid_) {
			*shadow = (uint16_t)(((uint16_t)h8C352Hi_ << 8) | v);
			h8C352HiValid_ = 0;
		} else {
			*shadow = (uint16_t)((*shadow & 0xff00u) | v);
		}
		chip_->Write(reg, *shadow);
		h8C352Writes_++;
	}
}

void CHardAc::M37702InjectSong(uint16_t cmd)
{
	/* Real M37702: NA1 mailbox + IRQ0; Sys11 shared-RAM strobe + IRQ0. */
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
	if (m37702MapKind_ == 1) {
		/* NA-1/NB: 8-word mailbox at MCU 0x800; main write raises IRQ0. */
		m37702Mailbox_[0] = cmd;
		m37702Mailbox_[1] = (uint16_t)(0x4000u | (cmd & 0x3fffu));
		if (m37702_) M37702SetInputLine(m37702_, M37710_LINE_IRQ0, M37702_HOLD_LINE);
		return;
	}
	if (!h8Shared_) return;
	const uint16_t w = (uint16_t)(0x4000u | (cmd & 0x3fffu));
	h8Shared_[0x0100] = (uint8_t)(w >> 8);
	h8Shared_[0x0101] = (uint8_t)(w & 0xff);
	h8Shared_[0x4050] = 0;
	const uint8_t hi = (uint8_t)(cmd >> 8);
	const uint8_t lo = (uint8_t)(cmd & 0xff);
	h8Shared_[0x0000] = hi;
	h8Shared_[0x0001] = lo;
	h8Shared_[0x0004] = 1;
	h8Shared_[0x0005] = lo;
	if (m37702_) M37702SetInputLine(m37702_, M37710_LINE_IRQ0, M37702_HOLD_LINE);
}

uint8_t CHardAc::M37702Read8(uint32_t addr)
{
	addr &= 0xffffffu;
	if (m37702MapKind_ == 1) {
		/* NA-1 C69 map (MAME namcona1_mcu_map). */
		if (addr >= 0x800u && addr <= 0xfffu) {
			const unsigned wi = ((unsigned)(addr - 0x800u) >> 1) & 7u;
			const uint16_t w = m37702Mailbox_[wi];
			return (addr & 1u) ? (uint8_t)(w >> 8) : (uint8_t)(w & 0xff);
		}
		if (addr >= 0x1000u && addr <= 0x1fffu && chip_) {
			const unsigned o = (unsigned)(addr - 0x1000u) & 0x1ffu;
			return CEmuChipC140Read(chip_, o);
		}
		if (addr >= 0x2000u && addr <= 0x2fffu && h8Shared_)
			return h8Shared_[(addr - 0x2000u) & 0xfffu];
		if (addr >= 0x3000u && addr <= 0xafffu && m37702LocalRam_)
			return m37702LocalRam_[addr - 0x3000u];
		if (addr >= 0x200000u && addr <= 0x27ffffu && h8Shared_) {
			const unsigned o = (unsigned)(addr - 0x200000u);
			if (o < 0x10000u) return h8Shared_[o];
			return 0;
		}
		/* Ports / ADC open-bus high. */
		return 0xff;
	}
	/* Sys11 C76 map. */
	if (h8Rom_ && addr >= 0x80000u && addr < 0x80000u + h8RomSize_)
		return h8Rom_[addr - 0x80000u];
	if (h8Rom_ && addr >= 0x200000u && addr < 0x200000u + h8RomSize_)
		return h8Rom_[addr - 0x200000u];
	if (h8Rom_ && addr >= 0x280000u && addr < 0x280000u + h8RomSize_)
		return h8Rom_[addr - 0x280000u];
	if (addr >= 0x4000u && addr <= 0xbfffu && h8Shared_)
		return h8Shared_[addr - 0x4000u];
	if (addr >= 0x2000u && addr <= 0x2fffu)
		return 0; /* C352 status */
	if (addr >= 0x510000u && addr <= 0x51ffffu)
		return 0x80; /* fambowl open-bus stub */
	return 0xff;
}

void CHardAc::M37702Write8(uint32_t addr, uint8_t v)
{
	addr &= 0xffffffu;
	if (m37702MapKind_ == 1) {
		if (addr >= 0x800u && addr <= 0xfffu) {
			const unsigned wi = ((unsigned)(addr - 0x800u) >> 1) & 7u;
			if (addr & 1u)
				m37702Mailbox_[wi] = (uint16_t)((m37702Mailbox_[wi] & 0x00ffu) | ((uint16_t)v << 8));
			else
				m37702Mailbox_[wi] = (uint16_t)((m37702Mailbox_[wi] & 0xff00u) | v);
			return;
		}
		if (addr >= 0x1000u && addr <= 0x1fffu && chip_) {
			const unsigned o = (unsigned)(addr - 0x1000u) & 0x1ffu;
			chip_->Write(o, v);
			h8C352Writes_++;
			return;
		}
		if (addr >= 0x2000u && addr <= 0x2fffu && h8Shared_) {
			h8Shared_[(addr - 0x2000u) & 0xfffu] = v;
			return;
		}
		if (addr >= 0x3000u && addr <= 0xafffu && m37702LocalRam_) {
			m37702LocalRam_[addr - 0x3000u] = v;
			return;
		}
		if (addr >= 0x200000u && addr <= 0x27ffffu && h8Shared_) {
			const unsigned o = (unsigned)(addr - 0x200000u);
			if (o < 0x10000u) h8Shared_[o] = v;
			return;
		}
		return;
	}
	/* Sys11 */
	if (addr >= 0x4000u && addr <= 0xbfffu && h8Shared_) {
		h8Shared_[addr - 0x4000u] = v;
		return;
	}
	if (addr >= 0x2000u && addr <= 0x2fffu && chip_) {
		const unsigned reg = (unsigned)((addr - 0x2000u) >> 1);
		uint16_t* shadow = &h8C352Shadow_[reg & 0x3ffu];
		if (!(addr & 1u)) {
			h8C352Hi_ = v;
			h8C352HiValid_ = 1;
			*shadow = (uint16_t)(((uint16_t)v << 8) | (*shadow & 0x00ffu));
			chip_->Write(reg, *shadow);
			h8C352Writes_++;
			return;
		}
		if (h8C352HiValid_) {
			*shadow = (uint16_t)(((uint16_t)h8C352Hi_ << 8) | v);
			h8C352HiValid_ = 0;
		} else {
			*shadow = (uint16_t)((*shadow & 0xff00u) | v);
		}
		chip_->Write(reg, *shadow);
		h8C352Writes_++;
	}
}

void CHardAc::SnkSetYmIrq(int which, int on)
{
	const uint8_t bit = (which == 0) ? 0x01u : 0x02u;
	if (on)
		snkStatus_ = (uint8_t)(snkStatus_ | bit);
	else
		snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)~bit);
}

void CHardAc::SoftPcmInjectSong(uint16_t cmd)
{
	/* Model2A/3 SCSP and Hornet RF5C400: latch only — no chip voice poking.
	   MixAdd/Render on those stubs stay silent until a real host is wired. */
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
}

void CHardAc::H8InjectSong(uint16_t cmd)
{
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
	if (!h8Shared_) return;
	if (h8MapKind_ == 0) {
		/* Sys12 C76: host song strobe is BE word at shared+0x0100 with bit14
		   set (0x40xx). H8 polls *0x080100, ACKs as 0x80xx, busy at +0x4050. */
		const uint16_t w = (uint16_t)(0x4000u | (cmd & 0x3fffu));
		h8Shared_[0x0100] = (uint8_t)(w >> 8);
		h8Shared_[0x0101] = (uint8_t)(w & 0xff);
		h8Shared_[0x4050] = 0;
	} else {
		/* ND-1: same C76-style mailbox as Sys12 — BE word at shared+0x0100
		   with bit14 set (ROM refs 00200100 + MOV.W #0x4000). Also poke the
		   low doorbells the MCU polls at +0x00/+0x04/+0x50. */
		const uint16_t w = (uint16_t)(0x4000u | (cmd & 0x3fffu));
		h8Shared_[0x0100] = (uint8_t)(w >> 8);
		h8Shared_[0x0101] = (uint8_t)(w & 0xff);
		h8Shared_[0x4050] = 0;
		const uint8_t hi = (uint8_t)(cmd >> 8);
		const uint8_t lo = (uint8_t)(cmd & 0xff);
		h8Shared_[0x0000] = hi;
		h8Shared_[0x0001] = lo;
		h8Shared_[0x0002] = hi;
		h8Shared_[0x0003] = lo;
		h8Shared_[0x0004] = 1;
		h8Shared_[0x0005] = lo;
		h8Shared_[0x0050] = 1;
		h8Shared_[0x0051] = lo;
	}
	/* System 12 only wires the H8's external IRQ1 to screen vblank.
	   Use that real vector to wake the mailbox poll after host injection. */
	if (h8_) H8SetInputLine(h8_, H8_LINE_IRQ1, H8_ASSERT_LINE);
}

int CHardAc::LoadRomsH8(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!h8_ || !fs || !ge) return 0;
	/* Only System 12 / ND-1 ship H8/3002 dumps. Sys11/22 use C74/C76
	   (M37702); NA/NB use C69/C70 (M37702) — do not attach H8 to those. */
	const int isNd1 = (_stricmp(ge->subtype, "nd1") == 0);
	const int isSys12 = (_stricmp(ge->subtype, "system12") == 0
		|| _stricmp(ge->subtype, "c352") == 0);
	if (!isNd1 && !isSys12) return 0;

	h8MapKind_ = isNd1 ? 1 : 0;
	h8WordSwap_ = isSys12 ? 1 : 0;
	h8C352Writes_ = 0;
	h8C352HiValid_ = 0;
	memset(h8C352Shadow_, 0, sizeof(h8C352Shadow_));

	if (h8Rom_) { free(h8Rom_); h8Rom_ = NULL; h8RomSize_ = 0; }
	if (!h8Shared_) {
		h8Shared_ = (uint8_t*)malloc(0x10000);
		if (!h8Shared_) return 0;
	}
	memset(h8Shared_, 0, 0x10000);

	/* Prefer catalog code/sub/audiocpu; else look for *11s* / *sub*. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sub") != 0
			&& _stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x1000u) continue;
		uint8_t* p = (uint8_t*)realloc(h8Rom_, sz);
		if (!p) continue;
		h8Rom_ = p;
		memcpy(h8Rom_, data, sz);
		h8RomSize_ = sz;
		break;
	}
	if (!h8RomSize_) {
		int best = -1, bestScore = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			if (sz < 0x1000u || sz > 0x100000u) continue;
			int sc = 0;
			if (CEmuAcContainsI(pathA, "11s")) sc += 200;
			if (CEmuAcContainsI(pathA, "sub")) sc += 150;
			if (CEmuAcContainsI(pathA, "sprog") || CEmuAcContainsI(pathA, "s-prog")) sc += 120;
			if (CEmuAcContainsI(pathA, "wave") || CEmuAcContainsI(pathA, "voice")
				|| CEmuAcContainsI(pathA, "wav"))
				sc -= 200;
			if (sz >= 0x10000u && sz <= 0x80000u) sc += 40;
			if (sc > bestScore) { bestScore = sc; best = i; }
		}
		if (best >= 0 && bestScore > 0) {
			const unsigned sz = fs->files[best].size;
			uint8_t* p = (uint8_t*)malloc(sz);
			if (p) {
				memcpy(p, fs->files[best].data, sz);
				h8Rom_ = p;
				h8RomSize_ = sz;
			}
		}
	}
	if (!h8RomSize_) return 0;

	if (h8WordSwap_) {
		for (unsigned i = 0; i + 1 < h8RomSize_; i += 2) {
			const uint8_t t = h8Rom_[i];
			h8Rom_[i] = h8Rom_[i + 1];
			h8Rom_[i + 1] = t;
		}
	}

	/* Wave / voice PCM into C352. */
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "pcm") != 0 && _stricmp(r->type, "voice") != 0
			&& _stricmp(r->type, "sample") != 0 && _stricmp(r->type, "adpcm") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		int off = r->offset;
		if (off < 0) off = 0;
		const unsigned need = (unsigned)off + sz;
		uint8_t* p = (uint8_t*)realloc(pcmRom_, need > pcmRomSize_ ? need : pcmRomSize_);
		if (!p) continue;
		if (need > pcmRomSize_) {
			memset(p + pcmRomSize_, 0, need - pcmRomSize_);
			pcmRomSize_ = need;
		}
		pcmRom_ = p;
		memcpy(pcmRom_ + (unsigned)off, data, sz);
	}
	if (!pcmRomSize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (CEmuAcContainsI(pathA, "11s") || CEmuAcContainsI(pathA, "sub"))
				continue;
			if (!(CEmuAcContainsI(pathA, "wave") || CEmuAcContainsI(pathA, "voice")
				|| CEmuAcContainsI(pathA, "wav") || CEmuAcContainsI(pathA, "c352")))
				continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, fs->files[i].size);
		}
	}
	if (chip_ && pcmRomSize_) chip_->SetPcmRom(pcmRom_, pcmRomSize_);

	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();

	CEmuH8BusSetAc(this);
	CEmuH8BusAttach(h8_, this);
	H8Reset(h8_);
	return 1;
}

int CHardAc::LoadRomsM37702(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* Sys11/22: external sprog + C76/C74 internal BIOS + C352.
	   NA1/NB1: C69/C70 internal BIOS @0xC000 + C219 + interleaved PCM. */
	if (!fs || !ge || !chip_ || !m37702_) return 0;

	if (h8Rom_) { free(h8Rom_); h8Rom_ = NULL; h8RomSize_ = 0; }
	if (m37702IntRom_) { free(m37702IntRom_); m37702IntRom_ = NULL; m37702IntRomSize_ = 0; }
	if (!h8Shared_) {
		h8Shared_ = (uint8_t*)malloc(0x10000);
		if (!h8Shared_) return 0;
	}
	memset(h8Shared_, 0, 0x10000);
	if (!m37702LocalRam_) {
		m37702LocalRam_ = (uint8_t*)malloc(0x8000);
		if (!m37702LocalRam_) return 0;
	}
	memset(m37702LocalRam_, 0, 0x8000);
	memset(m37702Mailbox_, 0, sizeof(m37702Mailbox_));
	h8MapKind_ = 2;
	h8C352Writes_ = 0;
	h8C352HiValid_ = 0;
	memset(h8C352Shadow_, 0, sizeof(h8C352Shadow_));

	/* Internal MCU BIOS: c69/c70/c74/c76.bin (16KB @ catalog offset 0xC000). */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x1000u) continue;
		const int isInt = (r->offset == 0xc000) || CEmuAcContainsI(r->name, "c69")
			|| CEmuAcContainsI(r->name, "c70") || CEmuAcContainsI(r->name, "c74")
			|| CEmuAcContainsI(r->name, "c75") || CEmuAcContainsI(r->name, "c76");
		if (!isInt) continue;
		uint8_t* p = (uint8_t*)malloc(sz);
		if (!p) continue;
		memcpy(p, data, sz);
		m37702IntRom_ = p;
		m37702IntRomSize_ = sz > 0x4000u ? 0x4000u : sz;
		break;
	}
	if (!m37702IntRomSize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (!(CEmuAcContainsI(pathA, "c69") || CEmuAcContainsI(pathA, "c70")
				|| CEmuAcContainsI(pathA, "c74") || CEmuAcContainsI(pathA, "c75")
				|| CEmuAcContainsI(pathA, "c76")))
				continue;
			const unsigned sz = fs->files[i].size;
			if (sz < 0x1000u || sz > 0x10000u) continue;
			uint8_t* p = (uint8_t*)malloc(sz);
			if (!p) continue;
			memcpy(p, fs->files[i].data, sz);
			m37702IntRom_ = p;
			m37702IntRomSize_ = sz > 0x4000u ? 0x4000u : sz;
			break;
		}
	}

	/* External program (Sys11/22 sprog) — skip 16KB internal BIOS dumps. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "bios") != 0
			&& _stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0
			&& _stricmp(r->type, "mcu") != 0 && _stricmp(r->type, "sub") != 0)
			continue;
		if (r->offset == 0xc000) continue;
		if (CEmuAcContainsI(r->name, "c69") || CEmuAcContainsI(r->name, "c70")
			|| CEmuAcContainsI(r->name, "c74") || CEmuAcContainsI(r->name, "c75")
			|| CEmuAcContainsI(r->name, "c76"))
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x1000u) continue;
		uint8_t* p = (uint8_t*)realloc(h8Rom_, sz);
		if (!p) continue;
		h8Rom_ = p;
		memcpy(h8Rom_, data, sz);
		h8RomSize_ = sz;
		break;
	}
	if (!h8RomSize_ && m37702MapKind_ != 1) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (CEmuAcContainsI(pathA, "wave") || CEmuAcContainsI(pathA, "wav"))
				continue;
			if (CEmuAcContainsI(pathA, "c69") || CEmuAcContainsI(pathA, "c76"))
				continue;
			const unsigned sz = fs->files[i].size;
			if (sz < 0x8000u || sz > 0x100000u) continue;
			uint8_t* p = (uint8_t*)malloc(sz);
			if (!p) continue;
			memcpy(p, fs->files[i].data, sz);
			h8Rom_ = p;
			h8RomSize_ = sz;
			break;
		}
	}
	/* NA1 needs internal BIOS; Sys11/22 need internal BIOS + external sprog. */
	if (!m37702IntRomSize_) return 0;
	if (m37702MapKind_ != 1 && !h8RomSize_) return 0;

	/* PCM: NA1 uses ROM_LOAD16_BYTE pairs (offset 0 / 1). */
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
	{
		const unsigned char* evenData = NULL;
		const unsigned char* oddData = NULL;
		unsigned evenSz = 0, oddSz = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "pcm") != 0 && _stricmp(r->type, "voice") != 0
				&& _stricmp(r->type, "sample") != 0 && _stricmp(r->type, "adpcm") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			if (r->offset == 0) { evenData = data; evenSz = sz; }
			else if (r->offset == 1) { oddData = data; oddSz = sz; }
			else {
				int off = r->offset;
				if (off < 0) off = 0;
				const unsigned need = (unsigned)off + sz;
				uint8_t* p = (uint8_t*)realloc(pcmRom_, need > pcmRomSize_ ? need : pcmRomSize_);
				if (!p) continue;
				if (need > pcmRomSize_) {
					memset(p + pcmRomSize_, 0, need - pcmRomSize_);
					pcmRomSize_ = need;
				}
				pcmRom_ = p;
				memcpy(pcmRom_ + (unsigned)off, data, sz);
			}
		}
		if (evenData && oddData && evenSz && oddSz) {
			const unsigned each = evenSz < oddSz ? evenSz : oddSz;
			const unsigned total = each * 2u;
			uint8_t* p = (uint8_t*)malloc(total);
			if (p) {
				for (unsigned i = 0; i < each; i++) {
					p[i * 2u + 0] = evenData[i];
					p[i * 2u + 1] = oddData[i];
				}
				free(pcmRom_);
				pcmRom_ = p;
				pcmRomSize_ = total;
			}
		}
	}
	if (!pcmRomSize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (!(CEmuAcContainsI(pathA, "wave") || CEmuAcContainsI(pathA, "wav")
				|| CEmuAcContainsI(pathA, "c352") || CEmuAcContainsI(pathA, "ep1")
				|| CEmuAcContainsI(pathA, "ep0")))
				continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, fs->files[i].size);
		}
	}
	if (chip_ && pcmRomSize_) chip_->SetPcmRom(pcmRom_, pcmRomSize_);

	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();

	CEmuM37702BusSetAc(this);
	CEmuM37702BusAttach(m37702_, this);
	M37702SetInternalRom(m37702_, m37702IntRom_, m37702IntRomSize_);
	M37702Reset(m37702_);
	m37702Soft_ = 0; /* real CPU attached */
	return 1;
}

int CHardAc::LoadRomsMs1(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!CEmuAcLoadMs1Code(&ms1Rom_, &ms1RomSize_, fs, ge))
		return 0;
	if (!ms1Ram_) {
		ms1Ram_ = (uint8_t*)malloc(0x10000);
		if (!ms1Ram_) return 0;
	}
	memset(ms1Ram_, 0, 0x10000);

	if (!CEmuAcLoadMs1Oki(&pcmRom_, &pcmRomSize_, fs, ge, "pcm1"))
		CEmuAcLoadMs1Oki(&pcmRom_, &pcmRomSize_, fs, ge, "pcm");
	CEmuAcLoadMs1Oki(&pcmRom2_, &pcmRom2Size_, fs, ge, "pcm2");
	if (!pcmRomSize_ || !pcmRom2Size_) {
		/* No pcm1/pcm2 typing: the two largest non-code members, name order. */
		int idx[8];
		int n = 0;
		for (int i = 0; i < fs->fileCount && n < (int)_countof(idx); i++) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x20000u) continue;
			idx[n++] = i;
		}
		for (int a = 0; a < n; a++) {
			for (int b = a + 1; b < n; b++) {
				char pa[CEMU_ZIP_PATH], pb[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[idx[a]].path, -1, pa, (int)sizeof(pa), NULL, NULL);
				WideCharToMultiByte(CP_ACP, 0, fs->files[idx[b]].path, -1, pb, (int)sizeof(pb), NULL, NULL);
				if (_stricmp(pa, pb) > 0) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
			}
		}
		if (!pcmRomSize_ && n >= 1)
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[idx[0]].data, fs->files[idx[0]].size);
		if (!pcmRom2Size_ && n >= 2)
			CEmuAcAppendPcm(&pcmRom2_, &pcmRom2Size_, fs->files[idx[1]].data, fs->files[idx[1]].size);
	}
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
	if (pcm2_ && pcmRom2Size_) pcm2_->SetPcmRom(pcmRom2_, pcmRom2Size_);

	/* soundlatch_w drives IRQ4 on System A/B, soundlatch_c_w drives IRQ6 on
	   System C. Pick the level whose autovector has its own handler; every
	   unused level shares one RTE stub in these ROMs. */
	ms1LatchLevel_ = 4;
	if (ms1RomSize_ >= 0x80) {
		const uint8_t* v = ms1Rom_;
		const unsigned v5 = ((unsigned)v[0x74] << 24) | ((unsigned)v[0x75] << 16) | ((unsigned)v[0x76] << 8) | v[0x77];
		const unsigned v6 = ((unsigned)v[0x78] << 24) | ((unsigned)v[0x79] << 16) | ((unsigned)v[0x7a] << 8) | v[0x7b];
		const unsigned v7 = ((unsigned)v[0x7c] << 24) | ((unsigned)v[0x7d] << 16) | ((unsigned)v[0x7e] << 8) | v[0x7f];
		if (v6 != v5 && v6 != v7) ms1LatchLevel_ = 6;
	}

	soundCmd_ = 0;
	soundCmdWord_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	ms1LatchIn_ = 0;
	ms1LatchOut_ = 0;
	ms1LatchIrq_ = 0;
	ms1OkiWrites_ = 0;
	ms1LatchReads_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;

	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();
	if (pcm2_) pcm2_->Reset();

	CEmuM68kBusSetMs1(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_set_int_ack_callback(NULL); /* autovector ? F3 installs its own */
	m68k_pulse_reset();
	return 1;
}

/* M92 sound ROMs are a ROM_LOAD16_BYTE pair into a 0x20000 region: the "sl"
   half is D7-D0 (even addresses) and the "sh" half is D15-D8 (odd). The
   catalog lists them in file order, so pick by name and only fall back to
   catalog order when neither carries the l/h marker.

   Do NOT rewrite 1FFF0 from IVT0. V35 reset executes FetchOp from FFFF0 with
   the decrypt table; newer Irem Software Guard builds store an encrypted
   JMP FAR (e.g. uccops F7��EA then plaintext 0040:03B0). Interpreting those
   five bytes as a raw CS:IP "outside ROM" and patching IVT0 over them
   destroys boot for every Rev 3.40+ set. Upper-half 0xFF padding is normal. */
static int CEmuAcLoadM92Code(uint8_t** dst, unsigned* dstSize,
	CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	const unsigned char* half[2] = { NULL, NULL };
	unsigned halfSize[2] = { 0, 0 };
	int n = 0;
	int typed = 0;

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "sub") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		const int isHigh = CEmuAcContainsI(r->name, "sh") || CEmuAcContainsI(r->name, "-h");
		const int isLow = CEmuAcContainsI(r->name, "sl") || CEmuAcContainsI(r->name, "-l");
		if (isHigh && !isLow) { half[1] = data; halfSize[1] = sz; typed = 1; }
		else if (isLow && !isHigh) { half[0] = data; halfSize[0] = sz; typed = 1; }
		else if (n < 2) { half[n] = data; halfSize[n] = sz; n++; }
	}
	if (!typed && n < 2)
		return 0;
	if (typed && (!half[0] || !half[1]))
		return 0;

	const unsigned each = halfSize[0] < halfSize[1] ? halfSize[0] : halfSize[1];
	if (!each) return 0;
	const unsigned total = 0x20000u;
	uint8_t* p = (uint8_t*)malloc(total);
	if (!p) return 0;
	memset(p, 0xff, total);
	for (unsigned i = 0; i < each && i * 2 + 1 < total; i++) {
		p[i * 2 + 0] = half[0][i];
		p[i * 2 + 1] = half[1][i];
	}
	/* Many M92 dumps only populate the first 32KB of each 64KB EPROM, so the
	   interleaved upper half (0x10000-0x1FFFF) is 0xFF padding aside from the
	   5-byte reset JMP. Rev 3.40+ song tables often point into that half
	   (uccopsj song1 @0x120A4). Mirror the low 64KB up when the upper half is
	   empty so those pointers land on real data ? keep the reset vector. */
	{
		unsigned upperUsed = 0;
		for (unsigned i = 0x10000u; i < 0x1fff0u; i++) {
			if (p[i] != 0xffu) {
				upperUsed++;
				if (upperUsed > 16u) break;
			}
		}
		if (upperUsed <= 16u) {
			uint8_t resetVec[16];
			memcpy(resetVec, p + 0x1fff0u, 16);
			memcpy(p + 0x10000u, p, 0x10000u);
			memcpy(p + 0x1fff0u, resetVec, 16);
		}
	}
	*dst = p;
	*dstSize = total;
	return 1;
}

int CHardAc::LoadRomsM92(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!v35_) return 0;
	if (!CEmuAcLoadM92Code(&m92Rom_, &m92RomSize_, fs, ge))
		return 0;
	if (!m92Ram_) {
		m92Ram_ = (uint8_t*)malloc(0x4000);
		if (!m92Ram_) return 0;
	}
	memset(m92Ram_, 0, 0x4000);

	/* GA20 samples: the generic pcm scan already handles bm_da / gf-da. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		if (CEmuAcPcmRomScore(r->name, r->type, sz) <= 0) continue;
		CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, data, sz);
	}
	if (!pcmRomSize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (CEmuAcPcmRomScore(pathA, "", fs->files[i].size) <= 0) continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, fs->files[i].size);
		}
	}
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);

	soundCmd_ = 0;
	soundCmdWord_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	m92Latch_ = 0;
	m92LatchPending_ = 0;
	m92Latch2_ = 0;
	m92LatchReads_ = 0;
	m92Ga20Writes_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;

	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();

	CEmuV35BusSetM92(this);
	CEmuV35BusAttach(v35_, this);
	{
		const uint8_t* tab = CEmuIremCpuDecryptionTable(ge->archive);
		m92DecryptValid_ = 0;
		m92BomberGatePatch_ = 0;
		m92EncryptedRet_ = 0x14;
		m92SongCmdBase_ = 0;
		m92ChannelBgm_ = 0;
		m92WordQueue_ = 0;
		m92ReadyWait_ = 0;
		if (tab) {
			/* Keep a private copy; FINT's 2nd byte is plaintext (Fetch8 in
			   ExecV25Group) so do NOT force table[0x92]=0x92 ? that broke
			   gunforce MOV SI (table[0x92]=0xBE) used by RAM init. */
			memcpy(m92Decrypt_, tab, 256);
			m92DecryptValid_ = 1;
			tab = m92Decrypt_;
			V35SetDecryptionTable(v35_, tab);
			if (m92Rom_ && m92RomSize_ >= 0x20000u) {
				for (unsigned i = 0; i < 256u; i++) {
					if (tab[i] == 0xC3u) {
						m92EncryptedRet_ = (uint8_t)i;
						break;
					}
				}
				/* Rev 3.40+ boots with encrypted JMP FAR at FFFF0 (EA); early
				   sets start with CLI (FA) then JMP FAR. Only channel-BGM
				   sequencers use latch 0x20+index — note-list sets (uccops)
				   keep catalog codes raw even when reset is EA. */
				if (tab[m92Rom_[0x1fff0u]] == 0xEAu) {
					for (unsigned off = 0x700u; off + 8u < 0x900u && off < m92RomSize_; off++) {
						if (tab[m92Rom_[off]] != 0x0Fu) continue;
						if (m92Rom_[off + 1u] != 0x92u) continue; /* FINT */
						unsigned stiAt = 0;
						for (unsigned k = off + 2u; k < off + 14u && k < m92RomSize_; k++) {
							if (tab[m92Rom_[k]] == 0xFBu) {
								stiAt = k;
								break;
							}
							/* INTP1 is FINT;IRET — no STI. */
							if (tab[m92Rom_[k]] == 0xCFu)
								break;
						}
						if (!stiAt) continue;
						/* CALL after STI → MOV BP,#0000 means channel-BGM. */
						for (unsigned j = stiAt; j + 3u < stiAt + 24u && j + 3u < m92RomSize_; j++) {
							if (tab[m92Rom_[j]] != 0xE8u) continue;
							int rel = (int)(m92Rom_[j + 1u] | ((unsigned)m92Rom_[j + 2u] << 8));
							if (rel >= 0x8000) rel -= 0x10000;
							const unsigned tgt = (unsigned)((int)j + 3 + rel);
							if (tgt + 3u < m92RomSize_
								&& tab[m92Rom_[tgt]] == 0xBDu
								&& m92Rom_[tgt + 1u] == 0x00u
								&& m92Rom_[tgt + 2u] == 0x00u)
								m92ChannelBgm_ = 1;
							break;
						}
						break;
					}
					if (m92ChannelBgm_)
						m92SongCmdBase_ = 0x20;
				}
				/* Newer IMC builds replace the 08C0 byte ring with a 16-slot
				   word ring at 0AF0 (enqueue SHL BX,1 / MOV [BX+0AF0],AX). */
				{
					unsigned af0 = 0, c31 = 0, wait3 = 0;
					for (unsigned i = 0x400u; i + 2u < 0x900u && i + 2u < m92RomSize_; i++) {
						const unsigned w = m92Rom_[i] | ((unsigned)m92Rom_[i + 1u] << 8);
						if (w == 0x0AF0u) af0++;
						if (w == 0x0C31u) {
							c31++;
							if (i + 2u < m92RomSize_ && m92Rom_[i + 2u] == 0x03u)
								wait3++;
						}
					}
					if (af0 >= 2u)
						m92WordQueue_ = 1;
					if (wait3 > 0 && c31 > 0)
						m92ReadyWait_ = 1;
				}
			}
		} else {
			V35SetDecryptionTable(v35_, NULL);
		}
	}
	V35Reset(v35_);
	return 1;
}

int CHardAc::LoadRomsGx(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* Same interleaved 16-bit code load as MS1; drivers.xml: offset 0=even,
	   0x20000=odd �� first catalog code member is D15-D8. */
	if (!CEmuAcLoadMs1Code(&ms1Rom_, &ms1RomSize_, fs, ge))
		return 0;
	if (!ms1Ram_) {
		ms1Ram_ = (uint8_t*)malloc(0x10000);
		if (!ms1Ram_) return 0;
	}
	memset(ms1Ram_, 0, 0x10000);

	/* Dual K054539 share one PCM region (MAME "shared"); concatenate catalog
	   pcm members by offset (tkmmpzdm: 2 MiB + 2 MiB @ 0x200000). */
	CEmuAcLoadMs1Oki(&pcmRom_, &pcmRomSize_, fs, ge, "pcm");
	if (!pcmRomSize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x40000u) continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, sz);
		}
	}
	if (chip_ && pcmRomSize_) chip_->SetPcmRom(pcmRom_, pcmRomSize_);
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);

	soundCmd_ = 0;
	soundCmdWord_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	memset(k056800Host_, 0, sizeof(k056800Host_));
	memset(k056800Snd_, 0, sizeof(k056800Snd_));
	k056800IntEn_ = 0;
	k056800Pending_ = 0;
	k056800Irq_ = 0;
	gxSoundCtrl_ = 0;
	gxSoundIntck_ = 0;
	gxPcmWrites_ = 0;
	gxTmsStatus_ = 0x07; /* dready|pc0|empty ? keep firmware out of TMS waits */
	opmWrites_ = 0;
	cpuCycles_ = 0;
	ms1LatchLevel_ = 1;
	ms1LatchIrq_ = 0;

	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();

	CEmuM68kBusSetMs1(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_set_int_ack_callback(NULL);
	m68k_pulse_reset();
	return 1;
}

int CHardAc::LoadRomsPcmChip(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* SCSP / RF5C400 / Seibu: host CPU not emulated — load wave ROM (+ optional
	   OKI for Seibu) for a future host; chips stay silent without audition. */
	if (!fs || !ge || !chip_) return 0;
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!r->type || !r->type[0]) continue;
		const int isPcm = (_stricmp(r->type, "pcm") == 0
			|| _strnicmp(r->type, "pcm", 3) == 0
			|| _stricmp(r->type, "voice") == 0
			|| _stricmp(r->type, "oki") == 0);
		if (!isPcm) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		int off = r->offset;
		if (off < 0) off = 0;
		const unsigned need = (unsigned)off + sz;
		uint8_t* p = (uint8_t*)realloc(pcmRom_, need > pcmRomSize_ ? need : pcmRomSize_);
		if (!p) return 0;
		if (need > pcmRomSize_) {
			if (pcmRomSize_)
				memset(p + pcmRomSize_, 0, need - pcmRomSize_);
			else
				memset(p, 0, need);
			pcmRomSize_ = need;
		}
		pcmRom_ = p;
		memcpy(pcmRom_ + (unsigned)off, data, sz);
	}
	if (!pcmRomSize_) {
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x8000u) continue;
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA,
				(int)sizeof(pathA), NULL, NULL);
			if (CEmuAcPcmRomScore(pathA, "", sz) <= 0 && sz < 0x40000u)
				continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, sz);
			if (pcmRomSize_ >= 0x100000u) break;
		}
	}
	if (chip_ && pcmRomSize_) chip_->SetPcmRom(pcmRom_, pcmRomSize_);
	if (pcm_ && pcmRomSize_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);

	/* Seibu: also map a Z80 image if present (encrypted — not stepped). */
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		memset(mem_, 0, sizeof(mem_));
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!CEmuAcIsCodeRomType(r->type)) continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			unsigned n = sz < 0x10000u ? sz : 0x10000u;
			memcpy(mem_, data, n);
			break;
		}
	}

	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	opmWrites_ = 0;
	cpuCycles_ = 0;
	if (chip_) chip_->Reset();
	if (pcm_) pcm_->Reset();
	/* Soft-open Model2 packs that ship incomplete wave dumps. */
	return (pcmRomSize_ > 0 || board_ == CEMU_AC_BOARD_SEGA_SCSP
		|| board_ == CEMU_AC_BOARD_KONAMI_RF5C400) ? 1 : 0;
}

int CHardAc::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge) return 0;
	if (board_ != CEMU_AC_BOARD_MEGASYSTEM1 && board_ != CEMU_AC_BOARD_KONAMI_GX && !cpu_)
		return 0;
	memset(mem_, 0, sizeof(mem_));
	if (pcmRom_) {
		free(pcmRom_);
		pcmRom_ = NULL;
		pcmRomSize_ = 0;
	}
	if (pcmRom2_) {
		free(pcmRom2_);
		pcmRom2_ = NULL;
		pcmRom2Size_ = 0;
	}
	if (soundRom_) {
		free(soundRom_);
		soundRom_ = NULL;
		soundRomSize_ = 0;
	}
	if (qsKabukiData_) {
		free(qsKabukiData_);
		qsKabukiData_ = NULL;
	}
	qsKabuki_ = 0;
	if (ms1Rom_) {
		free(ms1Rom_);
		ms1Rom_ = NULL;
		ms1RomSize_ = 0;
	}
	if (m92Rom_) {
		free(m92Rom_);
		m92Rom_ = NULL;
		m92RomSize_ = 0;
	}
	int loaded = 0;
	const unsigned char* codeRom = NULL;
	unsigned codeRomSize = 0;

	if (board_ == CEMU_AC_BOARD_MEGASYSTEM1)
		return LoadRomsMs1(fs, ge);
	if (board_ == CEMU_AC_BOARD_KONAMI_GX)
		return LoadRomsGx(fs, ge);
	if (board_ == CEMU_AC_BOARD_IREM_M92)
		return LoadRomsM92(fs, ge);
	if (board_ == CEMU_AC_BOARD_DECO)
		return LoadRomsDeco(fs, ge);
	if (board_ == CEMU_AC_BOARD_ATARI_SYS1)
		return LoadRomsAtariSys1(fs, ge);
	if (board_ == CEMU_AC_BOARD_NAMCO_C352) {
		if (m37702_)
			return LoadRomsM37702(fs, ge);
		return LoadRomsH8(fs, ge);
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 || board_ == CEMU_AC_BOARD_NAMCO_SYS2)
		return LoadRomsNamcoM6809(fs, ge);
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsg63701_)
		return LoadRomsWsg63701(fs, ge);
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_)
		return LoadRomsNamcoM6809(fs, ge);
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS86)
		return LoadRomsSys86(fs, ge);
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL)
		return LoadRomsSeibu(fs, ge);
	if (board_ == CEMU_AC_BOARD_IREM_M62)
		return LoadRomsM62(fs, ge);
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return LoadRomsSegaM1(fs, ge);
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP
		|| board_ == CEMU_AC_BOARD_KONAMI_RF5C400)
		return LoadRomsPcmChip(fs, ge);

	unsigned char* m72Code = (board_ == CEMU_AC_BOARD_IREM_M72)
		? CEmuAcM72InterleavedCode(fs, ge) : NULL;
	if (m72Code) {
		memcpy(mem_, m72Code, 0x10000);
		codeRom = m72Code;
		codeRomSize = 0x10000;
		loaded = 1;
		m72SoundRam_ = 1;
	} else if (board_ == CEMU_AC_BOARD_IREM_M72) {
		/* Dedicated sound ROM �� ROM map; sniff YM@40 (poundfor/m99/bbmanw). */
		m72SoundRam_ = (_stricmp(ge->subtype, "rtype") == 0) ? 1 : 0;
	}

	/* CPS2 QSound: keep the full audiocpu image in soundRom_ (sfz.01+sfz.02).
	   Only 0000-7FFF is fixed in Z80 space; 8000-BFFF is banked from +0x8000.
	   Do not blit the whole file into mem_ ? that would trash work RAM at F000. */
	if (board_ == CEMU_AC_BOARD_CPS_QS) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!CEmuAcIsCodeRomType(r->type))
				continue;
			int off = r->offset;
			if (off < 0) off = 0;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			const unsigned need = (unsigned)off + sz;
			uint8_t* p = (uint8_t*)realloc(soundRom_, need > soundRomSize_ ? need : soundRomSize_);
			if (!p) continue;
			if (need > soundRomSize_) {
				memset(p + soundRomSize_, 0xff, need - soundRomSize_);
				soundRomSize_ = need;
			}
			soundRom_ = p;
			memcpy(soundRom_ + (unsigned)off, data, sz);
			if ((unsigned)off == 0 && sz > codeRomSize) {
				codeRom = data;
				codeRomSize = sz;
			}
			loaded++;
		}
		if (!loaded) {
			/* Fallback: largest ?256K members that look like Z80 code, in name order. */
			int idxs[8];
			int nIdx = 0;
			for (int i = 0; i < fs->fileCount && nIdx < (int)_countof(idxs); i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				const unsigned sz = fs->files[i].size;
				if (sz < 0x4000u || sz > 0x40000u) continue;
				if (CEmuAcSoundRomScore(pathA, sz, fs->files[i].data) < 0) continue;
				idxs[nIdx++] = i;
			}
			for (int a = 0; a < nIdx; a++) {
				for (int b = a + 1; b < nIdx; b++) {
					char pa[CEMU_ZIP_PATH], pb[CEMU_ZIP_PATH];
					WideCharToMultiByte(CP_ACP, 0, fs->files[idxs[a]].path, -1, pa, (int)sizeof(pa), NULL, NULL);
					WideCharToMultiByte(CP_ACP, 0, fs->files[idxs[b]].path, -1, pb, (int)sizeof(pb), NULL, NULL);
					if (_stricmp(pa, pb) > 0) {
						int t = idxs[a]; idxs[a] = idxs[b]; idxs[b] = t;
					}
				}
			}
			unsigned off = 0;
			for (int k = 0; k < nIdx; k++) {
				const unsigned sz = fs->files[idxs[k]].size;
				const unsigned char* data = fs->files[idxs[k]].data;
				const unsigned need = off + sz;
				uint8_t* p = (uint8_t*)realloc(soundRom_, need > soundRomSize_ ? need : soundRomSize_);
				if (!p) break;
				if (need > soundRomSize_) {
					memset(p + soundRomSize_, 0xff, need - soundRomSize_);
					soundRomSize_ = need;
				}
				soundRom_ = p;
				memcpy(soundRom_ + off, data, sz);
				if (off == 0) {
					codeRom = data;
					codeRomSize = sz;
				}
				off += sz;
				loaded++;
			}
		}
		if (soundRom_ && soundRomSize_ >= 0x8000u)
			memcpy(mem_, soundRom_, 0x8000);
		/* CPS1 QSound Kabuki (dino/wof/punisher/slammast): dual opcode/data
		   decrypt for the fixed 0000-7FFF window. Banked 8000+ stays plain. */
		qsKabuki_ = 0;
		if (qsKabukiData_) { free(qsKabukiData_); qsKabukiData_ = NULL; }
		{
			CEmuKabukiKey kk;
			if (ge && ge->archive && soundRom_ && soundRomSize_ >= 0x8000u
				&& CEmuKabukiLookup(ge->archive, &kk)) {
				uint8_t* dataPlane = (uint8_t*)malloc(0x8000u);
				uint8_t* opPlane = (uint8_t*)malloc(0x8000u);
				if (dataPlane && opPlane) {
					CEmuKabukiDecode(soundRom_, opPlane, dataPlane, 0, 0x8000,
						kk.swapKey1, kk.swapKey2, kk.addrKey, kk.xorKey);
					memcpy(soundRom_, opPlane, 0x8000u);
					memcpy(mem_, opPlane, 0x8000u);
					qsKabukiData_ = dataPlane;
					qsKabuki_ = 1;
					free(opPlane);
				} else {
					free(dataPlane);
					free(opPlane);
				}
			}
		}
		/* Capcom QSound: catalog titles are a single byte in packet[0], but the
		   Z80 reads a 16-bit big-endian code from (IY+0)/(IY+1) �� code<<8.
		   Two driver families need an 8-bit index rewrite:

		   Classic (1.04?1.06b): count at (9000), table at 9006 ? pattern scanned
		   in 0100..0800 (address moves each revision; a hard-coded 0206 patch
		   once corrupted 1.04 and silenced ssf2t).

		   Later (batcir/ddsom/1944/�c): count at (F010), table ptr at (F013) ?
		   same IY 16-bit load, then DE=(F010) modulo. Without the rewrite,
		   HL=code<<8 folds onto the wrong (often empty) entry �� SILENT while
		   the idle loop still ticks (pc~0180). */
		if (soundRom_ && soundRomSize_ >= 0x8000u) {
			static const uint8_t kQsLookup[] = {
				0x21, 0x00, 0x90, /* LD HL,9000   */
				0x56, 0x23, 0x5e, /* LD D,(HL) / INC HL / LD E,(HL) */
				0xfd, 0x66, 0x00, /* LD H,(IY+0)  */
				0xfd, 0x6e, 0x01, /* LD L,(IY+1)  */
				0xb7, 0xed, 0x52, /* OR A / SBC HL,DE */
				0x30, 0xfb,       /* JR NC,-5     */
				0x19,             /* ADD HL,DE    */
				0x29, 0x29,       /* ADD HL,HL x2 */
				0x11, 0x06, 0x90, /* LD DE,9006   */
			};
			static const uint8_t kQsIdx[] = {
				0xfd, 0x6e, 0x00, /* LD L,(IY+0) */
				0x26, 0x00,       /* LD H,0 */
				0x29,             /* ADD HL,HL */
				0x29,             /* ADD HL,HL */
				0x18, 0x0b,       /* JR +11 �� LD DE,9006 */
			};
			/* LD H,(IY+0) / LD L,(IY+1) / LD DE,(F010) ? later QSound engines. */
			static const uint8_t kQsLookupF010[] = {
				0xfd, 0x66, 0x00,
				0xfd, 0x6e, 0x01,
				0xed, 0x5b, 0x10, 0xf0,
			};
			static const uint8_t kQsIdxF010[] = {
				0xfd, 0x6e, 0x00, /* LD L,(IY+0) */
				0x26, 0x00,       /* LD H,0 */
				0x00,             /* NOP pads the removed 3rd IY opcode byte */
				0xed, 0x5b, 0x10, 0xf0,
			};
			/* Capcom ZN keeps real 16-bit song words via latch+NMI ? do not
			   collapse to an 8-bit index (that silenced ts2). */
			if (!qsZn_) {
				int patched = 0;
				for (unsigned a = 0x0100u; a + sizeof(kQsLookup) <= 0x0800u; a++) {
					if (memcmp(soundRom_ + a, kQsLookup, sizeof(kQsLookup)) != 0)
						continue;
					memcpy(soundRom_ + a, kQsIdx, sizeof(kQsIdx));
					memcpy(mem_ + a, kQsIdx, sizeof(kQsIdx));
					patched = 1;
					break;
				}
				if (!patched) {
					for (unsigned a = 0x0100u; a + sizeof(kQsLookupF010) <= 0x0a00u; a++) {
						if (memcmp(soundRom_ + a, kQsLookupF010, sizeof(kQsLookupF010)) != 0)
							continue;
						memcpy(soundRom_ + a, kQsIdxF010, sizeof(kQsIdxF010));
						if (a + sizeof(kQsIdxF010) <= 0x10000u)
							memcpy(mem_ + a, kQsIdxF010, sizeof(kQsIdxF010));
						break;
					}
				}
			}
		}
	} else if (!m72Code) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!CEmuAcIsCodeRomType(r->type))
			continue;
		int off = r->offset;
		if (off < 0) off = 0;
		/* Catalog sometimes parks sound CPU images at bank offsets ?64K
		   (Model 2 SCSP, split 68000). Map into Z80 space from 0. */
		int mapOff = off;
		if (mapOff >= 0x10000) mapOff = 0;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		/* Sys16: skip uPD7751 MCU listed as type=sub @0000. */
		if ((board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
			&& CEmuAcIsSys16SpeechMcu(r->name, sz))
			continue;
		unsigned n = sz;
		if (mapOff + (int)n > 0x10000)
			n = (unsigned)(0x10000 - mapOff);
		memcpy(mem_ + mapOff, data, n);
		if (mapOff == 0 && sz > codeRomSize) {
			codeRom = data;
			codeRomSize = sz;
		}
		loaded++;
	}
	}

	/* Fallback: best sound-CPU member into 0000 (local zips with empty/mismatched romlist).
	   For dual 32K Sys16 dumps, concatenate into 64K ? not sharrier PCM names (7231/7232). */
	if (!loaded && board_ != CEMU_AC_BOARD_CPS_QS) {
		int best = -1;
		int bestScore = -1000;
		int second = -1;
		int secondScore = -1000;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			unsigned sz = fs->files[i].size;
			const unsigned char* data = fs->files[i].data;
			int sc = CEmuAcSoundRomScore(pathA, sz, data);
			if (sc > bestScore) {
				secondScore = bestScore;
				second = best;
				bestScore = sc;
				best = i;
			} else if (sc > secondScore) {
				secondScore = sc;
				second = i;
			}
		}
		/* Accept weak-but-plausible scores (128K Model2 code dumps score ~-40). */
		if (best >= 0 && bestScore >= -80) {
			unsigned sz = fs->files[best].size;
			const unsigned char* data = fs->files[best].data;
			unsigned n = sz > 0x10000 ? 0x10000u : sz;
			memcpy(mem_, data, n);
			codeRom = data;
			codeRomSize = sz;
			loaded++;
			/* Append second 32K bank only when first is ?32K (Sys16 dual Z80 dumps).
			   Skip sharrier/hangon PCM filenames (7231/7232) and 64K singles. */
			if (second >= 0 && secondScore >= 30 && n <= 0x8000u
				&& secondScore + 20 >= bestScore
				&& board_ != CEMU_AC_BOARD_HANGON) {
				unsigned n2 = fs->files[second].size;
				if (n2 > 0x8000) n2 = 0x8000u;
				if (n2 > 0)
					memcpy(mem_ + 0x8000, fs->files[second].data, n2);
			}
		}
	}

	CChip* pcmTarget = CEmuAcPrimaryPcmTarget(this);
	if (pcmTarget) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			if (CEmuAcPcmRomScore(r->name, r->type, sz) <= 0) continue;
			CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, data, sz);
		}
		if (!pcmRomSize_) {
			/* Sort PCM candidates by name so bank order is stable across
			   data\\ac vs data\\roms naming (mpr-10930 before mpr-10931). */
			int idxs[64];
			int nIdx = 0;
			for (int i = 0; i < fs->fileCount && nIdx < (int)_countof(idxs); i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				if (CEmuAcPcmRomScore(pathA, "", fs->files[i].size) <= 0) continue;
				idxs[nIdx++] = i;
			}
			for (int a = 0; a < nIdx; a++) {
				for (int b = a + 1; b < nIdx; b++) {
					char pa[CEMU_ZIP_PATH], pb[CEMU_ZIP_PATH];
					WideCharToMultiByte(CP_ACP, 0, fs->files[idxs[a]].path, -1, pa, (int)sizeof(pa), NULL, NULL);
					WideCharToMultiByte(CP_ACP, 0, fs->files[idxs[b]].path, -1, pb, (int)sizeof(pb), NULL, NULL);
					if (_stricmp(pa, pb) > 0) {
						int t = idxs[a]; idxs[a] = idxs[b]; idxs[b] = t;
					}
				}
			}
			for (int k = 0; k < nIdx; k++) {
				int i = idxs[k];
				CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, fs->files[i].size);
			}
		}
		if (pcmRomSize_)
			pcmTarget->SetPcmRom(pcmRom_, pcmRomSize_);
		/* CPS2: force multi-meg sample banks (sfx.11 / sz3.11). */
		if (board_ == CEMU_AC_BOARD_CPS_QS && pcmRomSize_ < 0x100000u) {
			if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
			for (int i = 0; i < fs->fileCount; i++) {
				if (fs->files[i].size < 0x100000u) continue;
				CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, fs->files[i].data, fs->files[i].size);
			}
			if (pcmRomSize_)
				pcmTarget->SetPcmRom(pcmRom_, pcmRomSize_);
		}
	}

	/* YM2610 ADPCM-A/B (Taito F2/H/B and V-System aerofgt family). Falls back to
	   size/name heuristics for zips whose romlist lacks adpcm entries. */
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_VSYSTEM) {
		if (chip_ && !CEmuAcLoadYm2610Adpcm(chip_, fs, ge)) {
			unsigned aOff = 0;
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				const unsigned sz = fs->files[i].size;
				if (sz < 0x20000u) continue;
				if (fs->files[i].data == codeRom) continue;
				if (CEmuAcContainsI(pathA, "adpcm") || CEmuAcContainsI(pathA, "pcm")
					|| CEmuAcContainsI(pathA, "voi") || sz >= 0x80000u) {
					chip_->SetAdpcmRom(fs->files[i].data, sz, aOff);
					aOff += sz;
					if (aOff >= 0x400000u) break;
				}
			}
		}
	}

	/* Banked sound ROM (Taito F200 / CT1-CT2 latch, aerofgt port 04,
	   Konami K054539 16K window @8000). */
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_TAITO_OPM
		|| board_ == CEMU_AC_BOARD_VSYSTEM
		|| (board_ == CEMU_AC_BOARD_KONAMI_PCM && konamiBankAddr_)) {
		if (codeRom && codeRomSize) {
			soundRom_ = (unsigned char*)malloc(codeRomSize);
			if (soundRom_) {
				memcpy(soundRom_, codeRom, codeRomSize);
				soundRomSize_ = codeRomSize;
			}
		}
	}
	/* Sys16B: MAME packs Z80 program at 0000 and bank/UPD7759 data at 10000+.
	   Catalog often tags the bank ROMs as "adpcm" ? still load them into
	   soundRom_ so port 40 can page song tables into 8000-DFFF. */
	if (board_ == CEMU_AC_BOARD_SYS32 && codeRom && codeRomSize) {
		soundRom_ = (unsigned char*)malloc(codeRomSize);
		if (soundRom_) {
			memcpy(soundRom_, codeRom, codeRomSize);
			soundRomSize_ = codeRomSize;
		}
	}
	if ((board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
		&& codeRom && codeRomSize) {
		/* Prefer catalog boardtype for Sys16B bank bit mapping. */
		if (board_ == CEMU_AC_BOARD_SYS16B)
			sys16RomBoard_ = CEmuAcOptionValue(ge, "boardtype", 0x5797u);
		else
			sys16RomBoard_ = 0x5358u;
		unsigned need = 0x10000u;
		struct { const unsigned char* data; unsigned sz; unsigned off; } banks[8];
		int nBank = 0;
		for (int i = 0; i < ge->romCount && nBank < 8; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz || data == codeRom) continue;
			if (CEmuAcIsSys16SpeechMcu(r->name, sz)) continue;
			if (_stricmp(r->type, "adpcm") != 0 && _stricmp(r->type, "code") != 0
				&& _stricmp(r->type, "sub") != 0)
				continue;
			if (sz < 0x4000u || sz > 0x40000u) continue;
			/* Catalog offsets are either bank-relative (aceattac 0/10000/20000)
			   or absolute soundcpu addresses (goldnaxe 0x10000). Mixing the
			   "subtract 0x10000" heuristic collapsed relative 0 and 0x10000 onto
			   the same slot and silenced 5358 multi-bank titles. */
			unsigned boff = (r->offset > 0) ? (unsigned)r->offset : 0u;
			banks[nBank].data = data;
			banks[nBank].sz = sz;
			banks[nBank].off = boff;
			nBank++;
		}
		/* Resolve destinations: if any bank starts at 0, treat all as relative
		   to the 0x10000 bank region; otherwise use absolute soundcpu offsets. */
		{
			int relative = 0;
			for (int b = 0; b < nBank; b++) {
				if (banks[b].off < 0x10000u) { relative = 1; break; }
			}
			for (int b = 0; b < nBank; b++) {
				unsigned dst = relative ? (0x10000u + banks[b].off) : banks[b].off;
				banks[b].off = (dst >= 0x10000u) ? (dst - 0x10000u) : dst;
				const unsigned end = 0x10000u + banks[b].off + banks[b].sz;
				if (end > need) need = end;
			}
		}
		/* Also pull a second equal-sized Z80 dump (shinobi epr-11268). */
		if (nBank == 0) {
			for (int i = 0; i < fs->fileCount && nBank < 8; i++) {
				if (fs->files[i].data == codeRom) continue;
				const unsigned sz = fs->files[i].size;
				if (sz != 0x8000u && sz != 0x10000u && sz != 0x20000u) continue;
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				if (CEmuAcIsSys16SpeechMcu(pathA, sz)) continue;
				if (CEmuAcSoundRomScore(pathA, sz, fs->files[i].data) < 30) continue;
				banks[nBank].data = fs->files[i].data;
				banks[nBank].sz = sz;
				banks[nBank].off = (unsigned)nBank * sz;
				need = 0x10000u + banks[nBank].off + sz;
				nBank++;
			}
		}
		if (nBank > 0 || codeRomSize > 0x8000u) {
			if (need < codeRomSize) need = codeRomSize;
			if (need < 0x10000u) need = 0x10000u;
			uint8_t* p = (uint8_t*)malloc(need);
			if (p) {
				memset(p, 0xff, need);
				unsigned n0 = codeRomSize > 0x8000u ? 0x8000u : codeRomSize;
				memcpy(p, codeRom, n0);
				if (codeRomSize > 0x8000u && codeRomSize <= need)
					memcpy(p + 0x8000, codeRom + 0x8000, codeRomSize - 0x8000u);
				for (int b = 0; b < nBank; b++) {
					unsigned dst = 0x10000u + banks[b].off;
					unsigned n = banks[b].sz;
					if (dst >= need) continue;
					if (dst + n > need) n = need - dst;
					if (n) memcpy(p + dst, banks[b].data, n);
				}
				if (soundRom_) free(soundRom_);
				soundRom_ = p;
				soundRomSize_ = need;
				/* Fixed ROM 0000-7FFF must match soundRom_ (catalog may have
				   painted 7751 over mem_ earlier; SetBank is a no-op here). */
				{
					unsigned n = 0x8000u;
					if (n > soundRomSize_) n = soundRomSize_;
					memcpy(mem_, soundRom_, n);
				}
				/* Prime bank window from first bank ROM. */
				if (need > 0x10000u) {
					unsigned n = 0x6000u;
					if (0x10000u + n > need) n = need - 0x10000u;
					memset(mem_ + 0x8000, 0xff, 0x6000);
					if (n) memcpy(mem_ + 0x8000, soundRom_ + 0x10000u, n);
				}
			}
		} else if (codeRomSize > 0) {
			/* Single 32K program (afighter): still keep mem_ = code. */
			unsigned n = codeRomSize > 0x8000u ? 0x8000u : codeRomSize;
			memcpy(mem_, codeRom, n);
		}
	}
	/* aerofgt sound_map: only 0000-77FF is fixed ROM and 7800-7FFF is RAM ?
	   the generic blit above put ROM bytes at 8000-FFFF, which pinned the Z80
	   to bank 1 and left the real bank window unreachable.
	   fromanc2 keeps a flat 0000-DFFF ROM image (no 32K bank window). */
	if (board_ == CEMU_AC_BOARD_VSYSTEM && soundRom_) {
		/* Sniff ISR port if subtype left vsIoKind_ at default. */
		if (vsIoKind_ == 0 && soundRomSize_ > 0x3a) {
			if (soundRom_[0x39] == 0xdb && soundRom_[0x3a] == 0x18)
				vsIoKind_ = 1;
			else if (soundRom_[0x39] == 0xdb && soundRom_[0x3a] == 0x08)
				vsIoKind_ = 2;
			else if (soundRom_[0x38] == 0xf3 && soundRom_[0x3b] == 0xc3)
				vsIoKind_ = 2;
		}
		memset(mem_, 0, sizeof(mem_));
		if (vsIoKind_ == 2) {
			unsigned n = soundRomSize_ < 0xe000u ? soundRomSize_ : 0xe000u;
			memcpy(mem_, soundRom_, n);
		} else if (vsIoKind_ == 3) {
			/* gunbird: fixed ROM 0000-7FFF, bank window 8000-FFFF. */
			unsigned n = soundRomSize_ < 0x8000u ? soundRomSize_ : 0x8000u;
			memcpy(mem_, soundRom_, n);
			SetBank(0);
		} else {
			unsigned n = soundRomSize_ < 0x7800u ? soundRomSize_ : 0x7800u;
			memcpy(mem_, soundRom_, n);
		}
	}

	if (!loaded
		&& board_ != CEMU_AC_BOARD_NAMCO_SYS1
		&& board_ != CEMU_AC_BOARD_NAMCO_SYS86
		&& board_ != CEMU_AC_BOARD_NAMCO_WSG
		&& !(pcmRomSize_ && (board_ == CEMU_AC_BOARD_NAMCO_C352
			|| board_ == CEMU_AC_BOARD_NAMCO_SYS2))) {
		/* Model 2/3 / System24 disk packs often ship without a Z80 image.
		   Still Open so the play path can classify SILENT instead of FAIL_OPEN. */
		const int softOpen =
			(_strnicmp(ge->subtype, "model2", 6) == 0
				|| _stricmp(ge->subtype, "model3") == 0
				|| board_ == CEMU_AC_BOARD_SYS24
				|| board_ == CEMU_AC_BOARD_SYS32
				|| fs->fileCount > 0) ? 1 : 0;
		if (!softOpen)
			return 0;
		loaded = 1;
	}

	/* Cotton song table at Z80 8A00 needs banked ROM on hardware.
	   Local cotton.zip: epr13860.a10 duplicates s-prog; opr13893.a11 is
	   speech/PCM (not a Z80 song table). Leave 8A00 empty �� REGSONLY. */

	cpu_->reset(mem_);
	cpu_->r.pc = 0;
	/* Real Z80 power-on leaves A?0xFF. Ay_Cpu::reset zeroes regs; m99-family
	   M72 (bbmanw/poundfor) PUSH AF at boot and stash A into F4DC as a command
	   AND-mask ? A=0 makes every latch byte look empty. */
	if (board_ == CEMU_AC_BOARD_IREM_M72)
		cpu_->r.b.a = 0xff;
	cpuCycles_ = 0;
	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	wsgNmiEnable_ = 0;
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		mem_[0x9101] = 0;
		mem_[0x8c01] = 0;
		/* Dig Dug (LD SP,$8B80): needs NMI during boot; Galaga/Bosco must
		   hold busy so early NMI cannot trash the ROM checksum. */
		if (mem_[0] == 0x31 && mem_[1] == 0x80 && mem_[2] == 0x8b) {
			wsgNmiEnable_ = 1;
		} else {
			mem_[0x9a8c] = 1;
			mem_[0x9b3c] = 1;
		}
	}
	ymAddr_ = 0;
	abStatusPulseSlot_ = ~0ull;
	memset(gngYmAddr_, 0, sizeof(gngYmAddr_));
	hangYmAddr_ = 0;
	memset(sytSlaveData_, 0, sizeof(sytSlaveData_));
	memset(sytMasterData_, 0, sizeof(sytMasterData_));
	sytMainMode_ = 0;
	sytSubMode_ = 0;
	sytStatus_ = 0;
	sytNmiEnabled_ = 0;
	m72SampleAddr_ = 0;
	/* Sniff M72 alternate I/O (YM2151 at 40/41) from early OUT opcodes. */
	if (board_ == CEMU_AC_BOARD_IREM_M72 && !m72IoAlt_) {
		for (unsigned i = 0; i + 1 < 0x400u; i++) {
			if (mem_[i] == 0xd3 && mem_[i + 1] == 0x40) {
				m72IoAlt_ = 1;
				break;
			}
		}
	}
	sjLatchFlag_ = 0;
	sjSemaphore2_ = 0;
	sjNmiMask_ = 0;
	sjNmiMaskSeen_ = 0;
	memset(ayAddr_, 0, sizeof(ayAddr_));
	bank_ = 0;
	bankLoaded_ = 0;
	SetBank(0);
	if (chip_) chip_->Reset();
	if (chip2_) chip2_->Reset();
	if (chip3_) chip3_->Reset();
	if (pcm_) pcm_->Reset();
	if (pcmRomSize_) {
		pcmTarget = CEmuAcPrimaryPcmTarget(this);
		if (pcmTarget)
			pcmTarget->SetPcmRom(pcmRom_, pcmRomSize_);
	}
	opmWrites_ = 0;
	if (m72Code) free(m72Code);
	return 1;
}

void CEmuHardAcSetActive(CHardAc* hw)
{
	CEmuZ80BusSetActive(hw);
}
