#pragma once
/* ogg 側のコピー。共有メモリの並びは kpi 側の kbs_gimic と同じにする。 */
/* G.I.M.I.C : kbsasami_host32 between the KPI and the exe, then c86ctl.dll.
   Shared memory layout is identical on 32 and 64. No pointers. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	KBS_GIMIC_CHIP_OPNA = 1, /* YM2203 / YM2608 / YMF288 / YM2612 FM */
	KBS_GIMIC_CHIP_OPM = 2,
	KBS_GIMIC_CHIP_OPL = 3,
	KBS_GIMIC_CHIP_OPLL = 4
};

void KbsGimicSetEnable(int on);
void KbsGimicBegin(int chip, uint32_t clockHz, uint32_t sampleRate);
void KbsGimicWrite(uint32_t addr, uint8_t data, int64_t sample);
/* ADPCM データ。ホストが BRDY を見てから書く。キューが空くまで捨てない。 */
void KbsGimicWritePaced(uint32_t addr, uint8_t data, int64_t sample);
/* 転送中は FM/SSG も捨てずに待つ。ms。平常は 40。 */
void KbsGimicSetQueueWait(int ms);
void KbsGimicAddSamples(uint32_t n);
void KbsGimicSetSample(int64_t sample);
void KbsGimicWriteNow(uint32_t addr, uint8_t data);
void KbsGimicSetBurst(int on);
/* ソフト音源と混ぜるとき、出力バッファ分だけチップを遅らせる。サンプル数。0 で戻す。 */
void KbsGimicSetExtraLag(int64_t samples);
/* 未再生のキューだけ捨て、鳴っている音は切らない。次の時刻合わせをやり直す。 */
void KbsGimicDropPending(void);
/* 転送がホスト側で空になるまで待つ。ms で打ち切る。 */
void KbsGimicWaitDrained(int ms);
void KbsGimicEnd(void);
/* 1 = チップが開いていてレジスタを受け取っている。 */
int KbsGimicDevOk(void);
/* 1 = ソフト PCM を直流に置き換えた（初期化中の resetReq も含む）。チップ無しは 0。 */
int KbsGimicSilence(void* interleavedS16, uint32_t frames, int64_t playSample, uint32_t sampleRate);

void KbsGimicHostStart(void);
void KbsGimicHostStop(void);
int KbsGimicHostBusy(void);

#ifdef __cplusplus
}
#endif
