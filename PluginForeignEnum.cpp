// 外部プラグイン列挙（exe 配下を KPI と同じ再帰で走査）
// 候補: in_*.dll / xmp-*.dll / 名前に aimp / または PE に入力系エクスポートがある DLL
#include "stdafx.h"
#include "PluginKinds.h"
#include "PluginWinamp.h"
#include "PluginXmplay.h"
#include "PluginAimp.h"
#include "PluginForeignEnum.h"

extern BYTE plugkind[];
extern BYTE kpiarch[];
extern int kpicnt;

static WORD ForeignGetPeMachine(const CString& path)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite, NULL)) return 0;
	IMAGE_DOS_HEADER dos{};
	if (f.Read(&dos, sizeof(dos)) != sizeof(dos)) return 0;
	if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
	f.Seek(dos.e_lfanew, CFile::begin);
	DWORD peSig = 0;
	if (f.Read(&peSig, sizeof(peSig)) != sizeof(peSig)) return 0;
	if (peSig != IMAGE_NT_SIGNATURE) return 0;
	IMAGE_FILE_HEADER fileHdr{};
	if (f.Read(&fileHdr, sizeof(fileHdr)) != sizeof(fileHdr)) return 0;
	return fileHdr.Machine;
}

static int ForeignIs64(const CString& path)
{
	WORD m = ForeignGetPeMachine(path);
	return (m == IMAGE_FILE_MACHINE_AMD64 || m == IMAGE_FILE_MACHINE_ARM64) ? 1 : 0;
}

static int ForeignLooksWinampName(const CString& name)
{
	CString n = name; n.MakeLower();
	return (n.Left(3) == L"in_" && n.Right(4) == L".dll") ? 1 : 0;
}

static int ForeignLooksXmplayName(const CString& name)
{
	CString n = name; n.MakeLower();
	return (n.Left(4) == L"xmp-" && n.Right(4) == L".dll") ? 1 : 0;
}

static int ForeignLooksAimpName(const CString& name)
{
	CString n = name; n.MakeLower();
	if (n.Right(4) != L".dll") return 0;
	if (n.Find(L"aimp") >= 0) return 1;
	return 0;
}

static const BYTE* ForeignRvaToPtr(const BYTE* base, const IMAGE_NT_HEADERS* nt, DWORD rva)
{
	const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
		DWORD va = sec[i].VirtualAddress;
		DWORD sz = sec[i].Misc.VirtualSize;
		if (sz == 0) sz = sec[i].SizeOfRawData;
		if (rva >= va && rva < va + sz)
			return base + sec[i].PointerToRawData + (rva - va);
	}
	return NULL;
}

enum {
	FOREIGN_HINT_WINAMP = 1,
	FOREIGN_HINT_XMPLAY = 2,
	FOREIGN_HINT_AIMP = 4
};

// LoadLibrary せず PE のエクスポート名だけ見る（どこ置きでも安全に候補判定）
static int ForeignPeExportHints(const CString& path)
{
	HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return 0;
	HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!hMap) { CloseHandle(hFile); return 0; }
	const BYTE* base = (const BYTE*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (!base) { CloseHandle(hMap); CloseHandle(hFile); return 0; }

	int hints = 0;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
	if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0) {
		const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
		if (nt->Signature == IMAGE_NT_SIGNATURE) {
			DWORD expRva = 0;
			if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
				const IMAGE_NT_HEADERS32* n32 = (const IMAGE_NT_HEADERS32*)nt;
				if (n32->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXPORT)
					expRva = n32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
			} else if (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
				const IMAGE_NT_HEADERS64* n64 = (const IMAGE_NT_HEADERS64*)nt;
				if (n64->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXPORT)
					expRva = n64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
			}
			if (expRva) {
				const IMAGE_EXPORT_DIRECTORY* exp = (const IMAGE_EXPORT_DIRECTORY*)ForeignRvaToPtr(base, nt, expRva);
				if (exp && exp->NumberOfNames > 0) {
					const DWORD* names = (const DWORD*)ForeignRvaToPtr(base, nt, exp->AddressOfNames);
					if (names) {
						for (DWORD i = 0; i < exp->NumberOfNames; i++) {
							const char* nm = (const char*)ForeignRvaToPtr(base, nt, names[i]);
							if (!nm) continue;
							if (strcmp(nm, "winampGetInModule2") == 0) hints |= FOREIGN_HINT_WINAMP;
							else if (strcmp(nm, "XMPIN_GetInterface") == 0) hints |= FOREIGN_HINT_XMPLAY;
							else if (strcmp(nm, "AIMPPluginGetHeader") == 0) hints |= FOREIGN_HINT_AIMP;
							if ((hints & (FOREIGN_HINT_WINAMP | FOREIGN_HINT_XMPLAY | FOREIGN_HINT_AIMP)) ==
								(FOREIGN_HINT_WINAMP | FOREIGN_HINT_XMPLAY | FOREIGN_HINT_AIMP))
								break;
						}
					}
				}
			}
		}
	}

	UnmapViewOfFile(base);
	CloseHandle(hMap);
	CloseHandle(hFile);
	return hints;
}

static int ForeignProbeHints(const CString& name, const CString& path)
{
	int hints = 0;
	if (ForeignLooksWinampName(name)) hints |= FOREIGN_HINT_WINAMP;
	if (ForeignLooksXmplayName(name)) hints |= FOREIGN_HINT_XMPLAY;
	if (ForeignLooksAimpName(name)) hints |= FOREIGN_HINT_AIMP;
	// 名前が無くても、どこに置いても PE エクスポートで拾う（フォルダ名は不要）
	if (hints == 0)
		hints = ForeignPeExportHints(path);
	return hints;
}

int PluginForeign_CountCandidatesInDir(const CString& dirPath)
{
	int count = 0;
	CFileFind f;
	CString pat = dirPath;
	if (!pat.IsEmpty() && pat.Right(1) != L"\\" && pat.Right(1) != L"/")
		pat += L"\\";
	pat += L"*.dll";
	if (!f.FindFile(pat)) return 0;
	int b = 1, c = 1;
	do {
		if (c) b = f.FindNextFile();
		c = 1;
		if (f.IsDirectory()) continue;
		CString name = f.GetFileName();
		CString path = f.GetFilePath();
		if (name.IsEmpty() || name == L"." || name == L"..") continue;
		if (ForeignProbeHints(name, path) != 0)
			count++;
	} while (b);
	f.Close();
	return count;
}

void PluginForeign_EnumInDir(const CString& dirPath)
{
	CFileFind f;
	CString pat = dirPath;
	if (!pat.IsEmpty() && pat.Right(1) != L"\\" && pat.Right(1) != L"/")
		pat += L"\\";
	pat += L"*.dll";
	if (!f.FindFile(pat)) return;
	int b = 1, c = 1;
	do {
		if (c) b = f.FindNextFile();
		c = 1;
		if (f.IsDirectory()) continue;
		CString name = f.GetFileName();
		CString path = f.GetFilePath();
		if (name.IsEmpty() || name == L"." || name == L"..") continue;
		const int hints = ForeignProbeHints(name, path);
		if (hints == 0) continue;
		const int is64 = ForeignIs64(path);
		int tried = 0;
		if ((hints & FOREIGN_HINT_WINAMP) && !tried)
			tried = PluginWinamp_TryEnum(path, is64);
		if ((hints & FOREIGN_HINT_XMPLAY) && !tried)
			tried = PluginXmplay_TryEnum(path, is64);
		if ((hints & FOREIGN_HINT_AIMP) && !tried)
			tried = PluginAimp_TryEnum(path, is64);
		(void)tried;
		OggPluginLoadOnOneFileDone();
	} while (b);
	f.Close();
}
