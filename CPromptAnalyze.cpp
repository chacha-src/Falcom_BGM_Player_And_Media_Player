#include "stdafx.h"
#include "CPromptAnalyze.h"
#include "CPromptEngine.h"
#include "CMediaPlayerDlg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include <cmath>

extern COggDlg* og;
extern CPlayList* pl;
extern CMediaPlayerDlg* mp;
extern int plcnt;
extern int plf;
extern save savedata;

volatile LONG g_mpPromptAnalyzeOnly = 0;

namespace {

const double kHopSec = 0.05;       // 50ms
const int    kMaxFrames = 12000;   // ~10分
const int    kMaxOutChars = 14000; // 4分×200-400コマンド想定
const int    kMaxPending = 512 * 1024;

struct AnaState {
	float rms[kMaxFrames];
	float bass[kMaxFrames];
	float tre[kMaxFrames];
	int nFrames = 0;
	int rate = 44100;
	int ch = 2;
	int bits = 16;
	uint8_t pending[kMaxPending];
	int pendingN = 0;
	double pendingSamples = 0;
	BOOL active = FALSE;
};

AnaState g_ana;
float g_pctTmp[kMaxFrames];
int g_anaMode = MP_ANA_BALANCED;
double g_anaExpectSec = 0;
MpPromptAnalyzeProgressCb g_anaProgCb = nullptr;
void* g_anaProgUser = nullptr;
int g_anaLastPct = -1;

static void ReportProgress(int pct, LPCTSTR status)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	// 描画負荷を抑えるため 2% 刻み(完了・開始は常に通知)
	if (pct != 0 && pct != 100 && pct == g_anaLastPct) return;
	if (pct != 0 && pct != 100 && g_anaLastPct >= 0 && pct < g_anaLastPct + 2 && (status == nullptr || !*status))
		return;
	g_anaLastPct = pct;
	if (g_anaProgCb)
		g_anaProgCb(pct, status ? status : _T(""), g_anaProgUser);
}

static void AnaClear()
{
	g_ana.nFrames = 0;
	g_ana.pendingN = 0;
	g_ana.pendingSamples = 0;
	g_ana.rate = 44100;
	g_ana.ch = 2;
	g_ana.bits = 16;
}

static int BytesPerSample(int bits)
{
	if (bits == 24) return 3;
	if (bits == 32) return 4;
	return 2;
}

static float SampleAt(const uint8_t* p, int bits)
{
	if (bits == 32) {
		const float* f = (const float*)p;
		float v = *f;
		if (v < -1.f) v = -1.f;
		if (v > 1.f) v = 1.f;
		return v;
	}
	if (bits == 24) {
		int v = (int)((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16));
		if (v & 0x800000) v |= ~0xFFFFFF;
		return (float)v / 8388608.f;
	}
	const short* s = (const short*)p;
	return (float)(*s) / 32768.f;
}

static void AnaPushFrame(float rms, float bass, float tre)
{
	if (g_ana.nFrames >= kMaxFrames) return;
	g_ana.rms[g_ana.nFrames] = rms;
	g_ana.bass[g_ana.nFrames] = bass;
	g_ana.tre[g_ana.nFrames] = tre;
	g_ana.nFrames++;
	const double done = g_ana.nFrames * kHopSec;
	int pct = 5;
	if (g_anaExpectSec > 0.5) {
		double r = done / g_anaExpectSec;
		if (r > 1.0) r = 1.0;
		pct = 5 + (int)(r * 90.0 + 0.5);
	}
	else {
		double p2 = g_ana.nFrames / 20.0;
		if (p2 > 90.0) p2 = 90.0;
		pct = 5 + (int)(p2 + 0.5);
	}
	ReportProgress(pct, LL14(L"解析中…", L"Analyzing...", L"Analyse...", L"Analisi...", L"Analizando...", L"분석 중…", L"分析中…", L"Analyzing...", L"Анализ...", L"Analysiere...", L"Analisando...", L"Bezig met analyse...", L"Analiza...", L"Analiz..."));
}

static void AnaProcessBytes(const uint8_t* data, int nbytes)
{
	if (!data || nbytes <= 0) return;
	const int bps = BytesPerSample(g_ana.bits);
	const int chn = (g_ana.ch < 1) ? 1 : g_ana.ch;
	const int frameBytes = bps * chn;
	if (frameBytes <= 0) return;
	int hopSamples = (int)(g_ana.rate * kHopSec + 0.5);
	if (hopSamples < 1) hopSamples = 1;

	int copy = nbytes;
	if (g_ana.pendingN + copy > kMaxPending)
		copy = kMaxPending - g_ana.pendingN;
	if (copy > 0) {
		memcpy(g_ana.pending + g_ana.pendingN, data, copy);
		g_ana.pendingN += copy;
	}

	while (g_ana.pendingN >= hopSamples * frameBytes
		&& g_ana.nFrames < kMaxFrames) {
		double sumSq = 0;
		double sumDiff = 0;
		double sumHi = 0;
		float prev = 0;
		const uint8_t* base = g_ana.pending;
		for (int i = 0; i < hopSamples; ++i) {
			const uint8_t* fr = base + i * frameBytes;
			float mono = 0;
			for (int c = 0; c < chn; ++c)
				mono += SampleAt(fr + c * bps, g_ana.bits);
			mono /= (float)chn;
			sumSq += (double)mono * (double)mono;
			const float d = mono - prev;
			sumDiff += (double)d * (double)d;
			sumHi += (double)fabsf(d);
			prev = mono;
		}
		const float rms = (float)sqrt(sumSq / (double)hopSamples);
		const float bass = (float)sqrt(sumDiff / (double)hopSamples);
		const float tre = (float)(sumHi / (double)hopSamples);
		AnaPushFrame(rms, bass, tre);
		const int drop = hopSamples * frameBytes;
		const int rest = g_ana.pendingN - drop;
		if (rest > 0)
			memmove(g_ana.pending, g_ana.pending + drop, rest);
		g_ana.pendingN = rest;
	}
}

static CString FmtTime(double sec)
{
	if (sec < 0) sec = 0;
	const int s = (int)(sec + 0.5);
	CString t;
	if (s >= 60)
		t.Format(L"%d:%02d", s / 60, s % 60);
	else
		t.Format(L"%d", s);
	return t;
}

static CString CmdAt(LPCTSTR letters, double t0, double t1, int v0, int v1, BOOL hasVal, double periodSec = 0.0)
{
	CString s;
	if (periodSec > 0.001) {
		CString per = FmtTime(periodSec);
		CString o0 = FmtTime(t0);
		CString o1 = FmtTime(t1);
		if (!hasVal) {
			if (fabs(t1 - t0) < 0.25)
				s.Format(L"%%%s%s<%s>", letters, (LPCTSTR)per, (LPCTSTR)o0);
			else
				s.Format(L"%%%s%s<%s-%s>", letters, (LPCTSTR)per, (LPCTSTR)o0, (LPCTSTR)o1);
			return s;
		}
		if (fabs(t1 - t0) < 0.25)
			s.Format(L"%%%s%s<%s>[%d]", letters, (LPCTSTR)per, (LPCTSTR)o0, v0);
		else if (v0 == v1)
			s.Format(L"%%%s%s<%s-%s>[%d]", letters, (LPCTSTR)per, (LPCTSTR)o0, (LPCTSTR)o1, v0);
		else
			s.Format(L"%%%s%s<%s-%s>[%d-%d]", letters, (LPCTSTR)per, (LPCTSTR)o0, (LPCTSTR)o1, v0, v1);
		return s;
	}
	if (!hasVal) {
		if (fabs(t1 - t0) < 0.25)
			s.Format(L"@%s%s", letters, (LPCTSTR)FmtTime(t0));
		else
			s.Format(L"@%s%s-%s", letters, (LPCTSTR)FmtTime(t0), (LPCTSTR)FmtTime(t1));
		return s;
	}
	if (fabs(t1 - t0) < 0.25) {
		s.Format(L"@%s%s[%d]", letters, (LPCTSTR)FmtTime(t0), v0);
	}
	else if (v0 == v1) {
		s.Format(L"@%s%s-%s[%d]", letters, (LPCTSTR)FmtTime(t0), (LPCTSTR)FmtTime(t1), v0);
	}
	else {
		s.Format(L"@%s%s-%s[%d-%d]", letters, (LPCTSTR)FmtTime(t0), (LPCTSTR)FmtTime(t1), v0, v1);
	}
	return s;
}

// 解析時点の DS 音量(%)。パターン内の d 値は「この値=100」相対で書く。
// 例: 基準40%なら rel115 → 絶対46%（いきなり90%等に飛ばない）
static int g_dsBasePct = 100;

static int CurrentDsPercent()
{
	int ds = savedata.dsvol;
	if (og && ::IsWindow(og->m_dsval.GetSafeHwnd()))
		ds = og->m_dsval.GetPos();
	if (ds == 0) ds = 1;
	int pct = (int)(((double)ds + 499.0) * 2.0 / 10.0 + 0.5);
	if (pct < 1) pct = 1;
	if (pct > 100) pct = 100;
	return pct;
}

// rel: 100=現在のDS基準、115≈+15%（基準40なら約46）。絶対0..100に丸め。
static int ScaleDsRel(int rel)
{
	int v = (int)((double)g_dsBasePct * (double)rel / 100.0 + 0.5);
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	return v;
}

static CString CmdDs(double t0, double t1, int rel0, int rel1)
{
	return CmdAt(L"d", t0, t1, ScaleDsRel(rel0), ScaleDsRel(rel1), TRUE, 0.0);
}

static BOOL AppendCmd(CString& out, const CString& cmd)
{
	if (cmd.IsEmpty()) return TRUE;
	const int add = cmd.GetLength() + (out.IsEmpty() ? 0 : 1);
	if (out.GetLength() + add > kMaxOutChars)
		return FALSE;
	if (!out.IsEmpty()) out += L' ';
	out += cmd;
	return TRUE;
}

static float MeanRange(const float* v, int n, int a, int b)
{
	if (!v || n <= 0) return 0;
	if (a < 0) a = 0;
	if (b >= n) b = n - 1;
	if (b < a) return 0;
	double s = 0;
	for (int i = a; i <= b; ++i) s += v[i];
	return (float)(s / (double)(b - a + 1));
}

static float Percentile(const float* src, int n, float p)
{
	if (!src || n <= 0) return 0;
	for (int i = 0; i < n; ++i) g_pctTmp[i] = src[i];
	// 簡易クイックソート風: 選択法で十分速い (n<=12000)
	for (int a = 0; a < n - 1; ++a) {
		int m = a;
		for (int b = a + 1; b < n; ++b)
			if (g_pctTmp[b] < g_pctTmp[m]) m = b;
		if (m != a) {
			float tmp = g_pctTmp[a];
			g_pctTmp[a] = g_pctTmp[m];
			g_pctTmp[m] = tmp;
		}
		// 百分位に必要な位置まで来たら打ち切り
		const int need = (int)((n - 1) * p + 0.5f);
		if (a >= need) break;
	}
	int i = (int)((n - 1) * p + 0.5f);
	if (i < 0) i = 0;
	if (i >= n) i = n - 1;
	return g_pctTmp[i];
}

static BOOL IsHealMode(int mode)
{
	return mode == MP_ANA_HEALING || mode == MP_ANA_RELAX
		|| mode == MP_ANA_SLEEP || mode == MP_ANA_YASURAGI
		|| mode == MP_ANA_AMBIENT;
}

// ---- 密な自動アレンジ生成 (4分で概ね200-400コマンド) ----
// DS: CmdDs（現在DS=100相対）
// EQ帯/拡張(N/K/I/S等): 100=中立
// FX r/c/y (oggDlg_ds / StdAfx コメント準拠):
//   0=オフ / 1-100=モードA(通常) / 101-200=モードB(別アルゴリズム)
//   r:A=リバーブ B=パンリバーブ
//   c:A=コーラス B=コーラスディストーション
//   y:A=ディレイ B=マルチディレイ(ピンポン)
// 環境 E/F: 空間プリセットは音色を大きく変えるので区間を絞って使う(常用しない)

static int ClampI(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static int ModeBias(int mode, int base, int comedy, int serious, int romantic, int intense, int chill, int electro, int orch, int retro, int cine)
{
	switch (mode) {
	case MP_ANA_COMEDY: return base + comedy;
	case MP_ANA_SERIOUS: return base + serious;
	case MP_ANA_ROMANTIC: return base + romantic;
	case MP_ANA_INTENSE: return base + intense;
	case MP_ANA_CHILL: return base + chill;
	case MP_ANA_ELECTRO: return base + electro;
	case MP_ANA_ORCHESTRAL: return base + orch;
	case MP_ANA_RETRO: return base + retro;
	case MP_ANA_CINEMATIC: return base + cine;
	case MP_ANA_ACOUSTIC: return base + (chill + serious) / 3;
	case MP_ANA_VOCAL: return base + romantic / 2;
	case MP_ANA_CLUB: return base + (intense + electro) / 2;
	case MP_ANA_AMBIENT: return base + chill;
	case MP_ANA_LIVE: return base + (orch + intense) / 3;
	case MP_ANA_SOFTPOP: return base + romantic / 3;
	case MP_ANA_HEALING: return base + chill / 2 - 2;
	case MP_ANA_RELAX: return base + chill / 2 - 1;
	case MP_ANA_SLEEP: return base + chill - 4;
	case MP_ANA_YASURAGI: return base + (chill + serious) / 4 - 2;
	default: return base; // BALANCED
	}
}

// FX モードA: 1-100 (0=オフ)。strengthPct=0..100
static int FxA(int strengthPct)
{
	if (strengthPct <= 0) return 0;
	return ClampI(strengthPct, 1, 100);
}
// FX モードB: 101-200。strengthPct=1..100 → 101..200
static int FxB(int strengthPct)
{
	if (strengthPct <= 0) return 0;
	return 100 + ClampI(strengthPct, 1, 100);
}

// 同じ系統(両方A / 両方B / 片方0)なら補間OK。A↔B跨ぎは瞬時切替。
static BOOL FxSameFamily(int a, int b)
{
	if (a == 0 || b == 0) return TRUE;
	const BOOL aB = (a > 100);
	const BOOL bB = (b > 100);
	return aB == bB;
}

struct ModeFxPref {
	int envId;       // E: 0=使わない
	int envAmt;      // F: かかり 0-100
	int revA;        // 静かな所の通常リバーブ目安
	int revB;        // 特殊区間のパンリバーブ強さ(0=使わない)
	int choA;
	int choB;        // コーラスディスト
	int dlyA;
	int dlyB;        // マルチディレイ
	int clarity;     // N オフセット
	int density;     // I
	int spatial;     // S
	int balHi;       // K (100中立、大=高域寄り)
};

static ModeFxPref ModePrefs(int mode)
{
	// 環境番号は oggDlg_ds.cpp の環境音響コメントに合わせる
	ModeFxPref p = {};
	switch (mode) {
	case MP_ANA_COMEDY:
		p = { 33, 35, 25, 0, 40, 0, 35, 45, 6, 2, 4, 4 }; // カラオケ、マルチディレイ多め
		break;
	case MP_ANA_SERIOUS:
		p = { 3, 55, 55, 25, 15, 0, 10, 0, -2, 6, 2, -4 }; // 教会、残響厚め・低域寄り
		break;
	case MP_ANA_ROMANTIC:
		p = { 32, 45, 50, 20, 45, 0, 15, 0, 4, 4, 8, 2 }; // ジャズクラブ、コーラスA+立体
		break;
	case MP_ANA_INTENSE:
		p = { 6, 40, 20, 0, 25, 40, 30, 35, 8, 8, 4, 4 }; // ライブハウス、ディスト+マルチ
		break;
	case MP_ANA_CHILL:
		p = { 7, 40, 45, 15, 30, 0, 10, 0, -2, 0, 4, -2 }; // 森、穏やかリバーブ
		break;
	case MP_ANA_ELECTRO:
		p = { 81, 50, 15, 30, 20, 45, 25, 55, 6, 4, 10, 6 }; // サイバーパンク、B系多め
		break;
	case MP_ANA_ORCHESTRAL:
		p = { 31, 60, 65, 30, 10, 0, 8, 0, 0, 8, 10, 0 }; // コンサートホール、パンリバーブ可
		break;
	case MP_ANA_RETRO:
		p = { 62, 40, 35, 0, 50, 25, 20, 0, -2, 2, 2, -2 }; // 古い劇場、コーラス系
		break;
	case MP_ANA_CINEMATIC:
		p = { 34, 55, 45, 40, 20, 0, 15, 20, 2, 6, 8, 0 }; // 映画館、パンリバーブ活用
		break;
	case MP_ANA_ACOUSTIC:
		p = { 7, 30, 40, 0, 15, 0, 8, 0, 2, 2, 4, -2 };
		break;
	case MP_ANA_VOCAL:
		p = { 32, 25, 35, 0, 25, 0, 12, 0, 8, 0, 2, 2 };
		break;
	case MP_ANA_CLUB:
		p = { 81, 40, 15, 20, 20, 35, 30, 50, 6, 6, 8, 4 };
		break;
	case MP_ANA_AMBIENT:
		p = { 7, 50, 55, 10, 20, 0, 5, 0, -4, 2, 6, -4 };
		break;
	case MP_ANA_LIVE:
		p = { 6, 45, 40, 25, 20, 0, 15, 0, 4, 4, 8, 2 };
		break;
	case MP_ANA_SOFTPOP:
		p = { 32, 30, 35, 0, 30, 0, 12, 0, 2, 2, 4, 0 };
		break;
	case MP_ANA_HEALING:
		p = { 7, 45, 50, 0, 20, 0, 5, 0, -2, 2, 4, -4 };
		break;
	case MP_ANA_RELAX:
		p = { 7, 35, 45, 0, 15, 0, 4, 0, -2, 0, 2, -2 };
		break;
	case MP_ANA_SLEEP:
		p = { 3, 40, 55, 0, 10, 0, 0, 0, -6, 0, 0, -6 };
		break;
	case MP_ANA_YASURAGI:
		p = { 7, 40, 50, 0, 18, 0, 4, 0, -2, 2, 4, -4 };
		break;
	default: // BALANCED
		p = { 0, 0, 30, 0, 20, 0, 18, 0, 0, 0, 2, 0 };
		break;
	}
	return p;
}

static CString BuildFromPatterns(int mode)
{
	CString out;
	if (g_ana.nFrames < 10) return out;

	const int n = g_ana.nFrames;
	const double dur = n * kHopSec;
	const float med = Percentile(g_ana.rms, n, 0.5f);
	const float p75 = Percentile(g_ana.rms, n, 0.75f);
	const float p90 = Percentile(g_ana.rms, n, 0.90f);
	const float medBass = Percentile(g_ana.bass, n, 0.5f);
	const float medTre = Percentile(g_ana.tre, n, 0.5f);
	g_dsBasePct = CurrentDsPercent();
	const ModeFxPref pref = ModePrefs(mode);
	const BOOL heal = IsHealMode(mode);

	// RMS 自己相関風の簡易周期推定 (30〜90秒帯)
	double breathPeriod = 0.0;
	{
		const int lagMin = (int)(30.0 / kHopSec);
		const int lagMax = (int)(90.0 / kHopSec);
		if (n > lagMax + 20 && lagMin < lagMax) {
			double bestCorr = 0.0;
			int bestLag = 0;
			const int step = (n > 4000) ? 4 : 2;
			for (int lag = lagMin; lag <= lagMax && lag < n; lag += step) {
				double num = 0, d0 = 0, d1 = 0;
				const int lim = n - lag;
				const int stride = (lim > 800) ? 3 : 1;
				for (int i = 0; i < lim; i += stride) {
					const double a = (double)g_ana.rms[i] - (double)med;
					const double b = (double)g_ana.rms[i + lag] - (double)med;
					num += a * b;
					d0 += a * a;
					d1 += b * b;
				}
				if (d0 > 1e-12 && d1 > 1e-12) {
					const double c = num / sqrt(d0 * d1);
					if (c > bestCorr) { bestCorr = c; bestLag = lag; }
				}
			}
			if (bestCorr > 0.18 && bestLag > 0)
				breathPeriod = bestLag * kHopSec;
		}
		if (heal && breathPeriod < 45.0)
			breathPeriod = 60.0;
		else if (!heal && breathPeriod > 0.0 && breathPeriod < 30.0)
			breathPeriod = 0.0;
	}
	const BOOL hasPct = (breathPeriod >= 30.0);

	{
		CString note;
		note.Format(L"# auto-analyze mode=%d %.1fs frames=%d dsBase=%dpct dense fxAB pct=%s\r\n",
			mode, dur, n, g_dsBasePct, hasPct ? L"1" : L"0");
		out = note;
	}

	double stepSec = 0.75;
	if (dur > 30.0) {
		double targetSeg = dur * 1.15;
		if (targetSeg < 180.0) targetSeg = 180.0;
		if (targetSeg > 380.0) targetSeg = 380.0;
		stepSec = dur / targetSeg;
		if (stepSec < 0.55) stepSec = 0.55;
		if (stepSec > 1.05) stepSec = 1.05;
	}
	int hop = (int)(stepSec / kHopSec + 0.5);
	if (hop < 6) hop = 6;

	// イントロ/アウトロ (DSのみ)
	{
		int head = n / 8;
		const int headMax = (int)(4.0 / kHopSec);
		if (head > headMax) head = headMax;
		if (head > 4 && MeanRange(g_ana.rms, n, 0, head) < med * 0.7f)
			AppendCmd(out, CmdDs(0, head * kHopSec, heal ? 75 : 60, 100));
		int tail = n / 6;
		const int tailMax = (int)(8.0 / kHopSec);
		if (tail > tailMax) tail = tailMax;
		if (tail > 4 && MeanRange(g_ana.rms, n, n - tail, n - 1) < med * 0.75f)
			AppendCmd(out, CmdDs((n - tail) * kHopSec, (n - 1) * kHopSec, 100, heal ? 70 : 50));
	}

	// セクション演出 + 環境
	{
		const int q1 = n / 4, mid = n / 2, q3 = n * 3 / 4;
		int midEnd = mid + hop;
		if (midEnd > n - 1) midEnd = n - 1;
		const float loudMid = MeanRange(g_ana.rms, n, mid, midEnd);
		const float loudQ1 = MeanRange(g_ana.rms, n, q1, q1 + hop);
		if (!heal && loudMid > p75)
			AppendCmd(out, CmdAt(L"br", mid * kHopSec, mid * kHopSec, 0, 0, FALSE));
		else if (loudQ1 < med * 0.85f || heal)
			AppendCmd(out, CmdAt(L"sb", q1 * kHopSec, q1 * kHopSec, 0, 0, FALSE));
		if (!heal && (mode == MP_ANA_INTENSE || mode == MP_ANA_ELECTRO || mode == MP_ANA_CLUB
			|| (p75 > med * 1.2f && med > 0.03f)))
			AppendCmd(out, CmdAt(L"fa", mid * kHopSec, mid * kHopSec, 0, 0, FALSE));
		else if (heal || mode == MP_ANA_CHILL || mode == MP_ANA_SERIOUS || mode == MP_ANA_SLEEP
			|| (med < 0.05f && p90 < med * 1.4f))
			AppendCmd(out, CmdAt(L"sl", q1 * kHopSec, q1 * kHopSec, 0, 0, FALSE));

		if (pref.envId > 0 && pref.envAmt > 0) {
			const double te0 = q1 * kHopSec;
			const double te1 = q3 * kHopSec;
			AppendCmd(out, CmdAt(L"E", te0, te0, pref.envId, pref.envId, TRUE));
			AppendCmd(out, CmdAt(L"F", te0, te0 + 2.0, 0, pref.envAmt, TRUE));
			AppendCmd(out, CmdAt(L"F", te1 - 2.0, te1, pref.envAmt, 0, TRUE));
			AppendCmd(out, CmdAt(L"E", te1, te1, 0, 0, TRUE));
		}
	}

	// 周期呼吸 % (本線の後でも文字が残っていれば追記。先頭付近に置く)
	if (hasPct && out.GetLength() < kMaxOutChars - 200) {
		const double per = breathPeriod;
		double o0 = per * 0.30;
		double o1 = per * 0.55;
		if (heal) { o0 = per * 0.25; o1 = per * 0.55; }
		int n0 = 100 + pref.clarity;
		int n1 = n0 + (heal ? 4 : 8);
		int s0 = 100 + pref.spatial;
		int s1 = s0 + (heal ? 3 : 6);
		n0 = ClampI(n0, 92, 118);
		n1 = ClampI(n1, 94, 124);
		s0 = ClampI(s0, 94, 120);
		s1 = ClampI(s1, 96, 128);
		AppendCmd(out, CmdAt(L"N", o0, o1, n0, n1, TRUE, per));
		AppendCmd(out, CmdAt(L"S", o0 + 2.0, o1, s0, s1, TRUE, per));
		if (!heal && pref.dlyA > 0)
			AppendCmd(out, CmdAt(L"y", o0, o0 + 3.0, 0, FxA(ClampI(pref.dlyA / 2, 8, 35)), TRUE, per));
		if (heal && pref.envAmt > 10)
			AppendCmd(out, CmdAt(L"F", o0, o1, ClampI(pref.envAmt / 3, 5, 30),
				ClampI(pref.envAmt * 2 / 3, 10, 45), TRUE, per));
	}

	int prevN = 100, prevK = 100, prevI = 100, prevS = 100;
	int prevR = 0, prevC = 0, prevY = 0;
	int prevA = 100, prevO = 100, prevG = 100, prevIi = 100, prevL = 100;
	int prevT = 100, prevP = 100;
	int prevD = 100;
	double tLaneN = 0, tLaneK = 0, tLaneI = 0, tLaneS = 0;
	double tLaneR = 0, tLaneC = 0, tLaneY = 0;
	double tLaneA = 0, tLaneO = 0, tLaneG = 0, tLaneIi = 0, tLaneL = 0;
	double tLaneT = 0, tLaneP = 0, tLaneD = 0;
	int punchBudget = (int)(dur / 10.0);
	if (punchBudget < 6) punchBudget = 6;
	if (heal) punchBudget = 0;
	else if (mode == MP_ANA_SLEEP) punchBudget = 0;

	auto flushEq = [&](LPCTSTR letter, double& t0, int& lastV, double t1, int newV) {
		if (newV == lastV) return;
		if (t1 <= t0 + 0.2) { lastV = newV; return; }
		AppendCmd(out, CmdAt(letter, t0, t1, lastV, newV, TRUE));
		t0 = t1;
		lastV = newV;
	};
	auto flushFx = [&](LPCTSTR letter, double& t0, int& lastV, double t1, int newV) {
		if (newV == lastV) return;
		if (t1 <= t0 + 0.15) {
			AppendCmd(out, CmdAt(letter, t1, t1, newV, newV, TRUE));
			t0 = t1;
			lastV = newV;
			return;
		}
		if (!FxSameFamily(lastV, newV)) {
			AppendCmd(out, CmdAt(letter, t1, t1, newV, newV, TRUE));
			t0 = t1;
			lastV = newV;
			return;
		}
		AppendCmd(out, CmdAt(letter, t0, t1, lastV, newV, TRUE));
		t0 = t1;
		lastV = newV;
	};
	auto flushDs = [&](double& t0, int& lastV, double t1, int newV) {
		if (newV == lastV) return;
		if (t1 <= t0 + 0.2) { lastV = newV; return; }
		AppendCmd(out, CmdDs(t0, t1, lastV, newV));
		t0 = t1;
		lastV = newV;
	};

	for (int i = 0; i + hop <= n; i += hop) {
		if (out.GetLength() > kMaxOutChars - 100) break;
		int i1 = i + hop - 1;
		if (i1 > n - 1) i1 = n - 1;
		const double t0 = i * kHopSec;
		const double t1 = (i1 + 1) * kHopSec;
		const float m = MeanRange(g_ana.rms, n, i, i1);
		const float b = MeanRange(g_ana.bass, n, i, i1);
		const float tr = MeanRange(g_ana.tre, n, i, i1);
		const int seg = i / hop;
		const BOOL quiet = (m < med * 0.7f);
		const BOOL loud = (m > p75);
		const BOOL peak = (m > p90 && p90 > 0.01f);

		int wantN = 100 + pref.clarity;
		int wantK = 100 + pref.balHi;
		int wantI = 100 + pref.density;
		int wantS = 100 + pref.spatial;
		int wantA = 100, wantO = 100, wantG = 100, wantIi = 100, wantL = 100;
		int wantT = 100, wantP = 100, wantD = 100;
		int wantR = 0, wantC = 0, wantY = 0;

		if (quiet) {
			wantN = ClampI(wantN - 4, 88, 120);
			wantI = ClampI(wantI + 2, 90, 125);
			wantR = FxA(ModeBias(mode, pref.revA, -10, 10, 8, -8, 6, -10, 12, 0, 8));
			wantC = FxA(ModeBias(mode, pref.choA, 0, -5, 10, -5, 4, -5, -8, 8, 0));
			if (!heal && pref.revB > 0 && (mode == MP_ANA_ORCHESTRAL || mode == MP_ANA_CINEMATIC || mode == MP_ANA_SERIOUS || mode == MP_ANA_LIVE))
				wantR = FxB(ClampI(pref.revB - 5, 10, 50));
		} else if (loud) {
			wantN = ClampI(wantN + (heal ? 2 : 6), 92, 128);
			wantS = ClampI(wantS + (heal ? 2 : 4), 95, 130);
			wantY = heal ? 0 : FxA(ModeBias(mode, pref.dlyA + 10, 8, -5, -5, 10, -8, 12, -5, 0, 0));
			wantC = FxA(ModeBias(mode, pref.choA / 2 + 10, 5, 0, 8, 5, 0, 5, 0, 10, 0));
			if (!heal && pref.choB > 0 && (mode == MP_ANA_INTENSE || mode == MP_ANA_ELECTRO || mode == MP_ANA_RETRO || mode == MP_ANA_CLUB))
				wantC = FxB(ClampI(pref.choB, 15, 55));
			if (!heal && pref.dlyB > 0 && (mode == MP_ANA_ELECTRO || mode == MP_ANA_COMEDY || mode == MP_ANA_INTENSE || mode == MP_ANA_CLUB) && peak)
				wantY = FxB(ClampI(pref.dlyB, 20, 60));
			if (!heal && pref.revB > 0 && (mode == MP_ANA_CINEMATIC || mode == MP_ANA_ORCHESTRAL || mode == MP_ANA_LIVE) && peak)
				wantR = FxB(ClampI(pref.revB, 15, 55));
			else if (!peak)
				wantR = FxA(ModeBias(mode, pref.revA / 2, 0, 5, 5, 0, 5, 0, 8, 0, 5));
		} else {
			wantR = FxA(ClampI(pref.revA / 2, 0, 40));
			wantC = FxA(ClampI(pref.choA / 2, 0, 35));
		}

		if (b > medBass * 1.15f) {
			wantA = ModeBias(mode, 112, 0, 2, 0, 6, 0, 4, 4, 8, 2);
			wantG = ClampI(wantG + 4, 100, 116);
			wantK = ClampI(wantK - 4, 88, 115);
		}
		if (tr > medTre * 1.15f) {
			wantO = ModeBias(mode, heal ? 104 : 112, 2, 0, 2, 4, 0, 6, 2, 2, 4);
			wantIi = ClampI(wantIi + 3, 100, 116);
			wantL = ClampI(wantL + (heal ? 0 : 4), 100, 118);
			wantN = ClampI(wantN + (heal ? 1 : 4), 90, 130);
			wantS = ClampI(wantS + (heal ? 1 : 4), 95, 135);
			wantK = ClampI(wantK + 3, 90, 120);
		}

		if (loud && seg > 1) {
			wantT = ModeBias(mode, heal ? 98 : 106, 4, -6, 0, 10, -8, 10, -2, 2, 4);
			wantP = ModeBias(mode, heal ? 100 : 104, 6, -2, 2, 4, 0, 2, 0, -4, 2);
		} else if (quiet) {
			wantT = ModeBias(mode, heal ? 94 : 96, 0, -4, -2, 0, -6, 0, -2, 0, -2);
		}

		if (peak && !heal)
			wantD = ModeBias(mode, 108, 2, -2, 0, 8, -4, 4, 0, 2, 0);
		else if (quiet)
			wantD = ModeBias(mode, heal ? 96 : 94, 0, -2, -2, 0, -4, 0, 0, 0, -2);

		if (mode == MP_ANA_BALANCED) {
			wantN = ClampI(100 + (wantN - 100) / 2, 94, 112);
			wantK = ClampI(100 + (wantK - 100) / 2, 94, 108);
			wantI = ClampI(100 + (wantI - 100) / 2, 94, 110);
			wantS = ClampI(100 + (wantS - 100) / 2, 96, 112);
			if (wantR > 100) wantR = FxA(30);
			if (wantC > 100) wantC = FxA(25);
			if (wantY > 100) wantY = FxA(25);
			wantR = ClampI(wantR, 0, 45);
			wantC = ClampI(wantC, 0, 35);
			wantY = ClampI(wantY, 0, 40);
		}
		if (heal) {
			wantN = ClampI(wantN, 92, 112);
			wantK = ClampI(wantK, 90, 108);
			wantI = ClampI(wantI, 94, 112);
			wantS = ClampI(wantS, 94, 114);
			wantT = ClampI(wantT, 88, 100);
			wantP = ClampI(wantP, 96, 104);
			wantD = ClampI(wantD, 85, 105);
			wantO = ClampI(wantO, 100, 108);
			wantL = ClampI(wantL, 100, 108);
			if (wantR > 100) wantR = FxA(40);
			if (wantC > 100) wantC = FxA(20);
			wantY = 0;
			wantR = ClampI(wantR, 0, 60);
			wantC = ClampI(wantC, 0, 30);
		}
		if (mode == MP_ANA_SLEEP) {
			wantT = ClampI(wantT, 88, 96);
			wantO = ClampI(wantO, 100, 104);
			wantN = ClampI(wantN, 92, 106);
		}
		if (mode == MP_ANA_VOCAL) {
			wantN = ClampI(wantN + 4, 96, 128);
			wantIi = ClampI(wantIi + 4, 100, 120);
		}
		if (mode == MP_ANA_RELAX) {
			wantS = ClampI(wantS, 94, 108);
			wantC = ClampI(wantC, 0, 22);
			wantD = ClampI(100 + (wantD - 100) / 2, 90, 104);
		}

		wantN = ClampI(wantN, 88, 130);
		wantK = ClampI(wantK, 88, 120);
		wantI = ClampI(wantI, 88, 128);
		wantS = ClampI(wantS, 90, 135);
		wantA = ClampI(wantA, 100, 120);
		wantO = ClampI(wantO, 100, 120);
		wantG = ClampI(wantG, 100, 118);
		wantIi = ClampI(wantIi, 100, 118);
		wantL = ClampI(wantL, 100, 118);
		wantT = ClampI(wantT, 88, 118);
		wantP = ClampI(wantP, 90, 112);
		wantD = ClampI(wantD, 75, 112);
		wantR = ClampI(wantR, 0, 200);
		wantC = ClampI(wantC, 0, 200);
		wantY = ClampI(wantY, 0, 200);

		const int phase = seg % 8;
		if (phase == 0 || abs(wantN - prevN) >= 4)
			flushEq(L"N", tLaneN, prevN, t1, wantN);
		if (phase == 1 || abs(wantK - prevK) >= 4)
			flushEq(L"K", tLaneK, prevK, t1, wantK);
		if (phase == 2 || abs(wantI - prevI) >= 4)
			flushEq(L"I", tLaneI, prevI, t1, wantI);
		if (phase == 3 || abs(wantS - prevS) >= 5)
			flushEq(L"S", tLaneS, prevS, t1, wantS);
		if (abs(wantR - prevR) >= 8)
			flushFx(L"r", tLaneR, prevR, t1, wantR);
		if (abs(wantC - prevC) >= 8)
			flushFx(L"c", tLaneC, prevC, t1, wantC);
		if (abs(wantY - prevY) >= 8)
			flushFx(L"y", tLaneY, prevY, t1, wantY);
		if (abs(wantA - prevA) >= 4)
			flushEq(L"a", tLaneA, prevA, t1, wantA);
		if (abs(wantO - prevO) >= 4)
			flushEq(L"o", tLaneO, prevO, t1, wantO);
		if (phase == 4 || abs(wantG - prevG) >= 4)
			flushEq(L"g", tLaneG, prevG, t1, wantG);
		if (phase == 5 || abs(wantIi - prevIi) >= 4)
			flushEq(L"i", tLaneIi, prevIi, t1, wantIi);
		if (phase == 6 || abs(wantL - prevL) >= 4)
			flushEq(L"l", tLaneL, prevL, t1, wantL);
		if (abs(wantT - prevT) >= 3)
			flushEq(L"t", tLaneT, prevT, t1, wantT);
		if (abs(wantP - prevP) >= 3)
			flushEq(L"p", tLaneP, prevP, t1, wantP);
		if (abs(wantD - prevD) >= 4)
			flushDs(tLaneD, prevD, t1, wantD);

		if (punchBudget > 0 && i > 2 && i + 2 < n) {
			const float thr = med + (p90 - med) * 0.55f;
			if (g_ana.rms[i] > thr && g_ana.rms[i] > g_ana.rms[i - 1] * 1.2f) {
				int yPunch = 0;
				if (mode == MP_ANA_ELECTRO || mode == MP_ANA_INTENSE || mode == MP_ANA_COMEDY || mode == MP_ANA_CLUB)
					yPunch = FxB(ModeBias(mode, 35, 10, 0, 0, 15, 0, 20, 0, 5, 5));
				else
					yPunch = FxA(ModeBias(mode, 45, 10, 0, 0, 15, -10, 10, 0, 5, 0));
				AppendCmd(out, CmdAt(L"y", t0, t0 + 0.5, 0, yPunch, TRUE));
				AppendCmd(out, CmdAt(L"y", t0 + 0.5, t0 + 1.1, yPunch, 0, TRUE));
				prevY = 0;
				tLaneY = t0 + 1.1;
				--punchBudget;
			}
		}

		if (hop >= 9 && !heal) {
			const float a0 = MeanRange(g_ana.rms, n, i, i + hop / 3);
			const float a1 = MeanRange(g_ana.rms, n, i + hop * 2 / 3, i1);
			if (a0 > 0.008f && a1 > a0 * 1.4f && a1 > med) {
				AppendCmd(out, CmdAt(L"t", t0, t1, 100, ModeBias(mode, 108, 2, -2, 0, 12, -4, 10, 0, 2, 4), TRUE));
				AppendCmd(out, CmdAt(L"N", t0, t1, 100, ModeBias(mode, 114, 2, 0, 2, 8, 0, 6, 2, 2, 4), TRUE));
			}
		}
	}

	const double tend = (n - 1) * kHopSec;
	flushEq(L"N", tLaneN, prevN, tend, 100);
	flushEq(L"K", tLaneK, prevK, tend, 100);
	flushEq(L"I", tLaneI, prevI, tend, 100);
	flushEq(L"S", tLaneS, prevS, tend, 100);
	flushFx(L"r", tLaneR, prevR, tend, 0);
	flushFx(L"c", tLaneC, prevC, tend, 0);
	flushFx(L"y", tLaneY, prevY, tend, 0);
	flushEq(L"a", tLaneA, prevA, tend, 100);
	flushEq(L"o", tLaneO, prevO, tend, 100);
	flushEq(L"g", tLaneG, prevG, tend, 100);
	flushEq(L"i", tLaneIi, prevIi, tend, 100);
	flushEq(L"l", tLaneL, prevL, tend, 100);
	flushEq(L"t", tLaneT, prevT, tend, 100);
	flushEq(L"p", tLaneP, prevP, tend, 100);
	flushDs(tLaneD, prevD, tend, 100);

	out.Trim();
	return out;
}

} // namespace

CString MpPromptAnalyzeModeName(int mode)
{
	mode = MpPromptAnalyzeModeClamp(mode);
	switch (mode) {
	case MP_ANA_COMEDY: return LL14(L"お笑い系", L"Comedy", L"Comedie", L"Commedia", L"Comedia", L"코미디", L"喜剧风", L"Comedy", L"Комедия", L"Komodie", L"Comedia", L"Komedie", L"Komedia", L"Komedi");
	case MP_ANA_SERIOUS: return LL14(L"シリアス系", L"Serious", L"Serieux", L"Serio", L"Serio", L"시리어스", L"严肃风", L"Serious", L"Серьёзный", L"Ernst", L"Serio", L"Serieus", L"Powazny", L"Ciddi");
	case MP_ANA_ROMANTIC: return LL14(L"ロマンチック", L"Romantic", L"Romantique", L"Romantico", L"Romantico", L"로맨틱", L"浪漫", L"Romantic", L"Романтика", L"Romantisch", L"Romantico", L"Romantisch", L"Romantyczny", L"Romantik");
	case MP_ANA_INTENSE: return LL14(L"激しめ", L"Intense", L"Intense", L"Intenso", L"Intenso", L"강렬", L"激烈", L"Intense", L"Интенсив", L"Intensiv", L"Intenso", L"Intens", L"Intensywny", L"Yogun");
	case MP_ANA_CHILL: return LL14(L"チル", L"Chill", L"Chill", L"Chill", L"Chill", L"칠", L"放松", L"Chill", L"Чилл", L"Chill", L"Chill", L"Chill", L"Chill", L"Chill");
	case MP_ANA_ELECTRO: return LL14(L"エレクトロ", L"Electro", L"Electro", L"Electro", L"Electro", L"일렉트로", L"电子", L"Electro", L"Электро", L"Electro", L"Electro", L"Electro", L"Electro", L"Elektro");
	case MP_ANA_ORCHESTRAL: return LL14(L"オーケストラ風", L"Orchestral", L"Orchestral", L"Orchestrale", L"Orquestal", L"오케스트라", L"管弦乐风", L"Orchestral", L"Оркестр", L"Orchester", L"Orquestral", L"Orkestraal", L"Orkiestrowy", L"Orkestral");
	case MP_ANA_RETRO: return LL14(L"レトロ", L"Retro", L"Retro", L"Retro", L"Retro", L"레트로", L"复古", L"Retro", L"Ретро", L"Retro", L"Retro", L"Retro", L"Retro", L"Retro");
	case MP_ANA_CINEMATIC: return LL14(L"シネマティック", L"Cinematic", L"Cinematique", L"Cinematografico", L"Cinematografico", L"시네마틱", L"电影感", L"Cinematic", L"Кино", L"Filmisch", L"Cinematico", L"Cinematisch", L"Filmowy", L"Sinematik");
	case MP_ANA_ACOUSTIC: return LL14(L"アコースティック", L"Acoustic", L"Acoustique", L"Acustico", L"Acustico", L"어쿠스틱", L"原声", L"Acoustic", L"Акустика", L"Akustisch", L"Acustico", L"Akoestisch", L"Akustyczny", L"Akustik");
	case MP_ANA_VOCAL: return LL14(L"ボーカルフォーカス", L"Vocal Focus", L"Focus vocal", L"Focus vocale", L"Enfoque vocal", L"보컬 포커스", L"人声焦点", L"Vocal Focus", L"Вокал", L"Gesangsfokus", L"Foco vocal", L"Vocale focus", L"Focus wokalny", L"Vokal odak");
	case MP_ANA_CLUB: return LL14(L"クラブ/ダンス", L"Club/Dance", L"Club/Dance", L"Club/Dance", L"Club/Dance", L"클럽/댄스", L"俱乐部/舞曲", L"Club/Dance", L"Клуб/Дэнс", L"Club/Dance", L"Club/Dance", L"Club/Dance", L"Club/Dance", L"Kulup/Dans");
	case MP_ANA_AMBIENT: return LL14(L"アンビエント", L"Ambient", L"Ambiant", L"Ambient", L"Ambient", L"앰비언트", L"氛围", L"Ambient", L"Эмбиент", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient");
	case MP_ANA_LIVE: return LL14(L"ライブステージ", L"Live Stage", L"Scene live", L"Palco live", L"Escenario en vivo", L"라이브 스테이지", L"现场舞台", L"Live Stage", L"Живая сцена", L"Live-Buehne", L"Palco ao vivo", L"Live-podium", L"Scena na zywo", L"Canli sahne");
	case MP_ANA_SOFTPOP: return LL14(L"ソフトポップ", L"Soft Pop", L"Soft Pop", L"Soft Pop", L"Soft Pop", L"소프트 팝", L"柔和流行", L"Soft Pop", L"Софт-поп", L"Soft Pop", L"Soft Pop", L"Soft Pop", L"Soft Pop", L"Soft Pop");
	case MP_ANA_HEALING: return LL14(L"ヒーリング", L"Healing", L"Guerison", L"Guarigione", L"Sanacion", L"힐링", L"疗愈", L"Healing", L"Исцеление", L"Heilung", L"Cura", L"Heling", L"Uzdrawianie", L"Iyilestirme");
	case MP_ANA_RELAX: return LL14(L"リラックス", L"Relax", L"Detente", L"Relax", L"Relax", L"릴랙스", L"放松", L"Relax", L"Релакс", L"Entspannen", L"Relaxar", L"Ontspannen", L"Relaks", L"Rahatla");
	case MP_ANA_SLEEP: return LL14(L"スリープ", L"Sleep", L"Sommeil", L"Sonno", L"Sueno", L"슬립", L"助眠", L"Sleep", L"Сон", L"Schlaf", L"Sono", L"Slaap", L"Sen", L"Uyku");
	case MP_ANA_YASURAGI: return LL14(L"やすらぎ", L"Serenity", L"Serenite", L"Serenita", L"Serenidad", L"평온", L"安详", L"Serenity", L"Спокойствие", L"Gelassenheit", L"Serenidade", L"Sereniteit", L"Spokoj", L"Huzur");
	default: return LL14(L"バランス", L"Balanced", L"Equilibre", L"Bilanciato", L"Equilibrado", L"밸런스", L"均衡", L"Balanced", L"Баланс", L"Ausgewogen", L"Equilibrado", L"Gebalanceerd", L"Zrownowazony", L"Dengeli");
	}
}

int MpPromptAnalyzeModeClamp(int mode)
{
	if (mode < 0 || mode >= MP_ANA_MODE_COUNT) return MP_ANA_BALANCED;
	return mode;
}

void MpPromptAnalyzeSetProgressCb(MpPromptAnalyzeProgressCb cb, void* user)
{
	g_anaProgCb = cb;
	g_anaProgUser = user;
}

void MpPromptAnalyzeSetExpectedDurationSec(double sec)
{
	if (sec < 0) sec = 0;
	if (sec > 60 * 30) sec = 60 * 30;
	g_anaExpectSec = sec;
}

BOOL MpPromptAnalyzeIsActive()
{
	return g_ana.active ? TRUE : FALSE;
}

void MpPromptAnalyzeBegin()
{
	AnaClear();
	g_anaLastPct = -1;
	g_ana.active = TRUE;
	InterlockedExchange(&g_mpPromptAnalyzeOnly, 1);
	ReportProgress(1, LL14(L"読込開始…", L"Loading...", L"Chargement...", L"Caricamento...", L"Cargando...", L"로딩…", L"加载中…", L"Loading...", L"Загрузка...", L"Laden...", L"Carregando...", L"Laden...", L"Wczytywanie...", L"Yukleniyor..."));
}

void MpPromptAnalyzeAbort()
{
	g_ana.active = FALSE;
	InterlockedExchange(&g_mpPromptAnalyzeOnly, 0);
	AnaClear();
	g_anaExpectSec = 0;
	ReportProgress(0, _T(""));
}

void MpPromptAnalyzeFeed(const void* p, UINT n, int rate, int ch, int bits)
{
	if (!g_ana.active || !p || n == 0) return;
	if (rate >= 8000) g_ana.rate = rate;
	if (ch >= 1) g_ana.ch = ch;
	if (bits == 16 || bits == 24 || bits == 32) g_ana.bits = bits;
	AnaProcessBytes((const uint8_t*)p, (int)n);
}

BOOL MpPromptAnalyzeSelected(CString& outText, int mode, CString* errMsg)
{
	outText.Empty();
	mode = MpPromptAnalyzeModeClamp(mode);
	g_anaMode = mode;
	if (!og || !::IsWindow(og->GetSafeHwnd())) {
		if (errMsg) *errMsg = LL14(L"再生エンジンがありません。", L"Playback engine missing.", L"Moteur absent.", L"Motore assente.", L"Motor ausente.", L"재생 엔진 없음.", L"缺少播放引擎。", L"لا يوجد محرك.", L"Нет движка.", L"Keine Engine.", L"Sem motor.", L"Geen engine.", L"Brak silnika.", L"Motor yok.");
		return FALSE;
	}
	if (!pl || pl->playcnt <= 0 || !pl->pc) {
		if (errMsg) *errMsg = LL14(L"プレイリストが空です。", L"Playlist is empty.", L"Liste vide.", L"Playlist vuota.", L"Lista vacia.", L"재생목록이 비어 있습니다.", L"播放列表为空。", L"القائمة فارغة.", L"Плейлист пуст.", L"Playlist leer.", L"Lista vazia.", L"Afspeellijst leeg.", L"Pusta playlista.", L"Liste bos.");
		return FALSE;
	}

	int idx = -1;
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		idx = mp->GetSelectedPcIndex();
	if (idx < 0 || idx >= pl->playcnt) {
		if (pl->pnt >= 0 && pl->pnt < pl->playcnt) idx = pl->pnt;
		else if (plcnt >= 0 && plcnt < pl->playcnt) idx = plcnt;
		else idx = 0;
	}
	playlistdata0* pc = &pl->pc[idx];
	if (!pc || pc->fol[0] == 0) {
		if (errMsg) *errMsg = LL14(L"解析する曲が選べません。", L"No track selected for analysis.", L"Aucun titre.", L"Nessuna traccia.", L"Sin pista.", L"분석할 곡이 없습니다.", L"没有可分析的曲目。", L"لا يوجد مقطع.", L"Нет трека.", L"Kein Titel.", L"Sem faixa.", L"Geen track.", L"Brak utworu.", L"Parca yok.");
		return FALSE;
	}

	double expect = 0;
	if (pc->time > 0) expect = (double)pc->time;
	MpPromptAnalyzeSetExpectedDurationSec(expect > 0 ? expect : 180.0);

	MpPromptAnalyzeBegin();
	const BOOL ok = og->AnalyzeTrackForPrompt(pc);
	g_ana.active = FALSE;
	InterlockedExchange(&g_mpPromptAnalyzeOnly, 0);

	if (!ok || g_ana.nFrames < 8) {
		MpPromptAnalyzeAbort();
		if (errMsg) *errMsg = LL14(L"音声の読込/解析に失敗しました。", L"Failed to load/analyze audio.", L"Echec analyse audio.", L"Analisi audio fallita.", L"Error al analizar audio.", L"오디오 분석 실패.", L"音频分析失败。", L"فشل تحليل الصوت.", L"Ошибка анализа.", L"Analyse fehlgeschlagen.", L"Falha na analise.", L"Analyse mislukt.", L"Blad analizy.", L"Analiz basarisiz.");
		return FALSE;
	}

	ReportProgress(96, LL14(L"コマンド生成中…", L"Generating commands...", L"Generation...", L"Generazione...", L"Generando...", L"명령 생성…", L"生成命令…", L"Generating...", L"Генерация...", L"Erzeuge...", L"Gerando...", L"Genereren...", L"Generowanie...", L"Uretiliyor..."));
	outText = BuildFromPatterns(mode);
	AnaClear();
	g_anaExpectSec = 0;
	ReportProgress(100, LL14(L"完了", L"Done", L"Termine", L"Fatto", L"Hecho", L"완료", L"完成", L"Done", L"Готово", L"Fertig", L"Concluido", L"Klaar", L"Gotowe", L"Bitti"));
	if (outText.IsEmpty()) {
		if (errMsg) *errMsg = LL14(L"有効なパターンが見つかりませんでした。", L"No matching patterns found.", L"Aucun motif.", L"Nessun pattern.", L"Sin patrones.", L"패턴을 찾지 못했습니다.", L"未找到匹配模式。", L"لا توجد أنماط.", L"Нет шаблонов.", L"Keine Muster.", L"Sem padroes.", L"Geen patronen.", L"Brak wzorcow.", L"Desen yok.");
		return FALSE;
	}
	return TRUE;
}
