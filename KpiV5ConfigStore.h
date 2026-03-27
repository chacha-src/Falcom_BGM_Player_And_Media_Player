#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct KpiV5ConfigEntry
{
	const wchar_t* plugin;
	const wchar_t* section;
	const wchar_t* key;
	const wchar_t* defaultValue;
};

std::wstring KpiV5PluginNameFromPath(const std::wstring& kpiPath);
std::wstring KpiV5NormalizePluginName(const std::wstring& pluginName);

bool KpiV5SetStr(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, const std::wstring& value);
std::wstring KpiV5GetStr(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue);
bool KpiV5SetBin(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, const BYTE* data, DWORD size);
DWORD KpiV5GetBin(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, BYTE* data, DWORD size);

bool KpiV5SetInt(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, INT64 value);
INT64 KpiV5GetInt(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, INT64 defaultValue);
bool KpiV5SetFloat(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, double value);
double KpiV5GetFloat(const std::wstring& pluginName, const std::wstring& section, const std::wstring& key, double defaultValue);

const std::vector<KpiV5ConfigEntry>& GetKpiV5KnownEntries();
