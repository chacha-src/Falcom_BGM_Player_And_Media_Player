#pragma once

#include "sasami_file.h"
#include <stdint.h>

struct SasamiMisaoEv {
	uint32_t tick;
	int port;
	uint8_t len;
	uint8_t bytes[8];
};

bool SasamiMisaoActive(const SasamiSong& song);
bool SasamiMisaoTrackValid(const SasamiSong& song, uint16_t ptr);

// PCMTBL (MISAO FM) -> MIDI events for SMF merge. port is usually 1.
int SasamiMisaoBuildEvents(const SasamiSong& song, SasamiMisaoEv* out, int maxEv, unsigned* totalTicks);

class SasamiMisaoSynth {
public:
	SasamiMisaoSynth();
	~SasamiMisaoSynth();
	bool Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* programsTxtDir, unsigned* sharedTempoT = NULL);
	void Close();
	void Reset();
	void SetTempoT(unsigned t);
	void TickOnce();
	void SynthesizeMix(double* stereoInterleaved, uint32_t frames);
	uint32_t SampleRate() const;
	int Ended() const;
	/* FMモニタ用: gate/note。maxCh は SASAMI_MISAO_MAX_CH まで */
	void FillMonitor(uint8_t* onOut, uint8_t* noteOut, int maxCh, int* outCount) const;

private:
	struct Impl;
	Impl* m;
};
