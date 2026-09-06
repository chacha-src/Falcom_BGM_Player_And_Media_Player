#pragma once
#include <stdint.h>

/* Capcom "Kabuki" Z80 opcode/data decrypt (MAME/kabuki.c, aosdk eng_qsf). */
struct CEmuKabukiKey {
	uint32_t swapKey1;
	uint32_t swapKey2;
	uint16_t addrKey;
	uint8_t xorKey;
};

/* Returns 1 and fills *out when archive is a known CPS1 QSound Kabuki set. */
int CEmuKabukiLookup(const char* archive, CEmuKabukiKey* out);

/* Decode length bytes from src into separate opcode and data planes. */
void CEmuKabukiDecode(const uint8_t* src, uint8_t* destOp, uint8_t* destData,
	int baseAddr, int length, uint32_t swapKey1, uint32_t swapKey2,
	uint16_t addrKey, uint8_t xorKey);
