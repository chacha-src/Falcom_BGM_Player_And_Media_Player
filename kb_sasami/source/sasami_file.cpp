#include "sasami_file.h"
#include "sasami_misao.h"

#include <windows.h>
#include <string.h>
#include <ctype.h>

static const wchar_t* SasamiExtOf(const wchar_t* path)
{
	if (!path) return L"";
	const wchar_t* dot = wcsrchr(path, L'.');
	return dot ? dot : L"";
}

static int SasamiEqExt(const wchar_t* path, const wchar_t* ext)
{
	const wchar_t* p = SasamiExtOf(path);
	while (*p && *ext) {
		wchar_t a = *p++;
		wchar_t b = *ext++;
		if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
		if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
		if (a != b) return 0;
	}
	return *p == 0 && *ext == 0;
}

bool SasamiExtIsMidi(const wchar_t* path)
{
	return SasamiEqExt(path, L".mpy") || SasamiEqExt(path, L".mpw2");
}

bool SasamiExtIsFm(const wchar_t* path)
{
	return SasamiEqExt(path, L".fpy");
}

bool SasamiExtIsAny(const wchar_t* path)
{
	return SasamiExtIsFm(path) || SasamiExtIsMidi(path);
}

static int SasamiSjisFieldOk(const char* s, int n)
{
	if (!s || n <= 0) return 0;
	int len = 0;
	int bad = 0;
	for (int i = 0; i < n && s[i]; i++) {
		const unsigned char c = (unsigned char)s[i];
		len++;
		if (c < 0x20 && c != 0x09)
			bad++;
	}
	if (len < 1) return 0;
	return bad * 4 <= len;
}

static void SasamiReadSjisField(const uint8_t* data, size_t size, uint32_t off, char* out, int outCap)
{
	out[0] = 0;
	if (!data || outCap < 2 || off >= size) return;
	const size_t n = (size - off < 64) ? (size - off) : 64;
	char buf[66];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, data + off, n);
	if (!SasamiSjisFieldOk(buf, (int)n)) return;
	int last = 0;
	for (int i = 0; i < 64 && buf[i]; i++) last = i + 1;
	if (last <= 0) return;
	memcpy(out, buf, (size_t)last);
	out[last] = 0;
}

static int SasamiCiEq(const char* s, const char* lit)
{
	for (; *lit; ++s, ++lit) {
		char a = *s;
		char b = *lit;
		if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if (a != b) return 0;
	}
	return 1;
}

static const char* SasamiFindCi(const char* s, const char* lit)
{
	if (!s || !lit || !lit[0]) return NULL;
	for (const char* p = s; *p; ++p) {
		if (SasamiCiEq(p, lit))
			return p;
	}
	return NULL;
}

static void SasamiSplitEmbeddedComposer(SasamiTags* tags)
{
	if (!tags || !tags->titleSjis[0] || tags->composerSjis[0]) return;
	char* t = tags->titleSjis;
	int best = -1;
	static const char* marks[] = { " Comp/Arr:", " Comp:", " Comp/", " Arr:" };
	for (int m = 0; m < (int)(sizeof(marks) / sizeof(marks[0])); ++m) {
		for (char* p = t; *p; ++p) {
			if (*p != ' ' && *p != '\t') continue;
			if (!SasamiCiEq(p + 1, marks[m] + 1)) continue;
			if (best < 0 || (int)(p - t) < best)
				best = (int)(p - t);
			break;
		}
	}
	if (best < 0) {
		const char* hit = SasamiFindCi(t, "omp/");
		if (!hit) hit = SasamiFindCi(t, "omp:");
		if (hit) {
			const char* q = hit;
			while (q > t && q[-1] != ' ' && q[-1] != '\t') --q;
			best = (int)(q - t);
		}
	}
	if (best < 0) return;
	char comp[65];
	strncpy_s(comp, t + best, _TRUNCATE);
	while (comp[0] == ' ' || comp[0] == '\t')
		memmove(comp, comp + 1, strlen(comp));
	strncpy_s(tags->composerSjis, comp, _TRUNCATE);
	t[best] = 0;
	while (best > 0 && (t[best - 1] == ' ' || t[best - 1] == '\t'))
		t[--best] = 0;
}

static void SasamiReadTagsFromMemory(const uint8_t* data, size_t size, SasamiKind kind, SasamiTags* out)
{
	memset(out, 0, sizeof(*out));
	if (!data || !out || size < 0x90) return;
	const uint32_t base = (kind == SASAMI_KIND_FPY) ? 0x50u : 0x160u;
	SasamiReadSjisField(data, size, base, out->titleSjis, 65);
	SasamiReadSjisField(data, size, base + 0x40u, out->composerSjis, 65);
	SasamiReadSjisField(data, size, base + 0x80u, out->commentSjis, 65);
	SasamiSplitEmbeddedComposer(out);
}

bool SasamiPeekTagsW(const wchar_t* path, SasamiTags* out)
{
	if (!path || !out) return false;
	memset(out, 0, sizeof(*out));
	const SasamiKind kind = SasamiKindFromPath(path);
	if (kind == SASAMI_KIND_UNKNOWN) return false;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	uint8_t buf[0x240];
	DWORD got = 0;
	const BOOL ok = ReadFile(h, buf, sizeof(buf), &got, NULL);
	CloseHandle(h);
	if (!ok || got < 0x90) return false;
	SasamiReadTagsFromMemory(buf, got, kind, out);
	return out->titleSjis[0] || out->composerSjis[0] || out->commentSjis[0];
}

SasamiKind SasamiKindFromPath(const wchar_t* path)
{
	if (SasamiEqExt(path, L".fpy")) return SASAMI_KIND_FPY;
	if (SasamiEqExt(path, L".mpw2")) return SASAMI_KIND_MPW2;
	if (SasamiEqExt(path, L".mpy")) return SASAMI_KIND_MPY;
	return SASAMI_KIND_UNKNOWN;
}

bool SasamiOffOk(const SasamiSong& s, uint32_t off, uint32_t need)
{
	return (uint64_t)off + need <= (uint64_t)s.dataSize;
}

uint8_t SasamiGet(const SasamiSong& s, uint32_t off)
{
	if (off >= s.dataSize) return 0;
	return s.data[off];
}

uint16_t SasamiGet16(const SasamiSong& s, uint32_t off)
{
	return (uint16_t)(SasamiGet(s, off) | ((uint16_t)SasamiGet(s, off + 1) << 8));
}

uint32_t SasamiGet24(const SasamiSong& s, uint32_t off)
{
	return (uint32_t)SasamiGet(s, off)
		| ((uint32_t)SasamiGet(s, off + 1) << 8)
		| ((uint32_t)SasamiGet(s, off + 2) << 16);
}

static void SasamiLoadMisaoTracks(SasamiSong* out)
{
	out->misaoEnabled = 0;
	out->misaoChCount = 0;
	memset(out->misaoTracks, 0, sizeof(out->misaoTracks));
	if (!out || out->dataSize < 0xF2) return;

	const int isFpy = (out->kind == SASAMI_KIND_FPY);
	const int isMpy = (out->kind == SASAMI_KIND_MPY || out->kind == SASAMI_KIND_MPW2);
	if (!isFpy && !isMpy) return;

	const uint8_t f0 = SasamiGet(*out, 0xF0);
	const int ppSig = isMpy && out->dataSize > 0xF3
		&& out->data[0xF1] == 'P' && out->data[0xF2] == 'P'
		&& (out->data[0xF3] == '\n' || out->data[0xF3] == 0);

	if (isFpy && (f0 == 0 || f0 == 0xFF)) {
		/* extended FPY may still have 0xD0 tracks without classic f0=1 flag */
	} else if (isMpy && !ppSig && f0 == 0) {
		return;
	}

	int maxCh = 0;
	int anyValid = 0;
	for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) {
		const uint16_t ptr = SasamiGet16(*out, (uint32_t)(0xD0 + i * 2));
		const uint32_t off = (ptr >= 0x1000) ? (uint32_t)(ptr - 0x1000) : ptr;
		out->misaoTracks[i].fileOff = off;
		out->misaoTracks[i].unused = SasamiMisaoTrackValid(*out, ptr) ? 0 : 1;
		if (!out->misaoTracks[i].unused && off) {
			anyValid = 1;
			if (i + 1 > maxCh) maxCh = i + 1;
		}
	}
	if (!anyValid) return;

	out->misaoEnabled = 1;
	if (isFpy) {
		if (f0 == 1) {
			const int f1 = SasamiGet(*out, 0xF1);
			out->misaoChCount = (f1 > 0 && f1 <= SASAMI_MISAO_MAX_CH) ? f1 : maxCh;
		} else {
			out->misaoChCount = maxCh;
		}
	} else if (ppSig) {
		out->misaoChCount = (f0 > 0 && f0 <= SASAMI_MISAO_MAX_CH) ? f0 : maxCh;
	} else {
		const int f1 = SasamiGet(*out, 0xF1);
		out->misaoChCount = (f1 > 0 && f1 <= SASAMI_MISAO_MAX_CH) ? f1 : maxCh;
		if (out->misaoChCount <= 0 && f0 > 0 && f0 <= SASAMI_MISAO_MAX_CH)
			out->misaoChCount = f0;
	}
	if (maxCh > out->misaoChCount) out->misaoChCount = maxCh;
	if (out->misaoChCount > SASAMI_MISAO_MAX_CH) out->misaoChCount = SASAMI_MISAO_MAX_CH;
}

static void SasamiReadTitle(SasamiSong* out)
{
	out->titleSjis[0] = 0;
	const uint32_t base = (out->kind == SASAMI_KIND_FPY) ? 0x50u : 0x160u;
	if (base >= out->dataSize) return;
	char buf[66];
	memset(buf, 0, sizeof(buf));
	const size_t n = (out->dataSize - base < 64) ? (out->dataSize - base) : 64;
	memcpy(buf, &out->data[base], n);
	int last = 0;
	for (int i = 0; i < 64 && buf[i]; i++) last = i + 1;
	if (last > 0) {
		memcpy(out->titleSjis, buf, (size_t)last);
		out->titleSjis[last] = 0;
	}
}

bool SasamiLoadMemory(const uint8_t* bytes, size_t size, SasamiKind hint, SasamiSong* out)
{
	if (!bytes || !out || size < 32) return false;
	if (size > SASAMI_MAX_FILE) return false;
	memset(out, 0, sizeof(*out));
	memcpy(out->data, bytes, size);
	out->dataSize = (uint32_t)size;
	out->kind = hint;
	out->mpyVersion = 1;
	out->trackCount = 0;

	if (hint == SASAMI_KIND_FPY) {
		out->kind = SASAMI_KIND_FPY;
		out->fmOpna10ch = (SasamiGet16(*out, 0x1E) == 0xFFFF) ? 1 : 0;
		out->versionWord = SasamiGet16(*out, 0x1C);
		out->trackCount = 10;
		for (int ch = 0; ch < 10; ch++) {
			const uint16_t ptr = SasamiGet16(*out, (uint32_t)(ch * 2));
			uint32_t off = (ptr >= 0x1000) ? (uint32_t)(ptr - 0x1000) : ptr;
			out->tracks[ch].fileOff = off;
			out->tracks[ch].part = -1;
			out->tracks[ch].unused = (ptr == 0 || ptr == 0x10F0 || off >= size) ? 1 : 0;
		}
		SasamiReadTitle(out);
		SasamiLoadMisaoTracks(out);
		return true;
	}

	if (size < 0x84) return false;

	const int isV2header =
		(bytes[0] == 0xEE && bytes[1] == 0xEE && bytes[2] == 0xEE);
	out->mpyVersion = isV2header ? 2 : 1;
	if (hint == SASAMI_KIND_MPW2) out->kind = SASAMI_KIND_MPW2;
	else out->kind = SASAMI_KIND_MPY;
	if (isV2header) out->kind = SASAMI_KIND_MPW2;

	out->versionWord = (unsigned)bytes[0x80] + ((unsigned)bytes[0x81] << 8);
	out->dualPort = bytes[0x82];
	out->wideTracks = bytes[0x83];
	out->trackCount = (bytes[0x83] == 1) ? 64 : 32;
	if (out->trackCount > 64) out->trackCount = 64;

	if (out->mpyVersion == 1) {
		for (int ch = 0; ch < out->trackCount; ch++) {
			int packed;
			if (ch > 31)
				packed = 0x100 + (ch - 32) * 3;
			else
				packed = ch * 3;
			const uint16_t addr = SasamiGet16(*out, (uint32_t)packed);
			uint32_t off = (addr >= 0x1000) ? (uint32_t)(addr - 0x1000) : addr;
			out->tracks[ch].fileOff = off;
			out->tracks[ch].part = SasamiGet(*out, (uint32_t)packed + 2) & 0x0F;
			out->tracks[ch].unused = (addr == 0x10F0 || off == 0xF0 || addr == 0) ? 1 : 0;
		}
	} else {
		if (size < 0x200 + 64 * 4) return false;
		for (int ch = 0; ch < out->trackCount; ch++) {
			const uint32_t packed = 0x200 + (uint32_t)ch * 4;
			const uint32_t addr = SasamiGet24(*out, packed);
			uint32_t off = (addr >= 0x1000) ? (addr - 0x1000) : addr;
			out->tracks[ch].fileOff = off;
			out->tracks[ch].part = SasamiGet(*out, packed + 3) & 0x0F;
			out->tracks[ch].unused = (off == 0xF0 || addr == 0) ? 1 : 0;
		}
	}
	SasamiReadTitle(out);
	if (out->kind == SASAMI_KIND_MPY || out->kind == SASAMI_KIND_MPW2) {
		if (out->wideTracks == 1 && !out->titleSjis[0] && size > 0x160)
			SasamiReadTitle(out);
	}
	SasamiLoadMisaoTracks(out);
	return true;
}

bool SasamiLoadFileW(const wchar_t* path, SasamiSong* out)
{
	if (!path || !out) return false;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > SASAMI_MAX_FILE) {
		CloseHandle(h);
		return false;
	}
	static uint8_t s_loadBuf[SASAMI_MAX_FILE];
	DWORD n = 0;
	const BOOL ok = ReadFile(h, s_loadBuf, (DWORD)li.QuadPart, &n, NULL);
	CloseHandle(h);
	if (!ok || n == 0) return false;
	return SasamiLoadMemory(s_loadBuf, n, SasamiKindFromPath(path), out);
}
