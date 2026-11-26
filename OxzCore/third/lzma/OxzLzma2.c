/* LZMA2 encoder wrapper. Public domain encoder (Igor Pavlov) lives beside this file. */
#ifndef Z7_ST
#define Z7_ST
#endif
#include "Precomp.h"
#include "Lzma2Enc.h"
#include <stdlib.h>
#include <string.h>

static void* OxzLzAlloc(ISzAllocPtr p, size_t size)
{
	(void)p;
	if (size == 0) return NULL;
	return malloc(size);
}

static void OxzLzFree(ISzAllocPtr p, void* address)
{
	(void)p;
	free(address);
}

typedef void (*OxzLzPulseFn)(void* user, unsigned long long done, unsigned long long span);

typedef struct {
	ICompressProgress vt;
	OxzLzPulseFn fn;
	void* user;
	unsigned long long span;
} OxzLzProg;

static SRes OxzLzOnProg(ICompressProgressPtr p, UInt64 inSize, UInt64 outSize)
{
	OxzLzProg* w = (OxzLzProg*)(void*)p;
	(void)outSize;
	if (w->fn && w->span)
		w->fn(w->user, (unsigned long long)inSize, w->span);
	return SZ_OK;
}

extern void* OxzLzmaAlloc(ISzAllocPtr p, size_t size);
extern void OxzLzmaFree(ISzAllocPtr p, void* address);

static const ISzAlloc kLzAlloc = { OxzLzAlloc, OxzLzFree };
static const ISzAlloc kLzBigAlloc = { OxzLzmaAlloc, OxzLzmaFree };

static int EncodeLzma2(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel, int wideDict, OxzLzPulseFn pulse, void* pulseUser)
{
	CLzma2EncProps props;
	CLzma2EncHandle enc;
	size_t comp, scale, basis;
	SRes res;
	unsigned dict, dictMax;
	if (!in || !out || !outSize || !prop || inSize == 0 || outCap < 16)
		return 1;
	scale = profile ? profile : inSize;
	basis = inSize;
	if (wideDict && scale > basis) basis = scale;
	Lzma2EncProps_Init(&props);
	if (scale <= 64ull * 1024ull * 1024ull) {
		props.lzmaProps.level = 9;
		props.lzmaProps.fb = 64;
		dictMax = 1u << 24;
	} else if (scale <= 256ull * 1024ull * 1024ull) {
		props.lzmaProps.level = 7;
		props.lzmaProps.fb = 48;
		dictMax = 1u << 23;
	} else {
		props.lzmaProps.level = 5;
		props.lzmaProps.fb = 32;
		dictMax = 1u << 22;
	}
	if (userLevel < 1) userLevel = 1;
	if (userLevel > 10) userLevel = 10;
	if (userLevel <= 2) {
		props.lzmaProps.level = userLevel;
		props.lzmaProps.algo = 0;
		props.lzmaProps.btMode = 0;
		props.lzmaProps.fb = 32;
		dictMax = 1u << (18 + (userLevel > 1 ? 1 : 0));
	} else {
		int hi = props.lzmaProps.level;
		props.lzmaProps.algo = 1;
		props.lzmaProps.btMode = 1;
		if (userLevel < 10) {
			unsigned cap = 1u << (16 + userLevel);
			props.lzmaProps.level = 1 + (hi - 1) * (userLevel - 1) / 9;
			if (props.lzmaProps.level < 5) props.lzmaProps.fb = 32;
			if (dictMax > cap) dictMax = cap;
		}
		if ((wideDict || userLevel < 10) && props.lzmaProps.fb > 32) props.lzmaProps.fb = 32;
		if (userLevel >= 7 && (wideDict || userLevel < 10)) props.lzmaProps.mc = 16;
	}
	props.lzmaProps.numThreads = 1;
	dict = 1u << 16;
	while ((size_t)dict < basis && dict < dictMax)
		dict <<= 1;
	props.lzmaProps.dictSize = dict;
	props.lzmaProps.reduceSize = (UInt64)basis;
	props.blockSize = LZMA2_ENC_PROPS_BLOCK_SIZE_SOLID;
	props.numBlockThreads_Max = 1;
	props.numTotalThreads = 1;
	props.numBlockThreads_Reduced = 1;
	enc = Lzma2Enc_Create(&kLzAlloc, &kLzBigAlloc);
	if (!enc) return 2;
	if (Lzma2Enc_SetProps(enc, &props) != SZ_OK) {
		Lzma2Enc_Destroy(enc);
		return 3;
	}
	*prop = Lzma2Enc_WriteProperties(enc);
	Lzma2Enc_SetDataSize(enc, (UInt64)inSize);
	comp = outCap;
	{
		OxzLzProg note;
		note.vt.Progress = OxzLzOnProg;
		note.fn = pulse;
		note.user = pulseUser;
		note.span = (unsigned long long)inSize;
		res = Lzma2Enc_Encode2(enc, NULL, (Byte*)out, &comp, NULL, (const Byte*)in, inSize, pulse ? &note.vt : NULL);
	}
	Lzma2Enc_Destroy(enc);
	if (res != SZ_OK || comp == 0) return 4;
	*outSize = comp;
	return 0;
}

int OxzLzma2Compress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel, OxzLzPulseFn pulse, void* pulseUser)
{
	int rc = EncodeLzma2(in, inSize, out, outCap, outSize, prop, profile, userLevel, 0, pulse, pulseUser);
	if (rc) return rc;
	if (*outSize >= inSize) return 4;
	return 0;
}

int OxzLzma2EncodeChunk(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel, OxzLzPulseFn pulse, void* pulseUser)
{
	return EncodeLzma2(in, inSize, out, outCap, outSize, prop, profile, userLevel, 1, pulse, pulseUser);
}
