#include "stdafx.h"
#include "ogg.h"
#include "Soft3DTexRes.h"

#include <wincodec.h>

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
