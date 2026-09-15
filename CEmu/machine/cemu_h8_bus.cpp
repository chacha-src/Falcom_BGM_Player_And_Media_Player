#include "StdAfx.h"
#include "cemu_h8_bus.h"
#include "cemu_hard_ac.h"
extern "C" {
#include "../vendor/h8/h8core.h"
}

static CHardAc* g_h8Active = NULL;

/* アクティブ AC ボードを設定 */
void CEmuH8BusSetAc(CHardAc* hw)
{
	g_h8Active = hw;
}

/* アクティブ AC ボード */
CHardAc* CEmuH8BusGetAc()
{
	return g_h8Active;
}

/* H8 8bit 読込 → CHardAc::H8Read8 */
static uint8_t H8BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->H8Read8(addr) : 0xff;
}

/* H8 8bit 書込 → CHardAc::H8Write8 */
static void H8BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->H8Write8(addr, data);
}

/* H8 コアへバス callback を接続 */
void CEmuH8BusAttach(H8Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	H8SetBus(cpu, hw, H8BusRead, H8BusWrite);
}
