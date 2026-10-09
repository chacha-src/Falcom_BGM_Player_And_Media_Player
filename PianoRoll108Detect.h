#pragma once
// 108鍵簡易ピアノロール検出
//
// 鳴っている基音だけを返す。
// 倍音は「3度・5度」などの音程ではなく、周波数の整数倍（n*f0）だけ。
// 検出スペクトルは振幅の2乗なので、比較の前に振幅へ戻す。
// 一つの音の部分音は、次数 n に対して振幅が n のべきで減衰する。
// その包絡に乗る山だけをその音の倍音として引き、乗らない山は別の音。
// 和音の5度や3度は整数倍の減衰曲線に乗らないので残る。
// 低域は半音が分析ビンより狭く、1音が2〜3鍵に割れる。分解できない範囲は
// 谷で分かれた音以外を1鍵にまとめる。
//
// 公開 API / 定数名は CPianoRoll.cpp 互換を維持する。
#include <algorithm>
#include <cmath>
#include <cstring>
#include "NoteFundamentalPick.h"
#include "PianoKeyTable.h"
#include "PianoRollPick.h"

namespace PianoRoll108
{
    static constexpr int COUNT = PianoKey::COUNT;

    static constexpr int WIN_LONG_END = 60;
    static constexpr int WIN_MID_END = 84;

    static constexpr int BASS_END = 48;   // C3
    static constexpr int MID_END = 72;    // C5
    static constexpr int C4_KEY = 60;
    static constexpr int LOW_MID_SPLIT = 48;
    static constexpr int O5_LO = 72;      // C5
    static constexpr int O5_HI = 84;      // C6
    static constexpr int EDGE_LO = 12;
    static constexpr int EDGE_HI = 96;    // C7
    // オクターブ0(C0–B0)は 185ms 窓で音程分解不能かつ A0 以下。ここ未満は検出しない。
    static constexpr int MUSIC_LOW_FLOOR = 24; // C1

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

    inline float BandMax(const float* st, int lo, int hi)
    {
        float mx = 0.0f;
        if (!st || lo >= hi) return 0.0f;
        for (int i = lo; i < hi; ++i)
            if (st[i] > mx) mx = st[i];
        return mx;
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
        float oMax = 0.0f;
        for (int i = 0; i < COUNT; ++i)
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

    inline bool OnsetSupportsPickInBand(const float* onset, const float* prevOnset,
        int keyIndex, int bandLo, int bandHi, float levelScale, float onsetDeltaScale = 1.0f)
    {
        if (!onset || !prevOnset || keyIndex < bandLo || keyIndex >= bandHi) return false;
        float oMax = 0.0f;
        for (int i = bandLo; i < bandHi; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.0020f) return false;
        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;
        float od = onsetDeltaScale;
        if (od < 0.25f) od = 0.25f;
        if (od > 4.0f) od = 4.0f;
        const float delta = onset[keyIndex] - prevOnset[keyIndex];
        return onset[keyIndex] >= oMax * 0.16f * scale &&
            delta >= oMax * 0.09f * od;
    }

    inline float AbsFloorForKey(int key, float baseFloor)
    {
        if (key < BASS_END) return baseFloor;
        if (key < C4_KEY) return baseFloor * 0.40f;
        if (key < O5_HI) return baseFloor * 0.16f;
        if (key < EDGE_HI) return baseFloor * 0.28f;
        return baseFloor * 0.50f;
    }

    inline bool IsStrictLocalPeak(const float* st, int i, int lo, int hi)
    {
        if (!st || i < lo || i >= hi) return false;
        const float v = st[i];
        if (v <= 1e-8f) return false;
        if (i > lo && st[i - 1] >= v) return false;
        if (i + 1 < hi && st[i + 1] > v) return false;
        return true;
    }

    // 低音の半音またぎを強度の強い側1本へ強制
    inline void ForceUniqueBassAdjacents(const float* st, bool* picked, int lo, int hi)
    {
        if (!st || !picked) return;
        for (int i = lo; i + 1 < hi; ++i) {
            if (!picked[i] || !picked[i + 1]) continue;
            if (st[i] >= st[i + 1])
                picked[i + 1] = false;
            else
                picked[i] = false;
        }
    }

    inline void MergeBandPicks(bool* dest, const bool* band, int lo, int hi)
    {
        if (!dest || !band) return;
        for (int i = lo; i < hi; ++i) {
            if (band[i])
                dest[i] = true;
        }
    }

    // 検出窓の周波数分解能。低音は Hann 8192、C6 以上は Blackman 4096（CPianoRoll と同一）。
    inline float AnalysisBinHz(int key)
    {
        const float nWin = (key >= WIN_MID_END) ? 4096.0f : 8192.0f;
        return 44100.0f / nWin;
    }

    // 半音 d 個ぶん離れた鍵への Hann 窓の漏れ（中心を 1 とした振幅比）。
    // 低音では半音間隔がビン幅より狭く、1音が 2〜3 鍵にまたがる。さらに ±3 ビン付近の
    // サイドローブが別の局所ピークになり、同じ音がもう 1〜2 鍵点灯する。
    inline float HannLeakAmp(int key, int deltaSemi)
    {
        if (deltaSemi == 0) return 1.0f;
        const float bins = fabsf((float)deltaSemi) * PianoKey::KeyHz(key) * 0.059463094f
            / AnalysisBinHz(key);
        if (bins < 1.0e-3f) return 1.0f;
        const float denom = 1.0f - bins * bins;
        const float sinc = sinf(3.14159265f * bins) / (3.14159265f * bins);
        if (fabsf(denom) < 0.08f)
            return fabsf(sinc) * 0.12f;
        float w = fabsf(sinc / denom);
        if (w > 1.0f) w = 1.0f;
        return w;
    }

    inline int LeakReachSemis(int key)
    {
        const float semi = PianoKey::KeyHz(key) * 0.059463094f;
        const float bins = 6.5f;
        int r = (int)ceilf(bins * AnalysisBinHz(key) / (semi > 1.0f ? semi : 1.0f));
        if (r < 1) r = 1;
        if (r > 8) r = 8;
        return r;
    }

    // 高音ほど絶対床を下げる。旧実装は最高オクターブの床を上げていたため、
    // 弦の弱い高音（基音が小さく、第2倍音は鍵盤外）が先にゼロになった。
    inline float AmpFloorForKey(int key, float powerFloor)
    {
        float rel = 0.20f;
        if (key < BASS_END) rel = 1.0f;
        else if (key < C4_KEY) rel = 0.42f;
        else if (key < O5_HI) rel = 0.16f;
        else if (key < EDGE_HI) rel = 0.09f;
        else rel = 0.055f;
        float p = powerFloor * rel;
        if (p < 1.0e-8f) p = 1.0e-8f;
        return sqrtf(p);
    }

    inline float BandStrict(int key, float pickBassRel, float pickLowMidRel,
        float pickMelodyRel, float pickTreRel)
    {
        float rel = 1.0f;
        if (key < BASS_END) rel = pickBassRel / 0.28f;
        else if (key < C4_KEY) rel = pickLowMidRel / 0.20f;
        else if (key < O5_HI) rel = pickMelodyRel / 0.10f;
        else rel = pickTreRel / 0.22f;
        if (rel < 0.35f) rel = 0.35f;
        if (rel > 3.5f) rel = 3.5f;
        return rel;
    }

    inline void BuildFramePicks(const float* blend, bool* outPicked, int count,
        float levelScale = 1.0f, float absNoiseFloor = 0.00055f,
        const float* onset = nullptr, const float* prevOnset = nullptr,
        float pickBassRel = 0.28f, float pickLowMidRel = 0.20f,
        float pickMelodyRel = 0.10f, float pickTreRel = 0.22f,
        float onsetDeltaScale = 1.0f)
    {
        if (!blend || !outPicked || count != COUNT) return;
        memset(outPicked, 0, (size_t)count * sizeof(bool));

        float scale = levelScale;
        if (scale < 0.55f) scale = 0.55f;
        if (scale > 1.25f) scale = 1.25f;

        float amp[COUNT];
        for (int i = 0; i < count; ++i)
            amp[i] = (blend[i] > 0.0f) ? sqrtf(blend[i]) : 0.0f;

        // 部分音 n=2..8 の対数振幅を Theil-Sen で直線に合わせる。
        // 傾きが負で包絡に乗る山だけが倍音。包絡より明らかに大きく、
        // 奇数次が親の系列に無い山は別の音。平坦な列は和音なので足さない。
        auto sieve = [&](const float* spec, int key, float* pred, bool* inlier,
            float* slopeOut, float* iceptOut) -> float {
            for (int n = 0; n <= 8; ++n) { pred[n] = 0.0f; inlier[n] = false; }
            *slopeOut = 0.0f;
            *iceptOut = 0.0f;
            if (key < MUSIC_LOW_FLOOR || key >= count || spec[key] < 1.0e-8f) return 0.0f;

            float a[9];
            a[1] = spec[key];
            float strongest = a[1];
            for (int n = 2; n <= 8; ++n) {
                const int hk = PianoKey::HarmonicKeyOnBoard(key, n);
                a[n] = (hk >= 0 && hk < count) ? spec[hk] : 0.0f;
                if (a[n] > strongest) strongest = a[n];
            }
            // 基音が無い（最強部分音の 6% 未満）。和音から想像した空の基音は音にしない。
            if (strongest > 1.0e-8f && a[1] < strongest * 0.06f) return 0.0f;
            // 1 割未満は基音として残せるが、上の山は別の音なので点数に足さない。
            const bool fundPresent = a[1] >= strongest * 0.10f;

            float logN[8], logA[8];
            int np = 0;
            if (fundPresent) {
                for (int n = 2; n <= 8; ++n) {
                    if (a[n] < 1.0e-5f) continue;
                    logN[np] = logf((float)n);
                    logA[np] = logf(a[n]);
                    ++np;
                }
            }
            float slope = 0.0f;
            float icept = 0.0f;
            bool fitted = false;
            if (np >= 2) {
                float slopes[32];
                int ns = 0;
                for (int i = 0; i < np; ++i) {
                    for (int j = i + 1; j < np; ++j) {
                        const float dn = logN[j] - logN[i];
                        if (dn < 1.0e-6f) continue;
                        slopes[ns++] = (logA[j] - logA[i]) / dn;
                    }
                }
                for (int i = 1; i < ns; ++i) {
                    const float v = slopes[i];
                    int j = i;
                    while (j > 0 && slopes[j - 1] > v) { slopes[j] = slopes[j - 1]; --j; }
                    slopes[j] = v;
                }
                slope = (ns > 0) ? slopes[ns / 2] : 0.0f;
                // 平坦な部分音列は減衰する一つの音ではない（同音量のオクターブ、和音）。
                if (slope < -0.20f) {
                    float ic[8];
                    for (int i = 0; i < np; ++i)
                        ic[i] = logA[i] - slope * logN[i];
                    for (int i = 1; i < np; ++i) {
                        const float v = ic[i];
                        int j = i;
                        while (j > 0 && ic[j - 1] > v) { ic[j] = ic[j - 1]; --j; }
                        ic[j] = v;
                    }
                    icept = ic[np / 2];
                    fitted = true;
                    *slopeOut = slope;
                    *iceptOut = icept;
                }
            }

            float score = a[1];
            if (fitted) {
                for (int n = 2; n <= 8; ++n) {
                    if (a[n] < 1.0e-5f) continue;
                    pred[n] = expf(icept + slope * logf((float)n));
                    const float hi = a[n] > pred[n] ? a[n] : pred[n];
                    const float lo = a[n] > pred[n] ? pred[n] : a[n];
                    if (lo > 1.0e-8f && hi <= lo * 1.70f) {
                        inlier[n] = true;
                        score += a[n];
                    }
                }
            } else if (fundPresent && np == 1) {
                for (int n = 2; n <= 8; ++n) {
                    if (a[n] < 1.0e-5f) continue;
                    // 1/n 以下は減衰する倍音。第3倍音が鍵盤外で第2倍音だけ大きいのは弦の基音。
                    const bool decay = a[n] <= a[1] / (float)n * 1.20f;
                    const bool highString = (n == 2)
                        && PianoKey::HarmonicKeyOnBoard(key, 3) < 0
                        && a[2] > a[1] * 1.15f;
                    if (decay || highString) {
                        pred[n] = a[n];
                        inlier[n] = true;
                        score += a[n];
                    }
                }
            }
            pred[1] = a[1];
            inlier[1] = true;
            return score;
        };

        float residual[COUNT];
        for (int i = 0; i < count; ++i) residual[i] = amp[i];

        bool emitted[COUNT];
        bool dead[COUNT];
        memset(emitted, 0, sizeof(emitted));
        memset(dead, 0, sizeof(dead));

        for (int round = 0; round < 16; ++round) {
            int best = -1;
            float bestScore = 0.0f;
            float bestPred[9];
            bool bestIn[9];
            float bestSlope = 0.0f;
            float bestIcept = 0.0f;
            for (int i = MUSIC_LOW_FLOOR; i < count; ++i) {
                if (dead[i] || emitted[i]) continue;
                if (i > 0 && residual[i - 1] > residual[i]) continue;
                if (i + 1 < count && residual[i + 1] >= residual[i]) continue;
                const float th = AmpFloorForKey(i, absNoiseFloor) * 2.15f
                    * BandStrict(i, pickBassRel, pickLowMidRel, pickMelodyRel, pickTreRel)
                    / scale;
                if (residual[i] < th) continue;
                float pred[9];
                bool inlier[9];
                float slope = 0.0f, icept = 0.0f;
                const float sc = sieve(residual, i, pred, inlier, &slope, &icept);
                if (sc < th || sc <= bestScore) continue;
                bool leak = false;
                for (int j = MUSIC_LOW_FLOOR; j < count && !leak; ++j) {
                    if (!emitted[j]) continue;
                    const int d = i - j;
                    const int ad = d < 0 ? -d : d;
                    if (ad < 1 || ad > 8) continue;
                    if (amp[i] <= amp[j] * HannLeakAmp(j, d) * 1.35f) leak = true;
                }
                if (leak) continue;
                best = i;
                bestScore = sc;
                bestSlope = slope;
                bestIcept = icept;
                for (int n = 0; n <= 8; ++n) { bestPred[n] = pred[n]; bestIn[n] = inlier[n]; }
            }
            if (best < 0) break;
            emitted[best] = true;

            int nIn = 0;
            for (int n = 2; n <= 8; ++n) if (bestIn[n]) ++nIn;
            const bool series = bestSlope < -0.20f && nIn >= 3;
            for (int n = 1; n <= 24; ++n) {
                const int hk = (n == 1) ? best : PianoKey::HarmonicKeyOnBoard(best, n);
                if (hk < 0 || hk >= count) continue;
                float pred = 0.0f;
                if (n == 1) pred = bestPred[1];
                else if (series)
                    pred = expf(bestIcept + bestSlope * logf((float)n));
                else if (n <= 8 && bestIn[n]) pred = bestPred[n];
                else continue;
                if (pred <= 1.0e-8f) continue;
                const float before = residual[hk];
                const float keep = before * before - pred * pred;
                residual[hk] = (keep > 0.0f) ? sqrtf(keep) : 0.0f;
                // 包絡以下は倍音の谷。少し上回るだけならフォルマント。
                // はっきり上回り、奇数次が親の系列に無いときだけ別の音。
                const bool ownTone = n > 1 && before > pred * 1.08f
                    && PianoKey::OddPartialExceedsParent(amp, hk, best, count);
                if (before <= pred * 1.05f || (!ownTone && before <= pred * 1.35f))
                    dead[hk] = true;
                int reach = 1;
                while (reach < 6 && HannLeakAmp(hk, reach) > 0.22f) ++reach;
                for (int d = -reach; d <= reach; ++d) {
                    if (d == 0) continue;
                    const int s = hk + d;
                    if (s < 0 || s >= count) continue;
                    const float skirt = pred * HannLeakAmp(hk, d);
                    const float sk = residual[s] * residual[s] - skirt * skirt;
                    residual[s] = (sk > 0.0f) ? sqrtf(sk) : 0.0f;
                    if (amp[s] <= skirt * 1.40f)
                        dead[s] = true;
                }
            }
        }

        for (int i = 0; i < count; ++i)
            outPicked[i] = emitted[i] && i >= MUSIC_LOW_FLOOR;

        // 分解できない低域は、谷が無い隣鍵を強い側へまとめる。
        for (int i = MUSIC_LOW_FLOOR; i < BASS_END; ++i) {
            if (!outPicked[i]) continue;
            const float semiHz = PianoKey::KeyHz(i) * 0.059463094f;
            const float binHz = AnalysisBinHz(i);
            if (semiHz > binHz * 1.15f) continue;
            int rad = (int)ceilf(2.2f * binHz / (semiHz > 0.5f ? semiHz : 0.5f));
            if (rad < 2) rad = 2;
            if (rad > 5) rad = 5;
            for (int j = i + 1; j <= i + rad && j < BASS_END; ++j) {
                if (!outPicked[j]) continue;
                float valley = amp[i] < amp[j] ? amp[i] : amp[j];
                for (int k = i + 1; k < j; ++k)
                    if (amp[k] < valley) valley = amp[k];
                const int strong = amp[i] >= amp[j] ? i : j;
                const int weak = strong == i ? j : i;
                const bool separated = (j - i) >= 2
                    && valley < amp[weak] * 0.50f && valley < amp[strong] * 0.50f;
                if (separated && PianoKey::OddPartialExceedsParent(amp, weak, strong, count))
                    continue;
                outPicked[weak] = false;
                if (weak == i) break;
            }
        }

        // 分解できる高さの短2度。峰の片側だけ残ったとき、漏れより大きい隣を戻す。
        for (int j = MUSIC_LOW_FLOOR; j < count; ++j) {
            if (!outPicked[j]) continue;
            if (PianoKey::KeyHz(j) * 0.059463094f < AnalysisBinHz(j) * 1.15f) continue;
            for (int d = -1; d <= 1; d += 2) {
                const int i = j + d;
                if (i < MUSIC_LOW_FLOOR || i >= count || outPicked[i] || dead[i]) continue;
                if (amp[i] < amp[j] * 0.82f) continue;
                if (amp[i] <= amp[j] * HannLeakAmp(j, d) * 1.35f) continue;
                outPicked[i] = true;
            }
        }

        if (onset && prevOnset) {
            for (int i = C4_KEY; i < count; ++i) {
                if (outPicked[i] || dead[i]) continue;
                if (amp[i] < AmpFloorForKey(i, absNoiseFloor) * 1.15f) continue;
                if (!IsStrictLocalPeak(blend, i, (i >= O5_HI) ? O5_HI : C4_KEY, count)) continue;
                bool harm = false;
                for (int j = MUSIC_LOW_FLOOR; j < i; ++j) {
                    if (!outPicked[j]) continue;
                    const int n = PianoKey::ExactHarmonicNumber(i, j, 12);
                    if (n < 2) continue;
                    if (amp[i] <= amp[j] / (float)n * 1.35f &&
                        !PianoKey::OddPartialExceedsParent(amp, i, j, count)) {
                        harm = true;
                        break;
                    }
                }
                if (harm) continue;
                const int blo = (i >= O5_HI) ? O5_HI : C4_KEY;
                if (OnsetSupportsPickInBand(onset, prevOnset, i, blo, count, scale, onsetDeltaScale))
                    outPicked[i] = true;
            }
        }
    }
}
