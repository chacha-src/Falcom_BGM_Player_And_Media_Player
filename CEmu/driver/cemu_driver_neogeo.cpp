#include "StdAfx.h"

#include "cemu_driver_neogeo.h"

#include "../z80/Ay_Cpu.h"

#include <string.h>



/* Neo Geo ドライバ */
CDriverNeo::CDriverNeo()

	: hw_(NULL)

	, hostRate_(44100)

	, cpuHz_(4000000)

	, ymHz_(8000000)

	, songCmd_(0x01)

	, songCmdHi_(0)

	, ymResidual_(0)

	, cpuAcc_(0)

	, injected_(0)

	, ymIrqPending_(0)

	, cpuDebt_(0)

	, injectAt_(0)

	, reinjected_(0)

{

}



/* 後始末 */
CDriverNeo::~CDriverNeo()

{

	Close();

}



/* ROM 読込後、NMI 許可までブートして曲コマンドを決める */
int CDriverNeo::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)

{

	if (!hw || !ge || !fs || hw->hardKind != CHard::KIND_NEO) return 0;

	hw_ = (CHardNeo*)hw;

	hostRate_ = hw_->SampleRate();

	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 4000000;

	ymHz_ = hw_->ymHz_ > 0 ? hw_->ymHz_ : 8000000;

	ymResidual_ = 0;

	cpuAcc_ = 0;

	injected_ = 0;

	reinjected_ = 0;



	/* 固定選曲: ホスト title が非 0（ユーザコマンド 0x20-0xFF）ならそれ。

 * さもなくばカタログ 0x01..0x3F（0x21+ 優先）。試行表／ピーク探索はしない。 */

	songCmd_ = 0x20;

	songCmdHi_ = 0;

	{

		const uint8_t t = (uint8_t)(titleCode & 0xff);

		if (titleCode > 0xffu) {

			songCmdHi_ = (uint8_t)((titleCode >> 8) & 0xff);

			songCmd_ = t;

		} else if (titleCode && t >= 0x01)

			songCmd_ = t;

		else {

			uint8_t pick = 0;

			for (int i = 0; i < ge->titleCount; i++) {

				const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

				if (c >= 0x21 && c <= 0x3f) { pick = c; break; }

			}

			if (!pick) {

				for (int i = 0; i < ge->titleCount; i++) {

					const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

					if (c >= 0x01 && c <= 0x3f) { pick = c; break; }

				}

			}

			if (pick)

				songCmd_ = pick;

		}

	}



	/* カタログ先頭は SE 0x01 や空ファンファーレ 0x20 が多い。0x21..0x3F の

 * 最初の BGM へ 1 回だけ寄せる。mslug の 0x02 ロゴは残す。 */

	if (!titleCode && (songCmd_ == 0x01 || songCmd_ == 0x20)) {

		uint8_t pick = 0;

		/* 短いタイトルジングル 0x21 よりステージ BGM 0x22+ を優先（kof95） */

		for (int i = 0; i < ge->titleCount; i++) {

			const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

			if (c >= 0x22 && c <= 0x3f) { pick = c; break; }

		}

		if (!pick) {

			for (int i = 0; i < ge->titleCount; i++) {

				const uint8_t c = (uint8_t)(ge->title[i].code & 0xff);

				if (c >= 0x21 && c <= 0x3f) { pick = c; break; }

			}

		}

		if (pick) songCmd_ = pick;

	}


	if (!hw_->LoadRoms(fs, ge, titleCode))

		return 0;



	CEmuHardNeoSetActive(hw_);

	/* ADK OS 8.8 表 (2E0C): 空きは 10 00 00 00（mosyougi 0x04）。穴と次の

 * 生きた 0x20 を飛ばし、偶奇カタログが同じ BGM に落ちないようにする。 */

	if (!songCmdHi_) {

		uint8_t* mAdk = hw_->Mem();

		if (mAdk) {

			const uint16_t tab = (uint16_t)(mAdk[0x2E0C] | ((uint16_t)mAdk[0x2E0D] << 8));

			const unsigned off = (unsigned)songCmd_ * 4u;

			if (tab >= 0x1000u && (unsigned)tab + off + 4u < 0xF800u

				&& mAdk[tab + 8] == 0x40

				&& mAdk[tab + off] == 0x10 && mAdk[tab + off + 1] == 0

				&& mAdk[tab + off + 2] == 0 && mAdk[tab + off + 3] == 0) {

				uint8_t live1 = 0, live2 = 0;

				for (unsigned c = (unsigned)songCmd_ + 1u; c < 0x40u; c++) {

					if (mAdk[tab + c * 4u] == 0x20) {

						if (!live1) live1 = (uint8_t)c;

						else { live2 = (uint8_t)c; break; }

					}

				}

				if (live2) songCmd_ = live2;

				else if (live1) songCmd_ = live1;

			}

		}

	}

	/* 初期 SNK: 種別表 (0173)。1=SE、2+=BGM。生きた曲ポインタ付き type>=2 を

 * 優先し、0x02 の平坦 SE や空の 0x20 ファンファーレに落とさない。 */

	{

		const uint8_t* m = hw_->Mem();

		if (m && m[0x172] == 0x21) {

			const uint16_t tab = (uint16_t)(m[0x173] | ((uint16_t)m[0x174] << 8));

			if (tab >= 0x8000u && tab < 0xf800u) {

				uint16_t songTab = 0;

				for (unsigned a = 0x0f00; a + 8u < 0x1100u; a++) {

					if (m[a] == 0xd6 && m[a + 1] == 0x20 && m[a + 5] == 0x21) {

						songTab = (uint16_t)(m[a + 6] | ((uint16_t)m[a + 7] << 8));

						break;

					}

				}

				auto songLive = [&](uint8_t cmd) -> int {

					if (!songTab || cmd < 0x20) return 1;

					const unsigned idx = (unsigned)(cmd - 0x20u) * 2u;

					const uint16_t p = (uint16_t)(m[(uint16_t)(songTab + idx)]

						| ((uint16_t)m[(uint16_t)(songTab + idx + 1)] << 8));

					return p != 0;

				};

				uint8_t bgm = 0;

				for (unsigned i = 0x21; i < 0x40; i++) {

					const uint8_t ty = m[(uint16_t)(tab + i)];

					if (ty >= 2 && ty <= 5 && songLive((uint8_t)i)) {

						bgm = (uint8_t)i;

						break;

					}

				}

				if (!bgm) {

					const uint8_t ty20 = m[(uint16_t)(tab + 0x20)];

					if (ty20 >= 2 && ty20 <= 5 && songLive(0x20))

						bgm = 0x20;

				}

				if (!bgm) {

					for (unsigned i = 2; i < 0x20; i++) {

						const uint8_t ty = m[(uint16_t)(tab + i)];

						if (ty >= 2 && ty <= 5) {

							bgm = (uint8_t)i;

							break;

						}

					}

				}

				const uint8_t curTy = (songCmd_ < 0x40)

					? m[(uint16_t)(tab + songCmd_)] : 0;

				const int curLive = (songCmd_ >= 0x20) ? songLive(songCmd_) : 0;

				/* ホストのカタログ選曲が正。Open 時に title が無い zip ドロップだけ空き/SE を付け替える */

				if (!titleCode && bgm && songCmd_ < 0x40 && (curTy < 2 || curTy > 5 || !curLive))

					songCmd_ = bgm;

			}

		}

	}

	/* NMI 許可（OUT $08）か約 1s までブート。KOF M1 はバンク初期化後に許可 */

	RunUntil((uint64_t)cpuHz_ / 4);

	{

		Ay_Cpu* cpu = hw_->Cpu();

		for (int i = 0; i < 120 && cpu && !hw_->NmiEnabled(); i++)

			RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 60);

	}

	int snkDrv = 0;

	int makoto = 0;

	uint8_t* mBoot = hw_->Mem();

	if (mBoot) {

		for (unsigned i = 0; i + 12u < 0x80u; i++) {

			if (mBoot[i] == (uint8_t)'S' && memcmp(mBoot + i, "Sound Driver", 12) == 0) {

				snkDrv = 1; break;

			}

		}

		for (unsigned i = 0; i + 6u < 0x80u; i++) {

			if (memcmp(mBoot + i, "MAKOTO", 6) == 0) {

				makoto = 1; break;

			}

		}

	}

	/* KOF 系 / SNK Sound Driver のみ: コールドブートは FE34=0xFF で、

 * FE34!=0 の間は曲エントリが中断する。$08/$07 でクリア。MAKOTO は $08 を

 * 通常コマンドとして積み FE34 を消さず、FE30 poke を飛ばして type-3 が欠ける。 */

	if (snkDrv && hw_->PeekRam(0xFE34) != 0) {

		SendZ80Command(0x08, 90);

		Ay_Cpu* cpu = hw_->Cpu();

		for (int i = 0; i < 120 && cpu; i++) {

			RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 60);

			if (hw_->PeekRam(0xFE34) == 0 && !hw_->SoundCmdPending()

				&& hw_->PeekRam(0xFEB0) == hw_->PeekRam(0xFEB1)

				&& hw_->PeekRam(0xFE47) == hw_->PeekRam(0xFE46))

				break;

		}

		if (hw_->PeekRam(0xFE34) != 0)

			SendZ80Command(0x07, 90);

		for (int i = 0; i < 60 && cpu && hw_->PeekRam(0xFE34) != 0; i++)

			RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 60);

	}

	if (mBoot) {

		if (mBoot[0xFE30] == 0xff) { mBoot[0xFE30] = 0; mBoot[0xFE31] = 0; }

		if (mBoot[0xFE21] == 0xff) { mBoot[0xFE21] = 0; mBoot[0xFE22] = 0; }

		if (mBoot[0xFE1C] == 0xff) { mBoot[0xFE1C] = 0; mBoot[0xFE1D] = 0; }

		if (makoto) {

			if (mBoot[0xFE34] == 0xff) { mBoot[0xFE34] = 0; mBoot[0xFE35] = 0; }

		}

		if (snkDrv) {

			if (mBoot[0xFDE1] == 0xff) { mBoot[0xFDE1] = 0; mBoot[0xFDE2] = 0; }

			if (mBoot[0xFD11] == 0xff) { mBoot[0xFD11] = 0; mBoot[0xFD12] = 0; }

		}

		/* Psikyo (s1945p): BGM 0x20..0x3F は F902!=0 の間リターンする */

		if (mBoot[0x66] == 0x08 && mBoot[0x67] == 0xd9

			&& mBoot[0x68] == 0xdb && mBoot[0x69] == 0x00) {

			mBoot[0xF900] = 0;

			mBoot[0xF902] = 0;

		}

	}

	if (songCmdHi_)

		SendZ80Command(songCmdHi_, 120);

	SendZ80Command(songCmd_, 120);

	injectAt_ = 0;

	injected_ = 1;

	return 1;

}



/* コマンドキューが空くまで待つ */
void CDriverNeo::WaitQueueIdle(int maxFrames60)

{

	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;

	if (!cpu || cpuHz_ < 1) return;

	const int early = (hw_->PeekRam(0xFE34) == 0

		&& hw_->Mem() && hw_->Mem()[0x172] == 0x21) ? 1 : 0;

	int sawCmd = 0;

	for (int i = 0; i < maxFrames60; i++) {

		RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 60);

		if (hw_->SoundCmdPending()) {

			sawCmd = 1;

			continue;

		}

		const uint8_t kofW = hw_->PeekRam(0xFEB0);

		const uint8_t kofR = hw_->PeekRam(0xFEB1);

		if (early) {

			if (sawCmd && i >= 30)

				return;

			continue;

		}

		const uint8_t earlyW = hw_->PeekRam(0xFE47);

		const uint8_t earlyR = hw_->PeekRam(0xFE46);

		if (kofW == kofR && earlyW == earlyR)

			return;

	}

}



/* Z80 へコマンドを送り完了を待つ */
void CDriverNeo::SendZ80Command(uint8_t cmd, int maxFrames60)

{

	if (!hw_ || !cmd) return;

	WaitQueueIdle(maxFrames60 > 0 ? maxFrames60 : 60);

	hw_->SetSoundCommand(cmd);

	WaitQueueIdle(maxFrames60 > 0 ? maxFrames60 : 60);

}



/* ハード参照を捨てる */
void CDriverNeo::Close()

{

	hw_ = NULL;

}



/* 同一 zip の別曲を NMI コマンドで切替 */
int CDriverNeo::OverlayTitle(unsigned titleCode)

{

	const uint8_t t = (uint8_t)(titleCode & 0xff);

	if (!hw_ || !t) return 0;

	songCmd_ = t;

	hw_->SetSoundCommand(t);

	injected_ = 1;

	return 1;

}



/* YM2610 クロックを CPU 比で進める */
void CDriverNeo::TickYm(uint64_t cpuCycles)

{

	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;

	ymResidual_ += cpuCycles * (uint64_t)ymHz_;

	const uint64_t ticks = ymResidual_ / (uint64_t)cpuHz_;

	ymResidual_ %= (uint64_t)cpuHz_;

	if (ticks)

		hw_->SoundChip()->AdvanceClocks(ticks);

}



/* YM タイマ満了を IM1、コマンドを NMI で届ける */
void CDriverNeo::DeliverIrqs()

{

	if (!hw_ || !hw_->Cpu()) return;

	Ay_Cpu* cpu = hw_->Cpu();

	CChip* chip = hw_->SoundChip();

	if (hw_->TakeNmiPulse())

		Ay_CpuNmi(cpu);

	/* YM2610 タイマ IRQ → Z80 IM1。Irq() 優先。取りこぼしエッジでも

 * status タイマフラグを受け、シーケンサが止まらないようにする。 */

	if (!chip)

		return;

	/* ISR が割り込み禁止中でも満了を貯める。DI 区間に落ちた tick は後で届ける。

 * 上限は長い DI がバーストにならないため。 */

	ymIrqPending_ += (int)chip->TakeTimerExpiries();

	if (ymIrqPending_ > 2) ymIrqPending_ = 2;

	/* IFF1 が立っているときだけ割り込む。DI 中の ForceIm1 は初期 M1 の
	   ビジィウェイトを壊す（nam1975 @2282）。 */

	if (cpu->r.im != 1 || !cpu->r.iff1)

		return;

	const int st = (chip->ReadStatus() & 0x03) != 0;

	/* タイマ満了ごとに 1 割り込み。マージした IRQ レベルを見ると、線が ISR の
	   EI 以降もアサートされたまま次命令境界で再発火し、Timer A+B より約 11% 多い。 */

	if ((chip->Irq() || st) && ymIrqPending_ > 0) {

		if (Ay_CpuIm1Interrupt(cpu))

			ymIrqPending_--;

	}

}



/* Z80 を endCycle まで進める */
void CDriverNeo::RunUntil(uint64_t endCycle)

{

	if (!hw_ || !hw_->Cpu()) return;

	Ay_Cpu* cpu = hw_->Cpu();

	CEmuHardNeoSetActive(hw_);

	int guard = 0;

	while ((uint64_t)cpu->time64() < endCycle && guard++ < 2000000) {

		DeliverIrqs();

		const int cycles = Ay_CpuRunOne(cpu);

		if (cycles <= 0) {

			cpu->adjust_time(4);

			hw_->AddCpuCycles(4);

			TickYm(4);

			continue;

		}

		hw_->AddCpuCycles((uint64_t)cycles);

		TickYm((uint64_t)cycles);

	}

}



/* サウンドコマンドをラッチし NMI を起こす */
void CDriverNeo::InjectSongCommand()

{

	if (!hw_ || !songCmd_) return;

	hw_->SetSoundCommand(songCmd_);

	injected_ = 1;

}



/* CPU＋YM2610 を進めステレオ合成 */
int CDriverNeo::Render(int16_t* stereo, int frames)

{

	if (!hw_ || !stereo || frames <= 0) return 0;

	Ay_Cpu* cpu = hw_->Cpu();

	CChip* chip = hw_->SoundChip();

	if (!cpu || !chip || hostRate_ < 1 || cpuHz_ < 1) return 0;

	CEmuHardNeoSetActive(hw_);



	for (int i = 0; i < frames; i++) {

		const uint64_t now = (uint64_t)cpu->time64();

		if (!injected_)

			InjectSongCommand();

		/* ラッチ未読が約 0.25s 続く（遅いハンドラ）なら 1 回再送 */

		if (injected_ && !reinjected_ && hw_->SoundCmdPending()

			&& now >= (uint64_t)cpuHz_ / 4) {

			hw_->SetSoundCommand(songCmd_);

			reinjected_ = 1;

		}

		cpuAcc_ += (int64_t)cpuHz_;

		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);

		cpuAcc_ %= (int64_t)hostRate_;

		if (cyclesPerSample < 1) cyclesPerSample = 1;

		/* 超過サイクルを持ち越す。内側ループは命令単位でしか止まれないので、
	   毎サンプル現在時刻から期限を再計算すると前サンプルの超過＋満額予算になり Z80 が速くなる。 */

		cpuDebt_ += cyclesPerSample;

		const uint64_t start = (uint64_t)cpu->time64();

		const uint64_t end = start

			+ (uint64_t)(cpuDebt_ > 0 ? cpuDebt_ : 0);

		while ((uint64_t)cpu->time64() < end) {

			DeliverIrqs();

			const int cycles = Ay_CpuRunOne(cpu);

			if (cycles <= 0) {

				cpu->adjust_time(4);

				hw_->AddCpuCycles(4);

				TickYm(4);

				continue;

			}

			hw_->AddCpuCycles((uint64_t)cycles);

			TickYm((uint64_t)cycles);

		}

		cpuDebt_ -= (int64_t)((uint64_t)cpu->time64() - start);

		chip->Render(stereo + i * 2, 1);

	}

	return frames;

}



/* Seek は未対応 */
int CDriverNeo::Seek(uint64_t sample)

{

	(void)sample;

	return 0;

}



/* Neo Geo ドライバ生成 */
CDriver* CDriverNeoCreate()

{

	return new CDriverNeo();

}


