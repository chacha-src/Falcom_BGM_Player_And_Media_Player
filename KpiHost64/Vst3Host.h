#pragma once

#include <Windows.h>

struct Vst3Inst;

Vst3Inst* Vst3Open(const wchar_t* vst3PathOrDll);
void Vst3Close(Vst3Inst* inst);
void Vst3MidiShort(Vst3Inst* inst, DWORD msg, int sampleOffset);
void Vst3Process(Vst3Inst* inst, float* outL, float* outR, int frames);
// Attaches the plug-in's own editor to parentHwnd; reports its size so the
// caller can fit the host window around it.
int Vst3EditorOpen(Vst3Inst* inst, void* parentHwnd, int* outW, int* outH);
void Vst3EditorClose(Vst3Inst* inst);
int Vst3IsOk(Vst3Inst* inst);
int Vst3GetLatencySamples(Vst3Inst* inst);
const wchar_t* Vst3LastError();
int Vst3MidiChannels(Vst3Inst* inst);
void Vst3SetMidiForceCh0(Vst3Inst* inst, int enable);
int Vst3ProgramCount(Vst3Inst* inst);
int Vst3ProgramName(Vst3Inst* inst, int index, wchar_t* out, int outChars);
int Vst3SetProgram(Vst3Inst* inst, int index);
int Vst3SetChannelProgram(Vst3Inst* inst, int midiCh, int index);
