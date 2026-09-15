/* Data East アーケード用 HuC6280 バス糊 */
#include "StdAfx.h"
#include "cemu_h6280_bus.h"
#include "cemu_hard_ac.h"
extern "C" {
#include "../vendor/h6280/h6280core.h"
}

static CHardAc* g_decoActive = NULL;

/* アクティブ DECO ボードを設定 */
void CEmuH6280BusSetDeco(CHardAc* hw)
{
	g_decoActive = hw;
}

/* アクティブ DECO ボード */
CHardAc* CEmuH6280BusGetDeco()
{
	return g_decoActive;
}

/* HuC6280 8bit 読込 → CHardAc::DecoRead8 */
static uint8_t H6280BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->DecoRead8(addr) : 0xff;
}

/* HuC6280 8bit 書込 → CHardAc::DecoWrite8 */
static void H6280BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->DecoWrite8(addr, data);
}

/* HuC6280 コアへバス callback を接続 */
void CEmuH6280BusAttach(H6280Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	H6280SetBus(cpu, hw, H6280BusRead, H6280BusWrite);
}
