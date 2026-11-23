/* PPMd (Dmitry Shkarin, public domain) via Igor Pavlov's Ppmd7z encoder. */
#include "Precomp.h"
#include "Ppmd7.h"
#include <stdlib.h>

typedef struct {
	IByteOut vt;
	unsigned char* buf;
	size_t cap;
	size_t pos;
	int overflow;
} PpmdOut;

static void PpmdWrite(IByteOutPtr p, Byte b)
{
	PpmdOut* o = (PpmdOut*)p;
	if (o->pos >= o->cap) {
		o->overflow = 1;
		return;
	}
	o->buf[o->pos++] = b;
}

static void* PpmdAlloc(ISzAllocPtr p, size_t size)
{
	(void)p;
	return size ? malloc(size) : NULL;
}

static void PpmdFree(ISzAllocPtr p, void* address)
{
	(void)p;
	free(address);
}

int OxzPpmdCompress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, size_t profile, int userLevel)
{
	static const ISzAlloc al = { PpmdAlloc, PpmdFree };
	CPpmd7* ppmd;
	PpmdOut bo;
	unsigned order;
	UInt32 mem;
	unsigned char* dst;
	size_t scale;
	if (!in || !out || !outSize || inSize == 0 || outCap < 8)
		return 1;
	scale = profile ? profile : inSize;
	if (userLevel < 1) userLevel = 1;
	if (userLevel > 10) userLevel = 10;
	if (userLevel <= 3) {
		order = 4;
		mem = 256u << 10;
	} else if (userLevel <= 6) {
		order = 5;
		mem = 1u << 20;
	} else if (scale > 8ull * 1024ull * 1024ull) {
		order = 4;
		mem = 1u << 20;
	} else if (scale > 256u * 1024u) {
		order = 6;
		mem = 2u << 20;
	} else if (scale < 4096) {
		order = 5;
		mem = 256u << 10;
	} else {
		order = 6;
		mem = 1u << 20;
	}
	dst = (unsigned char*)out;
	ppmd = (CPpmd7*)malloc(sizeof(CPpmd7));
	if (!ppmd) return 2;
	Ppmd7_Construct(ppmd);
	if (!Ppmd7_Alloc(ppmd, mem, &al)) {
		free(ppmd);
		return 3;
	}
	Ppmd7_Init(ppmd, order);
	bo.vt.Write = PpmdWrite;
	bo.buf = dst + 5;
	bo.cap = outCap - 5;
	bo.pos = 0;
	bo.overflow = 0;
	ppmd->rc.enc.Stream = &bo.vt;
	Ppmd7z_Init_RangeEnc(ppmd);
	Ppmd7z_EncodeSymbols(ppmd, (const Byte*)in, (const Byte*)in + inSize);
	Ppmd7z_Flush_RangeEnc(ppmd);
	Ppmd7_Free(ppmd, &al);
	free(ppmd);
	if (bo.overflow || bo.pos == 0 || bo.pos + 5 >= inSize)
		return 4;
	dst[0] = (unsigned char)order;
	dst[1] = (unsigned char)mem;
	dst[2] = (unsigned char)(mem >> 8);
	dst[3] = (unsigned char)(mem >> 16);
	dst[4] = (unsigned char)(mem >> 24);
	*outSize = bo.pos + 5;
	return 0;
}
