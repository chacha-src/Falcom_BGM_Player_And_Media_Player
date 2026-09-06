#include "StdAfx.h"
#include "cemu_v35_bus.h"
#include "cemu_hard_ac.h"
extern "C" {
#include "../vendor/v35/v35core.h"
}

static CHardAc* g_m92Active = NULL;

void CEmuV35BusSetM92(CHardAc* hw)
{
	g_m92Active = hw;
}

CHardAc* CEmuV35BusGetM92()
{
	return g_m92Active;
}

static uint8_t V35BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->M92Read8(addr) : 0xff;
}

static void V35BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->M92Write8(addr, data);
}

/* The M92 sound V35 has no I/O-space peripherals; the whole board sits in
   memory space. Keep the hooks so the core never guesses. */
static uint8_t V35BusIn(void* ctx, uint16_t port)
{
	(void)ctx;
	(void)port;
	return 0xff;
}

static void V35BusOut(void* ctx, uint16_t port, uint8_t data)
{
	(void)ctx;
	(void)port;
	(void)data;
}

void CEmuV35BusAttach(V35Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	V35SetBus(cpu, hw, V35BusRead, V35BusWrite, V35BusIn, V35BusOut);
}
