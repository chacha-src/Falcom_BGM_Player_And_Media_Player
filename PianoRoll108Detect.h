#pragma once
// 108鍵簡易ピアノロール検出
//
// 採択の本体は PianoRollSalience（白色化 + 反復倍音減算）。ここは
// 帯域ごとのしきい値とチューニング設定の対応付け、および前後処理を持つ。
//
// 旧実装は帯域ごとに枠数と「帯域最大値に対する相対比」でピックし、その後
// ベース倍音の減衰・ゴースト判定・全域倍音ふるいを重ねがけしていた。
// どの段も blend（＝振幅の2乗）の絶対量を直接比べていたため、低音と中高音で
// 桁が違い、ベースが鳴っている間は中高音がどこかの段で必ず落とされ、
// ベースが止まると基準が下がって今度はノイズが載る、という振動を起こしていた。
//
// 公開 API / 定数名は CPianoRoll.cpp 互換を維持する。
#include <algorithm>
#include <cmath>
#include <cstring>
#include "NoteFundamentalPick.h"
#include "PianoKeyTable.h"
#include "PianoRollPick.h"
#include "PianoRollSalience.h"

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

    // 帯域ごとの既定サリエンスしきい値[dB]と、対応するチューニング項目の既定値。
    // ユーザ設定は「既定値からの倍率」としてしきい値に掛かる（従来の相対比率
    // パラメータと向きが同じ: 値を上げると厳しくなる）。
    inline void SalienceThreshForKey(int i, float pickBassRel, float pickLowMidRel,
        float pickMelodyRel, float pickTreRel, float scale, float* outT)
    {
        float base, rel, defRel;
        if (i < BASS_END) { base = 11.0f; rel = pickBassRel; defRel = 0.28f; }
        else if (i < C4_KEY) { base = 9.0f; rel = pickLowMidRel; defRel = 0.20f; }
        else if (i < O5_HI) { base = 7.0f; rel = pickMelodyRel; defRel = 0.10f; }
        else if (i < EDGE_HI) { base = 7.0f; rel = pickTreRel; defRel = 0.22f; }
        // C7 以上は倍音が鍵盤範囲外に出て倍音列で裏を取れず、逆に下の音の
        // 高次倍音の吹き溜まりになる。基音として採るには強い根拠を要求する。
        else { base = 13.0f; rel = pickTreRel; defRel = 0.22f; }
        if (defRel <= 1e-6f) defRel = 1.0f;
        float t = base * (rel / defRel) / scale;
        if (t < 3.0f) t = 3.0f;
        if (t > 45.0f) t = 45.0f;
        *outT = t;
    }

    // 1フレーム分の基音採択。
    //
    // 旧実装は帯域ごとに「帯域最大値に対する相対比」でピックし、そのあと
    // ベース倍音の減衰・ゴースト判定・全域倍音ふるいを重ねがけしていた。
    // どの段も blend（＝振幅の2乗）の絶対量を直接比べていたため、ベースが
    // 鳴っている間は中高音がどの段かで必ず落とされ、ベースが止まると
    // 基準が下がって今度はノイズが載る、という振動を起こしていた。
    //
    // 新実装は PianoRollSalience の白色化＋反復倍音減算 1本に統一する。
    // ゴーストは「採った基音の倍音ぶんを引く」ことで消えるので、比率による
    // 個別のゴースト規則を持たない。
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
        if (scale < 0.70f) scale = 0.70f;
        if (scale > 1.10f) scale = 1.10f;

        // blend は振幅の2乗（ApplyDetectScale = amp^2 * 5）。比率の議論を
        // まともにするため振幅ドメインへ戻す。
        float amp[COUNT], absFloorAmp[COUNT];
        for (int i = 0; i < count; ++i) {
            const float v = blend[i];
            amp[i] = (v > 0.0f) ? sqrtf(v) : 0.0f;
            absFloorAmp[i] = sqrtf(AbsFloorForKey(i, absNoiseFloor));
        }

        PianoRollSalience::PickParams pp;
        // オクターブ0(C0–B0 = key12–23, 約16–31Hz)は 185ms 窓では半音間隔(<2Hz)が
        // 分解能(約5.4Hz)を大きく下回り、原理的に音程を分離できない。PSG/ピアノの
        // 最低音 A0 以下でもあり、実質 DC 漏れ裾の常時点灯源にしかならない。
        pp.pickLo = MUSIC_LOW_FLOOR;
        pp.pickHi = count;
        pp.bassEnd = BASS_END;
        pp.maxNotes = 24; // 音数制限は入れず、という要望に合わせて上限を拡大
        pp.fundMinDb = 3.0f;
        
        // 説明済みスロットへのペナルティを緩和。
        // ベースの倍音より小さい実音（メロディ等）が重なった場合、減算後の残差は
        // 元の振幅の半分未満になることがよくある。ここで 0.50 を要求すると
        // 「ベースに食われる」現象が起きるため、大幅に下げる。
        pp.explainedPenalty = 1.0f;
        pp.verifyHi = EDGE_HI;

        for (int i = 0; i < count; ++i) {
            float t;
            SalienceThreshForKey(i, pickBassRel, pickLowMidRel, pickMelodyRel,
                pickTreRel, scale, &t);
            
            float uMin = 0.15f;
            // 自分のアタックを持つ鍵は倍音の追従では説明できないので通しやすくする。
            if (onset && prevOnset &&
                OnsetSupportsPickInBand(onset, prevOnset, i, pp.pickLo, pp.pickHi,
                    scale, onsetDeltaScale)) {
                t *= 0.60f;
                // アタックがあるなら引き残り比率の制約も免除する
                uMin = 0.0f; 
            }
            pp.salThresh[i] = t;
            pp.unexplainedMin[i] = uMin;
        }

        PianoRollSalience::PickIterative(amp, absFloorAmp, outPicked, count, pp);

        // 低音は半音分解能が足りないので、またぎが残っていれば強い方1本にする。
        // （PickIterative 側でも隣接は塞いでいるが、範囲指定が違うので保険）
        ForceUniqueBassAdjacents(blend, outPicked, EDGE_LO, BASS_END);
        // ここで全域 RefineToLocalPeaks は掛けない。あれは「隣り合って採られた
        // 2音のうち弱いほうを消す」ので、中高音の半音和音（短2度）が必ず片方
        // 落ちる。しかも判定材料が減算前の blend なので、ベース倍音に覆われて
        // いた実音を採れるようにした今の採択と噛み合わない。局所ピーク性は
        // PickIterative が残差スペクトル上で毎回確認している。
    }
}
