#include "StdAfx.h"
#include "cemu_h8_bus.h"
#include "cemu_hard_ac.h"
extern "C" {
#include "../vendor/h8/h8core.h"
}

static CHardAc* g_h8Active = NULL;

void CEmuH8BusSetAc(CHardAc* hw)
{
	g_h8Active = hw;
}

CHardAc* CEmuH8BusGetAc()
{
	return g_h8Active;
}

static uint8_t H8BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->H8Read8(addr) : 0xff;
}

static void H8BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->H8Write8(addr, data);
}

void CEmuH8BusAttach(H8Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	H8SetBus(cpu, hw, H8BusRead, H8BusWrite);
}
