#ifndef CEMU_H6280CORE_H
#define CEMU_H6280CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Detached Hudson HuC6280 (65C02 + MMU) for CEmu arcade DECO boards.
   Based on MAME 0.122 portable core (Bryan McPhail / Buchmueller), with
   MAME program_read_byte / cpu_readop replaced by board bus callbacks.
   On-chip timer + IRQ status pages are handled inside the core; PSG and
   I/O port are stubbed (Data East leaves them unused). */

typedef struct H6280Cpu H6280Cpu;

typedef uint8_t (*H6280ReadFn)(void* ctx, uint32_t phys21);
typedef void (*H6280WriteFn)(void* ctx, uint32_t phys21, uint8_t data);

enum {
	H6280_LINE_IRQ1 = 0,  /* MAME input line 0 / IRQ1 vector */
	H6280_LINE_IRQ2 = 1,  /* MAME input line 1 / IRQ2 vector (YM2151 on cninja) */
	H6280_LINE_TIMER = 2,
	H6280_LINE_NMI = 3
};

enum {
	H6280_CLEAR_LINE = 0,
	H6280_ASSERT_LINE = 1
};

H6280Cpu* H6280Create(void);
void H6280Destroy(H6280Cpu* cpu);

void H6280SetBus(H6280Cpu* cpu, void* ctx, H6280ReadFn read, H6280WriteFn write);
void H6280Reset(H6280Cpu* cpu);
/* Run at least `cycles` CPU clocks; returns clocks actually consumed. */
int H6280Execute(H6280Cpu* cpu, int cycles);
void H6280SetInputLine(H6280Cpu* cpu, int line, int state);

uint16_t H6280Pc(const H6280Cpu* cpu);
uint8_t H6280RegA(const H6280Cpu* cpu);
uint8_t H6280RegX(const H6280Cpu* cpu);
uint8_t H6280RegY(const H6280Cpu* cpu);
uint8_t H6280RegP(const H6280Cpu* cpu);
uint8_t H6280Mpr(const H6280Cpu* cpu, unsigned idx);
void H6280SetMpr(H6280Cpu* cpu, unsigned idx, uint8_t page);
uint32_t H6280IrqCount(const H6280Cpu* cpu);

#ifdef __cplusplus
}
#endif

#endif /* CEMU_H6280CORE_H */
