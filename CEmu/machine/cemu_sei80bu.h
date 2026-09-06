#pragma once
#include <stdint.h>

/* SEI80BU dual decrypt (MAME sei80bu.cpp). Opcode vs data differ by M1. */
uint8_t CEmuSei80buOpcode(uint16_t addr, uint8_t src);
uint8_t CEmuSei80buData(uint16_t addr, uint8_t src);
