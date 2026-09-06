#include "StdAfx.h"
#include "cemu_modepref.h"
#include "cemu_mgr.h"
#include "cemu_zipfs.h"
#include "driver/cemu_driver.h"
#include "machine/cemu_hard.h"
#include "machine/cemu_hard_pcat.h"
#include <string.h>
#include <shlobj.h>

static int CEmuModeEntryHasOpt(const CEmuGameEntry* e, const char* name)
{
	if (!e || !name) return 0;
	for (int i = 0; i < e->optCount; i++) {
		if (_stricmp(e->opt[i].name, name) == 0)
			return 1;
	}
	return 0;
}

static int CEmuModeEntryIsMidi(const CEmuGameEntry* e)
{
	if (!e) return 0;
	if (CEmuModeEntryHasOpt(e, "midiout")) return 1;
	if (_stricmp(e->subtype, "midi") == 0) return 1;
	if (_stricmp(e->subtype, "midiout") == 0) return 1;
	if (_strnicmp(e->subtype, "midi", 4) == 0) return 1;
	return 0;
}

int CEmuModeIsMidiTag(const char* tag)
{
	if (!tag || !tag[0]) return 0;
	return (_stricmp(tag, "MIDI") == 0) ? 1 : 0;
}

int CEmuModeTagFromEntry(const CEmuGameEntry* e, char* tag, int tagCap)
{
	if (!tag || tagCap <= 0) return 0;
	tag[0] = 0;
	if (!e) return 0;
	if (CEmuModeEntryIsMidi(e)) {
		strncpy_s(tag, (size_t)tagCap, "MIDI", _TRUNCATE);
		return 1;
	}
	const char* s = e->subtype;
	if (!s || !s[0]) {
		strncpy_s(tag, (size_t)tagCap, "FM", _TRUNCATE);
		return 1;
	}
	/* Normalize common hoot subtypes to playlist chip tags. */
	if (_stricmp(s, "opna") == 0 || _stricmp(s, "8801-10") == 0)
		strncpy_s(tag, (size_t)tagCap, "OPNA", _TRUNCATE);
	else if (_stricmp(s, "opn") == 0 || _stricmp(s, "opn2") == 0 || _stricmp(s, "opnb") == 0)
		strncpy_s(tag, (size_t)tagCap, "OPN", _TRUNCATE);
	else if (_stricmp(s, "opm") == 0)
		strncpy_s(tag, (size_t)tagCap, "OPM", _TRUNCATE);
	else if (_stricmp(s, "opll") == 0 || _stricmp(s, "ym2413") == 0)
		strncpy_s(tag, (size_t)tagCap, "OPLL", _TRUNCATE);
	else if (_stricmp(s, "opl2") == 0 || _stricmp(s, "opl") == 0)
		strncpy_s(tag, (size_t)tagCap, "OPL2", _TRUNCATE);
	else if (_stricmp(s, "adlib") == 0)
		strncpy_s(tag, (size_t)tagCap, "ADLIB", _TRUNCATE);
	else if (_stricmp(s, "opl3") == 0)
		strncpy_s(tag, (size_t)tagCap, "OPL3", _TRUNCATE);
	else if (_stricmp(s, "soundblaster16") == 0 || _stricmp(s, "sb16") == 0
		|| _stricmp(s, "soundblaster") == 0 || _stricmp(s, "sbpro") == 0
		|| _stricmp(s, "sb") == 0)
		strncpy_s(tag, (size_t)tagCap, "SOUNDBLASTER", _TRUNCATE);
	else if (_stricmp(s, "gameblaster") == 0 || _stricmp(s, "cms") == 0)
		strncpy_s(tag, (size_t)tagCap, "GAMEBLASTER", _TRUNCATE);
	else if (_stricmp(s, "86") == 0 || _stricmp(s, "86+otomix2") == 0)
		strncpy_s(tag, (size_t)tagCap, "86", _TRUNCATE);
	else if (_stricmp(s, "beep") == 0)
		strncpy_s(tag, (size_t)tagCap, "BEEP", _TRUNCATE);
	else {
		/* Uppercase short subtype for chip (max tag-1). */
		char tmp[CEMU_MODE_TAG];
		int j = 0;
		for (const char* p = s; *p && j < CEMU_MODE_TAG - 1; p++) {
			char c = *p;
			if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				tmp[j++] = c;
		}
		tmp[j] = 0;
		if (!tmp[0]) strncpy_s(tmp, "FM", _TRUNCATE);
		strncpy_s(tag, (size_t)tagCap, tmp, _TRUNCATE);
	}
	return 1;
}

static int CEmuModeTagPreferRank(const char* tag)
{
	if (!tag || !tag[0]) return 0;
	if (_stricmp(tag, "OPNA") == 0) return 40;
	if (_stricmp(tag, "OPN") == 0) return 30;
	if (_stricmp(tag, "OPM") == 0) return 28;
	if (_stricmp(tag, "OPL3") == 0) return 26;
	if (_stricmp(tag, "SOUNDBLASTER") == 0 || _stricmp(tag, "ADLIB") == 0
		|| _stricmp(tag, "OPLL") == 0 || _stricmp(tag, "OPL2") == 0) return 24;
	if (_stricmp(tag, "GAMEBLASTER") == 0) return 22;
	if (_stricmp(tag, "86") == 0) return 20;
	if (_stricmp(tag, "BEEP") == 0) return 2;
	if (_stricmp(tag, "MIDI") == 0) return -100; /* selectable, never default when FM exists */
	return 10;
}

static void CEmuModeNormKey(const char* archive, char* key, int keyCap)
{
	strncpy_s(key, (size_t)keyCap, archive ? archive : "", _TRUNCATE);
	for (char* p = key; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + ('a' - 'A'));
	}
}

int CEmuCatalogListArchiveModes(const CEmuCatalog* cat, const char* archive,
	const char* dataDirHint, const CEmuZipFs* zipFs,
	CEmuArchiveMode* out, int outCap)
{
	(void)zipFs;
	if (!cat || !archive || !archive[0] || !out || outCap <= 0) return 0;
	char key[CEMU_ARCHIVE_NAME];
	CEmuModeNormKey(archive, key, (int)sizeof(key));
	int n = 0;
	for (int i = 0; i < cat->count; i++) {
		const CEmuGameEntry* e = cat->entry[i];
		if (!e) continue;
		if (_stricmp(e->archive, key) != 0) continue;
		if (dataDirHint && dataDirHint[0] && _stricmp(e->dataDir, dataDirHint) != 0)
			continue;
		char tag[CEMU_MODE_TAG];
		if (!CEmuModeTagFromEntry(e, tag, (int)sizeof(tag))) continue;
		int found = -1;
		for (int j = 0; j < n; j++) {
			if (_stricmp(out[j].tag, tag) == 0) { found = j; break; }
		}
		if (found >= 0) {
			/* Keep higher-chip entry index for same tag (more titles). */
			const CEmuGameEntry* prev = cat->entry[out[found].entryIndex];
			if (prev && e->titleCount > prev->titleCount)
				out[found].entryIndex = i;
			continue;
		}
		if (n >= outCap) continue;
		memset(&out[n], 0, sizeof(out[n]));
		strncpy_s(out[n].tag, tag, _TRUNCATE);
		strncpy_s(out[n].subtype, e->subtype, _TRUNCATE);
		out[n].isMidi = CEmuModeEntryIsMidi(e) ? 1 : 0;
		out[n].entryIndex = i;
		n++;
	}
	/* Sort: non-MIDI by prefer rank desc, MIDI last. */
	for (int a = 0; a < n; a++) {
		for (int b = a + 1; b < n; b++) {
			const int ra = CEmuModeTagPreferRank(out[a].tag);
			const int rb = CEmuModeTagPreferRank(out[b].tag);
			if (rb > ra) {
				CEmuArchiveMode tmp = out[a];
				out[a] = out[b];
				out[b] = tmp;
			}
		}
	}
	return n;
}

const CEmuGameEntry* CEmuCatalogFindArchiveForZipMode(const CEmuCatalog* cat,
	const char* archive, const char* dataDirHint, const CEmuZipFs* zipFs,
	const char* preferTag)
{
	if (!preferTag || !preferTag[0])
		return CEmuCatalogFindArchiveForZip(cat, archive, dataDirHint, zipFs);

	CEmuArchiveMode modes[CEMU_MODE_MAX];
	const int n = CEmuCatalogListArchiveModes(cat, archive, dataDirHint, zipFs, modes, CEMU_MODE_MAX);
	for (int i = 0; i < n; i++) {
		if (_stricmp(modes[i].tag, preferTag) != 0) continue;
		if (modes[i].entryIndex < 0 || modes[i].entryIndex >= cat->count) continue;
		const CEmuGameEntry* pick = cat->entry[modes[i].entryIndex];
		return pick;
	}
	/* Tag not found — fall back to default prefer (non-MIDI). */
	return CEmuCatalogFindArchiveForZip(cat, archive, dataDirHint, zipFs);
}

static ULONGLONG CEmuModeHashPath(const wchar_t* path)
{
	ULONGLONG h = 14695981039346656037ULL;
	if (!path) return h;
	for (const wchar_t* p = path; *p; p++) {
		wchar_t c = *p;
		if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
		if (c == L'/') c = L'\\';
		h ^= (ULONGLONG)c;
		h *= 1099511628211ULL;
	}
	return h;
}

static ULONGLONG CEmuModeHashStemA(const char* stem)
{
	ULONGLONG h = 14695981039346656037ULL;
	if (!stem) return h;
	/* Namespace prefix so stem keys never collide with legacy path hashes. */
	static const char kNs[] = "cemu-mode-stem:";
	for (const char* p = kNs; *p; p++) {
		h ^= (ULONGLONG)(unsigned char)*p;
		h *= 1099511628211ULL;
	}
	for (const char* p = stem; *p; p++) {
		char c = *p;
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		h ^= (ULONGLONG)(unsigned char)c;
		h *= 1099511628211ULL;
	}
	return h;
}

static int CEmuModePrefFileFromHash(ULONGLONG h, wchar_t* out, int outCap)
{
	if (!out || outCap <= 0) return 0;
	out[0] = 0;
	wchar_t base[MAX_PATH];
	base[0] = 0;
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)) || !base[0])
		return 0;
	_snwprintf_s(out, (size_t)outCap, _TRUNCATE, L"%s\\oggYSED\\cemumode", base);
	CreateDirectoryW(out, NULL);
	_snwprintf_s(out, (size_t)outCap, _TRUNCATE, L"%s\\oggYSED\\cemumode\\%016I64X", base, h);
	return 1;
}

/* Keys: archive stem (stable across relative/absolute/::title) + legacy path hashes. */
static int CEmuModePrefCollectKeys(const wchar_t* zipPath, ULONGLONG* keys, int keyCap,
	ULONGLONG* outCanon)
{
	if (!keys || keyCap < 1 || !zipPath || !zipPath[0]) return 0;
	int n = 0;
	auto addKey = [&](ULONGLONG h) {
		if (n >= keyCap) return;
		for (int i = 0; i < n; i++)
			if (keys[i] == h) return;
		keys[n++] = h;
	};
	auto addPath = [&](const wchar_t* p) {
		if (!p || !p[0]) return;
		addKey(CEmuModeHashPath(p));
	};

	wchar_t phys[CEMU_ZIP_PATH];
	unsigned ti = 1;
	phys[0] = 0;
	CEmuParseVirtualPath(zipPath, phys, (int)_countof(phys), &ti);
	(void)ti;
	const wchar_t* src = phys[0] ? phys : zipPath;

	char stem[CEMU_ARCHIVE_NAME] = {};
	if (CEmuArchiveStemFromPath(src, stem, (int)sizeof(stem)) && stem[0])
		addKey(CEmuModeHashStemA(stem));

	addPath(zipPath);
	addPath(src);
	wchar_t full[CEMU_ZIP_PATH];
	full[0] = 0;
	DWORD got = GetFullPathNameW(src, (DWORD)_countof(full), full, NULL);
	if (got > 0 && got < (DWORD)_countof(full) && full[0])
		addPath(full);

	/* Canonical write key = stem when known (survives path spelling). */
	ULONGLONG canon = keys[0];
	if (stem[0])
		canon = CEmuModeHashStemA(stem);
	if (outCanon) *outCanon = canon;
	return n;
}

static int CEmuModePrefReadFile(const wchar_t* path, char* tagOut, int tagCap)
{
	if (!path || !path[0] || !tagOut || tagCap <= 0) return 0;
	tagOut[0] = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	char buf[64] = {};
	DWORD rd = 0;
	ReadFile(h, buf, (DWORD)sizeof(buf) - 1, &rd, NULL);
	CloseHandle(h);
	if (rd < 1) return 0;
	buf[rd] = 0;
	for (int i = (int)strlen(buf) - 1; i >= 0; i--) {
		if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ') buf[i] = 0;
		else break;
	}
	if (!buf[0]) return 0;
	strncpy_s(tagOut, (size_t)tagCap, buf, _TRUNCATE);
	return 1;
}

int CEmuModePrefGet(const wchar_t* zipPath, char* tagOut, int tagCap)
{
	if (!tagOut || tagCap <= 0) return 0;
	tagOut[0] = 0;
	if (!zipPath || !zipPath[0]) return 0;
	ULONGLONG keys[8];
	ULONGLONG canon = 0;
	const int nk = CEmuModePrefCollectKeys(zipPath, keys, 8, &canon);
	wchar_t path[MAX_PATH];
	for (int i = 0; i < nk; i++) {
		if (!CEmuModePrefFileFromHash(keys[i], path, MAX_PATH)) continue;
		if (!CEmuModePrefReadFile(path, tagOut, tagCap)) continue;
		/* Migrate legacy path-hash sidecar → stem key for ResolveZip. */
		if (canon && keys[i] != canon) {
			wchar_t canonPath[MAX_PATH];
			if (CEmuModePrefFileFromHash(canon, canonPath, MAX_PATH)) {
				HANDLE wh = CreateFileW(canonPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
					CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (wh != INVALID_HANDLE_VALUE) {
					DWORD wr = 0;
					WriteFile(wh, tagOut, (DWORD)strlen(tagOut), &wr, NULL);
					WriteFile(wh, "\n", 1, &wr, NULL);
					CloseHandle(wh);
				}
			}
		}
		return 1;
	}
	return 0;
}

void CEmuModePrefSet(const wchar_t* zipPath, const char* tag)
{
	if (!zipPath || !zipPath[0]) return;
	ULONGLONG keys[8];
	ULONGLONG canon = 0;
	const int nk = CEmuModePrefCollectKeys(zipPath, keys, 8, &canon);
	if (nk < 1) return;
	wchar_t path[MAX_PATH];
	if (!tag || !tag[0]) {
		for (int i = 0; i < nk; i++) {
			if (CEmuModePrefFileFromHash(keys[i], path, MAX_PATH))
				DeleteFileW(path);
		}
		return;
	}
	if (!CEmuModePrefFileFromHash(canon ? canon : keys[0], path, MAX_PATH))
		return;
	HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	DWORD wr = 0;
	WriteFile(h, tag, (DWORD)strlen(tag), &wr, NULL);
	WriteFile(h, "\n", 1, &wr, NULL);
	CloseHandle(h);
	for (int i = 0; i < nk; i++) {
		if (keys[i] == (canon ? canon : keys[0])) continue;
		wchar_t alias[MAX_PATH];
		if (CEmuModePrefFileFromHash(keys[i], alias, MAX_PATH))
			DeleteFileW(alias);
	}
}

static int CEmuModeIsMidiExtW(const wchar_t* name)
{
	if (!name) return 0;
	const wchar_t* dot = wcsrchr(name, L'.');
	if (!dot) return 0;
	return (_wcsicmp(dot, L".mid") == 0
		|| _wcsicmp(dot, L".midi") == 0
		|| _wcsicmp(dot, L".rmi") == 0
		|| _wcsicmp(dot, L".smf") == 0) ? 1 : 0;
}

int CEmuZipExtractFirstMidi(const wchar_t* zipPath, wchar_t* outMidPath, int outCap)
{
	if (!zipPath || !outMidPath || outCap <= 0) return 0;
	outMidPath[0] = 0;
	CEmuZipFs* fs = (CEmuZipFs*)malloc(sizeof(CEmuZipFs));
	if (!fs) return 0;
	int ok = 0;
	if (CEmuZipFsOpen(fs, zipPath)) {
		int best = -1;
		for (int i = 0; i < fs->fileCount; i++) {
			if (!CEmuModeIsMidiExtW(fs->files[i].path)) continue;
			if (best < 0 || fs->files[i].size > fs->files[best].size)
				best = i;
		}
		if (best >= 0 && fs->files[best].data && fs->files[best].size > 0) {
			wchar_t tmpDir[MAX_PATH];
			tmpDir[0] = 0;
			GetTempPathW(MAX_PATH, tmpDir);
			const wchar_t* base = wcsrchr(fs->files[best].path, L'\\');
			if (!base) base = wcsrchr(fs->files[best].path, L'/');
			base = base ? base + 1 : fs->files[best].path;
			_snwprintf_s(outMidPath, (size_t)outCap, _TRUNCATE, L"%scemu_%08X_%s",
				tmpDir, (unsigned)GetTickCount(), base);
			HANDLE h = CreateFileW(outMidPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
				FILE_ATTRIBUTE_TEMPORARY, NULL);
			if (h != INVALID_HANDLE_VALUE) {
				DWORD wr = 0;
				WriteFile(h, fs->files[best].data, fs->files[best].size, &wr, NULL);
				CloseHandle(h);
				ok = (wr == fs->files[best].size) ? 1 : 0;
				if (!ok) { DeleteFileW(outMidPath); outMidPath[0] = 0; }
			}
		}
		CEmuZipFsClose(fs);
	}
	free(fs);
	return ok;
}

int CEmuCapturePcatMidiToFile(const wchar_t* zipPath, unsigned titleCode,
	wchar_t* outMidPath, int outCap)
{
	if (!zipPath || !outMidPath || outCap <= 0) return 0;
	outMidPath[0] = 0;

	CEmuModePrefSet(zipPath, "MIDI");
	wchar_t zipOut[CEMU_ZIP_PATH];
	char dataDir[CEMU_DATA_DIR];
	CEmuMgr* mgr = CEmuMgrGet();
	const CEmuGameEntry* ge = CEmuMgrResolveZip(mgr, zipPath, zipOut,
		(int)_countof(zipOut), dataDir, (int)sizeof(dataDir));
	/* MT-32 rows are often driver type=beep + midiout=1. ResolveZip can
	   miss prefer=MIDI if the pref hash path differs — force MIDI entry. */
	if (!ge || !CEmuModeEntryIsMidi(ge)) {
		char stem[CEMU_ARCHIVE_NAME] = {};
		const wchar_t* openZip = (zipOut[0] ? zipOut : zipPath);
		if (CEmuArchiveStemFromPath(openZip, stem, (int)sizeof(stem))) {
			const CEmuGameEntry* midGe = CEmuCatalogFindArchiveForZipMode(
				&mgr->catalog, stem, NULL, NULL, "MIDI");
			if (midGe && CEmuModeEntryIsMidi(midGe))
				ge = midGe;
		}
	}
	if (!ge || !CEmuModeEntryIsMidi(ge))
		return 0;

	const wchar_t* openZip = zipOut[0] ? zipOut : zipPath;
	CEmuZipFs* fs = (CEmuZipFs*)calloc(1, sizeof(CEmuZipFs));
	if (!fs) return 0;
	if (!CEmuZipFsOpen(fs, openZip)) { free(fs); return 0; }

	const int rate = 44100;
	CHard* hard = CEmuHardCreate(ge, rate);
	CDriver* drv = CEmuDriverCreate(ge);
	int ok = 0;
	if (hard && drv && hard->hardKind == CHard::KIND_PCAT
		&& drv->Open(hard, ge, fs, titleCode ? titleCode : 0x10)) {
		CHardPcat* hw = (CHardPcat*)hard;
		/* Reset after DOS boot so SMF is song-only. Do NOT TriggerPlay here —
		   CDriverPcat::Render fires TriggerPlay once when triggered_==0.
		   A second INT 7Fh play tears MT32/silp down to init/"THANKS" only. */
		hw->MidiCaptureReset();
		hw->MidiForceUart(1);
		/* Chunked render until the UART stream goes quiet. Do NOT key off
		   NoteOn plateau alone — sparse MT-32/SCI phrases hold notes for
		   seconds with no new NoteOns, and that used to cut mid-song (wrong
		   loop end + “wrong track” feel). Require a minimum after first
		   notes, then stop when MIDI bytes stall (re-loop hush / end). */
		enum {
			kChunk = 44100 / 2,
			kMax = 44100 * 240,
			kIdle = 44100 * 10,
			kMinAfterNotes = 44100 * 90
		};
		int16_t* buf = (int16_t*)malloc((size_t)kChunk * 4);
		if (buf) {
			int total = 0, lastBytes = 0, idle = 0, sawNotes = 0, afterNotes = 0;
			while (total < kMax) {
				const int n = (kMax - total > kChunk) ? kChunk : (kMax - total);
				drv->Render(buf, n);
				total += n;
				const int notes = (int)hw->MidiNoteOnCount();
				const int bytes = (int)hw->MidiByteCount();
				if (notes > 8) sawNotes = 1;
				if (sawNotes) afterNotes += n;
				if (bytes > lastBytes) {
					lastBytes = bytes;
					idle = 0;
				} else if (sawNotes && afterNotes >= kMinAfterNotes) {
					idle += n;
					if (idle >= kIdle) break;
				}
			}
			free(buf);
		}
		/* Setup-only captures (sysex/CC, no notes) → silent VST; reject. */
		if (hw->MidiByteCount() >= 16 && hw->MidiNoteOnCount() > 0) {
			wchar_t tmpDir[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, tmpDir);
			_snwprintf_s(outMidPath, (size_t)outCap, _TRUNCATE,
				L"%scemu_mpu_mt32_%08X.mid", tmpDir, (unsigned)GetTickCount());
			ok = hw->ExportCapturedSmf(outMidPath);
			if (!ok) { DeleteFileW(outMidPath); outMidPath[0] = 0; }
		}
		drv->Close();
	}
	if (drv) CEmuDriverDestroy(drv);
	if (hard) CEmuHardDestroy(hard);
	CEmuZipFsClose(fs);
	free(fs);
	return ok;
}
