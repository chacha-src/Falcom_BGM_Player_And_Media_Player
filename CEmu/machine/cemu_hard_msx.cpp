#include "StdAfx.h"
#include "cemu_hard_msx.h"
#include "../cemu_mgr.h"
#include "../cemu_zipfs.h"
#include "../chip/cemu_chip_ay.h"
#include "../chip/cemu_chip_scc.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../chip/cemu_chip_opl.h"
#include "../fmmon/fmmon_shadow.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include "../s98/device/emu2413/emu2413.h"
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

enum {
	MSX_CPU_HZ = 3579545,
	MSX_AY_HZ = 3579545 / 2,
	MSX_OPLL_HZ = 3579545
};

/* hoot kss.cpp IPL。ただし $0020-$0037 は $FF ではなく RET: $FF 上の RST 20/28/30 が RST 38 へチェインし音楽 ISR をネストした（replcart 無音）。 */
static const uint8_t kKssIpl[] = {
	0xd7,0xd3,0xa0,0xf5,0x7b,0xd3,0xa1,0xf1,0xc9,0xd3,0xa0,0xdb,0xa2,0xc9,0xff,0xff,
	0xed,0x56,0x31,0x80,0xf3,0xf3,0xdb,0x00,0xcd,0x00,0x00,0xfb,0xdb,0x01,0x18,0xfb,
	0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,
	0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xc9,0xf3,0xcd,0x00,0x00,0xfb,0xc9,
};

/* Rd16 の実装 */
static uint16_t Rd16(const uint8_t* p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* Wr16 の実装 */
static void Wr16(uint8_t* p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)(v >> 8);
}

/* KSS/カート SCC マッパ値: $3F（page2）、$80、$BF。$FF/$7F は Falcom 停止フラグ兼
   Sorcerian プレーヤ先頭（LDIR $2000→$B000）。そこを 0x3F 扱いすると sccMapped が立ち、
   続く $B800-B8BF がチップへ奪われ Bitbuster が 4 キードローンになる。 */
static int MsxIsSccMapperData(uint8_t data)
{
	if (data == 0x80)
		return 1;
	if ((data & 0x3fu) == 0x3fu && (data & 0x40u) == 0)
		return 1;
	return 0;
}

/* 最初の有ピッチノートまでのフレーム。0xFFFF = なし。休符／ノート前に F6 が出ると *f6Before を立てる — その経路は $0416 で停滞し得る。 */
static unsigned NukeninRestUntilNote(const uint8_t* p, unsigned n, int* f6Before)
{
	static const uint8_t kDur[16] = {
		0x60, 0x48, 0x30, 0x24, 0x18, 0x12, 0x0c, 0x09,
		0x06, 0x03, 0x03, 0x03, 0x10, 0x08, 0x04, 0x02
	};
	static const uint8_t kArg[16] = {
		1, 1, 1, 0, 0, 0, 1, 0, 3, 2, 1, 1, 1, 0, 0, 0
	};
	unsigned i = 0, rest = 0, steps = 0;
	if (f6Before) *f6Before = 0;
	while (i < n && steps++ < 80u) {
		const uint8_t b = p[i++];
		const unsigned hi = (unsigned)b >> 4, lo = (unsigned)b & 15u;
		if (hi < 0x0Du) {
			if (hi == 0x0Cu)
				rest += kDur[lo];
			else
				return rest;
		} else if (hi == 0x0Fu) {
			const unsigned a = kArg[lo];
			if (i + a > n)
				break;
			if (lo == 6u && rest == 0u && f6Before)
				*f6Before = 1;
			i += a;
			if (lo == 0x0Eu)
				break;
		}
	}
	return 0xFFFFu;
}

/* hoot ds4.cpp IPL — IM2、バンク 2/3 マップ、CALL $48F2、play/skip を poll */
static const uint8_t kDs4Ipl[] = {
	0xed,0x56,0x31,0x80,0xf3,0x3e,0x02,0x32,0x00,0x70,0x3c,0x32,0x00,0x78,0xcd,0xf2,
	0x48,0xdb,0x00,0xb7,0x20,0x04,0xdb,0x02,0x18,0xf7,0xf3,0x3e,0x02,0x32,0x00,0x70,
	0x3c,0x32,0x00,0x78,0xdb,0x01,0x32,0x94,0xc0,0xcd,0x9d,0x72,0xfb,0x18,0xe2,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xf3,0xf5,0xc5,0xd5,0xe5,0xdd,0xe5,0xfd,
	0xe5,0x3e,0x02,0x32,0x00,0x70,0x3c,0x32,0x00,0x78,0xcd,0x36,0x72,0xfd,0xe1,0xdd,
	0xe1,0xe1,0xd1,0xc1,0xf1,0xfb,0xc9,
};

/* BIOS／糊をメモリへ植える */
static void PlantPsgTrampoline(uint8_t* mem)
{
	if (!mem) return;
	if (mem[0x0090] == 0x00)
		mem[0x0090] = 0xC9; /* GICINI 入口 */
	mem[0x0093] = 0xC3; mem[0x0094] = 0xC0; mem[0x0095] = 0x00; /* 命令 JP 00C0 */
	mem[0x0096] = 0xC3; mem[0x0097] = 0xD0; mem[0x0098] = 0x00; /* 命令 JP 00D0 */
	/* BIOS WRTPSG は A をレジスタとして保つ。diskng Bosconian ISR は LD E,(HL); CALL $0093; INC L; INC A; CP $0D — A を壊すとカウントが $0D に届かない（ayW 数十万、IRQ 1）。 */
	mem[0x00C0] = 0xD3; mem[0x00C1] = 0xA0; /* 命令 OUT (A0),A */
	mem[0x00C2] = 0xF5;                     /* 命令 PUSH AF */
	mem[0x00C3] = 0x7B;                     /* 命令 LD A,E */
	mem[0x00C4] = 0xD3; mem[0x00C5] = 0xA1; /* 命令 OUT (A1),A */
	mem[0x00C6] = 0xF1;                     /* 命令 POP AF */
	mem[0x00C7] = 0xC9;                     /* RET 命令 */
	mem[0x00D0] = 0xD3; mem[0x00D1] = 0xA0;
	mem[0x00D2] = 0xDB; mem[0x00D3] = 0xA2;
	mem[0x00D4] = 0xC9;
}

/* PATCH2 @400 CALL $9003／LD HL,$A000（再生）。$4B6E は NOP 済みのことがある */
static int IsLaplacePsgPatch(const uint8_t* mem)
{
	return mem
		&& mem[0x046F] == 0xCD && mem[0x0470] == 0x03 && mem[0x0471] == 0x90
		&& mem[0x0431] == 0x21 && mem[0x0432] == 0x00 && mem[0x0433] == 0xA0;
}

/* $7F00 POP IX のあと LD ($BC33),IX。Play CALL $4B6E は HL=0 で着地し得る（待ちループ $04EC がまだ空）。チャネル 0 のトラックワードが $0000 になり $7CF1 が P1_ @A000 ではなく BIOS を取る。そのオーバーレイを落とし（スタック維持のため POP HL）、stub ワードを $A000 のまま — hoot はそこに 1 ファイルを載せる。ADD HL,DE は既に NOP。 */
static void PlantLaplacePsgPlay(uint8_t* mem)
{
	if (!mem || !IsLaplacePsgPatch(mem))
		return;
	if (mem[0x0434] == 0x19)
		mem[0x0434] = 0x00; /* ADD HL,DE を NOP */
	if (mem[0x6E61] == 0x38 && mem[0x6E62] == 0xF7) {
		mem[0x6E61] = 0x00;
		mem[0x6E62] = 0x00;
	}
	if (mem[0x7F00] == 0xE5 && mem[0x7F2A] == 0xCD && mem[0x7F2B] == 0x30) {
		/* POP HL（PUSH HL の釣り合い）。IX から $BC33 をオーバーレイしない */
		mem[0x7F0C] = 0xE1;
		memset(mem + 0x7F0D, 0x00, (size_t)(0x7F2A - 0x7F0D));
		uint16_t p = 0xA000;
		uint16_t ptrs[4];
		int i;
		if (mem[0xA000] == 0xFD) {
			for (i = 0; i < 4; i++) {
				ptrs[i] = p;
				p = (uint16_t)(p + Rd16(mem + p + 4));
			}
		} else {
			for (i = 0; i < 4; i++)
				ptrs[i] = 0xA000;
		}
		for (i = 0; i < 4; i++)
			Wr16(mem + 0x7F31 + i * 2, ptrs[i]);
		/* $7F00 が走っていない（待ちループは $7043）。PSG ワークページに種をまき、H.TIMI $77C6 が CALL $4B6E 無しで P1_ を取れるようにする */
		memcpy(mem + 0xBC00, mem + 0x7FA2, 0x20);
		for (i = 0; i < 4; i++) {
			uint8_t* ix = mem + 0xBC40 + i * 0x30;
			memcpy(ix, mem + 0x7E60, 0x30);
			ix[0] = ix[2] = ix[4] = (uint8_t)(ptrs[i] & 0xff);
			ix[1] = ix[3] = ix[5] = (uint8_t)(ptrs[i] >> 8);
			ix[0x26] = 0x4E;
		}
	}
	/* STOP しない（$7EBC が IX+0x0C をセットし mute）し PLAY もしない（$4B6E） */
	if (mem[0x0419] == 0xCD && mem[0x041A] == 0x4B && mem[0x041B] == 0x04)
		memset(mem + 0x0419, 0x00, 3);
	if (mem[0x0436] == 0xCD && mem[0x0437] == 0x6E && mem[0x0438] == 0x4B)
		memset(mem + 0x0436, 0x00, 3);
}

/* archive="game,fmpac_msx": FMPAC.ROM は同伴 zip にある */
static uint16_t CEmuMsxKoeiMmlHl(const uint8_t* mem, unsigned song)
{
	if (!mem || mem[0x8001] != 0x3D || mem[0x8002] != 0xD3)
		return 0;
	unsigned found = 0;
	for (unsigned a = 0x8000; a + 3u < 0xA800u; a += 0x80u) {
		if (mem[a + 1] != 0x3D || mem[a + 2] != 0xD3)
			continue;
		if (found == song)
			return (uint16_t)a;
		found++;
	}
	return 0;
}

/* genghis PATCH `IN A,(4); LD L,A; IN A,(5); LD H,A` のあと LDIR → $D300。タイトルコードは中間バイトに MML 番地をパック（0x00840001 → $8400、0x01990010 → $9900）。$80 刻みスキャンは 4 バイト開口だけ命中。3 バイトゲーム内コードは H=top L=song（$8401）を給電していた。 */
static uint16_t CEmuMsxKoeiMmlAddr(const uint8_t* mem, unsigned titleCode, unsigned low)
{
	uint16_t hl = (uint16_t)((titleCode >> 8) & 0xFFFFu);
	if (hl >= 0x8000u && hl < 0xC000u)
		return hl;
	hl = CEmuMsxKoeiMmlHl(mem, low);
	if (hl)
		return hl;
	return (uint16_t)((titleCode >> 16) + 0x8000u);
}

/* CEmuMsxMergeCompanions の実装 */
static void CEmuMsxMergeCompanions(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge || !ge->archive[0] || !strchr(ge->archive, ','))
		return;
	CEmuMgr* mgr = CEmuMgrGet();
	if (!mgr || !mgr->dataRoot[0]) return;

	char buf[CEMU_ARCHIVE_NAME];
	strncpy_s(buf, ge->archive, _TRUNCATE);
	char* ctx = NULL;
	char* tok = strtok_s(buf, ",", &ctx);
	int first = 1;
	while (tok) {
		while (*tok == ' ' || *tok == '\t') tok++;
		char* end = tok + strlen(tok);
		while (end > tok && (end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;
		if (!first && tok[0]) {
			wchar_t path[MAX_PATH];
			const char* dir = ge->dataDir[0] ? ge->dataDir : "msx";
			_snwprintf_s(path, _TRUNCATE, L"%s\\%hs\\%hs.zip", mgr->dataRoot, dir, tok);
			if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
				_snwprintf_s(path, _TRUNCATE, L"%s\\msx\\%hs.zip", mgr->dataRoot, tok);
			if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
				CEmuZipFsMergeZip(fs, path);
		}
		first = 0;
		tok = strtok_s(NULL, ",", &ctx);
	}
}

/* FindMsxCodeRom の実装 */
static const unsigned char* FindMsxCodeRom(CEmuZipFs* fs, const char* name, unsigned* sz)
{
	if (sz) *sz = 0;
	if (!fs || !name || !name[0]) return NULL;
	const unsigned char* data = CEmuZipFsFind(fs, name, sz);
	if (data && sz && *sz) return data;
	/* 欠 .kss はカタログ穴であり DRIVER.BIN エイリアスではない。puyo_msx kss xml は zip に無いファイルを名指す。DRIVER @0 マップはネイティブ @0100/@2000/@6000 レイアウトを飛ばした。 */
	{
		const size_t nl = strlen(name);
		if (nl >= 4 && _stricmp(name + nl - 4, ".kss") == 0)
			return NULL;
	}
	/* dssp1 は ran/BSRAND.OBJ を列挙するが zip は ran/DRIVER.BIN を載せる */
	const char* slash = strrchr(name, '/');
	if (!slash) slash = strrchr(name, '\\');
	if (!slash) {
		data = CEmuZipFsFind(fs, "DRIVER.BIN", sz);
		if (data && sz && *sz) return data;
		return NULL;
	}
	char alt[CEMU_ROM_NAME];
	const int n = (int)(slash - name + 1);
	if (n <= 0 || n >= (int)sizeof(alt) - 12) return NULL;
	memcpy(alt, name, (size_t)n);
	strcpy_s(alt + n, sizeof(alt) - (size_t)n, "DRIVER.BIN");
	data = CEmuZipFsFind(fs, alt, sz);
	if (data && sz && *sz) return data;
	strcpy_s(alt + n, sizeof(alt) - (size_t)n, "DRIVER");
	return CEmuZipFsFind(fs, alt, sz);
}

/* IsMsxPlatform の実装 */
static int IsMsxPlatform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "msx") == 0) return 1;
	if (_stricmp(ge->dataDir, "msx") == 0) return 1;
	if (_stricmp(ge->subtype, "kss") == 0 || _stricmp(ge->subtype, "opll") == 0) return 1;
	if (_stricmp(ge->subtype, "generic") == 0 && _stricmp(ge->platform, "msx") == 0) return 1;
	if (_stricmp(ge->subtype, "ds4") == 0 || _stricmp(ge->subtype, "ascii16") == 0) return 1;
	if (_stricmp(ge->subtype, "dq1") == 0 || _stricmp(ge->subtype, "dq2") == 0) return 1;
	return 0;
}

/* カタログ／オプションを解釈する */
int CHardMsx::ParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal)
{
	if (!ge || !name) return defVal;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) != 0) continue;
		const char* s = ge->opt[i].value;
		if (!s || !s[0]) return defVal;
		return (int)strtoul(s, NULL, 0);
	}
	return defVal;
}

CHardMsx::CHardMsx()
	: cpuHz_(MSX_CPU_HZ)
	, ayHz_(MSX_AY_HZ)
	, opllHz_(MSX_OPLL_HZ)
	, chips_(0)
	, playing_(0)
	, bank_(NULL)
	, bankBytes_(0)
	, cpu_(NULL)
	, chipAy_(NULL)
	, chipScc_(NULL)
	, chipSng_(NULL)
	, chipOpl_(NULL)
	, chipOpll_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, idle_(0)
	, loadAdr_(0)
	, loadSize_(0)
	, initAdr_(0)
	, intAdr_(0)
	, bankOfs_(0)
	, bankNum_(0)
	, bank8k_(0)
	, sccEnable_(0)
	, sccMapped_(0)
	, sccAccessed_(0)
	, ayWriteCount_(0)
	, opllWriteCount_(0)
	, opllLatch_(0)
	, genericMode_(0)
	, dqMode_(0)
	, initPc_(0x400)
	, mdataAddr_(0xA400)
	, mdataSize_(0x800)
	, titleCode_(0)
	, ge_(NULL)
	, playCmdPending_(0)
	, mapper_(MAP_NONE)
	, cart_(NULL)
	, cartBytes_(0)
	, ttlPrgBytes_(0)
	, ttlPrgAddr_(0)
{
	ascii16Bank_[0] = ascii16Bank_[1] = 0;
	ascii8Bank_[0] = ascii8Bank_[1] = ascii8Bank_[2] = ascii8Bank_[3] = 0;
	ds4Bank_[0] = ds4Bank_[1] = 0;
	hardKind = KIND_MSX;
	memset(mem_, 0, sizeof(mem_));
	memset(rom_, 0, sizeof(rom_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
	memset(ttlPrg_, 0, sizeof(ttlPrg_));
	memset(bankShadow_, 0, sizeof(bankShadow_));
	bank8kRam_[0] = bank8kRam_[1] = 0;
}

CHardMsx::~CHardMsx()
{
	Shutdown();
}

/* チップと CPU を生成する */
int CHardMsx::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsMsxPlatform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = MSX_CPU_HZ;
	ayHz_ = MSX_AY_HZ;
	opllHz_ = MSX_OPLL_HZ;
	chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
	chipScc_ = CEmuChipSccCreate((uint32_t)MSX_CPU_HZ, sampleRate_);
	cpu_ = new Ay_Cpu();
	return (cpu_ && chipAy_) ? 1 : 0;
}

/* PCM／コードバンク */
void CHardMsx::FreeBanks()
{
	for (int i = 0; i < BGM_BANKS; i++) {
		if (bgmBank_[i]) {
			free(bgmBank_[i]);
			bgmBank_[i] = NULL;
		}
		bgmBankSize_[i] = 0;
		bgmPresent_[i] = 0;
	}
}

/* チップ／CPU／ROM を破棄する */
void CHardMsx::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	FreeBanks();
	if (bank_) { free(bank_); bank_ = NULL; bankBytes_ = 0; }
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chipAy_) { CEmuChipAyDestroy(chipAy_); chipAy_ = NULL; }
	if (chipScc_) { CEmuChipSccDestroy(chipScc_); chipScc_ = NULL; }
	if (chipSng_) { CEmuChipSn76489Destroy(chipSng_); chipSng_ = NULL; }
	if (chipOpl_) { CEmuChipYm3812Destroy(chipOpl_); chipOpl_ = NULL; }
	if (chipOpll_) {
		OPLL_delete((OPLL*)chipOpll_);
		chipOpll_ = NULL;
	}
	if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
}

/* CHardMsx::AyWrites の実装 */
unsigned CHardMsx::AyWrites() const
{
	return chipAy_ ? CEmuChipAyWriteCount(chipAy_) : ayWriteCount_;
}

/* CHardMsx::OpllWrites の実装 */
unsigned CHardMsx::OpllWrites() const
{
	return opllWriteCount_;
}

/* CHardMsx::EnsureOpll の実装 */
void CHardMsx::EnsureOpll(int force)
{
	if (chipOpll_) return;
	if (!force && !(chips_ & CHIP_FMPAC)) return;
	OPLL* o = OPLL_new((uint32_t)opllHz_, (uint32_t)sampleRate_);
	if (o) {
		OPLL_set_quality(o, 1);
		OPLL_reset_patch(o, 0);
		chipOpll_ = (void*)o;
		memset(opllRegs_, 0, sizeof(opllRegs_));
	}
}

/* CHardMsx::EnsureSng の実装 */
void CHardMsx::EnsureSng()
{
	if (chipSng_) return;
	chipSng_ = CEmuChipSn76489Create((uint32_t)MSX_CPU_HZ, sampleRate_);
}

/* CHardMsx::EnsureMsxAudio の実装 */
void CHardMsx::EnsureMsxAudio()
{
	if (chipOpl_) return;
	if (!(chips_ & CHIP_MSXAUDIO)) return;
	chipOpl_ = CEmuChipYm3812Create((uint32_t)MSX_OPLL_HZ, sampleRate_);
}

/* BIOS／糊をメモリへ植える */
void CHardMsx::PlantBiosStubs()
{
	/* 名前付き MSX BIOS 入口のみ — 0000-03FF の一括 RET 埋めは undead の OPLLDRV @0100 を壊した。各スロットはパッチが init 中に上書きし得る 3 バイト JP/RET。 */
	static const uint16_t kRet[] = {
		0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030,
		0x0024, /* ENASLT 入口 */
		0x001C, /* CALSLT 入口 */
		0x0041, 0x0044, 0x0047, 0x004A, 0x004D,
		0x0050, 0x0053, 0x0056, 0x0059, 0x005C,
		0x005F, 0x0062, 0x0066, 0x0069, 0x006C,
		0x0090, /* GICINI — genghis/saziri/tantexr が CALL $0090 */
		0x0099, 0x009C, 0x009F, 0x00A2, 0x00A5,
		0x00A8, 0x00AB, 0x00AE, 0x00B1, 0x00B4,
		0x00D5  /* herzog ISR は RDPSG RET @00D4 のあと CALL $00D5。NOP スライドで $00E0 トランポリンへ入り vblank をネストした。 */
		/* $0100+ を植えない — Compile/Nichibutsu DRIVER.BIN は @0100 に載る（dsdx1/seiha）。RSLREG $0138 の RET がそれらのイメージを壊した。 */
	};
	for (unsigned i = 0; i < sizeof(kRet) / sizeof(kRet[0]); i++) {
		const uint16_t a = kRet[i];
		if (mem_[a] == 0x00)
			mem_[a] = 0xC9;
	}
	/* RDSLT @000C: HL からバイト、A のスロットは無視。本体 @00F0 は DRIVER.BIN @0100 より下。Slot0-page1=$FF（と偶数 $4018-$401F）は gokudo を PARTIAL→DEAD にした — $4000 の TITLE.COM が RDSLT を使う。 */
	if (mem_[0x000C] == 0x00) {
		mem_[0x000C] = 0xC3; mem_[0x000D] = 0xF0; mem_[0x000E] = 0x00;
		mem_[0x00F0] = 0x7E; /* 命令 LD A,(HL) */
		mem_[0x00F1] = 0xC9;
	}
	/* WRSLT @0014: (HL)=E 書き */
	if (mem_[0x0014] == 0x00) {
		mem_[0x0014] = 0xC3; mem_[0x0015] = 0xF2; mem_[0x0016] = 0x00;
		mem_[0x00F2] = 0x73; /* 命令 LD (HL),E */
		mem_[0x00F3] = 0xC9;
	}
	/* CP/M BDOS @0005: ROOT.COM（genghis）CALL 5。XOR A／RET = 成功。本体 $00B7 は BIOS RET と PSG トランポリン @00C0 の間。 */
	if (mem_[0x0005] == 0x00 || mem_[0x0005] == 0xC9) {
		if (mem_[0x00B7] == 0x00 && mem_[0x00B8] == 0x00) {
			mem_[0x0005] = 0xC3; mem_[0x0006] = 0xB7; mem_[0x0007] = 0x00;
			mem_[0x00B7] = 0xAF; /* 命令 XOR A */
			mem_[0x00B8] = 0xC9;
		} else if (mem_[0x0005] == 0x00)
			mem_[0x0005] = 0xC9;
	}
	/* SNSMAT $0141。Warp & Warp ISR は CALL $0141（行 5/8）のあと PSG ミキサへ AND。0 埋めは PATCH @0400 へスライドし音量 0。3 バイトが空のときだけ植える — Compile DRIVER.BIN @0100（puyo/dsdx1）に見せてはいけない。 */
	if (mem_[0x0141] == 0 && mem_[0x0142] == 0 && mem_[0x0143] == 0) {
		mem_[0x0141] = 0x3E; /* 命令 LD A,$FF */
		mem_[0x0142] = 0xFF;
		mem_[0x0143] = 0xC9;
	}
}

/* CHardMsx::KeepCompileRan2Alive の実装 */
void CHardMsx::KeepCompileRan2Alive()
{
	if (!genericMode_ || !cpu_) return;
	/* Sky Jaguar SCC+: $4416 のミックスは D280 bit1 でゲート。16K ダンプはそのビットを書かない。play/ISR は settle 植込後にクリアし得る。 */
	if (sccEnable_
		&& mem_[0x4009] == 0xC3 && mem_[0x400A] == 0x27 && mem_[0x400B] == 0x42)
		mem_[0xD280] |= 0x02;
	if (mem_[0x0418] != 0xFE || mem_[0x0419] != 0x65) return;
	if (mem_[0x041E] != 0xCD || mem_[0x041F] != 0x7D || mem_[0x0420] != 0x53)
		return;
	if (mem_[0x0038] != 0xC3) {
		mem_[0x0038] = 0xC3; mem_[0x0039] = 0xE0; mem_[0x003A] = 0x00;
		mem_[0x00E0] = 0xF5;
		mem_[0x00E1] = 0xCD; mem_[0x00E2] = 0x9F; mem_[0x00E3] = 0xFD;
		mem_[0x00E4] = 0xF1; mem_[0x00E5] = 0xFB; mem_[0x00E6] = 0xC9;
		PlantPsgTrampoline(mem_);
	}
	if (mem_[0xFD9F] != 0xC3 || mem_[0xFDA0] != 0xF9 || mem_[0xFDA1] != 0x44) {
		mem_[0xFD9F] = 0xC3;
		mem_[0xFDA0] = 0xF9;
		mem_[0xFDA1] = 0x44;
	}
}

/* メモリマップを切り替える */
void CHardMsx::MapAscii16(int page, uint8_t bank)
{
	if (!cart_ || cartBytes_ == 0) return;
	unsigned nBanks = (cartBytes_ + 0x3FFFu) / 0x4000u;
	if (nBanks == 0) nBanks = 1;
	const unsigned b = (unsigned)bank % nBanks;
	const unsigned off = b * 0x4000u;
	const uint16_t dest = page ? 0x8000 : 0x4000;
	unsigned n = 0x4000u;
	if (off >= cartBytes_) return;
	if (off + n > cartBytes_) n = cartBytes_ - off;
	memcpy(mem_ + dest, cart_ + off, n);
	if (n < 0x4000u)
		memset(mem_ + dest + n, 0xFF, 0x4000u - n);
	ascii16Bank_[page & 1] = (uint8_t)b;
}

/* メモリマップを切り替える */
void CHardMsx::MapAscii8(int page, uint8_t bank)
{
	if (!cart_ || cartBytes_ == 0) return;
	unsigned nBanks = (cartBytes_ + 0x1FFFu) / 0x2000u;
	if (nBanks == 0) nBanks = 1;
	const unsigned b = (unsigned)bank % nBanks;
	const unsigned off = b * 0x2000u;
	const uint16_t dest = (uint16_t)(0x4000u + (unsigned)(page & 3) * 0x2000u);
	unsigned n = 0x2000u;
	if (off >= cartBytes_) return;
	if (off + n > cartBytes_) n = cartBytes_ - off;
	memcpy(mem_ + dest, cart_ + off, n);
	if (n < 0x2000u)
		memset(mem_ + dest + n, 0xFF, 0x2000u - n);
	ascii8Bank_[page & 3] = (uint8_t)b;
}

/* メモリマップを切り替える */
void CHardMsx::MapDs4(int page, uint8_t bank)
{
	if (!cart_ || cartBytes_ == 0) return;
	unsigned nBanks = (cartBytes_ + 0x1FFFu) / 0x2000u;
	if (nBanks == 0) nBanks = 1;
	const unsigned b = (unsigned)bank % nBanks;
	const unsigned off = b * 0x2000u;
	const uint16_t dest = page ? 0xA000 : 0x8000;
	unsigned n = 0x2000u;
	if (off >= cartBytes_) return;
	if (off + n > cartBytes_) n = cartBytes_ - off;
	memcpy(mem_ + dest, cart_ + off, n);
	ds4Bank_[page & 1] = (uint8_t)b;
}

/* データを載せる */
int CHardMsx::LoadCartRom(CEmuZipFs* fs, const CEmuGameEntry* ge, const char* type)
{
	if (!fs || !ge || !type) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, type) != 0) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, ge->rom[i].name, &sz);
		if (!data || sz < 16) continue;
		if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
		cart_ = (uint8_t*)malloc(sz);
		if (!cart_) return 0;
		memcpy(cart_, data, sz);
		cartBytes_ = sz;
		return 1;
	}
	int best = -1;
	unsigned bestSz = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		if (_stricmp(pathA, "patch") == 0 || _stricmp(pathA, "fmpatch") == 0)
			continue;
		if (fs->files[i].size > bestSz) { bestSz = fs->files[i].size; best = i; }
	}
	if (best < 0 || bestSz < 16) return 0;
	if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
	cart_ = (uint8_t*)malloc(bestSz);
	if (!cart_) return 0;
	memcpy(cart_, fs->files[best].data, bestSz);
	cartBytes_ = bestSz;
	return 1;
}

/* PCM／コードバンク */
void CHardMsx::ApplyBank(uint8_t bankSel)
{
	if (!bank_ || bankNum_ == 0) return;
	const int bankno = (int)bankSel - (int)bankOfs_;
	if (bankno < 0 || bankno >= (int)bankNum_) {
		/* hoot kss.cpp: 範囲外 $FE マップは fetch/read を RAM へ戻す。sorc/bburn は OUT $7F のあとプレーヤを $8000/$B000 へ LDIR。 */
		memcpy(mem_ + 0x8000, bankShadow_, 0x4000);
		return;
	}
	/* hoot kss.cpp ポート $FE は 8K モードでも $8000 に 16K をマップ（8K ページは $9000/$B000 から切替）。 */
	const unsigned off = (unsigned)bankno * 0x4000u;
	if (off + 0x4000u <= bankBytes_)
		memcpy(mem_ + 0x8000, bank_ + off, 0x4000);
}

/* メモリマップを切り替える */
void CHardMsx::MapDefault()
{
}

/* I/O ポート読込 */
uint8_t CHardMsx::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (mapper_ == MAP_DS4) {
		if (p == 0x00) {
			const uint8_t ret = ioport_[0];
			ioport_[0] = 0;
			return ret;
		}
		if (p == 0x02) {
			idle_ = 1;
			return 0;
		}
	}
	if (dqMode_ && p == 0x99)
		return 0x80; /* VDP ステータス: 常に vblank。DQ2 IN B,(C) が進む */
	if (p == SKIP_PORT) {
		idle_ = 1;
		return 0;
	}
	/* BirdySoft/Compile/Enix パッチは再生を IN A,(2) で poll。init settle 後にワンショットエッジを武装 — sticky High は再トリガし PSG をクリアする。 */
	if (genericMode_ && p == 0x02) {
		if (playCmdPending_ > 0) {
			playCmdPending_--;
			/* firehawk 0xFF: 1 回目エッジで TMUS1M 再生済み。2 回目で CP FF 経路。 */
			if (playCmdPending_ == 6 && ioport_[6] == 0xB0) {
				ioport_[3] = 0xFF;
				ioport_[6] = 0;
			}
			return 0x01;
		}
		return 0;
	}
	if (genericMode_ && p == 0x04)
		return (uint8_t)(titleCode_ & 0xff);
	if (p == 0xa0 || p == 0xa1 || p == 0xa2) {
		if (chipAy_) return chipAy_->ReadData();
		return 0xff;
	}
	/* PPI ポート B（キーボード）。アクティブ Low。$00 は全キー押しに見える。TTLPRG の vblank ISR は IN A,($AA)/($A9) で行 8 をサンプル。 */
	if (p == 0xa9)
		return 0xff;
	if ((p == 0xc0 || p == 0xc1) && chipOpl_) {
		/* Y8950 ドライバはステータス bit7（タイマ IRQ）で回る。ここは YM3812 タイマをクロックしない。ready を返し CALL 6009 が FM を出せるようにする。 */
		return (uint8_t)(chipOpl_->ReadStatus() | 0x80);
	}
	/* MSX2+ システムタイマ E6/E7。Illusion City DRIVER.BIN は OPLL 書きを IN A,($E6); SUB C; CP 6; JR C で間隔。定数ポートだと $80DE で回り OPLL キー 0。各 IN で進める: Ay_Cpu::run は cpuCycles_ 更新前に IN を複数出し得る。 */
	if (p == 0xE6)
		return ++ioport_[0xE6];
	if (p == 0xE7)
		return ioport_[0xE7];
	return ioport_[p];
}

/* I/O ポート書込 */
void CHardMsx::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	if (p == 0xc0 || p == 0xc1) {
		if (chips_ & CHIP_MSXAUDIO)
			EnsureMsxAudio();
		if (chipOpl_) {
			chipOpl_->Write((uint32_t)(p & 1), data);
			ioport_[p] = data;
			return;
		}
	}
	if (p == 0x7c || p == 0x7d || p == 0xc0 || p == 0xc1 || p == 0xf0 || p == 0xf1) {
		/* 汎用パッチはカタログ use_opll ビット無しで 7C へ OUT することが多い。KSS は OPLL を強制生成しない — 迷 poke が AY/SCC 曲の上にドローンを残す（sorc_msx NOSEQ 回帰）。 */
		if (genericMode_)
			EnsureOpll(1);
		else
			EnsureOpll(0);
		if (chipOpll_) {
			OPLL* o = (OPLL*)chipOpll_;
			if ((p & 1) == 0)
				opllLatch_ = data;
			else {
				OPLL_writeReg(o, opllLatch_, data);
				opllRegs_[opllLatch_ & 0x3f] = data;
				FmMonShadowApplyOpllRegs(opllRegs_);
				opllWriteCount_++;
			}
		}
		ioport_[p] = data;
		return;
	}
	if (p == 0xa0 || p == 0xa1) {
		if (chipAy_) {
			chipAy_->Write((uint32_t)(p & 1), data);
			ayWriteCount_++;
		}
		ioport_[p] = data;
		return;
	}
	if (p == 0x7e || p == 0x7f) {
		EnsureSng();
		if (chipSng_)
			chipSng_->Write(0, data);
		ioport_[p] = data;
		return;
	}
	if (p == 0xfe) {
		ApplyBank(data);
		ioport_[p] = data;
		return;
	}
	/* 汎用 BirdySoft/Compile パッチ: OUT (0) が BGM 添字を載せることがある */
	if (genericMode_ && p == 0x00 && data < BGM_BANKS && bgmPresent_[data])
		StageBgm(data);
	ioport_[p] = data;
}

/* メモリ 8bit 書込 */
void CHardMsx::MemWrite(uint16_t addr, uint8_t data)
{
	/* FMPAC／MSX-MUSIC メモリマップ OPLL（ポート 7C/7D と同じラッチ／データ）。汎用カートリッジパッチのみ — KSS ドライバは 7C/7D で OPLL と話し、KSS で 7FF4/5 を OPLL 扱いすると通常 RAM 書きを奪う（sorc NOSEQ）。 */
	if ((genericMode_ || mapper_ != MAP_NONE) && (addr == 0x7ff4 || addr == 0x7ff5)) {
		EnsureOpll(1);
		if (chipOpll_) {
			OPLL* o = (OPLL*)chipOpll_;
			if (addr == 0x7ff4)
				opllLatch_ = data;
			else {
				OPLL_writeReg(o, opllLatch_, data);
				opllRegs_[opllLatch_ & 0x3f] = data;
				FmMonShadowApplyOpllRegs(opllRegs_);
				opllWriteCount_++;
			}
		}
		return;
	}

	if (mapper_ == MAP_ASCII16) {
		if (addr >= 0x6000 && addr < 0x7000) { MapAscii16(0, data); return; }
		if (addr >= 0x7000 && addr < 0x8000) { MapAscii16(1, data); return; }
		if (addr >= 0x4000 && addr < 0xC000) return;
	} else if (mapper_ == MAP_ASCII8) {
		if (addr >= 0x6000 && addr < 0x6800) { MapAscii8(0, data); return; }
		if (addr >= 0x6800 && addr < 0x7000) { MapAscii8(1, data); return; }
		if (addr >= 0x7000 && addr < 0x7800) { MapAscii8(2, data); return; }
		if (addr >= 0x7800 && addr < 0x8000) { MapAscii8(3, data); return; }
		if (addr >= 0x4000 && addr < 0xC000) return;
	} else if (mapper_ == MAP_DS4) {
		if (addr == 0x7000) { MapDs4(0, data); return; }
		if (addr == 0x7800) { MapDs4(1, data); return; }
		if (addr >= 0x4000 && addr < 0xC000) return;
	}

	/* Konami SCC: KSS は (addr & 0xDFFF) ^ 0x9800 で 9800 と B800 を同じ 0x90 バイトファイルへマップ。カートリッジ実機ではマッパへ 0x3F（page2）を書いてから窓が出る。そのゲート無しだと汎用タイトルの 9800 RAM が奪われる。 */
	if (chipScc_ && (sccEnable_ || sccMapped_)) {
		if (addr == 0xBFFE) {
			if (data & 0x20) {
				sccMapped_ = 1;
				/* SCC-I plus 窓は RAM ではなくマッパラッチ。バンクコピーが $BFFE を上書き。sccEnable_ は $9000/$B000 が 0x3F から離れても $B800 をルーティングし続ける（gradius 拡張 SCC キー $B8A0）。 */
				sccEnable_ = 1;
				CEmuChipSccSetPlusMode(chipScc_, 1);
			} else {
				sccEnable_ = 0;
				CEmuChipSccSetPlusMode(chipScc_, 0);
			}
			mem_[addr] = data;
			return;
		}
		if (addr == 0x9000) {
			if (!sccEnable_)
				sccMapped_ = MsxIsSccMapperData(data) ? 1 : 0;
			/* バンクハンドラへフォールスルー */
		} else if (addr == 0xb000) {
			/* SCC-I／一部 MegaROM は page3 経由でミラーを出す。$3F/$80/$BF のみ。 */
			if (MsxIsSccMapperData(data))
				sccMapped_ = 1;
			/* バンクハンドラへフォールスルー */
		} else {
			/* $B800-B8BF は SCC-I plus 窓（freq/vol @ $B8A0）。$9800-98BF は古典 SCC。XOR エイリアスは同じオフセットだが plus モードは $8F ではなく $AF をキーする。 */
			if (addr >= 0xB800 && addr < 0xB8C0)
				CEmuChipSccSetPlusMode(chipScc_, 1);
			const unsigned sccAddr = (unsigned)((addr & 0xdfffu) ^ 0x9800u);
			if (sccAddr < 0xC0u) {
				CEmuChipSccWriteReg(chipScc_, sccAddr, data);
				sccAccessed_ = 1;
				return;
			}
		}
	} else if (chipScc_ && !genericMode_
		&& (addr == 0x9000 || addr == 0xb000 || addr == 0xBFFE)) {
		/* KSS マッパプローブのみ。汎用 Falcom BIOS（sor/sorpyr/sorsen）は play/stop フラグを $B000（$FF／$00）に置く。$FF & $3F == $3F が sccMapped_ をラッチし MemRead が BIOS イメージから $B800-B8BF を奪った（AY 周波数書き、音量 0）。 */
		if (addr == 0xBFFE) {
			/* Snatcher init は $9000=3F の前に LD ($BFFE),$20。マップ窓経路がその書きを見ず、3F をラッチしない SCC-I タイトルが PSG のまま（7C/7D/86 無音）。 */
			if (data & 0x20) {
				sccMapped_ = 1;
				sccEnable_ = 1;
				CEmuChipSccSetPlusMode(chipScc_, 1);
			} else {
				sccEnable_ = 0;
				CEmuChipSccSetPlusMode(chipScc_, 0);
			}
			mem_[addr] = data;
			return;
		}
		if (addr == 0x9000)
			sccMapped_ = MsxIsSccMapperData(data) ? 1 : 0;
		else if (MsxIsSccMapperData(data))
			sccMapped_ = 1;
	}

	if (bank8k_ && bank_ && bankNum_) {
		if (addr == 0x9000 || addr == 0xb000) {
			const int page = (addr == 0x9000) ? 0 : 1;
			const uint16_t base = (addr == 0x9000) ? 0x8000 : 0xa000;
			const int bankno = (int)data - (int)bankOfs_;
			if (bankno >= 0 && bankno < (int)bankNum_) {
				const unsigned off = (unsigned)bankno * 0x2000u;
				if (off + 0x2000u <= bankBytes_)
					memcpy(mem_ + base, bank_ + off, 0x2000);
				bank8kRam_[page] = 0;
			} else {
				/* hoot: 無効バンクマップは fetch/read を隠し RAM へ戻す */
				memcpy(mem_ + base, bankShadow_ + (unsigned)page * 0x2000u, 0x2000);
				bank8kRam_[page] = 1;
			}
			return;
		}
		/* hoot: バンクを fetch/read、書きは隠し RAM。mem_ を上書きするとマップ ROM が壊れた（labyr/shiryo 8K KSS）。 */
		if (addr >= 0x8000 && addr < 0xC000) {
			bankShadow_[addr - 0x8000] = data;
			const int page = (addr < 0xA000) ? 0 : 1;
			if (bank8kRam_[page])
				mem_[addr] = data;
			return;
		}
	}
	mem_[addr] = data;
}

/* メモリ 8bit 読込 */
uint8_t CHardMsx::MemRead(uint16_t addr)
{
	/* カートリッジ SCC 窓（汎用＋マッパ 0x3F）。KSS は裏 ROM を読む必要がある: hoot は fetch/read を ram[] へマップし、Snatcher は曲ヘッダを $98A6／$B84A／$B7FA に置く — チップレジスタとして XOR エイリアスすると 6B/72/7E が無音。 */
	if (chipScc_ && sccMapped_ && genericMode_) {
		const unsigned sccAddr = (unsigned)((addr & 0xdfffu) ^ 0x9800u);
		if (sccAddr < 0xC0u)
			return CEmuChipSccReadReg(chipScc_, sccAddr);
	}
	return mem_[addr];
}

/* CHardMsx::IsKssMagic の実装 */
int CHardMsx::IsKssMagic(const unsigned char* data, unsigned sz) const
{
	if (!data || sz < 16) return 0;
	if (data[0] == 'K' && data[1] == 'S' && data[2] == 'C' && data[3] == 'C') return 1;
	if (data[0] == 'K' && data[1] == 'S' && data[2] == 'S' && data[3] == 'X') return 1;
	return 0;
}

/* バンク／BGM を載せる */
void CHardMsx::StageBgm(unsigned index)
{
	if (index >= BGM_BANKS || !bgmPresent_[index] || !bgmBank_[index]) return;
	unsigned n = bgmBankSize_[index];
	if (n > mdataSize_) n = mdataSize_;
	if (mdataAddr_ + n > 0x10000)
		n = 0x10000u - mdataAddr_;
	memcpy(mem_ + mdataAddr_, bgmBank_[index], n);
	if (n < mdataSize_ && mdataAddr_ + mdataSize_ <= 0x10000)
		memset(mem_ + mdataAddr_ + n, 0, mdataSize_ - n);
}

/* データを載せる */
int CHardMsx::LoadGeneric(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge || !cpu_) return 0;
	titleCode_ = titleCode;
	ge_ = ge;
	genericMode_ = 1;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	memset(opllRegs_, 0, sizeof(opllRegs_));
	idle_ = 0;
	playing_ = 0;
	chips_ = 0;
	ttlPrgBytes_ = 0;
	ttlPrgAddr_ = 0;

	CEmuMsxMergeCompanions(fs, ge);

	int loadedCode = 0;
	int loaded4000 = 0;
	int catalogDirMiss = 0;
	int catalogKss = 0;
	int useOpll = ParseOptHex(ge, "use_opll", 0);
	int useMsxa = ParseOptHex(ge, "use_msxa", 0);
	int hasSccp = 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "sccp") == 0) {
			hasSccp = 1;
			break;
		}
	}
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = (_stricmp(r->type, "code") == 0)
			? FindMsxCodeRom(fs, r->name, &sz)
			: CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) {
			/* crimson2/3 の zip は PMUS* と FMUS* の両方を持つ。PMUS が見つからないときだけ FM 側へ落とす。 */
			if (_stricmp(r->type, "bgm") == 0 && r->name[0]
				&& !_strnicmp(r->name, "PMUS", 4)) {
				char alt[CEMU_ROM_NAME];
				strncpy_s(alt, r->name, _TRUNCATE);
				alt[0] = (r->name[0] == 'p') ? 'f' : 'F';
				data = CEmuZipFsFind(fs, alt, &sz);
			}
			if (!data || !sz) {
				if (_stricmp(r->type, "code") == 0 && r->name[0]) {
					const size_t nl = strlen(r->name);
					if (nl >= 4 && _stricmp(r->name + nl - 4, ".kss") == 0)
						catalogKss = 1;
					if (strchr(r->name, '/') || strchr(r->name, '\\'))
						catalogDirMiss = 1;
				}
				continue;
			}
		}

		if (_stricmp(r->type, "code") == 0 || _stricmp(r->type, "fmbios") == 0
			|| _stricmp(r->type, "rom") == 0) {
			int off = r->offset;
			/* type=rom はカートリッジイメージ — page0 BIOS の外。ここで type=sccp を平坦化しない: Konami SCC+ ダンプは GRA.BIN @4000 を重ね、kgc* はパッチだけで既に PLAYS。 */
			if (_stricmp(r->type, "rom") == 0 && off <= 0)
				off = 0x4000;
			/* FMPAC.ROM @0 はスロット 1 カートリッジイメージであり page0 ではない。0 にマップすると BIOS stub とパッチ @0400 が消える（yosikon/winsltn）。一部 XML 行は code @4000（laplace）— それでもクリップ。 */
			const int isFmpac = (_stricmp(r->type, "fmbios") == 0
				|| _stricmp(r->name, "FMPAC.ROM") == 0) ? 1 : 0;
			if (isFmpac && off <= 0)
				off = 0x4000;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			/* Nemesis SCC+: GRA.BIN @4000 は loaded4000 を立て GRASCC.BIN を飛ばす。TwinBee SCC+ 行に code @4000 は無い。sccp を GRA へ重ねず sccp をロード。 */
			if (hasSccp && _stricmp(r->type, "code") == 0 && off == 0x4000)
				continue;
			/* パッド 64K FMPAC.ROM @4000 が後続コードを消してはいけない（yosikon DRIVER @$D400、winsltn ALL.BIN @$B9B9、rona MUSDRV @$CE00） */
			if (isFmpac) {
				for (int j = 0; j < ge->romCount; j++) {
					if (j == i) continue;
					const CEmuRomEntry* o = &ge->rom[j];
					if (_stricmp(o->type, "code") != 0 && _stricmp(o->type, "rom") != 0)
						continue;
					int ooff = o->offset;
					if (_stricmp(o->type, "rom") == 0 && ooff <= 0)
						ooff = 0x4000;
					if (ooff > off && ooff < off + (int)n)
						n = (unsigned)(ooff - off);
				}
			}
			memcpy(mem_ + off, data, n);
			loadedCode++;
			if (off < 0x8000 && off + (int)n > 0x4000)
				loaded4000 = 1;
			if (isFmpac)
				chips_ |= CHIP_FMPAC;
			if (_strnicmp(r->name, "TTLPRG", 6) == 0 && n > 0 && n <= sizeof(ttlPrg_)) {
				memcpy(ttlPrg_, data, n);
				ttlPrgBytes_ = n;
				ttlPrgAddr_ = (uint16_t)off;
			}
		} else if (_stricmp(r->type, "bgm") == 0) {
			int idx = r->offset;
			if (idx < 0 || idx >= BGM_BANKS) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			/* wingsp FMAOI は BLOAD FE 無し（周期ワード）。PSGAOI が本物の COM。
			   FMEND は同じ周期ダンプだが PSGEND が AOI プレーヤ＋ゴミ $8DE7 なので
			   差し替えない。LAST WING は PCH プレーヤを後から被せる。 */
			if (n && buf[0] != 0xFE && r->name[0]
				&& (r->name[0] == 'F' || r->name[0] == 'f')
				&& (r->name[1] == 'M' || r->name[1] == 'm')
				&& !((r->name[2] == 'E' || r->name[2] == 'e')
					&& (r->name[3] == 'N' || r->name[3] == 'n'))) {
				char alt[64];
				unsigned k = 0;
				alt[0] = 'P'; alt[1] = 'S'; alt[2] = 'G';
				while (r->name[2 + k] && k + 4u < sizeof(alt)) {
					alt[3 + k] = r->name[2 + k];
					k++;
				}
				alt[3 + k] = 0;
				unsigned asz = 0;
				const unsigned char* altp = CEmuZipFsFind(fs, alt, &asz);
				if (altp && asz && altp[0] == 0xFE) {
					unsigned nn = asz;
					if (nn > (unsigned)BGM_SIZE) nn = (unsigned)BGM_SIZE;
					unsigned char* nb = (unsigned char*)malloc(nn ? nn : 1);
					if (nb) {
						memcpy(nb, altp, nn);
						free(buf);
						buf = nb;
						n = nn;
					}
				}
			}
			if (bgmBank_[idx]) free(bgmBank_[idx]);
			bgmBank_[idx] = buf;
			bgmBankSize_[idx] = n;
			bgmPresent_[idx] = 1;
		}
	}

	/* TwinBee/soccer SCC+ 行は patch@$0400＋type=sccp だけ。既に $4000 にある GRA.BIN（Nemesis SCC+）へ重ねない */
	if (!loaded4000) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "sccp") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || !sz)
				continue;
			int off = r->offset;
			if (off <= 0)
				off = 0x4000;
			if (off < 0 || off >= 0x10000)
				continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			memcpy(mem_ + off, data, n);
			/* 16K SCC+（Sky Jaguar／Super Cobra）は $4000-7FFF だけ。$8000 へ複製するとワーク RAM と SCC 窓を潰す。32K TwinBee はそのまま $4000-BFFF を覆う。 */
			loadedCode++;
			loaded4000 = 1;
			break;
		}
	}

	/* カタログ ROM 欠のとき zip のみフォールバック: パッチより本物ドライバを優先 */
	int zipInitPc = -1;
	int zipMdata = -1;
	int zipMsize = -1;
	/* dssp1 usajan/fuku/heian xml は zip が載せなかったディレクトリ修飾ダンプを名指す。フォールスルーが basename 経由で ran/DRIVER.BIN を奪い、それらのタイトルを SILENT で Open。puyo .kss xml は下の DRIVER@$0100 レイアウトがまだ要る。 */
	if (!loadedCode && catalogDirMiss && !catalogKss)
		return 0;
	if (!loadedCode) {
		unsigned szDrv = 0, szPat = 0, szData = 0, szBgm = 0, szTone = 0, szSe = 0;
		const unsigned char* drv = CEmuZipFsFind(fs, "DRIVER.BIN", &szDrv);
		const unsigned char* pat = CEmuZipFsFind(fs, "patch", &szPat);
		const unsigned char* dataBin = CEmuZipFsFind(fs, "DATA.BIN", &szData);
		const unsigned char* bgmdrv = CEmuZipFsFind(fs, "BGMDRV.BIN", &szBgm);
		const unsigned char* tone = CEmuZipFsFind(fs, "TONE.BIN", &szTone);
		const unsigned char* se = CEmuZipFsFind(fs, "SE.BGE", &szSe);
		const int namePsg = (ge->name[0] && wcsstr(ge->name, L"(PSG)")) ? 1 : 0;
		if (drv && pat && szDrv >= 256 && szPat >= 8) {
			/* Compile puyo kss xml は欠 .kss を名指す。ネイティブレイアウトは DRIVER@$0100 DATA@$2000 patch@$6000 init $6000 */
			unsigned n = szDrv;
			if (0x100u + n > 0x10000u) n = 0xFF00u;
			memcpy(mem_ + 0x0100, drv, n);
			if (dataBin && szData) {
				n = szData;
				if (0x2000u + n > 0x10000u) n = 0xE000u;
				memcpy(mem_ + 0x2000, dataBin, n);
			}
			n = szPat;
			if (0x6000u + n > 0x10000u) n = 0xA000u;
			memcpy(mem_ + 0x6000, pat, n);
			loadedCode = 1;
			zipInitPc = 0x6000;
			if (!namePsg)
				useOpll = 1;
		} else if (bgmdrv && pat && szBgm >= 256 && szPat >= 8) {
			/* Princess Maker kss xml は欠 .kss を名指す。ネイティブレイアウトは patch@$0400 TONE@$C600 SE@$C800 BGMDRV@$CE00 mdata $B600 */
			unsigned n = szPat;
			if (0x0400u + n > 0x10000u) n = 0xFC00u;
			memcpy(mem_ + 0x0400, pat, n);
			if (tone && szTone) {
				n = szTone;
				if (0xC600u + n > 0x10000u) n = 0x3A00u;
				memcpy(mem_ + 0xC600, tone, n);
			}
			if (se && szSe) {
				n = szSe;
				if (0xC800u + n > 0x10000u) n = 0x3800u;
				memcpy(mem_ + 0xC800, se, n);
			}
			n = szBgm;
			if (0xCE00u + n > 0x10000u) n = 0x3200u;
			memcpy(mem_ + 0xCE00, bgmdrv, n);
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA,
					(int)sizeof(pathA), NULL, NULL);
				if (pathA[0] != 'G' && pathA[0] != 'g') continue;
				size_t ln = strlen(pathA);
				if (ln < 8 || _stricmp(pathA + ln - 4, ".BGM") != 0) continue;
				int hi = pathA[1] - '0';
				int lo = (int)strtol(pathA + 2, NULL, 16);
				int idx = -1;
				if (hi == 1) idx = lo;
				else if (hi == 6) idx = 8 + lo;
				else if (hi == 7) idx = 0x1E + lo;
				if (idx < 0 || idx >= BGM_BANKS) continue;
				unsigned bsz = fs->files[i].size;
				if (bsz > (unsigned)BGM_SIZE) bsz = (unsigned)BGM_SIZE;
				unsigned char* buf = (unsigned char*)malloc(bsz ? bsz : 1);
				if (!buf) continue;
				memcpy(buf, fs->files[i].data, bsz);
				if (bgmBank_[idx]) free(bgmBank_[idx]);
				bgmBank_[idx] = buf;
				bgmBankSize_[idx] = bsz;
				bgmPresent_[idx] = 1;
			}
			loadedCode = 1;
			zipInitPc = 0x0400;
			zipMdata = 0xB600;
			zipMsize = 0x1000;
			if (!namePsg)
				useOpll = 1;
		} else {
			int best = -1;
			unsigned bestSz = 0;
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				const unsigned sz = fs->files[i].size;
				if (sz < 256) continue;
				if (_stricmp(pathA, "patch") == 0) continue;
				if (sz > bestSz) { bestSz = sz; best = i; }
			}
			if (best >= 0) {
				unsigned n = fs->files[best].size;
				if (n > 0xC000) n = 0xC000;
				memcpy(mem_ + 0x4000, fs->files[best].data, n);
				loadedCode = 1;
			}
			/* 小さなパッチ @0400 があれば常にマップ */
			for (int i = 0; i < fs->fileCount; i++) {
				char pathA[CEMU_ZIP_PATH];
				WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
				if (_stricmp(pathA, "patch") != 0) continue;
				unsigned n = fs->files[i].size;
				if (n > 0x200) n = 0x200;
				memcpy(mem_ + 0x400, fs->files[i].data, n);
				loadedCode++;
				break;
			}
		}
	}
	if (!loadedCode) return 0;

	/* Hoot 汎用パッチは MSX BIOS を CALL（KOEI: 0090 GICINI、0093 WRTPSG、0096 RDPSG）。ゼロ NOP sled への CALL 0090 はパッチへ再入（pc≈01AB）。GICINI と WRTPSG/RDPSG トランポリンにだけ RET を植える — 0..0x3FF 全部 RET 埋めしない（undead ENASLT CALL 0024 が壊れる）。実装本体 @00C0/00D0 は undead OPLLDRV@0100 より下。 */
	PlantPsgTrampoline(mem_);
	PlantBiosStubs();

	initPc_ = (uint16_t)ParseOptHex(ge, "init_pc", 0x400);
	mdataAddr_ = (uint16_t)ParseOptHex(ge, "mdata_addr", 0xA400);
	{
		int catalogMs = ParseOptHex(ge, "mdata_size", 0x800);
		int ms = catalogMs;
		int mfs = ParseOptHex(ge, "mfile_size", 0);
		if (mfs > ms) ms = mfs;
		if (ms <= 0) ms = 0x800;
		if (ms > BGM_SIZE) ms = BGM_SIZE;
		mdataSize_ = (unsigned)ms;
		if (catalogMs <= 0) catalogMs = 0x800;
		/* 溢れる判定はカタログ mdata_size だけ。mfile_size で膨らますと
		   sdaisen 9000+0x8000 が A000 へ滑り、MUSIC.COM の表 $87EB=$9000 が空になる。 */
		if (zipInitPc >= 0)
			initPc_ = (uint16_t)zipInitPc;
		if (zipMdata >= 0)
			mdataAddr_ = (uint16_t)zipMdata;
		if (zipMsize > 0)
			mdataSize_ = (unsigned)zipMsize;
		/* Tokuma MSX·FAN／msfield: カタログ窓が 64K を溢れるときだけ A000 へ。
		   FMPAC パッチ再生経路は HL=A000。mfile_size 膨張は見ない（sdaisen）。 */
		if ((unsigned)mdataAddr_ + (unsigned)catalogMs > 0x10000u) {
			mdataAddr_ = 0xA000;
			if (mdataSize_ > 0x6000u)
				mdataSize_ = 0x6000u;
		}
	}

	if (useOpll)
		chips_ |= CHIP_FMPAC;
	if (useMsxa)
		chips_ |= CHIP_MSXAUDIO;
	EnsureOpll(useOpll ? 1 : 0);
	/* dante2 PSG 行は FMPAC.ROM を載せない。MUS07 は 9ch FM のみで CALSLT $4110 が空。 */
	if (!useOpll && initPc_ == 0x400 && mdataAddr_ == 0xB700
		&& mem_[0xC700] == 0xC3 && mem_[0x4000] == 0) {
		CEmuMgr* mgr = CEmuMgrGet();
		if (mgr && mgr->dataRoot[0] && fs) {
			wchar_t path[MAX_PATH];
			_snwprintf_s(path, _TRUNCATE, L"%s\\msx\\fmpac_msx.zip", mgr->dataRoot);
			if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
				CEmuZipFsMergeZip(fs, path);
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, "FMPAC.ROM", &sz);
			if (data && sz) {
				unsigned n = sz;
				if (n > 0x4000u) n = 0x4000u;
				memcpy(mem_ + 0x4000, data, n);
				chips_ |= CHIP_FMPAC;
				EnsureOpll(1);
			}
		}
	}
	/* wingsp LAST WING: PSG 行のプレーヤは PSGPCH（$8DE7）。FMPCH と FMEND を末尾バンクへ退避し後で被せる。 */
	if (initPc_ == 0x400 && mdataAddr_ == 0x83F9) {
		unsigned sz = 0;
		const unsigned char* fmp = CEmuZipFsFind(fs, "FMPCH.COM", &sz);
		if (fmp && sz && fmp[0] == 0xFE) {
			const unsigned idx = (unsigned)BGM_BANKS - 1u;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* nb = (unsigned char*)malloc(n ? n : 1);
			if (nb) {
				memcpy(nb, fmp, n);
				if (bgmBank_[idx]) free(bgmBank_[idx]);
				bgmBank_[idx] = nb;
				bgmBankSize_[idx] = n;
				bgmPresent_[idx] = 1;
				chips_ |= CHIP_FMPAC;
				EnsureOpll(1);
			}
		}
		sz = 0;
		const unsigned char* fend = CEmuZipFsFind(fs, "FMEND.COM", &sz);
		if (fend && sz) {
			const unsigned idx = (unsigned)BGM_BANKS - 2u;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* nb = (unsigned char*)malloc(n ? n : 1);
			if (nb) {
				memcpy(nb, fend, n);
				if (bgmBank_[idx]) free(bgmBank_[idx]);
				bgmBank_[idx] = nb;
				bgmBankSize_[idx] = n;
				bgmPresent_[idx] = 1;
			}
		}
	}
	if (useMsxa)
		EnsureMsxAudio();
	/* use_scc（kgc SCC+ 行）は残す。その後ゼロにしない: kgc3/4 が無音になった。KSS は LoadKssImage でマッパ 0x3F をまだ待つ。 */
	{
		const int useScc = ParseOptHex(ge, "use_scc", 0);
		sccEnable_ = useScc ? 1 : 0;
	}
	sccMapped_ = 0;
	sccAccessed_ = 0;
	if (chipScc_) chipScc_->Reset();
	/* Nemesis SCC+: grascc は $4912（GRA.BIN マッパ）を CALL するが GRASCC 再生は $4006（AND $7F のあと表）。SE/BGM/stop の 3 箇所を付け替える。 */
	if (sccEnable_ && initPc_ == 0x400
		&& mem_[0x4000] == 0xC3 && mem_[0x4001] == 0xCF
		&& mem_[0x4002] == 0x60) {
		static const unsigned kNemesisCall4912[] = { 0x041C, 0x0424, 0x042D };
		for (unsigned k = 0; k < 3u; k++) {
			const unsigned a = kNemesisCall4912[k];
			if (mem_[a] == 0xCD && mem_[a + 1] == 0x12 && mem_[a + 2] == 0x49) {
				mem_[a + 1] = 0x06;
				mem_[a + 2] = 0x40;
			}
		}
	}
	/* Sky Jaguar SCC+: init CALL $0430 が mute $9D を E01A 優先スロットへキュー。Play $93/$91/$88 は RET C（0x13 < 0x1D）し ISR が BGM を開始しない。$4415 は PSG stub。SCC ミックスは D280 bit1 セット時のみ $4416 — 16K ダンプはそのビットを書かない。 */
	if (sccEnable_ && initPc_ == 0x400
		&& mem_[0x4009] == 0xC3 && mem_[0x400A] == 0x27
		&& mem_[0x400B] == 0x42) {
		if (mem_[0x0456] == 0xCD && mem_[0x0457] == 0x30
			&& mem_[0x0458] == 0x04) {
			mem_[0x0456] = 0x00;
			mem_[0x0457] = 0x00;
			mem_[0x0458] = 0x00;
		}
		if (mem_[0x4251] == 0xBB && mem_[0x4252] == 0xD8) {
			mem_[0x4251] = 0x00;
			mem_[0x4252] = 0x00;
		}
		mem_[0xD280] |= 0x02;
		if (chipScc_)
			CEmuChipSccSetPlusMode(chipScc_, 1);
	}

	/* dante OPLL: RSLREG $0138 が空なので $D2EA が PATCH @0400 へ NOP スライドし $D37B が H.TIMI を植えない。mbsp と同様に植える（Compile DRIVER.BIN @0100 に見せない）。$4018 の FMPAC.ROM id は PAC2OPLL、INIOPL は APRLOPLL と比較 — BIOS 文字列をパッチしスロットスキャンが成功、ワークポインタが $FD09 に着地するようにする。 */
	if (initPc_ == 0x400
		&& mem_[0x0454] == 0xCD && mem_[0x0455] == 0x7B && mem_[0x0456] == 0xD3
		&& mem_[0x0413] == 0xCD && mem_[0x0414] == 0xE4 && mem_[0x0415] == 0xD2) {
		if (mem_[0x0138] == 0x00) {
			mem_[0x0138] = 0xAF; /* 命令 XOR A */
			mem_[0x0139] = 0xC9; /* RET — プライマリスロット 0 */
			mem_[0xFCC1] = 0;
		}
		if (mem_[0x4018] == 'P' && mem_[0x4019] == 'A' && mem_[0x401C] == 'O'
			&& mem_[0x42BE] == 'A' && mem_[0x42BF] == 'P' && mem_[0x42C2] == 'O') {
			memcpy(mem_ + 0x42BE, mem_ + 0x4018, 8);
		}
		mem_[0xFD0B] = 0xA0;
		mem_[0xFD0C] = 0xD7;
	}

	/* laplace PSG PATCH2: hoot は 1 ファイルを $A000 に載せるので ADD HL,DE（port4<<8）が mdata を越える。BC06 を植えない — テンポリロード。1 にすると 12 tick 後 $7E1C が $7EBC mute に当たる。 */
	if (initPc_ == 0x400)
		PlantLaplacePsgPlay(mem_);

	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

/* データを載せる */
int CHardMsx::LoadKssImage(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	unsigned sz = 0;
	const unsigned char* data = NULL;

	/* 小さな hoot 「patch」stub より本物 KSS マジックを優先 */
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "code") != 0) continue;
		unsigned s = 0;
		const unsigned char* d = CEmuZipFsFind(fs, ge->rom[i].name, &s);
		if (!d || s < 0x10) continue;
		if (IsKssMagic(d, s)) { data = d; sz = s; break; }
	}
	if (!data) {
		for (int i = 0; i < ge->romCount; i++) {
			if (_stricmp(ge->rom[i].type, "code") != 0) continue;
			if (_stricmp(ge->rom[i].name, "patch") == 0) continue;
			data = CEmuZipFsFind(fs, ge->rom[i].name, &sz);
			if (data && sz >= 0x10) break;
			data = NULL; sz = 0;
		}
	}
	if (!data) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			size_t n = strlen(pathA);
			if (n < 4) continue;
			if (_stricmp(pathA + n - 4, ".kss") != 0) continue;
			data = fs->files[i].data;
			sz = fs->files[i].size;
			break;
		}
	}
	if (!data || sz < 0x10) return 0;
	if (!IsKssMagic(data, sz) && sz < 256) return 0;

	if (sz > sizeof(rom_)) sz = (unsigned)sizeof(rom_);
	memcpy(rom_, data, sz);

	loadAdr_ = Rd16(rom_ + 4);
	loadSize_ = Rd16(rom_ + 6);
	initAdr_ = Rd16(rom_ + 8);
	intAdr_ = Rd16(rom_ + 10);
	bankOfs_ = rom_[0x0c];
	bankNum_ = (uint8_t)(rom_[0x0d] & 0x7f);
	bank8k_ = (rom_[0x0d] & 0x80) ? 1 : 0;
	chips_ = rom_[0x0f];
	/* マッパへ 0x3F が書かれるまで $9800 を SCC として奪わない。hoot はチップバイトから SCC をマップするが、いくつかの KSCC（replcart）はそこにワーク RAM を置く。Konami SCC タイトルは先に 0x3F を書く。 */
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	if (chipScc_) chipScc_->Reset();

	if (bankNum_) {
		bankBytes_ = 0x4000u * (unsigned)bankNum_;
		bank_ = (uint8_t*)malloc(bankBytes_);
		if (bank_) {
			memset(bank_, 0, bankBytes_);
			const unsigned src = 0x10u + (unsigned)loadSize_;
			if (src < sz) {
				unsigned n = bankBytes_;
				if (src + n > sz) n = sz - src;
				memcpy(bank_, rom_ + src, n);
			}
		}
	}

	EnsureOpll(0);
	if (chips_ & CHIP_SNG)
		EnsureSng();
	if (chips_ & CHIP_MSXAUDIO)
		EnsureMsxAudio();
	MapDefault();
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

/* KSS／カートリッジを載せる */
int CHardMsx::LoadKss(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge || !cpu_) return 0;
	memset(mem_, 0, sizeof(mem_));
	memset(rom_, 0, sizeof(rom_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(opllRegs_, 0, sizeof(opllRegs_));
	if (bank_) { free(bank_); bank_ = NULL; bankBytes_ = 0; }
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	idle_ = 0;
	playing_ = 0;
	genericMode_ = 0;
	dqMode_ = 0;
	ge_ = ge;
	titleCode_ = titleCode;

	if (chipOpll_) {
		OPLL_delete((OPLL*)chipOpll_);
		chipOpll_ = NULL;
	}
	if (chipOpl_) {
		CEmuChipYm3812Destroy(chipOpl_);
		chipOpl_ = NULL;
	}
	if (cart_) { free(cart_); cart_ = NULL; cartBytes_ = 0; }
	mapper_ = MAP_NONE;

	if (_stricmp(ge->subtype, "ds4") == 0)
		return LoadDs4(fs, ge, titleCode);
	if (_stricmp(ge->subtype, "ascii16") == 0)
		return LoadAscii16(fs, ge, titleCode);
	if (_stricmp(ge->subtype, "dq1") == 0 || _stricmp(ge->subtype, "dq2") == 0)
		return LoadDq(fs, ge, titleCode);

	const int hasInitPc = ParseOptHex(ge, "init_pc", -1) >= 0
		|| ParseOptHex(ge, "mdata_addr", -1) >= 0;
	const int isKssSub = (_stricmp(ge->subtype, "kss") == 0
		|| _stricmp(ge->subtype, "opll") == 0);

	/* オフセット romlist（hoot 汎用／BirdySoft／Compile／…）— KSS ではない */
	int hasOffsetCode = 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "code") != 0) continue;
		if (ge->rom[i].offset > 0) { hasOffsetCode = 1; break; }
	}

	if ((!isKssSub && (hasInitPc || hasOffsetCode || _stricmp(ge->subtype, "generic") == 0))
		|| (hasOffsetCode && hasInitPc)) {
		if (LoadGeneric(fs, ge, titleCode))
			return 1;
	}

	if (LoadKssImage(fs, ge)) {
		genericMode_ = 0;
		return 1;
	}

	/* 最後の手段: カタログオプション無しの汎用（zip ローカル推測） */
	return LoadGeneric(fs, ge, titleCode);
}

/* CHardMsx::StartSongKss の実装 */
int CHardMsx::StartSongKss(unsigned titleCode)
{
	if (!cpu_) return 0;
	memset(mem_, 0xc9, 0x4000);
	memset(mem_ + 0x4000, 0x00, 0xc000);
	if (loadSize_ && loadAdr_ < 0x10000) {
		unsigned n = loadSize_;
		if ((unsigned)loadAdr_ + n > 0x10000)
			n = 0x10000u - loadAdr_;
		if (0x10u + n <= sizeof(rom_))
			memcpy(mem_ + loadAdr_, rom_ + 0x10, n);
	}
	memcpy(mem_, kKssIpl, sizeof(kKssIpl));
	mem_[INIT_ADR] = (uint8_t)(initAdr_ & 0xff);
	mem_[INIT_ADR + 1] = (uint8_t)(initAdr_ >> 8);
	mem_[INT_ADR] = (uint8_t)(intAdr_ & 0xff);
	mem_[INT_ADR + 1] = (uint8_t)(intAdr_ >> 8);
	mem_[0x93] = 0xc3;
	mem_[0x94] = 0x01;
	mem_[0x95] = 0x00;
	mem_[0x96] = 0xc3;
	mem_[0x97] = 0x09;
	mem_[0x98] = 0x00;

	/* King Kong 2 KSS initAdr は再生 $6032。カートコールド init @ $6000（ミキサ $C081=$BC、4 チャネルスロット $C010-$C016 = $6299）が走らず、ISR 末尾 JP ($C016) が $0000 に当たり IPL へ再入する。 */
	if (initAdr_ == 0x6032 && intAdr_ == 0x6228 && loadAdr_ == 0x6000
		&& mem_[0x6000] == 0x3E && mem_[0x6001] == 0xBC
		&& mem_[0x6022] == 0x21 && mem_[0x6023] == 0x99 && mem_[0x6024] == 0x62
		&& mem_[0x602E] == 0x22 && mem_[0x602F] == 0x16 && mem_[0x6030] == 0xC0) {
		static const uint8_t kKkon2Cold[] = {
			0xF5, 0xCD, 0x00, 0x60, 0xF1, 0xC3, 0x32, 0x60
		};
		memcpy(mem_ + 0x0020, kKkon2Cold, sizeof kKkon2Cold);
		mem_[INIT_ADR] = 0x20;
		mem_[INIT_ADR + 1] = 0x00;
	}

	/* Labyrinth チップバイト 0x08 は MSX-AUDIO。タイトル 0x80/0x81 は BIT 7 を立てローダが Y8950 経路を取る。タイマ tick する Y8950 が無いとその経路は FM をキーオンしない。bit7 クリアは PSG 編成。 */
	{
		uint8_t play = (uint8_t)(titleCode & 0xff);
		if ((chips_ & CHIP_MSXAUDIO) && !(chips_ & CHIP_FMPAC) && (play & 0x80))
			play = (uint8_t)(play & 0x7f);
		/* sorc_msx: PSG XML コードは FMPAC 添字＋$3D（m3u 0x1C オープニング vs XML 0x59）。$1EE8 表は恒等マップなので 0x59 が誤バンクをパックし Bitbuster 展開が 4 キードローンになった。 */
		if (initAdr_ == 0x1E00 && intAdr_ == 0xB001 && play >= 0x3Du)
			play = (uint8_t)(play - 0x3Du);
		ioport_[PLAY_CODE_PORT] = play;
	}
	/* 最初のマッパ書き前に $8000 RAM をスナップショット。hoot の無効 $FE バンクはその RAM を戻す。PortOut(0xfe,0) 後のキャプチャはバンク 0 を保存し sorc/bburn の OUT $7F を no-op にした。 */
	memcpy(bankShadow_, mem_ + 0x8000, 0x4000);
	PortOut(0xfe, 0);
	bank8kRam_[0] = bank8kRam_[1] = 0;
	{
		const unsigned loadEnd = ((unsigned)loadAdr_ + (unsigned)loadSize_ > 0x10000u)
			? 0x10000u : ((unsigned)loadAdr_ + (unsigned)loadSize_);
		/* 8K KSS bank0→$8000。ペイロードが既にその窓を埋めていたら飛ばす: gradius 拡張 SCC は $3FC0+$8040 から $BFFF までロードし、オーバーレイが init をゼロへ飛ばした（pc≈07xx、irq=0）。 */
		const int covers8000 = (loadAdr_ < 0xA000 && loadEnd > 0x8000) ? 1 : 0;
		if (bank8k_ && bank_ && bankBytes_ >= 0x2000u && !covers8000) {
			memcpy(mem_ + 0x8000, bank_, 0x2000);
			if (bankNum_ > 1 && bankBytes_ >= 0x4000u)
				memcpy(mem_ + 0xA000, bank_ + 0x2000, 0x2000);
		}
	}

	/* Konami KSS 展開（f1s3d）: $4000 の 0xC2/0xAA stub が CALL $4077。それが OUT $FE で 16K ページを出し $8000/$B000 へ LDIR。Z80 経由だと $9000 が NOP（pc は $92xx 表）、IFF1 オフで IPL ISR が tick しない。$4077 と同じく bank_ からプレーヤと選択曲を載せ、CALL $9000／JP $9003。 */
	if (bank_ && bankBytes_ >= 0x2000u
		&& loadAdr_ == 0x4000 && initAdr_ == 0x4003 && intAdr_ == 0x4000
		&& loadSize_ <= 0x100u
		&& mem_[0x4000] == 0xC3 && mem_[0x4001] == 0x06 && mem_[0x4002] == 0x90
		&& mem_[0x4077] == 0xC5 && mem_[0x4083] == 0xD3 && mem_[0x4084] == 0xFE
		&& mem_[0x4029] == 0x2E && mem_[0x406B] == 0x11) {
		const unsigned page0 = mem_[0x402A];
		const uint16_t songDest = (uint16_t)(mem_[0x406C] | ((uint16_t)mem_[0x406D] << 8));
		unsigned playerBytes = page0 * 256u;
		if (playerBytes > bankBytes_)
			playerBytes = bankBytes_;
		if (playerBytes > 0x4000u)
			playerBytes = 0x4000u;
		memcpy(mem_ + 0x8000, bank_, playerBytes);
		uint8_t play = ioport_[PLAY_CODE_PORT];
		const uint8_t* ix = mem_ + 0x4099;
		unsigned page = page0;
		int found = 0;
		int guard = 0;
		while (guard++ < 32 && ix + 4 <= mem_ + 0x4100) {
			if (ix[0] & 0x80)
				break;
			if (play < ix[2]) {
				const unsigned nPages = ix[0];
				uint16_t dest = songDest;
				for (unsigned i = 0; i < nPages; i++) {
					const unsigned p = page + i;
					const unsigned src = ((p >> 6) * 0x4000u) + ((p & 63u) * 256u);
					if (src + 256u > bankBytes_ || (unsigned)dest + 256u > 0x10000u)
						break;
					memcpy(mem_ + dest, bank_ + src, 256);
					dest = (uint16_t)(dest + 256);
				}
				ioport_[PLAY_CODE_PORT] = (uint8_t)(play + ix[3]);
				found = 1;
				break;
			}
			play = (uint8_t)(play - ix[2]);
			page += ix[0];
			ix += 4;
		}
		if (found) {
			mem_[0x4003] = 0xF5;
			mem_[0x4004] = 0xCD; mem_[0x4005] = 0x00; mem_[0x4006] = 0x90;
			mem_[0x4007] = 0xF1;
			mem_[0x4008] = 0xC3; mem_[0x4009] = 0x03; mem_[0x400A] = 0x90;
		} else {
			mem_[0x4000] = 0xC9;
			mem_[0x4003] = 0xC9;
		}
	}

	/* 未設定 I＋$C9 埋めは IM2 ベクタを $C9C9 にした。IPL ISR へ向ける */
	if (mem_[0x00FF] == 0xC9 && mem_[0x0100] == 0xC9) {
		mem_[0x00FF] = 0x38;
		mem_[0x0100] = 0x00;
	}

	cpu_->reset(mem_);
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 0;
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	if (chipScc_) chipScc_->Reset();
	if (chipSng_) chipSng_->Reset();
	if (chipAy_) chipAy_->Reset();
	/* Space Manbow KSS パッチは CALL $4C15（$9000=3F）を RET にし、$9860/$988F へ波形／キーを書く。hoot は chips=0 から SCC をマップ。その窓が無いと poke がバンク影に当たり SFX $4E が無音（BGM はまだ PSG）。 */
	if (initAdr_ == 0x5F00 && intAdr_ == 0x5F80 && loadAdr_ == 0x5F00
		&& bank8k_ && bankNum_ == 4
		&& mem_[0x5F07] == 0x3E && mem_[0x5F08] == 0xC9
		&& mem_[0x5F09] == 0x32 && mem_[0x5F0A] == 0x30
		&& mem_[0x5F0B] == 0x64 && mem_[0x642E] == 0x3E
		&& mem_[0x642F] == 0x3F) {
		sccEnable_ = 1;
	}

	CEmuHardMsxSetActive(this);
	/* Shiryo loadAdr=$0000: 16K イメージの RST 00 ベクタは ADD HL,A（85 6F D0 24 C9）、WRTPSG は $01FC。IPL $0000 は RST 10 なのでリセットが $0010 に届く。ヘルパはその RST のあとだけ重ねる。リセット前に $0000 へ RST 20 を植えるとブート全体を飛ばした。 */
	if (initAdr_ == 0x0BD1) {
		Ay_CpuRunOne(cpu_);
		static const uint8_t kAddHlA[] = { 0x85, 0x6F, 0xD0, 0x24, 0xC9 };
		static const uint8_t kAddDeA[] = { 0x83, 0x5F, 0xD0, 0x14, 0xC9 };
		memcpy(mem_ + 0x0000, kAddHlA, sizeof kAddHlA);
		memcpy(mem_ + 0x0008, kAddDeA, sizeof kAddDeA);
		mem_[0x93] = 0xc3;
		mem_[0x94] = 0xFC;
		mem_[0x95] = 0x01;
	}
	/* hoot Play() は SKIP（m_playing）まで 60Hz IRQ を上げない。Gradius 拡張 SCC は $493D で EI したあと 16K を LDIR。そのコピー中の vblank が転送途中で $8000 を再マップし ISR JP $4000 が戻らない（pc≈07xx、0038 消去）。HALT は IRQ を取るので vblank 待ち init が SKIP に届く。f1s3d はホスト展開で vblank も見せてはいけない（WRTPSG EI → CALL $4000）。 */
	int guard = 0;
	uint64_t nextIrq = (uint64_t)MSX_CPU_HZ / 60u;
	while (!idle_ && guard++ < 4000000) {
		uint8_t* m = cpu_->get_mem();
		if (m && m[cpu_->r.pc] == 0x76) {
			cpu_->irqDelay = 0;
			if (cpu_->r.iff1) {
				if (cpu_->r.im != 2 || !Ay_CpuIm2Interrupt(cpu_, 0xff))
					Ay_CpuIm1Interrupt(cpu_);
			}
			cpuCycles_ += 16;
			cpu_->adjust_time(16);
			if (cpuCycles_ >= nextIrq)
				nextIrq += (uint64_t)MSX_CPU_HZ / 60u;
			continue;
		}
		const int cyc = Ay_CpuRunOne(cpu_);
		if (cyc <= 0) break;
		cpuCycles_ += (uint64_t)cyc;
		if (cpuCycles_ >= nextIrq)
			nextIrq += (uint64_t)MSX_CPU_HZ / 60u;
	}
	playing_ = 1;
	idle_ = 0;
	return 1;
}

/* CHardMsx::StartSongGeneric の実装 */
int CHardMsx::StartSongGeneric(unsigned titleCode)
{
	if (!cpu_) return 0;
	/* puyo kss xml は 0x01/0x81。ネイティブ Compile パッチ（init $6000）は汎用 0x101 パックを欲する。Bit7 は PSG vs FM だけ。 */
	if (ge_ && _stricmp(ge_->subtype, "kss") == 0 && initPc_ == 0x6000
		&& (titleCode & ~0xFFu) == 0)
		titleCode = 0x100u | (titleCode & 0x7Fu);
	titleCode_ = titleCode;
	/* カタログコードは最大 3 バイトをパックし、幅がどれがどれかを示す:
	     1 バイト（angelus 0x05）— bgm rom を選び、かつ曲。
	     2 バイト（aleste2 0x0115、gshogi 0x0501）— 中間バイトが bgm rom、下位が曲。rom 番号をドライバへ渡すとどのタイトルも 1 曲になる。
	     3 バイト（ys2 0x010112）— 下位が rom、中間がトラック、上位がポート 5 から読むエンジン。
	   下位バイトでは区別できない: gshogi の 0x01 トラックは有効な bgm 添字でもある。 */
	const unsigned low = titleCode & 0xff;
	const unsigned mid = (titleCode >> 8) & 0xff;
	const unsigned top = (titleCode >> 16) & 0xff;
	unsigned song = low;   /* 載せる bgm rom */
	unsigned sel3 = low;   /* ポート 3 メールボックス */
	unsigned sel4 = low;   /* ポート 4 メールボックス */

	/* f1sp3d 0x0d09/0x0e09: 下位バイトは同じファイル、中間はそれ自体が bgm rom ではないトラック。herzog/ys3/ds32 も下位を共有するが中間はファイル — それらをトラック扱いすると ds32 が無音。 */
	int extraCode = 0;
	if (ge_) {
		for (int i = 0; i < ge_->romCount; i++) {
			if (_stricmp(ge_->rom[i].type, "code") != 0)
				continue;
			if (ge_->rom[i].offset == (int)initPc_)
				continue;
			extraCode = 1;
			break;
		}
	}
	int lowFilePack = 0;
	if (ge_) {
		for (int i = 0; i < ge_->titleCount && !lowFilePack; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			const unsigned li = ci & 0xff;
			const unsigned mi = (ci >> 8) & 0xff;
			if (mi < 8u || li >= BGM_BANKS || !bgmPresent_[li])
				continue;
			if (mi < BGM_BANKS && bgmPresent_[mi])
				continue;
			lowFilePack = 1;
		}
	}
	/* yajiuma/arugies: いくつかの 0xMM00 タイトルは「ファイル MM のトラック 0」。dios 0x0100 と SE 0x00 だけがその種 — 下位をファイルに保つ。 */
	int n00 = 0;
	if (ge_) {
		for (int i = 0; i < ge_->titleCount; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu || (ci & 0xff) != 0)
				continue;
			const unsigned mi = (ci >> 8) & 0xff;
			if (mi < BGM_BANKS && bgmPresent_[mi])
				n00++;
		}
	}
	const int fileInMid = (extraCode && n00 >= 3) ? 1 : 0;
	/* dios 0x0100-0x0107: mid=1 は BGM vs SE、下位はファイル。A=0 は停止 */
	int nMid1 = 0;
	if (ge_) {
		for (int i = 0; i < ge_->titleCount; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			if (((ci >> 8) & 0xff) != 1)
				continue;
			const unsigned li = ci & 0xff;
			if (li < BGM_BANKS && bgmPresent_[li])
				nMid1++;
		}
	}
	const int classInMid = (extraCode && !fileInMid && nMid1 >= 6) ? 1 : 0;
	/* wingsp 0x0206: extraCode=0、mid は OUT コマンド 1/2、下位は .COM。gshogi 0x0501 は mid>2。sgolveli 0x0126 はファイルではない下位。 */
	int cmdInMid = 0;
	if (!extraCode && ge_) {
		int ok = 1, n2 = 0, nCmd = 0;
		for (int i = 0; i < ge_->titleCount; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			const unsigned li = ci & 0xff;
			const unsigned mi = (ci >> 8) & 0xff;
			if (mi == 0)
				continue;
			n2++;
			if (mi > 2u || li >= BGM_BANKS || !bgmPresent_[li])
				ok = 0;
			if (mi >= 1u && mi <= 2u && li != mi)
				nCmd++;
		}
		cmdInMid = (ok && n2 >= 2 && nCmd >= 1) ? 1 : 0;
	}
	/* sgolveli 0x0101／0x0001: extraCode=0（DRIVER は type=bgm @0100）、init $6000／mdata $0100。runemst3/randar3/gshogi/gokudo はこのゲートを外す（DRIVER は type=code、または init が $0400）。 */
	int sameLowCmd = 0;
	if (!extraCode && !cmdInMid && initPc_ == 0x6000 && mdataAddr_ == 0x0100
		&& ge_) {
		for (int i = 0; i < ge_->titleCount && !sameLowCmd; i++) {
			const unsigned ci = ge_->title[i].code;
			if (ci > 0xFFFFu)
				continue;
			const unsigned li = ci & 0xff;
			const unsigned mi = (ci >> 8) & 0xff;
			if (li >= BGM_BANKS || !bgmPresent_[li])
				continue;
			for (int j = i + 1; j < ge_->titleCount; j++) {
				const unsigned cj = ge_->title[j].code;
				if (cj > 0xFFFFu || (cj & 0xff) != li)
					continue;
				if (((cj >> 8) & 0xff) != mi) {
					sameLowCmd = 1;
					break;
				}
			}
		}
	}
	/* ultima4 0x0104: ファイルは下位、1 始まりトラックは mid。MUSICMSX play は載せたファイルのポインタ表へ A-1。 */

	/* dquiz 0x0001/0x0101 と m123 0xC00000/0xC00011: パッチ IN A,(4) がポインタ表を添字し、IN A,(3) が曲。dquiz 偶数／奇数は low=1 を共有するので song=low が同じ MUSIC01 を載せた。ds32 は mdata $2000 — そのゲートを残す。3 バイト 0xC000xx はさもなくば addrBox に見える。 */
	const int compilePtr = (extraCode && initPc_ == 0xF000
		&& mdataAddr_ == 0xC000) ? 1 : 0;
	/* saziri 0x4000xxxx: 上位ワードは mdata 内番地。lenam 0xFF000A はコマンド＋ファイルであり番地ではない（hi=0xFF00）。 */
	const unsigned hiWordEarly = titleCode >> 16;
	const int addrBoxEarly = (mdataSize_ > 0
		&& hiWordEarly >= mdataAddr_
		&& hiWordEarly < (unsigned)mdataAddr_ + mdataSize_) ? 1 : 0;
	/* silviana/feedback/sbp/xanadus: IN A,(4); CP 1; JR Z,se — 排他 SE 経路。タイトル 0x01 は BGM ファイル 1 なので port4=1 は play に届かない。JR オフセット >= $13 は algowars/famicle2 を飛ばす（0x01 でも PLAYS）。ninja/ginei に bgm rom 1 は無い。ff_msx 0x01 は PLAYS スモーク。mbsp JR Z,$0E はその床より下なので 0x01 が SE CALL $AA09 を取った。mbsp を port4Se に折らない — ファイル 0 を載せ FMUS01 を無音にした。下の mbspPack は port4=0 で下位ファイルを載せる。 */
	int port4Se = 0;
	if (!port4Se && titleCode <= 0xFFu && low == 1 && bgmPresent_[1]
		&& (unsigned)initPc_ + 80u < 0x10000u) {
		for (unsigned i = 0; i + 6u < 80u; i++) {
			const unsigned a = (unsigned)initPc_ + i;
			if (mem_[a] == 0xDB && mem_[a + 1] == 0x04
				&& mem_[a + 2] == 0xFE && mem_[a + 3] == 0x01
				&& mem_[a + 4] == 0x28 && mem_[a + 5] >= 0x13) {
				port4Se = 1;
				break;
			}
		}
	}
	/* ds00: LD HL,$7228（DATA2）; IN A,(4); CP 1; JR NZ; LD HL,$443B（DATA1）。0x0001 は DATA1（port4=1）。0x0101 は port4=low=1 のまま両選びが DATA1 曲 1 を載せた。0x01xx は port3=low で DATA2 を取る。 */
	int ds00Data = 0;
	if (initPc_ == 0x6000
		&& mem_[0x6016] == 0x21 && mem_[0x6017] == 0x28 && mem_[0x6018] == 0x72
		&& mem_[0x6019] == 0xDB && mem_[0x601A] == 0x04
		&& mem_[0x601B] == 0xFE && mem_[0x601C] == 0x01) {
		ds00Data = 1;
	}
	/* ps8/kubikiri/quinplf: IN A,(4); INC A が 1 始まりファイル内曲を格納。タイトル 0x01 はファイル 1 なので port4=song が SIM GIRL／FM01 を無音にした。 */
	int port4Inc = 0;
	if ((unsigned)initPc_ + 80u < 0x10000u) {
		for (unsigned i = 0; i + 3u < 80u; i++) {
			const unsigned a = (unsigned)initPc_ + i;
			if (mem_[a] == 0xDB && mem_[a + 1] == 0x04
				&& mem_[a + 2] == 0x3C) {
				port4Inc = 1;
				break;
			}
		}
	}
	/* tantexr: IN A,(6); LD H,A; IN A,(5); LD L,A のあと CALL $C000。4 バイト 0x239D0000 は LDIR dest 内の CALL $239D であり mdata 番地ではない。 */
	int port56Hl = 0;
	if ((unsigned)initPc_ + 80u < 0x10000u) {
		for (unsigned i = 0; i + 6u < 80u; i++) {
			const unsigned a = (unsigned)initPc_ + i;
			if (mem_[a] == 0xDB && mem_[a + 1] == 0x06
				&& mem_[a + 2] == 0x67 && mem_[a + 3] == 0xDB
				&& mem_[a + 4] == 0x05 && mem_[a + 5] == 0x6F) {
				port56Hl = 1;
				break;
			}
		}
	}

	/* gokudo: IN A,(4); CP 1 が ENDMSX.COM を選ぶ。IN A,(3) がそのファイルのポインタ表を添字。0x001 は sel4=low=1 で ENDMSX を鳴らした。 */
	const int gokudoPack = (!extraCode && initPc_ == 0x400
		&& mdataAddr_ == 0x4000 && mdataSize_ == 0x4000
		&& bgmPresent_[0] && bgmPresent_[1] && !bgmPresent_[2]) ? 1 : 0;
	/* nukenin: IN A,(4) ファイル／IN A,(3) トラックを CALL $084C へ。ys3 は init $3000 mdata $0300 — そのゲートを残す。 */
	const int nukeninPack = (extraCode && initPc_ == 0x3000
		&& mdataAddr_ == 0x4000 && mdataSize_ == 0x2600) ? 1 : 0;
	/* herzog: IN A,(4) は載せた MUS0n 内トラック。IN A,(5); CP 1 は SE。0x0009/0x0109 はファイル 9 を共有、mid がトラック。mid をポート 5 に置かない。 */
	const int herzogPack = (extraCode && initPc_ == 0x400
		&& mdataAddr_ == 0xC000 && mdataSize_ == 0x0C00) ? 1 : 0;
	/* mbsp: IN A,(4); CP 1 は SE $AA09 vs BGM $AA03。0x00.. は FMUSxx ファイル（A=0 は停止）。0x01xx は SE。サイズ $0A00 — playbal3 は $1000。 */
	const int mbspPack = (initPc_ == 0x400 && mdataAddr_ == 0xC200
		&& mdataSize_ == 0x0A00) ? 1 : 0;
	/* gulliver: IN A,(4); CP 1 が SE 表 $046D vs BGM $0457。MUSC.BIN は type=code。bgm rom は無い。 */
	const int gulliverPack = (extraCode && initPc_ == 0x400
		&& mem_[0x041E] == 0xDB && mem_[0x041F] == 0x04
		&& mem_[0x0420] == 0xFE && mem_[0x0421] == 0x01
		&& !bgmPresent_[0]) ? 1 : 0;
	/* dssp3 ran2/mzz: IN A,(3) トラック、IN A,(4); CP $65; CALL NZ LDIR $9800→$4000。ファイルは mid、トラックは low。song=low は 0x004 で BASAM 添字 4 を載せ、ファイル 0 のトラック 0x003/0x00f を逃した。 */
	const int compileCp65 = (initPc_ == 0x400
		&& mem_[0x0416] == 0xDB && mem_[0x0417] == 0x04
		&& mem_[0x0418] == 0xFE && mem_[0x0419] == 0x65) ? 1 : 0;
	/* ds13 Blaster Burn Gallery: IN A,(4); ADD A,A; LD HL,$0462 表。mid が 7 ワードバンク表を添字、low がトラック。song=low はすべての 0x..01 タイトルを表[1] に当て 0x302/0x304 を無音にした。 */
	const int compileTbl4 = (initPc_ == 0x400
		&& mem_[0x0419] == 0xDB && mem_[0x041A] == 0x04
		&& mem_[0x041B] == 0x87
		&& mem_[0x041F] == 0x21 && mem_[0x0420] == 0x62
		&& mem_[0x0421] == 0x04) ? 1 : 0;
	/* fuunroku: IN A,(4); CP 1 が MMLDATA @8000 vs MUSIC2 を選ぶ。0x000103 は sel4=low=3 で MUSIC2 表を取った。 */
	const int fuunrokuPack = (initPc_ == 0x400
		&& mem_[0x0452] == 0xDB && mem_[0x0453] == 0x03
		&& mem_[0x0483] == 0xDB && mem_[0x0484] == 0x04
		&& mem_[0x0485] == 0xFE && mem_[0x0486] == 0x01) ? 1 : 0;
	/* pup8 R-Police patch2: IN A,(4); CP 2 は DRIVER2（タイトル／SFX）。BGM 0x01–0x08
	   と 0x20 シャッフルは DRIVER3 @1000。0x21+ が SE。 */
	const int pup8Rp = (initPc_ == 0x400 && mdataAddr_ == 0x1000
		&& bgmPresent_[0] && bgmPresent_[1]
		&& mem_[0x0416] == 0xDB && mem_[0x0417] == 0x04
		&& mem_[0x0418] == 0xFE && mem_[0x0419] == 0x02
		&& mem_[0x041C] == 0xDB && mem_[0x041D] == 0x03) ? 1 : 0;
	/* ankoku: IN A,(3)*4 が $D048 レコード [file, skip, ptr] を添字。song=low は BATTLE 0x03 に STORY1 を載せた。Byte0 が bgm バンク。 */
	const int ankokuPack = (initPc_ == 0xD000 && mdataAddr_ == 0x0100
		&& mem_[0xD013] == 0xDB && mem_[0xD014] == 0x03
		&& mem_[0xD017] == 0x21 && mem_[0xD018] == 0x48
		&& mem_[0xD019] == 0xD0) ? 1 : 0;
	/* gshogi 0xMMTT: IN A,(3)→$D600 がトラック。ホストがファイル MM を載せる。song=low は 0x0203 を RMSC 扱いし MMSC 03/04/05 を無音にした。 */
	const int gshogiPack = (initPc_ == 0x400 && mdataAddr_ == 0xA000
		&& mdataSize_ == 0x3000
		&& mem_[0x0414] == 0xDB && mem_[0x0415] == 0x03
		&& mem_[0x0416] == 0x32 && mem_[0x0417] == 0x00
		&& mem_[0x0418] == 0xD6) ? 1 : 0;
	/* f1douchu DATA2/3/4 は 2 チャネル（02 AA）メニュージングル。Port7=1 は CALL $A306／$C040=0 を取り OPLL 経路が mute パッチをキーする（MON_THIN、peak=0）。PSG ゲームはそれらのファイルを鳴らす。 */
	const int f1douchuPsgJingle = (initPc_ == 0x400 && mdataAddr_ == 0xAA00
		&& mdataSize_ == 0x1600
		&& mem_[0x041C] == 0xC6 && mem_[0x041D] == 0x80
		&& mem_[0xA000] == 0xC3 && mem_[0xA001] == 0x0C
		&& mem_[0xA002] == 0xA1
		&& low >= 2u && low <= 4u) ? 1 : 0;
	/* Nemesis PSG: IN A,(4); CP 1 が SE $CD。3 バイト 0x0a01a6 は top=ファイル、mid=1、low=曲。`top { sel4=mid }` が毎エッジ SE→BGM になり 1 窓で mute。 */
	const int nemesisPsg = (initPc_ == 0x400 && mdataAddr_ == 0xBC00
		&& mem_[0x0416] == 0xDB && mem_[0x0417] == 0x04
		&& mem_[0x0418] == 0xFE && mem_[0x0419] == 0x01
		&& mem_[0x042A] == 0xCD && mem_[0x042B] == 0x00
		&& mem_[0x042C] == 0xAC) ? 1 : 0;
	/* wingsp OPLL: IN A,(4); CP 1/2 は再生経路（0=その場 $841F、1=LDIR $C000、2=OPEN $C062）。
	   タイトル 0x0002 BIG BOSS が sel4=low=2 で OPEN 経路に入り FMBGBS が壊れた。 */
	const int wingspPack = (initPc_ == 0x400 && mdataAddr_ == 0x83F9
		&& mem_[0x041D] == 0xDB && mem_[0x041E] == 0x04
		&& mem_[0x0422] == 0xFE && mem_[0x0423] == 0x01
		&& mem_[0x0426] == 0xFE && mem_[0x0427] == 0x02) ? 1 : 0;
	/* youmakrn: IN A,(5)/IN A,(4) が HL 再生番地（C012）。0x900002 は sel4=low=2 で HL=$9002 になりヘッダを飛ばす。 */
	const int youmakrnPack = (initPc_ == 0x2000 && mdataAddr_ == 0x9000
		&& mem_[0x201F] == 0xDB && mem_[0x2020] == 0x05
		&& mem_[0x2021] == 0x67
		&& mem_[0x2022] == 0xDB && mem_[0x2023] == 0x04
		&& mem_[0x2024] == 0x6F) ? 1 : 0;
	/* dante2: タイトル 0x04=MUSIC 00=MUS00。song=low は 0x07 に MUS07 を載せ port4=7 で
	   1 曲ファイルをはみ出す。MUS03 自体は count=0 の stub ではなく、ファイル番号がずれている。 */
	const int dante2Pack = (initPc_ == 0x400 && mdataAddr_ == 0xB700
		&& mem_[0x0422] == 0x21 && mem_[0x0423] == 0x00 && mem_[0x0424] == 0xB7
		&& mem_[0x0426] == 0x3A && mem_[0x0427] == 0x02 && mem_[0x0428] == 0xB7) ? 1 : 0;
	/* lastarmg: IN A,(3) が $80AF（PSG 10B）/$80B1（OPLL 26B）のタイトル添字。Byte0 が bgm ファイル。
	   song=low は 0x06 コマンドII に 126 を載せ、表の file=5（0B5 @42CA/43FC）を逃した。 */
	const int lastarmgPack = (initPc_ == 0x8000 && mdataAddr_ == 0x4000
		&& ((mem_[0x8023] == 0x11 && mem_[0x8024] == 0xAF && mem_[0x8025] == 0x80)
			|| (mem_[0x8025] == 0x11 && mem_[0x8026] == 0xB1 && mem_[0x8027] == 0x80))) ? 1 : 0;
	/* yakyufan: PATCH IN A,(4)=L IN A,(5)=H が HL=title>>8。0x98BD03 は END2 @6000+38BD。
	   `top { sel4=mid }` と同じ値だが play 側 else が song で潰さないよう明示する。 */
	const int yakyufanPack = (initPc_ == 0x400 && mdataAddr_ == 0x6000 && mdataSize_ == 0x4000
		&& mem_[0x0442] == 0xDB && mem_[0x0443] == 0x04 && mem_[0x0444] == 0x6F
		&& mem_[0x0445] == 0xDB && mem_[0x0446] == 0x05 && mem_[0x0447] == 0x67) ? 1 : 0;
	/* mmabtl: IN A,(4); LD HL,$7000; CALL $4806。HMD2 @C000。タイトル 0xTTFF は
	   ファイル=low、ファイル内トラック=mid。n00>=3 の fileInMid が 0x0200/0x0300 を
	   MUSIC3/4 と見なし、MUSIC5–9（0x0004–08）は MUSIC1 のトラック 4+（FC 03 に無い）へ
	   添字して無音にした。 */
	const int mmabtlPack = (initPc_ == 0x400 && mdataAddr_ == 0x7000 && mdataSize_ == 0x1000
		&& mem_[0x041D] == 0xCD && mem_[0x041E] == 0x06 && mem_[0x041F] == 0x48
		&& mem_[0xC000] == 0xC3 && mem_[0xC001] == 0x3D && mem_[0xC002] == 0xC0) ? 1 : 0;
	/* ishido: IN A,(4); CP 1 が SE $714E vs BGM $7148（A=0 で載せたファイルを再生）。
	   0x0001 Maple は sel4=song=1 で SE 経路に入り 2/C1.M が無音。JR Z,$0F は
	   port4Se の床 $13 より下。0x01xx だけ port4=1。 */
	const int ishidoPack = (initPc_ == 0x400 && mdataAddr_ == 0xAEBB
		&& mem_[0x0413] == 0xDB && mem_[0x0414] == 0x04
		&& mem_[0x0415] == 0xFE && mem_[0x0416] == 0x01
		&& mem_[0x0423] == 0xCD && mem_[0x0424] == 0x48 && mem_[0x0425] == 0x71) ? 1 : 0;
	/* nyancle: IN A,(3) が $5014 のトラック、IN A,(4) はファイル。PATCH が
	   OUT (3),file するので 2 回目 port2 エッジは file をトラックにし、
	   PSGDRV $8959 が $8C4C の 6 バイト枠 9+ を添字して無音。song=mid、エッジ 1。 */
	const int nyanclePack = (initPc_ == 0x400 && mdataAddr_ == 0x8C00
		&& mem_[0x041B] == 0xCD && mem_[0x041C] == 0x1B && mem_[0x041D] == 0x44
		&& mem_[0x042F] == 0xCD && mem_[0x0430] == 0x14 && mem_[0x0431] == 0x50) ? 1 : 0;
	/* hydefos: IN A,(4); CP 1 が SE $180C。BGM は C=port4 E=0 CALL $1806。
	   $1821 は C=0 で RET Z。0xFFxx は mid=0xFF なので C=$FF（全ch）。
	   lowFilePack が 0x0112 を sel4=0x11 にし SOUND15/17 が無音。C=$FF に固定。 */
	const int hydefosPack = (initPc_ == 0x400 && mdataAddr_ == 0x3000
		&& mem_[0x040B] == 0x21 && mem_[0x040C] == 0x00 && mem_[0x040D] == 0x18
		&& mem_[0x0431] == 0xCD && mem_[0x0432] == 0x06 && mem_[0x0433] == 0x18) ? 1 : 0;
	/* firehawk 0xFF: IN A,(3); CP FF は TO BOSS フラグ（$1B0D）だけで CALL $1B03 しない。
	   先に TMUS1M を再生し、2 回目 port2 エッジで port3=FF を立ててボス変化を載せる。 */
	const int firehawkPack = (initPc_ == 0x400
		&& mem_[0x041C] == 0xDB && mem_[0x041D] == 0x03
		&& mem_[0x041E] == 0xFE && mem_[0x041F] == 0xFF
		&& mem_[0x1B00] == 0xC3 && mem_[0x1B03] == 0xC3) ? 1 : 0;

	if (nemesisPsg) {
		/* Beginning 0x0001ac は top=0。表 $2C は GRAmd $B3F7。$BC00 に GRAm0 が要る（空だと無音）。 */
		if (top && top < BGM_BANKS && bgmPresent_[top])
			song = top;
		else if (mid && mid < BGM_BANKS && bgmPresent_[mid])
			song = mid;
		else
			song = 0;
		sel3 = low;
		sel4 = 0;
	} else if (compileTbl4) {
		song = 0;
		sel3 = low;
		sel4 = mid;
	} else if (compileCp65) {
		song = (mid < BGM_BANKS && bgmPresent_[mid]) ? mid : 0;
		sel3 = low;
		sel4 = mid;
	} else if (fuunrokuPack) {
		song = 0;
		sel3 = low;
		sel4 = mid;
	} else if (pup8Rp) {
		if (mid == 2 || low >= 0x21u) {
			song = 0;
			sel3 = low;
			sel4 = 2;
		} else {
			song = bgmPresent_[1] ? 1 : 0;
			sel3 = low;
			sel4 = 0;
		}
	} else if (ankokuPack) {
		unsigned file = 0;
		if (low < 64u) {
			const unsigned rec = 0xD048u + low * 4u;
			file = mem_[rec];
		}
		song = (file < BGM_BANKS && bgmPresent_[file]) ? file : 0;
		sel3 = low;
		sel4 = 0;
	} else if (gshogiPack) {
		song = (mid < BGM_BANKS && bgmPresent_[mid]) ? mid : 0;
		sel3 = low;
		sel4 = low;
	} else if (hydefosPack) {
		/* lowFilePack より前。0xFFxx は mid=0xFF で C=$FF（全ch）。
		   0x0112 を lowFilePack に取られると sel4=0x11 で SOUND15 が無音。
		   $1821 C=0 は RET Z。 */
		song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
		sel3 = low;
		sel4 = 0xFF;
	} else if (firehawkPack && low == 0xFFu) {
		song = 0;
		sel3 = 0;
		sel4 = 0;
	} else if (lowFilePack && low < BGM_BANKS && bgmPresent_[low]) {
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (compilePtr) {
		song = (mid < BGM_BANKS && bgmPresent_[mid]) ? mid : 0;
		sel3 = low;
		sel4 = mid;
	} else if (mmabtlPack) {
		song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
		sel3 = mid;
		sel4 = mid;
	} else if (nyanclePack) {
		song = (mid < BGM_BANKS && bgmPresent_[mid]) ? mid : low;
		sel3 = low;
		sel4 = mid;
	} else if (ishidoPack) {
		song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
		sel3 = low;
		sel4 = (mid == 1) ? 1 : 0;
	} else if (addrBoxEarly) {
		song = low;
		sel3 = low;
		sel4 = low;
	} else if (port4Se) {
		song = low;
		sel3 = low;
		sel4 = 0;
	} else if (ds00Data) {
		/* DATA2 @7228 は LD HL,nn を最初のポインタ $729B へ再配置。その表は本物の曲 2 つだけ（A=0 $7451、A=1 $7468）。0x0102..04 は余りヘッダバイトへ A=2+ を添字し、無音か $03 で RAM を壊した。 */
		song = low;
		sel3 = (mid && low >= 2u) ? 0 : low;
		sel4 = (mid == 0) ? 1 : 0;
	} else if (port4Inc) {
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (gokudoPack) {
		song = (mid == 1 && bgmPresent_[1]) ? 1 : 0;
		sel3 = low;
		sel4 = mid;
	} else if (nukeninPack) {
		song = (mid < BGM_BANKS && bgmPresent_[mid]) ? mid : 0;
		/* $084C A=$FF は停止。A が 10 バイトヘッダを添字。チャネルオフセットは $279E LDIR dest 相対 */
		sel3 = low;
		sel4 = mid;
	} else if (herzogPack) {
		if (top == 1) {
			/* SE 0x000100xx: IN A,(5); CP 1 のあと IN A,(3) CALL $B1B7。mid（常に 0）をポート 3 に置くとすべての SE が SAMESONG。 */
			song = 0;
			sel3 = low;
			sel4 = 0;
		} else {
			song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
			sel3 = mid;
			sel4 = mid;
		}
	} else if (gulliverPack) {
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (mbspPack) {
		if (mid == 1) {
			song = 0;
			sel3 = low;
			sel4 = 1;
		} else {
			song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
			/* $AA03 は IY+$18 経由で FMUS を添字。A ではない。A=0 は有効な最初の曲。A=1 は FMUS00 で空。 */
			sel3 = 0;
			sel4 = 0;
		}
	} else if (fileInMid && mid < BGM_BANKS && bgmPresent_[mid]) {
		song = mid;
		sel3 = low;
		sel4 = low;
	} else if (fileInMid && mid == 0 && bgmPresent_[0]) {
		song = 0;
		sel3 = low;
		sel4 = low;
	} else if (classInMid && mid == 1 && low < BGM_BANKS && bgmPresent_[low]) {
		/* 0x01xx BGM クラスのみ。1 バイトタイトルで A=mid を強制すると A=0 になり mbsp が無音（feedback は偶然自動再生）。 */
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (cmdInMid && mid >= 1 && mid <= 2 && bgmPresent_[low]) {
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (sameLowCmd && low < BGM_BANKS && bgmPresent_[low]) {
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (mdataAddr_ == 0xCEB1 && mid >= 1 && low < BGM_BANKS
		&& bgmPresent_[low]) {
		song = low;
		sel3 = mid;
		sel4 = mid;
	} else if (initPc_ == 0x3000 && mdataAddr_ == 0x0300) {
		/* ys3 0x15/0x1A: IN A,(4) は $1D00 のファイル内トラック、ファイルは low。Port4=song=0x15 は AF7MUS 内のトラックではない。 */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (initPc_ == 0x1000 && mdataAddr_ == 0x0300) {
		/* ys/ys2: port4=ファイル内トラック=mid、port5=エンジン=top、low=rom。
		   `top &&` 付きの下の枝は 0x000004 Y02MUS を落とした（sel4=low=4 で無音）。 */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (((initPc_ == 0x4D00 && mdataAddr_ == 0x6000)
		|| (initPc_ == 0x1000 && mdataAddr_ == 0x0300)
		|| mdataAddr_ == 0x8FF9) && top && top != 0xFF) {
		/* ys/ys2: port4 はファイル内トラック、port5 はエンジン、low は rom。0x010005 は sel4=low=5 をトラックとして無音のまま。daiva5 0x010003: IN A,(3) がファイル。IN A,(4) は MSX.BIN LDDR 後の CALL $ACCC へトラック。 */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (initPc_ == 0x400 && mdataAddr_ == 0x3500 && top && top != 0xFF) {
		/* Hertz psywrld 0x010000/0x010001: ファイルは low、トラックは mid。`top { sel4 = mid ? mid : low }` が VISUAL01 で port4=1。 */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (yakyufanPack) {
		/* 0x600001 → HL=$6000、0x98BD03 → 一旦 $98BD、page2 は後で $7000 へ写す。 */
		song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
		sel3 = low;
		sel4 = (titleCode >> 8) & 0xff;
	} else if (initPc_ == 0x400 && mdataAddr_ == 0xC200 && mdataSize_ == 0x1000
		&& top >= 1u && top <= 5u && top < BGM_BANKS && bgmPresent_[top]) {
		/* playbal3 0x010202: port4=2 が FM0 @8000 を LDIR。play $043C は port5 を FMn 表、port3 をトラック。ファイルは top、トラックは low、バンクは 2。song=low が誤 bgm を載せた。mid はゲーム内行すべてでたまたま 2。 */
		song = top;
		sel3 = low;
		sel4 = 2;
	} else if (wingspPack) {
		song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
		sel3 = low;
		sel4 = mid;
	} else if (youmakrnPack) {
		song = (low < BGM_BANKS && bgmPresent_[low]) ? low : 0;
		sel3 = low;
		sel4 = mid;
	} else if (dante2Pack) {
		const unsigned file = (low >= 4u) ? (low - 4u) : low;
		song = (file < BGM_BANKS && bgmPresent_[file]) ? file : 0;
		sel3 = song;
		sel4 = 0;
	} else if (lastarmgPack) {
		unsigned table = 0x80AF;
		unsigned recSz = 10;
		if (mem_[0x8025] == 0x11 && mem_[0x8026] == 0xB1 && mem_[0x8027] == 0x80) {
			table = 0x80B1;
			recSz = 26;
		}
		unsigned file = low;
		if (low < 64u) {
			const unsigned rec = table + low * recSz;
			if (rec < 0x10000u)
				file = mem_[rec];
		}
		song = (file < BGM_BANKS && bgmPresent_[file]) ? file : low;
		sel3 = low;
		sel4 = low;
	} else if (port56Hl) {
		/* tantexr 0x1C0B0002: IN A,(4) はトラック=mid。`top { sel4=low }` が mid=0 のときファイル番号を渡した。 */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (top == 0xFF) {
		/* lenam 0xFF000A: Hertz BGMDRV CALL $0B06 does LD A,C; OR A;
		   JP Z skip. C comes from IN A,(5). Port5=0 was a hard stop.
		   File is low, track in mid (0 is タイトル). */
		song = low;
		sel3 = low;
		sel4 = mid;
	} else if (top) {
		sel4 = mid ? mid : low;
	} else if (mid && !bgmPresent_[low] && bgmPresent_[mid]) {
		/* aleste2 0x0115: 中間バイトが bgm rom、low がその中の曲。gshogi 0x0501 は 0x01 が有効 rom でもあるので low をセレクタに保つ — 中間ファイルを載せると 0x0201 が無音。 */
		song = mid;
	}

	/* パッチがまだ要るコードイメージに mdata が重なるときは init settle 後まで StageBgm を遅らせる（Tokuma FMPAC @A000。Telenet alba2/valis2 TSTI/IPL89 @4000 で mdata_addr=4000 — 早期 StageBgm が LDIR ソースをゼロにし CALL AC06 を NOP sled に残した → pc≈F91F）。ys3: mdata $0300+$0E00 が EFCDAT @0B00 に重なる。$4000–$E000 窓もすべて遅らせる: gshogi mdata @A000 は init 中ワークスペースで、先に曲を載せると両選びが無音。 */
	int deferBgm = (mdataAddr_ >= 0x4000 && mdataAddr_ < 0xE000);
	if (!deferBgm && ge_ && mdataSize_ > 0) {
		const unsigned m0 = mdataAddr_;
		const unsigned m1 = mdataAddr_ + mdataSize_;
		for (int i = 0; i < ge_->romCount; i++) {
			if (_stricmp(ge_->rom[i].type, "code") != 0
				&& _stricmp(ge_->rom[i].type, "fmbios") != 0)
				continue;
			const unsigned off = (unsigned)(ge_->rom[i].offset < 0 ? 0 : ge_->rom[i].offset);
			if (off >= m0 && off < m1) { deferBgm = 1; break; }
		}
	}

	if (!deferBgm) {
		if (song < BGM_BANKS && bgmPresent_[song])
			StageBgm(song);
		else if (!nemesisPsg) {
			for (unsigned i = 0; i < BGM_BANKS; i++) {
				if (bgmPresent_[i]) { StageBgm(i); break; }
			}
		}
	}

	/* hoot MSX パッチが使うメールボックス:
	   - BirdySoft/Compile: port2=play、port4=song（Compile は 4→3 コピー）
	   - Enix/Falcom 風: port2=play、port3=song（angelus/can3/jngolf）
	   - Compile/jngolf: port7 bit0 = OPLL あり
	   - ys2/arcus: port5 = エンジン／バンク選択（コードの上位バイト） */
	ioport_[0x00] = (uint8_t)(song & 0xff);
	ioport_[0x02] = 0x01; /* 再生コマンド（settle 後 playCmdPending 経由で見える） */
	ioport_[0x03] = (uint8_t)(sel3 & 0xff);
	ioport_[0x04] = (uint8_t)(sel4 & 0xff);
	ioport_[0x05] = (top == 0xFF) ? 0 : (uint8_t)(top & 0xff);
	if (yakyufanPack) {
		ioport_[0x04] = (uint8_t)((titleCode >> 8) & 0xff);
		ioport_[0x05] = (uint8_t)((titleCode >> 16) & 0xff);
	}
	ioport_[0x07] = (chips_ & CHIP_FMPAC) ? 0x01 : 0x00;
	if (f1douchuPsgJingle)
		ioport_[0x07] = 0;
	/* kubikiri OPLL: カタログが top=0 の BGM（他は 0x01xxxx）。port5=0 だと
	   ISR が ($048B)!=1 で CE02 を再武装せず無音。SE の 0x00xxxx は触らない。
	   0x000203=03:03、0x000032=46:01。sel4=$FF はトラック 0 になり別曲になる。 */
	if (port4Inc && mdataAddr_ == 0x8800 && top == 0
		&& ((mid == 2 && low == 3) || (mid == 0 && low == 0x32)))
		ioport_[0x05] = 1;
	if (firehawkPack && low == 0xFFu) {
		ioport_[0x03] = 0;
		ioport_[0x04] = 0;
		ioport_[0x06] = 0xB0;
	}
	/* KOEI genghis: PATCH IN A,(4)/IN A,(5) を HL として MMLDATA @8000 へ。番地は (titleCode>>8) であり、下位バイトの $80 刻み添字ではない。 */
	int anyBgm = 0;
	for (unsigned i = 0; i < BGM_BANKS; i++) {
		if (bgmPresent_[i]) { anyBgm = 1; break; }
	}
	const int koeiIn45 = (!anyBgm
		&& mem_[0x0432] == 0xDB && mem_[0x0433] == 0x04
		&& mem_[0x0434] == 0x6F && mem_[0x0435] == 0xDB
		&& mem_[0x0436] == 0x05 && mem_[0x0437] == 0x67);
	if (koeiIn45) {
		uint16_t hl = CEmuMsxKoeiMmlAddr(mem_, titleCode, low);
		ioport_[0x04] = (uint8_t)(hl & 0xff);
		ioport_[0x05] = (uint8_t)(hl >> 8);
		ioport_[0x06] = (uint8_t)(mid & 0xff);
		sel4 = ioport_[0x04];
	}
	/* saziri 0x44730002／tantexr 0x239d0000: IN A,(5)/IN A,(6) を HL として載せたファイルへ。上位ワードは mdata 内なら絶対番地、さもなくばオフセット。0x01xxxx エンジンバイトは飛ばす。 */
	const unsigned hiWord = hiWordEarly;
	const int addrBox = addrBoxEarly ? 1 : 0;
	if (addrBox) {
		uint16_t hl = (uint16_t)hiWord;
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	} else if (port56Hl && titleCode > 0xFFFFu) {
		uint16_t hl = (uint16_t)(titleCode >> 16);
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	}
	ioport_[PLAY_CODE_PORT] = (uint8_t)(sel3 & 0xff);
	playCmdPending_ = 0; /* settle 後だけ武装 */
	titleCode_ = sel4;

	/* MSX BIOS IRQ @0038 → CALL H.TIMI（FD9F）→ EI;RET。
	   init が走る前にトランポリンを植える。パッチは 2 派に分かれどちらも 0038 を自分で書く:
	     - jesus/ankoku/columns/... は C3＋独自 ISR 番地を格納。後から植えるとドライバハンドラが消え 60Hz が裸 RET を呼ぶ（音楽は初期化され進まない）。
	     - ys2/arcus は 0039 オペランドだけ格納し、0038 に既に C3 があると仮定するので先頭からバイトが要る。
	   どちらでも我々のオペランドは既定だけ。0038 を主張した側が勝ち、どちらも主張しないタイトルは FD9F 経由で遅い H.TIMI フックへ。本体は最初 inert（EI;RET）。Tokuma MSX-FAN パッチは最初の命令で FMPAC BIOS 411F に H.TIMI をフックするので、settle 中に呼ぶと未載せ曲の上で BIOS プレーヤが走り停止する。曲が載ってから生かす。 */
	if (mem_[0xFD9F] == 0x00)
		mem_[0xFD9F] = 0xC9;
	mem_[0x0038] = 0xC3;
	mem_[0x0039] = 0xE0;
	mem_[0x003A] = 0x00; /* 命令 JP 00E0 */
	mem_[0x00E0] = 0xFB;             /* 命令 EI */
	mem_[0x00E1] = 0xC9;             /* 命令 RET */

	/* ys2: init LDIR $2000→$B000 がエンジン 1 再生経路（port5=1）の CALL $D48B 前に TTLPRG を消す。エンジン 0 は $102E で MUSPRG を再コピーするので最初のオーバーレイ飛ばしは両エンジンに安全。再生経路は常に先に CALL $106A。$107D は 0 開始なのでその stop は $BFC3（MUSPRG）へ。TTLPRG がまだマップだとその呼び出しは毒でエンジン 1 が $D48B に届かない。初回 play stop は no-op。 */
	if (initPc_ == 0x1000 && mdataAddr_ == 0x0300
		&& mem_[0x1006] == 0x21 && mem_[0x1007] == 0x00 && mem_[0x1008] == 0x20
		&& mem_[0x1009] == 0x11 && mem_[0x100A] == 0x00 && mem_[0x100B] == 0xB0) {
		memset(mem_ + 0x1006, 0x00, 11);
		if (mem_[0x101C] == 0xCD && mem_[0x101D] == 0x6A && mem_[0x101E] == 0x10)
			memset(mem_ + 0x101C, 0x00, 3);
	}

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpu_->r.sp = 0xF380;
	cpu_->r.iff1 = 1;
	cpu_->r.im = 1;
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 1;
	sccAccessed_ = 0;
	sccMapped_ = 0;
	if (chipScc_) chipScc_->Reset();

	/* 短い settle。init が play エッジ前にハンドラを入れる */
	CEmuHardMsxSetActive(this);
	int guard = 0;
	while (guard++ < 200000) {
		uint8_t* m = cpu_->get_mem();
		if (m && m[cpu_->r.pc] == 0x76) {
			cpu_->irqDelay = 0;
			if (!cpu_->r.iff1) {
				cpu_->r.pc = (uint16_t)(cpu_->r.pc + 1);
				cpuCycles_ += 4;
				continue;
			}
			/* yosikon CALL $D400 は EI;HALT 待ち。settle に Render IRQ は無い */
			if (cpu_->r.im != 2 || !Ay_CpuIm2Interrupt(cpu_, 0xff))
				Ay_CpuIm1Interrupt(cpu_);
			cpuCycles_ += 16;
			continue;
		}
		const int cyc = Ay_CpuRunOne(cpu_);
		if (cyc <= 0) break;
		cpuCycles_ += (uint64_t)cyc;
		if (idle_) break;
	}
	if (deferBgm) {
		if (song < BGM_BANKS && bgmPresent_[song])
			StageBgm(song);
		else if (!nemesisPsg) {
			for (unsigned i = 0; i < BGM_BANKS; i++) {
				if (bgmPresent_[i]) { StageBgm(i); break; }
			}
		}
		if (mdataAddr_ < 0xC000u && mdataAddr_ + mdataSize_ > 0x8000u)
			memcpy(bankShadow_, mem_ + 0x8000, 0x4000);
	}
	/* wingsp LAST WING: FMPCH プレーヤ（CALL $856D HL=$8E17）を被せ、END 周期データを $8E27 へ。
	   PSG 行は先に PSGEND が載る。856D は $8E1B からあと 6 本をチャネルポインタとして読むので
	   PSGEND の $018F/$01F3…（BIOS）を踏み peak=0 になる。FMEND を先に載せて OPLL 行と同じ
	   $8F0E… を残す。 */
	if (wingspPack && song == 4) {
		const unsigned pch = (bgmPresent_[BGM_BANKS - 1] && bgmBank_[BGM_BANKS - 1]
			&& bgmBank_[BGM_BANKS - 1][0] == 0xFE) ? (unsigned)BGM_BANKS - 1u : 0u;
		unsigned dataBank = 4;
		if (bgmPresent_[BGM_BANKS - 2] && bgmBank_[BGM_BANKS - 2]
			&& bgmBankSize_[BGM_BANKS - 2])
			dataBank = (unsigned)BGM_BANKS - 2u;
		if (dataBank != song && bgmPresent_[dataBank])
			StageBgm(dataBank);
		if (bgmPresent_[pch] && bgmBank_[pch] && bgmBankSize_[pch]
			&& bgmBank_[pch][0] == 0xFE) {
			const unsigned table = 0x8E17;
			const unsigned stream = 0x8E27;
			unsigned playerLen = table - 0x83F9u;
			if (playerLen > mdataSize_)
				playerLen = mdataSize_;
			if (bgmBankSize_[pch] >= playerLen)
				memcpy(mem_ + mdataAddr_, bgmBank_[pch], playerLen);
			mem_[table] = 0x00;
			mem_[table + 1] = 0x00;
			mem_[table + 2] = (uint8_t)(stream & 0xff);
			mem_[table + 3] = (uint8_t)(stream >> 8);
			if (bgmPresent_[dataBank] && bgmBank_[dataBank] && bgmBankSize_[dataBank]) {
				const unsigned char* src = bgmBank_[dataBank];
				unsigned sz = bgmBankSize_[dataBank];
				if (src[0] == 0xFE && sz > 0xA1Eu) {
					src += 0xA1E;
					sz -= 0xA1E;
				} else if (src[0] == 0xFE && sz > 7u) {
					src += 7;
					sz -= 7;
				}
				unsigned n = sz;
				if (stream + n > (unsigned)mdataAddr_ + mdataSize_)
					n = (unsigned)mdataAddr_ + mdataSize_ - stream;
				if (n)
					memcpy(mem_ + stream, src, n);
			}
			chips_ |= CHIP_FMPAC;
			EnsureOpll(1);
			ioport_[0x07] = 1;
		}
	}
	/* dante2 MUS07 は 9ch FM のみ。PSG xml は use_opll 無しで port7=0、OPLL 経路を踏まず無音。
	   MUS02 は 9ch でも PSG チャネルが残る。FMPAC を武装して MUSIC 07 を鳴らす。 */
	if (dante2Pack) {
		chips_ |= CHIP_FMPAC;
		EnsureOpll(1);
		ioport_[0x07] = 1;
	}
	/* yakyufan END2 0x98BD03: 曲ヘッダが $98BD（page2）。TSTO の RDSLT/CALSLT が
	   その窓をスロット ROM として読み空になる。ヘッダを $7000 へ写して HL を付け替える。 */
	if (yakyufanPack) {
		const unsigned src = (titleCode >> 8) & 0xFFFFu;
		if (src >= 0x8000u && src < 0xC000u
			&& src >= mdataAddr_ && src < (unsigned)mdataAddr_ + mdataSize_) {
			unsigned n = (unsigned)mdataAddr_ + mdataSize_ - src;
			if (n > 0x800u) n = 0x800u;
			memmove(mem_ + 0x7000, mem_ + src, n);
		}
	}
	/* dssp3 ran2: LDIR 後プレーヤ CALL $410D が RST 30（F7 スロット番地）を H.TIMI へコピー。$0030 は BIOS RET なので ISR が走らない（ayW=0、0038/WRTPSG トランポリンが壊れる）。IPL JP $44F9/$48D8 を残す。RSLREG/EXTBIO は RET 必須 — CALL $0138 はさもなくば PATCH @0400 へ NOP スライド。 */
	if (compileCp65) {
		if (mem_[0x0138] == 0x00) {
			mem_[0x0138] = 0xAF;
			mem_[0x0139] = 0xC9;
			mem_[0xFCC1] = 0;
		}
		if (mem_[0xFFCA] == 0x00)
			mem_[0xFFCA] = 0xC9;
		const int ran2 = (mem_[0x0438] == 0x21 && mem_[0x0439] == 0xF9
			&& mem_[0x043A] == 0x44) ? 1 : 0;
		if (ran2) {
			/* play 前にホストがプレーヤをコピー。BRN2-2 は約 7K なので IPL LDIR $3E00 が $5CEC–$7E00（BSS＋追加チャネル）をゼロにする。バンク 0 末尾を切らず選択バンクを重ねる。IPL LDIR を飛ばし CALL $5278 は残す。 */
			if (bgmPresent_[0] && bgmBank_[0] && bgmBankSize_[0] > 0x107u) {
				unsigned n = bgmBankSize_[0] - 0x107u;
				if (n > 0x3E00u) n = 0x3E00u;
				memcpy(mem_ + 0x4000, bgmBank_[0] + 0x107u, n);
			}
			if (song != 0 && song < BGM_BANKS && bgmPresent_[song]
				&& bgmBank_[song] && bgmBankSize_[song] > 0x107u) {
				unsigned n = bgmBankSize_[song] - 0x107u;
				if (n > 0x3E00u) n = 0x3E00u;
				memcpy(mem_ + 0x4000, bgmBank_[song] + 0x107u, n);
			}
			if (mem_[0x0442] == 0x21 && mem_[0x0443] == 0x00
				&& mem_[0x0444] == 0x98)
				memset(mem_ + 0x0442, 0x00, 11);
			/* $C0（bit6）は 9ch $C0 ポート経路。mzz は $80／$7C で PLAYS。同じプレーヤ、同じ BIOS stub。 */
			if (mem_[0x0458] == 0x3E && mem_[0x0459] == 0xC0)
				mem_[0x0459] = 0x80;
			for (unsigned a = 0x0100; a < 0x0400; a++) {
				if (mem_[a] == 0x00)
					mem_[a] = 0xC9;
			}
			/* パッチ隙間の WRTPSG 本体。CALL $0093 がスライドできるようにする */
			mem_[0x0466] = 0xD3; mem_[0x0467] = 0xA0;
			mem_[0x0468] = 0x7B;
			mem_[0x0469] = 0xD3; mem_[0x046A] = 0xA1;
			mem_[0x046B] = 0xC9;
			static const unsigned kWrt[] = { 0x492A, 0x4942, 0x4949, 0x4950 };
			for (unsigned i = 0; i < 4; i++) {
				const unsigned a = kWrt[i];
				if ((mem_[a] == 0xCD || mem_[a] == 0xC3)
					&& mem_[a + 1] == 0x93 && mem_[a + 2] == 0x00) {
					mem_[a + 1] = 0x66; mem_[a + 2] = 0x04;
				}
			}
		}
		/* 両 CALL $410D サイト（H.TIMI 上へ RST 30 インストール）。常駐プレーヤ（$4000）と載せたコピー（$9800）をパッチ。 */
		for (unsigned a = 0x4000; a + 2u < 0x5C00u; a++) {
			if (mem_[a] == 0xCD && mem_[a + 1] == 0x0D && mem_[a + 2] == 0x41)
				memset(mem_ + a, 0, 3);
		}
		for (unsigned a = 0x9800; a + 2u < 0x9C00u; a++) {
			if (mem_[a] == 0xCD && mem_[a + 1] == 0x0D && mem_[a + 2] == 0x41)
				memset(mem_ + a, 0, 3);
		}
	}
	/* yosikon: スロット 0 の FMPAC ID 一致が $D50E に A=0 を格納。play/ISR は 0 を「見つからない」と見る。検出は成功している（OPLL @401C）。 */
	if (mem_[0xD400] == 0xC3 && mem_[0xD401] == 0x78 && mem_[0xD402] == 0xD5
		&& mem_[0xD50E] == 0)
		mem_[0xD50E] = 1;
	/* rona H.TIMI @ $049D は 6 レジスタを push して CALL $D2F0／RET。フレームが漏れ最初の vblank RET がプレーヤ外へ飛ぶ。 */
	if (mem_[0x041B] == 0xCD && mem_[0x041C] == 0x18 && mem_[0x041D] == 0xD0
		&& mem_[0x04AE] == 0xCD && mem_[0x04AF] == 0xF0 && mem_[0x04B0] == 0xD2
		&& mem_[0x04B1] == 0xC9) {
		mem_[0x04AE] = 0xC3; mem_[0x04AF] = 0xD5; mem_[0x04B0] = 0x04;
		static const uint8_t kRonaTimi[] = {
			0xCD, 0xF0, 0xD2,
			0xFD, 0xE1, 0xDD, 0xE1, 0xE1, 0xD1, 0xC1, 0xF1,
			0xFB, 0xC9
		};
		memcpy(mem_ + 0x04D5, kRonaTimi, sizeof kRonaTimi);
		/* Play HALT @ $0416 は CALL $D018 前に vblank 待ち。ISR RET が HALT に着地し D02F が走らない。 */
		if (mem_[0x0416] == 0x76 && mem_[0x0417] == 0xAF)
			mem_[0x0416] = 0x00;
		/* INIOPL LDIR トランポリンを IY へ。A=$88 の PUSH AF/POP IY が $00E0 をコピー。play 時の 2 回目 CALL が下で植える IM1 本体を消す。CALSLT を飛ばす — ROM WRTOPL @ $4110 で足りる。 */
		if (mem_[0xD02B] == 0xCD && mem_[0xD02C] == 0x1C && mem_[0xD02D] == 0x00) {
			mem_[0xD02B] = 0x00; mem_[0xD02C] = 0x00; mem_[0xD02D] = 0x00;
		}
	}
	/* ys2 タイトルエンジン（port5=1）: init LDIR MUSPRG→$B000 が TTLPRG を消す */
	if (ttlPrgBytes_ && ttlPrgAddr_ && (top == 1 || ioport_[0x05] == 1)) {
		unsigned n = ttlPrgBytes_;
		if ((unsigned)ttlPrgAddr_ + n > 0x10000u)
			n = 0x10000u - ttlPrgAddr_;
		if (n > sizeof(ttlPrg_)) n = sizeof(ttlPrg_);
		memcpy(mem_ + ttlPrgAddr_, ttlPrg_, n);
		/* TTLPRG ISR @D105 は PUSH AF/DE のあと JP $0000（BIOS チェイン）。$0000 は空なので最初の vblank が BDOS XOR A;RET に当たり IFF1 を落とし PulseVblankIrq が再発火しない。 */
		if (mem_[0xD105] == 0xF5 && mem_[0xD116] == 0xC3
			&& mem_[0xD117] == 0x00 && mem_[0xD118] == 0x00
			&& mem_[0x0000] == 0x00) {
			mem_[0x0000] = 0xD1; /* 命令 POP DE */
			mem_[0x0001] = 0xF1; /* 命令 POP AF */
			mem_[0x0002] = 0xFB; /* 命令 EI */
			mem_[0x0003] = 0xC9; /* 命令 RET */
		}
	}
	/* 曲は載せ済み: トランポリン本体が H.TIMI に届く。0038 自体は init 中に主張した側へ残す。play エッジ後にだけ H.TIMI をフックするタイトルは下の RET を上書きしてから tick 開始。 */
	if (mem_[0xFD9F] == 0x00)
		mem_[0xFD9F] = 0xC9;
	mem_[0x00E0] = 0xF5;             /* 命令 PUSH AF */
	mem_[0x00E1] = 0xCD; mem_[0x00E2] = 0x9F; mem_[0x00E3] = 0xFD; /* 命令 CALL FD9F */
	mem_[0x00E4] = 0xF1;             /* 命令 POP AF */
	mem_[0x00E5] = 0xFB;             /* 命令 EI */
	mem_[0x00E6] = 0xC9;             /* 命令 RET */
	/* GREAT PASTEL2: CALL $0020 is DCOMPR (HL vs DE). A RET stub keeps Z from
	   (D460)==0 so CALL Z $D404 rewinds every vblank — FF-rest openers
	   (kpastel MF.VRM) never key a note. Body sits after the IRQ trampoline. */
	if (initPc_ == 0x400 && mdataAddr_ == 0x2FFC
		&& mem_[0xCFF9] == 0xFE && mem_[0xD1FE] == 0xF3) {
		mem_[0x0020] = 0xC3;
		mem_[0x0021] = 0xE8;
		mem_[0x0022] = 0x00;
		mem_[0x00E8] = 0x7C; /* LD A,H */
		mem_[0x00E9] = 0x92; /* SUB D */
		mem_[0x00EA] = 0xC0; /* RET NZ */
		mem_[0x00EB] = 0x7D; /* LD A,L */
		mem_[0x00EC] = 0x93; /* SUB E */
		mem_[0x00ED] = 0xC9;
	}
	/* Warp & Warp ISR CALL $0141（SNSMAT）。後で 0100-03FF RET 埋めが走るとロード時植込が消え、単独 C9 が行 ID を返すのでミキサ AND が 0。空または RET のみ — Compile @0100 ではない。 */
	if ((mem_[0x0141] == 0x00 || mem_[0x0141] == 0xC9)
		&& (mem_[0x0142] == 0x00 || mem_[0x0142] == 0xC9)
		&& (mem_[0x0143] == 0x00 || mem_[0x0143] == 0xC9)) {
		mem_[0x0141] = 0x3E; /* 命令 LD A,$FF */
		mem_[0x0142] = 0xFF;
		mem_[0x0143] = 0xC9;
	}
	/* ultima4 MUSICMSX: play は ($0105) を CE48 へ格納。ISR は 0 なら飛ばす。Compile DRIVER.BIN は @0100 なのでここだけ空。 */
	if (mdataAddr_ == 0xCEB1 && mem_[0xCA80] == 0x18 && mem_[0x0105] == 0)
		mem_[0x0105] = 1;
	/* tantexr PSGDRV: BIOS/ISR が $0100-$03FF NOP sled へ落ち（pc≈01DC）、play LDIR に届かない。ドライバは @D000、パッチ @0400、LDIR dest は $1000 — この窓は空。 */
	if (mdataAddr_ == 0x8000 && mem_[0xD000] == 0xC3
		&& mem_[0xD001] == 0x12 && mem_[0xD002] == 0xD0
		&& mem_[0x0400] == 0xF3) {
		for (unsigned a = 0x0100; a < 0x0400; a++) {
			if (mem_[a] == 0x00)
				mem_[a] = 0xC9;
		}
	}
	/* dssp1: zip は ran/DRIVER.BIN（ORG $4000）を載せるが XML は $3EF9 に BSRAND.OBJ。CALL $5921 がポインタ表に当たり、play ラッパの余分 POP が SP を壊す（$3F5B／pc $F12A）。16K イメージを滑らせ $5921 を曲添字にし、パッチ CALL $589C／$579A／$4AEA が本物コードに着地。パッチ番地に 0x107 を足さない — それらの $58xx/$4AEA は既に $4000 入口。 */
	if (mem_[0x0441] == 0xCD && mem_[0x0442] == 0x9A && mem_[0x0443] == 0x57
		&& mem_[0x579A] == 0xF3 && mem_[0x579B] == 0xCD
		&& mem_[0x579C] == 0x21 && mem_[0x579D] == 0x59
		&& mem_[0x5921] == 0x81) {
		uint8_t tmp[0x4000];
		memcpy(tmp, mem_ + 0x3EF9, 0x4000);
		memset(mem_ + 0x3EF9, 0, 0x107);
		memcpy(mem_ + 0x4000, tmp, 0x4000);
	}
	/* gokudo TITLE.COM は $4000 に 12K。パッチ LDDR $7000→$7C00 が空 $7000 を通り、Z80 重なりが 0x001 でホストをクラッシュ。TITLE 経路だけ memmove でシフトを再現し LDDR を NOP。再 play エッジは OUT (3),file 後にポート 3 を再 IN — 後続エッジが常に TITLE トラック 0／ENDMSX トラック 1 を選ぶ。添字を残す。 */
	if (gokudoPack
		&& mem_[0x0424] == 0x21 && mem_[0x0425] == 0x00 && mem_[0x0426] == 0x70
		&& mem_[0x0427] == 0x11 && mem_[0x0428] == 0x00 && mem_[0x0429] == 0x7C
		&& mem_[0x042A] == 0x01 && mem_[0x042B] == 0x00 && mem_[0x042C] == 0x30
		&& mem_[0x042D] == 0xED && mem_[0x042E] == 0xB8) {
		if (mid != 1)
			memmove(mem_ + 0x4C01, mem_ + 0x4001, 0x3000);
		memset(mem_ + 0x0424, 0x00, 11);
		if (mem_[0x041E] == 0xD3 && mem_[0x041F] == 0x03)
			mem_[0x041E] = mem_[0x041F] = 0x00;
	}
	/* nukenin: PATCH JP $0131 → $305F が IN A,(4) ファイル／IN A,(3) トラックのあと LDIR $500 を $279E へ。$084C もその CALL 前にヘッダ添字で IN A,(3)。$305F OUT (3),file: 2 回目 play エッジが A=file で CALL $084C し 0x100 をトラック 1 へ切替。エッジは 1 回だけ。両 IN を LD A,imm に植えファイル／トラックがずれないようにする。$0180 の PATCH RET は長さ変換 — 戻す。$0196 RET（BDOS）は残す。$279E 即値は付け替えない。J/K トラック 1+: ch0/ch1 MML が $0416 で停滞し ISR がキーしない。独立したトラック 6 stub を 3 つ植え、ch1/ch2 がコマンド途中に着地しないようヘッダを付け替える。このトラックの休符先（さもなくば最早ノート）ボイスを +0 へ重ねる。ファイル 0/3 は既に 3ch で PLAYS。 */
	if (nukeninPack && mem_[0x305F] == 0xDB && mem_[0x3060] == 0x04) {
		const unsigned file = (song <= 3u) ? song : 0u;
		const unsigned track = (unsigned)(sel3 & 0xff);
		const unsigned tent = 0x309Au + file * 4u;
		mem_[0x0869] = mem_[tent];
		mem_[0x086A] = mem_[tent + 1];
		if (mem_[0x0180] == 0xC9)
			mem_[0x0180] = 0xCB;
		if (mem_[0x3014] == 0xDB && mem_[0x3015] == 0x03) {
			mem_[0x3014] = 0x3E;
			mem_[0x3015] = (uint8_t)track;
		}
		mem_[0x305F] = 0x3E;
		mem_[0x3060] = (uint8_t)file;
		if (mem_[0x3076] == 0xDB && mem_[0x3077] == 0x03) {
			mem_[0x3076] = 0x3E;
			mem_[0x3077] = (uint8_t)track;
		}
		if (file >= 1u && file <= 2u && track >= 1u) {
			static const unsigned kPtab[4] = { 0x30AAu, 0x30C8u, 0x30EAu, 0x3114u };
			static const unsigned kHdr[4] = { 0x07B6u, 0x3136u, 0x31E0u, 0x32B2u };
			const unsigned hdr = kHdr[file] + track * 10u;
			const unsigned src = Rd16(mem_ + kPtab[file] + track * 2u);
			const unsigned fill = Rd16(mem_ + kPtab[file]); /* トラック 0: first=0 */
			const unsigned w0 = Rd16(mem_ + hdr + 0u);
			const unsigned w1 = Rd16(mem_ + hdr + 2u);
			const unsigned w3 = Rd16(mem_ + hdr + 6u);
			if (src >= 0x4000u && fill >= 0x4000u
				&& src + 0x100u < 0x10000u && fill + 0x40u < 0x10000u
				&& src != fill) {
				unsigned off = 0, best = 0xFFFFu, have = 0;
				const unsigned cand[3] = { 0u, w0, w1 };
				unsigned i;
				int f6 = 0;
				unsigned r1 = 0xFFFFu;
				if (src + w1 < 0x10000u) {
					unsigned lim = (w3 > w1) ? (w3 - w1) : 0x40u;
					r1 = NukeninRestUntilNote(mem_ + src + w1, lim, &f6);
				}
				if (r1 != 0u && r1 != 0xFFFFu && !f6 && r1 < 0xC0u) {
					off = w1;
					have = 1;
				} else {
					for (i = 0; i < 3u; i++) {
						const unsigned o = cand[i];
						unsigned lim, rest;
						if (src + o >= 0x10000u)
							continue;
						lim = (w3 > o) ? (w3 - o) : 0x40u;
						if (lim < 8u)
							continue;
						f6 = 0;
						rest = NukeninRestUntilNote(mem_ + src + o, lim, &f6);
						if (rest == 0xFFFFu || f6)
							continue;
						if (rest == 0u)
							rest = 0x800u;
						if (rest >= 0xC0u)
							continue;
						if (rest < best) {
							best = rest;
							off = o;
							have = 1;
						}
					}
				}
				{
					uint8_t stub[0x40];
					uint8_t tmp[0x70];
					unsigned n = 0;
					if (have) {
						n = (w3 > off) ? (w3 - off) : 0x20u;
						if (n > 0x70u) n = 0x70u;
						if (src + off + n > 0x10000u)
							n = 0x10000u - (src + off);
						if (n >= 8u)
							memcpy(tmp, mem_ + src + off, n);
						else
							n = 0;
					}
					memcpy(stub, mem_ + fill, 0x40);
					memcpy(mem_ + src, stub, 0x40);
					memcpy(mem_ + src + 0x80u, stub, 0x40);
					memcpy(mem_ + src + 0xC0u, stub, 0x40);
					if (n)
						memcpy(mem_ + src, tmp, n);
					Wr16(mem_ + hdr + 0u, 0x0080);
					Wr16(mem_ + hdr + 2u, 0x00C0);
					Wr16(mem_ + hdr + 6u, 0x0100);
				}
			}
		}
	}
	/* mbsp ISR $B907 CALSLT RSLREG $0138。パッチは CALSLT を JP (IX) に置換するので空 $0138 が PATCH @0400 へ NOP スライドし BGM が tick しない。SE $AA09 は $B907 を呼ばないのでクリックは既に動いていた。$0138 をグローバルに植えない — Compile DRIVER.BIN は @0100。 */
	if (mbspPack && mem_[0x0138] == 0x00) {
		mem_[0x0138] = 0xAF; /* 命令 XOR A */
		mem_[0x0139] = 0xC9; /* RET — プライマリスロット 0 */
		mem_[0xFCC1] = 0;
	}
	/* herzog ISR $BB13 CALL $00D8。$00D5 は既に RET なので CALL $00D5 は安全だが $00D8 は空で $00E0 へ NOP スライド — $AE1A の mute クリック後にネスト H.TIMI／IFF1 オフ。 */
	if (herzogPack && mem_[0x00D8] == 0x00)
		mem_[0x00D8] = 0xC9;
	/* herzog/mbsp は H.TIMI を FD9F に植える。IM1 $0038 をそのハンドラへ壊さない — $00E0 CALL FD9F は既に生き、その EI を飛ばすと最初の vblank 後 IFF1 オフ（MON_DEAD クリックのみ）。 */
	/* Sky Jaguar SCC+: settle 後に再適用。SCC ミックス $4416 は D280 bit1 でゲート。残り mute をクリアし play $93 が RET C しないようにする。 */
	if (sccEnable_ && initPc_ == 0x400
		&& mem_[0x4009] == 0xC3 && mem_[0x400A] == 0x27
		&& mem_[0x400B] == 0x42) {
		mem_[0xD280] |= 0x02;
		mem_[0xD2BC] = (uint8_t)(low & 0x3Fu);
		mem_[0xD2BB] = 1;
		memset(mem_ + 0xE018, 0, 0x28);
		if (mem_[0x4251] == 0xBB && mem_[0x4252] == 0xD8) {
			mem_[0x4251] = 0x00;
			mem_[0x4252] = 0x00;
		}
		if (chipScc_)
			CEmuChipSccSetPlusMode(chipScc_, 1);
		playCmdPending_ = 1;
	}
	/* Nemesis SCC+: init が CALL $4912 を戻さないが、settle 後も $4006 を保つ。8 回 play エッジは曲を頭出しし直す。 */
	if (sccEnable_ && initPc_ == 0x400
		&& mem_[0x4000] == 0xC3 && mem_[0x4001] == 0xCF
		&& mem_[0x4002] == 0x60) {
		static const unsigned kNemesisCall4912Settle[] = { 0x041C, 0x0424, 0x042D };
		for (unsigned k = 0; k < 3u; k++) {
			const unsigned a = kNemesisCall4912Settle[k];
			if (mem_[a] == 0xCD && mem_[a + 1] == 0x12 && mem_[a + 2] == 0x49) {
				mem_[a + 1] = 0x06;
				mem_[a + 2] = 0x40;
			}
		}
		if (chipScc_)
			CEmuChipSccSetPlusMode(chipScc_, 1);
		playCmdPending_ = 1;
	}
	/* Super Cobra SCC+: 同じ mute スロット RET C。Play $8B CLEAR は init CALL $0430 の残り $D0 に対する CP (HL); RET C。 */
	if (sccEnable_ && initPc_ == 0x400
		&& mem_[0x4003] == 0xC3 && mem_[0x4004] == 0xC4 && mem_[0x4005] == 0x41
		&& mem_[0x41FA] == 0xBE && mem_[0x41FB] == 0xD8) {
		mem_[0x41FB] = 0x00;
		memset(mem_ + 0xE012, 0, 0x28);
		playCmdPending_ = 1;
	}
	/* ds13 OPLL blizzard/dangerous: $147A が 0 のときミキサ JP Z $1AF3 が AY を永久ダンプ（pc=1AFE、ayW=170k）。FM 経路を残す。 */
	if (compileTbl4 && (chips_ & CHIP_FMPAC)
		&& low == 3 && (mid == 5 || mid == 6)) {
		mem_[0x1493] = 0x80;
		mem_[0x147A] = 0x20;
		/* ミキサは FM ブロック後いつも JP $1AF3。その AY ダンプはレジスタ毎に EI し、この 2 パックでは $1AFE を離れない。 */
		if (mem_[0x1A0A] == 0xC3 && mem_[0x1A0B] == 0xF3 && mem_[0x1A0C] == 0x1A)
			mem_[0x1A0A] = 0xC9;
		if (mem_[0x1AFE] == 0xFB)
			mem_[0x1AFE] = 0x00;
	}
	/* dante OPLL PATCH: CPU を待ちループに残す。init が $0138 を消したら RSLREG を再植 */
	if (initPc_ == 0x400 && cpu_
		&& mem_[0x0413] == 0xCD && mem_[0x0414] == 0xE4 && mem_[0x0415] == 0xD2
		&& mem_[0x0416] == 0x76) {
		cpu_->r.pc = 0x040A;
		cpu_->r.sp = 0xF380;
		cpu_->r.iff1 = 1;
		cpu_->r.iff2 = 1;
		cpu_->r.im = 1;
		cpu_->irqDelay = 0;
		if (mem_[0x0138] == 0x00) {
			mem_[0x0138] = 0xAF;
			mem_[0x0139] = 0xC9;
			mem_[0xFCC1] = 0;
		}
		if (mem_[0x4018] == 'P' && mem_[0x4019] == 'A' && mem_[0x401C] == 'O'
			&& mem_[0x42BE] == 'A' && mem_[0x42BF] == 'P' && mem_[0x42C2] == 'O') {
			memcpy(mem_ + 0x42BE, mem_ + 0x4018, 8);
		}
	}
	/* laplace PSG PATCH2: 待ちループ PC を残す。H.TIMI JP $77C6 */
	if (initPc_ == 0x400 && cpu_ && IsLaplacePsgPatch(mem_)) {
		PlantLaplacePsgPlay(mem_);
		cpu_->r.pc = 0x0448; /* play 後 HALT。$7043 へ再入しない */
		cpu_->r.sp = 0xF380;
		cpu_->r.iff1 = 1;
		cpu_->r.iff2 = 1;
		cpu_->r.im = 1;
		cpu_->irqDelay = 0;
		/* $4BC3 は $4BE7→H.TIMI をコピー。$BD80（RETI ラッパ／RST 30）ではなく PSG tick へ向ける。待ちループ CALL $0492（$4B9A RETI）を飛ばす。 */
		if (mem_[0x4BE7] == 0xC3 && mem_[0x4BE8] == 0x80 && mem_[0x4BE9] == 0xBD) {
			mem_[0x4BE8] = 0xC6;
			mem_[0x4BE9] = 0x77;
		}
		if (mem_[0x040D] == 0xCD && mem_[0x040E] == 0x92 && mem_[0x040F] == 0x04) {
			mem_[0x040D] = 0x00;
			mem_[0x040E] = 0x00;
			mem_[0x040F] = 0x00;
		}
		mem_[0xFD9F] = 0xC3;
		mem_[0xFDA0] = 0xC6;
		mem_[0xFDA1] = 0x77;
	}
	/* dante PSG PATCH2: play は IN A,(2)/IN A,(3) のあと CALL $DA43 */
	if (initPc_ == 0x400 && cpu_
		&& mem_[0x043E] == 0xCD && mem_[0x043F] == 0x43 && mem_[0x0440] == 0xDA
		&& mem_[0x0415] == 0xDB && mem_[0x0416] == 0x03) {
		mem_[0xFD9F] = 0xC9;
		mem_[0xDC8A] = 0xC9;
		cpu_->r.pc = 0x040A;
		cpu_->r.sp = 0xF380;
		cpu_->r.iff1 = 1;
		cpu_->r.iff2 = 1;
		cpu_->r.im = 1;
		cpu_->irqDelay = 0;
	}
	/* playbal3 ゲーム内: バンク 2 が FM0 $8000→$AB80 を LDIR したあと $C000–$DFFF（BSS）をゼロし $C200 の FMn stub を消す。タイトルはその埋めが要る。ゲーム内は載せたファイルを $1000 へコピーし wipe 後 $C200 を戻す。 */
	if (initPc_ == 0x400 && mdataAddr_ == 0xC200 && mdataSize_ == 0x1000
		&& top >= 1u && top <= 5u
		&& mem_[0x0407] == 0xDB && mem_[0x0408] == 0x02
		&& mem_[0x04B6] == 0x21 && mem_[0x04B7] == 0x00 && mem_[0x04B8] == 0xC0
		&& mem_[0x04BC] == 0x01 && mem_[0x04BD] == 0x00 && mem_[0x04BE] == 0x20) {
		memcpy(mem_ + 0x1000, mem_ + 0xC200, 0x1000);
		static const uint8_t kPlaybal3Cave[] = {
			0xAF,                   /* 命令 xor a */
			0x21, 0x00, 0xC0,       /* 命令 ld hl,$C000 */
			0x11, 0x01, 0xC0,       /* 命令 ld de,$C001 */
			0x01, 0x00, 0x20,       /* 命令 ld bc,$2000 */
			0x77,                   /* 命令 ld (hl),a */
			0xED, 0xB0,             /* 命令 ldir */
			0x21, 0x00, 0x10,       /* 命令 ld hl,$1000 */
			0x11, 0x00, 0xC2,       /* 命令 ld de,$C200 */
			0x01, 0x00, 0x10,       /* 命令 ld bc,$1000 */
			0xED, 0xB0,             /* 命令 ldir */
			0xFB,                   /* 命令 ei */
			0xC9                    /* 命令 ret */
		};
		memcpy(mem_ + 0x0600, kPlaybal3Cave, sizeof(kPlaybal3Cave));
		mem_[0x04B5] = 0xC3;
		mem_[0x04B6] = 0x00;
		mem_[0x04B7] = 0x06; /* 命令 jp $0600 */
		if (mem_[0xC200] == 0xF3)
			mem_[0xC200] = 0x00;
		if (mem_[0x1000] == 0xF3)
			mem_[0x1000] = 0x00;
		/* トラック 0 stub は DI。待ちループ JR Z $0407 が EI を飛ばす */
		if (mem_[0x042A] == 0x18 && mem_[0x042B] == 0xDB)
			mem_[0x042B] = 0xDA; /* 命令 jr $0406 */
		mem_[0x0038] = 0xC3;
		mem_[0x0039] = 0xE0;
		mem_[0x003A] = 0x00;
		PlantPsgTrampoline(mem_);
	}
	/* sdgundam PSG: ISR IN A,($99); JP P が $20DA を飛ばす（status=0）ので $2104 のあと $217D が進まない（seq=1 STOPS）。毎 IM1 で tick し、ISR pop 前の EI を飛ばす。 */
	if (initPc_ == 0x600 && mdataAddr_ == 0xCEEF
		&& mem_[0x060A] == 0xDB && mem_[0x060B] == 0x02
		&& mem_[0x0620] == 0x21 && mem_[0x0621] == 0xEF && mem_[0x0622] == 0xCE
		&& mem_[0x06DE] == 0xB7 && mem_[0x06DF] == 0xF2) {
		mem_[0x06DE] = 0xCD;
		mem_[0x06DF] = 0xDA;
		mem_[0x06E0] = 0x20;
		mem_[0x06E1] = 0x18;
		mem_[0x06E2] = 0x03;
		mem_[0x06E3] = 0x00;
		mem_[0x06E4] = 0x00;
		mem_[0x06E5] = 0x00;
	}

	/* ハンドラ存在後のワンショット play（port2/3/4 メールボックス） */
	playCmdPending_ = 8; /* エッジは少数。永久 sticky ではない */
	/* daiva5 MSX.BIN: play CALL $049D が LDDR $B74F→$BF4F。2 回目エッジが既再配置イメージをコピーし $ACCC を消す。すべての ED B8 パッチに適用しない: gokudo 0x01 は後続エッジで H.TIMI を植える必要がある（pending=1 が生きた選びを無音にした）。 */
	if (mdataAddr_ == 0x8FF9)
		playCmdPending_ = 1;
	/* lastarmg: play が OUT (3),file。2 回目エッジ IN A,(3) が file を曲番号として
	   再添字し、0x0B+ は 126 上の コマンドII オフセットで OPLL を mute、0x05–0x0A は
	   全部 コマンドになる。エッジ 1 回で足りる。 */
	if (lastarmgPack)
		playCmdPending_ = 1;
	if (nyanclePack)
		playCmdPending_ = 1;
	if (nukeninPack)
		playCmdPending_ = 1;
	if (initPc_ == 0x400 && mdataAddr_ == 0xC200 && mdataSize_ == 0x1000
		&& top >= 1u && top <= 5u
		&& mem_[0x04B5] == 0xC3 && mem_[0x04B6] == 0x00 && mem_[0x04B7] == 0x06)
		playCmdPending_ = 1;
	if (initPc_ == 0x600 && mdataAddr_ == 0xCEEF
		&& mem_[0x060A] == 0xDB && mem_[0x060B] == 0x02
		&& mem_[0x0620] == 0x21 && mem_[0x0621] == 0xEF && mem_[0x0622] == 0xCE)
		playCmdPending_ = 1;
	if (initPc_ == 0xD000 && mdataAddr_ == 0x0100
		&& mem_[0xD013] == 0xDB && mem_[0xD014] == 0x03
		&& mem_[0xD017] == 0x21 && mem_[0xD018] == 0x48 && mem_[0xD019] == 0xD0)
		playCmdPending_ = 1;
	/* dssp3 ran2/mzz: play LDIR OUT (3),file。2 回目エッジ IN A,(3) がそのファイルをトラックとして添字。コピー＋play は 1 回で足りる。 */
	if (compileCp65) {
		playCmdPending_ = 1;
		if (mem_[0x0440] == 0xD3 && mem_[0x0441] == 0x03)
			mem_[0x0440] = mem_[0x0441] = 0x00;
	}
	if (sccEnable_ && initPc_ == 0x400
		&& ((mem_[0x4009] == 0xC3 && mem_[0x400A] == 0x27 && mem_[0x400B] == 0x42)
			|| (mem_[0x4003] == 0xC3 && mem_[0x4004] == 0xC4 && mem_[0x4005] == 0x41)
			|| (mem_[0x4000] == 0xC3 && mem_[0x4001] == 0xCF && mem_[0x4002] == 0x60)))
		playCmdPending_ = 1;
	if (initPc_ == 0x400 && mdataAddr_ == 0xBC00
		&& mem_[0x042A] == 0xCD && mem_[0x042B] == 0x00 && mem_[0x042C] == 0xAC) {
		playCmdPending_ = 1;
	}
	/* dante OPLL: 余分な port2 エッジ毎に CALL $D2E4 が空 $D852 バックアップから H.TIMI を戻し（init $D37B は飛ばされた）CALSLT $4119 で mute。曲 LDIR と CALL $D2DE にはエッジ 1 回で足りる。 */
	if (initPc_ == 0x400
		&& mem_[0x0413] == 0xCD && mem_[0x0414] == 0xE4 && mem_[0x0415] == 0xD2
		&& mem_[0x0416] == 0x76)
		playCmdPending_ = 1;
	if (initPc_ == 0x400 && IsLaplacePsgPatch(mem_))
		playCmdPending_ = 0;
	/* crimson2/3 PSG: C=5 play は LD HL,(mdata); ADD mdata; LD (mdata),HL をその場で行う。2 回目メールボックスエッジが二重再配置し曲が死ぬ。OPLL（init 後 A11A/A11E=1）は CALSLT を使いエッジ 8 を残せる。 */
	if (initPc_ == 0x400 && mdataAddr_ >= 0xAC00 && mdataAddr_ < 0xAD00
		&& mem_[0xA100] == 0x0D && mem_[0xA11A] != 1 && mem_[0xA11E] != 1) {
		const uint8_t mdLo = (uint8_t)(mdataAddr_ & 0xff);
		const uint8_t mdHi = (uint8_t)(mdataAddr_ >> 8);
		for (unsigned a = 0xA100; a + 10u < 0xB000u; a++) {
			if (mem_[a] == 0x2A && mem_[a + 1] == mdLo && mem_[a + 2] == mdHi
				&& mem_[a + 3] == 0x11 && mem_[a + 4] == mdLo && mem_[a + 5] == mdHi
				&& mem_[a + 6] == 0x19 && mem_[a + 7] == 0x22
				&& mem_[a + 8] == mdLo && mem_[a + 9] == mdHi) {
				playCmdPending_ = 1;
				break;
			}
		}
	}
	ioport_[0x02] = 0x01;
	if (koeiIn45) {
		uint16_t hl = CEmuMsxKoeiMmlAddr(mem_, titleCode, low);
		ioport_[0x04] = (uint8_t)(hl & 0xff);
		ioport_[0x05] = (uint8_t)(hl >> 8);
		ioport_[0x06] = (uint8_t)(mid & 0xff);
		titleCode_ = ioport_[0x04];
	} else if (addrBox) {
		uint16_t hl = (uint16_t)hiWord;
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	} else if (port56Hl && titleCode > 0xFFFFu) {
		uint16_t hl = (uint16_t)(titleCode >> 16);
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = (uint8_t)(hl & 0xff);
		ioport_[0x06] = (uint8_t)(hl >> 8);
	} else if (yakyufanPack) {
		uint16_t hl = (uint16_t)((titleCode >> 8) & 0xFFFFu);
		if (hl >= 0x8000u)
			hl = 0x7000;
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(hl & 0xff);
		ioport_[0x05] = (uint8_t)(hl >> 8);
	} else if (lastarmgPack) {
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
	} else if (wingspPack) {
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
	} else if (top == 0xFF) {
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = 1; /* Hertz: C=0 は play を飛ばす */
	} else if (top) {
		ioport_[0x03] = (uint8_t)(sel3 & 0xff);
		ioport_[0x04] = (uint8_t)(sel4 & 0xff);
		ioport_[0x05] = (uint8_t)(top & 0xff);
	} else {
		ioport_[0x03] = (uint8_t)((compilePtr || compileCp65 || compileTbl4
			|| fuunrokuPack || pup8Rp
			|| gokudoPack || nukeninPack
			|| herzogPack || gulliverPack || mbspPack || ds00Data
			|| ankokuPack || gshogiPack || lastarmgPack || mmabtlPack
			|| nyanclePack || ishidoPack) ? sel3 : song);
		ioport_[0x04] = (uint8_t)((lowFilePack || fileInMid || classInMid
			|| cmdInMid || sameLowCmd || mdataAddr_ == 0xCEB1
			|| compilePtr || compileCp65 || compileTbl4
			|| fuunrokuPack || pup8Rp
			|| port4Se || port4Inc || ds00Data
			|| gokudoPack || nukeninPack || herzogPack || gulliverPack
			|| mbspPack || gshogiPack || mmabtlPack
			|| nyanclePack || ishidoPack
			|| (initPc_ == 0x3000 && mdataAddr_ == 0x0300)
			|| (initPc_ == 0x1000 && mdataAddr_ == 0x0300)) ? sel4 : song);
	}
	ioport_[0x07] = (chips_ & CHIP_FMPAC) ? 0x01 : 0x00;
	if (f1douchuPsgJingle)
		ioport_[0x07] = 0;
	if (dante2Pack)
		ioport_[0x07] = 1;
	idle_ = 0;
	return 1;
}

/* データを載せる */
int CHardMsx::LoadDs4(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!LoadCartRom(fs, ge, "code"))
		return 0;
	mapper_ = MAP_DS4;
	genericMode_ = 0;
	chips_ = 0;
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	memset(mem_, 0, sizeof(mem_));
	/* $4000 の固定 16K 窓＋Konami 8K ページ $8000/$A000 */
	{
		unsigned n = cartBytes_;
		if (n > 0x4000u) n = 0x4000u;
		memcpy(mem_ + 0x4000, cart_, n);
	}
	MapDs4(0, 2);
	MapDs4(1, 3);
	memcpy(mem_, kDs4Ipl, sizeof(kDs4Ipl));
	mem_[0x93] = 0xc3; mem_[0x94] = 0x02; mem_[0x95] = 0x11;
	mem_[0x96] = 0xc3; mem_[0x97] = 0x0e; mem_[0x98] = 0x11;
	mem_[0x1102] = 0xf3; mem_[0x1103] = 0xd3; mem_[0x1104] = 0xa0;
	mem_[0x1105] = 0xf5; mem_[0x1106] = 0x7b; mem_[0x1107] = 0xd3;
	mem_[0x1108] = 0xa1; mem_[0x1109] = 0xfb; mem_[0x110a] = 0xf1;
	mem_[0x110b] = 0xc9;
	mem_[0x110e] = 0xd3; mem_[0x110f] = 0xa0;
	mem_[0x1110] = 0xdb; mem_[0x1111] = 0xa2; mem_[0x1112] = 0xc9;
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 0;
	if (chipAy_) chipAy_->Reset();
	CEmuHardMsxSetActive(this);
	/* $48F2 LDIR が SFX ポインタ表 $7806→$E100 をコピー（BC=$059A、約 60k T-state）。hoot の 10000 サイクル warmup はその最後のコピーに短い。代わりに IPL poll（$0011）で止める。ポート 2 idle は無視 — IPL は Play までそこに居る。 */
	{
		int guard = 0;
		uint64_t cyc = 0;
		while (cyc < 200000u && guard++ < 2000000) {
			const uint16_t pc = cpu_->r.pc;
			if (pc >= 0x0011u && pc <= 0x0018u)
				break;
			const int c = Ay_CpuRunOne(cpu_);
			if (c <= 0) break;
			cyc += (uint64_t)c;
			cpuCycles_ += (uint64_t)c;
		}
	}
	idle_ = 0;
	mem_[0xc042] = 0x02;
	mem_[0xc043] = 0x03;
	mem_[0xc095] = 0xff;
	mem_[0xc098] = 0xbf;
	return 1;
}

/* データを載せる */
int CHardMsx::LoadAscii16(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	titleCode_ = titleCode;
	ge_ = ge;
	genericMode_ = 1;
	mapper_ = MAP_ASCII16;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	chips_ = CHIP_FMPAC;
	idle_ = 0;
	playing_ = 0;
	CEmuMsxMergeCompanions(fs, ge);
	if (!LoadCartRom(fs, ge, "rom"))
		return 0;
	MapAscii16(0, 0);
	MapAscii16(1, 1);
	int loadedCode = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "rom") == 0) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		int off = r->offset;
		if (off < 0) off = 0x400;
		if (off >= 0x10000) continue;
		unsigned n = sz;
		if (off + (int)n > 0x10000)
			n = (unsigned)(0x10000 - off);
		memcpy(mem_ + off, data, n);
		loadedCode++;
	}
	if (!loadedCode) {
		for (int i = 0; i < fs->fileCount; i++) {
			char pathA[CEMU_ZIP_PATH];
			WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
			if (_stricmp(pathA, "fmpatch") != 0 && _stricmp(pathA, "patch") != 0)
				continue;
			unsigned n = fs->files[i].size;
			if (n > 0x200) n = 0x200;
			memcpy(mem_ + 0x400, fs->files[i].data, n);
			loadedCode++;
			break;
		}
	}
	if (!loadedCode) return 0;
	PlantPsgTrampoline(mem_);
	PlantBiosStubs();
	initPc_ = (uint16_t)ParseOptHex(ge, "init_pc", 0x400);
	mdataAddr_ = (uint16_t)ParseOptHex(ge, "mdata_addr", 0xA400);
	mdataSize_ = 0x800;
	EnsureOpll(1);
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

/* データを載せる */
int CHardMsx::LoadDq(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	titleCode_ = titleCode;
	ge_ = ge;
	genericMode_ = 0;
	dqMode_ = 1;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	ayWriteCount_ = 0;
	opllWriteCount_ = 0;
	chips_ = 0;
	idle_ = 0;
	playing_ = 0;
	if (!LoadCartRom(fs, ge, "code"))
		return 0;
	/* 両カートは ASCII8。DQ2 のプレーヤ CALL $C428 は 6000/6800/7000/7800 を 4 つの 8K ページ（D, D+1, E, E+1）として書く。256K を ASCII16 扱いするとそれら対が 1 つの 16K バンクに潰れバンク曲データが無音。$0400 をトランポリンしない: CALL $0138 がそこへ NOP スライドしカート init へ再入してスタックが死んだ。 */
	mapper_ = MAP_ASCII8;
	MapAscii8(0, 0);
	MapAscii8(1, 1);
	MapAscii8(2, 2);
	MapAscii8(3, 3);
	PlantDqBios();
	initPc_ = 0x4010;
	if (mem_[0x4000] == 'A' && mem_[0x4001] == 'B') {
		const uint16_t init = (uint16_t)(mem_[0x4002] | ((uint16_t)mem_[0x4003] << 8));
		if (init >= 0x4000 && init < 0xC000)
			initPc_ = init;
	}
	mdataAddr_ = 0xC000;
	mdataSize_ = 0x800;
	sccEnable_ = 0;
	sccMapped_ = 0;
	sccAccessed_ = 0;
	cpu_->reset(mem_);
	cpuCycles_ = 0;
	if (chipAy_) chipAy_->Reset();
	return 1;
}

/* BIOS／糊をメモリへ植える */
void CHardMsx::PlantDqBios()
{
	/* DQ 専用 RET 埋め: ここに Compile DRIVER.BIN @ $0100 は無い */
	memset(mem_, 0xC9, 0x400);
	mem_[0x0004] = 0x00; /* MSX1 機種 */
	mem_[0x0006] = 0x98; /* VDP.DR ポート */
	mem_[0x0007] = 0x98; /* VDP.DW — DQ2 LD A,($0007); INC C → ポート $99 */
	PlantPsgTrampoline(mem_);
	/* RDSLT／WRSLT: RET 埋めだと ==0x00 植込が飛ばされる */
	mem_[0x000C] = 0xC3; mem_[0x000D] = 0xF0; mem_[0x000E] = 0x00;
	mem_[0x00F0] = 0x7E; mem_[0x00F1] = 0xC9;
	mem_[0x0014] = 0xC3; mem_[0x0015] = 0xF2; mem_[0x0016] = 0x00;
	mem_[0x00F2] = 0x73; mem_[0x00F3] = 0xC9;
	mem_[0x0138] = 0xAF; mem_[0x0139] = 0xC9; /* RSLREG: プライマリスロット 0 */
	mem_[0x0141] = 0x3E; mem_[0x0142] = 0x3F; mem_[0x0143] = 0xC9; /* SNSMAT キーマトリクス */
	mem_[0xFCC1] = 0; /* EXPTBL: 拡張無し */
	mem_[0x0038] = 0xFB; /* 音楽 ISR が植わるまで EI; RET */
	mem_[0x0039] = 0xC9;
}

/* BIOS／糊をメモリへ植える */
static void PlantDqMusicIsr(uint8_t* mem, uint16_t tick)
{
	static const uint8_t kIsr[] = {
		0xF5, 0xC5, 0xD5, 0xE5,
		0xDD, 0xE5, 0xFD, 0xE5,
		0xCD, 0x00, 0x00,
		0xFD, 0xE1, 0xDD, 0xE1,
		0xE1, 0xD1, 0xC1, 0xF1,
		0xFB, 0xC9
	};
	if (!mem) return;
	memcpy(mem + 0x0100, kIsr, sizeof kIsr);
	mem[0x0109] = (uint8_t)(tick & 0xff);
	mem[0x010A] = (uint8_t)(tick >> 8);
	mem[0x0038] = 0xC3;
	mem[0x0039] = 0x00;
	mem[0x003A] = 0x01;
	mem[0xFD9F] = 0xC9;
}

/* DqStepCpu の実装 */
static void DqStepCpu(CHardMsx* hw, Ay_Cpu* cpu, uint64_t* nextIrq, int irqOnBusy)
{
	uint8_t* m = cpu->get_mem();
	const uint16_t pc = cpu->r.pc;
	if (m && m[pc] == 0x76) {
		cpu->irqDelay = 0;
		if (!cpu->r.iff1) {
			cpu->r.pc = (uint16_t)(pc + 1);
			hw->AddCpuCycles(4);
			return;
		}
		if (cpu->r.im != 2 || !Ay_CpuIm2Interrupt(cpu, 0xff))
			Ay_CpuIm1Interrupt(cpu);
		hw->AddCpuCycles(16);
		return;
	}
	const int cyc = Ay_CpuRunOne(cpu);
	if (cyc <= 0) return;
	hw->AddCpuCycles((uint64_t)cyc);
	if (irqOnBusy && hw->CpuCycles() >= *nextIrq) {
		*nextIrq += (uint64_t)MSX_CPU_HZ / 60u;
		if (cpu->r.iff1) {
			cpu->irqDelay = 0;
			if (cpu->r.im != 2 || !Ay_CpuIm2Interrupt(cpu, 0xff))
				Ay_CpuIm1Interrupt(cpu);
		}
	}
}

/* CHardMsx::StartSongDq の実装 */
int CHardMsx::StartSongDq(unsigned titleCode)
{
	if (!cpu_) return 0;
	titleCode_ = titleCode;
	const uint8_t song = (uint8_t)(titleCode & 0xff);
	const int dq2 = (ge_ && _stricmp(ge_->subtype, "dq2") == 0) ? 1 : 0;

	MapAscii8(0, 0);
	MapAscii8(1, 1);
	MapAscii8(2, 2);
	MapAscii8(3, 3);
	PlantDqBios();

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpu_->r.sp = 0xF380;
	cpu_->r.iff1 = 1;
	cpu_->r.im = 1;
	cpuCycles_ = 0;
	idle_ = 0;
	playing_ = 1;
	sccAccessed_ = 0;
	ayWriteCount_ = 0;
	if (chipAy_) chipAy_->Reset();
	CEmuHardMsxSetActive(this);

	int guard = 0;
	int ready = 0;
	int isrPlanted = 0;
	uint64_t nextIrq = (uint64_t)MSX_CPU_HZ / 60u;
	while (guard++ < 800000) {
		if (dq2 && !isrPlanted && mem_[0xC000] == 0xC3 && mem_[0xC003] == 0xC3) {
			/* ドライバは RAM。Init の CALL $C003／DB $64 は C32F（E000 ハンドシェイク）待ち — それは C1CF が要り EI;RET ではない。 */
			PlantDqMusicIsr(mem_, 0xC000);
			isrPlanted = 1;
		}
		if (dq2) {
			if (isrPlanted && cpu_->r.pc == 0x40B2)
				ready = 1;
		} else if (mem_[0xD000] == 0xC3 && mem_[0xFD9F] == 0xC3) {
			ready = 1;
		}
		if (ready)
			break;
		DqStepCpu(this, cpu_, &nextIrq, dq2 && isrPlanted);
	}

	/* ページ 0 はカートバンク 0 を持ち、DQ1 CALL $4012 がまだ play に着地する */
	MapAscii8(0, 0);
	MapAscii8(1, 1);

	if (dq2)
		PlantDqMusicIsr(mem_, 0xC000);
	else
		PlantDqMusicIsr(mem_, 0xD003);

	if (dq2) {
		/* C003/DB はゲームイベント表（Overture = JP $4E0B スクリプト）。BGM 自体は C9D5: A を CA44 に格納、DF20=8、CA45 がバンク $1C をマップ、CC93 がチャネル状態を埋め、毎 vblank C1CF→CC13 が CB95 を再構築。インライン DB＋IRQ が曲 ID を奪った（SAMESONG）。 */
		mem_[0x0120] = 0x3E;
		mem_[0x0121] = song;
		mem_[0x0122] = 0xCD;
		mem_[0x0123] = 0xD5;
		mem_[0x0124] = 0xC9;
		mem_[0x0125] = 0xFB;
		mem_[0x0126] = 0x76;
		mem_[0x0127] = 0x18;
		mem_[0x0128] = 0xFD;
		cpu_->r.pc = 0x0120;
		cpu_->r.iff1 = 1;
		cpu_->r.im = 1;
		{
			int g = 0;
			while (g++ < 400000) {
				const uint16_t pc = cpu_->r.pc;
				if (pc == 0x0125 || pc == 0x0126)
					break;
				DqStepCpu(this, cpu_, &nextIrq, 1);
			}
			if (cpu_->r.pc == 0x0125 || cpu_->r.pc == 0x0126)
				cpu_->r.pc = 0x0126;
		}
	} else {
		mem_[0x0120] = 0x3E;
		mem_[0x0121] = song;
		mem_[0x0122] = 0xCD;
		mem_[0x0123] = 0x12;
		mem_[0x0124] = 0x40;
		mem_[0x0125] = 0xFB;
		mem_[0x0126] = 0x76;
		mem_[0x0127] = 0x18;
		mem_[0x0128] = 0xFD;
		cpu_->r.pc = 0x0120;
	}
	cpu_->r.iff1 = 1;
	cpu_->r.im = 1;
	idle_ = 0;
	playing_ = 1;
	return 1;
}

/* CHardMsx::StartSongDs4 の実装 */
int CHardMsx::StartSongDs4(unsigned titleCode)
{
	if (!cpu_) return 0;
	titleCode_ = titleCode;
	idle_ = 0;
	playing_ = 1;
	sccAccessed_ = 0;
	if ((titleCode & 0x80u) == 0) {
		ioport_[0x00] = 0x01;
		ioport_[0x01] = (uint8_t)(titleCode & 0xff);
	} else {
		/* IPL は BGM CALL $729D のあとだけ EI。SFX は $C095＋ISR $732F。PulseVblankIrq は iff1 が要り、無いと $0038 が走らない。 */
		mem_[0xc095] = (uint8_t)(titleCode & 0x7fu);
		cpu_->r.iff1 = 1;
		cpu_->r.iff2 = 1;
	}
	CEmuHardMsxSetActive(this);
	return 1;
}

/* 曲を開始する */
int CHardMsx::StartSong(unsigned titleCode)
{
	if (mapper_ == MAP_DS4)
		return StartSongDs4(titleCode);
	if (dqMode_)
		return StartSongDq(titleCode);
	if (genericMode_)
		return StartSongGeneric(titleCode);
	return StartSongKss(titleCode);
}

int CHardMsx::ApplyCatalogToggle(unsigned titleCode)
{
	const unsigned lo = titleCode & 0xffu;
	if (lo != 0xFFu)
		return 0;
	const int firehawkPack = (initPc_ == 0x400
		&& mem_[0x041C] == 0xDB && mem_[0x041D] == 0x03
		&& mem_[0x041E] == 0xFE && mem_[0x041F] == 0xFF
		&& mem_[0x1B00] == 0xC3 && mem_[0x1B03] == 0xC3) ? 1 : 0;
	if (!firehawkPack)
		return 0;
	/* TO BOSS: PATCH `IN A,(3); CP FF` は $1B0D フラグ。曲は変えない。 */
	ioport_[0x03] = 0xFF;
	mem_[0x1B0D] = 0xFF;
	return 1;
}

/* CEmuHardMsxSetActive の実装 */
void CEmuHardMsxSetActive(CHardMsx* hw)
{
	CEmuZ80BusSetActive(hw);
}
