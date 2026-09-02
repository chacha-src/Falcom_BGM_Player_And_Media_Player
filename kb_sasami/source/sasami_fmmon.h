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
	/* v6: PMD/FMP。kbsasami は version=5 のまま（この先は 0） */
	uint8_t keyOnEx[3];       /* FM3EX1-3 */
	uint8_t keyOnExHitCnt[3];
	uint8_t keyMidi[6];       /* 0xFF=fnum から。FMP キー専用はここに MIDI */
	uint8_t exMidi[3];
	uint8_t ssgMidi[3];
	uint8_t dumpFlags;        /* bit0=keys-only bit1=PPZ bit2=FM3EX bit3=MSX bit4=FMP */
	uint8_t pad6[3];          /* [0]=MSX deviceMask: PSG=1 OPLL=2 SCC=4 */
};

/* リングヘッダのみ（slot 全体 ~320KB をスタックに置かないこと） */
struct SasamiFmMonRingHdr {
	char magic[4];       // "OPNR"
	uint32_t version;    // 1
	uint32_t gen;        // 累積 Flush 回数。最新は slot[(gen-1)%RING]
	uint32_t reserved;
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
enum { SASAMI_FMMON_VERSION_V6 = 6 };
enum { SASAMI_FMMON_RING_VERSION = 1 };
enum {
	SASAMI_FMMON_FLAG_KEYSONLY = 1,
	SASAMI_FMMON_FLAG_PPZ = 2,
	SASAMI_FMMON_FLAG_FM3EX = 4,
	SASAMI_FMMON_FLAG_MSX = 8,
	SASAMI_FMMON_FLAG_FMP = 16
};
enum {
	SASAMI_FMMON_DEV_PSG = 1,
	SASAMI_FMMON_DEV_OPLL = 2,
	SASAMI_FMMON_DEV_SCC = 4,
	SASAMI_FMMON_DEV_HES = 8 /* HuC6280: SSG1-3 + pcm=SSG4-6 */
};

#ifdef __cplusplus
inline bool SasamiFmMonMagicOk(const SasamiFmMonDump& d)
{
	return d.magic[0] == 'O' && d.magic[1] == 'P' && d.magic[2] == 'N' && d.magic[3] == 'A'
		&& d.version >= 2 && d.version <= 6;
}
inline bool SasamiFmMonRingMagicOk(const SasamiFmMonRingHdr& r)
{
	return r.magic[0] == 'O' && r.magic[1] == 'P' && r.magic[2] == 'N' && r.magic[3] == 'R'
		&& r.version == SASAMI_FMMON_RING_VERSION;
}
inline bool SasamiFmMonRingMagicOk(const SasamiFmMonRing& r)
{
	SasamiFmMonRingHdr h;
	h.magic[0] = r.magic[0]; h.magic[1] = r.magic[1]; h.magic[2] = r.magic[2]; h.magic[3] = r.magic[3];
	h.version = r.version;
	h.gen = r.gen;
	h.reserved = r.reserved;
	return SasamiFmMonRingMagicOk(h);
}
#endif
