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
	{ "marukin",  0x54321076u, 0x54321076u, 0x4854u, 0x4fu },
	{ "marukina", 0x54321076u, 0x54321076u, 0x4854u, 0x4fu },
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

/* ---- NEC MC-8123（MAME src/devices/cpu/z80/mc8123.cpp を移植） ---- */

static uint8_t McBit(uint8_t v, unsigned n)
{
	return (uint8_t)((v >> n) & 1u);
}

static uint8_t McSwap8(uint8_t v,
	int b7, int b6, int b5, int b4, int b3, int b2, int b1, int b0)
{
	return (uint8_t)(
		((v >> b7) & 1) << 7 | ((v >> b6) & 1) << 6 |
		((v >> b5) & 1) << 5 | ((v >> b4) & 1) << 4 |
		((v >> b3) & 1) << 3 | ((v >> b2) & 1) << 2 |
		((v >> b1) & 1) << 1 | ((v >> b0) & 1) << 0);
}

static unsigned McSwap12Addr(unsigned addr)
{
	static const int src[12] = { 15, 14, 13, 12, 11, 10, 8, 6, 4, 2, 1, 0 };
	unsigned r = 0;
	for (int i = 0; i < 12; i++) {
		if (addr & (1u << src[i]))
			r |= 1u << (11 - i);
	}
	return r;
}

static uint8_t McDecryptType0(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 7, 5, 3, 1, 2, 0, 6, 4);
	if (swap == 1) val = McSwap8(val, 5, 3, 7, 2, 1, 0, 4, 6);
	if (swap == 2) val = McSwap8(val, 0, 3, 4, 6, 7, 1, 5, 2);
	if (swap == 3) val = McSwap8(val, 0, 7, 3, 2, 6, 4, 1, 5);
	if (McBit(param, 3) && McBit(val, 7)) val ^= (uint8_t)((1u << 5) | (1u << 3) | (1u << 0));
	if (McBit(param, 2) && McBit(val, 6)) val ^= (uint8_t)((1u << 7) | (1u << 2) | (1u << 1));
	if (McBit(val, 6)) val ^= (uint8_t)(1u << 7);
	if (McBit(param, 1) && McBit(val, 7)) val ^= (uint8_t)(1u << 6);
	if (McBit(val, 2)) val ^= (uint8_t)((1u << 5) | (1u << 0));
	val ^= (uint8_t)((1u << 4) | (1u << 3) | (1u << 1));
	if (McBit(param, 2)) val ^= (uint8_t)((1u << 5) | (1u << 2) | (1u << 0));
	if (McBit(param, 1)) val ^= (uint8_t)((1u << 7) | (1u << 6));
	if (McBit(param, 0)) val ^= (uint8_t)((1u << 5) | (1u << 0));
	if (McBit(param, 0)) val = McSwap8(val, 7, 6, 5, 1, 4, 3, 2, 0);
	return val;
}

static uint8_t McDecryptType1a(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 4, 2, 6, 5, 3, 7, 1, 0);
	if (swap == 1) val = McSwap8(val, 6, 0, 5, 4, 3, 2, 1, 7);
	if (swap == 2) val = McSwap8(val, 2, 3, 6, 1, 4, 0, 7, 5);
	if (swap == 3) val = McSwap8(val, 6, 5, 1, 3, 2, 7, 0, 4);
	if (McBit(param, 2)) val = McSwap8(val, 7, 6, 1, 5, 3, 2, 4, 0);
	if (McBit(val, 1)) val ^= (uint8_t)(1u << 0);
	if (McBit(val, 6)) val ^= (uint8_t)(1u << 3);
	if (McBit(val, 7)) val ^= (uint8_t)((1u << 6) | (1u << 3));
	if (McBit(val, 2)) val ^= (uint8_t)((1u << 6) | (1u << 3) | (1u << 1));
	if (McBit(val, 4)) val ^= (uint8_t)((1u << 7) | (1u << 6) | (1u << 2));
	if (McBit(val, 7) ^ McBit(val, 2)) val ^= (uint8_t)(1u << 4);
	val ^= (uint8_t)((1u << 6) | (1u << 3) | (1u << 1) | (1u << 0));
	if (McBit(param, 3)) val ^= (uint8_t)((1u << 7) | (1u << 2));
	if (McBit(param, 1)) val ^= (uint8_t)((1u << 6) | (1u << 3));
	if (McBit(param, 0)) val = McSwap8(val, 7, 6, 1, 4, 3, 2, 5, 0);
	return val;
}

static uint8_t McDecryptType1b(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 1, 0, 3, 2, 5, 6, 4, 7);
	if (swap == 1) val = McSwap8(val, 2, 0, 5, 1, 7, 4, 6, 3);
	if (swap == 2) val = McSwap8(val, 6, 4, 7, 2, 0, 5, 1, 3);
	if (swap == 3) val = McSwap8(val, 7, 1, 3, 6, 0, 2, 5, 4);
	if (McBit(val, 2) && McBit(val, 0)) val ^= (uint8_t)((1u << 7) | (1u << 4));
	if (McBit(val, 7)) val ^= (uint8_t)(1u << 2);
	if (McBit(val, 5)) val ^= (uint8_t)((1u << 7) | (1u << 2));
	if (McBit(val, 1)) val ^= (uint8_t)(1u << 5);
	if (McBit(val, 6)) val ^= (uint8_t)(1u << 1);
	if (McBit(val, 4)) val ^= (uint8_t)((1u << 6) | (1u << 5));
	if (McBit(val, 0)) val ^= (uint8_t)((1u << 6) | (1u << 2) | (1u << 1));
	if (McBit(val, 3)) val ^= (uint8_t)((1u << 7) | (1u << 6) | (1u << 2) | (1u << 1) | (1u << 0));
	val ^= (uint8_t)((1u << 6) | (1u << 4) | (1u << 0));
	if (McBit(param, 3)) val ^= (uint8_t)((1u << 4) | (1u << 1));
	if (McBit(param, 2)) val ^= (uint8_t)((1u << 7) | (1u << 6) | (1u << 3) | (1u << 0));
	if (McBit(param, 1)) val ^= (uint8_t)((1u << 4) | (1u << 3));
	if (McBit(param, 0)) val ^= (uint8_t)((1u << 6) | (1u << 2) | (1u << 1) | (1u << 0));
	return val;
}

static uint8_t McDecryptType2a(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 0, 1, 4, 3, 5, 6, 2, 7);
	if (swap == 1) val = McSwap8(val, 6, 3, 0, 5, 7, 4, 1, 2);
	if (swap == 2) val = McSwap8(val, 1, 6, 4, 5, 0, 3, 7, 2);
	if (swap == 3) val = McSwap8(val, 4, 6, 7, 5, 2, 3, 1, 0);
	if (McBit(val, 3) || (McBit(param, 1) && McBit(val, 2)))
		val = McSwap8(val, 6, 0, 7, 4, 3, 2, 1, 5);
	if (McBit(val, 5)) val ^= (uint8_t)(1u << 7);
	if (McBit(val, 6)) val ^= (uint8_t)(1u << 5);
	if (McBit(val, 0)) val ^= (uint8_t)(1u << 6);
	if (McBit(val, 4)) val ^= (uint8_t)((1u << 3) | (1u << 0));
	if (McBit(val, 1)) val ^= (uint8_t)(1u << 2);
	val ^= (uint8_t)((1u << 7) | (1u << 6) | (1u << 5) | (1u << 4) | (1u << 1));
	if (McBit(param, 2)) val ^= (uint8_t)((1u << 4) | (1u << 3) | (1u << 2) | (1u << 1) | (1u << 0));
	if (McBit(param, 3)) {
		if (McBit(param, 0))
			val = McSwap8(val, 7, 6, 5, 3, 4, 1, 2, 0);
		else
			val = McSwap8(val, 7, 6, 5, 1, 2, 4, 3, 0);
	} else if (McBit(param, 0)) {
		val = McSwap8(val, 7, 6, 5, 2, 1, 3, 4, 0);
	}
	return val;
}

static uint8_t McDecryptType2b(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 1, 3, 4, 6, 5, 7, 0, 2);
	if (swap == 1) val = McSwap8(val, 0, 1, 5, 4, 7, 3, 2, 6);
	if (swap == 2) val = McSwap8(val, 3, 5, 4, 1, 6, 2, 0, 7);
	if (swap == 3) val = McSwap8(val, 5, 2, 3, 0, 4, 7, 6, 1);
	if (McBit(val, 7) && McBit(val, 3)) val ^= (uint8_t)((1u << 6) | (1u << 4) | (1u << 0));
	if (McBit(val, 7)) val ^= (uint8_t)(1u << 2);
	if (McBit(val, 5)) val ^= (uint8_t)((1u << 7) | (1u << 3));
	if (McBit(val, 1)) val ^= (uint8_t)(1u << 5);
	if (McBit(val, 4)) val ^= (uint8_t)((1u << 7) | (1u << 5) | (1u << 3) | (1u << 1));
	if (McBit(val, 7) && McBit(val, 5)) val ^= (uint8_t)((1u << 4) | (1u << 0));
	if (McBit(val, 5) && McBit(val, 1)) val ^= (uint8_t)((1u << 4) | (1u << 0));
	if (McBit(val, 6)) val ^= (uint8_t)((1u << 7) | (1u << 5));
	if (McBit(val, 3)) val ^= (uint8_t)((1u << 7) | (1u << 6) | (1u << 5) | (1u << 1));
	if (McBit(val, 2)) val ^= (uint8_t)((1u << 3) | (1u << 1));
	val ^= (uint8_t)((1u << 7) | (1u << 3) | (1u << 2) | (1u << 1));
	if (McBit(param, 3)) val ^= (uint8_t)((1u << 6) | (1u << 3) | (1u << 1));
	if (McBit(param, 2)) val ^= (uint8_t)((1u << 7) | (1u << 6) | (1u << 5) | (1u << 3) | (1u << 2) | (1u << 1));
	if (McBit(param, 1)) val ^= (uint8_t)(1u << 7);
	if (McBit(param, 0)) val ^= (uint8_t)((1u << 5) | (1u << 2));
	return val;
}

static uint8_t McDecryptType3a(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 5, 3, 1, 7, 0, 2, 6, 4);
	if (swap == 1) val = McSwap8(val, 3, 1, 2, 5, 4, 7, 0, 6);
	if (swap == 2) val = McSwap8(val, 5, 6, 1, 2, 7, 0, 4, 3);
	if (swap == 3) val = McSwap8(val, 5, 6, 7, 0, 4, 2, 1, 3);
	if (McBit(val, 2)) val ^= (uint8_t)((1u << 7) | (1u << 5) | (1u << 4));
	if (McBit(val, 3)) val ^= (uint8_t)(1u << 0);
	if (McBit(param, 0)) val = McSwap8(val, 7, 2, 5, 4, 3, 1, 0, 6);
	if (McBit(val, 1)) val ^= (uint8_t)((1u << 6) | (1u << 0));
	if (McBit(val, 3)) val ^= (uint8_t)((1u << 4) | (1u << 2) | (1u << 1));
	if (McBit(param, 3)) val ^= (uint8_t)((1u << 4) | (1u << 3));
	if (McBit(val, 3)) val = McSwap8(val, 5, 6, 7, 4, 3, 2, 1, 0);
	if (McBit(val, 5)) val ^= (uint8_t)((1u << 2) | (1u << 1));
	val ^= (uint8_t)((1u << 6) | (1u << 5) | (1u << 4) | (1u << 3));
	if (McBit(param, 2)) val ^= (uint8_t)(1u << 7);
	if (McBit(param, 1)) val ^= (uint8_t)(1u << 4);
	if (McBit(param, 0)) val ^= (uint8_t)(1u << 0);
	return val;
}

static uint8_t McDecryptType3b(uint8_t val, uint8_t param, unsigned swap)
{
	if (swap == 0) val = McSwap8(val, 3, 7, 5, 4, 0, 6, 2, 1);
	if (swap == 1) val = McSwap8(val, 7, 5, 4, 6, 1, 2, 0, 3);
	if (swap == 2) val = McSwap8(val, 7, 4, 3, 0, 5, 1, 6, 2);
	if (swap == 3) val = McSwap8(val, 2, 6, 4, 1, 3, 7, 0, 5);
	if (McBit(val, 2)) val ^= (uint8_t)(1u << 7);
	if (McBit(val, 7)) val = McSwap8(val, 7, 6, 3, 4, 5, 2, 1, 0);
	if (McBit(param, 3)) val ^= (uint8_t)(1u << 7);
	if (McBit(val, 4)) val ^= (uint8_t)(1u << 6);
	if (McBit(val, 1)) val ^= (uint8_t)((1u << 6) | (1u << 4) | (1u << 2));
	if (McBit(val, 7) && McBit(val, 6)) val ^= (uint8_t)(1u << 1);
	if (McBit(val, 7)) val ^= (uint8_t)(1u << 1);
	if (McBit(param, 3)) val ^= (uint8_t)(1u << 7);
	if (McBit(param, 2)) val ^= (uint8_t)(1u << 0);
	if (McBit(param, 3)) val = McSwap8(val, 4, 6, 3, 2, 5, 0, 1, 7);
	if (McBit(val, 4)) val ^= (uint8_t)(1u << 1);
	if (McBit(val, 5)) val ^= (uint8_t)(1u << 4);
	if (McBit(val, 7)) val ^= (uint8_t)(1u << 2);
	val ^= (uint8_t)((1u << 5) | (1u << 3) | (1u << 2));
	if (McBit(param, 1)) val ^= (uint8_t)(1u << 7);
	if (McBit(param, 0)) val ^= (uint8_t)(1u << 3);
	return val;
}

static uint8_t McDecryptInternal(uint8_t val, uint8_t key, int opcode)
{
	unsigned type = 0, swap = 0;
	uint8_t param = 0;
	key ^= 0xff;
	if (key == 0x00)
		return val;
	type ^= (unsigned)McBit(key, 0) << 0;
	type ^= (unsigned)McBit(key, 2) << 0;
	type ^= (unsigned)McBit(key, 0) << 1;
	type ^= (unsigned)McBit(key, 1) << 1;
	type ^= (unsigned)McBit(key, 2) << 1;
	type ^= (unsigned)McBit(key, 4) << 1;
	type ^= (unsigned)McBit(key, 4) << 2;
	type ^= (unsigned)McBit(key, 5) << 2;
	swap ^= (unsigned)McBit(key, 0) << 0;
	swap ^= (unsigned)McBit(key, 1) << 0;
	swap ^= (unsigned)McBit(key, 2) << 1;
	swap ^= (unsigned)McBit(key, 3) << 1;
	param ^= (uint8_t)(McBit(key, 0) << 0);
	param ^= (uint8_t)(McBit(key, 0) << 1);
	param ^= (uint8_t)(McBit(key, 2) << 1);
	param ^= (uint8_t)(McBit(key, 3) << 1);
	param ^= (uint8_t)(McBit(key, 0) << 2);
	param ^= (uint8_t)(McBit(key, 1) << 2);
	param ^= (uint8_t)(McBit(key, 6) << 2);
	param ^= (uint8_t)(McBit(key, 1) << 3);
	param ^= (uint8_t)(McBit(key, 6) << 3);
	param ^= (uint8_t)(McBit(key, 7) << 3);
	if (!opcode) {
		param ^= (uint8_t)(1u << 0);
		type ^= 1u;
	}
	switch (type) {
	case 2: return McDecryptType1a(val, param, swap);
	case 3: return McDecryptType1b(val, param, swap);
	case 4: return McDecryptType2a(val, param, swap);
	case 5: return McDecryptType2b(val, param, swap);
	case 6: return McDecryptType3a(val, param, swap);
	case 7: return McDecryptType3b(val, param, swap);
	default: return McDecryptType0(val, param, swap);
	}
}

void CEmuMc8123Decode(const uint8_t* src, const uint8_t* key,
	uint8_t* destOp, uint8_t* destData, unsigned length)
{
	if (!src || !key || !destOp || !destData || !length)
		return;
	for (unsigned i = 0; i < length; i++) {
		const unsigned adr = (i >= 0xc000u) ? ((i & 0x3fffu) | 0x8000u) : i;
		const unsigned tbl = McSwap12Addr(adr);
		const uint8_t s = src[i];
		destOp[i] = McDecryptInternal(s, key[tbl], 1);
		destData[i] = McDecryptInternal(s, key[tbl | 0x1000u], 0);
	}
}
