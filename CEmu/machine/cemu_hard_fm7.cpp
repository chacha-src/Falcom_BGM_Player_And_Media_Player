#include "StdAfx.h"
#include "cemu_hard_fm7.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_ay.h"
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

enum {
	FM7_CPU_HZ = 2000000,
	/* PC-88 OPN マスタに合わせる。FM77AV 基板クロックは機種差 — ymfm @ /4 で可聴。 */
	FM7_OPN_HZ = 1228800, /* FM-7 / FM77AV YM2203（3.9936MHz ではない — ys2 が速くなった） */
	/* CEmuChipAy は hoot の単独 PSG /2 分周を内部適用。FM-7 の 2.4576 MHz 源を渡し、合成 PSG クロックを 1.2288 MHz にする。ここで 1.2288 MHz を渡すと Jikochu が 1 オクターブ遅くなる。 */
	FM7_AY_HZ = 2457600,
	FM7_FD_PSG_ADDR = 0xFD0D,
	FM7_FD_PSG_DATA = 0xFD0E,
	FM7_FD_OPN_ADDR = 0xFD15,
	FM7_FD_OPN_DATA = 0xFD16,
	FM7_FD_IRQEN = 0xFD02,
	FM7_FD_IRQST = 0xFD03,
	FM7_FD_SUBINTF = 0xFD05, /* bit7: サブCPU busy（R）／halt 要求（W） */
	FM7_FD_PLAY_CMD = 0xFD58,
	FM7_FD_PLAY_SONG = 0xFD59,
	FM7_FD_PLAY_A = 0xFD5A,
	FM7_FD_PLAY_B = 0xFD5B,
	FM7_FD_PLAY_C = 0xFD5C,
	FM7_FD_FALCOM_CMD = 0xFD80,
	FM7_FD_FALCOM_SONG = 0xFD82
};

static CHardFm7* s_activeFm7 = NULL;

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

/* IsFm7Platform の実装 */
static int IsFm7Platform(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "fm7") == 0 || _stricmp(ge->platform, "fm77av") == 0
		|| _stricmp(ge->platform, "mucomfm") == 0)
		return 1;
	if (_stricmp(ge->dataDir, "fm7") == 0) return 1;
	return 0;
}

/* PreferOpn の実装 */
static int PreferOpn(const CEmuGameEntry* ge)
{
	if (!ge) return 1;
	if (_stricmp(ge->platform, "fm77av") == 0) return 1;
	if (_stricmp(ge->platform, "fm7") == 0) return 0;
	if (_stricmp(ge->subtype, "opn") == 0 || _stricmp(ge->subtype, "ysav") == 0
		|| _stricmp(ge->subtype, "xanadu") == 0)
		return 1;
	if (_stricmp(ge->subtype, "psg") == 0 || _stricmp(ge->subtype, "ys") == 0
		|| _stricmp(ge->subtype, "xanadu2") == 0)
		return 0;
	/* アーカイブ語幹: *_fmav → OPN、platform 未設定なら *_fm7 → PSG */
	if (ge->archive[0]) {
		const char* a = ge->archive;
		const size_t n = strlen(a);
		if (n >= 5 && _stricmp(a + n - 5, "_fmav") == 0) return 1;
		if (n >= 4 && _stricmp(a + n - 4, "_fm7") == 0) return 0;
	}
	/* mucomfm／dataDir=fm7 で platform 無し: OPN を優先（AV タイトルが多い） */
	return 1;
}

/* IsFalcomSubtype の実装 */
static int IsFalcomSubtype(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "mucomfm") == 0) return 1;
	const char* s = ge->subtype;
	/* asteka2 は Falcom 発売だが古典 $FD58 メールボックス＋APRG バンク。Ys/Xanadu prog/$FD80 ではない — 非 Falcom TriggerPlay 経路に残す。 */
	if (_stricmp(s, "xanadu") == 0 || _stricmp(s, "xanadu2") == 0
		|| _stricmp(s, "ys") == 0 || _stricmp(s, "ysav") == 0)
		return 1;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "prog") == 0)
			return 1;
	}
	return 0;
}

static mc6809byte__t Fm7CpuRead(mc6809__t* cpu, mc6809addr__t addr, bool /* iscode 判定 */)
{
	CHardFm7* hw = (CHardFm7*)(cpu ? cpu->user : NULL);
	if (!hw) return 0xff;
	return hw->MemRead((uint16_t)addr);
}

/* バス書込 */
static void Fm7CpuWrite(mc6809__t* cpu, mc6809addr__t addr, mc6809byte__t data)
{
	CHardFm7* hw = (CHardFm7*)(cpu ? cpu->user : NULL);
	if (!hw) return;
	hw->MemWrite((uint16_t)addr, (uint8_t)data);
}

/* Fm7CpuFault の実装 */
static void Fm7CpuFault(mc6809__t* cpu, mc6809fault__t fault)
{
	if (cpu)
		longjmp(cpu->err, (int)fault);
}

CHardFm7::CHardFm7()
	: cpuHz_(FM7_CPU_HZ)
	, opnHz_(FM7_OPN_HZ)
	, ayHz_(FM7_AY_HZ)
	, useOpn_(1)
	, initPc_(0)
	, patchTableBase_(0)
	, mdataAddr_(0x3000)
	, mdataSize_(0x1000)
	, titleCode_(0)
	, playCmdLatch_(0)
	, playSongLatch_(0)
	, playParamA_(0)
	, playParamB_(0)
	, playParamC_(0)
	, playCmdHold_(0)
	, falcomCmdLatch_(0)
	, falcomSongLatch_(0)
	, falcomCmdHold_(0)
	, fd02_(0)
	, fd03_(0)
	, fd05_(0)
	, fd05HaltSticky_(0)
	, ymIrqSeen_(0)
	, fd03VsyncSet_(0)
	, fd03VsyncClr_(0x08)
	, fd03VsyncPhase_(0)
	, opnDataLatch_(0)
	, psgDataLatch_(0)
	, opnCmd_(0)
	, psgCmd_(0)
	, falcomMode_(0)
	, vdataAddr_(-1)
	, vdataSize_(0)
	, codeHighWater_(0)
	, chipOpn_(NULL)
	, chipAy_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, mmrSeg_(0)
	, mmrWin_(0)
	, mmrMode_(0)
	, mmrAvail_(0)
	, mmrOn_(0)
	, mmrTouched_(0)
	, albatrssMode_(0)
	, albatrssPoll_(0)
	, xana2Tick_(0)
	, xana2Tempo_(0)
{
	hardKind = KIND_FM7;
	memset(mem_, 0, sizeof(mem_));
	memset(mmrRam_, 0, sizeof(mmrRam_));
	memset(mmrBank_, 0, sizeof(mmrBank_));
	memset(&cpu_, 0, sizeof(cpu_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgmPresent_, 0, sizeof(bgmPresent_));
	memset(progBank_, 0, sizeof(progBank_));
	memset(progBankSize_, 0, sizeof(progBankSize_));
	memset(progPresent_, 0, sizeof(progPresent_));
	memset(voiceBank_, 0, sizeof(voiceBank_));
	memset(voiceBankSize_, 0, sizeof(voiceBankSize_));
	memset(voicePresent_, 0, sizeof(voicePresent_));
}

CHardFm7::~CHardFm7()
{
	Shutdown();
}

/* CHardFm7::BindCpuCallbacks の実装 */
void CHardFm7::BindCpuCallbacks()
{
	cpu_.user = this;
	cpu_.read = Fm7CpuRead;
	cpu_.write = Fm7CpuWrite;
	cpu_.fault = Fm7CpuFault;
}

/* チップと CPU を生成する */
int CHardFm7::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge || !IsFm7Platform(ge)) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = FM7_CPU_HZ;
	opnHz_ = FM7_OPN_HZ;
	ayHz_ = FM7_AY_HZ;
	useOpn_ = PreferOpn(ge);
	initPc_ = 0;
	patchTableBase_ = 0;
	mdataAddr_ = 0x3000;
	mdataSize_ = 0x1000;
	falcomMode_ = 0;
	vdataAddr_ = -1;
	vdataSize_ = 0;
	codeHighWater_ = 0;
	xana2Tick_ = 0;
	xana2Tempo_ = 0;
	chipOpn_ = NULL;
	chipAy_ = NULL;
	if (useOpn_) {
		/* ymfm/fmgen ラッパ経由の YM2203（opnaMode=0） */
		chipOpn_ = CEmuChipYm2608Create((uint32_t)opnHz_, 0, sampleRate_);
		if (!chipOpn_) return 0;
	} else {
		chipAy_ = CEmuChipAyCreate((uint32_t)ayHz_, sampleRate_);
		if (!chipAy_) return 0;
	}
	BindCpuCallbacks();
	return 1;
}

/* PCM／コードバンク */
void CHardFm7::FreeBanks()
{
	for (int i = 0; i < 128; i++) {
		if (bgmBank_[i]) {
			free(bgmBank_[i]);
			bgmBank_[i] = NULL;
		}
		bgmBankSize_[i] = 0;
		bgmPresent_[i] = 0;
		if (voiceBank_[i]) {
			free(voiceBank_[i]);
			voiceBank_[i] = NULL;
		}
		voiceBankSize_[i] = 0;
		voicePresent_[i] = 0;
	}
	for (int i = 0; i < PROG_BANKS; i++) {
		if (progBank_[i]) {
			free(progBank_[i]);
			progBank_[i] = NULL;
		}
		progBankSize_[i] = 0;
		progPresent_[i] = 0;
	}
}

/* CHardFm7::HasProgBanks の実装 */
int CHardFm7::HasProgBanks() const
{
	for (int i = 0; i < PROG_BANKS; i++)
		if (progPresent_[i]) return 1;
	return 0;
}

/* チップ／CPU／ROM を破棄する */
void CHardFm7::Shutdown()
{
	if (s_activeFm7 == this)
		s_activeFm7 = NULL;
	FreeBanks();
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
void CHardFm7::StageBgm(uint8_t index)
{
	/* 正確なバンクのみ — bank0 推測はしない */
	uint8_t use = index;
	if (use >= 128 || !bgmPresent_[use] || !bgmBank_[use])
		return;
	unsigned n = bgmBankSize_[use];
	/* カタログ mdata_size がヘッダ窓だけのときフルバンクを優先。laydock XML 窓は $4000+$200 だが MUS は 8K — DRIVER バイナリが $42B8 で終わるのは末尾が曲ワークスペースだから（PATCH の $5E74 表が同じ窓）。短い重なりへの拡張が要る。relics 級 $E000 オーバーレイは上限のまま。 */
	unsigned cap = mdataSize_;
	if (n > cap && mdataAddr_ + n <= 0x10000u
		&& (initPc_ < mdataAddr_ || initPc_ >= mdataAddr_ + n)) {
		const unsigned winEnd = mdataAddr_ + cap;
		const int shortTail = (codeHighWater_ > mdataAddr_
			&& codeHighWater_ <= mdataAddr_ + 0x1000u);
		if (!(codeHighWater_ > winEnd && codeHighWater_ > mdataAddr_) || shortTail) {
			/* laydock: DRIVER BSS $5E74 は生きたチャネル表。8K MUS をそこに拡張するとデータバイトがポインタになり、ISR の LDU #$5E74 / LDY ,U が本物ボイスを見ない（AR/MUL が 0、モニタキー、peak 0）。$5E74 の前で止める。 */
			if (shortTail && mdataAddr_ < 0x5E74u)
				cap = 0x5E74u - mdataAddr_;
			else
				cap = n;
			if (n < cap) cap = n;
		}
	}
	/* Ys PATCH t0 はヘッダページ（MANPR $08=2K、END/OMAKE $10=4K）だがバンクは 3K/8K。フレーズ 2 ポインタはその窓より先（MUSD10B $64A0、ENDMUS $1F60/$3D4A）。codeHighWater_ は $FFxx の PATCH なので上の汎用拡張は上限のまま。 */
	if (falcomMode_ && mdataAddr_ >= 0x3000u && mdataAddr_ < 0xE000u
		&& n > cap && mdataAddr_ + n <= 0xFE00u
		&& (initPc_ >= 0xFE00 || initPc_ < mdataAddr_))
		cap = n;
	if (n > cap) n = cap;
	if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
	if (mdataAddr_ + n > 0x10000) return;
	if (n > mdataSize_)
		mdataSize_ = n;
	unsigned clearN = cap;
	if (mdataAddr_ + clearN > 0x10000)
		clearN = 0x10000u - mdataAddr_;
	/* daiva OP.BIN @0: 16K 窓全体をゼロにすると曲の下の INITIATE.ROM が消える。上書きするバイトだけクリア。 */
	if (mdataAddr_ == 0 && clearN > n)
		clearN = n;
	if (clearN)
		memset(mem_ + mdataAddr_, 0, clearN);
	memcpy(mem_ + mdataAddr_, bgmBank_[use], n);
	/* daiva FLEET/BATL は既に $E000 へ再配置（JMP $E4xx）。PATCH コピーループ用に $0000 へ載せ、そのコピーが走らなくても JSR $E000 が効くようミラー。OP.BIN は ORCC 開始 — ミラーしない。 */
	if (mdataAddr_ == 0 && n >= 3 && mem_[0] == 0x7E && mem_[1] >= 0xE0) {
		unsigned m = n;
		if (m > 0x2000u) m = 0x2000u;
		memcpy(mem_ + 0xE000, mem_, m);
	}
	/* daiva BATL は JMP $E4xx ではなくワード表開始なので上の 7E ミラーが外れる。PATCH 曲 2 は $0000→$E000 / JSR $E0BE をコピー。 */
	else if (IsDaivaPatch() && mdataAddr_ == 0 && n >= 3 && mem_[0] != 0x1A) {
		unsigned m = n;
		if (m > 0x2000u) m = 0x2000u;
		memcpy(mem_ + 0xE000, mem_, m);
	}
	/* Ys MANPR は 0x4D00 も覗く — 主窓が 0x5C00 ならミラー。ここでは 0x3000 に載せない: YMUS 30xx ヘッダは再配置後番地。$3000 を載せると MANPR が壊れる（ローダ JSR $317A 等）。 */
	if (falcomMode_ && mdataAddr_ == 0x5c00 && n > 0) {
		unsigned mn = n;
		if (mn > 0x800u) mn = 0x800u;
		if (0x4d00 + mn <= 0x5c00)
			memcpy(mem_ + 0x4d00, bgmBank_[use], mn);
	}
}

/* バンク／BGM を載せる */
void CHardFm7::StageProg(uint8_t index)
{
	/* 正確な prog バンクのみ — 下位ニブル／最初の存在推測はしない */
	uint8_t use = index;
	if (use >= PROG_BANKS || !progPresent_[use] || !progBank_[use])
		return;
	unsigned n = progBankSize_[use];
	/* Falcom PATCH 表 t0 はロードページ（TTLPRG $10、MANPR $08）。PSG（FED0）と OPN（FF00 TITLEP）の両方。Xanadu 表は 08/10 行ではなく番地リスト — それらは $0000 のまま。 */
	unsigned loadBase = 0;
	if (falcomMode_ && patchTableBase_ >= 0xE000 && use >= 1) {
		const unsigned ent = (unsigned)patchTableBase_ + (unsigned)(use - 1) * 8u;
		if (ent + 1u < 0x10000u) {
			const uint8_t page = mem_[ent];
			const uint8_t win = mem_[ent + 1];
			if ((page == 0x08 || page == 0x10)
				&& (win == 0x00 || win == 0x30 || win == 0x4F || win == 0x5C))
				loadBase = (unsigned)page << 8;
		}
		if (!loadBase && patchTableBase_ == 0xFED0)
			loadBase = 0x0800u;
	}
	/* 高い PATCH（FEE0/FF00）を残す */
	unsigned cap = 0xFE00u;
	if (initPc_ >= 0xE000 && (unsigned)initPc_ < cap)
		cap = (unsigned)initPc_;
	if (mdataAddr_ > 0 && mdataAddr_ < cap && bgmPresent_[0])
		cap = mdataAddr_;
	if (loadBase >= cap) return;
	if (n > cap - loadBase) n = cap - loadBase;
	if (n > 0)
		memcpy(mem_ + loadBase, progBank_[use], n);
}

/* バンク／BGM を載せる */
void CHardFm7::StageVoice(uint8_t index)
{
	if (vdataAddr_ < 0) return;
	uint8_t use = index;
	if (use >= 128 || !voicePresent_[use] || !voiceBank_[use])
		return;
	unsigned n = voiceBankSize_[use];
	if (vdataSize_ > 0 && (unsigned)vdataSize_ < n)
		n = (unsigned)vdataSize_;
	if (vdataAddr_ + (int)n > 0x10000)
		n = (unsigned)(0x10000 - vdataAddr_);
	if (n > 0)
		memcpy(mem_ + vdataAddr_, voiceBank_[use], n);
}

/* CHardFm7::MmrInit の実装 */
void CHardFm7::MmrInit()
{
	mmrAvail_ = (useOpn_ && !falcomMode_) ? 1 : 0;
	mmrOn_ = 0;
	mmrSeg_ = 0;
	mmrWin_ = 0;
	mmrMode_ = 0;
	mmrTouched_ = 0;
	memset(mmrRam_, 0, sizeof(mmrRam_));
	for (int s = 0; s < 8; s++) {
		for (int i = 0; i < 16; i++)
			mmrBank_[s][i] = (uint8_t)(0x30 + i);
	}
	if (!mmrAvail_)
		return;
	/* リップした 64K を AV 既定 $30000 ビューと $00000 の恒等コピーへスナップショット。page 0-F で MMR 許可は no-op。 */
	memcpy(mmrRam_ + 0x30000, mem_, 0x10000);
	memcpy(mmrRam_, mem_, 0x10000);
}

/* CHardFm7::MmrPhys の実装 */
uint8_t* CHardFm7::MmrPhys(uint16_t addr)
{
	if (addr >= 0xFC00 || !mmrAvail_)
		return &mem_[addr];
	uint8_t page;
	if (!mmrOn_)
		page = (uint8_t)(0x30 + (addr >> 12));
	else if ((mmrMode_ & 0x40) && addr >= 0x7C00 && addr < 0x8000) {
		const unsigned win = ((unsigned)mmrWin_ << 8) + 0x7C00u;
		const unsigned phys = (win + (unsigned)(addr - 0x7C00u)) & 0xffffu;
		return &mmrRam_[phys];
	} else {
		page = (uint8_t)(mmrBank_[mmrSeg_ & 7][addr >> 12] & (MMR_PAGES - 1));
	}
	return &mmrRam_[((unsigned)page << 12) | (addr & 0x0fffu)];
}

/* CHardFm7::MmrCommit の実装 */
void CHardFm7::MmrCommit(uint16_t addr, unsigned n)
{
	if (!mmrAvail_ || n == 0)
		return;
	for (unsigned i = 0; i < n; i++) {
		const uint16_t a = (uint16_t)(addr + i);
		if (a < addr && i)
			break;
		if (a >= 0xFC00)
			break;
		*MmrPhys(a) = mem_[a];
		/* MMR オフ中も恒等ページを同期し、後で bank=i で許可しても載せたイメージが見えるようにする */
		if (!mmrOn_)
			mmrRam_[a] = mem_[a];
	}
}

/* バス読込 */
uint8_t CHardFm7::MmrReadReg(uint16_t addr)
{
	const unsigned off = (unsigned)addr - 0xFD80u;
	if (!mmrTouched_)
		return 0xFF;
	if (off < 0x10)
		return mmrBank_[mmrSeg_ & 7][off];
	if (off == 0x13)
		return mmrMode_;
	return 0xFF;
}

/* バス書込 */
void CHardFm7::MmrWriteReg(uint16_t addr, uint8_t data)
{
	const unsigned off = (unsigned)addr - 0xFD80u;
	mmrTouched_ = 1;
	if (off < 0x10) {
		mmrBank_[mmrSeg_ & 7][off] = data;
		return;
	}
	if (off == 0x10) {
		mmrSeg_ = (uint8_t)(data & 7);
		return;
	}
	if (off == 0x12) {
		mmrWin_ = data;
		return;
	}
	if (off == 0x13) {
		mmrMode_ = data;
		mmrOn_ = (data & 0x80) ? 1 : 0;
	}
}

/* CHardFm7::SeedTandeFmVoices の実装 */
void CHardFm7::SeedTandeFmVoices()
{
	/* T&E YM2203（laydock DRIVER $30EE、daiva OP.BIN $2FA8）: $6C00 に 34 バイトボイス。INITIATE.ROM は同じバンクを $0C00 に置く（I-ROM111851002）。今コピー — daiva の 16K OP.BIN @ $0000 が $0C00 を消す。 */
	if (!useOpn_ || falcomMode_)
		return;
	if (memcmp(mem_ + 0x0B00, "I-ROM", 5) != 0)
		return;
	if (mem_[0x0C00] > 0x1F && mem_[0x0C04] > 0x7F)
		return;
	memcpy(mem_ + 0x6C00, mem_ + 0x0C00, 0x0C00);
}

/* 再生／タイマを武装する */
void CHardFm7::ArmLaydockChannels()
{
	/* 2E55 BRA $2EB9 は曲 ptr +8 → +0 をコピーし delay=1。play が STA $5E6F のあと末尾を飛ばすと 2F3B LDY ,U が 0。 */
	if (mem_[0x2EB9] != 0x8E || mem_[0x2EBA] != 0x5E || mem_[0x2EBB] != 0x74)
		return;
	if (mem_[0x5E6F] == 0)
		return;
	for (int ch = 0; ch < 3; ch++) {
		uint8_t* c = mem_ + 0x5E74 + ch * 16;
		const uint16_t song = (uint16_t)(((uint16_t)c[8] << 8) | c[9]);
		const uint16_t cur = (uint16_t)(((uint16_t)c[0] << 8) | c[1]);
		if (song < 0x4000 || song >= 0x5E74)
			continue;
		if (cur != 0)
			continue;
		c[0] = c[8];
		c[1] = c[9];
		c[10] = c[8];
		c[11] = c[9];
		c[2] = 0x30;
		c[3] = 0x00;
		c[4] = 0x01;
		c[5] = 0x18;
		c[6] = 0x10;
		if (!c[7])
			c[7] = 1;
	}
}

/* CHardFm7::FinishDaivaOpPlay の実装 */
void CHardFm7::FinishDaivaOpPlay()
{
	/* PATCH 曲 0: JSR $1BBD（IRQ $2B3A）／$2B68／$2BCD。$1BBD は MMR $FD90 も poke。生きたマッパが無くても JSR は $2C6A チャネル表を $6000 の MUS00 へ植える。$0000 で ORCC #$50 開始は OP.BIN だけ — FLEET/BATL/ED は $1BBD を上書きするか余り OP に $2B3A 印を残し、誤バンクへ JSR する。 */
	if (mem_[0] != 0x1A || mem_[1] != 0x50)
		return;
	if (mem_[0x2B3A] != 0xB6 || mem_[0x2B3B] != 0xFD || mem_[0x2B3C] != 0x03)
		return;
	if (mem_[0x2BCD] != 0x8E || mem_[0x2BCE] != 0x2C || mem_[0x2BCF] != 0x6A)
		return;
	cpu_.dp = 0xFD;
	RunSubroutine(0x1BBD);
	RunSubroutine(0x2BCD);
}

/* CHardFm7::FinishDaivaEdPlay の実装 */
void CHardFm7::FinishDaivaEdPlay()
{
	/* PATCH 曲 3: ED.BIN @ $0000 に対し FIRQ $0CC9／JSR $0CD9／$0D36 を植える。ネイティブ poll が見逃すと PC が $FCxx に残る。 */
	if (!IsDaivaPatch())
		return;
	if ((titleCode_ & 0xffu) != 3)
		return;
	if (mem_[0] != 0x7E || mem_[1] != 0x00 || mem_[2] != 0x0A)
		return;
	mem_[0x0C4F] = playParamA_;
	mem_[0xFFF8] = 0x00;
	mem_[0xFFF9] = 0x2C;
	mem_[0x50F4] = 0xBD;
	mem_[0x50F5] = 0x0C;
	mem_[0x50F6] = 0xC9;
	cpu_.dp = 0xFD;
	cpu_.cc.i = true;
	RunSubroutine(0x0CD9, 400000, 1);
	RunSubroutine(0x0D36, 400000, 1);
	cpu_.pc.w = 0x5021;
	cpu_.cc.i = false;
	cpu_.cwai = false;
}

/* CHardFm7::FinishAlbatrssPlay の実装 */
void CHardFm7::FinishAlbatrssPlay()
{
	/* PATCH play: JSR $F002（stop）は RTS しない — YM busy mute 書き嵐（88k 書き、peak 0）。F004 自体は LDX PCR／STA $FF／RTS で OP.BIN $87CA だけ武装。生きた play JSR を NOP、ホストが $F000 を呼び F069 がチャネル BSS を植え、それから F004。 */
	if (!albatrssMode_)
		return;
	unsigned poll = albatrssPoll_, jsrStop = 0, jsrPlay = 0;
	const unsigned lim = (unsigned)initPc_ + 96u;
	for (unsigned a = initPc_; a + 3u < 0x10000u && a < lim; a++) {
		if (!poll && mem_[a] == 0xB6 && mem_[a + 1] == 0xFD && mem_[a + 2] == 0x58)
			poll = a;
		if (mem_[a] == 0xBD && mem_[a + 1] == 0xF0 && mem_[a + 2] == 0x02)
			jsrStop = a;
		if (mem_[a] == 0xBD && mem_[a + 1] == 0xF0 && mem_[a + 2] == 0x04)
			jsrPlay = a;
	}
	if (!poll || !jsrPlay)
		return;
	albatrssPoll_ = (uint16_t)poll;
	cpu_.d.b[1] = playSongLatch_;
	{
		const uint16_t song = mdataAddr_ ? mdataAddr_ : 0x2000;
		cpu_.index[0].w = song;
		mem_[0xF4E2] = (uint8_t)(song >> 8);
		mem_[0xF4E3] = (uint8_t)(song & 0xff);
		cpu_.cc.i = true;
		/* PATCH JSR は NOP 済みなので $F002 が再入できない */
		RunSubroutine(0xF000, 400000, 0);
		if (mem_[0xF880] == 0)
			mem_[0xF880] = mem_[song + 3] ? mem_[song + 3] : 5;
		/* DRIVER ROM は $F4D9 を 2（AY $FD0D）に既定。$F4BF は YM リードバック後に CLR。それが無いと $F000/$F002 が $F0A8 経由で mute。 */
		if (useOpn_)
			mem_[0xF4D9] = 0;
		else
			mem_[0xF4D9] = 1;
		/* F111: $A1 がボイスをロードし、$00 nn mm は nn F0DA tick の休符。Love & tears は全 FM チャネルで A1 00 B4..C0 開始 — F4DA=5 で約 15s 無音。4 プローブ窓がボイスバースト後 kon=00 で peak 0。長いリード休符だけ縮める。Rising Up ch1 A1 00 30（短い遅延ハーモニー）は触らない。 */
		{
			const uint16_t lim = (uint16_t)(song + 0x2000);
			for (int ch = 0; ch < 3; ch++) {
				const unsigned obj = 0xF4E8u + (unsigned)ch * 24u;
				uint16_t p = ((uint16_t)mem_[obj + 1] << 8) | mem_[obj + 2];
				if (p < song || (unsigned)p + 3u >= (unsigned)lim)
					continue;
				const uint8_t op = mem_[p];
				if (op > 0x2A && op != 0xFE && (op & 0x60) == 0x20)
					p++;
				if ((unsigned)p + 2u < (unsigned)lim
					&& mem_[p] == 0 && mem_[p + 1] >= 0x80) {
					mem_[p + 1] = 1;
					mem_[p + 2] = 0;
					MmrCommit(p, 3);
				}
			}
		}
	}
	RunSubroutine(0xF004);
	/* NOP したブート JSR の前に PATCH が植えた。$F819 の SWI は残す */
	if (mem_[0xF819] == 0x33 || mem_[0xF819] == 0x1A || mem_[0xF819] == 0x34) {
		mem_[0xFFFA] = 0xF8;
		mem_[0xFFFB] = 0x19;
	}
	if (mem_[0x87CA] == 0xB6 && mem_[0x87CB] == 0xFD && mem_[0x87CC] == 0x03) {
		mem_[0xFFF8] = 0x87;
		mem_[0xFFF9] = 0xCA;
	}
	if (jsrStop && mem_[jsrStop] == 0xBD) {
		mem_[jsrStop] = 0x12;
		mem_[jsrStop + 1] = 0x12;
		mem_[jsrStop + 2] = 0x12;
		MmrCommit((uint16_t)jsrStop, 3);
	}
	if (mem_[jsrPlay] == 0xBD) {
		mem_[jsrPlay] = 0x12;
		mem_[jsrPlay + 1] = 0x12;
		mem_[jsrPlay + 2] = 0x12;
		MmrCommit((uint16_t)jsrPlay, 3);
	}
	/* Play $003A JSR $F002 はブート stop の第 2 コピー。last-match 1 回では逃し得る。PATCH 内の DRIVER 呼び出しを全部 NOP。 */
	for (unsigned a = initPc_; a + 3u < 0x10000u && a < (unsigned)initPc_ + 128u; a++) {
		if (mem_[a] == 0xBD && mem_[a + 1] == 0xF0
			&& (mem_[a + 2] == 0x00 || mem_[a + 2] == 0x02 || mem_[a + 2] == 0x04)) {
			mem_[a] = mem_[a + 1] = mem_[a + 2] = 0x12;
			MmrCommit((uint16_t)a, 3);
		}
	}
	unsigned land = jsrPlay + 3;
	if (mem_[land] != 0x1C)
		land = poll;
	cpu_.pc.w = (uint16_t)land;
	cpu_.cc.i = false;
	cpu_.cc.f = true;
	cpu_.cwai = false;
	cpu_.sync = false;
	/* OP.BIN $87CA は $8505 がゼロの間すぐ戻る */
	if (mem_[0x8505] == 0) {
		mem_[0x8505] = 1;
		MmrCommit(0x8505, 1);
	}
}

/* CHardFm7::ParkAlbatrssIfStuck の実装 */
void CHardFm7::ParkAlbatrssIfStuck()
{
	const uint16_t irq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
	if (irq != 0x87CA)
		return;
	const uint16_t pc = cpu_.pc.w;
	/* $F000/$F002 init+stop と $F0A8 AY mute。$F445–$F4BE（書きヘルパ）と $F819（SWI tick）は残し、生きた tick が終えられるようにする */
	int stuck = 0;
	if ((pc >= 0xF000 && pc <= 0xF068) || (pc >= 0xF0A8 && pc < 0xF0D0))
		stuck = 1;
	if (stuck) {
		cpu_.pc.w = albatrssPoll_ ? albatrssPoll_ : 0x002B;
		cpu_.cc.i = false;
		cpu_.cc.f = true;
		cpu_.cwai = false;
		cpu_.sync = false;
	}
}

/* CHardFm7::UnwindMissingBios の実装 */
int CHardFm7::UnwindMissingBios()
{
	/* daiva OP.BIN はリップに無い F-BASIC／イニシエータ（$Axxx/$Bxxx/$Dxxx）を JSR。まだゼロのページを RTS 扱い。OP.BIN ISR $2B3A が載っている間だけ — グローバル空ページ RTS は kohaku オープニングを MUS へ逸らした。 */
	if (falcomMode_ || mdataAddr_ != 0)
		return 0;
	const uint16_t irq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
	if (irq != 0x2B3A)
		return 0;
	const uint16_t pc = cpu_.pc.w;
	if (pc < 0x8000 || pc >= 0xFC00)
		return 0;
	if (MemRead(pc) | MemRead((uint16_t)(pc + 1))
		| MemRead((uint16_t)(pc + 2)) | MemRead((uint16_t)(pc + 3)))
		return 0;
	const uint16_t sp = cpu_.index[3].w;
	const uint16_t ret = (uint16_t)(((uint16_t)MemRead(sp) << 8)
		| MemRead((uint16_t)(sp + 1)));
	cpu_.index[3].w = (uint16_t)(sp + 2);
	cpu_.pc.w = ret;
	cpu_.cc.i = false;
	cpu_.cwai = false;
	cpu_.sync = false;
	return 1;
}

/* I/O ポート読込 */
uint8_t CHardFm7::PortIn(uint16_t /* ポート */)
{
	return 0xff;
}

/* I/O ポート書込 */
void CHardFm7::PortOut(uint16_t /* ポート */, uint8_t /* データ */)
{
}

/* Fm7BusCmd の実装 */
static void Fm7BusCmd(CChip* chip, uint8_t* dataLatch, uint8_t cmd)
{
	if (!chip || !dataLatch) return;
	switch (cmd & 0x0f) {
	case 0x00: /* ハイインピーダンス */
		break;
	case 0x01: /* データ読み */
		*dataLatch = chip->ReadData();
		break;
	case 0x02: /* データ書き */
		chip->Write(1, *dataLatch);
		break;
	case 0x03: /* アドレスラッチ */
		chip->Write(0, *dataLatch);
		break;
	case 0x04: /* ステータス読み → データラッチ（MAME fm7_update_psg） */
		/* YM2203 busy（bit7）はゲストが書きと同じ命令バーストで poll するとセットのまま。albatrss DRIVER $F489 BITA #$80／BNE は STA #$04 のあと LDA 1,Y でこのラッチを待つ。 */
		*dataLatch = (uint8_t)(chip->ReadStatus() & 0x7F);
		break;
	case 0x09: /* ジョイスティック — ニュートラルを返す */
		*dataLatch = 0xff;
		break;
	default:
		break;
	}
}

/* メモリ 8bit 読込 */
uint8_t CHardFm7::MemRead(uint16_t addr)
{
	if (addr == FM7_FD_PLAY_CMD) {
		uint8_t v = playCmdLatch_;
		if ((v == 0x01 || v == 0x02) && playCmdHold_ > 0) {
			if (--playCmdHold_ <= 0)
				playCmdLatch_ = 0;
		}
		return v;
	}
	if (addr == FM7_FD_PLAY_SONG)
		return playSongLatch_;
	if (addr == FM7_FD_PLAY_A)
		return playParamA_;
	if (addr == FM7_FD_PLAY_B)
		return playParamB_;
	if (addr == FM7_FD_PLAY_C)
		return playParamC_;
	/* Falcom PATCH は $FD80 を poll。実 FM77AV その範囲は MMR。Falcom リップだけメールボックスを重ねる — 汎用タイトルはクリアラッチではなくオープンバス $FF を見る。 */
	if (falcomMode_) {
		if (addr == FM7_FD_FALCOM_CMD) {
			uint8_t v = falcomCmdLatch_;
			if (v != 0 && falcomCmdHold_ > 0) {
				if (--falcomCmdHold_ <= 0)
					falcomCmdLatch_ = 0;
			} else if (v != 0) {
				falcomCmdLatch_ = 0;
			}
			return v;
		}
		if (addr == FM7_FD_FALCOM_SONG)
			return falcomSongLatch_;
		if (addr == (FM7_FD_FALCOM_CMD + 1))
			return 0; /* FD81 ハンドシェイク */
		if (addr == 0xFD85)
			return mem_[0xFD85];
	}
	if (addr == FM7_FD_IRQST) {
		/* $FD03 読みは pending ビットをクリアし、laydock bit2 が IRQ 嵐にならない */
		uint8_t v = fd03_;
		fd03_ = (uint8_t)(fd03_ & (uint8_t)~0x0F);
		return v;
	}
	if (addr == FM7_FD_SUBINTF)
		return (uint8_t)(fd05_ | (fd05HaltSticky_ ? 0x80 : 0));

	if (addr >= 0xFD00 && addr <= 0xFDFF) {
		/* キーボード: $FD00 bit0 は 2MHz。$FD01 はスキャンコード（idle 0） */
		if (addr == 0xFD00)
			return 0x01;
		if (addr == 0xFD01)
			return 0x00;
		/* $FD02 読みはカセット／プリンタステータスであり IRQ マスクラッチではない */
		if (addr == FM7_FD_IRQEN)
			return 0xF0;
		if (addr == 0xFD04)
			return 0xFF;
		if (addr == 0xFD0B)
			return 0xFF; /* AV ブートモード: bit0 クリア = DOS */
		/* $FD0F 読みは実機で F-BASIC ROM を許可。音楽リップは $8000+ の RAM なのでバンク切替しない。値は MAME と同様 0。 */
		if (addr == 0xFD0F)
			return 0x00;
		/* YM2203 IRQ フラグ（アクティブ Low bit3）。この読みまで sticky。DeliverIrqs が ack したあとメインループ poll がパルスをまだ見る。 */
		if (addr == 0xFD17) {
			uint8_t v = 0xFF;
			if (useOpn_ && (ymIrqSeen_ || (chipOpn_ && chipOpn_->Irq())))
				v = (uint8_t)(v & ~0x08);
			ymIrqSeen_ = 0;
			return v;
		}
		/* FDC: 音楽イメージは既に載せ済み。not-ready（bit7）は BIOS ヘルパをハングさせる。idle／エラー無しを報告。Ys MANPR は DRQ も欲する。 */
		if (addr == 0xFD18)
			return 0;
		if (addr == 0xFD1F)
			return (!useOpn_ && patchTableBase_ == 0xFED0) ? 0x40 : 0x00;
		if (addr >= 0xFD19 && addr <= 0xFD1E)
			return 0x00;
		/* OPN（YM2203）: $FD15/$FD16 と実 FM77AV の AY 互換 $FD0D/$FD0E エイリアス（同じチップ）。 */
		if (useOpn_ && chipOpn_) {
			if (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA)
				return opnDataLatch_;
			if (addr == FM7_FD_OPN_ADDR)
				return chipOpn_->ReadStatus();
			if (addr == FM7_FD_PSG_ADDR)
				return 0xFF;
		}
		if (!useOpn_ && chipAy_ && albatrssMode_) {
			/* albatrss DRIVER は $FD15/$FD16（AV OPN ポート）をハードコード。PSG 双子ではそれらのポートは空でオープンバス $FF が BITA #$80 を回し続ける。AY バスへエイリアス。 */
			if (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA)
				return psgDataLatch_;
			if (addr == FM7_FD_OPN_ADDR || addr == FM7_FD_PSG_ADDR)
				return 0xFF;
		}
		if (!useOpn_ && chipAy_) {
			if (addr == FM7_FD_PSG_DATA)
				return psgDataLatch_;
			if (addr == FM7_FD_PSG_ADDR)
				return 0xFF;
		}
		return 0xFF;
	}
	return mem_[addr];
}

/* メモリ 8bit 書込 */
void CHardFm7::MemWrite(uint16_t addr, uint8_t data)
{
	if (addr == FM7_FD_PLAY_CMD) {
		playCmdLatch_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_PLAY_SONG) {
		playSongLatch_ = data;
		mem_[addr] = data;
		/* Laydock は play セットアップ完了時ここに $FF を書く。$FD58 を落とし BRA-to-poll ループが永久再トリガしないようにする。 */
		if (data == 0xFF && (playCmdLatch_ == 0x01 || playCmdLatch_ == 0x02))
			playCmdLatch_ = 0;
		return;
	}
	if (addr == FM7_FD_PLAY_A) {
		playParamA_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_PLAY_B) {
		playParamB_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_PLAY_C) {
		playParamC_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_FALCOM_CMD) {
		falcomCmdLatch_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_FALCOM_SONG) {
		falcomSongLatch_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_IRQEN) {
		fd02_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_IRQST) {
		fd03_ = data;
		mem_[addr] = data;
		return;
	}
	if (addr == FM7_FD_SUBINTF) {
		/* bit7 halt 要求 → busy をアサートし TST/BPL ハンドシェイク完了。クリア／他 → BMI 待ちループ用に not busy。介入 CLR（rogue IRQ）を跨いで halt を sticky にし、STB #$80 後のメイン CPU BPL 待ちが永久スピンしない。 */
		if (data & 0x80) {
			fd05_ = 0x80;
			fd05HaltSticky_ = 1;
		} else {
			fd05_ = 0x00;
			fd05HaltSticky_ = 0;
		}
		mem_[addr] = data;
		return;
	}
	if (useOpn_ && chipOpn_) {
		if (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA) {
			opnDataLatch_ = data;
			mem_[addr] = data;
			return;
		}
		if (addr == FM7_FD_OPN_ADDR) {
			opnCmd_ = data;
			Fm7BusCmd(chipOpn_, &opnDataLatch_, data);
			mem_[addr] = data;
			return;
		}
		/* FM77AV $FD0D は YM2203 の AY 互換 BDIR/BC1 ニブル */
		if (addr == FM7_FD_PSG_ADDR) {
			opnCmd_ = (uint8_t)(data & 0x03);
			Fm7BusCmd(chipOpn_, &opnDataLatch_, opnCmd_);
			mem_[addr] = data;
			return;
		}
	}
	if (!useOpn_ && chipAy_) {
		if (albatrssMode_ && (addr == FM7_FD_OPN_DATA || addr == FM7_FD_PSG_DATA)) {
			psgDataLatch_ = data;
			mem_[addr] = data;
			return;
		}
		if (addr == FM7_FD_PSG_DATA) {
			psgDataLatch_ = data;
			mem_[addr] = data;
			return;
		}
		if (albatrssMode_ && (addr == FM7_FD_OPN_ADDR || addr == FM7_FD_PSG_ADDR)) {
			psgCmd_ = data;
			Fm7BusCmd(chipAy_, &psgDataLatch_, data);
			mem_[addr] = data;
			return;
		}
		if (addr == FM7_FD_PSG_ADDR) {
			psgCmd_ = data;
			Fm7BusCmd(chipAy_, &psgDataLatch_, data);
			mem_[addr] = data;
			return;
		}
	}
	mem_[addr] = data;
}

/* CHardFm7::UnpackTitle の実装 */
void CHardFm7::UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut)
{
	const unsigned b0 = titleCode & 0xffu;
	const unsigned b1 = (titleCode >> 8) & 0xffu;
	const unsigned b2 = (titleCode >> 16) & 0xffu;
	const unsigned b3 = (titleCode >> 24) & 0xffu;
	uint8_t song = 0, bank = 0;
	/* Falcom パック: bits12.. = prog 系統、lo = トラック（0x2010 → prog2/song10） */
	if (b3 == 0 && b2 == 0 && b1 >= 0x10 && (b1 & 0x0f) == 0) {
		song = (uint8_t)b0;
		bank = (uint8_t)(b1 >> 4);
		if (!bank) bank = (uint8_t)(b1 ? b1 : 1);
	} else if (b3 != 0 && b1 == 0 && b2 == 0 && b0 != 0) {
		/* 0xNN000000 形式 */
		song = (uint8_t)b3;
		bank = (uint8_t)b0;
	} else if (titleCode != 0) {
		/* 汎用 FM-7: FD59=b0、FD5A=b1 — 非ゼロ曲添字を優先 */
		if (b0)
			song = (uint8_t)b0;
		else if (b1)
			song = (uint8_t)b1;
		else if (b3)
			song = (uint8_t)b3;
		else
			song = (uint8_t)(titleCode & 0xff);
		bank = song;
		if (b1 && b1 != song && b1 < 128)
			bank = (uint8_t)b1;
	}
	(void)b2;
	if (songOut) *songOut = song;
	if (bankOut) *bankOut = bank;
}

/* CHardFm7::RefreshFd03Polarity の実装 */
void CHardFm7::RefreshFd03Polarity()
{
	/* 既定: psyoblde/daiva/luxsor — BITA #$08／BNE skip → bit3 クリア */
	fd03VsyncSet_ = 0;
	fd03VsyncClr_ = 0x08;

	/* kohaku: $2C1D ORCC／JSR $D000／$2C2C LDA $FD03。イメージ内最初の LDA $FD03 は $2C2C なので下のワーク表スキャンは SOUND を飛ばす。 */
	const int kohakuIrq = (mem_[0x2C12] == 0x1A && mem_[0x2C13] == 0x10
		&& mem_[0x2C14] == 0x8E && mem_[0x2C15] == 0x2C && mem_[0x2C16] == 0x1D
		&& mem_[0x2C17] == 0xBF && mem_[0x2C18] == 0xFF && mem_[0x2C19] == 0xF8
		&& mem_[0x2C29] == 0xBD && mem_[0x2C2A] == 0xD0 && mem_[0x2C2B] == 0x00
		&& mem_[0xD000] == 0x86 && mem_[0xD001] == 0x01);
	if (kohakuIrq) {
		mem_[0xFFF8] = 0x2C;
		mem_[0xFFF9] = 0x1D;
	}

	/* XA2PSGPATCH は既に IRQ/FIRQ を $FF94 へ向ける。後の PR.NO2 スキャンが偽 $FD03 BITA を見つけ、tick からベクタを剥がす。 */
	const int xana2PsgIrq = IsXana2PsgPlayer()
		&& mem_[0xFF94] == 0x34 && mem_[0xFF96] == 0x10
		&& mem_[0xFF97] == 0x8E;
	if (xana2PsgIrq) {
		mem_[0xFFF8] = 0xFF;
		mem_[0xFFF9] = 0x94;
		mem_[0xFFF6] = 0xFF;
		mem_[0xFFF7] = 0x94;
	}

	/* 一部イメージはワーク表番地を植える。それらの FD03 ISR を探す */
	{
		auto looksFd03 = [&](uint16_t a) -> int {
			if (a < 0x0100 || a >= 0xFE00) return 0;
			const uint8_t* p = mem_ + a;
			if (p[0] == 0xB6 && p[1] == 0xFD && p[2] == 0x03) return 1;
			if (p[0] == 0x96 && p[1] == 0x03) return 1;
			return 0;
		};
		uint16_t hwIrq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
		if (!kohakuIrq && !xana2PsgIrq && !looksFd03(hwIrq)) {
			for (unsigned a = 0x0100; a + 4u < 0xF000u; a++) {
				if (looksFd03((uint16_t)a) && mem_[a + 3] == 0x85) {
					mem_[0xFFF8] = (uint8_t)(a >> 8);
					mem_[0xFFF9] = (uint8_t)(a & 0xff);
					break;
				}
			}
		}
	}

	const uint16_t irq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
	if (irq == 0 || irq == 0xFFFF)
		return;

	/* IRQ ISR の短い窓を FD03 ビットテストでスキャン */
	int hasBit0Beq = 0;   /* 85 01 27／85 01 10 27 — bit0 セットが要る */
	int hasBit0Bne = 0;   /* 85 01 26 — luxsor 別経路 */
	int hasBit3Beq = 0;   /* 85 08 27／10 27 — bit3 セットが要る */
	int hasBit3Bne = 0;   /* 85 08 26 — bit3 クリアが要る */
	int bit2BnePos = 0;   /* 85 04 26 ＋disp（laydock 音楽ゲート） */
	int bit2BneNeg = 0;   /* 85 04 26 −disp（busy スピン） */
	int bit2Beq = 0;      /* 85 04 27 — bit2 クリアで音楽（albatrss） */

	for (int i = 0; i < 48; i++) {
		const unsigned a = (unsigned)irq + (unsigned)i;
		if (a + 4 >= 0x10000) break;
		const uint8_t* p = mem_ + a;
		if (p[0] != 0x85) continue;
		const uint8_t mask = p[1];
		const uint8_t op = p[2];
		if (mask == 0x01) {
			if (op == 0x27) hasBit0Beq = 1;
			else if (op == 0x10 && p[3] == 0x27) hasBit0Beq = 1;
			else if (op == 0x26) hasBit0Bne = 1;
		} else if (mask == 0x08) {
			if (op == 0x27) hasBit3Beq = 1;
			else if (op == 0x10 && p[3] == 0x27) hasBit3Beq = 1;
			else if (op == 0x26) hasBit3Bne = 1;
		} else if (mask == 0x04 && op == 0x26) {
			const int8_t disp = (int8_t)p[3];
			if (disp >= 8) bit2BnePos = 1;
			else if (disp < 0) bit2BneNeg = 1;
		} else if (mask == 0x04 && op == 0x27) {
			bit2Beq = 1;
		}
	}

	/* laydock: BITA #4／BNE music。次いで BITA #8／BNE skip。1 vsync おきに bit2 をパルスし、ハウスキーピング（bit2 クリア）も走る。 */
	if (bit2BnePos && hasBit3Bne) {
		fd03VsyncSet_ = 0x04;
		fd03VsyncClr_ = 0x08;
		return;
	}
	/* ys_fm7 MANPR bit2 BEQ は「bit2 が要る」ではない — その経路は $FD00/$FD01 をサンプルするだけ。音楽カウントダウンは bit2 クリア分岐。既定を保つ。 */
	/* jikochu_fm7: BITA #4／BNE skip — 音楽は bit2 クリアのときだけ走る */
	if (bit2BnePos && !hasBit3Bne && !hasBit0Beq) {
		fd03VsyncSet_ = 0;
		fd03VsyncClr_ = 0x04;
		return;
	}
	/* reviver: bit0+bit3 が要る。セットなら bit2 スピン */
	if (hasBit0Beq && hasBit3Beq) {
		fd03VsyncSet_ = 0x09;
		fd03VsyncClr_ = 0x04;
		return;
	}
	/* wibarm: bit0 が要る。セットなら bit2 スピン（bit3 ゲート無し） */
	if (hasBit0Beq && bit2BneNeg && !hasBit0Bne) {
		fd03VsyncSet_ = 0x01;
		fd03VsyncClr_ = 0x04;
		return;
	}
	/* asteka2 APRG: BITA #1／LBEQ ＋ BITA #4／BEQ skip-RTI。bit0 をパルスし bit2 はクリア。bit2Beq のみ戻りはこのタイトルを mute。 */
	if (hasBit0Beq && !hasBit0Bne) {
		fd03VsyncSet_ = 0x01;
		fd03VsyncClr_ = (uint8_t)(0x08 | (bit2Beq ? 0x04 : 0));
		return;
	}
	/* albatrss OP.BIN $87CA: BITA #4／BEQ music。bit2 セットなら RTI */
	if (bit2Beq) {
		fd03VsyncSet_ = 0;
		fd03VsyncClr_ = 0x04;
		return;
	}
}

/* CHardFm7::ApplyFd03Vsync の実装 */
void CHardFm7::ApplyFd03Vsync()
{
	fd03_ = (uint8_t)((fd03_ | fd03VsyncSet_) & (uint8_t)~fd03VsyncClr_);
	/* jikochu $0614 を毎 vsync 再武装しない — PSG 書きが再トリガし SSG が壊れた。ゲートは TriggerPlay／Open settle で一度だけ。 */
}

/* 曲再生をトリガする */
void CHardFm7::TriggerPlay(unsigned titleCode)
{
	const uint8_t b0 = (uint8_t)(titleCode & 0xffu);
	const uint8_t b1 = (uint8_t)((titleCode >> 8) & 0xffu);
	const uint8_t b2 = (uint8_t)((titleCode >> 16) & 0xffu);
	const uint8_t b3 = (uint8_t)((titleCode >> 24) & 0xffu);
	uint8_t song = 0, bank = 0;
	UnpackTitle(titleCode, &song, &bank);

	if (falcomMode_ || HasProgBanks()) {
		/* Falcom: prog 添字は bits12..15（0x1000→1、0x2010→2、0x50a0→5） */
		uint8_t prog = (uint8_t)((titleCode >> 12) & 0xffu);
		if (!prog) prog = bank ? bank : song;
		if (!prog) prog = 1;
		/* BGM バンクは bits4..11（0x2010→1 MUSD10A、0x50a0→0x0A YMUS06）。下位ニブルはそのバンク内フレーズ添字。バンク 0 は本物（ys2 TTLMS1）。ゼロバンクを b0 で置換しない: 0x1001 は TTLMS1 フレーズ 1 でありバンク 1（TTLMS2）ではない。 */
		uint8_t track = (uint8_t)((titleCode >> 4) & 0xffu);
		/* Ys2（MUSPRG、prog バンク無し）: 同じビットフィールド。prog はトラックをミラー */
		if (!HasProgBanks()) {
			prog = track ? track : 1;
		}
		StageVoice(track);

		/* Ys AV PATCH 表（prog あたり 8 バイト、PATCH ロード = init_pc）:
		   t0 t1 | patchHi patchLo | callHi callLo | irqHi irqLo
		   t0<<8 = 窓サイズ、t1<<8 = BGM 窓（10 30→$3000、08 5C→$5C00）。
		   ローダは patchAddr に $FD85 を格納、JSR callAddr、STD irq→$FFE2。 */
		uint16_t callAddr = 0, irqAddr = 0, patchAddr = 0;
		const unsigned tableBase = (patchTableBase_ >= 0xE000) ? (unsigned)patchTableBase_
			: ((initPc_ >= 0xE000) ? (unsigned)initPc_ : 0xFF00u);
		if (HasProgBanks() && prog >= 1) {
			const unsigned ent = tableBase + (unsigned)(prog - 1) * 8u;
			if (ent + 8u <= 0x10000u) {
				const uint8_t t0 = mem_[ent];
				const uint8_t t1 = mem_[ent + 1];
				/* Ys 風サイズ／ページ対。xana PATCH はコードでありこの表ではない。Ys FM-7 行 0 は TTLPRG: t0=$10、t1=$00、call=$147A、irq=$11B7。ゼロページは外部 MUS 窓無しを意味し、行パディングにはしない。 */
				if ((t0 == 0x08 || t0 == 0x10)
					&& (t1 == 0x00 || t1 == 0x30 || t1 == 0x4F || t1 == 0x5C)) {
					patchAddr = (uint16_t)(((uint16_t)mem_[ent + 2] << 8) | mem_[ent + 3]);
					callAddr = (uint16_t)(((uint16_t)mem_[ent + 4] << 8) | mem_[ent + 5]);
					irqAddr = (uint16_t)(((uint16_t)mem_[ent + 6] << 8) | mem_[ent + 7]);
					if (t1 != 0) {
						mdataAddr_ = (uint16_t)((uint16_t)t1 << 8);
						mdataSize_ = (unsigned)t0 << 8;
					}
				}
			}
		}

		StageProg(prog);

		if (mdataAddr_ > 0 && bgmPresent_[track])
			StageBgm(track);
		else if (mdataAddr_ > 0 && bgmPresent_[song])
			StageBgm(song);
		falcomSongLatch_ = prog;
		falcomCmdLatch_ = 0x01;
		/* cmd を見えるままにし、$FD80 の PATCH poll が JSR call／FFE2 インストールできるようにする。hold=0 は初回読みでクリアされ PATCH が stop 経路（FFE2=FFDD）を取った。play ハンドラが再読できる程度。高すぎると play が数千回再武装（ys2_fmav Timer/IRQ が壊れた）。 */
		falcomCmdHold_ = (callAddr == 0 && HasProgBanks()) ? 512 : 64;
		mem_[FM7_FD_FALCOM_SONG] = prog;
		mem_[FM7_FD_FALCOM_CMD] = 0x01;
		/* フレーズは下位ニブル（Y00MUS(1)=0x1031）。ys2 MUSPRG に prog バンクが無いので HasProgBanks ローダは走らなかった。PATCH はまだ LDA 5,X（$FD85）／STA $FFB1／JSR $85DC。 */
		mem_[0xFD85] = (uint8_t)(titleCode & 0x0fu);
		if (!HasProgBanks())
			cpu_.dp = 0xFD;
		/* Ys AV（prog バンク）ローダ LDA $FD85／STA を patchAddr メールボックスへ */
		if (HasProgBanks()) {
			/* 曲／パラメータは $FD85（PATCH LDA 5,X、X=$FD80） */
			/* FD85 は載せた BGM バンク内のフレーズ選択 */
			uint8_t param = mem_[0xFD85];
			/* ys2 PATCH は DP=$FD。ys1 PATCH はしない — MANPR の DP 相対 I/O（FD03/FD15/FD16）が要る */
			cpu_.dp = 0xFD;
			/* OPN ヘルパ TST $2D12／BNE skip — 非ゼロですべての書きを mute */
			mem_[0x2D12] = 0;
			/* PATCH ローダをミラー: patchAddr（曲メールボックス）へ $FD85 を poke */
			if (patchAddr >= 0x0100 && patchAddr < 0xFE00)
				mem_[patchAddr] = mem_[0xFD85];
			unsigned progBytes = 0;
			if (prog < PROG_BANKS && progPresent_[prog])
				progBytes = progBankSize_[prog];
			/* $1785 の YMUSPR init は $131B≠0（残り ROM バイト）のとき JSR $18B6 を飛ばす。クリアし最初の武装が走る。 */
			if (callAddr == 0x1785)
				mem_[0x131B] = 0;
			/* call 先が 6809 コードに見えるときだけ PATCH 表に従う。MANPR $317A は曲ヘッダブロック（page3/data）。本物入口は後 — 生きた PATCH poll へ JSR し plantJsr しない。表にソフト IRQ があればまだインストール。 */
			int callLooksCode = 0;
			const unsigned progLoadBase = (!useOpn_ && patchTableBase_ == 0xFED0
				&& prog == 1) ? 0x1000u : 0x0800u;
			if (callAddr >= 0x0100 && callAddr < 0xFE00
				&& (progBytes == 0
					|| ((unsigned)callAddr >= progLoadBase
						&& (unsigned)callAddr < progLoadBase + progBytes))) {
				const uint8_t op = mem_[callAddr];
				/* PSHS/LDA/LDX/ORCC/JSR/JMP/NOP/BRA/BSR — page3/data ではない */
				if (op == 0x34 || op == 0x86 || op == 0x8E || op == 0x1A
					|| op == 0xBD || op == 0x7E || op == 0x12 || op == 0x20
					|| op == 0xB6 || op == 0x10 || op == 0xCC || op == 0x8D
					|| op == 0xAD || op == 0x6E || op == 0x32 || op == 0x33)
					callLooksCode = 1;
				/* 表がときどき PSHS プロローグ直前の短いパラメータブロックを指す（MANPR $317A → $318C） */
				if (!callLooksCode) {
					for (int d = 1; d < 24; d++) {
						const unsigned a = (unsigned)callAddr + (unsigned)d;
						if (a >= 0xFE00) break;
						if (mem_[a] == 0x34) {
							callAddr = (uint16_t)a;
							callLooksCode = 1;
							break;
						}
					}
				}
			}
			/* TTLPRG の $147A ローダは生きた PATCH フォアグラウンド経由で常駐引き渡し。その経路はネイティブ。境界付きホスト JSR は最終曲武装の前に戻る。 */
			if (prog == 1 && !useOpn_ && patchTableBase_ == 0xFED0)
				callLooksCode = 0;
			/* OPN Falcom（ys_fmav TITLEP、xana AV）: $FD80 の生きた PATCH poll が表自身をロード。ホスト JSR＋メールボックスクリアは TITLEP をネイティブローダ無し CWAI 待ちに残した。 */
			/* OMAKEP（prog 5、call $17AA）は YMUS プレーヤ。$FD15 の YM2203 と話す: wait-ready は LDA #$04／STA ,X／LDA 1,X／BMI。PSG 双子では $FD16 がオープンバス $FF なので BMI が終わらず全 YSM00x 行が無音。 */
			if (callLooksCode && prog == 5 && patchTableBase_ == 0xFED0) {
				callLooksCode = 0;
				if (!chipOpn_) {
					chipOpn_ = CEmuChipYm2608Create((uint32_t)opnHz_, 0, sampleRate_);
					if (chipOpn_)
						chipOpn_->Reset();
				}
				if (chipOpn_)
					useOpn_ = 1;
				mem_[0x133E] = mem_[0xFD85];
				mem_[0x133F] = 0;
				mem_[0x1340] = 0;
				mem_[0x1341] = 0;
				cpu_.d.b[1] = 1;
				cpu_.cc.i = true;
				RunSubroutine(0x17AA, 400000, 1);
				/* $10F1: STX $FFF8=$1101、STA $FD02=1、ANDCC #$EF。$1101 は FD03 ラッパ。表 irq $1345 はそれが JSR する音楽 tick でありハードベクタではない。 */
				RunSubroutine(0x10F1, 20000, 0);
				mem_[0xFFF8] = 0x11;
				mem_[0xFFF9] = 0x01;
				mem_[0xFFE2] = 0x13;
				mem_[0xFFE3] = 0x45;
				mem_[0xFC00] = 0x20;
				mem_[0xFC01] = 0xFE;
				cpu_.pc.w = 0xFC00;
				cpu_.cc.i = false;
				cpu_.cc.f = true;
				cpu_.cwai = false;
				falcomCmdLatch_ = 0;
				falcomCmdHold_ = 0;
				mem_[FM7_FD_FALCOM_CMD] = 0;
			}
			if (callLooksCode && !useOpn_) {
				/* MANPR play ディスパッチ: プロローグで CMPA #2。YMUSPR は #1 を欲する。既知武装に合うなら FD85 パラメータを優先。さもなくば #2。 */
				uint8_t arm = mem_[0xFD85];
				if (prog == 1 && !useOpn_ && patchTableBase_ == 0xFED0)
					arm = 0; /* TTLPRG の PATCH 行はフレーズ 0 を渡す */
				else if (arm != 1 && arm != 2 && arm != 9)
					arm = 2;
				cpu_.d.b[1] = arm;
				if (!useOpn_ && patchTableBase_ == 0xFED0)
					mem_[0xFFE5] = 0; /* MANPR ハード出力モード */
				RunSubroutine(callAddr);
				if (!useOpn_ && patchTableBase_ == 0xFED0) {
					const int man2 = (prog == 3) ? 1 : 0;
					const uint16_t shadowMixer = man2 ? 0x2EB4 : 0x2D4F;
					const uint16_t shadowVolume = man2 ? 0x2EB5 : 0x2D50;
					const uint16_t outputEntry = man2 ? 0x2F70 : 0x2DCA;
					/* PATCH は成功ロードを ANDCC #$EF で終える: IRQ 許可、FIRQ はまだマスク。剥いだ BIOS 経路がそのエピローグを逃し得るので本物の線状態を保つ。 */
					/* 無い BIOS フォアグラウンドはさもなくば載せたローダで再開し IRQ をマスクし永久待ち。MANPR の本物ハード IRQ が再生を持つ間、そのフォアグラウンドを $FC80 スタックより下へパーク。 */
					mem_[0xFC00] = 0x20; /* 命令 BRA $FC00 */
					mem_[0xFC01] = 0xFE;
					/* 欠ディスク BIOS が境界ローダを $2DCA unmute エピローグ前に戻したあと、MANPR のネイティブ影レジスタ復元を完了する */
					if (mem_[shadowVolume] == 0 && mem_[shadowVolume + 1] == 0
						&& mem_[shadowVolume + 2] == 0) {
						/* リップ BIOS は最終音量転送を完了できない。3 つの PSG 影音量だけ種をまく。ノート、タイミング、IRQ は MANPR が供給する。 */
						mem_[shadowVolume] = 0x0C;
						mem_[shadowVolume + 1] = 0x0C;
						mem_[shadowVolume + 2] = 0x0C;
					}
					/* リップ BIOS は MANPR へ直ジャンプすると TTLPRG を走らせず ISR ベクタをセットしない。手動でセット。 */
					if (mem_[0xFFE2] == 0xFF && mem_[0xFFE3] == 0xFF) {
						const uint16_t tick = man2 ? 0x29EC : 0x28EA;
						mem_[0xFFE2] = (uint8_t)(tick >> 8);
						mem_[0xFFE3] = (uint8_t)(tick & 0xFF);
					}
					/* 欠 BIOS 復元は AY ミキサ R7 も 0 のままにし、可聴 3 チャネル全部で周期 0 ノイズを許可する。MANPR の音楽はトーン基。3 トーンゲートだけ許可して開始し、後の書きが通常どおり所有する。 */
					if (mem_[shadowMixer] == 0)
						mem_[shadowMixer] = 0x38;
					if (mem_[outputEntry] == 0x34 && mem_[outputEntry + 1] == 0x01)
						RunSubroutine(outputEntry);
					/* MANPR2 $2CFE は STA $3040（リップ AY ヘルパ）の前に戻る。チャネルが 0 のままホスト tick がストリームを見ない — 残り smash トーン 1 つか、F コマンド飛ばし後は無音。play ルーチンが書いたフレーズ武装を終える。 */
					if (man2 && mem_[0x3040] == 0 && mdataAddr_ == 0x4F00) {
						uint16_t u = (uint16_t)((mem_[0x4F00] << 8) | mem_[0x4F01]);
						u = (uint16_t)(((u & 0xFFu) << 8) | (u >> 8));
						u = (uint16_t)(u + 0x200u - 13u);
						uint8_t phrase = mem_[0x2982];
						u = (uint16_t)(u + (uint16_t)(phrase + 1u) * 13u + 1u);
						static const uint16_t kCur[3] = { 0x3042, 0x3067, 0x308C };
						static const uint16_t kLoop[3] = { 0x3044, 0x3069, 0x308E };
						static const uint16_t kCnt[3] = { 0x3040, 0x3065, 0x308A };
						for (int ch = 0; ch < 3; ++ch) {
							uint16_t w = (uint16_t)((mem_[u] << 8) | mem_[(uint16_t)(u + 1)]);
							u = (uint16_t)(u + 2);
							w = (uint16_t)(((w & 0xFFu) << 8) | (w >> 8));
							w = (uint16_t)(w + 0x200u);
							mem_[kCur[ch]] = (uint8_t)(w >> 8);
							mem_[kCur[ch] + 1] = (uint8_t)w;
							w = (uint16_t)((mem_[u] << 8) | mem_[(uint16_t)(u + 1)]);
							u = (uint16_t)(u + 2);
							w = (uint16_t)(((w & 0xFFu) << 8) | (w >> 8));
							w = (uint16_t)(w + 0x200u);
							mem_[kLoop[ch]] = (uint8_t)(w >> 8);
							mem_[kLoop[ch] + 1] = (uint8_t)w;
							mem_[kCnt[ch]] = 1;
						}
						mem_[0x3047] = 0x08;
						mem_[0x306C] = 0x09;
						mem_[0x3091] = 0x0A;
						mem_[0x3048] = 0x00;
						mem_[0x306D] = 0x02;
						mem_[0x3092] = 0x04;
						mem_[0x2983] = 1;
					}
					cpu_.pc.w = 0xFC00;
					mem_[0x28E4] = 1;
					mem_[0xFFE5] = 0;
					cpu_.cc.i = false;
					cpu_.cc.f = true;
				}
				/* PATCH は既にこの play を消費 — settle 中に生の表番地を 2 回 JSR しないようラッチを落とす */
				falcomCmdLatch_ = 0;
				falcomCmdHold_ = 0;
				mem_[FM7_FD_FALCOM_CMD] = 0;
			}
			/* 表 call が無いとき JSR $0000 へフォールバックしない: TTLPRG（タイトル 0x1000）は B6xx 開始で、生きた PATCH poll が play コマンドを見る前に $FD80 をクリアする。 */
			/* ソフト IRQ スロット（PATCH STD $FFE2）。表 irq フィールドはしばしばワーク RAM トランポリン番地（ys 28EA/28A2）— 先が既に FD03 ISR に見えるときだけ $FFF8 を植える。 */
			if (!(prog == 1 && !useOpn_ && patchTableBase_ == 0xFED0)
				&& irqAddr >= 0x0100 && irqAddr < 0xFE00
				&& (progBytes == 0
					|| ((unsigned)irqAddr >= progLoadBase
						&& (unsigned)irqAddr < progLoadBase + progBytes)
					|| irqAddr >= 0x2C00)) {
				mem_[0xFFE2] = (uint8_t)(irqAddr >> 8);
				mem_[0xFFE3] = (uint8_t)(irqAddr & 0xff);
			}
			/* 一部 AV MANPR イメージは ISR ではなくワーク番地を植える */
			{
				auto looksFd03 = [&](uint16_t a) -> int {
					if (a < 0x0100 || a >= 0xFE00) return 0;
					const uint8_t* p = mem_ + a;
					if (p[0] == 0xB6 && p[1] == 0xFD && p[2] == 0x03) return 1;
					if (p[0] == 0x96 && p[1] == 0x03) return 1;
					return 0;
				};
				uint16_t hwIrq = (uint16_t)(((uint16_t)mem_[0xFFF8] << 8) | mem_[0xFFF9]);
				if (!looksFd03(hwIrq)) {
					uint16_t found = 0;
					for (unsigned a = 0x0100; a + 4u < 0xF000u; a++) {
						if (looksFd03((uint16_t)a) && mem_[a + 3] == 0x85) {
							found = (uint16_t)a;
							break;
						}
					}
					if (found) {
						mem_[0xFFF8] = (uint8_t)(found >> 8);
						mem_[0xFFF9] = (uint8_t)(found & 0xff);
					}
				}
			}
			/* Ys FM-7 のリップイメージに戻る BIOS フォアグラウンドは無い。PATCH が MANPR の IRQ ベクタを入れたら、メイン CPU を載せたディスクローダコードから外す: IRQ/FIRQ をマスクし、初期ゼロ周期 PSG レジスタイメージだけ残し、短い高周波パターンが永久に繰り返して聞こえる。 */
			if (!useOpn_ && patchTableBase_ == 0xFED0 && callLooksCode) {
				mem_[0xFC00] = 0x20; /* 命令 BRA $FC00 */
				mem_[0xFC01] = 0xFE;
				cpu_.pc.w = 0xFC00;
				mem_[0x28E4] = 1;
				mem_[0xFFE5] = 0;
				cpu_.cc.i = false;
				cpu_.cc.f = true;
			}
		}
		/* ハイブリッド用に古典メールボックスもミラー */
		playSongLatch_ = HasProgBanks() ? (uint8_t)(titleCode & 0x0fu) : track;
		if (!playSongLatch_ && track) playSongLatch_ = track;
		playParamA_ = b1;
		playParamB_ = b2;
		playParamC_ = b3;
		playCmdLatch_ = 0x01;
		playCmdHold_ = 8;
		mem_[FM7_FD_PLAY_SONG] = playSongLatch_;
		mem_[FM7_FD_PLAY_A] = playParamA_;
		mem_[FM7_FD_PLAY_B] = playParamB_;
		mem_[FM7_FD_PLAY_C] = playParamC_;
		mem_[FM7_FD_PLAY_CMD] = 0x01;
		FinishXana2PsgPlay();
		return;
	}

	/* バンクファイル vs バンク内曲:
	   - 上位バイト（b1）は BGM バンクがあればそれを選ぶ（sharrier 0x0100）
	   - 下位バイト（b0）はそのバンクが存在するときバンク添字（ishtar 0x0007）
	   - asteka2: FD59=b0 は APRG/ENDPRG/TTLPRG、FD5A=b1 はフレーズ。
	     0x0002 は TTLPRG（リップでは空）であり APRG 内の曲ではない。
	   ishtar は sharrier の逆: 0x0807 = バンク 7（ISSD）＋cmd 8、
	   0x0100 = バンク 0（IBGM1）＋cmd 1。$5000 の MUSIC.P が手がかり。 */
	auto bankOk = [&](unsigned i) -> int {
		return (i < 128 && bgmPresent_[i] && bgmBank_[i] && bgmBankSize_[i] > 0) ? 1 : 0;
	};
	const int musicP = (mem_[0x5000] == 0x7E && mem_[0x5001] == 0x58) ? 1 : 0;
	uint8_t stage = 0xff;
	if (IsDaivaPatch()) {
		/* FD59 が OP/FLEET/BATL/ED を選ぶ。0x0101 は FLEET フレーズ 1（b0=1）。0x0102 は BATL フレーズ 1（b0=2）。b1 をバンクにすると 0x0102 が FLEET を載せ PATCH 曲 2 がそれをコピーする。 */
		if (bankOk(b0) || (b0 == 0 && bankOk(0)))
			stage = (uint8_t)b0;
	} else if (IsAsteka2Patch()) {
		/* FD59=b0 は APRG/ENDPRG/TTLPRG、FD5A=b1 はフレーズ。0x0101 Castillo はファイル 1 フレーズ 1。0x0100 はファイル 0 フレーズ 1。b1 をバンクにしない — それが 0x0100 に ENDPRG を載せ APRG の $A3AF を誤イメージに走らせた。欠 TTLPRG（ファイル 2）は APRG を $0100 へコピーせず FinishAsteka2Play で救う。 */
		if (bankOk(b0) || (b0 == 0 && bankOk(0)))
			stage = (uint8_t)b0;
	} else if (IsTelenetMusFile()) {
		/* valis/dds/yakyufan: 0x01xx は play-cmd 1＋MUS ファイルが b0。b1 を載せる（常に 1、MUS01 あり）とすべての 0x01xx タイトルが Running Star／Fantasm Soldier になる。 */
		if (bankOk(b0) || (b0 == 0 && bankOk(0)))
			stage = (uint8_t)b0;
	} else if (IsSharrierPatch()) {
		/* 0xNNPP: MUS ファイルは b1、$B040 フレーズは b0（下は 1 始まり）。b0==0 はバンク 0（MUS00）へ落ちてはいけない。 */
		if (b1 && bankOk(b1))
			stage = (uint8_t)b1;
		else if (bankOk(b0))
			stage = (uint8_t)b0;
	} else if (musicP) {
		if (bankOk(b0))
			stage = (uint8_t)b0;
		else if (b0 == 0 && bankOk(0))
			stage = 0;
		else if (b1 && bankOk(b1))
			stage = (uint8_t)b1;
	} else if (b1 && bankOk(b1))
		stage = (uint8_t)b1;
	else if (b0 && bankOk(b0))
		stage = (uint8_t)b0;
	else if (b0 && bankOk(0))
		stage = 0;
	else if (bankOk(bank))
		stage = bank;
	else if (bankOk(song))
		stage = song;
	else if (bankOk(0))
		stage = 0;

	if (stage < 128 && bankOk(stage))
		StageBgm(stage);

	playSongLatch_ = b0;
	playParamA_ = b1;
	playParamB_ = b2;
	playParamC_ = b3;
	playCmdLatch_ = 0x01;
	/* jikochu: IRQ／メインは play 経路前に $FD58 を何度も poll。hold=8 が cmd をクリアし、ブート JSR $C000 だけが曲として残った。 */
	playCmdHold_ = (!useOpn_ && mdataAddr_ == 0xC000) ? 256
		: ((mdataAddr_ == 0) ? 256 : 8);
	mem_[FM7_FD_PLAY_SONG] = playSongLatch_;
	mem_[FM7_FD_PLAY_A] = playParamA_;
	mem_[FM7_FD_PLAY_B] = playParamB_;
	mem_[FM7_FD_PLAY_C] = playParamC_;
	mem_[FM7_FD_PLAY_CMD] = 0x01;
	if (IsSharrierPatch()) {
		/* PATCH LDA $FD5A／JSR $847B。A * $0E + $B040 がフレーズ表。スロット 0 は $FF なので XML 0x0500（フレーズ 0）は表添字 1。0x0607 のスロット 8 はパックボイスバイトであり行ではない — MUS06 スロット 1 ボイスをヘッダワード 7（0171）に対して再生。 */
		playParamA_ = (uint8_t)(b0 + 1);
		if ((titleCode & 0xffffu) == 0x0607u)
			playParamA_ = 1;
		mem_[FM7_FD_PLAY_A] = playParamA_;
	}

	/* jikochu_fm7: OPEN1 PSG 書きは TST $0614／BPL でゲート。PATCH は play セットアップ後 $0614 をクリアし IRQ tick が FD0D/FD0E に届かない。bit7 を保つ。 */
	if (!useOpn_ && chipAy_ && mdataAddr_ == 0xC000
		&& mem_[0xC19D] == 0x7D && mem_[0xC19E] == 0x06 && mem_[0xC19F] == 0x14)
		mem_[0x0614] = 0x80;

	FinishAlbatrssPlay();
	FinishAsteka2Play();
	FinishWibarmPlay();
	FinishDaivaOpPlay();
	FinishDaivaEdPlay();
	FinishSharrierPlay();
	ArmLaydockChannels();
}

/* CHardFm7::FinishXana2PsgPlay の実装 */
void CHardFm7::FinishXana2PsgPlay()
{
	/* XA2PSGPATCH ワード表: (play,tick)×4 のあと $FF00 で LDS。各 PR.NO* バンクは原点が違う同じプレーヤ。tick は IRQ $FF94 経由でのみ到達。PSG 基板に YM タイマは無く RefreshFd03 が $FFF8 を奪っていたので 1s プローブは切れた AY バーストを聞いた。 */
	xana2Tick_ = 0;
	xana2Tempo_ = 0;
	if (useOpn_)
		return;
	if (mem_[0xFF00] != 0x10 || mem_[0xFF01] != 0xCE
		|| mem_[0xFF02] != 0xFC || mem_[0xFF03] != 0x80)
		return;
	uint8_t prog = (uint8_t)((titleCode_ >> 12) & 0xffu);
	if (!prog)
		prog = 1;
	if (prog < 1 || prog > 4)
		return;
	const unsigned ent = 0xFEF0u + (unsigned)(prog - 1) * 4u;
	const uint16_t play = (uint16_t)(((uint16_t)mem_[ent] << 8) | mem_[ent + 1]);
	const uint16_t tick = (uint16_t)(((uint16_t)mem_[ent + 2] << 8) | mem_[ent + 3]);
	if (play < 0x1000 || tick < 0x1000 || play >= 0xE000 || tick >= 0xE000)
		return;
	if (mem_[play] != 0x34 || mem_[play + 1] != 0x01)
		return;

	uint16_t header = 0;
	for (unsigned a = play; a + 7u < 0x10000u && a < (unsigned)play + 96u; a++) {
		if (mem_[a] == 0x8E && mem_[a + 1] == 0x5C && mem_[a + 2] == 0x00
			&& mem_[a + 3] == 0x10 && mem_[a + 4] == 0x8E) {
			header = (uint16_t)(((uint16_t)mem_[a + 5] << 8) | mem_[a + 6]);
			break;
		}
	}
	if (!header)
		return;
	int hasLive = 0;
	for (unsigned a = play; a + 3u < 0x10000u && a < (unsigned)play + 96u; a++) {
		if (mem_[a] == 0xA7 && mem_[a + 1] == 0x88 && mem_[a + 2] == 0x1B) {
			hasLive = 1;
			break;
		}
	}
	const uint16_t live = (uint16_t)(hasLive ? (header + 27u) : header);
	uint16_t tempo = 0;
	for (unsigned a = tick; a + 3u < 0x10000u && a < (unsigned)tick + 24u; a++) {
		if (mem_[a] != 0x7A)
			continue;
		const uint16_t addr = (uint16_t)(((uint16_t)mem_[a + 1] << 8) | mem_[a + 2]);
		if (addr != 0x617B && addr != 0x606A && addr >= 0x1000 && addr < 0x8000) {
			tempo = addr;
			break;
		}
	}
	if (!tempo)
		return;

	mem_[0xF000] = 0x39;
	const uint8_t ph = (uint8_t)(titleCode_ & 0x0fu);
	mem_[0x617B] = 0;
	mem_[0x606A] = 0;
	mem_[0x607D] = (uint8_t)(ph & 1u);
	mem_[0x60A5] = (uint8_t)(((ph >> 1) & 1u) ^ 1u);

	cpu_.dp = 0xFD;
	cpu_.index[0].w = 0xFD80;
	RunSubroutine(play, 400000, 1);
	/* $416F 系は ptr を生きたレコードへコピーするが +6 を BSS $10（AY エンベロープ）のまま残す。ヘッダ +6 は 4bit 音量。 */
	if (hasLive) {
		for (int ch = 0; ch < 3; ch++) {
			const unsigned src = (unsigned)header + (unsigned)ch * 9u + 6u;
			const unsigned dst = (unsigned)live + (unsigned)ch * 9u + 6u;
			if (src < 0x10000u && dst < 0x10000u)
				mem_[dst] = (uint8_t)(mem_[src] & 0x0Fu);
		}
	}

	if (mem_[0xFF96] == 0x10 && mem_[0xFF97] == 0x8E) {
		mem_[0xFF98] = (uint8_t)(tick >> 8);
		mem_[0xFF99] = (uint8_t)tick;
	}
	mem_[0xFFF8] = 0xFF;
	mem_[0xFFF9] = 0x94;
	mem_[0xFFF6] = 0xFF;
	mem_[0xFFF7] = 0x94;
	/* ネイティブテンポリロードは約 488 Hz AV タイマ用に 8 または 12。60 Hz vsync は /1 分周が要り、play 時 count=9 が 1s 内に期限切れする。 */
	for (int i = 0; i < 12; i++) {
		mem_[tempo] = 1;
		RunSubroutine(tick, 80000, 1);
	}
	mem_[tempo] = 1;
	xana2Tick_ = tick;
	xana2Tempo_ = tempo;
	mem_[0xFC00] = 0x20;
	mem_[0xFC00 + 1] = 0xFE;
	cpu_.pc.w = 0xFC00;
	cpu_.cc.i = false;
	cpu_.cc.f = true;
	cpu_.cwai = false;
}

/* CHardFm7::IsXana2PsgPlayer の実装 */
int CHardFm7::IsXana2PsgPlayer() const
{
	if (useOpn_)
		return 0;
	if (mem_[0xFF00] != 0x10 || mem_[0xFF01] != 0xCE
		|| mem_[0xFF02] != 0xFC || mem_[0xFF03] != 0x80)
		return 0;
	if (mem_[0xFEF0] < 0x10 || mem_[0xFEF2] < 0x10)
		return 0;
	return 1;
}

/* CHardFm7::FinishSharrierPlay の実装 */
void CHardFm7::FinishSharrierPlay()
{
	if (!IsSharrierPatch())
		return;
	const uint8_t file = (uint8_t)((titleCode_ >> 8) & 0xff);
	if (file < 128 && bgmPresent_[file] && bgmBank_[file])
		StageBgm(file);
	if ((titleCode_ & 0xffffu) == 0x0607u) {
		playParamA_ = 1;
		mem_[FM7_FD_PLAY_A] = 1;
	}
	ArmSharrierSeq();
	/* スロット 1 のみ。ボス（A>=2）でホスト $847B は IDA の BSS を $8D00=FE に残し peak 0。ネイティブ PATCH は既にそれらの行を再生する。 */
	if (playParamA_ != 1)
		return;
	cpu_.cc.i = true;
	RunSubroutine(0x8400, 400000, 0);
	cpu_.d.b[1] = playParamA_;
	RunSubroutine(0x847B, 400000, 1);
	cpu_.cc.i = false;
}

/* 再生／タイマを武装する */
void CHardFm7::ArmSharrierSeq()
{
	/* IRQ $866E: LDU #$B000／LDU A,U／ADDD #$B100。既定ヘッダ 00FA/00C8 は共有 $B1xx スケール表に着地。ワード 0 を (slot_ptr - $100) へ向け、加算がそのスロットの $B000 シーケンスに届くようにする。00 06 行のみ — BATTLE FIELD の $B040 スロット 9 はその形式ではない。 */
	uint16_t seq = 0;
	if ((titleCode_ & 0xffffu) == 0x0607u) {
		seq = (uint16_t)(((uint16_t)mem_[0xB00E] << 8) | mem_[0xB00F]);
	} else {
		const unsigned slot = playParamA_;
		if (slot < 1 || slot > 12)
			return;
		const uint16_t row = (uint16_t)(0xB040u + slot * 14u);
		if (mem_[row] != 0x00 || mem_[row + 1] != 0x06)
			return;
		seq = (uint16_t)(((uint16_t)mem_[row + 2] << 8) | mem_[row + 3]);
	}
	if (seq == 0xFFFF || seq < 0x100 || seq >= 0x0F00)
		return;
	const uint16_t h = (uint16_t)(seq - 0x100);
	mem_[0xB000] = (uint8_t)(h >> 8);
	mem_[0xB001] = (uint8_t)(h & 0xff);
}

/* CHardFm7::FinishAsteka2Play の実装 */
void CHardFm7::FinishAsteka2Play()
{
	if (!IsAsteka2Patch())
		return;
	/* TTLPRG は 0 バイト。PATCH 曲 2 は空 RAM へ $8000→$0100／JSR $0855 をコピー。APRG フレーズ 1（表 $A5E2+3 → $A603）はフレーズ 0 の Mundo ではなく第 2 の 3 チャネルシーケンス。メールボックスを APRG+フレーズ 1 へ付け替え、ネイティブ $A3AF が Mundo と同じ経路を走る。 */
	if ((titleCode_ & 0xffu) != 2 || ((titleCode_ >> 8) & 0xffu) != 0)
		return;
	if (bgmPresent_[2] && bgmBankSize_[2] > 0)
		return;
	if (!bgmPresent_[0] || bgmBankSize_[0] == 0)
		return;
	StageBgm(0);
	playSongLatch_ = 0;
	playParamA_ = 1;
	playCmdLatch_ = 0x01;
	playCmdHold_ = 8;
	mem_[FM7_FD_PLAY_SONG] = 0;
	mem_[FM7_FD_PLAY_A] = 1;
	mem_[FM7_FD_PLAY_CMD] = 0x01;
	mem_[0x7EEC] = 1;
	mem_[0x7EEB] = 0;
}

/* CHardFm7::FinishWibarmPlay の実装 */
void CHardFm7::FinishWibarmPlay()
{
	if (!IsWibarmPatch())
		return;
	/* XML 0x0102 戦闘: PATCH LDA $FD5A / JSR $0D00, and $0D00 LSLA /
	   LEAX A,X from $ED00. MUS02 phrase 1's word is $9604 (off the
	   bank). The commented title was 0x0100 = MUS00 phrase 1, whose
	   word $002E lands inside MUS00. */
	if ((titleCode_ & 0xffu) != 2 || ((titleCode_ >> 8) & 0xffu) != 1)
		return;
	if (!bgmPresent_[0] || bgmBankSize_[0] == 0)
		return;
	StageBgm(0);
	playSongLatch_ = 0;
	playParamA_ = 1;
	playCmdLatch_ = 0x01;
	playCmdHold_ = 8;
	mem_[FM7_FD_PLAY_SONG] = 0;
	mem_[FM7_FD_PLAY_A] = 1;
	mem_[FM7_FD_PLAY_CMD] = 0x01;
}

/* CHardFm7::IsAsteka2Patch の実装 */
int CHardFm7::IsAsteka2Patch() const
{
	/* PATCH $202D: LDA $FD59。$203B: CMPA #2 — ファイルは FD59、フレーズは FD5A */
	return (initPc_ == 0x2000 && mdataAddr_ == 0x8000
		&& mem_[0x202D] == 0xB6 && mem_[0x202E] == 0xFD && mem_[0x202F] == 0x59
		&& mem_[0x203B] == 0x81 && mem_[0x203C] == 0x02) ? 1 : 0;
}

/* CHardFm7::IsDaivaPatch の実装 */
int CHardFm7::IsDaivaPatch() const
{
	/* PATCH $5032 LDA $FD59／$5044 CMPA #3 — OP/FLEET/BATL/ED 選択 */
	return (initPc_ == 0x5000 && mdataAddr_ == 0
		&& mem_[0x5032] == 0xB6 && mem_[0x5033] == 0xFD && mem_[0x5034] == 0x59
		&& mem_[0x5044] == 0x81 && mem_[0x5045] == 0x03) ? 1 : 0;
}

/* CHardFm7::IsWibarmPatch の実装 */
int CHardFm7::IsWibarmPatch() const
{
	/* PATCH $403A LDA $FD5A／$403D JSR $0D00 呼び出し */
	return (initPc_ == 0x4000 && mdataAddr_ == 0xED00
		&& mem_[0x403A] == 0xB6 && mem_[0x403B] == 0xFD && mem_[0x403C] == 0x5A
		&& mem_[0x403D] == 0xBD && mem_[0x403E] == 0x0D && mem_[0x403F] == 0x00) ? 1 : 0;
}

/* CHardFm7::IsTelenetMusFile の実装 */
int CHardFm7::IsTelenetMusFile() const
{
	/* valis: LDA $FD59／LDX #$C000／LDB $FD5A／JSR DRIVER 再生 */
	if (initPc_ == 0 && mdataAddr_ == 0xC000
		&& mem_[0x30] == 0xB6 && mem_[0x31] == 0xFD && mem_[0x32] == 0x59
		&& mem_[0x36] == 0x8E && mem_[0x37] == 0xC0 && mem_[0x38] == 0x00)
		return 1;
	/* dds: LDA $FD5A／STA $F8C0／LDA $FD59／LDD #$0100 再生 */
	if (initPc_ == 0x2000 && mdataAddr_ == 0x0100
		&& mem_[0x203E] == 0xB6 && mem_[0x203F] == 0xFD && mem_[0x2040] == 0x59
		&& mem_[0x2044] == 0xCC && mem_[0x2045] == 0x01 && mem_[0x2046] == 0x00)
		return 1;
	/* yakyufan: LDA $FD59／LDD #$2000／JSR $B824。albatrss ではない（init $0000、同じ mdata）。 */
	if (initPc_ == 0x1000 && mdataAddr_ == 0x2000
		&& mem_[0x103A] == 0xB6 && mem_[0x103B] == 0xFD && mem_[0x103C] == 0x59
		&& mem_[0x1040] == 0xCC && mem_[0x1041] == 0x20 && mem_[0x1042] == 0x00)
		return 1;
	return 0;
}

/* CHardFm7::IsSharrierPatch の実装 */
int CHardFm7::IsSharrierPatch() const
{
	/* PATCH $103B LDA $FD5A／$103E JSR $847B。A が $B040 を添字 */
	return (initPc_ == 0x1000 && mdataAddr_ == 0xB000
		&& mem_[0x103B] == 0xB6 && mem_[0x103C] == 0xFD && mem_[0x103D] == 0x5A
		&& mem_[0x103E] == 0xBD && mem_[0x103F] == 0x84 && mem_[0x1040] == 0x7B) ? 1 : 0;
}

/* CHardFm7::OpnWrites の実装 */
unsigned CHardFm7::OpnWrites() const
{
	if (!chipOpn_) return 0;
	unsigned w = 0, k = 0, f = 0, s = 0, m = 0;
	CEmuChipYm2608GetPlayMetrics(chipOpn_, &w, &k, &f, &s, &m);
	return w;
}

/* CHardFm7::AyWrites の実装 */
unsigned CHardFm7::AyWrites() const
{
	return chipAy_ ? CEmuChipAyWriteCount(chipAy_) : 0;
}

/* CPU を進める */
uint64_t CHardFm7::RunCpu(uint64_t cycles)
{
	if (cycles == 0) return 0;
	BindCpuCallbacks();
	const unsigned long start = cpu_.cycles;
	const unsigned long target = start + (unsigned long)cycles;
	int guard = 0;
	while (cpu_.cycles < target && guard++ < 4000000) {
		const int rc = mc6809_step(&cpu_);
		if (rc != 0)
			break;
	}
	const uint64_t ran = (uint64_t)(cpu_.cycles - start);
	cpuCycles_ += ran;
	return ran;
}

/* CPU を進める */
void CHardFm7::RunSubroutine(uint16_t addr, int maxSteps, int clockChips)
{
	if (addr == 0 || addr == 0xFFFF) return;
	if (maxSteps < 1) maxSteps = 200000;
	BindCpuCallbacks();
	const uint16_t retPc = cpu_.pc.w;
	const uint16_t sp0 = cpu_.index[3].w;
	cpu_.index[3].w = (uint16_t)(sp0 - 2);
	mem_[cpu_.index[3].w] = (uint8_t)(retPc >> 8);
	mem_[(uint16_t)(cpu_.index[3].w + 1)] = (uint8_t)(retPc & 0xff);
	cpu_.pc.w = addr;
	int guard = 0;
	while (guard++ < maxSteps) {
		const uint16_t before = cpu_.pc.w;
		const unsigned long c0 = cpu_.cycles;
		if (mc6809_step(&cpu_) != 0)
			break;
		const unsigned long dc = cpu_.cycles - c0;
		if (dc && clockChips) {
			if (chipOpn_ && cpuHz_ > 0)
				chipOpn_->AdvanceClocks(((uint64_t)dc * (uint64_t)opnHz_) / (uint64_t)cpuHz_ + 1u);
			if (chipAy_ && cpuHz_ > 0)
				chipAy_->AdvanceClocks(((uint64_t)dc * (uint64_t)ayHz_) / (uint64_t)cpuHz_ + 1u);
			cpuCycles_ += dc;
		}
		if (before != retPc && cpu_.pc.w == retPc)
			break;
	}
	cpu_.index[3].w = sp0;
	/* ホスト側同期呼び出し。欠 BIOS ヘルパはリッププログラムが RTS を逃し得る。境界呼び出しがホストへ戻ったあとフォアグラウンド CPU をスタック／RAM で走らせない。 */
	cpu_.pc.w = retPc;
}

/* CHardFm7::UnwindStuckBootJsr の実装 */
void CHardFm7::UnwindStuckBootJsr()
{
	/* Falcom 高 PATCH は表でありブート JSR ラッパではない */
	if (falcomMode_ && initPc_ >= 0xE000)
		return;
	const unsigned base = initPc_;
	if (base >= 0xFE00)
		return;
	unsigned jsrAt = 0, jsrTgt = 0, after = 0, poll = 0;
	for (unsigned a = base; a + 3u < 0x10000u && a < base + 96u; a++) {
		if (mem_[a] == 0xB6 && mem_[a + 1] == 0xFD && mem_[a + 2] == 0x58) {
			poll = a;
			break;
		}
		if (mem_[a] == 0xBD && jsrAt == 0) {
			jsrTgt = ((unsigned)mem_[a + 1] << 8) | mem_[a + 2];
			jsrAt = a;
			after = a + 3;
		}
	}
	if (!jsrAt || !poll || jsrTgt == 0)
		return;
	const uint16_t pc = cpu_.pc.w;
	int inPatch = (pc >= base && pc < poll + 16u) ? 1 : 0;
	if (inPatch)
		return;
	int inCallee = 0;
	if (pc >= jsrTgt && pc < jsrTgt + 0x2800u)
		inCallee = 1;
	if (jsrTgt >= 0xF000 && pc >= 0xF000)
		inCallee = 1;
	if (!inCallee)
		return;
	unsigned land = after;
	while (land + 3u <= poll) {
		if (mem_[land] == 0xBD) {
			land += 3;
			continue;
		}
		break;
	}
	for (unsigned a = land; a + 1u < poll && a < land + 20u; a++) {
		if (mem_[a] == 0x1C && mem_[a + 1] == 0xEF) {
			land = a;
			break;
		}
	}
	cpu_.pc.w = (uint16_t)land;
	cpu_.cc.i = false;
	cpu_.cc.f = true;
	cpu_.cwai = false;
	cpu_.sync = false;
}

/* zip から ROM／曲データを載せる */
int CHardFm7::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	titleCode_ = titleCode;
	memset(mem_, 0, sizeof(mem_));
	FreeBanks();
	codeHighWater_ = 0;
	mmrAvail_ = 0;
	mmrOn_ = 0;
	mmrTouched_ = 0;
	albatrssMode_ = 0;
	albatrssPoll_ = 0;
	playCmdLatch_ = 0;
	playSongLatch_ = 0;
	playParamA_ = 0;
	playParamB_ = 0;
	playParamC_ = 0;
	playCmdHold_ = 0;
	falcomCmdLatch_ = 0;
	falcomSongLatch_ = 0;
	falcomCmdHold_ = 0;
	fd02_ = 0;
	fd03_ = 0x08; /* bit3 セット = vsync pending 無し（PATCH BITA #$08／BNE skip） */
	fd05_ = 0; /* サブCPU は busy ではない */
	fd05HaltSticky_ = 0;
	ymIrqSeen_ = 0;
	fd03VsyncSet_ = 0;
	fd03VsyncClr_ = 0x08;
	fd03VsyncPhase_ = 0;
	opnDataLatch_ = 0;
	psgDataLatch_ = 0;
	opnCmd_ = 0;
	psgCmd_ = 0;
	falcomMode_ = IsFalcomSubtype(ge);
	vdataAddr_ = CEmuParseOptHex(ge, "vdata_addr", -1);
	vdataSize_ = CEmuParseOptHex(ge, "vdata_size", 0);
	if (vdataSize_ <= 0) vdataSize_ = CEmuParseOptHex(ge, "vfile_size", 0);
	int loadedCode = 0;

	int defMdata = 0x3000;
	int defMsize = 0x1000;
	if (falcomMode_) {
		defMdata = 0x5c00; /* xana2 系統 */
		defMsize = 0x2800;
		if (_stricmp(ge->subtype, "ys") == 0 || _stricmp(ge->subtype, "ysav") == 0
			|| _stricmp(ge->platform, "mucomfm") == 0) {
			/* 表エントリは t1 にページを埋め込む。MANPR は 4D00 も参照 — AV では 5C00 を優先 */
			defMdata = 0x5c00;
			int hasProgRom = 0;
			for (int i = 0; i < ge->romCount; i++) {
				if (_stricmp(ge->rom[i].type, "prog") == 0) { hasProgRom = 1; break; }
			}
			if (!hasProgRom)
				defMdata = 0x4d00;
			/* ys_fm7 PATCH@FED0 表は $4F00 アクティブ窓を使う */
			if (_stricmp(ge->subtype, "ys") == 0
				|| (ge->archive[0] && _stricmp(ge->archive, "ys_fm7") == 0))
				defMdata = 0x4f00;
		}
		if (ge->archive[0] && _strnicmp(ge->archive, "ys2", 3) == 0)
			defMdata = 0x8c00; /* Ys2 MUSPRG 窓 */
		/* ys2 風を検出: MUSPRG コード＋bgm、prog バンク無し */
		int hasMusPrg = 0, hasProgRom = 0;
		for (int i = 0; i < ge->romCount; i++) {
			if (_stricmp(ge->rom[i].type, "prog") == 0) hasProgRom = 1;
			if (_strnicmp(ge->rom[i].name, "MUSPRG", 6) == 0) hasMusPrg = 1;
		}
		if (hasMusPrg && !hasProgRom)
			defMdata = 0x8c00;
	}

	/* Falcom prog クランプ用に mdata を早く解決 */
	mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", defMdata);
	{
		int ms = CEmuParseOptHex(ge, "mdata_size", defMsize);
		int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
		if (mfs > ms) ms = mfs;
		if (ms <= 0 || ms > BGM_SIZE) ms = defMsize;
		mdataSize_ = (unsigned)ms;
	}
	if (falcomMode_ && vdataAddr_ < 0)
		vdataAddr_ = 0x0100;
	if (falcomMode_ && vdataSize_ <= 0)
		vdataSize_ = (int)mdataAddr_ > vdataAddr_ ? ((int)mdataAddr_ - vdataAddr_) : 0x4c00;

	/* Pass0: irom／ボイス下地。Pass1: code/prog/bgm（コードが irom に勝つ） */
	unsigned codeLo[24], codeHi[24];
	int nCode = 0;
	for (int pass = 0; pass < 2; pass++) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		const int isIrom = (_stricmp(r->type, "irom") == 0);
		const int isVoice = (_stricmp(r->type, "voice") == 0 || _stricmp(r->type, "vdata") == 0);
		const int isCode = (_stricmp(r->type, "code") == 0);
		const int isProg = (_stricmp(r->type, "prog") == 0);
		const int isBgm = (_stricmp(r->type, "bgm") == 0);
		if (pass == 0 && !isIrom && !isVoice) continue;
		if (pass == 1 && (isIrom || isVoice)) continue;

		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;

		if (isIrom || isCode) {
			int off = r->offset;
			if (off < 0) off = 0;
			if (off >= 0x10000) continue;
			unsigned n = sz;
			if (off + (int)n > 0x10000)
				n = (unsigned)(0x10000 - off);
			/* INITIATE.ROM は下地のみ — PATCH/DRIVER を壊さない */
			if (isIrom && pass == 0) {
				memcpy(mem_ + off, data, n);
				continue;
			}
			if (isCode) {
				memcpy(mem_ + off, data, n);
				loadedCode++;
				const unsigned end = (unsigned)off + n;
				if (end > codeHighWater_)
					codeHighWater_ = (uint16_t)(end > 0xffffu ? 0xffffu : end);
				if (nCode < 24) {
					codeLo[nCode] = (unsigned)off;
					codeHi[nCode] = end;
					nCode++;
				}
			}
		} else if (isBgm) {
			int idx = r->offset;
			if (idx < 0 || idx >= 128) continue;
			unsigned n = sz;
			/* 空 zip メンバ（asteka2 TTLPRG）を「あり」に見せない — TriggerPlay が StageBgm して mdata を消す。 */
			if (n == 0) continue;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (bgmBank_[idx]) free(bgmBank_[idx]);
			bgmBank_[idx] = buf;
			bgmBankSize_[idx] = n;
			bgmPresent_[idx] = 1;
		} else if (isProg) {
			int idx = r->offset;
			if (idx < 0 || idx >= PROG_BANKS) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (progBank_[idx]) free(progBank_[idx]);
			progBank_[idx] = buf;
			progBankSize_[idx] = n;
			progPresent_[idx] = 1;
			falcomMode_ = 1;
		} else if (isVoice) {
			int idx = r->offset;
			if (idx < 0 || idx >= 128) continue;
			unsigned n = sz;
			if (n > (unsigned)BGM_SIZE) n = (unsigned)BGM_SIZE;
			unsigned char* buf = (unsigned char*)malloc(n ? n : 1);
			if (!buf) continue;
			memcpy(buf, data, n);
			if (voiceBank_[idx]) free(voiceBank_[idx]);
			voiceBank_[idx] = buf;
			voiceBankSize_[idx] = n;
			voicePresent_[idx] = 1;
			/* 選択トラックの下地を vdata へ（Falcom） */
			uint8_t song = 0, bank = 0;
			UnpackTitle(titleCode, &song, &bank);
			if (idx == (int)song || idx == (int)bank || idx == (int)(titleCode & 0xff)) {
				if (vdataAddr_ >= 0) {
					unsigned vn = n;
					if (vdataSize_ > 0 && (unsigned)vdataSize_ < vn)
						vn = (unsigned)vdataSize_;
					if (vdataAddr_ + (int)vn > 0x10000)
						vn = (unsigned)(0x10000 - vdataAddr_);
					if (vn > 0)
						memcpy(mem_ + vdataAddr_, buf, vn);
				}
			}
		}
	}
	}

	if (!loadedCode) return 0;

	{
		int hasInit = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "init_pc") == 0) { hasInit = 1; break; }
		}
		if (hasInit) {
			initPc_ = (uint16_t)CEmuParseOptHex(ge, "init_pc", 0);
		} else if (initPc_ == 0) {
			int patchOff = -1, firstCode = -1;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0) continue;
				if (firstCode < 0) firstCode = r->offset;
				if (_strnicmp(r->name, "PATCH", 5) == 0
					|| _strnicmp(r->name, "XA1", 3) == 0
					|| _strnicmp(r->name, "XA2", 3) == 0)
					patchOff = r->offset;
			}
			if (patchOff >= 0)
				initPc_ = (uint16_t)patchOff;
			else if (firstCode >= 0)
				initPc_ = (uint16_t)firstCode;
			else
				initPc_ = 0;
		}
		/* 上で既にセットした falcom 既定のあと mdata オプションを再読 */
		if (!falcomMode_) {
			mdataAddr_ = (uint16_t)CEmuParseOptHex(ge, "mdata_addr", 0x3000);
			int ms = CEmuParseOptHex(ge, "mdata_size", 0x1000);
			int mfs = CEmuParseOptHex(ge, "mfile_size", 0);
			if (mfs > ms) ms = mfs;
			if (ms <= 0 || ms > BGM_SIZE) ms = 0x1000;
			mdataSize_ = (unsigned)ms;
		} else {
			int ma = CEmuParseOptHex(ge, "mdata_addr", -1);
			if (ma >= 0) mdataAddr_ = (uint16_t)ma;
		}
	}

	/* リセットベクタ → init_pc、次いで mc6809_reset（FFFE/FFFF を読む） */
	/* Falcom Ys PSG: PATCH@FED0 は prog あたり 8 バイト表。コードが続く。FED0 でブートすると表バイトを実行（約 FED5 で固まり irqPulses=0）。 */
	patchTableBase_ = initPc_;
	if (falcomMode_ && HasProgBanks() && initPc_ >= 0xE000) {
		const unsigned base = initPc_;
		int n = 0;
		if (base + 16u <= 0x10000u) {
			const uint8_t n0 = mem_[base + 8];
			const uint8_t n1 = mem_[base + 9];
			if ((n0 == 0x08 || n0 == 0x10) && n1 != 0 && n1 < 0x80)
				n = 1; /* entry0 = stop/pad。本物の行は +8 から */
		}
		while (n < 12 && base + (unsigned)n * 8u + 8u <= 0x10000u) {
			const uint8_t t0 = mem_[base + (unsigned)n * 8u];
			const uint8_t t1 = mem_[base + (unsigned)n * 8u + 1u];
			if (!((t0 == 0x08 || t0 == 0x10) && t1 != 0 && t1 < 0x80))
				break;
			n++;
		}
		if (n >= 2) {
			patchTableBase_ = (uint16_t)base;
			initPc_ = (uint16_t)(base + (unsigned)n * 8u);
		}
	}
	/* Xanadu AV PATCH は 08/10 Ys 行ではなくワード表開始。FEE0/FEF0 でブートするとそれらのバイトを実行（pc 固まり、I マスク、$FD80 が poll されない）。生きた LDS #$FC80 へ飛ばす。 */
	if (falcomMode_ && initPc_ >= 0xE000) {
		unsigned found = 0;
		const unsigned lim = (unsigned)initPc_ + 80u;
		for (unsigned a = initPc_; a + 4u <= 0x10000u && a < lim; a++) {
			if (mem_[a] == 0x10 && mem_[a + 1] == 0xCE
				&& mem_[a + 2] == 0xFC && mem_[a + 3] == 0x80) {
				found = a;
				break;
			}
		}
		if (found && found != (unsigned)initPc_)
			initPc_ = (uint16_t)found;
	}
	mem_[0xFFFE] = (uint8_t)(initPc_ >> 8);
	mem_[0xFFFF] = (uint8_t)(initPc_ & 0xff);
	BindCpuCallbacks();
	mc6809_reset(&cpu_);
	cpuCycles_ = 0;
	if (chipOpn_) chipOpn_->Reset();
	if (chipAy_) chipAy_->Reset();
	/* ishtar MUSIC.P IRQ $517F は JMP $518F で終わる（RTI 無し）— 最初の tick でフリーズ */
	if (mem_[0x518F] == 0x7E && mem_[0x5190] == 0x51 && mem_[0x5191] == 0x8F
		&& mem_[0x517F] == 0xB6 && mem_[0x5180] == 0xFD && mem_[0x5181] == 0x03) {
		mem_[0x518F] = 0x3B;
		mem_[0x5190] = 0x12;
		mem_[0x5191] = 0x12;
	}
	/* T&E ボイスバンク: OP.BIN/MUS 載せ前に INITIATE $0C00 → $6C00 をコピー */
	SeedTandeFmVoices();
	/* mdata 窓が他コードを飲み込まないときバンク 0（またはタイトルのバンク）を事前載せ。albatrss JSR $F000 はブート中 $2000 に MUS が要る。relics $0C00+$E000 はサイズ上限で除外。 */
	if (!falcomMode_ && mdataSize_ > 0 && mdataSize_ <= 0x4000u) {
		const unsigned m0 = mdataAddr_;
		unsigned m1 = mdataAddr_ + mdataSize_;
		if (m1 > 0x10000u) m1 = 0x10000u;
		int overlap = 0;
		for (int i = 0; i < nCode; i++) {
			const unsigned s = codeLo[i] > m0 ? codeLo[i] : m0;
			const unsigned e = codeHi[i] < m1 ? codeHi[i] : m1;
			if (e > s && (e - s) > 0x1000u)
				overlap = 1;
		}
		if (!overlap) {
			uint8_t song = 0, bank = 0;
			uint8_t st = 0xFF;
			if (IsAsteka2Patch()) {
				/* 正確な FD59 ファイルのみ — TTLPRG 欠で APRG へフォールバックしない（APRG を $0100 へコピーする）。 */
				const uint8_t file = (uint8_t)(titleCode & 0xff);
				if (file < 128 && bgmPresent_[file] && bgmBankSize_[file] > 0)
					st = file;
			} else if (IsSharrierPatch()) {
				const uint8_t file = (uint8_t)((titleCode >> 8) & 0xff);
				if (file < 128 && bgmPresent_[file] && bgmBankSize_[file] > 0)
					st = file;
			} else {
				UnpackTitle(titleCode, &song, &bank);
				if (song < 128 && bgmPresent_[song])
					st = song;
				else if (bank < 128 && bgmPresent_[bank])
					st = bank;
				else if (bgmPresent_[0])
					st = 0;
			}
			if (st < 128)
				StageBgm(st);
		}
	}
	/* albatrss PATCH: LDX #$2000／JSR $F000／JSR $F002。ネイティブ $F000 は 1s ブートを $F48F 書きヘルパで過ごし F069 チャネル BSS を植えない。両ブート JSR を NOP。曲を載せたあと FinishAlbatrssPlay がホストから $F000 を呼ぶ。 */
	{
		const unsigned lim = (unsigned)initPc_ + 80u;
		for (unsigned a = initPc_; a + 8u < 0x10000u && a < lim; a++) {
			if (mem_[a] == 0x8E && mem_[a + 3] == 0xBD
				&& mem_[a + 4] == 0xF0 && mem_[a + 5] == 0x00
				&& mem_[a + 6] == 0xBD && mem_[a + 7] == 0xF0
				&& mem_[a + 8] == 0x02) {
				mem_[a + 3] = mem_[a + 4] = mem_[a + 5] = 0x12;
				mem_[a + 6] = mem_[a + 7] = mem_[a + 8] = 0x12;
				albatrssMode_ = 1;
				for (unsigned p = initPc_; p + 3u < 0x10000u && p < initPc_ + 96u; p++) {
					if (mem_[p] == 0xB6 && mem_[p + 1] == 0xFD && mem_[p + 2] == 0x58) {
						albatrssPoll_ = (uint16_t)p;
						break;
					}
				}
				break;
			}
		}
	}
	MmrInit();
	return 1;
}

/* CEmuHardFm7SetActive の実装 */
void CEmuHardFm7SetActive(CHardFm7* hw)
{
	s_activeFm7 = hw;
}

/* CEmuHardFm7GetActive の実装 */
CHardFm7* CEmuHardFm7GetActive()
{
	return s_activeFm7;
}
