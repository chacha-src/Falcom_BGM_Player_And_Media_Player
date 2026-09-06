#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "cemu_mdx.h"
#include "fmmon_shadow.h"
#include "sasami_fmmon.h"

#include "mdx_util.h"
#include "mxdrv.h"
#include "mxdrv_context.h"
#include "mdc.h"

#define CEMU_MDX_POOL (8 * 1024 * 1024)

struct CEmuMdxImpl {
	MxdrvContext ctx;
	int started;
};

void CEmuMdxInit(CEmuMdxPlayer* p)
{
	if (!p) return;
	memset(p, 0, sizeof(*p));
}

void CEmuMdxClose(CEmuMdxPlayer* p)
{
	if (!p) return;
	if (p->impl) {
		CEmuMdxImpl* im = (CEmuMdxImpl*)p->impl;
		if (im->started) {
			MXDRV_End(&im->ctx);
			im->started = 0;
		}
		MxdrvContext_Terminate(&im->ctx);
		delete im;
		p->impl = NULL;
	}
	p->open = 0;
	p->curSample = 0;
	p->endSample = 0;
}

static DWORD CEmuMdxClampRate(DWORD sampleRate)
{
	if (sampleRate < 44100) return 22050;
	if (sampleRate < 48000) return 44100;
	return 48000;
}

static unsigned CEmuMdxBe32(const BYTE* p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

/* MDD/MDX packs often store a 16-slot BE32 offset table (tone @ 0x40). MXDRV wants BE16. */
static int CEmuMdxLooksDwordBody(const BYTE* mdx, DWORD mdxSize)
{
	if (!mdx || mdxSize < 64) return 0;
	unsigned first = CEmuMdxBe32(mdx);
	if (first < 0x20 || first >= mdxSize) return 0;
	for (int i = 0; i < 16; i++) {
		unsigned v = CEmuMdxBe32(mdx + i * 4);
		if ((v >> 16) != 0) return 0;
		if (v != 0 && v >= mdxSize) return 0;
	}
	return 1;
}

static int CEmuMdxHasTitleHeader(const BYTE* mdx, DWORD mdxSize)
{
	if (!mdx || !mdxSize) return 0;
	uint32_t ofs = 0;
	return MdxSeekFileImage(mdx, mdxSize, MDX_CHUNK_TYPE_MDX_BODY, &ofs) ? 1 : 0;
}

/* Own a normalized MDX image suitable for MdxUtil* (caller frees).
   pdxFileName: when wrapping a headerless body, bake this PDX/PCM.DAT name. */
static BYTE* CEmuMdxNormalize(const BYTE* mdx, DWORD mdxSize, DWORD* outSize,
	const char* pdxFileName)
{
	if (outSize) *outSize = 0;
	if (!mdx || !mdxSize) return NULL;

	BYTE* owned = NULL;
	unsigned sz = (unsigned)mdxSize;

	/* MDC → MDX (mdc2mdx takes ownership of its input buffer). */
	if (mdxSize >= 4 && CEmuMdxBe32(mdx) == 0x4d44431a) {
		owned = (BYTE*)malloc(mdxSize);
		if (!owned) return NULL;
		memcpy(owned, mdx, mdxSize);
		owned = mdc2mdx(owned, &sz);
		if (!owned || !sz) return NULL;
		mdx = owned;
		mdxSize = (DWORD)sz;
	}

	if (CEmuMdxHasTitleHeader(mdx, mdxSize)) {
		if (!owned) {
			owned = (BYTE*)malloc(mdxSize);
			if (!owned) return NULL;
			memcpy(owned, mdx, mdxSize);
		}
		if (outSize) *outSize = mdxSize;
		return owned;
	}

	/* Headerless body: optionally shrink BE32 table → BE16, then wrap title+PDX name. */
	const BYTE* body = mdx;
	DWORD bodySize = mdxSize;
	BYTE* converted = NULL;
	if (CEmuMdxLooksDwordBody(mdx, mdxSize)) {
		const unsigned shrink = 32; /* 16 dwords → 16 words */
		converted = (BYTE*)malloc(mdxSize - shrink);
		if (!converted) {
			if (owned) free(owned);
			return NULL;
		}
		for (int i = 0; i < 16; i++) {
			unsigned v = CEmuMdxBe32(mdx + i * 4);
			if (v) v -= shrink;
			converted[i * 2] = (BYTE)((v >> 8) & 0xff);
			converted[i * 2 + 1] = (BYTE)(v & 0xff);
		}
		memcpy(converted + 32, mdx + 64, mdxSize - 64);
		body = converted;
		bodySize = mdxSize - shrink;
	}

	size_t nameLen = (pdxFileName && pdxFileName[0]) ? strlen(pdxFileName) : 0;
	size_t hdrLen = 3 + nameLen + 1; /* CR LF 1A name NUL */
	BYTE* wrapped = (BYTE*)malloc(hdrLen + bodySize);
	if (!wrapped) {
		if (converted) free(converted);
		if (owned) free(owned);
		return NULL;
	}
	wrapped[0] = 0x0d;
	wrapped[1] = 0x0a;
	wrapped[2] = 0x1a;
	if (nameLen)
		memcpy(wrapped + 3, pdxFileName, nameLen);
	wrapped[3 + nameLen] = 0x00;
	memcpy(wrapped + hdrLen, body, bodySize);
	if (converted) free(converted);
	if (owned) free(owned);
	if (outSize) *outSize = (DWORD)(hdrLen + bodySize);
	return wrapped;
}

int CEmuMdxOpenBuffer(CEmuMdxPlayer* p,
	const BYTE* mdx, DWORD mdxSize,
	const BYTE* pdx, DWORD pdxSize,
	DWORD sampleRate, const wchar_t* srcPath,
	const char* pdxFileName)
{
	if (!p || !mdx || !mdxSize) return 0;
	CEmuMdxClose(p);

	DWORD normSize = 0;
	BYTE* norm = CEmuMdxNormalize(mdx, mdxSize, &normSize, pdxFileName);
	if (!norm || !normSize) return 0;

	uint32_t reqMdx = 0, reqPdx = 0;
	if (!MdxGetRequiredBufferSize(norm, normSize, pdxSize, &reqMdx, &reqPdx)) {
		free(norm);
		return 0;
	}
	if (reqMdx == 0) {
		free(norm);
		return 0;
	}

	BYTE* mdxBuf = (BYTE*)malloc(reqMdx);
	BYTE* pdxBuf = NULL;
	if (!mdxBuf) {
		free(norm);
		return 0;
	}
	if (reqPdx && pdx && pdxSize) {
		pdxBuf = (BYTE*)malloc(reqPdx);
		if (!pdxBuf) {
			free(mdxBuf);
			free(norm);
			return 0;
		}
	} else {
		reqPdx = 0;
		pdx = NULL;
		pdxSize = 0;
	}

	if (!MdxUtilCreateMdxPdxBuffer(norm, normSize, pdx, pdxSize,
			mdxBuf, reqMdx, pdxBuf, reqPdx)) {
		free(mdxBuf);
		if (pdxBuf) free(pdxBuf);
		free(norm);
		return 0;
	}
	free(norm);

	CEmuMdxImpl* im = new CEmuMdxImpl();
	memset(im, 0, sizeof(*im));
	if (!MxdrvContext_Initialize(&im->ctx, CEMU_MDX_POOL)) {
		delete im;
		free(mdxBuf);
		if (pdxBuf) free(pdxBuf);
		return 0;
	}

	sampleRate = CEmuMdxClampRate(sampleRate ? sampleRate : 44100);
	int start = MXDRV_Start(&im->ctx, (int)sampleRate, 0, 0, 0,
		(int)(reqMdx * 2), (int)(reqPdx ? reqPdx * 2 : 1024), 0);
	if (start != 0) {
		MxdrvContext_Terminate(&im->ctx);
		delete im;
		free(mdxBuf);
		if (pdxBuf) free(pdxBuf);
		return 0;
	}
	im->started = 1;

	{
		unsigned bodyPtr = ((unsigned)mdxBuf[4] << 8) | mdxBuf[5];
		if (bodyPtr + 4 < reqMdx) {
			unsigned expcmOfs = ((unsigned)mdxBuf[bodyPtr + 2] << 8) | mdxBuf[bodyPtr + 3];
			unsigned expcm = 0;
			if (bodyPtr + expcmOfs < reqMdx)
				expcm = (mdxBuf[bodyPtr + expcmOfs] == 0xe8) ? 1 : 0;
			unsigned char* pcm8 = (unsigned char*)MXDRV_GetWork(&im->ctx, MXDRV_WORK_PCM8);
			if (pcm8) *pcm8 = (unsigned char)expcm;
		}
	}

	uint32_t length1 = 0, length2 = 0;
	int loop = 0;
	__try {
		length1 = MXDRV_MeasurePlayTime(&im->ctx, mdxBuf, reqMdx, pdxBuf, reqPdx, 1, 0);
		length2 = MXDRV_MeasurePlayTime(&im->ctx, mdxBuf, reqMdx, pdxBuf, reqPdx, 2, 0);
		loop = (int)length2 - (int)length1;
		if (loop > 0 && length1 > 2000) length1 -= 2000;
		MXDRV_Play(&im->ctx, mdxBuf, reqMdx, pdxBuf, reqPdx);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		MXDRV_End(&im->ctx);
		MxdrvContext_Terminate(&im->ctx);
		delete im;
		free(mdxBuf);
		if (pdxBuf) free(pdxBuf);
		return 0;
	}
	free(mdxBuf);
	if (pdxBuf) free(pdxBuf);

	p->impl = im;
	p->sampleRate = sampleRate;
	p->curSample = 0;
	if (loop <= 0) {
		UINT64 ms = (UINT64)length1 + 1000ull;
		p->endSample = ms * (UINT64)sampleRate / 1000ull;
	} else {
		p->endSample = (UINT64)-1;
	}
	if (srcPath && srcPath[0])
		wcsncpy_s(p->sourcePath, srcPath, _TRUNCATE);
	else
		p->sourcePath[0] = 0;

	FmMonShadowReset();
	FmMonShadowSetSource(p->sourcePath);
	FmMonShadowSetSampleRate(p->sampleRate);
	FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	p->open = 1;
	return 1;
}

int CEmuMdxSeek(CEmuMdxPlayer* p, UINT64 sample, DWORD flags)
{
	(void)flags;
	if (!p || !p->impl) return 0;
	CEmuMdxImpl* im = (CEmuMdxImpl*)p->impl;
	if (p->endSample != (UINT64)-1 && sample > p->endSample)
		sample = p->endSample;
	UINT64 ms = (sample * 1000ull) / (p->sampleRate ? p->sampleRate : 44100);
	MXDRV_PlayAt(&im->ctx, (uint32_t)ms, 0x7FFFFFFF, 0);
	p->curSample = sample;
	return 1;
}

int CEmuMdxRender(CEmuMdxPlayer* p, short* outStereo, int sampleFrames)
{
	if (!p || !p->impl || !outStereo || sampleFrames <= 0) return 0;
	if (p->curSample >= p->endSample) return 0;
	CEmuMdxImpl* im = (CEmuMdxImpl*)p->impl;
	DWORD want = (DWORD)sampleFrames;
	if (p->endSample != (UINT64)-1 && p->curSample + want > p->endSample)
		want = (DWORD)(p->endSample - p->curSample);
	if (!want) return 0;
	MXDRV_GetPCM(&im->ctx, outStereo, (int)want);
	p->curSample += want;

	FmMonShadowSetSampleRate(p->sampleRate);
	FmMonShadowAddSamples(want);
	FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MDX);
	{
		uint8_t opm[256];
		for (int i = 0; i < 256; i++) {
			uint8_t v = 0;
			MxdrvContext_GetOpmReg(&im->ctx, (uint8_t)i, &v, NULL);
			opm[i] = v;
		}
		FmMonShadowSetOpmRegSnapshot(opm);
		for (int i = 0; i < 8; i++) {
			bool sum = false;
			MxdrvContext_GetPcmKeyOn(&im->ctx, (uint8_t)i, &sum);
			FmMonShadowPcmNote(i, sum ? 60 : 0, sum ? 1 : 0);
		}
	}
	FmMonShadowFlushKeysOnly(0);
	return (int)want;
}

UINT64 CEmuMdxLengthSamples(const CEmuMdxPlayer* p)
{
	if (!p) return 0;
	if (p->endSample == (UINT64)-1) return 0;
	return p->endSample;
}

BYTE* CEmuMdxConvertPcmDatToPdx(const BYTE* pcm, DWORD pcmSize, DWORD* outSize)
{
	if (outSize) *outSize = 0;
	if (!pcm || pcmSize < 512 + 16) return NULL;
	unsigned starts[64], ends[64];
	int nslot = 0;
	unsigned minOff = pcmSize, maxEnd = 512;
	for (int i = 0; i < 64; i++) {
		unsigned a = ((unsigned)pcm[i * 8] << 24) | ((unsigned)pcm[i * 8 + 1] << 16)
			| ((unsigned)pcm[i * 8 + 2] << 8) | (unsigned)pcm[i * 8 + 3];
		unsigned b = ((unsigned)pcm[i * 8 + 4] << 24) | ((unsigned)pcm[i * 8 + 5] << 16)
			| ((unsigned)pcm[i * 8 + 6] << 8) | (unsigned)pcm[i * 8 + 7];
		starts[i] = a;
		ends[i] = b;
		if (b > a && a >= 512 && b <= pcmSize) {
			nslot++;
			if (a < minOff) minOff = a;
			if (b > maxEnd) maxEnd = b;
		}
	}
	if (nslot < 1) return NULL;
	const unsigned hdr = 768;
	const unsigned payload = maxEnd > minOff ? maxEnd - minOff : 0;
	if (!payload) return NULL;
	unsigned total = hdr + payload;
	BYTE* pdx = (BYTE*)malloc(total);
	if (!pdx) return NULL;
	memset(pdx, 0, hdr);
	memcpy(pdx + hdr, pcm + minOff, payload);
	for (int i = 0; i < 64 && i < 96; i++) {
		unsigned a = starts[i], b = ends[i];
		if (b <= a || a < minOff || b > maxEnd) continue;
		unsigned ptr = hdr + (a - minOff);
		unsigned len = b - a;
		pdx[i * 8 + 0] = (BYTE)((ptr >> 24) & 0xff);
		pdx[i * 8 + 1] = (BYTE)((ptr >> 16) & 0xff);
		pdx[i * 8 + 2] = (BYTE)((ptr >> 8) & 0xff);
		pdx[i * 8 + 3] = (BYTE)(ptr & 0xff);
		pdx[i * 8 + 4] = (BYTE)((len >> 24) & 0xff);
		pdx[i * 8 + 5] = (BYTE)((len >> 16) & 0xff);
		pdx[i * 8 + 6] = (BYTE)((len >> 8) & 0xff);
		pdx[i * 8 + 7] = (BYTE)(len & 0xff);
	}
	if (outSize) *outSize = total;
	return pdx;
}
