#include "ym2612_pool.hpp"
#include "midisynth.hpp"
#include "ymfm.h"
#include "ymfm_opn.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ympool {
    // midiの同時発音数(SC8850)が128音なので合わせる = 6 * 22 = 132音。
const int kChips = 22;
const int kChPerChip = 6;
const int kVoices = kChips * kChPerChip;
const int kKeyChan[6] = { 0, 1, 2, 4, 5, 6 };

class Ym2612Split : public ymfm::ym2612 {
public:
    explicit Ym2612Split(ymfm::ymfm_interface& intf) : ymfm::ym2612(intf) {}
    void clock_split(int32_t out[6][2], uint32_t chanmask = 0x3fu)
    {
        if (chanmask == 0) chanmask = 0x3fu;
        m_fm.clock(chanmask);
        for (int c = 0; c < 6; c++) {
            if ((chanmask & (1u << c)) == 0) {
                out[c][0] = out[c][1] = 0;
                continue;
            }
            output_data temp;
            /* 演算子振幅は EG_HIRES_SHIFT 分だけ細かい。変調は 14bit のまま。
               弱い音が数 LSB の矩形になり、演奏中もジーッと残るのを防ぐ。 */
            m_fm.output(temp.clear(), 0, 8191 << ymfm::EG_HIRES_SHIFT, 1u << c);
            out[c][0] = temp.data[0];
            out[c][1] = temp.data[1];
        }
    }
};

struct ChipBox : ymfm::ymfm_interface {
    Ym2612Split chip;
    ChipBox() : chip(*this) {}
};

struct OpBytes {
    uint8_t raw[7];
};

struct Inst {
    int16_t note_off;
    uint8_t perc_key;
    uint8_t fbalg;
    uint8_t lfosens;
    OpBytes op[4];
    bool alive;
};

struct Bank {
    int msb;
    int lsb;
    int family; /* 0=XG, 1=GS (SC-55 lsb 0, SC-88 lsb 2, 88Pro lsb 3) */
    bool perc;
    Inst ins[128];
};

struct Slot {
    bool used;
    bool held;
    bool drum;
    bool want_off;
    bool bankExact;
    bool isEcho;
    int echoKind;
    int gen;
    int order;
    int chip;
    int ch;
    int damper;
    int sostenute;
    Inst inst;
    Inst base;
    double midi;
    double mul;
    double vib_depth;
    double vib_freq;
    double vib_phase;
    int vib_delay_left;
    int rel_left;
    enum { kPcmFrames = 8192 };
    enum { kDeciTaps = 255 };
    float pcm[kPcmFrames * 2];
    /* オーバーサンプルした演算子出力の、間引きフィルタ用の直前サンプル。 */
    float deci[(kDeciTaps - 1) * 2];
    int pcmN;
};

class YmNote : public note {
public:
    YmNote(struct Ym2612Pool::Impl* pool, int slot, int gen, int velocity);
    ~YmNote();
    bool synthesize(sample_t* buf, std::size_t samples, double rate, sample_t left, sample_t right);
    void note_off(int velocity);
    void sound_off();
    void set_frequency_multiplier(double value);
    void set_tremolo(int depth, double freq);
    void set_vibrato(double depth, double freq);
    void set_damper(int value);
    void set_sostenute(int value);
    void set_freeze(int) {}
    void apply_tone(const tone_color& c);
    bool echo_owns() const;
    void tick_echo(int samples);
    void drop_echo();
    struct Ym2612Pool::Impl* pool;
    int slot;
    int gen;
    int velocity;
    bool force_end;
    int echoSlot;
    int echoGen;
    int echoAge;
    int dryOffAge;
    int echoDelaySamp;
    int echoKeyed;
    float echoGain;
};

} // namespace ympool

using namespace ympool;

struct Ym2612Pool::Impl {
    std::vector<Bank> banks;
    ChipBox* chips[kChips];
    Slot slots[kVoices];
    int live;
    int block_open; /* 1 = このブロックの PCM は描き済み。発音数では閉じない */
    int order;
    bool inited;
    int raira;
    double rate;
    double clockRate;
    int oversample;
    int firOs;
    float fir[Slot::kDeciTaps];
    std::vector<float> over;

    Impl() : live(0), block_open(0), order(0), inited(false), raira(0), rate(44100),
             clockRate(44100), oversample(1), firOs(0)
    {
        for (int i = 0; i < kChips; i++) chips[i] = 0;
        for (int i = 0; i < kVoices; i++) {
            slots[i].used = false;
            slots[i].held = false;
            slots[i].drum = false;
            slots[i].want_off = false;
            slots[i].isEcho = false;
            slots[i].echoKind = 0;
            slots[i].gen = 1;
            slots[i].order = 0;
            slots[i].chip = i / kChPerChip;
            slots[i].ch = i % kChPerChip;
            slots[i].damper = 0;
            slots[i].sostenute = 0;
            slots[i].midi = 60;
            slots[i].mul = 1;
            slots[i].vib_depth = 0;
            slots[i].vib_freq = 3;
            slots[i].vib_phase = 0;
            slots[i].vib_delay_left = 0;
            slots[i].rel_left = 0;
            slots[i].pcmN = 0;
            std::memset(slots[i].deci, 0, sizeof(slots[i].deci));
        }
    }
    ~Impl()
    {
        for (int i = 0; i < kChips; i++) delete chips[i];
    }

    void init_chips()
    {
        if (inited) return;
        inited = true;
        for (int i = 0; i < kChips; i++) {
            chips[i] = new ChipBox();
            ymfm::ym2612& c = chips[i]->chip;
            c.write(0, 0x22); c.write(1, 0x0A);
            c.write(0, 0x27); c.write(1, 0x00);
            c.write(0, 0x2B); c.write(1, 0x00);
            for (int k = 0; k < 6; k++) {
                c.write(0, 0x28);
                c.write(1, (uint8_t)kKeyChan[k]);
            }
        }
    }

    void wr(int chip, int ch, int reg, int val)
    {
        ymfm::ym2612& c = chips[chip]->chip;
        int port = (ch >= 3) ? 2 : 0;
        c.write(port, (uint8_t)reg);
        c.write(port + 1, (uint8_t)val);
    }

    void key(int chip, int ch, int on)
    {
        ymfm::ym2612& c = chips[chip]->chip;
        int bits = kKeyChan[ch];
        if (on) bits |= 0xF0;
        c.write(0, 0x28);
        c.write(1, (uint8_t)bits);
    }

    static void fnum_of(double midi, double rate, int& block, int& fnum)
    {
        if (midi < 0) midi = 0;
        if (midi > 127) midi = 127;
        double freq = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        block = 4;
        double fn = freq * 1048576.0 / (rate > 1 ? rate : 44100.0) / 8.0;
        while (fn >= 2048.0 && block < 7) { fn *= 0.5; block++; }
        while (fn < 1024.0 && block > 0) { fn *= 2.0; block--; }
        if (fn < 0) fn = 0;
        if (fn > 2047) fn = 2047;
        fnum = (int)(fn + 0.5);
    }

    void clock_chip(int chip)
    {
        if (chip < 0 || chip >= kChips || !chips[chip]) return;
        int32_t ch[6][2];
        chips[chip]->chip.clock_split(ch);
    }

    void write_pitch(Slot& s)
    {
        double midi = s.midi;
        if (s.mul > 0 && s.mul != 1.0)
            midi += 12.0 * std::log(s.mul) / std::log(2.0);
        if (s.vib_depth != 0)
            midi += s.vib_depth * std::sin(s.vib_phase);
        int block = 0, fnum = 0;
        fnum_of(midi, clockRate, block, fnum);
        int cc = s.ch % 3;
        /* A4 latches block/F-num high. A0 commits both. Upper first. */
        wr(s.chip, s.ch, 0xA4 + cc, ((block & 7) << 3) | ((fnum >> 8) & 7));
        wr(s.chip, s.ch, 0xA0 + cc, fnum & 0xFF);
    }

    static int clampi(int v, int lo, int hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    void paint_wopn(Inst& in, const tone_color& c, int velocity, bool drum)
    {
        static const int carrier[8][4] = {
            {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1},
            {0,0,1,1}, {0,1,1,1}, {0,1,1,1}, {1,1,1,1}
        };
        const tone_paint t = make_tone_paint(c);
        int fb = (in.fbalg >> 3) & 7;
        int alg = in.fbalg & 7;
        fb = clampi(fb + t.fbDelta, 0, 7);
        in.fbalg = (uint8_t)((fb << 3) | alg);
        const int velTl = (127 - velocity) * 12 / 127;
        for (int op = 0; op < 4; op++) {
            uint8_t* r = in.op[op].raw;
            int ar = r[2] & 31;
            int ks = r[2] >> 6;
            int dr = r[3] & 31;
            int am = r[3] & 0x80;
            int sl = r[5] >> 4;
            int rr = r[5] & 15;
            int tl = r[1] & 127;
            int dt = (r[0] >> 4) & 7;
            int mul = r[0] & 15;
            const int isCar = carrier[alg][op];
            if (isCar) {
                ar = clampi(ar + t.arD, 0, 31);
                dr = clampi(dr + t.drD, 0, 31);
                rr = clampi(rr + t.rrD, 0, 15);
                sl = clampi(sl + t.slD, 0, 15);
                tl = clampi(tl + velTl + t.carTl, 0, 127);
                /* raira はドラム音量だけ。AR を上げるとリリースが短くなり、かかり具合が本家とずれる。 */
                if (drum && raira)
                    tl = clampi(tl - 8, 0, 127);
            } else {
                tl = clampi(tl - t.modTl, 0, 127);
                if (t.amsOn) am = 0x80;
                dt = clampi(dt + t.dtD, 0, 7);
                mul = clampi(mul + t.mulD, 0, 15);
            }
            r[0] = (uint8_t)((dt << 4) | (mul & 15));
            r[1] = (uint8_t)(tl & 127);
            r[2] = (uint8_t)((ks << 6) | (ar & 31));
            r[3] = (uint8_t)(am | (dr & 31));
            r[5] = (uint8_t)((sl << 4) | (rr & 15));
        }
    }

    void write_inst(Slot& s)
    {
        int cc = s.ch % 3;
        const Inst& in = s.inst;
        for (int d = 0; d < 7; d++) {
            for (int op = 0; op < 4; op++)
                wr(s.chip, s.ch, 0x30 + 0x10 * d + op * 4 + cc, in.op[op].raw[d]);
        }
        wr(s.chip, s.ch, 0xB0 + cc, in.fbalg);
        wr(s.chip, s.ch, 0xB4 + cc, 0xC0 | (in.lfosens & 0x3F));
    }

    void start_key(Slot& s)
    {
        key(s.chip, s.ch, 0);
        clock_chip(s.chip);
        write_inst(s);
        write_pitch(s);
        key(s.chip, s.ch, 1);
    }

    void release_key(Slot& s)
    {
        s.held = false;
        key(s.chip, s.ch, 0);
        int rr = carrier_rr(s.inst);
        double sec = 1.2;
        if (rr >= 13) sec = 0.12;
        else if (rr >= 8) sec = 0.35;
        else if (rr >= 4) sec = 0.7;
        s.rel_left = (int)(rate * sec);
        if (s.rel_left < 64) s.rel_left = 64;
    }

    void apply_lfo(size_t n)
    {
        if (n == 0) return;
        const double dt = (double)n / (rate > 1 ? rate : 44100.0);
        for (int i = 0; i < kVoices; i++) {
            if (!slots[i].used || slots[i].vib_depth <= 0) continue;
            if (slots[i].vib_delay_left > 0) {
                slots[i].vib_delay_left -= (int)n;
                continue;
            }
            double f = slots[i].vib_freq > 0.25 ? slots[i].vib_freq : 3.0;
            slots[i].vib_phase += 6.283185307179586 * f * dt;
            write_pitch(slots[i]);
        }
    }

    int carrier_rr(const Inst& in)
    {
        static const int carrier[8][4] = {
            {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1},
            {0,0,1,1}, {0,1,1,1}, {0,1,1,1}, {1,1,1,1}
        };
        int alg = in.fbalg & 7;
        int rr = 15;
        for (int op = 0; op < 4; op++) {
            if (!carrier[alg][op]) continue;
            int r = in.op[op].raw[5] & 15;
            if (r < rr) rr = r;
        }
        return rr;
    }

    const Inst* find_exact(bool perc, int msb, int lsb, int pc, int family, int* gotM = 0, int* gotL = 0)
    {
        pc &= 127;
        for (size_t i = 0; i < banks.size(); i++) {
            const Bank& b = banks[i];
            if (b.family != family || b.perc != perc || b.msb != msb || b.lsb != lsb) continue;
            if (b.ins[pc].alive) {
                if (gotM) *gotM = b.msb;
                if (gotL) *gotL = b.lsb;
                return &b.ins[pc];
            }
        }
        return 0;
    }

    /* gs.wopn stores SC-55 variations at LSB 0, SC-88 at LSB 2, 88Pro at LSB 3.
       Roland CC32: 0 panel, 1 SC-55, 2 SC-88, 3 88Pro, 4 8820. */
    static int gs_file_lsb(int cc32)
    {
        if (cc32 == 2) return 2;
        if (cc32 == 3 || cc32 == 4) return 3;
        return 0;
    }

    const Inst* find_melodic(int mode, int msb, int lsb, int pc, int* exact)
    {
        const Inst* in = 0;
        int gotM = -1, gotL = -1;
        if (msb == 127 && lsb <= 4) {
            int mapLsb = gs_file_lsb(lsb ? lsb : 1);
            in = find_exact(false, 127, mapLsb, pc, 1, &gotM, &gotL);
            if (!in) in = find_exact(false, 0, 0, pc, 1, &gotM, &gotL);
            if (in) {
                if (exact) *exact = (gotM == 127 && gotL == mapLsb) ? 1 : 0;
                return in;
            }
        }
        /* CC32=1..4 は gs.wopn。MSB 0 のキャピタルも含む。XG モードだけは family 0。 */
        int gs = (mode == 3) || (mode != 4 && msb < 64 && lsb >= 1 && lsb <= 4);
        if (gs) {
            int mapLsb = gs_file_lsb(lsb);
            int var = msb;
            in = find_exact(false, var, mapLsb, pc, 1, &gotM, &gotL);
            if (in && gotM == var && gotL == mapLsb) {
                if (exact) *exact = 1;
                return in;
            }
            if (!in && mapLsb) in = find_exact(false, var, 0, pc, 1, &gotM, &gotL);
            if (!in) in = find_exact(false, 0, 0, pc, 1, &gotM, &gotL);
            if (in) {
                if (exact) *exact = 0;
                return in;
            }
        }
        in = 0;
        if (msb == 64) in = find_exact(false, 64, lsb, pc, 0, &gotM, &gotL);
        if (!in && msb == 0) in = find_exact(false, 0, lsb, pc, 0, &gotM, &gotL);
        if (!in && msb) in = find_exact(false, msb, lsb, pc, 0, &gotM, &gotL);
        if (in && gotM == msb && gotL == lsb) {
            if (exact) *exact = 1;
            return in;
        }
        if (!in) in = find_exact(false, 0, 0, pc, 0, &gotM, &gotL);
        if (!in) in = find_exact(false, 0, 0, pc, 1, &gotM, &gotL);
        if (exact) *exact = 0;
        return in;
    }

    const Inst* find_drum(int mode, int msb, int lsb, int pc, int key)
    {
        int kit = 0;
        int gs = (mode == 3 || mode == 1 || mode == 2);
        if (gs)
            kit = pc ? pc : lsb;
        else {
            kit = lsb ? lsb : pc;
            if (msb == 126 || msb == 127) kit = lsb;
        }
        int fam = gs ? 1 : 0;
        const Inst* in = find_exact(true, 0, kit, key, fam);
        if (!in) in = find_exact(true, 0, kit, key, fam ^ 1);
        if (!in && kit) in = find_exact(true, 0, 0, key, fam);
        if (!in) in = find_exact(true, 0, 0, key, fam ^ 1);
        return in;
    }

    int pick()
    {
        int free_s = -1, rel_s = -1, echo_s = -1, on_s = -1;
        int rel_ord = 0x7fffffff, echo_ord = 0x7fffffff, on_ord = 0x7fffffff;
        for (int i = 0; i < kVoices; i++) {
            if (!slots[i].used) { free_s = i; break; }
            if (slots[i].isEcho) {
                if (slots[i].order < echo_ord) { echo_ord = slots[i].order; echo_s = i; }
            } else if (!slots[i].held && slots[i].order < rel_ord) {
                rel_ord = slots[i].order; rel_s = i;
            } else if (slots[i].held && slots[i].order < on_ord) {
                on_ord = slots[i].order; on_s = i;
            }
        }
        if (free_s >= 0) return free_s;
        if (rel_s >= 0) return rel_s;
        if (echo_s >= 0) return echo_s;
        return on_s >= 0 ? on_s : 0;
    }

    int pick_free()
    {
        for (int i = 0; i < kVoices; i++)
            if (!slots[i].used) return i;
        return -1;
    }

    void count_room(int& freeN, int& echoN)
    {
        freeN = 0;
        echoN = 0;
        for (int i = 0; i < kVoices; i++) {
            if (!slots[i].used) freeN++;
            else if (slots[i].isEcho) echoN++;
        }
    }

    void make_fir(int os)
    {
        if (os == firOs) return;
        firOs = os;
        const int nt = Slot::kDeciTaps;
        if (os < 2) {
            for (int i = 0; i < nt; i++) fir[i] = 0;
            return;
        }
        /* 折り返し点は 0.5/os。Kaiser で阻止域を深くし、10kHz 以上へ戻る側波を落とす。 */
        const double cutoff = 0.42 / (double)os;
        const int mid = nt / 2;
        const double beta = 8.6;
        auto bessel0 = [](double x) {
            double sum = 1.0, term = 1.0;
            const double x2 = x * x * 0.25;
            for (int k = 1; k < 40; k++) {
                term *= x2 / (double)(k * k);
                sum += term;
                if (term < sum * 1.0e-12) break;
            }
            return sum;
        };
        const double i0b = bessel0(beta);
        double sum = 0;
        for (int i = 0; i < nt; i++) {
            const double x = (double)(i - mid);
            const double s = (x == 0.0)
                ? (2.0 * cutoff)
                : (std::sin(2.0 * 3.141592653589793 * cutoff * x) / (3.141592653589793 * x));
            const double r = (mid > 0) ? (x / (double)mid) : 0.0;
            const double w = (r >= -1.0 && r <= 1.0) ? (bessel0(beta * std::sqrt(1.0 - r * r)) / i0b) : 0.0;
            fir[i] = (float)(s * w);
            sum += fir[i];
        }
        if (sum != 0.0) {
            for (int i = 0; i < nt; i++) fir[i] = (float)(fir[i] / sum);
        }
    }

    /* ホストが 48k や 44.1k のとき、YM をそのレートで刻むと変調の側波が可聴帯に折り返る。
       内部は 176k 以上で刻む。ogg（raira=1）は Kaiser で戻す。
       raira=0 の本家は 255tap FIR が期限を超えるので、平均で間引く（EG とピッチは内部レート）。 */
    void update_rate(double rate_)
    {
        if (!(rate_ > 1)) return;
        int os = 1;
        if (rate_ < 176000.0) {
            os = (int)std::ceil(176000.0 / rate_);
            if (os < 2) os = 2;
            if (os > 4) os = 4;
        }
        if (rate_ == rate && os == oversample && clockRate == rate_ * (double)os)
            return;
        rate = rate_;
        oversample = os;
        clockRate = rate_ * (double)os;
        make_fir(os);
        for (int i = 0; i < kVoices; i++) {
            std::memset(slots[i].deci, 0, sizeof(slots[i].deci));
            if (slots[i].used) write_pitch(slots[i]);
        }
    }

    void render(size_t n)
    {
        if (n > (size_t)Slot::kPcmFrames) n = (size_t)Slot::kPcmFrames;
        uint32_t mask[kChips];
        int slotOf[kChips][6];
        int nslot[kChips];
        for (int c = 0; c < kChips; c++) {
            mask[c] = 0;
            nslot[c] = 0;
        }
        for (int i = 0; i < kVoices; i++) {
            slots[i].pcmN = 0;
            if (!slots[i].used) continue;
            int c = slots[i].chip;
            int ch = slots[i].ch;
            if (c < 0 || c >= kChips || ch < 0 || ch >= 6) continue;
            mask[c] |= 1u << ch;
            if (nslot[c] < 6)
                slotOf[c][nslot[c]++] = i;
            slots[i].pcmN = (int)n;
        }
        /* 14bit は 9bit の 32 倍。128/32=4 で、強い音の大きさは前と同じ。
           EG_HIRES は線形の小数部だけなので、ここでも同じ倍率に戻す。 */
        const double scale = 4.0 / 32768.0 / 3.0 / (double)(1 << ymfm::EG_HIRES_SHIFT);
        const int os = oversample < 1 ? 1 : oversample;
        /* チップをサンプル横断で回す。サンプル毎に 22 チップを渡り歩くと
           32 パートで Render が再生期限を超え、リングが古い音を繰り返す。 */
        if (os == 1) {
            for (int c = 0; c < kChips; c++) {
                if (!mask[c] || !chips[c]) continue;
                for (size_t s = 0; s < n; s++) {
                    int32_t ch[6][2];
                    chips[c]->chip.clock_split(ch, mask[c]);
                    for (int k = 0; k < nslot[c]; k++) {
                        Slot& sl = slots[slotOf[c][k]];
                        sl.pcm[s * 2] = (float)(ch[sl.ch][0] * scale);
                        sl.pcm[s * 2 + 1] = (float)(ch[sl.ch][1] * scale);
                    }
                }
            }
            return;
        }
        const int nt = Slot::kDeciTaps;
        const int hist = nt - 1;
        const size_t ni = n * (size_t)os;
        if (over.size() < (size_t)6 * ni * 2)
            over.resize((size_t)6 * ni * 2);
        for (int c = 0; c < kChips; c++) {
            if (!mask[c] || !chips[c]) continue;
            for (size_t s = 0; s < ni; s++) {
                int32_t ch[6][2];
                chips[c]->chip.clock_split(ch, mask[c]);
                for (int k = 0; k < nslot[c]; k++) {
                    Slot& sl = slots[slotOf[c][k]];
                    const size_t at = ((size_t)k * ni + s) * 2;
                    over[at] = (float)(ch[sl.ch][0] * scale);
                    over[at + 1] = (float)(ch[sl.ch][1] * scale);
                }
            }
            for (int k = 0; k < nslot[c]; k++) {
                Slot& sl = slots[slotOf[c][k]];
                const float* src = over.data() + (size_t)k * ni * 2;
                if (!raira) {
                    const float inv = 1.0f / (float)os;
                    for (size_t i = 0; i < n; i++) {
                        float accL = 0, accR = 0;
                        const size_t base = i * (size_t)os;
                        for (int j = 0; j < os; j++) {
                            accL += src[(base + (size_t)j) * 2];
                            accR += src[(base + (size_t)j) * 2 + 1];
                        }
                        sl.pcm[i * 2] = accL * inv;
                        sl.pcm[i * 2 + 1] = accR * inv;
                    }
                    continue;
                }
                for (size_t i = 0; i < n; i++) {
                    double accL = 0, accR = 0;
                    const int base = (int)i * os;
                    for (int t = 0; t < nt; t++) {
                        const int idx = base + t;
                        float L, R;
                        if (idx < hist) {
                            L = sl.deci[idx * 2];
                            R = sl.deci[idx * 2 + 1];
                        } else {
                            const int s = idx - hist;
                            L = src[(size_t)s * 2];
                            R = src[(size_t)s * 2 + 1];
                        }
                        accL += (double)L * (double)fir[t];
                        accR += (double)R * (double)fir[t];
                    }
                    sl.pcm[i * 2] = (float)accL;
                    sl.pcm[i * 2 + 1] = (float)accR;
                }
                const int total = hist + (int)ni;
                float tail[(Slot::kDeciTaps - 1) * 2];
                for (int j = 0; j < hist; j++) {
                    const int srcI = total - hist + j;
                    float L, R;
                    if (srcI < hist) {
                        L = sl.deci[srcI * 2];
                        R = sl.deci[srcI * 2 + 1];
                    } else {
                        const int s = srcI - hist;
                        L = src[(size_t)s * 2];
                        R = src[(size_t)s * 2 + 1];
                    }
                    tail[j * 2] = L;
                    tail[j * 2 + 1] = R;
                }
                std::memcpy(sl.deci, tail, sizeof(tail));
            }
        }
    }

    void render_block(size_t n, double rate_)
    {
        update_rate(rate_);
        init_chips();
        render(n);
        apply_lfo(n);
        block_open = 1;
    }

    void ensure(size_t n, double rate_)
    {
        if (block_open) return;
        render_block(n, rate_);
    }

    void close_frame()
    {
        block_open = 0;
    }

    bool owns(int slot, int gen) const
    {
        return slot >= 0 && slot < kVoices && slots[slot].used && slots[slot].gen == gen;
    }

    void drop(int slot)
    {
        if (slot < 0 || slot >= kVoices) return;
        if (slots[slot].used) key(slots[slot].chip, slots[slot].ch, 0);
        slots[slot].used = false;
        slots[slot].held = false;
        slots[slot].isEcho = false;
        slots[slot].echoKind = 0;
        slots[slot].gen++;
    }
};

namespace ympool {

YmNote::YmNote(Ym2612Pool::Impl* pool, int slot, int gen, int velocity)
    : note(0, 8192), pool(pool), slot(slot), gen(gen), velocity(velocity), force_end(false),
      echoSlot(-1), echoGen(0), echoAge(0), dryOffAge(-1), echoDelaySamp(0), echoKeyed(0), echoGain(0)
{
    pool->live++;
}

bool YmNote::echo_owns() const
{
    return echoSlot >= 0 && pool->owns(echoSlot, echoGen) && pool->slots[echoSlot].isEcho;
}

void YmNote::drop_echo()
{
    if (echo_owns())
        pool->drop(echoSlot);
    echoSlot = -1;
    echoKeyed = 0;
}

void YmNote::tick_echo(int samples)
{
    if (echoSlot < 0 || samples <= 0) return;
    if (!echo_owns()) { echoSlot = -1; return; }
    echoAge += samples;
    Slot& e = pool->slots[echoSlot];
    if (!echoKeyed && echoAge >= echoDelaySamp) {
        pool->start_key(e);
        e.held = true;
        echoKeyed = 1;
    }
    if (echoKeyed == 1 && dryOffAge >= 0 && echoAge >= dryOffAge + echoDelaySamp) {
        pool->release_key(e);
        echoKeyed = 2;
    }
}

YmNote::~YmNote()
{
    drop_echo();
    if (pool->owns(slot, gen))
        pool->drop(slot);
    pool->live--;
}

bool YmNote::synthesize(sample_t* buf, std::size_t samples, double rate, sample_t left, sample_t right)
{
    pool->ensure(samples, rate);
    bool stay = pool->owns(slot, gen) && !force_end;
    if (stay) {
        Slot& s = pool->slots[slot];
        double v = velocity / 128.0;
        if (s.drum && pool->raira) v *= 1.85;
        const float* pcm = s.pcmN > 0 ? s.pcm : 0;
        size_t n = (size_t)s.pcmN;
        if (n > samples) n = samples;
        for (size_t i = 0; i < n; i++) {
            buf[i * 2] += pcm[i * 2] * left * v / 16384.0;
            buf[i * 2 + 1] += pcm[i * 2 + 1] * right * v / 16384.0;
        }
        if (!s.held) {
            s.rel_left -= (int)samples;
            if (s.rel_left <= 0) stay = false;
        }
    }
    if (echo_owns() && echoKeyed) {
        Slot& e = pool->slots[echoSlot];
        double v = (velocity / 128.0) * (double)echoGain;
        if (e.drum && pool->raira) v *= 1.85;
        const float* pcm = e.pcmN > 0 ? e.pcm : 0;
        size_t n = (size_t)e.pcmN;
        if (n > samples) n = samples;
        if (pcm) {
            for (size_t i = 0; i < n; i++) {
                buf[i * 2] += pcm[i * 2] * left * v / 16384.0;
                buf[i * 2 + 1] += pcm[i * 2 + 1] * right * v / 16384.0;
            }
        }
    }
    tick_echo((int)samples);
    if (echoKeyed == 2 && echo_owns()) {
        Slot& e = pool->slots[echoSlot];
        e.rel_left -= (int)samples;
        if (e.rel_left <= 0)
            drop_echo();
    }
    if (echoSlot >= 0) stay = true;
    return stay;
}

void YmNote::note_off(int)
{
    if (echoSlot >= 0 && dryOffAge < 0)
        dryOffAge = echoAge;
    if (!pool->owns(slot, gen)) return;
    Slot& s = pool->slots[slot];
    s.want_off = true;
    if (s.damper >= 64 || s.sostenute >= 64)
        return;
    pool->release_key(s);
}

void YmNote::sound_off()
{
    force_end = true;
    drop_echo();
    if (pool->owns(slot, gen))
        pool->drop(slot);
}

void YmNote::set_frequency_multiplier(double value)
{
    pool->init_chips();
    if (pool->owns(slot, gen)) {
        pool->slots[slot].mul = value;
        pool->write_pitch(pool->slots[slot]);
    }
    if (echo_owns()) {
        pool->slots[echoSlot].mul = value;
        if (echoKeyed)
            pool->write_pitch(pool->slots[echoSlot]);
    }
}

void YmNote::set_vibrato(double depth, double freq)
{
    if (!pool->owns(slot, gen)) return;
    Slot& s = pool->slots[slot];
    s.vib_depth = depth;
    s.vib_freq = freq > 0.25 ? freq : 3.0;
}

void YmNote::set_tremolo(int depth, double)
{
    if (!pool->owns(slot, gen) || depth <= 0) return;
    Slot& s = pool->slots[slot];
    int ams = (s.inst.lfosens >> 4) & 3;
    if (ams < 2) {
        ams = 2;
        s.inst.lfosens = (uint8_t)((ams << 4) | (s.inst.lfosens & 7));
        pool->write_inst(s);
    }
}

void YmNote::set_damper(int value)
{
    if (!pool->owns(slot, gen)) return;
    Slot& s = pool->slots[slot];
    s.damper = value;
    if (s.want_off && s.damper < 64 && s.sostenute < 64) {
        if (dryOffAge < 0) dryOffAge = echoAge;
        pool->release_key(s);
    }
}

void YmNote::set_sostenute(int value)
{
    if (!pool->owns(slot, gen)) return;
    Slot& s = pool->slots[slot];
    s.sostenute = value;
    if (s.want_off && s.damper < 64 && s.sostenute < 64) {
        if (dryOffAge < 0) dryOffAge = echoAge;
        pool->release_key(s);
    }
}

void YmNote::apply_tone(const tone_color& c)
{
    const int wet = echo_owns() ? 1 : 0;
    if (pool->owns(slot, gen)) {
        Slot& s = pool->slots[slot];
        s.inst = s.base;
        tone_color col = c;
        col.bankExact = s.bankExact ? 1 : 0;
        if (wet) col.spatPass = 1;
        pool->paint_wopn(s.inst, col, velocity, s.drum);
        pool->write_inst(s);
    }
    if (wet) {
        Slot& e = pool->slots[echoSlot];
        double det = e.midi;
        e.inst = e.base;
        tone_color col = c;
        col.bankExact = e.bankExact ? 1 : 0;
        col.spatPass = 0;
        pool->paint_wopn(e.inst, col, velocity, e.drum);
        e.midi = det;
        if (echoKeyed)
            pool->write_inst(e);
    }
}

} // namespace ympool

Ym2612Pool::Ym2612Pool() : impl(new Impl()) {}
Ym2612Pool::~Ym2612Pool() { delete impl; }
bool Ym2612Pool::ready() const { return impl && !impl->banks.empty(); }

void Ym2612Pool::set_raira(int raira)
{
    if (!impl) return;
    impl->raira = raira ? 1 : 0;
    impl->oversample = 0;
}

void Ym2612Pool::reset_render_frame()
{
    if (impl)
        impl->close_frame();
}

void Ym2612Pool::begin_frame(size_t samples, double rate)
{
    if (!impl || samples == 0) return;
    impl->render_block(samples, rate);
}

void Ym2612Pool::end_frame()
{
    if (impl)
        impl->close_frame();
}

bool Ym2612Pool::load_mem(const void* data, size_t sz, int family, int append)
{
    if (!impl || !data || sz < 32 || sz > 8 * 1024 * 1024) return false;
    const uint8_t* b = (const uint8_t*)data;
    int ver = 0;
    size_t o = 0;
    if (sz >= 13 && memcmp(b, "WOPN2-B2NK", 11) == 0) {
        ver = b[11] | (b[12] << 8);
        o = 13;
    } else if (sz >= 11 && memcmp(b, "WOPN2-BANK", 11) == 0) {
        ver = 1;
        o = 11;
    } else {
        return false;
    }
    if (o + 5 > sz) return false;
    int mb = (b[o] << 8) | b[o + 1];
    int pb = (b[o + 2] << 8) | b[o + 3];
    o += 5;
    if (mb < 1 || mb > 128 || pb < 0 || pb > 128) return false;
    int stride = (ver >= 2) ? 69 : 65;
    size_t need = o + (size_t)(mb + pb) * 34 + (size_t)(mb + pb) * 128 * stride;
    if (need > sz) return false;
    if (!append) impl->banks.clear();

    std::vector<Bank> banks;
    banks.resize((size_t)mb + (size_t)pb);
    for (int i = 0; i < mb + pb; i++) {
        banks[i].lsb = b[o + 32];
        banks[i].msb = b[o + 33];
        banks[i].family = family;
        banks[i].perc = i >= mb;
        o += 34;
    }
    for (int i = 0; i < mb + pb; i++) {
        for (int pc = 0; pc < 128; pc++) {
            const uint8_t* p = b + o;
            Inst& in = banks[i].ins[pc];
            in.note_off = (int16_t)((p[32] << 8) | p[33]);
            in.perc_key = p[34];
            in.fbalg = p[35];
            in.lfosens = p[36];
            in.alive = false;
            for (int op = 0; op < 4; op++) {
                memcpy(in.op[op].raw, p + 37 + op * 7, 7);
                int tl = in.op[op].raw[1] & 127;
                int ar = in.op[op].raw[2] & 31;
                if (tl < 120 && ar > 0) in.alive = true;
            }
            o += stride;
        }
    }
    impl->banks.insert(impl->banks.end(), banks.begin(), banks.end());
    return !impl->banks.empty();
}

bool Ym2612Pool::load(const wchar_t* path, int family, int append)
{
    if (!impl || !path || !path[0]) return false;
    FILE* fp = 0;
    if (_wfopen_s(&fp, path, L"rb") != 0 || !fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long sz = ftell(fp);
    if (sz < 32 || sz > 8 * 1024 * 1024) { fclose(fp); return false; }
    std::vector<uint8_t> b((size_t)sz);
    fseek(fp, 0, SEEK_SET);
    if (fread(&b[0], 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return false; }
    fclose(fp);
    return load_mem(&b[0], b.size(), family, append);
}

note* Ym2612Pool::note_on(int program, int key, int velocity, double freq_mul, const tone_color& color)
{
    if (!ready() || velocity <= 0) return 0;
    impl->init_chips();
    bool drum = (program & (1 << 24)) != 0;
    int mode = (program >> 21) & 7;
    int pc = program & 0x7F;
    int lsb = (program >> 7) & 0x7F;
    int msb = (program >> 14) & 0x7F;
    if (msb >= 120 && msb < 126) msb = 0;
    if (key < 0) key = 0;
    if (key > 127) key = 127;
    int exact = 0;
    const Inst* in = drum ? impl->find_drum(mode, msb, lsb, pc, key)
                          : impl->find_melodic(mode, msb, lsb, pc, &exact);
    if (!in || !in->alive) return 0;

    int s = impl->pick();
    if (impl->slots[s].used) {
        impl->key(impl->slots[s].chip, impl->slots[s].ch, 0);
        impl->clock_chip(impl->slots[s].chip);
        impl->slots[s].gen++;
    }
    Slot& slot = impl->slots[s];
    slot.used = true;
    slot.held = true;
    slot.drum = drum;
    slot.want_off = false;
    slot.isEcho = false;
    slot.echoKind = 0;
    slot.bankExact = drum ? true : (exact != 0);
    slot.damper = 0;
    slot.sostenute = 0;
    slot.vib_depth = 0;
    slot.vib_freq = 3;
    slot.vib_phase = 0;
    slot.order = ++impl->order;
    slot.base = *in;
    slot.inst = slot.base;
    tone_color col = color;
    col.mapLsb = lsb;
    col.varMsb = msb;
    col.pc = pc;
    col.sysMode = mode;
    col.bankExact = drum ? 1 : exact;
    echo_plan ep = plan_echo(col);
    int es = -1;
    if (ep.mode) {
        int freeN = 0, echoN = 0;
        impl->count_room(freeN, echoN);
        if (freeN >= 11 && echoN < 16)
            es = impl->pick_free();
    }
    tone_color dryCol = col;
    if (es >= 0) dryCol.spatPass = 1;
    impl->paint_wopn(slot.inst, dryCol, velocity, drum);
    slot.vib_delay_left = (int)(impl->rate * tone_vib_delay_sec(color.vibDelay));
    slot.mul = freq_mul > 0 ? freq_mul : 1;
    double midi = (double)key + (double)in->note_off;
    if (drum && in->perc_key > 0)
        midi = (double)in->perc_key + (double)in->note_off;
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    slot.midi = midi;
    impl->start_key(slot);
    ympool::YmNote* nn = new ympool::YmNote(impl, s, slot.gen, velocity);
    if (es >= 0) {
        Slot& e = impl->slots[es];
        e.used = true;
        e.held = false;
        e.drum = drum;
        e.want_off = false;
        e.isEcho = true;
        e.echoKind = ep.mode;
        e.bankExact = slot.bankExact;
        e.damper = 0;
        e.sostenute = 0;
        e.vib_depth = 0;
        e.vib_freq = 3;
        e.vib_phase = 0;
        e.order = ++impl->order;
        e.base = slot.base;
        e.inst = e.base;
        tone_color wet = col;
        wet.spatPass = 0;
        impl->paint_wopn(e.inst, wet, velocity, drum);
        e.mul = slot.mul;
        e.midi = slot.midi;
        double sr = impl->rate > 1 ? impl->rate : 44100.0;
        int delaySamp = (int)(sr * (ep.ms / 1000.0));
        if (delaySamp < 1) delaySamp = 1;
        nn->echoSlot = es;
        nn->echoGen = e.gen;
        nn->echoAge = 0;
        nn->dryOffAge = -1;
        nn->echoDelaySamp = delaySamp;
        nn->echoKeyed = 0;
        nn->echoGain = ep.gain;
    }
    return nn;
}
