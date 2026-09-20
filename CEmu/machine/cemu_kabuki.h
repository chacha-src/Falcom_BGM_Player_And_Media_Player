#pragma once
#include <stdint.h>

/* Capcom Kabuki Z80 オペコード／データ復号（MAME kabuki.c / aosdk eng_qsf） */
struct CEmuKabukiKey {
	uint32_t swapKey1;
	uint32_t swapKey2;
	uint16_t addrKey;
	uint8_t xorKey;
};

/* 既知の CPS1 QSound Kabuki セットなら 1 を返し *out を埋める */
int CEmuKabukiLookup(const char* archive, CEmuKabukiKey* out);

/* src の length バイトをオペコード面とデータ面へ復号する */
void CEmuKabukiDecode(const uint8_t* src, uint8_t* destOp, uint8_t* destData,
	int baseAddr, int length, uint32_t swapKey1, uint32_t swapKey2,
	uint16_t addrKey, uint8_t xorKey);

/* NEC MC-8123（MAME mc8123.cpp）。key は 8K。opcode 面と data 面を別バッファへ。
   アドレス i>=0xC000 は鍵引きが (i&0x3FFF)|0x8000。key[tbl]=0x00（xor 0xFF 後）は平文。 */
void CEmuMc8123Decode(const uint8_t* src, const uint8_t* key,
	uint8_t* destOp, uint8_t* destData, unsigned length);
