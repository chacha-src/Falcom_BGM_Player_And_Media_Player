#pragma once
#include <stdint.h>

// ============================================================================
// KpiHost32 曲ファイル用 VST MIDI（スロット 0 / 1）
// ----------------------------------------------------------------------------
// 本体がプレイリストの .mid を x64 VSTi で鳴らすときの入口。
// スロットが 2 つあるのはクロスフェード用。パイプの sessionId がスロット番号。
// 実体は VstMidiEngine（本体と同じソースをホスト側でもリンクしている）。
// ============================================================================

// SMF を開き VSTi を載せる。vstDllPath=GS 側、extraScanPath=XG 側（空なら savedata を使う）。
// 戻り: KPIHOST32_STATUS_*
uint32_t VstHost32_Open(int slot, const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath);
// PCM を読む。eof には KPIHOST32_EOF_* を OR する（短い読み／MIDI 未消化／SysEx・CC）。
uint32_t VstHost32_Render(int slot, uint32_t bytesWanted, uint8_t* dest, uint32_t destCap, uint32_t& gotBytes, uint32_t& eof);
uint32_t VstHost32_Seek(int slot, uint64_t posSample); // サンプル絶対位置
uint32_t VstHost32_Close(int slot);                   // 片方だけ閉じる
uint32_t VstHost32_CloseAll();                        // クロスフェード両スロット
int VstHost32_SongActive();                           // どちらか開いていれば 1（アイドルタイムアウト抑制）
int VstHost32_Rate(int slot);
int VstHost32_Channels(int slot);
int VstHost32_Bits(int slot);
uint64_t VstHost32_Length(int slot);
int VstHost32_Latency(int slot); // プラグイン遅延サンプル。マッパーなら 0
