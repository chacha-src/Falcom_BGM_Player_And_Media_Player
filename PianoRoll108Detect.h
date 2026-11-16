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
#include <algorithm>
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

        bool seriesOn[COUNT];
        float seriesHz[COUNT];
        float seriesA[COUNT];
        memset(seriesOn, 0, sizeof(seriesOn));
        memset(seriesHz, 0, sizeof(seriesHz));
        memset(seriesA, 0, sizeof(seriesA));
        int nPicked = 0;
        // スパース KL-NMF + 帯域バランス白色化 + ドラム基底辞書による基音分離。
        // ドラムの平坦な広帯域ノイズはオクターブ別ドラム基底 u[d] が吸収し、
        // 正音の倍音エネルギーは各鍵の調波テンプレート W[k] が基音側へ集約する。
        {
            static constexpr int NMF_K0 = MUSIC_LOW_FLOOR; // 28 (E1)
            static constexpr int NMF_K1 = EDGE_HI;         // 96 (C7)
            static constexpr int NMF_NK = NMF_K1 - NMF_K0; // 68
            static constexpr int NMF_ND = 7;               // 6 オクターブ + 全体広帯域
            static constexpr int NMF_MAX_E = 10;

            float bMaxN = 0.0f;
            if (p.burstHz && p.burstAmp && p.nBurst > 0) {
                for (int i = 0; i < p.nBurst; ++i)
                    if (p.burstAmp[i] > bMaxN) bMaxN = p.burstAmp[i];
            }
            auto centsDist = [](float a, float b) -> float {
                if (a <= 1.0f || b <= 1.0f) return 9999.0f;
                return 1731.234049066756f * fabsf(logf(a / b));
            };
            auto relShortN = [&](int key) -> float {
                if (bMaxN <= 0.0f || !p.burstHz || !p.burstAmp) return 0.0f;
                const float tgt = PianoKey::KeyHz(key) * powf(2.0f, p.tuneSemis / 12.0f);
                float best = 0.0f;
                for (int i = 0; i < p.nBurst; ++i) {
                    if (centsDist(p.burstHz[i], tgt) < 55.0f) {
                        const float v = p.burstAmp[i] / bMaxN;
                        if (v > best) best = v;
                    }
                }
                return best;
            };

            float obsHz[PickScratch::PMAX];
            float obsY[PickScratch::PMAX];
            float obsSnr[PickScratch::PMAX];
            float obsRawP[PickScratch::PMAX];
            int   obsKey[PickScratch::PMAX];
            int   nObs = 0;
            const int srcLim = (np < PickScratch::PMAX) ? np : PickScratch::PMAX;
            for (int i = 0; i < srcLim; ++i) {
                const float hz = phz[i];
                const float rawP = pa0[i] * pa0[i];
                if (hz <= 1.0f || rawP <= 0.0f) continue;
                const int k = KeyFromHzTune(hz, p.tuneSemis);
                if (k < NMF_K0 || k > 100) continue;
                const float a = pa0[i];
                const float b = (blend[k] > 0.0f) ? sqrtf(blend[k]) : a;
                const float mix = (relShortN(k) >= 0.14f) ? 0.30f : 0.55f;
                const float ampC = (1.0f - mix) * a + mix * (a < b * 1.25f ? a : b * 1.25f);
                const float nfVal = (floorPow[k] > 0.0f) ? sqrtf(floorPow[k]) : 0.0f;
                const float fSub = (k < BASS_END) ? 0.68f : ((k >= MID_END) ? 0.80f : 0.72f);
                const float net = ampC - fSub * nfVal;
                if (net <= 1.0e-5f) continue;
                obsHz[nObs] = hz;
                obsY[nObs] = net;
                obsSnr[nObs] = (nfVal > 1.0e-6f) ? (ampC / nfVal) : 10.0f;
                obsRawP[nObs] = rawP;
                obsKey[nObs] = k;
                ++nObs;
            }

            memset(seriesOn, 0, sizeof(seriesOn));
            memset(seriesHz, 0, sizeof(seriesHz));
            memset(seriesA, 0, sizeof(seriesA));

            if (nObs > 0) {
                // 帯域バランス白色化: 低音(nu=0.88, 1.55x)を保ちつつ中高音の平坦化(nu=0.72)を行う
                float octPow[8] = {};
                int   octCnt[8] = {};
                for (int q = 0; q < nObs; ++q) {
                    int o = obsKey[q] / 12;
                    if (o > 7) o = 7;
                    octPow[o] += obsY[q] * obsY[q];
                    ++octCnt[o];
                }
                float sigMax = 0.0f, octMean = 0.0f;
                int mc = 0;
                for (int o = 2; o <= 7; ++o) {
                    if (octCnt[o] <= 0) continue;
                    const float sg = sqrtf(octPow[o] / (float)octCnt[o]);
                    if (sg > sigMax) sigMax = sg;
                    octMean += sg;
                    ++mc;
                }
                if (mc > 0) octMean /= (float)mc;
                if (octMean > 1.0e-6f) {
                    const float floorClamp = (octMean * 0.25f > sigMax * 0.18f) ? (octMean * 0.25f) : (sigMax * 0.18f);
                    for (int q = 0; q < nObs; ++q) {
                        int o = obsKey[q] / 12;
                        if (o > 7) o = 7;
                        float sig = sqrtf(octPow[o] / (float)octCnt[o]);
                        if (sig < floorClamp) sig = floorClamp;
                        const float nu = (obsKey[q] < BASS_END) ? 0.88f : 0.72f;
                        obsY[q] *= powf(sig / octMean, nu - 1.0f);
                        if (obsKey[q] < BASS_END) obsY[q] *= 1.55f;
                    }
                }

                float yMax = 0.0f;
                for (int q = 0; q < nObs; ++q)
                    if (obsY[q] > yMax) yMax = obsY[q];
                if (yMax > 1.0e-6f) {
                    for (int q = 0; q < nObs; ++q) obsY[q] /= yMax;

                    float keyHz[NMF_NK];
                    float keyObs[NMF_NK] = {};
                    float keySnr[NMF_NK] = {};
                    float keyRawP[NMF_NK] = {};
                    const float tuneMulK = powf(2.0f, p.tuneSemis / 12.0f);
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        const float tgt = PianoKey::KeyHz(NMF_K0 + ki) * tuneMulK;
                        float bestHz = tgt, bestA = 0.0f, bestS = 0.0f, bestRP = 0.0f;
                        for (int q = 0; q < nObs; ++q) {
                            if (centsDist(obsHz[q], tgt) < 42.0f && obsY[q] > bestA) {
                                bestA = obsY[q];
                                bestHz = obsHz[q];
                                bestS = obsSnr[q];
                                bestRP = obsRawP[q];
                            }
                        }
                        keyHz[ki] = bestHz;
                        keyObs[ki] = bestA;
                        keySnr[ki] = bestS;
                        keyRawP[ki] = bestRP;
                    }

                    int   wIdx[NMF_NK][NMF_MAX_E];
                    float wVal[NMF_NK][NMF_MAX_E];
                    int   wLen[NMF_NK] = {};
                    float colSum[NMF_NK] = {};
                    int   keyPartials[NMF_NK] = {};

                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        if (keyObs[ki] <= 0.015f) continue;
                        const float f0 = keyHz[ki];
                        for (int h = 1; h <= NMF_MAX_E; ++h) {
                            const float tgt = f0 * (float)h;
                            if (tgt > 4600.0f) break;
                            const float tol = (h == 1) ? 35.0f : ((h <= 4) ? 32.0f : 38.0f);
                            int bestP = -1;
                            float bestD = tol;
                            for (int q = 0; q < nObs; ++q) {
                                const float d = centsDist(obsHz[q], tgt);
                                if (d < bestD) { bestD = d; bestP = q; }
                            }
                            if (bestP >= 0) {
                                if (h >= 2 && h <= 4 && obsY[bestP] >= keyObs[ki] * 0.18f)
                                    ++keyPartials[ki];
                                float w = powf((float)h, -0.85f);
                                if (h == 1) w *= 1.35f;
                                const int e = wLen[ki]++;
                                wIdx[ki][e] = bestP;
                                wVal[ki][e] = w;
                                colSum[ki] += w;
                            }
                        }
                        if (colSum[ki] > 0.0f) {
                            const float inv = 1.0f / colSum[ki];
                            for (int e = 0; e < wLen[ki]; ++e) wVal[ki][e] *= inv;
                            colSum[ki] = 1.0f;
                        }
                    }

                    int   wdIdx[NMF_ND][PickScratch::PMAX];
                    float wdVal[NMF_ND][PickScratch::PMAX];
                    int   wdLen[NMF_ND] = {};
                    float colSumD[NMF_ND] = {};
                    int   octCnt2[6] = {};
                    for (int q = 0; q < nObs; ++q) {
                        int d = obsKey[q] / 12 - 2;
                        if (d < 0) d = 0;
                        if (d > 5) d = 5;
                        wdIdx[d][wdLen[d]] = q; wdVal[d][wdLen[d]] = 1.0f; ++wdLen[d];
                        colSumD[d] += 1.0f; ++octCnt2[d];
                        wdIdx[6][wdLen[6]] = q; wdVal[6][wdLen[6]] = 1.0f; ++wdLen[6];
                        colSumD[6] += 1.0f;
                    }
                    for (int d = 0; d < NMF_ND; ++d) {
                        if (colSumD[d] > 0.0f) {
                            const float inv = 1.0f / colSumD[d];
                            for (int e = 0; e < wdLen[d]; ++e) wdVal[d][e] *= inv;
                            colSumD[d] = 1.0f;
                        }
                    }

                    float hAct[NMF_NK] = {};
                    float uAct[NMF_ND] = {};
                    for (int ki = 0; ki < NMF_NK; ++ki) if (colSum[ki] > 0.0f) hAct[ki] = keyObs[ki];
                    for (int d = 0; d < NMF_ND; ++d) if (colSumD[d] > 0.0f) uAct[d] = 0.06f;

                    float est[PickScratch::PMAX];
                    for (int it = 0; it < 28; ++it) {
                        for (int q = 0; q < nObs; ++q) est[q] = 1.0e-6f;
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            const float hv = hAct[ki];
                            if (hv <= 1.0e-6f) continue;
                            for (int e = 0; e < wLen[ki]; ++e) est[wIdx[ki][e]] += hv * wVal[ki][e];
                        }
                        for (int d = 0; d < NMF_ND; ++d) {
                            const float uv = uAct[d];
                            if (uv <= 1.0e-6f) continue;
                            for (int e = 0; e < wdLen[d]; ++e) est[wdIdx[d][e]] += uv * wdVal[d][e];
                        }
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            if (hAct[ki] <= 1.0e-6f || colSum[ki] <= 0.0f) continue;
                            float num = 0.0f;
                            for (int e = 0; e < wLen[ki]; ++e)
                                num += wVal[ki][e] * (obsY[wIdx[ki][e]] / est[wIdx[ki][e]]);
                            float pen = 0.060f / (hAct[ki] + 0.08f);
                            if (prevActive && prevActive[NMF_K0 + ki]) pen *= 0.62f;
                            else if (p.percussive > 0.35f) pen *= (1.0f + 0.70f * p.percussive);
                            hAct[ki] *= num / (colSum[ki] + pen);
                        }
                        for (int d = 0; d < NMF_ND; ++d) {
                            if (uAct[d] <= 1.0e-6f || colSumD[d] <= 0.0f) continue;
                            float num = 0.0f;
                            for (int e = 0; e < wdLen[d]; ++e)
                                num += wdVal[d][e] * (obsY[wdIdx[d][e]] / est[wdIdx[d][e]]);
                            float ld = 0.21f;
                            if (d < 6 && octCnt2[d] >= 4) ld *= 0.70f;
                            if (p.percussive > 0.35f) {
                                const float pm = 1.0f - 0.35f * p.percussive;
                                ld *= (pm > 0.45f) ? pm : 0.45f;
                            }
                            uAct[d] *= num / (colSumD[d] + ld);
                        }
                    }

                    float hMax = 0.0f;
                    float bMaxH[3] = {};
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        if (hAct[ki] > hMax) hMax = hAct[ki];
                        const int k = NMF_K0 + ki;
                        const int b = (k < BASS_END) ? 0 : ((k < MID_END) ? 1 : 2);
                        if (hAct[ki] > bMaxH[b]) bMaxH[b] = hAct[ki];
                    }

                    if (hMax > 1.0e-5f) {
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            if (keyObs[ki] < 0.025f) continue;
                            const int k = NMF_K0 + ki;
                            const int b = (k < BASS_END) ? 0 : ((k < MID_END) ? 1 : 2);
                            const float hN = hAct[ki] / hMax;
                            float th = (b == 0) ? 0.068f : ((b == 1) ? 0.135f : 0.115f);
                            if (prevActive && prevActive[k]) th *= 0.68f;
                            else if (p.percussive > 0.35f) th *= (1.0f + 0.245f * p.percussive);
                            const bool mainOk = (hN >= th);
                            const bool bandOk = (bMaxH[b] > hMax * 0.16f && hAct[ki] >= bMaxH[b] * 0.34f && hN >= th * 0.45f);
                            const bool burstOk = (relShortN(k) >= 0.14f && hN >= th * 0.50f);
                            if (!(mainOk || bandOk || burstOk)) continue;
                            if (hN < th * 1.65f && !(prevActive && prevActive[k]) && relShortN(k) < 0.12f
                                && keyPartials[ki] < 1 && keySnr[ki] < 16.0f)
                                continue;
                            seriesOn[k] = true;
                            seriesHz[k] = keyHz[ki];
                            seriesA[k] = (blend[k] > keyRawP[ki]) ? blend[k] : keyRawP[ki];
                        }

                        // 隣接半音のマージ漏れを抑制
                        for (int k = NMF_K0; k + 1 < NMF_K1; ++k) {
                            if (seriesOn[k] && seriesOn[k + 1]) {
                                if (hAct[k - NMF_K0] >= hAct[k + 1 - NMF_K0]) seriesOn[k + 1] = false;
                                else seriesOn[k] = false;
                            }
                        }

                        auto obsAtN = [&](float tgt, float tol) -> float {
                            float ba = 0.0f;
                            for (int q = 0; q < nObs; ++q)
                                if (centsDist(obsHz[q], tgt) < tol && obsY[q] > ba) ba = obsY[q];
                            return ba;
                        };

                        // コム倍音・オクターブ上倍音の刈り取り
                        for (int hi = BASS_END; hi < NMF_K1; ++hi) {
                            if (!seriesOn[hi] || (prevActive && prevActive[hi]) || relShortN(hi) >= 0.14f) continue;
                            const float fHi = keyHz[hi - NMF_K0];
                            const float aHi = keyObs[hi - NMF_K0];
                            const float hi3 = obsAtN(fHi * 3.0f, 36.0f);
                            for (int lo = NMF_K0; lo < hi; ++lo) {
                                if (!seriesOn[lo]) continue;
                                const float fLo = keyHz[lo - NMF_K0];
                                const int n = (int)floorf(fHi / fLo + 0.5f);
                                if (n < 2 || n > 10 || centsDist(fHi, fLo * (float)n) > 36.0f) continue;
                                if (n == 2 && hAct[hi - NMF_K0] < hAct[lo - NMF_K0] * 0.55f && hi3 < aHi * 0.22f
                                    && obsAtN(fLo * 3.0f, 36.0f) >= keyObs[lo - NMF_K0] * 0.25f) {
                                    seriesOn[hi] = false;
                                    break;
                                }
                                if (n >= 4 && hi3 < aHi * 0.30f) {
                                    const float nbL = obsAtN(fLo * (float)(n - 1), 36.0f);
                                    const float nbR = obsAtN(fLo * (float)(n + 1), 36.0f);
                                    const float nb = (nbL > nbR) ? nbL : nbR;
                                    if (nb >= aHi * 0.30f && hAct[hi - NMF_K0] < hAct[lo - NMF_K0] * 0.85f) {
                                        seriesOn[hi] = false;
                                        break;
                                    }
                                }
                            }
                        }

                        // オクターブ上げすぎの回復: 下の基音に 2f+3f 系列があり上音に 3f が薄ければ下を起こし上を落とす
                        for (int hi = NMF_K1 - 1; hi >= C4_KEY; --hi) {
                            if (!seriesOn[hi]) continue;
                            const int lo = hi - 12;
                            if (lo < NMF_K0 || seriesOn[lo]) continue;
                            const float aLo = keyObs[lo - NMF_K0];
                            const float aHi = keyObs[hi - NMF_K0];
                            if (aLo < aHi * 0.12f || aLo < 0.04f) continue;
                            const float fLo = keyHz[lo - NMF_K0];
                            const float fHi = keyHz[hi - NMF_K0];
                            const float b2 = obsAtN(fLo * 2.0f, 34.0f);
                            const float b3 = obsAtN(fLo * 3.0f, 34.0f);
                            if (b2 >= aLo * 0.25f && b3 >= aLo * 0.20f && b2 >= 0.05f && b3 >= 0.04f) {
                                seriesOn[lo] = true;
                                seriesHz[lo] = fLo;
                                seriesA[lo] = (blend[lo] > keyRawP[lo - NMF_K0]) ? blend[lo] : keyRawP[lo - NMF_K0];
                                const float h3 = obsAtN(fHi * 3.0f, 34.0f);
                                if (h3 < aHi * 0.30f && aHi < aLo * 1.15f)
                                    seriesOn[hi] = false;
                            }
                        }
                    }
                }
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
