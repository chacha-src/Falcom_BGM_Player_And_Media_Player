#include "StdAfx.h"
#include "cemu_v35_bus.h"
#include "cemu_hard_ac.h"
extern "C" {
#include "../vendor/v35/v35core.h"
}

static CHardAc* g_m92Active = NULL;

/* アクティブ M92 ボードを設定 */
void CEmuV35BusSetM92(CHardAc* hw)
{
	g_m92Active = hw;
}

/* アクティブ M92 ボード */
CHardAc* CEmuV35BusGetM92()
{
	return g_m92Active;
}

/* V35 メモリ 8bit 読込 → CHardAc::M92Read8 */
static uint8_t V35BusRead(void* ctx, uint32_t addr)
{
	CHardAc* hw = (CHardAc*)ctx;
	return hw ? hw->M92Read8(addr) : 0xff;
}

/* V35 メモリ 8bit 書込 → CHardAc::M92Write8 */
static void V35BusWrite(void* ctx, uint32_t addr, uint8_t data)
{
	CHardAc* hw = (CHardAc*)ctx;
	if (hw) hw->M92Write8(addr, data);
}

/* M92 音源 V35 に I/O 空間の周辺は無い。ボード全体がメモリ空間。
   コアが推測しないようフックは残す。 */
/* I/O IN: 未使用。常に 0xFF */
static uint8_t V35BusIn(void* ctx, uint16_t port)
{
	(void)ctx;
	(void)port;
	return 0xff;
}

/* I/O OUT: 未使用 */
static void V35BusOut(void* ctx, uint16_t port, uint8_t data)
{
	(void)ctx;
	(void)port;
	(void)data;
}

/* V35 コアへメモリ／I/O callback を接続 */
void CEmuV35BusAttach(V35Cpu* cpu, CHardAc* hw)
{
	if (!cpu) return;
	V35SetBus(cpu, hw, V35BusRead, V35BusWrite, V35BusIn, V35BusOut);
}
