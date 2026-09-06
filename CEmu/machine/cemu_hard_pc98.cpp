#include "StdAfx.h"
#include "cemu_hard_pc98.h"
#include "../cemu_rhythm.h"
#include "../chip/cemu_chip_opna.h"
#include "../fmmon/fmmon_shadow.h"
#include "../vendor/np2/np2ffi.h"
#include <stdlib.h>
#include <string.h>

/* Hoot KOEI / addressing=1 code ROMs pack load as high16=seg, low16=off
   (e.g. 0x01000100 → 0100:0100). Flat phys stays when value fits in 2MB. */
static unsigned Pc98RomPhys(int offset)
{
	const unsigned raw = (unsigned)offset;
	if (raw >= 0x200000u) {
		const unsigned seg = (raw >> 16) & 0xffffu;
		const unsigned ofs = raw & 0xffffu;
		return (seg << 4) + ofs;
	}
	return raw;
}

static int DosShellStarts(const CEmuGameEntry* ge, const char* const* prefixes);

/* Catalog <rom type="binary">00 a0 00 00</rom> embeds hex in the name —
   there is no zip member. type="string" is raw ASCII. */
static int Pc98ParseInlineRom(const char* name, uint8_t* out, int outCap)
{
	if (!name || !out || outCap <= 0) return 0;
	int n = 0;
	int haveHex = 0;
	for (const char* p = name; *p && n < outCap;) {
		while (*p == ' ' || *p == '\t' || *p == ',' || *p == ':') p++;
		if (!*p) break;
		const char c0 = p[0];
		const char c1 = p[1];
		int hi = -1, lo = -1;
		if (c0 >= '0' && c0 <= '9') hi = c0 - '0';
		else if (c0 >= 'a' && c0 <= 'f') hi = c0 - 'a' + 10;
		else if (c0 >= 'A' && c0 <= 'F') hi = c0 - 'A' + 10;
		if (c1 >= '0' && c1 <= '9') lo = c1 - '0';
		else if (c1 >= 'a' && c1 <= 'f') lo = c1 - 'a' + 10;
		else if (c1 >= 'A' && c1 <= 'F') lo = c1 - 'A' + 10;
		if (hi < 0 || lo < 0) {
			/* Not hex — treat whole name as ASCII string payload. */
			if (haveHex) break;
			n = 0;
			for (const char* q = name; *q && n < outCap; q++)
				out[n++] = (uint8_t)*q;
			return n;
		}
		out[n++] = (uint8_t)((hi << 4) | lo);
		haveHex = 1;
		p += 2;
	}
	return n;
}

enum {
	PC98_CPU_HZ = 8000000,
	PC98_OPN_CLOCK_HZ = 3993600,
	PC98_OPNA_CLOCK_HZ = 7987200,
	PC98_PIT_CLOCK_HZ = 1996800,
	PC98_OPN_IRQ_VEC = 0x0B,
	PC98_TIMER_VEC = 0x08,
	PC98_VSYNC_VEC = 0x0A,
	OPN_ADDR0 = 0x188,
	OPN_DATA0 = 0x18A,
	OPN_ADDR1 = 0x18C,
	OPN_DATA1 = 0x18E,
	EXT_CMD = 0x07E0,
	EXT_SONG = 0x07E2,
	EXT_PARAM = 0x07E4,
	EXT_STATE = 0x07E8,
	HOST_CMD = 0x07D0,
	HOST_P1 = 0x07D2,
	HOST_P2 = 0x07D4,
	HOST_P3 = 0x07D6,
	SOUND86_ID = 0xA460,
	SOUND86_FIFO_STAT = 0xA466,
	SOUND86_FIFO_CTL = 0xA468,
	SOUND86_DAC_CTL = 0xA46A,
	SOUND86_FIFO_DAT = 0xA46C,
	SOUND86_MUTE = 0xA66E,
	PIT_CT0 = 0x71,
	PIT_CTRL = 0x77,
	PIC_CMD = 0x00,
	PIC_MASK = 0x02,
	SLAVE_PIC_CMD = 0x08,
	SLAVE_PIC_MASK = 0x0A,
	VSYNC_ACK = 0x64,
	IO_DELAY = 0x5F,
	WOLF_SYNC0 = 0xE0D0,
	WOLF_SYNC1 = 0xE0D2
};

static CHardPc98* g_pc98Active = NULL;
static int g_pc98Eoi = 0;

static int CEmuParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal)
{
	if (!ge || !name) return defVal;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) != 0) continue;
		const char* v = ge->opt[i].value;
		if (!v || !v[0]) return defVal;
		return (int)strtoul(v, NULL, 0);
	}
	return defVal;
}

static void Pc98Out8(unsigned port, unsigned char val)
{
	CHardPc98* hw = g_pc98Active;
	if (hw) hw->PortOut((uint16_t)port, (uint8_t)val);
}

static unsigned char Pc98In8(unsigned port)
{
	CHardPc98* hw = g_pc98Active;
	if (!hw) return 0xff;
	return hw->PortIn((uint16_t)port);
}

CHardPc98::CHardPc98()
	: opnaMode(0)
	, cpuHz_(PC98_CPU_HZ)
	, opnHz_(PC98_OPN_CLOCK_HZ)
	, bootCs_(0)
	, bootIp_(0)
	, funcVect_(0x7f)
	, dataAddr_(0)
	, fileSize_(0)
	, data2Addr_(0)
	, file2Size_(0)
	, addressing_(0)
	, isDos_(0)
	, nopnDrv_(0)
	, dofmd_(0)
	, fmd98_(0)
	, fmdSongOff_(0)
	, rx98_(0)
	, rxSongOff_(0)
	, prog98_(0)
	, progSongAddr_(0)
	, bst398_(0)
	, koei98_(0)
	, cal98_(0)
	, madp98_(0)
	, n3golf98_(0)
	, dks98_(0)
	, mdplay98_(0)
	, musicComKeepalive_(0)
	, modeMidi_(0)
	, midiCapArmed_(0)
	, sound86Mask_(0x00) /* MAME reset: ID=0x40; bit0 set by software for OPNA enhance */
	, sound86FifoCtl_(0)
	, sound86DacCtl_(0)
	, sound86Mute_(0)
	, wolfteam98_(0)
	, wolfMiSeg_(0)
	, wolfSyncRun_(0)
	, wolfGateStop_(0x5B48)
	, wolfGatePlay_(0x5B5A)
	, wolfSongPtr_(0x5B5D)
	, wolfSongBuf_(0x7E5E)
	, wolfTitleWord_(0x643A)
	, wolfFlagA_(0x0662)
	, wstimer_(0)
	, dummySndRom_(0)
	, pc88VaIo_(0)
	, sorcGlue_(0)
	, olteusMapSeg_(0)
	, olteusDataSeg_(0)
	, olteusTimerOn_(0)
	, olteusTrampOk_(0)
	, olteusIrqPulse_(0)
	, olteusInTick_(0)
	, olteusTimerResidual_(0)
	, olteusTickGuard_(0)
	, vaPc88PortHits_(0)
	, vaPc98PortHits_(0)
	, vaPc88LatchedAddr_(0)
	, vaPc88LatchedAddrHi_(0)
	, cpuCycles_(0)
	, extCmd_(0)
	, extSong_(0)
	, extParam_(0)
	, stubState_(0)
	, picMask_(0xff)
	, slavePicMask_(0xff)
	, opnInService_(0)
	, irqEdgeSeen_(0)
	, irqEdgeConsumed_(0)
	, chip_(NULL)
	, sampleRate_(44100)
	, active_(0)
	, pitClockHz_(PC98_PIT_CLOCK_HZ)
	, pitReload_(0)
	, pitCounter_(0)
	, pitResidual_(0)
	, pitIrqPending_(0)
	, pitWriteHi_(0)
	, pitReadHi_(0)
	, pitRunning_(0)
	, vsyncResidual_(0)
	, vsyncPending_(0)
	, opnPumpResidual_(0)
	, hostFunc_(0)
	, hostParam1_(0)
	, hostParam2_(0)
	, hostParam3_(0)
	, hostStatus_(0)
	, opnWriteCount_(0)
	, opnKeyOnCount_(0)
	, opnTlLiveCount_(0)
	, opnFnumCount_(0)
	, opnTimerCount_(0)
	, opnIrqDeliverCount_(0)
	, lastSongLoadOk_(0)
	, lastSongLoadBytes_(0)
	, opnLogCount_(0)
	, opnLatchedAddr_(0)
	, ssgPortAJumper_(0)
	, opnLatchedAddrHi_(0)
	, wolfCmdLogCount_(0)
	, wolfCmdWriteCount_(0)
	, wolfBridgeEnable_(0)
	, wolfNoteOnCount_(0)
	, wolfNoteOffCount_(0)
	, wolfCtrlCount_(0)
	, wolfRunStatus_(0)
	, wolfDataIdx_(0)
	, wolfDataNeed_(0)
	, wolfInSysex_(0)
	, wolfVoiceCount_(3)
	, wolfVoiceClock_(0)
	, midiBytes_(NULL)
	, midiDelta_(NULL)
	, midiCount_(0)
	, midiNoteOnCount_(0)
	, midiPortOutCount_(0)
	, midiLastCycle_(0)
	, mpuUart_(0)
	, mpuAckR_(0)
	, mpuAckW_(0)
	, mpuRx_(0)
	, mpuRxFull_(0)
	, dosStubReady_(0)
	, picMasterIcw_(0)
	, picSlaveIcw_(0)
	, picMasterIcw1_(0)
	, picSlaveIcw1_(0)
	, dosGe_(NULL)
{
	hardKind = KIND_PC98;
	dosSong_[0] = 0;
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgm2Bank_, 0, sizeof(bgm2Bank_));
	memset(bgm2BankSize_, 0, sizeof(bgm2BankSize_));
	memset(opnLogAddr_, 0, sizeof(opnLogAddr_));
	memset(opnLogData_, 0, sizeof(opnLogData_));
	memset(wolfCmdLog_, 0, sizeof(wolfCmdLog_));
	memset(wolfData_, 0, sizeof(wolfData_));
	memset(wolfVoiceActive_, 0, sizeof(wolfVoiceActive_));
	memset(wolfVoiceMidiCh_, 0, sizeof(wolfVoiceMidiCh_));
	memset(wolfVoiceNote_, 0, sizeof(wolfVoiceNote_));
	memset(wolfVoiceAge_, 0, sizeof(wolfVoiceAge_));
	memset(wolfChVol_, 127, sizeof(wolfChVol_));
	memset(wolfChExpr_, 127, sizeof(wolfChExpr_));
	memset(mpuAckQ_, 0, sizeof(mpuAckQ_));
}

CHardPc98::~CHardPc98()
{
	Shutdown();
}

void CHardPc98::FreeBanks()
{
	for (int i = 0; i < 256; i++) {
		if (bgmBank_[i]) { free(bgmBank_[i]); bgmBank_[i] = NULL; }
		if (bgm2Bank_[i]) { free(bgm2Bank_[i]); bgm2Bank_[i] = NULL; }
		bgmBankSize_[i] = 0;
		bgm2BankSize_[i] = 0;
	}
}

uint8_t* CHardPc98::Mem()
{
	return np2_mem();
}

static int CEmuPc98IsFmp(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; ++i) {
		const char* s = ge->rom[i].name;
		if (!s) continue;
		for (; *s; ++s)
			if (_strnicmp(s, "fmp", 3) == 0 || _strnicmp(s, "tglfmp", 6) == 0)
				return 1;
	}
	return 0;
}

static int CEmuPc98IsMusicCom(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; ++i) {
		const char* s = ge->rom[i].name;
		if (!s) continue;
		const char* base = strrchr(s, '/');
		const char* back = strrchr(s, '\\');
		if (!base || (back && back > base)) base = back;
		base = base ? base + 1 : s;
		if (_stricmp(base, "MUSIC.COM") == 0 || _stricmp(base, "46.com") == 0)
			return 1;
	}
	return 0;
}

int CHardPc98::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	opnaMode = (_stricmp(ge->subtype, "opna") == 0) ? 1 : 0;
	opnHz_ = opnaMode ? PC98_OPNA_CLOCK_HZ : PC98_OPN_CLOCK_HZ;
	cpuHz_ = PC98_CPU_HZ;
	int clockmul = CEmuParseOptHex(ge, "clockmul", 0);
	if (clockmul <= 0) clockmul = CEmuParseOptHex(ge, "clock_mul", 1);
	if (clockmul < 1) clockmul = 1;
	/* hootrip keeps CPU at 8 MHz; clockmul is reported only. Keep 8 MHz. */
	(void)clockmul;

	bootCs_ = CEmuParseOptHex(ge, "bootcs", 0);
	bootIp_ = CEmuParseOptHex(ge, "bootip", 0);
	funcVect_ = CEmuParseOptHex(ge, "funcvect", 0x7f) & 0xff;
	dataAddr_ = CEmuParseOptHex(ge, "dataaddr", 0);
	fileSize_ = CEmuParseOptHex(ge, "filesize", 0);
	/* Falcom SORC98 catalog uses decimal "1000" for a 0x1000 window. */
	if (fileSize_ == 1000 && dataAddr_ == 0x3000)
		fileSize_ = 0x1000;
	data2Addr_ = CEmuParseOptHex(ge, "data2addr", 0);
	file2Size_ = CEmuParseOptHex(ge, "file2size", 0);
	addressing_ = CEmuParseOptHex(ge, "addressing", 0);
	if (addressing_ == 0)
		addressing_ = CEmuParseOptHex(ge, "adressing", 0);
	wstimer_ = CEmuParseOptHex(ge, "wstimer", 0);
	dummySndRom_ = CEmuParseOptHex(ge, "dummysndrom", 0);
	/* SORC98 v4–v10 share the same boot stub as v1–v3 but omit wstimer in
	   the catalog. Without it TriggerPlay skips the [085A] clear / cmd0
	   re-issue and the sequencer never leaves mute.
	   Catalog often writes filesize as decimal "1000" (strtoul base0), not
	   "0x1000" — accept both.
	   PC-88VA SORCERIAN uses the same INT7F glue with song window @0x11800. */
	if (wstimer_ <= 0 && bootIp_ == 0xf000 && funcVect_ == 0x7f
		&& (dataAddr_ == 0x3000 || dataAddr_ == 0x11800))
		wstimer_ = 1;
	sorcGlue_ = (wstimer_ > 0 && bootCs_ == 0 && bootIp_ != 0
		&& (dataAddr_ == 0x3000 || dataAddr_ == 0x11800)) ? 1 : 0;
	pc88VaIo_ = (_stricmp(ge->platform, "pc88va") == 0
		|| _stricmp(ge->platform, "pc88vados") == 0) ? 1 : 0;
	isDos_ = ((_stricmp(ge->platform, "pc98dos") == 0
		|| _stricmp(ge->platform, "pc9821") == 0
		|| _stricmp(ge->platform, "pc88vados") == 0) && bootCs_ == 0) ? 1 : 0;
	modeMidi_ = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "midiout") == 0) {
			modeMidi_ = 1;
			break;
		}
	}
	if (_stricmp(ge->subtype, "midiout") == 0 || _stricmp(ge->subtype, "midi") == 0)
		modeMidi_ = 1;
	mpuUart_ = 0;
	midiCapArmed_ = 0; /* BootDos shells may OUT 0→E0D0 forever; arm after */
	MidiCaptureReset();

	chip_ = CEmuChipYm2608Create((uint32_t)opnHz_, opnaMode, sampleRate_);
	if (!chip_) return 0;
	/* Select clock behavior by driver family. MUSIC.COM relies on SOUND BIOS
	   selecting YM2608 /2. FMP and Falcom RX program their own timer constants
	   and stay at reset /6. ymfm reports timer durations in half master clocks,
	   so FMP needs ×2 timer compensation; the old BIOS-equivalent ×3 overran
	   the driver's own Timer B cadence. */
	const int isFmp = CEmuPc98IsFmp(ge);
	const int needBios = CEmuPc98IsMusicCom(ge);
	const int is46oku = (_stricmp(ge->archive, "46oku98") == 0);
	if (needBios) {
		chip_->Write(0, 0x2F);
		/* Restore the native synthesis rate after 2Fh's 3× prescale.
		   46oku's remaining octave correction is an F-number block shift,
		   so envelope/LFO time is not slowed with the pitch. */
		chip_->SetPitchRateDiv(3u);
		if (is46oku) {
			chip_->SetPitchOctaveShift(-1);
			/* 46.COM repeatedly adds its fade amount to carrier TLs even
			   though this path has no live MUSIC.COM [0290]/[0294] state. */
			chip_->SetCarrierFadeClamp(1);
		}
	} else if (isFmp && modeMidi_) {
		chip_->Write(0, 0x2E); /* MIDI has no audible FM pitch to preserve. */
	}
	/* vg2 / other FMP: keep reset ÷6 pitch (no block shift). Earlier +1
	   octave made audio high vs FmMon F-numbers (screenshot: 440Hz=A4).
	   ymfm reports timer durations in half-master clocks, so FMP needs ×2
	   timer compensation — scale 1 left Timer-B ~half (~50 Hz vs ~100). */
	else if (isFmp && !modeMidi_)
		chip_->SetTimerClockScale(2u);
	else
		chip_->SetTimerClockScale(1u);

	np2_init();
	np2_reset();
	np2_setextsize(0);
	np2_set_adrsmask(0x000FFFFFu);
	/* PC-88VA CPU is V30; keep i286 for classic PC-98.
	   V30 patch is opt-in after bootcs VA probes stabilize — i286 runs
	   the Falcom SORC stub (same as SORC98) reliably. */
	np2_set_v30(0);
	uint8_t* mem = np2_mem();
	if (mem) memset(mem, 0, 0x200000);

	AttachIoHooks();
	active_ = 1;
	return 1;
}

void CHardPc98::Shutdown()
{
	DetachIoHooks();
	FreeBanks();
	if (chip_) { CEmuChipYm2608Destroy(chip_); chip_ = NULL; }
	delete[] midiBytes_; midiBytes_ = NULL;
	delete[] midiDelta_; midiDelta_ = NULL;
	midiCount_ = 0;
	active_ = 0;
	if (g_pc98Active == this) g_pc98Active = NULL;
}

void CHardPc98::AttachIoHooks()
{
	g_pc98Active = this;
	hootrip_out8 = Pc98Out8;
	hootrip_inp8 = Pc98In8;
}

void CHardPc98::DetachIoHooks()
{
	if (g_pc98Active == this) {
		hootrip_out8 = NULL;
		hootrip_inp8 = NULL;
		g_pc98Active = NULL;
	}
}

void CHardPc98::StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	FreeBanks();
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		if (_stricmp(r->type, "bgm") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			bgmBank_[idx] = (unsigned char*)malloc(sz);
			if (bgmBank_[idx]) {
				memcpy(bgmBank_[idx], data, sz);
				bgmBankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "bgm2") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			bgm2Bank_[idx] = (unsigned char*)malloc(sz);
			if (bgm2Bank_[idx]) {
				memcpy(bgm2Bank_[idx], data, sz);
				bgm2BankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "adpcm") == 0 && chip_ && r->offset >= 0) {
			chip_->SetAdpcmB(data, sz, (unsigned)r->offset);
		}
	}
}

int CHardPc98::LoadSongToAddr(unsigned songNum, int destAddr, int maxSize, int isSecondary)
{
	if (destAddr <= 0) return 0;
	unsigned char** banks = isSecondary ? bgm2Bank_ : bgmBank_;
	unsigned* sizes = isSecondary ? bgm2BankSize_ : bgmBankSize_;
	if (songNum > 255 || !banks[songNum]) {
		lastSongLoadOk_ = 0;
		lastSongLoadBytes_ = 0;
		return 0;
	}
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	unsigned n = sizes[songNum];
	if (maxSize > 0 && (unsigned)maxSize < n) n = (unsigned)maxSize;
	if ((unsigned)destAddr + n > 0x200000) {
		if ((unsigned)destAddr >= 0x200000) return 0;
		n = 0x200000u - (unsigned)destAddr;
	}
	memcpy(mem + destAddr, banks[songNum], n);
	/* BirdySoft drivers (MU-era and MF-era reloc builds) reject or mishandle
	   MF magic; streams are structurally MU (cal_98 __30 == calr __30 aside
	   from the letter). Normalize MF→MU for all cal98_ preloads. */
	if (cal98_ && n >= 2
		&& mem[destAddr] == 'M' && mem[destAddr + 1] == 'F')
		mem[destAddr + 1] = 'U';
	/* Wolfteam MS/MU `\x00B` streams (hioden/suzaku) share the `\x01B` layout
	   but keep a zero type byte; bump to 01 so MUSDRV accepts the song. */
	if (wolfteam98_ && n >= 2
		&& mem[destAddr] == 0x00 && mem[destAddr + 1] == 0x42)
		mem[destAddr] = 0x01;
	lastSongLoadOk_ = 1;
	lastSongLoadBytes_ = (int)n;
	return 1;
}

void CHardPc98::HostService(uint8_t func)
{
	hostStatus_ = 0xff;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	/* DOFMD/BRANM glue OUT 07D4/07D6 as real-mode off/seg (SI/DS). Catalog
	   adressing=0 would otherwise form a flat 00FA11FBh and miss the buffer. */
	/* DKS/FQ stubs pass ES:BX as real-mode song/table pointers on 07D4/07D6
	   (same shape as DOFMD). Flat (seg<<16)|off lands past 2MB and never loads. */
	const int realModeDest = addressing_ || dofmd_ || dks98_;
	int dest = 0;
	if (realModeDest) {
		dest = ((int)hostParam3_ << 4) + (int)hostParam2_;
	} else {
		dest = ((int)hostParam3_ << 16) | (int)hostParam2_;
		if (dest == 0 && hostParam2_ == 0 && hostParam3_ == 0)
			dest = dataAddr_;
	}
	unsigned song = hostParam1_ & 0xff;
	switch (func) {
	case 0x20: /* primary BGM load */
		if (LoadSongToAddr(song, dest > 0 ? dest : dataAddr_, fileSize_, 0))
			hostStatus_ = 0x00;
		break;
	case 0x21: /* secondary BGM load */
		if (LoadSongToAddr(song, dest > 0 ? dest : data2Addr_, file2Size_, 1))
			hostStatus_ = 0x00;
		break;
	case 0x10: /* set dataaddr from real-mode DS:BX (hostParam3:hostParam2) */
		/* Ys/Ys2 Falcom glue OUT 07D4/07D6 then OUT 07D0,10h. Catalog leaves
		   dataaddr=0 — without this, TriggerPlay never preloads / skips cmd1. */
		{
			const int addr = ((int)hostParam3_ << 4) + (int)hostParam2_;
			if (addr > 0 && addr < 0x200000)
				dataAddr_ = addr;
			hostStatus_ = 0x00;
		}
		break;
	case 0x11:
		/* DOFMD_98.BIN / BRANM_98 play path: IN AX,07D4/07D6 → SI/DS as
		   real-mode song pointer, then INT 45h into MSC/MV22/MUSIC.BIN. */
		if (dofmd_) {
			hostParam2_ = (uint16_t)((unsigned)dataAddr_ & 0x000Fu);
			hostParam3_ = (uint16_t)((unsigned)dataAddr_ >> 4);
		} else {
			hostParam2_ = (uint16_t)(dataAddr_ & 0xffff);
			hostParam3_ = (uint16_t)((dataAddr_ >> 16) & 0xffff);
		}
		hostStatus_ = 0x00;
		break;
	default:
		hostStatus_ = 0xff;
		break;
	}
}

void CHardPc98::PitOut(uint16_t port, uint8_t data)
{
	if (port == PIT_CTRL) {
		/* control word: only ch0 modes matter for sound pacing */
		pitWriteHi_ = 0;
		pitReadHi_ = 0;
		return;
	}
	if (port == PIT_CT0) {
		if (!pitWriteHi_) {
			pitReload_ = (pitReload_ & 0xff00) | data;
			pitWriteHi_ = 1;
		} else {
			pitReload_ = (pitReload_ & 0x00ff) | ((uint16_t)data << 8);
			pitWriteHi_ = 0;
			pitCounter_ = pitReload_ ? pitReload_ : 65536u;
			pitRunning_ = 1;
			pitIrqPending_ = 0;
		}
	}
}

uint8_t CHardPc98::PitIn(uint16_t port)
{
	if (port != PIT_CT0) return 0xff;
	uint16_t v = pitReload_;
	if (!pitReadHi_) {
		pitReadHi_ = 1;
		return (uint8_t)(v & 0xff);
	}
	pitReadHi_ = 0;
	return (uint8_t)(v >> 8);
}

void CHardPc98::PitTick(uint64_t cpuCycles)
{
	if (!pitRunning_ || cpuHz_ <= 0) return;
	pitResidual_ += cpuCycles * (uint64_t)pitClockHz_;
	uint64_t ticks = pitResidual_ / (uint64_t)cpuHz_;
	pitResidual_ %= (uint64_t)cpuHz_;
	while (ticks > 0) {
		uint32_t step = pitCounter_;
		if (step == 0) step = 65536;
		if (ticks < step) {
			pitCounter_ = step - (uint32_t)ticks;
			ticks = 0;
		} else {
			ticks -= step;
			pitCounter_ = pitReload_ ? pitReload_ : 65536u;
			pitIrqPending_ = 1;
		}
	}
}

void CHardPc98::TickSide(uint64_t cpuCycles)
{
	PitTick(cpuCycles);
	/* Do NOT clear [085A] bit7 here every CPU quantum — that path was
	   forcing SORC SSG3 (R0A) to stick at 0x0F (鳴りっぱなし) while FM
	   still advanced. Clear once per PIT music tick in DeliverIrqs. */
	if (cpuHz_ > 0) {
		vsyncResidual_ += cpuCycles * 60ull;
		if (vsyncResidual_ >= (uint64_t)cpuHz_) {
			vsyncResidual_ %= (uint64_t)cpuHz_;
			vsyncPending_ = 1;
		}
	}
	if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
		/* accumulate OPN clocks separately in driver; here track IRQ edge */
		int irq = chip_->Irq() ? 1 : 0;
		if (irq && !irqEdgeSeen_) {
			irqEdgeSeen_ = 1;
			irqEdgeConsumed_ = 0;
		} else if (!irq) {
			irqEdgeSeen_ = 0;
		}
	}
	/* olteus_va: host advances DS:[003C]/[CC4D] only through handshake (≤0x11).
	   Past that, IRQ0 → MAP:09BC so the real sequencer owns the counter.
	   Use ~60Hz for IRQ pulses (VA picture tick); 600Hz starved REP STOSW
	   VRAM clears in MAP:32DD and blocked song load. */
	if (olteusMapSeg_ && olteusDataSeg_ && olteusTimerOn_ && cpuHz_ > 0) {
		olteusTimerResidual_ += cpuCycles * 60ull;
		uint8_t* mem = np2_mem();
		const unsigned base = (unsigned)olteusDataSeg_ << 4;
		while (mem && base + 0xCC4Fu < 0x200000u
			&& olteusTimerResidual_ >= (uint64_t)cpuHz_) {
			olteusTimerResidual_ -= (uint64_t)cpuHz_;
			unsigned flag = (unsigned)mem[base + 0xCC4D]
				| ((unsigned)mem[base + 0xCC4D + 1] << 8);
			if (flag < 0x0011u) {
				/* Handshake: several soft steps per IRQ slot so boot
				   still reaches 0x11 quickly without 600 IRQs/sec. */
				for (int step = 0; step < 10 && flag < 0x0011u; step++) {
					unsigned c = (unsigned)mem[base + 0x3C]
						| ((unsigned)mem[base + 0x3D] << 8);
					c = (c + 2u) & 0xffffu;
					mem[base + 0x3C] = (uint8_t)(c & 0xff);
					mem[base + 0x3D] = (uint8_t)((c >> 8) & 0xff);
					if (c == 0x00C8u) {
						flag = (flag + 1u) & 0xffffu;
						mem[base + 0xCC4D] = (uint8_t)(flag & 0xff);
						mem[base + 0xCC4D + 1] = (uint8_t)((flag >> 8) & 0xff);
						mem[base + 0x3C] = 0x64;
						mem[base + 0x3D] = 0x00;
					}
				}
			} else {
				/* Handshake done — request sequencer tick. */
				olteusIrqPulse_ = 1;
			}
		}
	}
	if (olteusDataSeg_) {
		uint8_t* mem = np2_mem();
		const unsigned base = (unsigned)olteusDataSeg_ << 4;
		if (mem && base + 0x5BCFu < 0x200000u) {
			/* play: CMP [5BCE],03E7 / JE spin — force off the magic wait value */
			if (mem[base + 0x5BCE] == 0xE7 && mem[base + 0x5BCF] == 0x03) {
				mem[base + 0x5BCE] = 0x00;
				mem[base + 0x5BCF] = 0x00;
			}
			if (mem[base + 0x5BCA] == 0 && mem[base + 0x5BCB] == 0)
				mem[base + 0x5BCA] = 0x01;
		}
	}
}

void CHardPc98::ArmOlteusVaTimer(uint16_t mapSeg)
{
	if (!mapSeg || mapSeg == (uint16_t)DOS98_TRAMP_SEG)
		return;
	olteusMapSeg_ = mapSeg;
	uint8_t* mem = np2_mem();
	if (!mem)
		return;
	/* MAP entry: MOV AX,ss; MOV SS,AX; MOV AX,ds; MOV DS,AX — DS is CS+0x0F86. */
	const unsigned ent = (unsigned)mapSeg << 4;
	uint16_t dataSeg = (uint16_t)(mapSeg + 0x0F86u);
	if (ent + 10u < 0x200000u
		&& mem[ent] == 0xB8 && mem[ent + 3] == 0x8E && mem[ent + 4] == 0xD0
		&& mem[ent + 5] == 0xB8 && mem[ent + 8] == 0x8E && mem[ent + 9] == 0xD8) {
		dataSeg = (uint16_t)(mem[ent + 6] | ((unsigned)mem[ent + 7] << 8));
	}
	olteusDataSeg_ = dataSeg;
	/* MAP image >64K; plant inside first paragraph at a BSS zero-run (FE86).
	   near CALL 09BC / IRET — music tick ends in near RET. */
	const unsigned trampOff = 0xFE86u;
	const unsigned base = (unsigned)mapSeg << 4;
	const unsigned tramp = base + trampOff;
	if (tramp + 16u >= 0x200000u)
		return;
	if (!olteusTrampOk_) {
		/* PUSH ES; PUSHA; PUSH DS; MOV AX,dataSeg; MOV DS,AX; CALL 09BC;
		   POP DS; POPA; POP ES; IRET
		   09BC clobbers AX/CX/ES — without a full save, IRQ mid REP STOSW
		   (MAP VRAM clear @32C2) never finishes and song load never runs. */
		mem[tramp + 0] = 0x06; /* PUSH ES */
		mem[tramp + 1] = 0x60; /* PUSHA */
		mem[tramp + 2] = 0x1E; /* PUSH DS */
		mem[tramp + 3] = 0xB8;
		mem[tramp + 4] = (uint8_t)(dataSeg & 0xff);
		mem[tramp + 5] = (uint8_t)(dataSeg >> 8);
		mem[tramp + 6] = 0x8E;
		mem[tramp + 7] = 0xD8;
		/* disp = 09BC - (FE86+8+3) = 09BC - FE91 = 0B2B */
		mem[tramp + 8] = 0xE8;
		mem[tramp + 9] = 0x2B;
		mem[tramp + 10] = 0x0B;
		mem[tramp + 11] = 0x1F; /* POP DS */
		mem[tramp + 12] = 0x61; /* POPA */
		mem[tramp + 13] = 0x07; /* POP ES */
		mem[tramp + 14] = 0xCF; /* IRET */
		mem[0x08 * 4 + 0] = (uint8_t)(trampOff & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)((trampOff >> 8) & 0xff);
		mem[0x08 * 4 + 2] = (uint8_t)(mapSeg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)((mapSeg >> 8) & 0xff);
		olteusTrampOk_ = 1;
	} else {
		/* Keep IVT08 on the trampoline if something rewrote it. */
		mem[tramp + 4] = (uint8_t)(dataSeg & 0xff);
		mem[tramp + 5] = (uint8_t)(dataSeg >> 8);
		mem[0x08 * 4 + 0] = (uint8_t)(trampOff & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)((trampOff >> 8) & 0xff);
		mem[0x08 * 4 + 2] = (uint8_t)(mapSeg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)((mapSeg >> 8) & 0xff);
	}
	picMask_ = (uint8_t)(picMask_ & 0xfeu);
	np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	/* Arm soft IRQ0 even before OUT 10A — boot waits on [CC4D] via this tick. */
	olteusTimerOn_ = 1;
	/* Skip MAP VRAM plane clears (CS:32C2 REP STOSW ×4). Audio does not
	   need them; under host IRQ0 they burn the play budget and never finish. */
	if (base + 0x32C2u < 0x200000u && mem[base + 0x32C2] == 0x8B)
		mem[base + 0x32C2] = 0xC3;
}

static int IvtHooked(uint8_t vec, int dosMode)
{
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	unsigned b = (unsigned)vec * 4u;
	uint16_t off = (uint16_t)(mem[b] | (mem[b + 1] << 8));
	uint16_t seg = (uint16_t)(mem[b + 2] | (mem[b + 3] << 8));
	/* Null vector. SORC98 installs handlers at 0000:xxxx (CS=0) — that is
	   valid; only reject 0000:0000. Also reject PC BIOS ROM (F000) and
	   PC-98 high-ROM aliases (F800–FFFF) left by a corrupt INT 18 boot. */
	if (seg == 0 && off == 0) return 0;
	if (seg >= 0xF000) return 0;
	if (dosMode && seg == DOS98_TRAMP_SEG) return 0;
	return 1;
}

int CHardPc98::Int60Hooked() const
{
	return IvtHooked(0x60, isDos_);
}

int CHardPc98::DeliverIrqs()
{
	if (g_pc98Eoi) {
		g_pc98Eoi = 0;
		/* Do not clear opnInService_ on PIC EOI alone. YM2608 IRQs are
		   level-triggered; clearing here before the ISR acks timer status
		   (reg 0x27 / status read) re-enters forever and hangs PumpCycles
		   (pc88vados tetrisva/shinrava long renders). */
	}
	/* NOPNDRV acks via OPN reg 0x27, not always PIC OCW2 — release when line drops. */
	if (chip_ && !chip_->Irq())
		opnInService_ = 0;

	/* MUSIC.COM long-BGM keepalive: while play-enable [0290]=1, hold the
	   auto-mute counter [0294] at 0 so AH=2's 16-bar mute never trips.
	   Also clear the channel-mute bytes [ch+3] that AH=1 leave stuck. */
	if (musicComKeepalive_ && IvtHooked(0x70, 1)) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned s70 = (unsigned)mem[0x70 * 4 + 2]
				| ((unsigned)mem[0x70 * 4 + 3] << 8);
			const unsigned b70 = s70 << 4;
			if (b70 + 0x2A0u < 0x200000u && mem[b70 + 0x290] == 1) {
				mem[b70 + 0x294] = 0;
				/* Channel control blocks sit at CS:0003/0013/… — bit0 mute. */
				for (unsigned ch = 0; ch < 6; ++ch) {
					const unsigned off = b70 + 0x03u + ch * 0x10u;
					if (off < 0x200000u && (mem[off] & 1))
						mem[off] = (uint8_t)(mem[off] & ~1u);
				}
			}
		}
	}

	uint16_t flags = np2_reg_get(NP2_R_FLAGS);
	if ((flags & 0x200) == 0) /* IF clear */
		return 0;

	if (pitIrqPending_ && (picMask_ & 0x01) == 0 && IvtHooked(PC98_TIMER_VEC, isDos_)) {
		pitIrqPending_ = 0;
		np2_interrupt((uint8_t)PC98_TIMER_VEC);
		return 1;
	}
	if (vsyncPending_ && (picMask_ & 0x04) == 0 && IvtHooked(PC98_VSYNC_VEC, isDos_)) {
		vsyncPending_ = 0;
		np2_interrupt((uint8_t)PC98_VSYNC_VEC);
		return 1;
	}
	/* Level-triggered OPN IRQ (matches PC88). Edge latch alone missed
	   asserts that happened in TickOpn after the previous DeliverIrqs. */
	if (chip_ && chip_->Irq() && !opnInService_) {
		uint8_t* mem = np2_mem();
		/* famistava plants OPN on INT14 during play — mirror only when INT0B
		   is still vacant (rtype keeps an INT0A thunk on INT0B). */
		if (mem && pc88VaIo_ && IvtHooked(0x14, isDos_) && !IvtHooked(PC98_OPN_IRQ_VEC, isDos_)) {
			const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
			const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
			mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
		/* mbmusp/MUSE: SSG I/O A = 0xC0 → driver hooks INT14 and EOIs the
		   slave. Deliver there (do not mirror onto INT0B). */
		uint8_t vec = PC98_OPN_IRQ_VEC;
		if ((ssgPortAJumper_ & 0xC0) == 0xC0 && IvtHooked(0x14, isDos_)) {
			vec = 0x14;
			picMask_ = (uint8_t)(picMask_ & ~(1u << 2)); /* cascade */
			slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4)); /* IRQ12 */
		}
		if (!IvtHooked(vec, isDos_)) {
			/* Do not fall back to VSYNC (0x0A) or other IRQ lines — that
			   mis-delivered OPN timer IRQs into SORC98's VSYNC stub. */
			return 0;
		}
		if (IvtHooked(vec, isDos_)) {
			if (vec >= 0x08 && vec <= 0x0F && (picMask_ & (1 << (vec - 0x08))) != 0)
				return 0;
			if (vec >= 0x10 && vec <= 0x17
				&& (slavePicMask_ & (1 << (vec - 0x10))) != 0)
				return 0;
			opnInService_ = 1;
			irqEdgeConsumed_ = 1;
			opnIrqDeliverCount_++;
			np2_interrupt(vec);
			return 1;
		}
	}
	return 0;
}

/* --- PC-98 MPU-401 UART @ E0D0/E0D2 (FMP3 -m / midiout catalog) ----------- */

static uint8_t s_pc98MidiRun, s_pc98MidiNeed, s_pc98MidiD0;

void CHardPc98::MidiCaptureReset()
{
	if (!midiBytes_) midiBytes_ = new uint8_t[CEMU_PC98_MIDI_CAP];
	if (!midiDelta_) midiDelta_ = new uint32_t[CEMU_PC98_MIDI_CAP];
	midiCount_ = 0;
	midiNoteOnCount_ = 0;
	midiPortOutCount_ = 0;
	midiLastCycle_ = cpuCycles_;
	mpuRxFull_ = 0;
	/* Do NOT queue a power-on FE here. FMP3 -m detect (CS:1790) INs E0D2
	   and requires status != 0 && bit6 clear (idle 0x80). A pending ACK
	   makes MidiStatusIn return 0x00, so detect fails → [1DBE]=0 and the
	   sequencer never emits MIDI (midiBytes=0). FE is pushed from RESET
	   (FFh) / UART-mode (3Fh) command handlers only. */
	mpuAckR_ = mpuAckW_ = 0;
	s_pc98MidiRun = s_pc98MidiNeed = s_pc98MidiD0 = 0;
}

void CHardPc98::MidiPushAck(uint8_t v)
{
	mpuAckQ_[mpuAckW_ & 7] = v;
	mpuAckW_++;
}

void CHardPc98::MidiCaptureByte(uint8_t v)
{
	if (!midiBytes_ || !midiDelta_) return;
	if (midiCount_ >= (unsigned)CEMU_PC98_MIDI_CAP) return;
	uint32_t delta = 0;
	if (cpuHz_ > 0) {
		const uint64_t dc = cpuCycles_ - midiLastCycle_;
		uint64_t ticks = (dc * 960ull) / ((uint64_t)cpuHz_ + 1ull);
		if (midiNoteOnCount_ == 0) {
			const uint64_t cap = 960ull / 4ull;
			if (ticks > cap) ticks = cap;
		}
		if (ticks > 0xffffffffull) ticks = 0xffffffffull;
		delta = (uint32_t)ticks;
	}
	midiLastCycle_ = cpuCycles_;
	midiDelta_[midiCount_] = delta;
	midiBytes_[midiCount_] = v;
	midiCount_++;

	if (v & 0x80) {
		s_pc98MidiRun = v;
		const uint8_t hi = (uint8_t)(v & 0xf0);
		s_pc98MidiNeed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
		s_pc98MidiD0 = 0;
	} else if (s_pc98MidiRun) {
		if (s_pc98MidiNeed == 2 && s_pc98MidiD0 == 0) {
			s_pc98MidiD0 = v;
		} else {
			const uint8_t hi = (uint8_t)(s_pc98MidiRun & 0xf0);
			const int ch = (int)(s_pc98MidiRun & 0x0f);
			if (hi == 0x90 || hi == 0x80) {
				const int note = (s_pc98MidiNeed == 2) ? (int)s_pc98MidiD0 : (int)v;
				const int vel = (s_pc98MidiNeed == 2) ? (int)v : 0;
				const int on = (hi == 0x90 && vel > 0) ? 1 : 0;
				if (on) midiNoteOnCount_++;
				FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MIDI);
				FmMonShadowMidiNote(ch, note, on);
				FmMonShadowWriteAuxReg(0x10 + (unsigned)(ch & 0x0f),
					(unsigned)(on ? (note & 0x7f) : 0));
			}
			s_pc98MidiD0 = 0;
			if ((s_pc98MidiRun & 0xf0) == 0xC0 || (s_pc98MidiRun & 0xf0) == 0xD0)
				s_pc98MidiNeed = 1;
			else
				s_pc98MidiNeed = 2;
		}
	}
}

void CHardPc98::MidiDataOut(uint8_t data)
{
	midiPortOutCount_++;
	/* FMP3 -m probe loops OUT 00h to E0D0 during resident install and would
	   fill the capture buffer with zeros before the first song byte. */
	if (!midiCapArmed_)
		return;
	if (mpuUart_ || modeMidi_) {
		MidiCaptureByte(data);
		/* FMP3 -m emits a standard MIDI UART stream (same as Wolf MUSDRV).
		   Bridge to OPN so probes / non-VST paths have audible output. */
		if (wolfBridgeEnable_)
			WolfCmdByte(data);
	}
}

void CHardPc98::MidiCmdOut(uint8_t data)
{
	if (data == 0xff) {
		mpuUart_ = 0;
		MidiPushAck(0xfe);
		return;
	}
	if (data == 0x3f) {
		mpuUart_ = 1;
		MidiPushAck(0xfe);
		return;
	}
	MidiPushAck(0xfe);
}

uint8_t CHardPc98::MidiStatusIn()
{
	/* Standard MPU-401 status (Roland / RBIL / FMP3):
	   bit7=1 → no RX data; bit6=1 → not ready for write.
	   Idle ready = 0x80. FMP3 Wait1 accepts only nonzero+bit6clear; OUT
	   helpers spin while bit6 set. Returning 0x40 (PC/AT swapped polarity)
	   made FMP3 -m detect fail (midiBytes=0) and hang any E0D0 write. */
	if (mpuAckR_ != mpuAckW_ || mpuRxFull_)
		return 0x00; /* RX available, write OK */
	return 0x80; /* no RX, write OK */
}

uint8_t CHardPc98::MidiDataIn()
{
	if (mpuAckR_ != mpuAckW_) {
		uint8_t v = mpuAckQ_[mpuAckR_ & 7];
		mpuAckR_++;
		return v;
	}
	if (mpuRxFull_) {
		mpuRxFull_ = 0;
		return mpuRx_;
	}
	return 0xfe;
}

/* --- Wolfteam E0D0 MUSDRV command stream → OPN soft bridge ---------------

   Confirmed by capture (.cursor/_cemu_wolf_e0d0_probe): the Wolfteam MUSDRV
   timer ISR emits a *standard MIDI byte stream* out port E0D0 (an MPU-401 /
   MIDI-board UART). It never programs the YM2203 for notes; the FM chip only
   receives an init/mute block. Titles that detect no MIDI board still stall.

   Since Hoot has no MIDI-board synth, we translate the live MIDI stream into
   YM2203/2608 FM voices so the song is actually audible on the OPN. This is a
   real (if reduced-polyphony) synthesis of the observed note events — a
   generic FM patch is voiced per note-on and keyed off on note-off. */

/* One-octave OPN(A) F-number table (C..B); block carries the octave. The same
   values work for OPN (3.9936 MHz /72) and OPNA (7.9872 MHz /144) since both
   yield the ~55.5 kHz FM base rate. */
static const uint16_t kWolfFnum[12] = {
	0x0269, 0x028E, 0x02B4, 0x02DE, 0x030B, 0x0339,
	0x036B, 0x03A0, 0x03D7, 0x0412, 0x0450, 0x0492
};

void CHardPc98::WolfBridgeReset()
{
	wolfRunStatus_ = 0;
	wolfDataIdx_ = 0;
	wolfDataNeed_ = 0;
	wolfInSysex_ = 0;
	wolfVoiceClock_ = 0;
	wolfVoiceCount_ = opnaMode ? 6 : 3;
	for (int i = 0; i < 6; i++) {
		wolfVoiceActive_[i] = 0;
		wolfVoiceMidiCh_[i] = -1;
		wolfVoiceNote_[i] = -1;
		wolfVoiceAge_[i] = 0;
	}
	memset(wolfChVol_, 127, sizeof(wolfChVol_));
	memset(wolfChExpr_, 127, sizeof(wolfChExpr_));
}

void CHardPc98::WolfOpnW(int bank, uint8_t reg, uint8_t val)
{
	if (!chip_) return;
	chip_->Write((uint32_t)bank, reg);
	chip_->Write((uint32_t)(bank | 1), val);
}

/* Voice v: 0-2 → FM1-3 (bank0), 3-5 → FM4-6 (bank1, OPNA only). Program a
   generic 4-carrier (algorithm 7) patch with level scaled by velocity and the
   MIDI channel volume/expression. */
void CHardPc98::WolfProgramVoice(int v, int vel, int midiCh)
{
	const int bank = (v < 3) ? 0 : 0x100;
	const int ci = (v < 3) ? v : (v - 3);
	int eff = vel;
	eff = eff * (int)wolfChVol_[midiCh & 15] / 127;
	eff = eff * (int)wolfChExpr_[midiCh & 15] / 127;
	if (eff < 0) eff = 0; if (eff > 127) eff = 127;
	/* Louder notes → smaller TL (0x10 loudest .. ~0x38 quiet). */
	uint8_t tl = (uint8_t)(0x10 + ((127 - eff) * 40 / 127));
	for (int op = 0; op < 4; op++) {
		const uint8_t o = (uint8_t)((op << 2) + ci);
		WolfOpnW(bank, (uint8_t)(0x30 + o), 0x01); /* DT=0 MUL=1 */
		WolfOpnW(bank, (uint8_t)(0x40 + o), tl);   /* TL */
		WolfOpnW(bank, (uint8_t)(0x50 + o), 0x1F); /* KS=0 AR=31 */
		WolfOpnW(bank, (uint8_t)(0x60 + o), 0x00); /* DR=0 */
		WolfOpnW(bank, (uint8_t)(0x70 + o), 0x00); /* SR=0 */
		WolfOpnW(bank, (uint8_t)(0x80 + o), 0x0A); /* SL=0 RR=10 */
		WolfOpnW(bank, (uint8_t)(0x90 + o), 0x00); /* SSG-EG off */
	}
	WolfOpnW(bank, (uint8_t)(0xB0 + ci), 0x07); /* algorithm 7, FB 0 */
	WolfOpnW(bank, (uint8_t)(0xB4 + ci), 0xC0); /* L+R on */
}

void CHardPc98::WolfNoteOn(int midiCh, int note, int vel)
{
	if (!chip_ || note < 0 || note > 127) return;
	wolfNoteOnCount_++;
	const int nv = wolfVoiceCount_ > 0 ? wolfVoiceCount_ : 3;
	/* Reuse a voice already holding this (ch,note); else a free one; else the
	   oldest active voice. */
	int v = -1;
	for (int i = 0; i < nv; i++)
		if (wolfVoiceActive_[i] && wolfVoiceMidiCh_[i] == midiCh && wolfVoiceNote_[i] == note) { v = i; break; }
	if (v < 0)
		for (int i = 0; i < nv; i++)
			if (!wolfVoiceActive_[i]) { v = i; break; }
	if (v < 0) {
		uint64_t best = ~0ull;
		for (int i = 0; i < nv; i++)
			if (wolfVoiceAge_[i] < best) { best = wolfVoiceAge_[i]; v = i; }
	}
	if (v < 0) return;
	const int chBits = (v < 3) ? v : (0x04 + (v - 3));
	/* Key off before retune to force a clean re-attack. */
	WolfOpnW(0, 0x28, (uint8_t)chBits);
	WolfProgramVoice(v, vel, midiCh);
	const int oct = note / 12;
	int block = oct - 2;
	if (block < 0) block = 0; if (block > 7) block = 7;
	const uint16_t fnum = kWolfFnum[note % 12];
	const int bank = (v < 3) ? 0 : 0x100;
	const int ci = (v < 3) ? v : (v - 3);
	WolfOpnW(bank, (uint8_t)(0xA4 + ci), (uint8_t)(((block & 7) << 3) | ((fnum >> 8) & 7)));
	WolfOpnW(bank, (uint8_t)(0xA0 + ci), (uint8_t)(fnum & 0xff));
	WolfOpnW(0, 0x28, (uint8_t)(0xF0 | chBits)); /* key on all 4 slots */
	wolfVoiceActive_[v] = 1;
	wolfVoiceMidiCh_[v] = midiCh;
	wolfVoiceNote_[v] = note;
	wolfVoiceAge_[v] = ++wolfVoiceClock_;
}

void CHardPc98::WolfNoteOff(int midiCh, int note)
{
	if (!chip_) return;
	wolfNoteOffCount_++;
	const int nv = wolfVoiceCount_ > 0 ? wolfVoiceCount_ : 3;
	for (int i = 0; i < nv; i++) {
		if (wolfVoiceActive_[i] && wolfVoiceMidiCh_[i] == midiCh && wolfVoiceNote_[i] == note) {
			const int chBits = (i < 3) ? i : (0x04 + (i - 3));
			WolfOpnW(0, 0x28, (uint8_t)chBits); /* key off */
			wolfVoiceActive_[i] = 0;
			wolfVoiceNote_[i] = -1;
		}
	}
}

void CHardPc98::WolfAllNotesOff()
{
	if (!chip_) return;
	for (int i = 0; i < 6; i++) {
		if (wolfVoiceActive_[i]) {
			const int chBits = (i < 3) ? i : (0x04 + (i - 3));
			WolfOpnW(0, 0x28, (uint8_t)chBits);
		}
		wolfVoiceActive_[i] = 0;
		wolfVoiceNote_[i] = -1;
	}
}

void CHardPc98::WolfMidiDispatch(uint8_t status, uint8_t d0, uint8_t d1)
{
	const uint8_t cmd = (uint8_t)(status & 0xF0);
	const int ch = status & 0x0F;
	switch (cmd) {
	case 0x90:
		if (d1 > 0) WolfNoteOn(ch, d0, d1);
		else WolfNoteOff(ch, d0);
		break;
	case 0x80:
		WolfNoteOff(ch, d0);
		break;
	case 0xB0:
		wolfCtrlCount_++;
		if (d0 == 0x07) wolfChVol_[ch] = d1;        /* channel volume */
		else if (d0 == 0x0B) wolfChExpr_[ch] = d1;  /* expression */
		else if (d0 == 0x78 || d0 == 0x7B) WolfAllNotesOff(); /* all sound/notes off */
		break;
	default:
		/* Program change / pitch bend / aftertouch: not voiced by this bridge. */
		break;
	}
}

/* Parse the raw MIDI byte stream (handles running status, 2/3-byte channel
   messages, sysex skip, and 0xFF stream reset). */
void CHardPc98::WolfCmdByte(uint8_t data)
{
	if (data & 0x80) {
		if (data >= 0xF8)
			return; /* realtime: ignore */
		if (data == 0xF0) { wolfInSysex_ = 1; return; }
		if (data == 0xF7) { wolfInSysex_ = 0; wolfRunStatus_ = 0; return; }
		if (data == 0xFF) { /* system reset within stream */
			if (wolfBridgeEnable_) WolfAllNotesOff();
			wolfRunStatus_ = 0; wolfDataIdx_ = 0; wolfInSysex_ = 0;
			return;
		}
		if (data >= 0xF1 && data <= 0xF6) {
			wolfRunStatus_ = 0;
			wolfDataIdx_ = 0;
			wolfDataNeed_ = (data == 0xF2) ? 2 : ((data == 0xF1 || data == 0xF3) ? 1 : 0);
			return;
		}
		/* Channel voice status. */
		wolfRunStatus_ = data;
		wolfDataIdx_ = 0;
		const uint8_t hi = (uint8_t)(data & 0xF0);
		wolfDataNeed_ = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
		return;
	}
	if (wolfInSysex_ || wolfRunStatus_ == 0)
		return;
	wolfData_[wolfDataIdx_++] = data;
	if (wolfDataIdx_ < wolfDataNeed_)
		return;
	wolfDataIdx_ = 0; /* running status: keep wolfRunStatus_ */
	if (wolfBridgeEnable_)
		WolfMidiDispatch(wolfRunStatus_, wolfData_[0], wolfData_[1]);
}

uint8_t CHardPc98::PortIn(uint16_t port)
{
	/* PC-98 display status: bit 5 changes across vertical retrace.  Several
	   resident glues synchronize command hand-off by waiting for a low->high
	   transition (mscd_98 does this for 18 frames).  Returning the generic
	   open-bus FF here trapped those programs in their first wait loop. */
	if (port == 0x00A0) {
		const uint64_t halfFrame =
			(cpuHz_ > 120) ? (uint64_t)cpuHz_ / 120ull : 1ull;
		return ((cpuCycles_ / halfFrame) & 1ull) ? 0x20 : 0x00;
	}
	/* PC-88VA: PC-88 OPN ports read the same chip status/data. */
	if (pc88VaIo_) {
		switch (port) {
		case 0x44: case 0xA8: port = OPN_ADDR0; break;
		case 0x45: case 0xA9: port = OPN_DATA0; break;
		case 0x46: case 0xAC: port = OPN_ADDR1; break;
		case 0x47: case 0xAD: port = OPN_DATA1; break;
		default: break;
		}
	}
	switch (port) {
	case OPN_ADDR0: {
		uint8_t s = chip_ ? chip_->ReadStatus() : 0xff;
		/* olteus MAP DA40: IN 44h / TEST 80h busy-wait. ymfm stays busy
		   unless clocks advance between OUT and IN — mask for VA play. */
		if (pc88VaIo_)
			s = (uint8_t)(s & (uint8_t)~0x80);
		return s;
	}
	case OPN_DATA0:
		/* SSG I/O A (reg 0x0E): board IRQ jumper. MUSE/mbmusp read bits7-6
		   to pick INT14h; default open-bus 0 makes them hook INT0B while EOI
		   goes to the slave (hootrip preset_muse_irq_jumper). */
		if (chip_ && opnLatchedAddr_ == 0x0E && (ssgPortAJumper_ & 0x80))
			return ssgPortAJumper_;
		return chip_ ? chip_->ReadData() : 0xff;
	case OPN_ADDR1: {
		uint8_t s = chip_ ? chip_->ReadStatusHi() : 0xff;
		if (pc88VaIo_)
			s = (uint8_t)(s & (uint8_t)~0x80);
		return s;
	}
	case OPN_DATA1: return chip_ ? chip_->ReadDataHi() : 0xff;
	case EXT_CMD: return extCmd_;
	case EXT_SONG: return (uint8_t)(extSong_ & 0xff);
	case EXT_SONG + 1: return (uint8_t)(extSong_ >> 8);
	case EXT_PARAM: return (uint8_t)(extParam_ & 0xff);
	case EXT_PARAM + 1: return (uint8_t)(extParam_ >> 8);
	case EXT_STATE: return stubState_;
	case HOST_CMD: return hostStatus_;
	case HOST_P1: return (uint8_t)(hostParam1_ & 0xff);
	case HOST_P1 + 1: return (uint8_t)(hostParam1_ >> 8);
	case HOST_P2: return (uint8_t)(hostParam2_ & 0xff);
	case HOST_P2 + 1: return (uint8_t)(hostParam2_ >> 8);
	case HOST_P3: return (uint8_t)(hostParam3_ & 0xff);
	case HOST_P3 + 1: return (uint8_t)(hostParam3_ >> 8);
	case SOUND86_ID:
		/* PC-9801-86 @ 0188h: upper nibble Sound ID = 4 (MAME/NP2/Undocumented9801).
		   bit0 = YM2608 enhanced; bit1 = OPNA mask. Default mask=0 → ID 0x40. */
		if (!opnaMode)
			return 0xff; /* 26K / OPN-only: port absent */
		return (uint8_t)(0x40 | (sound86Mask_ & 0x03));
	/* A466–A66E: leave open-bus unless a title needs soft 86PCM.
	   Stubbing empty-FIFO here made FMP3 take a silent PCM path (vg2). */
	case 0x506:
		/* PC-88VA: MAP polls IN 506h bit0 as busy (olteus CS:7968).
		   Open-bus 0xFF spun forever before song load / sequencer. */
		if (pc88VaIo_)
			return 0x00;
		return 0xff;
	case PIC_CMD:
	case SLAVE_PIC_CMD:
		/* OCW3 IRR/ISR polls (e.g. ys_98 MANPR1 CS:5123 after arming
		   timer B). Unhandled reads were 0xFF and spun forever (opnW
		   hundreds of thousands, key=0). Soft-PIC has no latched ISR.
		   PC-88VA MAP (olteus CS:0C50): when DS:[00C0]!=0 wait for bit6
		   then clear; when [00C0]==0 bit6 must be clear or it re-spins. */
		if (pc88VaIo_ && port == SLAVE_PIC_CMD && olteusDataSeg_) {
			uint8_t* mem = np2_mem();
			const unsigned a = ((unsigned)olteusDataSeg_ << 4) + 0xC0u;
			if (mem && a + 1u < 0x200000u) {
				const unsigned c0 = (unsigned)mem[a] | ((unsigned)mem[a + 1] << 8);
				if (c0 != 0)
					return 0x40;
			}
			return 0x00;
		}
		return 0x00;
	case PIC_MASK: return picMask_;
	case SLAVE_PIC_MASK: return slavePicMask_;
	case PIT_CT0: case PIT_CTRL: return PitIn(port);
	case WOLF_SYNC0:
	case 0xC0D0: /* alternate PC-98 MIDI data port */
		if (modeMidi_ || mpuUart_)
			return MidiDataIn();
		if (port == WOLF_SYNC0)
			return wolfSyncRun_ ? 0xFE : 0xFF;
		return 0xff;
	case WOLF_SYNC1:
	case 0xC0D2:
		if (modeMidi_ || mpuUart_)
			return MidiStatusIn();
		if (port == WOLF_SYNC1)
			return wolfSyncRun_ ? 0x00 : 0xFF;
		return 0xff;
	default: return 0xff;
	}
}

void CHardPc98::PortOut(uint16_t port, uint8_t data)
{
	/* olteus_va: OUT 10A,0022 arms the picture/interval tick; 00/0C disarms.
	   Capture CS as MAP seg if the far-table hook has not run yet. */
	if (pc88VaIo_ && port == 0x10A) {
		if (data == 0x22) {
			uint16_t cs = np2_reg_get(NP2_R_CS);
			if (!olteusMapSeg_ && cs && cs != (uint16_t)DOS98_TRAMP_SEG)
				olteusMapSeg_ = cs;
			if (olteusMapSeg_)
				ArmOlteusVaTimer(olteusMapSeg_);
			olteusTimerOn_ = 1;
		} else if (data == 0x00 || data == 0x0C) {
			olteusTimerOn_ = 0;
		}
	}
	/* PC-88VA: music uses classic PC-88 OPN (44h/A8h); BIOSD also pokes
	   PC-98 188h. Stage address per port family so interleaved OUTs cannot
	   steal the latch (SSG C / mixer corruption). */
	if (pc88VaIo_) {
		switch (port) {
		case 0x44: case 0xA8:
			/* Latch + commit address. BPS tetrisva OPNA detect does
			   OUT 44h,FFh / IN 45h and expects ym2608 ID code 01 — without
			   Write(0) the chip address stays stale and [851A] never sets. */
			vaPc88LatchedAddr_ = data;
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0, data);
				opnLatchedAddr_ = data;
			}
			return;
		case 0x45: case 0xA9:
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0, vaPc88LatchedAddr_);
				chip_->Write(1, data);
				opnLatchedAddr_ = vaPc88LatchedAddr_;
				opnWriteCount_++;
				if (opnLogCount_ < 64) {
					opnLogAddr_[opnLogCount_] = vaPc88LatchedAddr_;
					opnLogData_[opnLogCount_] = data;
					opnLogCount_++;
				}
				if (vaPc88LatchedAddr_ == 0x28 && (data & 0xf0) != 0)
					opnKeyOnCount_++;
				if ((vaPc88LatchedAddr_ >= 0xa0 && vaPc88LatchedAddr_ <= 0xa2) ||
					(vaPc88LatchedAddr_ >= 0xa4 && vaPc88LatchedAddr_ <= 0xa6))
					opnFnumCount_++;
			}
			return;
		case 0x46: case 0xAC:
			vaPc88LatchedAddrHi_ = data;
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0x100, data);
				opnLatchedAddrHi_ = data;
			}
			return;
		case 0x47: case 0xAD:
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0x100, vaPc88LatchedAddrHi_);
				chip_->Write(0x101, data);
				opnLatchedAddrHi_ = vaPc88LatchedAddrHi_;
				opnWriteCount_++;
			}
			return;
		case OPN_ADDR0: case OPN_DATA0:
		case OPN_ADDR1: case OPN_DATA1:
			vaPc98PortHits_++;
			break; /* fall through — keep PC-98 path with re-assert */
		default:
			break;
		}
	}
	switch (port) {
	case OPN_ADDR0:
		if (chip_) {
			chip_->Write(0, data);
			opnLatchedAddr_ = data;
			opnWriteCount_++;
		}
		break;
	case OPN_DATA0:
		if (chip_) {
			if (pc88VaIo_)
				chip_->Write(0, opnLatchedAddr_);
			chip_->Write(1, data);
			if (opnLogCount_ < 64) {
				opnLogAddr_[opnLogCount_] = opnLatchedAddr_;
				opnLogData_[opnLogCount_] = data;
				opnLogCount_++;
			}
			if (opnLatchedAddr_ == 0x28 && (data & 0xf0) != 0)
				opnKeyOnCount_++;
			if (((opnLatchedAddr_ & 0xf0) == 0x40 || (opnLatchedAddr_ & 0xf0) == 0x50) && data < 0x7f)
				opnTlLiveCount_++;
			if ((opnLatchedAddr_ >= 0xa0 && opnLatchedAddr_ <= 0xa2) ||
				(opnLatchedAddr_ >= 0xa4 && opnLatchedAddr_ <= 0xa6))
				opnFnumCount_++;
			if (opnLatchedAddr_ == 0x24 || opnLatchedAddr_ == 0x25 || opnLatchedAddr_ == 0x27)
				opnTimerCount_++;
			if (opnLatchedAddr_ == 0x0E)
				ssgPortAJumper_ = data;
			opnWriteCount_++;
		}
		break;
	case OPN_ADDR1:
		if (chip_) {
			chip_->Write(0x100, data);
			opnLatchedAddrHi_ = data;
			opnWriteCount_++;
		}
		break;
	case OPN_DATA1:
		if (chip_) {
			if (pc88VaIo_)
				chip_->Write(0x100, opnLatchedAddrHi_);
			chip_->Write(0x101, data);
			if (opnLogCount_ < 64) {
				opnLogAddr_[opnLogCount_] = (uint16_t)(0x100u + opnLatchedAddrHi_);
				opnLogData_[opnLogCount_] = data;
				opnLogCount_++;
			}
			if (opnLatchedAddrHi_ == 0x28 && (data & 0xf0) != 0)
				opnKeyOnCount_++;
			if (((opnLatchedAddrHi_ & 0xf0) == 0x40 || (opnLatchedAddrHi_ & 0xf0) == 0x50) && data < 0x7f)
				opnTlLiveCount_++;
			opnWriteCount_++;
		}
		break;
	case EXT_CMD: extCmd_ = data; break;
	case EXT_SONG: extSong_ = (extSong_ & 0xff00) | data; break;
	case EXT_SONG + 1: extSong_ = (extSong_ & 0x00ff) | ((uint16_t)data << 8); break;
	case EXT_PARAM: extParam_ = (extParam_ & 0xff00) | data; break;
	case EXT_PARAM + 1: extParam_ = (extParam_ & 0x00ff) | ((uint16_t)data << 8); break;
	case EXT_STATE: stubState_ = data; break;
	case HOST_CMD:
		hostFunc_ = data;
		HostService(data);
		break;
	case HOST_P1: hostParam1_ = (hostParam1_ & 0xff00) | data; break;
	case HOST_P1 + 1: hostParam1_ = (hostParam1_ & 0x00ff) | ((uint16_t)data << 8); break;
	case HOST_P2: hostParam2_ = (hostParam2_ & 0xff00) | data; break;
	case HOST_P2 + 1: hostParam2_ = (hostParam2_ & 0x00ff) | ((uint16_t)data << 8); break;
	case HOST_P3: hostParam3_ = (hostParam3_ & 0xff00) | data; break;
	case HOST_P3 + 1: hostParam3_ = (hostParam3_ & 0x00ff) | ((uint16_t)data << 8); break;
	case WOLF_SYNC0:
	case 0xC0D0:
		/* midiout / FMP -m: capture UART MIDI. Wolfteam FM: command bridge. */
		if (modeMidi_ || mpuUart_ || port == 0xC0D0) {
			MidiDataOut(data);
			break;
		}
		wolfCmdWriteCount_++;
		if (wolfCmdLogCount_ < sizeof(wolfCmdLog_))
			wolfCmdLog_[wolfCmdLogCount_++] = data;
		WolfCmdByte(data);
		break;
	case WOLF_SYNC1:
	case 0xC0D2:
		if (modeMidi_ || mpuUart_ || port == 0xC0D2)
			MidiCmdOut(data);
		break;
	case PIC_CMD:
		if ((data & 0x10) != 0) {
			picMask_ = 0;
			picMasterIcw1_ = data;
			picMasterIcw_ = 1;
		} else if ((data & 0x20) != 0) {
			g_pc98Eoi = 1;
		}
		break;
	case PIC_MASK:
		if (picMasterIcw_) {
			picMasterIcw_++;
			if (picMasterIcw_ >= 3 + (picMasterIcw1_ & 1))
				picMasterIcw_ = 0;
		} else {
			picMask_ = data;
		}
		break;
	case SLAVE_PIC_CMD:
		if ((data & 0x10) != 0) {
			slavePicMask_ = 0;
			picSlaveIcw1_ = data;
			picSlaveIcw_ = 1;
		} else if ((data & 0x20) != 0) {
			g_pc98Eoi = 1;
		}
		break;
	case SLAVE_PIC_MASK:
		if (picSlaveIcw_) {
			picSlaveIcw_++;
			if (picSlaveIcw_ >= 3 + (picSlaveIcw1_ & 1))
				picSlaveIcw_ = 0;
		} else {
			slavePicMask_ = data;
		}
		break;
	case PIT_CT0: case PIT_CTRL: PitOut(port, data); break;
	case VSYNC_ACK: vsyncPending_ = 0; break;
	case IO_DELAY: break;
	case SOUND86_ID:
		/* Preserve Sound ID nibble; update mask/enhance bits (MAME mask_w). */
		if (opnaMode)
			sound86Mask_ = (uint8_t)(data & 0x03);
		break;
	case SOUND86_FIFO_STAT:
	case SOUND86_FIFO_CTL:
	case SOUND86_DAC_CTL:
	case SOUND86_FIFO_DAT:
	case SOUND86_MUTE:
		/* Accept writes so probes don't fault; no soft PCM engine yet. */
		if (opnaMode) {
			if (port == SOUND86_FIFO_CTL) sound86FifoCtl_ = data;
			else if (port == SOUND86_DAC_CTL) sound86DacCtl_ = data;
			else if (port == SOUND86_MUTE) sound86Mute_ = (uint8_t)(data & 1);
		}
		break;
	default: break;
	}
}

int CHardPc98::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	memset(mem, 0, 0x200000);
	StageBanks(fs, ge);
	nopnDrv_ = 0;
	dofmd_ = 0;
	fmd98_ = 0;
	fmdSongOff_ = 0;
	rx98_ = 0;
	rxSongOff_ = 0;
	prog98_ = 0;
	progSongAddr_ = 0;
	bst398_ = 0;
	koei98_ = 0;
	cal98_ = 0;
	madp98_ = 0;
	n3golf98_ = 0;
	dks98_ = 0;
	mdplay98_ = 0;
	wolfteam98_ = 0;
	wolfGateStop_ = 0x5B48;
	wolfGatePlay_ = 0x5B5A;
	wolfSongPtr_ = 0x5B5D;
	wolfSongBuf_ = 0x7E5E;
	wolfTitleWord_ = 0x643A;
	wolfFlagA_ = 0x0662;
	wolfMiSeg_ = 0;
	wolfSyncRun_ = 0;
	wolfCmdLogCount_ = 0;
	wolfCmdWriteCount_ = 0;
	wolfNoteOnCount_ = 0;
	wolfNoteOffCount_ = 0;
	wolfCtrlCount_ = 0;
	WolfBridgeReset();
	dosStubReady_ = 0;

	if (isDos_)
		return BootDos(fs, ge, titleCode);

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "binary") != 0
			&& _stricmp(r->type, "string") != 0)
			continue;
		/* KOEI packs code as seg:off dword (0xSSSSOOOO); others use flat phys. */
		const unsigned off = Pc98RomPhys(r->offset);
		if (off >= 0x200000u) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		uint8_t inlineBuf[256];
		if (!data || !sz) {
			const int nIn = Pc98ParseInlineRom(r->name, inlineBuf, (int)sizeof(inlineBuf));
			if (nIn <= 0) continue;
			data = inlineBuf;
			sz = (unsigned)nIn;
		}
		unsigned n = sz;
		if (off + n > 0x200000u)
			n = 0x200000u - off;
		memcpy(mem + off, data, n);
		if (r->name[0] && _strnicmp(r->name, "NOPNDRV", 7) == 0)
			nopnDrv_ = 1;
		/* DOFMD_98 and BRANM_98 share the INT 45 + host-0x11 play path
		   (seg:off song ptr via 07D4/07D6). BRANM skips INT 14h. */
		if (r->name[0] && (_strnicmp(r->name, "DOFMD", 5) == 0
			|| _strnicmp(r->name, "BRANM", 5) == 0))
			dofmd_ = 1;
		if (r->name[0] && _strnicmp(r->name, "FMD98", 5) == 0)
			fmd98_ = 1;
		if (r->name[0] && _stricmp(r->name, "RX.BIN") == 0)
			rx98_ = 1;
		/* Ys2 / Brandish-era Falcom OPN driver without RX.BIN glue name. */
		if (r->name[0] && (_stricmp(r->name, "2608.BIN") == 0
			|| _stricmp(r->name, "2203.BIN") == 0
			|| _stricmp(r->name, "10_005.BIN") == 0)
			&& dataAddr_ <= 0 && fileSize_ > 0)
			rx98_ = 1;
		/* Falcom PROG.BIN glue: only when catalog dataaddr is set (no invent). */
		if (data && n >= 0x90 && n <= 512 && dataAddr_ > 0 && fileSize_ > 0
			&& r->name[0] && _stricmp(r->name, "PROG.BIN") == 0) {
			prog98_ = 1;
			progSongAddr_ = dataAddr_;
		}
		if (r->name[0] && _strnicmp(r->name, "KOEI98", 6) == 0)
			koei98_ = 1;
		/* BirdySoft CAL/PAL/BEAST family: glue stub + OPN driver install INT60
		   but never hook IRQ3 (IVT 0x0B). Play spins on wait-flag [DS:269B]
		   until the relocated OPN ISR runs. Detect by stub/bin name. */
		if (r->name[0] && (_strnicmp(r->name, "CAL", 3) == 0
			|| _strnicmp(r->name, "PAL", 3) == 0
			|| _strnicmp(r->name, "THANATOS", 8) == 0
			|| _strnicmp(r->name, "BEAST", 5) == 0
			|| _strnicmp(r->name, "BST3", 4) == 0))
			cal98_ = 1;
		/* Beast3: 64K OPN driver at 0xFC00; glue cmd0 uses AH!=0 to pick
		   load (AH==0 is stop). Small title codes never take the load path. */
		if (r->name[0] && (_strnicmp(r->name, "BST3", 4) == 0
			|| _stricmp(r->name, "0FC00.BIN") == 0))
			bst398_ = 1;
		/* QueenSoft MADP: catalog binary at 0x100 plants INT40 → driver
		   (AL-indexed API @0xA000/0x7000). Glue INT7F maps cmd→INT40 AL. */
		if (r->name[0] && _strnicmp(r->name, "MADP", 4) == 0)
			madp98_ = 1;
		if (r->name[0] && _strnicmp(r->name, "N3GOLF", 6) == 0)
			n3golf98_ = 1;
		/* KSK DKS/FQ family: dks.bin/fq3.bin glue + BGMDK/BGMDRV @0x35000.
		   INT7F cmd1 → INT69 AH=0; songs are size-prefixed banks; host 07D4/07D6
		   are real-mode ES:BX (table/BSS). No catalog dataaddr → cmd1 never ran. */
		if (r->name[0] && (_stricmp(r->name, "DKS.BIN") == 0
			|| _stricmp(r->name, "FQ3.BIN") == 0
			|| _strnicmp(r->name, "BGMDK", 5) == 0
			|| _strnicmp(r->name, "BGM_DS", 5) == 0
			|| _strnicmp(r->name, "BGMFQ", 5) == 0
			|| _strnicmp(r->name, "BGMDRV", 6) == 0))
			dks98_ = 1;
		/* Glodia MDPLAY.BIN (etembl/ragnrk/biblem2): INT7F play uses INT 4A/40.
		   Driver installs INT40–4D and a PIT ISR, but the ISR is only written to
		   IVT08 from a late path — ensure INT08 is hooked after boot. MDPLAYD
		   (difrlm) already installs INT08 in init and must stay untouched. */
		if (r->name[0] && (_stricmp(r->name, "MDPLAY.BIN") == 0
			|| _strnicmp(r->name, "MDPLAY", 6) == 0
			|| _stricmp(r->name, "MDRIVE.BIN") == 0
			|| _strnicmp(r->name, "MDRIVE", 6) == 0)
			&& _strnicmp(r->name, "MDPLAYD", 7) != 0)
			mdplay98_ = 1;
		/* gulfwr nests boot as 1/000_BOOT — match by basename. */
		{
			const char* bootBase = r->name;
			const char* slash = strrchr(r->name, '/');
			if (!slash) slash = strrchr(r->name, '\\');
			if (slash) bootBase = slash + 1;
			if (r->name[0] && _stricmp(bootBase, "000_BOOT") == 0) {
			wolfteam98_ = 1;
			/* Discover relocated play-gate / flag / title BSS via d_98 opcode
			   context. Sibling MU* boots keep the same pre/post bytes but move
			   abs16 (gou 560C/062F/5EFE, zan2 57AA/062F/6E84, …). Writing the
			   d_98-only 0662 assist into relocated boots can force silence. */
			wolfGateStop_ = 0x5B48;
			wolfGatePlay_ = 0x5B5A;
			wolfSongPtr_ = 0x5B5D;
			wolfSongBuf_ = 0x7E5E;
			wolfTitleWord_ = 0x643A;
			wolfFlagA_ = 0x0662;
			if (data && n > 32) {
				static const uint8_t kPrePlay[] = { 0x85, 0x9D, 0xE8, 0x28, 0x01, 0x2E };
				static const uint8_t kPostPlay[] = { 0x33, 0xC0, 0xC3, 0x9D };
				static const uint8_t kPreStop[] = { 0x5B, 0x58, 0x9D, 0xF8, 0xC3, 0x2E };
				static const uint8_t kPostStop[] = { 0x07, 0x1F, 0x5F, 0x5E };
				/* Classic: OUT 64 / POP ES / POP DS / POPA / IRET.
				   dmdply: OUT 64 / POPA / POP DS / POP ES / IRET. */
				static const uint8_t kFlagPost[] = { 0xE6, 0x64, 0x07, 0x1F, 0x61, 0xCF };
				static const uint8_t kFlagPostAlt[] = { 0xE6, 0x64, 0x61, 0x1F, 0x07, 0xCF };
				uint16_t gp = 0, gs = 0;
				for (unsigned p = 6; p + 9 < n; p++) {
					if (data[p] != 0xC6 || data[p + 1] != 0x06) continue;
					if (data[p + 4] == 0xFF
						&& memcmp(data + p - 6, kPrePlay, 6) == 0
						&& memcmp(data + p + 5, kPostPlay, 4) == 0)
						gp = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
					if (data[p + 4] == 0x00
						&& memcmp(data + p - 6, kPreStop, 6) == 0
						&& memcmp(data + p + 5, kPostStop, 4) == 0)
						gs = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
				}
				if (gs && gp) {
					/* Canonical layout: play = stop+0x12, song far-ptr @stop+0x15.
					   dmdply folds play into flagA+2 (060B) — still usable. */
					wolfGateStop_ = gs;
					wolfGatePlay_ = gp;
					if ((uint16_t)(gp - gs) == 0x0012)
						wolfSongPtr_ = (uint16_t)(gs + 0x15);
					else
						wolfSongPtr_ = (uint16_t)(gs + 0x15);
				}
				/* INT4C play-armed byte: C6 06 fa,FF / OUT 64h / … / IRET */
				for (unsigned p = 0; p + 11 < n; p++) {
					if (data[p] != 0xC6 || data[p + 1] != 0x06 || data[p + 4] != 0xFF)
						continue;
					if (memcmp(data + p + 5, kFlagPost, 6) == 0
						|| memcmp(data + p + 5, kFlagPostAlt, 6) == 0) {
						wolfFlagA_ = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
						break;
					}
				}
				/* Title word: CMP [tw],AX / JE / MOV [tw],AX / C6 [fa+1],FF */
				for (unsigned p = 0; p + 12 < n; p++) {
					if (data[p] != 0x3B || data[p + 1] != 0x06 || data[p + 4] != 0x74)
						continue;
					if (data[p + 6] != 0xA3 || data[p + 9] != 0xC6 || data[p + 10] != 0x06)
						continue;
					const uint16_t tw = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
					const uint16_t tw2 = (uint16_t)(data[p + 7] | (data[p + 8] << 8));
					const uint16_t fa1 = (uint16_t)(data[p + 11] | (data[p + 12] << 8));
					if (tw == tw2 && fa1 == (uint16_t)(wolfFlagA_ + 1)) {
						wolfTitleWord_ = tw;
						break;
					}
				}
				/* Song shadow buffer: MOV SI/DI,imm near INT 4C (AH=08 path).
				   dmdply has no INT4C — detect REP STOSW clear of DI buffer. */
				for (unsigned p = 0; p + 12 < n; p++) {
					if (data[p] != 0xBE && data[p] != 0xBF) continue;
					const unsigned imm = (unsigned)data[p + 1] | ((unsigned)data[p + 2] << 8);
					if (imm < 0x6000u || imm > 0xB000u) continue;
					int hasInt4c = 0;
					for (unsigned q = p; q < p + 24 && q + 1 < n; q++) {
						if (data[q] == 0xCD && data[q + 1] == 0x4C) {
							hasInt4c = 1;
							break;
						}
					}
					if (hasInt4c) {
						wolfSongBuf_ = (uint16_t)imm;
						break;
					}
					if (data[p] == 0xBF && p + 9 < n
						&& data[p + 3] == 0xB9
						&& data[p + 6] == 0x2B && data[p + 7] == 0xC0
						&& data[p + 8] == 0xF3 && data[p + 9] == 0xAB) {
						wolfSongBuf_ = (uint16_t)imm;
						break;
					}
				}
			}
			}
		}
	}

	/* biblem2 OPN twin boots MAIN.EXE (CS=6000) and stays silent. Zip also
	   ships etembl-identical mdplay.bin — stage it at 0x600 and boot CS=0060
	   like ragnrk/etembl (MDDRV already at 0x10000; FMV at dataaddr). */
	if (mdplay98_ && bootCs_ == 0x6000 && fs) {
		unsigned sz = 0;
		const unsigned char* stub = CEmuZipFsFind(fs, "mdplay.bin", &sz);
		if (stub && sz >= 64 && sz <= 256) {
			memcpy(mem + 0x600, stub, sz < 0x200u ? sz : 0x200u);
			bootCs_ = 0x0060;
			bootIp_ = 0;
		}
	}

	/* DOFMD_98.BIN boot: INT 45h (MSC init) then INT 14h, then hooks INT 7Fh.
	   Without a BIOS serial stub INT 14h vector is 0000:0000 and boot never
	   reaches the INT 7Fh install — park a lone IRET below the glue at 0x600. */
	if (dofmd_) {
		mem[0x500] = 0xCF;
		mem[0x14 * 4 + 0] = 0x00;
		mem[0x14 * 4 + 1] = 0x05;
		mem[0x14 * 4 + 2] = 0x00;
		mem[0x14 * 4 + 3] = 0x00;
	}

	/* Wolfteam d_98.bin: after CALL 464E / INT 4C AH=08 it walks a file-id
	   list at 7000:0000 (LODSB / CMP AL,F9 / INT 4C AH=F0). Real game path
	   loads that list via INT 43 AX=005F after FS mount (INT 43 AX=8000), but
	   the stub patches 80D7→RET and skips mount — 7000 stays zeroed and boot
	   spins forever, never reaching INT 7Fh install or PIT enable. Park a
	   lone F9 terminator so the loop exits; TriggerPlay starts PIT + [5B5A].

	   Also: CALL 464E → CALL 5A9A polls ports E0D0/E0D2; open-bus 0xFF makes
	   TEST AL,40 spin. That hang is after INT 08 install, so the stub never
	   returns to OUT 07E8=81. NOP the handshake to a single RET (shared
	   across Wolfteam 000_BOOT builds that keep this helper near 5A9A —
	   locate by the E0D2 busy-wait signature). */
	if (wolfteam98_) {
		/* MUSDRV streams standard MIDI out E0D0; bridge it to the OPN. */
		wolfBridgeEnable_ = 1;
		WolfBridgeReset();
		mem[0x70000] = 0xF9;
		/* Only RET the 464E handshake busy-wait (first hit). ISR delay stubs
		   at 5B00+ are left intact and use WOLF_SYNC1=0 (not busy). */
		static const uint8_t kSyncBusy[] = { 0xBA, 0xD2, 0xE0, 0xEC, 0xA8, 0x40, 0x75, 0xFB };
		for (unsigned p = 0x600; p + 8 < 0x10000u; p++) {
			if (memcmp(mem + p, kSyncBusy, sizeof(kSyncBusy)) == 0) {
				mem[p] = 0xC3;
				break;
			}
		}
		/* Prefer explicit MI* banks over incidental MF (suzaku BL50). */
		int miBest = -1, miAny = -1;
		for (int fi = 0; fi < fs->fileCount; fi++) {
			const unsigned char* d = fs->files[fi].data;
			const unsigned sz = fs->files[fi].size;
			if (!d || sz < 16) continue;
			if (!(d[0] == 'M' && d[1] == 'F' && d[2] == 0x01 && d[13] == 0x28))
				continue;
			if (miAny < 0) miAny = fi;
			const wchar_t* wfn = fs->files[fi].path;
			const wchar_t* wbase = wfn;
			const wchar_t* wslash = wcsrchr(wfn, L'/');
			if (!wslash) wslash = wcsrchr(wfn, L'\\');
			if (wslash) wbase = wslash + 1;
			/* Prefer real instrument banks (MM, MD, OPNM). Tiny MI stubs
			   (zanyks 0B8_MI01 at 1K) must not beat MM01. */
			if (wcsstr(wbase, L"_MM") || wcsstr(wbase, L"_MD")
				|| wcsstr(wbase, L"OPNM")
				|| (wcsstr(wbase, L"_MI") && sz >= 2048u)) {
				miBest = fi;
				break;
			}
		}
		const int miFi = miBest >= 0 ? miBest : miAny;
		if (miFi >= 0) {
			const unsigned char* d = fs->files[miFi].data;
			unsigned nMi = fs->files[miFi].size;
			if (0x90000u + nMi > 0x200000u) nMi = 0x200000u - 0x90000u;
			memcpy(mem + 0x90000, d, nMi);
			wolfMiSeg_ = 0x9000;
		} else {
			/* apros ships songs only — plant a minimal MF header so INT4C
			   AH=00 / bank init has a non-bogus instrument block. */
			static const uint8_t kMinMf[] = {
				'M', 'F', 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
				0x18, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00
			};
			memcpy(mem + 0x90000, kMinMf, sizeof(kMinMf));
			wolfMiSeg_ = 0x9000;
		}
	}

	/* QueenSoft MADP: INT40 is an AL-indexed API (not the OPN ISR). Boot
	   glue INT40 AL=19 TESTs ES:[0501] bit3 (FM present) before programming
	   YM — plant before CPU start. Play INT7F maps cmd→AL=1D/1B. */
	if (madp98_)
		mem[0x501] = (uint8_t)(mem[0x501] | 0x08);

	/* SORC98 (and similar bootcs stubs): after CALL BIOS init they idle on
	   INT 18h (AH=98h). Catalog BIOS never hooks INT 18 — vector stays
	   0000:0000 and the first idle iteration executes IVT as code, flipping
	   handler segments to FFFF. Park IRET at 0x510 before CPU start. */
	{
		mem[0x510] = 0xCF;
		mem[0x18 * 4 + 0] = 0x10;
		mem[0x18 * 4 + 1] = 0x05;
		mem[0x18 * 4 + 2] = 0x00;
		mem[0x18 * 4 + 3] = 0x00;
	}

	/* Falcom 00BIOS / PR.* (ys3/xana2/…, catalog dummysndrom=1): second boot
	   CALL reads A000:3FEE and word [0536], then only runs FM init when the
	   derived flag at [0702] is non-zero. Zeroed RAM skips OPN setup entirely
	   (INT51/52 stay inert, opnW=0). Plant the BIOS equipment bit that the
	   probe tests (bit2 of [0536]) so FM init runs like a machine with a
	   sound board — same role as hoot's dummysndrom.
	   xana2 PR.NO0/PR.NO5/xana2e also require A000:0FEE bit3 set; without it
	   they take the no-FM path and never plant INT14/OPN. Keep bit0 for ys3. */
	if (dummySndRom_ || (bootCs_ == 0 && bootIp_ == 0x0600)) {
		mem[0x536] = (uint8_t)(mem[0x536] | 0x04);
		if (0xA0000u + 0x3FEEu < 0x200000u)
			mem[0xA0000u + 0x3FEEu] = (uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x09);
	}

	np2_reset();
	np2_set_adrsmask(0x000FFFFFu);
	np2_setextsize(0);
	np2_set_v30(0);

	/* Honor bootcs=0 when bootip is set (SORC98: CS=0000 IP=F000 → phys 0xF000).
	   Only default CS to 0x60 when both bootcs and bootip are unset/zero. */
	uint16_t cs = (uint16_t)((bootCs_ != 0 || bootIp_ != 0) ? bootCs_ : 0x0060);
	uint16_t ip = (uint16_t)bootIp_;
	np2_set_cs_ip(cs, ip);
	np2_set_ss_sp(0x1000, 0xFFFE);
	np2_reg_set(NP2_R_DS, cs);
	np2_reg_set(NP2_R_ES, cs);
	np2_reg_set(NP2_R_FLAGS, 0x0202); /* IF set */

	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
	extCmd_ = 0;
	stubState_ = 0;
	cpuCycles_ = 0;
	opnPumpResidual_ = 0;
	picMask_ = 0x00; /* unmask all for bootcs drivers that never program PIC */
	slavePicMask_ = 0x00;
	opnInService_ = 0;
	irqEdgeSeen_ = 0;
	irqEdgeConsumed_ = 0;
	if (opnaMode && chip_)
		CEmuLoadExternalYm2608Adpcm(chip_);
	return 1;
}

static void DosStripHash(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	int j = 0;
	int atTok = 1;
	for (int i = 0; in[i] && j < outCap - 1; i++) {
		const char c = in[i];
		if (c == '#' && atTok) continue;
		out[j++] = c;
		atTok = (c == ' ' || c == '\t') ? 1 : 0;
	}
	out[j] = 0;
}

static void DosSplitCmd(const char* cmd, char* name, int nameCap, char* tail, int tailCap)
{
	if (name && nameCap > 0) name[0] = 0;
	if (tail && tailCap > 0) tail[0] = 0;
	if (!cmd) return;
	while (*cmd == ' ' || *cmd == '\t') cmd++;
	int i = 0;
	while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t') i++;
	if (name && nameCap > 0) {
		int n = i;
		if (n >= nameCap) n = nameCap - 1;
		memcpy(name, cmd, (size_t)n);
		name[n] = 0;
	}
	const char* t = cmd + i;
	while (*t == ' ' || *t == '\t') t++;
	if (tail && tailCap > 0)
		strncpy_s(tail, (size_t)tailCap, t, _TRUNCATE);
}

static int DosIsEngineName(const char* name)
{
	if (!name || !name[0]) return 0;
	const char* ext = strrchr(name, '.');
	if (!ext) return 0;
	return _stricmp(ext, ".EXE") == 0
		|| _stricmp(ext, ".COM") == 0
		|| _stricmp(ext, ".DRV") == 0
		|| _stricmp(ext, ".SYS") == 0;
}

void CHardPc98::MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0
			&& _stricmp(r->type, "device") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		unsigned char donorBuf[256 * 1024];
		unsigned donorSz = 0;
		/* Song-only zips (gdm_mo/guyna/kizuato/nekoex) omit PMD_98.COM even
		   though the catalog lists it — pull the driver from a sibling pack. */
		if ((!data || !sz) && fs->zipPath[0] && DosIsEngineName(r->name)) {
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			static const wchar_t* kDonors[] = {
				L"xenon_98.zip", L"eveppz8_98.zip", L"chobaku_98.zip",
				L"frnunv98.zip", NULL
			};
			wchar_t donorPath[MAX_PATH];
			wcsncpy_s(donorPath, fs->zipPath, _TRUNCATE);
			wchar_t* slash = wcsrchr(donorPath, L'\\');
			if (!slash) slash = wcsrchr(donorPath, L'/');
			if (slash) {
				for (int d = 0; kDonors[d]; d++) {
					wcscpy_s(slash + 1, _countof(donorPath) - (slash + 1 - donorPath),
						kDonors[d]);
					if (CEmuZipFsExtractOne(donorPath, base, donorBuf,
						(unsigned)sizeof(donorBuf), &donorSz) && donorSz > 0) {
						data = donorBuf;
						sz = donorSz;
						break;
					}
				}
			}
		}
		if (!data || !sz) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		dos_.AddFile(base, data, sz);
	}
}

void CHardPc98::BindDosRomHandles(const CEmuGameEntry* ge)
{
	if (!ge) return;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0)
			continue;
		const int off = r->offset;
		/* Handles go up to DOS98_HANDLE_MAX-1 (fc98v12 songs past 0x30). */
		if (off < 0 || off >= DOS98_HANDLE_MAX) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (_stricmp(r->type, "conin") == 0)
			dos_.SetHandleText((uint16_t)off, base);
		else
			dos_.SetHandle((uint16_t)off, base);
	}
}

static int DosShellStarts(const CEmuGameEntry* ge, const char* const* prefixes)
{
	if (!ge || !prefixes) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "shell") != 0) continue;
		const char* n = ge->rom[i].name;
		if (!n || !n[0]) continue;
		for (int p = 0; prefixes[p]; p++) {
			const size_t plen = strlen(prefixes[p]);
			if (_strnicmp(n, prefixes[p], plen) == 0)
				return 1;
		}
	}
	return 0;
}


/* Name-load ADVH (EB 06 USDdrv, no "03 30"): install writes mov ax,CS+0x33
   for bind/data while the OEM ISR keeps mov ds,cs. watagolf finishes a
   +0x330 offset reloc (ISR ~069B); name-load never does. At BootDos, retarget
   bind-path immediates (off>=0x800) to the live INT F1 CS and plant INT0B.
   Early CS+0x33 refs (@034B/@0442) stay — AL=0 needs them. AL=1 still walks
   channel slots through @04AB, so TriggerPlay save/restores the ISR prologue. */
static int AdvhNormalizeNameLoadResident(uint8_t* mem, unsigned sF1)
{
	if (!mem || !sF1 || sF1 == (unsigned)DOS98_TRAMP_SEG)
		return 0;
	const unsigned fb = sF1 << 4;
	const unsigned ent = fb + ((unsigned)mem[0xF1 * 4] | ((unsigned)mem[0xF1 * 4 + 1] << 8));
	if (ent + 10 >= 0x200000u)
		return 0;
	if (!(mem[ent] == 0xEB && mem[ent + 1] == 0x06 && mem[ent + 2] == 'U'))
		return 0;
	const unsigned wrong = sF1 + 0x33u;
	int nBind = 0;
	for (unsigned off = 0x800; off + 3 < 0x5000u && fb + off + 3 < 0x200000u; off++) {
		if (mem[fb + off] != 0xB8)
			continue;
		const unsigned imm = (unsigned)mem[fb + off + 1]
			| ((unsigned)mem[fb + off + 2] << 8);
		if (imm != wrong)
			continue;
		mem[fb + off + 1] = (uint8_t)(sF1 & 0xff);
		mem[fb + off + 2] = (uint8_t)((sF1 >> 8) & 0xff);
		nBind++;
	}
	if (nBind < 2)
		return 0;
	/* Do NOT memcpy install bytes to +0x330 (corrupts name-load BSS).
	   Do NOT plant INT0B yet: thin AL=1 channel state + early OEM ISR
	   key-offs the bind notes. Bind→CS retarget alone restores audible AL=1. */
	return 1;
}

static void AdvhApplyResidentFixups(uint8_t* mem, unsigned sF1)
{
	const unsigned fb = sF1 << 4;
	for (unsigned off = 0; off + 5 < 0x5000u && fb + off + 5 < 0x200000u; off++) {
		if (mem[fb + off] == 0x9A) {
			unsigned to = (unsigned)mem[fb + off + 1] | ((unsigned)mem[fb + off + 2] << 8);
			unsigned ts = (unsigned)mem[fb + off + 3] | ((unsigned)mem[fb + off + 4] << 8);
			int d = (int)ts - (int)sF1;
			if (d < 0) d = -d;
			if (d > 0 && d < 0x100) {
				mem[fb + off + 3] = (uint8_t)(sF1 & 0xff);
				mem[fb + off + 4] = (uint8_t)((sF1 >> 8) & 0xff);
				ts = sF1;
			}
			if (ts == sF1 && to >= 0x600 && to < 0xA00) {
				const unsigned neu = to + 0x330;
				if (neu < 0x2000 && fb + neu + 2 < 0x200000u) {
					const uint8_t lo = mem[fb + to];
					const uint8_t hi = mem[fb + neu];
					const int loCode = (lo == 0x55 || lo == 0x50 || lo == 0x60 || lo == 0xFC || lo == 0xE8);
					const int hiCode = (hi == 0x55 || hi == 0x50 || hi == 0x60 || hi == 0xFC
						|| hi == 0xE8 || hi == 0xBB || hi == 0x8B || hi == 0x33);
					if ((!loCode && hiCode) || (to >= 0x6D0 && to < 0x900)) {
						mem[fb + off + 1] = (uint8_t)(neu & 0xff);
						mem[fb + off + 2] = (uint8_t)((neu >> 8) & 0xff);
					}
				}
			}
		}
		/* CS: jmp [reg+disp16] → command tables 1340/1380 (+0x330). */
		if (mem[fb + off] == 0x2E && mem[fb + off + 1] == 0xFF
			&& (mem[fb + off + 2] == 0xA5 || mem[fb + off + 2] == 0x95)) {
			const unsigned a = (unsigned)mem[fb + off + 3] | ((unsigned)mem[fb + off + 4] << 8);
			if (a == 0x1340u || a == 0x1380u) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 3] = (uint8_t)(neu & 0xff);
				mem[fb + off + 4] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
	}
	/* Abs16 data refs into the pre-reloc island (gate, flags) → +0x330. */
	for (unsigned off = 0x200; off + 4 < 0x2800u && fb + off + 4 < 0x200000u; off++) {
		const uint8_t b0 = mem[fb + off];
		if (b0 == 0xA0 || b0 == 0xA2 || b0 == 0xA1 || b0 == 0xA3) {
			const unsigned a = (unsigned)mem[fb + off + 1] | ((unsigned)mem[fb + off + 2] << 8);
			if (a < 0x100) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 1] = (uint8_t)(neu & 0xff);
				mem[fb + off + 2] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
		if ((b0 == 0xF6 || b0 == 0xF7) && (mem[fb + off + 1] & 0xC7) == 0x06) {
			const unsigned a = (unsigned)mem[fb + off + 2] | ((unsigned)mem[fb + off + 3] << 8);
			if (a < 0x100) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 2] = (uint8_t)(neu & 0xff);
				mem[fb + off + 3] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
		if ((b0 == 0x8B || b0 == 0x89 || b0 == 0x80 || b0 == 0x81 || b0 == 0x83
			|| b0 == 0xC6 || b0 == 0xC7 || b0 == 0x8A || b0 == 0x88
			|| b0 == 0xFF || b0 == 0xFE)
			&& (mem[fb + off + 1] & 0xC7) == 0x06) {
			const unsigned a = (unsigned)mem[fb + off + 2] | ((unsigned)mem[fb + off + 3] << 8);
			if (a < 0x100) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 2] = (uint8_t)(neu & 0xff);
				mem[fb + off + 3] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
	}
}

const char* CHardPc98::SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const
{
	if (!ge) return NULL;
	/* olteus_va: all songs are file@-1; MAP builds A:\MUSIC#F/P.MUS from title. */
	static char olteusSong[16];
	static const char* kOlteusSong[] = { "olteus", NULL };
	if (DosShellStarts(ge, kOlteusSong)) {
		const unsigned n = titleCode & 0x0fu;
		const char dig = (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
		const char fp = opnaMode ? 'F' : 'P';
		_snprintf_s(olteusSong, _TRUNCATE, "MUSIC%c%c.MUS", dig, fp);
		return olteusSong;
	}
	const int low = (int)(titleCode & 0xff);
	const int full = (int)titleCode;
	for (int pass = 0; pass < 2; pass++) {
		const int want = pass == 0 ? full : low;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "file") != 0) continue;
			if (r->offset != want) continue;
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			return base;
		}
	}
	/* cplay98/mdrv list song banks as conin (offset == title low byte). */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "conin") != 0) continue;
		if (r->offset != low) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (DosIsEngineName(base))
			continue;
		return base;
	}
	return NULL;
}

void CHardPc98::BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode)
{
	const char* sf = SelectedDosSong(ge, titleCode);
	/* hootrip: cplay/fplay open by ASCIIZ name; mdrv_98/mddrv_98 same (INT D2 AL=2).
	   Do NOT match bare mdrv98+mlp_hoot (content on handle 0). */
	static const char* kCplay[] = { "cplay", "fplay", NULL };
	static const char* kOpenName[] = {
		"cplay", "fplay", "musdrv", "mbmusp", "mdrv_9", "mddrv_9",
		"mlalf", "MLALF", "mlfplay", "play5", "ibgm", NULL
	};
	const int cplayFamily = DosShellStarts(ge, kCplay);
	int opensByName = cplayFamily || DosShellStarts(ge, kOpenName);
	/* famistava conin: INT7F AH=3F reads the ASCIIZ name from handle 0, then
	   AH=3D opens the real file — must not overwrite with song bytes. */
	if (sf && ge && !opensByName) {
		const int low = (int)(titleCode & 0xff);
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "conin") != 0 || r->offset != low)
				continue;
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			if (_stricmp(base, sf) == 0) {
				opensByName = 1;
				break;
			}
		}
	}
	/* usd_98 (ADVBIOS/ADVH): INT7F AH=3F reads song BYTES from BX=0
	   (CX=4000/FFFF). ADVH packs list songs only as conin@title — the
	   famistava heuristic above would bind the filename text (len=10 for
	   "DC_02P.USO") and leave keyOn=0. Always use binary handles. */
	{
		static const char* kUsdSong[] = { "usd_98", "usd98", NULL };
		if (DosShellStarts(ge, kUsdSong))
			opensByName = 0;
	}

	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		if (opensByName) {
			/* Filename text on handle 0 — driver opens via INT21 AH=3D. */
			dos_.SetHandleText(0, sf);
		} else {
			dos_.SetHandle(0, sf);
			dos_.SetHandle(5, sf);
			dos_.SetHandle(0x0B, sf);
			/* PMD_98 reads the song handle == title low byte (pre-bound at install). */
			const unsigned low = titleCode & 0xff;
			if (low < (unsigned)DOS98_HANDLE_MAX)
				dos_.SetHandle((uint16_t)low, sf);
		}
	}
	extCmd_ = 0;
	const unsigned byte2 = (titleCode >> 16) & 0xff;
	const unsigned hiByte = (titleCode >> 8) & 0xff;
	/* ARTDI packed NTL.PAC: title 0xHHSS — high byte = pack handle, low = index.
	   Stub wants the full word on EXT_SONG (hootrip). */
	int pacTitle = 0;
	int voiTitle = 0;
	if (ge) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "voice") != 0)
				continue;
			const char* base = r->name ? r->name : "";
			for (const char* p = base; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			const char* dot = strrchr(base, '.');
			if (!dot) continue;
			if (_stricmp(dot, ".PAC") == 0 && (unsigned)r->offset == hiByte && hiByte != 0)
				pacTitle = 1;
			if ((_stricmp(dot, ".VOI") == 0 || _stricmp(r->type, "voice") == 0)
				&& byte2 != 0 && (unsigned)r->offset == byte2)
				voiTitle = 1;
		}
	}
	if (cplayFamily) {
		/* INT 7F AH=9: in-bank index on EXT param (0x7E4). */
		extSong_ = 0;
		extParam_ = (uint16_t)byte2;
	} else if (pacTitle) {
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (voiTitle) {
		/* MDR external-voice: EXT_PARAM = voice handle (byte2). */
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = (uint16_t)byte2;
	} else if (pc88VaIo_) {
		/* PC-88VA DOS overlay glue (tetrisva/rtypeva/shinrava/famista*):
		   IN 7E4 reads only the low byte of EXT_PARAM as play mode.
		   Titles are either 0x0001xxxx (tetrisva) or 0xNN0000xx (rtype
		   0x01000010 / famista 0x04000010) — take byte2, or byte3 if zero.
		   olteus.com INT7F: IN 7E0 — 0=far 00DF (init), 2=far 03F0 (play). */
		static const char* kOlteusCmd[] = { "olteus", NULL };
		if (DosShellStarts(ge, kOlteusCmd))
			extCmd_ = 2;
		extSong_ = (uint16_t)(titleCode & 0xff);
		unsigned mode = (titleCode >> 16) & 0xff;
		if (mode == 0)
			mode = (titleCode >> 24) & 0xff;
		extParam_ = (uint16_t)mode;
	} else if (byte2 != 0) {
		extSong_ = (uint16_t)byte2;
		extParam_ = (uint16_t)hiByte;
	} else {
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = 0;
	}
}

static unsigned Pc98DosLin(uint16_t seg, uint16_t off)
{
	return ((unsigned)seg << 4) + (unsigned)off;
}

static void Pc98Wr16(uint8_t* mem, unsigned addr, uint16_t v)
{
	mem[addr] = (uint8_t)(v & 0xff);
	mem[addr + 1] = (uint8_t)(v >> 8);
}

int CHardPc98::RunDosDevices(const CEmuGameEntry* ge, uint64_t budgetCycles)
{
	if (!ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	int nOk = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "device") != 0) continue;
		const char* base = r->name ? r->name : "";
		for (const char* p = base; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		char name[DOS98_NAME];
		int nj = 0;
		for (const char* p = base; *p && *p != ' ' && *p != '\t' && nj < DOS98_NAME - 1; p++)
			name[nj++] = *p;
		name[nj] = 0;

		/* MUSE/SDD devices pick IRQ from SSG I/O A bits7-6; force INT14 path. */
		if (chip_ && (strstr(name, "MUSE") || strstr(name, "muse")
			|| strstr(name, "SDD") || strstr(name, "sdd"))) {
			ssgPortAJumper_ = 0xC0;
			chip_->Write(0, 0x0E);
			chip_->Write(1, 0xC0);
			opnLatchedAddr_ = 0x0E;
		}

		uint16_t loadSeg = 0, stratOff = 0, intrOff = 0;
		if (!dos_.LoadDeviceImage(mem, name, &loadSeg, &stratOff, &intrOff) || !loadSeg)
			continue;

		const uint16_t reqSeg = 0x0050;
		const uint16_t reqOff = 0x0100;
		const unsigned req = Pc98DosLin(reqSeg, reqOff);
		memset(mem + req, 0, 0x20);
		mem[req + 0] = 0x16;
		mem[req + 2] = 0x00; /* INIT */

		const uint16_t launchSeg = (uint16_t)DOS98_IDLE_SEG;
		const uint16_t stratPtr = 0x0040;
		const uint16_t intrPtr = 0x0044;
		const unsigned L0 = Pc98DosLin(launchSeg, 0);
		/* MOV AX,reqSeg; MOV ES,AX; MOV BX,reqOff; CALL FAR [stratPtr] */
		mem[L0 + 0x00] = 0xB8;
		mem[L0 + 0x01] = (uint8_t)(reqSeg & 0xff);
		mem[L0 + 0x02] = (uint8_t)(reqSeg >> 8);
		mem[L0 + 0x03] = 0x8E; mem[L0 + 0x04] = 0xC0;
		mem[L0 + 0x05] = 0xBB;
		mem[L0 + 0x06] = (uint8_t)(reqOff & 0xff);
		mem[L0 + 0x07] = (uint8_t)(reqOff >> 8);
		mem[L0 + 0x08] = 0x2E; /* CS: */
		mem[L0 + 0x09] = 0xFF; mem[L0 + 0x0A] = 0x1E;
		mem[L0 + 0x0B] = (uint8_t)(stratPtr & 0xff);
		mem[L0 + 0x0C] = (uint8_t)(stratPtr >> 8);
		mem[L0 + 0x0D] = 0xB8;
		mem[L0 + 0x0E] = (uint8_t)(reqSeg & 0xff);
		mem[L0 + 0x0F] = (uint8_t)(reqSeg >> 8);
		mem[L0 + 0x10] = 0x8E; mem[L0 + 0x11] = 0xC0;
		mem[L0 + 0x12] = 0xBB;
		mem[L0 + 0x13] = (uint8_t)(reqOff & 0xff);
		mem[L0 + 0x14] = (uint8_t)(reqOff >> 8);
		mem[L0 + 0x15] = 0x2E; /* CS: */
		mem[L0 + 0x16] = 0xFF; mem[L0 + 0x17] = 0x1E;
		mem[L0 + 0x18] = (uint8_t)(intrPtr & 0xff);
		mem[L0 + 0x19] = (uint8_t)(intrPtr >> 8);
		/* OUT 7E8,82; HLT */
		mem[L0 + 0x1A] = 0xBA; mem[L0 + 0x1B] = 0xE8; mem[L0 + 0x1C] = 0x07;
		mem[L0 + 0x1D] = 0xB0; mem[L0 + 0x1E] = 0x82;
		mem[L0 + 0x1F] = 0xEE;
		mem[L0 + 0x20] = 0xF4;
		Pc98Wr16(mem, Pc98DosLin(launchSeg, stratPtr), stratOff);
		Pc98Wr16(mem, Pc98DosLin(launchSeg, stratPtr) + 2, loadSeg);
		Pc98Wr16(mem, Pc98DosLin(launchSeg, intrPtr), intrOff);
		Pc98Wr16(mem, Pc98DosLin(launchSeg, intrPtr) + 2, loadSeg);

		np2_reg_set(NP2_R_DS, loadSeg);
		np2_reg_set(NP2_R_ES, reqSeg);
		np2_set_ss_sp(launchSeg, 0xFFFE);
		np2_set_cs_ip(launchSeg, 0x0000);
		np2_reg_set(NP2_R_FLAGS, 0x0202);
		stubState_ = 0;
		const uint64_t start = cpuCycles_;
		while (cpuCycles_ - start < budgetCycles) {
			if (stubState_ == 0x82)
				break;
			if (DeliverIrqs())
				continue;
			uint16_t cs = np2_reg_get(NP2_R_CS);
			uint16_t ip = np2_reg_get(NP2_R_IP);
			const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
			if (phys < 0x200000 && mem[phys] == 0xF4) {
				uint8_t vec = 0;
				if (dos_.TrapVector(cs, ip, &vec)) {
					CEmuDos98Result res = dos_.ServiceInt(mem, vec);
					if (res == DOS98_TERMINATED || res == DOS98_RESIDENT)
						break;
					dos_.IretReturn(mem);
					cpuCycles_ += 50;
					TickSide(50);
					continue;
				}
				cpuCycles_ += 200;
				TickSide(200);
				continue;
			}
			const int32_t cyc = np2_step();
			const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
			cpuCycles_ += u;
			TickSide(u);
			if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
				uint64_t ot = u * (uint64_t)opnHz_ / (uint64_t)cpuHz_;
				if (ot) chip_->AdvanceClocks(ot);
			}
		}
		if (IvtHooked(0xC8, 1) || IvtHooked(0xC0, 1) || IvtHooked(0xC3, 1))
			nOk++;
	}
	return nOk;
}

int CHardPc98::RunDosCommand(const char* cmdline, uint64_t budgetCycles)
{
	char stripped[256];
	char name[96];
	char tail[160];
	DosStripHash(cmdline, stripped, (int)sizeof(stripped));
	DosSplitCmd(stripped, name, (int)sizeof(name), tail, (int)sizeof(tail));
	const unsigned char* image = NULL;
	unsigned imageSize = 0;
	int isExe = 0;
	if (!dos_.ResolveProgram(name, &image, &imageSize, &isExe) || !image)
		return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	char pspTail[162];
	if (tail[0])
		_snprintf_s(pspTail, _TRUNCATE, " %s", tail);
	else
		pspTail[0] = 0;
	int ok = isExe ? dos_.LoadExe(mem, image, imageSize, pspTail)
		: dos_.LoadCom(mem, image, imageSize, pspTail);
	if (!ok) return 0;

	dosStubReady_ = 0;
	const uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budgetCycles) {
		if (stubState_ == 0x81) {
			dosStubReady_ = 1;
			return 1;
		}
		if (DeliverIrqs())
			continue;
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		uint8_t* m = np2_mem();
		const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
		if (m && phys < 0x200000 && m[phys] == 0xF4) {
			uint8_t vec = 0;
			if (dos_.TrapVector(cs, ip, &vec)) {
				CEmuDos98Result res = dos_.ServiceInt(m, vec);
				if (res == DOS98_TERMINATED || res == DOS98_RESIDENT)
					return 1;
				dos_.IretReturn(m);
				continue;
			}
			/* Genuine idle HLT — advance timers. */
			const uint64_t q = 200;
			cpuCycles_ += q;
			TickSide(q);
			if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
				uint64_t ot = q * (uint64_t)opnHz_ / (uint64_t)cpuHz_;
				if (ot) chip_->AdvanceClocks(ot);
			}
			continue;
		}
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
			uint64_t ot = u * (uint64_t)opnHz_ / (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
	}
	return stubState_ == 0x81 ? 1 : 0;
}

int CHardPc98::BootDos(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;

	np2_reset();
	np2_set_adrsmask(0x000FFFFFu);
	np2_setextsize(0);
	np2_set_v30(0);
	memset(mem, 0, 0xA0000);

	dos_.Reset();
	dos_.InitArena(mem);
	dos_.InstallTrampolines(mem);
	dos_.InstallDosStructures(mem);
	/* DOS packs can still depend on fixed firmware/code images.  In
	   particular, PONYCA's MSCDRV front end probes the SOUND.ROM signature
	   at CEE0:0004 and installs INT D2 from that ROM before exposing INT 7E.
	   The early isDos_ return in LoadRoms used to skip every code/binary ROM,
	   leaving a valid-looking INT 7E wrapper backed by the DOS D2 trampoline. */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "binary") != 0
			&& _stricmp(r->type, "string") != 0)
			continue;
		const unsigned off = Pc98RomPhys(r->offset);
		if (off >= 0x200000u)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		uint8_t inlineBuf[256];
		if (!data || !sz) {
			const int nIn = Pc98ParseInlineRom(r->name, inlineBuf,
				(int)sizeof(inlineBuf));
			if (nIn <= 0)
				continue;
			data = inlineBuf;
			sz = (unsigned)nIn;
		}
		unsigned n = sz;
		if (off + n > 0x200000u)
			n = 0x200000u - off;
		memcpy(mem + off, data, n);
	}
	MaterializeDosFiles(fs, ge);
	BindDosRomHandles(ge);
	dosGe_ = ge;
	dosSong_[0] = 0;
	const char* sf = SelectedDosSong(ge, titleCode);
	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		dos_.SetHandle(0x0B, sf);
	}

	extSong_ = (uint16_t)(titleCode & 0xff);
	extParam_ = 0;
	extCmd_ = 0;
	stubState_ = 0;
	cpuCycles_ = 0;
	opnPumpResidual_ = 0;
	picMask_ = 0xff;
	slavePicMask_ = 0xff;
	picMasterIcw_ = 0;
	picSlaveIcw_ = 0;
	opnInService_ = 0;
	irqEdgeSeen_ = 0;
	irqEdgeConsumed_ = 0;
	opnWriteCount_ = 0;
	opnKeyOnCount_ = 0;
	opnTlLiveCount_ = 0;
	opnFnumCount_ = 0;
	opnTimerCount_ = 0;
	opnIrqDeliverCount_ = 0;
	opnLogCount_ = 0;

	/* Generous shell budget: PMDB2+PMDPCM packs need several seconds.
	   imd_1 (PMDB2 without #/Mxx): catalog PMD→PCM→glue re-inits and drops
	   the PPC bank — run glue before PCM. Packs with #/Mxx (imd_2..4,
	   fc98v13) need catalog order (glue last). */
	const uint64_t setupBudget = (uint64_t)cpuHz_ * 8ull; /* ~8s per shell */
	/* mbmusp/MUSDRV: SSG I/O A bits7-6 select INT14; EOI assumes slave. */
	static const char* kSsgJumperShell[] = {
		"mbmus", "MBMUS", "musdrv", "MUSDRV", NULL
	};
	if (chip_ && DosShellStarts(ge, kSsgJumperShell)) {
		ssgPortAJumper_ = 0xC0;
		chip_->Write(0, 0x0E);
		chip_->Write(1, 0xC0);
		opnLatchedAddr_ = 0x0E;
	}
	/* shangva/demo_va: rom type=device (MUSIC.SYS/DEMO2.SYS) must INIT
	   before the glue shell so INT C8/C3 exist for play/stop. */
	RunDosDevices(ge, setupBudget);
	int hasGlue = 0, hasPcm = 0, hasHashM = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "shell") != 0) continue;
		const char* sn = r->name ? r->name : "";
		while (*sn == ' ' || *sn == '\t' || *sn == '#') sn++;
		if (_strnicmp(sn, "pmd_98", 6) == 0) hasGlue = 1;
		if (_strnicmp(sn, "pmdpcm", 6) == 0) hasPcm = 1;
		if (strchr(r->name, '#'))
			hasHashM = 1;
	}
	const int glueBeforePcm = hasGlue && hasPcm && !hasHashM;
	if (glueBeforePcm) {
		for (int pass = 0; pass < 3; pass++) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "shell") != 0) continue;
				const char* sn = r->name ? r->name : "";
				while (*sn == ' ' || *sn == '\t' || *sn == '#') sn++;
				const int isGlue = (_strnicmp(sn, "pmd_98", 6) == 0);
				const int isPcm = (_strnicmp(sn, "pmdpcm", 6) == 0);
				const int want = isGlue ? 1 : (isPcm ? 2 : 0);
				if (want != pass) continue;
				RunDosCommand(r->name, setupBudget);
			}
		}
	} else {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "shell") != 0) continue;
			RunDosCommand(r->name, setupBudget);
			const char* sn = r->name ? r->name : "";
			while (*sn == ' ' || *sn == '\t' || *sn == '#') sn++;
			if (_strnicmp(sn, "pmd_98", 6) == 0 && (dosStubReady_ || stubState_ == 0x81))
				break;
		}
	}
	dosStubReady_ = (stubState_ == 0x81) ? 1 : dosStubReady_;
	/* PC-88VA DOS overlays (tetrisva/shinrava/famista89): OPN ISR on INT14.
	   Mirror to INT0B when missing so DeliverIrqs can tick the sequencer. */
	if (mem && pc88VaIo_ && !IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)) {
		const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
		const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
		mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
		picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
	}
	/* rtypeva: COM plants OPN on INT0A → far 0C1B (VA ports). MAIN also
	   parks a PC-98-port ISR on INT14 — do NOT prefer that for VA play. */
	static const char* kRtype[] = { "rtype", NULL };
	if (mem && pc88VaIo_ && DosShellStarts(ge, kRtype) && IvtHooked(0x0A, 1)) {
		const unsigned o0a = (unsigned)mem[0x0A * 4] | ((unsigned)mem[0x0A * 4 + 1] << 8);
		const unsigned s0a = (unsigned)mem[0x0A * 4 + 2] | ((unsigned)mem[0x0A * 4 + 3] << 8);
		mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o0a & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o0a >> 8) & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s0a & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s0a >> 8) & 0xff);
		picMask_ = (uint8_t)(picMask_ & ~((1u << 2) | (1u << 3)));
	}
	/* olteus_va: remember MAP.EXE load seg (COM far-table [01C4]) and plant
	   INT08 → near tick trampoline once play is armed. */
	olteusMapSeg_ = 0;
	olteusDataSeg_ = 0;
	olteusTimerOn_ = 0;
	olteusTrampOk_ = 0;
	olteusIrqPulse_ = 0;
	olteusInTick_ = 0;
	olteusTimerResidual_ = 0;
	olteusTickGuard_ = 0;
	static const char* kOlteus[] = { "olteus", NULL };
	if (mem && DosShellStarts(ge, kOlteus)) {
		const uint16_t psp = dos_.PspSeg();
		const unsigned mapSeg = (unsigned)mem[Pc98DosLin(psp, 0x1C4)]
			| ((unsigned)mem[Pc98DosLin(psp, 0x1C5)] << 8);
		if (mapSeg && mapSeg != (unsigned)DOS98_TRAMP_SEG)
			ArmOlteusVaTimer((uint16_t)mapSeg);
		/* Finish MAP handshake before INT18 idle — COM returns early. */
		if (olteusMapSeg_ && olteusDataSeg_) {
			olteusTimerOn_ = 1;
			const uint64_t hsBudget = (uint64_t)cpuHz_ * 2ull;
			const uint64_t hsEnd = cpuCycles_ + hsBudget;
			while (cpuCycles_ < hsEnd) {
				const unsigned base = (unsigned)olteusDataSeg_ << 4;
				const unsigned flag = (unsigned)mem[base + 0xCC4D]
					| ((unsigned)mem[base + 0xCC4D + 1] << 8);
				if (flag >= 0x0011u)
					break;
				PumpCycles(cpuCycles_ + (uint64_t)cpuHz_ / 60ull);
			}
		}
	}
	/* PMD often installs the OPN ISR then leaves master mask FF; if IVT0B is
	   hooked, unmask IRQ3 so OPN timers can run. */
	if (mem && IvtHooked(PC98_OPN_IRQ_VEC, 1))
		picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
	/* usd_98 (ASCII USO): USD hooks only INT F2 (memcpy stub). Play goes
	   through INT F4 which ADVBIOS owns (AH=0 load / AH=1 play / …).
	   Do NOT mirror F2→F4 — that wiped the ADVBIOS API and left keyOn=0.
	   If F4 was never hooked, fall back to F2. INT F3 is an AH-multiplex
	   API (not the OPN timer ISR) — do not plant it on INT0B.
	   ADVBIOS.OVL packs often hang in far486 waiting on IN 60h bit5
	   (never toggles here) after F2/F4/far-table are ready but before
	   INT7F is planted — finish the install from the host. */
	static const char* kUsd[] = { "usd_98", "usd98", NULL };
	if (mem && DosShellStarts(ge, kUsd)) {
		const unsigned f2o = (unsigned)mem[0xF2 * 4] | ((unsigned)mem[0xF2 * 4 + 1] << 8);
		const unsigned f2s = (unsigned)mem[0xF2 * 4 + 2] | ((unsigned)mem[0xF2 * 4 + 3] << 8);
		const unsigned f4o = (unsigned)mem[0xF4 * 4] | ((unsigned)mem[0xF4 * 4 + 1] << 8);
		const unsigned f4s = (unsigned)mem[0xF4 * 4 + 2] | ((unsigned)mem[0xF4 * 4 + 3] << 8);
		const int f4Live = (f4s != 0 && f4s != (unsigned)DOS98_TRAMP_SEG
			&& !(f4s == f2s && f4o == f2o));
		if (!f4Live && f2s != 0 && f2s != (unsigned)DOS98_TRAMP_SEG) {
			mem[0xF4 * 4] = (uint8_t)(f2o & 0xff);
			mem[0xF4 * 4 + 1] = (uint8_t)((f2o >> 8) & 0xff);
			mem[0xF4 * 4 + 2] = (uint8_t)(f2s & 0xff);
			mem[0xF4 * 4 + 3] = (uint8_t)((f2s >> 8) & 0xff);
		}
		const unsigned s7f = (unsigned)mem[0x7F * 4 + 2] | ((unsigned)mem[0x7F * 4 + 3] << 8);
		if (f2s != 0 && f2s != (unsigned)DOS98_TRAMP_SEG
			&& (s7f == 0 || s7f == (unsigned)DOS98_TRAMP_SEG)) {
			const unsigned base = f2s << 4;
			const unsigned off486 = (unsigned)mem[base + 0x486] | ((unsigned)mem[base + 0x487] << 8);
			const unsigned seg486 = (unsigned)mem[base + 0x488] | ((unsigned)mem[base + 0x489] << 8);
			if (seg486 && off486) {
				mem[0x7F * 4 + 0] = 0xC4;
				mem[0x7F * 4 + 1] = 0x01;
				mem[0x7F * 4 + 2] = (uint8_t)(f2s & 0xff);
				mem[0x7F * 4 + 3] = (uint8_t)((f2s >> 8) & 0xff);
				/* Park at USD HLT idle (CS:01BD). */
				if (base + 0x1BF < 0x200000u) {
					mem[base + 0x1BD] = 0xF4;
					mem[base + 0x1BE] = 0xEB;
					mem[base + 0x1BF] = 0xFD;
				}
				np2_set_cs_ip((uint16_t)f2s, 0x01BD);
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				stubState_ = 0x81;
			}
		}
		/* ADVH USD (1585/1599): INT7F idle at HLT @022E after install.
		   Some ADVH.EXE builds use a DS=loadSeg decrypt stub at MZ entry
		   (IP in 0x120..0x200, xor-loop then jmp 0010) that USD's 0395/0402
		   miss when the TC0/EXEPACK signatures differ — F1 stays trampoline
		   while the MZ header + overlay body sit at [0706]/[0706]+10.
		   Finish that entry on the host, then plant INT7F + park. */
		{
			unsigned usdCs = 0;
			const unsigned cur7f = (unsigned)mem[0x7F * 4 + 2]
				| ((unsigned)mem[0x7F * 4 + 3] << 8);
			if (cur7f && cur7f != (unsigned)DOS98_TRAMP_SEG)
				usdCs = cur7f;
			else {
				const unsigned cs = np2_reg_get(NP2_R_CS);
				const unsigned ip = np2_reg_get(NP2_R_IP);
				if (cs > 0x100 && cs < 0xA000) {
					const unsigned b = cs << 4;
					if (b + 0x240 < 0x200000u && mem[b + 0x232] == 0x06
						&& mem[b + 0x233] == 0x1E && mem[b + 0x234] == 0x60)
						usdCs = cs;
					else if ((ip == 0x022E || ip == 0x022F || ip == 0x0230)
						&& b + 0x240 < 0x200000u && mem[b + 0x232] == 0x06)
						usdCs = cs;
				}
			}
			if (usdCs) {
				const unsigned base = usdCs << 4;
				const unsigned live7f = (unsigned)mem[0x7F * 4 + 2]
					| ((unsigned)mem[0x7F * 4 + 3] << 8);
				if (live7f == 0 || live7f == (unsigned)DOS98_TRAMP_SEG) {
					if (base + 0x240 < 0x200000u && mem[base + 0x232] == 0x06
						&& mem[base + 0x233] == 0x1E) {
						mem[0x7F * 4 + 0] = 0x32;
						mem[0x7F * 4 + 1] = 0x02;
						mem[0x7F * 4 + 2] = (uint8_t)(usdCs & 0xff);
						mem[0x7F * 4 + 3] = (uint8_t)((usdCs >> 8) & 0xff);
					}
				}
				if (base + 0x231 < 0x200000u) {
					mem[base + 0x22E] = 0xF4;
					mem[base + 0x22F] = 0xEB;
					mem[base + 0x230] = 0xFD;
					np2_set_cs_ip((uint16_t)usdCs, 0x022E);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				}
				stubState_ = 0x81;
			}
		}
		/* Name-load ADVH: finish dual-seg data / ISR DS consistency and
		   plant INT0B on the OEM music ISR while BootDos still owns the
		   resident image (before TriggerPlay song I/O). */
		{
			const unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
				| ((unsigned)mem[0xF1 * 4 + 3] << 8);
			if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG
				&& AdvhNormalizeNameLoadResident(mem, sF1))
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	/* Crowd CMD/CMDP: OPN probe at CS:2393 programs the chip then clears
	   [CS:1792] on the all-CF=0 path. INT60 play (idx1) spins while
	   [1792] < 1, so song reads succeed but keyOns never start. Later play
	   steps also require [1792]==1 exactly (not 2/3/4 from probe stages).
	   Music ISR is installed on INT14 (same as MADP/N3GOLF); mirror to
	   INT0B so DeliverIrqs can fire OPN timer ticks. */
	static const char* kCmd[] = { "CMD", "CMDP", NULL };
	if (mem && DosShellStarts(ge, kCmd)) {
		const unsigned o60 = (unsigned)mem[0x60 * 4] | ((unsigned)mem[0x60 * 4 + 1] << 8);
		const unsigned s60 = (unsigned)mem[0x60 * 4 + 2] | ((unsigned)mem[0x60 * 4 + 3] << 8);
		if (s60 != 0 && s60 != (unsigned)DOS98_TRAMP_SEG) {
			const unsigned flagPhys = ((unsigned)s60 << 4) + 0x1792u;
			if (flagPhys < 0x200000u)
				mem[flagPhys] = 1;
			unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
			unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
			if (s14 == 0 || s14 == (unsigned)DOS98_TRAMP_SEG) {
				/* Install path was skipped ([1792]==0 at hook time). Locate
				   ISR prologue in the resident CMD image and plant INT14. */
				static const uint8_t kIsr[] = { 0x9C, 0x60, 0x55, 0x1E, 0x06, 0xFA };
				const unsigned base = (unsigned)s60 << 4;
				unsigned found = 0;
				for (unsigned off = 0x100; off + sizeof(kIsr) < 0x8000; off++) {
					const unsigned p = base + off;
					if (p + sizeof(kIsr) > 0x200000u) break;
					int match = 1;
					for (unsigned k = 0; k < sizeof(kIsr); k++) {
						if (mem[p + k] != kIsr[k]) { match = 0; break; }
					}
					if (match) { found = off; break; }
				}
				if (found) {
					o14 = found;
					s14 = s60;
					mem[0x14 * 4] = (uint8_t)(o14 & 0xff);
					mem[0x14 * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
					mem[0x14 * 4 + 2] = (uint8_t)(s14 & 0xff);
					mem[0x14 * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				}
			}
			if (s14 != 0 && s14 != (unsigned)DOS98_TRAMP_SEG) {
				mem[0x0B * 4] = (uint8_t)(o14 & 0xff);
				mem[0x0B * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
				mem[0x0B * 4 + 2] = (uint8_t)(s14 & 0xff);
				mem[0x0B * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			(void)o60;
		}
	}
	/* HuLinks fakecall→music→46: MUSIC.COM parks the OPN ISR on INT14.
	   Mirror to INT0B only — do NOT also plant the same ISR on INT08/PIT.
	   Dual delivery (OPN + PIT) double-ticks the sequencer: 46oku fades out
	   as [0294] hits 0x10 early and mute-alls. Channel freeze was from the
	   cmd2→AH=1 mute path, not from missing PIT. */
	static const char* kStarcmd[] = { "fakecall", "music", "MUSIC", "46", NULL };
	if (mem && DosShellStarts(ge, kStarcmd)) {
		musicComKeepalive_ = 1;
		unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
		unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
		if (s14 != 0 && s14 != (unsigned)DOS98_TRAMP_SEG) {
			mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	/* Arm MPU capture only after shells finish (FMP -m probe OUTs zeros). */
	if (modeMidi_) {
		mpuUart_ = 1;
		midiCapArmed_ = 1;
		/* FMP3 -m / midiout: UART bytes are real MIDI — voice via OPN bridge. */
		wolfBridgeEnable_ = 1;
		WolfBridgeReset();
		MidiCaptureReset();
	}
	return 1;
}

void CHardPc98::PumpCycles(uint64_t endCycle)
{
	CEmuHardPc98SetActive(this);
	while (cpuCycles_ < endCycle) {
		uint8_t* mem = np2_mem();
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);

		/* olteus: after handshake, pulse real IRQ0 → IVT08 trampoline at
		   MAP:FE86 (PUSH DS; DS=CS; CALL 09BC; POP DS; IRET). Soft near-call
		   into 8419 nested badly from INT18 idle / MUSIC loops. */
		if (olteusIrqPulse_ && olteusMapSeg_) {
			olteusIrqPulse_ = 0;
			const unsigned base = (unsigned)olteusMapSeg_ << 4;
			const unsigned dbase = (unsigned)olteusDataSeg_ << 4;
			if (mem && base + 0xFE95u < 0x200000u) {
				/* Keep DS:[5BBA]=0 so 09BC→8419 does not take the early JMP. */
				if (olteusDataSeg_ && dbase + 0x5BBBu < 0x200000u) {
					mem[dbase + 0x5BBA] = 0;
					mem[dbase + 0x5BBB] = 0;
				}
				/* Re-plant trampoline + IVT each pulse — MAP may rewrite IVT08. */
				if (!olteusTrampOk_
					|| mem[0x08 * 4] != 0x86 || mem[0x08 * 4 + 1] != 0xFE
					|| mem[0x08 * 4 + 2] != (uint8_t)(olteusMapSeg_ & 0xff)
					|| mem[0x08 * 4 + 3] != (uint8_t)(olteusMapSeg_ >> 8))
					ArmOlteusVaTimer(olteusMapSeg_);
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt((uint8_t)PC98_TIMER_VEC);
				continue;
			}
		}
		if (DeliverIrqs())
			continue;
		cs = np2_reg_get(NP2_R_CS);
		ip = np2_reg_get(NP2_R_IP);
		mem = np2_mem();
		const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
		if (isDos_ && mem && phys < 0x200000 && mem[phys] == 0xF4) {
			uint8_t vec = 0;
			if (dos_.TrapVector(cs, ip, &vec)) {
				CEmuDos98Result res = dos_.ServiceInt(mem, vec);
				/* olteus MAP music keeps ticking after COM/EXE TSR or "exit";
				   aborting PumpCycles froze host timer assist. */
				if ((res == DOS98_TERMINATED || res == DOS98_RESIDENT)
					&& !olteusMapSeg_)
					return;
				dos_.IretReturn(mem);
				const uint64_t q = 50;
				cpuCycles_ += q;
				TickSide(q);
				continue;
			}
			const uint64_t q = 200;
			cpuCycles_ += q;
			TickSide(q);
			if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
				opnPumpResidual_ += q * (uint64_t)opnHz_;
				uint64_t ot = opnPumpResidual_ / (uint64_t)cpuHz_;
				opnPumpResidual_ %= (uint64_t)cpuHz_;
				if (ot) chip_->AdvanceClocks(ot);
			}
			continue;
		}
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
			opnPumpResidual_ += u * (uint64_t)opnHz_;
			uint64_t ot = opnPumpResidual_ / (uint64_t)cpuHz_;
			opnPumpResidual_ %= (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
	}
}

int CHardPc98::TriggerPlay(unsigned titleCode)
{
	unsigned song = titleCode & 0xff;
	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);

	const uint64_t drainBudget = (uint64_t)cpuHz_ / 2ull;

	if (isDos_) {
		if (dosGe_)
			BindDosTriggerSong(dosGe_, titleCode);
		else {
			extCmd_ = 0;
			extSong_ = (uint16_t)(titleCode & 0xff);
			extParam_ = 0;
			if (dosSong_[0]) {
				dos_.SetHandle(0, dosSong_);
				dos_.SetHandle(5, dosSong_);
				dos_.SetHandle(0x0B, dosSong_);
				if (song < (unsigned)DOS98_HANDLE_MAX)
					dos_.SetHandle((uint16_t)song, dosSong_);
			}
		}
		/* olteus: MAP opens A:\MUSIC#F/P.MUS — poke digit into the template. */
		if (olteusMapSeg_) {
			uint8_t* mem = np2_mem();
			const unsigned base = (unsigned)olteusMapSeg_ << 4;
			const unsigned n = titleCode & 0x0fu;
			const char dig = (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
			const char fp = opnaMode ? 'F' : 'P';
			if (mem) {
				for (unsigned off = 0; off + 11u < 0x20000u; off++) {
					const unsigned p = base + off;
					if (p + 11u >= 0x200000u)
						break;
					if (mem[p] == 'M' && mem[p + 1] == 'U' && mem[p + 2] == 'S'
						&& mem[p + 3] == 'I' && mem[p + 4] == 'C'
						&& (mem[p + 5] == ' ' || (mem[p + 5] >= '0' && mem[p + 5] <= '9')
							|| (mem[p + 5] >= 'A' && mem[p + 5] <= 'E'))
						&& mem[p + 7] == '.') {
						mem[p + 5] = (uint8_t)dig;
						if (mem[p + 6] == 'F' || mem[p + 6] == 'P' || mem[p + 6] == ' ')
							mem[p + 6] = (uint8_t)fp;
					}
				}
			}
		}
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		np2_interrupt((uint8_t)funcVect_);
		PumpCycles(cpuCycles_ + drainBudget);
		/* Some PMD glue paths (love_ed2 `/i`) need a second play poke after the
		   song buffer is resident — matches the itest double-trigger behavior.
		   MSCD_98 cmd0 is not idempotent: it stops, waits 18 retraces, then
		   starts.  A second poke's shorter budget stopped the new song and
		   stranded the CPU halfway through its wait. */
		static const char* kMscdPlay[] = { "MSCDRV", "mscd_98", NULL };
		const int repeatPlay = !(dosGe_ && DosShellStarts(dosGe_, kMscdPlay));
		if (repeatPlay) {
			if (dosGe_)
				BindDosTriggerSong(dosGe_, titleCode);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt((uint8_t)funcVect_);
			PumpCycles(cpuCycles_ + (drainBudget / 2ull));
		}
		/* famistava installs OPN ISR on INT14 during the play far-call — BootDos
		   is too early. Mirror only when INT0B is still vacant. */
		if (pc88VaIo_) {
			uint8_t* mem = np2_mem();
			if (mem && IvtHooked(0x14, 1) && !IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
				const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
				const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			/* rtypeva: start parses channels but leaves the ISR stream ([01C2]) and
			   [000F] idle; mute also clears OPN timer. Arm stream from ch0 + timer. */
			static const char* kRtypePlay[] = { "rtype", NULL };
			if (mem && dosGe_ && DosShellStarts(dosGe_, kRtypePlay)) {
				const uint16_t psp = dos_.PspSeg();
				const unsigned bufSeg = (unsigned)mem[Pc98DosLin(psp, 0x1DE)]
					| ((unsigned)mem[Pc98DosLin(psp, 0x1DF)] << 8);
				if (bufSeg) {
					const unsigned dbase = bufSeg << 4;
					const unsigned ch0 = (unsigned)mem[dbase + 0x48]
						| ((unsigned)mem[dbase + 0x49] << 8);
					if (ch0) {
						if (mem[dbase + 0x0F] == 0)
							mem[dbase + 0x0F] = 1;
						if ((mem[dbase + 0x1C2] | mem[dbase + 0x1C3]) == 0) {
							mem[dbase + 0x1C2] = (uint8_t)(ch0 & 0xff);
							mem[dbase + 0x1C3] = (uint8_t)(ch0 >> 8);
							mem[dbase + 0x1C4] = 1;
						}
					}
					if (chip_) {
						chip_->Write(0, 0x27);
						chip_->Write(1, 0x3F);
						opnTimerCount_++;
					}
				}
			}
		}
		/* usd_98 / ADVBIOS: INT7F does F4 AH=0 (load→55D1) + AH=1 (arm).
		   Timers + music ISR start via F4 AH=0x30, which plants ADVBIOS
		   CS:0690 on INT16. Mirror that ISR to INT0B for OPN timer ticks.
		   ADVH.EXE packs use INT F1 (mode=1) with song words at CS:0712/0716
		   instead of classic 0480/0484 — F4 stays trampoline. */
		if (dosGe_) {
			static const char* kUsdPlay[] = { "usd_98", "usd98", NULL };
			if (DosShellStarts(dosGe_, kUsdPlay)) {
				uint8_t* mem = np2_mem();
				if (mem) {
					unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
						| ((unsigned)mem[0x7F * 4 + 3] << 8);
					const unsigned sF4 = (unsigned)mem[0xF4 * 4 + 2]
						| ((unsigned)mem[0xF4 * 4 + 3] << 8);
					unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
						| ((unsigned)mem[0xF1 * 4 + 3] << 8);
					/* Finish Microsoft PACKED / xor-decrypt ADVH only when F1 is
					   still trampoline after BootDos (watagolf already live). */
					if ((sF1 == 0 || sF1 == (unsigned)DOS98_TRAMP_SEG)
						&& s7f && s7f != (unsigned)DOS98_TRAMP_SEG
						&& dos_.FindFile("ADVH.EXE")) {
						const unsigned base = s7f << 4;
						const CEmuDos98File* advh = dos_.FindFile("ADVH.EXE");
						unsigned alloc = (unsigned)mem[base + 0x706]
							| ((unsigned)mem[base + 0x707] << 8);
						if (!alloc || alloc == (unsigned)DOS98_TRAMP_SEG)
							alloc = (unsigned)mem[base + 0x712]
								| ((unsigned)mem[base + 0x713] << 8);
						if (advh && advh->data && advh->size >= 0x20 && alloc) {
							const unsigned ip = (unsigned)advh->data[0x14]
								| ((unsigned)advh->data[0x15] << 8);
							const unsigned csRel = (unsigned)advh->data[0x16]
								| ((unsigned)advh->data[0x17] << 8);
							const unsigned loadSeg = alloc + 0x10u;
							dos_.LoadOverlay(mem, advh->data, advh->size,
								(uint16_t)loadSeg, (uint16_t)loadSeg);
							memcpy(mem + (alloc << 4), advh->data, 0x20);
							const unsigned entrySeg = loadSeg + csRel;
							if (ip == 0x10u || ip == 0x00u
								|| (ip >= 0x100u && ip < 0x300u)) {
								const unsigned tramp = 0x50000;
								unsigned ti = 0;
								mem[tramp + ti++] = 0x9A;
								mem[tramp + ti++] = (uint8_t)(ip & 0xff);
								mem[tramp + ti++] = (uint8_t)((ip >> 8) & 0xff);
								mem[tramp + ti++] = (uint8_t)(entrySeg & 0xff);
								mem[tramp + ti++] = (uint8_t)((entrySeg >> 8) & 0xff);
								mem[tramp + ti++] = 0xF4;
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_DS, (uint16_t)loadSeg);
								np2_reg_set(NP2_R_ES, (uint16_t)loadSeg);
								np2_reg_set(NP2_R_SS, (uint16_t)s7f);
								np2_reg_set(NP2_R_SP, 0x1700);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								const uint64_t budget = (ip <= 0x20u)
									? ((uint64_t)cpuHz_ * 30ull)
									: ((uint64_t)cpuHz_ * 5ull);
								PumpCycles(cpuCycles_ + budget);
								sF1 = (unsigned)mem[0xF1 * 4 + 2]
									| ((unsigned)mem[0xF1 * 4 + 3] << 8);
								if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG) {
									mem[base + 0x70A] = 1;
									mem[base + 0x70B] = 0;
									const unsigned oF1 = (unsigned)mem[0xF1 * 4]
										| ((unsigned)mem[0xF1 * 4 + 1] << 8);
									mem[base + 0x70C] = (uint8_t)(oF1 & 0xff);
									mem[base + 0x70D] = (uint8_t)((oF1 >> 8) & 0xff);
									mem[base + 0x70E] = (uint8_t)(sF1 & 0xff);
									mem[base + 0x70F] = (uint8_t)((sF1 >> 8) & 0xff);
								}
							}
						}
					}
					const int f4Live = (sF4 && sF4 != (unsigned)DOS98_TRAMP_SEG);
					const int f1Live = (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG);
					const unsigned apiSeg = f4Live ? sF4 : (f1Live ? sF1 : 0);
					const unsigned apiVec = f4Live ? 0xF4u : 0xF1u;
					if (s7f && s7f != (unsigned)DOS98_TRAMP_SEG && apiSeg) {
						const unsigned base = s7f << 4;
						unsigned songLen = (unsigned)mem[base + 0x484]
							| ((unsigned)mem[base + 0x485] << 8);
						const unsigned songLenAdvh = (unsigned)mem[base + 0x712]
							? ((unsigned)mem[base + 0x716] | ((unsigned)mem[base + 0x717] << 8))
							: 0;
						if (songLenAdvh > songLen && songLenAdvh < 0xF000)
							songLen = songLenAdvh;
						/* Song workspace: ADVBIOS F4 AH=0 does mov di,imm16. */
						unsigned workSeg = 0x55D1;
						{
							const unsigned t0 = (unsigned)mem[(apiSeg << 4) + 0x113]
								| ((unsigned)mem[(apiSeg << 4) + 0x114] << 8);
							const unsigned fp = (apiSeg << 4) + t0;
							for (unsigned k = 0; k + 3 < 0x40 && fp + k + 3 < 0x200000u; k++) {
								if (mem[fp + k] == 0xBF) {
									workSeg = (unsigned)mem[fp + k + 1]
										| ((unsigned)mem[fp + k + 2] << 8);
									break;
								}
							}
						}
						if (songLen && workSeg && songLen < 0xF000 && f4Live) {
							/* ADVBIOS: AH=0x30 arms timers+ISR; AH=1 plays. */
							const unsigned tramp = 0x50000;
							unsigned ti = 0;
							mem[tramp + ti++] = 0xB8;
							mem[tramp + ti++] = (uint8_t)(workSeg & 0xff);
							mem[tramp + ti++] = (uint8_t)((workSeg >> 8) & 0xff);
							mem[tramp + ti++] = 0x8E;
							mem[tramp + ti++] = 0xD8;
							mem[tramp + ti++] = 0x31;
							mem[tramp + ti++] = 0xF6;
							mem[tramp + ti++] = 0x31;
							mem[tramp + ti++] = 0xFF;
							mem[tramp + ti++] = 0x31;
							mem[tramp + ti++] = 0xD2;
							mem[tramp + ti++] = 0xB9;
							mem[tramp + ti++] = (uint8_t)(songLen & 0xff);
							mem[tramp + ti++] = (uint8_t)((songLen >> 8) & 0xff);
							mem[tramp + ti++] = 0xB4;
							mem[tramp + ti++] = 0x30;
							mem[tramp + ti++] = 0xCD;
							mem[tramp + ti++] = 0xF4;
							mem[tramp + ti++] = 0xB4;
							mem[tramp + ti++] = 0x01;
							mem[tramp + ti++] = 0xCD;
							mem[tramp + ti++] = 0xF4;
							mem[tramp + ti++] = 0xF4;
							np2_reg_set(NP2_R_CS, 0x5000);
							np2_reg_set(NP2_R_IP, 0);
							np2_reg_set(NP2_R_FLAGS,
								(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
							PumpCycles(cpuCycles_ + (drainBudget / 2ull));
						}
						/* ADVH INT F1: call the driver's published API only.
						   EB 06 = filename open (AL=0 -> INT21 AH=3D);
						   EB 0F = memory load (AL=0 needs DS:0 + CX=len). */
						if (!f4Live && f1Live) {
							unsigned songOff = 0x712, lenOff = 0x716;
							{
								const unsigned i7 = (s7f << 4) + 0x240u;
								if (i7 + 20 < 0x200000u && mem[i7] == 0x2E
									&& mem[i7 + 1] == 0x8E && mem[i7 + 2] == 0x1E)
									songOff = (unsigned)mem[i7 + 3]
										| ((unsigned)mem[i7 + 4] << 8);
								for (unsigned k = 0; k + 4 < 0x30 && i7 + k + 4 < 0x200000u; k++) {
									if (mem[i7 + k] == 0x2E && mem[i7 + k + 1] == 0xA3) {
										lenOff = (unsigned)mem[i7 + k + 2]
											| ((unsigned)mem[i7 + k + 3] << 8);
										break;
									}
								}
							}
							unsigned songSeg = (unsigned)mem[base + songOff]
								| ((unsigned)mem[base + songOff + 1] << 8);
							unsigned advhLen = (unsigned)mem[base + lenOff]
								| ((unsigned)mem[base + lenOff + 1] << 8);
							/* If INT7F left an empty buffer, materialize USO
							   from the DOS file table (real disk contents). */
							if (dosSong_[0]) {
								const CEmuDos98File* sf = dos_.FindFile(dosSong_);
								const int need = (!advhLen || !songSeg
									|| (songSeg << 4) + 4 >= 0x200000u
									|| (mem[songSeg << 4] == 0 && mem[(songSeg << 4) + 1] == 0));
								if (sf && sf->data && sf->size && sf->size < 0xF000u && need) {
									unsigned dest = songSeg;
									if (!dest || dest == (unsigned)DOS98_TRAMP_SEG || dest < 0x1000u)
										dest = 0x2002;
									const unsigned dp = dest << 4;
									if (dp + sf->size < 0x200000u) {
										memcpy(mem + dp, sf->data, sf->size);
										songSeg = dest;
										advhLen = sf->size;
										mem[base + songOff] = (uint8_t)(dest & 0xff);
										mem[base + songOff + 1] = (uint8_t)((dest >> 8) & 0xff);
										mem[base + lenOff] = (uint8_t)(advhLen & 0xff);
										mem[base + lenOff + 1] = (uint8_t)((advhLen >> 8) & 0xff);
									}
								}
							}
							const unsigned ent = (sF1 << 4)
								+ ((unsigned)mem[0xF1 * 4] | ((unsigned)mem[0xF1 * 4 + 1] << 8));
							const int nameLoad = (ent + 10 < 0x200000u
								&& mem[ent] == 0xEB && mem[ent + 1] == 0x06
								&& mem[ent + 2] == 'U');
							const unsigned tramp = 0x50000;
							unsigned ti = 0;
							if (nameLoad && dosSong_[0]) {
								unsigned ns = 0x2002;
								unsigned np = ns << 4;
								unsigned i = 0;
								for (; dosSong_[i] && i < 12; i++)
									mem[np + i] = (uint8_t)dosSong_[i];
								mem[np + i] = 0;
								const unsigned fb = sF1 << 4;
								uint8_t isrSave[0x100];
								int isrSaved = 0;
								if (fb + 0x4AB + 0x100u < 0x200000u
									&& mem[fb + 0x4AB] == 0x50 && mem[fb + 0x4AC] == 0x53
									&& mem[fb + 0x4AD] == 0x51 && mem[fb + 0x4AE] == 0x52) {
									memcpy(isrSave, mem + fb + 0x4AB, 0x100);
									isrSaved = 1;
								}
								/* AL=0 (early CS+0x33 intact), AL=1 bind (BootDos
								   retargeted bind immediates to CS). Restore ISR
								   after bind walks slots through @04AB. */
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = (uint8_t)(ns & 0xff);
								mem[tramp + ti++] = (uint8_t)((ns >> 8) & 0xff);
								mem[tramp + ti++] = 0x8E;
								mem[tramp + ti++] = 0xD8;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xDB;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xD2;
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xF4;
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								PumpCycles(cpuCycles_ + (drainBudget / 4ull));
								ti = 0;
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x01;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xF4;
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								PumpCycles(cpuCycles_ + (drainBudget / 2ull));
								ti = 0;
								if (isrSaved)
									memcpy(mem + fb + 0x4AB, isrSave, 0x100);
								/* Re-enter TriggerPlay once; return immediately so
								   later ISR/timer assists cannot mute the replay. */
								{
									static int s_nameLoadReplay = 0;
									if (!s_nameLoadReplay) {
										s_nameLoadReplay = 1;
										const int ok = TriggerPlay(titleCode);
										s_nameLoadReplay = 0;
										return ok;
									}
								}
							} else if (songSeg && advhLen && advhLen < 0xF000u) {
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = (uint8_t)(songSeg & 0xff);
								mem[tramp + ti++] = (uint8_t)((songSeg >> 8) & 0xff);
								mem[tramp + ti++] = 0x8E;
								mem[tramp + ti++] = 0xD8;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xDB;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xD2;
								mem[tramp + ti++] = 0xB9;
								mem[tramp + ti++] = (uint8_t)(advhLen & 0xff);
								mem[tramp + ti++] = (uint8_t)((advhLen >> 8) & 0xff);
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x01;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xF4;
							}
							if (ti) {
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								PumpCycles(cpuCycles_ + (drainBudget / 2ull));
							}
							/* Memory-load: if AL=0 left the play flag clear
							   (INT7F often never set CX=len into the driver's
							   copy), place the USO into the work buffer and
							   re-issue AL=1. */
							if (!nameLoad && songSeg && advhLen) {
								unsigned advhWork = 0, advhFlag = 0x5000, advhDst = 0;
								const unsigned fb = sF1 << 4;
								for (unsigned off = 0x80; off + 9 < 0x1000u; off++) {
									if (mem[fb + off] == 0xBE && mem[fb + off + 3] == 0x8E
										&& mem[fb + off + 4] == 0xDE
										&& mem[fb + off + 5] == 0x80 && mem[fb + off + 6] == 0x3E
										&& mem[fb + off + 9] == 0x00) {
										advhWork = (unsigned)mem[fb + off + 1]
											| ((unsigned)mem[fb + off + 2] << 8);
										advhFlag = (unsigned)mem[fb + off + 7]
											| ((unsigned)mem[fb + off + 8] << 8);
										break;
									}
								}
								if (!advhWork) {
									for (unsigned off = 0x100; off + 8 < 0x800u; off++) {
										if (mem[fb + off] == 0xBF && mem[fb + off + 3] == 0x8E
											&& mem[fb + off + 4] == 0xC7
											&& mem[fb + off + 5] == 0xBF) {
											advhWork = (unsigned)mem[fb + off + 1]
												| ((unsigned)mem[fb + off + 2] << 8);
											advhDst = (unsigned)mem[fb + off + 6]
												| ((unsigned)mem[fb + off + 7] << 8);
											break;
										}
									}
								}
								const unsigned flagPhys = advhWork
									? ((advhWork << 4) + advhFlag) : 0;
								if (flagPhys && flagPhys < 0x200000u && mem[flagPhys] == 0) {
									const unsigned src = songSeg << 4;
									const unsigned dst = (advhWork << 4) + advhDst;
									if (src + advhLen < 0x200000u
										&& dst + advhLen < 0x200000u) {
										memcpy(mem + dst, mem + src, advhLen);
										mem[flagPhys] = 1;
										ti = 0;
										mem[tramp + ti++] = 0xB8;
										mem[tramp + ti++] = 0x01;
										mem[tramp + ti++] = 0x00;
										mem[tramp + ti++] = 0xCD;
										mem[tramp + ti++] = 0xF1;
										mem[tramp + ti++] = 0xF4;
										np2_reg_set(NP2_R_CS, 0x5000);
										np2_reg_set(NP2_R_IP, 0);
										np2_reg_set(NP2_R_FLAGS,
											(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
										PumpCycles(cpuCycles_ + (drainBudget / 2ull));
									}
								}
							}
						}
						/* Bind OPN IRQ3 (INT 0B) to the driver's real music ISR.
						   nameLoadAdvh: INT F1 entry is EB 06 'U' filename-load ADVH. */
						int nameLoadAdvh = 0;
						int found = 0;
						unsigned isrOff = 0, isrSeg = apiSeg;
						if (f1Live && !f4Live) {
							const unsigned ent = (sF1 << 4)
								+ ((unsigned)mem[0xF1 * 4]
									| ((unsigned)mem[0xF1 * 4 + 1] << 8));
							if (ent + 10 < 0x200000u
								&& mem[ent] == 0xEB && mem[ent + 1] == 0x06
								&& mem[ent + 2] == 'U')
								nameLoadAdvh = 1;
						}
						if (IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
							const unsigned o = (unsigned)mem[PC98_OPN_IRQ_VEC * 4]
								| ((unsigned)mem[PC98_OPN_IRQ_VEC * 4 + 1] << 8);
							const unsigned s = (unsigned)mem[PC98_OPN_IRQ_VEC * 4 + 2]
								| ((unsigned)mem[PC98_OPN_IRQ_VEC * 4 + 3] << 8);
							const unsigned bp = (s << 4) + o;
							if (bp + 8 < 0x200000u && mem[bp] == 0x50 && mem[bp + 1] == 0x53
								&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
								&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
								&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E)
								found = 1;
						}
						if (!found) {
							const unsigned o14 = (unsigned)mem[0x14 * 4]
								| ((unsigned)mem[0x14 * 4 + 1] << 8);
							const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
								| ((unsigned)mem[0x14 * 4 + 3] << 8);
							if (s14 == apiSeg && o14) {
								const unsigned bp = (s14 << 4) + o14;
								if (bp + 8 < 0x200000u && mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = o14;
									isrSeg = s14;
									found = 1;
								}
							}
						}
						if (!found) {
							for (unsigned v = 0; v < 256; v++) {
								const unsigned o = (unsigned)mem[v * 4]
									| ((unsigned)mem[v * 4 + 1] << 8);
								const unsigned s = (unsigned)mem[v * 4 + 2]
									| ((unsigned)mem[v * 4 + 3] << 8);
								if (s != apiSeg) continue;
								const unsigned bp = (s << 4) + o;
								if (bp + 8 < 0x200000u && mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = o;
									isrSeg = s;
									found = 1;
									break;
								}
							}
						}
						if (!found) {
							for (unsigned off = 0x600; off < 0x3000; off++) {
								const unsigned bp = (apiSeg << 4) + off;
								if (bp + 8 >= 0x200000u) break;
								if (mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = off;
									found = 1;
									break;
								}
							}
						}
						if (!found) {
							/* Filename-load ADVH leaves OEM ISR near 04AB. */
							for (unsigned off = 0x400; off < 0x600; off++) {
								const unsigned bp = (apiSeg << 4) + off;
								if (bp + 8 >= 0x200000u) break;
								if (mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = off;
									found = 1;
									break;
								}
							}
						}
						if (found && !nameLoadAdvh) {
							if (isrOff) {
								mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
								mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((isrOff >> 8) & 0xff);
								mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
								mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((isrSeg >> 8) & 0xff);
							}
							picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
							/* Name-load ADVH: AL=1 programs notes but leaves the
							   play/channel BSS thin. Forcing Timer A/B here makes
							   the OEM ISR run immediately and key-off everything
							   (peak→0). Memory-load (EB 0F / watagolf) arms itself. */
							if (chip_) {
								chip_->Write(0, 0x27);
								chip_->Write(1, 0x3F);
								opnTimerCount_++;
							}
						}
					}
				}
			}
		}
		/* FMPP / NLP_HOOT / MAKO_98: INT7F cmd0 loads; play is cmd2 (INT D2 /
		   INT60 / INT40). TriggerPlay only fired cmd0 — re-fire with EXT_CMD=2.
		   HuLinks fakecall/music/46 is different: 46.com cmd2 → INT70 AH=1 which
		   is MUSIC.COM mute-all (keys off + sets each channel [CS:ch+3]=1 so the
		   sequencer early-returns forever). Play-enable is INT70 AH=2.

		   TGLFMP/TGLFMP2 are NOT cmd2-play: their cmd0 already does INT D2 AL=0
		   stop + AH=3F read + AL=1 play. cmd2 is `MOV AX,1009 / INT D2` → FMP3
		   fn09 which sets [29AE]=1 and fade counts to 0x10, arming a fade-out
		   that stops vg2 after the first phrase. Keep them in kGluePlay for IRQ
		   unmask below, but do not re-fire cmd2. */
		{
			static const char* kStarPlay[] = {
				"fakecall", "music", "MUSIC", "46", NULL
			};
			const int starPlay = DosShellStarts(dosGe_, kStarPlay);
			if (starPlay)
				musicComKeepalive_ = 1;
			static const char* kGluePlay[] = {
				/* Hoot/GMPV4 families: INT7F/D2/60 cmd0 loads, cmd2 plays. */
				"FMPP", "FMP", "fmp3", "tglfmp",
				"NLP_HOOT", "nlp_hoot", "NAX", "nax", "NA", "nl", "NL",
				"MAKO_98", "MAKO", "mako", "MAKOP",
				"SDN_98", "SDN", "sdn",
				"FMDRV", "fmdrv", "FMDRV_98",
				"MBMUS", "mbmus", "MBMUSP", "mbmusp",
				"TRPSCHRN", "trpschrn", "TRPSCR98",
				"UFMD", "ufmd", "UFMD_98",
				"MIZ3", "miz3", "MIZ3_98",
				"PLAY5", "play5", "PLAY5_98", "PLAY3",
				"IBGM", "ibgm", "IBGMP",
				"EMD", "emd", "EMD_98", "FMD",
				"EXMUS", "exmus", "MARBLE98",
				"MUSDRV", "musdrv",
				"MDR_98", "mdr_98",
				"BGML_98", "bgml",
				"ARTDI_98", "artdi",
				"SYNTH_98", "synth", "SYNTHIA",
				"ELFMUS98", "elfmus",
				"YOUJU_98", "youju",
				"VALKY_98", "valky",
				"onion_98", "onion",
				"muse_98", "muse",
				"usmd_98", "usmd",
				"mdb_98", "mdb",
				"EMI", "EMIP", "emi",
				"RME", "rme", "RME_98",
				"SSD_98", "ssd_98",
				"ABIKO", "abiko",
				"iris_98", "iris",
				"NTMD", "ntmd",
				"SPLIT", "split",
				"ynsound", "YNSOUND", "yns_98", "YNS_98",
				"mlalf", "MLALF", "mlalf_98", "mlfplay",
				"opndrvx", "OPNDRVX",
				NULL
			};
			static const char* kTglFmp[] = { "tglfmp", "TGLFMP", NULL };
			if (DosShellStarts(dosGe_, kGluePlay)
				&& !DosShellStarts(dosGe_, kTglFmp)) {
				extCmd_ = 2;
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt((uint8_t)funcVect_);
				PumpCycles(cpuCycles_ + (drainBudget / 2ull));
			}
			if (starPlay && IvtHooked(0x70, 1)) {
				/* AH=2: [0290]=1 enables ISR; AL → [0292]/[0293] countdown.
				   When [0294] hits 0x10 the ISR mute-alls and clears [0290].
				   AL=FFh + reset [0294] keeps BGM alive. Beat = OPN INT0B
				   only (no PIT twin). */
				uint8_t* m70 = np2_mem();
				if (m70) {
					const unsigned s70 =
						(unsigned)m70[0x70 * 4 + 2]
						| ((unsigned)m70[0x70 * 4 + 3] << 8);
					const unsigned b70 = s70 << 4;
					if (b70 + 0x295u < 0x200000u)
						m70[b70 + 0x294] = 0;
				}
				np2_reg_set(NP2_R_AX, 0x02FF);
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt(0x70);
				PumpCycles(cpuCycles_ + (drainBudget / 4ull));
				uint8_t* mem = np2_mem();
				if (mem) {
					if (!IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)) {
						const unsigned o14 = (unsigned)mem[0x14 * 4]
							| ((unsigned)mem[0x14 * 4 + 1] << 8);
						const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
							| ((unsigned)mem[0x14 * 4 + 3] << 8);
						mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
					}
					if (IvtHooked(PC98_OPN_IRQ_VEC, 1))
						picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
				}
			} else if (DosShellStarts(dosGe_, kGluePlay)) {
				/* Unmask OPN IRQ for non-star glue drivers. */
				uint8_t* mem = np2_mem();
				static const char* kSlave14[] = {
					"mbmus", "MBMUS", "musdrv", "MUSDRV", "muse", "MUSE", NULL
				};
				const int slave14 = DosShellStarts(dosGe_, kSlave14)
					|| ((ssgPortAJumper_ & 0xC0) == 0xC0);
				if (mem) {
					if (slave14 && IvtHooked(0x14, 1)) {
						/* Keep ISR on INT14; unmask cascade + IRQ12. */
						picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
						slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
					} else if (!IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)) {
						const unsigned o14 = (unsigned)mem[0x14 * 4]
							| ((unsigned)mem[0x14 * 4 + 1] << 8);
						const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
							| ((unsigned)mem[0x14 * 4 + 3] << 8);
						mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
					}
					if (!slave14 && IvtHooked(PC98_OPN_IRQ_VEC, 1))
						picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
				}
			}
		}
		return 1;
	}

	int loaded = 0;
	/* Ys/Ys2 Falcom glue sets dataAddr_ inside cmd0 (HostService 0x10) per
	   bank — do not preload against a stale address from the previous title. */
	if (dataAddr_ > 0 && bootCs_ != 0x0160)
		loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
	if (data2Addr_ > 0)
		LoadSongToAddr(song & 0xff, data2Addr_, file2Size_, 1);

	/* BirdySoft CAL/PAL: driver relocates an OPN ISR but never writes IVT 0x0B.
	   Play (INT60) sets wait-flag [DS:269B]=FF and spins until the ISR clears
	   it — without the vector DeliverIrqs refuses OPN IRQs and play hangs.
	   Install the relocated ISR (FB50… or PUSH…/OUT 0Ah/STI variant) and park
	   IRET on INT08 when the chain target is still null. */
	if (cal98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned o60 = (unsigned)mem[0x60 * 4] | ((unsigned)mem[0x60 * 4 + 1] << 8);
			const unsigned s60 = (unsigned)mem[0x60 * 4 + 2] | ((unsigned)mem[0x60 * 4 + 3] << 8);
			/* Prefer relocated INT60 segment; MF-era __02.DAT also lives at
			   phys 0x1000 before/without a finished INT60 install. */
			unsigned bases[2];
			int nBase = 0;
			if (s60 && IvtHooked(0x60, 0))
				bases[nBase++] = s60 << 4;
			bases[nBase++] = 0x1000u;
			unsigned isr = 0, isrSeg = 0;
			static const uint8_t sigA[] = {
				0xFB, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x1E, 0x06, 0xFC
			};
			static const uint8_t sigB[] = {
				0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x1E, 0x06, 0xE4, 0x0A
			};
			for (int bi = 0; bi < nBase && !isr; bi++) {
				const unsigned base = bases[bi];
				const unsigned span = 0x8000;
				for (unsigned p = base; p + 32 < base + span && !isr; p++) {
					int okA = 1, okB = 1;
					for (unsigned i = 0; i < sizeof(sigA); i++)
						if (mem[p + i] != sigA[i]) { okA = 0; break; }
					for (unsigned i = 0; i < sizeof(sigB); i++)
						if (mem[p + i] != sigB[i]) { okB = 0; break; }
					if (!okA && !okB) continue;
					for (unsigned q = p; q + 3 < p + 0x30; q++) {
						if (mem[q] == 0xBA && mem[q + 1] == 0x88 && mem[q + 2] == 0x01) {
							isr = p - base;
							isrSeg = base >> 4;
							break;
						}
					}
				}
			}
			if (s60 && IvtHooked(0x60, 0)) {
				const unsigned base = s60 << 4;
				/* Fix INT60 near-call table if reloc left sentinel EB00. */
				for (unsigned p = base + (o60 ? o60 : 0x60); p + 8 < base + 0x200; p++) {
					if (mem[p] == 0x83 && mem[p + 1] == 0xE7 && mem[p + 2] == 0x0E
						&& mem[p + 3] == 0xFF && mem[p + 4] == 0x95) {
						const unsigned old = (unsigned)mem[p + 5] | ((unsigned)mem[p + 6] << 8);
						if (old == 0xEB00) {
							const unsigned table = (p + 7) - base;
							mem[p + 5] = (uint8_t)(table & 0xff);
							mem[p + 6] = (uint8_t)(table >> 8);
						}
						break;
					}
				}
			}
			if (isr) {
				if (!IvtHooked(PC98_TIMER_VEC, 0)) {
					mem[0x518] = 0xCF; /* IRET */
					mem[PC98_TIMER_VEC * 4 + 0] = 0x18;
					mem[PC98_TIMER_VEC * 4 + 1] = 0x05;
					mem[PC98_TIMER_VEC * 4 + 2] = 0x00;
					mem[PC98_TIMER_VEC * 4 + 3] = 0x00;
				}
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isr & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isr >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3)); /* IRQ3 OPN */
			}
		}
	}

	/* QueenSoft MADP: INT40 is AL-indexed API; AL=1A plants OPN ISR on
	   INT14 (IVT@0x50) like N3GOLF. Mirror INT14→INT0B + unmask IRQ3. */
	if (madp98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			mem[0x501] = (uint8_t)(mem[0x501] | 0x08);
			const unsigned isrOff = (unsigned)mem[0x14 * 4]
				| ((unsigned)mem[0x14 * 4 + 1] << 8);
			const unsigned isrSeg = (unsigned)mem[0x14 * 4 + 2]
				| ((unsigned)mem[0x14 * 4 + 3] << 8);
			if (isrSeg || isrOff) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
	}

	/* N3GOLF: glue parks the OPN timer ISR on INT 14h (IRQ12 path on the
	   real board) but never writes IVT 0x0B. Our OPN IRQ is delivered as
	   master IRQ3 → INT 0x0B, so copy the INT14 handler there and unmask.
	   Songs live inside fm.bin (filesize=0); play is INT7F cmd0 + EXT_SONG. */
	if (n3golf98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned isrOff = (unsigned)mem[0x14 * 4]
				| ((unsigned)mem[0x14 * 4 + 1] << 8);
			unsigned isrSeg = (unsigned)mem[0x14 * 4 + 2]
				| ((unsigned)mem[0x14 * 4 + 3] << 8);
			int ok = 0;
			if (isrSeg && isrOff) {
				const unsigned phys = (isrSeg << 4) + isrOff;
				if (phys + 6 < 0x200000u
					&& mem[phys] == 0xFA && mem[phys + 1] == 0x1E
					&& mem[phys + 2] == 0x06 && mem[phys + 3] == 0x60)
					ok = 1;
			}
			/* Fallback: scan fm.bin segment 0x0100 for CLI;PUSH DS;PUSH ES;PUSHA. */
			if (!ok) {
				const unsigned base = 0x0100u << 4;
				for (unsigned p = base; p + 32 < base + 0x8000u; p++) {
					if (mem[p] == 0xFA && mem[p + 1] == 0x1E
						&& mem[p + 2] == 0x06 && mem[p + 3] == 0x60
						&& mem[p + 4] == 0x8C && mem[p + 5] == 0xC8) {
						/* Prefer the EOI-bearing ISR (OUT 00h,20h nearby). */
						int eoi = 0;
						for (unsigned q = p; q + 4 < p + 0x40; q++) {
							if (mem[q] == 0xB0 && mem[q + 1] == 0x20
								&& mem[q + 2] == 0xE6 && mem[q + 3] == 0x00) {
								eoi = 1;
								break;
							}
						}
						if (eoi) {
							isrOff = p - base;
							isrSeg = 0x0100;
							ok = 1;
							break;
						}
					}
				}
			}
			if (ok) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3)); /* IRQ3 OPN */
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4)); /* IRQ12 */
			}
		}
		/* Fall through: cmd0 + song in EXT_SONG starts play (no dataaddr). */
	}

	/* Falcom PROG.BIN (Alm/LM): OPN detect success plants the music ISR on
	   INT14/INT15 (not INT0B). Mirror like MADP/N3GOLF so YM timer IRQs run.
	   Detect-fail path uses INT08 — keep PIT alive either way. */
	if (prog98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned isrOff = 0, isrSeg = 0;
			for (int v = 0; v < 2; v++) {
				const unsigned vec = (v == 0) ? 0x14u : 0x15u;
				if (!IvtHooked(vec, 0)) continue;
				isrOff = (unsigned)mem[vec * 4]
					| ((unsigned)mem[vec * 4 + 1] << 8);
				isrSeg = (unsigned)mem[vec * 4 + 2]
					| ((unsigned)mem[vec * 4 + 3] << 8);
				if (isrSeg || isrOff) break;
				isrOff = 0;
				isrSeg = 0;
			}
			if (isrSeg || isrOff) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			if (IvtHooked(PC98_TIMER_VEC, 0)) {
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				if (!pitRunning_) {
					pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
					if (pitReload_ == 0) pitReload_ = 1;
					pitCounter_ = pitReload_;
					pitRunning_ = 1;
				}
			}
			np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
	}

	/* KSK DKS/FQ (dks/duelsc/fq3/fq4): after boot INT69 AH=0A, CS:[tableVar]
	   points at the size-prefixed song bank (BSS past the driver image).
	   Locate the AH=0A prologue (06 1E 60 0E 1F B8 xx xx … A3 table), load
	   the catalog song there, and force cmd1 (AH=0 play). */
	if (dks98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned s69 = (unsigned)mem[0x69 * 4 + 2]
				| ((unsigned)mem[0x69 * 4 + 3] << 8);
			if (s69 && s69 < 0xF000) {
				const unsigned base = s69 << 4;
				unsigned tableVar = 0;
				const unsigned span = 0x1800;
				for (unsigned p = base; p + 24 < base + span && p + 24 < 0x200000u; p++) {
					/* Common: 06 1E 60 0E 1F B8 … 03 C1 89 1E .. A3 table */
					if (mem[p] == 0x06 && mem[p + 1] == 0x1E && mem[p + 2] == 0x60
						&& mem[p + 3] == 0x0E && mem[p + 4] == 0x1F && mem[p + 5] == 0xB8
						&& mem[p + 8] == 0x89 && mem[p + 9] == 0x0E
						&& mem[p + 12] == 0xA3
						&& mem[p + 15] == 0x03 && mem[p + 16] == 0xC1
						&& mem[p + 17] == 0x89 && mem[p + 18] == 0x1E
						&& mem[p + 21] == 0xA3) {
						tableVar = (unsigned)mem[p + 22] | ((unsigned)mem[p + 23] << 8);
						break;
					}
					/* FQ3 BGMDRV: … 03 C1; CS: MOV [seg],BX; CS: MOV [table],AX */
					if (mem[p] == 0x03 && mem[p + 1] == 0xC1
						&& mem[p + 2] == 0x2E && mem[p + 3] == 0x89 && mem[p + 4] == 0x1E
						&& mem[p + 7] == 0x2E && mem[p + 8] == 0xA3) {
						tableVar = (unsigned)mem[p + 9] | ((unsigned)mem[p + 10] << 8);
						break;
					}
				}
				if (tableVar && tableVar + 1u < 0x10000u) {
					const unsigned tableOff = (unsigned)mem[base + tableVar]
						| ((unsigned)mem[base + tableVar + 1] << 8);
					const int dest = (int)(base + tableOff);
					if (dest > 0 && LoadSongToAddr(song & 0xff,
						dest, fileSize_ > 0 ? fileSize_ : 0x1000, 0))
						loaded = 1;
				}
			}
		}
	}

	/* Glodia MDPLAY.BIN / MDRIVE.BIN: ensure INT08 has the driver's PIT ISR.
	   Prefer an EOI-bearing stub that also CALLs (sequencer), not a lone EOI/IRET. */
	if (mdplay98_ && !IvtHooked(PC98_TIMER_VEC, 0)) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned bases[3];
			int nBase = 0;
			bases[nBase++] = 0x1000u << 4;
			/* biblem MDRIVE @0x2B000; INT40 seg is a good hint when hooked. */
			const unsigned s40 = (unsigned)mem[0x40 * 4 + 2]
				| ((unsigned)mem[0x40 * 4 + 3] << 8);
			if (s40 && s40 < 0xF000)
				bases[nBase++] = s40 << 4;
			bases[nBase++] = 0x2B00u << 4;
			unsigned isrOff = 0, isrSeg = 0x10;
			unsigned bestScore = 0;
			for (int bi = 0; bi < nBase; bi++) {
				const unsigned base = bases[bi];
				if (base + 0x100 >= 0x200000u) continue;
				for (unsigned p = base; p + 16 < base + 0x3000u && p + 16 < 0x200000u; p++) {
					if (mem[p] != 0x60 && mem[p] != 0x1E && mem[p] != 0xFC)
						continue;
					int eoi = 0, iret = 0, call = 0;
					for (unsigned q = p; q + 4 < p + 0x60 && q + 4 < 0x200000u; q++) {
						if (mem[q] == 0xE8) call = 1;
						if (mem[q] == 0xB0 && mem[q + 1] == 0x20
							&& mem[q + 2] == 0xE6 && mem[q + 3] == 0x00)
							eoi = 1;
						if (mem[q] == 0xCF) {
							iret = 1;
							break;
						}
					}
					if (!(eoi && iret)) continue;
					const unsigned score = 1u + (call ? 2u : 0u) + (mem[p] == 0x60 ? 1u : 0u);
					if (score > bestScore) {
						bestScore = score;
						isrOff = p - base;
						isrSeg = base >> 4;
						if (score >= 3) break;
					}
				}
				if (bestScore >= 3) break;
			}
			if (bestScore > 0) {
				mem[PC98_TIMER_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_TIMER_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_TIMER_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_TIMER_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				if (!pitRunning_) {
					pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
					if (pitReload_ == 0) pitReload_ = 1;
					pitCounter_ = pitReload_;
					pitRunning_ = 1;
				}
			}
		}
	}

	/* Wolfteam 000_BOOT: glue INT 7Fh cmd1 → INT 4Ah (song in AX). INT 4A
	   calls INT 43h to (re)load BX:0000 from the game filesystem, which wipes
	   our dataaddr preload. Park IRET on INT 43 and issue cmd1 only (cmd0 is
	   stop / AX=FFFFh). */
	if (wolfteam98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			mem[0x520] = 0xCF;
			mem[0x43 * 4 + 0] = 0x20;
			mem[0x43 * 4 + 1] = 0x05;
			mem[0x43 * 4 + 2] = 0x00;
			mem[0x43 * 4 + 3] = 0x00;
			if (dataAddr_ > 0)
				loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
		}
		if (loaded) {
			wolfSyncRun_ = 1; /* ISR delay stubs need not-busy E0D2 */
			/* Boot PIC ICWs often leave IRQ0 masked; music ticks need PIT. */
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
			slavePicMask_ = 0x00;
			if (!pitRunning_) {
				pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
				if (pitReload_ == 0) pitReload_ = 1;
				pitCounter_ = pitReload_;
				pitRunning_ = 1;
				pitIrqPending_ = 0;
				pitResidual_ = 0;
			}
			if (mem) {
				/* INT 4C AH=00 expects DS:SI → MF instrument (byte13=0x28 after
				   ADD SI,8). Skip when no MI (apros) — bogus song-as-MI hangs
				   the bank load and never arms the sequencer. */
				if (wolfMiSeg_ > 0) {
					mem[wolfSongPtr_ + 0] = 0x00;
					mem[wolfSongPtr_ + 1] = 0x00;
					mem[wolfSongPtr_ + 2] = (uint8_t)(wolfMiSeg_ & 0xff);
					mem[wolfSongPtr_ + 3] = (uint8_t)((wolfMiSeg_ >> 8) & 0xff);
					if (dataAddr_ > 0) {
						int n = fileSize_ > 0 ? fileSize_ : 0x2000;
						if (n > 0x2000) n = 0x2000;
						if ((unsigned)wolfSongBuf_ + (unsigned)n <= 0x200000u
							&& dataAddr_ + n <= 0x200000)
							memcpy(mem + wolfSongBuf_, mem + dataAddr_, (size_t)n);
					}
					/* dmdply never installs INT4C — only call when hooked. */
					if (IvtHooked(0x4C, 0)) {
						mem[wolfGateStop_] = 0xFF;
						mem[wolfGatePlay_] = 0x00;
						np2_reg_set(NP2_R_AX, 0x0000);
						np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
						np2_interrupt(0x4C);
						DrainInterrupt(drainBudget / 2);
					}
				}
				/* Re-bind song ptr for ISR. Channel stream offsets from 6692
				   are relative to the dataaddr load (7800:0000), not CS:songBuf —
				   keep DS=dataaddr>>4 so SI like 0603 hits the song. */
				mem[wolfGateStop_] = 0x00;
				mem[wolfGatePlay_] = 0xFF;
				if (dataAddr_ > 0) {
					const uint16_t songSeg = (uint16_t)((unsigned)dataAddr_ >> 4);
					const uint16_t songOff = (uint16_t)((unsigned)dataAddr_ & 0xf);
					mem[wolfSongPtr_ + 0] = (uint8_t)(songOff & 0xff);
					mem[wolfSongPtr_ + 1] = (uint8_t)(songOff >> 8);
					mem[wolfSongPtr_ + 2] = (uint8_t)(songSeg & 0xff);
					mem[wolfSongPtr_ + 3] = (uint8_t)(songSeg >> 8);
				} else {
					mem[wolfSongPtr_ + 0] = (uint8_t)(wolfSongBuf_ & 0xff);
					mem[wolfSongPtr_ + 1] = (uint8_t)(wolfSongBuf_ >> 8);
					mem[wolfSongPtr_ + 2] = 0x00;
					mem[wolfSongPtr_ + 3] = 0x00;
				}
			}
			np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			extCmd_ = 1;
			extSong_ = (uint16_t)(titleCode & 0xffff);
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
			if (mem) {
				mem[wolfGateStop_] = 0x00;
				mem[wolfGatePlay_] = 0xFF;
				if (dataAddr_ > 0) {
					const uint16_t songSeg = (uint16_t)((unsigned)dataAddr_ >> 4);
					const uint16_t songOff = (uint16_t)((unsigned)dataAddr_ & 0xf);
					mem[wolfSongPtr_ + 0] = (uint8_t)(songOff & 0xff);
					mem[wolfSongPtr_ + 1] = (uint8_t)(songOff >> 8);
					mem[wolfSongPtr_ + 2] = (uint8_t)(songSeg & 0xff);
					mem[wolfSongPtr_ + 3] = (uint8_t)(songSeg >> 8);
					int n = fileSize_ > 0 ? fileSize_ : 0x2000;
					if (n > 0x2000) n = 0x2000;
					if ((unsigned)wolfSongBuf_ + (unsigned)n <= 0x200000u
						&& dataAddr_ + n <= 0x200000)
						memcpy(mem + wolfSongBuf_, mem + dataAddr_, (size_t)n);
					mem[wolfTitleWord_ + 0] = (uint8_t)(titleCode & 0xff);
					mem[wolfTitleWord_ + 1] = (uint8_t)((titleCode >> 8) & 0xff);
					/* INT4C epilogue + sequencer arm (see 000_BOOT @~6692):
					   MOV BYTE [fa],FF / MOV WORD [fa+3],1 / MOV BYTE [fa+2],0.
					   dmdply folds play-gate into fa+2 — keep that byte FF. */
					mem[wolfFlagA_] = 0xFF;
					if ((unsigned)wolfFlagA_ + 3u < 0x10000u) {
						const int playIsFa2 = (wolfGatePlay_ == (uint16_t)(wolfFlagA_ + 2));
						mem[wolfFlagA_ + 2] = playIsFa2 ? 0xFF : 0x00;
						mem[wolfFlagA_ + 3] = 0x01;
						mem[wolfFlagA_ + 4] = 0x00;
					}
					mem[wolfGateStop_] = 0x00;
					mem[wolfGatePlay_] = 0xFF;
				}
				const uint16_t f73 = (uint16_t)(wolfGateStop_ + 0x2B);
				const uint16_t f74 = (uint16_t)(wolfGateStop_ + 0x2C);
				if (mem[f73] != 0xFF && mem[f74] == 0xFF)
					mem[f74] = 0x00;
			}
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
			if (IvtHooked(PC98_TIMER_VEC, 0)) {
				for (int kick = 0; kick < 8; kick++) {
					np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					np2_interrupt((uint8_t)PC98_TIMER_VEC);
					DrainInterrupt(drainBudget / 32);
					if (mem)
						mem[wolfGatePlay_] = 0xFF;
				}
			}
			return 1;
		}
	}

	/* KOEI98 glue (funcvect INT 40h): cmd0=play, cmd2=stop. Title high word is
	   the music-bank segment (EXT 0x7E4); low word is song (0x7E2). Song data
	   already resides in packed code ROMs — no dataaddr preload. */
	if (koei98_ || funcVect_ == 0x40) {
		extCmd_ = 0;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		return 1;
	}

	/* Song preload only when catalog dataaddr is set (no CS invent). */
	if (fmd98_ && dataAddr_ > 0 && fileSize_ > 0) {
		if (LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0))
			loaded = 1;
	}

	if (rx98_ && dataAddr_ > 0 && fileSize_ > 0) {
		if (LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0))
			loaded = 1;
	}

	if (prog98_ && dataAddr_ > 0 && fileSize_ > 0) {
		if (LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0))
			loaded = 1;
	}

	/* Beast3 BST3 glue: cmd0 with AH==0 far-calls stop; AH!=0 selects load
	   (then clears AH). Catalog titles are 00xx so default cmd0 never loads. */
	if (bst398_)
		extSong_ = (uint16_t)((song & 0xff) | 0x0100);

	/* DOFMD_98 play path (gated by dofmd_): glue INT 7Fh cmd=1 queries host
	   0x11 for a real-mode song ptr (SI/DS), then INT 45h into MSC/MV22/etc.
	   Boot also needs the INT 14h IRET stub installed in LoadRoms. Default
	   cmd0/cmd1 INT sequence below is sufficient once those are in place. */

	/* NOPNDRV keeps song ptr at DS:19F4 (off) / DS:19F6 (seg) with DS=driver
	   CS (0x1000). AH=3 normally sets these from INT BX/ES, but software-INT
	   nesting made that unreliable — poke the words then INT7F play.
	   Only for actual NOPNDRV.COM loads (MUSIC.SYS / MUSDRV2 differ). */
	if (nopnDrv_ && bootCs_ != 0 && dataAddr_ > 0) {
		uint8_t* mem = np2_mem();
		const uint16_t drvCs = 0x1000;
		const uint32_t ptrOff = ((uint32_t)drvCs << 4) + 0x19F4u;
		const uint32_t ptrSeg = ((uint32_t)drvCs << 4) + 0x19F6u;
		const uint16_t songOff = (uint16_t)(dataAddr_ & 0xf);
		const uint16_t songSeg = (uint16_t)(dataAddr_ >> 4);
		const int songBytes = fileSize_ > 0 ? fileSize_ : 0x1000;
		const int ptrInSong = (int)ptrOff >= dataAddr_
			&& (int)ptrOff + 4 <= dataAddr_ + songBytes;

		if (!ptrInSong && mem && ptrSeg + 2u <= 0x200000u) {
			mem[ptrOff] = (uint8_t)(songOff & 0xff);
			mem[ptrOff + 1] = (uint8_t)(songOff >> 8);
			mem[ptrSeg] = (uint8_t)(songSeg & 0xff);
			mem[ptrSeg + 1] = (uint8_t)(songSeg >> 8);

			np2_set_cs_ip((uint16_t)bootCs_, 0x004D); /* HLT idle */
			np2_set_ss_sp(0x1000, 0xFFFE);
			np2_reg_set(NP2_R_FLAGS, 0x0202);

			/* Also issue AH=3 with IF clear so the driver-side bind matches. */
			np2_reg_set(NP2_R_ES, songSeg);
			np2_reg_set(NP2_R_BX, songOff);
			np2_reg_set(NP2_R_AX, 0x0300);
			np2_reg_set(NP2_R_FLAGS, 0x0002); /* IF clear — no nested IRQs */
			np2_interrupt(0x42);
			DrainInterrupt(drainBudget / 4);

			/* Re-assert ptr in case AH=3 clobbered it with a bad stack read. */
			mem[ptrOff] = (uint8_t)(songOff & 0xff);
			mem[ptrOff + 1] = (uint8_t)(songOff >> 8);
			mem[ptrSeg] = (uint8_t)(songSeg & 0xff);
			mem[ptrSeg + 1] = (uint8_t)(songSeg >> 8);

			np2_reg_set(NP2_R_FLAGS, 0x0202);
			extCmd_ = 0;
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget / 4);
			if (loaded || dataAddr_ == 0) {
				extCmd_ = 1;
				np2_interrupt((uint8_t)funcVect_);
				DrainInterrupt(drainBudget);
			}
			return 1;
		}
	}

	/* Xanadu Scenario II (xana2e @1F000 / PR.NO0 @4000 / PR.NO5 @10000):
	   INT7F reads EXT_SONG (07E2) as the command and EXT_CMD (07E0) as the
	   play selector — ports swapped vs Ys/00BIOS glue. Cmd FD far-calls
	   xana2e → PR.NO5 (INT14/15 + OPN). Play is cmd0 + selector 1 with the
	   song already at dataaddr; only selector 1 takes the INT7E+INT41 path. */
	{
		uint8_t* mem = np2_mem();
		if (mem && bootIp_ == 0x0600 && funcVect_ == 0x7f
			&& mem[0x1F000] == 0xFA && mem[0x1F001] == 0xFC
			&& mem[0x1F002] == 0x1E && mem[0x1F003] == 0x06) {
			if (0xA0000u + 0x3FEEu < 0x200000u)
				mem[0xA0000u + 0x3FEEu] =
					(uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x08);
			if (dataAddr_ > 0)
				loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
			extSong_ = 0x00FD;
			extCmd_ = 0;
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
			extSong_ = 0; /* command = play */
			extCmd_ = 1;  /* selector = INT7E + INT41 AH=2 */
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
			if (!IvtHooked(PC98_OPN_IRQ_VEC, 0)) {
				for (int v = 0; v < 2; v++) {
					const unsigned vec = (v == 0) ? 0x14u : 0x15u;
					if (!IvtHooked(vec, 0))
						continue;
					const unsigned off = (unsigned)mem[vec * 4]
						| ((unsigned)mem[vec * 4 + 1] << 8);
					const unsigned seg = (unsigned)mem[vec * 4 + 2]
						| ((unsigned)mem[vec * 4 + 3] << 8);
					const unsigned phys = (seg << 4) + off;
					if (phys >= 0x200000u || mem[phys] == 0xCF)
						continue;
					mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(off & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(off >> 8);
					mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(seg & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(seg >> 8);
					picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
					break;
				}
			} else {
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			/* Re-park on the glue INT18 idle so DrainInterrupt/Render stay sane. */
			np2_set_cs_ip(0x0000, 0x063C);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			(void)loaded;
			return 1;
		}
	}

	extCmd_ = 0;
	np2_interrupt((uint8_t)funcVect_);
	DrainInterrupt(drainBudget);
	/* After cmd0, Falcom glue may HostService(0x10) a song dest — load then
	   cmd1. Catalog dataaddr alone is also enough (no CS invent). */
	if ((bootCs_ == 0x0160 || rx98_ || fmd98_ || prog98_)
		&& dataAddr_ > 0 && fileSize_ > 0)
		loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
	if (loaded || lastSongLoadOk_) {
		extCmd_ = 1;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		/* After cmd1, some glues (Ys MANPRG, Beast3, Wolf SS) leave the YM
		   ISR on INT14/15 only. Mirror to INT0B for DeliverIrqs.
		   Skip lone-IRET stubs (DOFMD/BRANM park serial INT14 at 0000:0500
		   — copying that onto INT0B wipes MSC's OPN ISR). */
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned isrOff = 0, isrSeg = 0;
			for (int v = 0; v < 2; v++) {
				const unsigned vec = (v == 0) ? 0x14u : 0x15u;
				if (!IvtHooked(vec, 0)) continue;
				const unsigned off = (unsigned)mem[vec * 4]
					| ((unsigned)mem[vec * 4 + 1] << 8);
				const unsigned seg = (unsigned)mem[vec * 4 + 2]
					| ((unsigned)mem[vec * 4 + 3] << 8);
				const unsigned phys = (seg << 4) + off;
				if (phys >= 0x200000u) continue;
				/* DOFMD serial stub: single IRET (and our 0x500 park). */
				if (mem[phys] == 0xCF) continue;
				if (seg == 0 && off == 0x500) continue;
				isrOff = off;
				isrSeg = seg;
				break;
			}
			if (isrSeg || isrOff) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
			}
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
	}
	/* SORC98 glue only (boot CS=0, song @3000/VA@11800, wstimer): INT 7Fh
	   maps cmd0→INT D2 AL=3 (start) and cmd1→AL=0 (channel init). Default
	   cmd0-then-cmd1 leaves start cleared — re-issue cmd0 so play sticks.
	   Do NOT force-clear [085A].7 (old TickSide hammer): that made CALL 1CEE
	   run every quantum and stuck SSG3 R0A at 0x0F. Native BIOS keeps .7 set
	   and advances music on OPN Timer B (hoot-correct SSG gating).
	   Do NOT apply to other wstimer games (Ys CS=0160 etc.). */
	if (sorcGlue_) {
		extCmd_ = 0;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget / 2);
		/* Do NOT clear [085A] bit7 here or in TickSide. BIOS uses bit7 to
		   gate INT08→CALL 1CEE; forcing it clear made FM advance but stuck
		   SSG3 volume at 0x0F (hoot-correct gating needs the native skip).
		   Music continues on OPN Timer B while [085A].7 stays set. */
		/* Ensure PIT ticks remain available if the boot left IRQ0 masked. */
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		if (!pitRunning_) {
			pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
			if (pitReload_ == 0) pitReload_ = 1;
			pitCounter_ = pitReload_;
			pitRunning_ = 1;
			pitIrqPending_ = 0;
			pitResidual_ = 0;
		}
	}
	return 1;
}

void CHardPc98::DrainInterrupt(uint64_t budgetCycles)
{
	/* Step until we return near the boot HLT idle (CS==bootCs, IP in F4 patch)
	   or consume budget. Also keep OPN/PIT ticking so nested timer IRQs work. */
	const uint16_t idleCs = (uint16_t)((bootCs_ != 0 || bootIp_ != 0) ? bootCs_ : 0x0060);
	uint64_t start = cpuCycles_;
	uint64_t opnRes = 0;
	while (cpuCycles_ - start < budgetCycles) {
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		uint8_t* mem = np2_mem();
		if (cs == idleCs && mem) {
			uint32_t phys = ((uint32_t)cs << 4) + ip;
			if (phys < 0x200000 && mem[phys] == 0xF4) /* HLT */
				break;
		}
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
			opnRes += u * (uint64_t)opnHz_;
			uint64_t ot = opnRes / (uint64_t)cpuHz_;
			opnRes %= (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
		DeliverIrqs();
	}
}

int CHardPc98::TriggerStop()
{
	extCmd_ = 2;
	np2_interrupt((uint8_t)funcVect_);
	return 1;
}

void CEmuHardPc98SetActive(CHardPc98* hw)
{
	if (!hw) {
		if (g_pc98Active) {
			hootrip_out8 = NULL;
			hootrip_inp8 = NULL;
			g_pc98Active = NULL;
		}
		return;
	}
	g_pc98Active = hw;
	hootrip_out8 = Pc98Out8;
	hootrip_inp8 = Pc98In8;
}

CHardPc98* CEmuHardPc98GetActive()
{
	return g_pc98Active;
}
