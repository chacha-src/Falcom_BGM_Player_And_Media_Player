#include "ym2612_pool.hpp"
#include "midisynth.hpp"
#include "ymfm.h"
#include "ymfm_opn.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ympool {

const int kChips = 4;
const int kChPerChip = 6;
const int kVoices = kChips * kChPerChip;
const int kKeyChan[6] = { 0, 1, 2, 4, 5, 6 };

class Ym2612Split : public ymfm::ym2612 {
public:
    explicit Ym2612Split(ymfm::ymfm_interface& intf) : ymfm::ym2612(intf) {}
    void clock_split(int32_t out[6][2])
    {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        for (int c = 0; c < 6; c++) {
            output_data temp;
            m_fm.output(temp.clear(), 5, 256, 1u << c);
            out[c][0] = dac_discontinuity(temp.data[0]);
            out[c][1] = dac_discontinuity(temp.data[1]);
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
    int gen;
    int order;
    int chip;
    int ch;
    Inst inst;
    double midi;
    double mul;
    int rel_left;
    std::vector<float> pcm;
};

class YmNote : public note {
public:
    YmNote(struct Ym2612Pool::Impl* pool, int slot, int gen, int velocity);
    ~YmNote();
    bool synthesize(sample_t* buf, std::size_t samples, double rate, sample_t left, sample_t right);
    void note_off(int velocity);
    void sound_off();
    void set_frequency_multiplier(double value);
    void set_tremolo(int, double) {}
    void set_vibrato(double, double) {}
    void set_damper(int) {}
    void set_sostenute(int) {}
    void set_freeze(int) {}
    struct Ym2612Pool::Impl* pool;
    int slot;
    int gen;
    int velocity;
    bool force_end;
};

} // namespace ympool

using namespace ympool;

struct Ym2612Pool::Impl {
    std::vector<Bank> banks;
    ChipBox* chips[kChips];
    Slot slots[kVoices];
    int live;
    int frame_left;
    int order;
    bool inited;
    double rate;

    Impl() : live(0), frame_left(-1), order(0), inited(false), rate(44100)
    {
        for (int i = 0; i < kChips; i++) chips[i] = 0;
        for (int i = 0; i < kVoices; i++) {
            slots[i].used = false;
            slots[i].held = false;
            slots[i].gen = 1;
            slots[i].order = 0;
            slots[i].chip = i / kChPerChip;
            slots[i].ch = i % kChPerChip;
            slots[i].midi = 60;
            slots[i].mul = 1;
            slots[i].rel_left = 0;
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

    void write_pitch(Slot& s)
    {
        double midi = s.midi;
        if (s.mul > 0 && s.mul != 1.0)
            midi += 12.0 * std::log(s.mul) / std::log(2.0);
        int block = 0, fnum = 0;
        fnum_of(midi, rate, block, fnum);
        int cc = s.ch % 3;
        /* A4 latches block/F-num high. A0 commits both. Upper first. */
        wr(s.chip, s.ch, 0xA4 + cc, ((block & 7) << 3) | ((fnum >> 8) & 7));
        wr(s.chip, s.ch, 0xA0 + cc, fnum & 0xFF);
    }

    void write_inst(Slot& s, int chorus)
    {
        int cc = s.ch % 3;
        const Inst& in = s.inst;
        for (int d = 0; d < 7; d++) {
            for (int op = 0; op < 4; op++)
                wr(s.chip, s.ch, 0x30 + 0x10 * d + op * 4 + cc, in.op[op].raw[d]);
        }
        wr(s.chip, s.ch, 0xB0 + cc, in.fbalg);
        int sens = in.lfosens & 0x3F;
        if (chorus > 64) {
            int fms = sens & 7;
            if (fms < 7) fms++;
            sens = (sens & ~7) | fms;
        }
        wr(s.chip, s.ch, 0xB4 + cc, 0xC0 | sens);
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

    const Inst* find_exact(bool perc, int msb, int lsb, int pc, int family)
    {
        pc &= 127;
        for (size_t i = 0; i < banks.size(); i++) {
            const Bank& b = banks[i];
            if (b.family != family || b.perc != perc || b.msb != msb || b.lsb != lsb) continue;
            if (b.ins[pc].alive) return &b.ins[pc];
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

    const Inst* find_melodic(int mode, int msb, int lsb, int pc)
    {
        const Inst* in = 0;
        int gs = (mode == 3) || (mode == 0 && msb > 0 && msb < 64 && lsb <= 4);
        if (gs) {
            int mapLsb = gs_file_lsb(lsb);
            int var = msb;
            in = find_exact(false, var, mapLsb, pc, 1);
            if (!in && mapLsb) in = find_exact(false, var, 0, pc, 1);
            if (!in) in = find_exact(false, 0, 0, pc, 1);
            if (in) return in;
        }
        if (msb == 64) in = find_exact(false, 64, lsb, pc, 0);
        if (!in && msb == 0) in = find_exact(false, 0, lsb, pc, 0);
        if (!in && msb) in = find_exact(false, msb, lsb, pc, 0);
        if (!in) in = find_exact(false, 0, 0, pc, 0);
        if (!in) in = find_exact(false, 0, 0, pc, 1);
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
        int free_s = -1, rel_s = -1, on_s = -1;
        int rel_ord = 0x7fffffff, on_ord = 0x7fffffff;
        for (int i = 0; i < kVoices; i++) {
            if (!slots[i].used) { free_s = i; break; }
            if (!slots[i].held && slots[i].order < rel_ord) { rel_ord = slots[i].order; rel_s = i; }
            if (slots[i].held && slots[i].order < on_ord) { on_ord = slots[i].order; on_s = i; }
        }
        if (free_s >= 0) return free_s;
        if (rel_s >= 0) return rel_s;
        return on_s >= 0 ? on_s : 0;
    }

    void render(size_t n)
    {
        for (int i = 0; i < kVoices; i++) {
            if (!slots[i].used) continue;
            slots[i].pcm.assign(n * 2, 0.f);
        }
        std::vector<int32_t> mix(kChips * 6 * 2);
        for (size_t s = 0; s < n; s++) {
            for (int c = 0; c < kChips; c++) {
                int32_t ch[6][2];
                chips[c]->chip.clock_split(ch);
                for (int k = 0; k < 6; k++) {
                    mix[(c * 6 + k) * 2] = ch[k][0];
                    mix[(c * 6 + k) * 2 + 1] = ch[k][1];
                }
            }
            for (int i = 0; i < kVoices; i++) {
                if (!slots[i].used) continue;
                int id = slots[i].chip * 6 + slots[i].ch;
                const double scale = (128.0 * 64.0 / 65.0) / 32768.0 / 3.0;
                slots[i].pcm[s * 2] = (float)(mix[id * 2] * scale);
                slots[i].pcm[s * 2 + 1] = (float)(mix[id * 2 + 1] * scale);
            }
        }
    }

    void ensure(size_t n, double rate_)
    {
        if (frame_left >= 0) return;
        if (rate_ > 1 && rate_ != rate) {
            rate = rate_;
            init_chips();
            for (int i = 0; i < kVoices; i++)
                if (slots[i].used) write_pitch(slots[i]);
        }
        init_chips();
        render(n);
        frame_left = live > 0 ? live : 1;
    }

    void end_voice()
    {
        if (frame_left > 0) frame_left--;
        if (frame_left == 0) frame_left = -1;
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
        slots[slot].gen++;
    }
};

namespace ympool {

YmNote::YmNote(Ym2612Pool::Impl* pool, int slot, int gen, int velocity)
    : note(0, 8192), pool(pool), slot(slot), gen(gen), velocity(velocity), force_end(false)
{
    pool->live++;
}

YmNote::~YmNote()
{
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
        const float* pcm = s.pcm.empty() ? 0 : &s.pcm[0];
        size_t n = s.pcm.size() / 2;
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
    pool->end_voice();
    return stay;
}

void YmNote::note_off(int)
{
    if (!pool->owns(slot, gen)) return;
    Slot& s = pool->slots[slot];
    s.held = false;
    pool->key(s.chip, s.ch, 0);
    int rr = pool->carrier_rr(s.inst);
    double sec = 1.2;
    if (rr >= 13) sec = 0.12;
    else if (rr >= 8) sec = 0.35;
    else if (rr >= 4) sec = 0.7;
    s.rel_left = (int)(pool->rate * sec);
    if (s.rel_left < 64) s.rel_left = 64;
}

void YmNote::sound_off()
{
    force_end = true;
    if (pool->owns(slot, gen))
        pool->drop(slot);
}

void YmNote::set_frequency_multiplier(double value)
{
    if (!pool->owns(slot, gen)) return;
    pool->slots[slot].mul = value;
    pool->init_chips();
    pool->write_pitch(pool->slots[slot]);
}

} // namespace ympool

Ym2612Pool::Ym2612Pool() : impl(new Impl()) {}
Ym2612Pool::~Ym2612Pool() { delete impl; }
bool Ym2612Pool::ready() const { return impl && !impl->banks.empty(); }

void Ym2612Pool::reset_render_frame()
{
    if (impl)
        impl->frame_left = -1;
}

bool Ym2612Pool::load(const wchar_t* path, int family, int append)
{
    if (!impl) return false;
    if (!append) impl->banks.clear();
    if (!path) return !impl->banks.empty();
    FILE* fp = 0;
    if (_wfopen_s(&fp, path, L"rb") != 0 || !fp) return !impl->banks.empty();
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long sz = ftell(fp);
    if (sz < 32 || sz > 8 * 1024 * 1024) { fclose(fp); return false; }
    std::vector<uint8_t> b((size_t)sz);
    fseek(fp, 0, SEEK_SET);
    if (fread(&b[0], 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return false; }
    fclose(fp);

    int ver = 0;
    size_t o = 0;
    if (sz >= 13 && memcmp(&b[0], "WOPN2-B2NK", 11) == 0) {
        ver = b[11] | (b[12] << 8);
        o = 13;
    } else if (sz >= 11 && memcmp(&b[0], "WOPN2-BANK", 11) == 0) {
        ver = 1;
        o = 11;
    } else {
        return false;
    }
    if (o + 5 > b.size()) return false;
    int mb = (b[o] << 8) | b[o + 1];
    int pb = (b[o + 2] << 8) | b[o + 3];
    o += 5;
    if (mb < 1 || mb > 128 || pb < 0 || pb > 128) return false;
    int stride = (ver >= 2) ? 69 : 65;
    size_t need = o + (size_t)(mb + pb) * 34 + (size_t)(mb + pb) * 128 * stride;
    if (need > b.size()) return false;

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
            const uint8_t* p = &b[o];
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

note* Ym2612Pool::note_on(int program, int key, int velocity, double freq_mul, int chorus_send)
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
    const Inst* in = drum ? impl->find_drum(mode, msb, lsb, pc, key)
                          : impl->find_melodic(mode, msb, lsb, pc);
    if (!in || !in->alive) return 0;

    int s = impl->pick();
    if (impl->slots[s].used)
        impl->slots[s].gen++;
    Slot& slot = impl->slots[s];
    slot.used = true;
    slot.held = true;
    slot.order = ++impl->order;
    slot.inst = *in;
    slot.mul = freq_mul > 0 ? freq_mul : 1;
    double midi = (double)key + (double)in->note_off;
    if (drum && in->perc_key > 0)
        midi = (double)in->perc_key + (double)in->note_off;
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    slot.midi = midi;
    impl->key(slot.chip, slot.ch, 0);
    impl->write_inst(slot, chorus_send);
    impl->write_pitch(slot);
    impl->key(slot.chip, slot.ch, 1);
    return new ympool::YmNote(impl, s, slot.gen, velocity);
}
