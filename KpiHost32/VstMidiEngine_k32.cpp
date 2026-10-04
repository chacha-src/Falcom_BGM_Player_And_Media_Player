// KpiHost32（x86 / Win32）用。本体 ogg.exe（x64）と同じ VstMidiEngine.cpp を x86 でコンパイルする。
// kpihost_stdafx.h の save / LangPick14 が本体の MFC 依存を肩代わりする。
#include "../VstMidiEngine.cpp"
