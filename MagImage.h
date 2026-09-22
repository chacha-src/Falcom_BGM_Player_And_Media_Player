#pragma once
// MAKI02 / MAG (PC-98 16色)。WRD の @MAG 用。Woody-RINN MAG 仕様。
#include <string.h>

struct MagImage {
	int w;
	int h;
	int x0;
	int y0;
	unsigned pal[256]; // 0x00RRGGBB
	int nCol;
	unsigned* px;      // w*h、パレット番号（PC-98 16色は 0–15。@PAL/@FADE が効く）
	MagImage() : w(0), h(0), x0(0), y0(0), nCol(0), px(nullptr)
	{
		memset(pal, 0, sizeof(pal));
	}
};

void MagImageFree(MagImage* im);
int MagImageLoadPath(const wchar_t* path, MagImage* im);
int MagImageLoadMem(const unsigned char* data, unsigned size, MagImage* im);
