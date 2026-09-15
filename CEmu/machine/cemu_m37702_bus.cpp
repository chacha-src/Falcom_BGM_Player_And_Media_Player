#include "StdAfx.h"
#include "cemu_hard_ac.h"
#include "cemu_m37702_bus.h"
#include "m37702core.h"

static CHardAc* s_ac = NULL;

/* アクティブ AC ボードを設定 */
void CEmuM37702BusSetAc(CHardAc* hw)
{
	s_ac = hw;
}

/* アクティブ AC ボード */
CHardAc* CEmuM37702BusGetAc()
{
	return s_ac;
}

/* M37702 8bit 読込 → CHardAc::M37702Read8 */
static uint8_t M37702BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->M37702Read8(addr) : 0xff;
}

/* M37702 8bit 書込 → CHardAc::M37702Write8 */
static void M37702BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->M37702Write8(addr, data);
}

/* M37702 コアへバス callback を接続 */
void CEmuM37702BusAttach(M37702Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	M37702SetBus(cpu, hw, M37702BusRead, M37702BusWrite);
}
