#include "stdafx.h"
#include "MagImage.h"
#include <vector>

static unsigned MagU16(const unsigned char* p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static unsigned MagU32(const unsigned char* p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8)
		| ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned MagExpand4(unsigned v)
{
	v &= 15;
	if (!v) return 0;
	return (v << 4) | 0x0F;
}

void MagImageFree(MagImage* im)
{
	if (!im) return;
	delete[] im->px;
	memset(im, 0, sizeof(*im));
}

int MagImageLoadMem(const unsigned char* data, unsigned size, MagImage* im)
{
	if (!im) return 0;
	memset(im, 0, sizeof(*im));
	if (!data || size < 64) return 0;
	if (memcmp(data, "MAKI02  ", 8) != 0) return 0;

	unsigned hdrOff = 0;
	for (unsigned i = 12; i + 32 < size; ++i) {
		if (data[i] == 0x1A) {
			hdrOff = i + 1;
			break;
		}
	}
	if (!hdrOff || hdrOff + 32 + 48 > size) return 0;
	while (hdrOff < size && data[hdrOff] != 0)
		hdrOff++;
	if (hdrOff + 32 > size) return 0;

	const unsigned char* h = data + hdrOff;
	const unsigned screen = h[3];
	const int col256 = (screen & 0x80) ? 1 : 0;
	const unsigned x0 = MagU16(h + 4);
	const unsigned y0 = MagU16(h + 6);
	const unsigned x1 = MagU16(h + 8);
	const unsigned y1 = MagU16(h + 10);
	const unsigned unit = col256 ? 4u : 8u;
	unsigned w = (((x1 / unit) - (x0 / unit)) + 1) * unit;
	unsigned ht = (y1 >= y0) ? (y1 - y0 + 1) : 0;
	if (w < 8 || w > 1280 || ht < 1 || ht > 1600) return 0;

	const unsigned faOff = MagU32(h + 12);
	const unsigned fbOff = MagU32(h + 16);
	const unsigned fbSize = MagU32(h + 20);
	const unsigned pxOff = MagU32(h + 24);
	const unsigned pxSize = MagU32(h + 28);
	if (hdrOff + faOff >= size || hdrOff + fbOff >= size || hdrOff + pxOff >= size)
		return 0;
	const unsigned nCol = col256 ? 256u : 16u;
	if (hdrOff + 32 + nCol * 3 > size) return 0;
	const unsigned char* pal = data + hdrOff + 32;
	const unsigned char* fa = data + hdrOff + faOff;
	const unsigned faSize = (fbOff > faOff) ? (fbOff - faOff) : 0;
	const unsigned char* fb = data + hdrOff + fbOff;
	const unsigned char* pixels = data + hdrOff + pxOff;
	if (hdrOff + fbOff + fbSize > size || hdrOff + pxOff + pxSize > size)
		return 0;

	im->w = (int)w;
	im->h = (int)ht;
	im->x0 = (int)x0;
	im->y0 = (int)y0;
	im->nCol = (int)nCol;
	im->px = new (std::nothrow) unsigned[w * ht];
	if (!im->px) {
		memset(im, 0, sizeof(*im));
		return 0;
	}
	memset(im->px, 0, sizeof(unsigned) * w * ht);

	for (unsigned i = 0; i < nCol; ++i) {
		unsigned g = pal[i * 3 + 0];
		unsigned r = pal[i * 3 + 1];
		unsigned b = pal[i * 3 + 2];
		if (g <= 15 && r <= 15 && b <= 15) {
			r = MagExpand4(r);
			g = MagExpand4(g);
			b = MagExpand4(b);
		}
		im->pal[i] = (r << 16) | (g << 8) | b;
	}

	const unsigned copyX[16] = { 0, 1, 2, 4, 0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0 };
	const unsigned copyY[16] = { 0, 0, 0, 0, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16 };
	const unsigned copyPix = col256 ? 2u : 4u;
	const unsigned nUnit = w / unit;
	std::vector<unsigned char> line(nUnit, 0);
	unsigned faBit = 0;
	unsigned fbPos = 0;
	unsigned pxPos = 0;

	/* px は色ではなくパレット番号。WRD の @PAL/@FADE で後から色が変わる */
	for (unsigned y = 0; y < ht; ++y) {
		for (unsigned xu = 0; xu < nUnit; ++xu) {
			unsigned byteI = faBit / 8;
			unsigned bit = 7 - (faBit % 8);
			faBit++;
			if (byteI < faSize && (fa[byteI] & (1u << bit))) {
				if (fbPos < fbSize)
					line[xu] = (unsigned char)(line[xu] ^ fb[fbPos++]);
			}
		}
		unsigned dstX = 0;
		auto putNibble = [&](unsigned flag) {
			if (flag == 0) {
				if (col256) {
					for (int k = 0; k < 2 && dstX < w; ++k) {
						unsigned v = (pxPos < pxSize) ? pixels[pxPos++] : 0;
						im->px[y * w + dstX++] = v;
					}
				} else {
					for (int k = 0; k < 2 && dstX + 1 < w; ++k) {
						unsigned v = (pxPos < pxSize) ? pixels[pxPos++] : 0;
						im->px[y * w + dstX++] = (v >> 4) & 15;
						im->px[y * w + dstX++] = v & 15;
					}
				}
			} else {
				const unsigned dx = copyX[flag & 15] * copyPix;
				const unsigned dy = copyY[flag & 15];
				for (unsigned i = 0; i < copyPix && dstX < w; ++i) {
					unsigned sx = (dstX >= dx) ? (dstX - dx) : 0;
					unsigned sy = (y >= dy) ? (y - dy) : 0;
					im->px[y * w + dstX] = im->px[sy * w + sx];
					dstX++;
				}
			}
		};
		for (unsigned xu = 0; xu < nUnit; ++xu) {
			putNibble(line[xu] >> 4);
			putNibble(line[xu] & 15);
		}
	}

	if (screen & 1) {
		unsigned* doubled = new (std::nothrow) unsigned[w * ht * 2];
		if (doubled) {
			for (unsigned y = 0; y < ht; ++y) {
				memcpy(doubled + (y * 2) * w, im->px + y * w, w * sizeof(unsigned));
				memcpy(doubled + (y * 2 + 1) * w, im->px + y * w, w * sizeof(unsigned));
			}
			delete[] im->px;
			im->px = doubled;
			im->h = (int)(ht * 2);
		}
	}
	return 1;
}

int MagImageLoadPath(const wchar_t* path, MagImage* im)
{
	if (!path || !path[0] || !im) return 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 64 || size > 16 * 1024 * 1024) {
		CloseHandle(f);
		return 0;
	}
	std::vector<unsigned char> buf(size);
	const BOOL ok = ReadFile(f, buf.data(), size, &got, NULL);
	CloseHandle(f);
	if (!ok || got != size) return 0;
	return MagImageLoadMem(buf.data(), size, im);
}
