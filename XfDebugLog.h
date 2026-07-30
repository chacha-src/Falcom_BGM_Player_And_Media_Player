#pragma once
// 一時診断ログ（問題特定用）。%TEMP%\ogg_xf_seek.log に追記。
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

inline void XfDbg(const char* msg)
{
	if (!msg) return;
	wchar_t path[MAX_PATH];
	DWORD n = GetTempPathW(MAX_PATH, path);
	if (n == 0 || n >= MAX_PATH) return;
	wchar_t file[MAX_PATH];
	if (wcscpy_s(file, path) != 0) return;
	if (wcscat_s(file, L"ogg_xf_seek.log") != 0) return;
	HANDLE h = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	char line[1024];
	int len = _snprintf_s(line, _TRUNCATE, "%02d:%02d:%02d.%03d %s\r\n",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
	if (len > 0) {
		DWORD w = 0;
		WriteFile(h, line, (DWORD)len, &w, NULL);
	}
	CloseHandle(h);
}

inline void XfDbgF(const char* fmt, ...)
{
	char buf[900];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, _TRUNCATE, fmt, ap);
	va_end(ap);
	XfDbg(buf);
}
