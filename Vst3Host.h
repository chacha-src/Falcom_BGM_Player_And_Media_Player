#pragma once

#include <Windows.h>

struct Vst3Inst;

Vst3Inst* Vst3Open(const wchar_t* vst3PathOrDll);
void Vst3Close(Vst3Inst* inst);
void Vst3MidiShort(Vst3Inst* inst, DWORD msg, int sampleOffset);
void Vst3Process(Vst3Inst* inst, float* outL, float* outR, int frames);
int Vst3IsOk(Vst3Inst* inst);
const wchar_t* Vst3LastError();
int Vst3MidiChannels(Vst3Inst* inst);
int Vst3ProgramCount(Vst3Inst* inst);
int Vst3ProgramName(Vst3Inst* inst, int index, wchar_t* out, int outChars);
int Vst3SetProgram(Vst3Inst* inst, int index);
int Vst3SetChannelProgram(Vst3Inst* inst, int midiCh, int index);
