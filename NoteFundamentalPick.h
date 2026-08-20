#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

inline float ScaleGoertzelAmpFlat(float rawAmp)
{
    if (rawAmp <= 0.00005f) return 0.0f;
    double amp = rawAmp;
    if (amp > 0.0001) {
        const double boost = amp * 50.0;
        amp = boost * boost * 0.002;
        if (amp > 10.0) amp = 10.0;
    } else {
        amp = 0.0;
    }
    return (float)amp;
}

inline float ScaleGoertzelAmp(float rawAmp, int keyIndex, int keyCount)
{
    if (rawAmp <= 0.00005f) return 0.0f;
    double amp = rawAmp;
    amp *= 1.0 + ((double)keyIndex / (double)keyCount) * 3.0;
    return ScaleGoertzelAmpFlat((float)amp);
}

inline void BuildHarmonicSalience(const float* raw, float* salience, int count)
{
    static const int kHarmBelow[] = { 12, 19, 24 };
    for (int i = 0; i < count; ++i) {
        const float f = raw[i];
        if (f <= 0.0f) {
            salience[i] = 0.0f;
            continue;
        }
        float penalty = 1.0f;
        for (int d : kHarmBelow) {
            const int lo = i - d;
            if (lo < 0) continue;
            if (raw[lo] > f * 0.72f)
                penalty *= 0.40f;
        }
        const float h2 = (i + 12 < count) ? raw[i + 12] : 0.0f;
        const float h3 = (i + 19 < count) ? raw[i + 19] : 0.0f;
        salience[i] = f * (f + h2 * 0.48f + h3 * 0.26f) * penalty;
    }
}

inline int PickFundamentalNotes(const float* inStrengths, bool* outActive, int count,
    int maxNotes = 8, float relativeThresh = 0.22f)
{
    memset(outActive, 0, (size_t)count * sizeof(bool));
    if (!inStrengths || count <= 0 || maxNotes <= 0) return 0;

    std::vector<float> salience((size_t)count);
    BuildHarmonicSalience(inStrengths, salience.data(), count);

    float maxS = 0.0f;
    for (int i = 0; i < count; ++i)
        if (salience[i] > maxS) maxS = salience[i];
    if (maxS < 1e-6f) return 0;

    const float minS = maxS * relativeThresh;
    std::vector<float> work = salience;

    // h2,h3,h4,h5,h6,h8。3〜11（3度・5度など和音構成音）は倍音ではないので抑えない。
    static const int kHarmUp[] = {
        12, 19, 24, 28, 31, 36
    };
    static const int kHarmDown[] = { 12, 19, 24, 28, 31, 36 };

    int picked = 0;
    for (int round = 0; round < maxNotes; ++round)
    {
        int best = -1;
        float bestS = minS;
        for (int i = 0; i < count; ++i) {
            if (work[i] > bestS) {
                bestS = work[i];
                best = i;
            }
        }
        if (best < 0) break;

        outActive[best] = true;
        ++picked;
        work[best] = 0.0f;

        for (int h : kHarmUp) {
            const int hi = best + h;
            if (hi < count) work[hi] *= 0.05f;
        }
        for (int h : kHarmDown) {
            const int lo = best - h;
            if (lo >= 0) work[lo] *= 0.08f;
        }
    }
    return picked;
}

// 隣接セミトーンは最強1鍵だけ残す（C / C# / B の同時誤検出を抑制）
inline void RefineToLocalPeaks(const float* strengths, bool* active, int count, int radius = 1)
{
    if (!strengths || !active || count <= 0 || count > 128 || radius < 1) return;

    bool keep[128];
    memcpy(keep, active, (size_t)count * sizeof(bool));
    if (count < 128)
        memset(keep + count, 0, (size_t)(128 - count) * sizeof(bool));

    for (int i = 0; i < count; ++i) {
        if (!active[i]) continue;
        const int lo = (i - radius < 0) ? 0 : (i - radius);
        const int hi = (i + radius >= count) ? (count - 1) : (i + radius);
        for (int j = lo; j <= hi; ++j) {
            if (j == i || !active[j]) continue;
            if (strengths[j] > strengths[i]) {
                keep[i] = false;
                break;
            }
            if (strengths[j] >= strengths[i] * 0.98f && j > i) {
                keep[i] = false;
                break;
            }
        }
    }

    memcpy(active, keep, (size_t)count * sizeof(bool));
}

// 帯域内の最大値基準でピック（低音に埋もれないよう中高音用）
inline int PickFundamentalNotesRange(const float* inStrengths, bool* outActive, int count,
    int bandStart, int bandEnd, int maxNotes, float relativeThresh)
{
    if (!inStrengths || !outActive || count <= 0 || maxNotes <= 0) return 0;
    if (bandStart < 0) bandStart = 0;
    if (bandEnd > count) bandEnd = count;
    if (bandStart >= bandEnd) return 0;

    memset(outActive, 0, (size_t)count * sizeof(bool));

    // 全帯域スペクトルで倍音関係を参照し、候補の出どころだけ帯域内に限定する。
    // 帯域外をゼロにすると下の基音が見えず、倍音だけが独立音として大量に拾われる。
    std::vector<float> salience((size_t)count);
    BuildHarmonicSalience(inStrengths, salience.data(), count);

    float maxS = 0.0f;
    for (int i = bandStart; i < bandEnd; ++i)
        if (salience[i] > maxS) maxS = salience[i];
    if (maxS < 1e-6f) return 0;

    const float minS = maxS * relativeThresh;
    std::vector<float> work = salience;

    // h2,h3,h4,h5,h6,h8。3〜11（3度・5度など和音構成音）は倍音ではないので抑えない。
    static const int kHarmUp[] = {
        12, 19, 24, 28, 31, 36
    };
    static const int kHarmDown[] = { 12, 19, 24, 28, 31, 36 };

    bool bandPick[128];
    memset(bandPick, 0, sizeof(bandPick));
    int picked = 0;
    for (int round = 0; round < maxNotes; ++round) {
        int best = -1;
        float bestS = minS;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (work[i] > bestS) {
                bool isPeak = true;
                if (i > 0 && inStrengths[i - 1] >= inStrengths[i]) isPeak = false;
                if (i + 1 < count && inStrengths[i + 1] > inStrengths[i]) isPeak = false;
                if (isPeak) {
                    bestS = work[i];
                    best = i;
                }
            }
        }
        if (best < 0) break;

        bandPick[best] = true;
        ++picked;
        work[best] = 0.0f;
        for (int h : kHarmUp) {
            const int hi = best + h;
            if (hi < count) work[hi] *= 0.05f;
        }
        for (int h : kHarmDown) {
            const int lo = best - h;
            if (lo >= 0) work[lo] *= 0.08f;
        }
    }

    int merged = 0;
    for (int i = bandStart; i < bandEnd; ++i) {
        if (bandPick[i]) {
            outActive[i] = true;
            ++merged;
        }
    }
    return merged > 0 ? merged : picked;
}

// 帯域内だけ局所ピーク化
inline void RefineToLocalPeaksInBand(const float* strengths, bool* active, int count,
    int bandStart, int bandEnd, int radius = 1)
{
    if (!strengths || !active || bandStart < 0 || bandEnd > count) return;
    bool bandActive[128];
    memset(bandActive, 0, sizeof(bandActive));
    for (int i = bandStart; i < bandEnd; ++i)
        bandActive[i] = active[i];
    RefineToLocalPeaks(strengths, bandActive, count, radius);
    for (int i = bandStart; i < bandEnd; ++i)
        active[i] = bandActive[i];
}

// 帯域内だけサブハーモニック除去
// [FIX v2] 1.18f → 1.08f: サブハーモニック除去を強化
inline void SuppressBandSubharm(const float* strengths, bool* active, int count,
    int bandStart, int bandEnd)
{
    if (!strengths || !active || bandStart < 0 || bandEnd > count) return;

    static const int kDown[] = { 12, 19, 24, 7, 5 };
    for (int i = bandEnd - 1; i >= bandStart; --i) {
        if (!active[i]) continue;
        for (int d : kDown) {
            const int lo = i - d;
            if (lo < bandStart) continue;
            if (!active[lo]) continue;
            // [FIX v2] 1.18f → 1.08f
            if (strengths[i] <= strengths[lo] * 1.08f)
                active[i] = false;
        }
    }
}

// 倍音サリエンスを使わず生強度の局所ピークだけ拾う（低音の隣接誤検出向け）
inline int PickRawPeaksInRange(const float* strengths, bool* outActive, int count,
    int bandStart, int bandEnd, int maxNotes, float relativeThresh)
{
    if (!strengths || !outActive || count <= 0 || maxNotes <= 0) return 0;
    if (bandStart < 0) bandStart = 0;
    if (bandEnd > count) bandEnd = count;
    if (bandStart >= bandEnd) return 0;

    float bandMax = 0.0f;
    for (int i = bandStart; i < bandEnd; ++i)
        if (strengths[i] > bandMax) bandMax = strengths[i];
    if (bandMax < 1e-6f) return 0;

    const float minVal = bandMax * relativeThresh;
    struct Peak { int idx; float val; };
    Peak peaks[32];
    int peakCount = 0;

    for (int i = bandStart; i < bandEnd; ++i) {
        if (strengths[i] < minVal) continue;
        const int lo = (i - 1 < bandStart) ? bandStart : (i - 1);
        const int hi = (i + 1 >= bandEnd) ? (bandEnd - 1) : (i + 1);
        bool isPeak = true;
        for (int j = lo; j <= hi; ++j) {
            if (j == i) continue;
            if (strengths[j] >= strengths[i]) {
                isPeak = false;
                break;
            }
        }
        if (!isPeak) continue;
        if (peakCount < 32) {
            peaks[peakCount].idx = i;
            peaks[peakCount].val = strengths[i];
            ++peakCount;
        }
    }

    std::sort(peaks, peaks + peakCount, [](const Peak& a, const Peak& b) {
        return a.val > b.val;
    });

    int picked = 0;
    for (int p = 0; p < peakCount && picked < maxNotes; ++p) {
        outActive[peaks[p].idx] = true;
        ++picked;
    }
    return picked;
}

// Pre-scored strengths: greedy pick without BuildHarmonicSalience
inline int PickGreedyNotesToBand(const float* inStrengths, bool* outActive, int count,
    int bandStart, int bandEnd, int maxNotes, float relativeThresh)
{
    if (!inStrengths || !outActive || count <= 0 || maxNotes <= 0) return 0;
    if (bandStart < 0) bandStart = 0;
    if (bandEnd > count) bandEnd = count;
    if (bandStart >= bandEnd) return 0;

    float bandMax = 0.0f;
    for (int i = bandStart; i < bandEnd; ++i)
        if (inStrengths[i] > bandMax) bandMax = inStrengths[i];
    if (bandMax < 1e-6f) return 0;

    const float minS = bandMax * relativeThresh;
    float work[128];
    memset(work, 0, sizeof(work));
    for (int i = bandStart; i < bandEnd; ++i)
        work[i] = inStrengths[i];

    // h2,h3,h4,h5,h6,h8。3〜11（3度・5度など和音構成音）は倍音ではないので抑えない。
    static const int kHarmUp[] = {
        12, 19, 24, 28, 31, 36
    };
    static const int kHarmDown[] = { 12, 19, 24, 28, 31, 36 };

    int picked = 0;
    for (int round = 0; round < maxNotes; ++round) {
        int best = -1;
        float bestS = minS;
        for (int i = bandStart; i < bandEnd; ++i) {
            if (work[i] > bestS) {
                bestS = work[i];
                best = i;
            }
        }
        if (best < 0) break;

        outActive[best] = true;
        ++picked;
        work[best] = 0.0f;

        for (int h : kHarmUp) {
            const int hi = best + h;
            if (hi < count) work[hi] *= 0.06f;
        }
        for (int h : kHarmDown) {
            const int lo = best - h;
            if (lo >= 0) work[lo] *= 0.10f;
        }
    }
    return picked;
}

inline int PickFundamentalNotesToBand(const float* inStrengths, bool* outActive, int count,
    int bandStart, int bandEnd, int maxNotes, float relativeThresh)
{
    if (!inStrengths || !outActive || count <= 0 || maxNotes <= 0) return 0;
    if (bandStart < 0) bandStart = 0;
    if (bandEnd > count) bandEnd = count;
    if (bandStart >= bandEnd) return 0;

    bool temp[128];
    memset(temp, 0, sizeof(temp));
    PickFundamentalNotesRange(inStrengths, temp, count, bandStart, bandEnd, maxNotes, relativeThresh);
    int n = 0;
    for (int i = bandStart; i < bandEnd; ++i) {
        if (temp[i]) {
            outActive[i] = true;
            ++n;
        }
    }
    return n;
}

inline float BandMaxStrength(const float* strengths, int bandStart, int bandEnd)
{
    float bandMax = 0.0f;
    if (!strengths || bandStart >= bandEnd) return 0.0f;
    for (int i = bandStart; i < bandEnd; ++i)
        if (strengths[i] > bandMax) bandMax = strengths[i];
    return bandMax;
}

// 帯域内: 帯域最大に対する比率 + 上位N音 + 1位からの比率で弱い候補を刈る
inline void PruneBandPicks(const float* strengths, bool* active, int bandStart, int bandEnd,
    int maxKeep, float minRatioOfBandMax, float minRatioOfTop)
{
    if (!strengths || !active || bandStart >= bandEnd || maxKeep <= 0) return;

    const float bandMax = BandMaxStrength(strengths, bandStart, bandEnd);
    if (bandMax < 1e-6f) return;

    struct Item { int idx; float val; };
    Item items[64];
    int n = 0;

    for (int i = bandStart; i < bandEnd; ++i) {
        if (!active[i]) continue;
        if (strengths[i] < bandMax * minRatioOfBandMax) {
            active[i] = false;
            continue;
        }
        if (n < 64) {
            items[n].idx = i;
            items[n].val = strengths[i];
            ++n;
        }
    }
    if (n <= 0) return;

    std::sort(items, items + n, [](const Item& a, const Item& b) {
        return a.val > b.val;
    });

    const float topVal = items[0].val;
    for (int k = 0; k < n; ++k) {
        if (k >= maxKeep || items[k].val < topVal * minRatioOfTop)
            active[items[k].idx] = false;
    }
}

// 中音+高音を合わせて最大 maxTotal 音に制限
inline void LimitCombinedBandPicks(const float* strengths, bool* active,
    int bandAStart, int bandAEnd, int bandBStart, int bandBEnd,
    int maxTotal, float minRatioOfTop)
{
    if (!strengths || !active || maxTotal <= 0) return;

    struct Item { int idx; float val; };
    Item items[64];
    int n = 0;

    auto collect = [&](int from, int to) {
        for (int i = from; i < to; ++i) {
            if (!active[i]) continue;
            if (n < 64) {
                items[n].idx = i;
                items[n].val = strengths[i];
                ++n;
            }
        }
    };
    collect(bandAStart, bandAEnd);
    collect(bandBStart, bandBEnd);
    if (n <= maxTotal) return;

    std::sort(items, items + n, [](const Item& a, const Item& b) {
        return a.val > b.val;
    });

    const float topVal = items[0].val;
    for (int k = 0; k < n; ++k) {
        if (k >= maxTotal || items[k].val < topVal * minRatioOfTop)
            active[items[k].idx] = false;
    }
}

inline void MaskNonFundamentals(float* strengths, int count,
    int maxNotes = 8, float relativeThresh = 0.22f)
{
    if (!strengths || count <= 0 || count > 128) return;
    bool active[128];
    PickFundamentalNotes(strengths, active, count, maxNotes, relativeThresh);
    for (int i = 0; i < count; ++i) {
        if (!active[i]) strengths[i] = 0.0f;
    }
}

inline void NormalizeDisplayPeak(float* values, int count, float cap = 5.0f)
{
    if (!values || count <= 0 || cap <= 0.0f) return;
    float maxV = 0.0f;
    for (int i = 0; i < count; ++i)
        if (values[i] > maxV) maxV = values[i];
    if (maxV <= cap) return;
    const float scale = cap / maxV;
    for (int i = 0; i < count; ++i)
        values[i] *= scale;
}

inline void NormalizeDisplayPeakD(double* values, int count, double cap = 5.0)
{
    if (!values || count <= 0 || cap <= 0.0) return;
    double maxV = 0.0;
    for (int i = 0; i < count; ++i)
        if (values[i] > maxV) maxV = values[i];
    if (maxV <= cap) return;
    const double scale = cap / maxV;
    for (int i = 0; i < count; ++i)
        values[i] *= scale;
}