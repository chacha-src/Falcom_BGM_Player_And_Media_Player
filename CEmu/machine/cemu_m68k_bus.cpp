#include "StdAfx.h"
#include "cemu_m68k_bus.h"
#include "cemu_hard_x68k.h"
#include "cemu_hard_f3.h"
#include "cemu_hard_ac.h"
#include "cemu_hard_pico.h"

static CHardX68k* g_x68kActive = NULL;
static CHardF3* g_f3Active = NULL;
static CHardAc* g_ms1Active = NULL;
static CHardPico* g_picoActive = NULL;

/* X68k をアクティブにし、F3/MS1/Pico を外す */
void CEmuM68kBusSetX68k(CHardX68k* hw)
{
	g_x68kActive = hw;
	if (hw) { g_f3Active = NULL; g_ms1Active = NULL; g_picoActive = NULL; }
}

/* F3 をアクティブにし、X68k/MS1/Pico を外す */
void CEmuM68kBusSetF3(CHardF3* hw)
{
	g_f3Active = hw;
	if (hw) { g_x68kActive = NULL; g_ms1Active = NULL; g_picoActive = NULL; }
}

/* Mega System 1 をアクティブにし、X68k/F3/Pico を外す */
void CEmuM68kBusSetMs1(CHardAc* hw)
{
	g_ms1Active = hw;
	if (hw) { g_x68kActive = NULL; g_f3Active = NULL; g_picoActive = NULL; }
}

/* Pico をアクティブにし、X68k/F3/MS1 を外す */
void CEmuM68kBusSetPico(CHardPico* hw)
{
	g_picoActive = hw;
	if (hw) { g_x68kActive = NULL; g_f3Active = NULL; g_ms1Active = NULL; }
}

/* アクティブ X68k（無ければ NULL） */
CHardX68k* CEmuM68kBusGetX68k()
{
	return g_x68kActive;
}

/* アクティブ F3（無ければ NULL） */
CHardF3* CEmuM68kBusGetF3()
{
	return g_f3Active;
}

/* アクティブ MS1（無ければ NULL） */
CHardAc* CEmuM68kBusGetMs1()
{
	return g_ms1Active;
}

/* アクティブ Pico（無ければ NULL） */
CHardPico* CEmuM68kBusGetPico()
{
	return g_picoActive;
}

/* Musashi グローバルメモリ callback。アクティブは常に 1 ボード */
/* 8bit 読込: F3 → MS1 → X68k の順でディスパッチ */
extern "C" unsigned int m68k_read_memory_8(unsigned int address)
{
	if (g_f3Active) return g_f3Active->Read8(address);
	if (g_picoActive) return g_picoActive->Read8(address);
	if (g_ms1Active) return g_ms1Active->Ms1Read8(address);
	if (g_x68kActive) return g_x68kActive->Read8(address);
	return 0xff;
}

/* 16bit 読込 */
extern "C" unsigned int m68k_read_memory_16(unsigned int address)
{
	if (g_f3Active) return g_f3Active->Read16(address);
	if (g_picoActive) return g_picoActive->Read16(address);
	if (g_ms1Active) return g_ms1Active->Ms1Read16(address);
	if (g_x68kActive) return g_x68kActive->Read16(address);
	return 0xffff;
}

/* 32bit 読込。MS1 は 16bit を 2 回 */
extern "C" unsigned int m68k_read_memory_32(unsigned int address)
{
	if (g_f3Active) return g_f3Active->Read32(address);
	if (g_picoActive) return g_picoActive->Read32(address);
	if (g_ms1Active)
		return (g_ms1Active->Ms1Read16(address) << 16)
			| g_ms1Active->Ms1Read16(address + 2);
	if (g_x68kActive) return g_x68kActive->Read32(address);
	return 0xffffffffu;
}

/* 8bit 書込 */
extern "C" void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	if (g_f3Active) { g_f3Active->Write8(address, (uint8_t)value); return; }
	if (g_picoActive) { g_picoActive->Write8(address, (uint8_t)value); return; }
	if (g_ms1Active) { g_ms1Active->Ms1Write8(address, (uint8_t)value); return; }
	if (g_x68kActive) g_x68kActive->Write8(address, (uint8_t)value);
}

/* 16bit 書込 */
extern "C" void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	if (g_f3Active) { g_f3Active->Write16(address, (uint16_t)value); return; }
	if (g_picoActive) { g_picoActive->Write16(address, (uint16_t)value); return; }
	if (g_ms1Active) { g_ms1Active->Ms1Write16(address, (uint16_t)value); return; }
	if (g_x68kActive) g_x68kActive->Write16(address, (uint16_t)value);
}

/* 32bit 書込。MS1 は 16bit を 2 回 */
extern "C" void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	if (g_f3Active) { g_f3Active->Write32(address, (uint32_t)value); return; }
	if (g_picoActive) { g_picoActive->Write32(address, (uint32_t)value); return; }
	if (g_ms1Active) {
		g_ms1Active->Ms1Write16(address, (uint16_t)(value >> 16));
		g_ms1Active->Ms1Write16(address + 2, (uint16_t)value);
		return;
	}
	if (g_x68kActive) g_x68kActive->Write32(address, (uint32_t)value);
}
