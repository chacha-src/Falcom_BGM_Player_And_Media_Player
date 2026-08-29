#include "sasami_misao.h"
#include "sasami_misao_internal.h"

#include <string.h>

namespace {

static const int kMisaoSmfPort = 1;

} // namespace

bool SasamiMisaoTrackValid(const SasamiSong& song, uint16_t ptr)
{
	if (ptr == 0 || ptr == 0x10F0) return false;
	uint32_t off = (ptr >= 0x1000) ? (uint32_t)(ptr - 0x1000) : ptr;
	if (!SasamiOffOk(song, off, 3)) return false;
	if (song.kind != SASAMI_KIND_FPY && off < 0x300) return false;
	const uint8_t cmd = SasamiGet(song, off);
	return MisaoCmdValid(cmd);
}

bool SasamiMisaoActive(const SasamiSong& song)
{
	if (!song.misaoEnabled) return false;
	for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) {
		if (!song.misaoTracks[i].unused && song.misaoTracks[i].fileOff)
			return true;
	}
	return false;
}

int SasamiMisaoBuildEvents(const SasamiSong& song, SasamiMisaoEv* out, int maxEv, unsigned* totalTicks)
{
	if (!SasamiMisaoActive(song)) {
		if (totalTicks) *totalTicks = 0;
		return 0;
	}
	MisaoChState ch[SASAMI_MISAO_MAX_CH];
	MisaoInitChState(ch, song);
	const int chLimit = MisaoEffectiveChCount(song);

	int outCount = 0;
	unsigned tick = 0;
	unsigned T = kMisaoDefaultT;
	const int measureLen = 1;
	while (tick < kMisaoMaxTicks) {
		int any = 0;
		for (int i = 0; i < chLimit; i++) {
			if (!ch[i].alive) continue;
			any = 1;
			if (ch[i].wait >= 2) {
				ch[i].wait--;
				continue;
			}
			int guard = 0;
			while (ch[i].alive && ch[i].wait < 2 && guard++ < 4096) {
				uint32_t addr = ch[i].pc;
				if (!SasamiOffOk(song, addr, 1) || addr == 0xF0) {
					ch[i].alive = 0;
					break;
				}
				const int cmd = SasamiGet(song, addr);
				const uint8_t b1 = SasamiGet(song, addr + 1);
				const uint8_t b2 = SasamiGet(song, addr + 2);
				const uint16_t w1 = SasamiGet16(song, addr + 1);
				auto emit = [&](const uint8_t* d, int n) {
					if (outCount >= maxEv) return;
					SasamiMisaoEv& e = out[outCount++];
					e.tick = tick;
					e.port = kMisaoSmfPort;
					e.len = (uint8_t)n;
					memcpy(e.bytes, d, (size_t)n);
				};
				auto pushShort = [&](uint8_t st, uint8_t a, uint8_t b) {
					uint8_t d[3] = { st, a, b };
					const int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 2 : 3;
					emit(d, n);
				};
				switch (cmd) {
				case 0:
					pushShort((uint8_t)(0x80 | i), ch[i].lastNote, 0);
					ch[i].lastNote = (uint8_t)MisaoNoteKey(b1);
					pushShort((uint8_t)(0x90 | i), ch[i].lastNote, 100);
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 1:
					pushShort((uint8_t)(0x80 | i), ch[i].lastNote, 0);
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 2:
					pushShort((uint8_t)(0xC0 | i), b1, 0);
					ch[i].pc = addr + 3;
					break;
				case 3: {
					uint32_t dest = w1;
					if (dest >= 0x1000) dest -= 0x1000;
					if (dest == 0xF0) ch[i].alive = 0;
					else if (measureLen && dest < addr) {
						ch[i].backJumps++;
						if (ch[i].backJumps >= 2) ch[i].alive = 0;
						else ch[i].pc = dest;
					} else
						ch[i].pc = dest;
					break;
				}
				case 9:
					if (w1) {
						T = w1;
						const uint32_t mpqn = (uint32_t)((500ull * w1) / 13ull);
						uint8_t d[6] = { 0xFF, 0x51, 0x03,
							(uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
						emit(d, 6);
					}
					ch[i].pc = addr + 3;
					break;
				case 10:
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 11:
					pushShort((uint8_t)(0xB0 | i), 7, b1);
					ch[i].pc = addr + 3;
					break;
				case 12: {
					ch[i].pitchM = w1;
					const int pb = MisaoCombinedBend(ch[i]);
					pushShort((uint8_t)(0xE0 | i), (uint8_t)(pb & 0x7F), (uint8_t)((pb >> 7) & 0x7F));
					ch[i].pc = addr + 3;
					break;
				}
				case 13:
					ch[i].loopCnt = b1;
					ch[i].pc = addr + 3;
					break;
				case 14: {
					uint8_t c = ch[i].loopCnt;
					if (c) c--;
					if (c == 0) ch[i].pc = addr + 3;
					else {
						ch[i].loopCnt = c;
						uint32_t dest = w1;
						if (dest >= 0x1000) dest -= 0x1000;
						ch[i].pc = dest;
					}
					break;
				}
				case 18: {
					ch[i].detune = w1;
					const int pb = MisaoCombinedBend(ch[i]);
					pushShort((uint8_t)(0xE0 | i), (uint8_t)(pb & 0x7F), (uint8_t)((pb >> 7) & 0x7F));
					ch[i].pc = addr + 3;
					break;
				}
				case 24:
					ch[i].lastNote = (uint8_t)MisaoNoteKey(b1);
					pushShort((uint8_t)(0x90 | i), ch[i].lastNote, 100);
					ch[i].wait = b2;
					ch[i].pc = addr + 3;
					break;
				case 25:
					pushShort((uint8_t)(0xB0 | i), 10, (uint8_t)MisaoPanCc(b1));
					ch[i].pc = addr + 3;
					break;
				default:
					ch[i].pc = addr + 3;
					break;
				}
				if (ch[i].wait >= 2) break;
				if (cmd == 0 || cmd == 1 || cmd == 10 || cmd == 24) break;
			}
		}
		if (!any) break;
		(void)T;
		tick++;
	}
	if (totalTicks) *totalTicks = tick;
	return outCount;
}
