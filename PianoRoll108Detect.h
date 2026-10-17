#pragma once
// 108鍵ピアノロールの基音推定
//
// 入力は PianoRollPeakSpectrum が作る
//   - 鍵帯域の信号パワー blend[k]
//   - 鍵帯域の雑音床パワー floorPow[k]
//   - FFT 連続周波数での調波サリエンス fftSal[k] (任意)
//
// ノートは FFT 正弦波ピークを Hz で調波系列に束ねて決める。鍵グリッドで
// W[n·key] を足すと、楽曲ではメロディがベースの整数倍に乗って部分音が全部ノートになる。
#include <cmath>
#include <cstring>
#include "PianoKeyTable.h"

namespace PianoRoll108
{
    static constexpr int COUNT = PianoKey::COUNT;

    static constexpr int WIN_LONG_END = 60;
    static constexpr int WIN_MID_END = 84;

    static constexpr int BASS_END = 48;   // C3
    static constexpr int MID_END = 72;    // C5
    static constexpr int C4_KEY = 60;
    static constexpr int O5_HI = 84;      // C6
    static constexpr int EDGE_HI = 96;    // C7
    static constexpr int MUSIC_LOW_FLOOR = 28; // E1。C1 付近は 2f0 の影が出る

    inline int KeyBandIndex(int keyIndex)
    {
        if (keyIndex < BASS_END) return 0;
        if (keyIndex < MID_END) return 1;
        return 2;
    }

    inline float IirAlphaForKey(int keyIndex)
    {
        if (keyIndex < BASS_END) return 0.30f;
        if (keyIndex < MID_END) return 0.42f;
        return 0.48f;
    }

    inline void BuildDetectionSpectrum(const float* smoothed, const float* raw, float* out, int count)
    {
        if (!smoothed || !raw || !out || count <= 0) return;
        for (int i = 0; i < count; ++i)
            out[i] = smoothed[i] * 0.45f + raw[i] * 0.55f;
    }

    inline bool OnsetSupportsPick(const float* onset, const float* prevOnset,
        int keyIndex, float levelScale, float onsetDeltaScale = 1.0f)
    {
        if (!onset || !prevOnset || keyIndex < 0 || keyIndex >= COUNT) return false;
        // キックのオンセットを全域の基準にすると、高音の 16 分が常に埋もれる。
        int lo = MUSIC_LOW_FLOOR, hi = COUNT;
        if (keyIndex < BASS_END) { lo = MUSIC_LOW_FLOOR; hi = BASS_END; }
        else if (keyIndex < MID_END) { lo = BASS_END; hi = MID_END; }
        else { lo = MID_END; hi = COUNT; }
        float oMax = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.004f) return false;
        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;
        float od = onsetDeltaScale;
        if (od < 0.25f) od = 0.25f;
        if (od > 4.0f) od = 4.0f;
        const float delta = onset[keyIndex] - prevOnset[keyIndex];
        return onset[keyIndex] >= oMax * 0.20f * scale &&
            delta >= oMax * 0.12f * od;
    }

    inline bool IsBroadbandTransient(const float* onset, const float* prevOnset, float strict)
    {
        if (!onset || !prevOnset) return false;
        static constexpr int LO = 36;
        float oMax = 0.0f;
        for (int i = LO; i < COUNT; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.004f) return false;
        const float live = oMax * 0.01f;
        int rise = 0, kMin = COUNT, kMax = -1;
        for (int i = LO; i < COUNT; ++i) {
            if (onset[i] < live) continue;
            if (onset[i] <= prevOnset[i] * 2.0f + live * 0.5f) continue;
            ++rise;
            if (i < kMin) kMin = i;
            if (i > kMax) kMax = i;
        }
        float frac = 0.35f / (strict > 0.1f ? strict : 0.1f);
        if (frac < 0.10f) frac = 0.10f;
        if (frac > 0.90f) frac = 0.90f;
        return rise >= (int)((float)(COUNT - LO) * frac) && (kMax - kMin) >= 36;
    }

    static constexpr int SAL_HARM = 8;
    static constexpr int SUB_HARM = 24;
    // 基音条件を満たす系列はすべて採用（本数上限なし）。雲は残差減算と整数倍除外で止める。
    static constexpr int MAX_NOTES = COUNT;
    static constexpr float W_OFFSET_DB = 6.5f;
    static constexpr float W_CAP_DB = 40.0f;
    static constexpr float SAL_MIN = 10.5f;
    static constexpr float SAL_REL = 0.24f;
    static constexpr float PROMOTE_REL = 0.55f;
    static constexpr float EXPLAINED_FRAC = 0.18f;
    static constexpr float SEMI_SPILL = 0.55f;
    static constexpr float ENV_TAU = 0.40f;
    static constexpr float MASK_OFFSET_DB = 22.0f;
    static constexpr float MASK_UP_DB_PER_BARK = 13.0f;
    static constexpr float MASK_DOWN_DB_PER_BARK = 27.0f;

    struct HarmTable
    {
        short k[COUNT][SUB_HARM];
        float bark[COUNT];
        float availFrac[COUNT];
        float w[SAL_HARM];
        HarmTable()
        {
            for (int n = 1; n <= SAL_HARM; ++n)
                w[n - 1] = powf((float)n, -0.55f);
            float wAll = 0.0f;
            for (int n = 1; n <= 4; ++n) wAll += w[n - 1];
            for (int i = 0; i < COUNT; ++i) {
                k[i][0] = (short)i;
                for (int n = 2; n <= SUB_HARM; ++n)
                    k[i][n - 1] = (short)PianoKey::HarmonicKeyOnBoard(i, n);
                const float f = PianoKey::KeyHz(i);
                const float r = f / 7500.0f;
                bark[i] = 13.0f * atanf(0.00076f * f) + 3.5f * atanf(r * r);
                float wa = 0.0f;
                for (int n = 1; n <= 4; ++n)
                    if (k[i][n - 1] >= 0) wa += w[n - 1];
                availFrac[i] = wa / wAll;
            }
        }
    };

    inline const HarmTable& Harm()
    {
        static const HarmTable t;
        return t;
    }

    inline float BandStrict(int key, float pickBassRel, float pickLowMidRel,
        float pickMelodyRel, float pickTreRel)
    {
        float rel = 1.0f;
        if (key < BASS_END) rel = pickBassRel / 0.28f * 0.82f;
        else if (key < C4_KEY) rel = pickLowMidRel / 0.20f * 0.80f;
        else if (key < MID_END) rel = pickMelodyRel / 0.10f * 0.85f;
        else if (key < EDGE_HI) rel = pickMelodyRel / 0.10f * 1.00f;
        else rel = pickTreRel / 0.22f * 1.25f;
        if (rel < 0.35f) rel = 0.35f;
        if (rel > 3.5f) rel = 3.5f;
        return rel;
    }

    inline float AmpFloorForKey(int key, float powerFloor)
    {
        float rel = 0.20f;
        if (key < BASS_END) rel = 0.32f;
        else if (key < C4_KEY) rel = 0.22f;
        else if (key < O5_HI) rel = 0.14f;
        else if (key < EDGE_HI) rel = 0.10f;
        else rel = 0.08f;
        float p = powerFloor * rel;
        if (p < 1.0e-8f) p = 1.0e-8f;
        return sqrtf(p);
    }

    struct PickParams
    {
        float levelScale = 1.0f;
        float absNoiseFloor = 0.00055f;
        float pickBassRel = 0.28f;
        float pickLowMidRel = 0.20f;
        float pickMelodyRel = 0.10f;
        float pickTreRel = 0.22f;
        float ghostStrict = 1.0f;
        float envMargin = 1.10f;
        bool  verifyExplained = true;
        float percussive = 0.0f;
        const float* fftHarmSal = nullptr;
        const float* peakHz = nullptr;
        const float* peakAmp = nullptr;
        int nPeaks = 0;
        float tuneSemis = 0.0f;
        // 直前フレームの「ピック結果」の f0(Hz)。VisGap で点灯中の鍵は入れない。
        const float* prevF0Hz = nullptr;
        float* outF0Hz = nullptr;
    };

    inline int KeyFromHzTune(float hz, float tuneSemis)
    {
        if (hz <= 1.0f) return -1;
        const float kf = 69.0f + 12.0f * log2f(hz / 440.0f) - tuneSemis;
        const int k = (int)floorf(kf + 0.5f);
        if (k < 0 || k >= COUNT) return -1;
        return k;
    }

    inline int FindPeakNearHz(const float* hz, const float* amp, int n,
        float target, float centsTol, const bool* dead)
    {
        if (!hz || !amp || target <= 1.0f || n <= 0) return -1;
        float best = centsTol;
        int idx = -1;
        for (int i = 0; i < n; ++i) {
            if (dead && dead[i]) continue;
            if (hz[i] <= 1.0f || amp[i] <= 1.0e-10f) continue;
            const float c = 1731.234049066756f * fabsf(logf(hz[i] / target));
            if (c < best) {
                best = c;
                idx = i;
            }
        }
        return idx;
    }

    // Hz 昇順インデックス。cents 窓内だけ見る。全ピーク×logf は残差ループで UI を止める。
    inline int FindPeakInHzWindow(const float* hz, const float* amp, int n,
        const int* ord, float target, float centsTol)
    {
        if (!hz || !amp || !ord || target <= 1.0f || n <= 0) return -1;
        const float u = expf(centsTol * (1.0f / 1731.234049066756f));
        const float lo = target / u;
        const float hi = target * u;
        int l = 0, r = n;
        while (l < r) {
            const int m = (l + r) >> 1;
            if (hz[ord[m]] < lo) l = m + 1;
            else r = m;
        }
        float best = centsTol;
        int idx = -1;
        for (int t = l; t < n; ++t) {
            const int i = ord[t];
            const float f = hz[i];
            if (f > hi) break;
            if (f <= 1.0f || amp[i] <= 1.0e-10f) continue;
            const float c = 1731.234049066756f * fabsf(logf(f / target));
            if (c < best) {
                best = c;
                idx = i;
            }
        }
        return idx;
    }

    struct PickScratch
    {
        static constexpr int PMAX = 80;
        static constexpr int CMAX = 96;
        float amp[COUNT];
        float nf[COUNT];
        float thr[COUNT];
        float phz[PMAX];
        float pa[PMAX];
        float pa0[PMAX];
        int   idx[256];
        int   hzOrd[PMAX];
        float candF0[CMAX];
        bool  rejected[COUNT];
        bool  candUsed[CMAX];
        bool  continued[COUNT];
        float contA1[COUNT];
        int   prevOrd[COUNT];
        int   prevBestPk[COUNT];
        bool  peakUsed[PMAX];
    };

    inline PickScratch& PickWork()
    {
        static PickScratch s;
        return s;
    }

    inline float HarmCentsTol(int n, bool subtract)
    {
        if (subtract) return (n == 1) ? 36.0f : ((n <= 4) ? 42.0f : 48.0f);
        return (n == 1) ? 18.0f : ((n <= 4) ? 24.0f : 30.0f);
    }

    inline int PickNotes(const float* blend, const float* floorPow, const bool* prevActive,
        bool* outPicked, float* outOwnPow, const PickParams& p)
    {
        if (!blend || !floorPow || !outPicked || !outOwnPow) return 0;
        memset(outPicked, 0, sizeof(bool) * COUNT);
        if (p.outF0Hz) memset(p.outF0Hz, 0, sizeof(float) * COUNT);
        const HarmTable& ht = Harm();

        float scale = p.levelScale;
        if (scale < 0.55f) scale = 0.55f;
        if (scale > 1.25f) scale = 1.25f;

        PickScratch& wk = PickWork();
        float* amp = wk.amp;
        float* nf = wk.nf;
        float* thr = wk.thr;
        for (int i = 0; i < COUNT; ++i) {
            amp[i] = (blend[i] > 0.0f) ? sqrtf(blend[i]) : 0.0f;
            float n0 = (floorPow[i] > 0.0f) ? sqrtf(floorPow[i]) : 0.0f;
            const float nAbs = AmpFloorForKey(i, p.absNoiseFloor) * 0.72f;
            const float nCap = nAbs * 2.40f;
            if (n0 < nAbs) n0 = nAbs;
            if (n0 > nCap) n0 = nCap;
            nf[i] = n0;
            float t = SAL_MIN * BandStrict(i, p.pickBassRel, p.pickLowMidRel,
                p.pickMelodyRel, p.pickTreRel) / scale * (0.45f + 0.55f * ht.availFrac[i]);
            if (prevActive && prevActive[i]) t *= 0.68f;
            thr[i] = t;
        }

        static constexpr int PMAX = PickScratch::PMAX;
        static constexpr int CMAX = PickScratch::CMAX;
        static constexpr int HARM_N = 10;

        float* phz = wk.phz;
        float* pa = wk.pa;
        int* idx = wk.idx;
        int* hzOrd = wk.hzOrd;
        int np = 0;
        if (p.peakHz && p.peakAmp && p.nPeaks > 0) {
            int srcN = p.nPeaks;
            if (srcN > 256) srcN = 256;
            for (int i = 0; i < srcN; ++i) idx[i] = i;
            for (int i = 1; i < srcN; ++i) {
                const int v = idx[i];
                const float av = p.peakAmp[v];
                int j = i;
                while (j > 0 && p.peakAmp[idx[j - 1]] < av) {
                    idx[j] = idx[j - 1];
                    --j;
                }
                idx[j] = v;
            }
            for (int i = 0; i < srcN && np < PMAX; ++i) {
                const int s = idx[i];
                if (p.peakHz[s] <= 1.0f || p.peakAmp[s] <= 0.0f) continue;
                phz[np] = p.peakHz[s];
                pa[np] = sqrtf(p.peakAmp[s]);
                ++np;
            }
        }
        if (np <= 0) {
            for (int k = MUSIC_LOW_FLOOR; k < COUNT && np < PMAX; ++k) {
                if (amp[k] <= nf[k] * 1.5f) continue;
                if (k > 0 && amp[k - 1] > amp[k]) continue;
                if (k + 1 < COUNT && amp[k + 1] >= amp[k]) continue;
                phz[np] = PianoKey::KeyHz(k);
                pa[np] = amp[k];
                ++np;
            }
        }
        for (int i = 0; i < np; ++i) hzOrd[i] = i;
        for (int i = 1; i < np; ++i) {
            const int v = hzOrd[i];
            const float fv = phz[v];
            int j = i;
            while (j > 0 && phz[hzOrd[j - 1]] > fv) {
                hzOrd[j] = hzOrd[j - 1];
                --j;
            }
            hzOrd[j] = v;
        }
        float* pa0 = wk.pa0;
        for (int i = 0; i < np; ++i) pa0[i] = pa[i];

        float* candF0 = wk.candF0;
        int nCand = 0;
        auto addCand = [&](float f0) {
            if (f0 < PianoKey::KeyHz(MUSIC_LOW_FLOOR) * 0.97f) return;
            if (f0 > PianoKey::KeyHz(O5_HI) * 1.03f) return;
            for (int i = 0; i < nCand; ++i) {
                const float c = 1731.234049066756f * fabsf(logf(candF0[i] / f0));
                if (c < 25.0f) return;
            }
            if (nCand < CMAX) candF0[nCand++] = f0;
        };

        bool* rejected = wk.rejected;
        memset(rejected, 0, sizeof(wk.rejected));
        int nPicked = 0;
        int nBand[3] = { 0, 0, 0 };
        float firstSal = 0.0f;
        float firstSalBand[3] = { 0.0f, 0.0f, 0.0f };
        bool* candUsed = wk.candUsed;
        memset(candUsed, 0, sizeof(wk.candUsed));

        auto findNear = [&](float tgt, float centsTol) -> int {
            return FindPeakInHzWindow(phz, pa, np, hzOrd, tgt, centsTol);
        };

        // 歌声がギターの整数倍に乗ると奇数倍音も下の系列と一致する。
        // 傾斜で説明できない余った f0 ピークは独立音。
        auto extraSource = [&](float fHi, float fLo, int nMul) -> bool {
            if (fHi <= 1.0f || fLo <= 1.0f || nMul < 2 || nMul > 4) return false;
            const int jH = FindPeakInHzWindow(phz, pa0, np, hzOrd, fHi, HarmCentsTol(1, false));
            const int jL = FindPeakInHzWindow(phz, pa0, np, hzOrd, fLo, HarmCentsTol(1, true));
            if (jH < 0 || jL < 0) return false;
            const float pred = pa0[jL] * powf((float)nMul, -0.65f);
            if (pred <= 1.0e-12f) return pa0[jH] > 0.0f;
            return pa0[jH] > pred * 1.45f;
        };

        auto extraResidual = [&](float fHi, float fLo, int nMul) -> bool {
            if (fHi <= 1.0f || fLo <= 1.0f || nMul < 2 || nMul > 4) return false;
            const int jH = findNear(fHi, HarmCentsTol(1, false));
            const int jL = FindPeakInHzWindow(phz, pa0, np, hzOrd, fLo, HarmCentsTol(1, true));
            if (jH < 0) return false;
            if (jL < 0) return pa[jH] > 0.0f;
            const float pred = pa0[jL] * powf((float)nMul, -0.65f);
            if (pred <= 1.0e-12f) return pa[jH] > 0.0f;
            return pa[jH] > pred * 1.45f;
        };

        auto scoreF0 = [&](float f0, float& a1, int& nHit, int& nLow, int& nOdd) -> float {
            a1 = 0.0f;
            nHit = 0;
            nLow = 0;
            nOdd = 0;
            float s = 0.0f;
            for (int n = 1; n <= HARM_N; ++n) {
                const float tgt = f0 * (float)n;
                if (tgt > PianoKey::KeyHz(COUNT - 1) * 1.06f) break;
                const int j = findNear(tgt, HarmCentsTol(n, false));
                if (j < 0) continue;
                const int hk = KeyFromHzTune(phz[j], p.tuneSemis);
                const float n0 = (hk >= 0) ? nf[hk] : nf[MUSIC_LOW_FLOOR];
                float db = 0.0f;
                if (pa[j] > 0.0f && n0 > 0.0f)
                    db = 20.0f * log10f(pa[j] / n0) - W_OFFSET_DB;
                if (db < 0.0f) db = 0.0f;
                if (db > W_CAP_DB) db = W_CAP_DB;
                s += powf((float)n, -0.55f) * db;
                ++nHit;
                if (n <= 4) ++nLow;
                if (n == 3 || n == 5 || n == 7) ++nOdd;
                if (n == 1) a1 = pa[j];
            }
            return s;
        };

        auto subtractSeries = [&](float f0, float aFund) {
            float a1 = aFund;
            // 観測 f0 があるのに高い山から a1 を上げると、2f0 に乗った歌声を
            // 「強すぎる基音」とみなして丸ごと引いてしまう。
            if (a1 <= 1.0e-10f) {
                for (int n = 2; n <= 4; ++n) {
                    const int j = findNear(f0 * (float)n, HarmCentsTol(n, true));
                    if (j < 0) continue;
                    if (extraSource(f0 * (float)n, f0, n)) continue;
                    const float est = pa[j] * powf((float)n, 0.65f);
                    if (est > a1) a1 = est;
                }
            }
            if (a1 <= 1.0e-10f) return;
            for (int n = 1; n <= 12; ++n) {
                const int j = findNear(f0 * (float)n, HarmCentsTol(n, true));
                if (j < 0) continue;
                float pred = a1 * powf((float)n, -0.65f);
                // 3 次以上はコードトーンとボーカルが重なる。引き切ると声の f0 が消える。
                if (n >= 3) pred *= 0.55f;
                if (n >= 2 && extraSource(f0 * (float)n, f0, n))
                    pred *= 0.45f;
                if (pred > pa[j]) pa[j] = 0.0f;
                else pa[j] -= pred;
            }
        };

        auto hasOwnOdd = [&](float fHi, float fLo) -> bool {
            if (fHi <= 1.0f || fLo <= 1.0f) return false;
            for (int h = 3; h <= 7; h += 2) {
                const int j = findNear(fHi * (float)h, HarmCentsTol(h, false));
                if (j < 0) continue;
                const float ratio = phz[j] / fLo;
                bool ofLo = false;
                for (int n = 2; n <= 16; ++n) {
                    if (fabsf(ratio - (float)n) <= 0.03f * (float)n) { ofLo = true; break; }
                }
                if (!ofLo) return true;
            }
            return false;
        };

        auto seriesOk = [&](int key, float a1, int nHit, int nLow, int nOdd, bool fromPrev) -> bool {
            if (fromPrev) {
                if (nHit < 2) return false;
                if (a1 <= 0.0f && nLow < 2) return false;
                if (a1 <= 0.0f && nOdd < 1 && key < C4_KEY) return false;
                return true;
            }
            // 偶数倍音だけ当たるのは 1 オクターブ上の音の影。観測 a1 があっても同じ。
            if (nOdd < 1 && key < O5_HI) return false;
            // 低音は観測ピークが無いとキック／2f0 の影。欠け基音は繰り下げ側で起こす。
            if (key < BASS_END && a1 <= 0.0f) return false;
            if (key < C4_KEY) {
                if (nHit < 3 && !(nHit >= 2 && a1 > 0.0f && nLow >= 2)) return false;
            }
            else if (key < MID_END) {
                if (nHit < 2) return false;
                if (a1 <= 0.0f && nLow < 2) return false;
            }
            else if (key < O5_HI) {
                if (nHit < 3 && !(nHit >= 2 && a1 > 0.0f && nLow >= 2)) return false;
            }
            else {
                if (nHit < 2) return false;
            }
            return true;
        };

        auto commitKey = [&](float f0, int bestKey, float bestSal, float bestA1, int bestHit, bool fromPrev) -> bool {
            bool ok = true;
            const float a0 = amp[bestKey];
            if (!fromPrev) {
                if (bestKey >= EDGE_HI && !(prevActive && prevActive[bestKey])) {
                    if (bestHit < 2 || nPicked >= 2) ok = false;
                }
                else if (bestKey >= O5_HI && !(prevActive && prevActive[bestKey])) {
                    int nNear = 0;
                    for (int d = 1; d <= 5; ++d) {
                        if (bestKey - d >= 0 && amp[bestKey - d] >= a0 * 0.63f) ++nNear;
                        if (bestKey + d < COUNT && amp[bestKey + d] >= a0 * 0.63f) ++nNear;
                    }
                    if (nNear >= 4) ok = false;
                }
            }
            if (ok) {
                for (int d = -1; d <= 1; d += 2) {
                    const int a = bestKey + d;
                    if (a >= 0 && a < COUNT && outPicked[a] && a0 < amp[a] * 0.90f) ok = false;
                }
            }
            // 継続中の音は「下の倍音だから」では落とさない。コードトーンが
            // ベースの 3f0 に乗ると毎フレーム veto で細切れになる。
            if (ok && !fromPrev) {
                for (int j = MUSIC_LOW_FLOOR; j < bestKey && ok; ++j) {
                    if (!outPicked[j]) continue;
                    float fj = PianoKey::KeyHz(j);
                    if (p.outF0Hz && p.outF0Hz[j] > 1.0f) fj = p.outF0Hz[j];
                    if (fj <= 1.0f) continue;
                    const float ratio = f0 / fj;
                    for (int n = 2; n <= 10; ++n) {
                        if (fabsf(ratio - (float)n) > 0.03f * (float)n) continue;
                        if (!hasOwnOdd(f0, fj) && !extraResidual(f0, fj, n)) ok = false;
                        break;
                    }
                }
            }
            if (!ok) return false;
            outPicked[bestKey] = true;
            if (p.outF0Hz) p.outF0Hz[bestKey] = f0;
            if (nPicked == 0) firstSal = bestSal;
            {
                const int pb = KeyBandIndex(bestKey);
                if (firstSalBand[pb] <= 0.0f) firstSalBand[pb] = bestSal;
                ++nBand[pb];
            }
            ++nPicked;
            if (fromPrev) {
                wk.continued[bestKey] = true;
                wk.contA1[bestKey] = bestA1;
            }
            else {
                subtractSeries(f0, bestA1);
                for (int hi = bestKey + 1; hi < COUNT; ++hi) {
                    if (!outPicked[hi] || wk.continued[hi]) continue;
                    float fHi = PianoKey::KeyHz(hi);
                    if (p.outF0Hz && p.outF0Hz[hi] > 1.0f) fHi = p.outF0Hz[hi];
                    const float ratio = fHi / f0;
                    int nMul = 0;
                    for (int n = 2; n <= 10; ++n) {
                        if (fabsf(ratio - (float)n) <= 0.03f * (float)n) { nMul = n; break; }
                    }
                    if (nMul == 0) continue;
                    if (hasOwnOdd(fHi, f0) || extraSource(fHi, f0, nMul)) continue;
                    outPicked[hi] = false;
                    outOwnPow[hi] = 0.0f;
                    if (p.outF0Hz) p.outF0Hz[hi] = 0.0f;
                    --nPicked;
                    {
                        const int hb = KeyBandIndex(hi);
                        if (nBand[hb] > 0) --nBand[hb];
                    }
                }
            }
            return true;
        };

        // 前フレームの f0 を残差減算の前に全部載せる。低音から引くと伴奏の
        // 整数倍スロットが毎フレーム落ち、ピークは残っているのに細切れになる。
        memset(wk.continued, 0, sizeof(wk.continued));
        memset(wk.contA1, 0, sizeof(wk.contA1));
        memset(wk.peakUsed, 0, sizeof(wk.peakUsed));
        if (prevActive) {
            int nPrev = 0;
            for (int key = MUSIC_LOW_FLOOR; key < COUNT; ++key) {
                if (!prevActive[key] || rejected[key]) continue;
                float f0 = PianoKey::KeyHz(key);
                if (p.prevF0Hz && p.prevF0Hz[key] > 1.0f)
                    f0 = p.prevF0Hz[key];
                wk.prevBestPk[key] = FindPeakInHzWindow(phz, pa0, np, hzOrd, f0, 80.0f);
                wk.prevOrd[nPrev++] = key;
            }
            for (int i = 1; i < nPrev; ++i) {
                const int v = wk.prevOrd[i];
                float f0v = PianoKey::KeyHz(v);
                if (p.prevF0Hz && p.prevF0Hz[v] > 1.0f) f0v = p.prevF0Hz[v];
                float cv = 999.0f;
                if (wk.prevBestPk[v] >= 0 && f0v > 1.0f)
                    cv = 1731.234049066756f * fabsf(logf(phz[wk.prevBestPk[v]] / f0v));
                int j = i;
                while (j > 0) {
                    const int u = wk.prevOrd[j - 1];
                    float f0u = PianoKey::KeyHz(u);
                    if (p.prevF0Hz && p.prevF0Hz[u] > 1.0f) f0u = p.prevF0Hz[u];
                    float cu = 999.0f;
                    if (wk.prevBestPk[u] >= 0 && f0u > 1.0f)
                        cu = 1731.234049066756f * fabsf(logf(phz[wk.prevBestPk[u]] / f0u));
                    if (cu <= cv) break;
                    wk.prevOrd[j] = u;
                    --j;
                }
                wk.prevOrd[j] = v;
            }
            for (int pi = 0; pi < nPrev && nPicked < MAX_NOTES; ++pi) {
                const int key = wk.prevOrd[pi];
                float f0 = PianoKey::KeyHz(key);
                if (p.prevF0Hz && p.prevF0Hz[key] > 1.0f)
                    f0 = p.prevF0Hz[key];
                const int jNear = wk.prevBestPk[key];
                if (jNear >= 0 && jNear < PMAX && !wk.peakUsed[jNear]) {
                    const float fAlt = phz[jNear];
                    float aKeep = 0.0f, aAlt = 0.0f;
                    int h0 = 0, l0 = 0, o0 = 0, h1 = 0, l1 = 0, o1 = 0;
                    const float sKeep = scoreF0(f0, aKeep, h0, l0, o0);
                    const float sAlt = scoreF0(fAlt, aAlt, h1, l1, o1);
                    const int kAlt = KeyFromHzTune(fAlt, p.tuneSemis);
                    if (sAlt >= sKeep * 0.90f && kAlt >= key - 1 && kAlt <= key + 1) {
                        f0 = fAlt;
                        wk.peakUsed[jNear] = true;
                    }
                }
                const int keyNow = KeyFromHzTune(f0, p.tuneSemis);
                if (keyNow < MUSIC_LOW_FLOOR || keyNow >= COUNT) continue;
                if (outPicked[keyNow] || rejected[keyNow]) continue;
                float a1 = 0.0f;
                int nHit = 0, nLow = 0, nOdd = 0;
                const float s = scoreF0(f0, a1, nHit, nLow, nOdd);
                if (s < thr[keyNow] * 0.72f) continue;
                if (!seriesOk(keyNow, a1, nHit, nLow, nOdd, true)) continue;
                if (!commitKey(f0, keyNow, s, a1, nHit, true))
                    rejected[keyNow] = true;
            }
            for (int key = 0; key < COUNT; ++key) {
                if (!wk.continued[key] || !p.outF0Hz) continue;
                subtractSeries(p.outF0Hz[key], wk.contA1[key]);
            }
        }

        // 継続で引いたあとの残差ピークだけを新規候補にする。元ピーク一覧から
        // 拾うと、既に説明した倍音が毎フレーム「新しい音」として流れ落ちる。
        nCand = 0;
        memset(candUsed, 0, sizeof(wk.candUsed));
        for (int i = 0; i < np; ++i) {
            const int k = KeyFromHzTune(phz[i], p.tuneSemis);
            if (k < MUSIC_LOW_FLOOR || k >= O5_HI) continue;
            if (pa[i] <= 1.0e-10f) continue;
            if (outPicked[k]) continue;
            if (k < BASS_END) {
                const int j2 = findNear(phz[i] * 2.0f, HarmCentsTol(2, false));
                if (j2 >= 0 && pa[j2] > pa[i] * 1.15f &&
                    !extraResidual(phz[i] * 2.0f, phz[i], 2))
                    continue;
            }
            bool harmOfLower = false;
            for (int j = 0; j < np && !harmOfLower; ++j) {
                if (j == i || phz[j] <= 1.0f || pa[j] <= 1.0e-10f) continue;
                if (phz[j] >= phz[i] * 0.97f) continue;
                const float ratio = phz[i] / phz[j];
                for (int n = 2; n <= 10; ++n) {
                    if (fabsf(ratio - (float)n) > 0.03f * (float)n) continue;
                    if (!hasOwnOdd(phz[i], phz[j]) && !extraResidual(phz[i], phz[j], n))
                        harmOfLower = true;
                    break;
                }
            }
            if (!harmOfLower)
                addCand(phz[i]);
        }

        for (int iter = 0; iter < CMAX && nPicked < MAX_NOTES; ++iter) {
            int bestC = -1;
            float bestSal = 0.0f, bestA1 = 0.0f;
            int bestHit = 0;
            for (int c = 0; c < nCand; ++c) {
                if (candUsed[c]) continue;
                const int key = KeyFromHzTune(candF0[c], p.tuneSemis);
                if (key < MUSIC_LOW_FLOOR || key >= COUNT) {
                    candUsed[c] = true;
                    continue;
                }
                if (outPicked[key] || rejected[key]) {
                    candUsed[c] = true;
                    continue;
                }
                float a1 = 0.0f;
                int nHit = 0, nLow = 0, nOdd = 0;
                const float s = scoreF0(candF0[c], a1, nHit, nLow, nOdd);
                // 残差は減るだけ。nHit/スコア不足は後ラウンドで回復しない。
                if (!seriesOk(key, a1, nHit, nLow, nOdd, false) || s < thr[key]) {
                    candUsed[c] = true;
                    continue;
                }
                if (prevActive) {
                    bool hop = false;
                    for (int d = -1; d <= 1 && !hop; ++d) {
                        if (d == 0) continue;
                        const int n = key + d;
                        if (n < MUSIC_LOW_FLOOR || n >= COUNT) continue;
                        if (prevActive[n] && !outPicked[n] && s < firstSal * 0.80f)
                            hop = true;
                    }
                    if (hop) continue;
                }
                if (s > bestSal) {
                    bestSal = s;
                    bestC = c;
                    bestA1 = a1;
                    bestHit = nHit;
                }
            }
            if (bestC < 0) break;

            float f0 = candF0[bestC];
            int bestKey = KeyFromHzTune(f0, p.tuneSemis);
            if (bestKey < MUSIC_LOW_FLOOR || bestKey >= COUNT) {
                candUsed[bestC] = true;
                continue;
            }
            // 観測 f0 があるピークはそれ自体が音。下へ繰り下げると歌声が中低音に吸われる。
            if (bestKey >= BASS_END && bestA1 <= 1.0e-10f) {
                const int mMax = (bestKey >= MID_END) ? 8 : ((bestKey >= C4_KEY) ? 4 : 2);
                for (int m = mMax; m >= 2; --m) {
                    const float loF = f0 / (float)m;
                    const int lo = KeyFromHzTune(loF, p.tuneSemis);
                    if (lo < MUSIC_LOW_FLOOR || lo >= bestKey) continue;
                    if (outPicked[lo] || rejected[lo]) continue;
                    float a1b = 0.0f;
                    int nh = 0, nl = 0, nOddLo = 0;
                    const float sLo = scoreF0(loF, a1b, nh, nl, nOddLo);
                    if (sLo < thr[lo] || sLo < bestSal * 0.50f) continue;
                    if (!seriesOk(lo, a1b, nh, nl, nOddLo, false)) continue;
                    f0 = loF;
                    bestKey = lo;
                    bestSal = sLo;
                    bestA1 = a1b;
                    bestHit = nh;
                    break;
                }
            }

            candUsed[bestC] = true;
            if (!commitKey(f0, bestKey, bestSal, bestA1, bestHit, false))
                rejected[bestKey] = true;
        }

        for (int i = 0; i < COUNT; ++i)
            outOwnPow[i] = outPicked[i] ? blend[i] : 0.0f;
        auto dropKey = [&](int drop) {
            if (drop < 0 || drop >= COUNT || !outPicked[drop]) return;
            outPicked[drop] = false;
            outOwnPow[drop] = 0.0f;
            if (p.outF0Hz) p.outF0Hz[drop] = 0.0f;
            --nPicked;
        };
        for (int i = 0; i + 1 < COUNT; ++i) {
            if (!outPicked[i] || !outPicked[i + 1]) continue;
            int drop = (amp[i] >= amp[i + 1]) ? (i + 1) : i;
            if (wk.continued[drop] && !wk.continued[drop == i ? i + 1 : i])
                drop = (drop == i) ? (i + 1) : i;
            dropKey(drop);
        }
        // オクターブ重ね: 傾斜どおりの 2f0 だけ落とす。鍵グリッド振幅で高い方を消すと歌声が死ぬ。
        for (int lo = MUSIC_LOW_FLOOR; lo < COUNT; ++lo) {
            if (!outPicked[lo]) continue;
            const int hi = lo + 12;
            if (hi >= COUNT || !outPicked[hi]) continue;
            float fLo = PianoKey::KeyHz(lo);
            float fHi = PianoKey::KeyHz(hi);
            if (p.outF0Hz && p.outF0Hz[lo] > 1.0f) fLo = p.outF0Hz[lo];
            if (p.outF0Hz && p.outF0Hz[hi] > 1.0f) fHi = p.outF0Hz[hi];
            if (extraSource(fHi, fLo, 2)) continue;
            // 両方とも継続中ならオクターブ重ねを落とさない（細切れの主因）。
            if (wk.continued[lo] && wk.continued[hi]) continue;
            const int jL = FindPeakInHzWindow(phz, pa0, np, hzOrd, fLo, HarmCentsTol(1, true));
            const int jH = FindPeakInHzWindow(phz, pa0, np, hzOrd, fHi, HarmCentsTol(1, false));
            int drop = hi;
            if (jL < 0 && jH >= 0) drop = lo;
            else if (jH >= 0 && jL >= 0 && pa0[jL] < pa0[jH] * 0.50f) drop = lo;
            if (wk.continued[drop] && !wk.continued[drop == hi ? lo : hi])
                drop = (drop == hi) ? lo : hi;
            dropKey(drop);
        }
        if (p.outF0Hz) {
            for (int i = 0; i < COUNT; ++i) {
                if (outPicked[i] && p.outF0Hz[i] <= 1.0f)
                    p.outF0Hz[i] = PianoKey::KeyHz(i);
            }
        }
        return nPicked;

    }
}
