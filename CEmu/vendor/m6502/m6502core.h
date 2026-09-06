#ifndef CEMU_M6502CORE_H
#define CEMU_M6502CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thin Fake6502 wrapper for CEmu arcade DECO M6502/R65C02 sound boards. */

typedef struct M6502Cpu M6502Cpu;

typedef uint8_t (*M6502ReadFn)(void* ctx, uint16_t addr);
typedef void (*M6502WriteFn)(void* ctx, uint16_t addr, uint8_t data);

enum {
	M6502_LINE_IRQ = 0,
	M6502_LINE_NMI = 1
};

enum {
	M6502_CLEAR_LINE = 0,
	M6502_ASSERT_LINE = 1
};

M6502Cpu* M6502Create(void);
void M6502Destroy(M6502Cpu* cpu);
void M6502SetBus(M6502Cpu* cpu, void* ctx, M6502ReadFn read, M6502WriteFn write);
void M6502Reset(M6502Cpu* cpu);
/* Run at least `cycles` CPU clocks; returns clocks consumed. */
int M6502Execute(M6502Cpu* cpu, int cycles);
void M6502SetInputLine(M6502Cpu* cpu, int line, int state);

uint16_t M6502Pc(const M6502Cpu* cpu);
uint8_t M6502RegA(const M6502Cpu* cpu);
uint8_t M6502RegP(const M6502Cpu* cpu);
uint32_t M6502IrqCount(const M6502Cpu* cpu);

#ifdef __cplusplus
}
#endif

#endif /* CEMU_M6502CORE_H */
