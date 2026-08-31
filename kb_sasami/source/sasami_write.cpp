#include "sasami_write.h"
#include <windows.h>
#include <string.h>
#include <stdlib.h>

void SasamiWriteMidiClear(SasamiWriteMidi* w)
{
	if (!w) return;
	for (int i = 0; i < 32; i++) {
		if (w->vstComp[i]) free(w->vstComp[i]);
		if (w->vstCtrl[i]) free(w->vstCtrl[i]);
	}
	memset(w, 0, sizeof(*w));
	w->trackCount = 32;
	for (int i = 0; i < 32; i++) {
		w->vstProg[i] = -1;
		w->vstForceCh[i] = -1;
	}
}

void SasamiWriteFmClear(SasamiWriteFm* w)
{
	if (!w) return;
	memset(w, 0, sizeof(*w));
}

int SasamiStreamPut(SasamiTrackStream* s, const uint8_t* p, uint32_t n)
{
	if (!s || !p || n == 0) return 0;
	if (s->size + n > SASAMI_WRITE_STREAM) return 0;
	memcpy(s->bytes + s->size, p, n);
	s->size += n;
	s->used = 1;
	return 1;
}

int SasamiStreamPut3(SasamiTrackStream* s, uint8_t a, uint8_t b, uint8_t c)
{
	uint8_t t[3] = { a, b, c };
	return SasamiStreamPut(s, t, 3);
}

static void Put16(uint8_t* p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void Put24(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
}

static void CopySjisField(uint8_t* dest, const char* src, int cap)
{
	memset(dest, 0, (size_t)cap);
	if (!src || !src[0]) return;
	size_t n = strlen(src);
	if (n > (size_t)(cap - 1)) n = (size_t)(cap - 1);
	memcpy(dest, src, n);
}

static uint32_t FinalizeMidiTracks(const SasamiWriteMidi* w, uint8_t* out, uint32_t dataBaseAbs, uint32_t headerBytes, int v2)
{
	/* dataBaseAbs = absolute address (0x1000+fileOff). headerBytes = seed size before track data. */
	uint32_t cursorAbs = dataBaseAbs;
	uint32_t fileOff = headerBytes;
	int ntr = w->trackCount;
	if (ntr < 1) ntr = 32;
	if (ntr > SASAMI_WRITE_TR) ntr = SASAMI_WRITE_TR;

	uint32_t firstLoopAbs = 0; /* song-first cmd23 — for J with no local Q/|: */

	for (int ch = 0; ch < ntr; ch++) {
		const SasamiTrackStream* s = &w->tr[ch];
		uint32_t absAddr;
		uint8_t part = (uint8_t)(s->part & 0x0F);
		if (!s->used || s->size == 0) {
			absAddr = 0x10F0;
			if (v2) {
				Put24(out + 0x200 + (uint32_t)ch * 4, absAddr);
				out[0x200 + (uint32_t)ch * 4 + 3] = part;
			} else {
				int packed = (ch > 31) ? (0x100 + (ch - 32) * 3) : (ch * 3);
				Put16(out + packed, (uint16_t)absAddr);
				out[packed + 2] = part;
			}
			continue;
		}
		absAddr = cursorAbs;
		if (fileOff + s->size + 3 > SASAMI_WRITE_MAX) return 0;
		memcpy(out + fileOff, s->bytes, s->size);
		/* Patch track-relative MPY loop/jump (cmd24 / cmd10 with 0xF000|rel) to abs 0x1xxx.
		   0xE000 = jump to song-first |: (cross-track; classic DO-- [5:8]). */
		{
			const uint32_t trackFileOff = fileOff;
			for (uint32_t i = 0; i + 2 < s->size; ) {
				uint8_t* p = out + trackFileOff + i;
				const uint8_t cmd = p[0];
				if (cmd == 13 || cmd == 14 || cmd == 15) {
					i += 4;
					continue;
				}
				if (cmd == 23 && firstLoopAbs == 0)
					firstLoopAbs = (uint32_t)(0x1000 + trackFileOff + i);
				if (cmd == 24) {
					uint16_t rel = (uint16_t)(p[1] | (p[2] << 8));
					uint16_t abs = (uint16_t)(0x1000 + trackFileOff + rel);
					p[1] = (uint8_t)(abs & 0xFF);
					p[2] = (uint8_t)(abs >> 8);
				} else if (cmd == 0x0A) {
					uint16_t w1 = (uint16_t)(p[1] | (p[2] << 8));
					if ((w1 & 0xF000) == 0xF000) {
						uint16_t rel = (uint16_t)(w1 & 0x0FFF);
						uint16_t abs = (uint16_t)(0x1000 + trackFileOff + rel);
						p[1] = (uint8_t)(abs & 0xFF);
						p[2] = (uint8_t)(abs >> 8);
					} else if (w1 == 0xE000) {
						/* resolved in second pass once firstLoopAbs known */
					}
				}
				i += 3;
			}
		}
		fileOff += s->size;
		/* ensure end jump if last cmd is not jump-to-F0 */
		int needEnd = 1;
		if (s->size >= 3 && s->bytes[s->size - 3] == 0x0A) needEnd = 0;
		if (needEnd) {
			if (fileOff + 3 > SASAMI_WRITE_MAX) return 0;
			out[fileOff++] = 0x0A;
			out[fileOff++] = 0xF0;
			out[fileOff++] = 0x10;
			cursorAbs += 3;
		}
		cursorAbs += s->size;
		if (v2) {
			Put24(out + 0x200 + (uint32_t)ch * 4, absAddr);
			out[0x200 + (uint32_t)ch * 4 + 3] = part;
		} else {
			int packed = (ch > 31) ? (0x100 + (ch - 32) * 3) : (ch * 3);
			Put16(out + packed, (uint16_t)absAddr);
			out[packed + 2] = part;
		}
	}
	/* Second pass: 0xE000 soft-J → first |: in song */
	if (firstLoopAbs != 0) {
		const uint32_t dataEnd = fileOff;
		for (uint32_t i = headerBytes; i + 2 < dataEnd; ) {
			uint8_t cmd = out[i];
			if (cmd == 13 || cmd == 14 || cmd == 15) {
				i += 4;
				continue;
			}
			if (cmd == 0x0A) {
				uint16_t w1 = (uint16_t)(out[i + 1] | (out[i + 2] << 8));
				if (w1 == 0xE000) {
					out[i + 1] = (uint8_t)(firstLoopAbs & 0xFF);
					out[i + 2] = (uint8_t)(firstLoopAbs >> 8);
				}
			}
			i += 3;
		}
	}
	return fileOff;
}

uint32_t SasamiBuildMpy(const SasamiWriteMidi* w, uint8_t* out, uint32_t outCap)
{
	if (!w || !out || outCap < 0x400) return 0;
	memset(out, 0, outCap > SASAMI_WRITE_MAX ? SASAMI_WRITE_MAX : outCap);
	/* unused track slots default */
	for (int ch = 0; ch < 32; ch++) {
		Put16(out + ch * 3, 0x10F0);
		out[ch * 3 + 2] = 0;
	}
	out[0x80] = 0x33;
	out[0x81] = 0x75; /* version-ish matching CHA_01 */
	out[0x82] = (uint8_t)(w->dualPort ? 1 : 0);
	out[0x83] = (uint8_t)(w->wideTracks ? 1 : 0);
	CopySjisField(out + 0x160, w->titleSjis, 64);
	CopySjisField(out + 0x160 + 64, w->composerSjis, 64);
	CopySjisField(out + 0x160 + 128, w->commentSjis, 64);
	const uint32_t header = 0x200;
	const uint32_t dataAbs = 0x1000 + header;
	uint32_t sz = FinalizeMidiTracks(w, out, dataAbs, header, 0);
	return sz;
}

uint32_t SasamiBuildMpw2(const SasamiWriteMidi* w, uint8_t* out, uint32_t outCap)
{
	if (!w || !out || outCap < 0x400) return 0;
	memset(out, 0, outCap > SASAMI_WRITE_MAX ? SASAMI_WRITE_MAX : outCap);
	static const char kMagic[] = "\xEE\xEE\xEE\r\nSASAMI MPY Ver2.2\r\n";
	memcpy(out, kMagic, sizeof(kMagic) - 1);
	out[0x80] = 0x33;
	out[0x81] = 0x75;
	out[0x82] = (uint8_t)(w->dualPort ? 1 : 0);
	out[0x83] = 1; /* wide 64 tracks for MPW2 */
	CopySjisField(out + 0x160, w->titleSjis, 64);
	CopySjisField(out + 0x160 + 64, w->composerSjis, 64);
	CopySjisField(out + 0x160 + 128, w->commentSjis, 64);
	for (int ch = 0; ch < 64; ch++) {
		Put24(out + 0x200 + (uint32_t)ch * 4, 0x10F0);
		out[0x200 + (uint32_t)ch * 4 + 3] = 0;
	}
	const uint32_t header = 0x400;
	const uint32_t dataAbs = 0x1000 + header;
	/* Do NOT copy SasamiWriteMidi (~16MB) onto the stack. */
	SasamiWriteMidi* mut = const_cast<SasamiWriteMidi*>(w);
	const int oldTc = mut->trackCount;
	const int oldWide = mut->wideTracks;
	mut->trackCount = 64;
	mut->wideTracks = 1;
	uint32_t sz = FinalizeMidiTracks(mut, out, dataAbs, header, 1);
	mut->trackCount = oldTc;
	mut->wideTracks = oldWide;
	return sz;
}

/* .mpsmv = MPW2 body + "MPW3" + u32 ver=1 + 32 * (path + ints + state blobs) */
uint32_t SasamiBuildMpw3(const SasamiWriteMidi* w, uint8_t* out, uint32_t outCap)
{
	uint32_t sz = SasamiBuildMpw2(w, out, outCap);
	if (!sz) return 0;
	uint64_t trail = 8; /* magic + ver */
	for (int i = 0; i < 32; i++) {
		trail += 520 + 16 + 8;
		trail += w->vstCompLen[i];
		trail += w->vstCtrlLen[i];
	}
	if (sz + trail > outCap || sz + trail > SASAMI_WRITE_MAX) return 0;
	out[sz++] = 'M';
	out[sz++] = 'P';
	out[sz++] = 'W';
	out[sz++] = '3';
	uint32_t ver = 1;
	memcpy(out + sz, &ver, 4); sz += 4;
	for (int i = 0; i < 32; i++) {
		wchar_t path[260];
		memset(path, 0, sizeof(path));
		if (w->vstPath[i][0])
			wcsncpy_s(path, w->vstPath[i], _TRUNCATE);
		memcpy(out + sz, path, 520);
		sz += 520;
		int32_t prog = w->vstProg[i];
		int32_t msb = w->vstBankMsb[i];
		int32_t lsb = w->vstBankLsb[i];
		int32_t force = w->vstForceCh[i];
		memcpy(out + sz, &prog, 4); sz += 4;
		memcpy(out + sz, &msb, 4); sz += 4;
		memcpy(out + sz, &lsb, 4); sz += 4;
		memcpy(out + sz, &force, 4); sz += 4;
		uint32_t cLen = w->vstCompLen[i];
		uint32_t tLen = w->vstCtrlLen[i];
		memcpy(out + sz, &cLen, 4); sz += 4;
		memcpy(out + sz, &tLen, 4); sz += 4;
		if (cLen && w->vstComp[i]) {
			memcpy(out + sz, w->vstComp[i], cLen);
			sz += cLen;
		}
		if (tLen && w->vstCtrl[i]) {
			memcpy(out + sz, w->vstCtrl[i], tLen);
			sz += tLen;
		}
	}
	return sz;
}

uint32_t SasamiBuildFpy(const SasamiWriteFm* w, uint8_t* out, uint32_t outCap)
{
	if (!w || !out || outCap < 0x300) return 0;
	memset(out, 0, outCap > SASAMI_WRITE_MAX ? SASAMI_WRITE_MAX : outCap);
	/* header: 10 track ptrs, version, opna flag.
	   versionWord@0x1C: 0 = classic FPY (1-deep loops), 2 = FPY2 (nested up to 16). */
	if (w->fpy2) {
		out[0x1C] = 0x02;
		out[0x1D] = 0x00;
	} else {
		out[0x1C] = 0x00;
		out[0x1D] = 0x00;
	}
	if (w->opna10) {
		out[0x1E] = 0xFF;
		out[0x1F] = 0xFF;
	}
	CopySjisField(out + 0x50, w->titleSjis, 64);
	CopySjisField(out + 0x50 + 64, w->composerSjis, 64);
	CopySjisField(out + 0x50 + 128, w->commentSjis, 64);

	uint32_t fileOff = 0x100; /* leave room for header/title */
	uint32_t cursorAbs = 0x1000 + fileOff;
	uint32_t fmStart[10];
	uint32_t fmEnd[10];
	memset(fmStart, 0, sizeof(fmStart));
	memset(fmEnd, 0, sizeof(fmEnd));
	for (int ch = 0; ch < 10; ch++) {
		const SasamiTrackStream* s = &w->tr[ch];
		if (!s->used || s->size == 0) {
			Put16(out + ch * 2, 0x10F0);
			continue;
		}
		Put16(out + ch * 2, (uint16_t)cursorAbs);
		if (fileOff + s->size + 3 > SASAMI_WRITE_MAX) return 0;
		fmStart[ch] = fileOff;
		memcpy(out + fileOff, s->bytes, s->size);
		fmEnd[ch] = fileOff + s->size;
		/* Patch track-relative loop/jump markers to absolute 0x1xxx file addresses.
		   cmd14: w1 = body offset within this track stream
		   cmd3 with 0xF000|rel: J target within this track stream */
		{
			const uint32_t trackFileOff = fileOff;
			for (uint32_t i = 0; i + 2 < s->size; i += 3) {
				uint8_t* p = out + trackFileOff + i;
				if (p[0] == 14) {
					uint16_t rel = (uint16_t)(p[1] | (p[2] << 8));
					uint16_t abs = (uint16_t)(0x1000 + trackFileOff + rel);
					p[1] = (uint8_t)(abs & 0xFF);
					p[2] = (uint8_t)(abs >> 8);
				} else if (p[0] == 3) {
					uint16_t w1 = (uint16_t)(p[1] | (p[2] << 8));
					if ((w1 & 0xF000) == 0xF000) {
						uint16_t rel = (uint16_t)(w1 & 0x0FFF);
						uint16_t abs = (uint16_t)(0x1000 + trackFileOff + rel);
						p[1] = (uint8_t)(abs & 0xFF);
						p[2] = (uint8_t)(abs >> 8);
					}
				}
			}
		}
		fileOff += s->size;
		cursorAbs += s->size;
		/* FM end: FJUMP to 0xF0 */
		int needEnd = 1;
		if (s->size >= 3 && s->bytes[s->size - 3] == 3) needEnd = 0;
		if (needEnd) {
			out[fileOff++] = 3;
			out[fileOff++] = 0xF0;
			out[fileOff++] = 0x00;
			cursorAbs += 3;
		}
	}
	/* Misao PCM tracks (optional). Same 3-byte cmds as FM note/rest/tempo. */
	int misaoN = w->misaoChCount;
	if (misaoN < 0) misaoN = 0;
	if (misaoN > SASAMI_MISAO_MAX_CH) misaoN = SASAMI_MISAO_MAX_CH;
	int misaoUsed = 0;
	for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) {
		const SasamiTrackStream* s = &w->misao[i];
		if (!s->used || s->size == 0) {
			Put16(out + 0xD0 + i * 2, 0);
			continue;
		}
		Put16(out + 0xD0 + i * 2, (uint16_t)cursorAbs);
		if (fileOff + s->size + 3 > SASAMI_WRITE_MAX) return 0;
		memcpy(out + fileOff, s->bytes, s->size);
		fileOff += s->size;
		cursorAbs += s->size;
		int needEnd = 1;
		if (s->size >= 3 && s->bytes[s->size - 3] == 3) needEnd = 0;
		if (needEnd) {
			out[fileOff++] = 3;
			out[fileOff++] = 0xF0;
			out[fileOff++] = 0x00;
			cursorAbs += 3;
		}
		misaoUsed = 1;
		if (i + 1 > misaoN) misaoN = i + 1;
	}
	if (misaoUsed) {
		out[0xF0] = 1;
		out[0xF1] = (uint8_t)misaoN;
	}
	/* embed voices; patch FNEIRO placeholders 0xFE00|idx in prior stream bytes */
	uint16_t voiceAbs[64];
	memset(voiceAbs, 0, sizeof(voiceAbs));
	const uint32_t streamEnd = fileOff;
	for (int v = 0; v < w->voiceCount && v < 64; v++) {
		if (fileOff + 25 > SASAMI_WRITE_MAX) return 0;
		voiceAbs[v] = (uint16_t)(0x1000 + fileOff);
		memcpy(out + fileOff, w->voices[v], 25);
		fileOff += 25;
	}
	/* Patch 3-byte aligned FNEIRO cmds in stream region (tracks start at 0x100) */
	for (int ch = 0; ch < 10; ch++) {
		for (uint32_t i = fmStart[ch]; i && i + 2 < fmEnd[ch]; i += 3) {
			if (out[i] != 2) continue;
			uint16_t raw = (uint16_t)(out[i + 1] | (out[i + 2] << 8));
			if ((raw & 0xFF00) != 0xFE00) continue;
			int vi = (int)(raw & 0x3F);
			if (vi < 0 || vi >= 64 || voiceAbs[vi] == 0) continue;
			out[i + 1] = (uint8_t)(voiceAbs[vi] & 0xFF);
			out[i + 2] = (uint8_t)(voiceAbs[vi] >> 8);
		}
	}
	return fileOff;
}

int SasamiWriteFileW(const wchar_t* path, const uint8_t* data, uint32_t size)
{
	if (!path || !data || !size) return 0;
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD wr = 0;
	BOOL ok = WriteFile(h, data, size, &wr, NULL);
	CloseHandle(h);
	return ok && wr == size;
}
