#include "stdafx.h"
#include "Soft3DGfxQuality.h"
#include "Soft3DGpuInfo.h"

static const S3GfxQualityParams kS3GfxTable[10] = {
	/*0*/ { 0,    0, 0, 4,  0.35f, 0,   0,   0,    0,    0 },
	/*1*/ { 256,  0, 0, 4,  0.40f, 0,   0,   0,    2,  128 },
	/*2*/ { 512,  0, 0, 8,  0.50f, 240,135,  0,    4,  192 },
	/*3*/ { 768,  1, 1, 8,  0.55f, 320,180,256,    6,  256 },
	/*4*/ { 1024, 2, 1,16,  0.60f, 320,180,384,   10,  256 },
	/*5*/ { 1024, 2, 2,16,  0.70f, 320,180,384,   18,  384 }, /* ~current */
	/*6*/ { 1536, 2, 2,16,  0.75f, 480,270,512,   18,  512 },
	/*7*/ { 2048, 2, 2,16,  0.85f, 640,360,768,   20,  512 },
	/*8*/ { 3072, 2, 2,16,  0.90f, 800,450,1024,  20,  768 },
	/*9*/ { 4096, 2, 2,16,  1.00f, 960,540,1536,  20, 1024 },
};

int S3GfxQualityClamp(int level)
{
	if (level < 0) return 0;
	if (level > S3_GFX_QUALITY_MAX) return S3_GFX_QUALITY_MAX;
	return level;
}

const S3GfxQualityParams& S3GfxQualityGet(int level0to9)
{
	return kS3GfxTable[S3GfxQualityClamp(level0to9)];
}

static BOOL S3GfxNameLooksIgpu(const wchar_t* name)
{
	if (!name || !name[0]) return FALSE;
	wchar_t buf[160];
	wcsncpy_s(buf, name, _TRUNCATE);
	_wcsupr_s(buf);
	if (wcsstr(buf, L"UHD")) return TRUE;
	if (wcsstr(buf, L"HD GRAPHICS")) return TRUE;
	if (wcsstr(buf, L"IRIS")) return TRUE;
	if (wcsstr(buf, L"RADEON GRAPHICS") && !wcsstr(buf, L"RX ")) return TRUE; /* APU */
	if (wcsstr(buf, L"VEGA") && wcsstr(buf, L"GRAPHICS")) return TRUE;
	return FALSE;
}

int S3GfxQualitySuggestFromGpu()
{
	S3GpuProbe p = {};
	if (!S3GpuProbePrimary(&p) || p.software || !p.name[0])
		return 1;

	wchar_t up[160];
	wcsncpy_s(up, p.name, _TRUNCATE);
	_wcsupr_s(up);
	if (wcsstr(up, L"BASIC RENDER") || wcsstr(up, L"MICROSOFT BASIC") || wcsstr(up, L"WARP"))
		return 1;

	const UINT64 mb = p.vramBytes / (1024ull * 1024ull);
	int lv = 5;
	if (mb < 2048) lv = 2;
	else if (mb < 4096) lv = 3;
	else if (mb < 6144) lv = 5;
	else if (mb < 8192) lv = 6;
	else if (mb < 12288) lv = 7;
	else if (mb < 16384) lv = 8;
	else lv = 9;

	if (S3GfxNameLooksIgpu(p.name) && lv > 4)
		lv = 4;
	return S3GfxQualityClamp(lv);
}

void S3GfxQualityEnsureAutoDefault()
{
	if (savedata.s3_gfx_auto == 0) {
		savedata.s3_gfx_quality = S3GfxQualityClamp(savedata.s3_gfx_quality);
		return;
	}

	S3GpuProbe p = {};
	S3GpuProbePrimary(&p);
	const BOOL nameChanged = (p.name[0] && _wcsicmp(p.name, savedata.s3_gfx_gpu_tag) != 0);
	const BOOL neverTagged = (savedata.s3_gfx_gpu_tag[0] == 0);
	if (neverTagged || nameChanged) {
		savedata.s3_gfx_quality = S3GfxQualitySuggestFromGpu();
		wcsncpy_s(savedata.s3_gfx_gpu_tag, p.name, _TRUNCATE);
	} else {
		savedata.s3_gfx_quality = S3GfxQualityClamp(savedata.s3_gfx_quality);
	}
	savedata.s3_gfx_auto = 1;
}

void S3GfxQualityFillCombo(CComboBox& cb)
{
	if (!cb.GetSafeHwnd()) return;
	cb.ResetContent();
	for (int i = 0; i <= S3_GFX_QUALITY_MAX; i++) {
		CString s;
		s.Format(L"%d", i + 1);
		cb.AddString(s);
	}
}

LPCTSTR S3GfxQualityLabel()
{
	return LL14(
		L"画質", L"Gfx", L"Graph.", L"Graf.", L"Graf.",
		L"화질", L"画质", L"جودة", L"Кач.", L"Grafik",
		L"Graf.", L"Graf.", L"Graf.", L"Grafik");
}
