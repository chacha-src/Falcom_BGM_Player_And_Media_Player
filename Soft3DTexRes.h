#pragma once

// Load an EXE-embedded PNG (RCDATA) via WIC into BGRA8. Scales to dstW x dstH.
BOOL Soft3DTexLoadPngRes(int id, DWORD* dst, int dstW, int dstH);

// 迷路/レース CSO: リソース → exe隣の shaders\cso → hlsl を D3DCompile。outBlob は ID3DBlob**。
HRESULT Soft3DLoadCso(const wchar_t* set, const char* entry, const char* profile, int rid, const wchar_t* hlslLeaf, void** outBlob);
