/* CEmu detached H8/3002 (H8/300H advanced) — classic MAME 0.149 core. */
#ifndef CEMU_H8CORE_H
#define CEMU_H8CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct H8Cpu H8Cpu;
typedef uint8_t (*H8ReadFn)(void* ctx, uint32_t addr);
typedef void (*H8WriteFn)(void* ctx, uint32_t addr, uint8_t data);

enum { H8_LINE_IRQ0=0, H8_LINE_IRQ1, H8_LINE_IRQ2, H8_LINE_IRQ3, H8_LINE_IRQ4, H8_LINE_IRQ5, H8_LINE_NMI=9 };
enum { H8_CLEAR_LINE=0, H8_ASSERT_LINE=1 };

H8Cpu* H8Create(void);
void H8Destroy(H8Cpu* cpu);
void H8SetBus(H8Cpu* cpu, void* ctx, H8ReadFn read, H8WriteFn write);
void H8Reset(H8Cpu* cpu);
int H8Execute(H8Cpu* cpu, int cycles);
void H8SetInputLine(H8Cpu* cpu, int line, int state);
uint32_t H8Pc(const H8Cpu* cpu);
uint32_t H8Sp(const H8Cpu* cpu);
uint32_t H8Err(const H8Cpu* cpu);
uint32_t H8IrqCount(const H8Cpu* cpu);

#ifdef __cplusplus
}
#endif
#endif
