#include "StdAfx.h"
#include "cemu_hd63701_bus.h"
#include "cemu_hard_ac.h"
extern "C" {
#include "../vendor/hd63701/hd63701core.h"
}

static CHardAc* g_hd63701Active = NULL;

void CEmuHD63701BusSetAc(CHardAc* hw)
{
	g_hd63701Active = hw;
}

CHardAc* CEmuHD63701BusGetAc()
{
	return g_hd63701Active;
}

static uint8_t HD63701BusRead(void* ctx, uint16_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->HD63701Read8(addr) : 0xff;
}

static void HD63701BusWrite(void* ctx, uint16_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->HD63701Write8(addr, data);
}

static uint8_t HD63701BusPortRead(void* ctx, uint16_t port)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->HD63701PortRead(port) : 0xff;
}

static void HD63701BusPortWrite(void* ctx, uint16_t port, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->HD63701PortWrite(port, data);
}

void CEmuHD63701BusAttach(HD63701Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	HD63701SetBus(cpu, hw, HD63701BusRead, HD63701BusWrite,
		HD63701BusPortRead, HD63701BusPortWrite);
}
