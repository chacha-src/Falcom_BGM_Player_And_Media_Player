#pragma once
// 108鍵（MIDI 0…107）の等律周波数と倍音キー対応。A0=21, C8=108 は範囲内。

#include <cmath>

namespace PianoKey
{
    static constexpr int COUNT = 108;
    static constexpr int MIDI_BASE = 0;
    // 88鍵(A0=21起)時代の帯域境界を MIDI 絶対値で維持（108鍵化で index だけ変えず残っていた不整合を解消）
    // 旧 BAND_BASS_END=46 → MIDI 21..66, 旧 BAND_MID_END=73 → MIDI 67..93
    static constexpr int BASS_BAND_END = 67;  // [0,67) 低音 Goertzel 長窓 + 低音ピック
    static constexpr int MID_BAND_END = 94;   // [67,94) 中音, [94,COUNT) 高音
    // 旧88鍵 LOW_KEY_SPLIT=50 + MIDI21 → MIDI71(B4)。高音は 4096 Blackman 窓。
    static constexpr int TREBLE_WIN_START = 71;
    static constexpr int HARMONIC_N_MIN = 2;
    static constexpr int HARMONIC_N_MAX = 9;
    static constexpr int HARMONIC_COUNT = HARMONIC_N_MAX - HARMONIC_N_MIN + 1;

    inline float MidiToHz(int midi)
    {
        return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
    }

    inline float KeyHz(int keyIndex)
    {
        if (keyIndex < 0) return MidiToHz(MIDI_BASE);
        if (keyIndex >= COUNT) return MidiToHz(MIDI_BASE + COUNT - 1);
        return MidiToHz(MIDI_BASE + keyIndex);
    }

    // 108鍵フルレンジ: キー index をそのまま使う（88鍵時代の -12 オフセットは廃止）
    inline int GoertzelScaleKeyIndex(int keyIndex)
    {
        if (keyIndex < 0) return 0;
        if (keyIndex >= COUNT) return COUNT - 1;
        return keyIndex;
    }

    inline int NearestKeyIndex(float hz)
    {
        if (hz <= 1.0f) return 0;
        int best = 0;
        float bestErr = fabsf(KeyHz(0) - hz);
        for (int i = 1; i < COUNT; ++i) {
            const float e = fabsf(KeyHz(i) - hz);
            if (e < bestErr) {
                bestErr = e;
                best = i;
            }
        }
        return best;
    }

    // hi / lo が n:1 の等倍音関係（±3%×n、鍵インデックスは厳密でなく周波数比）
    inline bool IsHarmonicPairCompute(int hi, int lo)
    {
        if (hi <= lo || lo < 0 || hi >= COUNT) return false;
        const float fh = KeyHz(hi);
        const float fl = KeyHz(lo);
        if (fl <= 1e-3f) return false;
        const float ratio = fh / fl;
        for (int n = HARMONIC_N_MIN; n <= HARMONIC_N_MAX; ++n) {
            const float e = (float)n;
            if (fabsf(ratio - e) < 0.028f * e)
                return true;
        }
        return false;
    }

    // 検出パイプラインの O(n^2) ループで多用されるため事前計算テーブル化（結果は不変）。
    // C++11 のスレッドセーフな関数ローカル static で初期化（worker/Speana 双方から安全に参照）。
    inline bool IsHarmonicPair(int hi, int lo)
    {
        static const bool* const tbl = []() -> const bool* {
            static bool t[COUNT * COUNT];
            for (int a = 0; a < COUNT; ++a)
                for (int b = 0; b < COUNT; ++b)
                    t[a * COUNT + b] = IsHarmonicPairCompute(a, b);
            return t;
        }();
        if (hi <= lo || lo < 0 || hi >= COUNT) return false;
        return tbl[hi * COUNT + lo];
    }

    inline bool IsOctaveRelated(int hi, int lo)
    {
        if (hi <= lo || lo < 0 || hi >= COUNT) return false;
        const int d = hi - lo;
        return d == 12 || d == 24 || d == 36 || d == 48;
    }

    struct HarmonicMap
    {
        int up[COUNT][HARMONIC_COUNT];
        int down[COUNT][HARMONIC_COUNT];

        HarmonicMap()
        {
            for (int i = 0; i < COUNT; ++i) {
                const float f0 = KeyHz(i);
                for (int n = HARMONIC_N_MIN; n <= HARMONIC_N_MAX; ++n) {
                    const int slot = n - HARMONIC_N_MIN;
                    up[i][slot] = NearestKeyIndex(f0 * (float)n);
                    down[i][slot] = NearestKeyIndex(f0 / (float)n);
                }
            }
        }
    };

    inline const HarmonicMap& Harmonics()
    {
        static const HarmonicMap map;
        return map;
    }

    inline int HarmonicUpKey(int fundKey, int harmonicN)
    {
        if (fundKey < 0 || fundKey >= COUNT) return -1;
        if (harmonicN < HARMONIC_N_MIN || harmonicN > HARMONIC_N_MAX) return -1;
        return Harmonics().up[fundKey][harmonicN - HARMONIC_N_MIN];
    }

    inline int HarmonicDownKey(int fundKey, int harmonicN)
    {
        if (fundKey < 0 || fundKey >= COUNT) return -1;
        if (harmonicN < HARMONIC_N_MIN || harmonicN > HARMONIC_N_MAX) return -1;
        return Harmonics().down[fundKey][harmonicN - HARMONIC_N_MIN];
    }

    // 候補が下側の基音より弱く、周波数比で倍音なら false（ゴースト倍音）
    inline bool PassesFundamentalTest(const float* st, int candidate, int count)
    {
        if (!st || candidate < 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;

        float harmEnergy = 0.0f;
        for (int n = HARMONIC_N_MIN; n <= 6; ++n) {
            const int hk = HarmonicUpKey(candidate, n);
            if (hk >= 0 && hk < count && hk != candidate)
                harmEnergy += st[hk] * (0.50f / (float)n);
        }
        if (sc < harmEnergy * 0.92f)
            return false;

        for (int n = HARMONIC_N_MIN; n <= HARMONIC_N_MAX; ++n) {
            const int lo = HarmonicDownKey(candidate, n);
            if (lo < 0 || lo >= count || lo >= candidate) continue;
            if (st[lo] >= sc * 0.78f)
                return false;
        }
        return true;
    }

    // 持続・包絡延長用（基音判定をやや緩める）
    inline bool PassesFundamentalTestSustain(const float* st, int candidate, int count)
    {
        if (!st || candidate < 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;

        float harmEnergy = 0.0f;
        for (int n = HARMONIC_N_MIN; n <= 6; ++n) {
            const int hk = HarmonicUpKey(candidate, n);
            if (hk >= 0 && hk < count && hk != candidate)
                harmEnergy += st[hk] * (0.48f / (float)n);
        }
        if (sc < harmEnergy * 0.98f)
            return false;

        for (int n = HARMONIC_N_MIN; n <= HARMONIC_N_MAX; ++n) {
            const int lo = HarmonicDownKey(candidate, n);
            if (lo < 0 || lo >= count || lo >= candidate) continue;
            if (st[lo] >= sc * 0.88f)
                return false;
        }
        return true;
    }

    // サリエンス補完用: 明らかな上倍音だけ拒否（弱い基音のFM/弦は通す）
    inline bool SalienceLooksLikeFundamental(const float* st, int candidate, int count)
    {
        if (!st || candidate < 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;
        static const int kDown[] = { 12, 19, 24, 7, 5 };
        for (int d : kDown) {
            const int lo = candidate - d;
            if (lo < 0) continue;
            if (st[lo] >= sc * 0.62f) return false;
        }
        return true;
    }

    // 中高音: 低音帯の漏れを無視し、近接音のみで倍音判定（オクターブ和音は通す）
    inline bool SalienceAboveLowBand(const float* st, int candidate, int count, int bassBandEnd)
    {
        if (!st || candidate < 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 0.0f) return false;
        static const int kDown[] = { 5, 7 };
        for (int d : kDown) {
            const int lo = candidate - d;
            if (lo < 0) continue;
            if (candidate >= bassBandEnd && lo < bassBandEnd) continue;
            if (st[lo] >= sc * 0.62f) return false;
        }
        return true;
    }

    inline bool IsHarmonicOfAnyActive(const float* st, int candidate, const bool* active,
        int bandStart, int bandEnd, int count, float strengthRatio = 0.82f)
    {
        if (!st || !active || candidate < 0 || candidate >= count) return false;
        const float sc = st[candidate];
        for (int j = bandStart; j < bandEnd; ++j) {
            if (!active[j] || j == candidate) continue;
            if (!IsHarmonicPair(candidate, j) && !IsHarmonicPair(j, candidate)) continue;
            if (st[j] >= sc * strengthRatio)
                return true;
        }
        return false;
    }
}
