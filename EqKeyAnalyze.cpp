#include "EqKeyAnalyze.h"

#include <math.h>
#include <string.h>
#include <wchar.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float Clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static int IsHex(wchar_t c)
{
	return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

const wchar_t* EqKeySkipMarkup(const wchar_t* p)
{
	if (!p) return p;
	while (p[0] == L'!' && p[1] == L'@' && p[2]) {
		const wchar_t cmd = p[2];
		if (cmd == L'B' || cmd == L'I') {
			p += 3;
			continue;
		}
		if (cmd == L'C') {
			int hex = 1;
			for (int k = 0; k < 6; ++k) if (!IsHex(p[3 + k])) hex = 0;
			if (!hex) break;
			p += 9;
			continue;
		}
		if (cmd == L'F' && (p[3] == L'+' || p[3] == L'-')
			&& p[4] >= L'0' && p[4] <= L'9' && p[5] >= L'0' && p[5] <= L'9') {
			p += 6;
			continue;
		}
		break;
	}
	return p;
}

void EqKeyStripMarkup(const wchar_t* src, wchar_t* dst, int dstCap)
{
	if (!dst || dstCap <= 0) return;
	dst[0] = 0;
	if (!src) return;
	int w = 0;
	const wchar_t* p = src;
	while (*p && w + 1 < dstCap) {
		if (p[0] == L'!' && p[1] == L'@') {
			const wchar_t* n = EqKeySkipMarkup(p);
			if (n != p) { p = n; continue; }
		}
		dst[w++] = *p++;
	}
	dst[w] = 0;
}

static int RootIndexFromToken(wchar_t a, wchar_t b)
{
	static const wchar_t* names[12] = {
		L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
	};
	static const wchar_t* flats[12] = {
		L"C", L"Db", L"D", L"Eb", L"E", L"F", L"Gb", L"G", L"Ab", L"A", L"Bb", L"B"
	};
	wchar_t tok[3] = { a, 0, 0 };
	if (b == L'#' || b == L'b') tok[1] = b;
	for (int i = 0; i < 12; ++i) {
		if (wcscmp(tok, names[i]) == 0 || wcscmp(tok, flats[i]) == 0) return i;
	}
	return -1;
}

int EqKeyPrimaryIsMinor(const wchar_t* markupOrPlain)
{
	wchar_t vis[160];
	EqKeyStripMarkup(markupOrPlain, vis, 160);
	const wchar_t* p = vis;
	while (*p == L' ' || *p == L',') ++p;
	if (!*p) return 0;
	const wchar_t* comma = p;
	while (*comma && *comma != L',') ++comma;
	wchar_t tok[48];
	int n = (int)(comma - p);
	if (n > 47) n = 47;
	for (int i = 0; i < n; ++i) tok[i] = p[i];
	tok[n] = 0;
	for (int i = 0; tok[i]; ++i) {
		if (tok[i] == L'D' || tok[i] == L'd') {
			if ((tok[i + 1] == L'I' || tok[i + 1] == L'i') && (tok[i + 2] == L'M' || tok[i + 2] == L'm'))
				return 1;
		}
	}
	if (p[0] < L'A' || p[0] > L'G') return 0;
	++p;
	if (*p == L'#' || *p == L'b') ++p;
	// 小文字 m はマイナー。大文字 M は M7（メジャー）。
	if (*p == L'm') return 1;
	return 0;
}

// ---------------------------------------------------------------------------
// コード。Fujishima 型のテンプレート照合。ルートは最大ビンに固定しない。
// 低域クロマはルートの事前確率（Mauch のベース音）。
// ---------------------------------------------------------------------------

static const int EQKEY_NAME_MAX = 160;

struct EqPat { const wchar_t* name; int w[12]; float bonus; };

static const EqPat kPats[] = {
	{ L"",                                    { 3,0,0,0,2,0,0,1,0,0,0,0 }, 0.5f },
	{ L"!@C0066bbm!@C000000",                 { 3,0,0,2,0,0,0,1,0,0,0,0 }, 0.5f },
	{ L"!@Cff55005!@C000000",                 { 3,0,0,0,0,0,0,2,0,0,0,0 }, 0.4f },
	{ L"!@C8844ccsus!@Cff55004!@C000000",     { 3,0,0,0,0,3,0,1,0,0,0,0 }, 0.4f },
	{ L"!@C8844ccsus!@Cff55002!@C000000",     { 3,0,3,0,0,0,0,1,0,0,0,0 }, 0.4f },
	{ L"!@Caa7744dim!@C000000",               { 3,0,0,2,0,0,2,0,0,0,0,0 }, 0.3f },
	{ L"!@Ccc4400aug!@C000000",               { 3,0,0,0,2,0,0,0,2,0,0,0 }, 0.3f },
	{ L"!@Cff55007!@C000000",                 { 3,0,0,0,2,0,0,1,0,0,2,0 }, 0.3f },
	{ L"!@C00aa77M!@Cff55007!@C000000",       { 3,0,0,0,2,0,0,1,0,0,0,2 }, 0.3f },
	{ L"!@C0066bbm!@Cff55007!@C000000",       { 3,0,0,2,0,0,0,1,0,0,2,0 }, 0.3f },
	{ L"!@Cff55006!@C000000",                 { 3,0,0,0,2,0,0,1,0,2,0,0 }, 0.2f },
	{ L"!@C0066bbm!@Cff55006!@C000000",       { 3,0,0,2,0,0,0,1,0,2,0,0 }, 0.2f },
	{ L"!@Cbb7733add!@Cff55009!@C000000",     { 3,0,2,0,2,0,0,1,0,0,0,0 }, 0.2f },
	{ L"!@Cff55007!@C8844ccsus!@Cff55004!@C000000", { 3,0,0,0,0,2,0,1,0,0,2,0 }, 0.2f },
	{ L"!@C0066bbm!@Cff55007!@Cdd2222b!@Cff55005!@C000000", { 3,0,0,2,0,0,2,0,0,0,2,0 }, 0.2f },
	{ L"!@Caa7744dim!@Cff55007!@C000000",     { 3,0,0,2,0,0,2,0,0,2,0,0 }, 0.2f },
	{ L"!@Cff55009!@C000000",                 { 3,0,2,0,2,0,0,1,0,0,2,0 }, -0.5f },
	{ L"!@C00aa77M!@Cff55009!@C000000",       { 3,0,2,0,2,0,0,1,0,0,0,2 }, -0.5f },
	{ L"!@C0066bbm!@Cff55009!@C000000",       { 3,0,2,2,0,0,0,1,0,0,2,0 }, -0.5f },
};

static const wchar_t* kNote[12] = {
	L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
};

static void WCopy(wchar_t* d, int cap, const wchar_t* s)
{
	if (!d || cap <= 0) return;
	if (!s) { d[0] = 0; return; }
	int i = 0;
	for (; i < cap - 1 && s[i]; ++i) d[i] = s[i];
	d[i] = 0;
}

static void WCat(wchar_t* d, int cap, const wchar_t* s)
{
	if (!d || cap <= 0 || !s) return;
	int n = 0;
	while (n < cap && d[n]) ++n;
	int i = 0;
	for (; n + i < cap - 1 && s[i]; ++i) d[n + i] = s[i];
	d[n + i] = 0;
}

static int NameInList(const wchar_t* chord, const wchar_t* list)
{
	if (!list || !list[0] || !chord || !chord[0]) return 0;
	const wchar_t* p = list;
	while (*p) {
		const wchar_t* comma = wcsstr(p, L", ");
		size_t len = comma ? (size_t)(comma - p) : wcslen(p);
		if (wcsncmp(p, chord, len) == 0 && chord[len] == 0) return 1;
		if (!comma) break;
		p = comma + 2;
	}
	return 0;
}

static int PrevRoot(const wchar_t* prev)
{
	if (!prev) return -1;
	const wchar_t* p = EqKeySkipMarkup(prev);
	while (*p && !(*p >= L'A' && *p <= L'G')) ++p;
	if (!*p) return -1;
	return RootIndexFromToken(p[0], p[1]);
}

struct EqCand { wchar_t name[EQKEY_NAME_MAX]; float score; int complexity; int root; };

void EqKeyNotesToChroma(const float* strength108, float* bass12, float* mid12, float* high12, float* all12)
{
	// 次数 1..6 の半音位置。Klapuri 系の倍音加算で、基音に倍音エネルギーを戻す。
	static const int kOff[6] = { 0, 12, 19, 24, 28, 31 };
	static const float kW[6] = { 1.f, 0.50f, 0.33f, 0.25f, 0.20f, 0.16f };
	float sal[108];
	for (int k = 0; k < 108; ++k) {
		float s = 0.f;
		for (int h = 0; h < 6; ++h) {
			const int j = k + kOff[h];
			if (j < 108 && strength108) s += strength108[j] * kW[h];
		}
		sal[k] = s;
	}
	for (int i = 0; i < 12; ++i) {
		if (bass12) bass12[i] = 0.f;
		if (mid12) mid12[i] = 0.f;
		if (high12) high12[i] = 0.f;
		if (all12) all12[i] = 0.f;
	}
	if (!strength108) return;
	for (int note = 0; note < 108; ++note) {
		const int pc = note % 12;
		const float s = sal[note];
		if (note < 36) { if (bass12) bass12[pc] += s; }
		else if (note < 60) { if (mid12) mid12[pc] += s; }
		else { if (high12) high12[pc] += s; }
		if (all12) all12[pc] += s;
	}
}

void EqKeyEstimateChord(const float* noteClass12, float threshold, const wchar_t* prev, const float* bassPrior12, wchar_t* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!noteClass12) return;
	float maxVal = 0.f;
	for (int i = 0; i < 12; ++i) if (noteClass12[i] > maxVal) maxVal = noteClass12[i];
	if (maxVal < 0.001f) return;

	float n[12];
	int active = 0;
	int dom = 0;
	for (int i = 0; i < 12; ++i) {
		n[i] = noteClass12[i] / maxVal;
		if (n[i] < 0.08f) n[i] = 0.f;
		if (n[i] > 0.08f) ++active;
		if (n[i] > n[dom]) dom = i;
	}
	if (active <= 1) {
		if (n[dom] < threshold) return;
		WCopy(out, outCap, kNote[dom]);
		return;
	}

	float bp[12];
	float bpMax = 0.f;
	for (int i = 0; i < 12; ++i) {
		bp[i] = bassPrior12 ? bassPrior12[i] : 0.f;
		if (bp[i] > bpMax) bpMax = bp[i];
	}
	if (bpMax > 0.001f) {
		for (int i = 0; i < 12; ++i) {
			bp[i] /= bpMax;
			if (bp[i] < 0.12f) bp[i] = 0.f;
		}
	} else {
		for (int i = 0; i < 12; ++i) bp[i] = 0.f;
	}

	const int prevR = PrevRoot(prev);
	const int np = (int)(sizeof(kPats) / sizeof(kPats[0]));
	EqCand cands[48];
	int candCount = 0;

	for (int root = 0; root < 12; ++root) {
		if (n[root] < threshold && bp[root] < 0.45f) continue;
		for (int c = 0; c < np; ++c) {
			float sc = 0.f;
			int matched = 0, req = 0;
			for (int x = 0; x < 12; ++x) if (kPats[c].w[x] > 0) ++req;
			const int is9 = (req >= 5);
			for (int nn = 0; nn < 12; ++nn) {
				const int note = (root + nn) % 12;
				const int w = kPats[c].w[nn];
				if (w > 0) {
					sc += n[note] * (float)w * 2.f;
					if (n[note] > 0.12f) ++matched;
				} else if (n[note] > 0.25f) {
					sc -= n[note] * 1.5f;
				}
			}
			if (is9) {
				const float mr = (req > 0) ? (float)matched / (float)req : 0.f;
				if (mr < 0.8f) sc -= 10.f;
				if (n[(root + 2) % 12] < 0.2f) sc -= 5.f;
			} else {
				if (req > 0 && (float)matched / (float)req < 0.4f) sc -= 3.f;
				if (req == 4) {
					int tension = -1;
					for (int x = 11; x >= 1; --x) {
						if (x != 3 && x != 4 && x != 7 && x != 5 && kPats[c].w[x] > 0) {
							tension = x;
							break;
						}
					}
					if (tension >= 0) {
						const float tVal = n[(root + tension) % 12];
						if (tVal < 0.15f) sc -= 4.f;
						else if (tVal < 0.22f) sc -= 1.5f;
					}
				}
			}
			sc -= (float)(active - matched) * 1.0f;
			sc += kPats[c].bonus;
			if (req == 3) sc += 1.2f;
			if (req == 4) sc += 0.5f;
			if (req >= 5) sc -= 1.0f;
			if (bp[root] > 0.f) sc += 2.0f * bp[root];
			if (prevR == root) sc += 0.6f;

			wchar_t cur[EQKEY_NAME_MAX];
			WCopy(cur, EQKEY_NAME_MAX, kNote[root]);
			WCat(cur, EQKEY_NAME_MAX, kPats[c].name);
			if (prev && NameInList(cur, prev)) sc += 1.5f;

			const float gate = is9 ? 3.5f : 0.8f;
			if (sc > gate && candCount < 48) {
				WCopy(cands[candCount].name, EQKEY_NAME_MAX, cur);
				cands[candCount].score = sc;
				cands[candCount].complexity = req;
				cands[candCount].root = root;
				++candCount;
			}
		}
	}
	if (candCount <= 0) {
		if (n[dom] >= threshold) WCopy(out, outCap, kNote[dom]);
		return;
	}
	for (int i = 0; i < candCount; ++i) {
		int best = i;
		for (int j = i + 1; j < candCount; ++j) {
			const int jBetter = (fabsf(cands[j].score - cands[best].score) < 0.35f)
				? (cands[j].complexity < cands[best].complexity)
				: (cands[j].score > cands[best].score);
			if (jBetter) best = j;
		}
		if (best != i) {
			EqCand tmp = cands[i];
			cands[i] = cands[best];
			cands[best] = tmp;
		}
	}

	const int winRoot = cands[0].root;
	const float third = (n[(winRoot + 3) % 12] > n[(winRoot + 4) % 12]) ? n[(winRoot + 3) % 12] : n[(winRoot + 4) % 12];
	const float fifth = n[(winRoot + 7) % 12];
	if (cands[0].complexity <= 3 && fifth > 0.35f && third < 0.18f && n[winRoot] > 0.35f) {
		WCopy(out, outCap, kNote[winRoot]);
		WCat(out, outCap, L"!@B!@I[Power]!@B!@I");
		return;
	}

	WCopy(out, outCap, cands[0].name);
	int count = 1;
	for (int i = 1; i < candCount && count < 2; ++i) {
		if (cands[i].root != winRoot) continue;
		if (cands[0].score - cands[i].score > 2.0f) break;
		if (cands[i].score < cands[0].score * 0.62f) continue;
		if (wcscmp(cands[i].name, out) == 0) continue;
		if (wcsstr(cands[i].name, L"9") && cands[0].score - cands[i].score > 1.2f) continue;
		WCat(out, outCap, L", ");
		WCat(out, outCap, cands[i].name);
		++count;
	}
}

// ---------------------------------------------------------------------------
// 母音。中央抽出 → YIN → LPC フォルマント → 声道長を変えた五母音照合。
// 楽器は母音空間に入らない、または周期が歌唱帯域のブザーにならない。
// ---------------------------------------------------------------------------

static const int kVocSr = 16000;
static const int kVocN = 1024;

static float s_mid[2048];
static float s_side[2048];
static float s_re[1024];
static float s_im[1024];
static float s_sre[1024];
static float s_sim[1024];
static float s_time[1024];
static float s_lpcIn[1024];
static float s_comb[1024];

static wchar_t s_vowelShow = 0;
static wchar_t s_vowelPend = 0;
static int s_vowelStreak = 0;
static int s_vowelMiss = 0;
static int s_chorusLatch = 0;
static float s_f0Hist[5];
static int s_f0HistN = 0;
static float s_lockF0 = 0.f;
static float s_lastCentRatio = 0.7f;
static int s_lastOnsetRow = 0;
static int s_onsetHold = 0;
static float s_prevF1 = 0.f;
static float s_prevF2 = 0.f;
static int s_wasVoiced = 0;
static wchar_t s_shownKana = 0;

void EqKeyVocalReset()
{
	s_vowelShow = 0;
	s_vowelPend = 0;
	s_vowelStreak = 0;
	s_vowelMiss = 0;
	s_chorusLatch = 0;
	s_f0HistN = 0;
	s_lockF0 = 0.f;
	s_lastCentRatio = 0.7f;
	s_lastOnsetRow = 0;
	s_onsetHold = 0;
	s_prevF1 = 0.f;
	s_prevF2 = 0.f;
	s_wasVoiced = 0;
	s_shownKana = 0;
}

static void FftRadix2(float* re, float* im, int n, int sign)
{
	for (int i = 1, j = 0; i < n; ++i) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) {
			float tr = re[i]; re[i] = re[j]; re[j] = tr;
			tr = im[i]; im[i] = im[j]; im[j] = tr;
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		const float ang = (float)sign * 2.f * (float)M_PI / (float)len;
		const float wlenRe = cosf(ang);
		const float wlenIm = sinf(ang);
		for (int i = 0; i < n; i += len) {
			float wr = 1.f, wi = 0.f;
			const int half = len >> 1;
			for (int j = 0; j < half; ++j) {
				const int u = i + j;
				const int v = u + half;
				const float tr = wr * re[v] - wi * im[v];
				const float ti = wr * im[v] + wi * re[v];
				re[v] = re[u] - tr;
				im[v] = im[u] - ti;
				re[u] += tr;
				im[u] += ti;
				const float nwr = wr * wlenRe - wi * wlenIm;
				wi = wr * wlenIm + wi * wlenRe;
				wr = nwr;
			}
		}
	}
}

static int DownTo16k(const double* L, const double* R, int n, int sr, float* mid, float* side, int cap)
{
	if (!L || n < 64 || cap < 64) return 0;
	if (sr < 8000) sr = 44100;
	if (n > 4096) n = 4096;
	int outN = (int)((double)n * (double)kVocSr / (double)sr + 0.5);
	if (outN > cap) outN = cap;
	if (outN < 64) return 0;
	const double step = (double)sr / (double)kVocSr;
	double start = (double)n - (double)outN * step;
	if (start < 0) start = 0;
	for (int i = 0; i < outN; ++i) {
		const double center = start + ((double)i + 0.5) * step;
		int a = (int)floor(center - step * 0.5);
		int b = (int)floor(center + step * 0.5);
		if (a < 0) a = 0;
		if (b > n) b = n;
		if (b <= a) b = a + 1;
		if (b > n) b = n;
		if (b <= a) { mid[i] = 0.f; side[i] = 0.f; continue; }
		double sL = 0, sR = 0;
		for (int k = a; k < b; ++k) {
			sL += L[k];
			sR += R ? R[k] : L[k];
		}
		const double inv = 1.0 / (double)(b - a);
		mid[i] = (float)(0.5 * (sL + sR) * inv);
		side[i] = (float)(0.5 * (sL - sR) * inv);
	}
	return outN;
}

static float GoertzelAt(const float* x, int n, float freq)
{
	if (freq < 40.f || freq > 7500.f || n < 16) return 0.f;
	const double coeff = 2.0 * cos(2.0 * M_PI * (double)freq / (double)kVocSr);
	double s1 = 0, s2 = 0;
	for (int i = 0; i < n; ++i) {
		const double s0 = (double)x[i] + coeff * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	const double p = s2 * s2 + s1 * s1 - coeff * s1 * s2;
	return (float)sqrt(p > 0.0 ? p : 0.0);
}

struct HarmM {
	float centroid;
	float midToLow;
	int nHarm;
	float vocalSum;
};

static void MeasureHarm(const float* x, int n, float f0, HarmM* h)
{
	h->centroid = 0.f;
	h->midToLow = 0.f;
	h->nHarm = 0;
	h->vocalSum = 0.f;
	if (f0 < 60.f || f0 > 1100.f) return;
	float num = 0.f, den = 0.f, low = 0.f, mid = 0.f, peak = 0.f;
	float amps[10];
	int nh = 0;
	for (int k = 1; k <= 10; ++k) {
		const float f = f0 * (float)k;
		if (f > 4500.f) break;
		const float a = GoertzelAt(x, n, f);
		amps[nh++] = a;
		if (a > peak) peak = a;
		num += f * a;
		den += a;
		if (f < 220.f) low += a;
		else if (f < 3600.f) mid += a;
	}
	if (den < 1e-8f) return;
	h->centroid = num / den;
	h->midToLow = mid / (low + 1e-8f);
	h->vocalSum = mid;
	int cnt = 0;
	for (int i = 0; i < nh; ++i) {
		const float f = f0 * (float)(i + 1);
		const float w = (f > 500.f) ? powf(f / 500.f, 0.55f) : 1.f;
		if (amps[i] * w > peak * 0.11f) ++cnt;
	}
	h->nHarm = cnt;
}

static int Levinson(const float* r, float* a, int p)
{
	if (r[0] < 1e-10f || p > 18) return 0;
	float e = r[0];
	a[0] = 1.f;
	for (int i = 1; i <= p; ++i) a[i] = 0.f;
	for (int i = 1; i <= p; ++i) {
		float acc = r[i];
		for (int j = 1; j < i; ++j) acc -= a[j] * r[i - j];
		const float k = acc / e;
		if (k > 0.995f || k < -0.995f) return 0;
		float old[20];
		for (int j = 1; j < i; ++j) old[j] = a[j];
		for (int j = 1; j < i; ++j) a[j] = old[j] - k * old[i - j];
		a[i] = k;
		e *= (1.f - k * k);
		if (e < 1e-12f) return 0;
	}
	return 1;
}

static float HzToBark(float hz)
{
	if (hz < 1.f) hz = 1.f;
	const float x = hz / 7500.f;
	return 13.f * atanf(0.00076f * hz) + 3.5f * atanf(x * x);
}

struct VowelT { wchar_t ch; float f1; float f2; };

// 成人男性の日本語五母音。女声・子供は声道長スケールで吸収する。
static const VowelT kVowels[5] = {
	{ L'\u3042', 800.f, 1200.f },
	{ L'\u3044', 300.f, 2300.f },
	{ L'\u3046', 370.f, 1250.f },
	{ L'\u3048', 500.f, 1900.f },
	{ L'\u304A', 500.f, 800.f },
};

struct FormantPair {
	wchar_t vowel;
	float f1, f2, dist, prom;
};

static void ConsiderVowel(FormantPair* fp, float f1, float f2, float prom, float f0hint)
{
	if (!fp || f1 < 180.f || f2 < f1 + 150.f) return;
	float scC = 1.f;
	if (f0hint > 50.f) scC = 0.92f + (f0hint - 140.f) / 900.f;
	scC = Clampf(scC, 0.86f, 1.24f);
	const float scLo = (scC - 0.08f < 0.80f) ? 0.80f : scC - 0.08f;
	const float scHi = (scC + 0.08f > 1.36f) ? 0.36f + 1.0f : scC + 0.08f;
	const int atFund = (f0hint > 300.f && f1 <= f0hint * 1.14f) ? 1 : 0;
	for (float sc = scLo; sc <= scHi + 0.001f; sc += 0.025f) {
		const float nf1 = f1 / sc;
		const float nf2 = f2 / sc;
		for (int v = 0; v < 5; ++v) {
			float d1 = HzToBark(nf1) - HzToBark(kVowels[v].f1);
			// 女声の高音(f0 > F1)では第1倍音が本来のF1より高くなるため、基音位置なら低F1母音(い/う/え/お)のF1差を緩和する
			if (atFund && d1 > 0.f && kVowels[v].f1 <= 520.f) d1 *= 0.28f;
			const float d2 = HzToBark(nf2) - HzToBark(kVowels[v].f2);
			const float d = sqrtf(d1 * d1 + d2 * d2);
			if (d < fp->dist) {
				fp->dist = d;
				fp->vowel = kVowels[v].ch;
				fp->f1 = f1;
				fp->f2 = f2;
				fp->prom = prom;
			}
		}
	}
}

// 声の倍音だけの包絡。隣り合う倍音をそのまま F1/F2 にすると 2:3 になり「あ」へ化けるので、
// ガウシアンで滑らかな包絡にしてから、谷を挟んだ 2 つの山だけを取る。
static FormantPair MatchHarmEnv(const float* x, int n, float f0)
{
	FormantPair fp;
	fp.vowel = 0;
	fp.f1 = fp.f2 = 0.f;
	fp.dist = 99.f;
	fp.prom = 0.f;
	if (f0 < 95.f || f0 > 850.f) return fp;
	float hz[18], ampF1[18], ampF2[18];
	int nh = 0;
	float sum1 = 0.f, sum2 = 0.f;
	float eLoF1 = 0.f, eMidF1 = 0.f, eHiF1 = 0.f, eHiF2 = 0.f;
	const float sc = Clampf(0.92f + (f0 - 140.f) / 900.f, 0.88f, 1.24f);
	for (int k = 1; k <= 18; ++k) {
		const float f = f0 * (float)k;
		if (f > 4200.f) break;
		hz[nh] = f;
		const float r = GoertzelAt(x, n, f);
		// F1(〜950Hz)はそのままの振幅で口の開き具合(い/う vs え/お vs あ)を測り、
		// F2(900Hz〜)だけ声門減衰(-6dB/oct)を補正して「い」「え」の高域フォルマントを拾う
		ampF1[nh] = r;
		const float tilt2 = (f > 900.f) ? powf(f / 900.f, 0.78f) : 1.f;
		ampF2[nh] = r * tilt2;
		sum1 += ampF1[nh];
		sum2 += ampF2[nh];
		const float nf = f / sc;
		if (nf >= 220.f && nf < 450.f) eLoF1 += r;
		else if (nf >= 450.f && nf < 650.f) eMidF1 += r;
		else if (nf >= 650.f && nf <= 1020.f) eHiF1 += r;
		if (nf >= 1700.f && nf <= 3300.f) eHiF2 += ampF2[nh];
		++nh;
	}
	if (nh < 4 || sum1 < 1e-6f) return fp;
	const float mean1 = sum1 / (float)nh;
	const float mean2 = sum2 / (float)nh;

	float maxF1 = 0.f;
	for (int i = 0; i < nh; ++i) {
		if (hz[i] >= 220.f && hz[i] <= 980.f && ampF1[i] > maxF1) maxF1 = ampF1[i];
	}
	if (maxF1 < mean1 * 1.02f) return fp;

	for (int i = 0; i < nh; ++i) {
		const float f1 = hz[i];
		if (f1 < 220.f || f1 > 980.f) continue;
		if (ampF1[i] < maxF1 * 0.68f || ampF1[i] < mean1 * 0.90f) continue;
		float maxF2 = 0.f;
		for (int j = i + 1; j < nh; ++j) {
			if (hz[j] >= f1 + 220.f && hz[j] <= 3600.f && ampF2[j] > maxF2) maxF2 = ampF2[j];
		}
		if (maxF2 < mean2 * 0.80f) continue;
		for (int j = i + 1; j < nh; ++j) {
			const float f2 = hz[j];
			if (f2 < f1 + 220.f || f2 > 3600.f) continue;
			if (ampF2[j] < maxF2 * 0.68f || ampF2[j] < mean2 * 0.80f) continue;
			if (j == i + 1 && f0 < 260.f && f1 < 620.f) continue;
			const float p1 = ampF1[i] / (mean1 + 1e-8f);
			const float p2 = ampF2[j] / (mean2 + 1e-8f);
			const float prom = 0.52f * p1 + 0.48f * p2;
			if (prom < 1.18f) continue;
			FormantPair tr;
			tr.vowel = 0; tr.f1 = tr.f2 = 0.f; tr.dist = 99.f; tr.prom = 0.f;
			ConsiderVowel(&tr, f1, f2, prom * 1.28f, f0);
			if (!tr.vowel) continue;
			// 「あ」は実際に高F1帯(650〜1020Hz)が低F1帯(220〜450Hz)より強いときだけ
			if (tr.vowel == L'\u3042') {
				if (j == i + 1) tr.dist += 0.32f;
				if (f1 / sc < 620.f || eHiF1 < eLoF1 * 0.95f) {
					if (f2 / sc >= 1680.f) tr.vowel = (f1 / sc < 430.f) ? L'\u3044' : L'\u3048';
					else if (f1 / sc < 440.f) tr.vowel = L'\u3046';
					else tr.vowel = L'\u304A';
					if (tr.dist < 0.28f) tr.dist = 0.28f;
				}
			}
			if (tr.vowel == L'\u304A' && eHiF2 > (eLoF1 + eMidF1) * 0.70f) continue;
			const float effDist = tr.dist - 0.10f * Clampf(prom - 1.2f, 0.f, 1.4f);
			if (effDist < fp.dist) {
				fp = tr;
				fp.dist = Clampf(effDist, 0.08f, 99.f);
			}
		}
	}
	return fp;
}

// 鼻音「ん」の判定。口を閉じた鼻腔共鳴は 220〜380 Hz に集中し、800〜2200 Hz が抜ける。
static int IsNasalMurmur(const float* x, int n, float f0, const HarmM* h, float f1, float f2)
{
	if (f0 < 120.f || f0 > 650.f || !h || h->nHarm < 2) return 0;
	if (h->centroid > 620.f) return 0;
	if (f1 > 400.f && f2 > 950.f) return 0;
	float low = 0.f, mid = 0.f, hi = 0.f;
	for (int k = 1; k <= 12; ++k) {
		const float f = f0 * (float)k;
		if (f > 3600.f) break;
		const float a = GoertzelAt(x, n, f);
		if (f <= 440.f) low += a;
		else if (f <= 1900.f) mid += a;
		else hi += a;
	}
	return (low > 1e-5f && mid < low * 0.34f && hi < low * 0.25f) ? 1 : 0;
}

// 母音へ入る瞬間の子音行（0=母音のまま, 1=か, 2=さ, 3=た, 4=な, 5=は, 6=ま, 7=ら, 8=じ/ざ）。
static int DetectOnsetRow(const float* x, int n)
{
	if (!x || n < 512) return 0;
	const int seg = n / 5;
	float e0 = 0.f, e1 = 0.f, e3 = 0.f, hf0 = 0.f;
	int zc0 = 0, zc3 = 0;
	for (int i = 1; i < seg; ++i) {
		e0 += x[i] * x[i];
		const float d = x[i] - x[i - 1];
		hf0 += d * d;
		if ((x[i] >= 0.f) != (x[i - 1] >= 0.f)) ++zc0;
	}
	for (int i = seg; i < seg * 2; ++i) e1 += x[i] * x[i];
	for (int i = seg * 3; i < seg * 4; ++i) {
		e3 += x[i] * x[i];
		if ((x[i] >= 0.f) != (x[i - 1] >= 0.f)) ++zc3;
	}
	if (e3 < 1e-7f) return 0;
	const float zRate0 = (float)zc0 / (float)seg;
	const float zRate3 = (float)zc3 / (float)seg;
	const float hfRatio = hf0 / (e0 + 1e-8f);
	const float rise = e3 / (e0 + 1e-8f);
	if ((zRate0 > 0.26f || hfRatio > 0.95f) && zRate0 > zRate3 * 1.22f) {
		if (zRate0 > 0.32f || hfRatio > 1.35f) return 2; // さ/し/す/せ/そ
		return 8;                                        // ざ/じ/ず/ぜ/ぞ
	}
	if (rise > 3.2f && e1 > e0 * 1.8f) {
		if (zRate0 > 0.19f || hfRatio > 0.65f) return 3; // た/ち/つ/て/と
		return 1;                                        // か/き/く/け/こ
	}
	if (zRate0 > 0.19f && rise > 1.25f && rise <= 3.2f) return 5; // は/ひ/ふ/へ/ほ
	if (rise > 1.55f && rise <= 3.8f && zRate0 < 0.15f) return 6; // ま/な/ら
	return 0;
}

static wchar_t ComposeKana(int row, wchar_t v, float df2)
{
	if (v == L'\u3093') return v;
	int vi = -1;
	if (v == L'\u3042') vi = 0;
	else if (v == L'\u3044') vi = 1;
	else if (v == L'\u3046') vi = 2;
	else if (v == L'\u3048') vi = 3;
	else if (v == L'\u304A') vi = 4;
	if (vi < 0) return v;
	if (row == 6 && fabsf(df2) > 260.f) row = (df2 > 0.f) ? 7 : 4; // F2 遷移が大きければ ら/な
	static const wchar_t kTbl[9][5] = {
		{ L'\u3042', L'\u3044', L'\u3046', L'\u3048', L'\u304A' }, // あ
		{ L'\u304B', L'\u304D', L'\u304F', L'\u3051', L'\u3053' }, // か
		{ L'\u3055', L'\u3057', L'\u3059', L'\u305B', L'\u305D' }, // さ
		{ L'\u305F', L'\u3061', L'\u3064', L'\u3066', L'\u3068' }, // た
		{ L'\u306A', L'\u306B', L'\u306C', L'\u306D', L'\u306E' }, // な
		{ L'\u306F', L'\u3072', L'\u3075', L'\u3078', L'\u307B' }, // は
		{ L'\u307E', L'\u307F', L'\u3080', L'\u3081', L'\u3082' }, // ま
		{ L'\u3089', L'\u308A', L'\u308B', L'\u308C', L'\u308D' }, // ら
		{ L'\u3056', L'\u3058', L'\u305A', L'\u305C', L'\u305E' }, // ざ/じ
	};
	if (row < 0 || row > 8) row = 0;
	return kTbl[row][vi];
}

static FormantPair MatchVowel(const float* x, int n, float f0hint, int lpcOrder)
{
	FormantPair fp;
	fp.vowel = 0;
	fp.f1 = fp.f2 = 0.f;
	fp.dist = 99.f;
	fp.prom = 0.f;
	const int p = (lpcOrder < 8) ? 8 : (lpcOrder > 18 ? 18 : lpcOrder);
	float prev = 0.f;
	double ac[20];
	for (int i = 0; i <= p; ++i) ac[i] = 0;
	for (int i = 0; i < n; ++i) {
		const float w = 0.54f - 0.46f * cosf(2.f * (float)M_PI * (float)i / (float)(n - 1));
		const float y = (x[i] - 0.95f * prev) * w;
		prev = x[i];
		s_lpcIn[i] = y;
	}
	for (int lag = 0; lag <= p; ++lag) {
		double s = 0;
		for (int i = lag; i < n; ++i) s += (double)s_lpcIn[i] * (double)s_lpcIn[i - lag];
		ac[lag] = s;
	}
	ac[0] *= 1.001;
	float r[20], a[20];
	for (int i = 0; i <= p; ++i) r[i] = (float)ac[i];
	if (!Levinson(r, a, p)) return fp;

	enum { kBins = 160 };
	float env[kBins];
	const float fLo = 160.f, fHi = 4200.f;
	for (int b = 0; b < kBins; ++b) {
		const float hz = fLo + (fHi - fLo) * (float)b / (float)(kBins - 1);
		const float w = 2.f * (float)M_PI * hz / (float)kVocSr;
		float re = 1.f, im = 0.f;
		for (int k = 1; k <= p; ++k) {
			re -= a[k] * cosf((float)k * w);
			im += a[k] * sinf((float)k * w);
		}
		const float mag = 1.f / sqrtf(re * re + im * im + 1e-12f);
		env[b] = mag;
	}

	struct Pk { float hz, mag, prom; };
	Pk peaks[8];
	int npk = 0;
	for (int b = 2; b < kBins - 2 && npk < 8; ++b) {
		if (!(env[b] > env[b - 1] && env[b] >= env[b + 1])) continue;
		float left = env[b], right = env[b];
		const int span = 7;
		for (int j = b; j >= 0 && j > b - span; --j) if (env[j] < left) left = env[j];
		for (int j = b; j < kBins && j < b + span; ++j) if (env[j] < right) right = env[j];
		const float base = 0.5f * (left + right);
		const float prom = env[b] / (base + 1e-12f);
		if (prom < 1.35f) continue;
		peaks[npk].hz = fLo + (fHi - fLo) * (float)b / (float)(kBins - 1);
		peaks[npk].mag = env[b];
		peaks[npk].prom = prom;
		++npk;
	}
	if (npk < 2) return fp;

	float bestD = 1e9f;
	wchar_t bestV = 0;
	float bestF1 = 0, bestF2 = 0, bestProm = 0;
	for (int i = 0; i < npk; ++i) {
		if (peaks[i].hz < 190.f || peaks[i].hz > 1150.f) continue;
		for (int j = 0; j < npk; ++j) {
			if (peaks[j].hz < peaks[i].hz + 170.f || peaks[j].hz > 3700.f) continue;
			const float f1 = peaks[i].hz;
			const float f2 = peaks[j].hz;
			const float pr = (peaks[i].prom < peaks[j].prom) ? peaks[i].prom : peaks[j].prom;
			FormantPair trial;
			trial.vowel = 0;
			trial.f1 = trial.f2 = 0.f;
			trial.dist = bestD;
			trial.prom = 0.f;
			ConsiderVowel(&trial, f1, f2, pr, f0hint);
			if (trial.vowel && trial.dist < bestD) {
				bestD = trial.dist;
				bestV = trial.vowel;
				bestF1 = trial.f1;
				bestF2 = trial.f2;
				bestProm = trial.prom;
			}
		}
	}
	fp.f1 = bestF1;
	fp.f2 = bestF2;
	fp.dist = bestD;
	fp.prom = bestProm;
	fp.vowel = bestV;
	return fp;
}

struct YinCand { float f0; float ap; int tau; };

static int CollectYin(const float* x, int n, YinCand* out, int maxOut)
{
	const int tauMin = (int)((float)kVocSr / 980.f);
	int tauMax = (int)((float)kVocSr / 70.f);
	if (tauMax > n / 2) tauMax = n / 2;
	if (tauMax > 380) tauMax = 380;
	if (tauMax <= tauMin + 4 || maxOut <= 0) return 0;
	const int W = n - tauMax;
	if (W < 80) return 0;

	float diff[400];
	float cmnd[400];
	diff[0] = 0.f;
	for (int tau = 1; tau <= tauMax; ++tau) {
		double s = 0;
		for (int j = 0; j < W; ++j) {
			const float d = x[j] - x[j + tau];
			s += (double)d * (double)d;
		}
		diff[tau] = (float)s;
	}
	cmnd[0] = 1.f;
	double acc = 0;
	for (int tau = 1; tau <= tauMax; ++tau) {
		acc += diff[tau];
		cmnd[tau] = (acc > 0.0) ? (float)((double)diff[tau] * (double)tau / acc) : 1.f;
	}

	int nOut = 0;
	for (int tau = tauMin; tau < tauMax; ++tau) {
		if (!(cmnd[tau] <= cmnd[tau - 1] && cmnd[tau] < cmnd[tau + 1])) continue;
		if (cmnd[tau] > 0.52f) continue;
		const int t = tau;
		float better = (float)t;
		if (t > 0 && t + 1 <= tauMax) {
			const float s0 = cmnd[t - 1], s1 = cmnd[t], s2 = cmnd[t + 1];
			const float denom = s0 - 2.f * s1 + s2;
			if (fabsf(denom) > 1e-6f) {
				float delta = 0.5f * (s0 - s2) / denom;
				delta = Clampf(delta, -0.9f, 0.9f);
				better = (float)t + delta;
			}
		}
		if (better < 2.f) continue;
		const float f0 = (float)kVocSr / better;
		if (f0 < 70.f || f0 > 980.f) continue;
		int dup = -1;
		for (int i = 0; i < nOut; ++i) {
			const float r = (f0 > out[i].f0) ? f0 / out[i].f0 : out[i].f0 / f0;
			if (r < 1.04f) { dup = i; break; }
		}
		if (dup >= 0) {
			if (cmnd[t] < out[dup].ap) { out[dup].f0 = f0; out[dup].ap = cmnd[t]; out[dup].tau = t; }
			continue;
		}
		int slot = nOut;
		if (nOut >= maxOut) {
			slot = 0;
			for (int i = 1; i < nOut; ++i) if (out[i].ap > out[slot].ap) slot = i;
			if (cmnd[t] >= out[slot].ap) continue;
		} else {
			++nOut;
		}
		out[slot].f0 = f0;
		out[slot].ap = cmnd[t];
		out[slot].tau = t;
	}
	return nOut;
}

static void CombAtten(float* x, int n, int tau)
{
	if (tau < 2 || tau >= n / 2) return;
	for (int i = 0; i < n; ++i) {
		const float prev = (i >= tau) ? x[i - tau] : 0.f;
		s_comb[i] = x[i] - 0.82f * prev;
	}
	memcpy(x, s_comb, (size_t)n * sizeof(float));
}

static void PrepareVocalTime(const float* mid, const float* side, int N)
{
	const int tap = 24;
	for (int i = 0; i < N; ++i) {
		float w = 1.f;
		if (i < tap) w = 0.5f - 0.5f * cosf((float)M_PI * (float)i / (float)tap);
		else if (i > N - 1 - tap) w = 0.5f - 0.5f * cosf((float)M_PI * (float)(N - 1 - i) / (float)tap);
		s_re[i] = mid[i] * w;
		s_im[i] = 0.f;
		s_sre[i] = side[i] * w;
		s_sim[i] = 0.f;
	}
	FftRadix2(s_re, s_im, N, -1);
	FftRadix2(s_sre, s_sim, N, -1);
	float eMid = 0.f, eMask = 0.f, vMid = 0.f, vSide = 0.f;
	for (int k = 0; k < N; ++k) {
		const float magM = sqrtf(s_re[k] * s_re[k] + s_im[k] * s_im[k]);
		const float magS = sqrtf(s_sre[k] * s_sre[k] + s_sim[k] * s_sim[k]);
		int bin = k;
		if (bin > N / 2) bin = N - bin;
		const float freq = (float)bin * (float)kVocSr / (float)N;
		if (freq >= 180.f && freq <= 3400.f) {
			vMid += magM;
			vSide += magS;
		}
		float center = magM - 0.85f * magS;
		if (center < 0.f) center = 0.f;
		float mask = center / (magM + 1e-12f);
		if (mask > 1.f) mask = 1.f;
		if (freq < 125.f) mask *= 0.06f;
		else if (freq < 195.f) mask *= 0.25f;
		else if (freq > 4300.f) mask *= 0.22f;
		eMid += magM;
		eMask += magM * mask;
		s_re[k] *= mask;
		s_im[k] *= mask;
	}
	const int useMask = (eMask >= 0.18f * eMid);
	s_lastCentRatio = (vMid + vSide > 1e-8f) ? (vMid / (vMid + vSide)) : 0.7f;
	if (!useMask) {
		for (int i = 0; i < N; ++i) s_time[i] = mid[i];
	} else {
		FftRadix2(s_re, s_im, N, +1);
		const float inv = 1.f / (float)N;
		for (int i = 0; i < N; ++i) s_time[i] = s_re[i] * inv;
	}
	float mean = 0.f;
	for (int i = 0; i < N; ++i) mean += s_time[i];
	mean /= (float)N;
	const float R = expf(-2.f * (float)M_PI * 165.f / (float)kVocSr);
	float hp = 0.f, x1 = 0.f;
	for (int i = 0; i < N; ++i) {
		const float x = s_time[i] - mean;
		hp = (x - x1) + R * hp;
		x1 = x;
		s_time[i] = hp;
	}
}

static void ClearRaw(EqKeyVocalRaw* out)
{
	if (!out) return;
	out->vowel = 0;
	out->f0Hz = 0.f;
	out->chorus = 0;
	out->f1Hz = 0.f;
	out->f2Hz = 0.f;
	out->barkDist = 99.f;
	out->aperiodicity = 1.f;
	out->centroidHz = 0.f;
	out->nHarm = 0;
}

static int CandidateOk(const HarmM* h, float ap, float bark, float prom, float f0)
{
	const int needHarm = (bark < 0.55f || f0 > 380.f) ? 2 : 3;
	if (h->nHarm < needHarm) return 0;
	if (h->centroid < 380.f && h->midToLow < 0.80f) return 0;
	if (f0 < 120.f && h->centroid < 520.f && bark > 0.50f) return 0;
	// 伴奏だけの区間は左右に広がるか、低音で周期が浅い。
	if (s_lastCentRatio < 0.55f && ap > 0.16f) return 0;
	if (f0 < 165.f) return 0;
	if (f0 < 200.f && (ap > 0.14f || h->centroid < 620.f || h->midToLow < 1.45f || s_lastCentRatio < 0.62f)) return 0;
	// 歌の最中は伴奏が混ざるので YIN の非周期性が 0.3 前後まで上がる。声域でフォルマントが合うときはそこまで許す。
	float apLim = 0.20f;
	if (f0 >= 200.f && f0 <= 900.f && bark < 0.80f && prom >= 1.40f && h->nHarm >= 3) apLim = 0.46f;
	else if (f0 >= 190.f && bark < 0.95f && prom >= 1.45f && h->nHarm >= 3) apLim = 0.34f;
	if (s_lastCentRatio < 0.50f && apLim > 0.22f) apLim = 0.22f;
	if (ap > apLim) {
		// すでに追っている音程なら、子音や息で少し崩れても同じ音としてつなぐ。
		const int held = (s_lockF0 > 80.f && f0 > 80.f
			&& fabsf(f0 / s_lockF0 - 1.f) < 0.08f
			&& ap < 0.52f && bark < 0.95f && f0 >= 185.f);
		if (!held) return 0;
	}
	if (bark > 1.00f) return 0;
	if (prom < 1.32f) return 0;
	return 1;
}

static int SameHarmonicStack(float a, float b)
{
	if (a < 1.f || b < 1.f) return 0;
	const float r = (a > b) ? a / b : b / a;
	for (int k = 1; k <= 5; ++k) {
		if (fabsf(r - (float)k) / (float)k < 0.06f) return 1;
	}
	return 0;
}

void EqKeyVocalAnalyze(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate, EqKeyVocalRaw* out)
{
	ClearRaw(out);
	if (!out || !bufferL || sampleCount < 512) return;
	const int got = DownTo16k(bufferL, bufferR, sampleCount, sampleRate, s_mid, s_side, 2048);
	int N = 0;
	if (got >= kVocN) N = kVocN;
	else if (got >= 512) N = 512;
	else return;
	const float* mid = s_mid + (got - N);
	const float* side = s_side + (got - N);
	PrepareVocalTime(mid, side, N);

	float energy = 0.f;
	for (int i = 0; i < N; ++i) energy += s_time[i] * s_time[i];
	if (energy < 1e-8f) { out->nHarm = -1; return; }

	YinCand cands[8];
	int nc = CollectYin(s_time, N, cands, 6);
	if (nc <= 0) { out->nHarm = -2; return; }

	int dull = -1;
	float dullAp = 1.f;
	int clearVoice = 0;
	for (int i = 0; i < nc; ++i) {
		// 低い音が周期的でも、それはベースであって声の証拠ではない。
		if (cands[i].f0 >= 200.f && cands[i].ap < 0.12f) clearVoice = 1;
		if (cands[i].f0 < 175.f && cands[i].ap < dullAp) {
			dull = i;
			dullAp = cands[i].ap;
		}
	}
	if (clearVoice && dullAp > 0.28f) dull = -1;

	for (int pass = 0; pass < 2; ++pass) {
	if (pass == 1) {
		if (dull < 0) return;
		CombAtten(s_time, N, cands[dull].tau);
		nc = CollectYin(s_time, N, cands, 6);
		if (nc <= 0) return;
		dull = -1;
	}

	float f0hint = cands[0].f0;
	float hintAp = cands[0].ap;
	for (int i = 1; i < nc; ++i) {
		// 同じ倍音列なら高い方（オクターブ上）へ上げない。より低い基音が同程度に周期的ならそちらを採る。
		const int lowerFund = (cands[i].f0 < f0hint * 0.72f && cands[i].f0 >= 150.f
			&& cands[i].ap <= hintAp + 0.10f && SameHarmonicStack(cands[i].f0, f0hint));
		const int clearer = (cands[i].ap + 0.05f < hintAp && !SameHarmonicStack(cands[i].f0, f0hint));
		if (lowerFund || clearer) {
			f0hint = cands[i].f0;
			hintAp = cands[i].ap;
		}
	}
	FormantPair lo = MatchVowel(s_time, N, f0hint, 12);
	FormantPair hi = MatchVowel(s_time, N, f0hint, 16);
	FormantPair fp = (hi.dist + 0.04f < lo.dist) ? hi : lo;
	if (!fp.vowel || fp.dist > 0.72f || fp.prom < 1.55f) {
		for (int i = 0; i < nc; ++i) {
			if (cands[i].ap > 0.52f) continue;
			FormantPair hp = MatchHarmEnv(s_time, N, cands[i].f0);
			float bias = 0.08f;
			if (cands[i].f0 < 165.f) bias = 0.28f;
			else if (cands[i].f0 >= 190.f && cands[i].f0 <= 620.f) bias = 0.0f;
			if (hp.vowel && hp.dist + bias < fp.dist) {
				fp = hp;
				f0hint = cands[i].f0;
				hintAp = cands[i].ap;
			}
		}
	}
	out->f1Hz = fp.f1;
	out->f2Hz = fp.f2;
	out->barkDist = fp.dist;
	out->f0Hz = f0hint;
	out->aperiodicity = hintAp;
	if (!fp.vowel || fp.dist > 1.00f || fp.prom < 1.35f) {
		for (int i = 0; i < nc; ++i) {
			if (cands[i].ap > 0.16f || cands[i].f0 < 175.f || s_lastCentRatio < 0.62f) continue;
			HarmM nh;
			MeasureHarm(s_time, N, cands[i].f0, &nh);
			if (IsNasalMurmur(s_time, N, cands[i].f0, &nh, fp.f1, fp.f2)) {
				out->vowel = L'\u3093';
				out->f0Hz = cands[i].f0;
				out->aperiodicity = cands[i].ap;
				out->centroidHz = nh.centroid;
				out->nHarm = nh.nHarm;
				out->barkDist = 0.35f;
				return;
			}
		}
		out->nHarm = -3;
		if (pass == 0) continue;
		return;
	}

	int keep[8];
	int nk = 0;
	float bestAp = 1.f;
	int bestI = -1;
	for (int i = 0; i < nc; ++i) {
		HarmM h;
		MeasureHarm(s_time, N, cands[i].f0, &h);
		if (!CandidateOk(&h, cands[i].ap, fp.dist, fp.prom, cands[i].f0)) continue;
		keep[nk++] = i;
		if (cands[i].ap < bestAp) { bestAp = cands[i].ap; bestI = i; }
	}
	if (bestI < 0) {
		out->nHarm = -4;
		if (pass == 0) continue;
		return;
	}

	// 母音に使った高さに近く、かつ倍音列の中では一番低い基音を採る。
	// 高い候補へ上げると 2*f0 や 3*f0 を歌の音程として出してしまう。
	int showI = bestI;
	float bestDf = 1e9f;
	for (int i = 0; i < nk; ++i) {
		const float df = fabsf(cands[keep[i]].f0 - f0hint);
		if (df < bestDf) { bestDf = df; showI = keep[i]; }
	}
	for (int i = 0; i < nk; ++i) {
		const int ix = keep[i];
		if (!(cands[ix].f0 < cands[showI].f0 && cands[ix].f0 >= 155.f
			&& cands[ix].ap <= cands[showI].ap + 0.08f
			&& SameHarmonicStack(cands[ix].f0, cands[showI].f0)))
			continue;
		const float aLo = GoertzelAt(s_time, N, cands[ix].f0);
		const float aHi = GoertzelAt(s_time, N, cands[showI].f0);
		// 低い側に実音が無いなら、それは基音ではなく半分の周期。
		if (aLo < aHi * 0.42f) continue;
		showI = ix;
	}
	float shownF0 = cands[showI].f0;
	{
		const float a1 = GoertzelAt(s_time, N, shownF0);
		const float a2 = GoertzelAt(s_time, N, shownF0 * 2.f);
		// 低い候補の実音が弱く、1オクターブ上が強いときは、半分の周期を基音と見間違えている。
		if (shownF0 < 250.f && shownF0 * 2.f < 920.f && a2 > a1 * 1.8f) {
			int up = -1;
			for (int i = 0; i < nk; ++i) {
				const int ix = keep[i];
				const float r = cands[ix].f0 / shownF0;
				if (r > 1.85f && r < 2.20f && cands[ix].ap <= cands[showI].ap + 0.12f)
					up = ix;
			}
			if (up >= 0) {
				showI = up;
				shownF0 = cands[showI].f0;
			} else {
				shownF0 *= 2.f;
			}
		}
	}
	if (shownF0 < 155.f) showI = -1;
	for (int i = 0; showI >= 0 && i < nc; ++i) {
		if (cands[i].f0 >= 175.f || cands[i].f0 < 60.f) continue;
		if (cands[i].ap > cands[showI].ap + 0.06f) continue;
		// 高い旋律がベース音の上に乗っているだけなら消さない。低い候補がベースより明瞭なときだけ落とす。
		if (shownF0 >= 300.f && cands[i].ap + 0.02f >= cands[showI].ap) continue;
		if (!SameHarmonicStack(cands[i].f0, shownF0)) continue;
		HarmM bassH;
		MeasureHarm(s_time, N, cands[i].f0, &bassH);
		// 選んだ高さがベースの倍音そのものなら、声の音程ではない。
		if (bassH.centroid < 500.f && bassH.midToLow < 1.7f) showI = -1;
	}
	if (showI < 0) {
		out->nHarm = -4;
		if (pass == 0) continue;
		return;
	}
	// はっきりした基音があるとき、浅い谷は同じ声の漏れ。二声はどちらも同程度に周期が崩れる。
	if (bestAp < 0.08f) {
		int w = 0;
		for (int i = 0; i < nk; ++i) {
			if (cands[keep[i]].ap < 0.14f) keep[w++] = keep[i];
		}
		nk = w;
	}
	int chorus = 0;
	for (int i = 0; i < nk; ++i) {
		if (keep[i] == showI) continue;
		if (!SameHarmonicStack(cands[showI].f0, cands[keep[i]].f0) && cands[keep[i]].ap < 0.40f)
			chorus = 1;
	}
	HarmM shown;
	MeasureHarm(s_time, N, cands[showI].f0, &shown);
	// LPC が 2*f0 と 3*f0 に張り付いて「あ」と誤判定した場合、第1倍音(基音)の方が強ければ口の閉じた母音へ補正する
	if (fp.vowel == L'\u3042' && cands[showI].f0 >= 230.f) {
		const float sf0 = cands[showI].f0;
		const float a1 = GoertzelAt(s_time, N, sf0);
		const float a2 = GoertzelAt(s_time, N, sf0 * 2.f);
		if (a1 > a2 * 1.12f) {
			float hiE = 0.f, midE = 0.f;
			for (int k = 3; k <= 10; ++k) {
				const float fk = sf0 * (float)k;
				if (fk > 3600.f) break;
				const float ak = GoertzelAt(s_time, N, fk) * powf(fk / 900.f, 0.75f);
				if (fk >= 1750.f) hiE += ak;
				else midE += ak;
			}
			if (hiE > midE * 0.82f) fp.vowel = (sf0 < 380.f) ? L'\u3044' : L'\u3048';
			else fp.vowel = (sf0 < 410.f) ? L'\u3046' : L'\u304A';
		}
	}
	if (fp.dist > 0.62f && cands[showI].ap < 0.24f && IsNasalMurmur(s_time, N, cands[showI].f0, &shown, fp.f1, fp.f2))
		fp.vowel = L'\u3093';
	out->vowel = fp.vowel;
	out->f0Hz = shownF0;
	out->chorus = chorus;
	out->aperiodicity = cands[showI].ap;
	out->centroidHz = shown.centroid;
	out->nHarm = shown.nHarm;
	return;
	}
}

static void FormatMelody(wchar_t* out, int cap, wchar_t vowel, int chorus, float f0)
{
	if (!out || cap < 6) return;
	if (!vowel || !(f0 > 60.f && f0 < 2000.f)) {
		WCopy(out, cap, L"[   ]");
		return;
	}
	int p = 0;
	if (chorus && p + 1 < cap) out[p++] = L'+';
	int midi = (int)floor(69.5 + 12.0 * log(f0 / 440.0) / log(2.0));
	if (midi < 0) midi = 0;
	if (midi > 120) midi = 120;
	const int pc = midi % 12;
	const int oct = midi / 12 - 1;
	const wchar_t* nm = kNote[pc];
	while (*nm && p + 1 < cap) out[p++] = *nm++;
	if (oct < 0) {
		if (p + 1 < cap) out[p++] = L'-';
		const int ao = -oct;
		if (p + 1 < cap) out[p++] = (wchar_t)(L'0' + (ao % 10));
	} else if (oct >= 10) {
		if (p + 2 < cap) {
			out[p++] = (wchar_t)(L'0' + oct / 10);
			out[p++] = (wchar_t)(L'0' + oct % 10);
		}
	} else if (p + 1 < cap) {
		out[p++] = (wchar_t)(L'0' + oct);
	}
	while (p < 3 && p + 1 < cap) out[p++] = L' ';
	out[p] = 0;
}

static float MedianF0()
{
	if (s_f0HistN <= 0) return 0.f;
	float t[5];
	for (int i = 0; i < s_f0HistN; ++i) t[i] = s_f0Hist[i];
	for (int i = 0; i < s_f0HistN; ++i) {
		for (int j = i + 1; j < s_f0HistN; ++j) if (t[j] < t[i]) {
			float s = t[i]; t[i] = t[j]; t[j] = s;
		}
	}
	return t[s_f0HistN / 2];
}

void EqKeyVocalMelody(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate, int isSilent, wchar_t* melodyOut, int melodyCap)
{
	if (!melodyOut || melodyCap < 6) return;
	if (isSilent) {
		EqKeyVocalReset();
		WCopy(melodyOut, melodyCap, L"[   ]");
		return;
	}
	EqKeyVocalRaw raw;
	EqKeyVocalAnalyze(bufferL, bufferR, sampleCount, sampleRate, &raw);
	// 開始は「声域・フォルマントが近い・周期がはっきり」だけ。追従中は伴奏混じりのフレームも同じフレーズとして残す。
	const int strong = (raw.vowel && raw.f0Hz >= 210.f && raw.f0Hz <= 920.f
		&& raw.aperiodicity <= 0.30f && raw.barkDist < 0.72f);
	const int follow = (s_vowelShow && raw.vowel && raw.f0Hz >= 185.f && raw.f0Hz <= 940.f
		&& raw.aperiodicity <= 0.50f && raw.barkDist < 1.00f);
	const int pitched = strong || follow;
	// 表示中の音程から整数倍だけ飛んだ候補は、1フレームでは採用しない。
	const int harmonicFlip = (pitched && s_lockF0 > 60.f && SameHarmonicStack(raw.f0Hz, s_lockF0)
		&& (raw.f0Hz > s_lockF0 * 1.2f || raw.f0Hz < s_lockF0 * 0.84f));
	if (harmonicFlip) {
		if (s_f0HistN < 5) s_f0Hist[s_f0HistN++] = raw.f0Hz;
		else {
			memmove(s_f0Hist, s_f0Hist + 1, 4 * sizeof(float));
			s_f0Hist[4] = raw.f0Hz;
		}
		int agree = 0;
		for (int i = 0; i < s_f0HistN; ++i) {
			const float r = s_f0Hist[i] / raw.f0Hz;
			if (r > 0.94f && r < 1.06f) ++agree;
		}
		// 声域の音を、ベースの半分の高さへは数フレーム続けてからしか落とさない。上への直しは早く取る。
		const int downward = (s_lockF0 >= 250.f && raw.f0Hz < s_lockF0 * 0.60f);
		if (agree >= (downward ? 5 : 2)) {
			s_vowelStreak = 2;
			s_vowelMiss = 0;
			s_vowelShow = raw.vowel;
			s_lockF0 = raw.f0Hz;
			s_f0HistN = 1;
			s_f0Hist[0] = raw.f0Hz;
		}
	} else if (pitched) {
		const float prev = (s_f0HistN > 0) ? s_f0Hist[s_f0HistN - 1] : 0.f;
		const float ratio = (prev > 60.f) ? (raw.f0Hz / prev) : 1.f;
		// まだ出ていないときの大きな飛びだけやり直す。歌の音程移動では表示を消さない。
		if (!s_vowelShow && prev > 60.f && (ratio > 1.26f || ratio < 0.79f)) {
			s_vowelStreak = 1;
			s_f0HistN = 0;
		} else {
			++s_vowelStreak;
		}
		s_vowelMiss = 0;
		if (s_f0HistN < 5) s_f0Hist[s_f0HistN++] = raw.f0Hz;
		else {
			memmove(s_f0Hist, s_f0Hist + 1, 4 * sizeof(float));
			s_f0Hist[4] = raw.f0Hz;
		}
		if (raw.chorus) {
			if (s_chorusLatch < 3) ++s_chorusLatch;
		} else if (s_chorusLatch > 0) {
			--s_chorusLatch;
		}
		if (s_vowelStreak >= 2) {
			s_vowelShow = raw.vowel;
			s_lockF0 = MedianF0();
		}
	} else {
		++s_vowelMiss;
		// フレーズ中の子音・息は 200ms 程度は直前の音程を残す。無人声が続けば消す。
		if (s_vowelMiss >= (s_vowelShow ? 10 : 3)) {
			s_vowelShow = 0;
			s_vowelPend = 0;
			s_vowelStreak = 0;
			s_f0HistN = 0;
			s_chorusLatch = 0;
			s_lockF0 = 0.f;
		}
	}
	if (!s_vowelShow || s_lockF0 < 60.f) {
		WCopy(melodyOut, melodyCap, L"[   ]");
		return;
	}
	FormatMelody(melodyOut, melodyCap, s_vowelShow, s_chorusLatch >= 2, s_lockF0);
}
