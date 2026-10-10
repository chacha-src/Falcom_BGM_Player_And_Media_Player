#include "StdAfx.h"
#include "cemu_driver_f3.h"
#include "../chip/cemu_chip_es5505.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* Taito F3: 68000＋ES5505 音源基板 */
CDriverF3::CDriverF3()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(15238100)
	, cpuAcc_(0)
	, booted_(0)
	, songCode_(1)
	, tryCount_(0)
	, cmdIndex_(0)
	, dwellLeft_(0)
	, dwellFrames_(22050)
	, bestPeak_(0)
	, windowPeak_(0)
	, bestSongCode_(1)
	, locked_(0)
	, irqPhase_(0)
	, kickedMail_(0)
	, hitIdle_(0)
	, hitIrq_(0)
	, hitTask0_(0)
	, hitMail_(0)
	, hitPlay_(0)
	, hitDisp_(0)
	, hitTick_(0)
	, seqTickAcc_(0)
	, seqFastAcc_(0)
	, seqCalls_(0)
	, irq6Vec_(0)
	, delayGated_(0)
	, expiredHead_(0)
	, lastHeadWait_(0)
	, waitDecs_(0)
	, typeEPosts_(0)
	, f3Arabianm_(0)
	, idlePark_(0xC10A9Au)
	, mbDisp_(0xC131E6u)
	, strm0_(0)
	, demoRestart_(0)
	, restartEvery_(0)
	, chainSnapN_(0)
	, chainLoopEvery_(0)
	, tblOffs_(0)
	, chainPark_(0)
	, walkEntry_(0)
	, walking_(0)
{
	memset(romVec_, 0, sizeof(romVec_));
	memset(tryCodes_, 0, sizeof(tryCodes_));
	memset(chainSnap_, 0, sizeof(chainSnap_));
	songPtr_ = 0;
	gunN_ = 0;
	memset(gunNode_, 0, sizeof(gunNode_));
	memset(gunNext_, 0, sizeof(gunNext_));
	memset(gunFlg_, 0, sizeof(gunFlg_));
	memset(gunStrm_, 0, sizeof(gunStrm_));
}

/* 後始末 */
CDriverF3::~CDriverF3()
{
	Close();
}

/* 試行テーブルへ曲コードを追加（重複なし） */
static void CDriverF3Push(unsigned* dst, int* n, int cap, unsigned code)
{
	if (!dst || !n || *n >= cap || code == 0) return;
	for (int i = 0; i < *n; i++) {
		if (dst[i] == code) return;
	}
	dst[(*n)++] = code;
}

static unsigned CDriverF3OptU32(const CEmuGameEntry* ge, const char* name)
{
	if (!ge || !ge->opt || !name) return 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) == 0)
			return (unsigned)strtoul(ge->opt[i].value, NULL, 0);
	}
	return 0;
}

/* ROM 読込、DUART settle、キーオンゲート武装 */
int CDriverF3::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs || hw->hardKind != CHard::KIND_F3) return 0;
	hw_ = (CHardF3*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 15238100;
	cpuAcc_ = 0;
	booted_ = 0;
	locked_ = 0;
	bestPeak_ = 0;
	windowPeak_ = 0;
	tryCount_ = 0;
	cmdIndex_ = 0;
	irqPhase_ = 0;
	kickedMail_ = 0;
	hitIdle_ = 0;
	hitIrq_ = 0;
	hitTask0_ = 0;
	hitMail_ = 0;
	hitPlay_ = 0;
	hitDisp_ = 0;
	hitTick_ = 0;
	seqTickAcc_ = 0;
	seqFastAcc_ = 0;
	seqCalls_ = 0;
	irq6Vec_ = 0;
	delayGated_ = 0;
	expiredHead_ = 0;
	lastHeadWait_ = 0;
	waitDecs_ = 0;
	typeEPosts_ = 0;
	f3Arabianm_ = 0;
	idlePark_ = 0xC10A9Au;
	mbDisp_ = 0xC131E6u;
	strm0_ = 0;
	demoRestart_ = 0;
	restartEvery_ = 0;
	chainSnapN_ = 0;
	chainLoopEvery_ = 0;
	tblOffs_ = 0;
	chainPark_ = 0;
	walkEntry_ = 0;
	walking_ = 0;
	memset(romVec_, 0, sizeof(romVec_));
	songPtr_ = 0;
	gunN_ = 0;
	memset(chainSnap_, 0, sizeof(chainSnap_));

	songCode_ = titleCode ? titleCode : 1;
	CDriverF3Push(tryCodes_, &tryCount_, (int)_countof(tryCodes_), songCode_);
	if (tryCount_ < 1) {
		tryCodes_[0] = 1;
		tryCount_ = 1;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;
	tblOffs_ = CDriverF3OptU32(ge, "tbloffs");

	CEmuHardF3SetActive(hw_);
	/* ブート settle: DUART/IVR、TCB コピー、最初のタスクスライス */
	RunCycles(cpuHz_);
	booted_ = 1;
	/* 遅延中も IPL マスクなら一度落とし、DUART が走れるようにする（メイン CPU 無し） */
	{
		const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
		if (((sr >> 8) & 7) >= 6)
			m68k_set_reg(M68K_REG_SR, (sr & ~0x0700u) | 0x2000u);
	}
	RunCycles(cpuHz_ / 5);
	f3Arabianm_ = (hw_->Read32(0x28u) == 0xC10D12u) ? 1 : 0;
	if (!f3Arabianm_ && hw_->SoundChip())
		CEmuChipEs5505SetSlowLpe(hw_->SoundChip(), 1);
	/* 曲ロードが IRQ ベクタを踏む前の値。壊れたベクタへ飛ぶと以降の実行がチェインを消す。 */
	if (!f3Arabianm_) {
		for (int i = 0; i < 96; i++)
			romVec_[i] = hw_->Read32((unsigned)i * 4u);
	}
	songCode_ = tryCodes_[0];
	cmdIndex_ = tryCount_;
	locked_ = 1;
	songCode_ = MapSongCode(songCode_);
	if (!loadOnly)
		hw_->SetSongCommand(songCode_);
	/* C15702（C15538 内のボイスチェイン）は D4C0 が立つまで即 return。実機は task0 がフラグを ST。
	   D4F9 は C15702 のキーオン許可で、カタログの bset #4 前に見る。 */
	hw_->Write8(0xD4F9u, 1);
	hw_->Write8(0xD4C0u, 1);
	/* $6DFC は植えない。C12B8C はリストにあれば停止（compact + C12AD0 が 5DAA+4 を壊す）。C12E70 は常に C12C36 へ BRA して開始。 */
	KickMailboxOnce();
	RunCycles(cpuHz_ / 2);
	RescueGunlockCpu();
	/* C15538 が D4B3 をクリアし、D0F4 チェイン前に C13B94 が走るので C152B0 はキーオンゲートを見ない。チェイン生存後に武装。 */
	ArmKeyOnGates();
	irq6Vec_ = hw_->Read32(0x100u);
	{
		const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
		if (pc >= 0xC10A80u && pc < 0xC10B80u)
			idlePark_ = pc;
		else
			idlePark_ = f3Arabianm_ ? 0xC10A9Au : 0xC10B14u;
		const unsigned disp = hw_->Read32(0xFB3Eu + 4u);
		if (disp >= 0xC13100u && disp < 0xC13400u)
			mbDisp_ = disp;
		else
			mbDisp_ = f3Arabianm_ ? 0xC131E6u : 0xC13296u;
	}
	{
		/* C14A10 は A7 上の D0F4 を歩く。リセット SSP は $FFFFFFF8（8 バイト、IRQ のみ）。 */
		const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
		if (ssp < 0x400u || ssp >= 0xFFFF00u || ssp == 0x4C00u)
			m68k_set_reg(M68K_REG_ISP, 0x9E00);
	}
	dwellFrames_ = hostRate_ > 0 ? hostRate_ / 2 : 22050;
	if (dwellFrames_ < 1) dwellFrames_ = 1;
	dwellLeft_ = dwellFrames_;
	/* 他の曲コードを探さない（SAMESONG）。再エンキューもしない。 */
	locked_ = 1;
	bestSongCode_ = songCode_;
	CaptureGunlockChain();
	LogState("open");
	return 1;
}

/* ハード参照を捨てる */
void CDriverF3::Close()
{
	if (hw_)
		LogState("close");
	hw_ = NULL;
	booted_ = 0;
}

/* 同一 zip の別曲をメールボックスへ */
int CDriverF3::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	songCode_ = titleCode;
	locked_ = 1;
	if (delayGated_)
		hw_->EnableDelaySeqTick();
	expiredHead_ = 0;
	delayGated_ = 0;
	lastHeadWait_ = 0;
	waitDecs_ = 0;
	typeEPosts_ = 0;
	seqCalls_ = 0;
	strm0_ = 0;
	demoRestart_ = 0;
	restartEvery_ = 0;
	chainSnapN_ = 0;
	chainLoopEvery_ = 0;
	chainPark_ = 0;
	memset(chainSnap_, 0, sizeof(chainSnap_));
	songCode_ = MapSongCode(titleCode);
	hw_->SetSongCommand(songCode_);
	return 1;
}

/* C152B0 が見るキーオンゲートを武装 */
void CDriverF3::ArmKeyOnGates()
{
	if (!hw_) return;
	/* C152B0: cmpi.w #stamp, $d09a / bne skip。6630 キーオンゲートを優先。 */
	unsigned stamp = 0;
	for (unsigned a = 0xC13600u; a + 8u < 0xC15400u; a += 2u) {
		if (hw_->Read16(a) != 0x0C78u || hw_->Read16(a + 4u) != 0xD09Au)
			continue;
		const unsigned imm = hw_->Read16(a + 2u);
		const unsigned br = hw_->Read16(a + 6u);
		if (imm < 0x3000u || imm > 0x36FFu)
			continue;
		stamp = imm;
		if (br == 0x6630u)
			break;
	}
	if (stamp) {
		if (hw_->Read16(0xD09Au) != stamp)
			hw_->Write16(0xD09Au, (uint16_t)stamp);
		if (hw_->Read16(0xD09Eu) != stamp)
			hw_->Write16(0xD09Eu, (uint16_t)stamp);
	}
	if (hw_->Read8(0xD4F9u) == 0)
		hw_->Write8(0xD4F9u, 1);
	/* C15538 後 D4B3 は 0 なので C152B0 の start mode 分岐は死ぬ */
	if (hw_->Read8(0xD4B3u) == 0)
		hw_->Write8(0xD4B3u, 3);
	unsigned n = hw_->Read16(0xD0F4u);
	int hops = 0;
	while (n >= 0xD000u && n < 0xEE00u && hops < 16) {
		const uint8_t f = hw_->Read8(n + 2u);
		if ((f & 0x18u) != 0x18u)
			hw_->Write8(n + 2u, (uint8_t)(f | 0x18u));
		n = hw_->Read16(n);
		hops++;
	}
}

/* メールボックスを 1 回起こす */
void CDriverF3::KickMailboxOnce()
{
	if (!hw_ || kickedMail_) return;
	kickedMail_ = 1;
	unsigned wp = hw_->RingWp();
	unsigned pkt = (wp >= 6u) ? (wp - 6u) : ((wp + 0x800u - 6u) & 0x7feu);
	pkt &= 0x7feu;
	/* $EE00 は OS フリーリスト $EE8A の直前。$EE88（TRAP #3 / リスト頭）と $D200（OS 変数）は使わない。 */
	hw_->Write16(0xEE00u, 0);
	hw_->Write16(0xEE02u, 1);
	hw_->Write16(0xEE04u, pkt);
	hw_->Write16(0xEE06u, 0);
	const unsigned tcb = 0xFB3Eu;
	hw_->Write16(tcb + 0x0Au, 0xEE00u);
	hw_->Write16(tcb + 0x0Eu, 0xEE00u);
	hw_->Write16(tcb + 0x10u, 0xEE00u);
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	if (b2 == b3)
		hw_->Write8(tcb + 2u, (uint8_t)(b2 ^ 0x80u));
}

/* キュー済みならメールボックスを起こす */
void CDriverF3::WakeMailboxIfQueued()
{
	/* TRAP #6 待ちは TCB+3 bit7。TRAP #9 は TCB+2 bit7 を BSET してメッセージを届ける。
 * 両ビットが合うとスケジューラ XOR が 0 になり、キュー済みパケットのままメールボックスが眠る。
 * +$10 が実キュー頭のときだけ +2 を反転する。 */
	if (!hw_) return;
	const unsigned tcb = 0xFB3Eu;
	if (hw_->Read16(tcb + 0x10u) == 0)
		return;
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	if (b2 == b3)
		hw_->Write8(tcb + 2u, (uint8_t)(b2 ^ 0x80u));
}

/* type-E を 136 から確保し、TCB+A 経由で A5 に載せて C131E6 へ起こす */
void CDriverF3::PostTypeE()
{
	if (!hw_) return;
	const unsigned tcb = 0xFB3Eu;
	const unsigned cpuPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
	const unsigned cur = hw_->Read16(0x134u);
	if (cur == tcb && cpuPc >= 0xC131C0u && cpuPc < (f3Arabianm_ ? 0xC14B00u : 0xC15480u))
		return;
	/* FFFF / 音源レジスタ空間へ落ちたメールボックスはアイドル STOP へ戻す */
	if (cpuPc < 0xC00000u || cpuPc >= 0xC18000u || (cpuPc >= 0x200000u && cpuPc < 0x400000u)) {
		m68k_set_reg(M68K_REG_PC, idlePark_ ? idlePark_ : 0xC10A9Au);
		m68k_set_reg(M68K_REG_SR, 0x2000);
		hw_->Write8(tcb + 2u, 0x80);
		hw_->Write8(tcb + 3u, 0x80);
		hw_->Write32(tcb + 4u, mbDisp_ ? mbDisp_ : 0xC131E6u);
		return;
	}
	if ((seqCalls_ & 3u) != 0u)
		return;
	if (f3Arabianm_) {
		if (cpuPc < 0xC10A90u || cpuPc >= 0xC10AA0u)
			return;
	} else {
		const unsigned idle = idlePark_ ? idlePark_ : 0xC10B14u;
		if (cpuPc + 0x10u < idle || cpuPc >= idle + 0x10u)
			return;
	}
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	if (b2 != b3)
		return;
	if (b2 != 0x80u && b2 != 0x01u)
		return;
	const unsigned msg = hw_->Read16(0x0136u);
	if ((msg & 1u) || msg < 0x200u || msg >= 0xFF00u)
		return;
	if (msg >= 0xFB00u && msg < 0xFC00u)
		return;
	const unsigned nxt = hw_->Read16(msg);
	if ((nxt & 1u) && nxt != 0)
		return;
	if (nxt == 0 && hw_->Read16(0x0136u) == msg)
		return;
	hw_->Write16(0x0136u, nxt);
	hw_->Write16(msg, 0);
	hw_->Write16(msg + 2u, 0x000Eu);
	hw_->Write16(msg + 4u, 1);
	hw_->Write16(msg + 6u, 0);
	hw_->Write16(tcb + 0x0Au, (uint16_t)msg);
	hw_->Write32(tcb + 4u, mbDisp_ ? mbDisp_ : 0xC131E6u);
	hw_->Write8(tcb + 2u, (uint8_t)(b2 ^ 0x80u));
	typeEPosts_++;
}

/* 待ちループに戻ったメールボックスが flags=0000 のままなら sleep (8080) に戻す */
void CDriverF3::RestoreMailboxSleep()
{
	if (!hw_) return;
	const unsigned tcb = 0xFB3Eu;
	const unsigned saved = hw_->Read32(tcb + 4u);
	if (saved < 0xC131C0u || saved >= 0xC13320u)
		return;
	const unsigned cpuPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
	/* メールボックス／type ハンドラ実行中に起こすと USP が壊れる */
	if (cpuPc >= 0xC14884u && cpuPc < (f3Arabianm_ ? 0xC14B00u : 0xC15480u))
		return;
	if (cpuPc >= 0xC131C0u && cpuPc < 0xC13320u)
		return;
	if (cpuPc >= 0xC1E000u && cpuPc < 0xC20000u)
		return;
	if (hw_->Read16(0x134u) == 0xFB3Eu)
		return;
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	/* 0000=ディスパッチ中の残骸、0080=起こしたまま CPU が遅延へ逃げた */
	if ((b2 == 0 && b3 == 0) || (b2 == 0 && b3 == 0x80u) || (b2 == 0x80u && b3 == 0)) {
		hw_->Write8(tcb + 2u, 0x80);
		hw_->Write8(tcb + 3u, 0x80);
	}
}

/* 頭の巨大スタート待ちだけ、メールボックス C14A10 が生きてから 1 へ。以降の量子は type-E。 */
void CDriverF3::ExpireHeadWaitOnce()
{
	/* 待ちを 1 に潰すと曲が頭の 2 秒で終わる。減算は音源 CPU の D4A6 に任せる。 */
	return;
	if (!hw_ || expiredHead_) return;
	const unsigned cpuPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
	if (cpuPc >= 0xC10F00u && cpuPc < 0xC11100u)
		return;
	const unsigned head = hw_->Read16(0xD0F4u);
	if (head < 0xD000u || head >= 0xEE00u) return;
	const unsigned wait = hw_->Read16(head + 4u);
	if (lastHeadWait_ && wait < lastHeadWait_)
		waitDecs_++;
	lastHeadWait_ = wait;
	if (hitTick_ < 4u || wait < 0x0400u)
		return;
	if ((songCode_ & 0xffu) == 0x0Au)
		return;
	hw_->Write16(head + 4u, 1);
	expiredHead_ = 1;
}

/* 2s ごと。中休符は常に、巨大待ちは頭を落とした曲だけ。 */
void CDriverF3::PunchMediumWaits()
{
	/* 中休符を 1 にするとテンポが実機の数十倍になり、短い曲は STOPS になる。 */
	return;
	if (!hw_) return;
	unsigned n = hw_->Read16(0xD0F4u);
	int hops = 0;
	while (n >= 0xD000u && n < 0xEE00u && hops < 16) {
		const unsigned wait = hw_->Read16(n + 4u);
		if (wait >= 0x10u && (wait < 0x0400u || expiredHead_))
			hw_->Write16(n + 4u, 1);
		n = hw_->Read16(n);
		hops++;
	}
}

/* SetSongCommand 再注入が壊すフレーズを、チェイン RAM の頭へ戻して回す */
void CDriverF3::SnapChainOnce()
{
	if (!hw_ || chainSnapN_ > 0) return;
	unsigned n = hw_->Read16(0xD0F4u);
	if (n < 0xD000u || n >= 0xEE00u) return;
	int hops = 0;
	while (n >= 0xD000u && n < 0xEE00u && hops < 8
		&& chainSnapN_ + 6 <= (int)_countof(chainSnap_)) {
		chainSnap_[chainSnapN_++] = (uint16_t)n;
		chainSnap_[chainSnapN_++] = (uint16_t)hw_->Read16(n + 2u);
		chainSnap_[chainSnapN_++] = (uint16_t)hw_->Read16(n + 4u);
		chainSnap_[chainSnapN_++] = (uint16_t)hw_->Read16(n + 6u);
		chainSnap_[chainSnapN_++] = (uint16_t)hw_->Read16(n + 8u);
		chainSnap_[chainSnapN_++] = (uint16_t)hw_->Read16(n + 0xAu);
		n = hw_->Read16(n);
		hops++;
	}
}

void CDriverF3::RestoreChain()
{
	if (!hw_ || chainSnapN_ < 6) return;
	unsigned first = chainSnap_[0];
	for (int i = 0; i + 5 < chainSnapN_; i += 6) {
		const unsigned n = chainSnap_[i];
		if (n < 0xD000u || n >= 0xEE00u) continue;
		hw_->Write16(n + 2u, chainSnap_[i + 1]);
		{
			const unsigned wait = chainSnap_[i + 2];
			hw_->Write16(n + 4u, (wait && wait != 0xFFFFu) ? 1u : wait);
		}
		hw_->Write16(n + 6u, chainSnap_[i + 3]);
		hw_->Write16(n + 8u, chainSnap_[i + 4]);
		hw_->Write16(n + 0xAu, chainSnap_[i + 5]);
	}
	if (first >= 0xD000u && first < 0xEE00u)
		hw_->Write16(0xD0F4u, (uint16_t)first);
	hw_->Write16(0xD4A6u, 1);
}

/* 鳴っている声の音量指数が 0 のとき、ノート側の 0x78 まで戻す。音量 0 はそのまま。 */
void CDriverF3::LiftGunlockEnvelope()
{
	if (!hw_ || !hw_->SoundChip())
		return;
	CChip* chip = hw_->SoundChip();
	int live = 0;
	int last = -1;
	for (int v = 0; v < 32; v++) {
		chip->Write(0x0f, (uint32_t)v);
		const uint16_t st = CEmuChipEs5505Read(chip, 2);
		if (st < 0x0100u || st >= 0x8000u)
			continue;
		const uint16_t cr = CEmuChipEs5505PeekCr(chip, v);
		if (cr & 3u) {
			last = v;
			continue;
		}
		live++;
		/* 指数 0 だけ持ち上げる。CPU が既に付けた音量を E0 で上書きすると複数声で 32767 になる。 */
		if ((CEmuChipEs5505Read(chip, 8) & 0xF000u) == 0)
			chip->Write(0x08, 0xC0F0u);
		if ((CEmuChipEs5505Read(chip, 9) & 0xF000u) == 0)
			chip->Write(0x09, 0xC0F0u);
	}
	/* チェーンが生きていて声が全部止まった曲は、最後に置いた 1 声だけ戻す。全声を起こすと 32767 で割れる。 */
	const unsigned head = hw_->Read16(0xD0F4u);
	if (live == 0 && last >= 0 && head >= 0xD000u && head < 0xEE00u) {
		chip->Write(0x0f, (uint32_t)last);
		const uint16_t cr = CEmuChipEs5505PeekCr(chip, last);
		chip->Write(0x00, (uint32_t)((cr & 0x0ffcu) | 0x0018u));
		chip->Write(0x08, 0xC0F0u);
		chip->Write(0x09, 0xC0F0u);
	}
}

/* 音符の下位バイト + 0x15 を ES5505 の周波数へ書く。CPU の C1412E と同じピッチ。 */
void CDriverF3::ApplyGunlockPitch()
{
	if (!hw_ || f3Arabianm_ || tblOffs_ || seqCalls_ < 40u || !hw_->SoundChip())
		return;
	const unsigned tab = hw_->Read32(0xD404u);
	if (tab < 0xC00000u || tab >= 0xC80000u)
		return;
	static uint16_t semi[128];
	static int semiReady = 0;
	if (!semiReady) {
		for (int m = 0; m < 128; m++) {
			const double ratio = pow(2.0, (m - 60) / 12.0);
			int d = (int)(1024.0 * ratio + 0.5);
			if (d < 2) d = 2;
			if (d > 0x7ffe) d = 0x7ffe;
			semi[m] = (uint16_t)(d & 0xfffe);
		}
		semiReady = 1;
	}
	unsigned base = hw_->Read32(0xD414u);
	if (base >= tab && base < tab + 0x60000u)
		base -= tab;
	else if (base >= 0xC00000u)
		base = 0;
	if ((base < 0x40u || base >= 0x60000u) && songPtr_ >= 0x40u && songPtr_ < 0x60000u)
		base = songPtr_;
	if (base < 0x40u || base >= 0x60000u)
		return;
	unsigned songEnd = base + 0x4000u;
	const unsigned len = hw_->Read16(tab + base + 2u);
	if (len >= 0x40u && base + len < 0x60000u)
		songEnd = base + len;
	static unsigned curSong = 0xffffffffu;
	static unsigned curPos = 0;
	static unsigned curHold = 0;
	static unsigned curCall = 0;
	if (curSong != songCode_) {
		curSong = songCode_;
		curPos = base;
		curHold = 0;
		curCall = seqCalls_;
	}
	unsigned cmd = hw_->Read16(tab + (curPos & ~1u));
	int onNote = ((cmd & 0x8000u) && (cmd & 0xffu) < 0x58u) ? 1 : 0;
	if (seqCalls_ != curCall) {
		curCall = seqCalls_;
		unsigned beats = (cmd >> 8) & 0x7fu;
		if (beats < 2u) beats = 2u;
		if (beats > 16u) beats = 16u;
		int step = !onNote;
		if (onNote && ++curHold > beats)
			step = 1;
		if (step) {
			unsigned p = (curPos & ~1u) + 2u;
			for (int k = 0; k < 160; k++) {
				if (p < base || p + 1u >= songEnd)
					p = base;
				const unsigned w = hw_->Read16(tab + p);
				if ((w & 0x8000u) && (w & 0xffu) < 0x58u && p != (curPos & ~1u)) {
					curPos = p;
					cmd = w;
					onNote = 1;
					break;
				}
				p += 2u;
				if (p >= songEnd)
					p = base;
			}
			curHold = 0;
		}
	}
	if (!onNote)
		return;
	unsigned note[4];
	int nn = 1;
	note[0] = cmd & 0xffu;
	CChip* chip = hw_->SoundChip();
	int live[4];
	int nl = 0;
	for (int v = 0; v < 32 && nl < 4; v++) {
		chip->Write(0x0f, (uint32_t)v);
		const uint16_t st = CEmuChipEs5505Read(chip, 2);
		if (st < 0x0100u || st >= 0x8000u)
			continue;
		const uint16_t cr = CEmuChipEs5505PeekCr(chip, v);
		if (cr & 3u)
			continue;
		live[nl++] = v;
	}
	if (!nl) {
		for (int v = 0; v < 32; v++) {
			chip->Write(0x0f, (uint32_t)v);
			const uint16_t st = CEmuChipEs5505Read(chip, 2);
			if (st < 0x0100u || st >= 0x8000u)
				continue;
			const uint16_t cr = CEmuChipEs5505PeekCr(chip, v);
			chip->Write(0x00, (uint32_t)((cr & 0x0ffcu) | 0x0018u));
			chip->Write(0x08, 0xC0F0u);
			chip->Write(0x09, 0xC0F0u);
			live[nl++] = v;
			break;
		}
	}
	const int n = nl < nn ? nl : nn;
	for (int i = 0; i < n; i++) {
		int midi = (int)note[i] + 0x15;
		if (midi < 0) midi = 0;
		if (midi > 127) midi = 127;
		chip->Write(0x0f, (uint32_t)live[i]);
		chip->Write(0x01, semi[midi]);
	}
}

/* 起動直後のチェインを覚える。E9 がストリームを 0xFE**** へ飛ばす前の位置。 */
void CDriverF3::CaptureGunlockChain()
{
	songPtr_ = 0;
	gunN_ = 0;
	if (!hw_ || f3Arabianm_ || tblOffs_)
		return;
	const unsigned ptr = hw_->Read32(0xD414u);
	/* 後半バンクの曲は 0x20000 を超える。0x60000 は 0xC20000 窓の終わり。 */
	if (ptr >= 0x40u && ptr < 0x60000u)
		songPtr_ = ptr;
	unsigned hp = hw_->Read16(0xD0F4u);
	while (hp >= 0xD000u && hp < 0xEE00u && gunN_ < 12) {
		const unsigned nxt = hw_->Read16(hp);
		gunNode_[gunN_] = (uint16_t)hp;
		gunNext_[gunN_] = (uint16_t)nxt;
		gunFlg_[gunN_] = hw_->Read8(hp + 2u);
		gunStrm_[gunN_] = hw_->Read32(hp + 6u);
		gunN_++;
		if (nxt < 0xD000u || nxt >= 0xEE00u)
			break;
		hp = nxt;
	}
}

/* 0xFE**** へ飛んだストリームと、潰れた曲ポインタを起動時のチェインへ戻す。 */
void CDriverF3::HoldGunlockChain(int restore)
{
	if (!hw_ || f3Arabianm_ || tblOffs_ || !songPtr_ || gunN_ < 2)
		return;
	if (hw_->Read32(0xD414u) != songPtr_)
		hw_->Write32(0xD414u, songPtr_);
	if (hw_->Read32(0xD404u) != 0xC20000u)
		hw_->Write32(0xD404u, 0xC20000u);
	if ((hw_->Read16(0xD40Eu) & 0xffu) != (songCode_ & 0xffu))
		hw_->Write16(0xD40Eu, (uint16_t)(songCode_ & 0xffu));
	unsigned hp = hw_->Read16(0xD0F4u);
	int live = 0;
	while (hp >= 0xD000u && hp < 0xEE00u && live < 16) {
		live++;
		hp = hw_->Read16(hp);
	}
	if (restore && (live < 3 || hw_->Read16(0xD0F4u) != gunNode_[0])) {
		hw_->Write16(0xD0F4u, gunNode_[0]);
		for (int i = 0; i < gunN_; i++) {
			if (gunNext_[i] >= 0xD000u && gunNext_[i] < 0xEE00u)
				hw_->Write16(gunNode_[i], gunNext_[i]);
		}
	}
	for (int i = 0; i < gunN_; i++) {
		const unsigned node = gunNode_[i];
		if (node < 0xD000u || node >= 0xEE00u)
			continue;
		const unsigned s = hw_->Read32(node + 6u);
		const unsigned fl = hw_->Read8(node + 2u);
		/* 0xFE**** は E9 の桁あふれ。バンク内でフラグが残っている間だけ進んだ位置を覚える。 */
		const int inSong = (s >= songPtr_ && s < 0x60000u);
		if (inSong && (fl & 0x18u))
			gunStrm_[i] = s;
		else if (restore && gunStrm_[i] >= songPtr_ && gunStrm_[i] < 0x60000u)
			hw_->Write32(node + 6u, gunStrm_[i]);
		if (restore && (fl & 0x18u) == 0 && (gunFlg_[i] & 0x18u))
			hw_->Write8(node + 2u, gunFlg_[i]);
	}
}

/* 曲ロードが IRQ ベクタを ROM 外へ書いたら戻し、飛んだ PC をアイドルへ戻す。 */
void CDriverF3::RescueGunlockCpu()
{
	if (!hw_ || f3Arabianm_ || tblOffs_)
		return;
	for (int i = 0; i < 96; i++) {
		const unsigned saved = romVec_[i];
		if (saved < 0xC00000u || saved >= 0xC80000u)
			continue;
		const unsigned cur = hw_->Read32((unsigned)i * 4u);
		if (cur < 0xC00000u || cur >= 0xC80000u)
			hw_->Write32((unsigned)i * 4u, saved);
	}
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
	if (pc >= 0xC00000u && pc < 0xC80000u)
		return;
	m68k_set_reg(M68K_REG_PC, 0xC10B14u);
	m68k_set_reg(M68K_REG_SR, 0x2000);
	m68k_clear_stopped();
	const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
	if (ssp < 0x1000u || ssp >= 0xF000u)
		m68k_set_reg(M68K_REG_ISP, 0x9E00);
}

/* ガンロックは曲を載せたあとスケジューラ STOP で止まる。待ち減算へ 1 回戻す。 */
void CDriverF3::KickGunlockWalker()
{
	/* pbobble4（tbloffs 0xF3176）は既存の再生を崩さない。tbloffs 無しの基板だけ起こす。 */
	if (!hw_ || f3Arabianm_ || tblOffs_ || seqCalls_ < 40u)
		return;
	if (!walkEntry_) {
		for (unsigned a = 0xC14000u; a + 8u < 0xC16000u; a += 2u) {
			if (hw_->Read16(a) == 0x3F38u && hw_->Read16(a + 2u) == 0xD0F4u
				&& hw_->Read16(a - 4u) == 0x6100u) {
				walkEntry_ = a - 4u;
				break;
			}
		}
	}
	if (!walkEntry_)
		return;
	/* FFFF はテンポゲートが減算しない。1 にして次の呼び出しでループ解除を一度通す。 */
	if (hw_->Read16(0xD490u) == 0xFFFFu)
		hw_->Write16(0xD490u, 1);
	const unsigned head = hw_->Read16(0xD0F4u);
	const unsigned tab = hw_->Read32(0xD404u);
	if (head < 0xD000u || head >= 0xEE00u)
		return;
	if (tab < 0xC00000u || tab >= 0xC80000u)
		return;
	{
		/* 0x400 以上は曲頭のゲート。0x8F 級のフレーズ待ちは残す。 */
		static unsigned stuckSong = 0xffffffffu;
		static uint32_t stuckStrm[12];
		static int stuckN[12];
		if (stuckSong != songCode_) {
			stuckSong = songCode_;
			for (int i = 0; i < 12; i++) {
				stuckStrm[i] = 0xffffffffu;
				stuckN[i] = 0;
			}
		}
		unsigned node = head;
		int hops = 0;
		while (node >= 0xD000u && node < 0xEE00u && hops < 12) {
			const unsigned nxt = hw_->Read16(node);
			const unsigned fl = hw_->Read8(node + 2u);
			/* 頭以外の bit7 はウォーカーがそこで歩みを止める。bit5 は E9 の凍結。 */
			unsigned fl2 = fl;
			if (hops > 0)
				fl2 &= ~0x80u;
			fl2 &= ~0x20u;
			if (fl2 != fl)
				hw_->Write8(node + 2u, (uint8_t)fl2);
			unsigned strm = hw_->Read32(node + 6u);
			unsigned songOff = hw_->Read32(0xD414u);
			if (songOff >= tab && songOff < tab + 0x60000u)
				songOff -= tab;
			unsigned songEnd = songOff + 0x4000u;
			if (songOff >= 0x40u && songOff < 0x60000u) {
				const unsigned len = hw_->Read16(tab + songOff + 2u);
				if (len >= 0x40u && songOff + len < 0x60000u)
					songEnd = songOff + len;
			}
			if (songOff >= 0x40u && songEnd > songOff && tab >= 0xC00000u && tab < 0xC80000u) {
				if (strm < songOff || strm >= songEnd)
					strm = songOff;
				const unsigned cmd = hw_->Read16(tab + (strm & ~1u));
				const int onNote = ((cmd & 0x8000u) && (cmd & 0xffu) < 0x58u) ? 1 : 0;
				const int same = (hops < 12 && strm == stuckStrm[hops]) ? 1 : 0;
				unsigned beats = (cmd >> 8) & 0x7fu;
				if (beats < 2u) beats = 2u;
				if (beats > 16u) beats = 16u;
				int step = (!onNote) ? 1 : 0;
				if (onNote && same && hops < 12 && ++stuckN[hops] > (int)beats)
					step = 1;
				if (!same && hops < 12)
					stuckN[hops] = 0;
				if (step) {
					unsigned p = (strm & ~1u) + 2u;
					for (int k = 0; k < 160; k++) {
						if (p < songOff || p + 1u >= songEnd)
							p = songOff;
						const unsigned wnote = hw_->Read16(tab + p);
						if ((wnote & 0x8000u) && (wnote & 0xffu) < 0x58u && p != (strm & ~1u)) {
							strm = p;
							break;
						}
						p += 2u;
						if (p >= songEnd)
							p = songOff;
					}
					hw_->Write32(node + 6u, strm);
					if (hops < 12)
						stuckN[hops] = 0;
				}
			}
			if (hops < 12)
				stuckStrm[hops] = strm;
			const unsigned w = hw_->Read16(node + 4u);
			/* 待ち 0 から 1 を引くと桁が借りて bne がパーサを飛ばす。80xx は待ち 0。 */
			if (w == 0 || w >= 0xF000u)
				hw_->Write16(node + 4u, 1);
			node = nxt;
			hops++;
		}
	}
	unsigned song = hw_->Read32(0xD414u);
	if (song >= 0x40u && song < 0x40000u)
		song += tab;
	else if (song < 0xC00000u || song >= 0xC80000u)
		song = 0;
	if (song && hw_->Read32(head + 6u) < 0x40u) {
		unsigned offs[16];
		int nt = 0;
		unsigned p = song + 4u;
		for (int i = 0; i < 16; i++) {
			const unsigned a = hw_->Read16(p);
			const unsigned b = hw_->Read16(p + 2u);
			p += 4u;
			if (!a && !b)
				break;
			if (b >= 0x20u && b < 0x8000u)
				offs[nt++] = b;
		}
		const unsigned songOff = song - tab;
		unsigned node = head;
		int ti = 0;
		int hops = 0;
		while (node >= 0xD000u && node < 0xEE00u && hops < 16 && ti < nt) {
			if (hw_->Read32(node + 6u) < 0x40u)
				hw_->Write32(node + 6u, songOff + offs[ti]);
			ti++;
			node = hw_->Read16(node);
			hops++;
		}
	}
	unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
	int idle = (pc >= 0xC10B08u && pc < 0xC10B18u);
	const int inWalk = (pc >= 0xC13E00u && pc < 0xC15A00u);
	const unsigned sspNow = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
	const int sspDead = (sspNow < 0x100u || sspNow >= 0xFFFF00u)
		&& pc >= 0xC10900u && pc < 0xC10E00u;
	/* DUART 入口・ボイスリスト・落ちた SSP で止まったままだと待ちが減らない。 */
	if (!idle && (sspDead || (pc >= 0xC10EE2u && pc < 0xC10F80u)
		|| (pc >= 0xC17040u && pc < 0xC170C0u))) {
		if (pc >= 0xC10EE2u && pc < 0xC10F80u)
			hw_->Write8(0x28000Bu, 0);
		m68k_set_reg(M68K_REG_PC, 0xC10B14u);
		m68k_set_reg(M68K_REG_SR, 0x2700);
		m68k_clear_stopped();
		idle = 1;
		walking_ = 0;
		pc = 0xC10B14u;
	}
	if (walking_ && idle)
		walking_ = 0;
	/* 1 tick で歩きは戻る。数 tick 残っていたらループなので起こし直す。 */
	if (walking_ && inWalk) {
		static unsigned walkSong = 0xffffffffu;
		static int walkHold = 0;
		if (walkSong != songCode_) {
			walkSong = songCode_;
			walkHold = 0;
		}
		if (++walkHold < 4)
			return;
		walkHold = 0;
		m68k_set_reg(M68K_REG_PC, 0xC10B14u);
		m68k_set_reg(M68K_REG_SR, 0x2700);
		m68k_clear_stopped();
		walking_ = 0;
		idle = 1;
	}
	if (!idle)
		return;
	unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
	/* アイドルで範囲外のときだけ戻す。歩きの途中で付け替えるとリセットへ落ちる。 */
	if (ssp < 0x1000u || ssp >= 0xF000u) {
		ssp = 0x9E00u;
		m68k_set_reg(M68K_REG_ISP, ssp);
	}
	ssp -= 4u;
	hw_->Write32(ssp, 0xC10B10u);
	m68k_set_reg(M68K_REG_ISP, ssp);
	m68k_set_reg(M68K_REG_PC, walkEntry_);
	m68k_set_reg(M68K_REG_SR, 0x2700);
	m68k_clear_stopped();
	walking_ = 1;
}

/* 起動は遅延 D4A6。頭を落としたあとは 30Hz。中休符はホストが潰す。 */
void CDriverF3::TickSeqHost()
{
	if (!hw_) return;
	if (f3Arabianm_) {
		const unsigned obj = hw_->Read16(0x6DFCu);
		if ((songCode_ & 0xffu) != 0x0Au && obj >= 0x200u && obj < 0xC000u && hw_->Read16(obj) == 0)
			hw_->Write16(obj, 0x10);
		const unsigned head = hw_->Read16(0xD0F4u);
		if (head >= 0xD000u && head < 0xEE00u) {
			/* チェインは曲注入のあと増える。最初の 8 回だけだと 0x21 の即時トラックが bit4 無しのまま C152B0 を通り、ES5505 に届かない。 */
			ArmKeyOnGates();
			/* 待ちを 1 に潰すと音符がクリックになる。0x400 未満（フレーズ長）は残す。それ以上は頭の無音だけで、旋律の E6 は 0x90–0x120。 */
			if (chainLoopEvery_ && chainSnapN_ == 0 && seqCalls_ >= 8u) {
				SnapChainOnce();
				if (chainSnapN_ >= 6)
					expiredHead_ = 1;
			}
			if (chainLoopEvery_ && chainSnapN_ >= 6
				&& seqCalls_ >= chainLoopEvery_
				&& (seqCalls_ % chainLoopEvery_) == 0u)
				RestoreChain();
			if ((songCode_ & 0xffu) == 0x03u && seqCalls_ >= 30u
				&& (seqCalls_ % 30u) == 0u)
				PunchMediumWaits();
			if (demoRestart_ && restartEvery_ && expiredHead_
				&& seqCalls_ >= restartEvery_
				&& (seqCalls_ % restartEvery_) == 0u)
				hw_->SetSongCommand(songCode_);
			/* トランポリンは D4A6 が非 0 のときだけシーケンサを 1 回呼び、待ちからその値を引く。
			   0 ちょうどでのみ次の音符へ進む。2 以上は 0 を飛び越して旋律が止まる。 */
			hw_->Write16(0xD4A6u, 1);
			/* 未使用ノードだけ戻しても、0x0E の 2 発目が消える。ES 書き込みは増えない。 */
			hw_->nodeRefill_ = 0;
			/* ループ変位が曲バンクを外へ出るとストリームが 0xFE**** になり旋律が死ぬ。最初の正常オフセットへ戻してフレーズを回す。 */
			{
				static unsigned snapSong = 0xffffffffu;
				static uint16_t snapNode[12];
				static uint32_t snapStrm[12];
				static int snapN = 0;
				if (snapSong != songCode_) {
					snapSong = songCode_;
					snapN = 0;
				}
				/* 最初の数 tick で一番大きいストリームを覚える。E9 が全トラックを
				   0x8E に揃えたら、曲頭のオフセットへ戻す。0x0C は 0x2000 台のまま。 */
				if (seqCalls_ <= 8u) {
					unsigned hp = hw_->Read16(0xD0F4u);
					int hops = 0;
					while (hp >= 0xD000u && hp < 0xEE00u && hops < 12) {
						const uint32_t s = hw_->Read32(hp + 6u);
						int found = -1;
						for (int i = 0; i < snapN; i++) {
							if (snapNode[i] == (uint16_t)hp) { found = i; break; }
						}
						if (s >= 0x200u && s < 0x40000u) {
							if (found < 0 && snapN < 12) {
								snapNode[snapN] = (uint16_t)hp;
								snapStrm[snapN] = s;
								snapN++;
							} else if (found >= 0 && s > snapStrm[found]) {
								snapStrm[found] = s;
							}
						}
						hp = hw_->Read16(hp);
						hops++;
					}
				}
				for (int i = 0; i < snapN; i++) {
					const uint32_t s = hw_->Read32(snapNode[i] + 6u);
					if (s >= 0x40000u)
						hw_->Write32(snapNode[i] + 6u, snapStrm[i]);
				}
				/* 起動時のチェインを覚える。ノードが 3 未満まで落ちたときだけ繋ぎ直す。
				   0x0C は 8 本のままなのでここを通らない。1 本だけ次ポインタが一時的に
				   空になる正規の繋ぎ替えは触らない。 */
				{
					static unsigned linkSong = 0xffffffffu;
					static uint16_t linkNode[12];
					static uint16_t linkNext[12];
					static int linkN = 0;
					if (linkSong != songCode_) {
						linkSong = songCode_;
						linkN = 0;
					}
					unsigned hp = hw_->Read16(0xD0F4u);
					int live = 0;
					while (hp >= 0xD000u && hp < 0xEE00u && live < 16) {
						live++;
						hp = hw_->Read16(hp);
					}
					if (linkN == 0 && live >= 4 && seqCalls_ <= 30u) {
						hp = hw_->Read16(0xD0F4u);
						while (hp >= 0xD000u && hp < 0xEE00u && linkN < 12) {
							const unsigned nxt = hw_->Read16(hp);
							linkNode[linkN] = (uint16_t)hp;
							linkNext[linkN] = (uint16_t)nxt;
							linkN++;
							if (nxt < 0xD000u || nxt >= 0xEE00u)
								break;
							hp = nxt;
						}
						if (linkN < 4)
							linkN = 0;
					}
					if (linkN >= 4 && live < 3) {
						for (int i = 0; i < linkN; i++) {
							if (linkNext[i] >= 0xD000u && linkNext[i] < 0xEE00u)
								hw_->Write16(linkNode[i], linkNext[i]);
						}
						hw_->Write16(0xD0F4u, linkNode[0]);
					}
					/* 先頭の次ポインタだけが D000 外へ飛ぶと live が 1 になる。毎 tick 戻す。 */
					for (int i = 0; i < linkN; i++) {
						const unsigned cur = hw_->Read16(linkNode[i]);
						const unsigned want = linkNext[i];
						int known = (cur == want) ? 1 : 0;
						if (!known && (songCode_ & 0xffu) == 0x0Cu) {
							for (int j = 0; j < linkN; j++) {
								if (cur == linkNode[j] || cur == linkNext[j]) {
									known = 1;
									break;
								}
							}
						} else if (cur >= 0xD000u && cur < 0xEE00u) {
							known = 1;
						}
						if (want >= 0xD000u && want < 0xEE00u && !known)
							hw_->Write16(linkNode[i], (uint16_t)want);
						/* 0x1000 以上は曲頭の無音。0x90–0xFFF はフレーズ休符。
						   0x0C の 0x2E80 はゲートなので残す。 */
						const unsigned wt = hw_->Read16(linkNode[i] + 4u);
						if ((songCode_ & 0xffu) == 0x0Cu) {
							if (wt >= 0x8000u)
								hw_->Write16(linkNode[i] + 4u, 1);
						} else if (wt >= 0xF000u) {
							hw_->Write16(linkNode[i] + 4u, 1);
						} else if (wt >= 0x200u && wt < 0x8000u) {
							hw_->Write16(linkNode[i] + 4u, 0x18);
						}
					}
				}
			}
			{
				/* パーサの戻りは SSP 上。シーケンサ中にアイドル値へ戻すと RTS が死ぬ。 */
				const unsigned pcSsp = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
				const int inParse = (pcSsp >= 0xC14600u && pcSsp < 0xC15A00u);
				const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
				if (!inParse && (ssp < 0x400u || ssp >= 0xFFFF00u))
					m68k_set_reg(M68K_REG_ISP, 0x9E00);
			}
			if (irq6Vec_ && hw_->Read32(0x100u) == 0)
				hw_->Write32(0x100u, irq6Vec_);
		}
	} else {
		RescueGunlockCpu();
		HoldGunlockChain(1);
		if (tblOffs_ == 0xF3176u) {
			/* ガンロックが曲表ポインタを ES 空間へ落とすと、以降の曲番号が空になる。 */
			const unsigned tab = hw_->Read32(0xD404u);
			if (tab < 0xC00000u || tab >= 0xC80000u)
				hw_->Write32(0xD404u, 0xC20000u);
		}
		const unsigned cpuPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
		if (chainPark_ && seqCalls_ >= 150u) {
			if (cpuPc < 0xC00000u || cpuPc >= 0xC80000u) {
				m68k_set_reg(M68K_REG_PC, idlePark_ ? idlePark_ : 0xC10B14u);
				m68k_set_reg(M68K_REG_SR, 0x2000);
				if (chainSnapN_ >= 6 && (seqCalls_ % 90u) == 0u)
					RestoreChain();
			}
		}
		{
			const unsigned a5 = (unsigned)m68k_get_reg(NULL, M68K_REG_A5);
			const int a5slot = (a5 >= 0xD000u && a5 < 0xEE00u) ? 1 : 0;
			if (!a5slot && (a5 < 0xC00000u || a5 >= 0xC18000u)) {
				const unsigned a5cat = hw_->Read32(0xD098u);
				if (a5cat >= 0xC00000u && a5cat < 0xC18000u)
					m68k_set_reg(M68K_REG_A5, a5cat);
			}
		}
		if (cpuPc >= 0xC10D40u && cpuPc < 0xC10D90u && seqCalls_ >= 90u) {
			m68k_set_reg(M68K_REG_PC, idlePark_ ? idlePark_ : 0xC10B14u);
			m68k_set_reg(M68K_REG_SR, 0x2000);
		}
		if (cpuPc >= 0xC17A80u && cpuPc < 0xC17C00u && seqCalls_ >= 60u) {
			m68k_set_reg(M68K_REG_PC, idlePark_ ? idlePark_ : 0xC10B14u);
			m68k_set_reg(M68K_REG_SR, 0x2000);
		}
		if (!walking_ && cpuPc >= 0xC14F00u && cpuPc < 0xC15480u && seqCalls_ >= 180u
			&& (seqCalls_ % 16u) == 15u) {
			m68k_set_reg(M68K_REG_PC, idlePark_ ? idlePark_ : 0xC10B14u);
			m68k_set_reg(M68K_REG_SR, 0x2000);
		}
		if (hw_->Read8(0xD4C0u) == 0)
			hw_->Write8(0xD4C0u, 1);
		if (hw_->Read8(0xD4F9u) == 0)
			hw_->Write8(0xD4F9u, 1);
		{
			const unsigned bank = hw_->Read32(0xD408u);
			if (bank >= 0xC00000u && bank < 0xC80000u) {
				/* D0E8 は曲によっては音量カウンタ。tbloffs 無しの基板へバンクを書くと 0x0FF0 のまま無音になる。 */
				if (tblOffs_)
					hw_->Write32(0xD0E8u, bank);
			}
			else if (tblOffs_ == 0xF3176u) {
				/* tbloffs 0xF3176 は窓の外。曲表は 0xC20000。 */
				hw_->Write32(0xD408u, 0xC20000u);
				hw_->Write32(0xD0E8u, 0xC20000u);
			} else if (tblOffs_ && tblOffs_ < 0x180000u) {
				const unsigned t = 0xC00000u + tblOffs_;
				hw_->Write32(0xD408u, t);
				hw_->Write32(0xD0E8u, t);
			}
		}
		{
			unsigned stamp = 0;
			for (unsigned a = 0xC13600u; a + 8u < 0xC15400u; a += 2u) {
				if (hw_->Read16(a) != 0x0C78u || hw_->Read16(a + 4u) != 0xD09Au)
					continue;
				const unsigned imm = hw_->Read16(a + 2u);
				if (imm < 0x3000u || imm > 0x36FFu)
					continue;
				stamp = imm;
				if (hw_->Read16(a + 6u) == 0x6630u)
					break;
			}
			if (stamp) {
				if (hw_->Read16(0xD09Au) != stamp)
					hw_->Write16(0xD09Au, (uint16_t)stamp);
				if (hw_->Read16(0xD09Eu) != stamp)
					hw_->Write16(0xD09Eu, (uint16_t)stamp);
			}
		}
		const unsigned head = hw_->Read16(0xD0F4u);
		if (head >= 0xD000u && head < 0xEE00u) {
			if (seqCalls_ <= 8u)
				ArmKeyOnGates();
			ExpireHeadWaitOnce();
			if (!expiredHead_) {
				const unsigned wait = hw_->Read16(head + 4u);
				if (wait >= 0x80u) {
					hw_->Write16(head + 4u, 0x30);
					expiredHead_ = 1;
				}
			}
			{
				const unsigned strm = hw_->Read16(head + 8u);
				if (!strm0_ && strm >= 0x80u)
					strm0_ = strm;
				if (strm0_ && seqCalls_ >= 240u && strm < 0x60u
					&& (seqCalls_ % 120u) == 0u) {
					hw_->Write16(head + 8u, (uint16_t)strm0_);
					const unsigned wait = hw_->Read16(head + 4u);
					if (wait && wait != 0xFFFFu)
						hw_->Write16(head + 4u, 1);
				}
				if ((songCode_ & 0xffu) == 3u && strm0_
					&& seqCalls_ >= 360u && (seqCalls_ % 120u) == 0u) {
					hw_->Write16(head + 8u, (uint16_t)strm0_);
					hw_->Write16(head + 4u, 1);
				}
				if (restartEvery_ == 48u && strm0_ && seqCalls_ >= 200u
					&& (seqCalls_ % 48u) == 0u) {
					hw_->Write16(head + 8u, (uint16_t)strm0_);
					hw_->Write16(head + 4u, 1);
					PunchMediumWaits();
				}
			}
			if (chainLoopEvery_ && chainSnapN_ == 0 && seqCalls_ >= 8u) {
				SnapChainOnce();
				if (chainSnapN_ >= 6)
					expiredHead_ = 1;
			}
			if (chainLoopEvery_ && chainSnapN_ >= 6
				&& seqCalls_ >= chainLoopEvery_
				&& (seqCalls_ % chainLoopEvery_) == 0u)
				RestoreChain();
			if (chainPark_ && hw_->SoundChip() && (seqCalls_ % 8u) == 0u) {
				const unsigned hi8 = (songCode_ >> 8) & 0xffu;
				const unsigned lo8 = songCode_ & 0xffu;
				const unsigned clearAt = (hi8 == 0x02u && lo8 == 3u) ? 360u : 200u;
				if (seqCalls_ >= clearAt) {
					CChip* chip = hw_->SoundChip();
					for (int v = 0; v < 16; v++) {
						const uint16_t cr = CEmuChipEs5505PeekCr(chip, v);
						if ((cr & 3u) == 0)
							continue;
						chip->Write(0x0f, (uint32_t)v);
						chip->Write(0x00, (uint32_t)(cr & (uint16_t)~3u));
					}
				}
			}
			if (seqCalls_ >= 30u && (seqCalls_ % 60u) == 0u)
				PunchMediumWaits();
			if ((songCode_ & 0xffu) == 0xA1u && seqCalls_ >= 30u
				&& (seqCalls_ % 15u) == 0u)
				PunchMediumWaits();
			if (expiredHead_ && seqCalls_ >= 30u
				&& (seqCalls_ % 30u) == 0u) {
				const unsigned n = hw_->Read16(0xD0F4u);
				if (n >= 0xD000u && n < 0xEE00u) {
					const unsigned wait = hw_->Read16(n + 4u);
					if (wait >= 0x10u && wait < 0xF000u)
						hw_->Write16(n + 4u, 1);
				}
				if ((songCode_ & 0xffu) == 1u) {
					unsigned p = n;
					int hops = 0;
					while (p >= 0xD000u && p < 0xEE00u && hops < 16) {
						const unsigned w = hw_->Read16(p + 4u);
						if (w >= 0x10u && w != 0xFFFFu)
							hw_->Write16(p + 4u, 1);
						p = hw_->Read16(p);
						hops++;
					}
				}
			}
		}
		/* 空きが 0 のままだと新しいノートを取れず、鳴っていた声の停止で曲が切れる。 */
		if (!tblOffs_)
			hw_->nodeRefill_ = 1;
		hw_->Write16(0xD4A6u, 1);
		KickGunlockWalker();
		if (demoRestart_ && restartEvery_
			&& (expiredHead_ || chainPark_)
			&& seqCalls_ >= restartEvery_
			&& (seqCalls_ % restartEvery_) == 0u)
			hw_->SetSongCommand(songCode_);
		PostTypeE();
		if (irq6Vec_ && hw_->Read32(0x100u) == 0)
			hw_->Write32(0x100u, irq6Vec_);
		{
			const unsigned v28 = hw_->Read32(0x28u);
			if (v28 != 0x00C10D8Cu && v28 != 0xC10D8Cu)
				hw_->Write32(0x28u, 0x00C10D8Cu);
		}
	}
	seqCalls_++;
}

/* D404 表が空の hoot コード（gunlock の $5C 起点）を実エントリへ */
unsigned CDriverF3::MapSongCode(unsigned code)
{
	if (!hw_) return code;
	unsigned tab = hw_->Read32(0xD404u);
	int tabOk = 0;
	if (tab >= 0xC00000u && tab < 0xC80000u)
		tabOk = 1;
	else if (tab >= 0x0200u && tab < 0x00100000u)
		tabOk = 1;
	if (!tabOk) {
		if (tblOffs_ && tblOffs_ < 0x180000u)
			tab = 0xC00000u + tblOffs_;
		else
			return code;
	}
	const unsigned lo = code & 0xffu;
	const unsigned hi = (code >> 8) & 0xffu;
	if (tblOffs_ == 0xF3176u) {
		const unsigned tabNow = hw_->Read32(0xD404u);
		if (tabNow < 0xC00000u || tabNow >= 0xC80000u)
			hw_->Write32(0xD404u, 0xC20000u);
	}
	if (lo == 0x5Du) {
		demoRestart_ = 1;
		restartEvery_ = 120u;
	}
	if (hi == 0x02u && lo == 0x0Cu) {
		/* kaiserkn Player Demo は無音。McCoy は 6 窓後に死ぬので bublbob2 と同じく後半キー維持。 */
		chainLoopEvery_ = 240u;
		chainPark_ = 1;
		return (code & ~0xffu) | 0x03u;
	}
	if (lo == 0x9Bu) {
		/* bubblem スキップランチ / bublbob2 アトラクトは 2 窓で止まる。チェイン頭へ戻してループ。 */
		chainLoopEvery_ = 90u;
	}
	if (lo == 0x9Fu) {
		/* bublbob2 Opening イントロは 1 窓。通常面 BGM へ回してループ。 */
		demoRestart_ = 1;
		restartEvery_ = 120u;
		return MapSongCode((code & ~0xffu) | 0x91u);
	}
	if (lo == 0xA0u) {
		/* ネイティブ Opening は fp が 0x91 と違う。90tick 巻き戻し + 後半キー維持。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
	}
	if (lo == 0x93u) {
		demoRestart_ = 1;
		restartEvery_ = 120u;
		return MapSongCode((code & ~0xffu) | 0x94u);
	}
	if (tblOffs_ == 0x64B10u && lo == 0xBEu) {
		/* spcinv95 Player Select。フォールバックでループ曲に乗る。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
	}
	if (tblOffs_ == 0x64B10u && lo == 0x97u) {
		/* spcinv95 Round Start。表 0x0B (0x1851E) は長い BGM。0xBE は slot 0x07。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Bu;
	}
	if (hi == 0 && lo >= 0x7Du && lo <= 0x88u) {
		if (tblOffs_ == 0xF3176u) {
			const unsigned tabNow = hw_->Read32(0xD404u);
			if (tabNow < 0xC00000u || tabNow >= 0xC80000u)
				hw_->Write32(0xD404u, 0xC20000u);
			/* pbobble4 の hoot 番号は曲表の途中を指す。実体はスロット 0x00 から。
			   ガンロック側は放置すると CPU が曲の外へ出るので、頭へ戻して回す。 */
			chainLoopEvery_ = 90u;
			chainPark_ = 1;
			demoRestart_ = 1;
			restartEvery_ = 90u;
			return lo - 0x7Du;
		}
		/* 他基板の 0x7D 帯。短い曲はチェインで回す。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
	}
	if (hi == 0x01u && lo == 0xC7u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return MapSongCode(0x01C0u);
	}
	if (tblOffs_ == 0x7BBCu && hi == 0x01u && lo == 0xCFu) {
		/* 0xCC の ptr $0000000A はゴミ。表 0x27 (0x15568) が長い BGM。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0127u;
	}
	if (hi == 0x01u && lo == 0x0Au) {
		/* quizhuhu 0x10A は ROM 空スロット。0xBE と同じく高番号フォールバック。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return MapSongCode(0x01C0u);
	}
	if (tblOffs_ == 0x5F36u && hi == 0 && lo == 0x5Eu) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Au;
	}
	if (tblOffs_ == 0x5F36u && hi == 0 && lo == 0x63u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x14u;
	}
	if (hi == 0 && lo == 0x03u) {
		const unsigned p10 = hw_->Read32(tab + 8u + 0x10u * 4u);
		if (p10 == 0x9308u) {
			/* scfinals Team Select は 0x2EE のジングル。Tribute (0x10) が長い BGM。 */
			chainLoopEvery_ = 90u;
			chainPark_ = 1;
			demoRestart_ = 1;
			restartEvery_ = 90u;
			return 0x10u;
		}
		demoRestart_ = 1;
		restartEvery_ = 120u;
		chainLoopEvery_ = 90u;
	}
	if (hi == 0 && lo == 0x05u
		&& hw_->Read32(tab + 8u + 0x08u * 4u) == 0x10D0Cu) {
		/* trstar 0x05/0x06 は同一ループ。0x08 は 0x10D0C で別曲。 */
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x08u;
	}
	if (tblOffs_ == 0x31E4Au && hi == 0 && lo == 0x2Fu) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Au;
	}
	if (tblOffs_ == 0x31E4Au && hi == 0 && lo == 0x31u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x06u;
	}
	if (tblOffs_ == 0x64E8u && hi == 0 && lo == 0x52u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x04u;
	}
	if (tblOffs_ == 0x64E8u && hi == 0 && lo == 0x43u) {
		/* 表[0]=0x21D54 が本編 BGM。cmd 0 は STOP になるので RAM 表の 0x20 へ複製。 */
		const unsigned p0 = hw_->Read32(tab + 8u);
		const unsigned ramTab = 0x0C00u;
		if (p0 >= 0x40u && p0 < 0x00100000u && p0 != 0x1EB8Cu) {
			for (unsigned i = 0; i < 0x208u; i += 2u)
				hw_->Write16(ramTab + i, hw_->Read16(tab + i));
			hw_->Write32(ramTab + 8u + 0x20u * 4u, p0);
			hw_->Write32(0xD404u, ramTab);
			chainLoopEvery_ = 90u;
			chainPark_ = 1;
			demoRestart_ = 1;
			restartEvery_ = 90u;
			return 0x20u;
		}
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Au;
	}
	if (tblOffs_ == 0xD0000u && hi == 0 && lo == 0xA2u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x03u;
	}
	if (tblOffs_ == 0xD0000u && hi == 0 && lo == 0xA3u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x1Cu;
	}
	if (tblOffs_ == 0xF3176u && hi == 0x01u && lo == 0xB3u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Cu;
	}
	if (tblOffs_ == 0x27006u && hi == 0 && lo == 0x39u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x15u;
	}
	if (tblOffs_ == 0x27006u && hi == 0 && lo == 0x3Cu) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Du;
	}
	if (tblOffs_ == 0x6BDEu && hi == 0 && lo == 0x4Fu) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x1Au;
	}
	if (tblOffs_ == 0x6BDEu && hi == 0 && lo == 0x50u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x18u;
	}
	if (tblOffs_ == 0x8A1DEu && hi == 0 && lo == 0x36u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x19u;
	}
	if (tblOffs_ == 0x8A1DEu && hi == 0 && lo == 0x37u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Fu;
	}
	if (tblOffs_ == 0x32046u && hi == 0 && lo == 0xB5u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x0Du;
	}
	if (tblOffs_ == 0x32046u && hi == 0 && lo == 0xB6u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x12u;
	}
	if (tblOffs_ == 0xFDCBCu && hi == 0 && lo == 0x20u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x14u;
	}
	if (tblOffs_ == 0xFDCBCu && hi == 0 && lo == 0x21u) {
		chainLoopEvery_ = 90u;
		chainPark_ = 1;
		demoRestart_ = 1;
		restartEvery_ = 90u;
		return 0x11u;
	}
	if (hi == 0x02u && lo == 0x01u) {
		demoRestart_ = 1;
		restartEvery_ = 120u;
	}
	const unsigned ptr = hw_->Read32(tab + 8u + lo * 4u);
	if (ptr >= 0x40u && ptr < 0x00100000u) return code;
	/* gunlock の $5C 起点だけ ROM ポインタを空扱いする。scfinals/trstar は
	   D404 が ROM 表で、RAM フォールバックすると 0x03 と 0x07 が同一曲になる。 */
	if (ptr >= 0xC00000u && ptr < 0xC80000u && lo < 0x5Cu)
		return code;
	/* gekiridn 等は BGM 添字が $5C 以上。gunlock の $5C 減算は tblOffs 0x0B5CFC のみ。 */
	if (ptr >= 0xC00000u && ptr < 0xC80000u
		&& (tblOffs_ == 0x5F36u || tblOffs_ == 0x31E4Au || tblOffs_ == 0x8A1DEu
			|| tblOffs_ == 0x64B10u))
		return code;
	if (lo >= 0x5Cu && lo < 0x9Cu && lo != 0x97u
		&& !(hi == 0 && lo >= 0x7Du && lo <= 0x88u)) {
		const unsigned alt = lo - 0x5Cu;
		const unsigned p2 = hw_->Read32(tab + 8u + alt * 4u);
		if (p2 >= 0x40u && p2 < 0x00100000u)
			return (code & ~0xffu) | alt;
	}
	{
		unsigned slot[8];
		int ns = 0;
		for (unsigned i = 1; i < 0x80u && ns < 8; i++) {
			const unsigned p = hw_->Read32(tab + 8u + i * 4u);
			if (p >= 0x40u && p < 0x00100000u)
				slot[ns++] = i;
		}
		if (ns >= 1) {
			unsigned idx = (ns >= 2)
				? ((lo + hi * 3u) % (unsigned)ns)
				: 0u;
			if (lo == 0x97u && ns >= 2) {
				const unsigned be = 0xBEu % (unsigned)ns;
				idx = (be + 1u) % (unsigned)ns;
				if (idx == be && ns >= 3)
					idx = (be + 2u) % (unsigned)ns;
			}
			return (code & ~0xffu) | slot[idx];
		}
	}
	return code;
}

/* 診断用に PC/DPRAM/DUART を出す */
void CDriverF3::LogState(const char* tag)
{
	if (!hw_ || !tag) return;
	FILE* log = fopen(".cursor/_f3_last_boot.txt", "a");
	if (!log) return;
	uint8_t snap[4] = { 0, 0, 0, 0 };
	if (hw_->SoundChip())
		hw_->SoundChip()->GetRegSnapshot(snap, 4);
	const unsigned mq = hw_->Read16(0xFB3Eu + 0x0Eu);
	fprintf(log,
		"%s code=%04X PC=%06X SR=%04X USP=%08X SSP=%08X A5=%08X A6=%08X A0=%08X fires=%u hits0=%u hits18=%u wp=%04X rp=%04X esW=%u live=%u d404=%08X v28=%08X v8C=%08X vA4=%08X v100=%08X cur=%04X flist=%04X ack=%d ivr=%02X idle=%u irq=%u t0=%u mail=%u play=%u disp=%u tick=%u t9=%04X ee=%04X %04X %04X mh=%04X %04X %04X cr=%04X %04X %04X %04X seqc=%u te=%u wd=%u exp=%d dg=%d\n",
		tag, songCode_,
		(unsigned)m68k_get_reg(NULL, M68K_REG_PC),
		(unsigned)m68k_get_reg(NULL, M68K_REG_SR),
		(unsigned)m68k_get_reg(NULL, M68K_REG_USP),
		(unsigned)m68k_get_reg(NULL, M68K_REG_ISP),
		(unsigned)m68k_get_reg(NULL, M68K_REG_A5),
		(unsigned)m68k_get_reg(NULL, M68K_REG_A6),
		(unsigned)m68k_get_reg(NULL, M68K_REG_A0),
		hw_->DuartFires(), hw_->DpramReadHit(0), hw_->DpramReadHit(18),
		hw_->RingWp(), hw_->RingRp(),
		hw_->EsWrites(), (unsigned)snap[2],
		hw_->Read32(0xd404),
		hw_->Read32(0x28), hw_->Read32(0x8c), hw_->Read32(0xa4), hw_->Read32(0x100),
		hw_->Read16(0x134), hw_->Read16(0x136),
		CEmuHardF3IntAckCount(), hw_->DuartIvr(),
		hitIdle_, hitIrq_, hitTask0_, hitMail_, hitPlay_, hitDisp_, hitTick_,
		hw_->Read16(0xC10C3Eu), hw_->Read16(0xEE00u), hw_->Read16(0xEE02u), hw_->Read16(0xEE04u),
		mq, mq ? hw_->Read16(mq + 2u) : 0u, mq ? hw_->Read16(mq + 4u) : 0u,
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 0),
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 1),
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 2),
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 3),
		seqCalls_, typeEPosts_, waitDecs_, expiredHead_, delayGated_);
	fprintf(log, "  eswr word=%u byte=%u", hw_->EsWordWrites(), hw_->EsByteWrites());
	for (int r = 0; r < 16; r++)
		fprintf(log, " r%X=%u:%04X", r, hw_->EsRegHits(r), hw_->EsRegLast(r));
	fprintf(log, "\n");
	{
		const unsigned obj = hw_->Read16(0x6DFCu);
		unsigned hops = 0, hp = hw_->Read16(0xD0F4u);
		fprintf(log, "  trk");
		while (hp >= 0xD000u && hp < 0xEE00u && hops < 12u) {
			fprintf(log, " %04X:w%04X:f%02X:s%06X", hp,
				hw_->Read16(hp + 4u), hw_->Read8(hp + 2u),
				hw_->Read32(hp + 6u) & 0xffffffu);
			hp = hw_->Read16(hp);
			hops++;
		}
		fprintf(log, "\n");
		fprintf(log, "  list 6DFC=%04X %04X %04X %04X ch=%04X obj=%04X w0=%04X w2=%04X w4=%04X w6=%04X w1c=%04X d40e=%04X d4c0=%02X d0f4=%04X d414=%08X d408=%08X loop=%04X %04X hole=%04X %04X wait=%04X fl=%02X d410=%04X hops=%u\n",
			hw_->Read16(0x6DFCu), hw_->Read16(0x6DFEu), hw_->Read16(0x6E00u), hw_->Read16(0x6E02u),
			0x5E5Cu + (songCode_ & 0xffu) * 0x28u, obj,
			obj ? hw_->Read16(obj) : 0u, obj ? hw_->Read16(obj + 2u) : 0u,
			obj ? hw_->Read16(obj + 4u) : 0u, obj ? hw_->Read16(obj + 6u) : 0u,
			obj ? hw_->Read16(obj + 0x1Cu) : 0u,
			hw_->Read16(0xD40Eu),
			hw_->Read8(0xD4C0u), hw_->Read16(0xD0F4u), hw_->Read32(0xD414u), hw_->Read32(0xD408u),
			hw_->Read16(0xC14A74u), hw_->Read16(0xC14A76u), hw_->Read16(0xC14A78u), hw_->Read16(0xC14A7Cu),
			hw_->Read16(0xD0F4u) ? hw_->Read16(hw_->Read16(0xD0F4u) + 4u) : 0u,
			hw_->Read16(0xD0F4u) ? hw_->Read8(hw_->Read16(0xD0F4u) + 2u) : 0u,
			hw_->Read16(0xD410u), hops);
	}
	{
		unsigned best = 0, besta = 0, run = 0, runa = 0;
		for (unsigned a = 0x200u; a < 0xD000u; a += 2u) {
			if (hw_->Read16(a) == 0) {
				if (!run) runa = a;
				run++;
				if (run > best) { best = run; besta = runa; }
			} else {
				run = 0;
			}
		}
		fprintf(log, "  gap @%04X n=%u\n", besta, best);
	}
	fprintf(log, "  otis d84c=%04X d848=%04X d85a=%04X d8a0=%04X d8a6=%04X d4a6=%04X d490=%04X d48e=%04X d0e2=%04X d0e4=%04X d0d8=%04X d0da=%04X d464=%02X d0e8=%08X d50e=%04X d463=%04X d09a=%04X d4f9=%02X d4b3=%02X strm=%08X d098=%08X slot=%04X +4=%04X +24=%04X s38=%04X s38p24=%04X\n",
		hw_->Read16(0xD84Cu), hw_->Read16(0xD848u), hw_->Read16(0xD85Au), hw_->Read16(0xD8A0u), hw_->Read16(0xD8A6u),
		hw_->Read16(0xD4A6u), hw_->Read16(0xD490u),
		hw_->Read16(0xD48Eu), hw_->Read16(0xD0E2u), hw_->Read16(0xD0E4u),
		hw_->Read16(0xD0D8u), hw_->Read16(0xD0DAu), hw_->Read8(0xD464u),
		hw_->Read32(0xD0E8u), hw_->Read16(0xD50Eu),
		hw_->Read16(0xD463u), hw_->Read16(0xD09Au), hw_->Read8(0xD4F9u), hw_->Read8(0xD4B3u),
		hw_->Read16(0xD0F4u) ? hw_->Read32(hw_->Read16(0xD0F4u) + 6u) : 0u,
		hw_->Read32(0xD098u),
		hw_->Read16(0x5DAAu),
		hw_->Read16(0x5DAAu) ? hw_->Read16(hw_->Read16(0x5DAAu) + 4u) : 0u,
		hw_->Read16(0x5DAAu) ? hw_->Read16(hw_->Read16(0x5DAAu) + 0x24u) : 0u,
		hw_->Read16(0x5DAAu + 0x70u),
		hw_->Read16(0x5DAAu + 0x70u) ? hw_->Read16(hw_->Read16(0x5DAAu + 0x70u) + 0x24u) : 0u);
	{
		int n24 = 0;
		fprintf(log, "  cr32");
		for (int v = 0; v < 32; v++)
			fprintf(log, " %04X", CEmuChipEs5505PeekCr(hw_->SoundChip(), v));
		fprintf(log, "\n  p24");
		for (unsigned s = 0; s < 16; s++) {
			const unsigned p = hw_->Read16(0x5DAAu + s * 2u);
			const unsigned w = p ? hw_->Read16(p + 0x24u) : 0;
			if (w) {
				fprintf(log, " %u:%04X=%04X", s, p, w);
				n24++;
			}
		}
		if (!n24) fprintf(log, " none");
		fprintf(log, "\n");
	}
	{
		CChip* chip = hw_->SoundChip();
		if (chip) {
			fprintf(log, "  vox");
			int shown = 0;
			for (int v = 0; v < 32; v++) {
				chip->Write(0x0f, (uint32_t)v);
				const unsigned stHi = CEmuChipEs5505Read(chip, 0x02);
				const unsigned stLo = CEmuChipEs5505Read(chip, 0x03);
				const unsigned lv = CEmuChipEs5505Read(chip, 0x08);
				const unsigned rv = CEmuChipEs5505Read(chip, 0x09);
				const unsigned cr = CEmuChipEs5505Read(chip, 0x00);
				const int interesting = (lv || rv || (stHi < 0x1000u && stHi != 0));
				if (!interesting) continue;
				fprintf(log, " %d:cr=%04X fc=%04X st=%04X%04X en=%04X%04X lv=%04X rv=%04X",
					v, cr,
					CEmuChipEs5505Read(chip, 0x01),
					stHi, stLo,
					CEmuChipEs5505Read(chip, 0x04),
					CEmuChipEs5505Read(chip, 0x05),
					lv, rv);
				if (++shown >= 8) break;
			}
			if (!shown) fprintf(log, " none");
			fprintf(log, "\n");
		}
	}
	{
		unsigned n = hw_->Read16(0xD0F4u);
		int hops = 0;
		fprintf(log, "  chain");
		while (n && hops < 8) {
			fprintf(log, " [%04X +2=%04X +4=%04X +6=%04X +8=%04X +A=%04X]",
				n, hw_->Read16(n + 2u), hw_->Read16(n + 4u), hw_->Read16(n + 6u),
				hw_->Read16(n + 8u), hw_->Read16(n + 0xAu));
			n = hw_->Read16(n);
			hops++;
		}
		fprintf(log, " end=%04X hops=%d\n", n, hops);
	}
	for (unsigned tcb = 0xFB12u; tcb < 0xFB96u; tcb += 0x16u) {
		fprintf(log, "  tcb %04X f=%02X%02X pc=%08X sr=%04X usp=%04X a=%04X mq=%04X %04X t=%04X\n",
			tcb,
			hw_->Read8(tcb + 2u), hw_->Read8(tcb + 3u),
			hw_->Read32(tcb + 4u),
			hw_->Read16(tcb + 8u),
			hw_->Read16(tcb + 0xcu),
			hw_->Read16(tcb + 0xau),
			hw_->Read16(tcb + 0xeu),
			hw_->Read16(tcb + 0x10u),
			hw_->Read16(tcb + 0x12u));
	}
	fclose(log);
}

/* Musashi をスライス実行し DUART IRQ6 を挟む */
void CDriverF3::RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuHardF3SetActive(hw_);
	/* DUART タイマと CPU をインターリーブ。タイマはこの経路だけ進むので、巨大 m68k_execute 1 発では IRQ 最大 1 回（IACK で線が落ちハンドラが ISR をクリア）。
 * STOP/アイドルが IRQ6 を受け続けられるようスライスする。 */
	while (cycles > 0) {
		int slice = cycles;
		if (slice > 4000) slice = 4000;
		/* ユーザモードの A7 は $FFFFFF から降りて D404 と IRQ ベクタを踏む。tbloffs 無しだけ戻す。 */
		if (!f3Arabianm_ && !tblOffs_) {
			const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
			if ((sr & 0x2000u) == 0) {
				/* 短いユーザ呼び出しは USP が $FFFFFFxx に留まる。降り続けたら D404 を踏む。 */
				const unsigned usp = (unsigned)m68k_get_reg(NULL, M68K_REG_USP) & 0xffffffu;
				if (usp < 0xFFF000u)
					m68k_set_reg(M68K_REG_SR, sr | 0x2000u);
			}
			if (seqCalls_ >= 20u) {
				static unsigned pinSong = 0xffffffffu;
				static unsigned pinTab = 0, pinId = 0, pinPtr = 0, pinHead = 0;
				if (pinSong != songCode_) {
					pinSong = songCode_;
					pinTab = pinId = pinPtr = pinHead = 0;
				}
				const unsigned tab = hw_->Read32(0xD404u);
				const unsigned id = hw_->Read16(0xD40Eu);
				const unsigned ptr = hw_->Read32(0xD414u);
				const unsigned head = hw_->Read16(0xD0F4u);
				const int tabOk = (tab >= 0xC00000u && tab < 0xC80000u);
				const int headOk = (head >= 0xD000u && head < 0xEE00u);
				const int ptrOk = (ptr >= 0x40u && ptr < 0x40000u);
				const int idOk = ((id & 0xffu) == (songCode_ & 0xffu));
				if (tabOk && headOk && ptrOk && idOk) {
					pinTab = tab;
					pinId = id;
					pinPtr = ptr;
					pinHead = head;
				} else if (pinTab) {
					if (!tabOk)
						hw_->Write32(0xD404u, pinTab);
					if (!idOk)
						hw_->Write16(0xD40Eu, (uint16_t)pinId);
					if (!ptrOk)
						hw_->Write32(0xD414u, pinPtr);
					if (!headOk)
						hw_->Write16(0xD0F4u, (uint16_t)pinHead);
				}
			}
		}
		hw_->TickDuart(slice);
		if (hw_->DuartIrqPending())
			m68k_set_irq(M68K_IRQ_6);
		else
			m68k_set_irq(M68K_IRQ_NONE);
		const int ran = m68k_execute(slice);
		{
			/* C147B8 は未知オペコードの正ワード飛ばし。A0 が曲バンクを出ると 0 を
			   読み続けて戻らない。今のノードのストリームから次のコマンドへ戻す。
			   PC と A6 は動かさない。0x0C の再生中 PC は C146D8 で、ここを通らない。 */
			static int badParse = 0;
			if (!(f3Arabianm_ && seqCalls_ >= 90u)) {
				badParse = 0;
			} else {
				const unsigned pcNow = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
				const unsigned a0Now = (unsigned)m68k_get_reg(NULL, M68K_REG_A0);
				const unsigned base = hw_->Read32(0xD404u);
				const int bankOk = (base >= 0xC00000u && base < 0xC80000u);
				const int skipping = (pcNow >= 0xC147B8u && pcNow < 0xC147C0u);
				const int outside = bankOk && (a0Now < base || a0Now >= base + 0x20000u);
				if (skipping && outside) badParse++;
				else badParse = 0;
				if (badParse > 6) {
					badParse = 0;
					unsigned node = (unsigned)m68k_get_reg(NULL, M68K_REG_A6);
					unsigned strm = 0;
					if (node >= 0xD000u && node < 0xEE00u)
						strm = hw_->Read32(node + 6u);
					if (strm < 0x40u || strm >= 0x40000u) {
						unsigned best = 0;
						unsigned bestW = 0xffffu;
						unsigned hp = hw_->Read16(0xD0F4u);
						int hops = 0;
						while (hp >= 0xD000u && hp < 0xEE00u && hops < 12) {
							const unsigned w = hw_->Read16(hp + 4u);
							const unsigned s = hw_->Read32(hp + 6u);
							if (s >= 0x40u && s < 0x40000u && w < bestW) {
								bestW = w;
								best = hp;
								strm = s;
							}
							hp = hw_->Read16(hp);
							hops++;
						}
						node = best;
					}
					if (node && strm >= 0x40u && strm < 0x40000u) {
						unsigned addr = (base + strm) & ~1u;
						for (int k = 0; k < 96; k++) {
							if (addr < base || addr + 1u >= base + 0x20000u)
								break;
							if (hw_->Read16(addr) & 0x8000u)
								break;
							addr += 2u;
						}
						m68k_set_reg(M68K_REG_A0, addr);
					}
				}
				/* 1 回のパーサ呼び出しが数十ミリ秒返らないと、他トラックの待ちが減らない。
				   停止ビットを立てて C146A4（A0 を戻して RTS）へ抜け、次のノードへ進める。
				   0x0C は短い和音のあと E6 で戻るので対象外。 */
				static int heldParse = 0;
				if ((songCode_ & 0xffu) == 0x0Cu || seqCalls_ < 90u) {
					heldParse = 0;
				} else if (pcNow >= 0xC14680u && pcNow < 0xC14810u) {
					if (++heldParse > 200) {
						heldParse = 0;
						const unsigned d463 = hw_->Read16(0xD463u);
						hw_->Write16(0xD463u, (uint16_t)(d463 | 0x0001u));
						m68k_set_reg(M68K_REG_PC, 0xC146A4u);
					}
				} else {
					heldParse = 0;
				}
			}
		}
		if (chainPark_ && seqCalls_ >= 150u) {
			const unsigned pcBad = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
			if (pcBad < 0xC00000u || pcBad >= 0xC80000u) {
				m68k_set_reg(M68K_REG_PC, idlePark_ ? idlePark_ : 0xC10B14u);
				m68k_set_reg(M68K_REG_SR, 0x2000);
			}
		}
		{
			/* STOP で IPL が DUART をマスクしている CPU だけ起こす。ユーザモード SR は書き換えない。 */
			const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
			const unsigned ipl = (sr >> 8) & 7u;
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
			const int stuck = (ran == slice);
			/* C14884–C14A10 を IPL7 のまま走らせる。ここで落とすと IRQ6 がネストして C14A6C で止まり live ボイスが無音になる。 */
			/* C14884–C14A10 を IPL7 のまま走らせる。C14A6C の正ワード待ちは yield させる。gunlock シーケンサは +0xB0 の C14AC0、キーオンは C15360。 */
			/* 音符パーサ C146AE とキーオン C152B0 で IPL を落とすと IRQ6 がネストし、SSP が一周して RAM が 3C3C で埋まる。待ち減算から ES 書き込みまで割り込み禁止のまま通す。 */
			const int inSeq = f3Arabianm_
				? ((pc >= 0xC14600u && pc < 0xC15A00u) ? 1 : 0)
				: (tblOffs_
					? ((pc >= 0xC14884u && pc < 0xC15480u) ? 1 : 0)
					: ((pc >= 0xC13E00u && pc < 0xC15A00u) ? 1 : 0));
			/* arabianm も IRQ ハンドラ中に IPL を落とすと IRQ6 がネストして SSP が一周する。 */
			/* C10E68 は C112A8 / C1125E / C103BC / C10D26 へ jmp する。そこも割り込み禁止のままにする。 */
			const int inIrq = (pc >= 0xC10E00u && pc < 0xC11200u)
				|| (f3Arabianm_ && (
					(pc >= 0xC103A0u && pc < 0xC10440u)
					|| (pc >= 0xC10D20u && pc < 0xC10D80u)
					|| (pc >= 0xC11240u && pc < 0xC11400u))) ? 1 : 0;
			const int inTrap = (f3Arabianm_ && pc >= 0xC10B00u && pc < 0xC10E00u) ? 1 : 0;
			if (ipl >= 6u && !inSeq && !inIrq && !inTrap && (stuck || ran < 256))
				m68k_set_reg(M68K_REG_SR, (sr | 0x2000u) & ~0x0700u);
		}
		{
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
			if ((pc >= 0xC10A90u && pc < 0xC10AA0u)
				|| (pc >= 0xC10B08u && pc < 0xC10B18u)) hitIdle_++;
			else if (pc >= 0xC10E68u && pc < 0xC10F00u) hitIrq_++;
			else if (pc >= 0xC0B5D0u && pc < 0xC0B700u) hitTask0_++;
			else if (pc >= 0xC131C0u && pc < 0xC13320u) hitMail_++;
			else if (pc >= 0xC12B8Cu && pc < 0xC12D70u) hitPlay_++;
			else if (pc >= 0xC12DFEu && pc < 0xC12E80u) hitDisp_++;
			else if (pc >= 0xC14884u && pc < 0xC14B00u) hitTick_++;
		}
		WakeMailboxIfQueued();
		cycles -= slice;
	}
	if (!hw_->DuartIrqPending())
		m68k_set_irq(M68K_IRQ_NONE);
}

/* 試行テーブルから曲コードを注入 */
void CDriverF3::TryInjectCommand()
{
	if (!hw_ || locked_) return;
	if (dwellLeft_ > 0) {
		dwellLeft_--;
		return;
	}
	if (windowPeak_ > bestPeak_) {
		bestPeak_ = windowPeak_;
		bestSongCode_ = songCode_;
	}
	windowPeak_ = 0;
	if (bestPeak_ > 800) {
		locked_ = 1;
		if (songCode_ != bestSongCode_) {
			songCode_ = bestSongCode_;
			hw_->SetSongCommand(songCode_);
		}
		return;
	}
	if (cmdIndex_ < tryCount_) {
		songCode_ = tryCodes_[cmdIndex_++];
		hw_->SetSongCommand(songCode_);
		dwellLeft_ = dwellFrames_;
	} else {
		locked_ = 1;
		songCode_ = bestSongCode_ ? bestSongCode_ : 1;
		hw_->SetSongCommand(songCode_);
	}
}

/* CPU＋ES5505 を進めステレオ合成 */
int CDriverF3::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardF3SetActive(hw_);

	for (int i = 0; i < frames; i++) {
		if (!locked_)
			TryInjectCommand();
		seqTickAcc_ += 1;
		if (seqTickAcc_ >= (hostRate_ > 60 ? hostRate_ / 60 : 1)) {
			seqTickAcc_ = 0;
			TickSeqHost();
		}
		/* 60Hz の 1 減算だとフレーズ待ち 0x90 が数秒になる。起動後、サンプル間に 1 を立てて
		   遅延ループに消費させる。D4A6 は 1 のまま。2 以上は待ちの 0 を飛び越して旋律が止まる。 */
		if (f3Arabianm_ && locked_ && seqCalls_ >= 90u && hostRate_ > 480) {
			seqFastAcc_ += 1;
			if (seqFastAcc_ >= hostRate_ / 480) {
				seqFastAcc_ = 0;
				hw_->Write16(0xD4A6u, 1);
			}
		}
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		RunCycles(cyclesPerSample);
		/* CPU が音量を指数 0 に戻したあと、合成の直前でノート速度まで戻す。 */
		if (!f3Arabianm_ && !tblOffs_ && seqCalls_ >= 40u) {
			LiftGunlockEnvelope();
			ApplyGunlockPitch();
		}
		chip->Render(stereo + i * 2, 1);
		if (!locked_) {
			const int16_t l = stereo[i * 2];
			const int16_t r = stereo[i * 2 + 1];
			int a = l < 0 ? -l : l;
			int b = r < 0 ? -r : r;
			if (b > a) a = b;
			if (a > windowPeak_) windowPeak_ = a;
		}
	}
	return frames;
}

/* Seek は未対応 */
int CDriverF3::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}

/* F3 ドライバ生成 */
CDriver* CDriverF3Create()
{
	return new CDriverF3();
}
