#pragma once
#include <windows.h>
#include <string>
#include <vector>

std::wstring OxzExeDir();
std::wstring OxzBeside(const wchar_t* name);
bool OxzHas7za();
bool OxzIsOxz(const std::wstring& path);
std::wstring OxzBaseName(const std::wstring& path);
std::wstring OxzParent(const std::wstring& path);
std::wstring OxzQuote(const std::wstring& s);
std::wstring OxzTempList();
bool OxzWriteList(const std::wstring& file, const std::vector<std::wstring>& paths);
bool OxzReadList(const std::wstring& file, std::vector<std::wstring>& out);
std::wstring OxzNewWork();
void OxzRmTree(const std::wstring& dir);
void OxzCopyTree(const std::wstring& src, const std::wstring& dest);
void OxzImportOne(const std::wstring& src, const std::wstring& destDir);
std::wstring OxzDestFor(const std::vector<std::wstring>& paths);
bool OxzSamePath(const std::wstring& a, const std::wstring& b);

struct OxzSnap {
	wchar_t archive[520];
	wchar_t file[1024];
	int filePct;
	int overall;
	unsigned index;
	unsigned total;
	DWORD startTick;
};

void OxzProgBegin(const wchar_t* archive);
void OxzProgArchive(const wchar_t* archive);
void OxzProgFile(const wchar_t* file, unsigned index, unsigned total, int filePct, int overall);
void OxzProgRead(OxzSnap* out);

int OxzExtractTo(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base = 0, int span = 100);
/* zip など、RIP を戻さない展開。 */
int OxzExtractPlain(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base = 0, int span = 100);
int OxzTest(const wchar_t* arc, std::wstring& err, int base = 0, int span = 100);
int OxzPackWork(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base = 0, int span = 100);
/* 既存の書庫があれば先に展開し、inputs を足して（同名は上書き）dest へ圧縮する。 */
int OxzMergePack(const std::wstring& existing, const std::vector<std::wstring>& inputs, const std::wstring& dest, unsigned long long& bytes, std::wstring& err);
/* 開いている作業フォルダへ追加または上書きし、その場で書庫へ保存する。 */
int OxzSaveAdded(const std::wstring& work, const std::wstring& destDir, const std::vector<std::wstring>& inputs, const std::wstring& archive, unsigned long long& bytes, std::wstring& err);
