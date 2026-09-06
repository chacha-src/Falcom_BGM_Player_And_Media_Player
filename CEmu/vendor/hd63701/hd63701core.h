/* CEmu detached HD63701 — public API (FBNeo/MAME m6800 core). */
#ifndef CEMU_HD63701CORE_H
#define CEMU_HD63701CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct HD63701Cpu HD63701Cpu;
typedef uint8_t (*HD63701ReadFn)(void* ctx, uint16_t addr);
typedef void (*HD63701WriteFn)(void* ctx, uint16_t addr, uint8_t data);
typedef uint8_t (*HD63701PortReadFn)(void* ctx, uint16_t port);
typedef void (*HD63701PortWriteFn)(void* ctx, uint16_t port, uint8_t data);

enum { HD63701_LINE_IRQ = 0, HD63701_LINE_TIN = 1, HD63701_LINE_NMI = 32 };
enum { HD63701_CLEAR_LINE = 0, HD63701_ASSERT_LINE = 1, HD63701_HOLD_LINE = 2, HD63701_PULSE_LINE = 3 };

HD63701Cpu* HD63701Create(void);
void HD63701Destroy(HD63701Cpu* cpu);
void HD63701SetBus(HD63701Cpu* cpu, void* ctx,
	HD63701ReadFn read, HD63701WriteFn write,
	HD63701PortReadFn portRead, HD63701PortWriteFn portWrite);
void HD63701Reset(HD63701Cpu* cpu);
int HD63701Execute(HD63701Cpu* cpu, int cycles);
void HD63701SetInputLine(HD63701Cpu* cpu, int line, int state);
void HD63701ClearInterruptMask(HD63701Cpu* cpu);
uint16_t HD63701Pc(const HD63701Cpu* cpu);
uint32_t HD63701IrqCount(const HD63701Cpu* cpu);

#ifdef __cplusplus
}
#endif
#endif
