#include "stdafx.h"
#include "ogg.h"
#include "Soft3DTexRes.h"

#include <wincodec.h>
#include <d3dcompiler.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#endif

#ifndef S3TEX_RELEASE
#define S3TEX_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#endif

BOOL Soft3DTexLoadPngRes(int id, DWORD* dst, int dstW, int dstH)
{
	if (!dst || dstW <= 0 || dstH <= 0) return FALSE;
	HINSTANCE hi = AfxGetResourceHandle();
	HRSRC hrs = FindResource(hi, MAKEINTRESOURCE(id), RT_RCDATA);
	if (!hrs) return FALSE;
	HGLOBAL hg = LoadResource(hi, hrs);
	if (!hg) return FALSE;
	const DWORD n = SizeofResource(hi, hrs);
	const BYTE* mem = (const BYTE*)LockResource(hg);
	if (!mem || n < 24) return FALSE;

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
	return ok;
}

#ifndef S3CSO_RELEASE
#define S3CSO_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#endif

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
