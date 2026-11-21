#include "stdafx.h"
#include "ZpaqRun.h"
#include <vector>
#include <string>

int zpaq_wmain(int argc, wchar_t** argw);

static int ZpaqRun(std::vector<std::wstring>& args)
{
	std::vector<wchar_t*> ptrs;
	ptrs.reserve(args.size());
	for (size_t i = 0; i < args.size(); ++i) {
		if (args[i].empty()) return 2;
		ptrs.push_back(&args[i][0]);
	}
	if (ptrs.empty()) return 2;
	return zpaq_wmain((int)ptrs.size(), &ptrs[0]);
}

int ZpaqPathIsW(const wchar_t* path)
{
	HANDLE h;
	char b[4];
	DWORD rd = 0;
	size_t n;
	if (!path || !path[0]) return 0;
	h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		n = wcslen(path);
		return (n >= 5 && _wcsicmp(path + n - 5, L".zpaq") == 0) ? 1 : 0;
	}
	ReadFile(h, b, 4, &rd, NULL);
	CloseHandle(h);
	return (rd == 4 && b[0] == '7' && b[1] == 'k' && b[2] == 'S' && b[3] == 't') ? 1 : 0;
}

int ZpaqExtractToDirW(const wchar_t* archive, const wchar_t* destDir)
{
	std::vector<std::wstring> args;
	if (!archive || !archive[0] || !destDir || !destDir[0]) return 2;
	CreateDirectoryW(destDir, NULL);
	args.push_back(L"zpaq");
	args.push_back(L"x");
	args.push_back(archive);
	args.push_back(L"-to");
	args.push_back(destDir);
	args.push_back(L"-force");
	args.push_back(L"-t4");
	return ZpaqRun(args);
}

int ZpaqExtractLeafW(const wchar_t* archive, const wchar_t* leaf, const wchar_t* destFile,
	unsigned long long* written, time_t* mtime)
{
	std::vector<std::wstring> args;
	WIN32_FILE_ATTRIBUTE_DATA fad;
	HANDLE h;
	FILETIME ft;
	ULARGE_INTEGER ull;
	wchar_t parent[MAX_PATH * 2];
	wchar_t* slash;
	if (written) *written = 0;
	if (mtime) *mtime = 0;
	if (!archive || !leaf || !leaf[0] || !destFile) return 0;
	_snwprintf_s(parent, _TRUNCATE, L"%s", destFile);
	slash = wcsrchr(parent, L'\\');
	if (slash) { *slash = 0; CreateDirectoryW(parent, NULL); }
	DeleteFileW(destFile);
	args.push_back(L"zpaq");
	args.push_back(L"x");
	args.push_back(archive);
	args.push_back(leaf);
	args.push_back(L"-to");
	args.push_back(destFile);
	args.push_back(L"-force");
	args.push_back(L"-t4");
	ZpaqRun(args);
	if (!GetFileAttributesExW(destFile, GetFileExInfoStandard, &fad)) return 0;
	if (written) {
		ull.LowPart = fad.nFileSizeLow;
		ull.HighPart = fad.nFileSizeHigh;
		*written = ull.QuadPart;
	}
	if (mtime) {
		h = CreateFileW(destFile, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			if (GetFileTime(h, NULL, NULL, &ft)) {
				ull.LowPart = ft.dwLowDateTime;
				ull.HighPart = ft.dwHighDateTime;
				*mtime = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
			}
			CloseHandle(h);
		}
	}
	return 1;
}
