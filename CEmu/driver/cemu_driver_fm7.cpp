#include "StdAfx.h"
#include "cemu_driver_fm7.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_ay.h"
#include <string.h>

/* FM-7: M6809 + AY/YM2203。vsync とチップタイマ */
CDriverFm7::CDriverFm7()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(2000000)
	, opnHz_(1228800)
	, ayHz_(1228800)
	, booted_(0)
	, triggered_(0)
	, songCode_(0)
	, titleCode_(0)
	, opnResidual_(0)
	, opnTimerMul_(1)
	, opnTimerDiv_(1)
	, ayResidual_(0)
	, cpuAcc_(0)
	, nextVsync_(0)
	, vsyncPeriod_(2000000 / 60)
	, irqPulses_(0)
	, prevChipIrq_(0)
	, chipIrqSeen_(0)
	, lastFd03IrqVec_(0xFFFF)
{
}

/* 後始末 */
CDriverFm7::~CDriverFm7()
{
	Close();
}

/* OPN 書込回数 */
unsigned CDriverFm7::OpnWrites() const
{
	return hw_ ? hw_->OpnWrites() : 0;
}

/* AY 書込回数 */
unsigned CDriverFm7::AyWrites() const
{
	return hw_ ? hw_->AyWrites() : 0;
}

/* OPN/AY クロックを CPU 比で進める */
void CDriverFm7::TickChips(uint64_t cpuCycles)
{
	if (!hw_ || cpuCycles == 0) return;
	if (hw_->ChipOpn()) {
		const uint64_t den = (uint64_t)cpuHz_ * (uint64_t)opnTimerDiv_;
		opnResidual_ += cpuCycles * (uint64_t)opnHz_ * (uint64_t)opnTimerMul_;
		const uint64_t ticks = opnResidual_ / den;
		opnResidual_ %= den;
		if (ticks)
			hw_->ChipOpn()->AdvanceClocks(ticks);
	}
	if (hw_->ChipAy()) {
		ayResidual_ += cpuCycles * (uint64_t)ayHz_;
		const uint64_t ticks = ayResidual_ / (uint64_t)cpuHz_;
		ayResidual_ %= (uint64_t)cpuHz_;
		if (ticks)
			hw_->ChipAy()->AdvanceClocks(ticks);
	}
}

/* vsync とチップ IRQ を 1 線で届ける */
void CDriverFm7::DeliverIrqs(uint64_t now)
{
	if (!hw_) return;
	mc6809__t* cpu = hw_->Mc6809();
	if (!cpu) return;

	int vsyncDue = (triggered_ && vsyncPeriod_ > 0 && now >= nextVsync_) ? 1 : 0;
	int chipIrq = 0;
	if (hw_->ChipOpn() && hw_->ChipOpn()->Irq())
		chipIrq = 1;
	if (chipIrq)
		hw_->ymIrqSeen_ = 1;

	/* ISR から極性を嗅ぐ: laydock は bit2、reviver は bit0+bit3、既定は bit3 クリア。
	   play 後 $FFF8 再マウントで再嗅ぎ（asteka2 PATCH: $20D1 stub → $A1C9）。 */
	const uint16_t irqNow = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF8] << 8) | hw_->Mem()[0xFFF9]);
	if (irqNow != lastFd03IrqVec_) {
		lastFd03IrqVec_ = irqNow;
		hw_->RefreshFd03Polarity();
	}
	if (vsyncDue)
		hw_->ApplyFd03Vsync();

	/* 音楽 IRQ は 1 源だけ: OPN タイトルはチップタイマ端（vsync+タイマ二重は ys2_fmav が超速）。
	   PSG タイトルは vsync のみ。$FD03 ステータスは上で ApplyFd03Vsync。 */
	int chipIrqEdge = (chipIrq && !prevChipIrq_) ? 1 : 0;
	prevChipIrq_ = chipIrq;

	auto isFd03Stub = [&](uint16_t vec) -> int {
		if (vec == 0 || vec == 0xFFFF) return 0;
		const uint8_t* p = hw_->Mem() + vec;
		if (p[0] == 0xB6 && p[1] == 0xFD && p[2] == 0x03) return 1;
		if (p[0] == 0x96 && p[1] == 0x03) return 1;
		return 0;
	};
	const uint16_t irqVec = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF8] << 8) | hw_->Mem()[0xFFF9]);
	const uint16_t firqVec = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF6] << 8) | hw_->Mem()[0xFFF7]);
	const int irqStub = isFd03Stub(irqVec);
	const int firqStub = isFd03Stub(firqVec);
	/* kohaku PATCH $103F と albatrss PATCH $005D は単独 RTI。IRQ 中に FIRQ すると I セット・F クリアで CC+PC だけ引きフレームを壊す。 */
	const int firqIsRti = (firqVec != 0 && firqVec != 0xFFFF && firqVec < 0xFE00
		&& hw_->Mem()[firqVec] == 0x3B) ? 1 : 0;
	/* albatrss DRIVER ハングは ORCC #$10 を維持。OP.BIN $87CA に届く vsync では I を落とし ISR を走らせる。
	   PC が DRIVER $F000–$F8FF（SWI $F819）や OP.BIN ISR 内のときはマスクしたまま。ネスト IRQ が Y を壊す。 */
	if (vsyncDue && irqVec == 0x87CA) {
		const uint16_t pc = cpu->pc.w;
		if (pc < 0x0080) {
			cpu->cc.i = false;
			cpu->cwai = false;
		}
		hw_->ParkAlbatrssIfStuck();
	}
	if (vsyncDue && irqVec == 0x2B3A) {
		cpu->cc.i = false;
		cpu->cwai = false;
	}

	int raiseFromChip = 0;
	int raiseFromVsync = 0;
	int ranHostTick = 0;
	if (hw_->useOpn_ && hw_->ChipOpn()) {
		raiseFromChip = chipIrqEdge;
		if (chipIrqEdge)
			chipIrqSeen_ = 1;
		/* YM2203 Timer B が武装すれば音楽クロック（ys2_fmav）。それまでは基板 60Hz で PATCH/DRIVER ISR を進める。
	   cap-at-8 はブートバースト後に大半の OPN を無音にした。 */
		if (!raiseFromChip && vsyncDue && !chipIrqSeen_)
			raiseFromVsync = 1;
		/* daiva OP.BIN $2B3A: BITA #1 のあと YM Timer B をポーリング。チップタイマ乗っ取りは FD03 bit0 を落とし ISR がタイマ再初期化だけになる。 */
		if (vsyncDue && irqVec == 0x2B3A)
			raiseFromVsync = 1;
	} else {
		raiseFromVsync = vsyncDue;
		/* Ys のリップはハード ISR の先の BIOS が無い。実 60Hz IRQ でインストール済みソフトベクタをディスパッチし、欠けたフォアグラウンドはパーク。 */
		if (vsyncDue && hw_->patchTableBase_ == 0xFED0) {
			const uint16_t tick = (uint16_t)(((uint16_t)hw_->Mem()[0xFFE2] << 8)
				| hw_->Mem()[0xFFE3]);
			if (tick >= 0x0100 && tick < 0xFE00) {
				if (tick == 0x28EA || tick == 0x29EC) {
					/* MANPR1 と MANPR2 はワーク／チャネル形式は同じだがコードは独立成長。実エントリを使い、3 チャネルパーサを実行してポインタ格納を回復する。 */
					uint8_t* m = hw_->Mem();
					const int man2 = (tick == 0x29EC) ? 1 : 0;
					const uint16_t phase = man2 ? 0x2987 : 0x2885;
					const uint16_t active = man2 ? 0x2983 : 0x2881;
					const uint16_t flush = man2 ? 0x2DB3 : 0x2C4E;
					const uint16_t gate = man2 ? 0x2985 : 0x2883;
					const uint16_t gateReload = man2 ? 0x2984 : 0x2882;
					const uint16_t parser = man2 ? 0x2A98 : 0x293D;
					const uint16_t shadowVolume = man2 ? 0x2EB5 : 0x2D50;
					static const uint16_t kChannel1[3] = { 0x2E9A, 0x2EBF, 0x2EE4 };
					static const uint16_t kChannel2[3] = { 0x3040, 0x3065, 0x308A };
					const uint16_t* channels = man2 ? kChannel2 : kChannel1;
					/* 実機タイマは 488Hz。TTLPRG が /3（~162Hz）、MANPR がさらに /3（~54Hz チャネル更新）。
	   vsyncDue（60Hz）からディスパッチするので MANPR 内部 /3 をバイパスしテンポを合わせる。 */
					m[phase] = 3;
					if (m[phase] >= 3) {
						m[phase] = 0;
						if (m[active])
							hw_->RunSubroutine(flush);
						m[gate] = 0;
						const unsigned mdataEnd = (unsigned)hw_->mdataAddr_
							+ (unsigned)hw_->mdataSize_;
						for (int ch = 0; ch < 3; ++ch) {
							const uint16_t base = channels[ch];
							const uint8_t count = m[base];
							const uint16_t oldPtr = (uint16_t)(((uint16_t)m[base + 2] << 8)
								| m[base + 3]);
							unsigned origin = oldPtr;
							if (origin >= 0x4D00u && origin < 0x4F00u)
								origin += 0x200u;
							if (origin < hw_->mdataAddr_ || origin >= mdataEnd) {
								origin = (unsigned)(((uint16_t)m[base + 4] << 8) | m[base + 5]);
								if (origin >= 0x4D00u && origin < 0x4F00u)
									origin += 0x200u;
							}
							/* 終端 00: F6 内ループかフレーズループ。ネイティブフェッチはここで回るが、ホストは 00 でパーサを飛ばす（F8 mute / $4Fxx 破壊回復）。短いループ（Devil's wind）は窓 0 で死ぬ。 */
							if (origin >= hw_->mdataAddr_ && origin < mdataEnd
								&& m[origin] == 0) {
								const uint8_t f6left = m[base + 18];
								const unsigned f6end = (unsigned)(((uint16_t)m[base + 19] << 8)
									| m[base + 20]);
								const unsigned f6ret = (unsigned)(((uint16_t)m[base + 21] << 8)
									| m[base + 22]);
								unsigned restart = 0;
								if (f6left && origin == f6end
									&& f6ret >= hw_->mdataAddr_ && f6ret < mdataEnd) {
									restart = f6ret;
									m[base + 18] = (uint8_t)(f6left - 1);
								} else if (!f6left) {
									unsigned songLoop = (unsigned)(((uint16_t)m[base + 4] << 8)
										| m[base + 5]);
									if (songLoop >= 0x4D00u && songLoop < 0x4F00u)
										songLoop += 0x200u;
									if (songLoop >= hw_->mdataAddr_ && songLoop < mdataEnd
										&& songLoop != 0x0200u)
										restart = songLoop;
								}
								if (restart) {
									m[base + 2] = (uint8_t)(restart >> 8);
									m[base + 3] = (uint8_t)restart;
									origin = restart;
								}
							}
							/* ネイティブ FE/F6 は STU 2,X しない。LDU 2,X / LBRA $2960 が約 20 万ステップ回り $4F00 を踏む。F コマンドはホストで処理し duration を植え、短い step 上限で 1 回パース。 */
							unsigned keepPtr = origin;
							if (origin >= hw_->mdataAddr_ && origin < mdataEnd
								&& m[origin] >= 0xF0) {
								unsigned p = origin;
								for (int command = 0; command < 12 && p < mdataEnd; ++command) {
									const uint8_t op = m[p];
									uint16_t handler = 0;
									unsigned fallback = 0;
									if (!man2) {
										if (op == 0xFC) { handler = 0x2A86; fallback = 7; }
										else if (op == 0xFD) { handler = 0x2A81; fallback = 3; }
										else if (op == 0xFE) { handler = 0x2A6C; fallback = 2; }
										else if (op == 0xF6 && p + 4 <= mdataEnd) {
											handler = 0x2B10; fallback = 4;
										}
										else if (op == 0xF9) { handler = 0x2AB6; fallback = 1; }
										else if (op == 0xF7 || op == 0xF8) { fallback = 2; }
										else if (op == 0xFA || op == 0xFB) {
											/* PULS Y / LBRA パーサ — JSR しない。FA nn [note] のあと F6…（First step ch2）。 */
											fallback = 2;
											if (p + 2 < mdataEnd && m[p + 2] && m[p + 2] < 0xF0)
												fallback = 3;
										}
									} else {
										/* フレーズ武装は 4Fxx ストリームを植える。JSR FC/FD/FE/F6（音色フラグ＋ループ）。F7/F8 は飛ばす — $2E58/$2EBB を呼び 400 step 上限でミキサが mute した。 */
										switch (op) {
										case 0xF4: handler = 0x2F1B; fallback = 1; break;
										case 0xF5: handler = 0x2F09; fallback = 5; break;
										case 0xF6: handler = 0x2C75; fallback = 4; break;
										case 0xF7:
										case 0xF8: fallback = 2; break;
										case 0xF9: handler = 0x2C1B; fallback = 1; break;
										case 0xFC: handler = 0x2BEB; fallback = 7; break;
										case 0xFD: handler = 0x2BE6; fallback = 3; break;
										case 0xFE: handler = 0x2BD1; fallback = 2; break;
										case 0xFF: handler = 0x2BD0; fallback = 1; break;
										default: break;
										}
									}
									if (!handler && !fallback)
										break;
									if (!handler) {
										p += fallback;
										continue;
									}
									cpu->index[0].w = base;
									cpu->index[2].w = (uint16_t)(p + 1);
									hw_->RunSubroutine(handler, 400);
									if (op == 0xF6) {
										const unsigned nu = cpu->index[2].w;
										if (nu > p && nu < mdataEnd)
											p = nu;
										else
											p += fallback;
									} else {
										p += fallback;
									}
								}
								if (p >= hw_->mdataAddr_ && p < mdataEnd && m[p] < 0xF0) {
									m[base + 2] = (uint8_t)(p >> 8);
									m[base + 3] = (uint8_t)p;
									keepPtr = p;
								}
							}
							const uint16_t livePtr = (uint16_t)(((uint16_t)m[base + 2] << 8)
								| m[base + 3]);
							const uint16_t playFlag = man2 ? (uint16_t)0x2982 : (uint16_t)0x2880;
							const uint8_t playSave = m[playFlag];
							if (livePtr >= hw_->mdataAddr_ && livePtr < mdataEnd
								&& m[livePtr] != 0 && m[livePtr] < 0xF0) {
								cpu->index[0].w = base;
								hw_->RunSubroutine(parser, 8000);
							}
							if (playSave && m[playFlag] == 0)
								m[playFlag] = playSave;
							if (man2 && m[active] == 0)
								m[active] = 1;
							const uint16_t newPtr = (uint16_t)(((uint16_t)m[base + 2] << 8)
								| m[base + 3]);
							if (newPtr < hw_->mdataAddr_ || newPtr >= mdataEnd
								|| (keepPtr >= hw_->mdataAddr_ && keepPtr < mdataEnd
									&& newPtr + 0x80u < keepPtr
									&& newPtr < hw_->mdataAddr_ + 0x80u)) {
								if (keepPtr >= hw_->mdataAddr_ && keepPtr < mdataEnd) {
									m[base + 2] = (uint8_t)(keepPtr >> 8);
									m[base + 3] = (uint8_t)keepPtr;
								}
							}
							else if (count == 1 && newPtr == livePtr
								&& livePtr >= hw_->mdataAddr_ && livePtr < mdataEnd
								&& m[livePtr] != 0 && m[livePtr] < 0xF0
								&& livePtr + 2u < mdataEnd) {
								m[base + 2] = (uint8_t)((livePtr + 2u) >> 8);
								m[base + 3] = (uint8_t)(livePtr + 2u);
							}
							/* AY R8+ch をシャドウと同期。$2C4E は 4D00→4F00 再配置であり AY ダンプではない。ch0 音量 0 のまま周期だけメロディを歩くことがある。 */
							if (m[base] > 0 && hw_->ChipAy()) {
								uint8_t vol = m[shadowVolume + ch];
								if (vol == 0) {
									vol = 0x0C;
									m[shadowVolume + ch] = vol;
								}
								hw_->ChipAy()->Write(0, (uint32_t)(8 + ch));
								hw_->ChipAy()->Write(1, vol);
							}
						}
						m[gate] = m[gateReload];
					}
				} else {
					hw_->RunSubroutine(tick);
				}
				hw_->Mem()[0xFC00] = 0x20;
				hw_->Mem()[0xFC01] = 0xFE;
				cpu->pc.w = 0xFC00;
				cpu->cc.i = false;
				cpu->cc.f = true;
				irqPulses_++;
				ranHostTick = 1;
				raiseFromVsync = 0;
			}
		}
	}

	/* XA2PSGPATCH tick（バンク再配置）。ネイティブ IRQ $FF94 はトランポリン。テンポ再ロードは AV タイマ /8 なので 60Hz では /1 に強制。 */
	if (vsyncDue && !ranHostTick && hw_->Xana2Tick() && hw_->Xana2Tempo()) {
		hw_->Mem()[hw_->Xana2Tempo()] = 1;
		hw_->RunSubroutine(hw_->Xana2Tick(), 80000, 1);
		hw_->Mem()[0xFC00] = 0x20;
		hw_->Mem()[0xFC01] = 0xFE;
		cpu->pc.w = 0xFC00;
		cpu->cc.i = false;
		cpu->cc.f = true;
		irqPulses_++;
		ranHostTick = 1;
		raiseFromVsync = 0;
	}

	if (!ranHostTick && (raiseFromChip || raiseFromVsync)) {
		/* vsync 源は 6809 の 1 線だけ。ちょうど 1 ベクタが $FD03 ハンドラならその線。Ys で他ベクタを撃つと $FF00 の PATCH データへ飛び、MANPR 初ノート前に RTI フレームを壊す。 */
		const int ysPsg = (!hw_->useOpn_ && hw_->patchTableBase_ == 0xFED0) ? 1 : 0;
		const int routeFd03 = (raiseFromVsync && !raiseFromChip
			&& (irqStub != firqStub)) ? 1 : 0;
		const uint16_t pcNow = cpu->pc.w;
		const int inAlbDrv = (irqVec == 0x87CA
			&& ((pcNow >= 0xF000 && pcNow < 0xF900)
				|| (pcNow >= 0x8500 && pcNow < 0xC500)));
		if (!inAlbDrv) {
			if (hw_->useOpn_) {
				/* IRQ+FIRQ 同時（I も F もクリア）は kohaku を嵐にした: 4000+ パルスで YM 書込がほぼ無い。1 線、IRQ 優先。 */
				if (!cpu->cc.i) {
					cpu->irq = true;
					irqPulses_++;
				} else if (!cpu->cc.f && !firqIsRti) {
					cpu->firq = true;
					irqPulses_++;
				}
			} else {
				if (!cpu->cc.i && (!routeFd03 || irqStub)
					&& !(ysPsg && raiseFromChip && !raiseFromVsync && irqStub)) {
					cpu->irq = true;
					irqPulses_++;
				}
				if (!cpu->cc.f && !firqIsRti && (!routeFd03 || firqStub)
					&& !(ysPsg && raiseFromChip && !raiseFromVsync && firqStub)) {
					cpu->firq = true;
					irqPulses_++;
				}
			}
		}
	}

	if (chipIrq && hw_->ChipOpn())
		hw_->ChipOpn()->AckIrq();

	if (vsyncDue && vsyncPeriod_ > 0) {
		while (nextVsync_ + vsyncPeriod_ <= now)
			nextVsync_ += vsyncPeriod_;
		nextVsync_ += vsyncPeriod_;
	}
}

/* M6809 を endCycle まで進める。CWAI/SYNC は次 IRQ へ */
void CDriverFm7::RunUntil(uint64_t endCycle)
{
	if (!hw_) return;
	mc6809__t* cpu = hw_->Mc6809();
	if (!cpu) return;
	CEmuHardFm7SetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->cycles < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->cycles;
		DeliverIrqs(now);
		/* CWAI/SYNC: 次の vsync/サンプルへ飛ばし、IRQ を実時間に保つ */
		if (cpu->cwai || cpu->sync) {
			uint64_t wake = endCycle;
			if (vsyncPeriod_ > 0 && nextVsync_ > now && nextVsync_ < wake)
				wake = nextVsync_;
			uint64_t delta = (wake > now) ? (wake - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->cycles += (unsigned long)delta;
			hw_->AddCpuCycles(delta);
			TickChips(delta);
			continue;
		}
		const unsigned long before = cpu->cycles;
		const int rc = mc6809_step(cpu);
		uint64_t ran = (uint64_t)(cpu->cycles - before);
		if (ran == 0) ran = 1;
		hw_->AddCpuCycles(ran);
		TickChips(ran);
		if (rc != 0)
			break;
		hw_->UnwindMissingBios();
	}
}

/* $FD58/$FD80 メールボックスへ曲を載せる */
void CDriverFm7::TriggerSong()
{
	if (!hw_) return;
	hw_->TriggerPlay(titleCode_ ? titleCode_ : (unsigned)songCode_);
	triggered_ = 1;
}

/* ROM 読込、PATCH がベクタを植えるまでブート */
int CDriverFm7::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardFm7*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 2000000;
	opnHz_ = hw_->opnHz_ > 0 ? hw_->opnHz_ : 1228800;
	ayHz_ = hw_->ayHz_ > 0 ? hw_->ayHz_ : 2457600;
	vsyncPeriod_ = (uint64_t)cpuHz_ / 60;
	opnResidual_ = 0;
	opnTimerMul_ = 1;
	opnTimerDiv_ = 1;
	/* Ys II FM77AV の載せた Timer B は約 63.8Hz。このタイトルは基板 60Hz のまま。他 FM-7 OPN は下げない。 */
	if (ge->archive[0] && _stricmp(ge->archive, "ys2_fmav") == 0) {
		opnTimerMul_ = 15;
		opnTimerDiv_ = 16;
	}
	ayResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	prevChipIrq_ = 0;
	chipIrqSeen_ = 0;
	irqPulses_ = 0;
	lastFd03IrqVec_ = 0xFFFF;

	if (titleCode || ge->titleCount <= 0) {
		titleCode_ = titleCode;
	} else {
		/* 自動選曲は呼び出しが 0 かつセットに本物の title 0 が無いときだけ（jikochu は 0 が演奏可能な曲番号）。 */
		int hasTitleZero = 0;
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code == 0) { hasTitleZero = 1; break; }
		}
		if (hasTitleZero) {
			titleCode_ = 0;
		} else {
			titleCode_ = ge->title[0].code;
			for (int i = 0; i < ge->titleCount; i++) {
				if (ge->title[i].code != 0) {
					titleCode_ = ge->title[i].code;
					break;
				}
			}
		}
	}
	{
		uint8_t song = 0, bank = 0;
		CHardFm7::UnpackTitle(titleCode_, &song, &bank);
		(void)bank;
		songCode_ = song;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode_))
		return 0;

	CEmuHardFm7SetActive(hw_);
	{
		mc6809__t* cpu = hw_->Mc6809();
		if (cpu) {
			nextVsync_ = (uint64_t)cpu->cycles + vsyncPeriod_;
			/* 約 1.0s ブート。PATCH がベクタを植え FD58 ポーリングに達する */
			RunUntil((uint64_t)cpu->cycles + (uint64_t)cpuHz_);
			hw_->UnwindStuckBootJsr();
		}
	}
	booted_ = 1;
	hw_->RefreshFd03Polarity();
	TriggerSong();
	/* PATCH に $FD58/$FD80 再生を消費させ、IRQ ベクタを再マウントさせる */
	{
		mc6809__t* cpu = hw_->Mc6809();
		if (cpu) {
			uint64_t settle = (uint64_t)cpuHz_ / 10;
			/* Laydock: IRQ $3502 + $5E6F 武装に追加 settle。最初の vsync 音楽 tick 前に OPN 音色初期化を終える。 */
			const uint16_t irq = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF8] << 8) | hw_->Mem()[0xFFF9]);
			if (irq == 0x3502)
				settle = (uint64_t)cpuHz_ / 2;
			/* PATCH は TTLPRG 行 0 をすぐ見る。かつての 5 秒 settle は描画前に埋め込みタイトルを消費した。 */
			RunUntil((uint64_t)cpu->cycles + settle);
			hw_->FinishDaivaOpPlay();
			hw_->FinishDaivaEdPlay();
			hw_->FinishSharrierPlay();
			hw_->ArmLaydockChannels();
			if (!hw_->useOpn_ && hw_->patchTableBase_ == 0xFED0
				&& hw_->Mem()[0xFFE2] == 0xFF && hw_->Mem()[0xFFE3] == 0xFF) {
				/* 常駐引き渡しは TTLPRG を初期化したが、リップ BIOS は PATCH 最後の STD $FFE2 に戻れない。行 0 からその 1 ベクタ書込を完了し、欠けたフォアグラウンドをパーク。 */
				hw_->Mem()[0xFFE2] = 0x11;
				hw_->Mem()[0xFFE3] = 0xB7;
				hw_->Mem()[0xFC00] = 0x20;
				hw_->Mem()[0xFC01] = 0xFE;
				cpu->pc.w = 0xFC00;
				cpu->cc.i = false;
				cpu->cc.f = true;
			}
		}
	}
	/* ベクタ／DRIVER は play 後に再マウントし得る — もう一度更新 */
	hw_->RefreshFd03Polarity();
	hw_->FinishXana2PsgPlay();
	/* albatrss: PATCH JSR $F000/$F004 は戻らない（I が立ったまま）ので OP.BIN ISR $87CA が無音書込数万回でも走らない。インストール済みベクタが本物の $FD03 ハンドラならマスク解除。 */
	{
		mc6809__t* cpu = hw_->Mc6809();
		if (cpu && cpu->cc.i) {
			uint8_t* m = hw_->Mem();
			const uint16_t irq = (uint16_t)(((uint16_t)m[0xFFF8] << 8) | m[0xFFF9]);
			if (irq >= 0x0100 && irq < 0xFE00
				&& ((m[irq] == 0xB6 && m[irq + 1] == 0xFD && m[irq + 2] == 0x03)
					|| (m[irq] == 0x96 && m[irq + 1] == 0x03))) {
				cpu->cc.i = false;
				cpu->cwai = false;
			}
		}
		hw_->ParkAlbatrssIfStuck();
	}
	/* jikochu: PATCH は $FD58 消費後に $0614 をクリア。PSG ゲートを再武装。曲エントリも再適用 — play は $FD59 読前に JSR $C006 するので、短い settle はブート初期化だけがアクティブ曲になる。 */
	if (!hw_->useOpn_ && hw_->ChipAy() && hw_->mdataAddr_ == 0xC000) {
		uint8_t* m = hw_->Mem();
		if (m && m[0xC19D] == 0x7D && m[0xC19E] == 0x06 && m[0xC19F] == 0x14) {
			m[0x0614] = 0x80;
			const uint8_t song = hw_->playSongLatch_;
			uint16_t entry = 0xC000;
			if (song == 1) {
				m[0xC114] = 0xBD;
				m[0xC115] = 0xC2;
				m[0xC116] = 0x42;
				entry = 0xC00A;
			} else if (song == 2) {
				m[0xC114] = 0x7E;
				m[0xC115] = 0xC0;
				m[0xC116] = 0xE6;
				entry = 0xC010;
			} else {
				m[0xC114] = 0x7E;
				m[0xC115] = 0xC0;
				m[0xC116] = 0xE6;
				m[0xC01D] = 0x09;
				entry = 0xC000;
			}
			hw_->RunSubroutine(entry);
			m[0x0614] = 0x80;
		}
	}
	return 1;
}

/* ハード参照を捨てる */
void CDriverFm7::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

/* 同一 zip の別曲 */
int CDriverFm7::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	titleCode_ = titleCode;
	songCode_ = (uint8_t)(titleCode & 0xff);
	chipIrqSeen_ = 0;
	hw_->TriggerPlay(titleCode_ ? titleCode_ : (unsigned)songCode_);
	triggered_ = 1;
	return 1;
}

/* CPU＋チップを進めステレオ合成 */
int CDriverFm7::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	mc6809__t* cpu = hw_->Mc6809();
	if (!cpu || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardFm7SetActive(hw_);

	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		const uint64_t end = (uint64_t)cpu->cycles + (uint64_t)cyclesPerSample;
		RunUntil(end);
		const uint64_t now = (uint64_t)cpu->cycles;
		if (now >= nextVsync_ + vsyncPeriod_ * 4)
			nextVsync_ = now + vsyncPeriod_;

		int16_t opnBuf[2] = { 0, 0 };
		int16_t ayBuf[2] = { 0, 0 };
		if (hw_->ChipOpn())
			hw_->ChipOpn()->Render(opnBuf, 1);
		if (hw_->ChipAy())
			hw_->ChipAy()->Render(ayBuf, 1);
		int32_t l = (int32_t)opnBuf[0] + (int32_t)ayBuf[0];
		int32_t r = (int32_t)opnBuf[1] + (int32_t)ayBuf[1];
		if (l > 32767) l = 32767;
		if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
	return frames;
}

/* Seek は未対応 */
int CDriverFm7::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
