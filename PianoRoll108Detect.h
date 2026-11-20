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
        // 連続 NMF 活性・自己説明率の時間積分に使う解析ホップ(ms)。
        float dtMs = 16.0f;
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

    struct TemporalPickState
    {
        float hSmooth[COUNT];
        float shSmooth[COUNT];
        void clear() { memset(this, 0, sizeof(*this)); }
        void decaySilence()
        {
            for (int i = 0; i < COUNT; ++i) {
                hSmooth[i] *= 0.30f;
                shSmooth[i] *= 0.50f;
            }
        }
    };

    inline TemporalPickState& PickTemporal()
    {
        static TemporalPickState s{};
        return s;
    }

    inline void ResetPickState()
    {
        PickTemporal().clear();
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
        // 欠損倍音計上・2段音色適応 KL-NMF + 連続活性の非対称時間積分 + 隣接次数コム排除。
        // 1) 欠けている低次倍音(h=2..4、特に奇数次 h=3)を列和に残し、下オクターブ誤認を抑える。
        // 2) 第2パスで強い中低音の減衰傾きを観測倍音の中央値から適応させ、歪みギター等の高次倍音を親へ吸収する。
        // 3) 自己説明率(share)と帯域別の非対称時間積分で倍音ゴーストの居座りを断ち、スマートな線にする。
        {
            static constexpr int NMF_K0 = MUSIC_LOW_FLOOR; // 28 (E1)
            static constexpr int NMF_K1 = EDGE_HI;         // 96 (C7)
            static constexpr int NMF_NK = NMF_K1 - NMF_K0; // 68
            static constexpr int NMF_ND = 7;               // 6 オクターブ + 全体広帯域
            static constexpr int NMF_MAX_E = 12;

            TemporalPickState& tp = PickTemporal();
            const float dtMs = (p.dtMs >= 4.0f && p.dtMs <= 200.0f) ? p.dtMs : 16.0f;

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
                const float fSub = (k < BASS_END) ? 0.68f : ((k >= O5_HI) ? 0.85f : 0.72f);
                const float net = ampC - fSub * nfVal;
                if (net <= 1.0e-5f) continue;
                obsHz[nObs] = hz;
                obsY[nObs] = powf(net, 0.72f);
                obsSnr[nObs] = (nfVal > 1.0e-6f) ? (ampC / nfVal) : 10.0f;
                obsRawP[nObs] = rawP;
                obsKey[nObs] = k;
                ++nObs;
            }

            memset(seriesOn, 0, sizeof(seriesOn));
            memset(seriesHz, 0, sizeof(seriesHz));
            memset(seriesA, 0, sizeof(seriesA));

            if (nObs <= 0) {
                for (int i = 0; i < COUNT; ++i) tp.hSmooth[i] *= 0.40f;
            }
            else {
                // 帯域バランス白色化: 低音と高音の実音ダイナミクスを揃える
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
                        const float nu = (obsKey[q] < BASS_END) ? 0.88f : 0.56f;
                        obsY[q] *= powf(sig / octMean, nu - 1.0f);
                        if (obsKey[q] < BASS_END) obsY[q] *= 1.72f;
                        else if (obsKey[q] >= MID_END) obsY[q] *= 1.65f;
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
                    float keyCents[NMF_NK] = {};
                    const float tuneMulK = powf(2.0f, p.tuneSemis / 12.0f);
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        const float tgt = PianoKey::KeyHz(NMF_K0 + ki) * tuneMulK;
                        float bestHz = tgt, bestA = 0.0f, bestS = 0.0f, bestRP = 0.0f, bestC = 42.0f;
                        for (int q = 0; q < nObs; ++q) {
                            const float cd = centsDist(obsHz[q], tgt);
                            if (cd < 42.0f && obsY[q] > bestA) {
                                bestA = obsY[q];
                                bestHz = obsHz[q];
                                bestS = obsSnr[q];
                                bestRP = obsRawP[q];
                                bestC = cd;
                            }
                        }
                        keyHz[ki] = bestHz;
                        keyObs[ki] = bestA;
                        keySnr[ki] = bestS;
                        keyRawP[ki] = bestRP;
                        keyCents[ki] = bestC;
                    }

                    int   wIdx[NMF_NK][NMF_MAX_E];
                    int   wHarm[NMF_NK][NMF_MAX_E];
                    float wVal[NMF_NK][NMF_MAX_E];
                    float wMiss[NMF_NK] = {};
                    int   wLen[NMF_NK] = {};
                    float colSum[NMF_NK] = {};
                    int   keyPartials[NMF_NK] = {};

                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        if (keyObs[ki] <= 0.015f) continue;
                        const int k = NMF_K0 + ki;
                        const float f0 = keyHz[ki];
                        float obsW = 0.0f, missW = 0.0f;
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
                            float w = powf((float)h, -0.85f);
                            if (h == 1) w *= 1.35f;
                            if (bestP >= 0) {
                                if (h >= 2 && h <= 4 && obsY[bestP] >= keyObs[ki] * 0.18f)
                                    ++keyPartials[ki];
                                const int e = wLen[ki]++;
                                wIdx[ki][e] = bestP;
                                wHarm[ki][e] = h;
                                wVal[ki][e] = w;
                                obsW += w;
                            }
                            else if (h >= 2 && h <= 4) {
                                float ma = (k < BASS_END) ? 0.10f : 0.55f;
                                if (h == 3) ma *= 2.0f;
                                missW += w * ma;
                            }
                        }
                        if (obsW > 0.0f) {
                            const float inv = 1.0f / (obsW + missW);
                            for (int e = 0; e < wLen[ki]; ++e) wVal[ki][e] *= inv;
                            wMiss[ki] = missW * inv;
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
                    auto runNmfIters = [&](int nIt) {
                        for (int it = 0; it < nIt; ++it) {
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
                                float pen = 0.105f / (hAct[ki] + 0.065f);
                                if (keyCents[ki] > 16.0f) {
                                    const float ex = (keyCents[ki] - 16.0f) / 26.0f;
                                    pen *= (1.0f + 1.30f * ex * ex);
                                }
                                if (prevActive && prevActive[NMF_K0 + ki]) pen *= 0.78f;
                                else if (p.percussive > 0.35f) pen *= (1.0f + 0.55f * p.percussive);
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
                                    const float pm = 1.0f - 0.30f * p.percussive;
                                    ld *= (pm > 0.50f) ? pm : 0.50f;
                                }
                                uAct[d] *= num / (colSumD[d] + ld);
                            }
                        }
                    };

                    runNmfIters(18);

                    // 第2パス: 強い中低音(k<72)の倍音減衰スロープをロバスト中央値で適応し、
                    // 歪みギターや明るいブラスの高次倍音コムを基音へ回収する。
                    float h1Max = 0.0f;
                    for (int ki = 0; ki < NMF_NK; ++ki)
                        if (hAct[ki] > h1Max) h1Max = hAct[ki];
                    if (h1Max > 1.0e-5f) {
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            const int k = NMF_K0 + ki;
                            if (k >= MID_END || hAct[ki] < h1Max * 0.14f || wLen[ki] < 3 || keyObs[ki] <= 0.03f)
                                continue;
                            float slopes[NMF_MAX_E];
                            int ns = 0;
                            for (int e = 0; e < wLen[ki]; ++e) {
                                const int h = wHarm[ki][e];
                                if (h < 2 || h > NMF_MAX_E) continue;
                                const float rat = obsY[wIdx[ki][e]] / keyObs[ki];
                                if (rat <= 0.02f) continue;
                                float sl = -logf(rat) / logf((float)h);
                                if (sl < 0.08f) sl = 0.08f;
                                if (sl > 1.60f) sl = 1.60f;
                                slopes[ns++] = sl;
                            }
                            if (ns < 2) continue;
                            std::sort(slopes, slopes + ns);
                            const float fitSlope = slopes[ns / 2];
                            float newW[NMF_MAX_E];
                            float obsSum = 0.0f;
                            for (int e = 0; e < wLen[ki]; ++e) {
                                const int h = wHarm[ki][e];
                                const float stdW = powf((float)h, -0.85f) * ((h == 1) ? 1.35f : 1.0f);
                                if (h == 1) newW[e] = stdW;
                                else {
                                    const float fitW = powf((float)h, -fitSlope);
                                    newW[e] = 0.15f * stdW + 0.85f * fitW;
                                }
                                obsSum += newW[e];
                            }
                            const float keepObs = 1.0f - wMiss[ki];
                            if (obsSum > 1.0e-6f && keepObs > 0.05f) {
                                const float sc = keepObs / obsSum;
                                for (int e = 0; e < wLen[ki]; ++e) wVal[ki][e] = newW[e] * sc;
                            }
                        }
                        runNmfIters(14);
                    }

                    // 再構成スペクトルと各鍵の自己説明率 shareVal(h=1..2 の最大占有率)
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
                    float shareVal[NMF_NK] = {};
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        if (hAct[ki] <= 1.0e-6f) continue;
                        for (int e = 0; e < wLen[ki]; ++e) {
                            if (wHarm[ki][e] <= 2) {
                                const float s = (hAct[ki] * wVal[ki][e]) / est[wIdx[ki][e]];
                                if (s > shareVal[ki]) shareVal[ki] = s;
                            }
                        }
                    }

                    // 帯域別の非対称時間積分(低音はじっくり、高音メロディは素早く追従)
                    float hRawMax = 0.0f;
                    for (int ki = 0; ki < NMF_NK; ++ki)
                        if (hAct[ki] > hRawMax) hRawMax = hAct[ki];
                    float hUse[NMF_NK] = {};
                    float shUse[NMF_NK] = {};
                    const float aSh = 1.0f - expf(-dtMs / 65.0f);
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        const int k = NMF_K0 + ki;
                        const float v = (hRawMax > 1.0e-5f) ? (hAct[ki] / hRawMax) : 0.0f;
                        const float tu = (k < BASS_END) ? 75.0f : ((k < MID_END) ? 60.0f : 26.0f);
                        const float td = (k < BASS_END) ? 48.0f : ((k < MID_END) ? 40.0f : 34.0f);
                        const float a = (v >= tp.hSmooth[k]) ? (1.0f - expf(-dtMs / tu)) : (1.0f - expf(-dtMs / td));
                        tp.hSmooth[k] = tp.hSmooth[k] * (1.0f - a) + v * a;
                        hUse[ki] = tp.hSmooth[k];

                        if (hAct[ki] > 1.0e-5f)
                            tp.shSmooth[k] = tp.shSmooth[k] * (1.0f - aSh) + shareVal[ki] * aSh;
                        else
                            tp.shSmooth[k] *= (1.0f - aSh);
                        shUse[ki] = tp.shSmooth[k];
                    }

                    float hMax = 0.0f;
                    float bMaxH[3] = {};
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        if (hUse[ki] > hMax) hMax = hUse[ki];
                        const int k = NMF_K0 + ki;
                        const int b = (k < BASS_END) ? 0 : ((k < MID_END) ? 1 : 2);
                        if (hUse[ki] > bMaxH[b]) bMaxH[b] = hUse[ki];
                    }

                    if (hMax > 1.0e-5f) {
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            if (keyObs[ki] < 0.018f && hUse[ki] < hMax * 0.22f) continue;
                            const int k = NMF_K0 + ki;
                            // 超高域(D6以上)のシンバル金属共鳴の常時点灯を防ぐ
                            if (k >= 86 && keyPartials[ki] < 1 && keySnr[ki] < 18.0f && relShortN(k) < 0.22f)
                                continue;
                            const int b = (k < BASS_END) ? 0 : ((k < MID_END) ? 1 : 2);
                            const float hN = hUse[ki] / hMax;
                            float th = (b == 0) ? 0.095f : ((b == 1) ? 0.175f : 0.092f);
                            if (prevActive && prevActive[k]) th *= 0.72f;
                            else if (p.percussive > 0.35f) th *= (1.0f + 0.18f * p.percussive);
                            const bool mainOk = (hN >= th);
                            const bool bandOk = (bMaxH[b] > hMax * 0.14f && hUse[ki] >= bMaxH[b] * 0.48f && hN >= th * 0.45f);
                            const bool burstOk = (relShortN(k) >= 0.14f && hN >= th * 0.50f);
                            if (!(mainOk || bandOk || burstOk)) continue;
                            if (k >= BASS_END) {
                                const float shReq = (k < MID_END) ? 0.55f : 0.32f;
                                if (!(prevActive && prevActive[k])) {
                                    if (shareVal[ki] < shReq) continue;
                                }
                                else {
                                    if (shUse[ki] < shReq * 0.60f) continue;
                                }
                            }
                            if (hN < th * 1.60f && !(prevActive && prevActive[k]) && relShortN(k) < 0.12f
                                && keyPartials[ki] < 1 && keySnr[ki] < 16.0f)
                                continue;
                            seriesOn[k] = true;
                            seriesHz[k] = keyHz[ki];
                            seriesA[k] = (blend[k] > keyRawP[ki]) ? blend[k] : keyRawP[ki];
                        }

                        // 隣接半音のマージ漏れを抑制
                        for (int k = NMF_K0; k + 1 < NMF_K1; ++k) {
                            if (seriesOn[k] && seriesOn[k + 1]) {
                                if (hUse[k - NMF_K0] >= hUse[k + 1 - NMF_K0]) seriesOn[k + 1] = false;
                                else seriesOn[k] = false;
                            }
                        }

                        auto obsAtN = [&](float tgt, float tol) -> float {
                            float ba = 0.0f;
                            for (int q = 0; q < nObs; ++q)
                                if (centsDist(obsHz[q], tgt) < tol && obsY[q] > ba) ba = obsY[q];
                            return ba;
                        };

                        // 下オクターブのサブハーモニック・ゴースト(上音の偶数倍音への相乗り)を除去
                        for (int lo = NMF_K0; lo + 12 < NMF_K1; ++lo) {
                            if (!seriesOn[lo]) continue;
                            const int oct = lo + 12;
                            const float aLo = keyObs[lo - NMF_K0];
                            const float aOct = keyObs[oct - NMF_K0];
                            const float fLo = keyHz[lo - NMF_K0];
                            if (aOct >= aLo * 0.80f
                                && obsAtN(fLo * 3.0f, 36.0f) < aLo * 0.24f
                                && obsAtN(fLo * 5.0f, 38.0f) < aLo * 0.24f
                                && hUse[lo - NMF_K0] < hUse[oct - NMF_K0] * 0.90f) {
                                seriesOn[lo] = false;
                            }
                            if (seriesOn[lo] && seriesOn[oct] && aLo < aOct * 0.24f
                                && obsAtN(fLo * 3.0f, 36.0f) < aOct * 0.25f) {
                                seriesOn[lo] = false;
                            }
                        }

                        // 純正律デチューニング判定 + 隣接次数コム倍音排除(高域で 3*fHi が帯域外でも機能)
                        for (int hi = BASS_END; hi < NMF_K1; ++hi) {
                            if (!seriesOn[hi]) continue;
                            const float fHi = keyHz[hi - NMF_K0];
                            const float aHi = keyObs[hi - NMF_K0];
                            const bool can3 = (fHi * 3.0f <= 4400.0f);
                            const float hi3 = can3 ? obsAtN(fHi * 3.0f, 36.0f) : 0.0f;
                            const float hi2 = (fHi * 2.0f <= 4400.0f) ? obsAtN(fHi * 2.0f, 36.0f) : 0.0f;
                            const float c12 = centsDist(fHi, PianoKey::KeyHz(hi) * tuneMulK);
                            for (int lo = NMF_K0; lo < hi; ++lo) {
                                if (!seriesOn[lo]) continue;
                                const float fLo = keyHz[lo - NMF_K0];
                                const int n = (int)floorf(fHi / fLo + 0.5f);
                                if (n < 2 || n > 12) continue;
                                const float cJ = centsDist(fHi, fLo * (float)n);
                                if (cJ > 36.0f) continue;

                                if ((n == 5 || n == 7 || n == 11) && cJ + 5.0f < c12
                                    && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.98f) {
                                    seriesOn[hi] = false;
                                    break;
                                }
                                if (n >= 3 && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.95f) {
                                    const float sibL = obsAtN(fLo * (float)(n - 1), 38.0f);
                                    const float sibR = (fLo * (float)(n + 1) <= 4600.0f)
                                        ? obsAtN(fLo * (float)(n + 1), 38.0f) : sibL;
                                    const bool inComb = (sibL >= aHi * 0.22f && sibR >= aHi * 0.22f);
                                    bool sticksOut = false;
                                    if (can3 && hi3 >= aHi * 0.30f) {
                                        float combAt3 = 0.0f;
                                        if (fLo * (float)(3 * n - 1) <= 4600.0f) {
                                            const float cL = obsAtN(fLo * (float)(3 * n - 1), 38.0f);
                                            const float cR = obsAtN(fLo * (float)(3 * n + 1), 38.0f);
                                            combAt3 = (cL > cR) ? cL : cR;
                                        }
                                        if (hi3 > combAt3 * 1.45f) sticksOut = true;
                                    }
                                    else if (!can3 && fHi * 2.0f <= 4400.0f && hi2 >= aHi * 0.35f) {
                                        const float cL = obsAtN(fLo * (float)(2 * n - 1), 38.0f);
                                        const float cR = obsAtN(fLo * (float)(2 * n + 1), 38.0f);
                                        const float combAt2 = (cL > cR) ? cL : cR;
                                        if (hi2 > combAt2 * 1.50f) sticksOut = true;
                                    }
                                    if (inComb && !sticksOut) {
                                        if (n >= 5 || hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.65f) {
                                            seriesOn[hi] = false;
                                            break;
                                        }
                                    }
                                }
                                if (n == 2 && can3 && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.75f
                                    && hi3 < aHi * 0.24f
                                    && obsAtN(fLo * 3.0f, 36.0f) >= keyObs[lo - NMF_K0] * 0.22f) {
                                    seriesOn[hi] = false;
                                    break;
                                }
                                if (n >= 3 && can3 && hi3 < aHi * 0.26f
                                    && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * ((n == 3) ? 0.54f : 0.75f)) {
                                    seriesOn[hi] = false;
                                    break;
                                }
                            }
                        }

                        // オクターブ上げすぎの回復: 下の基音に明確な 2f+3f 系列があり上音に 3f が薄ければ下を起こす
                        for (int hi = NMF_K1 - 1; hi >= C4_KEY; --hi) {
                            if (!seriesOn[hi]) continue;
                            const int lo = hi - 12;
                            if (lo < NMF_K0 || seriesOn[lo]) continue;
                            const float aLo = keyObs[lo - NMF_K0];
                            const float aHi = keyObs[hi - NMF_K0];
                            if (aLo < aHi * 0.16f || aLo < 0.05f) continue;
                            const float fLo = keyHz[lo - NMF_K0];
                            const float fHi = keyHz[hi - NMF_K0];
                            const float b2 = obsAtN(fLo * 2.0f, 32.0f);
                            const float b3 = obsAtN(fLo * 3.0f, 32.0f);
                            if (b2 >= aLo * 0.25f && b3 >= aLo * 0.25f && b2 >= 0.05f && b3 >= 0.05f) {
                                seriesOn[lo] = true;
                                seriesHz[lo] = fLo;
                                seriesA[lo] = (blend[lo] > keyRawP[lo - NMF_K0]) ? blend[lo] : keyRawP[lo - NMF_K0];
                                const float h3 = (fHi * 3.0f <= 4400.0f) ? obsAtN(fHi * 3.0f, 34.0f) : aHi;
                                if (h3 < aHi * 0.28f && aHi < aLo * 1.15f)
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
