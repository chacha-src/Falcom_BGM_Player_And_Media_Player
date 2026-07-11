#pragma once
// 108鍵簡易ピアノロール検出: 帯域ピック → 独立基音へ統合（本数上限なし、倍音は1系列1基音）。
// [更新履歴]
//   - 帯域しきい値は元値に復帰済み(隣接ホッピング対策等の実験切り分けのため)。
//   - BuildFramePicks 末尾に PruneAbsoluteNoiseFloor(絶対値ノイズフロア0.02)を追加。
//     実音源のデバッグログで確認された、鍵盤全域に散らばる微小ノイズ(blend値
//     0.0000〜0.0113程度)の picked[] 誤通過を弾くためのもの。
//   ※ 過去のバージョンで「このファイルは変更していません」という注記を残して
//     しまっていたが誤りだった。以後、変更履歴は必ずこのヘッダーに明記する。
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
    static constexpr int O5_LO = 72;    // C5 = O5 開始（主旋律帯）
    static constexpr int O5_HI = 84;    // C6 = O5 終端
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
                if (!PianoKey::IsHarmonicPairExtended(hi, lo))
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

    inline void PruneWeakUpperHarmonicsInBand(const float* blend, bool* picked,
        int bandLo, int bandHi, int maxSemitoneGap = 36, float strengthRatio = 0.82f)
    {
        if (!blend || !picked || bandLo >= bandHi) return;
        static const int kUp[] = { 3, 4, 5, 7, 12, 19, 24, 28, 31, 36 };
        for (int i = bandHi - 1; i >= bandLo; --i) {
            if (!picked[i]) continue;
            for (int d : kUp) {
                if (d > maxSemitoneGap) continue;
                const int loIdx = i - d;
                if (loIdx < bandLo || !picked[loIdx]) continue;
                if (!PianoKey::IsHarmonicPairExtended(i, loIdx)) continue;
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
        ConsolidateHarmonicsInBand(blend, picked, 0, BASS_END, count, scoreRatio, false, 0, false);
        ConsolidateHarmonicsInBand(blend, picked, BASS_END, C4_KEY, count, 0.32f, true, 9, true);
        ConsolidateHarmonicsInBand(blend, picked, C4_KEY, MID_END, count, scoreRatio, true, 11, true);
        ConsolidateHarmonicsInBand(blend, picked, MID_END, EDGE_HI, count, scoreRatio, true, 11, true);
        ConsolidateHarmonicsInBand(blend, picked, EDGE_HI, COUNT, count, 0.36f, true, 9, false);
        PruneWeakUpperHarmonicsInBand(blend, picked, BASS_END, EDGE_HI, 36, 0.88f);
        PruneWeakUpperHarmonicsInBand(blend, picked, EDGE_HI, COUNT, 36, 0.82f);
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
                if (!PianoKey::IsHarmonicPairExtended(i, j)) continue;
                if (blend[j] >= sc * 0.42f) {
                    picked[i] = false;
                    break;
                }
            }
        }
    }

    // 帯域をまたぐ倍音漏れを落とす。
    // ベース上の弱い主旋律は「親より小さいオクターブ」でも自帯域で目立っていれば残す。
    inline void PruneCrossBandHarmonicGhosts(const float* blend, bool* picked, int count)
    {
        if (!blend || !picked || count <= 0) return;
        for (int hi = count - 1; hi >= BASS_END; --hi) {
            if (!picked[hi]) continue;
            if (PianoKey::IsHarmonicGhostPartial(blend, hi, count, BASS_END))
                picked[hi] = false;
        }
    }

    inline void PruneLowMidAgainstBass(const float* blend, bool* picked, int count)
    {
        if (!blend || !picked || count <= 0) return;
        const float lowMidMax = BandMax(blend, BASS_END, C4_KEY);
        for (int b = 0; b < BASS_END; ++b) {
            if (!picked[b]) continue;
            const float bsc = blend[b];
            if (bsc < 1e-6f) continue;
            for (int i = BASS_END; i < C4_KEY; ++i) {
                if (!picked[i]) continue;
                if (!PianoKey::IsHarmonicPairExtended(i, b))
                    continue;
                const int dist = i - b;
                const bool octaveLike = (dist == 12 || dist == 24 || dist == 36);
                // オクターブ: 低中音帯で目立つ弱い主旋律は残す
                if (octaveLike) {
                    if (lowMidMax > 1e-6f && blend[i] >= lowMidMax * 0.18f)
                        continue;
                    if (blend[i] < bsc * 0.35f)
                        picked[i] = false;
                    continue;
                }
                // 非オクターブ高次のみ従来どおり
                if (blend[i] < bsc * 0.58f)
                    picked[i] = false;
            }
        }
    }

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
        // [検証のため元値へ復帰] 隣接ホッピング対策(StabilizeAdjacentBinHopping)を
        // 単独の変数として切り分けて検証するため、しきい値は元の値に戻した。
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

        // [大幅強化] O6F以上と伺っている最高音域は、多少の本物の見逃しを許容してでも
        // ゴーストを潰す方針とする。絶対値・相対値をさらに引き上げ、加えて
        // 「近傍(±2鍵)よりどれだけ突出しているか」も要求する。
        // ディストーション/ドラムの広帯域ノイズは特定の1鍵だけが鋭く突出することが
        // 少ない(なだらかに広がる)ため、これで人の耳の「音色の鋭さ」判断を
        // 簡易的に近似する狙い。
        const float treMax = BandMax(blend, MID_END, COUNT);
        // [緩衝帯] O6F の実際の鍵番号が境界(EDGE_HI=100)と完全には一致しない
        // 可能性を考慮し、その手前(94〜99)にも中間的な厳しさを適用しておく。
        for (int i = 94; i < EDGE_HI; ++i) {
            if (!outPicked[i]) continue;
            if (blend[i] < 0.15f || blend[i] < treMax * 0.45f) {
                outPicked[i] = false;
                continue;
            }
            float neighborMax2 = 0.0f;
            for (int d = -2; d <= 2; ++d) {
                if (d == 0) continue;
                const int j = i + d;
                if (j < MID_END || j >= COUNT) continue;
                if (blend[j] > neighborMax2) neighborMax2 = blend[j];
            }
            if (neighborMax2 > 0.0f && blend[i] < neighborMax2 * 1.5f)
                outPicked[i] = false;
        }

        for (int i = EDGE_HI; i < COUNT; ++i) {
            if (!outPicked[i]) continue;
            if (blend[i] < 0.25f || blend[i] < treMax * 0.60f) {
                outPicked[i] = false;
                continue;
            }
            float neighborMax = 0.0f;
            for (int d = -2; d <= 2; ++d) {
                if (d == 0) continue;
                const int j = i + d;
                if (j < MID_END || j >= COUNT) continue;
                if (blend[j] > neighborMax) neighborMax = blend[j];
            }
            if (neighborMax > 0.0f && blend[i] < neighborMax * 1.8f)
                outPicked[i] = false;
        }
    }

    // 全帯域共通ではなく帯域別の絶対値ノイズフロア。
    // 低音ピークが大きい曲では中高音の実メロディ絶対値が小さくなり、
    // 一律 0.02 だと C4 以上が全滅する（ガウバン参上等で確認）。
    inline void PruneAbsoluteNoiseFloor(const float* blend, bool* outPicked, int count,
        float absFloor = 0.02f)
    {
        if (!blend || !outPicked) return;
        const float floorBass = absFloor;
        const float floorLowMid = absFloor * 0.45f;
        const float floorMid = absFloor * 0.22f;
        const float floorTre = absFloor * 0.28f;
        for (int i = 0; i < count; ++i) {
            if (!outPicked[i]) continue;
            float fl = floorMid;
            if (i < BASS_END) fl = floorBass;
            else if (i < C4_KEY) fl = floorLowMid;
            else if (i >= MID_END) fl = floorTre;
            if (blend[i] < fl)
                outPicked[i] = false;
        }
    }

    inline void BuildDetectionSpectrum(const float* smoothed, const float* raw, float* out, int count)
    {
        if (!smoothed || !raw || !out || count <= 0) return;
        for (int i = 0; i < count; ++i)
            out[i] = smoothed[i] * 0.52f + raw[i] * 0.48f;
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

    // 帯域内オンセット: 全体最大ではなく対象帯域のオンセット最大を基準にする。
    // ベース/ドラムの短窓エネルギーが全体を支配すると O5 主旋律の onset が埋もれるため。
    inline bool OnsetSupportsPickInBand(const float* onset, const float* prevOnset,
        int keyIndex, int bandLo, int bandHi, float levelScale)
    {
        if (!onset || !prevOnset || keyIndex < bandLo || keyIndex >= bandHi) return false;
        float oMax = 0.0f;
        for (int i = bandLo; i < bandHi; ++i)
            if (onset[i] > oMax) oMax = onset[i];
        if (oMax < 0.0025f) return false;

        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        const float delta = onset[keyIndex] - prevOnset[keyIndex];
        return onset[keyIndex] >= oMax * 0.18f * scale &&
            delta >= oMax * 0.10f;
    }

    // 弱い主旋律救済: O5 帯で局所ピークかつ帯域内オンセットがあるものを強制ピック。
    // 振幅はベースに負けていても、アタックを持つ実音はここを通る。
    inline void PromoteOnsetMelodyInBand(const float* blend, const float* onset,
        const float* prevOnset, bool* picked, int count,
        int bandLo, int bandHi, float levelScale, float bandPeakRatio = 0.16f)
    {
        if (!blend || !onset || !prevOnset || !picked || bandLo >= bandHi) return;
        const float bmax = BandMax(blend, bandLo, bandHi);
        if (bmax < 1e-6f) return;
        const float peakMin = bmax * bandPeakRatio;

        for (int i = bandLo; i < bandHi; ++i) {
            if (picked[i]) continue;
            if (blend[i] < peakMin) continue;
            if (i > bandLo && blend[i - 1] >= blend[i]) continue;
            if (i + 1 < bandHi && blend[i + 1] > blend[i]) continue;
            if (!OnsetSupportsPickInBand(onset, prevOnset, i, bandLo, bandHi, levelScale))
                continue;
            // 明らかな低次倍音ゴーストだけは救済しない
            if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END))
                continue;
            picked[i] = true;
        }
    }

    // 高音ゴースト抑制: オンセットが無く、帯域内でも弱い／ゴースト形のピックを落とす。
    // 主旋律帯(O5)はオンセット無しでも帯域トップ級なら残す（サスティン中の弱い保持）。
    inline void PruneTrebleGhostsWithoutOnset(const float* blend, const float* onset,
        const float* prevOnset, bool* picked, int count, float levelScale)
    {
        if (!blend || !onset || !prevOnset || !picked) return;

        const float o5Max = BandMax(blend, O5_LO, O5_HI);
        const float hiMax = BandMax(blend, O5_HI, COUNT);

        for (int i = O5_LO; i < COUNT; ++i) {
            if (!picked[i]) continue;
            const bool hasOnset = (i < O5_HI)
                ? OnsetSupportsPickInBand(onset, prevOnset, i, O5_LO, O5_HI, levelScale)
                : OnsetSupportsPickInBand(onset, prevOnset, i, O5_HI, COUNT, levelScale);

            if (hasOnset) continue;

            if (i < O5_HI) {
                // O5: サスティン中の主旋律は残す。薄い漏れだけ落とす。
                if (o5Max > 1e-6f && blend[i] < o5Max * 0.22f)
                    picked[i] = false;
                else if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END))
                    picked[i] = false;
            }
            else {
                // O6+: オンセット無しは原則ゴースト扱い（ドラム/漏れの赤点対策）
                if (hiMax > 1e-6f && blend[i] < hiMax * 0.45f) {
                    picked[i] = false;
                    continue;
                }
                if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END) ||
                    !PianoKey::HasOwnOvertoneSupport(blend, i, count, 0.10f))
                    picked[i] = false;
            }
        }
    }

    inline void BuildFramePicks(const float* blend, bool* outPicked, int count,
        float levelScale = 1.0f, float absNoiseFloor = 0.02f,
        const float* onset = nullptr, const float* prevOnset = nullptr)
    {
        if (!blend || !outPicked || count != COUNT) return;
        memset(outPicked, 0, (size_t)count * sizeof(bool));

        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        // ベース支配時: 中高音の弱い主旋律を拾いやすくする（帯域内相対ピックの感度だけ緩和）。
        const float bassMx = BandMax(blend, 0, BASS_END);
        const float midHiMx = BandMax(blend, C4_KEY, COUNT);
        float melodyScale = scale;
        if (bassMx > 1e-6f && midHiMx > 1e-8f && bassMx >= midHiMx * 2.5f) {
            melodyScale = scale * 0.82f;
            if (melodyScale < 0.55f) melodyScale = 0.55f;
        }

        {
            PickSingleBassNote(blend, outPicked, count, 0.40f * scale);
        }

        const struct BandCfg { int lo, hi; float scoreRatio; float peakRatio; int maxNotes; bool melodyBand; } bands[] = {
            { BASS_END, LOW_MID_SPLIT, 0.22f, 0.16f, 2, false },
            { LOW_MID_SPLIT, C4_KEY, 0.16f, 0.10f, 0, true },
            { C4_KEY, O5_LO, 0.11f, 0.070f, 0, true },
            { O5_LO, O5_HI, 0.09f, 0.055f, 0, true },  // O5 主旋律帯: より敏感
            { O5_HI, EDGE_HI, 0.14f, 0.090f, 0, false },
            { EDGE_HI, COUNT, 0.26f, 0.16f, 0, false },
        };
        for (const BandCfg& b : bands) {
            const float s = b.melodyBand ? melodyScale : scale;
            if (b.maxNotes > 0) {
                PickHarmonicPeaksInBand(blend, outPicked, count,
                    b.lo, b.hi, b.maxNotes, b.scoreRatio * s, b.peakRatio * s);
            }
            else {
                PickAllFundamentalsInBand(blend, outPicked, count,
                    b.lo, b.hi, b.scoreRatio * s, b.peakRatio * s);
            }
        }

        RefineToLocalPeaksInBand(blend, outPicked, count, 0, BASS_END, 1);
        RefineToLocalPeaksInBand(blend, outPicked, count, BASS_END, MID_END, 1);
        RefineToLocalPeaksInBand(blend, outPicked, count, MID_END, COUNT, 1);

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
            else if (i >= C4_KEY &&
                PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END))
                outPicked[i] = false;
        }

        PruneBassSubharmonics(blend, outPicked, count);
        PruneLowMidAgainstBass(blend, outPicked, count);
        PruneCrossBandHarmonicGhosts(blend, outPicked, count);
        PruneUltraTrebleHarmonics(blend, outPicked, count);

        // オンセット軸: 弱い O5 主旋律を拾い、オンセット無しの高音ゴーストを落とす
        if (onset && prevOnset) {
            PromoteOnsetMelodyInBand(blend, onset, prevOnset, outPicked, count,
                O5_LO, O5_HI, melodyScale, 0.14f);
            PromoteOnsetMelodyInBand(blend, onset, prevOnset, outPicked, count,
                C4_KEY, O5_LO, melodyScale, 0.18f);
            PruneTrebleGhostsWithoutOnset(blend, onset, prevOnset, outPicked, count, scale);
        }

        PruneWeakRelativePerBand(blend, outPicked, count);

        CollapseNearbyPicks(blend, outPicked, 0, BASS_END, 7, false);
        CollapseNearbyPicks(blend, outPicked, BASS_END, LOW_MID_SPLIT, 3, false);
        CollapseNearbyPicks(blend, outPicked, LOW_MID_SPLIT, C4_KEY, 4, false);
        CollapseNearbyPicks(blend, outPicked, C4_KEY, O5_LO, 3, false);
        CollapseNearbyPicks(blend, outPicked, O5_LO, O5_HI, 3, false);
        CollapseNearbyPicks(blend, outPicked, O5_HI, COUNT, 4, false);

        PruneEdgeRegisterNoise(blend, outPicked, count);
        PruneAbsoluteNoiseFloor(blend, outPicked, count, absNoiseFloor);
    }
}