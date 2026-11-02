#include "stdafx.h"
#include "ogg.h"
#include "Soft3DTexRes.h"
#include "resource.h"

#include <wincodec.h>
#include <d3dcompiler.h>
#include <string.h>
#include <stdlib.h>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#endif

#ifndef S3TEX_RELEASE
#define S3TEX_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#endif

static void BonusExeDir(wchar_t* dir, int cch)
{
	dir[0] = 0;
	GetModuleFileNameW(NULL, dir, cch);
	wchar_t* sl = wcsrchr(dir, L'\\');
	if (sl) *sl = 0;
}

BOOL BonusImageRead(int id, BYTE** data, DWORD* len)
{
	if (data) *data = NULL;
	if (len) *len = 0;
	if (!data || !len || id <= 0) return FALSE;
	wchar_t rel[96] = {};
	if (id >= IDR_TAC_PH0 && id < IDR_TAC_PH0 + 28) {
		int i = id - IDR_TAC_PH0;
		int theme = (i < 14) ? i : i - 14;
		swprintf_s(rel, L"tactics\\terrain\\ph%d_%c.jpg", theme, (i < 14) ? L'a' : L'n');
	} else {
		struct Row { int id; const wchar_t* rel; };
		static const Row kRow[] = {
			{ IDR_S3TEX_M_WALL0, L"meiro\\m_wall0.png" }, { IDR_S3TEX_M_WALL1, L"meiro\\m_wall1.png" },
			{ IDR_S3TEX_M_WALL2, L"meiro\\m_wall2.png" }, { IDR_S3TEX_M_WALL3, L"meiro\\m_wall3.png" },
			{ IDR_S3TEX_M_FLOOR0, L"meiro\\m_floor0.png" }, { IDR_S3TEX_M_FLOOR1, L"meiro\\m_floor1.png" },
			{ IDR_S3TEX_M_FLOOR2, L"meiro\\m_floor2.png" }, { IDR_S3TEX_M_FLOOR3, L"meiro\\m_floor3.png" },
			{ IDR_S3TEX_M_MIRWALL, L"meiro\\m_mirwall.png" }, { IDR_S3TEX_M_MIRFLOOR, L"meiro\\m_mirfloor.png" },
			{ IDR_S3TEX_M_WALL0_D, L"meiro\\m_wall0_d.png" }, { IDR_S3TEX_M_WALL1_D, L"meiro\\m_wall1_d.png" },
			{ IDR_S3TEX_M_WALL2_D, L"meiro\\m_wall2_d.png" }, { IDR_S3TEX_M_WALL3_D, L"meiro\\m_wall3_d.png" },
			{ IDR_S3TEX_M_FLOOR0_D, L"meiro\\m_floor0_d.png" }, { IDR_S3TEX_M_FLOOR1_D, L"meiro\\m_floor1_d.png" },
			{ IDR_S3TEX_M_FLOOR2_D, L"meiro\\m_floor2_d.png" }, { IDR_S3TEX_M_FLOOR3_D, L"meiro\\m_floor3_d.png" },
			{ IDR_S3TEX_M_ITEM, L"meiro\\m_item.png" }, { IDR_S3TEX_M_GIMMICK, L"meiro\\m_gimmick.png" },
			{ IDR_S3TEX_M_GLASS, L"meiro\\m_glass.png" }, { IDR_S3TEX_M_BRICK2, L"meiro\\m_brick2.png" },
			{ IDR_S3TEX_M_ENV, L"meiro\\m_env.png" }, { IDR_S3TEX_M_SKY, L"meiro\\m_sky.png" },
			{ IDR_S3TEX_M_SKY2, L"meiro\\m_sky2.png" },
			{ IDR_S3TEX_R_FOREST, L"race\\r_forest.png" }, { IDR_S3TEX_R_RUINS, L"race\\r_ruins.png" },
			{ IDR_S3TEX_R_OIL, L"race\\r_oil.png" }, { IDR_S3TEX_R_NIGHT, L"race\\r_night.png" },
			{ IDR_S3TEX_R_UNDER, L"race\\r_under.png" }, { IDR_S3TEX_R_GRASS, L"race\\r_grass.png" },
			{ IDR_S3TEX_R_MESA, L"race\\r_mesa.png" }, { IDR_S3TEX_R_CLOUD, L"race\\r_cloud.png" },
			{ IDR_S3TEX_R_CRAFT, L"race\\r_craft.png" }, { IDR_S3TEX_R_BAND, L"race\\r_band.png" },
			{ IDR_S3TEX_R_FOREST_D, L"race\\r_forest_d.png" }, { IDR_S3TEX_R_RUINS_D, L"race\\r_ruins_d.png" },
			{ IDR_S3TEX_R_OIL_D, L"race\\r_oil_d.png" }, { IDR_S3TEX_R_NIGHT_D, L"race\\r_night_d.png" },
			{ IDR_S3TEX_R_UNDER_D, L"race\\r_under_d.png" }, { IDR_S3TEX_R_GRASS_D, L"race\\r_grass_d.png" },
			{ IDR_S3TEX_R_MESA_D, L"race\\r_mesa_d.png" }, { IDR_S3TEX_R_CLOUD_D, L"race\\r_cloud_d.png" },
			{ IDR_S3TEX_R_CRAFT_D, L"race\\r_craft_d.png" }, { IDR_S3TEX_R_WATER, L"race\\r_water.png" },
			{ IDR_S3TEX_R_OBS, L"race\\r_obs.png" }, { IDR_S3TEX_R_ENV, L"race\\r_env.png" },
			{ IDR_S3TEX_R_SKY, L"race\\r_sky.png" }, { IDR_S3TEX_R_SKY2, L"race\\r_sky2.png" },
			{ IDR_S3TEX_R_ITEM, L"race\\r_item.png" }, { IDR_S3TEX_R_WOOD, L"race\\r_wood.png" },
			{ IDR_S3TEX_R_ENV2, L"race\\r_env2.png" }, { IDR_S3TEX_R_SKY3, L"race\\r_sky3.png" },
			{ IDR_S3TEX_R_SKY4, L"race\\r_sky4.png" }, { IDR_S3TEX_R_WATER_D, L"race\\r_water_d.png" },
			{ IDR_S3TEX_R_OBS_D, L"race\\r_obs_d.png" },
			{ IDR_TAC_DETAIL, L"tactics\\tac_detail.png" }, { IDR_TAC_MAT, L"tactics\\tac_mat.png" },
			{ IDR_TAC_HAIRTEX, L"tactics\\tac_hair.png" },
			{ IDR_TAC_ASSETS, L"tactics\\tac_assets.bin" }, { IDR_TAC_SFX, L"tactics\\tac_sfx.bin" },
			{ IDR_TAC_MON, L"tactics\\tac_mon.bin" }, { IDR_TAC_GEAR, L"tactics\\tac_gear.bin" },
			{ IDR_TAC_AVA, L"tactics\\tac_ava.bin" }, { IDR_TAC_FACE, L"tactics\\tac_face.bin" },
			{ IDR_TAC_HAIR, L"tactics\\tac_hair.bin" },
		};
		const wchar_t* hit = NULL;
		for (int i = 0; i < (int)(sizeof(kRow) / sizeof(kRow[0])); i++) if (kRow[i].id == id) { hit = kRow[i].rel; break; }
		if (!hit) return FALSE;
		wcscpy_s(rel, hit);
	}
	wchar_t exe[MAX_PATH];
	BonusExeDir(exe, MAX_PATH);
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\tacticsbgm\\resource\\%s", exe, rel);
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return FALSE;
	DWORD sz = GetFileSize(h, NULL);
	if (sz < 24 || sz > 512u * 1024u * 1024u) { CloseHandle(h); return FALSE; }
	BYTE* b = (BYTE*)malloc(sz);
	DWORD got = 0;
	BOOL ok = b && ReadFile(h, b, sz, &got, NULL) && got == sz;
	CloseHandle(h);
	if (!ok) { free(b); return FALSE; }
	*data = b; *len = sz;
	return TRUE;
}

BOOL Soft3DTexLoadPngRes(int id, DWORD* dst, int dstW, int dstH)
{
	if (!dst || dstW <= 0 || dstH <= 0) return FALSE;
	BYTE* owned = NULL;
	DWORD n = 0;
	const BYTE* mem = NULL;
	if (BonusImageRead(id, &owned, &n)) mem = owned;
	if (!mem) {
		HINSTANCE hi = AfxGetResourceHandle();
		HRSRC hrs = FindResource(hi, MAKEINTRESOURCE(id), RT_RCDATA);
		if (!hrs) return FALSE;
		HGLOBAL hg = LoadResource(hi, hrs);
		if (!hg) return FALSE;
		n = SizeofResource(hi, hrs);
		mem = (const BYTE*)LockResource(hg);
	}
	if (!mem || n < 24) { free(owned); return FALSE; }

	IWICImagingFactory* fac = NULL;
	IWICStream* stream = NULL;
	IWICBitmapDecoder* dec = NULL;
	IWICBitmapFrameDecode* frame = NULL;
	IWICFormatConverter* conv = NULL;
	IWICBitmapScaler* scaler = NULL;
	IWICBitmapSource* src = NULL;
	BOOL ok = FALSE;
	UINT w = 0, h = 0;

	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&fac));
	if (FAILED(hr) || !fac) {
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&fac));
	}
	if (FAILED(hr) || !fac) goto done;
	if (FAILED(fac->CreateStream(&stream)) || !stream) goto done;
	if (FAILED(stream->InitializeFromMemory((BYTE*)mem, n))) goto done;
	if (FAILED(fac->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, &dec)) || !dec) goto done;
	if (FAILED(dec->GetFrame(0, &frame)) || !frame) goto done;
	if (FAILED(fac->CreateFormatConverter(&conv)) || !conv) goto done;
	if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
		NULL, 0.0, WICBitmapPaletteTypeCustom))) goto done;

	conv->GetSize(&w, &h);
	src = conv;
	if ((int)w != dstW || (int)h != dstH) {
		if (FAILED(fac->CreateBitmapScaler(&scaler)) || !scaler) goto done;
		if (FAILED(scaler->Initialize(conv, (UINT)dstW, (UINT)dstH, WICBitmapInterpolationModeFant))) goto done;
		src = scaler;
	}
	if (FAILED(src->CopyPixels(NULL, (UINT)(dstW * 4), (UINT)(dstW * dstH * 4), (BYTE*)dst))) goto done;
	ok = TRUE;
done:
	S3TEX_RELEASE(scaler);
	S3TEX_RELEASE(conv);
	S3TEX_RELEASE(frame);
	S3TEX_RELEASE(dec);
	S3TEX_RELEASE(stream);
	S3TEX_RELEASE(fac);
	free(owned);
	return ok;
}

#ifndef S3CSO_RELEASE
#define S3CSO_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#endif

static BOOL S3BlobLooksLikeDxbc(ID3DBlob* b)
{
	if (!b || b->GetBufferSize() < 4) return FALSE;
	const BYTE* p = (const BYTE*)b->GetBufferPointer();
	return p[0] == 'D' && p[1] == 'X' && p[2] == 'B' && p[3] == 'C';
}

static HRESULT S3LoadCsoFile(const wchar_t* path, ID3DBlob** out)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
	DWORD sz = GetFileSize(h, NULL);
	if (!sz || sz == INVALID_FILE_SIZE) { CloseHandle(h); return E_FAIL; }
	ID3DBlob* b = NULL;
	HRESULT hr = D3DCreateBlob(sz, &b);
	if (FAILED(hr) || !b) { CloseHandle(h); return hr; }
	DWORD rd = 0;
	if (!ReadFile(h, b->GetBufferPointer(), sz, &rd, NULL) || rd != sz) {
		CloseHandle(h); S3CSO_RELEASE(b); return E_FAIL;
	}
	CloseHandle(h);
	if (!S3BlobLooksLikeDxbc(b)) { S3CSO_RELEASE(b); return E_FAIL; }
	*out = b;
	return S_OK;
}

static HRESULT S3LoadCsoResource(int id, ID3DBlob** out)
{
	if (id <= 0) return E_FAIL;
	HINSTANCE hi = AfxGetResourceHandle();
	if (!hi) hi = (HINSTANCE)GetModuleHandleW(NULL);
	HRSRC r = FindResourceW(hi, MAKEINTRESOURCEW(id), RT_RCDATA);
	if (!r) return E_FAIL;
	HGLOBAL g = LoadResource(hi, r);
	if (!g) return E_FAIL;
	DWORD sz = SizeofResource(hi, r);
	const void* p = LockResource(g);
	if (!p || !sz) return E_FAIL;
	ID3DBlob* b = NULL;
	HRESULT hr = D3DCreateBlob(sz, &b);
	if (FAILED(hr) || !b) return hr;
	memcpy(b->GetBufferPointer(), p, sz);
	if (!S3BlobLooksLikeDxbc(b)) { S3CSO_RELEASE(b); return E_FAIL; }
	*out = b;
	return S_OK;
}

static HRESULT S3CompileHlslFile(const wchar_t* path, const char* entry, const char* profile, ID3DBlob** out)
{
	ID3DBlob* err = NULL;
	HRESULT hr = D3DCompileFromFile(path, NULL, NULL, entry, profile,
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
	if (FAILED(hr)) {
		S3CSO_RELEASE(err);
		hr = D3DCompileFromFile(path, NULL, NULL, entry, profile,
			D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, out, &err);
	}
	S3CSO_RELEASE(err);
	return hr;
}

static void S3ShaderDir(wchar_t* base, size_t n)
{
	base[0] = 0;
	GetModuleFileNameW(NULL, base, (DWORD)n);
	wchar_t* slash = wcsrchr(base, L'\\');
	if (slash) slash[1] = 0;
}

HRESULT Soft3DLoadCso(const wchar_t* set, const char* entry, const char* profile, int rid, const wchar_t* hlslLeaf, void** outBlob)
{
	if (!set || !entry || !profile || !outBlob) return E_INVALIDARG;
	*outBlob = NULL;
	ID3DBlob* blob = NULL;
	if (rid && SUCCEEDED(S3LoadCsoResource(rid, &blob))) { *outBlob = blob; return S_OK; }

	wchar_t base[MAX_PATH] = {};
	S3ShaderDir(base, MAX_PATH);
	wchar_t we[32];
	MultiByteToWideChar(CP_UTF8, 0, entry, -1, we, 32);
	wchar_t cso[MAX_PATH];
	_snwprintf_s(cso, MAX_PATH, _TRUNCATE, L"%sshaders\\cso\\%s_%s.cso", base, set, we);
	if (SUCCEEDED(S3LoadCsoFile(cso, &blob))) { *outBlob = blob; return S_OK; }

	wchar_t hlsl[MAX_PATH];
	if (hlslLeaf && hlslLeaf[0]) {
		_snwprintf_s(hlsl, MAX_PATH, _TRUNCATE, L"%sshaders\\%s", base, hlslLeaf);
		if (GetFileAttributesW(hlsl) != INVALID_FILE_ATTRIBUTES) {
			if (SUCCEEDED(S3CompileHlslFile(hlsl, entry, profile, &blob))) { *outBlob = blob; return S_OK; }
		}
		wchar_t cwd[MAX_PATH] = {};
		GetCurrentDirectoryW(MAX_PATH, cwd);
		size_t cl = wcslen(cwd);
		if (cl && cwd[cl - 1] != L'\\') wcscat_s(cwd, L"\\");
		_snwprintf_s(hlsl, MAX_PATH, _TRUNCATE, L"%sshaders\\%s", cwd, hlslLeaf);
		if (GetFileAttributesW(hlsl) != INVALID_FILE_ATTRIBUTES) {
			if (SUCCEEDED(S3CompileHlslFile(hlsl, entry, profile, &blob))) { *outBlob = blob; return S_OK; }
		}
	}
	return E_FAIL;
}
