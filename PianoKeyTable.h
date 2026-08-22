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

    // ゴースト剪定用: 高次倍音(10〜24次)まで含めた整数比判定。
    // プロファイル次元(h2..h9)とは独立。漏れ込みタワーは n>9 も普通に出る。
    static constexpr int HARMONIC_PAIR_N_MAX = 24;

    // hi / lo が n:1 の等倍音関係（±3%×n、鍵インデックスは厳密でなく周波数比）
    inline bool IsHarmonicPairCompute(int hi, int lo, int nMax = HARMONIC_N_MAX)
    {
        if (hi <= lo || lo < 0 || hi >= COUNT) return false;
        const float fh = KeyHz(hi);
        const float fl = KeyHz(lo);
        if (fl <= 1e-3f) return false;
        const float ratio = fh / fl;
        const int nHi = (nMax < HARMONIC_N_MIN) ? HARMONIC_N_MIN : nMax;
        for (int n = HARMONIC_N_MIN; n <= nHi; ++n) {
            const float e = (float)n;
            if (fabsf(ratio - e) < 0.028f * e)
                return true;
        }
        return false;
    }

    inline int GetHarmonicNCompute(int hi, int lo, int nMax = HARMONIC_N_MAX)
    {
        if (hi <= lo || lo < 0 || hi >= COUNT) return 0;
        const float fh = KeyHz(hi);
        const float fl = KeyHz(lo);
        if (fl <= 1e-3f) return 0;
        const float ratio = fh / fl;
        const int nHi = (nMax < HARMONIC_N_MIN) ? HARMONIC_N_MIN : nMax;
        for (int n = HARMONIC_N_MIN; n <= nHi; ++n) {
            const float e = (float)n;
            if (fabsf(ratio - e) < 0.035f * e)
                return n;
        }
        return 0;
    }

    // 基音候補 fundKey の n 次倍音に最も近い鍵（n は 2 以上、HARMONIC_N_MAX 外も可）
    inline int HarmonicDownKeyAny(int partialKey, int harmonicN)
    {
        if (partialKey < 0 || partialKey >= COUNT || harmonicN < 2) return -1;
        return NearestKeyIndex(KeyHz(partialKey) / (float)harmonicN);
    }

    inline int HarmonicUpKeyAny(int fundKey, int harmonicN)
    {
        if (fundKey < 0 || fundKey >= COUNT || harmonicN < 2) return -1;
        return NearestKeyIndex(KeyHz(fundKey) * (float)harmonicN);
    }

    // 検出パイプラインの O(n^2) ループで多用されるため事前計算テーブル化（結果は不変）。
    // テーブルは h2..h9（従来互換）。高次は IsHarmonicPairExtended を使う。
    inline bool IsHarmonicPair(int hi, int lo)
    {
        static const bool* const tbl = []() -> const bool* {
            static bool t[COUNT * COUNT];
            for (int a = 0; a < COUNT; ++a)
                for (int b = 0; b < COUNT; ++b)
                    t[a * COUNT + b] = IsHarmonicPairCompute(a, b, HARMONIC_N_MAX);
            return t;
        }();
        if (hi <= lo || lo < 0 || hi >= COUNT) return false;
        return tbl[hi * COUNT + lo];
    }

    inline bool IsHarmonicPairExtended(int hi, int lo)
    {
        return IsHarmonicPairCompute(hi, lo, HARMONIC_PAIR_N_MAX);
    }

    // candidate が、より強い下側ピークの整数倍音として説明できるか（漏れ込みゴースト判定）。
    // parentMustBePeak: 親が局所ピークであることまで要求（平坦ノイズ床での誤爆防止）
    inline bool IsPartialOfStrongerLower(const float* st, int candidate, int count,
        float parentMinRatio = 0.55f, float upperMaxRatio = 1.05f, bool parentMustBePeak = true)
    {
        if (!st || candidate <= 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 1e-8f) return false;

        for (int n = HARMONIC_N_MIN; n <= HARMONIC_PAIR_N_MAX; ++n) {
            const int lo = HarmonicDownKeyAny(candidate, n);
            if (lo < 0 || lo >= candidate) continue;
            if (!IsHarmonicPairExtended(candidate, lo)) continue;

            const float loSc = st[lo];
            if (loSc < sc * parentMinRatio) continue;
            if (sc > loSc * upperMaxRatio) continue; // 上が明らかに強い → 独立メロディ寄り

            if (parentMustBePeak) {
                if (lo > 0 && st[lo - 1] > loSc) continue;
                if (lo + 1 < count && st[lo + 1] > loSc) continue;
            }
            return true;
        }
        return false;
    }

    // 候補自身が基音らしい倍音列を持つか（オクターブ重ねメロディ保護用）
    inline bool HasOwnOvertoneSupport(const float* st, int candidate, int count,
        float minRatio = 0.14f)
    {
        if (!st || candidate < 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 1e-8f) return false;
        float own = 0.0f;
        const int h2 = HarmonicUpKeyAny(candidate, 2);
        const int h3 = HarmonicUpKeyAny(candidate, 3);
        if (h2 >= 0 && h2 < count) own += st[h2];
        if (h3 >= 0 && h3 < count) own += st[h3] * 0.70f;
        return own >= sc * minRatio;
    }

    // 漏れ込みゴースト判定。
    // [重要] n は 2〜8 のみ。n=9〜24 まで広げると O5 主旋律がベースの
    // 15〜20次倍音として誤認され食われる（ガウバン参上 0〜10秒で確認）。
    // 実害のある漏れ込みはほぼ h2〜h6（オクターブ〜2オクターブ＋α）。
    // bassBandEnd: 低音帯の終端(PianoRoll108::BASS_END を渡す)
    inline bool IsHarmonicGhostPartial(const float* st, int candidate, int count,
        int bassBandEnd = 36)
    {
        if (!st || candidate <= 0 || candidate >= count) return false;
        const float sc = st[candidate];
        if (sc <= 1e-8f) return false;

        static constexpr int kGhostHarmonicNMax = 8;

        int bandLo = 0, bandHi = count;
        if (candidate < bassBandEnd) {
            bandLo = 0; bandHi = bassBandEnd;
        }
        else if (candidate < 60) {
            bandLo = bassBandEnd; bandHi = 60;
        }
        else if (candidate < 72) {
            bandLo = 60; bandHi = 72;
        }
        else if (candidate < 100) {
            bandLo = 72; bandHi = 100;
        }
        else {
            bandLo = 100; bandHi = count;
        }
        float bandMax = 0.0f;
        for (int i = bandLo; i < bandHi; ++i)
            if (st[i] > bandMax) bandMax = st[i];
        const bool bandProminent = (bandMax > 1e-6f && sc >= bandMax * 0.18f);

        if (HasOwnOvertoneSupport(st, candidate, count, 0.12f) && bandProminent)
            return false;

        for (int n = HARMONIC_N_MIN; n <= kGhostHarmonicNMax; ++n) {
            const int lo = HarmonicDownKeyAny(candidate, n);
            if (lo < 0 || lo >= candidate) continue;
            // n<=8 なので通常の IsHarmonicPair で足りるが、念のため Extended の
            // 計算を nMax=8 相当で行う（ペア表は h9 までなので compute 直呼び）
            if (!IsHarmonicPairCompute(candidate, lo, kGhostHarmonicNMax)) continue;

            const float loSc = st[lo];
            if (lo > 0 && st[lo - 1] > loSc) continue;
            if (lo + 1 < count && st[lo + 1] > loSc) continue;

            const bool octaveLike = (n == 2 || n == 4 || n == 8);
            const bool parentInBass = (lo < bassBandEnd);

            if (octaveLike) {
                if (parentInBass) {
                    // ベースのオクターブ重ねは「帯域またがりゴースト」になりやすい。
                    // 自帯域で十分目立ち、かつ親より明らかに強く自前倍音もあるときだけ独立音。
                    if (bandProminent && sc >= loSc * 1.12f &&
                        HasOwnOvertoneSupport(st, candidate, count, 0.14f))
                        continue;
                    if (sc <= loSc * 1.05f)
                        return true;
                    if (!bandProminent)
                        return true;
                    continue;
                }
                if (!bandProminent && sc < loSc * 0.65f)
                    return true;
            }
            else {
                // h3/h5/h6/h7: 帯域トップ級はメロディ候補として残す
                if (sc >= bandMax * 0.40f)
                    continue;
                if (loSc >= sc * 0.55f && sc <= loSc * 0.90f)
                    return true;
            }
        }
        return false;
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
