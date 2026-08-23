// OfflineHelp.cpp - CHM オフラインヘルプの配置確保と表示
#include "stdafx.h"
#include "OfflineHelp.h"
#include "UpdateCheck.h"
#include <htmlhelp.h>
#include <wininet.h>
#include <ShlObj.h>
#include <process.h>
#include <ctime>

#pragma comment(lib, "htmlhelp.lib")
#pragma comment(lib, "wininet.lib")

#include "minizip/unzip.h"
#ifdef USEWIN32IOAPI
#include "minizip/iowin32.h"
#endif

static const TCHAR* kUpdateUrlPrimary = _T("https://ppp.oohara.jp/download/oggYSEDbgm09a_uni_avx2_VC2026.zip");
static const TCHAR* kUpdateUrlFallback = _T("https://ppp.oohara.jp/download/oggYSEDbgm08g_uni_avx2_VC2026.zip");
static const ULONGLONG kChmMinBytes = 20000ULL;
static const ULONGLONG kZipMinBytes = 200000ULL;

static volatile LONG g_chmEnsureRunning = 0;

static CString OfflineHelpExeDir()
{
	TCHAR exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);
	CString dir(exePath);
	const int slash = dir.ReverseFind(_T('\\'));
	if (slash >= 0)
		dir = dir.Left(slash);
	return dir;
}

CString OfflineHelpGetChmPath()
{
	CString path = OfflineHelpExeDir();
	path += _T('\\');
	path += OFFLINE_HELP_CHM_NAME;
	return path;
}

static bool OfflineHelpFileLooksOk(LPCTSTR path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
		return false;
	ULARGE_INTEGER ull;
	ull.LowPart = fad.nFileSizeLow;
	ull.HighPart = fad.nFileSizeHigh;
	if (ull.QuadPart < kChmMinBytes)
		return false;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	unsigned char sig[4] = { 0 };
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, sig, 4, &rd, NULL);
	CloseHandle(h);
	// ITSF = CHM signature
	return ok && rd == 4 && sig[0] == 'I' && sig[1] == 'T' && sig[2] == 'S' && sig[3] == 'F';
}

static bool OfflineHelpCopyFile(LPCTSTR src, LPCTSTR dst)
{
	if (!OfflineHelpFileLooksOk(src))
		return false;
	CreateDirectory(OfflineHelpExeDir(), NULL);
	if (!CopyFile(src, dst, FALSE))
		return false;
	return OfflineHelpFileLooksOk(dst);
}

static bool OfflineHelpTryCopyFromTempDirs(const CString& destPath)
{
	TCHAR tempPath[MAX_PATH] = { 0 };
	GetTempPath(MAX_PATH, tempPath);

	CString candidates[4];
	candidates[0].Format(_T("%sogg_update_extract\\%s"), tempPath, OFFLINE_HELP_CHM_NAME);
	candidates[1].Format(_T("%sogg_update\\%s"), tempPath, OFFLINE_HELP_CHM_NAME);
	candidates[2].Format(_T("%s\\oggYSEDbgm_update\\%s"), tempPath, OFFLINE_HELP_CHM_NAME);

	PWSTR downloadsW = NULL;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &downloadsW)) && downloadsW)
	{
		candidates[3].Format(_T("%s\\oggYSEDbgm_update\\%s"), downloadsW, OFFLINE_HELP_CHM_NAME);
		CoTaskMemFree(downloadsW);
	}

	for (int i = 0; i < 4; ++i)
	{
		if (candidates[i].IsEmpty())
			continue;
		if (OfflineHelpCopyFile(candidates[i], destPath))
			return true;
	}
	return false;
}

// UpdateCheck と同じ ZIP URL 解決（簡易: HEAD で存在確認）
static CString OfflineHelpResolveZipUrl()
{
	HINTERNET hInternet = InternetOpen(_T("oggOfflineHelp/1.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInternet)
		return CString();

	DWORD timeout = 5000;
	InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

	const TCHAR* urls[2] = { kUpdateUrlPrimary, kUpdateUrlFallback };
	CString found;
	for (int i = 0; i < 2; ++i)
	{
		CString noCache;
		noCache.Format(_T("%s?t=%lld"), urls[i], (long long)time(NULL));
		DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_SECURE;
		HINTERNET hConnect = InternetOpenUrl(hInternet, noCache, NULL, 0, flags, 0);
		if (!hConnect)
			continue;
		DWORD statusCode = 0;
		DWORD statusCodeLen = sizeof(statusCode);
		const BOOL ok = HttpQueryInfo(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
			&statusCode, &statusCodeLen, NULL);
		InternetCloseHandle(hConnect);
		if (ok && statusCode == 200)
		{
			found = urls[i];
			break;
		}
	}
	InternetCloseHandle(hInternet);
	return found;
}

static bool OfflineHelpHttpDownload(const CString& url, const CString& localPath)
{
	DeleteFile(localPath);
	HINTERNET hInternet = InternetOpen(_T("oggOfflineHelp/1.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInternet)
		return false;

	DWORD timeout = 120000;
	InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	CString noCache;
	noCache.Format(_T("%s?t=%lld"), (LPCTSTR)url, (long long)time(NULL));
	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
	if (url.Find(_T("https://")) == 0)
		flags |= INTERNET_FLAG_SECURE;

	HINTERNET hConnect = InternetOpenUrl(hInternet, noCache, NULL, 0, flags, 0);
	if (!hConnect)
	{
		InternetCloseHandle(hInternet);
		return false;
	}

	DWORD statusCode = 0;
	DWORD statusCodeLen = sizeof(statusCode);
	if (!HttpQueryInfo(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeLen, NULL)
		|| statusCode != 200)
	{
		InternetCloseHandle(hConnect);
		InternetCloseHandle(hInternet);
		return false;
	}

	CFile f;
	if (!f.Open(localPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
	{
		InternetCloseHandle(hConnect);
		InternetCloseHandle(hInternet);
		return false;
	}

	char buf[8192];
	DWORD bytesRead = 0;
	ULONGLONG total = 0;
	BOOL readOk = TRUE;
	while ((readOk = InternetReadFile(hConnect, buf, sizeof(buf), &bytesRead)) != FALSE && bytesRead > 0)
	{
		try {
			f.Write(buf, (UINT)bytesRead);
		}
		catch (CFileException* e) {
			e->Delete();
			readOk = FALSE;
			break;
		}
		total += bytesRead;
	}
	f.Close();
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);

	if (!readOk || total < kZipMinBytes)
	{
		DeleteFile(localPath);
		return false;
	}
	return true;
}

static bool OfflineHelpExtractChmFromZip(const CString& zipPath, const CString& destPath)
{
	zlib_filefunc64_def ffunc;
#ifdef USEWIN32IOAPI
	fill_win32_filefunc64W(&ffunc);
#endif
	unzFile uf;
#ifdef USEWIN32IOAPI
	uf = unzOpen2_64(zipPath, &ffunc);
#else
	uf = unzOpen64(CT2A(zipPath));
#endif
	if (!uf)
		return false;

	unz_global_info64 gi;
	if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK)
	{
		unzClose(uf);
		return false;
	}

	bool bFound = false;
	for (ZPOS64_T i = 0; i < gi.number_entry; i++)
	{
		char filename_inzip[1024];
		unz_file_info64 fi;
		if (unzGetCurrentFileInfo64(uf, &fi, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK)
			break;

		const char* p = filename_inzip;
		const char* fileNameOnly = filename_inzip;
		while (*p)
		{
			if (*p == '/' || *p == '\\')
				fileNameOnly = p + 1;
			p++;
		}
		if (*fileNameOnly == '\0')
		{
			unzGoToNextFile(uf);
			continue;
		}

		CString currentFileName = CA2T(fileNameOnly, CP_UTF8);
		if (currentFileName.CompareNoCase(OFFLINE_HELP_CHM_NAME) != 0)
		{
			if ((ZPOS64_T)(i + 1) < gi.number_entry)
				unzGoToNextFile(uf);
			continue;
		}

		if (unzOpenCurrentFile(uf) != UNZ_OK)
			break;

		CString tmpPath = destPath + _T(".part");
		DeleteFile(tmpPath);
		CFile outFile;
		bool writeOk = false;
		ULONGLONG written = 0;
		if (outFile.Open(tmpPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
		{
			char buf[8192];
			int n;
			writeOk = true;
			while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0)
			{
				try {
					outFile.Write(buf, (UINT)n);
				}
				catch (CFileException* e) {
					e->Delete();
					writeOk = false;
					break;
				}
				written += (ULONGLONG)n;
			}
			if (n < 0)
				writeOk = false;
			outFile.Close();
		}
		unzCloseCurrentFile(uf);

		if (writeOk && written == (ULONGLONG)fi.uncompressed_size && written >= kChmMinBytes)
		{
			DeleteFile(destPath);
			if (MoveFile(tmpPath, destPath) && OfflineHelpFileLooksOk(destPath))
				bFound = true;
			else
				DeleteFile(tmpPath);
		}
		else
		{
			DeleteFile(tmpPath);
		}
		break;
	}
	unzClose(uf);
	return bFound;
}

static unsigned __stdcall OfflineHelpEnsureThread(void*)
{
	const CString destPath = OfflineHelpGetChmPath();
	if (OfflineHelpFileLooksOk(destPath))
	{
		InterlockedExchange(&g_chmEnsureRunning, 0);
		return 0;
	}

	if (OfflineHelpTryCopyFromTempDirs(destPath))
	{
		InterlockedExchange(&g_chmEnsureRunning, 0);
		return 0;
	}

	DWORD dwFlags = 0;
	if (!InternetGetConnectedState(&dwFlags, 0))
	{
		InterlockedExchange(&g_chmEnsureRunning, 0);
		return 0;
	}

	const CString url = OfflineHelpResolveZipUrl();
	if (url.IsEmpty())
	{
		InterlockedExchange(&g_chmEnsureRunning, 0);
		return 0;
	}

	TCHAR tempPath[MAX_PATH] = { 0 };
	GetTempPath(MAX_PATH, tempPath);
	CString zipPath;
	zipPath.Format(_T("%sogg_help_fetch.zip"), tempPath);
	CString extractDir;
	extractDir.Format(_T("%sogg_update_extract"), tempPath);
	CreateDirectory(extractDir, NULL);

	if (OfflineHelpHttpDownload(url, zipPath))
	{
		CString extracted;
		extracted.Format(_T("%s\\%s"), (LPCTSTR)extractDir, OFFLINE_HELP_CHM_NAME);
		if (OfflineHelpExtractChmFromZip(zipPath, extracted))
			OfflineHelpCopyFile(extracted, destPath);
		// ZIP は本体更新でも使うことがあるので残してもよいが、ヘルプ専用取得は削除
		DeleteFile(zipPath);
	}

	InterlockedExchange(&g_chmEnsureRunning, 0);
	return 0;
}

void OfflineHelpEnsureAvailable()
{
	const CString path = OfflineHelpGetChmPath();
	if (OfflineHelpFileLooksOk(path))
		return;
	if (InterlockedCompareExchange(&g_chmEnsureRunning, 1, 0) != 0)
		return;
	const uintptr_t th = _beginthreadex(NULL, 0, OfflineHelpEnsureThread, NULL, 0, NULL);
	if (th)
		CloseHandle((HANDLE)th);
	else
		InterlockedExchange(&g_chmEnsureRunning, 0);
}

static bool OfflineHelpPreferEnglish(int preferredEn)
{
	if (preferredEn == 0)
		return false;
	if (preferredEn == 1)
		return true;
	const LANGID lang = GetUserDefaultUILanguage();
	return PRIMARYLANGID(lang) == LANG_ENGLISH;
}

void OfflineHelpOpen(HWND hwndOwner, int preferredEn)
{
	const CString path = OfflineHelpGetChmPath();
	if (!OfflineHelpFileLooksOk(path))
	{
		// すぐ取れるなら取る
		if (!OfflineHelpTryCopyFromTempDirs(path))
			OfflineHelpEnsureAvailable();

		if (!OfflineHelpFileLooksOk(path))
		{
			AfxMessageBox(LL14(
				L"オフラインヘルプ（oggYSEDbgm_uni_avx2.chm）が見つかりません。\n"
				L"バックグラウンドで取得を開始しました。しばらくしてからもう一度開いてください。\n"
				L"（自動アップデート用 ZIP にヘルプが含まれている場合、展開済みの一時フォルダからもコピーします）",
				L"Offline help (oggYSEDbgm_uni_avx2.chm) was not found.\n"
				L"A background download has started. Please try again shortly.\n"
				L"(If the update ZIP includes the help file, it can also be copied from a temp extract folder.)",
				L"Aide hors ligne introuvable. Telechargement en arriere-plan demarre.",
				L"Guida offline non trovata. Download in background avviato.",
				L"No se encontro la ayuda sin conexion. Se inicio la descarga en segundo plano.",
				L"오프라인 도움말을 찾을 수 없습니다. 백그라운드 다운로드를 시작했습니다.",
				L"未找到离线帮助。已开始后台获取。",
				L"تعذر العثور على التعليمات دون اتصال. بدأ التنزيل في الخلفية.",
				L"Офлайн-справка не найдена. Фоновая загрузка запущена.",
				L"Offline-Hilfe nicht gefunden. Hintergrunddownload gestartet.",
				L"Ajuda offline nao encontrada. Download em segundo plano iniciado.",
				L"Offline-help niet gevonden. Achtergronddownload gestart.",
				L"Nie znaleziono pomocy offline. Rozpoczeto pobieranie w tle.",
				L"Cevrimdisi yardim bulunamadi. Arka plan indirme baslatildi."));
			return;
		}
	}

	const bool useEn = OfflineHelpPreferEnglish(preferredEn);
	CString topic;
	topic.Format(_T("%s::/%s/index.htm"), (LPCTSTR)path, useEn ? _T("en") : _T("ja"));

	HWND hh = HtmlHelp(hwndOwner ? hwndOwner : GetDesktopWindow(), topic, HH_DISPLAY_TOPIC, 0);
	if (!hh)
	{
		// トピック指定が失敗したら CHM 本体だけ開く
		hh = HtmlHelp(hwndOwner ? hwndOwner : GetDesktopWindow(), path, HH_DISPLAY_TOPIC, 0);
	}
	if (!hh)
		ShellExecute(hwndOwner, _T("open"), path, NULL, NULL, SW_SHOWNORMAL);
}
