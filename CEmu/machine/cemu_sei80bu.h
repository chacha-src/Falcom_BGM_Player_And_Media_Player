#pragma once
#include <stdint.h>

/* SEI80BU 二重復号（MAME sei80bu.cpp）。オペコードとデータは M1 で分岐 */
/* オペコードフェッチ用復号 */
uint8_t CEmuSei80buOpcode(uint16_t addr, uint8_t src);
/* データ読込用復号 */
uint8_t CEmuSei80buData(uint16_t addr, uint8_t src);
