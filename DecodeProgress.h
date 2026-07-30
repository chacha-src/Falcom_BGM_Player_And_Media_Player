#pragma once

// PCMデコード進捗(WAV/mp3/FLAC書き出し・プロンプト解析で共用)

typedef void (*MpDecodeProgressCb)(int percent /*0..100*/, LPCTSTR status, void* user);

void MpDecodeProgressSetCb(MpDecodeProgressCb cb, void* user);
void MpDecodeProgressClearCb();
void MpDecodeProgressReset();
void MpDecodeProgressSetExpectedSec(double sec);
// 複数曲: 全体のうち base..(base+span) にマップ (例: 2/5曲目なら base=40, span=20)
void MpDecodeProgressSetSegment(int basePct, int spanPct);
// PCMデコード段階の上限% (既定95。mp3/FLACはエンコード余白のため 78 など)
void MpDecodeProgressSetPcmCap(int maxPct);
void MpDecodeProgressReport(int pct, LPCTSTR status);
// PCM段階直後(後処理開始など)。pcmCap+4 付近へ進める(最大99)
void MpDecodeProgressBumpAfterPcm(LPCTSTR status);
void MpDecodeProgressOnPcm(UINT nbytes, int rate, int ch, int bits);
