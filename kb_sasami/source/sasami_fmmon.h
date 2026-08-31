#pragma once
// OPNA FM monitor dump — kbsasami (raira=1) writes, 本体 FMモニタ reads.
#include <stdint.h>

enum { SASAMI_FMMON_PCM_MAX = 16 };
/* ~10ms/tick × 遅延数秒分。48 では高テンポ＋DS遅延で履歴が消える */
enum { SASAMI_FMMON_RING = 256 };

#pragma pack(push, 1)
struct SasamiFmMonDump {
	char magic[4];          // "OPNA"
	uint32_t version;       // 5
	uint32_t seq;           // increments on register writes
	uint32_t sampleRate;
	uint64_t curSample;
	uint8_t regs[0x200];    // bank0 [0x000..0x0FF], bank1 [0x100..0x1FF]
	uint8_t keyOnFm[6];     // FM ch1-6 (0/1)
	uint8_t ssgOn[3];       // SSG A/B/C gate (1=on)
	uint8_t rhythmKey;      // sounding rhythm bits (hold/flash)
	uint8_t fm10;           // 1=OPNA 10ch
	uint8_t pcmCount;       // MISAO PCM channels in use (0=none)
	uint8_t rhythmPulse;    // key-on ビット（Flush 区間、消費型）
	uint8_t rhythmHitCnt[6];
	uint8_t keyOnHitCnt[6]; /* FM key-on 書き込み累積（パネル SLOT 取りこぼし防止） */
	uint8_t ssgHitCnt[3];   /* SSG gate-on 累積 */
	uint8_t padHit;
	char titleSjis[64];
	wchar_t sourcePath[260];
	uint8_t pcmOn[SASAMI_FMMON_PCM_MAX];
	uint8_t pcmNote[SASAMI_FMMON_PCM_MAX]; // MIDI note 0..127
	uint8_t regWriteBits[64]; /* v5: regs[i] が直前 Flush 区間に書かれたら bit i */
};

/* リング: live 上書きで消えるフラッシュを UI が全部読めるようにする */
struct SasamiFmMonRing {
	char magic[4];       // "OPNR"
	uint32_t version;    // 1
	uint32_t gen;        // 累積 Flush 回数。最新は slot[(gen-1)%RING]
	uint32_t reserved;
	SasamiFmMonDump slot[SASAMI_FMMON_RING];
};
#pragma pack(pop)

enum { SASAMI_FMMON_VERSION = 5 };
enum { SASAMI_FMMON_RING_VERSION = 1 };

#ifdef __cplusplus
inline bool SasamiFmMonMagicOk(const SasamiFmMonDump& d)
{
	return d.magic[0] == 'O' && d.magic[1] == 'P' && d.magic[2] == 'N' && d.magic[3] == 'A'
		&& d.version >= 2 && d.version <= 5;
}
inline bool SasamiFmMonRingMagicOk(const SasamiFmMonRing& r)
{
	return r.magic[0] == 'O' && r.magic[1] == 'P' && r.magic[2] == 'N' && r.magic[3] == 'R'
		&& r.version == SASAMI_FMMON_RING_VERSION;
}
#endif
