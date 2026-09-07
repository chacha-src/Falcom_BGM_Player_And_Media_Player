// Z80 CPU emulator

// Game_Music_Emu https://bitbucket.org/mpyne/game-music-emu/
#ifndef AY_CPU_H
#define AY_CPU_H

#include "blargg_endian.h"

typedef blargg_long cpu_time_t;

// must be defined by caller
void ay_cpu_out( class Ay_Cpu*, cpu_time_t, unsigned addr, int data );
int ay_cpu_in( class Ay_Cpu*, unsigned addr );
/* Optional memory-map side effects (CPS1 YM2151 @ F000/F001). Default = direct mem. */
void ay_cpu_write( class Ay_Cpu*, unsigned addr, int data );
int ay_cpu_read( class Ay_Cpu*, unsigned addr );

class Ay_Cpu {
public:
	// Clear all registers and keep pointer to 64K memory passed in
	void reset( void* mem_64k );

	uint8_t* get_mem() const { return mem; }
	
	// Run until specified time is reached. Returns true if suspicious/unsupported
	// instruction was encountered at any point during run.
	bool run( cpu_time_t end_time );
	
	// Time of beginning of next instruction
	cpu_time_t time() const             { return state->time + state->base; }
	
	// Alter current time. Not supported during run() call.
	void set_time( cpu_time_t t )       { state->time = t - state->base; }
	void adjust_time( int delta )       { state->time += delta; }
	
	#if BLARGG_BIG_ENDIAN
		struct regs_t { uint8_t b, c, d, e, h, l, flags, a; };
	#else
		struct regs_t { uint8_t c, b, e, d, l, h, a, flags; };
	#endif
	static_assert( sizeof (regs_t) == 8, "Invalid register size, padding issue?" );
	
	struct pairs_t { uint16_t bc, de, hl, fa; };
	
	// Registers are not updated until run() returns
	struct registers_t {
		uint16_t pc;
		uint16_t sp;
		uint16_t ix;
		uint16_t iy;
		union {
			regs_t b; //  b.b, b.c, b.d, b.e, b.h, b.l, b.flags, b.a
			pairs_t w; // w.bc, w.de, w.hl. w.fa
		};
		union {
			regs_t b;
			pairs_t w;
		} alt;
		uint8_t iff1;
		uint8_t iff2;
		uint8_t r;
		uint8_t i;
		uint8_t im;
	};
	//registers_t r; (below for efficiency)
	
	// can read this far past end of memory
	enum { cpu_padding = 0x100 };

	/* Z80 EI: maskable IRQs stay blocked for one instruction after EI.
	   Games (e.g. aerofgt) do DI; OUT addr; EI; OUT data — without the delay
	   an IRQ between EI and OUT corrupts the YM address latch. */
	uint8_t irqDelay;
	
public:
	Ay_Cpu();
private:
	uint8_t szpc [0x200];
	uint8_t* mem;
	cpu_time_t end_time_;
	struct state_t {
		cpu_time_t base;
		cpu_time_t time;
	};
	state_t* state; // points to state_ or a local copy within run()
	state_t state_;
	void set_end_time( cpu_time_t t );
public:
	registers_t r;
};

/* IM2 helpers and single-instruction step (see Ay_Cpu_irq.cpp) */
uint16_t Ay_CpuIm2Target(const Ay_Cpu* cpu, uint8_t vector);
bool Ay_CpuIm2Interrupt(Ay_Cpu* cpu, uint8_t vector);
bool Ay_CpuIm2InterruptTo(Ay_Cpu* cpu, uint16_t target);
/* IM1 maskable IRQ → 0x0038 (CPS1 sound CPU). Requires IFF1. */
bool Ay_CpuIm1Interrupt(Ay_Cpu* cpu);
/* Same as Im1Interrupt: requires IFF1 (DI forbidden) and no irqDelay. */
bool Ay_CpuForceIm1(Ay_Cpu* cpu);
/* IM0 RST vector supplied by an external buffer (Irem M72: 08/18/28). */
bool Ay_CpuRstInterrupt(Ay_Cpu* cpu, uint16_t target);
/* Non-maskable interrupt → 0x0066 (System16 sound latch). */
bool Ay_CpuNmi(Ay_Cpu* cpu);
int Ay_CpuRunOne(Ay_Cpu* cpu);

inline void Ay_Cpu::set_end_time( cpu_time_t t )
{
	cpu_time_t delta = state->base - t;
	state->base = t;
	state->time += delta;
}

#endif
