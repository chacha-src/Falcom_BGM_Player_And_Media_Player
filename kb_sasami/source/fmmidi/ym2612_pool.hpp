#pragma once
#include <stddef.h>
#include <stdint.h>

class note;
struct tone_color;

/* YM2612 x10 (60 voices). WOPN instruments stay in chip register form. */
class Ym2612Pool {
public:
    Ym2612Pool();
    ~Ym2612Pool();
    bool load(const wchar_t* path, int family = 0, int append = 0);
    bool load_mem(const void* data, size_t size, int family = 0, int append = 0);
    struct Impl;
    bool ready() const;
    void set_raira(int raira);
    void reset_render_frame();
    /* 1 ブロックを 1 回だけ描く。発音数では区切らない。 */
    void begin_frame(size_t samples, double rate);
    void end_frame();
    note* note_on(int program, int key, int velocity, double freq_mul, const tone_color& color);
private:
    Impl* impl;
};
