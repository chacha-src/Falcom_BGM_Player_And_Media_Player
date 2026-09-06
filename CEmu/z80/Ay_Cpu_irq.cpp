#include "StdAfx.h"
// Z80 IM2 interrupt delivery and single-instruction step (hoot pc88 Runner)
#include "Ay_Cpu.h"

static uint16_t Ay_CpuPeek16(const uint8_t* mem, uint16_t addr)
{
	return (uint16_t)mem[addr] | ((uint16_t)mem[(uint16_t)(addr + 1)] << 8);
}

uint16_t Ay_CpuIm2Target(const Ay_Cpu* cpu, uint8_t vector)
{
	if (!cpu) return 0;
	const uint8_t* mem = cpu->get_mem();
	if (!mem) return 0;
	const uint16_t table = (uint16_t)(((uint16_t)cpu->r.i << 8) | vector);
	return Ay_CpuPeek16(mem, table);
}

bool Ay_CpuIm2Interrupt(Ay_Cpu* cpu, uint8_t vector)
{
	if (!cpu || !cpu->r.iff1 || cpu->r.im != 2)
		return false;
	if (cpu->irqDelay)
		return false;
	const uint16_t target = Ay_CpuIm2Target(cpu, vector);
	if (target == 0)
		return false;
	return Ay_CpuIm2InterruptTo(cpu, target);
}

bool Ay_CpuIm2InterruptTo(Ay_Cpu* cpu, uint16_t target)
{
	if (!cpu || !cpu->r.iff1 || cpu->r.im != 2)
		return false;
	if (cpu->irqDelay)
		return false;
	if (target == 0)
		return false;
	uint8_t* mem = cpu->get_mem();
	if (!mem) return false;

	/* Z80: leave HALT with PC at the following insn so RETI resumes
	   past it (tf88sr PATCH play HALTs waiting for RTC). */
	if (mem[cpu->r.pc] == 0x76)
		cpu->r.pc = (uint16_t)(cpu->r.pc + 1);

	cpu->r.iff1 = 0;
	cpu->r.iff2 = 0;
	cpu->r.sp = (uint16_t)(cpu->r.sp - 2);
	mem[cpu->r.sp] = (uint8_t)(cpu->r.pc & 0xff);
	mem[(uint16_t)(cpu->r.sp + 1)] = (uint8_t)(cpu->r.pc >> 8);
	cpu->r.pc = target;
	return true;
}

/* Push PC and vector to 0038. Caller must already have checked IFF1/irqDelay. */
static bool Ay_CpuDoIm1Vector(Ay_Cpu* cpu)
{
	uint8_t* mem = cpu->get_mem();
	if (!mem) return false;
	if (mem[cpu->r.pc] == 0x76)
		cpu->r.pc = (uint16_t)(cpu->r.pc + 1);
	cpu->r.iff1 = 0;
	cpu->r.iff2 = 0;
	cpu->r.sp = (uint16_t)(cpu->r.sp - 2);
	mem[cpu->r.sp] = (uint8_t)(cpu->r.pc & 0xff);
	mem[(uint16_t)(cpu->r.sp + 1)] = (uint8_t)(cpu->r.pc >> 8);
	cpu->r.pc = 0x0038;
	return true;
}

/* Same as Ay_CpuIm1Interrupt: requires IFF1 and no irqDelay.
   Never interrupts while DI (IFF1 clear). */
bool Ay_CpuForceIm1(Ay_Cpu* cpu)
{
	if (!cpu || !cpu->r.iff1)
		return false;
	if (cpu->irqDelay)
		return false;
	return Ay_CpuDoIm1Vector(cpu);
}

bool Ay_CpuIm1Interrupt(Ay_Cpu* cpu)
{
	if (!cpu || !cpu->r.iff1)
		return false;
	if (cpu->irqDelay)
		return false;
	return Ay_CpuDoIm1Vector(cpu);
}

/* IM 0: the device drives an RST opcode onto the bus during IACK. Irem M72
   feeds the Z80 through an RST_NEG_BUFFER, so the target is not always 0038. */
bool Ay_CpuRstInterrupt(Ay_Cpu* cpu, uint16_t target)
{
	if (!cpu || !cpu->r.iff1)
		return false;
	if (cpu->irqDelay)
		return false;
	uint8_t* mem = cpu->get_mem();
	if (!mem) return false;
	if (mem[cpu->r.pc] == 0x76)
		cpu->r.pc = (uint16_t)(cpu->r.pc + 1);
	cpu->r.iff1 = 0;
	cpu->r.iff2 = 0;
	cpu->r.sp = (uint16_t)(cpu->r.sp - 2);
	mem[cpu->r.sp] = (uint8_t)(cpu->r.pc & 0xff);
	mem[(uint16_t)(cpu->r.sp + 1)] = (uint8_t)(cpu->r.pc >> 8);
	cpu->r.pc = (uint16_t)(target & 0x0038);
	return true;
}

bool Ay_CpuNmi(Ay_Cpu* cpu)
{
	if (!cpu) return false;
	uint8_t* mem = cpu->get_mem();
	if (!mem) return false;
	if (mem[cpu->r.pc] == 0x76)
		cpu->r.pc = (uint16_t)(cpu->r.pc + 1);
	cpu->r.iff2 = cpu->r.iff1;
	cpu->r.iff1 = 0;
	cpu->r.sp = (uint16_t)(cpu->r.sp - 2);
	mem[cpu->r.sp] = (uint8_t)(cpu->r.pc & 0xff);
	mem[(uint16_t)(cpu->r.sp + 1)] = (uint8_t)(cpu->r.pc >> 8);
	cpu->r.pc = 0x0066;
	return true;
}

int Ay_CpuRunOne(Ay_Cpu* cpu)
{
	if (!cpu) return 0;
	/* Consume EI delay after the instruction that followed EI completes. */
	const uint8_t clearDelay = cpu->irqDelay;
	const cpu_time_t start = cpu->time();
	cpu->run(start + 4);
	int cycles = (int)(cpu->time() - start);
	if (cycles <= 0) {
		cpu->run(start + 64);
		cycles = (int)(cpu->time() - start);
	}
	if (clearDelay)
		cpu->irqDelay = 0;
	return cycles > 0 ? cycles : 4;
}
