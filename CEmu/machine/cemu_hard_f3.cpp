#include "StdAfx.h"
#include "cemu_hard_f3.h"
#include "cemu_m68k_bus.h"
#include "../chip/cemu_chip_es5505.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>

static CHardF3* g_f3Self = NULL;

/* MAME taito_en: DUART IACK は IVR を返す（68K オートベクタではない）。オートベクタ 0x78 は致命リブート stub。ファームは本物ハンドラをベクタ IVR に置く。 */
static int g_f3IntAckCount = 0;
static int g_f3IntAckLast = -1;

/* CEmuHardF3IntAck の実装 */
static int CEmuHardF3IntAck(int level)
{
	CHardF3* hw = CEmuHardF3GetActive();
	/* Musashi 既定に合わせる: IACK でラッチレベルをクリア。まだ pending ならドライバが再アサート。 */
	m68k_set_irq(M68K_IRQ_NONE);
	g_f3IntAckCount++;
	if (!hw) {
		g_f3IntAckLast = -1;
		return M68K_INT_ACK_AUTOVECTOR;
	}
	g_f3IntAckLast = (int)hw->DuartIvr();
	(void)level;
	return g_f3IntAckLast;
}

int CEmuHardF3IntAckCount() { return g_f3IntAckCount; }
int CEmuHardF3IntAckLast() { return g_f3IntAckLast; }
void CEmuHardF3IntAckReset() { g_f3IntAckCount = 0; g_f3IntAckLast = -1; }

/* CEmuHardF3SetActive の実装 */
void CEmuHardF3SetActive(CHardF3* hw)
{
	g_f3Self = hw;
	CEmuM68kBusSetF3(hw);
	if (hw)
		m68k_set_int_ack_callback(CEmuHardF3IntAck);
	else
		m68k_set_int_ack_callback(NULL); /* X68k 用に Musashi オートベクタを戻す */
}

/* CEmuHardF3GetActive の実装 */
CHardF3* CEmuHardF3GetActive()
{
	return g_f3Self ? g_f3Self : CEmuM68kBusGetF3();
}

CHardF3::CHardF3()
	: cpuHz_(15238100)
	, esHz_(15238100)
	, audioCpu_(NULL)
	, audioCpuSize_(0)
	, ensoniq_(NULL)
	, ensoniqSize_(0)
	, bankMask_(0)
	, cpuBankMax_(1)
	, chip_(NULL)
	, sampleRate_(44100)
	, musashiReady_(0)
	, songCode_(1)
{
	hardKind = KIND_F3;
	memset(osram_, 0, sizeof(osram_));
	memset(dpram_, 0, sizeof(dpram_));
	memset(dpramReadHits_, 0, sizeof(dpramReadHits_));
	dpramWriteHits_ = 0;
	dpramTraceN_ = 0;
	memset(otisBank_, 0, sizeof(otisBank_));
	memset(calcOtisBank_, 0, sizeof(calcOtisBank_));
	memset(cpuBankEntry_, 0, sizeof(cpuBankEntry_));
	memset(duart_, 0, sizeof(duart_));
	memset(esp_, 0, sizeof(esp_));
	duartIrqPending_ = 0;
	duartImr_ = 0x08; /* ソフトが IMR を組み直すまでタイマ IRQ は既定で許可 */
	duartIsr_ = 0;
	duartAcr_ = 0x30;
	duartCtr_ = 0x09c4;
	duartCounterOn_ = 0;
	duartFires_ = 0;
	duartTimerAcc_ = 0;
	ringInited_ = 0;
	duartIp_ = 0x83; /* IP0/IP1 はストラップ High、bit7 は常に 1（MAME mc68681） */
	duartIpcr_ = 0x03;
	duartIpAcc_ = 0;
	esWrites_ = 0;
}

CHardF3::~CHardF3()
{
	Shutdown();
}

/* チップと CPU を生成する */
int CHardF3::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	if (_stricmp(ge->subtype, "f3system") != 0)
		return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = 15238100; /* 音源クロック 30.47618 MHz / 2 */
	esHz_ = 15238100;
	chip_ = CEmuChipEs5505Create((uint32_t)esHz_, sampleRate_);
	musashiReady_ = 0;
	return chip_ ? 1 : 0;
}

/* チップ／CPU／ROM を破棄する */
void CHardF3::Shutdown()
{
	if (CEmuHardF3GetActive() == this)
		CEmuHardF3SetActive(NULL);
	if (chip_) {
		chip_->SetPcmRom(NULL, 0);
		CEmuChipEs5505Destroy(chip_);
		chip_ = NULL;
	}
	if (audioCpu_) { free(audioCpu_); audioCpu_ = NULL; audioCpuSize_ = 0; }
	if (ensoniq_) { free(ensoniq_); ensoniq_ = NULL; ensoniqSize_ = 0; }
	musashiReady_ = 0;
}

/* PCM／コードバンク */
void CHardF3::RebuildOtisBanks()
{
	for (int i = 0; i < kOtisBankWords; i++) {
		calcOtisBank_[i] = (otisBank_[i] & bankMask_) << 20;
		if (chip_)
			CEmuChipEs5505SetVoiceBank(chip_, i, calcOtisBank_[i]);
	}
}

void CHardF3::PlaceEvenBytes(uint8_t* dst, unsigned dstBytes, unsigned dstOff,
	const uint8_t* src, unsigned srcBytes)
{
	if (!dst || !src || !srcBytes || dstOff >= dstBytes) return;
	unsigned di = dstOff;
	for (unsigned si = 0; si < srcBytes && di < dstBytes; si++, di += 2)
		dst[di] = src[si];
}

void CHardF3::PlaceInterleaved(uint8_t* dst, unsigned dstBytes, unsigned dstOff,
	const uint8_t* even, unsigned evenBytes, const uint8_t* odd, unsigned oddBytes)
{
	if (!dst) return;
	if (even && evenBytes)
		PlaceEvenBytes(dst, dstBytes, dstOff, even, evenBytes);
	if (odd && oddBytes)
		PlaceEvenBytes(dst, dstBytes, dstOff + 1, odd, oddBytes);
}

/* CEmuF3ContainsI の実装 */
static int CEmuF3ContainsI(const char* hay, const char* needle)
{
	if (!hay || !needle || !needle[0]) return 0;
	const size_t n = strlen(needle);
	for (const char* p = hay; *p; p++) {
		if (_strnicmp(p, needle, (unsigned)n) == 0) return 1;
	}
	return 0;
}

/* CEmuF3AudioCpuScore の実装 */
static int CEmuF3AudioCpuScore(const char* name, const char* type, unsigned sz)
{
	int score = 0;
	if (type && type[0]) {
		/* カタログは taito_en:audiocpu 半体に sub0/sub1（偶数／奇数） */
		if (_stricmp(type, "audiocpu") == 0 || _stricmp(type, "soundcpu") == 0
			|| _stricmp(type, "sub0") == 0 || _stricmp(type, "sub1") == 0
			|| _stricmp(type, "sub") == 0 || _stricmp(type, "code") == 0
			|| _stricmp(type, "68k") == 0)
			score += 200;
		if (_strnicmp(type, "main", 4) == 0)
			score -= 400; /* 68EC020 プログラム — 音源 CPU としてロードしない */
		if (_stricmp(type, "ensoniq") == 0 || _stricmp(type, "pcm") == 0
			|| _stricmp(type, "wave") == 0 || _stricmp(type, "adpcm") == 0)
			score -= 300;
	}
	if (sz == 0x40000) score += 80;
	else if (sz == 0x80000) score += 40;
	else if (sz == 0x20000) score += 50;
	else if (sz >= 0x100000) score -= 80;
	if (CEmuF3ContainsI(name, ".32") || CEmuF3ContainsI(name, ".33")
		|| CEmuF3ContainsI(name, ".5") || CEmuF3ContainsI(name, ".6"))
		score += 40;
	if (CEmuF3ContainsI(name, "snd") || CEmuF3ContainsI(name, "sound")) score += 30;
	if (CEmuF3ContainsI(name, "cpu")) score += 20;
	return score;
}

/* CEmuF3EnsoniqScore の実装 */
static int CEmuF3EnsoniqScore(const char* name, const char* type, unsigned sz)
{
	int score = 0;
	if (type && type[0]) {
		if (_stricmp(type, "ensoniq") == 0 || _stricmp(type, "pcm") == 0
			|| _stricmp(type, "wave") == 0 || _stricmp(type, "otis") == 0)
			score += 200;
		if (_stricmp(type, "audiocpu") == 0 || _stricmp(type, "code") == 0)
			score -= 300;
	}
	if (sz >= 0x100000) score += 100;
	if (sz >= 0x200000) score += 40;
	if (CEmuF3ContainsI(name, ".rom") || CEmuF3ContainsI(name, ".bin")) score += 10;
	if (sz <= 0x80000) score -= 40;
	return score;
}

/* 8bit 読込 */
uint8_t CHardF3::Read8(unsigned addr)
{
	addr &= 0xffffffu;
	/* MAME taito_en: OSRAM 64KB が 0-0xFFFF、mirror(0x30000) → 0-0x3FFFF、高ミラー 0xFF0000。ファーム（SD-1 由来）は両方使う。 */
	if (addr < 0x40000u || (addr >= 0xff0000u && addr <= 0xffffffu)) {
		return osram_[addr & 0xffffu];
	}
	if (addr >= 0x140000u && addr <= 0x140fffu) {
		/* DPRAM 右側、umask16 0xff00 → 各ワードの上位バイト */
		const unsigned o = (addr - 0x140000u) >> 1;
		if (o < kDpramBytes) {
			/* 64 バイト×32 バケツでカウンタが DPRAM 全体を覆う: ホストコマンドパケットはバケツ 0、リング head/tail ロングは $900/$904（バケツ 18）。先頭 32 バイトだけだと「一度も poll していない」と「ポインタは poll するがパケットは読まない」を区別できない。 */
			dpramReadHits_[o >> 6]++;
			if (dpramTraceN_ < 16) {
				/* どのファームルーチンがメールボックスに触るか。ホスト側を推測リングではなく本物プロトコルに合わせる。 */
				const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PPC);
				int seen = 0;
				for (int t = 0; t < dpramTraceN_; t++)
					if (dpramTracePc_[t] == pc) { seen = 1; break; }
				if (!seen) {
					dpramTracePc_[dpramTraceN_] = pc;
					dpramTraceOff_[dpramTraceN_] = o;
					dpramTraceN_++;
				}
			}
			if (addr & 1) return 0xff;
			return dpram_[o];
		}
		return 0xff;
	}
	if (addr >= 0x200000u && addr <= 0x20001fu) {
		const uint16_t v = CEmuChipEs5505Read(chip_, (addr - 0x200000u) >> 1);
		return (addr & 1) ? (uint8_t)(v & 0xff) : (uint8_t)(v >> 8);
	}
	if (addr >= 0x260000u && addr <= 0x2601ffu) {
		if (addr & 1) return esp_[(addr - 0x260000u) >> 1];
		return 0xff;
	}
	if (addr >= 0x280000u && addr <= 0x28001fu) {
		/* MC68681: umask16 0x00ff — 各ワードの下位バイト。Reg index = (addr>>1)&0xf。 */
		if (!(addr & 1)) return 0xff;
		const unsigned reg = ((addr - 0x280000u) >> 1) & 0x0fu;
		switch (reg) {
		case 0x1: /* SRA: RxRDY|TxRDY|TxEMT — UART poll ループに TxEMT が要る */
		case 0x9: /* SRB ステータス */
			return 0x01 | 0x04 | 0x08;
		case 0x3: /* CRA リードバックへ SRA をミラーしない */
			return duart_[reg];
		case 0x5: /* ISR — MAME はレジスタを返す。Counter Ready の ack はこの読みではなく Stop Counter（0x0F）のみ。 */
			return duartIsr_;
		case 0x4: /* IPCR: bits 0-3 = IP0-3、bits 4-7 = 状態変化。読むと COS ビットと ISR bit 7 をクリア（MAME mc68681）。 */
			{
				const uint8_t v = duartIpcr_;
				duartIpcr_ &= 0x0f;
				duartIsr_ &= (uint8_t)~0x80;
				UpdateDuartIrq();
				return v;
			}
		case 0x6: /* CTUR カウンタ上位 */
			return (uint8_t)(duartCtr_ >> 8);
		case 0x7: /* CTLR カウンタ下位 */
			return (uint8_t)(duartCtr_ & 0xff);
		case 0xc: /* IVR 割り込みベクタ */
			return DuartIvr();
		case 0xd: /* IP — Gun Buster は IP0/IP1 High。IP2=1MHz、IP3=0.5MHz */
			return (uint8_t)(duartIp_ | 0x80);
		case 0xe:
			/* START COUNTER コマンド。タイマモード（ACR bit 6）はカウントを再開。Counter Ready は Stop Counter までセットのまま（MAME）。 */
			duartCounterOn_ = 1;
			duartTimerAcc_ = 0;
			return 0x00;
		case 0xf:
			/* STOP COUNTER コマンド: Counter Ready をクリア。カウンタモード（ACR bit 6 クリア）ではカウントも停止。 */
			if (!(duartAcr_ & 0x40))
				duartCounterOn_ = 0;
			duartIsr_ &= (uint8_t)~0x08;
			UpdateDuartIrq();
			return 0x00;
		default:
			return duart_[reg];
		}
	}
	if (addr >= 0xc00000u && addr <= 0xc1ffffu) {
		const unsigned bank = cpuBankEntry_[0] % (cpuBankMax_ ? cpuBankMax_ : 1);
		const unsigned off = (addr - 0xc00000u) + 0x100000u + bank * 0x20000u;
		return (audioCpu_ && off < audioCpuSize_) ? audioCpu_[off] : 0xff;
	}
	if (addr >= 0xc20000u && addr <= 0xc3ffffu) {
		const unsigned bank = cpuBankEntry_[1] % (cpuBankMax_ ? cpuBankMax_ : 1);
		const unsigned off = (addr - 0xc20000u) + 0x100000u + bank * 0x20000u;
		return (audioCpu_ && off < audioCpuSize_) ? audioCpu_[off] : 0xff;
	}
	if (addr >= 0xc40000u && addr <= 0xc7ffffu) {
		const unsigned bank = cpuBankEntry_[2] % (cpuBankMax_ ? cpuBankMax_ : 1);
		const unsigned off = (addr - 0xc40000u) + 0x100000u + bank * 0x20000u;
		return (audioCpu_ && off < audioCpuSize_) ? audioCpu_[off] : 0xff;
	}
	return 0xff;
}

/* 16bit 読込 */
uint16_t CHardF3::Read16(unsigned addr)
{
	addr &= 0xffffffu;
	if (addr >= 0x200000u && addr <= 0x20001fu && !(addr & 1))
		return CEmuChipEs5505Read(chip_, (addr - 0x200000u) >> 1);
	return (uint16_t)((Read8(addr) << 8) | Read8(addr + 1));
}

/* 32bit 読込 */
uint32_t CHardF3::Read32(unsigned addr)
{
	return ((uint32_t)Read16(addr) << 16) | (uint32_t)Read16(addr + 2);
}

/* 8bit 書込 */
void CHardF3::Write8(unsigned addr, uint8_t data)
{
	addr &= 0xffffffu;
	if (addr < 0x40000u || (addr >= 0xff0000u && addr <= 0xffffffu)) {
		osram_[addr & 0xffffu] = data;
		return;
	}
	if (addr >= 0x140000u && addr <= 0x140fffu) {
		const unsigned o = (addr - 0x140000u) >> 1;
		if (o < kDpramBytes && !(addr & 1))
			dpram_[o] = data;
		return;
	}
	if (addr >= 0x200000u && addr <= 0x20001fu && chip_) {
		const unsigned reg = (addr - 0x200000u) >> 1;
		uint16_t cur = CEmuChipEs5505Read(chip_, reg);
		if (addr & 1) cur = (uint16_t)((cur & 0xff00) | data);
		else cur = (uint16_t)((cur & 0x00ff) | (data << 8));
		chip_->Write(reg, cur);
		esWrites_++;
		return;
	}
	if (addr >= 0x260000u && addr <= 0x2601ffu) {
		if (addr & 1) esp_[(addr - 0x260000u) >> 1] = data;
		return;
	}
	if (addr >= 0x280000u && addr <= 0x28001fu) {
		if (!(addr & 1)) return;
		const unsigned reg = ((addr - 0x280000u) >> 1) & 0x0fu;
		duart_[reg] = data;
		if (reg == 0x4) {
			duartAcr_ = data;
			/* タイマモード（bit 6）は選択後フリーラン。カウンタモードは start-counter 読みで武装する。 */
			if (duartAcr_ & 0x40)
				duartCounterOn_ = 1;
			/* ACR bits 0-3 は IP0-3 状態変化 IRQ を許可（MAME） */
			if ((duartIpcr_ >> 4) & (data & 0x0f)) {
				duartIsr_ |= 0x80;
				UpdateDuartIrq();
			}
		}
		if (reg == 0x5) {
			/* IMR 書き込み（ISR 番地への書き） */
			duartImr_ = data;
			UpdateDuartIrq();
		}
		if (reg == 0x6) {
			duartCtr_ = (uint16_t)((duartCtr_ & 0x00ff) | ((uint16_t)data << 8));
		}
		if (reg == 0x7) {
			duartCtr_ = (uint16_t)((duartCtr_ & 0xff00) | data);
		}
		if (reg == 0xc) {
			duart_[0x0c] = data ? data : 0x40;
		}
		/* 書き側の 0x0E/0x0F は出力ポートビットの set/reset。カウンタコマンドではない（それらは読み側）。ここで ISR をクリアするとファーム待ちの counter-ready が落ちる。 */
		return;
	}
	if (addr >= 0x300000u && addr <= 0x30003fu) {
		const unsigned o = (addr - 0x300000u) >> 1;
		if (o < kOtisBankWords) {
			if (!(addr & 1))
				otisBank_[o] = (uint16_t)((otisBank_[o] & 0x00ff) | (data << 8));
			else
				otisBank_[o] = (uint16_t)((otisBank_[o] & 0xff00) | data);
			RebuildOtisBanks();
		}
		return;
	}
	/* 0x340000 ボリューム（MB87078）— 当面無視 */
}

/* 16bit 書込 */
void CHardF3::Write16(unsigned addr, uint16_t data)
{
	addr &= 0xffffffu;
	if (addr >= 0x200000u && addr <= 0x20001fu && !(addr & 1) && chip_) {
		chip_->Write((addr - 0x200000u) >> 1, data);
		esWrites_++;
		return;
	}
	if (addr >= 0x300000u && addr <= 0x30003fu && !(addr & 1)) {
		const unsigned o = (addr - 0x300000u) >> 1;
		if (o < kOtisBankWords) {
			otisBank_[o] = data;
			RebuildOtisBanks();
		}
		return;
	}
	Write8(addr, (uint8_t)(data >> 8));
	Write8(addr + 1, (uint8_t)(data & 0xff));
}

/* 32bit 書込 */
void CHardF3::Write32(unsigned addr, uint32_t data)
{
	Write16(addr, (uint16_t)(data >> 16));
	Write16(addr + 2, (uint16_t)(data & 0xffff));
}

/* IRQ 配送 */
void CHardF3::UpdateDuartIrq()
{
	const int was = duartIrqPending_;
	duartIrqPending_ = (duartIsr_ & duartImr_) ? 1 : 0;
	/*
	 * Musashi は IRQ を m68k_set_irq / SR 書きでのみサンプルする。IMR が既に ISR をセットしたタイマをタイムスライス途中で許可したとき（STOP #$2000 直前）、ここで線をアサートし STOP の set_sr() check_interrupts が起きる。
	 */
	if (duartIrqPending_ && CEmuHardF3GetActive() == this)
		m68k_set_irq(M68K_IRQ_6);
	else if (was && !duartIrqPending_ && CEmuHardF3GetActive() == this)
		m68k_set_irq(M68K_IRQ_NONE);
}

/* バス読込 */
unsigned CHardF3::DpramMovepRead(unsigned byteOff) const
{
	/* $140000+off の MOVEP.W は偶数バイト off と off+2 を読む → dpram[off/2], dpram[off/2+1] */
	const unsigned i0 = (byteOff >> 1) & (kDpramBytes - 1);
	const unsigned i1 = ((byteOff + 2) >> 1) & (kDpramBytes - 1);
	return ((unsigned)dpram_[i0] << 8) | (unsigned)dpram_[i1];
}

/* バス書込 */
void CHardF3::DpramMovepWrite(unsigned byteOff, unsigned value)
{
	const unsigned i0 = (byteOff >> 1) & (kDpramBytes - 1);
	const unsigned i1 = ((byteOff + 2) >> 1) & (kDpramBytes - 1);
	dpram_[i0] = (uint8_t)((value >> 8) & 0xff);
	dpram_[i1] = (uint8_t)(value & 0xff);
}

/* バス書込 */
void CHardF3::DpramRingWriteByte(unsigned byteOff, uint8_t data)
{
	/* 音源 CPU は even d1 で move.b (a0,d1.w) → dpram[d1/2] */
	const unsigned i = (byteOff >> 1) & (kDpramBytes - 1);
	dpram_[i] = data;
}

/* CHardF3::EnqueueRingPacket の実装 */
void CHardF3::EnqueueRingPacket(const uint8_t* bytes, int nbytes)
{
	if (!bytes || nbytes < 1) return;
	unsigned wp = DpramMovepRead(0x900);
	if (wp >= 0x800) wp = 0;
	for (int i = 0; i < nbytes; i++) {
		DpramRingWriteByte(wp, bytes[i]);
		wp += 2;
		if (wp >= 0x800) wp = 0;
	}
	DpramMovepWrite(0x900, wp);
}

/* CHardF3::EnsureHostRing の実装 */
void CHardF3::EnsureHostRing()
{
	/*
	 * ファーム C10FFA は 03008100 + MOVEP.W wp=6 を書く。既に走っていたら $904 の読みポインタを触らない — rp=wp を植えるとリングが空に見え、今キューしたパケットが孤立する。
	 */
	if (dpram_[0] == 0x03 && dpram_[1] == 0x81) {
		ringInited_ = 1;
		return;
	}
	dpram_[0] = 0x03;
	dpram_[1] = 0x81;
	dpram_[2] = 0x00;
	dpram_[3] = 0x00;
	DpramMovepWrite(0x900, 6);
	/* $904（読みポインタ）は 0 のまま。遅いハンドシェイク消費がオフセット 6 のパケットへ歩ける。 */
	ringInited_ = 1;
}

/* 曲コマンドをメールボックスへ書く */
void CHardF3::SetSongCommand(unsigned code)
{
	songCode_ = code ? code : 1;
	/*
	 * F3 メールボックス（arabianm ファームから読み取り）:
	 *   リング $140000、偶数バイトのみ、MOVEP head $900 / tail $904
	 *   パケット = [len][cmd][args…]、len = 1 + len 後のバイト数
	 * $C11048 のリーダは cmd の bit 7 必須。その後 $C12E1C でディスパッチするタスク開始。添字 (cmd & $7f) は <= $10、len は $C131B0 表のコマンド毎エントリと一致必須。さもなくばパケットは黙って捨てられる。ハンドラはワード表 $C1318E。
	 * cmd $80/$81/$82 はすべて引数 1 で $C12B8C の再生ルーチンへ。
	 *
	 * 曲 ID は ($D404)+8 の long 表を添字。arabianm は 1, 2, 9, $0A… にエントリ、他はゼロ。ゼロは「その曲は無い」。
	 */
	EnsureHostRing();
	const uint8_t lo = (uint8_t)(songCode_ & 0xff);
	const uint8_t hi = (uint8_t)((songCode_ >> 8) & 0xff);
	uint8_t pkt[4];
	pkt[0] = 0x03;
	pkt[1] = 0x80; /* cmd 添字 0 */
	pkt[2] = lo ? lo : 1;
	EnqueueRingPacket(pkt, 3);
	if (hi) {
		pkt[1] = 0x82;
		pkt[2] = hi;
		EnqueueRingPacket(pkt, 3);
	}
}

/* 遅延トランポリンの D4A6 分岐を探す */
static unsigned CHardF3FindDelayGate(uint8_t* cpu, unsigned size)
{
	if (!cpu || size < 0x100010u) return 0;
	const unsigned win0 = 0x100000u;
	const unsigned win1 = size < 0x120000u ? size : 0x120000u;
	for (unsigned i = win0; i + 8u <= win1; i += 2) {
		if (cpu[i] == 0x4a && cpu[i + 1] == 0x78
			&& cpu[i + 2] == 0xd4 && cpu[i + 3] == 0xa6
			&& cpu[i + 5] == 0x12 && cpu[i + 6] == 0x2f && cpu[i + 7] == 0x0e
			&& (cpu[i + 4] == 0x67 || cpu[i + 4] == 0x60))
			return i;
	}
	return 0;
}

/* CHardF3::DisableDelaySeqTick の実装 */
void CHardF3::DisableDelaySeqTick()
{
	/* Open 後、遅延トランポリンの jsr C1490A は SSP 上で約 14Hz。USP 上のメールボックス type-$E は第 2 エンベロープクロック無しの同じ C1490A。jsr を Bra で飛ばし、ブート風呼び出しが戻れるよう subq は残す。 */
	const unsigned i = CHardF3FindDelayGate(audioCpu_, audioCpuSize_);
	if (i)
		audioCpu_[i + 4] = 0x60;
}

/* OverlayTitle で頭待ちを再開するため beq に戻す */
void CHardF3::EnableDelaySeqTick()
{
	const unsigned i = CHardF3FindDelayGate(audioCpu_, audioCpuSize_);
	if (i)
		audioCpu_[i + 4] = 0x67;
}

/* 周辺クロックを進める */
int CHardF3::TickDuart(int cpuCycles)
{
	if (cpuCycles <= 0) return duartIrqPending_;
	/*
	 * Gun Buster: IP2/IP5 = 1 MHz、IP3/IP4 = 0.5 MHz。Ensoniq 校正と IPCR COS IRQ（ACR bits 0-3）がこれらのエッジを見る。
	 */
	duartIpAcc_ += cpuCycles;
	{
		const int hz = cpuHz_ > 0 ? cpuHz_ : 15238100;
		const int64_t step = (int64_t)hz / 2000000; /* 1 MHz エッジ */
		if (step < 1) {
			/* 直前の IP を保持 */
		} else {
			const unsigned edges = (unsigned)(duartIpAcc_ / step);
			uint8_t ip = 0x83;
			if (edges & 1u) ip |= 0x24;       /* 入力ピン IP2＋IP5 */
			if ((edges >> 1) & 1u) ip |= 0x18; /* 入力ピン IP3＋IP4 */
			if (ip != duartIp_) {
				const uint8_t changed = (uint8_t)((ip ^ duartIp_) & 0x0f);
				duartIpcr_ = (uint8_t)((ip & 0x0f) | (changed << 4));
				duartIp_ = ip;
				if (duartAcr_ & changed) {
					duartIsr_ |= 0x80;
					UpdateDuartIrq();
				}
			}
		}
	}
	/*
	 * タイマレート: DUART クロック 16/4=4MHz、ACR=$30 → X1/CLK/16 モードビット。
	 * ファームは CTR=$09C4。周期 ≈ CTR * 16 / 4MHz（CPU 約 15.2MHz）。
	 * CTR 基準周期を妥当な IRQ レート（約 0.5–2 kHz）へクランプ。
	 */
	if (!duartCounterOn_) return duartIrqPending_;
	unsigned ctr = duartCtr_ ? duartCtr_ : 0x09c4;
	int period = (int)((int64_t)ctr * 16 * (int64_t)cpuHz_ / 4000000);
	if (period < cpuHz_ / 2000) period = cpuHz_ / 2000;
	if (period < 1000) period = 1000;
	duartTimerAcc_ += cpuCycles;
	while (duartTimerAcc_ >= period) {
		duartTimerAcc_ -= period;
		duartIsr_ |= 0x08; /* カウンタ／タイマ ready */
		duartFires_++;
		UpdateDuartIrq();
	}
	return duartIrqPending_;
}

/* zip から ROM／曲データを載せる */
int CHardF3::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	(void)titleCode;
	if (!fs || !ge || !chip_) return 0;
	if (audioCpu_) { free(audioCpu_); audioCpu_ = NULL; audioCpuSize_ = 0; }
	if (ensoniq_) { free(ensoniq_); ensoniq_ = NULL; ensoniqSize_ = 0; }
	memset(osram_, 0, sizeof(osram_));
	memset(dpram_, 0, sizeof(dpram_));
	memset(otisBank_, 0, sizeof(otisBank_));
	memset(duart_, 0, sizeof(duart_));
	memset(esp_, 0, sizeof(esp_));
	memset(dpramReadHits_, 0, sizeof(dpramReadHits_));
	dpramWriteHits_ = 0;
	dpramTraceN_ = 0;
	duartIrqPending_ = 0;
	duartImr_ = 0;
	duartIsr_ = 0;
	duartAcr_ = 0x30;
	duartCtr_ = 0x09c4;
	duartCounterOn_ = 0;
	duartFires_ = 0;
	duartTimerAcc_ = 0;
	duart_[0x0c] = 0x0f;
	ringInited_ = 0;
	duartIp_ = 0x83;
	duartIpcr_ = 0x03;
	duartIpAcc_ = 0;
	esWrites_ = 0;

	struct Cand { int idx; int score; unsigned size; int fromGe; char name[CEMU_ROM_NAME]; };
	Cand cpuC[64]; int cpuN = 0;
	Cand pcmC[64]; int pcmN = 0;

	auto pushCand = [](Cand* arr, int* n, int cap, int idx, int score, unsigned size, int fromGe, const char* name) {
		if (!n || *n >= cap || score <= 0) return;
		Cand& c = arr[(*n)++];
		c.idx = idx; c.score = score; c.size = size; c.fromGe = fromGe;
		strncpy_s(c.name, name ? name : "", _TRUNCATE);
	};

	if (ge->rom && ge->romCount > 0) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || data == (const unsigned char*)1 || !sz) continue;
			pushCand(cpuC, &cpuN, 64, i, CEmuF3AudioCpuScore(r->name, r->type, sz), sz, 1, r->name);
			pushCand(pcmC, &pcmN, 64, i, CEmuF3EnsoniqScore(r->name, r->type, sz), sz, 1, r->name);
		}
	}
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const char* base = pathA;
		const char* slash = strrchr(pathA, '\\');
		if (slash) base = slash + 1;
		slash = strrchr(base, '/');
		if (slash) base = slash + 1;
		const unsigned sz = fs->files[i].size;
		if (!fs->files[i].data || !sz) continue;
		pushCand(cpuC, &cpuN, 64, i, CEmuF3AudioCpuScore(base, "", sz), sz, 0, base);
		pushCand(pcmC, &pcmN, 64, i, CEmuF3EnsoniqScore(base, "", sz), sz, 0, base);
	}

	/* 候補をスコア降順、次いで名前でソート */
	auto sortCand = [](Cand* a, int n) {
		for (int i = 0; i < n; i++) {
			for (int j = i + 1; j < n; j++) {
				if (a[j].score > a[i].score
					|| (a[j].score == a[i].score && _stricmp(a[j].name, a[i].name) < 0)) {
					Cand t = a[i]; a[i] = a[j]; a[j] = t;
				}
			}
		}
	};
	sortCand(cpuC, cpuN);
	sortCand(pcmC, pcmN);

	/* cpu を名前で重複排除。インタリーブ用に似たサイズの上位ペアを残す。メイン CPU 残りよりカタログ sub0/sub1（score≥200）を優先。 */
	Cand cpuPick[8]; int cpuPickN = 0;
	for (int pass = 0; pass < 2 && cpuPickN < 2; pass++) {
		for (int i = 0; i < cpuN && cpuPickN < 8; i++) {
			if (pass == 0 && cpuC[i].score < 200) continue;
			int dup = 0;
			for (int j = 0; j < cpuPickN; j++) {
				if (_stricmp(cpuPick[j].name, cpuC[i].name) == 0) { dup = 1; break; }
			}
			if (dup) continue;
			if (cpuC[i].size > 0x100000) continue;
			cpuPick[cpuPickN++] = cpuC[i];
		}
	}

	if (getenv("CEMU_F3_DEBUG")) {
		for (int i = 0; i < cpuPickN; i++)
			fprintf(stderr, "F3 cpuPick[%d] %s score=%d size=%u fromGe=%d\n",
				i, cpuPick[i].name, cpuPick[i].score, cpuPick[i].size, cpuPick[i].fromGe);
	}

	audioCpuSize_ = 0x180000;
	audioCpu_ = (uint8_t*)malloc(audioCpuSize_);
	if (!audioCpu_) return 0;
	memset(audioCpu_, 0xff, audioCpuSize_);

	if (cpuPickN >= 2) {
		/* 先頭 2 つを 0x100000 の偶数／奇数として組む。カタログ sub0=偶数、sub1=奇数を優先。 */
		Cand a = cpuPick[0], b = cpuPick[1];
		auto typeOf = [&](const Cand& c) -> const char* {
			if (c.fromGe && ge->rom && c.idx >= 0 && c.idx < ge->romCount)
				return ge->rom[c.idx].type;
			return "";
		};
		const char* ta = typeOf(a);
		const char* tb = typeOf(b);
		if (_stricmp(ta, "sub1") == 0 && _stricmp(tb, "sub0") == 0) {
			Cand t = a; a = b; b = t;
		} else if (!(_stricmp(ta, "sub0") == 0 && _stricmp(tb, "sub1") == 0)) {
			if (_stricmp(a.name, b.name) > 0) { Cand t = a; a = b; b = t; }
		}
		const unsigned char* da = NULL; unsigned sa = 0;
		const unsigned char* db = NULL; unsigned sb = 0;
		if (a.fromGe)
			da = CEmuZipFsFind(fs, a.name, &sa);
		else if (a.idx >= 0 && a.idx < fs->fileCount) {
			da = fs->files[a.idx].data; sa = fs->files[a.idx].size;
		}
		if (b.fromGe)
			db = CEmuZipFsFind(fs, b.name, &sb);
		else if (b.idx >= 0 && b.idx < fs->fileCount) {
			db = fs->files[b.idx].data; sb = fs->files[b.idx].size;
		}
		if (da && db && da != (const unsigned char*)1 && db != (const unsigned char*)1)
			PlaceInterleaved(audioCpu_, audioCpuSize_, 0x100000, da, sa, db, sb);
	} else if (cpuPickN == 1) {
		unsigned sa = 0;
		const unsigned char* da = cpuPick[0].fromGe
			? CEmuZipFsFind(fs, cpuPick[0].name, &sa)
			: (cpuPick[0].idx < fs->fileCount ? fs->files[cpuPick[0].idx].data : NULL);
		if (!cpuPick[0].fromGe && cpuPick[0].idx < fs->fileCount)
			sa = fs->files[cpuPick[0].idx].size;
		if (da && da != (const unsigned char*)1 && sa) {
			unsigned n = sa;
			if (0x100000u + n > audioCpuSize_) n = audioCpuSize_ - 0x100000u;
			memcpy(audioCpu_ + 0x100000, da, n);
		}
	} else {
		free(audioCpu_); audioCpu_ = NULL; audioCpuSize_ = 0;
		return 0;
	}

	/* Ensoniq: MAME V2 レイアウト — カタログオフセットで LOAD16_BYTE（偶数レーン）。名前で重複排除しない: 同一ファイルが 2 バンクへミラーされることが多い（cbombers/dariusg）。空 bank0 は妥当（pbobble3 は 0x400000 開始）。 */
	ensoniqSize_ = kEnsoniqMax;
	ensoniq_ = (uint8_t*)malloc(ensoniqSize_);
	if (!ensoniq_) {
		free(audioCpu_); audioCpu_ = NULL; audioCpuSize_ = 0;
		return 0;
	}
	memset(ensoniq_, 0, ensoniqSize_);
	int pcmPlaced = 0;
	if (ge->rom && ge->romCount > 0) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!r->type[0]) continue;
			if (_stricmp(r->type, "ensoniq") != 0 && _stricmp(r->type, "pcm") != 0
				&& _stricmp(r->type, "wave") != 0 && _stricmp(r->type, "otis") != 0)
				continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
			if (!data || data == (const unsigned char*)1 || !sz) continue;
			unsigned bankOff = (r->offset > 0) ? (unsigned)r->offset : 0u;
			if (bankOff >= ensoniqSize_) continue;
			PlaceEvenBytes(ensoniq_, ensoniqSize_, bankOff, data, sz);
			pcmPlaced++;
			if (getenv("CEMU_F3_DEBUG"))
				fprintf(stderr, "F3 pcmPlace %s off=%u size=%u\n", r->name, bankOff, sz);
		}
	}
	if (!pcmPlaced) {
		Cand pcmPick[8]; int pcmPickN = 0;
		for (int i = 0; i < pcmN && pcmPickN < 8; i++) {
			int dup = 0;
			for (int j = 0; j < pcmPickN; j++) {
				if (_stricmp(pcmPick[j].name, pcmC[i].name) == 0) { dup = 1; break; }
			}
			if (dup || pcmC[i].size < 0x80000) continue;
			pcmPick[pcmPickN++] = pcmC[i];
		}
		for (int i = 0; i < pcmPickN && i < 4; i++) {
			unsigned sz = 0;
			const unsigned char* data = pcmPick[i].fromGe
				? CEmuZipFsFind(fs, pcmPick[i].name, &sz)
				: (pcmPick[i].idx < fs->fileCount ? fs->files[pcmPick[i].idx].data : NULL);
			if (!pcmPick[i].fromGe && pcmPick[i].idx < fs->fileCount)
				sz = fs->files[pcmPick[i].idx].size;
			if (!data || data == (const unsigned char*)1 || !sz) continue;
			unsigned bankOff = (unsigned)i * 0x400000u;
			if (pcmPick[i].fromGe && ge->rom && pcmPick[i].idx >= 0 && pcmPick[i].idx < ge->romCount
				&& ge->rom[pcmPick[i].idx].offset > 0)
				bankOff = (unsigned)ge->rom[pcmPick[i].idx].offset;
			if (bankOff >= ensoniqSize_) break;
			PlaceEvenBytes(ensoniq_, ensoniqSize_, bankOff, data, sz);
			pcmPlaced++;
		}
		if (getenv("CEMU_F3_DEBUG")) {
			for (int i = 0; i < pcmPickN; i++)
				fprintf(stderr, "F3 pcmPick[%d] %s score=%d size=%u fromGe=%d\n",
					i, pcmPick[i].name, pcmPick[i].score, pcmPick[i].size, pcmPick[i].fromGe);
		}
	}
	if (!pcmPlaced) {
		/* フォールバック: 大きなファイルを連続 BE ワードとして */
		for (int i = 0; i < fs->fileCount; i++) {
			if (fs->files[i].size < 0x100000 || !fs->files[i].data) continue;
			unsigned n = fs->files[i].size;
			if (n > ensoniqSize_) n = ensoniqSize_;
			memcpy(ensoniq_, fs->files[i].data, n);
			break;
		}
	}
	/*
	 * カタログが唯一の 2 PCM ROM を 0x800000/0xC00000 に置くことがある（popnpop）。MAME は 0 / 0x400000 にロード。bank0 が空で両ファイルが高半面に着地したら再配置。
	 * 3 バンクで bank0 空（pbobble3）: 最初の埋まっているバンクを bank0 へミラーし、otisbank 書き前の早期キーオンが無音にならないようにする。
	 */
	{
		int bank0 = 0;
		for (unsigned i = 0; i < 0x1000u && i < ensoniqSize_; i++)
			if (ensoniq_[i]) { bank0 = 1; break; }
		if (!bank0 && pcmPlaced >= 2) {
			unsigned src[4]; int n = 0;
			for (unsigned b = 0; b < 4 && n < 4; b++) {
				const unsigned off = b * 0x400000u;
				int hit = 0;
				for (unsigned i = 0; i < 0x1000u && off + i < ensoniqSize_; i++)
					if (ensoniq_[off + i]) { hit = 1; break; }
				if (hit) src[n++] = off;
			}
			if (pcmPlaced == 2 && n == 2 && src[0] >= 0x800000u) {
				uint8_t* tmp = (uint8_t*)malloc(0x800000u);
				if (tmp) {
					memcpy(tmp, ensoniq_ + src[0], 0x400000u);
					memcpy(tmp + 0x400000u, ensoniq_ + src[1], 0x400000u);
					memset(ensoniq_, 0, ensoniqSize_);
					memcpy(ensoniq_, tmp, 0x800000u);
					free(tmp);
					if (getenv("CEMU_F3_DEBUG"))
						fprintf(stderr, "F3 pcmReloc 2-bank high→0/4M\n");
				}
			} else if (n >= 1 && src[0] == 0x400000u) {
				memcpy(ensoniq_, ensoniq_ + 0x400000u, 0x400000u);
				if (getenv("CEMU_F3_DEBUG"))
					fprintf(stderr, "F3 pcmMirror bank1→bank0\n");
			}
		}
	}

	bankMask_ = (ensoniqSize_ / 0x200000u) ? ((ensoniqSize_ / 0x200000u) - 1u) : 0u;
	RebuildOtisBanks();
	chip_->SetPcmRom(ensoniq_, ensoniqSize_);
	chip_->Reset();

	if (audioCpuSize_ > 0x100000u)
		cpuBankMax_ = (audioCpuSize_ - 0x100000u) / 0x20000u;
	else
		cpuBankMax_ = 1;
	if (cpuBankMax_ < 1) cpuBankMax_ = 1;
	for (int i = 0; i < 3; i++)
		cpuBankEntry_[i] = (unsigned)i % cpuBankMax_;

	/* taito_en device_reset: ROM ワード 0x80000（= バイト 0x100000）からリセットベクタをコピー */
	if (audioCpuSize_ >= 0x100008u) {
		memcpy(osram_, audioCpu_ + 0x100000, 8);
	}

	/* IPL=7 にロックする STOP/MOVE-to-SR をパッチ — メイン CPU 無しでは DUART IRQ6 が要る。TRAP #9 の BCLR D0,2(A1) は触らない: TCB+2 bit7（TRAP #6 が立てた待ちフラグ）をクリアする。+3 へ向けると、走っているメールボックス（flags 0080）が次の IRQ パケットでスリープする。 */
	if (audioCpu_ && audioCpuSize_ >= 8) {
		for (unsigned i = 0; i + 7 < audioCpuSize_; i += 2) {
			if (audioCpu_[i] == 0x4e && audioCpu_[i + 1] == 0x72
				&& audioCpu_[i + 2] == 0x27 && audioCpu_[i + 3] == 0x00) {
				audioCpu_[i + 2] = 0x20; /* 命令 STOP #$2000 */
			}
			if (audioCpu_[i] == 0x4e && audioCpu_[i + 1] == 0x7c
				&& audioCpu_[i + 2] == 0x27 && audioCpu_[i + 3] == 0x00) {
				audioCpu_[i + 2] = 0x20; /* 命令 MOVE #$2000,SR */
			}
		}
	}

	/* C10FEE はブートスピン待ちであり 60Hz tick ではない。Open 後 CPU はスケジューラ STOP にパーク。C14884 は type-$E メールボックスハンドラ（D098+$0E）。アイドル STOP から jsr C1490A しない — フリーリストが空になる。パーサ `move.w #0; A-line` を NOP。opcode-0 A-line は MOVE SR になる。ホストは #$2700 のあと IPL を落とす。 */
	if (audioCpu_ && audioCpuSize_ > 0x10000Cu) {
		static const uint8_t kDelayTail[8] = {
			0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x71, 0x53, 0x83
		};
		static const uint8_t kPushD0f4[4] = { 0x3f, 0x38, 0xd0, 0xf4 };
		static const uint8_t kAlineUser[6] = { 0x30, 0x3c, 0x00, 0x00, 0xa0, 0x00 };
		const unsigned win0 = 0x100000u;
		const unsigned win1 = (audioCpuSize_ < 0x120000u) ? audioCpuSize_ : 0x120000u;
		unsigned delayOff = 0, tickOff = 0, playOff = 0, holeOff = 0;
		int gunOs = 0;
		if (audioCpuSize_ > 0x10002Cu) {
			const unsigned v28 = ((unsigned)audioCpu_[0x100028] << 24)
				| ((unsigned)audioCpu_[0x100029] << 16)
				| ((unsigned)audioCpu_[0x10002A] << 8)
				| (unsigned)audioCpu_[0x10002B];
			if (v28 == 0x00C10D8Cu || v28 == 0xC10D8Cu)
				gunOs = 1;
		}
		if (win1 > win0 + 16u) {
			for (unsigned i = win0; i + 10u <= win1; i += 2) {
				/* 呼び出し側は死んだ move.l ではなく 3-nop 本体（C10FEE）へ bsr */
				if (!delayOff && memcmp(audioCpu_ + i, kDelayTail, 8) == 0)
					delayOff = i;
				if (!tickOff && i >= win0 + 4u && i + 4u <= win1
					&& memcmp(audioCpu_ + i, kPushD0f4, 4) == 0
					&& audioCpu_[i - 4] == 0x61 && audioCpu_[i - 3] == 0x00)
					tickOff = i - 4u;
			}
			if (tickOff) {
				for (unsigned i = win0; i + 4u <= win1; i += 2) {
					if (audioCpu_[i] != 0x61 || audioCpu_[i + 1] != 0x00)
						continue;
					const int disp = (int16_t)(((unsigned)audioCpu_[i + 2] << 8)
						| (unsigned)audioCpu_[i + 3]);
					if ((unsigned)(i + 2 + disp) == tickOff) {
						playOff = i;
						break;
					}
				}
			}
			const unsigned nop0 = win0 + 0x13600u;
			const unsigned nop1 = win0 + 0x15200u;
			static const uint8_t kLoadD0e8Ble[8] = {
				0x2a, 0x38, 0xd0, 0xe8, 0x6f, 0x00, 0x00, 0x74
			};
			static const uint8_t kLoadD0e8[4] = { 0x2a, 0x38, 0xd0, 0xe8 };
			if (!gunOs) {
				for (unsigned i = nop0; i + 8u <= nop1 && i + 8u <= win1; i += 2) {
					if (memcmp(audioCpu_ + i, kLoadD0e8Ble, 8) == 0) {
						memset(audioCpu_ + i, 0x4e, 8);
						audioCpu_[i + 1] = audioCpu_[i + 3] = audioCpu_[i + 5] = audioCpu_[i + 7] = 0x71;
					}
				}
				for (unsigned i = nop0; i + 4u <= nop1 && i + 4u <= win1; i += 2) {
					if (memcmp(audioCpu_ + i, kLoadD0e8, 4) == 0) {
						audioCpu_[i] = 0x4e; audioCpu_[i + 1] = 0x71;
						audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
					}
				}
			}
			for (unsigned i = nop0; i + 6u <= nop1 && i + 6u <= win1; i += 2) {
				if (memcmp(audioCpu_ + i, kAlineUser, 6) == 0) {
					audioCpu_[i] = 0x4e; audioCpu_[i + 1] = 0x71;
					audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
					audioCpu_[i + 4] = 0x4e; audioCpu_[i + 5] = 0x71;
				}
			}
			/* メールボックス type $E はカタログリロードで IPL7 を上げ、`move.w #0; A-line` でユーザ IPL0 へ落としてから jsr C14884。A-line RTE が USP を壊した。MOVE SR ならメールボックスタスクを離れずに落とせる。ここで NOP すると IPL7 が C146AE まで残り、正のワードを飛ばすループが yield しない。 */
			const unsigned mb0 = win0 + 0x13200u;
			const unsigned mb1 = win0 + 0x13600u;
			for (unsigned i = mb0; i + 6u <= mb1 && i + 6u <= win1; i += 2) {
				if (memcmp(audioCpu_ + i, kAlineUser, 6) == 0) {
					audioCpu_[i] = 0x4e; audioCpu_[i + 1] = 0x71;
					audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
					audioCpu_[i + 4] = 0x4e; audioCpu_[i + 5] = 0x71;
				}
			}
			static const uint8_t kAlineIpl7[6] = { 0x30, 0x3c, 0x27, 0x00, 0xa0, 0x00 };
			for (unsigned i = mb0; i + 6u <= mb1 && i + 6u <= win1; i += 2) {
				if (memcmp(audioCpu_ + i, kAlineIpl7, 6) == 0) {
					/* メールボックスタスク SR（ユーザ IPL0）に留まる。MOVE SR #$2700 は C146AE を割り込み不可にし、最初のストリームワードが消費されなかった。 */
					audioCpu_[i] = 0x4e; audioCpu_[i + 1] = 0x71;
					audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
					audioCpu_[i + 4] = 0x4e; audioCpu_[i + 5] = 0x71;
				}
			}
			/* Type $E: C12A14（カタログリロード）を飛ばす。Packet+6 を曲 ID にすると D0F4/D09A が書き換わり arabianm 0x21 は d0f4=0 で終わる。bsr.w / movea.w (sp)+,a5 に合わせる — +6 の A-line は上ループで既に MOVE SR。C12B8C の A-line は OS コール。書き換えない。 */
			for (unsigned i = mb0; i + 6u <= mb1 && i + 6u <= win1; i += 2) {
				if (audioCpu_[i] == 0x61 && audioCpu_[i + 1] == 0x00
					&& audioCpu_[i + 4] == 0x3a && audioCpu_[i + 5] == 0x5f) {
					audioCpu_[i] = 0x4e; audioCpu_[i + 1] = 0x71;
					audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
					break;
				}
			}
			/* Type $E: C13306(D098+$E) は C1100B（奇数）を返す。jsr (a0) は C146AE へ落ちて戻らない。C1490A は実証済みシーケンサ（遅延トランポリン）。bsr C13306 / jsr (a0) を置換 — 6 バイト。gunlock OS は playOff が RAM を 2600 で潰すので触らない。 */
			if (playOff && !gunOs) {
				const unsigned playCpu = 0xC00000u + (playOff - win0);
				for (unsigned i = mb0; i + 6u <= mb1 && i + 6u <= win1; i += 2) {
					if (audioCpu_[i] == 0x61 && audioCpu_[i + 1] == 0x00
						&& audioCpu_[i + 4] == 0x4e && audioCpu_[i + 5] == 0x90) {
						audioCpu_[i] = 0x4e;
						audioCpu_[i + 1] = 0xb9;
						audioCpu_[i + 2] = (uint8_t)(playCpu >> 24);
						audioCpu_[i + 3] = (uint8_t)(playCpu >> 16);
						audioCpu_[i + 4] = (uint8_t)(playCpu >> 8);
						audioCpu_[i + 5] = (uint8_t)playCpu;
						break;
					}
				}
			}
			/* C14884→C149E4 はメールボックス 4(a5) を D4A6 へ。tickOff 直前をファーム共通で探す。gunlock は D4A6 を立てると tickOff が 2600 smash。 */
			if (tickOff >= win0 + 0x80u && !gunOs) {
				const unsigned q0 = tickOff - 0x80u;
				const unsigned q1 = tickOff;
				for (unsigned i = q0; i + 8u <= q1 && i + 8u <= win1; i += 2) {
					if (audioCpu_[i] == 0x30 && audioCpu_[i + 1] == 0x2d
						&& audioCpu_[i + 2] == 0x00 && audioCpu_[i + 3] == 0x04
						&& audioCpu_[i + 4] == 0x31 && audioCpu_[i + 5] == 0xc0
						&& audioCpu_[i + 6] == 0xd4 && audioCpu_[i + 7] == 0xa6) {
						audioCpu_[i] = 0x30; audioCpu_[i + 1] = 0x3c;
						audioCpu_[i + 2] = 0x00; audioCpu_[i + 3] = 0x01;
						break;
					}
				}
			}
			/* Type 1 で 4(a5)<0 は曲 0..$62 に bsr C12B8C をループ（全停止）して RTS。その経路は arabianm 0x21 の D0F4 を殺す。bsr だけ NOP。再生経路へ bra しない（C12E08 6A→60 はブートを壊した）。gunlock OS はアドレスがずれるので触らない。 */
			if (!gunOs) {
				const unsigned p0 = win0 + 0x12E00u;
				const unsigned p1 = win0 + 0x12E20u;
				for (unsigned i = p0; i + 8u <= p1 && i + 8u <= win1; i += 2) {
					if (audioCpu_[i] == 0x70 && audioCpu_[i + 1] == 0x00
						&& audioCpu_[i + 2] == 0x61 && audioCpu_[i + 3] == 0x00) {
						audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
						audioCpu_[i + 4] = 0x4e; audioCpu_[i + 5] = 0x71;
						break;
					}
				}
			}
			/* opcode-0（trap #3 次いで A-line）のみ。他の 2700 A-line は OS コール — 全部置換するとブートが壊れた。 */
			static const uint8_t kOp0Aline[8] = {
				0x4e, 0x43, 0x30, 0x3c, 0x27, 0x00, 0xa0, 0x00
			};
			for (unsigned i = nop0; i + 8u <= nop1 && i + 8u <= win1; i += 2) {
				if (memcmp(audioCpu_ + i, kOp0Aline, 8) == 0) {
					audioCpu_[i + 2] = 0x46; audioCpu_[i + 3] = 0xfc;
					audioCpu_[i + 4] = 0x27; audioCpu_[i + 5] = 0x00;
					audioCpu_[i + 6] = 0x4e; audioCpu_[i + 7] = 0x71;
				}
			}
			if (gunOs) {
				/* C152B0: cmpi.w #stamp,$d09a / bne skip。変位は 6630 とは限らない。 */
				for (unsigned i = win0 + 0x13600u; i + 8u <= win0 + 0x15400u && i + 8u <= win1; i += 2) {
					if (audioCpu_[i] == 0x0c && audioCpu_[i + 1] == 0x78
						&& audioCpu_[i + 4] == 0xd0 && audioCpu_[i + 5] == 0x9a
						&& audioCpu_[i + 6] == 0x66) {
						audioCpu_[i + 6] = 0x4e;
						audioCpu_[i + 7] = 0x71;
					}
				}
			}
			if (tickOff + 0x80u <= win1) {
				for (unsigned i = tickOff + 0x60u; i + 8u <= tickOff + 0x80u; i += 2) {
					if (audioCpu_[i] == 0x64 && audioCpu_[i + 1] == 0x06) {
						for (unsigned j = i + 2; j + 2 <= i + 10 && j + 1 < win1; j += 2) {
							if (audioCpu_[j] == 0x66 && audioCpu_[j + 1] == 0x08) {
								audioCpu_[j] = 0x6e;
								break;
							}
						}
						break;
					}
				}
			}
			const unsigned hole0 = win0 + 0x1C000u;
			for (unsigned i = (hole0 < win1 ? hole0 : win0); i + 40u <= win1; i += 2) {
				int ok = 1;
				for (int k = 0; k < 40; k++) {
					if (audioCpu_[i + k] != 0xff) { ok = 0; break; }
				}
				if (ok) { holeOff = i; break; }
			}
		}
		if (delayOff && tickOff && holeOff) {
			const unsigned holeCpu = 0xC00000u + (holeOff - win0);
			const unsigned backCpu = 0xC00000u + (delayOff - win0) + 6u; /* subq 命令 */
			uint8_t tr[36];
			memset(tr, 0x4e, sizeof(tr));
			tr[0] = 0x4a; tr[1] = 0x78; tr[2] = 0xd4; tr[3] = 0xa6;
			tr[4] = 0x67; tr[5] = 0x12;
			tr[6] = 0x2f; tr[7] = 0x0e;
			if (gunOs) {
				const unsigned callOff = playOff ? playOff : tickOff;
				const unsigned callCpu = 0xC00000u + (callOff - win0);
				tr[8] = 0x3c; tr[9] = 0x78; tr[10] = 0xd0; tr[11] = 0xf4;
				tr[12] = 0x4e; tr[13] = 0xb9;
				tr[14] = (uint8_t)(callCpu >> 24); tr[15] = (uint8_t)(callCpu >> 16);
				tr[16] = (uint8_t)(callCpu >> 8); tr[17] = (uint8_t)callCpu;
				tr[18] = 0x2c; tr[19] = 0x5f;
				tr[20] = 0x42; tr[21] = 0x78; tr[22] = 0xd4; tr[23] = 0xa6;
			} else {
				const unsigned callOff = playOff ? playOff : tickOff;
				const unsigned callCpu = 0xC00000u + (callOff - win0);
				tr[8] = 0x3c; tr[9] = 0x78; tr[10] = 0xd0; tr[11] = 0xf4;
				tr[12] = 0x4e; tr[13] = 0xb9;
				tr[14] = (uint8_t)(callCpu >> 24); tr[15] = (uint8_t)(callCpu >> 16);
				tr[16] = (uint8_t)(callCpu >> 8); tr[17] = (uint8_t)callCpu;
				tr[18] = 0x2c; tr[19] = 0x5f;
				tr[20] = 0x42; tr[21] = 0x78; tr[22] = 0xd4; tr[23] = 0xa6;
			}
			tr[24] = 0x4e; tr[25] = 0xf9;
			tr[26] = (uint8_t)(backCpu >> 24); tr[27] = (uint8_t)(backCpu >> 16);
			tr[28] = (uint8_t)(backCpu >> 8); tr[29] = (uint8_t)backCpu;
			memcpy(audioCpu_ + holeOff, tr, 30);
			audioCpu_[delayOff + 0] = 0x4e;
			audioCpu_[delayOff + 1] = 0xf9;
			audioCpu_[delayOff + 2] = (uint8_t)(holeCpu >> 24);
			audioCpu_[delayOff + 3] = (uint8_t)(holeCpu >> 16);
			audioCpu_[delayOff + 4] = (uint8_t)(holeCpu >> 8);
			audioCpu_[delayOff + 5] = (uint8_t)holeCpu;
			/* C10CE0: movea.w #0,a7 / jsr C17A80 は致命リセットで STOP テンプレートを全 OTIS ボイスへコピー。ブートは C1090A/C109CC を使う。この遅い jsr だけ nop。gunlock OS はアドレスがずれる。 */
			if (!gunOs) {
				for (unsigned i = win0 + 0x10C00u; i + 6u <= win0 + 0x10D80u && i + 6u <= win1; i += 2) {
					if (audioCpu_[i] == 0x4e && audioCpu_[i + 1] == 0xb9
						&& audioCpu_[i + 2] == 0x00 && audioCpu_[i + 3] == 0xc1
						&& audioCpu_[i + 4] == 0x7a && audioCpu_[i + 5] == 0x80) {
						audioCpu_[i] = 0x4e; audioCpu_[i + 1] = 0x71;
						audioCpu_[i + 2] = 0x4e; audioCpu_[i + 3] = 0x71;
						audioCpu_[i + 4] = 0x4e; audioCpu_[i + 5] = 0x71;
						break;
					}
				}
			}
		}
	}

	CEmuHardF3SetActive(this);
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	/* m68k_init() は int_ack をクリアする — F3 DUART IVR コールバックを再インストール */
	m68k_set_int_ack_callback(CEmuHardF3IntAck);
	m68k_pulse_reset();
	{
		const unsigned sp = (((unsigned)osram_[0] << 24) | ((unsigned)osram_[1] << 16)
			| ((unsigned)osram_[2] << 8) | (unsigned)osram_[3]) & 0xffffffu;
		const unsigned pc = (((unsigned)osram_[4] << 24) | ((unsigned)osram_[5] << 16)
			| ((unsigned)osram_[6] << 8) | (unsigned)osram_[7]) & 0xffffffu;
		m68k_set_reg(M68K_REG_SP, sp ? sp : 0x1000);
		m68k_set_reg(M68K_REG_PC, pc);
	}
	musashiReady_ = 1;
	songCode_ = 0;
	return 1;
}
