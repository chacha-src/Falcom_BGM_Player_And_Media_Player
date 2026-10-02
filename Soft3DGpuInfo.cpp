#include "stdafx.h"
#include "Soft3DGpuInfo.h"
#include <dxgi.h>

#pragma comment(lib, "dxgi.lib")

/* 名前は EnumDisplayDevices、VRAM は DXGI の DedicatedVideoMemory だけ。
   EnumOutputs / QueryVideoMemoryInfo / ディスプレイクラス全レジストリ走査は
   切れたモニタやドライバ待ちで数十秒〜1分かかる。表示名には要らない。 */
static S3GpuProbe g_s3GpuCache;
static BOOL g_s3GpuCached = FALSE;

static BOOL S3GpuNameMatches(const wchar_t* a, const wchar_t* b)
{
	if (!a || !b || !a[0] || !b[0])
		return FALSE;
	if (_wcsicmp(a, b) == 0)
		return TRUE;
	return (wcsstr(a, b) != NULL || wcsstr(b, a) != NULL) ? TRUE : FALSE;
}

BOOL S3GpuProbePrimary(S3GpuProbe* out)
{
	if (!out) return FALSE;
	if (g_s3GpuCached) {
		*out = g_s3GpuCache;
		return out->name[0] != 0;
	}
	ZeroMemory(out, sizeof(*out));

	wchar_t primaryName[128] = {};
	DISPLAY_DEVICE dd = {};
	dd.cb = sizeof(dd);
	if (EnumDisplayDevices(NULL, 0, &dd, 0))
		wcsncpy_s(primaryName, dd.DeviceString, _TRUNCATE);

	IDXGIFactory1* factory = NULL;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
		IDXGIAdapter1* best = NULL;
		int bestScore = -1;
		for (UINT i = 0; i < 8; ++i) {
			IDXGIAdapter1* adapter = NULL;
			const HRESULT er = factory->EnumAdapters1(i, &adapter);
			if (er == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(er) || !adapter)
				continue;

			DXGI_ADAPTER_DESC1 desc = {};
			if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
				adapter->Release();
				continue;
			}
			if (desc.VendorId == 0x1414 && desc.DeviceId == 0x8c) {
				adapter->Release();
				continue;
			}

			int score = 1;
			if (desc.DedicatedVideoMemory > 0)
				score = 2;
			if (S3GpuNameMatches(desc.Description, primaryName))
				score = 3;

			if (score > bestScore) {
				if (best)
					best->Release();
				best = adapter;
				bestScore = score;
			} else {
				adapter->Release();
			}
			if (bestScore >= 3)
				break;
		}

		if (best) {
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(best->GetDesc1(&desc))) {
				wcsncpy_s(out->name, desc.Description, _TRUNCATE);
				out->vendorId = desc.VendorId;
				out->deviceId = desc.DeviceId;
				out->software = FALSE;
				UINT64 vram = (UINT64)desc.DedicatedVideoMemory;
				if (vram == 0)
					vram = (UINT64)desc.SharedSystemMemory;
				out->vramBytes = vram;
			}
			best->Release();
		}
		factory->Release();
	}

	if (!out->name[0] && primaryName[0])
		wcsncpy_s(out->name, primaryName, _TRUNCATE);

	g_s3GpuCache = *out;
	g_s3GpuCached = TRUE;
	return out->name[0] != 0;
}

void S3GpuFormatInfoString(const S3GpuProbe& p, CString& out)
{
	out.Empty();
	CString name(p.name);
	name.Trim();
	if (name.IsEmpty())
		return;
	const UINT64 mb64 = p.vramBytes / (1024ull * 1024ull);
	const UINT mb = (mb64 > 0xffffffffull) ? 0xffffffffu : (UINT)mb64;
	if (mb >= 1024)
		out.Format(_T("%s (%u GB)"), (LPCTSTR)name, (mb + 512u) / 1024u);
	else if (mb > 0)
		out.Format(_T("%s (%u MB)"), (LPCTSTR)name, mb);
	else
		out = name;
}

CString S3GpuBuildInfoString()
{
	S3GpuProbe p = {};
	S3GpuProbePrimary(&p);
	CString s;
	S3GpuFormatInfoString(p, s);
	return s;
}
