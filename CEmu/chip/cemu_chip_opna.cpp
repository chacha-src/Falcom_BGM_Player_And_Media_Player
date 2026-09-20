#include "StdAfx.h"
#include "cemu_chip_opna.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include "ymfm.h"
#include "ymfm_opn.h"
#include <string.h>

struct CEmuChipOpnaImpl : ymfm::ymfm_interface {
	ymfm::ym2608* opna; /* Sound Board II / subtype opna|8801-10 */
	ymfm::ym2203* opn;  /* PC-88 OPN（subtype opn）— ym2608@4MHz ではない */
	int32_t hostRate;
	int32_t chipRate;
	uint32_t inputClock;
	int64_t chipAcc;
	int64_t tickAcc;
	int32_t curL;
	int32_t curR;
	uint8_t lastAddr[2];
	int opnaMode;
	int irqAsserted;
	int64_t timerLeft[2];
	int64_t dbgLastDur[2];
	uint8_t mode27;
	unsigned timerClockScale;
	unsigned timerClockScaleDen;
	uint64_t timerScaleResidual;
	unsigned pitchRateDiv;
	int pitchOctaveShift; /* FMブロック書き換えのみ。シャドウは元のブロックを保持。 */
	int carrierFadeClamp;
	struct FadePatch {
		uint8_t valid;
		uint8_t alg;
		uint8_t keyTl[4];
		uint8_t minTl[4];
	} fadePatch[64];
	/* ADPCM-A: 固定 8KiB リズムROM（ym2608_adpcm_rom.bin）。 */
	uint8_t adpcmRom[0x2000];
	unsigned adpcmRomSize;
	/* ADPCM-B: ゲームサンプルRAM（catalog type=adpcm、多くの場合100KB超）。 */
	uint8_t adpcmB[256 * 1024];
	unsigned adpcmBSize;

	int allowTimerAIrq; /* 0=mucom88流（タイマBのみ）、1=多くのPC88ドライバ */
	unsigned dbgFireA, dbgFireB, dbgIrqPulse;
	uint64_t dbgClockSum;

	/* 再生プローブ指標（データポート書き込みのみ）。 */
	unsigned playWrites;
	unsigned playKeyOns;
	unsigned playFnumChanges;
	unsigned playSsgPeriodChanges;
	unsigned playSsgPeriodChg[3]; /* A/B/C */
	unsigned playChMask;
	int opnKeyFlush_;
	uint8_t playLastFnumLo[6];
	uint8_t playLastFnumHi[6];
	uint16_t playLastSsgPeriod[3];
	uint8_t playHaveFnum[6];
	uint8_t playHaveSsg[3];
	uint8_t ssgRegs[16]; /* bank0 $00-$0F シャドウ */
	uint8_t fmRegs[512]; /* bank0+bank1 イメージ（FmMon。プローブは [0..255]） */
	unsigned ssgRegWrites[16]; /* SSGレジスタ毎の書き込み回数 */
	uint8_t ssgVolCHist[64];
	unsigned ssgVolCHistN;
	unsigned ssgVolCNon0F;
	unsigned ssgVolCZero;
	uint8_t ssgVolCSeen[32];
	uint8_t ssgMixHist[64];
	unsigned ssgMixHistN;
	uint8_t ssgMixSeen[32]; /* 256/8 */
	uint64_t ssgEnergy[3]; /* YM2203 A/B/C の |sample| 合計（OPNA MixTo1 では0） */

	CEmuChipOpnaImpl()
		: opna(NULL)
		, opn(NULL)
		, hostRate(44100)
		, chipRate(0)
		, inputClock(0)
		, chipAcc(0)
		, tickAcc(0)
		, curL(0)
		, curR(0)
		, opnaMode(1)
		, irqAsserted(0)
		, mode27(0)
		, timerClockScale(1)
		, timerClockScaleDen(1)
		, timerScaleResidual(0)
		, pitchRateDiv(1)
		, pitchOctaveShift(0)
		, carrierFadeClamp(0)
		, adpcmRomSize(0)
		, adpcmBSize(0)
		, allowTimerAIrq(1)
		, dbgFireA(0)
		, dbgFireB(0)
		, dbgIrqPulse(0)
		, dbgClockSum(0)
		, playWrites(0)
		, playKeyOns(0)
		, playFnumChanges(0)
		, playSsgPeriodChanges(0)
		, playChMask(0)
		, opnKeyFlush_(0)
	{
		memset(lastAddr, 0, sizeof(lastAddr));
		memset(adpcmRom, 0, sizeof(adpcmRom));
		memset(adpcmB, 0, sizeof(adpcmB));
		memset(playLastFnumLo, 0, sizeof(playLastFnumLo));
		memset(playLastFnumHi, 0, sizeof(playLastFnumHi));
		memset(playLastSsgPeriod, 0, sizeof(playLastSsgPeriod));
		memset(playHaveFnum, 0, sizeof(playHaveFnum));
		memset(playHaveSsg, 0, sizeof(playHaveSsg));
		memset(playSsgPeriodChg, 0, sizeof(playSsgPeriodChg));
		memset(ssgRegs, 0, sizeof(ssgRegs));
		memset(fmRegs, 0, sizeof(fmRegs));
		memset(ssgRegWrites, 0, sizeof(ssgRegWrites));
		memset(ssgVolCHist, 0, sizeof(ssgVolCHist));
		ssgVolCHistN = 0;
		ssgVolCNon0F = 0;
		ssgVolCZero = 0;
		memset(ssgVolCSeen, 0, sizeof(ssgVolCSeen));
		memset(ssgMixHist, 0, sizeof(ssgMixHist));
		ssgMixHistN = 0;
		memset(ssgMixSeen, 0, sizeof(ssgMixSeen));
		memset(ssgEnergy, 0, sizeof(ssgEnergy));
		memset(fadePatch, 0, sizeof(fadePatch));
		timerLeft[0] = timerLeft[1] = -1;
		dbgLastDur[0] = dbgLastDur[1] = 0;
	}

	~CEmuChipOpnaImpl()
	{
		delete opna;
		delete opn;
		opna = NULL;
		opn = NULL;
	}

	void CreateChips(int wantOpna)
	{
		delete opna; opna = NULL;
		delete opn; opn = NULL;
		opnaMode = wantOpna ? 1 : 0;
		if (opnaMode) {
			opna = new ymfm::ym2608(*this);
			/* ローカル ymfm 音量拡張は内部クランプ前に FM+ADPCM を2倍にする。
			   ここでは参照 ymfm の unity。そうしないと ADPCM-B 無しでも密なFMがクリップ。
			   SSG は50%のまま、モノラル MixTo1 がステレオバス横でヘッドルームを持つ。 */
			opna->setfmvolume(32768);
			opna->setpsgvolume(32768);
		} else {
			opn = new ymfm::ym2203(*this);
		}
	}

	void ymfm_update_irq(bool asserted) override
	{
		/* asserted=true は無視: CPUサウンドIRQ辺は ExpireTimers から来る
		   （mucom=タイマBのみ、他PC88=allowTimerAIrq 経由でAおよび/またはB）。
		   asserted=false はクリアする。 */
		if (!asserted)
			irqAsserted = 0;
	}

	void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override
	{
		if (tnum >= 2) return;
		/* 負 = キャンセル */
		timerLeft[tnum] = duration_in_clocks;
		if (duration_in_clocks > 0)
			dbgLastDur[tnum] = duration_in_clocks;
	}

	uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
	{
		if (type == ymfm::ACCESS_ADPCM_A && address < adpcmRomSize)
			return adpcmRom[address];
		if (type == ymfm::ACCESS_ADPCM_B && address < sizeof(adpcmB))
			return adpcmB[address];
		return 0;
	}

	void ymfm_external_write(ymfm::access_class type, uint32_t address, uint8_t data) override
	{
		if (type == ymfm::ACCESS_ADPCM_B && address < sizeof(adpcmB)) {
			adpcmB[address] = data;
			if (address + 1 > adpcmBSize)
				adpcmBSize = address + 1;
		}
	}

	void ExpireTimers(int64_t clocks)
	{
		for (int t = 0; t < 2; t++) {
			if (timerLeft[t] < 0) continue;
			timerLeft[t] -= clocks;
			/* リロード跨ぎでオーバーシュートを適用し、大きな AdvanceClocks が
			   周期を伸ばさないようにする（KOEI OPN がわずかに遅くなる原因だった）。 */
			while (timerLeft[t] <= 0) {
				const int64_t over = -timerLeft[t];
				timerLeft[t] = -1;
				if (m_engine)
					m_engine->engine_timer_expired((uint32_t)t);
				if (t == 0) dbgFireA++;
				else dbgFireB++;
				/* hoot ssFMTimer と同じ: load-enable と irq-enable の両方が必要。
				   IRQのみ（load無し）でも ymfm は自動リロードする。それをパルスすると
				   mucom がサントラより数%急ぐ。 */
				const uint8_t loadEn = (uint8_t)(mode27 & (t == 0 ? 0x01 : 0x02));
				const uint8_t irqEn = (uint8_t)(mode27 & (t == 0 ? 0x04 : 0x08));
				if (loadEn && irqEn && (t == 1 || allowTimerAIrq)) {
					irqAsserted = 1;
					dbgIrqPulse++;
				}
				if (timerLeft[t] < 0)
					break; /* キャンセル済み */
				timerLeft[t] -= over;
			}
		}
	}

	/* 音声サンプル経路のみ（Renderから呼ぶ）。AdvanceClocks からは使わない —
	   以前は generate() が Render と二重クロックになり OPN/OPNA が約2倍速だった。
	   タイマ/IRQ は AdvanceClocks のみ（OPM の Count vs Mix と同じ分離）。 */
	void ChipSample()
	{
		if (opn && opnKeyFlush_) {
			opn->flush_fm_clock();
			opnKeyFlush_ = 0;
		}
		if (opna) {
			ymfm::ym2608::output_data o;
			opna->generate(&o, 1);
			const int n = (int)ymfm::ym2608::OUTPUTS;
			const int32_t a = o.data[0];
			const int32_t b = o.data[1 % n];
			const int32_t c = o.data[2 % n];
			/* ステレオMix: FM L/R に ADPCM を両ch加算。 */
			curL = a + c;
			curR = b + c;
		} else if (opn) {
			ymfm::ym2203::output_data o;
			opn->generate(&o, 1);
			/* YM2203: data[0]=FM（モノラル）、data[1..3]=SSG A/B/C。
			   3ch合計（OPNA MixTo1 と同様 2/3 スケール）— Cのみのドライバ
			   （SORC98 heartbeat / 多くの Falcom OPN）は data[1] だけだと無音だった。 */
			const int32_t fm = o.data[0];
			/* SSG は OPNA setpsgvolume(32768) と同様に半分。MixTo1 の 2/3 合計を維持。 */
			const int32_t ssg = (o.data[1] + o.data[2] + o.data[3]) / 3;
			ssgEnergy[0] += (uint64_t)(o.data[1] < 0 ? -o.data[1] : o.data[1]);
			ssgEnergy[1] += (uint64_t)(o.data[2] < 0 ? -o.data[2] : o.data[2]);
			ssgEnergy[2] += (uint64_t)(o.data[3] < 0 ? -o.data[3] : o.data[3]);
			curL = curR = fm + ssg;
		}
	}

	void ResetChip()
	{
		if (opna) opna->reset();
		if (opn) opn->reset();
	}

	void WritePort(uint32_t offset, uint8_t data)
	{
		if (opna) opna->write(offset, data);
		else if (opn && (offset & 2) == 0) /* OPN に hi ポートは無い */
			opn->write(offset & 1, data);
	}

	uint8_t ReadStatus()
	{
		if (opna) return opna->read_status();
		if (opn) return opn->read_status();
		return 0;
	}

	uint8_t ReadData()
	{
		if (opna) return opna->read_data();
		if (opn) return opn->read_data();
		return 0;
	}

	uint8_t ReadStatusHi()
	{
		/* 拡張ポートは ADPCM-B の EOS/BRDY/PLAYING とそのIRQフラグを出す。
		   低タイマステータスの上に 0x0c を強制すると、ゲストのポーリングが
		   EOS を常時観測し、生きている PLAYING 状態が隠れた。 */
		if (opna) return opna->read_status_hi();
		/* OPN: hi ステータス無し。lo をミラー。 */
		return ReadStatus();
	}

	uint8_t ReadDataHi()
	{
		if (opna) return opna->read_data_hi();
		return 0;
	}

	uint32_t SampleRateForClock(uint32_t clk) const
	{
		if (opna) return opna->sample_rate(clk);
		if (opn) return opn->sample_rate(clk);
		return clk / 8;
	}

	void ClampCarrierTlBeforeKeyOn(unsigned ch)
	{
		if (!carrierFadeClamp || ch >= 3) return;
		static const uint8_t kOff[4] = { 0x00, 0x04, 0x08, 0x0C };
		const unsigned alg = fmRegs[0xB0 + ch] & 7u;
		/* レジスタ群は SLOT1,SLOT3,SLOT2,SLOT4。 */
		static const uint8_t kCarrierMask[8] = {
			0x08, 0x08, 0x08, 0x08, 0x0C, 0x0E, 0x0E, 0x0F
		};
		const unsigned carriers = kCarrierMask[alg];
		uint8_t tl[4];
		for (unsigned op = 0; op < 4; ++op)
			tl[op] = fmRegs[0x40 + kOff[op] + ch] & 0x7F;

		FadePatch* patch = NULL;
		FadePatch* freePatch = NULL;
		for (unsigned i = 0; i < 64; ++i) {
			FadePatch* p = &fadePatch[i];
			if (!p->valid) {
				if (!freePatch) freePatch = p;
				continue;
			}
			if (p->alg != alg) continue;
			int same = 1;
			for (unsigned op = 0; op < 4; ++op)
				if ((carriers & (1u << op)) == 0 && p->keyTl[op] != tl[op])
					same = 0;
			if (same) { patch = p; break; }
		}
		if (!patch) {
			patch = freePatch;
			if (!patch) return;
			patch->valid = 1;
			patch->alg = (uint8_t)alg;
			memcpy(patch->keyTl, tl, sizeof(tl));
			memcpy(patch->minTl, tl, sizeof(tl));
			return;
		}

		for (unsigned op = 0; op < 4; ++op) {
			if ((carriers & (1u << op)) == 0) continue;
			if (tl[op] < patch->minTl[op])
				patch->minTl[op] = tl[op];
			if (tl[op] > patch->minTl[op]) {
				WritePort(0, 0x40 + kOff[op] + ch);
				WritePort(1, patch->minTl[op]);
				fmRegs[0x40 + kOff[op] + ch] = patch->minTl[op];
			}
		}
		/* ドライバがキーオンデータの前に選んでいたアドレスを戻す。 */
		WritePort(0, 0x28);
	}

	void RefreshChipRate()
	{
		int64_t rate = (int64_t)SampleRateForClock(inputClock);
		if (rate < 8000)
			rate = (int64_t)(opnaMode ? (inputClock / 8) : (inputClock / 4));
		rate /= (int64_t)(pitchRateDiv ? pitchRateDiv : 1);
		if (rate < 1) rate = 1;
		chipRate = (int32_t)rate;
	}
};

static int32_t CEmuClamp16(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

/* YM2608/YM2203 初期化。opnaMode≠0 なら OPNA、0 なら OPN。 */
void CEmuChipOpnaInit(CEmuChipOpna* c, uint32_t clockHz, int opnaMode, int sampleRate)
{
	if (!c) return;
	CEmuChipOpnaShutdown(c);
	CEmuChipOpnaImpl* impl = new CEmuChipOpnaImpl();
	impl->CreateChips(opnaMode ? 1 : 0);
	impl->hostRate = sampleRate > 0 ? sampleRate : 44100;
	impl->ResetChip();
	/* OPN は実 ym2203（clock/72 の FM+タイマ）。ym2608@4MHz で OPN を
	   エミュレートしない — それは clock/144 で、誤差の多い2倍ハックが
	   テンポを歪める（sorc88 など）。 */
	const uint32_t clk = clockHz ? clockHz : (impl->opnaMode ? 7987200u : 3993600u);
	impl->inputClock = clk;
	impl->chipRate = (int32_t)impl->SampleRateForClock(clk);
	if (impl->chipRate < 8000)
		impl->chipRate = (int32_t)(impl->opnaMode ? (clk / 8) : (clk / 4));
	impl->chipAcc = 0;
	impl->tickAcc = 0;
	impl->irqAsserted = 0;
	impl->curL = impl->curR = 0;
	c->chip = impl;
	c->sampleRate = impl->hostRate;
	c->opnaMode = impl->opnaMode;
	c->ready = 1;
}

void CEmuChipOpnaReset(CEmuChipOpna* c)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	impl->ResetChip();
	impl->chipAcc = 0;
	impl->curL = impl->curR = 0;
	memset(impl->lastAddr, 0, sizeof(impl->lastAddr));
	impl->timerLeft[0] = impl->timerLeft[1] = -1;
	impl->irqAsserted = 0;
	impl->playWrites = 0;
	impl->playKeyOns = 0;
	impl->playFnumChanges = 0;
	impl->playSsgPeriodChanges = 0;
	impl->playChMask = 0;
	memset(impl->playLastFnumLo, 0, sizeof(impl->playLastFnumLo));
	memset(impl->playLastFnumHi, 0, sizeof(impl->playLastFnumHi));
	memset(impl->playLastSsgPeriod, 0, sizeof(impl->playLastSsgPeriod));
	memset(impl->playHaveFnum, 0, sizeof(impl->playHaveFnum));
	memset(impl->playHaveSsg, 0, sizeof(impl->playHaveSsg));
	memset(impl->playSsgPeriodChg, 0, sizeof(impl->playSsgPeriodChg));
	memset(impl->ssgRegs, 0, sizeof(impl->ssgRegs));
	memset(impl->fmRegs, 0, sizeof(impl->fmRegs));
	memset(impl->ssgRegWrites, 0, sizeof(impl->ssgRegWrites));
	memset(impl->ssgVolCHist, 0, sizeof(impl->ssgVolCHist));
	impl->ssgVolCHistN = 0;
	impl->ssgVolCNon0F = 0;
	impl->ssgVolCZero = 0;
	memset(impl->ssgVolCSeen, 0, sizeof(impl->ssgVolCSeen));
	memset(impl->ssgMixHist, 0, sizeof(impl->ssgMixHist));
	impl->ssgMixHistN = 0;
	memset(impl->ssgMixSeen, 0, sizeof(impl->ssgMixSeen));
	memset(impl->ssgEnergy, 0, sizeof(impl->ssgEnergy));
	memset(impl->fadePatch, 0, sizeof(impl->fadePatch));
}

void CEmuChipOpnaWrite(CEmuChipOpna* c, uint32_t addr, uint32_t data)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	if (addr == 0 || addr == 0x100) {
		const int p = (addr == 0x100) ? 1 : 0;
		impl->lastAddr[p] = (uint8_t)data;
		impl->WritePort((uint32_t)(p * 2), (uint8_t)data);
		/* YM2608/2203: アドレス 2D/2E/2F 書きで FMクロック ÷6/÷3/÷2 を
		   即時選択（データ書き不要）。sample_rate() はプリスケールを追うので
		   ここで chipRate を更新 — しないと BIOS/アプリの 2Fh がピッチを3倍し、
		   Render はリセット ÷6 のまま進む（vg2 など）。 */
		if (p == 0 && (data == 0x2D || data == 0x2E || data == 0x2F)) {
			impl->RefreshChipRate();
		}
		return;
	}
	if (addr == 1 || addr == 0x101) {
		const int p = (addr == 0x101) ? 1 : 0;
		const uint8_t rawData = (uint8_t)data;
		uint8_t chipData = rawData;
		const unsigned reg = impl->lastAddr[p];
		if (p == 0 && reg == 0x28 && (rawData & 0xF0) != 0)
			impl->ClampCarrierTlBeforeKeyOn(rawData & 7u);
		/* A4-A6 は通常chの F-number/block 上位。AC-AE は特殊モード演算子の上位。
		   YM入力だけシフトする。下の指標/FmMon はドライバの生ブロックを意図的に保持。 */
		if (impl->pitchOctaveShift != 0
			&& ((reg >= 0xA4 && reg <= 0xA6)
				|| (p == 0 && reg >= 0xAC && reg <= 0xAE))) {
			int block = (chipData >> 3) & 7;
			block += impl->pitchOctaveShift;
			if (block < 0) block = 0;
			if (block > 7) block = 7;
			chipData = (uint8_t)((chipData & ~0x38u) | ((unsigned)block << 3));
		}
		impl->WritePort((uint32_t)(p * 2 + 1), chipData);
		const unsigned shadowAddr = (p ? 0x100u : 0u) | impl->lastAddr[p];
		if (p == 0 && impl->lastAddr[0] == 0x27)
			impl->mode27 = (uint8_t)data;
		/* YM2203: キーオンはFMクロックまでラッチ。フラッシュは ChipSample へ延期 —
		   Z80 IRQ/PortOut 内から clock_fm を呼ぶとネストが壊れた。 */
		if (impl->opn && p == 0 && impl->lastAddr[0] == 0x28)
			impl->opnKeyFlush_ = 1;
		/* 再生指標: タイマ/ステータス（0x24-0x27）ノイズは無視。 */
		{
			const unsigned r = impl->lastAddr[p];
			const uint8_t d = rawData;
			if (!(r >= 0x24 && r <= 0x27))
				impl->playWrites++;
			if (r == 0x28 && (d & 0xf0) != 0) {
				impl->playKeyOns++;
				impl->playChMask |= 1u << (d & 7);
			}
			/* A0-A2 / A4-A6（OPNA ch3-5 は +0x100）: F-number の動き。 */
			if ((r >= 0xA0 && r <= 0xA2) || (r >= 0xA4 && r <= 0xA6)) {
				const int slot = (r <= 0xA2) ? (int)(r - 0xA0) : (int)(r - 0xA4);
				const int ch = slot + (p ? 3 : 0);
				if (ch >= 0 && ch < 6) {
					if (r <= 0xA2) {
						if (impl->playHaveFnum[ch]
							&& impl->playLastFnumLo[ch] != d)
							impl->playFnumChanges++;
						impl->playLastFnumLo[ch] = d;
						impl->playHaveFnum[ch] = 1;
					} else {
						if (impl->playHaveFnum[ch]
							&& impl->playLastFnumHi[ch] != d)
							impl->playFnumChanges++;
						impl->playLastFnumHi[ch] = d;
						impl->playHaveFnum[ch] = 1;
					}
				}
			}
			/* SSG トーン周期 00/01, 02/03, 04/05。 */
			if (p == 0 && r <= 0x05) {
				const int ch = (int)(r >> 1);
				uint16_t per = impl->playLastSsgPeriod[ch];
				if (r & 1)
					per = (uint16_t)((per & 0x00ff) | ((d & 0x0f) << 8));
				else
					per = (uint16_t)((per & 0x0f00) | d);
				if (impl->playHaveSsg[ch] && per != impl->playLastSsgPeriod[ch]) {
					impl->playSsgPeriodChanges++;
					impl->playSsgPeriodChg[ch]++;
				}
				impl->playLastSsgPeriod[ch] = per;
				impl->playHaveSsg[ch] = 1;
			}
			if (p == 0 && r < 16) {
				impl->ssgRegs[r] = d;
				impl->ssgRegWrites[r]++;
				if (r == 10) {
					if (impl->ssgVolCHistN < 64)
						impl->ssgVolCHist[impl->ssgVolCHistN++] = d;
					if ((d & 0x1f) != 0x0f) impl->ssgVolCNon0F++;
					if ((d & 0x1f) == 0) impl->ssgVolCZero++;
					impl->ssgVolCSeen[d & 0x1f] = 1;
				}
				if (r == 7) {
					if (impl->ssgMixHistN < 64)
						impl->ssgMixHist[impl->ssgMixHistN++] = d;
					impl->ssgMixSeen[d >> 3] |= (uint8_t)(1u << (d & 7));
				}
			}
			impl->fmRegs[shadowAddr & 0x1FFu] = d;
		}
		FmMonShadowWriteReg(shadowAddr, rawData); /* FMモニタへOPNレジスタをシャドウ */
	}
}

void CEmuChipOpnaAdvanceClocks(CEmuChipOpna* c, uint64_t chipCycles)
{
	if (!c || !c->chip || chipCycles == 0) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	impl->dbgClockSum += chipCycles;
	/* タイマ/IRQのみ — PCMは Render/ChipSample のみで生成。
	   整数以外の補償は num/den + 残余。BIOS 2Fh の上に ×3 を重ねると
	   PC-98（ys2 など）が数倍速になる。 */
	const unsigned den = impl->timerClockScaleDen ? impl->timerClockScaleDen : 1u;
	impl->timerScaleResidual += chipCycles * (uint64_t)impl->timerClockScale;
	const uint64_t scaled = impl->timerScaleResidual / (uint64_t)den;
	impl->timerScaleResidual %= (uint64_t)den;
	if (scaled)
		impl->ExpireTimers((int64_t)scaled);
}

int CEmuChipOpnaIrq(const CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((CEmuChipOpnaImpl*)c->chip)->irqAsserted ? 1 : 0;
}

void CEmuChipOpnaAckIrq(CEmuChipOpna* c)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	/* ポート 0xE4 = PC-88 INT ack（hoot z80_lower_IRQ）。YM reg 0x27 はパルスしない —
	   KOEI FMDRV は OUT E4 のあとでタイマフラグを読む。新しい辺は周期ごとに
	   ExpireTimers から来る（フラグはスティッキーのまま）。 */
	impl->irqAsserted = 0;
}

uint8_t CEmuChipOpnaReadStatus(CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((CEmuChipOpnaImpl*)c->chip)->ReadStatus();
}

uint8_t CEmuChipOpnaReadData(CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((CEmuChipOpnaImpl*)c->chip)->ReadData();
}

uint8_t CEmuChipOpnaReadStatusHi(CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((CEmuChipOpnaImpl*)c->chip)->ReadStatusHi();
}

uint8_t CEmuChipOpnaReadDataHi(CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((CEmuChipOpnaImpl*)c->chip)->ReadDataHi();
}

void CEmuChipOpnaRender(CEmuChipOpna* c, int16_t* stereo, int frames)
{
	if (!c || !c->chip || !stereo || frames <= 0) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	for (int i = 0; i < frames; i++) {
		int64_t sumL = 0, sumR = 0;
		int nGen = 0;
		impl->chipAcc += (int64_t)impl->chipRate;
		/* chipRate は ymfm sample_rate（プリスケール後）。ホストへリサンプル。 */
		while (impl->chipAcc >= (int64_t)impl->hostRate) {
			impl->chipAcc -= (int64_t)impl->hostRate;
			impl->ChipSample();
			sumL += impl->curL;
			sumR += impl->curR;
			nGen++;
		}
		if (nGen > 0) {
			impl->curL = (int32_t)(sumL / nGen);
			impl->curR = (int32_t)(sumR / nGen);
		}
		/* ステレオMix: 生成済み L/R を 16bit へ飽和。 */
		stereo[i * 2] = (int16_t)CEmuClamp16(impl->curL);
		stereo[i * 2 + 1] = (int16_t)CEmuClamp16(impl->curR);
	}
}

void CEmuChipOpnaSetAdpcmRom(CEmuChipOpna* c, const uint8_t* data, unsigned size, unsigned destOffset)
{
	if (!c || !c->chip || !data || size == 0) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	if (destOffset >= sizeof(impl->adpcmRom)) return;
	unsigned n = size;
	if (destOffset + n > sizeof(impl->adpcmRom))
		n = (unsigned)sizeof(impl->adpcmRom) - destOffset;
	memcpy(impl->adpcmRom + destOffset, data, n);
	if (destOffset + n > impl->adpcmRomSize)
		impl->adpcmRomSize = destOffset + n;
}

void CEmuChipOpnaSetAdpcmB(CEmuChipOpna* c, const uint8_t* data, unsigned size, unsigned destOffset)
{
	if (!c || !c->chip || !data || size == 0) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	if (destOffset >= sizeof(impl->adpcmB)) return;
	unsigned n = size;
	if (destOffset + n > sizeof(impl->adpcmB))
		n = (unsigned)sizeof(impl->adpcmB) - destOffset;
	memcpy(impl->adpcmB + destOffset, data, n);
	if (destOffset + n > impl->adpcmBSize)
		impl->adpcmBSize = destOffset + n;
}

unsigned CEmuChipOpnaGetAdpcmRomSize(const CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((const CEmuChipOpnaImpl*)c->chip)->adpcmRomSize;
}

unsigned CEmuChipOpnaGetAdpcmBSize(const CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((const CEmuChipOpnaImpl*)c->chip)->adpcmBSize;
}

void CEmuChipOpnaSetTimerIrqPolicy(CEmuChipOpna* c, int allowTimerA)
{
	if (!c || !c->chip) return;
	((CEmuChipOpnaImpl*)c->chip)->allowTimerAIrq = allowTimerA ? 1 : 0;
}

void CEmuChipOpnaSetTimerClockScale(CEmuChipOpna* c, unsigned scale)
{
	CEmuChipOpnaSetTimerClockScaleRatio(c, scale ? scale : 1u, 1u);
}

void CEmuChipOpnaSetTimerClockScaleRatio(CEmuChipOpna* c, unsigned num, unsigned den)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	impl->timerClockScale = num ? num : 1u;
	impl->timerClockScaleDen = den ? den : 1u;
	impl->timerScaleResidual = 0;
}

void CEmuChipOpnaSetPitchRateDiv(CEmuChipOpna* c, unsigned div)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	impl->pitchRateDiv = div ? div : 1;
	impl->RefreshChipRate();
}

void CEmuChipOpnaSetPitchOctaveShift(CEmuChipOpna* c, int octaves)
{
	if (!c || !c->chip) return;
	if (octaves < -7) octaves = -7;
	if (octaves > 7) octaves = 7;
	((CEmuChipOpnaImpl*)c->chip)->pitchOctaveShift = octaves;
}

void CEmuChipOpnaSetCarrierFadeClamp(CEmuChipOpna* c, int enable)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	impl->carrierFadeClamp = enable ? 1 : 0;
	memset(impl->fadePatch, 0, sizeof(impl->fadePatch));
}

int32_t CEmuChipOpnaGetChipRate(const CEmuChipOpna* c)
{
	if (!c || !c->chip) return 0;
	return ((const CEmuChipOpnaImpl*)c->chip)->chipRate;
}

void CEmuChipOpnaGetTimerDebug(const CEmuChipOpna* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse)
{
	if (fireA) *fireA = 0;
	if (fireB) *fireB = 0;
	if (irqPulse) *irqPulse = 0;
	if (!c || !c->chip) return;
	const CEmuChipOpnaImpl* impl = (const CEmuChipOpnaImpl*)c->chip;
	if (fireA) *fireA = impl->dbgFireA;
	if (fireB) *fireB = impl->dbgFireB;
	if (irqPulse) *irqPulse = impl->dbgIrqPulse;
}

void CEmuChipOpnaGetTimerDebugEx(const CEmuChipOpna* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse,
	int64_t* lastDurB, uint64_t* clockSum)
{
	CEmuChipOpnaGetTimerDebug(c, fireA, fireB, irqPulse);
	if (lastDurB) *lastDurB = 0;
	if (clockSum) *clockSum = 0;
	if (!c || !c->chip) return;
	const CEmuChipOpnaImpl* impl = (const CEmuChipOpnaImpl*)c->chip;
	if (lastDurB) *lastDurB = impl->dbgLastDur[1];
	if (clockSum) *clockSum = impl->dbgClockSum;
}

void CEmuChipOpnaClearTimerDebug(CEmuChipOpna* c)
{
	if (!c || !c->chip) return;
	CEmuChipOpnaImpl* impl = (CEmuChipOpnaImpl*)c->chip;
	impl->dbgFireA = impl->dbgFireB = impl->dbgIrqPulse = 0;
	impl->dbgClockSum = 0;
}

void CEmuChipOpnaShutdown(CEmuChipOpna* c)
{
	if (!c) return;
	if (c->chip) {
		delete (CEmuChipOpnaImpl*)c->chip;
		c->chip = NULL;
	}
	c->ready = 0;
}

class CChipYm2608 : public CChip {
public:
	CChipYm2608(uint32_t clockHz, int opnaMode, int sampleRate)
	{
		memset(&core_, 0, sizeof(core_));
		CEmuChipOpnaInit(&core_, clockHz, opnaMode, sampleRate);
	}

	~CChipYm2608() override
	{
		CEmuChipOpnaShutdown(&core_);
	}

	void Reset() override { CEmuChipOpnaReset(&core_); }

	void Write(uint32_t addr, uint32_t data) override
	{
		CEmuChipOpnaWrite(&core_, addr, data);
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		CEmuChipOpnaAdvanceClocks(&core_, chipCycles);
	}

	void Render(int16_t* stereo, int frames) override
	{
		CEmuChipOpnaRender(&core_, stereo, frames);
	}

	bool Irq() const override { return CEmuChipOpnaIrq(&core_) != 0; }

	void AckIrq() override { CEmuChipOpnaAckIrq(&core_); }

	uint8_t ReadStatus() override { return CEmuChipOpnaReadStatus(&core_); }
	uint8_t ReadData() override { return CEmuChipOpnaReadData(&core_); }
	uint8_t ReadStatusHi() override { return CEmuChipOpnaReadStatusHi(&core_); }
	uint8_t ReadDataHi() override { return CEmuChipOpnaReadDataHi(&core_); }

	void SetAdpcmRom(const uint8_t* data, unsigned size, unsigned destOffset) override
	{
		CEmuChipOpnaSetAdpcmRom(&core_, data, size, destOffset);
	}

	void SetAdpcmB(const uint8_t* data, unsigned size, unsigned destOffset) override
	{
		CEmuChipOpnaSetAdpcmB(&core_, data, size, destOffset);
	}

	unsigned GetAdpcmRomSize() const override
	{
		return CEmuChipOpnaGetAdpcmRomSize(&core_);
	}

	unsigned GetAdpcmBSize() const override
	{
		return CEmuChipOpnaGetAdpcmBSize(&core_);
	}

	void SetTimerIrqPolicy(int allowTimerA) override
	{
		CEmuChipOpnaSetTimerIrqPolicy(&core_, allowTimerA);
	}

	void SetTimerClockScale(unsigned scale) override
	{
		CEmuChipOpnaSetTimerClockScale(&core_, scale);
	}

	void SetTimerClockScaleRatio(unsigned num, unsigned den) override
	{
		CEmuChipOpnaSetTimerClockScaleRatio(&core_, num, den);
	}

	void SetPitchRateDiv(unsigned div) override
	{
		CEmuChipOpnaSetPitchRateDiv(&core_, div);
	}

	void SetPitchOctaveShift(int octaves) override
	{
		CEmuChipOpnaSetPitchOctaveShift(&core_, octaves);
	}

	void SetCarrierFadeClamp(int enable) override
	{
		CEmuChipOpnaSetCarrierFadeClamp(&core_, enable);
	}

	void TimerDebug(unsigned* fireA, unsigned* fireB, unsigned* irqPulse) const
	{
		CEmuChipOpnaGetTimerDebug(&core_, fireA, fireB, irqPulse);
	}

	void TimerDebugEx(unsigned* fireA, unsigned* fireB, unsigned* irqPulse,
		int64_t* lastDurB, uint64_t* clockSum) const
	{
		CEmuChipOpnaGetTimerDebugEx(&core_, fireA, fireB, irqPulse, lastDurB, clockSum);
	}

	void TimerDebugClear()
	{
		CEmuChipOpnaClearTimerDebug(&core_);
	}

	void PlayMetrics(unsigned* writes, unsigned* keyOns, unsigned* fnumChg,
		unsigned* ssgChg, unsigned* chMask) const
	{
		const CEmuChipOpnaImpl* impl =
			core_.chip ? (const CEmuChipOpnaImpl*)core_.chip : NULL;
		if (writes) *writes = impl ? impl->playWrites : 0;
		if (keyOns) *keyOns = impl ? impl->playKeyOns : 0;
		if (fnumChg) *fnumChg = impl ? impl->playFnumChanges : 0;
		if (ssgChg) *ssgChg = impl ? impl->playSsgPeriodChanges : 0;
		if (chMask) *chMask = impl ? impl->playChMask : 0;
	}

	void SsgDebug(uint8_t regsOut[16], unsigned periodChg[3], uint64_t energy[3],
		unsigned* regWrites, uint8_t* volCHist, unsigned* volCHistN,
		uint8_t* mixHist, unsigned* mixHistN,
		unsigned* volCNon0F, unsigned* volCZero, uint8_t volCSeen[32]) const
	{
		const CEmuChipOpnaImpl* impl =
			core_.chip ? (const CEmuChipOpnaImpl*)core_.chip : NULL;
		if (regsOut) {
			if (impl) memcpy(regsOut, impl->ssgRegs, 16);
			else memset(regsOut, 0, 16);
		}
		for (int i = 0; i < 3; i++) {
			if (periodChg) periodChg[i] = impl ? impl->playSsgPeriodChg[i] : 0;
			if (energy) energy[i] = impl ? impl->ssgEnergy[i] : 0;
		}
		if (regWrites) {
			if (impl) memcpy(regWrites, impl->ssgRegWrites, 16 * sizeof(unsigned));
			else memset(regWrites, 0, 16 * sizeof(unsigned));
		}
		if (volCHist && volCHistN) {
			*volCHistN = impl ? impl->ssgVolCHistN : 0;
			if (impl && *volCHistN)
				memcpy(volCHist, impl->ssgVolCHist, *volCHistN);
		}
		if (mixHist && mixHistN) {
			*mixHistN = impl ? impl->ssgMixHistN : 0;
			if (impl && *mixHistN)
				memcpy(mixHist, impl->ssgMixHist, *mixHistN);
		}
		if (volCNon0F) *volCNon0F = impl ? impl->ssgVolCNon0F : 0;
		if (volCZero) *volCZero = impl ? impl->ssgVolCZero : 0;
		if (volCSeen) {
			if (impl) memcpy(volCSeen, impl->ssgVolCSeen, 32);
			else memset(volCSeen, 0, 32);
		}
	}

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		const CEmuChipOpnaImpl* impl =
			core_.chip ? (const CEmuChipOpnaImpl*)core_.chip : NULL;
		if (!buf || !impl) return 0;
		if (cap >= 512) {
			memcpy(buf, impl->fmRegs, 512);
			return 512;
		}
		if (cap >= 256) {
			memcpy(buf, impl->fmRegs, 256);
			return 256;
		}
		if (cap < 16) return 0;
		memcpy(buf, impl->ssgRegs, 16);
		return 16;
	}

private:
	CEmuChipOpna core_;
};

/* YM2608/YM2203 の CChip ラッパ生成。 */
CChip* CEmuChipYm2608Create(uint32_t clockHz, int opnaMode, int sampleRate)
{
	return new CChipYm2608(clockHz, opnaMode, sampleRate);
}

void CEmuChipYm2608Destroy(CChip* c)
{
	delete c;
}

void CEmuChipYm2608GetTimerDebug(CChip* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse)
{
	if (!c) {
		if (fireA) *fireA = 0;
		if (fireB) *fireB = 0;
		if (irqPulse) *irqPulse = 0;
		return;
	}
	static_cast<CChipYm2608*>(c)->TimerDebug(fireA, fireB, irqPulse);
}

void CEmuChipYm2608GetTimerDebugEx(CChip* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse,
	int64_t* lastDurB, uint64_t* clockSum)
{
	if (!c) {
		if (fireA) *fireA = 0;
		if (fireB) *fireB = 0;
		if (irqPulse) *irqPulse = 0;
		if (lastDurB) *lastDurB = 0;
		if (clockSum) *clockSum = 0;
		return;
	}
	static_cast<CChipYm2608*>(c)->TimerDebugEx(fireA, fireB, irqPulse, lastDurB, clockSum);
}

void CEmuChipYm2608ClearTimerDebug(CChip* c)
{
	if (!c) return;
	static_cast<CChipYm2608*>(c)->TimerDebugClear();
}

void CEmuChipYm2608GetPlayMetrics(CChip* c, unsigned* writes, unsigned* keyOns,
	unsigned* fnumChg, unsigned* ssgChg, unsigned* chMask)
{
	if (!c) {
		if (writes) *writes = 0;
		if (keyOns) *keyOns = 0;
		if (fnumChg) *fnumChg = 0;
		if (ssgChg) *ssgChg = 0;
		if (chMask) *chMask = 0;
		return;
	}
	static_cast<CChipYm2608*>(c)->PlayMetrics(writes, keyOns, fnumChg, ssgChg, chMask);
}

void CEmuChipYm2608GetSsgDebug(CChip* c, uint8_t regsOut[16], unsigned periodChg[3],
	uint64_t energy[3])
{
	CEmuChipYm2608GetSsgDebugEx(c, regsOut, periodChg, energy, NULL);
}

void CEmuChipYm2608GetSsgDebugEx(CChip* c, uint8_t regsOut[16], unsigned periodChg[3],
	uint64_t energy[3], unsigned regWrites[16])
{
	if (!c) {
		if (regsOut) memset(regsOut, 0, 16);
		if (periodChg) periodChg[0] = periodChg[1] = periodChg[2] = 0;
		if (energy) energy[0] = energy[1] = energy[2] = 0;
		if (regWrites) memset(regWrites, 0, 16 * sizeof(unsigned));
		return;
	}
	static_cast<CChipYm2608*>(c)->SsgDebug(regsOut, periodChg, energy, regWrites,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

void CEmuChipYm2608GetSsgWriteHist(CChip* c, uint8_t* volCHist, unsigned* volCHistN,
	uint8_t* mixHist, unsigned* mixHistN)
{
	unsigned non0f = 0, zero = 0;
	uint8_t seen[32];
	if (!c) {
		if (volCHistN) *volCHistN = 0;
		if (mixHistN) *mixHistN = 0;
		return;
	}
	static_cast<CChipYm2608*>(c)->SsgDebug(NULL, NULL, NULL, NULL,
		volCHist, volCHistN, mixHist, mixHistN, &non0f, &zero, seen);
}

void CEmuChipYm2608GetSsgVolCStats(CChip* c, unsigned* non0F, unsigned* zero,
	uint8_t seen[32])
{
	if (!c) {
		if (non0F) *non0F = 0;
		if (zero) *zero = 0;
		if (seen) memset(seen, 0, 32);
		return;
	}
	static_cast<CChipYm2608*>(c)->SsgDebug(NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, non0F, zero, seen);
}
