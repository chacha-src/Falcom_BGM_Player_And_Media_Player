#pragma once
#include <stdint.h>
#include <vector>

// ============================================================================
// KpiHost64 曲ファイル用 VST MIDI（スロット 0 / 1）
// ----------------------------------------------------------------------------
// 本体がプレイリストの .mid を x64 VSTi で鳴らすときの入口。
// スロットが 2 つあるのはクロスフェード用。パイプの sessionId がスロット番号。
// 実体は VstMidiEngine（本体と同じソースをホスト側でもリンクしている）。
// ============================================================================

// SMF を開き VSTi を載せる。vstDllPath=GS 側、extraScanPath=XG 側（空なら savedata を使う）。
// 戻り: KPIHOST64_STATUS_*
uint32_t VstHost64_Open(int slot, const wchar_t* midPath, const wchar_t* vstDllPath, const wchar_t* extraScanPath);
// PCM を読む。eof には KPIHOST64_EOF_* を OR する（短い読み／MIDI 未消化／SysEx・CC）。
uint32_t VstHost64_Render(int slot, uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof);
uint32_t VstHost64_Seek(int slot, uint64_t posSample); // サンプル絶対位置
uint32_t VstHost64_Close(int slot);                   // 片方だけ閉じる
uint32_t VstHost64_CloseAll();                        // クロスフェード両スロット
int VstHost64_SongActive();                           // どちらか開いていれば 1（アイドルタイムアウト抑制）
int VstHost64_Rate(int slot);
int VstHost64_Channels(int slot);
int VstHost64_Bits(int slot);
uint64_t VstHost64_Length(int slot);
int VstHost64_Latency(int slot); // プラグイン遅延サンプル。マッパーなら 0
