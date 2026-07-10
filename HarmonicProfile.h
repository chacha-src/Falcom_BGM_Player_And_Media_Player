#pragma once
// HarmonicProfile.h
//
// 「生波形のテーブル一致」ではなく、基音に対する倍音2〜9次の相対エネルギー比率
// (ピッチにも位相にも依存しない「音色の形」)をテンプレート化し、観測値との
// 類似度で音色を分類するモジュール。
//
// [重要・お断り] ここに並んでいる各プロファイルの具体的な数値は、実測データから
// 較正したものではなく、一般的な楽器の傾向から見積もった「それらしい初期値」に
// 過ぎない。真に有効なテンプレートにするには、実際の音源から倍音比率を測定して
// 較正する作業が本来必要であり、これは最終的にはお姫様の耳での確認が要る。
// このモジュールは単体では判定を確定させず、既存のゴースト判定(CPianoRoll.cpp)へ
// 「もう一つの参考情報」を追加するだけの位置づけとする。
//
// 設計メモ:
// - 調波楽器は「滑らかな減衰 / 奇数強調 / 1/n 系」など形が違うので、種類を増やして
//   実音がノイズ側に誤分類されないようにする。
// - ノイズ/打楽器/漏れ込みは「平坦・不規則・低次だけ異常に強い」など、調波減衰と
//   直交する形を別テンプレとして持つ。種類が足りないとコサイン類似度が曖昧になり、
//   ゴースト除去に効かない。
// - LooksLikeNoiseProfile は「最良がノイズ」だけでなく、最良ノイズが最良楽器を
//   明確に上回るときだけ true（誤殺防止）。

#include <cmath>
#include <cstdint>
#include "PianoKeyTable.h"

namespace HarmonicProfile
{
    static constexpr int kHarmonicDims = 8; // 2次〜9次倍音(PianoKey::HARMONIC_N_MIN..MAX)

    struct Profile
    {
        const char* name;
        float ratio[kHarmonicDims]; // 基音を1.0とした時の各倍音の相対エネルギー比率(目安値)
        bool  isNoiseLike;          // true: ディストーション/ドラム等の広帯域ノイズ的プロファイル
    };

    // ---------------------------------------------------------------------------
    // 調波楽器側: 実音がここに落ちるように形を分散させる
    // ノイズ側: 平坦・不規則・打楽器・漏れ込み。isNoiseLike=true
    // 値は相対形が重要（コサイン類似度はスケール不変）。絶対レベルは揃える必要なし。
    // ---------------------------------------------------------------------------
    static constexpr Profile kProfiles[] = {
        // name                 h2     h3     h4     h5     h6     h7     h8     h9      noise?

        // --- ほぼ正弦 / サブ ---
        { "PureTone",         { 0.04f, 0.015f,0.01f, 0.008f,0.006f,0.005f,0.004f,0.003f}, false },
        { "SubBass",          { 0.18f, 0.06f, 0.03f, 0.015f,0.01f, 0.008f,0.006f,0.004f}, false },

        // --- 鍵盤・撥弦 ---
        { "PianoBright",      { 0.62f, 0.38f, 0.24f, 0.16f, 0.11f, 0.08f, 0.055f,0.035f}, false },
        { "PianoSoft",        { 0.42f, 0.22f, 0.12f, 0.07f, 0.045f,0.03f, 0.02f, 0.012f}, false },
        { "ElectricPiano",    { 0.48f, 0.55f, 0.18f, 0.28f, 0.12f, 0.16f, 0.08f, 0.10f }, false }, // ベル寄り奇数
        { "HarpPluck",        { 0.70f, 0.42f, 0.28f, 0.16f, 0.10f, 0.055f,0.035f,0.02f }, false },
        { "GuitarNylon",      { 0.52f, 0.30f, 0.22f, 0.14f, 0.10f, 0.07f, 0.05f, 0.03f }, false },
        { "GuitarSteel",      { 0.58f, 0.36f, 0.28f, 0.18f, 0.14f, 0.10f, 0.075f,0.05f }, false },
        { "PluckMuted",       { 0.35f, 0.14f, 0.08f, 0.04f, 0.025f,0.015f,0.01f, 0.008f}, false },

        // --- 擦弦・管・リード ---
        { "BowedString",      { 0.48f, 0.38f, 0.32f, 0.26f, 0.22f, 0.18f, 0.15f, 0.12f }, false },
        { "CelloWarm",        { 0.55f, 0.40f, 0.28f, 0.20f, 0.14f, 0.10f, 0.07f, 0.05f }, false },
        { "BrassOpen",        { 0.58f, 0.48f, 0.40f, 0.34f, 0.28f, 0.22f, 0.18f, 0.14f }, false },
        { "BrassMuted",       { 0.40f, 0.28f, 0.18f, 0.12f, 0.08f, 0.05f, 0.035f,0.02f }, false },
        { "FluteAir",         { 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.015f,0.01f, 0.008f}, false },
        { "ClarinetOdd",      { 0.08f, 0.55f, 0.08f, 0.38f, 0.07f, 0.26f, 0.06f, 0.18f }, false },
        { "OrganFlute",       { 0.10f, 0.58f, 0.10f, 0.40f, 0.10f, 0.30f, 0.10f, 0.22f }, false },
        { "SaxReed",          { 0.45f, 0.50f, 0.28f, 0.32f, 0.18f, 0.22f, 0.12f, 0.14f }, false },

        // --- 合成波形の基本形（ゲーム曲・シンセ伴奏向け） ---
        { "SawWave",          { 0.50f, 0.33f, 0.25f, 0.20f, 0.17f, 0.14f, 0.125f,0.11f }, false }, // ~1/n
        { "SquareOdd",        { 0.05f, 0.33f, 0.05f, 0.20f, 0.05f, 0.14f, 0.05f, 0.11f }, false },
        { "TriangleOdd",      { 0.04f, 0.11f, 0.03f, 0.04f, 0.02f, 0.02f, 0.015f,0.012f}, false },
        { "SoftPad",          { 0.38f, 0.28f, 0.20f, 0.14f, 0.10f, 0.07f, 0.05f, 0.035f}, false },
        { "ChoirAh",          { 0.42f, 0.36f, 0.22f, 0.30f, 0.14f, 0.18f, 0.10f, 0.12f }, false },

        // --- 金属・打鍵（調波寄り。ノイズ扱いしない） ---
        { "BellPartial",      { 0.35f, 0.55f, 0.22f, 0.45f, 0.16f, 0.30f, 0.12f, 0.18f }, false },
        { "Marimba",          { 0.25f, 0.08f, 0.45f, 0.06f, 0.04f, 0.12f, 0.03f, 0.02f }, false }, // 強い4次寄り
        { "Vibraphone",       { 0.40f, 0.18f, 0.32f, 0.12f, 0.20f, 0.08f, 0.10f, 0.05f }, false },

        // --- ノイズ / 打楽器 / 歪み / 漏れ込み（ゴースト除去側） ---
        // 平坦広帯域
        { "BroadbandNoise",   { 0.92f, 0.88f, 0.90f, 0.84f, 0.86f, 0.80f, 0.82f, 0.76f }, true  },
        { "PinkishNoise",     { 0.85f, 0.72f, 0.68f, 0.58f, 0.55f, 0.48f, 0.45f, 0.40f }, true  },
        { "WhiteAir",         { 0.95f, 0.95f, 0.94f, 0.93f, 0.92f, 0.91f, 0.90f, 0.89f }, true  },
        // 打楽器
        { "KickBody",         { 0.55f, 0.22f, 0.12f, 0.18f, 0.25f, 0.30f, 0.28f, 0.22f }, true  }, // 低次+高次ノイズ床
        { "SnareBody",        { 0.70f, 0.55f, 0.60f, 0.50f, 0.58f, 0.48f, 0.52f, 0.42f }, true  },
        { "HiHatCymbal",      { 0.75f, 0.78f, 0.82f, 0.80f, 0.85f, 0.83f, 0.88f, 0.86f }, true  }, // 高次が落ちない
        { "TomResonance",     { 0.60f, 0.45f, 0.35f, 0.40f, 0.32f, 0.38f, 0.30f, 0.28f }, true  },
        { "PercClick",        { 0.40f, 0.55f, 0.35f, 0.50f, 0.45f, 0.48f, 0.42f, 0.40f }, true  },
        // 歪み・金属ノイズ
        { "DistortedStack",   { 0.85f, 0.80f, 0.78f, 0.72f, 0.70f, 0.65f, 0.62f, 0.58f }, true  },
        { "MetalClang",       { 0.55f, 0.70f, 0.40f, 0.65f, 0.35f, 0.60f, 0.30f, 0.55f }, true  }, // 山谷が激しい
        { "InharmonicSpray",  { 0.30f, 0.70f, 0.25f, 0.65f, 0.20f, 0.60f, 0.18f, 0.55f }, true  },
        // 倍音漏れ・ゴースト特有の「基音として見たときの歪んだ形」
        { "GhostLeakage",     { 0.95f, 0.25f, 0.80f, 0.20f, 0.70f, 0.18f, 0.60f, 0.15f }, true  }, // 偶数次だけ異常
        { "UpperPartialOnly", { 0.15f, 0.12f, 0.70f, 0.10f, 0.55f, 0.08f, 0.45f, 0.06f }, true  }, // 高次だけ残る
        { "SparseSpike",      { 0.10f, 0.85f, 0.08f, 0.10f, 0.75f, 0.08f, 0.10f, 0.65f }, true  }, // 飛び飛び
        { "FloorRumble",      { 0.90f, 0.40f, 0.55f, 0.35f, 0.50f, 0.30f, 0.45f, 0.28f }, true  },
    };
    static constexpr int kProfileCount = (int)(sizeof(kProfiles) / sizeof(kProfiles[0]));

    // ノイズ判定: 最良ノイズが最良楽器をどれだけ上回れば採用するか
    static constexpr float kNoiseOverMusicalMargin = 0.035f;

    // コサイン類似度(0〜1、全成分非負なので負値にはならない想定)
    inline float CosineSimilarity(const float* a, const float* b, int n)
    {
        float dot = 0.0f, na = 0.0f, nb = 0.0f;
        for (int i = 0; i < n; ++i) {
            dot += a[i] * b[i];
            na += a[i] * a[i];
            nb += b[i] * b[i];
        }
        if (na <= 1e-9f || nb <= 1e-9f) return 0.0f;
        return dot / (sqrtf(na) * sqrtf(nb));
    }

    // candidate の実測倍音比率ベクトルを組み立てる。
    // blend: 検出用スペクトル(既存の blend[] をそのまま渡す想定)
    inline void BuildObservedRatios(const float* blend, int candidate, int count, float* outRatios)
    {
        for (int i = 0; i < kHarmonicDims; ++i) outRatios[i] = 0.0f;
        if (!blend || candidate < 0 || candidate >= count) return;
        const float f = blend[candidate];
        if (f <= 1e-6f) return;
        for (int n = PianoKey::HARMONIC_N_MIN; n <= PianoKey::HARMONIC_N_MAX; ++n) {
            const int hk = PianoKey::HarmonicUpKey(candidate, n);
            const int slot = n - PianoKey::HARMONIC_N_MIN;
            if (hk >= 0 && hk < count && hk != candidate)
                outRatios[slot] = blend[hk] / f;
        }
    }

    // 観測ベクトルに十分な倍音エネルギーがあるか（全滅なら分類しない）
    inline bool HasUsableHarmonicEnergy(const float* ratios)
    {
        if (!ratios) return false;
        float sum = 0.0f;
        for (int i = 0; i < kHarmonicDims; ++i)
            sum += ratios[i];
        return sum >= 0.08f;
    }

    // 最も近いプロファイルを分類する。
    // outConfidence: 最良一致のコサイン類似度(0〜1、高いほど確信度が高い)
    // outIsNoiseLike: 最良一致がノイズ系プロファイルかどうか
    // 戻り値: 一致したプロファイルの index(該当なしなら -1)
    inline int Classify(const float* blend, int candidate, int count,
        float* outConfidence, bool* outIsNoiseLike)
    {
        float observed[kHarmonicDims];
        BuildObservedRatios(blend, candidate, count, observed);
        if (!HasUsableHarmonicEnergy(observed)) {
            if (outConfidence) *outConfidence = 0.0f;
            if (outIsNoiseLike) *outIsNoiseLike = false;
            return -1;
        }

        int best = -1;
        float bestScore = -1.0f;
        for (int p = 0; p < kProfileCount; ++p) {
            const float s = CosineSimilarity(observed, kProfiles[p].ratio, kHarmonicDims);
            if (s > bestScore) {
                bestScore = s;
                best = p;
            }
        }
        if (outConfidence) *outConfidence = (bestScore < 0.0f) ? 0.0f : bestScore;
        if (outIsNoiseLike) *outIsNoiseLike = (best >= 0) ? kProfiles[best].isNoiseLike : false;
        return best;
    }

    // 楽器側・ノイズ側それぞれの最良スコアを返す
    inline void ScoreMusicalVsNoise(const float* blend, int candidate, int count,
        float* outBestMusical, float* outBestNoise, int* outBestNoiseIndex)
    {
        if (outBestMusical) *outBestMusical = 0.0f;
        if (outBestNoise) *outBestNoise = 0.0f;
        if (outBestNoiseIndex) *outBestNoiseIndex = -1;

        float observed[kHarmonicDims];
        BuildObservedRatios(blend, candidate, count, observed);
        if (!HasUsableHarmonicEnergy(observed)) return;

        float bestM = 0.0f, bestN = 0.0f;
        int bestNi = -1;
        for (int p = 0; p < kProfileCount; ++p) {
            const float s = CosineSimilarity(observed, kProfiles[p].ratio, kHarmonicDims);
            if (kProfiles[p].isNoiseLike) {
                if (s > bestN) {
                    bestN = s;
                    bestNi = p;
                }
            }
            else if (s > bestM) {
                bestM = s;
            }
        }
        if (outBestMusical) *outBestMusical = bestM;
        if (outBestNoise) *outBestNoise = bestN;
        if (outBestNoiseIndex) *outBestNoiseIndex = bestNi;
    }

    // 簡易ヘルパー: 「ノイズ系プロファイルに、十分な確信度で一致したか」だけを返す。
    // minConfidence: これ未満の類似度なら「該当なし」として false を返す
    // (倍音がほとんど無い/測定不能な場合に誤ってノイズ扱いしないためのガード)。
    // 加えて、最良ノイズが最良楽器を kNoiseOverMusicalMargin 以上上回るときだけ true。
    inline bool LooksLikeNoiseProfile(const float* blend, int candidate, int count,
        float minConfidence = 0.75f)
    {
        float bestM = 0.0f, bestN = 0.0f;
        ScoreMusicalVsNoise(blend, candidate, count, &bestM, &bestN, nullptr);
        if (bestN < minConfidence) return false;
        return bestN >= bestM + kNoiseOverMusicalMargin;
    }
}
