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
        const float* burstHz = nullptr;
        const float* burstAmp = nullptr;
        int nBurst = 0;
        float tuneSemis = 0.0f;
        // 直前フレームの「ピック結果」の f0(Hz)。VisGap で点灯中の鍵は入れない。
        const float* prevF0Hz = nullptr;
        float* outF0Hz = nullptr;
        // 短窓の立ち上がり。新規ノートはここが立った鍵だけ。倍音の雨は立ち上がらない。
        const float* onset = nullptr;
        const float* prevOnset = nullptr;
        // C5 以上で「歌」として立てた鍵。歪みギターの高次倍音はここを立てない。
        bool* ownVoice = nullptr;
        // 歌声は一人。直前のメロディ f0 と、途切れたフレーム数。
        float* melodyHz = nullptr;
        int* melodyMiss = nullptr;
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
        // しきい値はフレーム内最大との比。AGC後の絶対値にすると、曲ごとに無反応か洪水になる。
        float frameMax = 1.0e-8f;
        for (int i = 0; i < np; ++i)
            if (pa0[i] > frameMax) frameMax = pa0[i];

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

        auto centsOf = [&](float a, float b) -> float {
            if (a <= 1.0f || b <= 1.0f) return 9999.0f;
            return 1731.234049066756f * fabsf(logf(a / b));
        };
        // 下のピークの整数倍（40cent）で、予測より 3 倍も大きくない山は部分音。
        // 「独自の奇数倍音がある」は密集スペクトルでは常に真になり、倍音が別ノートになる。
        // 歌声は f0 自体が傾斜を大きく超えるので残る。本数上限は使わない。
        auto suppressedPartial = [&](float fHi, float maxCents, float ratioKeep) -> bool {
            if (fHi <= 1.0f) return false;
            const int jH = FindPeakInHzWindow(phz, pa0, np, hzOrd, fHi, maxCents);
            if (jH < 0) return false;
            for (int j = 0; j < np; ++j) {
                if (phz[j] <= 1.0f || pa0[j] <= 1.0e-10f) continue;
                if (phz[j] >= fHi * 0.97f) continue;
                // 5倍以上は密集した低音といくらでも一致し、歌声の f0 を部分音にして消す。
                for (int n = 2; n <= 4; ++n) {
                    if (centsOf(fHi, phz[j] * (float)n) > maxCents) continue;
                    const float pred = pa0[j] * powf((float)n, -0.65f);
                    if (pred > 1.0e-12f && pa0[jH] >= pred * ratioKeep) break;
                    return true;
                }
            }
            return false;
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
                if (n >= 2) {
                    const int jFund = FindPeakInHzWindow(phz, pa0, np, hzOrd, f0, 40.0f);
                    // 倍音の方が基音より大きい山は、伴奏に乗った別の音（歌など）。引かない。
                    if (jFund >= 0 && pa0[j] > pa0[jFund] * 0.80f) continue;
                }
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
            // 低音・声は、基音ピーク自体が大きければ倍音が少なくてもその音。
            // 奇数倍音の有無を先に見ると、倍音の薄い歌がここで消える。
            if (key < BASS_END && a1 > frameMax * 0.05f) return true;
            if (key >= C4_KEY && key < O5_HI && a1 > frameMax * 0.10f && nHit >= 1) return true;
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

        auto attackAt = [&](int key) -> bool {
            if (!p.onset || !p.prevOnset || key < 0 || key >= COUNT) return false;
            const float o0 = p.onset[key];
            const float d0 = o0 - p.prevOnset[key];
            // 隣鍵の方が強い立ち上がりなら、同じ山の裾なので新規ノートにしない。
            if (key > 0 && p.onset[key - 1] > o0) return false;
            if (key + 1 < COUNT && p.onset[key + 1] > o0) return false;
            float rise = d0, level = o0;
            for (int d = -1; d <= 1; d += 2) {
                const int k = key + d;
                if (k < 0 || k >= COUNT) continue;
                const float dlt = p.onset[k] - p.prevOnset[k];
                if (dlt > rise) rise = dlt;
            }
            // 絶対値は窓の種類で桁が変わる。立ち上がりは「今のレベルに対する増え」で見る。
            // 0.35 だと 8192 窓に埋もれた歪みギターの刻みが立ち上がりにならない。
            return level > 0.0004f && rise > 0.0002f && rise > level * 0.18f;
        };

        bool prevVoice[COUNT];
        bool bornVoice[COUNT];
        memset(prevVoice, 0, sizeof(prevVoice));
        memset(bornVoice, 0, sizeof(bornVoice));
        if (p.ownVoice) memcpy(prevVoice, p.ownVoice, sizeof(prevVoice));

        // C5 以上が、今鳴っている低い音の 2..12 倍なら歪みギターの倍音。
        // 大きいだけでは歌にしない。ギターと同時に立ち上がる高音も倍音。
        // 低い音が伸びているあいだに高音だけ立ち上がったら歌。
        auto guitarPartialParent = [&](float fHi, int keyHi) -> int {
            if (fHi <= 1.0f || keyHi < 72) return -1;
            const int jH = FindPeakInHzWindow(phz, pa0, np, hzOrd, fHi, 40.0f);
            const float aHi = (jH >= 0) ? pa0[jH] : 0.0f;
            int best = -1;
            float bestA = 0.0f;
            for (int j = MUSIC_LOW_FLOOR; j < keyHi && j < COUNT; ++j) {
                if (!outPicked[j] && !(prevActive && prevActive[j])) continue;
                float fj = PianoKey::KeyHz(j);
                if (p.outF0Hz && p.outF0Hz[j] > 1.0f) fj = p.outF0Hz[j];
                else if (p.prevF0Hz && prevActive && prevActive[j] && p.prevF0Hz[j] > 1.0f)
                    fj = p.prevF0Hz[j];
                const int jP = FindPeakInHzWindow(phz, pa0, np, hzOrd, fj, 40.0f);
                const float aP = (jP >= 0) ? pa0[jP] : 0.0f;
                // 高音より小さい下の音は、その高音の親ではない。偶然の高次倍も除外。
                if (aP < aHi || aP < bestA) continue;
                for (int n = 2; n <= 6; ++n) {
                    if (centsOf(fHi, fj * (float)n) <= 40.0f) {
                        best = j;
                        bestA = aP;
                        break;
                    }
                }
            }
            return best;
        };
        auto keepHigh = [&](float fHi, int keyHi) -> bool {
            if (keyHi < 72) return true;
            if (prevVoice[keyHi] || bornVoice[keyHi]) return true;
            if (keyHi > 0 && prevVoice[keyHi - 1]) return true;
            if (keyHi + 1 < COUNT && prevVoice[keyHi + 1]) return true;
            const int jH = FindPeakInHzWindow(phz, pa0, np, hzOrd, fHi, 40.0f);
            const float aHi = (jH >= 0) ? pa0[jH] : 0.0f;
            const int parent = guitarPartialParent(fHi, keyHi);
            if (parent < 0) return aHi >= frameMax * 0.12f;
            // ギターと同じ瞬間に出る高音は倍音。コードが前から鳴っていて、
            // このフレームで初めて入る大きい高音だけを歌にする。毎フレームの再採用はしない。
            const bool parentHeld = prevActive && prevActive[parent];
            const bool highWas = prevActive && prevActive[keyHi];
            if (!parentHeld || highWas) return false;
            return aHi >= frameMax * 0.12f;
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
            // 新規は、その鍵（±1）に立ち上がりがあるときだけ。継続中の音は切らない。
            // 立ち上がりのない倍音ピークが毎フレーム別ノートになるのが画面の雨。
            if (ok && !fromPrev && !attackAt(bestKey)) {
                const bool strongBass = (bestKey < BASS_END && bestA1 > frameMax * 0.05f);
                const bool strongVoice = (bestKey >= C4_KEY && bestKey < 72 && bestA1 > frameMax * 0.10f);
                if (!strongBass && !strongVoice) ok = false;
            }
            if (ok && bestKey >= 72 && !keepHigh(f0, bestKey)) ok = false;
            if (!ok) return false;
            outPicked[bestKey] = true;
            if (bestKey >= 72 && keepHigh(f0, bestKey)) bornVoice[bestKey] = true;
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
            }
            for (int hi = bestKey + 1; hi < COUNT; ++hi) {
                if (!outPicked[hi]) continue;
                if (wk.continued[hi] && hi >= MID_END) continue;
                float fHi = PianoKey::KeyHz(hi);
                if (p.outF0Hz && p.outF0Hz[hi] > 1.0f) fHi = p.outF0Hz[hi];
                if (wk.continued[hi]) continue;
                if (attackAt(hi)) continue;
                if (!suppressedPartial(fHi, 40.0f, 3.0f)) continue;
                outPicked[hi] = false;
                wk.continued[hi] = false;
                outOwnPow[hi] = 0.0f;
                if (p.outF0Hz) p.outF0Hz[hi] = 0.0f;
                --nPicked;
                {
                    const int hb = KeyBandIndex(hi);
                    if (nBand[hb] > 0) --nBand[hb];
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
                const int jKeep = FindPeakInHzWindow(phz, pa0, np, hzOrd, f0, 45.0f);
                // 歪みギターは倍音の方が基音より大きい。全体最大との比で切るとルートが消える。
                // つなぐ条件は「同じ高さのピークがまだある」ことだけ。
                const bool peakHeld = (jKeep >= 0 && pa0[jKeep] > 0.004f);
                if (!peakHeld && s < thr[keyNow] * 0.72f) continue;
                if (!peakHeld && !seriesOk(keyNow, a1, nHit, nLow, nOdd, true)) continue;
                if (peakHeld)
                    f0 = phz[jKeep];
                const int keyHeld = KeyFromHzTune(f0, p.tuneSemis);
                if (keyHeld < MUSIC_LOW_FLOOR || keyHeld >= COUNT) continue;
                if (keyHeld >= 72 && !keepHigh(f0, keyHeld) && !prevVoice[key] && !prevVoice[keyHeld])
                    continue;
                if (!commitKey(f0, keyHeld, s, a1, nHit, true))
                    rejected[keyHeld] = true;
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
            if (suppressedPartial(phz[i], 40.0f, 3.0f)) continue;
            if (k >= 72 && !keepHigh(phz[i], k)) continue;
            // 持続ベースは短い窓では立ち上がらない。大きい独立ピークはそのまま音。
            const bool strongBass = (k < BASS_END && pa[i] > frameMax * 0.05f);
            const bool strongVoice = (k >= C4_KEY && k < 72 && pa[i] > frameMax * 0.10f);
            if (!attackAt(k) && !strongBass && !strongVoice) continue;
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
        // ここまでの系列スコアは捨てる。大きい山を「弱い低音の倍音」とみなして
        // 2 分の 1・3 分の 1 へ落とすと、実音が消えて低音が雲になり、残った高音は
        // 全体エネルギー比で切られて出ない。
        // ノートは目立つピークそのもの。より大きい低い音の減衰倍音だけを落とす。
        float seriesMax = 0.0f;
        for (int i = 0; i < np; ++i)
            if (pa0[i] > seriesMax) seriesMax = pa0[i];
        bool seriesOn[COUNT];
        float seriesHz[COUNT];
        float seriesA[COUNT];
        memset(seriesOn, 0, sizeof(seriesOn));
        memset(seriesHz, 0, sizeof(seriesHz));
        memset(seriesA, 0, sizeof(seriesA));
        nPicked = 0;
        if (seriesMax > 1.0e-8f && np > 0) {
            int ordA[PickScratch::PMAX];
            const int nSort = (np < PickScratch::PMAX) ? np : PickScratch::PMAX;
            for (int i = 0; i < nSort; ++i) ordA[i] = i;
            for (int i = 1; i < nSort; ++i) {
                const int v = ordA[i];
                int j = i;
                while (j > 0 && pa0[ordA[j - 1]] < pa0[v]) {
                    ordA[j] = ordA[j - 1];
                    --j;
                }
                ordA[j] = v;
            }
            // pa0 は取り込み時に sqrt 済みの振幅。帯域ごとの床はフレーム最大との比。
            // 大きい山から採る。上のコードトーンを「下の倍音」の根拠にすると
            // オクターブ下の誤ったベースが残るので、自分より小さい部分音または
            // 第2・第3倍音が揃った低音系列を見る。基音が無い音を下に作り足すことはしない。
            // 0 低音、1 中音、2 C5、3 C6、4 C7。
            auto bandOf = [](int k) -> int {
                if (k < BASS_END) return 0;
                if (k < MID_END) return 1;
                if (k < 84) return 2;
                if (k < EDGE_HI) return 3;
                return 4;
            };
            const float floorR[5] = { 0.14f, 0.11f, 0.07f, 0.07f, 0.14f };
            const int cap[5] = { 4, 10, 4, 3, 1 };
            int used[5] = {};
            auto centsTo = [](float hz, float target) -> float {
                if (hz <= 1.0f || target <= 1.0f) return 9999.0f;
                return 1731.234049066756f * fabsf(logf(hz / target));
            };
            float bMax = 0.0f;
            if (p.burstHz && p.burstAmp && p.nBurst > 0) {
                for (int i = 0; i < p.nBurst; ++i)
                    if (p.burstAmp[i] > bMax) bMax = p.burstAmp[i];
            }
            auto relShortKey = [&](int key) -> float {
                if (bMax <= 0.0f || !p.burstHz || !p.burstAmp) return 0.0f;
                const float target = PianoKey::KeyHz(key);
                float best = 0.0f;
                for (int i = 0; i < p.nBurst; ++i) {
                    if (centsTo(p.burstHz[i], target) < 60.0f) {
                        const float r = p.burstAmp[i] / bMax;
                        if (r > best) best = r;
                    }
                }
                return best;
            };
            auto quietHits = [&](float hz, float amp, float minRat) -> int {
                int hit = 0;
                const float mul[3] = { 2.0f, 3.0f, 4.0f };
                const float tol[3] = { 32.0f, 36.0f, 40.0f };
                for (int n = 0; n < 3; ++n) {
                    float best = 0.0f;
                    const float target = hz * mul[n];
                    for (int j = 0; j < nSort; ++j) {
                        if (centsTo(phz[j], target) < tol[n] && pa0[j] > best)
                            best = pa0[j];
                    }
                    if (best >= amp * minRat && best <= amp * 1.05f && best >= seriesMax * 0.06f)
                        ++hit;
                }
                return hit;
            };
            auto hasFullBassSeries = [&](float hz, float amp) -> bool {
                float b2 = 0.0f, b3 = 0.0f;
                for (int j = 0; j < nSort; ++j) {
                    if (centsTo(phz[j], hz * 2.0f) < 65.0f && pa0[j] > b2) b2 = pa0[j];
                    if (centsTo(phz[j], hz * 3.0f) < 65.0f && pa0[j] > b3) b3 = pa0[j];
                }
                return b2 >= amp * 0.25f && b2 <= amp * 4.5f
                    && b3 >= amp * 0.20f && b3 <= amp * 4.5f
                    && b2 >= seriesMax * 0.08f && b3 >= seriesMax * 0.08f;
            };
            int hits[PickScratch::PMAX];
            int hitsSoft[PickScratch::PMAX];
            int keyHitsSoft[COUNT] = {};
            float bandLin[5] = {};
            for (int i = 0; i < nSort; ++i) {
                const int k = KeyFromHzTune(phz[i], p.tuneSemis);
                if (k < MUSIC_LOW_FLOOR || k > 100) continue;
                const int b = bandOf(k);
                if (pa0[i] > bandLin[b]) bandLin[b] = pa0[i];
            }
            for (int i = 0; i < nSort; ++i) {
                hits[i] = quietHits(phz[i], pa0[i], 0.40f);
                hitsSoft[i] = quietHits(phz[i], pa0[i], 0.20f);
                const int k = KeyFromHzTune(phz[i], p.tuneSemis);
                if (k < MUSIC_LOW_FLOOR || k > 100) continue;
                if (k < BASS_END && hitsSoft[i] < 1 && pa0[i] >= bandLin[0] * 0.55f
                    && hasFullBassSeries(phz[i], pa0[i]))
                    hitsSoft[i] = 1;
                if (hitsSoft[i] > keyHitsSoft[k]) keyHitsSoft[k] = hitsSoft[i];
            }
            for (int oi = 0; oi < nSort; ++oi) {
                const int i = ordA[oi];
                float hz = phz[i];
                int k = KeyFromHzTune(hz, p.tuneSemis);
                if (k < MUSIC_LOW_FLOOR || k > 100) continue;
                int b = bandOf(k);
                float fl = floorR[b];
                if (b == 0 && hitsSoft[i] >= 1) fl = 0.12f;
                if (b == 1 && hitsSoft[i] >= 1) fl = 0.10f;
                if ((b == 2 || b == 3) && bandLin[b] > 0.0f
                    && pa0[i] >= bandLin[b] * 0.75f && relShortKey(k) >= 0.06f)
                    fl = 0.055f;
                if (pa0[i] < seriesMax * fl) continue;
                if (k >= 72 && pa0[i] < seriesMax * 0.12f && bandLin[b] > 0.0f
                    && pa0[i] < bandLin[b] * 0.40f)
                    continue;
                if (b == 4) {
                    const float needC7 = (k == 96) ? ((relShortKey(96) >= 0.05f) ? 0.24f : 0.40f) : 0.50f;
                    if (pa0[i] < seriesMax * needC7) continue;
                }
                if (k >= 88 && pa0[i] < seriesMax * 0.10f) {
                    if (!(pa0[i] >= seriesMax * 0.06f && relShortKey(k) >= 0.10f)) continue;
                }
                // 部分音の無い弱い低音は、上の和音のオクターブ下にできた影。
                if (b == 0 && hitsSoft[i] < 1 && pa0[i] < seriesMax * 0.18f) continue;
                if (used[b] >= cap[b]) continue;
                if (prevActive && p.prevF0Hz) {
                    for (int d = -1; d <= 1; ++d) {
                        const int q = k + d;
                        if (q < MUSIC_LOW_FLOOR || q >= COUNT || !prevActive[q]) continue;
                        if (p.prevF0Hz[q] <= 1.0f) continue;
                        if (centsTo(hz, p.prevF0Hz[q]) <= 55.0f) {
                            k = q;
                            hz = p.prevF0Hz[q];
                            break;
                        }
                    }
                }
                if (seriesOn[k]) continue;
                if (k > 100) continue;
                b = bandOf(k);
                if (used[b] >= cap[b]) continue;
                // 同一鍵内の主ローブ割れ(0.85 半音以内)は強い方だけ残す。
                // 隣鍵(1 半音)は下の clash で、全音(2 半音)は和音として扱う。
                bool shoulder = false;
                for (int j = 0; j < nSort && !shoulder; ++j) {
                    if (!(pa0[j] > pa0[i] * 1.35f)) continue;
                    if (centsTo(phz[j], phz[i]) <= 85.0f) shoulder = true;
                }
                if (shoulder) continue;
                // 2.4〜3.5 半音隣に部分音のある大きな山があるときは、根拠のない弱い側を捨てる。
                const int hitForBacked = (k >= 60) ? hitsSoft[i] : hits[i];
                if (hitForBacked < 1 && pa0[i] < seriesMax * 0.30f) {
                    bool backed = false;
                    for (int j = 0; j < nSort && !backed; ++j) {
                        if (hits[j] < 1 || !(pa0[j] > pa0[i] * 1.35f)) continue;
                        const float gap = centsTo(phz[j], phz[i]);
                        // 300 cent 付近は短 3 度の和音。そこから外れた中途半端な割れだけ捨てる。
                        if (gap >= 240.0f && gap <= 350.0f && fabsf(gap - 300.0f) > 40.0f)
                            backed = true;
                    }
                    if (backed) continue;
                }
                const int hit = hits[i];
                bool clash = false;
                for (int q = k - 3; q <= k + 3 && !clash; ++q) {
                    if (q < 0 || q >= COUNT || !seriesOn[q]) continue;
                    const int dk = (q > k) ? (q - k) : (k - q);
                    if (dk <= 1) clash = true;
                    else if (dk == 2 && hit < 1) {
                        const float need = (hitsSoft[i] >= 1) ? 0.40f : 0.48f;
                        if (pa0[i] < seriesA[q] * need) clash = true;
                    }
                    else if (dk == 3 && hit < 1) {
                        const float need = (k >= 60 && hitsSoft[i] >= 1) ? 0.20f : 0.30f;
                        if (pa0[i] < seriesA[q] * need) clash = true;
                    }
                }
                if (clash) continue;
                bool ghost = false;
                const float champNeed = (b >= 3) ? 0.36f : 0.30f;
                const bool bandChamp = bandLin[b] > 0.0f && pa0[i] >= bandLin[b] * 0.85f
                    && pa0[i] >= seriesMax * champNeed;
                const int maxN = (k >= 84) ? 10 : 5;
                for (int lo = MUSIC_LOW_FLOOR; lo < k && !ghost; ++lo) {
                    if (!seriesOn[lo]) continue;
                    const float f0 = seriesHz[lo];
                    const float aLo = seriesA[lo];
                    // 自分より大きい倍音も見る。真の実音は自分の系列(hitsSoft)を持つ。
                    if (f0 <= 1.0f || (aLo < pa0[i] && aLo < seriesMax * 0.22f)) continue;
                    const float ratio = phz[i] / f0;
                    const int n = (int)floorf(ratio + 0.5f);
                    if (n < 2 || n > maxN) continue;
                    if (centsTo(phz[i], f0 * (float)n) > 40.0f) continue;
                    // オクターブ系(2・4・6 倍)は整数倍ちょうどに張り付く。自分の部分音が
                    // 無ければ下の音の倍音。下の音よりはっきり弱いものを倍音として落とす。
                    // 実音(自分の系列あり)は通す。5 度(3 倍)は実音が同じ位置に来るので触らない。
                    if (hitsSoft[i] < 1 && (n == 2 || n == 4 || n == 6)
                        && aLo >= seriesMax * 0.20f && pa0[i] < aLo * 0.75f
                        && centsTo(phz[i], f0 * (float)n) < 24.0f) {
                        ghost = true;
                        break;
                    }
                    // 第5倍音(-14 cent)・第7倍音(-31 cent)は平均律の鍵中心から外れて整数倍に張り付く。
                    const float keyDev = centsTo(phz[i], PianoKey::KeyHz(k));
                    if (n == 7 && centsTo(phz[i], f0 * 7.0f) < 20.0f && keyDev > 16.0f) {
                        ghost = true;
                        break;
                    }
                    if (n == 5 && centsTo(phz[i], f0 * 5.0f) < 9.0f && keyDev > 9.0f && hitsSoft[i] < 1) {
                        ghost = true;
                        break;
                    }
                    // バンド最大でも、自分の系列が無ければ倍音判定を免れさせない。
                    if (bandChamp && hitsSoft[i] >= 1) continue;
                    float lim = (n >= 6) ? 0.30f : 0.20f;
                    if (n == 2) lim = 0.50f;
                    else if (n == 3) lim = 0.36f;
                    else if (n == 4) lim = 0.26f;
                    if (hit >= 2 && n >= 3) continue;
                    if (n >= 3 && n <= 5 && pa0[i] >= seriesMax * 0.28f) continue;
                    if ((n == 6 || n == 8) && pa0[i] >= seriesMax * 0.28f) continue;
                    if (n >= 6 && pa0[i] >= seriesMax * 0.25f) continue;
                    if (n == 2 && k >= 84 && pa0[i] >= seriesMax * 0.25f) continue;
                    if (n == 2 && hitsSoft[i] >= 1 && pa0[i] >= seriesMax * 0.18f) continue;
                    if (n == 2 && pa0[i] >= seriesMax * 0.32f && pa0[i] >= aLo * 0.40f) continue;
                    if (n == 2 && centsTo(phz[i], f0 * 2.0f) < 22.0f && aLo > pa0[i] * 1.6f
                        && pa0[i] < seriesMax * 0.18f) {
                        ghost = true;
                        break;
                    }
                    if (pa0[i] < aLo * lim && pa0[i] < seriesMax * 0.55f)
                        ghost = true;
                }
                if (ghost) continue;
                seriesOn[k] = true;
                seriesHz[k] = hz;
                seriesA[k] = pa0[i];
                ++used[b];
                ++nPicked;
            }
            // 出した低音が、一番大きい中音の 1 オクターブ下から外れていて、
            // その位置に十分な山があるときだけ替える。山が無い音は作らない。
            int loudMid = -1;
            float loudA = 0.0f;
            for (int key = BASS_END; key < MID_END; ++key) {
                if (seriesOn[key] && seriesA[key] > loudA) {
                    loudA = seriesA[key];
                    loudMid = key;
                }
            }
            if (loudMid >= 0) {
                const int target = loudMid - 12;
                int best = -1;
                float bestA = 0.0f, bestHz = 0.0f;
                if (target >= MUSIC_LOW_FLOOR && target < BASS_END) {
                    for (int i = 0; i < nSort; ++i) {
                        const int key = KeyFromHzTune(phz[i], p.tuneSemis);
                        if (abs(key - target) > 1) continue;
                        if (centsTo(phz[i], PianoKey::KeyHz(target)) > 70.0f) continue;
                        if (pa0[i] < seriesMax * 0.40f) continue;
                        if (pa0[i] > bestA) {
                            bestA = pa0[i];
                            best = key;
                            bestHz = phz[i];
                        }
                    }
                }
                int cur = -1;
                for (int key = MUSIC_LOW_FLOOR; key < BASS_END; ++key) {
                    if (seriesOn[key]) { cur = key; break; }
                }
                if (best >= 0 && (cur < 0 || abs(cur - target) > 2)) {
                    bool dropCur = true;
                    if (cur >= 0) {
                        if ((cur + 12 < COUNT && seriesOn[cur + 12])
                            || (keyHitsSoft[cur] >= 1 && seriesA[cur] >= seriesMax * 0.22f && seriesA[cur] < seriesMax * 0.90f))
                            dropCur = false;
                    }
                    if (cur >= 0 && dropCur) {
                        seriesOn[cur] = false;
                        const int h3 = cur + 19;
                        if (h3 < COUNT && seriesOn[h3] && seriesA[h3] < seriesMax * 0.48f)
                            seriesOn[h3] = false;
                    }
                    seriesOn[best] = true;
                    seriesHz[best] = bestHz;
                    seriesA[best] = bestA;
                }
            }
            // 選んだ低音の 1 オクターブ上にも同じ帯域の山があるときは、そのオクターブも残す。
            {
                int bassNow = -1;
                float bassA = 0.0f, bassHz = 0.0f;
                for (int key = MUSIC_LOW_FLOOR; key < BASS_END; ++key) {
                    if (seriesOn[key] && seriesA[key] > bassA) {
                        bassA = seriesA[key];
                        bassNow = key;
                        bassHz = seriesHz[key];
                    }
                }
                if (bassNow >= 0 && bassHz > 1.0f) {
                    const int up = bassNow + 12;
                    if (up < BASS_END) {
                        int bestUp = -1;
                        float bestUpA = 0.0f, bestUpHz = 0.0f;
                        for (int i = 0; i < nSort; ++i) {
                            const int key = KeyFromHzTune(phz[i], p.tuneSemis);
                            if (abs(key - up) > 1) continue;
                            if (centsTo(phz[i], bassHz * 2.0f) > 40.0f) continue;
                            if (pa0[i] < seriesMax * 0.40f) continue;
                            if (pa0[i] > bestUpA) {
                                bestUpA = pa0[i];
                                bestUp = key;
                                bestUpHz = phz[i];
                            }
                        }
                        if (bestUp >= 0 && !seriesOn[bestUp]) {
                            seriesOn[bestUp] = true;
                            seriesHz[bestUp] = bestUpHz;
                            seriesA[bestUp] = bestUpA;
                        }
                    }
                }
            }
            // 3〜4 音目の低音は、十分な振幅または自分の部分音があるときだけ残す。
            for (int pass = 0; pass < 2; ++pass) {
                int nBass = 0;
                int quiet = -1;
                float quietA = 1.0e9f;
                for (int key = MUSIC_LOW_FLOOR; key < BASS_END; ++key) {
                    if (!seriesOn[key]) continue;
                    ++nBass;
                    if (seriesA[key] < quietA) {
                        quietA = seriesA[key];
                        quiet = key;
                    }
                }
                if (nBass >= 4 && quiet >= 0 && (quietA < seriesMax * 0.32f || keyHitsSoft[quiet] < 1)) {
                    seriesOn[quiet] = false;
                }
                else if (nBass == 3 && quiet >= 0 && quietA < seriesMax * 0.30f
                    && !(keyHitsSoft[quiet] >= 1 && quietA >= seriesMax * 0.24f)) {
                    seriesOn[quiet] = false;
                }
            }
            // --- 倍音説明パス ---
            // 実音は自分の倍音列(2f/3f/4f = keyHitsSoft)を持つ。整数倍(2〜8 倍)に
            // 張り付き、自分の系列を持たない音は、下の実音が出している倍音そのもの。
            // 個別の閾値規則ではなく、全倍音・全下位音を一括で清算して「主音」を残す。
            {
                bool on0[COUNT];
                float a0arr[COUNT];
                float f0arr[COUNT];
                for (int k = 0; k < COUNT; ++k) {
                    on0[k] = seriesOn[k];
                    a0arr[k] = seriesA[k];
                    f0arr[k] = (seriesHz[k] > 1.0f) ? seriesHz[k] : PianoKey::KeyHz(k);
                }
                for (int hi = MUSIC_LOW_FLOOR; hi < COUNT; ++hi) {
                    if (!seriesOn[hi]) continue;
                    // 実音は自分の系列を持つ。高音域の倍音は自分の部分音が乏しいので、
                    // 低中音は 1 本、高音(C5 以上)は 2 本の自系列があれば実音として守る。
                    const int needOwn = (hi >= 93) ? 2 : 1;
                    if (keyHitsSoft[hi] >= needOwn) continue;
                    if (a0arr[hi] >= seriesMax * 0.40f) continue;   // 突出した山は主音として残す
                    const float fHi = f0arr[hi];
                    // 下の実音たちの倍音エネルギーで観測振幅をどれだけ説明できるか積算する。
                    float explained = 0.0f;
                    for (int lo = MUSIC_LOW_FLOOR; lo < hi; ++lo) {
                        if (!on0[lo]) continue;
                        const float f0 = f0arr[lo];
                        if (f0 <= 1.0f || a0arr[lo] < a0arr[hi]) continue; // 基音は倍音以上
                        const float ratio = fHi / f0;
                        const int n = (int)floorf(ratio + 0.5f);
                        if (n < 2 || n > 8) continue;
                        if (centsTo(fHi, f0 * (float)n) > 30.0f) continue;
                        // n 倍音の見込み振幅(観測は sqrt power、おおむね 1/n で減衰)。
                        explained += a0arr[lo] / (float)n;
                    }
                    // 観測振幅が下位音の倍音和で説明し切れるなら倍音として落とす。
                    if (explained >= a0arr[hi] * 0.45f)
                        seriesOn[hi] = false;
                }
            }
            for (int key = 96; key <= 100; ++key) {
                if (!seriesOn[key]) continue;
                const float needC7 = (key == 96) ? ((relShortKey(96) >= 0.05f) ? 0.15f : 0.24f) : 0.50f;
                if (seriesA[key] < seriesMax * needC7)
                    seriesOn[key] = false;
            }
            for (int key = 72; key <= 100; ++key) {
                if (!seriesOn[key]) continue;
                const int b = bandOf(key);
                if (seriesA[key] < seriesMax * 0.12f && bandLin[b] > 0.0f
                    && seriesA[key] < bandLin[b] * 0.40f)
                    seriesOn[key] = false;
            }
        }
        nPicked = 0;
        for (int i = 0; i < COUNT; ++i) {
            if (!seriesOn[i]) {
                outPicked[i] = false;
                outOwnPow[i] = 0.0f;
                if (p.outF0Hz) p.outF0Hz[i] = 0.0f;
                continue;
            }
            outPicked[i] = true;
            outOwnPow[i] = blend[i] > seriesA[i] ? blend[i] : seriesA[i];
            if (p.outF0Hz) p.outF0Hz[i] = seriesHz[i];
            ++nPicked;
        }
        if (p.melodyHz) *p.melodyHz = 0.0f;
        if (p.melodyMiss) *p.melodyMiss = 0;
        if (p.ownVoice) memset(p.ownVoice, 0, sizeof(bool) * COUNT);
        return nPicked;

    }
}
