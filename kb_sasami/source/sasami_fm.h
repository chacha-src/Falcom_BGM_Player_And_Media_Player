#pragma once

#include "sasami_file.h"
#include <stdint.h>
#include <mutex>

class SasamiFmPlayer {
public:
	SasamiFmPlayer();
	~SasamiFmPlayer();
	bool Open(const SasamiSong& song, uint32_t sampleRate, const wchar_t* rhythmDir, int fmMode = 2, const wchar_t* sampleBaseDir = NULL);
	void Close();
	/* OPN/OPNA のレジスタ dump（BEEP は無効）。
	   raira=1 は %TEMP%\ogg_kbsasami\fmmon_live.opna（ogg のモニタ）。
	   raira=0 は fmmon_live_r0_32/64.opna（kbsasami_host）。同時再生で live を混ぜない。 */
	void SetFmMonDump(int enable, const wchar_t* sourcePath, int raira = 1);
	int PlayFmMode() const; // 0=BEEP 1=OPN 2=OPNA
	uint32_t Render(int16_t* interleavedStereo, uint32_t frames);
	uint64_t SeekSample(uint64_t sample);
	uint64_t TotalSamples() const { return m_totalSamples; }
	/* 一番長い戻り J の区間。無いときは 0。1 周 = End - Start。 */
	uint64_t LoopStartSample() const { return m_loopStartSample; }
	uint64_t LoopEndSample() const { return m_loopEndSample; }
	uint64_t CurSample() const { return m_curSample; }
	uint32_t SampleRate() const { return m_hostRate; }
	const char* TitleSjis() const { return m_title; }

private:
	struct Impl;
	uint32_t RenderUnlocked(int16_t* interleavedStereo, uint32_t frames);
	void StartDumpThread();
	void StopDumpThread();
	void DumpTimerLoop();
	static unsigned long __stdcall DumpThreadProc(void* self);
	Impl* m;
	uint32_t m_hostRate;
	uint64_t m_totalSamples;
	uint64_t m_curSample;
	uint64_t m_loopStartSample;
	uint64_t m_loopEndSample;
	char m_title[65];
	std::mutex m_lock;
	void* m_dumpStop;
	void* m_dumpTimer;
	void* m_dumpThread;
};
