#include "StdAfx.h"
#include "cemu_driver_pc88.h"
#include "../machine/cemu_hard_pc88.h"
#include "../chip/cemu_chip_opna.h"
#include "../z80/Ay_Cpu.h"
#include <string.h>
#include <stdlib.h>

enum {
	PC88_CPU_HZ = 4000000,
	PC88_OPN_CLOCK_HZ = 3993600,
	PC88_OPNA_CLOCK_HZ = 7987200,
	VEC_VRTC = 0x02,
	VEC_RTC = 0x04,
	VEC_SOUND = 0x08,
	RTC_HZ = 600,
	/* hoot の PC88 VSYNC は 60 Hz（tnmbox.cpp SetInterval(1/60)）。
	   旧 56.4 Hz は 24 kHz CRT 20 行で、use_vrtc 曲が約 6% 遅かった。
	   Timer B 駆動（mistyblue / lastarmg）はチップクロックのまま動かない。 */
	VRTC_HZ = 60,
	/* 実 PC-88 曲が曲中に無音で持つ最長はこれ未満。超えたら休みではなく停止。
	   プレーヤは数フレームごとに F-num をビブラート用に書き換えるので、レジスタが 2 秒完全静止なら演奏中ではない。 */
	WD_IDLE_MS = 2000
};

static unsigned s_vrtcIrqs = 0;
static unsigned s_soundIrqs = 0;

CDriverPc88::CDriverPc88()
	: hw_(NULL)
	, sampleRate_(44100)
	, hostRate_(44100)
	, cpuHz_(PC88_CPU_HZ)
	, opnHz_(PC88_OPN_CLOCK_HZ)
	, booted_(0)
	, triggered_(0)
	, forceEiBoot_(0)
	, nextRtc_(0)
	, nextVrtc_(0)
	, rtcPeriod_(0)
	, vrtcPeriod_(0)
	, opnResidual_(0)
	, cpuAcc_(0)
	, cpuCycleBudget_(0)
	, lead_(NULL)
	, leadCap_(0)
	, leadLen_(0)
	, leadPos_(0)
	, capturing_(0)
	, capAcc_(0)
	, wdSamples_(0)
	, wdLastActive_(0)
	, wdMotion_(0)
	, wdTimerFires_(0)
	, wdReplays_(0)
	, wdEverActive_(0)
	, wdArmedTick_(0)
	, replayPending_(0)
{
}

CDriverPc88::~CDriverPc88()
{
	Close();
}

/* ROM を載せ、ブートして曲を起動する */
int CDriverPc88::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardPc88*)hw;
	hostRate_ = hw_->SampleRate();
	sampleRate_ = hostRate_;
	opnHz_ = hw_->opnaMode ? PC88_OPNA_CLOCK_HZ : PC88_OPN_CLOCK_HZ;
	cpuHz_ = (hw_->cpuHz_ > 0) ? hw_->cpuHz_ : PC88_CPU_HZ;
	rtcPeriod_ = (uint64_t)cpuHz_ / RTC_HZ;
	vrtcPeriod_ = (uint64_t)cpuHz_ / VRTC_HZ;
	nextRtc_ = rtcPeriod_;
	nextVrtc_ = vrtcPeriod_;
	opnResidual_ = 0;
	cpuAcc_ = 0;
	cpuCycleBudget_ = 0;
	leadLen_ = 0;
	leadPos_ = 0;
	capturing_ = 0;
	capAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	forceEiBoot_ = 0;
	wdSamples_ = 0;
	wdLastActive_ = 0;
	wdMotion_ = 0;
	wdTimerFires_ = 0;
	wdReplays_ = 0;
	wdEverActive_ = 0;
	wdArmedTick_ = 0;
	replayPending_ = 0;
	CEmuPc88IrqResetCount();
	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;
	/* ブート: PATCH がポーリングへ達するまで。上限約 1.0s。feris/gunyu は DRIVER に残すと page0 を壊す — I=01/F3 だけスナップショット。
	   JR 入口（ashe/andrgyns 等）はフル settle が要る。poll+iff1 の早期退出は DRIVER 初期化を切る。 */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		uint8_t* mem = hw_->Mem();
		const int jrEntry = mem && mem[0] == 0x18;
		uint8_t page0[0x80];
		int hadPoll = 0, pollAt = -1;
		if (mem) {
			memcpy(page0, mem, sizeof(page0));
			pollAt = hw_->CmdPollPc();
			hadPoll = pollAt >= 0;
		}
		const uint64_t chunk = (uint64_t)cpuHz_ / 32;
		for (int step = 0; step < 32 && cpu && mem; step++) {
			RunUntil((uint64_t)cpu->time64() + chunk);
			int nowPoll = hw_->CmdPollPc();
			if (hadPoll && nowPoll < 0) {
				/* Wing 系のみ（I=F3 + 音源 Cxxx + 高い CALL、または mugen3 I=01）。
				   素の I=F3 復元は pocky2 を誤爆し、半初期化シーケンサのままポーリングに残した（キーオン=0）。 */
				const uint16_t snd = Ay_CpuIm2Target(cpu, (uint8_t)VEC_SOUND);
				if (cpu->r.pc >= 0x80
					&& ((cpu->r.i == 0xF3 && snd >= 0xC000 && hw_->NeedsBootEiPulse())
						|| (cpu->r.i == 0x01 && cpu->r.pc < 0x1000))) {
					memcpy(mem, page0, sizeof(page0));
					if (pollAt >= 0)
						cpu->r.pc = (uint16_t)pollAt;
					cpu->r.iff1 = 1;
				}
				break;
			}
			/* JR 入口: ポーリングが生きたら止める。ashe（DRIVER@7800）だけフル約 1s settle — 早期退出は peak=0。
			   PATCH 内の poll バイトは静的。PC が poll より前なら止めない（lizard88/gineiden/gallforc 復号）。
			   iceclimb88: settle 中の VRTC がコマンドハンドラへ入り（pc が FE/CP を過ぎる）、そこで止めると B816=FF のまま。 */
			if (nowPoll >= 0x80
				&& cpu->r.pc >= (unsigned)nowPoll
				&& cpu->r.pc < (unsigned)nowPoll + 8)
				break;
			if (nowPoll >= 0 && cpu->r.pc < 0x80) {
				if (jrEntry) {
					if (mem[0x7800] == 0xC3)
						continue; /* フル settle */
					if (mem[0x0100] == 0x31 && step < 24)
						continue;
					if ((int)cpu->r.pc < nowPoll)
						continue;
					/* PC が正確に IN A,(00) のときだけ — JR Z の disp FB をオペコード扱いしない */
					if ((int)cpu->r.pc != nowPoll)
						continue;
					break;
				}
				if (cpu->r.iff1)
					break;
			}
		}
		/* settle がコマンドハンドラ途中で終わったら poll 待ちへスナップバック */
		if (cpu && mem && pollAt >= 0x80
			&& cpu->r.pc > (unsigned)pollAt + 4
			&& cpu->r.pc < (unsigned)pollAt + 0x60) {
			cpu->r.pc = (uint16_t)pollAt;
			cpu->r.iff1 = 1;
			hw_->cmd = 0;
		} else if (cpu && mem && pollAt >= 0 && cpu->r.pc < 0x80
			&& (int)cpu->r.pc > pollAt) {
			cpu->r.pc = (uint16_t)pollAt;
			cpu->r.iff1 = 1;
			hw_->cmd = 0;
		}
	}
	hw_->FixupIm2AfterBoot();
	hw_->PruneDeadTickSources();
	/* lizard88: PATCH は復号後 A3DF に JP 00B2 を植える。cmd=1 の前に再アサートしないと A3B0 が 1 CALL で曲を消費する */
	if (hw_->NeedsLizardArm())
		hw_->ArmLizardOpnTimer();
	hw_->ArmPwmajan2();
	/* Wing destge/hadou 系: settle 後も DI。I=F3 + 音源ベクタ Cxxx + DI 下の高い CALL（NeedsBootEiPulse）。素の I=F3（pocky2）はマッチさせない。gunyu は PATCH に FB があり settle 終了時 iff1=1。scheme OPNA: PATCH@9000 / I=0x80 — NeedsBootEiPulse または IsSchemeOpna。 */
	/* Wing destge/hadou 系: settle 後も DI。I=F3 + 音源ベクタ Cxxx + DI 下の高い CALL（NeedsBootEiPulse）。素の I=F3（pocky2）はマッチさせない。gunyu は PATCH に FB があり settle 終了時 iff1=1。scheme OPNA: PATCH@9000 / I=0x80 — NeedsBootEiPulse または IsSchemeOpna。 */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		const uint16_t snd = cpu ? Ay_CpuIm2Target(cpu, (uint8_t)VEC_SOUND) : 0;
		const int schemeBootEi = hw_->IsSchemeOpna() && cpu
			&& cpu->r.i == 0x80 && cpu->r.pc >= 0x80;
		int didEiPulse = 0;
		if (cpu && !cpu->r.iff1 && cpu->r.im == 2
			&& (hw_->NeedsBootEiPulse()
				|| (snd != 0 && cpu->r.i == 0xF3 && snd >= 0xC000
					&& hw_->NeedsBootEiPulse())
				|| (snd != 0 && cpu->r.i == 0x01 && cpu->r.pc >= 0x200 && cpu->r.pc < 0x1000)
				|| schemeBootEi)) {
			/* INT2@8350: MUS2 が I:08 を植えなければ音源 IRQ をそこに向ける */
			if (schemeBootEi && snd == 0) {
				uint8_t* mem = hw_->Mem();
				if (mem && mem[0x8350] == 0xC3) {
					const uint16_t table = (uint16_t)(((uint16_t)cpu->r.i << 8) | (uint16_t)VEC_SOUND);
					mem[table] = 0x50;
					mem[table + 1] = 0x83;
				}
			}
			forceEiBoot_ = 1;
			didEiPulse = 1;
			const int pulseMax = hw_->IsSchemeOpna() ? 256 : 96;
			for (int pulse = 0; pulse < pulseMax; pulse++) {
				cpu->r.iff1 = 1;
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 32);
				if (cpu->r.pc < 0x80)
					break; /* PATCH poll @0 に到達 */
				if (hw_->IsSchemeOpna()
					&& cpu->r.pc >= 0x9000 && cpu->r.pc < 0x9080)
					break; /* scheme のポーリング @9000 */
			}
			forceEiBoot_ = 0;
		}
		/* スナップ: Wing は EI パルス後。scheme は常に 9000 poll へパーク */
		if ((didEiPulse && cpu && cpu->r.pc >= 0x80 && !hw_->IsSchemeOpna())
			|| (hw_->IsSchemeOpna() && cpu && cpu->r.pc >= 0xA000)) {
			uint8_t* mem = hw_->Mem();
			if (mem) {
				const int pollLo = hw_->IsSchemeOpna() ? 0x9000 : 0;
				const int pollHi = hw_->IsSchemeOpna() ? 0x9080 : 0x70;
				for (int i = pollLo; i + 4 < pollHi; i++) {
					if (mem[i] == 0xDB && mem[i + 1] == 0x00
						&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28) {
						cpu->r.pc = (uint16_t)i;
						cpu->r.iff1 = 1;
						if (hw_->IsSchemeOpna())
							cpu->r.sp = 0x0100;
						break;
					}
				}
			}
		}
	}
	/* Scheme: MUS2 の OPN ポートメールボックスはブート後も 32/44/46 のまま */
	if (hw_->IsSchemeOpna()) {
		uint8_t* mem = hw_->Mem();
		if (mem) {
			mem[0xf0bb] = 0x32;
			mem[0xf0bc] = 0x44;
			mem[0xf0bd] = 0x46;
		}
	}
	/* 最終パークは poll 待ち — settle/EI パルス中の VRTC が PC をコマンドディスパッチャに残す（iceclimb88 B816 が ROM FF）。安全なのは IN A,(00) 番地だけ。JR Z の disp（FB）にいると EI がオペコード実行されメールボックス植込を飛ばす。 */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		uint8_t* mem = hw_->Mem();
		if (cpu && mem && cpu->r.pc < 0x80) {
			for (int i = 0; i + 4 < 0x70; i++) {
				if (mem[i] == 0xDB && mem[i + 1] == 0x00
					&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28
					&& (int)cpu->r.pc != i) {
					cpu->r.pc = (uint16_t)i;
					hw_->cmd = 0;
					break;
				}
			}
		}
		/* lizard88 の (A572) は意図的に触らない。それを読む RET Z は A3A3、CALL A3B0 が既に曲を鳴らした A376 エピローグ。0 でもプレーヤは止まらない。ゲートするのは停止ルーチンで、0 の分岐が OPN 07-0E を書いてチップを mute する。CHardPc88::ArmLizardOpnTimer 参照。 */
	}
	booted_ = 1;
	return 1;
}

/* ハード参照を捨てる */
void CDriverPc88::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
	free(lead_);
	lead_ = NULL;
	leadCap_ = 0;
	leadLen_ = 0;
	leadPos_ = 0;
	capturing_ = 0;
}

/* 同一 zip の別曲をライブで切替する */
int CDriverPc88::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	hw_->titleCode_ = titleCode;
	triggered_ = 0;
	TriggerPlay();
	return 1;
}

/* OPN クロックを CPU 比で進める */
void CDriverPc88::TickOpn(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	opnResidual_ += cpuCycles * (uint64_t)opnHz_;
	const uint64_t opnTicks = opnResidual_ / (uint64_t)cpuHz_;
	opnResidual_ %= (uint64_t)cpuHz_;
	if (opnTicks)
		hw_->SoundChip()->AdvanceClocks(opnTicks);
}

/* 期限の IRQ／NMI を届ける */
void CDriverPc88::DeliverIrqs(uint64_t now)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	/* castle/castleex PROG2 曲武装は I=$1A、ベクタ $1A04/$1A08。I=$FF に強制しない — ISR ページが孤立し OPN が mute する。 */
	int vrtcDue = hw_->useVrtc && now >= nextVrtc_;
	int rtcDue = hw_->useRtc && now >= nextRtc_;
	int opnDue = chip && chip->Irq()
		&& (!hw_->soundIrqMasked || hw_->IgnoreSoundIrqMask());
	if (cpu->r.iff1 && cpu->r.im == 2) {
		if (vrtcDue && Ay_CpuIm2Target(cpu, VEC_VRTC) == 0) vrtcDue = 0;
		if (rtcDue && Ay_CpuIm2Target(cpu, VEC_RTC) == 0) rtcDue = 0;
		if (opnDue && Ay_CpuIm2Target(cpu, VEC_SOUND) == 0) opnDue = 0;
	}
	if (cpu->r.iff1 && (vrtcDue || rtcDue || opnDue)) {
		/* FM 音源 IRQ を VRTC/RTC より優先。KOEI OPN（valis2）の Timer B は VRTC に近く、常に VRTC を先に取るとベクタ 08 が飢えてテンポが半減。OPNA は Timer A が速いので影響が小さい。 */
		const uint8_t vector = opnDue ? (uint8_t)VEC_SOUND
			: vrtcDue ? (uint8_t)VEC_VRTC
			: (uint8_t)VEC_RTC;
		if (Ay_CpuIm2Interrupt(cpu, vector)) {
			if (vector == VEC_VRTC) {
				nextVrtc_ += vrtcPeriod_;
				/* SOUND 優先で溜まった VRTC をバーストで埋めない。hoot VSYNC
				   は 60 Hz グリッド。遅れ分を一気に吐くと tnmbox が暴れる。 */
				if (vrtcPeriod_ > 0 && now > nextVrtc_) {
					const uint64_t late = (now - nextVrtc_) / vrtcPeriod_;
					if (late)
						nextVrtc_ += late * vrtcPeriod_;
				}
				s_vrtcIrqs++;
			} else if (vector == VEC_RTC) {
				nextRtc_ += rtcPeriod_;
				if (rtcPeriod_ > 0 && now > nextRtc_) {
					const uint64_t late = (now - nextRtc_) / rtcPeriod_;
					if (late)
						nextRtc_ += late * rtcPeriod_;
				}
			} else if (vector == VEC_SOUND && chip) {
				/* hoot: ほぼ全 PC88 ドライバは raise_IRQ の直後に lower_IRQ（エッジ）。線を OUT E4 まで High に保つと EI 後に ISR 再入し mucom テンポが走る。YM ステータスは KOEI 用に sticky。E4 も ack。 */
				chip->AckIrq();
				s_soundIrqs++;
			}
		}
	} else {
		/* DI 中（ISR 内）は期限フラグを保持 — 予定を前へ滑らせない。IRQ が落ちる／遅れる。 */
		if (cpu->r.iff1) {
			if (rtcDue) nextRtc_ = now + rtcPeriod_;
			if (vrtcDue) nextVrtc_ = now + vrtcPeriod_;
		}
	}
}

/* TriggerPlay は曲をゲストへ渡し、PATCH コマンドドレインの間走らせる — 大半のリップは丸 1 秒。cmd はループ後にホストがクリアするので早期退出は稀。その間シーケンサは生きていて、カタログ 867 曲中 676 がこの窓でキーオンする。AdvanceClocks はタイマだけ（PCM は Render）。誰もサンプルしないチップへ鳴り、曲は約 1s から始まったように見えた。窓を捨てず合成し、ライブサンプルより先に渡す。

   無音先頭は EndLeadCapture で切る。キックが純初期化のリップ（1942_88 は 8s ドレインでノート無し）に無音イントロを足さない。 */
enum { LEAD_MAX_SECONDS = 16 };

/* CDriverPc88::BeginLeadCapture の実装 */
void CDriverPc88::BeginLeadCapture()
{
	if (!hw_ || !hw_->SoundChip() || hostRate_ < 1) return;
	capturing_ = 1;
	capAcc_ = 0;
}

/* CDriverPc88::CaptureLead の実装 */
void CDriverPc88::CaptureLead(uint64_t cpuCycles)
{
	if (!capturing_ || cpuCycles == 0) return;
	CChip* chip = hw_ ? hw_->SoundChip() : NULL;
	if (!chip) return;
	capAcc_ += (int64_t)cpuCycles * (int64_t)hostRate_;
	while (capAcc_ >= (int64_t)cpuHz_) {
		capAcc_ -= (int64_t)cpuHz_;
		if (leadLen_ + 2 > leadCap_) {
			const int limit = hostRate_ * 2 * LEAD_MAX_SECONDS;
			if (leadCap_ >= limit) {
				capturing_ = 0; /* 異常に長いドレイン: 成長を止める */
				return;
			}
			int want = leadCap_ ? leadCap_ * 2 : hostRate_ * 2 / 4;
			if (want > limit) want = limit;
			int16_t* grown = (int16_t*)realloc(lead_, (size_t)want * sizeof(int16_t));
			if (!grown) {
				capturing_ = 0;
				return;
			}
			lead_ = grown;
			leadCap_ = want;
		}
		chip->Render(lead_ + leadLen_, 1);
		leadLen_ += 2;
	}
}

/* 直近キックが拾ったオープニング（先頭欠落プローブ用）。ウォッチドッグカウンタと同じく、プローブは同時 1 曲なのでグローバル。 */
static unsigned s_leadFrames = 0;
static unsigned s_leadTrimmed = 0;
unsigned CEmuPc88LeadFrames() { return s_leadFrames; }
unsigned CEmuPc88LeadTrimmedFrames() { return s_leadTrimmed; }

/* CDriverPc88::EndLeadCapture の実装 */
void CDriverPc88::EndLeadCapture()
{
	capturing_ = 0;
	s_leadFrames = (unsigned)(leadLen_ / 2);
	s_leadTrimmed = 0;
	if (leadLen_ <= 0) return;
	/* ブロック内 p2p。|sample| ではない。SSG が音量だけ残しミキサ OFF だと聞こえない DC が 0 にならず、p2p はその台地を無音と読む。オフセットが段差になるブロックは大きいので、リップは一瞬の静かなリードを残せる — そちらへ誤るのが意図。持続レベル必須にすると navitune の数 ms オープニングクリック（それだけが出力）が消えた。 */
	const int block = 512 * 2;
	const int thr = 96;
	int firstLoud = -1;
	for (int base = leadPos_; base < leadLen_; base += block) {
		int hi = -32768, lo = 32767;
		const int end = (base + block < leadLen_) ? base + block : leadLen_;
		for (int i = base; i < end; i++) {
			const int v = lead_[i];
			if (v > hi) hi = v;
			if (v < lo) lo = v;
		}
		if ((hi - lo) / 2 > thr) {
			firstLoud = base;
			break;
		}
	}
	if (firstLoud < 0) {
		s_leadTrimmed = (unsigned)((leadLen_ - leadPos_) / 2);
		leadLen_ = leadPos_; /* 初期化無音だけ */
		return;
	}
	s_leadTrimmed = (unsigned)((firstLoud - leadPos_) / 2);
	leadPos_ = firstLoud;
}

/* CDriverPc88::DrainLead の実装 */
int CDriverPc88::DrainLead(int16_t* stereo, int frames)
{
	if (leadPos_ >= leadLen_) {
		if (leadLen_) {
			leadLen_ = 0;
			leadPos_ = 0;
		}
		return 0;
	}
	int n = (leadLen_ - leadPos_) / 2;
	if (n > frames) n = frames;
	memcpy(stereo, lead_ + leadPos_, (size_t)n * 2 * sizeof(int16_t));
	leadPos_ += n * 2;
	if (leadPos_ >= leadLen_) {
		leadLen_ = 0;
		leadPos_ = 0;
	}
	return n;
}

/* CPU を endCycle まで進める */
void CDriverPc88::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardPc88SetActive(hw_);
	while ((uint64_t)cpu->time64() < endCycle) {
		if (forceEiBoot_ && !cpu->r.iff1 && cpu->r.im == 2)
			cpu->r.iff1 = 1;
		hw_->GuardHardrankPc();
		const uint64_t now = (uint64_t)cpu->time64();
		DeliverIrqs(now);
		/* tf88sr PATCH play は RTC 待ちで HALT。起こさず進めると Ay_Cpu HALT は残りスライスを燃やすだけ同じ PC を再試行し、HALT 後の play CALL が走らない。 */
		uint8_t* mem = hw_->Mem();
		if (mem && mem[cpu->r.pc] == 0x76) {
			uint64_t wake = endCycle;
			if (hw_->useRtc && rtcPeriod_ > 0 && nextRtc_ > now && nextRtc_ < wake)
				wake = nextRtc_;
			if (hw_->useVrtc && vrtcPeriod_ > 0 && nextVrtc_ > now && nextVrtc_ < wake)
				wake = nextVrtc_;
			uint64_t delta = (wake > now) ? (wake - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->adjust_time((int)delta);
			hw_->AddCpuCycles(delta);
			TickOpn(delta);
			CaptureLead(delta);
			continue;
		}
		const int cycles = Ay_CpuRunOne(cpu);
		hw_->AddCpuCycles((uint64_t)cycles);
		TickOpn((uint64_t)cycles);
		CaptureLead((uint64_t)cycles);
	}
}

/* PATCH コマンドポーリング — page0 stub の `IN A,(00) / OR A / JR Z,-` */
int CDriverPc88::FindPollLoop() const
{
	return hw_ ? hw_->CmdPollPc() : -1;
}

/* 再キックはゲストがその poll にいるときだけ着地する。止まったリップが外へ迷った（暴走 PC、抜けないアイドル）ときは、先に使えるスタックで poll へ戻す。 */
void CDriverPc88::Unwedge()
{
	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;
	if (!cpu) return;
	const int pollAt = FindPollLoop();
	if (pollAt < 0) return;
	/* 既に page0 stub、または Falcom E027 poll にいる */
	if (pollAt < 0x80 && cpu->r.pc < 0x80) return;
	if (pollAt >= 0x80 && (int)cpu->r.pc >= pollAt && (int)cpu->r.pc < pollAt + 8)
		return;
	if (hw_->SkipUnwedge())
		return;
	cpu->r.pc = (uint16_t)pollAt;
	if (cpu->r.sp < 0x0200 || cpu->r.sp >= 0xF000)
		cpu->r.sp = 0x0200;
	cpu->r.iff1 = 1;
	hw_->cmd = 0;
}

/* 曲開始。Render から分離し、停滞ウォッチドッグが初回と同じキックを再発行できるようにする */
void CDriverPc88::TriggerPlay()
{
	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;
	if (!cpu) return;
	CEmuHardPc88SetActive(hw_);
	if (wdReplays_ > 0)
		Unwedge();
	if (!triggered_) {
		BeginLeadCapture();
		/* ブートが潰した mdata/vdata を再載せ — ただし mdata が PATCH/スタックページ上ならしない。パックバンクオフセットが残るよう完全 titleCode_ を使う。 */
		if (hw_->ShouldRestageSong())
			hw_->LoadSongData(hw_->titleCode_);
		hw_->ApplyFalcomPlay();
		/* KOEI FMDRV: BGM は再生添字 0（パック CIM @4000）。PCM SE タイトル（valis2 PCM00.. = code>=0xE0）は生コードを渡し PATCH の CP E0 経路を走らせる — 0 強制は ADPCM を mute し UI 下のステータス待ちで回った。 */
		if (hw_->PackedKoei()) {
			hw_->song = hw_->PlaySongIndex();
			hw_->param = hw_->PlayParamIndex();
		} else {
			/* LoadRoms の再生添字規則に合わせる（ポインタ表バンク → 上位バイト） */
			hw_->song = hw_->PlaySongIndex();
			hw_->param = hw_->PlayParamIndex();
		}
		if (hw_->PlayKickBase()) {
			/* Game Arts: PATCH ポート再生は CALL +6 init。ホストがその後プレーヤ基点（ISR 入口）を CALL。castle/castleex: cmd=1 約 64 ホストサンプル（CALL 1033 武装）のあと CALL PROG2@1000。 */
			const unsigned base = hw_->PlayKickBase();
			/* N88 thexder/bokosuka: キック前の cmd=1 は PATCH ポート再生が CALL stop（E80E）／N88 へ迷い F304 を壊す。キックのみ。 */
			if (hw_->NeedsDeferredRtc() && !hw_->PlayKickInitOff()) {
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, hw_->PlayKickEi());
				/* DEMOM/MUSIC 再生入口が PATCH poll へ RET するまで待ち、最初の RTC tick 前にチャネル／ボイス初期化を終える */
				for (int step = 0; step < 64; step++) {
					RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
					if (cpu->r.pc < 0x80)
						break;
				}
				hw_->EnableDeferredRtc();
				if (hw_->NeedsPlayEi() && !cpu->r.iff1)
					cpu->r.iff1 = 1;
			} else if (base >= 0x40 && base < 0x100 && !hw_->PlayKickInitOff()) {
				/* robowr88 曲 1: page0 トランポリン CALL CB5A / JP CB48。cmd=1 はまだ CALL $005B → PROG1 BA41（PROG2 載せ後はゼロ）。キックのみ。PC が poll（トランポリン $C0 より下）に戻るまで待ち、RTC は許可しない。 */
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, hw_->PlayKickEi());
				for (int step = 0; step < 64; step++) {
					RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
					if (cpu->r.pc < 0x40)
						break;
				}
				if (hw_->NeedsPlayEi() && !cpu->r.iff1)
					cpu->r.iff1 = 1;
			} else if (base == 0x1000) {
				/* castle/castleex: PROG2@1000 初期化のあと PATCH cmd=1 で曲武装。105D は CALL wipe で終わる（LD SP,$FE80 + PUSH MUSIC@F800）。castle: CALL 1374 / ISR 154E / tick 1669 / enable 14F4。castleex PROG2 は再配置: CALL 12DE / ISR 14B8 / tick 15D3 / enable 145E — ハードコード RAM ではなくオペコードで合わせる。 */
				uint8_t* mem = hw_->Mem();
				if (mem && mem[0x1082] == 0xCD) {
					const unsigned tgt = (unsigned)mem[0x1083]
						| ((unsigned)mem[0x1084] << 8);
					int wipe = 0;
					if (tgt + 48u < 0x10000u) {
						for (unsigned k = 0; k < 40; k++) {
							if (mem[tgt + k] == 0x31
								&& mem[tgt + k + 1] == 0x80
								&& mem[tgt + k + 2] == 0xFE) {
								wipe = 1;
								break;
							}
						}
					}
					if (wipe) {
						mem[0x1082] = 0x00;
						mem[0x1083] = 0x00;
						mem[0x1084] = 0x00;
					}
				}
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, 0);
				for (int step = 0; step < 32; step++) {
					RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
					if (cpu->r.pc < 0x80)
						break;
				}
				hw_->cmd = 1;
				for (int step = 0; step < 128; step++) {
					RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
					if (step >= 8 && cpu->r.pc < 0x80 && hw_->cmd == 0)
						break;
				}
				hw_->cmd = 0;
				unsigned isr = 0, tick = 0, enable = 0;
				if (mem) {
					for (unsigned a = 0x1400; a + 8u < 0x1800u; a++) {
						if (mem[a] == 0xF3 && mem[a + 1] == 0xF5
							&& mem[a + 2] == 0x3A && mem[a + 5] == 0x3D
							&& mem[a + 6] == 0x20) {
							isr = a;
							tick = (unsigned)mem[a + 3]
								| ((unsigned)mem[a + 4] << 8);
							break;
						}
					}
					for (unsigned a = 0x1400; a + 7u < 0x1600u; a++) {
						if (mem[a] == 0x3A && mem[a + 3] == 0xF6
							&& mem[a + 4] == 0x01 && mem[a + 5] == 0xD3
							&& mem[a + 6] == 0xE6) {
							enable = (unsigned)mem[a + 1]
								| ((unsigned)mem[a + 2] << 8);
							break;
						}
					}
				}
				if (mem && isr) {
					const uint8_t ip = (cpu->r.i != 0) ? cpu->r.i : 0x1a;
					cpu->r.i = ip;
					const unsigned v4 = ((unsigned)ip << 8) | 0x04u;
					const unsigned v8 = ((unsigned)ip << 8) | 0x08u;
					mem[v4] = (uint8_t)(isr & 0xff);
					mem[v4 + 1] = (uint8_t)(isr >> 8);
					mem[v8] = (uint8_t)((isr + 1u) & 0xff);
					mem[v8 + 1] = (uint8_t)((isr + 1u) >> 8);
					if (tick < 0x10000u && mem[tick] == 0 && hw_->param)
						mem[tick] = 1;
				}
				if (mem && enable && enable < 0x10000u)
					mem[enable] = (uint8_t)(mem[enable] | 0x01);
				cpu->r.iff1 = 1;
			} else if (!hw_->PlayKickInitOff()
				&& base >= 0xb000 && base < 0xe000) {
				/* yokosuka SOUND@B5C3: PATCH cmd=1（param!=FF）は DI; CALL SOUND+0x1BD して poll へ戻る。ホスト DirectPlayKick で同じ入口を短い cmd RunUntil にネストすると FM が mute。PATCH に CALL を終えさせ、その後 RTC 用に EI。効果音: param=FF → (E23C)。 */
				hw_->cmd = 1;
				for (int step = 0; step < 256; step++) {
					RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
					if (step >= 4 && cpu->r.pc < 0x80 && hw_->cmd == 0)
						break;
				}
				hw_->cmd = 0;
				if (hw_->PlayKickEi() && !cpu->r.iff1)
					cpu->r.iff1 = 1;
			} else {
				hw_->cmd = 1;
				if (hw_->PlayKickInitOff()) {
					for (int step = 0; step < 16; step++) {
						RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
						if (step >= 2 && cpu->r.pc < 0x80)
							break;
					}
				} else {
					/* プローブ合わせ: cpuHz/hostRate で 64 ホストサンプル */
					RunUntil((uint64_t)cpu->time64()
						+ (uint64_t)cpuHz_ * 64u / (uint64_t)(hostRate_ > 0 ? hostRate_ : 44100));
				}
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, hw_->PlayKickEi());
			}
		} else {
			/* cmd=1 消費中は DI。hangon88/iceclimb は EI 下で poll。IN A,(01) と LD (mailbox),A の間の RTC/VRTC が A を壊す（0x8F→0x80）かストアを飛ばす（B816 が ROM FF）。B もクリアし、ED 49 OUT (C),C で C=0 がポート 0 を叩くようにする。 */
			const int wantEi = cpu->r.iff1 || hw_->NeedsPlayEi();
			uint8_t* mem = hw_->Mem();
			/* yaksa PATCH2 は DI + use_vrtc で poll。play CALL には EI が要る。NeedsPlayEi() をキーにしない — forcePlayEi タイトル（と use_vrtc の makai）は cmd を DI 下でドレインしないと VRTC がハンドラにネストし ISR 途中でパークする。 */
			const int keepEiForVrtcLoad = hw_->useVrtc && !cpu->r.iff1
				&& mem && mem[0] == 0x18;
			if (!keepEiForVrtcLoad)
				cpu->r.iff1 = 0;
			cpu->r.b.b = 0;
			hw_->cmd = 1;
			if (hw_->IsSchemeOpna()) {
				hw_->SchemePlayTrigger(hw_->titleCode_);
				Ay_Cpu* cpu2 = hw_->Cpu();
				if (cpu2) {
					cpu2->r.iff1 = 1;
					cpu2->r.sp = 0x0100;
				}
			}
			/* spitfl88 のみ: PATCH に A6A9 を武装させ、A824 を再アサート（ブート CALL A826 は mid-RAM 79D7 < 0x34 のときクリア）。Game Arts キック（jikochu*）では走らせない — A6A9/A824 が音楽と衝突する。 */
			if (mem && hw_->useRtc && mem[0xA826] == 0xF3
				&& mem[0xA830] == 0xFE && mem[0xA831] == 0x34) {
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 32);
				if (mem[0xA6A9] == 0x20 && mem[0xA824] == 0)
					mem[0xA824] = 1;
			}
			/* gineiden: PATCH play に vec08／曲ロードを植えさせ、CALL 4E2F がクリアした Timer B を再武装 */
			if (hw_->NeedsGineidenArm()) {
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 8);
				hw_->ArmGineidenOpnTimer();
			}
			if (hw_->NeedsLizardArm()) {
				hw_->ArmLizardOpnTimer();
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 8);
				hw_->ArmLizardOpnTimer();
			}
			hw_->ArmPwmajan2();
			hw_->ArmYaksaPlay();
			if (hw_->NeedsNavituneArm()) {
				/* cmd=1 LDIR/cmd10 の前にリストポインタを 7700 へ。SP=$0200 は $01E0 の植込を潰す。navimus より下へパーク。 */
				if (cpu->r.sp < 0x4000 || cpu->r.sp >= 0x7700)
					cpu->r.sp = 0x7000;
				hw_->ApplyNavituneTitleSong();
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 4);
				hw_->ApplyNavituneTitleSong();
				hw_->FinishNavitunePlay();
			}
			if (hw_->NeedsYakyufanArm()) {
				/* PATCH cmd=1 が CALL play に達するまで（0C5D 経由で 0118 をクリア） */
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 8);
				hw_->ArmYakyufanPlay();
			}
			/* サンプルループが IRQ を再許可する前に DI 下で cmd をドレイン */
			const int pollAt = FindPollLoop();
			int sawDispatch = (pollAt < 0);
			const int drainSteps = hw_->NeedsLongPlayDrain() ? 512 : 64;
			for (int step = 0; step < drainSteps; step++) {
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 64);
				const int pc = (int)cpu->r.pc;
				int atPoll = 0;
				if (pollAt >= 0x80)
					atPoll = (pc >= pollAt && pc < pollAt + 8);
				else if (pollAt >= 0)
					atPoll = (pc < 0x80 && pc <= pollAt + 4);
				if (pollAt >= 0x80) {
					if (!atPoll)
						sawDispatch = 1;
				} else if (pollAt >= 0 && pc < 0x80 && pc > pollAt + 4)
					sawDispatch = 1;
				/* 1942 ADEE は 0034（まだ <0x80）。page0 PC なら何でも中断すると LDIR 途中で止まり、A343 が I+Timer を武装する前に終わる。 */
				if (hw_->NeedsLongPlayDrain()) {
					if (step >= 8 && hw_->cmd == 0 && pollAt >= 0
						&& pc == pollAt && sawDispatch)
						break;
					continue;
				}
				if (step >= 2 && hw_->cmd == 0 && atPoll && sawDispatch)
					break;
			}
			hw_->cmd = 0;
			hw_->FixupIm2AfterPlay();
			hw_->ArmYaksaPlay();
			if (hw_->NeedsLongPlayDrain() && hw_->SoundChip() && cpu) {
				/* 1942 A343 はモード 2A で終わる。Timer B を生かし port32 をアンマスクし、AD92 が FM をシーケンスできるようにする。 */
				hw_->PortOut(0x44, 0x26);
				hw_->PortOut(0x45, 0xCF);
				hw_->PortOut(0x44, 0x27);
				hw_->PortOut(0x45, 0x2A);
				hw_->PortOut(0x32, (uint8_t)(hw_->PortIn(0x32) & 0x7F));
				cpu->r.iff1 = 1;
			}
			if (hw_->NeedsDeferredRtc()) {
				RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 8);
				hw_->EnableDeferredRtc();
			}
			if (wantEi)
				cpu->r.iff1 = 1;
		}
		EndLeadCapture();
		triggered_ = 1;
	}
}

static unsigned s_wdReplayCount = 0;
static int s_wdEnabled = 1;

unsigned CEmuPc88WatchdogReplays() { return s_wdReplayCount; }
void CEmuPc88WatchdogResetCount() { s_wdReplayCount = 0; }
void CEmuPc88WatchdogSetEnabled(int on) { s_wdEnabled = on ? 1 : 0; }

unsigned CEmuPc88VrtcIrqs() { return s_vrtcIrqs; }
unsigned CEmuPc88SoundIrqs() { return s_soundIrqs; }
void CEmuPc88IrqResetCount() { s_vrtcIrqs = 0; s_soundIrqs = 0; }

/* プレーヤが走る手段を残さず終わったブート用の停滞ウォッチドッグ: ゲストが割り込みを落とした、音源 IRQ をマスクした、tick 源が無い。生存信号はキーオン＋F-num＋SSG 周期変化。アイドルのレジスタポーリングは演奏ではない。ノートが出た瞬間に武装解除。

   一度鳴った曲は再起動しない。生きたプレーヤへ play キックを重ねると RAM 途中状態から再開し、数秒ごとに壊れた半再起動になる。本物の欠陥（yokosuka: ポート 70h テキスト窓未実装でシーケンサが別ページから音価を読み 1 秒で死ぬ）が悪いループに聞こえる。きれいな再起動は ROM 再読が要るが Open 後 zip は閉じているので、終わった曲は無音のまま、止めた原因を直す。 */
void CDriverPc88::WatchdogTick()
{
	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;
	CChip* chip = hw_ ? hw_->SoundChip() : NULL;
	if (!cpu || !chip || sampleRate_ < 1 || !s_wdEnabled) return;

	unsigned w = 0, k = 0, f = 0, s = 0, m = 0;
	CEmuChipYm2608GetPlayMetrics(chip, &w, &k, &f, &s, &m);
	const unsigned motion = k + f + s;
	if (motion != wdMotion_) {
		wdMotion_ = motion;
		wdLastActive_ = wdSamples_;
		wdEverActive_ = 1;
		return;
	}
	const uint64_t idleLimit = (uint64_t)sampleRate_ * WD_IDLE_MS / 1000u;
	if (wdSamples_ - wdLastActive_ < idleLimit)
		return;
	wdLastActive_ = wdSamples_;

	/* 安く冪等なので毎回両方かける。ループ間ギャップは聞こえる。タイムアウトを使うより安い。 */
	const int haveVec = Ay_CpuIm2Target(cpu, VEC_SOUND)
		|| Ay_CpuIm2Target(cpu, VEC_VRTC) || Ay_CpuIm2Target(cpu, VEC_RTC);
	if (!cpu->r.iff1 && cpu->r.im == 2 && haveVec)
		cpu->r.iff1 = 1;
	if (hw_->soundIrqMasked && Ay_CpuIm2Target(cpu, VEC_SOUND))
		hw_->PortOut(0x32, (uint8_t)(hw_->PortIn(0x32) & 0x7F));

	unsigned fa = 0, fb = 0, ip = 0;
	CEmuChipYm2608GetTimerDebug(chip, &fa, &fb, &ip);
	const int ticking = (fa + fb) != wdTimerFires_;
	wdTimerFires_ = fa + fb;
	/* 一部リップ（mappy88, jikochu*）は play コールがオープニングノートをインラインで書き、tick を組まない。シーケンサは 1 回進んで止まる。インストール済みベクタにクロックを 1 回だけ与える。ブートが全源を死なせたときのみ。 */
	if (!wdArmedTick_ && !ticking && !hw_->useVrtc && !hw_->useRtc) {
		wdArmedTick_ = 1;
		if (Ay_CpuIm2Target(cpu, VEC_SOUND)) {
			hw_->ArmFallbackOpnTimer();
			return;
		}
		if (Ay_CpuIm2Target(cpu, VEC_VRTC)) {
			hw_->useVrtc = 1;
			nextVrtc_ = (uint64_t)cpu->time64() + vrtcPeriod_;
			return;
		}
		if (Ay_CpuIm2Target(cpu, VEC_RTC)) {
			hw_->useRtc = 1;
			nextRtc_ = (uint64_t)cpu->time64() + rtcPeriod_;
			return;
		}
	}
	/* 最後の手段、かつ一度もノートが出ていないときだけ: キックがブートと競った可能性。何か鳴ったら干渉を止める。gra88 / gallforc は初ノートまで DRIVER 初期化約 3s — そのリードインを停滞と数えない。 */
	if (wdEverActive_ || wdReplays_ >= 4)
		return;
	if (!wdEverActive_) {
		const uint64_t bootGrace = (uint64_t)sampleRate_ * 4000u / 1000u;
		if (wdSamples_ < bootGrace)
			return;
	}
	wdReplays_++;
	s_wdReplayCount++;
	replayPending_ = 1;
}

/* CPU とチップを進めステレオ PCM を合成する */
int CDriverPc88::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardPc88SetActive(hw_);
	if (hw_->NeedsN88RtcGuard())
		hw_->GuardN88RtcVector();
	if (replayPending_) {
		replayPending_ = 0;
		triggered_ = 0;
	}
	if (!triggered_)
		TriggerPlay();
	if (hostRate_ < 1 || cpuHz_ < 1) return 0;
	/* play キックが既に出したオープニング小節（BeginLeadCapture） */
	const int lead = DrainLead(stereo, frames);
	for (int i = lead; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		/* 命令の超過を次サンプルへ持ち越す。無いと各サンプルが予算を約半命令（Z80/OPN クロック約 5–6% 増）超え、サントラテンポが速くなる。 */
		cpuCycleBudget_ += (int64_t)cyclesPerSample;
		hw_->GuardHardrankPc();
		while (cpuCycleBudget_ > 0) {
			const uint64_t now = (uint64_t)cpu->time64();
			DeliverIrqs(now);
			uint8_t* mem = hw_->Mem();
			if (mem && mem[cpu->r.pc] == 0x76) {
				uint64_t wake = now + (uint64_t)cpuCycleBudget_;
				if (hw_->useRtc && rtcPeriod_ > 0 && nextRtc_ > now && nextRtc_ < wake)
					wake = nextRtc_;
				if (hw_->useVrtc && vrtcPeriod_ > 0 && nextVrtc_ > now && nextVrtc_ < wake)
					wake = nextVrtc_;
				uint64_t delta = (wake > now) ? (wake - now) : 4;
				if (delta < 4) delta = 4;
				if (delta > (uint64_t)cpuCycleBudget_)
					delta = (uint64_t)cpuCycleBudget_;
				cpu->adjust_time((int)delta);
				cpuCycleBudget_ -= (int64_t)delta;
				hw_->AddCpuCycles(delta);
				TickOpn(delta);
				continue;
			}
			const int cycles = Ay_CpuRunOne(cpu);
			cpuCycleBudget_ -= (int64_t)cycles;
			hw_->AddCpuCycles((uint64_t)cycles);
			TickOpn((uint64_t)cycles);
		}
		/* schwarz FE19 / Falcom E000 PATCH: play 周りを DI し、対応する EI が無い */
		if (hw_->NeedsPlayEi() && !cpu->r.iff1)
			cpu->r.iff1 = 1;
		if (hw_->NeedsN88RtcGuard() && (i & 63) == 0)
			hw_->GuardN88RtcVector();
		chip->Render(stereo + i * 2, 1);
		if ((++wdSamples_ & 511) == 0) {
			WatchdogTick();
			/* その場で再キック。次 Render() へ先送りすると、そのコールのバッファ全体がループ間ギャップに足される。 */
			if (replayPending_) {
				replayPending_ = 0;
				triggered_ = 0;
				TriggerPlay();
			}
		}
	}
	/* FmMon AddSamples/Flush は readcemu 側のみ（二重だと curSample が 2 倍進み同期が崩れる） */
	return frames;
}

/* Seek は未対応 */
int CDriverPc88::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
