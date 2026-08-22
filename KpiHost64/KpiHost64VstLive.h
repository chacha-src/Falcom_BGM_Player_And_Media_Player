#pragma once

#include <stdint.h>
#include <vector>

uint32_t VstHost64_LiveLoad(uint32_t part1to32, const wchar_t* path, uint32_t isVst3);
uint32_t VstHost64_LiveUnload(uint32_t part1to32);
uint32_t VstHost64_LiveUnloadAll();
uint32_t VstHost64_LiveMidi(uint32_t port, uint32_t msg);
uint32_t VstHost64_LiveSysex(uint32_t port, const uint8_t* data, uint32_t len);
uint32_t VstHost64_LiveRender(uint32_t frames, std::vector<uint8_t>& reply);
uint32_t VstHost64_LiveAudioStart();
uint32_t VstHost64_LiveAudioStop();
uint32_t VstHost64_LiveEditorOpen(uint32_t part1to32);
uint32_t VstHost64_LiveEditorClose(uint32_t part1to32);
uint32_t VstHost64_LiveSetSendChannel(uint32_t part1to32, int32_t sendCh);
uint32_t VstHost64_LivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
	std::vector<uint8_t>& reply);
uint32_t VstHost64_LiveSetProgram(uint32_t part1to32, uint32_t index);

// Non-zero while live parts are loaded or the render thread is running. The
// idle timeout must not tear the host down underneath a loaded instrument.
int VstHost64_LiveActive();
