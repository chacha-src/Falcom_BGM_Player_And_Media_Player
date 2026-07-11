// CAnalyzerDlg.cpp : 簡易波形アナライザー(スクロールBB・多ch・周波数特性)
#include "stdafx.h"
#include "ogg.h"
#include "CAnalyzerDlg.h"
#include <cmath>
#include <algorithm>

IMPLEMENT_DYNAMIC(CAnalyzerDlg, CCustomBlurDialogExBase)

namespace
{
	static constexpr COLORREF ANALYZER_CHROMA_KEY = RGB(18, 20, 28);
	static constexpr COLORREF ANALYZER_BG = RGB(18, 20, 28);

	static const COLORREF kChColor[CAnalyzerDlg::CH_MAX] = {
		RGB(80, 200, 255), RGB(255, 140, 180), RGB(120, 230, 140), RGB(255, 200, 80),
		RGB(180, 140, 255), RGB(80, 220, 200), RGB(255, 160, 100), RGB(200, 200, 220)
	};

	inline float SampleToFloat(const void* pData, int bits, int frame, int ch, int channels)
	{
		if (!pData || channels <= 0) return 0.0f;
		if (bits == 16) {
			const short* s = (const short*)pData;
			return (float)s[frame * channels + ch] / 32768.0f;
		}
		if (bits == 24) {
			const unsigned char* b = (const unsigned char*)pData;
			const int idx = (frame * channels + ch) * 3;
			int v = b[idx] | (b[idx + 1] << 8) | (b[idx + 2] << 16);
			if (v & 0x800000) v |= ~0xFFFFFF;
			return (float)v / 8388608.0f;
		}
		if (bits == 32) {
			const float* f = (const float*)pData;
			return f[frame * channels + ch];
		}
		if (bits == 8) {
			const unsigned char* u = (const unsigned char*)pData;
			return ((float)u[frame * channels + ch] - 128.0f) / 128.0f;
		}
		return 0.0f;
	}

	inline float AmpToDb(float a)
	{
		const float x = fabsf(a);
		if (x < 1e-8f) return -96.0f;
		float db = 20.0f * log10f(x);
		if (db < -96.0f) db = -96.0f;
		if (db > 0.0f) db = 0.0f;
		return db;
	}

	void FftRadix2(float* re, float* im, int n)
	{
		int j = 0;
		for (int i = 1; i < n; ++i) {
			int bit = n >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j) {
				std::swap(re[i], re[j]);
				std::swap(im[i], im[j]);
			}
		}
		for (int len = 2; len <= n; len <<= 1) {
			const float ang = -6.28318530718f / (float)len;
			const float wlenRe = cosf(ang);
			const float wlenIm = sinf(ang);
			for (int i = 0; i < n; i += len) {
				float wRe = 1.0f, wIm = 0.0f;
				const int half = len >> 1;
				for (int k = 0; k < half; ++k) {
					const float uRe = re[i + k];
					const float uIm = im[i + k];
					const float vRe = re[i + k + half] * wRe - im[i + k + half] * wIm;
					const float vIm = re[i + k + half] * wIm + im[i + k + half] * wRe;
					re[i + k] = uRe + vRe;
					im[i + k] = uIm + vIm;
					re[i + k + half] = uRe - vRe;
					im[i + k + half] = uIm - vIm;
					const float nWRe = wRe * wlenRe - wIm * wlenIm;
					wIm = wRe * wlenIm + wIm * wlenRe;
					wRe = nWRe;
				}
			}
		}
	}

	// 対数軸の帯域境界(edge=0..bins → 20Hz..nyquist)
	float SpecEdgeHz(int edge, int bins, float nyquist)
	{
		const float fMin = 20.0f;
		const float fMax = (std::max)(fMin * 1.01f, nyquist);
		if (bins <= 0) return fMin;
		const float t = (float)edge / (float)bins;
		return fMin * powf(fMax / fMin, t);
	}

	float SpecCenterHz(int bin, int bins, float nyquist)
	{
		return sqrtf(SpecEdgeHz(bin, bins, nyquist) * SpecEdgeHz(bin + 1, bins, nyquist));
	}
}

CAnalyzerDlg::CAnalyzerDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_ANALYZER, pParent)
{
	InitializeCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		m_ring[c].assign(RING_SAMPLES, 0.0f);
		m_ringSnap[c].assign(RING_SAMPLES, 0.0f);
	}
	m_fftRe.assign(FFT_SIZE, 0.0f);
	m_fftIm.assign(FFT_SIZE, 0.0f);
	m_fftWindow.assign(FFT_SIZE, 0.0f);
	for (int i = 0; i < FFT_SIZE; ++i)
		m_fftWindow[i] = 0.5f * (1.0f - cosf(6.28318530718f * (float)i / (float)(FFT_SIZE - 1)));
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			m_specDb[c][b] = -96.0f;
			m_specPeakDb[c][b] = -96.0f;
		}
	}
}

CAnalyzerDlg::~CAnalyzerDlg()
{
	ReleaseBuffers();
	DeleteCriticalSection(&m_cs);
}

void CAnalyzerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAnalyzerDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_SHOWWINDOW()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CAnalyzerDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(
		L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore",
		L"Analizador", L"분석기", L"分析器", L"المحلل",
		L"Анализатор", L"Analysator", L"Analisador", L"Analyser",
		L"Analizator", L"Analizor"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
#if CCUSTOM_AERO_SUPPORT
	if (!CCC_IsAeroEnabled())
#endif
		ModifyStyleEx(0, WS_EX_DLGMODALFRAME);

	int ax = savedata.analyzerx;
	int ay = savedata.analyzery;
	int aw = savedata.analyzerw;
	int ah = savedata.analyzerh;
	if (ax == -1 || ay == -1 || aw < 200 || ah < 120 || aw > 10000 || ah > 10000) {
		ax = 140; ay = 180; aw = 720; ah = 420;
	}
	SetWindowPos(&CWnd::wndTop, ax, ay, aw, ah, SWP_NOZORDER | SWP_NOOWNERZORDER);

	m_font.CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));

	m_feedEnabled = true;
	SetTimer(1, 33, nullptr);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled())
		ApplyDwmBlur();
#endif
	return TRUE;
}

void CAnalyzerDlg::ResumePlaybackFeed() { m_feedEnabled = true; }
void CAnalyzerDlg::PauseFeed() { m_feedEnabled = false; }

void CAnalyzerDlg::ResetPlaybackState()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c)
		std::fill(m_ring[c].begin(), m_ring[c].end(), 0.0f);
	m_ringWrite = 0;
	m_ringFilled = 0;
	m_accSamples = 0;
	m_pendingScroll = 0;
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			m_specDb[c][b] = -96.0f;
			m_specPeakDb[c][b] = -96.0f;
		}
	}
	LeaveCriticalSection(&m_cs);
	m_waveReady = false;
	m_specReady = false;
	m_specDirty = true;
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

void CAnalyzerDlg::DetachForDestroy()
{
	m_feedEnabled = false;
	KillTimer(1);
	ReleaseBuffers();
}

LPCTSTR CAnalyzerDlg::ChannelLabel(int ch, int channels)
{
	static const TCHAR* stereo[] = { _T("L"), _T("R") };
	static const TCHAR* ch51[] = { _T("L"), _T("R"), _T("C"), _T("LFE"), _T("SL"), _T("SR") };
	static const TCHAR* ch71[] = { _T("L"), _T("R"), _T("C"), _T("LFE"), _T("SL"), _T("SR"), _T("BL"), _T("BR") };
	static TCHAR generic[8][8];
	if (channels == 2 && ch < 2) return stereo[ch];
	if (channels == 6 && ch < 6) return ch51[ch];
	if (channels >= 8 && ch < 8) return ch71[ch];
	_stprintf_s(generic[ch], _T("Ch%d"), ch + 1);
	return generic[ch];
}

int CAnalyzerDlg::VisibleChannelCount(int waveH) const
{
	const int ch = (std::max)(1, (std::min)(m_channels, CH_MAX));
	const int minBand = 22;
	const int fit = (std::max)(1, waveH / minBand);
	return (std::min)(ch, fit);
}

void CAnalyzerDlg::FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels)
{
	if (!m_feedEnabled || !pData || frames <= 0) return;
	if (channels < 1) channels = 1;
	if (channels > CH_MAX) channels = CH_MAX;
	if (sampleRate > 0) {
		m_sampleRate = sampleRate;
		const int targetW = (m_waveW > 40) ? m_waveW : 640;
		m_samplesPerCol = (std::max)(8, (sampleRate * 3 / 2) / targetW);
	}

	EnterCriticalSection(&m_cs);
	if (m_channels != channels) {
		m_channels = channels;
		m_waveReady = false;
		m_specDirty = true;
	}

	for (int i = 0; i < frames; ++i) {
		for (int c = 0; c < channels; ++c)
			m_ring[c][m_ringWrite] = SampleToFloat(pData, bits, i, c, channels);
		for (int c = channels; c < CH_MAX; ++c)
			m_ring[c][m_ringWrite] = 0.0f;
		m_ringWrite = (m_ringWrite + 1) % RING_SAMPLES;
		if (m_ringFilled < RING_SAMPLES) ++m_ringFilled;
	}
	m_accSamples += frames;

	const int spc = (std::max)(8, m_samplesPerCol);
	int pushed = 0;
	while (m_accSamples >= spc && pushed < 64) {
		m_accSamples -= spc;
		++m_pendingScroll;
		++pushed;
	}
	const int cap = (m_waveW > 0) ? m_waveW : 640;
	if (m_pendingScroll > cap)
		m_pendingScroll = cap;
	LeaveCriticalSection(&m_cs);
}

bool CAnalyzerDlg::SnapshotRing(int& outWrite, int& outFilled, int& outChannels)
{
	EnterCriticalSection(&m_cs);
	outChannels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	outWrite = m_ringWrite;
	outFilled = m_ringFilled;
	if (outFilled <= 0) {
		LeaveCriticalSection(&m_cs);
		return false;
	}
	for (int c = 0; c < outChannels; ++c)
		memcpy(m_ringSnap[c].data(), m_ring[c].data(), RING_SAMPLES * sizeof(float));
	LeaveCriticalSection(&m_cs);
	return true;
}

void CAnalyzerDlg::UpdateSpectrumFromRing()
{
	// 波形と同じタイマー周期で更新(バックバッファへ描いてから Present)。間引きしない。
	int channels = 0, sr = 44100, filled = 0, write = 0;
	if (!SnapshotRing(write, filled, channels))
		return;
	EnterCriticalSection(&m_cs);
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	LeaveCriticalSection(&m_cs);
	if (filled < 64) return;

	const int use = (std::min)(filled, FFT_SIZE);
	int start = write - use;
	while (start < 0) start += RING_SAMPLES;

	const float nyquist = (float)sr * 0.5f;
	const float norm = 2.0f / (float)FFT_SIZE;
	const int kNyq = FFT_SIZE / 2;
	float magLin[FFT_SIZE / 2 + 1];
	float instant[CH_MAX][SPEC_BINS];
	float smoothed[SPEC_BINS];

	auto magAt = [&](float kFrac) -> float {
		if (kFrac < 1.0f) kFrac = 1.0f;
		if (kFrac > (float)(kNyq - 1)) kFrac = (float)(kNyq - 1);
		const int k0 = (int)kFrac;
		const float t = kFrac - (float)k0;
		return magLin[k0] * (1.0f - t) + magLin[k0 + 1] * t;
	};

	for (int c = 0; c < channels; ++c) {
		for (int i = 0; i < FFT_SIZE; ++i) {
			float v = 0.0f;
			if (i >= FFT_SIZE - use) {
				const int src = (start + (i - (FFT_SIZE - use))) % RING_SAMPLES;
				v = m_ringSnap[c][src];
			}
			m_fftRe[i] = v * m_fftWindow[i];
			m_fftIm[i] = 0.0f;
		}
		FftRadix2(m_fftRe.data(), m_fftIm.data(), FFT_SIZE);

		magLin[0] = 0.0f;
		for (int k = 1; k <= kNyq; ++k)
			magLin[k] = sqrtf(m_fftRe[k] * m_fftRe[k] + m_fftIm[k] * m_fftIm[k]) * norm;

		// 対数帯域ごとにエネルギー平均。帯域幅 < 1 FFT bin なら線形補間
		// (低域の階段化を防ぎ、高域のピーク固まりも抑える)
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float fLo = SpecEdgeHz(b, SPEC_BINS, nyquist);
			const float fHi = SpecEdgeHz(b + 1, SPEC_BINS, nyquist);
			float kLo = fLo * (float)FFT_SIZE / (float)sr;
			float kHi = fHi * (float)FFT_SIZE / (float)sr;
			if (kLo < 1.0f) kLo = 1.0f;
			if (kHi > (float)kNyq) kHi = (float)kNyq;
			if (kHi <= kLo) kHi = kLo + 0.05f;

			float energy = 0.0f;
			float weight = 0.0f;
			if (kHi - kLo < 1.0f) {
				const float m = magAt(0.5f * (kLo + kHi));
				energy = m * m;
				weight = 1.0f;
			}
			else {
				const int i0 = (int)kLo;
				const int i1 = (int)kHi;
				for (int k = i0; k <= i1; ++k) {
					if (k < 1 || k > kNyq) continue;
					const float a = (std::max)(kLo, (float)k);
					const float bb = (std::min)(kHi, (float)(k + 1));
					const float w = bb - a;
					if (w <= 0.0f) continue;
					const float m = magLin[k];
					energy += m * m * w;
					weight += w;
				}
			}
			const float rms = (weight > 1e-12f) ? sqrtf(energy / weight) : 0.0f;
			instant[c][b] = AmpToDb(rms);
		}

		// 周波数方向の軽い平滑(ギザギザ／固まり感を減らす)
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float l = instant[c][(std::max)(0, b - 1)];
			const float m = instant[c][b];
			const float r = instant[c][(std::min)(SPEC_BINS - 1, b + 1)];
			smoothed[b] = l * 0.18f + m * 0.64f + r * 0.18f;
		}
		for (int b = 0; b < SPEC_BINS; ++b)
			instant[c][b] = smoothed[b];
	}

	EnterCriticalSection(&m_cs);
	for (int c = 0; c < channels; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			if (instant[c][b] > m_specDb[c][b])
				m_specDb[c][b] = instant[c][b];
			else
				m_specDb[c][b] = m_specDb[c][b] * 0.78f + instant[c][b] * 0.22f;
			if (instant[c][b] >= m_specPeakDb[c][b])
				m_specPeakDb[c][b] = instant[c][b];
			else
				m_specPeakDb[c][b] = m_specPeakDb[c][b] * 0.985f + instant[c][b] * 0.015f;
		}
	}
	LeaveCriticalSection(&m_cs);
	m_specDirty = true;
}

void CAnalyzerDlg::ReleaseBuffers()
{
	if (m_waveScratchDC.GetSafeHdc()) {
		if (m_waveScratchOld) m_waveScratchDC.SelectObject(m_waveScratchOld);
		m_waveScratchOld = nullptr;
		if (m_waveScratchBmp.GetSafeHandle()) m_waveScratchBmp.DeleteObject();
		m_waveScratchDC.DeleteDC();
	}
	if (m_waveDC.GetSafeHdc()) {
		if (m_waveOld) m_waveDC.SelectObject(m_waveOld);
		m_waveOld = nullptr;
		if (m_waveBmp.GetSafeHandle()) m_waveBmp.DeleteObject();
		m_waveDC.DeleteDC();
	}
	if (m_specDC.GetSafeHdc()) {
		if (m_specOld) m_specDC.SelectObject(m_specOld);
		m_specOld = nullptr;
		if (m_specBmp.GetSafeHandle()) m_specBmp.DeleteObject();
		m_specDC.DeleteDC();
	}
	m_waveW = m_waveH = 0;
	m_specW = m_specH = 0;
	m_waveReady = m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
	m_chromaReady = false;
	m_chromaW = m_chromaH = 0;
	m_lastWaveScroll = 0;
#endif
}

bool CAnalyzerDlg::EnsureWaveBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_waveDC.GetSafeHdc() && m_waveW == w && m_waveH == h && m_waveScratchDC.GetSafeHdc())
		return true;

	if (m_waveScratchDC.GetSafeHdc()) {
		if (m_waveScratchOld) m_waveScratchDC.SelectObject(m_waveScratchOld);
		m_waveScratchOld = nullptr;
		if (m_waveScratchBmp.GetSafeHandle()) m_waveScratchBmp.DeleteObject();
		m_waveScratchDC.DeleteDC();
	}
	if (m_waveDC.GetSafeHdc()) {
		if (m_waveOld) m_waveDC.SelectObject(m_waveOld);
		m_waveOld = nullptr;
		if (m_waveBmp.GetSafeHandle()) m_waveBmp.DeleteObject();
		m_waveDC.DeleteDC();
	}

	if (!m_waveDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_waveBmp.CreateCompatibleBitmap(&refDC, w, h)) { m_waveDC.DeleteDC(); return false; }
	m_waveOld = m_waveDC.SelectObject(&m_waveBmp);

	if (!m_waveScratchDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_waveScratchBmp.CreateCompatibleBitmap(&refDC, w, h)) return false;
	m_waveScratchOld = m_waveScratchDC.SelectObject(&m_waveScratchBmp);

	m_waveW = w;
	m_waveH = h;
	m_waveReady = false;
	return true;
}

bool CAnalyzerDlg::EnsureSpecBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_specDC.GetSafeHdc() && m_specW == w && m_specH == h)
		return true;
	if (m_specDC.GetSafeHdc()) {
		if (m_specOld) m_specDC.SelectObject(m_specOld);
		m_specOld = nullptr;
		if (m_specBmp.GetSafeHandle()) m_specBmp.DeleteObject();
		m_specDC.DeleteDC();
	}
	if (!m_specDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_specBmp.CreateCompatibleBitmap(&refDC, w, h)) { m_specDC.DeleteDC(); return false; }
	m_specOld = m_specDC.SelectObject(&m_specBmp);
	m_specW = w;
	m_specH = h;
	m_specReady = false;
	m_specDirty = true;
	return true;
}

void CAnalyzerDlg::FullRedrawWave(COLORREF bg)
{
	if (!m_waveDC.GetSafeHdc() || m_waveW <= 0 || m_waveH <= 0) return;
	m_waveDC.FillSolidRect(0, 0, m_waveW, m_waveH, bg);

	int channels = 0, filled = 0, write = 0;
	if (!SnapshotRing(write, filled, channels)) {
		m_waveReady = true;
		return;
	}
	int spc = 64;
	EnterCriticalSection(&m_cs);
	spc = (std::max)(8, m_samplesPerCol);
	LeaveCriticalSection(&m_cs);

	const int vis = VisibleChannelCount(m_waveH);
	m_waveLayoutCh = vis;
	if (filled < 8) {
		m_waveReady = true;
		return;
	}

	const int bandH = m_waveH / vis;
	CFont* oldFont = m_waveDC.SelectObject(&m_font);
	m_waveDC.SetBkMode(TRANSPARENT);

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		CPen grid(PS_SOLID, 1, RGB(40, 44, 58));
		CPen* op = m_waveDC.SelectObject(&grid);
		m_waveDC.MoveTo(0, mid);
		m_waveDC.LineTo(m_waveW, mid);

		CPen pen(PS_SOLID, 1, kChColor[c % CH_MAX]);
		m_waveDC.SelectObject(&pen);

		for (int x = 0; x < m_waveW; ++x) {
			const int ageCols = m_waveW - 1 - x;
			const int sampleBack = ageCols * spc + spc;
			float mn = 1.0f, mx = -1.0f;
			for (int s = 0; s < spc; ++s) {
				int idx = write - sampleBack + s;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				if (filled < RING_SAMPLES && idx >= filled) continue;
				const float v = m_ringSnap[c][idx];
				if (v < mn) mn = v;
				if (v > mx) mx = v;
			}
			if (mn > mx) { mn = 0; mx = 0; }
			m_waveDC.MoveTo(x, mid - (int)(mx * amp));
			m_waveDC.LineTo(x, mid - (int)(mn * amp));
		}

		m_waveDC.SetTextColor(kChColor[c % CH_MAX]);
		m_waveDC.TextOut(4, y0 + 2, ChannelLabel(c, channels));
		m_waveDC.SelectObject(op);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
}

int CAnalyzerDlg::ScrollWaveAndDrawNew(COLORREF bg)
{
	if (!m_waveDC.GetSafeHdc() || !m_waveScratchDC.GetSafeHdc()) return 0;

	int scroll = 0, channels = 0, write = 0, filled = 0, spc = 64;
	EnterCriticalSection(&m_cs);
	scroll = m_pendingScroll;
	m_pendingScroll = 0;
	spc = (std::max)(8, m_samplesPerCol);
	LeaveCriticalSection(&m_cs);

	const int vis = VisibleChannelCount(m_waveH);
	if (vis != m_waveLayoutCh || !m_waveReady) {
		FullRedrawWave(bg);
		return -1;
	}
	if (scroll <= 0) return 0;
	if (scroll >= m_waveW) {
		FullRedrawWave(bg);
		return -1;
	}
	if (!SnapshotRing(write, filled, channels))
		return 0;

	const int keep = m_waveW - scroll;
	m_waveScratchDC.BitBlt(0, 0, keep, m_waveH, &m_waveDC, scroll, 0, SRCCOPY);
	m_waveScratchDC.FillSolidRect(keep, 0, scroll, m_waveH, bg);
	m_waveDC.BitBlt(0, 0, m_waveW, m_waveH, &m_waveScratchDC, 0, 0, SRCCOPY);

	const int bandH = m_waveH / vis;
	CFont* oldFont = m_waveDC.SelectObject(&m_font);
	m_waveDC.SetBkMode(TRANSPARENT);

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		CPen grid(PS_SOLID, 1, RGB(40, 44, 58));
		CPen* op = m_waveDC.SelectObject(&grid);
		m_waveDC.MoveTo(keep, mid);
		m_waveDC.LineTo(m_waveW, mid);

		CPen pen(PS_SOLID, 1, kChColor[c % CH_MAX]);
		m_waveDC.SelectObject(&pen);

		for (int x = keep; x < m_waveW; ++x) {
			const int ageCols = m_waveW - 1 - x;
			const int sampleBack = ageCols * spc + spc;
			float mn = 1.0f, mx = -1.0f;
			for (int s = 0; s < spc; ++s) {
				int idx = write - sampleBack + s;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				if (filled < RING_SAMPLES && idx >= filled) continue;
				const float v = m_ringSnap[c][idx];
				if (v < mn) mn = v;
				if (v > mx) mx = v;
			}
			if (mn > mx) { mn = 0; mx = 0; }
			m_waveDC.MoveTo(x, mid - (int)(mx * amp));
			m_waveDC.LineTo(x, mid - (int)(mn * amp));
		}

		m_waveDC.FillSolidRect(0, y0, 28, (std::min)(14, y1 - y0), bg);
		m_waveDC.SetTextColor(kChColor[c % CH_MAX]);
		m_waveDC.TextOut(4, y0 + 2, ChannelLabel(c, channels));
		m_waveDC.SelectObject(op);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	return scroll;
}

void CAnalyzerDlg::RedrawSpectrum(COLORREF bg)
{
	if (!m_specDC.GetSafeHdc() || m_specW <= 0 || m_specH <= 0) return;
	m_specDC.FillSolidRect(0, 0, m_specW, m_specH, bg);

	int channels = 2, sr = 44100;
	float spec[CH_MAX][SPEC_BINS], peak[CH_MAX][SPEC_BINS];
	EnterCriticalSection(&m_cs);
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	memcpy(spec, m_specDb, sizeof(spec));
	memcpy(peak, m_specPeakDb, sizeof(peak));
	LeaveCriticalSection(&m_cs);

	// 周波数特性は同一軸に全 ch を重ね描き(波形の帯分割とは別)
	const int vis = channels;
	const int padL = 36, padR = 8, padT = 16, padB = 18;
	CRect plot(padL, padT, m_specW - padR, m_specH - padB);
	if (plot.Width() < 8 || plot.Height() < 8) {
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	CFont* oldFont = m_specDC.SelectObject(&m_font);
	m_specDC.SetBkMode(TRANSPARENT);
	m_specDC.SetTextColor(RGB(180, 190, 210));
	m_specDC.TextOut(padL, 1,
		LL14(L"周波数特性", L"Frequency response", L"Reponse en frequence", L"Risposta in frequenza",
			L"Respuesta en frecuencia", L"주파수 특성", L"频率特性", L"الاستجابة الترددية",
			L"АЧХ", L"Frequenzgang", L"Resposta em frequencia", L"Frequentierespons",
			L"Charakterystyka", L"Frekans yaniti"));

	CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
	CPen* oldPen = m_specDC.SelectObject(&frame);
	m_specDC.SelectStockObject(NULL_BRUSH);
	m_specDC.Rectangle(plot);

	CPen grid(PS_DOT, 1, RGB(45, 50, 65));
	m_specDC.SelectObject(&grid);
	m_specDC.SetTextColor(RGB(130, 140, 160));
	for (int db = 0; db >= -80; db -= 20) {
		const float t = (0.0f - (float)db) / 96.0f;
		const int y = plot.bottom - (int)(t * plot.Height());
		m_specDC.MoveTo(plot.left, y);
		m_specDC.LineTo(plot.right, y);
		CString lab; lab.Format(_T("%d"), db);
		m_specDC.TextOut(2, y - 6, lab);
	}

	const float nyquist = (float)sr * 0.5f;
	const int markHz[] = { 100, 1000, 10000 };
	for (int mi = 0; mi < 3; ++mi) {
		const float f = (float)markHz[mi];
		if (f >= nyquist) continue;
		const float t = logf(f / 20.0f) / logf(nyquist / 20.0f);
		if (t < 0.0f || t > 1.0f) continue;
		const int x = plot.left + (int)(t * plot.Width());
		m_specDC.MoveTo(x, plot.top);
		m_specDC.LineTo(x, plot.bottom);
		CString lab;
		if (markHz[mi] >= 1000) lab.Format(_T("%dk"), markHz[mi] / 1000);
		else lab.Format(_T("%d"), markHz[mi]);
		m_specDC.TextOut(x - 8, plot.bottom + 1, lab);
	}

	auto dbToY = [&](float db) -> int {
		float t = (db + 96.0f) / 96.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};

	auto hzToX = [&](float hz) -> int {
		const float fMin = 20.0f;
		const float fMax = (std::max)(fMin * 1.01f, nyquist);
		float t = logf((std::max)(hz, fMin) / fMin) / logf(fMax / fMin);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.left + (int)(t * plot.Width());
	};

	for (int c = 0; c < vis; ++c) {
		CPen peakPen(PS_SOLID, 1, RGB(
			(GetRValue(kChColor[c]) * 2 + 255) / 3,
			(GetGValue(kChColor[c]) * 2 + 255) / 3,
			(GetBValue(kChColor[c]) * 2 + 255) / 3));
		m_specDC.SelectObject(&peakPen);
		for (int b = 0; b < SPEC_BINS; ++b) {
			const int x = hzToX(SpecCenterHz(b, SPEC_BINS, nyquist));
			const int y = dbToY(peak[c][b]);
			if (b == 0) m_specDC.MoveTo(x, y);
			else m_specDC.LineTo(x, y);
		}

		CPen curve(PS_SOLID, 2, kChColor[c]);
		m_specDC.SelectObject(&curve);
		for (int b = 0; b < SPEC_BINS; ++b) {
			const int x = hzToX(SpecCenterHz(b, SPEC_BINS, nyquist));
			const int y = dbToY(spec[c][b]);
			if (b == 0) m_specDC.MoveTo(x, y);
			else m_specDC.LineTo(x, y);
		}

		m_specDC.SetTextColor(kChColor[c]);
		m_specDC.TextOut(plot.right - 36, padT + 2 + c * 12, ChannelLabel(c, channels));
	}

	m_specDC.SelectObject(oldPen);
	m_specDC.SelectObject(oldFont);
	m_specReady = true;
	m_specDirty = false;
}

void CAnalyzerDlg::Present(CDC& dc, const CRect& rc, BOOL bAero)
{
	const int split = rc.top + (int)(rc.Height() * 0.65);
	const int waveH = split - rc.top;
	const int specH = rc.bottom - split;
	if (m_waveReady && m_waveDC.GetSafeHdc()) {
#if CCUSTOM_AERO_SUPPORT
		if (bAero) {
			// アクリル時は OnPaint 側のクロマキャッシュ経由で提示する
		}
		else
#endif
			dc.BitBlt(0, 0, m_waveW, waveH, &m_waveDC, 0, 0, SRCCOPY);
	}
	if (m_specReady && m_specDC.GetSafeHdc()) {
#if CCUSTOM_AERO_SUPPORT
		if (bAero) {
		}
		else
#endif
			dc.BitBlt(0, split, m_specW, specH, &m_specDC, 0, 0, SRCCOPY);
	}
	if (!bAero)
		dc.FillSolidRect(0, split - 1, rc.Width(), 2, RGB(60, 65, 80));
}

void CAnalyzerDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.IsRectEmpty()) return;

	const int split = rc.top + (int)(rc.Height() * 0.65);
	const int waveW = rc.Width();
	const int waveH = split - rc.top;
	const int specW = rc.Width();
	const int specH = rc.bottom - split;
	const int clientW = rc.Width();
	const int clientH = rc.Height();

#if CCUSTOM_AERO_SUPPORT
	const BOOL bAero = (savedata.aero == 1 && CCC_IsAeroEnabled() && CCC_IsWin11());
	const COLORREF bg = bAero ? ANALYZER_CHROMA_KEY : ANALYZER_BG;
#else
	const BOOL bAero = FALSE;
	const COLORREF bg = ANALYZER_BG;
#endif

	if (!EnsureWaveBuffer(dc, waveW, waveH) || !EnsureSpecBuffer(dc, specW, specH)) {
		dc.FillSolidRect(rc, ANALYZER_BG);
		return;
	}

	int pending = 0;
	EnterCriticalSection(&m_cs);
	pending = m_pendingScroll;
	LeaveCriticalSection(&m_cs);

	bool didWaveFull = false;
	bool didWaveScroll = false;
#if CCUSTOM_AERO_SUPPORT
	m_lastWaveScroll = 0;
#endif

	if (!m_waveReady) {
		FullRedrawWave(bg);
		didWaveFull = true;
	}
	else if (pending > 0) {
		const int scrolled = ScrollWaveAndDrawNew(bg);
		if (scrolled < 0) {
			didWaveFull = true;
		}
		else if (scrolled > 0) {
			didWaveScroll = true;
#if CCUSTOM_AERO_SUPPORT
			m_lastWaveScroll = scrolled;
#endif
		}
	}

	UpdateSpectrumFromRing();
	const bool didSpec = (!m_specReady || m_specDirty);
	if (didSpec)
		RedrawSpectrum(bg);

#if CCUSTOM_AERO_SUPPORT
	if (bAero) {
		if (m_chromaW != clientW || m_chromaH != clientH) {
			m_chromaCache.Release();
			m_chromaReady = false;
			m_chromaW = clientW;
			m_chromaH = clientH;
		}
		if (m_chromaCache.Ensure(dc.GetSafeHdc(), clientW, clientH)) {
			const COLORREF key = ANALYZER_CHROMA_KEY;
			if (m_waveReady && m_waveDC.GetSafeHdc()) {
				if (didWaveScroll && m_lastWaveScroll > 0 && m_chromaReady) {
					// 波形: キャッシュを横スクロールし、新規列だけクロマ変換
					m_chromaCache.ScrollCols(0, 0, waveW, waveH, m_lastWaveScroll);
					const int keep = waveW - m_lastWaveScroll;
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), keep, 0,
						keep, 0, m_lastWaveScroll, waveH, key);
					// 左端ラベルがスクロールで消えるので帯ラベル列だけ更新
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0,
						(std::min)(32, waveW), waveH, key);
				}
				else if (didWaveFull || !m_chromaReady) {
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0, waveW, waveH, key);
				}
			}
			if (m_specReady && m_specDC.GetSafeHdc() && (didSpec || !m_chromaReady)) {
				m_chromaCache.UpdateRect(m_specDC.GetSafeHdc(), 0, 0, 0, split, specW, specH, key);
			}
			m_chromaCache.FillOpaqueRect(0, split - 1, clientW, 2, RGB(60, 65, 80), key);
			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, 0, clientW, clientH);
		}
		else {
			Present(dc, rc, FALSE);
		}
	}
	else
#endif
	{
		Present(dc, rc, FALSE);
	}
}

BOOL CAnalyzerDlg::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CAnalyzerDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		CRect rc;
		GetWindowRect(&rc);
		if (!IsIconic()) {
			savedata.analyzerx = rc.left;
			savedata.analyzery = rc.top;
			savedata.analyzerw = rc.Width();
			savedata.analyzerh = rc.Height();
		}
		if (::IsWindow(m_hWnd) && IsWindowVisible()) {
			// 波形スクロール／周波数BB更新のため表示中はタイマー周期で再描画
			Invalidate(FALSE);
		}
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CAnalyzerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	ReleaseBuffers();
#if CCUSTOM_AERO_SUPPORT
	if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
		ApplyDwmBlur();
#endif
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
	if (bShow && CCC_IsAeroEnabled()) {
		ApplyDwmBlur();
		Invalidate(FALSE);
	}
#endif
}

void CAnalyzerDlg::OnClose()
{
	savedata.analyzerwindow = 0;
	DetachForDestroy();
	DestroyWindow();
}

BOOL CAnalyzerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
		PostMessage(WM_CLOSE);
		return TRUE;
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
