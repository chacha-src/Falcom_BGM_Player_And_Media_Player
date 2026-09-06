#ifndef CEMU_V35CORE_H
#define CEMU_V35CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NEC V35 (uPD70136) real-mode core for CEmu.

   Instruction coverage is V30 level (8086 + the 80186-class additions NEC
   shipped), plus the V25/V35 0x0F extension group: register-bank switching,
   the bit instructions and the BCD string ops. On top of that it models the
   parts of the on-chip peripheral block the Irem M92 sound driver needs — the
   relocatable special-function register page, the eight register banks and the
   internal interrupt controller that owns INTP0/INTP1.

   The core owns no board knowledge: every off-chip access goes through the
   callbacks installed by V35SetBus, the same split cemu_m68k_bus.cpp uses for
   Musashi. Opcode fetches (and only opcode fetches) are passed through the
   per-set substitution table when one is installed, which is how Irem's
   "Software Guard" V35 encrypts its program. */

typedef struct V35Cpu V35Cpu;

typedef uint8_t (*V35ReadFn)(void* ctx, uint32_t addr);
typedef void (*V35WriteFn)(void* ctx, uint32_t addr, uint8_t data);
typedef uint8_t (*V35InFn)(void* ctx, uint16_t port);
typedef void (*V35OutFn)(void* ctx, uint16_t port, uint8_t data);

enum {
	V35_LINE_NMI = 0,
	V35_LINE_INTP0 = 1,
	V35_LINE_INTP1 = 2,
	V35_LINE_INTP2 = 3
};

enum {
	V35_CLEAR_LINE = 0,
	V35_ASSERT_LINE = 1
};

V35Cpu* V35Create(void);
void V35Destroy(V35Cpu* cpu);

void V35SetBus(V35Cpu* cpu, void* ctx,
	V35ReadFn read, V35WriteFn write, V35InFn in, V35OutFn out);
/* 256-entry opcode substitution applied on the fetch path; NULL = plain V35. */
void V35SetDecryptionTable(V35Cpu* cpu, const uint8_t* table256);

void V35Reset(V35Cpu* cpu);
/* Runs at least `cycles` clocks (a REP string op may overshoot) and returns
   how many were actually consumed. */
int V35Execute(V35Cpu* cpu, int cycles);
void V35SetInputLine(V35Cpu* cpu, int line, int state);

uint32_t V35Pc(const V35Cpu* cpu);
uint16_t V35Reg(const V35Cpu* cpu, int index); /* 0..15 = raw bank word */
int V35Halted(const V35Cpu* cpu);
/* Diagnostics for bring-up: unimplemented opcodes seen, and the last one. */
uint32_t V35BadOpCount(const V35Cpu* cpu);
uint32_t V35LastBadOp(const V35Cpu* cpu);
uint32_t V35IrqCount(const V35Cpu* cpu);
/* Diagnostics: ExternalInt source histogram + bank-switch vs classic IVT. */
uint32_t V35IrqSrcCount(const V35Cpu* cpu, unsigned srcIndex);
uint32_t V35IrqBankSwitchCount(const V35Cpu* cpu);
uint32_t V35IrqClassicCount(const V35Cpu* cpu);
uint32_t V35UnmaskedIrq(const V35Cpu* cpu);
uint32_t V35BankswitchIrqMask(const V35Cpu* cpu);
uint16_t V35BankWord(const V35Cpu* cpu, unsigned bank, unsigned index);

#ifdef __cplusplus
}
#endif

#endif /* CEMU_V35CORE_H */
