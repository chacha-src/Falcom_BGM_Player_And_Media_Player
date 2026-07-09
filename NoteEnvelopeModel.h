#pragma once
// NoteEnvelopeModel.h (v2)
//
// [重要] 初版は「ピーク後の指数減衰カーブを予測し、実測との残差で再アタックを判定」
// する方式だったが、これは持続音(ハープの伸び、ストリングス等)で予測カーブが
// 実測より先にどんどん下がってしまい、残差が時間とともに際限なく拡大して
// 誤った再アタックを連鎖的に発生させる欠陥があった(1音がノイズの縦縞になる不具合)。
//
// v2 では減衰カーブ予測を完全に廃止し、代わりに:
//   1) 直近の谷(トラフ)からの実測リバウンド量
//   2) 短窓Goertzelによるオンセット検出(PianoRoll108::OnsetSupportsPick、
//      既存のチューニング済みロジックをそのまま再利用)
// の「両方が同時に成立した時だけ」再アタックと判定する。
// 片方だけでは発火しないため、持続音の自然な揺らぎだけでは暴走しない。
//
// 音色クラスは分類の参考情報としてのみ保持し、判定のゲーティングには使わない
// (前バージョンの「分類→減衰カーブ」という間接的な依存が事故の温床だったため)。

#include <cmath>
#include <cstdint>
#include <cstring>

namespace NoteEnvelope
{
    enum class Class : uint8_t
    {
        Unknown = 0,
        Percussive, // 立ち上がりが速い(ピアノ/ハープ/ベル/ドラム等)
        Sustained,  // 立ち上がりが緩やか、または長時間ほぼ一定(ストリングス/オルガン/トランペット等)
        COUNT
    };

    // attackMsMax   : オンセットからピーク到達までの時間がこれ以下なら Percussive 側に分類
    // reboundRatio  : 「直近の谷からの実測リバウンド量」が peakVal に対してこの比率以上必要
    // minRetriggerMs: 誤検出防止のための最短再トリガ間隔(msec)
    struct Preset
    {
        Class cls;
        float attackMsMax;
        float reboundRatio;
        float minRetriggerMs;
    };

    // テンポ130の16分音符 ≈ 115ms。minRetriggerMs はこれより短く、
    // かつ同一アタックの立ち上がり中に二重発火しない程度に設定する。
    // [重要] 実音源での検証がまだ不十分なため、誤発火が起きにくい方向へ
    // 意図的に保守的な値にしてある。実際に鳴らしながら、まずは
    // minRetriggerMs を大きくする/reboundRatio を上げる方向で様子を見て、
    // 逆(16分連打が拾えない)の問題が出た場合にだけ緩めることを推奨する。
    static constexpr Preset kPresets[] = {
        // cls              attackMsMax  reboundRatio  minRetriggerMs
        { Class::Percussive,      30.0f,        0.45f,          90.0f },
        { Class::Sustained,   100000.0f,        0.55f,         120.0f },
    };
    static constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
    static constexpr Preset kFallbackPreset = kPresets[0];

    struct NoteEnvelopeState
    {
        bool     wasOn = false;
        float    onsetTimeMs = 0.0f;
        float    peakVal = 0.0f;
        float    peakTimeMs = 0.0f;   // 分類(立ち上がり時間の実測)専用
        float    troughVal = 0.0f;    // 直近リセット以降の最小値(リバウンド判定の基準)
        float    lastRetriggerMs = 0.0f;
        bool     classified = false;
        Class    cls = Class::Unknown;
        int      presetIdx = 0;
        uint32_t reattackCount = 0;
        float    measuredHalfDecayMs = -1.0f; // ピークから実測値が半減するまでの実測時間(参考情報のみ)

        void ResetOn(float nowMs, float initVal)
        {
            wasOn = true;
            onsetTimeMs = nowMs;
            peakVal = initVal;
            peakTimeMs = nowMs;
            troughVal = initVal;
            lastRetriggerMs = nowMs;
            classified = false;
            cls = Class::Unknown;
            presetIdx = 0;
            reattackCount = 0;
            measuredHalfDecayMs = -1.0f;
        }

        void ResetOff()
        {
            wasOn = false;
            peakVal = 0.0f;
            troughVal = 0.0f;
            classified = false;
            cls = Class::Unknown;
            measuredHalfDecayMs = -1.0f;
        }
    };

    inline int ClassifyPreset(float attackTimeMs)
    {
        for (int i = 0; i < kPresetCount; ++i)
            if (attackTimeMs <= kPresets[i].attackMsMax)
                return i;
        return kPresetCount - 1;
    }

    // 毎フレーム呼ぶ。ノートオン継続中のみ意味がある。
    // nowMs        : 現在時刻(msec, 単調増加)
    // rawVal       : このフレームのノート強度(m_noteStrength 相当)
    // onsetSupport : このフレーム、短窓Goertzelのオンセット検出が「本物のアタックらしい」
    //                と判定したか(PianoRoll108::OnsetSupportsPick の結果をそのまま渡す)。
    //                これが false の間は、どれだけ値が揺れても再アタックとは判定しない。
    // 戻り値: このフレームで「新しいアタック」が起きたと判定したら true
    inline bool Update(NoteEnvelopeState& st, float nowMs, float rawVal, bool onsetSupport)
    {
        if (!st.wasOn)
        {
            st.ResetOn(nowMs, rawVal);
            return false;
        }

        if (rawVal > st.peakVal)
        {
            st.peakVal = rawVal;
            st.peakTimeMs = nowMs;
            st.measuredHalfDecayMs = -1.0f;
        }
        else if (st.measuredHalfDecayMs < 0.0f && st.peakVal > 1e-6f &&
            rawVal <= st.peakVal * 0.5f)
        {
            st.measuredHalfDecayMs = nowMs - st.peakTimeMs;
        }

        if (rawVal < st.troughVal)
            st.troughVal = rawVal;

        if (!st.classified)
        {
            const float sinceOnset = nowMs - st.onsetTimeMs;
            if (sinceOnset >= 40.0f)
            {
                const float attackTimeMs = st.peakTimeMs - st.onsetTimeMs;
                st.presetIdx = ClassifyPreset(attackTimeMs);
                st.cls = kPresets[st.presetIdx].cls;
                st.classified = true;
            }
        }
        const Preset& p = st.classified ? kPresets[st.presetIdx] : kFallbackPreset;

        bool reattack = false;
        if (onsetSupport)
        {
            const bool reboundOk = (st.peakVal > 1e-6f) &&
                ((rawVal - st.troughVal) >= st.peakVal * p.reboundRatio);
            const bool intervalOk = (nowMs - st.lastRetriggerMs) >= p.minRetriggerMs;
            if (reboundOk && intervalOk)
            {
                reattack = true;
                st.onsetTimeMs = nowMs;
                st.peakVal = rawVal;
                st.peakTimeMs = nowMs;
                st.troughVal = rawVal;
                st.lastRetriggerMs = nowMs;
                st.classified = false;
                ++st.reattackCount;
            }
        }

        return reattack;
    }

    // ドラム/打撃ノイズ由来のゴースト候補かどうかの簡易判定(参考情報ベース、opt-in)。
    // 「実測ハーフディケイが極端に短い(35ms未満)」を条件とする一次スクリーニングであり、
    // 単独では確定的な楽器判定ではない。誤って弱いスタッカートまで消す可能性があるため、
    // CPianoRoll 側では既定で無効にしておくことを推奨する。
    inline bool LooksImpulsive(const NoteEnvelopeState& st)
    {
        if (st.measuredHalfDecayMs < 0.0f) return false;
        return st.measuredHalfDecayMs < 35.0f;
    }
}