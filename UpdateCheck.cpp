// UpdateCheck.cpp - 起動時更新チェック・ダウンロード・展開
#include "stdafx.h"
#include "UpdateCheck.h"
#include "oggDlg.h"
#include <wininet.h>
#include <thread>
#include <ctime>
#include <sys/stat.h> // ファイル情報を取得するために追加いたしましたわ

#pragma comment(lib, "wininet.lib")

#include "minizip/unzip.h"  // ioapi.h を経由して zlib_filefunc64_def 等を定義
#ifdef USEWIN32IOAPI
#include "minizip/iowin32.h"
#endif

static const TCHAR* UPDATE_URL = _T("https://ppp.oohara.jp/download/oggYSEDbgm08g_uni_avx2_VC2026.zip");
static const TCHAR* TARGET_EXE_NAME = _T("oggYSEDbgm_uni_avx2.exe");

// ビルド時間で確認するようにしておりますわ
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

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
	if (url.Find(_T("https://")) == 0) flags |= INTERNET_FLAG_SECURE;

	// キャッシュを確実に回避するため、URLの末尾に現在時刻を付けますわ
	CString noCacheUrl;
	noCacheUrl.Format(_T("%s?t=%lld"), (LPCTSTR)url, (long long)time(NULL));

	HINTERNET hConnect = InternetOpenUrl(hInternet, noCacheUrl, NULL, 0, flags, 0);
	if (!hConnect) { InternetCloseHandle(hInternet); return 0; }

	// ファイルが存在するか（HTTPステータスが 200 OK か）を確認します
	DWORD statusCode = 0;
	DWORD statusCodeLen = sizeof(statusCode);
	if (HttpQueryInfo(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeLen, NULL))
	{
		if (statusCode != 200)
		{
			// ファイルがない場合は 0 を返しますわ
			InternetCloseHandle(hConnect);
			InternetCloseHandle(hInternet);
			return 0;
		}
	}

	char rawDate[256] = { 0 };
	DWORD rawLen = sizeof(rawDate);
	if (HttpQueryInfoA(hConnect, HTTP_QUERY_LAST_MODIFIED, rawDate, &rawLen, NULL))
	{
		CString dbgRaw;
		dbgRaw.Format(_T("[UpdateCheck] サーバーからの生の応答日時: %S\n"), rawDate);
		OutputDebugString(dbgRaw);
	}

	SYSTEMTIME st = { 0 };
	DWORD bufLen = sizeof(st);
	if (HttpQueryInfo(hConnect, HTTP_QUERY_LAST_MODIFIED | HTTP_QUERY_FLAG_SYSTEMTIME, &st, &bufLen, NULL))
	{
		// ローカルに変換して time_t 取得
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

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
	if (url.Find(_T("https://")) == 0) flags |= INTERNET_FLAG_SECURE;

	// キャッシュ回避
	CString noCacheUrl;
	noCacheUrl.Format(_T("%s?t=%lld"), (LPCTSTR)url, (long long)time(NULL));

	HINTERNET hConnect = InternetOpenUrl(hInternet, noCacheUrl, NULL, 0, flags, 0);
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

// ZIP から特定のファイル（targetFileName）だけを抽出し、destDir直下に展開する
static bool ExtractZipToDir(const CString& zipPath, const CString& destDir, const CString& targetFileName)
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

	bool bFound = false;

	for (ZPOS64_T i = 0; i < gi.number_entry; i++)
	{
		char filename_inzip[1024];
		unz_file_info64 fi;
		if (unzGetCurrentFileInfo64(uf, &fi, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK) break;

		const char* name = filename_inzip;

		// フォルダ階層が含まれている場合、ファイル名だけを切り出す
		const char* p = name;
		const char* fileNameOnly = name;
		while (*p) {
			if (*p == '/' || *p == '\\') {
				fileNameOnly = p + 1;
			}
			p++;
		}

		if (*fileNameOnly == '\0') { unzGoToNextFile(uf); continue; } // ディレクトリはスキップ

		CString currentFileName = CA2T(fileNameOnly, CP_UTF8);

		// 目的のファイルかどうか確認
		if (currentFileName.CompareNoCase(targetFileName) == 0)
		{
			if (unzOpenCurrentFile(uf) != UNZ_OK) { unzGoToNextFile(uf); continue; }

			// 目的のファイルを destDir の直下に展開
			CString outPath = destDir + _T("\\") + currentFileName;
			CFile outFile;
			if (outFile.Open(outPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
			{
				char buf[8192];
				int n;
				while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0)
					outFile.Write(buf, n);
				outFile.Close();
				bFound = true;
			}
			unzCloseCurrentFile(uf);
			break; // 見つかったら他のファイルは展開せずにループを抜ける
		}

		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	return bFound;
}

static const DWORD CHECK_INTERVAL_SEC = 600;  // 10分

static DWORD WINAPI UpdateCheckThreadProc(LPVOID param)
{
	extern save savedata; // 保存データを参照できるように追加いたしましたわ
	HWND hWnd = (HWND)param;
	if (!hWnd || !IsWindow(hWnd)) return 0;

	// プログラムのビルド時間を見ますわ
	time_t buildTime = GetBuildTimeUtc();
	if (buildTime == (time_t)-1) return 0;

	// ビルド時刻 + 1時間
	time_t threshold = buildTime + 3600;

	CString dbgMsg;
	dbgMsg.Format(_T("[UpdateCheck] BuildTime: %lld, Threshold: %lld\n"), (long long)buildTime, (long long)threshold);
	OutputDebugString(dbgMsg);

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

		dbgMsg.Format(_T("[UpdateCheck] ServerModified: %lld, LastUpdateCheck: %lld\n"), (long long)serverModified, (long long)savedata.lastUpdateCheck);
		OutputDebugString(dbgMsg);

		// ----------------------------------------------------------------------------------
		// ここが無限ループを防ぐ重要な変更点ですわ！
		// サーバーの時間が「プログラム作成時間＋1時間」より新しく、かつ、
		// 「保存データに記録された前回の更新時間」よりも新しい場合のみ更新通知を出しますわ
		// ----------------------------------------------------------------------------------
		if (serverModified != 0 && serverModified > threshold && (__int64)serverModified > savedata.lastUpdateCheck)
		{
			OutputDebugString(_T("[UpdateCheck] 更新を検知いたしました！メッセージを送信しますわ。\n"));
			PostMessage(hWnd, WM_APP_UPDATE_AVAILABLE, 0, 0);
		}

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

	// 現在実行しているファイルの名前を取得いたしますわ
	TCHAR exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);

	// コピー先のファイル名を必ず「oggYSEDbgm_uni_avx2.exe」にするための処理ですわ
	TCHAR targetExePath[MAX_PATH] = { 0 };
	_tcscpy_s(targetExePath, MAX_PATH, exePath);
	TCHAR* pSlash = _tcsrchr(targetExePath, _T('\\'));
	if (pSlash != NULL)
	{
		// フォルダの場所を残して、最後の円マークの後ろに正しいファイル名を連結いたします
		*(pSlash + 1) = _T('\0');
		_tcscat_s(targetExePath, MAX_PATH, TARGET_EXE_NAME);
	}
	else
	{
		_tcscpy_s(targetExePath, MAX_PATH, TARGET_EXE_NAME);
	}

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

	// ZIPから oggYSEDbgm_uni_avx2.exe だけを取り出して extractDir に展開しますわ
	if (!ExtractZipToDir(zipPath, extractDir, TARGET_EXE_NAME))
	{
		return false;
	}

	CString extractedPath;
	extractedPath.Format(_T("%s\\%s"), extractDir, TARGET_EXE_NAME);

	time_t serverTime = HttpGetLastModified(UPDATE_URL);
	if (serverTime > 0)
	{
		time_t newTime = serverTime + 1;

		// コンピューターの内部時間を変換いたします
		ULARGE_INTEGER ull;
		ull.QuadPart = ((ULONGLONG)newTime * 10000000ULL) + 116444736000000000ULL;
		FILETIME ft;
		ft.dwLowDateTime = ull.LowPart;
		ft.dwHighDateTime = ull.HighPart;

		// 時間を書き換えますわ
		HANDLE hFile = CreateFile(extractedPath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			// 作成日時、アクセス日時、更新日時の3つすべてを揃えますわ
			SetFileTime(hFile, &ft, &ft, &ft);
			CloseHandle(hFile);
		}
	}

	// 実際に更新したファイルを見て、その更新日付で lastUpdateCheck を更新いたしますわ
	struct __stat64 fileStat;
	if (_tstat64(extractedPath, &fileStat) == 0)
	{
		savedata.lastUpdateCheck = (__int64)fileStat.st_mtime;
	}
	else
	{
		savedata.lastUpdateCheck = (__int64)time(NULL);
	}

	// 命令書（バッチファイル）の作成
	CFile bat;
	if (!bat.Open(batPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
		return false;

	// 文字化けを起こさないよう変換いたします
	CStringA extractDirA(extractDir);
	CStringA targetExeA(TARGET_EXE_NAME);
	CStringA exePathA(exePath);              // 現在動いているファイル
	CStringA targetExePathA(targetExePath);  // 新しく作る正しい名前のファイル
	CStringA tempPathA(tempPath);

	CStringA batContentA;
	batContentA.Format(
		"@echo off\r\n"
		"set LOG=\"%sogg_update_log.txt\"\r\n"
		"echo 更新処理を開始いたします > %%LOG%%\r\n"
		"set RETRY=0\r\n"
		":wait\r\n"
		"ping -n 4 127.0.0.1 >nul\r\n"
		"copy /y \"%s\\%s\" \"%s\" >> %%LOG%% 2>&1\r\n"
		"if not errorlevel 1 goto success\r\n"
		"set /a RETRY+=1\r\n"
		"if %%RETRY%% geq 15 goto fail\r\n"
		"goto wait\r\n"
		":success\r\n"
		"echo 上書きに成功いたしました！ >> %%LOG%%\r\n"
		"start \"\" \"%s\"\r\n"
		"goto end\r\n"
		":fail\r\n"
		"echo 上書きに失敗いたしました... >> %%LOG%%\r\n"
		"start \"\" \"%s\"\r\n"
		":end\r\n"
		"del \"%%~f0\"\r\n",
		(LPCSTR)tempPathA,
		(LPCSTR)extractDirA, (LPCSTR)targetExeA, (LPCSTR)targetExePathA,
		(LPCSTR)targetExePathA, // 成功時は新しい方を起動します
		(LPCSTR)exePathA        // 失敗時は元のファイルを起動します
	);

	bat.Write(batContentA, batContentA.GetLength());
	bat.Close();

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

	// 命令書（バッチファイル）の実行
	ShellExecute(NULL, _T("open"), batPath, NULL, tempPath, SW_HIDE);

	// アプリケーションを終了させますわ
	exit(0);

	return true;
}