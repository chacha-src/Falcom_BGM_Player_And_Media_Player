#include "StdAfx.h"
#include "cemu_hard_x68k.h"
#include "cemu_x68k_dos.h"
#include "cemu_m68k_bus.h"
#include "../chip/cemu_chip_opm.h"
#include "../fmmon/fmmon_shadow.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t g_x68MidiAck;
static uint8_t g_x68MidiIer;
static uint8_t g_x68MidiRun, g_x68MidiNeed, g_x68MidiD0;
static unsigned g_x68MidiN, g_x68MidiNotes, g_x68MidiWr, g_x68MidiRd;
static unsigned g_x68GpipPhase;
enum { CEMU_X68_MIDI_CAP = 65536 };
static uint8_t g_x68MidiBuf[CEMU_X68_MIDI_CAP];

static unsigned g_x68MidiOff[16];

/* X68MidiReset の実装 */
static void X68MidiReset()
{
	g_x68MidiAck = 0;
	g_x68MidiIer = 0;
	g_x68MidiRun = g_x68MidiNeed = g_x68MidiD0 = 0;
	g_x68MidiN = g_x68MidiNotes = g_x68MidiWr = g_x68MidiRd = 0;
	g_x68GpipPhase = 0;
	memset(g_x68MidiOff, 0, sizeof(g_x68MidiOff));
}

/* X68MidiCapture の実装 */
static void X68MidiCapture(uint8_t v)
{
	if (g_x68MidiN < (unsigned)CEMU_X68_MIDI_CAP)
		g_x68MidiBuf[g_x68MidiN++] = v;
	if (v == 0xF0) { g_x68MidiRun = 0xF0; g_x68MidiNeed = 0; g_x68MidiD0 = 0; return; }
	if (v == 0xF7) { g_x68MidiRun = 0; g_x68MidiNeed = 0; g_x68MidiD0 = 0; return; }
	if (v >= 0xF8) return;
	if (g_x68MidiRun == 0xF0) return;
	if (v & 0x80) {
		g_x68MidiRun = v;
		const uint8_t hi = (uint8_t)(v & 0xf0);
		g_x68MidiNeed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
		g_x68MidiD0 = 0;
	} else if (g_x68MidiRun) {
		if (g_x68MidiNeed == 2 && g_x68MidiD0 == 0) {
			g_x68MidiD0 = v;
		} else {
			const uint8_t hi = (uint8_t)(g_x68MidiRun & 0xf0);
			if (hi == 0x90 && v > 0)
				g_x68MidiNotes++;
			g_x68MidiD0 = 0;
			g_x68MidiNeed = ((g_x68MidiRun & 0xf0) == 0xC0
				|| (g_x68MidiRun & 0xf0) == 0xD0) ? 1 : 2;
		}
	}
}

extern "C" unsigned CEmuX68kMidiByteCount() { return g_x68MidiN; }
extern "C" unsigned CEmuX68kMidiNoteOnCount() { return g_x68MidiNotes; }
extern "C" unsigned CEmuX68kMidiPortWrites() { return g_x68MidiWr; }
/* CEmuX68kMidiDump の実装 */
extern "C" void CEmuX68kMidiDump(FILE* f)
{
	if (!f) return;
	fprintf(f, "    offs");
	for (int i = 0; i < 16; i++) {
		if (g_x68MidiOff[i])
			fprintf(f, " +%X=%u", i, g_x68MidiOff[i]);
	}
	fprintf(f, " rd=%u ier=%02X head", g_x68MidiRd, g_x68MidiIer);
	for (unsigned i = 0; i < g_x68MidiN && i < 24; i++)
		fprintf(f, " %02X", g_x68MidiBuf[i]);
	fprintf(f, "\n");
}

/* CEmuX68kMidiDumpRegs の実装 */
extern "C" void CEmuX68kMidiDumpRegs(FILE* f)
{
	if (!f) return;
	CHardX68k* hw = CEmuHardX68kGetActive();
	const unsigned a4 = (unsigned)m68k_get_reg(NULL, M68K_REG_A4) & 0xffffffu;
	const unsigned d0 = (unsigned)m68k_get_reg(NULL, M68K_REG_D0);
	const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR) & 0xffffu;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	const unsigned rdA4 = hw ? (unsigned)hw->Read8(a4) : 0xffu;
	const unsigned dsr = hw ? (unsigned)hw->Read8(0xeafa09u) : 0xffu;
	const unsigned gpip = hw ? (unsigned)hw->Read8(0xe88001u) : 0xffu;
	fprintf(f, "    pc=%06X sr=%04X sp=%06X d0=%08X a4=%06X (a4)=%02X dsr=%02X gpip=%02X v78=%06X v10c=%06X\n",
		pc, sr, sp, d0, a4, rdA4, dsr, gpip,
		hw ? (hw->Read32(0x78u) & 0xffffffu) : 0u,
		hw ? (hw->Read32(0x10cu) & 0xffffffu) : 0u);
}

/* CEmuX68kIntAck の実装 */
static int CEmuX68kIntAck(int level)
{
	CHardX68k* hw = CEmuM68kBusGetX68k();
	if (level == 2) {
		const int vec = hw ? hw->AckMfpIrq() : M68K_INT_ACK_AUTOVECTOR;
		m68k_set_irq(M68K_IRQ_NONE);
		return vec;
	}
	if (hw && hw->SoundChip())
		hw->SoundChip()->AckIrq();
	/* パルス: ack で Musashi 線を落とす。トランポリン jsr/ISR 中に IRQ6 を保持すると move #$2500,sr（または IPL≤5）が即座に再入し SSP が DOS イメージ $F08700 を下へ歩く。 */
	m68k_set_irq(M68K_IRQ_NONE);
	return M68K_INT_ACK_AUTOVECTOR;
}

/* CEmuHardX68kSetActive の実装 */
void CEmuHardX68kSetActive(CHardX68k* hw)
{
	CEmuM68kBusSetX68k(hw);
	if (hw)
		m68k_set_int_ack_callback(CEmuX68kIntAck);
	else
		m68k_set_int_ack_callback(NULL);
}

/* CEmuHardX68kGetActive の実装 */
CHardX68k* CEmuHardX68kGetActive()
{
	return CEmuM68kBusGetX68k();
}

CHardX68k::CHardX68k()
	: cpuHz_(10000000)
	, opmHz_(4000000)
	, opmWrites_(0)
	, chip_(NULL)
	, sampleRate_(44100)
	, ymAddr_(0)
	, songFlag_(0)
	, songCode_(0)
	, pc_(0)
	, fetchCount_(0)
	, musashiReady_(0)
	, adpcmPlaying_(0)
	, adpcmAddr_(0)
	, adpcmSize_(0)
	, adpcmPos_(0)
	, adpcmSignal_(0)
	, adpcmStep_(0)
	, adpcmNibble_(0)
	, adpcmRateHz_(15600)
	, adpcmPan_(0)
	, adpcmPpi_(0x08)
	, adpcmPhase_(0)
	, adpcmPaused_(0)
	, dmacMtc_(0)
	, dmacMar_(0)
	, dmacOcr_(0)
	, dmacBtc_(0)
	, dmacBar_(0)
	, adpcmChainPtr_(0)
	, adpcmChainLeft_(0)
	, dosFileCount_(0)
	, dosMbA1_(0)
	, dosMbD0_(0)
	, dosMbD1_(0)
	, dosMbResult_(0)
	, softMfp_(0)
	, mfpTdAcc_(0)
	, mfpTcAcc_(0)
	, mfpIrqPending_(0)
	, mfpIrqVec_(0)
{
	hardKind = KIND_X68K;
	memset(rom_, 0, sizeof(rom_));
	memset(ram_, 0, sizeof(ram_));
	memset(high_, 0, sizeof(high_));
	memset(heap_, 0, sizeof(heap_));
	memset(ext_, 0, sizeof(ext_));
	memset(mfp_, 0, sizeof(mfp_));
	memset(dosFiles_, 0, sizeof(dosFiles_));
	memset(dosHandles_, 0, sizeof(dosHandles_));
	for (int i = 0; i < kDosHandles; i++)
		dosHandles_[i].file = -1;
}

CHardX68k::~CHardX68k()
{
	Shutdown();
}

/* チップと CPU を生成する */
int CHardX68k::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	if (_stricmp(ge->platform, "x68k") != 0 && _stricmp(ge->dataDir, "x68k") != 0)
		return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = 10000000;
	opmHz_ = 4000000;
	softMfp_ = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "mfp") == 0 && strtoul(ge->opt[i].value, NULL, 0))
			softMfp_ = 1;
	}
	chip_ = CEmuChipYm2151Create((uint32_t)opmHz_, sampleRate_);
	opmWrites_ = 0;
	musashiReady_ = 0;
	X68MidiReset();
	return chip_ ? 1 : 0;
}

/* チップ／CPU／ROM を破棄する */
void CHardX68k::Shutdown()
{
	if (CEmuM68kBusGetX68k() == this)
		CEmuM68kBusSetX68k(NULL);
	if (chip_) {
		CEmuChipYm2151Destroy(chip_);
		chip_ = NULL;
	}
	musashiReady_ = 0;
}

/* 曲コマンドをメールボックスへ書く */
void CHardX68k::SetSongCommand(unsigned code)
{
	songCode_ = (uint16_t)(code & 0xffff);
	songFlag_ = 0x01;
}

/* CHardX68k::HighPtr の実装 */
uint8_t* CHardX68k::HighPtr(unsigned addr24)
{
	const unsigned page = addr24 & 0xff0000u;
	if (page == 0xff0000u || page == 0x1f0000u)
		return &high_[addr24 & 0xffffu];
	return NULL;
}

/* CHardX68k::HighPtr の実装 */
const uint8_t* CHardX68k::HighPtr(unsigned addr24) const
{
	const unsigned page = addr24 & 0xff0000u;
	if (page == 0xff0000u || page == 0x1f0000u)
		return &high_[addr24 & 0xffffu];
	return NULL;
}

/* 8bit 読込 */
uint8_t CHardX68k::Read8(unsigned addr)
{
	addr &= 0xffffffu;
	if (addr < (unsigned)kRomBytes)
		return rom_[addr];
	if (addr >= (unsigned)kHeapBase && addr < (unsigned)kHeapBase + (unsigned)kHeapBytes)
		return heap_[addr - (unsigned)kHeapBase];
	if (addr >= 0xf00000u && addr <= 0xf0ffffu)
		return ram_[addr - 0xf00000u];
	if (const uint8_t* h = HighPtr(addr))
		return *h;
	if (addr == 0xe00000u)
		return songFlag_;
	if (addr == 0xe00001u)
		return (uint8_t)(songCode_ & 0xff);
	if (addr == 0xe00002u)
		return (uint8_t)((songCode_ >> 8) & 0xff);
	/* DOS ファイル操作結果メールボックス $E00018..$E0001B（ビッグエンディアン long） */
	if (addr >= 0xe00018u && addr <= 0xe0001bu) {
		const unsigned sh = (3u - (addr - 0xe00018u)) * 8u;
		return (uint8_t)((dosMbResult_ >> sh) & 0xffu);
	}
	/* YM2151 ステータス（奇数ポート） */
	if (addr == 0xe90003u || addr == 0xe90001u)
		return chip_ ? (uint8_t)(chip_->ReadStatus() & 0x7fu) : 0;
	/* MSM6258V ステータス: idle を返し _ADPCMSNS スピンが常にドレインする */
	if (addr == 0xe92001u || addr == 0xe92003u)
		return 0;
	if (addr == 0xe9a005u || addr == 0xe9a007u)
		return adpcmPpi_;
	if (addr == 0xe9a001u || addr == 0xe9a003u)
		return (uint8_t)(adpcmPlaying_ ? 0x08 : 0x00);
	if (addr == 0xe00800u) {
		if (musashiReady_)
			m68k_end_timeslice();
		return 0;
	}
	/* Soft MFP（$E88000 / $E8A000）。0→$FF にリマップしない: IER/IMR/TCDCR リセットは 0（無効／停止）。オープンバス $FF は「タイマ既に ON」を隠した。 */
	if ((addr >= 0xe88000u && addr <= 0xe88fffu) || (addr >= 0xe8a000u && addr <= 0xe8afffu)) {
		const unsigned off = addr & 0xfffu;
		uint8_t v = mfp_[off];
		/* GPIP（$E88001）: オープンバス High、bit7 セットで `tst.b / bmi` がハングしない。Bit4 は VDISP — arcus はクリア待ち（`btst #4 / bne`）、MIDI_DRV.68K はセット後クリア待ち（`btst #4 / beq` 次いで bne）。毎回トグルし両相待ちを終わらせる。 */
		if (off == 0x001u) {
			if (v == 0) v = 0xff;
			g_x68GpipPhase++;
			if (g_x68GpipPhase & 1u)
				v = (uint8_t)(v | 0x10u);
			else
				v = (uint8_t)(v & (uint8_t)~0x10);
		}
		(void)softMfp_;
		return v;
	}
	/* CZ-6BM1 / YM3802 MIDI。オープンバス $FF は永久 busy。DSR（$EAFA09）: bit7 IRQ、bit6 TxRDY（ZMUSIC `btst #6,(a4)` / zmusic2 macro.mac の set_a3a4）、bit2 TxEMPTY、bit1 TxRDY。MIDI_DRV は A4=$EAFA09 で `tst.b (a4) / bpl` 待ち、+4 に Tx データを書く。 */
	if ((addr >= 0xeafa00u && addr <= 0xeafa0fu)
		|| (addr >= 0xefa000u && addr <= 0xefa00fu)) {
		g_x68MidiRd++;
		const unsigned r = addr & 0x0fu;
		uint8_t st = (uint8_t)(0x80u | 0x40u | 0x04u | 0x02u);
		if (g_x68MidiAck) {
			g_x68MidiAck = 0;
			st = (uint8_t)(st | 0x80u);
		}
		if (r == 0x03u) return g_x68MidiIer;
		return st;
	}
	/* $E00000..$E7FFFF メールボックス／タイムスライス MMIO 後の追加 RAM */
	if (addr >= (unsigned)kExtBase && addr < (unsigned)kExtBase + (unsigned)kExtBytes)
		return ext_[addr - (unsigned)kExtBase];
	if (addr >= 0xe80000u && addr <= 0xefffffu)
		return 0xff;
	return 0;
}

/* 16bit 読込 */
uint16_t CHardX68k::Read16(unsigned addr)
{
	addr &= 0xffffffu;
	return (uint16_t)((Read8(addr) << 8) | Read8((addr + 1) & 0xffffffu));
}

/* 32bit 読込 */
uint32_t CHardX68k::Read32(unsigned addr)
{
	addr &= 0xffffffu;
	return ((uint32_t)Read16(addr) << 16) | (uint32_t)Read16((addr + 2) & 0xffffffu);
}

/* 8bit 書込 */
void CHardX68k::Write8(unsigned addr, uint8_t data)
{
	addr &= 0xffffffu;
	if (addr < (unsigned)kRomBytes) {
		rom_[addr] = data;
		return;
	}
	if (addr >= (unsigned)kHeapBase && addr < (unsigned)kHeapBase + (unsigned)kHeapBytes) {
		heap_[addr - (unsigned)kHeapBase] = data;
		return;
	}
	if (addr >= 0xf00000u && addr <= 0xf0ffffu) {
		ram_[addr - 0xf00000u] = data;
		return;
	}
	if (uint8_t* h = HighPtr(addr)) {
		*h = data;
		return;
	}
	if (addr == 0xe00000u) {
		songFlag_ = data;
		return;
	}
	/* ゲストが曲コードバイトを poke し得る。メールボックスを一貫させる */
	if (addr == 0xe00001u) {
		songCode_ = (uint16_t)((songCode_ & 0xff00u) | data);
		return;
	}
	if (addr == 0xe00002u) {
		songCode_ = (uint16_t)((songCode_ & 0x00ffu) | ((uint16_t)data << 8));
		return;
	}
	if ((addr >= 0xe88000u && addr <= 0xe88fffu) || (addr >= 0xe8a000u && addr <= 0xe8afffu)) {
		mfp_[addr & 0xfffu] = data;
		return;
	}
	if (addr >= (unsigned)kExtBase && addr < (unsigned)kExtBase + (unsigned)kExtBytes) {
		ext_[addr - (unsigned)kExtBase] = data;
		return;
	}
	if (addr == 0xe90001u || addr == 0xe90003u) {
		if (!chip_) return;
		const int a0 = (int)((addr >> 1) & 1);
		chip_->Write((uint32_t)a0, data);
		if (a0)
			opmWrites_ = CEmuChipYm2151WriteCount(chip_);
		return;
	}
	/* HD63450 DMAC チャネル 3 が MSM6258V を供給。転送カウントとメモリアドレスは MTC/MAR へ実際に書かれたバイトからラッチする。D2/A1 から読むのはそれらがまだそこに残る 1 ドライバイディオムだけ有効で、系統全体が OPM は鳴るが PCM サンプルが 1 つも出なかった理由。レジスタスヌープは別経路でチャネルを組むリップのフォールバックとして残す。 */
	if (addr >= 0xe840c0u && addr <= 0xe840ffu) {
		switch (addr) {
		case 0xe840c5u: dmacOcr_ = data; break;
		case 0xe840cau: dmacMtc_ = (uint16_t)((dmacMtc_ & 0x00ffu) | ((unsigned)data << 8)); break;
		case 0xe840cbu: dmacMtc_ = (uint16_t)((dmacMtc_ & 0xff00u) | data); break;
		case 0xe840ccu: dmacMar_ = (dmacMar_ & 0x00ffffffu) | ((unsigned)data << 24); break;
		case 0xe840cdu: dmacMar_ = (dmacMar_ & 0xff00ffffu) | ((unsigned)data << 16); break;
		case 0xe840ceu: dmacMar_ = (dmacMar_ & 0xffff00ffu) | ((unsigned)data << 8); break;
		case 0xe840cfu: dmacMar_ = (dmacMar_ & 0xffffff00u) | data; break;
		case 0xe840dau: dmacBtc_ = (uint16_t)((dmacBtc_ & 0x00ffu) | ((unsigned)data << 8)); break;
		case 0xe840dbu: dmacBtc_ = (uint16_t)((dmacBtc_ & 0xff00u) | data); break;
		case 0xe840dcu: dmacBar_ = (dmacBar_ & 0x00ffffffu) | ((unsigned)data << 24); break;
		case 0xe840ddu: dmacBar_ = (dmacBar_ & 0xff00ffffu) | ((unsigned)data << 16); break;
		case 0xe840deu: dmacBar_ = (dmacBar_ & 0xffff00ffu) | ((unsigned)data << 8); break;
		case 0xe840dfu: dmacBar_ = (dmacBar_ & 0xffffff00u) | data; break;
		default: break;
		}
	}
	if (addr == 0xe840c0u) {
		if (data == 0xff)
			adpcmPlaying_ = 0;
		return;
	}
	if (addr == 0xe840cau && musashiReady_) {
		adpcmSize_ = ((unsigned)m68k_get_reg(NULL, M68K_REG_D2) & 0xffffu) << 1;
		adpcmAddr_ = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
		return;
	}
	if (addr == 0xe840ccu && musashiReady_) {
		adpcmAddr_ = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
		return;
	}
	if (addr == 0xe840c7u) {
		/* CCR: bit7 STR 開始、bit5 HLT 停止、bit4 SAB ソフトアボート。旧来の厳密 0x88 開始テストは下位ビットが違う開始バイトのドライバを全部逃した。 */
		if (data & 0x10u) { /* SAB（ソフトアボート） */
			adpcmPlaying_ = 0;
			adpcmPaused_ = 0;
			adpcmChainLeft_ = 0;
			FmMonShadowPcmNote(0, 0, 0);
			return;
		}
		if (!(data & 0x80u)) {
			/* HLT は再生中ブロックを落とさずポーズ切替。MUCO の _ADPCMMOD ポーズ／再開ペアがまさにこれ。 */
			adpcmPaused_ = (data & 0x20u) ? 1 : 0;
			if (!adpcmPlaying_)
				FmMonShadowPcmNote(0, 0, 0);
			return;
		}
		adpcmPaused_ = 0;
		adpcmSignal_ = 0;
		adpcmStep_ = 0;
		adpcmNibble_ = 0;
		/* OCR bits 3-2 がチェイン選択: 10 = 配列チェイン。チャネルは MAR/MTC ではなく BAR 上の 6 バイト {address, count} 記述子を歩く。サンプル前に短いプライミングブロックをキューするドライバ（CODE-ZERO）はこの形だけ使う。 */
		if ((dmacOcr_ & 0x0cu) == 0x08u && dmacBtc_ > 0) {
			adpcmChainPtr_ = dmacBar_ & 0xffffffu;
			adpcmChainLeft_ = dmacBtc_;
			if (!AdpcmLoadChainEntry()) {
				adpcmPlaying_ = 0;
				FmMonShadowPcmNote(0, 0, 0);
			}
			return;
		}
		adpcmChainLeft_ = 0;
		const unsigned dmaSize = (unsigned)dmacMtc_ << 1;
		const unsigned dmaAddr = dmacMar_ & 0xffffffu;
		const int dmaOk = (dmaSize > 0 && dmaAddr >= 0x400u
			&& dmaAddr < 0x1000000u);
		if (dmaOk) {
			AdpcmStartBlock(dmaAddr, dmacMtc_);
		} else if (adpcmSize_ > 0) {
			unsigned a = adpcmAddr_;
			if (musashiReady_)
				a = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
			AdpcmStartBlock(a, adpcmSize_ >> 1);
		} else {
			adpcmPlaying_ = 0;
			FmMonShadowPcmNote(0, 0, 0);
		}
		return;
	}
	/* MSM6258V コマンドレジスタ。$E9200x は実機の ADPCM チップであり第 2 OPM 窓ではない: $01 停止、$02 再生開始。$E92003 は CPU 給電データポート。DMA 駆動リップはフラッシュ用に poke するだけ。 */
	if (addr == 0xe92001u) {
		if (data & 0x01u) {
			adpcmPlaying_ = 0;
			adpcmPaused_ = 0;
			adpcmChainLeft_ = 0;
			FmMonShadowPcmNote(0, 0, 0);
		} else if (data & 0x02u) {
			adpcmPaused_ = 0;
			if (!adpcmPlaying_ && adpcmSize_ > 0) {
				adpcmSignal_ = 0;
				adpcmStep_ = 0;
				AdpcmStartBlock(adpcmAddr_, adpcmSize_ >> 1);
			}
		}
		return;
	}
	if (addr == 0xe92003u)
		return;
	if (addr == 0xe9a005u || addr == 0xe9a007u) {
		adpcmPpi_ = data;
		adpcmPan_ = data & 3;
		switch ((data >> 2) & 3) {
		case 0: adpcmRateHz_ = 7800; break;
		case 1: adpcmRateHz_ = 10400; break;
		default: adpcmRateHz_ = 15600; break;
		}
		if (adpcmPlaying_) {
			const unsigned rate = (unsigned)adpcmRateHz_;
			const int mid = FmMonShadowPitchRateToMidi(
				(unsigned)(((uint64_t)rate * 4096u + 7800u) / 15600u));
			FmMonShadowPcmNote(0, (mid >= 0) ? mid : 60, 1);
		}
		return;
	}
	if ((addr >= 0xeafa00u && addr <= 0xeafa0fu)
		|| (addr >= 0xefa000u && addr <= 0xefa00fu)) {
		const unsigned r = addr & 0x0fu;
		g_x68MidiWr++;
		g_x68MidiOff[r]++;
		if (r == 0x03u)
			g_x68MidiIer = data;
		/* 奇数バイト YM3802: +9 TxD。一部 MIDI_DRV.68K ビルドは +B/+D も使う */
		if (r == 0x09u || r == 0x0Bu || r == 0x0Du)
			X68MidiCapture(data);
		g_x68MidiAck = 1;
		return;
	}
}

/* OKI ADPCM ステップ表（MSM6258 / 類似） */
static const int kAdpcmIndexShift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static int kAdpcmDiffLut[49 * 16];
static int kAdpcmLutReady = 0;

/* CEmuX68kAdpcmInitLut の実装 */
static void CEmuX68kAdpcmInitLut()
{
	if (kAdpcmLutReady) return;
	static const int nbl2bit[16][4] = {
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};
	for (int step = 0; step <= 48; step++) {
		int stepval = 16;
		for (int i = 0; i < step; i++)
			stepval = (stepval * 11 + 5) / 10;
		for (int nib = 0; nib < 16; nib++) {
			kAdpcmDiffLut[step * 16 + nib] = nbl2bit[nib][0] *
				(stepval * nbl2bit[nib][1] +
				 stepval / 2 * nbl2bit[nib][2] +
				 stepval / 4 * nbl2bit[nib][3] +
				 stepval / 8);
		}
	}
	kAdpcmLutReady = 1;
}

/* CHardX68k::AdpcmStartBlock の実装 */
void CHardX68k::AdpcmStartBlock(unsigned addr, unsigned bytes)
{
	adpcmAddr_ = addr & 0xffffffu;
	adpcmSize_ = bytes << 1; /* ニブル */
	adpcmPos_ = 0;
	adpcmPhase_ = 0;
	adpcmPlaying_ = 1;
	/* MSM6258 → FM モニタ ADPCM キー行（OPM+ADPCM）。15.6kHz はネイティブ再生レートであり可聴発振周波数ではない。絶対 Hz 変換は O10 へ誤クランプするのでユニティピッチ（C4）として表示する。 */
	const unsigned rate = (unsigned)(adpcmRateHz_ > 0 ? adpcmRateHz_ : 15600);
	const int mid = FmMonShadowPitchRateToMidi(
		(unsigned)(((uint64_t)rate * 4096u + 7800u) / 15600u));
	FmMonShadowPcmNote(0, (mid >= 0) ? mid : 60, 1);
	{
		uint8_t snap[8];
		memset(snap, 0, sizeof(snap));
		snap[0] = 1;
		snap[1] = (uint8_t)(rate >> 8);
		snap[2] = (uint8_t)rate;
		snap[3] = (uint8_t)(adpcmAddr_ >> 16);
		snap[4] = (uint8_t)(adpcmAddr_ >> 8);
		snap[5] = (uint8_t)adpcmAddr_;
		snap[6] = (uint8_t)(adpcmSize_ >> 8);
		snap[7] = (uint8_t)adpcmSize_;
		FmMonShadowSetCompanionRegs(snap, sizeof(snap));
	}
}

/* 配列チェインの次 6 バイト {address, count} 記述子を取る。デコーダ状態（signal/step）はブロックを跨ぐ: チェインはチップへの 1 本の連続 ADPCM ストリーム。 */
int CHardX68k::AdpcmLoadChainEntry()
{
	while (adpcmChainLeft_ > 0) {
		const unsigned p = adpcmChainPtr_;
		adpcmChainPtr_ = (p + 6u) & 0xffffffu;
		adpcmChainLeft_--;
		const unsigned a = Read32(p) & 0xffffffu;
		const unsigned n = Read16(p + 4u);
		if (n == 0 || a < 0x400u) continue;
		AdpcmStartBlock(a, n);
		return 1;
	}
	return 0;
}

/* ADPCM をステレオへ混成する */
void CHardX68k::MixAdpcm(int16_t* stereo, int frames)
{
	if (!stereo || frames <= 0 || !adpcmPlaying_ || adpcmPaused_ || adpcmSize_ < 1) return;
	CEmuX68kAdpcmInitLut();
	const int rate = adpcmRateHz_ > 0 ? adpcmRateHz_ : 15600;
	const int64_t step = ((int64_t)rate << 16) / (sampleRate_ > 0 ? sampleRate_ : 44100);
	for (int i = 0; i < frames; i++) {
		adpcmPhase_ += step;
		while (adpcmPhase_ >= 0x10000) {
			adpcmPhase_ -= 0x10000;
			if (adpcmPos_ >= adpcmSize_) {
				if (AdpcmLoadChainEntry())
					continue;
				adpcmPlaying_ = 0;
				FmMonShadowPcmNote(0, 0, 0);
				break;
			}
			unsigned byteAddr = adpcmAddr_ + (adpcmPos_ >> 1);
			uint8_t b = Read8(byteAddr);
			const int nib = (adpcmPos_ & 1) ? ((b >> 4) & 0x0f) : (b & 0x0f);
			adpcmPos_++;
			int stepIdx = adpcmStep_;
			if (stepIdx < 0) stepIdx = 0;
			if (stepIdx > 48) stepIdx = 48;
			adpcmSignal_ += kAdpcmDiffLut[stepIdx * 16 + nib];
			if (adpcmSignal_ > 2047) adpcmSignal_ = 2047;
			if (adpcmSignal_ < -2048) adpcmSignal_ = -2048;
			stepIdx += kAdpcmIndexShift[nib & 7];
			if (stepIdx < 0) stepIdx = 0;
			if (stepIdx > 48) stepIdx = 48;
			adpcmStep_ = stepIdx;
		}
		if (!adpcmPlaying_) break;
		/* 12bit デコーダ出力。単純 <<4 はバスドラム 1 ブロックをデジタルフルスケールにし OPM 合計の行き先がなくなる。hoot の 0xF0/0xC0 PCM対OPM バランスを保つため、未スケール OPM を下げず PCM 側を減衰する。 */
		int32_t s = adpcmSignal_ * 12;
		int32_t l = s, r = s;
		if (adpcmPan_ == 1) r = 0;
		else if (adpcmPan_ == 2) l = 0;
		else if (adpcmPan_ == 3) { l = 0; r = 0; }
		int32_t ol = (int32_t)stereo[i * 2] + l;
		int32_t orr = (int32_t)stereo[i * 2 + 1] + r;
		if (ol > 32767) ol = 32767;
		if (ol < -32768) ol = -32768;
		if (orr > 32767) orr = 32767;
		if (orr < -32768) orr = -32768;
		stereo[i * 2] = (int16_t)ol;
		stereo[i * 2 + 1] = (int16_t)orr;
	}
}

/* IRQ 配送 */
int CHardX68k::AckMfpIrq()
{
	const int vec = mfpIrqVec_ ? (int)mfpIrqVec_ : M68K_INT_ACK_AUTOVECTOR;
	mfpIrqPending_ = 0;
	mfpIrqVec_ = 0;
	return vec;
}

/* IRQ 配送 */
int CHardX68k::MfpTimerDIrqArmed() const
{
	/* IERB bit4＋IMRB bit4＋TCDCR Timer-D delay ≠ stop（許可判定） */
	return ((mfp_[0x09] & 0x10u) && (mfp_[0x15] & 0x10u) && (mfp_[0x1d] & 7u)) ? 1 : 0;
}

/* 周辺クロックを進める */
void CHardX68k::TickMfp(int cpuCycles)
{
	if (cpuCycles <= 0 || cpuHz_ <= 0 || mfpIrqPending_) return;
	static const int kPre[8] = { 0, 4, 10, 16, 50, 64, 100, 200 };
	const unsigned tcdcr = mfp_[0x1d];
	const unsigned ierb = mfp_[0x09];
	const unsigned imrb = mfp_[0x15];
	const unsigned vr = mfp_[0x17] ? mfp_[0x17] : 0x40u;
	const int mfpClk = 4000000;
	int minPeriod = cpuHz_ / 4000;
	if (minPeriod < 1) minPeriod = 1;

	/* Timer D 源 4 → vec (VR&F0)|4 → VR=$40 なら $110。Timer C 源 5 → $114。 */
	const int preD = kPre[tcdcr & 7u];
	const int preC = kPre[(tcdcr >> 4) & 7u];
	if ((ierb & 0x10u) && (imrb & 0x10u) && preD > 0) {
		unsigned count = mfp_[0x25] ? (unsigned)mfp_[0x25] : 256u;
		int periodCy = (int)(((int64_t)cpuHz_ * (int64_t)count * (int64_t)preD) / mfpClk);
		if (periodCy < minPeriod) periodCy = minPeriod;
		mfpTdAcc_ += cpuCycles;
		if (mfpTdAcc_ >= periodCy) {
			mfpTdAcc_ -= periodCy;
			const unsigned vec = (vr & 0xf0u) | 4u;
			const unsigned isr = Read32(vec * 4u) & 0xffffffu;
			if (isr >= 0x400u && isr < 0x00f00000u) {
				mfpIrqPending_ = 1;
				mfpIrqVec_ = (uint8_t)vec;
			}
		}
	} else {
		mfpTdAcc_ = 0;
	}
	if (mfpIrqPending_) return;
	if ((ierb & 0x20u) && (imrb & 0x20u) && preC > 0) {
		unsigned count = mfp_[0x23] ? (unsigned)mfp_[0x23] : 256u;
		int periodCy = (int)(((int64_t)cpuHz_ * (int64_t)count * (int64_t)preC) / mfpClk);
		if (periodCy < minPeriod) periodCy = minPeriod;
		mfpTcAcc_ += cpuCycles;
		if (mfpTcAcc_ >= periodCy) {
			mfpTcAcc_ -= periodCy;
			const unsigned vec = (vr & 0xf0u) | 5u;
			const unsigned isr = Read32(vec * 4u) & 0xffffffu;
			if (isr >= 0x400u && isr < 0x00f00000u) {
				mfpIrqPending_ = 1;
				mfpIrqVec_ = (uint8_t)vec;
			}
		}
	} else {
		mfpTcAcc_ = 0;
	}
}

/* 16bit 書込 */
void CHardX68k::Write16(unsigned addr, uint16_t data)
{
	addr &= 0xffffffu;
	/* DOS ファイル操作トリガ: 下位バイトが fn（Human68k $3D/$3E/$3F/$4E/…） */
	if (addr == 0xe0001eu) {
		dosMbResult_ = DosFileOp(data & 0xffu, dosMbA1_, dosMbD0_, dosMbD1_);
		return;
	}
	Write8(addr, (uint8_t)(data >> 8));
	Write8((addr + 1) & 0xffffffu, (uint8_t)(data & 0xff));
}

/* 32bit 書込 */
void CHardX68k::Write32(unsigned addr, uint32_t data)
{
	addr &= 0xffffffu;
	if (addr == 0xe00014u) {
		dosMbD1_ = data;
		return;
	}
	if (addr == 0xe00018u) {
		dosMbA1_ = data;
		return;
	}
	if (addr == 0xe0001cu) {
		dosMbD0_ = data;
		return;
	}
	Write16(addr, (uint16_t)(data >> 16));
	Write16((addr + 2) & 0xffffffu, (uint16_t)(data & 0xffff));
}

/* CEmuX68kIsTrapF の実装 */
static int CEmuX68kIsTrapF(const char* name)
{
	if (!name) return 0;
	const char* base = name;
	for (const char* p = name; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}
	return _stricmp(base, "trap_f.bin") == 0;
}

/* Human68k .X（HU）— NetBSD hux.h / aout2hux レイアウト。64 バイトヘッダを剥がし BSS をクリアし、デルタ符号化リロケだけ適用。 */
static int CEmuX68kLoadHumanX(uint8_t* dst, unsigned dstCap, unsigned loadAddr,
	const unsigned char* data, unsigned sz)
{
	if (!dst || !data || sz < 0x40u) return 0;
	if (data[0] != 'H' || data[1] != 'U') return 0;

	const unsigned base = ((unsigned)data[4] << 24) | ((unsigned)data[5] << 16)
		| ((unsigned)data[6] << 8) | (unsigned)data[7];
	const unsigned text = ((unsigned)data[0x0c] << 24) | ((unsigned)data[0x0d] << 16)
		| ((unsigned)data[0x0e] << 8) | (unsigned)data[0x0f];
	const unsigned datasz = ((unsigned)data[0x10] << 24) | ((unsigned)data[0x11] << 16)
		| ((unsigned)data[0x12] << 8) | (unsigned)data[0x13];
	const unsigned bss = ((unsigned)data[0x14] << 24) | ((unsigned)data[0x15] << 16)
		| ((unsigned)data[0x16] << 8) | (unsigned)data[0x17];
	const unsigned rsize = ((unsigned)data[0x18] << 24) | ((unsigned)data[0x19] << 16)
		| ((unsigned)data[0x1a] << 8) | (unsigned)data[0x1b];
	const unsigned body = text + datasz;
	if (body == 0 || 0x40u + body > sz) return 0;
	if (loadAddr >= dstCap) return 0;

	unsigned n = body;
	if (loadAddr + n > dstCap)
		n = dstCap - loadAddr;
	memcpy(dst + loadAddr, data + 0x40, n);

	if (bss && loadAddr + body < dstCap) {
		unsigned bz = bss;
		if (loadAddr + body + bz > dstCap)
			bz = dstCap - (loadAddr + body);
		memset(dst + loadAddr + body, 0, bz);
	}

	/* デルタ符号化リロケ: 短い BE16、または 0x0001 + BE32 長いデルタ */
	if (rsize && 0x40u + body + rsize <= sz && n == body) {
		const unsigned char* rel = data + 0x40 + body;
		unsigned ri = 0;
		unsigned loc = 0;
		const unsigned delta = loadAddr - base;
		while (ri + 2u <= rsize) {
			unsigned w = ((unsigned)rel[ri] << 8) | (unsigned)rel[ri + 1];
			ri += 2;
			if (w == 1u) {
				if (ri + 4u > rsize) break;
				w = ((unsigned)rel[ri] << 24) | ((unsigned)rel[ri + 1] << 16)
					| ((unsigned)rel[ri + 2] << 8) | (unsigned)rel[ri + 3];
				ri += 4;
			}
			loc += w;
			if (loc + 4u > body) break;
			const unsigned at = loadAddr + loc;
			if (at + 4u > dstCap) break;
			const unsigned old = ((unsigned)dst[at] << 24) | ((unsigned)dst[at + 1] << 16)
				| ((unsigned)dst[at + 2] << 8) | (unsigned)dst[at + 3];
			const unsigned neu = old + delta;
			dst[at] = (uint8_t)(neu >> 24);
			dst[at + 1] = (uint8_t)(neu >> 16);
			dst[at + 2] = (uint8_t)(neu >> 8);
			dst[at + 3] = (uint8_t)(neu);
		}
	}
	return 1;
}

/* ARTDINK は FLOAT2.X を $2E000、A2.X を $30000 にマップするが FLOAT2 の text は $2F80 で KEEPPR が走らないため A2.X が FPU op 表を上書きする。FLOAT2 を A2.X 下の穴へ滑らせる（$2A000+$2F80=$2CF80）。 */
static unsigned CEmuX68kFloat2LoadAddr(const CEmuGameEntry* ge, unsigned xmlOff,
	const unsigned char* data, unsigned sz)
{
	if (!ge || xmlOff != 0x2e000u || !data || sz < 0x14u) return xmlOff;
	if (data[0] != 'H' || data[1] != 'U') return xmlOff;
	const unsigned text = ((unsigned)data[0x0c] << 24) | ((unsigned)data[0x0d] << 16)
		| ((unsigned)data[0x0e] << 8) | (unsigned)data[0x0f];
	const unsigned datasz = ((unsigned)data[0x10] << 24) | ((unsigned)data[0x11] << 16)
		| ((unsigned)data[0x12] << 8) | (unsigned)data[0x13];
	const unsigned end = xmlOff + text + datasz;
	int clash = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const int o = ge->rom[i].offset;
		if (o <= 0) continue;
		const unsigned u = (unsigned)o;
		if (u > xmlOff && u < end) { clash = 1; break; }
	}
	return clash ? 0x2a000u : xmlOff;
}

/* hoot opmdrv.bin: init の 2 回目（BOOT が $F08xxx IRQ/trap から再起動）は最初のパスが $10000 を上書きしたため $48E77FFE を逃す。3 つの失敗スピンを「既に初期化済み」→ restore/rts にする。 */
static void CEmuX68kFixOpmdrvBinInit(uint8_t* rom, unsigned n)
{
	if (!rom || n < 0xB9Au) return;
	if (rom[0x400] != 0 || rom[0x401] != 0 || rom[0x402] != 0x0B || rom[0x403] != 0x06)
		return;
	if (rom[0xB16] != 0x22 || rom[0xB17] != 0x3C) return;
	if (rom[0xB18] != 0x48 || rom[0xB19] != 0xE7 || rom[0xB1A] != 0x7F || rom[0xB1B] != 0xFE)
		return;
	/* bra.s $B94（movem/rts）。変位は次命令から */
	if (rom[0xB32] == 0x60 && rom[0xB33] == 0xFE) {
		rom[0xB32] = 0x60;
		rom[0xB33] = 0x60; /* 番地 B34+0x60 = B94 */
	}
	if (rom[0xB4C] == 0x60 && rom[0xB4D] == 0xFE) {
		rom[0xB4C] = 0x60;
		rom[0xB4D] = 0x46; /* 番地 B4E+0x46 = B94 */
	}
	if (rom[0xB70] == 0x60 && rom[0xB71] == 0xFE) {
		rom[0xB70] = 0x60;
		rom[0xB71] = 0x22; /* 番地 B72+0x22 = B94 */
	}
}

/* AliceSoft System3 BOOT（OPMDRV2.X + FLOAT2 + ADV + AMUS.DAT）: $15200 から $48E77FFE を 1000 ワード×3 スキャンし、$10000 を植えて OPMDRV M_INTON/M_ALLOC/M_INIT を呼ぶ。ミスはそれぞれ bra.s *。Settle が PC を trap15 イメージ（$F08xxx）と見て壊れた分類し、最初のパスが目印を上書きしたあとリセットベクタから再起動 — 再試行が永久スピン（abtengu $574、dps $55C、tousin $536）。FixOpmdrvBinInit と同様それらのスピンを M_INTON trap へ飛ばす。 */
static void CEmuX68kFixAliceOpmScan(uint8_t* rom, unsigned n)
{
	if (!rom || n < 0x600u) return;
	/* hoot opmdrv.bin グルー（$400 = $B06）とは別 */
	if (n > 0xB16u && rom[0x400] == 0 && rom[0x401] == 0
		&& rom[0x402] == 0x0B && rom[0x403] == 0x06)
		return;
	const unsigned hi = (n < 0x800u) ? n : 0x800u;
	for (unsigned a = 0x400u; a + 8u < hi; a += 2u) {
		if (rom[a] != 0x22 || rom[a + 1] != 0x3C) continue;
		if (rom[a + 2] != 0x48 || rom[a + 3] != 0xE7
			|| rom[a + 4] != 0x7F || rom[a + 5] != 0xFE)
			continue;
		unsigned tgt = 0;
		const unsigned lim = (a + 0xA0u < hi) ? (a + 0xA0u) : hi;
		for (unsigned b = a; b + 6u < lim; b += 2u) {
			if (rom[b] == 0x72 && rom[b + 1] == 0x0D
				&& rom[b + 2] == 0x70 && rom[b + 3] == 0xF0
				&& rom[b + 4] == 0x4E && rom[b + 5] == 0x4F) {
				tgt = b;
				break;
			}
		}
		if (!tgt) return;
		for (unsigned b = a; b + 2u <= tgt; b += 2u) {
			if (rom[b] != 0x60 || rom[b + 1] != 0xFE) continue;
			const int disp = (int)tgt - (int)(b + 2u);
			if (disp >= -128 && disp <= 127)
				rom[b + 1] = (uint8_t)(disp & 0xff);
		}
		return;
	}
}

/* KOEI MML（MUS*.opm）は (i) で始まりノートが続き、ボイスは TEST.OPM / EWMX.OPM。グルー WRITE は (i)、次いでボイスバンク、次いで曲を送るので、曲自身の (i) がノートコンパイル前にバンクを消す。先頭 512 バイトで (v…) が続かない先頭 init を落とす。 */
static void CEmuX68kSkipBareOpmInit(const unsigned char** pdata, unsigned* psz)
{
	const unsigned char* data = *pdata;
	unsigned sz = *psz;
	unsigned skip = 0;
	if (sz >= 5u && data[0] == '(' && data[1] == 'i' && data[2] == ')'
		&& data[3] == '\r' && data[4] == '\n')
		skip = 5u;
	else if (sz >= 4u && data[0] == '(' && data[1] == 'i' && data[2] == ')'
		&& data[3] == '\n')
		skip = 4u;
	else
		return;
	unsigned i;
	for (i = skip; i + 1u < sz && i < skip + 512u; i++) {
		if (data[i] == '(' && data[i + 1u] == 'v')
			return;
	}
	*pdata = data + skip;
	*psz = sz - skip;
}

/* SD_DRV.X の IRQ 経路は A5+$D28 の Human68k 常駐状態バイトを見る。この ROM シェルでは BGM コマンド成功後もその OS 所有バイトが 0 のままで、分岐がシーケンサを永久に飛ばす。直後の命令は通常のチャネル状態チェックでシェルに安全。この正確な版固有ゲートだけ除く。 */
static void CEmuX68kFixSdDrvHostGate(uint8_t* ram, unsigned loadAddr, unsigned body)
{
	if (!ram || body < 0xfecu) return;
	const unsigned gate = loadAddr + 0xfe4u;
	static const uint8_t sig[] = { 0x4a, 0x2a, 0x0d, 0x28, 0x67, 0x00, 0x02, 0x26 };
	if (gate + sizeof(sig) > loadAddr + body
		|| memcmp(ram + gate, sig, sizeof(sig)) != 0)
		return;
	ram[gate + 4] = 0x4e;
	ram[gate + 5] = 0x71;
	ram[gate + 6] = 0x4e;
	ram[gate + 7] = 0x71;
}

/* StarCraft OP.X / OPMDRV.X（rougea, phantas4, qstaff）: M_ALLOC のトラックプールはドライバ TEXT 約 $11A96。IRQ6 ISR / $1243C フラグも同じ窓。BOOT は $103FF の MML ワークスペースを要求し、その上に KIM.OPM/P4.OPM（約 27KB）をコンパイルする。m_and_m はコンパイルを飛ばす（曲は MAIN.X）ので ISR が残る。プールを DOS ヒープ $A00000 へ付け替える — 41F9 abs.l は直接移せる。41FA/43FA pc-rel lea は $A00000 に届かないので move.l ptr(pc),An にし、ポインタは HU データ節末尾ゼロに置く。 */
static void CEmuX68kFixOpxTrackHeap(uint8_t* rom, unsigned n, uint8_t* heapRam, unsigned heapBytes)
{
	if (!rom || n < 0x20000u) return;
	if (!heapRam || heapBytes < 0x3FF20u) return;
	/* m_and_m / phantas3 はこの OP.X を共有するが MAIN.X でコンパイル — ドライバ内プールを奪わない */
	{
		int hasMml = 0;
		const unsigned hi = (n < 0x800u) ? n : 0x800u;
		for (unsigned a = 0x400u; a + 6u < hi; a += 2u) {
			if (rom[a] == 0x43 && rom[a + 1] == 0xf9
				&& rom[a + 2] == 0 && rom[a + 3] == 0x03
				&& rom[a + 4] == 0 && rom[a + 5] == 0) {
				hasMml = 1;
				break;
			}
		}
		if (!hasMml) return;
	}
	/* VOPM は奇数番地にあることが多い（$FFFFFFFF 後のヘッダパディング） */
	unsigned load = 0;
	for (unsigned a = 0x8000u; a + 4u < 0x20000u && a + 4u < n; a++) {
		if (rom[a] == 'V' && rom[a + 1] == 'O'
			&& rom[a + 2] == 'P' && rom[a + 3] == 'M') {
			load = a & ~0xffu;
			break;
		}
	}
	if (load < 0x8000u || load > 0x18000u) return;

	unsigned pool = 0;
	int poolHits = 0;
	for (unsigned a = load; a + 6u < load + 0x8000u && a + 6u < n; a += 2u) {
		if (rom[a] != 0x41 || rom[a + 1] != 0xf9) continue;
		const unsigned dest = ((unsigned)rom[a + 2] << 24)
			| ((unsigned)rom[a + 3] << 16)
			| ((unsigned)rom[a + 4] << 8) | (unsigned)rom[a + 5];
		if (dest < load + 0x1800u || dest > load + 0x3000u) continue;
		int hits = 0;
		for (unsigned b = load; b + 6u < load + 0x8000u && b + 6u < n; b += 2u) {
			if (rom[b] == 0x41 && rom[b + 1] == 0xf9
				&& (((unsigned)rom[b + 2] << 24) | ((unsigned)rom[b + 3] << 16)
					| ((unsigned)rom[b + 4] << 8) | (unsigned)rom[b + 5]) == dest)
				hits++;
		}
		if (hits > poolHits) {
			poolHits = hits;
			pool = dest;
		}
	}
	/* 41F9/41FA M_ALLOC ポインタ表を $A00000 へ付け替えない。Play cmd 1 が同じ表を添字する（`lea table(pc)` 次いで mulu #4）。奪うと全メールボックスが空ヒープスロットになり、コンパイル系統 3 リップ（同一 KIM/P4/QS.OPM）が 1 ドローンになる。コンパイルワークスペースは $10A44/$10A48 経由で DOS ヒープに残る。 */
	(void)pool;
	(void)poolHits;

	/* M_INIT はデータ節 $152D2 / ドライバ $10000 を $10A44/$10A48 へ格納。コンパイルは TEXT を下へ埋める（ISR $11C58）。バンプを DOS ヒープへ向ける。 */
	{
		const unsigned tramp = CEMU_X68K_DOS_HEAP + 0x3FF00u;
		int planted = 0;
		for (unsigned a = load; a + 14u < load + 0x8000u && a + 14u < n; a += 2u) {
			if (rom[a] != 0x23 || rom[a + 1] != 0xc2) continue;
			if (rom[a + 6] != 0x23 || rom[a + 7] != 0xc9) continue;
			const unsigned s1 = ((unsigned)rom[a + 2] << 24) | ((unsigned)rom[a + 3] << 16)
				| ((unsigned)rom[a + 4] << 8) | (unsigned)rom[a + 5];
			const unsigned s2 = ((unsigned)rom[a + 8] << 24) | ((unsigned)rom[a + 9] << 16)
				| ((unsigned)rom[a + 10] << 8) | (unsigned)rom[a + 11];
			if (s1 != 0x10A48u || s2 != 0x10A44u) continue;
			rom[a] = 0x4e;
			rom[a + 1] = 0xf9;
			rom[a + 2] = (uint8_t)(tramp >> 24);
			rom[a + 3] = (uint8_t)(tramp >> 16);
			rom[a + 4] = (uint8_t)(tramp >> 8);
			rom[a + 5] = (uint8_t)tramp;
			planted = 1;
			break;
		}
		if (planted) {
			uint8_t* t = heapRam + 0x3FF00u;
			/* 命令 move.l #$A00000,$10A48 ; move.l #$A40000,$10A44 ; rts */
			t[0] = 0x23; t[1] = 0xfc;
			t[2] = 0x00; t[3] = 0xa0; t[4] = 0x00; t[5] = 0x00;
			t[6] = 0x00; t[7] = 0x01; t[8] = 0x0a; t[9] = 0x48;
			t[10] = 0x23; t[11] = 0xfc;
			t[12] = 0x00; t[13] = 0xa4; t[14] = 0x00; t[15] = 0x00;
			t[16] = 0x00; t[17] = 0x01; t[18] = 0x0a; t[19] = 0x44;
			t[20] = 0x4e; t[21] = 0x75;
		}
	}

	/* データ節: dc.l dataStart, $10A44 — M_INIT が dataStart を $10A44 へコピーしてコンパイルバンプにする。ヒープ頂へリダイレクト。 */
	{
		unsigned dataStart = 0;
		for (unsigned a = load + 0x5000u; a + 4u < load + 0x7000u && a + 4u < n; a += 2u) {
			if (rom[a] == 0x48 && rom[a + 1] == 0xe7
				&& rom[a + 2] == 0x7f && rom[a + 3] == 0xfe) {
				dataStart = a;
				break;
			}
		}
		if (dataStart) {
			for (unsigned a = dataStart; a + 8u < dataStart + 0x80u && a + 8u < n; a += 2u) {
				const unsigned v0 = ((unsigned)rom[a] << 24) | ((unsigned)rom[a + 1] << 16)
					| ((unsigned)rom[a + 2] << 8) | (unsigned)rom[a + 3];
				const unsigned v1 = ((unsigned)rom[a + 4] << 24) | ((unsigned)rom[a + 5] << 16)
					| ((unsigned)rom[a + 6] << 8) | (unsigned)rom[a + 7];
				if (v0 == dataStart && v1 == 0x10A44u) {
					const unsigned hi = CEMU_X68K_DOS_HEAP + 0x40000u;
					rom[a] = (uint8_t)(hi >> 24);
					rom[a + 1] = (uint8_t)(hi >> 16);
					rom[a + 2] = (uint8_t)(hi >> 8);
					rom[a + 3] = (uint8_t)hi;
					break;
				}
			}
		}
	}
}

/* コンパイル系統 BOOT（lea $30000 次いで jsr MAIN）: $48E77FFE スキャンが OP.X $10A48 ヒープスロットへ #$10000 を植えるので MML コンパイルがドライバ TEXT を埋める。その植込を DOS ヒープへ向ける。m_and_m は飛ばす（$30000 MML オーバーレイ無し）。 */
static void CEmuX68kFixOpxCompileBoot(uint8_t* rom, unsigned n, uint8_t* heapRam, unsigned heapBytes)
{
	if (!rom) return;
	if (!heapRam || heapBytes < 0x3FF40u) return;
	const unsigned hi = (n < 0x800u) ? n : 0x800u;
	int hasScan = 0, hasMml = 0;
	for (unsigned a = 0x400u; a + 8u < hi; a += 2u) {
		if (rom[a] == 0x22 && rom[a + 1] == 0x3c
			&& rom[a + 2] == 0x48 && rom[a + 3] == 0xe7
			&& rom[a + 4] == 0x7f && rom[a + 5] == 0xfe)
			hasScan = 1;
		if (rom[a] == 0x43 && rom[a + 1] == 0xf9
			&& rom[a + 2] == 0 && rom[a + 3] == 0x03
			&& rom[a + 4] == 0 && rom[a + 5] == 0)
			hasMml = 1;
	}
	if (!hasScan || !hasMml) return;
	for (unsigned a = 0x400u; a + 6u < hi; a += 2u) {
		if (rom[a] == 0x22 && rom[a + 1] == 0xbc
			&& rom[a + 2] == 0 && rom[a + 3] == 0xa0
			&& rom[a + 4] == 0 && rom[a + 5] == 0) {
			/* 22BC は既に付け替え済み、または下にまだ #$10000 */
			break;
		}
		if (rom[a] == 0x22 && rom[a + 1] == 0xbc
			&& rom[a + 2] == 0 && rom[a + 3] == 0x01
			&& rom[a + 4] == 0 && rom[a + 5] == 0) {
			rom[a + 2] = 0x00;
			rom[a + 3] = 0xa0;
			rom[a + 4] = 0x00;
			rom[a + 5] = 0x00;
			break;
		}
	}
	/* move.l #$A00000,(a1) の次は move.l a0,-4(a0); moveq #1。それらを残し $10A44 = ヒープ頂もセットするトランポリンを jsr。 */
	for (unsigned a = 0x400u; a + 6u < hi; a += 2u) {
		if (rom[a] == 0x23 && rom[a + 1] == 0x48
			&& rom[a + 2] == 0xff && rom[a + 3] == 0xfc
			&& rom[a + 4] == 0x72 && rom[a + 5] == 0x01) {
			const unsigned tramp = CEMU_X68K_DOS_HEAP + 0x3FF20u;
			rom[a] = 0x4e;
			rom[a + 1] = 0xb9;
			rom[a + 2] = (uint8_t)(tramp >> 24);
			rom[a + 3] = (uint8_t)(tramp >> 16);
			rom[a + 4] = (uint8_t)(tramp >> 8);
			rom[a + 5] = (uint8_t)tramp;
			uint8_t* t = heapRam + 0x3FF20u;
			/* 命令 move.l a0,-4(a0) ; move.l #$A40000,$10A44 ; moveq #1,d1 ; rts */
			t[0] = 0x23; t[1] = 0x48; t[2] = 0xff; t[3] = 0xfc;
			t[4] = 0x23; t[5] = 0xfc;
			t[6] = 0x00; t[7] = 0xa4; t[8] = 0x00; t[9] = 0x00;
			t[10] = 0x00; t[11] = 0x01; t[12] = 0x0a; t[13] = 0x44;
			t[14] = 0x72; t[15] = 0x01;
			t[16] = 0x4e; t[17] = 0x75;
			break;
		}
	}

	/* BOOT コンパイルが上書きする前に IRQ6 ISR をスナップショット */
	if (heapRam && heapBytes >= 0x3F880u) {
		for (unsigned a = 0x10000u; a + 12u < 0x18000u && a + 12u < n; a += 2u) {
			if (rom[a] != 0x70 || rom[a + 1] != 0x6a) continue;
			if (rom[a + 2] != 0x43 || rom[a + 3] != 0xf9) continue;
			const unsigned isr = ((unsigned)rom[a + 4] << 24) | ((unsigned)rom[a + 5] << 16)
				| ((unsigned)rom[a + 6] << 8) | (unsigned)rom[a + 7];
			if (isr < 0x11000u || isr >= 0x14000u || isr + 0x80u > n) continue;
			if (rom[isr] != 0x48 || rom[isr + 1] != 0xe7) continue;
			memcpy(heapRam + 0x3F800u, rom + isr, 0x80u);
			break;
		}
	}
}

/* CEmuX68kBasename の実装 */
static void CEmuX68kBasename(const char* path, char* out, int outMax)
{
	if (!out || outMax < 2) return;
	out[0] = 0;
	if (!path) return;
	const char* base = path;
	for (const char* p = path; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':')
			base = p + 1;
	}
	int n = 0;
	for (; base[n] && n < outMax - 1; n++) {
		char c = base[n];
		if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		out[n] = c;
	}
	out[n] = 0;
}

struct CEmuX68kOpmSlot {
	const unsigned char* data;
	unsigned size;
	char name[32];
	unsigned off;
};

/* CEmuX68kOpmSlotNameCmp の実装 */
static int CEmuX68kOpmSlotNameCmp(const void* a, const void* b)
{
	const CEmuX68kOpmSlot* sa = (const CEmuX68kOpmSlot*)a;
	const CEmuX68kOpmSlot* sb = (const CEmuX68kOpmSlot*)b;
	return _stricmp(sa->name, sb->name);
}

/* CEmuX68kIsOpmdrvName の実装 */
static int CEmuX68kIsOpmdrvName(const char* name)
{
	char base[32];
	CEmuX68kBasename(name, base, (int)sizeof(base));
	return _strnicmp(base, "OPMDRV", 6) == 0;
}

/* hoot XML は Bretonne Lays をまだ BR1000M0.OPM と書くが zip は BR_01.OPM。ファジー数字コア照合では対にならない。逃した 16KB 音楽スロット全部に残り MML がちょうど 1 つなら XML 順で落とし、タイトルバイトが実データに着地するようにする。 */
static int CEmuX68kFillMissingOpmSlots(CHardX68k* hw, CEmuZipFs* fs,
	CEmuX68kOpmSlot* miss, int missN, const unsigned char** used, int usedN)
{
	if (!hw || !fs || !miss || missN <= 0 || missN > 64) return 0;
	CEmuX68kOpmSlot left[64];
	int leftN = 0;
	for (int i = 0; i < fs->fileCount && leftN < 64; i++) {
		const unsigned char* d = fs->files[i].data;
		unsigned sz = fs->files[i].size;
		if (!d || sz < 8u) continue;
		int already = 0;
		for (int u = 0; u < usedN; u++) {
			if (used[u] == d) { already = 1; break; }
		}
		if (already) continue;
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		if (CEmuX68kIsOpmdrvName(pathA)) continue;
		if (d[0] != '(' && d[0] != '/' && d[0] != '*') continue;
		CEmuX68kOpmSlot* s = &left[leftN];
		memset(s, 0, sizeof(*s));
		s->data = d;
		s->size = sz;
		CEmuX68kBasename(pathA, s->name, (int)sizeof(s->name));
		leftN++;
	}
	if (leftN != missN) return 0;
	qsort(left, (size_t)leftN, sizeof(left[0]), CEmuX68kOpmSlotNameCmp);
	int filled = 0;
	for (int i = 0; i < missN; i++) {
		const unsigned char* data = left[i].data;
		unsigned sz = left[i].size;
		CEmuX68kSkipBareOpmInit(&data, &sz);
		unsigned n = sz;
		const unsigned off = miss[i].off;
		if (off + n > 0x400000u)
			n = 0x400000u - off;
		if (!n) continue;
		memcpy(hw->Mem() + off, data, n);
		filled++;
	}
	return filled;
}

/* CHardX68k::DosRegisterFile の実装 */
void CHardX68k::DosRegisterFile(const char* name, unsigned addr, unsigned size)
{
	if (!name || !size || dosFileCount_ >= kDosFiles) return;
	char base[32];
	CEmuX68kBasename(name, base, (int)sizeof(base));
	if (!base[0]) return;
	DosFile* f = &dosFiles_[dosFileCount_++];
	memset(f, 0, sizeof(*f));
	strncpy(f->name, base, sizeof(f->name) - 1);
	f->addr = addr & 0xffffffu;
	f->size = size;
}

/* CHardX68k::DosFindFile の実装 */
int CHardX68k::DosFindFile(const char* path) const
{
	char want[32];
	CEmuX68kBasename(path, want, (int)sizeof(want));
	if (!want[0]) return -1;
	for (int i = 0; i < dosFileCount_; i++) {
		if (_stricmp(dosFiles_[i].name, want) == 0)
			return i;
	}
	return -1;
}

/* ゲスト ASCIZ パスを読む。a1 が埋まった namecks を指すときは Human68k namecks（drive@+0、path@+1、name@+66、ext@+74）から NAME.EXT を再構成。 */
static int CEmuX68kReadDosPath(CHardX68k* hw, unsigned a1, char* path, int pathMax)
{
	if (!hw || !path || pathMax < 4) return 0;
	path[0] = 0;
	a1 &= 0xffffffu;
	const unsigned b0 = hw->Read8(a1);
	/* ヒューリスティック: namecks ドライブは 0..26、path[0] は '\\' または 0 — ASCIZ ではない */
	const unsigned b1 = hw->Read8((a1 + 1u) & 0xffffffu);
	if (b0 <= 26u && (b1 == '\\' || b1 == '/' || b1 == 0)) {
		char name[9], ext[4];
		memset(name, 0, sizeof(name));
		memset(ext, 0, sizeof(ext));
		for (int i = 0; i < 8; i++) {
			char c = (char)hw->Read8((a1 + 66u + (unsigned)i) & 0xffffffu);
			if (c == ' ' || c == 0) break;
			name[i] = c;
		}
		for (int i = 0; i < 3; i++) {
			char c = (char)hw->Read8((a1 + 74u + (unsigned)i) & 0xffffffu);
			if (c == ' ' || c == 0) break;
			ext[i] = c;
		}
		if (!name[0]) return 0;
		if (ext[0])
			_snprintf(path, (size_t)pathMax, "%s.%s", name, ext);
		else
			_snprintf(path, (size_t)pathMax, "%s", name);
		path[pathMax - 1] = 0;
		return 1;
	}
	for (int i = 0; i < pathMax - 1; i++) {
		path[i] = (char)hw->Read8((a1 + (unsigned)i) & 0xffffffu);
		if (!path[i]) { path[i] = 0; return path[0] ? 1 : 0; }
	}
	path[pathMax - 1] = 0;
	return path[0] ? 1 : 0;
}

/* Human68k NAMECK: ASCIZ パス直後に 91 バイト namecks を埋める */
static void CEmuX68kFillNamecks(CHardX68k* hw, unsigned a1, const char* path)
{
	if (!hw || !path || !path[0]) return;
	unsigned plen = 0;
	while (path[plen] && plen < 95u) plen++;
	const unsigned ncks = (a1 + plen + 1u) & 0xffffffu;
	for (unsigned i = 0; i < 91u; i++)
		hw->Write8((ncks + i) & 0xffffffu, 0);

	char drive = 0;
	const char* rest = path;
	if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
		&& path[1] == ':') {
		char d = path[0];
		if (d >= 'a' && d <= 'z') d = (char)(d - 'a' + 'A');
		drive = (char)(d - 'A' + 1);
		rest = path + 2;
	}
	hw->Write8(ncks, (uint8_t)drive);

	char base[32];
	CEmuX68kBasename(path, base, (int)sizeof(base));
	/* ベース名無しのパス部分（バックスラッシュ正規化、65 バイト） */
	{
		char dir[66];
		memset(dir, 0, sizeof(dir));
		const char* slash = nullptr;
		for (const char* p = rest; *p; p++) {
			if (*p == '/' || *p == '\\') slash = p;
		}
		int n = 0;
		if (slash) {
			for (const char* p = rest; p <= slash && n < 64; p++) {
				char c = *p;
				if (c == '/') c = '\\';
				dir[n++] = c;
			}
		}
		for (int i = 0; i < 65; i++)
			hw->Write8((ncks + 1u + (unsigned)i) & 0xffffffu, (uint8_t)dir[i]);
	}
	/* name[8] + ext[3]、Human68k 風スペースパッド */
	{
		const char* dot = nullptr;
		for (const char* p = base; *p; p++) {
			if (*p == '.') dot = p;
		}
		char name[9], ext[4];
		memset(name, ' ', 8); name[8] = 0;
		memset(ext, ' ', 3); ext[3] = 0;
		if (dot) {
			int ni = 0;
			for (const char* p = base; p < dot && ni < 8; p++)
				name[ni++] = *p;
			int ei = 0;
			for (const char* p = dot + 1; *p && ei < 3; p++)
				ext[ei++] = *p;
		} else {
			int ni = 0;
			for (const char* p = base; *p && ni < 8; p++)
				name[ni++] = *p;
		}
		for (int i = 0; i < 8; i++)
			hw->Write8((ncks + 66u + (unsigned)i) & 0xffffffu, (uint8_t)name[i]);
		for (int i = 0; i < 3; i++)
			hw->Write8((ncks + 74u + (unsigned)i) & 0xffffffu, (uint8_t)ext[i]);
	}
}

/* CHardX68k::DosFileOp の実装 */
unsigned CHardX68k::DosFileOp(unsigned fn, unsigned a1, unsigned d0, unsigned d1)
{
	fn &= 0xffu;
	if (fn == 0x4eu) {
		char path[96];
		memset(path, 0, sizeof(path));
		if (!CEmuX68kReadDosPath(this, a1, path, (int)sizeof(path)))
			return 0xffffffffu;
		if (DosFindFile(path) < 0) return 0xffffffffu;
		CEmuX68kFillNamecks(this, a1, path);
		return 0u;
	}
	if (fn == 0x3du || fn == 0x3cu) {
		char path[96];
		memset(path, 0, sizeof(path));
		if (!CEmuX68kReadDosPath(this, a1, path, (int)sizeof(path)))
			return 0xffffffffu;
		const int fi = DosFindFile(path);
		if (fi < 0) return 0xffffffffu;
		for (int h = 0; h < kDosHandles; h++) {
			if (dosHandles_[h].file < 0) {
				dosHandles_[h].file = fi;
				dosHandles_[h].pos = 0;
				return (unsigned)(5 + h);
			}
		}
		return 0xffffffffu;
	}
	if (fn == 0x3eu) {
		const int h = (int)d0 - 5;
		if (h < 0 || h >= kDosHandles) return 0xffffffffu;
		dosHandles_[h].file = -1;
		dosHandles_[h].pos = 0;
		return 0;
	}
	if (fn == 0x3fu) {
		const int h = (int)d0 - 5;
		if (h < 0 || h >= kDosHandles) return 0xffffffffu;
		const int fi = dosHandles_[h].file;
		if (fi < 0 || fi >= dosFileCount_) return 0xffffffffu;
		const DosFile* f = &dosFiles_[fi];
		unsigned pos = dosHandles_[h].pos;
		unsigned want = d1;
		if (pos >= f->size) return 0;
		if (pos + want > f->size)
			want = f->size - pos;
		for (unsigned i = 0; i < want; i++)
			Write8((a1 + i) & 0xffffffu, Read8((f->addr + pos + i) & 0xffffffu));
		dosHandles_[h].pos = pos + want;
		return want;
	}
	if (fn == 0x40u)
		return d1;
	if (fn == 0x43u) {
		const int h = (int)d0 - 5;
		if (h < 0 || h >= kDosHandles) return 0xffffffffu;
		if (dosHandles_[h].file < 0) return 0xffffffffu;
		dosHandles_[h].pos = d1;
		return d1;
	}
	if (fn == 0x4cu)
		return 0xffffffffu;
	return 0;
}

/* CHardX68k::BootIplFf0b86 の実装 */
void CHardX68k::BootIplFf0b86()
{
	/* 本物 IPL サブルーチン（trap_f @ $FF0B86 → RTS @ $FF0CAC）: BOOT.BIN が $1F0000→$FF0000 コピー後に呼ぶ MFP/ADPCM 立ち上げ。high_ は両ページを既にエイリアスするのでコピーは no-op。要るのは JSR だけ。 */
	if (!musashiReady_) return;
	if (high_[0x0b86] == 0 && high_[0x0b87] == 0) return;
	/* シグネチャ: lea $E8A000,a0 */
	if (high_[0x0b86] != 0x41 || high_[0x0b87] != 0xf9) return;

	const unsigned bootSp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	const unsigned bootPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	unsigned sp = bootSp;
	if (sp < 8u || sp > 0xfffff8u)
		sp = 0x2000u; /* IPL リセット SP */

	/* $F0 ワーク RAM の戻りトランポリン: RTS */
	const unsigned ret = 0xf0ffe0u;
	Write16(ret, 0x4e75);
	sp = (sp - 4u) & 0xffffffu;
	Write32(sp, ret);

	m68k_set_reg(M68K_REG_SR, 0x2700); /* スーパーバイザ、IPL 中は IRQ マスク */
	m68k_set_reg(M68K_REG_SP, sp);
	m68k_set_reg(M68K_REG_PC, 0xff0b86u);

	/* 上限: サブルーチンは約 0x294 バイト。余裕あるサイクル予算 */
	for (int n = 0; n < 200000; n += 64) {
		m68k_execute(64);
		const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		if (pc == ret || pc == (ret + 2u))
			break;
		/* 暴走した — 中止 */
		if (pc < 0xff0000u || pc > 0xff0fffu)
			break;
	}

	m68k_set_reg(M68K_REG_SP, bootSp);
	m68k_set_reg(M68K_REG_PC, bootPc);
	pc_ = bootPc;
}

/* zip から ROM／曲データを載せる */
int CHardX68k::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge) return 0;
	memset(rom_, 0, sizeof(rom_));
	memset(ram_, 0, sizeof(ram_));
	memset(high_, 0, sizeof(high_));
	memset(heap_, 0, sizeof(heap_));
	memset(ext_, 0, sizeof(ext_));
	memset(mfp_, 0, sizeof(mfp_));
	mfp_[0x17] = 0x40; /* VR: Timer D → vec $44 → $110（ベクタ） */
	mfpTdAcc_ = 0;
	mfpTcAcc_ = 0;
	mfpIrqPending_ = 0;
	mfpIrqVec_ = 0;
	dosFileCount_ = 0;
	memset(dosFiles_, 0, sizeof(dosFiles_));
	for (int i = 0; i < kDosHandles; i++) {
		dosHandles_[i].file = -1;
		dosHandles_[i].pos = 0;
	}
	int loaded = 0;
	int trapFLoaded = 0;
	CEmuX68kOpmSlot miss[64];
	const unsigned char* usedPtr[128];
	int missN = 0;
	int usedN = 0;

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "x") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		unsigned off = (r->offset < 0) ? 0u : (unsigned)r->offset;
		if (!data || !sz) {
			if (off >= 0x20000u && off < (unsigned)kRomBytes && missN < 64) {
				memset(&miss[missN], 0, sizeof(miss[0]));
				miss[missN].off = off;
				CEmuX68kBasename(r->name, miss[missN].name, (int)sizeof(miss[missN].name));
				missN++;
			}
			continue;
		}
		if (usedN < (int)_countof(usedPtr))
			usedPtr[usedN++] = data;
		/* Human68k リロケは type=x のみ（XML ロード番地 = 本体）。type=code は hoot 同様生バイト — XML が base-0x40 なら HU ヘッダを残し本体が offset+0x40 に着地（dsj 01.bin @67C0 → 本体 @6800）。 */
		const int isTypeX = (_stricmp(r->type, "x") == 0);

		/* trap_f / IPL 高ページ */
		if (CEmuX68kIsTrapF(r->name) || off == 0x1f0000u || off == 0xff0000u) {
			unsigned n = sz > (unsigned)kHighBytes ? (unsigned)kHighBytes : sz;
			memcpy(high_, data, n);
			trapFLoaded = 1;
			continue;
		}

		/* $E00000..$E7FFFF 追加 RAM（ADPCM／拡張。メールボックスは MMIO） */
		if (off >= (unsigned)kExtBase && off < (unsigned)kExtBase + (unsigned)kExtBytes) {
			const unsigned local = off - (unsigned)kExtBase;
			unsigned n = sz;
			if (local + n > (unsigned)kExtBytes)
				n = (unsigned)kExtBytes - local;
			memcpy(ext_ + local, data, n);
			DosRegisterFile(r->name, off, n);
			loaded++;
			continue;
		}

		/* メイン RAM $000000..$3FFFFF（4MB）。旧 mid 窓 $10xxxx はここ */
		if (off < (unsigned)kRomBytes) {
			if (isTypeX)
				off = CEmuX68kFloat2LoadAddr(ge, off, data, sz);
			if (isTypeX && CEmuX68kLoadHumanX(rom_, (unsigned)kRomBytes, off, data, sz)) {
				const unsigned text = ((unsigned)data[0x0c] << 24)
					| ((unsigned)data[0x0d] << 16)
					| ((unsigned)data[0x0e] << 8) | (unsigned)data[0x0f];
				const unsigned datasz = ((unsigned)data[0x10] << 24)
					| ((unsigned)data[0x11] << 16)
					| ((unsigned)data[0x12] << 8) | (unsigned)data[0x13];
				CEmuX68kFixSdDrvHostGate(rom_, off, text + datasz);
				DosRegisterFile(r->name, off, sz > 0x40u ? sz - 0x40u : sz);
				loaded++;
				continue;
			}
			unsigned n = sz;
			const int opmGlue = (rom_[0x400] == 0 && rom_[0x401] == 0
				&& rom_[0x402] == 0x0B && rom_[0x403] == 0x06);
			if (opmGlue && !isTypeX && off >= 0x20000u)
				CEmuX68kSkipBareOpmInit(&data, &sz);
			n = sz;
			if (off + n > (unsigned)kRomBytes)
				n = (unsigned)kRomBytes - off;
			memcpy(rom_ + off, data, n);
			DosRegisterFile(r->name, off, n);
			loaded++;
			continue;
		}

		/* 既知窓の外のオフセット — 飛ばす（推測しない） */
	}

	if (missN > 0 && rom_[0x400] == 0 && rom_[0x401] == 0
		&& rom_[0x402] == 0x0B && rom_[0x403] == 0x06) {
		const int extra = CEmuX68kFillMissingOpmSlots(this, fs, miss, missN,
			usedPtr, usedN);
		loaded += extra;
	}

	if (!loaded) return 0;
	CEmuX68kFixOpmdrvBinInit(rom_, 0x100000u);
	CEmuX68kFixAliceOpmScan(rom_, 0x100000u);
	CEmuX68kFixOpxTrackHeap(rom_, 0x20000u, heap_, (unsigned)kHeapBytes);
	CEmuX68kFixOpxCompileBoot(rom_, 0x20000u, heap_, (unsigned)kHeapBytes);

	CEmuHardX68kSetActive(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	{
		const unsigned sp = (((unsigned)rom_[0] << 24) | ((unsigned)rom_[1] << 16)
			| ((unsigned)rom_[2] << 8) | (unsigned)rom_[3]) & 0xffffffu;
		const unsigned pc = (((unsigned)rom_[4] << 24) | ((unsigned)rom_[5] << 16)
			| ((unsigned)rom_[6] << 8) | (unsigned)rom_[7]) & 0xffffffu;
		m68k_set_reg(M68K_REG_SP, sp);
		m68k_set_reg(M68K_REG_PC, pc);
	}
	musashiReady_ = 1;
	pc_ = m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	/* trap_f あり: BOOT の前に IPL $FF0B86 を一度走らせる（BOOT が JSR する同じサブルーチン） */
	if (trapFLoaded)
		BootIplFf0b86();
	/* BOOT が薄い stub を残したとき $F08000 に一貫した Human68k DOS/IOCS */
	CEmuX68kDosInstall(this);
	CEmuX68kHookFloat2(this);
	fetchCount_ = 0;
	songFlag_ = 0;
	songCode_ = 0;
	ymAddr_ = 0;
	adpcmPlaying_ = 0;
	adpcmAddr_ = 0;
	adpcmSize_ = 0;
	adpcmPos_ = 0;
	adpcmSignal_ = 0;
	adpcmStep_ = 0;
	adpcmNibble_ = 0;
	adpcmRateHz_ = 15600;
	adpcmPan_ = 0;
	adpcmPpi_ = 0x08;
	adpcmPhase_ = 0;
	adpcmPaused_ = 0;
	dmacMtc_ = 0;
	dmacMar_ = 0;
	dmacOcr_ = 0;
	dmacBtc_ = 0;
	dmacBar_ = 0;
	adpcmChainPtr_ = 0;
	adpcmChainLeft_ = 0;
	if (chip_) chip_->Reset();
	opmWrites_ = 0;
	return 1;
}
