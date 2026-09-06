#include "StdAfx.h"
#include "driver/cemu_driver.h"
#include "driver/cemu_driver_pc88.h"
#include "driver/cemu_driver_pc98.h"
#include "driver/cemu_driver_ac.h"
#include "driver/cemu_driver_x68k.h"
#include "driver/cemu_driver_sg1000.h"
#include "driver/cemu_driver_x1.h"
#include "driver/cemu_driver_msx.h"
#include "driver/cemu_driver_fm7.h"
#include "driver/cemu_driver_pcat.h"
#include "driver/cemu_driver_neogeo.h"
#include "driver/cemu_driver_f3.h"
#include "machine/cemu_hard_pc88.h"
#include "machine/cemu_hard_pc98.h"
#include "machine/cemu_hard_ac.h"
#include "machine/cemu_hard_x68k.h"
#include "machine/cemu_hard_sg1000.h"
#include "machine/cemu_hard_x1.h"
#include "machine/cemu_hard_msx.h"
#include "machine/cemu_hard_fm7.h"
#include "machine/cemu_hard_pcat.h"
#include "machine/cemu_hard_neogeo.h"
#include "machine/cemu_hard_f3.h"
#include <string.h>

static int IsPc98Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	return (_strnicmp(ge->platform, "pc98", 4) == 0
		|| _stricmp(ge->platform, "pc98dos") == 0
		|| _stricmp(ge->platform, "pc9821") == 0
		|| _stricmp(ge->platform, "pc98vx") == 0
		/* PC-88VA / VA-DOS: V30 + bootcs/BIOSD like PC-98 (not Z80 PC-88). */
		|| _stricmp(ge->platform, "pc88va") == 0
		|| _stricmp(ge->platform, "pc88vados") == 0) ? 1 : 0;
}

static int IsPc88Z80Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (IsPc98Platform(ge)) return 0; /* excludes pc88va / pc88vados */
	return (_strnicmp(ge->platform, "pc88", 4) == 0
		|| _stricmp(ge->platform, "pc80sr") == 0) ? 1 : 0;
}

static int IsSg1000Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->subtype, "sg1000") == 0) return 1;
	if (_stricmp(ge->platform, "sg1000") == 0) return 1;
	if (_stricmp(ge->dataDir, "sc3000") == 0) return 1;
	if (_strnicmp(ge->archive, "sc_", 3) == 0) return 1;
	return 0;
}

static int IsF3Platform(const CEmuGameEntry* ge);

static int IsAcPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (IsSg1000Platform(ge)) return 0;
	if (_stricmp(ge->subtype, "f3system") == 0) return 0;
	/* MSX/KSS must not fall into AC via dataDir=ac (zip in data\\ac). */
	if (_stricmp(ge->platform, "msx") == 0 || _stricmp(ge->subtype, "kss") == 0
		|| _stricmp(ge->subtype, "opll") == 0 || _stricmp(ge->dataDir, "msx") == 0)
		return 0;
	if (_stricmp(ge->platform, "megadrive") == 0 || _stricmp(ge->dataDir, "megadrive") == 0) return 0;
	if (_strnicmp(ge->platform, "capcom", 6) == 0) return 1;
	if (_stricmp(ge->platform, "sega") == 0) return 1;
	if (_stricmp(ge->platform, "namco") == 0) return 1;
	if (_strnicmp(ge->platform, "konami", 6) == 0) return 1;
	if (_stricmp(ge->platform, "taito") == 0) return 1;
	if (_stricmp(ge->platform, "irem") == 0) return 1;
	if (_stricmp(ge->platform, "dataeast") == 0) return 1;
	/* NeoGeo → CHardNeo (checked before AC); do not claim as AC board=0. */
	if (_stricmp(ge->platform, "videosystem") == 0) return 1;
	if (_stricmp(ge->platform, "jaleco") == 0) return 1;
	if (_stricmp(ge->platform, "technos") == 0) return 1;
	if (_stricmp(ge->platform, "toaplan") == 0) return 1;
	if (_stricmp(ge->platform, "snk") == 0) return 1;
	if (_stricmp(ge->platform, "nichibutsu") == 0) return 1;
	if (_stricmp(ge->platform, "seibu") == 0) return 1;
	if (_stricmp(ge->platform, "tecmo") == 0) return 1;
	if (_stricmp(ge->platform, "banpresto") == 0) return 1;
	if (_stricmp(ge->platform, "cave") == 0) return 1;
	if (_stricmp(ge->platform, "psikyo") == 0) return 1;
	if (_stricmp(ge->platform, "nmk") == 0) return 1;
	/* Smaller arcade houses present in local roms/ + arcdata xml2. */
	static const char* const kMoreAc[] = {
		"tehkan", "upl", "alpha", "yunsung", "athena", "atlus", "kaneko",
		"raizing", "eighting", "allumer", "atari", "bootleg", "deniam",
		"mitchell", "seta", "fuuki", "dooyong", "tatsumi", "tad", "marble",
		"technosoft", "easttechnology", "universal", "nintendo", "sunsoft",
		"success", "f2system"
	};
	for (int i = 0; i < (int)_countof(kMoreAc); i++)
		if (_stricmp(ge->platform, kMoreAc[i]) == 0)
			return 1;
	if (_stricmp(ge->dataDir, "ac") == 0) return 1;
	if (_strnicmp(ge->subtype, "cps", 3) == 0) return 1;
	if (_stricmp(ge->subtype, "system1") == 0 || _stricmp(ge->subtype, "system2") == 0) return 1;
	if (_stricmp(ge->subtype, "megasys1") == 0) return 1;
	if (_stricmp(ge->subtype, "m72") == 0 || _stricmp(ge->subtype, "m92") == 0
		|| _stricmp(ge->subtype, "m62") == 0) return 1;
	if (_stricmp(ge->subtype, "scramble") == 0 || _stricmp(ge->subtype, "scobra") == 0
		|| _stricmp(ge->subtype, "frogger") == 0
		|| _stricmp(ge->subtype, "timeplt") == 0 || _stricmp(ge->subtype, "gx400") == 0
		|| _stricmp(ge->subtype, "jungler") == 0 || _stricmp(ge->subtype, "circusc") == 0
		|| _stricmp(ge->subtype, "pooyan") == 0 || _stricmp(ge->subtype, "locomotn") == 0
		|| _stricmp(ge->subtype, "salamander") == 0
		|| _stricmp(ge->subtype, "ddragon2") == 0
		|| _stricmp(ge->subtype, "scontra") == 0 || _stricmp(ge->subtype, "hcastle") == 0
		|| _stricmp(ge->subtype, "tecmo16") == 0 || _stricmp(ge->subtype, "gunbird") == 0
		|| _stricmp(ge->subtype, "masterw") == 0 || _stricmp(ge->subtype, "viofight") == 0
		|| _stricmp(ge->subtype, "tnzs") == 0 || _stricmp(ge->subtype, "chukatai") == 0
		|| _stricmp(ge->subtype, "flstory") == 0 || _stricmp(ge->subtype, "nycaptor") == 0
		|| _stricmp(ge->subtype, "tokio") == 0 || _stricmp(ge->subtype, "buggychl") == 0
		|| _stricmp(ge->subtype, "terracre") == 0
		|| _stricmp(ge->subtype, "terraf") == 0 || _stricmp(ge->subtype, "cclimbr2") == 0
		|| _stricmp(ge->subtype, "robokid") == 0 || _stricmp(ge->subtype, "ninjakd2") == 0
		|| _stricmp(ge->subtype, "mnight") == 0 || _stricmp(ge->subtype, "battlantis") == 0
		|| _stricmp(ge->subtype, "68k2") == 0
		|| _stricmp(ge->subtype, "cabal") == 0 || _stricmp(ge->subtype, "raiden") == 0) return 1;
	if (_stricmp(ge->subtype, "f2system") == 0 || _stricmp(ge->subtype, "bsystem") == 0
		|| _stricmp(ge->subtype, "dual68") == 0 || _stricmp(ge->subtype, "taitoh") == 0
		|| _stricmp(ge->subtype, "fullt") == 0 || _stricmp(ge->subtype, "rastan") == 0
		|| _stricmp(ge->subtype, "asuka") == 0 || _stricmp(ge->subtype, "opwolf") == 0
		|| _stricmp(ge->subtype, "rainbow") == 0 || _stricmp(ge->subtype, "taitosj") == 0) return 1;
	if (_strnicmp(ge->subtype, "system16", 8) == 0) return 1;
	if (_strnicmp(ge->subtype, "system18", 8) == 0) return 1;
	if (_strnicmp(ge->subtype, "system24", 8) == 0) return 1;
	if (_strnicmp(ge->subtype, "system32", 8) == 0) return 1;
	if (_stricmp(ge->subtype, "outrun") == 0 || _stricmp(ge->subtype, "aburner") == 0) return 1;
	if (_stricmp(ge->subtype, "sharrier") == 0 || _stricmp(ge->subtype, "hangon") == 0) return 1;
	if (_stricmp(ge->subtype, "toutrun") == 0 || _stricmp(ge->subtype, "shangon") == 0) return 1;
	if (_stricmp(ge->subtype, "aerofgt") == 0) return 1;
	if (_stricmp(ge->subtype, "gng") == 0 || _stricmp(ge->subtype, "opn2") == 0) return 1;
	return 0;
}

static int IsNeoPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "neogeo") == 0 || _stricmp(ge->subtype, "neogeo") == 0)
		return 1;
	/* Mis-tagged Neo: snk/generic with YM2610 must not fall into AC board=0. */
	if (_stricmp(ge->platform, "snk") == 0
		&& (_stricmp(ge->subtype, "generic") == 0 || ge->subtype[0] == 0)) {
		for (int i = 0; i < ge->chipCount; i++)
			if (ge->chipIds[i] == CEMU_CHIP_YM2610)
				return 1;
	}
	return 0;
}

static int IsF3Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	return (_stricmp(ge->subtype, "f3system") == 0) ? 1 : 0;
}

static int IsX68kPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "x68k") == 0) return 1;
	if (_stricmp(ge->dataDir, "x68k") == 0) return 1;
	return 0;
}

static int IsX1Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "x1") == 0) return 1;
	if (_stricmp(ge->dataDir, "x1") == 0) return 1;
	if (_stricmp(ge->subtype, "x1") == 0 || _stricmp(ge->subtype, "x1psg") == 0) return 1;
	return 0;
}

static int IsFm7Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "fm7") == 0 || _stricmp(ge->platform, "fm77av") == 0
		|| _stricmp(ge->platform, "mucomfm") == 0)
		return 1;
	if (_stricmp(ge->dataDir, "fm7") == 0) return 1;
	return 0;
}

static int IsMsxPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "msx") == 0) return 1;
	if (_stricmp(ge->dataDir, "msx") == 0) return 1;
	if (_stricmp(ge->subtype, "kss") == 0) return 1;
	return 0;
}

static int IsPcatAdlibPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "pcatdos") != 0 && _stricmp(ge->platform, "pcat") != 0)
		return 0;
	const char* sub = ge->subtype;
	if (_stricmp(sub, "adlib") == 0 || _stricmp(sub, "opl2") == 0
		|| _stricmp(sub, "opl") == 0 || _stricmp(sub, "opl3") == 0
		|| _stricmp(sub, "soundblaster16") == 0 || _stricmp(sub, "sb16") == 0
		|| _stricmp(sub, "soundblaster") == 0 || _stricmp(sub, "sbpro") == 0
		|| _stricmp(sub, "sb") == 0
		|| _stricmp(sub, "gameblaster") == 0 || _stricmp(sub, "cms") == 0
		|| _stricmp(sub, "beep") == 0 || _stricmp(sub, "midiout") == 0)
		return 1;
	return 0;
}

CHard* CEmuHardCreate(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return NULL;
	if (IsF3Platform(ge)) {
		CHardF3* hw = new CHardF3();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsNeoPlatform(ge)) {
		CHardNeo* hw = new CHardNeo();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsPcatAdlibPlatform(ge)) {
		CHardPcat* hw = new CHardPcat();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsPc88Z80Platform(ge)) {
		CHardPc88* hw = new CHardPc88();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsPc98Platform(ge)) {
		CHardPc98* hw = new CHardPc98();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsSg1000Platform(ge)) {
		CHardSg1000* hw = new CHardSg1000();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	/* MSX before AC: dataDir=ac alone used to steal KSS archives. */
	if (IsMsxPlatform(ge)) {
		CHardMsx* hw = new CHardMsx();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsAcPlatform(ge)) {
		CHardAc* hw = new CHardAc();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsX68kPlatform(ge)) {
		CHardX68k* hw = new CHardX68k();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsX1Platform(ge)) {
		CHardX1* hw = new CHardX1();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	if (IsFm7Platform(ge)) {
		CHardFm7* hw = new CHardFm7();
		if (!hw->Init(ge, sampleRate)) {
			delete hw;
			return NULL;
		}
		return hw;
	}
	return NULL;
}

void CEmuHardDestroy(CHard* hw)
{
	if (!hw) return;
	if (hw->hardKind == CHard::KIND_PC98) {
		CHardPc98* p = (CHardPc98*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_PC88) {
		CHardPc88* p = (CHardPc88*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_AC) {
		CHardAc* p = (CHardAc*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_SG1000) {
		CHardSg1000* p = (CHardSg1000*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_X68K) {
		CHardX68k* p = (CHardX68k*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_X1) {
		CHardX1* p = (CHardX1*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_FM7) {
		CHardFm7* p = (CHardFm7*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_MSX) {
		CHardMsx* p = (CHardMsx*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_PCAT) {
		CHardPcat* p = (CHardPcat*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_NEO) {
		CHardNeo* p = (CHardNeo*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	if (hw->hardKind == CHard::KIND_F3) {
		CHardF3* p = (CHardF3*)hw;
		p->Shutdown();
		delete p;
		return;
	}
	delete hw;
}

CDriver* CEmuDriverCreate(const CEmuGameEntry* ge)
{
	if (!ge) return NULL;
	if (IsF3Platform(ge))
		return CDriverF3Create();
	if (IsNeoPlatform(ge))
		return CDriverNeoCreate();
	if (IsPcatAdlibPlatform(ge))
		return new CDriverPcat();
	if (IsPc88Z80Platform(ge))
		return new CDriverPc88();
	if (IsPc98Platform(ge))
		return new CDriverPc98();
	if (IsSg1000Platform(ge))
		return new CDriverSg1000();
	if (IsAcPlatform(ge))
		return new CDriverAc();
	if (IsX68kPlatform(ge))
		return new CDriverX68k();
	if (IsX1Platform(ge))
		return new CDriverX1();
	if (IsFm7Platform(ge))
		return new CDriverFm7();
	if (IsMsxPlatform(ge))
		return new CDriverMsx();
	return NULL;
}

void CEmuDriverDestroy(CDriver* drv)
{
	delete drv;
}
