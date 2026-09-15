#include "StdAfx.h"
#include "cemu_hard_x1.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum {
	X1_CPU_HZ = 4000000,
	X1_OPM_HZ = 4000000,
	X1_AY_HZ = 2000000
};

/* CEmuParseOptHex の実装 */
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

/* IsX1Platform の実装 */
static int IsX1Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "x1") == 0) return 1;
	if (_stricmp(ge->dataDir, "x1") == 0) return 1;
	if (_stricmp(ge->subtype, "x1") == 0 || _stricmp(ge->subtype, "x1psg") == 0) return 1;
	return 0;
}

/* KOEI YDOS3X: D200 または D000 の load+10 「OVL-1」印が要る。F800 stub だけでは判定しない — 多くの mucom はそこに 0xF37C を残し、hoot IM2 ベクタ 0/6 が要る（F37C のみ検出は high opmW／peak=0 で無音にした）。 */
static int MemHasYdos(const uint8_t* mem)
{
	if (!mem) return 0;
	if (mem[0xD20A] == 'O' && mem[0xD20B] == 'V' && mem[0xD20C] == 'L'
		&& mem[0xD20D] == '-' && mem[0xD20E] == '1')
		return 1;
	if (mem[0xD00A] == 'O' && mem[0xD00B] == 'V' && mem[0xD00C] == 'L'
		&& mem[0xD00D] == '-' && mem[0xD00E] == '1')
		return 1;
	return 0;
}

/* Gen1 sangoku は load 時に読める OVL-1 無しで YDOS3X.SYS を載せる（PATCH まで暗号）。カタログ rom 名が LoadRoms の CIM ミラーを許可する。 */
static int GeHasYdosRom(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		const char* n = ge->rom[i].name;
		if (!n || !n[0]) continue;
		if (_strnicmp(n, "YDOS", 4) == 0) return 1;
	}
	return 0;
}

/* X1IsYdos の実装 */
static int X1IsYdos(CHardX1* hw)
{
	return (hw && (hw->ydosRom_ || MemHasYdos(hw->Mem()))) ? 1 : 0;
}

/* Tecnosoft kugyoku: `CP FF; JR Z; LD B,A; AND 0F; CALL drv`。ポート 1/F が 0x80/0x81/0xFF コマンド。AND 0F がトラックニブル。lo をラッチにすると 0x80000003 が A=3 で drv を呼ぶ（そのトラックは無い）。 */
static int CEmuX1TecnoCmdHi(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 8u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0xFE || p[1] != 0xFF)
			continue;
		for (unsigned j = 2; j < 12u && i + j + 1u < patchLen; j++) {
			if (p[j] == 0xE6 && p[j + 1] == 0x0F)
				return 1;
		}
	}
	return 0;
}

/* PATCH `LD HL,4000 / LD (drv+0x0A),HL` — Telenet dds/luxsor/yakyufan。CTC ISR は `LD A,(drv+0x0F); OR A; RET Z` し、そのバイトが 0 のままだと TL 組みを飛ばす（high keyOn、hist60=0、peak=0）。 */
static uint16_t CEmuX1TelenetPlayGate(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 6u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 6u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0x21 || p[1] != 0x00 || p[2] != 0x40 || p[3] != 0x22)
			continue;
		const unsigned nn = (unsigned)p[4] | ((unsigned)p[5] << 8);
		if ((nn & 0xFFu) != 0x0Au || nn < 0xE000u)
			continue;
		return (uint16_t)(nn + 5u);
	}
	return 0;
}

/* Enix JESUS: `CP 09; JR NC`（ポート 1 は 0-8）のあと `CP 72`（ポート 0F は OPMTBL 添字＝グローバル音楽 ID）。 */
static int CEmuX1JesusSplit(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	int saw09 = 0, saw72 = 0;
	for (unsigned i = 0; i + 2u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0xFE && p[1] == 0x09 && i + 2u < patchLen && p[2] == 0x30)
			saw09 = 1;
		if (p[0] == 0xFE && p[1] == 0x72)
			saw72 = 1;
	}
	return (saw09 && saw72) ? 1 : 0;
}

/* Herzog OPMX1: `LD DE,2802; LD L,A; ADD HL,HL; ADD HL,DE` が mdata+2 のポインタ表を添字。PSGX1 は $1B60 で `LD DE,2800`（トラック 0 が先頭ワード）。revo2 DEMO: `LD A,(F5F8); CP 03; JP NC` は曲 >= 3 を mute/init（MUS103 lo=3 が当たり TL=7F を書いた）。 */
static int CEmuX1SongIdFromHi(const uint8_t* mem)
{
	if (!mem) return 0;
	for (unsigned a = 0; a + 6u < 0x10000u; a++) {
		if (mem[a] == 0x11 && mem[a + 1] == 0x02 && mem[a + 2] == 0x28)
			return 1;
		if (mem[a] == 0x11 && mem[a + 1] == 0x00 && mem[a + 2] == 0x28
			&& mem[a + 3] == 0x6F && mem[a + 4] == 0x26 && mem[a + 5] == 0x00)
			return 1;
		if (mem[a] == 0x3A && mem[a + 1] == 0xF8 && mem[a + 2] == 0xF5
			&& mem[a + 3] == 0xFE && mem[a + 4] == 0x03)
			return 1;
	}
	return 0;
}

/* Falcom xana2: `IN A,(0F); SUB 2` — 系統 hi は常に 0x02、トラックは lo。ゲート無しだと euphory 0x02000004（曲 2、バンク 4）がトラック 4 になる。 */
static int CEmuX1FalcomLoTrack(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 4u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 4u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0xDB && p[1] == 0x0F && p[2] == 0xD6 && p[3] == 0x02)
			return 1;
		if ((p[0] == 0x0E && p[1] == 0x0F)
			|| (p[0] == 0x01 && p[1] == 0x0F && p[2] == 0x00)) {
			for (unsigned j = 2; j < 12u && i + j + 1u < patchLen; j++) {
				if (p[j] == 0xD6 && p[j + 1] == 0x02)
					return 1;
			}
		}
	}
	return 0;
}

/* pwmajan PATCH: `LD HL,$6000; LD D,A; DEC D` のあとファイル毎 LDIR。SASURAI dest $4800／POCO dest $5000、ソースは $6000 のまま。 */
static int X1IsPwmajanPatch(const uint8_t* mem)
{
	return mem
		&& mem[0xF03F] == 0x21 && mem[0xF040] == 0x00 && mem[0xF041] == 0x60
		&& mem[0xF042] == 0x57 && mem[0xF043] == 0x15;
}

/* sghost PATCH: ポート F は OPDEMO（1）／BACKUP（>=2）／ドライバ（0）を選び、MA00x 添字ではない。`LD HL,$B000; LD DE,$E000`。 */
static int X1IsSghostPatch(const uint8_t* mem)
{
	return mem
		&& mem[0xF02B] == 0x21 && mem[0xF02C] == 0x00 && mem[0xF02D] == 0xB0
		&& mem[0xF02E] == 0x11 && mem[0xF02F] == 0x00 && mem[0xF030] == 0xE0;
}

/* ishtar PATCH @ $F000: `DI; LD SP,$F100; IM 2; LD A,1; LD I,A; CALL $0B96`。タイトル mid は載せた IBGM/IS* ファイル内のバイトオフセット。 */
static int X1IsIshtarPatch(const uint8_t* mem)
{
	return mem
		&& mem[0xF000] == 0xF3 && mem[0xF001] == 0x31
		&& mem[0xF002] == 0x00 && mem[0xF003] == 0xF1
		&& mem[0xF00A] == 0xCD && mem[0xF00B] == 0x96
		&& mem[0xF00C] == 0x0B;
}

/* ys_x1 PATCH: `LD HL,$C000; LD DE,$4D00; LD BC,$0D00; LDIR` のあと `IN A,(0F); LD ($28A5),A`。ポート F はファイル内トラック（0 は有効）。ポート 1 の上位ニブルはモード（$00 ゲーム内／$20 タイトル／$30 エンディング）。 */
static int X1IsYs1Patch(const uint8_t* mem)
{
	if (!mem) return 0;
	for (unsigned i = 0; i + 11u < 256u; i++) {
		if (mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0xC0
			&& mem[i + 3] == 0x11 && mem[i + 4] == 0x00 && mem[i + 5] == 0x4D
			&& mem[i + 6] == 0x01 && mem[i + 7] == 0x00 && mem[i + 8] == 0x0D
			&& mem[i + 9] == 0xED && mem[i + 10] == 0xB0)
			return 1;
	}
	return 0;
}

/* gand PATCH: `LD HL,$8600; LD DE,$0200; LD BC,$1000; LDIR` のあと `IN A,(1); CP $1C` が $00FC の 8 バイト行を添字。ポート 1 は曲 ID（0 = 効果）。ポート F 1 は mdata→$B700、2 は 78B2 経路。 */
static int X1IsGandPatch(const uint8_t* mem)
{
	return mem
		&& mem[0x25] == 0x21 && mem[0x26] == 0x00 && mem[0x27] == 0x86
		&& mem[0x28] == 0x11 && mem[0x29] == 0x00 && mem[0x2A] == 0x02
		&& mem[0x2B] == 0x01 && mem[0x2C] == 0x00 && mem[0x2D] == 0x10
		&& mem[0x2E] == 0xED && mem[0x2F] == 0xB0;
}

/* ys2: `LD HL,C000; LD DE,4000; LD BC,1000; LDIR` — 曲 >= $20 は BGM を $4000 へコピー。曲 < $20 はそこにある表を添字（モード 0）。 */
static int CEmuX1Ys2Mirror4000(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 11u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 11u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0x21 && p[1] == 0x00 && p[2] == 0xC0
			&& p[3] == 0x11 && p[4] == 0x00 && p[5] == 0x40
			&& p[6] == 0x01 && p[7] == 0x00 && p[8] == 0x10
			&& p[9] == 0xED && p[10] == 0xB0)
			return 1;
	}
	return 0;
}

/* Laplace: `LD C,0F; IN E,(C)` のあと `LD A,D5; OUT (1FA3); OUT (C),E` — ポート F は CTC 時定数でありトラック添字ではない。 */
static int CEmuX1LaplaceCtcF(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 4u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0x0E || p[1] != 0x0F || p[2] != 0xED || p[3] != 0x58)
			continue;
		for (unsigned j = 4; j + 1u < 16u && i + j + 1u < patchLen; j++) {
			if (p[j] == 0x3E && p[j + 1] == 0xD5)
				return 1;
		}
	}
	return 0;
}

/* wibarm: `LD BC,000F; IN A,(C); CP FF; CALL Z` — ポート F はファイル内トラック（0 = フィールド）またはエンディングオーバーレイの $FF。ポート 1 のコピーではない。 */
static int CEmuX1WibarmPortF(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 8u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	for (unsigned i = 0; i + 8u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] != 0x01 || p[1] != 0x0F || p[2] != 0x00)
			continue;
		for (unsigned j = 3; j + 4u < 12u && i + j + 4u < patchLen; j++) {
			if (p[j] == 0xFE && p[j + 1] == 0xFF && p[j + 2] == 0xCC)
				return 1;
		}
	}
	return 0;
}

/* ametruck: `IN A,(1); CP 03; JR NC` — ポート 1 は OPENING/HISCORE/ENDING。`IN A,(0F); CALL play` — hi がファイル内バリアント（ROUTE 333）。 */
static int CEmuX1AmetruckPortF(const uint8_t* mem, unsigned patchOff, unsigned patchLen)
{
	if (!mem || patchLen < 10u) return 0;
	if (patchOff + patchLen > 0x10000u)
		patchLen = 0x10000u - patchOff;
	int sawCp03 = 0, sawInF = 0;
	for (unsigned i = 0; i + 5u <= patchLen; i++) {
		const uint8_t* p = mem + patchOff + i;
		if (p[0] == 0xFE && p[1] == 0x03 && p[2] == 0x30)
			sawCp03 = 1;
		if (p[0] == 0x01 && p[1] == 0x0F && p[2] == 0x00
			&& p[3] == 0xED && p[4] == 0x78)
			sawInF = 1;
	}
	return (sawCp03 && sawInF) ? 1 : 0;
}

/* CEmuX1TelenetPlayTempo の実装 */
static uint16_t CEmuX1TelenetPlayTempo(const uint8_t* mem, uint16_t gate)
{
	if (!mem || !gate) return 0;
	const uint8_t glo = (uint8_t)gate;
	const uint8_t ghi = (uint8_t)(gate >> 8);
	for (unsigned a = 0xE000u; a + 10u < 0x10000u; a++) {
		if (mem[a] == 0x3A && mem[a + 1] == glo && mem[a + 2] == ghi
			&& mem[a + 3] == 0xB7 && mem[a + 4] == 0xC8
			&& mem[a + 5] == 0x21 && mem[a + 8] == 0x35 && mem[a + 9] == 0xC0) {
			const unsigned nn = (unsigned)mem[a + 6] | ((unsigned)mem[a + 7] << 8);
			if (nn >= 0xE000u && nn < 0x10000u)
				return (uint16_t)nn;
		}
	}
	return 0;
}

/* 再生／タイマを武装する */
void CHardX1::ArmTelenetPlayGate()
{
	if (!opmPlayGate_) return;
	const unsigned ptr = (unsigned)opmPlayGate_ - 5u;
	const unsigned bgm = (unsigned)mem_[ptr] | ((unsigned)mem_[ptr + 1] << 8);
	if (bgm < 0x100u || bgm >= 0xE000u) return;
	mem_[opmPlayGate_] = 1;
	/* ISR `DEC (tempo); RET NZ` はゼロ BSS バイトから 256 tick（約 5s）待って最初の TL 書き。最初の IRQ が走るようにプライムする。 */
	if (opmPlayTempo_ && mem_[opmPlayTempo_] == 0)
		mem_[opmPlayTempo_] = 1;
}

/* データを載せる */
void CHardX1::LoadSghostOpmPatches()
{
	/* OPMDRV INIT $4595: スロット 1..7、ワード表 $B030、FB は $20+ch、PMS は $38+ch、オペレータ 24 バイトは $40+ch step 8、最後に $0F。PATCH CALL INIT は $F05A へ戻る。ホスト側で適用し、ゼロ表 INIT 後に着地した MA00x も TL/AR を組む。 */
	if (psgOnly_ || !chipOpm_ || !X1IsSghostPatch(mem_))
		return;
	if (playSongLatchF_ != 0)
		return;
	{
		const uint16_t hl0 = (uint16_t)mem_[0xB030] | ((uint16_t)mem_[0xB031] << 8);
		if (hl0 < 0xB000u || hl0 >= 0xBC00u)
			return;
	}
	for (int slot = 0; slot < 7; slot++) {
		const unsigned t = 0xB030u + (unsigned)slot * 2u;
		const uint16_t hl = (uint16_t)mem_[t] | ((uint16_t)mem_[t + 1] << 8);
		if (hl < 0xB000u || hl >= 0xBC00u)
			continue;
		const int ch = slot + 1;
		uint16_t p = hl;
		chipOpm_->Write(0, (uint8_t)(0x20 + ch));
		chipOpm_->Write(1, mem_[p++]);
		chipOpm_->Write(0, (uint8_t)(0x38 + ch));
		chipOpm_->Write(1, mem_[p++]);
		uint8_t d = (uint8_t)(0x40 + ch);
		for (int i = 0; i < 24; i++) {
			chipOpm_->Write(0, d);
			chipOpm_->Write(1, mem_[p++]);
			d = (uint8_t)(d + 8);
		}
		chipOpm_->Write(0, 0x0f);
		chipOpm_->Write(1, mem_[p]);
	}
}

CHardX1::CHardX1()
	: cpuHz_(X1_CPU_HZ)
	, opmHz_(X1_OPM_HZ)
	, ayHz_(X1_AY_HZ)
	, psgOnly_(0)
	, opnMode_(0)
	, initPc_(0xC000)
	, mdataAddr_(0x4000)
	, mdataSize_((unsigned)BGM_SIZE)
	, titleCode_(0)
	, playCmdLatch_(0)
	, playSongLatch_(0)
	, playSongLatchF_(0)
	, playCmdHoldIrqs_(0)
	, ydosCmdSeen_(0)
	, ydosInhibitReentry_(0)
	, ydosRom_(0)
	, opmPlayGate_(0)
	, opmPlayTempo_(0)
	, tecnoCmdHi_(0)
	, jesusSplitPorts_(0)
	, songIdFromHi_(0)
	, skipPrestageRam_(0)
	, skipTriggerStage_(0)
	, psgStatToggle_(0)
	, ys2Mirror4000_(0)
	, laplaceCtcF_(0)
	, wibarmPortF_(0)
	, falcomPortF_(0)
	, ametruckPortF_(0)
	, marsHoldIrq_(0)
	, marsSeenProg_(0)
	, marsPlayReady_(0)
	, cpu_(NULL)
	, chipOpm_(NULL)
	, chipOpn_(NULL)
	, chipAy_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, stageLimit_(0x10000u)
	, bgmStageOff_(0)
	, ctcVectorBase_(0)
	, ctcVectorProgrammed_(0)
{	hardKind = KIND_X1;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
	memset(ctcIe_, 0, sizeof(ctcIe_));
	memset(ctcExpectTc_, 0, sizeof(ctcExpectTc_));
	memset(ctcControl_, 0, sizeof(ctcControl_));
	memset(ctcTc_, 0, sizeof(ctcTc_));
	memset(ctcTcValid_, 0, sizeof(ctcTcValid_));
	for (int i = 0; i < 4; i++)
		xmlCtcVec_[i] = -1;
}

CHardX1::~CHardX1()
{
	Shutdown();
}

/* チップと CPU を生成する */
int CHardX1::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsX1Platform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = X1_CPU_HZ;
	opmHz_ = X1_OPM_HZ;
	ayHz_ = X1_AY_HZ;
	psgOnly_ = (_stricmp(ge->subtype, "psg") == 0 || _stricmp(ge->subtype, "x1psg") == 0) ? 1 : 0;
	opnMode_ = (_stricmp(ge->subtype, "opn") == 0) ? 1 : 0;
	/* init_pc／mdata_* は ROM＋オプション確定後に LoadRoms で最終化 */
	initPc_ = 0xC000;
	mdataAddr_ = 0x4000;
	mdataSize_ = (unsigned)BGM_SIZE;
	if (opnMode_)
		chipOpn_ = CEmuChipYm2608Create((uint32_t)opmHz_, 0, sampleRate_);
	else if (!psgOnly_) {
		chipOpm_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
		CEmuChipYm2151SetRlZeroAsLr(chipOpm_, 1);
	}
	chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
	cpu_ = new Ay_Cpu();
	return (cpu_ && chipAy_ && (psgOnly_ || chipOpm_ || chipOpn_)) ? 1 : 0;
}

/* PCM／コードバンク */
void CHardX1::FreeBanks()
{
	for (int i = 0; i < 128; i++) {
		if (bgmBank_[i]) {
			free(bgmBank_[i]);
			bgmBank_[i] = NULL;
		}
		bgmBankSize_[i] = 0;
		bgmPresent_[i] = 0;
	}
}

/* チップ／CPU／ROM を破棄する */
void CHardX1::Shutdown()
{
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	FreeBanks();
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chipOpm_) {
		CEmuChipYm2151Destroy(chipOpm_);
		chipOpm_ = NULL;
	}
	if (chipOpn_) {
		CEmuChipYm2608Destroy(chipOpn_);
		chipOpn_ = NULL;
	}
	if (chipAy_) {
		CEmuChipAyDestroy(chipAy_);
		chipAy_ = NULL;
	}
}

/* バンク／BGM を載せる */
void CHardX1::StageBgm(uint8_t index)
{
	/* 正確なバンクのみ — YDOS の bank0 推測はしない */
	uint8_t use = index;
	if (use >= 128 || !bgmPresent_[use] || !bgmBank_[use])
		return;
	unsigned n = bgmBankSize_[use];
	if (n > mdataSize_) n = mdataSize_;
	if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
	/* BGM が mdata より上に載ったコードを壊さないようクランプ（crimson OP@7A00） */
	if (stageLimit_ > mdataAddr_) {
		unsigned room = (unsigned)stageLimit_ - (unsigned)mdataAddr_;
		if (n > room) n = room;
	}
	if (n == 0) return;
	unsigned srcOff = bgmStageOff_;
	if (srcOff >= bgmBankSize_[use])
		return;
	unsigned avail = bgmBankSize_[use] - srcOff;
	if (n > avail) n = avail;
	const uint8_t* src = bgmBank_[use] + srcOff;
	/* OUT 0 経路: mucomx1 は $5000 の 8K IO 窓へ載せる。KOEI YDOS は既に MUSIC.CIM を mdata（$4000）へマップしその窓を IN (C) — ファイルを $5000 へコピーすると CIM+$1000 が重なり曲 6+ が別バイトへ移る（suiko 01000006.. 無音、CTC TC が 1 で固まる）。 */
	if (X1IsYdos(this) && mdataAddr_ != 0x5000) {
		unsigned ioBase = mdataAddr_;
		unsigned ioN = n;
		if (ioBase + ioN > 0x10000u)
			ioN = 0x10000u - ioBase;
		if (ioN)
			memcpy(ioport_ + ioBase, src, ioN);
	} else {
		unsigned ioN = n;
		if (ioN > 0x2000u) ioN = 0x2000u;
		memcpy(ioport_ + 0x5000, src, ioN);
		if (ioN < 0x2000u)
			memset(ioport_ + 0x5000 + ioN, 0, 0x2000u - ioN);
	}
	/* mdata_addr の RAM へミラー（タイトル毎オプション）。mdataSize_ 窓全体を memset しない: sphari mdata@A000＋既定 32K は OPMDRV@E000 を消す（opmW=0）。hayato mdata@C000＋32K は頂上 RAM を消す。載せたバイト＋短いパッドだけクリア（YDOS と同じ考え）。 */
	if (mdataAddr_ + n <= 0x10000) {
		memcpy(mem_ + mdataAddr_, src, n);
		unsigned pad = 0x100;
		if (n + pad > mdataSize_) pad = (mdataSize_ > n) ? (mdataSize_ - n) : 0;
		if (pad && mdataAddr_ + n + pad <= 0x10000)
			memset(mem_ + mdataAddr_ + n, 0, pad);
	}
	/* ENDING の tick はノート保持中 NZ。ファイル内ループはそれを曲終了と見る（`JR NZ,$10E6` → JP $FE00）。PSG I/O ポート A bit5 も待ちから抜ける（`JR NZ,$1095`）。OPENING のプレーヤは Z を返し bit5 を保つ。 */
	if (ametruckPortF_ && mem_[0x1098] == 0x20 && mem_[0x1099] == 0x4C) {
		mem_[0x1098] = mem_[0x1099] = 0x00;
		if (mem_[0x10C3] == 0x20 && mem_[0x10C4] == 0xD0)
			mem_[0x10C3] = 0x18;
	}
	/* ys2 モード 0 は C000 LDIR 無しで $4000 のポインタ表を添字。エンディング（lo==$20）は LDIR C000→$2800。ここでミラーし、最初の PATCH stop が遅れても $16C6 がデータを見る。 */
	if (ys2Mirror4000_ && n) {
		unsigned n4 = n;
		if (n4 > 0x1000u) n4 = 0x1000u;
		memcpy(mem_ + 0x4000, src, n4);
		if ((titleCode_ & 0xffu) == 0x20u)
			memcpy(mem_ + 0x2800, src, n4);
	}
	mem_[LOAD_FLAG] = 0xff;
}

/* CEMU_X1_CTC_TRACE=1 はゲストの CTC 組み（メイン基板 1FA0 と CZ-8BS1 音源基板 0704）をダンプし、tick 源と分周を推測ではなく読む。 */
static void X1CtcTrace(CHardX1* hw, uint16_t port, uint8_t data)
{
	static int mode = -1;
	static int left = 0;
	if (mode < 0) {
		const char* e = getenv("CEMU_X1_CTC_TRACE");
		mode = (e && *e && *e != '0') ? 1 : 0;
		left = 64;
	}
	if (!mode || left <= 0) return;
	left--;
	Ay_Cpu* cpu = hw ? hw->Cpu() : NULL;
	printf("[ctc] port=%04X data=%02X pc=%04X%s\n", port, data,
		cpu ? cpu->r.pc : 0,
		(data & 0x01) ? "  ctrl" : "  vec/tc");
}

/* CHardX1::CtcReset の実装 */
void CHardX1::CtcReset()
{
	ctcVectorBase_ = 0;
	ctcVectorProgrammed_ = 0;
	memset(ctcIe_, 0, sizeof(ctcIe_));
	memset(ctcExpectTc_, 0, sizeof(ctcExpectTc_));
	memset(ctcControl_, 0, sizeof(ctcControl_));
	memset(ctcTc_, 0, sizeof(ctcTc_));
	memset(ctcTcValid_, 0, sizeof(ctcTcValid_));
}

/* バス書込 */
void CHardX1::CtcWrite(int channel, uint8_t data)
{
	if (channel < 0 || channel > 3) return;
	if (ctcExpectTc_[channel]) {
		/* 時定数: 0 は 256（Zilog CTC） */
		ctcTc_[channel] = data;
		ctcTcValid_[channel] = 1;
		ctcExpectTc_[channel] = 0;
		return;
	}
	if ((data & 0x01) == 0) {
		/* 割り込みベクタロード（Zilog: チャネル 0 のみ。bits7-3 = 基点） */
		if (channel == 0) {
			ctcVectorBase_ = (uint8_t)(data & 0xf8);
			ctcVectorProgrammed_ = 1;
		}
		return;
	}
	/* 制御ワード: bit7=IE、bit6=カウンタ、bit5=プリスケール /256、bit2=続く TC */
	ctcControl_[channel] = data;
	ctcIe_[channel] = (data & 0x80) ? 1 : 0;
	ctcExpectTc_[channel] = (data & 0x04) ? 1 : 0;
	if (ctcExpectTc_[channel])
		ctcTcValid_[channel] = 0;
}

/* CHardX1::CtcTimerPeriodCycles の実装 */
unsigned CHardX1::CtcTimerPeriodCycles(int channel) const
{
	if (channel < 0 || channel > 3) return 0;
	if (!ctcTcValid_[channel]) return 0;
	/* カウンタモード（bit6）: ホストは vsync／既定のまま — フリータイマではない */
	if (ctcControl_[channel] & 0x40) return 0;
	unsigned tc = ctcTc_[channel] ? (unsigned)ctcTc_[channel] : 256u;
	const unsigned prescale = (ctcControl_[channel] & 0x20) ? 256u : 16u;
	return tc * prescale;
}

/* CHardX1::CtcCounterTc の実装 */
unsigned CHardX1::CtcCounterTc(int channel) const
{
	if (channel < 0 || channel > 3) return 0;
	if (!ctcTcValid_[channel]) return 0;
	/* カウンタモードのみ（bit6）: チャネルはトリガ入力を分周する */
	if (!(ctcControl_[channel] & 0x40)) return 0;
	return ctcTc_[channel] ? (unsigned)ctcTc_[channel] : 256u;
}

/* CHardX1::CtcVector の実装 */
uint8_t CHardX1::CtcVector(int channel) const
{
	if (channel < 0 || channel > 3)
		return 0;
	/* ゲストが組んだ CTC 基点が勝つ（manreq OPMDRV は 0x18 を書き ch3=0x1E） */
	if (ctcVectorProgrammed_)
		return (uint8_t)(ctcVectorBase_ + (uint8_t)(channel * 2));
	/* XML ctc0/ctc3 = hoot use_ctcN ベクタ上書き */
	if (xmlCtcVec_[channel] >= 0)
		return (uint8_t)(xmlCtcVec_[channel] & 0xff);
	/* hoot mucomx1 既定: TIMER ch0→0、VSYNC ch3→6 */
	return (uint8_t)(channel * 2);
}

/* I/O ポート読込 */
uint8_t CHardX1::PortIn(uint16_t port)
{
	const uint16_t p = port;
	/* コマンド／曲メールボックス — レベル読み。playCmdHoldIrqs_ 減衰（と任意 OUT0）がクリア。YDOS PATCH はディスパッチ後 IN 待ちへ戻る。ラッチは OUT0 StageBgm に足りる間 High。ydosCmdSeen_ を立て、待ちループ IN 前の事故 IRQ OUT0,0 が play エッジを落とさないようにする。 */
	if (p == 0x0000) {
		/* YDOS ポインタ構築 OUT0 のあと、hold 会計のためラッチは残すが 0 を返し、PATCH が 0x90 の前後で 0x91 に再入しないようにする */
		if (ydosInhibitReentry_)
			return 0;
		if (playCmdLatch_ && X1IsYdos(this) && cpu_
			&& cpu_->r.pc >= 0x0020 && cpu_->r.pc < 0x0070)
			ydosCmdSeen_ = 1;
		return playCmdLatch_;
	}
	if (p == 0x0001)
		return playSongLatch_;
	/* Falcom xana2 PATCH: IN A,(0F); SUB 2 が play/IRQ ベクタ表を添字。曲ラッチをミラー（hoot 音楽 ID 基点 2）。Herzog/revo2: ポート 1 は非ゼロ play コマンド。ポート 0F はファイル内トラック（0 は有効 — herzog PATCH `OR A; JR Z` が play を飛ばす）。 */
	/* gand PATCH `LD BC,$0F01; IN A,(C)` — Z80 はバスに BC を出す。000F ではない。 */
	if (p == 0x000f
		|| (X1IsGandPatch(mem_) && ((p & 0xff) == 0x0f || (p >> 8) == 0x0f))) {
		/* pwmajan: ポート 1 = ファイル、ポート F = トラック。sghost: ポート F = 0/1/2（ドライバ／OPDEMO／BACKUP）。MA00x ではない。 */
		if (X1IsPwmajanPatch(mem_) || X1IsSghostPatch(mem_)
			|| X1IsYs1Patch(mem_) || X1IsGandPatch(mem_))
			return playSongLatchF_;
		return (jesusSplitPorts_ || songIdFromHi_ || laplaceCtcF_ || ys2Mirror4000_
			|| wibarmPortF_ || falcomPortF_ || ametruckPortF_
			|| (initPc_ == 0xE900 && opmPlayGate_))
			? playSongLatchF_ : playSongLatch_;
	}
	if (opnMode_ && chipOpn_ && (p & 0xff) == 0xe0)
		return chipOpn_->ReadStatus();
	if (opnMode_ && chipOpn_ && (p & 0xff) == 0xe1)
		return chipOpn_->ReadData();
	if (!psgOnly_ && chipOpm_ && (p == 0x0700 || p == 0x0701))
		return chipOpm_->ReadStatus();
	/* psg xml に CZ-8BS1 が無い: OPM ステータス bit7 はクリア読み。Square PROG $1A40 `IN A,(0700); BIT 7; JP NZ` と T&E $8A58 `IN A,(0701); JP M` はさもなくば永久スピン。 */
	if (psgOnly_ && (p == 0x0700 || p == 0x0701))
		return 0;
	/* OUT 0704,$47 のあと $5A（または $47）が ioport にエコーするので PSG 専用機でも「OPM ボードあり」プローブが成功する。KING' KNIGHT は ($000C)=1 を格納し ISR $1139 CALL $169A。PSY-O-BLADE は ($00A3)=1、$0082 JP $8A66。どちらも 0700 を書くが psgOnly_ は無視。0 を返すとプローブ失敗しそれらのドライバは AY 経路へ。CTC OUT は生きたタイマを組む — エコーだけ抑止。 */
	if (psgOnly_ && p >= 0x0704 && p <= 0x0707)
		return 0;
	if (p == 0x1a01) {
		/* Bit2 = ready（Laplace／Dempa BIT 2）。Bit7 は交互: mars ISR `IN A,(1A01); JP P` が S=1 待ち、`JP M` が S=0。定数 0x04（S=0）は最初の待ちから出ない。0x80 は BIT 2 失敗。 */
		psgStatToggle_ ^= 1;
		return psgStatToggle_ ? 0x84 : 0x04;
	}
	/* Microcabin msnk: busy-wait IN A,(0FF8); AND 81; JP NZ — クリア = ready */
	if (p == 0x0ff8 || p == 0x0ff9 || p == 0x0ffc)
		return 0x00;
	/* X1 PSG は上位バイトでデコード。DRIVER `OUT (C),A` はデータを C に残すのでポートは 1C<data>／1B<data>。1C00/1B00 ではない。 */
	{
		const uint16_t ph = (uint16_t)(p & 0xff00);
		if (ph == 0x1c00 || ph == 0x1b00 || ph == 0x1900
			|| (ph == 0x1a00 && (p & 0xff) != 0x01)) {
			if (chipAy_) return chipAy_->ReadData();
			return 0xff;
		}
	}
	return ioport_[p];
}

/* I/O ポート書込 */
void CHardX1::PortOut(uint16_t port, uint8_t data)
{
	const uint16_t p = port;
	if (p == 0x0000) {
		int ydos = X1IsYdos(this);
		/* ys_x1 タイトル／エンディング `XOR A; OUT (0),A` がメールボックスを ack。そこでバンク 0 を載せるると LDIR で $8000/$6000 へ行く前に $C000 の TTLMUS/ENDMUS が置換される。 */
		if (X1IsYs1Patch(mem_) && data == 0) {
			playCmdLatch_ = 0;
			playCmdHoldIrqs_ = 0;
			ydosCmdSeen_ = 0;
			ydosInhibitReentry_ = 0;
			if (mem_[PLAY_FLAG] == 0x01)
				mem_[PLAY_FLAG] = 0x00;
			return;
		}
		/* Mucom は意図してここに BGM バンク添字を OUT。KOEI YDOS PATCH は CIM ポインタ構築中 `IN A,(1); DEC C; OUT (C),C` — その事故 OUT 0,0 で StageBgm／CIM 破壊をしてはいけない。再入抑止を武装し 0x90 は走らせ、待ちループ IN は 0 を見る。 */
		if (ydos && data == 0) {
			int patchPtr = (cpu_ && cpu_->r.pc >= 0x0040 && cpu_->r.pc < 0x0050);
			if (ydosCmdSeen_ && patchPtr)
				ydosInhibitReentry_ = 1;
		} else {
			uint8_t bank = data;
			StageBgm(bank);
			playCmdLatch_ = 0;
			playCmdHoldIrqs_ = 0;
			ydosCmdSeen_ = 0;
			ydosInhibitReentry_ = 0;
		}
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		return;
	}
	/* Telenet PATCH: IN A,(1); OUT (C),A が曲をエコーして CALL play。cmd ラッチは約 1.5s レベル High。play 自体が約 1s DI なので待ちループが cmd=1 を再見て再入する。luxsor の play は ISR ゲートを XOR クリアしワーク RAM を LDIR — GAPPY/STOPS。エッジをここで消費し play をワンショットに。 */
	if (p == 0x0001 && opmPlayGate_) {
		playCmdLatch_ = 0;
		playCmdHoldIrqs_ = 0;
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		ioport_[p] = data;
		return;
	}
	/* produce: PATCH が PROG1 $0285 で `CALL stop`、OUT 1,song、オーバーレイの $0AB8/$29AE で `CALL play`。オーバーレイはここ。TriggerPlay ではない（stop が走る前に消す）。 */
	if (p == 0x0001 && skipTriggerStage_) {
		StageBgm(data);
		playCmdLatch_ = 0;
		playCmdHoldIrqs_ = 0;
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		ioport_[p] = data;
		return;
	}
	/* Laplace／wibarm: 待ちループ再入。OUT 1 エコーのあと CALL play。レベル High cmd は毎パスで再初期化（DI）。wibarm の $FF 経路は曲ヘッダを LDDR — 2 回目で二重スライドし無音。 */
	if (p == 0x0001 && (laplaceCtcF_ || wibarmPortF_
		|| (X1IsGandPatch(mem_) && playSongLatch_ >= 5 && playSongLatch_ <= 0x0A))) {
		playCmdLatch_ = 0;
		playCmdHoldIrqs_ = 0;
		if (mem_[PLAY_FLAG] == 0x01)
			mem_[PLAY_FLAG] = 0x00;
		ioport_[p] = data;
		return;
	}
	if (opnMode_ && chipOpn_ && ((p & 0xff) == 0xe0 || (p & 0xff) == 0xe1)) {
		chipOpn_->Write((uint32_t)(p & 1), data);
		return;
	}
	if (!psgOnly_ && chipOpm_ && (p == 0x0700 || p == 0x0701)) {
		chipOpm_->Write((uint32_t)(p & 1), data);
		return;
	}
	/* CZ-8BS1 は OPM 隣 0704-0707 に独自 Z80 CTC。メイン基板 1FA0 マップと同じ 4 チャネル — 生きた CTC を組む。 */
	if (p >= 0x0704 && p <= 0x0707) {
		X1CtcTrace(this, p, data);
		CtcWrite((int)(p - 0x0704), data);
		ioport_[p] = data;
		return;
	}
	{
		const uint16_t ph = (uint16_t)(p & 0xff00);
		if (ph == 0x1b00 || (ph == 0x1a00 && (p & 0xff) != 0x01)) {
			/* hoot は port>>8 を ssAY8910 へ: 1B は奇数なのでデータ。mars もステータス 1A01 隣の 1A00 でデータをクロック。下位バイトの C は無視 — Laplace 8613 はそこにデータを残す。 */
			if (chipAy_) chipAy_->Write(1, data);
			ioport_[p] = data;
			return;
		}
		if (ph == 0x1c00 || ph == 0x1900) {
			/* 1C は偶数なので AY アドレスラッチ。mars は 1900。 */
			if (chipAy_) chipAy_->Write(0, data);
			ioport_[p] = data;
			return;
		}
	}
	/* Z80 CTC: MAME は 1FA0-1FA3 とミラー 1FA8-1FAB */
	if ((p >= 0x1fa0 && p <= 0x1fa3) || (p >= 0x1fa8 && p <= 0x1fab)) {
		X1CtcTrace(this, p, data);
		CtcWrite((int)(p & 3), data);
		ioport_[p] = data;
		return;
	}
	ioport_[p] = data;
}

/* メモリ 8bit 書込 */
void CHardX1::MemWrite(uint16_t addr, uint8_t data)
{
	mem_[addr] = data;
}

/* メモリ 8bit 読込 */
uint8_t CHardX1::MemRead(uint16_t addr)
{
	return mem_[addr];
}

void CHardX1::UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut,
	int ydos)
{
	/* hoot X1/NCS: 0xSS0000BB → song=SS bank=BB。0xSS000000 → song=SS bank=0。low のみ 0x000000NN → song=NN bank=NN（旧 StageBgm(song)）。拡張 mid!=0 コード（ishtar 0x00028408）は lo を song/bank に保つ。Humming Bird Laplace は hi をフラグバイト（0x68..0xC0）、本物 song/bank は lo — hi を曲にすると song=0x80 で DRIVER 呼び出しが無音。閾値 0x60 で Falcom xanaopm 0x48000001／0x2C000002 はまだ song=hi（MML id）bank=lo に解く。KOEI YDOS: 0x010000SS = ループ、0x000000SS = ワンショット。hi は再生フラグでありトラックではない。hi を曲にするとすべての 0x01****** タイトルがトラック 1（sangoku/suiko/sangoku2 で SAMESONG）。NCS 0x01000000 は本当に曲 1 — ydos でなければ適用しない。 */
	const unsigned lo = titleCode & 0xffu;
	const unsigned hi = (titleCode >> 24) & 0xffu;
	const unsigned mid = (titleCode >> 8) & 0xffffu;
	uint8_t song = 0, bank = 0;
	if (ydos && mid == 0 && hi == 1) {
		song = (uint8_t)lo;
		bank = 0;
	} else if (mid == 0 && hi >= 0x60) {
		song = (uint8_t)lo;
		bank = (uint8_t)lo;
	} else if (mid == 0 && (hi != 0 || lo != 0)) {
		/* 標準 hoot パック — 曲は 0 でもよい（メインテーマ） */
		song = (uint8_t)hi;
		bank = (uint8_t)lo;
		if (hi == 0 && lo != 0) {
			song = (uint8_t)lo;
			bank = (uint8_t)lo;
		}
	} else if (lo != 0) {
		song = (uint8_t)lo;
		bank = (uint8_t)lo;
	} else if (hi != 0 && hi < 0x40) {
		song = (uint8_t)hi;
		bank = 0;
	} else {
		song = 0;
		bank = 0;
	}
	if (songOut) *songOut = song;
	if (bankOut) *bankOut = bank;
}

/* CHardX1::PrestageBgm の実装 */
void CHardX1::PrestageBgm(unsigned titleCode)
{
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank, ydosRom_);
	bgmStageOff_ = 0;
	/* pwmajan: 0xTT0000BB — BB は PROG2/NORMAL/SASURAI/POCO、TT はファイル内トラック。0x00000004 の Unpack は song=bank=4。 */
	{
		const unsigned lo = titleCode & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (initPc_ == 0xF000 && mdataAddr_ == 0x6000 && mid == 0
			&& lo >= 1u && lo <= 5u)
			bank = (uint8_t)lo;
	}
	if (laplaceCtcF_) {
		const unsigned lo = titleCode & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (lo < 128 && bgmPresent_[lo] && bgmBank_[lo])
			bank = (uint8_t)lo;
		bgmStageOff_ = mid;
	}
	if (X1IsIshtarPatch(mem_) && !laplaceCtcF_) {
		const unsigned lo = titleCode & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		/* 0xFE にバンク無し。カタログは IBGM5（druaga B／trap #2）を指す */
		if (lo == 0xFEu && bgmPresent_[4] && bgmBank_[4])
			bank = 4;
		else if (lo < 128 && bgmPresent_[lo] && bgmBank_[lo])
			bank = (uint8_t)lo;
		bgmStageOff_ = mid;
	}
	/* hyd2: lo は flags:bank ニブル（0x12 = flag1 bank2）。0x12 を載せると PROG2/PROG3 を逃す。$4000 ミラーは $81xx プレーヤも壊す。 */
	if (initPc_ == 0xFE00) {
		const unsigned nibble = (titleCode & 0x0fu);
		if (nibble < 128 && bgmPresent_[nibble] && bgmBank_[nibble])
			bank = (uint8_t)nibble;
	}
	uint8_t stage = bank;
	if (!(stage < 128 && bgmPresent_[stage] && bgmBank_[stage])) {
		if (song < 128 && bgmPresent_[song] && bgmBank_[song])
			stage = song;
		else
			stage = 0xff;
	}
	if (stage < 128 && bgmPresent_[stage] && bgmBank_[stage]) {
		if ((mdataAddr_ == 0x4000 || mdataAddr_ == 0) && initPc_ != 0xFE00) {
			unsigned n = bgmBankSize_[stage];
			if (n > mdataSize_) n = mdataSize_;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			memset(mem_ + 0x4000, 0, n);
			memcpy(mem_ + 0x4000, bgmBank_[stage], n);
		}
		/* 再生時オーバーレイ: PATCH が初期化するまでブートコード（produce PROG1、xanaopm PR.NO0）を残す。TriggerPlay はまだ StageBgm。 */
		if (!skipPrestageRam_)
			StageBgm(stage);
	}
	/* DRIVER ブート中は play メールボックスをアイドルに保つ */
	playCmdLatch_ = 0;
	playSongLatch_ = 0;
	playSongLatchF_ = 0;
	playCmdHoldIrqs_ = 0;
	ydosCmdSeen_ = 0;
	ydosInhibitReentry_ = 0;
	mem_[PLAY_FLAG] = 0;
	mem_[PLAY_CODE] = 0;
}

/* 曲再生をトリガする */
void CHardX1::TriggerPlay(unsigned titleCode)
{
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank, ydosRom_);
	/* 二重メールボックス: ポートラッチ（Falcom）＋空きなら C010/C011（hoot Play）。IO@5000 は常に載せる。RAM ミラーは StageBgm が安全と見たときだけ。 */
	playCmdLatch_ = 0x01;
	/* Falcom xana2: 固定系統 hi=0x02、トラック ID は lo（ポート0F／CP 1Ah） */
	{
		const unsigned lo = titleCode & 0xffu;
		const unsigned hi = (titleCode >> 24) & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (X1IsPwmajanPatch(mem_) && mid == 0 && lo >= 1u && lo <= 5u) {
			/* ポート 1 = ファイル（NZ 必須）、ポート F = トラック（0 は #00） */
			song = (uint8_t)lo;
			bank = (uint8_t)lo;
			playSongLatchF_ = (uint8_t)hi;
		}
		if (X1IsSghostPatch(mem_) && mid == 0) {
			/* hi 0/1/2 = ドライバ／OPDEMO／BACKUP。lo は MA00x/MB00x */
			playSongLatchF_ = (uint8_t)hi;
			song = (uint8_t)lo;
			bank = (uint8_t)lo;
		}
		if (mid == 0 && hi == 2 && falcomPortF_)
			playSongLatch_ = lo ? (uint8_t)lo : (uint8_t)hi;
		else
			playSongLatch_ = song;
		/* Tecnosoft: hi は 0x80/0x81/0xFF（play／イントロ飛ばし／イントロ付き）。バンクは UnpackTitle の hi>=0x40 経路から既に `lo`。 */
		if (tecnoCmdHi_ && mid == 0 && hi >= 0x80)
			playSongLatch_ = (uint8_t)hi;
		/* JESUS: ポート 1 = コピー記述子 0-8、ポート 0F = OPMTBL 添字。グローバル ID >= 9 はポート 1 を共有し飛ばされていた（CP 09）。 */
		if (jesusSplitPorts_) {
			playSongLatchF_ = song;
			playSongLatch_ = (song < 9) ? song : bank;
		}
		if (songIdFromHi_ && mid == 0 && hi < 0x80 && !tecnoCmdHi_ && !falcomPortF_) {
			playSongLatchF_ = (uint8_t)hi;
			/* Herzog PATCH `IN A,(1); OR A; JR Z` は A=0 で CALL play を飛ばす。BGM トラック 0 はポート 0F = 0 と非ゼロ ポート 1 で 1800 へ届く必要がある。EFFECT タイトルは lo==0 — ポート 1 = 0 を保ち PSG PATCH2 が play(track=lo) ではなく CALL $180c を取る。 */
			if (psgOnly_ && lo == 0)
				playSongLatch_ = 0;
			else
				playSongLatch_ = hi ? (uint8_t)hi : 1;
		}
		bgmStageOff_ = 0;
		if (laplaceCtcF_) {
			if (lo < 128 && bgmPresent_[lo] && bgmBank_[lo])
				bank = (uint8_t)lo;
			bgmStageOff_ = mid;
			/* ポート F は CTC TC（0 = 256）でありトラックではない */
			playSongLatchF_ = 0;
		}
		if (X1IsIshtarPatch(mem_) && !laplaceCtcF_) {
			if (lo == 0xFEu && bgmPresent_[4] && bgmBank_[4])
				bank = 4;
			else if (lo < 128 && bgmPresent_[lo] && bgmBank_[lo])
				bank = (uint8_t)lo;
			bgmStageOff_ = mid;
		}
		/* ys2: ポート 1 は PATCH コマンド（CP $20 がモード 0/1）。ポート F はファイル内トラック。TTLMSn（lo>=$30）はゲーム内 MANPR1 として PLAY。タイトルエンジン（port1>=$20）はパート 0 で STOP。 */
		if (ys2Mirror4000_ && mid == 0 && !laplaceCtcF_) {
			if (lo >= 0x30u) {
				playSongLatch_ = 1;
				playSongLatchF_ = (uint8_t)hi;
			} else {
				playSongLatch_ = hi ? (uint8_t)hi : (uint8_t)lo;
				playSongLatchF_ = (uint8_t)hi;
			}
		}
		if (wibarmPortF_ && mid == 0 && !laplaceCtcF_ && !ys2Mirror4000_) {
			/* 0x00000002 フィールド = トラック 0。0x01000002 バトル = トラック 1。0xFF000003 エンディング = オーバーレイ＋トラック 0。ポート 1 は NZ のまま。 */
			playSongLatchF_ = (uint8_t)hi;
			playSongLatch_ = (hi && hi != 0xFFu) ? (uint8_t)hi : 1;
		}
		/* xana2 PSG/OPM: ポート F は PR.NOx 系統（hi）。SUB 2 が play/IRQ ベクタを添字。lo は載せた m.000x バンクだけ。 */
		if (falcomPortF_ && mid == 0 && hi && !wibarmPortF_ && !laplaceCtcF_
			&& !ys2Mirror4000_ && !jesusSplitPorts_) {
			playSongLatchF_ = (uint8_t)hi;
			/* 系統 3/4/5（PR.NO3+）— 載せた m.000x は 1 曲。OPM xml はまだ PR.NO0 @ $1000。ポート1=0 を奪うと系統 3 タイトル全部が無音（c1 は鳴り同じラッチ）。 */
			if (psgOnly_ && hi >= 3u)
				playSongLatch_ = 0;
		}
		if (ametruckPortF_ && mid == 0 && !wibarmPortF_ && !falcomPortF_
			&& !laplaceCtcF_ && !ys2Mirror4000_) {
			/* ポート 1 = ファイル 0-2（< 3 のまま）。ポート F = hi のバリアント */
			playSongLatch_ = (uint8_t)lo;
			playSongLatchF_ = (uint8_t)hi;
		}
		/* Telenet luxsor/yakyufan PATCH `IN A,(0F); CALL play`。Play は A を $EA1D/$EA18 に格納し ix+23 へコピー。0 = 表+2 で再起動（無限ループ）。Hoot の ioport[0x0F] は書かれないので 0 のまま。ここで曲 ID を返すと BGM が N 回ラップして STOPS（seq=2）。 */
		if (initPc_ == 0xE900 && opmPlayGate_ && !ametruckPortF_
			&& !wibarmPortF_ && !falcomPortF_ && !laplaceCtcF_
			&& !ys2Mirror4000_)
			playSongLatchF_ = 0;
		if (initPc_ == 0xFE00) {
			playSongLatch_ = (uint8_t)lo;
			const unsigned nibble = lo & 0x0fu;
			if (nibble < 128 && bgmPresent_[nibble] && bgmBank_[nibble])
				bank = (uint8_t)nibble;
		}
		/* x1sc EFC00P／euphory PSG: 載せた各 MUS/DEM は 1 曲。0x000000NN の Unpack は song=NN で範囲外。Play 0。 */
		if (psgOnly_ && mid == 0 && hi == 0) {
			if ((initPc_ == 0xF000 && mdataAddr_ == 0x5000 && mem_[0xEB00] != 0)
				|| (initPc_ == 0xF000 && mdataAddr_ == 0x9000
					&& mdataSize_ == 0x1000))
				playSongLatch_ = 0;
		}
		/* x1sc DRIVER $3E5B は 0704 CTC リードバックを「OPM ボード」と見て $0003 に 1 を格納。ISR/play が FM 経路を取り 0700 を書くが psgOnly_ は無視。EFC00P は $48 開始。 */
		if (psgOnly_ && initPc_ == 0xF000 && mdataAddr_ == 0x5000
			&& mem_[0xEB00] == 0x48)
			mem_[0x0003] = 0;
		/* songIdFromHi_／falcom 奪取のあと再アサート */
		if (X1IsPwmajanPatch(mem_) && mid == 0 && lo >= 1u && lo <= 5u) {
			playSongLatch_ = (uint8_t)lo;
			playSongLatchF_ = (uint8_t)hi;
		}
		if (X1IsSghostPatch(mem_) && mid == 0) {
			playSongLatch_ = (uint8_t)lo;
			playSongLatchF_ = (uint8_t)hi;
		}
		if (X1IsYs1Patch(mem_) && mid == 0 && !ys2Mirror4000_ && !falcomPortF_) {
			playSongLatchF_ = (uint8_t)hi;
			/* ポート 1 上位ニブル: $00 ゲーム内、$20 タイトル、$30 エンディング。ゲーム内トラック 0 でも非ゼロ ポート 1 が要る。PATCH の `IN A,(1); AND F0; CP 20` が $4D00 経路に留まる。 */
			if (lo >= 0x20u)
				playSongLatch_ = (uint8_t)lo;
			else
				playSongLatch_ = hi ? (uint8_t)hi : (lo ? (uint8_t)lo : 1);
		}
		if (X1IsGandPatch(mem_) && mid == 0 && !ys2Mirror4000_ && !falcomPortF_) {
			playSongLatch_ = (uint8_t)lo;
			playSongLatchF_ = (uint8_t)hi;
		}
	}
	playCmdHoldIrqs_ = 90; /* 約 1.5s hold。遅い PATCH poll が OUT0／クリア前に cmd を見る */
	ydosCmdSeen_ = 0;
	ydosInhibitReentry_ = 0;
	{
		int mailboxFree = 1;
		if (initPc_ >= 0xC000 && initPc_ < 0xC100)
			mailboxFree = 0;
		/* gaia/hayato: mdata_addr=0xC000 — C010/C011 を poke すると BGM ヘッダが壊れる（音楽がメールボックス上へ C0/1A を書き戻す）。ポートラッチのみ。 */
		if (mdataAddr_ <= PLAY_FLAG
			&& (unsigned)mdataAddr_ + mdataSize_ > (unsigned)PLAY_FLAG)
			mailboxFree = 0;
		if (mailboxFree) {
			mem_[PLAY_FLAG] = 0x01;
			mem_[PLAY_CODE] = playSongLatch_;
		}
	}

	uint8_t stage = bank;
	if (!(stage < 128 && bgmPresent_[stage] && bgmBank_[stage])) {
		if (song < 128 && bgmPresent_[song] && bgmBank_[song])
			stage = song;
		else
			stage = 0xff;
	}
	if (stage < 128 && bgmPresent_[stage] && bgmBank_[stage]) {
		if ((mdataAddr_ == 0x4000 || mdataAddr_ == 0) && initPc_ != 0xFE00) {
			unsigned n = bgmBankSize_[stage];
			if (n > mdataSize_) n = mdataSize_;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			memset(mem_ + 0x4000, 0, n);
			memcpy(mem_ + 0x4000, bgmBank_[stage], n);
		}
		if (!skipTriggerStage_)
			StageBgm(stage);
	}
	/* pwmajan SASURAI/POCO: PATCH LDIR $6000→$4800/$5000 がソースと重なるので、ホストが再生番地へコピーしそれらの LDIR を NOP。 */
	if (X1IsPwmajanPatch(mem_)) {
		const unsigned lo = titleCode & 0xffu;
		uint16_t dest = 0;
		unsigned cap = 0;
		if (lo == 4u) {
			dest = 0x4800;
			cap = 0x3000;
		} else if (lo == 5u) {
			dest = 0x5000;
			cap = 0xA800u;
		}
		if (dest && lo < 128 && bgmPresent_[lo] && bgmBank_[lo]) {
			unsigned n = bgmBankSize_[lo];
			if (n > cap) n = cap;
			if ((unsigned)dest + n > 0x10000u)
				n = 0x10000u - dest;
			memcpy(mem_ + dest, bgmBank_[lo], n);
		}
	}
	/* gand MENU: play ptr は PATCH LDDR $1200→$2D00 のあと $4B14+。バンクバッファからコピー（memcpy $1200→$2D00 は重なる）。ポート F=1 はその LDDR を飛ばすのでホストコピー（と $7939 RET）が残る。 */
	if (X1IsGandPatch(mem_) && mdataAddr_ == 0x1200) {
		const unsigned lo = titleCode & 0xffu;
		if (lo >= 5u && lo <= 0x0Au && lo < 128 && bgmPresent_[lo] && bgmBank_[lo]) {
			unsigned n = bgmBankSize_[lo];
			if (n > 0x4D00u) n = 0x4D00u;
			memcpy(mem_ + 0x2D00, bgmBank_[lo], n);
		}
		if (lo == 0x0Au && bgmPresent_[0x0B] && bgmBank_[0x0B]) {
			unsigned n = bgmBankSize_[0x0B];
			if (0x9500u + n > 0x10000u)
				n = 0x10000u - 0x9500u;
			memcpy(mem_ + 0x9500, bgmBank_[0x0B], n);
		}
		if (lo >= 5u && lo <= 0x0Au)
			playSongLatchF_ = 1;
		if (lo == 0x0Au) {
			/* $7939/$7942 F9 hooks: JP $95B3 is game fade ($9C34/$38CF)
			   and kills OPM mix. RET keeps catalog 天上界へ in DRIVER. */
			mem_[0x7939] = 0xC9;
			mem_[0x7942] = 0xC9;
		}
	}
	ArmTelenetPlayGate();
}

/* CHardX1::OpmWrites の実装 */
unsigned CHardX1::OpmWrites() const
{
	if (chipOpn_) {
		unsigned w = 0;
		CEmuChipYm2608GetPlayMetrics(chipOpn_, &w, NULL, NULL, NULL, NULL);
		return w;
	}
	return chipOpm_ ? CEmuChipYm2151WriteCount(chipOpm_) : 0;
}

/* CHardX1::AyWrites の実装 */
unsigned CHardX1::AyWrites() const
{
	return chipAy_ ? CEmuChipAyWriteCount(chipAy_) : 0;
}

/* zip から ROM／曲データを載せる */
int CHardX1::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge || !cpu_) return 0;
	titleCode_ = titleCode;
	memset(mem_, 0, sizeof(mem_));
	memset(ioport_, 0, sizeof(ioport_));
	FreeBanks();
	CtcReset();
	psgStatToggle_ = 0;
	for (int i = 0; i < 4; i++)
		xmlCtcVec_[i] = -1;
	/* hoot X1 ドライバ use_ctcN／ゲームオプション ctcN = そのチャネルの IM2 ベクタ */
	{
		static const char* kNames[4] = { "ctc0", "ctc1", "ctc2", "ctc3" };
		static const char* kUseNames[4] = { "use_ctc0", "use_ctc1", "use_ctc2", "use_ctc3" };
		for (int ch = 0; ch < 4; ch++) {
			int v = CEmuParseOptHex(ge, kNames[ch], -1);
			if (v < 0) v = CEmuParseOptHex(ge, kUseNames[ch], -1);
			/* hoot の use_ctcN 値 0 は無効 — 既定を保つ */
			if (v > 0)
				xmlCtcVec_[ch] = v & 0xff;
		}
	}
	int loadedCode = 0;
	ydosRom_ = (uint8_t)(GeHasYdosRom(ge) ? 1 : 0);
	opmPlayGate_ = 0;
	opmPlayTempo_ = 0;
	tecnoCmdHi_ = 0;
	jesusSplitPorts_ = 0;
	songIdFromHi_ = 0;

	/* mdata 窓を早く解決し、過大コード（Falcom PR.NO2 @0、mdata@5c00）が Prestage 前に音楽領域へ溢れないようにする */
	mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", 0x4000);
	{
		int ms = CEmuParseOptHex(ge, "mdata_size", (int)BGM_SIZE);
		int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
		if (mfs > ms) ms = mfs;
		if (ms <= 0 || ms > BGM_SIZE) ms = BGM_SIZE;
		mdataSize_ = (unsigned)ms;
	}
	int vdataAddr = CEmuParseOptHex(ge, "vdata_addr", -1);
	int vdataSize = CEmuParseOptHex(ge, "vdata_size", 0);
	if (vdataSize <= 0) vdataSize = CEmuParseOptHex(ge, "vfile_size", 0);
	int hasMdataOpt = 0, hasBgmRom = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "mdata_addr") == 0) {
			hasMdataOpt = 1;
			break;
		}
	}
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "bgm") == 0) {
			hasBgmRom = 1;
			break;
		}
	}

	for (int pass = 0; pass < 2; pass++) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		const int isVoice = (_stricmp(r->type, "voice") == 0 || _stricmp(r->type, "vdata") == 0);
		const int isCode = (_stricmp(r->type, "code") == 0);
		/* Pass0: ボイス下地（Falcom）。Pass1: code/data/bgm — コードが勝つ */
		if (pass == 0 && !isVoice) continue;
		if (pass == 1 && isVoice) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		if (isCode) {
			int off = r->offset;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			/* ブロブが侵入するときコードを mdata_addr より下へクランプ（xana2 PR.NO2 24K@0 vs mdata@5c00）。ボイス窓が同じオフセット開始なら vdata_size を優先。カタログ窓も bgm も無い既定 mdata $4000（aspic PSG PROG @0FD0）はプレーヤを $581E に残す。 */
			if ((hasMdataOpt || hasBgmRom) && (int)mdataAddr_ > off) {
				unsigned cap = (unsigned)((int)mdataAddr_ - off);
				if (vdataAddr == off && vdataSize > 0 && (unsigned)vdataSize < cap)
					cap = (unsigned)vdataSize;
				if (n > cap) n = cap;
			}
			memcpy(mem_ + off, data, n);
			loadedCode++;
		} else if (_stricmp(r->type, "data") == 0) {
			/* カタログ「data」は hoot IO 窓。YDOS は CIM を Z80 RAM としても LD — 安全ならミラー。mucom 0x4000 IO 窓への無条件ミラーは jesus/sghost/zeliard を壊した（high opmW、peak=0）。Gen1 sangoku は load 時 OVL-1 印無しで OPMDAT.CIM を B400 に置く（復号は後）。空 dest＋非 4000 でキーする。 */
			int off = r->offset;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			memcpy(ioport_ + off, data, n);
			int ydos = X1IsYdos(this);
			int highCim = (off != 0x4000);
			if (ydos || highCim) {
				int unused = 1;
				unsigned probe = n < 64u ? n : 64u;
				for (unsigned i = 0; i < probe; i++) {
					if (mem_[off + i]) { unused = 0; break; }
				}
				if (unused)
					memcpy(mem_ + off, data, n);
			}
			if (ydos && !bgmPresent_[0] && n > 0) {
				unsigned bn = n;
				if (bn > (unsigned)BGM_SIZE) bn = (unsigned)BGM_SIZE;
				unsigned char* buf = (unsigned char*)malloc(BGM_SIZE);
				if (buf) {
					memset(buf, 0, BGM_SIZE);
					memcpy(buf, data, bn);
					bgmBank_[0] = buf;
					bgmBankSize_[0] = bn;
					bgmPresent_[0] = 1;
				}
			}
		} else if (_stricmp(r->type, "bgm") == 0) {
			int idx = r->offset;
			if (idx < 0 || idx >= 128) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(BGM_SIZE);
			if (!buf) continue;
			memset(buf, 0, BGM_SIZE);
			memcpy(buf, data, n);
			if (bgmBank_[idx]) free(bgmBank_[idx]);
			bgmBank_[idx] = buf;
			bgmBankSize_[idx] = n;
			bgmPresent_[idx] = 1;
		} else if (isVoice) {
			/* Falcom: 選択トラックだけ vdata_addr にボイス下地。コードパスがその上へ PR.NO0 等を載せる（xana2opm）。 */
			int vaddr = vdataAddr;
			int vsize = vdataSize;
			if (vsize <= 0) vsize = CEmuParseOptHex(ge, "vfile_size", (int)sz);
			if (vaddr < 0) continue;
			uint8_t song = 0, bank = 0;
			UnpackTitle(titleCode, &song, &bank, ydosRom_);
			int idx = r->offset;
			if (idx < 0) idx = 0;
			if (idx != (int)song && idx != (int)bank)
				continue;
			int dest = vaddr;
			if (dest < 0 || dest >= 0x10000) continue;
			unsigned n = sz;
			if (vsize > 0 && (unsigned)vsize < n) n = (unsigned)vsize;
			if (dest + (int)n > 0x10000) n = (unsigned)(0x10000 - dest);
			if ((int)mdataAddr_ > dest) {
				unsigned cap = (unsigned)((int)mdataAddr_ - dest);
				if (n > cap) n = cap;
			}
			memcpy(mem_ + dest, data, n);
		}
	}
	} /* パス */

	/* Falcom xana2 PSG: カタログコードは常に PR.NO2 @0。系統 hi>=3 は一致するボイス PR.NOx をプレーヤとして残す — pass1 が PR.NO2 を載せボス／エンディングが別エンジン（SILENT）。OPM xml は PR.NO0 @ $1000。$0000 へ PR.NO3 の 0x5c00 を載せるとその stub が消える（系統 3 タイトル全部 SILENT）。 */
	if (psgOnly_ && vdataAddr == 0 && vdataSize > 0) {
		const unsigned lo = titleCode & 0xffu;
		const unsigned hi = (titleCode >> 24) & 0xffu;
		const unsigned mid = (titleCode >> 8) & 0xffffu;
		if (mid == 0 && hi >= 3u && hi <= 5u) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "voice") != 0 && _stricmp(r->type, "vdata") != 0)
					continue;
				if (r->offset != (int)lo)
					continue;
				unsigned sz = 0;
				const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
				if (!data || !sz)
					break;
				unsigned n = sz;
				if ((unsigned)vdataSize < n)
					n = (unsigned)vdataSize;
				if ((int)mdataAddr_ > 0 && (unsigned)mdataAddr_ < n)
					n = (unsigned)mdataAddr_;
				if (n > 0x10000u)
					n = 0x10000u;
				memcpy(mem_, data, n);
				break;
			}
		}
	}

	if (!loadedCode) return 0;

	/* 開始 PC を解決: 明示 init_pc → PATCH コードオフセット → 最初のコード → C000 */
	{
		int hasInit = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "init_pc") == 0) { hasInit = 1; break; }
		}
		if (hasInit) {
			initPc_ = (uint16_t)CEmuParseOptHex(ge, "init_pc", 0xC000);
		} else {
			int patchOff = -1, firstCode = -1;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (firstCode < 0) firstCode = r->offset;
				if (_stricmp(r->name, "PATCH") == 0)
					patchOff = r->offset;
			}
			if (patchOff >= 0)
				initPc_ = (uint16_t)patchOff;
			else if (firstCode >= 0)
				initPc_ = (uint16_t)firstCode;
			else
				initPc_ = 0xC000;
		}
		mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", 0x4000);
		{
			int ms = CEmuParseOptHex(ge, "mdata_size", (int)BGM_SIZE);
			int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
			if (mfs > ms) ms = mfs;
			if (ms <= 0 || ms > BGM_SIZE) ms = BGM_SIZE;
			mdataSize_ = (unsigned)ms;
		}
		/* mdata_addr より上の最寄りコードブロブが StageBgm 書きを上限にする。mdata_addr==0 で mdata 窓内のコードはオーバーレイ stub（xanaopm PR.NO0 @1000）— PATCH がプレーヤを $F000 へコピーしたあと音楽が置換する想定。crimson VOICE@6200 と mdata@4000 はまだ上限が要る。 */
		stageLimit_ = 0x10000u;
		skipPrestageRam_ = 0;
		skipTriggerStage_ = 0;
		ys2Mirror4000_ = 0;
		laplaceCtcF_ = 0;
		wibarmPortF_ = 0;
		falcomPortF_ = 0;
		ametruckPortF_ = 0;
		marsHoldIrq_ = 0;
		marsSeenProg_ = 0;
		marsPlayReady_ = 0;
		bgmStageOff_ = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0) continue;
			if (_stricmp(r->name, "PATCH") == 0) {
				if (r->offset > (int)mdataAddr_ && r->offset < (int)stageLimit_)
					stageLimit_ = (uint32_t)r->offset;
				continue;
			}
			if (r->offset == (int)mdataAddr_ && mdataAddr_ != 0) {
				/* produce: mdata 上の PROG1 はプレーヤであり音楽オーバーレイ先ではない。ここで PROG4 を載せると JP $0285 が消える。 */
				skipPrestageRam_ = 1;
				skipTriggerStage_ = 1;
			}
			if (mdataAddr_ == 0 && r->offset > 0 && r->offset < (int)mdataSize_) {
				skipPrestageRam_ = 1;
				continue; /* 音楽窓内のオーバーレイ stub */
			}
			/* gand OPENING は code@$3900 と bgm バンク 1-3。3900 で上限すると MENU（0x4D00）が切れ曲 5-0A が $4B14+ に届かない。 */
			{
				int alsoBgm = 0;
				for (int j = 0; j < ge->romCount; j++) {
					if (_stricmp(ge->rom[j].type, "bgm") != 0) continue;
					if (_stricmp(ge->rom[j].name, r->name) == 0) {
						alsoBgm = 1;
						break;
					}
				}
				if (alsoBgm)
					continue;
			}
			if (r->offset > (int)mdataAddr_ && r->offset < (int)stageLimit_)
				stageLimit_ = (uint32_t)r->offset;
		}
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0) continue;
			if (_stricmp(r->name, "PATCH") != 0) continue;
			if (r->offset < 0 || r->offset >= 0x10000) break;
			unsigned plen = 256u;
			if (r->offset + (int)plen > 0x10000)
				plen = (unsigned)(0x10000 - r->offset);
			opmPlayGate_ = CEmuX1TelenetPlayGate(mem_, (unsigned)r->offset, plen);
			opmPlayTempo_ = CEmuX1TelenetPlayTempo(mem_, opmPlayGate_);
			tecnoCmdHi_ = (uint8_t)CEmuX1TecnoCmdHi(mem_, (unsigned)r->offset, plen);
			jesusSplitPorts_ = (uint8_t)CEmuX1JesusSplit(mem_, (unsigned)r->offset, plen);
			ys2Mirror4000_ = (uint8_t)CEmuX1Ys2Mirror4000(mem_, (unsigned)r->offset, plen);
			laplaceCtcF_ = (uint8_t)CEmuX1LaplaceCtcF(mem_, (unsigned)r->offset, plen);
			wibarmPortF_ = (uint8_t)CEmuX1WibarmPortF(mem_, (unsigned)r->offset, plen);
			falcomPortF_ = (uint8_t)CEmuX1FalcomLoTrack(mem_, (unsigned)r->offset, plen);
			ametruckPortF_ = (uint8_t)CEmuX1AmetruckPortF(mem_, (unsigned)r->offset, plen);
			if (X1IsPwmajanPatch(mem_)) {
				/* CALL $40F3 の間 PROG2 を残す。TriggerPlay が載せる */
				skipPrestageRam_ = 1;
				/* 重なる SASURAI/POCO LDIR（ED B0）を NOP */
				mem_[0xF079] = mem_[0xF07A] = 0;
				mem_[0xF088] = mem_[0xF089] = 0;
			}
			if (X1IsGandPatch(mem_)) {
				/* ブート `CALL $390E` はコード OPENING が要る。mdata の MENU は 0x4D00 で Prestage 中に $3900 を消す。 */
				skipPrestageRam_ = 1;
			}
			/* OPENING は mdata $1000 に居るので produce 風 skipTriggerStage_ ラッチが立つ。各ファイルがプレーヤ — HISCORE/ENDING は $1000 へオーバーレイ必須。`LD A,C9; LD ($107C),A` も NOP し CALL play がファイル内ループを走る（ここは CTC3 が IM2 ISR を tick しない）。 */
			if (ametruckPortF_) {
				skipTriggerStage_ = 0;
				if (mem_[0x0068] == 0x3E && mem_[0x0069] == 0xC9
					&& mem_[0x006A] == 0x32)
					memset(mem_ + 0x0068, 0x00, 5);
				/* OPENING のファイル内 JR はここへ戻らない。HISCORE は既に $1321 に C9、ENDING は RET NZ — どちらも CTC3 IM2 tick が要る。このゲストは届かない（ch0 に TC が無く ZC0→TRG3 が idle）。代わりに PATCH ISR を poll。 */
				if (mem_[0x0075] == 0x21 && mem_[0x0076] == 0x38
					&& mem_[0x0077] == 0x07) {
					mem_[0x0075] = 0xF3; /* DI 命令 */
					mem_[0x0076] = 0xCD;
					mem_[0x0077] = 0xA3;
					mem_[0x0078] = 0x00; /* 命令 CALL $00A3 */
					mem_[0x0079] = 0x18;
					mem_[0x007A] = 0xFB; /* 命令 JR $0076 */
				}
			}
			break;
		}
		/* ys2 PSG は PATCH2（同じ C000→4000 LDIR）。上の PATCH のみスキャンは見ないので TTLMS パックと $2F0E NOP が武装せず、port1 が $30（タイトル）になり SSG 音量が 0 のまま。 */
		if (!ys2Mirror4000_ && psgOnly_) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (_stricmp(r->name, "PATCH2") != 0) continue;
				if (r->offset < 0 || r->offset >= 0x10000) break;
				unsigned plen = 256u;
				if (r->offset + (int)plen > 0x10000)
					plen = (unsigned)(0x10000 - r->offset);
				ys2Mirror4000_ = (uint8_t)CEmuX1Ys2Mirror4000(mem_,
					(unsigned)r->offset, plen);
				break;
			}
		}
		/* kugyoku PSG は OPM PATCH と同じ `CP FF; AND 0F` コマンドニブルの PATCH2。PATCH のみスキャンは tecnoCmdHi_ を武装せず、0x80/0x81/0xFF タイトルが lo をトラックとしてラッチ（5 の AND 0F は 1 トラックファイルのトラック 5 → SILENT）。 */
		if (!tecnoCmdHi_) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (_stricmp(r->name, "PATCH2") != 0) continue;
				if (r->offset < 0 || r->offset >= 0x10000) break;
				unsigned plen = 256u;
				if (r->offset + (int)plen > 0x10000)
					plen = (unsigned)(0x10000 - r->offset);
				tecnoCmdHi_ = (uint8_t)CEmuX1TecnoCmdHi(mem_,
					(unsigned)r->offset, plen);
				break;
			}
		}
		/* ASPIC SPECIAL PSG PATCH2: ISR $0089 が $0097=0 の間毎 VSYNC に CALL $0071（stop）。tick 毎 stop を NOP し play が武装できるようにする。PROG play $581E は `POP AF; OR A; RET Z` で終わるが PATCH2 は push しない — 戻りを pop し RET Z で $0000 へ。RET。 */
		if (psgOnly_ && initPc_ == 0
			&& mem_[0x0000] == 0xF3 && mem_[0x0001] == 0xED
			&& mem_[0x0013] == 0x32 && mem_[0x0014] == 0x32
			&& mem_[0x0015] == 0x58
			&& mem_[0x0089] == 0xCD && mem_[0x008A] == 0x71
			&& mem_[0x008B] == 0x00)
			memset(mem_ + 0x0089, 0x00, 3);
		if (psgOnly_ && initPc_ == 0
			&& mem_[0x581E] == 0x26 && mem_[0x581F] == 0x00
			&& mem_[0x5835] == 0xF1 && mem_[0x5836] == 0xB7
			&& mem_[0x5837] == 0xC8) {
			mem_[0x5835] = 0xC9;
			mem_[0x5836] = 0x00;
			mem_[0x5837] = 0x00;
		}
		songIdFromHi_ = (uint8_t)CEmuX1SongIdFromHi(mem_);
		/* hyd2: flag 0（イントロ＋ループ）はタイムアウトで CALL $FF46。それは CALL $FEAB = JP $81F2 で JP $8170（ループ）に届かない。イントロ飛ばしは既に CALL $8170 して PLAYS。 */
		if (initPc_ == 0xFE00
			&& mem_[0xFF46] == 0xCD && mem_[0xFF47] == 0xAB
			&& mem_[0xFF48] == 0xFE && mem_[0xFF4F] == 0xC3
			&& mem_[0xFF50] == 0x70 && mem_[0xFF51] == 0x81) {
			/* CALL $FEAB = JP $81F2 は JP $8170 へ戻らない。$0744 カウントダウンは触らない — 縮めると ISR から JP $8170 し flag 0 が mute。イントロラップがループを運ぶ必要がある。 */
			memset(mem_ + 0xFF46, 0x00, 3);
		}
		/* luxsor OPMDRV/PSGDRV play はワーク RAM を LDIR したあと `XOR A; LD ($EA0F),A` で ArmTelenetPlayGate がちょうどプライムした ISR ゲートをクリア。ISR `LD A,($EA0F); OR A; RET Z` はそれ以降 tick しない。 */
		if (initPc_ == 0xE900 && opmPlayGate_ == 0xEA0F) {
			if (mem_[0xECCD] == 0x32 && mem_[0xECCE] == 0x0F
				&& mem_[0xECCF] == 0xEA)
				memset(mem_ + 0xECCD, 0x00, 3);
			if (mem_[0xEAE9] == 0xAF && mem_[0xEAEA] == 0x32
				&& mem_[0xEAEB] == 0x0F && mem_[0xEAEC] == 0xEA)
				memset(mem_ + 0xEAEA, 0x00, 3);
			/* `LD A,($EA1D/$EA18); LD (IX+23),A` — 0 = 表+2 の無限ループ。ポート F が曲 ID を給電していたので BGM が N 回ラップして STOPS。IN (F) がまだ ID を見ても A=0 を強制。 */
			if (mem_[0xED29] == 0x3A && mem_[0xED2A] == 0x1D
				&& mem_[0xED2B] == 0xEA && mem_[0xED2C] == 0xDD
				&& mem_[0xED2D] == 0x77 && mem_[0xED2E] == 0x17) {
				mem_[0xED29] = 0xAF;
				mem_[0xED2A] = 0x00;
				mem_[0xED2B] = 0x00;
			}
			if (mem_[0xEB41] == 0x3A && mem_[0xEB42] == 0x18
				&& mem_[0xEB43] == 0xEA && mem_[0xEB44] == 0xDD
				&& mem_[0xEB45] == 0x77 && mem_[0xEB46] == 0x17) {
				mem_[0xEB41] = 0xAF;
				mem_[0xEB42] = 0x00;
				mem_[0xEB43] = 0x00;
			}
			/* ISR `INC ($EA1E)` はゲーム用 128 tick 終了フラグだが、$EC53 `LD A,($EA1E); ADD A,D` は同じバイトを TL 加算に使う。約 128 音楽 tick 後 D が $7F（mute）へクランプしたまま — キーは動くが STOPS/GAPPY。Hoot はインクリメントしない（ioport BSS は 0）。 */
			if (mem_[0xEEE5] == 0x21 && mem_[0xEEE6] == 0x1E
				&& mem_[0xEEE7] == 0xEA && mem_[0xEEE8] == 0x34)
				mem_[0xEEE8] = 0x00;
			/* `DEC (IX+7)` は TL エンベロープ添字。$E1 がリロードするが、$EA0F が NZ の間 ISR は $E1 を飛ばす（ArmTelenet）。15 音楽 tick 後添字が表の 127 スロットに当たり、キーは動くが BGM が聞こえない（STOPS/GAPPY MON_OK）。 */
			if (mem_[0xEF0A] == 0xDD && mem_[0xEF0B] == 0x35
				&& mem_[0xEF0C] == 0x07)
				memset(mem_ + 0xEF0A, 0x00, 3);
			/* PSGDRV `$EA72` は ix+7 から AY 音量を計算し `LD A,($EA0F); OR A; RET NZ` — ArmTelenet がゲートを 1 に保つので書きが起きない（ayW>0、peak=0）。OPMDRV にこの列はない。 */
			if (mem_[0xEA79] == 0x3A && mem_[0xEA7A] == 0x0F
				&& mem_[0xEA7B] == 0xEA && mem_[0xEA7C] == 0xB7
				&& mem_[0xEA7D] == 0xC0 && mem_[0xEA7E] == 0xDD
				&& mem_[0xEA7F] == 0x72 && mem_[0xEA80] == 0x13)
				mem_[0xEA7D] = 0x00;
			/* 同じドライバの `$ED07` エンベロープ `DEC D; JP P; LD D,0` は約 2.5s で ix+19 を mute へ減衰。キーオンレベルを保持。 */
			if (mem_[0xED1C] == 0x15 && mem_[0xED1D] == 0xF2
				&& mem_[0xED1E] == 0x22 && mem_[0xED1F] == 0xED
				&& mem_[0xED20] == 0x16 && mem_[0xED21] == 0x00
				&& mem_[0xED22] == 0xDD && mem_[0xED23] == 0x72
				&& mem_[0xED24] == 0x13)
				memset(mem_ + 0xED1C, 0x00, 6);
		}
		/* ys_x1: MANPR1 @ $0100 は 0x4C00 で $4000 を覆う。タイトル／エンディングエンジンがそこ（TTLPRG／ENDPRG）。コードパス後に戻し、PATCH の LDIR $4000→$8800／CALL $4A2E が正しいプレーヤを見る。 */
		if (X1IsYs1Patch(mem_) && vdataAddr >= 0) {
			const unsigned lo = titleCode & 0xffu;
			const char* vn = NULL;
			if (lo == 0x20u) vn = "TTLPRG";
			else if (lo == 0x30u) vn = "ENDPRG";
			if (vn) {
				unsigned vsz = 0;
				const unsigned char* vp = CEmuZipFsFind(fs, vn, &vsz);
				if (vp && vsz) {
					unsigned n = vsz;
					if (vdataSize > 0 && (unsigned)vdataSize < n)
						n = (unsigned)vdataSize;
					if (vdataAddr + (int)n > 0x10000)
						n = (unsigned)(0x10000 - vdataAddr);
					memcpy(mem_ + vdataAddr, vp, n);
				}
			}
		}
		if (ys2Mirror4000_ && vdataAddr >= 0) {
			unsigned msz = 0;
			const unsigned char* man = CEmuZipFsFind(fs, "MANPR1", &msz);
			if (man && msz) {
				unsigned n = msz;
				if (vdataSize > 0 && (unsigned)vdataSize < n)
					n = (unsigned)vdataSize;
				if (vdataAddr + (int)n > 0x10000)
					n = (unsigned)(0x10000 - vdataAddr);
				memcpy(mem_ + vdataAddr, man, n);
				/* モード 0（cmd < $20）は完全なゲーム内プレーヤが要る。TTLPRG オーバーレイはタイトル画面モード 1 のみ。TTLMSn（lo>=$30）はゲーム内再生なので MANPR1 を残す。エンディング（lo==$20）は CALL $16C6／JP $1A8F をパッチし LDIR C000→$2800 — それらは ENDPRG に着地し TTLPRG ではない。 */
				const unsigned hi = (titleCode >> 24) & 0xffu;
				const unsigned lo = titleCode & 0xffu;
				const unsigned cmd = hi ? hi : lo;
				if (lo == 0x20u) {
					unsigned esz = 0;
					const unsigned char* endp = CEmuZipFsFind(fs, "ENDPRG", &esz);
					if (endp && esz) {
						unsigned en = esz;
						if (vdataSize > 0 && (unsigned)vdataSize < en)
							en = (unsigned)vdataSize;
						if (vdataAddr + (int)en > 0x10000)
							en = (unsigned)(0x10000 - vdataAddr);
						memcpy(mem_ + vdataAddr, endp, en);
					}
					/* 最初のコマンドは CALL $00AE/$00C2 = JP $2F91。エンディング表がその stub を $1A8F へ書き換える前。$2F91 は $2800 BGM 窓内なので植えた RET は StageBgm に消される。PATCH の CALL を NOP（止める前曲は無い）。 */
					if (mem_[0x0026] == 0xCD && mem_[0x0027] == 0xAE
						&& mem_[0x0028] == 0x00)
						memset(mem_ + 0x0026, 0x00, 3);
					if (mem_[0x002B] == 0xCD && mem_[0x002C] == 0xC2
						&& mem_[0x002D] == 0x00)
						memset(mem_ + 0x002B, 0x00, 3);
				} else if (cmd >= 0x20u && lo < 0x30u) {
					for (int i = 0; i < ge->romCount; i++) {
						const CEmuRomEntry* r = &ge->rom[i];
						if (_stricmp(r->type, "code") != 0) continue;
						if (_stricmp(r->name, "TTLPRG") != 0) continue;
						unsigned tsz = 0;
						const unsigned char* ttl = CEmuZipFsFind(fs, r->name, &tsz);
						if (!ttl || !tsz) break;
						int off = r->offset;
						if (off < 0) off = 0;
						unsigned tn = tsz;
						if (off + (int)tn > 0x10000)
							tn = (unsigned)(0x10000 - off);
						memcpy(mem_ + off, ttl, tn);
						break;
					}
				}
			}
		}
		/* ys2 PATCH2 はブート時とタイトル経路で `LD A,$C9; LD ($2F0E),A` を植える。$2F0E は `POP AF; RET`。RET に置換すると push した AF が漏れ SSG 音量経路が走らない（ayW>0、mixer=$38、vols=0）。ゲーム内 `$39D4=C9`（OPM ポート 0700）は残す。OPM PATCH にこのストアは無い。 */
		if (psgOnly_ && ys2Mirror4000_) {
			if (mem_[0x0010] == 0x3E && mem_[0x0011] == 0xC9
				&& mem_[0x0012] == 0x32 && mem_[0x0013] == 0x0E
				&& mem_[0x0014] == 0x2F)
				memset(mem_ + 0x0012, 0x00, 3);
			if (mem_[0x0050] == 0x3E && mem_[0x0051] == 0xC9
				&& mem_[0x0052] == 0x32 && mem_[0x0053] == 0x0E
				&& mem_[0x0054] == 0x2F)
				memset(mem_ + 0x0052, 0x00, 3);
		}
		/* 未パッチ mars ISR: IN (1A01); JP P,$4208; IN; JP M,$420D */
		if (mem_[0x420A] == 0xF2 && mem_[0x420B] == 0x08 && mem_[0x420C] == 0x42
			&& mem_[0x420F] == 0xFA && mem_[0x4210] == 0x0D && mem_[0x4211] == 0x42)
			marsHoldIrq_ = 1;
	}

	cpu_->reset(mem_);
	cpu_->r.pc = initPc_;
	cpuCycles_ = 0;
	if (chipOpm_) chipOpm_->Reset();
	if (chipOpn_) chipOpn_->Reset();
	if (chipAy_) chipAy_->Reset();
	/* ここで C010-C012 を poke しない — Play()／TriggerPlay 用に予約 */
	return 1;
}

/* CEmuHardX1SetActive の実装 */
void CEmuHardX1SetActive(CHardX1* hw)
{
	CEmuZ80BusSetActive(hw);
}
