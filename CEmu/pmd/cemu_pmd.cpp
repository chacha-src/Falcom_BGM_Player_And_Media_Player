/* Embedded PMDWin player. Compiled MBCS to match PMDWIN TCHAR=char. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../cemu_zipfs.h"
#include "../cemu_rhythm.h"
#include "../fmmon/cemu_fmmon_bind.h"
#include "cemu_pmd.h"
#include "pmdwincore.h"

PMDWIN* g_pmdwinFmMonActive = NULL;

static LONG s_pmdTempSeq = 0;

static const char* CEmuPmdBaseName(const char* name)
{
	const char* base = name ? name : "";
	for (const char* p = base; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':')
			base = p + 1;
	}
	return base;
}

static int CEmuPmdNameLooksPmd(const char* name)
{
	if (!name || !name[0]) return 0;
	const char* base = CEmuPmdBaseName(name);
	while (*base == '#' || *base == ' ' || *base == '\t')
		++base;
	return _strnicmp(base, "PMD", 3) == 0;
}

int CEmuPmdGeLooks(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (CEmuPmdNameLooksPmd(ge->archive))
		return 1;
	for (int i = 0; i < ge->romCount; i++) {
		if (CEmuPmdNameLooksPmd(ge->rom[i].name))
			return 1;
	}
	return 0;
}

static int CEmuPmdIsEngineName(const char* name)
{
	const char* base = CEmuPmdBaseName(name);
	const char* ext = strrchr(base, '.');
	if (!ext) return 0;
	return _stricmp(ext, ".EXE") == 0
		|| _stricmp(ext, ".COM") == 0
		|| _stricmp(ext, ".DRV") == 0
		|| _stricmp(ext, ".SYS") == 0
		|| _stricmp(ext, ".BAT") == 0;
}

static int CEmuPmdIsSongName(const char* name)
{
	const char* base = CEmuPmdBaseName(name);
	const char* ext = strrchr(base, '.');
	if (!ext) return 0;
	return _stricmp(ext, ".M") == 0
		|| _stricmp(ext, ".M2") == 0
		|| _stricmp(ext, ".M86") == 0;
}

static int CEmuPmdIsPcmName(const char* name)
{
	const char* base = CEmuPmdBaseName(name);
	const char* ext = strrchr(base, '.');
	if (!ext) return 0;
	return _stricmp(ext, ".PPC") == 0
		|| _stricmp(ext, ".PVI") == 0
		|| _stricmp(ext, ".P86") == 0
		|| _stricmp(ext, ".PPS") == 0
		|| _stricmp(ext, ".PZI") == 0
		|| _stricmp(ext, ".PCM") == 0;
}

static int CEmuPmdWideToA(const wchar_t* w, char* out, int cap)
{
	if (!out || cap <= 0) return 0;
	out[0] = 0;
	if (!w || !w[0]) return 0;
	int n = WideCharToMultiByte(CP_ACP, 0, w, -1, out, cap, NULL, NULL);
	if (n <= 0) {
		out[0] = 0;
		return 0;
	}
	return 1;
}

static void CEmuPmdWipeDir(const wchar_t* dir)
{
	if (!dir || !dir[0]) return;
	wchar_t spec[MAX_PATH];
	_snwprintf_s(spec, _TRUNCATE, L"%s\\*.*", dir);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(spec, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		RemoveDirectoryW(dir);
		return;
	}
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0
			|| (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		wchar_t child[MAX_PATH];
		_snwprintf_s(child, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			CEmuPmdWipeDir(child);
		else
			DeleteFileW(child);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	RemoveDirectoryW(dir);
}

static int CEmuPmdWriteFileW(const wchar_t* path, const unsigned char* data, unsigned size)
{
	if (!path || !data || !size) return 0;
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD wr = 0;
	BOOL ok = WriteFile(h, data, size, &wr, NULL);
	CloseHandle(h);
	return ok && wr == size;
}

static int CEmuPmdRhythmExists(const wchar_t* dir)
{
	if (!dir || !dir[0]) return 0;
	static const wchar_t* kNames[] = {
		L"2608_BD.WAV", L"2608_bd.wav", L"2608_HH.WAV", L"2608_hh.wav",
		NULL
	};
	for (int i = 0; kNames[i]; i++) {
		wchar_t p[MAX_PATH];
		_snwprintf_s(p, _TRUNCATE, L"%s\\%s", dir, kNames[i]);
		if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
			return 1;
	}
	return 0;
}

static int CEmuPmdDirHasRhythm(const wchar_t* dir)
{
	if (CEmuPmdRhythmExists(dir))
		return 1;
	if (!dir || !dir[0]) return 0;
	wchar_t p[MAX_PATH];
	_snwprintf_s(p, _TRUNCATE, L"%s\\ym2608_adpcm_rom.bin", dir);
	return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static int CEmuPmdFindRhythmDirA(char* outA, int cap)
{
	if (!outA || cap <= 0) return 0;
	outA[0] = 0;
	static char s_cached[MAX_PATH];
	static LONG s_once = 0;
	if (s_once && s_cached[0]) {
		strncpy_s(outA, (size_t)cap, s_cached, _TRUNCATE);
		return 1;
	}
	wchar_t rhy[MAX_PATH] = {};
	GetRhythmPath(rhy, MAX_PATH);
	size_t n = wcslen(rhy);
	while (n > 0 && (rhy[n - 1] == L'\\' || rhy[n - 1] == L'/'))
		rhy[--n] = 0;
	if (CEmuPmdDirHasRhythm(rhy) && CEmuPmdWideToA(rhy, outA, cap)) {
		strncpy_s(s_cached, outA, _TRUNCATE);
		s_once = 1;
		return 1;
	}

	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	wchar_t* slash = wcsrchr(exe, L'\\');
	if (slash) *slash = 0;
	wchar_t cands[6][MAX_PATH];
	int nc = 0;
	if (exe[0]) {
		wcsncpy_s(cands[nc++], exe, _TRUNCATE);
		_snwprintf_s(cands[nc++], _TRUNCATE, L"%s\\Plugins\\Kobarin\\fmpmd", exe);
		_snwprintf_s(cands[nc++], _TRUNCATE, L"%s\\Plugins\\Kobarin\\fmpmd\\Rhythm", exe);
		_snwprintf_s(cands[nc++], _TRUNCATE, L"%s\\Plugins\\kbsasami", exe);
	}
	for (int i = 0; i < nc; i++) {
		if (!CEmuPmdDirHasRhythm(cands[i]))
			continue;
		if (!CEmuPmdWideToA(cands[i], outA, cap))
			continue;
		strncpy_s(s_cached, outA, _TRUNCATE);
		s_once = 1;
		return 1;
	}
	return 0;
}

static const char* CEmuPmdPickSong(const CEmuGameEntry* ge, unsigned titleCode,
	const CEmuZipFs* fs, char* found, int foundCap)
{
	if (found && foundCap > 0) found[0] = 0;
	if (ge) {
		const int low = (int)(titleCode & 0xff);
		const int full = (int)titleCode;
		const int hi = (int)((titleCode >> 8) & 0xff);
		for (int pass = 0; pass < 3; pass++) {
			const int want = pass == 0 ? full : (pass == 1 ? low : hi);
			if (pass == 2 && (hi == 0 || hi == low || titleCode <= 0xffu))
				continue;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0)
					continue;
				if (r->offset != want) continue;
				const char* base = CEmuPmdBaseName(r->name);
				if (CEmuPmdIsEngineName(base) || !CEmuPmdIsSongName(base))
					continue;
				if (found && foundCap > 0)
					strncpy_s(found, (size_t)foundCap, base, _TRUNCATE);
				return found && found[0] ? found : base;
			}
		}
	}
	if (!fs) return NULL;
	int songIdx = 0;
	const int wantIdx = (int)(titleCode & 0xff);
	char first[CEMU_ROM_NAME] = {};
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const char* base = CEmuPmdBaseName(pathA);
		if (!CEmuPmdIsSongName(base)) continue;
		if (!first[0])
			strncpy_s(first, base, _TRUNCATE);
		if (wantIdx && songIdx == wantIdx) {
			if (found && foundCap > 0)
				strncpy_s(found, (size_t)foundCap, base, _TRUNCATE);
			return found && found[0] ? found : base;
		}
		songIdx++;
	}
	if (first[0] && found && foundCap > 0) {
		strncpy_s(found, (size_t)foundCap, first, _TRUNCATE);
		return found;
	}
	return first[0] ? first : NULL;
}

static int CEmuPmdZipHasP86(const CEmuZipFs* fs)
{
	if (!fs) return 0;
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const char* base = CEmuPmdBaseName(pathA);
		if (CEmuPmdNameLooksPmd(base) && strstr(base, "86"))
			return 1;
		const char* ext = strrchr(base, '.');
		if (ext && _stricmp(ext, ".P86") == 0)
			return 1;
	}
	return 0;
}

static int CEmuPmdExtractNeeded(CEmuPmdPlayer* p, const CEmuZipFs* fs, const char* songName)
{
	if (!p || !fs || !p->tempDir[0]) return 0;
	int wrote = 0;
	for (int i = 0; i < fs->fileCount; i++) {
		const CEmuZipFile* z = &fs->files[i];
		if (!z->data || !z->size) continue;
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(CP_ACP, 0, z->path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const char* base = CEmuPmdBaseName(pathA);
		if (!base[0] || CEmuPmdIsEngineName(base)) continue;
		const int wantSong = songName && songName[0] && _stricmp(base, songName) == 0;
		if (!wantSong && !CEmuPmdIsPcmName(base))
			continue;
		if (z->size > 16u * 1024u * 1024u) continue;
		wchar_t out[MAX_PATH];
		wchar_t wbase[MAX_PATH];
		MultiByteToWideChar(CP_ACP, 0, base, -1, wbase, MAX_PATH);
		_snwprintf_s(out, _TRUNCATE, L"%s\\%s", p->tempDir, wbase);
		if (CEmuPmdWriteFileW(out, z->data, z->size))
			wrote++;
	}
	return wrote;
}

void CEmuPmdInit(CEmuPmdPlayer* p)
{
	if (!p) return;
	memset(p, 0, sizeof(*p));
}

void CEmuPmdClose(CEmuPmdPlayer* p)
{
	if (!p) return;
	if (p->impl) {
		PMDWIN* win = (PMDWIN*)p->impl;
		if (g_pmdwinFmMonActive == win)
			g_pmdwinFmMonActive = NULL;
		win->music_stop();
		delete win;
		p->impl = NULL;
	}
	if (p->tempDir[0]) {
		CEmuPmdWipeDir(p->tempDir);
		p->tempDir[0] = 0;
	}
	p->open = 0;
	p->curSample = 0;
	p->endSample = 0;
}

int CEmuPmdOpen(CEmuPmdPlayer* p, const CEmuGameEntry* ge, const wchar_t* zipPath,
	unsigned titleCode, DWORD sampleRate, CEmuZipFs* fsIn)
{
	if (!p || !zipPath || !zipPath[0]) return 0;
	if (!CEmuPmdGeLooks(ge)) return 0;
	CEmuPmdClose(p);

	CEmuZipFs localFs;
	memset(&localFs, 0, sizeof(localFs));
	CEmuZipFs* fs = fsIn;
	int ownFs = 0;
	if (!fs || fs->fileCount <= 0 || fs->namesOnly) {
		if (!CEmuZipFsOpen(&localFs, zipPath))
			return 0;
		fs = &localFs;
		ownFs = 1;
	}

	wchar_t tmpRoot[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tmpRoot);
	const LONG id = InterlockedIncrement(&s_pmdTempSeq);
	_snwprintf_s(p->tempDir, _TRUNCATE, L"%sogg_cemu_pmd\\%u_%ld",
		tmpRoot, GetCurrentProcessId(), id);
	{
		wchar_t parent[MAX_PATH];
		_snwprintf_s(parent, _TRUNCATE, L"%sogg_cemu_pmd", tmpRoot);
		CreateDirectoryW(parent, NULL);
	}
	CreateDirectoryW(p->tempDir, NULL);

	char song[CEMU_ROM_NAME] = {};
	const char* songName = CEmuPmdPickSong(ge, titleCode, fs, song, (int)sizeof(song));
	const int pmd86 = CEmuPmdZipHasP86(fs);
	if (!songName || !songName[0] || !CEmuPmdExtractNeeded(p, fs, songName)) {
		if (ownFs) CEmuZipFsClose(fs);
		CEmuPmdClose(p);
		return 0;
	}
	if (ownFs)
		CEmuZipFsClose(fs);

	char songPathA[MAX_PATH * 2] = {};
	char dirA[MAX_PATH] = {};
	if (!CEmuPmdWideToA(p->tempDir, dirA, (int)sizeof(dirA))) {
		CEmuPmdClose(p);
		return 0;
	}
	_snprintf_s(songPathA, _TRUNCATE, "%s\\%s", dirA, songName);
	if (GetFileAttributesA(songPathA) == INVALID_FILE_ATTRIBUTES) {
		CEmuPmdClose(p);
		return 0;
	}

	PMDWIN* win = new PMDWIN();
	if (!win) {
		CEmuPmdClose(p);
		return 0;
	}

	char rhyA[MAX_PATH] = {};
	CEmuPmdFindRhythmDirA(rhyA, (int)sizeof(rhyA));
	if (!win->init(rhyA[0] ? rhyA : dirA)) {
		delete win;
		CEmuPmdClose(p);
		return 0;
	}

	TCHAR* pcmDirs[2] = { dirA, NULL };
	win->setpcmdir(pcmDirs);

	/* OPNA は ~55 kHz。DS が 96k/192k でもここで回すと Mix が重い。
	   48k 以下に落とす（192k は 4 倍整数、アップスケーラ任せ）。 */
	int rate = sampleRate ? (int)sampleRate : 44100;
	if (rate < 8000)
		rate = 44100;
	if (rate > 48000) {
		if ((rate % 48000) == 0)
			rate = 48000;
		else if ((rate % 44100) == 0)
			rate = 44100;
		else
			rate = 48000;
	}
	win->setpcmrate(rate);
	win->setppzrate(rate);
	win->setppsinterpolation(true);
	win->setp86interpolation(true);
	win->setppzinterpolation(true);
	win->setppsuse(true);
	win->setrhythmwithssgeffect(false);
	win->setpmd86pcmmode(pmd86 ? true : false);
	/* 55kHz sinc (128-tap) is far too heavy for realtime CEmu mix. Native rate. */
	win->setfmcalc55k(false);
	win->setfmwait(0);
	win->setssgwait(0);
	win->setrhythmwait(0);
	win->setadpcmwait(0);

	/* getlength reloads the file and walks the sequence once; skip the extra music_load. */
	int lengthMs = 0, loopMs = 0;
	int ret = PMDWIN_OK;
	if (win->getlength(songPathA, &lengthMs, &loopMs) && lengthMs > 0) {
		p->endSample = (UINT64)lengthMs * (UINT64)rate / 1000ull;
		ret = PMDWIN_OK;
	} else {
		ret = win->music_load(songPathA);
		p->endSample = 0;
	}
	if (ret == ERR_OPEN_PPC_FILE || ret == ERR_WRONG_PPC_FILE
		|| ret == ERR_OPEN_PPS_FILE || ret == ERR_OPEN_P86_FILE
		|| ret == ERR_OPEN_PPZ1_FILE || ret == ERR_OPEN_PPZ2_FILE
		|| ret == ERR_WRONG_PPZ1_FILE || ret == ERR_WRONG_PPZ2_FILE
		|| ret == ERR_WRONG_P86_FILE || ret == ERR_WRONG_PPS_FILE) {
		ret = PMDWIN_OK;
	}
	if (ret != PMDWIN_OK
		&& ret != WARNING_PPC_ALREADY_LOAD
		&& ret != WARNING_PPS_ALREADY_LOAD
		&& ret != WARNING_P86_ALREADY_LOAD
		&& ret != WARNING_PPZ1_ALREADY_LOAD
		&& ret != WARNING_PPZ2_ALREADY_LOAD) {
		delete win;
		CEmuPmdClose(p);
		return 0;
	}
	win->music_start();

	p->impl = win;
	p->sampleRate = (DWORD)rate;
	p->curSample = 0;
	p->open = 1;
	CEmuFmMonBeginOpen(ge, zipPath, rate);
	CEmuFmMonBindFromGe(ge);
	return 1;
}

int CEmuPmdSeek(CEmuPmdPlayer* p, UINT64 sample, DWORD flags)
{
	(void)flags;
	if (!p || !p->impl || !p->open) return 0;
	PMDWIN* win = (PMDWIN*)p->impl;
	const int rate = p->sampleRate ? (int)p->sampleRate : 44100;
	int ms = (int)(sample * 1000ull / (UINT64)rate);
	if (ms < 0) ms = 0;
	win->setpos(ms);
	p->curSample = sample;
	return 1;
}

int CEmuPmdRender(CEmuPmdPlayer* p, short* outStereo, int sampleFrames)
{
	if (!p || !p->impl || !p->open || !outStereo || sampleFrames <= 0)
		return 0;
	PMDWIN* win = (PMDWIN*)p->impl;
	win->getpcmdata((int16_t*)outStereo, sampleFrames);
	p->curSample += (UINT64)sampleFrames;
	return sampleFrames;
}

UINT64 CEmuPmdLengthSamples(const CEmuPmdPlayer* p)
{
	return p ? p->endSample : 0;
}
