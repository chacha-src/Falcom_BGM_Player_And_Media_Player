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
        // 静かな曲の単一和音アタック(鋭いピーク群と深い谷)を打楽器ノイズと誤認しないよう、
        // 非ピーク谷ビンを含む広帯域連続上昇とオクターブ横断充填の両方を要求する。
        const float live = oMax * 0.015f;
        const float valleyLive = oMax * 0.020f;
        int rise = 0, valleyRise = 0, kMin = COUNT, kMax = -1;
        int octRise[6] = {};
        for (int i = LO; i < COUNT; ++i) {
            if (onset[i] < live) continue;
            if (onset[i] <= prevOnset[i] * 2.1f + live * 0.5f) continue;
            ++rise;
            if (i < kMin) kMin = i;
            if (i > kMax) kMax = i;
            const int oc = (i - LO) / 12;
            if (oc >= 0 && oc < 6) ++octRise[oc];
            const bool isLocalPeak = (i > LO && i + 1 < COUNT
                && onset[i] > onset[i - 1] && onset[i] >= onset[i + 1]);
            if (!isLocalPeak && onset[i] >= valleyLive) ++valleyRise;
        }
        int filledOcts = 0;
        for (int oc = 0; oc < 6; ++oc)
            if (octRise[oc] >= 5) ++filledOcts;
        float frac = 0.36f / (strict > 0.1f ? strict : 0.1f);
        if (frac < 0.10f) frac = 0.10f;
        if (frac > 0.90f) frac = 0.90f;
        const int needRise = (int)((float)(COUNT - LO) * frac);
        const int needValley = (needRise * 42) / 100;
        return rise >= needRise && valleyRise >= needValley
            && filledOcts >= 3 && (kMax - kMin) >= 38;
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
        static constexpr int PMAX = 160;
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
        float indepHold[COUNT];
        float indepPeak[COUNT];
        float onsetSlow[COUNT];
        int   indepStreak[COUNT];
        float busySmooth;
        void clear() { memset(this, 0, sizeof(*this)); }
        void decaySilence()
        {
            for (int i = 0; i < COUNT; ++i) {
                hSmooth[i] *= 0.30f;
                shSmooth[i] *= 0.50f;
                indepHold[i] *= 0.35f;
                indepPeak[i] *= 0.50f;
                onsetSlow[i] *= 0.40f;
                indepStreak[i] = 0;
            }
            busySmooth *= 0.50f;
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
        if (p.ownVoice) memset(p.ownVoice, 0, sizeof(bool) * COUNT);
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
            static constexpr int NMF_K1 = 100;             // D#7 まで候補に含める
            static constexpr int NMF_NK = NMF_K1 - NMF_K0; // 72
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
            float obsPre[PickScratch::PMAX];
            float obsWOct[PickScratch::PMAX];
            float obsSnr[PickScratch::PMAX];
            float obsRawP[PickScratch::PMAX];
            int   obsKey[PickScratch::PMAX];
            int   nObs = 0;
            const int srcLim = (np < PickScratch::PMAX) ? np : PickScratch::PMAX;
            for (int i = 0; i < srcLim; ++i) {
                const float hz = phz[i];
                const float rawP = pa0[i] * pa0[i];
                if (hz <= 1.0f || hz > 9200.0f || rawP <= 0.0f) continue;
                const float kf = 69.0f + 12.0f * log2f(hz / 440.0f) - p.tuneSemis;
                const int kNear = (int)floorf(kf + 0.5f);
                if (kNear < NMF_K0) continue;
                const int kClamped = (kNear < COUNT) ? kNear : (COUNT - 1);
                const float a = pa0[i];
                const float b = (kNear < COUNT && blend[kClamped] > 0.0f) ? sqrtf(blend[kClamped]) : a;
                const float mix = (kNear < COUNT && relShortN(kClamped) >= 0.14f) ? 0.30f : 0.55f;
                const float ampC = (1.0f - mix) * a + mix * (a < b * 1.25f ? a : b * 1.25f);
                float nfVal = (floorPow[kClamped] > 0.0f) ? sqrtf(floorPow[kClamped]) : 0.0f;
                if (kNear >= COUNT) {
                    const float roll = powf(3951.0f / hz, 0.65f);
                    nfVal *= (roll > 0.35f) ? roll : 0.35f;
                }
                const float fSub = (kNear < BASS_END) ? 0.68f : ((kNear >= O5_HI) ? 0.78f : 0.72f);
                const float net = ampC - fSub * nfVal;
                if (net <= 1.0e-5f) continue;
                obsHz[nObs] = hz;
                obsPre[nObs] = powf(net, 0.72f);
                obsY[nObs] = obsPre[nObs];
                obsWOct[nObs] = 1.0f;
                obsSnr[nObs] = (nfVal > 1.0e-6f) ? (ampC / nfVal) : 10.0f;
                obsRawP[nObs] = rawP;
                obsKey[nObs] = kNear;
                ++nObs;
            }

            memset(seriesOn, 0, sizeof(seriesOn));
            memset(seriesHz, 0, sizeof(seriesHz));
            memset(seriesA, 0, sizeof(seriesA));

            if (nObs <= 0) {
                for (int i = 0; i < COUNT; ++i) tp.hSmooth[i] *= 0.40f;
            }
            else {
                // オクターブ別の穏やかなRMSホワイトニング(NMF基音域 k<100 のみで集計し、ギター倍音密集時は抑え単独高音は持ち上げる)
                float octSq[6] = {};
                int octCntW[6] = {};
                for (int q = 0; q < nObs; ++q) {
                    const int k = obsKey[q];
                    if (k < 24 || k >= NMF_K1) continue;
                    int oc = (k - 24) / 12;
                    if (oc < 0) oc = 0;
                    if (oc > 5) oc = 5;
                    octSq[oc] += obsY[q] * obsY[q];
                    ++octCntW[oc];
                }
                float octRms[6] = {};
                float rmsSum = 0.0f;
                int rmsN = 0;
                for (int oc = 0; oc < 6; ++oc) {
                    if (octCntW[oc] > 0) {
                        octRms[oc] = sqrtf(octSq[oc] / (float)octCntW[oc]);
                        rmsSum += octRms[oc];
                        ++rmsN;
                    }
                }
                const float meanRms = (rmsN > 0) ? (rmsSum / (float)rmsN) : 0.0f;
                float wOct[6] = { 1, 1, 1, 1, 1, 1 };
                if (meanRms > 1.0e-6f) {
                    for (int oc = 0; oc < 6; ++oc) {
                        const float r = (octRms[oc] > meanRms * 0.30f) ? (octRms[oc] / meanRms) : 0.30f;
                        float w = powf(r, -0.25f);
                        const float wCap = (oc <= 1) ? 1.36f : ((oc >= 4) ? 1.38f : 1.30f);
                        if (w < 0.76f) w = 0.76f;
                        if (w > wCap) w = wCap;
                        wOct[oc] = w;
                    }
                }
                for (int q = 0; q < nObs; ++q) {
                    const float kf = 69.0f + 12.0f * log2f(obsHz[q] / 440.0f) - p.tuneSemis;
                    int oc = ((int)floorf(kf + 0.5f) - 24) / 12;
                    if (oc < 0) oc = 0;
                    if (oc > 5) oc = 5;
                    float bassShelf = (49.0f - kf) / 13.0f;
                    if (bassShelf < 0.0f) bassShelf = 0.0f;
                    if (bassShelf > 1.0f) bassShelf = 1.0f;
                    const float g = wOct[oc] * (1.0f + 0.34f * bassShelf);
                    obsWOct[q] = g;
                    obsY[q] *= g;
                }

                // 低音単発ブーム時に中高音全体が 1/7 へ圧縮され KL-NMF の非凸スパース死に陥る現象(バス・ポンピング)を防ぐ
                float yBassMax = 0.0f, yUpperMax = 0.0f;
                for (int q = 0; q < nObs; ++q) {
                    if (obsKey[q] < BASS_END) {
                        if (obsY[q] > yBassMax) yBassMax = obsY[q];
                    }
                    else if (obsKey[q] < NMF_K1) {
                        if (obsY[q] > yUpperMax) yUpperMax = obsY[q];
                    }
                }
                const float yMax = (yUpperMax > yBassMax * 0.66f) ? yUpperMax : (yBassMax * 0.66f);
                if (yMax > 1.0e-6f) {
                    for (int q = 0; q < nObs; ++q) {
                        obsY[q] /= yMax;
                        if (obsY[q] > 1.22f) obsY[q] = 1.22f;
                    }

                    float keyHz[NMF_NK];
                    float keyObs[NMF_NK] = {};
                    float keyPre[NMF_NK] = {};
                    float keyWO[NMF_NK] = {};
                    float keySnr[NMF_NK] = {};
                    float keyRawP[NMF_NK] = {};
                    float keyCents[NMF_NK] = {};
                    const float tuneMulK = powf(2.0f, p.tuneSemis / 12.0f);
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        const float tgt = PianoKey::KeyHz(NMF_K0 + ki) * tuneMulK;
                        float bestHz = tgt, bestA = 0.0f, bestPr = 0.0f, bestWO = 1.0f;
                        float bestS = 0.0f, bestRP = 0.0f, bestC = 42.0f;
                        for (int q = 0; q < nObs; ++q) {
                            const float cd = centsDist(obsHz[q], tgt);
                            if (cd < 42.0f && obsY[q] > bestA) {
                                bestA = obsY[q];
                                bestPr = obsPre[q];
                                bestWO = obsWOct[q];
                                bestHz = obsHz[q];
                                bestS = obsSnr[q];
                                bestRP = obsRawP[q];
                                bestC = cd;
                            }
                        }
                        keyHz[ki] = bestHz;
                        keyObs[ki] = bestA;
                        keyPre[ki] = bestPr;
                        keyWO[ki] = bestWO;
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
                        bool hasH2 = false;
                        for (int h = 1; h <= NMF_MAX_E; ++h) {
                            const float tgt = f0 * (float)h;
                            if (tgt > 9200.0f) break;
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
                                if (h == 2) hasH2 = true;
                                if (h >= 2 && h <= 4 && bestD <= 22.0f
                                    && obsY[bestP] >= keyObs[ki] * 0.17f && obsSnr[bestP] >= 3.8f)
                                    ++keyPartials[ki];
                                const int e = wLen[ki]++;
                                wIdx[ki][e] = bestP;
                                wHarm[ki][e] = h;
                                wVal[ki][e] = w;
                                obsW += w;
                            }
                            else if (h >= 2 && h <= 4) {
                                // 3.6kHz 以下の可聴主帯域では欠損倍音ペナルティをそのまま保って単峰倍音ゴーストを抑え、
                                // 低音で 2f0 があるのに 3f0 が欠ける場合は下オクターブ相乗り抑制のため重めに計上する。
                                float ma = (k < BASS_END) ? ((h == 3 && hasH2) ? 0.30f : 0.12f) : 0.52f;
                                if (h == 3 && k >= BASS_END) ma *= 2.0f;
                                if (tgt > 3500.0f) {
                                    float roll = (7200.0f - tgt) / 3700.0f;
                                    if (roll < 0.06f) roll = 0.06f;
                                    ma *= roll;
                                }
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
                        if (obsKey[q] < NMF_K1) {
                            int d = obsKey[q] / 12 - 2;
                            if (d < 0) d = 0;
                            if (d > 5) d = 5;
                            wdIdx[d][wdLen[d]] = q; wdVal[d][wdLen[d]] = 1.0f; ++wdLen[d];
                            colSumD[d] += 1.0f; ++octCnt2[d];
                        }
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

                    auto peakAtHz = [&](float tgt, float tol) -> float {
                        float ba = 0.0f;
                        for (int q = 0; q < nObs; ++q)
                            if (centsDist(obsHz[q], tgt) < tol && obsY[q] > ba) ba = obsY[q];
                        return ba;
                    };

                    // 46ms短窓×16msホップ(66%重なり)では1ホップ比だと実アタックが1.6倍程度に潰れるため、
                    // 上昇80ms・下降30msの非対称基底包絡 onsetSlow と比較して真の新規発音だけを検出する
                    float prevSlowOnset[COUNT];
                    memcpy(prevSlowOnset, tp.onsetSlow, sizeof(prevSlowOnset));
                    if (p.onset) {
                        const float aUp = 1.0f - expf(-dtMs / 80.0f);
                        const float aDn = 1.0f - expf(-dtMs / 30.0f);
                        for (int k = 0; k < COUNT; ++k) {
                            const float cur = p.onset[k];
                            const float a = (cur >= tp.onsetSlow[k]) ? aUp : aDn;
                            tp.onsetSlow[k] = tp.onsetSlow[k] * (1.0f - a) + cur * a;
                        }
                    }

                    const bool broadbandHit = IsBroadbandTransient(p.onset, p.prevOnset, 1.0f);
                    bool indepVoice[NMF_NK] = {};
                    bool isCluster[NMF_NK] = {};
                    // C5 以上の候補の独立声部検証(Pass-1 前段で判定し、真の上声をゼロ吸着させない):
                    // 空隣ビンによる偽突出(cL=cR=0)を親の標準減衰床で塞ぎ、2f0>f0 の自然な楽器も 3f0(sibR) で識別する。
                    for (int ki = MID_END - NMF_K0; ki < NMF_NK; ++ki) {
                        const int k = NMF_K0 + ki;
                        const float aHi = keyObs[ki];
                        if (aHi <= 0.02f) {
                            tp.indepHold[k] = 0.0f;
                            tp.indepPeak[k] = 0.0f;
                            tp.indepStreak[k] = 0;
                            continue;
                        }
                        const float shL = (ki > 0) ? keyObs[ki - 1] : 0.0f;
                        const float shR = (ki + 1 < NMF_NK) ? keyObs[ki + 1] : 0.0f;
                        if (k >= O5_HI && keyPartials[ki] < 1) {
                            int dense = 0;
                            for (int j = ki - 6; j <= ki + 6; ++j)
                                if (j >= MID_END - NMF_K0 && j < NMF_NK && j != ki && keyObs[j] >= 0.28f)
                                    ++dense;
                            if ((shL >= aHi * 0.54f || shR >= aHi * 0.54f) && dense >= 3)
                                isCluster[ki] = true;
                        }
                        if (isCluster[ki]) {
                            tp.indepHold[k] = 0.0f;
                            tp.indepPeak[k] = 0.0f;
                            tp.indepStreak[k] = 0;
                            continue;
                        }

                        const float fHi = keyHz[ki];
                        const float hi2 = (fHi * 2.0f <= 9000.0f) ? peakAtHz(fHi * 2.0f, 35.0f) : 0.0f;
                        const float hi3 = (fHi * 3.0f <= 9000.0f) ? peakAtHz(fHi * 3.0f, 36.0f) : 0.0f;
                        const float nfAmp = (floorPow[k] > 0.0f) ? sqrtf(floorPow[k]) : 0.0f;

                        bool hasHarmParent = false;
                        for (int lj = 0; lj < ki; ++lj) {
                            if (keyObs[lj] < 0.07f) continue;
                            const float fLo = keyHz[lj];
                            const int n = (int)floorf(fHi / fLo + 0.5f);
                            if (n < 2 || n > 12) continue;
                            const float pTol = (n >= 5) ? 24.0f : 33.0f;
                            if (centsDist(fHi, fLo * (float)n) <= pTol) {
                                hasHarmParent = true;
                                break;
                            }
                        }

                        const bool sharpSine = (!hasHarmParent && k <= 88 && !broadbandHit && p.percussive < 0.28f
                            && aHi >= 0.24f && keySnr[ki] >= 7.5f && keyCents[ki] <= 15.0f
                            && aHi >= shL * 1.36f && aHi >= shR * 1.36f);
                        const bool tonalQual = (keyPartials[ki] >= 1 || sharpSine);

                        // 短窓(46ms)の倍音が長窓(185ms/93ms)の低音基音より数フレーム先行して立ち上がる現象を防ぐため、
                        // 下位オクターブ/完全5度下(2f0/3f0/4f0の親候補)の46msオンセットが同時に急上昇している場合は先行ジャンプを抑える。
                        bool lowerParentRising = false;
                        if (!hasHarmParent && p.onset) {
                            static const int kOffs[3] = { 12, 19, 24 };
                            for (int oi = 0; oi < 3; ++oi) {
                                const int lk = k - kOffs[oi];
                                if (lk < NMF_K0) continue;
                                const float lNf = (floorPow[lk] > 0.0f) ? sqrtf(floorPow[lk]) : 0.0f;
                                if (p.onset[lk] > prevSlowOnset[lk] * 1.55f && p.onset[lk] > lNf * 3.0f) {
                                    lowerParentRising = true;
                                    break;
                                }
                            }
                        }

                        const bool hasOnsetJump = (!broadbandHit && !lowerParentRising && p.onset
                            && aHi >= 0.22f && keySnr[ki] >= 6.5f
                            && p.onset[k] > prevSlowOnset[k] * 2.05f
                            && p.onset[k] > nfAmp * 3.8f);

                        bool blockedByParent = false;
                        bool inParentComb = false;
                        float pcComb2[12] = {};
                        if (k < O5_HI) {
                            for (int lj = 0; lj < ki; ++lj) {
                                if (keyObs[lj] < 0.08f) continue;
                                const int loKey = NMF_K0 + lj;
                                if (loKey >= MID_END) continue;
                                const float fLo = keyHz[lj];
                                const int n = (int)floorf(fHi / fLo + 0.5f);
                                if (n < 2 || n > 8) continue;
                                if (centsDist(fHi, fLo * (float)n) > 30.0f) continue;
                                const int pc = loKey % 12;
                                const float c2L = peakAtHz(fLo * (float)(2 * n - 1), 38.0f);
                                const float c2R = (fLo * (float)(2 * n + 1) <= 9000.0f)
                                    ? peakAtHz(fLo * (float)(2 * n + 1), 38.0f) : c2L;
                                const float m2 = (c2L > c2R) ? c2L : c2R;
                                if (m2 > pcComb2[pc]) pcComb2[pc] = m2;
                            }
                        }

                        const float minPartY = (k >= O5_HI) ? 0.055f : 0.095f;
                        const float c12Hi = keyCents[ki];
                        for (int lj = 0; lj < ki; ++lj) {
                            if (keyObs[lj] < 0.07f) continue;
                            const int loKey = NMF_K0 + lj;
                            // C5以上の親候補はオクターブホワイトニング前(keyPre)で比較し、
                            // 実音のC6オクターブ重ね(keyPre[C6]≒keyPre[C5])は潰さず、弱い2f0ゴースト(keyPre[C6]<keyPre[C5])だけ捕捉する
                            if (loKey >= MID_END && keyPre[lj] < keyPre[ki] * 1.05f) continue;
                            const float fLo = keyHz[lj];
                            const int n = (int)floorf(fHi / fLo + 0.5f);
                            if (n < 2 || n > 12) continue;
                            const float pTol = (n >= 5) ? 24.0f : 33.0f;
                            const float cJ = centsDist(fHi, fLo * (float)n);
                            if (cJ > pTol) continue;

                            // 純正律デチューニング(5/7/10/11次倍音)が平均律鍵中心より厳密に倍音位置へ張り付いている場合は即座に親倍音と判定
                            if ((n == 5 || n == 7 || n == 10 || n == 11)
                                && c12Hi >= 9.0f && cJ <= 14.0f && cJ + 5.0f < c12Hi) {
                                inParentComb = true;
                                blockedByParent = true;
                                break;
                            }

                            float maxOtherPc2 = 0.0f;
                            if (k < O5_HI) {
                                const int myPc = loKey % 12;
                                for (int pc = 0; pc < 12; ++pc)
                                    if (pc != myPc && pcComb2[pc] > maxOtherPc2) maxOtherPc2 = pcComb2[pc];
                            }

                            const float sibL = peakAtHz(fLo * (float)(n - 1), 38.0f);
                            const float sibR = (fLo * (float)(n + 1) <= 9000.0f)
                                ? peakAtHz(fLo * (float)(n + 1), 38.0f) : sibL;
                            const float sibMax = (sibL > sibR) ? sibL : sibR;

                            // 親の標準減衰床(obsY=net^0.72 空間での減衰と3kHz超の高域ロールオフに加え、
                            // 2f0/3f0が基音より強い楽器では両隣実測倍音の最小側 sibMin から自然減衰床を補い、
                            // 密集和音の他声部による誤遮蔽を避けつつ単音の強い倍音コムだけを抑える)
                            const float f2Hz = fHi * 2.0f;
                            const float f3Hz = fHi * 3.0f;
                            const float roll2 = (f2Hz > 3000.0f) ? (std::max)(0.45f, 1.0f - (f2Hz - 3000.0f) / 10000.0f) : 1.0f;
                            const float roll3 = (f3Hz > 3000.0f) ? (std::max)(0.45f, 1.0f - (f3Hz - 3000.0f) / 10000.0f) : 1.0f;
                            const float sibMin = (sibL < sibR) ? sibL : sibR;
                            const float sibEnv = (n == 2) ? (sibR * 0.60f) : sibMin;
                            const float env2N = sibEnv * powf((float)(2 * n) / (float)(n + 1), -0.92f) * roll2;
                            const float env3N = sibEnv * powf((float)(3 * n) / (float)(n + 1), -0.92f) * roll3;
                            float floor2N = keyObs[lj] * powf((float)(2 * n), -0.92f) * roll2;
                            float floor3N = keyObs[lj] * powf((float)(3 * n), -0.92f) * roll3;
                            if (env2N > floor2N) floor2N = env2N;
                            if (env3N > floor3N) floor3N = env3N;

                            bool ownPart = false;
                            if (hi3 >= aHi * 0.25f && hi3 >= minPartY) {
                                const float cL = peakAtHz(fLo * (float)(3 * n - 1), 38.0f);
                                const float cR = (fLo * (float)(3 * n + 1) <= 9000.0f)
                                    ? peakAtHz(fLo * (float)(3 * n + 1), 38.0f) : cL;
                                const float cm = (cL > cR) ? cL : cR;
                                const float b3 = (cm > floor3N) ? cm : floor3N;
                                if (hi3 > b3 * 1.44f) ownPart = true;
                            }
                            if (!ownPart && hi2 >= aHi * 0.25f && hi2 >= minPartY) {
                                const float cL = peakAtHz(fLo * (float)(2 * n - 1), 38.0f);
                                const float cR = (fLo * (float)(2 * n + 1) <= 9000.0f)
                                    ? peakAtHz(fLo * (float)(2 * n + 1), 38.0f) : cL;
                                if (n == 2 && maxOtherPc2 < 0.08f) {
                                    const float b2 = (cR > floor2N) ? cR : floor2N;
                                    if (hi2 > cL * 1.18f && hi2 > b2 * 1.40f) ownPart = true;
                                }
                                else {
                                    const float cmRaw = ((cL > cR) ? cL : cR) + 0.55f * maxOtherPc2;
                                    const float b2 = (cmRaw > floor2N) ? cmRaw : floor2N;
                                    if (hi2 > b2 * 1.46f) ownPart = true;
                                }
                            }
                            if (ownPart) continue;

                            const float j7 = (fLo * 7.0f <= 9000.0f) ? peakAtHz(fLo * 7.0f, 20.0f) : 0.0f;
                            const float j11 = (fLo * 11.0f <= 9000.0f) ? peakAtHz(fLo * 11.0f, 22.0f) : 0.0f;
                            const float j13 = (fLo * 13.0f <= 9000.0f) ? peakAtHz(fLo * 13.0f, 22.0f) : 0.0f;
                            float justComb = (j7 > j11) ? j7 : j11;
                            if (j13 > justComb) justComb = j13;

                            if (n >= 5 && n != 6 && n != 8) {
                                if (sibL >= aHi * 0.24f && sibR >= aHi * 0.24f) {
                                    inParentComb = true;
                                    blockedByParent = true;
                                    break;
                                }
                            }
                            if (justComb >= aHi * 0.35f && n >= 4) {
                                inParentComb = true;
                                blockedByParent = true;
                                break;
                            }

                            const bool diffAttack = (hasOnsetJump && keyPartials[ki] >= 1
                                && p.onset[loKey] <= prevSlowOnset[loKey] * 1.28f + 1.0e-6f);
                            if (diffAttack) continue;

                            bool darkParentLead = false;
                            if (keyPartials[ki] >= 1) {
                                if (n == 2) {
                                    // 2f0>f0 の自然な楽器(弦/ピアノ/ハープ等)では 3f0(sibR) も強く立つ。
                                    darkParentLead = (aHi >= keyObs[lj] * 1.15f && sibR < aHi * 0.36f
                                        && justComb < aHi * 0.24f);
                                }
                                else if (n == 3) {
                                    darkParentLead = (aHi >= keyObs[lj] * 1.12f && aHi > sibMax * 1.40f
                                        && justComb < aHi * 0.24f);
                                }
                                else if (n == 4 || n == 6 || n == 8) {
                                    const float natPred = keyObs[lj] * powf((float)n, -0.75f) * 1.35f;
                                    darkParentLead = (aHi >= natPred && aHi > sibMax * 1.35f
                                        && aHi > justComb * 2.20f && aHi >= 0.24f);
                                }
                            }
                            if (darkParentLead) continue;

                            blockedByParent = true;
                            break;
                        }

                        if (tonalQual && !blockedByParent) {
                            ++tp.indepStreak[k];
                            if (tp.indepStreak[k] >= 2 || hasOnsetJump) {
                                tp.indepHold[k] = 1.0f;
                                if (aHi > tp.indepPeak[k]) tp.indepPeak[k] = aHi;
                                else tp.indepPeak[k] = tp.indepPeak[k] * 0.94f + aHi * 0.06f;
                                if (wLen[ki] == 1 && wHarm[ki][0] == 1 && wVal[ki][0] < 0.88f) {
                                    wVal[ki][0] = 0.88f;
                                    wMiss[ki] = 0.12f;
                                }
                            }
                        }
                        else {
                            tp.indepStreak[k] = 0;
                            if (inParentComb) {
                                tp.indepHold[k] *= 0.25f;
                            }
                            else if (tp.indepHold[k] > 0.25f && aHi >= tp.indepPeak[k] * 0.42f && aHi >= 0.21f) {
                                // 親が明確にブロックしている間は短窓先行ゴーストの居座りを断つため速やかに減衰させる
                                const float hTau = blockedByParent ? 62.0f : 170.0f;
                                tp.indepHold[k] *= expf(-dtMs / hTau);
                            }
                            else {
                                tp.indepHold[k] = 0.0f;
                                tp.indepPeak[k] = 0.0f;
                            }
                        }
                        indepVoice[ki] = (tp.indepHold[k] >= 0.25f);
                        if (p.ownVoice) p.ownVoice[k] = indepVoice[ki];
                    }

                    // 中音域の非倍音・多倍音裏付けあり・高SNR・音程正確な内声和音(cleanMid)を判定する。
                    // 静穏〜中密度フレームでは倍音ゴースト用の強い非凸L0ペナルティを和らげて和音内声のゼロ崩壊を防ぐ。
                    bool cleanMid[NMF_NK] = {};
                    const float prevBusy = tp.busySmooth;
                    for (int ki = BASS_END - NMF_K0; ki < MID_END - NMF_K0; ++ki) {
                        if (keyObs[ki] <= 0.03f || keyPartials[ki] < 1 || keySnr[ki] < 8.5f || keyCents[ki] > 16.0f)
                            continue;
                        if (prevBusy >= 0.48f && keyPartials[ki] < 2) continue;
                        const float fCur = keyHz[ki];
                        bool isHarmOfLower = false;
                        for (int lj = 0; lj < ki; ++lj) {
                            if (keyObs[lj] < 0.08f) continue;
                            const float fLo = keyHz[lj];
                            const int n = (int)floorf(fCur / fLo + 0.5f);
                            if (n < 2 || n > 8) continue;
                            if (centsDist(fCur, fLo * (float)n) <= 34.0f) {
                                isHarmOfLower = true;
                                break;
                            }
                        }
                        if (!isHarmOfLower) cleanMid[ki] = true;
                    }

                    float hAct[NMF_NK] = {};
                    float uAct[NMF_ND] = {};
                    for (int ki = 0; ki < NMF_NK; ++ki) if (colSum[ki] > 0.0f) hAct[ki] = keyObs[ki];
                    for (int d = 0; d < NMF_ND; ++d) if (colSumD[d] > 0.0f) uAct[d] = 0.050f;

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
                                const int k = NMF_K0 + ki;
                                float num = 0.0f;
                                for (int e = 0; e < wLen[ki]; ++e)
                                    num += wVal[ki][e] * (obsY[wIdx[ki][e]] / est[wIdx[ki][e]]);
                                const bool gentleHi = (k >= MID_END && indepVoice[ki] && !isCluster[ki]);
                                float pen;
                                if (gentleHi)
                                    pen = 0.042f / (hAct[ki] + 0.220f);
                                else if (cleanMid[ki])
                                    pen = 0.065f / (hAct[ki] + 0.120f);
                                else if (k < BASS_END)
                                    pen = 0.105f / (hAct[ki] + 0.068f);
                                else
                                    pen = 0.115f / (hAct[ki] + 0.062f);
                                if (isCluster[ki]) pen *= 1.35f;
                                if (keyCents[ki] > 16.0f) {
                                    const float ex = (keyCents[ki] - 16.0f) / 26.0f;
                                    pen *= (1.0f + 1.30f * ex * ex);
                                }
                                if (prevActive && prevActive[k]) pen *= 0.78f;
                                else if (p.percussive > 0.35f) pen *= (1.0f + 0.55f * p.percussive);
                                hAct[ki] *= num / (colSum[ki] + pen);
                            }
                            for (int d = 0; d < NMF_ND; ++d) {
                                if (uAct[d] <= 1.0e-6f || colSumD[d] <= 0.0f) continue;
                                float num = 0.0f;
                                for (int e = 0; e < wdLen[d]; ++e)
                                    num += wdVal[d][e] * (obsY[wdIdx[d][e]] / est[wdIdx[d][e]]);
                                float ld = 0.21f;
                                if (d < 4) {
                                    if (octCnt2[d] >= 4) ld *= 0.70f;
                                }
                                else if (d < 6) {
                                    if (octCnt2[d] <= 6) ld = 0.42f;
                                    else if (octCnt2[d] == 7) ld = 0.28f;
                                    else ld = 0.17f;
                                }
                                // 混雑・激しいフレームでは広帯域ノイズ辞書(uAct)が背景雑音をより多く吸収するよう連続適応する
                                ld *= (1.0f - 0.24f * tp.busySmooth);
                                if (p.percussive > 0.35f) {
                                    const float pm = 1.0f - 0.35f * p.percussive;
                                    ld *= (pm > 0.45f) ? pm : 0.45f;
                                }
                                uAct[d] *= num / (colSumD[d] + ld);
                            }
                        }
                    };

                    runNmfIters(18);

                    // 楽曲の静穏〜激しさ(スペクトル混雑度・広帯域雑音・打撃度)を連続推定し、
                    // 静かな曲では繊細な内声和音を拾い、激しい曲では単峰打撃・広帯域ノイズを抑える。
                    {
                        float peakDens = ((float)nObs - 26.0f) / 58.0f;
                        if (peakDens < 0.0f) peakDens = 0.0f;
                        if (peakDens > 1.0f) peakDens = 1.0f;
                        float uOctSum = 0.0f;
                        for (int d = 0; d < 6; ++d) uOctSum += uAct[d];
                        float noiseDens = (uAct[6] + 0.16f * uOctSum - 0.12f) / 0.42f + p.percussive * 0.45f;
                        if (noiseDens < 0.0f) noiseDens = 0.0f;
                        if (noiseDens > 1.0f) noiseDens = 1.0f;
                        const float busyInst = 0.50f * peakDens + 0.50f * noiseDens;
                        const float aB = 1.0f - expf(-dtMs / 100.0f);
                        tp.busySmooth = tp.busySmooth * (1.0f - aB) + busyInst * aB;
                    }
                    const float busy = tp.busySmooth;

                    // 第2パス: 中低音(k<72)の倍音減衰スロープをロバスト中央値で適応し、
                    // 歪みギター・ブラス・ピアノの全倍音コムを基音へ吸収する。
                    // ただし物理的に独立と検証された上声(indepVoice)の位置だけは標準減衰で止め、メロディ吸収を防ぐ。
                    float h1Max = 0.0f;
                    for (int ki = 0; ki < NMF_NK; ++ki)
                        if (hAct[ki] > h1Max) h1Max = hAct[ki];
                    if (h1Max > 1.0e-5f) {
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            const int k = NMF_K0 + ki;
                            if (k >= MID_END || hAct[ki] < h1Max * 0.11f || wLen[ki] < 3 || keyObs[ki] <= 0.03f)
                                continue;
                            float slopes[NMF_MAX_E];
                            int ns = 0;
                            for (int e = 0; e < wLen[ki]; ++e) {
                                const int h = wHarm[ki][e];
                                if (h < 2 || h > NMF_MAX_E) continue;
                                const int hk = obsKey[wIdx[ki][e]] - NMF_K0;
                                if (hk >= MID_END - NMF_K0 && hk < NMF_NK && indepVoice[hk]) continue;
                                const float rat = obsY[wIdx[ki][e]] / keyObs[ki];
                                if (rat <= 0.02f) continue;
                                float sl = -logf(rat) / logf((float)h);
                                if (sl < 0.06f) sl = 0.06f;
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
                                if (h == 1) {
                                    newW[e] = stdW;
                                }
                                else {
                                    float fitW = powf((float)h, -fitSlope);
                                    const int hk = obsKey[wIdx[ki][e]] - NMF_K0;
                                    if (hk >= MID_END - NMF_K0 && hk < NMF_NK && indepVoice[hk] && fitW > stdW)
                                        fitW = stdW;
                                    newW[e] = 0.15f * stdW + 0.85f * fitW;
                                }
                                obsSum += newW[e];
                            }
                            const float keepObs = 1.0f - wMiss[ki];
                            if (obsSum > 1.0e-6f && keepObs > 0.05f) {
                                const float sc = keepObs / obsSum;
                                for (int e = 0; e < wLen[ki]; ++e) {
                                    wVal[ki][e] = newW[e] * sc;
                                    // 物理検証済み独立上声の基音ビンでは、複数低音オクターブの重畳主張(遮蔽)を抑える
                                    if (wHarm[ki][e] >= 2) {
                                        const int hk = obsKey[wIdx[ki][e]] - NMF_K0;
                                        if (hk >= MID_END - NMF_K0 && hk < NMF_NK && indepVoice[hk])
                                            wVal[ki][e] *= 0.38f;
                                    }
                                }
                            }
                        }
                        for (int ki = MID_END - NMF_K0; ki < NMF_NK; ++ki) {
                            if (colSum[ki] > 0.0f && indepVoice[ki] && !isCluster[ki] && hAct[ki] < keyObs[ki] * 0.38f)
                                hAct[ki] = keyObs[ki] * 0.38f;
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
                    float eAct[NMF_NK] = {};
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        if (hAct[ki] <= 1.0e-6f) continue;
                        for (int e = 0; e < wLen[ki]; ++e) {
                            if (wHarm[ki][e] <= 2) {
                                const float s = (hAct[ki] * wVal[ki][e]) / est[wIdx[ki][e]];
                                if (s > shareVal[ki]) shareVal[ki] = s;
                            }
                        }
                        // 中低音は多倍音選択性を完全に維持し、独立検証された高声のみ高域ロールオフ分を連続補正する
                        const int k = NMF_K0 + ki;
                        float hfGain = 1.0f;
                        if (k >= MID_END && indepVoice[ki] && !isCluster[ki]) {
                            float t = (float)(k - 70) / 20.0f;
                            if (t > 1.0f) t = 1.0f;
                            hfGain = 1.0f + 0.42f * t;
                        }
                        eAct[ki] = hAct[ki] * hfGain;
                    }

                    // 帯域別の非対称時間積分(低音ブームによる中高音の包絡ポンピングを防ぐため正規化基準を分離する)
                    float rawBassMax = 0.0f, rawUpperMax = 0.0f;
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        const int k = NMF_K0 + ki;
                        if (k < BASS_END) {
                            if (eAct[ki] > rawBassMax) rawBassMax = eAct[ki];
                        }
                        else {
                            if (eAct[ki] > rawUpperMax) rawUpperMax = eAct[ki];
                        }
                    }
                    const float refBassRaw = (rawBassMax > rawUpperMax * 0.72f) ? rawBassMax : (rawUpperMax * 0.72f);
                    const float refUpperRaw = (rawUpperMax > rawBassMax * 0.72f) ? rawUpperMax : (rawBassMax * 0.72f);
                    float hUse[NMF_NK] = {};
                    float shUse[NMF_NK] = {};
                    const float aSh = 1.0f - expf(-dtMs / 65.0f);
                    for (int ki = 0; ki < NMF_NK; ++ki) {
                        const int k = NMF_K0 + ki;
                        const float hRawRef = (k < BASS_END) ? refBassRaw : refUpperRaw;
                        float v = (hRawRef > 1.0e-5f) ? (eAct[ki] / hRawRef) : 0.0f;
                        if (v > 1.25f) v = 1.25f;
                        const float tu = (k < BASS_END) ? 75.0f : ((k < MID_END) ? 58.0f : ((k < O5_HI) ? 34.0f : 24.0f));
                        const float td = (k < BASS_END) ? 48.0f : ((k < MID_END) ? 40.0f : 42.0f);
                        const float a = (v >= tp.hSmooth[k]) ? (1.0f - expf(-dtMs / tu)) : (1.0f - expf(-dtMs / td));
                        tp.hSmooth[k] = tp.hSmooth[k] * (1.0f - a) + v * a;
                        hUse[ki] = tp.hSmooth[k];

                        if (eAct[ki] > 1.0e-5f)
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
                    const float hUpperMax = (bMaxH[1] > bMaxH[2]) ? bMaxH[1] : bMaxH[2];
                    const float hRefBass = (bMaxH[0] > hUpperMax * 0.72f) ? bMaxH[0] : (hUpperMax * 0.72f);
                    const float hRefUpper = (hUpperMax > bMaxH[0] * 0.72f) ? hUpperMax : (bMaxH[0] * 0.72f);

                    if (hMax > 1.0e-5f) {
                        for (int ki = 0; ki < NMF_NK; ++ki) {
                            const int k = NMF_K0 + ki;
                            if (keyObs[ki] < 0.018f && hUse[ki] < hMax * 0.22f) continue;
                            // 低域の無倍音キックドラム・低域単発ランブル抑止(激しい曲では単峰SNR要件を引き上げ、静かな曲の正弦波ベースは保つ)
                            if (k < BASS_END && keyPartials[ki] < 1 && wLen[ki] <= 1
                                && !(prevActive && prevActive[k])
                                && (p.percussive > 0.20f || keySnr[ki] < (13.0f + 13.0f * busy) || keyCents[ki] > 20.0f))
                                continue;
                            // 超高域の金属共鳴・打楽器単峰ノイズ・広帯域シンバルクラスタの誤点灯を防ぐ
                            // (9.2kHzまでFFTを拡張したため D#6 以上の実音は2次倍音が観測でき、無倍音単峰は高SNR非打楽器に限る)
                            if ((isCluster[ki] && relShortN(k) < 0.22f)
                                || (k >= 87 && keyPartials[ki] < 1 && ((keySnr[ki] < 9.5f && relShortN(k) < 0.16f) || p.percussive > 0.36f))
                                || (k >= 96 && keyPartials[ki] < 1 && keySnr[ki] < 12.0f))
                                continue;
                            const int b = (k < BASS_END) ? 0 : ((k < MID_END) ? 1 : 2);
                            const float hRef = (b == 0) ? hRefBass : hRefUpper;
                            const float hN = (hRef > 1.0e-5f) ? (hUse[ki] / hRef) : 0.0f;
                            float th = (b == 0) ? 0.135f : ((b == 1) ? 0.218f : 0.172f);
                            // 中音域で下位倍音と重ならない独立和音構成音は、静かな曲(低busy)ほど繊細に抽出し激しい曲(高busy)では締める
                            if (b == 1) {
                                if (shareVal[ki] >= 0.78f && keyPartials[ki] >= 1)
                                    th = 0.138f + 0.062f * busy;
                                else if (shareVal[ki] >= 0.90f && keySnr[ki] >= 10.0f && busy < 0.45f)
                                    th = 0.150f + 0.080f * busy;
                            }
                            if (prevActive && prevActive[k]) th *= 0.70f;
                            else {
                                if (p.percussive > 0.35f) th *= (1.0f + 0.18f * p.percussive);
                                // 混雑・激しいフレームでの無倍音単峰ノイズを全帯域で穏やかに抑える
                                if (keyPartials[ki] < 1 && !(k >= MID_END && indepVoice[ki]))
                                    th *= (1.0f + 0.24f * busy);
                            }
                            const bool allowSubTh = (b < 2 || indepVoice[ki]);
                            const bool mainOk = (hN >= th);
                            const bool bandOk = (allowSubTh && bMaxH[b] > hRef * 0.16f && hUse[ki] >= bMaxH[b] * 0.50f && hN >= th * 0.52f);
                            const bool burstOk = (allowSubTh && relShortN(k) >= 0.15f && hN >= th * 0.52f);
                            if (!(mainOk || bandOk || burstOk)) continue;
                            if (k < BASS_END) {
                                if (!(prevActive && prevActive[k]) && shareVal[ki] < 0.34f) continue;
                            }
                            else {
                                const float shReq = (k < MID_END) ? 0.56f : (indepVoice[ki] ? 0.38f : 0.48f);
                                if (!(prevActive && prevActive[k])) {
                                    if (shareVal[ki] < shReq) continue;
                                }
                                else {
                                    if (shUse[ki] < shReq * 0.58f) continue;
                                }
                            }
                            if (k < MID_END && hN < th * 1.55f && !(prevActive && prevActive[k]) && relShortN(k) < 0.12f
                                && keyPartials[ki] < 1 && keySnr[ki] < 15.0f)
                                continue;
                            if (k >= MID_END && hN < th * 1.35f && !(prevActive && prevActive[k]) && relShortN(k) < 0.10f
                                && keyPartials[ki] < 1 && keySnr[ki] < 8.5f)
                                continue;
                            seriesOn[k] = true;
                            seriesHz[k] = keyHz[ki];
                            seriesA[k] = (blend[k] > keyRawP[ki]) ? blend[k] : keyRawP[ki];
                        }

                        // 隣接半音のマージ漏れを抑制
                        for (int k = NMF_K0; k + 1 < NMF_K1; ++k) {
                            if (seriesOn[k] && seriesOn[k + 1]) {
                                if (hUse[k - NMF_K0] >= hUse[k + 1 - NMF_K0])
                                    seriesOn[k + 1] = false;
                                else
                                    seriesOn[k] = false;
                            }
                        }

                        auto obsAtN = [&](float tgt, float tol) -> float {
                            float ba = 0.0f;
                            for (int q = 0; q < nObs; ++q)
                                if (centsDist(obsHz[q], tgt) < tol && obsY[q] > ba) ba = obsY[q];
                            return ba;
                        };

                        // 下オクターブ・仮想基音のサブハーモニック・ゴーストを除去
                        // 1) 上音の偶数倍音相乗り・完全5度和音の仮想基音錯覚(ホワイトニング前 keyPre で判定)
                        // 2) NMF が基音観測(keyObs)より 2 倍以上過大予測しつつ、倍音エネルギーの大半を
                        //    既に点灯している上位和音(seriesOn[hk])から借用している仮想低音を排除する。
                        for (int lo = MID_END - 1; lo >= NMF_K0; --lo) {
                            if (!seriesOn[lo]) continue;
                            const int li = lo - NMF_K0;
                            const int oct = lo + 12;
                            const float aLo = keyObs[li];
                            const float aOct = (oct < NMF_K1) ? keyObs[oct - NMF_K0] : 0.0f;
                            const float pLo = keyPre[li];
                            const float pOct = (oct < NMF_K1) ? keyPre[oct - NMF_K0] : 0.0f;
                            const float fLo = keyHz[li];
                            const float o3 = obsAtN(fLo * 3.0f, 36.0f);
                            const float o5 = obsAtN(fLo * 5.0f, 38.0f);
                            const bool fifthActive = (lo + 19 < NMF_K1 && seriesOn[lo + 19]);
                            if (oct < NMF_K1 && pOct >= pLo * 0.90f
                                && o3 < aLo * 0.25f
                                && o5 < aLo * 0.25f
                                && hUse[li] < hUse[oct - NMF_K0] * 1.05f)
                                seriesOn[lo] = false;
                            if (seriesOn[lo] && oct < NMF_K1 && seriesOn[oct] && pLo < pOct * 0.30f
                                && (o3 < aOct * 0.26f || (fifthActive && o5 < aOct * 0.22f)))
                                seriesOn[lo] = false;
                            if (seriesOn[lo] && wLen[li] >= 2 && wHarm[li][0] == 1) {
                                const float predF0 = hAct[li] * wVal[li][0];
                                float totHarmE = 0.0f, ownHarmE = 0.0f, maxStolenPre = 0.0f;
                                int nStolen = 0;
                                for (int e = 1; e < wLen[li]; ++e) {
                                    const int h = wHarm[li][e];
                                    if (h < 2 || h > 6) continue;
                                    const int idx = wIdx[li][e];
                                    const int hk = obsKey[idx];
                                    const float ew = wVal[li][e] * obsY[idx];
                                    totHarmE += ew;
                                    if (hk > lo && hk < NMF_K1 && seriesOn[hk]) {
                                        ++nStolen;
                                        if (keyPre[hk - NMF_K0] > maxStolenPre)
                                            maxStolenPre = keyPre[hk - NMF_K0];
                                    }
                                    else {
                                        ownHarmE += ew;
                                    }
                                }
                                if (nStolen >= 1 && totHarmE > 0.0f && maxStolenPre > 0.0f) {
                                    const bool weakOwn = (ownHarmE < totHarmE * 0.35f)
                                        && (aLo < predF0 * 0.48f)
                                        && (pLo < maxStolenPre * 0.38f);
                                    const bool multiStolen = (nStolen >= 2)
                                        && (ownHarmE < totHarmE * 0.30f)
                                        && (aLo < predF0 * 0.62f)
                                        && (pLo < maxStolenPre * 0.32f);
                                    if (weakOwn || multiStolen)
                                        seriesOn[lo] = false;
                                }
                            }
                        }

                        // 純正律デチューニング判定 + 隣接次数コム倍音排除(高次倍音も 9kHz まで観測して独立上声を識別)
                        for (int hi = BASS_END; hi < NMF_K1; ++hi) {
                            if (!seriesOn[hi]) continue;
                            const float fHi = keyHz[hi - NMF_K0];
                            const float aHi = keyObs[hi - NMF_K0];
                            const bool canObs3 = (fHi * 3.0f <= 9000.0f);
                            const bool req3 = (fHi * 3.0f <= 3600.0f);
                            const float hi3 = canObs3 ? obsAtN(fHi * 3.0f, 36.0f) : 0.0f;
                            const float hi2 = (fHi * 2.0f <= 9000.0f) ? obsAtN(fHi * 2.0f, 36.0f) : 0.0f;
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
                                    const float sibR = (fLo * (float)(n + 1) <= 9000.0f)
                                        ? obsAtN(fLo * (float)(n + 1), 38.0f) : sibL;
                                    const bool inComb = (sibL >= aHi * 0.22f && sibR >= aHi * 0.22f);
                                    bool sticksOut = false;
                                    if (canObs3 && hi3 >= aHi * 0.26f) {
                                        float combAt3 = 0.0f;
                                        if (fLo * (float)(3 * n - 1) <= 9000.0f) {
                                            const float cL = obsAtN(fLo * (float)(3 * n - 1), 38.0f);
                                            const float cR = (fLo * (float)(3 * n + 1) <= 9000.0f)
                                                ? obsAtN(fLo * (float)(3 * n + 1), 38.0f) : cL;
                                            combAt3 = (cL > cR) ? cL : cR;
                                        }
                                        if (hi3 > combAt3 * 1.40f) sticksOut = true;
                                    }
                                    if (!sticksOut && fHi * 2.0f <= 9000.0f && hi2 >= aHi * 0.28f) {
                                        const float cL = obsAtN(fLo * (float)(2 * n - 1), 38.0f);
                                        const float cR = (fLo * (float)(2 * n + 1) <= 9000.0f)
                                            ? obsAtN(fLo * (float)(2 * n + 1), 38.0f) : cL;
                                        const float combAt2 = (cL > cR) ? cL : cR;
                                        if (hi2 > combAt2 * 1.45f) sticksOut = true;
                                    }
                                    const bool chordDegIndep = (hi >= MID_END && indepVoice[hi - NMF_K0]
                                        && (n == 3 || n == 4 || n == 6 || n == 8));
                                    if (inComb && !sticksOut && !chordDegIndep) {
                                        if (n >= 5 || hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.65f) {
                                            seriesOn[hi] = false;
                                            break;
                                        }
                                    }
                                }
                                if (!(hi >= MID_END && indepVoice[hi - NMF_K0])) {
                                    if (n == 2 && req3 && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.72f
                                        && obsAtN(fLo * 3.0f, 36.0f) >= keyObs[lo - NMF_K0] * 0.22f) {
                                        // 6f0(hi3)が欠落している場合に加え、5f0/7f0コムに埋もれて突出しない2f0ゴーストも抑止する
                                        const float c5 = obsAtN(fLo * 5.0f, 38.0f);
                                        const float c7 = (fLo * 7.0f <= 9000.0f) ? obsAtN(fLo * 7.0f, 38.0f) : c5;
                                        const float comb6 = (c5 > c7) ? c5 : c7;
                                        const bool buriedInComb = (comb6 >= aHi * 0.18f && hi3 <= comb6 * 1.15f
                                            && keyPre[hi - NMF_K0] < keyPre[lo - NMF_K0] * 0.74f
                                            && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * 0.56f);
                                        if (hi3 < aHi * 0.22f || buriedInComb) {
                                            seriesOn[hi] = false;
                                            break;
                                        }
                                    }
                                    if (n >= 3 && req3 && hi3 < aHi * 0.24f
                                        && hUse[hi - NMF_K0] < hUse[lo - NMF_K0] * ((n == 3) ? 0.54f : 0.75f)) {
                                        seriesOn[hi] = false;
                                        break;
                                    }
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
                                const float h3 = (fHi * 3.0f <= 3800.0f) ? obsAtN(fHi * 3.0f, 34.0f) : aHi;
                                if (!(hi >= MID_END && indepVoice[hi - NMF_K0]) && h3 < aHi * 0.26f && aHi < aLo * 1.15f)
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
        return nPicked;

    }
}
