#include "OxzFilt.h"
#include <string.h>

static int OxzInterestingOp(uint8_t op)
{
	switch (op) {
	case 0x01: case 0x03: case 0x09: case 0x0B:
	case 0x21: case 0x23: case 0x29: case 0x2B:
	case 0x31: case 0x33: case 0x39: case 0x3B:
	case 0x63: case 0x69: case 0x6B:
	case 0x80: case 0x81: case 0x83: case 0x85: case 0x87:
	case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8D:
	case 0xC6: case 0xC7: case 0xF7: case 0xFF:
		return 1;
	default:
		return 0;
	}
}

static int OxzInterestingOp2(uint8_t op)
{
	switch (op) {
	case 0x10: case 0x11: case 0x28: case 0x29:
	case 0x6E: case 0x7E: case 0xA3: case 0xAB:
	case 0xAF: case 0xB0: case 0xB1: case 0xB6:
	case 0xB7: case 0xBE: case 0xBF:
		return 1;
	default:
		return 0;
	}
}

static void OxzConvAt(uint8_t* d, size_t pos, size_t next, int encoding)
{
	int32_t v;
	memcpy(&v, d + pos, 4);
	if (encoding)
		v = (int32_t)((uint32_t)next + (uint32_t)v);
	else
		v = (int32_t)((uint32_t)v - (uint32_t)next);
	memcpy(d + pos, &v, 4);
}

void OxzRipConvert(uint8_t* d, size_t size, int encoding)
{
	size_t i = 0;
	if (!d || size < 6) return;
	while (i + 6 <= size) {
		if (d[i] == 0xFF && (d[i + 1] == 0x15 || d[i + 1] == 0x25)) {
			OxzConvAt(d, i + 2, i + 6, encoding);
			i += 6;
			continue;
		}
		if (d[i] >= 0x40 && d[i] <= 0x4F && i + 7 <= size) {
			uint8_t op = d[i + 1];
			if (op != 0x0F && OxzInterestingOp(op) && (d[i + 2] & 0xC7) == 0x05) {
				OxzConvAt(d, i + 3, i + 7, encoding);
				i += 7;
				continue;
			}
			if (op == 0x0F && i + 8 <= size && OxzInterestingOp2(d[i + 2]) && (d[i + 3] & 0xC7) == 0x05) {
				OxzConvAt(d, i + 4, i + 8, encoding);
				i += 8;
				continue;
			}
		}
		i++;
	}
}
