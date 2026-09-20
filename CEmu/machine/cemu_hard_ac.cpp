#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_ac_board_spec.h"
#include "cemu_m68k_bus.h"
#include "cemu_v35_bus.h"
#include "cemu_h6280_bus.h"
#include "cemu_h8_bus.h"
#include "cemu_m37702_bus.h"
#include "cemu_namco_c7x_rom.h"
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
#include "../chip/cemu_chip_x1_010.h"
#include "../chip/cemu_chip_ymz280b.h"
#include "../chip/cemu_chip_msm5232.h"
#include "../chip/cemu_chip_ga20.h"
#include "../chip/cemu_chip_irem_dac.h"
#include "cemu_sei80bu.h"
#include "../chip/cemu_chip_scsp.h"
#include "../chip/cemu_chip_rf5c400.h"
#include "../chip/cemu_chip_scc.h"
#include "cemu_kabuki.h"
#include "../chip/cemu_chip_multipcm.h"
#include "../chip/cemu_chip_ymz280b.h"
#include "../chip/cemu_chip_x1_010.h"
#include "../s98/device/emu2413/emu2413.h"
#include "../fmmon/fmmon_shadow.h"
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
/* SYS2: $7200 後の双子メールボックス（assault/dirtfox/finallap/mirninja） */
static uint8_t s_acSys2TwinMail = 0;

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
	, konamiPcm2Addr_(0u)
	, konamiSoundCtrl_(0)
	, konamiSh1NmiArm_(0)
	, ms1Rom_(NULL)
	, ms1RomSize_(0)
	, scspSampleBank_(0)
	, hornetGti_(0)
	, hornetTimerEn_(0)
	, hornetTimerIrq_(0)
	, hornetTimerAcc_(0)
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
	, raizingType_(0)
	, raizingLatchPending_(0)
	, raizingNmiPending_(0)
	, raizingLastKeyOns_(0)
	, raizingIdlePolls_(0)
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
	, s32IrqIn_(0)
	, s32Bank_(0)
	, s32YmIrqPrev_(0)
	, konamiK7232Map_(0)
	, taitoOpmMap_(0)
	, alphaOpll_(NULL)
	, alphaYmAddr_(0)
	, alphaOpllAddr_(0)
	, alphaNmiMask_(0)
	, alphaPaLatch_(0)
	, sjLatchFlag_(0)
	, sjSemaphore2_(0)
	, sjNmiMask_(0)
	, sjNmiMaskSeen_(0)
	, toaplanTimerA_(0)
	, toaplanYmPort_(0)
	, toaplanKaneko_(0)
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
	memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
	memset(raizingLatch_, 0, sizeof(raizingLatch_));
	memset(raizingLatchOut_, 0, sizeof(raizingLatchOut_));
	memset(s32IrqCtrl_, 0, sizeof(s32IrqCtrl_));
}

CHardAc::~CHardAc()
{
	Shutdown();
}

/* IsAcPlatform の実装 */
static int IsAcPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->subtype, "sg1000") == 0 || _stricmp(ge->dataDir, "sc3000") == 0)
		return 0;
	if (_stricmp(ge->subtype, "pico") == 0 || _stricmp(ge->dataDir, "pico") == 0)
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
	/* NeoGeo は CHardNeo — AC board=UNKNOWN（board=?/0）として開かない */
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

/* Data East HuC6280 + YM2151（+ OKI）— cninja / thndzone / deco32 級。古い M6502/R65C02 基板はここに列挙しない（CEmuAcIsDecoM6502Sub 参照）。 */
static int CEmuAcIsDecoH6280Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	static const char* const kSubs[] = {
		"deco32", "thndzone", "supbtime", "cninja", "decomlc",
		"midres"
	};
	for (int i = 0; i < (int)_countof(kSubs); i++)
		if (_stricmp(sub, kSubs[i]) == 0)
			return 1;
	return 0;
}

/* Data East M6502 / R65C02 音源基板（karnov / dec0 / dec8 マップ） */
static int CEmuAcIsDecoDec8Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	/* MAME dec8.cpp: R65C02、YM2203@$2000、YM3812@$4000、ラッチ@$6000。ここに drgninja を載せない — Bad Dudes / Heavy Barrel / Robocop は dec0。 */
	static const char* const kSubs[] = {
		"cobracom", "brkthru", "exprraid", "makyosen", "oscar"
	};
	for (int i = 0; i < (int)_countof(kSubs); i++)
		if (_stricmp(sub, kSubs[i]) == 0)
			return 1;
	return 0;
}

/* MAME dec0.cpp: YM2203@$0800、YM3812@$1000、ラッチ@$3000、OKI@$3800 */
static int CEmuAcIsDecoDec0Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	static const char* const kSubs[] = {
		"drgninja", "baddudes", "hbarrel", "hippodrm", "robocop",
		"birdtry", "stadhero", "slyspy", "secretag", "lastmisn"
	};
	for (int i = 0; i < (int)_countof(kSubs); i++)
		if (_stricmp(sub, kSubs[i]) == 0)
			return 1;
	return 0;
}

/* CEmuAcIsDecoM6502Sub の実装 */
static int CEmuAcIsDecoM6502Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	if (_stricmp(sub, "karnov") == 0 || _stricmp(sub, "triothep") == 0
		|| _stricmp(sub, "actfancr") == 0)
		return 1;
	if (CEmuAcIsDecoDec0Sub(sub)) return 1;
	return CEmuAcIsDecoDec8Sub(sub);
}

/* CEmuAcIsDecoSub の実装 */
static int CEmuAcIsDecoSub(const char* sub)
{
	return CEmuAcIsDecoH6280Sub(sub) || CEmuAcIsDecoM6502Sub(sub);
}

/* decoCpuKind_ の種別: 0=H6280 cninja、1=karnov、2=dec0/actfancr、4=dec8、8=midres HuC6280 */
static int CEmuAcDecoCpuKind(const char* sub)
{
	if (!sub) return 0;
	if (_stricmp(sub, "midres") == 0) return 8;
	if (_stricmp(sub, "karnov") == 0) return 1;
	if (CEmuAcIsDecoDec8Sub(sub)) return 4;
	if (CEmuAcIsDecoDec0Sub(sub)) return 2;
	if (CEmuAcIsDecoM6502Sub(sub)) return 2;
	return 0;
}

/* MAME raiden2.cpp: SEI80BU Z80 + YM2151 + OKI×2。raiden（YM3812）と混ぜない。 */
static int CEmuAcIsRaiden2(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && _stricmp(ge->subtype, "raiden2") == 0)
		return 1;
	if (!ge->archive || !ge->archive[0]) return 0;
	if (_strnicmp(ge->archive, "raiden2", 7) == 0) return 1;
	if (_strnicmp(ge->archive, "raidndx", 7) == 0) return 1;
	if (_strnicmp(ge->archive, "raidendx", 8) == 0) return 1;
	return 0;
}

/* MAME t5182.cpp: Toshiba T5182 内部 Z80 + YM2151。8K 内部 ROM が CPU、32K は 8000 の曲データ。 */
static int CEmuAcIsT5182(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	static const char* const kNames[] = {
		"darkmist", "mustache", "panicr", "metlfrzr"
	};
	for (unsigned i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
		const size_t n = strlen(kNames[i]);
		if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, kNames[i]) == 0)
			return 1;
		if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, kNames[i], (int)n) == 0)
			return 1;
	}
	return 0;
}

/* MAME sunelectronics/shangha3.cpp heberpop / blocken: Z80+YM3438+OKI。Seibu ではない。 */
static int CEmuAcIsHeberpop(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	static const char* const kNames[] = { "heberpop", "blocken" };
	for (unsigned i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
		const size_t n = strlen(kNames[i]);
		if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, kNames[i]) == 0)
			return 1;
		if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, kNames[i], (int)n) == 0)
			return 1;
	}
	return 0;
}

/* MAME taito/tnzs.cpp kabukiz: 第 3 Z80 + YM2203 I/O。Konami K054539 ではない。 */
static int CEmuAcIsKabukiz(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, "kabukiz") == 0)
		return 1;
	if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, "kabukiz", 7) == 0)
		return 1;
	return 0;
}

/* MAME capcom/bionicc.cpp: Z80+YM2151。Taito OPM ではない。topsecrt は日本名。 */
static int CEmuAcIsBionicc(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	static const char* const kNames[] = { "bionicc", "topsecrt" };
	for (unsigned i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
		const size_t n = strlen(kNames[i]);
		if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, kNames[i]) == 0)
			return 1;
		if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, kNames[i], (int)n) == 0)
			return 1;
	}
	return 0;
}

/* MAME capcom/sf.cpp: 音楽 Z80 + YM2151 @E000、ラッチ C800→NMI。sf2/sfa には食い込まない。 */
static int CEmuAcIsStreetFighter(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, "sf1") == 0)
		return 1;
	if (ge->archive && ge->archive[0]) {
		static const char* const kArcs[] = {
			"sf", "sfj", "sfua", "sfan", "sfp", "sfjan", "sfw"
		};
		for (unsigned i = 0; i < sizeof(kArcs) / sizeof(kArcs[0]); i++)
			if (_stricmp(ge->archive, kArcs[i]) == 0)
				return 1;
	}
	return 0;
}

/* MAME legionna.cpp godzilla / dcon.cpp sdgndmps: seibu_sound_map + YM2151 + OKI×1（cupsoc の YM3812 ではない）。 */
static int CEmuAcIsSeibuYm2151(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]) {
		if (_stricmp(ge->archive, "godzilla") == 0) return 1;
		if (_stricmp(ge->archive, "sdgndmps") == 0) return 1;
		if (_strnicmp(ge->archive, "denjinmk", 8) == 0) return 1;
		if (_strnicmp(ge->archive, "grainbow", 8) == 0) return 1;
	}
	if (ge->subtype && ge->subtype[0]
		&& (_stricmp(ge->subtype, "godzilla") == 0
			|| _stricmp(ge->subtype, "sdgndmps") == 0))
		return 1;
	return 0;
}

/* MAME cabal.cpp: SEI80BU 8K @0000 + 32K 曲 ROM @8000、YM2151、MSM5205×2（PCM は後回し）。 */
static int CEmuAcIsCabal(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && _stricmp(ge->subtype, "cabal") == 0)
		return 1;
	if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, "cabal", 5) == 0)
		return 1;
	return 0;
}

/* MAME taito/rbisland.cpp jumping: Z80 24/4 + YM2203×2 24/8 @B000/B400、ラッチ B800→IRQ0。ribl の YM2151+CIU ではない。 */
static int CEmuAcIsJumping(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "jumping", 7) == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "jumping") == 0)
		return 1;
	return 0;
}

/* MAME nemesis.cpp konamigt: nemesis sound_map（ROM 0000-3FFF、RAM 4000-47FF）。VBLANK NMI 無し。8K gx400 共有 ROM（latch==1 ISR）ではない。 */
static int CEmuAcIsKonamigt(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "konamigt", 8) == 0)
		return 1;
	return 0;
}

/* MAME konami/megazone.cpp: Z80 3.072 MHz + AY8910 I/O、共有 RAM E000。timeplt メモリマップではない。 */
static int CEmuAcIsMegazone(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "megazone", 8) == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "megazone") == 0)
		return 1;
	return 0;
}

/* MAME nintendo/mario.cpp masao: Z80 14.31818/8 + AY @4000/6000、ラッチは AY ポートA、IRQ0 は 7F00 立ち下がり。 */
static int CEmuAcIsMasao(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _stricmp(ge->archive, "masao") == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "masao") == 0)
		return 1;
	return 0;
}

/* MAME technos/ddragon.cpp: MC6809 + YM2151@2800。ddragon2/3 は Z80。 */
static int CEmuAcIsDdragon1(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]) {
		if (_strnicmp(ge->archive, "ddragon2", 8) == 0) return 0;
		if (_strnicmp(ge->archive, "ddragon3", 8) == 0) return 0;
		if (_strnicmp(ge->archive, "ddragon", 7) == 0) return 1;
	}
	if (ge->subtype && _stricmp(ge->subtype, "ddragon") == 0)
		return 1;
	return 0;
}

/* MAME technos/renegade.cpp: MC6809 + YM3526@2800。nkdodge/spdodgeb は別基板。 */
static int CEmuAcIsKuniokun(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && (_stricmp(ge->subtype, "kuniokun") == 0
		|| _stricmp(ge->subtype, "renegade") == 0))
		return 1;
	if (!ge->archive || !ge->archive[0]) return 0;
	if (_strnicmp(ge->archive, "kuniokun", 8) == 0) return 1;
	if (_strnicmp(ge->archive, "renegade", 8) == 0) return 1;
	return 0;
}

/* MAME technos/matmania.cpp: M6502 + AY8910×2。maniach は M6809+YM3526 なので載せない。 */
static int CEmuAcIsMatmania(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && ge->subtype[0]
		&& (_stricmp(ge->subtype, "excthour") == 0
			|| _stricmp(ge->subtype, "matmania") == 0
			|| _stricmp(ge->subtype, "bigprowr") == 0))
		return 1;
	if (ge->archive && ge->archive[0]
		&& (_stricmp(ge->archive, "matmania") == 0
			|| _stricmp(ge->archive, "excthour") == 0
			|| _stricmp(ge->archive, "bigprowr") == 0))
		return 1;
	return 0;
}

/* 音源が Z80 + YM2610 + TC0140SYT の Taito サブタイプ */
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

/* 古い Z80 + YM2151 + PC060HA 基板（Rastan/Asuka）の Taito サブタイプ */
static int CEmuAcIsTaitoOpmSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "fullt") == 0
		|| _stricmp(sub, "rastan") == 0
		|| _stricmp(sub, "asuka") == 0
		|| _stricmp(sub, "opwolf") == 0
		|| _stricmp(sub, "rainbow") == 0
		|| _stricmp(sub, "daisenpuu") == 0) ? 1 : 0;
}

/* Taito B System YM2203 + PC060HA（masterw）。viofight は OKI @B000 を追加。tnzs/chukatai は同じ Z80+YM2203+PC060HA 級。 */
static int CEmuAcIsTaitoYm2203Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	/* Taito 8bit 基板（Z80 音源 CPU）は YM2203 セット。後の 16bit だけ YM2151 へ移った。2203 リップを OPM レジスタマップで駆動すると曲データが OPM 制御レジスタへ入り、YM 書込は数えても無音になった。 */
	return (_stricmp(sub, "masterw") == 0
		|| _stricmp(sub, "viofight") == 0
		|| _stricmp(sub, "tnzs") == 0
		|| _stricmp(sub, "chukatai") == 0
		|| _stricmp(sub, "extrmatn") == 0
		|| _stricmp(sub, "palamed") == 0
		|| _stricmp(sub, "cachat") == 0
		|| _stricmp(sub, "kurikint") == 0
		|| _stricmp(sub, "plumppop") == 0
		|| _stricmp(sub, "kikikai") == 0
		|| _stricmp(sub, "bubblebobble") == 0
		|| _stricmp(sub, "flipull") == 0
		|| _stricmp(sub, "plotting") == 0
		|| _stricmp(sub, "puzznic") == 0
		|| _stricmp(sub, "cubybop") == 0
		|| _stricmp(sub, "arkanoid") == 0
		|| _stricmp(sub, "arkanoid2") == 0
		|| _stricmp(sub, "kicknrun") == 0
		|| _stricmp(sub, "ribl") == 0
		|| _stricmp(sub, "gladiatr") == 0
		|| _stricmp(sub, "horshoes") == 0
		|| _stricmp(sub, "ashnojoe") == 0
		|| _stricmp(sub, "fhawk") == 0
		|| _stricmp(sub, "volfied") == 0
		|| _stricmp(sub, "insectx") == 0
		|| _stricmp(sub, "kabukiz") == 0
		|| _stricmp(sub, "kageki") == 0
		|| _stricmp(sub, "darius") == 0
		|| _stricmp(sub, "tokio") == 0
		|| _stricmp(sub, "lsasquad") == 0
		|| _stricmp(sub, "daikaiju") == 0
		|| _stricmp(sub, "kage") == 0) ? 1 : 0;
}

/* Konami Z80 + YM2151 + K007232（PCM は stub。FM が BGM 経路）。salamander/lifeforce は scontra と同じマップ（ラッチ A000、YM C000）。 */
static int CEmuAcIsKonamiK7232Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	/* Konami「Z80 + YM2151（+ K007232 PCM）」音源区画。カタログはゲーム毎に 1 ドライバ型なので同じ基板が十数ラベルで再出。エイリアス無しだと BOARD_UNKNOWN になり全く鳴らない。K007232 サンプルは未合成なので YM2151 のみ BGM になる。 */
	static const char* const kSubs[] = {
		"scontra", "thundercross", "crimfght", "twin16", "salamander",
		"ajax", "gradius3", "chqflag", "88games", "bottom9", "flakattack",
		"blkpanther", "bladestl", "fastlane", "hotchase",
		"rollergames", "crusherm", "combatsc", "contra",
		"ddribble", "jackal",
		"labyrunr", "battlnts", "aliens2", "weclemans"
	};
	for (unsigned i = 0; i < sizeof(kSubs) / sizeof(kSubs[0]); i++)
		if (_stricmp(sub, kSubs[i]) == 0) return 1;
	return 0;
}

/* MAME k007452: 8bit×8bit 乗算と 16bit÷16bit。wecleman/flkatck は $9000。
   CHardAc にメンバを足さないので namcoCus30_ 先頭 12 バイトに載せる（K7232 では CUS30 未使用）。
   [0..5]=operands、[6..7]=product、[8..9]=remainder、[10..11]=quotient。 */
static uint8_t CEmuAcK007452Read(const uint8_t* s, unsigned off)
{
	switch (off & 7u) {
	case 0: return s[6];
	case 1: return s[7];
	case 2: return s[8];
	case 3: return s[9];
	case 4: return s[10];
	case 5: return s[11];
	default: return 0;
	}
}

static void CEmuAcK007452Write(uint8_t* s, unsigned off, uint8_t data)
{
	off &= 7u;
	if (off < 6u)
		s[off] = data;
	if (off == 1u) {
		const unsigned r = (unsigned)s[0] * (unsigned)s[1];
		s[6] = (uint8_t)r;
		s[7] = (uint8_t)(r >> 8);
	} else if (off == 5u) {
		const unsigned dividend = ((unsigned)s[4] << 8) | (unsigned)s[5];
		const unsigned divisor = ((unsigned)s[2] << 8) | (unsigned)s[3];
		if (!divisor) {
			s[8] = s[9] = 0;
			s[10] = s[11] = 0xff;
		} else {
			const unsigned q = dividend / divisor;
			const unsigned rem = dividend % divisor;
			s[8] = (uint8_t)rem;
			s[9] = (uint8_t)(rem >> 8);
			s[10] = (uint8_t)q;
			s[11] = (uint8_t)(q >> 8);
		}
	}
}

/* Seta/Allumer と Cave はゲーム本体のメインプログラムを走るので、チップ・作業 RAM・IRQ 原因レジスタが実基板どおりにデコードされないと音楽コードに届かない — しかもゲーム同士で一致しない。各行は MAME の address_map（seta.cpp、seta2.cpp、atlus/cave.cpp）。

   kind 1 = X1-010 RAM 窓、kind 2 = Cave YMZ280B レジスタ／データ対。 */
struct CEmuAcM68kPcmSpec {
	const char* sub;
	int kind;
	unsigned chipAddr;
	unsigned chipSpan;
	unsigned ramAddr;
	unsigned ramSize;
	unsigned irqAddr; /* Cave irq_cause_r 窓。基板に無いときは 0 */
};

static const CEmuAcM68kPcmSpec kAcM68kPcmSpecs[] = {
	/* --- Seta / Allumer の X1-010 --- */
	{ "blandia",  1, 0xc00000u, 0x4000u, 0x200000u, 0x10000u,  0 },
	{ "daioh",    1, 0xc00000u, 0x4000u, 0x100000u, 0x10000u,  0 },
	{ "drgnunit", 1, 0x100000u, 0x4000u, 0xf00000u, 0x10000u,  0 },
	{ "madshark", 1, 0xd00000u, 0x4000u, 0x200000u, 0x10000u,  0 },
	{ "atehate",  1, 0x100000u, 0x4000u, 0x900000u, 0x100000u, 0 },
	{ "magspeed", 1, 0xd00000u, 0x4000u, 0x200000u, 0x10000u,  0 },
	/* seta2 セットはすべて B00000 で一致 */
	{ "grdians",  1, 0xb00000u, 0x4000u, 0x200000u, 0x10000u,  0 },
	{ "myangel",  1, 0xb00000u, 0x4000u, 0x200000u, 0x10000u,  0 },
	{ "myangel2", 1, 0xb00000u, 0x4000u, 0x200000u, 0x10000u,  0 },
	/* --- Cave の YMZ280B --- */
	{ "ddonpach", 2, 0x300000u, 4u, 0x100000u, 0x10000u, 0x800000u },
	{ "uopoko",   2, 0x300000u, 4u, 0x100000u, 0x10000u, 0x600000u },
	{ "guwange",  2, 0x800000u, 4u, 0x200000u, 0x10000u, 0x300000u },
	{ "korokoro", 2, 0x240000u, 4u, 0x300000u, 0x10000u, 0x1c0000u }
};

/* CEmuAcFindM68kPcmSpec の実装 */
static const CEmuAcM68kPcmSpec* CEmuAcFindM68kPcmSpec(const char* sub)
{
	if (!sub || !sub[0]) return NULL;
	for (unsigned i = 0; i < _countof(kAcM68kPcmSpecs); i++) {
		if (_stricmp(sub, kAcM68kPcmSpecs[i].sub) == 0)
			return &kAcM68kPcmSpecs[i];
	}
	return NULL;
}

/* TECMO16 マップの一部セットは Toaplan/Cave の YM2151 位置に YM3812 を挿す。カタログはゲーム毎にチップを書き、ドライバ族と食い違う: 本物 tecmo16（riot/ginkun/fstarfrc）は "YM2151+MSM6295"、rygar と tbowl は "YM3812+MSM5205"。 */
static int CEmuAcIsTecmoOplSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "tbowl") == 0
		|| _stricmp(sub, "spbactn") == 0
		|| _stricmp(sub, "rygar") == 0
		|| _stricmp(sub, "gemini") == 0) ? 1 : 0;
}

/* MAME atlus/cave.cpp Z80 音源。tecmoOpl_ にパック（CHardAc メンバを増やさない）。
   5=sailormn/agallet YM2151+OKIx2。7=hotdogst YM2203+OKI。8=mazinger YM2203+OKI。
   9=metmqstr YM2151+OKIx2（hotdogst メモリ）。10=pwrinst2/plegends YM2203+OKIx2。
   11=Dooyong bluehawk_sound_map（superx）。12=powerins nmk16 YM2203+OKIx2+NMK112。
   13=DECO32 Z80 nslasher（fghthistu と同マップ）。 */
static int CEmuAcCaveZ80Kind(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	const char* sub = ge->subtype ? ge->subtype : "";
	const char* arc = ge->archive ? ge->archive : "";
	if ((sub[0] && _stricmp(sub, "agallet") == 0)
		|| (arc && arc[0] && (_stricmp(arc, "agallet") == 0 || _stricmp(arc, "sailormn") == 0)))
		return 5;
	if ((sub[0] && _stricmp(sub, "hotdogst") == 0)
		|| (arc && arc[0] && _stricmp(arc, "hotdogst") == 0))
		return 7;
	if ((sub[0] && _stricmp(sub, "mazinger") == 0)
		|| (arc && arc[0] && _stricmp(arc, "mazinger") == 0))
		return 8;
	if ((sub[0] && _stricmp(sub, "metmqstr") == 0)
		|| (arc && arc[0] && _stricmp(arc, "metmqstr") == 0))
		return 9;
	if ((sub[0] && _stricmp(sub, "pwrinst2") == 0)
		|| (arc && arc[0] && (_stricmp(arc, "pwrinst2") == 0 || _stricmp(arc, "plegends") == 0)))
		return 10;
	return 0;
}

/* Cave OKI: 128KiB 窓×2（lo/hi）。MAME oki_bank_w<Chip,Mask>。 */
static void CEmuAcCaveOkiBank(unsigned* t, unsigned sz, uint8_t data, unsigned mask)
{
	unsigned pages = (sz >= 0x20000u) ? (sz / 0x20000u) : 1u;
	if (!t || pages == 0) return;
	unsigned b1 = (unsigned)(data & mask) % pages;
	unsigned b2 = (unsigned)((data >> 4) & mask) % pages;
	unsigned lo = b1 * 2u, hi = b2 * 2u;
	t[0] = t[1] = t[2] = t[3] = lo;
	t[4] = lo; t[5] = lo + 1u;
	t[6] = hi; t[7] = hi + 1u;
}

static int CEmuAcCaveZ80Ram(int kind, unsigned addr)
{
	if (kind == 5) return addr >= 0xc000u;
	if (kind == 7 || kind == 9 || kind == 10) return addr >= 0xe000u;
	if (kind == 8) return (addr >= 0xc000u && addr < 0xc800u) || addr >= 0xf800u;
	return 0;
}

static unsigned CEmuAcCaveZ80BankMask(int kind)
{
	if (kind == 5) return 0x1fu;
	if (kind == 7 || kind == 9) return 0x0fu;
	if (kind == 8 || kind == 10) return 0x07u;
	return 0x1fu;
}

/* tharrier/manybloc: 00000-1FFFF 固定 ROM[0]、20000-3FFFF は ROM+0x20000 から 128KiB×4。data==3 は無視。 */
static void CEmuAcTharrierOkiBank(unsigned* t, unsigned sz, uint8_t data)
{
	if (!t) return;
	data &= 0x03u;
	if (data == 0x03u) return;
	unsigned pages = (sz >= 0x10000u) ? (sz / 0x10000u) : 1u;
	if (pages == 0) pages = 1u;
	t[0] = t[1] = t[2] = t[3] = t[4] = 0;
	t[5] = (pages > 1u) ? 1u : 0u;
	unsigned p = 2u + (unsigned)data * 2u;
	t[6] = p % pages;
	t[7] = (p + 1u) % pages;
}

/* MAME deco32 sound_bankswitch_w / okim6295 set_rom_bank: 256KiB 窓×2（512KiB ROM）。 */
static void CEmuAcDeco32OkiBank(unsigned* t, unsigned sz, unsigned bank)
{
	if (!t) return;
	unsigned pages = (sz >= 0x10000u) ? (sz / 0x10000u) : 1u;
	unsigned base = (bank & 1u) * 4u;
	if (pages < 4u) {
		t[0] = t[1] = t[2] = t[3] = t[4] = t[5] = t[6] = t[7] = 0;
		return;
	}
	if (base + 3u >= pages)
		base = (pages - 4u) & ~3u;
	t[0] = t[1] = t[2] = t[3] = base;
	t[4] = base;
	t[5] = base + 1u;
	t[6] = base + 2u;
	t[7] = base + 3u;
}

void CHardAc::SeibuOkiBank(uint8_t data)
{
	if (!pcm_ || pcmRomSize_ < 0x40000u) return;
	CEmuAcDeco32OkiBank(raizingOkiBank_[0], pcmRomSize_, data);
	CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
}

/* World/Korea/Japan Night Slashers: Z80 音源。US nslasheru は HuC6280 のまま DECO。 */
static int CEmuAcIsNslasherZ80(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	const char* a = ge->archive ? ge->archive : "";
	if (a[0]) {
		if (_strnicmp(a, "nslasheru", 9) == 0)
			return 0;
		if (_strnicmp(a, "nslasher", 8) == 0)
			return 1;
	}
	if (ge->subtype[0] && _stricmp(ge->subtype, "nslasher") == 0)
		return 1;
	return 0;
}

/* NMK112: OKI の 64KiB×4 窓。pwrinst2 は page_mask 無し（フレーズ表は bank0）。 */
static void CEmuAcNmk112Bank(unsigned* t, unsigned sz, unsigned banknum, uint8_t data)
{
	if (!t) return;
	unsigned pages = (sz >= 0x10000u) ? (sz / 0x10000u) : 1u;
	unsigned page = pages ? ((unsigned)data % pages) : 0u;
	if (banknum == 0) {
		t[0] = t[1] = t[2] = t[3] = t[4] = page;
	} else if (banknum == 1) {
		t[5] = page;
	} else if (banknum == 2) {
		t[6] = page;
	} else {
		t[7] = page;
	}
}

/* ------------------------------------------------------------------------
   ドライバ型エイリアス表。

   カタログはゲーム毎に <driver type> を 1 つ書くので、既にエミュ済みの基板が何十ものラベルで再出し、すべて BOARD_UNKNOWN＝完全無音になっていた。下の各項目は MAME で音源区画（音源 CPU + FM/PSG + ラッチ様式）が一致するエミュ済み基板へ型を向ける。映像ハードはここでは無関係。

   本表は主連鎖が UNKNOWN を返したあとだけ参照するので、既に扱う基板から型を奪わない。

   音源区画に CEmu 未実装コアが要る型は意図的に不在: Seta X1-010（blandia/daioh/grdians/metafox/myangel/atehate/stg/wingforc/madshark）、Cave YMZ280B（ddonpach/guwange/uopoko/korokoro）、Taito F3 ES5505（asurabld）、Sega UFO/Print Club セット。どこへでもマップすると無音がノイズに代わるだけ。daikaiju は X1-010 ではなく Taito lsasquad 系 YM2203+AY（map 5）。
   ------------------------------------------------------------------------ */
struct CEmuAcAliasEntry {
	const char* sub;
	CEmuAcBoard board;
};

static const CEmuAcAliasEntry kAcAliases[] = {
	/* --- Seta / Allumer: メイン 68000 + X1-010 RAM 窓 @B00000 --- */
	{ "blandia",    CEMU_AC_BOARD_M68K_PCM },
	{ "daioh",      CEMU_AC_BOARD_M68K_PCM },
	{ "drgnunit",   CEMU_AC_BOARD_M68K_PCM },
	{ "metafox",    CEMU_AC_BOARD_M68K_PCM },
	{ "madshark",   CEMU_AC_BOARD_M68K_PCM },
	{ "wingforc",   CEMU_AC_BOARD_M68K_PCM },
	{ "atehate",    CEMU_AC_BOARD_M68K_PCM },
	{ "stg",        CEMU_AC_BOARD_M68K_PCM },
	{ "grdians",    CEMU_AC_BOARD_M68K_PCM },
	{ "myangel",    CEMU_AC_BOARD_M68K_PCM },
	{ "myangel2",   CEMU_AC_BOARD_M68K_PCM },
	{ "magspeed",   CEMU_AC_BOARD_M68K_PCM },
	/* --- Cave: メイン 68000 + YMZ280B ポート対 @300000 --- */
	{ "ddonpach",   CEMU_AC_BOARD_M68K_PCM },
	{ "guwange",    CEMU_AC_BOARD_M68K_PCM },
	{ "uopoko",     CEMU_AC_BOARD_M68K_PCM },
	{ "korokoro",   CEMU_AC_BOARD_M68K_PCM },
	/* --- Konami: Z80 + AY×2（timeplt 音源基板） --- */
	{ "megazone",   CEMU_AC_BOARD_KONAMI_TIMEPLT },
	{ "rocnrope",   CEMU_AC_BOARD_KONAMI_TIMEPLT },
	{ "ironhors",   CEMU_AC_BOARD_GNG },
	{ "scotrsht",   CEMU_AC_BOARD_GNG },
	/* --- Konami: SN76489 系クラシック（System 1 音源区画） --- */
	{ "mikie",      CEMU_AC_BOARD_SEGA_SYS1 },
	{ "shaolins",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "kontest",    CEMU_AC_BOARD_SEGA_SYS1 },
	{ "trackfld",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "hyperspt",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "yiear",      CEMU_AC_BOARD_SEGA_SYS1 },
	{ "sbasketb",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "mrgoemon",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "lomakai",    CEMU_AC_BOARD_TAITO_OPM },
	/* --- Konami: Z80 + YM2151（+ K007232）音源 --- */
	{ "mainevt",    CEMU_AC_BOARD_KONAMI_K7232 },
	{ "tmnt",       CEMU_AC_BOARD_KONAMI_K7232 },
	{ "hexion",     CEMU_AC_BOARD_KONAMI_K7232 },
	{ "newufo",     CEMU_AC_BOARD_SYS18 },
	{ "rocknrage",  CEMU_AC_BOARD_KONAMI_K7232 },
	{ "weclemans",  CEMU_AC_BOARD_KONAMI_K7232 },
	{ "gyruss",     CEMU_AC_BOARD_KONAMI_K7232 },
	{ "hustler",    CEMU_AC_BOARD_KONAMI_SCRAMBLE },
	/* --- Konami: Z80 + YM3812（hcastle 音源基板） --- */
	{ "spy",        CEMU_AC_BOARD_KONAMI_HCASTLE },
	/* --- Konami: Z80 + YM2151 + K053260 音源 --- */
	{ "rollerg",    CEMU_AC_BOARD_KONAMI_PCM },
	{ "surpatk",    CEMU_AC_BOARD_KONAMI_PCM },
	{ "overdrive",  CEMU_AC_BOARD_KONAMI_PCM },
	{ "ultraman",   CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	/* --- Konami: 68000 + K054539×2 + K056800（System GX 族） --- */
	{ "zr107",      CEMU_AC_BOARD_KONAMI_GX },
	{ "polycomm",   CEMU_AC_BOARD_KONAMI_GX },

	/* --- Taito: Z80 + YM2151 または YM2203 --- */
	{ "arkanoid",   CEMU_AC_BOARD_TAITO_OPM },
	{ "arkanoid2",  CEMU_AC_BOARD_TAITO_OPM },
	{ "bubblebobble", CEMU_AC_BOARD_TAITO_OPM },
	{ "tokio",       CEMU_AC_BOARD_TAITO_OPM },
	{ "flipull",    CEMU_AC_BOARD_TAITO_OPM },
	{ "darius",     CEMU_AC_BOARD_TAITO_OPM },
	{ "mlanding",   CEMU_AC_BOARD_TAITO_OPM },
	{ "kicknrun",   CEMU_AC_BOARD_TAITO_OPM },
	{ "plumppop",   CEMU_AC_BOARD_TAITO_OPM },
	{ "kurikint",   CEMU_AC_BOARD_TAITO_OPM },
	{ "fhawk",      CEMU_AC_BOARD_TAITO_OPM },
	{ "kikikai",    CEMU_AC_BOARD_TAITO_OPM },
	{ "ribl",       CEMU_AC_BOARD_TAITO_OPM },
	{ "palamed",    CEMU_AC_BOARD_TAITO_OPM },
	{ "cachat",     CEMU_AC_BOARD_TAITO_OPM },
	{ "horshoes",   CEMU_AC_BOARD_TAITO_OPM },
	{ "tubeit",     CEMU_AC_BOARD_TAITO_OPM },
	{ "cubybop",    CEMU_AC_BOARD_TAITO_OPM },
	{ "plotting",   CEMU_AC_BOARD_TAITO_OPM },
	{ "puzznic",    CEMU_AC_BOARD_TAITO_OPM },
	{ "gladiatr",   CEMU_AC_BOARD_TAITO_OPM },
	{ "volfied",    CEMU_AC_BOARD_TAITO_OPM },
	{ "ashnojoe",   CEMU_AC_BOARD_TAITO_OPM },
	{ "momoko",     CEMU_AC_BOARD_GNG },
	{ "masao",      CEMU_AC_BOARD_TAITO_SJ },
	{ "bionicc",    CEMU_AC_BOARD_BIONICC },
	{ "topsecrt",   CEMU_AC_BOARD_BIONICC },
	{ "lastduel",   CEMU_AC_BOARD_ROBOKID },
	{ "madgear",    CEMU_AC_BOARD_ROBOKID },
	{ "sf1",        CEMU_AC_BOARD_TAITO_OPM },
	{ "2mindril",   CEMU_AC_BOARD_TAITO_OPM },
	/* --- Taito: Z80 + YM2610 音源 --- */
	{ "wits",       CEMU_AC_BOARD_TAITO_YM2610 },
	{ "insectx",    CEMU_AC_BOARD_TAITO_OPM },
	{ "kabukiz",    CEMU_AC_BOARD_TAITO_OPM },
	{ "jumping",    CEMU_AC_BOARD_GNG },
	{ "xsystem",    CEMU_AC_BOARD_TAITO_YM2610 },
	{ "godzilla",   CEMU_AC_BOARD_SEIBU_OPL },
	{ "enmadaio",   CEMU_AC_BOARD_TAITO_YM2610 },
	/* --- Z80 + AY + MSM5232（flstory 音源基板） --- */
	{ "lsasquad",   CEMU_AC_BOARD_TAITO_OPM },
	{ "daikaiju",   CEMU_AC_BOARD_TAITO_OPM },
	{ "msisaac",    CEMU_AC_BOARD_FLSTORY },
	{ "kage",       CEMU_AC_BOARD_TAITO_OPM },
	{ "equites",    CEMU_AC_BOARD_FLSTORY },
	/* --- Z80 + AY-3-8910 複数 --- */
	{ "chaknpop",   CEMU_AC_BOARD_TAITO_SJ },
	{ "retofinv",   CEMU_AC_BOARD_TAITO_SJ },
	{ "pbaction",   CEMU_AC_BOARD_TAITO_SJ },
	{ "solomon",    CEMU_AC_BOARD_TAITO_SJ },
	{ "1942",       CEMU_AC_BOARD_TAITO_SJ },
	{ "sonson",     CEMU_AC_BOARD_TAITO_SJ },
	{ "sidearms",   CEMU_AC_BOARD_GNG },
	{ "exerizer",   CEMU_AC_BOARD_GNG },
	{ "fcombat",    CEMU_AC_BOARD_TAITO_SJ },
	{ "ikki",       CEMU_AC_BOARD_TAITO_SJ },
	{ "tubep",      CEMU_AC_BOARD_TAITO_SJ },
	{ "btime",      CEMU_AC_BOARD_TAITO_SJ },
	{ "bombjack",   CEMU_AC_BOARD_TAITO_SJ },
	{ "popeye",     CEMU_AC_BOARD_TAITO_SJ },
	{ "mrdo",       CEMU_AC_BOARD_TAITO_SJ },
	{ "bankp",      CEMU_AC_BOARD_TAITO_SJ },
	{ "combh",      CEMU_AC_BOARD_TAITO_SJ },
	{ "gberet",     CEMU_AC_BOARD_TAITO_SJ },
	{ "higemaru",   CEMU_AC_BOARD_TAITO_SJ },
	{ "disco",      CEMU_AC_BOARD_TAITO_SJ },
	{ "swimmer",    CEMU_AC_BOARD_TAITO_SJ },
	{ "magmax",     CEMU_AC_BOARD_TAITO_SJ },
	{ "circusc",    CEMU_AC_BOARD_TAITO_SJ },
	{ "starforc",   CEMU_AC_BOARD_TAITO_SJ },
	{ "senjyo",     CEMU_AC_BOARD_TAITO_SJ },
	{ "baluba",     CEMU_AC_BOARD_TAITO_SJ },
	{ "worldcup",   CEMU_AC_BOARD_TAITO_SJ },
	{ "tehkanwc",   CEMU_AC_BOARD_TAITO_SJ },
	{ "gridiron",   CEMU_AC_BOARD_TAITO_SJ },

	/* --- Tecmo: Z80 + YM3812 対 / YM2151 + OKI --- */
	{ "rygar",      CEMU_AC_BOARD_TECMO16 },
	{ "gemini",     CEMU_AC_BOARD_TECMO16 },
	{ "tbowl",      CEMU_AC_BOARD_TECMO16 },
	{ "wc90",       CEMU_AC_BOARD_TECMO16 },
	{ "spbactn",    CEMU_AC_BOARD_TECMO16 },
	/* --- Nichibutsu / Nihon Bussan: I/O 経由 Z80 + YM3812 --- */
	{ "argus",      CEMU_AC_BOARD_TERRACRE },
	{ "terracra",   CEMU_AC_BOARD_TERRACRE },
	{ "valtric",    CEMU_AC_BOARD_TERRACRE },
	{ "butasan",    CEMU_AC_BOARD_TERRACRE },
	{ "bombsa",     CEMU_AC_BOARD_TERRACRE },
	{ "cop01",      CEMU_AC_BOARD_TAITO_SJ },
	{ "terracra",   CEMU_AC_BOARD_TERRACRE },
	{ "ginganin",   CEMU_AC_BOARD_TERRACRE },
	/* --- Toaplan: Z80 + YM3812 音源 --- */
	{ "tp",         CEMU_AC_BOARD_TOAPLAN1 },
	{ "slapfght",   CEMU_AC_BOARD_TOAPLAN1 },
	{ "daisenpuu",  CEMU_AC_BOARD_TAITO_OPM },
	{ "twinhawk",   CEMU_AC_BOARD_TAITO_OPM },
	{ "tekipaki",   CEMU_AC_BOARD_TOAPLAN1 },
	{ "pipibibs",   CEMU_AC_BOARD_TOAPLAN1 },
	{ "vimana",     CEMU_AC_BOARD_TOAPLAN1 },
	{ "fireshrk",   CEMU_AC_BOARD_TOAPLAN1 },
	{ "snowbros",   CEMU_AC_BOARD_TOAPLAN1 },
	/* --- Toaplan 2 / Raizing: Z80 + YM2151 + OKI6295 音源 --- */
	{ "truxton2",   CEMU_AC_BOARD_TECMO16 },
	{ "batrider",   CEMU_AC_BOARD_RAIZING },
	{ "bbakraid",   CEMU_AC_BOARD_RAIZING },
	{ "bgaregga",   CEMU_AC_BOARD_RAIZING },
	{ "mahou",      CEMU_AC_BOARD_RAIZING },
	/* --- Cave / Banpresto: Z80 + YM2151 + OKI6295 音源 --- */
	{ "agallet",    CEMU_AC_BOARD_TECMO16 },
	{ "metmqstr",   CEMU_AC_BOARD_TECMO16 },
	{ "mazinger",   CEMU_AC_BOARD_TECMO16 },
	{ "hotdogst",   CEMU_AC_BOARD_TECMO16 },
	{ "gaia",       CEMU_AC_BOARD_TECMO16 },
	{ "dadandan",   CEMU_AC_BOARD_TECMO16 },
	{ "pwrinst1",   CEMU_AC_BOARD_TECMO16 },
	{ "pwrinst2",   CEMU_AC_BOARD_TECMO16 },
	/* --- Kaneko / Tatsumi / その他 Z80 + YM + OKI --- */
	{ "djboy",      CEMU_AC_BOARD_ROBOKID },
	{ "blazeon",    CEMU_AC_BOARD_ROBOKID },
	{ "hvyunit",    CEMU_AC_BOARD_ROBOKID },
	{ "bloodwar",   CEMU_AC_BOARD_TECMO16 },
	{ "superx",     CEMU_AC_BOARD_TECMO16 },
	{ "apache3",    CEMU_AC_BOARD_TECMO16 },
	{ "cybertnk",   CEMU_AC_BOARD_TECMO16 },
	{ "gigandes",   CEMU_AC_BOARD_TAITO_YM2610 },
	{ "hyperduel",  CEMU_AC_BOARD_TECMO16 },
	{ "crospang",   CEMU_AC_BOARD_ROBOKID },
	{ "pang",       CEMU_AC_BOARD_ROBOKID },
	{ "mgakuen",    CEMU_AC_BOARD_ROBOKID },
	{ "marukin",    CEMU_AC_BOARD_ROBOKID },
	{ "marukina",   CEMU_AC_BOARD_ROBOKID },
	/* --- Seibu / 韓国 YM3812 + OKI6295 --- */
	{ "darkmist",   CEMU_AC_BOARD_T5182 },
	{ "mustache",   CEMU_AC_BOARD_T5182 },
	{ "panicr",     CEMU_AC_BOARD_T5182 },
	{ "metlfrzr",   CEMU_AC_BOARD_T5182 },
	{ "cshooter",   CEMU_AC_BOARD_ROBOKID },
	{ "airbuster",  CEMU_AC_BOARD_ROBOKID },
	{ "nmg5",       CEMU_AC_BOARD_ROBOKID },
	{ "yunsun16",   CEMU_AC_BOARD_ROBOKID },
	{ "pclubys",    CEMU_AC_BOARD_ROBOKID },
	{ "heberpop",   CEMU_AC_BOARD_HEBERPOP },
	{ "blocken",    CEMU_AC_BOARD_HEBERPOP },
	/* --- SNK: Z80 + YM3812 音源 --- */
	{ "sengoku",    CEMU_AC_BOARD_SNK_OPL },
	{ "empcity",    CEMU_AC_BOARD_ROBOKID },
	/* --- UPL: I/O 上の Z80 + YM2203×2 --- */
	{ "ninjakid2",  CEMU_AC_BOARD_ROBOKID },
	{ "nmk004",     CEMU_AC_BOARD_ROBOKID },
	{ "bjtwin",     CEMU_AC_BOARD_ROBOKID },
	{ "tdragon2",   CEMU_AC_BOARD_ROBOKID },
	{ "macross2",   CEMU_AC_BOARD_ROBOKID },
	{ "msgundam",   CEMU_AC_BOARD_ROBOKID },
	{ "tharrier",   CEMU_AC_BOARD_ROBOKID },
	/* --- Technos: Z80 + YM2151 + OKI/ADPCM 音源 --- */
	{ "ddragon",    CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	{ "ctribe",     CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	{ "kuniokun",   CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	{ "nkdodge",    CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	{ "excthour",   CEMU_AC_BOARD_DECO },
	{ "matmania",   CEMU_AC_BOARD_DECO },
	{ "bigprowr",   CEMU_AC_BOARD_DECO },
	{ "dbz",        CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	{ "dbz2",       CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	/* --- Sega Y-board / OutRun 級: Z80 + YM2151 + SegaPCM --- */
	{ "pdrift",     CEMU_AC_BOARD_OUTRUN },
	{ "spmonaco",   CEMU_AC_BOARD_ABURNER },
	{ "eropn",      CEMU_AC_BOARD_HANGON },
	{ "eropnx2",    CEMU_AC_BOARD_HANGON },
	/* --- Sega System 1/2 級 Z80 + SN --- */
	{ "angelkds",   CEMU_AC_BOARD_ROBOKID },
	{ "spcpostn",   CEMU_AC_BOARD_ROBOKID },
	{ "calorie",    CEMU_AC_BOARD_TAITO_SJ },
	{ "perfrman",   CEMU_AC_BOARD_TOAPLAN1 },
	/* --- Banpresto の Sega System 16B / 24 --- */
	{ "gundamex",   CEMU_AC_BOARD_SYS16B },
	{ "sdgndmps",   CEMU_AC_BOARD_SEIBU_OPL },
	/* --- Namco 音源 --- */
	{ "pacman",     CEMU_AC_BOARD_NAMCO_WSG },
	{ "jrpacman",   CEMU_AC_BOARD_NAMCO_WSG },
	{ "rallyx",     CEMU_AC_BOARD_NAMCO_WSG },
	{ "pengo",      CEMU_AC_BOARD_NAMCO_WSG },
	{ "pp",         CEMU_AC_BOARD_NAMCO_WSG },
	{ "pp2",        CEMU_AC_BOARD_NAMCO_WSG },
	{ "tceptor",    CEMU_AC_BOARD_NAMCO_SYS86 },
	{ "system21b",  CEMU_AC_BOARD_NAMCO_SYS2 }
	/* "gnet" は無い: Taito G-NET は SPU 駆動の PS1 派生。C352 基板へ回すと psyvaria/raycris/xiistag が open で故障した。 */
};

/* CEmuAcAliasBoard の実装 */
static CEmuAcBoard CEmuAcAliasBoard(const char* sub)
{
	if (!sub || !sub[0]) return CEMU_AC_BOARD_UNKNOWN;
	for (unsigned i = 0; i < sizeof(kAcAliases) / sizeof(kAcAliases[0]); i++)
		if (_stricmp(sub, kAcAliases[i].sub) == 0)
			return kAcAliases[i].board;
	return CEMU_AC_BOARD_UNKNOWN;
}

/* Sega Pengo: カタログ plat=sega sub=pengo は OPM+CUS30 と書き、さもなくば "pengo" WSG エイリアス前に NAMCO_SYS1 を取る。アーカイブ pengo2 は 315-5010 World セット。 */
static int CEmuAcIsPengo(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype[0] && _stricmp(ge->subtype, "pengo") == 0)
		return 1;
	if (ge->archive && (_stricmp(ge->archive, "pengo") == 0
		|| _stricmp(ge->archive, "pengo2") == 0))
		return 1;
	return 0;
}

/* hoot namco.xml は hopmappy を C30 のみの wsg63701 と書くが、
   MAME namcos86 hopmappy_mcu_map は YM2151 @$2000 + CUS30。
   MCU は $2001 の busy を BMI 待ちするので WSG 経路だと $FF で永久ループ。 */
static int CEmuAcIsHopmappy(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive[0] && _stricmp(ge->archive, "hopmappy") == 0)
		return 1;
	for (int i = 0; i < ge->romCount; i++) {
		const char* n = ge->rom[i].name;
		if (n[0] && (_strnicmp(n, "hm1_", 4) == 0 || _strnicmp(n, "hm1-", 4) == 0))
			return 1;
	}
	return 0;
}

/* MAME src/devices/machine/segacrpt_device.cpp の Sega 315-5010 */
static const uint8_t kSega3155010Conv[32][4] = {
	{ 0xa0,0x80,0xa8,0x88 }, { 0x28,0xa8,0x08,0x88 },
	{ 0x28,0xa8,0x08,0x88 }, { 0xa0,0x80,0xa8,0x88 },
	{ 0xa0,0x80,0x20,0x00 }, { 0xa0,0x80,0x20,0x00 },
	{ 0x08,0x28,0x88,0xa8 }, { 0xa0,0x80,0xa8,0x88 },
	{ 0x08,0x00,0x88,0x80 }, { 0x28,0xa8,0x08,0x88 },
	{ 0xa0,0x80,0x20,0x00 }, { 0x08,0x00,0x88,0x80 },
	{ 0xa0,0x80,0x20,0x00 }, { 0xa0,0x80,0x20,0x00 },
	{ 0xa0,0x80,0x20,0x00 }, { 0x00,0x08,0x20,0x28 },
	{ 0x88,0x80,0x08,0x00 }, { 0xa0,0x80,0x20,0x00 },
	{ 0x88,0x80,0x08,0x00 }, { 0x00,0x08,0x20,0x28 },
	{ 0x08,0x28,0x88,0xa8 }, { 0x08,0x28,0x88,0xa8 },
	{ 0xa0,0x80,0xa8,0x88 }, { 0xa0,0x80,0x20,0x00 },
	{ 0x08,0x00,0x88,0x80 }, { 0x88,0x80,0x08,0x00 },
	{ 0x00,0x08,0x20,0x28 }, { 0x88,0x80,0x08,0x00 },
	{ 0x08,0x28,0x88,0xa8 }, { 0x08,0x28,0x88,0xa8 },
	{ 0x08,0x00,0x88,0x80 }, { 0xa0,0x80,0x20,0x00 }
};

/* CEmuAcSega3155010Decode の実装 */
static void CEmuAcSega3155010Decode(const uint8_t* encIn, uint8_t* opOut, uint8_t* dataOut, unsigned n)
{
	for (unsigned addr = 0; addr < n; addr++) {
		const uint8_t src = encIn[addr];
		const int row = (int)((addr & 1u) + (((addr >> 4) & 1u) << 1)
			+ (((addr >> 8) & 1u) << 2) + (((addr >> 12) & 1u) << 3));
		int col = (int)(((src >> 3) & 1) + (((src >> 5) & 1) << 1));
		uint8_t xorval = 0;
		if (src & 0x80) {
			col = 3 - col;
			xorval = 0xa8;
		}
		opOut[addr] = (uint8_t)((src & (uint8_t)~0xa8) | (uint8_t)(kSega3155010Conv[2 * row][col] ^ xorval));
		dataOut[addr] = (uint8_t)((src & (uint8_t)~0xa8) | (uint8_t)(kSega3155010Conv[2 * row + 1][col] ^ xorval));
	}
}

/* Pengo CPU の 4K EPROM: IC8/7/15/14/21/20/32/31 @0000-7FFF */
static int CEmuAcPengoIcOff(const char* name)
{
	if (!name || !name[0]) return -1;
	int ic = -1;
	for (const char* p = name; *p; p++) {
		if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'c' || p[1] == 'C')
			&& p[2] >= '0' && p[2] <= '9') {
			ic = atoi(p + 2);
			break;
		}
	}
	if (ic < 0) {
		const char* dot = strrchr(name, '.');
		if (dot && dot[1] >= '0' && dot[1] <= '9')
			ic = atoi(dot + 1);
	}
	switch (ic) {
	case 8: return 0x0000;
	case 7: return 0x1000;
	case 15: return 0x2000;
	case 14: return 0x3000;
	case 21: return 0x4000;
	case 20: return 0x5000;
	case 32: return 0x6000;
	case 31: return 0x7000;
	default: return -1;
	}
}

/* ゲーム毎ドライバ名の Konami Z80 + YM2151 + K053260 音源基板 */
static int CEmuAcIsKonamiK053260Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	static const char* const kSubs[] = {
		"tmnt2", "ssriders2", "qgakumon2"
	};
	for (unsigned i = 0; i < sizeof(kSubs) / sizeof(kSubs[0]); i++)
		if (_stricmp(sub, kSubs[i]) == 0) return 1;
	return 0;
}

/* ゲーム毎名の Konami Z80 + K054539（単チップ）音源基板 */
static int CEmuAcIsKonamiK054539Sub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	static const char* const kSubs[] = {
		"lethal", "gaiapolis", "martchmp", "premsocr", "prmrsocr"
	};
	for (unsigned i = 0; i < sizeof(kSubs) / sizeof(kSubs[0]); i++)
		if (_stricmp(sub, kSubs[i]) == 0) return 1;
	return 0;
}

/* MAME konami/gijoe.cpp と lethal.cpp: 同一 Z80 マップ。K054539 @F800、K054321 @FC00、YM 無し。 */
static int CEmuAcIsGijoe(const CEmuGameEntry* ge)
{
	return (ge && ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "gijoe", 5) == 0) ? 1 : 0;
}
static int CEmuAcIsLethalen(const CEmuGameEntry* ge)
{
	return (ge && ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "lethalen", 8) == 0) ? 1 : 0;
}
static int CEmuAcIsGijoeLethal(const CEmuGameEntry* ge)
{
	if (CEmuAcIsGijoe(ge) || CEmuAcIsLethalen(ge))
		return 1;
	/* カタログ archive が空でも subtype lethal は同一マップ（gijoe+lethalen のみ）。 */
	return (ge && ge->subtype && _stricmp(ge->subtype, "lethal") == 0) ? 1 : 0;
}
static int CEmuAcIsXmen(const CEmuGameEntry* ge)
{
	return (ge && ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "xmen", 4) == 0) ? 1 : 0;
}
static int CEmuAcIsGlfgreat(const CEmuGameEntry* ge)
{
	return (ge && ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "glfgreat", 8) == 0) ? 1 : 0;
}
static int CEmuAcIsPrmrsocr(const CEmuGameEntry* ge)
{
	return (ge && ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "prmrsocr", 8) == 0) ? 1 : 0;
}
static int CEmuAcIsRollerg(const CEmuGameEntry* ge)
{
	return (ge && ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "rollerg", 7) == 0) ? 1 : 0;
}
static int CEmuAcIsSpy(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0] && _stricmp(ge->archive, "spy") == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "spy") == 0)
		return 1;
	return 0;
}

/* Taito flstory / nycaptor 級: Z80 + AY-3-8910 + MSM5232 */
static int CEmuAcIsFlstorySub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "flstory") == 0
		|| _stricmp(sub, "nycaptor") == 0
		|| _stricmp(sub, "buggychl") == 0) ? 1 : 0;
}

/* Nichibutsu Terra Cresta 族: I/O 経由 Z80 + YM3526（または YM2203） */
static int CEmuAcIsTerracreSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "terracre") == 0
		|| _stricmp(sub, "terracra") == 0) ? 1 : 0;
}

/* MAME terracre.cpp terracren: YM2203 子基板。ROM は 15b/17b の Z80 32K。YM3526 セット（terracre）ではない。 */
static int CEmuAcIsTerracren(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "terracren", 9) == 0)
		return 1;
	return 0;
}

/* MAME segahang.cpp endurobl: sound_board_2203（Hang-On / Space Harrier と同じ）。endurob2 は 2203×2 + sound_map_2151。 */
static int CEmuAcIsEnduroBl(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "endurobl", 8) == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "eropn") == 0)
		return 1;
	return 0;
}
static int CEmuAcIsEnduroB2(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0]
		&& _strnicmp(ge->archive, "endurob2", 8) == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "eropnx2") == 0)
		return 1;
	return 0;
}

/* MAME segaorun.cpp init_outrunb: Z80 は bit5/6 入れ替え。 */
static int CEmuAcIsOutrunb(const CEmuGameEntry* ge)
{
	if (!ge || !ge->archive || !ge->archive[0]) return 0;
	return (_strnicmp(ge->archive, "outrunb", 7) == 0) ? 1 : 0;
}

/* MAME segaxbd.cpp: Super Monaco GP / Last Survivor は After Burner と同じ X-Board 音源。 */
static int CEmuAcIsSmgp(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, "smgp", 4) == 0)
		return 1;
	if (ge->subtype && _stricmp(ge->subtype, "spmonaco") == 0)
		return 1;
	return 0;
}
static int CEmuAcIsLastsurv(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, "lastsurv", 8) == 0)
		return 1;
	for (int i = 0; i < ge->romCount; i++) {
		const char* n = ge->rom[i].name;
		if (n && n[0] && _strnicmp(n, "epr-12054", 9) == 0)
			return 1;
	}
	return 0;
}

/* MAME konami/hexion.cpp: メイン Z80 が音源。K051649 @E800 + OKI @F200、RAM A000、バンク 8000。 */
static int CEmuAcIsHexion(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->archive && ge->archive[0] && _strnicmp(ge->archive, "hexion", 6) == 0)
		return 1;
	if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, "hexion") == 0)
		return 1;
	return 0;
}

/* MAME sega/segaufo.cpp: New UFO Catcher / Mini。Z80 + YM3438 I/O 40-43、RAM E000。
   ufo21/ufo800 は EX 基板（UPD7759）でファームが違うので載せない。 */
static int CEmuAcIsNewufo(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype && ge->subtype[0] && _stricmp(ge->subtype, "newufo") == 0)
		return 1;
	if (ge->archive && ge->archive[0]) {
		if (_strnicmp(ge->archive, "newufo", 6) == 0)
			return 1;
		if (_stricmp(ge->archive, "ufomini") == 0)
			return 1;
	}
	return 0;
}

/* Nichibutsu Armed F / Terra Force: Z80 + YM3812、RAM @F800、ラッチ I/O 4/6 */
static int CEmuAcIsArmedfSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "terraf") == 0
		|| _stricmp(sub, "armedf") == 0
		|| _stricmp(sub, "cclimbr2") == 0
		|| _stricmp(sub, "kozure") == 0
		|| _stricmp(sub, "legion") == 0) ? 1 : 0;
}

/* Crazy Climber 2 / Legion 下基板: ROM 0000-BFFF、RAM C000-FFFF（MAME cclimbr2_soundmap）。公式 legion は YM3526、ブートレグは YM3812。terraf と同じ I/O ラッチ。F800 のみ RAM は使わない。 */
static int CEmuAcIsCclimbr2Map(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "cclimbr2") == 0
		|| _stricmp(sub, "legion") == 0) ? 1 : 0;
}

/* サブタイプが 4 つの Raizing 音源改訂のどれか（CEMU_AC_BOARD_RAIZING コメント参照）。他は 0。 */
static int CEmuAcRaizingType(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	if (_stricmp(sub, "mahou") == 0) return 1;
	if (_stricmp(sub, "bgaregga") == 0) return 2;
	if (_stricmp(sub, "batrider") == 0) return 3;
	if (_stricmp(sub, "bbakraid") == 0) return 4;
	return 0;
}

/* UPL Ninja Kid II / Atomic Robo-kid: Z80 I/O 上の YM2203×2 */
static int CEmuAcIsRobokidSub(const char* sub)
{
	if (!sub || !sub[0]) return 0;
	return (_stricmp(sub, "robokid") == 0
		|| _stricmp(sub, "ninjakd2") == 0
		|| _stricmp(sub, "ninjakid2") == 0
		|| _stricmp(sub, "mnight") == 0
		|| _stricmp(sub, "airbuster") == 0
		|| _stricmp(sub, "djboy") == 0
		|| _stricmp(sub, "blazeon") == 0
		|| _stricmp(sub, "hvyunit") == 0
		|| _stricmp(sub, "crospang") == 0
		|| _stricmp(sub, "empcity") == 0
		|| _stricmp(sub, "cshooter") == 0
		|| _stricmp(sub, "nmg5") == 0
		|| _stricmp(sub, "yunsun16") == 0
		|| _stricmp(sub, "pclubys") == 0
		|| _stricmp(sub, "angelkds") == 0
		|| _stricmp(sub, "spcpostn") == 0
		|| _stricmp(sub, "deniam16b") == 0
		|| _stricmp(sub, "lastduel") == 0
		|| _stricmp(sub, "madgear") == 0
		|| _stricmp(sub, "pang") == 0
		|| _stricmp(sub, "mgakuen") == 0
		|| _stricmp(sub, "marukin") == 0) ? 1 : 0;
}

/* CEmuAcHasChip の実装 */
static int CEmuAcHasChip(const CEmuGameEntry* ge, int chipId)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->chipCount && i < 8; i++)
		if (ge->chipIds[i] == chipId)
			return 1;
	return 0;
}

/* CEmuAcGxTitleIsVoice の実装 */
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

/* CEmuAcPickGxDefaultTitle の実装 */
unsigned CEmuAcPickGxDefaultTitle(const CEmuGameEntry* ge)
{
	if (!ge || ge->titleCount <= 0)
		return 0x0105u;
	/* バンク 0x01 のキャラ／ステージ曲（0x101-0x10B）を優先。セレクトファンファーレ（0x10C+）、ボイス、スピーカチェック（0x12B）、オープニングボイスは飛ばす。 */
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

/* CEmuAcDestroyMainChip の実装 */
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
	if (hw && hw->board_ == CEMU_AC_BOARD_KONAMI_K7232 && hw->konamiK7232Map_ == 7) {
		CEmuChipSccDestroy(chip);
		return;
	}
	if (hw && hw->board_ == CEMU_AC_BOARD_TAITO_OPM && hw->TaitoOpmMap() == 16) {
		CEmuChipAyDestroy(chip);
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
		if (hw->board_ == CEMU_AC_BOARD_TAITO_SJ
			&& (hw->vsIoKind_ == 15 || hw->vsIoKind_ == 16
				|| hw->vsIoKind_ == 17 || hw->vsIoKind_ == 18
				|| hw->vsIoKind_ == 21 || hw->vsIoKind_ == 22))
			CEmuChipSn76489Destroy(chip);
		else
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
		if (hw && hw->KonamiRollergMap())
			CEmuChipYm3812Destroy(chip);
		else if (hw && hw->pcmKind_ == 4)
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
		if (hw && (hw->toaplanKaneko_ == 3 || hw->toaplanKaneko_ == 5))
			CEmuChipAyDestroy(chip);
		else
			CEmuChipYm3812Destroy(chip);
		break;
	case CEMU_AC_BOARD_TECMO16:
		if (hw && (hw->tecmoOpl_ == 0 || hw->tecmoOpl_ == 5 || hw->tecmoOpl_ == 9))
			CEmuChipYm2151Destroy(chip);
		else if (hw && (hw->tecmoOpl_ == 6 || hw->tecmoOpl_ == 7
			|| hw->tecmoOpl_ == 8 || hw->tecmoOpl_ == 10))
			CEmuChipYm2608Destroy(chip);
		else
			CEmuChipYm3812Destroy(chip);
		break;
	case CEMU_AC_BOARD_SNK_OPL:
		if (hw && hw->snkMapKind_ == 3)
			CEmuChipAyDestroy(chip);
		else
			CEmuChipYm3812Destroy(chip);
		break;
	case CEMU_AC_BOARD_SEIBU_OPL:
		if (hw && hw->seibuSongOr80_ >= 2)
			CEmuChipYm2151Destroy(chip);
		else
			CEmuChipYm3812Destroy(chip);
		break;
	case CEMU_AC_BOARD_KONAMI_HCASTLE:
	case CEMU_AC_BOARD_BATTLANTIS:
		CEmuChipYm3812Destroy(chip);
		break;
	case CEMU_AC_BOARD_ALPHA68K2:
		if (hw && hw->vsIoKind_ == 1)
			CEmuChipAyDestroy(chip);
		else
			CEmuChipYm2608Destroy(chip);
		break;
	case CEMU_AC_BOARD_ROBOKID:
		if (hw && (hw->vsIoKind_ == 7 || hw->vsIoKind_ == 9
			|| hw->vsIoKind_ == 10 || hw->vsIoKind_ == 12
			|| hw->vsIoKind_ == 15))
			CEmuChipYm3812Destroy(chip);
		else if (hw && hw->vsIoKind_ == 5)
			CEmuChipYm2151Destroy(chip);
		else
			CEmuChipYm2608Destroy(chip);
		break;
	case CEMU_AC_BOARD_TERRACRE:
		if (hw && hw->terracreMap_ >= 3)
			CEmuChipYm2608Destroy(chip);
		else
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

/* CEmuAcDestroyPcmChip の実装 */
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

/* CEmuAcDestroyAuxChip の実装 */
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


/* CEmuAcResolveBoard の実装 */
CEmuAcBoard CEmuAcResolveBoard(const CEmuGameEntry* ge)
{
	if (!ge) return CEMU_AC_BOARD_UNKNOWN;
	if (CEmuAcIsHopmappy(ge))
		return CEMU_AC_BOARD_NAMCO_SYS86;
	/* T5182 は Taito OPM / Seibu OPL エイリアスより先。darkmist は plat=taito、mustache は seibu+mustache。 */
	if (CEmuAcIsT5182(ge))
		return CEMU_AC_BOARD_T5182;
	if (CEmuAcIsHeberpop(ge))
		return CEMU_AC_BOARD_HEBERPOP;
	/* X-Board: カタログ sub=spmonaco/toutrun でも After Burner と同じ Z80+YM2151+SegaPCM。 */
	if (CEmuAcIsSmgp(ge) || CEmuAcIsLastsurv(ge))
		return CEMU_AC_BOARD_ABURNER;
	if (CEmuAcIsBionicc(ge))
		return CEMU_AC_BOARD_BIONICC;
	if (CEmuAcIsKabukiz(ge))
		return CEMU_AC_BOARD_TAITO_OPM;
	if (CEmuAcIsStreetFighter(ge))
		return CEMU_AC_BOARD_TAITO_OPM;
	if (CEmuAcIsNewufo(ge))
		return CEMU_AC_BOARD_SYS18;
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
		|| _stricmp(ge->subtype, "cps2simm") == 0
		|| _stricmp(ge->subtype, "zn") == 0 || hasQSound)
		board = CEMU_AC_BOARD_CPS_QS;
	/* Sega System 16B クローン／派生: 別ドライバ名の同じ Z80 + YM 音源区画。動く基板を再利用。 */
	else if (_stricmp(ge->subtype, "deniam16b") == 0
		|| (ge->archive[0] && (_stricmp(ge->archive, "logicpro") == 0
			|| _stricmp(ge->archive, "karianx") == 0)))
		board = CEMU_AC_BOARD_ROBOKID;
	else if (_stricmp(ge->subtype, "lastduel") == 0
		|| _stricmp(ge->subtype, "madgear") == 0
		|| (ge->archive[0] && (_stricmp(ge->archive, "lastduel") == 0
			|| _stricmp(ge->archive, "madgear") == 0
			|| _stricmp(ge->archive, "ledstorm") == 0)))
		board = CEMU_AC_BOARD_ROBOKID;
	else if (_stricmp(ge->subtype, "deniam16c") == 0)
		board = CEMU_AC_BOARD_SYS16B;
	/* Sega System E: Z80 + SN76496×2、つまり System 1 音源区画 */
	else if (_stricmp(ge->subtype, "systeme") == 0)
		board = CEMU_AC_BOARD_SEGA_SYS1;
	/* Technos Double Dragon 3 は ddragon2 の Z80 + YM2151 + OKI6295 を共有 */
	else if (_stricmp(ge->subtype, "ddragon3") == 0)
		board = CEMU_AC_BOARD_TECHNOS_DDRAGON2;
	/* MAME konami/dbz.cpp: Z80 + YM2151 @C000 + OKI @D000 + latch @E000（K053260 ではない） */
	else if (_stricmp(ge->subtype, "dbz") == 0)
		board = CEMU_AC_BOARD_TECHNOS_DDRAGON2;
	/* MAME konami/ultraman.cpp: Z80 + YM2151 @F000 + OKI @E000 + latch @C000。K053260 ではない */
	else if (_stricmp(ge->subtype, "ultraman") == 0
		|| (ge->archive[0] && _stricmp(ge->archive, "ultraman") == 0))
		board = CEMU_AC_BOARD_TECHNOS_DDRAGON2;
	/* Atari Gauntlet hw は System 1 音源区画（6502 + YM2151 + POKEY） */
	else if (_stricmp(ge->subtype, "gauntlet") == 0)
		board = CEMU_AC_BOARD_ATARI_SYS1;
	/* Tehkan / Taito 多 AY 音源 CPU は Taito SJ 配置を共有 */
	else if (_stricmp(ge->subtype, "starforce") == 0
		|| _stricmp(ge->subtype, "swimmer") == 0
		|| _stricmp(ge->subtype, "halleysc") == 0
		|| _stricmp(ge->subtype, "worldcup") == 0
		|| _stricmp(ge->subtype, "circusc") == 0)
		board = CEMU_AC_BOARD_TAITO_SJ;
	else if (_strnicmp(ge->subtype, "system18", 8) == 0)
		board = CEMU_AC_BOARD_SYS18;
	else if (_strnicmp(ge->subtype, "system24", 8) == 0)
		board = CEMU_AC_BOARD_SYS24;
	else if (_strnicmp(ge->subtype, "system32", 8) == 0
		|| _stricmp(ge->subtype, "system_multi") == 0
		|| _stricmp(ge->subtype, "multi32") == 0)
		board = CEMU_AC_BOARD_SYS32;
	else if (_stricmp(ge->subtype, "systemgx") == 0)
		/* 本物 System GX（konamigx.cpp: tkmmpzdm/rungun2/racinfrc…）だけが 68000 音源 CPU + K056800 メールボックスの K054539×2 族。mystwarr.cpp 族はチップは共有するが Z80 なので "054539x2" サブタイプは KONAMI_PCM へ — ここに回すと $0000 の Z80 プログラム（DI/IM 1/JP）を Musashi に渡し音源 ROM 未ロード（srom=0）。 */
		board = CEMU_AC_BOARD_KONAMI_GX;
	else if ((_stricmp(ge->platform, "namco") == 0
		&& (_stricmp(ge->subtype, "system2") == 0
			|| _stricmp(ge->subtype, "system21") == 0
			|| _stricmp(ge->subtype, "c140") == 0))
		|| (hasC140 && !naNb))
		board = CEMU_AC_BOARD_NAMCO_SYS2;
	else if (_stricmp(ge->platform, "namco") == 0 && _stricmp(ge->subtype, "system1") == 0)
		board = CEMU_AC_BOARD_NAMCO_SYS1;
	else if ((_stricmp(ge->platform, "namco") == 0
			&& _stricmp(ge->subtype, "system86") == 0)
		/* hoot namco.xml は hopmappy を C30 のみの wsg63701 と書くが、
		   MAME namcos86 hopmappy_mcu_map は YM2151 @$2000 + CUS30。 */
		|| (ge->archive && ge->archive[0] && _stricmp(ge->archive, "hopmappy") == 0))
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
	else if (CEmuAcIsPengo(ge))
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
		|| _stricmp(ge->subtype, "054539x2") == 0
		|| CEmuAcIsKonamiK053260Sub(ge->subtype)
		|| CEmuAcIsKonamiK054539Sub(ge->subtype)
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
		/* Capcom 前期 CPS YM2203 基板（MAME gng/srumbler/tigeroad/commando） */
		|| _stricmp(ge->subtype, "tigerroad") == 0 || _stricmp(ge->subtype, "tigeroad") == 0
		|| _stricmp(ge->subtype, "rushcrsh") == 0 || _stricmp(ge->subtype, "srumbler") == 0
		|| _stricmp(ge->subtype, "commando") == 0 || _stricmp(ge->subtype, "sectionz") == 0
		|| _stricmp(ge->subtype, "trojan") == 0
		|| _stricmp(ge->subtype, "exedexes") == 0 || _stricmp(ge->subtype, "gunsmoke") == 0
		|| _stricmp(ge->subtype, "blktiger") == 0 || _stricmp(ge->subtype, "blacktiger") == 0
		|| _stricmp(ge->subtype, "sidearms") == 0
		/* Tecmo YM2203×2（Ninja Gaiden / Shadow Warriors）。Gemini Wing / Silkworm は YM3812+MSM5205 — この YM2203 マップを取ってはいけない。 */
		|| _stricmp(ge->subtype, "gaiden") == 0
		/* Konami ironhors/scotrsht: Z80+YM2203 I/O 00/01、ラッチ 8000。AY timeplt ではない。 */
		|| _stricmp(ge->subtype, "ironhors") == 0
		/* Jaleco momoko: Z80+YM2203×2 @A000/C000、ラッチは YM2 ポートA。Taito OPM ではない。 */
		|| _stricmp(ge->subtype, "momoko") == 0
		|| _stricmp(ge->subtype, "exerizer") == 0
		|| CEmuAcIsJumping(ge))
		board = CEMU_AC_BOARD_GNG;
	else if (_stricmp(ge->subtype, "aburner") == 0
		/* Sega X-Board / G-LOC: After Burner と同じ Z80+YM2151+SegaPCM 級 */
		|| _stricmp(ge->subtype, "gforce") == 0
		|| CEmuAcIsSmgp(ge) || CEmuAcIsLastsurv(ge))
		board = CEMU_AC_BOARD_ABURNER;
	else if (_stricmp(ge->subtype, "outrun") == 0 || _stricmp(ge->subtype, "toutrun") == 0
		|| _stricmp(ge->subtype, "shangon") == 0)
		board = CEMU_AC_BOARD_OUTRUN;
	else if (_stricmp(ge->subtype, "sharrier") == 0 || _stricmp(ge->subtype, "hangon") == 0
		|| CEmuAcIsEnduroBl(ge) || CEmuAcIsEnduroB2(ge))
		board = CEMU_AC_BOARD_HANGON;
		else if (_stricmp(ge->subtype, "aerofgt") == 0 || _stricmp(ge->platform, "videosystem") == 0
		|| _stricmp(ge->subtype, "gunbird") == 0
		|| (_stricmp(ge->platform, "psikyo") == 0
			&& ((_stricmp(ge->subtype, "sengoku") == 0)
				|| (ge->archive[0]
					&& (_stricmp(ge->archive, "samuraia") == 0
						|| _stricmp(ge->archive, "sngkace") == 0
						|| _stricmp(ge->archive, "sngkacea") == 0
						|| _stricmp(ge->archive, "samuraiak") == 0)))))
		board = CEMU_AC_BOARD_VSYSTEM;
	else if (CEmuAcIsMasao(ge))
		board = CEMU_AC_BOARD_TAITO_SJ;
	else if (CEmuAcIsMegazone(ge))
		board = CEMU_AC_BOARD_KONAMI_TIMEPLT;
	else if (CEmuAcIsTaitoYm2610Sub(ge->subtype)
		|| (_stricmp(ge->platform, "taito") == 0 && CEmuAcHasChip(ge, CEMU_CHIP_YM2610)))
		board = CEMU_AC_BOARD_TAITO_YM2610;
	else if (CEmuAcIsTaitoOpmSub(ge->subtype)
		|| CEmuAcIsTaitoYm2203Sub(ge->subtype))
		board = CEMU_AC_BOARD_TAITO_OPM;
	else if (_stricmp(ge->subtype, "taitosj") == 0
		|| _stricmp(ge->subtype, "bankp") == 0
		|| _stricmp(ge->subtype, "combh") == 0
		|| _stricmp(ge->subtype, "gberet") == 0
		|| _stricmp(ge->subtype, "higemaru") == 0)
		board = CEMU_AC_BOARD_TAITO_SJ;
	else if (_stricmp(ge->subtype, "scramble") == 0
		|| _stricmp(ge->subtype, "scobra") == 0
		|| _stricmp(ge->subtype, "frogger") == 0)
		board = CEMU_AC_BOARD_KONAMI_SCRAMBLE;
	else if (_stricmp(ge->subtype, "timeplt") == 0
		|| _stricmp(ge->subtype, "pooyan") == 0
		|| _stricmp(ge->subtype, "locomotn") == 0
		|| _stricmp(ge->subtype, "jungler") == 0)
		board = CEMU_AC_BOARD_KONAMI_TIMEPLT;
	else if (_stricmp(ge->subtype, "gx400") == 0
		|| _stricmp(ge->subtype, "nemesis") == 0)
		board = CEMU_AC_BOARD_KONAMI_GX400;
	else if (CEmuAcIsMatmania(ge))
		board = CEMU_AC_BOARD_DECO;
	else if (CEmuAcIsKonamiK7232Sub(ge->subtype))
		board = CEMU_AC_BOARD_KONAMI_K7232;
	else if (_stricmp(ge->subtype, "68k2") == 0
		|| _stricmp(ge->subtype, "mmpanic") == 0
		|| (ge->archive[0] && (_stricmp(ge->archive, "mmpanic") == 0
			|| _stricmp(ge->archive, "animaljr") == 0
			|| _stricmp(ge->archive, "funkyfig") == 0)))
		board = CEMU_AC_BOARD_ALPHA68K2;
	else if (CEmuAcIsSpy(ge) || _stricmp(ge->subtype, "hcastle") == 0)
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
			|| _stricmp(ge->subtype, "cabal") == 0))
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
		/* 古典 Toaplan1 Z80+YM3812（共有 RAM メールボックス）。MCU/Toaplan2 サブタイプ（tp/vimana/fireshrk/…）は未マップのまま。 */
		board = CEMU_AC_BOARD_TOAPLAN1;
	else if (CEmuAcIsNslasherZ80(ge))
		/* MAME deco32.cpp nslasher: Z80 + YM2151 + OKI×2。US nslasheru は HuC6280 なのでここへ来ない。 */
		board = CEMU_AC_BOARD_TECMO16;
	else if (CEmuAcIsDecoSub(ge->subtype))
		/* HuC6280 Data East 基板だけ（cninja/thndzone/deco32/…）。古い AY 基板（btime/disco）は UNKNOWN のまま — ソフト SILENT であり FAIL_OPEN ではない。 */
		board = CEMU_AC_BOARD_DECO;
	else if (_strnicmp(ge->platform, "capcom", 6) == 0
		&& (_stricmp(ge->subtype, "gng") == 0 || _stricmp(ge->subtype, "opn2") == 0))
		board = CEMU_AC_BOARD_GNG;
	else if (_strnicmp(ge->platform, "capcom", 6) == 0
		&& (_strnicmp(ge->subtype, "cps1", 4) == 0 || ge->subtype[0] == 0))
		board = CEMU_AC_BOARD_CPS1;
	if (board == CEMU_AC_BOARD_UNKNOWN)
		board = CEmuAcAliasBoard(ge->subtype);
	return board;
}

/* Slap Fight ハード（tigerh / alcon / getstar）: AY×2 @ A080/A090、共有コマンド @C800、周期 NMI — Truxton YM3812 メールボックスではない。 */
static int CEmuAcIsSlapfght(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype[0]
		&& (_stricmp(ge->subtype, "tigerh") == 0
			|| _stricmp(ge->subtype, "slapfght") == 0))
		return 1;
	if (ge->archive
		&& (_stricmp(ge->archive, "tigerh") == 0
			|| _stricmp(ge->archive, "alcon") == 0
			|| _stricmp(ge->archive, "getstar") == 0
			|| _stricmp(ge->archive, "slapfght") == 0
			|| _stricmp(ge->archive, "slapfigh") == 0))
		return 1;
	return 0;
}

/* MAME slapfght.cpp perfrman: AY×2 メモリマップは tigerh と同じ A080/A090。共有 RAM は 8800、Z80/AY 2 MHz、NMI 240 Hz。 */
static int CEmuAcIsPerfrman(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (ge->subtype[0] && _stricmp(ge->subtype, "perfrman") == 0)
		return 1;
	if (ge->archive
		&& (_stricmp(ge->archive, "perfrman") == 0
			|| _stricmp(ge->archive, "perfrmanu") == 0))
		return 1;
	return 0;
}

/* チップと CPU を生成する */
int CHardAc::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsAcPlatform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	board_ = CEmuAcResolveBoard(ge);
	if (CEmuAcIsHopmappy(ge))
		board_ = CEMU_AC_BOARD_NAMCO_SYS86;
	if (CEmuAcIsSmgp(ge) || CEmuAcIsLastsurv(ge))
		board_ = CEMU_AC_BOARD_ABURNER;
	const int hasOpm = CEmuAcHasChip(ge, CEMU_CHIP_OPM);
	const int hasSegaPcm = CEmuAcHasChip(ge, CEMU_CHIP_SEGAPCM);
	const int hasK054539 = CEmuAcHasChip(ge, CEMU_CHIP_K054539);

	qsZn_ = (_stricmp(ge->subtype, "zn") == 0) ? 1 : 0;

	pcmKind_ = 0;
	auxKind_ = 0;
	m37702Soft_ = 0;
	m37702C140_ = 0;
	m37702MapKind_ = 0;
	m37702MaskRom_ = 0;
	m37702McuKind_ = 0;
	snkMapKind_ = 0;
	snkStatus_ = 0;
	terracreMap_ = 0;
	tecmoOpl_ = 0;
	m68kPcmKind_ = 0;
	m68kPcmAddr_ = 0;
	m68kPcmSpan_ = 0;
	m68kRamAddr_ = 0;
	m68kRamSize_ = 0;
	m68kIrqAddr_ = 0;
	ms1RamAlloc_ = 0;
	memset(m68kHiWord_, 0, sizeof(m68kHiWord_));
	m68kVblankAcc_ = 0;
	m68kVblankLevel_ = 0;
	m68kVblankPending_ = 0;
	flstoryNmiEn_ = 0;
	toaplanKaneko_ = 0;
	segaM1Audio_ = 0;
	chip3_ = NULL;
	mainIsYm2203_ = 0;
	mainIsYm2610_ = 0;
	mainIsYm2612_ = 0;
	wsgMappy_ = 0;
	wsg63701_ = 0;
	gngCommandoMap_ = 0;
	gngGaidenMap_ = 0;
	taitoOpmMap_ = 0;
	konamiPcmWindow_ = 0x40u;
	konamiPcm2Addr_ = 0u;
	konamiSoundCtrl_ = 0;
	sys16RomBoard_ = (board_ == CEMU_AC_BOARD_SYS16A) ? 0x5358u : 0x5797u;
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && CEmuAcIsPengo(ge))
		sys16RomBoard_ = 0x5047u;

	if (board_ == CEMU_AC_BOARD_GNG) {
		/* Commando（と類似）: RAM 4000、ラッチ 6000、YM 8000 — GNG の C000/C800/E000 マップではない。ExedExes は同じデコード上の AY+SN。それでも開く。 */
		if (_stricmp(ge->subtype, "commando") == 0
			|| _stricmp(ge->subtype, "exedexes") == 0)
			gngCommandoMap_ = 1;
		else if (_stricmp(ge->subtype, "sidearms") == 0
			|| (ge->archive[0] && _stricmp(ge->archive, "sidearms") == 0))
			gngCommandoMap_ = 2;
		else if (_stricmp(ge->subtype, "tigerroad") == 0
			|| _stricmp(ge->subtype, "tigeroad") == 0
			|| _stricmp(ge->subtype, "rushcrsh") == 0
			|| (ge->archive[0] && (_stricmp(ge->archive, "tigeroad") == 0
				|| _stricmp(ge->archive, "f1dream") == 0
				|| _stricmp(ge->archive, "srumbler") == 0
				|| _stricmp(ge->archive, "rushcrsh") == 0)))
			gngCommandoMap_ = 3;
		else if (_stricmp(ge->subtype, "ironhors") == 0
			|| (ge->archive[0] && (_stricmp(ge->archive, "ironhors") == 0
				|| _stricmp(ge->archive, "scotrsht") == 0)))
			gngCommandoMap_ = 4;
		else if (_stricmp(ge->subtype, "momoko") == 0
			|| _stricmp(ge->subtype, "exerizer") == 0
			|| (ge->archive[0] && (_stricmp(ge->archive, "momoko") == 0
				|| _stricmp(ge->archive, "skyfox") == 0
				|| _stricmp(ge->archive, "exerizer") == 0)))
			gngCommandoMap_ = 5;
		else if (CEmuAcIsJumping(ge))
			gngCommandoMap_ = 6;
		/* Tecmo gaiden/shadoww: YM2203×2 @ F810/F820 + OKI @ F800、ラッチ NMI */
		else if (_stricmp(ge->subtype, "gaiden") == 0)
			gngGaidenMap_ = 1;
		if (gngGaidenMap_) {
			/* MAME tecmo/gaiden: Z80+YM2203×2 @ 4 MHz、OKI6295、ラッチ→NMI */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0; /* chip2 OPN — Render は Chip2 経由で加算 */
			pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
			pcmKind_ = 2;
		} else if (gngCommandoMap_ == 2) {
			/* MAME capcom/sidearms: Z80+YM2203×2 @ 16/4 = 4 MHz。ラッチ D000 poll。YM1 IRQ → IRQ0。 */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0x8000u;
		} else if (gngCommandoMap_ == 3) {
			/* MAME capcom/tigeroad: YM1 8000、YM2 A000、RAM C000、ラッチ E000 poll。
			   srumbler は同じ窓、クロック 16/4=4 MHz。 */
			const int srum = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "rushcrsh") == 0)
				|| (ge->archive[0] && (_stricmp(ge->archive, "srumbler") == 0
					|| _stricmp(ge->archive, "rushcrsh") == 0)))) ? 1 : 0;
			cpuHz_ = srum ? 4000000 : 3579545;
			opmHz_ = cpuHz_;
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (gngCommandoMap_ == 4) {
			/* MAME konami/ironhors: Z80+YM2203 @ 18.432/6 = 3.072 MHz。RAM 4000、ラッチ 8000、YM I/O 00/01。 */
			cpuHz_ = 3072000;
			opmHz_ = 3072000;
			chip_ = CEmuChipYm2608Create(3072000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			if (chip_)
				chip_->SetTimerIrqPolicy(0);
		} else if (gngCommandoMap_ == 5) {
			/* MAME jaleco/momoko: Z80 @ 2.5 MHz、YM2203×2 @ 1.25 MHz。RAM 8000、YM A000/C000。
			   ラッチは YM2 ポートA。IRQ 無し（メインループが poll）。
			   skyfox/exerizer は同じ窓だがラッチは B000、Z80/YM は 14.31818/8。 */
			const int sky = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "exerizer") == 0)
				|| (ge->archive[0] && (_stricmp(ge->archive, "skyfox") == 0
					|| _stricmp(ge->archive, "exerizer") == 0)))) ? 1 : 0;
			cpuHz_ = sky ? 1789772 : 2500000;
			opmHz_ = sky ? 1789772 : 1250000;
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			if (chip_)
				chip_->SetTimerIrqPolicy(0);
			if (chip2_)
				chip2_->SetTimerIrqPolicy(0);
		} else if (gngCommandoMap_ == 6) {
			/* MAME rbisland jumping: Z80 24/4=6 MHz、YM2203×2 24/8=3 MHz。ラッチ IRQ0。YM irq_handler 無し。 */
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm2608Create(3000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(3000000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			if (chip_)
				chip_->SetTimerIrqPolicy(0);
			if (chip2_)
				chip2_->SetTimerIrqPolicy(0);
		} else {
			/* MAME: Z80 @ 3 MHz、YM2203×2 @ 1.5 MHz。MVP: E000 と E002 で共有する OPN 1 基（まだ可聴）。 */
			cpuHz_ = 3000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			chip2_ = NULL;
		}
	} else if (board_ == CEMU_AC_BOARD_HANGON) {
		cpuHz_ = 4000000;
		/* YM2203 の音色生成とタイマは物理 4 MHz マスタを共有。同じ領域でチップを tick。AdvanceClocks だけ半減するとピッチは正しいが Timer B とシーケンサが半速になった。 */
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
		mainIsYm2203_ = 1;
		/* 音楽テンポ = Timer B IRQ。Timer A だと ISR レートがおおよそ倍 */
		if (chip_)
			chip_->SetTimerIrqPolicy(0);
		vsIoKind_ = CEmuAcIsEnduroB2(ge) ? 1 : 0;
		if (vsIoKind_ == 1) {
			/* MAME endurob2: sound_map_2151 + portmap_2203x2。PCM は 315-5218 BANK_512（OutRun 級）。 */
			chip2_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			if (chip2_)
				chip2_->SetTimerIrqPolicy(0);
			pcm_ = CEmuChipSegaPcmCreate(4000000u, sampleRate_, 12u, 0x70u);
		} else {
			chip2_ = NULL;
			/* MAME マスタ: SEGAPCM_DISCRETE(..., 8_MHz_XTAL / 2) → 4 MHz。 */
			pcm_ = CEmuChipSegaPcmCreateDiscrete(4000000u, sampleRate_);
		}
		pcmKind_ = 1;
	} else if (board_ == CEMU_AC_BOARD_SYS18) {
		cpuHz_ = 8000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		mainIsYm2612_ = 1;
		/* MAME system18: YM3438 2 基。第 2 チップはミラーではなく本物 OPN2 */
		chip2_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		auxKind_ = 5;
		pcm_ = CEmuChipRf5c68Create(10000000u, sampleRate_);
		pcmKind_ = 5;
		if (CEmuAcIsNewufo(ge)) {
			/* MAME segaufo: Z80 8 MHz + YM3438 I/O 40-43。RF5C68 / 2 本目 YM は無い。 */
			vsIoKind_ = 1;
			if (chip2_) {
				CEmuChipYm2612Destroy(chip2_);
				chip2_ = NULL;
			}
			auxKind_ = 0;
			if (pcm_) {
				CEmuChipRf5c68Destroy(pcm_);
				pcm_ = NULL;
			}
			pcmKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			bankLoaded_ = 1;
		}
	} else if (board_ == CEMU_AC_BOARD_SYS24) {
		/* 68000×2 + YM2151（Z80 / RF5C68 無し）。イメージから sound_addr / irq_addr を載せる Musashi ホスト経路ができるまでディスクはソフト open のみ。 */
		cpuHz_ = 10000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_VSYSTEM) {
		/* MAME vsystem/aerofgt: Z80 20/4 = 5 MHz、YM2610 8 MHz（両方 PCB 確認）。音源 ROM は 128K を 8000-FFFF に 32K 窓 4 つでバンク。fromanc2 は Z80 を 8 MHz。BGM には 5 MHz で十分近い。Psikyo gunbird: Z80+YM2610 @ 8 MHz、I/O YM@04、ラッチ@08（vsIoKind 3）。
		   Psikyo sngkace/samuraia: Z80 32/8 = 4 MHz、YM2610 32/4 = 8 MHz。RAM 7800、I/O YM@00、バンク@04、ラッチ@08、ack@0c（vsIoKind 6）。 */
		const int psikyo = (_stricmp(ge->subtype, "gunbird") == 0) ? 1 : 0;
		const int sngkace = (_stricmp(ge->platform, "psikyo") == 0
			&& ((_stricmp(ge->subtype, "sengoku") == 0)
				|| (ge->archive[0]
					&& (_stricmp(ge->archive, "samuraia") == 0
						|| _stricmp(ge->archive, "sngkace") == 0
						|| _stricmp(ge->archive, "sngkacea") == 0
						|| _stricmp(ge->archive, "samuraiak") == 0)))) ? 1 : 0;
		cpuHz_ = psikyo ? 8000000 : (sngkace ? 4000000 : 5000000);
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2610Create(8000000u, sampleRate_);
		mainIsYm2610_ = 1;
		chip2_ = NULL;
		bankBase_ = 0x8000u;
		bankSize_ = 0x8000u;
		vsIoKind_ = 0;
		if (psikyo)
			vsIoKind_ = 3;
		else if (sngkace)
			vsIoKind_ = 6;
		else if (_stricmp(ge->subtype, "turbofrc") == 0
			|| _stricmp(ge->subtype, "f1gp") == 0
			|| _stricmp(ge->subtype, "spinlbrk") == 0
			|| _stricmp(ge->archive, "pspikes") == 0
			|| _stricmp(ge->archive, "karatblz") == 0
			|| _stricmp(ge->archive, "spinlbrk") == 0
			|| _stricmp(ge->archive, "turbofrc") == 0
			|| _stricmp(ge->archive, "f1gp") == 0
			|| _stricmp(ge->archive, "f1gp2") == 0)
			vsIoKind_ = 1;
		else if (_stricmp(ge->subtype, "pipedrm") == 0
			|| _stricmp(ge->archive, "pipedrm") == 0) {
			vsIoKind_ = 4;
			cpuHz_ = 3579545;
		}
		else if (_stricmp(ge->subtype, "fromanc2") == 0
			|| _stricmp(ge->subtype, "fromanc4") == 0
			|| _stricmp(ge->archive, "fromanc2") == 0
			|| _stricmp(ge->archive, "fromanc4") == 0
			|| _stricmp(ge->archive, "fromancr") == 0) {
			vsIoKind_ = 2;
			cpuHz_ = 8000000;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (_stricmp(ge->subtype, "welltris") == 0
			|| _stricmp(ge->archive, "welltris") == 0
			|| _stricmp(ge->archive, "quiz18k") == 0) {
			vsIoKind_ = 5;
			cpuHz_ = 4000000;
		}
	} else if (board_ == CEMU_AC_BOARD_TAITO_YM2610) {
		/* MAME taito_f2 のクロック: Z80 24/6 = 4 MHz、YM2610 24/3 = 8 MHz */
		cpuHz_ = 4000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2610Create(8000000u, sampleRate_);
		mainIsYm2610_ = 1;
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_TAITO_OPM) {
		/* MAME taito_rastan / taito_asuka: Z80 4 MHz、YM2151 4 MHz。masterw/viofight（Taito B YM2203）: 同じ PC060HA マップ @9000/A000 だが OPN @ 3 MHz。viofight は OKI @B000 も。darius: YM2203×2 @9000/A000、PC060HA @B000、Z80/YM 4 MHz。kikikai: YM2203 @C000、共有 RAM メールボックス 9FFF、vblank IRQ。 */
		const int ym2203 = CEmuAcIsTaitoYm2203Sub(ge->subtype);
		const int darius = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "darius") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "darius") == 0))) ? 1 : 0;
		const int kikikai = (ge && ((ge->subtype[0] && (_stricmp(ge->subtype, "kikikai") == 0
				|| _stricmp(ge->subtype, "kicknrun") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "kikikaik") == 0
				|| _stricmp(ge->archive, "kikikai") == 0
				|| _stricmp(ge->archive, "kicknrun") == 0)))) ? 1 : 0;
		const int tokio = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "tokio") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "tokio") == 0))) ? 1 : 0;
		const int bublbobl = (!tokio && ge
			&& ((ge->subtype[0] && _stricmp(ge->subtype, "bubblebobble") == 0)
				|| (ge->archive[0] && _strnicmp(ge->archive, "bublbobl", 8) == 0))) ? 1 : 0;
		const int lsasquad = (ge && ((ge->subtype[0] && (_stricmp(ge->subtype, "lsasquad") == 0
				|| _stricmp(ge->subtype, "daikaiju") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "lsasquad") == 0
				|| _stricmp(ge->archive, "daikaiju") == 0)))) ? 1 : 0;
		const int lkage = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "kage") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "lkage") == 0))) ? 1 : 0;
		/* 旧 TNZS 基板（tnzs_mcu / kageki / chukatai）: SUB Z80 上の YM2203 @B000、RAM D000、共有 E000。カタログアーカイブは tnzsjo であり tnzsb ではない。 */
		const int tnzsOld = (ge && (
			(ge->archive[0] && (_stricmp(ge->archive, "tnzsjo") == 0
				|| _stricmp(ge->archive, "tnzso") == 0
				|| _stricmp(ge->archive, "kageki") == 0
				|| _stricmp(ge->archive, "chukatai") == 0
				|| _stricmp(ge->archive, "extrmatn") == 0
				|| _stricmp(ge->archive, "insectx") == 0
				|| _stricmp(ge->archive, "plumppop") == 0))
			|| (ge->subtype[0] && (_stricmp(ge->subtype, "kageki") == 0
				|| _stricmp(ge->subtype, "chukatai") == 0
				|| _stricmp(ge->subtype, "extrmatn") == 0
				|| _stricmp(ge->subtype, "insectx") == 0
				|| _stricmp(ge->subtype, "plumppop") == 0)))) ? 1 : 0;
		const int ashnojoe = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "ashnojoe") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "ashnojoe") == 0
				|| _stricmp(ge->archive, "scessjoe") == 0)))) ? 1 : 0;
		const int daisenpu = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "daisenpuu") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "twinhawk") == 0
				|| _stricmp(ge->archive, "daisenpu") == 0
				|| _stricmp(ge->archive, "daisenpuu") == 0)))) ? 1 : 0;
		const int cadashFam = (ge && ge->archive[0] && (_stricmp(ge->archive, "cadash") == 0
			|| _stricmp(ge->archive, "earthjkr") == 0
			|| _stricmp(ge->archive, "galmedes") == 0
			|| _stricmp(ge->archive, "topspeed") == 0)) ? 1 : 0;
		const int lomakai = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "lomakai") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "lomakai") == 0
				|| _stricmp(ge->archive, "makaiden") == 0)))) ? 1 : 0;
		const int fhawk = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "fhawk") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "fhawk") == 0))) ? 1 : 0;
		const int kurikint = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "kurikint") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "kurikint") == 0))) ? 1 : 0;
		const int taitoL1 = (ge && ((ge->subtype[0] && (_stricmp(ge->subtype, "palamed") == 0
				|| _stricmp(ge->subtype, "cachat") == 0
				|| _stricmp(ge->subtype, "horshoes") == 0
				|| _stricmp(ge->subtype, "flipull") == 0
				|| _stricmp(ge->subtype, "plotting") == 0
				|| _stricmp(ge->subtype, "puzznic") == 0
				|| _stricmp(ge->subtype, "cubybop") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "palamed") == 0
				|| _stricmp(ge->archive, "cachat") == 0
				|| _stricmp(ge->archive, "horshoes") == 0
				|| _stricmp(ge->archive, "flipull") == 0
				|| _stricmp(ge->archive, "tubeit") == 0
				|| _stricmp(ge->archive, "cubybop") == 0
				|| _stricmp(ge->archive, "plotting") == 0
				|| _stricmp(ge->archive, "puzznic") == 0)))) ? 1 : 0;
		const int volfied = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "volfied") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "volfied") == 0))) ? 1 : 0;
		const int arkanoid = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "arkanoid") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "arkanoid") == 0
				|| _stricmp(ge->archive, "arknoidu") == 0
				|| _stricmp(ge->archive, "arknoidj") == 0
				|| _stricmp(ge->archive, "arkbl2") == 0)))) ? 1 : 0;
		const int kabukiz = CEmuAcIsKabukiz(ge);
		const int streetf = CEmuAcIsStreetFighter(ge);
		taitoOpmMap_ = ashnojoe ? 8 : (daisenpu ? 9 : (cadashFam ? 10 : (lomakai ? 11 : (darius ? 1 : (kikikai ? 2 : (tokio ? 3 : (bublbobl ? 4
			: (lsasquad ? 5 : (lkage ? 6 : (tnzsOld ? 7 : 0))))))))));
		if (fhawk) taitoOpmMap_ = 12;
		else if (kurikint) taitoOpmMap_ = 13;
		else if (taitoL1) taitoOpmMap_ = 14;
		else if (volfied) taitoOpmMap_ = 15;
		else if (arkanoid) taitoOpmMap_ = 16;
		else if (kabukiz) taitoOpmMap_ = 17;
		else if (streetf) taitoOpmMap_ = 18;
		/* 0 rastan/asuka OPM、1 darius OPN×2、2 kikikai/kicknrun YM2203 @C000 共有 RAM、3 tokio、4 bublbobl YM2203+YM3526、5 lsasquad YM2203+AY、6 lkage YM2203×2（bublbobl マップ、YM2203 @A000）、7 旧 TNZS YM2203 @B000（PC060HA 無し）、8 ashnojoe YM2203 I/O、9 twinhawk/daisenpu YM2151 @E000 PC060HA @E200 RAM@C000、10 cadash/earthjkr/galmedes/topspeed（asuka マップ、ISR は Timer A bit0）、11 lomakai Mega System 1-Z YM2203 I/O、12 fhawk YM2203 @F000 PC060HA @E000、13 kurikint YM2203 @E800 DPRAM @E000、14 Taito L 1cpu YM2203 @A000 TC0090LVC、15 volfied YM2203 @9000 PC060HA @8800 RAM 8000-87FF、16 arkanoid Z80+YM2149 @D000 MCU stub D018、67AE、E995 は MCU 解除、17 kabukiz 第3 Z80 YM2203 I/O 00-01 ラッチ 02、18 Street Fighter 1 YM2151 @E000 ラッチ C800 NMI。 */
		/* MAME masterw: Z80B @ 24/4 = 6 MHz、YM2203 @ 24/8 = 3 MHz。darius/lkage: Z80 と YM2203 は 4 MHz。tokio/bublbobl/lsasquad: Z80+YM @ 24/8 = 3 MHz（bublbobl は YM3526 も）。ashnojoe: Z80/YM 8/2=4 MHz。lomakai: Z80 3 MHz、YM2203 1.5 MHz。fhawk/kurikint: Z80 12/3=4 MHz、YM2203 12/4=3 MHz。Taito L 1cpu: TC0090LVC 13.33056/2、YM2203 /4。 */
		cpuHz_ = (taitoOpmMap_ == 16) ? 6000000
			: ((taitoOpmMap_ == 15) ? 4000000
			: ((taitoOpmMap_ == 14) ? 6665280
			: ((taitoOpmMap_ == 12 || taitoOpmMap_ == 13) ? 4000000
			: ((taitoOpmMap_ == 8) ? 4000000
			: ((taitoOpmMap_ == 11) ? 3000000
			: ((taitoOpmMap_ == 3 || taitoOpmMap_ == 4 || taitoOpmMap_ == 5) ? 3000000
			: ((darius || taitoOpmMap_ == 6) ? 4000000 : ((taitoOpmMap_ == 17 || ym2203) ? 6000000 : 4000000))))))));
		opmHz_ = (taitoOpmMap_ == 16) ? 3000000
			: ((taitoOpmMap_ == 15) ? 4000000
			: ((taitoOpmMap_ == 14) ? 3332640
			: ((taitoOpmMap_ == 12 || taitoOpmMap_ == 13) ? 3000000
			: ((taitoOpmMap_ == 8) ? 4000000
			: ((taitoOpmMap_ == 11) ? 1500000
			: ((taitoOpmMap_ == 3 || taitoOpmMap_ == 4 || taitoOpmMap_ == 5) ? 3000000
			: ((darius || taitoOpmMap_ == 6) ? 4000000 : ((taitoOpmMap_ == 17 || ym2203) ? 3000000 : 4000000))))))));
		if (taitoOpmMap_ == 18) {
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
		}
		if (taitoOpmMap_ == 16) {
			/* MAME arkanoid: YM2149 12/4=3 MHz、pin26 Low は CEmuChipAy の clock/2。 */
			chip_ = CEmuChipAyCreate(3000000u, sampleRate_);
			mainIsYm2203_ = 0;
			chip2_ = NULL;
			auxKind_ = 0;
			pcm_ = NULL;
			pcmKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0x10000u;
			bankLoaded_ = 1;
		} else if (ym2203 || taitoOpmMap_ == 11 || taitoOpmMap_ == 17) {
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			if (_stricmp(ge->subtype, "viofight") == 0) {
				pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
				pcmKind_ = 2;
			}
			if (darius) {
				chip2_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
				auxKind_ = 0;
			} else if (taitoOpmMap_ == 4) {
				chip2_ = CEmuChipYm3812Create((uint32_t)opmHz_, sampleRate_);
				auxKind_ = 3; /* YM3526 は OPL2 コア経由 */
			} else if (taitoOpmMap_ == 5) {
				/* MAME lsasquad: YM2203 @A000 + AY/YM2149 @C000 の配置 */
				chip2_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
				auxKind_ = 2;
			} else if (taitoOpmMap_ == 6) {
				/* MAME lkage: YM2203×2 @9000 / @A000（bublbobl マップ） */
				chip2_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN */, sampleRate_);
				auxKind_ = 0;
			} else {
				chip2_ = NULL;
			}
		} else {
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			chip2_ = NULL;
		}
		if (taitoOpmMap_ != 16) {
			bankBase_ = 0x4000u;
			bankSize_ = 0x4000u;
			if (taitoOpmMap_ == 18) {
				/* MAME sf sound_map: 線形 32K ROM 0000-7FFF、RAM C000。バンク無し。 */
				bankBase_ = 0;
				bankSize_ = 0;
				bankLoaded_ = 1;
			} else if (kikikai || taitoOpmMap_ == 3 || taitoOpmMap_ == 4
				|| taitoOpmMap_ == 5 || taitoOpmMap_ == 6 || taitoOpmMap_ == 7
				|| taitoOpmMap_ == 8 || taitoOpmMap_ == 11 || taitoOpmMap_ == 13
				|| taitoOpmMap_ == 14 || taitoOpmMap_ == 15 || taitoOpmMap_ == 17) {
				/* 0000-7FFF の線形 32K。SetBank(0) は ROM[0:4000] を 4000-7FFF へ blit しブートチェックサムが 007C でハング。ROM+0x8000 から 8000 に 8K バンク 7 本。16K blit は飛ばす。 */
				bankBase_ = 0;
				bankSize_ = 0x8000u;
				if (taitoOpmMap_ == 7 || taitoOpmMap_ == 12 || taitoOpmMap_ == 13
					|| taitoOpmMap_ == 14 || taitoOpmMap_ == 15 || taitoOpmMap_ == 17)
					bankLoaded_ = 1;
			}
		}
	} else if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		/* MAME sega_system1 のクロック: SOUND_CLOCK 8 MHz。Z80 /2、SN1 /4、SN2 /2。
		   Konami trackfld 族は同じ SN 区画だがラッチ／ストローブ番地が違う（vsIoKind 1..3）。 */
		int tf = 0;
		int systeme = (ge && ge->subtype[0] && _stricmp(ge->subtype, "systeme") == 0) ? 1 : 0;
		if (ge) {
			const char* sub = ge->subtype;
			const char* arc = ge->archive;
			if ((sub && _stricmp(sub, "trackfld") == 0)
				|| (arc && (_stricmp(arc, "trackfld") == 0
					|| _stricmp(arc, "hyprolym") == 0
					|| _stricmp(arc, "hyprolyb") == 0
					|| _stricmp(arc, "reaktor") == 0)))
				tf = 1;
			else if ((sub && (_stricmp(sub, "hyperspt") == 0
					|| _stricmp(sub, "sbasketb") == 0))
				|| (arc && (_stricmp(arc, "hyperspt") == 0
					|| _stricmp(arc, "sbasketb") == 0
					|| _stricmp(arc, "roadf") == 0)))
				tf = 2;
			else if ((sub && _stricmp(sub, "mikie") == 0)
				|| (arc && _stricmp(arc, "mikie") == 0))
				tf = 3;
		}
		if (systeme) {
			/* MAME seage.cpp: Z80 10.738635/2、SN76496×2 @ /3。I/O PSG。 */
			vsIoKind_ = 4;
			cpuHz_ = 5369317;
			opmHz_ = 3579545;
			chip_ = CEmuChipSn76489Create(3579545u, sampleRate_);
			chip2_ = CEmuChipSn76489Create(3579545u, sampleRate_);
			auxKind_ = 1;
			bankBase_ = 0;
			bankSize_ = 0;
			bankLoaded_ = 1;
		} else if (tf) {
			vsIoKind_ = tf;
			cpuHz_ = 3579545;
			opmHz_ = 1789772;
			chip_ = CEmuChipSn76489Create(1789772u, sampleRate_);
			chip2_ = (tf == 3) ? CEmuChipSn76489Create(3579545u, sampleRate_) : NULL;
			auxKind_ = 1;
			bankBase_ = 0;
			bankSize_ = 0;
			bankLoaded_ = 1;
			ms1LatchIn_ = 0;
		} else {
			vsIoKind_ = 0;
			cpuHz_ = 4000000;
			opmHz_ = 2000000;
			chip_ = CEmuChipSn76489Create(2000000u, sampleRate_);
			chip2_ = CEmuChipSn76489Create(4000000u, sampleRate_);
			auxKind_ = 1;
		}
	} else if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		/* MAME taito_taitosj のクロック: Z80 12/4 = 3 MHz、AY-3-8910 12/8 = 1.5 MHz。MAME cop01: Z80 20/8 = 2.5 MHz、AY×3 I/O 20/16 = 1.25 MHz。 */
		const int cop01 = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "cop01") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "cop01") == 0))) ? 1 : 0;
		const int magmax = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "magmax") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "magmax") == 0))) ? 1 : 0;
		const int bombjack = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "bombjack") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "bombjack") == 0))) ? 1 : 0;
		const int calorie = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "calorie") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "calorie") == 0))) ? 1 : 0;
		const int solomon = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "solomon") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "solomon") == 0))) ? 1 : 0;
		const int halleys = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "halleysc") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "halleys") == 0
				|| _stricmp(ge->archive, "benberob") == 0)))) ? 1 : 0;
		const int pbaction = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "pbaction") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "pbaction") == 0))) ? 1 : 0;
		const int chaknpop = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "chaknpop") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "chaknpop") == 0))) ? 1 : 0;
		/* Tehkan swimmer / guzzler: 同一 Z80+AY×2 マップ。サブタイプ swimmer。 */
		const int swimmer = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "swimmer") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "swimmer") == 0
				|| _stricmp(ge->archive, "guzzler") == 0)))) ? 1 : 0;
		const int tubep = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "tubep") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "tubep") == 0))) ? 1 : 0;
		const int retofinv = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "retofinv") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "retofinv") == 0))) ? 1 : 0;
		const int ikki = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "ikki") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "ikki") == 0))) ? 1 : 0;
		const int circusc = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "circusc") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "circusc") == 0))) ? 1 : 0;
		const int starforce = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "starforce") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "starforc") == 0
				|| _stricmp(ge->archive, "senjyo") == 0
				|| _stricmp(ge->archive, "baluba") == 0
				|| _stricmp(ge->archive, "megaforc") == 0)))) ? 1 : 0;
		/* Tehkan World Cup / Gridiron Fight: 16K Z80 + AY×2 + MSM5205（BGM は AY） */
		const int worldcup = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "worldcup") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "tehkanwc") == 0
				|| _stricmp(ge->archive, "gridiron") == 0
				|| _stricmp(ge->archive, "teedoff") == 0)))) ? 1 : 0;
		const int fcombat = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "fcombat") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "fcombat") == 0))) ? 1 : 0;
		/* MAME sega/bankp: メイン Z80 が SN×3 を I/O 00/01/02 に直書き。音源 CPU 無し。 */
		const int bankp = (ge && ((ge->subtype[0] && (_stricmp(ge->subtype, "bankp") == 0
				|| _stricmp(ge->subtype, "combh") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "bankp") == 0
				|| _stricmp(ge->archive, "combh") == 0)))) ? 1 : 0;
		const int gberet = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "gberet") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "gberet") == 0))) ? 1 : 0;
		const int higemaru = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "higemaru") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "higemaru") == 0))) ? 1 : 0;
		/* 1942p は I/O 14/15・NMI。本番 1942 はメモリマップ AY×2。1942_88 等は除外。 */
		const int cap1942 = (ge && (
			(ge->subtype[0] && _strnicmp(ge->subtype, "1942", 4) == 0
				&& _strnicmp(ge->subtype, "1942p", 5) != 0
				&& (ge->subtype[4] == 0
					|| ((ge->subtype[4] | 32) >= 'a' && (ge->subtype[4] | 32) <= 'z')))
			|| (ge->archive[0] && _strnicmp(ge->archive, "1942", 4) == 0
				&& _strnicmp(ge->archive, "1942p", 5) != 0
				&& (ge->archive[4] == 0
					|| ((ge->archive[4] | 32) >= 'a' && (ge->archive[4] | 32) <= 'z'))))) ? 1 : 0;
		if (cop01) vsIoKind_ = 4;
		else if (magmax) vsIoKind_ = 5;
		else if (bombjack) vsIoKind_ = 6;
		else if (calorie) vsIoKind_ = 7;
		else if (solomon) vsIoKind_ = 8;
		else if (halleys) vsIoKind_ = 9;
		else if (pbaction) vsIoKind_ = 10;
		else if (chaknpop) vsIoKind_ = 11;
		else if (cap1942) vsIoKind_ = 12;
		else if (swimmer) vsIoKind_ = 13;
		else if (tubep) vsIoKind_ = 14;
		else if (retofinv) vsIoKind_ = 15;
		else if (ikki) vsIoKind_ = 16;
		else if (circusc) vsIoKind_ = 17;
		else if (starforce) vsIoKind_ = 18;
		else if (worldcup) vsIoKind_ = 19;
		else if (fcombat) vsIoKind_ = 20;
		else if (bankp) vsIoKind_ = 21;
		else if (gberet) vsIoKind_ = 22;
		else if (higemaru) vsIoKind_ = 23;
		else if (CEmuAcIsMasao(ge)) vsIoKind_ = 24;
		else vsIoKind_ = 0;
		if (worldcup) {
			/* MAME tehkanwc: Z80 18.432/4、AY 18.432/12。MSM5205 は SE。 */
			cpuHz_ = 4608000;
			opmHz_ = 1536000;
			chip_ = CEmuChipAyCreate(1536000u, sampleRate_);
			chip2_ = CEmuChipAyCreate(1536000u, sampleRate_);
			chip3_ = NULL;
			auxKind_ = 2;
			bankBase_ = 0;
			bankSize_ = 0x4000u;
		} else if (retofinv || ikki || circusc || starforce) {
			/* MAME retofinv: Z80/SN×2 18.432/6。ikki: サブ Z80 4 MHz、SN 2 MHz / 4 MHz。
			   circusc: Z80 14.318181/4、SN×2 /8。starforce: サブ 2 MHz、SN×3 2 MHz。 */
			if (circusc) {
				cpuHz_ = 3579545;
				opmHz_ = 1789772;
				chip_ = CEmuChipSn76489Create(1789772u, sampleRate_);
				chip2_ = CEmuChipSn76489Create(1789772u, sampleRate_);
				chip3_ = NULL;
				bankSize_ = 0x4000u;
			} else if (starforce) {
				cpuHz_ = 2000000;
				opmHz_ = 2000000;
				chip_ = CEmuChipSn76489Create(2000000u, sampleRate_);
				chip2_ = CEmuChipSn76489Create(2000000u, sampleRate_);
				chip3_ = CEmuChipSn76489Create(2000000u, sampleRate_);
				bankSize_ = 0x2000u;
			} else {
				cpuHz_ = retofinv ? 3072000 : 4000000;
				opmHz_ = retofinv ? 3072000 : 2000000;
				chip_ = CEmuChipSn76489Create(retofinv ? 3072000u : 2000000u, sampleRate_);
				chip2_ = CEmuChipSn76489Create(retofinv ? 3072000u : 4000000u, sampleRate_);
				chip3_ = NULL;
				bankSize_ = 0x2000u;
			}
			auxKind_ = 1;
			bankBase_ = 0;
		} else if (bankp) {
			/* MAME bankp: Z80 / SN76489A×3 とも 15.46848 MHz / 6。vblank NMI（port 07 bit4）。 */
			cpuHz_ = 2578080;
			opmHz_ = 2578080;
			chip_ = CEmuChipSn76489Create(2578080u, sampleRate_);
			chip2_ = CEmuChipSn76489Create(2578080u, sampleRate_);
			chip3_ = CEmuChipSn76489Create(2578080u, sampleRate_);
			auxKind_ = 1;
			bankBase_ = 0;
			bankSize_ = 0xE000u;
		} else if (gberet) {
			/* MAME gberet: メイン Z80 18.432/6、SN76489A /12。NMI が CALL 7801。 */
			cpuHz_ = 3072000;
			opmHz_ = 1536000;
			chip_ = CEmuChipSn76489Create(1536000u, sampleRate_);
			chip2_ = NULL;
			chip3_ = NULL;
			auxKind_ = 1;
			bankBase_ = 0;
			bankSize_ = 0xC000u;
		} else if (higemaru) {
			/* MAME higemaru: メイン Z80 12/4、AY8910×2 12/8。音源 CPU 無し。 */
			cpuHz_ = 3000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipAyCreate(1500000u, sampleRate_);
			chip2_ = CEmuChipAyCreate(1500000u, sampleRate_);
			chip3_ = NULL;
			auxKind_ = 2;
			bankBase_ = 0;
			bankSize_ = 0x8000u;
		} else if (CEmuAcIsMasao(ge)) {
			/* MAME mario.cpp masao: Z80+AY 14.31818/8。ROM 4K、RAM 2000、ラッチはポートA。 */
			cpuHz_ = 1789772;
			opmHz_ = 1789772;
			chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
			chip2_ = NULL;
			chip3_ = NULL;
			auxKind_ = 2;
			bankBase_ = 0;
			bankSize_ = 0;
		} else {
		cpuHz_ = fcombat ? 3328000 : ((cop01 || magmax) ? 2500000 : (solomon ? 3072000 : (swimmer ? 2000000 : (tubep ? 2496000 : 3000000))));
		opmHz_ = fcombat ? 1664000 : ((cop01 || magmax) ? 1250000 : (swimmer ? 2000000 : (tubep ? 1248000 : 1500000)));
		chip_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
		chip2_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
		chip3_ = (cap1942 || swimmer) ? NULL : CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
		auxKind_ = 2;
		if (cop01 || magmax || bombjack || calorie || solomon || halleys || pbaction || chaknpop || cap1942 || swimmer || tubep || fcombat) {
			bankBase_ = 0;
			bankSize_ = swimmer ? 0x1000u
				: ((magmax || bombjack || calorie || solomon || halleys || pbaction || chaknpop || cap1942 || tubep || fcombat) ? 0x4000u : 0x8000u);
		}
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE) {
		/* MAME galaxian/scramble: Z80+AY @ 14318000/8 → 1.789772 MHz。AY unmute 補助は入れない — 一定音を強制しプローブ classify() が FLAT（同一ピーク）として棄却する。 */
		const int frogger = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "frogger") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "frogger") == 0))) ? 1 : 0;
		const int hustler = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "hustler") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "hustler") == 0))) ? 1 : 0;
		cpuHz_ = 1789772;
		opmHz_ = 1789772;
		chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
		chip2_ = (frogger || hustler) ? NULL : CEmuChipAyCreate(1789772u, sampleRate_);
		auxKind_ = 2;
		vsIoKind_ = frogger ? 1 : (hustler ? 2 : 0);
		if (frogger) {
			bankBase_ = 0;
			bankSize_ = 0x1800u;
		} else if (hustler) {
			bankBase_ = 0;
			bankSize_ = 0x1000u;
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		if (CEmuAcIsMegazone(ge)) {
			/* MAME megazone: Z80 18.432/6、AY 14.318/8、1×AY。vsIoKind 1。 */
			vsIoKind_ = 1;
			cpuHz_ = 3072000;
			opmHz_ = 1789772;
			chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
			chip2_ = NULL;
			auxKind_ = 2;
			bankSize_ = 0;
		} else {
			/* MAME timeplt_a: Z80/AY DERIVED_CLOCK(1,8) は 18.432 MHz から → 2.304 MHz */
			cpuHz_ = 2304000;
			opmHz_ = 2304000;
			chip_ = CEmuChipAyCreate(2304000u, sampleRate_);
			chip2_ = CEmuChipAyCreate(2304000u, sampleRate_);
			auxKind_ = 2;
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		/* MAME nemesis/gx400: Z80+AY @ 14318180/8 のクロック */
		cpuHz_ = 1789772;
		opmHz_ = 1789772;
		chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
		chip2_ = CEmuChipAyCreate(1789772u, sampleRate_);
		auxKind_ = 2;
		vsIoKind_ = CEmuAcIsKonamigt(ge) ? 1 : 0;
		/* AY1 ポート A は nemesis_portA_r（周期タイマ）。0 に強制しない */
	} else if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		/* snkMapKind_ 1 = ddragon3.cpp（ROM 0000-BFFF、RAM C000、YM C800、OKI D800、ラッチ E000）。
		   2 = dbz.cpp（ROM 0000-7FFF、RAM 8000-BFFF、YM C000、OKI D000、ラッチ E000、Z80/YM 4 MHz）。
		   3 = ultraman.cpp（ROM 0000-7FFF、RAM 8000-BFFF、ラッチ C000、NMI許可 D000、OKI E000、YM F000）。0 = ddragon2。
		   4 = ddragon.cpp MC6809 YM2151@2800 ラッチ@1000→IRQ YM→FIRQ ROM@8000。
		   5 = renegade.cpp MC6809 YM3526@2800 ラッチ@1000-17FF→IRQ YM→FIRQ ROM@8000。 */
		if (CEmuAcIsDdragon1(ge))
			snkMapKind_ = 4;
		else if (CEmuAcIsKuniokun(ge))
			snkMapKind_ = 5;
		else if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "ddragon3") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "ddragon3") == 0
				|| _stricmp(ge->archive, "wwfwfest") == 0))))
			snkMapKind_ = 1;
		else if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "dbz") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "dbz") == 0
				|| _stricmp(ge->archive, "dbz2") == 0))))
			snkMapKind_ = 2;
		else if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "ultraman") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "ultraman") == 0)))
			snkMapKind_ = 3;
		if (snkMapKind_ >= 4) {
			/* MAME: MC6809 @ 12/2=6 MHz（内部 /4）。ddragon YM2151 3.579545、kuniokun YM3526 3 MHz。MSM は未接続。 */
			cpuHz_ = 6000000;
			opmHz_ = (snkMapKind_ == 4) ? 3579545 : 3000000;
			chip_ = (snkMapKind_ == 4)
				? CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_)
				: CEmuChipYm3812Create((uint32_t)opmHz_, sampleRate_);
			chip2_ = NULL;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
			if (!namcoM6809_) return 0;
		} else {
		/* MAME technos/ddragon2: Z80 3.579545、YM2151 同じ、OKI 1.056 MHz。dbz/ultraman は 4 MHz。 */
		cpuHz_ = (snkMapKind_ == 2 || snkMapKind_ == 3) ? 4000000 : 3579545;
		opmHz_ = cpuHz_;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
		pcmKind_ = 2;
		}
	} else if (board_ == CEMU_AC_BOARD_IREM_M62) {
		/* MAME irem/irem.cpp m62_audio のクロック: M6803 @ 3.579545 MHz、AY @ /4 */
		cpuHz_ = 3579545;
		opmHz_ = 894886;
		chip_ = CEmuChipAyCreate(894886u, sampleRate_);
		chip2_ = CEmuChipAyCreate(894886u, sampleRate_);
		auxKind_ = 2;
		m6803_ = (struct m6800*)calloc(1, sizeof(struct m6800));
		if (!m6803_) return 0;
	} else if (board_ == CEMU_AC_BOARD_IREM_M72) {
		/* MAME irem_m72: Z80 と YM2151 とも SOUND_CLOCK 3.579545 MHz。古典 M72（rtype）= sound_ram_map（アップロード済みプログラム付き 64K RAM 全体）。M81/M82/M84（rtype2）= sound_rom_map（ROM + F000 RAM）。専用音源ダンプは ROM マップ既定。インターリーブ upload は LoadRoms で sound_ram_map。 */
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
		/* MAME irem_m92: V35 は素の 14.318181 MHz XTAL。YM2151 と GA20 は XTAL/4。V35 が暗号化音源ドライバ（CEmu/vendor/v35）を走る。下の Z80 は作るがこの基板ではステップしない。 */
		cpuHz_ = 14318181;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipGa20Create(3579545u, sampleRate_);
		pcmKind_ = 7;
		v35_ = V35Create();
		if (!v35_) return 0;
	} else if (board_ == CEMU_AC_BOARD_ATARI_SYS1) {
		/* MAME atarisy1 sound_map: M6502 @ 14.31818/8 MHz、YM2151 @ /4、ラッチ @1810 → NMI、YM IRQ → IRQ。POKEY @1870 は stub。 */
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
		if (CEmuAcIsMatmania(ge))
			decoCpuKind_ = 9;
		if (decoCpuKind_ == 9) {
			/* MAME matmania: M6502 12/2/6=1 MHz、AY8910×2 12/8=1.5 MHz、ラッチ IRQ、周期 NMI。 */
			cpuHz_ = 1000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipAyCreate(1500000u, sampleRate_);
			chip2_ = CEmuChipAyCreate(1500000u, sampleRate_);
			auxKind_ = 2;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			h6280_ = NULL;
			m6502_ = M6502Create();
			if (!m6502_) return 0;
		} else if (decoCpuKind_ == 0) {
			/* MAME cninja.cpp: HuC6280 @ XTAL/8 + YM2203 @ XTAL/8 + YM2151 @ XTAL/9 + OKI6295×2。内部 HuC6280 PSG は未使用（route 0）。 */
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
		} else if (decoCpuKind_ == 8) {
			/* MAME dec0.cpp midres(): H6280 24/4/3=2 MHz、YM3812 12/4=3 MHz @108000、
			   YM2203 12/8=1.5 MHz @118000、OKI 1.056 MHz PIN7 High @130000、
			   ラッチ @138000 → NMI、YM3812 irq → IRQ1、RAM 1F0000。 */
			cpuHz_ = 2000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
			chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			h6280_ = H6280Create();
			if (!h6280_) return 0;
		} else if (decoCpuKind_ == 1) {
			/* MAME karnov.cpp: M6502 @ 12/8 MHz + YM2203 @ 1.5 + YM3526 @ 3。YM3812 が YM3526（OPL）の代用。ラッチ NMI。OPL IRQ → IRQ。 */
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
			const int brk = (ge && ((_stricmp(ge->subtype, "brkthru") == 0)
				|| (ge->archive && (_stricmp(ge->archive, "brkthru") == 0
					|| _stricmp(ge->archive, "darwin") == 0)))) ? 1 : 0;
			const int expr = (ge && ((_stricmp(ge->subtype, "exprraid") == 0)
				|| (ge->archive && _stricmp(ge->archive, "exprraid") == 0))) ? 1 : 0;
			if (brk || expr) {
				/* MAME brkthru.cpp / exprraid.cpp: MC6809 @ 12/2=6 MHz、YM3526 @3、YM2203 @1.5、ラッチ NMI。 */
				decoCpuKind_ = brk ? 6 : 7;
				cpuHz_ = 6000000;
				opmHz_ = 3000000;
				chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
				chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
				auxKind_ = 0;
				pcm_ = NULL;
				pcm2_ = NULL;
				pcmKind_ = 0;
				h6280_ = NULL;
				m6502_ = NULL;
				namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
				if (!namcoM6809_) return 0;
			} else {
			/* MAME dec8（cobracom/oscar/…）: R65C02 @ 1.5 MHz + YM2203 @2000 + YM3812 @4000 + ラッチ @6000 → NMI。OKI 無し。 */
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
			}
		} else {
			/* MAME dec0/actfancr: M6502/R65C02 @ 1.5 MHz + YM2203 + YM3812 + OKI。ラッチ NMI。YM3812 IRQ → IRQ。 */
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
		/* MAME jaleco/megasys1: 音源 68000 @ 7 MHz、YM2151 @ 3.5 MHz、OKI6295×2 @ 4 MHz PIN7 High（clock / 132 = 30303 nibble/s）。CChipOki6295 はそのレートを取り、生クリスタルではない。 */
		cpuHz_ = 7000000;
		opmHz_ = 3500000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(4000000u / 132u, sampleRate_);
		pcm2_ = CEmuChipOki6295Create(4000000u / 132u, sampleRate_);
		pcmKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		/* MAME konamigx gxsndmap: 音源 68000 @ 16 MHz、K054539×2 @ 18.432 MHz、K056800 メールボックス、TMS57002 DASP（stub）。 */
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
		/* Sys12 H8/3002 @ 16.9344 MHz。ND-1 @ 49.152/3 ≈ 16.384 MHz。MAME は Sys12 C352 を 25.4016 MHz、分周 288。Sys11/22 と ND-1 は 24.576 MHz。Sys11/22 + NA-1/NB-1: 本物 M37702（C74/C76/C69）— H8 を再利用できない。NA/NB は C219（C140）。 */
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
		/* NA-1/NA-2 と NB-1/NB-2 は音源側で一族ではない: NA は $C000 の C69/C70 内部 ROM が C219 を駆動。NB は外部 nX-spr0 プログラムが 16.7 MHz の C352 を駆動。NB を NA 扱いすると PCM チップが違い、アーカイブに無い内部 BIOS を求め、NB-1 11 セットが open で棄却された。 */
		const int naOnly = (ge && (_stricmp(ge->subtype, "na1") == 0
			|| _stricmp(ge->subtype, "na2") == 0));
		/* System 22 の C74 は 49.152 MHz / 3。System 11 の C76 が使う 16.9344 MHz ではない。ドライバのテンポはここから直取り。 */
		cpuHz_ = (nd1 || sys22) ? 16384000 : (naOnly ? 12500000
			: (naNb ? 16700000 : 16934400));
		m37702Soft_ = 0;
		m37702C140_ = naOnly ? 1 : 0;
		m37702MapKind_ = naOnly ? 1 : (sys22 ? 2 : 0);
		/* Namco はプラットフォーム毎に M37702 を貼り替え、各ラベルに独自 16KB マスク ROM（namcomcu.cpp）: NA-1/NA-2 は C69/C70、System 22 は C74、NB-1/NB-2 と System FL は C75、System 11 は C76。NA セットはアーカイブに c69/c70 を同梱。残りは無い。 */
		m37702McuKind_ = sys22 ? 74 : (sys11 ? 76 : ((naNb && !naOnly) ? 75 : 0));
		if (naOnly) {
			opmHz_ = 8192000;
			chip_ = CEmuChipC140Create((uint32_t)opmHz_, sampleRate_);
			if (chip_) CEmuChipC140SetType(chip_, 2); /* C219 */
		} else if (naNb) {
			opmHz_ = 16700000;
			chip_ = CEmuChipC352Create((uint32_t)opmHz_, sampleRate_);
		} else {
			opmHz_ = (!nd1 && !sysM377) ? 25401600 : 24576000;
			chip_ = CEmuChipC352Create((uint32_t)opmHz_, sampleRate_);
		}
		chip2_ = NULL;
		if (sysM377) {
			h8_ = NULL;
			m37702_ = M37702Create();
			if (!m37702_) return 0;
			m37702Soft_ = 1; /* LoadRomsM37702 接続後にクリア */
		} else {
			m37702_ = NULL;
			h8_ = H8Create();
			if (!h8_) return 0;
		}
	} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		/* MAME namcos2 の配置: M6809 @ 2.048 MHz + YM2151 @ 3.579545 + C140 @ 21.333 kHz */
		cpuHz_ = 2048000;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipC140Create(21333u, sampleRate_);
		pcmKind_ = 8;
		if (pcm_ && ge && ge->subtype
			&& (_stricmp(ge->subtype, "system21") == 0
				|| _stricmp(ge->subtype, "system21b") == 0))
			CEmuChipC140SetType(pcm_, 1);
		namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
		if (!namcoM6809_) return 0;
	} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
		/* MAME namcos1: M6809 @ 49.152/32 MHz + YM2151 + CUS30 の配置 */
		cpuHz_ = 1536000;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipC30Create(24000u, sampleRate_, CEMU_C30_STEREO);
		pcmKind_ = 9;
		namcoM6809_ = (struct mc6809*)calloc(1, sizeof(mc6809__t));
		if (!namcoM6809_) return 0;
	} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS86) {
		/* MAME namcos86: HD63701 @ 49.152/8 MHz + CUS30 + YM2151 の配置 */
		cpuHz_ = 1536000;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		/* CUS60 847A mute は RL=$00 を書く。roishtar は TL 生きたまま KeyOn するが RL は 0（fmgen 無音）。RL=0 を L+R と扱う。 */
		CEmuChipYm2151SetRlZeroAsLr(chip_, 1);
		chip2_ = NULL;
		pcm_ = CEmuChipC30Create(24000u, sampleRate_, CEMU_C30_STEREO);
		pcmKind_ = 9;
		hd63701_ = HD63701Create();
		if (!hd63701_) return 0;
		/* 本物 CUS60 曲は KC/KeyOn を組む。mute 再描画補助は全タイトルを同じクリップピークに潰した — 生 YM トラフィックを残す。 */
	} else if (board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		/* Galaga / Dig Dug / Bosco: Z80 @ 約 3.072 MHz + Pac-Man WSG 3 声（PROM 波形）。Mappy 期 15XX は wsg6809 に MAPPY。wsg63701（pacland/skykid）: HD63701 + CUS30 MAPPY、YM2151 無し。 */
		wsgMappy_ = (_stricmp(ge->subtype, "wsg6809") == 0
			|| _stricmp(ge->subtype, "wsg63701") == 0) ? 1 : 0;
		wsg63701_ = (_stricmp(ge->subtype, "wsg63701") == 0) ? 1 : 0;
		const int pacman = (wsgMappy_ || wsg63701_) ? 0 : 1;
		/* MAME skykid.cpp: HD63701 @ 49.152/8 = 6.144 MHz。wsg6809 は 1.536 MHz のまま。 */
		cpuHz_ = pacman ? 3072000 : (wsg63701_ ? 6144000 : 1536000);
		opmHz_ = pacman ? 96000 : 24000;
		/* MAME skykid.cpp: HD63701 + NAMCO_CUS30（wave RAM + ステレオレジスタ）。15XX MAPPY ではない。wsg6809（mappy/toypop）は MAPPY のまま。 */
		chip_ = CEmuChipC30Create((uint32_t)opmHz_, sampleRate_,
			wsg63701_ ? CEMU_C30_STEREO
			: (pacman ? CEMU_C30_PACMAN : CEMU_C30_MAPPY));
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
		/* MAME toaplan1: Z80 + YM3812 とも 28 MHz / 8。コマンドは 8000 の共有 RAM（ラッチ/NMI ではない）。YM I/O ポートはゲームで違う: truxton/rallybik=60、hellfire=70、zerowing=A8、他は 00。snowbros（Kaneko、ここでエイリアス）はそのメールボックスではない: I/O YM 02/03、ラッチ 04 → NMI、YM IRQ0、Z80 6 MHz / YM 3 MHz。slapfght（tigerh/alcon/getstar）: AY×2 1.5 MHz メモリマップ、C800 cmd、NMI 360 Hz（tigerh）/ 180 Hz（alcon/getstar）。kaneko_=3。perfrman: 同じ AY デコード、共有 RAM 8800、2 MHz、NMI 240 Hz。kaneko_=5。 */
		toaplanKaneko_ = 0;
		if (ge && ((_stricmp(ge->subtype, "snowbros") == 0)
			|| (ge->archive && _stricmp(ge->archive, "snowbros") == 0)))
			toaplanKaneko_ = 1;
		else if (ge && ((_stricmp(ge->subtype, "wardner") == 0)
			|| (ge->archive && _stricmp(ge->archive, "wardner") == 0)))
			toaplanKaneko_ = 2;
		else if (CEmuAcIsSlapfght(ge))
			toaplanKaneko_ = 3;
		else if (ge && ((_stricmp(ge->subtype, "pipibibs") == 0)
			|| (ge->archive && _stricmp(ge->archive, "pipibibs") == 0)))
			toaplanKaneko_ = 4;
		else if (CEmuAcIsPerfrman(ge))
			toaplanKaneko_ = 5;
		if (toaplanKaneko_ == 1) {
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
		} else if (toaplanKaneko_ == 3) {
			cpuHz_ = 3000000;
			opmHz_ = 1500000;
		} else if (toaplanKaneko_ == 4) {
			/* MAME pipibibi: Z80 / YM3812 とも 27/8 = 3.375 MHz。YM はメモリ E000。 */
			cpuHz_ = 3375000;
			opmHz_ = 3375000;
		} else if (toaplanKaneko_ == 5) {
			cpuHz_ = 2000000;
			opmHz_ = 2000000;
			bankBase_ = 0;
			bankSize_ = 0;
		} else {
			cpuHz_ = 3500000;
			opmHz_ = 3500000;
		}
		pcm_ = NULL;
		pcmKind_ = 0;
		toaplanYmPort_ = 0x00;
		if (toaplanKaneko_ == 3 || toaplanKaneko_ == 5) {
			chip_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
			chip2_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
			auxKind_ = 2;
			/* YM ポートバイトを NMI レート旗に再利用: 1 = 360 Hz（tigerh） */
			if (toaplanKaneko_ == 3 && ge
				&& ((_stricmp(ge->subtype, "tigerh") == 0)
					|| (ge->archive && _stricmp(ge->archive, "tigerh") == 0)))
				toaplanYmPort_ = 1;
		} else {
			chip_ = CEmuChipYm3812Create((uint32_t)opmHz_, sampleRate_);
			chip2_ = NULL;
			if (toaplanKaneko_ == 1)
				toaplanYmPort_ = 0x02;
			else if (ge && ge->archive) {
				if (_stricmp(ge->archive, "truxton") == 0
					|| _stricmp(ge->archive, "rallybik") == 0)
					toaplanYmPort_ = 0x60;
				else if (_stricmp(ge->archive, "hellfire") == 0)
					toaplanYmPort_ = 0x70;
				else if (_stricmp(ge->archive, "zerowing") == 0)
					toaplanYmPort_ = 0xa8;
			}
		}
	} else if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		/* snk68（3812）: Z80 + YM3812 I/O 00/20、ラッチ @ F800 → NMI。古典 SNK（athena/ikari/gwar…）: メモリマップ YM3526/Y8950×2、ラッチ @ E000 → IRQ0（MAME snk.cpp YM3526_*_sound_map）。
		   aso: 単発 YM3526 @F000、ラッチ @D000、RAM C000（aso_YM3526_sound_map）。mainsnk/canvas: AY×2 @E000/@E008、ラッチ @A000 → NMI、周期 IRQ 244 Hz。 */
		const int aso = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "aso") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "aso") == 0
				|| _stricmp(ge->archive, "alphamis") == 0
				|| _stricmp(ge->archive, "arian") == 0)))) ? 1 : 0;
		const int mainsnk = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "mainsnk") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "mainsnk") == 0
				|| _stricmp(ge->archive, "canvas") == 0)))) ? 1 : 0;
		const int classic = (!aso && !mainsnk && ge && (_stricmp(ge->subtype, "3526x2") == 0
			|| _stricmp(ge->subtype, "3526_8950") == 0
			|| _stricmp(ge->subtype, "3526") == 0
			|| _stricmp(ge->subtype, "chopper") == 0));
		const int fitegolf = (!aso && !mainsnk && !classic && ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "fitegolf") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "fitegolf") == 0
				|| _stricmp(ge->archive, "countryc") == 0)))) ? 1 : 0;
		snkMapKind_ = aso ? 2 : (mainsnk ? 3 : (classic ? 1 : (fitegolf ? 4 : 0)));
		cpuHz_ = 4000000;
		if (snkMapKind_ >= 1) {
			bankBase_ = 0;
			bankSize_ = 0;
		}
		if (snkMapKind_ == 3) {
			opmHz_ = 2000000;
			chip_ = CEmuChipAyCreate(2000000u, sampleRate_);
			chip2_ = CEmuChipAyCreate(2000000u, sampleRate_);
			auxKind_ = 2;
			pcm_ = NULL;
			pcmKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else {
			opmHz_ = 4000000;
			chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
			chip2_ = (snkMapKind_ == 1) ? CEmuChipYm3812Create(4000000u, sampleRate_) : NULL;
			auxKind_ = (snkMapKind_ == 1) ? 3 : 0;
			pcm_ = NULL;
			pcmKind_ = 0;
			if (snkMapKind_ == 2) {
				bankBase_ = 0;
				bankSize_ = 0;
			}
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		/* MAME thunderx/scontra/crimfght/twin16: Z80 + YM2151 @ 3.579545。K007232 PCM レジスタ窓は受けるが未合成 — BGM は YM2151。変種は konamiK7232Map_ 経由。
		   combatsc: Z80 1.5 MHz + YM2203 3 MHz。ホスト 0418 → IRQ0。シーケンサは YM タイマ B を poll（YM IRQ 線は無し）。
		   hexion: メイン Z80 @ 6 MHz が音源。K051649 @E800 + OKI @F200。YM2151 マップではない。 */
		if (CEmuAcIsHexion(ge)) {
			konamiK7232Map_ = 7;
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipSccCreate(3000000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
			pcmKind_ = 2;
			bankBase_ = 0x8000u;
			bankSize_ = 0x2000u;
			bankLoaded_ = 1;
		} else {
		const int combatsc = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "combatsc") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "combatsc") == 0))) ? 1 : 0;
		const int ajax = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "ajax") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "ajax") == 0))) ? 1 : 0;
		const int chqflag = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "chqflag") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "chqflag") == 0))) ? 1 : 0;
		const int wecleman = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "weclemans") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "wecleman") == 0))) ? 1 : 0;
		const int flakattack = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "flakattack") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "mx5000") == 0
				|| _stricmp(ge->archive, "flkatck") == 0)))) ? 1 : 0;
		if (combatsc) {
			konamiK7232Map_ = 3;
			cpuHz_ = 1500000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm2608Create(3000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			bankBase_ = 0;
			bankSize_ = 0;
		} else {
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			if (ge && ge->subtype[0] && _stricmp(ge->subtype, "crimfght") == 0)
				konamiK7232Map_ = 1;
			else if (ge && ge->subtype[0] && _stricmp(ge->subtype, "gradius3") == 0)
				konamiK7232Map_ = 2;
			else if (ajax)
				konamiK7232Map_ = 4;
			else if (chqflag)
				konamiK7232Map_ = 5;
			else if (wecleman || flakattack)
				konamiK7232Map_ = 6;
			else
				konamiK7232Map_ = 0;
		}
		if (konamiK7232Map_ == 6)
			memset(namcoCus30_, 0, 16);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		}
	} else if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		/* MAME alpha68k_II: Z80 @ 6 MHz、YM2203 @ 約 3 MHz、YM2413 @ 3.579545、DAC、IN 00 経由ラッチ、バンク @ C000（16KiB）。周期 NMI @ 約 7614 Hz。
		   mmpanic/animaljr（MAME ddenlovr）: Z80 @ 3.58 MHz、YM2413+AY I/O、RAM 6000、ラッチ NMI＋vblank IRQ0。 */
		const int mmpanic = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "mmpanic") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "mmpanic") == 0
				|| _stricmp(ge->archive, "animaljr") == 0
				|| _stricmp(ge->archive, "funkyfig") == 0)))) ? 1 : 0;
		vsIoKind_ = mmpanic ? 1 : 0;
		chip2_ = NULL;
		if (mmpanic) {
			cpuHz_ = 3579545;
			opmHz_ = 1789772;
			chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
			mainIsYm2203_ = 0;
			pcm_ = NULL;
			pcmKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else {
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0 /* OPN/YM2203 */, sampleRate_);
			mainIsYm2203_ = 1;
			pcm_ = CEmuChipIremDacCreate(sampleRate_);
			pcmKind_ = 6;
			bankBase_ = 0xc000;
			bankSize_ = 0x4000;
		}
		alphaYmAddr_ = 0;
		alphaOpllAddr_ = 0;
		alphaNmiMask_ = 0;
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
		/* MAME hcastle: Z80 + YM3812 @ A000（IRQ→NMI）、ラッチ @ D000、K007232 @ B000 / K051649 @ 9800 は stub。
		   spy.cpp: YM3812 @C000、ラッチ @D000 poll、YM IRQ→NMI、K007232×2 @A000/@B000。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		vsIoKind_ = CEmuAcIsSpy(ge) ? 1 : 0;
		chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_M68K_PCM) {
		/* Seta/Allumer（MAME seta.cpp、seta2.cpp）は X1-010 の 8 KiB RAM 窓を B00000 に置き、メイン 68000 を 16 MHz + vblank IRQ2。Cave（MAME cave.cpp）は YMZ280B のレジスタ／データポート対を下位バイトレーン 300000、16 MHz + vblank IRQ1。 */
		{
			const CEmuAcM68kPcmSpec* sp = CEmuAcFindM68kPcmSpec(ge->subtype);
			/* 未掲載セットは最も多い Seta 配置へフォールバック */
			static const CEmuAcM68kPcmSpec kSetaDefault = {
				"", 1, 0xb00000u, 0x4000u, 0x200000u, 0x10000u, 0
			};
			if (!sp) sp = &kSetaDefault;
			m68kPcmKind_ = sp->kind;
			m68kPcmAddr_ = sp->chipAddr;
			m68kPcmSpan_ = sp->chipSpan;
			m68kRamAddr_ = sp->ramAddr;
			m68kRamSize_ = sp->ramSize;
			m68kIrqAddr_ = sp->irqAddr;
			cpuHz_ = 16000000;
			if (sp->kind == 2) {
				opmHz_ = 16934400;
				m68kVblankLevel_ = 1; /* INPUT_MERGER_ANY_HIGH を inputline 1 へ */
				chip_ = CEmuChipYmz280bCreate((uint32_t)opmHz_, sampleRate_);
			} else {
				opmHz_ = 16000000;
				m68kVblankLevel_ = 2;
				chip_ = CEmuChipX1010Create((uint32_t)opmHz_, sampleRate_);
			}
		}
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_TECMO16) {
		/* MAME tecmo16: Z80 @ 4 MHz、FM @ FC04、OKI @ FC00、ラッチ @ FC08→NMI。Tecmo 自前セット（tecmo16/wc90/tbowl/spbactn/rygar）はそのソケットに YM3812 であり YM2151 ではない。それらのリップを OPM レジスタマップで駆動すると書込は数えても全タイトル無音。 */
		/* 古典 Tecmo は 16bit FC00 マップではない（MAME tecmo.cpp / tbowl.cpp）。rygar: ROM 0000-3FFF、RAM 4000-47FF、YM3526 8000、ラッチ C000。gemini/silkworm: ROM 0000-7FFF、RAM 8000-87FF、YM3812 A000、ラッチ C000。tbowl: ROM 0000-7FFF、RAM C000-C7FF、YM3812 D000+D800、ラッチ E010。 */
		{
			const int caveKind = CEmuAcCaveZ80Kind(ge);
			if (caveKind)
				tecmoOpl_ = caveKind;
			else if (ge && _stricmp(ge->subtype, "rygar") == 0)
				tecmoOpl_ = 1;
			else if (ge && _stricmp(ge->subtype, "gemini") == 0)
				tecmoOpl_ = 2;
			else if (ge && _stricmp(ge->subtype, "tbowl") == 0)
				tecmoOpl_ = 4;
			else if (ge && (_stricmp(ge->subtype, "spbactn") == 0
				|| (ge->archive && (_stricmp(ge->archive, "spbactn") == 0
					|| _stricmp(ge->archive, "spbactnj") == 0))))
				tecmoOpl_ = 3;
			else if (ge && ((_stricmp(ge->subtype, "wc90") == 0)
				|| (ge->archive && _stricmp(ge->archive, "wc90") == 0)))
				tecmoOpl_ = 6;
			else if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "superx") == 0)
				|| (ge->archive && (_stricmp(ge->archive, "superx") == 0
					|| _stricmp(ge->archive, "superxm") == 0
					|| _stricmp(ge->archive, "rshark") == 0
					|| _stricmp(ge->archive, "popbingo") == 0
					|| _stricmp(ge->archive, "bluehawk") == 0
					|| _stricmp(ge->archive, "flytiger") == 0
					|| _stricmp(ge->archive, "primella") == 0))))
				tecmoOpl_ = 11;
			else if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "pwrinst1") == 0)
				|| (ge->archive && (_stricmp(ge->archive, "powerins") == 0
					|| _stricmp(ge->archive, "powerinsj") == 0
					|| _stricmp(ge->archive, "powerinsa") == 0
					|| _stricmp(ge->archive, "powerinsb") == 0))))
				tecmoOpl_ = 12;
			else if (CEmuAcIsNslasherZ80(ge))
				tecmoOpl_ = 13;
			else
				tecmoOpl_ = CEmuAcIsTecmoOplSub(ge->subtype) ? 3 : 0;
		}
		bankBase_ = 0x4000u;
		bankSize_ = 0x4000u;
		if (tecmoOpl_ == 3) {
			/* MAME spbactn: 線形 64K ROM 0000-EFFF。4000 バンク窓は潰す。 */
			bankBase_ = 0;
			bankSize_ = 0;
		}
		if (tecmoOpl_ == 5) {
			/* MAME cave.cpp sailormn: Z80 8 MHz、YM2151 4 MHz、OKI×2 2.112 MHz PIN7 High。I/O ラッチ → NMI、YM IRQ0。FC00 マップは使わない。 */
			cpuHz_ = 8000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2151Create(4000000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(2112000u / 132u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(2112000u / 132u, sampleRate_);
			pcmKind_ = 2;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else if (tecmoOpl_ == 7) {
			/* MAME cave.cpp hotdogst: Z80/YM2203 4 MHz、OKI 2 MHz PIN7 High。RAM E000。 */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(2000000u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
		} else if (tecmoOpl_ == 8) {
			/* MAME cave.cpp mazinger: Z80/YM2203 4 MHz、OKI 1.056 MHz PIN7 High。RAM C000/F800。 */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1056000u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
		} else if (tecmoOpl_ == 9) {
			/* MAME cave.cpp metmqstr: Z80 8 MHz、YM2151 4 MHz、OKI×2 2 MHz PIN7 High。hotdogst メモリ。 */
			cpuHz_ = 8000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2151Create(4000000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(2000000u / 132u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(2000000u / 132u, sampleRate_);
			pcmKind_ = 2;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else if (tecmoOpl_ == 10) {
			/* MAME cave.cpp pwrinst2: Z80 8 MHz、YM2203 4 MHz、OKI×2 3 MHz PIN7 Low、NMK112。バンク窓 8000。 */
			cpuHz_ = 8000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(3000000u / 165u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(3000000u / 165u, sampleRate_);
			pcmKind_ = 2;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else if (tecmoOpl_ == 11) {
			/* MAME dooyong.cpp bluehawk_sound_map（superx/rshark/popbingo/flytiger）:
			   Z80 4 MHz、YM2151 4 MHz、OKI 1 MHz PIN7 High。ROM 0000-EFFF、RAM F000-F7FF、
			   ラッチ F800 は poll（generic_latch に pending 線無し）、YM F808/F809、OKI F80A。 */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2151Create(4000000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
			pcmKind_ = 2;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (tecmoOpl_ == 12) {
			/* MAME nmk16.cpp powerins: Z80 12/2=6 MHz、YM2203 12/8=1.5 MHz、OKI×2 16/4=4 MHz PIN7 Low、NMK112。
			   ROM 0000-BFFF、RAM C000-DFFF、ラッチ E000 poll（generic_latch に pending 線無し）。
			   I/O は macross2_sound_io_map: YM 00/01、OKI0 80、OKI1 88、NMK112 90-97。YM irq → IRQ0。 */
			cpuHz_ = 6000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(4000000u / 165u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(4000000u / 165u, sampleRate_);
			pcmKind_ = 2;
			bankBase_ = 0;
			bankSize_ = 0;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else if (tecmoOpl_ == 13) {
			/* MAME deco32.cpp nslasher / fghthistu:
			   Z80 32.22/9=3.58 MHz、YM2151 3.58 MHz irq+latch → IRQ0 merger、
			   OKI0 32.22/32 PIN7 High、OKI1 32.22/16 PIN7 High。
			   ROM 0000-7FFF、RAM 8000-87FF、YM A000/A001、OKI0 B000、OKI1 C000、
			   ラッチ D000。I/O 0000-FFFF は audiocpu ROM 領域。YM 0x1B が OKI バンク。 */
			cpuHz_ = 3580000;
			opmHz_ = 3580000;
			chip_ = CEmuChipYm2151Create(3580000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1006875u / 132u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(2013750u / 132u, sampleRate_);
			pcmKind_ = 2;
			bankBase_ = 0;
			bankSize_ = 0;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuAcDeco32OkiBank(raizingOkiBank_[0], 0x80000u, 0);
			CEmuAcDeco32OkiBank(raizingOkiBank_[1], 0x80000u, 0);
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else if (tecmoOpl_ == 6) {
			/* MAME tecmo/wc90.cpp: Z80 8 MHz/2、YM2608 8 MHz、チップ上 ADPCM。ROM 0000-BFFF、RAM F000-F7FF、YM F800-F803、ラッチ FC10 → NMI。 */
			cpuHz_ = 4000000;
			opmHz_ = 8000000;
			chip_ = CEmuChipYm2608Create(8000000u, 1 /* OPNA */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = NULL;
			pcmKind_ = 0;
		} else {
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = tecmoOpl_
				? CEmuChipYm3812Create((uint32_t)opmHz_, sampleRate_)
				: CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			chip2_ = (tecmoOpl_ == 4)
				? CEmuChipYm3812Create((uint32_t)opmHz_, sampleRate_)
				: NULL;
			pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
			pcmKind_ = 2;
			if (!tecmoOpl_ || tecmoOpl_ == 2) {
				/* tecmo16: 線形 64K。gemini: 線形 32K 0000-7FFF。既定 SetBank(0) は 4000-7FFF を潰す。 */
				bankBase_ = 0;
				bankSize_ = 0;
			}
		}
	} else if (board_ == CEMU_AC_BOARD_RAIZING) {
		/* MAME toaplan/raizing.cpp + raizing_batrider.cpp。いずれも 32 MHz 発振器から。OKI は出力レート（PIN7 High で clock/132、Low で /165）を取り、それが CChipOki6295 の要求。 */
		raizingType_ = CEmuAcRaizingType(ge->subtype);
		if (raizingType_ == 4) {
			cpuHz_ = 32000000 / 6;
			opmHz_ = 16934400;
			chip_ = CEmuChipYmz280bCreate((uint32_t)opmHz_, sampleRate_);
			/* 両チップ出力は TA8201 1 基に着く（MAME add_route ALL_OUTPUTS → "mono"）。だから音源プログラムは全音楽ボイスを右振りのままにできる。 */
			CEmuChipYmz280bSetMono(chip_, 1);
			pcm_ = NULL;
			pcmKind_ = 0;
		} else {
			cpuHz_ = (raizingType_ == 3) ? 32000000 / 6 : 32000000 / 8;
			/* mahoudai は代わりに 27 MHz 映像クリスタルから OPM をクロック */
			opmHz_ = (raizingType_ == 1) ? 27000000 / 8 : 32000000 / 8;
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			const unsigned okiClock = (raizingType_ == 1) ? 32000000u / 32u
				: (raizingType_ == 2) ? 32000000u / 16u : 32000000u / 10u;
			pcm_ = CEmuChipOki6295Create(okiClock / 132u, sampleRate_);
			pcmKind_ = 2;
			if (raizingType_ == 3) {
				/* 第 2 OKI は PIN7 Low 配線なので clock/165 で走る */
				pcm2_ = CEmuChipOki6295Create(okiClock / 165u, sampleRate_);
				CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
			}
			if (raizingType_ != 1)
				CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
		}
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_FLSTORY) {
		/* MAME flstory: Z80 @ 4 MHz、YM2149 @ 2 MHz マップ C800。MSM5232 @ CA00 が主旋律。AY はノイズ/FX/DAC トラフィック。MAME msisaac: AY×2 @8000/@8002 + MSM @8010、ラッチ C000、NMI C001/C002。この基板では taitoOpmMap_=1 としてパック（TAITO_OPM ではない）。 */
		cpuHz_ = 4000000;
		opmHz_ = 2000000;
		chip_ = CEmuChipAyCreate(2000000u, sampleRate_);
		chip2_ = CEmuChipMsm5232Create(2000000u, sampleRate_);
		pcm_ = NULL;
		pcmKind_ = 0;
		auxKind_ = 4; /* chip2 用に MSM5232 を破棄 */
		flstoryNmiEn_ = 0;
		if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "msisaac") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "msisaac") == 0))) {
			taitoOpmMap_ = 1;
			chip3_ = CEmuChipAyCreate(2000000u, sampleRate_);
			bankBase_ = 0;
			bankSize_ = 0x4000u;
		} else if (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "nycaptor") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "nycaptor") == 0
				|| _stricmp(ge->archive, "cyclshtg") == 0
				|| _stricmp(ge->archive, "wyvernf0") == 0)))) {
			/* MAME nycaptor: AY×2 @C800/@C802、MSM @C900、ラッチ D000、NMI D200/D400。taitoOpmMap_=2（kikikai 9FFF poke）は使わない。 */
			taitoOpmMap_ = 3;
			chip3_ = CEmuChipAyCreate(2000000u, sampleRate_);
			bankBase_ = 0;
			bankSize_ = 0xc000u;
		}
	} else if (board_ == CEMU_AC_BOARD_TERRACRE) {
		/* terracre: YM3526 @4 MHz、RAM C000、ラッチ I/O 04/06。armedf/terraf: YM3812 @4 MHz、RAM F800-FFFF、同じ I/O。cclimbr2/legion: RAM C000-FFFF（cclimbr2_soundmap）。公式 legion/cclimbr2 は YM3526、legion ブートレグは YM3812。ホストコマンドは ((cmd&0x7f)<<1)|1（MAME sound_command_w）。
		   argus: YM2203×1 I/O 00-01、RAM 8000、ラッチ C000。butasan/valtric: YM2203×2 I/O 00-01/80-81、RAM C000、ラッチ E000。YM IRQ0。 */
		const int argus = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "argus") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "argus") == 0))) ? 1 : 0;
		const int valtric = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "valtric") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "valtric") == 0))) ? 1 : 0;
		const int butasan = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "butasan") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "butasan") == 0
				|| _stricmp(ge->archive, "bombsa") == 0)))) ? 1 : 0;
		if (CEmuAcIsTerracren(ge))
			terracreMap_ = 6;
		else if (argus)
			terracreMap_ = 3;
		else if (valtric)
			terracreMap_ = 5; /* sound_map_a + YM×2 portmap_2 */
		else if (butasan)
			terracreMap_ = 4;
		else if (CEmuAcIsCclimbr2Map(ge->subtype))
			terracreMap_ = 2;
		else
			terracreMap_ = CEmuAcIsArmedfSub(ge->subtype) ? 1 : 0;
		if (terracreMap_ >= 3 && terracreMap_ <= 5) {
			cpuHz_ = 5000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			chip2_ = (terracreMap_ >= 4) ? CEmuChipYm2608Create(1500000u, 0, sampleRate_) : NULL;
			auxKind_ = 0;
			pcm_ = NULL;
			pcmKind_ = 0;
		} else if (terracreMap_ == 6) {
			/* MAME terracren: Z80/YM2203 16/4 = 4 MHz。I/O は YM3526 セットと同じ 00/01/04/06。IRQ は周期 7812 Hz（YM irq 無し）。 */
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			if (chip_)
				chip_->SetTimerIrqPolicy(0);
			chip2_ = NULL;
			auxKind_ = 0;
			pcm_ = NULL;
			pcmKind_ = 0;
		} else {
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
			chip2_ = NULL;
			pcm_ = NULL;
			pcmKind_ = 0;
		}
	} else if (board_ == CEMU_AC_BOARD_ROBOKID) {
		const int tharrier = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "tharrier") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "tharrier") == 0
				|| _stricmp(ge->archive, "manybloc") == 0)))) ? 1 : 0;
		const int macross2 = (ge && (
			(ge->subtype[0] && (_stricmp(ge->subtype, "macross2") == 0
				|| _stricmp(ge->subtype, "tdragon2") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "macross2") == 0
				|| _stricmp(ge->archive, "tdragon2") == 0)))) ? 1 : 0;
		const int airbustr = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "airbuster") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "airbustr") == 0
				|| _stricmp(ge->archive, "airbuster") == 0)))) ? 1 : 0;
		const int djboy = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "djboy") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "djboy") == 0))) ? 1 : 0;
		const int blazeon = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "blazeon") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "blazeon") == 0))) ? 1 : 0;
		const int hvyunit = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "hvyunit") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "hvyunit") == 0))) ? 1 : 0;
		const int crospang = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "crospang") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "crospang") == 0
				|| _stricmp(ge->archive, "heuksun") == 0)))) ? 1 : 0;
		const int empcity = (ge && (
			(ge->subtype[0] && (_stricmp(ge->subtype, "empcity") == 0
				|| _stricmp(ge->subtype, "cshooter") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "empcity") == 0
				|| _stricmp(ge->archive, "cshooter") == 0)))) ? 1 : 0;
		const int nmg5 = (ge && (
			(ge->subtype[0] && (_stricmp(ge->subtype, "nmg5") == 0
				|| _stricmp(ge->subtype, "yunsun16") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "nmg5") == 0
				|| _stricmp(ge->archive, "searchey") == 0
				|| _stricmp(ge->archive, "wondstck") == 0
				|| _stricmp(ge->archive, "magicbub") == 0)))) ? 1 : 0;
		const int pclubys = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "pclubys") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "pclubys") == 0
				|| _stricmp(ge->archive, "garogun") == 0
				|| _stricmp(ge->archive, "7ordi") == 0)))) ? 1 : 0;
		const int angelkds = (ge && (
			(ge->subtype[0] && (_stricmp(ge->subtype, "angelkds") == 0
				|| _stricmp(ge->subtype, "spcpostn") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "angelkds") == 0
				|| _stricmp(ge->archive, "spcpostn") == 0)))) ? 1 : 0;
		const int deniam = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "deniam16b") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "logicpro") == 0
				|| _stricmp(ge->archive, "karianx") == 0)))) ? 1 : 0;
		const int lastduel = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "lastduel") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "lastduel") == 0))) ? 1 : 0;
		const int madgear = (ge && (
			(ge->subtype[0] && _stricmp(ge->subtype, "madgear") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "madgear") == 0
				|| _stricmp(ge->archive, "ledstorm") == 0)))) ? 1 : 0;
		const int mgakuen = (ge && (
			(ge->subtype[0] && (_stricmp(ge->subtype, "pang") == 0
				|| _stricmp(ge->subtype, "mgakuen") == 0))
			|| (ge->archive[0] && (_stricmp(ge->archive, "mgakuen") == 0
				|| _stricmp(ge->archive, "mgakuen2") == 0
				|| _stricmp(ge->archive, "mgakuenh") == 0
				|| _stricmp(ge->archive, "marukin") == 0
				|| _stricmp(ge->archive, "marukina") == 0)))) ? 1 : 0;
		if (tharrier) {
			/* MAME nmk16 tharrier/manybloc: Z80 4.9152/3 MHz、YM2203 12/8=1.5 MHz、OKI 4 MHz PIN7 Low。 */
			vsIoKind_ = 2;
			cpuHz_ = (ge && ge->archive[0] && _stricmp(ge->archive, "manybloc") == 0)
				? 3000000 : 4915200;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(4000000u / 165u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(4000000u / 165u, sampleRate_);
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else if (airbustr) {
			/* MAME airbustr: Z80 12/2=6 MHz、YM2203 12/4=3 MHz、OKI 3 MHz PIN7 Low。ラッチ NMI、YM IRQ0。 */
			vsIoKind_ = 3;
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm2608Create(3000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(3000000u / 165u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
		} else if (djboy) {
			/* MAME djboy: Z80 12/2=6 MHz、YM2203 12/4=3 MHz、OKI×2 12/8=1.5 MHz PIN7 Low。ラッチ NMI、YM IRQ0。 */
			vsIoKind_ = 4;
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm2608Create(3000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1500000u / 165u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(1500000u / 165u, sampleRate_);
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
		} else if (blazeon) {
			/* MAME blazeon: Z80 4 MHz、YM2151 4 MHz。ラッチ NMI。YM irq 未接続。ROM 0000-BFFF 固定。 */
			vsIoKind_ = 5;
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2151Create(4000000u, sampleRate_);
			mainIsYm2203_ = 0;
			chip2_ = NULL;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (hvyunit) {
			/* MAME hvyunit: Z80 12/2=6 MHz、YM2203 12/4=3 MHz。ラッチ NMI、YM IRQ0。RAM C000-C7FF、バンク I/O 00 & 3。 */
			vsIoKind_ = 6;
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
			chip_ = CEmuChipYm2608Create(3000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			auxKind_ = 0;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
		} else if (crospang) {
			/* MAME crospang: Z80 3.579545 MHz、YM3812 同、OKI 14.318MHz/16 PIN7 High。ラッチ poll、YM IRQ0。 */
			vsIoKind_ = 7;
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
			chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
			mainIsYm2203_ = 0;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(894886u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (empcity) {
			/* MAME stfight cpu2_map: Z80 12/4=3 MHz、YM2203×2 12/8=1.5 MHz。ラッチ F000 bit7、周期 120 Hz IRQ0。 */
			vsIoKind_ = 8;
			cpuHz_ = 3000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (nmg5 || pclubys) {
			/* MAME nmg5/yunsun16: Z80 4 MHz、YM3812 4 MHz、OKI 1 MHz PIN7 High。ラッチ NMI、YM IRQ0。 */
			vsIoKind_ = pclubys ? 10 : 9;
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
			mainIsYm2203_ = 0;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
		} else if (angelkds) {
			/* MAME angelkds: Z80 4 MHz、YM2203×2 4 MHz。4 ニブルメールボックス 80-83。YM1 irq → IRQ0。 */
			vsIoKind_ = 11;
			cpuHz_ = 4000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			chip2_ = CEmuChipYm2608Create(4000000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			raizingLatch_[0] = raizingLatch_[1] = 0;
			raizingLatchOut_[0] = raizingLatchOut_[1] = 0;
		} else if (deniam) {
			/* MAME deniam16b: Z80 6.25 MHz、YM3812 4.167 MHz、OKI 1.042 MHz PIN7 High。ラッチ NMI、YM IRQ0。 */
			vsIoKind_ = 12;
			cpuHz_ = 6250000;
			opmHz_ = 4166666;
			chip_ = CEmuChipYm3812Create(4166666u, sampleRate_);
			mainIsYm2203_ = 0;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1041666u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
		} else if (lastduel) {
			/* MAME lastduel sound_map: Z80/YM2203×2 3.579545。ラッチ poll、YM1 irq→IRQ0。 */
			vsIoKind_ = 13;
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
			chip_ = CEmuChipYm2608Create(3579545u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(3579545u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (madgear) {
			/* MAME madgear_sound_map: Z80/YM2203×2 3.579545、OKI 1 MHz PIN7 High。 */
			vsIoKind_ = 14;
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
			chip_ = CEmuChipYm2608Create(3579545u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(3579545u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0;
			pcm_ = CEmuChipOki6295Create(1000000u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
			bank_ = 0;
		} else if (mgakuen) {
			/* MAME mitchell mgakuen: Z80 16/2=8 MHz、YM2413 16/4=4 MHz、OKI 16/16=1 MHz PIN7 High。
			   I/O 03=YM data、04=YM addr、05=OKI、02=バンク。IRQ0 2×frame。OPLL は sizeof 外。 */
			vsIoKind_ = 15;
			cpuHz_ = 8000000;
			opmHz_ = 4000000;
			chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
			mainIsYm2203_ = 0;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
			pcm2_ = NULL;
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
			bank_ = 0;
			alphaOpllAddr_ = 0;
			alphaPaLatch_ = 0;
			if (alphaOpll_) {
				OPLL_delete((OPLL*)alphaOpll_);
				alphaOpll_ = NULL;
			}
			{
				OPLL* o = OPLL_new(4000000u, (uint32_t)sampleRate_);
				if (o) {
					OPLL_set_quality(o, 1);
					OPLL_reset_patch(o, 0);
					alphaOpll_ = (void*)o;
				}
			}
		} else if (macross2) {
			/* MAME nmk16 macross2/tdragon2: Z80 4 MHz、YM2203 12/8=1.5 MHz、OKI 16/4=4 MHz PIN7 Low、NMK112。 */
			vsIoKind_ = 1;
			cpuHz_ = 4000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = NULL;
			pcm_ = CEmuChipOki6295Create(4000000u / 165u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(4000000u / 165u, sampleRate_);
			pcmKind_ = 2;
			auxKind_ = 0;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
			memset(raizingOkiBank_, 0, sizeof(raizingOkiBank_));
			CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		} else {
			/* MAME ninjakd2/robokid: Z80 @ 5 MHz、I/O 00/01 と 80/81 の YM2203×2 @ 1.5 MHz。ラッチ @ E000。
			   ninjakd2 音源 CPU は MC8123。zip の .key + 64K 暗号 ROM を qsKabuki 二面に載せる。 */
			cpuHz_ = 5000000;
			opmHz_ = 1500000;
			chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			mainIsYm2203_ = 1;
			chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
			auxKind_ = 0; /* chip2 は OPN — Render は Chip2 経路で加算 */
			pcm_ = NULL;
			pcmKind_ = 0;
		}
	} else if (board_ == CEMU_AC_BOARD_BATTLANTIS) {
		/* MAME battlnts: Z80 + YM3812×2 @ A000 / C000、ラッチ @ E000→IRQ0 */
		cpuHz_ = 3579545;
		opmHz_ = 3000000;
		chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
		chip2_ = CEmuChipYm3812Create(3000000u, sampleRate_);
		auxKind_ = 3; /* chip2 用に OPL を破棄 */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_T5182) {
		/* MAME t5182.cpp: 内部 Z80 3.579545 + YM2151 3.579545。PCM 無し。
		   vsIoKind_=1 は darkmist/panicr 外部 ROM のデータ線入れ替え（CPU D1↔ROM D6 …）。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create(3579545u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		vsIoKind_ = 0;
		if (ge) {
			if ((ge->subtype[0] && _stricmp(ge->subtype, "darkmist") == 0)
				|| (ge->archive[0] && _strnicmp(ge->archive, "darkmist", 8) == 0))
				vsIoKind_ = 1;
		}
	} else if (board_ == CEMU_AC_BOARD_HEBERPOP) {
		/* MAME shangha3.cpp heberpop: Z80 48/8=6 MHz、YM3438 48/6=8 MHz、OKI 1.056 MHz PIN7 High。 */
		cpuHz_ = 6000000;
		opmHz_ = 8000000;
		chip_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		mainIsYm2612_ = 1;
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
		pcm2_ = NULL;
		pcmKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_BIONICC) {
		/* MAME bionicc: Z80 / YM2151 14.31818/4。MCU はラッチ+NMI に畳む。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create(3579545u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* Seibu 音源: SEI80BU 暗号化 Z80。既定は YM3812+OKI1（raiden/heatbrl/cupsoc）。
		   raiden2/raidndx は MAME raiden2_sound_map: YM2151 28.636/8 + OKI×2 28.636/28 PIN7 High。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip2_ = NULL;
		seibuEnc_ = 1;
		if (CEmuAcIsRaiden2(ge)) {
			seibuSongOr80_ = 2;
			chip_ = CEmuChipYm2151Create(3579545u, sampleRate_);
			pcm_ = CEmuChipOki6295Create(28636363u / 28u, sampleRate_);
			pcm2_ = CEmuChipOki6295Create(28636363u / 28u, sampleRate_);
		} else if (CEmuAcIsCabal(ge)) {
			/* MAME cabal: YM2151 3.579545。MSM5205 は SFX — BGM は OPM。 */
			seibuSongOr80_ = 5;
			chip_ = CEmuChipYm2151Create(3579545u, sampleRate_);
			pcm_ = NULL;
			pcm2_ = NULL;
			pcmKind_ = 0;
		} else if (CEmuAcIsSeibuYm2151(ge)) {
			/* MAME legionna godzilla / dcon sdgndmps: YM2151 14.31818/4 + OKI 1.056 MHz PIN7 High。 */
			seibuSongOr80_ = (ge->archive && _stricmp(ge->archive, "sdgndmps") == 0) ? 4 : 3;
			chip_ = CEmuChipYm2151Create(3579545u, sampleRate_);
			pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
			pcm2_ = NULL;
		} else {
			chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
			pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
			pcm2_ = NULL;
			/* Raiden の FM 表は 0x80|catalog。cupsoc 他は固定 0x8e */
			seibuSongOr80_ = (ge && ge->archive
				&& _strnicmp(ge->archive, "raiden", 6) == 0) ? 1 : 0;
		}
		pcmKind_ = (seibuSongOr80_ == 5) ? 0 : 2;
	} else if (board_ == CEMU_AC_BOARD_SEGA_SCSP) {
		/* 初期 Model 2 / Model 1 音源ダンプ（daytona/vf）は MultiPCM+YM3438。Model 2A/3 は SCSP。subtype model2（model2a ではない）で MultiPCM を検出。 */
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
		cpuHz_ = 16000000;
		opmHz_ = 16934400;
		chip_ = CEmuChipRf5c400Create(16934400u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		hornetGti_ = (_stricmp(ge->subtype, "gticlub") == 0) ? 1 : 0;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		const int isK054539 = (_stricmp(ge->subtype, "054539") == 0
			|| _stricmp(ge->subtype, "054539x2") == 0
			|| CEmuAcIsKonamiK054539Sub(ge->subtype) || hasK054539
			|| CEmuAcIsPrmrsocr(ge)) ? 1 : 0;
		/* mystwarr.cpp sound_map は同じ 4 MB サンプル ROM 上で K054539 #1 を E000-E22F、#2 を E400-E62F。単チップ基板（bucky/moomesa/xexex）は最初の窓だけ埋める。 */
		const int isK054539x2 = (_stricmp(ge->subtype, "054539x2") == 0) ? 1 : 0;
		/* K053260（parodius/simpsons/…）: Z80+YM2151+K053260 @ 3.579545 MHz。カタログ既定 YM@F800 / PCM@FC00（ssriders 級は FA00）。Bucky/Moo/X-Men K054539: YM@EC00、PCM@E000、バンク@F800、K054321@F000。 */
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
		if (CEmuAcIsGijoeLethal(ge)) {
			/* MAME gijoe.cpp / lethal.cpp sound_map: ROM 0000-EFFF、RAM F000-F7FF、
			   K054539 F800-FA2F、K054321 FC00-FC03。YM/バンク無し。タイマ→NMI。 */
			konamiOpmAddr_ = 0xffffu;
			konamiPcmAddr_ = 0xf800u;
			konamiBankAddr_ = 0;
			konamiPcmWindow_ = 0x230u;
			cpuHz_ = CEmuAcIsGijoe(ge) ? 8000000 : 6000000;
			opmHz_ = 4000000;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (CEmuAcIsXmen(ge)) {
			/* MAME xmen.cpp sound_map: ROM 0000-7FFF、16K バンク 8000-BFFF×8、RAM C000-DFFF、
			   K054539 E000、YM2151 E800（EC00 ミラー）、K054321 F000、bank F800。Z80 8 MHz。タイマ NMI 無し。 */
			konamiOpmAddr_ = 0xe800u;
			konamiPcmAddr_ = 0xe000u;
			konamiBankAddr_ = 0xf800u;
			konamiPcmWindow_ = 0x230u;
			cpuHz_ = 8000000;
			opmHz_ = 4000000;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
		} else if (CEmuAcIsPrmrsocr(ge)) {
			/* MAME tmnt2.cpp prmrsocr_audio_map: ROM 0000-7FFF、16K×8 バンク 8000、RAM C000、
			   K054539 E000（offset 0x100 を 0x200 へ）、K054321 F000、bank F800。YM 無し。タイマ NMI。Z80 8 MHz。 */
			konamiOpmAddr_ = 0xffffu;
			konamiPcmAddr_ = 0xe000u;
			konamiBankAddr_ = 0xf800u;
			konamiPcmWindow_ = 0x230u;
			cpuHz_ = 8000000;
			opmHz_ = 4000000;
			bankBase_ = 0x8000u;
			bankSize_ = 0x4000u;
		} else if (CEmuAcIsGlfgreat(ge)) {
			/* MAME tmnt2.cpp glfgreat_audio_map: ROM 0000-7FFF、RAM F000-F7FF、K053260 F800-F82F、
			   NMI arm FA00。YM 無し。TIM2 500 Hz HOLD_LINE IRQ0。Z80 3.58 MHz。 */
			konamiOpmAddr_ = 0xffffu;
			konamiPcmAddr_ = 0xf800u;
			konamiBankAddr_ = 0;
			konamiPcmWindow_ = 0x40u;
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
			bankBase_ = 0;
			bankSize_ = 0;
		} else if (CEmuAcIsRollerg(ge)) {
			/* MAME rollerg.cpp sound_map: ROM 0000-7FFF、RAM 8000-87FF、K053260 A000-A02F、
			   YM3812 C000/C001、NMI arm FC00。IRQ0 HOLD_LINE。Z80/YM/PCM 3.58 MHz。 */
			konamiOpmAddr_ = 0xc000u;
			konamiPcmAddr_ = 0xa000u;
			konamiBankAddr_ = 0;
			konamiPcmWindow_ = 0x40u;
			cpuHz_ = 3579545;
			opmHz_ = 3579545;
			bankBase_ = 0;
			bankSize_ = 0;
		}
		if (hasOpm || isK054539 || CEmuAcIsGlfgreat(ge) || CEmuAcIsRollerg(ge)) {
			if (CEmuAcIsRollerg(ge)) {
				chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
				pcm_ = CEmuChipK053260Create(3579545u, sampleRate_);
				pcmKind_ = 3;
			} else if (isK054539) {
				chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
				pcm_ = CEmuChipK054539Create(18432000u, sampleRate_);
				pcmKind_ = 4;
				if (isK054539x2) {
					pcm2_ = CEmuChipK054539Create(18432000u, sampleRate_);
					konamiPcm2Addr_ = CEmuAcOptionValue(ge, "pcm2_addr", 0xe400u);
					CEmuChipK054539SetFmMonBase(pcm_, 0);
					CEmuChipK054539SetFmMonBase(pcm2_, 8);
				}
			} else {
				chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
				pcm_ = CEmuChipK053260Create(3579545u, sampleRate_);
				pcmKind_ = 3;
			}
		} else {
			chip_ = CEmuChipK053260Create(3579545u, sampleRate_);
			pcmKind_ = 3;
		}
		chip2_ = NULL;
	} else if (board_ == CEMU_AC_BOARD_SYS32) {
		/* MAME segas32: Z80 @ 8 MHz。system32 は YM3438×2 + RF5C68（12.5 MHz）。
		   system_multi は YM3438×1 + MultiPCM 10 MHz（40 MHz/4）。 */
		cpuHz_ = 8000000;
		opmHz_ = 8000000;
		const int multi = (ge && ge->subtype
			&& (_stricmp(ge->subtype, "system_multi") == 0
				|| _stricmp(ge->subtype, "multi32") == 0)) ? 1 : 0;
		vsIoKind_ = 0;
		memset(s32IrqCtrl_, 0, sizeof(s32IrqCtrl_));
		s32IrqIn_ = 0;
		s32Bank_ = 0;
		s32YmIrqPrev_ = 0;
		chip_ = CEmuChipYm2612Create(8000000u, sampleRate_);
		mainIsYm2612_ = 1;
		if (multi) {
			vsIoKind_ = 1;
			if (ge->archive && _stricmp(ge->archive, "scross") == 0)
				vsIoKind_ = 2;
			chip2_ = NULL;
			auxKind_ = 0;
			pcm_ = CEmuChipMultiPcmCreate(10000000u, sampleRate_, 0);
			pcmKind_ = 10;
		} else {
			chip2_ = CEmuChipYm2612Create(8000000u, sampleRate_);
			auxKind_ = 5;
			pcm_ = CEmuChipRf5c68Create(12500000u, sampleRate_);
			pcmKind_ = 5;
		}
	} else if (board_ == CEMU_AC_BOARD_OUTRUN || board_ == CEMU_AC_BOARD_ABURNER) {
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipSegaPcmCreate(4000000u, sampleRate_, 12u, 0x70u);
		pcmKind_ = 1;
		/* ABURNER vsIoKind_: 0=After Burner（NMI ラッチ、YM は BIT0 ポール）。1=smgp RST38 CALL $0A38。2=lastsurv RST38 が YM wait と重なる。 */
		if (board_ == CEMU_AC_BOARD_ABURNER) {
			if (CEmuAcIsSmgp(ge))
				vsIoKind_ = 1;
			else if (CEmuAcIsLastsurv(ge))
				vsIoKind_ = 2;
			else
				vsIoKind_ = 0;
		}
	} else if (board_ == CEMU_AC_BOARD_CPS1) {
		/* MAME cps1: Z80+YM2151 @ 3.579545 MHz、OKI @ 1 MHz PIN7 HIGH。
		   Timer A は 64*(1024-0xC8<<2)/clock ≈ 249.7 Hz（sf2 ISR が毎割込 $10=$C8）。
		   QSound 250 Hz と同じ時基。4 MHz だと 279 Hz。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1000000u / 132u, sampleRate_);
		pcmKind_ = 2;
		bankBase_ = 0x8000u;
		bankSize_ = 0x4000u;
	} else {
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		if (hasSegaPcm) {
			pcm_ = CEmuChipSegaPcmCreate(4000000u, sampleRate_, 12u, 0x70u);
			pcmKind_ = 1;
		}
	}
	cpu_ = new Ay_Cpu();
	opmWrites_ = 0;
	return (chip_ && cpu_) ? 1 : 0;
}

/* チップ／CPU／ROM を破棄する */
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
		/* chip3 は AY（SJ #3 / msisaac AY2）。starforce だけ SN×3 の 3 本目。auxKind_ は使わない（FLSTORY msisaac は MSM chip2 用に auxKind_=4 を保つ）。 */
		if (vsIoKind_ == 18 || vsIoKind_ == 21)
			CEmuChipSn76489Destroy(chip3_);
		else
			CEmuChipAyDestroy(chip3_);
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

/* OPLL レジスタを CHardAc の外に置き、ファクトリ sizeof を動かさない */
static uint8_t s_alphaOpllRegs[64];
static unsigned s_alphaOpllWrites;

/* CHardAc::AlphaOpllWrites の実装 */
unsigned CHardAc::AlphaOpllWrites() const
{
	return s_alphaOpllWrites;
}

/* CHardAc::AlphaMixOpll の実装 */
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

/* ---- Taito TC0140SYT / PC060HA の実装（MAME src/mame/shared/taitosnd.cpp） ---- */

uint8_t CHardAc::KonamiAyTimer() const
{
	/* MAME timeplt_a / scramble portB_r: 音源 CPU total_cycles からの 2 進 5 進 /5120。コア走行中は Z80 time() を優先し、AddCpuCycles が遅れても音楽の bit7 待ちループが進む。 */
	static const uint8_t kTimer[10] = {
		0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0
	};
	const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time64() : cpuCycles_;
	return kTimer[(cyc / 512ull) % 10ull];
}

/* CHardAc::Gx400PortA の実装 */
uint8_t CHardAc::Gx400PortA() const
{
	/* MAME gx400_state::nemesis_portA_r — bit2 は欠ける 68000「フレーム」クロックとしてトグルし音源メインループ（wait clear→set @029C）が走れる。bits4/6/7 は High（0xD0）。 */
	const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time64() : cpuCycles_;
	return (uint8_t)(((cyc / 512ull) & 0x0full) | 0xd0u);
}

/* CHardAc::SytUpdateNmi の実装 */
void CHardAc::SytUpdateNmi()
{
	/* MAME のステータスビットフィールドで PORT01_FULL = 0x01、PORT23_FULL = 0x02 */
	const int pending = (sytStatus_ & 0x03) != 0;
	if (pending && sytNmiEnabled_)
		irqPulse_ = 1;
}

/* バス書込 */
void CHardAc::SytMasterWriteCommand(uint8_t cmd)
{
	/* マスタ側: port_w(0)、comm_w（下位ニブル）、port_w(1)、comm_w（上位ニブル）。Taito YM2610 ゲームの 68000 ドライバはすべて下位ニブルを先に送る。 */
	sytMainMode_ = 0;
	sytSlaveData_[0] = (uint8_t)(cmd & 0x0f);
	sytMainMode_ = 1;
	sytSlaveData_[1] = (uint8_t)((cmd >> 4) & 0x0f);
	sytMainMode_ = 2;
	sytStatus_ |= 0x01; /* PORT01_FULL */
	/* East Tech（gigandes/ballbros）: NMI $0066 は RETI。0071 が PORT01 を見て 00C5 が PORT23 で曲番号を組む。
	   1 バイトだけだと 00C5 が status bit1 待ちで回る。MAME taitosnd は mode2/3 で PORT23_FULL。 */
	if (mem_[0x66] == 0xed && mem_[0x67] == 0x4d
		&& mem_[0x71] == 0x3e && mem_[0x73] == 0x32
		&& mem_[0x74] == 0x00 && mem_[0x75] == 0xe2) {
		sytSlaveData_[2] = (uint8_t)(cmd & 0x0f);
		sytSlaveData_[3] = (uint8_t)((cmd >> 4) & 0x0f);
		sytMainMode_ = 4;
		sytStatus_ |= 0x02; /* PORT23_FULL */
	}
	/* Magical Date EX: NMI は RETN。メイン CALL $0100 が SYT status AND 03; CP 03 なので PORT01+PORT23 両方が要る。 */
	if (mem_[0] == 0xf3 && mem_[3] == 0xc3 && mem_[4] == 0x26 && mem_[5] == 0x01
		&& mem_[0x100] == 0x3e && mem_[0x101] == 0x04
		&& mem_[0x10a] == 0xfe && mem_[0x10b] == 0x03) {
		sytSlaveData_[2] = (uint8_t)(cmd & 0x0f);
		sytSlaveData_[3] = (uint8_t)((cmd >> 4) & 0x0f);
		sytMainMode_ = 4;
		sytStatus_ |= 0x02;
	}
	SytUpdateNmi();
}

/* CHardAc::SytSlavePortW の実装 */
void CHardAc::SytSlavePortW(uint8_t data)
{
	sytSubMode_ = (uint8_t)(data & 0x0f);
}

/* CHardAc::SytSlaveCommW の実装 */
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

/* CHardAc::SytSlaveCommR の実装 */
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
		/* MAME は master_comm_r で *_FULL_MASTER ビットをクリアし、Taito 68000 ドライバはそのメールボックスを連続 poll。CEmu は音源サブシステムだけ走るので誰もドレインせずセットのまま固まった。Rastan は 01E4 で bit 2 を試し、毎回ハンドシェイクから抜けた。マスタは既に消費済みと報告。 */
		sytStatus_ = (uint8_t)(sytStatus_ & ~0x0c);
		res = sytStatus_;
		break;
	default:
		break;
	}
	return res;
}

/* CHardAc::SetBank の実装 */
void CHardAc::SetBank(int bank)
{
	if (!soundRom_) return;
	if (!bankSize_) return;
	/* Sys16A/B バンクは I/O ポート 40（5358 / 5797）。この 16K 窓ヘルパではない。既定 bankBase_=0x4000 は固定 ROM 4000-7FFF を壊す。 */
	if (board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
		return;
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 14) {
		/* TC0090LVC: 6000-7FFF は 8K ROM バンク（FF08）。固定 0000-5FFF。 */
		if (!soundRom_ || soundRomSize_ < 0x2000u)
			return;
		const unsigned nb = soundRomSize_ >> 13;
		unsigned b = (unsigned)bank & 0x1fu;
		if (nb)
			b %= nb;
		bank_ = (int)b;
		bankLoaded_ = 1;
		const unsigned src = b << 13;
		unsigned n = 0x2000u;
		if (src + n > soundRomSize_)
			n = soundRomSize_ - src;
		memcpy(mem_ + 0x6000, soundRom_ + src, n);
		if (n < 0x2000u)
			memset(mem_ + 0x6000 + n, 0xff, 0x2000u - n);
		return;
	}
	/* CPS2 QSound: Z80 8000-BFFF は audiocpu+0x8000 からバンク（FBNeo 配置。MAME は領域 8000-FFFF に穴を残し同じバイトが +0x10000 に居る）。 */
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
	/* K053260 Simpsons 級: MAME audiobank 項目 0..2 → +0x10000、3..7 → +0x10000 + (i-2)*0x4000 */
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
	/* CPS1: MAME membank("bank1")->configure_entries(0, n, &audiocpu[0x10000], 0x4000);
	   cps1_snd_bankswitch_w は data&1 のみ。64K ファイルは 32K 固定@0000 + CONTINUE 32K@10000。
	   汎用 src=bank*0x4000 だと偶数=ROM先頭・奇数=固定 4000 を 8000 へ載せ、II テーマ（論理 C000=bank1）が無音または先頭曲ループになる。 */
	if (board_ == CEMU_AC_BOARD_CPS1) {
		const int b = bank & 1;
		if (soundRomSize_ < 0x14000u)
			return;
		const unsigned src = 0x10000u + (unsigned)b * 0x4000u;
		if (src >= soundRomSize_)
			return;
		if (b == bank_ && bankLoaded_)
			return;
		bank_ = b;
		bankLoaded_ = 1;
		unsigned n = 0x4000u;
		if (src + n > soundRomSize_)
			n = soundRomSize_ - src;
		memset(mem_ + 0x8000, 0xff, 0x4000);
		if (n) memcpy(mem_ + 0x8000, soundRom_ + src, n);
		return;
	}
	/* pipedrm: イメージは 32K Z80@0000 + 64K バンク@10000。汎用 src=bank*8000 は Z80 先頭を
	   8000-FFFF に載せ、BGM シーケンサ（バンク ROM）を消して FM が SILENT・SFX ADPCM だけ残る。 */
	if (board_ == CEMU_AC_BOARD_VSYSTEM && vsIoKind_ == 4) {
		if (soundRomSize_ < 0x18000u)
			return;
		unsigned pages = (soundRomSize_ - 0x10000u) / 0x8000u;
		if (pages == 0)
			return;
		const int b = (int)((unsigned)bank & 1u);
		/* ブートが 7800-80FF をゼロ埋めしたあと OUT (04),0 で窓を載せ直す。LoadRoms 時点の blit をキャッシュするとヘッダ 0x100 が消えたまま。 */
		bank_ = b;
		bankLoaded_ = 1;
		const unsigned src = 0x10000u + (unsigned)b * 0x8000u;
		unsigned n = 0x8000u;
		if (src + n > soundRomSize_)
			n = soundRomSize_ - src;
		memset(mem_ + 0x8000, 0xff, 0x8000);
		if (n)
			memcpy(mem_ + 0x8000, soundRom_ + src, n);
		return;
	}
	/* MAME taito_f2 machine_start: configure_entry(i, base + 0x4000 * (i % banks))。aerofgt: soundbank->configure_entries(0, 4, base, 0x8000)。形は同じ、窓サイズが違う。 */
	if (soundRomSize_ <= bankSize_) return;
	const unsigned banks = soundRomSize_ / bankSize_;
	if (banks == 0) return;
	const int want = bank % (int)banks;
	/* Rastan はほぼ毎 YM2151 reg-1B 書で同じバンクを再選択。書毎 16K memcpy は観測効果無しで実行を支配する。 */
	if (want == bank_ && bankLoaded_) return;
	bank_ = want;
	bankLoaded_ = 1;
	const unsigned src = (unsigned)bank_ * bankSize_;
	unsigned n = bankSize_;
	if (src + n > soundRomSize_) n = soundRomSize_ - src;
	/* Psikyo gunbird: RAM 8000-81FF が 32K バンク窓に重なる（MAME gunbird_sound_map）。復元すると OUT (00) バンク書をまたいでシーケンサ BSS が残る。blit 重ねは全曲を同じドローンにした。 */
	uint8_t ramKeep[0x200];
	const int keepGunbirdRam = (board_ == CEMU_AC_BOARD_VSYSTEM && vsIoKind_ == 3);
	if (keepGunbirdRam)
		memcpy(ramKeep, mem_ + 0x8000, sizeof(ramKeep));
	memset(mem_ + bankBase_, 0xff, bankSize_);
	if (n) memcpy(mem_ + bankBase_, soundRom_ + src, n);
	if (keepGunbirdRam)
		memcpy(mem_ + 0x8000, ramKeep, sizeof(ramKeep));
}

/* CHardAc::M72PumpSample の実装 */
void CHardAc::M72PumpSample()
{
	if (!pcm_ || !pcmRom_ || m72SampleAddr_ >= pcmRomSize_) return;
	const uint8_t s = pcmRom_[m72SampleAddr_];
	if (!s) return; /* 00 はサンプル終端 */
	pcm_->Write(0, s);
	m72SampleAddr_++;
}

/* ---- Jaleco Mega System 1 音源基板（MAME megasys1A/B_sound_map） ----
     000000-01FFFF  ROM（ロード済みイメージ全体へマップ）
     040000-040001  soundlatch[0] 読（メイン→音源）。sys B では soundlatch[1] 書
     060000-060001  soundlatch[1] 書（音源→メイン）。sys B では読も
     080000-080003  YM2151、umask16(0x00ff) → 下位バイトレーン
     0A0000-0A0003  OKI6295 #0 書、0A0001 読 = ステータス
     0C0000-0C0003  OKI6295 #1 書、0C0001 読 = ステータス
     0E0000-0EFFFF  RAM、ミラー 0x10000

   Konami System GX（MAME konamigx.cpp gxsndmap）は Ms1* 入口を共有:
     000000-03FFFF  ROM
     100000-10FFFF  RAM
     200000-2004FF  K054539×2（上位=chip_#1、下位=pcm_#2）
     300001         TMS57002 データ stub
     400000-40001F  K056800 音源側（umask 0x00ff → 奇数バイト）
     500000-500001  TMS57002 ステータス／制御（bit0 下降で IRQ2 クリア）
     580000         nop                                                      */

static unsigned CEmuGxK056800Off(unsigned addr)
{
	/* 下位バイトレーン上のバイトオフセット → デバイスオフセット (addr>>1) & 7 */
	return ((addr >> 1) & 7u);
}

/* バス読込 */
unsigned CHardAc::Ms1Read16(unsigned addr)
{
	if (board_ == CEMU_AC_BOARD_M68K_PCM) {
		unsigned v = (M68kPcmRead8(addr) << 8) | M68kPcmRead8(addr + 1);
		/* ヒストグラムは作業 RAM 窓の先頭 64 KiB だけ。atehate の窓は 1 MiB なので配列末尾を越さずクランプ。 */
		if (g_traceM68kRead && m68kRamSize_ && addr >= m68kRamAddr_ && addr - m68kRamAddr_ < m68kRamSize_) {
			unsigned off = addr - m68kRamAddr_;
			if (off + 1 < _countof(g_m68kReadCount)) {
				g_m68kReadCount[off]++;
				g_m68kReadCount[off + 1]++;
			}
		}
		return v;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP)
		return segaM1Audio_ ? Sega68Read16(addr) : Sega2ARead16(addr);
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400)
		return HornetRead16(addr);
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
			/* MAME umask16: ワードオフセットがレジスタを選ぶ。上位バイト → chip_#1、下位バイト → pcm_#2 */
			const unsigned off = (addr - 0x200000u) >> 1;
			const unsigned hi = chip_ ? CEmuChipK054539PeekReg(chip_, off) : 0;
			const unsigned lo = pcm_ ? CEmuChipK054539PeekReg(pcm_, off) : 0;
			return (hi << 8) | lo;
		}
		if (addr == 0x500000u)
			return gxTmsStatus_;
		/* K056800 / TMS データは Read8 経由の下位バイトだけ見える */
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

/* Seta/Cave: プログラム ROM は 0 から。PCM チップと作業 RAM は実基板がデコードする窓。他はオープンバス読 — これらのプログラムは映像／入力レジスタを常時探り、RAM 内容で答えると POST 失敗経路へ落ちた。 */
unsigned CHardAc::M68kPcmRead8(unsigned addr)
{
	addr &= 0xffffffu;
	if (m68kPcmSpan_ && addr - m68kPcmAddr_ < m68kPcmSpan_) {
		if (!chip_) return 0;
		const unsigned off = addr - m68kPcmAddr_;
		/* 両チップは下位バイトレーンだけ応答 */
		if (!(addr & 1))
			return (m68kPcmKind_ == 1) ? m68kHiWord_[(off >> 1) & 0x1fffu] : 0;
		if (m68kPcmKind_ == 2)
			return CEmuChipYmz280bReadStatus(chip_);
		/* MAME x1_010_device::word_r — ワードオフセットがレジスタ添字そのものなので 16 KiB 窓がチップの 8 KiB レジスタファイルをマップ。 */
		return CEmuChipX1010Read(chip_, off >> 1);
	}
	/* MAME cave_state::irq_cause_r: 保留中ソースのビットを Low にした 0x0003。+4 / +6 読がそのソースを ACK。 */
	if (m68kIrqAddr_ && addr - m68kIrqAddr_ < 8u) {
		const unsigned off = addr - m68kIrqAddr_;
		unsigned res = 0x0003u;
		if (m68kVblankPending_) res ^= 0x01u;
		if (off == 5u || off == 4u) {
			m68kVblankPending_ = 0;
			ms1LatchIrq_ = 0;
		}
		return (off & 1) ? (uint8_t)(res & 0xff) : (uint8_t)(res >> 8);
	}
	if (ms1Rom_ && addr < ms1RomSize_) return ms1Rom_[addr];
	if (m68kRamSize_ && addr - m68kRamAddr_ < m68kRamSize_)
		return ms1Ram_ ? ms1Ram_[addr - m68kRamAddr_] : 0xff;
	return 0xff;
}

/* バス書込 */
void CHardAc::M68kPcmWrite8(unsigned addr, uint8_t v)
{
	addr &= 0xffffffu;
	if (m68kPcmSpan_ && addr - m68kPcmAddr_ < m68kPcmSpan_) {
		if (!chip_) return;
		g_m68kPcmWrites++;
		const unsigned off = addr - m68kPcmAddr_;
		if (!(addr & 1)) {
			/* ワード上位バイトは合成器に届かない。MAME は読が返すためだけに側バッファへ残す。 */
			if (m68kPcmKind_ == 1)
				m68kHiWord_[(off >> 1) & 0x1fffu] = v;
			return;
		}
		if (m68kPcmKind_ == 2) {
			/* +1 = レジスタ選択、+3 = データ */
			CEmuChipYmz280bWritePort(chip_, (off & 2) ? 1u : 0u, v);
		} else {
			CEmuChipX1010Write(chip_, off >> 1, v);
		}
		gxPcmWrites_++;
		return;
	}
	if (ms1Rom_ && addr < ms1RomSize_) return;
	if (m68kRamSize_ && addr - m68kRamAddr_ < m68kRamSize_) {
		if (ms1Ram_) ms1Ram_[addr - m68kRamAddr_] = v;
		return;
	}
}

/* CHardAc::M68kPcmTickVblank の実装 */
void CHardAc::M68kPcmTickVblank(int cycles)
{
	if (board_ != CEMU_AC_BOARD_M68K_PCM || cycles <= 0) return;
	m68kVblankAcc_ += (uint64_t)cycles;
	const uint64_t period = (uint64_t)(cpuHz_ > 0 ? cpuHz_ : 16000000) / 60u;
	if (period && m68kVblankAcc_ >= period) {
		m68kVblankAcc_ %= period;
		m68kVblankPending_ = 1;
		ms1LatchIrq_ = 1;
	}
}

int g_traceM68kRead = 0;
int g_m68kReadCount[0x10000];
int g_m68kPcmWrites = 0;

/* バス読込 */
unsigned CHardAc::Ms1Read8(unsigned addr)
{
	if (board_ == CEMU_AC_BOARD_M68K_PCM) {
		unsigned v = M68kPcmRead8(addr);
		if (g_traceM68kRead && m68kRamSize_ && addr >= m68kRamAddr_ && addr - m68kRamAddr_ < m68kRamSize_) {
			unsigned off = addr - m68kRamAddr_;
			if (off < _countof(g_m68kReadCount))
				g_m68kReadCount[off]++;
		}
		return v;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP)
		return segaM1Audio_ ? Sega68Read8(addr) : Sega2ARead8(addr);
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400)
		return HornetRead8(addr);
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
			return 0x00; /* TMS57002 データ stub */
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

/* バス書込 */
void CHardAc::Ms1Write16(unsigned addr, uint16_t v)
{
	if (board_ == CEMU_AC_BOARD_M68K_PCM) {
		M68kPcmWrite8(addr & ~1u, (uint8_t)(v >> 8));
		M68kPcmWrite8((addr & ~1u) | 1u, (uint8_t)v);
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP) {
		if (segaM1Audio_)
			Sega68Write16(addr, v);
		else
			Sega2AWrite16(addr, v);
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		HornetWrite16(addr, v);
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
			/* 下位バイトが制御語（umask はしばしば 0x00ff） */
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
		ms1LatchOut_ = v; /* メイン CPU 向け — こちらでは誰も聞いていない */
		return;
	}
	if (page == 0x080000u && chip_) {
		/* 080001 = アドレスラッチ、080003 = データ（下位バイトレーン） */
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

/* バス書込 */
void CHardAc::Ms1Write8(unsigned addr, uint8_t v)
{
	if (board_ == CEMU_AC_BOARD_M68K_PCM) {
		M68kPcmWrite8(addr, v);
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP) {
		if (segaM1Audio_)
			Sega68Write8(addr, v);
		else
			Sega2AWrite8(addr, v);
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		HornetWrite8(addr, v);
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
			return; /* TMS57002 データ stub */
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
	/* ここの周辺はすべて下位バイトレーン。偶数番地バイト書は D15-D8 に着き、何もデコードしない。 */
	if (addr & 1)
		Ms1Write16(addr & ~1u, v);
}

/* IRQ 配送 */
int CHardAc::Ms1IrqLevel() const
{
	if (board_ == CEMU_AC_BOARD_M68K_PCM)
		return ms1LatchIrq_ ? m68kVblankLevel_ : 0;
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return segaMidiIrq_ ? 2 : 0; /* 8251 RxRDY を IRQ2 へ */
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP)
		return CEmuChipScspIrqLevel(); /* SCSP タイマ A/B/C + MIDI 入力 */
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		/* Hornet: 周期タイマ → IRQ1、K056800 メールボックス → IRQ2 */
		int level = 0;
		if (hornetTimerIrq_) level = 1;
		if (k056800Irq_ && level < 2) level = 2;
		return level;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		int level = 0;
		if (k056800Irq_) level = 1;
		/* 制御 bit0 がセットの間だけタイマ → IRQ2（MAME k054539_irq_gen） */
		if (chip_ && chip_->Irq() && (gxSoundCtrl_ & 1) && level < 2)
			level = 2;
		return level;
	}
	int level = ms1LatchIrq_ ? ms1LatchLevel_ : 0;
	/* MAME sound_irq(): YM2151 を set_input_line(4, HOLD_LINE) へ */
	if (chip_ && chip_->Irq() && level < 4) level = 4;
	return level;
}

/* IRQ 配送 */
void CHardAc::Ms1AckIrq()
{
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_)
		return; /* UART FIFO が空になると RxRDY をクリア */
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400
		|| board_ == CEMU_AC_BOARD_KONAMI_GX) {
		/* IRQ 線は各自の ACK ポート / sound_w(4) でクリア */
		return;
	}
	ms1LatchIrq_ = 0;
	if (chip_ && chip_->Irq()) chip_->AckIrq();
}

/* ---- Irem M92 音源基板（MAME irem/m92.cpp sound_map） ----------------
   周辺はすべて下位バイトレーンでデコード（umask16 0x00ff）。ワードアクセスは偶数番地でデバイスへ届き、奇数バイトは捨てる — ここでは奇数周辺バイトを無視してモデル。 */
uint8_t CHardAc::M92Read8(uint32_t addr)
{
	addr &= 0xfffffu;
	if (addr < 0x20000u)
		return (m92Rom_ && addr < m92RomSize_) ? m92Rom_[addr] : 0xff;
	if (addr >= 0xa0000u && addr <= 0xa3fffu)
		return m92Ram_ ? m92Ram_[addr - 0xa0000u] : 0xff;
	if (addr >= 0xa8000u && addr <= 0xa803fu) {
		/* iremga20_device::read — offset&7 == 7 がボイス活性旗 */
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
	/* FFFF0-FFFFF は音源 ROM 末尾をミラーし、SFR と衝突せずリセットベクタが IDB ページ内に着く */
	if (addr >= 0xffff0u)
		return (m92Rom_ && m92RomSize_ >= 0x20000u) ? m92Rom_[0x1fff0u + (addr & 0xfu)] : 0xff;
	return 0xff;
}

/* バス書込 */
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
		/* generic_latch_8 acknowledge_w — INTP1 要求を落とす */
		m92LatchPending_ = 0;
		if (v35_) V35SetInputLine(v35_, V35_LINE_INTP1, V35_CLEAR_LINE);
		return;
	}
	if (addr == 0xa8046u) {
		m92Latch2_ = v;
		return;
	}
}

/* CHardAc::M92PatchRom の実装 */
int CHardAc::M92PatchRom(uint32_t off, uint8_t v)
{
	if (!m92Rom_ || off >= m92RomSize_) return 0;
	m92Rom_[off] = v;
	return 1;
}

/* IRQ 配送 */
void CHardAc::M92SyncIrqs()
{
	if (!v35_) return;
	/* ym2151.irq_handler → NEC_INPUT_LINE_INTP0 をチップのタイマステータスビットに追従する LEVEL（こちらのエッジラッチではない）。ISR 前にここでラッチを ACK すると AND AL,#03 がクリア状態を見てシーケンサを飛ばす。ラッチを ASSERT のまま残すと INTP0 が永久洪水。 */
	if (chip_) {
		const int pending = (chip_->ReadStatus() & 0x03) != 0;
		V35SetInputLine(v35_, V35_LINE_INTP0,
			pending ? V35_ASSERT_LINE : V35_CLEAR_LINE);
		if (!pending)
			chip_->AckIrq();
	}
	/* soundlatch data_pending → NEC_INPUT_LINE_INTP1。ドライバが A8044 を書くまで保持（separate_acknowledge）。 */
	V35SetInputLine(v35_, V35_LINE_INTP1,
		m92LatchPending_ ? V35_ASSERT_LINE : V35_CLEAR_LINE);
}

/* バンク済みポインタ先頭バイトの Sys2 曲表レコード型を覗く。型 $20 = bit6 メールボックス BGM 経路。他の型は bit6 無しステータスが要る。 */
static int CEmuAcSys2SongInfo(const uint8_t* rom, unsigned romSize, unsigned songLo,
	unsigned* recAddr, uint8_t* hdr, unsigned hdrCap);

/* CHardAc::SetSoundCommandWord の実装 */
void CHardAc::SetSoundCommandWord(uint16_t cmd)
{
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		/* System GX メールボックス: バンク済みタイトル 0x01xx..0x05xx は (lo, hi)。他（tkmmpzdm の 0x06xx ボイス等）は (hi, lo)。 */
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
		Sega2AInjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400) {
		/* カタログコードは 0x01xx0000。16bit 語は上位半分 */
		HornetInjectSong(cmd ? ((unsigned)cmd << 16) : 0u);
		return;
	}
	if ((board_ == CEMU_AC_BOARD_NAMCO_SYS86
			|| (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsg63701_))
		&& HD63701Active()) {
		HD63701InjectSong((uint8_t)(cmd & 0xffu));
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 || board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		/* soundCmdWord_ は既に 16bit id 全体を持つ。SetSoundCommand は soundCmdWord_ から lo/hi を投げる（消さない）。 */
		SetSoundCommand((uint8_t)(cmd & 0xff));
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP && segaM1Audio_) {
		SegaMidiInjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		soundCmdWord_ = cmd;
		soundCmd_ = (uint8_t)(cmd & 0xff);
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	/* Capcom ZN: PSX は FF 00（ボイスリセット）のあと BE 曲語を送る */
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

/* CHardAc::GxHostInject の実装 */
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

/* メイン CPU からのサウンドコマンドをラッチする */
void CHardAc::SetSoundCommand(uint8_t cmd)
{
	if (board_ == CEMU_AC_BOARD_GNG && (gngCommandoMap_ == 2 || gngCommandoMap_ == 3 || gngCommandoMap_ == 5)) {
		/* sidearms: ラッチ D000 poll。tigeroad: ラッチ E000 poll。momoko: YM2 ポートA poll。NMI 無し。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_GNG && gngCommandoMap_ == 4) {
		/* ironhors: ラッチ書 + sh_irqtrigger → Z80 IRQ0 HOLD。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_GNG && gngCommandoMap_ == 6) {
		/* jumping: GENERIC_LATCH_8 data_pending → IRQ0。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_GX) {
		SetSoundCommandWord(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_M68K_PCM) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		/* HACK: サウンドコマンド番地はゲームで違う。トレースが M68K のスピン位置を教える。当面は先頭数バイトへ書いてみる。 */
		if (m68kRamSize_ >= 4 && ms1Ram_) {
			ms1Ram_[0] = cmd;
			ms1Ram_[1] = cmd;
			ms1Ram_[2] = cmd;
			ms1Ram_[3] = cmd;
		}
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
		/* deco_146 / generic_latch_8: pending → HuC6280 IRQ1 または M6502/M6809 NMI。
		   midres（kind 8）は dec0_base: latch → NMI、YM3812 → IRQ1。IRQ1 をラッチに使うと YM と衝突する。
		   matmania（kind 9）: latch → M6502 IRQ。YM は無い。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (h6280_ && decoCpuKind_ == 8)
			H6280SetInputLine(h6280_, H6280_LINE_NMI, H6280_ASSERT_LINE);
		else if (h6280_)
			H6280SetInputLine(h6280_, H6280_LINE_IRQ1, H6280_ASSERT_LINE);
		if (m6502_ && decoCpuKind_ == 9)
			M6502SetInputLine(m6502_, M6502_LINE_IRQ, M6502_ASSERT_LINE);
		else if (m6502_)
			M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_ASSERT_LINE);
		if (namcoM6809_ && (decoCpuKind_ == 6 || decoCpuKind_ == 7))
			NamcoCpuRaw(namcoM6809_)->nmi = true;
		return;
	}
	if (board_ == CEMU_AC_BOARD_ATARI_SYS1) {
		/* atarijsa: soundlatch を 6502 NMI へ */
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
		/* TRI-RAM メールボックス:
		   - blazer: コマンドは $7100、ステータス bit7 は $7101
		   - rompers: 2 バイト枠。$7101 に bits 5-6 が無いと IRQ ハンドラが $7100 から cmd を載せる D1AC 経路を飛ばす
		   - pacmania: 同様の $7101 ハンドシェイク
		   - Sys2（finallap/assault）: 16bit コードは $7100=lo、$7101=hi|0x60
		   - burnforc 級: 同じプロトコルを $7110（namcoMailOff_=0x110） */
		if (!soundCmdWord_)
			soundCmdWord_ = cmd;
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		const uint16_t w = soundCmdWord_;
		/* dsaber の codeaddr は $7111（奇数）。偶数強制しない — ファームの LDX 枠の 1 バイト前にコマンドを置いていた。 */
		const unsigned mail = namcoMailOff_ & 0x7ffu;
		namcoTriRam_[mail] = (uint8_t)(w & 0xffu);
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && (w >> 8)) {
			/* 16bit: $7100=lo、$7101=(hi&0x1F)|flag。$7102/$7103 に flag を種まきしない — assault の枠歩行がそれを曲 0 と見て BGM を殺す。 */
			const uint8_t fl = NamcoMailFlag();
			namcoTriRam_[mail + 1u] = (uint8_t)(((w >> 8) & 0x1fu) | fl);
		} else if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
			/* ステータスは FLAGS のみ（曲 id を OR しない — 0x4C のような id は既に bit6 があり型 $20 BGM ゲートを誤トリップ）。型 $20 レコードは bit6 が欲しい。他は非 bit6 経路（rthun2 0x32 は $5A。旧 $64 SFX 型だけではない）。 */
			uint8_t fl = NamcoMailFlag();
			const int rec = CEmuAcSys2SongInfo(soundRom_, soundRomSize_, w & 0xffu, NULL, NULL, 0);
			/* 表プローブが失敗しても bit6 を残し、型 $20 BGM が bit6 経路へ入れるように（phelios 0x1F）。 */
			if (rec > 0 && rec != 0x20)
				fl = (uint8_t)(fl & (uint8_t)~0x40u);
			namcoTriRam_[mail + 1u] = fl;
		} else {
			namcoTriRam_[mail + 1u] = (uint8_t)((w & 0x7fu) | 0x60u);
		}
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && s_acSys2TwinMail
			&& mail <= 0x1feu && (mail + 0x100u + 1u) < 0x800u) {
			namcoTriRam_[mail + 0x100u] = namcoTriRam_[mail];
			namcoTriRam_[mail + 0x101u] = namcoTriRam_[mail + 1u];
		}
		/* DPRAM コピー後に一部タイトルが poll する Sys2 作業 RAM ミラーにも種まき */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
			namcoWorkRam_[0x1016 & 0x1fff] = (uint8_t)(w & 0xffu);
			namcoWorkRam_[0x1017 & 0x1fff] = (uint8_t)(w >> 8);
			namcoWorkRam_[0x101a & 0x1fff] = (uint8_t)(w & 0xffu);
			namcoWorkRam_[0x101b & 0x1fff] = (uint8_t)(w >> 8);
			namcoWorkRam_[0x101c & 0x1fff] = (uint8_t)(w & 0xffu);
			namcoWorkRam_[0x101d & 0x1fff] = (uint8_t)(w >> 8);
		}
		/* I がクリアされるまで IRQ を遅延 — RAM テスト中（I=1）の早いパルスは保留のまま最初の CLI で発火し、$D007 前に A を消す。 */
		if (namcoM6809_ && !NamcoCpuRaw(namcoM6809_)->cc.i) {
			namcoIrqAssert_ = 1;
			NamcoCpuRaw(namcoM6809_)->irq = true;
		} else {
			namcoIrqAssert_ = 1; /* SyncIrqs が CLI 後にパルス */
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && H8Active()) {
		H8InjectSong(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG) {
		/* Dig Dug: 曲ビットは $9A80+。Galaga: メインが $9211 に id、$9AA0 許可。Bosco: $8Axx のワンショット旗。Dig Dug/Galaga で Bosco $8Axx を poke しない — それらのバイトは digdug 作業/SE 状態。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (sys16RomBoard_ == 0x5047u) {
			/* Pengo: 1C4B は (IX+1) で $7000 ポインタ表を添字。カタログ 0x28（メイン BGM）は 8 語ヘッダの先 — 枠 8 は $7800。ジングル 01/02/04 は 1..4 のまま。 */
			if (chip_)
				CEmuChipC30SetEnable(chip_, 1);
			mem_[0x9040] = 1;
			uint8_t idx = cmd;
			if (cmd == 0x08 || cmd == 0x18 || cmd == 0x28)
				idx = 8;
			else if (cmd > 9)
				idx = (uint8_t)((cmd % 6u) + 1u);
			static const uint16_t kVoices[] = { 0x8c60, 0x8c70, 0x8c80 };
			for (unsigned i = 0; i < 3; i++) {
				const uint16_t b = kVoices[i];
				mem_[b] = cmd ? 1 : 0;
				mem_[(uint16_t)(b + 1)] = cmd ? idx : 0;
			}
			wsgNmiEnable_ = 0;
			return;
		}
		if (wsgMappy_) {
			/* MAME 15xx 共有 RAM $40+n は旗であり曲番号バイトではない。音源 CPU は $40,$41,$42… を歩く（mappy/gaplus は展開、digdug2 は #$40 から LDA ,X）し添字 n で JSR play。$40 にカタログ id を書くとハンドシェイク $11/"GO" が全タイトルで曲 0 をアサート（SAMESONG）。$80 は植えない: digdug2 IRQ がそのバウンスバッファを 15XX ボイスレジスタへコピーして CLR。 */
			CEmuChipC30SetEnable(chip_, 1);
			if (chip_) {
				const unsigned slot = 0x40u + ((unsigned)cmd & 0x3fu);
				if (!wsgNmiEnable_) {
					for (unsigned a = 0x40u; a < 0x80u; a++)
						chip_->Write(a, 0);
				}
				chip_->Write(slot, 1);
			}
			wsgNmiEnable_ = 1;
			namcoIrqAssert_ = 1;
			if (namcoM6809_ && !NamcoCpuRaw(namcoM6809_)->cc.i) {
				const uint16_t irqv = (uint16_t)(((unsigned)NamcoM6809Read8(0xfff8) << 8)
					| NamcoM6809Read8(0xfff9));
				const uint16_t rstv = (uint16_t)(((unsigned)NamcoM6809Read8(0xfffe) << 8)
					| NamcoM6809Read8(0xffff));
				if (irqv != rstv)
					NamcoCpuRaw(namcoM6809_)->irq = true;
			}
			return;
		}
		const int bosco = (mem_[0x80] == 0x3a && mem_[0x81] == 0x01
			&& mem_[0x82] == 0x8c) ? 1 : 0;
		const int galaga = (mem_[0x8a] == 0x11 && mem_[0x8b] == 0x01
			&& mem_[0x8c] == 0x91) ? 1 : 0;
		const int xevious = (mem_[0x80] == 0x01 && mem_[0x81] == 0x32
			&& mem_[0x82] == 0x22) ? 1 : 0;
		if (!cmd && !bosco) cmd = 1;
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
		mem_[0x9101] = 0; /* galaga ブートハンドシェイク */
		mem_[0x8c01] = 0; /* bosco ブートハンドシェイク */
		mem_[0x9b3c] = 0; /* digdug ビジー */
		mem_[0x9a8c] = 0; /* galaga ビジー */
		mem_[0x8a01] = 0; /* digdug 待ちゲート */
		if (galaga) {
			mem_[0x9211] = cmd;
			mem_[0x9aa0] = 1;
		}
		if (bosco) {
			/* NMI $00A8: $8A1F mute、$8A15 空コピー。BGM は $8A14→0289。
			   $0139 即値を曲 id に差し $0C 固定を外す。 */
			mem_[0x8a1f] = 0;
			mem_[0x8a15] = 0;
			mem_[0x8a08] = 0;
			mem_[0x8a0b] = 0;
			mem_[0x8a0c] = 0;
			mem_[0x8a0d] = 0;
			mem_[0x8a0e] = 0;
			mem_[0x8a0f] = 0;
			mem_[0x8a10] = 0;
			mem_[0x8a11] = 0;
			mem_[0x8a12] = 0;
			mem_[0x8a13] = 0;
			mem_[0x8a16] = 0;
			mem_[0x8a09] = 0;
			mem_[0x8a0a] = 0;
			mem_[0x8a14] = 1;
			mem_[0x0139] = cmd ? (uint8_t)cmd : (uint8_t)0x0cu;
			mem_[0x8a58] = 0x0a;
			mem_[0x8a59] = 0x0a;
			mem_[0x8a5a] = 0x0a;
		}
		if (xevious) {
			/* 0x01 は 01D2 の A001 枝がループ。他は $01DC 即値を曲 id にして同じ 028B を回す。 */
			unsigned i;
			for (i = 0; i < 0x10u; i++)
				mem_[(uint16_t)(0xa000u + i)] = 0;
			if (cmd == 0x01u)
				mem_[0xa001] = 1;
			else {
				mem_[0xa000] = 1;
				mem_[0x01dc] = cmd ? (uint8_t)cmd : 1;
			}
			mem_[0xa080] = 0;
			mem_[0xa094] = 0;
		}
		wsgNmiEnable_ = 1;
		irqPulse_ = 1;    /* ドライバはこれを NMI にする */
		return;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_TAITO_OPM) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		/* kikikai に PC060HA は無い — メイン Z80 が共有 RAM 9FFF に曲を植え、音源 CPU が 0xFF でなくなるまで poll。 */
		if (taitoOpmMap_ == 16) {
			/* MAME arkanoid: 曲は CALL $67AE（E980 リング）。C7F0 の EI;JR $ で
			   vblank ISR $0081 → $6792 を回す。
			   67DB チェックサム成功は E995=$FF を立て、6638 が RET NZ で曲開始を捨てる
			   （MCU 解除待ち）。データ面は ROM 一致するのでここで E995=0 にする。
			   6817 の AY 初期値はチェックサム成功時だけ入る — 未初期化なら同じ値を植える。 */
			soundCmd_ = cmd ? cmd : (uint8_t)0x0a;
			if (cpu_ && mem_) {
				mem_[0xe995] = 0;
				if (mem_[0xe891] == 0) {
					mem_[0xe891] = 0xff;
					mem_[0xe892] = 0x03;
					mem_[0xe89b] = 0x3f;
					mem_[0xe91b] = 0x3f;
					mem_[0xe893] = 0x01;
					mem_[0xe913] = 0x01;
				}
				mem_[0xc7f0] = 0xfb;
				mem_[0xc7f1] = 0x18;
				mem_[0xc7f2] = 0xfe;
				/* ISR $0081 は CALL $0222/$02D6 の映像／パドルで 6792 に届かない。 */
				mem_[0x008f] = 0x00;
				mem_[0x0090] = 0x00;
				mem_[0x0091] = 0x00;
				mem_[0x0092] = 0x00;
				mem_[0x0093] = 0x00;
				mem_[0x0094] = 0x00;
				const uint16_t sp = 0xc7fcu;
				mem_[sp] = 0xf0;
				mem_[sp + 1] = 0xc7;
				cpu_->r.sp = sp;
				cpu_->r.b.a = soundCmd_;
				cpu_->r.pc = (uint16_t)0x67aeu;
				cpu_->r.iff1 = 0;
				cpu_->r.iff2 = 0;
				cpu_->r.im = 1;
				cpu_->irqDelay = 0;
			}
			return;
		}
		if (taitoOpmMap_ == 2) {
			/* kicknrun: ISR 0202 が (A700) を poll、アイドル $DF。kikikai は 9FFF。 */
			if (mem_[4] == 0xbd && mem_[5] == 0x00)
				mem_[0xa700] = cmd ? cmd : (uint8_t)0x07;
			else
				mem_[0x9fff] = cmd ? cmd : (uint8_t)0xff;
			irqPulse_ = 1;
			return;
		}
		if (taitoOpmMap_ == 13) {
			/* MAME taito_l kurikint_2_map: メールは E7F0（アイドル $FF）。ISR 0171 が ≠FF なら E7F1 へコピーして 112C。
			   C000 bit6 は 0121 の再入ロック。残っていると 2 回目以降の vblank が E7F0 を見ない。 */
			mem_[0xe7f0] = cmd ? cmd : (uint8_t)0x01;
			if (mem_[0xdfa1] == 0)
				mem_[0xdfa1] = 0x01; /* 128E は DFA1=0 なら曲>=9 を捨てる。0xEF が立てる。 */
			mem_[0xc000] = (uint8_t)(mem_[0xc000] & (uint8_t)~0x40);
			irqPulse_ = 1;
			return;
		}
		if (taitoOpmMap_ == 14) {
			/* Taito L 1cpu: palamed は 8000+A タスク旗。cachat RST 08 は 8007 から 15 バイト枠。
			   horshoes は 9700/9701 メール。flipull は 9A00 キュー（0B1E）と $0B48。 */
			if (mem_[0] == 0xf3 && mem_[4] == 0x07) {
				unsigned s;
				for (s = 0; s < 0x22u; s++) {
					const unsigned hl = 0x8007u + s * 0x0fu;
					if (hl + 0x0du >= 0x9fffu)
						break;
					if ((mem_[hl] & 1u) == 0) {
						const unsigned tbl = 0x01cdu + (unsigned)(cmd & 0xffu) * 2u;
						mem_[hl] = 0xff;
						mem_[hl - 1u] = cmd ? cmd : (uint8_t)0x0a;
						mem_[hl + 1u] = 0x01;
						if (tbl + 1u < 0x6000u) {
							mem_[hl + 0x0cu] = mem_[tbl];
							mem_[hl + 0x0du] = mem_[tbl + 1u];
						}
						break;
					}
				}
			} else if ((soundRom_ && soundRomSize_ >= 3u && soundRom_[0] == 0xc3
					&& soundRom_[1] == 0x89 && soundRom_[2] == 0x00)
				|| (mem_[0] == 0xc3 && mem_[1] == 0x89 && mem_[2] == 0x00)) {
				/* 458C: 9700 書込ポインタ、9701 読ポインタ、9702 から 16 スロット。
				   IM2 は 0666 が FF00=5E / FF01=60 / FF02=62。全部 60 にすると
				   irq2（$01A4 CALL $4582 音源）が $032D グラフィックに吸われる。 */
				const unsigned wr = (unsigned)((mem_[0x9700] + 1u) & 0x0fu);
				mem_[0x9702u + wr] = cmd ? cmd : (uint8_t)0x01;
				mem_[0x9700] = (uint8_t)wr;
				mem_[0xff00] = 0x5e;
				mem_[0xff01] = 0x60;
				mem_[0xff02] = 0x62;
			} else if ((soundRom_ && soundRomSize_ >= 3u && soundRom_[0] == 0xc3
					&& soundRom_[1] == 0xd0 && soundRom_[2] == 0x03)
				|| (mem_[0] == 0xc3 && mem_[1] == 0xd0 && mem_[2] == 0x03)) {
				/* 0B1E: 9A00=ディレイ、9A01=残数、9A02=コマンド。irq1 $0983 が CALL $0B48 して
				   9700 リングへ。04D0 が書く IM2 表は FF00=04/06/08（I=0 → $097F/$0983/$09CC）。 */
				mem_[0x9a00] = 0;
				mem_[0x9a01] = 1;
				mem_[0x9a02] = cmd ? cmd : (uint8_t)0x01;
				mem_[0xff00] = 0x04;
				mem_[0xff01] = 0x06;
				mem_[0xff02] = 0x08;
			} else {
				mem_[0x8000u + (cmd & 0xffu)] = 1;
			}
			if (mem_[0] == 0xf3 && mem_[4] == 0x07)
				mem_[0xff03] = (uint8_t)(mem_[0xff03] | 6u);
			else if (mem_[0] == 0xc3 && mem_[1] == 0xd0 && mem_[2] == 0x03)
				mem_[0xff03] = (uint8_t)(mem_[0xff03] | 2u);
			else
				mem_[0xff03] = (uint8_t)(mem_[0xff03] | 5u);
			irqPulse_ = 1;
			return;
		}
		if (taitoOpmMap_ == 3 || taitoOpmMap_ == 4 || taitoOpmMap_ == 5
			|| taitoOpmMap_ == 6) {
			/* tokio/bublbobl: ラッチ + A800/B001 許可でゲートされた NMI */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			if (flstoryNmiEn_)
				irqPulse_ = 1;
			return;
		}
		if (taitoOpmMap_ == 8 || taitoOpmMap_ == 11)
			return;
		if (taitoOpmMap_ == 17) {
			/* MAME tnzsb sound_command_w: ラッチ + IRQ0 HOLD。IN 02 で解除。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (taitoOpmMap_ == 18) {
			/* MAME sf.cpp soundcmd_w: ラッチ + NMI。YM irq → IRQ0。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (taitoOpmMap_ == 7) {
			/* 旧 TNZS: サウンドラッチ無し。サブ CPU が共有 RAM を poll。tnzsjo EF10（FF=アイドル）、chukatai 16 バイトキュー E005、kageki E03E。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			if (mem_[3] == 0x31 && mem_[5] == 0xd7) {
				/* kageki: DI;IM1;LD SP,D700 */
				mem_[0xe03e] = cmd;
			} else if (mem_[3] == 0x21 && mem_[4] == 0x00 && mem_[5] == 0xd0) {
				/* chukatai: DI;IM1;LD HL,D000 */
				mem_[0xe005] = cmd;
			} else {
				mem_[0xef10] = cmd;
				mem_[0xef28] = 0x80; /* 024B JP P は符号付き + ならボイス歩行を飛ばす */
				memset(mem_ + 0xe600, 0, 0x200); /* 044A IX=E603 オブジェクト */
				/* 06CD が埋める DF80 リングへミラー。008F が DI アイドル中に既に EF10 をドレインした場合に備える。 */
				{
					uint8_t rd = (uint8_t)(mem_[0xdf81] & 0x0f);
					uint8_t wr = (uint8_t)((mem_[0xdf80] + 1) & 0x0f);
					if (wr == rd)
						rd = (uint8_t)((rd + 1) & 0x0f);
					mem_[0xdf80] = wr;
					mem_[0xdf81] = rd;
					mem_[0xdf82 + wr] = cmd;
				}
			}
			irqPulse_ = 1;
			return;
		}
		SytMasterWriteCommand(cmd);
		return;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (vsIoKind_ == 4 || vsIoKind_ == 5) {
			/* cop01/magmax: ラッチ pending → IRQ0。SJ NMI ではない */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 6 || vsIoKind_ == 7) {
			/* bombjack/calorie: ラッチはメモリで poll。vblank NMI/IRQ0 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			return;
		}
		if (vsIoKind_ == 8) {
			/* solomon: ラッチ @8000、書で NMI、120 Hz IRQ0 シーケンサ */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 10) {
			/* pbaction: ラッチ @8000、CTC TRG0 パルス → IM2 vec 0（NMI ではない） */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 11) {
			/* chaknpop: ラッチ無し。RST 20 が A000 経由でリング 8590 へキュー。ブートの rst $20 / A=0x50 は >=$50 mute で 85A0 bit7 を立て、A0CA が次コマンドをスクリプト経路へ。その旗と両 AY ボイスブロックを消し、BGM を 1 本キュー。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			memset(mem_ + 0x8500, 0, 0xA8);
			{
				uint8_t a = 1;
				mem_[0x85A5] = a;
				mem_[0x85A6] = 0;
				mem_[0x8590u + a] = cmd;
				mem_[0x85A8] = 0xff;
			}
			return;
		}
		if (vsIoKind_ == 12) {
			/* 1942: ラッチ 6000 を poll。NMI 無し。IRQ0 はスキャンライン 4 本/フレーム。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			return;
		}
		if (vsIoKind_ == 13) {
			/* swimmer/guzzler: ラッチ 3000 clear-on-read。pending → IRQ0 HOLD。NMI は 244 Hz シーケンサ。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 14) {
			/* tubep: ラッチ I/O 06 poll（bit7=pending）。IRQ0 はスキャンライン 64/192。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			return;
		}
		if (vsIoKind_ == 15) {
			/* retofinv: ラッチ 4000 → IRQ0。NMI は 120 Hz シーケンサ。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 16) {
			/* ikki: 共有 RAM。0004 は CD90 が非0のとき CEAD+5 → CD50。CD50 も直書き。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			mem_[0xcd90] = 1;
			mem_[0xcd50] = cmd;
			mem_[0xcead] = (uint8_t)(cmd - 5u);
			mem_[0xceaf] = cmd;
			return;
		}
		if (vsIoKind_ == 17) {
			/* circusc: ラッチ 6000。ホスト SOUND-ON → IRQ0 IM1。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 18) {
			/* starforce: PIO PA = ラッチ。pending strobe → IM2 vec 00。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 19) {
			/* tehkanwc: ラッチ書 → NMI。vblank IRQ0。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 20) {
			/* fcombat: ラッチ 6000 poll。NMI/IRQ 無し。bit7 は JP 0000 リセット。 */
			soundCmd_ = (uint8_t)(cmd & 0x7fu);
			soundCmdPending_ = 1;
			return;
		}
		if (vsIoKind_ == 22) {
			/* gberet: メールは D81B、D81A=1。NMI @0066 が CALL 7801。SN ラッチ F200 とは別。 */
			mem_[0xd81b] = cmd;
			mem_[0xd81a] = 1;
			return;
		}
		if (vsIoKind_ == 24) {
			/* masao: ラッチは AY ポートA。7F00 立ち下がり → HOLD_LINE IRQ0。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			irqPulse_ = 1;
			return;
		}
		if (vsIoKind_ == 23) {
			/* higemaru: 61xx が 6242 でチャネルを武装。RST08 の CALL 5BCB を EF00 経由にして一発呼ぶ。 */
			static const uint16_t kSong[32] = {
				0x621Au, 0x615Du, 0x6181u, 0x61C0u, 0x6151u, 0x616Fu, 0x61B4u, 0x6193u,
				0x61A8u, 0x61C0u, 0x61C0u, 0x61C0u, 0x61CCu, 0x61CCu, 0x61CCu, 0x61E7u,
				0x618Au, 0x61F6u, 0x6138u, 0x6202u, 0x61DBu, 0x621Au, 0x6112u, 0x60B8u,
				0x60C7u, 0x60D6u, 0x60E5u, 0x60F4u, 0x6103u, 0x6112u, 0x61A8u, 0x61C0u
			};
			const uint16_t h = kSong[cmd & 31u];
			mem_[0xef00] = 0x3au;
			mem_[0xef01] = 0xffu;
			mem_[0xef02] = 0xefu;
			mem_[0xef03] = 0xa7u;
			mem_[0xef04] = 0x28u;
			mem_[0xef05] = 0x07u;
			mem_[0xef06] = 0xafu;
			mem_[0xef07] = 0x32u;
			mem_[0xef08] = 0xffu;
			mem_[0xef09] = 0xefu;
			mem_[0xef0a] = 0xcdu;
			mem_[0xef0b] = (uint8_t)(h & 0xffu);
			mem_[0xef0c] = (uint8_t)(h >> 8);
			mem_[0xef0d] = 0xcdu;
			mem_[0xef0e] = 0xcbu;
			mem_[0xef0f] = 0x5bu;
			mem_[0xef10] = 0xc9u;
			if (mem_[0x0225] == 0xcdu && mem_[0x0226] == 0xcbu && mem_[0x0227] == 0x5bu) {
				mem_[0x0226] = 0x00u;
				mem_[0x0227] = 0xefu;
			}
			mem_[0xefff] = 1;
			return;
		}
		if (vsIoKind_ == 21) {
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			mem_[0xe001] = (uint8_t)(mem_[0xe001] | 0x10u);
			/* combh: 0000=LD SP,E7A0。0220 は FF。曲は CALL 4B90、フレームは CALL 4C84。
			   NMI 0B9A は E4A2 キューで、IX bit7 の SN 更新はメインの 4C84。HALT で 60Hz。 */
			if (mem_[0] == 0x31 && mem_[1] == 0xa0 && mem_[2] == 0xe7
				&& mem_[0x4b90] == 0x0e && mem_[0x4c84] == 0xddu) {
				mem_[0xe800] = 0xefu;
				mem_[0xe801] = 0xcdu; mem_[0xe802] = 0x58u; mem_[0xe803] = 0x4cu;
				mem_[0xe804] = 0xefu;
				mem_[0xe805] = 0x0eu; mem_[0xe806] = 0x00u;
				mem_[0xe807] = 0x16u; mem_[0xe808] = 0x00u;
				mem_[0xe809] = 0x1eu; mem_[0xe80a] = 0x00u;
				mem_[0xe80b] = 0x3eu; mem_[0xe80c] = cmd;
				mem_[0xe80d] = 0xcdu; mem_[0xe80e] = 0x90u; mem_[0xe80f] = 0x4bu;
				mem_[0xe810] = 0x76u;
				mem_[0xe811] = 0xcdu; mem_[0xe812] = 0x84u; mem_[0xe813] = 0x4cu;
				mem_[0xe814] = 0x18u; mem_[0xe815] = 0xfau;
				if (cpu_) {
					const unsigned pc = (unsigned)cpu_->r.pc;
					if (pc == 0x0220u || (pc >= 0xe800u && pc <= 0xe815u)
						|| pc == 0x4c58u || pc == 0x4b90u)
						cpu_->r.pc = 0xe800;
				}
				return;
			}
			/* bankp: 0220 は CALL DAF3 / RST 28 / JP E800。E800 は
			   CALL 0896; LD C,0; LD E,0; LD A,cmd; CALL DA04; JR $。
			   DA04 が (IY+0) に bit7 を立て、NMI の DB6B が SN を出し続ける。
			   DA73 はアトラクト用で bit7 が立たず 1 発で止まる。 */
			if (mem_[0] == 0xc3 && (mem_[1] == 0xc0 || mem_[1] == 0x20)
				&& (mem_[2] == 0xab || mem_[2] == 0x02)) {
				mem_[0] = 0xc3;
				mem_[1] = 0x20;
				mem_[2] = 0x02;
			}
			if (mem_[0x0220] == 0x31 && mem_[0x0221] == 0x50 && mem_[0x0222] == 0xe7) {
				mem_[0x0223] = 0xcd; mem_[0x0224] = 0xf3; mem_[0x0225] = 0xda;
				mem_[0x0226] = 0xef;
				mem_[0x0227] = 0xc3; mem_[0x0228] = 0x00; mem_[0x0229] = 0xe8;
			}
			mem_[0xe800] = 0xcd; mem_[0xe801] = 0x96; mem_[0xe802] = 0x08;
			mem_[0xe803] = 0x0e; mem_[0xe804] = 0x00;
			mem_[0xe805] = 0x1e; mem_[0xe806] = 0x00;
			mem_[0xe807] = 0x3e; mem_[0xe808] = cmd;
			mem_[0xe809] = 0xcd; mem_[0xe80a] = 0x04; mem_[0xe80b] = 0xda;
			mem_[0xe80c] = 0x18; mem_[0xe80d] = 0xfe;
			if (cpu_) {
				const unsigned pc = (unsigned)cpu_->r.pc;
				if (pc == 0x0242u || pc == 0x0243u || pc == 0x0244u
					|| (pc >= 0xe800u && pc <= 0xe80du))
					cpu_->r.pc = 0xe800;
			}
			return;
		}
		if (vsIoKind_ == 9) {
			/* halleys: ラッチ @5000、書で NMI（AY I/O は SJ NMI マスクではない） */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			sjLatchFlag_ = 1;
			irqPulse_ = 1;
			return;
		}
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		sjLatchFlag_ = 1;
		/* NMI は AY#4 ポート B bit0（アクティブ Low）でゲート。音源プログラムが書くまで MAME の merger は NMI をマスク。書かないゲームでも注入コマンドが失われないよう許可。 */
		if (!sjNmiMaskSeen_ || sjNmiMask_)
			irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE
		|| board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
		|| board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (vsIoKind_ == 1 && board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
			/* MAME megazone 共有 RAM: メイン 3807 = Z80 E007 が曲コード。E120 が 0 だと ISR の 18FB が即 RET。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			if (mem_) {
				mem_[0xe007] = cmd;
				mem_[0xe120] = 1;
			}
			irqPulse_ = 1;
			return;
		}
		/* Scramble: PPI PB bit3 が 7474 をクロック → Z80 INT（IM0 ベクタ 0xFF=RST38）。Time Pilot / GX400: ホストエッジ → HOLD_LINE IRQ0。ラッチは AY ポート A（scramble AY2 / timeplt AY1）または mem e001（gx400）。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (snkMapKind_ >= 4) {
			/* ddragon/kuniokun: generic_latch pending → M6809 IRQ。YM → FIRQ。 */
			if (namcoM6809_ && !NamcoCpuRaw(namcoM6809_)->cc.i)
				NamcoCpuRaw(namcoM6809_)->irq = true;
			else
				namcoIrqAssert_ = 1;
			return;
		}
		/* soundlatch_w → NMI。YM2151 IRQ が音楽シーケンサを駆動 */
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		if (konamiK7232Map_ == 7) {
			/* hexion: A000 はゲーム位相 0–7（曲番号ではない）。NMI 03E2 は A000!=0 のとき
			   CALL 6241 → LD A,(AFCF); JP NZ,5E2A。5DDE が 0x80–0x89 を AFCF に書く。
			   A001 にカタログを書くと 04DA の入れ子 JP が 0x80 でテーブル外へ飛ぶ。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			if (mem_[0xa000] == 0)
				mem_[0xa000] = 1;
			mem_[0xa001] = 0;
			mem_[0xafce] = 0;
			mem_[0xafe0] = 0;
			mem_[0xafc3] = (uint8_t)(cmd ^ 0xffu);
			mem_[0xafcf] = cmd;
			return;
		}
		/* MAME: ホスト soundlatch 書 + IRQ0（IM1）。spy は 3FC0 HOLD_LINE → IRQ0 と YM3812→NMI。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (vsIoKind_ == 1)
			irqPulse_ = 1; /* mmpanic: generic_latch_8 → NMI */
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		/* soundlatch → NMI。YM タイマ IRQ が BGM を駆動。Cave 16bit ラッチは SetSoundCommandWord。
		   kind 11（Dooyong bluehawk）: ラッチは F800 poll。NMI ベクタ 0066 はブート途中。 */
		soundCmd_ = cmd;
		soundCmdWord_ = cmd;
		soundCmdPending_ = 1;
		if (tecmoOpl_ != 11 && tecmoOpl_ != 12)
			irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_RAIZING) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (!RaizingHandshakeAcked()) {
			RaizingPostCommand(0x55, 0x55);
			return;
		}
		/* カタログの曲コードの行き先は改訂で違う: mahoudai と Battle Garegga はコマンドそのもの。Batrider と Battle Bakraid はサブコマンド＋添字に分ける — Batrider は command & 0x1F（0 = BGM 開始）、Battle Bakraid は下位ニブル（1 = BGM 開始）、どちらも第 2 ラッチに曲番号。 */
		switch (raizingType_) {
		case 3: RaizingPostCommand(0x00, cmd); break;
		case 4:
			/* カタログ 0x11 "(ODYSSEY)" とそれ以降の添字はダミー FF 0F 終端を指す。本物 Odyssey は曲 0x02。 */
			RaizingPostCommand(0x01, (uint8_t)(cmd >= 0x11 ? 0x02 : cmd));
			break;
		default: RaizingPostCommand(cmd, 0x00); break;
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_FLSTORY) {
		/* MAME flstory: ラッチ pending ∧ DA00 許可 → NMI（input_merger ALL_HIGH） */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (flstoryNmiEn_)
			irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE
		|| board_ == CEMU_AC_BOARD_ROBOKID
		|| board_ == CEMU_AC_BOARD_BATTLANTIS) {
		/* 周期 IRQ0（terracre/armedf）/ YM2203 IRQ（robokid）/ ホスト IRQ0（battlantis）。ラッチは ISR から poll。MAME terracre/armedf sound_w: ((cmd&0x7f)<<1)|1。Z80 は右シフトしてカタログ曲添字を復元。 */
		if (board_ == CEMU_AC_BOARD_TERRACRE && terracreMap_ >= 3 && terracreMap_ <= 5) {
			/* MAME argus: generic_latch_8 poll。シフト無し。IRQ は YM2203。 */
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			return;
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 8) {
			/* MAME stfight fm_w: 0x80 | catalog。読 F000 が bit7 を落とす。周期 120 Hz がシーケンサ。 */
			soundCmd_ = (uint8_t)(0x80u | (cmd & 0x7fu));
			soundCmdPending_ = 1;
			return;
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 11) {
			/* MAME angelkds: ポート 80=下位ニブル、81=上位、83 bit0=strobe。ファームは各 IN を AND 0x0F。 */
			raizingLatch_[0] = (uint8_t)(cmd & 0x0fu);
			raizingLatch_[1] = (uint8_t)((cmd >> 4) & 0x0fu);
			raizingLatchOut_[0] = 0;
			raizingLatchOut_[1] = 1;
			soundCmd_ = cmd;
			soundCmdPending_ = 1;
			return;
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 15) {
			/* Mitchell: 平文は A で CALL $03EC → $7803。Kabuki（marukin）は $7629 → $76B1。
			   カタログ BGM 33 は 0x21。bank6 表 0x20.. は 06+チャネルポインタの本曲。
			   0x01..0x1F は先頭 E0 で 7717 へ落ちるのでリマップしない。
			   Kabuki の RST38 はオペコード面が DI で始まり、即値はデータ面なので
			   平文 ISR として読むとゴミへ飛ぶ。M1 は mem_、即値は qsKabukiData_ へ
			   CALL $7645;EI;RET を植える。mgakuen 平文 ISR は CALL $7800 相当。 */
			const int kabuki = (qsKabuki_ || (mem_ && mem_[0] == 0x31)) ? 1 : 0;
			soundCmd_ = cmd ? cmd : (uint8_t)0x01;
			soundCmdPending_ = 1;
			if (cpu_ && mem_) {
				uint16_t sp = cpu_->r.sp;
				if (sp < 4u || sp > 0xfffdu)
					sp = kabuki ? (uint16_t)0xf880u : (uint16_t)0xef70u;
				sp = (uint16_t)(sp - 2u);
				uint16_t ret = (uint16_t)0x009bu;
				if (kabuki) {
					mem_[0x0038] = 0xcd;
					if (qsKabukiData_) {
						qsKabukiData_[0x0039] = 0x45;
						qsKabukiData_[0x003a] = 0x76;
					}
					mem_[0x003b] = 0xfb;
					mem_[0x003c] = 0xc9;
					/* 76B1→775C が EAFC を 0 にする。78E0 は EAFC=0 だとテンポ枝を飛ばす。
					   RAM トランポリンは Kabuki 対象外なので即値も mem_。 */
					mem_[0xfff0] = 0x3e;
					mem_[0xfff1] = 0x08;
					mem_[0xfff2] = 0x32;
					mem_[0xfff3] = 0xfc;
					mem_[0xfff4] = 0xea;
					mem_[0xfff5] = 0xfb;
					mem_[0xfff6] = 0x18;
					mem_[0xfff7] = 0xfe;
					ret = 0xfff0u;
					sp = 0xf87eu;
				}
				mem_[sp] = (uint8_t)(ret & 0xffu);
				mem_[sp + 1] = (uint8_t)(ret >> 8);
				cpu_->r.sp = sp;
				cpu_->r.b.a = soundCmd_;
				cpu_->r.pc = kabuki ? (uint16_t)0x7629u : (uint16_t)0x03ecu;
				cpu_->r.iff1 = 0;
				cpu_->r.iff2 = 0;
				cpu_->r.im = 1;
				cpu_->irqDelay = 0;
			}
			return;
		}
		if (board_ == CEMU_AC_BOARD_TERRACRE)
			cmd = (uint8_t)(((cmd & 0x7fu) << 1) | 1u);
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		/* macross2/tdragon2/tharrier/crospang: ラッチは poll。IRQ は YM タイマだけ。airbustr/djboy/hvyunit はラッチ NMI。 */
		if (!(board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 1 || vsIoKind_ == 2 || vsIoKind_ == 7 || vsIoKind_ == 13 || vsIoKind_ == 14 || vsIoKind_ == 15)))
			irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TOAPLAN1) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (toaplanKaneko_ == 1) {
			/* MAME snowbros: soundlatch[0] 書 → Z80 NMI。読は I/O 04 */
			irqPulse_ = 1;
			return;
		}
		if (toaplanKaneko_ == 3 || toaplanKaneko_ == 5) {
			/* MAME slapfght: コマンドは共有 RAM C800。C801==AA はブート存在。NMI が C800 を poll（0xFF アイドル）。
			   perfrman は同じプロトコルで窓が 8800。 */
			const uint16_t base = (toaplanKaneko_ == 5) ? 0x8800u : 0xc800u;
			mem_[base] = cmd;
			mem_[(uint16_t)(base + 1u)] = 0xaa;
			return;
		}
		/* 共有 RAM メールボックス: (mail) がコマンド（0xFF アイドル）。Truxton は (8001)==0xAA をメイン CPU 存在旗にも保つ。Wardner はブート中だけ (C002)==0xAA を待つ — その後 C001-C7FE は BSS。 */
		mem_[ToaplanMail()] = cmd;
		if (toaplanKaneko_ == 4)
			mem_[ToaplanReady()] = 0xff;
		else if (toaplanKaneko_ != 2)
			mem_[ToaplanReady()] = 0xaa;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (snkMapKind_ == 3) {
			/* MAME mainsnk: soundlatch pending → NMI。周期 IRQ0 244 Hz がシーケンサ。 */
			irqPulse_ = 1;
			return;
		}
		if (snkMapKind_ == 2) {
			/* MAME aso: ラッチ @ D000。CMDIRQ+BUSY を立てレベル IRQ0。 */
			snkStatus_ = (uint8_t)((snkStatus_ & (uint8_t)~0x03u) | 0x0cu);
			irqPulse_ = 1;
			return;
		}
		if (snkMapKind_) {
			/* 古典 SNK: ラッチ @ E000。ステータス bit3=pending、bit2=busy。IRQ0 は (status & 0x0B) != 0 の間レベル保持（MAME）。注入後の最初の IRQ がラッチになるよう古い YM ビットを落とす。 */
			mem_[0xe000] = cmd;
			snkStatus_ = (uint8_t)((snkStatus_ & (uint8_t)~0x03u) | 0x0cu);
			irqPulse_ = 1;
			return;
		}
		/* MAME snk68: soundlatch 書 → NMI。Z80 は F800 でラッチを読む。NMI リング（F152/F133）にも置き、ブート中に NMI エッジ 1 回を失っても 00D6 のメインループ poll がドレインできるように。ロック番地は NMI から嗅ぐ: 0068 の LD A,(F1xx)。 */
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
	if (board_ == CEMU_AC_BOARD_T5182) {
		/* MAME t5182 shared: 4021 = ワード数、4022+ が語。コマンド 80 XX が曲 XX。CPU IRQ bit1。
		   メインセマフォ（port20 bit0）は 0 のまま — 1 だと 0D62 がキューを捨てる。 */
		mem_[0x4021] = 1;
		mem_[0x4022] = 0x80;
		mem_[0x4023] = cmd;
		seibuRst10_ |= 2;
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_HEBERPOP) {
		/* MAME generic_latch_8: 68000 が 20000F へ書く → pending が Z80 IRQ0 を保持。IN C0 で解除。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_BIONICC) {
		/* MAME: MCU が P1 を m_mcu_to_audiocpu へ載せ、68000 が E4002 で NMI。
		   NMI 0066 が (A000) を (C000) へコピー。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* seibu_sound main_w: バイト 0/1 + オフセット 4 で RST18。sub2main_pending をクリアし RST18 がラッチを読む（ブートの pending_w はさもなくば (4012)=1 のままエンキューを飛ばす）。ファームは D（ラッチ byte1）で配送: 0x80 が曲走査へ。表添字は E（ラッチ byte0）。raiden/cupsoc は |0x80。raiden2 曲表は生 id（0x0C が BGM、0x8C は別レコードで無音）。 */
		uint8_t idx = cmd;
		if (seibuSongOr80_ == 2 || seibuSongOr80_ == 5) {
			/* raiden2 / cabal 曲表は生 id。|0x80 は別レコード（cabal は DEC A して 8000 の 4 バイト枠）。 */
		} else if (seibuSongOr80_ < 3) {
			if (idx < 0x80)
				idx = (uint8_t)(idx | 0x80u);
			if (idx == 0x80 || idx == 0x81 || idx == 0x82 || idx == 0x84)
				idx = 0x8e;
		} else if (idx < 0x80) {
			/* godzilla カタログ 0x42- は表枠が 0xC2-。sdgndmps は 0x80-0x87 を 0x8e に潰さない。 */
			idx = (uint8_t)(idx | 0x80u);
		}
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
		/* MAME cmd_w: ラッチ。bit7 クリアで IRQ アサート。ホストは後で 0x80 を書き sound_irq_ack_w が CLEAR_LINE（メイン CPU ハンドシェイク）。 */
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
	/* Capcom ZN（MAME zn.cpp）: soundlatch → NMI、読は I/O 00。カタログバイトコードは FF 00 00 code へ展開（語経路と同じ）。 */
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
	/* CPS2 QSound: 68K は共有 RAM C000 に 16 バイトパケットを書き、ドアベルとして C00F をクリア（!=0xFF）。IRQ @0038 は CFFD==0x88 のときコピー。Init は CFFF==0xFF（68K 準備）まで 00DD でスピン。68K が無いのでその待ちをこちらで解く。シーケンサは周期 IM1 — NMI 無し。Packet[0] は 9006 のプログラム表へのサウンドコード添字（stock の code%3 畳みを置き換える LoadRoms パッチ参照）。 */
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
	/* K053260: メイン→音源ポート 0/1 + 音源 IRQ（MAME sound_irqtrigger / z80_irq_w）。SH1 NMI（FA00/HALT）は別。コマンドハンドラは @0038。 */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && pcmKind_ == 3) {
		soundCmd_ = cmd;
		soundCmdWord_ = cmd; /* 8bit 経路: ポート 1 の上位バイトをクリア */
		soundCmdPending_ = 1;
		CChip* pcm = pcm_ ? pcm_ : chip_;
		if (pcm) {
			CEmuChipK053260MainWrite(pcm, 0, cmd);
			CEmuChipK053260MainWrite(pcm, 1, 0x00);
		}
		irqPulse_ = 1;
		return;
	}
	/* gijoe: K054321 ラッチ + sound_irq_w → IRQ0。lethalen: ラッチのみ（タイマ NMI）。 */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && pcmKind_ == 4 && KonamiJoeMap()) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_VSYSTEM && vsIoKind_ == 3) {
		/* Psikyo gunbird の NMI は IN A,(08) を $8006 の 16 バイトリングへコピー（添字 $8002）。メインループは CALL $0633 でそのリングをドレイン。BGM $20-$3F はその後 $1FAC へ。ここにバイトを植え、Timer-A ISR で NMI エッジを逃しても全タイトルがブート $40 SFX ドローンに残らないように。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		const unsigned idx = (unsigned)mem_[0x8002] & 0x0fu;
		mem_[0x8006u + idx] = cmd;
		mem_[0x8002] = (uint8_t)((idx + 1u) & 0x0fu);
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SYS18 && vsIoKind_ == 1) {
		/* UFO Catcher: RST38 CALL 4000。F009 が曲メール（80=tick、81-8F=BGM 0-15）。
		   F022 は 402B のカウントダウンで、カタログ id ではない。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		mem_[0xf009] = (cmd < 0x81u) ? (uint8_t)(0x80u + cmd) : cmd;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1 && vsIoKind_ == 4) {
		/* System E: hangonjr は CA00 メール + C200=1 で vblank が 512F を呼ぶ。
		   transfrm の hoot は IC2 音源 blob @8000、曲は FD00。NMI は無い。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (mem_[0x8004] == 0xcd && mem_[0x8005] == 0x84 && mem_[0x8006] == 0x80)
			mem_[0xfd00] = cmd;
		else {
			mem_[0xca00] = cmd;
			mem_[0xc200] = 1;
			/* C250 bit2 が落ちていると 0124 アトラクトが CA00 を上書きして BGM が途切れる。 */
			mem_[0xc250] = (uint8_t)(mem_[0xc250] | 4u);
		}
		return;
	}
	soundCmd_ = cmd;
	soundCmdPending_ = 1;
	/* Z80 線をパルス。CheckIrq は Sys16A→NMI（uPD7759 + ラッチ）、Sys16B→IM1（YM と同じベクタ経由のラッチ。注入時タイマがアイドルでも IN A,(C0) が走るために要る）。 */
	irqPulse_ = 1;
	/* m99（bbmanw/poundfor）: コマンド読はラッチを F4DC と AND */
	if (board_ == CEMU_AC_BOARD_IREM_M72 && m72IoAlt_ && mem_[0xf4dc] == 0)
		mem_[0xf4dc] = 0xff;
	/* OutRun ProcessCommand は (F800) を poll。After Burner / Hang-On は F800–F807 を空き枠キュー（0x80 = 空）に保つ。NMI は空き枠へラッチを書く。それらのマーカを上書きしない。 */
	if (board_ == CEMU_AC_BOARD_OUTRUN)
		mem_[0xf800] = cmd;
	/* bayroute: 00E0 が (F800) を曲開始。ISR が IN (C0)==0 だと F9AC/F9AD を毎 tick クリアするので
	   ラッチだけではレース。0x80 アイドルは 03E5 が RET NC（cmd>=0x15）で無視。 */
	if (board_ == CEMU_AC_BOARD_SYS16B && mem_
		&& mem_[0x26] == 0x3e && mem_[0x27] == 0x90
		&& mem_[0x28] == 0x32 && mem_[0x29] == 0x00 && mem_[0x2a] == 0xf8)
		mem_[0xf800] = cmd;
	if (board_ == CEMU_AC_BOARD_ABURNER && vsIoKind_ == 1) {
		/* smgp NMI: EXX; INC E; CP 1A; IN A,(40)。ブート LD DE,F82A のあと E が 0x1A を超え、JR C で捨てる。
		   本体は F800-F80F の 0x80 空き枠を AB と同じくドレインする。 */
		for (unsigned i = 0; i < 0x10u; i++) {
			if (mem_[0xf800u + i] == 0x80u) {
				mem_[0xf800u + i] = cmd;
				break;
			}
		}
	}
	if (board_ == CEMU_AC_BOARD_ABURNER && vsIoKind_ == 2) {
		/* lastsurv NMI: HL=F800, B=8, (HL)==0 の枠へ IN A,(40)。 */
		for (unsigned i = 0; i < 8u; i++) {
			if (mem_[0xf800u + i] == 0) {
				mem_[0xf800u + i] = cmd;
				break;
			}
		}
	}
	/* System 32: 共有 RAM E000。V60 IRQ 源へ。NMI は RETN/CALL のみ（brival の 0066=DI は壊す）。
	   f1en/ga2/spidman は空枠=0x80（f1en は E000-E001、ga2/spidman は E000-E007）。
	   arabfgt/holo は 0 埋めなので 0x80 枠が無く E000 直書き。 */
	if (board_ == CEMU_AC_BOARD_SYS32) {
		int slotted = 0;
		for (unsigned i = 0; i < 0x10u; i++) {
			if (mem_[0xe000u + i] == 0x80u) {
				mem_[0xe000u + i] = cmd;
				slotted = 1;
				break;
			}
		}
		if (!slotted) {
			mem_[0xe000] = cmd;
			mem_[0xe001] = cmd;
		}
		mem_[0xffff] = 0;
		Sys32SignalIrq(1);
		/* NMI は RETN（ED 45）のみ。f1en 0066 は CALL でブート途中へ吸い込まれ、brival は DI。 */
		if (mem_[0x66] == 0xED && mem_[0x67] == 0x45)
			irqPulse_ = 1;
	}
}

/* I/O ポート読込 */
uint8_t CHardAc::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (board_) {
	case CEMU_AC_BOARD_HEBERPOP:
		/* 00-03 YM3438。busy bit7 は ymfm sticky — 0281 が RLCA;JR C で待つ。
		   80 OKI。C0 ラッチ。bit7 セットは SFX 経路。 */
		if (p <= 0x03)
			return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
		if (p == 0x80)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (p == 0xc0) {
			irqPulse_ = 0;
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_T5182:
		/* 00/01 YM2151。busy bit7 は ymfm が sticky なので落とす（0CC5 が無限待ち）。
		   10/11 セマフォは書専用。20: bit0 メインセマフォ、bit1 CPU IRQ。30 コイン。 */
		if (p == 0x00 || p == 0x01)
			return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
		if (p == 0x20)
			return (uint8_t)((seibuMainPending_ ? 1u : 0u)
				| ((seibuRst10_ & 2) ? 2u : 0u));
		if (p == 0x30)
			return 0x00;
		return 0xff;
	case CEMU_AC_BOARD_TAITO_OPM:
		if (taitoOpmMap_ == 17) {
			if (p == 0x00)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (p == 0x01)
				return chip_ ? chip_->ReadData() : 0xff;
			if (p == 0x02) {
				irqPulse_ = 0;
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		if (taitoOpmMap_ == 8 || taitoOpmMap_ == 11) {
			if (p == 0x00)
				/* $0915 は status bit7 busy 待ち。OPNA コアの busy を落とさないと初期化が無限ループ。
				   lomakai ISR は BIT 0,A（Timer A）でシーケンサをゲートするので bit0 を残す。 */
				return chip_ ? (uint8_t)((chip_->ReadStatus() & 0x03) | (taitoOpmMap_ == 11 ? 0x01 : 0x00)) : 0x00;
			if (p == 0x01)
				return chip_ ? chip_->ReadData() : 0xff;
			if (taitoOpmMap_ == 11)
				return 0xff;
			if (p == 0x04) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (p == 0x06)
				return (uint8_t)(soundCmdPending_ ? 1 : 0);
			return 0xff;
		}
		return 0xff;
	case CEMU_AC_BOARD_RAIZING:
		return RaizingPortIn(p);
	case CEMU_AC_BOARD_TECMO16: {
		if (tecmoOpl_ == 13) {
			/* MAME z80_sound_io: 0000-FFFF は audiocpu ROM。IN A,(C) が 8000+ の曲表を読む。 */
			const unsigned a = (unsigned)port;
			if (soundRom_ && a < soundRomSize_)
				return soundRom_[a];
			return mem_[a & 0xffffu];
		}
		if (tecmoOpl_ == 12) {
			/* MAME macross2_sound_io_map（powerins が共有）。busy は mazinger と同じく sticky。 */
			if (p == 0x00 || p == 0x01)
				return chip_ ? ((p & 1) ? chip_->ReadData() : (uint8_t)(chip_->ReadStatus() & 0x7fu)) : 0x00;
			if (p == 0x80)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (p == 0x88)
				return pcm2_ ? pcm2_->ReadStatus() : 0x00;
			return 0xff;
		}
		const int cave = CaveZ80Io();
		if (!cave)
			return 0xff;
		if (cave == 10) {
			/* pwrinst2: OKI 00/08、YM 40/41、ラッチ hi@60 lo@70 */
			if (p == 0x00)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (p == 0x08)
				return pcm2_ ? pcm2_->ReadStatus() : 0x00;
			if (p == 0x40 || p == 0x41)
				return chip_ ? ((p & 1) ? chip_->ReadData() : chip_->ReadStatus()) : 0x00;
			if (p == 0x60)
				return (uint8_t)(soundCmdWord_ >> 8);
			if (p == 0x70) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		if (p == 0x20)
			return 0x00; /* flags_r stub */
		if (p == 0x30) {
			soundCmdPending_ = 0;
			/* hotdogst NMI: IN 30 → E004。メインは E004 を 0x6D と比較するので上位バイト。mazinger は 8bit コマンドで IN 30 のみ。 */
			if (cave == 7)
				return (uint8_t)(soundCmdWord_ >> 8);
			return soundCmd_;
		}
		if (p == 0x40) {
			if (cave == 7)
				return soundCmd_;
			return (uint8_t)(soundCmdWord_ >> 8);
		}
		if (cave == 8) {
			if (p == 0x52 || p == 0x53) {
				if (!chip_) return 0x00;
				/* 07F9 は IN 52 AND 80 の busy 待ち。ymfm busy は AdvanceClocks 無しだと sticky。bit7 を落としてタイマ bit0-1 は残す。 */
				if (p & 1)
					return chip_->ReadData();
				return (uint8_t)(chip_->ReadStatus() & 0x7fu);
			}
			return 0xff;
		}
		if (p == 0x50 || p == 0x51) {
			if (!chip_) return 0x00;
			if (cave == 7)
				return (p & 1) ? chip_->ReadData() : chip_->ReadStatus();
			return chip_->ReadStatus();
		}
		if (p == 0x60)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (p == 0x80)
			return pcm2_ ? pcm2_->ReadStatus() : 0x00;
		return 0xff;
	}
	case CEMU_AC_BOARD_SYS16A:
	case CEMU_AC_BOARD_SYS16B:
	case CEMU_AC_BOARD_SYS24:
		if (p == 0x00 || p == 0x01) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			/* hwchamp（5521）はタイマ制御書のあと、Timer A に本物周期が付く前にステータス bit0 をソフト待ち。CPU 時間の最初の数秒はパルス（writeCount だけだと BGM 途中で切れ BIT 0,A;JR Z スピンへ再トラップ → WEAK）。 */
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
			return 0x00; /* uPD7759 busy_r << 7 — 非ビジー */
		if (p == 0xc0) {
			/* サウンドラッチ。アイドル時も goldnaxe の ISR は毎 YM tick で IN A,(C0) — 0 を返すと 0212/02C7 が OR A;JP Z,0B56 の mute-all 経路（TL=7F 永久）。0x80 はファームの空枠マーカ（02D4）で 02C7 は無視（AND #7F;RET Z）。Cotton は 0x80 を不一致として RET。
			   bayroute ISR は OR A;JP NZ,$0070 で F9AE リングへ積む。0x80 を空と見ず毎 tick が同じ曲に潰して SAMESONG。 */
			if (!soundCmdPending_)
				return 0x80;
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_SYS18:
		if (vsIoKind_ == 1) {
			/* MAME segaufo ufo_portmap: YM3438 40-43、PIT 00-03、315-5296 80-FF。
			   ブート 00C3 は IN (88-8B) / (C8-CB) が "SEGA" でないと JP 00A4 で EI しない。 */
			if (p >= 0x40 && p <= 0x43)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (p <= 0x03)
				return 0x00;
			{
				const unsigned n = (unsigned)p & 0x3fu;
				if (n >= 0x08u && n <= 0x0bu) {
					static const uint8_t kSega[4] = { 0x53, 0x45, 0x47, 0x41 };
					return kSega[n - 0x08u];
				}
			}
			return 0xff;
		}
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
		/* MAME system32_sound_portmap: YM1 @80-83 mirror 0C、YM2 @90-93。multi は YM 1 基。
		   f1en メインループは IN (80); BIT 1（Timer B）。ymfm が bit1 を上げない間は
		   SYS16B と同じ 256Hz パルス。bit0 は触らない（arabfgt/ga2 の RRA Timer A）。 */
		if ((p & 0xf0) == 0x80 || ((p & 0xf0) == 0x90 && !Sys32MultiPcm())) {
			CChip* ym = ((p & 0xf0) == 0x90 && chip2_) ? chip2_ : chip_;
			uint8_t st = ym ? ym->ReadStatus() : 0x00;
			st = (uint8_t)(st & 0x7fu);
			/* f1en BIT 1。ga2 は RRA Timer A だが、arabfgt/holo（空枠=0）へ bit0 を足すと
			   0xA0 が STOPS になる。0x80 キューのタイトル（001C=36 80）だけ bit0 もパルス。 */
			uint8_t pulse = 0x02u;
			if (mem_[0x1c] == 0x36u && mem_[0x1d] == 0x80u)
				pulse = (uint8_t)(pulse | 0x01u);
			if ((st & pulse) != pulse) {
				const uint64_t now = cpu_ ? (uint64_t)cpu_->time64() : cpuCycles_;
				const uint64_t period = (uint64_t)cpuHz_ / 256;
				if (period > 0) {
					const uint64_t slot = now / period;
					if (slot != abStatusPulseSlot_) {
						abStatusPulseSlot_ = slot;
						st = (uint8_t)(st | pulse);
					}
				}
			}
			return st;
		}
		return 0xff;
	case CEMU_AC_BOARD_OUTRUN:
	case CEMU_AC_BOARD_ABURNER:
		/* MAME segaorun / aburner: YM2151 @ 00/01（ミラー 00-3F）、ラッチ @ 40（ミラー 40-7F） */
		if (p < 0x40) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			/* After Burner メインループ: IN A,(01); BIT 0,A; JP Z,$0036。bit0 を常時オンにしない — 音楽エンジンが CPU レートで空転（良いノート少数 → 一時停止 → 超高速ゴミ）。OPM タイマ組前は約 256 Hz パルスでブートが抜けられるように。 */
			if (board_ == CEMU_AC_BOARD_ABURNER && (p & 1)
				&& chip_ && CEmuChipYm2151WriteCount(chip_) < 48) {
				/* smgp は IN A,(01); RRA; RRA; JR NC — Timer B (bit1) 待ち。AB は BIT 0。 */
				const uint8_t pulseBit = (vsIoKind_ == 1) ? (uint8_t)0x02 : (uint8_t)0x01;
				if (!(st & pulseBit)) {
					const uint64_t period = (uint64_t)cpuHz_ / 256;
					if (period > 0) {
						const uint64_t slot = cpuCycles_ / period;
						if (slot != abStatusPulseSlot_) {
							abStatusPulseSlot_ = slot;
							st = (uint8_t)(st | pulseBit);
						}
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
		/* MAME sound_portmap_2203: ラッチ @ 40 のみ。endurob2 は YM1 @00/01、YM2 @C0/C1、ラッチ 40。 */
		if (vsIoKind_ == 1) {
			if (p <= 0x01)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (p >= 0xc0 && p <= 0xc1)
				return chip2_ ? (uint8_t)(chip2_->ReadStatus() & 0x7fu) : 0x00;
		}
		if (p >= 0x40 && p < 0x80) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_VSYSTEM:
		/* Video System I/O 配置 3 種が 1 基板を共有:
		   0 aerofgt/gstriker/taotaido: YM 00-03、バンク 04、ack 08、ラッチ 0c
		   1 spinlbrk/turbofrc/f1gp/pspikes: バンク 00、ラッチ 14、YM 18-1b
		   2 fromanc2: ラッチ 00/04、YM 08-0b（バンク無し）
		   3 Psikyo gunbird: バンク 00、YM 04-07、ラッチ 08、ack 0c
		   4 pipedrm: バンク 04、ラッチ 16、ack 17、YM 18-1b
		   5 welltris/quiz18k: バンク 00、YM 08-0b、ラッチ 10、ack 18
		   6 Psikyo sngkace/samuraia: YM 00-03、バンク 04、ラッチ 08、ack 0c */
		if (vsIoKind_ == 6) {
			if (p <= 0x03 && chip_) {
				switch (p & 3) {
				case 0: return chip_->ReadStatus();
				case 1: return chip_->ReadData();
				case 2: return chip_->ReadStatusHi();
				default: return chip_->ReadDataHi();
				}
			}
			if (p == 0x08) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		{
			int ymOff = -1;
			if (vsIoKind_ == 1) {
				if (p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
			} else if (vsIoKind_ == 2) {
				if (p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
			} else if (vsIoKind_ == 4) {
				if (p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
			} else if (vsIoKind_ == 5) {
				if (p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
			} else if (vsIoKind_ == 3) {
				if (p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
			} else {
				if (p <= 0x03) ymOff = (int)(p & 3);
			}
			/* 交差配線を許容: 一部ダンプはまだ他方のデコードに当たる。fromanc2 の 00/04 はラッチ。pipedrm の 04/16/17 も YM ではない。 */
			if (vsIoKind_ != 2 && vsIoKind_ != 4 && vsIoKind_ != 5) {
				if (ymOff < 0 && p <= 0x03) ymOff = (int)(p & 3);
				if (ymOff < 0 && p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
			}
			if (ymOff < 0 && p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
			if (ymOff < 0 && p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
			if (ymOff >= 0 && chip_) {
				switch (ymOff & 3) {
				case 0: {
					uint8_t s = chip_->ReadStatus();
					if (vsIoKind_ == 4)
						s = (uint8_t)(s & 0x7fu);
					return s;
				}
				case 1: return chip_->ReadData();
				case 2: {
					uint8_t s = chip_->ReadStatusHi();
					if (vsIoKind_ == 4)
						s = (uint8_t)(s & 0x7fu);
					return s;
				}
				default: return chip_->ReadDataHi();
				}
			}
			if (p == 0x0c || p == 0x14 || p == 0x10 || (vsIoKind_ == 2 && (p == 0x00 || p == 0x04))
				|| (vsIoKind_ == 3 && p == 0x08)
				|| (vsIoKind_ == 4 && p == 0x16)) {
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
		/* MAME irem_m72 rtype/rtype2 + poundfor/m99（YM@40）を統合 */
		if (p == 0x00 || p == 0x01 || p == 0x40 || p == 0x41)
			return chip_ ? chip_->ReadStatus() : 0x00;
		/* set_separate_acknowledge(true) の generic_latch_8: ラッチ読は RST 18h 線をアサートしたまま、ポート 06/83/42 が ACK するまで。 */
		if (p == 0x02 || p == 0x80 || p == 0x42)
			return soundCmd_;
		if (p == 0x84) {
			const uint8_t v = (pcmRom_ && m72SampleAddr_ < pcmRomSize_)
				? pcmRom_[m72SampleAddr_] : 0x00;
			return v;
		}
		return 0xff;
	case CEMU_AC_BOARD_TOAPLAN1:
		if (toaplanKaneko_ == 3 || toaplanKaneko_ == 5)
			return 0xff;
		if (toaplanKaneko_ == 1) {
			if (p == 0x02 || p == 0x03)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (p == 0x04) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		/* YM3812 ステータスはゲーム本物のデータ／アドレスポートだけ。DIP/TJUMP ポートは 0x00（日本／アイドル）を返し、粘着タイマ旗ではない。 */
		{
			const uint8_t ym = toaplanYmPort_;
			if (p == ym || p == (uint8_t)(ym + 1u)) {
				uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
				if (toaplanTimerA_) {
					st |= 0x40; /* Timer A 旗 — ISR 音楽経路のゲート */
					toaplanTimerA_ = 0;
				}
				return st;
			}
		}
		/* DIP / TJUMP / SYSTEM / P1 / P2 — 日本テリトリ、コイン無し */
		return 0x00;
	case CEMU_AC_BOARD_SNK_OPL:
		/* I/O 00 = YM3812 ステータス／アドレス。20 = データ（書のみ）。古典はメモリマップ。mainsnk IN 00 は IRQ ACK（0xFF）。 */
		if (snkMapKind_ == 3)
			return 0xff;
		if (snkMapKind_)
			return 0xff;
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_TERRACRE:
		if (terracreMap_ >= 3 && terracreMap_ <= 5) {
			if (p == 0x00)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (p == 0x01)
				return chip_ ? chip_->ReadData() : 0xff;
			if (terracreMap_ >= 4 && (p == 0x80 || p == 0x81)) {
				if (p == 0x80)
					return chip2_ ? chip2_->ReadStatus() : 0x00;
				return chip2_ ? chip2_->ReadData() : 0xff;
			}
			return 0xff;
		}
		/* ラッチクリア @04、ラッチ読 @06。map 6 は YM2203 status も 00。 */
		if (p == 0x04) {
			soundCmdPending_ = 0;
			if (terracreMap_ == 6)
				soundCmd_ = 0;
			return 0x00;
		}
		if (p == 0x06) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (p == 0x00 || p == 0x01) {
			if (terracreMap_ == 6)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			return chip_ ? chip_->ReadStatus() : 0x00;
		}
		return 0xff;
	case CEMU_AC_BOARD_ROBOKID:
		if (vsIoKind_ == 15) {
			/* MAME mitchell_io_map: 00-02 入力。05 の bit0 は irq_source。
			   ISR は CPL;AND 1;JP NZ 映像。bit0=1 なら音源 CALL $7800/$7626。
			   bit3=0 は vblank（SYS0）。marukin ブートは CPL;AND 8;JR Z 待ち。
			   0xF7 = bit0=1・bit3=0。0xFF だと marukin が 013E でハングする。 */
			if (p <= 0x02)
				return 0xff;
			if (p == 0x05)
				return 0xf7;
			return 0xff;
		}
		if (vsIoKind_ == 13 || vsIoKind_ == 14)
			return 0xff;
		if (vsIoKind_ == 3) {
			if (p == 0x02)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (p == 0x03)
				return chip_ ? chip_->ReadData() : 0xff;
			if (p == 0x04)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (p == 0x06) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		if (vsIoKind_ == 4) {
			/* MAME djboy soundcpu_port_am: YM 02/03、ラッチ 04、OKI-L 06、OKI-R 07 */
			if (p == 0x02)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (p == 0x03)
				return chip_ ? chip_->ReadData() : 0xff;
			if (p == 0x04) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (p == 0x06)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (p == 0x07)
				return pcm2_ ? pcm2_->ReadStatus() : 0x00;
			return 0xff;
		}
		if (vsIoKind_ == 6) {
			/* MAME hvyunit sound_io: YM 02/03、ラッチ 04 */
			if (p == 0x02)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (p == 0x03)
				return chip_ ? chip_->ReadData() : 0xff;
			if (p == 0x04) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		if (vsIoKind_ == 7) {
			/* MAME crospang sound_io_map: YM 00/01、OKI 02、ラッチ 06 */
			if (p == 0x00 || p == 0x01)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (p == 0x02)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (p == 0x06) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		if (vsIoKind_ == 9 || vsIoKind_ == 10) {
			/* MAME nmg5 sound_io_map: YM 10/11、ラッチ 18、OKI 1C */
			if (p == 0x10 || p == 0x11)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (p == 0x18) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (p == 0x1c)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			return 0xff;
		}
		if (vsIoKind_ == 12) {
			/* MAME deniam sound_io_map: ラッチ 01、YM 02-03、OKI 05 */
			if (p == 0x01) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (p == 0x02 || p == 0x03)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (p == 0x05)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			return 0xff;
		}
		if (vsIoKind_ == 11) {
			/* MAME angelkds sound_portmap: YM1 00/01、YM2 40/41、メールボックス 80-83 */
			if (p == 0x00 || p == 0x01)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (p == 0x40 || p == 0x41)
				return chip2_ ? (uint8_t)(chip2_->ReadStatus() & 0x7f) : 0x00;
			if (p >= 0x80 && p <= 0x83) {
				switch (p & 3) {
				case 0: return raizingLatch_[0];
				case 1: return raizingLatch_[1];
				case 2: return raizingLatchOut_[0];
				default: return raizingLatchOut_[1];
				}
			}
			return 0xff;
		}
		if (vsIoKind_ == 5) {
			/* MAME blazeon_soundport: YM2151 02/03 は読どちらも status（RST 18 が IN 03 / RLA で busy 待ち）。ラッチ 06 */
			if (p == 0x02 || p == 0x03)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (p == 0x06) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return 0xff;
		}
		if (vsIoKind_ == 1 || vsIoKind_ == 2) {
			if (p == 0x00 || p == 0x01)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (vsIoKind_ == 1 && p == 0x80)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (vsIoKind_ == 1 && p == 0x88)
				return pcm2_ ? pcm2_->ReadStatus() : 0x00;
			return 0xff;
		}
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p == 0x80 || p == 0x81)
			return chip2_ ? chip2_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_ALPHA68K2:
		/* MAME generic_latch_8 読: データのみ。ACK は OUT 00 の clear_w。ここでクリアすると 2 回の IN 00 が ACK 後 0 と違い、毎 poll でコマンド 0x1x を再トリガ（RST 30 モード 3 が落ち着かない）。 */
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
		/* ZN: I/O 00 = soundlatch。この読の内側から NMI をパルスしない — ハンドラは IN 途中で、入れ子 NMI が下位バイトを格納したあと外側 LD (HL),A が F100 を上位バイト（0）で上書きする。 */
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
		/* 既定 GNG はメモリマップ。ironhors は YM2203 @ I/O 00/01。 */
		if (gngCommandoMap_ == 4) {
			if (!chip_) return 0xff;
			if ((p & 1) == 0)
				return (uint8_t)(chip_->ReadStatus() & 0x7fu);
			return chip_->ReadData();
		}
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_PCM:
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_SCRAMBLE:
		/* MAME scramble_sound_io_map: AY1 @10/20、AY2 @40/80。AY2 ポート A = ラッチ、ポート B = タイマ。
		   frogger: 1×AY。offset&0x40 = data、&0x80 = address。ポート A = ラッチ、B = タイマ（bit3/5 入れ替え）。 */
		if (vsIoKind_ == 1 || vsIoKind_ == 2) {
			if (p & 0x40) {
				const uint8_t a = (uint8_t)(ayAddr_[0] & 0x0f);
				if (a == 0x0e) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				if (a == 0x0f) {
					/* PCB は Port B bit3 = 700Hz。メインループは 0→1 エッジ待ち。
					   KonamiAyTimer の 10 段表は run() 中に止まって見え、待ちが無限になる。
					   読みのたびに bit5 をトグル（約 32 読みで半周期）。 */
					ayAddr_[2] = (uint8_t)(ayAddr_[2] + 1u);
					return (uint8_t)((ayAddr_[2] & 0x20u) ? 0x08u : 0x00u);
				}
				return chip_ ? chip_->ReadData() : 0xff;
			}
			return 0xff;
		}
		if (p == 0x20)
			return chip_ ? chip_->ReadData() : 0xff;
		if (p == 0x80) {
			const uint8_t a = (uint8_t)(ayAddr_[1] & 0x0f);
			if (a == 0x0e) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (a == 0x0f) {
				/* メインループ @02B3 は Port B bit7 の 0→1。表は IN 待ち中に cyc が止まって無限待ち。 */
				ayAddr_[2] = (uint8_t)(ayAddr_[2] + 1u);
				return (uint8_t)((ayAddr_[2] & 1u) ? 0x80u : 0x00u);
			}
			return chip2_ ? chip2_->ReadData() : 0xff;
		}
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_TIMEPLT:
		/* MAME megazone sound_io_map: OUT 00 address、OUT 02 data、IN 00-02 data_r。ポート A = タイマ nibble + I8039 status。 */
		if (vsIoKind_ == 1) {
			if (p <= 0x02) {
				const uint8_t a = (uint8_t)(ayAddr_[0] & 0x0f);
				if (a == 0x0e) {
					/* port_a_r: (timer<<4)|i8039_status。ブートは bit5 の 0 待ち→1 待ち。 */
					ayAddr_[2] = (uint8_t)(ayAddr_[2] + 1u);
					return (uint8_t)((ayAddr_[2] & 0x0fu) << 4);
				}
				return chip_ ? chip_->ReadData() : 0xff;
			}
			return 0xff;
		}
		return 0xff;
	case CEMU_AC_BOARD_TAITO_SJ:
		if (vsIoKind_ == 18 && p == 0x00)
			return soundCmd_;
		if (vsIoKind_ == 21)
			return 0x00;
		if (vsIoKind_ == 22 || vsIoKind_ == 23)
			return 0xff;
		if (vsIoKind_ == 19) {
			/* MAME tehkanwc sound_port: AY1 data_r @00、AY2 data_r @02 */
			if (p == 0x00)
				return chip_ ? chip_->ReadData() : 0xff;
			if (p == 0x02)
				return chip2_ ? chip2_->ReadData() : 0xff;
			return 0xff;
		}
		if (vsIoKind_ == 14 && p == 0x06)
			return (uint8_t)((soundCmdPending_ ? 0x80u : 0u) | (soundCmd_ & 0x7fu));
		if ((vsIoKind_ == 4 || vsIoKind_ == 5) && p == 0x06) {
			/* MAME cop01/magmax sound_command_r: (latch << 1) | timer Q の合成 */
			sjSemaphore2_ ^= 1;
			return (uint8_t)((soundCmd_ << 1) | (sjSemaphore2_ & 1u));
		}
		if (vsIoKind_ == 6 || vsIoKind_ == 7) {
			const uint8_t q = (uint8_t)(p & (uint8_t)~0x6e);
			CChip* ay = NULL;
			if (q <= 0x01) ay = chip_;
			else if (q == 0x10 || q == 0x11) ay = chip2_;
			else if (q == 0x80 || q == 0x81) ay = chip3_;
			if (ay && (q & 1))
				return ay->ReadData();
			return 0xff;
		}
		return 0xff;
	case CEMU_AC_BOARD_NAMCO_C352:
		return chip_ ? chip_->ReadStatus() : 0x00;
	case CEMU_AC_BOARD_SEGA_SYS1:
		if (vsIoKind_ == 4) {
			if (p == 0xbf || p == 0xbb)
				return 0x80; /* VDP status bit7 = vblank。RST38 が JP P $C100 を踏まない */
			if (p == 0x7e) {
				const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time64() : 0;
				unsigned line = (unsigned)((cyc / 342u) % 262u);
				if (line < 2u) line = 2u;
				if (line >= 0x80u) line = 2u + (line % 0x7eu);
				return (uint8_t)line;
			}
			if (p == 0xfa)
				return 0x00; /* PPI PC bit4 = ADC INTR。0F65 は bit4=0 待ち */
			if (p == 0xf8)
				return 0x80; /* アナログ中立 */
			return 0xff; /* E0/E1/E2 入力、F2/F3 DIP。active-low 開放 */
		}
		return 0xff;
	default:
		return 0xff;
	}
}

/* I/O ポート書込 */
void CHardAc::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (board_ == CEMU_AC_BOARD_HEBERPOP) {
		if (p <= 0x03) {
			if (chip_) {
				chip_->Write(p & 3, data);
				if (p & 1)
					opmWrites_++;
			}
			return;
		}
		if (p == 0x80) {
			if (pcm_)
				pcm_->Write(0, data);
			return;
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_T5182) {
		if (p == 0x00) {
			ymAddr_ = data;
			if (chip_) chip_->Write(0, data);
			return;
		}
		if (p == 0x01) {
			if (chip_) {
				chip_->Write(1, data);
				if (ymAddr_ != 0x0e && ymAddr_ != 0x0f)
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		if (p == 0x10) { seibuSubPending_ = 1; return; }
		if (p == 0x11) { seibuSubPending_ = 0; return; }
		if (p == 0x12) {
			seibuRst10_ &= ~4;
			if (chip_ && chip_->Irq())
				chip_->AckIrq();
			return;
		}
		if (p == 0x13) { seibuRst10_ &= ~2; return; }
		return;
	}
	if (!chip_) return;
	switch (board_) {
	case CEMU_AC_BOARD_TAITO_OPM:
		if (taitoOpmMap_ != 8 && taitoOpmMap_ != 11 && taitoOpmMap_ != 17)
			return;
		if (p == 0x00) {
			ymAddr_ = data;
			if (chip_) chip_->Write(0, data);
			return;
		}
		if (p == 0x01) {
			if (chip_) {
				chip_->Write(1, data);
				if (ymAddr_ != 0x0e && ymAddr_ != 0x0f)
					opmWrites_++;
			}
			if (taitoOpmMap_ == 17 && ymAddr_ == 0x0e && data != 0xff && soundRom_ && soundRomSize_ >= 0x4000u) {
				const unsigned src = (unsigned)(data & 7) * 0x4000u;
				unsigned n = 0x4000u;
				if (src < soundRomSize_) {
					if (src + n > soundRomSize_)
						n = soundRomSize_ - src;
					memcpy(mem_ + 0x8000, soundRom_ + src, n);
					if (n < 0x4000u)
						memset(mem_ + 0x8000 + n, 0xff, 0x4000u - n);
				}
			}
			if (taitoOpmMap_ == 8 && ymAddr_ == 0x0f && soundRom_ && soundRomSize_ >= 0x8000u) {
				const unsigned src = (unsigned)(data & 0x0f) * 0x8000u;
				unsigned n = 0x8000u;
				if (src < soundRomSize_) {
					if (src + n > soundRomSize_)
						n = soundRomSize_ - src;
					memcpy(mem_ + 0x8000, soundRom_ + src, n);
					if (n < 0x8000u)
						memset(mem_ + 0x8000 + n, 0xff, 0x8000u - n);
				}
			}
			return;
		}
		return;
	case CEMU_AC_BOARD_TAITO_SJ:
		if (vsIoKind_ == 22 || vsIoKind_ == 23)
			return;
		if (vsIoKind_ == 21) {
			/* MAME bankp io_map: SN1/2/3 @00/01/02。07 bit4 = vblank NMI。 */
			if (p == 0x00) {
				if (chip_) { chip_->Write(0, data); opmWrites_++; }
			} else if (p == 0x01) {
				if (chip2_) { chip2_->Write(0, data); opmWrites_++; }
			} else if (p == 0x02) {
				if (chip3_) { chip3_->Write(0, data); opmWrites_++; }
			} else if (p == 0x07) {
				sjNmiMask_ = (data >> 4) & 1;
				sjNmiMaskSeen_ = 1;
			}
			return;
		}
		if (vsIoKind_ == 8 || vsIoKind_ == 10) {
			/* MAME solomon / pbaction: AY1 10-11、AY2 20-21、AY3 30-31。pbaction は CTC @00-03 も。それらの書はここでは無視（Tick がファームから TRG0 + 126 Hz タイマを合成）。 */
			CChip* ay = NULL;
			if (p == 0x10 || p == 0x11) ay = chip_;
			else if (p == 0x20 || p == 0x21) ay = chip2_;
			else if (p == 0x30 || p == 0x31) ay = chip3_;
			if (ay) {
				ay->Write(p & 1, data);
				if (p & 1) opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ == 6 || vsIoKind_ == 7) {
			/* MAME bombjack/calorie audio_portmap: AY0 00-01、AY1 10-11、AY2 80-81、ミラー 0x6E */
			const uint8_t q = (uint8_t)(p & (uint8_t)~0x6e);
			CChip* ay = NULL;
			if (q <= 0x01) ay = chip_;
			else if (q == 0x10 || q == 0x11) ay = chip2_;
			else if (q == 0x80 || q == 0x81) ay = chip3_;
			if (ay) {
				ay->Write(q & 1, data);
				if (q & 1) opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ == 13) {
			/* MAME swimmer_audio_portmap: AY1 00-01 / AY2 80-81 は data_address_w
			   （偶数=data、奇数=address）。CEmu AY Write(0)=addr Write(1)=data。 */
			CChip* ay = NULL;
			if (p <= 0x01) ay = chip_;
			else if (p == 0x80 || p == 0x81) ay = chip2_;
			if (ay) {
				ay->Write((p & 1) ^ 1, data);
				if ((p & 1) == 0) opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ == 19) {
			/* MAME tehkanwc sound_port: AY1 00-01 / AY2 02-03 data_address_w */
			CChip* ay = NULL;
			if (p <= 0x01) ay = chip_;
			else if (p == 0x02 || p == 0x03) ay = chip2_;
			if (ay) {
				ay->Write((p & 1) ^ 1, data);
				if ((p & 1) == 0) opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ == 14) {
			if (p == 0x07) {
				soundCmdPending_ = 0;
				return;
			}
			CChip* ay = NULL;
			if (p <= 0x01) ay = chip_;
			else if (p <= 0x03) ay = chip2_;
			else if (p <= 0x05) ay = chip3_;
			if (ay) {
				ay->Write(p & 1, data);
				if (p & 1) opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ != 4 && vsIoKind_ != 5)
			return;
		{
			CChip* ay = NULL;
			if (p <= 0x01) ay = chip_;
			else if (p <= 0x03) ay = chip2_;
			else if (p <= 0x05) ay = chip3_;
			if (ay) {
				ay->Write(p & 1, data);
				if (p & 1) opmWrites_++;
			}
		}
		return;
	case CEMU_AC_BOARD_RAIZING:
		RaizingPortOut(p, data);
		return;
	case CEMU_AC_BOARD_SEIBU_OPL:
		/* MAME legionna godzilla_sound_io_map: OUT (00) が 512KiB OKI の set_rom_bank(data&1)。 */
		if (seibuSongOr80_ >= 3 && p == 0x00)
			SeibuOkiBank(data);
		return;
	case CEMU_AC_BOARD_TECMO16: {
		if (tecmoOpl_ == 13)
			return; /* I/O は ROM のみ */
		if (tecmoOpl_ == 12) {
			if (p == 0x00 || p == 0x01) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
				return;
			}
			if (p == 0x80) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (p == 0x88) {
				if (pcm2_) pcm2_->Write(0, data);
				return;
			}
			if (p >= 0x90 && p <= 0x97) {
				const int chip = (p >> 2) & 1;
				const unsigned banknum = (unsigned)(p & 3);
				const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
				CEmuAcNmk112Bank(raizingOkiBank_[chip], sz, banknum, data);
				return;
			}
			return;
		}
		const int cave = CaveZ80Io();
		if (!cave)
			return;
		if (p == 0x00 && cave != 10) {
			if (soundRom_ && soundRomSize_ > 0x4000u) {
				bankLoaded_ = 0;
				SetBank((int)(data & CEmuAcCaveZ80BankMask(cave)));
			}
			return;
		}
		if (cave == 10) {
			if (p == 0x00) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (p == 0x08) {
				if (pcm2_) pcm2_->Write(0, data);
				return;
			}
			if (p >= 0x10 && p <= 0x17) {
				const int chip = (p >> 2) & 1;
				const unsigned banknum = (unsigned)(p & 3);
				const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
				CEmuAcNmk112Bank(raizingOkiBank_[chip], sz, banknum, data);
				return;
			}
			if (p == 0x40 || p == 0x41) {
				if (chip_) {
					chip_->Write(p & 1u, data);
					if (p & 1) opmWrites_++;
				}
				return;
			}
			if (p == 0x50)
				return; /* soundlatch ACK */
			if (p == 0x80) {
				if (soundRom_ && soundRomSize_ > 0x8000u) {
					bankLoaded_ = 0;
					SetBank((int)(data & CEmuAcCaveZ80BankMask(10)));
				}
				return;
			}
			return;
		}
		if (p == 0x10)
			return; /* FIFO / latch ACK */
		if (cave == 8) {
			if (p == 0x50 || p == 0x51) {
				if (chip_) {
					chip_->Write(p & 1u, data);
					if (p & 1) opmWrites_++;
				}
				return;
			}
			if (p == 0x70) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (p == 0x74) {
				CEmuAcCaveOkiBank(raizingOkiBank_[0], pcmRomSize_, data, 0x03u);
				return;
			}
			return;
		}
		if (p == 0x50 || p == 0x51) {
			if (chip_) {
				chip_->Write(p & 1u, data);
				if (p & 1) {
					if (cave == 5 || cave == 9)
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
					else
						opmWrites_++;
				}
			}
			return;
		}
		if (p == 0x60) {
			if (pcm_) pcm_->Write(0, data);
			return;
		}
		if (p == 0x70 || p == 0x90 || p == 0xc0) {
			const int chip = (p == 0x90 || p == 0xc0) ? 1 : 0;
			const unsigned mask = (cave == 5) ? 0x0fu : (cave == 9) ? 0x07u : 0x03u;
			const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
			CEmuAcCaveOkiBank(raizingOkiBank_[chip], sz, data, mask);
			return;
		}
		if (p == 0x80) {
			if (pcm2_) pcm2_->Write(0, data);
			return;
		}
		return;
	}
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
			/* バンク窓 8000-DFFF — ROM 基板毎の MAME segas16b upd7759_control_w */
			if (soundRom_ && soundRomSize_ > 0x10000u) {
				unsigned size = soundRomSize_ - 0x10000u;
				if (size == 0) size = 1;
				unsigned bankoffs = 0;
				const unsigned bt = sys16RomBoard_;
				if (bt == 0x5358u || board_ == CEMU_AC_BOARD_SYS16A) {
					/* 171-5358: D2..D5 = A8..A11 の /CS（アクティブ Low） */
					if (!(data & 0x04u)) bankoffs = 0x00000u;
					if (!(data & 0x08u)) bankoffs = 0x10000u;
					if (!(data & 0x10u)) bankoffs = 0x20000u;
					if (!(data & 0x20u)) bankoffs = 0x30000u;
					bankoffs += (unsigned)(data & 0x03u) * 0x4000u;
				} else if (bt == 0x5521u || bt == 0x5704u) {
					/* 171-5521 / 5704: D3 が A11/A12 を選び、D0-D2 = A14-A16 */
					bankoffs = ((data & 0x08u) >> 3) * 0x20000u;
					bankoffs += (unsigned)(data & 0x07u) * 0x4000u;
				} else {
					/* 171-5797（既定）: D3=A11/A12、D4=A17、D0-D2=A14-A16 */
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
		if (vsIoKind_ == 1) {
			if (p >= 0x40 && p <= 0x43 && chip_) {
				chip_->Write(p & 3, data);
				if (p & 1)
					opmWrites_++;
			}
			return;
		}
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
		/* YM1 @80-8F、YM2 @90-9F。バンク A0/B0。IRQ コントローラ C0/D0。 */
		if ((p & 0xf0) == 0x80) {
			if (chip_) {
				chip_->Write(p & 3, data);
				if ((p & 3) == 1)
					opmWrites_++;
			}
		} else if ((p & 0xf0) == 0x90 && !Sys32MultiPcm()) {
			CChip* ym2 = chip2_ ? chip2_ : chip_;
			if (ym2) {
				ym2->Write(p & 3, data);
				if ((p & 3) == 1)
					opmWrites_++;
			}
		} else if (p >= 0xa0 && p <= 0xaf) {
			s32Bank_ = (s32Bank_ & ~0x3fu) | (unsigned)(data & 0x3f);
			Sys32ApplyBank();
		} else if (p >= 0xb0 && p <= 0xbf) {
			if (Sys32MultiPcm() && pcm_) {
				if (vsIoKind_ == 2)
					CEmuChipMultiPcmSetBankPair(pcm_, data & 7u, data & 7u);
				else
					CEmuChipMultiPcmSetBankPair(pcm_, data & 7u, (data >> 3) & 7u);
			} else {
				s32Bank_ = (s32Bank_ & 0x3fu)
					| ((unsigned)(data & 0x04) << 4)
					| ((unsigned)(data & 0x03) << 7);
				Sys32ApplyBank();
			}
		} else if (p >= 0xc0 && p <= 0xcf) {
			const unsigned off = (unsigned)(p & 0x0f);
			if (off & 1u)
				s32IrqIn_ = (uint8_t)(s32IrqIn_ & data);
		} else if (p >= 0xd0 && p <= 0xd7) {
			s32IrqCtrl_[p & 3] = data;
			if (soundCmdPending_) {
				/* ブート LDIR が E000 を消したあと、D1=V60 マップでコマンドを枠へ戻す。 */
				const uint8_t cmd = soundCmd_;
				int slotted = 0;
				for (unsigned i = 0; i < 0x10u; i++) {
					if (mem_[0xe000u + i] == 0x80u) {
						mem_[0xe000u + i] = cmd;
						slotted = 1;
						break;
					}
				}
				if (!slotted) {
					mem_[0xe000] = cmd;
					mem_[0xe001] = cmd;
				}
				Sys32SignalIrq(1);
			}
		}
		break;
	case CEMU_AC_BOARD_GNG:
		if (gngCommandoMap_ == 4) {
			chip_->Write(p & 1, data);
			if (p & 1)
				opmWrites_++;
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
	case CEMU_AC_BOARD_HANGON:
		if (vsIoKind_ == 1) {
			CChip* ym = NULL;
			if (p <= 0x01) ym = chip_;
			else if (p >= 0xc0 && p <= 0xc1) ym = chip2_;
			if (ym) {
				ym->Write(p & 1, data);
				if (p & 1) opmWrites_++;
			}
		}
		break;
	case CEMU_AC_BOARD_VSYSTEM:
		if (vsIoKind_ == 6) {
			/* MAME sngkace_sound_io_map: YM @00-03、バンク @04 は data&3、ラッチ @08、ack @0c。 */
			if (p <= 0x03) {
				if (chip_) {
					chip_->Write(p & 3, data);
					if ((p & 3) == 1)
						opmWrites_++;
				}
			} else if (p == 0x04)
				SetBank(data & 0x03);
			else if (p == 0x0c)
				ClearSoundCmdPending();
			break;
		}
		{
			int ymOff = -1;
			if (vsIoKind_ == 1) {
				if (p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
				else if (p == 0x00 || p == 0x0c) SetBank(data & 0x03);
				else if (p == 0x14) ClearSoundCmdPending();
			} else if (vsIoKind_ == 2) {
				if (p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
				else if (p == 0x0c) ClearSoundCmdPending();
				if (ymOff >= 0) {
					chip_->Write(ymOff & 3, data);
					if ((ymOff & 3) == 1)
						opmWrites_++;
				}
				break;
			} else if (vsIoKind_ == 4) {
				if (p >= 0x18 && p <= 0x1b) ymOff = (int)(p - 0x18);
				else if (p == 0x04) SetBank(data & 0x03);
				else if (p == 0x17) ClearSoundCmdPending();
				if (ymOff >= 0) {
					chip_->Write(ymOff & 3, data);
					if ((ymOff & 3) == 1)
						opmWrites_++;
				}
				break;
			} else if (vsIoKind_ == 5) {
				if (p >= 0x08 && p <= 0x0b) ymOff = (int)(p - 0x08);
				else if (p == 0x00) SetBank(data & 0x03);
				else if (p == 0x18) ClearSoundCmdPending();
				if (ymOff >= 0) {
					chip_->Write(ymOff & 3, data);
					if ((ymOff & 3) == 1)
						opmWrites_++;
				}
				break;
			} else if (vsIoKind_ == 3) {
				/* MAME gunbird_sound_io_map: バンク @00 は (data>>4)&3、YM @04-07、ラッチ @08、ack @0c。aerofgt 交差配線へフォールスルーしない: それはバンク書を YM アドレス、ACK を SetBank(cmd) と扱い、全タイトルをバンク 0 に固定した。 */
				if (p >= 0x04 && p <= 0x07) ymOff = (int)(p - 0x04);
				else if (p == 0x00) {
					SetBank((data >> 4) & 0x03);
					break;
				} else if (p == 0x0c) {
					ClearSoundCmdPending();
					break;
				}
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
		else if (p == 0x04)
			SetBank(data);
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
			ClearSoundCmdPending(); /* ラッチ ACK */
		} else if (p == 0x80 || p == 0x81 || p == 0x10 || p == 0x11
			|| p == 0x46 || p == 0x47) {
			/* rtype2_sample_addr_w / poundfor_sample_addr_w（11bit グラニュール） */
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
		if (toaplanKaneko_ == 3 || toaplanKaneko_ == 5)
			break;
		if (toaplanKaneko_ == 1) {
			if (p == 0x02) {
				chip_->Write(0, data);
			} else if (p == 0x03) {
				chip_->Write(1, data);
				opmWrites_ = CEmuChipYm3812WriteCount(chip_);
			}
			/* I/O 04 書 = 68k への soundlatch[1]（ブートハンドシェイク） */
			break;
		}
		/* 既知の Toaplan1 YM ポート対をすべて受ける（非ネイティブポートへの書は無害。読は PortIn でフィルタ）。 */
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
			break; /* 古典 SNK はメモリマップ */
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x20 || p == 0x01) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm3812WriteCount(chip_);
		}
		/* 0x40/0x80 = uPD7759 — FM BGM 経路では無視 */
		break;
	case CEMU_AC_BOARD_TERRACRE:
		if (terracreMap_ >= 3 && terracreMap_ <= 5) {
			CChip* ym = NULL;
			if (p <= 0x01) ym = chip_;
			else if (terracreMap_ >= 4 && (p == 0x80 || p == 0x81)) ym = chip2_;
			if (ym) {
				ym->Write(p & 1, data);
				if (p & 1) opmWrites_++;
			}
			break;
		}
		/* MAME sound_3526_io_map / sound_2203_io_map: YM @00/01、DAC @02/03（未実装 stub） */
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x01) {
			chip_->Write(1, data);
			if (terracreMap_ == 6)
				opmWrites_++;
			else
				opmWrites_ = CEmuChipYm3812WriteCount(chip_);
		}
		break;
	case CEMU_AC_BOARD_ROBOKID:
		if (vsIoKind_ == 15) {
			/* MAME mitchell_io_map: 02 バンク、03 YM2413 data、04 addr、05 OKI。 */
			if (p == 0x02) {
				bank_ = (int)(data & 0x0f);
				if (soundRom_ && soundRomSize_ > 0x8000u) {
					unsigned src = 0x8000u + (unsigned)bank_ * 0x4000u;
					unsigned n = 0x4000u;
					if (src < soundRomSize_) {
						if (src + n > soundRomSize_)
							n = soundRomSize_ - src;
						memcpy(mem_ + 0x8000, soundRom_ + src, n);
					}
				}
			} else if (p == 0x04) {
				alphaOpllAddr_ = data;
			} else if (p == 0x03) {
				s_alphaOpllRegs[alphaOpllAddr_ & 63] = data;
				s_alphaOpllWrites++;
				opmWrites_++;
				if (alphaOpll_)
					OPLL_writeReg((OPLL*)alphaOpll_, alphaOpllAddr_, data);
				FmMonShadowApplyOpllRegs(s_alphaOpllRegs);
			} else if (p == 0x05) {
				if (pcm_) pcm_->Write(0, data);
			}
			break;
		}
		if (vsIoKind_ == 13 || vsIoKind_ == 14)
			break;
		if (vsIoKind_ == 3) {
			/* MAME airbustr sound_io_map: 00 バンク、02/03 YM、04 OKI、06 ラッチ */
			if (p == 0x00) {
				bankLoaded_ = 0;
				SetBank((int)(data & 7));
			} else if (p == 0x02 || p == 0x03) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			} else if (p == 0x04) {
				if (pcm_) pcm_->Write(0, data);
			}
			break;
		}
		if (vsIoKind_ == 4) {
			/* MAME djboy soundcpu_port_am: 00 バンク、02/03 YM、06/07 OKI */
			if (p == 0x00) {
				bankLoaded_ = 0;
				SetBank((int)(data & 7));
			} else if (p == 0x02 || p == 0x03) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			} else if (p == 0x06) {
				if (pcm_) pcm_->Write(0, data);
			} else if (p == 0x07) {
				if (pcm2_) pcm2_->Write(0, data);
			}
			break;
		}
		if (vsIoKind_ == 6) {
			/* MAME hvyunit sound_io: 00 バンク &3、02/03 YM、04 ラッチ */
			if (p == 0x00) {
				bankLoaded_ = 0;
				SetBank((int)(data & 3));
			} else if (p == 0x02 || p == 0x03) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			}
			break;
		}
		if (vsIoKind_ == 7) {
			/* MAME crospang sound_io_map: YM 00/01、OKI 02、ラッチ 06 */
			if (p == 0x00 || p == 0x01) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
			} else if (p == 0x02) {
				if (pcm_) pcm_->Write(0, data);
			}
			break;
		}
		if (vsIoKind_ == 9 || vsIoKind_ == 10) {
			/* MAME nmg5 sound_io_map: 00 OKI バンク、10/11 YM、1C OKI */
			if (p == 0x00 && pcm_) {
				unsigned* t = raizingOkiBank_[0];
				const unsigned base = (data & 1u) ? 2u : 0u;
				t[0] = t[1] = t[2] = t[3] = base;
				t[4] = t[5] = t[6] = t[7] = base + 1u;
			} else if (p == 0x10 || p == 0x11) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
			} else if (p == 0x1c) {
				if (pcm_) pcm_->Write(0, data);
			}
			break;
		}
		if (vsIoKind_ == 12) {
			/* MAME deniam: YM 02/03、OKI 05、バンク 07 bit6 */
			if (p == 0x02 || p == 0x03) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
			} else if (p == 0x05) {
				if (pcm_) pcm_->Write(0, data);
			} else if (p == 0x07 && pcm_) {
				unsigned* t = raizingOkiBank_[0];
				const unsigned base = ((data >> 6) & 1u) ? 2u : 0u;
				t[0] = t[1] = t[2] = t[3] = base;
				t[4] = t[5] = t[6] = t[7] = base + 1u;
			}
			break;
		}
		if (vsIoKind_ == 11) {
			/* MAME angelkds sound_portmap: YM1 00/01、YM2 40/41。OUT 80-83 は m_sound2（ホスト読、無視）。 */
			if (p == 0x00 || p == 0x01) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			} else if (p == 0x40 || p == 0x41) {
				if (chip2_) {
					chip2_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			}
			break;
		}
		if (vsIoKind_ == 5) {
			/* MAME blazeon_soundport: YM2151 02/03、ラッチ 06 */
			if (p == 0x02 || p == 0x03) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
			}
			break;
		}
		if (vsIoKind_ == 2) {
			/* MAME tharrier_sound_io_map: YM 00/01 のみ。OKI はメモリマップ */
			if (p == 0x00 || p == 0x01) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			}
			break;
		}
		if (vsIoKind_ == 1) {
			/* MAME macross2_sound_io_map: YM 00/01、OKI0 80、OKI1 88、NMK112 90-97 */
			if (p == 0x00 || p == 0x01) {
				if (chip_) {
					chip_->Write(p & 1, data);
					if (p & 1) opmWrites_++;
				}
			} else if (p == 0x80) {
				if (pcm_) pcm_->Write(0, data);
			} else if (p == 0x88) {
				if (pcm2_) pcm2_->Write(0, data);
			} else if (p >= 0x90 && p <= 0x97) {
				const int chip = (p >> 2) & 1;
				const unsigned banknum = (unsigned)(p & 3);
				const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
				CEmuAcNmk112Bank(raizingOkiBank_[chip], sz, banknum, data);
			}
			break;
		}
		/* MAME ninjakd2_sound_io: YM2203 #1 @00/01、#2 @80/81 の配置 */
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
		/* MAME: 00=ラッチクリア、08=DAC、0a/0b=YM2413、0c/0d=YM2203、0e=バンク。mmpanic: 08/09=YM2413、0c=AY データ、0e=AY アドレス。 */
		if (vsIoKind_ == 1) {
			if (p == 0x08)
				alphaOpllAddr_ = data;
			else if (p == 0x09) {
				s_alphaOpllRegs[alphaOpllAddr_ & 63] = data;
				s_alphaOpllWrites++;
				if (alphaOpll_)
					OPLL_writeReg((OPLL*)alphaOpll_, alphaOpllAddr_, data);
				FmMonShadowApplyOpllRegs(s_alphaOpllRegs);
			} else if (p == 0x0e) {
				if (chip_) chip_->Write(0, data);
			} else if (p == 0x0c) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_++;
				}
			}
			break;
		}
		{
			const uint8_t lo = (uint8_t)(p & 0x0f);
			if (lo <= 0x01) {
				/* MAME soundlatch clear_w: pending+data。ブート OUT 00 @C088 は注入前に走る。0x1x 後の曲 ACK がカタログバイトを残すと poll ループが BGM を永久再開。 */
				soundCmdPending_ = 0;
				soundCmd_ = 0;
			} else if (lo == 0x08 || lo == 0x09) {
				if (pcm_) pcm_->Write(0, data);
			} else if (lo == 0x0a) {
				alphaOpllAddr_ = data;
			} else if (lo == 0x0b) {
				s_alphaOpllRegs[alphaOpllAddr_ & 63] = data;
				s_alphaOpllWrites++;
				if (alphaOpll_)
					OPLL_writeReg((OPLL*)alphaOpll_, alphaOpllAddr_, data);
				FmMonShadowApplyOpllRegs(s_alphaOpllRegs);
			} else if (lo == 0x0c) {
				alphaYmAddr_ = data;
				if (chip_) chip_->Write(0, data);
			} else if (lo == 0x0d) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_++;
				}
				/* MAME porta_w: 未使用 YM ポートの 0xFF を飛ばす。許可は bit0 アクティブ Low。エッジのみ 1→0 は pa_latch がまだ 0 リセットの最初の OUT 0D,0 を逃した。 */
				if (alphaYmAddr_ == 0x0e) {
					if (data != 0xffu) {
						alphaNmiMask_ = (uint8_t)((data & 1) ? 0 : 1);
						alphaPaLatch_ = (uint8_t)(data & 1);
					}
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
		if (vsIoKind_ == 1 || vsIoKind_ == 2) {
			if (p & 0x40) {
				if (chip_) { chip_->Write(1, data); opmWrites_++; }
			} else if (p & 0x80) {
				ayAddr_[0] = (uint8_t)(data & 0x0f);
				if (chip_) chip_->Write(0, data);
			}
			break;
		}
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
	case CEMU_AC_BOARD_KONAMI_TIMEPLT:
		if (vsIoKind_ == 1) {
			if (p == 0x00) {
				ayAddr_[0] = (uint8_t)(data & 0x0f);
				if (chip_) chip_->Write(0, data);
			} else if (p == 0x02) {
				if (chip_) { chip_->Write(1, data); opmWrites_++; }
			}
		}
		break;
	case CEMU_AC_BOARD_NAMCO_C352:
		chip_->Write(p >> 1, data);
		break;
	case CEMU_AC_BOARD_SEGA_SYS1:
		if (vsIoKind_ == 4) {
			if (p == 0x7b) {
				if (chip_) { chip_->Write(0, data); opmWrites_++; }
			} else if (p == 0x7e || p == 0x7f) {
				CChip* sn = chip2_ ? chip2_ : chip_;
				if (sn) { sn->Write(0, data); opmWrites_++; }
			}
		}
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------------
   Raizing / Eighting 音源区画（MAME toaplan/raizing.cpp と raizing_batrider.cpp）。4 改訂が Z80 1 基を共有するが他はほぼ一致せず、raizingType_ が各マップをゲート。CEMU_AC_BOARD_RAIZING 参照。
   ------------------------------------------------------------------------ */

void CHardAc::RaizingSetZ80Bank(unsigned entry)
{
	if (!soundRom_ || raizingType_ == 1 || raizingType_ == 4) return;
	const unsigned banks = soundRomSize_ / 0x4000u;
	if (!banks) return;
	/* Battle Garegga は 16K バンク 8 本だけ。4bit セレクタ全体へミラー（MAME init_bgaregga は 0-7 を 2 回組む）。 */
	const unsigned src = (entry % banks) * 0x4000u;
	unsigned n = 0x4000u;
	if (src + n > soundRomSize_) n = soundRomSize_ - src;
	memset(mem_ + 0x8000, 0xff, 0x4000);
	if (n) memcpy(mem_ + 0x8000, soundRom_ + src, n);
	bank_ = (int)entry;
}

/* PCM／コードバンク */
void CHardAc::RaizingOkiBankW(unsigned offset, uint8_t data)
{
	/* MAME raizing_oki_bankswitch_w: 1 書で窓 2 つを組む — 下位ニブルが `offset`、上位ニブルが `offset`+1 — 各項目はフレーズ表ページとその 64K データ窓の両方へ着く。オフセット bit 2 がチップを選ぶので Batrider の C0-C6 は両 OKI に届き、Battle Garegga の E006-E008 は常に最初だけ。 */
	for (unsigned half = 0; half < 2; half++) {
		const unsigned o = offset + half;
		const unsigned chip = (o & 4u) >> 2;
		const unsigned slot = o & 3u;
		const unsigned entry = (half ? (unsigned)(data >> 4) : data) & 0x0fu;
		raizingOkiBank_[chip][slot] = entry;
		raizingOkiBank_[chip][4 + slot] = entry;
	}
}

/* MAME raizing.cpp sound_z80_mem。mahoudai: 0000-BFFF ROM、C000-DFFF は 68000 と共有 RAM、YM2151 E000/E001、OKI E004、コインカウンタ E00E。bgaregga は 8000 に 16K バンク窓を足し、メールボックスをラッチに置換: OKI バンク E006-E008、Z80 バンク E00A、ラッチ ACK E00C、ラッチ読 E01C、pending 旗 E01D。Batrider と Battle Bakraid はメモリにチップ無し — I/O ポート（RaizingPortOut）。 */
void CHardAc::RaizingMemWrite(uint16_t addr, uint8_t data)
{
	const uint16_t ramEnd = (raizingType_ == 4) ? 0xffff : 0xdfff;
	if (addr >= 0xc000 && addr <= ramEnd) {
		mem_[addr] = data;
		return;
	}
	if (raizingType_ >= 3) return; /* ROM */
	switch (addr) {
	case 0xe000:
	case 0xe001:
		if (chip_) {
			chip_->Write(addr & 1u, data);
			if (addr & 1) opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		}
		return;
	case 0xe004:
		if (pcm_) pcm_->Write(0, data);
		return;
	case 0xe006:
	case 0xe007:
	case 0xe008:
		if (raizingType_ == 2) RaizingOkiBankW(addr - 0xe006u, data);
		return;
	case 0xe00a:
		if (raizingType_ == 2) RaizingSetZ80Bank(data & 0x0fu);
		return;
	case 0xe00c:
		/* generic_latch_8 separate_acknowledge: この書だけが IRQ0 を落とす */
		if (raizingType_ == 2) raizingLatchPending_ = 0;
		return;
	default:
		return;
	}
}

/* バス読込 */
uint8_t CHardAc::RaizingMemRead(uint16_t addr)
{
	if (raizingType_ >= 3 || addr <= 0xdfff)
		return mem_[addr];
	switch (addr) {
	case 0xe000:
	case 0xe001:
		return chip_ ? chip_->ReadStatus() : 0x00;
	case 0xe004:
		return pcm_ ? pcm_->ReadStatus() : 0x00;
	case 0xe01c:
		return raizingLatch_[0];
	case 0xe01d:
		/* MAME bgaregga_E01D_r: bit 0 クリアはコマンド待ち */
		return (uint8_t)(raizingLatchPending_ ? 0x00 : 0x01);
	default:
		return 0x00;
	}
}

/* MAME batrider_sound_z80_port / bbakraid_sound_z80_port:
     40/42  68000 への応答ラッチ
     44     68000 の音源 IRQ をアサート    46  この Z80 の NMI をクリア
     48/4A  ホストコマンドラッチ 2 本
     80/81  YM2151（Batrider）または YMZ280B（Battle Bakraid）
     82/84  OKI #1 / #2   88  Z80 ROM バンク   C0-C6  OKI サンプルバンク   */
void CHardAc::RaizingPortOut(uint8_t port, uint8_t data)
{
	switch (port) {
	case 0x40:
	case 0x42:
		raizingLatchOut_[(port >> 1) & 1] = data;
		return;
	case 0x44:
		return; /* 68000 への音源 IRQ — 向こう側に何も居ない */
	case 0x46:
		raizingNmiPending_ = 0;
		return;
	case 0x80:
	case 0x81:
		if (!chip_) return;
		if (raizingType_ == 4) {
			CEmuChipYmz280bWritePort(chip_, port & 1u, data);
			if (port & 1) opmWrites_++;
		} else {
			chip_->Write(port & 1u, data);
			if (port & 1) opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		}
		return;
	case 0x82:
		if (pcm_) pcm_->Write(0, data);
		return;
	case 0x84:
		if (pcm2_) pcm2_->Write(0, data);
		return;
	case 0x88:
		RaizingSetZ80Bank(data & 0x0fu);
		return;
	default:
		if (port >= 0xc0 && port <= 0xc6)
			RaizingOkiBankW(port - 0xc0u, data);
		return;
	}
}

/* CHardAc::RaizingPortIn の実装 */
uint8_t CHardAc::RaizingPortIn(uint8_t port)
{
	switch (port) {
	case 0x48:
	case 0x4a:
		return raizingLatch_[(port >> 1) & 1];
	case 0x80:
	case 0x81:
		if (!chip_) return 0x00;
		return (raizingType_ == 4) ? CEmuChipYmz280bReadStatus(chip_)
			: chip_->ReadStatus();
	case 0x82:
		return pcm_ ? pcm_->ReadStatus() : 0x00;
	case 0x84:
		return pcm2_ ? pcm2_->ReadStatus() : 0x00;
	default:
		return 0x00;
	}
}

/* 68000 は常に move.l 1 本で両ラッチを書くので、片方しか見ない基板でもコマンドは (code, data) 対。 */
int CHardAc::RaizingTrackIdle()
{
	if (raizingType_ == 4) {
		for (int i = 0; i < 8; i++) {
			if (mem_[0xc000 + i * 16] & 0x80)
				return 0;
		}
		if (CEmuChipYmz280bPlayingCount(chip_) > 0)
			return 0;
		return 1;
	}
	/* KeyOnCount は累積なので、!= 0 だけだと終わったジングルも「忙しい」。新規 KeyOn も OKI ボイスも無い 1 秒区間をトラック終端と見る。ループ BGM は $08 をストローブし続け、OKI 多用曲は ReadStatus を立て続けるのでライブのまま。 */
	const unsigned keys = chip_ ? CEmuChipYm2151KeyOnCount(chip_) : 0;
	const int oki = (pcm_ && (pcm_->ReadStatus() & 0x0f))
		|| (pcm2_ && (pcm2_->ReadStatus() & 0x0f));
	const int grew = keys > raizingLastKeyOns_;
	raizingLastKeyOns_ = keys;
	if (oki || grew) {
		raizingIdlePolls_ = 0;
		return 0;
	}
	if (raizingIdlePolls_ < 4) {
		raizingIdlePolls_++;
		return 0;
	}
	return 1;
}

/* CHardAc::RaizingPostCommand の実装 */
void CHardAc::RaizingPostCommand(uint8_t cmd, uint8_t data)
{
	raizingIdlePolls_ = 0;
	raizingLatch_[0] = cmd;
	raizingLatch_[1] = data;
	if (raizingType_ == 1) {
		/* mahoudai にラッチは無い: メールボックスは共有 RAM 先頭 2 バイト。C001 が要求型（0 = 音開始）、C000 がコード。Z80 はまず C000 を poll して見るか決め、C000 に 0xFF を置いて応答するので C001 を先に書く。 */
		mem_[0xc001] = data;
		mem_[0xc000] = cmd;
		return;
	}
	if (raizingType_ == 2) {
		raizingLatchPending_ = 1; /* E00C ACK まで IRQ0 を保持 */
		return;
	}
	raizingNmiPending_ = 1;
}

/* メモリ 8bit 書込 */
void CHardAc::MemWrite(uint16_t addr, uint8_t data)
{
	if (board_ == CEMU_AC_BOARD_HEBERPOP) {
		/* 0000-F7FF ROM。F800-FFFF 2K RAM（スタックは SP=0000 で FFFE へラップ）。 */
		if (addr >= 0xf800)
			mem_[addr] = data;
		return;
	}
	if (board_ == CEMU_AC_BOARD_BIONICC) {
		if (addr == 0x8000 || addr == 0x8001) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_++;
			}
			return;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff)
			mem_[addr] = data;
		return;
	}
	if (board_ == CEMU_AC_BOARD_T5182) {
		/* 2000-3FFF: 2K RAM ミラー。4000-7FFF: 共有 256B ミラー。ROM は書かない。 */
		if (addr >= 0x2000 && addr <= 0x3fff) {
			mem_[0x2000u + (addr & 0x7ffu)] = data;
			return;
		}
		if (addr >= 0x4000 && addr <= 0x7fff) {
			mem_[0x4000u + (addr & 0xffu)] = data;
			return;
		}
		return;
	}
	/* MAME toaplan1 sound_map: 0000-7FFF ROM、8000-87FF 共有 RAM。Wardner（twincobr_m）: 作業 8000-807F、共有コマンド RAM C000-C7FF。 */
	if (board_ == CEMU_AC_BOARD_TOAPLAN1) {
		if (toaplanKaneko_ == 3 || toaplanKaneko_ == 5) {
			/* MAME tigerh_sound_map / perfrman_sound_map: AY1 A080/A082、AY2 A090/A092、NMI 許可 A0E0 / 禁止 A0F0。RAM C800 または 8800。 */
			if (addr == 0xa080) {
				if (chip_) chip_->Write(0, data);
				return;
			}
			if (addr == 0xa082) {
				if (chip_) { chip_->Write(1, data); opmWrites_++; }
				return;
			}
			if (addr == 0xa090) {
				if (chip2_) chip2_->Write(0, data);
				return;
			}
			if (addr == 0xa092) {
				if (chip2_) { chip2_->Write(1, data); opmWrites_++; }
				return;
			}
			if (addr == 0xa0e0) {
				flstoryNmiEn_ = 1;
				return;
			}
			if (addr == 0xa0f0) {
				flstoryNmiEn_ = 0;
				return;
			}
			if (toaplanKaneko_ == 5) {
				if (addr >= 0x8800 && addr < 0x9000)
					mem_[addr] = data;
				return;
			}
			if (addr >= 0xc800)
				mem_[addr] = data;
			return;
		}
		if (toaplanKaneko_ == 2) {
			/* Wardner: 作業 8000-807F、共有 C000-C7FF、追加 RAM C800-CFFF（ブート LD SP,C800） */
			if ((addr >= 0x8000 && addr <= 0x807f)
				|| (addr >= 0xc000 && addr <= 0xcfff))
				mem_[addr] = data;
			return;
		}
		if (toaplanKaneko_ == 4) {
			/* MAME pipibibs_sound_z80_mem: RAM 8000-87FF、YM3812 E000-E001 */
			if (addr == 0xe000 || addr == 0xe001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
				return;
			}
			if (addr >= 0x8000 && addr <= 0x87ff)
				mem_[addr] = data;
			return;
		}
		if (addr >= 0x8000 && addr <= 0x87ff)
			mem_[addr] = data;
		return;
	}
	/* MAME snk68 sound_map: 0000-EFFF ROM、F000-F7FF RAM、F800 ラッチ。古典 SNK（athena/…）: C000-CFFF RAM、E000 ラッチ、E800/EC00 YM1、F000/F400 YM2、F800 ステータス。aso: RAM C000 YM F000。mainsnk: RAM 8000 AY E000/E008。 */
	if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		if (snkMapKind_ == 3) {
			if (addr >= 0x8000 && addr <= 0x87ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xe000 || addr == 0xe001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr == 0xe008 || addr == 0xe009) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			return;
		}
		if (snkMapKind_ == 2) {
			if (addr >= 0xc000 && addr <= 0xc7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf000 || addr == 0xf001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
				return;
			}
			return;
		}
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
				/* MAME sound_status_w: 下位ニブルは 0x0F。0 の上位ニブルビットが対応ステータス旗をクリア（keep = data>>4）。 */
				snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)(data >> 4));
				return;
			}
			return;
		}
		if (addr >= 0xf000 && addr <= 0xf7ff)
			mem_[addr] = data;
		/* F800 書 = soundlatch2（メインへの ACK）— 無視 */
		return;
	}
	/* Seibu: RAM 2000-27FF + 音源 I/O 4000-401B + OKI 6000 */
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
			seibuRst18_ = 0; /* irq_clear / RST18 の EOI */
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
				if (addr & 1) {
					opmWrites_ = (seibuSongOr80_ >= 2)
						? CEmuChipYm2151WriteCount(chip_)
						: CEmuChipYm3812WriteCount(chip_);
				}
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
		if (addr == 0x6002 && seibuSongOr80_ == 2) {
			if (pcm2_) pcm2_->Write(0, data);
			return;
		}
		return;
	}
	/* MAME taito_f2 / taito_h / asuka(bonzeadv) sound_map: 0000-3FFF ROM、4000-7FFF バンク、C000-DFFF RAM、E000-E003 YM2610、E200 slave_port_w、E201 slave_comm_w、F200 バンクスイッチ。 */
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
		return; /* E400 パン / EE00 / F000 は no-op。ROM は poke しない */
	}
	/* MAME taito_rastan / taito_asuka 基本マップ: 0000-3FFF ROM、4000-7FFF バンク、8000-8FFF RAM、9000/9001 YM2151、A000/A001 PC060HA、B000/C000/D000 MSM5205（未エミュ — それらの曲はすべて YM2151）。 */
	if (board_ == CEMU_AC_BOARD_TAITO_OPM) {
		if (taitoOpmMap_ != 14 && taitoOpmMap_ != 16 && taitoOpmMap_ != 17 && taitoOpmMap_ != 18 && mem_[0] == 0xc3
			&& ((mem_[1] == 0x89 && mem_[2] == 0x00)
				|| (mem_[1] == 0xd0 && mem_[2] == 0x03)))
			taitoOpmMap_ = 14;
		if (taitoOpmMap_ != 14 && taitoOpmMap_ != 16 && taitoOpmMap_ != 17 && taitoOpmMap_ != 18 && mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x5e)
			taitoOpmMap_ = 14;
		if (taitoOpmMap_ == 18) {
			/* MAME sf sound_map: RAM C000-C7FF、YM2151 E000/E001。ROM は poke しない。 */
			if (addr == 0xe000 || addr == 0xe001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
				}
				return;
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				mem_[addr] = data;
			return;
		}
		if (taitoOpmMap_ == 17) {
			if (addr >= 0xe000)
				mem_[addr] = data;
			return;
		}
		if (taitoOpmMap_ == 16) {
			/* MAME arkanoid_map: RAM C000-C7FF mirror 0800、AY D000/D001、
			   D008 gfx/MCU reset、D010 watchdog、D018 MCU、VRAM E000-EFFF。 */
			if (addr >= 0xc000 && addr < 0xd000) {
				mem_[0xc000u + (addr & 0x07ffu)] = data;
				return;
			}
			if ((addr & ~0x0fe6u) == 0xd000u || addr == 0xd000 || addr == 0xd001) {
				if (chip_) {
					chip_->Write(addr & 1u, data);
					if (addr & 1u) opmWrites_++;
				}
				return;
			}
			if (addr == 0xd008 || addr == 0xd010 || addr == 0xd018)
				return;
			if (addr >= 0xe000) {
				mem_[addr] = data;
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 15) {
			/* MAME volfied z80_map: RAM 8000-87FF、PC060HA 8800/8801、YM2203 9000。 */
			if (addr >= 0x8000 && addr <= 0x87ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x8800) { SytSlavePortW(data); return; }
			if (addr == 0x8801) { SytSlaveCommW(data); return; }
			if (addr == 0x9000 || addr == 0x9001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) {
						opmWrites_++;
					} else {
						ymAddr_ = data;
					}
				}
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 12) {
			/* MAME taito_l fhawk_3_map: RAM 8000-9FFF、PC060HA E000、YM2203 F000。バンクは YM ポートA。 */
			if (addr >= 0x8000 && addr <= 0x9fff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf000 || addr == 0xf001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) {
						opmWrites_++;
						if (ymAddr_ == 0x0e)
							SetBank(data & 3);
					} else {
						ymAddr_ = data;
					}
				}
				return;
			}
			if (addr == 0xe000) { SytSlavePortW(data); return; }
			if (addr == 0xe001) { SytSlaveCommW(data); return; }
			return;
		}
		if (taitoOpmMap_ == 13) {
			/* MAME taito_l kurikint_2_map: RAM C000-DFFF、DPRAM E000-E7FF、YM2203 E800。 */
			if (addr >= 0xc000 && addr <= 0xe7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xe800 || addr == 0xe801) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 14) {
			/* MAME taito_l palamed_map: RAM 8000-9FFF、YM2203 A000-A003、PPI A800、制御 B000。
			   C000-FDFF は TC0090LVC VRAM（線形で足りる）。FE00 vregs、FF00 ベクタ、FF03 irq_enable、FF08 rom_bank。 */
			if (addr >= 0x8000 && addr <= 0x9fff) {
				mem_[addr] = data;
				return;
			}
			if (addr >= 0xa000 && addr <= 0xa003) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			if (addr >= 0xa800 && addr <= 0xa803) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xb000 || addr == 0xb001
				|| addr == 0xb800 || addr == 0xb801)
				return;
			if (addr >= 0xc000) {
				/* MAME common_banks_map: FF00-FF08 は mirror 0x00F0（cachat は FFF8、palamed は FF08）。 */
				if (addr >= 0xff00) {
					const unsigned n = addr & 0x0fu;
					mem_[addr] = data;
					mem_[0xff00u + n] = data;
					if (n == 8)
						SetBank(data);
					return;
				}
				mem_[addr] = data;
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 9) {
			/* MAME taito_x daisenpu_sound_map: RAM C000-DFFF、YM2151 E000、PC060HA E200、バンク F200。 */
			if (addr >= 0xc000 && addr <= 0xdfff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xe000 || addr == 0xe001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
					else
						ymAddr_ = data;
				}
				return;
			}
			if (addr == 0xe200) { SytSlavePortW(data); return; }
			if (addr == 0xe201) { SytSlaveCommW(data); return; }
			if (addr == 0xf200) { SetBank(data & 7); return; }
			return;
		}
		if (taitoOpmMap_ == 11) {
			/* MAME megasys1 z80_sound_map: RAM C000-C7FF。F000 は nopw。 */
			if (addr >= 0xc000 && addr <= 0xc7ff)
				mem_[addr] = data;
			return;
		}
		if (taitoOpmMap_ == 8) {
			/* MAME ashnojoe sound_map: RAM 6000-7FFF。バンク 8000 は ROM。 */
			if (addr >= 0x6000 && addr < 0x8000)
				mem_[addr] = data;
			return;
		}
		if (taitoOpmMap_ == 2) {
			/* MAME kikikai sound_map: RAM 8000-BFFF、YM2203 C000/C001 の配置 */
			if (addr >= 0x8000 && addr <= 0xbfff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xc000 || addr == 0xc001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr == 0xc001)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			/* 作業変数は C100+。YM は C000/C001 だけ占有 */
			if (addr >= 0xc002)
				mem_[addr] = data;
			return;
		}
		if (taitoOpmMap_ == 3) {
			/* MAME tokio_sound_map: RAM 8000-8FFF、ラッチ 9000、NMI 禁止 A000、NMI 許可 A800、YM2203 B000/B001 */
			if (addr >= 0x8000 && addr <= 0x8fff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xa000) {
				flstoryNmiEn_ = 0;
				return;
			}
			if (addr == 0xa800) {
				flstoryNmiEn_ = 1;
				if (soundCmdPending_)
					irqPulse_ = 1;
				return;
			}
			if (addr == 0xb000 || addr == 0xb001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 4 || taitoOpmMap_ == 6) {
			/* MAME bublbobl / lkage sound_map: RAM 8000、YM @9000、chip2 @A000、ラッチ B000、NMI 許可 B001、NMI 禁止 B002。lkage の chip2 は YM2203。 */
			if (addr >= 0x8000 && addr <= 0x8fff) {
				mem_[addr] = data;
				return;
			}
			if (addr >= 0x9000 && addr <= 0x9fff) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			if (addr >= 0xa000 && addr <= 0xafff) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
				}
				return;
			}
			if ((addr & ~0x0ffcu) == 0xb001) {
				flstoryNmiEn_ = 1;
				if (soundCmdPending_)
					irqPulse_ = 1;
				return;
			}
			if ((addr & ~0x0ffcu) == 0xb002) {
				flstoryNmiEn_ = 0;
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 5) {
			/* MAME lsasquad_sound_map: RAM 8000-87FF、YM2203 A000、AY C000、ラッチ D000、NMI 禁止 D400、NMI 許可 D800 */
			if (addr >= 0x8000 && addr <= 0x87ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xa000 || addr == 0xa001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			if (addr == 0xc000 || addr == 0xc001) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
				}
				return;
			}
			if (addr == 0xd400) {
				flstoryNmiEn_ = 0;
				return;
			}
			if (addr == 0xd800) {
				flstoryNmiEn_ = 1;
				if (soundCmdPending_)
					irqPulse_ = 1;
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 7) {
			/* MAME tnzs base_sub_map: ROM 0000-7FFF、A000 経由バンク 8000-9FFF、YM2203 B000-B001、MCU/IN C000、RAM D000、共有 E000 */
			if (addr == 0xa000) {
				if (soundRom_ && soundRomSize_ > 0x8000u) {
					const unsigned src = 0x8000u + (unsigned)(data & 3) * 0x2000u;
					unsigned n = 0x2000u;
					if (src < soundRomSize_) {
						if (src + n > soundRomSize_)
							n = soundRomSize_ - src;
						memset(mem_ + 0x8000, 0xff, 0x2000);
						memcpy(mem_ + 0x8000, soundRom_ + src, n);
					}
				}
				return;
			}
			if (addr == 0xb000 || addr == 0xb001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
					else
						ymAddr_ = data;
				}
				return;
			}
			if (addr >= 0xd000 && addr <= 0xefff) {
				mem_[addr] = data;
				return;
			}
			return;
		}
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
						/* MAME taito_b masterw/viofight: YM2203 ポート A（SSG レジスタ 0x0E）が audiobank bits 1:0 を選ぶ */
						if (ymAddr_ == 0x0e)
							SetBank(data & 3);
					} else {
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
						/* CT1/CT2（reg 0x1B）がここの ROM バンクを駆動。ymfm write_data: m_output_io = data >> 6。MAME はその ACCESS_IO バイトを sound_bankswitch_w へ直結するのでバンクは bits 7-6 からであり 1-0 ではない。 */
						if (ymAddr_ == 0x1b) SetBank((data >> 6) & 3);
					}
				} else {
					ymAddr_ = data;
				}
			}
			return;
		}
		if (taitoOpmMap_ == 1) {
			/* darius: 第 2 YM2203 @A000、PC060HA @B000、バンク @DC00 */
			if (addr == 0xa000 || addr == 0xa001) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr == 0xa001)
						opmWrites_++;
				}
				return;
			}
			if (addr == 0xb000) { SytSlavePortW(data); return; }
			if (addr == 0xb001) { SytSlaveCommW(data); return; }
			if (addr == 0xdc00) { SetBank(data & 3); return; }
			return;
		}
		if (addr == 0xa000) { SytSlavePortW(data); return; }
		if (addr == 0xa001) { SytSlaveCommW(data); return; }
		/* viofight: OKI6295 @ B000（両アドレス） */
		if (pcm_ && (addr == 0xb000 || addr == 0xb001)) {
			pcm_->Write(0, data);
			return;
		}
		return;
	}
	/* MAME vsystem/aerofgt sound_map: 0000-77FF ROM、7800-7FFF RAM、8000-FFFF バンク。ブートコードは 7800 から 0x8FF バイトをクリアし RAM を越えてバンク窓へ入る — ハードではそれらの書は ROM に当たり捨てられるので、ここでも窓を書保護。fromanc2: 0000-DFFF ROM、E000-FFFF RAM（7800 窓無し）。 */
	if (board_ == CEMU_AC_BOARD_VSYSTEM) {
		if (vsIoKind_ == 2) {
			if (addr >= 0xe000)
				mem_[addr] = data;
		} else if (vsIoKind_ == 3) {
			/* gunbird: RAM は 8000-81FF のみ。8200-FFFF はバンク ROM */
			if (addr >= 0x8000 && addr <= 0x81ff)
				mem_[addr] = data;
		} else if (addr >= 0x7800 && addr <= 0x7fff) {
			mem_[addr] = data;
		}
		return;
	}
	/* MAME irem_m72: sound_ram_map（M72 基板）は 0000-FFFF RAM。sound_rom_map（M81/M82/M84）は 0000-EFFF ROM + F000-FFFF RAM。 */
	if (board_ == CEMU_AC_BOARD_IREM_M72) {
		if (m72SoundRam_ || addr >= 0xf000)
			mem_[addr] = data;
		return;
	}
	/* MAME segas16b sound_map: 0000-7FFF ROM、8000-DFFF バンク、F800-FFFF RAM */
	if (board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B) {
		if (addr >= 0xf800)
			mem_[addr] = data;
		return;
	}
	/* MAME sega_system1 sound_map: 0000-7FFF ROM、8000-87FF RAM（ミラー 1800）、A000 SN1（ミラー 1FFF）、C000 SN2（ミラー 1FFF）、E000 ラッチ（ミラー 1FFF）。 */
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (vsIoKind_ == 4) {
			/* System E: RAM C000-FFFF。0000-BFFF は ROM／バンク。 */
			if (addr >= 0xc000)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 1) {
			/* MAME trackfld sound_map: RAM 4000-43FF、SN ラッチ A000、ストローブ C000。 */
			if (addr >= 0x4000 && addr <= 0x5fff)
				mem_[0x4000 + (addr & 0x03ff)] = data;
			else if (addr >= 0xa000 && addr <= 0xbfff)
				ms1LatchIn_ = data;
			else if (addr >= 0xc000 && addr <= 0xdfff && chip_) {
				chip_->Write(0, (uint8_t)ms1LatchIn_);
				opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ == 2) {
			/* MAME hyperspt/sbasketb: RAM 4000、SN ラッチ E001、ストローブ E002。 */
			if (addr >= 0x4000 && addr <= 0x4fff)
				mem_[addr] = data;
			else if (addr == 0xe001)
				ms1LatchIn_ = data;
			else if (addr == 0xe002 && chip_) {
				chip_->Write(0, (uint8_t)ms1LatchIn_);
				opmWrites_++;
			}
			return;
		}
		if (vsIoKind_ == 3) {
			/* MAME mikie sound_map: RAM 4000-43FF、SN1 @8002、SN2 @8004。 */
			if (addr >= 0x4000 && addr <= 0x43ff)
				mem_[addr] = data;
			else if (addr == 0x8002 && chip_) {
				chip_->Write(0, data);
				opmWrites_++;
			} else if (addr == 0x8004 && chip2_) {
				chip2_->Write(0, data);
				opmWrites_++;
			}
			return;
		}
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
	/* MAME taito_taitosj オーディオマップ: 0000-3FFF ROM、4000-43FF RAM、4800/4802/4804 AY アドレス+データ、5000/5001 soundlatch セマフォ。 */
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (vsIoKind_ == 24) {
			/* MAME masao_sound_map: RAM 2000-23FF、AY data 4000、address 6000。 */
			if (addr >= 0x2000 && addr <= 0x23ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x6000) {
				ayAddr_[0] = (uint8_t)(data & 0x0f);
				if (chip_) chip_->Write(0, data);
				return;
			}
			if (addr == 0x4000) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_++;
				}
				return;
			}
			return;
		}
		if (vsIoKind_ == 22) {
			/* MAME gberet prg_map: RAM C000-DFFF、K005849 E000-E047、SN ラッチ F200 / ストローブ F400。 */
			if (addr == 0xf200)
				soundCmd_ = data;
			else if (addr == 0xf400 && chip_) {
				chip_->Write(0, soundCmd_);
				opmWrites_++;
			} else if (addr >= 0xc000)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 23) {
			/* MAME higemaru: C801-C802 AY1 address_data、C803-C804 AY2。VRAM D000 / RAM E000。 */
			if (addr == 0xc801 || addr == 0xc802) {
				if (chip_) {
					chip_->Write((uint8_t)(addr - 0xc801u), data);
					if (addr == 0xc802)
						opmWrites_++;
				}
				return;
			}
			if (addr == 0xc803 || addr == 0xc804) {
				if (chip2_) {
					chip2_->Write((uint8_t)(addr - 0xc803u), data);
					if (addr == 0xc804)
						opmWrites_++;
				}
				return;
			}
			if (addr >= 0xc000)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 21) {
			if (addr >= 0xe000)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 4) {
			if (addr >= 0xc000 && addr <= 0xc7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x8000) {
				soundCmdPending_ = 0;
				irqPulse_ = 0;
				return;
			}
			return;
		}
		if (vsIoKind_ == 5) {
			/* MAME magmax: A15 オープン、ack @4000 ミラー 1FFF、RAM 6000-67FF */
			addr = (uint16_t)(addr & 0x7fffu);
			if (addr >= 0x6000) {
				mem_[0x6000u + (addr & 0x07ffu)] = data;
				return;
			}
			if (addr >= 0x4000) {
				soundCmdPending_ = 0;
				irqPulse_ = 0;
				return;
			}
			return;
		}
		if (vsIoKind_ == 6) {
			/* MAME bombjack: A15 オープン、RAM 4000-47FF、ラッチ読 @6000 */
			addr = (uint16_t)(addr & 0x7fffu);
			if (addr >= 0x4000 && addr < 0x6000)
				mem_[0x4000u + (addr & 0x07ffu)] = data;
			return;
		}
		if (vsIoKind_ == 7) {
			/* MAME calorie: ROM 0000-3FFF ミラー 4000、RAM 8000-87FF、ラッチ C000 */
			if (addr >= 0x8000 && addr < 0xc000)
				mem_[0x8000u + (addr & 0x07ffu)] = data;
			return;
		}
		if (vsIoKind_ == 8 || vsIoKind_ == 10) {
			/* MAME solomon / pbaction: RAM 4000-47FF、ラッチ @8000、ack FFFF */
			if (addr >= 0x4000 && addr < 0x4800)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 11) {
			/* MAME chaknpop: RAM 8000-87FF、AY 8804-8807、MCU 8800 の配置 */
			if (addr >= 0x8000 && addr < 0x8800)
				mem_[addr] = data;
			else if (addr == 0x8804 && chip_)
				chip_->Write(0, data);
			else if (addr == 0x8805 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
			} else if (addr == 0x8806 && chip2_)
				chip2_->Write(0, data);
			else if (addr == 0x8807 && chip2_) {
				chip2_->Write(1, data);
				opmWrites_++;
			} else if (addr >= 0x9000 && addr < 0xa000)
				mem_[addr] = data;
			else if (addr >= 0xc000)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 13) {
			/* MAME swimmer_audio_map: RAM 2000-23FF（ミラー 0C00）、NMI ack 4000。AY は I/O。 */
			if (addr >= 0x2000 && addr < 0x3000) {
				mem_[0x2000u + (addr & 0x03ffu)] = data;
				return;
			}
			return;
		}
		if (vsIoKind_ == 14) {
			if (addr >= 0xe000 && addr < 0xe800)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 15) {
			if (addr >= 0x2000 && addr < 0x2800)
				mem_[addr] = data;
			else if (addr == 0x8000) {
				if (chip_) { chip_->Write(0, data); opmWrites_++; }
			} else if (addr == 0xa000) {
				if (chip2_) { chip2_->Write(0, data); opmWrites_++; }
			}
			return;
		}
		if (vsIoKind_ == 16) {
			if (addr >= 0xc000 && addr < 0xd000)
				mem_[addr] = data;
			else if (addr == 0xd801) {
				if (chip_) { chip_->Write(0, data); opmWrites_++; }
			} else if (addr == 0xd802) {
				if (chip2_) { chip2_->Write(0, data); opmWrites_++; }
			}
			return;
		}
		if (vsIoKind_ == 17) {
			/* MAME circusc sound_map: RAM 4000-43FF mirror 1C00。A000-A07F sound_w mirror 1F80。 */
			if (addr >= 0x4000 && addr < 0x6000) {
				mem_[0x4000u + (addr & 0x03ffu)] = data;
				return;
			}
			if ((addr & 0xe000u) == 0xa000u) {
				const unsigned off = (unsigned)(addr & 7u);
				if (off == 0)
					ymAddr_ = data;
				else if (off == 1) {
					if (chip_) { chip_->Write(0, (uint8_t)ymAddr_); opmWrites_++; }
				} else if (off == 2) {
					if (chip2_) { chip2_->Write(0, (uint8_t)ymAddr_); opmWrites_++; }
				}
			}
			return;
		}
		if (vsIoKind_ == 18) {
			/* MAME senjyo_sound_map: RAM 4000-43FF、SN 8000/9000/A000。 */
			if (addr >= 0x4000 && addr < 0x4400)
				mem_[addr] = data;
			else if (addr == 0x8000) {
				if (chip_) { chip_->Write(0, data); opmWrites_++; }
			} else if (addr == 0x9000) {
				if (chip2_) { chip2_->Write(0, data); opmWrites_++; }
			} else if (addr == 0xa000) {
				if (chip3_) { chip3_->Write(0, data); opmWrites_++; }
			}
			return;
		}
		if (vsIoKind_ == 19) {
			/* MAME tehkanwc sound_mem: RAM 4000-47FF。MSM 8001 / nop 8002/8003。 */
			if (addr >= 0x4000 && addr < 0x4800)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 20) {
			/* MAME fcombat: RAM 4000-47FF。AY data_address 8002/8003, A002/A003, C002/C003。 */
			if (addr >= 0x4000 && addr < 0x4800) {
				mem_[addr] = data;
				return;
			}
			CChip* ay = NULL;
			if (addr == 0x8002 || addr == 0x8003) ay = chip_;
			else if (addr == 0xa002 || addr == 0xa003) ay = chip2_;
			else if (addr == 0xc002 || addr == 0xc003) ay = chip3_;
			if (ay) {
				if (addr & 1) ay->Write(0, data);
				else { ay->Write(1, data); opmWrites_++; }
			}
			return;
		}
		if (vsIoKind_ == 12) {
			/* MAME 1942 sound_map: RAM 4000-47FF、AY1 address_data 8000-8001、AY2 C000-C001。
			   ファームは 8000/C000 にレジスタ番号、8001/C001 にデータを書く（CEmu AY Write 0=addr 1=data）。 */
			if (addr >= 0x4000 && addr < 0x4800) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x8000 || addr == 0x8001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
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
			return;
		}
		if (addr >= 0x4000 && addr < (vsIoKind_ == 9 ? 0x4800u : 0x4400u)) {
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
					/* AY#4（ここの枠 2）ポート B bit0 = 音源 NMI マスク、Low = オン */
					if (slot == 2 && ayAddr_[2] == 0x0f && vsIoKind_ != 9) {
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
	/* MAME scramble_sound_map: RAM 8000-8FFF（1K ミラー）。frogger は 4000。 */
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && vsIoKind_ != 1 && vsIoKind_ != 2) {
		if (addr >= 0x8000 && addr < 0x9000)
			mem_[0x8000u + (addr & 0x03ffu)] = data;
		return;
	}
	/* MAME frogger_sound_map / hustler_sound_map: RAM 4000-43FF mirror 1C00。フィルタ 6000 は無視。 */
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && (vsIoKind_ == 1 || vsIoKind_ == 2)) {
		if (addr >= 0x4000 && addr < 0x6000)
			mem_[0x4000u + (addr & 0x03ffu)] = data;
		return;
	}
	/* MAME timeplt_a: ROM 0000-2FFF、RAM 3000-33FF。jungler/locomotn は RAM 2000-23FF と SP=$2400、3000 は別ワーク。エイリアスするとスタックがチャンネル RAM を壊す。 */
	if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		if (vsIoKind_ == 1) {
			if (addr >= 0xe000 && addr <= 0xe7ff) {
				mem_[addr] = data;
				return;
			}
			return;
		}
		if (addr >= 0x2000 && addr <= 0x2fff) {
			mem_[0x2000 + (addr & 0x03ff)] = data;
			return;
		}
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
		return; /* フィルタ／未マップ */
	}
	/* MAME nemesis sound_map / gx400_sound_map（AY + ラッチ。K005289 は stub） */
	if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (addr >= 0x4000 && addr <= (vsIoKind_ == 1 ? 0x47ffu : 0x7fffu)) {
			mem_[addr] = data;
			return;
		}
		if (addr >= 0x8000 && addr <= 0x87ff) {
			mem_[addr] = data; /* バブルセット上の voiceram */
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
		/* e003/e004 K005289 tg、a000/c000 ld、e007 フィルタ、e000 音声 — 無視 */
		if (addr >= 0xa000)
			return;
		return;
	}
	/* MAME ddragon2_sound_map: ROM 0000-7FFF、RAM 8000-87FF、YM2151 8800-8801、OKI 9800、ラッチ A000。
	   ddragon3: ROM 0000-BFFF、RAM C000-C7FF、YM C800、OKI D800、ラッチ E000、バンク E800。
	   dbz: ROM 0000-7FFF、RAM 8000-BFFF、YM C000、OKI D000、ラッチ E000。 */
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		if (snkMapKind_ >= 4)
			return;
		if (snkMapKind_ != 1 && mem_[0] == 0xc3 && mem_[1] == 0x00 && mem_[2] == 0x01
			&& snkMapKind_ == 0)
			snkMapKind_ = 2;
		if (snkMapKind_ == 3) {
			if (addr >= 0x8000 && addr <= 0xbfff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xd000) {
				mem_[addr] = data; /* NMI enable bit0 */
				return;
			}
			if (addr == 0xe000) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xf000) {
				if (chip_) chip_->Write(0, data);
				return;
			}
			if (addr == 0xf001) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
				return;
			}
			return;
		}
		if (snkMapKind_ == 2) {
			if (addr >= 0x8000 && addr <= 0xbfff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xc000) {
				if (chip_) chip_->Write(0, data);
				return;
			}
			if (addr == 0xc001) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
				return;
			}
			if (addr >= 0xd000 && addr <= 0xd002) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			return;
		}
		if (snkMapKind_ == 1) {
			if (addr >= 0xc000 && addr <= 0xc7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xc800) {
				if (chip_) chip_->Write(0, data);
				return;
			}
			if (addr == 0xc801) {
				if (chip_) {
					chip_->Write(1, data);
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
				return;
			}
			if (addr == 0xd800) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xe800) {
				unsigned pages = (pcmRomSize_ >= 0x10000u) ? (pcmRomSize_ / 0x10000u) : 1u;
				unsigned base = (unsigned)(data & 1) * 4u;
				if (pages < 8u) base = 0;
				for (int i = 0; i < 8; i++) {
					unsigned p = base + (unsigned)(i & 3);
					raizingOkiBank_[0][i] = (p < pages) ? p : 0;
				}
				if (pcm_)
					CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
				return;
			}
			return;
		}
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
	/* MAME thunderx/scontra（map0）/ crimfght（map1）: Z80 + YM2151 + K007232 stub の配置 */
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		if (konamiK7232Map_ == 7) {
			/* MAME hexion_map: RAM A000-BFFF、VRAM/PMC C000-DFFF、SCC E800、OKI F200、
			   K053252 F000、バンク F480、ウォッチドッグ F540。 */
			if (addr >= 0xa000 && addr <= 0xbfff) {
				mem_[addr] = data;
				return;
			}
			if (addr >= 0xc000 && addr <= 0xdfff) {
				mem_[addr] = data;
				return;
			}
			if (addr >= 0xe800 && addr <= 0xe8ff) {
				if (chip_)
					chip_->Write((uint32_t)(addr - 0xe800u), data);
				return;
			}
			if (addr == 0xf200) {
				if (pcm_)
					pcm_->Write(0, data);
				return;
			}
			if (addr == 0xf480) {
				const unsigned bank = (unsigned)(data & 0x0fu);
				bank_ = (int)bank;
				if (soundRom_ && soundRomSize_ >= 0x2000u) {
					const unsigned src = (bank * 0x2000u) % (soundRomSize_ & ~0x1fffu);
					unsigned n = 0x2000u;
					if (src + n > soundRomSize_)
						n = soundRomSize_ - src;
					if (n)
						memcpy(mem_ + 0x8000, soundRom_ + src, n);
					if (n < 0x2000u)
						memset(mem_ + 0x8000 + n, 0xff, 0x2000u - n);
				}
				return;
			}
			if (addr >= 0xf000)
				return;
			return;
		}
		if (konamiK7232Map_ == 3) {
			/* MAME combatsc: RAM 8000-87FF、YM2203 E000、UPD 9000-C000 stub */
			if (addr >= 0x8000 && addr <= 0x87ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xe000 || addr == 0xe001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					opmWrites_++;
				}
				return;
			}
			return;
		}
		if (konamiK7232Map_ == 2) {
			/* MAME gradius3: RAM F800-FFFF、YM2151 F030、K007232 F020 stub の配置 */
			if (addr >= 0xf800) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf030 || addr == 0xf031) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
				return;
			}
			if (addr >= 0xf000)
				return;
			return;
		}
		/* twin16 は 8000-87FF。古いダンプは 8800-8FFF も触る — 8K を許可 */
		if (addr >= 0x8000 && addr <= 0x8fff) {
			mem_[addr] = data;
			return;
		}
		const unsigned ym = konamiK7232Map_ == 1 ? 0xa000u : 0xc000u;
		if (addr == ym || addr == ym + 1u) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		if (konamiK7232Map_ == 6 && addr >= 0x9000 && addr <= 0x9007) {
			CEmuAcK007452Write(namcoCus30_, addr - 0x9000u, data);
			return;
		}
		/* K007232 / uPD7759 / バンクスイッチ — 書は受ける。PCM 合成はまだ無い */
		if (addr >= 0x9000)
			return;
		return;
	}
	/* MAME alpha68k_II sound_map: ROM 0000-7FFF、RAM 8000-87FF、バンク C000-FFFF */
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		if (vsIoKind_ == 1) {
			if (addr >= 0x6000 && addr <= 0x66ff)
				mem_[addr] = data;
			return;
		}
		if (addr >= 0x8000 && addr <= 0x87ff)
			mem_[addr] = data;
		return;
	}
	/* MAME hcastle sound_map: YM3812 @A000、K007232 @B000、ラッチ @D000。
	   spy: ROM 0000-7FFF、RAM 8000-87FF、YM3812 @C000、ラッチ @D000。 */
	if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		if (addr >= 0x8000 && addr <= 0x87ff) {
			mem_[addr] = data;
			return;
		}
		{
			const unsigned ym = HcastleSpyMap() ? 0xc000u : 0xa000u;
			if (addr == ym || addr == (ym + 1u)) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
				return;
			}
		}
		if (addr >= 0x9800)
			return;
		return;
	}
	if (board_ == CEMU_AC_BOARD_RAIZING) {
		RaizingMemWrite(addr, data);
		return;
	}
	/* MAME tecmo16 sound_map: ROM 0000-EFFF、RAM F000-FBFF、OKI FC00、YM2151 FC04/05、ラッチ FC08。古典マップ: rygar RAM 4000 YM 8000 ラッチ C000。gemini RAM 8000 YM A000 ラッチ C000。tbowl RAM C000 YM D000/D800 ラッチ E010。 */
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		if (tecmoOpl_ == 11) {
			if (addr >= 0xf000 && addr <= 0xf7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf808 || addr == 0xf809) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
				}
				return;
			}
			if (addr == 0xf80a) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			return;
		}
		if (tecmoOpl_ == 12) {
			if (addr >= 0xc000 && addr <= 0xdfff)
				mem_[addr] = data;
			return;
		}
		if (tecmoOpl_ == 13) {
			if (addr >= 0x8000 && addr <= 0x87ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xa000 || addr == 0xa001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) {
						opmWrites_ = CEmuChipYm2151WriteCount(chip_);
						if (ymAddr_ == 0x1b) {
							CEmuAcDeco32OkiBank(raizingOkiBank_[0], pcmRomSize_, data >> 0);
							CEmuAcDeco32OkiBank(raizingOkiBank_[1], pcmRom2Size_ ? pcmRom2Size_ : pcmRomSize_, data >> 1);
						}
					} else
						ymAddr_ = data;
				}
				return;
			}
			if (addr == 0xb000) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xc000) {
				if (pcm2_) pcm2_->Write(0, data);
				return;
			}
			return;
		}
		if (tecmoOpl_ == 5) {
			/* MAME: RAM C000-DFFF ミラー 2000（8K） */
			if (addr >= 0xc000)
				mem_[0xc000u + (addr & 0x1fffu)] = data;
			return;
		}
		if (tecmoOpl_ == 7 || tecmoOpl_ == 9 || tecmoOpl_ == 10) {
			if (addr >= 0xe000)
				mem_[addr] = data;
			return;
		}
		if (tecmoOpl_ == 8) {
			if ((addr >= 0xc000 && addr < 0xc800) || addr >= 0xf800)
				mem_[addr] = data;
			return;
		}
		if (tecmoOpl_ == 6) {
			/* MAME wc90 sound_map: RAM F000-F7FF、YM2608 F800-F803、FC00 IRQ ACK、FC10 ラッチ（読） */
			if (addr >= 0xf000 && addr <= 0xf7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr >= 0xf800 && addr <= 0xf803) {
				if (chip_) {
					const unsigned a = addr & 3u;
					chip_->Write(a >= 2u ? (0x100u + (a & 1u)) : a, data);
					if (a & 1u) opmWrites_++;
				}
				return;
			}
			if (addr == 0xfc00) {
				if (chip_ && chip_->Irq())
					chip_->AckIrq();
				return;
			}
			return;
		}
		if (tecmoOpl_ == 1) {
			if (addr >= 0x4000 && addr <= 0x47ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x8000 || addr == 0x8001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
				return;
			}
			if (addr >= 0xc000)
				return;
			return;
		}
		if (tecmoOpl_ == 2) {
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
			if (addr >= 0xc000)
				return;
			return;
		}
		if (tecmoOpl_ == 4) {
			if (addr >= 0xc000 && addr <= 0xc7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xd000 || addr == 0xd001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
				return;
			}
			if (addr == 0xd800 || addr == 0xd801) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_++;
				}
				return;
			}
			if (addr >= 0xe000)
				return;
			return;
		}
		if (tecmoOpl_ == 3) {
			/* MAME spbactn sound_map: RAM F000-F7FF、OKI F800、YM3812 F810/F811、IRQ ack FC00、ラッチ FC20。 */
			if (addr >= 0xf000 && addr <= 0xf7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf800) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xf810 || addr == 0xf811) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1)
						opmWrites_ = CEmuChipYm3812WriteCount(chip_);
				}
				return;
			}
			if (addr == 0xfc00) {
				if (chip_ && chip_->Irq())
					chip_->AckIrq();
				return;
			}
			return;
		}
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
					opmWrites_ = tecmoOpl_ ? (opmWrites_ + 1)
						: CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		if (addr >= 0xfc00)
			return;
		return;
	}
	/* MAME flstory sound_map: RAM C000-C7FF、AY C800、MSM CA00-CA0D、音量 CC00/CE00、ラッチ D800、NMI 許可 DA00、クリア DC00、DAC DE00。E000-EFFF は任意診断 ROM（無い → オープンバス 0x00）。msisaac（taitoOpmMap_=1）: RAM 4000-47FF、AY 8000/8002、MSM 8010、ラッチ C000、NMI 許可 C001 / 禁止 C002。 */
	if (board_ == CEMU_AC_BOARD_FLSTORY) {
		if (taitoOpmMap_ == 1) {
			if (addr >= 0x4000 && addr <= 0x47ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0x8000 || addr == 0x8001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr == 0x8002 || addr == 0x8003) {
				if (chip3_) {
					chip3_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr >= 0x8010 && addr <= 0x801d) {
				if (chip2_) {
					chip2_->Write(addr - 0x8010u, data);
					opmWrites_++;
				}
				return;
			}
			if (addr == 0xc001) {
				flstoryNmiEn_ = 1;
				if (soundCmdPending_)
					irqPulse_ = 1;
				return;
			}
			if (addr == 0xc002) {
				flstoryNmiEn_ = 0;
				return;
			}
			return;
		}
		if (taitoOpmMap_ == 3) {
			/* MAME nycaptor sound_map: AY×2 @C800/@C802、MSM @C900、ラッチ D000、NMI 許可 D200 / 禁止 D400、DAC D600。 */
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
			if (addr == 0xc802 || addr == 0xc803) {
				if (chip3_) {
					chip3_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr >= 0xc900 && addr <= 0xc90d) {
				if (chip2_) {
					chip2_->Write(addr - 0xc900u, data);
					opmWrites_++;
				}
				return;
			}
			if (addr == 0xd200) {
				flstoryNmiEn_ = 1;
				if (soundCmdPending_)
					irqPulse_ = 1;
				return;
			}
			if (addr == 0xd400) {
				flstoryNmiEn_ = 0;
				return;
			}
			return;
		}
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
			/* soundnmi in_set<1> — ラッチ pending のとき NMI を許可 */
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
			/* 8bit DAC — 受ける。合成はまだ無い */
			return;
		}
		if (addr >= 0xcc00)
			return;
		return;
	}
	/* MAME battlnts sound_map: RAM 8000-87FF、YM3812 @A000 + @C000、ラッチ E000 */
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
	/* MAME robokid/ninjakd2: RAM C000-C7FF。YM は I/O マップ */
	if (board_ == CEMU_AC_BOARD_ROBOKID) {
		if (vsIoKind_ == 15) {
			if (addr >= 0xc000)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 13) {
			if (addr == 0xe800 || addr == 0xe801) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr == 0xf000 || addr == 0xf001) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr >= 0xe000 && addr <= 0xe7ff)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 14) {
			if (addr == 0xf000 || addr == 0xf001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr == 0xf002 || addr == 0xf003) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr == 0xf004) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xf00a) {
				bank_ = (int)(data & 1);
				if (soundRom_ && soundRomSize_ > 0x8000u) {
					unsigned src = 0x8000u + (unsigned)bank_ * 0x4000u;
					unsigned n = 0x5000u;
					if (src >= soundRomSize_)
						return;
					if (src + n > soundRomSize_)
						n = soundRomSize_ - src;
					memcpy(mem_ + 0x8000, soundRom_ + src, n);
				}
				return;
			}
			if (addr >= 0xd000 && addr <= 0xd7ff)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 8) {
			if (addr == 0xc000 || addr == 0xc001) {
				if (chip_) {
					chip_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr == 0xc800 || addr == 0xc801) {
				if (chip2_) {
					chip2_->Write(addr & 1, data);
					if (addr & 1) opmWrites_++;
				}
				return;
			}
			if (addr >= 0xf800)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 9) {
			if (addr >= 0xe000 && addr <= 0xe7ff)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 10 || vsIoKind_ == 12) {
			if (addr >= 0xf800)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 11) {
			if (addr >= 0x8000 && addr <= 0x87ff)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 6 || vsIoKind_ == 7) {
			if (addr >= 0xc000 && addr <= 0xc7ff)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 3 || vsIoKind_ == 4 || vsIoKind_ == 5) {
			if (addr >= 0xc000 && addr <= 0xdfff)
				mem_[addr] = data;
			return;
		}
		if (vsIoKind_ == 2) {
			if (addr >= 0xc000 && addr <= 0xc7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf400) {
				if (pcm_) pcm_->Write(0, data);
				return;
			}
			if (addr == 0xf500) {
				if (pcm2_) pcm2_->Write(0, data);
				return;
			}
			if (addr == 0xf600) {
				CEmuAcTharrierOkiBank(raizingOkiBank_[0], pcmRomSize_, data);
				return;
			}
			if (addr == 0xf700) {
				CEmuAcTharrierOkiBank(raizingOkiBank_[1], pcmRom2Size_, data);
				return;
			}
			return;
		}
		if (vsIoKind_ == 1) {
			if (addr >= 0xc000 && addr <= 0xdfff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xe001) {
				bankLoaded_ = 0;
				SetBank((int)(data & 7));
				return;
			}
			return;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff) {
			mem_[addr] = data;
			return;
		}
		return;
	}
	/* terracre: RAM C000-CFFF。armedf/terraf: RAM F800-FFFF。cclimbr2/legion: RAM C000-FFFF。YM/ラッチは I/O。 */
	if (board_ == CEMU_AC_BOARD_TERRACRE) {
		if (terracreMap_ == 3 || terracreMap_ == 5) {
			if (addr >= 0x8000 && addr < 0x8800)
				mem_[addr] = data;
			return;
		}
		if (terracreMap_ == 4) {
			if (addr >= 0xc000 && addr < 0xc800)
				mem_[addr] = data;
			return;
		}
		if (terracreMap_ == 2) {
			if (addr >= 0xc000)
				mem_[addr] = data;
		} else if (terracreMap_ == 1) {
			if (addr >= 0xf800)
				mem_[addr] = data;
		} else if (addr >= 0xc000 && addr <= 0xcfff) {
			mem_[addr] = data;
		}
		return;
	}
	/* CPS1 音源マップ: F000/F001=YM2151、F002/F003=OKI、F004=バンク、F006=?、F008=ラッチ */
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
		if (addr == 0xf004) {
			/* MAME cps1_snd_bankswitch_w: data&1 で CONTINUE 16K を 8000 へ。OKI バンクではない。 */
			SetBank(data & 1);
			return;
		}
		/* その他 I/O — データプレーンを無視し、F0xx の ROM イメージが MemRead で読まれないように */
		if (addr >= 0xf002 && addr <= 0xf00f)
			return;
	}
	/* CPS2 QSound マップ（MAME qsound_sub_map）: ROM 0000-7FFF、バンク 8000-BFFF、共有 RAM C000-CFFF、QSound D000-D002、バンク切 D003、ステータス D007、作業 RAM F000-FFFF。旧コードは F000 を QSound と扱い Z80 RAM を壊した。 */
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
	if ((board_ == CEMU_AC_BOARD_OUTRUN || board_ == CEMU_AC_BOARD_ABURNER
			|| (board_ == CEMU_AC_BOARD_HANGON && vsIoKind_ == 1))
		&& pcm_
		&& ((addr >= 0xf000 && addr <= 0xf0ff) || (addr >= 0x1000 && addr <= 0x1fff))) {
		pcm_->Write(addr & 0xff, data);
		return;
	}
	if ((board_ == CEMU_AC_BOARD_SYS18 || board_ == CEMU_AC_BOARD_SYS24) && pcm_) {
		if (addr >= 0xc000 && addr <= 0xcfff) {
			pcm_->Write(addr & 0xff, data);
			return;
		}
		if (addr >= 0xd000 && addr <= 0xdfff) {
			pcm_->Write(addr & 0xff, data);
			return;
		}
	}
	if (board_ == CEMU_AC_BOARD_SYS32 && pcm_ && addr >= 0xc000 && addr <= 0xdfff) {
		if (Sys32MultiPcm()) {
			pcm_->Write(addr & 3, data);
		} else if (addr & 0x1000) {
			CEmuChipRf5c68MemW(pcm_, addr & 0xfffu, data);
		} else {
			pcm_->Write(addr & 0x0f, data);
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_HANGON) {
		if (vsIoKind_ == 1) {
			if (addr >= 0xf800)
				mem_[addr] = data;
			return;
		}
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
		/* K054321 sound_map: Bucky/Moo @F000、gijoe/lethalen @FC00。
		   [0]=音源→メイン、[2]/[3]=メイン→音源ラッチ */
		{
			const unsigned k321 = KonamiJoeMap() ? 0xfc00u : 0xf000u;
			if (pcmKind_ == 4 && addr >= k321 && addr <= k321 + 3u) {
				if (addr == k321) {
					/* 音源→メインラッチ。CEmu では誰も聞かない */
					return;
				}
				return;
			}
		}
		/* K053260: FA00 が SH1→NMI を武装（MAME z80_arm_nmi_w）。音源 CPU は LD (FA00),A / HALT。NMI（RETN）が HALT の先を再開。
		   rollerg の FA00 は未マップ。NMI arm は FC00。 */
		if (pcmKind_ == 3 && addr == 0xfa00 && pcmBase != 0xfa00u && !KonamiRollergMap()) {
			konamiSh1NmiArm_ = 1;
			return;
		}
		if (KonamiRollergMap() && addr == 0xfc00) {
			konamiSh1NmiArm_ = 1;
			return;
		}
		if (pcm2_ && konamiPcm2Addr_
			&& addr >= konamiPcm2Addr_ && addr < konamiPcm2Addr_ + pcmWin) {
			pcm2_->Write(addr - konamiPcm2Addr_, data);
			return;
		}
		if (KonamiPrmrsocrMap() && pcm && addr >= 0xe000u && addr <= 0xe22fu) {
			const unsigned off = addr - 0xe000u;
			const unsigned reg = ((off & 0x100u) << 1) | (off & 0xffu);
			uint8_t v = data;
			if (reg == 0x22fu)
				v = (uint8_t)((data & 0x10u) | 0x21u);
			pcm->Write(reg, v);
			return;
		}
		if (pcm && addr >= pcmBase && addr < pcmBase + pcmWin) {
			uint32_t off = addr - pcmBase;
			uint8_t v = data;
			/* gijoe は FA2F に 0x80/0x90（bit7=レジスタ凍結）を書く。MAME はその間 key-on を捨て、
			   bit5 が無いとタイマ NMI も出ない。lethalen ファームは 0x21。bit4 の 22d リードバックは残す。 */
			if (KonamiJoeMap() && off == 0x22f)
				v = (uint8_t)((data & 0x10u) | 0x21u);
			pcm->Write(off, v);
			return;
		}
		/* YM2151 @opm/@opm+1、加えて Konami F81x データポートミラー（thndrx2 は RST28 ビジー待ちのあと LD (F811),A）。
		   xmen: E800 と EC00 ミラー。glfgreat/prmrsocr は YM 無し。rollerg は YM3812 @C000。 */
		if (KonamiRollergMap() && chip_ && (addr == 0xc000u || addr == 0xc001u)) {
			chip_->Write(addr & 1, data);
			if (addr & 1)
				opmWrites_ = CEmuChipYm3812WriteCount(chip_);
			return;
		}
		if (chip_ && pcm_ && !KonamiJoeMap() && opm != 0xffffu
			&& (addr == opm || addr == (opm + 1u)
				|| (KonamiXmenMap() && (addr == 0xec00u || addr == 0xec01u))
				|| (pcmKind_ == 3 && (addr == (opm + 0x10u) || addr == (opm + 0x11u))))) {
			chip_->Write(addr & 1, data);
			if ((addr & 1) == 1)
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		/* カタログが実際に YM を F000 に置いたときだけミラー（lgtnfght は A000。F800 級基板は F000-F7FF を作業 RAM に保つ）。 */
		if (pcmKind_ == 3 && opm == 0xf000u && chip_ && pcm_
			&& (addr == 0xf000 || addr == 0xf001)) {
			chip_->Write(addr & 1, data);
			if ((addr & 1) == 1)
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		if (konamiBankAddr_ && addr == konamiBankAddr_) {
			/* mystwarr sound_ctrl_w: bits 0-2 バンク、bit 4 が K054539 タイマ → NMI をゲート（クリアすると保留 NMI も消す）。 */
			konamiSoundCtrl_ = data;
			SetBank((int)data);
			return;
		}
		/* 作業 RAM（典型は F000-F7FF。lgtnfght は 8000+）。チップ窓は上で既に return — 残りの高位空間は RAM/オープン。 */
		if (addr >= 0x8000)
			mem_[addr] = data;
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_C352 && chip_) {
		chip_->Write(addr >> 1, data);
		return;
	}
	/* MAME namcos1 sound_map（M6809、コアはまだ無い）: YM2151 @4000、CUS30 amap @5000-53FF（wave 5000-50FF、regs 5100-513F）。 */
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
	/* MAME namcos86 common_mcu_map: CUS30 @1000-13FF、YM2151 @2000-2001 の配置 */
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
	/* WSG: Pac-Man/Galaga 3 声レジスタ @6800-681F。共有 RAM 8000-9FFF。$6822 音許可パルス。Pengo は同じニブルマップを $9000 へ移す。 */
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && chip_) {
		if (sys16RomBoard_ == 0x5047u && addr >= 0x9000 && addr <= 0x901f) {
			chip_->Write(addr & 0x1f, data & 0x0f);
			opmWrites_++;
			return;
		}
		if (sys16RomBoard_ == 0x5047u && addr >= 0x9040 && addr <= 0x9047) {
			mem_[addr] = (uint8_t)(data & 1u);
			if (addr == 0x9041)
				CEmuChipC30SetEnable(chip_, data & 1);
			return;
		}
		if (addr >= 0x6800 && addr <= 0x681f) {
			chip_->Write(addr & 0x1f, data & 0x0f);
			opmWrites_++;
			return;
		}
		if (addr == 0x6822) {
			/* その他ラッチパルス — WSG を許可したまま */
			mem_[addr] = data;
			return;
		}
		if (addr >= 0x8000 && addr <= 0x9fff) {
			mem_[addr] = data;
			return;
		}
		/* xevious: 作業 RAM + SP は $A000-$A7FF。ここを落とすと 028B の曲状態が全部消える。 */
		if (addr >= 0xa000 && addr <= 0xa7ff) {
			mem_[addr] = data;
			return;
		}
		/* 他 MMIO は無視。ROM は無傷に保つ */
		if (addr >= 0x4000)
			return;
		return;
	}
	/* GNG: ROM 0000-7FFF、RAM C000-C7FF、ラッチ C800、YM2203 E000-E003。Commando マップ: RAM 4000-47FF、ラッチ 6000、YM2203 8000-8003。Tecmo gaiden: ROM 0000-DFFF、RAM F000-F7FF、OKI F800、YM F810/F820、ラッチ FC20 → NMI（シーケンサは YM2203 IRQ）。 */
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
		if (gngCommandoMap_ == 4) {
			if (addr >= 0x4000 && addr <= 0x43ff) {
				mem_[addr] = data;
				return;
			}
			return;
		}
		if (gngCommandoMap_ == 5) {
			/* MAME momoko sound_map: RAM 8000-87FF、YM1 A000/A001、YM2 C000/C001。9000/B000 nop。 */
			if (addr >= 0x8000 && addr <= 0x87ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xa000 && chip_) {
				gngYmAddr_[0] = data;
				chip_->Write(0, data);
				return;
			}
			if (addr == 0xa001 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr == 0xc000 && chip2_) {
				gngYmAddr_[1] = data;
				chip2_->Write(0, data);
				return;
			}
			if (addr == 0xc001 && chip2_) {
				chip2_->Write(1, data);
				opmWrites_++;
				return;
			}
			return;
		}
		if (gngCommandoMap_ == 6) {
			/* MAME jumping_state::sound_map: RAM 8000-8FFF、YM1 B000/B001、YM2 B400/B401、BC00 nop。 */
			if (addr >= 0x8000 && addr <= 0x8fff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xb000 && chip_) {
				gngYmAddr_[0] = data;
				chip_->Write(0, data);
				return;
			}
			if (addr == 0xb001 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr == 0xb400 && chip2_) {
				gngYmAddr_[1] = data;
				chip2_->Write(0, data);
				return;
			}
			if (addr == 0xb401 && chip2_) {
				chip2_->Write(1, data);
				opmWrites_++;
				return;
			}
			return;
		}
		if (gngCommandoMap_ == 2) {
			/* MAME sidearms_sound_map: RAM C000-C7FF、YM1 F000-F001、YM2 F002-F003。 */
			if (addr >= 0xc000 && addr <= 0xc7ff) {
				mem_[addr] = data;
				return;
			}
			if (addr == 0xf000 && chip_) {
				gngYmAddr_[0] = data;
				chip_->Write(0, data);
				return;
			}
			if (addr == 0xf001 && chip_) {
				chip_->Write(1, data);
				opmWrites_++;
				return;
			}
			if (addr == 0xf002 && chip2_) {
				gngYmAddr_[1] = data;
				chip2_->Write(0, data);
				return;
			}
			if (addr == 0xf003 && chip2_) {
				chip2_->Write(1, data);
				opmWrites_++;
				return;
			}
			return;
		}
		if (gngCommandoMap_ == 3) {
			/* MAME tigeroad sound_map: YM1 8000/8001、YM2 A000/A001、RAM C000-C7FF。 */
			if (addr >= 0xc000 && addr <= 0xc7ff) {
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
			if (addr == 0xa000 && chip2_) {
				gngYmAddr_[1] = data;
				chip2_->Write(0, data);
				return;
			}
			if (addr == 0xa001 && chip2_) {
				chip2_->Write(1, data);
				opmWrites_++;
				return;
			}
			return;
		}
		if (gngCommandoMap_ == 1) {
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
		/* 他 MMIO は無視。ROM は poke しない */
		if (addr >= 0x8000)
			return;
		return;
	}
	mem_[addr] = data;
}

/* メモリ 8bit 読込 */
uint8_t CHardAc::MemRead(uint16_t addr)
{
	if (board_ == CEMU_AC_BOARD_BIONICC) {
		if (addr == 0x8000 || addr == 0x8001) {
			/* 014B は (8001) bit7 busy 待ち。0038 は RRA で Timer A bit0。busy は sticky なので落とす。 */
			return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
		}
		if (addr == 0xa000)
			return soundCmd_;
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_T5182) {
		if (addr <= 0x1fff)
			return mem_[addr];
		if (addr <= 0x3fff)
			return mem_[0x2000u + (addr & 0x7ffu)];
		if (addr <= 0x7fff)
			return mem_[0x4000u + (addr & 0xffu)];
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && sys16RomBoard_ == 0x5047u) {
		/* 315-5010: オペコードプレーンは mem_（Z80 フェッチ）。データプレーンはここ */
		if (qsKabuki_ && qsKabukiData_ && addr < 0x8000u)
			return qsKabukiData_[addr];
		if (addr >= 0x9000 && addr <= 0x903f)
			return 0xff; /* DSW2 */
		if (addr >= 0x9040 && addr <= 0x907f)
			return 0x00; /* DSW1: Demo Sounds オン（bit1=0） */
		if (addr >= 0x9080 && addr <= 0x90ff)
			return 0xff; /* IN0/IN1 アクティブ Low アイドル */
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_TOAPLAN1 && (toaplanKaneko_ == 3 || toaplanKaneko_ == 5)) {
		if (addr == 0xa081)
			return chip_ ? chip_->ReadData() : 0xff;
		if (addr == 0xa091)
			return chip2_ ? chip2_->ReadData() : 0xff;
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_TOAPLAN1 && toaplanKaneko_ == 4) {
		if (addr == 0xe000 || addr == 0xe001) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			if (toaplanTimerA_) {
				st |= 0x40;
				toaplanTimerA_ = 0;
			}
			return st;
		}
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		if (addr >= 0x2000 && addr <= 0x27ff)
			return mem_[addr];
		if (addr == 0x4008 || addr == 0x4009) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			/* raiden2 1081: LD A,(4009); BIT 7; JP NZ — ymfm busy は AdvanceClocks 無しだと sticky */
			if (seibuSongOr80_ >= 2)
				st = (uint8_t)(st & 0x7fu);
			return st;
		}
		if (addr == 0x4010 || addr == 0x4011)
			return seibuMain2Sub_[addr & 1];
		if (addr == 0x4012)
			return seibuSubPending_ ? 1 : 0;
		if (addr == 0x4013)
			return 0xff; /* コイン */
		if (addr == 0x6000)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (addr == 0x6002 && seibuSongOr80_ == 2)
			return pcm2_ ? pcm2_->ReadStatus() : 0x00;
		/* データ復号経由の暗号化 ROM（または平文バンクイメージ） */
		if (soundRom_) {
			if (seibuSongOr80_ == 5) {
				if (addr < 0x2000u && addr < soundRomSize_)
					return seibuEnc_ ? CEmuSei80buData(addr, soundRom_[addr]) : soundRom_[addr];
				if (addr >= 0x8000u && addr < soundRomSize_)
					return soundRom_[addr];
				return 0xff;
			}
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
		if (snkMapKind_ == 3) {
			if (addr >= 0x8000 && addr <= 0x87ff)
				return mem_[addr];
			if (addr == 0xa000)
				return soundCmd_;
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			return mem_[addr];
		}
		if (snkMapKind_ == 2) {
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			if (addr == 0xd000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xe000) {
				snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)~0x04u);
				return 0xff;
			}
			if (addr == 0xf000)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr == 0xf004) {
				snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)~0x08u);
				return 0xff;
			}
			if (addr == 0xf006) {
				SnkSetYmIrq(0, 0);
				return 0xff;
			}
			return mem_[addr];
		}
		if (snkMapKind_) {
			if (addr >= 0xc000 && addr <= 0xcfff)
				return mem_[addr];
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)~0x08u); /* pending をクリア */
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
			case 0: {
				/* gigandes 16E1: LD A,(E000); AND 80 の busy 待ち。ymfm busy は AdvanceClocks 無しだと sticky。 */
				uint8_t st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
				/* East Tech: 00E5 は status&3。16E1 の busy ポールがワンショット
				   パルスを先に食うので、両方のタイマビットを立てたままにする。
				   bit1 が無いと 038B の FM シーケンサに届かない。 */
				if (mem_[0] == 0xf3 && mem_[1] == 0x31
					&& mem_[0x66] == 0xed && mem_[0x67] == 0x4d)
					st = (uint8_t)((st & (uint8_t)~0x03u) | 0x03u);
				return st;
			}
			case 1: return chip_->ReadData();
			case 2: return (uint8_t)(chip_->ReadStatusHi() & 0x7fu);
			default: return chip_->ReadDataHi();
			}
		}
		if (addr == 0xe201) return SytSlaveCommR();
		if (addr >= 0xe000 && addr <= 0xf2ff) return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM) {
		if (taitoOpmMap_ != 14 && taitoOpmMap_ != 16 && taitoOpmMap_ != 17 && taitoOpmMap_ != 18 && mem_[0] == 0xc3
			&& ((mem_[1] == 0x89 && mem_[2] == 0x00)
				|| (mem_[1] == 0xd0 && mem_[2] == 0x03)))
			taitoOpmMap_ = 14;
		if (taitoOpmMap_ != 14 && taitoOpmMap_ != 16 && taitoOpmMap_ != 17 && taitoOpmMap_ != 18 && mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x5e)
			taitoOpmMap_ = 14;
		if (taitoOpmMap_ == 18) {
			if (addr == 0xe000 || addr == 0xe001)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (addr == 0xc800) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			if (addr < 0x8000)
				return mem_[addr];
			return 0xff;
		}
		if (taitoOpmMap_ == 17) {
			if (addr >= 0xe000)
				return mem_[addr];
			if (addr < 0xc000)
				return mem_[addr];
			return 0xff;
		}
		if (taitoOpmMap_ == 16) {
			/* D00C bit6=MCU ready、bit7=0 でブート 17E6 と 15BE 待ちが抜ける。
			   D001 は AY data。F000 nopr。C800 は C000 ミラー。 */
			if (addr == 0xd001 || ((addr & ~0x0fe6u) == 0xd001u))
				return chip_ ? chip_->ReadData() : 0xff;
			if (addr == 0xd00c)
				return 0x40;
			if (addr == 0xd008 || addr == 0xd010)
				return 0xff;
			if (addr == 0xd018)
				return 0x00;
			if (addr >= 0xc000 && addr < 0xd000)
				return mem_[0xc000u + (addr & 0x07ffu)];
			if (addr >= 0xe000 && addr <= 0xefff)
				return mem_[addr];
			if (addr >= 0xf000)
				return 0xff;
			return mem_[addr];
		}
		if (taitoOpmMap_ == 11) {
			if (addr == 0xe000)
				return soundCmd_;
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			if (addr < 0x4000)
				return mem_[addr];
			return 0xff;
		}
		if (taitoOpmMap_ == 15) {
			if (addr == 0x9000 || addr == 0x9001) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_ ? chip_->ReadData() : 0xff;
				}
				return chip_ ? (uint8_t)((chip_->ReadStatus() & 0x03u) | 0x01u) : 0x01;
			}
			if (addr == 0x8801) return SytSlaveCommR();
			return mem_[addr];
		}
		if (taitoOpmMap_ == 12) {
			if (addr == 0xf000 || addr == 0xf001) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_ ? chip_->ReadData() : 0xff;
				}
				/* 11DA は BIT 7,(F000) 待ち。bit7 を落とさないと YM init で止まる。IRQ 02C6 は bit0。 */
				return 0x01;
			}
			if (addr == 0xe001) return SytSlaveCommR();
			return mem_[addr];
		}
		if (taitoOpmMap_ == 13) {
			if (addr == 0xe800 || addr == 0xe801) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_ ? chip_->ReadData() : 0xff;
				}
				return 0x01;
			}
			if (addr >= 0xc000)
				return mem_[addr];
			return mem_[addr];
		}
		if (taitoOpmMap_ == 14) {
			if (addr >= 0xa000 && addr <= 0xa003) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f) {
						/* flipull 04D8: SSG 0x0E → 8213。bit2=0 は JP $7689（DSW 音源テストで
						   I=$98 に張り替え、ゲーム irq1 $0983 の 9A00 キューを捨てる）。bit2=1 で
						   058B の本編へ。0B00 は連続 2 回の bit1、058B は bit0 の AND。0xFF で全部立つ。 */
						return 0xff;
					}
					return chip_ ? chip_->ReadData() : 0xff;
				}
				return 0x01;
			}
			if (addr >= 0xa800 && addr <= 0xa803)
				return 0xff;
			if (addr == 0xb001 || addr == 0xb801)
				return 0x00;
			if (addr >= 0xff00) {
				const unsigned n = addr & 0x0fu;
				if (n <= 8)
					return mem_[0xff00u + n];
			}
			return mem_[addr];
		}
		if (taitoOpmMap_ == 9) {
			if (addr == 0xe000 || addr == 0xe001)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x03u) : 0x00;
			if (addr == 0xe201)
				return SytSlaveCommR();
			return mem_[addr];
		}
		if (taitoOpmMap_ == 10) {
			/* cadash: $1466 は 9001 bit7 busy 待ち。ISR は status&3 のあと bit0 必須。bit7 を落とさないと初期化が止まり、bit0 が無いと 040E シーケンサを飛ばす。 */
			if (chip_ && (addr == 0x9000 || addr == 0x9001))
				return (uint8_t)((chip_->ReadStatus() & 0x03u) | 0x01u);
			if (addr == 0xa001) return SytSlaveCommR();
			return mem_[addr];
		}
		if (taitoOpmMap_ == 2) {
			if (chip_ && (addr == 0xc000 || addr == 0xc001)) {
				if (addr & 1) {
					/* YM2203 SSG 0x0E/0x0F は DSW0/DSW1。オープンバス 0 は Demo Sounds（SWA:4）を殺し、シーケンサがキーしない。 */
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_->ReadData();
				}
				return (uint8_t)(chip_->ReadStatus() & 0x03);
			}
			if (addr >= 0x8000)
				return mem_[addr];
		} else if (taitoOpmMap_ == 3) {
			if (addr == 0x9000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0x9800)
				return (uint8_t)(soundCmdPending_ ? 1 : 0);
			if (chip_ && (addr == 0xb000 || addr == 0xb001)) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_->ReadData();
				}
				/* 0E42 の BIT 7,(HL) ビジー待ち — ビジーをマスク。Timer A を OR し、0167 の ISR poll（AND 01 / JR Z）が永久スピンしないように。 */
				return (uint8_t)((chip_->ReadStatus() & 0x03) | 0x01);
			}
			if (addr >= 0x8000 && addr <= 0x8fff)
				return mem_[addr];
			/* 命令フェッチは MemRead — 32K ROM を 0xFF にしてはいけない */
			if (addr < 0x8000)
				return mem_[addr];
			return 0xff;
		} else if (taitoOpmMap_ == 4 || taitoOpmMap_ == 6) {
			if (chip_ && addr >= 0x9000 && addr <= 0x9fff) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_->ReadData();
				}
				return (uint8_t)((chip_->ReadStatus() & 0x03) | 0x01);
			}
			if (chip2_ && addr >= 0xa000 && addr <= 0xafff) {
				if (addr & 1)
					return chip2_->ReadData();
				/* bublbobl OPL2 ISR は BIT 7 待ち。lkage YM2203 ISR は AND 01 */
				if (taitoOpmMap_ == 6)
					return (uint8_t)((chip2_->ReadStatus() & 0x03) | 0x01);
				return (uint8_t)(chip2_->ReadStatus() | 0x80);
			}
			if ((addr & ~0x0ffcu) == 0xb000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if ((addr & ~0x0ffcu) == 0xb001)
				return (uint8_t)(soundCmdPending_ ? 1 : 0);
			if (addr >= 0x8000 && addr <= 0x8fff)
				return mem_[addr];
			if (addr < 0x8000)
				return mem_[addr];
			return 0xff;
		} else if (taitoOpmMap_ == 5) {
			if (addr == 0xd000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xd800) {
				/* lsasquad: bit0=pending。daikaiju（F3 31）は pending のとき bit0=0 bit1=1。
				   ISR は D800 bit0 が立っていると D000 を読まない。 */
				if (mem_[0] == 0xf3 && mem_[1] == 0x31)
					return (uint8_t)(soundCmdPending_ ? 0x02 : 0x01);
				return (uint8_t)(soundCmdPending_ ? 1 : 0);
			}
			if (chip_ && (addr == 0xa000 || addr == 0xa001)) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_->ReadData();
				}
				/* ISR @0182 AND 01 / JR Z スピン — Timer A をセットのまま */
				return (uint8_t)((chip_->ReadStatus() & 0x03) | 0x01);
			}
			if (addr >= 0x8000 && addr <= 0x87ff)
				return mem_[addr];
			if (addr < 0x8000)
				return mem_[addr];
			return 0xff;
		} else if (taitoOpmMap_ == 7) {
			if (chip_ && (addr == 0xb000 || addr == 0xb001)) {
				if (addr & 1) {
					if (ymAddr_ == 0x0e || ymAddr_ == 0x0f)
						return 0xff;
					return chip_->ReadData();
				}
				return (uint8_t)((chip_->ReadStatus() & 0x03) | 0x01);
			}
			if (addr == 0xc000 || addr == 0xc001 || addr == 0xc002) {
				/* kageki: IN0/IN1 アイドル = アクティブ Low 0xFF。tnzs 5FAC: CALL MCU、CP 55 / JP NZ エラー（スピンし得る）。chukatai: ステータス bit0=IBF、bit1=busy。
				   insectx は DI;IM1;JP 01A1。C000 が 5A/A5 だと 0549 テストへ。開放は 0xFF。 */
				if (mem_[3] == 0x31 && mem_[5] == 0xd7)
					return 0xff;
				if (mem_[3] == 0xc3 && mem_[4] == 0xa1 && mem_[5] == 0x01)
					return 0xff;
				if (mem_[3] == 0xfd && addr == 0xc000)
					return 0x55;
				return 0x01;
			}
			if (addr >= 0xd000 && addr <= 0xefff)
				return mem_[addr];
			if (addr < 0xa000)
				return mem_[addr];
			return 0xff;
		} else if (taitoOpmMap_ == 1) {
			if (chip_ && (addr == 0x9000 || addr == 0x9001))
				return (addr & 1) ? chip_->ReadData() : chip_->ReadStatus();
			if (chip2_ && (addr == 0xa000 || addr == 0xa001))
				return (addr & 1) ? chip2_->ReadData() : chip2_->ReadStatus();
			if (addr == 0xb001) return SytSlaveCommR();
			if (addr >= 0x9000 && addr <= 0xdfff) return 0x00;
		} else {
			if (chip_ && (addr == 0x9000 || addr == 0x9001)) {
				uint8_t st = chip_->ReadStatus();
				/* tetrista/cameltrya の $10ED は BIT 7,(HL) 待ち。busy が張り付くと RST 00。 */
				if (mainIsYm2203_) {
					st = (uint8_t)(st & 0x7fu);
					/* cameltrya ISR $01BB は status bit0 待ち。drain が $9000 に 0x0E を残すと
					   タイマ IRQ 後も bit0=0 のまま固まる。handshake ROM だけ Timer A を立てる。 */
					if (mem_[0x0b] == 0x3e)
						st = (uint8_t)(st | 0x01u);
				}
				return st;
			}
			if (addr == 0xa001) return SytSlaveCommR();
			if (addr >= 0x9000 && addr <= 0xdfff) return 0x00;
		}
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (vsIoKind_ == 4)
			return mem_[addr];
		if (vsIoKind_ >= 1 && vsIoKind_ <= 3) {
			const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time64() : 0;
			if (vsIoKind_ == 1) {
				if (addr >= 0x4000 && addr <= 0x5fff)
					return mem_[0x4000 + (addr & 0x03ff)];
				if (addr >= 0x6000 && addr <= 0x7fff) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				if (addr >= 0x8000 && addr <= 0x9fff)
					return (uint8_t)((cyc / 1024u) & 0x0fu);
				if (addr == 0xe002)
					return 0x00; /* VLM 非 busy */
				if (addr >= 0xc000 && addr <= 0xdfff) {
					if (chip_) {
						chip_->Write(0, (uint8_t)ms1LatchIn_);
						opmWrites_++;
					}
					return 0xff;
				}
				return mem_[addr];
			}
			if (vsIoKind_ == 2) {
				if (addr >= 0x4000 && addr <= 0x4fff)
					return mem_[addr];
				if (addr >= 0x6000 && addr <= 0x7fff) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				if (addr >= 0x8000 && addr <= 0x9fff)
					return (uint8_t)((cyc / 1024u) & 3u);
				return mem_[addr];
			}
			if (addr >= 0x4000 && addr <= 0x43ff)
				return mem_[addr];
			if (addr == 0x8003) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0x8005)
				return (uint8_t)(cyc / 512u);
			return mem_[addr];
		}
		if (addr >= 0x8000 && addr <= 0x9fff)
			return mem_[0x8000 + (addr & 0x07ff)];
		if (addr >= 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xa000) return 0xff; /* 書のみ PSG ポート */
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
		if (vsIoKind_ == 22) {
			if (addr < 0xc000)
				return mem_[addr];
			if (addr <= 0xdfff)
				return mem_[addr];
			if (addr <= 0xe1ff)
				return mem_[addr];
			if (addr == 0xf200 || addr == 0xf400 || addr == 0xf600)
				return 0xff;
			if (addr >= 0xf601 && addr <= 0xf603)
				return 0xff;
			return 0xff;
		}
		if (vsIoKind_ == 24) {
			if (addr == 0x4000) {
				if ((ayAddr_[0] & 0x0f) == 0x0e) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				return chip_ ? chip_->ReadData() : 0xff;
			}
			if (addr < 0x1000 || (addr >= 0x2000 && addr <= 0x23ff))
				return mem_[addr];
			return 0xff;
		}
		if (vsIoKind_ == 23) {
			if (addr < 0x8000)
				return mem_[addr];
			if (addr >= 0xc000 && addr <= 0xc004)
				return 0xff;
			if (addr >= 0xc000 && addr <= 0xefff)
				return mem_[addr];
			return 0xff;
		}
		if (vsIoKind_ == 21)
			return mem_[addr];
		if (vsIoKind_ == 4) {
			if (addr == 0x8000) {
				const uint8_t v = (uint8_t)(soundCmdPending_ ? 1 : 0);
				soundCmdPending_ = 0;
				irqPulse_ = 0;
				return v;
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			if (addr < 0x8000)
				return mem_[addr];
			return 0xff;
		}
		if (vsIoKind_ == 5) {
			addr = (uint16_t)(addr & 0x7fffu);
			if (addr >= 0x4000 && addr < 0x6000) {
				const uint8_t v = (uint8_t)(soundCmdPending_ ? 1 : 0);
				soundCmdPending_ = 0;
				irqPulse_ = 0;
				return v;
			}
			if (addr >= 0x6000)
				return mem_[0x6000u + (addr & 0x07ffu)];
			return mem_[addr];
		}
		if (vsIoKind_ == 6) {
			addr = (uint16_t)(addr & 0x7fffu);
			if (addr >= 0x6000) {
				if (soundCmdPending_) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				return 0x00;
			}
			if (addr >= 0x4000)
				return mem_[0x4000u + (addr & 0x07ffu)];
			return mem_[addr];
		}
		if (vsIoKind_ == 7) {
			if (addr >= 0xc000) {
				if (soundCmdPending_) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				return 0x00;
			}
			if (addr >= 0x8000)
				return mem_[0x8000u + (addr & 0x07ffu)];
			if (addr >= 0x4000)
				return mem_[addr & 0x3fffu];
			return mem_[addr];
		}
		if (vsIoKind_ == 8 || vsIoKind_ == 10) {
			if (addr == 0x8000)
				return soundCmd_;
			if (addr >= 0x4000 && addr < 0x4800)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 11) {
			if (addr == 0x8800)
				return 0x00; /* MCU データ: 0 → ROM チェックサム経路（合計は 0） */
			if (addr == 0x8801)
				return 0x03; /* bit0 ホスト準備、bit1 MCU にデータあり */
			if (addr >= 0x8808 && addr <= 0x880b)
				return 0xff; /* DSW/P1/SYSTEM/P2 アイドル。880A bit7 は 1 のまま */
			if (chip_ && addr == 0x8804)
				return chip_->ReadStatus();
			if (chip_ && addr == 0x8805)
				return chip_->ReadData();
			if (chip2_ && addr == 0x8806)
				return chip2_->ReadStatus();
			if (chip2_ && addr == 0x8807)
				return chip2_->ReadData();
			return mem_[addr];
		}
		if (vsIoKind_ == 13) {
			/* 3000-3FFF ラッチ clear-on-read。RAM 2000-2FFF。4000 は NMI ack 読（捨て）。 */
			if (addr >= 0x3000 && addr < 0x4000) {
				if (soundCmdPending_) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				return 0x00;
			}
			if (addr >= 0x2000 && addr < 0x3000)
				return mem_[0x2000u + (addr & 0x03ffu)];
			return mem_[addr];
		}
		if (vsIoKind_ == 14) {
			if (addr == 0xd000)
				return 0x00;
			if (addr >= 0xe000 && addr < 0xe800)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 15) {
			if (addr == 0x4000) {
				soundCmdPending_ = 0;
				irqPulse_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x2000 && addr < 0x2800)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 16) {
			if (addr >= 0xc000 && addr < 0xd000)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 17) {
			if ((addr & 0xe000u) == 0x6000u)
				return soundCmd_;
			if ((addr & 0xe000u) == 0x8000u) {
				const uint64_t cyc = cpu_ ? (uint64_t)cpu_->time64() : cpuCycles_;
				return (uint8_t)((cyc >> 9) & 0x1eu);
			}
			if (addr >= 0x4000 && addr < 0x6000)
				return mem_[0x4000u + (addr & 0x03ffu)];
			return mem_[addr];
		}
		if (vsIoKind_ == 18) {
			if (addr >= 0x4000 && addr < 0x4400)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 19) {
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x4000 && addr < 0x4800)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 20) {
			if (addr == 0x6000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0x8001)
				return chip_ ? chip_->ReadData() : 0xff;
			if (addr == 0xa001)
				return chip2_ ? chip2_->ReadData() : 0xff;
			if (addr == 0xc001)
				return chip3_ ? chip3_->ReadData() : 0xff;
			if (addr >= 0x4000 && addr < 0x4800)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 12) {
			/* 6000/6001 soundlatch。ファームは 4002 と比較して変化だけ取るので値は安定させておく。 */
			if (addr == 0x6000 || addr == 0x6001)
				return soundCmd_;
			if (addr >= 0x4000 && addr < 0x4800)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 9 && addr >= 0x4000 && addr < 0x4800)
			return mem_[addr];
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
			/* soundlatch_flags_r: bit3 = ラッチ満杯、bit2 = semaphore2 */
			return (uint8_t)((sjLatchFlag_ ? 8 : 0) | (sjSemaphore2_ ? 4 : 0) | 3);
		}
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && vsIoKind_ != 1 && vsIoKind_ != 2) {
		if (addr >= 0x8000 && addr < 0x9000)
			return mem_[0x8000u + (addr & 0x03ffu)];
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && (vsIoKind_ == 1 || vsIoKind_ == 2)) {
		if (addr >= 0x4000 && addr < 0x6000)
			return mem_[0x4000u + (addr & 0x03ffu)];
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		if (vsIoKind_ == 1) {
			if (addr < 0x2000)
				return mem_[addr];
			if (addr >= 0xe000 && addr <= 0xe7ff)
				return mem_[addr];
			return 0xff;
		}
		if (addr < 0x2000)
			return mem_[addr];
		if (addr < 0x3000)
			return mem_[0x2000 + (addr & 0x03ff)];
		if (addr < 0x4000)
			return mem_[0x3000 + (addr & 0x03ff)];
		if ((addr & 0xf000) == 0x4000) {
			const uint8_t a = (uint8_t)(ayAddr_[0] & 0x0f);
			if (a == 0x0e) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (a == 0x0f) {
				/* jungler/locomotn（$2000 RAM）と pooyan（bit7 待ち）は IN 待ちで time64 が止まる。
				   timeplt 本体は KonamiAyTimer（60Hz シーケンサ）。 */
				const int timerPace = (mem_[2] == 0x20) || (mem_[0x00cf] == 0x80)
					|| (mem_[0x00c6] == 0x80);

				if (timerPace) {
					ayAddr_[2] = (uint8_t)(ayAddr_[2] + 1u);
					static const uint8_t kTimer[10] = {
						0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0
					};
					return kTimer[(ayAddr_[2] >> 4) % 10u];
				}
				return KonamiAyTimer();
			}
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
			/* AY1 データ。レジスタ 0x0E = MAME nemesis_portA_r（周期タイマ） */
			if ((ayAddr_[0] & 0x0f) == 0x0e)
				return Gx400PortA();
			return chip_ ? chip_->ReadData() : 0xff;
		}
		if (addr == 0xe205) {
			/* AY2 I/O A はハードでは K005289 書ポート。読は未使用 */
			return chip2_ ? chip2_->ReadData() : 0xff;
		}
		if (addr >= 0x4000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0xa000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		if (snkMapKind_ >= 4)
			return 0xff;
		if (snkMapKind_ != 1 && mem_[0] == 0xc3 && mem_[1] == 0x00 && mem_[2] == 0x01
			&& snkMapKind_ == 0)
			snkMapKind_ = 2;
		if (snkMapKind_ == 3) {
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xe000)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xf000 || addr == 0xf001) {
				if (chip_) {
					uint8_t st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
					if ((st & 0x03) == 0) {
						chip_->AdvanceClocks(256);
						st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
					}
					return st;
				}
				return 0x00;
			}
			if (addr >= 0x8000 && addr <= 0xbfff)
				return mem_[addr];
			return mem_[addr];
		}
		if (snkMapKind_ == 2) {
			if (addr == 0xc000 || addr == 0xc001) {
				if (chip_) {
					uint8_t st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
					if ((st & 0x03) == 0) {
						chip_->AdvanceClocks(256);
						st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
					}
					return st;
				}
				return 0x00;
			}
			if (addr >= 0xd000 && addr <= 0xd002)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x8000 && addr <= 0xbfff)
				return mem_[addr];
			return mem_[addr];
		}
		if (snkMapKind_ == 1) {
			if (addr == 0xc800 || addr == 0xc801)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (addr == 0xd800)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			return mem_[addr];
		}
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
		if (konamiK7232Map_ == 7) {
			if (addr >= 0xf400 && addr <= 0xf403)
				return 0xff;
			if (addr == 0xf440)
				return 0xff;
			if (addr == 0xf441)
				return 0xf7; /* bit3 052591 ready（ゲームは 0 待ち） */
			if (addr == 0xf540)
				return 0x00;
			if (addr == 0xf200)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr >= 0xe800 && addr <= 0xe8ff)
				return chip_ ? CEmuChipSccReadReg(chip_, (unsigned)(addr - 0xe800u)) : 0xff;
			if (addr >= 0xa000)
				return mem_[addr];
			return mem_[addr];
		}
		if (konamiK7232Map_ == 3) {
			if (addr == 0xd000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xe000 || addr == 0xe001)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (addr == 0xb000)
				return 0x00; /* UPD7759 非 busy */
			if (addr >= 0x8000 && addr <= 0x87ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (konamiK7232Map_ == 2) {
			if (addr == 0xf010) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xf030 || addr == 0xf031)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr >= 0xf800)
				return mem_[addr];
			if (addr >= 0xf000)
				return 0x00;
			return mem_[addr];
		}
		const unsigned latch = (konamiK7232Map_ == 1) ? 0xc000u
			: (konamiK7232Map_ == 4) ? 0xe000u
			: (konamiK7232Map_ == 5) ? 0xd000u
			: 0xa000u;
		const unsigned ym = konamiK7232Map_ == 1 ? 0xa000u : 0xc000u;
		if (addr == latch) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == ym || addr == ym + 1u) {
			if (!chip_) return 0x00;
			if (konamiK7232Map_ == 6) {
				/* wecleman/flkatck: RST 08 が status bit7 busy を待ち、メインループが bit1 Timer B を poll。
				   ymfm busy は AdvanceClocks 無しだと sticky。bit7 を落とし、タイマが寝ていれば 256clk 進める。 */
				uint8_t st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
				if ((st & 0x03) == 0) {
					chip_->AdvanceClocks(256);
					st = (uint8_t)(chip_->ReadStatus() & 0x7fu);
				}
				return st;
			}
			return chip_->ReadStatus();
		}
		if (konamiK7232Map_ == 6 && addr >= 0x9000 && addr <= 0x9007)
			return CEmuAcK007452Read(namcoCus30_, addr - 0x9000u);
		if (addr >= 0x8000 && addr <= 0x8fff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0x00; /* K007232 / stub 群 */
	}
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		/* 0000-7FFF ROM / C000-FFFF バンクは LoadRoms+SetBank 経由で既に mem_ にライブ */
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		if (addr == 0xd000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		{
			const unsigned ym = HcastleSpyMap() ? 0xc000u : 0xa000u;
			if (addr == ym || addr == (ym + 1u))
				return chip_ ? chip_->ReadStatus() : 0x00;
		}
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_RAIZING)
		return RaizingMemRead(addr);
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		if (tecmoOpl_ == 11) {
			if (addr == 0xf800) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xf808 || addr == 0xf809)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0x00;
			if (addr == 0xf80a)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr >= 0xf000 && addr <= 0xf7ff)
				return mem_[addr];
			if (addr >= 0xf000)
				return 0x00;
			return mem_[addr];
		}
		if (tecmoOpl_ == 12) {
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xc000 && addr <= 0xdfff)
				return mem_[addr];
			if (addr >= 0xc000)
				return 0x00;
			return mem_[addr];
		}
		if (tecmoOpl_ == 13) {
			if (addr == 0xd000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xa000 || addr == 0xa001)
				/* ymfm YM2151: 奇数オフセットが status。ISR は A001 bit1 で Timer B とラッチを分ける。 */
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr == 0xb000)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xc000)
				return pcm2_ ? pcm2_->ReadStatus() : 0x00;
			if (addr >= 0x8000 && addr <= 0x87ff)
				return mem_[addr];
			if (addr < 0x8000)
				return mem_[addr];
			return 0x00;
		}
		if (tecmoOpl_ == 5)
			return mem_[(addr >= 0xc000) ? (0xc000u + (addr & 0x1fffu)) : addr];
		if (tecmoOpl_ == 7 || tecmoOpl_ == 8 || tecmoOpl_ == 9 || tecmoOpl_ == 10)
			return mem_[addr];
		if (tecmoOpl_ == 6) {
			if (addr >= 0xf800 && addr <= 0xf803) {
				if (!chip_) return 0x00;
				const unsigned a = addr & 3u;
				if (a == 0) return chip_->ReadStatus();
				if (a == 1) return chip_->ReadData();
				if (a == 2) return chip_->ReadStatusHi();
				return chip_->ReadDataHi();
			}
			if (addr == 0xfc10) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xf000 && addr <= 0xf7ff)
				return mem_[addr];
			if (addr >= 0xf000)
				return 0x00;
		} else if (tecmoOpl_ == 1) {
			if (addr == 0x8000 || addr == 0x8001)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x4000 && addr <= 0x47ff)
				return mem_[addr];
			if (addr >= 0x4000)
				return 0x00;
		} else if (tecmoOpl_ == 2) {
			if (addr == 0xa000 || addr == 0xa001)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x8000 && addr <= 0x87ff)
				return mem_[addr];
			if (addr >= 0x8000)
				return 0x00;
		} else if (tecmoOpl_ == 4) {
			if (addr == 0xd000 || addr == 0xd001)
				return chip_ ? chip_->ReadStatus() : 0x00;
			if (addr == 0xd800 || addr == 0xd801)
				return chip2_ ? chip2_->ReadStatus() : 0x00;
			if (addr == 0xe010) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			if (addr >= 0xc000)
				return 0x00;
		} else if (tecmoOpl_ == 3) {
			if (addr == 0xf800)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xf810 || addr == 0xf811)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x03u) : 0x00;
			if (addr == 0xfc20) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xf000 && addr <= 0xf7ff)
				return mem_[addr];
			if (addr >= 0xf000)
				return 0x00;
		} else {
			if (addr == 0xfc00)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xfc04 || addr == 0xfc05)
				/* 0029 / RST28 は FC05 bit7。ROM イメージは FC05=FF。ステータスはタイマ bit のみ返す。 */
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x03u) : 0x00;
			if (addr == 0xfc08) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xf000 && addr <= 0xfbff)
				return mem_[addr];
			if (addr >= 0xf000)
				return 0x00;
		}
	}
	if (board_ == CEMU_AC_BOARD_FLSTORY) {
		if (taitoOpmMap_ == 1) {
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xc001)
				return (uint8_t)(soundCmdPending_ ? 1 : 0);
			if (addr >= 0x4000 && addr <= 0x47ff)
				return mem_[addr];
			if (addr < 0x4000)
				return mem_[addr];
			return 0xff;
		}
		if (taitoOpmMap_ == 3) {
			if (addr == 0xd000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			if (addr < 0xc000)
				return mem_[addr];
			return 0xff;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff)
			return mem_[addr];
		if (addr == 0xd800) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xda00)
			return (uint8_t)(soundCmdPending_ ? 1 : 0);
		if (addr == 0xde00)
			return 0x00; /* DAC 読は不明 */
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
		if (vsIoKind_ == 15) {
			/* Kabuki: M1 は mem_（オペコード）。即値／LD はデータ面。
			   バンク窓 8000-BFFF は soundRom_/qsKabukiData_ の 32K+bank*16K。 */
			if (addr >= 0xc000u)
				return mem_[addr];
			if (qsKabuki_ && qsKabukiData_) {
				uint8_t v;
				if (addr < 0x8000u)
					v = qsKabukiData_[addr];
				else {
					unsigned src = 0x8000u + (unsigned)(bank_ & 0x0f) * 0x4000u
						+ (unsigned)(addr - 0x8000u);
					v = (soundRomSize_ == 0 || src < soundRomSize_)
						? qsKabukiData_[src] : 0xff;
				}
				return v;
			}
			return mem_[addr];
		}
		if (vsIoKind_ == 13) {
			if (addr == 0xf800) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xe800 || addr == 0xe801)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (addr == 0xf000 || addr == 0xf001)
				return chip2_ ? (uint8_t)(chip2_->ReadStatus() & 0x7f) : 0x00;
			return mem_[addr];
		}
		if (vsIoKind_ == 14) {
			if (addr == 0xf006) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0xf000 || addr == 0xf001)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (addr == 0xf002 || addr == 0xf003)
				return chip2_ ? (uint8_t)(chip2_->ReadStatus() & 0x7f) : 0x00;
			if (addr == 0xf004)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			return mem_[addr];
		}
		if (vsIoKind_ == 8) {
			if (addr == 0xf000) {
				const uint8_t v = soundCmd_;
				soundCmd_ = (uint8_t)(soundCmd_ & 0x7fu);
				soundCmdPending_ = 0;
				return v;
			}
			if (addr == 0xc000 || addr == 0xc001)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7f) : 0x00;
			if (addr == 0xc800 || addr == 0xc801)
				return chip2_ ? (uint8_t)(chip2_->ReadStatus() & 0x7f) : 0x00;
			return mem_[addr];
		}
		if (vsIoKind_ == 9) {
			if (addr >= 0xe000 && addr <= 0xe7ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 10 || vsIoKind_ == 12)
			return mem_[addr];
		if (vsIoKind_ == 11)
			return mem_[addr];
		if (vsIoKind_ == 6 || vsIoKind_ == 7) {
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 3 || vsIoKind_ == 4 || vsIoKind_ == 5) {
			if (addr >= 0xc000 && addr <= 0xdfff)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 2) {
			if (addr == 0xf000)
				return soundCmd_;
			if (addr == 0xf400)
				return pcm_ ? pcm_->ReadStatus() : 0x00;
			if (addr == 0xf500)
				return pcm2_ ? pcm2_->ReadStatus() : 0x00;
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (vsIoKind_ == 1) {
			if (addr == 0xf000)
				return soundCmd_;
			if (addr >= 0xc000 && addr <= 0xdfff)
				return mem_[addr];
			return mem_[addr];
		}
		if (qsKabuki_ && qsKabukiData_ && addr < 0xc000u)
			return qsKabukiData_[addr];
		if (addr == 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff)
			return mem_[addr];
		return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE) {
		if (terracreMap_ == 3 || terracreMap_ == 5) {
			if (addr == 0xc000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x8000 && addr < 0x8800)
				return mem_[addr];
			return mem_[addr];
		}
		if (terracreMap_ == 4) {
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0xc000 && addr < 0xc800)
				return mem_[addr];
			return mem_[addr];
		}
		if (terracreMap_ == 2) {
			if (addr >= 0xc000)
				return mem_[addr];
		} else if (terracreMap_ == 1) {
			if (addr >= 0xf800)
				return mem_[addr];
		} else if (addr >= 0xc000 && addr <= 0xcfff) {
			return mem_[addr];
		}
	}
	if (board_ == CEMU_AC_BOARD_CPS1) {
		/* YM2151 ステータス（bit7=busy）。fmgen に busy は無い。タイマビットだけ返す */
		if (chip_ && (addr == 0xf000 || addr == 0xf001))
			return chip_->ReadStatus();
		/* メイン 68K からのサウンドコマンドラッチ（Z80 が poll）。F008 = ラッチ lo。F00A = ラッチ hi / 第 2 バイト。 */
		if (addr == 0xf008) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xf00a) {
			/* version-5（megaman/sfzch: LD SP,D800）ISR は D010 を 16bit キューポインタに使う。
			   D010=F000（00AD が置く YM ポート）のまま F00A==0xFF だと EX DE,HL が F000 を曲リングへ流し全 TL=7F。
			   D011==0xF0 の間は 0 を返しフラッシュしない。pending 中は 0 で (0,cmd) を D010 に格納し、
			   次の ISR（F008 が pending を落としたあと）で 0xFF を返して格納語をリングへ送る。 */
			const int cpsVer5 = (mem_[3] == 0x31 && mem_[4] == 0x00 && mem_[5] == 0xd8);
			if (cpsVer5) {
				if (mem_[0xd011] == 0xf0)
					return 0x00;
				return soundCmdPending_ ? (uint8_t)0x00 : (uint8_t)0xff;
			}
			return 0xff;
		}
		if ((addr == 0xf002 || addr == 0xf003) && pcm_)
			return pcm_->ReadStatus();
		/* 未マップ I/O ミラー — ROM 0xFF を返さない（busy でスピンする） */
		if (addr >= 0xf000 && addr <= 0xf00f)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_CPS_QS) {
		if (addr == 0xd007)
			return chip_ ? chip_->ReadStatus() : 0x80;
		/* Kabuki: オペコードプレーンは mem_（Z80 フェッチ）。データプレーンはここ */
		if (qsKabuki_ && qsKabukiData_ && addr < 0x8000u)
			return qsKabukiData_[addr];
		/* C000-CFFF / F000-FFFF は RAM — mem_[] へフォールスルー */
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM) {
		const unsigned opm = konamiOpmAddr_ ? konamiOpmAddr_ : 0xf800u;
		const unsigned pcmBase = konamiPcmAddr_ ? konamiPcmAddr_ : 0xfc00u;
		const unsigned pcmWin = konamiPcmWindow_ ? konamiPcmWindow_ : 0x40u;
		/* K054321: Bucky LD BC,(F002)、gijoe/lethalen LD BC,(FC02) */
		{
			const unsigned k321 = KonamiJoeMap() ? 0xfc00u : 0xf000u;
			if (pcmKind_ == 4 && (addr == k321 + 2u || addr == k321 + 3u)) {
				if (addr == k321 + 2u) {
					soundCmdPending_ = 0;
					return soundCmd_;
				}
				return 0x00;
			}
		}
		if (KonamiRollergMap() && chip_ && (addr == 0xc000u || addr == 0xc001u))
			return chip_->ReadStatus();
		if (chip_ && pcm_ && !KonamiJoeMap() && opm != 0xffffu
			&& (addr == opm || addr == (opm + 1u)
				|| (KonamiXmenMap() && (addr == 0xec00u || addr == 0xec01u))
				|| (pcmKind_ == 3 && (addr == (opm + 0x10u) || addr == (opm + 0x11u)))))
			return chip_->ReadStatus();
		if (pcmKind_ == 3 && opm == 0xf000u && chip_ && pcm_
			&& (addr == 0xf000 || addr == 0xf001))
			return chip_->ReadStatus();
		if (pcm2_ && konamiPcm2Addr_
			&& addr >= konamiPcm2Addr_ && addr < konamiPcm2Addr_ + pcmWin)
			return pcm2_->ReadStatus();
		if (KonamiPrmrsocrMap() && pcmKind_ == 4 && addr >= 0xe000u && addr <= 0xe22fu) {
			CChip* pcm = pcm_ ? pcm_ : chip_;
			if (!pcm) return 0x00;
			const unsigned off = addr - 0xe000u;
			const unsigned reg = ((off & 0x100u) << 1) | (off & 0xffu);
			return CEmuChipK054539PeekReg(pcm, reg);
		}
		if (addr >= pcmBase && addr < pcmBase + pcmWin) {
			CChip* pcm = pcm_ ? pcm_ : chip_;
			if (!pcm) return 0x00;
			if (pcmKind_ == 3)
				return CEmuChipK053260Read(pcm, addr - pcmBase);
			if (KonamiJoeMap())
				return CEmuChipK054539PeekReg(pcm, addr - pcmBase);
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
		if (gngCommandoMap_ == 4) {
			if (addr == 0x8000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr >= 0x4000 && addr <= 0x43ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (gngCommandoMap_ == 5) {
			if (addr == 0xb000) {
				/* skyfox: ラッチ @B000。momoko はここを読まない。 */
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (chip_ && (addr == 0xa000 || addr == 0xa001)) {
				if (addr & 1)
					return chip_->ReadData();
				return (uint8_t)(chip_->ReadStatus() & 0x7fu);
			}
			if (chip2_ && (addr == 0xc000 || addr == 0xc001)) {
				if (addr & 1) {
					if ((gngYmAddr_[1] & 0x0fu) == 0x0eu) {
						soundCmdPending_ = 0;
						return soundCmd_;
					}
					return chip2_->ReadData();
				}
				return (uint8_t)(chip2_->ReadStatus() & 0x7fu);
			}
			if (addr >= 0x8000 && addr <= 0x87ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (gngCommandoMap_ == 6) {
			if (addr == 0xb800) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (chip_ && (addr == 0xb000 || addr == 0xb001)) {
				if (addr & 1)
					return chip_->ReadData();
				return (uint8_t)(chip_->ReadStatus() & 0x7fu);
			}
			if (chip2_ && (addr == 0xb400 || addr == 0xb401)) {
				if (addr & 1)
					return chip2_->ReadData();
				return (uint8_t)(chip2_->ReadStatus() & 0x7fu);
			}
			if (addr >= 0x8000 && addr <= 0x8fff)
				return mem_[addr];
			return mem_[addr];
		}
		if (gngCommandoMap_ == 2) {
			if (addr == 0xd000)
				return soundCmd_;
			if (chip_ && (addr == 0xf000 || addr == 0xf001)) {
				/* 009A は (F000) bit7 busy 待ち。fmgen は busy を戻すのでクリアしないとブートでハング。タイマフラグは残す。 */
				if (addr & 1)
					return chip_->ReadData();
				return (uint8_t)(chip_->ReadStatus() & 0x7fu);
			}
			if (chip2_ && (addr == 0xf002 || addr == 0xf003)) {
				if (addr & 1)
					return chip2_->ReadData();
				return (uint8_t)(chip2_->ReadStatus() & 0x7fu);
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (gngCommandoMap_ == 3) {
			if (addr == 0xe000) {
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (chip_ && (addr == 0x8000 || addr == 0x8001)) {
				if (addr & 1)
					return chip_->ReadData();
				return (uint8_t)(chip_->ReadStatus() & 0x7fu);
			}
			if (chip2_ && (addr == 0xa000 || addr == 0xa001)) {
				if (addr & 1)
					return chip2_->ReadData();
				return (uint8_t)(chip2_->ReadStatus() & 0x7fu);
			}
			if (addr >= 0xc000 && addr <= 0xc7ff)
				return mem_[addr];
			return mem_[addr];
		}
		if (gngCommandoMap_ == 1) {
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
		/* Avengers: ラッチ ACK + pending ビット @E006（MAME lwings_sound_map） */
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
		if (vsIoKind_ == 1) {
			if (pcm_ && ((addr >= 0xf000 && addr <= 0xf0ff) || (addr >= 0x1000 && addr <= 0x1fff))) {
				uint8_t ram[256];
				if (pcm_->GetRegSnapshot(ram, 256) > 0)
					return ram[addr & 0xff];
			}
			return mem_[addr];
		}
		if (chip_ && (addr >= 0xd000 && addr <= 0xdfff) && (addr & 1) == 0)
			return chip_->ReadStatus();
		/* SegaPCM は RW マップ — Z80 が ctrl/end を poll。書は mem_[] に当たらない */
		if (pcm_ && addr >= 0xe000 && addr <= 0xefff) {
			uint8_t ram[256];
			if (pcm_->GetRegSnapshot(ram, 256) > 0)
				return ram[addr & 0xff];
		}
	}
	/* OutRun / After Burner: SegaPCM @ $F000（と $1000 ミラー） */
	if ((board_ == CEMU_AC_BOARD_OUTRUN || board_ == CEMU_AC_BOARD_ABURNER)
		&& pcm_
		&& ((addr >= 0xf000 && addr <= 0xf0ff) || (addr >= 0x1000 && addr <= 0x1fff))) {
		uint8_t ram[256];
		if (pcm_->GetRegSnapshot(ram, 256) > 0)
			return ram[addr & 0xff];
	}
	if ((board_ == CEMU_AC_BOARD_SYS18 || board_ == CEMU_AC_BOARD_SYS24) && pcm_) {
		if ((addr >= 0xc000 && addr <= 0xcfff) || (addr >= 0xd000 && addr <= 0xdfff)) {
			uint8_t ram[256];
			if (pcm_->GetRegSnapshot(ram, 256) > 0)
				return ram[addr & 0xff];
		}
	}
	if (board_ == CEMU_AC_BOARD_SYS32 && pcm_ && addr >= 0xc000 && addr <= 0xdfff) {
		if (Sys32MultiPcm())
			return pcm_->ReadStatus();
		if (addr & 0x1000)
			return CEmuChipRf5c68MemR(pcm_, addr & 0xfffu);
		/* MAME rf5c68 map: A12=0 はレジスタ write-only。読は unmap 0xFF。 */
		return 0xff;
	}
	return mem_[addr];
}

/* CEmuAcZipBaseName の実装 */
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

/* 音源 CPU 候補を採点。cotton.zip は s-prog.rom より先に speech0.rom を列挙 — PCM を先に選ぶと PC=AD08 / peak=0。16–64K の Z80 DI/IM1 ダンプを優先。 */
static int CEmuAcIsCodeRomType(const char* t)
{
	if (!t || !t[0]) return 0;
	if (_stricmp(t, "code") == 0 || _stricmp(t, "sub") == 0) return 1;
	if (_stricmp(t, "audiocpu") == 0 || _stricmp(t, "sound") == 0) return 1;
	/* xml2: code0/code1（バイト分割 68000）と sub0/sub1（音源 CPU） */
	if (_strnicmp(t, "code", 4) == 0) return 1;
	if (_strnicmp(t, "sub", 3) == 0) return 1;
	return 0;
}

/* カタログは Sys16 uPD7751 音声 MCU を type=sub @0000（shinobi/alexkidd/bodyslam…）。それは Z80 コードではない — mem_ へ載せる extra epr-11267（F3 ED 56）を 04 20… で消し、PC が MCU ゴミで固まった。 */
static int CEmuAcIsSys16SpeechMcu(const char* name, unsigned sz)
{
	if (sz > 0 && sz <= 0x400u) return 1;
	if (!name || !name[0]) return 0;
	char base[CEMU_ZIP_PATH];
	CEmuAcZipBaseName(name, base, (int)sizeof(base));
	return (strstr(base, "7751") != NULL || strstr(base, "7759") != NULL) ? 1 : 0;
}

/* CEmuAcSoundRomScore の実装 */
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
	/* 古典 16/32/64K 音源 CPU を優先。128K はしばしばメイン/GFX（aburner2 epr-11102） */
	if (sz == 0x8000 || sz == 0x4000 || sz == 0x10000) score += 50;
	else if (sz == 0x20000) {
		/* CPS2 Z80 はバンク 128K — 明らかな CPS/QSound 名だけブースト */
		if (strstr(base, "sfx") || strstr(base, "sz3") || strstr(base, ".01")
			|| strstr(base, "qsound"))
			score += 70;
		else
			score -= 40;
	}
	else if (sz == 0x1000 || sz == 0x2000) score += 45; /* Galaga 期 WSG Z80 */
	else if (sz < 0x2000) score -= 20;
	/* Z80 ブート: DI; IM 1 — System16 音源 CPU */
	if (sz >= 4 && data[0] == 0xf3 && data[1] == 0xed && data[2] == 0x56)
		score += 100;
	else if (sz >= 4 && data[0] == 0xf3 && data[1] == 0xed && data[2] == 0x5e)
		score += 100; /* System 32: DI; IM 2 */
	else if (sz >= 2 && data[0] == 0xed && data[1] == 0x56)
		score += 100; /* GNG gg2.bin: 先に IM 1 */
	else if (sz >= 1 && (data[0] == 0xf3 || data[0] == 0xc3 || data[0] == 0x31))
		score += 40;
	if (_strnicmp(base, "s-prog", 6) == 0 || _strnicmp(base, "sprog", 5) == 0)
		score += 80;
	if (_strnicmp(base, "gg2", 3) == 0) score += 80; /* Capcom GNG 音源 */
	if (_strnicmp(base, "epr", 3) == 0) score += 40;
	/* OutRun 音源 CPU は素の "10187" / epr-10187.* */
	if (strstr(base, "10187") || strstr(base, "11112")) score += 80;
	if (strstr(base, "7233") || strstr(base, "7234")) score += 80;
	if (strstr(base, "ic72") || strstr(base, "ic73")) score += 70;
	if (strstr(base, "7231") || strstr(base, "7232")) score -= 120;
	if (strstr(base, "sound") || strstr(base, "snd")) score += 30;
	/* CPS2 QSound プログラム: sfx.01 / sz3.01 */
	if (strstr(base, ".01") || strstr(base, "sfx") || strstr(base, "sz3.01"))
		score += 90;
	/* Konami 音源プログラム名 */
	if (strstr(base, ".e01") || strstr(base, "e01") || strstr(base, "e03")
		|| strstr(base, "m05") || strstr(base, "768."))
		score += 70;
	/* shinobi.a7 / epr7535.fz 風の音源プログラム名 */
	if (n >= 3 && (base[n - 2] == 'a' || base[n - 2] == 'A') &&
		(base[n - 1] >= '7' && base[n - 1] <= '9'))
		score += 20;
	return score;
}

/* CEmuAcContainsI の実装 */
static int CEmuAcContainsI(const char* s, const char* needle)
{
	if (!s || !needle || !needle[0]) return 0;
	const size_t nl = strlen(needle);
	for (const char* p = s; *p; p++)
		if (_strnicmp(p, needle, nl) == 0)
			return 1;
	return 0;
}

/* CEmuAcPcmRomScore の実装 */
static int CEmuAcPcmRomScore(const char* name, const char* type, unsigned sz)
{
	if (!name || !sz) return -1000;
	/* 明示カタログ役割が権威。数値 QSound ファイル名（ts2-02 等）はさもなくば妥当 PCM として採点され、本物サンプル ROM の後に付き、高バンクで Z80 オペコードが符号付き音声として露出する。 */
	if (type && (_stricmp(type, "code") == 0 || _stricmp(type, "sub") == 0
		|| _stricmp(type, "audiocpu") == 0 || _stricmp(type, "program") == 0))
		return -1000;
	char base[CEMU_ZIP_PATH];
	CEmuAcZipBaseName(name, base, (int)sizeof(base));
	int score = 0;
	if (type && (_stricmp(type, "voice") == 0 || _stricmp(type, "adpcm") == 0
		|| _stricmp(type, "adpcm1") == 0 || _stricmp(type, "adpcm2") == 0
		|| _stricmp(type, "pcm") == 0 || _stricmp(type, "pcm1") == 0
		|| _stricmp(type, "pcm2") == 0 || _stricmp(type, "sample") == 0))
		score += 100;
	if (_strnicmp(base, "speech", 6) == 0) score += 120;
	if (_strnicmp(base, "pcm", 3) == 0) score += 100;
	if (_strnicmp(base, "opr", 3) == 0) score += 80;
	if (_strnicmp(base, "mpr", 3) == 0 && sz >= 0x10000) score += 90; /* Sega PCM バンク */
	if (CEmuAcContainsI(base, "voice")) score += 100;
	if (CEmuAcContainsI(base, "oki")) score += 100;
	if (CEmuAcContainsI(base, "qsound") || CEmuAcContainsI(base, "qs")) score += 100;
	if (CEmuAcContainsI(base, "c352") || CEmuAcContainsI(base, "wav")
		|| CEmuAcContainsI(base, "wave"))
		score += 100;
	if (CEmuAcContainsI(base, "k053260") || CEmuAcContainsI(base, "053260")) score += 100;
	if (CEmuAcContainsI(base, "k054539") || CEmuAcContainsI(base, "054539")) score += 100;
	/* CPS2 サンプルバンク sfx.11 / sz3.11 */
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
		score -= 80; /* プログラムでありサンプルではない */
	if (CEmuAcContainsI(base, "7231") || CEmuAcContainsI(base, "7232")
		|| CEmuAcContainsI(base, "snd7231") || CEmuAcContainsI(base, "snd7232"))
		score += 120;
	if (CEmuAcContainsI(base, "sound") || CEmuAcContainsI(base, "snd"))
		score -= 20;
	return score ? score : -100;
}

/* CEmuAcAppendPcm の実装 */
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

/* カタログオフセットに ROM を置く。穴は 0xFF（オープンバス／未プログラム） */
static int CEmuAcPlacePcm(uint8_t** dst, unsigned* dstSize, unsigned offset,
	const uint8_t* data, unsigned size)
{
	if (!dst || !dstSize || !data || !size) return 0;
	if (offset > 0xffffffffu - size) return 0;
	const unsigned need = offset + size;
	if (*dstSize < need) {
		uint8_t* p = (uint8_t*)realloc(*dst, (size_t)need);
		if (!p) return 0;
		if (need > *dstSize)
			memset(p + *dstSize, 0xff, (size_t)(need - *dstSize));
		*dst = p;
		*dstSize = need;
	}
	memcpy(*dst + offset, data, size);
	return 1;
}

/* hoot カタログ <option> 値。C 風 hex/10 進として解析 */
static unsigned CEmuAcOptionValue(const CEmuGameEntry* ge, const char* name, unsigned dflt)
{
	if (!ge || !ge->opt) return dflt;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) == 0)
			return (unsigned)strtoul(ge->opt[i].value, NULL, 0);
	}
	return dflt;
}

/* 音源プログラムが別 Z80 ダンプではなくバイトインターリーブ主 CPU ROM 対の中に居る Irem M72 変種（airduel、gallop）。romlist は対をオフセット 0 と 1 でタグし、"z80_offset" オプションに Z80 窓を持つ。重なる 64K イメージ 2 枚として載せる extra Z80 が V30 データを実行した。呼び出し側所有の 64K イメージを返す。 */
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
	/* nspirit は z80_offset=0x11000、対は 64K×2=128K。末尾 64K を要求すると 0x10000 へ落ち、V30 データを実行する。
	   窓が 64K 未満でも offset からコピー（残りは 0 = sound_ram の作業域）。offset がイメージ外のときだけ末尾へ。 */
	if (base >= interleaved)
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

/* CEmuAcPrimaryPcmTarget の実装 */
static CChip* CEmuAcPrimaryPcmTarget(CHardAc* hw)
{
	if (!hw) return NULL;
	/* YM2610 基板は下の専用ローダ経由で ADPCM-A/B を取る。SetPcmRom ではない（全部 ADPCM-B バンクに着く）。Mega System 1 は独立 OKI バンク 2 本 — それも別ロード。 */
	if (hw->board_ == CEMU_AC_BOARD_VSYSTEM || hw->board_ == CEMU_AC_BOARD_TAITO_YM2610
		|| hw->board_ == CEMU_AC_BOARD_MEGASYSTEM1 || hw->board_ == CEMU_AC_BOARD_KONAMI_GX)
		return NULL;
	/* SYS32 RF5C68 は Z80 が 64KB RAM へアップロード。hoot の type=pcm mpr-* はグラフィック。 */
	if (hw->board_ == CEMU_AC_BOARD_SYS32 && !hw->Sys32MultiPcm())
		return NULL;
	if (hw->PcmChip()) return hw->PcmChip();
	if (hw->board_ == CEMU_AC_BOARD_CPS_QS || hw->board_ == CEMU_AC_BOARD_KONAMI_PCM
		|| hw->board_ == CEMU_AC_BOARD_NAMCO_C352
		|| hw->board_ == CEMU_AC_BOARD_NAMCO_WSG)
		return hw->SoundChip();
	return NULL;
}

/* YM2610 ADPCM-A / ADPCM-B バンク。カタログ rom 型は "adpcma" / "adpcmb" でファイル毎の到着オフセット（ninjaw は 512K ADPCM-A バンク 3 本）。 */
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

/* Mega System 1 音源 ROM は ROM_LOAD16_BYTE 対: カタログ最初の "code" メンバが D15-D8、2 番目が D7-D0。リセットベクタで確認 — edf/64street はその順でのみ SSP=0x000FFFFE、PC=0x00000400。 */
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
	/* 事前組立 68000 イメージ 1 本（metamrph/rungun 054539x2 パックは偶数／奇数半分ではなく 256KiB audiocpu 1 本）。 */
	if (n == 1 && half[0] && halfSize[0] >= 0x10000u) {
		uint8_t* p = (uint8_t*)malloc(halfSize[0]);
		if (!p) return 0;
		memcpy(p, half[0], halfSize[0]);
		*dst = p;
		*dstSize = halfSize[0];
		return 1;
	}
	if (n < 2) {
		/* 使えるカタログ項目が無い: 68000 プログラム半分に見える同じサイズのメンバ 2 つを名順で取る */
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

/* Seta パックは偶数／奇数対の上に第 3 プログラム ROM を持つ — blandia の ux001003.u202 が 100000-1FFFFF を覆う。CEmuAcLoadMs1Code は最初の 2 つだけインターリーブするので、尾は別置き。MAME が ROM_LOAD16_WORD_SWAP で載せるためバイトスワップ。 */
static void CEmuAcLoadM68kPcmExtraCode(uint8_t** dst, unsigned* dstSize,
	CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		const unsigned off = (unsigned)(r->offset > 0 ? r->offset : 0);
		if (off < 2u) continue; /* インターリーブ対 */
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		const unsigned need = off + sz;
		if (need > *dstSize) {
			uint8_t* p = (uint8_t*)realloc(*dst, need);
			if (!p) return;
			memset(p + *dstSize, 0xff, need - *dstSize);
			*dst = p;
			*dstSize = need;
		}
		for (unsigned k = 0; k + 1 < sz; k += 2) {
			(*dst)[off + k] = data[k + 1];
			(*dst)[off + k + 1] = data[k];
		}
	}
}

/* pcm1 を OKI #0（0x0A0000）へ、pcm2 を OKI #1（0x0C0000）へ */
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

/* バス読込 */
uint8_t CHardAc::DecoRead8(uint32_t phys)
{
	phys &= 0x1fffffu;
	if (decoCpuKind_ == 8) {
		/* MAME midres_s_map: ROM 000000-00FFFF、YM3812 108000、YM2203 118000、
		   OKI 130000、latch 138000、RAM 1F0000。 */
		if (phys <= 0x00ffffu) {
			if (decoRom_ && phys < decoRomSize_) return decoRom_[phys];
			return 0xff;
		}
		if (phys >= 0x108000u && phys <= 0x108001u)
			return chip_ ? chip_->ReadStatus() : 0xff;
		if (phys >= 0x118000u && phys <= 0x118001u) {
			if (!chip2_) return 0xff;
			return (phys & 1u) ? chip2_->ReadData() : chip2_->ReadStatus();
		}
		if (phys >= 0x130000u && phys <= 0x130001u)
			return pcm_ ? pcm_->ReadStatus() : 0xff;
		if (phys >= 0x138000u && phys <= 0x138001u) {
			decoLatchReads_++;
			const uint8_t v = soundCmd_;
			soundCmdPending_ = 0;
			return v;
		}
		if (phys >= 0x1f0000u && phys <= 0x1f1fffu) {
			if (!decoRam_) return 0;
			return decoRam_[phys - 0x1f0000u];
		}
		return 0xff;
	}
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

/* バス書込 */
void CHardAc::DecoWrite8(uint32_t phys, uint8_t v)
{
	phys &= 0x1fffffu;
	if (decoCpuKind_ == 8) {
		if (phys >= 0x108000u && phys <= 0x108001u) {
			if (!chip_) return;
			if (!(phys & 1u)) {
				decoYm2151Addr_ = v;
				chip_->Write(0, v);
			} else {
				chip_->Write(1, v);
				opmWrites_ = CEmuChipYm3812WriteCount(chip_);
			}
			return;
		}
		if (phys >= 0x118000u && phys <= 0x118001u) {
			if (!chip2_) return;
			if (!(phys & 1u)) {
				decoYm2203Addr_ = v;
				chip2_->Write(0, v);
			} else {
				chip2_->Write(1, v);
			}
			return;
		}
		if (phys >= 0x130000u && phys <= 0x130001u) {
			if (!pcm_) return;
			pcm_->Write(0, v);
			decoOkiWrites_++;
			return;
		}
		if (phys >= 0x1f0000u && phys <= 0x1f1fffu) {
			if (!decoRam_) return;
			const unsigned off = phys - 0x1f0000u;
			decoRam_[off] = v;
			if (off >= 0x310u && off <= 0x31fu && v != 0)
				decoChanWrites_++;
			return;
		}
		return;
	}
	/* MPR1 が $F8 からずれると論理 $2xxx/$3xxx が ROM ページに着き失われる。音源作業 RAM オフセットを decoRam_ へミラー。 */
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
			/* MAME cninja: YM2151 CT ビット → OKI2 ROM バンク（256K ページ） */
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
		/* BGM 二分探索用にチャネル枠インストールを数える */
		if (off >= 0x310u && off <= 0x31fu && v != 0)
			decoChanWrites_++;
		return;
	}
}

/* IRQ 配送 */
void CHardAc::DecoSyncIrqs()
{
	if (m6502_ && decoCpuKind_ == 9) {
		/* matmania: ラッチ HOLD → IRQ。NMI は sound_nmi_enable + 約 900 Hz。AY に IRQ 線は無い。 */
		M6502SetInputLine(m6502_, M6502_LINE_IRQ,
			soundCmdPending_ ? M6502_ASSERT_LINE : M6502_CLEAR_LINE);
		if (sjNmiMask_ && cpuHz_ > 0) {
			const uint64_t period = (uint64_t)cpuHz_ / 900u;
			if (period && cpuCycles_ >= namcoNextVblank_) {
				namcoNextVblank_ = cpuCycles_ + period;
				M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_CLEAR_LINE);
				M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_ASSERT_LINE);
			}
		} else {
			M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_CLEAR_LINE);
		}
		return;
	}
	if (m6502_) {
		/* YM2151（Atari）/ YM3812（Deco）IRQ → M6502 IRQ。ラッチは NMI エッジ */
		int ymIrq = 0;
		if (chip_) {
			ymIrq = chip_->Irq() ? 1 : 0;
			if (!ymIrq) {
				const uint8_t st = chip_->ReadStatus();
				if (decoCpuKind_ == 5)
					ymIrq = (st & 0x03) ? 1 : 0; /* YM2151 タイマ A/B */
				else if (st & 0x80)
					ymIrq = 1; /* OPL IRQ 旗 */
			}
		}
		/* MAME dec8: YM2203 と YM3812/YM3526 の IRQ を input_merger ANY_HIGH → 6502 IRQ */
		if (!ymIrq && chip2_ && chip2_->Irq())
			ymIrq = 1;
		M6502SetInputLine(m6502_, M6502_LINE_IRQ,
			ymIrq ? M6502_ASSERT_LINE : M6502_CLEAR_LINE);
		if (!soundCmdPending_)
			M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_CLEAR_LINE);
		return;
	}
	if (!h6280_) return;
	if (decoCpuKind_ == 8) {
		/* midres: MPR はファームが 21bit 窓（108000/118000/1F0000）を載せる。cninja の MPR1=$F8 固定はしない。
		   YM3812 irq → IRQ1。ラッチは NMI（SetSoundCommand）。 */
		int ymIrq = 0;
		if (chip_) {
			ymIrq = chip_->Irq() ? 1 : 0;
			if (!ymIrq && (chip_->ReadStatus() & 0x80))
				ymIrq = 1;
		}
		H6280SetInputLine(h6280_, H6280_LINE_IRQ1,
			ymIrq ? H6280_ASSERT_LINE : H6280_CLEAR_LINE);
		H6280SetInputLine(h6280_, H6280_LINE_IRQ2, H6280_CLEAR_LINE);
		return;
	}
	/* cninja はブート後 MPR1 を再マップしない（常に RAM ページ $F8）。ずれると STA $2310 が ROM に当たり捨てられ、BGM チャネルがインストールされない。 */
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

/* バス読込 */
uint8_t CHardAc::DecoM6502Read8(uint16_t addr)
{
	/* karnov: RAM 0000-05FF、ラッチ 0800、YM2203 1000、YM3526 1800、ROM 8000。dec0: RAM 0000-07FF、YM2203 0800、YM3812 1000、ラッチ 3000、OKI 3800、ROM 8000。dec8: RAM 0000-05FF、YM2203 2000、YM3812 4000、ラッチ 6000、ROM 8000。actfancr: dec0 と同様だが ROM は 4000 から。atari sys1: RAM 0000-0FFF（ミラー 2000）、YM2151 1800、ラッチ 1810、ステータス 1820、POKEY 1870 stub、ROM 4000-FFFF。 */
	const int kind = decoCpuKind_;
	if (kind == 9) {
		if (addr <= 0x01ffu)
			return decoM6502Ram_[addr];
		if (addr == 0x2007u) {
			decoLatchReads_++;
			soundCmdPending_ = 0;
			if (m6502_)
				M6502SetInputLine(m6502_, M6502_LINE_IRQ, M6502_CLEAR_LINE);
			return soundCmd_;
		}
		if (addr >= 0x8000u) {
			if (decoRom_ && decoRomSize_ >= 0x10000u)
				return decoRom_[addr];
			const unsigned off = addr - 0x8000u;
			return (decoRom_ && off < decoRomSize_) ? decoRom_[off] : 0xff;
		}
		return 0xff;
	}
	if (kind == 5) {
		/* RAM 0000-0FFF ミラー 2000。I/O 1800-18xx ミラー 3800。ROM 4000+。addr&0x1fff だけでは畳まない — $8000 が RAM にエイリアスする。 */
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
				/* Atari Sys1 / JSA-I rdio: bit4 = 音声準備（アクティブ Low → アイドルで 0）、bit6 = メイン→音源準備（アクティブ Low）、bits 2-3 は High 固定。NMI はラッチ読前に bit4 クリアを待つ。 */
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
			/* 64K イメージは CPU 空間で線形。32K イメージは $8000 に居る */
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
	/* 64K 音源 ROM（triothep）: 線形マップで reset@$4000 が動く。32K イメージ（actfancr/cobracom）: $8000+ のみ。 */
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

/* バス書込 */
void CHardAc::DecoM6502Write8(uint16_t addr, uint8_t v)
{
	const int kind = decoCpuKind_;
	if (kind == 9) {
		if (addr <= 0x01ffu) { decoM6502Ram_[addr] = v; return; }
		/* MAME ay8910 data_address_w: even=data, odd=address。CEmuChipAy は逆（even=addr）。 */
		if (addr == 0x2000u || addr == 0x2001u) {
			if (!chip_) return;
			if (addr & 1u) chip_->Write(0, v);
			else chip_->Write(1, v);
			opmWrites_++;
			return;
		}
		if (addr == 0x2002u || addr == 0x2003u) {
			if (!chip2_) return;
			if (addr & 1u) chip2_->Write(0, v);
			else chip2_->Write(1, v);
			opmWrites_++;
			return;
		}
		if (addr == 0x2004u)
			return; /* DAC 未接続。AY BGM のみ */
		if (addr == 0x2005u) {
			sjNmiMask_ = (v & 1u) ? 1 : 0;
			return;
		}
		return;
	}
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
				/* /WRIO — バンク + YM リセット bit0。自己診断エコー用に bit7 を残す */
				atariJsaIo_ = v;
				return;
			}
			if (io == 0x1824u || io == 0x2806u || io == 0x1826u) {
				/* /IRQACK（Sys1 ミラー）— ソフト YM IRQ ラッチを落とす */
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

/* バス読込 */
static uint8_t DecoM6502BusRead(void* ctx, uint16_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->DecoM6502Read8(addr) : 0xff;
}

/* バス書込 */
static void DecoM6502BusWrite(void* ctx, uint16_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->DecoM6502Write8(addr, data);
}

static mc6809byte__t NamcoM6809BusRead(mc6809__t* cpu, mc6809addr__t addr, bool iscode);
static void NamcoM6809BusWrite(mc6809__t* cpu, mc6809addr__t addr, mc6809byte__t data);
static void NamcoM6809BusFault(mc6809__t* cpu, mc6809fault__t fault);

/* データを載せる */
int CHardAc::LoadRomsDeco(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if ((!h6280_ && !m6502_ && !namcoM6809_) || !fs || !ge) return 0;
	if (decoRom_) { free(decoRom_); decoRom_ = NULL; decoRomSize_ = 0; }
	if (!decoRam_) {
		decoRam_ = (uint8_t*)malloc(0x2000);
		if (!decoRam_) return 0;
	}
	memset(decoRam_, 0, 0x2000);
	memset(decoM6502Ram_, 0, sizeof(decoM6502Ram_));

	if (decoCpuKind_ == 9) {
		decoRom_ = (uint8_t*)calloc(1, 0x10000);
		if (!decoRom_) return 0;
		decoRomSize_ = 0x10000u;
		int placed = 0;
		unsigned next = 0x8000u;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0
				&& _stricmp(r->type, "code") != 0 && _stricmp(r->type, "cpu") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || sz < 0x2000u || sz > 0x8000u) continue;
			unsigned at = (r->offset >= 0x8000 && r->offset < 0x10000) ? (unsigned)r->offset : next;
			if (at + sz > 0x10000u) sz = 0x10000u - at;
			memcpy(decoRom_ + at, data, sz);
			next = at + sz;
			if (next < 0x8000u) next = 0x8000u;
			placed++;
		}
		if (!placed) {
			int n16 = 0, i16[4];
			int n8 = 0, i8[8];
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (sz == 0x4000u && d[0] == 0x4cu && n16 < 4)
					i16[n16++] = i;
				else if (sz == 0x2000u && n8 < 8)
					i8[n8++] = i;
			}
			if (n16 >= 2) {
				memcpy(decoRom_ + 0x8000, fs->files[i16[0]].data, 0x4000);
				memcpy(decoRom_ + 0xc000, fs->files[i16[1]].data, 0x4000);
				placed = 2;
			} else if (n16 == 1) {
				memcpy(decoRom_ + 0x8000, fs->files[i16[0]].data, 0x4000);
				memcpy(decoRom_ + 0xc000, fs->files[i16[0]].data, 0x4000);
				placed = 1;
			} else if (n8 >= 2) {
				unsigned at = 0x8000u;
				for (int k = 0; k < n8 && at < 0x10000u; k++) {
					unsigned n = fs->files[i8[k]].size;
					if (at + n > 0x10000u) n = 0x10000u - at;
					memcpy(decoRom_ + at, fs->files[i8[k]].data, n);
					at += n;
					placed++;
				}
			}
		}
		if (!placed) { free(decoRom_); decoRom_ = NULL; decoRomSize_ = 0; return 0; }
		sjNmiMask_ = 0;
		namcoNextVblank_ = 0;
		soundCmd_ = 0;
		soundCmdPending_ = 0;
		opmWrites_ = 0;
		cpuCycles_ = 0;
		if (chip_) chip_->Reset();
		if (chip2_) chip2_->Reset();
		if (m6502_) {
			M6502SetBus(m6502_, this, DecoM6502BusRead, DecoM6502BusWrite);
			M6502SetDecrypt(m6502_, 0);
			M6502Reset(m6502_);
		}
		return 1;
	}

	/* カタログ "code"/"sub"/"audiocpu" の 64K メンバを優先。無ければ zip を採点 */
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

	/* OKI サンプル ROM（HuC6280 / dec0） */
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

	if (namcoM6809_ && decoCpuKind_ >= 6) {
		mc6809__t* cpu = NamcoCpuRaw(namcoM6809_);
		cpu->user = this;
		cpu->read = NamcoM6809BusRead;
		cpu->write = NamcoM6809BusWrite;
		cpu->fault = NamcoM6809BusFault;
		mc6809_reset(cpu);
		cpu->nmi_armed = true;
		return 1;
	}
	if (m6502_) {
		M6502SetBus(m6502_, this, DecoM6502BusRead, DecoM6502BusWrite);
		/* oscar/srdarwin/ghostb: DECO 222。平文 cobracom は 78 D8、暗号は 78 B8（CLD→CLV）。 */
		if (decoRom_ && decoRomSize_ >= 2u
			&& decoRom_[0] == 0x78u && decoRom_[1] == 0xB8u)
			M6502SetDecrypt(m6502_, 1);
		else if (ge && CEmuAcOptionValue(ge, "decrypt", 0))
			M6502SetDecrypt(m6502_, 1);
		M6502Reset(m6502_);
		return 1;
	}
	CEmuH6280BusSetDeco(this);
	CEmuH6280BusAttach(h6280_, this);
	H6280Reset(h6280_);
	return 1;
}

/* データを載せる */
int CHardAc::LoadRomsAtariSys1(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* カタログ code ROM をオフセットに載せて 64K 6502 イメージを組む（marble 8000/C000、indytemp 4000/8000/C000、…）。 */
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
		/* フォールバック: zip メンバをサイズ昇順で 8000+ へパック */
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

/* バス書込 */
static void NamcoM6809BusWrite(mc6809__t* cpu, mc6809addr__t addr, mc6809byte__t data)
{
	CHardAc* hw = (CHardAc*)(cpu ? cpu->user : NULL);
	if (hw) hw->NamcoM6809Write8((uint16_t)addr, (uint8_t)data);
}

/* NamcoM6809BusFault の実装 */
static void NamcoM6809BusFault(mc6809__t* cpu, mc6809fault__t fault)
{
	if (cpu) longjmp(cpu->err, (int)fault);
}

/* ファイル静的。CHardAc 配置を事前ビルド smoke obj に対し ABI 安定に保つ */
static uint8_t s_acNamcoMailFlag = 0x60;

/* CHardAc::SetNamcoMailFlag の実装 */
void CHardAc::SetNamcoMailFlag(uint8_t f)
{
	s_acNamcoMailFlag = f ? f : 0x60u;
}

/* CHardAc::NamcoMailFlag の実装 */
uint8_t CHardAc::NamcoMailFlag() const
{
	return s_acNamcoMailFlag ? s_acNamcoMailFlag : 0x60u;
}

/* バンク済みポインタ先頭バイトの Sys2 曲表レコード型を覗く。型 $20 = bit6 メールボックス BGM 経路。他の型は bit6 無しステータスが要る。 */
static int CEmuAcSys2SongInfo(const uint8_t* rom, unsigned romSize, unsigned songLo,
	unsigned* recAddr, uint8_t* hdr, unsigned hdrCap)
{
	if (recAddr) *recAddr = 0;
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
	if (recAddr) *recAddr = addr;
	if (hdr && hdrCap) {
		const unsigned n = (addr + hdrCap <= romSize) ? hdrCap : (romSize - addr);
		memcpy(hdr, rom + addr, n);
		if (n < hdrCap) memset(hdr + n, 0, hdrCap - n);
	}
	return (int)rom[addr];
}

/* CHardAc::Sys2SongRecType の実装 */
int CHardAc::Sys2SongRecType(unsigned songLo) const
{
	return CEmuAcSys2SongInfo(soundRom_, soundRomSize_, songLo, NULL, NULL, 0);
}

/* CHardAc::Sys2SongInfo の実装 */
int CHardAc::Sys2SongInfo(unsigned songLo, unsigned* recAddr, uint8_t* hdr, unsigned hdrCap) const
{
	return CEmuAcSys2SongInfo(soundRom_, soundRomSize_, songLo, recAddr, hdr, hdrCap);
}

/* PCM／コードバンク */
void CHardAc::NamcoM6809SetBank(unsigned bank)
{
	namcoBank_ = bank;
}

/* バス読込 */
uint8_t CHardAc::NamcoM6809Read8(uint16_t addr)
{
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && snkMapKind_ >= 4) {
		if (addr <= 0x0fffu)
			return mem_[addr];
		if (snkMapKind_ == 4) {
			/* ddragon_sound_map: ラッチ @1000、ADPCM 状態 @1800、YM2151 @2800 */
			if (addr == 0x1000u) {
				soundCmdPending_ = 0;
				if (namcoM6809_) NamcoCpuRaw(namcoM6809_)->irq = false;
				return soundCmd_;
			}
			if (addr == 0x1800u)
				return 0x00; /* MSM idle。busy 待ちを通す */
			if (addr == 0x2800u || addr == 0x2801u)
				return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0;
		} else {
			/* renegade_sound_map: ラッチ @1000-17FF、YM3526 @2800-2FFF、3800-7FFF nopr */
			if (addr >= 0x1000u && addr <= 0x17ffu) {
				soundCmdPending_ = 0;
				if (namcoM6809_) NamcoCpuRaw(namcoM6809_)->irq = false;
				return soundCmd_;
			}
			if (addr >= 0x2800u && addr <= 0x2fffu)
				return chip_ ? chip_->ReadStatus() : 0;
			if (addr >= 0x3800u && addr <= 0x7fffu)
				return 0xff;
		}
		if (addr >= 0x8000u) {
			const unsigned off = addr - 0x8000u;
			return (soundRom_ && off < soundRomSize_) ? soundRom_[off] : 0xff;
		}
		return 0xff;
	}
	if (board_ == CEMU_AC_BOARD_DECO && decoCpuKind_ >= 6) {
		if (addr <= 0x1fffu)
			return decoRam_ ? decoRam_[addr] : 0;
		if (decoCpuKind_ == 6) {
			/* brkthru: YM3526 @$2000、ラッチ @$4000、YM2203 @$6000 */
			if (addr == 0x2000u || addr == 0x2001u)
				return chip_ ? chip_->ReadStatus() : 0;
			if (addr == 0x4000u) {
				decoLatchReads_++;
				soundCmdPending_ = 0;
				return soundCmd_;
			}
			if (addr == 0x6000u || addr == 0x6001u)
				return chip2_ ? chip2_->ReadStatus() : 0;
		} else {
			/* exprraid: YM2203 @$2000、YM3526 @$4000、ラッチ @$6000 */
			if (addr == 0x2000u || addr == 0x2001u)
				return chip2_ ? chip2_->ReadStatus() : 0;
			if (addr == 0x4000u || addr == 0x4001u)
				return chip_ ? chip_->ReadStatus() : 0;
			if (addr == 0x6000u) {
				decoLatchReads_++;
				soundCmdPending_ = 0;
				return soundCmd_;
			}
		}
		if (addr >= 0x8000u) {
			const unsigned off = addr - 0x8000u;
			return (decoRom_ && off < decoRomSize_) ? decoRom_[off] : 0xff;
		}
		return 0xff;
	}
	/* Mappy / Dig Dug 2 / Super Pac-Man: M6809 + namco_15xx amap @0000-03FF、ラッチ @2000、ROM @E000/F000。 */
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		if (addr <= 0x03ffu)
			return chip_ ? CEmuChipC30Read(chip_, addr) : 0xff;
		if (addr >= 0xe000u) {
			if (!soundRom_ || !soundRomSize_) return 0xff;
			/* MAME: $E000 に 8K。4K タイトル（superpac/pacnpal）は $F000 に載せ $E000 を空ける — 4K をミラーし E000 フェッチを安全に。 */
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
		/* RAM テスト後ファームは CLR $5000/$5001 して JMP $D004。MCU は $5000=$A6 を保ち LDD が生存チェックを通すと期待される。 */
		if (off == 0u && namcoCus30_[2] == 0xa6u)
			return 0xa6;
		/* VBlank ラッチ: IRQ ハンドラは $53F3 を INC するが、レベル／タイミング癖で $D02A のメイン待ちが飢える。保留として読めるように保つ。 */
		if (off == 0x3f3u)
			return 1;
		if (pcm_ && off >= 0x100u && off < 0x140u) {
			uint8_t regs[0x40];
			if (pcm_->GetRegSnapshot(regs, sizeof(regs)) > 0)
				return regs[off - 0x100u];
		}
		return namcoCus30_[off];
	}
	/* Sys2: C140 @ $5000/$6000（8bit）、DPRAM メールボックス @ $7000 */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_
		&& ((addr >= 0x5000u && addr <= 0x5fffu)
			|| (addr >= 0x6000u && addr <= 0x6fffu))) {
		return CEmuChipC140Read(pcm_, addr & 0x1ffu);
	}
	if (addr >= 0x7000u && addr <= 0x7fffu) {
		const unsigned off = addr & 0x7ffu;
		/* berabohm 級 Sys1: MCU 生存は $7000=$A6。$7001 は 0 のまま */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 && off == 0u)
			return 0xa6;
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS1 && off == 1u)
			return 0x00;
		/* Sys2 起動: MCU ハンドシェイク後、Sys1 $5000 と同様に $703A=$A6 を保持 */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2
			&& off == 0x3au && namcoTriRam_[0x3a] == 0xa6u)
			return 0xa6;
		/* fourtrax: FIRQ CMPA $75FF,#$65。D922 は D95B が読む前に $7100 をゼロ — ラッチ済みコマンドと MCU バイトを重ねる。 */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && (namcoMailOff_ & 0x8000u)) {
			if (off == 0x5ffu)
				return 0x65;
			if (off == (namcoMailOff_ & 0x7ffu))
				return (uint8_t)(soundCmd_ & 0xffu);
		}
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

/* バス書込 */
void CHardAc::NamcoM6809Write8(uint16_t addr, uint8_t v)
{
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && snkMapKind_ >= 4) {
		if (addr <= 0x0fffu) {
			mem_[addr] = v;
			return;
		}
		if (snkMapKind_ == 4) {
			if (addr == 0x2800u || addr == 0x2801u) {
				if (!chip_) return;
				if (!(addr & 1u)) { namcoYmAddr_ = v; chip_->Write(0, v); }
				else { chip_->Write(1, v); opmWrites_++; }
				return;
			}
			return; /* 3800-3807 ADPCM nop */
		}
		if (addr >= 0x2800u && addr <= 0x2fffu) {
			if (!chip_) return;
			if (!(addr & 1u)) { namcoYmAddr_ = v; chip_->Write(0, v); }
			else { chip_->Write(1, v); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
			return;
		}
		return; /* 1800/2000/3000 ADPCM nop */
	}
	if (board_ == CEMU_AC_BOARD_DECO && decoCpuKind_ >= 6) {
		if (addr <= 0x1fffu) {
			if (decoRam_) decoRam_[addr] = v;
			return;
		}
		if (decoCpuKind_ == 6) {
			if (addr == 0x2000u || addr == 0x2001u) {
				if (!chip_) return;
				if (!(addr & 1u)) { decoYm2151Addr_ = v; chip_->Write(0, v); }
				else { chip_->Write(1, v); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
				return;
			}
			if (addr == 0x6000u || addr == 0x6001u) {
				if (!chip2_) return;
				if (!(addr & 1u)) { decoYm2203Addr_ = v; chip2_->Write(0, v); }
				else chip2_->Write(1, v);
				return;
			}
		} else {
			if (addr == 0x2000u || addr == 0x2001u) {
				if (!chip2_) return;
				if (!(addr & 1u)) { decoYm2203Addr_ = v; chip2_->Write(0, v); }
				else chip2_->Write(1, v);
				return;
			}
			if (addr == 0x4000u || addr == 0x4001u) {
				if (!chip_) return;
				if (!(addr & 1u)) { decoYm2151Addr_ = v; chip_->Write(0, v); }
				else { chip_->Write(1, v); opmWrites_ = CEmuChipYm3812WriteCount(chip_); }
				return;
			}
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		if (addr <= 0x03ffu) {
			if (chip_) chip_->Write(addr, v);
			opmWrites_++;
			return;
		}
		/* メインラッチ: $2000/$6000 = IRQ ACK（MAME）。$2003/$2007 = いくつかの Mappy 期セットの sound_enable（LS259 の Q3/Q7）。 */
		if (addr == 0x2000u || addr == 0x6000u) {
			if (namcoM6809_) NamcoCpuRaw(namcoM6809_)->irq = false;
			namcoIrqAssert_ = 0;
			return;
		}
		if (addr == 0x2003u || addr == 0x2007u || addr == 0x6003u || addr == 0x6007u) {
			CEmuChipC30SetEnable(chip_, (v & 1u) ? 1 : 1); /* レベル: どの書でも許可 */
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
	/* Sys2: C140 @ $5000/$6000。DPRAM @ $7000（C140 ではない） */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_
		&& ((addr >= 0x5000u && addr <= 0x5fffu)
			|| (addr >= 0x6000u && addr <= 0x6fffu))) {
		pcm_->Write(addr & 0x1ffu, v);
		return;
	}
	if (addr >= 0x7000u && addr <= 0x7fffu) {
		const unsigned off = addr & 0x7ffu;
		namcoTriRam_[off] = v;
		/* burnforc: ROM チェック失敗は $77FE=$FF を投稿し、$77FD の MCU が $A6 のち $6A になるのを待つ（C65/C68 DPRAM ハンドシェイク）。 */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && off == 0x7feu && v == 0xffu)
			namcoTriRam_[0x7fd] = 0xa6;
		return;
	}
	if (addr >= 0x8000u && addr <= 0x9fffu) {
		namcoWorkRam_[addr & 0x1fffu] = v;
		return;
	}
	if (addr == 0xc000u || addr == 0xc001u) {
		/* MAME namcos1/2: バンク = data >> 4 */
		NamcoM6809SetBank((unsigned)(v >> 4));
		return;
	}
	if (addr == 0xd001u) {
		/* MAME ではウォッチドッグ nop。ソフト MCU は CPU が A6 を A に標本化したあと $77FD を A6→6A に進める（CMPA はまだレジスタ A を見る）。 */
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

/* IRQ 配送 */
void CHardAc::NamcoM6809SyncIrqs()
{
	if (!namcoM6809_) return;
	mc6809__t* cpu = NamcoCpuRaw(namcoM6809_);
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && snkMapKind_ >= 4) {
		int ymIrq = 0;
		if (chip_) {
			ymIrq = chip_->Irq() ? 1 : 0;
			if (!ymIrq && (chip_->ReadStatus() & 0x80))
				ymIrq = 1;
		}
		cpu->firq = ymIrq ? true : false;
		if (soundCmdPending_ && !cpu->cc.i)
			cpu->irq = true;
		else if (!soundCmdPending_)
			cpu->irq = false;
		else if (namcoIrqAssert_ && !cpu->cc.i) {
			cpu->irq = true;
			namcoIrqAssert_ = 0;
		}
		return;
	}
	if (board_ == CEMU_AC_BOARD_DECO && decoCpuKind_ >= 6) {
		int ymIrq = 0;
		if (chip_) {
			ymIrq = chip_->Irq() ? 1 : 0;
			if (!ymIrq && (chip_->ReadStatus() & 0x80))
				ymIrq = 1;
		}
		cpu->irq = ymIrq ? true : false;
		return;
	}
	g_namcoSync++;
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && wsgMappy_) {
		/* I がクリアされたら遅延曲 IRQ（SetSoundCommand は I=1 のブート中に撃っても失われてはいけない） */
		if (namcoIrqAssert_ && !cpu->cc.i) {
			const uint16_t irqv = (uint16_t)(((unsigned)NamcoM6809Read8(0xfff8) << 8)
				| NamcoM6809Read8(0xfff9));
			const uint16_t rstv = (uint16_t)(((unsigned)NamcoM6809Read8(0xfffe) << 8)
				| NamcoM6809Read8(0xffff));
			if (irqv != rstv)
				cpu->irq = true;
			namcoIrqAssert_ = 0;
		}
		/* MAME mappy: sub_irq_mask がセットのとき vblank が音源 CPU IRQ0 をアサート。約 60 Hz ソフトアサート。$2000 のラッチ書でクリア。 */
		if ((uint64_t)cpu->cycles >= namcoNextVblank_) {
			namcoNextVblank_ = (uint64_t)cpu->cycles + (uint64_t)cpuHz_ / 60u;
			g_namcoVblank++;
			/* superpac: IRQ ベクタ == RESET。vblank パルスは毎フレームブート（LDS #$0400）に再入し、$40 曲 id が定着しない。 */
			const uint16_t irqv = (uint16_t)(((unsigned)NamcoM6809Read8(0xfff8) << 8)
				| NamcoM6809Read8(0xfff9));
			const uint16_t rstv = (uint16_t)(((unsigned)NamcoM6809Read8(0xfffe) << 8)
				| NamcoM6809Read8(0xffff));
			if (!cpu->cc.i && irqv != rstv)
				cpu->irq = true;
		}
		return;
	}
	/* YM2151 タイマ → FIRQ / Sys2 C140 INT1 → FIRQ。ソースがライブの間 FIRQ 線を保持 — SyncIrqs tick 毎にクリア（エッジのみ）すると mc6809 が F を標本化する前にパルスを失った。 */
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
	/* I がクリアされたら遅延コマンド IRQ */
	if (namcoIrqAssert_ && !cpu->cc.i) {
		cpu->irq = true;
		namcoIrqAssert_ = 0;
	}
	/* VBlank / 周期 IRQ。MAME Sys2: irq0_line_hold @ 2*60 Hz。Sys1 は 60 のまま。 */
	if ((uint64_t)cpu->cycles >= namcoNextVblank_) {
		const unsigned hz = (board_ == CEMU_AC_BOARD_NAMCO_SYS2) ? 120u : 60u;
		namcoNextVblank_ = (uint64_t)cpu->cycles + (uint64_t)cpuHz_ / hz;
		g_namcoVblank++;
		namcoWorkRam_[0x119] = 0x0e;
		/* Pacmania は DP+$1C（しばしば $901C）を poll。blazer は $8119 */
		namcoWorkRam_[0x11c] = 0x0e;
		namcoWorkRam_[0x101c & 0x1fff] = 0x0e;
		if (!cpu->cc.i)
			cpu->irq = true;
		/* Sys2: FIRQ は C140 INT1 を優先。タイマ未許可のときだけ vblank パルスへフォールバックし、ブートが ANDCC #$BF / アイドルに届くように。 */
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

/* データを載せる */
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
	/* Sys2 メールボックス hi: bit6 セット（0x40/0x60）が assault D27D チャネル init（$902B ゲートを書く）を選ぶ。bit6 クリアは空の $9352 経路 → SILENT。 */
	s_acNamcoMailFlag = 0x60;
	s_acSys2TwinMail = 0;

	/* ---- Mappy 期 WSG6809: 4K/8K 音源 CPU + 256B 波形 PROM ---- */
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
			/* スタックは $0400 直下 — digdug2 作業域は約 $03C0 まで */
			cpu->S.w = 0x03f0u;
			cpu->nmi_armed = true;
		}
		return 1;
	}

	/* カタログ audiocpu/sound/code を優先。無ければ最良の *s0* / *snd* メンバ。16K 整列の全音源バンク（s0+s1）を連結 — バンクスイッチ bits 4-6 が全領域の 16K 窓を選ぶ（MAME namcos1）。Sys2 dsaber/rthun2 は snd1 をオフセット 0x20000 に列挙。XML オフセットへ置くと穴が出ても 256KiB マップを保つ。 */
	{
		int placeCode = 0;
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2
			|| board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
			for (int i = 0; i < ge->romCount; i++) {
				if (ge->rom[i].offset > 0) { placeCode = 1; break; }
			}
		}
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0
				&& _stricmp(r->type, "code") != 0 && _stricmp(r->type, "cpu") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || sz < 0x4000u) continue;
			if (placeCode)
				CEmuAcPlacePcm(&soundRom_, &soundRomSize_,
					(unsigned)(r->offset < 0 ? 0 : r->offset), data, sz);
			else
				CEmuAcAppendPcm(&soundRom_, &soundRomSize_, data, sz);
		}
	}
	/* カタログが s0 だけでも兄弟 s1 を取る。Sys2 C68 期セットは 128KiB バンク 2 本（dsaber snd0+snd1）。64KiB 上限はバンク 8+ を FF のまま。 */
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
		/* s0 が s1 より先になるよう名順を優先 */
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
	/* Sys2: あれば C140 ボイス ROM も載せる。*voi* / カタログ pcm を優先。audiocpu イメージをサンプルとして飲み込まない。 */
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2 && pcm_) {
		if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
		/* パス 1: カタログ voice/pcm 項目。XML オフセットを尊重し voi2 が 0x80000 に着く（valkyrie 風ミラーが 1MiB 窓を埋める）。 */
		{
			int placePcm = 0;
			for (int i = 0; i < ge->romCount; i++) {
				if (ge->rom[i].offset > 0) { placePcm = 1; break; }
			}
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
				if (placePcm)
					CEmuAcPlacePcm(&pcmRom_, &pcmRomSize_,
						(unsigned)(r->offset < 0 ? 0 : r->offset), data, sz);
				else
					CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, data, sz);
			}
		}
		/* パス 2: voi/c140 名の zip メンバ（snd/s0 音源 CPU は飛ばす） */
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

	/* Sys2 メールボックス: hoot <option name="codeaddr"> はホスト書番地（finallap $7100、burnforc $7110、dsaber $7111）。無ければ先頭 128KiB で最も多い LDX #$71xx へフォールバック。 */
	namcoMailOff_ = 0x100;
	if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
		const unsigned opt = CEmuAcOptionValue(ge, "codeaddr", 0);
		if (opt >= 0x7000u && opt <= 0x77ffu) {
			namcoMailOff_ = opt & 0x7ffu;
		} else if (soundRom_ && soundRomSize_ >= 0x4000u) {
			unsigned hits[256];
			memset(hits, 0, sizeof(hits));
			const unsigned n = soundRomSize_ < 0x20000u ? soundRomSize_ : 0x20000u;
			for (unsigned i = 0; i + 2u < n; i++) {
				if (soundRom_[i] == 0x8eu && soundRom_[i + 1u] == 0x71u)
					hits[soundRom_[i + 2u]]++;
			}
			unsigned bestLo = 0, bestN = 0;
			for (unsigned lo = 0; lo < 256u; lo++) {
				if (hits[lo] > bestN) {
					bestN = hits[lo];
					bestLo = lo;
				}
			}
			if (bestN)
				namcoMailOff_ = 0x100u + bestLo;
		}
		if (ge && ge->archive
			&& (_stricmp(ge->archive, "assault") == 0
				|| _stricmp(ge->archive, "dirtfoxj") == 0
				|| _stricmp(ge->archive, "finallap") == 0
				|| _stricmp(ge->archive, "mirninja") == 0
				|| _stricmp(ge->archive, "sws92") == 0
				|| _stricmp(ge->archive, "ordyne") == 0
				|| _stricmp(ge->archive, "metlhawk") == 0
				|| _stricmp(ge->archive, "finalap3") == 0
				|| _stricmp(ge->archive, "gollygho") == 0
				|| _stricmp(ge->archive, "luckywld") == 0
				|| _stricmp(ge->archive, "suzuka8h") == 0
				|| _stricmp(ge->archive, "solvalou") == 0
				|| _stricmp(ge->archive, "cybsled") == 0))
			s_acSys2TwinMail = 1;
		/* fourtrax C68: $75FF==$65（MCU）でないと FIRQ は RTI。CHardAc を増やさず namcoMailOff_ の上位ビットがその経路をタグ。 */
		if (CEmuAcOptionValue(ge, "foutrax", 0)) {
			namcoMailOff_ |= 0x8000u;
			namcoTriRam_[0x5ff] = 0x65;
			namcoTriRam_[0x50f] = 0x01;
		}
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
		/* 作業 RAM は $8000-$9FFF のみ（MAME namcos1/2）。$A000 は未マップ — そこの FIRQ/IRQ push は失われ Sys2 が SILENT。 */
		cpu->S.w = 0x9ff0u;
		cpu->nmi_armed = true;
		/* Sys1 MCU ハンドシェイク変種:
		   - dspirit/pacmania: LDD $5000。A または B == $A6（$5001=$A6）
		   - berabohm/shadowld/blastoff/mmaze: LDA $7000。A == $A6、その後 $7001 == 0 まで待つ（TRI-RAM 基点のソフト MCU メールボックス）。 */
		namcoCus30_[0] = 0;
		namcoCus30_[1] = 0xa6;
		namcoCus30_[2] = 0xa6; /* CLR 後の再チェック用に $5000 生存を粘着 */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS1) {
			namcoTriRam_[0] = 0xa6;
			namcoTriRam_[1] = 0x00;
		}
		/* Sys2（finallap）: LDA $703A / CMPA #$A6 スピン。その後 TST $703B==0 */
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
			namcoTriRam_[0x3a] = 0xa6;
			namcoTriRam_[0x3b] = 0x00;
		}
	}
	return 1;
}

/* H8 基板用の C352 バイトレーン 1 本。レジスタファイルは実機で読み戻す（MAME c352_device::read）。Namco ドライバは空きボイスの旗を走査し更新前にボイス音量を読む。H8 はビッグエンディアンなので偶数番地が上位バイト — M37702 側と逆。 */
uint8_t CHardAc::C352ReadLane(unsigned off) const
{
	const uint16_t w = CEmuChipC352Read(chip_, off >> 1);
	return (off & 1u) ? (uint8_t)(w & 0xff) : (uint8_t)(w >> 8);
}

/* バス読込 */
uint8_t CHardAc::H8Read8(uint32_t addr)
{
	addr &= 0xffffffu;
	if (h8Rom_ && addr < h8RomSize_)
		return h8Rom_[addr];
	if (h8MapKind_ == 0) {
		/* System 12 */
		if (addr >= 0x080000u && addr < 0x090000u && h8Shared_) {
			const unsigned o = addr - 0x080000u;
			/* ホストは +0x4050 で busy を自動 ACK し、ドライバ待ちループが抜ける */
			if (o == 0x4050u) return 0;
			return h8Shared_[o];
		}
		if (addr >= 0x280000u && addr < 0x288000u && chip_)
			return C352ReadLane(addr - 0x280000u);
		if (addr >= 0x300000u && addr < 0x300040u)
			return 0xff;
		return 0xff;
	}
	/* ND-1 */
	if (addr >= 0x200000u && addr < 0x210000u && h8Shared_) {
		const unsigned o = addr - 0x200000u;
		if (o == 0x4050u) return 0; /* Sys12 と同様に busy を自動 ACK */
		return h8Shared_[o];
	}
	if (addr >= 0xa00000u && addr < 0xa08000u && chip_)
		return C352ReadLane(addr - 0xa00000u);
	/* DSW / 入力 — オープンバス High で POST がハングしない */
	if (addr >= 0xc00000u && addr < 0xc00040u)
		return 0xff;
	return 0xff;
}

/* バス書込 */
void CHardAc::H8Write8(uint32_t addr, uint8_t v)
{
	addr &= 0xffffffu;
	if (h8MapKind_ == 0) {
		if (addr >= 0x080000u && addr < 0x090000u && h8Shared_) {
			h8Shared_[addr - 0x080000u] = v;
			return;
		}
		if (addr >= 0x280000u && addr < 0x288000u && chip_) {
			/* C352 は 16bit デバイスだが H8 はしばしば片レーンへ MOV.B。偶数だけのラッチは対になる奇数書が無い vol/freq 上位バイトを落とした。MAME mem_mask のようにマージ。 */
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

/* CHardAc::M37702InjectSong の実装 */
void CHardAc::M37702InjectSong(uint16_t cmd)
{
	/* 本物 M37702: NA1 メールボックス + IRQ0。Sys11 共有 RAM ストローブ + IRQ0 */
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
	if (m37702MapKind_ == 1) {
		/* NA-1/NA-2: MCU $800 に 16bit メール枠 8 つ。実基板では 68000 が枠 4 を書いたときだけ MCU の IRQ0 が上がるので、コマンドはそこへ入る（MAME mcu_mailbox_w_68k）。 */
		m37702Mailbox_[0] = cmd;
		m37702Mailbox_[1] = (uint16_t)(0x4000u | (cmd & 0x3fffu));
		m37702Mailbox_[4] = cmd;
		if (m37702_) M37702SetInputLine(m37702_, M37710_LINE_IRQ0, M37702_HOLD_LINE);
		return;
	}
	if (!h8Shared_) return;
	/* C74、C75、C76 は同じ Namco 音源ドライバを走り、ホスト IF は共有 RAM の 1 基点から固定オフセット: C74 は MCU $5000、C75/C76 は $4000（2 ROM は他 $1000 ずれの同じコード）。その基点相対:
	     +$000  曲要求、bit 14 = 「メイン CPU が埋めた」
	     +$3FC  要求キューへのホスト書添字
	     +$3FE  MCU 読添字
	     +$400  （u16 先番地、u16 値）poke のキュー
	     +$480  $5A = 「メイン CPU は生きている」
	   マジックバイトは意図的にクリアのまま。セットすると全コマンドがメイン CPU からのキュー poke で来るモードになり、ここにはメイン CPU が無い。クリアのままドライバは +$000 の曲要求を自分で読む。それが CEmu の駆動。 */
	const uint16_t w = (uint16_t)(0x4000u | (cmd & 0x3fffu));
	if (m37702McuKind_) {
		const unsigned base = (m37702MapKind_ == 2) ? 0x1000u : 0x0000u;
		h8Shared_[base + 0x000u] = (uint8_t)(w & 0xff);
		h8Shared_[base + 0x001u] = (uint8_t)(w >> 8);
	} else {
		/* ND-1 と、ドライバが Namco C7x マスク ROM ではなくゲーム自身のデータ ROM に居る他の M37702 基板 */
		h8Shared_[0x0100] = (uint8_t)(w & 0xff);
		h8Shared_[0x0101] = (uint8_t)(w >> 8);
		h8Shared_[0x4050] = 0;
		h8Shared_[0x0000] = (uint8_t)(cmd & 0xff);
		h8Shared_[0x0001] = (uint8_t)(cmd >> 8);
		h8Shared_[0x0004] = 1;
		h8Shared_[0x0005] = (uint8_t)(cmd >> 8);
	}
	/* System 22 は Timer A0 から要求を poll するのでホスト IRQ 不要。System 11 / NB-1 は 60 Hz IRQ0 ハンドラから処理。 */
	if (m37702MapKind_ != 2 && m37702_)
		M37702SetInputLine(m37702_, M37710_LINE_IRQ0, M37702_HOLD_LINE);
}

/* バス読込 */
uint8_t CHardAc::M37702Read8(uint32_t addr)
{
	addr &= 0xffffffu;
	if (m37702MapKind_ == 1) {
		/* NA-1 C69 マップ（MAME namcona1_mcu_map） */
		if (addr >= 0x800u && addr <= 0xfffu) {
			const unsigned wi = ((unsigned)(addr - 0x800u) >> 1) & 7u;
			const uint16_t w = m37702Mailbox_[wi];
			return (addr & 1u) ? (uint8_t)(w >> 8) : (uint8_t)(w & 0xff);
		}
		if (addr >= 0x1000u && addr <= 0x1fffu && chip_) {
			const unsigned o = (unsigned)(addr - 0x1000u) & 0x1ffu;
			return CEmuChipC140Read(chip_, o);
		}
		/* $2000 は 68000 作業 RAM 先頭ページ、$200000 は全体をミラー。どちらもバイトスワップ（MAME na1mcu_shared_r）。 */
		if (addr >= 0x2000u && addr <= 0x2fffu && h8Shared_)
			return h8Shared_[((addr - 0x2000u) & 0xfffu) ^ 1u];
		if (addr >= 0x3000u && addr <= 0xafffu && m37702LocalRam_)
			return m37702LocalRam_[addr - 0x3000u];
		if (addr >= 0x200000u && addr <= 0x27ffffu && h8Shared_) {
			const unsigned o = (unsigned)(addr - 0x200000u);
			if (o < 0x10000u) return h8Shared_[o ^ 1u];
			return 0;
		}
		/* ポート／ADC はオープンバス High */
		return 0xff;
	}
	/* Sys11 C76 マップ。$C000-$FFFF はコアの int_rom 窓が担当 */
	if (h8Rom_ && addr >= 0x80000u && addr < 0x80000u + h8RomSize_)
		return h8Rom_[addr - 0x80000u];
	if (h8Rom_ && addr >= 0x200000u && addr < 0x200000u + h8RomSize_)
		return h8Rom_[addr - 0x200000u];
	if (h8Rom_ && addr >= 0x280000u && addr < 0x280000u + h8RomSize_)
		return h8Rom_[addr - 0x280000u];
	if (addr >= 0x4000u && addr <= 0xbfffu && h8Shared_)
		return h8Shared_[addr - 0x4000u];
	/* C352 レジスタファイルは読み戻す（MAME c352_device::read）。C7x ドライバはそれに依存: 空きボイスの旗を走査し更新前に現在音量を読む。この窓が 0 のままだと全ボイスがアイドル無音に見え、同じボイスを 1 ノートへ再割当し続け、KeyOn を永久ストローブし、音量も周波数も 1 つも書かなかった。 */
	if (addr >= 0x2000u && addr <= 0x2fffu && chip_) {
		const uint16_t w = CEmuChipC352Read(chip_, (unsigned)((addr - 0x2000u) >> 1));
		return (addr & 1u) ? (uint8_t)(w >> 8) : (uint8_t)(w & 0xff);
	}
	if (addr >= 0x2000u && addr <= 0x2fffu)
		return 0;
	if (addr >= 0x510000u && addr <= 0x51ffffu)
		return 0x80; /* fambowl オープンバス stub */
	return 0xff;
}

/* バス書込 */
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
			h8Shared_[((addr - 0x2000u) & 0xfffu) ^ 1u] = v;
			return;
		}
		if (addr >= 0x3000u && addr <= 0xafffu && m37702LocalRam_) {
			m37702LocalRam_[addr - 0x3000u] = v;
			return;
		}
		if (addr >= 0x200000u && addr <= 0x27ffffu && h8Shared_) {
			const unsigned o = (unsigned)(addr - 0x200000u);
			if (o < 0x10000u) h8Shared_[o ^ 1u] = v;
			return;
		}
		return;
	}
	/* Sys11 */
	if (addr >= 0x4000u && addr <= 0xbfffu && h8Shared_) {
		h8Shared_[addr - 0x4000u] = v;
		return;
	}
	/* MCU $2000-$2FFF の C352（MAME namcos11 c76_map）。M37702 はリトルエンディアンなので語ストアは偶数番地に下位半分を置く — このシャドウ論理が書かれたビッグエンディアン H8 と逆。半分を入れ替えると全レジスタで KeyOn ビットと周波数が誤バイトになり、ドライバが数百レジスタ書いても 1 ボイスも鳴らなかった。

	   レジスタ $202 は KeyOn/KeyOff トリガ。チップは語全体の書だけに反応（MAME は mem_mask が 0xffff でないと早期 return）。上位半分が来るまで下位半分を保持。 */
	if (addr >= 0x2000u && addr <= 0x2fffu && chip_) {
		const unsigned reg = (unsigned)((addr - 0x2000u) >> 1);
		uint16_t* shadow = &h8C352Shadow_[reg & 0x3ffu];
		if (!(addr & 1u)) {
			*shadow = (uint16_t)((*shadow & 0xff00u) | v);
			if (reg == 0x202u) return;
		} else {
			*shadow = (uint16_t)(((uint16_t)v << 8) | (*shadow & 0x00ffu));
		}
		chip_->Write(reg, *shadow);
		h8C352Writes_++;
	}
}

/* IRQ 配送 */
void CHardAc::SnkSetYmIrq(int which, int on)
{
	const uint8_t bit = (which == 0) ? 0x01u : 0x02u;
	if (on)
		snkStatus_ = (uint8_t)(snkStatus_ | bit);
	else
		snkStatus_ = (uint8_t)(snkStatus_ & (uint8_t)~bit);
}

/* CHardAc::SoftPcmInjectSong の実装 */
void CHardAc::SoftPcmInjectSong(uint16_t cmd)
{
	/* Model2A/3 SCSP と Hornet RF5C400: ラッチのみ — チップボイス poke 無し。本物ホストが配線されるまでそれらの stub の MixAdd/Render は無音。 */
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
}

/* CHardAc::H8InjectSong の実装 */
void CHardAc::H8InjectSong(uint16_t cmd)
{
	soundCmdWord_ = cmd;
	soundCmd_ = (uint8_t)(cmd & 0xff);
	soundCmdPending_ = 1;
	if (!h8Shared_) return;
	if (h8MapKind_ == 0) {
		/* Sys12 C76: ホスト曲ストローブは共有+0x0100 の BE 語で bit14 セット（0x40xx）。H8 は *0x080100 を poll、0x80xx として ACK、busy は +0x4050。 */
		const uint16_t w = (uint16_t)(0x4000u | (cmd & 0x3fffu));
		h8Shared_[0x0100] = (uint8_t)(w >> 8);
		h8Shared_[0x0101] = (uint8_t)(w & 0xff);
		h8Shared_[0x4050] = 0;
	} else {
		/* ND-1: Sys12 と同じ C76 風メールボックス — 共有+0x0100 の BE 語で bit14 セット（ROM 参照 00200100 + MOV.W #0x4000）。MCU が poll する低位ドアベル +0x00/+0x04/+0x50 も poke。 */
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
	/* System 12 は H8 外部 IRQ1 を画面 vblank にだけ配線。ホスト注入後にメールボックス poll を起こす本物ベクタとして使う。 */
	if (h8_) H8SetInputLine(h8_, H8_LINE_IRQ1, H8_ASSERT_LINE);
}

/* データを載せる */
int CHardAc::LoadRomsH8(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!h8_ || !fs || !ge) return 0;
	/* H8/3002 ダンプを同梱するのは System 12 / ND-1 だけ。Sys11/22 は C74/C76（M37702）。NA/NB は C69/C70（M37702）— それらに H8 を付けない。 */
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

	/* カタログ code/sub/audiocpu を優先。無ければ *11s* / *sub* を探す */
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

	/* 波形／ボイス PCM を C352 へ */
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

/* データを載せる */
int CHardAc::LoadRomsM37702(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* Sys11/22: 外部 sprog + C76/C74 内部 BIOS + C352。NA1/NB1: C69/C70 内部 BIOS @0xC000 + C219 + インターリーブ PCM。 */
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

	/* C74/C75/C76 プログラムは MCU のマスク ROM で、どのアーカイブも持たない。type=bios として列挙するのは pr1data.8k、バンク 0 に C7x イメージを持つ 512KB *データ* ROM。それを代用しても半成功止まり: System 22 では RAM ベクタ基点が違う Prop Cycle 改訂。Super System 22 / System 11 では $C000 イメージはローダで、Timer A0 ハンドラが JMP ($BFBA) で終わる — メイン CPU が本物シーケンサを上げたあと埋める共有 RAM ポインタ。下の本物マスク ROM があればその推測は不要。 */
	const unsigned char* maskRom = NULL;
	switch (m37702McuKind_) {
	case 74: maskRom = cemu_c74_rom; break;
	case 75: maskRom = cemu_c75_rom; break;
	case 76: maskRom = cemu_c76_rom; break;
	default: break;
	}
	m37702MaskRom_ = (m37702MapKind_ == 2);

	/* 内部 MCU BIOS: c69/c70/c74/c76.bin（16KB @ カタログオフセット 0xC000） */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || sz < 0x1000u) continue;
		int isInt = (r->offset == 0xc000) || CEmuAcContainsI(r->name, "c69")
			|| CEmuAcContainsI(r->name, "c70") || CEmuAcContainsI(r->name, "c74")
			|| CEmuAcContainsI(r->name, "c75") || CEmuAcContainsI(r->name, "c76");
		/* System 11/22 と NB-1 に c7x ダンプは無い。C74/C76 プログラムはそれら 29 セットが type=bios で持つ共有 pr1data.8k。線形バンク 0 イメージ: ファイルオフセット $FFD6-$FFFF が M37710 ベクタ表（reset = $C030）なので $C000-$FFFF が MCU ROM。マスク ROM が無い基板だけやる価値がある。 */
		const int isBios = (m37702MapKind_ != 1 && !maskRom
			&& _stricmp(r->type, "bios") == 0 && sz >= 0x10000u);
		if (isBios) isInt = 1;
		if (!isInt) continue;
		uint8_t* p = (uint8_t*)malloc(sz);
		if (!p) continue;
		memcpy(p, data, sz);
		m37702IntRom_ = p;
		m37702IntRomSize_ = isBios ? sz : (sz > 0x4000u ? 0x4000u : sz);
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
	if (maskRom && !m37702IntRomSize_) {
		uint8_t* p = (uint8_t*)malloc(0x4000u);
		if (p) {
			memcpy(p, maskRom, 0x4000u);
			m37702IntRom_ = p;
			m37702IntRomSize_ = 0x4000u;
		}
	}

	/* 外部プログラム（Sys11/22 sprog）— 16KB 内部 BIOS ダンプは飛ばす */
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
		/* 上でバンク 0 MCU イメージとして既に確保済み */
		if (m37702IntRomSize_ >= 0x10000u && _stricmp(r->type, "bios") == 0)
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
	/* System 22 は MCU ROM 1 本を MAME が窓 2 つで見せる: $200000 の region("mcu", 0) と $C000 の region("mcu", 0xC000)。上でバンク 0 イメージとして確保したあとも $200000 で応答する必要がある — さもなくばそこのドライバコードと表がオープンバスになり KeyOn まで届かない。 */
	if (!h8RomSize_ && m37702MapKind_ != 1 && m37702IntRomSize_ >= 0x10000u) {
		uint8_t* p = (uint8_t*)malloc(m37702IntRomSize_);
		if (p) {
			memcpy(p, m37702IntRom_, m37702IntRomSize_);
			h8Rom_ = p;
			h8RomSize_ = m37702IntRomSize_;
		}
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
	/* C69/C70 内部 ROM から走るのは NA-1 マップだけ。System 11/22 と NB-1 は外部音源プログラム（rr1data / teNsprog / nrN-spr0）をブートし c7x ダンプを同梱しないので、そこで要求すると MCU プログラムと波形 ROM が揃っていても 42 アーカイブが open で棄却された。 */
	if (m37702MapKind_ == 1 && !m37702IntRomSize_) return 0;
	if (!m37702IntRomSize_ && !h8RomSize_) return 0;

	/* PCM。NA-1 は 2 本の波形 ROM を ROM_LOAD16_BYTE 対（オフセット 0 と 1）として列挙しバイト毎インターリーブ。他ではオフセットは素のバイト位置で、オフセット 0 は「領域の最初の ROM」— System 22 / System 11 / NB-1 の大半。オフセット 1 項目が実際にあるかで決め、単独オフセット 0 ROM は来ない対の偶数半分として保留せず置く。 */
	if (pcmRom_) { free(pcmRom_); pcmRom_ = NULL; pcmRomSize_ = 0; }
	if (m37702MapKind_ == 1) {
		/* NA-1/NA-2: PCM 列挙はすべて ROM_LOAD16_BYTE。オフセット 0/1、100000/100001、200000/200001 は別のインターリーブ対。0/1 だけを対と見て pcmRom_ を free() すると残りが落ちた。 */
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
			const unsigned dest = (unsigned)off & ~1u;
			const unsigned odd = (unsigned)off & 1u;
			const unsigned need = dest + sz * 2u;
			uint8_t* p = (uint8_t*)realloc(pcmRom_, need > pcmRomSize_ ? need : pcmRomSize_);
			if (!p) continue;
			if (need > pcmRomSize_) {
				memset(p + pcmRomSize_, 0, need - pcmRomSize_);
				pcmRomSize_ = need;
			}
			pcmRom_ = p;
			for (unsigned n = 0; n < sz; n++)
				pcmRom_[dest + n * 2u + odd] = data[n];
		}
	} else {
		const unsigned char* evenData = NULL;
		const unsigned char* oddData = NULL;
		unsigned evenSz = 0, oddSz = 0;
		int interleaved = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (r->offset != 1) continue;
			if (_stricmp(r->type, "pcm") == 0 || _stricmp(r->type, "voice") == 0
				|| _stricmp(r->type, "sample") == 0 || _stricmp(r->type, "adpcm") == 0)
				interleaved = 1;
		}
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "pcm") != 0 && _stricmp(r->type, "voice") != 0
				&& _stricmp(r->type, "sample") != 0 && _stricmp(r->type, "adpcm") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			if (interleaved && r->offset == 0) { evenData = data; evenSz = sz; }
			else if (interleaved && r->offset == 1) { oddData = data; oddSz = sz; }
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
	/* コアの int_rom 窓は $C000 から番地。c69/c70 ダンプはちょうどその 16KB。pr1data.8k はファイルオフセット＝番地の線形バンク 0 イメージなので ROM 半分は +$C000 から。 */
	{
		const uint8_t* introm = m37702IntRom_;
		unsigned intsize = m37702IntRomSize_;
		if (introm && intsize >= 0x10000u) {
			introm += 0xc000u;
			intsize = 0x4000u;
		}
		M37702SetInternalRom(m37702_, introm, intsize);
	}
	/* System 22 は CPU 基板でポート P4 bit 4 を High にし、共有 C74 プログラムが I/O 基板入口ではなく音源ドライバ入口を取る。Super System 22 に第 2 MCU は無く、ラッチを読み戻すだけ。 */
	M37702SetPortIn(m37702_, 4, m37702MaskRom_ ? 0x10 : 0x00);
	M37702SetPort5Mirror(m37702_, m37702MapKind_ == 1);
	M37702Reset(m37702_);
	m37702Soft_ = 0; /* 実 CPU 接続済み */
	return 1;
}

static void CEmuAcLoadM68kPcmExtraCode(uint8_t** dst, unsigned* dstSize, CEmuZipFs* fs, const CEmuGameEntry* ge);

/* データを載せる */
int CHardAc::LoadRomsMs1(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!CEmuAcLoadMs1Code(&ms1Rom_, &ms1RomSize_, fs, ge))
		return 0;
	if (board_ == CEMU_AC_BOARD_M68K_PCM)
		CEmuAcLoadM68kPcmExtraCode(&ms1Rom_, &ms1RomSize_, fs, ge);
	/* atehate は作業 RAM 1 MiB で応答。族の残りは 64 KiB */
	unsigned ramNeed = 0x10000u;
	if (board_ == CEMU_AC_BOARD_M68K_PCM && m68kRamSize_ > ramNeed)
		ramNeed = m68kRamSize_;
	if (ms1Ram_ && ms1RamAlloc_ < ramNeed) {
		free(ms1Ram_);
		ms1Ram_ = NULL;
	}
	if (!ms1Ram_) {
		ms1Ram_ = (uint8_t*)malloc(ramNeed);
		if (!ms1Ram_) return 0;
		ms1RamAlloc_ = ramNeed;
	}
	memset(ms1Ram_, 0, ms1RamAlloc_);

	if (!CEmuAcLoadMs1Oki(&pcmRom_, &pcmRomSize_, fs, ge, "pcm1"))
		CEmuAcLoadMs1Oki(&pcmRom_, &pcmRomSize_, fs, ge, "pcm");
	CEmuAcLoadMs1Oki(&pcmRom2_, &pcmRom2Size_, fs, ge, "pcm2");
	if (!pcmRomSize_ || !pcmRom2Size_) {
		/* pcm1/pcm2 型は無い: コード以外で最大のメンバ 2 つを名順 */
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
	/* Seta/Cave: サンプル ROM は chip_ のもの。ここでの PCM チップであり、別サンプラ横の FM チップではない。 */
	if (board_ == CEMU_AC_BOARD_M68K_PCM && chip_ && pcmRomSize_)
		chip_->SetPcmRom(pcmRom_, pcmRomSize_);

	/* System A/B では soundlatch_w が IRQ4、System C では soundlatch_c_w が IRQ6。独自ハンドラを持つオートベクタのレベルを選ぶ。未使用レベルはこれらの ROM で RTE stub 1 本を共有。 */
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
	m68k_set_int_ack_callback(NULL); /* オートベクタ — F3 は自分のを入れる */
	m68k_pulse_reset();
	return 1;
}

/* M92 音源 ROM は 0x20000 領域への ROM_LOAD16_BYTE 対: "sl" 半分が D7-D0（偶数番地）、"sh" 半分が D15-D8（奇数）。カタログはファイル順なので名で選び、l/h マーカが無いときだけカタログ順へフォールバック。

   1FFF0 を IVT0 から書き直さない。V35 リセットは復号表付きで FFFF0 から FetchOp。新しい Irem Software Guard ビルドは暗号化 JMP FAR を置く（例: uccops F7…EA のち平文 0040:03B0）。その 5 バイトを生 CS:IP「ROM 外」と解釈して IVT0 を重ねると Rev 3.40+ 全セットのブートを壊す。上半分 0xFF パディングは正常。 */
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
	/* 多くの M92 ダンプは各 64KB EPROM の先頭 32KB だけ埋めるので、インターリーブ上半分（0x10000-0x1FFFF）は 5 バイトのリセット JMP 以外 0xFF パディング。Rev 3.40+ 曲表はしばしばその半分を指す（uccopsj 曲1 @0x120A4）。上半分が空なら下位 64KB を上へミラーし、それらのポインタが実データに着くように — リセットベクタは残す。 */
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

/* データを載せる */
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

	/* GA20 サンプル: 汎用 pcm 走査が既に bm_da / gf-da を扱う */
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
			/* 私有コピーを残す。FINT の第 2 バイトは平文（ExecV25Group の Fetch8）なので table[0x92]=0x92 を強制しない — gunforce の RAM init が使う MOV SI（table[0x92]=0xBE）が壊れた。 */
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
				/* Rev 3.40+ は FFFF0 の暗号化 JMP FAR（EA）でブート。初期セットは CLI（FA）のち JMP FAR。チャネル BGM シーケンサだけがラッチ 0x20+添字を使う — ノートリストセット（uccops）はリセットが EA でもカタログコードを生のまま。 */
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
							/* INTP1 は FINT;IRET — STI 無し */
							if (tab[m92Rom_[k]] == 0xCFu)
								break;
						}
						if (!stiAt) continue;
						/* STI 後の CALL → MOV BP,#0000 はチャネル BGM */
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
				/* 新しい IMC ビルドは 08C0 バイトリングを 0AF0 の 16 枠語リングに置換（エンキュー SHL BX,1 / MOV [BX+0AF0],AX） */
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

/* データを載せる */
int CHardAc::LoadRomsGx(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* MS1 と同じインターリーブ 16bit コードロード。drivers.xml: オフセット 0=偶数、0x20000=奇数 — カタログ最初の code メンバが D15-D8。 */
	if (!CEmuAcLoadMs1Code(&ms1Rom_, &ms1RomSize_, fs, ge))
		return 0;
	if (!ms1Ram_) {
		ms1Ram_ = (uint8_t*)malloc(0x10000);
		if (!ms1Ram_) return 0;
	}
	memset(ms1Ram_, 0, 0x10000);

	/* K054539×2 は 1 つの PCM 領域を共有（MAME "shared"）。カタログ pcm メンバをオフセットで連結（tkmmpzdm: 2 MiB + 2 MiB @ 0x200000）。 */
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
	gxTmsStatus_ = 0x07; /* dready|pc0|empty — ファームを TMS 待ちから外す */
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

/* データを載せる */
int CHardAc::LoadRomsPcmChip(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	/* SCSP / RF5C400 / Seibu: ホスト CPU 未エミュ — 将来ホスト用に波形 ROM（Seibu は任意 OKI）を載せる。試聴無しではチップは無音。 */
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

	/* Seibu: あれば Z80 イメージもマップ（暗号化 — ステップしない） */
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
	/* 不完全な波形ダンプを同梱する Model2 パックはソフト open */
	return (pcmRomSize_ > 0 || board_ == CEMU_AC_BOARD_SEGA_SCSP
		|| board_ == CEMU_AC_BOARD_KONAMI_RF5C400) ? 1 : 0;
}

/* QSound 1.04 表歩行はオペコードフェッチと即値／ディスプレースメント読を混ぜる。Kabuki はそれらを別プレーンへ復号するので、21 00 90 の平文 memcmp は dino/wof に一致しない。isImm[i]==1 がデータプレーンを比較。care==NULL は全バイト照合。care[i]==0 は即値番地など無視（1.03 は表が 7C10 で 9000 固定パターンに落ちない）。 */
static int QsMatchMixed(const uint8_t* op, const uint8_t* dt, unsigned a,
	const uint8_t* pat, const uint8_t* isImm, unsigned n, const uint8_t* care)
{
	if (!op || !dt) return 0;
	for (unsigned i = 0; i < n; i++) {
		if (care && !care[i]) continue;
		const uint8_t got = isImm[i] ? dt[a + i] : op[a + i];
		if (got != pat[i]) return 0;
	}
	return 1;
}

/* Taito L 1cpu: POST（B801 待ち / 9FF6 RAM 検査 / flipull 8K 和と 63E0）は音源に不要。
   horshoes は I=0 の IM2 表が $0060→$032D。flipull は SP を置いて $04D0 へ（本編 irq1 $0983）。 */
static void CEmuAcPatchTaitoL1Cpu(uint8_t* mem, uint8_t* rom, unsigned romSz)
{
	if (!mem) return;
	if (mem[0] == 0xc3 && mem[1] == 0x89 && mem[2] == 0x00) {
		/* horshoes: 0089 の B801 ディレイと JP $780B RAM テストを飛ばし 00C3 の初期化へ。
		   ISR $032D が FF08=$0B で bank B の音源を呼ぶので 780B / ROM+0x1780B は壊さない。
		   00C6 以降の CALL $05A2/$05F9 も YM 初期化なので NOP しない。 */
		mem[0x0089] = 0xf3;
		mem[0x008a] = 0xed;
		mem[0x008b] = 0x5e;
		mem[0x008c] = 0xc3;
		mem[0x008d] = 0xc3;
		mem[0x008e] = 0x00;
		/* 0666 と同じ: irq0=$04ED（EI RET）、irq1=$032D、irq2=$01A4 音源 */
		mem[0xff00] = 0x5e;
		mem[0xff01] = 0x60;
		mem[0xff02] = 0x62;
		if (rom && romSz > 0x008eu) {
			rom[0x0089] = 0xf3;
			rom[0x008a] = 0xed;
			rom[0x008b] = 0x5e;
			rom[0x008c] = 0xc3;
			rom[0x008d] = 0xc3;
			rom[0x008e] = 0x00;
		}
	} else if (mem[0] == 0xc3 && mem[1] == 0xd0 && mem[2] == 0x03) {
		mem[0x03d0] = 0xf3;
		mem[0x03d1] = 0x31;
		mem[0x03d2] = 0xfe;
		mem[0x03d3] = 0x9f;
		mem[0x03d4] = 0xc3;
		mem[0x03d5] = 0xd0;
		mem[0x03d6] = 0x04;
		mem[0x63e0] = 0xaf;
		mem[0x63e1] = 0xc9;
		if (rom && romSz > 0x03d6u) {
			rom[0x03d0] = 0xf3;
			rom[0x03d1] = 0x31;
			rom[0x03d2] = 0xfe;
			rom[0x03d3] = 0x9f;
			rom[0x03d4] = 0xc3;
			rom[0x03d5] = 0xd0;
			rom[0x03d6] = 0x04;
		}
		if (rom && romSz > 0x83e1u) {
			rom[0x83e0] = 0xaf;
			rom[0x83e1] = 0xc9;
		}
	}
}

void CHardAc::TaitoL1EnsureMem()
{
	if (board_ != CEMU_AC_BOARD_TAITO_OPM)
		return;
	if (!soundRom_ || soundRomSize_ < 3u)
		return;
	const int isHors = (soundRom_[0] == 0xc3 && soundRom_[1] == 0x89 && soundRom_[2] == 0x00);
	const int isFlip = (soundRom_[0] == 0xc3 && soundRom_[1] == 0xd0 && soundRom_[2] == 0x03);
	if (!isHors && !isFlip)
		return;
	taitoOpmMap_ = 14;
	unsigned n = (soundRomSize_ < 0x6000u) ? soundRomSize_ : 0x6000u;
	memcpy(mem_, soundRom_, n);
	if (n < 0x6000u)
		memset(mem_ + n, 0xff, 0x6000u - n);
	SetBank(isHors ? 0x0b : 4);
	CEmuAcPatchTaitoL1Cpu(mem_, soundRom_, soundRomSize_);
}

/* zip から ROM／曲データを載せる */
int CHardAc::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge) return 0;
	if (board_ != CEMU_AC_BOARD_MEGASYSTEM1 && board_ != CEMU_AC_BOARD_KONAMI_GX && !cpu_)
		return 0;
	memset(mem_, 0, sizeof(mem_));
	if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT && CEmuAcIsMegazone(ge))
		vsIoKind_ = 1;
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

	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && sys16RomBoard_ == 0x5047u && fs) {
		memset(mem_, 0, 0x10000);
		const uint8_t* prom = NULL;
		unsigned promSz = 0;
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1,
				pathA, (int)sizeof(pathA), NULL, NULL);
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d) continue;
			if (sz == 256u || sz == 512u) {
				prom = d;
				promSz = sz;
				continue;
			}
			if (sz != 0x1000u) continue;
			const int off = CEmuAcPengoIcOff(pathA);
			if (off < 0) continue;
			memcpy(mem_ + off, d, 0x1000u);
			loaded = 1;
		}
		if (loaded && mem_[0] != 0x31) {
			uint8_t* enc = (uint8_t*)malloc(0x8000u);
			uint8_t* dataPlane = (uint8_t*)malloc(0x8000u);
			if (enc && dataPlane) {
				memcpy(enc, mem_, 0x8000u);
				CEmuAcSega3155010Decode(enc, mem_, dataPlane, 0x8000u);
				if (qsKabukiData_) {
					free(qsKabukiData_);
					qsKabukiData_ = NULL;
				}
				qsKabukiData_ = dataPlane;
				qsKabuki_ = 1;
				dataPlane = NULL;
			}
			free(enc);
			free(dataPlane);
		}
		if (loaded) {
			uint8_t* p = (uint8_t*)malloc(0x8000u);
			if (p) {
				memcpy(p, mem_, 0x8000u);
				if (soundRom_) free(soundRom_);
				soundRom_ = p;
				soundRomSize_ = 0x8000u;
			}
			if (chip_ && prom && promSz)
				chip_->SetPcmRom(prom, promSz);
			if (chip_)
				CEmuChipC30SetEnable(chip_, 1);
			codeRom = mem_;
			codeRomSize = 0x8000u;
		}
	}

	if (board_ == CEMU_AC_BOARD_MEGASYSTEM1
		|| board_ == CEMU_AC_BOARD_M68K_PCM)
		return LoadRomsMs1(fs, ge);
	if (board_ == CEMU_AC_BOARD_KONAMI_GX)
		return LoadRomsGx(fs, ge);
	if (board_ == CEMU_AC_BOARD_IREM_M92)
		return LoadRomsM92(fs, ge);
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && snkMapKind_ >= 4) {
		int got = 0;
		if (ge) {
			for (int i = 0; i < ge->romCount && !got; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "audiocpu") != 0 && _stricmp(r->type, "sound") != 0
					&& _stricmp(r->type, "code") != 0 && _stricmp(r->type, "cpu") != 0)
					continue;
				unsigned sz = 0;
				const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
				if (!data || sz < 0x8000u) continue;
				if (data[0] != 0x1au && data[0] != 0x10u && data[0] != 0x1cu)
					continue;
				soundRom_ = (uint8_t*)malloc(0x8000u);
				if (!soundRom_) return 0;
				unsigned n = (sz > 0x8000u) ? 0x8000u : sz;
				memcpy(soundRom_, data, n);
				if (n < 0x8000u) memset(soundRom_ + n, 0xff, 0x8000u - n);
				soundRomSize_ = 0x8000u;
				got = 1;
			}
		}
		if (!got) {
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] != 0x1au && d[0] != 0x10u && d[0] != 0x1cu) continue;
				soundRom_ = (uint8_t*)malloc(0x8000u);
				if (!soundRom_) return 0;
				memcpy(soundRom_, d, 0x8000u);
				soundRomSize_ = 0x8000u;
				got = 1;
				break;
			}
		}
		if (!soundRom_ || !namcoM6809_) return 0;
		memset(mem_, 0, 0x1000);
		soundCmd_ = 0;
		soundCmdPending_ = 0;
		opmWrites_ = 0;
		cpuCycles_ = 0;
		namcoIrqAssert_ = 0;
		namcoFirqAssert_ = 0;
		namcoYmAddr_ = 0;
		if (chip_) chip_->Reset();
		{
			mc6809__t* cpu = NamcoCpuRaw(namcoM6809_);
			cpu->user = this;
			cpu->read = NamcoM6809BusRead;
			cpu->write = NamcoM6809BusWrite;
			cpu->fault = NamcoM6809BusFault;
			mc6809_reset(cpu);
			cpu->nmi_armed = true;
		}
		return 1;
	}
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
	if (board_ == CEMU_AC_BOARD_RAIZING)
		return LoadRomsRaizing(fs, ge);
	if (board_ == CEMU_AC_BOARD_T5182)
		return LoadRomsT5182(fs, ge);
	if (board_ == CEMU_AC_BOARD_HEBERPOP)
		return LoadRomsHeberpop(fs, ge);
	if (board_ == CEMU_AC_BOARD_BIONICC)
		return LoadRomsBionicc(fs, ge);
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL)
		return LoadRomsSeibu(fs, ge);
	if (board_ == CEMU_AC_BOARD_IREM_M62)
		return LoadRomsM62(fs, ge);
	if (board_ == CEMU_AC_BOARD_SEGA_SCSP)
		return segaM1Audio_ ? LoadRomsSegaM1(fs, ge)
			: LoadRomsSegaScsp(fs, ge);
	if (board_ == CEMU_AC_BOARD_KONAMI_RF5C400)
		return LoadRomsHornet(fs, ge);

	unsigned char* m72Code = (board_ == CEMU_AC_BOARD_IREM_M72)
		? CEmuAcM72InterleavedCode(fs, ge) : NULL;
	if (m72Code) {
		memcpy(mem_, m72Code, 0x10000);
		codeRom = m72Code;
		codeRomSize = 0x10000;
		loaded = 1;
		m72SoundRam_ = 1;
	} else if (board_ == CEMU_AC_BOARD_IREM_M72) {
		/* 専用音源 ROM → ROM マップ。YM@40（poundfor/m99/bbmanw）を嗅ぐ */
		m72SoundRam_ = (_stricmp(ge->subtype, "rtype") == 0) ? 1 : 0;
	}

	/* CPS2 QSound: 全 audiocpu イメージを soundRom_ に残す（sfz.01+sfz.02）。Z80 空間で固定なのは 0000-7FFF だけ。8000-BFFF は +0x8000 からバンク。ファイル全体を mem_ へ blit しない — F000 の作業 RAM を壊す。 */
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
			/* フォールバック: Z80 コードに見える最大 ?256K メンバを名順 */
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
		/* CPS1 QSound Kabuki（dino/wof/punisher/slammast）: 固定 0000-7FFF 窓の二重オペコード／データ復号。バンク 8000+ は平文のまま。 */
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
		/* Capcom QSound: カタログタイトルは packet[0] の 1 バイトだが、Z80 は (IY+0)/(IY+1) から 16bit BE コードを読む → code<<8。8bit 添字書き換えが要るドライバ族は 3 つ:

		   古典（1.04–1.06b）: 個数は (9000)、表は 9006 — 0100..0800 でパターン走査（番地は改訂毎に動く。ハードコード 0206 パッチはかつて 1.04 を壊し ssf2t を無音にした）。

		   初期（ssf2 1.03）: 同じ IY 16bit + 剰余 + *4 だが個数は (7C10)、表は 7C16。9000/8000 固定照合は外れ、HL=code<<8 が表+0x400 を指して選曲と実音がずれる。

		   後期（batcir/ddsom/1944/…）: 個数は (F010)、表 ptr は (F013) — 同じ IY 16bit ロードのあと DE=(F010) 剰余。書き換え無しだと HL=code<<8 が誤った（しばしば空）項目へ畳み、アイドルループは tick したまま（pc 約 0180）SILENT。 */
		if (soundRom_ && soundRomSize_ >= 0x8000u) {
			/* 即値／ディスプレースメントバイト（1）対オペコード（0）。表 $9000（CPS2）と $8000（CPS1 Kabuki 1.04）で同じ配置。 */
			static const uint8_t kQsIsImm[] = {
				0, 1, 1,
				0, 0, 0,
				0, 0, 1,
				0, 0, 1,
				0, 0, 0,
				0, 1,
				0,
				0, 0,
				0, 1, 1,
			};
			/* LD HL,nn / LD DE,nn の番地は版で動く。オペコードと IY 剰余だけ見る。 */
			static const uint8_t kQsCare[] = {
				1, 0, 0,
				1, 1, 1,
				1, 1, 1,
				1, 1, 1,
				1, 1, 1,
				1, 1,
				1,
				1, 1,
				1, 0, 0,
			};
			static const uint8_t kQsPat9000[] = {
				0x21, 0x00, 0x90,
				0x56, 0x23, 0x5e,
				0xfd, 0x66, 0x00,
				0xfd, 0x6e, 0x01,
				0xb7, 0xed, 0x52,
				0x30, 0xfb,
				0x19,
				0x29, 0x29,
				0x11, 0x06, 0x90,
			};
			static const uint8_t kQsPat8000[] = {
				0x21, 0x00, 0x80,
				0x56, 0x23, 0x5e,
				0xfd, 0x66, 0x00,
				0xfd, 0x6e, 0x01,
				0xb7, 0xed, 0x52,
				0x30, 0xfb,
				0x19,
				0x29, 0x29,
				0x11, 0x06, 0x80,
			};
			static const uint8_t kQsIdx[] = {
				0xfd, 0x6e, 0x00, /* LD L,(IY+0) */
				0x26, 0x00,       /* LD H,0 */
				0x29,             /* ADD HL,HL */
				0x29,             /* ADD HL,HL */
				0x18, 0x0b,       /* JR +11 → LD DE,表 */
			};
			/* LD H,(IY+0) / LD L,(IY+1) / LD DE,(F010) — 後期 QSound エンジン */
			static const uint8_t kQsLookupF010[] = {
				0xfd, 0x66, 0x00,
				0xfd, 0x6e, 0x01,
				0xed, 0x5b, 0x10, 0xf0,
			};
			static const uint8_t kQsIdxF010[] = {
				0xfd, 0x6e, 0x00, /* LD L,(IY+0) */
				0x26, 0x00,       /* LD H,0 */
				0x00,             /* 除いた 3 番目の IY オペコードバイトを NOP で埋める */
				0xed, 0x5b, 0x10, 0xf0,
			};
			/* Capcom ZN はラッチ+NMI 経由で本物 16bit 曲語を保つ — 8bit 添字へ畳まない（それが ts2 を無音にした）。 */
			if (!qsZn_) {
				const uint8_t* op = mem_;
				const uint8_t* dt = (qsKabuki_ && qsKabukiData_) ? qsKabukiData_ : mem_;
				int patched = 0;
				for (unsigned a = 0x0100u; a + sizeof(kQsPat9000) <= 0x0800u; a++) {
					if (!QsMatchMixed(op, dt, a, kQsPat9000, kQsIsImm, sizeof(kQsPat9000), NULL)
						&& !QsMatchMixed(op, dt, a, kQsPat8000, kQsIsImm, sizeof(kQsPat8000), NULL)
						&& !QsMatchMixed(op, dt, a, kQsPat9000, kQsIsImm, sizeof(kQsPat9000), kQsCare))
						continue;
					memcpy(soundRom_ + a, kQsIdx, sizeof(kQsIdx));
					memcpy(mem_ + a, kQsIdx, sizeof(kQsIdx));
					if (qsKabukiData_ && a + sizeof(kQsIdx) <= 0x8000u)
						memcpy(qsKabukiData_ + a, kQsIdx, sizeof(kQsIdx));
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
	} else if (!m72Code && !(board_ == CEMU_AC_BOARD_NAMCO_WSG && sys16RomBoard_ == 0x5047u && loaded)) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (!CEmuAcIsCodeRomType(r->type))
			continue;
		int off = r->offset;
		if (off < 0) off = 0;
		/* カタログは音源 CPU イメージをバンクオフセット ?64K（Model 2 SCSP、分割 68000）に置くことがある。Z80 空間へは 0 からマップ。 */
		int mapOff = off;
		if (mapOff >= 0x10000) mapOff = 0;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		/* megazone type=sub は I8039 DAC 4K。Z80 8K を 0000 に残す。 */
		if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT
			&& (vsIoKind_ == 1 || CEmuAcIsMegazone(ge))
			&& sz <= 0x1000u)
			continue;
		/* Sys16: type=sub @0000 として列挙された uPD7751 MCU を飛ばす */
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
		if (board_ == CEMU_AC_BOARD_SEGA_SYS1 && vsIoKind_ >= 1 && vsIoKind_ <= 3 && fs) {
			/* hyperspt は c10@0000 + c09@2000 の 16K。2 本目が無いとチェックサム 0000-3FFF で止まる。 */
			if (mem_[0x2000] == 0 && mem_[0x2001] == 0) {
				for (int i = 0; i < fs->fileCount; i++) {
					const unsigned sz = fs->files[i].size;
					const uint8_t* d = fs->files[i].data;
					if (!d || sz != 0x2000u) continue;
					if (d[0] == mem_[0] && d[1] == mem_[1] && d[2] == mem_[2])
						continue;
					if (d[0] == 0x00 && d[1] != 0x06)
						continue;
					memcpy(mem_ + 0x2000, d, 0x2000u);
					break;
				}
			}
			memset(mem_ + 0xe000, 0, 32);
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && vsIoKind_ != 1 && vsIoKind_ != 2 && fs) {
			/* 2K/4K 音源 ROM を名順で 0000 から連結。カタログが両方 offset=0 だと 2 本目が先頭を潰す。 */
			int idxs[8];
			int nIdx = 0;
			for (int i = 0; i < fs->fileCount && nIdx < (int)_countof(idxs); i++) {
				const unsigned sz = fs->files[i].size;
				if (sz != 0x800u && sz != 0x1000u)
					continue;
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
			for (int k = 0; k < nIdx && off < 0x3000u; k++) {
				unsigned sz = fs->files[idxs[k]].size;
				if (off + sz > 0x3000u)
					sz = 0x3000u - off;
				if (sz)
					memcpy(mem_ + off, fs->files[idxs[k]].data, sz);
				off += fs->files[idxs[k]].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ == 4 && fs) {
			/* fitegolf: gu3 16K (F3 31) + gu4 32K。countryc: 64K F3 31。GFX 32K を連結しない。 */
			int start = -1;
			int big64 = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (d[0] != 0xf3 || d[1] != 0x31) continue;
				if (sz == 0x10000u) { big64 = i; break; }
				if ((sz == 0x4000u || sz == 0x8000u) && start < 0)
					start = i;
			}
			if (big64 >= 0) {
				unsigned n = fs->files[big64].size;
				if (n > 0xc000u) n = 0xc000u;
				memcpy(mem_, fs->files[big64].data, n);
				loaded++;
			} else if (start >= 0) {
				unsigned off = fs->files[start].size;
				if (off > 0xc000u) off = 0xc000u;
				memcpy(mem_, fs->files[start].data, off);
				loaded++;
				char startName[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[start].path, -1, startName, (int)sizeof(startName), NULL, NULL);
				int best = -1;
				char bestName[CEMU_ZIP_PATH];
				bestName[0] = 0;
				for (int i = 0; i < fs->fileCount; i++) {
					if (i == start) continue;
					const unsigned sz = fs->files[i].size;
					if (sz != 0x4000u && sz != 0x8000u) continue;
					char pn[CEMU_ZIP_PATH];
					WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pn, (int)sizeof(pn), NULL, NULL);
					if (_stricmp(pn, startName) <= 0) continue;
					if (best < 0 || _stricmp(pn, bestName) < 0) {
						best = i;
						memcpy(bestName, pn, sizeof(bestName));
					}
				}
				if (best >= 0 && off < 0xc000u) {
					unsigned sz = fs->files[best].size;
					if (off + sz > 0xc000u)
						sz = 0xc000u - off;
					if (sz)
						memcpy(mem_ + off, fs->files[best].data, sz);
					loaded++;
				}
			}
			memset(mem_ + 0xc000, 0, 0x1000);
		}
		if (board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ >= 2 && snkMapKind_ != 4 && fs) {
			/* aso: 32K+16K。mainsnk: 16K。canvas: 16K+8K。カタログ offset=0 の二重 blit を名順連結で直す。 */
			int idxs[8];
			int nIdx = 0;
			for (int i = 0; i < fs->fileCount && nIdx < (int)_countof(idxs); i++) {
				const unsigned sz = fs->files[i].size;
				if (sz != 0x2000u && sz != 0x4000u && sz != 0x8000u)
					continue;
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
			const unsigned cap = (snkMapKind_ == 2) ? 0xc000u : 0x8000u;
			for (int k = 0; k < nIdx && off < cap; k++) {
				unsigned sz = fs->files[idxs[k]].size;
				if (off + sz > cap)
					sz = cap - off;
				if (sz)
					memcpy(mem_ + off, fs->files[idxs[k]].data, sz);
				off += fs->files[idxs[k]].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ == 1 && fs) {
			/* chopper/psychos/tdfever: 64K 音源 CPU と Y8950 ADPCM が同サイズ。DI;LD SP (F3 31) を選ぶ。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (sz >= 0x4000u && d[0] == 0xf3 && d[1] == 0x31) {
					if (sz == 0x10000u) { pick = i; break; }
					if (pick < 0) pick = i;
				}
			}
			if (pick >= 0) {
				unsigned n = fs->files[pick].size;
				if (n > 0xc000u) n = 0xc000u;
				memcpy(mem_, fs->files[pick].data, n);
				memset(mem_ + 0xc000, 0, 0x1000);
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 0 && fs) {
			/* fstarfrc/ginkun/riot: 64K Z80 (DI;IM1;LD SP,FC00) と 128K OKI が同 zip。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0xf000, 0, 0xc00);
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 11 && fs) {
			/* MAME superx: 64K 音源 Z80 は IM 1; LD SP,F800（先頭 F3 無し）。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xed && d[1] == 0x56 && d[2] == 0x31
					&& d[3] == 0x00 && d[4] == 0xf8) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0xf000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 12 && fs) {
			/* MAME powerins: 128K Z80 DI;IM1;LD SP,DFFF。0000-BFFF ROM、C000-DFFF RAM。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x20000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56
					&& d[3] == 0x31 && d[4] == 0xff && d[5] == 0xdf) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0xc000u);
				memset(mem_ + 0xc000, 0, 0x2000);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 13 && fs) {
			/* MAME nslasher: 64K Z80 JP $003B / LD SP,$8800。I/O が全 ROM を見るので soundRom_ に複製。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xc3 && d[1] == 0x3b && d[2] == 0x00
					&& d[0x3c] == 0x31 && d[0x3d] == 0x00 && d[0x3e] == 0x88) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0x8000, 0, 0x800);
				if (soundRom_) { free(soundRom_); soundRom_ = NULL; }
				soundRom_ = (uint8_t*)malloc(0x10000u);
				if (soundRom_) {
					memcpy(soundRom_, fs->files[pick].data, 0x10000u);
					soundRomSize_ = 0x10000u;
				}
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiJoeMap() && fs) {
			/* MAME gijoe/lethalen: 64K Z80 IM 1 のあと JP $0086。ROM 0000-EFFF。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xed && d[1] == 0x56 && d[3] == 0xc3
					&& d[4] == 0x86 && d[5] == 0x00) {
					pick = i;
					break;
				}
				if (d[0] == 0xed && d[1] == 0x56 && d[2] == 0xc3
					&& d[3] == 0x86 && d[4] == 0x00) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0xf000, 0, 0x800);
				/* gijoe 0849 自己テスト末尾が JP $080C で戻らない。RET にして 00AD の本番初期化へ。 */
				if (mem_[0x0855] == 0xc3 && mem_[0x0856] == 0x0c && mem_[0x0857] == 0x08)
					mem_[0x0855] = 0xc9;
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiXmenMap() && fs) {
			/* MAME xmen: 128K 065-a01.6f = ED 56 F3 C3 06 02。0000-7FFF 固定、16K×8 バンク。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x20000u) continue;
				if (d[0] == 0xed && d[1] == 0x56 && d[2] == 0xf3 && d[3] == 0xc3) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				if (soundRom_) free(soundRom_);
				soundRomSize_ = fs->files[pick].size;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0xc000, 0, 0x2000);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiPrmrsocrMap() && fs) {
			/* MAME prmrsocr: 128K 101c05.5e = 31 00 E0 ED 56。0000-7FFF 固定、16K×8 バンク @8000。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x20000u) continue;
				if (d[0] == 0x31 && d[1] == 0x00 && d[2] == 0xe0 && d[3] == 0xed) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				if (soundRom_) free(soundRom_);
				soundRomSize_ = fs->files[pick].size;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0xc000, 0, 0x2000);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiGlfMap() && fs) {
			/* MAME glfgreat: 32K 061f01.4e = ED 56 AF 32 2F F8。ROM 0000-7FFF、RAM F000。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xed && d[1] == 0x56 && d[2] == 0xaf && d[4] == 0x2f && d[5] == 0xf8) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0xf000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiRollergMap() && fs) {
			/* MAME rollerg: 32K 999m01.e11 = ED 56 AF 32 2F A0。ROM 0000-7FFF、RAM 8000。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xed && d[1] == 0x56 && d[2] == 0xaf && d[4] == 0x2f && d[5] == 0xa0) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0x8000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE && fs) {
			/* MAME spy: 32K 857d01.bin = ED 56 31 00 87。ROM 0000-7FFF、RAM 8000-87FF。
			   シグネチャで vsIoKind を立て、カタログがサンプル ROM を code にしてもチェックサム 8A8A が通る。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xed && d[1] == 0x56 && d[2] == 0x31
					&& d[3] == 0x00 && d[4] == 0x87) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				vsIoKind_ = 1;
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0x8000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 2 && fs) {
			/* gemini/silkworm/backfirt: 32K Z80 (DI;IM1;LD SP,8800) と 32K ADPCM が同 zip。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0x8000, 0, 0x800);
				/* backfirt: JP 0083 は $2000 RAM へ delay をコピーする。CEmu は 0000-7FFF を ROM 扱いなので
				   CALL $2010 が JP (HL) データに落ち 2C40E945 で再ブートする。
				   0083 は RETN ではない（旧パッチ条件が外れ続けた）。00F3 の CALL 022C;EI へ飛ばし、
				   CALL 2010 を ROM 内 delay $0139 へ張り替える。 */
				if (mem_[0x0a] == 0xc3 && mem_[0x0b] == 0x83 && mem_[0x0c] == 0x00
					&& mem_[0xf3] == 0xcd && mem_[0xf4] == 0x2c
					&& mem_[0x139] == 0xf5 && mem_[0x145] == 0xc9) {
					mem_[0x0b] = 0xf3;
					if (mem_[0x0d] == 0xc3 && mem_[0x0e] == 0x83 && mem_[0x0f] == 0x00)
						mem_[0x0e] = 0xf3;
					for (unsigned a = 0; a + 2u < 0x8000u; a++) {
						if (mem_[a] == 0xcdu && mem_[a + 1u] == 0x10u && mem_[a + 2u] == 0x20u) {
							mem_[a + 1u] = 0x39u;
							mem_[a + 2u] = 0x01u;
						}
					}
				}
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 19 && fs) {
			/* tehkanwc/gridiron: 16K Z80 (F3 ED 56) と 16K ADPCM が同 zip。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x4000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x4000u);
				memset(mem_ + 0x4000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TOAPLAN1 && toaplanKaneko_ == 5 && fs) {
			/* perfrman: 8K Z80（LD HL,8801）。GFX は zip に無い。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x2000u) continue;
				if (d[0] == 0x21 && d[1] == 0x01 && d[2] == 0x88) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x2000u);
				memset(mem_ + 0x8800, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 3 && fs) {
			/* spbactn: 64K Z80 (F3 ED 56) + 128K OKI。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0xf000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 8 && fs) {
			/* ashnojoe: 32K Z80 (F3 ED 56) + 512K バンク。 */
			int pick = -1;
			int bank = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (sz == 0x8000u && d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56)
					pick = i;
				else if (sz >= 0x20000u)
					bank = i;
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0x6000, 0, 0x2000);
				mem_[0x0e] = 0x01; /* HALT×0x78 待ちを 1 に短縮 */
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
			if (bank >= 0) {
				if (soundRom_) free(soundRom_);
				soundRomSize_ = fs->files[bank].size;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_) {
					memcpy(soundRom_, fs->files[bank].data, soundRomSize_);
					unsigned n = 0x8000u;
					if (n > soundRomSize_) n = soundRomSize_;
					memcpy(mem_ + 0x8000, soundRom_, n);
				} else
					soundRomSize_ = 0;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 9 && fs) {
			/* twinhawk/daisenpu: 32K Z80 (F3 ED 56)。線形 32K。SetBank(0) は 4000 を潰す。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0xc000, 0, 0x2000);
				if (soundRom_) free(soundRom_);
				soundRomSize_ = 0x8000u;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 16 && fs) {
			/* arkanoid: 32K ブート F3 ED 56 @0000 + 32K @8000。MCU 2K と GFX は捨てる。 */
			int boot = -1, bank = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (boot < 0 && d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56)
					boot = i;
				else if (bank < 0)
					bank = i;
			}
			if (boot >= 0) {
				memcpy(mem_, fs->files[boot].data, 0x8000u);
				if (bank >= 0)
					memcpy(mem_ + 0x8000, fs->files[bank].data, 0x8000u);
				else
					memset(mem_ + 0x8000, 0xff, 0x8000);
				memset(mem_ + 0xc000, 0, 0x800);
				memset(mem_ + 0xe000, 0, 0x1000);
				if (soundRom_) free(soundRom_);
				soundRomSize_ = 0x10000u;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_) {
					memcpy(soundRom_, mem_, 0x10000u);
				} else
					soundRomSize_ = 0;
				codeRom = fs->files[boot].data;
				codeRomSize = fs->files[boot].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 17 && fs) {
			/* kabukiz: 128K audiocpu F3 ED 56。GFX は zip に無い。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x20000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				const unsigned sz = fs->files[pick].size;
				const uint8_t* d = fs->files[pick].data;
				if (soundRom_) free(soundRom_);
				soundRomSize_ = sz;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, d, soundRomSize_);
				else
					soundRomSize_ = 0;
				memcpy(mem_, d, 0x8000u);
				memcpy(mem_ + 0x8000, d, 0x4000u);
				memset(mem_ + 0xc000, 0xff, 0x2000);
				memset(mem_ + 0xe000, 0, 0x2000);
				codeRom = d;
				codeRomSize = sz;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 18 && fs) {
			/* Street Fighter 1: 32K 音楽 Z80 F3 ED 56 31 00 C8。sfu-00/sf-01 は MSM 第2 Z80。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				const uint8_t* d = fs->files[pick].data;
				memcpy(mem_, d, 0x8000u);
				memset(mem_ + 0x8000, 0xff, 0x4000);
				memset(mem_ + 0xc000, 0, 0x800);
				codeRom = d;
				codeRomSize = 0x8000u;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 15 && fs) {
			/* volfied: 32K Z80 (F3 ED 56 3E 05 32 00 88)。GFX 128K はスキップ。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56
					&& d[3] == 0x3e && d[4] == 0x05) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0x8000, 0, 0x800);
				if (soundRom_) free(soundRom_);
				soundRomSize_ = 0x8000u;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && (taitoOpmMap_ == 12 || taitoOpmMap_ == 13) && fs) {
			/* fhawk: 64K F3 ED 56。kurikint: 64K C3 FB 00 のあと $00FB で F3 ED 56。GFX 128K はスキップ。 */
			int pick = -1, pickC3 = -1, pick64 = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				pick64 = i;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
				if (d[0] == 0xc3)
					pickC3 = i;
			}
			if (pick < 0) pick = pickC3;
			if (pick < 0) pick = pick64;
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				if (taitoOpmMap_ == 12)
					memset(mem_ + 0x8000, 0, 0x2000);
				else {
					memset(mem_ + 0xc000, 0, 0x2800);
					mem_[0xe7f0] = 0xff;
				}
				if (soundRom_) free(soundRom_);
				soundRomSize_ = 0x10000u;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && fs && taitoOpmMap_ != 14 && taitoOpmMap_ != 16 && taitoOpmMap_ != 17 && taitoOpmMap_ != 18) {
			/* C3 タイトルは Init の subtype 漏れでも ROM シグネチャで map 14 にする。 */
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (sz != 0x10000u && sz != 0x20000u && sz != 0x40000u)
					continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x5e) {
					taitoOpmMap_ = 14;
					break;
				}
				if (d[0] == 0xc3 && d[1] == 0x89 && d[2] == 0x00 && sz == 0x20000u) {
					taitoOpmMap_ = 14;
					break;
				}
				if (d[0] == 0xc3 && d[1] == 0xd0 && d[2] == 0x03 && sz == 0x10000u) {
					taitoOpmMap_ = 14;
					break;
				}
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 14 && fs) {
			/* Taito L 1cpu: メイン 64K/128K/256K。GFX は偶数サイズでも C3/F3 で無いことが多い。 */
			int pick = -1, pick128 = -1, pick64 = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (sz != 0x10000u && sz != 0x20000u && sz != 0x40000u)
					continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x5e) {
					pick = i;
					break;
				}
				if (d[0] == 0xc3) {
					if (sz == 0x20000u && pick128 < 0)
						pick128 = i;
					else if (sz == 0x10000u && pick64 < 0)
						pick64 = i;
					else if (pick < 0)
						pick = i;
				}
			}
			if (pick < 0) pick = pick128;
			if (pick < 0) pick = pick64;
			if (pick >= 0) {
				const unsigned sz = fs->files[pick].size;
				const uint8_t* d = fs->files[pick].data;
				if (soundRom_) free(soundRom_);
				soundRomSize_ = sz;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, d, soundRomSize_);
				else
					soundRomSize_ = 0;
				unsigned n = (sz < 0x6000u) ? sz : 0x6000u;
				memcpy(mem_, d, n);
				if (n < 0x6000u)
					memset(mem_ + n, 0xff, 0x6000u - n);
				memset(mem_ + 0x8000, 0, 0x8000);
				if (d[0] == 0xc3 && d[1] == 0x89 && d[2] == 0x00)
					SetBank(0x0b);
				else if (d[0] == 0xc3 && d[1] == 0xd0 && d[2] == 0x03)
					SetBank(4);
				else
					SetBank(0);
				CEmuAcPatchTaitoL1Cpu(mem_, soundRom_, soundRomSize_);
				codeRom = d;
				codeRomSize = sz;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 10 && fs) {
			/* cadash/earthjkr/galmedes/topspeed: 64K Z80 (F3 ED 56 3E 05)。topspeed zip は 64K が 3 本。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56 && d[3] == 0x3e) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0x8000, 0, 0x1000);
				if (soundRom_) free(soundRom_);
				soundRomSize_ = 0x10000u;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 11 && fs) {
			/* lomakai: 64K Z80 (F3 ED 56 C3 D9 00)。マップは 0000-3FFF ROM + C000 RAM。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56 && d[3] == 0xc3) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x4000u);
				memset(mem_ + 0xc000, 0, 0x800);
				soundCmd_ = 0xff;
				if (soundRom_) free(soundRom_);
				soundRomSize_ = 0x4000u;
				soundRom_ = (uint8_t*)malloc(soundRomSize_);
				if (soundRom_)
					memcpy(soundRom_, fs->files[pick].data, soundRomSize_);
				else
					soundRomSize_ = 0;
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 5 && fs) {
			/* daikaiju: 32K Z80 (DI; LD SP,$87FF)。lsasquad は F3 ED 56 のまま汎用ロード。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x8000u) continue;
				if (d[0] == 0xf3 && d[1] == 0x31) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x8000u);
				memset(mem_ + 0x8000, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 12 && fs) {
			/* deniam16b: 64K Z80 (F3 ED 56) と 512K OKI が同 zip。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				memset(mem_ + 0xf800, 0, 0x800);
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 13 || vsIoKind_ == 14) && fs) {
			/* lastduel/madgear: 64K Z80 (F3 ED 56)。madgear zip には 128K OKI が 2 本。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x10000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				memcpy(mem_, fs->files[pick].data, 0x10000u);
				if (vsIoKind_ == 13)
					memset(mem_ + 0xe000, 0, 0x800);
				else {
					memset(mem_ + 0xd000, 0, 0x800);
					if (soundRomSize_ > 0x8000u || fs->files[pick].size > 0x8000u) {
						const uint8_t* src = fs->files[pick].data;
						memcpy(mem_ + 0x8000, src + 0x8000u, 0x5000u);
					}
				}
				codeRom = fs->files[pick].data;
				codeRomSize = fs->files[pick].size;
				loaded++;
			}
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 0 && fs) {
			/* ninjakd2: NEC MC8123。8K .key + 64K 暗号音源（nk2_06）。nk2_09 は 01 02 03… サンプル。
			   オペコードは mem_/soundRom_、データ面は qsKabukiData_（Kabuki と同じ二面、sizeof 不変）。
			   ブートレグ ninjakd2a/b は平文 F3 ED 56 で鍵が無いのでここを飛ばす。 */
			int keyIdx = -1, encIdx = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1,
					pathA, (int)sizeof(pathA), NULL, NULL);
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (sz == 0x2000u && (strstr(pathA, ".key") || strstr(pathA, ".KEY")
					|| strstr(pathA, "key")))
					keyIdx = i;
				if (sz == 0x10000u && d[0] != 0xf3
					&& !(d[0] == 0x01 && d[1] == 0x02 && d[2] == 0x03))
					encIdx = i;
			}
			if (keyIdx >= 0 && encIdx >= 0) {
				uint8_t* op = (uint8_t*)malloc(0x10000u);
				uint8_t* dt = (uint8_t*)malloc(0x10000u);
				if (op && dt) {
					CEmuMc8123Decode(fs->files[encIdx].data, fs->files[keyIdx].data,
						op, dt, 0x10000u);
					if (soundRom_) { free(soundRom_); soundRom_ = NULL; }
					if (qsKabukiData_) { free(qsKabukiData_); qsKabukiData_ = NULL; }
					soundRom_ = op;
					soundRomSize_ = 0x10000u;
					qsKabukiData_ = dt;
					qsKabuki_ = 1;
					memcpy(mem_, op, 0x10000u);
					memset(mem_ + 0xc000, 0, 0x800);
					codeRom = op;
					codeRomSize = 0x10000u;
					loaded++;
				} else {
					free(op);
					free(dt);
				}
			}
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 15 && fs) {
			/* mgakuen 平文: 32K F3 ED 56。marukin Kabuki: 32K 暗号 → オペコード 31 xx xx F3 ED 56。
			   バンク 128K。MAME mitchell_decode: 0000-7FFF base 0、各 16K バンク base 0x8000。
			   soundRom_ = オペコード 32K|128K。qsKabukiData_ = 同レイアウトのデータ面。 */
			int pick = -1, bankRom = -1, kabukiPick = -1;
			CEmuKabukiKey kk;
			int haveKey = (ge && ge->archive[0] && CEmuKabukiLookup(ge->archive, &kk)) ? 1 : 0;
			if (!haveKey) {
				kk.swapKey1 = 0x54321076u;
				kk.swapKey2 = 0x54321076u;
				kk.addrKey = 0x4854u;
				kk.xorKey = 0x4fu;
			}
			uint8_t opProbe[8], dtProbe[8];
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d) continue;
				if (pick < 0 && sz == 0x8000u && d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56)
					pick = i;
				if (kabukiPick < 0 && sz == 0x8000u && d[0] != 0xf3) {
					CEmuKabukiDecode(d, opProbe, dtProbe, 0, 8,
						kk.swapKey1, kk.swapKey2, kk.addrKey, kk.xorKey);
					if (opProbe[3] == 0xf3 && opProbe[4] == 0xed && opProbe[5] == 0x56)
						kabukiPick = i;
				}
				if (bankRom < 0 && sz == 0x20000u && d[0] != 0)
					bankRom = i;
			}
			const int useKabuki = (pick < 0 && kabukiPick >= 0) ? 1 : 0;
			if (useKabuki)
				pick = kabukiPick;
			if (qsKabukiData_) {
				free(qsKabukiData_);
				qsKabukiData_ = NULL;
			}
			qsKabuki_ = 0;
			if (pick >= 0) {
				const uint8_t* d = fs->files[pick].data;
				memset(mem_ + 0xc000, 0, 0x4000);
				unsigned bankBytes = (bankRom >= 0) ? fs->files[bankRom].size : 0u;
				unsigned need = 0x8000u + bankBytes;
				if (soundRom_) free(soundRom_);
				soundRom_ = (uint8_t*)malloc(need);
				if (!soundRom_) {
					soundRomSize_ = 0;
				} else if (useKabuki) {
					uint8_t* dataPlane = (uint8_t*)malloc(need);
					if (!dataPlane) {
						free(soundRom_);
						soundRom_ = NULL;
						soundRomSize_ = 0;
					} else {
						CEmuKabukiDecode(d, soundRom_, dataPlane, 0, 0x8000,
							kk.swapKey1, kk.swapKey2, kk.addrKey, kk.xorKey);
						if (bankRom >= 0) {
							const uint8_t* b = fs->files[bankRom].data;
							for (unsigned off = 0; off < bankBytes; ) {
								unsigned n = 0x4000u;
								if (off + n > bankBytes)
									n = bankBytes - off;
								CEmuKabukiDecode(b + off, soundRom_ + 0x8000u + off,
									dataPlane + 0x8000u + off, 0x8000, (int)n,
									kk.swapKey1, kk.swapKey2, kk.addrKey, kk.xorKey);
								off += n;
							}
						}
						soundRomSize_ = need;
						qsKabukiData_ = dataPlane;
						qsKabuki_ = 1;
						memcpy(mem_, soundRom_, 0x8000u);
						if (bankBytes >= 0x4000u)
							memcpy(mem_ + 0x8000, soundRom_ + 0x8000u, 0x4000u);
						else
							memset(mem_ + 0x8000, 0xff, 0x4000);
					}
				} else {
					memcpy(soundRom_, d, 0x8000u);
					soundRomSize_ = 0x8000u;
					memcpy(mem_, d, 0x8000u);
					if (bankRom >= 0) {
						memcpy(soundRom_ + 0x8000u, fs->files[bankRom].data, bankBytes);
						soundRomSize_ = need;
						memcpy(mem_ + 0x8000, fs->files[bankRom].data, 0x4000u);
					} else
						memset(mem_ + 0x8000, 0xff, 0x4000);
				}
				if (soundRom_) {
					codeRom = d;
					codeRomSize = 0x8000u;
					loaded++;
					bankLoaded_ = 1;
				}
			}
		}
		/* cclimbr2/legion: カタログは 48K イメージを C000 へ blit し得る。ハード RAM はそこから（cclimbr2_soundmap）。ROM は BFFF まで。 */
		if (board_ == CEMU_AC_BOARD_TERRACRE && terracreMap_ == 2)
			memset(mem_ + 0xc000, 0, 0x4000);
	}

	/* フォールバック: 最良の音源 CPU メンバを 0000 へ（romlist が空／不一致のローカル zip）。32K Sys16 ダンプ 2 本は 64K へ連結 — sharrier PCM 名（7231/7232）ではない。 */
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
		/* 弱いが妥当なスコアも受ける（128K Model2 コードダンプは約 -40） */
		if (best >= 0 && bestScore >= -80) {
			unsigned sz = fs->files[best].size;
			const unsigned char* data = fs->files[best].data;
			unsigned n = sz > 0x10000 ? 0x10000u : sz;
			memcpy(mem_, data, n);
			codeRom = data;
			codeRomSize = sz;
			loaded++;
			/* 最初が ?32K のときだけ第 2 の 32K バンクを付ける（Sys16 双子 Z80 ダンプ）。sharrier/hangon PCM ファイル名（7231/7232）と 64K 単体は飛ばす。 */
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
			if (pcm2_ && r->type && (_stricmp(r->type, "pcm2") == 0
				|| _stricmp(r->type, "adpcm2") == 0))
				CEmuAcAppendPcm(&pcmRom2_, &pcmRom2Size_, data, sz);
			else
				CEmuAcAppendPcm(&pcmRom_, &pcmRomSize_, data, sz);
		}
		if (!pcmRomSize_) {
			/* PCM 候補を名で並べ、data\ac 対 data\roms の命名でもバンク順が安定するように（mpr-10930 が mpr-10931 より先）。 */
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
		if (pcm2_ && pcmRom2Size_)
			pcm2_->SetPcmRom(pcmRom2_, pcmRom2Size_);
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 12
			&& pcm2_ && pcmRomSize_ >= 0x400000u && !pcmRom2Size_) {
			/* 名順 1MB×4 = oki1 (10+11) | oki2 (8+9)。MAME nmk112 rom0/rom1。 */
			const unsigned half = pcmRomSize_ / 2u;
			CEmuAcAppendPcm(&pcmRom2_, &pcmRom2Size_, pcmRom_ + half, pcmRomSize_ - half);
			pcmRomSize_ = half;
			if (pcm_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
			pcm2_->SetPcmRom(pcmRom2_, pcmRom2Size_);
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 13
			&& pcm2_ && pcmRomSize_ >= 0x100000u && !pcmRom2Size_) {
			/* mbh-10.14l + mbh-11.16l 名順連結 → oki0 | oki1。各 512KiB。 */
			const unsigned half = pcmRomSize_ / 2u;
			CEmuAcAppendPcm(&pcmRom2_, &pcmRom2Size_, pcmRom_ + half, pcmRomSize_ - half);
			pcmRomSize_ = half;
			if (pcm_) pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
			pcm2_->SetPcmRom(pcmRom2_, pcmRom2Size_);
		}
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 13) {
			CEmuAcDeco32OkiBank(raizingOkiBank_[0], pcmRomSize_, 0);
			CEmuAcDeco32OkiBank(raizingOkiBank_[1], pcmRom2Size_ ? pcmRom2Size_ : pcmRomSize_, 0);
			if (pcm_) CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
			if (pcm2_) CEmuChipOki6295SetBankTable(pcm2_, raizingOkiBank_[1]);
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 4 && pcm2_ && pcmRomSize_ && !pcmRom2Size_)
			pcm2_->SetPcmRom(pcmRom_, pcmRomSize_);
		if (board_ == CEMU_AC_BOARD_TECMO16 && (tecmoOpl_ == 5 || (tecmoOpl_ >= 7 && tecmoOpl_ <= 10))) {
			for (int chip = 0; chip < 2; chip++) {
				const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
				unsigned pages = (sz >= 0x20000u) ? (sz / 0x20000u) : 1u;
				unsigned lo = 0, hi = 0;
				(void)pages;
				unsigned* t = raizingOkiBank_[chip];
				t[0] = t[1] = t[2] = t[3] = lo;
				t[4] = lo; t[5] = lo + 1u;
				t[6] = hi; t[7] = hi + 1u;
			}
		}
		if (board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 1 || vsIoKind_ == 2)) {
			for (int chip = 0; chip < 2; chip++) {
				unsigned* t = raizingOkiBank_[chip];
				const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
				unsigned pages = (sz >= 0x10000u) ? (sz / 0x10000u) : 1u;
				t[0] = t[1] = t[2] = t[3] = t[4] = 0;
				t[5] = (pages > 1u) ? 1u : 0u;
				if (vsIoKind_ == 2) {
					t[6] = (pages > 2u) ? 2u : 0u;
					t[7] = (pages > 3u) ? 3u : t[6];
				} else {
					t[5] = t[6] = t[7] = 0;
				}
			}
		}
		/* mystwarr の両 K054539 は同じサンプル ROM 領域を番地 */
		if (pcmRomSize_ && pcm2_ && konamiPcm2Addr_)
			pcm2_->SetPcmRom(pcmRom_, pcmRomSize_);
		/* CPS2: 数メガのサンプルバンクを強制（sfx.11 / sz3.11） */
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

	/* YM2610 ADPCM-A/B（Taito F2/H/B と V-System aerofgt 族）。romlist に adpcm 項目が無い zip はサイズ／名ヒューリスティックへフォールバック。 */
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
		/* MAME psikyo init_sngkace: ADPCM サンプルの bit6/7 を入れ替える。そのまま鳴らすと PCM が壊れる。 */
		if (vsIoKind_ == 6 && chip_ && fs) {
			const unsigned char* src = NULL;
			unsigned srcSz = 0;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (!r->type || _stricmp(r->type, "adpcma") != 0) continue;
				src = CEmuZipFsFind(fs, r->name, &srcSz);
				if (src && srcSz) break;
			}
			if (!src || !srcSz) {
				for (int i = 0; i < fs->fileCount; i++) {
					char pathA[CEMU_ZIP_PATH];
					WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
					if (fs->files[i].size == 0x100000u && CEmuAcContainsI(pathA, "u68")) {
						src = fs->files[i].data;
						srcSz = fs->files[i].size;
						break;
					}
				}
			}
			if (src && srcSz) {
				uint8_t* tmp = (uint8_t*)malloc(srcSz);
				if (tmp) {
					memcpy(tmp, src, srcSz);
					for (unsigned i = 0; i < srcSz; i++) {
						const uint8_t x = tmp[i];
						tmp[i] = (uint8_t)(((x & 0x40) << 1) | ((x & 0x80) >> 1) | (x & 0x3f));
					}
					chip_->SetAdpcmRom(tmp, srcSz, 0);
					free(tmp);
				}
			}
		}
	}

	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 6 && chip_) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!r->type || _stricmp(r->type, "pcm") != 0) continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz) continue;
			int off = r->offset;
			if (off < 0) off = 0;
			chip_->SetAdpcmB(data, sz, (unsigned)off);
		}
	}

	/* CPS1: MAME audiocpu は 32K 固定 @0000 + ROM_CONTINUE 32K @0x10000。64K を線形に 0000-FFFF へ置くと F004 バンク 1（file+0x4000 を 8000 へ）が CONTINUE 後半を読み、OKI サンプル番号が壊れる。 */
	if (board_ == CEMU_AC_BOARD_CPS1 && codeRom && codeRomSize) {
		const unsigned need = (codeRomSize == 0x10000u) ? 0x18000u : codeRomSize;
		uint8_t* p = (uint8_t*)malloc(need);
		if (p) {
			memset(p, 0xff, need);
			if (codeRomSize == 0x10000u) {
				memcpy(p, codeRom, 0x8000u);
				memcpy(p + 0x10000u, codeRom + 0x8000u, 0x8000u);
			} else {
				memcpy(p, codeRom, codeRomSize);
			}
			if (soundRom_) free(soundRom_);
			soundRom_ = p;
			soundRomSize_ = need;
			unsigned n = (need < 0x8000u) ? need : 0x8000u;
			memcpy(mem_, soundRom_, n);
			if (n < 0x8000u)
				memset(mem_ + n, 0xff, 0x8000u - n);
			memset(mem_ + 0x8000, 0xff, 0x8000u);
		}
	}

	/* バンク音源 ROM（Taito F200 / CT1-CT2 ラッチ、aerofgt ポート 04、Konami K054539 16K 窓 @8000） */
	if ((board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_VSYSTEM
		|| (board_ == CEMU_AC_BOARD_TAITO_OPM && (taitoOpmMap_ < 12 || taitoOpmMap_ > 15))
		|| (board_ == CEMU_AC_BOARD_KONAMI_PCM && konamiBankAddr_)
		|| (board_ == CEMU_AC_BOARD_TECMO16 && (tecmoOpl_ == 5 || (tecmoOpl_ >= 7 && tecmoOpl_ <= 10)))
		|| (board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 1 || vsIoKind_ == 3 || vsIoKind_ == 4 || vsIoKind_ == 5 || vsIoKind_ == 6 || vsIoKind_ == 7 || vsIoKind_ == 8 || vsIoKind_ == 9 || vsIoKind_ == 10 || vsIoKind_ == 11 || vsIoKind_ == 12 || vsIoKind_ == 13 || vsIoKind_ == 14)))) {
		if (codeRom && codeRomSize) {
			soundRom_ = (unsigned char*)malloc(codeRomSize);
			if (soundRom_) {
				memcpy(soundRom_, codeRom, codeRomSize);
				soundRomSize_ = codeRomSize;
				/* sailormn: 0000-3FFF ROM、4000-7FFF バンク、8000-BFFF 未マップ、C000-FFFF RAM。汎用 64K blit は RAM 穴に ROM を残した。 */
				if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 5) {
					memset(mem_ + 0x8000, 0xff, 0x4000);
					memset(mem_ + 0xc000, 0x00, 0x4000);
					unsigned n = (codeRomSize < 0x4000u) ? codeRomSize : 0x4000u;
					memcpy(mem_, soundRom_, n);
				} else if (board_ == CEMU_AC_BOARD_TECMO16 && (tecmoOpl_ == 7 || tecmoOpl_ == 9)) {
					memset(mem_ + 0x8000, 0xff, 0x6000);
					memset(mem_ + 0xe000, 0x00, 0x2000);
					unsigned n = (codeRomSize < 0x4000u) ? codeRomSize : 0x4000u;
					memcpy(mem_, soundRom_, n);
				} else if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 8) {
					memset(mem_ + 0x8000, 0xff, 0x4000);
					memset(mem_ + 0xc000, 0x00, 0x0800);
					memset(mem_ + 0xc800, 0xff, 0x3000);
					memset(mem_ + 0xf800, 0x00, 0x0800);
					unsigned n = (codeRomSize < 0x4000u) ? codeRomSize : 0x4000u;
					memcpy(mem_, soundRom_, n);
				} else if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 10) {
					memset(mem_ + 0xc000, 0xff, 0x2000);
					memset(mem_ + 0xe000, 0x00, 0x2000);
					unsigned n = (codeRomSize < 0x8000u) ? codeRomSize : 0x8000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0x8000u)
						memset(mem_ + n, 0xff, 0x8000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 8) {
					memset(mem_ + 0xf800, 0x00, 0x800);
					unsigned n = (codeRomSize < 0x8000u) ? codeRomSize : 0x8000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0x8000u)
						memset(mem_ + n, 0xff, 0x8000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 9) {
					memset(mem_ + 0xe000, 0x00, 0x800);
					unsigned n = (codeRomSize < 0xe000u) ? codeRomSize : 0xe000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0xe000u)
						memset(mem_ + n, 0xff, 0xe000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 10) {
					memset(mem_ + 0xf800, 0x00, 0x800);
					unsigned n = (codeRomSize < 0xf800u) ? codeRomSize : 0xf800u;
					memcpy(mem_, soundRom_, n);
					if (n < 0xf800u)
						memset(mem_ + n, 0xff, 0xf800u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 12) {
					memset(mem_ + 0xf800, 0x00, 0x800);
					unsigned n = (codeRomSize < 0xf800u) ? codeRomSize : 0xf800u;
					memcpy(mem_, soundRom_, n);
					if (n < 0xf800u)
						memset(mem_ + n, 0xff, 0xf800u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 11) {
					memset(mem_ + 0x8000, 0x00, 0x800);
					unsigned n = (codeRomSize < 0x8000u) ? codeRomSize : 0x8000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0x8000u)
						memset(mem_ + n, 0xff, 0x8000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 7) {
					memset(mem_ + 0xc000, 0x00, 0x800);
					unsigned n = (codeRomSize < 0xc000u) ? codeRomSize : 0xc000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0xc000u)
						memset(mem_ + n, 0xff, 0xc000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 13) {
					memset(mem_ + 0xe000, 0x00, 0x800);
					unsigned n = (codeRomSize < 0xe000u) ? codeRomSize : 0xe000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0xe000u)
						memset(mem_ + n, 0xff, 0xe000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 14) {
					memset(mem_ + 0xd000, 0x00, 0x800);
					unsigned n = (codeRomSize < 0x8000u) ? codeRomSize : 0x8000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0x8000u)
						memset(mem_ + n, 0xff, 0x8000u - n);
					if (soundRomSize_ > 0x8000u) {
						unsigned src = 0x8000u + (unsigned)(bank_ & 1) * 0x4000u;
						unsigned bn = 0x5000u;
						if (src < soundRomSize_) {
							if (src + bn > soundRomSize_)
								bn = soundRomSize_ - src;
							memcpy(mem_ + 0x8000, soundRom_ + src, bn);
						}
					}
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 5) {
					memset(mem_ + 0xc000, 0x00, 0x2000);
					unsigned n = (codeRomSize < 0xc000u) ? codeRomSize : 0xc000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0xc000u)
						memset(mem_ + n, 0xff, 0xc000u - n);
				} else if (board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 1 || vsIoKind_ == 3 || vsIoKind_ == 4 || vsIoKind_ == 6)) {
					memset(mem_ + 0xc000, 0x00, 0x2000);
					unsigned n = (codeRomSize < 0x8000u) ? codeRomSize : 0x8000u;
					memcpy(mem_, soundRom_, n);
					if (n < 0x8000u)
						memset(mem_ + n, 0xff, 0x8000u - n);
				} else if (KonamiXmenMap() || KonamiPrmrsocrMap()) {
					/* MAME xmen/prmrsocr: 0000-7FFF 固定 ROM、C000-DFFF RAM、8000-BFFF は SetBank。 */
					memcpy(mem_, soundRom_, 0x8000u);
					memset(mem_ + 0xc000, 0, 0x2000);
				}
			}
		}
	}
	/* Sys16B: MAME は Z80 プログラムを 0000、バンク/UPD7759 データを 10000+ にパック。カタログはバンク ROM を "adpcm" とタグしがち — それでも soundRom_ へ載せ、ポート 40 が曲表を 8000-DFFF へページできるように。 */
	if (board_ == CEMU_AC_BOARD_SYS32 && codeRom && codeRomSize) {
		/* MAME soundcpu 4MB: プログラムを先頭 1MB にミラーし、type=pcm を 0x100000+。
		   A000 バンク窓がサンプルを RF5C68 RAM へコピーする。 */
		enum { kS32Sound = 0x400000u };
		if (soundRom_) {
			free(soundRom_);
			soundRom_ = NULL;
			soundRomSize_ = 0;
		}
		soundRom_ = (unsigned char*)malloc(kS32Sound);
		if (soundRom_) {
			memset(soundRom_, 0xff, kS32Sound);
			if (codeRomSize) {
				unsigned dest = 0;
				while (dest < 0x100000u) {
					unsigned n = codeRomSize;
					if (dest + n > 0x100000u)
						n = 0x100000u - dest;
					memcpy(soundRom_ + dest, codeRom, n);
					if (dest + codeRomSize <= dest)
						break;
					dest += codeRomSize;
				}
			}
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (!r->type || _stricmp(r->type, "pcm") != 0)
					continue;
				unsigned sz = 0;
				const unsigned char* d = CEmuZipFsFind(fs, r->name, &sz);
				if (!d || !sz)
					continue;
				unsigned dst = 0x100000u + (unsigned)r->offset;
				if (dst >= kS32Sound)
					continue;
				unsigned n = sz;
				if (dst + n > kS32Sound)
					n = kS32Sound - dst;
				memcpy(soundRom_ + dst, d, n);
				/* MAME ROM_LOAD_x2: 512KB を 1MB 枠へリロード */
				if (sz && dst + sz + n <= kS32Sound)
					memcpy(soundRom_ + dst + sz, d, n);
			}
			soundRomSize_ = kS32Sound;
		}
		memset(mem_ + 0xc000, 0, 0x4000);
		s32Bank_ = 0;
		/* A000 は OUT (A0) まで 64K イメージのまま。bank0 を先に当てると
		   arabfgt 0xA0 が STOPS になった。 */
	}
	if ((board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
		&& codeRom && codeRomSize) {
		/* Sys16B バンクビット割当はカタログ boardtype を優先 */
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
			/* カタログオフセットはバンク相対（aceattac 0/10000/20000）か絶対 soundcpu 番地（goldnaxe 0x10000）。"0x10000 を引く" ヒューリスティックを混ぜると相対 0 と 0x10000 が同じ枠に畳み、5358 多バンクタイトルが無音になった。 */
			unsigned boff = (r->offset > 0) ? (unsigned)r->offset : 0u;
			banks[nBank].data = data;
			banks[nBank].sz = sz;
			banks[nBank].off = boff;
			nBank++;
		}
		/* 到着先を解決: いずれかのバンクが 0 始まりならすべて 0x10000 バンク領域相対。さもなくば絶対 soundcpu オフセット。 */
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
		/* 同じサイズの第 2 Z80 ダンプも取る（shinobi epr-11268） */
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
				/* 固定 ROM 0000-7FFF は soundRom_ と一致しなければならない（カタログが先に 7751 を mem_ へ塗ったかも。ここでの SetBank は no-op）。 */
				{
					unsigned n = 0x8000u;
					if (n > soundRomSize_) n = soundRomSize_;
					memcpy(mem_, soundRom_, n);
				}
				/* 最初のバンク ROM からバンク窓を初期化 */
				if (need > 0x10000u) {
					unsigned n = 0x6000u;
					if (0x10000u + n > need) n = need - 0x10000u;
					memset(mem_ + 0x8000, 0xff, 0x6000);
					if (n) memcpy(mem_ + 0x8000, soundRom_ + 0x10000u, n);
				}
			}
		} else if (codeRomSize > 0) {
			/* 単体 32K プログラム（afighter）: それでも mem_ = code を保つ */
			unsigned n = codeRomSize > 0x8000u ? 0x8000u : codeRomSize;
			memcpy(mem_, codeRom, n);
		}
	}
	/* aerofgt sound_map: 固定 ROM は 0000-77FF だけ、7800-7FFF は RAM — 上の汎用 blit は ROM バイトを 8000-FFFF に置き、Z80 をバンク 1 に固定して本物バンク窓を届かなくした。fromanc2 は平坦 0000-DFFF ROM イメージ（32K バンク窓無し）。 */
	if (board_ == CEMU_AC_BOARD_VSYSTEM && (soundRom_ || vsIoKind_ == 2)) {
		/* サブタイプが vsIoKind_ を既定のままなら ISR ポートを嗅ぐ */
		if (vsIoKind_ == 0 && soundRom_ && soundRomSize_ > 0x3a) {
			if (soundRom_[0x39] == 0xdb && soundRom_[0x3a] == 0x18)
				vsIoKind_ = 1;
			else if (soundRom_[0x39] == 0xdb && soundRom_[0x3a] == 0x08)
				vsIoKind_ = 2;
			else if (soundRom_[0x38] == 0xf3 && soundRom_[0x3b] == 0xc3)
				vsIoKind_ = 2;
		}
		memset(mem_, 0, sizeof(mem_));
		if (vsIoKind_ == 2) {
			/* 64K/128K IM1;DI;LD SP（fromanc2/4）を 2MB ADPCM より優先。 */
			int pick = -1;
			if (fs) {
				for (int i = 0; i < fs->fileCount; i++) {
					const unsigned sz = fs->files[i].size;
					const uint8_t* d = fs->files[i].data;
					if (!d) continue;
					if (sz != 0x8000u && sz != 0x10000u && sz != 0x20000u)
						continue;
					if (d[0] == 0xed && d[1] == 0x56) {
						if (sz == 0x10000u) { pick = i; break; }
						if (pick < 0) pick = i;
					}
				}
			}
			if (pick >= 0) {
				unsigned n = fs->files[pick].size;
				if (n > 0xe000u) n = 0xe000u;
				memcpy(mem_, fs->files[pick].data, n);
			} else if (soundRom_) {
				unsigned n = soundRomSize_ < 0xe000u ? soundRomSize_ : 0xe000u;
				memcpy(mem_, soundRom_, n);
			}
			memset(mem_ + 0xe000, 0, 0x2000);
		} else if (vsIoKind_ == 3) {
			/* gunbird: 固定 ROM 0000-7FFF、バンク窓 8000-FFFF */
			unsigned n = soundRomSize_ < 0x8000u ? soundRomSize_ : 0x8000u;
			memcpy(mem_, soundRom_, n);
			SetBank(0);
		} else {
			/* aerofgt/pspikes: 0000-77FF ROM、7800 RAM、8000 バンク。
			   spinlbrk は 32K Z80 + 別 64K soundbank。pipedrm は 32K@0000 + 64K@10000。 */
			int z80 = -1, bankf = -1, big = -1;
			if (fs) {
				for (int i = 0; i < fs->fileCount; i++) {
					const unsigned sz = fs->files[i].size;
					const uint8_t* d = fs->files[i].data;
					if (!d) continue;
					if (sz == 0x20000u && d[0] == 0xed && d[1] == 0x56)
						big = i;
					else if (sz == 0x8000u && d[0] == 0xed && d[1] == 0x56)
						z80 = i;
					else if (sz == 0x10000u && d[0] == 0xed && d[1] == 0x56) {
						if (z80 < 0) z80 = i;
					} else if (sz == 0x10000u)
						bankf = i;
				}
			}
			if (vsIoKind_ == 4 && z80 >= 0) {
				uint8_t* img = (uint8_t*)malloc(0x20000u);
				if (img) {
					memset(img, 0, 0x20000u);
					memcpy(img, fs->files[z80].data, 0x8000u);
					if (bankf >= 0) {
						unsigned n = fs->files[bankf].size;
						if (n > 0x10000u) n = 0x10000u;
						memcpy(img + 0x10000u, fs->files[bankf].data, n);
					}
					if (soundRom_) free(soundRom_);
					soundRom_ = img;
					soundRomSize_ = 0x20000u;
				}
				memcpy(mem_, fs->files[z80].data, 0x7800u);
				memset(mem_ + 0x7800, 0, 0x800);
				/* 0670 は A=1 で OUT (04)。MAME set_entry(data&1)。バンク 1 = u3+0x8000 を先に載せる。 */
				SetBank(1);
			} else if (big >= 0) {
				unsigned sz = fs->files[big].size;
				uint8_t* img = (uint8_t*)malloc(sz);
				if (img) {
					memcpy(img, fs->files[big].data, sz);
					if (soundRom_) free(soundRom_);
					soundRom_ = img;
					soundRomSize_ = sz;
				}
				unsigned n = sz < 0x7800u ? sz : 0x7800u;
				memcpy(mem_, fs->files[big].data, n);
				memset(mem_ + 0x7800, 0, 0x800);
			} else if (z80 >= 0) {
				unsigned zs = fs->files[z80].size;
				unsigned n = zs < 0x7800u ? zs : 0x7800u;
				memcpy(mem_, fs->files[z80].data, n);
				memset(mem_ + 0x7800, 0, 0x800);
				if (bankf >= 0) {
					unsigned sz = fs->files[bankf].size;
					uint8_t* img = (uint8_t*)malloc(sz);
					if (img) {
						memcpy(img, fs->files[bankf].data, sz);
						if (soundRom_) free(soundRom_);
						soundRom_ = img;
						soundRomSize_ = sz;
					}
				}
			} else if (soundRom_) {
				unsigned n = soundRomSize_ < 0x7800u ? soundRomSize_ : 0x7800u;
				memcpy(mem_, soundRom_, n);
			}
		}
	}

	if (!loaded
		&& board_ != CEMU_AC_BOARD_NAMCO_SYS1
		&& board_ != CEMU_AC_BOARD_NAMCO_SYS86
		&& board_ != CEMU_AC_BOARD_NAMCO_WSG
		&& !(pcmRomSize_ && (board_ == CEMU_AC_BOARD_NAMCO_C352
			|| board_ == CEMU_AC_BOARD_NAMCO_SYS2))) {
		/* Model 2/3 / System24 ディスクパックはしばしば Z80 イメージ無し。それでも Open し、再生経路が FAIL_OPEN ではなく SILENT に分類できるように。 */
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

	/* Cotton の Z80 8A00 曲表はハードではバンク ROM が要る。ローカル cotton.zip: epr13860.a10 は s-prog の複製。opr13893.a11 は音声/PCM（Z80 曲表ではない）。8A00 は空のまま → REGSONLY。 */

	if (board_ == CEMU_AC_BOARD_ALPHA68K2 && fs && fs->fileCount > 0) {
		if (vsIoKind_ == 1) {
			/* mmpanic: 128K F3 ED 56。0000-5FFF ROM、6000-66FF RAM、8000-FFFF ROM。 */
			int pick = -1;
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz != 0x20000u) continue;
				if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56) {
					pick = i;
					break;
				}
			}
			if (pick >= 0) {
				const uint8_t* d = fs->files[pick].data;
				memcpy(mem_, d, 0x6000u);
				memset(mem_ + 0x6000, 0, 0x2000);
				memcpy(mem_ + 0x8000, d + 0x8000u, 0x8000u);
			}
		} else {
		/* MAME audiocpu は各 64K イメージが 0x20000*n の 512KiB 窓（間に穴）。ブートは DI / LD SP,$87FF / JP $0021 / OUT ($0E),2 / JP $C000 なので C000 はバンク 2（ROM+0x8000）であり 64K 1:1 memcpy ではない。 */
		int idx[8];
		int nIdx = 0;
		for (int i = 0; i < fs->fileCount && nIdx < (int)_countof(idx); i++) {
			const unsigned sz = fs->files[i].size;
			if (sz < 0x4000u || sz > 0x10000u) continue;
			idx[nIdx++] = i;
		}
		for (int a = 0; a < nIdx; a++) {
			for (int b = a + 1; b < nIdx; b++) {
				char pa[CEMU_ZIP_PATH], pb[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[idx[a]].path, -1,
					pa, (int)sizeof(pa), NULL, NULL);
				WideCharToMultiByte(CP_ACP, 0, fs->files[idx[b]].path, -1,
					pb, (int)sizeof(pb), NULL, NULL);
				int na = -1, nb = -1, ca = -1, cb = -1;
				for (const char* p = pa; *p; p++) {
					if (*p >= '0' && *p <= '9') {
						if (ca < 0) ca = 0;
						ca = ca * 10 + (*p - '0');
						na = ca;
					} else ca = -1;
				}
				for (const char* p = pb; *p; p++) {
					if (*p >= '0' && *p <= '9') {
						if (cb < 0) cb = 0;
						cb = cb * 10 + (*p - '0');
						nb = cb;
					} else cb = -1;
				}
				if (na > nb || (na == nb && _stricmp(pa, pb) > 0)) {
					int t = idx[a]; idx[a] = idx[b]; idx[b] = t;
				}
			}
		}
		if (nIdx > 1) {
			int boot = -1;
			for (int i = 0; i < nIdx; i++) {
				const unsigned char* d = fs->files[idx[i]].data;
				if (fs->files[idx[i]].size < 4u || !d) continue;
				/* skyadvnt/gangwars: DI; LD SP,$87FF。goldmedl/timesold は DI を省略 */
				if ((d[0] == 0xf3 && d[1] == 0x31 && d[2] == 0xff && d[3] == 0x87)
					|| (d[0] == 0x31 && d[1] == 0xff && d[2] == 0x87)) {
					boot = i;
					break;
				}
			}
			if (boot > 0) {
				const int t = idx[0];
				idx[0] = idx[boot];
				idx[boot] = t;
			}
		}
		/* goldmedl: 38/39/40/1 を 0x10000*n にパック（MAME）。ブートが DI 無し LD SP の 64K イメージ 4 本 — 0x20000 穴は使わない。 */
		int packed = 0;
		if (nIdx == 4) {
			packed = 1;
			for (int i = 0; i < nIdx; i++) {
				if (fs->files[idx[i]].size != 0x10000u)
					packed = 0;
			}
			const unsigned char* d0 = (packed && fs->files[idx[0]].size >= 3u)
				? fs->files[idx[0]].data : NULL;
			if (!(d0 && d0[0] == 0x31 && d0[1] == 0xff && d0[2] == 0x87))
				packed = 0;
		}
		if (packed && nIdx > 1) {
			int num[8];
			for (int i = 0; i < nIdx; i++) {
				char pa[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[idx[i]].path, -1,
					pa, (int)sizeof(pa), NULL, NULL);
				int n = -1, c = -1;
				for (const char* p = pa; *p; p++) {
					if (*p >= '0' && *p <= '9') {
						if (c < 0) c = 0;
						c = c * 10 + (*p - '0');
						n = c;
					} else c = -1;
				}
				num[i] = n;
			}
			int out[8];
			int nOut = 1;
			out[0] = idx[0];
			for (int i = 1; i < nIdx; i++)
				if (num[i] > num[0]) out[nOut++] = idx[i];
			for (int i = 1; i < nIdx; i++)
				if (num[i] < num[0]) out[nOut++] = idx[i];
			if (nOut == nIdx) {
				for (int i = 0; i < nIdx; i++)
					idx[i] = out[i];
			}
		}
		if (nIdx > 0) {
			if (soundRom_) { free(soundRom_); soundRom_ = NULL; soundRomSize_ = 0; }
			const unsigned need = 0x80000u;
			const unsigned stride = packed ? 0x10000u : 0x20000u;
			uint8_t* p = (uint8_t*)calloc(1, need);
			if (p) {
				soundRom_ = p;
				soundRomSize_ = need;
				for (int i = 0; i < nIdx; i++) {
					unsigned off = (unsigned)i * stride;
					if (off >= need) break;
					unsigned n = fs->files[idx[i]].size;
					if (off + n > need) n = need - off;
					memcpy(soundRom_ + off, fs->files[idx[i]].data, n);
				}
				memset(mem_, 0, 0x10000);
				memcpy(mem_, soundRom_, 0x8000);
				loaded = 1;
				codeRom = soundRom_;
				codeRomSize = need;
			}
		}
		}
	}

	cpu_->reset(mem_);
	cpu_->r.pc = 0;
	/* 本物 Z80 電源投入は A を 0xFF のまま。Ay_Cpu::reset はレジスタをゼロ。m99 族 M72（bbmanw/poundfor）はブートで PUSH AF し A をコマンド AND マスクとして F4DC に置く — A=0 だと全ラッチバイトが空に見える。 */
	if (board_ == CEMU_AC_BOARD_IREM_M72)
		cpu_->r.b.a = 0xff;
	cpuCycles_ = 0;
	soundCmd_ = 0;
	soundCmdPending_ = 0;
	irqPulse_ = 0;
	wsgNmiEnable_ = 0;
	if (board_ == CEMU_AC_BOARD_NAMCO_WSG && sys16RomBoard_ != 0x5047u) {
		mem_[0x9101] = 0;
		mem_[0x8c01] = 0;
		/* Dig Dug（LD SP,$8B80）: ブート中に NMI が要る。Galaga/Bosco は busy を保持し、早い NMI が ROM チェックサムを壊さないように。 */
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
	/* 早い OUT オペコードから M72 代替 I/O（YM2151 @40/41）を嗅ぐ */
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
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		alphaNmiMask_ = 0;
		alphaPaLatch_ = 0;
		alphaYmAddr_ = 0;
		alphaOpllAddr_ = 0;
		memset(s_alphaOpllRegs, 0, sizeof(s_alphaOpllRegs));
		s_alphaOpllWrites = 0;
		if (alphaOpll_)
			OPLL_reset((OPLL*)alphaOpll_);
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		&& snkMapKind_ < 4
		&& mem_[0] == 0xf3 && mem_[1] == 0x3a && mem_[2] == 0x00 && mem_[3] == 0xe0)
		snkMapKind_ = 1;
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2
		&& snkMapKind_ < 4
		&& mem_[0] == 0xc3 && mem_[1] == 0x00 && mem_[2] == 0x01) {
		/* dbz/dbz2 と ultraman はどちらも JP $0100。NMI ベクタで分ける */
		if (mem_[0x66] == 0xc3 && mem_[0x67] == 0x4c && mem_[0x68] == 0x09)
			snkMapKind_ = 3;
		else if (snkMapKind_ != 3)
			snkMapKind_ = 2;
	}
	/* kikikai audiocpu: DI;IM1;JP 0068。線形 32K — SetBank は 0000-3FFF を 4000-7FFF へミラーしブートチェックサムがハング。 */
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& taitoOpmMap_ == 0
		&& mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x56
		&& mem_[3] == 0xc3 && mem_[4] == 0xa1 && mem_[5] == 0x01) {
		/* insectx: DI;IM1;JP 01A1。サブタイプ取りこぼしだと map 0 のまま D000 が ROM で検査が JP 0000。 */
		taitoOpmMap_ = 7;
		bankBase_ = 0;
		bankSize_ = 0x8000u;
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& taitoOpmMap_ == 0
		&& mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x56
		&& mem_[3] == 0xc3 && mem_[4] == 0x68 && mem_[5] == 0x00) {
		taitoOpmMap_ = 2;
		bankBase_ = 0;
		bankSize_ = 0x8000u;
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& taitoOpmMap_ == 0
		&& mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x56
		&& mem_[3] == 0x3e && mem_[5] == 0x32 && mem_[6] == 0x00 && mem_[7] == 0xa0
		&& mem_[0x0b] == 0x3e) {
		/* tetrista/cameltrya/viofight: LD (A000) のあと JP 00AD。4000-7FFF は ROM+4000。
		   SetBank(0) は ROM[0:4000] を重ね曲表を潰す。champwr は 000B C3 AA 01。 */
		bankBase_ = 0x4000u;
		bankSize_ = 0x4000u;
		bankLoaded_ = 1;
		memset(mem_ + 0x8000, 0, 0x1000);
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x56
		&& mem_[5] == 0x32 && mem_[6] == 0x00 && mem_[7] == 0xe0
		&& mem_[0x0b] == 0xc3 && mem_[0x0c] == 0xaa && mem_[0x0d] == 0x01) {
		/* fhawk: ld (E000),a と JP 01AA。champwr は (A000) で同じ stub — 誤って map 12 にしない。 */
		taitoOpmMap_ = 12;
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& mem_[0] == 0xc3 && mem_[1] == 0xfb && mem_[2] == 0x00) {
		/* kurikint audiocpu: JP 00FB。 */
		taitoOpmMap_ = 13;
		bankBase_ = 0;
		bankSize_ = 0x8000u;
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x5e) {
		/* Taito L 1cpu palamed/cachat: DI;IM 2。 */
		taitoOpmMap_ = 14;
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& mem_[0] == 0xc3
		&& ((mem_[1] == 0x89 && mem_[2] == 0x00)
			|| (mem_[1] == 0xd0 && mem_[2] == 0x03))) {
		/* horshoes JP 0089 / flipull JP 03D0。F3 ED 5E が無いと map 0 のまま 780B が bank0 の JR $780B 無限ループ。 */
		taitoOpmMap_ = 14;
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1 && vsIoKind_ == 4 && mem_) {
		/* hangonjr: CALL $A000 はバンク ROM（hoot zip に無い）。NOP して 0110 アトラクトへ。 */
		if (mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x56
			&& mem_[0x129] == 0xcd && mem_[0x12a] == 0x00 && mem_[0x12b] == 0xa0) {
			mem_[0x129] = 0x00;
			mem_[0x12a] = 0x00;
			mem_[0x12b] = 0x00;
			/* 01B9 は C200 が 0 だと 512F を飛ばす。常時 CALL 512F;JP 01E9 にして
			   0A9E の VDP スプライトダンプに音源 tick を奪われないようにする。 */
			if (mem_[0x1b9] == 0x3a && mem_[0x1ba] == 0x00 && mem_[0x1bb] == 0xc2) {
				mem_[0x1b9] = 0xcd; mem_[0x1ba] = 0x2f; mem_[0x1bb] = 0x51;
				mem_[0x1bc] = 0xc3; mem_[0x1bd] = 0xe9; mem_[0x1be] = 0x01;
			}
		}
		/* transfrm hoot は IC2 音源 blob を 0000 に置く。実機は 8000 バンク。
		   $8084 は FD00 コマンド受付のみ（$80 は RET M、bit7=0 は 852C ミュート）。
		   毎 vblank は $8004（CALL 8084 のあと FD08/FD88 を歩いて OUT 7B/7F）。 */
		if (mem_[0] == 0x5c && mem_[4] == 0xcd && mem_[5] == 0x84 && mem_[6] == 0x80) {
			memcpy(mem_ + 0x8000, mem_, 0x8000);
			mem_[0] = 0xed; mem_[1] = 0x56;
			mem_[2] = 0x31; mem_[3] = 0x00; mem_[4] = 0xd0;
			mem_[5] = 0xfb;
			mem_[6] = 0x18; mem_[7] = 0xfe;
			mem_[0x38] = 0xcd; mem_[0x39] = 0x04; mem_[0x3a] = 0x80;
			mem_[0x3b] = 0xfb;
			mem_[0x3c] = 0xc9;
			mem_[0xfd00] = 0x80;
		}
	}
	if (!(board_ == CEMU_AC_BOARD_FLSTORY && (taitoOpmMap_ == 1 || taitoOpmMap_ == 3))
		&& !(board_ == CEMU_AC_BOARD_TAITO_SJ
			&& (vsIoKind_ == 4 || vsIoKind_ == 5 || vsIoKind_ == 6
				|| vsIoKind_ == 7 || vsIoKind_ == 8 || vsIoKind_ == 9
				|| vsIoKind_ == 10 || vsIoKind_ == 11 || vsIoKind_ == 12
				|| vsIoKind_ == 13 || vsIoKind_ == 14
				|| vsIoKind_ == 15 || vsIoKind_ == 16
				|| vsIoKind_ == 17 || vsIoKind_ == 18 || vsIoKind_ == 19 || vsIoKind_ == 20 || vsIoKind_ == 21 || vsIoKind_ == 22 || vsIoKind_ == 23 || vsIoKind_ == 24))
		&& !(board_ == CEMU_AC_BOARD_GNG && (gngCommandoMap_ == 2 || gngCommandoMap_ == 3 || gngCommandoMap_ == 4 || gngCommandoMap_ == 5 || gngCommandoMap_ == 6))
		&& !(board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 1 || vsIoKind_ == 2 || vsIoKind_ == 3 || vsIoKind_ == 4 || vsIoKind_ == 5 || vsIoKind_ == 6 || vsIoKind_ == 7 || vsIoKind_ == 8 || vsIoKind_ == 9 || vsIoKind_ == 10 || vsIoKind_ == 11 || vsIoKind_ == 12 || vsIoKind_ == 13 || vsIoKind_ == 14 || vsIoKind_ == 15))
		&& !(board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE)
		&& !(board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT && vsIoKind_ == 1)
		&& !(board_ == CEMU_AC_BOARD_TERRACRE && terracreMap_ >= 3)
		&& !(board_ == CEMU_AC_BOARD_KONAMI_K7232 && konamiK7232Map_ >= 3)
		&& !(board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ >= 1)
		&& !(board_ == CEMU_AC_BOARD_TECMO16 && (tecmoOpl_ == 0 || tecmoOpl_ == 2 || tecmoOpl_ == 3 || tecmoOpl_ == 11 || tecmoOpl_ == 12 || tecmoOpl_ == 13))
		&& !(board_ == CEMU_AC_BOARD_VSYSTEM && (vsIoKind_ == 2 || vsIoKind_ == 4))
		&& !(board_ == CEMU_AC_BOARD_TOAPLAN1 && (toaplanKaneko_ == 4 || toaplanKaneko_ == 5))
		&& !(board_ == CEMU_AC_BOARD_ALPHA68K2 && vsIoKind_ == 1)
		&& !(board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && (snkMapKind_ == 1 || snkMapKind_ == 2 || snkMapKind_ == 3 || snkMapKind_ >= 4))
		&& !(board_ == CEMU_AC_BOARD_SEGA_SYS1 && vsIoKind_ >= 1)
		&& !(board_ == CEMU_AC_BOARD_KONAMI_HCASTLE && vsIoKind_ == 1)
		&& !(board_ == CEMU_AC_BOARD_SYS18 && vsIoKind_ == 1)
		&& !(board_ == CEMU_AC_BOARD_TAITO_OPM && mem_[0] == 0xf3
			&& mem_[0x0b] == 0x3e && mem_[5] == 0x32 && mem_[7] == 0xa0)
		&& taitoOpmMap_ != 2 && taitoOpmMap_ != 3 && taitoOpmMap_ != 4
		&& taitoOpmMap_ != 5 && taitoOpmMap_ != 6 && taitoOpmMap_ != 7
		&& taitoOpmMap_ != 8 && taitoOpmMap_ != 9 && taitoOpmMap_ != 11
		&& taitoOpmMap_ != 12 && taitoOpmMap_ != 13 && taitoOpmMap_ != 14
		&& taitoOpmMap_ != 15 && taitoOpmMap_ != 18)
		SetBank(0);
	if (board_ == CEMU_AC_BOARD_FLSTORY && taitoOpmMap_ == 1)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ == 2)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ == 3)
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TOAPLAN1 && (toaplanKaneko_ == 0 || toaplanKaneko_ == 4))
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TOAPLAN1 && toaplanKaneko_ == 5)
		memset(mem_ + 0x8800, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_SNK_OPL && snkMapKind_ == 4)
		memset(mem_ + 0xc000, 0, 0x1000);
	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 0)
		memset(mem_ + 0xf000, 0, 0xc00); /* MAME tecmo16: RAM F000-FBFF */
	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 11)
		memset(mem_ + 0xf000, 0, 0x800); /* MAME bluehawk: RAM F000-F7FF */
	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 12)
		memset(mem_ + 0xc000, 0, 0x2000); /* MAME powerins: RAM C000-DFFF */
	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 13)
		memset(mem_ + 0x8000, 0, 0x800); /* MAME deco32 z80_sound_map: RAM 8000-87FF */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiJoeMap())
		memset(mem_ + 0xf000, 0, 0x800); /* MAME gijoe/lethal: RAM F000-F7FF */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiXmenMap())
		memset(mem_ + 0xc000, 0, 0x2000); /* MAME xmen: RAM C000-DFFF */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiPrmrsocrMap())
		memset(mem_ + 0xc000, 0, 0x2000); /* MAME prmrsocr: RAM C000-DFFF */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiGlfMap())
		memset(mem_ + 0xf000, 0, 0x800); /* MAME glfgreat: RAM F000-F7FF */
	if (board_ == CEMU_AC_BOARD_KONAMI_PCM && KonamiRollergMap())
		memset(mem_ + 0x8000, 0, 0x800); /* MAME rollerg: RAM 8000-87FF */
	if (board_ == CEMU_AC_BOARD_KONAMI_HCASTLE)
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610)
		memset(mem_ + 0xc000, 0, 0x2000);
	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 2)
		memset(mem_ + 0x8000, 0, 0x800); /* MAME gemini: RAM 8000-87FF */
	if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 3)
		memset(mem_ + 0xf000, 0, 0x800); /* MAME spbactn: RAM F000-F7FF */
	if (board_ == CEMU_AC_BOARD_FLSTORY && taitoOpmMap_ == 3)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 4)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 5)
		memset(mem_ + 0x6000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 6)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 7)
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 8)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 9)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 10)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 12)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 13)
		memset(mem_ + 0x2000, 0, 0x400);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 14)
		memset(mem_ + 0xe000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 15)
		memset(mem_ + 0x2000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 16)
		memset(mem_ + 0xc000, 0, 0x1000);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 17)
		memset(mem_ + 0x4000, 0, 0x400);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 18)
		memset(mem_ + 0x4000, 0, 0x400);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 19)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 20)
		memset(mem_ + 0x4000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 22) {
		/* カタログ offset が全部 0 だと 16K×3 が先頭で潰れる。リセットが 3E 00 32 44 E0 の面を 0000 へ。 */
		if (fs) {
			for (int i = 0; i < fs->fileCount; i++) {
				const unsigned sz = fs->files[i].size;
				const uint8_t* d = fs->files[i].data;
				if (!d || sz < 0x2000u)
					continue;
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1,
					pathA, (int)sizeof(pathA), NULL, NULL);
				unsigned off = 0xffffu;
				if (d[0] == 0x3eu && d[1] == 0x00u && d[2] == 0x32u && d[3] == 0x44u)
					off = 0;
				else if (strstr(pathA, "02.8") || strstr(pathA, "l02") || strstr(pathA, "h02"))
					off = 0x4000u;
				else if (strstr(pathA, "01.7") || strstr(pathA, "l01") || strstr(pathA, "h01"))
					off = 0x8000u;
				if (off > 0x8000u)
					continue;
				unsigned n = sz;
				if (off + n > 0xc000u)
					n = 0xc000u - off;
				if (n)
					memcpy(mem_ + off, d, n);
			}
		}
		memset(mem_ + 0xc000, 0, 0x2000);
		memset(mem_ + 0xe000, 0, 0x200);
		/* 00E8 JP 7307 は C000 塗り＋74D3 の映像待ち。ホストに K005849 が無いので 012A（LD SP,DF60）へ。 */
		if (mem_[0x00e8] == 0xc3u && mem_[0x00e9] == 0x07u && mem_[0x00ea] == 0x73u
			&& mem_[0x012a] == 0x31u && mem_[0x012b] == 0x60u && mem_[0x012c] == 0xdfu) {
			mem_[0x00e9] = 0x2au;
			mem_[0x00ea] = 0x01u;
		}
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 24) {
		/* 4K masao-5.rom = DI; LD HL,2000; JP 0140。メイン ROM を先に載せると Z80 がゴミで固まる。 */
		if (fs && ge) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (!r->name[0]) continue;
				unsigned sz = 0;
				const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
				if (!data || sz < 0x1000u) continue;
				if (data[0] != 0xf3u || data[1] != 0x21u || data[2] != 0x00u || data[3] != 0x20u)
					continue;
				memset(mem_, 0, 0x10000);
				memcpy(mem_, data, 0x1000u);
				break;
			}
		}
		memset(mem_ + 0x2000, 0, 0x400);
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 23) {
		memset(mem_ + 0xd000, 0, 0x1000);
		memset(mem_ + 0xe000, 0, 0x1000);
		/* 004A JP 1BF1 は VRAM 照合と 1B97 チェックサム。004A を 0050 の RAM/キュー初期化へ。 */
		if (mem_[0x004a] == 0xc3u && mem_[0x004b] == 0xf1u && mem_[0x004c] == 0x1bu
			&& mem_[0x0050] == 0x21u && mem_[0x0051] == 0x00u && mem_[0x0052] == 0xe0u) {
			mem_[0x004b] = 0x50u;
			mem_[0x004c] = 0x00u;
		}
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 21) {
		memset(mem_ + 0xe000, 0, 0x2000);
		mem_[0xe001] = 0x10u;
		/* JP ABC0 を飛ばし 0220 = CALL DAF3 / RST 28 / JP E800。07D4 は RST38。DA04 が bit7。 */
		if (mem_[0] == 0xc3u && mem_[1] == 0xc0u && mem_[2] == 0xabu
			&& mem_[0x220] == 0x31u && mem_[0x221] == 0x50u && mem_[0x222] == 0xe7u) {
			mem_[1] = 0x20u;
			mem_[2] = 0x02u;
			mem_[0x0223] = 0xcdu; mem_[0x0224] = 0xf3u; mem_[0x0225] = 0xdau;
			mem_[0x0226] = 0xefu;
			mem_[0x0227] = 0xc3u; mem_[0x0228] = 0x00u; mem_[0x0229] = 0xe8u;
			mem_[0xe800] = 0xcdu; mem_[0xe801] = 0x96u; mem_[0xe802] = 0x08u;
			mem_[0xe803] = 0x0eu; mem_[0xe804] = 0x00u;
			mem_[0xe805] = 0x1eu; mem_[0xe806] = 0x00u;
			mem_[0xe807] = 0x3eu; mem_[0xe808] = (uint8_t)titleCode;
			mem_[0xe809] = 0xcdu; mem_[0xe80a] = 0x04u; mem_[0xe80b] = 0xdau;
			mem_[0xe80c] = 0x18u; mem_[0xe80d] = 0xfeu;
		}
		/* combh: RST 28 / CALL 4C58 mute / RST 28 / CALL 4B90 / HALT+4C84。 */
		if (mem_[0] == 0x31u && mem_[1] == 0xa0u && mem_[2] == 0xe7u
			&& mem_[0x4b90] == 0x0eu && mem_[0x4c84] == 0xddu) {
			mem_[0xe800] = 0xefu;
			mem_[0xe801] = 0xcdu; mem_[0xe802] = 0x58u; mem_[0xe803] = 0x4cu;
			mem_[0xe804] = 0xefu;
			mem_[0xe805] = 0x0eu; mem_[0xe806] = 0x00u;
			mem_[0xe807] = 0x16u; mem_[0xe808] = 0x00u;
			mem_[0xe809] = 0x1eu; mem_[0xe80a] = 0x00u;
			mem_[0xe80b] = 0x3eu; mem_[0xe80c] = (uint8_t)titleCode;
			mem_[0xe80d] = 0xcdu; mem_[0xe80e] = 0x90u; mem_[0xe80f] = 0x4bu;
			mem_[0xe810] = 0x76u;
			mem_[0xe811] = 0xcdu; mem_[0xe812] = 0x84u; mem_[0xe813] = 0x4cu;
			mem_[0xe814] = 0x18u; mem_[0xe815] = 0xfau;
		}
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232 && konamiK7232Map_ == 6 && fs) {
		/* MAME flkatck/wecleman: 32K 音源 Z80 は 0000-7FFF。カタログ mx5000 は 669_m02 を offset 0x8000 に書き RAM 窓へ載せリセットが 00 埋め。 */
		int pick = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d || sz != 0x8000u) continue;
			if (d[0] == 0x06 && d[1] == 0x00 && d[2] == 0x21
				&& d[3] == 0x00 && d[4] == 0x80) {
				pick = i;
				break;
			}
		}
		if (pick >= 0) {
			memcpy(mem_, fs->files[pick].data, 0x8000u);
			codeRom = fs->files[pick].data;
			codeRomSize = fs->files[pick].size;
			loaded++;
		}
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232 && konamiK7232Map_ == 7 && fs) {
		/* MAME hexion: 128K Z80 @0000-7FFF + 8K×16 バンク @8000。OKI 256K。 */
		int pick = -1, oki = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d) continue;
			if (pick < 0 && sz == 0x20000u && d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56)
				pick = i;
			if (oki < 0 && sz == 0x40000u)
				oki = i;
		}
		if (pick >= 0) {
			const uint8_t* d = fs->files[pick].data;
			const unsigned sz = fs->files[pick].size;
			memcpy(mem_, d, 0xa000u);
			if (soundRom_) free(soundRom_);
			soundRom_ = (uint8_t*)malloc(sz);
			if (soundRom_) {
				memcpy(soundRom_, d, sz);
				soundRomSize_ = sz;
			} else
				soundRomSize_ = 0;
			codeRom = d;
			codeRomSize = sz;
			loaded++;
			bankLoaded_ = 1;
		}
		if (oki >= 0) {
			if (pcmRom_) free(pcmRom_);
			pcmRomSize_ = fs->files[oki].size;
			pcmRom_ = (uint8_t*)malloc(pcmRomSize_);
			if (pcmRom_) {
				memcpy(pcmRom_, fs->files[oki].data, pcmRomSize_);
				if (pcm_)
					pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
			} else
				pcmRomSize_ = 0;
		}
		memset(mem_ + 0xa000, 0, 0x2000);
		memset(mem_ + 0xc000, 0, 0x2000);
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232 && konamiK7232Map_ >= 3 && konamiK7232Map_ != 7)
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && vsIoKind_ != 1 && vsIoKind_ != 2)
		memset(mem_ + 0x8000, 0, 0x400);
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && vsIoKind_ == 2) {
		/* MAME init_hustler / decode_frogger_sound: hustler.6 先頭 0x800 は D0/D1 入れ替え。hustler.7 は平文 @0800。 */
		if (fs) {
			static const char* const kHus[2] = { "hustler.6", "hustler.7" };
			unsigned off = 0;
			for (int i = 0; i < 2; i++) {
				unsigned sz = 0;
				const unsigned char* data = CEmuZipFsFind(fs, kHus[i], &sz);
				if (!data || sz < 2u || data == (const unsigned char*)1)
					continue;
				unsigned n = sz;
				if (off + n > 0x1000u)
					n = 0x1000u - off;
				if (n)
					memcpy(mem_ + off, data, n);
				off += 0x800u;
			}
		}
		for (unsigned i = 0; i < 0x800u; i++) {
			const uint8_t b = mem_[i];
			mem_[i] = (uint8_t)((b & 0xfcu) | (uint8_t)((b & 1u) << 1) | (uint8_t)((b >> 1) & 1u));
		}
		memset(mem_ + 0x4000, 0, 0x400);
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT && (vsIoKind_ == 1 || CEmuAcIsMegazone(ge))) {
		vsIoKind_ = 1;
		/* カタログ type=sub の 319e01.3a（I8039 4K）が 0000 を上書きする。Z80 は 319e02.6d 8K AF 32 01 C0。 */
		if (fs) {
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, "319e02.6d", &sz);
			if (data && data != (const unsigned char*)1 && sz >= 0x2000u)
				memcpy(mem_, data, 0x2000u);
			else {
				for (int i = 0; i < fs->fileCount; i++) {
					if (fs->files[i].size == 0x2000u
						&& fs->files[i].data
						&& fs->files[i].data[0] == 0xafu
						&& fs->files[i].data[1] == 0x32u
						&& fs->files[i].data[2] == 0x01u
						&& fs->files[i].data[3] == 0xc0u) {
						memcpy(mem_, fs->files[i].data, 0x2000u);
						break;
					}
				}
			}
		}
		memset(mem_ + 0xe000, 0, 0x800);
		mem_[0xe00d] = 0x04;
		mem_[0xe00e] = 0x04;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE && vsIoKind_ == 1) {
		/* MAME decode_frogger_sound: 608/609/610 を 0000/0800/1000 へ。先頭 0x800 は D0/D1 入れ替え。 */
		static const char* const kFrog[3] = { "frogger.608", "frogger.609", "frogger.610" };
		unsigned off = 0;
		for (int i = 0; i < 3; i++) {
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, kFrog[i], &sz);
			if (!data || sz < 2u || data == (const unsigned char*)1)
				continue;
			unsigned n = sz;
			if (off + n > 0x1800u)
				n = 0x1800u - off;
			if (n)
				memcpy(mem_ + off, data, n);
			off += 0x800u;
		}
		for (unsigned i = 0; i < 0x800u; i++) {
			const uint8_t b = mem_[i];
			mem_[i] = (uint8_t)((b & 0xfcu) | (uint8_t)((b & 1u) << 1) | (uint8_t)((b >> 1) & 1u));
		}
		memset(mem_ + 0x4000, 0, 0x400);
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE && terracreMap_ == 6 && fs) {
		/* MAME ROM_LOAD 11.15b @0000 + 12.17b @4000。zip は tc2a_15b.bin / tc2a_17b.bin。 */
		static const char* const kTcLo[2] = { "tc2a_15b.bin", "11.15b" };
		static const char* const kTcHi[2] = { "tc2a_17b.bin", "12.17b" };
		for (int i = 0; i < 2; i++) {
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, kTcLo[i], &sz);
			if (data && data != (const unsigned char*)1 && sz >= 0x1000u) {
				unsigned n = sz > 0x4000u ? 0x4000u : sz;
				memcpy(mem_, data, n);
				break;
			}
		}
		for (int i = 0; i < 2; i++) {
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, kTcHi[i], &sz);
			if (data && data != (const unsigned char*)1 && sz >= 0x1000u) {
				unsigned n = sz > 0x4000u ? 0x4000u : sz;
				memcpy(mem_ + 0x4000, data, n);
				break;
			}
		}
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE && (terracreMap_ == 0 || terracreMap_ == 6))
		memset(mem_ + 0xc000, 0, 0x1000);
	if (board_ == CEMU_AC_BOARD_HANGON && vsIoKind_ == 1)
		memset(mem_ + 0xf800, 0, 0x800);
	if (CEmuAcIsOutrunb(ge) && mem_) {
		/* MAME segaorun.cpp init_outrunb: Z80 は bit5/6 入れ替え。復号後は公式 epr-10187 と同じ DI;IM1。 */
		unsigned n = 0x10000u;
		for (unsigned i = 0; i < n; i++) {
			const uint8_t b = mem_[i];
			mem_[i] = (uint8_t)((b & 0x9fu) | (uint8_t)((b & 0x20u) << 1) | (uint8_t)((b & 0x40u) >> 1));
		}
		if (soundRom_ && soundRomSize_) {
			for (unsigned i = 0; i < soundRomSize_; i++) {
				const uint8_t b = soundRom_[i];
				soundRom_[i] = (uint8_t)((b & 0x9fu) | (uint8_t)((b & 0x20u) << 1) | (uint8_t)((b & 0x40u) >> 1));
			}
		}
		if (fs) {
			/* MAME: a-6/a-5/a-4 は 8K + CONTINUE 8K @+0x10000（BANK_512 隙間）。 */
			static const char* const kPcm[3] = { "a-6.bin", "a-5.bin", "a-4.bin" };
			uint8_t* lay = (uint8_t*)malloc(0x80000u);
			if (lay) {
				memset(lay, 0xff, 0x80000u);
				for (int bnk = 0; bnk < 3; bnk++) {
					unsigned sz = 0;
					const unsigned char* data = CEmuZipFsFind(fs, kPcm[bnk], &sz);
					if (!data || data == (const unsigned char*)1 || sz < 0x10000u)
						continue;
					const unsigned dst = (unsigned)bnk * 0x20000u;
					memcpy(lay + dst, data, 0x8000u);
					memcpy(lay + dst + 0x10000u, data + 0x8000u, 0x8000u);
				}
				if (pcmRom_) free(pcmRom_);
				pcmRom_ = lay;
				pcmRomSize_ = 0x80000u;
				if (pcm_)
					pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
			}
		}
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE && (terracreMap_ == 3 || terracreMap_ == 5))
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TERRACRE && terracreMap_ == 4)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_GNG && (gngCommandoMap_ == 2 || gngCommandoMap_ == 3))
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_GNG && gngCommandoMap_ == 4)
		memset(mem_ + 0x4000, 0, 0x400);
	if (board_ == CEMU_AC_BOARD_GNG && gngCommandoMap_ == 5)
		memset(mem_ + 0x8000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_GNG && gngCommandoMap_ == 6) {
		memset(mem_ + 0x8000, 0, 0x1000);
		if (soundRom_ && soundRomSize_ >= 0x10000u)
			memcpy(mem_ + 0xc000, soundRom_ + 0xc000, 0x4000);
		else if (soundRom_ && soundRomSize_ >= 0xc000u)
			memcpy(mem_ + 0xc000, soundRom_ + 0xc000, soundRomSize_ - 0xc000u);
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (mem_[0] == 0xc3 && mem_[1] == 0xe5 && mem_[2] == 0x01)
			vsIoKind_ = 1;
		if (vsIoKind_ == 1)
			memset(mem_ + 0x4000, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 13)
		memset(mem_ + 0xe000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 14) {
		memset(mem_ + 0xd000, 0, 0x800);
		if (soundRom_ && soundRomSize_ > 0x8000u) {
			unsigned src = 0x8000u + (unsigned)(bank_ & 1) * 0x4000u;
			unsigned n = 0x5000u;
			if (src < soundRomSize_) {
				if (src + n > soundRomSize_)
					n = soundRomSize_ - src;
				memcpy(mem_ + 0x8000, soundRom_ + src, n);
			}
		}
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 0) {
		if (soundRom_ && soundRomSize_ >= 0xc000u)
			memcpy(mem_, soundRom_, 0xc000u);
		memset(mem_ + 0xc000, 0, 0x800);
		if (qsKabukiData_)
			qsKabuki_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 15) {
		memset(mem_ + 0xc000, 0, 0x4000);
		if (soundRom_ && soundRomSize_ >= 0x8000u)
			memcpy(mem_, soundRom_, 0x8000u);
		if (soundRom_ && soundRomSize_ > 0x8000u) {
			unsigned src = 0x8000u + (unsigned)(bank_ & 0x0f) * 0x4000u;
			unsigned n = 0x4000u;
			if (src < soundRomSize_) {
				if (src + n > soundRomSize_)
					n = soundRomSize_ - src;
				memcpy(mem_ + 0x8000, soundRom_ + src, n);
			}
		}
		if (qsKabukiData_ && soundRomSize_ >= 0x8000u)
			qsKabuki_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 8) {
		memset(mem_ + 0xf800, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 9) {
		memset(mem_ + 0xe000, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 10) {
		memset(mem_ + 0xf800, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 12) {
		memset(mem_ + 0xf800, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 11) {
		memset(mem_ + 0x8000, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 7) {
		memset(mem_ + 0xc000, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 5) {
		memset(mem_ + 0xc000, 0, 0x2000);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && (vsIoKind_ == 1 || vsIoKind_ == 3 || vsIoKind_ == 4 || vsIoKind_ == 6)) {
		memset(mem_ + 0xc000, 0, (vsIoKind_ == 6) ? 0x800 : 0x2000);
		SetBank(0);
	}
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 2)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 11) {
		/* カタログは 8K code ROM 5 本を連結。5 本目は A000（MAME 0000-7FFF + A000-BFFF）。8000-87FF は作業 RAM。 */
		if (soundRom_) {
			unsigned n = (soundRomSize_ < 0x8000u) ? soundRomSize_ : 0x8000u;
			memcpy(mem_, soundRom_, n);
			if (soundRomSize_ > 0x8000u) {
				unsigned b = soundRomSize_ - 0x8000u;
				if (b > 0x2000u) b = 0x2000u;
				memcpy(mem_ + 0xa000, soundRom_ + 0x8000u, b);
			}
		}
		memset(mem_ + 0x8000, 0, 0x800);
		mem_[0x85A8] = 0xff;
		/* $073F（ld a,$50 / rst $20）を NOP しない。0000-7FFF チェックサムは 0。その 3 バイトをパッチすると合計が失敗し $06E8 でハング。 */
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 2) {
		if (mem_[4] == 0xbd && mem_[5] == 0x00) {
			memset(mem_ + 0xa800, 0, 0x1800);
			mem_[0xa700] = 0xdf;
		} else
			mem_[0x9fff] = 0xff;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 8)
		memset(mem_ + 0x6000, 0, 0x2000);
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 11)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 9)
		memset(mem_ + 0xc000, 0, 0x2000);
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 12)
		memset(mem_ + 0x8000, 0, 0x2000);
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 13) {
		memset(mem_ + 0xc000, 0, 0x2800);
		mem_[0xe7f0] = 0xff;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 14) {
		memset(mem_ + 0x8000, 0, 0x8000);
		if (soundRom_ && soundRomSize_ >= 0x6000u) {
			memcpy(mem_, soundRom_, 0x6000u);
			if (soundRom_[0] == 0xc3 && soundRom_[1] == 0x89 && soundRom_[2] == 0x00)
				SetBank(0x0b);
			else if (soundRom_[0] == 0xc3 && soundRom_[1] == 0xd0 && soundRom_[2] == 0x03)
				SetBank(4);
			else
				SetBank(0);
			CEmuAcPatchTaitoL1Cpu(mem_, soundRom_, soundRomSize_);
		}
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 16) {
		if (soundRom_ && soundRomSize_ >= 0x10000u)
			memcpy(mem_, soundRom_, 0x10000u);
		else if (soundRom_) {
			unsigned n = soundRomSize_ < 0x10000u ? soundRomSize_ : 0x10000u;
			memcpy(mem_, soundRom_, n);
			if (n < 0x10000u)
				memset(mem_ + n, 0xff, 0x10000u - n);
		}
		memset(mem_ + 0xc000, 0, 0x800);
		memset(mem_ + 0xe000, 0, 0x1000);
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 15) {
		if (soundRom_) {
			unsigned n = (soundRomSize_ < 0x8000u) ? soundRomSize_ : 0x8000u;
			memcpy(mem_, soundRom_, n);
			if (n < 0x8000u)
				memset(mem_ + n, 0xff, 0x8000u - n);
		}
		memset(mem_ + 0x8000, 0, 0x800);
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 10)
		memset(mem_ + 0x8000, 0, 0x1000);
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 7) {
		/* 固定 32K + 8000 の 8K バンク。D000/E000 は ROM ではなく RAM */
		if (soundRom_) {
			unsigned n = (soundRomSize_ < 0x8000u) ? soundRomSize_ : 0x8000u;
			memcpy(mem_, soundRom_, n);
			if (soundRomSize_ > 0x8000u) {
				unsigned b = 0x2000u;
				if (0x8000u + b > soundRomSize_)
					b = soundRomSize_ - 0x8000u;
				memset(mem_ + 0x8000, 0xff, 0x2000);
				memcpy(mem_ + 0x8000, soundRom_ + 0x8000u, b);
			}
		}
		memset(mem_ + 0xd000, 0, 0x1000);
		memset(mem_ + 0xe000, 0, 0x1000);
		if (mem_[3] == 0x31 && mem_[5] == 0xd7) {
			mem_[0xe03e] = 0xff;
			mem_[0xe0fc] = 0;
		} else if (mem_[3] == 0x21 && mem_[4] == 0x00 && mem_[5] == 0xd0) {
			mem_[0xe003] = 0x55;
			for (int i = 0; i < 16; i++)
				mem_[0xe005 + i] = 0xff;
		} else if (mem_[3] == 0xc3 && mem_[4] == 0xa1 && mem_[5] == 0x01) {
			mem_[0xec09] = 0xee;
			/* insectx ブートは D000 を 55/AA 検査し失敗で JP 0000。マップが遅れると dumps=516。 */
			if (mem_[0x1b4] == 0x20 && mem_[0x1b5] == 0x5e)
				mem_[0x1b4] = mem_[0x1b5] = 0x00;
			if (mem_[0x1bb] == 0x20 && mem_[0x1bc] == 0x57)
				mem_[0x1bb] = mem_[0x1bc] = 0x00;
			if (mem_[0x1d3] == 0xcd && mem_[0x1d4] == 0xfb && mem_[0x1d5] == 0x01)
				mem_[0x1d3] = mem_[0x1d4] = mem_[0x1d5] = 0x00;
			if (mem_[0x1eb] == 0xcd && mem_[0x1ec] == 0x08 && mem_[0x1ed] == 0x02)
				mem_[0x1eb] = mem_[0x1ec] = mem_[0x1ed] = 0x00;
		} else {
			mem_[0xef10] = 0xff;
			mem_[0xef11] = 1;
			/* ISR CALL 0082 はメイン CPU オブジェクトリストを歩く。NOP し、68000 が E603/ED00 を埋めなくても音楽（CALL 006D）が走れるように。 */
			if (mem_[0x55] == 0xcd && mem_[0x56] == 0x82 && mem_[0x57] == 0x00) {
				mem_[0x55] = 0x00;
				mem_[0x56] = 0x00;
				mem_[0x57] = 0x00;
			}
			/* 00E9 CALL 0666 はこのダンプで JR $ する 8bit ROM チェックサム */
			if (mem_[0xe9] == 0xcd && mem_[0xea] == 0x66 && mem_[0xeb] == 0x06) {
				mem_[0xe9] = 0x00;
				mem_[0xea] = 0x00;
				mem_[0xeb] = 0x00;
			}
		}
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 17) {
		/* MAME kabukiz_cpu2_map: 固定 32K + 8000 の 16K×8 バンク。RAM E000-FFFF。 */
		if (soundRom_) {
			unsigned n = (soundRomSize_ < 0x8000u) ? soundRomSize_ : 0x8000u;
			memcpy(mem_, soundRom_, n);
			if (n < 0x8000u)
				memset(mem_ + n, 0xff, 0x8000u - n);
			unsigned b = 0x4000u;
			if (b > soundRomSize_)
				b = soundRomSize_;
			memset(mem_ + 0x8000, 0xff, 0x4000);
			memcpy(mem_ + 0x8000, soundRom_, b);
		}
		memset(mem_ + 0xe000, 0, 0x2000);
		bankLoaded_ = 1;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 18)
		memset(mem_ + 0xc000, 0, 0x800);
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && (snkMapKind_ == 2 || snkMapKind_ == 3) && fs) {
		int pick = -1, pcm = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d) continue;
			if (pick < 0 && sz == 0x8000u && d[0] == 0xc3)
				pick = i;
			if (sz == 0x40000u)
				pcm = i;
		}
		if (pick >= 0) {
			memcpy(mem_, fs->files[pick].data, 0x8000u);
			memset(mem_ + 0x8000, 0, 0x4000);
			loaded++;
			bankLoaded_ = 1;
			if (mem_[0x66] == 0xc3 && mem_[0x67] == 0x4c && mem_[0x68] == 0x09)
				snkMapKind_ = 3;
			else if (snkMapKind_ != 1 && snkMapKind_ != 3)
				snkMapKind_ = 2;
		}
		if (pcm >= 0) {
			if (pcmRom_) free(pcmRom_);
			pcmRomSize_ = fs->files[pcm].size;
			pcmRom_ = (uint8_t*)malloc(pcmRomSize_);
			if (pcmRom_) {
				memcpy(pcmRom_, fs->files[pcm].data, pcmRomSize_);
				if (pcm_)
					pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
			} else
				pcmRomSize_ = 0;
		}
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2 && snkMapKind_ == 1 && fs) {
		int pick = -1, pcm = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d) continue;
			if (pick < 0 && sz >= 0xc000u && d[0] == 0xf3 && d[1] == 0x3a
				&& d[2] == 0x00 && d[3] == 0xe0)
				pick = i;
			if (sz == 0x80000u)
				pcm = i;
		}
		if (pick >= 0) {
			unsigned n = fs->files[pick].size;
			if (n > 0xc000u) n = 0xc000u;
			memcpy(mem_, fs->files[pick].data, n);
			memset(mem_ + 0xc000, 0, 0x800);
			loaded++;
			bankLoaded_ = 1;
		}
		if (pcm >= 0) {
			if (pcmRom_) free(pcmRom_);
			pcmRomSize_ = fs->files[pcm].size;
			pcmRom_ = (uint8_t*)malloc(pcmRomSize_);
			if (pcmRom_) {
				memcpy(pcmRom_, fs->files[pcm].data, pcmRomSize_);
				unsigned pages = pcmRomSize_ / 0x10000u;
				for (int i = 0; i < 8; i++)
					raizingOkiBank_[0][i] = (unsigned)(i & 3);
				if (pages < 4u)
					memset(raizingOkiBank_[0], 0, sizeof(raizingOkiBank_[0]));
				if (pcm_) {
					pcm_->SetPcmRom(pcmRom_, pcmRomSize_);
					CEmuChipOki6295SetBankTable(pcm_, raizingOkiBank_[0]);
				}
			} else
				pcmRomSize_ = 0;
		}
	}
	if (chip_) chip_->Reset();
	if (chip2_) chip2_->Reset();
	if (chip3_) chip3_->Reset();
	if (pcm_) pcm_->Reset();
	if (board_ == CEMU_AC_BOARD_ROBOKID && vsIoKind_ == 8) {
		/* MAME stfight machine_start: YM アドレス 0x2F（FM÷2 PSG÷1） */
		if (chip_) chip_->Write(0, 0x2f);
		if (chip2_) chip2_->Write(0, 0x2f);
	}
	if (pcmRomSize_) {
		pcmTarget = CEmuAcPrimaryPcmTarget(this);
		if (pcmTarget)
			pcmTarget->SetPcmRom(pcmRom_, pcmRomSize_);
	}
	opmWrites_ = 0;
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && fs && ge && ge->archive[0]
		&& (_stricmp(ge->archive, "horshoes") == 0
			|| _stricmp(ge->archive, "flipull") == 0
			|| _stricmp(ge->archive, "tubeit") == 0
			|| _stricmp(ge->archive, "plotting") == 0
			|| _stricmp(ge->archive, "puzznic") == 0
			|| _stricmp(ge->archive, "cubybop") == 0)) {
		int pick = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d) continue;
			if (sz != 0x10000u && sz != 0x20000u) continue;
			if (d[0] == 0xc3 && ((d[1] == 0x89 && d[2] == 0x00) || (d[1] == 0xd0 && d[2] == 0x03))) {
				pick = i;
				break;
			}
		}
		if (pick >= 0) {
			const unsigned sz = fs->files[pick].size;
			const uint8_t* d = fs->files[pick].data;
			if (soundRom_) free(soundRom_);
			soundRomSize_ = sz;
			soundRom_ = (uint8_t*)malloc(soundRomSize_);
			if (soundRom_)
				memcpy(soundRom_, d, soundRomSize_);
			else
				soundRomSize_ = 0;
			taitoOpmMap_ = 14;
			unsigned n = (sz < 0x6000u) ? sz : 0x6000u;
			memcpy(mem_, d, n);
			if (d[1] == 0x89)
				SetBank(0x0b);
			else
				SetBank(4);
			CEmuAcPatchTaitoL1Cpu(mem_, soundRom_, soundRomSize_);
		}
	}
	if (board_ == CEMU_AC_BOARD_SYS18 && vsIoKind_ == 1 && fs) {
		int pick = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			const unsigned sz = fs->files[i].size;
			const uint8_t* d = fs->files[i].data;
			if (!d || sz != 0x10000u) continue;
			if (d[0] == 0xf3 && d[1] == 0xed && d[2] == 0x56
				&& d[3] == 0x31 && d[4] == 0x00 && d[5] == 0x00)
				pick = i;
		}
		if (pick >= 0) {
			memcpy(mem_, fs->files[pick].data, 0xc000u);
			memset(mem_ + 0xe000, 0, 0x2000);
			loaded++;
			bankLoaded_ = 1;
		}
	}
	if (m72Code) free(m72Code);
	return 1;
}

int CHardAc::Sys32IrqPending() const
{
	return (s32IrqIn_ & (uint8_t)(~s32IrqCtrl_[3]) & 7) != 0;
}

uint8_t CHardAc::Sys32IrqVector() const
{
	const uint8_t eff = (uint8_t)(s32IrqIn_ & (uint8_t)(~s32IrqCtrl_[3]) & 7);
	for (int v = 0; v < 3; v++) {
		if (eff & (1 << v))
			return (uint8_t)(2 * v);
	}
	return 0;
}

void CHardAc::Sys32SignalIrq(int which)
{
	for (int i = 0; i < 3; i++) {
		if (s32IrqCtrl_[i] == (uint8_t)which)
			s32IrqIn_ |= (uint8_t)(1 << i);
	}
}

void CHardAc::Sys32ClearIrq(int which)
{
	for (int i = 0; i < 3; i++) {
		if (s32IrqCtrl_[i] == (uint8_t)which)
			s32IrqIn_ &= (uint8_t)~(1 << i);
	}
}

void CHardAc::Sys32ApplyBank()
{
	if (!soundRom_ || soundRomSize_ < 0x2000u) return;
	unsigned nBanks = soundRomSize_ / 0x2000u;
	if (!nBanks) nBanks = 1;
	unsigned bank = s32Bank_ % nBanks;
	const unsigned src = bank * 0x2000u;
	unsigned n = 0x2000u;
	if (src + n > soundRomSize_) n = soundRomSize_ - src;
	memset(mem_ + 0xa000, 0xff, 0x2000);
	if (n) memcpy(mem_ + 0xa000, soundRom_ + src, n);
	bank_ = (int)bank;
	bankLoaded_ = 1;
}

void CHardAc::Sys32TickYmIrq()
{
	const int ymLine = (chip_ && chip_->Irq()) ? 1 : 0;
	if (ymLine && !s32YmIrqPrev_)
		Sys32SignalIrq(0);
	else if (!ymLine && s32YmIrqPrev_)
		Sys32ClearIrq(0);
	s32YmIrqPrev_ = ymLine;
}

/* CEmuHardAcSetActive の実装 */
void CEmuHardAcSetActive(CHardAc* hw)
{
	CEmuZ80BusSetActive(hw);
}
