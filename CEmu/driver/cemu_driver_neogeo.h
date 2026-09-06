#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_neogeo.h"

class CDriverNeo : public CDriver {
public:
	CDriverNeo();
	~CDriverNeo() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

private:
	CHardNeo* hw_;
	int hostRate_;
	int cpuHz_;
	int ymHz_;
	uint8_t songCmd_;
	uint64_t ymResidual_;
	int64_t cpuAcc_;
	int injected_;
	uint64_t injectAt_;
	int reinjected_;

	void RunUntil(uint64_t endCycle);
	void TickYm(uint64_t cpuCycles);
	void DeliverIrqs();
	void InjectSongCommand();
	void WaitQueueIdle(int maxFrames60);
	void SendZ80Command(uint8_t cmd, int maxFrames60);
};

CDriver* CDriverNeoCreate();
