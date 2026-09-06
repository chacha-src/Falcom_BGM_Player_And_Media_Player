/* CEmu M6502 wrapper — Fake6502 (public domain) with board bus callbacks. */
#include "m6502core.h"
#include <stdlib.h>
#include <string.h>

#define FAKE6502_NOT_STATIC
#include "fake6502.h"

struct M6502Cpu {
	void* ctx;
	M6502ReadFn read;
	M6502WriteFn write;
	int irqLine;
	int nmiLine;
	int nmiPrev;
	uint32_t irqCount;
};

static M6502Cpu* g_m6502Active = NULL;

uint8 read6502(ushort address)
{
	M6502Cpu* cpu = g_m6502Active;
	if (!cpu || !cpu->read) return 0xff;
	return cpu->read(cpu->ctx, (uint16_t)address);
}

void write6502(ushort address, uint8 value)
{
	M6502Cpu* cpu = g_m6502Active;
	if (!cpu || !cpu->write) return;
	cpu->write(cpu->ctx, (uint16_t)address, (uint8_t)value);
}

M6502Cpu* M6502Create(void)
{
	return (M6502Cpu*)calloc(1, sizeof(M6502Cpu));
}

void M6502Destroy(M6502Cpu* cpu)
{
	if (!cpu) return;
	if (g_m6502Active == cpu) g_m6502Active = NULL;
	free(cpu);
}

void M6502SetBus(M6502Cpu* cpu, void* ctx, M6502ReadFn read, M6502WriteFn write)
{
	if (!cpu) return;
	cpu->ctx = ctx;
	cpu->read = read;
	cpu->write = write;
}

void M6502Reset(M6502Cpu* cpu)
{
	if (!cpu) return;
	g_m6502Active = cpu;
	cpu->irqLine = 0;
	cpu->nmiLine = 0;
	cpu->nmiPrev = 0;
	cpu->irqCount = 0;
	reset6502();
	status |= FLAG_CONSTANT;
}

int M6502Execute(M6502Cpu* cpu, int cycles)
{
	if (!cpu || cycles <= 0) return 0;
	g_m6502Active = cpu;
	/* Level IRQ: Fake6502 irq6502() is edge-ish; re-arm while line held and I clear. */
	if (cpu->irqLine && !(status & FLAG_INTERRUPT)) {
		irq6502();
		cpu->irqCount++;
	}
	if (cpu->nmiLine && !cpu->nmiPrev) {
		nmi6502();
		cpu->irqCount++;
	}
	cpu->nmiPrev = cpu->nmiLine;
	return (int)exec6502((uint32)cycles);
}

void M6502SetInputLine(M6502Cpu* cpu, int line, int state)
{
	if (!cpu) return;
	const int on = state ? 1 : 0;
	if (line == M6502_LINE_IRQ)
		cpu->irqLine = on;
	else if (line == M6502_LINE_NMI) {
		if (on && !cpu->nmiLine)
			cpu->nmiPrev = 0; /* allow fresh edge */
		cpu->nmiLine = on;
	}
}

uint16_t M6502Pc(const M6502Cpu* cpu)
{
	(void)cpu;
	return (uint16_t)pc;
}

uint8_t M6502RegA(const M6502Cpu* cpu)
{
	(void)cpu;
	return (uint8_t)a;
}

uint8_t M6502RegP(const M6502Cpu* cpu)
{
	(void)cpu;
	return (uint8_t)status;
}

uint32_t M6502IrqCount(const M6502Cpu* cpu)
{
	return cpu ? cpu->irqCount : 0;
}
