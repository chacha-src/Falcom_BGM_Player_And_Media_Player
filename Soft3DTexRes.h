#pragma once

// Load an EXE-embedded PNG (RCDATA) via WIC into BGRA8. Scales to dstW x dstH.
// 画像は tacticsbgm\resource にあればそちらを使う。
BOOL Soft3DTexLoadPngRes(int id, DWORD* dst, int dstW, int dstH);

// 迷路・レース・タクティクスの画像。呼び出し側が free する。
BOOL BonusImageRead(int id, BYTE** data, DWORD* len);
// メモリマップで読み取り専用参照を返す（free 不要）。
BOOL BonusImageMap(int id, const BYTE** data, DWORD* len);
// tac_tex.bin 内の名前（tactics/tac_mat.png, mat/00.jpg）。参照はマップ寿命。
BOOL BonusTexNamed(const char* rel, const BYTE** data, DWORD* len);

// 迷路/レース CSO: リソース → exe隣の shaders\cso → hlsl を D3DCompile。outBlob は ID3DBlob**。
HRESULT Soft3DLoadCso(const wchar_t* set, const char* entry, const char* profile, int rid, const wchar_t* hlslLeaf, void** outBlob);
