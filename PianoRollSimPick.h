#pragma once
// sim_pianoroll_pick.py 準拠: 2窓スペクトル(低音16384 / 中高音8192同一) + lobe + pick_band

#include <algorithm>
#include <cmath>
#include <cstring>
#include "PianoKeyTable.h"

namespace PianoRollSimPick
{
    // MIDI 絶対境界（PianoKeyTable.h）。108鍵化後も旧88鍵と同じ音高帯を維持。
    static constexpr int BAND_BASS_END = PianoKey::BASS_BAND_END;  // [0,67) ≒ MIDI 0..66
    static constexpr int BAND_MID_END = PianoKey::MID_BAND_END;    // 中音 [67,94), 高音 [94,COUNT)

    static constexpr float BAND_PICK_REL_BASS = 0.26f;
    static constexpr float BAND_PICK_REL_MID  = 0.26f;
    static constexpr float BAND_PICK_REL_TRE  = 0.26f;
    static constexpr float BAND_PICK_PROM_BASS = 0.14f;
    static constexpr float BAND_PICK_PROM_MID  = 0.14f;
    static constexpr float BAND_PICK_PROM_TRE  = 0.14f;
    // 伴奏に埋もれた弱いベル: 通常閾値よりわずかに緩める（sparseBell 時のみ）
    static constexpr float BAND_PICK_REL_TRE_BELL  = 0.22f;
    static constexpr float BAND_PICK_PROM_TRE_BELL   = 0.11f;
    static constexpr float BAND_PICK_REL_TRE_MELODY  = 0.23f;
    static constexpr float BAND_PICK_PROM_TRE_MELODY = 0.12f;

    inline bool LocalPeakLoose(const float* st, int i, int lo, int hi)
    {
        if (!st || i < lo || i >= hi || st[i] <= 0.0f) return false;
        if (i > lo && st[i - 1] > st[i] * 1.01f) return false;
        if (i + 1 < hi && st[i + 1] > st[i] * 1.01f) return false;
        return true;
    }

    struct TrebleContext {
        bool sparseBell = false;
        bool melodyTreble = false;
        float treMax = 0.0f;
        float midMax = 0.0f;
        int peakCount = 0;
    };

    inline TrebleContext AnalyzeTrebleContext(const float* st, int count)
    {
        TrebleContext ctx;
        if (!st || count <= BAND_MID_END) return ctx;

        const int lo = BAND_MID_END;
        const int hi = count;
        for (int i = BAND_BASS_END; i < BAND_MID_END; ++i)
            if (st[i] > ctx.midMax) ctx.midMax = st[i];
        for (int i = lo; i < hi; ++i)
            if (st[i] > ctx.treMax) ctx.treMax = st[i];

        if (ctx.treMax > 1e-9f) {
            const float minPeak = ctx.treMax * 0.15f;
            for (int i = lo; i < hi; ++i) {
                if (st[i] < minPeak) continue;
                if (LocalPeakLoose(st, i, lo, hi)) ++ctx.peakCount;
            }
        }

        const float ratio = (ctx.treMax > 1e-9f) ? (ctx.midMax / ctx.treMax) : 999.0f;
        // 伴奏に埋もれた弱いベル: 高音が弱く中音が支配的なフレームだけ緩和
        ctx.sparseBell =
            (ctx.treMax < 0.007f && ctx.midMax > 0.005f && ratio > 4.5f) ||
            (ctx.peakCount <= 5 && ctx.treMax < 0.005f && ratio > 7.5f) ||
            (ctx.peakCount <= 4 && ctx.treMax < 0.0015f && ratio > 5.0f);
        ctx.melodyTreble = !ctx.sparseBell && ctx.treMax >= 0.008f;
        return ctx;
    }

    inline bool IsStrongTrebleMelodyPeak(const float* st, int candidate, const TrebleContext& ctx, int count)
    {
        if (!st || !ctx.melodyTreble || candidate < BAND_MID_END || candidate >= count)
            return false;
        const float sc = st[candidate];
        if (sc < ctx.treMax * 0.10f || sc < 0.005f) return false;
        return LocalPeakLoose(st, candidate, BAND_MID_END, count);
    }

    // 強い高音メロディ: 中音帯 subharm は mid が pick されたときだけ効かせる
    inline bool PassesTrebleMelodyPick(const float* st, int candidate, int count,
        const bool* midPicked = nullptr)
    {
        if (!st || candidate < BAND_MID_END || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;

        float harmEnergy = 0.0f;
        for (int n = PianoKey::HARMONIC_N_MIN; n <= 6; ++n) {
            const int hk = PianoKey::HarmonicUpKey(candidate, n);
            if (hk >= 0 && hk < count && hk != candidate)
                harmEnergy += st[hk] * (0.50f / (float)n);
        }
        if (sc < harmEnergy * 0.92f)
            return false;

        const bool treblePeak = LocalPeakLoose(st, candidate, BAND_MID_END, count);
        for (int n = PianoKey::HARMONIC_N_MIN; n <= PianoKey::HARMONIC_N_MAX; ++n) {
            const int lo = PianoKey::HarmonicDownKey(candidate, n);
            if (lo < 0 || lo >= count || lo >= candidate) continue;
            if (lo < BAND_MID_END) {
                if (midPicked && midPicked[lo] && st[lo] >= sc * 0.94f && !treblePeak)
                    return false;
                continue;
            }
            if (st[lo] >= sc * 0.78f)
                return false;
        }
        return true;
    }

    inline bool PassesTrebleFundamentalTest(const float* st, int candidate, int count,
        const bool* midPicked = nullptr)
    {
        if (!st || candidate < BAND_MID_END || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;

        float harmEnergy = 0.0f;
        for (int n = PianoKey::HARMONIC_N_MIN; n <= 6; ++n) {
            const int hk = PianoKey::HarmonicUpKey(candidate, n);
            if (hk >= 0 && hk < count && hk != candidate)
                harmEnergy += st[hk] * (0.50f / (float)n);
        }
        if (sc < harmEnergy * 0.92f)
            return false;

        const bool treblePeak = LocalPeakLoose(st, candidate, BAND_MID_END, count);
        for (int n = PianoKey::HARMONIC_N_MIN; n <= PianoKey::HARMONIC_N_MAX; ++n) {
            const int lo = PianoKey::HarmonicDownKey(candidate, n);
            if (lo < 0 || lo >= count || lo >= candidate) continue;
            if (lo < BAND_MID_END) {
                if (midPicked && midPicked[lo] && st[lo] >= sc * 0.88f && !treblePeak)
                    return false;
                continue;
            }
            if (st[lo] >= sc * 0.78f)
                return false;
        }
        return true;
    }

    // 高音ベル: 伴奏の D4 等(中低音帯)で PassesFundamentalTest が落とされるのを防ぐ（sparseBell 時のみ使用）
    inline bool PassesTrebleBellPick(const float* st, int candidate, int count, float treMax)
    {
        if (!st || candidate < BAND_MID_END || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;

        float harmEnergy = 0.0f;
        for (int n = 2; n <= 4; ++n) {
            const int hk = PianoKey::HarmonicUpKey(candidate, n);
            if (hk >= 0 && hk < count && hk != candidate)
                harmEnergy += st[hk] * (0.45f / (float)n);
        }
        if (sc < harmEnergy * 0.82f)
            return false;

        for (int n = 2; n <= PianoKey::HARMONIC_N_MAX; ++n) {
            const int lo = PianoKey::HarmonicDownKey(candidate, n);
            if (lo < BAND_MID_END || lo >= candidate) continue;
            if (st[lo] >= sc * 0.90f)
                return false;
        }
        return PianoKey::SalienceLooksLikeFundamental(st, candidate, count)
            || (sc >= treMax * 0.12f && sc >= 0.0008f);
    }

    inline bool PassesTreblePick(const float* st, int candidate, int count,
        const TrebleContext& ctx, const bool* midPicked = nullptr)
    {
        if (ctx.sparseBell)
            return PassesTrebleBellPick(st, candidate, count, ctx.treMax);
        if (ctx.melodyTreble || IsStrongTrebleMelodyPeak(st, candidate, ctx, count))
            return PassesTrebleMelodyPick(st, candidate, count, midPicked);
        return PassesTrebleFundamentalTest(st, candidate, count, midPicked);
    }

    inline int CountBandPeaks(const float* st, int lo, int hi, float relToMax)
    {
        if (!st || lo >= hi) return 0;
        const float bandMax = st[lo];
        float maxV = bandMax;
        for (int i = lo + 1; i < hi; ++i)
            if (st[i] > maxV) maxV = st[i];
        if (maxV < 1e-6f) return 0;
        const float minV = maxV * relToMax;
        int n = 0;
        for (int i = lo; i < hi; ++i) {
            if (st[i] < minV) continue;
            if (LocalPeakLoose(st, i, lo, hi)) ++n;
        }
        return n;
    }

    inline void SupplementTrebleBellPeaks(const float* st, const float* shaped, bool* active,
        int count, int sampleRate, int winBass, int winMid, const TrebleContext& ctx,
        float pickScale = 1.0f)
    {
        if (!st || !shaped || !active || count <= 0 || !ctx.sparseBell) return;
        const int lo = BAND_MID_END;
        const int hi = count;
        if (lo >= hi) return;

        if (CountBandPeaks(st, lo, hi, 0.12f) >= 14)
            return;

        float bandMax = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (st[i] > bandMax) bandMax = st[i];
        if (bandMax < 0.002f) return;

        const float minS = bandMax * (0.08f * pickScale);
        for (int i = lo; i < hi; ++i) {
            if (active[i] || st[i] < minS) continue;
            if (!LocalPeakLoose(st, i, lo, hi)) continue;
            if (!PassesTrebleBellPick(st, i, count, ctx.treMax)) continue;
            if (shaped[i] < bandMax * 0.06f && st[i] < bandMax * 0.22f) continue;
            active[i] = true;
        }
    }

    inline void SupplementMelodyTreblePeaks(const float* st, const float* shaped, bool* active,
        int count, const TrebleContext& ctx, float pickScale = 1.0f,
        const bool* midPicked = nullptr)
    {
        if (!st || !shaped || !active || count <= 0 || !ctx.melodyTreble) return;
        const int lo = BAND_MID_END;
        const int hi = count;
        if (lo >= hi) return;

        if (CountBandPeaks(st, lo, hi, 0.12f) >= 10)
            return;

        float bandMax = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (st[i] > bandMax) bandMax = st[i];
        if (bandMax < 0.008f) return;

        const float minS = bandMax * (0.14f * pickScale);
        for (int i = lo; i < hi; ++i) {
            if (active[i] || st[i] < minS) continue;
            if (!IsStrongTrebleMelodyPeak(st, i, ctx, count)) continue;
            if (!PassesTrebleMelodyPick(st, i, count, midPicked)) continue;
            if (shaped[i] < bandMax * 0.08f && st[i] < bandMax * 0.24f) continue;
            active[i] = true;
        }
    }

    inline int WinSamplesForBlur(int keyIndex, int winBass, int winMid, int winTreble)
    {
        if (keyIndex < BAND_BASS_END) return winBass;
        if (keyIndex < PianoKey::TREBLE_WIN_START) return winMid;
        return winTreble;
    }

    inline int BlurSemi(int keyIndex, int sampleRate, int winBass, int winMid, int winTreble)
    {
        const int n = WinSamplesForBlur(keyIndex, winBass, winMid, winTreble);
        const float bw = 2.5f * (float)sampleRate / (float)n;
        const float semi = PianoKey::KeyHz(keyIndex) * 0.0594631f;
        int r = (int)(bw / semi + 0.65f);
        if (r < 1) r = 1;
        if (r > 5) r = 5;
        return r;
    }

    inline bool SharesLobe(int i, int j, int sampleRate, int winBass, int winMid, int winTreble)
    {
        const int d = (i > j) ? (i - j) : (j - i);
        return d <= BlurSemi(i, sampleRate, winBass, winMid, winTreble)
            || d <= BlurSemi(j, sampleRate, winBass, winMid, winTreble);
    }

    inline void ApplyLobeShaping(const float* st, float* shaped, int count, int sampleRate,
        int winBass, int winMid, int winTreble)
    {
        if (!st || !shaped || count <= 0) return;
        memcpy(shaped, st, (size_t)count * sizeof(float));
        for (int i = 0; i < count; ++i) {
            const int r = BlurSemi(i, sampleRate, winBass, winMid, winTreble);
            if (r <= 1) continue;
            const int jl = (i - r < 0) ? 0 : (i - r);
            const int jh = (i + r + 1 >= count) ? count : (i + r + 1);
            float peak = 0.0f;
            for (int j = jl; j < jh; ++j)
                if (st[j] > peak) peak = st[j];
            shaped[i] = (st[i] >= peak * 0.99f) ? peak : (st[i] * 0.05f);
        }
    }

    inline float BandMedianPositive(const float* st, int lo, int hi)
    {
        float tmp[128];
        int n = 0;
        for (int i = lo; i < hi; ++i)
            if (st[i] > 0.0f && n < 128) tmp[n++] = st[i];
        if (n <= 0) return 0.0f;
        std::sort(tmp, tmp + n);
        return tmp[n / 2];
    }

    inline void CollapseLobePicks(const float* st, bool* active, int lo, int hi, int sampleRate,
        int winBass, int winMid, int winTreble)
    {
        if (!st || !active || lo >= hi) return;
        bool keep[128];
        for (int i = 0; i < hi && i < 128; ++i)
            keep[i] = active[i];
        for (int i = lo; i < hi; ++i) {
            if (!active[i]) continue;
            for (int j = i + 1; j < hi; ++j) {
                if (!active[j] || !SharesLobe(i, j, sampleRate, winBass, winMid, winTreble))
                    continue;
                if (PianoKey::IsHarmonicPair(j, i) || PianoKey::IsHarmonicPair(i, j))
                    continue;
                if (st[j] > st[i] * 1.03f)
                    keep[i] = false;
                else if (st[i] >= st[j] * 0.97f)
                    keep[j] = false;
            }
        }
        for (int i = lo; i < hi; ++i)
            active[i] = keep[i];
    }

    inline void PickBand(const float* st, const float* shaped, bool* activeOut,
        int lo, int hi, int count, int sampleRate, int winBass, int winMid, int winTreble,
        float rel = 0.26f, float prom = 0.14f, const TrebleContext* treCtx = nullptr,
        const bool* midPicked = nullptr)
    {
        if (!st || !shaped || !activeOut || lo >= hi || count <= 0) return;

        float fund[128];
        memset(fund, 0, sizeof(fund));

        float bandMax = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (st[i] > bandMax) bandMax = st[i];
        if (bandMax <= 0.0f) return;

        const float med = BandMedianPositive(st, lo, hi);
        const float floorVal = med * 0.76f + 1e-5f;
        const float floorCap = bandMax * 0.28f;
        const float floor = (floorVal < floorCap) ? floorVal : floorCap;

        for (int i = lo; i < hi; ++i) {
            const float ex = (shaped[i] > floor) ? (shaped[i] - floor) : 0.0f;
            float harm = 0.0f;
            for (int n = 2; n <= 6; ++n) {
                const int hk = PianoKey::HarmonicUpKey(i, n);
                if (hk >= 0 && hk < count)
                    harm += shaped[hk] * (0.52f / (float)n);
            }
            fund[i] = (ex > 0.0f) ? (ex * ex / (0.07f + harm)) : 0.0f;
        }

        float work[128];
        for (int i = 0; i < count && i < 128; ++i)
            work[i] = (i >= lo && i < hi) ? fund[i] : 0.0f;

        float bandWorkMax = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (work[i] > bandWorkMax) bandWorkMax = work[i];
        if (bandWorkMax <= 0.0f) return;

        const float minv = bandWorkMax * rel;
        const float minprom = bandWorkMax * prom;

        bool bandActive[128];
        memset(bandActive, 0, sizeof(bandActive));

        for (int round = 0; round < 16; ++round) {
            int best = -1;
            float bs = minv;
            for (int i = lo; i < hi; ++i) {
                if (work[i] < minv) continue;
                float surround = 0.0f;
                for (int j = lo; j < hi; ++j) {
                    if (j == i) continue;
                    if (PianoKey::IsHarmonicPair(i, j) || PianoKey::IsHarmonicPair(j, i))
                        continue;
                    if (work[j] > surround) surround = work[j];
                }
                const float prominence = work[i] - surround;
                if (prominence < minprom) continue;
                const float score = work[i] + prominence * 0.35f;
                if (score > bs) {
                    bs = score;
                    best = i;
                }
            }
            if (best < 0) break;
            const bool trebleBand = (lo >= BAND_MID_END);
            const bool passFund = (trebleBand && treCtx)
                ? PassesTreblePick(st, best, count, *treCtx, midPicked)
                : PianoKey::PassesFundamentalTest(st, best, count);
            if (!passFund ||
                !LocalPeakLoose(st, best, lo, hi)) {
                work[best] = 0.0f;
                continue;
            }
            bandActive[best] = true;
            work[best] = 0.0f;
            const float f0 = PianoKey::KeyHz(best);
            for (int n = 2; n <= 9; ++n) {
                const int up = PianoKey::NearestKeyIndex(f0 * (float)n);
                const int dn = PianoKey::NearestKeyIndex(f0 / (float)n);
                if (up >= 0 && up < count) work[up] *= 0.06f;
                if (dn >= 0 && dn < count) work[dn] *= 0.06f;
            }
        }

        if (!(treCtx && treCtx->sparseBell && lo >= BAND_MID_END))
            CollapseLobePicks(st, bandActive, lo, hi, sampleRate, winBass, winMid, winTreble);
        for (int i = lo; i < hi; ++i)
            if (bandActive[i]) activeOut[i] = true;
    }

    inline void PickAllBands(const float* st, const float* shaped, bool* active, int count,
        int sampleRate, int winBass, int winMid, int winTreble, float pickScale = 1.0f)
    {
        if (!st || !shaped || !active || count <= 0) return;
        const TrebleContext treCtx = AnalyzeTrebleContext(st, count);
        float treRel = BAND_PICK_REL_TRE;
        float treProm = BAND_PICK_PROM_TRE;
        if (treCtx.sparseBell) {
            treRel = BAND_PICK_REL_TRE_BELL;
            treProm = BAND_PICK_PROM_TRE_BELL;
        }
        else if (treCtx.melodyTreble) {
            treRel = BAND_PICK_REL_TRE_MELODY;
            treProm = BAND_PICK_PROM_TRE_MELODY;
        }
        treRel *= pickScale;
        treProm *= pickScale;

        memset(active, 0, (size_t)count * sizeof(bool));
        PickBand(st, shaped, active, 0, BAND_BASS_END, count, sampleRate, winBass, winMid, winTreble,
            BAND_PICK_REL_BASS * pickScale, BAND_PICK_PROM_BASS * pickScale);
        PickBand(st, shaped, active, BAND_BASS_END, BAND_MID_END, count, sampleRate, winBass, winMid, winTreble,
            BAND_PICK_REL_MID * pickScale, BAND_PICK_PROM_MID * pickScale);
        bool midPicked[128];
        memset(midPicked, 0, sizeof(midPicked));
        for (int i = BAND_BASS_END; i < BAND_MID_END && i < count; ++i)
            midPicked[i] = active[i];
        PickBand(st, shaped, active, BAND_MID_END, count, count, sampleRate, winBass, winMid, winTreble,
            treRel, treProm, &treCtx, midPicked);
        // 高音ゴーストの主因だった supplement は無効化（PickBand + 基音テストのみ）
    }

    inline float BandDisplayBoost(int keyIndex)
    {
        if (keyIndex < BAND_BASS_END) return 1.0f;
        if (keyIndex < BAND_MID_END) return 1.04f;
        return 1.0f;
    }

} // namespace PianoRollSimPick
