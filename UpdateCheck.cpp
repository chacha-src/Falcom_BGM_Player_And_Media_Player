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

// 09a があれば優先。なければ 08g。08g 適用後に 09a が公開されれば、次回チェックで 09a へ進む。
static const TCHAR* UPDATE_URL_PRIMARY = _T("https://ppp.oohara.jp/download/oggYSEDbgm09a_uni_avx2_VC2026.zip");
static const TCHAR* UPDATE_URL_FALLBACK = _T("https://ppp.oohara.jp/download/oggYSEDbgm08g_uni_avx2_VC2026.zip");
static const TCHAR* TARGET_EXE_NAME = _T("oggYSEDbgm_uni_avx2.exe");
static const TCHAR* TARGET_HOST_EXE_NAME = _T("KpiHost64.exe");

// コンパイル時間ではなく、現在動いている実行ファイルの実際の更新日時を取得するように変更いたしましたわ
// これにより、ファイルの一部だけをビルドした際の「時間が過去のままになる落とし穴」を完全に回避いたします
static time_t GetExecutableModificationTimeUtc()
{
	TCHAR exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);

	HANDLE hFile = CreateFile(exePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return 0;

	FILETIME ftWrite;
	bool bSuccess = GetFileTime(hFile, NULL, NULL, &ftWrite);
	CloseHandle(hFile);

	if (!bSuccess) return 0;

	// Windowsの時間をUTCのtime_tに変換いたします
	ULARGE_INTEGER ull;
	ull.LowPart = ftWrite.dwLowDateTime;
	ull.HighPart = ftWrite.dwHighDateTime;
	return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

// HTTP HEAD で Last-Modified を取得、失敗時は 0
static time_t HttpGetLastModified(const CString& url)
{
	time_t result = 0;
	// プロキシ環境下の方でも検知できるよう、システム（IE/WPAD/PAC）のプロキシ設定を使用いたしますわ
	HINTERNET hInternet = InternetOpen(_T("oggUpdateCheck/1.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
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
		SYSTEMTIME st = { 0 };
		// 生の文字列を解釈して、確実に世界標準時(UTC)のまま時間を取得いたします
		if (InternetTimeToSystemTimeA(rawDate, &st, 0))
		{
			FILETIME ft;
			SystemTimeToFileTime(&st, &ft);
			ULARGE_INTEGER ull;
			ull.LowPart = ft.dwLowDateTime;
			ull.HighPart = ft.dwHighDateTime;
			// 1601年からの時間を表す形式から、基準時間を引いて time_t(UTC) に変換いたしますわ
			result = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
		}
	}
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);
	return result;
}

// 利用可能な更新 ZIP を決定する（09a 優先、なければ 08g）。見つからなければ空文字、*outModified は 0。
static CString ResolveUpdateUrl(time_t* outModified)
{
	if (outModified)
		*outModified = 0;

	const time_t primaryModified = HttpGetLastModified(UPDATE_URL_PRIMARY);
	if (primaryModified != 0)
	{
		if (outModified)
			*outModified = primaryModified;
		return UPDATE_URL_PRIMARY;
	}

	const time_t fallbackModified = HttpGetLastModified(UPDATE_URL_FALLBACK);
	if (fallbackModified != 0)
	{
		if (outModified)
			*outModified = fallbackModified;
		return UPDATE_URL_FALLBACK;
	}

	return CString();
}

// ZIP をダウンロード、成功時はパスを返す
static bool HttpDownloadToFile(const CString& url, const CString& localPath)
{
	// プロキシ環境下の方でもダウンロードできるよう、システムのプロキシ設定を使用いたしますわ
	HINTERNET hInternet = InternetOpen(_T("oggUpdateCheck/1.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
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

static volatile LONG g_updatePromptOpen = 0;
static volatile LONG g_updateMsgQueued = 0;
static volatile __int64 g_updateDismissedVersion = 0;

void RunStartupUpdateCheck()
{
	extern save savedata;

	// オフライン時は待たずに通常起動を続ける
	DWORD dwFlags = 0;
	if (!InternetGetConnectedState(&dwFlags, 0))
		return;

	const time_t exeTime = GetExecutableModificationTimeUtc();
	if (exeTime == 0)
		return;

	time_t serverModified = 0;
	const CString updateUrl = ResolveUpdateUrl(&serverModified);
	if (updateUrl.IsEmpty() ||
		serverModified == 0 ||
		serverModified <= exeTime + 120 ||
		(__int64)serverModified <= savedata.lastUpdateCheck)
	{
		return;
	}

	// 「いいえ」でも「はい」(失敗時)でも、この起動中は定期チェックから再通知しない。
	// 保存データには書かないため、次回起動時には再度確認メッセージが出る。
	UpdateCheckDismissVersion((__int64)serverModified);

	const int ret = AfxMessageBox(LL14(
		L"アップデートファイルがあります。\n今すぐ更新しますか？\n(いいえ = 次回起動時まで保留)",
		L"An update file is available.\nWould you like to update now?\n(No = postpone until next launch)",
		L"Un fichier de mise à jour est disponible.\nMettre à jour maintenant ?\n(Non = reporter au prochain démarrage)",
		L"È disponibile un file di aggiornamento.\nAggiornare ora?\n(No = rimanda al prossimo avvio)",
		L"Hay un archivo de actualización disponible.\n¿Actualizar ahora?\n(No = posponer hasta el próximo inicio)",
		L"업데이트 파일이 있습니다.\n지금 업데이트하시겠습니까?\n(아니요 = 다음 실행 시까지 보류)",
		L"有更新文件。\n是否立即更新？\n(否 = 推迟到下次启动)",
		L"يتوفر ملف تحديث.\nهل تريد التحديث الآن؟\n(لا = التأجيل حتى التشغيل التالي)",
		L"Доступен файл обновления.\nОбновить сейчас?\n(Нет = отложить до следующего запуска)",
		L"Eine Aktualisierungsdatei ist verfügbar.\nJetzt aktualisieren?\n(Nein = bis zum nächsten Start aufschieben)",
		L"Um arquivo de atualização está disponível.\nAtualizar agora?\n(Não = adiar até a próxima inicialização)",
		L"Er is een updatebestand beschikbaar.\nNu bijwerken?\n(Nee = uitstellen tot volgende start)",
		L"Dostępny jest plik aktualizacji.\nCzy zaktualizować teraz?\n(Nie = odłóż do następnego uruchomienia)",
		L"Güncelleme dosyası mevcut.\nŞimdi güncellemek istiyor musunuz?\n(Hayır = sonraki başlatmaya ertele)"
	), MB_YESNO);

	if (ret == IDYES)
		DoUpdateAndRestart();  // 成功時はプロセス終了、失敗時はそのまま通常起動を続ける
}

void UpdateCheckDismissVersion(__int64 serverModified)
{
	if (serverModified > 0)
		InterlockedExchange64(&g_updateDismissedVersion, serverModified);
}

void UpdateCheckBeginPrompt()
{
	InterlockedExchange(&g_updateMsgQueued, 0);
	InterlockedExchange(&g_updatePromptOpen, 1);
}

void UpdateCheckEndPrompt(bool dismissedNo, __int64 serverModified)
{
	InterlockedExchange(&g_updatePromptOpen, 0);
	if (dismissedNo && serverModified > 0)
		UpdateCheckDismissVersion(serverModified);
}

static DWORD WINAPI UpdateCheckThreadProc(LPVOID param)
{
	extern save savedata; // 保存データを参照できるように追加いたしましたわ
	HWND hWnd = (HWND)param;
	if (!hWnd || !IsWindow(hWnd)) return 0;

	// コンパイル時間ではなく、現在動いているプログラムの実際の更新時間を見ますわ
	time_t exeTime = GetExecutableModificationTimeUtc();
	if (exeTime == 0) return 0;

	// プログラムの更新時刻 + 2分（120秒）に変更いたしましたわ
	time_t threshold = exeTime + 120;

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

		time_t serverModified = 0;
		const CString updateUrl = ResolveUpdateUrl(&serverModified);

		// サーバーの時間が「プログラム更新時間＋指定時間」より新しく、かつ、
		// 「保存データに記録された前回の更新時間」よりも新しい場合のみ更新通知を出しますわ
		// 09a 優先・なければ 08g。08g 適用後に 09a が出れば、その Last-Modified で再通知する。
		const __int64 dismissed = InterlockedCompareExchange64(&g_updateDismissedVersion, 0, 0);
		if (!updateUrl.IsEmpty() && serverModified != 0 && serverModified > threshold &&
			(__int64)serverModified > savedata.lastUpdateCheck &&
			(__int64)serverModified > dismissed &&
			InterlockedCompareExchange(&g_updatePromptOpen, 0, 0) == 0 &&
			InterlockedCompareExchange(&g_updateMsgQueued, 0, 0) == 0)
		{
			InterlockedExchange(&g_updateMsgQueued, 1);
			PostMessage(hWnd, WM_APP_UPDATE_AVAILABLE, (WPARAM)serverModified, 0);
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

	// KpiHost64.exe も同じフォルダに配置する前提で上書き対象に含めますわ
	TCHAR targetHostExePath[MAX_PATH] = { 0 };
	_tcscpy_s(targetHostExePath, MAX_PATH, targetExePath);
	TCHAR* pSlashHost = _tcsrchr(targetHostExePath, _T('\\'));
	if (pSlashHost != NULL)
	{
		*(pSlashHost + 1) = _T('\0');
		_tcscat_s(targetHostExePath, MAX_PATH, TARGET_HOST_EXE_NAME);
	}
	else
	{
		_tcscpy_s(targetHostExePath, MAX_PATH, TARGET_HOST_EXE_NAME);
	}

	TCHAR tempPath[MAX_PATH], zipPath[MAX_PATH], extractDir[MAX_PATH], batPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	_stprintf_s(zipPath, _T("%sogg_update.zip"), tempPath);
	_stprintf_s(extractDir, _T("%sogg_update_extract"), tempPath);
	_stprintf_s(batPath, _T("%sogg_updater.bat"), tempPath);

	CreateDirectory(extractDir, NULL);

	// ダウンロード直前にも再解決（09a 優先）。チェック時は 08g だけでも、この時点で 09a があれば 09a を取る。
	time_t serverTime = 0;
	const CString updateUrl = ResolveUpdateUrl(&serverTime);
	if (updateUrl.IsEmpty() || !HttpDownloadToFile(updateUrl, zipPath))
	{
		AfxMessageBox(LL14(L"ダウンロードに失敗しました。\nネットワーク接続を確認してください。", L"Download failed.\nPlease check your network connection.", L"Telechargement echoue.\nVerifiez votre connexion reseau.", L"Download fallito.\nControlla la connessione di rete.", L"Descarga fallida.\nCompruebe su conexion de red.", L"??? ????. ??? ??? ??? ???.", L"下载失败。\n请检查网络连接。", L"????? ???????. ????? ???????.", L"?????? ?????????. ??????? ?????????? ? ?????.", L"Download fehlgeschlagen.\nBitte Netzwerkverbindung prufen.", L"Download falhou.\nVerifique sua conexao de rede.", L"Download mislukt.\nControleer uw netwerkverbinding.", L"Pobieranie nie powiodlo sie.\nSprawdz polaczenie sieciowe.", L"Indirme basarisiz.\nAg baglantisini kontrol edin."));
		return false;
	}

	// ZIPから oggYSEDbgm_uni_avx2.exe と KpiHost64.exe を取り出して extractDir に展開しますわ
	if (!ExtractZipToDir(zipPath, extractDir, TARGET_EXE_NAME))
	{
		return false;
	}
	if (!ExtractZipToDir(zipPath, extractDir, TARGET_HOST_EXE_NAME))
	{
		return false;
	}

	CString extractedPath;
	extractedPath.Format(_T("%s\\%s"), extractDir, TARGET_EXE_NAME);
	CString extractedHostPath;
	extractedHostPath.Format(_T("%s\\%s"), extractDir, TARGET_HOST_EXE_NAME);

	// 展開したファイルの時刻をサーバー時刻に合わせておきますわ。
	// copy コマンドは元ファイルの時刻を引き継ぐため、上書きが「実際に成功したファイルだけ」が
	// サーバー時刻になり、失敗したファイルは古いままとなります。
	// これにより、権限不足などで上書きに失敗した場合は次回に再検知される仕組みですわ。
	// （serverTime は上で ResolveUpdateUrl 済み。08g 適用後に 09a が出れば、その時刻で再検知される）
	if (serverTime > 0)
	{
		time_t newTime = serverTime + 1;

		// コンピューターの内部時間を変換いたします
		ULARGE_INTEGER ull;
		ull.QuadPart = ((ULONGLONG)newTime * 10000000ULL) + 116444736000000000ULL;
		FILETIME ft;
		ft.dwLowDateTime = ull.LowPart;
		ft.dwHighDateTime = ull.HighPart;

		// 時間を書き換えますわ（exe / host どちらも）
		HANDLE hFile = CreateFile(extractedPath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			// 作成日時、アクセス日時、更新日時の3つすべてを揃えますわ
			SetFileTime(hFile, &ft, &ft, &ft);
			CloseHandle(hFile);
		}
		HANDLE hFile2 = CreateFile(extractedHostPath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile2 != INVALID_HANDLE_VALUE)
		{
			SetFileTime(hFile2, &ft, &ft, &ft);
			CloseHandle(hFile2);
		}
	}

	// 命令書（バッチファイル）の作成
	CFile bat;
	if (!bat.Open(batPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
		return false;

	// 文字化けを起こさないよう変換いたします
	CStringA extractDirA(extractDir);
	CStringA targetExeA(TARGET_EXE_NAME);
	CStringA targetHostExeA(TARGET_HOST_EXE_NAME);
	CStringA targetExePathA(targetExePath);  // 新しく作る正しい名前のファイル
	CStringA targetHostExePathA(targetHostExePath); // 新しいホストexe

	// 実行ファイルがある正しいフォルダのパスを抜き出しますわ
	CStringA exeDirA = targetExePathA;
	int slashPos = exeDirA.ReverseFind('\\');
	if (slashPos != -1)
	{
		exeDirA = exeDirA.Left(slashPos);
	}

	// 引数付き起動（ファイル関連付け等）なら、更新後の再起動へ同じ引数を引き継ぐ。
	// これで関連付けから開いたファイルが、アップデート後にそのまま演奏開始される。
	// バッチでは % が特殊文字のため %% にエスケープしておく。
	CStringA cmdArgsA;
	{
		CWinApp* pApp = AfxGetApp();
		if (pApp && pApp->m_lpCmdLine && pApp->m_lpCmdLine[0])
			cmdArgsA = CStringA(pApp->m_lpCmdLine);
		cmdArgsA.Trim();
		cmdArgsA.Replace("%", "%%");
	}

	// 命令書は二段構えでございます。
	//   ランチャー部（常に通常権限）… プロセス終了待ち→書込可否を判定→
	//                                 書込可なら worker を同じ権限で実行、
	//                                 書込不可なら worker だけを昇格（UAC）で実行し、
	//                                 最後に必ず通常権限でアプリを再起動して自身を削除。
	//   worker 部（昇格され得る）  … 対象の停止と上書きのみを担当し、再起動も削除も行いません。
	// これにより Program Files 等でも上書きでき、昇格を断られた場合でも必ず起動し直します。
	CStringA batContentA;
	batContentA.Format(
		"@echo off\r\n"
		"setlocal\r\n"
		"if \"%%~1\"==\"worker\" goto worker\r\n"
		"taskkill /IM %s /F >nul 2>&1\r\n"
		"ping -n 4 127.0.0.1 >nul\r\n"
		"taskkill /IM %s /F >nul 2>&1\r\n"
		"set \"PROBE=%s\\ogg_upd_probe.tmp\"\r\n"
		"type nul > \"%%PROBE%%\" 2>nul\r\n"
		"if exist \"%%PROBE%%\" (\r\n"
		"  del \"%%PROBE%%\" >nul 2>&1\r\n"
		"  call :worker\r\n"
		") else (\r\n"
		"  powershell -NoProfile -Command \"Start-Process -FilePath '%%~f0' -ArgumentList 'worker' -Verb RunAs -Wait\" >nul 2>&1\r\n"
		")\r\n"
		"cd /d \"%s\"\r\n"
		"start \"\" \"%s\" %s\r\n"
		"del \"%%~f0\"\r\n"
		"goto :eof\r\n"
		":worker\r\n"
		"set RETRY=0\r\n"
		":wait\r\n"
		"ping -n 4 127.0.0.1 >nul\r\n"
		"taskkill /IM %s /F >nul 2>&1\r\n"
		"copy /y \"%s\\%s\" \"%s\" >nul 2>&1\r\n"
		"if errorlevel 1 goto retry\r\n"
		"copy /y \"%s\\%s\" \"%s\" >nul 2>&1\r\n"
		"if not errorlevel 1 goto :eof\r\n"
		":retry\r\n"
		"set /a RETRY+=1\r\n"
		"if %%RETRY%% geq 15 goto :eof\r\n"
		"goto wait\r\n",
		(LPCSTR)targetHostExeA,                                       // 起動直後の停止（host）
		(LPCSTR)targetHostExeA,                                       // 再度の停止（host）
		(LPCSTR)exeDirA,                                              // 書込可否テスト先フォルダ
		(LPCSTR)exeDirA,                                              // 再起動前の移動先
		(LPCSTR)targetExePathA,                                       // 再起動する本体
		(LPCSTR)cmdArgsA,                                             // 元の起動引数（関連付けファイル等）
		(LPCSTR)targetHostExeA,                                       // worker内の停止（host）
		(LPCSTR)extractDirA, (LPCSTR)targetHostExeA, (LPCSTR)targetHostExePathA, // host 上書き
		(LPCSTR)extractDirA, (LPCSTR)targetExeA, (LPCSTR)targetExePathA          // 本体 上書き
	);

	bat.Write(batContentA, batContentA.GetLength());
	bat.Close();

	// 命令書（バッチファイル）の実行
	ShellExecute(NULL, _T("open"), batPath, NULL, tempPath, SW_HIDE);

	// アプリケーションを終了させますわ
	exit(0);

	return true;
}