#pragma once
#include <string>

/*
  分割ボリュームは先頭 64 バイトが OXZVOL1。
  0 印 8、8 version u32=1、12 index、16 桁数、20 flags（bit0 は自己解凍）、
  24 payload u64、32 全体 u64、40 分割サイズ u64、48 個数 u32、52 予約 u32、56 予約 u64。
  続きは .o01。100 を超える番号になるときは最初から .o001。
  自己解凍の末尾 16 バイトは OXZSFX1 と、ヘッダ位置の u64。
  分割するとき、各ファイルの大きさは指定サイズに揃える。
  先頭が自己解凍なら、exe と 64 バイトの見出しと 16 バイトの末尾を引いた分だけ中身を入れる。
  それ以外の巻は、64 バイトの見出しを引いた分だけ入れる。oxz 以外は分割しない。
*/

void OxzOptApply();
int OxzOptAsk();
int OxzOptSfx();
int OxzOptThreads();
unsigned long long OxzOptSplit();
const wchar_t* OxzOptSplitText();
void OxzOptSetAsk(int on);
void OxzOptSetSfx(int on);
void OxzOptSetThreads(int n);
void OxzOptSetSplitText(const wchar_t* text);
int OxzSplitPresetCount();
const wchar_t* OxzSplitPreset(int index);
unsigned long long OxzParseBytes(const wchar_t* text);

int OxzIsOxzFamily(const wchar_t* path);
int OxzMaterializeOxz(const wchar_t* path, std::wstring& out, int* temp);
void OxzReleaseOxz(const std::wstring& path, int temp);
int OxzSealOutput(const std::wstring& archive, unsigned long long* bytes, std::wstring& err);
