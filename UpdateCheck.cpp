// UpdateCheck.cpp - 起動時更新チェック・ダウンロード・展開
#include "stdafx.h"
#include "UpdateCheck.h"
#include "oggDlg.h"
#include <wininet.h>
#include <thread>
#include <ctime>

#pragma comment(lib, "wininet.lib")

#include "minizip/unzip.h"  // ioapi.h を経由して zlib_filefunc64_def 等を定義
#ifdef USEWIN32IOAPI
#include "minizip/iowin32.h"
#endif

static const TCHAR* UPDATE_URL = _T("https://ppp.oohara.jp/download/oggYSEDbgm08g_uni_avx2_VC2026.zip");

// __DATE__ "Mar 18 2025", __TIME__ "12:34:56" をパースして time_t 取得
// __DATE__/__TIME__ はコンパイラのローカル時刻。mktime で time_t(UTC) に変換
static time_t GetBuildTimeUtc()
{
	static const char* months[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
	struct tm t = { 0 };
	int m, d, y, hh, mm, ss;
	char month[4] = { 0 };
	sscanf_s(__DATE__, "%3s %d %d", month, (unsigned)sizeof(month), &d, &y);
	sscanf_s(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
	for (m = 0; m < 12; m++) if (strcmp(months[m], month) == 0) break;
	t.tm_year = y - 1900;
	t.tm_mon = m;
	t.tm_mday = d;
	t.tm_hour = hh;
	t.tm_min = mm;
	t.tm_sec = ss;
	t.tm_isdst = -1;  // 夏時間は自動判定
	return mktime(&t);  // ローカル→time_t(UTC)
}

// HTTP HEAD で Last-Modified を取得、失敗時は 0
static time_t HttpGetLastModified(const CString& url)
{
	time_t result = 0;
	HINTERNET hInternet = InternetOpen(_T("oggUpdateCheck/1.0"), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (!hInternet) return 0;

	DWORD timeout = 5000;
	InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
	if (url.Find(_T("https://")) == 0) flags |= INTERNET_FLAG_SECURE;

	HINTERNET hConnect = InternetOpenUrl(hInternet, url, NULL, 0, flags, 0);
	if (!hConnect) { InternetCloseHandle(hInternet); return 0; }

	SYSTEMTIME st = { 0 };
	DWORD bufLen = sizeof(st);
	if (HttpQueryInfo(hConnect, HTTP_QUERY_LAST_MODIFIED | HTTP_QUERY_FLAG_SYSTEMTIME, &st, &bufLen, NULL))
	{
		// Last-Modified は GMT。SYSTEMTIME は UTC。ローカルに変換して mktime で time_t(UTC) 取得
		SystemTimeToTzSpecificLocalTime(NULL, &st, &st);
		struct tm t = { 0 };
		t.tm_year = st.wYear - 1900;
		t.tm_mon = st.wMonth - 1;
		t.tm_mday = st.wDay;
		t.tm_hour = st.wHour;
		t.tm_min = st.wMinute;
		t.tm_sec = st.wSecond;
		t.tm_isdst = -1;
		result = mktime(&t);
	}
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);
	return result;
}

// ZIP をダウンロード、成功時はパスを返す
static bool HttpDownloadToFile(const CString& url, const CString& localPath)
{
	HINTERNET hInternet = InternetOpen(_T("oggUpdateCheck/1.0"), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (!hInternet) return false;

	DWORD timeout = 60000; // 60秒
	InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
	if (url.Find(_T("https://")) == 0) flags |= INTERNET_FLAG_SECURE;

	HINTERNET hConnect = InternetOpenUrl(hInternet, url, NULL, 0, flags, 0);
	if (!hConnect) { InternetCloseHandle(hInternet); return false; }

	CFile f;
	if (!f.Open(localPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
	{
		InternetCloseHandle(hConnect);
		InternetCloseHandle(hInternet);
		return false;
	}

	char buf[8192];
	DWORD bytesRead;
	while (InternetReadFile(hConnect, buf, sizeof(buf), &bytesRead) && bytesRead > 0)
		f.Write(buf, bytesRead);

	f.Close();
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);
	return true;
}

// ZIP を destDir に展開
static bool ExtractZipToDir(const CString& zipPath, const CString& destDir)
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
	if (!uf) return false;

	unz_global_info64 gi;
	if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) { unzClose(uf); return false; }

	for (ZPOS64_T i = 0; i < gi.number_entry; i++)
	{
		char filename_inzip[1024];
		unz_file_info64 fi;
		if (unzGetCurrentFileInfo64(uf, &fi, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) break;

		const char* name = filename_inzip;
		while (*name == '/' || *name == '\\') name++;
		if (!*name) { unzGoToNextFile(uf); continue; }

		CString outPath = destDir + _T("\\") + CA2T(name, CP_UTF8);
		for (int k = 0; k < outPath.GetLength(); k++)
			if (outPath[k] == '/') outPath.SetAt(k, '\\');

		if (name[strlen(name) - 1] == '/' || name[strlen(name) - 1] == '\\')
		{
			CreateDirectory(outPath, NULL);
		}
		else
		{
			int lastSlash = outPath.ReverseFind('\\');
			if (lastSlash >= 0) {
				CString parentDir = outPath.Left(lastSlash);
				for (int i = 1; i < parentDir.GetLength(); i++) {
					if (parentDir[i] == '\\') {
						CString sub = parentDir.Left(i);
						CreateDirectory(sub, NULL);
					}
				}
				CreateDirectory(parentDir, NULL);
			}

			if (unzOpenCurrentFile(uf) != UNZ_OK) { unzGoToNextFile(uf); continue; }

			CFile outFile;
			if (outFile.Open(outPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
			{
				char buf[8192];
				int n;
				while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0)
					outFile.Write(buf, n);
				outFile.Close();
			}
			unzCloseCurrentFile(uf);
		}

		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	return true;
}

static const DWORD CHECK_INTERVAL_SEC = 600;  // 10分

static DWORD WINAPI UpdateCheckThreadProc(LPVOID param)
{
	HWND hWnd = (HWND)param;
	if (!hWnd || !IsWindow(hWnd)) return 0;

	time_t buildTime = GetBuildTimeUtc();
	if (buildTime == (time_t)-1) return 0;

	// ビルド時刻 + 1時間
	time_t threshold = buildTime + 3600;

	for (;;)
	{
		if (!IsWindow(hWnd)) break;

		// ネット未接続時はスキップ（タイムアウト待ちを避ける）
		DWORD dwFlags = 0;
		if (!InternetGetConnectedState(&dwFlags, 0))
		{
			for (DWORD i = 0; i < CHECK_INTERVAL_SEC; i++)
			{
				if (!IsWindow(hWnd)) return 0;
				Sleep(1000);
			}
			continue;
		}

		time_t serverModified = HttpGetLastModified(UPDATE_URL);
		if (serverModified != 0 && serverModified > threshold)
			PostMessage(hWnd, WM_APP_UPDATE_AVAILABLE, 0, 0);

		// 10分待機（1秒ずつ分割してウィンドウ破棄を検知）
		for (DWORD i = 0; i < CHECK_INTERVAL_SEC; i++)
		{
			if (!IsWindow(hWnd)) return 0;
			Sleep(1000);
		}
	}
	return 0;
}

void StartUpdateCheckThread(HWND hNotifyWnd)
{
	CreateThread(NULL, 0, UpdateCheckThreadProc, hNotifyWnd, 0, NULL);
}

// 更新実行：ダウンロード→展開→バッチで置換→再起動
bool DoUpdateAndRestart()
{
	extern TCHAR karento2[1024];
	extern save savedata;

	TCHAR exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);
	CString exeDir = exePath;
	int lastSlash = exeDir.ReverseFind('\\');
	if (lastSlash >= 0) exeDir = exeDir.Left(lastSlash + 1);

	TCHAR tempPath[MAX_PATH], zipPath[MAX_PATH], extractDir[MAX_PATH], batPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	_stprintf_s(zipPath, _T("%sogg_update.zip"), tempPath);
	_stprintf_s(extractDir, _T("%sogg_update_extract"), tempPath);
	_stprintf_s(batPath, _T("%sogg_updater.bat"), tempPath);

	CreateDirectory(extractDir, NULL);

	if (!HttpDownloadToFile(UPDATE_URL, zipPath))
	{
		AfxMessageBox(LL14(L"ダウンロードに失敗しました。\nネットワーク接続を確認してください。", L"Download failed.\nPlease check your network connection.", L"Telechargement echoue.\nVerifiez votre connexion reseau.", L"Download fallito.\nControlla la connessione di rete.", L"Descarga fallida.\nCompruebe su conexion de red.", L"??? ????. ??? ??? ??? ???.", L"下载失败。\n请检查网络连接。", L"????? ???????. ????? ???????.", L"?????? ?????????. ??????? ?????????? ? ?????.", L"Download fehlgeschlagen.\nBitte Netzwerkverbindung prufen.", L"Download falhou.\nVerifique sua conexao de rede.", L"Download mislukt.\nControleer uw netwerkverbinding.", L"Pobieranie nie powiodlo sie.\nSprawdz polaczenie sieciowe.", L"Indirme basarisiz.\nAg baglantisini kontrol edin."));
		return false;
	}

	if (!ExtractZipToDir(zipPath, extractDir))
		return false;

	// バッチ作成: 待機→xcopyで全ファイルコピー→起動→自己削除
	CStdioFile bat;
	if (!bat.Open(batPath, CFile::modeCreate | CFile::modeWrite | CFile::typeText))
		return false;

	CString batContent;
	batContent.Format(_T("@echo off\r\n")
		_T(":wait\r\n")
		_T("ping -n 1 127.0.0.1 >nul\r\n")
		_T("xcopy /s /e /y \"%s\\*\" \"%s\" >nul 2>&1\r\n")
		_T("if errorlevel 1 goto wait\r\n")
		_T("start \"\" \"%s\"\r\n")
		_T("del \"%%~f0\"\r\n"),
		extractDir, exeDir, exePath);
	bat.WriteString(batContent);
	bat.Close();

	// savedata 更新（lastUpdateCheck = 現在時刻）
	savedata.lastUpdateCheck = (__int64)time(NULL);

#if _UNICODE
	CFile ab;
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL))
	{
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
#else
	CFile ab;
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL))
	{
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
#endif

	// バッチ実行（非同期）
	ShellExecute(NULL, _T("open"), batPath, NULL, tempPath, SW_HIDE);

	return true;
}
