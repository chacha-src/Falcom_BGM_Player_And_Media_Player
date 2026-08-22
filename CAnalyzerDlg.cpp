// CAnalyzerDlg.cpp : 簡易波形アナライザー(スクロールBB・多ch・周波数特性)
#include "stdafx.h"
#include "ogg.h"
#include "CAnalyzerDlg.h"
#include "ProAudio.h"
#include "oggDlg.h"
#include "CEqualizer.h"
#include "CProToolsDlg.h"
#include "CMediaPlayerDlg.h"
#include <cmath>
#include <cstring>
#include <algorithm>

extern COggDlg* og;
extern int tempo;
extern int pitch;

static void AnalyzerMeterGeom(int channels, int& n, int& meterW, int& gap, int& totalW);
static int AnalyzerMeterStripWidth(int channels, int showLr);
static int AnalyzerWavePlotWidth(int waveW, int channels, int showLr);

IMPLEMENT_DYNAMIC(CAnalyzerDlg, CCustomBlurDialogExBase)

namespace
{
	static constexpr COLORREF ANALYZER_CHROMA_KEY = RGB(18, 20, 28);
	static constexpr COLORREF ANALYZER_BG = RGB(18, 20, 28);
	// アクリル時、ラベル下地を CHROMA にすると透過＋縁が消えて L/R が見えない
	static constexpr COLORREF ANALYZER_LABEL_PLATE = RGB(32, 36, 48);

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
			// DS/アップスケール出力は int32（IEEE float ではない）。Speana/ピアノロールと同じ正規化。
			const int* s = (const int*)pData;
			return (float)((double)s[frame * channels + ch] / 2147483648.0);
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

	bool CopyUnicodeToClipboard(HWND hwnd, const CString& text)
	{
		if (!hwnd || !::OpenClipboard(hwnd))
			return false;
		::EmptyClipboard();
		const SIZE_T bytes = (SIZE_T)(text.GetLength() + 1) * sizeof(WCHAR);
		HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (!hMem) {
			::CloseClipboard();
			return false;
		}
		void* p = ::GlobalLock(hMem);
		if (!p) {
			::GlobalFree(hMem);
			::CloseClipboard();
			return false;
		}
		memcpy(p, (LPCWSTR)text, (size_t)bytes);
		::GlobalUnlock(hMem);
		if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
			::GlobalFree(hMem);
			::CloseClipboard();
			return false;
		}
		::CloseClipboard();
		return true;
	}

	// oggDlg_ds.cpp の EQ_FREQS と同じ帯域(オーバーレイ用ローカル複製)
	static const float kEqFreqs[CAnalyzerDlg::EQ_OVERLAY_BANDS] = {
		25.0f, 40.0f, 63.0f, 100.0f, 160.0f,
		250.0f, 400.0f, 630.0f, 1000.0f, 1600.0f,
		2500.0f, 4000.0f, 6300.0f, 10000.0f, 16000.0f
	};

	// savedata.eq[0..14]: 100=0dB、偏差*0.12 → ±12dB (equaliser と同じ)
	inline float EqSliderToDb(int slider)
	{
		return ((float)slider - 100.0f) * 0.12f;
	}

	inline int EqDbToSlider(float gainDb)
	{
		int v = (int)(gainDb / 0.12f + 100.0f + (gainDb >= 0.0f ? 0.5f : -0.5f));
		if (v < 0) v = 0;
		if (v > 200) v = 200;
		return v;
	}

	void GetZoomFreqRange(int zoom, float nyquist, float& fMin, float& fMax)
	{
		fMin = 20.0f;
		fMax = (std::max)(fMin * 1.01f, nyquist);
		if (zoom == CAnalyzerDlg::ZoomLow) {
			fMax = (std::min)(fMax, 250.0f);
		}
		else if (zoom == CAnalyzerDlg::ZoomMid) {
			fMin = 250.0f;
			fMax = (std::min)(fMax, 4000.0f);
			if (fMax <= fMin) fMax = fMin * 1.01f;
		}
		else if (zoom == CAnalyzerDlg::ZoomHigh) {
			fMin = 4000.0f;
			if (fMin >= fMax) fMin = fMax * 0.5f;
		}
	}

	inline float HzToPlotX(float hz, const CRect& plot, float fMin, float fMax)
	{
		float t = logf((std::max)(hz, fMin) / fMin) / logf(fMax / fMin);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return (float)(plot.left + (int)(t * plot.Width()));
	}

	inline float PlotXToHz(int x, const CRect& plot, float fMin, float fMax)
	{
		if (plot.Width() <= 0) return fMin;
		float t = (float)(x - plot.left) / (float)plot.Width();
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return fMin * powf(fMax / fMin, t);
	}

	COLORREF WaterfallColor(float db)
	{
		// -96..0 → 青→緑→黄→白
		float t = (db + 96.0f) / 96.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		int r, g, b;
		if (t < 0.33f) {
			float u = t / 0.33f;
			r = (int)(20 + u * 20);
			g = (int)(30 + u * 120);
			b = (int)(60 + u * 160);
		}
		else if (t < 0.66f) {
			float u = (t - 0.33f) / 0.33f;
			r = (int)(40 + u * 180);
			g = (int)(150 + u * 80);
			b = (int)(220 - u * 180);
		}
		else {
			float u = (t - 0.66f) / 0.34f;
			r = (int)(220 + u * 35);
			g = (int)(230 + u * 25);
			b = (int)(40 + u * 200);
		}
		if (r > 255) r = 255;
		if (g > 255) g = 255;
		if (b > 255) b = 255;
		return RGB(r, g, b);
	}

	// TP/LUFS は簡易計測。ツールチップでその旨を明示する(誤解防止)。
	inline LPCTSTR AnalyzerTpLufsTipText()
	{
		return LL14(
			L"簡易計測の目安です。TP はサンプル間補間による概算ピーク、LUFS は K特性を省いた瞬時ラウドネスの近似で、放送規格の厳密な値ではありません。",
			L"Rough in-app estimate. TP is an approximate inter-sample peak and LUFS is a momentary loudness approximation without full K-weighting; not a broadcast-accurate measurement.",
			L"Estimation approximative. TP est un pic inter-echantillons approche et LUFS une approximation de la sonie momentanee sans ponderation K complete; ce ne sont pas des mesures normalisees.",
			L"Stima approssimativa. TP e un picco inter-campione approssimato e LUFS una loudness momentanea senza ponderazione K completa; non sono misure da broadcast.",
			L"Estimacion aproximada. TP es un pico entre muestras aproximado y LUFS una sonoridad momentanea sin ponderacion K completa; no son medidas de norma broadcast.",
			L"간이 측정 값입니다. TP는 샘플 간 보간에 의한 근사 피크, LUFS는 K 가중을 생략한 순시 라우드니스 근사이며 방송 규격의 정확한 값이 아닙니다.",
			L"仅为简易估算。TP 为采样间插值的近似峰值，LUFS 为省略 K 计权的瞬时响度近似，并非广播标准的精确值。",
			L"تقدير تقريبي فقط. TP ذروة تقريبية بين العينات، وLUFS تقريب للجهارة اللحظية بدون ترجيح K كامل، وليست قياسات معيارية للبث.",
			L"Приблизительная оценка. TP - примерный межсэмпловый пик, LUFS - приближение мгновенной громкости без полного K-взвешивания; это не вещательные измерения.",
			L"Grobe Schatzung. TP ist ein naherungsweiser Inter-Sample-Peak und LUFS eine momentane Lautheitsnaherung ohne vollstandige K-Bewertung; keine sendetauglichen Messwerte.",
			L"Estimativa aproximada. TP e um pico entre amostras aproximado e LUFS uma aproximacao de loudness momentanea sem ponderacao K completa; nao sao medidas de broadcast.",
			L"Ruwe schatting. TP is een benaderde inter-sample piek en LUFS een momentane luidheidsbenadering zonder volledige K-weging; geen broadcast-nauwkeurige meting.",
			L"Szacunkowy pomiar. TP to przyblizony szczyt miedzy probkami, a LUFS to przyblizenie chwilowej glosnosci bez pelnego wazenia K; to nie sa pomiary emisyjne.",
			L"Kaba bir tahmindir. TP ornekler arasi yaklasik tepe, LUFS ise tam K agirliklandirmasi olmayan anlik ses yuksekligi yaklasimidir; yayin standardi olcumu degildir.");
	}

	enum {
		IDM_SPEC_OVERLAY = 42001,
		IDM_SPEC_SPLIT_V = 42002,
		IDM_SPEC_SPLIT_H = 42003,
		IDM_SPEC_GRID4 = 42004,
		IDM_SPEC_GRID8 = 42005,
		IDM_STYLE_FILL = 42010,
		IDM_STYLE_LINE = 42011,
		IDM_STYLE_BARS = 42012,
		IDM_STYLE_CUBASE = 42013,
		IDM_STYLE_SPAN = 42014,
		IDM_STYLE_ABLETON = 42015,
		IDM_STYLE_FABFILTER = 42016,
		IDM_PEAK_HOLD = 42020,
		IDM_EQ_OVERLAY = 42021,
		IDM_FREEZE = 42022,
		IDM_RESET_PEAK = 42023,
		IDM_LEVEL_METER = 42024,
		IDM_ALWAYS_ON_TOP = 42025,
		IDM_CLEAR_DISPLAY = 42026,
		IDM_COPY_HOVER = 42027,
		IDM_COPY_PEAK = 42028,
		IDM_COPY_LEVELS = 42029,
		IDM_WAVE_SCROLL = 42030,
		IDM_WAVE_TRIGGER = 42031,
		IDM_LOWER_SPECTRUM = 42032,
		IDM_LOWER_WATERFALL = 42033,
		IDM_LOWER_PHASE = 42034,
		IDM_SPEC_DIFF = 42035,
		IDM_SPEC_SNAP = 42036,
		IDM_SPEC_SNAP_CLEAR = 42037,
		IDM_ZOOM_FULL = 42038,
		IDM_ZOOM_LOW = 42039,
		IDM_ZOOM_MID = 42040,
		IDM_ZOOM_HIGH = 42041,
		IDM_MARKER_ADD = 42042,
		IDM_MARKER_REMOVE = 42043,
		IDM_MARKER_CLEAR = 42044,
		IDM_CORR_METER = 42045,
		IDM_SAVE_JACKET = 42050,
		IDM_MS_NARROW = 42051,
		IDM_MS_WIDE = 42052,
		IDM_MS_MONO = 42053,
		IDM_MS_RESET = 42054,
		IDM_MS_FROM_CORR = 42055,
		IDM_WAVE_ENVELOPE = 42070,
		IDM_WAVE_MS = 42071,
		IDM_WAVE_LOUDNESS = 42072,
		IDM_WAVE_PHOSPHOR = 42073,
		IDM_LOWER_VECTOR = 42080,
		IDM_LOWER_RTA = 42081,
		IDM_LOWER_CORRFREQ = 42082,
		IDM_LOWER_PHASEFREQ = 42083,

		IDM_VIEW_TOP_2D = 42300,
		IDM_VIEW_TOP_3D = 42301,
		IDM_VIEW_BOT_2D = 42302,
		IDM_VIEW_BOT_3D = 42303,
		IDM_SOFT3D_API_DEMO = 42304,
		IDM_VIEW_TOP_RESET = 42308,
		IDM_VIEW_BOT_RESET = 42309,

		IDM_WAVE_SPEED_BASE = 42100, // +0..WAVE_SPEED_COUNT-1
		WM_ANALYZER_SPEC_DONE = WM_APP + 510,
		WM_ANALYZER_PRESENT = WM_APP + 511,
		WM_ANALYZER_SYNC = WM_APP + 512
	};
}

namespace {

class CAnHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_AN_HELP };
	explicit CAnHelpDlg(CWnd* pParent = nullptr)
		: CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CAnHelpDlg* g_anHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CAnHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CAnHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"アナライザー操作ガイド", L"Analyzer Guide", L"Guide analyseur", L"Guida analizzatore",
		L"Guía del analizador", L"분석기 가이드", L"分析器指南", L"دليل المحلل",
		L"Руководство анализатора", L"Analysator-Anleitung", L"Guia do analisador", L"Analyser-gids",
		L"Przewodnik analizatora", L"Analizör kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CAnHelpDlg::OnOK() { DestroyWindow(); }
void CAnHelpDlg::OnCancel() { DestroyWindow(); }
void CAnHelpDlg::OnClose() { DestroyWindow(); }

void CAnHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_anHelpDlg == this)
		g_anHelpDlg = nullptr;
	delete this;
}

BOOL CAnHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CAnHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* baseFont = GetFont();
	CFont boldFont;
	{
		LOGFONT lf = {};
		if (baseFont && baseFont->GetSafeHandle())
			baseFont->GetLogFont(&lf);
		else {
			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
			lf = ncm.lfMessageFont;
		}
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
	}
	CFont* oldFont = dc.SelectObject(baseFont);

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 2;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		CFont* prev = dc.SelectObject(&boldFont);
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, t);
		dc.SelectObject(prev);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 120));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"アナライザー操作ガイド", L"Analyzer — Guide", L"Guide analyseur", L"Guida analizzatore",
		L"Guía analizador", L"분석기 가이드", L"分析器指南", L"دليل المحلل",
		L"Анализатор — руководство", L"Analysator-Guide", L"Guia analisador", L"Analyser-gids",
		L"Analizator — przewodnik", L"Analizör kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"再生中の PCM を上部の波形と下部の周波数表示で監視します。右クリックで切替。",
		L"Watch live PCM: wave on top, spectrum below. Right-click to switch modes.",
		L"Surveillez le PCM : onde en haut, spectre en bas. Clic droit pour changer.",
		L"Monitora il PCM: onda in alto, spettro sotto. Tasto destro per cambiare.",
		L"Vea el PCM: onda arriba, espectro abajo. Clic derecho para cambiar.",
		L"재생 PCM을 상단 파형·하단 스펙트럼으로 감시. 우클릭으로 전환.",
		L"监视播放 PCM：上方波形、下方频谱。右键切换模式。",
		L"راقب PCM: موجة أعلى وطيف أسفل. يمين للتبديل.",
		L"Следите за PCM: волна сверху, спектр снизу. ПКМ — смена режима.",
		L"PCM beobachten: Welle oben, Spektrum unten. Rechtsklick wechselt.",
		L"Monitore o PCM: onda em cima, espectro abaixo. Direito troca modos.",
		L"Bekijk PCM: golf boven, spectrum onder. Rechtsklik wisselt.",
		L"Obserwuj PCM: fala u góry, widmo na dole. PPM przełącza.",
		L"PCM izle: üstte dalga, altta spektrum. Sağ tık ile değiştir."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KSPECTRUM);

	title(L, y, LL14(L"表示レイアウト", L"Display layout", L"Disposition", L"Layout",
		L"Diseño", L"표시 레이아웃", L"显示布局", L"تخطيط العرض",
		L"Макет", L"Anordnung", L"Layout", L"Indeling",
		L"Układ", L"Düzen"));
	y += titleLh;
	body(L, y, LL14(
		L"・上部 …… スクロール／エンベロープ／Mid-Side／ラウドネス／トリガー／残光。速度は右クリック",
		L"· Upper …… scroll / envelope / M-S / loudness / trigger / phosphor. Speed via RMB",
		L"· Haut …… onde / enveloppe / M-S / loudness / trigger / phosphore. Vitesse clic droit",
		L"· Alto …… onda / inviluppo / M-S / loudness / trigger / fosforo. Velocità destro",
		L"· Superior …… onda / envolvente / M-S / loudness / trigger / fósforo. Velocidad clic der.",
		L"· 상단 …… 스크롤 / 엔벨로프 / M-S / 라우드니스 / 트리거 / 잔광. 우클릭으로 속도",
		L"· 上部 …… 滚动/包络/M-S/响度/触发/余辉。右键调速度",
		L"· أعلى …… تمرير / غلاف / M-S / جهارة / محفز / فسفور. السرعة يمين",
		L"· Верх …… прокрутка / огибающая / M-S / громкость / триггер / фосфор. Скорость ПКМ",
		L"· Oben …… Scroll / Hüllkurve / M-S / Loudness / Trigger / Phosphor. Tempo RMB",
		L"· Topo …… rolagem / envelope / M-S / loudness / trigger / fósforo. Velocidade direito",
		L"· Boven …… scroll / omhullende / M-S / loudness / trigger / fosfor. Snelheid RMB",
		L"· Góra …… scroll / obwiednia / M-S / głośność / trigger / fosfor. Prędkość PPM",
		L"· Üst …… kaydırma / zarf / M-S / loudness / tetik / fosfor. Hız sağ tık")); y += lh;
	body(L, y, LL14(
		L"・下部 …… スペクトラム／1/3oct RTA／スペクトログラム／位相／ゴニオ／帯域相関／帯域位相",
		L"· Lower …… spectrum / 1/3-oct RTA / spectrogram / phase / gonio / corr vs Hz / phase vs Hz",
		L"· Bas …… spectre / RTA 1/3 / spectrogramme / phase / gonio / corr. / phase vs Hz",
		L"· Basso …… spettro / RTA 1/3 / spettrogramma / fase / gonio / corr. / fase vs Hz",
		L"· Inferior …… espectro / RTA 1/3 / espectrograma / fase / gonio / corr. / fase vs Hz",
		L"· 하단 …… 스펙트럼 / 1/3oct RTA / 스펙트로그램 / 위상 / 고니오 / 대역상관 / 대역위상",
		L"· 下部 …… 频谱 / 1/3oct RTA / 频谱图 / 相位 / 测向 / 频段相关 / 频段相位",
		L"· أسفل …… طيف / RTA / مخطط / طور / اتجاه / ترابط / طور vs هرتز",
		L"· Низ …… спектр / RTA 1/3 / спектрограмма / фаза / гонио / корр. / фаза vs Гц",
		L"· Unten …… Spektrum / 1/3-Okt-RTA / Spektrogramm / Phase / Gonio / Korr. / Phase vs Hz",
		L"· Inferior …… espectro / RTA 1/3 / espectrograma / fase / gonio / corr. / fase vs Hz",
		L"· Onder …… spectrum / 1/3-oct RTA / spectrogram / fase / gonio / corr. / fase vs Hz",
		L"· Dół …… widmo / RTA 1/3 / spektrogram / faza / gonio / kor. / faza vs Hz",
		L"· Alt …… spektrum / 1/3oct RTA / spektrogram / faz / gonyo / korelasyon / faz vs Hz")); y += lh;
	body(L, y, LL14(
		L"・右端メーター …… 波形は LR／φ の左から始まります。LR を消しても φ は右端に残ります",
		L"· Right meters …… the wave starts left of LR / φ. Hide LR and φ stays at the right edge",
		L"· Compteurs à droite …… l'onde commence à gauche de LR / φ. Sans LR, φ reste à droite",
		L"· Meter a destra …… l'onda inizia a sinistra di LR / φ. Senza LR, φ resta a destra",
		L"· Medidores a la derecha …… la onda empieza a la izq. de LR / φ. Sin LR, φ queda a la der.",
		L"· 오른쪽 미터 …… 파형은 LR/φ 왼쪽부터. LR를 꺼도 φ는 오른쪽에 남음",
		L"· 右端表头 …… 波形从 LR／φ 左侧开始。关掉 LR 后 φ 仍留在右端",
		L"· المقاييس يميناً …… تبدأ الموجة يسار LR / φ. دون LR يبقى φ يميناً",
		L"· Метры справа …… волна слева от LR / φ. Без LR φ остаётся справа",
		L"· Rechte Anzeigen …… Welle links von LR / φ. Ohne LR bleibt φ rechts",
		L"· Medidores à direita …… a onda começa à esq. de LR / φ. Sem LR, φ fica à dir.",
		L"· Meters rechts …… golf start links van LR / φ. Zonder LR blijft φ rechts",
		L"· Mierniki z prawej …… fala od lewej LR / φ. Bez LR φ zostaje z prawej",
		L"· Sağ ölçerler …… dalga LR / φ solundan başlar. LR kapalı olsa da φ sağda kalır")); y += lh;
	body(L, y, LL14(
		L"・周波数モード …… 塗+線 / バー / Cubase・SPAN・Ableton・FabFilter 風など",
		L"· Spectrum style …… fill+line / bars / Cubase·SPAN·Ableton·FabFilter looks",
		L"· Style spectre …… rempl.+ligne / barres / styles DAW",
		L"· Stile spettro …… riemp.+linea / barre / stili DAW",
		L"· Estilo espectro …… relleno+línea / barras / estilos DAW",
		L"· 스펙트럼 스타일 …… 채움+선 / 막대 / Cubase·SPAN 등",
		L"· 频谱样式 …… 填充+线 / 柱状 / Cubase·SPAN 等风格",
		L"· نمط الطيف …… تعبئة+خط / أشرطة / أنماط DAW",
		L"· Стиль спектра …… заливка+линия / столбцы / стили DAW",
		L"· Spektrumstil …… Füllung+Linie / Balken / DAW-Looks",
		L"· Estilo espectro …… preench.+linha / barras / estilos DAW",
		L"· Spectrumstijl …… vulling+lijn / balken / DAW-stijlen",
		L"· Styl widma …… wypełn.+linia / słupki / style DAW",
		L"· Spektrum stili …… dolgu+çizgi / çubuk / DAW görünümleri")); y += lh + 2;

	// mini layout diagram
	{
		const int gx = L, gy = y, gw = min(300, rc.Width() - L * 2), gh = lh * 3 + 8;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
		dc.FillSolidRect(gx + 4, gy + 4, gw - 8, gh / 2 - 4, RGB(40, 80, 120));
		dc.SetTextColor(RGB(230, 240, 255));
		dc.TextOut(gx + 10, gy + 6, L"WAVE");
		const int by = gy + gh / 2 + 2;
		const int barH = gh / 2 - 6;
		const int barW = 8;
		const int bars[] = { 40, 70, 55, 85, 60, 45, 75, 50 };
		for (int i = 0; i < 8; ++i) {
			const int h = barH * bars[i] / 100;
			dc.FillSolidRect(gx + 10 + i * (barW + 3), by + barH - h, barW, h, RGB(80, 180, 220));
		}
		dc.SetTextColor(RGB(55, 55, 70));
		dc.TextOut(gx + 10 + 8 * (barW + 3) + 8, by + 2, L"SPEC");
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		y = gy + gh + 4;
	}

	title(L, y, LL14(L"ズーム・マーカー・計測", L"Zoom, markers, meters", L"Zoom, marqueurs, mesures", L"Zoom, marker, misure",
		L"Zoom, marcadores, medidores", L"줌·마커·계측", L"缩放、标记、计量", L"تكبير وعلامات ومقاييس",
		L"Зум, маркеры, метры", L"Zoom, Marker, Anzeigen", L"Zoom, marcadores, medidores", L"Zoom, markers, meters",
		L"Zoom, znaczniki, mierniki", L"Zoom, işaretler, ölçerler"));
	y += titleLh;
	body(L, y, LL14(
		L"・周波数範囲 …… 全帯域 / 低域(20–250Hz) / 中域 / 高域(4kHz〜)",
		L"· Freq. range …… full / low (20–250 Hz) / mid / high (4 kHz+)",
		L"· Plage …… complète / graves / médiums / aigus (4 kHz+)",
		L"· Intervallo …… intero / bassi / medi / alti (4 kHz+)",
		L"· Rango …… completo / graves / medios / agudos (4 kHz+)",
		L"· 주파수 범위 …… 전대역 / 저역 / 중역 / 고역(4kHz+)",
		L"· 频率范围 …… 全频 / 低频 / 中频 / 高频(4kHz+)",
		L"· النطاق …… كامل / منخفض / متوسط / عالي (4 كيلوهرتز+)",
		L"· Диапазон …… весь / низ / середина / верх (от 4 кГц)",
		L"· Bereich …… voll / Tiefen / Mitten / Höhen (ab 4 kHz)",
		L"· Faixa …… completa / graves / médios / agudos (4 kHz+)",
		L"· Bereik …… volledig / laag / midden / hoog (4 kHz+)",
		L"· Zakres …… pełny / niskie / średnie / wysokie (od 4 kHz)",
		L"· Aralık …… tam / bas / orta / tiz (4 kHz+)")); y += lh;
	body(L, y, LL14(
		L"・ホバーで Hz/dB 読取。マーカー追加/削除。レベルメーター・位相相関も切替可",
		L"· Hover shows Hz/dB. Add/remove markers. Toggle level & correlation meters",
		L"· Survol = Hz/dB. Marqueurs. Compteurs niveau / corrélation",
		L"· Hover = Hz/dB. Marker. Misuratori livello / correlazione",
		L"· Hover = Hz/dB. Marcadores. Medidores nivel / correlación",
		L"· 호버로 Hz/dB. 마커 추가/삭제. 레벨·상관 미터 토글",
		L"· 悬停显示 Hz/dB。可加删标记。可切换电平/相关表",
		L"· التمرير = هرتز/ديسيبل. علامات. مقاييس المستوى/الترابط",
		L"· Наведение = Гц/дБ. Маркеры. Уровень / корреляция",
		L"· Hover = Hz/dB. Marker. Pegel- / Korrelationsanzeige",
		L"· Hover = Hz/dB. Marcadores. Medidores nível / correlação",
		L"· Hover = Hz/dB. Markers. Niveau- / correlatiemeter",
		L"· Hover = Hz/dB. Znaczniki. Poziom / korelacja",
		L"· Hover = Hz/dB. İşaretler. Seviye / korelasyon")); y += lh;
	body(L, y, LL14(
		L"・TP/LUFS …… 波形付近に簡易 True Peak と LUFS。ホバーで計測の注意が出ます",
		L"· TP/LUFS …… simple True Peak & LUFS near the wave; hover for notes",
		L"· TP/LUFS …… True Peak et LUFS simples près de l'onde",
		L"· TP/LUFS …… True Peak e LUFS semplici vicino all'onda",
		L"· TP/LUFS …… True Peak y LUFS simples junto a la onda",
		L"· TP/LUFS …… 파형 근처 간이 True Peak·LUFS. 호버로 안내",
		L"· TP/LUFS …… 波形旁简易 True Peak 与 LUFS；悬停有说明",
		L"· TP/LUFS …… قمة حقيقية و LUFS بسيطان قرب الموجة",
		L"· TP/LUFS …… простой True Peak и LUFS у волны",
		L"· TP/LUFS …… einfaches True Peak & LUFS an der Welle",
		L"· TP/LUFS …… True Peak e LUFS simples perto da onda",
		L"· TP/LUFS …… eenvoudige True Peak & LUFS bij de golf",
		L"· TP/LUFS …… prosty True Peak i LUFS przy fali",
		L"· TP/LUFS …… dalga yanında basit True Peak ve LUFS")); y += lh;
	body(L, y, LL14(
		L"・スペクトログラムを画像保存 …… 右クリックで PNG/BMP に書き出し（ジャケット用）",
		L"· Save spectrogram image …… RMB exports PNG/BMP (for jacket use)",
		L"· Sauver spectro …… clic droit PNG/BMP",
		L"· Salva spettrogramma …… destro PNG/BMP",
		L"· Guardar espectrograma …… clic der. PNG/BMP",
		L"· 스펙트로그램 이미지 저장 …… 우클릭으로 PNG/BMP",
		L"· 保存频谱图 …… 右键导出 PNG/BMP（封面用）",
		L"· حفظ المخطط الطيفي …… يمين PNG/BMP",
		L"· Сохранить спектрограмму …… ПКМ PNG/BMP",
		L"· Spektrogramm speichern …… RMB PNG/BMP",
		L"· Salvar espectrograma …… direito PNG/BMP",
		L"· Spectrogram opslaan …… RMB PNG/BMP",
		L"· Zapisz spektrogram …… PPM PNG/BMP",
		L"· Spektrogram kaydet …… sag tik PNG/BMP")); y += lh;
	body(L, y, LL14(
		L"・相関→M/S …… 右クリックの相関ヒストから狭める/広げる/モノを提案",
		L"· Corr→M/S …… RMB suggests narrow/wide/mono from corr history",
		L"· Corr→M/S …… clic droit propose d apres l historique",
		L"· Corr→M/S …… destro propone da cronologia",
		L"· Corr→M/S …… clic der. sugiere desde historial",
		L"· 상관→M/S …… 우클릭 이력으로 좁힘/넓힘/모노 제안",
		L"· 相关→M/S …… 右键根据历史建议收窄/加宽/单声道",
		L"· ارتباط→M/S …… يمين يقترح من السجل",
		L"· Корр→M/S …… ПКМ предлагает по истории",
		L"· Korr→M/S …… RMB schlagt aus Verlauf vor",
		L"· Corr→M/S …… direito sugere pelo historico",
		L"· Corr→M/S …… RMB stelt voor uit geschiedenis",
		L"· Kor→M/S …… PPM proponuje z historii",
		L"· Kor→M/S …… sag tik gecmisten onerir")); y += lh;

	title(L, y, LL14(L"操作・ウィンドウ", L"Actions & window", L"Actions et fenêtre", L"Azioni e finestra",
		L"Acciones y ventana", L"조작·창", L"操作与窗口", L"إجراءات والنافذة",
		L"Действия и окно", L"Aktionen & Fenster", L"Ações e janela", L"Acties & venster",
		L"Akcje i okno", L"İşlemler ve pencere"));
	y += titleLh;
	body(L, y, LL14(
		L"・右クリック …… レイアウト/スタイル/差分/フリーズ/コピー/常に手前 など",
		L"· Right-click …… layout/style/diff/freeze/copy/always-on-top, etc.",
		L"· Clic droit …… disposition/style/diff/gel/copie/premier plan…",
		L"· Destro …… layout/stile/diff/congelamento/copia/primo piano…",
		L"· Clic der. …… diseño/estilo/diff/congelar/copiar/siempre visible…",
		L"· 우클릭 …… 레이아웃/스타일/차분/정지/복사/항상 위 등",
		L"· 右键 …… 布局/样式/差分/冻结/复制/置顶等",
		L"· يمين …… تخطيط/نمط/فرق/تجميد/نسخ/دائماً أعلى…",
		L"· ПКМ …… макет/стиль/разница/заморозка/копия/поверх…",
		L"· Rechtsklick …… Layout/Stil/Diff/Freeze/Kopieren/immer oben…",
		L"· Direito …… layout/estilo/diff/congelar/copiar/sempre no topo…",
		L"· Rechtsklik …… layout/stijl/diff/bevriezen/kopiëren/boven…",
		L"· PPM …… układ/styl/różnica/zamroź/kopiuj/zawsze na wierzchu…",
		L"· Sağ tık …… düzen/stil/fark/dondur/kopyala/her zaman üstte…")); y += lh;
	body(L, y, LL14(
		L"・キー …… W 上部モード / S 下部モード / M レベル / F フリーズ / P ピーク / E EQ",
		L"· Keys …… W upper mode / S lower mode / M levels / F freeze / P peak / E EQ",
		L"· Touches …… W haut / S bas / M niveaux / F gel / P crête / E EQ",
		L"· Tasti …… W alto / S basso / M livelli / F gelo / P picco / E EQ",
		L"· Teclas …… W superior / S inferior / M niveles / F congelar / P pico / E EQ",
		L"· 키 …… W 상단 모드 / S 하단 모드 / M 레벨 / F 프리즈 / P 피크 / E EQ",
		L"· 按键 …… W 上部模式 / S 下部模式 / M 电平 / F 冻结 / P 峰值 / E EQ",
		L"· مفاتيح …… W أعلى / S أسفل / M مستوى / F تجميد / P ذروة / E EQ",
		L"· Клавиши …… W верх / S низ / M уровень / F заморозка / P пик / E EQ",
		L"· Tasten …… W oben / S unten / M Pegel / F Freeze / P Peak / E EQ",
		L"· Teclas …… W topo / S baixo / M níveis / F congelar / P pico / E EQ",
		L"· Toetsen …… W boven / S onder / M niveau / F bevriezen / P piek / E EQ",
		L"· Klawisze …… W góra / S dół / M poziom / F zamroź / P szczyt / E EQ",
		L"· Tuşlar …… W üst / S alt / M seviye / F dondur / P tepe / E EQ")); y += lh;
	body(L, y, LL14(
		L"・EQオーバーレイ …… 曲線をドラッグして帯域ゲインを直接いじれます",
		L"· EQ overlay …… drag the curve to tweak band gains live",
		L"· Superposition EQ …… glisser la courbe pour les gains",
		L"· Overlay EQ …… trascina la curva per i gain",
		L"· Superposición EQ …… arrastre la curva para ganancias",
		L"· EQ 오버레이 …… 곡선을 드래그해 밴드 게인 조절",
		L"· EQ 叠加 …… 拖动曲线直接调频段增益",
		L"· تراكب EQ …… اسحب المنحنى لتعديل الكسب",
		L"· Оверлей EQ …… тяните кривую для усиления полос",
		L"· EQ-Overlay …… Kurve ziehen für Bandverstärkung",
		L"· Overlay EQ …… arraste a curva para ganhos",
		L"· EQ-overlay …… sleep de curve voor bandgains",
		L"· Nakładka EQ …… przeciągnij krzywą, by zmienić wzmocnienie",
		L"· EQ kaplama …… eğriyi sürükleyerek bant kazancını ayarla")); y += lh;
	body(L, y, LL14(
		L"・ウィンドウ …… 枠をドラッグで移動、端でリサイズ。位置/サイズは記憶されます",
		L"· Window …… drag the frame to move, edges to resize. Position is remembered",
		L"· Fenêtre …… glisser pour déplacer, bords pour redimensionner",
		L"· Finestra …… trascina per spostare, bordi per ridimensionare",
		L"· Ventana …… arrastre para mover, bordes para redimensionar",
		L"· 창 …… 테두리 드래그로 이동, 가장자리로 크기. 위치 저장",
		L"· 窗口 …… 拖边框移动、拖边缘缩放；位置会记住",
		L"· النافذة …… اسحب للإزاحة والحواف للتحجيم. يُحفظ الموضع",
		L"· Окно …… перетащите рамку / края. Позиция запоминается",
		L"· Fenster …… Rahmen ziehen / Ränder skalieren. Position merken",
		L"· Janela …… arraste a moldura / bordas. Posição lembrada",
		L"· Venster …… sleep kader / randen. Positie wordt onthouden",
		L"· Okno …… przeciągnij ramkę / krawędzie. Pozycja zapamiętana",
		L"· Pencere …… çerçeveyi sürükle, kenarlardan boyutlandır. Konum hatırlanır")); y += lh + 2;

	title(L, y, LL14(L"簡易3D(Soft3D)表示", L"Soft 3D view", L"Vue 3D simplifiée", L"Vista 3D semplificata",
		L"Vista 3D simple", L"간이 3D(Soft3D) 표시", L"简易3D(Soft3D)显示", L"عرض Soft3D",
		L"Простой 3D-вид", L"Soft-3D-Ansicht", L"Vista Soft 3D", L"Eenvoudige 3D-weergave",
		L"Widok Soft 3D", L"Soft 3B görünüm"));
	y += titleLh;
	y = CCC_GdiHelpDrawSoft3DDemo(dc, L, y, min(280, rc.Width() - L * 2), min(100, max(72, rc.Height() / 6)),
		CCC_HELPDEMO_KWAVE);
	body(L, y, LL14(
		L"・右クリック「表示」→ 簡易3D で上部／下部ペインを CPU 描画の透視表示に切替",
		L"· Right-click → View → Soft 3D switches the upper/lower pane to a CPU-drawn 3D view",
		L"· Clic droit → Affichage → 3D simplifiée pour les panneaux",
		L"· Destro → Vista → 3D semplificato per i riquadri",
		L"· Clic der. → Vista → 3D simple para los paneles",
		L"· 우클릭 「표시」→ 간이 3D 로 상단/하단 패널을 CPU 투시 표시로 전환",
		L"· 右键「显示」→ 简易3D 将上/下窗格切换为 CPU 透视绘制",
		L"· يمين ← العرض ← Soft 3D لتحويل اللوحين إلى عرض ثلاثي بالمعالج",
		L"· ПКМ → Вид → простой 3D для верхней/нижней панели",
		L"· Rechtsklick → Ansicht → Soft 3D für obere/untere Anzeige",
		L"· Direito → Exibir → Soft 3D para os painéis",
		L"· Rechtsklik → Weergave → Soft 3D voor de panelen",
		L"· PPM → Widok → Soft 3D dla paneli",
		L"· Sağ tık → Görünüm → Soft 3B ile panelleri çevir")); y += lh;
	body(L, y, LL14(
		L"・ペイン内を左ドラッグで視点回転、ホイールでズーム(0.35〜4.0倍)",
		L"· Left-drag inside a pane to orbit, wheel to zoom (0.35x–4.0x)",
		L"· Glisser gauche = orbite, molette = zoom (0,35–4,0x)",
		L"· Trascina sinistro = orbita, rotella = zoom (0,35–4,0x)",
		L"· Arrastrar izq. = órbita, rueda = zoom (0,35–4,0x)",
		L"· 패널 안 좌드래그로 시점 회전, 휠로 줌(0.35~4.0배)",
		L"· 窗格内左键拖动旋转视角，滚轮缩放（0.35～4.0 倍）",
		L"· سحب أيسر للدوران، العجلة للتكبير (0.35–4.0)",
		L"· ЛКМ-перетаскивание — облёт, колесо — зум (0,35–4,0x)",
		L"· Linksziehen = Orbit, Rad = Zoom (0,35–4,0x)",
		L"· Arrastar esq. = órbita, roda = zoom (0,35–4,0x)",
		L"· Links slepen = orbit, wiel = zoom (0,35–4,0x)",
		L"· Przeciąganie LPM = orbita, kółko = zoom (0,35–4,0x)",
		L"· Sol sürükleme = yörünge, teker = zoom (0,35–4,0x)")); y += lh;
	body(L, y, LL14(
		L"・0 キーで視点を既定へ。視点は上下ペイン別に保存されます(重いときは 2D へ)",
		L"· Press 0 to reset the camera. Each pane keeps its own camera (go 2D if it feels heavy)",
		L"· 0 = réinitialiser la caméra ; chaque panneau garde la sienne",
		L"· 0 = reset camera; ogni riquadro ha la propria",
		L"· 0 = reiniciar cámara; cada panel guarda la suya",
		L"· 0 키로 시점 초기화. 시점은 상·하 패널별로 저장(무거우면 2D)",
		L"· 按 0 复位视角。上下窗格各自保存视角（卡就回 2D）",
		L"· 0 لتصفير الكاميرا؛ كل لوح يحفظ كاميرته",
		L"· 0 — сброс камеры; у каждой панели своя",
		L"· 0 = Kamera zurücksetzen; jede Anzeige merkt ihre eigene",
		L"· 0 = redefinir câmera; cada painel guarda a sua",
		L"· 0 = camera resetten; elk paneel bewaart de eigen stand",
		L"· 0 = reset kamery; każdy panel ma własną",
		L"· 0 = kamerayı sıfırla; her panel kendi kamerasını tutar")); y += lh;
	body(L, y, LL14(
		L"・ボタンやチェックなど CCustom にも小さな Soft3D 飾りがあります（全面 Soft3D とは別）",
		L"· CCustom buttons/checks also get tiny Soft 3D accents (separate from full Soft 3D view)",
		L"· Boutons/cases CCustom ont aussi de petits accents Soft 3D (hors vue Soft 3D)",
		L"· Pulsanti/check CCustom hanno anche piccoli accenti Soft 3D (oltre alla vista Soft 3D)",
		L"· Botones/casillas CCustom también llevan Soft 3D pequeño (aparte de la vista Soft 3D)",
		L"· 버튼·체크 등 CCustom에도 작은 Soft3D 장식(전체 Soft3D와 별개)",
		L"· 按钮/复选等 CCustom 也有细小 Soft3D 装饰（与整窗 Soft3D 不同）",
		L"· أزرار/مربعات CCustom لها أيضاً زخارف Soft3D صغيرة (غير العرض الكامل)",
		L"· Кнопки/флажки CCustom — мелкие Soft 3D-акценты (не полный Soft 3D-вид)",
		L"· CCustom-Buttons/Checks haben auch kleine Soft-3D-Akzente (neben Soft-3D-Ansicht)",
		L"· Botões/checks CCustom também têm Soft 3D pequeno (além da vista Soft 3D)",
		L"· CCustom-knoppen/checks hebben ook Soft 3D-accenten (naast Soft 3D-weergave)",
		L"· Przyciski/checkboxy CCustom mają też Soft 3D (oprócz widoku Soft 3D)",
		L"· CCustom düğme/onayda da Soft 3B süs (tam Soft 3B görünümden ayrı)")); y += lh + 2;

	muted(L, y, LL14(
		L"キャプションの「?」または右クリック「操作ガイド」でもこの画面を開けます。",
		L"Open this sheet from caption \"?\" or right-click → Operation guide.",
		L"Ouvrir via « ? » ou clic droit → Guide d'utilisation.",
		L"Apri da « ? » o destro → Guida operativa.",
		L"Ábralo con « ? » o clic der. → Guía de operación.",
		L"캡션「?」또는 우클릭「조작 가이드」로도 열 수 있습니다.",
		L"也可通过标题栏「?」或右键「操作指南」打开。",
		L"افتح من «؟» أو يمين ← دليل التشغيل.",
		L"Откройте через «?» или ПКМ → Руководство.",
		L"Öffnen über „?“ oder Rechtsklick → Bedienungsanleitung.",
		L"Abra pelo «?» ou direito → Guia de operação.",
		L"Open via «?» of rechtsklik → Handleiding.",
		L"Otwórz przez «?» lub PPM → Przewodnik.",
		L"Başlık «?» veya sağ tık → İşlem kılavuzu ile açın."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

CAnalyzerDlg::CAnalyzerDlg(CWnd* pParent)	: CCustomBlurDialogExBase(IDD_ANALYZER, pParent)
{
	InitializeCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		m_ring[c].assign(RING_SAMPLES, 0.0f);
		m_ringSnap[c].assign(RING_SAMPLES, 0.0f);
		m_meterPeak[c] = 0.0f;
		m_meterHold[c] = 0.0f;
		m_meterRms[c] = 0.0f;
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
			m_specSnapDb[c][b] = -96.0f;
		}
		for (int r = 0; r < WF_ROWS; ++r) {
			for (int b = 0; b < SPEC_BINS; ++b)
				m_wfHist[c][r][b] = -96.0f;
		}
	}
	m_wfWrite = 0;
	m_wfFilled = 0;
	m_specSnapValid = false;
	m_tpLin = 0.0f;
	m_tpHold = 0.0f;
	m_lufsMom = -70.0f;
	m_lufsSum = 0.0;
	m_lufsCount = 0.0;
	m_eqDrag = false;
	m_eqDragBand = -1;
	m_eqDragPlot.SetRectEmpty();
	m_tpLufsTipRc.SetRectEmpty();
	memset(m_corrHist, 0, sizeof(m_corrHist));
	m_corrHistHead = 0;
	memset(m_specCorr, 0, sizeof(m_specCorr));
	memset(m_specPhase, 0, sizeof(m_specPhase));
	memset(m_fftLre, 0, sizeof(m_fftLre));
	memset(m_fftLim, 0, sizeof(m_fftLim));
	memset(m_gonio, 0, sizeof(m_gonio));
	m_waveLayoutPlotW = 0;
}

CAnalyzerDlg::~CAnalyzerDlg()
{
	StopSpecWorker();
	ReleaseBuffers();
	DeleteCriticalSection(&m_cs);
}


void CAnalyzerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_AN_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CAnalyzerDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_SHOWWINDOW()
	ON_WM_ERASEBKGND()
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(IDC_AN_HELP, &CAnalyzerDlg::OnBnClickedHelp)
	ON_COMMAND(ID_HELP_SHOWSHEET, &CAnalyzerDlg::OnBnClickedHelp)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_SETCURSOR()
	ON_WM_MOUSEWHEEL()
	ON_COMMAND(IDM_SPEC_OVERLAY, &CAnalyzerDlg::OnSpecLayoutOverlay)
	ON_COMMAND(IDM_SPEC_SPLIT_V, &CAnalyzerDlg::OnSpecLayoutSplitV)
	ON_COMMAND(IDM_SPEC_SPLIT_H, &CAnalyzerDlg::OnSpecLayoutSplitH)
	ON_COMMAND(IDM_SPEC_GRID4, &CAnalyzerDlg::OnSpecLayoutGrid4)
	ON_COMMAND(IDM_SPEC_GRID8, &CAnalyzerDlg::OnSpecLayoutGrid8)
	ON_COMMAND(IDM_STYLE_FILL, &CAnalyzerDlg::OnSpecStyleFill)
	ON_COMMAND(IDM_STYLE_LINE, &CAnalyzerDlg::OnSpecStyleLine)
	ON_COMMAND(IDM_STYLE_BARS, &CAnalyzerDlg::OnSpecStyleBars)
	ON_COMMAND(IDM_STYLE_CUBASE, &CAnalyzerDlg::OnSpecStyleCubase)
	ON_COMMAND(IDM_STYLE_SPAN, &CAnalyzerDlg::OnSpecStyleSpan)
	ON_COMMAND(IDM_STYLE_ABLETON, &CAnalyzerDlg::OnSpecStyleAbleton)
	ON_COMMAND(IDM_STYLE_FABFILTER, &CAnalyzerDlg::OnSpecStyleFabFilter)
	ON_COMMAND_RANGE(IDM_WAVE_SPEED_BASE, IDM_WAVE_SPEED_BASE + CAnalyzerDlg::WAVE_SPEED_COUNT - 1, &CAnalyzerDlg::OnWaveSpeedCmd)
	ON_COMMAND(IDM_PEAK_HOLD, &CAnalyzerDlg::OnTogglePeakHold)
	ON_COMMAND(IDM_EQ_OVERLAY, &CAnalyzerDlg::OnToggleEqOverlay)
	ON_COMMAND(IDM_FREEZE, &CAnalyzerDlg::OnToggleFreeze)
	ON_COMMAND(IDM_RESET_PEAK, &CAnalyzerDlg::OnResetPeakHold)
	ON_COMMAND(IDM_LEVEL_METER, &CAnalyzerDlg::OnToggleLevelMeter)
	ON_COMMAND(IDM_ALWAYS_ON_TOP, &CAnalyzerDlg::OnToggleAlwaysOnTop)
	ON_COMMAND(IDM_CLEAR_DISPLAY, &CAnalyzerDlg::OnClearDisplay)
	ON_COMMAND(IDM_COPY_HOVER, &CAnalyzerDlg::OnCopyHoverReadout)
	ON_COMMAND(IDM_COPY_PEAK, &CAnalyzerDlg::OnCopyPeakFreq)
	ON_COMMAND(IDM_COPY_LEVELS, &CAnalyzerDlg::OnCopyLevels)
	ON_COMMAND(IDM_WAVE_SCROLL, &CAnalyzerDlg::OnWaveModeScroll)
	ON_COMMAND(IDM_WAVE_TRIGGER, &CAnalyzerDlg::OnWaveModeTrigger)
	ON_COMMAND_RANGE(IDM_WAVE_ENVELOPE, IDM_WAVE_PHOSPHOR, &CAnalyzerDlg::OnWaveModeExtra)
	ON_COMMAND(IDM_LOWER_SPECTRUM, &CAnalyzerDlg::OnLowerModeSpectrum)
	ON_COMMAND(IDM_LOWER_WATERFALL, &CAnalyzerDlg::OnLowerModeWaterfall)
	ON_COMMAND(IDM_LOWER_PHASE, &CAnalyzerDlg::OnLowerModePhase)
	ON_COMMAND_RANGE(IDM_LOWER_VECTOR, IDM_LOWER_PHASEFREQ, &CAnalyzerDlg::OnLowerModeExtra)
	ON_COMMAND(IDM_SPEC_DIFF, &CAnalyzerDlg::OnToggleSpecDiff)
	ON_COMMAND(IDM_SPEC_SNAP, &CAnalyzerDlg::OnCaptureSpecSnap)
	ON_COMMAND(IDM_SPEC_SNAP_CLEAR, &CAnalyzerDlg::OnClearSpecSnap)
	ON_COMMAND(IDM_ZOOM_FULL, &CAnalyzerDlg::OnFreqZoomFull)
	ON_COMMAND(IDM_ZOOM_LOW, &CAnalyzerDlg::OnFreqZoomLow)
	ON_COMMAND(IDM_ZOOM_MID, &CAnalyzerDlg::OnFreqZoomMid)
	ON_COMMAND(IDM_ZOOM_HIGH, &CAnalyzerDlg::OnFreqZoomHigh)
	ON_COMMAND(IDM_MARKER_ADD, &CAnalyzerDlg::OnMarkerAdd)
	ON_COMMAND(IDM_MARKER_REMOVE, &CAnalyzerDlg::OnMarkerRemoveNearest)
	ON_COMMAND(IDM_MARKER_CLEAR, &CAnalyzerDlg::OnMarkerClearAll)
	ON_COMMAND(IDM_CORR_METER, &CAnalyzerDlg::OnToggleCorrMeter)
	ON_COMMAND(IDM_SAVE_JACKET, &CAnalyzerDlg::OnSaveSpectrogramJacket)
	ON_COMMAND(IDM_MS_NARROW, &CAnalyzerDlg::OnMsPresetNarrow)
	ON_COMMAND(IDM_MS_WIDE, &CAnalyzerDlg::OnMsPresetWide)
	ON_COMMAND(IDM_MS_MONO, &CAnalyzerDlg::OnMsPresetMono)
	ON_COMMAND(IDM_MS_RESET, &CAnalyzerDlg::OnMsPresetReset)
	ON_COMMAND(IDM_MS_FROM_CORR, &CAnalyzerDlg::OnMsPresetFromCorr)
	ON_MESSAGE(WM_ANALYZER_SPEC_DONE, &CAnalyzerDlg::OnSpecAnalysisDone)
	ON_MESSAGE(WM_ANALYZER_PRESENT, &CAnalyzerDlg::OnPresentRequest)
	ON_MESSAGE(WM_ANALYZER_SYNC, &CAnalyzerDlg::OnSyncRequest)
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
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

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

	m_specLayout = savedata.analyzerspeclayout;
	if (m_specLayout < SpecOverlay || m_specLayout > SpecGrid8)
		m_specLayout = SpecOverlay;
	m_specStyle = savedata.analyzerspecstyle;
	if (m_specStyle < StyleFill || m_specStyle > StyleFabFilter)
		m_specStyle = StyleFill;
	m_waveSpeedPct = savedata.analyzerwavespeed;
	if (m_waveSpeedPct < 25 || m_waveSpeedPct > 200)
		m_waveSpeedPct = 100;
	m_peakHold = (savedata.analyzerpeakhold != 0);
	m_eqOverlay = (savedata.analyzereqoverlay != 0);
	m_showLevelMeter = (savedata.analyzerlevelmeter != 0);
	m_alwaysOnTop = (savedata.analyzertopmost != 0);
	m_waveMode = savedata.analyzerwavemode;
	if (m_waveMode < WaveScroll || m_waveMode >= WAVE_MODE_COUNT)
		m_waveMode = WaveScroll;
	m_lowerMode = savedata.analyzerlowermode;
	if (m_lowerMode < LowerSpectrum || m_lowerMode >= LOWER_MODE_COUNT)
		m_lowerMode = LowerSpectrum;
	m_specDiff = (savedata.analyzerspecdiff != 0);
	m_freqZoom = savedata.analyzerfreqzoom;
	if (m_freqZoom < ZoomFull || m_freqZoom > ZoomHigh)
		m_freqZoom = ZoomFull;
	m_frozen = false;
	m_hoverPlot.SetRectEmpty();
	m_hoverPlotCount = 0;
	SyncSoft3DCamsFromSave();


	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	// TP/LUFS 読取部だけツールチップを付ける(簡易計測である旨)。
	// 子コントロールを持たない描画専用ダイアログなので矩形ツール。矩形は描画時に更新。
	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		CRect dummy(0, 0, 1, 1);
		m_tooltip.AddTool(this, AnalyzerTpLufsTipText(), &dummy, 1);
		if (m_help.GetSafeHwnd())
			m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		m_tooltip.SetDelayTime(TTDT_INITIAL, 400);
		m_tooltip.SetDelayTime(TTDT_RESHOW, 120);
		m_tooltip.SetDelayTime(TTDT_AUTOPOP, 12000);
		m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 460);
		m_tooltip.Activate(TRUE);
	}
	EnableToolTips(TRUE);

	// 座標は先に載せ、最前面なら Z 順だけ差し替え(ピアノロールと同様)
	if (m_alwaysOnTop && ::IsWindow(m_hWnd)) {
		SetWindowPos(&CWnd::wndTopMost, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	}

	m_feedEnabled = true;
	StartSpecWorker();
	// タイマーは座標保存のみ。描画は解析/音声完了の PostMessage で自由走行(ピアノロール方式)。
	SetTimer(1, 500, nullptr);
	EnableMainWindowLock(&savedata.analyzerMainLock, TRUE);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}
void CAnalyzerDlg::ResumePlaybackFeed() { m_feedEnabled = true; }
void CAnalyzerDlg::PauseFeed() { m_feedEnabled = false; }

void CAnalyzerDlg::ExportRemoteBars(BYTE outCh64[][64], int maxCh, int& outCh) const
{
	outCh = 0;
	if (!outCh64 || maxCh < 1) return;
	int n = m_channels;
	if (n < 1) n = 1;
	if (n > CH_MAX) n = CH_MAX;
	if (n > maxCh) n = maxCh;
	outCh = n;
	const float (*src)[SPEC_BINS] = m_specDb; // Soft3D/リモートはライブ（ピーク固定を避ける）
	for (int c = 0; c < n; ++c) {
		for (int o = 0; o < 64; ++o) {
			const int b0 = o * SPEC_BINS / 64;
			const int b1 = (o + 1) * SPEC_BINS / 64;
			float db = -96.0f;
			for (int b = b0; b < b1 && b < SPEC_BINS; ++b) {
				const float v = src[c][b];
				if (v > db) db = v;
			}
			int h = (int)((db + 96.0f) + 0.5f);
			if (h < 0) h = 0;
			if (h > 96) h = 96;
			outCh64[c][o] = (BYTE)h;
		}
	}
}

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
		m_meterPeak[c] = 0.0f;
		m_meterHold[c] = 0.0f;
		m_meterRms[c] = 0.0f;
	}
	m_waveDispPeak = 0.25f;
	m_tpLin = 0.0f;
	m_tpHold = 0.0f;
	m_lufsMom = -70.0f;
	m_lufsSum = 0.0;
	m_lufsCount = 0.0;
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			m_specDb[c][b] = -96.0f;
			m_specPeakDb[c][b] = -96.0f;
		}
		for (int r = 0; r < WF_ROWS; ++r) {
			for (int b = 0; b < SPEC_BINS; ++b)
				m_wfHist[c][r][b] = -96.0f;
		}
	}
	m_wfWrite = 0;
	m_wfFilled = 0;
	memset(m_specCorr, 0, sizeof(m_specCorr));
	memset(m_specPhase, 0, sizeof(m_specPhase));
	memset(m_gonio, 0, sizeof(m_gonio));
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
	KillTimer(2);
	StopSpecWorker();
	InterlockedExchange(&m_presentPosted, 0);
	InterlockedExchange(&m_syncPosted, 0);
	if (::IsWindow(m_hWnd)) {
		MSG msg;
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_SPEC_DONE, WM_ANALYZER_SPEC_DONE, PM_REMOVE)) {}
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_PRESENT, WM_ANALYZER_PRESENT, PM_REMOVE)) {}
		while (PeekMessage(&msg, m_hWnd, WM_ANALYZER_SYNC, WM_ANALYZER_SYNC, PM_REMOVE)) {}
	}
	ReleaseBuffers();
}

void CAnalyzerDlg::StartSpecWorker()
{
	if (m_hSpecThread) return;
	InterlockedExchange(&m_specStop, 0);
	InterlockedExchange(&m_specNeed, 0);
	m_hSpecWake = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!m_hSpecWake) return;
	m_hSpecThread = CreateThread(NULL, 0, SpecWorkerEntry, this, 0, NULL);
	if (!m_hSpecThread) {
		CloseHandle(m_hSpecWake);
		m_hSpecWake = nullptr;
	}
}

void CAnalyzerDlg::StopSpecWorker()
{
	InterlockedExchange(&m_specStop, 1);
	if (m_hSpecWake) SetEvent(m_hSpecWake);
	if (m_hSpecThread) {
		WaitForSingleObject(m_hSpecThread, 2000);
		CloseHandle(m_hSpecThread);
		m_hSpecThread = nullptr;
	}
	if (m_hSpecWake) {
		CloseHandle(m_hSpecWake);
		m_hSpecWake = nullptr;
	}
}

void CAnalyzerDlg::RequestSpecAnalysis()
{
	if (m_frozen) return;
	// RubberBand 伸縮中はスペクトル更新を間引き、ピアノロール/再生と CPU を奪い合わない
	if (tempo != 200 || pitch != 200) {
		static DWORD s_lastSpecKick = 0;
		const DWORD now = GetTickCount();
		if (s_lastSpecKick != 0 && (now - s_lastSpecKick) < 40u)
			return;
		s_lastSpecKick = now;
	}
	InterlockedExchange(&m_specNeed, 1);
	if (m_hSpecWake) SetEvent(m_hSpecWake);
}

DWORD WINAPI CAnalyzerDlg::SpecWorkerEntry(LPVOID param)
{
	return static_cast<CAnalyzerDlg*>(param)->SpecWorkerLoop();
}

DWORD CAnalyzerDlg::SpecWorkerLoop()
{
	while (InterlockedCompareExchange(&m_specStop, 0, 0) == 0) {
		if (m_hSpecWake)
			WaitForSingleObject(m_hSpecWake, 20);
		if (InterlockedCompareExchange(&m_specStop, 0, 0) != 0)
			break;
		if (InterlockedExchange(&m_specNeed, 0) == 0)
			continue;
		UpdateSpectrumFromRing();
		// SPEC_DONE を無制限 Post するとポンプを埋め、ピアノ/EQ を飢餓させる。
		// 提示は KickUiPresent の合流・ms2 スロットルに任せる。
		if (::IsWindow(m_hWnd))
			KickUiPresent();
	}
	return 0;
}

LRESULT CAnalyzerDlg::OnSpecAnalysisDone(WPARAM, LPARAM)
{
	// ピアノロール同様: 解析ができたフレームからすぐ描く(タイマー待ちにしない)
	KickUiPresent();
	return 0;
}

static void AnalyzerPresentInvalidate(HWND hWnd)
{
	if (!::IsWindow(hWnd))
		return;
	CRect cr;
	::GetClientRect(hWnd, &cr);
	const int capH = CCC_GetCustomCaptionHeight(hWnd);
	if (capH > 0 && cr.Height() > capH)
		cr.top = capH;
	if (!cr.IsRectEmpty())
		CCC_InvalidateRectMinusOverlay(hWnd, cr);
}

LRESULT CAnalyzerDlg::OnPresentRequest(WPARAM, LPARAM)
{
	// presentPosted は OnPaint 完了まで保持。
	// UpdateWindow 定常化はピアノと同様に MP スクロールを飢餓させるため使わない。
	if (::IsWindow(m_hWnd) && IsWindowVisible() && !IsIconic()) {
		AnalyzerPresentInvalidate(m_hWnd);
	}
	else {
		InterlockedExchange(&m_presentPosted, 0);
	}
	return 0;
}

void COggDlg_SyncAnalyzerFast();

void CAnalyzerDlg::RequestSyncFromMainUi()
{
	if (!::IsWindow(m_hWnd)) return;
	// 可視化頻度は savedata.ms2（EQ 表示でも間引かない）
	int minMs = savedata.ms2;
	if (minMs < 16) minMs = 16;
	if (minMs > 960) minMs = 960;
	// 伸縮中は同期も倍間隔（FeedPCM/描画が再生スレッドと競合しやすい）
	if (tempo != 200 || pitch != 200) {
		minMs *= 2;
		if (minMs < 32) minMs = 32;
	}
	const DWORD now = GetTickCount();
	if (m_lastSyncPostTick != 0 && (now - m_lastSyncPostTick) < (DWORD)minMs)
		return;
	if (InterlockedCompareExchange(&m_syncPosted, 1, 0) != 0) return;
	m_lastSyncPostTick = now;
	PostMessage(WM_ANALYZER_SYNC, 0, 0);
}

void CAnalyzerDlg::PumpSyncNow()
{
	if (!::IsWindow(m_hWnd) || !IsWindowVisible() || IsIconic()) return;
	int minMs = savedata.ms2;
	if (minMs < 16) minMs = 16;
	if (minMs > 960) minMs = 960;
	if (tempo != 200 || pitch != 200) {
		minMs *= 2;
		if (minMs < 32) minMs = 32;
	}
	const DWORD now = GetTickCount();
	if (m_lastSyncPostTick != 0 && (now - m_lastSyncPostTick) < (DWORD)minMs)
		return;
	if (InterlockedCompareExchange(&m_syncPosted, 0, 0) != 0) {
		MSG msg;
		while (::PeekMessage(&msg, m_hWnd, WM_ANALYZER_SYNC, WM_ANALYZER_SYNC, PM_REMOVE)) {}
		InterlockedExchange(&m_syncPosted, 0);
	}
	m_lastSyncPostTick = now;
	COggDlg_SyncAnalyzerFast();
}

LRESULT CAnalyzerDlg::OnSyncRequest(WPARAM, LPARAM)
{
	InterlockedExchange(&m_syncPosted, 0);
	if (!::IsWindow(m_hWnd) || !IsWindowVisible() || IsIconic())
		return 0;
	COggDlg_SyncAnalyzerFast();
	return 0;
}

void CAnalyzerDlg::SetSpecLayout(int layout)
{
	if (layout < SpecOverlay || layout > SpecGrid8) return;
	if (m_specLayout == layout) return;
	m_specLayout = layout;
	savedata.analyzerspeclayout = layout;
	m_specDirty = true;
	m_specReady = false;
	Invalidate(FALSE);
}

void CAnalyzerDlg::SetSpecStyle(int style)
{
	if (style < StyleFill || style > StyleFabFilter) return;
	if (m_specStyle == style) return;
	m_specStyle = style;
	savedata.analyzerspecstyle = style;
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::SetWaveSpeedPct(int pct)
{
	if (pct < 25) pct = 25;
	if (pct > 200) pct = 200;
	if (m_waveSpeedPct == pct) return;
	m_waveSpeedPct = pct;
	savedata.analyzerwavespeed = pct;
	EnterCriticalSection(&m_cs);
	if (m_sampleRate > 0) {
		const int targetW = (m_waveW > 40) ? m_waveW : 640;
		const int baseSpc = (std::max)(4, m_sampleRate * 6 / 5 / targetW);
		m_samplesPerCol = (std::max)(2, (baseSpc * 100 + m_waveSpeedPct / 2) / m_waveSpeedPct);
	}
	LeaveCriticalSection(&m_cs);
}

void CAnalyzerDlg::SetWaveMode(int mode)
{
	if (mode < WaveScroll || mode >= WAVE_MODE_COUNT) return;
	if (m_waveMode == mode) return;
	m_waveMode = mode;
	savedata.analyzerwavemode = mode;
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
	m_waveReady = false;   // 描き方が変わるので波形BBは作り直し
	m_waveLayoutPlotW = 0;
	m_specDirty = true;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false; // スクロール差分キャッシュを捨てる
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::SetLowerMode(int mode)
{
	if (mode < LowerSpectrum || mode >= LOWER_MODE_COUNT) return;
	if (m_lowerMode == mode) return;
	m_lowerMode = mode;
	savedata.analyzerlowermode = mode;
	m_hoverValid = false;
	m_hoverBin = -1;
	m_specDirty = true;
	m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::SetFreqZoom(int zoom)
{
	if (zoom < ZoomFull || zoom > ZoomHigh) return;
	if (m_freqZoom == zoom) return;
	m_freqZoom = zoom;
	savedata.analyzerfreqzoom = zoom;
	m_hoverValid = false;
	m_hoverBin = -1;
	m_specDirty = true;
	m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::PushWaterfallRow()
{
	// UpdateSpectrumFromRing 完了ごとに1行積む(解析スレッド)。描画側は CS で読む。
	EnterCriticalSection(&m_cs);
	const int row = m_wfWrite;
	for (int c = 0; c < CH_MAX; ++c)
		memcpy(m_wfHist[c][row], m_specDb[c], sizeof(float) * SPEC_BINS);
	m_wfWrite = (m_wfWrite + 1) % WF_ROWS;
	if (m_wfFilled < WF_ROWS) ++m_wfFilled;
	LeaveCriticalSection(&m_cs);
}

void CAnalyzerDlg::SyncEqUiFromSavedata()
{
	if (og && og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
		og->m_EqualizerDlg->SyncSlidersFromSavedata();
}

int CAnalyzerDlg::WaveSpeedIndex() const
{
	int nearest = 3;
	int best = 100000;
	for (int i = 0; i < WAVE_SPEED_COUNT; ++i) {
		const int d = abs(kWaveSpeedPct[i] - m_waveSpeedPct);
		if (d < best) { best = d; nearest = i; }
	}
	return nearest;
}

void CAnalyzerDlg::WaveSpeedSliderCb(void* ctx, int value)
{
	CAnalyzerDlg* p = (CAnalyzerDlg*)ctx;
	if (!p || !::IsWindow(p->GetSafeHwnd())) return;
	p->SetWaveSpeedPct(value);
}

void CAnalyzerDlg::ResetPeakHold()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b)
			m_specPeakDb[c][b] = m_specDb[c][b];
		m_meterHold[c] = m_meterRms[c];
	}
	m_tpHold = m_tpLin;
	LeaveCriticalSection(&m_cs);
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnSpecLayoutOverlay() { SetSpecLayout(SpecOverlay); }
void CAnalyzerDlg::OnSpecLayoutSplitV() { SetSpecLayout(SpecSplitV); }
void CAnalyzerDlg::OnSpecLayoutSplitH() { SetSpecLayout(SpecSplitH); }
void CAnalyzerDlg::OnSpecLayoutGrid4() { SetSpecLayout(SpecGrid4); }
void CAnalyzerDlg::OnSpecLayoutGrid8() { SetSpecLayout(SpecGrid8); }
void CAnalyzerDlg::OnSpecStyleFill() { SetSpecStyle(StyleFill); }
void CAnalyzerDlg::OnSpecStyleLine() { SetSpecStyle(StyleLine); }
void CAnalyzerDlg::OnSpecStyleBars() { SetSpecStyle(StyleBars); }
void CAnalyzerDlg::OnSpecStyleCubase() { SetSpecStyle(StyleCubase); }
void CAnalyzerDlg::OnSpecStyleSpan() { SetSpecStyle(StyleSpan); }
void CAnalyzerDlg::OnSpecStyleAbleton() { SetSpecStyle(StyleAbleton); }
void CAnalyzerDlg::OnSpecStyleFabFilter() { SetSpecStyle(StyleFabFilter); }

void CAnalyzerDlg::OnWaveModeScroll() { SetWaveMode(WaveScroll); }
void CAnalyzerDlg::OnWaveModeTrigger() { SetWaveMode(WaveTrigger); }
void CAnalyzerDlg::OnWaveModeExtra(UINT nID)
{
	if (nID == IDM_WAVE_ENVELOPE) SetWaveMode(WaveEnvelope);
	else if (nID == IDM_WAVE_MS) SetWaveMode(WaveMidSide);
	else if (nID == IDM_WAVE_LOUDNESS) SetWaveMode(WaveLoudness);
	else if (nID == IDM_WAVE_PHOSPHOR) SetWaveMode(WavePhosphor);
}
void CAnalyzerDlg::OnLowerModeSpectrum() { SetLowerMode(LowerSpectrum); }
void CAnalyzerDlg::OnLowerModeWaterfall() { SetLowerMode(LowerWaterfall); }
void CAnalyzerDlg::OnLowerModePhase() { SetLowerMode(LowerPhase); }
void CAnalyzerDlg::OnLowerModeExtra(UINT nID)
{
	if (nID == IDM_LOWER_VECTOR) SetLowerMode(LowerVector);
	else if (nID == IDM_LOWER_RTA) SetLowerMode(LowerRta);
	else if (nID == IDM_LOWER_CORRFREQ) SetLowerMode(LowerCorrFreq);
	else if (nID == IDM_LOWER_PHASEFREQ) SetLowerMode(LowerPhaseFreq);
}
void CAnalyzerDlg::OnFreqZoomFull() { SetFreqZoom(ZoomFull); }
void CAnalyzerDlg::OnFreqZoomLow() { SetFreqZoom(ZoomLow); }
void CAnalyzerDlg::OnFreqZoomMid() { SetFreqZoom(ZoomMid); }
void CAnalyzerDlg::OnFreqZoomHigh() { SetFreqZoom(ZoomHigh); }

void CAnalyzerDlg::OnToggleSpecDiff()
{
	m_specDiff = !m_specDiff;
	savedata.analyzerspecdiff = m_specDiff ? 1 : 0;
	m_specDirty = true;
	m_specReady = false;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnCaptureSpecSnap()
{
	EnterCriticalSection(&m_cs);
	memcpy(m_specSnapDb, m_specDb, sizeof(m_specSnapDb));
	LeaveCriticalSection(&m_cs);
	m_specSnapValid = true;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnClearSpecSnap()
{
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b)
			m_specSnapDb[c][b] = -96.0f;
	}
	LeaveCriticalSection(&m_cs);
	m_specSnapValid = false;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnMarkerAdd()
{
	if (!m_hoverValid) return;
	int hz = (int)(m_hoverHz + 0.5f);
	if (hz <= 0) return;
	if (hz > 96000) hz = 96000;
	int slot = -1;
	for (int i = 0; i < MARKER_MAX; ++i) {
		if (savedata.analyzermarkers[i] <= 0) { slot = i; break; }
	}
	if (slot < 0) {
		// 空きがなければ先頭を押し出す(FIFO)
		for (int i = 0; i < MARKER_MAX - 1; ++i)
			savedata.analyzermarkers[i] = savedata.analyzermarkers[i + 1];
		slot = MARKER_MAX - 1;
	}
	savedata.analyzermarkers[slot] = hz;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnMarkerRemoveNearest()
{
	int best = -1;
	float bestD = 0.0f;
	for (int i = 0; i < MARKER_MAX; ++i) {
		if (savedata.analyzermarkers[i] <= 0) continue;
		float d = 0.0f;
		if (m_hoverValid) {
			d = fabsf(logf((float)savedata.analyzermarkers[i]
				/ (std::max)(m_hoverHz, 1.0f)));
		}
		if (best < 0 || d < bestD) { bestD = d; best = i; }
	}
	if (best < 0) return;
	savedata.analyzermarkers[best] = 0;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnMarkerClearAll()
{
	bool any = false;
	for (int i = 0; i < MARKER_MAX; ++i) {
		if (savedata.analyzermarkers[i] != 0) any = true;
		savedata.analyzermarkers[i] = 0;
	}
	if (!any) return;
	m_specDirty = true;
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnToggleCorrMeter()
{
	savedata.pro_corr_meter = savedata.pro_corr_meter ? 0 : 1;
	m_waveReady = false;
	m_waveLayoutPlotW = 0;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false; // 相関帯の幅が変わるため残像を避ける
#endif
	extern void MpPersistSavedataQuick();
	MpPersistSavedataQuick();
	Invalidate(FALSE);
	KickUiPresent();
	// MPバナーは再生中毎フレーム再描画されるので、次フレームで φ/LR が追従する
}

static void AnalyzerApplyMsPreset(int widthPct, int mono)
{
	savedata.pro_ms_width = ProClampI(widthPct, 0, 200);
	savedata.pro_ms_mono = mono ? 1 : 0;
	extern CProToolsDlg* g_proToolsDlg;
	if (g_proToolsDlg && ::IsWindow(g_proToolsDlg->GetSafeHwnd()))
		g_proToolsDlg->LoadFromSavedata();
}

void CAnalyzerDlg::OnMsPresetNarrow() { AnalyzerApplyMsPreset(40, 0); }
void CAnalyzerDlg::OnMsPresetWide() { AnalyzerApplyMsPreset(160, 0); }
void CAnalyzerDlg::OnMsPresetMono() { AnalyzerApplyMsPreset(100, 1); }
void CAnalyzerDlg::OnMsPresetReset() { AnalyzerApplyMsPreset(100, 0); }

static void AnalyzerMsWidthSliderCb(void* /*ctx*/, int value)
{
	// 同値クリックで mono を落とさない（幅変更時だけ mono=0）
	if (ProClampI(value, 0, 200) == ProClampI(savedata.pro_ms_width, 0, 200))
		return;
	AnalyzerApplyMsPreset(value, 0);
}

void CAnalyzerDlg::OnMsPresetFromCorr()
{
	const int sn = m_corrHistHead < 64 ? m_corrHistHead : 64;
	if (sn <= 0) {
		AnalyzerApplyMsPreset(100, 0);
		return;
	}
	double sum = 0.0;
	for (int i = 0; i < sn; ++i) {
		const int idx = (m_corrHistHead - sn + i) & 63;
		sum += (double)m_corrHist[idx];
	}
	const float avg = (float)(sum / (double)sn);
	// High φ → correlated / mono-ish; low/negative → wide side; mid → narrow.
	if (avg >= 0.75f)
		AnalyzerApplyMsPreset(100, 1);
	else if (avg >= 0.35f)
		AnalyzerApplyMsPreset(40, 0);
	else if (avg >= -0.15f)
		AnalyzerApplyMsPreset(100, 0);
	else
		AnalyzerApplyMsPreset(160, 0);
}

void CAnalyzerDlg::OnSaveSpectrogramJacket()
{
	CFileDialog fd(FALSE, _T("png"), _T("spectrogram.png"),
		OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_ENABLESIZING,
		_T("PNG (*.png)|*.png|Bitmap (*.bmp)|*.bmp||"), this);
	if (fd.DoModal() != IDOK) return;

	const CString path = fd.GetPathName();
	const bool asBmp = (path.Right(4).CompareNoCase(_T(".bmp")) == 0);
	CImage img;
	HBITMAP hbCopy = NULL;

	if (m_specReady && m_specBmp.GetSafeHandle() && m_specW > 0 && m_specH > 0) {
		HDC screen = ::GetDC(NULL);
		if (screen) {
			HDC mem = ::CreateCompatibleDC(screen);
			hbCopy = ::CreateCompatibleBitmap(screen, m_specW, m_specH);
			if (hbCopy && mem) {
				HGDIOBJ old = ::SelectObject(mem, hbCopy);
				::BitBlt(mem, 0, 0, m_specW, m_specH, m_specDC.GetSafeHdc(), 0, 0, SRCCOPY);
				::SelectObject(mem, old);
			}
			if (mem) ::DeleteDC(mem);
			::ReleaseDC(NULL, screen);
		}
		if (hbCopy) {
			img.Attach(hbCopy);
			hbCopy = NULL;
		}
	}

	if (img.IsNull()) {
		int wfWrite = 0, wfFilled = 0;
		EnterCriticalSection(&m_cs);
		wfWrite = m_wfWrite;
		wfFilled = m_wfFilled;
		const int ch = (std::max)(1, (std::min)(m_channels, CH_MAX));
		LeaveCriticalSection(&m_cs);
		const int imgW = SPEC_BINS;
		const int imgH = WF_ROWS;
		if (SUCCEEDED(img.Create(imgW, imgH, 32, CImage::createAlphaChannel))) {
			for (int y = 0; y < imgH; ++y) {
				for (int x = 0; x < imgW; ++x) {
					float db = -96.0f;
					EnterCriticalSection(&m_cs);
					const int src = ((wfWrite - 1 - y) % WF_ROWS + WF_ROWS) % WF_ROWS;
					if (y < wfFilled)
						db = m_wfHist[0][src][x];
					LeaveCriticalSection(&m_cs);
					float t = (db + 96.0f) / 96.0f;
					if (t < 0.f) t = 0.f;
					if (t > 1.f) t = 1.f;
					int r, g, b;
					if (t < 0.33f) {
						float u = t / 0.33f;
						r = (int)(20 + u * 20); g = (int)(30 + u * 120); b = (int)(60 + u * 160);
					}
					else if (t < 0.66f) {
						float u = (t - 0.33f) / 0.33f;
						r = (int)(40 + u * 180); g = (int)(150 + u * 80); b = (int)(220 - u * 180);
					}
					else {
						float u = (t - 0.66f) / 0.34f;
						r = (int)(220 + u * 35); g = (int)(230 + u * 25); b = (int)(40 + u * 200);
					}
					img.SetPixel(x, y, RGB(r, g, b));
				}
			}
			(void)ch;
		}
	}

	if (img.IsNull()) {
		MessageBox(LL14(L"保存できるスペクトル画像がありません。", L"No spectrogram image to save.", L"Pas d'image spectre.", L"Nessuna immagine spettro.", L"No hay imagen de espectro.", L"저장할 스펙트럼 없음.", L"没有可保存的频谱图。", L"لا صورة طيف.", L"Нет изображения спектра.", L"Kein Spektrogramm.", L"Sem imagem.", L"Geen beeld.", L"Brak obrazu.", L"Kaydedilecek spektrum yok."), _T("Spectrogram"), MB_OK | MB_ICONINFORMATION);
		return;
	}
	HRESULT hr = E_FAIL;
	try {
		if (asBmp)
			hr = img.Save(path, Gdiplus::ImageFormatBMP);
		else
			hr = img.Save(path, Gdiplus::ImageFormatPNG);
	}
	catch (...) {}
	img.Destroy();
	if (FAILED(hr)) {
		MessageBox(LL14(L"画像の保存に失敗しました。", L"Failed to save image.", L"Echec enregistrement.", L"Salvataggio fallito.", L"Error al guardar.", L"저장 실패.", L"保存失败。", L"فشل الحفظ.", L"Не удалось сохранить.", L"Speichern fehlgeschlagen.", L"Falha ao salvar.", L"Opslaan mislukt.", L"Zapis nieudany.", L"Kayit basarisiz."), _T("Spectrogram"), MB_OK | MB_ICONWARNING);
		return;
	}
	CString msg;
	msg.Format(LL14(L"保存しました:\n%s", L"Saved:\n%s", L"Enregistre:\n%s", L"Salvato:\n%s", L"Guardado:\n%s", L"저장:\n%s", L"已保存:\n%s", L"تم الحفظ:\n%s", L"Сохранено:\n%s", L"Gespeichert:\n%s", L"Salvo:\n%s", L"Opgeslagen:\n%s", L"Zapisano:\n%s", L"Kaydedildi:\n%s"), (LPCTSTR)path);
	MessageBox(msg, _T("Spectrogram"), MB_OK | MB_ICONINFORMATION);
}

void CAnalyzerDlg::OnWaveSpeedCmd(UINT nID)
{
	const int idx = (int)nID - IDM_WAVE_SPEED_BASE;
	if (idx < 0 || idx >= WAVE_SPEED_COUNT) return;
	SetWaveSpeedPct(kWaveSpeedPct[idx]);
}

void CAnalyzerDlg::OnTogglePeakHold()
{
	m_peakHold = !m_peakHold;
	savedata.analyzerpeakhold = m_peakHold ? 1 : 0;
	if (!m_peakHold)
		ResetPeakHold();
	else {
		m_specDirty = true;
		Invalidate(FALSE);
	}
}

void CAnalyzerDlg::OnToggleEqOverlay()
{
	m_eqOverlay = !m_eqOverlay;
	savedata.analyzereqoverlay = m_eqOverlay ? 1 : 0;
	m_specDirty = true;
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnToggleFreeze()
{
	m_frozen = !m_frozen;
	Invalidate(FALSE);
}

void CAnalyzerDlg::OnResetPeakHold()
{
	ResetPeakHold();
}

void CAnalyzerDlg::OnToggleLevelMeter()
{
	m_showLevelMeter = !m_showLevelMeter;
	savedata.analyzerlevelmeter = m_showLevelMeter ? 1 : 0;
	m_waveReady = false;
	m_waveLayoutPlotW = 0;
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false; // 右端ストリップ残像を避ける
#endif
	Invalidate(FALSE);
	KickUiPresent();
}

void CAnalyzerDlg::OnToggleAlwaysOnTop()
{
	m_alwaysOnTop = !m_alwaysOnTop;
	savedata.analyzertopmost = m_alwaysOnTop ? 1 : 0;
	if (::IsWindow(m_hWnd)) {
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
}

void CAnalyzerDlg::OnClearDisplay()
{
	// 波形/周波数/スペクトログラム履歴と TP・LUFS のホールドを消す。
	// 差分用スナップショットは「スナップショットを消去」でのみ消す。
	ResetPlaybackState();
#if CCUSTOM_AERO_SUPPORT
	m_chromaReady = false;
#endif
}

void CAnalyzerDlg::OnCopyHoverReadout()
{
	if (!m_hoverValid)
		return;
	const int channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	CString s;
	if (m_hoverHz >= 1000.0f)
		s.Format(_T("%s\t%.3f kHz\t%.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
	else
		s.Format(_T("%s\t%.1f Hz\t%.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
	CopyUnicodeToClipboard(m_hWnd, s);
}

void CAnalyzerDlg::OnCopyPeakFreq()
{
	float bestDb = -96.0f;
	int bestCh = 0;
	int bestBin = 0;
	int channels = 2;
	int sr = 44100;
	EnterCriticalSection(&m_cs);
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	sr = (m_sampleRate > 0) ? m_sampleRate : 44100;
	const float (*bins)[SPEC_BINS] = m_peakHold ? m_specPeakDb : m_specDb;
	for (int c = 0; c < channels; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			if (bins[c][b] > bestDb) {
				bestDb = bins[c][b];
				bestCh = c;
				bestBin = b;
			}
		}
	}
	LeaveCriticalSection(&m_cs);

	const float nyquist = (float)sr * 0.5f;
	const float hz = SpecCenterHz(bestBin, SPEC_BINS, nyquist);
	CString s;
	if (hz >= 1000.0f)
		s.Format(_T("%s\t%.3f kHz\t%.1f dB\tSR %d"), ChannelLabel(bestCh, channels), hz / 1000.0f, bestDb, sr);
	else
		s.Format(_T("%s\t%.1f Hz\t%.1f dB\tSR %d"), ChannelLabel(bestCh, channels), hz, bestDb, sr);
	CopyUnicodeToClipboard(m_hWnd, s);
}

void CAnalyzerDlg::OnCopyLevels()
{
	float hold[CH_MAX] = {};
	float rms[CH_MAX] = {};
	float tp = 0.0f, lufs = -70.0f;
	int channels = 2;
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		hold[c] = m_meterHold[c];
		rms[c] = m_meterRms[c];
	}
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	tp = (std::max)(m_tpHold, m_tpLin);
	lufs = m_lufsMom;
	LeaveCriticalSection(&m_cs);

	CString s;
	for (int c = 0; c < channels; ++c) {
		CString one;
		one.Format(_T("%s\t%.1f dB\thold %.1f dB"),
			ChannelLabel(c, channels), AmpToDb(rms[c]), AmpToDb(hold[c]));
		if (c > 0) s += _T("\t");
		s += one;
	}
	float tpDb = -99.9f;
	if (tp > 1e-6f) tpDb = 20.0f * log10f(tp);
	CString extra;
	extra.Format(_T("\tTP %.1f dBTP\tLUFS %.1f"), tpDb, lufs);
	s += extra;
	CopyUnicodeToClipboard(m_hWnd, s);
}

void CAnalyzerDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CCustomPopupMenu menu;

	menu.AddSeparator();

	CCustomPopupMenu* subViewTop = menu.AddSubMenu(
		LL14(L"上パネル表示", L"Upper panel view", L"Vue panneau superieur", L"Vista pannello superiore", L"Vista panel superior", L"상단 패널 표시", L"上方面板显示", L"عرض اللوح العلوي", L"Вид верхней панели", L"Obere Panelansicht", L"Vista do painel superior", L"Bovenpaneelweergave", L"Widok gornego panelu", L"Ust panel gorunumu"),
		LL14(L"上部ペインの表示モード（通常2D／簡易3D）を選びます。", L"Choose upper pane view mode (normal 2D / soft 3D).", L"Choisir le mode du panneau superieur (2D / 3D simplifie).", L"Scegli modalita riquadro superiore (2D / 3D semplificato).", L"Elegir modo del panel superior (2D / 3D simple).", L"상단 패널 표시 모드(일반 2D/간이 3D)를 고릅니다.", L"选择上部窗格显示模式（普通2D/简易3D）。", L"اختر وضع اللوح العلوي (2D / 3D مبسط).", L"Режим верхней панели (2D / простой 3D).", L"Obere Anzeige (2D / einfaches 3D).", L"Modo do painel superior (2D / 3D simples).", L"Bovenpaneelmodus (2D / eenvoudig 3D).", L"Tryb gornego panelu (2D / uproszczone 3D).", L"Ust panel modu (2D / basit 3B)."));
	if (subViewTop) {
		subViewTop->AddCheck(IDM_VIEW_TOP_2D,
			LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"),
			!IsSoft3DTop());
		subViewTop->AddCheck(IDM_VIEW_TOP_3D,
			LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"ثلاثي الأبعاد مبسط", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"),
			IsSoft3DTop());
		if (IsSoft3DTop()) {
			int yaw10 = (int)(m_camTop.yawDeg * 10.f);
			int pit10 = (int)(m_camTop.pitchDeg * 10.f);
			int zoomPct = (int)(m_camTop.zoom * 100.f + 0.5f);
			if (yaw10 < -1800) yaw10 = -1800; if (yaw10 > 1800) yaw10 = 1800;
			if (pit10 < -850) pit10 = -850; if (pit10 > 850) pit10 = 850;
			if (zoomPct < 35) zoomPct = 35; if (zoomPct > 400) zoomPct = 400;
			subViewTop->AddSeparator();
			subViewTop->AddSlider(LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
				-1800, 1800, yaw10, &CAnalyzerDlg::Soft3dYawTopCb, this,
				LL14(L"水平回転（ドラッグ中に反映）", L"Horizontal rotation (live)", L"Rotation horizontale (direct)", L"Rotazione orizzontale (live)", L"Rotacion horizontal (en vivo)", L"수평 회전(즉시)", L"水平旋转（即时）", L"دوران أفقي (مباشر)", L"Горизонтальный поворот (сразу)", L"Horizontale Drehung (live)", L"Rotacao horizontal (ao vivo)", L"Horizontale rotatie (live)", L"Obrot poziomy (na zywo)", L"Yatay donus (anlik)"));
			subViewTop->AddSlider(LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
				-850, 850, pit10, &CAnalyzerDlg::Soft3dPitchTopCb, this,
				LL14(L"仰角（ドラッグ中に反映）", L"Elevation angle (live)", L"Angle d'elevation (direct)", L"Angolo di elevazione (live)", L"Angulo de elevacion (en vivo)", L"앙각(즉시)", L"仰角（即时）", L"زاوية الارتفاع (مباشر)", L"Угол наклона (сразу)", L"Neigungswinkel (live)", L"Angulo de elevacao (ao vivo)", L"Elevatiehoek (live)", L"Kat nachylenia (na zywo)", L"Yukselis acisi (anlik)"));
			subViewTop->AddSlider(LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
				35, 400, zoomPct, &CAnalyzerDlg::Soft3dZoomTopCb, this,
				LL14(L"拡大縮小（ドラッグ中に反映）", L"Zoom (live)", L"Zoom (direct)", L"Zoom (live)", L"Zoom (en vivo)", L"확대/축소(즉시)", L"缩放（即时）", L"تكبير (مباشر)", L"Масштаб (сразу)", L"Zoom (live)", L"Zoom (ao vivo)", L"Zoom (live)", L"Powiększenie (na zywo)", L"Yakinlastirma (anlik)"));
			subViewTop->AddSeparator();
			subViewTop->AddCommand(IDM_VIEW_TOP_RESET,
				LL14(L"視点をリセット", L"Reset view", L"Reinitialiser la vue", L"Reimposta vista", L"Restablecer vista",
					L"시점 재설정", L"重置视角", L"إعادة ضبط العرض", L"Сбросить вид", L"Ansicht zurucksetzen",
					L"Redefinir vista", L"Weergave resetten", L"Resetuj widok", L"Gorunumu sifirla"),
				LL14(L"カメラを既定の視点（Yaw/Pitch/Zoom）に戻します。", L"Restore the default camera (yaw / pitch / zoom).", L"Retablir la camera par defaut (lacet / tangage / zoom).", L"Ripristina la camera predefinita (yaw / pitch / zoom).", L"Restaurar la camara predeterminada (yaw / pitch / zoom).",
					L"기본 카메라(요/피치/줌)로 되돌립니다.", L"将相机恢复为默认视角（偏航/俯仰/缩放）。", L"استعادة الكاميرا الافتراضية (الانحراف / الميل / التكبير).", L"Вернуть камеру к значениям по умолчанию (yaw / pitch / zoom).", L"Kamera auf Standardansicht (Yaw / Pitch / Zoom) zurucksetzen.",
					L"Restaurar a camera padrao (yaw / pitch / zoom).", L"Herstel de standaardcamera (yaw / pitch / zoom).", L"Przywroc domyslna kamere (yaw / pitch / zoom).", L"Kamerayi varsayilan gorunume (yaw / pitch / zoom) dondur."));
		}
	}
	CCustomPopupMenu* subViewBot = menu.AddSubMenu(
		LL14(L"下パネル表示", L"Lower panel view", L"Vue panneau inferieur", L"Vista pannello inferiore", L"Vista panel inferior", L"하단 패널 표시", L"下方面板显示", L"عرض اللوح السفلي", L"Вид нижней панели", L"Untere Panelansicht", L"Vista do painel inferior", L"Onderpaneelweergave", L"Widok dolnego panelu", L"Alt panel gorunumu"),
		LL14(L"下部ペインの表示モード（通常2D／簡易3D）を選びます。", L"Choose lower pane view mode (normal 2D / soft 3D).", L"Choisir le mode du panneau inferieur (2D / 3D simplifie).", L"Scegli modalita riquadro inferiore (2D / 3D semplificato).", L"Elegir modo del panel inferior (2D / 3D simple).", L"하단 패널 표시 모드(일반 2D/간이 3D)를 고릅니다.", L"选择下部窗格显示模式（普通2D/简易3D）。", L"اختر وضع اللوح السفلي (2D / 3D مبسط).", L"Режим нижней панели (2D / простой 3D).", L"Untere Anzeige (2D / einfaches 3D).", L"Modo do painel inferior (2D / 3D simples).", L"Onderpaneelmodus (2D / eenvoudig 3D).", L"Tryb dolnego panelu (2D / uproszczone 3D).", L"Alt panel modu (2D / basit 3B)."));
	if (subViewBot) {
		subViewBot->AddCheck(IDM_VIEW_BOT_2D,
			LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"),
			!IsSoft3DBot());
		subViewBot->AddCheck(IDM_VIEW_BOT_3D,
			LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"ثلاثي الأبعاد مبسط", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"),
			IsSoft3DBot());
		if (IsSoft3DBot()) {
			int yaw10 = (int)(m_camBot.yawDeg * 10.f);
			int pit10 = (int)(m_camBot.pitchDeg * 10.f);
			int zoomPct = (int)(m_camBot.zoom * 100.f + 0.5f);
			if (yaw10 < -1800) yaw10 = -1800; if (yaw10 > 1800) yaw10 = 1800;
			if (pit10 < -850) pit10 = -850; if (pit10 > 850) pit10 = 850;
			if (zoomPct < 35) zoomPct = 35; if (zoomPct > 400) zoomPct = 400;
			subViewBot->AddSeparator();
			subViewBot->AddSlider(LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
				-1800, 1800, yaw10, &CAnalyzerDlg::Soft3dYawBotCb, this,
				LL14(L"水平回転（ドラッグ中に反映）", L"Horizontal rotation (live)", L"Rotation horizontale (direct)", L"Rotazione orizzontale (live)", L"Rotacion horizontal (en vivo)", L"수평 회전(즉시)", L"水平旋转（即时）", L"دوران أفقي (مباشر)", L"Горизонтальный поворот (сразу)", L"Horizontale Drehung (live)", L"Rotacao horizontal (ao vivo)", L"Horizontale rotatie (live)", L"Obrot poziomy (na zywo)", L"Yatay donus (anlik)"));
			subViewBot->AddSlider(LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
				-850, 850, pit10, &CAnalyzerDlg::Soft3dPitchBotCb, this,
				LL14(L"仰角（ドラッグ中に反映）", L"Elevation angle (live)", L"Angle d'elevation (direct)", L"Angolo di elevazione (live)", L"Angulo de elevacion (en vivo)", L"앙각(즉시)", L"仰角（即时）", L"زاوية الارتفاع (مباشر)", L"Угол наклона (сразу)", L"Neigungswinkel (live)", L"Angulo de elevacao (ao vivo)", L"Elevatiehoek (live)", L"Kat nachylenia (na zywo)", L"Yukselis acisi (anlik)"));
			subViewBot->AddSlider(LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
				35, 400, zoomPct, &CAnalyzerDlg::Soft3dZoomBotCb, this,
				LL14(L"拡大縮小（ドラッグ中に反映）", L"Zoom (live)", L"Zoom (direct)", L"Zoom (live)", L"Zoom (en vivo)", L"확대/축소(즉시)", L"缩放（即时）", L"تكبير (مباشر)", L"Масштаб (сразу)", L"Zoom (live)", L"Zoom (ao vivo)", L"Zoom (live)", L"Powiększenie (na zywo)", L"Yakinlastirma (anlik)"));
			subViewBot->AddSeparator();
			subViewBot->AddCommand(IDM_VIEW_BOT_RESET,
				LL14(L"視点をリセット", L"Reset view", L"Reinitialiser la vue", L"Reimposta vista", L"Restablecer vista",
					L"시점 재설정", L"重置视角", L"إعادة ضبط العرض", L"Сбросить вид", L"Ansicht zurucksetzen",
					L"Redefinir vista", L"Weergave resetten", L"Resetuj widok", L"Gorunumu sifirla"),
				LL14(L"カメラを既定の視点（Yaw/Pitch/Zoom）に戻します。", L"Restore the default camera (yaw / pitch / zoom).", L"Retablir la camera par defaut (lacet / tangage / zoom).", L"Ripristina la camera predefinita (yaw / pitch / zoom).", L"Restaurar la camara predeterminada (yaw / pitch / zoom).",
					L"기본 카메라(요/피치/줌)로 되돌립니다.", L"将相机恢复为默认视角（偏航/俯仰/缩放）。", L"استعادة الكاميرا الافتراضية (الانحراف / الميل / التكبير).", L"Вернуть камеру к значениям по умолчанию (yaw / pitch / zoom).", L"Kamera auf Standardansicht (Yaw / Pitch / Zoom) zurucksetzen.",
					L"Restaurar a camera padrao (yaw / pitch / zoom).", L"Herstel de standaardcamera (yaw / pitch / zoom).", L"Przywroc domyslna kamere (yaw / pitch / zoom).", L"Kamerayi varsayilan gorunume (yaw / pitch / zoom) dondur."));
		}
	}

	CCustomPopupMenu* subWave = menu.AddSubMenu(
		LL14(L"上部の表示", L"Upper display", L"Affichage superieur", L"Display superiore", L"Pantalla superior", L"상단 표시", L"上部显示", L"العرض العلوي", L"Верхняя область", L"Obere Anzeige", L"Exibicao superior", L"Bovenste weergave", L"Gorny widok", L"Ust gosterim"),
		LL14(L"上部ペインの表示内容を選びます。", L"Choose what the upper pane shows.", L"Choisir l'affichage du panneau superieur.", L"Scegli cosa mostra il riquadro superiore.", L"Elegir que muestra el panel superior.", L"상단 패널 표시 내용을 고릅니다.", L"选择上部窗格显示内容。", L"اختر ما يعرضه اللوح العلوي.", L"Выбрать содержимое верхней панели.", L"Obere Anzeige wahlen.", L"Escolher o que o painel superior mostra.", L"Kies wat het bovenpaneel toont.", L"Wybierz zawartosc gornego panelu.", L"Ust panelde ne gosterilecegini sec."));
	if (subWave) {
		CCustomPopupMenu* subWaveTime = subWave->AddSubMenu(
			LL14(L"時間軸", L"Time domain", L"Domaine temporel", L"Dominio tempo", L"Dominio temporal", L"시간축", L"时域", L"مجال زمني", L"Временная область", L"Zeitbereich", L"Dominio do tempo", L"Tijdsdomein", L"Domena czasu", L"Zaman alani"),
			LL14(L"スクロール波形・エンベロープ・M/S・ラウドネス。", L"Scrolling wave, envelope, M/S, loudness.", L"Onde, enveloppe, M/S, loudness.", L"Onda, inviluppo, M/S, loudness.", L"Onda, envolvente, M/S, loudness.", L"스크롤 파형·엔벨로프·M/S·라우드니스.", L"滚动波形、包络、M/S、响度。", L"موجة وغلاف وM/S وloudness.", L"Волна, огибающая, M/S, громкость.", L"Scrollwelle, Hüllkurve, M/S, Loudness.", L"Onda, envelope, M/S, loudness.", L"Golf, omhullende, M/S, loudness.", L"Fala, obwiednia, M/S, glosnosc.", L"Kayan dalga, zarf, M/S, loudness."));
		if (subWaveTime) {
			subWaveTime->AddCheck(IDM_WAVE_SCROLL,
				LL14(L"スクロール波形", L"Scrolling wave", L"Onde defilante", L"Onda scorrevole", L"Onda desplazable", L"스크롤 파형", L"滚动波形", L"موجة متمررة", L"Прокрутка волны", L"Scrollende Welle", L"Onda rolante", L"Scrollende golf", L"Przewijana fala", L"Kayan dalga"),
				m_waveMode == WaveScroll);
			subWaveTime->AddCheck(IDM_WAVE_ENVELOPE,
				LL14(L"エンベロープ (min/max)", L"Envelope (min/max)", L"Enveloppe (min/max)", L"Inviluppo (min/max)", L"Envolvente (min/max)", L"엔벨로프 (min/max)", L"包络 (min/max)", L"غلاف (min/max)", L"Огибающая (min/max)", L"Hüllkurve (min/max)", L"Envelope (min/max)", L"Omhullende (min/max)", L"Obwiednia (min/max)", L"Zarf (min/max)"),
				m_waveMode == WaveEnvelope,
				LL14(L"各列の最小〜最大を塗り潰した DAW 風波形です。", L"DAW-style filled min-to-max per column.", L"Onde DAW remplie min-max par colonne.", L"Onda DAW riempita min-max per colonna.", L"Onda DAW rellena min-max por columna.", L"열마다 최소~최대를 채운 DAW 풍 파형.", L"按列填充最小到最大的 DAW 风格波形。", L"موجة DAW تملأ الأدنى-الأقصى لكل عمود.", L"DAW-волна с заливкой min-max по столбцам.", L"DAW-Welle mit Min-Max-Füllung je Spalte.", L"Onda DAW preenchida min-max por coluna.", L"DAW-golf gevuld min-max per kolom.", L"Fala DAW wypelniona min-max w kolumnie.", L"Sutun basina min-max dolgulu DAW dalgasi."));
			subWaveTime->AddCheck(IDM_WAVE_MS,
				LL14(L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side", L"Mid / Side"),
				m_waveMode == WaveMidSide,
				LL14(L"Mid=(L+R)/2 と Side=(L-R)/2 を2レーンで表示します。", L"Show Mid=(L+R)/2 and Side=(L-R)/2 on two lanes.", L"Afficher Mid=(L+R)/2 et Side=(L-R)/2.", L"Mostra Mid=(L+R)/2 e Side=(L-R)/2.", L"Mostrar Mid=(L+R)/2 y Side=(L-R)/2.", L"Mid=(L+R)/2와 Side=(L-R)/2를 두 레인으로.", L"以两行显示 Mid=(L+R)/2 与 Side=(L-R)/2。", L"عرض Mid=(L+R)/2 و Side=(L-R)/2.", L"Показать Mid=(L+R)/2 и Side=(L-R)/2.", L"Mid=(L+R)/2 und Side=(L-R)/2 in zwei Bahnen.", L"Mostrar Mid=(L+R)/2 e Side=(L-R)/2.", L"Toon Mid=(L+R)/2 en Side=(L-R)/2.", L"Pokaz Mid=(L+R)/2 i Side=(L-R)/2.", L"Mid=(L+R)/2 ve Side=(L-R)/2 iki seritte."));
			subWaveTime->AddCheck(IDM_WAVE_LOUDNESS,
				LL14(L"ラウドネス履歴 (RMS)", L"Loudness history (RMS)", L"Historique loudness (RMS)", L"Cronologia loudness (RMS)", L"Historial de loudness (RMS)", L"라우드니스 이력 (RMS)", L"响度历史 (RMS)", L"سجل الجهارة (RMS)", L"История громкости (RMS)", L"Loudness-Verlauf (RMS)", L"Historico de loudness (RMS)", L"Loudness-geschiedenis (RMS)", L"Historia glosnosci (RMS)", L"Loudness gecmisi (RMS)"),
				m_waveMode == WaveLoudness,
				LL14(L"列ごとの RMS と簡易 LUFS 目安線です。", L"Per-column RMS with a simple LUFS guide line.", L"RMS par colonne avec un guide LUFS simple.", L"RMS per colonna con linea guida LUFS.", L"RMS por columna con linea guia LUFS.", L"열별 RMS와 간이 LUFS 기준선.", L"每列 RMS，带简易 LUFS 参考线。", L"RMS لكل عمود مع خط إرشاد LUFS.", L"RMS по столбцам и простая линия LUFS.", L"RMS je Spalte mit einfacher LUFS-Linie.", L"RMS por coluna com linha guia LUFS.", L"RMS per kolom met eenvoudige LUFS-lijn.", L"RMS w kolumnie z linia LUFS.", L"Sutun RMS ve basit LUFS cizgisi."));
		}
		CCustomPopupMenu* subWaveScope = subWave->AddSubMenu(
			LL14(L"オシロ", L"Scope", L"Oscillo", L"Oscilloscopio", L"Osciloscopio", L"스코프", L"示波", L"راسم", L"Осциллограф", L"Scope", L"Osciloscopio", L"Scoop", L"Oscyloskop", L"Osiloskop"),
			LL14(L"トリガー式と残光オシロ。", L"Triggered and phosphor scopes.", L"Oscillo declenche et a persistance.", L"Oscilloscopio con trigger e fosforo.", L"Osciloscopio con disparo y fosforo.", L"트리거·잔광 스코프.", L"触发式与余辉示波。", L"راسم محفز وفسفور.", L"Триггерный и фосфорный скоп.", L"Getriggertes und Phosphor-Scope.", L"Osciloscopio disparado e fosforo.", L"Getriggerde en fosfor-scoop.", L"Wyzwalany i fosforowy oscyloskop.", L"Tetiklemeli ve fosfor osiloskop."));
		if (subWaveScope) {
			subWaveScope->AddCheck(IDM_WAVE_TRIGGER,
				LL14(L"トリガー式オシロ", L"Triggered scope", L"Oscillo declenche", L"Oscilloscopio con trigger", L"Osciloscopio con disparo", L"트리거 오실로스코프", L"触发示波器", L"راسم ذبذبات محفز", L"Синхр. осциллограф", L"Getriggertes Oszilloskop", L"Osciloscopio disparado", L"Getriggerde scoop", L"Wyzwalany oscyloskop", L"Tetiklemeli osiloskop"),
				m_waveMode == WaveTrigger);
			subWaveScope->AddCheck(IDM_WAVE_PHOSPHOR,
				LL14(L"残光オシロ", L"Phosphor scope", L"Oscillo a persistance", L"Oscilloscopio al fosforo", L"Osciloscopio de fosforo", L"잔광 스코프", L"余辉示波器", L"راسم فسفور", L"Фосфорный скоп", L"Phosphor-Oszilloskop", L"Osciloscopio de fosforo", L"Fosfor-scoop", L"Oscyloskop fosforowy", L"Fosfor osiloskop"),
				m_waveMode == WavePhosphor,
				LL14(L"ゼロクロス整列のトレースを残光で重ねます。", L"Zero-cross aligned traces stacked with phosphor decay.", L"Traces alignees au zero avec persistance.", L"Tracce allineate allo zero con decadimento.", L"Trazas alineadas al cero con persistencia.", L"제로크로스 정렬 트레이스를 잔광으로 겹칩니다.", L"零交叉对齐的轨迹以余辉叠加。", L"آثار محاذاة عند الصفر مع تلاشي فسفور.", L"Трассы по пересечению нуля с затуханием.", L"Nulldurchgangs-Spuren mit Phosphor-Abklingen.", L"Trilhas alinhadas no zero com persistencia.", L"Nuldoorgang-sporen met fosforverval.", L"Slady na przejscu przez zero z zanikiem.", L"Sifir gecisli izler fosfor sönmesiyle."));
		}
	}

	CCustomPopupMenu* subLower = menu.AddSubMenu(
		LL14(L"下部の表示", L"Lower display", L"Affichage inferieur", L"Display inferiore", L"Pantalla inferior", L"하단 표시", L"下部显示", L"العرض السفلي", L"Нижняя область", L"Untere Anzeige", L"Exibicao inferior", L"Onderste weergave", L"Dolny widok", L"Alt gosterim"),
		LL14(L"下部ペインの表示内容を選びます。", L"Choose what the lower pane shows.", L"Choisir l'affichage du panneau inferieur.", L"Scegli cosa mostra il riquadro inferiore.", L"Elegir que muestra el panel inferior.", L"하단 패널 표시 내용을 고릅니다.", L"选择下部窗格显示内容。", L"اختر ما يعرضه اللوح السفلي.", L"Выбрать содержимое нижней панели.", L"Untere Anzeige wahlen.", L"Escolher o que o painel inferior mostra.", L"Kies wat het onderpaneel toont.", L"Wybierz zawartosc dolnego panelu.", L"Alt panelde ne gosterilecegini sec."));
	if (subLower) {
		CCustomPopupMenu* subLspec = subLower->AddSubMenu(
			LL14(L"スペクトル", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"طيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"),
			LL14(L"周波数特性と 1/3 オクターブ RTA。", L"Frequency response and 1/3-octave RTA.", L"Reponse en frequence et RTA 1/3 d'octave.", L"Risposta in frequenza e RTA 1/3.", L"Respuesta en frecuencia y RTA 1/3.", L"주파수 특성과 1/3 옥타브 RTA.", L"频率特性与 1/3 倍频程 RTA。", L"استجابة ترددية و RTA ثلث أوكتاف.", L"АЧХ и RTA 1/3 октавы.", L"Frequenzgang und 1/3-Oktav-RTA.", L"Resposta em frequencia e RTA 1/3.", L"Frequentierespons en 1/3-octaaf-RTA.", L"Charakterystyka i RTA 1/3 oktawy.", L"Frekans yaniti ve 1/3 oktav RTA."));
		if (subLspec) {
			subLspec->AddCheck(IDM_LOWER_SPECTRUM,
				LL14(L"周波数特性", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"الطيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"),
				m_lowerMode == LowerSpectrum);
			subLspec->AddCheck(IDM_LOWER_RTA,
				LL14(L"1/3 オクターブ RTA", L"1/3-octave RTA", L"RTA 1/3 d'octave", L"RTA 1/3 d'ottava", L"RTA de 1/3 de octava", L"1/3 옥타브 RTA", L"1/3 倍频程 RTA", L"RTA ثلث أوكتاف", L"RTA 1/3 октавы", L"1/3-Oktav-RTA", L"RTA 1/3 de oitava", L"1/3-octaaf-RTA", L"RTA 1/3 oktawy", L"1/3 oktav RTA"),
				m_lowerMode == LowerRta,
				LL14(L"ISO 31 バンドの 1/3 オクターブ実時間アナライザです。", L"ISO 31-band 1/3-octave real-time analyzer.", L"Analyseur temps réel ISO 31 bandes 1/3 d'octave.", L"Analizzatore in tempo reale ISO 31 bande 1/3.", L"Analizador en tiempo real ISO 31 bandas 1/3.", L"ISO 31밴드 1/3 옥타브 실시간 분석기.", L"ISO 31 段 1/3 倍频程实时分析。", L"محلل زمن حقيقي ISO 31 نطاقاً ثلث أوكتاف.", L"RTA ISO 31 полос 1/3 октавы.", L"ISO-31-Band 1/3-Oktav-Echtzeitanalysator.", L"Analisador em tempo real ISO 31 faixas 1/3.", L"ISO 31-band 1/3-octaaf realtime-analyzer.", L"RTA ISO 31 pasm 1/3 oktawy.", L"ISO 31 bant 1/3 oktav gercek zamanli analizor."));
		}
		subLower->AddCheck(IDM_LOWER_WATERFALL,
			LL14(L"スペクトログラム", L"Spectrogram", L"Spectrogramme", L"Spettrogramma", L"Espectrograma", L"스펙트로그램", L"频谱图", L"مخطط طيفي", L"Спектрограмма", L"Spektrogramm", L"Espectrograma", L"Spectrogram", L"Spektrogram", L"Spektrogram"),
			m_lowerMode == LowerWaterfall);
		CCustomPopupMenu* subLstereo = subLower->AddSubMenu(
			LL14(L"ステレオ場", L"Stereo field", L"Champ stereo", L"Campo stereo", L"Campo estereo", L"스테레오 장", L"立体声场", L"مجال ستيريو", L"Стереополе", L"Stereofeld", L"Campo estereo", L"Stereoveld", L"Pole stereo", L"Stereo alani"),
			LL14(L"位相スコープと密度ゴニオメータ。", L"Phase scope and density goniometer.", L"Scope de phase et goniometre densite.", L"Scope di fase e goniometro densita.", L"Osciloscopio de fase y goniometro.", L"위상 스코프와 밀도 고니오미터.", L"相位示波与密度测向仪。", L"راسم طور ومقياس اتجاه كثافة.", L"Фазовый скоп и гониометр.", L"Phasenskop und Dichte-Goniometer.", L"Escopo de fase e goniometro.", L"Fasescoop en dichtheidsgoniometer.", L"Skop fazy i goniometr gestosci.", L"Faz skopu ve yogunluk gonyometresi."));
		if (subLstereo) {
			subLstereo->AddCheck(IDM_LOWER_PHASE,
				LL14(L"位相スコープ", L"Phase scope", L"Scope de phase", L"Scope di fase", L"Osciloscopio de fase", L"위상 스코프", L"相位示波器", L"راسم الطور", L"Фазовый скоп", L"Phasenskop", L"Escopo de fase", L"Fasescoop", L"Skop fazy", L"Faz skopu"),
				m_lowerMode == LowerPhase);
			subLstereo->AddCheck(IDM_LOWER_VECTOR,
				LL14(L"ゴニオメータ (密度)", L"Goniometer (density)", L"Goniometre (densite)", L"Goniometro (densita)", L"Goniometro (densidad)", L"고니오미터 (밀도)", L"测向仪（密度）", L"مقياس الاتجاه (كثافة)", L"Гониометр (плотность)", L"Goniometer (Dichte)", L"Goniometro (densidade)", L"Goniometer (dichtheid)", L"Goniometr (gestosc)", L"Gonyometre (yogunluk)"),
				m_lowerMode == LowerVector);
		}
		CCustomPopupMenu* subLband = subLower->AddSubMenu(
			LL14(L"帯域解析", L"Band analysis", L"Analyse par bande", L"Analisi per banda", L"Analisis por banda", L"대역 분석", L"频段分析", L"تحليل النطاق", L"Анализ полос", L"Bandanalyse", L"Analise por faixa", L"Bandanalyse", L"Analiza pasm", L"Bant analizi"),
			LL14(L"周波数ごとの相関と位相差。", L"Correlation and phase difference vs frequency.", L"Correlation et phase par frequence.", L"Correlazione e fase per frequenza.", L"Correlacion y fase por frecuencia.", L"주파수별 상관과 위상차.", L"按频率的相关与相位差。", L"ترابط وطور حسب التردد.", L"Корреляция и фаза по частоте.", L"Korrelation und Phase vs. Frequenz.", L"Correlacao e fase por frequencia.", L"Correlatie en fase vs frequentie.", L"Korelacja i faza vs czestotliwosc.", L"Frekansa gore korelasyon ve faz."));
		if (subLband) {
			subLband->AddCheck(IDM_LOWER_CORRFREQ,
				LL14(L"帯域相関", L"Correlation vs frequency", L"Correlation par frequence", L"Correlazione per frequenza", L"Correlacion por frecuencia", L"대역 상관", L"频段相关", L"ترابط حسب التردد", L"Корреляция по частоте", L"Korrelation vs. Frequenz", L"Correlacao por frequencia", L"Correlatie vs frequentie", L"Korelacja vs czestotliwosc", L"Frekansa gore korelasyon"),
				m_lowerMode == LowerCorrFreq,
				LL14(L"L/R クロススペクトルから周波数ごとの相関 (−1…+1) を出します。", L"Per-band L/R correlation (−1…+1) from the cross-spectrum.", L"Correlation L/R par bande (−1…+1) via le spectre croise.", L"Correlazione L/R per banda (−1…+1) dallo spettro incrociato.", L"Correlacion L/R por banda (−1…+1) del espectro cruzado.", L"L/R 크로스 스펙트럼으로 주파수별 상관(−1…+1).", L"由 L/R 交叉谱得到各频段相关（−1…+1）。", L"ترابط L/R لكل نطاق (−1…+1) من الطيف المتقاطع.", L"Корреляция L/R по полосам (−1…+1) из кросс-спектра.", L"L/R-Korrelation je Band (−1…+1) aus dem Kreuzspektrum.", L"Correlacao L/R por faixa (−1…+1) do espectro cruzado.", L"L/R-correlatie per band (−1…+1) uit het kruisspectrum.", L"Korelacja L/R w pasmie (−1…+1) ze spektrum skrośnego.", L"Capraz spektrumdan bant basina L/R korelasyonu (−1…+1)."));
			subLband->AddCheck(IDM_LOWER_PHASEFREQ,
				LL14(L"帯域位相差", L"Phase vs frequency", L"Phase par frequence", L"Fase per frequenza", L"Fase por frecuencia", L"대역 위상차", L"频段相位差", L"طور حسب التردد", L"Фаза по частоте", L"Phase vs. Frequenz", L"Fase por frequencia", L"Fase vs frequentie", L"Faza vs czestotliwosc", L"Frekansa gore faz"),
				m_lowerMode == LowerPhaseFreq,
				LL14(L"L/R の周波数ごとの位相差です（モノは 0°、逆相は ±180°）。", L"Per-band L/R phase difference (mono = 0°, antiphase = ±180°).", L"Difference de phase L/R par bande (mono = 0°, antiphasé = ±180°).", L"Differenza di fase L/R per banda (mono = 0°, antifase = ±180°).", L"Diferencia de fase L/R por banda (mono = 0°, antifase = ±180°).", L"주파수별 L/R 위상차(모노 0°, 역위상 ±180°).", L"各频段 L/R 相位差（单声道 0°，反相 ±180°）。", L"فرق طور L/R لكل نطاق (مونو 0°، عكس ±180°).", L"Разность фаз L/R по полосам (моно 0°, противофаза ±180°).", L"L/R-Phasendifferenz je Band (Mono 0°, Gegenphase ±180°).", L"Diferenca de fase L/R por faixa (mono = 0°, antifase = ±180°).", L"L/R-faseverschil per band (mono = 0°, tegenfase = ±180°).", L"Roznica fazy L/R w pasmie (mono 0°, antyfaza ±180°).", L"Bant basina L/R faz farki (mono 0°, ters faz ±180°)."));
		}
	}

	CCustomPopupMenu* subDiff = menu.AddSubMenu(
		LL14(L"スペクトラム差分", L"Spectrum difference", L"Difference de spectre", L"Differenza di spettro", L"Diferencia de espectro", L"스펙트럼 차분", L"频谱差分", L"فرق الطيف", L"Разница спектра", L"Spektrumdifferenz", L"Diferenca de espectro", L"Spectrumverschil", L"Roznica widma", L"Spektrum farki"),
		LL14(L"スナップショットとの差分表示で周波数特性の変化を比較します。", L"Compare spectrum changes against a captured snapshot.", L"Comparer le spectre a un instantane capture.", L"Confronta lo spettro con un'istantanea.", L"Comparar el espectro con una instantanea.", L"스냅샷과 비교해 스펙트럼 변화를 봅니다.", L"对照快照比较频谱变化。", L"مقارنة تغيّر الطيف مع لقطة محفوظة.", L"Сравнивать спектр со снимком.", L"Spektrumanderungen mit Momentaufnahme vergleichen.", L"Comparar mudancas do espectro com um instantaneo.", L"Vergelijk spectrumwijzigingen met een momentopname.", L"Porownuj zmiany widma ze snapshotem.", L"Spektrum degisimini anlik goruntuyle karsilastir."));
	if (subDiff) {
		subDiff->AddCheck(IDM_SPEC_DIFF,
			LL14(L"差分を表示", L"Show difference", L"Afficher la difference", L"Mostra differenza", L"Mostrar diferencia", L"차분 표시", L"显示差分", L"إظهار الفرق", L"Показать разницу", L"Differenz anzeigen", L"Mostrar diferenca", L"Verschil tonen", L"Pokaz roznice", L"Farki goster"),
			m_specDiff);
		subDiff->AddCommand(IDM_SPEC_SNAP,
			LL14(L"現在をスナップショット", L"Capture snapshot", L"Capturer un instantane", L"Cattura istantanea", L"Capturar instantanea", L"현재를 스냅샷", L"捕获快照", L"التقاط لقطة", L"Сделать снимок", L"Momentaufnahme erstellen", L"Capturar instantaneo", L"Momentopname maken", L"Zrob migawke", L"Anlik goruntu al"));
		subDiff->AddCommand(IDM_SPEC_SNAP_CLEAR,
			LL14(L"スナップショットを消去", L"Clear snapshot", L"Effacer l'instantane", L"Cancella istantanea", L"Borrar instantanea", L"스냅샷 지우기", L"清除快照", L"مسح اللقطة", L"Удалить снимок", L"Momentaufnahme loschen", L"Limpar instantaneo", L"Momentopname wissen", L"Wyczysc migawke", L"Anlik goruntuyu sil"),
			NULL, m_specSnapValid);
	}

	CCustomPopupMenu* subZoom = menu.AddSubMenu(
		LL14(L"周波数の範囲", L"Frequency range", L"Plage de frequences", L"Intervallo di frequenza", L"Rango de frecuencias", L"주파수 범위", L"频率范围", L"نطاق التردد", L"Диапазон частот", L"Frequenzbereich", L"Faixa de frequencia", L"Frequentiebereik", L"Zakres czestotliwosci", L"Frekans araligi"),
		LL14(L"表示する周波数帯域をズームします（全帯域／低域／中域／高域）。", L"Zoom the spectrum to a frequency band (full / low / mid / high).", L"Zoomer le spectre sur une bande (complete / graves / mediums / aigus).", L"Zoom dello spettro su una banda (intera / bassi / medi / alti).", L"Acercar el espectro a una banda (completa / graves / medios / agudos).", L"스펙트럼을 주파수 대역으로 확대합니다(전대역/저역/중역/고역).", L"将频谱缩放到频段（全频/低频/中频/高频）。", L"تكبير الطيف إلى نطاق تردد (كامل/منخفض/متوسط/عالي).", L"Масштабировать спектр по полосе (весь / низ / середина / верх).", L"Spektrum auf Frequenzband zoomen (voll / Tiefen / Mitten / Hohen).", L"Aproximar o espectro a uma faixa (completa / graves / medios / agudos).", L"Zoom het spectrum naar een band (volledig / laag / midden / hoog).", L"Przybliz widmo do pasma (pelne / niskie / srednie / wysokie).", L"Spektrumu frekans bandina yaklastir (tam / bas / orta / tiz)."));
	if (subZoom) {
		subZoom->AddCheck(IDM_ZOOM_FULL,
			LL14(L"全帯域", L"Full range", L"Bande complete", L"Banda intera", L"Banda completa", L"전대역", L"全频段", L"النطاق الكامل", L"Весь диапазон", L"Voller Bereich", L"Faixa completa", L"Volledig bereik", L"Pelny zakres", L"Tam bant"),
			m_freqZoom == ZoomFull);
		subZoom->AddCheck(IDM_ZOOM_LOW,
			LL14(L"低域 (20-250Hz)", L"Low (20-250 Hz)", L"Graves (20-250 Hz)", L"Bassi (20-250 Hz)", L"Graves (20-250 Hz)", L"저역 (20-250Hz)", L"低频 (20-250Hz)", L"منخفض (20-250 هرتز)", L"Низкие (20-250 Гц)", L"Tiefen (20-250 Hz)", L"Graves (20-250 Hz)", L"Laag (20-250 Hz)", L"Niskie (20-250 Hz)", L"Bas (20-250 Hz)"),
			m_freqZoom == ZoomLow);
		subZoom->AddCheck(IDM_ZOOM_MID,
			LL14(L"中域 (250Hz-4kHz)", L"Mid (250 Hz - 4 kHz)", L"Mediums (250 Hz - 4 kHz)", L"Medi (250 Hz - 4 kHz)", L"Medios (250 Hz - 4 kHz)", L"중역 (250Hz-4kHz)", L"中频 (250Hz-4kHz)", L"متوسط (250 هرتز - 4 كيلوهرتز)", L"Средние (250 Гц - 4 кГц)", L"Mitten (250 Hz - 4 kHz)", L"Medios (250 Hz - 4 kHz)", L"Midden (250 Hz - 4 kHz)", L"Srednie (250 Hz - 4 kHz)", L"Orta (250 Hz - 4 kHz)"),
			m_freqZoom == ZoomMid);
		subZoom->AddCheck(IDM_ZOOM_HIGH,
			LL14(L"高域 (4kHz以上)", L"High (4 kHz and up)", L"Aigus (4 kHz et plus)", L"Alti (da 4 kHz)", L"Agudos (desde 4 kHz)", L"고역 (4kHz 이상)", L"高频 (4kHz 以上)", L"عالي (4 كيلوهرتز فأكثر)", L"Высокие (от 4 кГц)", L"Hohen (ab 4 kHz)", L"Agudos (a partir de 4 kHz)", L"Hoog (vanaf 4 kHz)", L"Wysokie (od 4 kHz)", L"Tiz (4 kHz ve ustu)"),
			m_freqZoom == ZoomHigh);
	}

	CCustomPopupMenu* subMarker = menu.AddSubMenu(
		LL14(L"周波数マーカー", L"Frequency markers", L"Marqueurs de frequence", L"Marcatori di frequenza", L"Marcadores de frecuencia", L"주파수 마커", L"频率标记", L"علامات التردد", L"Частотные маркеры", L"Frequenzmarker", L"Marcadores de frequencia", L"Frequentiemarkeringen", L"Znaczniki czestotliwosci", L"Frekans isaretleri"),
		LL14(L"ホバー位置への追加や、近いマーカー／すべての削除など周波数マーカーを管理します。", L"Manage frequency markers (add at hover, remove nearest, or clear all).", L"Gerer les marqueurs (ajouter au survol, retirer le plus proche, tout effacer).", L"Gestisci i marcatori (aggiungi all'hover, rimuovi il piu vicino, cancella tutti).", L"Administrar marcadores (anadir al hover, quitar el mas cercano, borrar todos).", L"호버에 추가·가까운 것/전체 삭제 등 주파수 마커를 관리합니다.", L"管理频率标记（悬停处添加、删除最近、全部清除）。", L"إدارة علامات التردد (إضافة عند التمرير، إزالة الأقرب، مسح الكل).", L"Управлять частотными маркерами (добавить, удалить ближайший, очистить все).", L"Frequenzmarker verwalten (an Hover, Nachsten entfernen, alle loschen).", L"Gerenciar marcadores (adicionar no hover, remover o mais proximo, limpar todos).", L"Beheer frequentiemarkers (toevoegen bij hover, dichtstbij verwijderen, alles wissen).", L"Zarzadzaj znacznikami (dodaj przy hover, usun najblizszy, wyczysc wszystkie).", L"Frekans isaretlerini yonet (hover'a ekle, en yakini sil, tumunu temizle)."));
	if (subMarker) {
		subMarker->AddCommand(IDM_MARKER_ADD,
			LL14(L"ホバー位置に追加", L"Add at hover position", L"Ajouter a la position du survol", L"Aggiungi alla posizione del cursore", L"Anadir en la posicion del cursor", L"호버 위치에 추가", L"在悬停位置添加", L"إضافة عند موضع المؤشر", L"Добавить в позиции курсора", L"An Hover-Position hinzufugen", L"Adicionar na posicao do cursor", L"Toevoegen op hoverpositie", L"Dodaj w miejscu kursora", L"Imlec konumuna ekle"),
			NULL, m_hoverValid);
		subMarker->AddCommand(IDM_MARKER_REMOVE,
			LL14(L"最も近いものを削除", L"Remove nearest", L"Supprimer le plus proche", L"Rimuovi il piu vicino", L"Eliminar el mas cercano", L"가장 가까운 것 삭제", L"删除最近的", L"إزالة الأقرب", L"Удалить ближайший", L"Nachsten entfernen", L"Remover o mais proximo", L"Dichtstbijzijnde verwijderen", L"Usun najblizszy", L"En yakini kaldir"));
		subMarker->AddCommand(IDM_MARKER_CLEAR,
			LL14(L"すべて削除", L"Clear all", L"Tout effacer", L"Cancella tutti", L"Borrar todos", L"모두 삭제", L"全部清除", L"مسح الكل", L"Удалить все", L"Alle loschen", L"Limpar todos", L"Alles wissen", L"Usun wszystkie", L"Tumunu temizle"));
	}

	CCustomPopupMenu* subMs = menu.AddSubMenu(
		LL14(L"相関→M/S", L"Correlation→M/S", L"Correlation→M/S", L"Correlazione→M/S", L"Correlacion→M/S", L"상관→M/S", L"相关→M/S", L"ترابط→M/S", L"Корреляция→M/S", L"Korrelation→M/S", L"Correlacao→M/S", L"Correlatie→M/S", L"Korelacja→M/S", L"Korelasyon→M/S"),
		LL14(L"相関メーターの傾向から Mid/Side 幅プリセットを適用します。", L"Apply Mid/Side width presets from correlation trends.", L"Appliquer des presets Mid/Side selon la correlation.", L"Applica preset Mid/Side dalla correlazione.", L"Aplicar presets Mid/Side segun la correlacion.", L"상관 추세에 따라 Mid/Side 폭 프리셋을 적용합니다.", L"根据相关趋势应用 Mid/Side 宽度预设。", L"تطبيق إعدادات عرض Mid/Side من اتجاه الترابط.", L"Применять пресеты ширины Mid/Side по корреляции.", L"Mid/Side-Breitenpresets aus Korrelationstrends.", L"Aplicar presets Mid/Side pela correlacao.", L"Pas Mid/Side-breedtepresets toe via correlatie.", L"Stosuj presety szerokosci Mid/Side z korelacji.", L"Korelasyon egiliminden Mid/Side genislik onayarlarini uygula."));
	if (subMs) {
		subMs->AddSlider(
			LL14(L"M/S 幅 (%)", L"M/S width (%)", L"Largeur M/S (%)", L"Larghezza M/S (%)", L"Ancho M/S (%)",
				L"M/S 폭 (%)", L"M/S 宽度 (%)", L"عرض M/S (%)", L"Ширина M/S (%)", L"M/S-Breite (%)",
				L"Largura M/S (%)", L"M/S-breedte (%)", L"Szerokosc M/S (%)", L"M/S genislik (%)"),
			0, 200, ProClampI(savedata.pro_ms_width, 0, 200),
			AnalyzerMsWidthSliderCb, NULL,
			LL14(L"Mid/Side 幅（ドラッグ中に反映・モノ解除）", L"Mid/Side width (live; clears mono)", L"Largeur Mid/Side (direct; coupe mono)", L"Larghezza Mid/Side (live; toglie mono)", L"Ancho Mid/Side (en vivo; quita mono)",
				L"Mid/Side 폭(즉시·모노 해제)", L"Mid/Side 宽度（即时；清除 mono）", L"عرض Mid/Side (مباشر؛ يلغي mono)", L"Ширина Mid/Side (сразу; сброс mono)", L"Mid/Side-Breite (live; Mono aus)",
				L"Largura Mid/Side (ao vivo; limpa mono)", L"Mid/Side-breedte (live; mono uit)", L"Szerokosc Mid/Side (na zywo; bez mono)", L"Mid/Side genislik (anlik; mono kapat)"));
		subMs->AddCheck(IDM_MS_NARROW,
			LL14(L"M/S 狭め (40%)", L"M/S narrow (40%)", L"M/S etroit (40%)", L"M/S stretto (40%)", L"M/S estrecho (40%)", L"M/S 좁게 (40%)", L"M/S 窄 (40%)", L"M/S ضيق (40%)", L"M/S узко (40%)", L"M/S schmal (40%)", L"M/S estreito (40%)", L"M/S smal (40%)", L"M/S wasko (40%)", L"M/S dar (40%)"),
			savedata.pro_ms_width == 40 && !savedata.pro_ms_mono);
		subMs->AddCheck(IDM_MS_WIDE,
			LL14(L"M/S 広げ (160%)", L"M/S wide (160%)", L"M/S large (160%)", L"M/S ampio (160%)", L"M/S ancho (160%)", L"M/S 넓게 (160%)", L"M/S 宽 (160%)", L"M/S واسع (160%)", L"M/S широко (160%)", L"M/S breit (160%)", L"M/S largo (160%)", L"M/S breed (160%)", L"M/S szeroko (160%)", L"M/S genis (160%)"),
			savedata.pro_ms_width == 160 && !savedata.pro_ms_mono);
		subMs->AddCheck(IDM_MS_MONO,
			LL14(L"M/S モノ", L"M/S mono", L"M/S mono", L"M/S mono", L"M/S mono", L"M/S 모노", L"M/S  mono", L"M/S mono", L"M/S моно", L"M/S mono", L"M/S mono", L"M/S mono", L"M/S mono", L"M/S mono"),
			savedata.pro_ms_mono != 0);
		subMs->AddCheck(IDM_MS_RESET,
			LL14(L"M/S リセット", L"M/S reset", L"M/S reinit.", L"M/S reset", L"M/S restablecer", L"M/S 리셋", L"M/S 重置", L"M/S reset", L"M/S сброс", L"M/S reset", L"M/S reset", L"M/S reset", L"M/S reset", L"M/S sifirla"),
			savedata.pro_ms_width == 100 && !savedata.pro_ms_mono);
		subMs->AddCommand(IDM_MS_FROM_CORR,
			LL14(L"相関ヒストから提案", L"Suggest from corr history", L"Suggérer depuis corr.", L"Suggerisci da corr.", L"Sugerir desde corr.", L"상관 이력에서 제안", L"根据相关历史建议", L"اقتراح من الارتباط", L"Предложить по корреляции", L"Aus Korr.-Verlauf vorschlagen", L"Sugerir pelo hist. corr.", L"Voorstel uit corr.-geschiedenis", L"Zaproponuj z historii kor.", L"Korelasyon gecmisinden oner"));
	}
	menu.AddCommand(IDM_SAVE_JACKET,
		LL14(L"スペクトルを画像保存…", L"Save spectrogram as image…", L"Enregistrer le spectrogramme…", L"Salva spettrogramma…", L"Guardar espectrograma…", L"스펙트럼 이미지 저장…", L"将频谱图保存为图片…", L"حفظ الطيف كصورة…", L"Сохранить спектrogram…", L"Spektrogramm speichern…", L"Salvar espectrograma…", L"Spectrogram opslaan…", L"Zapisz spektrogram…", L"Spektrumu kaydet…"),
		LL14(L"スペクトル／スペクトログラムを画像として書き出します（ジャケット用）。", L"Export the spectrum/spectrogram as an image (for jacket use).", L"Exporter le spectre/spectrogramme en image (pour jaquette).", L"Esporta spettro/spettrogramma come immagine (per jacket).", L"Exportar espectro/espectrograma como imagen (para caratula).", L"스펙트럼/스펙트로그램을 이미지로 내보냅니다(재킷용).", L"将频谱/频谱图导出为图片（封面用）。", L"تصدير الطيف/المخطط الطيفي كصورة (للغلاف).", L"Экспортировать спектр/спектрограмму как изображение (для обложки).", L"Spektrum/Spektrogramm als Bild exportieren (fur Cover).", L"Exportar espectro/espectrograma como imagem (para capa).", L"Exporteer spectrum/spectrogram als afbeelding (voor jacket).", L"Eksportuj widmo/spektrogram jako obraz (na okladke).", L"Spektrum/spektrogrami goruntu olarak disa aktar (kapak icin)."));
	menu.AddSeparator();

	CCustomPopupMenu* subLayout = menu.AddSubMenu(
		LL14(L"周波数特性の表示", L"Frequency display", L"Affichage frequence", L"Visualizzazione frequenza", L"Vista de frecuencia", L"주파수 표시", L"频率显示", L"عرض التردد", L"Отображение АЧХ", L"Frequenzanzeige", L"Exibicao de frequencia", L"Frequentieweergave", L"Wyswietlanie czest.", L"Frekans gorunumu"),
		LL14(L"チャンネルの配置（重ね描き／上下・左右分割／グリッド）を選びます。", L"Choose channel layout (overlay / split / grid).", L"Choisir la disposition des canaux (superpose / division / grille).", L"Scegli layout canali (sovrapposto / divisione / griglia).", L"Elegir disposicion de canales (superpuesto / division / cuadrícula).", L"채널 배치(겹침/상하·좌우 분할/그리드)를 고릅니다.", L"选择声道布局（叠加/上下·左右分割/网格）。", L"اختر تخطيط القنوات (تراكب/تقسيم/شبكة).", L"Выбрать раскладку каналов (наложение / разделение / сетка).", L"Kanallage wahlen (Uberlagerung / Teilung / Raster).", L"Escolher layout dos canais (sobreposto / divisao / grade).", L"Kies kanaallayout (overlay / splitsen / raster).", L"Wybierz uklad kanalow (nakladanie / podzial / siatka).", L"Kanal duzenini sec (ustuste / bolme / izgara)."));
	if (subLayout) {
		subLayout->AddCheck(IDM_SPEC_OVERLAY,
			LL14(L"重ね描き", L"Overlay", L"Superpose", L"Sovrapposto", L"Superpuesto", L"겹침", L"叠加", L"تراكب", L"Наложение", L"Uberlagert", L"Sobreposto", L"Overlay", L"Nakladanie", L"Ustuste"),
			m_specLayout == SpecOverlay);
		subLayout->AddCheck(IDM_SPEC_SPLIT_V,
			LL14(L"上下分割", L"Split vertical", L"Division verticale", L"Divisione verticale", L"Division vertical", L"상하 분할", L"上下分割", L"تقسيم رأسي", L"Вертикально", L"Vertikal teilen", L"Dividir vertical", L"Verticaal", L"Pionowo", L"Dikey bol"),
			m_specLayout == SpecSplitV);
		subLayout->AddCheck(IDM_SPEC_SPLIT_H,
			LL14(L"左右分割", L"Split horizontal", L"Division horizontale", L"Divisione orizzontale", L"Division horizontal", L"좌우 분할", L"左右分割", L"تقسيم أفقي", L"Горизонтально", L"Horizontal teilen", L"Dividir horizontal", L"Horizontaal", L"Poziomo", L"Yatay bol"),
			m_specLayout == SpecSplitH);
		subLayout->AddCheck(IDM_SPEC_GRID4,
			LL14(L"4分割 (2x2)", L"4-way (2x2)", L"4 voies (2x2)", L"4 vie (2x2)", L"4 vias (2x2)", L"4분할 (2x2)", L"四分割(2x2)", L"4 اتجاهات", L"4 панели", L"4-fach (2x2)", L"4 vias (2x2)", L"4-weg (2x2)", L"4 panele (2x2)", L"4 bolum (2x2)"),
			m_specLayout == SpecGrid4, NULL, m_channels >= 4);
		subLayout->AddCheck(IDM_SPEC_GRID8,
			LL14(L"8分割 (2x4)", L"8-way (2x4)", L"8 voies (2x4)", L"8 vie (2x4)", L"8 vias (2x4)", L"8분할 (2x4)", L"八分割(2x4)", L"8 اتجاهات", L"8 панелей", L"8-fach (2x4)", L"8 vias (2x4)", L"8-weg (2x4)", L"8 paneli (2x4)", L"8 bolum (2x4)"),
			m_specLayout == SpecGrid8, NULL, m_channels >= 5);
	}

	CCustomPopupMenu* subStyle = menu.AddSubMenu(
		LL14(L"周波数の表示モード", L"Frequency display mode", L"Mode d'affichage frequence", L"Modalita visualizzazione", L"Modo de visualizacion", L"주파수 표시 모드", L"频率显示模式", L"وضع عرض التردد", L"Режим АЧХ", L"Frequenz-Anzeigemodus", L"Modo de frequencia", L"Frequentieweergavemodus", L"Tryb wyswietlania czest.", L"Frekans gorunum modu"),
		LL14(L"周波数特性の見た目（Ozone／Cubase／SPAN／Ableton／FabFilter 風など）を選びます。", L"Choose spectrum visual style (Ozone / Cubase / SPAN / Ableton / FabFilter looks, etc.).", L"Choisir le style visuel du spectre (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Scegli lo stile visuale dello spettro (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Elegir el estilo visual del espectro (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"스펙트럼 시각 스타일(Ozone/Cubase/SPAN/Ableton/FabFilter 등)을 고릅니다.", L"选择频谱视觉风格（Ozone/Cubase/SPAN/Ableton/FabFilter 等）。", L"اختر نمط عرض الطيف (Ozone/Cubase/SPAN/Ableton/FabFilter…).", L"Выбрать визуальный стиль спектра (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Visuellen Spektrumstil wahlen (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Escolher o estilo visual do espectro (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Kies visuele spectrumstijl (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Wybierz styl wizualny widma (Ozone / Cubase / SPAN / Ableton / FabFilter…).", L"Spektrum gorsel stilini sec (Ozone / Cubase / SPAN / Ableton / FabFilter…)."));
	if (subStyle) {
		subStyle->AddCheck(IDM_STYLE_FILL,
			LL14(L"Ozone 風 (塗+線)", L"Ozone (fill+line)", L"Ozone (rempl.+ligne)", L"Ozone (riemp.+linea)", L"Ozone (relleno+linea)", L"Ozone (채움+선)", L"Ozone(填充+线)", L"Ozone (تعبئة+خط)", L"Ozone (заливка+линия)", L"Ozone (Fullung+Linie)", L"Ozone (preench.+linha)", L"Ozone (vulling+lijn)", L"Ozone (wypeln.+linia)", L"Ozone (dolgu+cizgi)"),
			m_specStyle == StyleFill);
		subStyle->AddCheck(IDM_STYLE_CUBASE,
			LL14(L"Cubase Frequency 風", L"Cubase Frequency style", L"Style Cubase Frequency", L"Stile Cubase Frequency", L"Estilo Cubase Frequency", L"Cubase Frequency 스타일", L"Cubase Frequency 风格", L"نمط Cubase Frequency", L"Стиль Cubase Frequency", L"Cubase Frequency-Stil", L"Estilo Cubase Frequency", L"Cubase Frequency-stijl", L"Styl Cubase Frequency", L"Cubase Frequency tarzı"),
			m_specStyle == StyleCubase);
		subStyle->AddCheck(IDM_STYLE_SPAN,
			LL14(L"Voxengo SPAN 風", L"Voxengo SPAN style", L"Style Voxengo SPAN", L"Stile Voxengo SPAN", L"Estilo Voxengo SPAN", L"Voxengo SPAN 스타일", L"Voxengo SPAN 风格", L"نمط Voxengo SPAN", L"Стиль Voxengo SPAN", L"Voxengo SPAN-Stil", L"Estilo Voxengo SPAN", L"Voxengo SPAN-stijl", L"Styl Voxengo SPAN", L"Voxengo SPAN tarzı"),
			m_specStyle == StyleSpan);
		subStyle->AddCheck(IDM_STYLE_ABLETON,
			LL14(L"Ableton Spectrum 風", L"Ableton Spectrum style", L"Style Ableton Spectrum", L"Stile Ableton Spectrum", L"Estilo Ableton Spectrum", L"Ableton Spectrum 스타일", L"Ableton Spectrum 风格", L"نمط Ableton Spectrum", L"Стиль Ableton Spectrum", L"Ableton Spectrum-Stil", L"Estilo Ableton Spectrum", L"Ableton Spectrum-stijl", L"Styl Ableton Spectrum", L"Ableton Spectrum tarzı"),
			m_specStyle == StyleAbleton);
		subStyle->AddCheck(IDM_STYLE_FABFILTER,
			LL14(L"FabFilter Pro-Q 風", L"FabFilter Pro-Q style", L"Style FabFilter Pro-Q", L"Stile FabFilter Pro-Q", L"Estilo FabFilter Pro-Q", L"FabFilter Pro-Q 스타일", L"FabFilter Pro-Q 风格", L"نمط FabFilter Pro-Q", L"Стиль FabFilter Pro-Q", L"FabFilter Pro-Q-Stil", L"Estilo FabFilter Pro-Q", L"FabFilter Pro-Q-stijl", L"Styl FabFilter Pro-Q", L"FabFilter Pro-Q tarzı"),
			m_specStyle == StyleFabFilter);
		subStyle->AddCheck(IDM_STYLE_BARS,
			LL14(L"バー (汎用)", L"Bars (generic)", L"Barres", L"Barre", L"Barras", L"막대", L"柱状", L"أشرطة", L"Столбцы", L"Balken", L"Barras", L"Balken", L"Slupki", L"Cubuk"),
			m_specStyle == StyleBars);
		subStyle->AddCheck(IDM_STYLE_LINE,
			LL14(L"線のみ", L"Line only", L"Ligne seule", L"Solo linea", L"Solo linea", L"선만", L"仅线", L"خط فقط", L"Только линия", L"Nur Linie", L"Somente linha", L"Alleen lijn", L"Tylko linia", L"Sadece cizgi"),
			m_specStyle == StyleLine);
	}

	CCustomPopupMenu* subSpeed = menu.AddSubMenu(
		LL14(L"波形の流れる速度", L"Wave scroll speed", L"Vitesse de defilement", L"Velocita scorrimento", L"Velocidad de desplazamiento", L"파형 스크롤 속도", L"波形滚动速度", L"سرعة تمرير الموجة", L"Скорость прокрутки волны", L"Wellen-Scrollgeschwindigkeit", L"Velocidade de rolagem", L"Golf-scrolsnelheid", L"Predkosc przewijania fali", L"Dalga kaydirma hizi"),
		LL14(L"上部波形の横スクロール速さを調整します。", L"Adjust horizontal scroll speed of the upper waveform.", L"Regler la vitesse de defilement de l'onde.", L"Regola la velocita di scorrimento dell'onda.", L"Ajustar la velocidad de desplazamiento de la onda.", L"상단 파형의 가로 스크롤 속도를 조정합니다.", L"调整上部波形的横向滚动速度。", L"ضبط سرعة التمرير الأفقي للموجة العلوية.", L"Скорость горизонтальной прокрутки верхней волны.", L"Horizontale Scrollgeschwindigkeit der oberen Welle.", L"Ajustar a velocidade de rolagem da onda superior.", L"Pas horizontale scrollsnelheid van de golf aan.", L"Dostosuj predkosc przewijania fali gornej.", L"Ust dalganin yatay kaydirma hizini ayarla."));
	if (subSpeed) {
		subSpeed->AddSlider(
			LL14(L"速度 (%)", L"Speed (%)", L"Vitesse (%)", L"Velocita (%)", L"Velocidad (%)", L"속도 (%)", L"速度 (%)", L"السرعة (%)", L"Скорость (%)", L"Geschwindigkeit (%)", L"Velocidade (%)", L"Snelheid (%)", L"Predkosc (%)", L"Hiz (%)"),
			25, 200, m_waveSpeedPct, &CAnalyzerDlg::WaveSpeedSliderCb, this,
			LL14(L"25%=遅い … 100%=標準 … 200%=速い（ドラッグ中に即反映）", L"25%=slow … 100%=normal … 200%=fast (live while dragging)", L"25%=lent … 100%=normal … 200%=rapide (temps reel)", L"25%=lento … 100%=normale … 200%=veloce (in tempo reale)", L"25%=lento … 100%=normal … 200%=rapido (en vivo)", L"25%=느림 … 100%=표준 … 200%=빠름(드래그 중 즉시 반영)", L"25%=慢 … 100%=标准 … 200%=快（拖动时即时生效）", L"25%=بطيء … 100%=عادي … 200%=سريع (مباشر أثناء السحب)", L"25%=медленно … 100%=обычно … 200%=быстро (сразу при перетаскивании)", L"25%=langsam … 100%=normal … 200%=schnell (live beim Ziehen)", L"25%=lento … 100%=normal … 200%=rapido (ao vivo)", L"25%=traag … 100%=normaal … 200%=snel (live tijdens slepen)", L"25%=wolno … 100%=normalnie … 200%=szybko (na zywo)", L"25%=yavas … 100%=normal … 200%=hizli (suruklerken anlik)"));
		subSpeed->AddSeparator();
		subSpeed->AddCommand(IDM_WAVE_SPEED_BASE + 3,
			LL14(L"100% に戻す", L"Reset to 100%", L"Remettre a 100%", L"Ripristina a 100%", L"Restablecer a 100%", L"100%로 되돌리기", L"重置为 100%", L"إعادة إلى 100%", L"Сбросить на 100%", L"Auf 100% zurucksetzen", L"Redefinir para 100%", L"Terugzetten naar 100%", L"Przywroc 100%", L"100%'e sifirla"));
	}

	menu.AddSeparator();
	{
		CCustomPopupMenu* meter = menu.AddSubMenu(
			LL14(L"メーター", L"Meter", L"Indicateurs", L"Misuratori", L"Medidores", L"미터", L"电平表", L"المقاييس", L"Метры", L"Anzeigen", L"Medidores", L"Meters", L"Mierniki", L"Olcerler"),
			LL14(L"ピークホールド／EQオーバーレイ／レベル／フリーズ／リセット", L"Peak hold, EQ overlay, level, freeze, reset", L"Maintien de crete, superposition EQ, niveau, gel, reset", L"Picco, EQ overlay, livello, congelamento, reset", L"Retencion de pico, superposicion EQ, nivel, congelar, reset", L"피크 홀드/EQ 오버레이/레벨/프리즈/리셋", L"峰值保持/EQ叠加/电平/冻结/重置", L"الاحتفاظ بالذروة وتراكب EQ والمستوى والتجميد وإعادة الضبط", L"Удержание пика, оверлей EQ, уровень, заморозка, сброс", L"Peak Hold, EQ-Overlay, Pegel, Einfrieren, Reset", L"Retencao de pico, sobreposicao EQ, nivel, congelar, reset", L"Piekvasthouden, EQ-overlay, niveau, bevriezen, reset", L"Przytrzymanie szczytu, nakladka EQ, poziom, zamroz, reset", L"Tepe tutma, EQ kaplama, seviye, dondur, sifirla"));
		if (meter) {
			meter->AddCheck(IDM_PEAK_HOLD,
				LL14(L"ピークホールド", L"Peak hold", L"Maintien de crete", L"Picco trattenuto", L"Retencion de pico", L"피크 홀드", L"峰值保持", L"الاحتفاظ بالذروة", L"Удержание пика", L"Peak Hold", L"Retencao de pico", L"Piekvasthouden", L"Przytrzymanie szczytu", L"Tepe tutma"),
				m_peakHold,
				LL14(L"メーター／スペクトル上のピークレベルを保持表示します。", L"Hold peak levels on meters and the spectrum.", L"Maintenir les niveaux de crete sur les indicateurs et le spectre.", L"Trattiene i livelli di picco su misuratori e spettro.", L"Retener niveles de pico en medidores y espectro.", L"미터/스펙트럼의 피크 레벨을 유지 표시합니다.", L"在电平表和频谱上保持峰值电平显示。", L"الاحتفاظ بمستويات الذروة على المقاييس والطيف.", L"Удерживать пиковые уровни на метрах и спектре.", L"Peak-Pegel auf Anzeigen und Spektrum halten.", L"Reter niveis de pico nos medidores e no espectro.", L"Pieklevels op meters en spectrum vasthouden.", L"Przytrzymuj poziomy szczytu na miernikach i widmie.", L"Olcerlerde ve spektrumda tepe seviyelerini tut."));
			meter->AddCheck(IDM_EQ_OVERLAY,
				LL14(L"EQオーバーレイ", L"EQ overlay", L"Superposition EQ", L"Sovrapposizione EQ", L"Superposicion EQ", L"EQ 오버레이", L"EQ叠加", L"تراكب EQ", L"Оверлей EQ", L"EQ-Overlay", L"Sobreposicao EQ", L"EQ-overlay", L"Nakladka EQ", L"EQ kaplama"),
				m_eqOverlay,
				LL14(L"周波数特性の上に EQ カーブを重ねてドラッグ調整できます。", L"Overlay the EQ curve on the spectrum for drag adjustment.", L"Superposer la courbe EQ pour un reglage au glisser.", L"Sovrapponi la curva EQ per regolarla trascinando.", L"Superponer la curva EQ para ajustar arrastrando.", L"스펙트럼 위에 EQ 곡선을 겹쳐 드래그 조정합니다.", L"在频谱上叠加 EQ 曲线以便拖动调整。", L"تراكب منحنى EQ على الطيف للضبط بالسحب.", L"Наложить кривую EQ на спектр для правки перетаскиванием.", L"EQ-Kurve uber Spektrum legen und per Ziehen anpassen.", L"Sobrepor a curva EQ no espectro para ajustar arrastando.", L"EQ-curve over spectrum leggen om te slepen.", L"Nakladka krzywej EQ na widmo do regulacji przeciagnieciem.", L"Spektrum uzerine EQ egrisini bindirip surukleyerek ayarla."));
			meter->AddCheck(IDM_LEVEL_METER,
				LL14(L"レベルメーター", L"Level meter", L"Indicateur de niveau", L"Misuratore di livello", L"Medidor de nivel", L"레벨 미터", L"电平表", L"مقياس المستوى", L"Уровень сигнала", L"Pegelanzeige", L"Medidor de nivel", L"Niveaumeter", L"Miernik poziomu", L"Seviye olcer"),
				m_showLevelMeter,
				LL14(L"レベルメーターの表示／非表示を切り替えます。LR を消しても位相相関は残ります。", L"Show or hide the level meters. Correlation stays if LR is hidden.", L"Afficher ou masquer les indicateurs de niveau. La correlation reste sans LR.", L"Mostra o nasconde i misuratori di livello. La correlazione resta senza LR.", L"Mostrar u ocultar los medidores de nivel. La correlacion permanece sin LR.", L"레벨 미터 표시/숨기기. LR를 꺼도 상관 미터는 남습니다.", L"显示或隐藏电平表。关掉 LR 后相位相关仍保留。", L"إظهار أو إخفاء مقاييس المستوى. يبقى الترابط دون LR.", L"Показать или скрыть измерители уровня. Корреляция остается без LR.", L"Pegelanzeigen ein- oder ausblenden. Korrelation bleibt ohne LR.", L"Mostrar ou ocultar os medidores de nivel. A correlacao permanece sem LR.", L"Niveaumeters tonen of verbergen. Correlatie blijft zonder LR.", L"Pokaz lub ukryj mierniki poziomu. Korelacja zostaje bez LR.", L"Seviye olcerlerini goster veya gizle. LR kapali olsa da korelasyon kalir."));
			meter->AddCheck(IDM_CORR_METER,
				LL14(L"位相相関メーター", L"Correlation meter", L"Correlometre", L"Misuratore di correlazione", L"Medidor de correlacion", L"위상 상관 미터", L"相位相关表", L"مقياس الترابط الطوري", L"Измеритель корреляции", L"Korrelationsanzeige", L"Medidor de correlacao", L"Correlatiemeter", L"Miernik korelacji", L"Faz korelasyon olceri"),
				savedata.pro_corr_meter != 0,
				LL14(L"ステレオ位相相関を表示します（-1…+1）。LR バーを消しても右端に残ります。", L"Show stereo phase correlation (-1…+1). Stays at the right if LR meters are hidden.", L"Correlation de phase stereo (-1…+1). Reste a droite sans LR.", L"Correlazione di fase stereo (-1…+1). Resta a destra senza LR.", L"Correlacion de fase estereo (-1…+1). Permanece a la derecha sin LR.", L"스테레오 위상 상관(-1…+1). LR를 꺼도 오른쪽에 남습니다.", L"立体声相位相关（-1…+1）。关掉 LR 后仍留在右端。", L"ترابط الطور الاستريو (-1…+1). يبقى يميناً دون LR.", L"Фазовая корреляция стерео (-1…+1). Остается справа без LR.", L"Stereo-Phasenkorrelation (-1…+1). Bleibt rechts ohne LR.", L"Correlacao de fase stereo (-1…+1). Permanece a direita sem LR.", L"Stereo-fasecorrelatie (-1…+1). Blijft rechts zonder LR.", L"Korelacja fazy stereo (-1…+1). Zostaje po prawej bez LR.", L"Stereo faz korelasyonu (-1…+1). LR kapali olsa da sagda kalir."));
			meter->AddCheck(IDM_FREEZE,
				LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"),
				m_frozen,
				LL14(L"アナライザ表示を一時停止（フリーズ）します。", L"Freeze the analyzer display.", L"Geler l'affichage de l'analyseur.", L"Congela la visualizzazione dell'analizzatore.", L"Congelar la pantalla del analizador.", L"분석기 표시를 일시 정지(프리즈)합니다.", L"冻结分析器显示。", L"تجميد عرض المحلل.", L"Заморозить отображение анализатора.", L"Analysatoranzeige einfrieren.", L"Congelar a exibicao do analisador.", L"Bevries de analyserweergave.", L"Zamroz wyswietlacz analizatora.", L"Analizor goruntusunu dondur."));
			meter->AddCommand(IDM_RESET_PEAK,
				LL14(L"ピークをリセット (ダブルクリック)", L"Reset peaks (double-click)", L"Reinit. cretes (double-clic)", L"Reset picchi (doppio clic)", L"Restablecer picos (doble clic)", L"피크 리셋(더블클릭)", L"重置峰值(双击)", L"إعادة الذروة (نقر مزدوج)", L"Сброс пиков (двойной клик)", L"Peaks zurucksetzen (Doppelklick)", L"Redefinir picos (duplo clique)", L"Piekreset (dubbelklik)", L"Reset szczytow (dwuklik)", L"Tepe sifirla (cift tik)"),
				LL14(L"ピークホールドをリセットします（ダブルクリックでも可）。", L"Reset peak hold (also via double-click).", L"Reinitialiser le maintien de crete (aussi double-clic).", L"Reset del picco trattenuto (anche doppio clic).", L"Restablecer retencion de pico (tambien doble clic).", L"피크 홀드를 리셋합니다(더블클릭도 가능).", L"重置峰值保持（也可双击）。", L"إعادة الاحتفاظ بالذروة (أو بالنقر المزدوج).", L"Сброс удержания пика (также двойной клик).", L"Peak Hold zurucksetzen (auch Doppelklick).", L"Redefinir retencao de pico (tambem duplo clique).", L"Piekvasthouden resetten (ook dubbelklik).", L"Reset przytrzymania szczytu (tez dwuklik).", L"Tepe tutmayi sifirla (cift tik de olur)."));
		}
	}

	menu.AddSeparator();
	{
		CCustomPopupMenu* clip = menu.AddSubMenu(
			LL14(L"クリップボード", L"Clipboard", L"Presse-papiers", L"Appunti", L"Portapapeles", L"클립보드", L"剪贴板", L"الحافظة", L"Буфер обмена", L"Zwischenablage", L"Area de transferencia", L"Klembord", L"Schowek", L"Pano"),
			LL14(L"ホバー値／最大ピーク／レベルをコピー", L"Copy hover, loudest peak, or levels", L"Copier lecture au survol, pic max ou niveaux", L"Copia hover, picco max o livelli", L"Copiar hover, pico maximo o niveles", L"호버 값/최대 피크/레벨 복사", L"复制悬停读数/最大峰值/电平", L"نسخ قراءة التمرير أو أعلى قمة أو المستويات", L"Копировать наведение, макс. пик или уровни", L"Hover-, Peak- oder Pegelwerte kopieren", L"Copiar hover, pico mais alto ou niveis", L"Hover-, piek- of niveauwaarden kopieren", L"Kopiuj hover, szczyt lub poziomy", L"Hover, tepe veya seviye kopyala"));
		if (clip) {
			clip->AddCommand(IDM_COPY_HOVER,
				LL14(L"ホバー値をコピー", L"Copy hover readout", L"Copier la lecture au survol", L"Copia lettura hover", L"Copiar lectura al pasar", L"호버 값 복사", L"复制悬停读数", L"نسخ قراءة التمرير", L"Копировать наведение", L"Hover-Wert kopieren", L"Copiar leitura ao pairar", L"Hoverwaarde kopieren", L"Kopiuj odczyt hover", L"Hover degerini kopyala"),
				LL14(L"カーソル位置の周波数／レベル値をクリップボードへコピーします。", L"Copy the hover frequency/level readout to the clipboard.", L"Copier la lecture frequence/niveau au survol dans le presse-papiers.", L"Copia lettura frequenza/livello hover negli appunti.", L"Copiar la lectura de frecuencia/nivel al portapapeles.", L"호버 위치의 주파수/레벨 값을 클립보드로 복사합니다.", L"将悬停处的频率/电平读数复制到剪贴板。", L"نسخ قراءة التردد/المستوى عند التمرير إلى الحافظة.", L"Копировать частоту/уровень под курсором в буфер.", L"Hover-Frequenz/Pegel in die Zwischenablage kopieren.", L"Copiar a leitura de frequencia/nivel ao pairar para a area de transferencia.", L"Kopieer hover frequentie/niveau naar het klembord.", L"Kopiuj odczyt czestotliwosci/poziomu hover do schowka.", L"Hover frekans/seviye degerini panoya kopyala."),
				m_hoverValid);
			clip->AddCommand(IDM_COPY_PEAK,
				LL14(L"最大ピークをコピー", L"Copy loudest peak", L"Copier le pic max", L"Copia picco massimo", L"Copiar pico maximo", L"최대 피크 복사", L"复制最大峰值", L"نسخ أعلى قمة", L"Копировать макс. пик", L"Lautesten Peak kopieren", L"Copiar pico mais alto", L"Luidste piek kopieren", L"Kopiuj najglosniejszy szczyt", L"En yuksek tepeyi kopyala"),
				LL14(L"最大ピークの値をクリップボードへコピーします。", L"Copy the loudest peak value to the clipboard.", L"Copier la valeur du pic maximal dans le presse-papiers.", L"Copia il valore del picco massimo negli appunti.", L"Copiar el valor del pico maximo al portapapeles.", L"최대 피크 값을 클립보드로 복사합니다.", L"将最大峰值复制到剪贴板。", L"نسخ قيمة أعلى قمة إلى الحافظة.", L"Копировать значение максимального пика в буфер.", L"Lautesten Peak-Wert in die Zwischenablage kopieren.", L"Copiar o valor do pico mais alto para a area de transferencia.", L"Kopieer de luidste piekwaarde naar het klembord.", L"Kopiuj wartosc najglosniejszego szczytu do schowka.", L"En yuksek tepe degerini panoya kopyala."));
			clip->AddCommand(IDM_COPY_LEVELS,
				LL14(L"レベルをコピー", L"Copy levels", L"Copier les niveaux", L"Copia livelli", L"Copiar niveles", L"레벨 복사", L"复制电平", L"نسخ المستويات", L"Копировать уровни", L"Pegel kopieren", L"Copiar niveis", L"Niveaus kopieren", L"Kopiuj poziomy", L"Seviyeleri kopyala"),
				LL14(L"現在のレベル値をクリップボードへコピーします。", L"Copy current level values to the clipboard.", L"Copier les niveaux actuels dans le presse-papiers.", L"Copia i livelli correnti negli appunti.", L"Copiar los niveles actuales al portapapeles.", L"현재 레벨 값을 클립보드로 복사합니다.", L"将当前电平值复制到剪贴板。", L"نسخ مستويات المستوى الحالية إلى الحافظة.", L"Копировать текущие уровни в буфер.", L"Aktuelle Pegelwerte in die Zwischenablage kopieren.", L"Copiar os niveis atuais para a area de transferencia.", L"Kopieer huidige niveaus naar het klembord.", L"Kopiuj biezace poziomy do schowka.", L"Gecerli seviye degerlerini panoya kopyala."));
		}
	}

	menu.AddSeparator();
	menu.AddCommand(IDM_CLEAR_DISPLAY,
		LL14(L"表示をクリア", L"Clear display", L"Effacer l'affichage", L"Cancella visualizzazione", L"Borrar pantalla", L"표시 지우기", L"清除显示", L"مسح العرض", L"Очистить экран", L"Anzeige leeren", L"Limpar exibicao", L"Weergave wissen", L"Wyczysc wyswietlacz", L"Goruntuyu temizle"),
		LL14(L"表示履歴（波形トレースなど）をクリアします。", L"Clear display history (wave traces, etc.).", L"Effacer l'historique d'affichage (traces d'onde, etc.).", L"Cancella la cronologia di visualizzazione (tracce onda, ecc.).", L"Borrar el historial de pantalla (trazas de onda, etc.).", L"표시 이력(파형 트레이스 등)을 지웁니다.", L"清除显示历史（波形轨迹等）。", L"مسح سجل العرض (آثار الموجة وغيرها).", L"Очистить историю отображения (трассы волны и т. д.).", L"Anzeigehistorie leeren (Wellenspuren usw.).", L"Limpar o historico de exibicao (trilhas de onda, etc.).", L"Wis weergavegeschiedenis (golfsporen enz.).", L"Wyczysc historie wyswietlania (slady fali itd.).", L"Goruntu gecmisini temizle (dalga izleri vb.)."));
	menu.AddCheck(IDM_ALWAYS_ON_TOP,
		LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위에 표시", L"始终置顶", L"دائما في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd op voorgrond", L"Zawsze na wierzchu", L"Her zaman ustte"),
		m_alwaysOnTop,
		LL14(L"ウィンドウを常に他のウィンドウの手前に表示します。", L"Keep this window always on top of others.", L"Garder cette fenetre toujours au premier plan.", L"Mantieni questa finestra sempre in primo piano.", L"Mantener esta ventana siempre delante de las demas.", L"이 창을 항상 다른 창 위에 표시합니다.", L"将此窗口始终置于其他窗口之上。", L"إبقاء هذه النافذة دائماً فوق النوافذ الأخرى.", L"Держать это окно поверх остальных.", L"Dieses Fenster immer im Vordergrund halten.", L"Manter esta janela sempre acima das outras.", L"Houd dit venster altijd boven andere.", L"Trzymaj to okno zawsze na wierzchu.", L"Bu pencereyi her zaman digerlerinin ustunde tut."));
	menu.AddSeparator();
	menu.AddCheck(IDM_SOFT3D_API_DEMO,
		LL14(L"Soft3D APIデモ", L"Soft3D API demo", L"Demo API Soft3D", L"Demo API Soft3D", L"Demo API Soft3D", L"Soft3D API 데모", L"Soft3D API演示", L"عرض Soft3D API", L"Демо Soft3D API", L"Soft3D-API-Demo", L"Demo API Soft3D", L"Soft3D API-demo", L"Demo Soft3D API", L"Soft3D API demosu"),
		m_soft3dApiDemo,
		LL14(L"カメラがゆっくり周回し、箱・球・トーラス・波形が公転する確認用デモです（もう一度選ぶと解除）。", L"Camera slowly orbits while boxes, spheres, torus and waves revolve (pick again to turn off).", L"La caméra orbite; boîtes, sphères, tore et ondes tournent (réactiver pour couper).", L"La camera orbita; scatole, sfere, toro e onde ruotano (riseleziona per spegnere).", L"La cámara orbita; cajas, esferas, toro y ondas giran (vuelva a elegir para apagar).", L"카메라가 천천히 돌며 상자·구·토러스·파형이 공전하는 확인용 데모(다시 선택하면 해제).", L"相机缓慢环绕，箱/球/环/波形公转的确认演示（再选一次关闭）。", L"الكاميرا تدور ببطء مع صناديق وكرات وحلقات وموجات (أعد الاختيار للإيقاف).", L"Камера медленно облетает; ящики, сферы, тор и волны вращаются (выберите снова, чтобы выкл.).", L"Kamera kreist langsam; Boxen, Kugeln, Torus und Wellen kreisen (erneut wählen zum Aus).", L"A câmera orbita; caixas, esferas, toro e ondas giram (escolha de novo para desligar).", L"Camera draait langzaam; dozen, bollen, torus en golven cirkelen (kies opnieuw om uit).", L"Kamera powoli orbituje; pudełka, sfery, torus i fale krążą (wybierz ponownie, by wyłączyć).", L"Kamera yavaş döner; kutular, küreler, torus ve dalgalar dolanır (kapatmak için tekrar seçin)."));
	menu.AddSeparator();
	{
		CCustomPopupMenu* openSub = menu.AddSubMenu(
			LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Offnen", L"Abrir", L"Openen", L"Otworz", L"Ac"),
			LL14(L"イコライザ／ピアノロール／MIDIモニタ／操作ガイドを開きます。", L"Open equalizer, piano roll, MIDI monitor, or the operation guide.", L"Ouvrir egaliseur, piano roll, moniteur MIDI ou le guide.", L"Apri equalizzatore, piano roll, monitor MIDI o la guida.", L"Abrir ecualizador, piano roll, monitor MIDI o la guia.", L"이퀄라이저/피아노 롤/MIDI 모니터/조작 가이드를 엽니다.", L"打开均衡器/钢琴卷帘/MIDI监视器/操作指南。", L"فتح المعادل أو لفافة البيانو أو مراقب MIDI أو الدليل.", L"Открыть эквалайзер, пианоролл, MIDI-монитор или руководство.", L"Equalizer, Piano-Roll, MIDI-Monitor oder Anleitung offnen.", L"Abrir equalizador, piano roll, monitor MIDI ou o guia.", L"Open equalizer, piano-roll, MIDI-monitor of handleiding.", L"Otworz equalizer, piano roll, monitor MIDI lub przewodnik.", L"Equalizer, piyano roll, MIDI izleyici veya kilavuzu ac."));
		if (openSub) {
			openSub->AddCommand(ID_MP_OPEN_EQ,
				LL14(L"イコライザを開く", L"Open equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore", L"Abrir ecualizador",
					L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل", L"Открыть эквалайзер", L"Equalizer öffnen",
					L"Abrir equalizador", L"Equalizer openen", L"Otworz equalizer", L"Equalizeri ac"),
				LL14(L"イコライザウィンドウを開きます。", L"Open the equalizer window.", L"Ouvrir la fenetre de l'egaliseur.", L"Apri la finestra dell'equalizzatore.", L"Abrir la ventana del ecualizador.", L"이퀄라이저 창을 엽니다.", L"打开均衡器窗口。", L"فتح نافذة المعادل.", L"Открыть окно эквалайзера.", L"Equalizer-Fenster offnen.", L"Abrir a janela do equalizador.", L"Open het equalizer-venster.", L"Otworz okno equalizera.", L"Equalizer penceresini ac."));
			openSub->AddCommand(ID_MP_OPEN_PIANOROLL,
				LL14(L"ピアノロールを開く", L"Open piano roll", L"Ouvrir le piano roll", L"Apri piano roll", L"Abrir piano roll",
					L"피아노 롤 열기", L"打开钢琴卷帘", L"فتح لفافة البيانو", L"Открыть пианоролл", L"Piano-Roll öffnen",
					L"Abrir piano roll", L"Piano-roll openen", L"Otworz piano roll", L"Piyano rolunu ac"),
				LL14(L"簡易ピアノロールウィンドウを開きます。", L"Open the simple piano roll window.", L"Ouvrir la fenetre du piano roll simple.", L"Apri la finestra del piano roll semplice.", L"Abrir la ventana del piano roll simple.", L"간이 피아노 롤 창을 엽니다.", L"打开简易钢琴卷帘窗口。", L"فتح نافذة لفافة البيانو البسيطة.", L"Открыть окно простого пианоролла.", L"Einfaches Piano-Roll-Fenster offnen.", L"Abrir a janela do piano roll simples.", L"Open het eenvoudige piano-roll-venster.", L"Otworz okno prostego piano roll.", L"Basit piyano roll penceresini ac."));
			openSub->AddCommand(ID_MP_OPEN_MIDIMON,
				LL14(L"MIDIモニタを開く", L"Open MIDI monitor", L"Ouvrir le moniteur MIDI", L"Apri monitor MIDI", L"Abrir monitor MIDI",
					L"MIDI 모니터 열기", L"打开MIDI监视器", L"فتح مراقب MIDI", L"Открыть MIDI-монитор", L"MIDI-Monitor öffnen",
					L"Abrir monitor MIDI", L"MIDI-monitor openen", L"Otworz monitor MIDI", L"MIDI izleyiciyi ac"),
				LL14(L"MIDI 32パート・モニタを開きます。", L"Open the 32-part MIDI monitor.", L"Ouvrir le moniteur MIDI 32 parties.", L"Apri il monitor MIDI a 32 parti.", L"Abrir el monitor MIDI de 32 partes.", L"MIDI 32파트 모니터를 엽니다.", L"打开 MIDI 32 声部监视器。", L"فتح مراقب MIDI ذا 32 جزءاً.", L"Открыть MIDI-монитор на 32 партии.", L"32-Part-MIDI-Monitor offnen.", L"Abrir o monitor MIDI de 32 partes.", L"Open de MIDI-monitor met 32 partijen.", L"Otworz monitor MIDI 32 partii.", L"32 part MIDI izleyiciyi ac."));
			openSub->AddSeparator();
			openSub->AddCommand(ID_HELP_SHOWSHEET,
				LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa",
					L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل",
					L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding",
					L"Przewodnik", L"İşlem kılavuzu"),
				LL14(L"アナライザの操作ガイドを表示します。", L"Show the analyzer operation guide.", L"Afficher le guide d'utilisation de l'analyseur.", L"Mostra la guida operativa dell'analizzatore.", L"Mostrar la guia de operacion del analizador.", L"분석기 조작 가이드를 표시합니다.", L"显示分析器操作指南。", L"عرض دليل تشغيل المحلل.", L"Показать руководство по анализатору.", L"Bedienungsanleitung des Analysators anzeigen.", L"Mostrar o guia de operacao do analisador.", L"Toon de handleiding van de analyser.", L"Pokaz przewodnik po analizatorze.", L"Analizor islem kilavuzunu goster."));
		}
	}

	if (point.x == -1 && point.y == -1) {
		CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
		point = CPoint(rc.left + 8, rc.top + 8);
	}
	const UINT cmd = menu.Track(point, this);
	if (cmd == IDM_VIEW_TOP_2D) {
		savedata.analyzerviewmodeTop = 0;
		KickUiPresent();
	}
	else if (cmd == IDM_VIEW_TOP_3D) {
		savedata.analyzerviewmodeTop = 1;
		if (!(savedata.soft3dTourSeen & 2)) {
			savedata.soft3dTourSeen |= 2;
			m_soft3dTourUntil = GetTickCount() + 3000;
		}
		SyncSoft3DCamsFromSave();
		KickUiPresent();
	}
	else if (cmd == IDM_VIEW_BOT_2D) {
		savedata.analyzerviewmodeBot = 0;
		KickUiPresent();
	}
	else if (cmd == IDM_VIEW_BOT_3D) {
		savedata.analyzerviewmodeBot = 1;
		if (!(savedata.soft3dTourSeen & 2)) {
			savedata.soft3dTourSeen |= 2;
			m_soft3dTourUntil = GetTickCount() + 3000;
		}
		SyncSoft3DCamsFromSave();
		KickUiPresent();
	}
	else if (cmd == IDM_VIEW_TOP_RESET) {
		savedata.analyzer3dyawTop = -220;
		savedata.analyzer3dpitchTop = 260;
		savedata.analyzer3dzoomTop = 100;
		SyncSoft3DCamsFromSave();
		KickUiPresent();
	}
	else if (cmd == IDM_VIEW_BOT_RESET) {
		savedata.analyzer3dyawBot = -220;
		savedata.analyzer3dpitchBot = 260;
		savedata.analyzer3dzoomBot = 100;
		SyncSoft3DCamsFromSave();
		KickUiPresent();
	}
	else if (cmd == IDM_SOFT3D_API_DEMO) {
		m_soft3dApiDemo = !m_soft3dApiDemo;
		if (m_soft3dApiDemo)
			SetTimer(2, 33, nullptr); // 回転アニメ用
		else
			KillTimer(2);
		KickUiPresent();
	}
	else if (cmd == ID_MP_OPEN_EQ || cmd == ID_MP_OPEN_PIANOROLL || cmd == ID_MP_OPEN_MIDIMON) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->PostMessage(WM_COMMAND, cmd);
		else if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (cmd == ID_MP_OPEN_EQ)
				og->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
			else if (cmd == ID_MP_OPEN_MIDIMON)
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 3, 0);
			else
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 1, 0);
		}
	}
	else if (cmd != 0)
		SendMessage(WM_COMMAND, cmd);
}
LPCTSTR CAnalyzerDlg::ChannelLabel(int ch, int channels)
{
	static const TCHAR* stereo[] = { _T("L"), _T("R") };
	static const TCHAR* ch21[] = { _T("L"), _T("R"), _T("LFE") };
	static const TCHAR* ch51[] = { _T("L"), _T("R"), _T("C"), _T("LFE"), _T("SL"), _T("SR") };
	static const TCHAR* ch71[] = { _T("L"), _T("R"), _T("C"), _T("LFE"), _T("SL"), _T("SR"), _T("BL"), _T("BR") };
	static TCHAR generic[8][8];
	if (channels == 2 && ch < 2) return stereo[ch];
	if (channels == 3 && ch < 3) return ch21[ch];
	if (channels == 6 && ch < 6) return ch51[ch];
	if (channels >= 8 && ch < 8) return ch71[ch];
	_stprintf_s(generic[ch], _T("Ch%d"), ch + 1);
	return generic[ch];
}

int CAnalyzerDlg::VisibleChannelCount(int waveH) const
{
	if (m_waveMode == WaveMidSide) {
		const int fit = (std::max)(1, waveH / 22);
		return (std::min)(2, fit);
	}
	if (m_waveMode == WaveLoudness) {
		const int ch = (m_channels >= 2) ? 2 : 1;
		const int fit = (std::max)(1, waveH / 22);
		return (std::min)(ch, fit);
	}
	const int ch = (std::max)(1, (std::min)(m_channels, CH_MAX));
	const int minBand = 22;
	const int fit = (std::max)(1, waveH / minBand);
	return (std::min)(ch, fit);
}

void CAnalyzerDlg::FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels)
{
	if (!m_feedEnabled || m_frozen || !pData || frames <= 0) return;
	if (channels < 1) channels = 1;
	if (channels > CH_MAX) channels = CH_MAX;
	if (sampleRate > 0) {
		m_sampleRate = sampleRate;
		// ~1.2 秒分が見える密度 × 速度倍率。細かすぎると負荷、粗すぎるとカクつく
		const int targetW = (m_waveW > 40) ? m_waveW : 640;
		const int baseSpc = (std::max)(4, sampleRate * 6 / 5 / targetW);
		const int speedPct = (std::max)(25, (std::min)(200, m_waveSpeedPct));
		m_samplesPerCol = (std::max)(2, (baseSpc * 100 + speedPct / 2) / speedPct);
	}

	float framePeak[CH_MAX] = {};
	double frameSumSq[CH_MAX] = {};
	int frameN[CH_MAX] = {};
	// 簡易 TP: 直前2サンプルから 3点ラグランジュで半サンプル位置を推定(オーバーシュートを拾う)
	float tpPrev1[2] = { 0.0f, 0.0f };
	float tpPrev2[2] = { 0.0f, 0.0f };
	float framePeakTp = 0.0f;
	EnterCriticalSection(&m_cs);
	if (m_channels != channels) {
		m_channels = channels;
		m_waveReady = false;
		m_specDirty = true;
	}
	{
		const int i1 = (m_ringWrite - 1 + RING_SAMPLES) % RING_SAMPLES;
		const int i2 = (m_ringWrite - 2 + RING_SAMPLES) % RING_SAMPLES;
		for (int c = 0; c < 2; ++c) {
			tpPrev1[c] = m_ring[c][i1];
			tpPrev2[c] = m_ring[c][i2];
		}
	}

	const int tpCh = (channels >= 2) ? 2 : 1;
	for (int i = 0; i < frames; ++i) {
		for (int c = 0; c < channels; ++c) {
			const float v = SampleToFloat(pData, bits, i, c, channels);
			m_ring[c][m_ringWrite] = v;
			const float a = fabsf(v);
			if (a > framePeak[c]) framePeak[c] = a;
			frameSumSq[c] += (double)a * (double)a;
			++frameN[c];
			if (c < tpCh) {
				if (a > framePeakTp) framePeakTp = a;
				const float mid = 0.375f * tpPrev2[c] + 0.75f * tpPrev1[c] - 0.125f * v;
				const float am = fabsf(mid);
				if (am > framePeakTp) framePeakTp = am;
				tpPrev2[c] = tpPrev1[c];
				tpPrev1[c] = v;
			}
		}
		for (int c = channels; c < CH_MAX; ++c)
			m_ring[c][m_ringWrite] = 0.0f;
		m_ringWrite = (m_ringWrite + 1) % RING_SAMPLES;
		if (m_ringFilled < RING_SAMPLES) ++m_ringFilled;
	}
	m_accSamples += frames;

	// 簡易 LUFS(モーメンタリ近似): ProAudio と同じ 10*log10(mean)-0.691。K特性は省略。
	{
		double meanSq = 0.0;
		int used = 0;
		for (int c = 0; c < tpCh; ++c) {
			if (frameN[c] > 0) {
				meanSq += frameSumSq[c] / (double)frameN[c];
				++used;
			}
		}
		if (used > 0) {
			meanSq /= (double)used;
			m_lufsSum += meanSq * (double)frames;
			m_lufsCount += (double)frames;
			if (meanSq < 1e-12) meanSq = 1e-12;
			const float block = 10.0f * (float)log10(meanSq) - 0.691f;
			if (block > m_lufsMom)
				m_lufsMom = m_lufsMom * 0.60f + block * 0.40f;
			else
				m_lufsMom = m_lufsMom * 0.92f + block * 0.08f;
			if (m_lufsMom < -70.0f) m_lufsMom = -70.0f;
		}
		m_tpLin = framePeakTp;
		if (framePeakTp > m_tpHold)
			m_tpHold = framePeakTp;
		else
			m_tpHold *= 0.9995f;
	}

	float maxPk = 0.0f;
	const int meterCh = (std::min)(channels, CH_MAX);
	for (int c = 0; c < meterCh; ++c) {
		float rms = 0.0f;
		if (frameN[c] > 0)
			rms = (float)sqrt(frameSumSq[c] / (double)frameN[c]);
		if (rms > m_meterRms[c])
			m_meterRms[c] = m_meterRms[c] * 0.55f + rms * 0.45f;
		else
			m_meterRms[c] = m_meterRms[c] * 0.92f + rms * 0.08f;

		// ピークホールドも RMS の最大値を保持(サンプルピークではない)
		if (m_meterRms[c] >= m_meterHold[c])
			m_meterHold[c] = m_meterRms[c];
		else
			m_meterHold[c] = m_meterHold[c] * 0.997f;

		// 参考用にサンプルピークも残す(描画では使わない)
		if (framePeak[c] > m_meterPeak[c])
			m_meterPeak[c] = framePeak[c];
		else
			m_meterPeak[c] = m_meterPeak[c] * 0.92f + framePeak[c] * 0.08f;

		// 波形オートレンジは従来通り L/R 基準(LFE等で縮小しないように)
		if (c < 2 && framePeak[c] > maxPk) maxPk = framePeak[c];
	}
	// 波形オートレンジはサンプルピーク基準(min/max描画用)
	if (maxPk > m_waveDispPeak)
		m_waveDispPeak = m_waveDispPeak * 0.70f + maxPk * 0.30f;
	else
		m_waveDispPeak = m_waveDispPeak * 0.997f + maxPk * 0.003f;
	if (m_waveDispPeak < 0.06f) m_waveDispPeak = 0.06f;
	if (m_waveDispPeak > 1.0f) m_waveDispPeak = 1.0f;

	const int spc = (std::max)(4, m_samplesPerCol);
	int pushed = 0;
	const int pushCap = (m_waveW > 0) ? m_waveW : 120;
	while (m_accSamples >= spc && pushed < pushCap) {
		m_accSamples -= spc;
		++m_pendingScroll;
		++pushed;
	}
	const int cap = (m_waveW > 0) ? m_waveW : 640;
	if (m_pendingScroll > cap)
		m_pendingScroll = cap;
	LeaveCriticalSection(&m_cs);
	RequestSpecAnalysis();
	KickUiPresent();
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
	// |X|*2/N。Hann 時は正弦≈-6dBFS 読み(窓補正なし)。過大表示を避けるため窓補正はしない。
	const float norm = 2.0f / (float)FFT_SIZE;
	const int kNyq = FFT_SIZE / 2;
	float magLin[FFT_SIZE / 2 + 1];
	float instant[CH_MAX][SPEC_BINS];
	float smoothed[SPEC_BINS];
	float corrTmp[SPEC_BINS];
	float phaseTmp[SPEC_BINS];
	for (int b = 0; b < SPEC_BINS; ++b) {
		corrTmp[b] = 0.0f;
		phaseTmp[b] = 0.0f;
	}

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
		if (c == 0) {
			m_fftLre[0] = m_fftRe[0];
			m_fftLim[0] = m_fftIm[0];
			for (int k = 1; k <= kNyq; ++k) {
				m_fftLre[k] = m_fftRe[k];
				m_fftLim[k] = m_fftIm[k];
			}
		}

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

		if (c == 1) {
			for (int b = 0; b < SPEC_BINS; ++b) {
				const float fLo = SpecEdgeHz(b, SPEC_BINS, nyquist);
				const float fHi = SpecEdgeHz(b + 1, SPEC_BINS, nyquist);
				float kLo = fLo * (float)FFT_SIZE / (float)sr;
				float kHi = fHi * (float)FFT_SIZE / (float)sr;
				if (kLo < 1.0f) kLo = 1.0f;
				if (kHi > (float)kNyq) kHi = (float)kNyq;
				if (kHi <= kLo) kHi = kLo + 0.05f;
				double crossRe = 0.0, crossIm = 0.0, magL2 = 0.0, magR2 = 0.0;
				const int i0 = (int)kLo;
				const int i1 = (int)kHi;
				for (int k = i0; k <= i1; ++k) {
					if (k < 1 || k > kNyq) continue;
					const float a = (std::max)(kLo, (float)k);
					const float bb = (std::min)(kHi, (float)(k + 1));
					const float w = bb - a;
					if (w <= 0.0f) continue;
					const float Lre = m_fftLre[k], Lim = m_fftLim[k];
					const float Rre = m_fftRe[k], Rim = m_fftIm[k];
					crossRe += (double)((Lre * Rre + Lim * Rim) * w);
					crossIm += (double)((Lim * Rre - Lre * Rim) * w);
					magL2 += (double)((Lre * Lre + Lim * Lim) * w);
					magR2 += (double)((Rre * Rre + Rim * Rim) * w);
				}
				const double den = sqrt(magL2 * magR2);
				float corr = (den > 1e-20) ? (float)(crossRe / den) : 0.0f;
				if (corr > 1.f) corr = 1.f;
				if (corr < -1.f) corr = -1.f;
				corrTmp[b] = corr;
				phaseTmp[b] = (float)atan2(crossIm, crossRe);
			}
		}
	}

	if (channels < 2) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			corrTmp[b] = 0.0f;
			phaseTmp[b] = 0.0f;
		}
	}

	EnterCriticalSection(&m_cs);
	const bool peakHold = m_peakHold;
	memcpy(m_specCorr, corrTmp, sizeof(m_specCorr));
	memcpy(m_specPhase, phaseTmp, sizeof(m_specPhase));
	for (int c = 0; c < channels; ++c) {
		for (int b = 0; b < SPEC_BINS; ++b) {
			if (instant[c][b] > m_specDb[c][b])
				m_specDb[c][b] = instant[c][b];
			else
				m_specDb[c][b] = m_specDb[c][b] * 0.78f + instant[c][b] * 0.22f;
			if (peakHold) {
				if (instant[c][b] >= m_specPeakDb[c][b])
					m_specPeakDb[c][b] = instant[c][b];
				else
					m_specPeakDb[c][b] = m_specPeakDb[c][b] * 0.985f + instant[c][b] * 0.015f;
			}
			else {
				m_specPeakDb[c][b] = m_specDb[c][b];
			}
		}
	}
	LeaveCriticalSection(&m_cs);
	PushWaterfallRow();
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
	if (m_frameDC.GetSafeHdc()) {
		if (m_frameOld) m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
		if (m_frameBmp.GetSafeHandle()) m_frameBmp.DeleteObject();
		m_frameDC.DeleteDC();
	}
	m_waveW = m_waveH = 0;
	m_specW = m_specH = 0;
	m_frameW = m_frameH = 0;
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

bool CAnalyzerDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h)
		return true;
	if (m_frameDC.GetSafeHdc()) {
		if (m_frameOld) m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
		if (m_frameBmp.GetSafeHandle()) m_frameBmp.DeleteObject();
		m_frameDC.DeleteDC();
	}
	if (!m_frameDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) { m_frameDC.DeleteDC(); return false; }
	m_frameOld = m_frameDC.SelectObject(&m_frameBmp);
	m_frameW = w;
	m_frameH = h;
	return true;
}

void CAnalyzerDlg::KickUiPresent()
{
	// 多重 Post を防ぐ。表示キックは ms2（ピアノ/EQ/MP と帯域を分け合う）
	if (!::IsWindow(m_hWnd)) return;
	if (InterlockedCompareExchange(&m_fullRedrawBusy, 0, 0) != 0) {
		InterlockedExchange(&m_presentDeferred, 1);
		return;
	}
	const DWORD now = GetTickCount();
	if (InterlockedCompareExchange(&m_presentPosted, 0, 0) != 0) {
		// Invalidate 待ち固着時のみ強制描画して開放
		if (m_lastPresentKickTick != 0 && (now - m_lastPresentKickTick) >= 150u) {
			MSG msg;
			while (::PeekMessage(&msg, m_hWnd, WM_ANALYZER_PRESENT, WM_ANALYZER_PRESENT, PM_REMOVE)) {}
			AnalyzerPresentInvalidate(m_hWnd);
			UpdateWindow();
			InterlockedExchange(&m_presentPosted, 0);
		}
		else {
			return;
		}
	}
	int minMs = savedata.ms2;
	if (minMs < 16) minMs = 16;
	if (minMs > 960) minMs = 960;
	// Soft3D APIデモは回転アニメのため短周期でキック
	if (m_soft3dApiDemo && minMs > 33) minMs = 33;
	if (m_lastPresentKickTick != 0 && (now - m_lastPresentKickTick) < (DWORD)minMs)
		return;
	if (InterlockedCompareExchange(&m_presentPosted, 1, 0) != 0) return;
	m_lastPresentKickTick = now;
	if (!PostMessage(WM_ANALYZER_PRESENT, 0, 0))
		InterlockedExchange(&m_presentPosted, 0);
}

void CAnalyzerDlg::FullRedrawWave(COLORREF bg)
{
	if (!m_waveDC.GetSafeHdc() || m_waveW <= 0 || m_waveH <= 0) return;
	// 全再描画なので溜まった scroll は無効（完了後の catch-up 嵐を防ぐ）
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
	const int plotWEarly = AnalyzerWavePlotWidth(m_waveW, m_channels, m_showLevelMeter ? 1 : 0);
	if (m_waveMode != WavePhosphor || !m_waveReady)
		m_waveDC.FillSolidRect(0, 0, m_waveW, m_waveH, bg);
	else if (plotWEarly < m_waveW)
		m_waveDC.FillSolidRect(plotWEarly, 0, m_waveW - plotWEarly, m_waveH, bg);

	int channels = 0, filled = 0, write = 0;
	if (!SnapshotRing(write, filled, channels)) {
		m_waveReady = true;
		return;
	}
	int spc = 64;
	EnterCriticalSection(&m_cs);
	spc = (std::max)(4, m_samplesPerCol);
	LeaveCriticalSection(&m_cs);

	const int vis = VisibleChannelCount(m_waveH);
	m_waveLayoutCh = vis;
	const int plotW = AnalyzerWavePlotWidth(m_waveW, channels, m_showLevelMeter ? 1 : 0);
	m_waveLayoutPlotW = plotW;
	if (filled < 8) {
		m_waveReady = true;
		return;
	}

	const int bandH = m_waveH / vis;
	CFont* oldFont = m_waveDC.SelectObject(&m_font);
	m_waveDC.SetBkMode(TRANSPARENT);
	float dispPeak = 0.25f;
	float lufsNow = -70.0f;
	EnterCriticalSection(&m_cs);
	dispPeak = m_waveDispPeak;
	lufsNow = m_lufsMom;
	LeaveCriticalSection(&m_cs);
	// 直近ピークを帯の ~72% に合わせる(常時フル振りを避ける)。静音は拡大しすぎない。
	float waveGain = 0.70f / (std::max)(0.08f, dispPeak);
	if (waveGain > 2.2f) waveGain = 2.2f;
	if (waveGain < 0.55f) waveGain = 0.55f;

	// トリガー／残光オシロ: リングの直近ウィンドウを立ち上がりゼロクロスに合わせて静止表示。
	const bool trigger = (m_waveMode == WaveTrigger || m_waveMode == WavePhosphor);
	int trigSpc = spc;
	int trigStart = 0;
	if (trigger) {
		const int budget = (std::min)(filled, RING_SAMPLES / 2);
		trigSpc = (std::max)(1, budget / (std::max)(1, plotW));
		const int window = trigSpc * plotW;
		int base = write - window;
		int searchMax = (std::min)(RING_SAMPLES / 4, filled - window);
		if (searchMax < 0) searchMax = 0;
		trigStart = base;
		for (int k = 0; k < searchMax; ++k) {
			const int cur = ((base - k) % RING_SAMPLES + RING_SAMPLES) % RING_SAMPLES;
			const int prv = ((base - k - 1) % RING_SAMPLES + RING_SAMPLES) % RING_SAMPLES;
			if (m_ringSnap[0][prv] <= 0.0f && m_ringSnap[0][cur] > 0.0f) {
				trigStart = base - k;
				break;
			}
		}
	}

	if (m_waveMode == WavePhosphor && m_waveScratchDC.GetSafeHdc()) {
		// 残光: 背景へフェードしてから新しいトレースを重ねる（ちらつき回避のため BB 内で完結）
		m_waveScratchDC.FillSolidRect(0, 0, plotW, m_waveH, bg);
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 42, 0 };
		::GdiAlphaBlend(m_waveDC.GetSafeHdc(), 0, 0, plotW, m_waveH,
			m_waveScratchDC.GetSafeHdc(), 0, 0, plotW, m_waveH, bf);
	}

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		HGDIOBJ oldPen = m_waveDC.SelectObject(::GetStockObject(DC_PEN));
		::SetDCPenColor(m_waveDC.GetSafeHdc(), RGB(40, 44, 58));
		m_waveDC.MoveTo(0, mid);
		m_waveDC.LineTo(plotW, mid);

		const COLORREF col = (m_waveMode == WaveMidSide)
			? ((c == 0) ? RGB(120, 210, 255) : RGB(255, 180, 90))
			: kChColor[c % CH_MAX];
		::SetDCPenColor(m_waveDC.GetSafeHdc(), col);
		const COLORREF fillCol = RGB(
			(GetRValue(col) * 2 + 18) / 5,
			(GetGValue(col) * 2 + 20) / 5,
			(GetBValue(col) * 2 + 28) / 5);

		if (m_waveMode == WaveLoudness) {
			const float lufsT = (lufsNow + 70.0f) / 70.0f;
			int lufsY = y1 - (int)((lufsT < 0.f ? 0.f : (lufsT > 1.f ? 1.f : lufsT)) * (y1 - y0 - 2));
			::SetDCPenColor(m_waveDC.GetSafeHdc(), RGB(80, 90, 70));
			m_waveDC.MoveTo(0, lufsY);
			m_waveDC.LineTo(plotW, lufsY);
		}

		for (int x = 0; x < plotW; ++x) {
			const int ageCols = plotW - 1 - x;
			const int sampleBack = trigger
				? (write - (trigStart + x * trigSpc))
				: (ageCols * spc + spc);
			const int useSpc = trigger ? trigSpc : spc;
			float mn = 1.0f, mx = -1.0f, sum2 = 0.0f;
			int cnt = 0;
			bool any = false;
			for (int s = 0; s < useSpc; ++s) {
				int idx = write - sampleBack + s;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				if (filled < RING_SAMPLES && idx >= filled) continue;
				float v;
				if (m_waveMode == WaveMidSide) {
					float L = m_ringSnap[0][idx];
					float R = (channels >= 2) ? m_ringSnap[1][idx] : L;
					v = (c == 0) ? (0.5f * (L + R)) : (0.5f * (L - R));
				}
				else {
					int srcCh = c;
					if (srcCh >= channels) srcCh = channels - 1;
					if (srcCh < 0) srcCh = 0;
					v = m_ringSnap[srcCh][idx];
				}
				if (v > 1.0f) v = 1.0f;
				if (v < -1.0f) v = -1.0f;
				if (!any || v < mn) mn = v;
				if (!any || v > mx) mx = v;
				sum2 += v * v;
				++cnt;
				any = true;
			}
			if (!any) continue;
			if (m_waveMode == WaveLoudness) {
				float rms = (cnt > 0) ? sqrtf(sum2 / (float)cnt) : 0.0f;
				if (rms > 1.f) rms = 1.f;
				const int yh = y1 - 1 - (int)(rms * (amp * 2));
				const int yTop = (yh < y0 + 1) ? (y0 + 1) : yh;
				if (yTop < y1 - 1)
					m_waveDC.FillSolidRect(x, yTop, 1, (y1 - 1) - yTop, fillCol);
				continue;
			}
			mn *= waveGain; mx *= waveGain;
			if (mn < -1.0f) mn = -1.0f;
			if (mx > 1.0f) mx = 1.0f;
			const int yMx = mid - (int)(mx * amp);
			const int yMn = mid - (int)(mn * amp);
			if (m_waveMode == WaveEnvelope) {
				int top = (yMx < yMn) ? yMx : yMn;
				int bot = (yMx < yMn) ? yMn : yMx;
				if (bot < top + 1) bot = top + 1;
				m_waveDC.FillSolidRect(x, top, 1, bot - top, fillCol);
				m_waveDC.SetPixel(x, yMx, col);
			}
			else {
				m_waveDC.MoveTo(x, yMx);
				m_waveDC.LineTo(x, yMn);
			}
		}

		m_waveDC.FillSolidRect(0, y0, 28, (std::min)(14, y1 - y0), ANALYZER_LABEL_PLATE);
		m_waveDC.SetTextColor(col);
		if (m_waveMode == WaveMidSide)
			m_waveDC.TextOut(4, y0 + 2, (c == 0) ? _T("M") : _T("S"));
		else if (m_waveMode == WaveLoudness)
			m_waveDC.TextOut(4, y0 + 2, (c == 0) ? _T("RMS") : ChannelLabel(c, channels));
		else
			m_waveDC.TextOut(4, y0 + 2, ChannelLabel(c, channels));
		m_waveDC.SelectObject(oldPen);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	EnterCriticalSection(&m_cs);
	m_pendingScroll = 0;
	LeaveCriticalSection(&m_cs);
}

int CAnalyzerDlg::ScrollWaveAndDrawNew(COLORREF bg, int maxScroll)
{
	if (!m_waveDC.GetSafeHdc() || !m_waveScratchDC.GetSafeHdc()) return 0;
	if (m_waveMode == WaveTrigger || m_waveMode == WavePhosphor) {
		// トリガー／残光は毎回全描画(横スクロールしない)
		FullRedrawWave(bg);
		return -1;
	}

	int scroll = 0, channels = 0, write = 0, filled = 0, spc = 64;
	EnterCriticalSection(&m_cs);
	scroll = m_pendingScroll;
	if (maxScroll > 0 && scroll > maxScroll)
		scroll = maxScroll;
	m_pendingScroll -= scroll;
	spc = (std::max)(4, m_samplesPerCol);
	LeaveCriticalSection(&m_cs);

	const int vis = VisibleChannelCount(m_waveH);
	const int plotW = AnalyzerWavePlotWidth(m_waveW, m_channels, m_showLevelMeter ? 1 : 0);
	if (vis != m_waveLayoutCh || plotW != m_waveLayoutPlotW || !m_waveReady) {
		FullRedrawWave(bg);
		return -1;
	}
	if (scroll <= 0) return 0;
	if (scroll >= plotW) {
		FullRedrawWave(bg);
		return -1;
	}
	if (!SnapshotRing(write, filled, channels))
		return 0;

	const int keep = plotW - scroll;
	m_waveScratchDC.BitBlt(0, 0, keep, m_waveH, &m_waveDC, scroll, 0, SRCCOPY);
	m_waveScratchDC.FillSolidRect(keep, 0, scroll, m_waveH, bg);
	m_waveDC.BitBlt(0, 0, plotW, m_waveH, &m_waveScratchDC, 0, 0, SRCCOPY);

	const int bandH = m_waveH / vis;
	CFont* oldFont = m_waveDC.SelectObject(&m_font);
	m_waveDC.SetBkMode(TRANSPARENT);
	float dispPeak = 0.25f;
	float lufsNow = -70.0f;
	EnterCriticalSection(&m_cs);
	dispPeak = m_waveDispPeak;
	lufsNow = m_lufsMom;
	LeaveCriticalSection(&m_cs);
	float waveGain = 0.70f / (std::max)(0.08f, dispPeak);
	if (waveGain > 2.2f) waveGain = 2.2f;
	if (waveGain < 0.55f) waveGain = 0.55f;

	for (int c = 0; c < vis; ++c) {
		const int y0 = c * bandH;
		const int y1 = (c == vis - 1) ? m_waveH : (c + 1) * bandH;
		const int mid = (y0 + y1) / 2;
		const int amp = (std::max)(2, (y1 - y0) / 2 - 2);

		HGDIOBJ oldPen = m_waveDC.SelectObject(::GetStockObject(DC_PEN));
		::SetDCPenColor(m_waveDC.GetSafeHdc(), RGB(40, 44, 58));
		m_waveDC.MoveTo(keep, mid);
		m_waveDC.LineTo(plotW, mid);

		const COLORREF col = (m_waveMode == WaveMidSide)
			? ((c == 0) ? RGB(120, 210, 255) : RGB(255, 180, 90))
			: kChColor[c % CH_MAX];
		::SetDCPenColor(m_waveDC.GetSafeHdc(), col);
		const COLORREF fillCol = RGB(
			(GetRValue(col) * 2 + 18) / 5,
			(GetGValue(col) * 2 + 20) / 5,
			(GetBValue(col) * 2 + 28) / 5);

		if (m_waveMode == WaveLoudness) {
			const float lufsT = (lufsNow + 70.0f) / 70.0f;
			int lufsY = y1 - (int)((lufsT < 0.f ? 0.f : (lufsT > 1.f ? 1.f : lufsT)) * (y1 - y0 - 2));
			::SetDCPenColor(m_waveDC.GetSafeHdc(), RGB(80, 90, 70));
			m_waveDC.MoveTo(keep, lufsY);
			m_waveDC.LineTo(plotW, lufsY);
		}

		for (int x = keep; x < plotW; ++x) {
			const int ageCols = plotW - 1 - x;
			const int sampleBack = ageCols * spc + spc;
			float mn = 1.0f, mx = -1.0f, sum2 = 0.0f;
			int cnt = 0;
			bool any = false;
			for (int s = 0; s < spc; ++s) {
				int idx = write - sampleBack + s;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				if (filled < RING_SAMPLES && idx >= filled) continue;
				float v;
				if (m_waveMode == WaveMidSide) {
					float L = m_ringSnap[0][idx];
					float R = (channels >= 2) ? m_ringSnap[1][idx] : L;
					v = (c == 0) ? (0.5f * (L + R)) : (0.5f * (L - R));
				}
				else {
					int srcCh = c;
					if (srcCh >= channels) srcCh = channels - 1;
					if (srcCh < 0) srcCh = 0;
					v = m_ringSnap[srcCh][idx];
				}
				if (v > 1.0f) v = 1.0f;
				if (v < -1.0f) v = -1.0f;
				if (!any || v < mn) mn = v;
				if (!any || v > mx) mx = v;
				sum2 += v * v;
				++cnt;
				any = true;
			}
			if (!any) continue;
			if (m_waveMode == WaveLoudness) {
				float rms = (cnt > 0) ? sqrtf(sum2 / (float)cnt) : 0.0f;
				if (rms > 1.f) rms = 1.f;
				const int yh = y1 - 1 - (int)(rms * (amp * 2));
				const int yTop = (yh < y0 + 1) ? (y0 + 1) : yh;
				if (yTop < y1 - 1)
					m_waveDC.FillSolidRect(x, yTop, 1, (y1 - 1) - yTop, fillCol);
				continue;
			}
			mn *= waveGain; mx *= waveGain;
			if (mn < -1.0f) mn = -1.0f;
			if (mx > 1.0f) mx = 1.0f;
			const int yMx = mid - (int)(mx * amp);
			const int yMn = mid - (int)(mn * amp);
			if (m_waveMode == WaveEnvelope) {
				int top = (yMx < yMn) ? yMx : yMn;
				int bot = (yMx < yMn) ? yMn : yMx;
				if (bot < top + 1) bot = top + 1;
				m_waveDC.FillSolidRect(x, top, 1, bot - top, fillCol);
				m_waveDC.SetPixel(x, yMx, col);
			}
			else {
				m_waveDC.MoveTo(x, yMx);
				m_waveDC.LineTo(x, yMn);
			}
		}

		m_waveDC.FillSolidRect(0, y0, 28, (std::min)(14, y1 - y0), ANALYZER_LABEL_PLATE);
		m_waveDC.SetTextColor(col);
		if (m_waveMode == WaveMidSide)
			m_waveDC.TextOut(4, y0 + 2, (c == 0) ? _T("M") : _T("S"));
		else if (m_waveMode == WaveLoudness)
			m_waveDC.TextOut(4, y0 + 2, (c == 0) ? _T("RMS") : ChannelLabel(c, channels));
		else
			m_waveDC.TextOut(4, y0 + 2, ChannelLabel(c, channels));
		m_waveDC.SelectObject(oldPen);
	}
	m_waveDC.SelectObject(oldFont);
	m_waveReady = true;
	return scroll;
}

void CAnalyzerDlg::DrawEqOverlay(CDC& dc, const CRect& plot, float nyquist)
{
	if (plot.Width() < 8 || plot.Height() < 8) return;

	float fMinZ = 20.0f, fMaxZ = nyquist;
	GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);
	auto hzToX = [&](float hz) -> int {
		return (int)HzToPlotX(hz, plot, fMinZ, fMaxZ);
	};
	// EQ ゲイン軸: 上=+12dB / 中=0 / 下=-12dB (equaliser と同じスケール)
	auto gainToY = [&](float gainDb) -> int {
		float t = (gainDb + 12.0f) / 24.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};

	// ズーム時は範囲外の帯域を端に潰さない(掴める位置とカーブを一致させる)
	auto bandVisible = [&](int b) -> bool {
		const float f = kEqFreqs[b];
		return f < nyquist && f >= fMinZ * 0.999f && f <= fMaxZ * 1.001f;
	};

	CPen bandPen(PS_DOT, 1, RGB(90, 110, 70));
	CPen* oldPen = dc.SelectObject(&bandPen);
	for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
		if (!bandVisible(b)) continue;
		const int x = hzToX(kEqFreqs[b]);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
	}

	const int y0 = gainToY(0.0f);
	CPen zeroPen(PS_SOLID, 1, RGB(120, 160, 90));
	CPen* prev = dc.SelectObject(&zeroPen);
	dc.MoveTo(plot.left, y0);
	dc.LineTo(plot.right, y0);

	POINT pts[EQ_OVERLAY_BANDS];
	int nPts = 0;
	for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
		if (!bandVisible(b)) continue;
		int eqv = savedata.eq[b];
		if (eqv < 0) eqv = 0;
		if (eqv > 200) eqv = 200;
		pts[nPts].x = hzToX(kEqFreqs[b]);
		pts[nPts].y = gainToY(EqSliderToDb(eqv));
		++nPts;
	}
	if (nPts >= 2) {
		CPen eqPen(PS_SOLID, 2, RGB(200, 255, 120));
		CPen* prev2 = dc.SelectObject(&eqPen);
		dc.Polyline(pts, nPts);
		for (int i = 0; i < nPts; ++i) {
			dc.FillSolidRect(pts[i].x - 2, pts[i].y - 2, 5, 5, RGB(220, 255, 160));
		}
		dc.SelectObject(prev2 ? prev2 : prev);
	}
	dc.SetTextColor(RGB(180, 220, 120));
	dc.TextOut(plot.left + 4, plot.top + 2, _T("EQ"));
	dc.SelectObject(oldPen);
}

void CAnalyzerDlg::DrawSpecPanel(CDC& dc, const CRect& plot, int chBegin, int chCount,
	float spec[][SPEC_BINS], float peak[][SPEC_BINS], int channels, int sr, bool drawTitle)
{
	if (plot.Width() < 8 || plot.Height() < 8 || chCount <= 0) return;
	const float nyquist = (float)sr * 0.5f;
	const int style = m_specStyle;
	const bool drawPeak = m_peakHold;

	CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
	CPen* oldPen = dc.SelectObject(&frame);
	dc.SelectStockObject(NULL_BRUSH);
	dc.Rectangle(plot);
	dc.SelectObject(oldPen); // frame 破棄前に復元

	if (drawTitle) {
		dc.SetTextColor(RGB(180, 190, 210));
		dc.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"周波数特性", L"Frequency response", L"Reponse en frequence", L"Risposta in frequenza",
				L"Respuesta en frecuencia", L"주파수 특성", L"频率特性", L"الاستجابة الترددية",
				L"АЧХ", L"Frequenzgang", L"Resposta em frequencia", L"Frequentierespons",
				L"Charakterystyka", L"Frekans yaniti"));
	}

	// 表示レンジ: 通常 -96..0dB / 差分 -48..+48dB(0dB が中央)
	const bool diff = (m_specDiff && m_specSnapValid);
	float fMinZ = 20.0f, fMaxZ = nyquist;
	GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);

	auto dbToY = [&](float db) -> int {
		float t = diff ? ((db + 48.0f) / 96.0f) : ((db + 96.0f) / 96.0f);
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return plot.bottom - (int)(t * plot.Height());
	};
	auto hzToX = [&](float hz) -> int {
		return (int)HzToPlotX(hz, plot, fMinZ, fMaxZ);
	};
	// 塗り/バーの基準線。差分表示では 0dB(中央)から上下に伸ばす。
	const int base0 = diff ? dbToY(0.0f) : (plot.bottom - 1);

	CPen grid(PS_DOT, 1, RGB(45, 50, 65));
	oldPen = dc.SelectObject(&grid);
	dc.SetTextColor(RGB(120, 130, 150));
	for (int db = diff ? 40 : 0; db >= (diff ? -40 : -80); db -= 20) {
		const int y = dbToY((float)db);
		if (y <= plot.top || y >= plot.bottom) continue;
		dc.MoveTo(plot.left, y);
		dc.LineTo(plot.right, y);
		if (drawTitle && plot.Width() > 60) {
			CString lab;
			lab.Format(_T("%d"), db);
			dc.TextOut(plot.left - 26, y - 6, lab);
		}
	}

	const int markHz[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
	for (int mi = 0; mi < 10; ++mi) {
		const float f = (float)markHz[mi];
		if (f >= nyquist) continue;
		if (f < fMinZ || f > fMaxZ) continue; // ズーム範囲外の目盛りは出さない
		const int x = hzToX(f);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
		if (drawTitle && plot.Height() > 40) {
			CString lab;
			if (markHz[mi] >= 1000) lab.Format(_T("%dk"), markHz[mi] / 1000);
			else lab.Format(_T("%d"), markHz[mi]);
			dc.TextOut(x - 6, plot.bottom - 12, lab);
		}
	}

	// ズーム範囲に入るビンだけを使う(範囲外を端に潰すと縦線状の塊になる)
	int binLo = 0, binHi = SPEC_BINS - 1;
	if (m_freqZoom != ZoomFull) {
		binLo = SPEC_BINS; binHi = -1;
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
			if (cHz < fMinZ || cHz > fMaxZ) continue;
			if (b < binLo) binLo = b;
			if (b > binHi) binHi = b;
		}
		if (binHi < binLo) { binLo = 0; binHi = SPEC_BINS - 1; }
		else {
			if (binLo > 0) --binLo;             // 端が切れて見えないように1つ外側まで
			if (binHi < SPEC_BINS - 1) ++binHi;
		}
	}
	const int nPts = binHi - binLo + 1;

	for (int i = 0; i < chCount; ++i) {
		const int c = chBegin + i;
		if (c < 0 || c >= channels || c >= CH_MAX) continue;

		POINT fillPts[SPEC_BINS + 2];
		POINT peakPts[SPEC_BINS];
		POINT linePts[SPEC_BINS];
		for (int b = binLo; b <= binHi; ++b) {
			const int k = b - binLo;
			const int x = hzToX(SpecCenterHz(b, SPEC_BINS, nyquist));
			linePts[k].x = x;
			linePts[k].y = dbToY(diff ? (spec[c][b] - m_specSnapDb[c][b]) : spec[c][b]);
			peakPts[k].x = x;
			peakPts[k].y = dbToY(diff ? (peak[c][b] - m_specSnapDb[c][b]) : peak[c][b]);
			fillPts[k] = linePts[k];
		}
		fillPts[nPts].x = linePts[nPts - 1].x;
		fillPts[nPts].y = base0;
		fillPts[nPts + 1].x = linePts[0].x;
		fillPts[nPts + 1].y = base0;

		const COLORREF col = kChColor[c];
		const COLORREF fill = RGB(
			(GetRValue(col) * 2 + 18) / 5,
			(GetGValue(col) * 2 + 20) / 5,
			(GetBValue(col) * 2 + 28) / 5);
		const COLORREF fillSoft = RGB(
			(GetRValue(col) + 18 * 3) / 4,
			(GetGValue(col) + 20 * 3) / 4,
			(GetBValue(col) + 28 * 3) / 4);
		const COLORREF fillCubase = RGB(
			(GetRValue(col) + 40) / 3,
			(GetGValue(col) + 90) / 3,
			(GetBValue(col) + 110) / 3);

		const bool isBars = (style == StyleBars || style == StyleSpan || style == StyleAbleton);
		const bool isFill = (style == StyleFill || style == StyleCubase || style == StyleFabFilter);
		const bool isLine = (style == StyleLine || style == StyleFill || style == StyleCubase
			|| style == StyleFabFilter);

		if (isBars) {
			int barW;
			if (style == StyleSpan)
				barW = (std::max)(1, plot.Width() / nPts); // 密着
			else if (style == StyleAbleton)
				barW = (std::max)(2, plot.Width() / (std::max)(1, nPts / 2) - 2); // やや太い
			else
				barW = (std::max)(1, plot.Width() / nPts - 1);

			for (int b = binLo; b <= binHi; ++b) {
				if (style == StyleAbleton && (b & 1)) continue; // 間引いてAbleton風の区切り
				const int k = b - binLo;
				const int x = linePts[k].x;
				const int y = linePts[k].y;
				const int hw = barW / 2;
				CRect bar(x - hw, y, x - hw + barW, base0);
				if (bar.top > bar.bottom) { const int t = bar.top; bar.top = bar.bottom; bar.bottom = t; }
				if (bar.Width() < 1) bar.right = bar.left + 1;
				dc.FillSolidRect(bar, (style == StyleSpan) ? fill : fillSoft);
				dc.FillSolidRect(x - hw, y, barW, (style == StyleSpan) ? 1 : 2, col);
				if (drawPeak && style == StyleSpan) {
					dc.FillSolidRect(peakPts[k].x - 1, peakPts[k].y, 3, 2,
						RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
				}
			}
			if (drawPeak && style == StyleAbleton) {
				for (int b = binLo; b <= binHi; b += 2) {
					const int k = b - binLo;
					dc.FillSolidRect(peakPts[k].x - 1, peakPts[k].y, 3, 2,
						RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
				}
			}
			else if (drawPeak && style == StyleBars) {
				for (int k = 0; k < nPts; ++k) {
					dc.FillSolidRect(peakPts[k].x - 1, peakPts[k].y, 3, 2,
						RGB((GetRValue(col) + 255) / 2, (GetGValue(col) + 255) / 2, (GetBValue(col) + 255) / 2));
				}
			}
		}
		else if (isFill) {
			CBrush br((style == StyleCubase) ? fillCubase
				: (style == StyleFabFilter) ? fillSoft : fill);
			CBrush* oldBr = dc.SelectObject(&br);
			dc.SelectStockObject(NULL_PEN);
			dc.SetPolyFillMode(WINDING);
			dc.Polygon(fillPts, nPts + 2);
			dc.SelectObject(oldBr);
		}

		if (drawPeak && !isBars) {
			CPen peakPen(PS_SOLID, 1, RGB(
				(GetRValue(col) * 2 + 255) / 3,
				(GetGValue(col) * 2 + 255) / 3,
				(GetBValue(col) * 2 + 255) / 3));
			CPen* prev = dc.SelectObject(&peakPen);
			dc.Polyline(peakPts, nPts);
			dc.SelectObject(prev ? prev : oldPen);
		}

		if (isLine) {
			const int penW = (style == StyleFabFilter) ? 1
				: (style == StyleCubase) ? 2 : 2;
			CPen curve(PS_SOLID, penW, col);
			CPen* prev = dc.SelectObject(&curve);
			dc.Polyline(linePts, nPts);
			dc.SelectObject(prev ? prev : oldPen);
		}

		dc.SetTextColor(col);
		dc.TextOut(plot.right - 28, plot.top + 2 + i * 12, ChannelLabel(c, channels));
	}

	if (diff) {
		// 0dB 基準線(差分表示であることを明示)
		dc.FillSolidRect(plot.left, base0, plot.Width(), 1, RGB(120, 130, 160));
		if (drawTitle) {
			dc.SetTextColor(RGB(200, 190, 140));
			dc.TextOut(plot.left + 34, plot.top + 2,
				LL14(L"差分", L"Diff", L"Diff.", L"Diff.", L"Dif.", L"차분", L"差分", L"فرق",
					L"Разн.", L"Diff.", L"Dif.", L"Versch.", L"Roznica", L"Fark"));
		}
	}

	if (m_eqOverlay)
		DrawEqOverlay(dc, plot, nyquist);
	DrawFreqMarkers(dc, plot, nyquist);

	dc.SelectObject(oldPen);
}

void CAnalyzerDlg::DrawFreqMarkers(CDC& dc, const CRect& plot, float nyquist)
{
	if (plot.Width() < 8 || plot.Height() < 8) return;
	float fMinZ = 20.0f, fMaxZ = nyquist;
	GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);

	CPen mk(PS_DOT, 1, RGB(255, 170, 90));
	CPen* oldPen = dc.SelectObject(&mk);
	int shown = 0;
	for (int i = 0; i < MARKER_MAX; ++i) {
		const int hz = savedata.analyzermarkers[i];
		if (hz <= 0) continue;
		const float f = (float)hz;
		if (f < fMinZ || f > fMaxZ || f >= nyquist) continue;
		const int x = (int)HzToPlotX(f, plot, fMinZ, fMaxZ);
		dc.MoveTo(x, plot.top);
		dc.LineTo(x, plot.bottom);
		if (plot.Height() > 40) {
			CString lab;
			if (hz >= 1000) lab.Format(_T("%.2fk"), (double)hz / 1000.0);
			else lab.Format(_T("%d"), hz);
			dc.SetTextColor(RGB(255, 190, 120));
			dc.TextOut((std::min)((long)x + 2, plot.right - 34),
				plot.top + 14 + (shown % 3) * 12, lab);
		}
		++shown;
	}
	dc.SelectObject(oldPen);
}

void CAnalyzerDlg::RedrawSpectrum(COLORREF bg)
{
	if (!m_specDC.GetSafeHdc() || m_specW <= 0 || m_specH <= 0) return;
	m_specDC.FillSolidRect(0, 0, m_specW, m_specH, bg);

	int channels = 2, sr = 44100, layout = SpecOverlay;
	float spec[CH_MAX][SPEC_BINS], peak[CH_MAX][SPEC_BINS];
	EnterCriticalSection(&m_cs);
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	layout = m_specLayout;
	memcpy(spec, m_specDb, sizeof(spec));
	memcpy(peak, m_specPeakDb, sizeof(peak));
	LeaveCriticalSection(&m_cs);

	CFont* oldFont = m_specDC.SelectObject(&m_font);
	m_specDC.SetBkMode(TRANSPARENT);

	const int gap = 4;
	const int padL = 4, padR = 4, padT = 16, padB = 4;
	CRect area(padL, padT, m_specW - padR, m_specH - padB);

	m_hoverPlotCount = 0;
	auto addHoverPlot = [&](const CRect& plot, int ch) {
		if (m_hoverPlotCount >= HOVER_PLOT_MAX) return;
		m_hoverPlots[m_hoverPlotCount] = plot;
		m_hoverPlotCh[m_hoverPlotCount] = ch;
		++m_hoverPlotCount;
	};

	const float nyquist = (float)sr * 0.5f;

	// --- 位相スコープ(リサージュ) ---
	if (m_lowerMode == LowerPhase) {
		CRect plot(area.left + 4, area.top, area.right - 4, area.bottom - 4);
		// ホバー表は空にしない(他処理の前提)。位相中はホバー読取自体を抑止する。
		addHoverPlot(plot, 0);
		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"位相スコープ (L/R)", L"Phase scope (L/R)", L"Scope de phase (L/R)", L"Scope di fase (L/R)",
				L"Osciloscopio de fase (L/R)", L"위상 스코프 (L/R)", L"相位示波器 (L/R)", L"راسم الطور (L/R)",
				L"Фазовый скоп (L/R)", L"Phasenskop (L/R)", L"Escopo de fase (L/R)", L"Fasescoop (L/R)",
				L"Skop fazy (L/R)", L"Faz skopu (L/R)"));

		const int side = (std::min)(plot.Width(), plot.Height());
		CRect box(plot.left + (plot.Width() - side) / 2, plot.top + (plot.Height() - side) / 2,
			plot.left + (plot.Width() - side) / 2 + side, plot.top + (plot.Height() - side) / 2 + side);
		CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
		CPen* oldPen = m_specDC.SelectObject(&frame);
		m_specDC.SelectStockObject(NULL_BRUSH);
		m_specDC.Rectangle(box);
		m_specDC.SelectObject(oldPen);

		if (channels < 2 || side < 24) {
			m_specDC.SetTextColor(RGB(200, 180, 140));
			m_specDC.TextOut(plot.left + 8, plot.top + 8,
				LL14(L"ステレオ音源でのみ表示されます。", L"Shown for stereo sources only.",
					L"Affiche uniquement pour les sources stereo.", L"Mostrato solo per sorgenti stereo.",
					L"Solo se muestra con fuentes estereo.", L"스테레오 음원에서만 표시됩니다.",
					L"仅在立体声音源时显示。", L"يظهر لمصادر ستيريو فقط.",
					L"Показывается только для стерео.", L"Nur bei Stereoquellen sichtbar.",
					L"Exibido apenas para fontes estereo.", L"Alleen bij stereobronnen.",
					L"Widoczne tylko dla zrodel stereo.", L"Sadece stereo kaynaklarda gosterilir."));
		}
		else {
			int wr = 0, wf = 0, wch = 0;
			if (SnapshotRing(wr, wf, wch) && wf >= 64) {
				const int cx = (box.left + box.right) / 2;
				const int cy = (box.top + box.bottom) / 2;
				const int r = side / 2 - 2;
				// 目安の十字(上=同相 / 横=逆相方向)
				CPen ax(PS_DOT, 1, RGB(50, 56, 72));
				CPen* prevAx = m_specDC.SelectObject(&ax);
				m_specDC.MoveTo(cx, box.top); m_specDC.LineTo(cx, box.bottom);
				m_specDC.MoveTo(box.left, cy); m_specDC.LineTo(box.right, cy);
				m_specDC.SelectObject(prevAx);

				const int useN = (std::min)(wf, 2048);
				const int step = (std::max)(1, useN / 512);
				POINT pts[513];
				int n = 0;
				for (int i = 0; i < useN && n < 512; i += step) {
					int idx = wr - useN + i;
					while (idx < 0) idx += RING_SAMPLES;
					idx %= RING_SAMPLES;
					float l = m_ringSnap[0][idx];
					float rr = m_ringSnap[1][idx];
					if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
					if (rr > 1.0f) rr = 1.0f; if (rr < -1.0f) rr = -1.0f;
					// 45度回転: 上=モノラル(同相)、左右=広がり
					const float mx = (l + rr) * 0.7071f;
					const float my = (l - rr) * 0.7071f;
					pts[n].x = cx + (int)(my * r);
					pts[n].y = cy - (int)(mx * r);
					++n;
				}
				if (n >= 2) {
					CPen liss(PS_SOLID, 1, RGB(120, 230, 170));
					CPen* prev = m_specDC.SelectObject(&liss);
					m_specDC.Polyline(pts, n);
					m_specDC.SelectObject(prev);
				}
			}
		}
		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	// --- 密度ゴニオメータ ---
	if (m_lowerMode == LowerVector) {
		CRect plot(area.left + 4, area.top, area.right - 4, area.bottom - 4);
		addHoverPlot(plot, 0);
		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"ゴニオメータ (密度)", L"Goniometer (density)", L"Goniometre (densite)", L"Goniometro (densita)",
				L"Goniometro (densidad)", L"고니오미터 (밀도)", L"测向仪（密度）", L"مقياس الاتجاه (كثافة)",
				L"Гониометр (плотность)", L"Goniometer (Dichte)", L"Goniometro (densidade)", L"Goniometer (dichtheid)",
				L"Goniometr (gestosc)", L"Gonyometre (yogunluk)"));
		const int side = (std::min)(plot.Width(), plot.Height());
		CRect box(plot.left + (plot.Width() - side) / 2, plot.top + (plot.Height() - side) / 2,
			plot.left + (plot.Width() - side) / 2 + side, plot.top + (plot.Height() - side) / 2 + side);
		for (int y = 0; y < GONIO_N; ++y)
			for (int x = 0; x < GONIO_N; ++x)
				m_gonio[y][x] = (unsigned char)((m_gonio[y][x] * 13) / 16);
		if (channels >= 2) {
			int wr = 0, wf = 0, wch = 0;
			if (SnapshotRing(wr, wf, wch) && wf >= 64) {
				const int useN = (std::min)(wf, 2048);
				const int step = (std::max)(1, useN / 800);
				for (int i = 0; i < useN; i += step) {
					int idx = wr - useN + i;
					while (idx < 0) idx += RING_SAMPLES;
					idx %= RING_SAMPLES;
					float l = m_ringSnap[0][idx];
					float rr = m_ringSnap[1][idx];
					if (l > 1.f) l = 1.f; if (l < -1.f) l = -1.f;
					if (rr > 1.f) rr = 1.f; if (rr < -1.f) rr = -1.f;
					const float mx = (l + rr) * 0.7071f;
					const float my = (l - rr) * 0.7071f;
					int gx = (int)((my * 0.5f + 0.5f) * (GONIO_N - 1));
					int gy = (int)((0.5f - mx * 0.5f) * (GONIO_N - 1));
					if (gx < 0) gx = 0; if (gx >= GONIO_N) gx = GONIO_N - 1;
					if (gy < 0) gy = 0; if (gy >= GONIO_N) gy = GONIO_N - 1;
					unsigned int nv = (unsigned int)m_gonio[gy][gx] + 18u;
					if (nv > 255u) nv = 255u;
					m_gonio[gy][gx] = (unsigned char)nv;
				}
			}
		}
		static DWORD imgG[GONIO_N * GONIO_N];
		for (int y = 0; y < GONIO_N; ++y) {
			for (int x = 0; x < GONIO_N; ++x) {
				const int v = m_gonio[y][x];
				DWORD c = 0;
				if (v > 0) {
					const int r = (std::min)(255, 20 + v);
					const int g = (std::min)(255, 40 + v * 2);
					const int b = (std::min)(255, 30 + v / 2);
					c = ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
				}
				imgG[y * GONIO_N + x] = c;
			}
		}
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = GONIO_N;
		bi.bmiHeader.biHeight = -GONIO_N;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		m_specDC.FillSolidRect(box, RGB(12, 14, 20));
		const int oldMode = m_specDC.SetStretchBltMode(COLORONCOLOR);
		::StretchDIBits(m_specDC.GetSafeHdc(), box.left, box.top, box.Width(), box.Height(),
			0, 0, GONIO_N, GONIO_N, imgG, &bi, DIB_RGB_COLORS, SRCCOPY);
		m_specDC.SetStretchBltMode(oldMode);
		CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
		CPen* oldPen = m_specDC.SelectObject(&frame);
		m_specDC.SelectStockObject(NULL_BRUSH);
		m_specDC.Rectangle(box);
		const int cx = (box.left + box.right) / 2;
		const int cy = (box.top + box.bottom) / 2;
		CPen ax(PS_DOT, 1, RGB(50, 56, 72));
		CPen* prevAx = m_specDC.SelectObject(&ax);
		m_specDC.MoveTo(cx, box.top); m_specDC.LineTo(cx, box.bottom);
		m_specDC.MoveTo(box.left, cy); m_specDC.LineTo(box.right, cy);
		m_specDC.SelectObject(prevAx);
		m_specDC.SelectObject(oldPen);
		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	// --- 1/3 オクターブ RTA ---
	if (m_lowerMode == LowerRta) {
		static const float kRtaHz[31] = {
			20.f, 25.f, 31.5f, 40.f, 50.f, 63.f, 80.f, 100.f, 125.f, 160.f,
			200.f, 250.f, 315.f, 400.f, 500.f, 630.f, 800.f, 1000.f, 1250.f, 1600.f,
			2000.f, 2500.f, 3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f, 20000.f
		};
		CRect plot(area.left + 28, area.top, area.right, area.bottom - 14);
		addHoverPlot(plot, -1);
		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			LL14(L"1/3 オクターブ RTA", L"1/3-octave RTA", L"RTA 1/3 d'octave", L"RTA 1/3 d'ottava",
				L"RTA de 1/3 de octava", L"1/3 옥타브 RTA", L"1/3 倍频程 RTA", L"RTA ثلث أوكتاف",
				L"RTA 1/3 октавы", L"1/3-Oktav-RTA", L"RTA 1/3 de oitava", L"1/3-octaaf-RTA",
				L"RTA 1/3 oktawy", L"1/3 oktav RTA"));
		CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
		CPen* oldPen = m_specDC.SelectObject(&frame);
		m_specDC.SelectStockObject(NULL_BRUSH);
		m_specDC.Rectangle(plot);
		m_specDC.SelectObject(oldPen);
		float rtaDb[31];
		for (int i = 0; i < 31; ++i) {
			const float fC = kRtaHz[i];
			if (fC >= nyquist) { rtaDb[i] = -96.f; continue; }
			const float fLo = fC / 1.122462f;
			const float fHi = fC * 1.122462f;
			float best = -96.f;
			for (int b = 0; b < SPEC_BINS; ++b) {
				const float hz = SpecCenterHz(b, SPEC_BINS, nyquist);
				if (hz < fLo || hz > fHi) continue;
				for (int c = 0; c < channels; ++c)
					if (spec[c][b] > best) best = spec[c][b];
			}
			rtaDb[i] = best;
		}
		const int nBar = 31;
		const int gapB = 2;
		const int innerW = plot.Width() - 8;
		const int bw = (innerW - gapB * (nBar - 1)) / nBar;
		for (int i = 0; i < nBar; ++i) {
			float t = (rtaDb[i] + 96.f) / 96.f;
			if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
			const int x0 = plot.left + 4 + i * (bw + gapB);
			const int h = (int)(t * (plot.Height() - 8));
			COLORREF col = RGB(80, 180, 220);
			if (rtaDb[i] > -9.f) col = RGB(255, 200, 80);
			if (rtaDb[i] > -3.f) col = RGB(255, 90, 80);
			if (h > 0)
				m_specDC.FillSolidRect(x0, plot.bottom - 4 - h, (std::max)(1, bw), h, col);
			if (bw >= 10 && (i == 7 || i == 17 || i == 27)) {
				CString lab;
				if (kRtaHz[i] >= 1000.f) lab.Format(_T("%.0fk"), kRtaHz[i] / 1000.f);
				else lab.Format(_T("%.0f"), kRtaHz[i]);
				m_specDC.SetTextColor(RGB(140, 150, 170));
				m_specDC.TextOut(x0, plot.bottom - 13, lab);
			}
		}
		if (m_eqOverlay)
			DrawEqOverlay(m_specDC, plot, nyquist);
		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	// --- 帯域相関 / 帯域位相差 ---
	if (m_lowerMode == LowerCorrFreq || m_lowerMode == LowerPhaseFreq) {
		const bool isCorr = (m_lowerMode == LowerCorrFreq);
		CRect plot(area.left + 28, area.top, area.right, area.bottom - 12);
		addHoverPlot(plot, 0);
		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(plot.left + 2, (int)(std::max)(0L, plot.top - 13),
			isCorr
			? LL14(L"帯域相関 (L/R)", L"Correlation vs frequency (L/R)", L"Correlation par frequence (L/R)", L"Correlazione per frequenza (L/R)",
				L"Correlacion por frecuencia (L/R)", L"대역 상관 (L/R)", L"频段相关 (L/R)", L"ترابط حسب التردد (L/R)",
				L"Корреляция по частоте (L/R)", L"Korrelation vs. Frequenz (L/R)", L"Correlacao por frequencia (L/R)", L"Correlatie vs frequentie (L/R)",
				L"Korelacja vs czestotliwosc (L/R)", L"Frekansa gore korelasyon (L/R)")
			: LL14(L"帯域位相差 (L/R)", L"Phase vs frequency (L/R)", L"Phase par frequence (L/R)", L"Fase per frequenza (L/R)",
				L"Fase por frecuencia (L/R)", L"대역 위상차 (L/R)", L"频段相位差 (L/R)", L"طور حسب التردد (L/R)",
				L"Фаза по частоте (L/R)", L"Phase vs. Frequenz (L/R)", L"Fase por frequencia (L/R)", L"Fase vs frequentie (L/R)",
				L"Faza vs czestotliwosc (L/R)", L"Frekansa gore faz (L/R)"));
		CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
		CPen* oldPen = m_specDC.SelectObject(&frame);
		m_specDC.SelectStockObject(NULL_BRUSH);
		m_specDC.Rectangle(plot);
		m_specDC.SelectObject(oldPen);
		const int midY = (plot.top + plot.bottom) / 2;
		CPen zero(PS_DOT, 1, RGB(70, 80, 100));
		oldPen = m_specDC.SelectObject(&zero);
		m_specDC.MoveTo(plot.left, midY);
		m_specDC.LineTo(plot.right, midY);
		m_specDC.SelectObject(oldPen);
		float localC[SPEC_BINS], localP[SPEC_BINS];
		EnterCriticalSection(&m_cs);
		memcpy(localC, m_specCorr, sizeof(localC));
		memcpy(localP, m_specPhase, sizeof(localP));
		LeaveCriticalSection(&m_cs);
		float fMinZ = 20.0f, fMaxZ = nyquist;
		GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);
		POINT pts[SPEC_BINS];
		int nPts = 0;
		for (int b = 0; b < SPEC_BINS; ++b) {
			const float hz = SpecCenterHz(b, SPEC_BINS, nyquist);
			if (hz < fMinZ || hz > fMaxZ) continue;
			const float tx = logf((std::max)(hz, fMinZ) / fMinZ) / logf(fMaxZ / fMinZ);
			float ty;
			if (isCorr)
				ty = (localC[b] + 1.f) * 0.5f;
			else
				ty = (localP[b] / 3.14159265f + 1.f) * 0.5f;
			if (ty < 0.f) ty = 0.f; if (ty > 1.f) ty = 1.f;
			pts[nPts].x = plot.left + (int)(tx * plot.Width());
			pts[nPts].y = plot.bottom - (int)(ty * plot.Height());
			const int y0 = midY;
			const int y1 = pts[nPts].y;
			const int top = (y0 < y1) ? y0 : y1;
			const int bot = (y0 < y1) ? y1 : y0;
			COLORREF fill = isCorr
				? ((localC[b] >= 0.f) ? RGB(40, 90, 70) : RGB(90, 40, 50))
				: RGB(50, 60, 90);
			if (bot > top)
				m_specDC.FillSolidRect(pts[nPts].x, top, 1, bot - top, fill);
			++nPts;
		}
		if (nPts >= 2) {
			CPen ln(PS_SOLID, 1, isCorr ? RGB(120, 230, 170) : RGB(160, 190, 255));
			CPen* prev = m_specDC.SelectObject(&ln);
			m_specDC.Polyline(pts, nPts);
			m_specDC.SelectObject(prev);
		}
		DrawFreqMarkers(m_specDC, plot, nyquist);
		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	// --- スペクトログラム(ウォーターフォール) ---
	if (m_lowerMode == LowerWaterfall) {
		int wfWrite = 0, wfFilled = 0;
		EnterCriticalSection(&m_cs);
		wfWrite = m_wfWrite;
		wfFilled = m_wfFilled;
		LeaveCriticalSection(&m_cs);

		float fMinZ = 20.0f, fMaxZ = nyquist;
		GetZoomFreqRange(m_freqZoom, nyquist, fMinZ, fMaxZ);
		int binLo = 0, binHi = SPEC_BINS - 1;
		if (m_freqZoom != ZoomFull) {
			binLo = SPEC_BINS; binHi = -1;
			for (int b = 0; b < SPEC_BINS; ++b) {
				const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
				if (cHz < fMinZ || cHz > fMaxZ) continue;
				if (b < binLo) binLo = b;
				if (b > binHi) binHi = b;
			}
			if (binHi < binLo) { binLo = 0; binHi = SPEC_BINS - 1; }
		}
		const int imgW = binHi - binLo + 1;

		// 小さな画像(bins x WF_ROWS)を作って一括 StretchDIBits(1ピクセルずつ塗ると重い)。
		// 行は imgW 個ずつ詰める(DIB の行ストライド = imgW*4)。
		static DWORD img[WF_ROWS * SPEC_BINS];
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = imgW;
		bi.bmiHeader.biHeight = -WF_ROWS; // トップダウン(先頭行=最新)
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		auto drawWf = [&](const CRect& plot, int ch) {
			if (plot.Width() < 8 || plot.Height() < 8) return;
			if (ch < 0) ch = 0;
			if (ch >= CH_MAX) ch = CH_MAX - 1;
			EnterCriticalSection(&m_cs);
			for (int r = 0; r < WF_ROWS; ++r) {
				const int src = ((wfWrite - 1 - r) % WF_ROWS + WF_ROWS) % WF_ROWS;
				const bool valid = (r < wfFilled);
				for (int b = binLo; b <= binHi; ++b) {
					const float db = valid ? m_wfHist[ch][src][b] : -96.0f;
					const COLORREF c = WaterfallColor(db);
					// DIB は BGRX 順 → little endian の DWORD では 0x00RRGGBB
					img[r * imgW + (b - binLo)] = ((DWORD)GetRValue(c) << 16)
						| ((DWORD)GetGValue(c) << 8) | (DWORD)GetBValue(c);
				}
			}
			LeaveCriticalSection(&m_cs);
			const int oldMode = m_specDC.SetStretchBltMode(COLORONCOLOR);
			::StretchDIBits(m_specDC.GetSafeHdc(),
				plot.left, plot.top, plot.Width(), plot.Height(),
				0, 0, imgW, WF_ROWS, img, &bi, DIB_RGB_COLORS, SRCCOPY);
			m_specDC.SetStretchBltMode(oldMode);

			CPen frame(PS_SOLID, 1, RGB(55, 60, 78));
			CPen* oldPen = m_specDC.SelectObject(&frame);
			m_specDC.SelectStockObject(NULL_BRUSH);
			m_specDC.Rectangle(plot);
			m_specDC.SelectObject(oldPen);
			if (m_eqOverlay)
				DrawEqOverlay(m_specDC, plot, nyquist);
			DrawFreqMarkers(m_specDC, plot, nyquist);
			m_specDC.SetTextColor(kChColor[ch]);
			m_specDC.TextOut(plot.right - 28, plot.top + 2, ChannelLabel(ch, channels));
		};

		m_specDC.SetTextColor(RGB(180, 190, 210));
		m_specDC.TextOut(area.left + 2, (int)(std::max)(0L, area.top - 13),
			LL14(L"スペクトログラム", L"Spectrogram", L"Spectrogramme", L"Spettrogramma",
				L"Espectrograma", L"스펙트로그램", L"频谱图", L"مخطط طيفي",
				L"Спектрограмма", L"Spektrogramm", L"Espectrograma", L"Spectrogram",
				L"Spektrogram", L"Spektrogram"));

		if (layout == SpecOverlay || channels <= 1) {
			// 重ね描きできないので先頭chのみ
			CRect plot(area.left + 4, area.top, area.right, area.bottom - 4);
			addHoverPlot(plot, 0);
			drawWf(plot, 0);
		}
		else if (layout == SpecSplitV) {
			const int n = channels;
			const int cellH = (area.Height() - gap * (n - 1)) / n;
			for (int i = 0; i < n; ++i) {
				CRect plot(area.left + 4, area.top + i * (cellH + gap),
					area.right, area.top + i * (cellH + gap) + cellH);
				addHoverPlot(plot, i);
				drawWf(plot, i);
			}
		}
		else if (layout == SpecSplitH) {
			const int n = channels;
			const int cellW = (area.Width() - gap * (n - 1)) / n;
			for (int i = 0; i < n; ++i) {
				CRect plot(area.left + i * (cellW + gap), area.top,
					area.left + i * (cellW + gap) + cellW, area.bottom - 4);
				addHoverPlot(plot, i);
				drawWf(plot, i);
			}
		}
		else {
			const int cols = (layout == SpecGrid4) ? 2 : 4;
			const int maxCell = (layout == SpecGrid4) ? 4 : 8;
			const int cellW = (area.Width() - gap * (cols - 1)) / cols;
			const int cellH = (area.Height() - gap) / 2;
			for (int i = 0; i < maxCell; ++i) {
				if (i >= channels) break;
				const int r = i / cols, c = i % cols;
				CRect plot(area.left + c * (cellW + gap), area.top + r * (cellH + gap),
					area.left + c * (cellW + gap) + cellW, area.top + r * (cellH + gap) + cellH);
				addHoverPlot(plot, i);
				drawWf(plot, i);
			}
		}

		m_specDC.SelectObject(oldFont);
		m_specReady = true;
		m_specDirty = false;
		return;
	}

	if (layout == SpecOverlay || channels <= 1) {
		CRect plot(area.left + 28, area.top, area.right, area.bottom - 12);
		// 重ね描き: ホバーは全chから最大dBのチャンネルを選ぶ(UpdateHover側)
		addHoverPlot(plot, -1);
		DrawSpecPanel(m_specDC, plot, 0, channels, spec, peak, channels, sr, true);
	}
	else if (layout == SpecSplitV) {
		const int n = channels;
		const int cellH = (area.Height() - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i) {
			CRect plot(area.left + 20, area.top + i * (cellH + gap),
				area.right, area.top + i * (cellH + gap) + cellH);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}
	else if (layout == SpecSplitH) {
		const int n = channels;
		const int cellW = (area.Width() - gap * (n - 1)) / n;
		for (int i = 0; i < n; ++i) {
			CRect plot(area.left + i * (cellW + gap), area.top,
				area.left + i * (cellW + gap) + cellW, area.bottom - 4);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}
	else if (layout == SpecGrid4) {
		const int cols = 2, rows = 2;
		const int cellW = (area.Width() - gap) / cols;
		const int cellH = (area.Height() - gap) / rows;
		for (int i = 0; i < 4; ++i) {
			if (i >= channels) break;
			const int r = i / cols, c = i % cols;
			CRect plot(area.left + c * (cellW + gap), area.top + r * (cellH + gap),
				area.left + c * (cellW + gap) + cellW, area.top + r * (cellH + gap) + cellH);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}
	else { // SpecGrid8
		const int cols = 4, rows = 2;
		const int cellW = (area.Width() - gap * (cols - 1)) / cols;
		const int cellH = (area.Height() - gap) / rows;
		for (int i = 0; i < 8; ++i) {
			if (i >= channels) break;
			const int r = i / cols, c = i % cols;
			CRect plot(area.left + c * (cellW + gap), area.top + r * (cellH + gap),
				area.left + c * (cellW + gap) + cellW, area.top + r * (cellH + gap) + cellH);
			addHoverPlot(plot, i);
			DrawSpecPanel(m_specDC, plot, i, 1, spec, peak, channels, sr, i == 0);
		}
	}

	// アクティブな m_hoverPlot は UpdateHoverFromPoint がカーソルで決める(ここで [0]=L に戻さない)

	m_specDC.SelectObject(oldFont);
	m_specReady = true;
	m_specDirty = false;
}

// メーター帯の寸法。描画とクロマ更新幅で同じ値を使う。
static void AnalyzerMeterGeom(int channels, int& n, int& meterW, int& gap, int& totalW)
{
	n = channels;
	if (n < 1) n = 1;
	if (n > CAnalyzerDlg::CH_MAX) n = CAnalyzerDlg::CH_MAX;
	// ch 数が増えるほど細く詰める(右端ストリップが波形を潰さない範囲に収める)
	meterW = (n <= 2) ? 10 : (n <= 4 ? 8 : 6);
	gap = (n <= 4) ? 3 : 2;
	totalW = n * meterW + (n - 1) * gap + 8;
}

void CAnalyzerDlg::DrawLevelMeters(CDC& dc, const CRect& waveRc, COLORREF bg)
{
	// bg: アクリル時は CHROMA 以外の不透明色で塗る(キー色だと透過して消える)
	const COLORREF panelBg = (bg == ANALYZER_CHROMA_KEY) ? RGB(22, 26, 36) : RGB(12, 14, 20);
	// アクリル用ストリップは ~39px。40 未満で return するとバーごと消える。
	if (waveRc.Width() < 28 || waveRc.Height() < 40) return;

	float hold[CH_MAX] = {}, rms[CH_MAX] = {};
	int channels = 2;
	EnterCriticalSection(&m_cs);
	for (int c = 0; c < CH_MAX; ++c) {
		hold[c] = m_meterHold[c];
		rms[c] = m_meterRms[c];
	}
	channels = m_channels;
	LeaveCriticalSection(&m_cs);

	int n = 1, meterW = 10, gap = 3, totalW = 0;
	AnalyzerMeterGeom(channels, n, meterW, gap, totalW);
	const bool showLr = (m_showLevelMeter != false);
	const bool showCorr = (savedata.pro_corr_meter != 0 && n >= 2);
	if (!showLr && !showCorr) return;

	// LR バーは右端。消したときはその位置へ φ を移す。両方出すときは φ を LR の左へ。
	int rightEdge = waveRc.right - 4;
	CRect area(0, 0, 0, 0);
	if (showLr) {
		area.SetRect(rightEdge - totalW, waveRc.top + 8, rightEdge, waveRc.bottom - 8);
		dc.FillSolidRect(area, panelBg);
		rightEdge = area.left - 4;
	}

	auto ampToY = [&](float a) -> int {
		float db = AmpToDb(a);
		float t = (db + 72.0f) / 72.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		return area.bottom - (int)(t * area.Height());
	};

	// 位相相関メーター(-1..+1)と L/R バランス。
	if (showCorr) {
		const float corr = ProAudio_CorrValue();
		const float bal = ProAudio_CorrBalance();
		m_corrHist[m_corrHistHead & 63] = corr;
		m_corrHistHead++;
		const int corrW = 22;
		const int sparkW = 18;
		CRect corrRc(rightEdge - corrW, waveRc.top + 10, rightEdge, waveRc.bottom - 10);
		CRect sparkRc(corrRc.left - sparkW - 2, corrRc.top, corrRc.left - 2, corrRc.bottom);
		if (sparkRc.left < waveRc.left + 2) {
			const int dx = waveRc.left + 2 - sparkRc.left;
			sparkRc.OffsetRect(dx, 0);
			corrRc.OffsetRect(dx, 0);
		}
		if (corrRc.Width() >= 12) {
			if (!showLr) {
				const int slotL = (sparkRc.left > 2) ? (sparkRc.left - 2) : waveRc.left;
				CRect slot(slotL, waveRc.top + 8, waveRc.right - 2, waveRc.bottom - 8);
				if (slot.Width() > 0 && slot.Height() > 0)
					dc.FillSolidRect(slot, panelBg);
			}
			dc.FillSolidRect(corrRc, RGB(28, 32, 44));
			dc.Draw3dRect(corrRc, RGB(70, 80, 100), RGB(40, 45, 60));
			const int midY = (corrRc.top + corrRc.bottom) / 2;
			dc.FillSolidRect(corrRc.left + 2, midY, corrRc.Width() - 4, 1, RGB(90, 100, 120));
			int y = midY - (int)(corr * ((corrRc.Height() / 2) - 5));
			if (y < corrRc.top + 3) y = corrRc.top + 3;
			if (y > corrRc.bottom - 4) y = corrRc.bottom - 4;
			dc.FillSolidRect(corrRc.left + 3, y - 2, corrRc.Width() - 6, 5, RGB(100, 230, 150));
			int bx = corrRc.left + corrRc.Width() / 2 + (int)(bal * (corrRc.Width() / 2 - 3));
			if (bx < corrRc.left + 2) bx = corrRc.left + 2;
			if (bx > corrRc.right - 3) bx = corrRc.right - 3;
			dc.FillSolidRect(bx - 1, corrRc.bottom - 5, 3, 4, RGB(255, 180, 80));
			dc.SetTextColor(RGB(170, 200, 190));
			dc.SetBkMode(TRANSPARENT);
			CFont* of2 = dc.SelectObject(&m_font);
			dc.TextOut(corrRc.left + 2, corrRc.top, _T("φ"));
			dc.SelectObject(of2);

			const int sn = m_corrHistHead < 64 ? m_corrHistHead : 64;
			if (sn >= 4 && sparkRc.Width() >= 12 && sparkRc.right <= corrRc.left) {
				dc.FillSolidRect(sparkRc, RGB(22, 26, 36));
				const int mid = (sparkRc.top + sparkRc.bottom) / 2;
				dc.FillSolidRect(sparkRc.left, mid, sparkRc.Width(), 1, RGB(60, 70, 90));
				for (int i = 0; i < sn && i < sparkRc.Width() - 2; ++i) {
					const int idx = (m_corrHistHead - sn + i) & 63;
					float v = m_corrHist[idx];
					if (v > 1.f) v = 1.f; if (v < -1.f) v = -1.f;
					int yy = mid - (int)(v * ((sparkRc.Height() / 2) - 3));
					if (yy < sparkRc.top + 1) yy = sparkRc.top + 1;
					if (yy > sparkRc.bottom - 2) yy = sparkRc.bottom - 2;
					dc.SetPixel(sparkRc.left + 1 + i, yy, v >= 0 ? RGB(100, 220, 140) : RGB(220, 120, 100));
				}
			}
		}
	}

	if (!showLr) return;

	// すべて RMS 基準: バー=現在RMS、白線=RMSピークホールド、黄/赤=RMS閾値
	const float ampYellow = powf(10.0f, -9.0f / 20.0f);
	const float ampRed = powf(10.0f, -3.0f / 20.0f);
	const int yYellow = ampToY(ampYellow);
	const int yRed = ampToY(ampRed);

	for (int c = 0; c < n; ++c) {
		const int x0 = area.left + 4 + c * (meterW + gap);
		CRect track(x0, area.top + 2, x0 + meterW, area.bottom - 2);
		dc.FillSolidRect(track, RGB(36, 42, 56));

		const float aRms = (std::min)(1.0f, (std::max)(0.0f, rms[c]));
		const float aHold = (std::min)(1.0f, (std::max)(0.0f, hold[c]));
		const int yFill = ampToY(aRms);
		const int yHold = ampToY(aHold);

		if (yFill < track.bottom) {
			const int ySafeTop = (std::max)(yFill, yYellow);
			if (ySafeTop < track.bottom)
				dc.FillSolidRect(CRect(track.left, ySafeTop, track.right, track.bottom), kChColor[c]);
			if (yFill < yYellow) {
				const int yY0 = (std::max)(yFill, yRed);
				if (yY0 < yYellow)
					dc.FillSolidRect(CRect(track.left, yY0, track.right, yYellow), RGB(255, 200, 80));
			}
			if (yFill < yRed)
				dc.FillSolidRect(CRect(track.left, yFill, track.right, yRed), RGB(255, 80, 80));
		}
		if (yHold >= track.top && yHold < track.bottom)
			dc.FillSolidRect(track.left, yHold, track.Width(), 2, RGB(220, 230, 245));

		dc.SetTextColor(kChColor[c]);
		dc.SetBkMode(TRANSPARENT);
		CFont* of = dc.SelectObject(&m_font);
		dc.TextOut(x0, area.top - 2, ChannelLabel(c, channels));
		dc.SelectObject(of);
	}
}

// 波形右端のメーター帯幅(クロマ更新・波形プロット幅用)。
// showLr=0 でも相関メーターだけで帯を取る（LR を消しても φ は残す）。
static int AnalyzerMeterStripWidth(int channels, int showLr)
{
	int n = 1, meterW = 10, gap = 3, totalW = 0;
	AnalyzerMeterGeom(channels, n, meterW, gap, totalW);
	int w = 0;
	if (showLr)
		w += totalW + 16; // 余白多め(>=44 for stereo)
	if (savedata.pro_corr_meter && n >= 2)
		w += 28 + 20; // φ 相関帯 + 左スパーク
	if (w > 0 && w < 28)
		w = 28; // DrawLevelMeters の早期 return 下限以上
	return w;
}

static int AnalyzerWavePlotWidth(int waveW, int channels, int showLr)
{
	const int strip = AnalyzerMeterStripWidth(channels, showLr);
	int plot = waveW - strip;
	if (plot < 48)
		plot = waveW;
	return plot;
}

void CAnalyzerDlg::DrawTpLufsReadout(CDC& dc, const CRect& waveRc)
{
	if (waveRc.Width() < 120 || waveRc.Height() < 28) return;

	float tp = 0.0f, lufs = -70.0f;
	EnterCriticalSection(&m_cs);
	tp = (std::max)(m_tpHold, m_tpLin);
	lufs = m_lufsMom;
	LeaveCriticalSection(&m_cs);

	float tpDb = -99.9f;
	if (tp > 1e-6f) tpDb = 20.0f * log10f(tp);
	if (tpDb < -99.9f) tpDb = -99.9f;

	CString s;
	s.Format(_T("TP %.1f dBTP   LUFS %.1f  [%s]"), tpDb, lufs,
		LL14(L"簡易", L"approx", L"approx", L"approx", L"aprox", L"간이", L"简易", L"تقريبي",
			L"прибл.", L"ca.", L"aprox", L"ca.", L"ok.", L"yakl."));

	CFont* of = dc.SelectObject(&m_font);
	dc.SetBkMode(TRANSPARENT);
	const CSize sz = dc.GetTextExtent(s);
	// 左端は ch ラベル板、フリーズ表示があるときはさらに右へ寄せる
	const int x0 = waveRc.left + (m_frozen ? 96 : 34);
	CRect box(x0, waveRc.top + 2, x0 + sz.cx + 10, waveRc.top + 6 + sz.cy);
	if (box.right > waveRc.right - 4)
		box.right = waveRc.right - 4;
	if (box.Width() > 20) {
		// アクリルでも読めるよう不透明プレート(CHROMA キーでは塗らない)
		dc.FillSolidRect(box, ANALYZER_LABEL_PLATE);
		dc.SetTextColor(tpDb > -0.1f ? RGB(255, 150, 130) : RGB(200, 215, 235));
		dc.TextOut(box.left + 5, box.top + 1, s);
	}
	else {
		box.SetRectEmpty();
	}
	dc.SelectObject(of);

	// ツールチップ矩形を実描画位置へ合わせる(簡易計測である旨を出す)。
	// 毎フレーム送らないよう、変化したときだけ更新する。
	if (box != m_tpLufsTipRc) {
		m_tpLufsTipRc = box;
		if (m_tooltip.GetSafeHwnd() && !m_tpLufsTipRc.IsRectEmpty())
			m_tooltip.SetToolRect(this, 1, &m_tpLufsTipRc);
	}
}

void CAnalyzerDlg::DrawHoverReadout(CDC& dc, const CRect& clientRc)
{
	UNREFERENCED_PARAMETER(clientRc);
	if (!m_hoverValid || m_hoverPlot.IsRectEmpty()) return;

	CRect plot = m_hoverPlot;
	plot.OffsetRect(0, m_hoverSplitY);
	if (plot.Width() < 8 || plot.Height() < 8) return;

	int sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	const float nyquist = (float)sr * 0.5f;
	float fMin = 20.0f, fMax = (std::max)(20.2f, nyquist);
	GetZoomFreqRange(m_freqZoom, nyquist, fMin, fMax);
	float tx = logf((std::max)(m_hoverHz, fMin) / fMin) / logf(fMax / fMin);
	if (tx < 0.0f) tx = 0.0f;
	if (tx > 1.0f) tx = 1.0f;
	// 差分表示中は縦軸が ±48dB(0dB=中央)。読取点も同じ軸に載せる。
	float ty;
	if (m_specDiff && m_specSnapValid && m_hoverBin >= 0 && m_hoverCh >= 0 && m_hoverCh < CH_MAX
		&& m_lowerMode != LowerCorrFreq && m_lowerMode != LowerPhaseFreq)
		ty = ((m_hoverDb - m_specSnapDb[m_hoverCh][m_hoverBin]) + 48.0f) / 96.0f;
	else if (m_lowerMode == LowerCorrFreq)
		ty = (m_hoverDb + 1.0f) * 0.5f;
	else if (m_lowerMode == LowerPhaseFreq)
		ty = (m_hoverDb + 180.0f) / 360.0f;
	else
		ty = (m_hoverDb + 96.0f) / 96.0f;
	if (ty < 0.0f) ty = 0.0f;
	if (ty > 1.0f) ty = 1.0f;
	const int cx = plot.left + (int)(tx * plot.Width());
	const int cy = plot.bottom - (int)(ty * plot.Height());

	CPen cross(PS_DOT, 1, RGB(180, 190, 210));
	CPen* old = dc.SelectObject(&cross);
	dc.MoveTo(cx, plot.top);
	dc.LineTo(cx, plot.bottom);
	dc.MoveTo(plot.left, cy);
	dc.LineTo(plot.right, cy);
	dc.SelectObject(old);
	dc.FillSolidRect(cx - 2, cy - 2, 5, 5, RGB(255, 230, 120));

	CString s;
	const int channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	if (m_lowerMode == LowerCorrFreq) {
		if (m_hoverHz >= 1000.0f)
			s.Format(_T("%s  %.2f kHz  φ %.2f"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
		else
			s.Format(_T("%s  %.0f Hz  φ %.2f"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
	}
	else if (m_lowerMode == LowerPhaseFreq) {
		if (m_hoverHz >= 1000.0f)
			s.Format(_T("%s  %.2f kHz  %.0f deg"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
		else
			s.Format(_T("%s  %.0f Hz  %.0f deg"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
	}
	else if (m_hoverHz >= 1000.0f)
		s.Format(_T("%s  %.2f kHz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
	else
		s.Format(_T("%s  %.0f Hz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
	if (m_frozen)
		s += _T("  [FREEZE]");

	CSize sz = dc.GetTextExtent(s);
	CRect box(plot.left + 8, plot.top + 8, plot.left + 16 + sz.cx, plot.top + 16 + sz.cy);
	if (box.right > plot.right - 4)
		box.OffsetRect(plot.right - 4 - box.right, 0);
	dc.FillSolidRect(box, RGB(20, 24, 36));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(230, 235, 245));
	CFont* of = dc.SelectObject(&m_font);
	dc.TextOut(box.left + 4, box.top + 2, s);
	dc.SelectObject(of);
}

bool CAnalyzerDlg::UpdateHoverFromPoint(CPoint ptClient)
{
	const bool wasValid = m_hoverValid;
	const int oldBin = m_hoverBin;
	const float oldDb = m_hoverDb;
	const int oldCh = m_hoverCh;

	m_hoverValid = false;
	m_hoverBin = -1;
	if (m_hoverPlotCount <= 0 || m_hoverSplitY <= 0)
		return wasValid;
	// 位相／ゴニオは周波数軸ではないので読取を出さない
	if (m_lowerMode == LowerPhase || m_lowerMode == LowerVector)
		return wasValid;

	// カーソル下のパネルを探す(分割時は各ch、重ね時は1枚で ch=-1)
	int hit = -1;
	CRect hitPlot;
	for (int i = 0; i < m_hoverPlotCount; ++i) {
		CRect plot = m_hoverPlots[i];
		plot.OffsetRect(0, m_hoverSplitY);
		if (plot.PtInRect(ptClient) && plot.Width() >= 8 && plot.Height() >= 8) {
			hit = i;
			hitPlot = plot;
			break;
		}
	}
	if (hit < 0)
		return wasValid;

	int sr = 44100, channels = 2;
	float localBins[CH_MAX][SPEC_BINS];
	EnterCriticalSection(&m_cs);
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
	memcpy(localBins, m_specDb, sizeof(localBins));
	LeaveCriticalSection(&m_cs);

	const float nyquist = (float)sr * 0.5f;
	float fMin = 20.0f, fMax = (std::max)(20.2f, nyquist);
	GetZoomFreqRange(m_freqZoom, nyquist, fMin, fMax);
	const float hz = PlotXToHz(ptClient.x, hitPlot, fMin, fMax);

	int bestB = -1;
	float bestDist = 1e9f;
	for (int b = 0; b < SPEC_BINS; ++b) {
		const float cHz = SpecCenterHz(b, SPEC_BINS, nyquist);
		if (m_freqZoom != ZoomFull && (cHz < fMin || cHz > fMax)) continue;
		const float d = fabsf(logf((std::max)(cHz, 1.0f) / (std::max)(hz, 1.0f)));
		if (bestB < 0 || d < bestDist) { bestDist = d; bestB = b; }
	}
	if (bestB < 0) bestB = 0;

	int ch = m_hoverPlotCh[hit];
	if (ch < 0) {
		// 重ね描き: そのビンで最も大きい ch を表示
		ch = 0;
		float bestDb = localBins[0][bestB];
		for (int c = 1; c < channels; ++c) {
			if (localBins[c][bestB] > bestDb) {
				bestDb = localBins[c][bestB];
				ch = c;
			}
		}
	}
	if (ch < 0) ch = 0;
	if (ch >= channels) ch = channels - 1;

	m_hoverHz = SpecCenterHz(bestB, SPEC_BINS, nyquist);
	m_hoverDb = localBins[ch][bestB];
	if (m_lowerMode == LowerCorrFreq || m_lowerMode == LowerPhaseFreq) {
		EnterCriticalSection(&m_cs);
		if (m_lowerMode == LowerCorrFreq)
			m_hoverDb = m_specCorr[bestB];
		else
			m_hoverDb = m_specPhase[bestB] * (180.0f / 3.14159265f);
		LeaveCriticalSection(&m_cs);
	}
	m_hoverCh = ch;
	m_hoverBin = bestB;
	m_hoverPlot = m_hoverPlots[hit];
	m_hoverValid = true;

	return !wasValid || oldBin != m_hoverBin || oldCh != m_hoverCh
		|| fabsf(oldDb - m_hoverDb) >= 0.5f;
}

bool CAnalyzerDlg::HitEqBand(CPoint ptClient, int& outBand, CRect& outPlot)
{
	outBand = -1;
	outPlot.SetRectEmpty();
	// Soft3D 中は読取専用（誤操作防止）
	if (IsSoft3DBot()) return false;
	// EQ カーブが出ている表示(周波数特性/スペクトログラム)でのみ掴める
	if (!m_eqOverlay || m_lowerMode == LowerPhase || m_lowerMode == LowerVector
		|| m_lowerMode == LowerCorrFreq || m_lowerMode == LowerPhaseFreq) return false;
	if (m_hoverPlotCount <= 0 || m_hoverSplitY <= 0) return false;

	int sr = 44100;
	EnterCriticalSection(&m_cs);
	sr = m_sampleRate > 0 ? m_sampleRate : 44100;
	LeaveCriticalSection(&m_cs);
	const float nyquist = (float)sr * 0.5f;
	float fMin = 20.0f, fMax = (std::max)(20.2f, nyquist);
	GetZoomFreqRange(m_freqZoom, nyquist, fMin, fMax);

	for (int i = 0; i < m_hoverPlotCount; ++i) {
		CRect plot = m_hoverPlots[i];
		plot.OffsetRect(0, m_hoverSplitY);
		if (plot.Width() < 8 || plot.Height() < 8) continue;
		if (!plot.PtInRect(ptClient)) continue;

		int best = -1, bestD = 0;
		for (int b = 0; b < EQ_OVERLAY_BANDS; ++b) {
			const float f = kEqFreqs[b];
			if (f >= nyquist || f < fMin * 0.999f || f > fMax * 1.001f) continue;
			int eqv = savedata.eq[b];
			if (eqv < 0) eqv = 0;
			if (eqv > 200) eqv = 200;
			float t = (EqSliderToDb(eqv) + 12.0f) / 24.0f;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			const int x = (int)HzToPlotX(f, plot, fMin, fMax);
			const int y = plot.bottom - (int)(t * plot.Height());
			const int dx = abs(x - ptClient.x);
			const int dy = abs(y - ptClient.y);
			if (dx > 10 || dy > 10) continue;
			const int d = dx * dx + dy * dy;
			if (best < 0 || d < bestD) { best = b; bestD = d; }
		}
		if (best < 0) return false;
		outBand = best;
		outPlot = plot;
		return true;
	}
	return false;
}

void CAnalyzerDlg::ApplyEqBandFromY(int band, const CRect& plot, int yClient)
{
	if (band < 0 || band >= EQ_OVERLAY_BANDS || plot.Height() < 8) return;
	float t = (float)(plot.bottom - yClient) / (float)plot.Height();
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	const int slider = EqDbToSlider(t * 24.0f - 12.0f);
	if (savedata.eq[band] == slider) return;
	savedata.eq[band] = slider;
	SyncEqUiFromSavedata();
	m_specDirty = true;
	KickUiPresent();
}

void CAnalyzerDlg::Present(CDC& dc, const CRect& rc, BOOL bAero)
{
	UNREFERENCED_PARAMETER(bAero);
	// rc はキャプション下の content 矩形（top は capH のことがある）
	const int contentH = rc.Height();
	const int splitLocal = (int)(contentH * 0.65);
	m_hoverSplitY = rc.top + splitLocal;
	const int waveH = splitLocal;
	const int specH = contentH - splitLocal;
	const int clientW = rc.Width();
	const int clientH = contentH;
	const bool top3d = IsSoft3DTop();
	const bool bot3d = IsSoft3DBot();

	CDC* pDst = &dc;
	if (EnsureFrameBuffer(dc, clientW, clientH) && m_frameDC.GetSafeHdc())
		pDst = &m_frameDC;

	if (m_soft3dApiDemo) {
		GdiSoft3D::PresentApiDemo(*pDst, CRect(0, 0, clientW, clientH));
	}
	else {
	if (top3d) {
		PresentSoft3DTop(*pDst, CRect(0, 0, clientW, waveH));
	}
	else {
		if (m_waveReady && m_waveDC.GetSafeHdc())
			pDst->BitBlt(0, 0, m_waveW, waveH, &m_waveDC, 0, 0, SRCCOPY);
		else
			pDst->FillSolidRect(0, 0, clientW, waveH, ANALYZER_BG);

		DrawTpLufsReadout(*pDst, CRect(0, 0, m_waveW > 0 ? m_waveW : clientW, waveH));

		if (m_showLevelMeter || savedata.pro_corr_meter)
			DrawLevelMeters(*pDst, CRect(0, 0, m_waveW > 0 ? m_waveW : clientW, waveH), ANALYZER_BG);
	}

	if (bot3d) {
		PresentSoft3DBot(*pDst, CRect(0, splitLocal, clientW, clientH));
	}
	else {
		if (m_specReady && m_specDC.GetSafeHdc())
			pDst->BitBlt(0, splitLocal, m_specW, specH, &m_specDC, 0, 0, SRCCOPY);
		else
			pDst->FillSolidRect(0, splitLocal, clientW, specH, ANALYZER_BG);
	}

	if (!top3d || !bot3d)
		pDst->FillSolidRect(0, splitLocal - 1, clientW, 2, RGB(60, 65, 80));
	if (m_hoverValid && !bot3d)
		DrawHoverReadout(*pDst, CRect(0, 0, clientW, clientH));
	}
	if (m_frozen) {
		pDst->SetBkMode(TRANSPARENT);
		pDst->SetTextColor(RGB(255, 180, 80));
		CFont* of = pDst->SelectObject(&m_font);
		pDst->TextOut(8, 4, LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
		pDst->SelectObject(of);
	}
	if ((top3d || bot3d) && m_soft3dTourUntil != 0 && GetTickCount() < m_soft3dTourUntil) {
		pDst->SetBkMode(TRANSPARENT);
		pDst->SetTextColor(RGB(240, 245, 255));
		CFont* of = pDst->SelectObject(&m_font);
		const int tipY = m_frozen ? 22 : 8;
		pDst->TextOut(8, tipY, LL14(
			L"ドラッグ=回転 ホイール=ズーム 0=リセット",
			L"Drag=rotate  Wheel=zoom  0=reset",
			L"Glisser=rotation  Molette=zoom  0=reinit.",
			L"Trascina=ruota  Rotella=zoom  0=reset",
			L"Arrastrar=rotar  Rueda=zoom  0=restablecer",
			L"드래그=회전  휠=줌  0=리셋",
			L"拖动=旋转  滚轮=缩放  0=重置",
			L"سحب=دوران  عجلة=تكبير  0=إعادة",
			L"Перетащ.=поворот  Колесо=масштаб  0=сброс",
			L"Ziehen=drehen  Rad=Zoom  0=Reset",
			L"Arrastar=girar  Roda=zoom  0=redefinir",
			L"Sleep=draaien  Wiel=zoom  0=reset",
			L"Przeciagnij=obrot  Kolo=zoom  0=reset",
			L"Surukle=don  Tekerlek=zoom  0=sifirla"));
		pDst->SelectObject(of);
	}
	if ((top3d || bot3d) && (savedata.soft3dPerfHintDismiss & 2) == 0) {
		static DWORD s_anSoftT0 = 0;
		static int s_anSlow = 0;
		static DWORD s_anHintUntil = 0;
		const DWORD now = GetTickCount();
		if (s_anSoftT0 != 0) {
			const DWORD spent = now - s_anSoftT0;
			if (spent >= 28) {
				if (++s_anSlow >= 40) {
					s_anSlow = 0;
					s_anHintUntil = now + 4000;
				}
			} else if (s_anSlow > 0) {
				--s_anSlow;
			}
		}
		s_anSoftT0 = now;
		if (now < s_anHintUntil) {
			pDst->SetBkMode(TRANSPARENT);
			pDst->SetTextColor(RGB(255, 190, 160));
			CFont* of = pDst->SelectObject(&m_font);
			pDst->TextOut(8, 26, LL14(
				L"重いときは右クリックから 2D に戻せます",
				L"Feeling heavy? Switch back to 2D from the context menu",
				L"Trop lourd ? Revenez en 2D via le menu contextuel",
				L"Troppo pesante? Torna al 2D dal menu contestuale",
				L"¿Va lento? Vuelva a 2D desde el menú contextual",
				L"무거우면 우클릭에서 2D로 돌릴 수 있습니다",
				L"若觉得卡，可从右键菜单切回 2D",
				L"ثقيل؟ عد إلى 2D من قائمة السياق",
				L"Тяжело? Вернитесь в 2D через контекстное меню",
				L"Zu zäh? Über das Kontextmenü zurück zu 2D",
				L"Pesado? Volte ao 2D pelo menu de contexto",
				L"Te zwaar? Ga terug naar 2D via het contextmenu",
				L"Za ciężko? Wróć do 2D z menu kontekstowego",
				L"Ağır mı? Bağlam menüsünden 2B'ye dönün"));
			pDst->SelectObject(of);
		}
	}

	if (pDst != &dc) {
#if CCUSTOM_AERO_SUPPORT
		if (!CCC_IsAeroEnabled() && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11())
			CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, rc.top, clientW, clientH,
				pDst->GetSafeHdc(), 0, 0, clientW, clientH);
		else
#endif
			dc.BitBlt(0, rc.top, clientW, clientH, pDst, 0, 0, SRCCOPY);
	}
}

void CAnalyzerDlg::SyncSoft3DCamsFromSave()
{
	GdiSoft3D::CamFromSaved(m_camTop, savedata.analyzer3dyawTop, savedata.analyzer3dpitchTop, savedata.analyzer3dzoomTop);
	GdiSoft3D::CamFromSaved(m_camBot, savedata.analyzer3dyawBot, savedata.analyzer3dpitchBot, savedata.analyzer3dzoomBot);
}

void CAnalyzerDlg::PaletteApplySoft3D()
{
	SyncSoft3DCamsFromSave();
	KickUiPresent();
}

void CAnalyzerDlg::PersistSoft3DCams()
{
	GdiSoft3D::CamToSaved(m_camTop, savedata.analyzer3dyawTop, savedata.analyzer3dpitchTop, savedata.analyzer3dzoomTop);
	GdiSoft3D::CamToSaved(m_camBot, savedata.analyzer3dyawBot, savedata.analyzer3dpitchBot, savedata.analyzer3dzoomBot);
}

void CAnalyzerDlg::Soft3dYawTopCb(void* ctx, int value)
{
	auto* self = (CAnalyzerDlg*)ctx;
	if (!self) return;
	self->m_camTop.yawDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_camTop);
	self->PersistSoft3DCams();
	self->KickUiPresent();
}
void CAnalyzerDlg::Soft3dPitchTopCb(void* ctx, int value)
{
	auto* self = (CAnalyzerDlg*)ctx;
	if (!self) return;
	self->m_camTop.pitchDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_camTop);
	self->PersistSoft3DCams();
	self->KickUiPresent();
}
void CAnalyzerDlg::Soft3dZoomTopCb(void* ctx, int value)
{
	auto* self = (CAnalyzerDlg*)ctx;
	if (!self) return;
	self->m_camTop.zoom = (float)value / 100.f;
	GdiSoft3D::ClampCam(self->m_camTop);
	self->PersistSoft3DCams();
	self->KickUiPresent();
}
void CAnalyzerDlg::Soft3dYawBotCb(void* ctx, int value)
{
	auto* self = (CAnalyzerDlg*)ctx;
	if (!self) return;
	self->m_camBot.yawDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_camBot);
	self->PersistSoft3DCams();
	self->KickUiPresent();
}
void CAnalyzerDlg::Soft3dPitchBotCb(void* ctx, int value)
{
	auto* self = (CAnalyzerDlg*)ctx;
	if (!self) return;
	self->m_camBot.pitchDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_camBot);
	self->PersistSoft3DCams();
	self->KickUiPresent();
}
void CAnalyzerDlg::Soft3dZoomBotCb(void* ctx, int value)
{
	auto* self = (CAnalyzerDlg*)ctx;
	if (!self) return;
	self->m_camBot.zoom = (float)value / 100.f;
	GdiSoft3D::ClampCam(self->m_camBot);
	self->PersistSoft3DCams();
	self->KickUiPresent();
}

void CAnalyzerDlg::PresentSoft3DTop(CDC& dc, const CRect& pane)
{
	const int pw = pane.Width(), ph = pane.Height();
	if (pw < 8 || ph < 8) return;
	CDC clip; clip.CreateCompatibleDC(&dc);
	CBitmap bm; bm.CreateCompatibleBitmap(&dc, pw, ph);
	CBitmap* ob = clip.SelectObject(&bm);
	clip.FillSolidRect(0, 0, pw, ph, ANALYZER_BG);

	const float boxes[1][6] = { { -1.20f, 1.25f, -0.05f, 0.75f, -0.05f, 1.05f } };
	GdiSoft3D::View v;
	GdiSoft3D::BuildView(pw, ph, m_camTop, boxes, 1, v);
	GdiSoft3D::Context ctx;
	if (!ctx.Create(pw, ph)) {
		clip.SelectObject(ob);
		return;
	}
	ctx.view = v;
	ctx.SetFog(GdiSoft3D::FogLinear, ANALYZER_BG, 0.05f, 1.1f, 0.7f);
	ctx.BeginFrame(ANALYZER_BG);
	ctx.DrawGrid(-1.05f, 1.05f, 0.0f, 0.95f, 0.0f, 6, RGB(40, 44, 58));

	int wr = 0, wf = 0, wch = 0;
	if (!SnapshotRing(wr, wf, wch) || wf < 32) {
		ctx.EndFrame();
		ctx.Present(clip, 0, 0);
		dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
		clip.SelectObject(ob);
		return;
	}

	const int chUse = (std::max)(1, (std::min)(wch, 2));
	const bool trigger = (m_waveMode == WaveTrigger || m_waveMode == WavePhosphor);
	const bool showCorr3d = (savedata.pro_corr_meter != 0 && chUse >= 2);
	const float xWaveR = (m_showLevelMeter || showCorr3d) ? 0.70f : 1.0f;
	const float xSpan = xWaveR + 1.0f;

	if (trigger) {
		const int cols = 120;
		const int budget = (std::min)(wf, RING_SAMPLES / 2);
		const int spc = (std::max)(1, budget / cols);
		const int window = spc * cols;
		int base = wr - window;
		int searchMax = (std::min)(RING_SAMPLES / 4, wf - window);
		if (searchMax < 0) searchMax = 0;
		int trigStart = base;
		for (int k = 0; k < searchMax; ++k) {
			const int cur = ((base - k) % RING_SAMPLES + RING_SAMPLES) % RING_SAMPLES;
			const int prv = ((base - k - 1) % RING_SAMPLES + RING_SAMPLES) % RING_SAMPLES;
			if (m_ringSnap[0][prv] <= 0.0f && m_ringSnap[0][cur] > 0.0f) {
				trigStart = base - k;
				break;
			}
		}
		auto sampleAt = [&](int ch, int i) -> float {
			int idx = trigStart + i * spc;
			while (idx < 0) idx += RING_SAMPLES;
			idx %= RING_SAMPLES;
			float a = m_ringSnap[ch][idx];
			if (a > 1.f) a = 1.f; if (a < -1.f) a = -1.f;
			return a;
		};
		for (int i = 0; i < cols - 1; ++i) {
			const float x0 = -1.0f + xSpan * ((float)i / (float)cols);
			const float x1 = -1.0f + xSpan * ((float)(i + 1) / (float)cols);
			for (int c = 0; c < chUse; ++c) {
				float s0 = sampleAt(c, i);
				float s1 = sampleAt(c, i + 1);
				if (m_waveMode == WaveMidSide && chUse >= 2) {
					const float L0 = sampleAt(0, i), R0 = sampleAt(1, i);
					const float L1 = sampleAt(0, i + 1), R1 = sampleAt(1, i + 1);
					s0 = (c == 0) ? 0.5f * (L0 + R0) : 0.5f * (L0 - R0);
					s1 = (c == 0) ? 0.5f * (L1 + R1) : 0.5f * (L1 - R1);
				}
				const float y0 = 0.32f + s0 * 0.28f;
				const float y1 = 0.32f + s1 * 0.28f;
				const float z0 = (c == 0) ? 0.22f : 0.55f;
				const float z1 = z0 + 0.06f;
				const float yLo = (std::min)(y0, y1) - 0.01f;
				const float yHi = (std::max)(y0, y1) + 0.01f;
				ctx.DrawBox(x0, x1, yHi, z0, z1, kChColor[c], yLo);
			}
		}
		ctx.DrawLine(-1.0f, 0.32f, 0.15f, xWaveR, 0.32f, 0.15f, RGB(70, 78, 98));
		ctx.DrawLine(-1.0f, 0.32f, 0.70f, xWaveR, 0.32f, 0.70f, RGB(70, 78, 98));
	}
	else {
		static float histL[96] = {}, histR[96] = {};
		static int histInit = 0;
		const int cols = 96;
		if (!histInit) { memset(histL, 0, sizeof(histL)); memset(histR, 0, sizeof(histR)); histInit = 1; }
		const int useN = (std::min)(wf, 512);
		float peakL = 0.f, peakR = 0.f, sumL = 0.f, sumR = 0.f;
		for (int s = 0; s < useN; ++s) {
			int idx = wr - useN + s;
			while (idx < 0) idx += RING_SAMPLES;
			idx %= RING_SAMPLES;
			float aL = m_ringSnap[0][idx];
			float aR = (chUse >= 2) ? m_ringSnap[1][idx] : aL;
			if (m_waveMode == WaveMidSide) {
				const float m = 0.5f * (aL + aR);
				const float sd = 0.5f * (aL - aR);
				aL = m; aR = sd;
			}
			const float absL = fabsf(aL), absR = fabsf(aR);
			if (absL > peakL) peakL = absL;
			if (absR > peakR) peakR = absR;
			sumL += aL * aL;
			sumR += aR * aR;
		}
		if (m_waveMode == WaveLoudness) {
			peakL = (useN > 0) ? sqrtf(sumL / (float)useN) : 0.f;
			peakR = (useN > 0) ? sqrtf(sumR / (float)useN) : 0.f;
		}
		if (peakL > 1.f) peakL = 1.f;
		if (peakR > 1.f) peakR = 1.f;
		memmove(histL, histL + 1, (cols - 1) * sizeof(float));
		memmove(histR, histR + 1, (cols - 1) * sizeof(float));
		histL[cols - 1] = peakL;
		histR[cols - 1] = peakR;
		ctx.DrawMirrorFloor(-1.05f, xWaveR + 0.05f, 0.22f, 0.68f, RGB(50, 70, 110), 0.22f);
		COLORREF cL = (m_waveMode == WaveMidSide) ? RGB(120, 210, 255) : kChColor[0];
		COLORREF cR = (m_waveMode == WaveMidSide) ? RGB(255, 180, 90) : kChColor[1];
		if (chUse >= 2)
			ctx.DrawStereoBarsLR(-1.0f, xWaveR, cols, histL, histR, 0.30f, 0.55f, 0.52f, 0.12f, cL, cR);
		else
			ctx.DrawStereoBarsLR(-1.0f, xWaveR, cols, histL, nullptr, 0.30f, 0.55f, 0.52f, 0.12f, cL, cL);
	}

	if (m_showLevelMeter) {
		const int ch = (std::max)(1, (std::min)(m_channels, CH_MAX));
		const float slot = 0.055f;
		for (int c = 0; c < ch; ++c) {
			float lv = m_meterRms[c];
			if (lv < 0.02f) continue;
			if (lv > 1.f) lv = 1.f;
			const float x0 = 0.78f + slot * (float)c;
			ctx.DrawBox(x0, x0 + slot * 0.70f, 0.04f + lv * 0.55f, 0.20f, 0.40f, kChColor[c % CH_MAX], 0.f);
		}
	}
	if (showCorr3d) {
		const float corr = ProAudio_CorrValue();
		float t = (corr + 1.f) * 0.5f;
		if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
		const float x0 = m_showLevelMeter ? 1.12f : 0.78f;
		ctx.DrawBox(x0, x0 + 0.08f, 0.04f + t * 0.55f, 0.45f, 0.62f, RGB(100, 230, 150), 0.f);
	}

	ctx.EndFrame();
	ctx.Present(clip, 0, 0);
	// Soft2D ヴィネットは毎フレ O(wh) で重い。数フレに1回・永続バッファで品質パス
	{
		static DWORD s_postTick = 0;
		static GdiSoft2D::Context s2;
		static int s2w = 0, s2h = 0;
		const DWORD now = GetTickCount();
		if (now - s_postTick >= 100u) {
			s_postTick = now;
			if (s2w != pw || s2h != ph || !s2.fb.hdc) {
				if (!s2.Create(pw, ph, false) || !s2.fb.hdc) { /* skip */ }
				else { s2w = pw; s2h = ph; }
			}
			if (s2.fb.hdc && s2w == pw && s2h == ph) {
				::BitBlt(s2.fb.hdc, 0, 0, pw, ph, clip.GetSafeHdc(), 0, 0, SRCCOPY);
				s2.Vignette(0.32f);
				s2.Present(clip, 0, 0);
			}
		}
	}
	dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
	clip.SelectObject(ob);
}

void CAnalyzerDlg::PresentSoft3DBot(CDC& dc, const CRect& pane)
{
	const int pw = pane.Width(), ph = pane.Height();
	if (pw < 8 || ph < 8) return;
	CDC clip; clip.CreateCompatibleDC(&dc);
	CBitmap bm; bm.CreateCompatibleBitmap(&dc, pw, ph);
	CBitmap* ob = clip.SelectObject(&bm);
	clip.FillSolidRect(0, 0, pw, ph, ANALYZER_BG);

	// ---- 位相スコープ ----
	if (m_lowerMode == LowerPhase) {
		const float boxes[1][6] = { { -0.85f, 0.85f, -0.05f, 0.85f, -0.05f, 1.05f } };
		GdiSoft3D::View v;
		GdiSoft3D::BuildView(pw, ph, m_camBot, boxes, 1, v);
		GdiSoft3D::Scene sc;
		sc.Begin(v);
		sc.AddFloor(-0.75f, 0.75f, 0.0f, 0.95f, RGB(40, 44, 58));
		sc.AddBox(-0.02f, 0.02f, 0.02f, 0.0f, 0.95f, RGB(70, 78, 98), 0.f);
		sc.AddBox(-0.75f, 0.75f, 0.02f, 0.46f, 0.50f, RGB(70, 78, 98), 0.f);

		int wr = 0, wf = 0, wch = 0;
		if (SnapshotRing(wr, wf, wch) && wf >= 64 && wch >= 2) {
			const int useN = (std::min)(wf, 1024);
			const int step = (std::max)(1, useN / 220);
			int n = 0;
			for (int i = 0; i < useN && n < 220; i += step, ++n) {
				int idx = wr - useN + i;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				float l = m_ringSnap[0][idx];
				float rr = m_ringSnap[1][idx];
				if (l > 1.f) l = 1.f; if (l < -1.f) l = -1.f;
				if (rr > 1.f) rr = 1.f; if (rr < -1.f) rr = -1.f;
				const float x = (l - rr) * 0.55f;
				const float y = fabsf(l + rr) * 0.40f + 0.02f;
				const float z = 0.05f + 0.85f * ((float)n / 220.f);
				const float s = 0.014f;
				COLORREF col = RGB(120, 230, 170);
				if (fabsf(l - rr) > 0.55f) col = RGB(255, 160, 100);
				sc.AddBox(x - s, x + s, y, z, z + 0.022f, col, 0.f);
			}
		}
		sc.Flush(clip);
		dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
		clip.SelectObject(ob);
		return;
	}

	// ---- スペクトログラム: L/R を左右に分けた時間ウォール ----
	if (m_lowerMode == LowerWaterfall) {
		const float boxes[1][6] = { { -1.10f, 1.10f, -0.02f, 0.55f, 0.0f, 1.10f } };
		GdiSoft3D::View v;
		GdiSoft3D::BuildView(pw, ph, m_camBot, boxes, 1, v);
		GdiSoft3D::Scene sc;
		sc.Begin(v);
		sc.AddFloor(-1.05f, 1.05f, 0.0f, 1.0f, RGB(34, 38, 50));

		int wfWrite = 0, wfFilled = 0;
		EnterCriticalSection(&m_cs);
		wfWrite = m_wfWrite;
		wfFilled = m_wfFilled;
		LeaveCriticalSection(&m_cs);
		// 面予算: rows×bins×ch×3面 ≤ kMaxFaces。間引き必須。
		const int rows = (std::min)(wfFilled, 36);
		const int bins = SPEC_BINS;
		const int chUse = (std::max)(1, (std::min)(m_channels, 2));
		const float rowD = 0.85f / 36.f;
		const int bStep = 4;
		for (int r = rows - 1; r >= 0; --r) {
			const int src = ((wfWrite - 1 - r) % WF_ROWS + WF_ROWS) % WF_ROWS;
			const float z0 = 0.08f + rowD * (float)r;
			const float z1 = z0 + rowD * 0.80f;
			for (int c = 0; c < chUse; ++c) {
				const float xOff = (chUse >= 2) ? ((c == 0) ? -1.02f : 0.02f) : -1.0f;
				const float xSpan = (chUse >= 2) ? 0.98f : 2.0f;
				COLORREF base = kChColor[c % CH_MAX];
				for (int b = 0; b < bins; b += bStep) {
					float db = m_wfHist[c][src][b];
					float t = (db + 96.f) / 96.f;
					if (t < 0.08f) continue;
					if (t > 1.f) t = 1.f;
					const float xL = xOff + xSpan * ((float)b / (float)bins);
					const float xR = xOff + xSpan * ((float)(b + bStep) / (float)bins) - 0.004f;
					if (xR <= xL) continue;
					sc.AddBox(xL, xR, t * 0.40f, z0, z1, GdiSoft3D::Shade(base, 0.45f + 0.55f * t), 0.f);
				}
			}
		}
		sc.Flush(clip);
		dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
		clip.SelectObject(ob);
		return;
	}

	if (m_lowerMode == LowerVector) {
		const float boxes[1][6] = { { -0.85f, 0.85f, -0.05f, 0.85f, -0.05f, 1.05f } };
		GdiSoft3D::View v;
		GdiSoft3D::BuildView(pw, ph, m_camBot, boxes, 1, v);
		GdiSoft3D::Scene sc;
		sc.Begin(v);
		sc.AddFloor(-0.75f, 0.75f, 0.0f, 0.95f, RGB(40, 44, 58));
		sc.AddBox(-0.02f, 0.02f, 0.02f, 0.0f, 0.95f, RGB(70, 78, 98), 0.f);
		sc.AddBox(-0.75f, 0.75f, 0.02f, 0.46f, 0.50f, RGB(70, 78, 98), 0.f);
		int wr = 0, wf = 0, wch = 0;
		if (SnapshotRing(wr, wf, wch) && wf >= 64 && wch >= 2) {
			const int useN = (std::min)(wf, 800);
			const int step = (std::max)(1, useN / 160);
			int n = 0;
			for (int i = 0; i < useN && n < 160; i += step, ++n) {
				int idx = wr - useN + i;
				while (idx < 0) idx += RING_SAMPLES;
				idx %= RING_SAMPLES;
				float l = m_ringSnap[0][idx];
				float rr = m_ringSnap[1][idx];
				if (l > 1.f) l = 1.f; if (l < -1.f) l = -1.f;
				if (rr > 1.f) rr = 1.f; if (rr < -1.f) rr = -1.f;
				const float x = (l - rr) * 0.55f;
				const float y = fabsf(l + rr) * 0.38f + 0.02f;
				const float z = 0.08f + 0.80f * ((float)n / 160.f);
				const float s = 0.018f;
				sc.AddBox(x - s, x + s, y, z, z + 0.03f, RGB(90, 210, 160), 0.f);
			}
		}
		sc.Flush(clip);
		dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
		clip.SelectObject(ob);
		return;
	}

	if (m_lowerMode == LowerRta || m_lowerMode == LowerCorrFreq || m_lowerMode == LowerPhaseFreq) {
		const float boxes[1][6] = { { -1.05f, 1.05f, -0.02f, 0.62f, 0.0f, 0.95f } };
		GdiSoft3D::View v;
		GdiSoft3D::BuildView(pw, ph, m_camBot, boxes, 1, v);
		GdiSoft3D::Scene sc;
		sc.Begin(v);
		sc.AddFloor(-1.0f, 1.0f, 0.0f, 0.88f, RGB(40, 44, 58));
		if (m_lowerMode == LowerRta) {
			static const float kRtaHz[31] = {
				20.f, 25.f, 31.5f, 40.f, 50.f, 63.f, 80.f, 100.f, 125.f, 160.f,
				200.f, 250.f, 315.f, 400.f, 500.f, 630.f, 800.f, 1000.f, 1250.f, 1600.f,
				2000.f, 2500.f, 3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f, 20000.f
			};
			const float nyq = (m_sampleRate > 0) ? (float)m_sampleRate * 0.5f : 22050.f;
			float lev[31];
			int nB = 0;
			float spec0[SPEC_BINS], spec1[SPEC_BINS];
			int chN = 1;
			EnterCriticalSection(&m_cs);
			memcpy(spec0, m_specDb[0], sizeof(spec0));
			memcpy(spec1, m_specDb[1], sizeof(spec1));
			chN = m_channels;
			LeaveCriticalSection(&m_cs);
			for (int i = 0; i < 31; ++i) {
				if (kRtaHz[i] >= nyq) break;
				const float fLo = kRtaHz[i] / 1.122462f;
				const float fHi = kRtaHz[i] * 1.122462f;
				float best = -96.f;
				for (int b = 0; b < SPEC_BINS; ++b) {
					const float hz = SpecCenterHz(b, SPEC_BINS, nyq);
					if (hz < fLo || hz > fHi) continue;
					if (spec0[b] > best) best = spec0[b];
					if (chN >= 2 && spec1[b] > best) best = spec1[b];
				}
				float t = (best + 96.f) / 96.f;
				if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
				lev[nB++] = t;
			}
			for (int i = 0; i < nB; ++i) {
				if (lev[i] < 0.02f) continue;
				const float x0 = -1.0f + 2.0f * ((float)i / (float)nB);
				const float x1 = -1.0f + 2.0f * ((float)(i + 1) / (float)nB) - 0.012f;
				if (x1 > x0)
					sc.AddBox(x0, x1, lev[i] * 0.52f, 0.12f, 0.72f, RGB(80, 180, 220), 0.f);
			}
		}
		else {
			float lev[SPEC_BINS];
			EnterCriticalSection(&m_cs);
			for (int b = 0; b < SPEC_BINS; ++b) {
				if (m_lowerMode == LowerCorrFreq)
					lev[b] = (m_specCorr[b] + 1.f) * 0.5f;
				else
					lev[b] = (m_specPhase[b] / 3.14159265f + 1.f) * 0.5f;
				if (lev[b] < 0.f) lev[b] = 0.f;
				if (lev[b] > 1.f) lev[b] = 1.f;
			}
			LeaveCriticalSection(&m_cs);
			const COLORREF col = (m_lowerMode == LowerCorrFreq) ? RGB(120, 230, 170) : RGB(160, 190, 255);
			for (int b = 0; b < SPEC_BINS; ++b) {
				if (lev[b] < 0.02f) continue;
				const float x0 = -1.0f + 2.0f * ((float)b / (float)SPEC_BINS);
				const float x1 = -1.0f + 2.0f * ((float)(b + 1) / (float)SPEC_BINS) - 0.004f;
				if (x1 > x0)
					sc.AddBox(x0, x1, lev[b] * 0.52f, 0.12f, 0.72f, col, 0.f);
			}
		}
		sc.Flush(clip);
		dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
		clip.SelectObject(ob);
		return;
	}

	// ---- 周波数特性: Scene + ステレオ左右並び ----
	float maxY = 0.55f;
	float gap = 0.18f;
	switch (m_specStyle) {
	case StyleLine: gap = 0.40f; break;
	case StyleBars: gap = 0.28f; break;
	case StyleCubase: gap = 0.18f; maxY = 0.60f; break;
	case StyleSpan: gap = 0.06f; break;
	case StyleAbleton: gap = 0.22f; break;
	case StyleFabFilter: gap = 0.12f; maxY = 0.58f; break;
	default: break;
	}

	const float boxes[1][6] = { { -1.05f, 1.05f, -0.02f, maxY + 0.08f, 0.0f, 0.95f } };
	GdiSoft3D::View v;
	GdiSoft3D::BuildView(pw, ph, m_camBot, boxes, 1, v);
	GdiSoft3D::Scene sc;
	sc.Begin(v);
	sc.AddFloor(-1.0f, 1.0f, 0.0f, 0.88f, RGB(40, 44, 58));

	const float (*src)[SPEC_BINS] = m_peakHold ? m_specPeakDb : m_specDb;
	const int bins = SPEC_BINS;
	const int chN = (std::max)(1, (std::min)(m_channels, 2));
	float levL[SPEC_BINS] = {}, levR[SPEC_BINS] = {};
	for (int b = 0; b < bins; ++b) {
		float tL = (src[0][b] + 96.f) / 96.f;
		if (tL < 0.f) tL = 0.f; if (tL > 1.f) tL = 1.f;
		levL[b] = tL;
		if (chN >= 2) {
			float tR = (src[1][b] + 96.f) / 96.f;
			if (tR < 0.f) tR = 0.f; if (tR > 1.f) tR = 1.f;
			levR[b] = tR;
		}
	}
	if (chN >= 2) {
		sc.AddStereoBarsLR(-1.0f, 1.0f, bins, levL, levR,
			0.28f, 0.55f, maxY, gap, kChColor[0], kChColor[1]);
	} else {
		sc.AddStereoBarsLR(-1.0f, 1.0f, bins, levL, nullptr,
			0.28f, 0.55f, maxY, gap, kChColor[0], kChColor[0]);
	}

	sc.Flush(clip);
	{
		static DWORD s_postTickBot = 0;
		static GdiSoft2D::Context s2;
		static int s2w = 0, s2h = 0;
		const DWORD now = GetTickCount();
		if (now - s_postTickBot >= 100u) {
			s_postTickBot = now;
			if (s2w != pw || s2h != ph || !s2.fb.hdc) {
				if (!s2.Create(pw, ph, false) || !s2.fb.hdc) { /* skip */ }
				else { s2w = pw; s2h = ph; }
			}
			if (s2.fb.hdc && s2w == pw && s2h == ph) {
				::BitBlt(s2.fb.hdc, 0, 0, pw, ph, clip.GetSafeHdc(), 0, 0, SRCCOPY);
				s2.Vignette(0.28f);
				s2.Present(clip, 0, 0);
			}
		}
	}
	dc.BitBlt(pane.left, pane.top, pw, ph, &clip, 0, 0, SRCCOPY);
	clip.SelectObject(ob);
}

void CAnalyzerDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rcFull;
	GetClientRect(&rcFull);
	if (rcFull.IsRectEmpty()) {
		InterlockedExchange(&m_presentPosted, 0);
		return;
	}
	CRect rc = rcFull;
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && rc.Height() > capH)
		rc.top = capH;

	const int contentH = rc.Height();
	const int splitLocal = (int)(contentH * 0.65);
	const int split = rc.top + splitLocal;
	const int waveW = rc.Width();
	const int waveH = splitLocal;
	const int specW = rc.Width();
	const int specH = contentH - splitLocal;
	const int clientW = rc.Width();
	const int clientH = contentH;

#if CCUSTOM_AERO_SUPPORT
	const BOOL bAero = (savedata.aero == 1 && CCC_IsAeroEnabled() && CCC_IsWin11());
	const COLORREF bg = bAero ? ANALYZER_CHROMA_KEY : ANALYZER_BG;
#else
	const BOOL bAero = FALSE;
	const COLORREF bg = ANALYZER_BG;
#endif

	if (!EnsureWaveBuffer(dc, waveW, waveH) || !EnsureSpecBuffer(dc, specW, specH)) {
		dc.FillSolidRect(rc, ANALYZER_BG);
		InterlockedExchange(&m_presentPosted, 0);
		return;
	}

	int pending = 0;
	EnterCriticalSection(&m_cs);
	pending = m_pendingScroll;
	LeaveCriticalSection(&m_cs);

	// 1フレームで大きく飛ぶとカクつくが、追いつき不足も精度を落とす。
	const int scrollCap = (std::max)(8, (std::min)(48, m_waveW > 0 ? m_waveW / 20 : 24));

	bool didWaveFull = false;
	bool didWaveScroll = false;
	bool needDeferredKick = false;
#if CCUSTOM_AERO_SUPPORT
	m_lastWaveScroll = 0;
#endif

	// Soft3D 中は 2D 波形/スペクトルの GDI 更新を飛ばす（二重描画で UI/ピアノが死ぬ）
	const bool softTop = IsSoft3DTop();
	const bool softBot = IsSoft3DBot();
	if (!softTop) {
		// トリガー表示は毎回全描画(スクロール差分を使わない)
		if (!m_waveReady || m_waveMode == WaveTrigger || m_waveMode == WavePhosphor) {
			InterlockedExchange(&m_fullRedrawBusy, 1);
			FullRedrawWave(bg);
			didWaveFull = true;
			InterlockedExchange(&m_fullRedrawBusy, 0);
			if (InterlockedExchange(&m_presentDeferred, 0) != 0)
				needDeferredKick = true;
		}
		else if (pending > 0) {
			const int scrolled = ScrollWaveAndDrawNew(bg, scrollCap);
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
	} else {
		// Soft3D scroll 用に pending だけ消費（2D BB は触らない）
		if (pending > 0) {
			EnterCriticalSection(&m_cs);
			m_pendingScroll = 0;
			LeaveCriticalSection(&m_cs);
		}
	}

	// FFT キックは FeedPCM 側のみ。ここで再要求するとフォーカス時に描画ループ化する。
	const bool didSpec = (!softBot && (!m_specReady || m_specDirty));
	if (didSpec)
		RedrawSpectrum(bg);

	m_hoverSplitY = split;
	// マウス停止中も解析描画でパネル表が更新されるため、カーソル位置からホバーを再同期
	if (m_trackingMouse) {
		CPoint pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);
		UpdateHoverFromPoint(pt);
	}
	m_hoverChanged = false;

	// 未消化スクロールの追い付き Kick は提示後に行う（presentPosted 解放後）
	EnterCriticalSection(&m_cs);
	const bool moreScroll = (m_pendingScroll > 0);
	LeaveCriticalSection(&m_cs);

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
				const int stripW = AnalyzerMeterStripWidth(m_channels, m_showLevelMeter ? 1 : 0);
				const int plotW = AnalyzerWavePlotWidth(waveW, m_channels, m_showLevelMeter ? 1 : 0);
				const int meterWdraw = (plotW < waveW) ? (waveW - plotW) : stripW;
				const int stripX = (meterWdraw > 0)
					? ((plotW < waveW) ? plotW : (std::max)(0, waveW - meterWdraw))
					: 0;

				if (didWaveScroll && m_lastWaveScroll > 0 && m_chromaReady) {
					if (plotW >= waveW && meterWdraw > 0)
						m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), stripX, 0, stripX, 0, meterWdraw, waveH, key);
					m_chromaCache.ScrollCols(0, 0, plotW, waveH, m_lastWaveScroll);
					const int keep = plotW - m_lastWaveScroll;
					if (keep >= 0 && m_lastWaveScroll > 0)
						m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), keep, 0,
							keep, 0, m_lastWaveScroll, waveH, key);
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0,
						(std::min)(32, plotW), waveH, key);
					if (!m_tpLufsTipRc.IsRectEmpty()) {
						const int rw = (std::min)((long)plotW, m_tpLufsTipRc.right + 8);
						const int rh = (std::min)((long)waveH, m_tpLufsTipRc.bottom + 4);
						if (rw > 0 && rh > 0)
							m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0, rw, rh, key);
					}
				}
				else if (didWaveFull || !m_chromaReady) {
					m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(), 0, 0, 0, 0, waveW, waveH, key);
				}

				if (meterWdraw > 0 && m_waveScratchDC.GetSafeHdc()) {
					m_waveScratchDC.FillSolidRect(0, 0, meterWdraw, waveH, key);
					DrawLevelMeters(m_waveScratchDC, CRect(0, 0, meterWdraw, waveH), key);
					m_chromaCache.UpdateRect(m_waveScratchDC.GetSafeHdc(),
						0, 0, stripX, 0, meterWdraw, waveH, key);
				}

				// TP/LUFS も独立プレート(不透明)としてクロマへ焼く
				if (m_waveScratchDC.GetSafeHdc()) {
					const int bandH = (std::min)(waveH, 28);
					m_waveScratchDC.FillSolidRect(0, 0, waveW, bandH, key);
					DrawTpLufsReadout(m_waveScratchDC, CRect(0, 0, waveW, waveH));
					if (!m_tpLufsTipRc.IsRectEmpty() && m_tpLufsTipRc.bottom <= bandH) {
						m_chromaCache.UpdateRect(m_waveScratchDC.GetSafeHdc(),
							m_tpLufsTipRc.left, m_tpLufsTipRc.top,
							m_tpLufsTipRc.left, m_tpLufsTipRc.top,
							m_tpLufsTipRc.Width(), m_tpLufsTipRc.Height(), key);
					}
				}
			}
			if (m_specReady && m_specDC.GetSafeHdc() && (didSpec || !m_chromaReady)) {
				m_chromaCache.UpdateRect(m_specDC.GetSafeHdc(), 0, 0, 0, splitLocal, specW, specH, key);
			}
			m_chromaCache.FillOpaqueRect(0, splitLocal - 1, clientW, 2, RGB(60, 65, 80), key);

			if (m_hoverValid) {
				CRect plot = m_hoverPlot;
				plot.OffsetRect(0, splitLocal);
				CString s;
				const int channels = (std::max)(1, (std::min)(m_channels, CH_MAX));
				if (m_hoverHz >= 1000.0f)
					s.Format(_T("%s  %.2f kHz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz / 1000.0f, m_hoverDb);
				else
					s.Format(_T("%s  %.0f Hz  %.1f dB"), ChannelLabel(m_hoverCh, channels), m_hoverHz, m_hoverDb);
				CDC* pTmp = &m_waveScratchDC;
				if (pTmp->GetSafeHdc()) {
					CFont* of = pTmp->SelectObject(&m_font);
					CSize sz = pTmp->GetTextExtent(s);
					pTmp->SelectObject(of);
					CRect box(plot.left + 8, plot.top + 8, plot.left + 16 + sz.cx, plot.top + 16 + sz.cy);
					if (box.right > plot.right - 4)
						box.OffsetRect(plot.right - 4 - box.right, 0);
					const int bw = box.Width(), bh = box.Height();
					if (bw > 0 && bh > 0 && bw < waveW && bh < waveH) {
						pTmp->FillSolidRect(0, 0, bw, bh, RGB(20, 24, 36));
						pTmp->SetBkMode(TRANSPARENT);
						pTmp->SetTextColor(RGB(230, 235, 245));
						of = pTmp->SelectObject(&m_font);
						pTmp->TextOut(4, 2, s);
						pTmp->SelectObject(of);
						m_chromaCache.UpdateRect(pTmp->GetSafeHdc(), 0, 0, box.left, box.top, bw, bh, key);
					}
				}
			}
			if (m_frozen) {
				CString fr = LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu");
				if (m_waveScratchDC.GetSafeHdc()) {
					CFont* of = m_waveScratchDC.SelectObject(&m_font);
					CSize sz = m_waveScratchDC.GetTextExtent(fr);
					const int bw = sz.cx + 12, bh = sz.cy + 8;
					m_waveScratchDC.FillSolidRect(0, 0, bw, bh, RGB(40, 32, 16));
					m_waveScratchDC.SetBkMode(TRANSPARENT);
					m_waveScratchDC.SetTextColor(RGB(255, 180, 80));
					m_waveScratchDC.TextOut(6, 4, fr);
					m_waveScratchDC.SelectObject(of);
					m_chromaCache.UpdateRect(m_waveScratchDC.GetSafeHdc(), 0, 0, 4, 2, bw, bh, key);
				}
			}

			// 「メインに追従」をクロマバッファへ焼いてから1回だけ BlitFull。
			// カスタムキャプション時は帯上の子チェックなので本文クロマへ焼かない。
			if (capH <= 0)
			{
				CRect lockRc;
				CCC_MainLockGetOverlayRect(m_hWnd, lockRc);
				if (!lockRc.IsRectEmpty() && m_chromaCache.hdcDib) {
					CRect headerRow(0, lockRc.top, clientW, lockRc.bottom);
					if (headerRow.top < 0)
						headerRow.top = 0;
					if (headerRow.bottom > clientH)
						headerRow.bottom = clientH;

					CRect wavePart = headerRow;
					if (wavePart.bottom > splitLocal)
						wavePart.bottom = splitLocal;
					if (wavePart.top < wavePart.bottom && m_waveReady && m_waveDC.GetSafeHdc()) {
						m_chromaCache.UpdateRect(m_waveDC.GetSafeHdc(),
							wavePart.left, wavePart.top, wavePart.left, wavePart.top,
							wavePart.Width(), wavePart.Height(), key);
					}
					CRect specPart = headerRow;
					if (specPart.top < splitLocal)
						specPart.top = splitLocal;
					if (specPart.top < specPart.bottom && m_specReady && m_specDC.GetSafeHdc()) {
						m_chromaCache.UpdateRect(m_specDC.GetSafeHdc(),
							specPart.left, specPart.top - splitLocal, specPart.left, specPart.top,
							specPart.Width(), specPart.Height(), key);
					}
					CDC dcCache;
					dcCache.Attach(m_chromaCache.hdcDib);
					CCC_MainLockPaintClient(dcCache, m_hWnd);
					dcCache.Detach();
					m_chromaCache.MakeRectOpaque(lockRc.left, lockRc.top, lockRc.Width(), lockRc.Height());
				}
			}

			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, capH, clientW, clientH);
		}
		else {
			Present(dc, rc, FALSE);
			CCC_MainLockPaintClient(dc, m_hWnd);
		}
	}
	else
#endif
	{
		Present(dc, rc, FALSE);
		CCC_MainLockPaintClient(dc, m_hWnd);
	}

	CCC_CaptionPaint(dc, m_hWnd);

	// 描画完了後に提示フラグを開放し、未消化スクロール/遅延 Kick があれば1回だけ
	InterlockedExchange(&m_presentPosted, 0);
	if ((moreScroll && !didWaveFull) || needDeferredKick)
		KickUiPresent();
}

BOOL CAnalyzerDlg::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CAnalyzerDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 2) {
		if (m_soft3dApiDemo)
			KickUiPresent();
		else
			KillTimer(2);
		return;
	}
	if (nIDEvent == 1) {
		// 描画は PostMessage 自由走行。タイマーは座標保存のみ。
		CRect rc;
		GetWindowRect(&rc);
		if (!IsIconic()) {
			savedata.analyzerx = rc.left;
			savedata.analyzery = rc.top;
			savedata.analyzerw = rc.Width();
			savedata.analyzerh = rc.Height();
		}
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}


void CAnalyzerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	ReleaseBuffers();
#if CCUSTOM_AERO_SUPPORT
	// Finalize の再実行はしない。DWM 属性の軽い再適用のみ。
	if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
#endif
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
	if (::IsWindow(m_hWnd)) {
		CRect cr;
		GetClientRect(&cr);
		const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
		if (capH > 0 && cr.Height() > capH)
			cr.top = capH;
		if (!cr.IsRectEmpty())
			CCC_InvalidateRectMinusOverlay(m_hWnd, cr);
	}
}
void CAnalyzerDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
	// 基底側で Apply/Refresh 済み。ここでは内容再描画のみ。
	if (bShow && CCC_IsAeroEnabled())
		Invalidate(FALSE);
#endif
}


void CAnalyzerDlg::OnClose()
{
	savedata.analyzerwindow = 0;
	DetachForDestroy();
	DestroyWindow();
}

void CAnalyzerDlg::OnDestroy()
{
	if (g_anHelpDlg && ::IsWindow(g_anHelpDlg->GetSafeHwnd()))
		g_anHelpDlg->DestroyWindow();
	CCustomBlurDialogExBase::OnDestroy();
}

void CAnalyzerDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CAnalyzerDlg::ShowHelpSheet()
{
	if (g_anHelpDlg && ::IsWindow(g_anHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_anHelpDlg, this);
		return;
	}
	if (g_anHelpDlg && !::IsWindow(g_anHelpDlg->GetSafeHwnd()))
		g_anHelpDlg = nullptr;
	CAnHelpDlg* dlg = new CAnHelpDlg(this);
	if (!dlg->Create(IDD_AN_HELP, this)) {
		delete dlg;
		return;
	}
	g_anHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CAnalyzerDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}
void CAnalyzerDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	if (m_rotDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_rotDragging = false;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			PersistSoft3DCams();
			return;
		}
		GdiSoft3D::Cam& cam = (m_rotPane == 0) ? m_camTop : m_camBot;
		GdiSoft3D::OrbitDrag(cam, m_rotYaw0, m_rotPitch0, m_rotOrigin, point);
		PersistSoft3DCams();
		KickUiPresent();
		return;
	}
	if (m_eqDrag) {
		if (!(nFlags & MK_LBUTTON)) {
			// キャプチャを取り逃した場合の保険
			m_eqDrag = false;
			m_eqDragBand = -1;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		}
		else {
			ApplyEqBandFromY(m_eqDragBand, m_eqDragPlot, point.y);
			if (m_eqDragBand >= 0 && m_eqDragBand < EQ_OVERLAY_BANDS) {
				// ドラッグ中は読取ボックスに帯域の Hz とゲインを出す
				m_hoverHz = kEqFreqs[m_eqDragBand];
				m_hoverDb = EqSliderToDb((std::min)(200, (std::max)(0, savedata.eq[m_eqDragBand])));
				m_hoverBin = -1;
				m_hoverPlot = m_eqDragPlot;
				m_hoverPlot.OffsetRect(0, -m_hoverSplitY);
				m_hoverValid = true;
				m_hoverChanged = true;
			}
			KickUiPresent();
			return;
		}
	}
	if (!m_trackingMouse) {
		TRACKMOUSEEVENT tme = { sizeof(tme) };
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		TrackMouseEvent(&tme);
		m_trackingMouse = true;
	}
	CRect rc; GetClientRect(&rc);
	m_hoverSplitY = rc.top + (int)(rc.Height() * 0.65);
	// フォーカス時のマウス洪水で全再描画しない。変化時のみ提示要求を合流。
	if (UpdateHoverFromPoint(point)) {
		m_hoverChanged = true;
		KickUiPresent();
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CAnalyzerDlg::OnMouseLeave()
{
	m_trackingMouse = false;
	if (m_hoverValid) {
		m_hoverValid = false;
		m_hoverBin = -1;
		m_hoverChanged = true;
		KickUiPresent();
	}
	CCustomBlurDialogExBase::OnMouseLeave();
}

void CAnalyzerDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	CRect rc; GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int contentTop = (capH > 0) ? capH : 0;
	const int contentH = rc.Height() - contentTop;
	const int split = contentTop + (int)(contentH * 0.65);
	if (point.y >= contentTop) {
		if (IsSoft3DTop() && point.y < split) {
			m_rotDragging = true;
			m_rotPane = 0;
			m_rotOrigin = point;
			m_rotYaw0 = m_camTop.yawDeg;
			m_rotPitch0 = m_camTop.pitchDeg;
			SetCapture();
			return;
		}
		if (IsSoft3DBot() && point.y >= split) {
			m_rotDragging = true;
			m_rotPane = 1;
			m_rotOrigin = point;
			m_rotYaw0 = m_camBot.yawDeg;
			m_rotPitch0 = m_camBot.pitchDeg;
			SetCapture();
			return;
		}
	}
	int band = -1;
	CRect plot;
	if (HitEqBand(point, band, plot)) {
		m_eqDrag = true;
		m_eqDragBand = band;
		m_eqDragPlot = plot;
		SetCapture();
		ApplyEqBandFromY(band, plot, point.y);
		return; // 基底のウィンドウドラッグへ渡さない
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

BOOL CAnalyzerDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
		if (pt.y >= capH) {
			if (m_eqDrag) {
				::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
				return TRUE;
			}
			int band = -1;
			CRect plot;
			if (HitEqBand(pt, band, plot)) {
				::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
				return TRUE;
			}
			CRect rc;
			GetClientRect(&rc);
			const int contentH = rc.Height() - capH;
			const int split = capH + (int)(contentH * 0.65);
			if (m_rotDragging || (IsSoft3DTop() && pt.y < split) || (IsSoft3DBot() && pt.y >= split)) {
				if (CCC_SetUiCursor(IDC_UI_GRAB))
					return TRUE;
			}
			if (CCC_SetUiCursor(IDC_UI_CROSS))
				return TRUE;
		}
	}
	return CCustomBlurDialogExBase::OnSetCursor(pWnd, nHitTest, message);
}

void CAnalyzerDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_rotDragging) {
		m_rotDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		PersistSoft3DCams();
		return;
	}
	if (m_eqDrag) {
		m_eqDrag = false;
		m_eqDragBand = -1;
		m_eqDragPlot.SetRectEmpty();
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		m_specDirty = true;
		KickUiPresent();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CAnalyzerDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(point);
	ResetPeakHold();
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CAnalyzerDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	CPoint client = pt;
	ScreenToClient(&client);
	CRect rc; GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int contentTop = (capH > 0) ? capH : 0;
	const int contentH = rc.Height() - contentTop;
	const int split = contentTop + (int)(contentH * 0.65);
	GdiSoft3D::Cam* cam = nullptr;
	if (IsSoft3DTop() && client.y >= contentTop && client.y < split)
		cam = &m_camTop;
	else if (IsSoft3DBot() && client.y >= split)
		cam = &m_camBot;
	if (!cam)
		return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
	GdiSoft3D::WheelZoom(*cam, zDelta);
	PersistSoft3DCams();
	KickUiPresent();
	return TRUE;
}

BOOL CAnalyzerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN) {
		bool textFocus = false;
		{
			wchar_t cls[32] = { 0 };
			HWND hFocus = ::GetFocus();
			if (hFocus && ::GetClassName(hFocus, cls, 32) > 0)
				textFocus = (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ComboBox") == 0);
		}
		// Ctrl+K = メディアプレイヤーのコマンドパレット
		if (!textFocus && (pMsg->wParam == 'K')
			&& (::GetKeyState(VK_CONTROL) & 0x8000)
			&& !(::GetKeyState(VK_MENU) & 0x8000)) {
			extern CMediaPlayerDlg* mp;
			if (mp && ::IsWindow(mp->GetSafeHwnd())) {
				mp->OpenCommandPalette();
				return TRUE;
			}
		}
		if (!textFocus
			&& (pMsg->wParam == '0' || pMsg->wParam == VK_NUMPAD0)
			&& !(::GetKeyState(VK_CONTROL) & 0x8000)
			&& !(::GetKeyState(VK_MENU) & 0x8000)) {
			bool did = false;
			if (IsSoft3DTop()) {
				savedata.analyzer3dyawTop = -220;
				savedata.analyzer3dpitchTop = 260;
				savedata.analyzer3dzoomTop = 100;
				did = true;
			}
			if (IsSoft3DBot()) {
				savedata.analyzer3dyawBot = -220;
				savedata.analyzer3dpitchBot = 260;
				savedata.analyzer3dzoomBot = 100;
				did = true;
			}
			if (did) {
				SyncSoft3DCamsFromSave();
				KickUiPresent();
				return TRUE;
			}
		}
		if (pMsg->wParam == VK_ESCAPE) {
			PostMessage(WM_CLOSE);
			return TRUE;
		}
		if (pMsg->wParam == 'F' || pMsg->wParam == 'f') {
			OnToggleFreeze();
			return TRUE;
		}
		if (pMsg->wParam == 'P' || pMsg->wParam == 'p') {
			OnTogglePeakHold();
			return TRUE;
		}
		if (pMsg->wParam == 'E' || pMsg->wParam == 'e') {
			OnToggleEqOverlay();
			return TRUE;
		}
		if (pMsg->wParam == 'M' || pMsg->wParam == 'm') {
			OnToggleLevelMeter();
			return TRUE;
		}
		if (pMsg->wParam == 'T' || pMsg->wParam == 't') {
			OnToggleAlwaysOnTop();
			return TRUE;
		}
		if (pMsg->wParam == 'W' || pMsg->wParam == 'w') {
			SetWaveMode((m_waveMode + 1) % WAVE_MODE_COUNT);
			return TRUE;
		}
		if (pMsg->wParam == 'S' || pMsg->wParam == 's') {
			SetLowerMode((m_lowerMode + 1) % LOWER_MODE_COUNT);
			return TRUE;
		}
		if (pMsg->wParam == 'D' || pMsg->wParam == 'd') {
			OnToggleSpecDiff();
			return TRUE;
		}
		if (pMsg->wParam == 'Z' || pMsg->wParam == 'z') {
			SetFreqZoom((m_freqZoom + 1) % 4);
			return TRUE;
		}
		if (pMsg->wParam == 'C' || pMsg->wParam == 'c') {
			if (::GetKeyState(VK_CONTROL) & 0x8000) {
				if (m_hoverValid)
					OnCopyHoverReadout();
				else
					OnCopyPeakFreq();
				return TRUE;
			}
			OnClearDisplay();
			return TRUE;
		}
		if (pMsg->wParam == VK_SPACE) {
			ResetPeakHold();
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
