/* CEmu detached HuC6280 core - see h6280core.h */
#include "h6280core.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef uint32_t offs_t;

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE __inline static
#else
#define INLINE static inline
#endif
#endif

#define ASSERT_LINE 1
#define CLEAR_LINE 0
#define INPUT_LINE_NMI 32

/* MAME PAIR */
typedef union {
	UINT32 d;
	struct { UINT16 l, h; } w;
	struct { UINT8 l, h, h2, h3; } b;
} PAIR;

typedef struct H6280Cpu {
	PAIR ppc, pc, sp, zp, ea;
	UINT8 a, x, y, p;
	UINT8 mmr[8];
	UINT8 irq_mask;
	UINT8 timer_status;
	UINT8 timer_ack;
	UINT8 clocks_per_cycle;
	INT32 timer_value;
	INT32 timer_load;
	UINT8 nmi_state;
	UINT8 irq_state[3];
	UINT8 irq_pending;
	int (*irq_callback)(int irqline);
	UINT8 io_buffer;
	int icount;
	void* bus_ctx;
	H6280ReadFn bus_read;
	H6280WriteFn bus_write;
	uint32_t irq_count;
} H6280Cpu;

/* The opcode macros expect a global named h6280 and h6280_ICount. */
static H6280Cpu* g_active;
static H6280Cpu h6280;
static int h6280_ICount;

/* Stubs for leftover MAME helpers referenced by the opcode macros. */
static void logerror(const char* fmt, ...) { (void)fmt; }
static unsigned activecpu_get_pc(void) { return h6280.pc.w.l; }
static void io_write_byte_8(unsigned addr, unsigned data)
{
	(void)addr;
	(void)data;
}

#define H6280_RESET_VEC 0xfffe
#define H6280_NMI_VEC   0xfffc
#define H6280_TIMER_VEC 0xfffa
#define H6280_IRQ1_VEC  0xfff8
#define H6280_IRQ2_VEC  0xfff6
#define H6280_IRQ_MASK  0x7
#define LAZY_FLAGS 0

static void set_irq_line(int irqline, int state);

static UINT8 h6280_irq_status_r(offs_t offset)
{
	int status;
	switch (offset & 3) {
	default: return h6280.io_buffer;
	case 3:
		status = 0;
		if (h6280.irq_state[1] != CLEAR_LINE) status |= 1;
		if (h6280.irq_state[0] != CLEAR_LINE) status |= 2;
		if (h6280.irq_state[2] != CLEAR_LINE) status |= 4;
		return (UINT8)(status | (h6280.io_buffer & (~H6280_IRQ_MASK)));
	case 2:
		return (UINT8)(h6280.irq_mask | (h6280.io_buffer & (~H6280_IRQ_MASK)));
	}
}

static void h6280_irq_status_w(offs_t offset, UINT8 data)
{
	h6280.io_buffer = data;
	switch (offset & 3) {
	default: break;
	case 2:
		h6280.irq_mask = data & 0x7;
		if (!h6280.irq_pending) h6280.irq_pending = 2;
		break;
	case 3:
		set_irq_line(2, CLEAR_LINE);
		break;
	}
}

static UINT8 h6280_timer_r(offs_t offset)
{
	(void)offset;
	return (UINT8)(((h6280.timer_value / 1024) & 0x7F) | (h6280.io_buffer & 0x80));
}

static void h6280_timer_w(offs_t offset, UINT8 data)
{
	h6280.io_buffer = data;
	switch (offset & 1) {
	case 0:
		h6280.timer_load = h6280.timer_value = ((data & 127) + 1) * 1024;
		return;
	case 1:
		if (data & 1) {
			if (h6280.timer_status == 0)
				h6280.timer_value = h6280.timer_load;
		}
		h6280.timer_status = data & 1;
		return;
	}
}

/* Internal HuC6280 pages (physical). PSG/port stubbed for DECO. */
static UINT8 h6280_bus_read(UINT32 phys)
{
	const UINT32 page = phys & 0x1ffc00u;
	if (page == 0x1fec00u)
		return h6280_timer_r(phys & 1u);
	if (page == 0x1ff400u)
		return h6280_irq_status_r(phys & 3u);
	if (page == 0x1fe800u || page == 0x1ff000u)
		return h6280.io_buffer;
	if (h6280.bus_read)
		return h6280.bus_read(h6280.bus_ctx, phys & 0x1fffffu);
	return 0xff;
}

static void h6280_bus_write(UINT32 phys, UINT8 data)
{
	const UINT32 page = phys & 0x1ffc00u;
	if (page == 0x1fec00u) {
		h6280_timer_w(phys & 1u, data);
		return;
	}
	if (page == 0x1ff400u) {
		h6280_irq_status_w(phys & 3u, data);
		return;
	}
	if (page == 0x1fe800u || page == 0x1ff000u) {
		h6280.io_buffer = data;
		return;
	}
	if (h6280.bus_write)
		h6280.bus_write(h6280.bus_ctx, phys & 0x1fffffu, data);
}

#include "h6280ops_cemu.h"
#include "tblh6280_cemu.c"

static void sync_from_active(void)
{
	if (g_active && g_active != &h6280)
		*g_active = h6280;
}

static void load_active(H6280Cpu* cpu)
{
	g_active = cpu;
	if (cpu)
		h6280 = *cpu;
}

static void set_irq_line(int irqline, int state)
{
	if (irqline == INPUT_LINE_NMI) {
		if (state != ASSERT_LINE) return;
		h6280.nmi_state = (UINT8)state;
		if (!h6280.irq_pending) h6280.irq_pending = 2;
		h6280.irq_count++;
		return;
	}
	if (irqline < 3) {
		if (h6280.irq_state[irqline] == state) return;
		h6280.irq_state[irqline] = (UINT8)state;
		if (!h6280.irq_pending) h6280.irq_pending = 2;
		if (state == ASSERT_LINE) h6280.irq_count++;
	}
}

H6280Cpu* H6280Create(void)
{
	H6280Cpu* cpu = (H6280Cpu*)calloc(1, sizeof(H6280Cpu));
	return cpu;
}

void H6280Destroy(H6280Cpu* cpu)
{
	if (!cpu) return;
	if (g_active == cpu) g_active = NULL;
	free(cpu);
}

void H6280SetBus(H6280Cpu* cpu, void* ctx, H6280ReadFn read, H6280WriteFn write)
{
	if (!cpu) return;
	cpu->bus_ctx = ctx;
	cpu->bus_read = read;
	cpu->bus_write = write;
}

void H6280Reset(H6280Cpu* cpu)
{
	void* ctx;
	H6280ReadFn rd;
	H6280WriteFn wr;
	uint32_t irq_count;
	if (!cpu) return;
	ctx = cpu->bus_ctx;
	rd = cpu->bus_read;
	wr = cpu->bus_write;
	irq_count = cpu->irq_count;
	memset(cpu, 0, sizeof(*cpu));
	cpu->bus_ctx = ctx;
	cpu->bus_read = rd;
	cpu->bus_write = wr;
	cpu->irq_count = irq_count;
	load_active(cpu);
	P = _fI | _fB;
	h6280.sp.d = 0x1ff;
	PCL = RDMEM(H6280_RESET_VEC);
	PCH = RDMEM((H6280_RESET_VEC + 1));
	/* Match modern MAME: low-speed = 4 clocks/cycle. */
	h6280.clocks_per_cycle = 4;
	h6280.timer_status = 0;
	h6280.timer_load = 128 * 1024;
	{
		int i;
		for (i = 0; i < 3; i++)
			h6280.irq_state[i] = CLEAR_LINE;
	}
	h6280.nmi_state = CLEAR_LINE;
	h6280.irq_pending = 0;
	*cpu = h6280;
}

int H6280Execute(H6280Cpu* cpu, int cycles)
{
	int in;
	if (!cpu || cycles <= 0) return 0;
	load_active(cpu);
	h6280_ICount = cycles;
	if (h6280.irq_pending == 2)
		h6280.irq_pending--;
	do {
		h6280.ppc = h6280.pc;
		in = RDOP();
		PCW++;
		insnh6280[in]();
		if (h6280.irq_pending) {
			if (h6280.irq_pending == 1) {
				if (!(P & _fI)) {
					h6280.irq_pending--;
					CHECK_AND_TAKE_IRQ_LINES;
				}
			} else {
				h6280.irq_pending--;
			}
		}
		if (h6280.timer_status) {
			if (h6280.timer_value <= 0) {
				if (!h6280.irq_pending)
					h6280.irq_pending = 1;
				while (h6280.timer_value <= 0)
					h6280.timer_value += h6280.timer_load;
				set_irq_line(2, ASSERT_LINE);
			}
		}
	} while (h6280_ICount > 0);
	{
		const int used = cycles - h6280_ICount;
		*cpu = h6280;
		return used > 0 ? used : cycles;
	}
}

void H6280SetInputLine(H6280Cpu* cpu, int line, int state)
{
	if (!cpu) return;
	/* During H6280Execute the live registers live in the static `h6280`
	   mirror. Reloading from `cpu` mid-op (e.g. latch ack from DecoRead8)
	   would clobber PC/MPR and re-assert stale IRQ lines — that caused an
	   IRQ1 storm on DECO (cninja) and left the command queue empty. */
	if (g_active != cpu)
		load_active(cpu);
	else
		g_active = cpu;
	if (line == H6280_LINE_NMI)
		set_irq_line(INPUT_LINE_NMI, state ? ASSERT_LINE : CLEAR_LINE);
	else if (line >= 0 && line <= 2)
		set_irq_line(line, state ? ASSERT_LINE : CLEAR_LINE);
	*cpu = h6280;
}

uint16_t H6280Pc(const H6280Cpu* cpu) { return cpu ? cpu->pc.w.l : 0; }
uint8_t H6280RegA(const H6280Cpu* cpu) { return cpu ? cpu->a : 0; }
uint8_t H6280RegX(const H6280Cpu* cpu) { return cpu ? cpu->x : 0; }
uint8_t H6280RegY(const H6280Cpu* cpu) { return cpu ? cpu->y : 0; }
uint8_t H6280RegP(const H6280Cpu* cpu) { return cpu ? cpu->p : 0; }
uint8_t H6280Mpr(const H6280Cpu* cpu, unsigned idx)
{
	return (cpu && idx < 8u) ? cpu->mmr[idx] : 0;
}
void H6280SetMpr(H6280Cpu* cpu, unsigned idx, uint8_t page)
{
	if (!cpu || idx >= 8u) return;
	if (g_active == cpu) {
		h6280.mmr[idx] = page;
		*cpu = h6280;
	} else {
		cpu->mmr[idx] = page;
	}
}
uint32_t H6280IrqCount(const H6280Cpu* cpu) { return cpu ? cpu->irq_count : 0; }
