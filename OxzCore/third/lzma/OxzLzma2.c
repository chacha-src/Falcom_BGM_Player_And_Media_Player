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

int OxzLzma2Compress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile)
{
	static const ISzAlloc kAlloc = { OxzLzAlloc, OxzLzFree };
	CLzma2EncProps props;
	CLzma2EncHandle enc;
	size_t comp, scale;
	SRes res;
	unsigned dict, dictMax;
	if (!in || !out || !outSize || !prop || inSize == 0 || outCap < 16)
		return 1;
	scale = profile ? profile : inSize;
	Lzma2EncProps_Init(&props);
	if (scale <= 64ull * 1024ull * 1024ull) {
		props.lzmaProps.level = 9;
		props.lzmaProps.fb = 273;
		dictMax = 1u << 25;
	} else if (scale <= 256ull * 1024ull * 1024ull) {
		props.lzmaProps.level = 7;
		props.lzmaProps.fb = 128;
		dictMax = 1u << 24;
	} else {
		props.lzmaProps.level = 5;
		props.lzmaProps.fb = 64;
		dictMax = 1u << 23;
	}
	props.lzmaProps.algo = 1;
	props.lzmaProps.btMode = 1;
	props.lzmaProps.numThreads = 1;
	dict = 1u << 16;
	while ((size_t)dict < inSize && dict < dictMax)
		dict <<= 1;
	props.lzmaProps.dictSize = dict;
	props.lzmaProps.reduceSize = (UInt64)inSize;
	props.blockSize = LZMA2_ENC_PROPS_BLOCK_SIZE_SOLID;
	props.numBlockThreads_Max = 1;
	props.numTotalThreads = 1;
	props.numBlockThreads_Reduced = 1;
	enc = Lzma2Enc_Create(&kAlloc, &kAlloc);
	if (!enc) return 2;
	if (Lzma2Enc_SetProps(enc, &props) != SZ_OK) {
		Lzma2Enc_Destroy(enc);
		return 3;
	}
	*prop = Lzma2Enc_WriteProperties(enc);
	Lzma2Enc_SetDataSize(enc, (UInt64)inSize);
	comp = outCap;
	res = Lzma2Enc_Encode2(enc, NULL, (Byte*)out, &comp, NULL, (const Byte*)in, inSize, NULL);
	Lzma2Enc_Destroy(enc);
	if (res != SZ_OK || comp == 0 || comp >= inSize) return 4;
	*outSize = comp;
	return 0;
}
