#pragma once
// timerp() の総尺・現在位置（ttt）と同じ式で残り ms を出す。
// クロスフェード開始判定はここだけを見る（oggsize バイト換算の独自式や oldw キューは使わない）。

inline void SongHeardSec_FromGlobals(
	int mode,
	__int64 playbFrames,
	int oggsize,
	int loop2,
	int rate,
	int ch,
	int depthBits,
	long qSamplesHeard,
	double& outTotalSec,
	double& outHeardSec)
{
	outTotalSec = 0.0;
	outHeardSec = 0.0;
	if (rate <= 0) rate = 44100;
	if (ch < 1) ch = 1;
	if (ch > 6) ch = 6;
	const double wavv[] = { 0, 1.0, 2.0, 3.0 / 0.75, 4.0 / 0.75, 5.0 / 0.75, 6.0 / 0.75 };
	const double wavv2[] = { 0, 2.0, 1.0, 2.0 / 3.0, 2.0 / 4.0, 2.0 / 5.0, 2.0 / 6.0 };
	const double depth = (depthBits > 0) ? ((double)depthBits / 16.0) : 1.0;

	// 総尺: timerp と同じ
	if (mode == -10) {
		if (oggsize > 0)
			outTotalSec = (double)oggsize / (double)rate;
	}
	else if (loop2 > 0) {
		// FLAC/WAV/OGG: スライダー範囲 = loop2 フレーム（PublishSongTiming 後も同じ）
		outTotalSec = (double)loop2 / (double)rate;
	}
	else if (oggsize > 0) {
		double d = depth;
		if (d < 0.25) d = 0.25;
		outTotalSec = (double)oggsize / ((double)rate * 2.0 * wavv[ch] * d);
		if (mode == -9 && ch > 2) outTotalSec *= (double)ch / 2.0;
	}

	// 現在: timerp と同じ（playb から DS キュー分を引く）
	__int64 heard = playbFrames;
	if (qSamplesHeard > 0) {
		if (heard > qSamplesHeard) heard -= qSamplesHeard;
		else heard = 0;
	}
	if (mode == -10) {
		outHeardSec = (double)heard / (double)rate;
	}
	else {
		outHeardSec = (double)heard / ((double)rate / wavv2[ch]);
		if (mode == -9 && ch > 2) outHeardSec *= (double)ch / 2.0;
	}
	if (outHeardSec < 0.0) outHeardSec = 0.0;
}

inline __int64 SongRemainMs_FromSecs(double totalSec, double heardSec)
{
	if (totalSec <= heardSec || totalSec <= 0.0) return 0;
	return (__int64)((totalSec - heardSec) * 1000.0 + 0.5);
}

// スロット内部は常に PCM フレーム。出力キューは時間に換算して引く。
inline void SongHeardSec_FromSlot(
	__int64 lengthFrames,
	__int64 playbFrames,
	int srcRate,
	__int64 queuedOutBytes,
	int outBpf,
	int outRate,
	double& outTotalSec,
	double& outHeardSec)
{
	outTotalSec = 0.0;
	outHeardSec = 0.0;
	if (srcRate <= 0) srcRate = 44100;
	if (lengthFrames > 0)
		outTotalSec = (double)lengthFrames / (double)srcRate;
	outHeardSec = (double)playbFrames / (double)srcRate;
	if (outBpf > 0 && outRate > 0 && queuedOutBytes > 0) {
		const double qSec = (double)queuedOutBytes / ((double)outBpf * (double)outRate);
		outHeardSec -= qSec;
	}
	if (outHeardSec < 0.0) outHeardSec = 0.0;
}
