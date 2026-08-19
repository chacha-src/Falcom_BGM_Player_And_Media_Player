// UpdateCheck.cpp - 起動時更新チェック・ダウンロード・展開
#include "stdafx.h"
#include "UpdateCheck.h"
#include "oggDlg.h"
#include <wininet.h>
#include <ShlObj.h>
#include <ctime>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

#include "minizip/unzip.h"  // ioapi.h を経由して zlib_filefunc64_def 等を定義
#ifdef USEWIN32IOAPI
#include "minizip/iowin32.h"
#endif

// 09a があれば優先。なければ 08g。08g 適用後に 09a が公開されれば、次回チェックで 09a へ進む。
static const TCHAR* UPDATE_URL_PRIMARY = _T("https://ppp.oohara.jp/download/oggYSEDbgm09a_uni_avx2_VC2026.zip");
static const TCHAR* UPDATE_URL_FALLBACK = _T("https://ppp.oohara.jp/download/oggYSEDbgm08g_uni_avx2_VC2026.zip");
static const TCHAR* TARGET_EXE_NAME = _T("oggYSEDbgm_uni_avx2.exe");
static const TCHAR* TARGET_HOST_EXE_NAME = _T("KpiHost64.exe");
static const TCHAR* TARGET_CHM_NAME = _T("oggYSEDbgm_uni_avx2.chm");

// 配布 ZIP / 展開 EXE の下限（空・404 HTML・途中切断を弾く）
// 本体は数MB級、KpiHost64 は ~100KB 未満もあり得るので別閾値
static const ULONGLONG UPDATE_ZIP_MIN_BYTES = 200000ULL;
static const ULONGLONG UPDATE_MAIN_EXE_MIN_BYTES = 1000000ULL; // oggYSEDbgm_uni_avx2.exe
static const ULONGLONG UPDATE_HOST_EXE_MIN_BYTES = 20000ULL;   // KpiHost64.exe
static const ULONGLONG UPDATE_CHM_MIN_BYTES = 20000ULL;        // oggYSEDbgm_uni_avx2.chm（任意・無くても更新成功）

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

static ULONGLONG GetPathFileSize(LPCTSTR path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
		return 0;
	ULARGE_INTEGER ull;
	ull.LowPart = fad.nFileSizeLow;
	ull.HighPart = fad.nFileSizeHigh;
	return ull.QuadPart;
}

// PE 先頭 MZ と最低サイズを確認（展開破損・HTML誤保存を弾く）
static bool IsLikelyPeExe(LPCTSTR path, ULONGLONG minBytes)
{
	const ULONGLONG sz = GetPathFileSize(path);
	if (sz < minBytes)
		return false;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	unsigned char mz[2] = { 0, 0 };
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, mz, 2, &rd, NULL);
	CloseHandle(h);
	return ok && rd == 2 && mz[0] == 'M' && mz[1] == 'Z';
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

// ZIP をダウンロード。HTTP 200・最低サイズを満たさない場合は失敗（途中ファイルは削除）
static bool HttpDownloadToFile(const CString& url, const CString& localPath)
{
	DeleteFile(localPath);

	// プロキシ環境下の方でもダウンロードできるよう、システムのプロキシ設定を使用いたしますわ
	HINTERNET hInternet = InternetOpen(_T("oggUpdateCheck/1.0"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInternet) return false;

	DWORD timeout = 120000; // 大容量 ZIP 向けに 120 秒
	InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
	if (url.Find(_T("https://")) == 0) flags |= INTERNET_FLAG_SECURE;

	// キャッシュ回避
	CString noCacheUrl;
	noCacheUrl.Format(_T("%s?t=%lld"), (LPCTSTR)url, (long long)time(NULL));

	HINTERNET hConnect = InternetOpenUrl(hInternet, noCacheUrl, NULL, 0, flags, 0);
	if (!hConnect) { InternetCloseHandle(hInternet); return false; }

	DWORD statusCode = 0;
	DWORD statusCodeLen = sizeof(statusCode);
	if (!HttpQueryInfo(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeLen, NULL)
		|| statusCode != 200)
	{
		InternetCloseHandle(hConnect);
		InternetCloseHandle(hInternet);
		return false;
	}

	ULONGLONG contentLen = 0;
	{
		char lenBuf[64] = { 0 };
		DWORD lenBufLen = sizeof(lenBuf);
		if (HttpQueryInfoA(hConnect, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lenBufLen, NULL))
		{
			contentLen = (ULONGLONG)_atoi64(lenBuf);
			if (contentLen > 0 && contentLen < UPDATE_ZIP_MIN_BYTES)
			{
				InternetCloseHandle(hConnect);
				InternetCloseHandle(hInternet);
				return false;
			}
		}
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

	if (!readOk || total < UPDATE_ZIP_MIN_BYTES || (contentLen > 0 && total != contentLen))
	{
		DeleteFile(localPath);
		return false;
	}
	return true;
}

// ZIP から特定のファイル（targetFileName）だけを抽出し、destDir直下に展開する
// 展開バイト数が ZIP 内 uncompressed_size と一致。requirePe のときだけ PE(MZ) を要求
static bool ExtractZipToDir(const CString& zipPath, const CString& destDir, const CString& targetFileName,
	ULONGLONG minBytes, bool requirePe = true)
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
			DeleteFile(outPath);

			CFile outFile;
			bool writeOk = false;
			ULONGLONG written = 0;
			if (outFile.Open(outPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
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

			const bool peOk = !requirePe || IsLikelyPeExe(outPath, minBytes);
			if (writeOk
				&& written == (ULONGLONG)fi.uncompressed_size
				&& written >= minBytes
				&& peOk)
			{
				bFound = true;
			}
			else
			{
				DeleteFile(outPath);
			}
			break; // 見つかったら他のファイルは展開せずにループを抜ける
		}

		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	return bFound;
}

// 前回の自動更新が成功していればフラグを消し、失敗のままなら true
static bool UpdateAttemptStillFailed(time_t exeTime)
{
	extern save savedata;
	if (savedata.updateAttemptExeTime == 0 || exeTime == 0)
		return false;
	if ((__int64)exeTime != savedata.updateAttemptExeTime)
	{
		savedata.updateAttemptExeTime = 0;
		MpPersistSavedataQuick();
		return false;
	}
	return true;
}

// 自動上書き失敗時: 「ダウンロード」へ ZIP→EXE 展開しエクスプローラを開く（アプリは継続）
static bool DoManualUpdateToDownloads(const CString& updateUrl, time_t serverTime)
{
	extern save savedata;

	PWSTR downloadsW = NULL;
	TCHAR destDir[MAX_PATH] = { 0 };
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &downloadsW)) && downloadsW)
	{
		_stprintf_s(destDir, _T("%s\\oggYSEDbgm_update"), downloadsW);
		CoTaskMemFree(downloadsW);
	}
	else
	{
		TCHAR profile[MAX_PATH] = { 0 };
		if (FAILED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, profile)) || profile[0] == 0)
			return false;
		_stprintf_s(destDir, _T("%s\\Downloads\\oggYSEDbgm_update"), profile);
	}

	CreateDirectory(destDir, NULL);

	TCHAR zipPath[MAX_PATH];
	_stprintf_s(zipPath, _T("%s\\ogg_update.zip"), destDir);

	if (!HttpDownloadToFile(updateUrl, zipPath))
	{
		AfxMessageBox(LL14(
			L"ダウンロードに失敗しました。\nネットワーク接続を確認してください。",
			L"Download failed.\nPlease check your network connection.",
			L"Telechargement echoue.\nVerifiez votre connexion reseau.",
			L"Download fallito.\nControlla la connessione di rete.",
			L"Descarga fallida.\nCompruebe su conexion de red.",
			L"다운로드에 실패했습니다.\n네트워크 연결을 확인하세요.",
			L"下载失败。\n请检查网络连接。",
			L"فشل التنزيل.\nيرجى التحقق من اتصال الشبكة.",
			L"Не удалось загрузить.\nПроверьте подключение к сети.",
			L"Download fehlgeschlagen.\nBitte Netzwerkverbindung prufen.",
			L"Download falhou.\nVerifique sua conexao de rede.",
			L"Download mislukt.\nControleer uw netwerkverbinding.",
			L"Pobieranie nie powiodlo sie.\nSprawdz polaczenie sieciowe.",
			L"Indirme basarisiz.\nAg baglantisini kontrol edin."));
		return false;
	}

	if (!ExtractZipToDir(zipPath, destDir, TARGET_EXE_NAME, UPDATE_MAIN_EXE_MIN_BYTES)
		|| !ExtractZipToDir(zipPath, destDir, TARGET_HOST_EXE_NAME, UPDATE_HOST_EXE_MIN_BYTES))
	{
		AfxMessageBox(LL14(
			L"ZIPの展開に失敗しました。\nダウンロードフォルダのファイルを確認してください。",
			L"Failed to extract the ZIP.\nPlease check the files in the Downloads folder.",
			L"Echec de l'extraction du ZIP.\nVerifiez les fichiers du dossier Telechargements.",
			L"Estrazione ZIP non riuscita.\nControlla i file nella cartella Download.",
			L"Error al extraer el ZIP.\nCompruebe los archivos en Descargas.",
			L"ZIP 압축 해제에 실패했습니다.\n다운로드 폴더의 파일을 확인하세요.",
			L"ZIP 解压失败。\n请检查下载文件夹中的文件。",
			L"فشل استخراج ZIP.\nيرجى التحقق من الملفات في مجلد التنزيلات.",
			L"Не удалось распаковать ZIP.\nПроверьте файлы в папке Загрузки.",
			L"ZIP-Entpacken fehlgeschlagen.\nBitte Dateien im Ordner Downloads prufen.",
			L"Falha ao extrair o ZIP.\nVerifique os arquivos na pasta Downloads.",
			L"Uitpakken van ZIP mislukt.\nControleer de bestanden in Downloads.",
			L"Nie udalo sie rozpakowac ZIP.\nSprawdz pliki w folderze Pobrane.",
			L"ZIP acma basarisiz.\nIndirilenler klasorundeki dosyalari kontrol edin."));
		return false;
	}
	// ヘルプ CHM は任意（古い ZIP には無い）。あれば一緒に展開
	ExtractZipToDir(zipPath, destDir, TARGET_CHM_NAME, UPDATE_CHM_MIN_BYTES, false);

	// 展開済み EXE の時刻をサーバーに合わせ（手動コピー後の再検知用）
	if (serverTime > 0)
	{
		time_t newTime = serverTime + 1;
		ULARGE_INTEGER ull;
		ull.QuadPart = ((ULONGLONG)newTime * 10000000ULL) + 116444736000000000ULL;
		FILETIME ft;
		ft.dwLowDateTime = ull.LowPart;
		ft.dwHighDateTime = ull.HighPart;

		TCHAR exeOut[MAX_PATH], hostOut[MAX_PATH];
		_stprintf_s(exeOut, _T("%s\\%s"), destDir, TARGET_EXE_NAME);
		_stprintf_s(hostOut, _T("%s\\%s"), destDir, TARGET_HOST_EXE_NAME);
		HANDLE h1 = CreateFile(exeOut, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (h1 != INVALID_HANDLE_VALUE) { SetFileTime(h1, &ft, &ft, &ft); CloseHandle(h1); }
		HANDLE h2 = CreateFile(hostOut, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (h2 != INVALID_HANDLE_VALUE) { SetFileTime(h2, &ft, &ft, &ft); CloseHandle(h2); }
	}

	savedata.updateAttemptExeTime = 0;
	if (serverTime > 0)
		savedata.lastUpdateCheck = (__int64)serverTime;
	MpPersistSavedataQuick();

	ShellExecute(NULL, _T("open"), destDir, NULL, NULL, SW_SHOWNORMAL);

	AfxMessageBox(LL14(
		L"自動更新に失敗したため、更新ファイルを「ダウンロード」フォルダへ展開しました。\n"
		L"本プログラムを終了し、展開先の oggYSEDbgm_uni_avx2.exe と KpiHost64.exe\n"
		L"（あれば oggYSEDbgm_uni_avx2.chm も）をインストールフォルダへ上書きコピーしてください。\n"
		L"（エクスプローラでフォルダを開きました）",
		L"Automatic update failed, so the update files were extracted to your Downloads folder.\n"
		L"Please quit this program, then copy oggYSEDbgm_uni_avx2.exe and KpiHost64.exe\n"
		L"(and oggYSEDbgm_uni_avx2.chm if present) over the ones in the install folder.\n"
		L"(The folder was opened in Explorer.)",
		L"La mise a jour automatique a echoue ; les fichiers ont ete extraits dans Telechargements.\n"
		L"Quittez ce programme, puis copiez oggYSEDbgm_uni_avx2.exe et KpiHost64.exe\n"
		L"(et oggYSEDbgm_uni_avx2.chm s'il est present) par-dessus ceux du dossier d'installation.\n"
		L"(Le dossier a ete ouvert dans l'Explorateur.)",
		L"Aggiornamento automatico non riuscito; i file sono stati estratti in Download.\n"
		L"Chiudi questo programma, quindi copia oggYSEDbgm_uni_avx2.exe e KpiHost64.exe\n"
		L"(e oggYSEDbgm_uni_avx2.chm se presente) sopra quelli nella cartella di installazione.\n"
		L"(La cartella e stata aperta in Esplora risorse.)",
		L"La actualizacion automatica fallo; los archivos se extrajeron en Descargas.\n"
		L"Cierre este programa y copie oggYSEDbgm_uni_avx2.exe y KpiHost64.exe\n"
		L"(y oggYSEDbgm_uni_avx2.chm si existe) sobre los del folder de instalacion.\n"
		L"(Se abrio la carpeta en el Explorador.)",
		L"자동 업데이트에 실패하여 업데이트 파일을 다운로드 폴더에 풀었습니다.\n"
		L"이 프로그램을 종료한 뒤 oggYSEDbgm_uni_avx2.exe 와 KpiHost64.exe\n"
		L"(있으면 oggYSEDbgm_uni_avx2.chm 도) 설치 폴더에 덮어쓰기로 복사하세요.\n"
		L"(탐색기에서 폴더를 열었습니다)",
		L"自动更新失败，已将更新文件解压到“下载”文件夹。\n"
		L"请退出本程序，然后将 oggYSEDbgm_uni_avx2.exe 与 KpiHost64.exe\n"
		L"（如有 oggYSEDbgm_uni_avx2.chm 也一并）覆盖复制到安装文件夹。\n"
		L"（已在资源管理器中打开该文件夹）",
		L"فشل التحديث التلقائي، وتم استخراج ملفات التحديث إلى مجلد التنزيلات.\n"
		L"يرجى إنهاء هذا البرنامج ثم نسخ oggYSEDbgm_uni_avx2.exe و KpiHost64.exe\n"
		L"(و oggYSEDbgm_uni_avx2.chm إن وُجد) فوق الملفات في مجلد التثبيت.\n"
		L"(تم فتح المجلد في المستكشف)",
		L"Автообновление не удалось; файлы распакованы в папку Загрузки.\n"
		L"Закройте программу и скопируйте oggYSEDbgm_uni_avx2.exe и KpiHost64.exe\n"
		L"(и oggYSEDbgm_uni_avx2.chm при наличии) поверх файлов в папке установки.\n"
		L"(Папка открыта в Проводнике.)",
		L"Automatisches Update fehlgeschlagen; Dateien wurden nach Downloads entpackt.\n"
		L"Beenden Sie das Programm und kopieren Sie oggYSEDbgm_uni_avx2.exe und KpiHost64.exe\n"
		L"(sowie oggYSEDbgm_uni_avx2.chm falls vorhanden) uber die Dateien im Installationsordner.\n"
		L"(Der Ordner wurde im Explorer geoffnet.)",
		L"A atualizacao automatica falhou; os arquivos foram extraidos em Downloads.\n"
		L"Encerre este programa e copie oggYSEDbgm_uni_avx2.exe e KpiHost64.exe\n"
		L"(e oggYSEDbgm_uni_avx2.chm se houver) sobre os da pasta de instalacao.\n"
		L"(A pasta foi aberta no Explorer.)",
		L"Automatische update mislukt; bestanden zijn uitgepakt in Downloads.\n"
		L"Sluit dit programma en kopieer oggYSEDbgm_uni_avx2.exe en KpiHost64.exe\n"
		L"(en oggYSEDbgm_uni_avx2.chm indien aanwezig) over die in de installatiemap.\n"
		L"(De map is geopend in Verkenner.)",
		L"Automatyczna aktualizacja nie powiodla sie; pliki rozpakowano do Pobrane.\n"
		L"Zamknij program i skopiuj oggYSEDbgm_uni_avx2.exe oraz KpiHost64.exe\n"
		L"(oraz oggYSEDbgm_uni_avx2.chm jesli jest) na pliki w folderze instalacji.\n"
		L"(Folder otwarto w Eksploratorze.)",
		L"Otomatik guncelleme basarisiz; dosyalar Indirilenler klasorune acildi.\n"
		L"Bu programi kapatip oggYSEDbgm_uni_avx2.exe ve KpiHost64.exe dosyalarini\n"
		L"(varsa oggYSEDbgm_uni_avx2.chm de) kurulum klasorundekilerin uzerine kopyalayin.\n"
		L"(Klasor Gezgin'de acildi.)"));
	return true;
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

	// 前回試行後に exe が新しくなっていれば成功扱い。同一なら失敗フラグを残す。
	const bool prevFailed = UpdateAttemptStillFailed(exeTime);

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

	const wchar_t* msg;
	if (prevFailed) {
		msg = LL14(
			L"前回の自動更新が完了していないようです。\n"
			L"更新ファイルを「ダウンロード」フォルダへ展開し、手動で上書きしますか？\n"
			L"(いいえ = 次回起動時まで保留)",
			L"The previous automatic update does not appear to have completed.\n"
			L"Extract the update files to Downloads for a manual overwrite?\n"
			L"(No = postpone until next launch)",
			L"La precedente mise a jour automatique ne semble pas terminee.\n"
			L"Extraire les fichiers vers Telechargements pour un remplacement manuel ?\n"
			L"(Non = reporter au prochain demarrage)",
			L"Il precedente aggiornamento automatico non sembra completato.\n"
			L"Estrarre i file in Download per la sovrascrittura manuale?\n"
			L"(No = rimanda al prossimo avvio)",
			L"La actualizacion automatica anterior no parece haberse completado.\n"
			L"¿Extraer los archivos en Descargas para sobrescribir manualmente?\n"
			L"(No = posponer hasta el proximo inicio)",
			L"이전 자동 업데이트가 완료되지 않은 것 같습니다.\n"
			L"다운로드 폴더에 풀어 수동으로 덮어쓰시겠습니까?\n"
			L"(아니요 = 다음 실행 시까지 보류)",
			L"上次自动更新似乎未完成。\n"
			L"是否解压到“下载”文件夹以便手动覆盖？\n"
			L"(否 = 推迟到下次启动)",
			L"يبدو أن التحديث التلقائي السابق لم يكتمل.\n"
			L"هل تريد استخراج الملفات إلى التنزيلات للكتابة اليدوية؟\n"
			L"(لا = التأجيل حتى التشغيل التالي)",
			L"Предыдущее автообновление, похоже, не завершилось.\n"
			L"Распаковать файлы в Загрузки для ручной замены?\n"
			L"(Нет = отложить до следующего запуска)",
			L"Das vorherige automatische Update scheint nicht abgeschlossen.\n"
			L"Dateien nach Downloads entpacken und manuell uberschreiben?\n"
			L"(Nein = bis zum nachsten Start aufschieben)",
			L"A atualizacao automatica anterior parece nao ter sido concluida.\n"
			L"Extrair os arquivos em Downloads para substituir manualmente?\n"
			L"(Nao = adiar ate a proxima inicializacao)",
			L"De vorige automatische update lijkt niet voltooid.\n"
			L"Bestanden uitpakken naar Downloads voor handmatig overschrijven?\n"
			L"(Nee = uitstellen tot volgende start)",
			L"Poprzednia automatyczna aktualizacja wyglada na niedokonczona.\n"
			L"Rozpakowac pliki do Pobrane w celu recznego nadpisania?\n"
			L"(Nie = odloz do nastepnego uruchomienia)",
			L"Onceki otomatik guncelleme tamamlanmamis gorunuyor.\n"
			L"Manuel uzerine yazma icin Indirilenler klasorune acilsin mi?\n"
			L"(Hayir = sonraki baslatmaya ertele)");
	} else {
		msg = LL14(
			L"アップデートファイルがあります。\n今すぐ更新しますか？\n(いいえ = 次回起動時まで保留)",
			L"An update file is available.\nWould you like to update now?\n(No = postpone until next launch)",
			L"Un fichier de mise a jour est disponible.\nMettre a jour maintenant ?\n(Non = reporter au prochain demarrage)",
			L"E disponibile un file di aggiornamento.\nAggiornare ora?\n(No = rimanda al prossimo avvio)",
			L"Hay un archivo de actualizacion disponible.\n¿Actualizar ahora?\n(No = posponer hasta el proximo inicio)",
			L"업데이트 파일이 있습니다.\n지금 업데이트하시겠습니까?\n(아니요 = 다음 실행 시까지 보류)",
			L"有更新文件。\n是否立即更新？\n(否 = 推迟到下次启动)",
			L"يتوفر ملف تحديث.\nهل تريد التحديث الآن؟\n(لا = التأجيل حتى التشغيل التالي)",
			L"Доступен файл обновления.\nОбновить сейчас?\n(Нет = отложить до следующего запуска)",
			L"Eine Aktualisierungsdatei ist verfugbar.\nJetzt aktualisieren?\n(Nein = bis zum nachsten Start aufschieben)",
			L"Um arquivo de atualizacao esta disponivel.\nAtualizar agora?\n(Nao = adiar ate a proxima inicializacao)",
			L"Er is een updatebestand beschikbaar.\nNu bijwerken?\n(Nee = uitstellen tot volgende start)",
			L"Dostepny jest plik aktualizacji.\nCzy zaktualizowac teraz?\n(Nie = odloz do nastepnego uruchomienia)",
			L"Guncelleme dosyasi mevcut.\nSimdi guncellemek istiyor musunuz?\n(Hayir = sonraki baslatmaya ertele)");
	}
	const int ret = AfxMessageBox(msg, MB_YESNO);

	if (ret == IDYES)
		DoUpdateAndRestart();  // 成功時はプロセス終了、手動展開時/失敗時はそのまま通常起動を続ける
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

	// 起動直後に成功/失敗フラグを一度整理（成功ならクリア）
	UpdateAttemptStillFailed(exeTime);

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

// 更新実行：ダウンロード→展開確認→バッチで置換→再起動
// 前回自動更新が失敗していた場合は「ダウンロード」へ手動展開してアプリは継続
bool DoUpdateAndRestart()
{
	extern TCHAR karento2[1024];
	extern save savedata;

	const time_t exeTimeNow = GetExecutableModificationTimeUtc();
	const bool prevFailed = (savedata.updateAttemptExeTime != 0
		&& exeTimeNow != 0
		&& (__int64)exeTimeNow == savedata.updateAttemptExeTime);

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

	// ダウンロード直前にも再解決（09a 優先）。チェック時は 08g だけでも、この時点で 09a があれば 09a を取る。
	time_t serverTime = 0;
	const CString updateUrl = ResolveUpdateUrl(&serverTime);
	if (updateUrl.IsEmpty())
	{
		AfxMessageBox(LL14(
			L"ダウンロードに失敗しました。\nネットワーク接続を確認してください。",
			L"Download failed.\nPlease check your network connection.",
			L"Telechargement echoue.\nVerifiez votre connexion reseau.",
			L"Download fallito.\nControlla la connessione di rete.",
			L"Descarga fallida.\nCompruebe su conexion de red.",
			L"다운로드에 실패했습니다.\n네트워크 연결을 확인하세요.",
			L"下载失败。\n请检查网络连接。",
			L"فشل التنزيل.\nيرجى التحقق من اتصال الشبكة.",
			L"Не удалось загрузить.\nПроверьте подключение к сети.",
			L"Download fehlgeschlagen.\nBitte Netzwerkverbindung prufen.",
			L"Download falhou.\nVerifique sua conexao de rede.",
			L"Download mislukt.\nControleer uw netwerkverbinding.",
			L"Pobieranie nie powiodlo sie.\nSprawdz polaczenie sieciowe.",
			L"Indirme basarisiz.\nAg baglantisini kontrol edin."));
		return false;
	}

	// 前回の自動上書きが失敗していた → 手動展開フォールバック（アプリは終了しない）
	if (prevFailed)
	{
		DoManualUpdateToDownloads(updateUrl, serverTime);
		return false;
	}

	TCHAR tempPath[MAX_PATH], zipPath[MAX_PATH], extractDir[MAX_PATH], batPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	_stprintf_s(zipPath, _T("%sogg_update.zip"), tempPath);
	_stprintf_s(extractDir, _T("%sogg_update_extract"), tempPath);
	_stprintf_s(batPath, _T("%sogg_updater.bat"), tempPath);

	CreateDirectory(extractDir, NULL);

	if (!HttpDownloadToFile(updateUrl, zipPath))
	{
		AfxMessageBox(LL14(
			L"ダウンロードに失敗しました。\nネットワーク接続を確認してください。",
			L"Download failed.\nPlease check your network connection.",
			L"Telechargement echoue.\nVerifiez votre connexion reseau.",
			L"Download fallito.\nControlla la connessione di rete.",
			L"Descarga fallida.\nCompruebe su conexion de red.",
			L"다운로드에 실패했습니다.\n네트워크 연결을 확인하세요.",
			L"下载失败。\n请检查网络连接。",
			L"فشل التنزيل.\nيرجى التحقق من اتصال الشبكة.",
			L"Не удалось загрузить.\nПроверьте подключение к сети.",
			L"Download fehlgeschlagen.\nBitte Netzwerkverbindung prufen.",
			L"Download falhou.\nVerifique sua conexao de rede.",
			L"Download mislukt.\nControleer uw netwerkverbinding.",
			L"Pobieranie nie powiodlo sie.\nSprawdz polaczenie sieciowe.",
			L"Indirme basarisiz.\nAg baglantisini kontrol edin."));
		return false;
	}

	// 終了前に展開して内容を確認（ZIP破損・サイズ不一致・非PEはここで弾く）
	if (!ExtractZipToDir(zipPath, extractDir, TARGET_EXE_NAME, UPDATE_MAIN_EXE_MIN_BYTES)
		|| !ExtractZipToDir(zipPath, extractDir, TARGET_HOST_EXE_NAME, UPDATE_HOST_EXE_MIN_BYTES))
	{
		DeleteFile(zipPath);
		AfxMessageBox(LL14(
			L"ZIPの展開に失敗しました。\n一時フォルダへの書き込み権限やディスク容量を確認してください。",
			L"Failed to extract the ZIP.\nCheck write permission and disk space in the temp folder.",
			L"Echec de l'extraction du ZIP.\nVerifiez les droits d'ecriture et l'espace disque du dossier temporaire.",
			L"Estrazione ZIP non riuscita.\nControlla autorizzazioni e spazio nel folder temporaneo.",
			L"Error al extraer el ZIP.\nCompruebe permisos y espacio en la carpeta temporal.",
			L"ZIP 압축 해제에 실패했습니다.\n임시 폴더의 쓰기 권한과 디스크 용량을 확인하세요.",
			L"ZIP 解压失败。\n请检查临时文件夹的写入权限和磁盘空间。",
			L"فشل استخراج ZIP.\nيرجى التحقق من أذونات الكتابة ومساحة القرص في المجلد المؤقت.",
			L"Не удалось распаковать ZIP.\nПроверьте права записи и место на диске во временной папке.",
			L"ZIP-Entpacken fehlgeschlagen.\nBitte Schreibrechte und Speicherplatz im Temp-Ordner prufen.",
			L"Falha ao extrair o ZIP.\nVerifique permissao de escrita e espaco na pasta temporaria.",
			L"Uitpakken van ZIP mislukt.\nControleer schrijfrechten en schijfruimte in de temp-map.",
			L"Nie udalo sie rozpakowac ZIP.\nSprawdz uprawnienia i miejsce w folderze tymczasowym.",
			L"ZIP acma basarisiz.\nGecici klasorde yazma izni ve disk alanini kontrol edin."));
		return false;
	}
	// オフラインヘルプ CHM（ZIP に含まれていれば展開。無くても更新は続行）
	ExtractZipToDir(zipPath, extractDir, TARGET_CHM_NAME, UPDATE_CHM_MIN_BYTES, false);

	CString extractedPath;
	extractedPath.Format(_T("%s\\%s"), extractDir, TARGET_EXE_NAME);
	CString extractedHostPath;
	extractedHostPath.Format(_T("%s\\%s"), extractDir, TARGET_HOST_EXE_NAME);

	// 二重確認（展開直後の PE / サイズ）
	if (!IsLikelyPeExe(extractedPath, UPDATE_MAIN_EXE_MIN_BYTES)
		|| !IsLikelyPeExe(extractedHostPath, UPDATE_HOST_EXE_MIN_BYTES))
	{
		DeleteFile(zipPath);
		DeleteFile(extractedPath);
		DeleteFile(extractedHostPath);
		AfxMessageBox(LL14(
			L"展開した更新ファイルが不正です。\nもう一度更新を試すか、ネットワークを確認してください。",
			L"The extracted update files are invalid.\nPlease retry the update or check your network.",
			L"Les fichiers extraits sont invalides.\nReessayez la mise a jour ou verifiez le reseau.",
			L"I file estratti non sono validi.\nRiprova l'aggiornamento o controlla la rete.",
			L"Los archivos extraidos no son validos.\nReintente la actualizacion o compruebe la red.",
			L"풀린 업데이트 파일이 올바르지 않습니다.\n다시 시도하거나 네트워크를 확인하세요.",
			L"解压后的更新文件无效。\n请重试更新或检查网络。",
			L"ملفات التحديث المستخرجة غير صالحة.\nأعد المحاولة أو تحقق من الشبكة.",
			L"Распакованные файлы обновления недействительны.\nПовторите обновление или проверьте сеть.",
			L"Die entpackten Update-Dateien sind ungultig.\nBitte erneut versuchen oder Netzwerk prufen.",
			L"Os arquivos extraidos sao invalidos.\nTente novamente ou verifique a rede.",
			L"De uitgepakte updatebestanden zijn ongeldig.\nProbeer opnieuw of controleer het netwerk.",
			L"Rozpakowane pliki aktualizacji sa nieprawidlowe.\nSprobuj ponownie lub sprawdz siec.",
			L"Acilan guncelleme dosyalari gecersiz.\nYeniden deneyin veya agi kontrol edin."));
		return false;
	}

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
	{
		AfxMessageBox(LL14(
			L"更新用スクリプトの作成に失敗しました。\n一時フォルダへの書き込み権限を確認してください。",
			L"Failed to create the update script.\nCheck write permission in the temp folder.",
			L"Echec de la creation du script de mise a jour.\nVerifiez les droits d'ecriture du dossier temporaire.",
			L"Creazione dello script di aggiornamento non riuscita.\nControlla i permessi della cartella temporanea.",
			L"No se pudo crear el script de actualizacion.\nCompruebe permisos de la carpeta temporal.",
			L"업데이트 스크립트 작성에 실패했습니다.\n임시 폴더 쓰기 권한을 확인하세요.",
			L"无法创建更新脚本。\n请检查临时文件夹的写入权限。",
			L"فشل إنشاء برنامج التحديث.\nيرجى التحقق من أذونات المجلد المؤقت.",
			L"Не удалось создать скрипт обновления.\nПроверьте права записи во временной папке.",
			L"Update-Skript konnte nicht erstellt werden.\nBitte Schreibrechte im Temp-Ordner prufen.",
			L"Falha ao criar o script de atualizacao.\nVerifique a permissao na pasta temporaria.",
			L"Kan updatescript niet maken.\nControleer schrijfrechten in de temp-map.",
			L"Nie udalo sie utworzyc skryptu aktualizacji.\nSprawdz uprawnienia folderu tymczasowego.",
			L"Guncelleme betigi olusturulamadi.\nGecici klasor yazma iznini kontrol edin."));
		return false;
	}

	// 文字化けを起こさないよう変換いたします
	CStringA extractDirA(extractDir);
	CStringA targetExeA(TARGET_EXE_NAME);
	CStringA targetHostExeA(TARGET_HOST_EXE_NAME);
	CStringA targetChmA(TARGET_CHM_NAME);
	CStringA targetExePathA(targetExePath);  // 新しく作る正しい名前のファイル
	CStringA targetHostExePathA(targetHostExePath); // 新しいホストexe
	CStringA targetChmPathA = targetExePathA;
	{
		const int slashChm = targetChmPathA.ReverseFind('\\');
		if (slashChm >= 0)
			targetChmPathA = targetChmPathA.Left(slashChm + 1) + targetChmA;
		else
			targetChmPathA = targetChmA;
	}

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
	// 本体 exe も taskkill 対象に含め、ロック残存による上書き失敗を減らします。
	CStringA batContentA;
	batContentA.Format(
		"@echo off\r\n"
		"setlocal\r\n"
		"if \"%%~1\"==\"worker\" goto worker\r\n"
		"taskkill /IM %s /F >nul 2>&1\r\n"
		"taskkill /IM %s /F >nul 2>&1\r\n"
		"ping -n 4 127.0.0.1 >nul\r\n"
		"taskkill /IM %s /F >nul 2>&1\r\n"
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
		"taskkill /IM %s /F >nul 2>&1\r\n"
		"copy /y \"%s\\%s\" \"%s\" >nul 2>&1\r\n"
		"if errorlevel 1 goto retry\r\n"
		"copy /y \"%s\\%s\" \"%s\" >nul 2>&1\r\n"
		"if errorlevel 1 goto retry\r\n"
		"if exist \"%s\\%s\" copy /y \"%s\\%s\" \"%s\" >nul 2>&1\r\n"
		"goto :eof\r\n"
		":retry\r\n"
		"set /a RETRY+=1\r\n"
		"if %%RETRY%% geq 20 goto :eof\r\n"
		"goto wait\r\n",
		(LPCSTR)targetExeA,                                           // 起動直後の停止（本体）
		(LPCSTR)targetHostExeA,                                       // 起動直後の停止（host）
		(LPCSTR)targetExeA,                                           // 再度の停止（本体）
		(LPCSTR)targetHostExeA,                                       // 再度の停止（host）
		(LPCSTR)exeDirA,                                              // 書込可否テスト先フォルダ
		(LPCSTR)exeDirA,                                              // 再起動前の移動先
		(LPCSTR)targetExePathA,                                       // 再起動する本体
		(LPCSTR)cmdArgsA,                                             // 元の起動引数（関連付けファイル等）
		(LPCSTR)targetExeA,                                           // worker内の停止（本体）
		(LPCSTR)targetHostExeA,                                       // worker内の停止（host）
		(LPCSTR)extractDirA, (LPCSTR)targetHostExeA, (LPCSTR)targetHostExePathA, // host 上書き
		(LPCSTR)extractDirA, (LPCSTR)targetExeA, (LPCSTR)targetExePathA,          // 本体 上書き
		(LPCSTR)extractDirA, (LPCSTR)targetChmA, (LPCSTR)extractDirA, (LPCSTR)targetChmA, (LPCSTR)targetChmPathA // CHM（あれば・失敗してもOK）
	);

	bat.Write(batContentA, batContentA.GetLength());
	bat.Close();

	// 終了前に「試行直前の exe 日時」を保存。次回起動で日時が変わっていなければ上書き失敗。
	if (exeTimeNow != 0)
	{
		savedata.updateAttemptExeTime = (__int64)exeTimeNow;
		MpPersistSavedataQuick();
	}

	// 命令書（バッチファイル）の実行
	const HINSTANCE hShell = ShellExecute(NULL, _T("open"), batPath, NULL, tempPath, SW_HIDE);
	if ((INT_PTR)hShell <= 32)
	{
		savedata.updateAttemptExeTime = 0;
		MpPersistSavedataQuick();
		AfxMessageBox(LL14(
			L"更新用スクリプトの起動に失敗しました。",
			L"Failed to start the update script.",
			L"Echec du demarrage du script de mise a jour.",
			L"Avvio dello script di aggiornamento non riuscito.",
			L"No se pudo iniciar el script de actualizacion.",
			L"업데이트 스크립트를 시작하지 못했습니다.",
			L"无法启动更新脚本。",
			L"فشل تشغيل برنامج التحديث.",
			L"Не удалось запустить скрипт обновления.",
			L"Update-Skript konnte nicht gestartet werden.",
			L"Falha ao iniciar o script de atualizacao.",
			L"Kan updatescript niet starten.",
			L"Nie udalo sie uruchomic skryptu aktualizacji.",
			L"Guncelleme betigi baslatilamadi."));
		return false;
	}

	// アプリケーションを終了させますわ
	exit(0);

	return true;
}
