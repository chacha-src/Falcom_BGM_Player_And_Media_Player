#pragma once

// Load an EXE-embedded PNG (RCDATA) via WIC into BGRA8. Scales to dstW x dstH.
// 画像は tacticsbgm\resource にあればそちらを使う。
BOOL Soft3DTexLoadPngRes(int id, DWORD* dst, int dstW, int dstH);
BOOL Soft3DTexLoadNamed(const char* rel, DWORD* dst, int dstW, int dstH);
struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
BOOL Soft3DCreateTex2DMips(ID3D11Device* dev, const DWORD* src, int w, int h, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv);

// 迷路・レース・タクティクスの画像。呼び出し側が free する。
BOOL BonusImageRead(int id, BYTE** data, DWORD* len);
// メモリマップで読み取り専用参照を返す（free 不要）。
BOOL BonusImageMap(int id, const BYTE** data, DWORD* len);
// tac_tex.bin 内の名前（tactics/tac_mat.png, mat/00.jpg）。参照はマップ寿命。
BOOL BonusTexNamed(const char* rel, const BYTE** data, DWORD* len);
// ボーナス bin のメモリマップを閉じる。更新展開が同じファイルを上書きできるようにする。
void Soft3DReleaseBonusBins();

// 迷路/レース CSO: リソース → exe隣の shaders\cso → hlsl を D3DCompile。outBlob は ID3DBlob**。
HRESULT Soft3DLoadCso(const wchar_t* set, const char* entry, const char* profile, int rid, const wchar_t* hlslLeaf, void** outBlob);
