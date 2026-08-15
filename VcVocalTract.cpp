#include "stdafx.h"
#include "VcVocalTract.h"

// Windows.h の min/max マクロが Signalsmith と衝突するため解除
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "signalsmith-stretch/signalsmith-stretch.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct VcVocalTract::Impl {
	signalsmith::stretch::SignalsmithStretch<float> stretch;
	std::vector<float> inBuf;
	std::vector<float> outBuf;
	bool configured = false;
};

VcVocalTract::VcVocalTract()
	: m_impl(new Impl())
	, m_rate(48000), m_quality(1)
	, m_toneLp(0), m_toneHp(0), m_brightState(0)
	, m_robotPh(0), m_noise(1)
	, m_lastPitch(1.f), m_lastFormant(1.f)
{
}

VcVocalTract::~VcVocalTract()
{
	delete m_impl;
	m_impl = nullptr;
}

void VcVocalTract::Reset(int sampleRate, int quality)
{
	if (sampleRate < 8000) sampleRate = 48000;
	m_rate = sampleRate;
	m_quality = quality ? 1 : 0;
	m_toneLp = m_toneHp = m_brightState = 0;
	m_robotPh = 0;
	m_lastPitch = m_lastFormant = 1.f;

	if (!m_impl) m_impl = new Impl();
	if (m_quality)
		m_impl->stretch.presetDefault(1, (float)m_rate, true);
	else
		m_impl->stretch.presetCheaper(1, (float)m_rate, true);
	m_impl->stretch.reset();
	m_impl->configured = true;

	// 音声向けフォルマント基準（おおよそ話声の中心）
	m_impl->stretch.setFormantBase(160.f / (float)m_rate);
}

void VcVocalTract::Process(const float* in, float* out, int n, const VcVocalParams& p)
{
	if (n <= 0) return;
	if (!m_impl || !m_impl->configured || p.quality != m_quality)
		Reset(m_rate > 0 ? m_rate : 48000, p.quality);

	const float pitch = ClampF(p.pitch, 0.50f, 2.50f);
	const float formant = ClampF(p.formant, 0.70f, 1.35f);

	// 素通し（標準プリセット）
	if (fabsf(pitch - 1.f) < 0.008f && fabsf(formant - 1.f) < 0.008f
		&& p.style == 0 && p.breath < 0.5f && fabsf(p.bright - 100.f) < 0.5f) {
		if (p.gain == 1.f) {
			memcpy(out, in, sizeof(float) * (size_t)n);
			return;
		}
		for (int i = 0; i < n; ++i)
			out[i] = ClampF(in[i] * p.gain, -1.f, 1.f);
		return;
	}

	if (fabsf(pitch - m_lastPitch) > 1e-4f || fabsf(formant - m_lastFormant) > 1e-4f) {
		// pitch と formant を独立に設定。
		// compensatePitch=true: ピッチに引きずられるフォルマント移動を打ち消し、
		// その上で formant 倍率だけを乗せる（男女変換の本命）
		m_impl->stretch.setTransposeFactor(pitch, 8000.f / (float)m_rate);
		m_impl->stretch.setFormantFactor(formant, true);
		m_lastPitch = pitch;
		m_lastFormant = formant;
	}

	if ((int)m_impl->inBuf.size() < n) {
		m_impl->inBuf.resize((size_t)n);
		m_impl->outBuf.resize((size_t)n);
	}
	memcpy(m_impl->inBuf.data(), in, sizeof(float) * (size_t)n);

	float* inPtrs[1] = { m_impl->inBuf.data() };
	float* outPtrs[1] = { m_impl->outBuf.data() };
	m_impl->stretch.process(inPtrs, n, outPtrs, n);

	const float breath = (p.breath > 0.5f) ? (p.breath / 100.f) * 0.03f : 0.f;
	const float brightDb = (p.bright - 100.f) * 0.045f;
	// わずかな抜け（キラキラではなく空気感）
	const float airAmt = (fabsf(pitch - 1.f) > 0.04f) ? 0.10f : 0.f;

	for (int i = 0; i < n; ++i) {
		float y = m_impl->outBuf[(size_t)i];
		if (breath > 0.f) {
			m_noise = m_noise * 1664525u + 1013904223u;
			y += (((int)(m_noise >> 16) - 32768) / 32768.f) * breath * (0.06f + fabsf(y));
		}
		m_toneLp += 0.05f * (y - m_toneLp);
		const float air = y - m_toneLp;
		y += air * airAmt;
		y += air * (brightDb * 0.03f);

		if (p.style == 2) {
			m_toneLp += 0.2f * (y - m_toneLp);
			m_toneHp += 0.03f * (y - m_toneHp);
			y = (m_toneLp - m_toneHp) * 1.3f;
		} else if (p.style == 1) {
			m_robotPh += 2.0 * M_PI * 36.0 / (double)m_rate;
			if (m_robotPh > 2.0 * M_PI) m_robotPh -= 2.0 * M_PI;
			y *= (0.8f + 0.2f * (float)sin(m_robotPh));
			y = floorf(y * 40.f + 0.5f) / 40.f;
		}
		y *= p.gain;
		out[i] = ClampF(y, -1.f, 1.f);
	}
}

bool VcParamsFromSavedata(VcVocalParams& out)
{
	out.pitch = ClampF((float)savedata.vc_pitch / 100.f, 0.50f, 2.50f);
	out.formant = ClampF((float)savedata.vc_formant / 100.f, 0.70f, 1.35f);
	out.gain = ClampF((float)savedata.vc_gain / 100.f, 0.25f, 2.0f);
	out.bright = (float)savedata.vc_bright;
	out.breath = (float)savedata.vc_breath;
	out.style = savedata.vc_style;
	if (out.style < 0) out.style = 0;
	if (out.style > 2) out.style = 2;
	out.quality = savedata.vc_quality ? 1 : 0;
	// 明示チェックOFFならマイクミックスへは適用しない（VC単体モニタは別経路）
	if (!savedata.vc_mic_apply)
		return false;
	const bool active =
		fabsf(out.pitch - 1.f) >= 0.008f ||
		fabsf(out.formant - 1.f) >= 0.008f ||
		fabsf(out.gain - 1.f) >= 0.008f ||
		fabsf(out.bright - 100.f) >= 0.5f ||
		out.breath > 0.5f ||
		out.style != 0;
	return active;
}

void VcProcessInterleavedStereo(VcVocalTract& tract, float* interleavedLR, int frames, int sampleRate, const VcVocalParams& p)
{
	if (!interleavedLR || frames <= 0) return;
	if (sampleRate < 8000) sampleRate = 48000;
	if (tract.SampleRate() != sampleRate)
		tract.Reset(sampleRate, p.quality);

	const int kMax = 2048;
	float mono[2048];
	int off = 0;
	while (off < frames) {
		int n = frames - off;
		if (n > kMax) n = kMax;
		for (int i = 0; i < n; ++i) {
			const float L = interleavedLR[(off + i) * 2 + 0];
			const float R = interleavedLR[(off + i) * 2 + 1];
			mono[i] = 0.5f * (L + R);
		}
		tract.Process(mono, mono, n, p);
		for (int i = 0; i < n; ++i) {
			interleavedLR[(off + i) * 2 + 0] = mono[i];
			interleavedLR[(off + i) * 2 + 1] = mono[i];
		}
		off += n;
	}
}
