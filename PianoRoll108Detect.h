#pragma once
// 108鍵簡易ピアノロール検出
//
// 根治方針:
//   ノート＝基音 salience（部分音強度ではない）。
//   ただし全域1回の貪欲採択だとベースが先に枠を使い切り、
//   倍音スロット抑制で中高（ピアノ）が空になる。
//   → 帯域ごとに salience 採択枠を分け、低音の±1またがりは強制単一化。
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

    // ベースの倍音を減衰させ、中高音がベース倍音に「食われる」のを防ぐ。
    inline void SoftAttenuateBassHarmonics(const bool* bassPicked, float* specAmp, int count)
    {
        if (!bassPicked || !specAmp) return;
        for (int b = EDGE_LO; b < BASS_END; ++b) {
            if (!bassPicked[b]) continue;
            for (int n = 2; n <= 6; ++n) {
                const int hk = PianoKey::HarmonicUpKeyAny(b, n);
                if (hk < BASS_END || hk >= count) continue;
                // 振幅ドメインでの減衰。以前の power(0.32) は amp(~0.56)
                specAmp[hk] *= 0.56f;
            }
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

    inline void BuildFramePicks(const float* blend, bool* outPicked, int count,
        float levelScale = 1.0f, float absNoiseFloor = 0.00055f,
        const float* onset = nullptr, const float* prevOnset = nullptr,
        float pickBassRel = 0.28f, float pickLowMidRel = 0.20f,
        float pickMelodyRel = 0.10f, float pickTreRel = 0.22f,
        float onsetDeltaScale = 1.0f)
    {
        if (!blend || !outPicked || count != COUNT) return;
        memset(outPicked, 0, (size_t)count * sizeof(bool));

        float amp[COUNT];
        for (int i = 0; i < count; ++i) {
            const float floor = AbsFloorForKey(i, absNoiseFloor);
            amp[i] = (blend[i] >= floor) ? sqrtf(blend[i]) : 0.0f;
        }

        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        bool band[COUNT];

        // ---- 1) 低音: 枠多め、相対厳しめ。隣接は後で強制1本化 ----
        PickFundamentalNotesRange(amp, band, count, EDGE_LO, BASS_END, 24, pickBassRel / scale);
        for (int i = 0; i < EDGE_LO; ++i) band[i] = false;
        CollapseNearbyPicks(amp, band, EDGE_LO, BASS_END, 3, false); // 強い方優先
        ForceUniqueBassAdjacents(amp, band, EDGE_LO, BASS_END);
        RefineToLocalPeaksInBand(amp, band, count, EDGE_LO, BASS_END, 1);
        // 低域DC/サブオーディオ裾の除去
        for (int i = EDGE_LO; i < MUSIC_LOW_FLOOR && i < BASS_END; ++i)
            band[i] = false;
        // それ以上(オクターブ1〜)は「下隣の半音より強い」真の局所ピークのみ採用する。
        for (int i = MUSIC_LOW_FLOOR; i < BASS_END; ++i) {
            if (!band[i]) continue;
            if (amp[i - 1] >= amp[i] * 0.99f)
                band[i] = false;
        }
        MergeBandPicks(outPicked, band, EDGE_LO, BASS_END);

        // ベース倍音をソフト減衰した振幅スペクトルで上帯を採択（ゴースト抑制＋ピアノ枠確保）
        float upperSpecAmp[COUNT];
        memcpy(upperSpecAmp, amp, sizeof(upperSpecAmp));
        SoftAttenuateBassHarmonics(outPicked, upperSpecAmp, count);

        // ---- 2) 低中 (C3–C4): 弦の支えなど ----
        PickFundamentalNotesRange(upperSpecAmp, band, count, BASS_END, C4_KEY, 24, pickLowMidRel);
        CollapseNearbyPicks(amp, band, BASS_END, C4_KEY, 3, false);
        MergeBandPicks(outPicked, band, BASS_END, C4_KEY);

        // ---- 3) メロディ帯 C4–C6: 枠を多め・閾値緩め（ピアノ本命）----
        // 音数制限を 5 から 24 へ拡大。
        PickFundamentalNotesRange(upperSpecAmp, band, count, C4_KEY, O5_HI, 24, pickMelodyRel);
        // 帯域内 salience で取れなかったアタックを onset で救出（ゴースト形は除外）
        if (onset && prevOnset) {
            for (int i = C4_KEY; i < O5_HI; ++i) {
                if (band[i]) continue;
                if (upperSpecAmp[i] < sqrtf(AbsFloorForKey(i, absNoiseFloor)) * 1.5f) continue;
                if (!IsStrictLocalPeak(upperSpecAmp, i, C4_KEY, O5_HI)) continue;
                if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END)) continue;
                if (OnsetSupportsPickInBand(onset, prevOnset, i, C4_KEY, O5_HI, scale, onsetDeltaScale))
                    band[i] = true;
            }
        }
        CollapseNearbyPicks(amp, band, C4_KEY, O5_HI, 2, false);
        MergeBandPicks(outPicked, band, C4_KEY, O5_HI);

        // ---- 4) 高音 C6–C7: 控えめ ----
        PickFundamentalNotesRange(upperSpecAmp, band, count, O5_HI, EDGE_HI, 24, pickTreRel);
        for (int i = O5_HI; i < EDGE_HI; ++i) {
            if (!band[i]) continue;
            if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END) &&
                !PianoKey::HasOwnOvertoneSupport(blend, i, count, 0.18f))
                band[i] = false;
        }
        MergeBandPicks(outPicked, band, O5_HI, EDGE_HI);

        // ---- 5) 最高音 C7–B7: 通常曲の高音メロディ/装飾を拾う ----
        if (EDGE_HI < count) {
            PickFundamentalNotesRange(upperSpecAmp, band, count, EDGE_HI, count, 24, pickTreRel);
            for (int i = EDGE_HI; i < count; ++i) {
                if (!band[i]) continue;
                if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END) &&
                    !PianoKey::HasOwnOvertoneSupport(blend, i, count, 0.18f))
                    band[i] = false;
            }
            MergeBandPicks(outPicked, band, EDGE_HI, count);
        }

        // ---- 全域倍音ふるい（パルス波/矩形波対策）----
        for (int i = count - 1; i > EDGE_LO; --i) {
            if (!outPicked[i]) continue;
            for (int j = i - 1; j >= EDGE_LO; --j) {
                if (!outPicked[j]) continue;
                if (amp[j] <= amp[i]) continue; // 親はより強い方のみ
                
                int n = PianoKey::GetHarmonicNCompute(i, j, 16);
                if (n == 0) continue;

                // 予想される倍音の振幅比率
                float expectedAmpRatio = 0.0f;
                if (n == 2) expectedAmpRatio = 0.50f;
                else if (n == 3) expectedAmpRatio = 0.33f;
                else if (n == 4) expectedAmpRatio = 0.25f;
                else if (n == 5) expectedAmpRatio = 0.20f;
                else if (n == 6) expectedAmpRatio = 0.16f;
                else if (n == 7) expectedAmpRatio = 0.14f;
                else if (n == 8) expectedAmpRatio = 0.12f;
                else if (n <= 16) expectedAmpRatio = 1.0f / (float)n * 1.15f;
                else expectedAmpRatio = 0.05f;

                // オクターブ（n=2,4,8,16）は倍音が強く出ることが多いのでマージンを少し取る
                float margin = (n == 2 || n == 4 || n == 8 || n == 16) ? 1.45f : 1.35f;

                if (amp[i] < amp[j] * expectedAmpRatio * margin) {
                    outPicked[i] = false;
                    break;
                }
            }
        }

        // 最後にもう一度低音隣接を潰す（帯域マージ後の漏れ）
        ForceUniqueBassAdjacents(amp, outPicked, EDGE_LO, BASS_END);
        // 全域の局所ピーク化（裾野を拾ってしまった場合の掃除）
        RefineToLocalPeaks(amp, outPicked, count, 1);
    }
}
