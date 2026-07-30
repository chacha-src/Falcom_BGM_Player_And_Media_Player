#include "stdafx.h"
#include "CPromptAnalyze.h"
#include "CPromptEngine.h"
#include "CMediaPlayerDlg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include <algorithm>
#include <cmath>
#include <vector>

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
const int    kMaxOutChars = 2000;

struct AnaState {
	std::vector<float> rms;
	std::vector<float> bass;   // 簡易: 差分包絡(低域寄り)
	std::vector<float> tre;    // 簡易: 高差分
	int rate = 44100;
	int ch = 2;
	int bits = 16;
	std::vector<uint8_t> pending;
	double pendingSamples = 0;
	BOOL active = FALSE;
};

AnaState g_ana;
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
	g_ana.rms.clear();
	g_ana.bass.clear();
	g_ana.tre.clear();
	g_ana.pending.clear();
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
	if ((int)g_ana.rms.size() >= kMaxFrames) return;
	g_ana.rms.push_back(rms);
	g_ana.bass.push_back(bass);
	g_ana.tre.push_back(tre);
	const double done = g_ana.rms.size() * kHopSec;
	int pct = 5;
	if (g_anaExpectSec > 0.5)
		pct = 5 + (int)((std::min)(1.0, done / g_anaExpectSec) * 90.0 + 0.5);
	else
		pct = 5 + (int)((std::min)(90.0, g_ana.rms.size() / 20.0) + 0.5);
	ReportProgress(pct, LL14(L"解析中…", L"Analyzing...", L"Analyse...", L"Analisi...", L"Analizando...", L"분석 중…", L"分析中…", L"Analyzing...", L"Анализ...", L"Analysiere...", L"Analisando...", L"Bezig met analyse...", L"Analiza...", L"Analiz..."));
}

static void AnaProcessBytes(const uint8_t* data, int nbytes)
{
	if (!data || nbytes <= 0) return;
	const int bps = BytesPerSample(g_ana.bits);
	const int frameBytes = bps * (std::max)(1, g_ana.ch);
	if (frameBytes <= 0) return;
	const int hopSamples = (std::max)(1, (int)(g_ana.rate * kHopSec + 0.5));

	g_ana.pending.insert(g_ana.pending.end(), data, data + nbytes);

	while ((int)g_ana.pending.size() >= hopSamples * frameBytes
		&& (int)g_ana.rms.size() < kMaxFrames) {
		double sumSq = 0;
		double sumDiff = 0;
		double sumHi = 0;
		float prev = 0;
		const uint8_t* base = g_ana.pending.data();
		for (int i = 0; i < hopSamples; ++i) {
			const uint8_t* fr = base + i * frameBytes;
			float mono = 0;
			for (int c = 0; c < g_ana.ch; ++c)
				mono += SampleAt(fr + c * bps, g_ana.bits);
			mono /= (float)g_ana.ch;
			sumSq += (double)mono * (double)mono;
			const float d = mono - prev;
			sumDiff += (double)d * (double)d;
			sumHi += (double)fabsf(d);
			prev = mono;
		}
		const float rms = (float)sqrt(sumSq / (double)hopSamples);
		const float bass = (float)sqrt(sumDiff / (double)hopSamples); // 動きの滑らかさ寄り
		const float tre = (float)(sumHi / (double)hopSamples);
		AnaPushFrame(rms, bass, tre);
		g_ana.pending.erase(g_ana.pending.begin(),
			g_ana.pending.begin() + hopSamples * frameBytes);
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

static CString CmdAt(LPCTSTR letters, double t0, double t1, int v0, int v1, BOOL hasVal)
{
	CString s;
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
	return CmdAt(L"d", t0, t1, ScaleDsRel(rel0), ScaleDsRel(rel1), TRUE);
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

static float MeanRange(const std::vector<float>& v, int a, int b)
{
	if (v.empty()) return 0;
	if (a < 0) a = 0;
	if (b >= (int)v.size()) b = (int)v.size() - 1;
	if (b < a) return 0;
	double s = 0;
	for (int i = a; i <= b; ++i) s += v[i];
	return (float)(s / (double)(b - a + 1));
}

static float Percentile(std::vector<float> v, float p)
{
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	const int i = (int)((v.size() - 1) * p + 0.5f);
	return v[(std::max)(0, (std::min)((int)v.size() - 1, i))];
}

// ---- パターン群 (順次追加前提) ----
// P01: イントロが弱い → DS音量フェードイン（値は現在DS=100相対 → CmdDsで絶対化）
static void PatSoftIntro(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 20) return;
	const int n = (int)a.rms.size();
	const int head = (std::min)(n / 8, (int)(4.0 / kHopSec)); // ~先頭4秒 or 1/8
	const float m = MeanRange(a.rms, 0, head);
	if (m < med * 0.55f && med > 0.01f) {
		const double t1 = head * kHopSec;
		AppendCmd(out, CmdDs(0, t1, 55, 100));
	}
}

// P02: 終盤が落ちる → フェードアウト気味
static void PatSoftOutro(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 30) return;
	const int n = (int)a.rms.size();
	const int tail = (std::min)(n / 6, (int)(8.0 / kHopSec));
	const float m = MeanRange(a.rms, n - tail, n - 1);
	if (m < med * 0.6f && med > 0.01f) {
		const double t0 = (n - tail) * kHopSec;
		const double t1 = (n - 1) * kHopSec;
		AppendCmd(out, CmdDs(t0, t1, 100, 45));
	}
}

// P03: 大きな盛り上がり → br 明るめ
static void PatBrightChorus(CString& out, const AnaState& a, float p90)
{
	if (a.rms.size() < 40) return;
	const int n = (int)a.rms.size();
	const int win = (int)(3.0 / kHopSec);
	int best = -1;
	float bestM = 0;
	for (int i = 0; i + win < n; i += win / 2) {
		const float m = MeanRange(a.rms, i, i + win);
		if (m > bestM) { bestM = m; best = i; }
	}
	if (best >= 0 && bestM > p90 * 0.95f && p90 > 0.02f) {
		AppendCmd(out, CmdAt(L"br", best * kHopSec, best * kHopSec, 0, 0, FALSE));
		AppendCmd(out, CmdAt(L"N", best * kHopSec, (best + win) * kHopSec, 100, 118, TRUE));
	}
}

// P04: 静かな中盤 → sb しょんぼり
static void PatQuietMelancholy(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 50) return;
	const int n = (int)a.rms.size();
	const int win = (int)(5.0 / kHopSec);
	int best = -1;
	float bestM = 1e9f;
	const int lo = n / 5;
	const int hi = n - n / 5;
	for (int i = lo; i + win < hi; i += win / 2) {
		const float m = MeanRange(a.rms, i, i + win);
		if (m < bestM) { bestM = m; best = i; }
	}
	if (best >= 0 && bestM < med * 0.7f && med > 0.01f) {
		AppendCmd(out, CmdAt(L"sb", best * kHopSec, best * kHopSec, 0, 0, FALSE));
		AppendCmd(out, CmdAt(L"r", best * kHopSec, (best + win) * kHopSec, 100, 125, TRUE));
	}
}

// P05: 急な立ち上がり(ビルド) → テンポ微増
static void PatBuildUp(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 40) return;
	const int n = (int)a.rms.size();
	const int win = (int)(4.0 / kHopSec);
	for (int i = 0; i + win < n; i += win) {
		const float a0 = MeanRange(a.rms, i, i + win / 3);
		const float a1 = MeanRange(a.rms, i + win * 2 / 3, i + win);
		if (a0 > 0.01f && a1 > a0 * 1.55f && a1 > med) {
			AppendCmd(out, CmdAt(L"t", i * kHopSec, (i + win) * kHopSec, 100, 110, TRUE));
			return;
		}
	}
}

// P06: 急落(ドロップ後) → ディレイ短フラッシュ
static void PatDropDelay(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 40) return;
	const int n = (int)a.rms.size();
	const int win = (int)(2.5 / kHopSec);
	for (int i = win; i + win < n; i += win / 2) {
		const float a0 = MeanRange(a.rms, i - win, i);
		const float a1 = MeanRange(a.rms, i, i + win);
		if (a0 > med * 1.2f && a1 < a0 * 0.55f) {
			AppendCmd(out, CmdAt(L"y", i * kHopSec, (i + win / 2) * kHopSec, 100, 130, TRUE));
			AppendCmd(out, CmdAt(L"y", (i + win / 2) * kHopSec, (i + win) * kHopSec, 130, 100, TRUE));
			return;
		}
	}
}

// P07: 低域寄り区間 → a/b 帯ブースト
static void PatBassBoost(CString& out, const AnaState& a, float medBass)
{
	if (a.bass.size() < 40) return;
	const int n = (int)a.bass.size();
	const int win = (int)(4.0 / kHopSec);
	int best = -1;
	float bestM = 0;
	for (int i = 0; i + win < n; i += win) {
		const float m = MeanRange(a.bass, i, i + win);
		const float e = MeanRange(a.rms, i, i + win);
		if (e > 0.02f && m > bestM) { bestM = m; best = i; }
	}
	if (best >= 0 && bestM > medBass * 1.25f) {
		AppendCmd(out, CmdAt(L"a", best * kHopSec, (best + win) * kHopSec, 100, 112, TRUE));
		AppendCmd(out, CmdAt(L"b", best * kHopSec, (best + win) * kHopSec, 100, 110, TRUE));
	}
}

// P08: 高域寄り区間 → 高域EQ + 鮮明
static void PatTrebleLift(CString& out, const AnaState& a, float medTre)
{
	if (a.tre.size() < 40) return;
	const int n = (int)a.tre.size();
	const int win = (int)(4.0 / kHopSec);
	int best = -1;
	float bestM = 0;
	for (int i = 0; i + win < n; i += win) {
		const float m = MeanRange(a.tre, i, i + win);
		if (m > bestM) { bestM = m; best = i; }
	}
	if (best >= 0 && bestM > medTre * 1.3f) {
		AppendCmd(out, CmdAt(L"n", best * kHopSec, (best + win) * kHopSec, 100, 114, TRUE));
		AppendCmd(out, CmdAt(L"o", best * kHopSec, (best + win) * kHopSec, 100, 112, TRUE));
		AppendCmd(out, CmdAt(L"N", best * kHopSec, (best + win) * kHopSec, 100, 115, TRUE));
	}
}

// P09: 長尺で平均的に激しい → fa ファストを中盤に
static void PatOverallFast(CString& out, const AnaState& a, float med, float p75)
{
	if (a.rms.size() < 80) return;
	if (med > 0.04f && p75 > med * 1.15f) {
		const double t = (a.rms.size() / 2) * kHopSec;
		AppendCmd(out, CmdAt(L"fa", t, t, 0, 0, FALSE));
	}
}

// P10: 全体が静かめ → sl スローを序盤に
static void PatOverallSlow(CString& out, const AnaState& a, float med, float p90)
{
	if (a.rms.size() < 60) return;
	if (med > 0.005f && p90 < med * 1.35f && med < 0.06f) {
		const double t = (a.rms.size() / 5) * kHopSec;
		AppendCmd(out, CmdAt(L"sl", t, t, 0, 0, FALSE));
	}
}

// P11: 2番目の山 → サブコーラスを明るく
static void PatSecondChorus(CString& out, const AnaState& a, float p90)
{
	if (a.rms.size() < 80) return;
	const int n = (int)a.rms.size();
	const int win = (std::max)(8, n / 20);
	int best = -1, second = -1;
	float bestV = -1.f, secondV = -1.f;
	for (int i = 0; i + win < n; i += win / 2) {
		float s = 0;
		for (int j = 0; j < win; ++j) s += a.rms[i + j];
		s /= win;
		if (s > bestV) { secondV = bestV; second = best; bestV = s; best = i; }
		else if (s > secondV) { secondV = s; second = i; }
	}
	if (second < 0 || secondV < p90 * 0.85f) return;
	if (best >= 0 && (second > best ? second - best : best - second) < win) return;
	AppendCmd(out, CmdAt(L"br", second * kHopSec, second * kHopSec, 0, 0, FALSE));
	AppendCmd(out, CmdAt(L"N", second * kHopSec, (second + win) * kHopSec, 100, 112, TRUE));
}

// P12: 長い静かな谷のあと → リバーブで復帰
static void PatSilenceGapReverb(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 100) return;
	const int n = (int)a.rms.size();
	const float thr = med * 0.45f;
	int bestLen = 0, bestEnd = -1, run = 0;
	for (int i = 0; i < n; ++i) {
		if (a.rms[i] < thr) { ++run; if (run > bestLen) { bestLen = run; bestEnd = i; } }
		else run = 0;
	}
	if (bestLen < 20 || bestEnd < 0) return;
	const double t0 = (bestEnd - bestLen / 2) * kHopSec;
	const double t1 = (bestEnd + 1) * kHopSec;
	AppendCmd(out, CmdAt(L"r", t0, t1, 100, 145, TRUE));
	AppendCmd(out, CmdDs(t1, t1 + 2.0, 70, 100));
}

// P13: 中盤が窪む → ピッチ/音量で持ち上げ
static void PatMidrangeScoop(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 60) return;
	const int n = (int)a.rms.size();
	const int q1 = n / 4, q3 = n * 3 / 4;
	float mid = 0, sides = 0;
	int mc = 0, sc = 0;
	for (int i = 0; i < n; ++i) {
		if (i >= q1 && i < q3) { mid += a.rms[i]; ++mc; }
		else { sides += a.rms[i]; ++sc; }
	}
	if (mc < 8 || sc < 8) return;
	mid /= mc; sides /= sc;
	if (mid >= sides * 0.92f || mid >= med) return;
	AppendCmd(out, CmdAt(L"p", q1 * kHopSec, q3 * kHopSec, 100, 106, TRUE));
	AppendCmd(out, CmdDs(q1 * kHopSec, q3 * kHopSec, 100, 108));
}

// P14: 高域エネルギー帯 → ステレオ感
static void PatStereoWiden(CString& out, const AnaState& a, float medTre)
{
	if (a.tre.size() < 40 || a.rms.size() < 40) return;
	const int n = (int)a.rms.size();
	const int win = (std::max)(6, n / 16);
	int best = -1;
	float bestV = -1.f;
	for (int i = 0; i + win < n; ++i) {
		float s = 0;
		for (int j = 0; j < win; ++j) s += a.tre[i + j];
		s /= win;
		if (s > bestV) { bestV = s; best = i; }
	}
	if (best < 0 || bestV < medTre * 1.2f) return;
	AppendCmd(out, CmdAt(L"S", best * kHopSec, (best + win) * kHopSec, 100, 125, TRUE));
}

// P15: 終盤テンポを少し落とす
static void PatLateTempoDip(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 80) return;
	const int n = (int)a.rms.size();
	const int t0 = n * 3 / 4;
	float late = 0;
	for (int i = t0; i < n; ++i) late += a.rms[i];
	late /= (std::max)(1, n - t0);
	if (late > med * 1.05f) return;
	AppendCmd(out, CmdAt(L"t", t0 * kHopSec, n * kHopSec, 100, 94, TRUE));
	AppendCmd(out, CmdAt(L"sl", t0 * kHopSec, t0 * kHopSec, 0, 0, FALSE));
}

// P16: 序盤の抜け感 → 鮮明
static void PatEarlyClarity(CString& out, const AnaState& a, float medTre)
{
	if (a.tre.size() < 40) return;
	const int n = (int)a.tre.size();
	const int end = n / 4;
	float early = 0;
	for (int i = 0; i < end; ++i) early += a.tre[i];
	early /= (std::max)(1, end);
	if (early < medTre * 1.1f) return;
	AppendCmd(out, CmdAt(L"N", 0, end * kHopSec, 100, 118, TRUE));
	AppendCmd(out, CmdAt(L"o", 0, end * kHopSec, 100, 110, TRUE));
}

// P17: 長い静かな持続 → 環境音パッド
static void PatEnvPad(CString& out, const AnaState& a, float med)
{
	if (a.rms.size() < 100) return;
	const int n = (int)a.rms.size();
	int quiet = 0;
	for (int i = 0; i < n; ++i)
		if (a.rms[i] < med * 0.7f) ++quiet;
	if (quiet < n / 3) return;
	const double mid = (n / 2) * kHopSec;
	AppendCmd(out, CmdAt(L"E", mid, mid, 16, 16, TRUE));
	AppendCmd(out, CmdAt(L"F", mid, n * kHopSec, 0, 55, TRUE));
}

// P18: トランジェント強め → ディレイパンチ
static void PatPunchyTransients(CString& out, const AnaState& a, float med, float p90)
{
	if (a.rms.size() < 50) return;
	if (med < 0.01f || p90 < med * 1.8f) return;
	const int n = (int)a.rms.size();
	const float thr = med + (p90 - med) * 0.7f;
	int hits = 0;
	for (int i = 2; i + 2 < n && hits < 3; ++i) {
		if (a.rms[i] > thr && a.rms[i] > a.rms[i - 1] * 1.25f) {
			AppendCmd(out, CmdAt(L"y", i * kHopSec, (i + 4) * kHopSec, 100, 135, TRUE));
			++hits;
			i += 8;
		}
	}
}

// P19: 終盤の低域寄り → ベース温め
static void PatWarmBassEnd(CString& out, const AnaState& a, float medBass)
{
	if (a.bass.size() < 60) return;
	const int n = (int)a.bass.size();
	const int t0 = n * 2 / 3;
	float late = 0, early = 0;
	for (int i = 0; i < n / 3; ++i) early += a.bass[i];
	for (int i = t0; i < n; ++i) late += a.bass[i];
	early /= (std::max)(1, n / 3);
	late /= (std::max)(1, n - t0);
	if (late < early * 1.08f || late < medBass) return;
	AppendCmd(out, CmdAt(L"a", t0 * kHopSec, n * kHopSec, 100, 114, TRUE));
	AppendCmd(out, CmdAt(L"b", t0 * kHopSec, n * kHopSec, 100, 108, TRUE));
}

// P20: ダイナミクス広い → 空間と圧縮バランス
static void PatBreathRoom(CString& out, const AnaState& a, float med, float p90)
{
	if (a.rms.size() < 60) return;
	if (med < 0.008f || p90 < med * 2.2f) return;
	const int n = (int)a.rms.size();
	AppendCmd(out, CmdAt(L"r", 0, n * kHopSec, 100, 118, TRUE));
	AppendCmd(out, CmdAt(L"c", (n / 4) * kHopSec, (n * 3 / 4) * kHopSec, 100, 112, TRUE));
}

static void PatModeFlavor(CString& out, const AnaState& a, float med, float p90, int mode)
{
	if (a.rms.size() < 20) return;
	const int n = (int)a.rms.size();
	const double mid = (n / 2) * kHopSec;
	const double q1 = (n / 4) * kHopSec;
	const double q3 = (n * 3 / 4) * kHopSec;
	switch (mode) {
	case MP_ANA_COMEDY:
		AppendCmd(out, CmdAt(L"br", q1, q1, 0, 0, FALSE));
		AppendCmd(out, CmdAt(L"p", mid, q3, 100, 108, TRUE));
		AppendCmd(out, CmdAt(L"y", mid, mid + 2.0, 100, 140, TRUE));
		AppendCmd(out, CmdAt(L"N", q1, mid, 100, 110, TRUE));
		AppendCmd(out, CmdAt(L"t", mid, q3, 100, 106, TRUE));
		break;
	case MP_ANA_SERIOUS:
		AppendCmd(out, CmdAt(L"sb", q1, q1, 0, 0, FALSE));
		AppendCmd(out, CmdAt(L"r", 0, mid, 100, 135, TRUE));
		AppendCmd(out, CmdAt(L"t", 0, q1, 100, 92, TRUE));
		AppendCmd(out, CmdAt(L"c", mid, q3, 100, 118, TRUE));
		AppendCmd(out, CmdDs(q3, n * kHopSec, 100, 70));
		break;
	case MP_ANA_ROMANTIC:
		AppendCmd(out, CmdAt(L"c", q1, q3, 100, 125, TRUE));
		AppendCmd(out, CmdAt(L"p", mid, q3, 100, 104, TRUE));
		AppendCmd(out, CmdAt(L"S", mid, q3, 100, 115, TRUE));
		AppendCmd(out, CmdAt(L"r", 0, n * kHopSec, 100, 128, TRUE));
		AppendCmd(out, CmdAt(L"E", mid, mid, 14, 14, TRUE));
		break;
	case MP_ANA_INTENSE:
		AppendCmd(out, CmdAt(L"fa", mid, mid, 0, 0, FALSE));
		// 相対112 ≈ 基準の+12%（基準40%なら約45%）
		AppendCmd(out, CmdDs(q1, mid, 100, 112));
		AppendCmd(out, CmdAt(L"N", mid, q3, 100, 120, TRUE));
		AppendCmd(out, CmdAt(L"y", mid, mid + 1.2, 100, 150, TRUE));
		AppendCmd(out, CmdAt(L"a", mid, q3, 100, 112, TRUE));
		break;
	case MP_ANA_CHILL:
		AppendCmd(out, CmdAt(L"sl", q1, q1, 0, 0, FALSE));
		AppendCmd(out, CmdAt(L"r", 0, n * kHopSec, 100, 120, TRUE));
		AppendCmd(out, CmdDs(0, q1, 70, 100));
		AppendCmd(out, CmdAt(L"F", mid, q3, 0, 40, TRUE));
		AppendCmd(out, CmdAt(L"t", 0, mid, 100, 96, TRUE));
		break;
	case MP_ANA_ELECTRO:
		AppendCmd(out, CmdAt(L"y", mid, mid + 1.5, 100, 145, TRUE));
		AppendCmd(out, CmdAt(L"t", q1, mid, 100, 112, TRUE));
		AppendCmd(out, CmdAt(L"o", mid, q3, 100, 118, TRUE));
		AppendCmd(out, CmdAt(L"fa", q1, q1, 0, 0, FALSE));
		AppendCmd(out, CmdAt(L"S", mid, q3, 100, 122, TRUE));
		break;
	case MP_ANA_ORCHESTRAL:
		AppendCmd(out, CmdAt(L"r", 0, n * kHopSec, 100, 140, TRUE));
		AppendCmd(out, CmdAt(L"I", mid, q3, 100, 118, TRUE));
		AppendCmd(out, CmdAt(L"S", mid, q3, 100, 120, TRUE));
		AppendCmd(out, CmdAt(L"c", q1, q3, 100, 115, TRUE));
		AppendCmd(out, CmdDs(0, q1, 80, 100));
		break;
	case MP_ANA_RETRO:
		AppendCmd(out, CmdAt(L"c", q1, q3, 100, 130, TRUE));
		AppendCmd(out, CmdAt(L"p", mid, mid + 3.0, 100, 96, TRUE));
		AppendCmd(out, CmdAt(L"a", mid, q3, 100, 115, TRUE));
		AppendCmd(out, CmdAt(L"n", q1, mid, 100, 108, TRUE));
		AppendCmd(out, CmdAt(L"y", mid, mid + 2.0, 100, 125, TRUE));
		break;
	case MP_ANA_CINEMATIC:
		AppendCmd(out, CmdAt(L"t", 0, mid, 95, 108, TRUE));
		AppendCmd(out, CmdAt(L"E", mid, mid, 18, 18, TRUE));
		AppendCmd(out, CmdAt(L"F", mid, q3, 0, 70, TRUE));
		AppendCmd(out, CmdDs(q3, n * kHopSec, 100, 55));
		AppendCmd(out, CmdAt(L"r", q1, q3, 100, 135, TRUE));
		AppendCmd(out, CmdAt(L"I", mid, q3, 100, 112, TRUE));
		break;
	default:
		AppendCmd(out, CmdAt(L"N", mid, q3, 100, 108, TRUE));
		AppendCmd(out, CmdAt(L"r", q1, q3, 100, 110, TRUE));
		break;
	}
	(void)med; (void)p90;
}

static CString BuildFromPatterns(int mode)
{
	CString out;
	if (g_ana.rms.size() < 10) return out;

	const float med = Percentile(g_ana.rms, 0.5f);
	const float p75 = Percentile(g_ana.rms, 0.75f);
	const float p90 = Percentile(g_ana.rms, 0.90f);
	const float medBass = Percentile(g_ana.bass, 0.5f);
	const float medTre = Percentile(g_ana.tre, 0.5f);

	g_dsBasePct = CurrentDsPercent();

	// コメント行はパーサが無視するので先頭に解析メモ
	{
		CString note;
		note.Format(L"# auto-analyze mode=%d %.1fs frames=%d dsBase=%d%%\r\n",
			mode, g_ana.rms.size() * kHopSec, (int)g_ana.rms.size(), g_dsBasePct);
		if (note.GetLength() < kMaxOutChars)
			out = note;
	}

	PatSoftIntro(out, g_ana, med);
	PatSoftOutro(out, g_ana, med);
	PatBrightChorus(out, g_ana, p90);
	PatQuietMelancholy(out, g_ana, med);
	PatBuildUp(out, g_ana, med);
	PatDropDelay(out, g_ana, med);
	PatBassBoost(out, g_ana, medBass);
	PatTrebleLift(out, g_ana, medTre);
	PatOverallFast(out, g_ana, med, p75);
	PatOverallSlow(out, g_ana, med, p90);
	PatSecondChorus(out, g_ana, p90);
	PatSilenceGapReverb(out, g_ana, med);
	PatMidrangeScoop(out, g_ana, med);
	PatStereoWiden(out, g_ana, medTre);
	PatLateTempoDip(out, g_ana, med);
	PatEarlyClarity(out, g_ana, medTre);
	PatEnvPad(out, g_ana, med);
	PatPunchyTransients(out, g_ana, med, p90);
	PatWarmBassEnd(out, g_ana, medBass);
	PatBreathRoom(out, g_ana, med, p90);
	PatModeFlavor(out, g_ana, med, p90, mode);

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

	if (!ok || g_ana.rms.size() < 8) {
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
