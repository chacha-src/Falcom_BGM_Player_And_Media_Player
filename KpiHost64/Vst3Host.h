#pragma once

#include <Windows.h>

// ============================================================================
// VST3 インストゥルメントの薄いホスト（KpiHost64 用の宣言。実体は ../Vst3Host.cpp）
// MIDI と Process はオーディオ側。EditorOpen は UI スレッド必須。
// ============================================================================

struct Vst3Inst;

Vst3Inst* Vst3Open(const wchar_t* vst3PathOrDll);
void Vst3Close(Vst3Inst* inst);
void Vst3MidiShort(Vst3Inst* inst, DWORD msg, int sampleOffset);
void Vst3MidiSysex(Vst3Inst* inst, const unsigned char* data, int bytes, int sampleOffset);
void Vst3Process(Vst3Inst* inst, float* outL, float* outR, int frames);
// プラグイン自身のエディタを parentHwnd に載せる。サイズを返すので窓を合わせる。
int Vst3EditorOpen(Vst3Inst* inst, void* parentHwnd, int* outW, int* outH);
void Vst3EditorClose(Vst3Inst* inst);
int Vst3IsOk(Vst3Inst* inst);
int Vst3GetLatencySamples(Vst3Inst* inst);
const wchar_t* Vst3LastError();
int Vst3MidiChannels(Vst3Inst* inst);
int Vst3IsInstrument(Vst3Inst* inst);
int Vst3ProgramCount(Vst3Inst* inst);
int Vst3ProgramName(Vst3Inst* inst, int index, wchar_t* out, int outChars);
int Vst3SetProgram(Vst3Inst* inst, int index);
int Vst3SetChannelProgram(Vst3Inst* inst, int midiCh, int index);
