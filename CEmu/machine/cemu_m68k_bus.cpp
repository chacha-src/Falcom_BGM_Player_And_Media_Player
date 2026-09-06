#include "StdAfx.h"
#include "cemu_m68k_bus.h"
#include "cemu_hard_x68k.h"
#include "cemu_hard_f3.h"
#include "cemu_hard_ac.h"

static CHardX68k* g_x68kActive = NULL;
static CHardF3* g_f3Active = NULL;
static CHardAc* g_ms1Active = NULL;

void CEmuM68kBusSetX68k(CHardX68k* hw)
{
	g_x68kActive = hw;
	if (hw) { g_f3Active = NULL; g_ms1Active = NULL; }
}

void CEmuM68kBusSetF3(CHardF3* hw)
{
	g_f3Active = hw;
	if (hw) { g_x68kActive = NULL; g_ms1Active = NULL; }
}

void CEmuM68kBusSetMs1(CHardAc* hw)
{
	g_ms1Active = hw;
	if (hw) { g_x68kActive = NULL; g_f3Active = NULL; }
}

CHardX68k* CEmuM68kBusGetX68k()
{
	return g_x68kActive;
}

CHardF3* CEmuM68kBusGetF3()
{
	return g_f3Active;
}

CHardAc* CEmuM68kBusGetMs1()
{
	return g_ms1Active;
}

/* Musashi global memory callbacks — exactly one board active. */
extern "C" unsigned int m68k_read_memory_8(unsigned int address)
{
	if (g_f3Active) return g_f3Active->Read8(address);
	if (g_ms1Active) return g_ms1Active->Ms1Read8(address);
	if (g_x68kActive) return g_x68kActive->Read8(address);
	return 0xff;
}

extern "C" unsigned int m68k_read_memory_16(unsigned int address)
{
	if (g_f3Active) return g_f3Active->Read16(address);
	if (g_ms1Active) return g_ms1Active->Ms1Read16(address);
	if (g_x68kActive) return g_x68kActive->Read16(address);
	return 0xffff;
}

extern "C" unsigned int m68k_read_memory_32(unsigned int address)
{
	if (g_f3Active) return g_f3Active->Read32(address);
	if (g_ms1Active)
		return (g_ms1Active->Ms1Read16(address) << 16)
			| g_ms1Active->Ms1Read16(address + 2);
	if (g_x68kActive) return g_x68kActive->Read32(address);
	return 0xffffffffu;
}

extern "C" void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	if (g_f3Active) { g_f3Active->Write8(address, (uint8_t)value); return; }
	if (g_ms1Active) { g_ms1Active->Ms1Write8(address, (uint8_t)value); return; }
	if (g_x68kActive) g_x68kActive->Write8(address, (uint8_t)value);
}

extern "C" void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	if (g_f3Active) { g_f3Active->Write16(address, (uint16_t)value); return; }
	if (g_ms1Active) { g_ms1Active->Ms1Write16(address, (uint16_t)value); return; }
	if (g_x68kActive) g_x68kActive->Write16(address, (uint16_t)value);
}

extern "C" void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	if (g_f3Active) { g_f3Active->Write32(address, (uint32_t)value); return; }
	if (g_ms1Active) {
		g_ms1Active->Ms1Write16(address, (uint16_t)(value >> 16));
		g_ms1Active->Ms1Write16(address + 2, (uint16_t)value);
		return;
	}
	if (g_x68kActive) g_x68kActive->Write32(address, (uint32_t)value);
}
