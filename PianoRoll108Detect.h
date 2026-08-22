#pragma once
// 108鍵簡易ピアノロール検出
//
// 旧実装（帯域ごとの枠数と相対比によるピック）をベースにしつつ、
// 「低音に食われる（高音が消える）」問題と「音数制限」を解消した「いいとこ取り」版。
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

    inline void BuildFramePicks(const float* blend, bool* outPicked, int count,
        float levelScale = 1.0f, float absNoiseFloor = 0.00055f,
        const float* onset = nullptr, const float* prevOnset = nullptr,
        float pickBassRel = 0.28f, float pickLowMidRel = 0.20f,
        float pickMelodyRel = 0.10f, float pickTreRel = 0.22f,
        float onsetDeltaScale = 1.0f)
    {
        if (!blend || !outPicked || count != COUNT) return;
        memset(outPicked, 0, (size_t)count * sizeof(bool));

        float gated[COUNT];
        for (int i = 0; i < count; ++i) {
            const float floor = AbsFloorForKey(i, absNoiseFloor);
            gated[i] = (blend[i] >= floor) ? blend[i] : 0.0f;
        }

        float scale = levelScale;
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        bool band[COUNT];

        // ---- 1) 低音: 枠2本、相対厳しめ。隣接は後で強制1本化 ----
        PickFundamentalNotesRange(gated, band, count, EDGE_LO, BASS_END, 8, pickBassRel / scale);
        for (int i = 0; i < EDGE_LO; ++i) band[i] = false;
        CollapseNearbyPicks(blend, band, EDGE_LO, BASS_END, 3, false); // 強い方優先
        ForceUniqueBassAdjacents(blend, band, EDGE_LO, BASS_END);
        RefineToLocalPeaksInBand(blend, band, count, EDGE_LO, BASS_END, 1);
        // 低域DC/サブオーディオ裾の除去: 基音は「下隣の半音より強い」真の局所ピークで
        // あること。すぐ下の鍵(EDGE_LO 未満のサブオーディオ帯を含む)が同等以上に強ければ、
        // それは基音ではなく DC 方向へ単調増加する漏れ裾の縁にすぎない。裾は下ほど強いので
        // 縁を落とすと1つ上へ連鎖し、真の局所ピーク(下より強い鍵)が現れた所で自然に止まる。
        // RefineToLocalPeaksInBand は帯域下端より下を見ないため、この判定を別途行う。
        // オクターブ0(C0–B0 = key12–23, 約16–31Hz)は 185ms 窓では半音間隔(<2Hz)が
        // 分解能(約5.4Hz)を大きく下回り、原理的に音程を分離できない。PSG/ピアノの
        // 最低音 A0 以下でもあり、実質 DC 漏れ裾の常時点灯源にしかならないため検出しない。
        for (int i = EDGE_LO; i < MUSIC_LOW_FLOOR && i < BASS_END; ++i)
            band[i] = false;
        // それ以上(オクターブ1〜)は「下隣の半音より強い」真の局所ピークのみ採用する。
        // すぐ下の鍵が同等以上に強ければ、それは基音でなく DC 方向へ単調増加する漏れ裾の
        // 縁にすぎない。係数 0.98 は低音側の分解能不足で隣接鍵がほぼ同値になる分の許容。
        for (int i = MUSIC_LOW_FLOOR; i < BASS_END; ++i) {
            if (!band[i]) continue;
            if (blend[i - 1] >= blend[i] * 0.98f)
                band[i] = false;
        }
        MergeBandPicks(outPicked, band, EDGE_LO, BASS_END);

        // ベース倍音のソフト減衰は高音の欠落（低音に食われる）を招くため廃止。
        // ゴースト除去は IsHarmonicGhostPartial に任せる。
        // float upperSpec[COUNT];
        // memcpy(upperSpec, gated, sizeof(upperSpec));
        // SoftAttenuateBassHarmonics(outPicked, upperSpec, count);

        // ---- 2) 低中 (C3–C4): 弦の支えなど ----
        // 音数制限を大幅に緩和（実質無制限）
        PickFundamentalNotesRange(gated, band, count, BASS_END, C4_KEY, 16, pickLowMidRel);
        CollapseNearbyPicks(blend, band, BASS_END, C4_KEY, 3, false);
        MergeBandPicks(outPicked, band, BASS_END, C4_KEY);

        // ---- 3) メロディ帯 C4–C6: 枠を多め・閾値緩め（ピアノ本命）----
        PickFundamentalNotesRange(gated, band, count, C4_KEY, O5_HI, 24, pickMelodyRel);
        // 帯域内 salience で取れなかったアタックを onset で救出（ゴースト形は除外）
        if (onset && prevOnset) {
            for (int i = C4_KEY; i < O5_HI; ++i) {
                if (band[i]) continue;
                if (gated[i] < AbsFloorForKey(i, absNoiseFloor) * 1.5f) continue;
                if (!IsStrictLocalPeak(gated, i, C4_KEY, O5_HI)) continue;
                if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END)) continue;
                if (OnsetSupportsPickInBand(onset, prevOnset, i, C4_KEY, O5_HI, scale, onsetDeltaScale))
                    band[i] = true;
            }
        }
        CollapseNearbyPicks(blend, band, C4_KEY, O5_HI, 2, false);
        MergeBandPicks(outPicked, band, C4_KEY, O5_HI);

        // ---- 4) 高音 C6–C7: 控えめ ----
        PickFundamentalNotesRange(gated, band, count, O5_HI, EDGE_HI, 16, pickTreRel);
        for (int i = O5_HI; i < EDGE_HI; ++i) {
            if (!band[i]) continue;
            if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END) &&
                !PianoKey::HasOwnOvertoneSupport(blend, i, count, 0.18f))
                band[i] = false;
        }
        MergeBandPicks(outPicked, band, O5_HI, EDGE_HI);

        // ---- 5) 最高音 C7–B7: 通常曲の高音メロディ/装飾を拾う ----
        if (EDGE_HI < count) {
            PickFundamentalNotesRange(gated, band, count, EDGE_HI, count, 16, pickTreRel);
            for (int i = EDGE_HI; i < count; ++i) {
                if (!band[i]) continue;
                if (PianoKey::IsHarmonicGhostPartial(blend, i, count, BASS_END) &&
                    !PianoKey::HasOwnOvertoneSupport(blend, i, count, 0.18f))
                    band[i] = false;
            }
            MergeBandPicks(outPicked, band, EDGE_HI, count);
        }

        // ---- 全域倍音ふるい（パルス波/矩形波対策）----
        // PSG 等のパルス波は全整数次倍音を含むため、各倍音自身も倍音列(2f,3f…)を
        // 持ち、HasOwnOvertoneSupport 等で「独立音」に誤判定されて残る。帯域別ピック
        // では取り切れないので、最後に全域で「より強い確定音の整数倍音位置にある
        // 弱いピック」を剪定する。高い方から見て、より強い下位ピック j の n:1 倍音に
        // あたる i を落とす。独立音級(親に肉薄する強さ)は残す。
        // n は h2〜h8 まで。24次まで広げるとベースの 15〜20 次としてメロディを落とす。
        // メロディ帯(C4–C6)はピアノのオクターブ重ねを守るため、明確に弱い倍音のみ対象。
        for (int i = count - 1; i > EDGE_LO; --i) {
            if (!outPicked[i]) continue;
            for (int j = i - 1; j >= EDGE_LO; --j) {
                if (!outPicked[j]) continue;
                if (blend[j] <= blend[i]) continue; // 親はより強い方のみ
                
                int n = PianoKey::GetHarmonicNCompute(i, j, 16);
                if (n == 0) continue;

                // パワー（blend）での予測減衰カーブ。振幅が1/nならパワーは1/n^2程度。
                // 余裕を持たせて、予想最大パワーを計算。
                float expectedPowerRatio = 2.0f / powf((float)n, 1.3f);
                if (n == 2 || n == 4 || n == 8) expectedPowerRatio *= 1.5f;

                if (blend[i] > blend[j] * expectedPowerRatio) continue; // 独立音級は残す

                // 倍音の予想パワー以下だった場合、独自の倍音列を持っていれば実音として救出
                bool isOctave = (n == 2 || n == 4 || n == 8 || n == 16);
                if (!isOctave && PianoKey::HasOwnOvertoneSupport(blend, i, count, 0.15f)) {
                    continue; // 実音として残す
                }

                outPicked[i] = false;
                break;
            }
        }

        // 最後にもう一度低音隣接を潰す（帯域マージ後の漏れ）
        ForceUniqueBassAdjacents(blend, outPicked, EDGE_LO, BASS_END);
        // 全域 RefineToLocalPeaks は中高音の半音和音（短2度）を落とすため廃止。
        // 各帯域の PickFundamentalNotesRange 内で isPeak 判定を行っているため不要。
    }
}
