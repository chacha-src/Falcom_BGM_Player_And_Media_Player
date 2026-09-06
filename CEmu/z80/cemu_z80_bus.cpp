#include "StdAfx.h"

#include "cemu_z80_bus.h"

#include "../machine/cemu_hard_ac.h"
#include "../machine/cemu_hard_sg1000.h"
#include "../machine/cemu_hard_x1.h"
#include "../machine/cemu_hard_msx.h"
#include "../machine/cemu_hard_neogeo.h"

#define BLARGG_LITTLE_ENDIAN 1

#include "Ay_Cpu.h"



static CHard* g_z80BusHard = NULL;



void CEmuZ80BusSetActive(CHard* hw)

{

	g_z80BusHard = hw;

}



CHard* CEmuZ80BusGetActive()

{

	return g_z80BusHard;

}



int ay_cpu_in(Ay_Cpu* cpu, unsigned addr)

{

	(void)cpu;

	CHard* hw = g_z80BusHard;

	if (!hw) return 0xff;

	return hw->PortIn((uint16_t)addr);

}



void ay_cpu_out(Ay_Cpu* cpu, cpu_time_t time, unsigned addr, int data)

{

	(void)cpu;

	(void)time;

	CHard* hw = g_z80BusHard;

	if (!hw) return;

	hw->PortOut((uint16_t)addr, (uint8_t)data);

}



int ay_cpu_read(Ay_Cpu* cpu, unsigned addr)

{

	CHard* hw = g_z80BusHard;

	if (hw && hw->hardKind == CHard::KIND_AC)

		return ((CHardAc*)hw)->MemRead((uint16_t)addr);

	if (hw && hw->hardKind == CHard::KIND_SG1000)

		return ((CHardSg1000*)hw)->MemRead((uint16_t)addr);

	if (hw && hw->hardKind == CHard::KIND_X1)

		return ((CHardX1*)hw)->MemRead((uint16_t)addr);

	if (hw && hw->hardKind == CHard::KIND_MSX)

		return ((CHardMsx*)hw)->MemRead((uint16_t)addr);
	if (hw && hw->hardKind == CHard::KIND_NEO)

		return ((CHardNeo*)hw)->MemRead((uint16_t)addr);

	if (cpu) {

		uint8_t* m = cpu->get_mem();

		if (m) return m[(uint16_t)addr];

	}

	return 0xff;

}



void ay_cpu_write(Ay_Cpu* cpu, unsigned addr, int data)

{

	CHard* hw = g_z80BusHard;

	if (hw && hw->hardKind == CHard::KIND_AC) {

		((CHardAc*)hw)->MemWrite((uint16_t)addr, (uint8_t)data);

		return;

	}

	if (hw && hw->hardKind == CHard::KIND_SG1000) {

		((CHardSg1000*)hw)->MemWrite((uint16_t)addr, (uint8_t)data);

		return;

	}

	if (hw && hw->hardKind == CHard::KIND_X1) {

		((CHardX1*)hw)->MemWrite((uint16_t)addr, (uint8_t)data);

		return;

	}

	if (hw && hw->hardKind == CHard::KIND_MSX) {

		((CHardMsx*)hw)->MemWrite((uint16_t)addr, (uint8_t)data);

		return;

	}
	if (hw && hw->hardKind == CHard::KIND_NEO) {

		((CHardNeo*)hw)->MemWrite((uint16_t)addr, (uint8_t)data);

		return;

	}

	if (cpu) {

		uint8_t* m = cpu->get_mem();

		if (m) m[(uint16_t)addr] = (uint8_t)data;

	}

}
