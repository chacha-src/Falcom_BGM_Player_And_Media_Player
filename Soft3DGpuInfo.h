#pragma once
#include <Windows.h>

/* Primary DXGI adapter probe (shared by about-string and Soft3D gfx defaults). */
struct S3GpuProbe {
	wchar_t name[128];
	UINT vendorId;
	UINT deviceId;
	UINT64 vramBytes;
	BOOL software;
};

/* Fills probe; returns TRUE if a usable adapter description was found. */
BOOL S3GpuProbePrimary(S3GpuProbe* out);

/* Formats probe into "Name (N GB|MB)" for UI (caller provides CString). */
void S3GpuFormatInfoString(const S3GpuProbe& p, CString& out);
CString S3GpuBuildInfoString();
