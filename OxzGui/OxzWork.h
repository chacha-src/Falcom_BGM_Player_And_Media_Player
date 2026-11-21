#pragma once
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
int OxzExtractTo(const wchar_t* arc, const wchar_t* dest, std::wstring& err);
int OxzTest(const wchar_t* arc, std::wstring& err);
int OxzPackWork(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err);
/* 既存の書庫があれば先に展開し、inputs を足して（同名は上書き）dest へ圧縮する。 */
int OxzMergePack(const std::wstring& existing, const std::vector<std::wstring>& inputs, const std::wstring& dest, unsigned long long& bytes, std::wstring& err);
