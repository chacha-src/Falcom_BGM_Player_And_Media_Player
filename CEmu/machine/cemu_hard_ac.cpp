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
		"nslasher", "midres"
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
		"cobracom", "brkthru", "exprraid", "lastmisn", "makyosen", "oscar"
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
		"birdtry", "stadhero", "slyspy", "secretag"
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

/* decoCpuKind_ の種別: 0=H6280、1=karnov、2=dec0/actfancr、4=dec8（cobracom…） */
static int CEmuAcDecoCpuKind(const char* sub)
{
	if (!sub) return 0;
	if (_stricmp(sub, "karnov") == 0) return 1;
	if (CEmuAcIsDecoDec8Sub(sub)) return 4;
	if (CEmuAcIsDecoDec0Sub(sub)) return 2;
	if (CEmuAcIsDecoM6502Sub(sub)) return 2;
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
		|| _stricmp(sub, "rainbow") == 0) ? 1 : 0;
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
		|| _stricmp(sub, "arkanoid") == 0
		|| _stricmp(sub, "arkanoid2") == 0
		|| _stricmp(sub, "kicknrun") == 0
		|| _stricmp(sub, "ribl") == 0
		|| _stricmp(sub, "momoko") == 0
		|| _stricmp(sub, "masao") == 0
		|| _stricmp(sub, "gladiatr") == 0
		|| _stricmp(sub, "horshoes") == 0
		|| _stricmp(sub, "ashnojoe") == 0
		|| _stricmp(sub, "fhawk") == 0
		|| _stricmp(sub, "volfied") == 0
		|| _stricmp(sub, "kageki") == 0
		|| _stricmp(sub, "darius") == 0
		|| _stricmp(sub, "tokio") == 0
		|| _stricmp(sub, "lsasquad") == 0
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
		"blkpanther", "bladestl", "fastlane", "hotchase", "combh",
		"rollergames", "bigprowr", "crusherm", "combatsc", "contra",
		"ddribble", "jackal", "gberet", "jailbrek", "hyperspt",
		"labyrunr", "battlnts", "aliens2"
	};
	for (unsigned i = 0; i < sizeof(kSubs) / sizeof(kSubs[0]); i++)
		if (_stricmp(sub, kSubs[i]) == 0) return 1;
	return 0;
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

/* ------------------------------------------------------------------------
   ドライバ型エイリアス表。

   カタログはゲーム毎に <driver type> を 1 つ書くので、既にエミュ済みの基板が何十ものラベルで再出し、すべて BOARD_UNKNOWN＝完全無音になっていた。下の各項目は MAME で音源区画（音源 CPU + FM/PSG + ラッチ様式）が一致するエミュ済み基板へ型を向ける。映像ハードはここでは無関係。

   本表は主連鎖が UNKNOWN を返したあとだけ参照するので、既に扱う基板から型を奪わない。

   音源区画に CEmu 未実装コアが要る型は意図的に不在: Seta X1-010（blandia/daioh/grdians/metafox/myangel/atehate/stg/daikaiju/wingforc/madshark）、Cave YMZ280B（ddonpach/guwange/uopoko/korokoro）、Taito F3 ES5505（asurabld）、Sega UFO/Print Club セット。どこへでもマップすると無音がノイズに代わるだけ。
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
	{ "ironhors",   CEMU_AC_BOARD_KONAMI_TIMEPLT },
	{ "scotrsht",   CEMU_AC_BOARD_KONAMI_TIMEPLT },
	/* --- Konami: SN76489 系クラシック（System 1 音源区画） --- */
	{ "mikie",      CEMU_AC_BOARD_SEGA_SYS1 },
	{ "shaolins",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "kontest",    CEMU_AC_BOARD_SEGA_SYS1 },
	{ "trackfld",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "yiear",      CEMU_AC_BOARD_SEGA_SYS1 },
	{ "sbasketb",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "mrgoemon",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "lomakai",    CEMU_AC_BOARD_SEGA_SYS1 },
	/* --- Konami: Z80 + YM2151（+ K007232）音源 --- */
	{ "mainevt",    CEMU_AC_BOARD_KONAMI_K7232 },
	{ "tmnt",       CEMU_AC_BOARD_KONAMI_K7232 },
	{ "hexion",     CEMU_AC_BOARD_KONAMI_K7232 },
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
	{ "ultraman",   CEMU_AC_BOARD_KONAMI_PCM },
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
	{ "kikikai",    CEMU_AC_BOARD_TAITO_OPM },
	{ "ribl",       CEMU_AC_BOARD_TAITO_OPM },
	{ "palamed",    CEMU_AC_BOARD_TAITO_OPM },
	{ "cachat",     CEMU_AC_BOARD_TAITO_OPM },
	{ "horshoes",   CEMU_AC_BOARD_TAITO_OPM },
	{ "gladiatr",   CEMU_AC_BOARD_TAITO_OPM },
	{ "volfied",    CEMU_AC_BOARD_TAITO_OPM },
	{ "ashnojoe",   CEMU_AC_BOARD_TAITO_OPM },
	{ "momoko",     CEMU_AC_BOARD_TAITO_OPM },
	{ "masao",      CEMU_AC_BOARD_TAITO_OPM },
	{ "bionicc",    CEMU_AC_BOARD_TAITO_OPM },
	{ "lastduel",   CEMU_AC_BOARD_TAITO_OPM },
	{ "madgear",    CEMU_AC_BOARD_TAITO_OPM },
	{ "sf1",        CEMU_AC_BOARD_TAITO_OPM },
	{ "2mindril",   CEMU_AC_BOARD_TAITO_OPM },
	/* --- Taito: Z80 + YM2610 音源 --- */
	{ "wits",       CEMU_AC_BOARD_TAITO_YM2610 },
	{ "insectx",    CEMU_AC_BOARD_TAITO_YM2610 },
	{ "xsystem",    CEMU_AC_BOARD_TAITO_YM2610 },
	{ "godzilla",   CEMU_AC_BOARD_TAITO_YM2610 },
	{ "enmadaio",   CEMU_AC_BOARD_TAITO_YM2610 },
	/* --- Z80 + AY + MSM5232（flstory 音源基板） --- */
	{ "lsasquad",   CEMU_AC_BOARD_TAITO_OPM },
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
	{ "sidearms",   CEMU_AC_BOARD_TAITO_SJ },
	{ "exerizer",   CEMU_AC_BOARD_TAITO_SJ },
	{ "fcombat",    CEMU_AC_BOARD_TAITO_SJ },
	{ "ikki",       CEMU_AC_BOARD_TAITO_SJ },
	{ "tubep",      CEMU_AC_BOARD_TAITO_SJ },
	{ "btime",      CEMU_AC_BOARD_TAITO_SJ },
	{ "bombjack",   CEMU_AC_BOARD_TAITO_SJ },
	{ "popeye",     CEMU_AC_BOARD_TAITO_SJ },
	{ "mrdo",       CEMU_AC_BOARD_TAITO_SJ },
	{ "bankp",      CEMU_AC_BOARD_TAITO_SJ },
	{ "disco",      CEMU_AC_BOARD_TAITO_SJ },
	{ "swimmer",    CEMU_AC_BOARD_TAITO_SJ },
	{ "magmax",     CEMU_AC_BOARD_TAITO_SJ },

	/* --- Tecmo: Z80 + YM3812 対 / YM2151 + OKI --- */
	{ "rygar",      CEMU_AC_BOARD_TECMO16 },
	{ "gemini",     CEMU_AC_BOARD_TECMO16 },
	{ "tbowl",      CEMU_AC_BOARD_TECMO16 },
	{ "wc90",       CEMU_AC_BOARD_TECMO16 },
	{ "spbactn",    CEMU_AC_BOARD_TECMO16 },
	/* --- Nichibutsu / Nihon Bussan: I/O 経由 Z80 + YM3812 --- */
	{ "argus",      CEMU_AC_BOARD_TERRACRE },
	{ "valtric",    CEMU_AC_BOARD_TERRACRE },
	{ "butasan",    CEMU_AC_BOARD_TERRACRE },
	{ "cop01",      CEMU_AC_BOARD_TAITO_SJ },
	{ "terracra",   CEMU_AC_BOARD_TERRACRE },
	{ "ginganin",   CEMU_AC_BOARD_TERRACRE },
	/* --- Toaplan: Z80 + YM3812 音源 --- */
	{ "tp",         CEMU_AC_BOARD_TOAPLAN1 },
	{ "slapfght",   CEMU_AC_BOARD_TOAPLAN1 },
	{ "daisenpuu",  CEMU_AC_BOARD_TOAPLAN1 },
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
	{ "djboy",      CEMU_AC_BOARD_TECMO16 },
	{ "blazeon",    CEMU_AC_BOARD_TECMO16 },
	{ "hvyunit",    CEMU_AC_BOARD_TECMO16 },
	{ "bloodwar",   CEMU_AC_BOARD_TECMO16 },
	{ "superx",     CEMU_AC_BOARD_TECMO16 },
	{ "apache3",    CEMU_AC_BOARD_TECMO16 },
	{ "cybertnk",   CEMU_AC_BOARD_TECMO16 },
	{ "gigandes",   CEMU_AC_BOARD_TECMO16 },
	{ "hyperduel",  CEMU_AC_BOARD_TECMO16 },
	{ "crospang",   CEMU_AC_BOARD_TECMO16 },
	/* --- Seibu / 韓国 YM3812 + OKI6295 --- */
	{ "darkmist",   CEMU_AC_BOARD_SEIBU_OPL },
	{ "panicr",     CEMU_AC_BOARD_SEIBU_OPL },
	{ "cshooter",   CEMU_AC_BOARD_SEIBU_OPL },
	{ "airbuster",  CEMU_AC_BOARD_SEIBU_OPL },
	{ "nmg5",       CEMU_AC_BOARD_SEIBU_OPL },
	{ "yunsun16",   CEMU_AC_BOARD_SEIBU_OPL },
	{ "heberpop",   CEMU_AC_BOARD_SEIBU_OPL },
	/* --- SNK: Z80 + YM3812 音源 --- */
	{ "sengoku",    CEMU_AC_BOARD_SNK_OPL },
	{ "empcity",    CEMU_AC_BOARD_SNK_OPL },
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
	{ "excthour",   CEMU_AC_BOARD_TECHNOS_DDRAGON2 },
	/* --- Sega Y-board / OutRun 級: Z80 + YM2151 + SegaPCM --- */
	{ "pdrift",     CEMU_AC_BOARD_OUTRUN },
	{ "spmonaco",   CEMU_AC_BOARD_OUTRUN },
	{ "eropn",      CEMU_AC_BOARD_OUTRUN },
	{ "eropnx2",    CEMU_AC_BOARD_OUTRUN },
	/* --- Sega System 1/2 級 Z80 + SN --- */
	{ "angelkds",   CEMU_AC_BOARD_SEGA_SYS1 },
	{ "calorie",    CEMU_AC_BOARD_TAITO_SJ },
	{ "perfrman",   CEMU_AC_BOARD_SEGA_SYS1 },
	/* --- Banpresto の Sega System 16B / 24 --- */
	{ "gundamex",   CEMU_AC_BOARD_SYS16B },
	{ "sdgndmps",   CEMU_AC_BOARD_SYS16B },
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
		"dbz", "dbz2", "glfgreat", "xmen", "asterix", "gijoe", "prmrsocr",
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
		"lethal", "kabukiz", "gaiapolis", "martchmp", "premsocr"
	};
	for (unsigned i = 0; i < sizeof(kSubs) / sizeof(kSubs[0]); i++)
		if (_stricmp(sub, kSubs[i]) == 0) return 1;
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
	return (_stricmp(sub, "terracre") == 0) ? 1 : 0;
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
		|| _stricmp(sub, "mnight") == 0) ? 1 : 0;
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
		if (hw && hw->toaplanKaneko_ == 3)
			CEmuChipAyDestroy(chip);
		else
			CEmuChipYm3812Destroy(chip);
		break;
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
		|| _stricmp(ge->subtype, "deniam16c") == 0)
		board = CEMU_AC_BOARD_SYS16B;
	/* Sega System E: Z80 + SN76496×2、つまり System 1 音源区画 */
	else if (_stricmp(ge->subtype, "systeme") == 0)
		board = CEMU_AC_BOARD_SEGA_SYS1;
	/* Technos Double Dragon 3 は ddragon2 の Z80 + YM2151 + OKI6295 を共有 */
	else if (_stricmp(ge->subtype, "ddragon3") == 0)
		board = CEMU_AC_BOARD_TECHNOS_DDRAGON2;
	/* Atari Gauntlet hw は System 1 音源区画（6502 + YM2151 + POKEY） */
	else if (_stricmp(ge->subtype, "gauntlet") == 0)
		board = CEMU_AC_BOARD_ATARI_SYS1;
	/* Tehkan / Taito 多 AY 音源 CPU は Taito SJ 配置を共有 */
	else if (_stricmp(ge->subtype, "starforce") == 0
		|| _stricmp(ge->subtype, "swimmer") == 0
		|| _stricmp(ge->subtype, "halleysc") == 0
		|| _stricmp(ge->subtype, "worldcup") == 0)
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
		|| _stricmp(ge->subtype, "trojan") == 0 || _stricmp(ge->subtype, "higemaru") == 0
		|| _stricmp(ge->subtype, "exedexes") == 0 || _stricmp(ge->subtype, "gunsmoke") == 0
		|| _stricmp(ge->subtype, "blktiger") == 0 || _stricmp(ge->subtype, "blacktiger") == 0
		/* Tecmo YM2203×2（Ninja Gaiden / Shadow Warriors）。Gemini Wing / Silkworm は YM3812+MSM5205 — この YM2203 マップを取ってはいけない。 */
		|| _stricmp(ge->subtype, "gaiden") == 0)
		board = CEMU_AC_BOARD_GNG;
	else if (_stricmp(ge->subtype, "aburner") == 0
		/* Sega X-Board / G-LOC: After Burner と同じ Z80+YM2151+SegaPCM 級 */
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
		/* 古典 Toaplan1 Z80+YM3812（共有 RAM メールボックス）。MCU/Toaplan2 サブタイプ（tp/vimana/fireshrk/…）は未マップのまま。 */
		board = CEMU_AC_BOARD_TOAPLAN1;
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

/* チップと CPU を生成する */
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
			|| _stricmp(ge->subtype, "exedexes") == 0
			|| _stricmp(ge->subtype, "higemaru") == 0)
			gngCommandoMap_ = 1;
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
		chip2_ = NULL;
		/* 音楽テンポ = Timer B IRQ。Timer A だと ISR レートがおおよそ倍 */
		if (chip_)
			chip_->SetTimerIrqPolicy(0);
		/* MAME マスタ: SEGAPCM_DISCRETE(..., 8_MHz_XTAL / 2) → 4 MHz。フル 8 MHz はサンプルが約 2 倍速／薄くなった。ストリーム = clock/64 = 62.5 kHz。 */
		pcm_ = CEmuChipSegaPcmCreateDiscrete(4000000u, sampleRate_);
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
	} else if (board_ == CEMU_AC_BOARD_SYS24) {
		/* 68000×2 + YM2151（Z80 / RF5C68 無し）。イメージから sound_addr / irq_addr を載せる Musashi ホスト経路ができるまでディスクはソフト open のみ。 */
		cpuHz_ = 10000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_VSYSTEM) {
		/* MAME vsystem/aerofgt: Z80 20/4 = 5 MHz、YM2610 8 MHz（両方 PCB 確認）。音源 ROM は 128K を 8000-FFFF に 32K 窓 4 つでバンク。fromanc2 は Z80 を 8 MHz。BGM には 5 MHz で十分近い。Psikyo gunbird: Z80+YM2610 @ 8 MHz、I/O YM@04、ラッチ@08（vsIoKind 3）。 */
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
		const int kikikai = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "kikikai") == 0)
			|| (ge->archive[0] && (_stricmp(ge->archive, "kikikaik") == 0
				|| _stricmp(ge->archive, "kikikai") == 0)))) ? 1 : 0;
		const int tokio = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "tokio") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "tokio") == 0))) ? 1 : 0;
		const int bublbobl = (!tokio && ge
			&& ((ge->subtype[0] && _stricmp(ge->subtype, "bubblebobble") == 0)
				|| (ge->archive[0] && _strnicmp(ge->archive, "bublbobl", 8) == 0))) ? 1 : 0;
		const int lsasquad = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "lsasquad") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "lsasquad") == 0))) ? 1 : 0;
		const int lkage = (ge && ((ge->subtype[0] && _stricmp(ge->subtype, "kage") == 0)
			|| (ge->archive[0] && _stricmp(ge->archive, "lkage") == 0))) ? 1 : 0;
		/* 旧 TNZS 基板（tnzs_mcu / kageki / chukatai）: SUB Z80 上の YM2203 @B000、RAM D000、共有 E000。カタログアーカイブは tnzsjo であり tnzsb ではない。 */
		const int tnzsOld = (ge && (
			(ge->archive[0] && (_stricmp(ge->archive, "tnzsjo") == 0
				|| _stricmp(ge->archive, "tnzso") == 0
				|| _stricmp(ge->archive, "kageki") == 0
				|| _stricmp(ge->archive, "chukatai") == 0
				|| _stricmp(ge->archive, "extrmatn") == 0))
			|| (ge->subtype[0] && (_stricmp(ge->subtype, "kageki") == 0
				|| _stricmp(ge->subtype, "chukatai") == 0
				|| _stricmp(ge->subtype, "extrmatn") == 0)))) ? 1 : 0;
		taitoOpmMap_ = darius ? 1 : (kikikai ? 2 : (tokio ? 3 : (bublbobl ? 4
			: (lsasquad ? 5 : (lkage ? 6 : (tnzsOld ? 7 : 0))))));
		/* 0 rastan/asuka OPM、1 darius OPN×2、2 kikikai、3 tokio、4 bublbobl YM2203+YM3526、5 lsasquad YM2203+AY、6 lkage YM2203×2（bublbobl マップ、YM2203 @A000）、7 旧 TNZS YM2203 @B000（PC060HA 無し）。 */
		/* MAME masterw: Z80B @ 24/4 = 6 MHz、YM2203 @ 24/8 = 3 MHz。darius/lkage: Z80 と YM2203 は 4 MHz。tokio/bublbobl/lsasquad: Z80+YM @ 24/8 = 3 MHz（bublbobl は YM3526 も）。 */
		cpuHz_ = (taitoOpmMap_ == 3 || taitoOpmMap_ == 4 || taitoOpmMap_ == 5) ? 3000000
			: ((darius || taitoOpmMap_ == 6) ? 4000000 : (ym2203 ? 6000000 : 4000000));
		opmHz_ = (taitoOpmMap_ == 3 || taitoOpmMap_ == 4 || taitoOpmMap_ == 5) ? 3000000
			: ((darius || taitoOpmMap_ == 6) ? 4000000 : (ym2203 ? 3000000 : 4000000));
		if (ym2203) {
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
		bankBase_ = 0x4000u;
		bankSize_ = 0x4000u;
		if (kikikai || taitoOpmMap_ == 3 || taitoOpmMap_ == 4
			|| taitoOpmMap_ == 5 || taitoOpmMap_ == 6 || taitoOpmMap_ == 7) {
			/* 0000-7FFF の線形 32K。SetBank(0) は ROM[0:4000] を 4000-7FFF へ blit しブートチェックサムが 007C でハング。ROM+0x8000 から 8000 に 8K バンク 7 本。16K blit は飛ばす。 */
			bankBase_ = 0;
			bankSize_ = 0x8000u;
			if (taitoOpmMap_ == 7)
				bankLoaded_ = 1;
		}
	} else if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		/* MAME sega_system1 のクロック: SOUND_CLOCK 8 MHz。Z80 /2、SN1 /4、SN2 /2 */
		cpuHz_ = 4000000;
		opmHz_ = 2000000;
		chip_ = CEmuChipSn76489Create(2000000u, sampleRate_);
		chip2_ = CEmuChipSn76489Create(4000000u, sampleRate_);
		auxKind_ = 1;
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
		vsIoKind_ = cop01 ? 4 : (magmax ? 5 : (bombjack ? 6 : (calorie ? 7 : (solomon ? 8 : (halleys ? 9 : (pbaction ? 10 : (chaknpop ? 11 : 0)))))));
		cpuHz_ = (cop01 || magmax) ? 2500000 : (solomon ? 3072000 : 3000000);
		opmHz_ = (cop01 || magmax) ? 1250000 : 1500000;
		chip_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
		chip2_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
		chip3_ = CEmuChipAyCreate((uint32_t)opmHz_, sampleRate_);
		auxKind_ = 2;
		if (cop01 || magmax || bombjack || calorie || solomon || halleys || pbaction || chaknpop) {
			bankBase_ = 0;
			bankSize_ = (magmax || bombjack || calorie || solomon || halleys || pbaction || chaknpop) ? 0x4000u : 0x8000u;
		}
	} else if (board_ == CEMU_AC_BOARD_KONAMI_SCRAMBLE) {
		/* MAME galaxian/scramble: Z80+AY @ 14318000/8 → 1.789772 MHz。AY unmute 補助は入れない — 一定音を強制しプローブ classify() が FLAT（同一ピーク）として棄却する。 */
		cpuHz_ = 1789772;
		opmHz_ = 1789772;
		chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
		chip2_ = CEmuChipAyCreate(1789772u, sampleRate_);
		auxKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_TIMEPLT) {
		/* MAME timeplt_a: Z80/AY DERIVED_CLOCK(1,8) は 18.432 MHz から → 2.304 MHz */
		cpuHz_ = 2304000;
		opmHz_ = 2304000;
		chip_ = CEmuChipAyCreate(2304000u, sampleRate_);
		chip2_ = CEmuChipAyCreate(2304000u, sampleRate_);
		auxKind_ = 2;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		/* MAME nemesis/gx400: Z80+AY @ 14318180/8 のクロック */
		cpuHz_ = 1789772;
		opmHz_ = 1789772;
		chip_ = CEmuChipAyCreate(1789772u, sampleRate_);
		chip2_ = CEmuChipAyCreate(1789772u, sampleRate_);
		auxKind_ = 2;
		/* AY1 ポート A は nemesis_portA_r（周期タイマ）。0 に強制しない */
	} else if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		/* MAME technos/ddragon: Z80 3.579545、YM2151 同じ、OKI 1.056 MHz */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
		pcmKind_ = 2;
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
		if (decoCpuKind_ == 0) {
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
		cpuHz_ = pacman ? 3072000 : 1536000;
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
		/* MAME toaplan1: Z80 + YM3812 とも 28 MHz / 8。コマンドは 8000 の共有 RAM（ラッチ/NMI ではない）。YM I/O ポートはゲームで違う: truxton/rallybik=60、hellfire=70、zerowing=A8、他は 00。snowbros（Kaneko、ここでエイリアス）はそのメールボックスではない: I/O YM 02/03、ラッチ 04 → NMI、YM IRQ0、Z80 6 MHz / YM 3 MHz。slapfght（tigerh/alcon/getstar）: AY×2 1.5 MHz メモリマップ、C800 cmd、NMI 360 Hz（tigerh）/ 180 Hz（alcon/getstar）。kaneko_=3 としてパック。 */
		toaplanKaneko_ = 0;
		if (ge && ((_stricmp(ge->subtype, "snowbros") == 0)
			|| (ge->archive && _stricmp(ge->archive, "snowbros") == 0)))
			toaplanKaneko_ = 1;
		else if (ge && ((_stricmp(ge->subtype, "wardner") == 0)
			|| (ge->archive && _stricmp(ge->archive, "wardner") == 0)))
			toaplanKaneko_ = 2;
		else if (CEmuAcIsSlapfght(ge))
			toaplanKaneko_ = 3;
		if (toaplanKaneko_ == 1) {
			cpuHz_ = 6000000;
			opmHz_ = 3000000;
		} else if (toaplanKaneko_ == 3) {
			cpuHz_ = 3000000;
			opmHz_ = 1500000;
		} else {
			cpuHz_ = 3500000;
			opmHz_ = 3500000;
		}
		pcm_ = NULL;
		pcmKind_ = 0;
		toaplanYmPort_ = 0x00;
		if (toaplanKaneko_ == 3) {
			chip_ = CEmuChipAyCreate(1500000u, sampleRate_);
			chip2_ = CEmuChipAyCreate(1500000u, sampleRate_);
			auxKind_ = 2;
			/* YM ポートバイトを NMI レート旗に再利用: 1 = 360 Hz（tigerh） */
			if (ge && ((_stricmp(ge->subtype, "tigerh") == 0)
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
		/* snk68（3812）: Z80 + YM3812 I/O 00/20、ラッチ @ F800 → NMI。古典 SNK（athena/ikari/gwar…）: メモリマップ YM3526/Y8950×2、ラッチ @ E000 → IRQ0（MAME snk.cpp YM3526_*_sound_map）。 */
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
		auxKind_ = classic ? 3 : 0; /* chip2 を YM3812 として破棄 */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
		/* MAME thunderx/scontra/crimfght/twin16: Z80 + YM2151 @ 3.579545。K007232 PCM レジスタ窓は受けるが未合成 — BGM は YM2151。変種は konamiK7232Map_ 経由。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
		if (ge && ge->subtype[0] && _stricmp(ge->subtype, "crimfght") == 0)
			konamiK7232Map_ = 1;
		else if (ge && ge->subtype[0] && _stricmp(ge->subtype, "gradius3") == 0)
			konamiK7232Map_ = 2;
		else
			konamiK7232Map_ = 0;
	} else if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		/* MAME alpha68k_II: Z80 @ 6 MHz、YM2203 @ 約 3 MHz、YM2413 @ 3.579545、DAC、IN 00 経由ラッチ、バンク @ C000（16KiB）。周期 NMI @ 約 7614 Hz。 */
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
		/* MAME m_sound_nmi_mask は 0 始まり。YM2203 ポート A bit0 Low で許可。リセット時 mask=1 は Z80 を入れ子 NMI した（dumps=1）。 */
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
		/* MAME hcastle: Z80 + YM3812 @ A000（IRQ→NMI）、ラッチ @ D000、K007232 @ B000 / K051649 @ 9800 は stub。 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
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
		if (ge && (_stricmp(ge->subtype, "agallet") == 0
			|| (ge->archive && (_stricmp(ge->archive, "agallet") == 0
				|| _stricmp(ge->archive, "sailormn") == 0))))
			tecmoOpl_ = 5;
		else if (ge && _stricmp(ge->subtype, "rygar") == 0)
			tecmoOpl_ = 1;
		else if (ge && _stricmp(ge->subtype, "gemini") == 0)
			tecmoOpl_ = 2;
		else if (ge && _stricmp(ge->subtype, "tbowl") == 0)
			tecmoOpl_ = 4;
		else if (ge && _stricmp(ge->subtype, "spbactn") == 0)
			tecmoOpl_ = 3;
		else if (ge && ((_stricmp(ge->subtype, "wc90") == 0)
			|| (ge->archive && _stricmp(ge->archive, "wc90") == 0)))
			tecmoOpl_ = 6;
		else
			tecmoOpl_ = CEmuAcIsTecmoOplSub(ge->subtype) ? 3 : 0;
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
		/* terracre: YM3526 @4 MHz、RAM C000、ラッチ I/O 04/06。armedf/terraf: YM3812 @4 MHz、RAM F800-FFFF、同じ I/O。cclimbr2/legion: RAM C000-FFFF（cclimbr2_soundmap）。公式 legion/cclimbr2 は YM3526、legion ブートレグは YM3812。ホストコマンドは ((cmd&0x7f)<<1)|1（MAME sound_command_w）。 */
		if (CEmuAcIsCclimbr2Map(ge->subtype))
			terracreMap_ = 2;
		else
			terracreMap_ = CEmuAcIsArmedfSub(ge->subtype) ? 1 : 0;
		cpuHz_ = 4000000;
		opmHz_ = 4000000;
		chip_ = CEmuChipYm3812Create(4000000u, sampleRate_);
		chip2_ = NULL;
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_ROBOKID) {
		/* MAME ninjakd2/robokid: Z80 @ 5 MHz、I/O 00/01 と 80/81 の YM2203×2 @ 1.5 MHz。ラッチ @ E000。 */
		cpuHz_ = 5000000;
		opmHz_ = 1500000;
		chip_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
		mainIsYm2203_ = 1;
		chip2_ = CEmuChipYm2608Create(1500000u, 0 /* OPN */, sampleRate_);
		auxKind_ = 0; /* chip2 は OPN — Render は Chip2 経路で加算 */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_BATTLANTIS) {
		/* MAME battlnts: Z80 + YM3812×2 @ A000 / C000、ラッチ @ E000→IRQ0 */
		cpuHz_ = 3579545;
		opmHz_ = 3000000;
		chip_ = CEmuChipYm3812Create(3000000u, sampleRate_);
		chip2_ = CEmuChipYm3812Create(3000000u, sampleRate_);
		auxKind_ = 3; /* chip2 用に OPL を破棄 */
		pcm_ = NULL;
		pcmKind_ = 0;
	} else if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* Seibu 音源: SEI80BU 暗号化 Z80 + YM3812 + OKI6295 */
		cpuHz_ = 3579545;
		opmHz_ = 3579545;
		chip_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		chip2_ = NULL;
		pcm_ = CEmuChipOki6295Create(1056000u, sampleRate_);
		pcmKind_ = 2;
		seibuEnc_ = 1;
		/* Raiden の FM 表は 0x80|catalog。cupsoc 他は固定 0x8e */
		seibuSongOr80_ = (ge && ge->archive
			&& _strnicmp(ge->archive, "raiden", 6) == 0) ? 1 : 0;
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
			|| CEmuAcIsKonamiK054539Sub(ge->subtype) || hasK054539) ? 1 : 0;
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
		if (hasOpm || isK054539) {
			chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
			if (isK054539) {
				pcm_ = CEmuChipK054539Create(18432000u, sampleRate_);
				pcmKind_ = 4;
				if (isK054539x2) {
					pcm2_ = CEmuChipK054539Create(18432000u, sampleRate_);
					konamiPcm2Addr_ = CEmuAcOptionValue(ge, "pcm2_addr", 0xe400u);
					CEmuChipK054539SetFmMonBase(pcm_, 0);
					CEmuChipK054539SetFmMonBase(pcm2_, 8);
				}
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
		/* MAME segas32: Z80 @ 8 MHz、YM3438×2 @80/90、RF5C68 @C000-DFFF、共有 RAM E000-FFFF */
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
		/* chip3 は常に AY（SJ #3 / msisaac AY2）。auxKind_ は使わない（FLSTORY msisaac は MSM chip2 用に auxKind_=4 を保つ）。 */
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
	/* Sys16A/B バンクは I/O ポート 40（5358 / 5797）。この 16K 窓ヘルパではない。既定 bankBase_=0x4000 は固定 ROM 4000-7FFF を壊す。 */
	if (board_ == CEMU_AC_BOARD_SYS16A || board_ == CEMU_AC_BOARD_SYS16B)
		return;
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
		/* deco_146 / generic_latch_8: pending → HuC6280 IRQ1 または M6502 NMI */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		if (h6280_) H6280SetInputLine(h6280_, H6280_LINE_IRQ1, H6280_ASSERT_LINE);
		if (m6502_) M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_ASSERT_LINE);
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
		if (!cmd) cmd = 1;
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
			/* 毎 NMI で走査するワンショット SE/BGM 要求旗 */
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
			/* 一部経路が $6805/$680A/$680F へコピーするボイスミラーニブル */
			mem_[0x8a58] = 0x0a;
			mem_[0x8a59] = 0x0a;
			mem_[0x8a5a] = 0x0a;
		}
		wsgNmiEnable_ = 1;
		irqPulse_ = 1;    /* ドライバはこれを NMI にする */
		return;
	}
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_TAITO_OPM) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		/* kikikai に PC060HA は無い — メイン Z80 が共有 RAM 9FFF に曲を植え、音源 CPU が 0xFF でなくなるまで poll。 */
		if (taitoOpmMap_ == 2) {
			mem_[0x9fff] = cmd ? cmd : (uint8_t)0xff;
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
		/* Scramble: PPI PB bit3 が 7474 をクロック → Z80 INT（IM0 ベクタ 0xFF=RST38）。Time Pilot / GX400: ホストエッジ → HOLD_LINE IRQ0。ラッチは AY ポート A（scramble AY2 / timeplt AY1）または mem e001（gx400）。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECHNOS_DDRAGON2) {
		/* soundlatch_w → NMI。YM2151 IRQ が音楽シーケンサを駆動 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232
		|| board_ == CEMU_AC_BOARD_KONAMI_HCASTLE) {
		/* MAME: ホスト soundlatch 書 + IRQ0（IM1）。HCastle は D000 を poll。IF 待ちのブート経路が見えるようそれでもパルス。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		irqPulse_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		/* MAME: soundlatch 書。Z80 は IN 00 で読む。音楽は YM2203 ポート A でゲートされた周期 NMI（約 7614 Hz）で進む。 */
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
		return;
	}
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		/* soundlatch → NMI。YM2151 タイマ IRQ が BGM を駆動。Cave 16bit ラッチ: カタログ id は下位バイト。 */
		soundCmd_ = cmd;
		soundCmdWord_ = cmd;
		soundCmdPending_ = 1;
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
		if (board_ == CEMU_AC_BOARD_TERRACRE)
			cmd = (uint8_t)(((cmd & 0x7fu) << 1) | 1u);
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
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
		if (toaplanKaneko_ == 3) {
			/* MAME slapfght: コマンドは共有 RAM C800。C801==AA はブート存在。NMI が C800 を poll（0xFF アイドル）。 */
			mem_[0xc800] = cmd;
			mem_[0xc801] = 0xaa;
			return;
		}
		/* 共有 RAM メールボックス: (mail) がコマンド（0xFF アイドル）。Truxton は (8001)==0xAA をメイン CPU 存在旗にも保つ。Wardner はブート中だけ (C002)==0xAA を待つ — その後 C001-C7FE は BSS。 */
		mem_[ToaplanMail()] = cmd;
		if (toaplanKaneko_ != 2)
			mem_[ToaplanReady()] = 0xaa;
		return;
	}
	if (board_ == CEMU_AC_BOARD_SNK_OPL) {
		soundCmd_ = cmd;
		soundCmdPending_ = 1;
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
	if (board_ == CEMU_AC_BOARD_SEIBU_OPL) {
		/* seibu_sound main_w: バイト 0/1 + オフセット 4 で RST18。sub2main_pending をクリアし RST18 がラッチを読む（ブートの pending_w はさもなくば (4012)=1 のままエンキューを飛ばす）。ファームは D（ラッチ byte1）で配送: 0x80 が曲走査へ。表添字は E（ラッチ byte0）。カタログ BGM id は表枠の下位 7 ビット — raiden 0x1B→0x9B、cupsoc 0x32→0xB2。コイン／無効 0x80-0x84 は 0x8e へフォールバック。 */
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
	/* System 32: メイン↔Z80 メールボックスは共有 RAM E000（ポート C0 ではない） */
	if (board_ == CEMU_AC_BOARD_SYS32) {
		mem_[0xe000] = cmd;
		mem_[0xe001] = cmd;
		mem_[0xe00f] = 0x00;
	}
}

/* I/O ポート読込 */
uint8_t CHardAc::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (board_) {
	case CEMU_AC_BOARD_RAIZING:
		return RaizingPortIn(p);
	case CEMU_AC_BOARD_TECMO16:
		if (tecmoOpl_ != 5)
			return 0xff;
		/* MAME sailormn_sound_portmap。flags_r は 0 に stub */
		if (p == 0x20)
			return 0x00;
		if (p == 0x30) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (p == 0x40)
			return (uint8_t)(soundCmdWord_ >> 8);
		if (p == 0x50 || p == 0x51)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p == 0x60)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		if (p == 0x80)
			return pcm2_ ? pcm2_->ReadStatus() : 0x00;
		return 0xff;
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
			/* サウンドラッチ。アイドル時も goldnaxe の ISR は毎 YM tick で IN A,(C0) — 0 を返すと 0212/02C7 が OR A;JP Z,0B56 の mute-all 経路（TL=7F 永久）。0x80 はファームの空枠マーカ（02D4）で 02C7 は無視（AND #7F;RET Z）。Cotton は 0x80 を不一致として RET。 */
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
		/* MAME system32_sound_portmap: YM3438 #1 @80-83 / #2 @90-93 の配置 */
		if (p >= 0x80 && p <= 0x83)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (p >= 0x90 && p <= 0x93)
			return chip2_ ? chip2_->ReadStatus() : (chip_ ? chip_->ReadStatus() : 0x00);
		return 0xff;
	case CEMU_AC_BOARD_OUTRUN:
	case CEMU_AC_BOARD_ABURNER:
		/* MAME segaorun / aburner: YM2151 @ 00/01（ミラー 00-3F）、ラッチ @ 40（ミラー 40-7F） */
		if (p < 0x40) {
			uint8_t st = chip_ ? chip_->ReadStatus() : 0x00;
			/* After Burner メインループ: IN A,(01); BIT 0,A; JP Z,$0036。bit0 を常時オンにしない — 音楽エンジンが CPU レートで空転（良いノート少数 → 一時停止 → 超高速ゴミ）。OPM タイマ組前は約 256 Hz パルスでブートが抜けられるように。 */
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
		/* MAME sound_portmap_2203: ラッチ @ 40 のみ（ミラー 40-7F）。I/O に YM 無し */
		if (p >= 0x40 && p < 0x80) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		return 0xff;
	case CEMU_AC_BOARD_VSYSTEM:
		/* Video System I/O 配置 3 種が 1 基板を共有:
		   0 aerofgt/gstriker/taotaido: YM 00-03、バンク 04、ack 08、ラッチ 0c
		   1 spinlbrk/turbofrc/f1gp/pspikes: バンク 00、ラッチ 14、YM 18-1b
		   2 fromanc2/welltris: ラッチ 00/04/10、YM 08-0b、バンク 00/18
		   3 Psikyo gunbird: バンク 00、YM 04-07、ラッチ 08、ack 0c */
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
			/* 交差配線を許容: 一部ダンプはまだ他方のデコードに当たる */
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
		if (toaplanKaneko_ == 3)
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
		/* I/O 00 = YM3812 ステータス／アドレス。20 = データ（書のみ）。古典はメモリマップ */
		if (snkMapKind_)
			return 0xff;
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_TERRACRE:
		/* ラッチクリア @04、ラッチ読 @06 */
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
		/* GNG はメモリマップ。I/O は未使用 */
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_PCM:
		if (p == 0x00 || p == 0x01)
			return chip_ ? chip_->ReadStatus() : 0x00;
		return 0xff;
	case CEMU_AC_BOARD_KONAMI_SCRAMBLE:
		/* MAME scramble_sound_io_map: AY1 @10/20、AY2 @40/80。AY2 ポート A = ラッチ、ポート B = タイマ。 */
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
	case CEMU_AC_BOARD_TAITO_SJ:
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
	default:
		return 0xff;
	}
}

/* I/O ポート書込 */
void CHardAc::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (!chip_) return;
	switch (board_) {
	case CEMU_AC_BOARD_TAITO_SJ:
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
	case CEMU_AC_BOARD_TECMO16:
		if (tecmoOpl_ != 5)
			return;
		if (p == 0x00) {
			/* z80_rombank_w<0x1f>: 4000 の 16K 窓 */
			if (soundRom_ && soundRomSize_ > 0x4000u) {
				bankLoaded_ = 0;
				SetBank((int)(data & 0x1fu));
			}
			return;
		}
		if (p == 0x10)
			return; /* FIFO を 68000 へ ACK */
		if (p == 0x50 || p == 0x51) {
			chip_->Write(p & 1u, data);
			if (p & 1)
				opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			return;
		}
		if (p == 0x60) {
			if (pcm_) pcm_->Write(0, data);
			return;
		}
		if (p == 0x70 || p == 0xc0) {
			const int chip = (p == 0xc0) ? 1 : 0;
			const unsigned sz = chip ? pcmRom2Size_ : pcmRomSize_;
			unsigned pages = (sz >= 0x20000u) ? (sz / 0x20000u) : 1u;
			unsigned b1 = (unsigned)(data & 0x0f) % pages;
			unsigned b2 = (unsigned)((data >> 4) & 0x0f) % pages;
			unsigned lo = b1 * 2u, hi = b2 * 2u;
			unsigned* t = raizingOkiBank_[chip];
			t[0] = t[1] = t[2] = t[3] = lo;
			t[4] = lo; t[5] = lo + 1u;
			t[6] = hi; t[7] = hi + 1u;
			return;
		}
		if (p == 0x80) {
			if (pcm2_) pcm2_->Write(0, data);
			return;
		}
		return;
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
		/* YM1 @80-83、YM2 @90-93。バンク @A0/B0 の配置 */
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
		if (toaplanKaneko_ == 3)
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
		/* MAME sound_3526_io_map: YM @00/01、DAC @02/03（未実装 stub） */
		if (p == 0x00) {
			chip_->Write(0, data);
		} else if (p == 0x01) {
			chip_->Write(1, data);
			opmWrites_ = CEmuChipYm3812WriteCount(chip_);
		}
		break;
	case CEMU_AC_BOARD_ROBOKID:
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
		/* MAME: 00=ラッチクリア、08=DAC、0a/0b=YM2413、0c/0d=YM2203、0e=バンク */
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
	/* MAME toaplan1 sound_map: 0000-7FFF ROM、8000-87FF 共有 RAM。Wardner（twincobr_m）: 作業 8000-807F、共有コマンド RAM C000-C7FF。 */
	if (board_ == CEMU_AC_BOARD_TOAPLAN1) {
		if (toaplanKaneko_ == 3) {
			/* MAME tigerh_sound_map: AY1 A080/A082、AY2 A090/A092、NMI 許可 A0E0 / 禁止 A0F0、RAM C800-FFFF */
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
		if (addr >= 0x8000 && addr <= 0x87ff)
			mem_[addr] = data;
		return;
	}
	/* MAME snk68 sound_map: 0000-EFFF ROM、F000-F7FF RAM、F800 ラッチ。古典 SNK（athena/…）: C000-CFFF RAM、E000 ラッチ、E800/EC00 YM1、F000/F400 YM2、F800 ステータス。 */
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
	/* MAME timeplt_a: ROM 0000-2FFF、RAM 3000-33FF、AY1 データ/アドレス 4000/5000、AY2 データ/アドレス 6000/7000、フィルタ 8000+。 */
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
		return; /* フィルタ／未マップ */
	}
	/* MAME nemesis sound_map / gx400_sound_map（AY + ラッチ。K005289 は stub） */
	if (board_ == CEMU_AC_BOARD_KONAMI_GX400) {
		if (addr >= 0x4000 && addr <= 0x7fff) {
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
	/* MAME ddragon2_sound_map: ROM 0000-7FFF、RAM 8000-87FF、YM2151 8800-8801、OKI 9800、ラッチ A000。 */
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
	/* MAME thunderx/scontra（map0）/ crimfght（map1）: Z80 + YM2151 + K007232 stub の配置 */
	if (board_ == CEMU_AC_BOARD_KONAMI_K7232) {
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
		const unsigned ym = konamiK7232Map_ ? 0xa000u : 0xc000u;
		if (addr == ym || addr == ym + 1u) {
			if (chip_) {
				chip_->Write(addr & 1, data);
				if (addr & 1)
					opmWrites_ = CEmuChipYm2151WriteCount(chip_);
			}
			return;
		}
		/* K007232 / uPD7759 / バンクスイッチ — 書は受ける。PCM 合成はまだ無い */
		if (addr >= 0x9000)
			return;
		return;
	}
	/* MAME alpha68k_II sound_map: ROM 0000-7FFF、RAM 8000-87FF、バンク C000-FFFF */
	if (board_ == CEMU_AC_BOARD_ALPHA68K2) {
		if (addr >= 0x8000 && addr <= 0x87ff)
			mem_[addr] = data;
		return;
	}
	/* MAME hcastle sound_map: YM3812 @A000、K007232 @B000、ラッチ @D000 */
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
	if (board_ == CEMU_AC_BOARD_RAIZING) {
		RaizingMemWrite(addr, data);
		return;
	}
	/* MAME tecmo16 sound_map: ROM 0000-EFFF、RAM F000-FBFF、OKI FC00、YM2151 FC04/05、ラッチ FC08。古典マップ: rygar RAM 4000 YM 8000 ラッチ C000。gemini RAM 8000 YM A000 ラッチ C000。tbowl RAM C000 YM D000/D800 ラッチ E010。 */
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		if (tecmoOpl_ == 5) {
			/* MAME: RAM C000-DFFF ミラー 2000（8K） */
			if (addr >= 0xc000)
				mem_[0xc000u + (addr & 0x1fffu)] = data;
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
		if (addr >= 0xc000 && addr <= 0xc7ff) {
			mem_[addr] = data;
			return;
		}
		return;
	}
	/* terracre: RAM C000-CFFF。armedf/terraf: RAM F800-FFFF。cclimbr2/legion: RAM C000-FFFF。YM/ラッチは I/O。 */
	if (board_ == CEMU_AC_BOARD_TERRACRE) {
		if (terracreMap_ == 2) {
			if (addr >= 0xc000)
				mem_[addr] = data;
		} else if (terracreMap_) {
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
		/* K054321 sound_map @F000: [0]=音源→メイン、[2]/[3]=メイン→音源ラッチ */
		if (pcmKind_ == 4 && addr >= 0xf000 && addr <= 0xf003) {
			if (addr == 0xf000) {
				/* 音源→メインラッチ。CEmu では誰も聞かない */
				return;
			}
			return;
		}
		/* K053260: FA00 が SH1→NMI を武装（MAME z80_arm_nmi_w）。音源 CPU は LD (FA00),A / HALT。NMI（RETN）が HALT の先を再開。 */
		if (pcmKind_ == 3 && addr == 0xfa00 && pcmBase != 0xfa00u) {
			konamiSh1NmiArm_ = 1;
			return;
		}
		if (pcm2_ && konamiPcm2Addr_
			&& addr >= konamiPcm2Addr_ && addr < konamiPcm2Addr_ + pcmWin) {
			pcm2_->Write(addr - konamiPcm2Addr_, data);
			return;
		}
		if (pcm && addr >= pcmBase && addr < pcmBase + pcmWin) {
			pcm->Write(addr - pcmBase, data);
			return;
		}
		/* YM2151 @opm/@opm+1、加えて Konami F81x データポートミラー（thndrx2 は RST28 ビジー待ちのあと LD (F811),A）。 */
		if (chip_ && pcm_
			&& (addr == opm || addr == (opm + 1u)
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
	if (board_ == CEMU_AC_BOARD_TOAPLAN1 && toaplanKaneko_ == 3) {
		if (addr == 0xa081)
			return chip_ ? chip_->ReadData() : 0xff;
		if (addr == 0xa091)
			return chip2_ ? chip2_->ReadData() : 0xff;
		return mem_[addr];
	}
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
			return 0xff; /* コイン */
		if (addr == 0x6000)
			return pcm_ ? pcm_->ReadStatus() : 0x00;
		/* データ復号経由の暗号化 ROM（または平文バンクイメージ） */
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
			if (addr == 0xd800)
				return (uint8_t)(soundCmdPending_ ? 1 : 0);
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
			if (addr == 0xc000 || addr == 0xc001) {
				/* kageki: IN0/IN1 アイドル = アクティブ Low 0xFF。tnzs 5FAC: CALL MCU、CP 55 / JP NZ エラー（スピンし得る）。chukatai: ステータス bit0=IBF、bit1=busy。 */
				if (mem_[3] == 0x31 && mem_[5] == 0xd7)
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
			if (chip_ && (addr == 0x9000 || addr == 0x9001))
				return chip_->ReadStatus();
			if (addr == 0xa001) return SytSlaveCommR();
			if (addr >= 0x9000 && addr <= 0xdfff) return 0x00;
		}
	}
	if (board_ == CEMU_AC_BOARD_SEGA_SYS1) {
		if (addr >= 0x8000 && addr <= 0x9fff)
			return mem_[0x8000 + (addr & 0x07ff)];
		if (addr >= 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xa000) return 0xff; /* 書のみ PSG ポート */
	}
	if (board_ == CEMU_AC_BOARD_TAITO_SJ) {
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
		if (addr == 0xa000 || addr == 0xa001)
			return chip_ ? chip_->ReadStatus() : 0x00;
		if (addr >= 0x8000 && addr <= 0x87ff)
			return mem_[addr];
		if (addr >= 0x8000)
			return 0x00;
	}
	if (board_ == CEMU_AC_BOARD_RAIZING)
		return RaizingMemRead(addr);
	if (board_ == CEMU_AC_BOARD_TECMO16) {
		if (tecmoOpl_ == 5)
			return mem_[(addr >= 0xc000) ? (0xc000u + (addr & 0x1fffu)) : addr];
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
		} else {
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
		if (addr == 0xe000) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr >= 0xc000 && addr <= 0xc7ff)
			return mem_[addr];
	}
	if (board_ == CEMU_AC_BOARD_TERRACRE) {
		if (terracreMap_ == 2) {
			if (addr >= 0xc000)
				return mem_[addr];
		} else if (terracreMap_) {
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
		/* メイン 68K からのサウンドコマンドラッチ（Z80 が poll）。F008 = ラッチ lo。F00A = ラッチ hi / 第 2 バイト。Capcom version-5（megaman/sfzch）は YM ポインタ D010 がライブの間 (F00A)==0xFF のときだけコマンドをキュー — 両ポートに同じバイトを返すとキューを飛ばし全 FM チャネルが TL=7F。 */
		if (addr == 0xf008) {
			soundCmdPending_ = 0;
			return soundCmd_;
		}
		if (addr == 0xf00a)
			return 0xff;
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
		/* K054321: LD BC,(F002) がメイン→音源ラッチ対を取る */
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
		if (pcm2_ && konamiPcm2Addr_
			&& addr >= konamiPcm2Addr_ && addr < konamiPcm2Addr_ + pcmWin)
			return pcm2_->ReadStatus();
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

/* CEmuAcPrimaryPcmTarget の実装 */
static CChip* CEmuAcPrimaryPcmTarget(CHardAc* hw)
{
	if (!hw) return NULL;
	/* YM2610 基板は下の専用ローダ経由で ADPCM-A/B を取る。SetPcmRom ではない（全部 ADPCM-B バンクに着く）。Mega System 1 は独立 OKI バンク 2 本 — それも別ロード。 */
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
		M6502SetInputLine(m6502_, M6502_LINE_IRQ,
			ymIrq ? M6502_ASSERT_LINE : M6502_CLEAR_LINE);
		if (!soundCmdPending_)
			M6502SetInputLine(m6502_, M6502_LINE_NMI, M6502_CLEAR_LINE);
		return;
	}
	if (!h6280_) return;
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

/* データを載せる */
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
		if (board_ == CEMU_AC_BOARD_NAMCO_SYS2) {
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
	if (soundRomSize_ > 0 && soundRomSize_ <= 0x20000u) {
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
				|| _stricmp(ge->archive, "sws92") == 0))
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

/* zip から ROM／曲データを載せる */
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
		if (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 5) {
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
	if (board_ == CEMU_AC_BOARD_TAITO_YM2610 || board_ == CEMU_AC_BOARD_TAITO_OPM
		|| board_ == CEMU_AC_BOARD_VSYSTEM
		|| (board_ == CEMU_AC_BOARD_KONAMI_PCM && konamiBankAddr_)
		|| (board_ == CEMU_AC_BOARD_TECMO16 && tecmoOpl_ == 5)) {
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
				}
			}
		}
	}
	/* Sys16B: MAME は Z80 プログラムを 0000、バンク/UPD7759 データを 10000+ にパック。カタログはバンク ROM を "adpcm" とタグしがち — それでも soundRom_ へ載せ、ポート 40 が曲表を 8000-DFFF へページできるように。 */
	if (board_ == CEMU_AC_BOARD_SYS32 && codeRom && codeRomSize) {
		soundRom_ = (unsigned char*)malloc(codeRomSize);
		if (soundRom_) {
			memcpy(soundRom_, codeRom, codeRomSize);
			soundRomSize_ = codeRomSize;
		}
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
	if (board_ == CEMU_AC_BOARD_VSYSTEM && soundRom_) {
		/* サブタイプが vsIoKind_ を既定のままなら ISR ポートを嗅ぐ */
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
			/* gunbird: 固定 ROM 0000-7FFF、バンク窓 8000-FFFF */
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
	/* kikikai audiocpu: DI;IM1;JP 0068。線形 32K — SetBank は 0000-3FFF を 4000-7FFF へミラーしブートチェックサムがハング。 */
	if (board_ == CEMU_AC_BOARD_TAITO_OPM
		&& taitoOpmMap_ == 0
		&& mem_[0] == 0xf3 && mem_[1] == 0xed && mem_[2] == 0x56
		&& mem_[3] == 0xc3 && mem_[4] == 0x68 && mem_[5] == 0x00) {
		taitoOpmMap_ = 2;
		bankBase_ = 0;
		bankSize_ = 0x8000u;
		bankLoaded_ = 1;
	}
	if (!(board_ == CEMU_AC_BOARD_FLSTORY && (taitoOpmMap_ == 1 || taitoOpmMap_ == 3))
		&& !(board_ == CEMU_AC_BOARD_TAITO_SJ
			&& (vsIoKind_ == 4 || vsIoKind_ == 5 || vsIoKind_ == 6
				|| vsIoKind_ == 7 || vsIoKind_ == 8 || vsIoKind_ == 9
				|| vsIoKind_ == 10 || vsIoKind_ == 11))
		&& taitoOpmMap_ != 2 && taitoOpmMap_ != 3 && taitoOpmMap_ != 4
		&& taitoOpmMap_ != 5 && taitoOpmMap_ != 6 && taitoOpmMap_ != 7)
		SetBank(0);
	if (board_ == CEMU_AC_BOARD_FLSTORY && taitoOpmMap_ == 1)
		memset(mem_ + 0x4000, 0, 0x800);
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
	if (board_ == CEMU_AC_BOARD_TAITO_OPM && taitoOpmMap_ == 2)
		mem_[0x9fff] = 0xff;
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

/* CEmuHardAcSetActive の実装 */
void CEmuHardAcSetActive(CHardAc* hw)
{
	CEmuZ80BusSetActive(hw);
}
