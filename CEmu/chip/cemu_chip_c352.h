#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Namco C352、32ch 16bit PCM。 */
CChip* CEmuChipC352Create(uint32_t clockHz, int sampleRate);
void CEmuChipC352Destroy(CChip* c);
/* ワードレジスタ読み戻し。実チップおよび MAME c352_device::read と同じ。
   ボイスレジスタは 0x00-0xFF、制御レジスタは 0x200。 */
uint16_t CEmuChipC352Read(CChip* c, unsigned reg);
