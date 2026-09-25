#pragma once
#include <stdint.h>

class note;

/* YM2612 x4 (24 voices). WOPN instruments stay in chip register form. */
class Ym2612Pool {
public:
    Ym2612Pool();
    ~Ym2612Pool();
    bool load(const wchar_t* path, int family = 0, int append = 0);
    struct Impl;
    bool ready() const;
    void reset_render_frame();
    note* note_on(int program, int key, int velocity, double freq_mul, int chorus_send);
private:
    Impl* impl;
};
