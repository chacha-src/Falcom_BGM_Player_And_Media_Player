#include "StdAfx.h"
#include "cemu_kabuki.h"
#include <string.h>

/* 既知 CPS1 QSound Kabuki 鍵（swap 桁は hex 符号化の順列） */
static const struct {
	const char* archive;
	uint32_t swap1;
	uint32_t swap2;
	uint16_t addr;
	uint8_t xork;
} kKeys[] = {
	{ "dino",     0x76543210u, 0x24601357u, 0x4343u, 0x43u },
	{ "dinou",    0x76543210u, 0x24601357u, 0x4343u, 0x43u },
	{ "dinoh",    0x76543210u, 0x24601357u, 0x4343u, 0x43u },
	{ "dinohunt", 0x76543210u, 0x24601357u, 0x4343u, 0x43u },
	{ "wof",      0x01234567u, 0x54163072u, 0x5151u, 0x51u },
	{ "wofa",     0x01234567u, 0x54163072u, 0x5151u, 0x51u },
	{ "wofj",     0x01234567u, 0x54163072u, 0x5151u, 0x51u },
	{ "wofu",     0x01234567u, 0x54163072u, 0x5151u, 0x51u },
	{ "wofh",     0x01234567u, 0x54163072u, 0x5151u, 0x51u },
	{ "punisher", 0x67452103u, 0x75316024u, 0x2222u, 0x22u },
	{ "punishru", 0x67452103u, 0x75316024u, 0x2222u, 0x22u },
	{ "punishrj", 0x67452103u, 0x75316024u, 0x2222u, 0x22u },
	{ "slammast", 0x54321076u, 0x65432107u, 0x3131u, 0x19u },
	{ "mbomberj", 0x54321076u, 0x65432107u, 0x3131u, 0x19u },
	{ "mbombrd",  0x54321076u, 0x65432107u, 0x3131u, 0x19u },
};

/* archive 名から Kabuki 鍵を引く。未知なら 0 */
int CEmuKabukiLookup(const char* archive, CEmuKabukiKey* out)
{
	if (!archive || !out) return 0;
	for (int i = 0; i < (int)(sizeof(kKeys) / sizeof(kKeys[0])); i++) {
		if (_stricmp(archive, kKeys[i].archive) != 0)
			continue;
		out->swapKey1 = kKeys[i].swap1;
		out->swapKey2 = kKeys[i].swap2;
		out->addrKey = kKeys[i].addr;
		out->xorKey = kKeys[i].xork;
		return 1;
	}
	return 0;
}

/* 下位ニブル側の 2bit 対スワップ */
static int BitSwap1(int src, int key, int sel)
{
	if (sel & (1 << ((key >> 0) & 7)))
		src = (src & 0xfc) | ((src & 0x01) << 1) | ((src & 0x02) >> 1);
	if (sel & (1 << ((key >> 4) & 7)))
		src = (src & 0xf3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1);
	if (sel & (1 << ((key >> 8) & 7)))
		src = (src & 0xcf) | ((src & 0x10) << 1) | ((src & 0x20) >> 1);
	if (sel & (1 << ((key >> 12) & 7)))
		src = (src & 0x3f) | ((src & 0x40) << 1) | ((src & 0x80) >> 1);
	return src;
}

/* 上位ニブル側の 2bit 対スワップ（BitSwap1 の逆順） */
static int BitSwap2(int src, int key, int sel)
{
	if (sel & (1 << ((key >> 12) & 7)))
		src = (src & 0xfc) | ((src & 0x01) << 1) | ((src & 0x02) >> 1);
	if (sel & (1 << ((key >> 8) & 7)))
		src = (src & 0xf3) | ((src & 0x04) << 1) | ((src & 0x08) >> 1);
	if (sel & (1 << ((key >> 4) & 7)))
		src = (src & 0xcf) | ((src & 0x10) << 1) | ((src & 0x20) >> 1);
	if (sel & (1 << ((key >> 0) & 7)))
		src = (src & 0x3f) | ((src & 0x40) << 1) | ((src & 0x80) >> 1);
	return src;
}

/* 1 バイト復号: swap → 回転 → XOR → もう一度 swap */
static int ByteDecode(int src, uint32_t swapKey1, uint32_t swapKey2,
	uint8_t xorKey, int sel)
{
	src = BitSwap1(src, (int)(swapKey1 & 0xffffu), sel & 0xff);
	src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
	src = BitSwap2(src, (int)(swapKey1 >> 16), sel & 0xff);
	src ^= (int)xorKey;
	src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
	src = BitSwap2(src, (int)(swapKey2 & 0xffffu), sel >> 8);
	src = ((src & 0x7f) << 1) | ((src & 0x80) >> 7);
	src = BitSwap1(src, (int)(swapKey2 >> 16), sel >> 8);
	return src;
}

/* オペコード面とデータ面を別 sel で復号する */
void CEmuKabukiDecode(const uint8_t* src, uint8_t* destOp, uint8_t* destData,
	int baseAddr, int length, uint32_t swapKey1, uint32_t swapKey2,
	uint16_t addrKey, uint8_t xorKey)
{
	if (!src || !destOp || !destData || length <= 0) return;
	for (int a = 0; a < length; a++) {
		const int selOp = (a + baseAddr) + (int)addrKey;
		destOp[a] = (uint8_t)ByteDecode(src[a], swapKey1, swapKey2, xorKey, selOp);
		const int selData = ((a + baseAddr) ^ 0x1fc0) + (int)addrKey + 1;
		destData[a] = (uint8_t)ByteDecode(src[a], swapKey1, swapKey2, xorKey, selData);
	}
}
