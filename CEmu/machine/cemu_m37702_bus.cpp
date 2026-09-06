#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_m37702_bus.h"
#include "m37702core.h"

static CHardAc* s_ac = NULL;

void CEmuM37702BusSetAc(CHardAc* hw)
{
	s_ac = hw;
}

CHardAc* CEmuM37702BusGetAc()
{
	return s_ac;
}

static uint8_t M37702BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->M37702Read8(addr) : 0xff;
}

static void M37702BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->M37702Write8(addr, data);
}

void CEmuM37702BusAttach(M37702Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	M37702SetBus(cpu, hw, M37702BusRead, M37702BusWrite);
}
