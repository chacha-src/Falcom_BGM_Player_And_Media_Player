#include "stdafx.h"
#include "SasamiToneNames.h"
#include "resource.h"
#include "VstMidiEngine.h"

static BYTE* s_gsDat = NULL;
static BYTE* s_xgDat = NULL;
static BYTE* s_exDat = NULL;
static int s_gsBytes = 0, s_xgBytes = 0, s_exBytes = 0;
static int s_datTried = 0;

static const wchar_t* kGmName[128] = {
	L"Acoustic Grand", L"Bright Piano", L"Electric Grand", L"Honky-tonk",
	L"E.Piano 1", L"E.Piano 2", L"Harpsichord", L"Clavi",
	L"Celesta", L"Glockenspiel", L"Music Box", L"Vibraphone",
	L"Marimba", L"Xylophone", L"Tubular Bells", L"Dulcimer",
	L"Drawbar Organ", L"Percussive Organ", L"Rock Organ", L"Church Organ",
	L"Reed Organ", L"Accordion", L"Harmonica", L"Tango Accordion",
	L"Nylon Guitar", L"Steel Guitar", L"Jazz Guitar", L"Clean Guitar",
	L"Muted Guitar", L"Overdrive Gt", L"Distortion Gt", L"Gt Harmonics",
	L"Acoustic Bass", L"Finger Bass", L"Picked Bass", L"Fretless Bass",
	L"Slap Bass 1", L"Slap Bass 2", L"Synth Bass 1", L"Synth Bass 2",
	L"Violin", L"Viola", L"Cello", L"Contrabass",
	L"Tremolo Str", L"Pizzicato Str", L"Harp", L"Timpani",
	L"Strings", L"Slow Strings", L"Syn.Strings1", L"Syn.Strings2",
	L"Choir Aahs", L"Voice Oohs", L"SynVox", L"Orchestra Hit",
	L"Trumpet", L"Trombone", L"Tuba", L"Muted Trumpet",
	L"French Horns", L"Brass 1", L"Synth Brass1", L"Synth Brass2",
	L"Soprano Sax", L"Alto Sax", L"Tenor Sax", L"Baritone Sax",
	L"Oboe", L"EnglishHorn", L"Bassoon", L"Clarinet",
	L"Piccolo", L"Flute", L"Recorder", L"Pan Flute",
	L"Blown Bottle", L"Shakuhachi", L"Whistle", L"Ocarina",
	L"Square Lead", L"Saw Lead", L"Calliope", L"Chiff Lead",
	L"Charang", L"Voice Lead", L"Fifths Lead", L"Bass & Lead",
	L"New Age Pad", L"Warm Pad", L"Polysynth", L"Choir Pad",
	L"Bowed Glass", L"Metallic Pad", L"Halo Pad", L"Sweep Pad",
	L"Rain", L"Soundtrack", L"Crystal", L"Atmosphere",
	L"Brightness", L"Goblins", L"Echoes", L"Sci-Fi",
	L"Sitar", L"Banjo", L"Shamisen", L"Koto",
	L"Kalimba", L"Bagpipe", L"Fiddle", L"Shanai",
	L"Tinkle Bell", L"Agogo", L"Steel Drums", L"Woodblock",
	L"Taiko Drum", L"Melodic Tom", L"Synth Drum", L"Reverse Cymbal",
	L"Gt Fret Noise", L"Breath Noise", L"Seashore", L"Bird Tweet",
	L"Telephone", L"Helicopter", L"Applause", L"Gunshot"
};

static void CopyW(wchar_t* dst, int dstN, const wchar_t* src)
{
	if (!dst || dstN <= 0) return;
	wcsncpy_s(dst, dstN, src ? src : L"", _TRUNCATE);
}

static BOOL LoadResDat(UINT id, BYTE** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HINSTANCE hi = AfxGetResourceHandle();
	if (!hi) hi = GetModuleHandleW(NULL);
	HRSRC hr = FindResourceW(hi, MAKEINTRESOURCEW(id), RT_RCDATA);
	if (!hr) return FALSE;
	HGLOBAL hg = LoadResource(hi, hr);
	DWORD sz = SizeofResource(hi, hr);
	const BYTE* p = (const BYTE*)LockResource(hg);
	if (!p || sz < 20) return FALSE;
	BYTE* buf = new BYTE[sz];
	memcpy(buf, p, sz);
	*out = buf;
	*outN = (int)sz;
	return TRUE;
}

static BOOL LoadFileDat(const wchar_t* path, BYTE** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return FALSE;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz < 20 || sz > 8 * 1024 * 1024) { CloseHandle(f); return FALSE; }
	BYTE* buf = new BYTE[sz];
	if (!ReadFile(f, buf, sz, &got, NULL) || got != sz) {
		CloseHandle(f); delete[] buf; return FALSE;
	}
	CloseHandle(f);
	*out = buf;
	*outN = (int)sz;
	return TRUE;
}

static void EnsureDat()
{
	if (s_datTried) return;
	s_datTried = 1;
	if (!LoadResDat(IDR_SASAMI_GS, &s_gsDat, &s_gsBytes))
		LoadFileDat(L"C:\\Windows\\SASAMI_GS.DAT", &s_gsDat, &s_gsBytes);
	if (!LoadResDat(IDR_SASAMI_XG, &s_xgDat, &s_xgBytes))
		LoadFileDat(L"C:\\Windows\\SASAMI_XG.DAT", &s_xgDat, &s_xgBytes);
	if (!LoadResDat(IDR_SASAMI_EX, &s_exDat, &s_exBytes))
		LoadFileDat(L"C:\\Windows\\SASAMI_EX.DAT", &s_exDat, &s_exBytes);
}

static void SjisToW(const char* src, int srcN, wchar_t* dst, int dstN)
{
	if (!dst || dstN <= 0) return;
	dst[0] = 0;
	if (!src || srcN <= 0) return;
	char tmp[32];
	int n = srcN;
	if (n > 31) n = 31;
	memcpy(tmp, src, n);
	tmp[n] = 0;
	MultiByteToWideChar(932, 0, tmp, -1, dst, dstN);
	dst[dstN - 1] = 0;
}

static BOOL LookupGsExact(int mapId, int bank, int pc, wchar_t* out, int outN)
{
	if (!s_gsDat || s_gsBytes < 20) return FALSE;
	const int rec = s_gsBytes / 20;
	for (int i = 0; i < rec; ++i) {
		const BYTE* r = s_gsDat + i * 20;
		if (r[0] == (BYTE)mapId && r[1] == (BYTE)bank && r[2] == (BYTE)pc) {
			SjisToW((const char*)(r + 3), 17, out, outN);
			return out[0] != 0;
		}
	}
	return FALSE;
}

static BOOL LookupGs(int mapId, int bank, int pc, wchar_t* out, int outN)
{
	if (LookupGsExact(mapId, bank, pc, out, outN)) return TRUE;
	if (mapId == 0 || mapId == 6) return FALSE;
	/* Soft fallback for monitor / auto: 8850 then Capital. */
	if (LookupGsExact(4, bank, pc, out, outN)) return TRUE;
	if (LookupGsExact(4, 0, pc, out, outN)) return TRUE;
	return FALSE;
}

static BOOL LookupXgExact(int msb, int lsb, int pc, wchar_t* out, int outN)
{
	if (!s_xgDat || s_xgBytes < 20) return FALSE;
	const int rec = s_xgBytes / 20;
	for (int i = 0; i < rec; ++i) {
		const BYTE* r = s_xgDat + i * 20;
		if (r[0] == (BYTE)msb && r[1] == (BYTE)lsb && r[2] == (BYTE)pc) {
			SjisToW((const char*)(r + 3), 17, out, outN);
			return out[0] != 0;
		}
	}
	return FALSE;
}

static BOOL LookupEx(int mapId, int bank, int pc, wchar_t* out, int outN)
{
	if (!s_exDat || s_exBytes < 20) return FALSE;
	const int rec = s_exBytes / 20;
	for (int i = 0; i < rec; ++i) {
		const BYTE* r = s_exDat + i * 20;
		if (r[0] == (BYTE)mapId && r[1] == (BYTE)bank && r[2] == (BYTE)pc) {
			SjisToW((const char*)(r + 3), 17, out, outN);
			return out[0] != 0;
		}
	}
	return FALSE;
}

static int ExBank(int mapId, int msb, int lsb, int isDrum)
{
	if (mapId == 9 || mapId == 14) {
		if (isDrum || msb == 120) return 120;
		return lsb;
	}
	if (mapId == 11) {
		if (isDrum || msb == 122) return 122;
		return lsb;
	}
	if (mapId == 13) return lsb;
	return msb;
}

static BOOL LookupXgOk(int msb, int lsb, int pc, wchar_t* out, int outN)
{
	if (!s_xgDat || s_xgBytes < 20) return FALSE;
	const int rec = s_xgBytes / 20;
	for (int pass = 0; pass < 2; ++pass) {
		const BYTE wantM = (BYTE)((pass == 0) ? msb : 0);
		const BYTE wantL = (BYTE)((pass == 0) ? lsb : 0);
		const BYTE wantP = (BYTE)pc;
		for (int i = 0; i < rec; ++i) {
			const BYTE* r = s_xgDat + i * 20;
			if (r[0] == wantM && r[1] == wantL && r[2] == wantP) {
				SjisToW((const char*)(r + 3), 17, out, outN);
				return out[0] != 0;
			}
		}
	}
	return FALSE;
}

void SasamiToneLookup(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum,
	wchar_t* out, int outN)
{
	if (!out || outN <= 0) return;
	out[0] = 0;
	EnsureDat();
	if (isDrum) {
		if (isXg) {
			if (LookupXgOk(127, 0, pc, out, outN) && out[0]) return;
		} else {
			if (mapId >= 9 || bankMsb == 120) {
				const int mid = (mapId >= 9) ? mapId : 9;
				if (LookupEx(mid, ExBank(mid, bankMsb, bankLsb, 1), pc, out, outN) && out[0]) return;
			}
			if (VstMidiBankMsbIsSdNative(bankMsb) || mapId == 6) {
				if (LookupGs(6, bankMsb, pc, out, outN) && out[0]) return;
			}
			if (LookupGs(0, 0, pc, out, outN) && out[0]) return;
		}
		CopyW(out, outN, L"Standard Kit");
		return;
	}
	if (isXg) {
		if (LookupXgOk(bankMsb, bankLsb, pc, out, outN) && out[0]) return;
	} else if (mapId >= 9) {
		if (LookupEx(mapId, ExBank(mapId, bankMsb, bankLsb, 0), pc, out, outN) && out[0]) return;
	} else if (bankMsb == 121) {
		if (LookupEx(9, bankLsb, pc, out, outN) && out[0]) return;
	} else if (bankMsb == 126 || bankMsb == 127) {
		if (LookupGs(1, bankMsb, pc, out, outN) && out[0]) return;
	} else if (VstMidiBankMsbIsSdNative(bankMsb) || mapId == 6) {
		if (LookupGs(6, bankMsb, pc, out, outN) && out[0]) return;
		if (mapId == 6 && bankLsb >= 1 && bankLsb <= 4) {
			if (LookupGs(bankLsb, bankMsb, pc, out, outN) && out[0]) return;
		}
	} else if (mapId == 8) {
		const int b = (bankMsb == 126) ? 126 : 127;
		if (LookupGs(1, b, pc, out, outN) && out[0]) return;
	} else if (mapId != 5) {
		if (LookupGs(mapId, bankMsb, pc, out, outN) && out[0]) return;
	}
	if (pc >= 0 && pc < 128)
		CopyW(out, outN, kGmName[pc]);
	else
		CopyW(out, outN, L"—");
}

void SasamiToneLookupStrict(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum,
	wchar_t* out, int outN)
{
	if (!out || outN <= 0) return;
	out[0] = 0;
	EnsureDat();
	pc &= 127;
	bankMsb &= 127;
	bankLsb &= 127;
	if (isDrum) {
		if (isXg) {
			LookupXgExact(127, 0, pc, out, outN);
			return;
		}
		if (mapId >= 9 || bankMsb == 120) {
			const int mid = (mapId >= 9) ? mapId : 9;
			LookupEx(mid, ExBank(mid, bankMsb, bankLsb, 1), pc, out, outN);
			return;
		}
		if (VstMidiBankMsbIsSdNative(bankMsb) || mapId == 6) {
			LookupGsExact(6, bankMsb, pc, out, outN);
			return;
		}
		LookupGsExact(0, 0, pc, out, outN);
		return;
	}
	if (isXg) {
		LookupXgExact(bankMsb, bankLsb, pc, out, outN);
		return;
	}
	if (mapId >= 9) {
		LookupEx(mapId, ExBank(mapId, bankMsb, bankLsb, 0), pc, out, outN);
		return;
	}
	if (bankMsb == 121) {
		LookupEx(9, bankLsb, pc, out, outN);
		return;
	}
	if (mapId == 8) {
		const int b = (bankMsb == 126) ? 126 : 127;
		LookupGsExact(1, b, pc, out, outN);
		return;
	}
	if (mapId == 5) {
		/* GM: only bank 0/0 has names. */
		if (bankMsb == 0 && bankLsb == 0 && pc >= 0 && pc < 128)
			CopyW(out, outN, kGmName[pc]);
		return;
	}
	if (VstMidiBankMsbIsSdNative(bankMsb) || mapId == 6) {
		LookupGsExact(6, bankMsb, pc, out, outN);
		return;
	}
	LookupGsExact(mapId, bankMsb, pc, out, outN);
}

int SasamiToneBankUsed(int isXg, int mapId, int bankMsb, int bankLsb, int isDrum)
{
	wchar_t n[40];
	for (int pc = 0; pc < 128; ++pc) {
		n[0] = 0;
		SasamiToneLookupStrict(isXg, mapId, bankMsb, bankLsb, pc, isDrum, n, 40);
		if (n[0]) return 1;
	}
	return 0;
}

void SasamiToneLookupAuto(int bankMsb, int bankLsb, int pc, int isDrum, wchar_t* out, int outN)
{
	if (!out || outN <= 0) return;
	out[0] = 0;
	EnsureDat();
	pc &= 127;
	bankMsb &= 127;
	bankLsb &= 127;
	const int drumCh = isDrum;

	if (drumCh) {
		if (LookupXgOk(127, 0, pc, out, outN) && out[0]) return;
		if (LookupGs(0, 0, pc, out, outN) && out[0]) return;
		CopyW(out, outN, L"Standard Kit");
		return;
	}

	/* XG-ish MSB (0 + LSB variation, or 127 drum already handled) */
	if (LookupXgOk(bankMsb, bankLsb, pc, out, outN) && out[0]) return;

	/* GS Capital / SC maps — SASAMI @n:m uses bank as GS bank byte */
	if (LookupGs(4, bankMsb, pc, out, outN) && out[0]) return;
	if (LookupGs(1, bankMsb, pc, out, outN) && out[0]) return;
	if (bankLsb && LookupGs(4, bankLsb, pc, out, outN) && out[0]) return;
	if (VstMidiBankMsbIsSdNative(bankMsb)) {
		if (LookupGs(6, bankMsb, pc, out, outN) && out[0]) return;
	}
	if (bankMsb == 121 && LookupEx(9, bankLsb, pc, out, outN) && out[0]) return;
	if (LookupGs(4, 0, pc, out, outN) && out[0]) return;

	CopyW(out, outN, kGmName[pc]);
}
