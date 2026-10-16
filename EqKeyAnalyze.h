#pragma once

// 再生中 PCM からのコード推定と、歌唱時だけの母音ラベル。
// ヒープは使わない。呼び出しは EQ キーワーカーの単一スレッドに限る。

struct EqKeyVocalRaw {
	wchar_t vowel;       // 0、または あいうえお
	float f0Hz;
	int chorus;          // 歌唱帯域に独立した声が二つ以上
	float f1Hz;
	float f2Hz;
	float barkDist;      // 母音テンプレートまでの距離。大きいほど母音らしくない
	float aperiodicity;  // YIN の正規化差分。0 が周期的
	float centroidHz;
	int nHarm;
};

void EqKeyVocalReset();
void EqKeyVocalAnalyze(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate, EqKeyVocalRaw* out);
// 無音や非歌唱は L"[   ]"。歌唱の基音が数フレーム続いたときだけ L"C4" / コーラスは L"+C4"。
void EqKeyVocalMelody(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate, int isSilent, wchar_t* melodyOut, int melodyCap);

// strength[108] を倍音加算してから低/中/高/全のピッチクラスへ畳む。
void EqKeyNotesToChroma(const float* strength108, float* bass12, float* mid12, float* high12, float* all12);

// 12 ルートすべてをコード型と照合する。bassPrior12 は全帯域のときの低域クロマ（なくてもよい）。
void EqKeyEstimateChord(const float* noteClass12, float threshold, const wchar_t* prev, const float* bassPrior12, wchar_t* out, int outCap);

// CCustomStatic の !@B / !@I / !@Crrggbb / !@F±nn と同じ区切りでタグを除く。
const wchar_t* EqKeySkipMarkup(const wchar_t* p);
void EqKeyStripMarkup(const wchar_t* src, wchar_t* dst, int dstCap);
// 先頭コードがマイナー（m / m7 / dim）なら 1。M7 はメジャー。
int EqKeyPrimaryIsMinor(const wchar_t* markupOrPlain);
