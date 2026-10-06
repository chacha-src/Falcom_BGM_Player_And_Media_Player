#include "stdafx.h"
#include "AudioUpscaler.h"
#include "XfadePlayback.h"
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioUpscaler g_audioUpscalerArr[2];
AudioUpscaler& ActiveAudioUpscaler()
{
	return g_audioUpscalerArr[XfDecSlot()];
}
int g_ds_pcm_ch = 2;
int g_ds_pcm_rate = 44100;
int g_ds_pcm_bits = 16;
int g_pcm_upscale_active = 0;
// (10240*6/2)*5 と mp3.h の BUFSZ*OUTPUT_BUFFER_NUM を一致させる
static const ULONG kDsBaseRingBytes = (ULONG)((10240 * 6 / 2) * 5);
ULONG g_ds_buffer_bytes = kDsBaseRingBytes;

static void RefreshDsBufferBytesFromFormat()
{
	const int outBps = (g_ds_pcm_bits >= 8) ? (g_ds_pcm_bits / 8) : 2;
	const int outAlign = g_ds_pcm_ch * outBps;
	const int refAlign = 2 * 2; // レガシー基準: ステレオ 16bit フレーム
	if (outAlign <= 0 || refAlign <= 0) {
		g_ds_buffer_bytes = kDsBaseRingBytes;
		return;
	}
	int rate = g_ds_pcm_rate;
	if (rate < 8000)
		rate = 44100;
	ULONG frames = kDsBaseRingBytes / (ULONG)refAlign;
	// 44100Hz 基準のリング秒数を全レートで維持（高SRで過去読み不足→簡易ピアノロール/スペアナ早出しを防ぐ）
	frames = (ULONG)(((uint64_t)frames * (uint64_t)rate + 22050ULL) / 44100ULL);
	ULONG bytes = frames * (ULONG)outAlign;
	// bufwav3[ kBase * 8 ] 上限（8ch×32bit まで想定）
	const ULONG cap = kDsBaseRingBytes * 8;
	if (bytes > cap)
		bytes = (cap / (ULONG)outAlign) * (ULONG)outAlign;
	g_ds_buffer_bytes = bytes ? bytes : kDsBaseRingBytes;
}

void ResetAudioUpscalerPipeline()
{
	g_audioUpscalerArr[XfDecSlot()].Reset();
}

int AudioUpscaler::BufferedFrames() const
{
	if (!m_active || m_srcCh < 1 || m_fifoCount < m_srcCh)
		return 0;
	const int frames = m_fifoCount / m_srcCh;
	const int consumed = (m_readPos > 0.0) ? (int)m_readPos : 0;
	const int ahead = frames - consumed;
	return (ahead > 0) ? ahead : 0;
}

int SpeakerLayoutToOutChannels(int layout)
{
	switch (layout) {
	case 1: return 3;  // 2.1
	case 2: return 4;  // 4.0
	case 3: return 6;  // 5.1
	case 4: return 8;  // 7.1
	case 5: return 2;  // 未使用（ConfigurePlaybackOutputAndUpscaler でソースchを使う）
	default: return 2; // 2.0
	}
}

std::uint32_t DirectSoundChannelMaskForOutput(int outCh, int speaker_layout)
{
	if (outCh <= 0)
		outCh = 2;
	if (outCh > 8)
		outCh = 8;
	switch (outCh) {
	case 1:
		return SPEAKER_FRONT_CENTER;
	case 2:
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
	case 3:
		// 本アプリでは 3ch = 2.1 (L/R/LFE)。LRC にはしない。
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY;
	case 4:
		// 4 出力は常にクアッド4マスク（従来の else 枝は5ビットになり nChannels と g_ds_pcm_ch が食い違って落ちる）
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
	case 5:
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER
			| SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
	case 6:
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER
			| SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
	case 7:
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER
			| SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_LEFT | SPEAKER_LOW_FREQUENCY;
	default:
		return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER
			| SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT | SPEAKER_LOW_FREQUENCY;
	}
}

CString ChannelLayoutLabel(int ch)
{
	switch (ch) {
	case 1: return L"mono";
	case 2: return L"stereo";
	case 3: return L"2.1ch";
	case 4: return L"4ch";
	case 5: return L"4.1ch";
	case 6: return L"5.1ch";
	case 7: return L"6.1ch";
	case 8: return L"7.1ch";
	default: {
		CString s;
		s.Format(L"%dch", ch);
		return s;
	}
	}
}

const wchar_t* AudioUpscaleFlowSymbol()
{
	return L"\u2726"; // ✦（スクロール装飾のダイヤと同系）
}

CString FormatAudioPlaybackSpec(int rateHz, int ch, int bits)
{
	if (rateHz <= 0 || ch <= 0)
		return CString();
	const CString chStr = ChannelLayoutLabel(ch);
	const int b = abs(bits);
	CString s;
	if (b > 0)
		s.Format(L"%d Hz  %s  %d bit", rateHz, (LPCTSTR)chStr, b);
	else
		s.Format(L"%d Hz  %s", rateHz, (LPCTSTR)chStr);
	return s;
}

CString FormatAudioPlaybackDisplay(int srcRate, int srcCh, int srcBits)
{
	const CString src = FormatAudioPlaybackSpec(srcRate, srcCh, srcBits);
	if (src.IsEmpty() || !g_pcm_upscale_active)
		return src;
	const int dstBits = g_ds_pcm_bits;
	if (srcRate == g_ds_pcm_rate && srcCh == g_ds_pcm_ch && abs(srcBits) == dstBits)
		return src;
	const CString dst = FormatAudioPlaybackSpec(g_ds_pcm_rate, g_ds_pcm_ch, dstBits);
	if (dst.IsEmpty())
		return src;
	CString out;
	out.Format(L"%s  %s  %s", (LPCTSTR)src, AudioUpscaleFlowSymbol(), (LPCTSTR)dst);
	return out;
}

float AudioUpscaler::s_fifoPool[AudioUpscaler::kFifoPool][AudioUpscaler::kUpFifoCap];
int AudioUpscaler::s_fifoPoolUsed;

AudioUpscaler::AudioUpscaler()
{
	const int i = s_fifoPoolUsed++;
	if (i >= 0 && i < kFifoPool) {
		m_fifo = s_fifoPool[i];
		m_fifoOwned = false;
	} else {
		m_fifo = new float[kUpFifoCap]();
		m_fifoOwned = true;
	}
}

AudioUpscaler::~AudioUpscaler()
{
	if (m_fifoOwned)
		delete[] m_fifo;
}

void AudioUpscaler::Configure(int srcRate, int srcCh, int srcBits,
	int dstRate, int dstCh, int dstBits)
{
	if (srcRate < 1) srcRate = 44100;
	if (dstRate < 1) dstRate = 44100;
	if (srcCh < 1) srcCh = 2;
	if (dstCh < 1) dstCh = 2;
	srcBits = abs(srcBits);
	dstBits = abs(dstBits);
	if (!(srcBits == 8 || srcBits == 16 || srcBits == 24 || srcBits == 32)) srcBits = 16;
	if (!(dstBits == 16 || dstBits == 24 || dstBits == 32)) dstBits = 16;

	const bool wantActive = (srcRate != dstRate || srcCh != dstCh || srcBits != dstBits)
		&& !(srcCh > kUpChMax || dstCh > kUpChMax);
	/* 同じ入出力なら FIFO を捨てない（混合中に毎バッファ Configure するとノイズフェードになる） */
	if (m_srcRate == srcRate && m_srcCh == srcCh && m_srcBits == srcBits
		&& m_dstRate == dstRate && m_dstCh == dstCh && m_dstBits == dstBits
		&& m_active == wantActive) {
		RefreshDsBufferBytesFromFormat();
		return;
	}

	m_srcRate = srcRate;
	m_srcCh = srcCh;
	m_srcBits = srcBits;
	m_dstRate = dstRate;
	m_dstCh = dstCh;
	m_dstBits = dstBits;

	m_bitDepthEnhance = (srcRate == dstRate && srcCh == dstCh && dstBits > srcBits);
	m_active = wantActive;
	if (srcCh > kUpChMax || dstCh > kUpChMax)
		m_active = false;
	if (!m_active) {
		m_fifoHead = 0;
		m_fifoCount = 0;
		m_readPos = 0.0;
	}
	else {
		Reset();
	}
	RefreshDsBufferBytesFromFormat();
}

void AudioUpscaler::Reset()
{
	m_fifoHead = 0;
	m_fifoCount = 0;
	m_readPos = 0.0;
	m_ditherRng = 0xC0FFEE01u;
}

namespace {
	static inline float LanczosKernel2(float x)
	{
		if (x == 0.0f) return 1.0f;
		if (fabsf(x) >= 2.0f) return 0.0f;
		const float pix = (float)M_PI * x;
		return (sinf(pix) / pix) * (sinf(pix * 0.5f) / (pix * 0.5f));
	}

	static inline float TpdfUnit(uint32_t& rng)
	{
		rng = rng * 1664525u + 1013904223u;
		const float a = (float)(rng & 0xFFFFu) / 65536.0f;
		rng = rng * 1664525u + 1013904223u;
		const float b = (float)(rng & 0xFFFFu) / 65536.0f;
		return (a + b) - 1.0f;
	}

	static inline float DitherAmplitudeFloat(int dstBits)
	{
		if (dstBits >= 32) return 1.0f / 2147483648.0f;
		if (dstBits >= 24) return 1.0f / 8388608.0f;
		return 1.0f / 32768.0f;
	}
}

void AudioUpscaler::EnsureConfigured() const
{
}

int AudioUpscaler::FloatToPcm(const float* interleaved, int nFrames, int ch, int srcBits, int dstBits, uint8_t* dst, uint32_t& rng)
{
	const int n = nFrames * ch;
	const bool expandBits = (dstBits > srcBits);
	const float ditherAmp = expandBits ? DitherAmplitudeFloat(dstBits) : 0.0f;

	if (dstBits == 16) {
		int16_t* o = (int16_t*)dst;
		for (int i = 0; i < n; ++i) {
			double x = interleaved[i];
			if (expandBits) x += (double)TpdfUnit(rng) * (double)ditherAmp;
			if (x > 1.0) x = 1.0;
			else if (x < -1.0) x = -1.0;
			int v = (int)floor(x * 32767.0 + 0.5);
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			o[i] = (int16_t)v;
		}
		return n * 2;
	}
	if (dstBits == 24) {
		int o = 0;
		for (int i = 0; i < n; ++i) {
			double x = interleaved[i];
			if (expandBits) x += (double)TpdfUnit(rng) * (double)ditherAmp;
			if (x > 1.0) x = 1.0;
			else if (x < -1.0) x = -1.0;
			int v = (int)floor(x * 8388607.0 + 0.5);
			if (v > 8388607) v = 8388607;
			if (v < -8388608) v = -8388608;
			dst[o++] = (uint8_t)(v & 0xFF);
			dst[o++] = (uint8_t)((v >> 8) & 0xFF);
			dst[o++] = (uint8_t)((v >> 16) & 0xFF);
		}
		return n * 3;
	}
	// 32
	int32_t* o = (int32_t*)dst;
	for (int i = 0; i < n; ++i) {
		double x = interleaved[i];
		if (expandBits) x += (double)TpdfUnit(rng) * (double)ditherAmp;
		if (x > 1.0) x = 1.0;
		else if (x < -1.0) x = -1.0;
		int64_t v = (int64_t)floor(x * 2147483647.0 + 0.5);
		if (v > 2147483647LL) v = 2147483647LL;
		if (v < -2147483648LL) v = -2147483648LL;
		o[i] = (int32_t)v;
	}
	return n * 4;
}

float AudioUpscaler::SampleInputLanczos(int ch, double posFrames) const
{
	const int64_t totalFrames = (m_srcCh > 0) ? (int64_t)(m_fifoCount / m_srcCh) : 0;
	if (totalFrames <= 0) return 0.0f;
	double x = posFrames;
	if (x < 0.0) x = 0.0;
	if (x > (double)totalFrames - 1.0) x = (double)totalFrames - 1.0;
	const int i0 = (int)floor(x);
	const float frac = (float)(x - floor(x));
	auto getS = [&](int frameIdx, int c) -> float {
		if (frameIdx < 0) frameIdx = 0;
		if (frameIdx >= (int)totalFrames) frameIdx = (int)totalFrames - 1;
		const int sample = frameIdx * m_srcCh + c;
		return m_fifo[(m_fifoHead + sample) % kUpFifoCap];
	};
	double sum = 0.0;
	double wsum = 0.0;
	for (int j = -2; j <= 2; ++j) {
		const float w = LanczosKernel2(frac - (float)j);
		if (w == 0.0f) continue;
		sum += (double)getS(i0 + j, ch) * (double)w;
		wsum += (double)w;
	}
	if (wsum <= 1e-12) return getS(i0, ch);
	float y = (float)(sum / wsum);
	if (y > 1.0f) y = 1.0f;
	else if (y < -1.0f) y = -1.0f;
	return y;
}

// 同一レートでビット深度のみ上げる場合: サブサンプル位相の Lanczos 合成で
// 量子化格子の間を補間し、24/32bit 出力に意味のある小数部を載せる。
float AudioUpscaler::SampleInputBitEnhanced(int ch, double posFrames) const
{
	const float c0 = SampleInputLanczos(ch, posFrames - 0.5);
	const float c1 = SampleInputLanczos(ch, posFrames);
	const float c2 = SampleInputLanczos(ch, posFrames + 0.5);
	return c0 * 0.25f + c1 * 0.5f + c2 * 0.25f;
}

float AudioUpscaler::SampleInput(int ch, double posFrames) const
{
	if (m_bitDepthEnhance)
		return SampleInputBitEnhanced(ch, posFrames);
	return SampleInputLanczos(ch, posFrames);
}

// ステレオ → マルチch: 5.1→2ch ダウンミックスと対になる ITU 系マトリクス。
// リア/サイドは L−R 差分（モノ成分はセンター/LFE、差分はサラウンドへ）。
static void UpmixStereoToSurround(float L, float R, int dstCh, float* dstChOut, float s2)
{
	auto clip1 = [](float x) -> float {
		if (x > 1.0f) return 1.0f;
		if (x < -1.0f) return -1.0f;
		return x;
	};

	const float diff = L - R;
	const float C = (L + R) * s2;
	const float lfe = (L + R) * 0.25f;
	// 旧: s2*0.5 ≈ 0.35 で差分が二重減衰。5.1→2ch の s2 係数に合わせつつ体感補正。
	const float kSurround51 = (savedata.surround > 0)
		? (0.85f + 0.40f * ((savedata.surround > 100 ? 100 : savedata.surround) / 100.0f))
		: 0.85f;
	const float kSide71 = kSurround51;
	const float kRear71 = (savedata.surround > 0)
		? (0.65f + 0.30f * ((savedata.surround > 100 ? 100 : savedata.surround) / 100.0f))
		: 0.65f;

	if (dstCh == 2) {
		dstChOut[0] = clip1(L);
		dstChOut[1] = clip1(R);
	}
	else if (dstCh == 3) {
		dstChOut[0] = clip1(L);
		dstChOut[1] = clip1(R);
		dstChOut[2] = clip1(lfe);
	}
	else if (dstCh == 4) {
		// FL, FR, BL, BR
		const float rear = diff * kSurround51;
		dstChOut[0] = clip1(L);
		dstChOut[1] = clip1(R);
		dstChOut[2] = clip1(rear);
		dstChOut[3] = clip1(-rear);
	}
	else if (dstCh == 6) {
		// FL, FR, FC, LFE, BL, BR
		const float rear = diff * kSurround51;
		dstChOut[0] = clip1(L);
		dstChOut[1] = clip1(R);
		dstChOut[2] = clip1(C);
		dstChOut[3] = clip1(lfe);
		dstChOut[4] = clip1(rear);
		dstChOut[5] = clip1(-rear);
	}
	else if (dstCh >= 8) {
		// FL, FR, FC, LFE, BL, BR, SL, SR
		const float rear = diff * kRear71;
		const float side = diff * kSide71;
		dstChOut[0] = clip1(L);
		dstChOut[1] = clip1(R);
		dstChOut[2] = clip1(C);
		dstChOut[3] = clip1(lfe);
		dstChOut[4] = clip1(rear);
		dstChOut[5] = clip1(-rear);
		dstChOut[6] = clip1(side);
		dstChOut[7] = clip1(-side);
		for (int d = 8; d < dstCh; ++d)
			dstChOut[d] = clip1(C);
	}
	else {
		dstChOut[0] = clip1(L);
		if (dstCh >= 2) dstChOut[1] = clip1(R);
		for (int d = 2; d < dstCh; ++d)
			dstChOut[d] = clip1(C);
	}
}

void AudioUpscaler::BuildOutputFrame(double posInSrcFrames, float* dstCh) const
{
	// ソースは FLAC/WAV 系の並び想定: FL,FR,FC,LFE,BL,BR[,SL,SR]
	if (m_srcCh < 1 || m_srcCh > kUpChMax || m_dstCh < 1 || m_dstCh > kUpChMax) {
		const int n = (m_dstCh > 0 && m_dstCh <= kUpChMax) ? m_dstCh : 0;
		for (int d = 0; d < n; ++d)
			dstCh[d] = 0.0f;
		return;
	}
	float srcSamp[kUpChMax];
	for (int c = 0; c < m_srcCh; ++c)
		srcSamp[c] = SampleInput(c, posInSrcFrames);

	for (int d = 0; d < m_dstCh; ++d)
		dstCh[d] = 0.0f;

	const float s2 = 0.70710678f;
	auto clip1 = [](float x) -> float {
		if (x > 1.0f) return 1.0f;
		if (x < -1.0f) return -1.0f;
		return x;
	};

	if (m_srcCh == m_dstCh) {
		for (int d = 0; d < m_dstCh; ++d)
			dstCh[d] = clip1(srcSamp[d]);
		return;
	}

	if (m_srcCh == 1) {
		float m = srcSamp[0];
		if (m_dstCh == 2) {
			dstCh[0] = clip1(m);
			dstCh[1] = clip1(m);
		}
		else if (m_dstCh == 3) {
			dstCh[0] = clip1(m);
			dstCh[1] = clip1(m);
			dstCh[2] = clip1(m * 0.5f);
		}
		else if (m_dstCh == 4) {
			dstCh[0] = clip1(m);
			dstCh[1] = clip1(m);
			dstCh[2] = clip1(m);
			dstCh[3] = clip1(m);
		}
		else if (m_dstCh == 6) {
			dstCh[0] = clip1(m);
			dstCh[1] = clip1(m);
			dstCh[2] = clip1(m * s2);
			dstCh[3] = clip1(m * 0.25f);
			dstCh[4] = 0.0f;
			dstCh[5] = 0.0f;
		}
		else if (m_dstCh >= 8) {
			dstCh[0] = clip1(m);
			dstCh[1] = clip1(m);
			dstCh[2] = clip1(m * s2);
			dstCh[3] = clip1(m * 0.25f);
			for (int d = 4; d < m_dstCh; ++d)
				dstCh[d] = 0.0f;
		}
		else {
			if (m_dstCh >= 1) dstCh[0] = clip1(m);
			for (int d = 1; d < m_dstCh; ++d)
				dstCh[d] = clip1(m);
		}
		return;
	}

	if (m_srcCh == 2) {
		UpmixStereoToSurround(srcSamp[0], srcSamp[1], m_dstCh, dstCh, s2);
		return;
	}

	// 5.1 -> stereo
	if (m_srcCh == 6 && m_dstCh == 2) {
		float fl = srcSamp[0], fr = srcSamp[1], fc = srcSamp[2], lfe = srcSamp[3];
		float bl = srcSamp[4], br = srcSamp[5];
		float L = fl + s2 * fc + s2 * bl + 0.5f * lfe;
		float R = fr + s2 * fc + s2 * br + 0.5f * lfe;
		const float g = 0.35f;
		dstCh[0] = clip1(L * g);
		dstCh[1] = clip1(R * g);
		return;
	}

	// 7.1 -> stereo
	if (m_srcCh == 8 && m_dstCh == 2) {
		float fl = srcSamp[0], fr = srcSamp[1], fc = srcSamp[2], lfe = srcSamp[3];
		float bl = srcSamp[4], br = srcSamp[5], sl = srcSamp[6], sr = srcSamp[7];
		float L = fl + s2 * fc + bl + sl + 0.5f * lfe;
		float R = fr + s2 * fc + br + sr + 0.5f * lfe;
		const float g = 0.28f;
		dstCh[0] = clip1(L * g);
		dstCh[1] = clip1(R * g);
		return;
	}

	// 7.1 -> 5.1（サイドをバックへマッピング）
	if (m_srcCh == 8 && m_dstCh == 6) {
		for (int i = 0; i < 4; ++i)
			dstCh[i] = clip1(srcSamp[i]);
		dstCh[4] = clip1(s2 * (srcSamp[4] + srcSamp[6]));
		dstCh[5] = clip1(s2 * (srcSamp[5] + srcSamp[7]));
		return;
	}

	// 5.1 -> 7.1（サイドにバックを割当）
	if (m_srcCh == 6 && m_dstCh == 8) {
		for (int i = 0; i < 6; ++i)
			dstCh[i] = clip1(srcSamp[i]);
		dstCh[6] = srcSamp[4];
		dstCh[7] = srcSamp[5];
		return;
	}

	// 4ch クアッド FL,FR,BL,BR -> stereo
	if (m_srcCh == 4 && m_dstCh == 2) {
		const float g = 0.5f;
		dstCh[0] = clip1((srcSamp[0] + srcSamp[2]) * g);
		dstCh[1] = clip1((srcSamp[1] + srcSamp[3]) * g);
		return;
	}

	// 6 -> 4（前面＋背面）
	if (m_srcCh == 6 && m_dstCh == 4) {
		dstCh[0] = clip1(srcSamp[0]); dstCh[1] = clip1(srcSamp[1]);
		dstCh[2] = clip1(srcSamp[4]); dstCh[3] = clip1(srcSamp[5]);
		return;
	}

	// 8 -> 4
	if (m_srcCh == 8 && m_dstCh == 4) {
		dstCh[0] = clip1(srcSamp[0]); dstCh[1] = clip1(srcSamp[1]);
		dstCh[2] = clip1(s2 * (srcSamp[4] + srcSamp[6]));
		dstCh[3] = clip1(s2 * (srcSamp[5] + srcSamp[7]));
		return;
	}

	// 4 -> 5.1
	if (m_srcCh == 4 && m_dstCh == 6) {
		float fl = srcSamp[0], fr = srcSamp[1], bl = srcSamp[2], br = srcSamp[3];
		dstCh[0] = clip1(fl);
		dstCh[1] = clip1(fr);
		dstCh[2] = clip1((fl + fr) * s2);
		dstCh[3] = clip1((fl + fr) * 0.25f);
		dstCh[4] = clip1(bl);
		dstCh[5] = clip1(br);
		return;
	}

	// 5.1 / 7.1 -> 2.1
	if (m_srcCh == 6 && m_dstCh == 3) {
		float fl = srcSamp[0], fr = srcSamp[1], fc = srcSamp[2], lfe = srcSamp[3];
		float bl = srcSamp[4], br = srcSamp[5];
		float L = (fl + s2 * fc + s2 * bl + 0.5f * lfe) * 0.35f;
		float R = (fr + s2 * fc + s2 * br + 0.5f * lfe) * 0.35f;
		dstCh[0] = clip1(L); dstCh[1] = clip1(R);
		dstCh[2] = clip1(lfe * 0.5f + (fl + fr) * 0.125f);
		return;
	}
	if (m_srcCh == 8 && m_dstCh == 3) {
		float fl = srcSamp[0], fr = srcSamp[1], fc = srcSamp[2], lfe = srcSamp[3];
		float bl = srcSamp[4], br = srcSamp[5], sl = srcSamp[6], sr = srcSamp[7];
		float L = (fl + s2 * fc + bl + sl + 0.5f * lfe) * 0.28f;
		float R = (fr + s2 * fc + br + sr + 0.5f * lfe) * 0.28f;
		dstCh[0] = clip1(L); dstCh[1] = clip1(R);
		dstCh[2] = clip1(lfe * 0.5f + (fl + fr) * 0.125f);
		return;
	}

	if (m_srcCh == 3 && m_dstCh == 2) {
		// 2.1(L,R,LFE) → stereo: LFE を両chへ折りたたむ
		dstCh[0] = clip1(srcSamp[0] + srcSamp[2] * 0.5f);
		dstCh[1] = clip1(srcSamp[1] + srcSamp[2] * 0.5f);
		return;
	}

	// 2.1(L,R,LFE) → 多ch: _s1=LFE、LR を Center / Rear / Side にも
	if (m_srcCh == 3 && m_dstCh == 4) {
		const float kSur = 0.85f;
		dstCh[0] = clip1(srcSamp[0] + srcSamp[2] * 0.5f);
		dstCh[1] = clip1(srcSamp[1] + srcSamp[2] * 0.5f);
		dstCh[2] = clip1(srcSamp[0] * kSur);
		dstCh[3] = clip1(srcSamp[1] * kSur);
		return;
	}
	if (m_srcCh == 3 && m_dstCh == 6) {
		const float kSur = 0.85f;
		dstCh[0] = clip1(srcSamp[0]);
		dstCh[1] = clip1(srcSamp[1]);
		dstCh[2] = clip1((srcSamp[0] + srcSamp[1]) * s2); // Center ← LR
		dstCh[3] = clip1(srcSamp[2]); // LFE ← _s1
		dstCh[4] = clip1(srcSamp[0] * kSur);
		dstCh[5] = clip1(srcSamp[1] * kSur);
		return;
	}
	if (m_srcCh == 3 && m_dstCh >= 8) {
		const float kSur = 0.85f;
		const float kSide = 0.85f;
		dstCh[0] = clip1(srcSamp[0]);
		dstCh[1] = clip1(srcSamp[1]);
		dstCh[2] = clip1((srcSamp[0] + srcSamp[1]) * s2);
		dstCh[3] = clip1(srcSamp[2]);
		dstCh[4] = clip1(srcSamp[0] * kSur);
		dstCh[5] = clip1(srcSamp[1] * kSur);
		dstCh[6] = clip1(srcSamp[0] * kSide);
		dstCh[7] = clip1(srcSamp[1] * kSide);
		for (int d = 8; d < m_dstCh; ++d)
			dstCh[d] = 0.0f;
		return;
	}

	if (m_dstCh < m_srcCh) {
		for (int d = 0; d < m_dstCh; ++d)
			dstCh[d] = clip1(srcSamp[d]);
		return;
	}

	for (int d = 0; d < m_srcCh; ++d)
		dstCh[d] = clip1(srcSamp[d]);
	for (int d = m_srcCh; d < m_dstCh; ++d)
		dstCh[d] = clip1(srcSamp[m_srcCh - 1]);
}

void AudioUpscaler::PushInterleaved(const uint8_t* pcm, int byteCount)
{
	if (!m_active || byteCount <= 0 || !pcm) return;
	if (m_srcCh < 1 || m_srcCh > kUpChMax) return;
	const int bps = m_srcBits / 8;
	const int frameBytes = m_srcCh * bps;
	if (frameBytes <= 0) return;
	int nFrames = byteCount / frameBytes;
	if (nFrames <= 0) return;

	int maxFifo = m_srcCh * ((m_srcRate > 1) ? m_srcRate : 1) * 2;
	if (maxFifo > kUpFifoCap)
		maxFifo = kUpFifoCap - (kUpFifoCap % m_srcCh);
	if (maxFifo < m_srcCh)
		maxFifo = m_srcCh;

	const uint8_t* p = pcm;
	for (int f = 0; f < nFrames; ++f) {
		float tmp[kUpChMax];
		if (m_srcBits == 8) {
			for (int c = 0; c < m_srcCh; ++c) {
				float v = (float)((int)p[c] - 128) / 128.0f;
				if (v > 1.0f) v = 1.0f;
				else if (v < -1.0f) v = -1.0f;
				tmp[c] = v;
			}
		}
		else if (m_srcBits == 16) {
			const int16_t* s = (const int16_t*)p;
			for (int c = 0; c < m_srcCh; ++c)
				tmp[c] = (float)s[c] / 32768.0f;
		}
		else if (m_srcBits == 24) {
			for (int c = 0; c < m_srcCh; ++c) {
				const uint8_t* b = p + c * 3;
				int32_t v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16));
				if (v & 0x800000) v |= ~0xFFFFFF;
				tmp[c] = (float)v / 8388608.0f;
			}
		}
		else {
			const int32_t* s = (const int32_t*)p;
			for (int c = 0; c < m_srcCh; ++c)
				tmp[c] = (float)((double)s[c] / 2147483648.0);
		}
		p += frameBytes;

		while (m_fifoCount + m_srcCh > maxFifo && m_fifoCount >= m_srcCh) {
			m_fifoHead += m_srcCh;
			if (m_fifoHead >= kUpFifoCap) m_fifoHead %= kUpFifoCap;
			m_fifoCount -= m_srcCh;
			m_readPos -= 1.0;
			if (m_readPos < 0.0) m_readPos = 0.0;
		}
		if (m_fifoCount + m_srcCh > kUpFifoCap)
			break;
		for (int c = 0; c < m_srcCh; ++c)
			m_fifo[(m_fifoHead + m_fifoCount++) % kUpFifoCap] = tmp[c];
	}
}

bool AudioUpscaler::NeedsMoreInput() const
{
	if (!m_active) return false;
	const int64_t nIn = (m_srcCh > 0) ? (int64_t)(m_fifoCount / m_srcCh) : 0;
	double need = m_readPos + 4.0 + (double)m_dstRate / (double)m_srcRate * 2.0;
	return (double)nIn < need;
}

int AudioUpscaler::SuggestInputBytes(int dstBytesRemaining) const
{
	if (!m_active) return 0;
	const int outFrame = m_dstCh * (m_dstBits / 8);
	const int inFrame = m_srcCh * (m_srcBits / 8);
	if (outFrame <= 0 || inFrame <= 0) return 4096;
	int outFrames = dstBytesRemaining / outFrame + 4;
	double inFrames = ceil((double)outFrames * (double)m_srcRate / (double)m_dstRate) + 8.0;
	int bytes = (int)(inFrames * (double)inFrame);
	bytes += inFrame * 8;
	return (std::max)(bytes, inFrame * 64);
}

int AudioUpscaler::PullInterleaved(uint8_t* dst, int dstCapacity)
{
	if (!m_active || !dst || dstCapacity <= 0) return 0;
	const int outFrameBytes = m_dstCh * (m_dstBits / 8);
	int outFramesCap = dstCapacity / outFrameBytes;
	if (outFramesCap <= 0) return 0;
	if (m_srcCh < 1 || m_dstCh < 1 || m_dstCh > kUpChMax) return 0;
	int batchFrames = kUpPullCap / m_dstCh;
	if (batchFrames < 1) return 0;

	int outBytes = 0;
	const double step = (double)m_srcRate / (double)m_dstRate;
	int producedTotal = 0;
	while (producedTotal < outFramesCap) {
		int want = outFramesCap - producedTotal;
		if (want > batchFrames) want = batchFrames;
		int produced = 0;
		while (produced < want) {
			const int64_t nIn = (int64_t)(m_fifoCount / m_srcCh);
			if ((double)nIn < m_readPos + step + 5.0)
				break;
			BuildOutputFrame(m_readPos, m_scratchFrame);
			for (int c = 0; c < m_dstCh; ++c)
				m_pullTmp[produced * m_dstCh + c] = m_scratchFrame[c];
			m_readPos += step;
			produced++;
		}
		if (produced == 0) break;
		outBytes += FloatToPcm(m_pullTmp, produced, m_dstCh, m_srcBits, m_dstBits, dst + outBytes, m_ditherRng);
		producedTotal += produced;
	}

	const int64_t dropFrames = (int64_t)m_readPos;
	if (dropFrames > 0 && m_srcCh > 0) {
		const int dropSamp = (int)dropFrames * m_srcCh;
		if (dropSamp > 0 && dropSamp <= m_fifoCount) {
			m_fifoHead = (m_fifoHead + dropSamp) % kUpFifoCap;
			m_fifoCount -= dropSamp;
			m_readPos -= (double)dropFrames;
		}
	}
	return outBytes;
}
