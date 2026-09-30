#pragma once

#include <Windows.h>
#include "third_party/vst2/aeffect.h"

#ifdef __cplusplus
extern "C" {
#endif

int Sf2PathIsSoundFont(const wchar_t* path);
int Sf2ProbeHeader(const wchar_t* path);
AEffect* Sf2Vst2Open(const wchar_t* path, audioMasterCallback host);
int Sf2Vst2IsInstance(const AEffect* e);

#ifdef __cplusplus
}
#endif
