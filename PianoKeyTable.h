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

    // 鍵盤上端を超える倍音は「最上鍵」に丸めない。丸めると最上鍵が自分の倍音で
    // 自分を支持する自己参照になり、B7 が常時点灯する。
    inline int HarmonicKeyOnBoard(int fundKey, int harmonicN)
    {
        if (harmonicN <= 1) return fundKey;
        if (fundKey < 0 || fundKey >= COUNT || harmonicN < 2) return -1;
        const float hz = KeyHz(fundKey) * (float)harmonicN;
        if (hz > KeyHz(COUNT - 1) * 1.03f) return -1;
        const int k = HarmonicUpKeyAny(fundKey, harmonicN);
        if (k <= fundKey || k >= COUNT) return -1;
        return k;
    }

    // hi が lo の n 次倍音として鍵盤上で採用する鍵そのものであるとき n を返す。
    // 比率が「だいたい n」だけでは、隣の半音（別の旋律）まで倍音にしてしまう。
    inline int ExactHarmonicNumber(int hi, int lo, int nMax = 24)
    {
        if (hi <= lo || lo < 0 || hi >= COUNT) return 0;
        const float fl = KeyHz(lo);
        if (fl <= 1.0e-3f) return 0;
        const float ratio = KeyHz(hi) / fl;
        const int n = (int)(ratio + 0.5f);
        if (n < HARMONIC_N_MIN || n > nMax) return 0;
        if (HarmonicKeyOnBoard(lo, n) != hi) return 0;
        return n;
    }

    // 振幅ドメインの倍音包絡。 blend はパワーなので呼ぶ前に sqrt する。
    // base は n=1 換算の振幅、slope は amp(n) = base / n^slope。
    // 基音が体鳴で凹む擦弦でも、第2倍音以降の中央値から base を戻す。
    // ある次数だけ異常に大きい（別の実音が重なった）点は中央値では支配しない。
    struct AmpEnv { float base; float slope; };

    inline AmpEnv FitAmpEnv(const float* amp, int key, int count)
    {
        AmpEnv env;
        env.base = (amp && key >= 0 && key < count) ? amp[key] : 0.0f;
        if (env.base < 0.0f) env.base = 0.0f;
        env.slope = 1.05f;
        if (!amp || key < 0 || key >= count) return env;

        float implied[8];
        int m = 0;
        for (int n = 2; n <= 8; ++n) {
            const int hk = HarmonicKeyOnBoard(key, n);
            if (hk < 0 || hk >= count) continue;
            const float v = amp[hk];
            if (v < 1e-6f) continue;
            implied[m++] = v * (float)n;
        }
        if (m <= 0) {
            // 倍音が無い（正弦に近い）。高次を発明しないよう減衰は急にする。
            env.base = amp[key];
            env.slope = 1.75f;
            return env;
        }
        if (m == 1) {
            // 上の部分音が1本だけ。
            // 1/n 付近なら通常の倍音なので傾き 1 で引き切る。傾きを急にすると
            // 予測が小さくなり、第2倍音がオクターブ上の別音として残る。
            // 基音より明らかに大きい第2倍音だけは擦弦の穴として包絡に入れる。
            // 高音の弦は第3倍音が鍵盤の外に出て、見えるのが第2倍音だけになる。
            // 同程度の音量（オクターブの正弦）は急な傾きのまま残す。
            int onlyN = 0;
            float onlyV = 0.0f;
            for (int n = 2; n <= 8; ++n) {
                const int hk = HarmonicKeyOnBoard(key, n);
                if (hk < 0 || hk >= count) continue;
                if (amp[hk] < 1.0e-6f) continue;
                onlyN = n;
                onlyV = amp[hk];
            }
            env.base = amp[key];
            env.slope = 1.75f;
            if (amp[key] > 1.0e-6f && onlyN >= 2) {
                const float harm = amp[key] / (float)onlyN;
                if (onlyN == 2 && onlyV > amp[key] * 1.15f) {
                    env.base = onlyV * 2.0f;
                    env.slope = 1.0f;
                    if (amp[key] > env.base) env.base = amp[key];
                }
                else if (onlyV <= harm * 1.8f)
                    env.slope = 1.0f;
            }
            return env;
        }
        for (int i = 1; i < m; ++i) {
            const float v = implied[i];
            int j = i;
            while (j > 0 && implied[j - 1] > v) { implied[j] = implied[j - 1]; --j; }
            implied[j] = v;
        }
        env.base = implied[m / 2];

        float eSum = 0.0f;
        int eN = 0;
        for (int n = 2; n <= 8; ++n) {
            const int hk = HarmonicKeyOnBoard(key, n);
            if (hk < 0 || hk >= count) continue;
            const float v = amp[hk];
            if (v < 1e-6f || env.base <= v) continue;
            const float pred1 = env.base / (float)n;
            if (v > pred1 * 2.6f || v < pred1 * 0.12f) continue;
            eSum += logf(env.base / v) / logf((float)n);
            ++eN;
        }
        if (eN > 0) env.slope = eSum / (float)eN;
        if (env.slope < 0.55f) env.slope = 0.55f;
        if (env.slope > 1.80f) env.slope = 1.80f;

        m = 0;
        for (int n = 2; n <= 8; ++n) {
            const int hk = HarmonicKeyOnBoard(key, n);
            if (hk < 0 || hk >= count) continue;
            const float v = amp[hk];
            if (v < 1e-6f) continue;
            implied[m++] = v * powf((float)n, env.slope);
        }
        if (m > 0) {
            for (int i = 1; i < m; ++i) {
                const float v = implied[i];
                int j = i;
                while (j > 0 && implied[j - 1] > v) { implied[j] = implied[j - 1]; --j; }
                implied[j] = v;
            }
            env.base = implied[m / 2];
        }
        // 基音が倍音からの外挿より十分あるなら、そちらも採用して過小推定を避ける。
        if (amp[key] > env.base) env.base = amp[key];
        return env;
    }

    inline float PredictHarmonicAmp(const AmpEnv& env, int harmonicN)
    {
        if (harmonicN < 1 || env.base <= 0.0f) return 0.0f;
        if (harmonicN == 1) return env.base;
        return env.base / powf((float)harmonicN, env.slope);
    }

    // 候補の奇数次（3,5,7）が、親の包絡では説明できない大きさか。
    // オクターブ上の実音は親の偶数次としか重ならないので、奇数次の余りが独立音の証拠。
    inline bool OddPartialExceedsParent(const float* amp, int note, int parent, int count)
    {
        if (!amp || note < 0 || parent < 0 || note >= count || parent >= count) return false;
        const AmpEnv env = FitAmpEnv(amp, parent, count);
        for (int n = 3; n <= 7; n += 2) {
            const int hk = HarmonicKeyOnBoard(note, n);
            if (hk < 0 || hk >= count) continue;
            const float ratio = KeyHz(hk) / KeyHz(parent);
            const int pn = (int)(ratio + 0.5f);
            float pred = 0.0f;
            if (pn >= 2 && fabsf(ratio - (float)pn) <= 0.05f * (float)pn)
                pred = PredictHarmonicAmp(env, pn);
            // 親の系列に乗らないビンは、予測 0 と比べるとどんな残差も「超過」になる。
            // 独立した音の証拠は、候補自身に対して大きい局所ピークだけ。
            if (pred > 1.0e-8f) {
                if (amp[hk] > pred * 1.75f && amp[hk] > amp[note] * 0.15f)
                    return true;
            }
            else if (amp[hk] > amp[note] * 0.45f) {
                if (hk > 0 && amp[hk - 1] > amp[hk]) continue;
                if (hk + 1 < count && amp[hk + 1] >= amp[hk]) continue;
                return true;
            }
        }
        return false;
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
        const int h2 = HarmonicKeyOnBoard(candidate, 2);
        const int h3 = HarmonicKeyOnBoard(candidate, 3);
        if (h2 >= 0 && h2 < count) own += st[h2];
        if (h3 >= 0 && h3 < count) own += st[h3] * 0.70f;
        return own >= sc * minRatio;
    }

    // 親の整数倍音として振幅包絡で説明できるか。
    // 入力 st は検出スペクトル（パワー = 振幅^2）。比較は振幅に戻してから行う。
    // 「親がより大きい」「帯域最大の何割」では、小さい高音も弦の倍音も全部ゴーストになる。
    // 逆に、矩形波の倍音は自分の倍音列を持つので「自前の倍音がある」だけでは救えず、
    // 奇数次が親の予測を超えるかで独立音と倍音を分ける。
    // 候補が予測の kPartialExplain 倍を超える、または奇数次が余るなら実音。
    // bassBandEnd は旧帯域ヒューリスティックの名残。判定には使わない（API 互換）。
    static constexpr float kPartialExplain = 1.48f;
    static constexpr int kGhostNMax = 18;

    inline bool IsHarmonicGhostPartial(const float* st, int candidate, int count,
        int bassBandEnd = 36)
    {
        (void)bassBandEnd;
        if (!st || candidate <= 0 || candidate >= count || count > COUNT) return false;
        if (st[candidate] <= 1e-10f) return false;

        float amp[COUNT];
        for (int i = 0; i < count; ++i)
            amp[i] = (st[i] > 0.0f) ? sqrtf(st[i]) : 0.0f;

        const float sc = amp[candidate];
        if (sc < 1e-7f) return false;

        for (int n = HARMONIC_N_MIN; n <= kGhostNMax; ++n) {
            const int lo = HarmonicDownKeyAny(candidate, n);
            if (lo < 0 || lo >= candidate) continue;
            if (HarmonicKeyOnBoard(lo, n) != candidate) continue;
            if (lo > 0 && amp[lo - 1] > amp[lo] * 1.02f) continue;
            if (lo + 1 < count && amp[lo + 1] > amp[lo] * 1.02f) continue;
            if (amp[lo] < sc * 0.45f) continue;

            const float raw = amp[lo] / (float)n;
            if (raw > 1e-8f && sc <= raw * 1.55f) {
                if (!(sc > raw * 1.08f && OddPartialExceedsParent(amp, candidate, lo, count)))
                    return true;
            }
            const AmpEnv env = FitAmpEnv(amp, lo, count);
            const float pred = PredictHarmonicAmp(env, n);
            if (pred < 1e-8f) continue;
            if (sc > pred * kPartialExplain) continue;
            if (sc > pred * 1.08f && OddPartialExceedsParent(amp, candidate, lo, count))
                continue;
            return true;
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
