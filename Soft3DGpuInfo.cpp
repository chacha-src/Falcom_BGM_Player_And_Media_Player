#include "stdafx.h"
#include "Soft3DGpuInfo.h"
#include <dxgi1_4.h>

#pragma comment(lib, "dxgi.lib")

static UINT64 S3GpuQueryVramBytesFromRegistry(UINT vendorId, UINT deviceId, const wchar_t* descName)
{
	HKEY hClass = NULL;
	if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
		0, KEY_READ, &hClass) != ERROR_SUCCESS)
		return 0;

	wchar_t venTok[16];
	wchar_t devTok[16];
	_snwprintf_s(venTok, _TRUNCATE, L"VEN_%04X", vendorId & 0xffffu);
	_snwprintf_s(devTok, _TRUNCATE, L"DEV_%04X", deviceId & 0xffffu);

	UINT64 best = 0;
	for (DWORD i = 0; ; ++i) {
		wchar_t subName[64];
		DWORD subLen = (DWORD)_countof(subName);
		if (::RegEnumKeyExW(hClass, i, subName, &subLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
			break;
		BOOL digits = (subLen > 0) ? TRUE : FALSE;
		for (DWORD c = 0; c < subLen; ++c) {
			if (subName[c] < L'0' || subName[c] > L'9') {
				digits = FALSE;
				break;
			}
		}
		if (!digits)
			continue;

		HKEY hSub = NULL;
		if (::RegOpenKeyExW(hClass, subName, 0, KEY_READ, &hSub) != ERROR_SUCCESS)
			continue;

		BOOL match = FALSE;
		wchar_t matching[256];
		DWORD mcb = sizeof(matching);
		DWORD type = 0;
		if (::RegQueryValueExW(hSub, L"MatchingDeviceId", NULL, &type, (LPBYTE)matching, &mcb) == ERROR_SUCCESS
			&& (type == REG_SZ || type == REG_EXPAND_SZ)) {
			matching[_countof(matching) - 1] = 0;
			_wcsupr_s(matching);
			if (wcsstr(matching, venTok) && wcsstr(matching, devTok))
				match = TRUE;
		}
		if (!match && descName && descName[0]) {
			wchar_t driverDesc[256];
			DWORD dcb = sizeof(driverDesc);
			if (::RegQueryValueExW(hSub, L"DriverDesc", NULL, &type, (LPBYTE)driverDesc, &dcb) == ERROR_SUCCESS
				&& (type == REG_SZ || type == REG_EXPAND_SZ)) {
				driverDesc[_countof(driverDesc) - 1] = 0;
				if (_wcsicmp(driverDesc, descName) == 0)
					match = TRUE;
			}
		}

		UINT64 qw = 0;
		if (match) {
			DWORD cb = sizeof(qw);
			type = 0;
			if (::RegQueryValueExW(hSub, L"HardwareInformation.qwMemorySize", NULL, &type, (LPBYTE)&qw, &cb) == ERROR_SUCCESS) {
				if ((type == REG_QWORD || type == REG_BINARY) && cb >= sizeof(UINT64) && qw > best)
					best = qw;
			}
		}
		::RegCloseKey(hSub);
	}
	::RegCloseKey(hClass);
	return best;
}

BOOL S3GpuProbePrimary(S3GpuProbe* out)
{
	if (!out) return FALSE;
	ZeroMemory(out, sizeof(*out));

	IDXGIFactory1* factory = NULL;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
		IDXGIAdapter1* best = NULL;
		int bestScore = -1;
		IDXGIAdapter1* adapter = NULL;
		for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
			if (!adapter)
				continue;

			DXGI_ADAPTER_DESC1 desc = {};
			if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
				adapter->Release();
				adapter = NULL;
				continue;
			}
			if (desc.VendorId == 0x1414 && desc.DeviceId == 0x8c) {
				adapter->Release();
				adapter = NULL;
				continue;
			}

			int score = 1;
			IDXGIOutput* output = NULL;
			for (UINT oi = 0; adapter->EnumOutputs(oi, &output) != DXGI_ERROR_NOT_FOUND; ++oi) {
				if (!output)
					continue;
				DXGI_OUTPUT_DESC od = {};
				if (SUCCEEDED(output->GetDesc(&od)) && od.AttachedToDesktop) {
					if (score < 2)
						score = 2;
					if (od.Monitor) {
						MONITORINFO mi = { sizeof(mi) };
						if (GetMonitorInfo(od.Monitor, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY))
							score = 3;
					}
				}
				output->Release();
				output = NULL;
				if (score >= 3)
					break;
			}

			if (score > bestScore) {
				if (best)
					best->Release();
				best = adapter;
				bestScore = score;
				adapter = NULL;
			} else {
				adapter->Release();
				adapter = NULL;
			}
		}

		if (best) {
			DXGI_ADAPTER_DESC1 desc = {};
			if (SUCCEEDED(best->GetDesc1(&desc))) {
				wcsncpy_s(out->name, desc.Description, _TRUNCATE);
				out->vendorId = desc.VendorId;
				out->deviceId = desc.DeviceId;
				out->software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? TRUE : FALSE;
				UINT64 vram = (UINT64)desc.DedicatedVideoMemory;
				if (vram == 0)
					vram = (UINT64)desc.SharedSystemMemory;
				IDXGIAdapter3* a3 = NULL;
				if (SUCCEEDED(best->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)) && a3) {
					DXGI_QUERY_VIDEO_MEMORY_INFO vmi = {};
					if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi))) {
						if (vmi.Budget > vram)
							vram = vmi.Budget;
					}
					a3->Release();
				}
				const UINT64 regVram = S3GpuQueryVramBytesFromRegistry(desc.VendorId, desc.DeviceId, out->name);
				if (regVram > vram)
					vram = regVram;
				out->vramBytes = vram;
			}
			best->Release();
		}
		factory->Release();
	}

	if (!out->name[0]) {
		DISPLAY_DEVICE dd = {};
		dd.cb = sizeof(dd);
		if (EnumDisplayDevices(NULL, 0, &dd, 0)) {
			wcsncpy_s(out->name, dd.DeviceString, _TRUNCATE);
		}
	}
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
