#pragma once
// 108鍵ピアノロール検出: 帯域ピック → 独立基音へ統合（本数上限なし、倍音は1系列1基音）。
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

    static constexpr int BASS_END = 36;
    static constexpr int MID_END = 72;
    static constexpr int C4_KEY = 60;   // O4 境界（この下を低中音として厳格化）
    static constexpr int LOW_MID_SPLIT = 48; // C3: これ未満はベース漏れが多い
    static constexpr int EDGE_LO = 12;
    static constexpr int EDGE_HI = 100;

    inline int KeyBandIndex(int keyIndex)
    {
        if (keyIndex < BASS_END) return 0;
        if (keyIndex < MID_END) return 1;
        return 2;
    }

    inline float IirAlphaForKey(int keyIndex)
    {
        if (keyIndex < BASS_END) return 0.28f;
        if (keyIndex < MID_END) return 0.40f;
        return 0.44f;
    }

    inline float BandMax(const float* st, int lo, int hi)
    {
        float mx = 0.0f;
        if (!st || lo >= hi) return 0.0f;
        for (int i = lo; i < hi; ++i)
            if (st[i] > mx) mx = st[i];
        return mx;
    }

    // 帯域内のみ調波系列を1基音へ（maxGap>0 で遠距離連鎖マージを防止）
    inline void ConsolidateHarmonicsInBand(const float* blend, bool* picked,
        int bandLo, int bandHi, int count, float scoreRatio, bool promoteFundamental,
        int maxSemitoneGap = 0, bool skipOctaveDoubling = false)
    {
        if (!blend || !picked || bandLo >= bandHi || count <= 0) return;

        int keys[48];
        float scores[48];
        int n = 0;
        for (int i = bandLo; i < bandHi; ++i) {
            if (!picked[i]) continue;
            if (n < 48) {
                keys[n] = i;
                scores[n] = HarmonicFundamentalScore(blend, i, count);
                ++n;
            }
        }
        if (n <= 1) return;

        int parent[48];
        for (int i = 0; i < n; ++i) parent[i] = i;

        auto findRoot = [&](int x) {
            while (parent[x] != x) {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        };
        auto unite = [&](int a, int b) {
            a = findRoot(a);
            b = findRoot(b);
            if (a != b) parent[b] = a;
        };

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                const int lo = keys[i] < keys[j] ? keys[i] : keys[j];
                const int hi = keys[i] < keys[j] ? keys[j] : keys[i];
                const int dist = hi - lo;
                if (maxSemitoneGap > 0 && dist > maxSemitoneGap)
                    continue;
                if (skipOctaveDoubling && (dist == 12 || dist == 24 || dist == 36))
                    continue;
                if (!PianoKey::IsHarmonicPair(hi, lo))
                    continue;
                unite(i, j);
            }
        }

        bool keep[128];
        memset(keep, 0, sizeof(keep));

        for (int r = 0; r < n; ++r) {
            if (findRoot(r) != r) continue;

            int members[16];
            int nm = 0;
            float clusterMax = 0.0f;
            for (int j = 0; j < n; ++j) {
                if (findRoot(j) != r) continue;
                if (nm < 16) members[nm++] = keys[j];
                if (scores[j] > clusterMax) clusterMax = scores[j];
            }
            if (nm <= 0 || clusterMax < 1e-6f) continue;

            const float floor = clusterMax * scoreRatio;
            int keeper = -1;
            for (int m = 0; m < nm; ++m) {
                const int k = members[m];
                const float sc = HarmonicFundamentalScore(blend, k, count);
                if (sc < floor) continue;
                if (keeper < 0 || k < keeper)
                    keeper = k;
            }
            if (keeper < 0) continue;

            if (promoteFundamental) {
                for (int nn = 2; nn <= 5; ++nn) {
                    const int lo = PianoKey::HarmonicDownKey(keeper, nn);
                    if (lo < bandLo || lo >= bandHi) continue;
                    if (blend[lo] < blend[keeper] * 0.20f) continue;
                    if (blend[lo] >= blend[keeper] * 0.38f ||
                        PianoKey::PassesFundamentalTestSustain(blend, lo, count)) {
                        keeper = lo;
                    }
                }
            }

            keep[keeper] = true;
        }

        for (int i = bandLo; i < bandHi; ++i)
            picked[i] = keep[i];
    }

    // 中高音: 近い下側倍音で明らかに弱いものだけ落とす（和音・オクターブは維持）
    inline void PruneWeakUpperHarmonicsInBand(const float* blend, bool* picked,
        int bandLo, int bandHi, int maxSemitoneGap = 14, float strengthRatio = 0.82f)
    {
        if (!blend || !picked || bandLo >= bandHi) return;
        static const int kUp[] = { 3, 4, 5, 7, 12, 19 };
        for (int i = bandHi - 1; i >= bandLo; --i) {
            if (!picked[i]) continue;
            for (int d : kUp) {
                if (d > maxSemitoneGap) continue;
                const int loIdx = i - d;
                if (loIdx < bandLo || !picked[loIdx]) continue;
                if (!PianoKey::IsHarmonicPair(i, loIdx)) continue;
                if (blend[i] <= blend[loIdx] * strengthRatio) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    inline void ConsolidateToIndependentFundamentals(const float* blend, bool* picked,
        int count, float scoreRatio = 0.28f)
    {
        if (!blend || !picked || count <= 0) return;
        // 低音: 帯域内統合のみ。中高音: サブ帯域ごと・近距離倍音のみ（連鎖マージ禁止）
        ConsolidateHarmonicsInBand(blend, picked, 0, BASS_END, count, scoreRatio, false, 0, false);
        ConsolidateHarmonicsInBand(blend, picked, BASS_END, C4_KEY, count, 0.32f, true, 9, true);
        ConsolidateHarmonicsInBand(blend, picked, C4_KEY, MID_END, count, scoreRatio, true, 11, true);
        ConsolidateHarmonicsInBand(blend, picked, MID_END, EDGE_HI, count, scoreRatio, true, 11, true);
        ConsolidateHarmonicsInBand(blend, picked, EDGE_HI, COUNT, count, 0.36f, true, 9, false);
        PruneWeakUpperHarmonicsInBand(blend, picked, BASS_END, EDGE_HI, 14, 0.82f);
        PruneWeakUpperHarmonicsInBand(blend, picked, EDGE_HI, COUNT, 12, 0.78f);
    }

    inline void PruneBassSubharmonics(const float* blend, bool* picked, int count)
    {
        if (!blend || !picked || count <= 0) return;
        for (int i = 0; i < BASS_END - 1; ++i) {
            if (!picked[i]) continue;
            for (int j = i + 1; j < BASS_END; ++j) {
                if (!picked[j]) continue;
                if (!PianoKey::IsHarmonicPair(j, i)) continue;
                if (blend[j] >= blend[i] * 0.45f) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    inline void PruneUltraTrebleHarmonics(const float* blend, bool* picked, int count)
    {
        if (!blend || !picked || count <= 0) return;
        for (int i = EDGE_HI; i < count; ++i) {
            if (!picked[i]) continue;
            const float sc = blend[i];
            for (int j = BASS_END; j < EDGE_HI; ++j) {
                if (!picked[j]) continue;
                if (!PianoKey::IsHarmonicPair(i, j)) continue;
                if (blend[j] >= sc * 0.42f) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    // O4 未満: 低音ピックの倍音・漏れエネルギーを落とす（ドラム/ベース漏れの C2〜G2 帯）
    inline void PruneLowMidAgainstBass(const float* blend, bool* picked, int count)
    {
        if (!blend || !picked || count <= 0) return;
        for (int b = 0; b < BASS_END; ++b) {
            if (!picked[b]) continue;
            const float bsc = blend[b];
            if (bsc < 1e-6f) continue;
            for (int i = BASS_END; i < C4_KEY; ++i) {
                if (!picked[i]) continue;
                if (!PianoKey::IsHarmonicPair(i, b) && !PianoKey::IsHarmonicPair(b, i))
                    continue;
                if (i > b && blend[i] < bsc * 0.58f)
                    picked[i] = false;
            }
        }
    }

    // 低音: 調波スコアで1本。2位が近い且つ非倍音なら未確定として拾わない（フレーム間暴れ抑制）
    inline void PickSingleBassNote(const float* blend, bool* outPicked, int count, float thresh)
    {
        if (!blend || !outPicked || count <= 0) return;
        int best = -1, second = -1;
        float bestSc = 0.0f, secondSc = 0.0f;
        for (int i = 0; i < BASS_END; ++i) {
            const float sc = HarmonicFundamentalScore(blend, i, count);
            if (sc > bestSc) {
                secondSc = bestSc;
                second = best;
                bestSc = sc;
                best = i;
            }
            else if (sc > secondSc) {
                secondSc = sc;
                second = i;
            }
        }
        const float bassMx = BandMax(blend, 0, BASS_END);
        if (best < 0 || bestSc < bassMx * thresh || blend[best] < bassMx * 0.48f)
            return;
        if (second >= 0 && secondSc >= bestSc * 0.88f) {
            const int lo = best < second ? best : second;
            const int hi = best < second ? second : best;
            if (PianoKey::IsHarmonicPair(hi, lo))
                best = lo;
        }
        outPicked[best] = true;
    }

    inline void PruneWeakRelativePerBand(const float* blend, bool* picked, int count)
    {
        if (!blend || !picked || count <= 0) return;
        const struct BandFloor { int lo, hi; float relFloor; } bands[] = {
            { 0, BASS_END, 0.32f },
            { BASS_END, LOW_MID_SPLIT, 0.16f },
            { LOW_MID_SPLIT, C4_KEY, 0.12f },
            { C4_KEY, MID_END, 0.08f },
            { MID_END, EDGE_HI, 0.08f },
            { EDGE_HI, COUNT, 0.18f },
        };
        for (const BandFloor& b : bands) {
            float bmax = 0.0f;
            for (int i = b.lo; i < b.hi; ++i) {
                if (!picked[i]) continue;
                if (blend[i] > bmax) bmax = blend[i];
            }
            if (bmax < 1e-6f) continue;
            const float floor = bmax * b.relFloor;
            for (int i = b.lo; i < b.hi; ++i) {
                if (!picked[i]) continue;
                if (blend[i] < floor)
                    picked[i] = false;
            }
        }
    }

    inline void PruneEdgeRegisterNoise(const float* blend, bool* outPicked, int count)
    {
        if (!blend || !outPicked || count != COUNT) return;

        const float bassMax = BandMax(blend, 0, BASS_END);
        for (int i = 0; i < EDGE_LO; ++i) {
            if (!outPicked[i]) continue;
            if (blend[i] < 0.08f || blend[i] < bassMax * 0.30f)
                outPicked[i] = false;
        }

        const float treMax = BandMax(blend, MID_END, COUNT);
        for (int i = EDGE_HI; i < COUNT; ++i) {
            if (!outPicked[i]) continue;
            if (blend[i] < 0.09f || blend[i] < treMax * 0.32f)
                outPicked[i] = false;
        }
    }

    inline void BuildDetectionSpectrum(const float* smoothed, const float* raw, float* out, int count)
    {
        if (!smoothed || !raw || !out || count <= 0) return;
        for (int i = 0; i < count; ++i)
            out[i] = smoothed[i] * 0.52f + raw[i] * 0.48f;
    }

    inline void BuildFramePicks(const float* blend, bool* outPicked, int count,
        float levelScale = 1.0f)
    {
        if (!blend || !outPicked || count != COUNT) return;
        memset(outPicked, 0, (size_t)count * sizeof(bool));

        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        // 低音: 1本のみ（調波スコア＋曖昧時は拾わない）。中高音: 上限なし帯域ピック。
        {
            PickSingleBassNote(blend, outPicked, count, 0.40f * scale);
        }

        const struct BandCfg { int lo, hi; float scoreRatio; float peakRatio; int maxNotes; } bands[] = {
            { BASS_END, LOW_MID_SPLIT, 0.22f, 0.16f, 2 },
            { LOW_MID_SPLIT, C4_KEY, 0.18f, 0.12f, 0 },
            { C4_KEY, MID_END, 0.16f, 0.10f, 0 },
            { MID_END, EDGE_HI, 0.15f, 0.095f, 0 },
            { EDGE_HI, COUNT, 0.24f, 0.15f, 0 },
        };
        for (const BandCfg& b : bands) {
            if (b.maxNotes > 0) {
                PickHarmonicPeaksInBand(blend, outPicked, count,
                    b.lo, b.hi, b.maxNotes, b.scoreRatio * scale, b.peakRatio * scale);
            }
            else {
                PickAllFundamentalsInBand(blend, outPicked, count,
                    b.lo, b.hi, b.scoreRatio * scale, b.peakRatio * scale);
            }
        }

        RefineToLocalPeaksInBand(blend, outPicked, count, 0, BASS_END, 1);
        RefineToLocalPeaksInBand(blend, outPicked, count, BASS_END, MID_END, 1);
        RefineToLocalPeaksInBand(blend, outPicked, count, MID_END, COUNT, 1);

        // 倍音スパムを先に1系列1基音へ（Salience 前に実施）
        ConsolidateToIndependentFundamentals(blend, outPicked, count, 0.28f);

        for (int i = 0; i < BASS_END; ++i) {
            if (!outPicked[i]) continue;
            if (!PianoKey::SalienceLooksLikeFundamental(blend, i, count))
                outPicked[i] = false;
            else if (!PianoKey::PassesFundamentalTestSustain(blend, i, count))
                outPicked[i] = false;
        }

        for (int i = BASS_END; i < COUNT; ++i) {
            if (!outPicked[i]) continue;
            if (!PianoKey::SalienceAboveLowBand(blend, i, count, BASS_END))
                outPicked[i] = false;
            else if (i < LOW_MID_SPLIT &&
                !PianoKey::PassesFundamentalTest(blend, i, count))
                outPicked[i] = false;
            else if (i < C4_KEY &&
                !PianoKey::PassesFundamentalTestSustain(blend, i, count))
                outPicked[i] = false;
        }

        PruneBassSubharmonics(blend, outPicked, count);
        PruneLowMidAgainstBass(blend, outPicked, count);
        PruneUltraTrebleHarmonics(blend, outPicked, count);
        PruneWeakRelativePerBand(blend, outPicked, count);

        CollapseNearbyPicks(blend, outPicked, 0, BASS_END, 7, false);
        CollapseNearbyPicks(blend, outPicked, BASS_END, LOW_MID_SPLIT, 3, false);
        CollapseNearbyPicks(blend, outPicked, LOW_MID_SPLIT, C4_KEY, 4, false);
        CollapseNearbyPicks(blend, outPicked, C4_KEY, COUNT, 4, false);

        PruneEdgeRegisterNoise(blend, outPicked, count);
    }

    inline bool OnsetSupportsPick(const float* onset, const float* prevOnset,
        int keyIndex, float levelScale)
    {
        if (!onset || !prevOnset || keyIndex < 0 || keyIndex >= COUNT) return false;

        float oMax = 0.0f;
        for (int i = 0; i < COUNT; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.004f) return false;

        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        const float delta = onset[keyIndex] - prevOnset[keyIndex];
        return onset[keyIndex] >= oMax * 0.22f * scale &&
            delta >= oMax * 0.14f;
    }
}
