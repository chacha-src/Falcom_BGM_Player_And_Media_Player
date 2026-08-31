#pragma once

#include <Windows.h>

// ============================================================================
// VST3 インストゥルメントの薄いホスト
// ----------------------------------------------------------------------------
// 実体は Vst3Host.cpp。KpiHost64 は Vst3Host_k64.cpp から同じソースを x64 で
// リンクする（コピーを分けると x64 側だけ古い実装が残る）。
// MIDI と Process はオーディオ側。EditorOpen は UI スレッド必須。
// ============================================================================

struct Vst3Inst; // 中身は Vst3Host.cpp。呼び出し側からは opaque。

Vst3Inst* Vst3Open(const wchar_t* vst3PathOrDll); // バンドル or 中の DLL。失敗時 NULL
void Vst3Close(Vst3Inst* inst);
void Vst3MidiShort(Vst3Inst* inst, DWORD msg, int sampleOffset); // sampleOffset=ブロック先頭からのフレーム
void Vst3MidiSysex(Vst3Inst* inst, const unsigned char* data, int bytes, int sampleOffset);
void Vst3Process(Vst3Inst* inst, float* outL, float* outR, int frames);
// プラグイン自身のエディタを parentHwnd に載せる。サイズを返すので窓を合わせる。
int Vst3EditorOpen(Vst3Inst* inst, void* parentHwnd, int* outW, int* outH);
void Vst3EditorClose(Vst3Inst* inst);
/* soft: skip IPlugView::removed (SampleTank hang on close). */
void Vst3EditorCloseEx(Vst3Inst* inst, int soft);
int Vst3IsOk(Vst3Inst* inst);
int Vst3GetLatencySamples(Vst3Inst* inst);
const wchar_t* Vst3LastError();
int Vst3MidiChannels(Vst3Inst* inst);
int Vst3IsInstrument(Vst3Inst* inst);
int Vst3ProgramCount(Vst3Inst* inst);
int Vst3ProgramName(Vst3Inst* inst, int index, wchar_t* out, int outChars);
int Vst3SetProgram(Vst3Inst* inst, int index);
int Vst3SetChannelProgram(Vst3Inst* inst, int midiCh, int index); // midiCh=0..15
int Vst3ParamCount(Vst3Inst* inst);
int Vst3ParamName(Vst3Inst* inst, int index, wchar_t* out, int outChars);
int Vst3ParamDisplay(Vst3Inst* inst, int index, wchar_t* out, int outChars);
float Vst3GetParam(Vst3Inst* inst, int index);
int Vst3SetParam(Vst3Inst* inst, int index, float value01);
/* Component/controller state blobs. Get*: malloc'd buffer; caller free(). */
int Vst3GetComponentState(Vst3Inst* inst, unsigned char** outBytes, int* outLen);
int Vst3GetControllerState(Vst3Inst* inst, unsigned char** outBytes, int* outLen);
int Vst3SetComponentState(Vst3Inst* inst, const unsigned char* bytes, int len);
int Vst3SetControllerState(Vst3Inst* inst, const unsigned char* bytes, int len);
/* Restore: component setState → controller setComponentState → controller setState. */
int Vst3ApplyStates(Vst3Inst* inst,
	const unsigned char* comp, int compLen,
	const unsigned char* ctrl, int ctrlLen);
