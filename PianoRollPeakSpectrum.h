#pragma once
// 検出スペクトル: 解析窓の FFT から正弦波成分を取り出し、鍵へ集める。
//
// 48kHz では ScaleWinSamples を 2 のべき乗(8192)へ揃えたので、ゼロ詰めのリンギングは出ない。
// ここではさらに次の 3 つを行う。
//  1) 雑音床は鍵の周囲(±2 半音、最低 32 ビン)の 25% 分位点から求める。
//     高調波が並ぶ低〜中音でもビンの 3/4 が山で埋まることはないので、信号が床に化けない。
//  2) ピークの振幅は放物線補間と 3 ビン和(Hann 主ローブ積分)の大きい方で採り、
//     真の周波数から最も近い 1 つの鍵へ入れる。隣の鍵への主ローブ漏れは出ず、
//     窓の中で音が動いて山が太ってもエネルギーが落ちない。
//  3) 連続周波数の調波サリエンス: n*f0 の位置に本当に局所ピークがあるときだけ
//     S/N[dB] を足す。第 3・5・7 倍音が平均律の鍵中心から外れても基音へ回収できる。
#include <cmath>
#include <cstring>
#include "PianoKeyTable.h"

namespace PianoRollPeakSpectrum
{
    static constexpr int KEYS = PianoKey::COUNT;
    static constexpr int FFT_MAX = 16384;
    static constexpr int PEAK_MAX = 768;
    static constexpr int SAL_HARMONICS = 10;
    static constexpr float SIDELOBE_HANN_DB = -31.5f;
    static constexpr float SIDELOBE_BLACKMAN_DB = -58.0f;

    struct TuneAcc
    {
        double c = 0.0;
        double s = 0.0;
        double w = 0.0;
    };

    class Analyzer
    {
    public:
        void Analyze(const double* windowed, int n, int sampleRate, float sidelobeDb,
            int keyLo, int keyHi, float tuneSemis, float peakSnrDb,
            float* outMag, float* outFloor, float* outHarmSal, TuneAcc* tune)
        {
            if (!windowed || !outMag || !outFloor || sampleRate < 8000) return;
            if (keyLo < 0) keyLo = 0;
            if (keyHi > KEYS) keyHi = KEYS;
            for (int k = keyLo; k < keyHi; ++k) {
                outMag[k] = 0.0f;
                outFloor[k] = 0.0f;
                if (outHarmSal) outHarmSal[k] = 0.0f;
            }
            if (keyLo >= keyHi || n < 64) return;
            if (n > FFT_MAX) {
                windowed += n - FFT_MAX;
                n = FFT_MAX;
            }

            int m = 64;
            while (m < n) m <<= 1;
            Prepare(m);
            for (int i = 0; i < n; ++i) {
                m_re[i] = windowed[i];
                m_im[i] = 0.0;
            }
            for (int i = n; i < m; ++i) {
                m_re[i] = 0.0;
                m_im[i] = 0.0;
            }
            Fft();

            const int half = m / 2;
            for (int b = 0; b <= half; ++b)
                m_pow[b] = (float)(m_re[b] * m_re[b] + m_im[b] * m_im[b]);

            const double binHz = (double)sampleRate / (double)m;
            const double resBins = (double)m / (double)n;
            const double tuneMul = pow(2.0, (double)tuneSemis / 12.0);
            const float magScale = 2.5f / (float)n;
            const float lobeNorm = ((sidelobeDb < -45.0f) ? 1.70f : 1.50f) * (float)resBins;

            // 1) 雑音床:
            //    低〜中音域(〜350Hz)は 8192 点 FFT でも 1 オクターブあたり 10〜40 ビンしかなく、
            //    和音や倍音の主ローブ裾(±3 ビン)が帯域全体を埋めるため、狭帯域の中央値を取ると
            //    「楽音自身の裾」を雑音床と誤認してノートが全滅する。
            //    一方、5kHz 以上の超高域を含めると無音ビンばかりで床が 0 に張り付き過剰検出になる。
            //    そこで、ビン間隔が十分に広く(1半音=4〜30ビン)かつ常に音楽成分が存在する
            //    中域(350Hz〜3200Hz)の非ピークビンの 25% 分位点を基準フロアとし、
            //    中高域(key>=64)のみ局所(±6半音・最低32ビン)の 25% 分位点とブレンドする。
            unsigned char isPeakLobe[FFT_MAX / 2 + 4];
            memset(isPeakLobe, 0, (size_t)(half + 2));
            for (int b = 2; b <= half - 2; ++b) {
                if (m_pow[b] > m_pow[b - 1] && m_pow[b] >= m_pow[b + 1]) {
                    isPeakLobe[b - 1] = 1;
                    isPeakLobe[b] = 1;
                    isPeakLobe[b + 1] = 1;
                }
            }

            int gCnt = 0;
            const int bMidLo = (int)ceil(350.0 / binHz);
            const int bMidHi = (int)floor(3200.0 / binHz);
            for (int b = (bMidLo > 3 ? bMidLo : 3); b <= (bMidHi < half - 2 ? bMidHi : half - 2) && gCnt < (FFT_MAX / 2); ++b) {
                if (!isPeakLobe[b]) m_sel[gCnt++] = m_pow[b];
            }
            const float midBandFloor = (gCnt > 8) ? SelectKth(m_sel, gCnt, gCnt / 4) : 0.0f;

            float noisePow[KEYS];
            for (int k = 0; k < KEYS; ++k) {
                if (k < 64) {
                    // 低〜中低音(E4未満)はビン裾重なりによる自己マスクを防ぐため基準フロアを使用
                    const float bandMul = (k < 48) ? 1.15f : 1.00f;
                    noisePow[k] = midBandFloor * bandMul;
                    continue;
                }
                const double fc = (double)PianoKey::KeyHz(k) * tuneMul;
                double b0 = fc * 0.70710678 / binHz; // ±6 半音
                double b1 = fc * 1.41421356 / binHz;
                const double minW = 32.0 * resBins;
                if (b1 - b0 < minW) {
                    const double c = fc / binHz;
                    b0 = c - minW * 0.5;
                    b1 = c + minW * 0.5;
                }
                int i0 = (int)floor(b0);
                int i1 = (int)ceil(b1);
                if (i0 < 2) i0 = 2;
                if (i1 > half - 2) i1 = half - 2;
                int cnt = 0;
                for (int b = i0; b <= i1; ++b) {
                    if (!isPeakLobe[b]) m_sel[cnt++] = m_pow[b];
                }
                float locFloor = midBandFloor;
                if (cnt >= 8) {
                    locFloor = SelectKth(m_sel, cnt, cnt / 4);
                }
                else {
                    cnt = 0;
                    for (int b = i0; b <= i1; ++b) m_sel[cnt++] = m_pow[b];
                    if (cnt > 0) locFloor = SelectKth(m_sel, cnt, cnt / 5);
                }
                // 高域で和音が密集しても基準フロアの 1.6 倍までに抑え、メロディ消失を防ぐ
                if (midBandFloor > 0.0f && locFloor > midBandFloor * 1.6f)
                    locFloor = midBandFloor * 1.6f;
                if (locFloor < midBandFloor * 0.45f)
                    locFloor = midBandFloor * 0.45f;
                noisePow[k] = locFloor;
            }
            for (int k = keyLo; k < keyHi; ++k)
                outFloor[k] = sqrtf(noisePow[k]) * magScale;

            // 2) 局所ピークの抽出 → サイドローブ除去 → 最も近い 1 つの鍵へ集約。
            const double fLo = (double)PianoKey::KeyHz(keyLo) * tuneMul * 0.97153;
            const double fHi = (double)PianoKey::KeyHz(keyHi - 1) * tuneMul * 1.02930;
            int bLo = (int)floor(fLo / binHz) - 1;
            int bHi = (int)ceil(fHi / binHz) + 1;
            if (bLo < 2) bLo = 2;
            if (bHi > half - 2) bHi = half - 2;

            const float snrLin = powf(10.0f, peakSnrDb / 10.0f);
            int np = 0;
            for (int b = bLo; b <= bHi && np < PEAK_MAX; ++b) {
                const float p = m_pow[b];
                if (!(p > m_pow[b - 1] && p >= m_pow[b + 1])) continue;
                const int kn = KeyOfHz((double)b * binHz, tuneSemis);
                const float np0 = noisePow[kn];
                const float a = 10.0f * log10f(m_pow[b - 1] + 1.0e-30f);
                const float c = 10.0f * log10f(p);
                const float e = 10.0f * log10f(m_pow[b + 1] + 1.0e-30f);
                const float den = a - 2.0f * c + e;
                float delta = (den < -1.0e-6f) ? 0.5f * (a - e) / den : 0.0f;
                if (delta > 0.5f) delta = 0.5f;
                if (delta < -0.5f) delta = -0.5f;
                const float peakDb = c - 0.25f * (a - e) * delta;
                float pkPow = powf(10.0f, peakDb / 10.0f);
                // 窓内でビブラートやエンベロープ変動がある音は主ローブ 3 ビンへエネルギーが分散するため、
                // 3 ビン積分値との大きい方を採る(割り当て先は単一鍵 k のみなので隣鍵へは漏れない)。
                const float lobe = (m_pow[b - 1] + p + m_pow[b + 1]) / lobeNorm;
                if (lobe > pkPow) pkPow = lobe;
                const float snrNeed = (kn < 60) ? (snrLin * 0.70f) : ((kn < 84) ? (snrLin * 0.85f) : snrLin);
                if (pkPow < 1.0e-20f || pkPow <= np0 * snrNeed) continue;
                m_peakBin[np] = (float)b + delta;
                m_peakPow[np] = pkPow;
                m_peakSnr[np] = (np0 > 0.0f) ? pkPow / np0 : 1.0e6f;
                ++np;
            }

            int order[PEAK_MAX];
            for (int i = 0; i < np; ++i) {
                const float v = m_peakPow[i];
                int j = i;
                while (j > 0 && m_peakPow[order[j - 1]] < v) {
                    order[j] = order[j - 1];
                    --j;
                }
                order[j] = i;
            }
            const float slLin = powf(10.0f, (sidelobeDb + 6.0f) / 10.0f);
            int kept[PEAK_MAX];
            int nk = 0;
            float keyPow[KEYS];
            for (int k = keyLo; k < keyHi; ++k) keyPow[k] = 0.0f;
            for (int oi = 0; oi < np; ++oi) {
                const int i = order[oi];
                bool side = false;
                for (int q = 0; q < nk && !side; ++q) {
                    const int j = kept[q];
                    const float d = fabsf(m_peakBin[i] - m_peakBin[j]) / (float)resBins;
                    if (d < 1.5f || d > 24.0f) continue;
                    float lim = m_peakPow[j] * slLin;
                    if (d > 2.5f) {
                        const float t = 2.5f / d;
                        const float t2 = t * t;
                        lim *= t2 * t2 * t2;
                    }
                    if (m_peakPow[i] <= lim) side = true;
                }
                if (side) continue;
                kept[nk++] = i;

                const double hz = (double)m_peakBin[i] * binHz;
                if (hz <= 1.0) continue;
                const double kf = 69.0 + 12.0 * log2(hz / 440.0) - (double)tuneSemis;
                const int k = (int)floor(kf + 0.5);
                if (k < keyLo || k >= keyHi) continue;
                if (fabs(kf - (double)k) > 0.42) continue;
                if (m_peakPow[i] > keyPow[k]) keyPow[k] = m_peakPow[i];
                if (tune && m_peakSnr[i] >= 100.0f && k >= 36 && k < 96) {
                    const double dev = kf - (double)k;
                    const double w = sqrt((double)m_peakPow[i]);
                    tune->c += w * cos(6.283185307179586 * dev);
                    tune->s += w * sin(6.283185307179586 * dev);
                    tune->w += w;
                }
            }
            for (int k = keyLo; k < keyHi; ++k)
                outMag[k] = (keyPow[k] > 0.0f) ? sqrtf(keyPow[k]) * magScale : 0.0f;

            // 3) 連続周波数の調波サリエンス。基音ピークが動いて割れていても、
            //    n*f0 の位置にある倍音ピークから低〜中音の基音を見つけられるようにする。
            if (outHarmSal) {
                for (int k = keyLo; k < keyHi; ++k) {
                    if (k < 24 || keyPow[k] <= 0.0f) continue;
                    const double f0 = (double)PianoKey::KeyHz(k) * tuneMul;
                    float sal = 0.0f;
                    for (int h = 1; h <= SAL_HARMONICS; ++h) {
                        const double fh = f0 * (double)h;
                        const double bc = fh / binHz;
                        if (bc >= (double)(half - 3)) break;
                        const float w = 1.0f / powf((float)h, 0.55f);
                        double rHalf = bc * 0.018;
                        if (rHalf < 1.1 * resBins) rHalf = 1.1 * resBins;
                        int i0 = (int)floor(bc - rHalf);
                        int i1 = (int)ceil(bc + rHalf);
                        if (i0 < 2) i0 = 2;
                        if (i1 > half - 2) i1 = half - 2;
                        float pk = 0.0f;
                        for (int b = i0; b <= i1; ++b) {
                            if (m_pow[b] > m_pow[b - 1] && m_pow[b] >= m_pow[b + 1])
                                if (m_pow[b] > pk) pk = m_pow[b];
                        }
                        const int hk = KeyOfHz(fh, tuneSemis);
                        const float nf = noisePow[hk];
                        if (nf <= 0.0f || pk <= nf * snrLin) continue;
                        float db = 10.0f * log10f(pk / nf) - 5.5f;
                        if (db > 40.0f) db = 40.0f;
                        sal += w * db;
                    }
                    outHarmSal[k] = sal;
                }
            }
        }

    private:
        static int KeyOfHz(double hz, float tuneSemis)
        {
            int k = 0;
            if (hz > 1.0)
                k = (int)floor(69.0 + 12.0 * log2(hz / 440.0) - (double)tuneSemis + 0.5);
            if (k < 0) k = 0;
            if (k >= KEYS) k = KEYS - 1;
            return k;
        }

        static float SelectKth(float* v, int n, int kth)
        {
            int lo = 0, hi = n - 1;
            while (lo < hi) {
                const float pivot = v[(lo + hi) >> 1];
                int i = lo, j = hi;
                while (i <= j) {
                    while (v[i] < pivot) ++i;
                    while (v[j] > pivot) --j;
                    if (i <= j) {
                        const float t = v[i]; v[i] = v[j]; v[j] = t;
                        ++i; --j;
                    }
                }
                if (kth <= j) hi = j;
                else if (kth >= i) lo = i;
                else break;
            }
            return v[kth];
        }

        void Prepare(int m)
        {
            if (m == m_m) return;
            m_m = m;
            for (int i = 0; i < m / 2; ++i) {
                const double a = 6.283185307179586 * (double)i / (double)m;
                m_cos[i] = cos(a);
                m_sin[i] = -sin(a);
            }
            int bits = 0;
            while ((1 << bits) < m) ++bits;
            for (int i = 0; i < m; ++i) {
                int r = 0;
                for (int b = 0; b < bits; ++b)
                    if (i & (1 << b)) r |= 1 << (bits - 1 - b);
                m_rev[i] = r;
            }
        }

        void Fft()
        {
            const int m = m_m;
            for (int i = 0; i < m; ++i) {
                const int j = m_rev[i];
                if (j > i) {
                    double t = m_re[i]; m_re[i] = m_re[j]; m_re[j] = t;
                    t = m_im[i]; m_im[i] = m_im[j]; m_im[j] = t;
                }
            }
            for (int len = 2; len <= m; len <<= 1) {
                const int hl = len >> 1;
                const int step = m / len;
                for (int i = 0; i < m; i += len) {
                    for (int k = 0; k < hl; ++k) {
                        const double wr = m_cos[k * step];
                        const double wi = m_sin[k * step];
                        const int a = i + k;
                        const int b = a + hl;
                        const double tr = m_re[b] * wr - m_im[b] * wi;
                        const double ti = m_re[b] * wi + m_im[b] * wr;
                        m_re[b] = m_re[a] - tr;
                        m_im[b] = m_im[a] - ti;
                        m_re[a] += tr;
                        m_im[a] += ti;
                    }
                }
            }
        }

        int m_m = 0;
        int m_rev[FFT_MAX];
        double m_re[FFT_MAX];
        double m_im[FFT_MAX];
        double m_cos[FFT_MAX / 2];
        double m_sin[FFT_MAX / 2];
        float m_pow[FFT_MAX / 2 + 1];
        float m_sel[FFT_MAX / 2 + 1];
        float m_peakBin[PEAK_MAX];
        float m_peakPow[PEAK_MAX];
        float m_peakSnr[PEAK_MAX];
    };
}
