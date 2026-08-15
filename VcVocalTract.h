#pragma once
#include <vector>

// =============================================================================
// 本格ボイスチェンジコア（MIT: Signalsmith Stretch を正規利用）
//  third_party/NOTICE.txt に帰属表示
// =============================================================================
struct VcVocalParams {
	float pitch;   // F0 倍率
	float formant; // フォルマント倍率
	float gain;
	float bright;
	float breath;
	int style;
	int quality;
};

class VcVocalTract {
public:
	VcVocalTract();
	~VcVocalTract();
	void Reset(int sampleRate, int quality);
	void Process(const float* in, float* out, int n, const VcVocalParams& p);

	int SampleRate() const { return m_rate; }
	float LastPeriodConf() const { return 1.f; }

private:
	struct Impl;
	Impl* m_impl;
	int m_rate;
	int m_quality;
	float m_toneLp, m_toneHp, m_brightState;
	double m_robotPh;
	unsigned m_noise;
	float m_lastPitch, m_lastFormant;
};

// savedata.vc_* からパラメータを構築。効果が実質オフなら false。
bool VcParamsFromSavedata(VcVocalParams& out);
// インターリーブ float マイクに VC を適用（L/R はモノラル処理して両chへ）
void VcProcessInterleavedStereo(VcVocalTract& tract, float* interleavedLR, int frames, int sampleRate, const VcVocalParams& p);
