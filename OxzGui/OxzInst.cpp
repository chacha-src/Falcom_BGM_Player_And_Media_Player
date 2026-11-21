#include "OxzInst.h"
#include "OxzId.h"
#include "OxzLang.h"
#include "OxzWork.h"
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

static bool OxzSetSz(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& value);

static void OxzSetDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD value)
{
	HKEY h = NULL;
	if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL) != ERROR_SUCCESS)
		return;
	RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
	RegCloseKey(h);
}

static void OxzStore(const wchar_t* name, const std::wstring& label, const std::wstring& command, const std::wstring& icon, int sep)
{
	std::wstring k = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell\\") + name;
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, k.c_str());
	if (sep) {
		OxzSetDword(HKEY_LOCAL_MACHINE, k.c_str(), L"CommandFlags", 8);
		return;
	}
	OxzSetSz(HKEY_LOCAL_MACHINE, k.c_str(), L"MUIVerb", label);
	OxzSetSz(HKEY_LOCAL_MACHINE, k.c_str(), L"Icon", icon);
	OxzSetSz(HKEY_LOCAL_MACHINE, (k + L"\\command").c_str(), NULL, command);
}

static void OxzCascade(const wchar_t* key, const std::wstring& label, const std::wstring& icon, const wchar_t* subs, const wchar_t* applies)
{
	OxzSetSz(HKEY_LOCAL_MACHINE, key, L"MUIVerb", label);
	OxzSetSz(HKEY_LOCAL_MACHINE, key, L"Icon", icon);
	OxzSetSz(HKEY_LOCAL_MACHINE, key, L"SubCommands", subs);
	OxzSetSz(HKEY_LOCAL_MACHINE, key, L"LegacyDisable", L"");
	if (applies) OxzSetSz(HKEY_LOCAL_MACHINE, key, L"AppliesTo", applies);
}

static bool OxzSetSz(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& value)
{
	HKEY h = NULL;
	if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL) != ERROR_SUCCESS)
		return false;
	LSTATUS st = RegSetValueExW(h, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
	RegCloseKey(h);
	return st == ERROR_SUCCESS;
}

static std::wstring OxzGuid(const GUID& g)
{
	wchar_t b[64];
	StringFromGUID2(g, b, 64);
	return b;
}

bool OxzIsAdmin()
{
	BOOL member = FALSE;
	SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
	PSID sid = NULL;
	if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid))
		return false;
	CheckTokenMembership(NULL, sid, &member);
	FreeSid(sid);
	return member ? true : false;
}

std::wstring OxzProgramDir()
{
	PWSTR p = NULL;
	std::wstring s = L"C:\\Program Files";
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &p)) && p) {
		s = p;
		CoTaskMemFree(p);
	}
	return s + L"\\OxzGui";
}

static bool OxzCopyReplace(const std::wstring& from, const std::wstring& to)
{
	if (OxzSamePath(from, to)) return true;
	std::wstring old = to + L".old";
	DeleteFileW(old.c_str());
	MoveFileExW(to.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
	if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) return false;
	MoveFileExW(old.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	return true;
}

static void OxzWriteVerbs(const std::wstring& exe)
{
	std::wstring icon = L"\"" + exe + L"\",0";
	std::wstring q = OxzQuote(exe);
	std::wstring dll = OxzParent(exe) + L"\\OxzShell.dll";
	std::wstring menu = OxzGuid(CLSID_OxzMenu);
	std::wstring drag = OxzGuid(CLSID_OxzDrag);
	std::wstring drop = OxzGuid(CLSID_OxzDrop);
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\AllFilesystemObjects\\shell\\OxzPack");
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\Directory\\Background\\shell\\OxzPack");
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive\\shell\\OxzExtractHere");
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive\\shell\\OxzExtractTo");
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\.oxz", NULL, OXZ_PROGID);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive", NULL, OxzTxt(OS_ARCHIVE_DESC));
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive\\DefaultIcon", NULL, icon);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive\\shell\\open\\command", NULL, q + L" \"%1\"");
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive\\shellex\\DropHandler", NULL, drop);
	OxzStore(L"OxzGui.Open", OxzTxt(OS_OPEN), q + L" \"%1\"", icon, 0);
	OxzStore(L"OxzGui.ExtractDlg", OxzTxt(OS_EXTRACT_DLG), q + L" /extractdlg \"%1\"", icon, 0);
	OxzStore(L"OxzGui.Here", OxzTxt(OS_SHELL_HERE), q + L" /exthere \"%1\"", icon, 0);
	OxzStore(L"OxzGui.To", OxzTxt(OS_SHELL_TO), q + L" /extractto \"%1\"", icon, 0);
	OxzStore(L"OxzGui.Test", OxzTxt(OS_TEST), q + L" /test \"%1\"", icon, 0);
	OxzStore(L"OxzGui.Sep", L"", L"", icon, 1);
	OxzStore(L"OxzGui.CompressDlg", OxzTxt(OS_COMPRESS_DLG), q + L" /editlist \"%1\"", icon, 0);
	OxzStore(L"OxzGui.Compress", OxzTxt(OS_SHELL_PACK), q + L" /packone \"%1\"", icon, 0);
	OxzStore(L"OxzGui.CompressDlgDir", OxzTxt(OS_COMPRESS_DLG), q + L" /editlist \"%V\"", icon, 0);
	OxzStore(L"OxzGui.CompressDir", OxzTxt(OS_SHELL_PACK), q + L" /packdir \"%V\"", icon, 0);
	OxzCascade(L"Software\\Classes\\AllFilesystemObjects\\shell\\Oxz", OxzTxt(OS_MENU), icon, L"OxzGui.CompressDlg;OxzGui.Compress", L"System.FileExtension:<>\".oxz\"");
	OxzCascade(L"Software\\Classes\\OxzArchive\\shell\\Oxz", OxzTxt(OS_MENU), icon, L"OxzGui.Open;OxzGui.ExtractDlg;OxzGui.Here;OxzGui.To;OxzGui.Test;OxzGui.Sep;OxzGui.CompressDlg;OxzGui.Compress", NULL);
	OxzCascade(L"Software\\Classes\\Directory\\Background\\shell\\Oxz", OxzTxt(OS_MENU), icon, L"OxzGui.CompressDlgDir;OxzGui.CompressDir", NULL);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\AllFilesystemObjects\\shellex\\ContextMenuHandlers\\Oxz", NULL, menu);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\Oxz", NULL, menu);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\Directory\\shellex\\DragDropHandlers\\Oxz", NULL, drag);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\Drive\\shellex\\DragDropHandlers\\Oxz", NULL, drag);
	OxzSetSz(HKEY_LOCAL_MACHINE, L"Software\\Classes\\Folder\\shellex\\DragDropHandlers\\Oxz", NULL, drag);
	const GUID* ids[] = { &CLSID_OxzPack, &CLSID_OxzHere, &CLSID_OxzTo, &CLSID_OxzDrop, &CLSID_OxzMenu, &CLSID_OxzDrag };
	for (int i = 0; i < 6; ++i) {
		std::wstring k = L"Software\\Classes\\CLSID\\" + OxzGuid(*ids[i]) + L"\\InprocServer32";
		OxzSetSz(HKEY_LOCAL_MACHINE, k.c_str(), NULL, dll);
		OxzSetSz(HKEY_LOCAL_MACHINE, k.c_str(), L"ThreadingModel", L"Apartment");
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

bool OxzIsInstalled()
{
	if (GetFileAttributesW((OxzProgramDir() + L"\\OxzGui.exe").c_str()) == INVALID_FILE_ATTRIBUTES)
		return false;
	wchar_t buf[80];
	DWORD cb = sizeof(buf);
	if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\OxzArchive\\shellex\\DropHandler", NULL, RRF_RT_REG_SZ, NULL, buf, &cb) != ERROR_SUCCESS)
		return false;
	return _wcsicmp(buf, OxzGuid(CLSID_OxzDrop).c_str()) == 0;
}

bool OxzInstallHere(std::wstring& err)
{
	std::wstring dest = OxzProgramDir();
	std::wstring srcExe = OxzBeside(L"OxzGui.exe");
	std::wstring srcDll = OxzBeside(L"OxzShell.dll");
	std::wstring src7 = OxzBeside(L"7za.exe");
	CreateDirectoryW(dest.c_str(), NULL);
	if (GetFileAttributesW(srcDll.c_str()) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(src7.c_str()) == INVALID_FILE_ATTRIBUTES) {
		err = OxzTxt(OS_MISSING);
		return false;
	}
	if (!OxzCopyReplace(srcExe, dest + L"\\OxzGui.exe") || !OxzCopyReplace(srcDll, dest + L"\\OxzShell.dll") || !OxzCopyReplace(src7, dest + L"\\7za.exe")) {
		err = OxzTxt(OS_INSTALL_FAIL);
		return false;
	}
	OxzWriteVerbs(dest + L"\\OxzGui.exe");
	if (!OxzIsInstalled()) {
		err = OxzTxt(OS_INSTALL_FAIL);
		return false;
	}
	return true;
}

static void OxzDeleteKey(const wchar_t* key)
{
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
}

bool OxzRemoveInstall(std::wstring& err)
{
	OxzDeleteKey(L"Software\\Classes\\.oxz");
	OxzDeleteKey(L"Software\\Classes\\OxzArchive");
	OxzDeleteKey(L"Software\\Classes\\AllFilesystemObjects\\shell\\OxzPack");
	OxzDeleteKey(L"Software\\Classes\\AllFilesystemObjects\\shell\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\AllFilesystemObjects\\shellex\\ContextMenuHandlers\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\Directory\\Background\\shell\\OxzPack");
	OxzDeleteKey(L"Software\\Classes\\Directory\\Background\\shell\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\Directory\\Background\\shellex\\ContextMenuHandlers\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\Directory\\shellex\\DragDropHandlers\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\Drive\\shellex\\DragDropHandlers\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\Folder\\shellex\\DragDropHandlers\\Oxz");
	OxzDeleteKey(L"Software\\Classes\\OxzArchive\\shell\\OxzExtractHere");
	OxzDeleteKey(L"Software\\Classes\\OxzArchive\\shell\\OxzExtractTo");
	OxzDeleteKey(L"Software\\Classes\\OxzArchive\\shell\\Oxz");
	const wchar_t* store[] = {
		L"OxzGui.Open", L"OxzGui.ExtractDlg", L"OxzGui.Here", L"OxzGui.To", L"OxzGui.Test",
		L"OxzGui.Sep", L"OxzGui.CompressDlg", L"OxzGui.Compress", L"OxzGui.CompressDlgDir", L"OxzGui.CompressDir"
	};
	for (int i = 0; i < 10; ++i) {
		std::wstring k = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CommandStore\\shell\\") + store[i];
		OxzDeleteKey(k.c_str());
	}
	OxzDeleteKey((L"Software\\Classes\\CLSID\\" + OxzGuid(CLSID_OxzPack)).c_str());
	OxzDeleteKey((L"Software\\Classes\\CLSID\\" + OxzGuid(CLSID_OxzHere)).c_str());
	OxzDeleteKey((L"Software\\Classes\\CLSID\\" + OxzGuid(CLSID_OxzTo)).c_str());
	OxzDeleteKey((L"Software\\Classes\\CLSID\\" + OxzGuid(CLSID_OxzDrop)).c_str());
	OxzDeleteKey((L"Software\\Classes\\CLSID\\" + OxzGuid(CLSID_OxzMenu)).c_str());
	OxzDeleteKey((L"Software\\Classes\\CLSID\\" + OxzGuid(CLSID_OxzDrag)).c_str());
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	std::wstring dir = OxzProgramDir();
	wchar_t self[32768];
	GetModuleFileNameW(NULL, self, 32768);
	if (OxzSamePath(OxzParent(self), dir)) {
		wchar_t temp[MAX_PATH];
		GetTempPathW(MAX_PATH, temp);
		std::wstring wipe = std::wstring(temp) + L"OxzGui_wipe.exe";
		CopyFileW(self, wipe.c_str(), FALSE);
		std::wstring cmd = OxzQuote(wipe) + L" /uninstall_wipe " + OxzQuote(dir);
		std::vector<wchar_t> buf(cmd.begin(), cmd.end());
		buf.push_back(0);
		STARTUPINFOW si;
		PROCESS_INFORMATION pi;
		memset(&si, 0, sizeof(si));
		memset(&pi, 0, sizeof(pi));
		si.cb = sizeof(si);
		if (CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			ExitProcess(0);
		}
	} else {
		OxzRmTree(dir);
	}
	err.clear();
	return true;
}

bool OxzRelaunchElevated(const wchar_t* args)
{
	wchar_t exe[32768];
	SHELLEXECUTEINFOW si;
	GetModuleFileNameW(NULL, exe, 32768);
	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SEE_MASK_NOCLOSEPROCESS;
	si.lpVerb = L"runas";
	si.lpFile = exe;
	si.lpParameters = args;
	si.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&si)) return false;
	if (si.hProcess) {
		WaitForSingleObject(si.hProcess, INFINITE);
		CloseHandle(si.hProcess);
	}
	return true;
}

void OxzRefreshMenuText()
{
	if (!OxzIsInstalled()) return;
	OxzWriteVerbs(OxzProgramDir() + L"\\OxzGui.exe");
}
