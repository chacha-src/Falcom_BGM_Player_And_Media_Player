#include "StdAfx.h"
#include "cemu_driver_f3.h"
#include "../chip/cemu_chip_es5505.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
{
	memset(tryCodes_, 0, sizeof(tryCodes_));
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

	songCode_ = titleCode ? titleCode : 1;
	CDriverF3Push(tryCodes_, &tryCount_, (int)_countof(tryCodes_), songCode_);
	if (tryCount_ < 1) {
		tryCodes_[0] = 1;
		tryCount_ = 1;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

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
	songCode_ = tryCodes_[0];
	cmdIndex_ = tryCount_;
	locked_ = 1;
	songCode_ = MapSongCode(songCode_);
	hw_->SetSongCommand(songCode_);
	/* C15702（C15538 内のボイスチェイン）は D4C0 が立つまで即 return。実機は task0 がフラグを ST。
	   D4F9 は C15702 のキーオン許可で、カタログの bset #4 前に見る。 */
	hw_->Write8(0xD4F9u, 1);
	hw_->Write8(0xD4C0u, 1);
	/* $6DFC は植えない。C12B8C はリストにあれば停止（compact + C12AD0 が 5DAA+4 を壊す）。C12E70 は常に C12C36 へ BRA して開始。 */
	KickMailboxOnce();
	RunCycles(cpuHz_ / 2);
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
			if (seqCalls_ <= 8u)
				ArmKeyOnGates();
			ExpireHeadWaitOnce();
			if (seqCalls_ >= 60u && (seqCalls_ % 120u) == 0u)
				PunchMediumWaits();
			if (expiredHead_ && seqCalls_ >= 60u && seqCalls_ <= 720u
				&& (seqCalls_ % 60u) == 0u) {
				const unsigned n = hw_->Read16(0xD0F4u);
				if (n >= 0xD000u && n < 0xEE00u) {
					const unsigned wait = hw_->Read16(n + 4u);
					if (wait >= 0x10u)
						hw_->Write16(n + 4u, 1);
				}
			}
			if (expiredHead_ && seqCalls_ < 480u && hw_->SoundChip()) {
				CChip* chip = hw_->SoundChip();
				for (int v = 0; v < 8; v++) {
					const uint16_t cr = CEmuChipEs5505PeekCr(chip, v);
					if ((cr & 3u) == 0)
						continue;
					chip->Write(0x0f, (uint32_t)v);
					chip->Write(0x00, (uint32_t)(cr & (uint16_t)~3u));
				}
			}
			if (!expiredHead_ || (seqCalls_ & 1u) == 0u)
				hw_->Write16(0xD4A6u, 1);
			{
				const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
				if (ssp < 0x400u || ssp >= 0xFFFF00u)
					m68k_set_reg(M68K_REG_ISP, 0x9E00);
			}
			if (irq6Vec_ && hw_->Read32(0x100u) == 0)
				hw_->Write32(0x100u, irq6Vec_);
		}
	} else {
		const unsigned cpuPc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
		if (cpuPc >= 0xC10B08u && cpuPc < 0xC10B20u) {
			const unsigned a5cat = hw_->Read32(0xD098u);
			if (a5cat >= 0xC00000u && a5cat < 0xC18000u)
				m68k_set_reg(M68K_REG_A5, a5cat);
		}
		if (cpuPc >= 0xC14F00u && cpuPc < 0xC15480u && seqCalls_ >= 180u
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
			if (bank >= 0xC00000u && bank < 0xC80000u)
				hw_->Write32(0xD0E8u, bank);
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
			if (seqCalls_ >= 30u && (seqCalls_ % 60u) == 0u)
				PunchMediumWaits();
			if (expiredHead_ && seqCalls_ >= 30u
				&& (seqCalls_ % 30u) == 0u) {
				const unsigned n = hw_->Read16(0xD0F4u);
				if (n >= 0xD000u && n < 0xEE00u) {
					const unsigned wait = hw_->Read16(n + 4u);
					if (wait >= 0x10u && wait < 0xF000u)
						hw_->Write16(n + 4u, 1);
				}
			}
		}
		if (!expiredHead_ || (seqCalls_ & 1u) == 0u)
			hw_->Write16(0xD4A6u, 1);
		PostTypeE();
		{
			const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
			if (ssp < 0x400u || ssp >= 0xFFFF00u)
				m68k_set_reg(M68K_REG_ISP, 0x9E00);
		}
		if (irq6Vec_ && hw_->Read32(0x100u) == 0)
			hw_->Write32(0x100u, irq6Vec_);
	}
	seqCalls_++;
}

/* D404 表が空の hoot コード（gunlock の $5C 起点）を実エントリへ */
unsigned CDriverF3::MapSongCode(unsigned code)
{
	if (!hw_) return code;
	const unsigned tab = hw_->Read32(0xD404u);
	if (tab < 0xC00000u || tab >= 0xC80000u) return code;
	const unsigned lo = code & 0xffu;
	const unsigned ptr = hw_->Read32(tab + 8u + lo * 4u);
	if (ptr != 0 && ptr < 0x00100000u) return code;
	if (lo >= 0x5Cu && lo < 0x9Cu) {
		const unsigned alt = lo - 0x5Cu;
		const unsigned p2 = hw_->Read32(tab + 8u + alt * 4u);
		if (p2 != 0 && p2 < 0x00100000u)
			return (code & ~0xffu) | alt;
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
	{
		const unsigned obj = hw_->Read16(0x6DFCu);
		fprintf(log, "  list 6DFC=%04X %04X %04X %04X ch=%04X obj=%04X w0=%04X w2=%04X w4=%04X w6=%04X w1c=%04X d40e=%04X d4c0=%02X d0f4=%04X d414=%08X d408=%08X loop=%04X %04X hole=%04X %04X\n",
			hw_->Read16(0x6DFCu), hw_->Read16(0x6DFEu), hw_->Read16(0x6E00u), hw_->Read16(0x6E02u),
			0x5E5Cu + (songCode_ & 0xffu) * 0x28u, obj,
			obj ? hw_->Read16(obj) : 0u, obj ? hw_->Read16(obj + 2u) : 0u,
			obj ? hw_->Read16(obj + 4u) : 0u, obj ? hw_->Read16(obj + 6u) : 0u,
			obj ? hw_->Read16(obj + 0x1Cu) : 0u,
			hw_->Read16(0xD40Eu),
			hw_->Read8(0xD4C0u), hw_->Read16(0xD0F4u), hw_->Read32(0xD414u), hw_->Read32(0xD408u),
			hw_->Read16(0xC14A74u), hw_->Read16(0xC14A76u), hw_->Read16(0xC14A78u), hw_->Read16(0xC14A7Cu));
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
			for (int v = 0; v < 4; v++) {
				chip->Write(0x0f, (uint32_t)v);
				fprintf(log, " %d:cr=%04X fc=%04X st=%04X%04X en=%04X%04X k2=%04X k1=%04X lv=%04X rv=%04X ac=%04X%04X",
					v,
					CEmuChipEs5505Read(chip, 0x00),
					CEmuChipEs5505Read(chip, 0x01),
					CEmuChipEs5505Read(chip, 0x02),
					CEmuChipEs5505Read(chip, 0x03),
					CEmuChipEs5505Read(chip, 0x04),
					CEmuChipEs5505Read(chip, 0x05),
					CEmuChipEs5505Read(chip, 0x06),
					CEmuChipEs5505Read(chip, 0x07),
					CEmuChipEs5505Read(chip, 0x08),
					CEmuChipEs5505Read(chip, 0x09),
					CEmuChipEs5505Read(chip, 0x0a),
					CEmuChipEs5505Read(chip, 0x0b));
			}
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
		hw_->TickDuart(slice);
		if (hw_->DuartIrqPending())
			m68k_set_irq(M68K_IRQ_6);
		else
			m68k_set_irq(M68K_IRQ_NONE);
		const int ran = m68k_execute(slice);
		{
			/* STOP で IPL が DUART をマスクしている CPU だけ起こす。ユーザモード SR は書き換えない。 */
			const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
			const unsigned ipl = (sr >> 8) & 7u;
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
			const int stuck = (ran == slice);
			/* C14884–C14A10 を IPL7 のまま走らせる。ここで落とすと IRQ6 がネストして C14A6C で止まり live ボイスが無音になる。 */
			/* C14884–C14A10 を IPL7 のまま走らせる。C14A6C の正ワード待ちは yield させる。gunlock シーケンサは +0xB0 の C14AC0、キーオンは C15360。 */
			const int inSeq = f3Arabianm_
				? ((pc >= 0xC14884u && pc < 0xC14A60u) ? 1 : 0)
				: ((pc >= 0xC14884u && pc < 0xC15480u) ? 1 : 0);
			const int inIrq = (!f3Arabianm_ && pc >= 0xC10E00u && pc < 0xC11080u) ? 1 : 0;
			if (ipl >= 6u && !inSeq && !inIrq && (stuck || ran < 256))
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
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		RunCycles(cyclesPerSample);
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
