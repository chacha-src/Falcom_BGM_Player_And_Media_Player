#pragma once
/* KPI FM モニタ向けの共有 OPNA/OPN2/MSX レジスタシャドウ + dump 書き出し */
#include "sasami_fmmon.h"
#include "fmmon/fmmon_write.h"

#ifdef __cplusplus
extern "C" {
#endif

void FmMonShadowReset(void);
/* Overlay SE の open/render 中は BGM モニタを保持。入れ子可 */
void FmMonShadowHold(int on);
int FmMonShadowIsHeld(void);
void FmMonShadowSetSource(const wchar_t* path);
void FmMonShadowSetSampleRate(uint32_t sr);
void FmMonShadowAddSamples(uint32_t n);
/* ループ/シーク時の時計引き継ぎ用。UI は dump.curSample を DS の可聴位置
   (g_heardBytes 由来) と比べる。可聴側はループで 0 に戻らないので、
   Reset で時計だけ 0 にすると以後ずっと可聴より先の dump を出してしまう。 */
uint64_t FmMonShadowGetCurSample(void);
void FmMonShadowSetCurSample(uint64_t n);
/* FM モニタヘッダ用の platform + チップ基底ラベル (例 "PC-88","OPNA")。
   Flush は必要なら +EX/+ADPCM を付ける。dump.titleSjis に格納。 */
void FmMonShadowSetIdentity(const char* platform, const char* chip);
/* モニタが表示するチップ名を読み戻す。プローブが画面上の音源モードと
   実際に配線した基板を突き合わせるため。 */
void FmMonShadowGetIdentity(char* platform, unsigned platformLen,
	char* chip, unsigned chipLen);
/* 1=OPNA(6ch+ADPCM)  0=OPN(3ch)  2=YM2610/OPNB(4ch+SSG+ADPCM-A/B)  -1=非 OPN(A) */
void FmMonShadowSetOpnaLayout(int layout);
/* addr: 0x000-0x1FF (port1 = 0x100|reg)。00→00 も「書いた」と記録する。 */
void FmMonShadowWriteReg(unsigned addr, unsigned data);
/* OPM など snapshot 経路で、実際に書いた番地だけ sticky にする（00→00 含む）。 */
void FmMonShadowMarkRegWrite(unsigned addr);
/* AY-3-8910 レジスタ書き込み → SSG シャドウ ($00-$0F) */
void FmMonShadowWriteAyReg(unsigned reg, unsigned data);
/* ソフト MIDI / 鍵盤のみ: ch 0..15, midiNote 0..127 */
void FmMonShadowMidiNote(int ch, int midiNote, int on);
/* 鍵盤専用へ入る（OPL/MSX を消す）。PC/AT BEEP / CMS / MPU が使う */
void FmMonShadowEnterKeysOnly(unsigned profile);
/* 鍵盤専用 dump の VIEW_REGS 用 aux レジスタ (PIT/SAA/…) */
void FmMonShadowWriteAuxReg(unsigned addr, unsigned data);
/* 鍵盤/ハイブリッド dump の追加 PCM / PDX / ADPCM スロット (0..PCM_MAX-1) */
void FmMonShadowPcmNote(int ch, int midiNote, int on);
/* サンプル SPU のピッチレート (0x1000 = 等倍) → MIDI 0..127。無効なら <0 */
int FmMonShadowPitchRateToMidi(unsigned pitchRate);
/* Hz → MIDI 0..127。無効なら <0 (NSF/SID 系) */
int FmMonShadowHzToMidi(double freqHz);
/* dirty かつ約 4ms 経過（または force）なら dump を Flush */
void FmMonShadowFlush(int force);
/* MIDI シャドウから KEYSONLY dump（レジスタ無し） */
void FmMonShadowFlushKeysOnly(int force);
/* pad6[1] 鍵盤 UI プロファイル (SASAMI_FMMON_KEYS_*)。FlushKeysOnly の前に呼ぶ */
void FmMonShadowSetKeysProfile(unsigned profile);
/* OPM (YM2151) 256 レジスタ snapshot。MDX 用。dump.regs[0..255] + FLAG_OPM。
   keyRegOrNeg1: $08 書き込みデータを渡すとその ch のゲートをラッチ。-1 はレジスタのみ。 */
void FmMonShadowSetOpmRegSnapshot(const unsigned char* regs256);
void FmMonShadowSetOpmRegSnapshotEx(const unsigned char* regs256, int keyRegOrNeg1);
void FmMonShadowApplyK054539Reg(unsigned ofs, unsigned data8);

/* MSX KSS: AY クロック (Hz)。未設定時は OPNA 相当 */
void FmMonShadowSetSsgClock(unsigned clockHz);
/* SSG バンク ($00-$0F) と、そこから導いたゲート/音程を読み戻す。
   ハイブリッド基板で他チップ snapshot のあと SSG 行が残るかプローブする。 */
void FmMonShadowDebugSsg(unsigned char regs16[16], unsigned char ssgOn[3],
	unsigned char ssgMidi[3]);
/* MSX プロファイル: dumpFlags の MSX + pad6[0] の deviceMask */
void FmMonShadowSetMsxDevices(unsigned deviceMask);
/* YM2413/FMPAC: 64 レジスタ。ch0-5→FM、ch6-8→EX */
void FmMonShadowApplyOpllRegs(const unsigned char* reg64);
/* Konami SCC: freq[5] 12bit, vol[5] 0..15, enableMask bits0-4 */
void FmMonShadowApplyScc(const unsigned* freq12, const unsigned* vol4, unsigned enableMask);
/* HuC6280 HES: 6ch。period[6] 12bit, vol[6] 0..31, control[6] (bit7=on)。
   ch0-2 → SSG, ch3-5 → pcm (UI は SSG4-6)。DEV_PSG|DEV_HES を立てる。 */
void FmMonShadowApplyHes(const unsigned* period12, const unsigned* vol5, const unsigned* control);

/* SN76489 / SMS PSG: tonePeriod[3] 10bit, noisePeriod 10bit (または 0),
   vol[4] 0..15 (チップ尺度: 0=大音量), toneOnMask bits0-2 (+bit3 ノイズ) */
void FmMonShadowApplySn76489(const unsigned* tonePeriod10, unsigned noisePeriod,
	const unsigned* vol4, unsigned onMask);
/* SN76489 書き込みバイトのストリームパーサ (latch/data) */
void FmMonShadowWriteSnByte(unsigned data);

/* OPL2/3 (DRO / S98 OPL*)。mode: 1=OPL2, 2=OPL3, 3=DualOPL2 (2×OPL2 をバンク扱い)。
   WriteOplReg: addr bit8 = port/chip バンク、下位 8 = レジスタ。 */
void FmMonShadowSetOplMode(unsigned mode);
void FmMonShadowWriteOplReg(unsigned addr, unsigned data);

/* アーケード / VGM PCM チップ → 鍵盤専用 PCM 行 (プロファイルは自動設定) */
void FmMonShadowApplyQSoundReg(unsigned ofs, unsigned data16);
void FmMonShadowApplyRf5cReg(unsigned ofs, unsigned data8);
void FmMonShadowSetC352Clock(unsigned clockHz);
int FmMonShadowC352PitchToMidi(unsigned pitch);
void FmMonShadowApplyC352Reg(unsigned ofs, unsigned data16);
void FmMonShadowApplySegaPcmMem(unsigned addr, unsigned data8);
void FmMonShadowApplyOki6295(unsigned data8);
void FmMonShadowApplyGa20Reg(unsigned ofs, unsigned data8);
/* xxxx+yyyy の yyyy を 256B に圧縮して bank1 または bank2 へ。n>256 は先頭だけ。 */
void FmMonShadowSetCompanionRegs(const uint8_t* data, unsigned nbytes);
/* YMW258 MultiPCM: chipId 0/1, port 0=data 1=slot 2=reg */
void FmMonShadowApplyMultiPcm(int chipId, unsigned port, unsigned data8);

#ifdef __cplusplus
}
#endif
