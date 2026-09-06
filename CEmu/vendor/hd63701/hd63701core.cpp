/* CEmu HD63701 bus wrapper around FBNeo m6800/hd63701 core. */
#include "hd63701core.h"
#include "burnint.h"
#include "m6800.h"

struct HD63701Cpu {
	void* ctx;
	HD63701ReadFn read;
	HD63701WriteFn write;
	HD63701PortReadFn portRead;
	HD63701PortWriteFn portWrite;
	uint32_t irqCount;
};

static HD63701Cpu* g_cpu;

unsigned char M6800ReadByte(unsigned short Address)
{
	if (g_cpu && g_cpu->read) return g_cpu->read(g_cpu->ctx, Address);
	return 0xff;
}
void M6800WriteByte(unsigned short Address, unsigned char Data)
{
	if (g_cpu && g_cpu->write) g_cpu->write(g_cpu->ctx, Address, Data);
}
unsigned char M6800ReadOp(unsigned short Address)
{
	return M6800ReadByte(Address);
}
unsigned char M6800ReadOpArg(unsigned short Address)
{
	return M6800ReadByte(Address);
}
unsigned char M6800ReadPort(unsigned short Address)
{
	if (g_cpu && g_cpu->portRead) return g_cpu->portRead(g_cpu->ctx, Address);
	return 0xff;
}
void M6800WritePort(unsigned short Address, unsigned char Data)
{
	if (g_cpu && g_cpu->portWrite) g_cpu->portWrite(g_cpu->ctx, Address, Data);
}

HD63701Cpu* HD63701Create(void)
{
	HD63701Cpu* cpu = (HD63701Cpu*)calloc(1, sizeof(HD63701Cpu));
	if (!cpu) return NULL;
	g_cpu = cpu;
	hd63701_init();
	return cpu;
}

void HD63701Destroy(HD63701Cpu* cpu)
{
	if (!cpu) return;
	if (g_cpu == cpu) g_cpu = NULL;
	free(cpu);
}

void HD63701SetBus(HD63701Cpu* cpu, void* ctx,
	HD63701ReadFn read, HD63701WriteFn write,
	HD63701PortReadFn portRead, HD63701PortWriteFn portWrite)
{
	if (!cpu) return;
	cpu->ctx = ctx;
	cpu->read = read;
	cpu->write = write;
	cpu->portRead = portRead;
	cpu->portWrite = portWrite;
	g_cpu = cpu;
}

void HD63701Reset(HD63701Cpu* cpu)
{
	if (!cpu) return;
	g_cpu = cpu;
	m6800_reset();
}

int HD63701Execute(HD63701Cpu* cpu, int cycles)
{
	if (!cpu || cycles <= 0) return 0;
	g_cpu = cpu;
	return hd63701_execute(cycles);
}

void HD63701SetInputLine(HD63701Cpu* cpu, int line, int state)
{
	if (!cpu) return;
	g_cpu = cpu;
	m6800_set_irq_line(line, state);
	if (state) cpu->irqCount++;
}

void HD63701ClearInterruptMask(HD63701Cpu* cpu)
{
	if (!cpu) return;
	g_cpu = cpu;
	m6800_Regs r;
	m6800_get_context(&r);
	r.cc = (UINT8)(r.cc & (UINT8)~0x10u); /* clear I */
	m6800_set_context(&r);
}

uint16_t HD63701Pc(const HD63701Cpu* cpu)
{
	(void)cpu;
	return (uint16_t)m6800_get_pc();
}

uint32_t HD63701IrqCount(const HD63701Cpu* cpu)
{
	return cpu ? cpu->irqCount : 0;
}
