#include "StdAfx.h"
#include "cemu_sei80bu.h"

static inline int Bit(uint16_t a, int n) { return (a >> n) & 1; }

static uint8_t BitSwap87654301(uint8_t s)
{
	return (uint8_t)((s & 0xfcu) | ((s & 1u) << 1) | ((s >> 1) & 1u));
}
static uint8_t BitSwap87654210(uint8_t s)
{
	return (uint8_t)((s & 0xf0u) | ((s & 0x04u) << 1) | ((s & 0x08u) >> 1)
		| (s & 0x03u));
}
static uint8_t BitSwap87645310(uint8_t s)
{
	return (uint8_t)((s & 0xc0u) | ((s & 0x10u) << 1) | ((s & 0x20u) >> 1)
		| (s & 0x0fu));
}
static uint8_t BitSwap76543210(uint8_t s)
{
	return (uint8_t)(((s & 0x40u) << 1) | ((s & 0x80u) >> 1) | (s & 0x3fu));
}

uint8_t CEmuSei80buData(uint16_t a, uint8_t src)
{
	if (Bit(a, 9) & Bit(a, 8)) src ^= 0x80;
	if (Bit(a, 11) & Bit(a, 4) & Bit(a, 1)) src ^= 0x40;
	if (Bit(a, 11) & !Bit(a, 8) & Bit(a, 1)) src ^= 0x04;
	if (Bit(a, 13) & !Bit(a, 6) & Bit(a, 4)) src ^= 0x02;
	if (!Bit(a, 11) & Bit(a, 9) & Bit(a, 2)) src ^= 0x01;

	if (Bit(a, 13) & Bit(a, 4)) src = BitSwap87654301(src);
	if (Bit(a, 8) & Bit(a, 4)) src = BitSwap87654210(src);
	return src;
}

uint8_t CEmuSei80buOpcode(uint16_t a, uint8_t src)
{
	if (Bit(a, 9) & Bit(a, 8)) src ^= 0x80;
	if (Bit(a, 11) & Bit(a, 4) & Bit(a, 1)) src ^= 0x40;
	if (!Bit(a, 13) & Bit(a, 12)) src ^= 0x20;
	if (!Bit(a, 6) & Bit(a, 1)) src ^= 0x10;
	if (!Bit(a, 12) & Bit(a, 2)) src ^= 0x08;
	if (Bit(a, 11) & !Bit(a, 8) & Bit(a, 1)) src ^= 0x04;
	if (Bit(a, 13) & !Bit(a, 6) & Bit(a, 4)) src ^= 0x02;
	if (!Bit(a, 11) & Bit(a, 9) & Bit(a, 2)) src ^= 0x01;

	if (Bit(a, 13) & Bit(a, 4)) src = BitSwap87654301(src);
	if (Bit(a, 8) & Bit(a, 4)) src = BitSwap87654210(src);
	if (Bit(a, 12) & Bit(a, 9)) src = BitSwap87645310(src);
	if (Bit(a, 11) & !Bit(a, 6)) src = BitSwap76543210(src);
	return src;
}
