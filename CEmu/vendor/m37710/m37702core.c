/* CEmu detached M37702 core - Belmont m37710 engine with bus callbacks. */
#include "cemu_m37702cm.h"

uint m37710i_adc_tbl[1] = { 0 };
uint m37710i_sbc_tbl[1] = { 0 };

extern void (*const m37710i_opcodes_M0X0[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes_M0X1[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes_M1X0[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes_M1X1[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes42_M0X0[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes42_M0X1[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes42_M1X0[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes42_M1X1[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes89_M0X0[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes89_M0X1[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes89_M1X0[])(m37710i_cpu_struct *cpustate);
extern void (*const m37710i_opcodes89_M1X1[])(m37710i_cpu_struct *cpustate);
extern uint m37710i_get_reg_M0X0(m37710i_cpu_struct *cpustate, int regnum);
extern uint m37710i_get_reg_M0X1(m37710i_cpu_struct *cpustate, int regnum);
extern uint m37710i_get_reg_M1X0(m37710i_cpu_struct *cpustate, int regnum);
extern uint m37710i_get_reg_M1X1(m37710i_cpu_struct *cpustate, int regnum);
extern void m37710i_set_reg_M0X0(m37710i_cpu_struct *cpustate, int regnum, uint val);
extern void m37710i_set_reg_M0X1(m37710i_cpu_struct *cpustate, int regnum, uint val);
extern void m37710i_set_reg_M1X0(m37710i_cpu_struct *cpustate, int regnum, uint val);
extern void m37710i_set_reg_M1X1(m37710i_cpu_struct *cpustate, int regnum, uint val);
extern void m37710i_set_line_M0X0(m37710i_cpu_struct *cpustate, int line, int state);
extern void m37710i_set_line_M0X1(m37710i_cpu_struct *cpustate, int line, int state);
extern void m37710i_set_line_M1X0(m37710i_cpu_struct *cpustate, int line, int state);
extern void m37710i_set_line_M1X1(m37710i_cpu_struct *cpustate, int line, int state);
extern int  m37710i_execute_M0X0(m37710i_cpu_struct *cpustate, int cycles);
extern int  m37710i_execute_M0X1(m37710i_cpu_struct *cpustate, int cycles);
extern int  m37710i_execute_M1X0(m37710i_cpu_struct *cpustate, int cycles);
extern int  m37710i_execute_M1X1(m37710i_cpu_struct *cpustate, int cycles);

void (*const *const m37710i_opcodes[4])(m37710i_cpu_struct *cpustate) = {
	m37710i_opcodes_M0X0, m37710i_opcodes_M0X1, m37710i_opcodes_M1X0, m37710i_opcodes_M1X1
};
void (*const *const m37710i_opcodes2[4])(m37710i_cpu_struct *cpustate) = {
	m37710i_opcodes42_M0X0, m37710i_opcodes42_M0X1, m37710i_opcodes42_M1X0, m37710i_opcodes42_M1X1
};
void (*const *const m37710i_opcodes3[4])(m37710i_cpu_struct *cpustate) = {
	m37710i_opcodes89_M0X0, m37710i_opcodes89_M0X1, m37710i_opcodes89_M1X0, m37710i_opcodes89_M1X1
};
uint (*const m37710i_get_reg[4])(m37710i_cpu_struct *cpustate, int regnum) = {
	m37710i_get_reg_M0X0, m37710i_get_reg_M0X1, m37710i_get_reg_M1X0, m37710i_get_reg_M1X1
};
void (*const m37710i_set_reg[4])(m37710i_cpu_struct *cpustate, int regnum, uint val) = {
	m37710i_set_reg_M0X0, m37710i_set_reg_M0X1, m37710i_set_reg_M1X0, m37710i_set_reg_M1X1
};
void (*const m37710i_set_line[4])(m37710i_cpu_struct *cpustate, int line, int state) = {
	m37710i_set_line_M0X0, m37710i_set_line_M0X1, m37710i_set_line_M1X0, m37710i_set_line_M1X1
};
int (*const m37710i_execute[4])(m37710i_cpu_struct *cpustate, int cycles) = {
	m37710i_execute_M0X0, m37710i_execute_M0X1, m37710i_execute_M1X0, m37710i_execute_M1X1
};

const int m37710_irq_levels[M37710_LINE_MAX] = {
	0x70, 0x73, 0x74, 0x71, 0x72, 0x7c, 0x7b, 0x7a,
	0x79, 0x78, 0x77, 0x76, 0x75, 0x7f, 0x7e, 0x7d,
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static const int m37710_irq_vectors[M37710_LINE_MAX] = {
	0xffd6, 0xffd8, 0xffda, 0xffdc, 0xffde, 0xffe0, 0xffe2, 0xffe4,
	0xffe6, 0xffe8, 0xffea, 0xffec, 0xffee, 0xfff0, 0xfff2, 0xfff4,
	0xfff6, 0xfff8, 0xfffa, 0xfffc, 0xfffe,
	0, 0, 0, 0, 0, 0, 0, 0
};

/* M37702M2: 512 bytes internal RAM @ 0x80-0x27F */
#define M37702_IRAM_BASE 0x80u
#define M37702_IRAM_SIZE 0x200u

static void m37710_set_irq_line(m37710i_cpu_struct *cpustate, int line, int state)
{
	FTABLE_SET_LINE(cpustate, line, state);
}

INLINE void m37710i_push_8(m37710i_cpu_struct *cpustate, uint value)
{
	m37710_write_8(REG_S, value);
	REG_S = MAKE_UINT_16(REG_S - 1);
}

INLINE void m37710i_push_16(m37710i_cpu_struct *cpustate, uint value)
{
	m37710i_push_8(cpustate, value >> 8);
	m37710i_push_8(cpustate, value & 0xff);
}

INLINE uint m37710i_get_reg_p(m37710i_cpu_struct *cpustate)
{
	return (FLAG_N & 0x80) |
		((FLAG_V >> 1) & 0x40) |
		FLAG_M |
		FLAG_X |
		FLAG_D |
		FLAG_I |
		((!FLAG_Z) << 1) |
		((FLAG_C >> 8) & 1);
}

static void m37702_recalc_timer(m37710i_cpu_struct *cpustate, int timer)
{
	static const int tscales[4] = { 2, 16, 64, 512 };
	if (!(cpustate->m37710_regs[0x40] & (1 << timer))) {
		cpustate->timer_period[timer] = 0;
		cpustate->timer_left[timer] = 0;
		return;
	}
	{
		const int tval = cpustate->m37710_regs[0x46 + (timer * 2)] |
			(cpustate->m37710_regs[0x47 + (timer * 2)] << 8);
		const int mode = cpustate->m37710_regs[0x56 + timer] & 3;
		const int scale = tscales[(cpustate->m37710_regs[0x56 + timer] >> 6) & 3];
		if (mode != 0) {
			cpustate->timer_period[timer] = 0;
			cpustate->timer_left[timer] = 0;
			return;
		}
		/* Ignore ultra-fast 8 MHz (tval==0, scale==2) like MAME. */
		if (tval == 0 && scale == 2) {
			cpustate->timer_period[timer] = 0;
			cpustate->timer_left[timer] = 0;
			return;
		}
		cpustate->timer_period[timer] = (tval + 1) * scale;
		if (cpustate->timer_period[timer] < 64)
			cpustate->timer_period[timer] = 64;
		cpustate->timer_left[timer] = cpustate->timer_period[timer];
	}
}

static uint8_t m37710_internal_r(m37710i_cpu_struct *cpustate, int offset)
{
	offset &= 0x7f;
	switch (offset) {
	case 0x34: case 0x3c: return 0x08;
	case 0x35: case 0x3d: return 0xff;
	case 0x70: return (uint8_t)(cpustate->m37710_regs[offset] | 8);
	default: return cpustate->m37710_regs[offset];
	}
}

static void m37710_internal_w(m37710i_cpu_struct *cpustate, int offset, uint8_t data)
{
	int i;
	const uint8_t prev = cpustate->m37710_regs[offset & 0x7f];
	offset &= 0x7f;
	cpustate->m37710_regs[offset] = data;

	switch (offset) {
	case 0x40:
		for (i = 0; i < 8; i++) {
			if ((data & (1 << i)) && !(prev & (1 << i)))
				m37702_recalc_timer(cpustate, i);
			if (!(data & (1 << i))) {
				cpustate->timer_period[i] = 0;
				cpustate->timer_left[i] = 0;
			}
		}
		break;
	case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b:
	case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51:
	case 0x52: case 0x53: case 0x54: case 0x55:
		i = (offset - 0x46) / 2;
		if (cpustate->m37710_regs[0x40] & (1 << i))
			m37702_recalc_timer(cpustate, i);
		break;
	case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b:
	case 0x5c: case 0x5d:
		i = offset - 0x56;
		if (cpustate->m37710_regs[0x40] & (1 << i))
			m37702_recalc_timer(cpustate, i);
		break;
	case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
	case 0x76: case 0x77: case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c:
	case 0x7d: case 0x7e: case 0x7f:
		m37710_set_irq_line(cpustate, offset, (data & 8) ? HOLD_LINE : CLEAR_LINE);
		m37710i_update_irqs(cpustate);
		break;
	default:
		break;
	}
}

uint8_t m37702_bus_read8(m37710i_cpu_struct *cpustate, uint32_t addr)
{
	addr &= 0xffffffu;
	if (addr < 0x80u)
		return m37710_internal_r(cpustate, (int)addr);
	if (addr >= M37702_IRAM_BASE && addr < M37702_IRAM_BASE + M37702_IRAM_SIZE)
		return cpustate->iram[addr - M37702_IRAM_BASE];
	if (cpustate->int_rom && addr >= 0xc000u && addr <= 0xffffu) {
		const unsigned off = (unsigned)(addr - 0xc000u);
		if (off < cpustate->int_rom_size)
			return cpustate->int_rom[off];
		return 0xff;
	}
	if (cpustate->bus_read)
		return cpustate->bus_read(cpustate->bus_ctx, addr);
	return 0xff;
}

void m37702_bus_write8(m37710i_cpu_struct *cpustate, uint32_t addr, uint8_t data)
{
	addr &= 0xffffffu;
	if (addr < 0x80u) {
		m37710_internal_w(cpustate, (int)addr, data);
		return;
	}
	if (addr >= M37702_IRAM_BASE && addr < M37702_IRAM_BASE + M37702_IRAM_SIZE) {
		cpustate->iram[addr - M37702_IRAM_BASE] = data;
		return;
	}
	/* Internal ROM is read-only. */
	if (addr >= 0xc000u && addr <= 0xffffu)
		return;
	if (cpustate->bus_write)
		cpustate->bus_write(cpustate->bus_ctx, addr, data);
}

uint16_t m37702_bus_read16(m37710i_cpu_struct *cpustate, uint32_t addr)
{
	return (uint16_t)(m37702_bus_read8(cpustate, addr) |
		((uint16_t)m37702_bus_read8(cpustate, addr + 1) << 8));
}

void m37702_bus_write16(m37710i_cpu_struct *cpustate, uint32_t addr, uint16_t data)
{
	m37702_bus_write8(cpustate, addr, (uint8_t)(data & 0xff));
	m37702_bus_write8(cpustate, addr + 1, (uint8_t)(data >> 8));
}

void m37710i_update_irqs(m37710i_cpu_struct *cpustate)
{
	int curirq, pending = (int)LINE_IRQ;
	int wantedIRQ = -1;
	int curpri = 0;

	for (curirq = M37710_LINE_MAX - 1; curirq >= 0; curirq--) {
		if (pending & (1 << curirq)) {
			if (m37710_irq_levels[curirq]) {
				const int control = cpustate->m37710_regs[m37710_irq_levels[curirq]];
				const int thispri = control & 7;
				if (!FLAG_I && thispri > curpri && thispri > (int)cpustate->ipl) {
					wantedIRQ = curirq;
					curpri = thispri;
				}
			} else {
				wantedIRQ = curirq;
				curpri = 7;
				break;
			}
		}
	}

	if (wantedIRQ != -1) {
		CPU_STOPPED &= ~STOP_LEVEL_WAI;
		m37710_set_irq_line(cpustate, wantedIRQ, CLEAR_LINE);
		CLK(13);
		m37710i_push_8(cpustate, REG_PB >> 16);
		m37710i_push_16(cpustate, REG_PC);
		m37710i_push_8(cpustate, cpustate->ipl);
		m37710i_push_8(cpustate, m37710i_get_reg_p(cpustate));
		FLAG_I = IFLAG_SET;
		cpustate->ipl = (uint)curpri;
		REG_PB = 0;
		REG_PC = m37710_read_8(m37710_irq_vectors[wantedIRQ]) |
			(m37710_read_8(m37710_irq_vectors[wantedIRQ] + 1) << 8);
		cpustate->irq_count++;
	}
}

static void m37702_tick_timers(m37710i_cpu_struct *cpustate, int cycles)
{
	int i;
	for (i = 0; i < 8; i++) {
		if (cpustate->timer_period[i] <= 0) continue;
		cpustate->timer_left[i] -= cycles;
		while (cpustate->timer_left[i] <= 0) {
			cpustate->timer_left[i] += cpustate->timer_period[i];
			m37710_set_irq_line(cpustate, M37710_LINE_TIMERA0 - i, HOLD_LINE);
		}
	}
}

M37702Cpu* M37702Create(void)
{
	M37702Cpu* cpu = (M37702Cpu*)calloc(1, sizeof(M37702Cpu));
	return cpu;
}

void M37702Destroy(M37702Cpu* cpu)
{
	free(cpu);
}

void M37702SetBus(M37702Cpu* cpu, void* ctx, M37702ReadFn read, M37702WriteFn write)
{
	if (!cpu) return;
	cpu->s.bus_ctx = ctx;
	cpu->s.bus_read = read;
	cpu->s.bus_write = write;
}

void M37702SetInternalRom(M37702Cpu* cpu, const uint8_t* data, unsigned size)
{
	if (!cpu) return;
	cpu->s.int_rom = data;
	cpu->s.int_rom_size = size;
}

void M37702Reset(M37702Cpu* cpu)
{
	m37710i_cpu_struct *cpustate;
	int i;
	if (!cpu) return;
	cpustate = &cpu->s;
	memset(cpustate->m37710_regs, 0, sizeof(cpustate->m37710_regs));
	memset(cpustate->iram, 0, sizeof(cpustate->iram));
	for (i = 0; i < 8; i++) {
		cpustate->timer_period[i] = 0;
		cpustate->timer_left[i] = 0;
	}
	CPU_STOPPED = 0;
	cpustate->m37710_regs[0x1f] |= 3;
	cpustate->m37710_regs[0x34] = (cpustate->m37710_regs[0x34] & 0xf0) | 8;
	cpustate->m37710_regs[0x3c] = (cpustate->m37710_regs[0x3c] & 0xf0) | 8;
	cpustate->m37710_regs[0x35] = 2;
	cpustate->m37710_regs[0x3d] = 2;
	cpustate->ipl = 0;
	FLAG_M = MFLAG_CLEAR;
	FLAG_X = XFLAG_CLEAR;
	FLAG_D = DFLAG_CLEAR;
	FLAG_I = IFLAG_SET;
	LINE_IRQ = 0;
	IRQ_DELAY = 0;
	REG_D = 0;
	REG_PB = 0;
	REG_DB = 0;
	REG_S = (REG_S & 0xff) | 0x100;
	REG_XH = REG_X & 0xff00; REG_X &= 0xff;
	REG_YH = REG_Y & 0xff00; REG_Y &= 0xff;
	REG_B = REG_A & 0xff00; REG_A &= 0xff;
	REG_BB = REG_BA & 0xff00; REG_BA &= 0xff;
	m37710i_set_execution_mode(cpustate, EXECUTION_MODE_M0X0);
	REG_PC = m37710_read_8(0xfffe) | (m37710_read_8(0xffff) << 8);
	cpustate->irq_count = 0;
}

int M37702Execute(M37702Cpu* cpu, int cycles)
{
	m37710i_cpu_struct *cpustate;
	int used;
	if (!cpu || cycles <= 0) return 0;
	cpustate = &cpu->s;
	cpustate->ICount = cycles;
	m37710i_update_irqs(cpustate);
	used = cpustate->execute(cpustate, cycles);
	if (used < 0) used = cycles;
	m37702_tick_timers(cpustate, used);
	m37710i_update_irqs(cpustate);
	return used;
}

void M37702SetInputLine(M37702Cpu* cpu, int line, int state)
{
	if (!cpu) return;
	m37710_set_irq_line(&cpu->s, line, state);
	if (state != CLEAR_LINE)
		m37710i_update_irqs(&cpu->s);
}

uint32_t M37702Pc(const M37702Cpu* cpu)
{
	if (!cpu) return 0;
	return (uint32_t)(cpu->s.pb | cpu->s.pc);
}

uint32_t M37702Sp(const M37702Cpu* cpu)
{
	return cpu ? (uint32_t)cpu->s.s : 0;
}

uint32_t M37702IrqCount(const M37702Cpu* cpu)
{
	return cpu ? cpu->s.irq_count : 0;
}
