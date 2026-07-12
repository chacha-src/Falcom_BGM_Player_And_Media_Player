#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include "PianoKeyTable.h"

// 帯域内: 局部ピーク -> 倍音整合 -> 基音条件を満たすものをすべて採用（本数上限なし）

inline float HarmonicFundamentalScore(const float* st, int i, int count)
{
    if (!st || i < 0 || i >= count) return 0.0f;
    const float f = st[i];
    if (f <= 1e-8f) return 0.0f;

    float support = 1.0f;
    static const int kHarmSemi[] = { 12, 19, 28 };
    static const float kHarmW[] = { 0.55f, 0.30f, 0.15f };
    for (int h = 0; h < 3; ++h) {
        const int j = i + kHarmSemi[h];
        if (j < count && st[j] > 0.0f)
            support += kHarmW[h] * min(st[j] / f, 1.5f);
    }
    return f * support;
}

inline int SnapToLocalMaximum(const float* st, int key, int lo, int hi)
{
    if (!st || key < lo || key >= hi) return key;
    int best = key;
    float bestS = st[key];
    for (int j = max(lo, key - 1); j < min(hi, key + 2); ++j) {
        if (st[j] > bestS) {
            bestS = st[j];
            best = j;
        }
    }
    return best;
}

inline bool IsLocalPeakInBand(const float* st, int i, int lo, int hi)
{
    if (i < lo || i >= hi) return false;
    const int jl = (i - 1 < lo) ? lo : (i - 1);
    const int jh = (i + 1 >= hi) ? (hi - 1) : (i + 1);
    for (int j = jl; j <= jh; ++j) {
        if (j == i) continue;
        if (st[j] >= st[i]) return false;
    }
    return true;
}

// 帯域内の局部ピークのうち、基音スコア閾値を満たすものをすべて採用（本数上限なし）
inline int PickAllFundamentalsInBand(const float* st, bool* outActive, int count,
    int bandStart, int bandEnd, float scoreRatio, float peakRatio)
{
    if (!st || !outActive || count <= 0) return 0;
    if (bandStart < 0) bandStart = 0;
    if (bandEnd > count) bandEnd = count;
    if (bandStart >= bandEnd) return 0;

    float bandMax = 0.0f;
    for (int i = bandStart; i < bandEnd; ++i)
        if (st[i] > bandMax) bandMax = st[i];
    if (bandMax < 1e-6f) return 0;

    const float peakMin = bandMax * peakRatio;
    struct Item { int idx; float score; float val; };
    Item items[64];
    int n = 0;

    for (int i = bandStart; i < bandEnd; ++i) {
        if (st[i] < peakMin) continue;
        if (!IsLocalPeakInBand(st, i, bandStart, bandEnd)) continue;
        const int snapped = SnapToLocalMaximum(st, i, bandStart, bandEnd);
        bool already = false;
        for (int u = 0; u < n; ++u)
            if (items[u].idx == snapped) { already = true; break; }
        if (already) continue;
        const float score = HarmonicFundamentalScore(st, snapped, count);
        if (n < 64) {
            items[n].idx = snapped;
            items[n].score = score;
            items[n].val = st[snapped];
            ++n;
        }
    }

    if (n <= 0) return 0;

    float topScore = items[0].score;
    for (int u = 1; u < n; ++u)
        if (items[u].score > topScore) topScore = items[u].score;
    if (topScore < 1e-6f) topScore = bandMax;

    int picked = 0;
    for (int k = 0; k < n; ++k) {
        if (items[k].score < topScore * scoreRatio) continue;
        outActive[items[k].idx] = true;
        ++picked;
    }
    return picked;
}

inline int PickHarmonicPeaksInBand(const float* st, bool* outActive, int count,
    int bandStart, int bandEnd, int maxNotes, float scoreRatio, float peakRatio)
{
    if (!st || !outActive || count <= 0 || maxNotes <= 0) return 0;
    if (bandStart < 0) bandStart = 0;
    if (bandEnd > count) bandEnd = count;
    if (bandStart >= bandEnd) return 0;

    float bandMax = 0.0f;
    for (int i = bandStart; i < bandEnd; ++i)
        if (st[i] > bandMax) bandMax = st[i];
    if (bandMax < 1e-6f) return 0;

    const float peakMin = bandMax * peakRatio;
    struct Item { int idx; float score; float val; };
    Item items[64];
    int n = 0;

    for (int i = bandStart; i < bandEnd; ++i) {
        if (st[i] < peakMin) continue;
        if (!IsLocalPeakInBand(st, i, bandStart, bandEnd)) continue;
        const int snapped = SnapToLocalMaximum(st, i, bandStart, bandEnd);
        bool already = false;
        for (int u = 0; u < n; ++u)
            if (items[u].idx == snapped) { already = true; break; }
        if (already) continue;
        const float score = HarmonicFundamentalScore(st, snapped, count);
        if (n < 64) {
            items[n].idx = snapped;
            items[n].score = score;
            items[n].val = st[snapped];
            ++n;
        }
    }

    if (n <= 0) return 0;

    std::sort(items, items + n, [](const Item& a, const Item& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.val > b.val;
    });

    float topScore = items[0].score;
    if (topScore < 1e-6f) topScore = bandMax;

    int picked = 0;
    for (int k = 0; k < n && picked < maxNotes; ++k) {
        if (items[k].score < topScore * scoreRatio) continue;
        const int idx = items[k].idx;
        bool dup = false;
        for (int p = 0; p < k; ++p)
            if (items[p].idx == idx) { dup = true; break; }
        if (dup) continue;
        outActive[idx] = true;
        ++picked;
    }
    return picked;
}

inline void CollapseNearbyPicks(const float* st, bool* active, int lo, int hi, int minSemitoneGap,
    bool preferLowerPitch = false)
{
    if (!st || !active || lo >= hi || minSemitoneGap < 1) return;

    struct Item { int idx; float val; };
    Item items[32];
    int n = 0;
    for (int i = lo; i < hi; ++i) {
        if (!active[i]) continue;
        if (n < 32) { items[n].idx = i; items[n].val = st[i]; ++n; }
    }
    if (n <= 1) return;

    if (!preferLowerPitch) {
        std::sort(items, items + n, [](const Item& a, const Item& b) {
            return a.val > b.val;
        });
    }
    else {
        std::sort(items, items + n, [](const Item& a, const Item& b) {
            return a.idx < b.idx;
        });
    }

    int keepIdx[32];
    int kept = 0;
    for (int k = 0; k < n; ++k) {
        bool ok = true;
        for (int j = 0; j < kept; ++j) {
            if (abs(items[k].idx - items[keepIdx[j]].idx) < minSemitoneGap) {
                ok = false;
                break;
            }
        }
        if (ok)
            keepIdx[kept++] = k;
    }

    for (int i = lo; i < hi; ++i) active[i] = false;
    for (int j = 0; j < kept; ++j)
        active[items[keepIdx[j]].idx] = true;
}

inline void RejectUpperHarmonicPicks(const float* st, bool* active, int lo, int hi,
    const bool* lockKeys = nullptr)
{
    if (!st || !active || lo >= hi) return;
    static const int kUp[] = { 12, 19, 24, 17, 7, 5, 4, 3 };
    for (int i = hi - 1; i >= lo; --i) {
        if (!active[i]) continue;
        if (lockKeys && lockKeys[i]) continue;
        for (int h : kUp) {
            const int loIdx = i - h;
            if (loIdx < 0) continue;
            // 上の方が明らかに弱いときだけ倍音ゴースト。強い上声部は独立した音。
            if (st[i] <= st[loIdx] * 0.70f) {
                active[i] = false;
                break;
            }
        }
    }
}

inline float BandMaxInRange(const float* st, int lo, int hi)
{
    float bandMax = 0.0f;
    for (int i = lo; i < hi; ++i)
        if (st[i] > bandMax) bandMax = st[i];
    return bandMax;
}

inline bool BandContainsPeakNear(const float* st, int lo, int hi, int key, int radius)
{
    if (!st || key < lo || key >= hi) return false;
    const float bandMax = BandMaxInRange(st, lo, hi);
    if (bandMax < 1e-6f) return false;

    const int jl = max(lo, key - radius);
    const int jh = min(hi, key + radius + 1);
    for (int j = jl; j < jh; ++j) {
        if (st[j] >= bandMax * 0.18f && st[j] >= st[key] * 0.82f)
            return true;
    }
    return false;
}

// 複音: 偽サブハーモニックと上倍音ゴーストのみ除去（和音の3度/5度は残す）
inline void ResolveHarmonicsLight(const float* st, bool* active, int lo, int hi,
    const bool* lockKeys = nullptr)
{
    if (!st || !active || lo >= hi) return;

    for (int i = lo; i < hi; ++i) {
        if (!active[i]) continue;
        const float sc = st[i];
        for (int j = i + 1; j < hi; ++j) {
            if (!PianoKey::IsHarmonicPair(j, i)) continue;
            if (!active[j]) continue;
            if (st[j] >= sc * 0.70f) {
                if (lockKeys && lockKeys[i]) {
                    active[j] = false;
                }
                else if (lockKeys && lockKeys[j]) {
                    // keep locked upper
                }
                else {
                    active[i] = false;
                }
                break;
            }
        }
    }
    RejectUpperHarmonicPicks(st, active, lo, hi, lockKeys);
}

// 単音スタック: 倍音整理 + 基音昇格（ハープ等の過検出抑制）
inline void ResolveHarmonicPicks(const float* st, bool* active, int lo, int hi,
    const bool* lockKeys = nullptr)
{
    if (!st || !active || lo >= hi) return;
    const int count = PianoKey::COUNT;

    for (int i = lo; i < hi; ++i) {
        if (!active[i]) continue;
        const float sc = st[i];
        for (int j = i + 1; j < hi; ++j) {
            if (!PianoKey::IsHarmonicPair(j, i)) continue;
            if (!active[j]) continue;
            if (st[j] >= sc * 0.70f) {
                if (lockKeys && lockKeys[i]) {
                    active[j] = false;
                }
                else if (lockKeys && lockKeys[j]) {
                }
                else {
                    active[i] = false;
                }
                break;
            }
        }
    }

    RejectUpperHarmonicPicks(st, active, lo, hi, lockKeys);

    for (int i = hi - 1; i >= lo; --i) {
        if (!active[i]) continue;
        if (lockKeys && lockKeys[i]) continue;
        if (PianoKey::PassesFundamentalTest(st, i, count)) continue;
        bool hasMelodicAbove = false;
        for (int j = i + 1; j < hi; ++j) {
            if (!active[j]) continue;
            const int d = j - i;
            if (d >= 1 && d <= 11 && !PianoKey::IsHarmonicPair(j, i) && !PianoKey::IsOctaveRelated(j, i)) {
                hasMelodicAbove = true;
                break;
            }
        }
        if (hasMelodicAbove) continue;
        const float curSal = HarmonicFundamentalScore(st, i, count);
        int bestRoot = -1;
        float bestRootSal = 0.0f;
        for (int j = lo; j < i; ++j) {
            if (!PianoKey::IsHarmonicPair(i, j)) continue;
            const float rootSal = HarmonicFundamentalScore(st, j, count);
            if (rootSal > bestRootSal) {
                bestRootSal = rootSal;
                bestRoot = j;
            }
        }
        if (bestRoot < 0 || (i - bestRoot) > 14) continue;
        if (bestRootSal >= curSal * 0.78f || st[bestRoot] >= st[i] * 0.42f) {
            if (lockKeys && lockKeys[bestRoot]) continue;
            active[i] = false;
            active[bestRoot] = true;
        }
    }

    for (int i = hi - 1; i >= lo; --i) {
        if (!active[i]) continue;
        if (lockKeys && lockKeys[i]) continue;
        for (int j = lo; j < i; ++j) {
            if (!active[j]) continue;
            if (!PianoKey::IsHarmonicPair(i, j)) continue;
            if (lockKeys && lockKeys[j]) continue;
            if (st[j] >= st[i] * 0.32f)
                active[i] = false;
            else
                active[j] = false;
            break;
        }
    }
}

// 鳴っている音のオクターブをフレーム間で固定（倍音整理の入れ替わりを防ぐ）
inline void StabilizeOctavePicks(const float* st, bool* picked,
    const bool* activeKeys, const bool* prevActiveKeys, int lo, int hi)
{
    if (!st || !picked || lo >= hi) return;

    for (int i = lo; i < hi; ++i) {
        if (!activeKeys || !activeKeys[i]) continue;
        picked[i] = true;
        for (int d = 12; d <= 24; d += 12) {
            if (i + d < hi && picked[i + d] && (!activeKeys[i + d]))
                picked[i + d] = false;
            if (i - d >= lo && picked[i - d] && st[i] >= st[i - d] * 0.28f)
                picked[i - d] = false;
        }
    }

    if (prevActiveKeys) {
        for (int i = lo; i < hi - 12; ++i) {
            if (!prevActiveKeys[i] || (activeKeys && activeKeys[i])) continue;
            if (!picked[i + 12] || picked[i]) continue;
            if (st[i] >= st[i + 12] * 0.35f) {
                picked[i] = true;
                picked[i + 12] = false;
            }
        }
    }

    for (int i = lo; i < hi - 12; ++i) {
        if (!picked[i] || !picked[i + 12]) continue;
        if (activeKeys && (activeKeys[i] || activeKeys[i + 12])) continue;
        if (prevActiveKeys && (prevActiveKeys[i] || prevActiveKeys[i + 12])) continue;
        if (st[i] >= st[i + 12] * 0.36f)
            picked[i + 12] = false;
        else
            picked[i] = false;
    }
}

inline void FilterWeakOutliers(const float* st, bool* active, int lo, int hi,
    float relToBandMax = 0.20f)
{
    if (!st || !active || lo >= hi) return;
    const float bandMax = BandMaxInRange(st, lo, hi);
    if (bandMax < 1e-6f) return;
    const float absMin = bandMax * relToBandMax;

    for (int i = lo; i < hi; ++i) {
        if (!active[i] || st[i] >= absMin) continue;
        bool related = false;
        for (int j = lo; j < hi; ++j) {
            if (j == i || !active[j]) continue;
            if (PianoKey::IsHarmonicPair(i, j) || PianoKey::IsHarmonicPair(j, i)) {
                related = true;
                break;
            }
            if (abs(i - j) <= 2 && st[j] >= absMin) {
                related = true;
                break;
            }
        }
        if (!related)
            active[i] = false;
    }
}
