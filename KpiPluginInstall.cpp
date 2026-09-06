// KpiPluginInstall.cpp — Plugins.zip を公式配布から取得し exe 隣へ展開
#include "stdafx.h"
#include "KpiPluginInstall.h"
#include <wininet.h>
#include "minizip/unzip.h"
#include "minizip/iowin32.h"

#pragma comment(lib, "wininet.lib")

static void KpiInstallPump()
{
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			PostQuitMessage((int)msg.wParam);
			break;
		}
		if (msg.message == WM_TIMER)
			continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static BOOL KpiInstallMkDirDeep(const TCHAR* path)
{
	if (!path || !path[0]) return FALSE;
	TCHAR buf[MAX_PATH * 2] = {};
	_tcsncpy(buf, path, _countof(buf) - 1);
	for (TCHAR* p = buf; *p; ++p) {
		if (*p == L'/' || *p == L'\\') {
			const TCHAR c = *p;
			*p = 0;
			if (buf[0] && _tcslen(buf) > 2)
				CreateDirectory(buf, NULL);
			*p = c;
		}
	}
	CreateDirectory(buf, NULL);
	return TRUE;
}

static time_t KpiInstallFileMtimeUtc(LPCTSTR path);

static BOOL KpiInstallSafeRelPath(const char* nameInZip, CString& outRel)
{
	outRel.Empty();
	if (!nameInZip || !nameInZip[0]) return FALSE;
	CStringA a(nameInZip);
	a.Replace('/', '\\');
	if (a.Find("..") >= 0) return FALSE;
	if (a.GetLength() >= 2 && a[1] == ':') return FALSE;
	while (a.GetLength() > 0 && (a[0] == '\\' || a[0] == '/'))
		a = a.Mid(1);
	if (a.IsEmpty()) return FALSE;
	outRel = CString(a);
	return TRUE;
}

/* ZIP エントリ日時（DOS/tmu）→ time_t。比較用（タイムゾーン差は許容）。 */
static time_t KpiInstallZipInfoMtimeUtc(const unz_file_info64& fi)
{
	if (fi.tmu_date.tm_year < 1980 || fi.tmu_date.tm_mon < 0 || fi.tmu_date.tm_mon > 11
		|| fi.tmu_date.tm_mday < 1 || fi.tmu_date.tm_mday > 31)
		return 0;
	SYSTEMTIME st = {};
	st.wYear = (WORD)fi.tmu_date.tm_year;
	st.wMonth = (WORD)(fi.tmu_date.tm_mon + 1);
	st.wDay = (WORD)fi.tmu_date.tm_mday;
	st.wHour = (WORD)fi.tmu_date.tm_hour;
	st.wMinute = (WORD)fi.tmu_date.tm_min;
	st.wSecond = (WORD)fi.tmu_date.tm_sec;
	FILETIME ft = {};
	if (!SystemTimeToFileTime(&st, &ft))
		return 0;
	ULARGE_INTEGER ull;
	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;
	if (ull.QuadPart < 116444736000000000ULL)
		return 0;
	return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

/* 退避・無効化ツリーは展開しない（重複 KPI/古い DLL を戻さない） */
static BOOL KpiInstallZipRelIsJunk(const CString& rel)
{
	CString u(rel);
	u.MakeLower();
	if (u.Find(L"_fmmon_bak") >= 0) return TRUE;
	if (u.Find(L"_fmmon_kpi_bak") >= 0) return TRUE;
	if (u.Find(L".ogg_unused") >= 0) return TRUE;
	return FALSE;
}

/* mergeNewerOnly: 無い→追加、ZIP の方が新しい→上書き。ローカルが新しければスキップ。 */
static BOOL KpiInstallExtractZip(const TCHAR* zipPath, const TCHAR* destDir, CString& errOut, BOOL mergeNewerOnly)
{
	errOut.Empty();
	zlib_filefunc64_def ffunc = {};
	fill_win32_filefunc64W(&ffunc);
	unzFile uf = unzOpen2_64(zipPath, &ffunc);
	if (!uf) {
		errOut = LL14(L"ZIP を開けません。", L"Could not open ZIP.", L"ZIP illisible.",
			L"ZIP non apribile.", L"No se pudo abrir el ZIP.", L"ZIP을 열 수 없습니다.",
			L"无法打开 ZIP。", L"تعذر فتح ZIP.", L"Не удалось открыть ZIP.", L"ZIP nicht offenbar.",
			L"Nao foi possivel abrir o ZIP.", L"ZIP openen mislukt.", L"Nie mozna otworzyc ZIP.",
			L"ZIP acilamadi.");
		return FALSE;
	}
	unz_global_info64 gi = {};
	if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) {
		unzClose(uf);
		errOut = LL14(L"ZIP が不正です。", L"Invalid ZIP.", L"ZIP invalide.", L"ZIP non valido.",
			L"ZIP invalido.", L"ZIP이 올바르지 않습니다.", L"ZIP 无效。", L"ZIP غير صالح.",
			L"Некорректный ZIP.", L"ZIP ungultig.", L"ZIP invalido.", L"Ongeldige ZIP.",
			L"Nieprawidlowy ZIP.", L"Gecersiz ZIP.");
		return FALSE;
	}

	int extracted = 0;
	for (ZPOS64_T i = 0; i < gi.number_entry; i++) {
		char filename_inzip[1024] = {};
		unz_file_info64 fi = {};
		if (unzGetCurrentFileInfo64(uf, &fi, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK)
			break;
		CString rel;
		if (!KpiInstallSafeRelPath(filename_inzip, rel) || KpiInstallZipRelIsJunk(rel)) {
			if ((ZPOS64_T)(i + 1) < gi.number_entry) unzGoToNextFile(uf);
			continue;
		}
		const BOOL isDir = (rel.Right(1) == L"\\" || rel.Right(1) == L"/"
			|| (fi.external_fa & FILE_ATTRIBUTE_DIRECTORY) != 0);
		CString outPath;
		outPath.Format(L"%s\\%s", destDir, (LPCTSTR)rel);
		if (isDir) {
			KpiInstallMkDirDeep(outPath);
			if ((ZPOS64_T)(i + 1) < gi.number_entry) unzGoToNextFile(uf);
			continue;
		}
		if (mergeNewerOnly) {
			const time_t tDst = KpiInstallFileMtimeUtc(outPath);
			if (tDst != 0) {
				const time_t tZip = KpiInstallZipInfoMtimeUtc(fi);
				/* ローカルが ZIP 以上に新しい → スキップ（自前ビルド KPI/DLL を守る） */
				if (tZip == 0 || tZip <= tDst + 2) {
					if ((ZPOS64_T)(i + 1) < gi.number_entry) unzGoToNextFile(uf);
					continue;
				}
			}
		}
		{
			const int slash = outPath.ReverseFind(L'\\');
			if (slash > 0)
				KpiInstallMkDirDeep(outPath.Left(slash));
		}
		if (unzOpenCurrentFile(uf) != UNZ_OK) {
			if ((ZPOS64_T)(i + 1) < gi.number_entry) unzGoToNextFile(uf);
			continue;
		}
		DeleteFile(outPath);
		HANDLE hOut = CreateFile(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		BOOL writeOk = FALSE;
		ULONGLONG written = 0;
		if (hOut != INVALID_HANDLE_VALUE) {
			writeOk = TRUE;
			char buf[8192];
			int n;
			while ((n = unzReadCurrentFile(uf, buf, sizeof(buf))) > 0) {
				DWORD wr = 0;
				if (!WriteFile(hOut, buf, (DWORD)n, &wr, NULL) || wr != (DWORD)n) {
					writeOk = FALSE;
					break;
				}
				written += (ULONGLONG)n;
			}
			if (n < 0) writeOk = FALSE;
			CloseHandle(hOut);
		}
		unzCloseCurrentFile(uf);
		if (!writeOk || (fi.uncompressed_size > 0 && written != (ULONGLONG)fi.uncompressed_size)) {
			DeleteFile(outPath);
		} else {
			extracted++;
			/* ZIP 日時を残すと次回マージ判定が安定する */
			const time_t tZip = KpiInstallZipInfoMtimeUtc(fi);
			if (tZip != 0) {
				FILETIME ft;
				ULARGE_INTEGER ull;
				ull.QuadPart = ((ULONGLONG)tZip * 10000000ULL) + 116444736000000000ULL;
				ft.dwLowDateTime = ull.LowPart;
				ft.dwHighDateTime = ull.HighPart;
				HANDLE h2 = CreateFile(outPath, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (h2 != INVALID_HANDLE_VALUE) {
					SetFileTime(h2, NULL, NULL, &ft);
					CloseHandle(h2);
				}
			}
		}
		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	/* マージ時は「全部スキップ」も成功（既に最新） */
	if (extracted <= 0 && !mergeNewerOnly) {
		errOut = LL14(L"ZIP からファイルを展開できませんでした。", L"Could not extract files from ZIP.",
			L"Extraction ZIP impossible.", L"Estrazione ZIP non riuscita.", L"No se pudo extraer el ZIP.",
			L"ZIP에서 파일을 펼 수 없습니다.", L"无法从 ZIP 解压文件。", L"تعذر استخراج الملفات من ZIP.",
			L"Не удалось распаковать ZIP.", L"ZIP konnte nicht entpackt werden.",
			L"Nao foi possivel extrair o ZIP.", L"ZIP uitpakken mislukt.", L"Nie mozna rozpakowac ZIP.",
			L"ZIP acilamadi.");
		return FALSE;
	}
	return TRUE;
}

BOOL KpiInstall_DownloadAndExtract(LPCTSTR exeDir, KpiInstallProgressFn progress, void* ctx, CString& errOut)
{
	errOut.Empty();
	if (!exeDir || !exeDir[0]) {
		errOut = LL14(L"配置先が不正です。", L"Invalid destination.", L"Destination invalide.",
			L"Destinazione non valida.", L"Destino invalido.", L"배치 위치가 올바르지 않습니다.",
			L"目标路径无效。", L"الوجهة غير صالحة.", L"Некорректный путь.", L"Ziel ungultig.",
			L"Destino invalido.", L"Ongeldige bestemming.", L"Nieprawidlowa sciezka.", L"Hedef gecersiz.");
		return FALSE;
	}

	TCHAR tmp[MAX_PATH] = {};
	GetTempPath(MAX_PATH, tmp);
	TCHAR zipPath[MAX_PATH] = {};
	_sntprintf_s(zipPath, _TRUNCATE, L"%sogg_kpi_Plugins.zip", tmp);
	DeleteFile(zipPath);

	if (progress) progress(0, ctx);

	HINTERNET hInet = InternetOpen(L"oggKpiPluginInstall/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInet) {
		errOut = LL14(L"ネットワークを初期化できません。", L"Could not init network.", L"Reseau indisponible.",
			L"Rete non disponibile.", L"Red no disponible.", L"네트워크 초기화 실패.", L"无法初始化网络。",
			L"تعذر تهيئة الشبكة.", L"Сеть недоступна.", L"Netzwerk nicht initialisierbar.",
			L"Nao foi possivel iniciar a rede.", L"Netwerk starten mislukt.", L"Nie mozna zainicjowac sieci.",
			L"Ag baslatilamadi.");
		return FALSE;
	}
	DWORD timeout = 600000;
	InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	const TCHAR* url = L"https://ppp.oohara.jp/download/Plugins.zip";
	const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
		| INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_UI
		| INTERNET_FLAG_SECURE
		| INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID;
	HINTERNET hUrl = InternetOpenUrl(hInet, url, NULL, 0, flags, 0);
	if (!hUrl) {
		InternetCloseHandle(hInet);
		errOut = LL14(L"ダウンロードに失敗しました。", L"Download failed.", L"Echec du telechargement.",
			L"Download non riuscito.", L"Fallo la descarga.", L"다운로드 실패.", L"下载失败。",
			L"فشل التنزيل.", L"Сбой загрузки.", L"Download fehlgeschlagen.",
			L"Falha no download.", L"Download mislukt.", L"Pobieranie nieudane.", L"Indirme basarisiz.");
		return FALSE;
	}

	DWORD status = 0, slen = sizeof(status);
	if (!HttpQueryInfo(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, NULL)
		|| status < 200 || status >= 300) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		errOut = LL14(L"ダウンロードに失敗しました（サーバー応答エラー）。",
			L"Download failed (server error).", L"Echec du telechargement (serveur).",
			L"Download non riuscito (server).", L"Fallo la descarga (servidor).",
			L"다운로드 실패(서버 오류).", L"下载失败（服务器错误）。", L"فشل التنزيل (خطأ بالخادم).",
			L"Сбой загрузки (ошибка сервера).", L"Download fehlgeschlagen (Server).",
			L"Falha no download (servidor).", L"Download mislukt (server).",
			L"Pobieranie nieudane (serwer).", L"Indirme basarisiz (sunucu).");
		return FALSE;
	}

	ULONGLONG contentLen = 0;
	{
		char lenBuf[64] = {};
		DWORD lenBufLen = sizeof(lenBuf);
		if (HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH, lenBuf, &lenBufLen, NULL))
			contentLen = (ULONGLONG)_atoi64(lenBuf);
	}

	HANDLE hFile = CreateFile(zipPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		errOut = LL14(L"一時ファイルを作成できません。", L"Could not create temp file.", L"Fichier temporaire impossible.",
			L"Impossibile creare file temporaneo.", L"No se pudo crear archivo temporal.",
			L"임시 파일을 만들 수 없습니다.", L"无法创建临时文件。", L"تعذر إنشاء ملف مؤقت.",
			L"Не удалось создать временный файл.", L"Temp-Datei nicht erstellbar.",
			L"Nao foi possivel criar arquivo temporario.", L"Tijdelijk bestand maken mislukt.",
			L"Nie mozna utworzyc pliku tymczasowego.", L"Gecici dosya olusturulamadi.");
		return FALSE;
	}

	BYTE buf[16384];
	DWORD read = 0;
	ULONGLONG total = 0;
	DWORD lastUi = GetTickCount();
	BOOL ok = TRUE;
	while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
		DWORD wr = 0;
		if (!WriteFile(hFile, buf, read, &wr, NULL) || wr != read) {
			ok = FALSE;
			break;
		}
		total += read;
		const DWORD now = GetTickCount();
		if (progress && (now - lastUi) >= 200) {
			lastUi = now;
			int pct = 0;
			if (contentLen > 0)
				pct = (int)((total * 90ULL) / contentLen);
			else
				pct = (int)((total / (256ULL * 1024ULL)) % 90);
			if (pct > 90) pct = 90;
			if (pct < 0) pct = 0;
			progress(pct, ctx);
			KpiInstallPump();
		}
	}
	CloseHandle(hFile);
	InternetCloseHandle(hUrl);
	InternetCloseHandle(hInet);

	if (!ok || total < 1000ULL) {
		DeleteFile(zipPath);
		errOut = LL14(L"ダウンロードが完了しませんでした。", L"Download incomplete.", L"Telechargement incomplet.",
			L"Download incompleto.", L"Descarga incompleta.", L"다운로드가 완료되지 않았습니다.",
			L"下载未完成。", L"التنزيل غير مكتمل.", L"Загрузка не завершена.", L"Download unvollstandig.",
			L"Download incompleto.", L"Download onvolledig.", L"Pobieranie niekompletne.", L"Indirme tamamlanmadi.");
		return FALSE;
	}

	if (progress) {
		progress(92, ctx);
		KpiInstallPump();
	}
	ok = KpiInstallExtractZip(zipPath, exeDir, errOut, FALSE);
	DeleteFile(zipPath);
	if (progress) {
		progress(ok ? 100 : 92, ctx);
		KpiInstallPump();
	}
	return ok;
}

// ---------------------------------------------------------------------------
// Plugins.zip サイレント更新（kbsasami / fmpmd / 依存 DLL 共通）
// ZIP: https://ppp.oohara.jp/download/Plugins.zip
// DL 条件: Plugins フォルダ無し → 無条件 / ZIP Last-Modified が exe より新しい
// それ以外は DL しない（HEAD 相当の日時確認のみ）
// ---------------------------------------------------------------------------

static const TCHAR* PLUGINS_ZIP_URL = L"https://ppp.oohara.jp/download/Plugins.zip";

static time_t KpiInstallFileMtimeUtc(LPCTSTR path)
{
	if (!path || !path[0]) return 0;
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
		return 0;
	ULARGE_INTEGER ull;
	ull.LowPart = fad.ftLastWriteTime.dwLowDateTime;
	ull.HighPart = fad.ftLastWriteTime.dwHighDateTime;
	if (ull.QuadPart < 116444736000000000ULL)
		return 0;
	return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

// Last-Modified（UTC）。失敗は 0。期限切れ証明書のサイト向けに DATE/CN 無視。
static time_t KpiInstallHttpLastModified(LPCTSTR url)
{
	if (!url || !url[0]) return 0;
	HINTERNET hInet = InternetOpen(L"oggKbsasamiUpdate/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInet) return 0;
	DWORD timeout = 8000;
	InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

	CString noCache;
	noCache.Format(L"%s?t=%lld", url, (long long)time(NULL));
	const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE
		| INTERNET_FLAG_SECURE
		| INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID
		| INTERNET_FLAG_NO_UI;
	HINTERNET hUrl = InternetOpenUrl(hInet, noCache, NULL, 0, flags, 0);
	if (!hUrl) {
		InternetCloseHandle(hInet);
		return 0;
	}
	DWORD status = 0, slen = sizeof(status);
	if (!HttpQueryInfo(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, NULL)
		|| status != 200) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		return 0;
	}
	time_t result = 0;
	char rawDate[256] = {};
	DWORD rawLen = sizeof(rawDate);
	if (HttpQueryInfoA(hUrl, HTTP_QUERY_LAST_MODIFIED, rawDate, &rawLen, NULL)) {
		SYSTEMTIME st = {};
		if (InternetTimeToSystemTimeA(rawDate, &st, 0)) {
			FILETIME ft;
			SystemTimeToFileTime(&st, &ft);
			ULARGE_INTEGER ull;
			ull.LowPart = ft.dwLowDateTime;
			ull.HighPart = ft.dwHighDateTime;
			result = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
		}
	}
	InternetCloseHandle(hUrl);
	InternetCloseHandle(hInet);
	return result;
}

static BOOL KpiInstallHttpDownloadFile(LPCTSTR url, LPCTSTR destPath)
{
	if (!url || !destPath) return FALSE;
	DeleteFile(destPath);
	HINTERNET hInet = InternetOpen(L"oggKbsasamiUpdate/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInet) return FALSE;
	DWORD timeout = 120000;
	InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	CString noCache;
	noCache.Format(L"%s?t=%lld", url, (long long)time(NULL));
	const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE
		| INTERNET_FLAG_SECURE | INTERNET_FLAG_KEEP_CONNECTION
		| INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID
		| INTERNET_FLAG_NO_UI;
	HINTERNET hUrl = InternetOpenUrl(hInet, noCache, NULL, 0, flags, 0);
	if (!hUrl) {
		InternetCloseHandle(hInet);
		return FALSE;
	}
	DWORD status = 0, slen = sizeof(status);
	if (!HttpQueryInfo(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, NULL)
		|| status < 200 || status >= 300) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		return FALSE;
	}
	HANDLE hFile = CreateFile(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		return FALSE;
	}
	BYTE buf[16384];
	DWORD read = 0;
	ULONGLONG total = 0;
	BOOL ok = TRUE;
	while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
		DWORD wr = 0;
		if (!WriteFile(hFile, buf, read, &wr, NULL) || wr != read) {
			ok = FALSE;
			break;
		}
		total += read;
		KpiInstallPump();
	}
	CloseHandle(hFile);
	InternetCloseHandle(hUrl);
	InternetCloseHandle(hInet);
	if (!ok || total < 1000ULL) {
		DeleteFile(destPath);
		return FALSE;
	}
	return TRUE;
}

static void KpiInstallEnsureRhythmAssets(LPCTSTR exeDir);

/* 起動1回だけ Plugins.zip を検討（kbsasami / fmpmd の二重 DL 防止） */
static BOOL KpiInstallSilentMaybeFetchPluginsZip(LPCTSTR exeDir)
{
	static BOOL s_done = FALSE;
	static BOOL s_did = FALSE;
	if (s_done)
		return s_did;
	s_done = TRUE;

	if (!exeDir || !exeDir[0])
		return FALSE;

	DWORD netFlags = 0;
	if (!InternetGetConnectedState(&netFlags, 0))
		return FALSE;

	TCHAR pluginsDir[MAX_PATH * 2] = {};
	_sntprintf_s(pluginsDir, _TRUNCATE, L"%sPlugins", exeDir);
	const BOOL pluginsMissing =
		(GetFileAttributes(pluginsDir) == INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;

	TCHAR exePath[MAX_PATH] = {};
	GetModuleFileName(NULL, exePath, MAX_PATH);
	const time_t exeMt = KpiInstallFileMtimeUtc(exePath);

	BOOL doDownload = FALSE;
	if (pluginsMissing) {
		/* Plugins 自体が無い → 無条件で取得 */
		doDownload = TRUE;
	} else {
		const time_t serverMod = KpiInstallHttpLastModified(PLUGINS_ZIP_URL);
		/* ZIP が exe より新しければ更新。判定不能・古ければ DL しない */
		if (serverMod != 0 && exeMt != 0 && serverMod > exeMt + 120)
			doDownload = TRUE;
	}

	if (!doDownload)
		return FALSE;

	TCHAR tmp[MAX_PATH] = {};
	GetTempPath(MAX_PATH, tmp);
	TCHAR zipPath[MAX_PATH] = {};
	_sntprintf_s(zipPath, _TRUNCATE, L"%sogg_kpi_Plugins_silent.zip", tmp);
	if (!KpiInstallHttpDownloadFile(PLUGINS_ZIP_URL, zipPath))
		return FALSE;

	CString err;
	/* 無い→追加、ZIP が新しい→上書き。自前の新しい KPI/DLL は残す */
	const BOOL ok = KpiInstallExtractZip(zipPath, exeDir, err, TRUE);
	DeleteFile(zipPath);
	s_did = ok ? TRUE : FALSE;
	return s_did;
}

BOOL KpiInstall_SilentUpdateKbsasami(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0])
		return FALSE;
	/* kbsasami も Plugins.zip 同梱分から展開（専用 kbsasami.zip は使わない） */
	const BOOL did = KpiInstallSilentMaybeFetchPluginsZip(exeDir);
	KpiInstallEnsureRhythmAssets(exeDir);
	return did;
}


static const TCHAR* C60_WINFMP_ZIP_URL = L"https://c60.la.coocan.jp/download/WinFMP052.zip";
static const TCHAR* AOSOFT_PDZF_ZIP_URL = L"https://aosoft.jp/cgi-bin/download.cgi?file=pdzfz8x_Release_v2.0.1.zip";

#ifndef IMAGE_FILE_MACHINE_AMD64
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#endif
#ifndef IMAGE_FILE_MACHINE_I386
#define IMAGE_FILE_MACHINE_I386 0x014c
#endif

static void KpiInstallEnsureFmpmdDirs(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0]) return;
	TCHAR p[MAX_PATH * 2] = {};
	_sntprintf_s(p, _TRUNCATE, L"%sPlugins", exeDir);
	CreateDirectory(p, NULL);
	_sntprintf_s(p, _TRUNCATE, L"%sPlugins\\Kobarin", exeDir);
	CreateDirectory(p, NULL);
	_sntprintf_s(p, _TRUNCATE, L"%sPlugins\\Kobarin\\fmpmd", exeDir);
	CreateDirectory(p, NULL);
	_sntprintf_s(p, _TRUNCATE, L"%sPlugins\\Kobarin\\fmpmd\\Rhythm", exeDir);
	CreateDirectory(p, NULL);
	_sntprintf_s(p, _TRUNCATE, L"%sPlugins\\kbsasami", exeDir);
	CreateDirectory(p, NULL);
}

static BOOL KpiInstallFileExists(LPCTSTR path)
{
	return (path && path[0] && GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

/* Plugins.zip 同梱のリズム資産（既存配布物）。欠落時のみ補完する */
static void KpiInstallCopyIfMissing(LPCTSTR src, LPCTSTR dst)
{
	if (!KpiInstallFileExists(src) || !dst || !dst[0]) return;
	if (KpiInstallFileExists(dst)) return;
	{
		const size_t n = _tcslen(dst);
		for (size_t i = n; i > 0; --i) {
			if (dst[i - 1] == L'\\' || dst[i - 1] == L'/') {
				TCHAR dir[MAX_PATH * 2] = {};
				_tcsncpy_s(dir, dst, i - 1);
				KpiInstallMkDirDeep(dir);
				break;
			}
		}
	}
	CopyFile(src, dst, TRUE);
}

/* fmpmd 内の rom/wav を揃え、kbsasami へも欠落分だけミラー（SASAMI 探索用） */
static void KpiInstallEnsureRhythmAssets(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0]) return;
	TCHAR fmpmd[MAX_PATH * 2] = {};
	TCHAR sasami[MAX_PATH * 2] = {};
	_sntprintf_s(fmpmd, _TRUNCATE, L"%sPlugins\\Kobarin\\fmpmd", exeDir);
	_sntprintf_s(sasami, _TRUNCATE, L"%sPlugins\\kbsasami", exeDir);
	CreateDirectory(fmpmd, NULL);
	{
		TCHAR rhy[MAX_PATH * 2] = {};
		_sntprintf_s(rhy, _TRUNCATE, L"%s\\Rhythm", fmpmd);
		CreateDirectory(rhy, NULL);
	}
	CreateDirectory(sasami, NULL);

	/* ルート ↔ Rhythm で足りない側へコピー */
	{
		TCHAR rom[MAX_PATH * 2] = {}, romR[MAX_PATH * 2] = {};
		_sntprintf_s(rom, _TRUNCATE, L"%s\\ym2608_adpcm_rom.bin", fmpmd);
		_sntprintf_s(romR, _TRUNCATE, L"%s\\Rhythm\\ym2608_adpcm_rom.bin", fmpmd);
		if (KpiInstallFileExists(rom) && !KpiInstallFileExists(romR))
			CopyFile(rom, romR, TRUE);
		else if (KpiInstallFileExists(romR) && !KpiInstallFileExists(rom))
			CopyFile(romR, rom, TRUE);
	}

	static const TCHAR* kNames[6] = { L"BD", L"SD", L"TOP", L"HH", L"TOM", L"RIM" };
	for (int i = 0; i < 6; i++) {
		TCHAR root[MAX_PATH * 2] = {}, rhy[MAX_PATH * 2] = {}, wav[MAX_PATH * 2] = {};
		TCHAR rimAlt[MAX_PATH * 2] = {};
		_sntprintf_s(root, _TRUNCATE, L"%s\\2608_%s.WAV", fmpmd, kNames[i]);
		_sntprintf_s(rhy, _TRUNCATE, L"%s\\Rhythm\\2608_%s.WAV", fmpmd, kNames[i]);
		_sntprintf_s(wav, _TRUNCATE, L"%s\\wav\\2608_%s.WAV", fmpmd, kNames[i]);
		if (i == 5)
			_sntprintf_s(rimAlt, _TRUNCATE, L"%s\\Rhythm\\2608_RiM.WAV", fmpmd);

		TCHAR src[MAX_PATH * 2] = {};
		if (KpiInstallFileExists(rhy)) _tcscpy_s(src, rhy);
		else if (KpiInstallFileExists(wav)) _tcscpy_s(src, wav);
		else if (KpiInstallFileExists(root)) _tcscpy_s(src, root);
		else if (i == 5 && KpiInstallFileExists(rimAlt)) _tcscpy_s(src, rimAlt);

		if (src[0]) {
			KpiInstallCopyIfMissing(src, rhy);
			KpiInstallCopyIfMissing(src, root);
			/* RiM → RIM 正規名も用意（探索名が RIM） */
			if (i == 5 && _tcsicmp(src, rhy) != 0)
				KpiInstallCopyIfMissing(src, rhy);
		}
	}

	/* SASAMI: Plugins\kbsasami にも欠落分だけ */
	{
		TCHAR romSrc[MAX_PATH * 2] = {}, romDst[MAX_PATH * 2] = {};
		_sntprintf_s(romSrc, _TRUNCATE, L"%s\\ym2608_adpcm_rom.bin", fmpmd);
		_sntprintf_s(romDst, _TRUNCATE, L"%s\\ym2608_adpcm_rom.bin", sasami);
		if (!KpiInstallFileExists(romSrc))
			_sntprintf_s(romSrc, _TRUNCATE, L"%s\\Rhythm\\ym2608_adpcm_rom.bin", fmpmd);
		KpiInstallCopyIfMissing(romSrc, romDst);

		TCHAR sasRhy[MAX_PATH * 2] = {};
		_sntprintf_s(sasRhy, _TRUNCATE, L"%s\\Rhythm", sasami);
		CreateDirectory(sasRhy, NULL);
		for (int i = 0; i < 6; i++) {
			TCHAR src[MAX_PATH * 2] = {}, dst[MAX_PATH * 2] = {}, dstR[MAX_PATH * 2] = {};
			_sntprintf_s(src, _TRUNCATE, L"%s\\Rhythm\\2608_%s.WAV", fmpmd, kNames[i]);
			if (!KpiInstallFileExists(src))
				_sntprintf_s(src, _TRUNCATE, L"%s\\2608_%s.WAV", fmpmd, kNames[i]);
			if (!KpiInstallFileExists(src) && i == 5)
				_sntprintf_s(src, _TRUNCATE, L"%s\\Rhythm\\2608_RiM.WAV", fmpmd);
			_sntprintf_s(dst, _TRUNCATE, L"%s\\2608_%s.WAV", sasami, kNames[i]);
			_sntprintf_s(dstR, _TRUNCATE, L"%s\\Rhythm\\2608_%s.WAV", sasami, kNames[i]);
			KpiInstallCopyIfMissing(src, dst);
			KpiInstallCopyIfMissing(src, dstR);
		}
	}
}

/* PE Machine（AMD64/I386）。失敗は 0 */
static WORD KpiInstallPeMachine(LPCTSTR path)
{
	if (!path || !path[0]) return 0;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	IMAGE_DOS_HEADER dos = {};
	DWORD rd = 0;
	WORD machine = 0;
	if (ReadFile(h, &dos, sizeof(dos), &rd, NULL) && rd == sizeof(dos)
		&& dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0) {
		LARGE_INTEGER off;
		off.QuadPart = (LONGLONG)dos.e_lfanew;
		if (SetFilePointerEx(h, off, NULL, FILE_BEGIN)) {
			DWORD sig = 0;
			IMAGE_FILE_HEADER fh = {};
			if (ReadFile(h, &sig, sizeof(sig), &rd, NULL) && rd == sizeof(sig)
				&& sig == IMAGE_NT_SIGNATURE
				&& ReadFile(h, &fh, sizeof(fh), &rd, NULL) && rd == sizeof(fh))
				machine = fh.Machine;
		}
	}
	CloseHandle(h);
	return machine;
}

/* fmpmd 同梱 KPI の arch。kpi64 ホスト想定でデフォルト AMD64 */
static WORD KpiInstallWantedFmpmdMachine(LPCTSTR fmpmdDir)
{
	TCHAR pmd[MAX_PATH * 2] = {};
	TCHAR fmp[MAX_PATH * 2] = {};
	_sntprintf_s(pmd, _TRUNCATE, L"%s\\kbpmd.kpi", fmpmdDir);
	_sntprintf_s(fmp, _TRUNCATE, L"%s\\kbfmp.kpi", fmpmdDir);
	const WORD mPmd = KpiInstallPeMachine(pmd);
	const WORD mFmp = KpiInstallPeMachine(fmp);
	if (mPmd == IMAGE_FILE_MACHINE_AMD64 || mFmp == IMAGE_FILE_MACHINE_AMD64)
		return IMAGE_FILE_MACHINE_AMD64;
	if (mPmd == IMAGE_FILE_MACHINE_I386 || mFmp == IMAGE_FILE_MACHINE_I386)
		return IMAGE_FILE_MACHINE_I386;
	return IMAGE_FILE_MACHINE_AMD64;
}

static BOOL KpiInstallDllNeedsArchFix(LPCTSTR path, WORD want)
{
	if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES)
		return TRUE;
	const WORD m = KpiInstallPeMachine(path);
	return (m == 0 || m != want) ? TRUE : FALSE;
}

/* PE をマップして export 名の有無だけ見る（x64 DLL を Win32 本体から LoadLibrary できない） */
static BOOL KpiInstallPeHasExportA(LPCTSTR path, const char* exportName)
{
	if (!path || !path[0] || !exportName || !exportName[0]) return FALSE;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return FALSE;
	HANDLE map = CreateFileMapping(h, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!map) { CloseHandle(h); return FALSE; }
	const BYTE* base = (const BYTE*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
	BOOL found = FALSE;
	if (base) {
		const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
		if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0) {
			const BYTE* ntBase = base + dos->e_lfanew;
			const DWORD sig = *(const DWORD*)ntBase;
			if (sig == IMAGE_NT_SIGNATURE) {
				const IMAGE_FILE_HEADER* fh = (const IMAGE_FILE_HEADER*)(ntBase + 4);
				const WORD optMagic = *(const WORD*)(ntBase + 4 + sizeof(IMAGE_FILE_HEADER));
				DWORD expRva = 0;
				const IMAGE_SECTION_HEADER* sec = NULL;
				UINT nsec = fh->NumberOfSections;
				if (optMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
					const IMAGE_NT_HEADERS64* n64 = (const IMAGE_NT_HEADERS64*)ntBase;
					if (n64->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXPORT)
						expRva = n64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
					sec = (const IMAGE_SECTION_HEADER*)((const BYTE*)&n64->OptionalHeader
						+ n64->FileHeader.SizeOfOptionalHeader);
				} else if (optMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
					const IMAGE_NT_HEADERS32* n32 = (const IMAGE_NT_HEADERS32*)ntBase;
					if (n32->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXPORT)
						expRva = n32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
					sec = (const IMAGE_SECTION_HEADER*)((const BYTE*)&n32->OptionalHeader
						+ n32->FileHeader.SizeOfOptionalHeader);
				}
				auto rvaToPtr = [&](DWORD rva) -> const BYTE* {
					if (!sec) return NULL;
					for (UINT i = 0; i < nsec; i++) {
						if (rva >= sec[i].VirtualAddress
							&& rva < sec[i].VirtualAddress + sec[i].SizeOfRawData)
							return base + sec[i].PointerToRawData + (rva - sec[i].VirtualAddress);
					}
					return NULL;
				};
				if (expRva) {
					const IMAGE_EXPORT_DIRECTORY* exp =
						(const IMAGE_EXPORT_DIRECTORY*)rvaToPtr(expRva);
					if (exp && exp->NumberOfNames && exp->AddressOfNames) {
						const DWORD* names = (const DWORD*)rvaToPtr(exp->AddressOfNames);
						if (names) {
							for (DWORD i = 0; i < exp->NumberOfNames; i++) {
								const char* nm = (const char*)rvaToPtr(names[i]);
								if (nm && strcmp(nm, exportName) == 0) {
									found = TRUE;
									break;
								}
							}
						}
					}
				}
			}
		}
		UnmapViewOfFile(base);
	}
	CloseHandle(map);
	CloseHandle(h);
	return found;
}

static BOOL KpiInstallPmdWinNeedsReplace(LPCTSTR path, WORD want)
{
	if (KpiInstallDllNeedsArchFix(path, want))
		return TRUE;
	/* 公式 c60 はアーキ正しくても FMモニタ用 export が無い */
	return KpiInstallPeHasExportA(path, "pmdwin_fmmon_snapshot") ? FALSE : TRUE;
}

/* ZIP を temp に展開し、relSrc を destFile へ（上書き） */
static BOOL KpiInstallZipCopyOne(LPCTSTR zipUrl, LPCTSTR workDir, LPCTSTR relSrc, LPCTSTR destFile)
{
	if (!zipUrl || !workDir || !relSrc || !destFile) return FALSE;
	TCHAR zipPath[MAX_PATH * 2] = {};
	_sntprintf_s(zipPath, _TRUNCATE, L"%s\\_dl.zip", workDir);
	DeleteFile(zipPath);
	if (!KpiInstallHttpDownloadFile(zipUrl, zipPath))
		return FALSE;
	TCHAR extractDir[MAX_PATH * 2] = {};
	_sntprintf_s(extractDir, _TRUNCATE, L"%s\\_ex", workDir);
	/* 展開先を空に近い状態へ */
	{
		TCHAR pat[MAX_PATH * 2] = {};
		_sntprintf_s(pat, _TRUNCATE, L"%s\\*", extractDir);
		WIN32_FIND_DATA fd = {};
		HANDLE hf = FindFirstFile(pat, &fd);
		if (hf != INVALID_HANDLE_VALUE) {
			do {
				if (fd.cFileName[0] == L'.') continue;
				TCHAR full[MAX_PATH * 2] = {};
				_sntprintf_s(full, _TRUNCATE, L"%s\\%s", extractDir, fd.cFileName);
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					/* 浅い削除で足りない場合は Extract が上書きする */
				} else {
					DeleteFile(full);
				}
			} while (FindNextFile(hf, &fd));
			FindClose(hf);
		}
	}
	CreateDirectory(extractDir, NULL);
	CString err;
	const BOOL okEx = KpiInstallExtractZip(zipPath, extractDir, err, FALSE);
	DeleteFile(zipPath);
	if (!okEx) return FALSE;
	TCHAR src[MAX_PATH * 2] = {};
	_sntprintf_s(src, _TRUNCATE, L"%s\\%s", extractDir, relSrc);
	if (GetFileAttributes(src) == INVALID_FILE_ATTRIBUTES)
		return FALSE;
	{
		const int slash = (int)_tcslen(destFile);
		for (int i = slash - 1; i > 0; --i) {
			if (destFile[i] == L'\\' || destFile[i] == L'/') {
				TCHAR dir[MAX_PATH * 2] = {};
				_tcsncpy_s(dir, destFile, i);
				KpiInstallMkDirDeep(dir);
				break;
			}
		}
	}
	return CopyFile(src, destFile, FALSE) ? TRUE : FALSE;
}

static BOOL KpiInstallFetchOfficialFmpmdDlls(LPCTSTR fmpmdDir, WORD want)
{
	if (!fmpmdDir || !fmpmdDir[0] || want == 0) return FALSE;
	TCHAR tmp[MAX_PATH] = {};
	GetTempPath(MAX_PATH, tmp);
	TCHAR work[MAX_PATH * 2] = {};
	_sntprintf_s(work, _TRUNCATE, L"%sogg_fmpmd_dllfix", tmp);
	CreateDirectory(work, NULL);

	const BOOL want64 = (want == IMAGE_FILE_MACHINE_AMD64);
	TCHAR destPmd[MAX_PATH * 2] = {};
	TCHAR destWin[MAX_PATH * 2] = {};
	TCHAR destPdz[MAX_PATH * 2] = {};
	_sntprintf_s(destPmd, _TRUNCATE, L"%s\\PMDWin.dll", fmpmdDir);
	_sntprintf_s(destWin, _TRUNCATE, L"%s\\WinFMP.dll", fmpmdDir);
	_sntprintf_s(destPdz, _TRUNCATE, L"%s\\PDZFZ8XWin.dll", fmpmdDir);

	BOOL any = FALSE;
	/* PMDWin: arch 不一致、または fmmon export 無し（c60 公式など）→ Plugins.zip から */
	if (KpiInstallPmdWinNeedsReplace(destPmd, want)) {
		TCHAR zipPath[MAX_PATH * 2] = {};
		_sntprintf_s(zipPath, _TRUNCATE, L"%s\\_plugins.zip", work);
		if (KpiInstallHttpDownloadFile(PLUGINS_ZIP_URL, zipPath)) {
			TCHAR exDir[MAX_PATH * 2] = {};
			_sntprintf_s(exDir, _TRUNCATE, L"%s\\_plex", work);
			CreateDirectory(exDir, NULL);
			CString err;
			if (KpiInstallExtractZip(zipPath, exDir, err, FALSE)) {
				TCHAR src[MAX_PATH * 2] = {};
				if (want64)
					_sntprintf_s(src, _TRUNCATE, L"%s\\Plugins\\Kobarin\\fmpmd\\PMDWin.dll", exDir);
				else
					_sntprintf_s(src, _TRUNCATE, L"%s\\Plugins\\Kobarin\\fmpmd\\PMDWinx86\\PMDWin.dll", exDir);
				if (KpiInstallFileExists(src) && CopyFile(src, destPmd, FALSE))
					any = TRUE;
			}
			DeleteFile(zipPath);
		}
	}
	if (KpiInstallDllNeedsArchFix(destWin, want)) {
		const TCHAR* rel = want64 ? L"WinFMP052\\x64\\WinFMP.dll" : L"WinFMP052\\x86\\WinFMP.dll";
		if (KpiInstallZipCopyOne(C60_WINFMP_ZIP_URL, work, rel, destWin))
			any = TRUE;
	}
	if (KpiInstallDllNeedsArchFix(destPdz, want)) {
		const TCHAR* rel = want64 ? L"Release\\x64\\PDZFZ8XWin.dll" : L"Release\\x86\\PDZFZ8XWin.dll";
		if (KpiInstallZipCopyOne(AOSOFT_PDZF_ZIP_URL, work, rel, destPdz))
			any = TRUE;
	}
	return any;
}

static void KpiInstallDisableLegacyFmpmd(LPCTSTR fmpmdDir)
{
	if (!fmpmdDir || !fmpmdDir[0]) return;
	TCHAR src[MAX_PATH * 2] = {};
	TCHAR dst[MAX_PATH * 2] = {};
	_sntprintf_s(src, _TRUNCATE, L"%s\\fmpmd.kpi", fmpmdDir);
	if (GetFileAttributes(src) == INVALID_FILE_ATTRIBUTES)
		return;
	_sntprintf_s(dst, _TRUNCATE, L"%s\\fmpmd.kpi.ogg_unused", fmpmdDir);
	DeleteFile(dst);
	MoveFile(src, dst);
}

static int KpiInstallPathIsUnder(LPCTSTR path, LPCTSTR dir)
{
	if (!path || !dir || !path[0] || !dir[0]) return 0;
	const size_t n = _tcslen(dir);
	if (_tcsnicmp(path, dir, n) != 0) return 0;
	return (path[n] == 0 || path[n] == L'\\' || path[n] == L'/') ? 1 : 0;
}

static void KpiInstallPropagateOne(LPCTSTR srcFile, LPCTSTR dstFile)
{
	if (!srcFile || !dstFile || !srcFile[0] || !dstFile[0]) return;
	if (_tcsicmp(srcFile, dstFile) == 0) return;
	if (GetFileAttributes(srcFile) == INVALID_FILE_ATTRIBUTES) return;
	CopyFile(srcFile, dstFile, FALSE);
}

static void KpiInstallPropagateFmpmdRecurse(LPCTSTR dir, LPCTSTR srcDir,
	LPCTSTR srcFmp, LPCTSTR srcPmd, LPCTSTR srcDll, LPCTSTR srcWinFmp, LPCTSTR srcPdz)
{
	TCHAR pattern[MAX_PATH * 2] = {};
	_sntprintf_s(pattern, _TRUNCATE, L"%s\\*", dir);
	WIN32_FIND_DATA fd = {};
	HANDLE h = FindFirstFile(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0
			|| (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		TCHAR full[MAX_PATH * 2] = {};
		_sntprintf_s(full, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			KpiInstallPropagateFmpmdRecurse(full, srcDir, srcFmp, srcPmd, srcDll, srcWinFmp, srcPdz);
			continue;
		}
		if (_tcsicmp(fd.cFileName, L"fmpmd.kpi") == 0) {
			TCHAR unused[MAX_PATH * 2] = {};
			_sntprintf_s(unused, _TRUNCATE, L"%s.ogg_unused", full);
			DeleteFile(unused);
			MoveFile(full, unused);
			continue;
		}
		if (KpiInstallPathIsUnder(full, srcDir))
			continue;
		if (_tcsicmp(fd.cFileName, L"kbfmp.kpi") == 0)
			KpiInstallPropagateOne(srcFmp, full);
		else if (_tcsicmp(fd.cFileName, L"kbpmd.kpi") == 0)
			KpiInstallPropagateOne(srcPmd, full);
		else if (_tcsicmp(fd.cFileName, L"PMDWin.dll") == 0)
			KpiInstallPropagateOne(srcDll, full);
		else if (_tcsicmp(fd.cFileName, L"WinFMP.dll") == 0 && srcWinFmp && srcWinFmp[0])
			KpiInstallPropagateOne(srcWinFmp, full);
		else if (_tcsicmp(fd.cFileName, L"PDZFZ8XWin.dll") == 0 && srcPdz && srcPdz[0])
			KpiInstallPropagateOne(srcPdz, full);
	} while (FindNextFile(h, &fd));
	FindClose(h);
}

/* Plugins.zip 展開後: 必要 arch の DLL を正規位置へ寄せる */
static void KpiInstallNormalizeFmpmdDllLayout(LPCTSTR fmpmdDir, WORD want)
{
	if (!fmpmdDir || !fmpmdDir[0] || want == 0) return;
	TCHAR rootPmd[MAX_PATH * 2] = {}, rootWin[MAX_PATH * 2] = {};
	TCHAR x86Pmd[MAX_PATH * 2] = {}, x86Win[MAX_PATH * 2] = {};
	_sntprintf_s(rootPmd, _TRUNCATE, L"%s\\PMDWin.dll", fmpmdDir);
	_sntprintf_s(rootWin, _TRUNCATE, L"%s\\WinFMP.dll", fmpmdDir);
	_sntprintf_s(x86Pmd, _TRUNCATE, L"%s\\PMDWinx86\\PMDWin.dll", fmpmdDir);
	_sntprintf_s(x86Win, _TRUNCATE, L"%s\\WinFMPx86\\WinFMP.dll", fmpmdDir);
	if (want == IMAGE_FILE_MACHINE_I386) {
		if (GetFileAttributes(x86Pmd) != INVALID_FILE_ATTRIBUTES)
			CopyFile(x86Pmd, rootPmd, FALSE);
		if (GetFileAttributes(x86Win) != INVALID_FILE_ATTRIBUTES)
			CopyFile(x86Win, rootWin, FALSE);
	}
}

static void KpiInstallFinalizeFmpmdLayout(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0]) return;
	KpiInstallEnsureFmpmdDirs(exeDir);
	TCHAR fmpmdDir[MAX_PATH * 2] = {};
	TCHAR pluginsDir[MAX_PATH * 2] = {};
	TCHAR srcFmp[MAX_PATH * 2] = {};
	TCHAR srcPmd[MAX_PATH * 2] = {};
	TCHAR srcDll[MAX_PATH * 2] = {};
	TCHAR srcWin[MAX_PATH * 2] = {};
	TCHAR srcPdz[MAX_PATH * 2] = {};
	_sntprintf_s(fmpmdDir, _TRUNCATE, L"%sPlugins\\Kobarin\\fmpmd", exeDir);
	_sntprintf_s(pluginsDir, _TRUNCATE, L"%sPlugins", exeDir);
	_sntprintf_s(srcFmp, _TRUNCATE, L"%s\\kbfmp.kpi", fmpmdDir);
	_sntprintf_s(srcPmd, _TRUNCATE, L"%s\\kbpmd.kpi", fmpmdDir);
	_sntprintf_s(srcDll, _TRUNCATE, L"%s\\PMDWin.dll", fmpmdDir);
	_sntprintf_s(srcWin, _TRUNCATE, L"%s\\WinFMP.dll", fmpmdDir);
	_sntprintf_s(srcPdz, _TRUNCATE, L"%s\\PDZFZ8XWin.dll", fmpmdDir);
	KpiInstallDisableLegacyFmpmd(fmpmdDir);
	KpiInstallEnsureRhythmAssets(exeDir);
	if (GetFileAttributes(srcFmp) == INVALID_FILE_ATTRIBUTES
		&& GetFileAttributes(srcPmd) == INVALID_FILE_ATTRIBUTES)
		return;
	if (GetFileAttributes(pluginsDir) != INVALID_FILE_ATTRIBUTES) {
		const TCHAR* winSrc = (GetFileAttributes(srcWin) != INVALID_FILE_ATTRIBUTES) ? srcWin : NULL;
		const TCHAR* pdzSrc = (GetFileAttributes(srcPdz) != INVALID_FILE_ATTRIBUTES) ? srcPdz : NULL;
		KpiInstallPropagateFmpmdRecurse(pluginsDir, fmpmdDir, srcFmp, srcPmd, srcDll, winSrc, pdzSrc);
	}
}

BOOL KpiInstall_SilentUpdateFmpmd(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0])
		return FALSE;

	DWORD netFlags = 0;
	const BOOL online = InternetGetConnectedState(&netFlags, 0) ? TRUE : FALSE;

	/* Plugins.zip はフォルダ作成前に判定（空 Plugins を先に作らない） */
	BOOL did = FALSE;
	if (online)
		did = KpiInstallSilentMaybeFetchPluginsZip(exeDir);

	KpiInstallEnsureFmpmdDirs(exeDir);

	TCHAR fmpmdDir[MAX_PATH * 2] = {};
	_sntprintf_s(fmpmdDir, _TRUNCATE, L"%sPlugins\\Kobarin\\fmpmd", exeDir);
	TCHAR localPmdDll[MAX_PATH * 2] = {};
	TCHAR localWinDll[MAX_PATH * 2] = {};
	TCHAR localPdzDll[MAX_PATH * 2] = {};
	_sntprintf_s(localPmdDll, _TRUNCATE, L"%s\\PMDWin.dll", fmpmdDir);
	_sntprintf_s(localWinDll, _TRUNCATE, L"%s\\WinFMP.dll", fmpmdDir);
	_sntprintf_s(localPdzDll, _TRUNCATE, L"%s\\PDZFZ8XWin.dll", fmpmdDir);

	const WORD want = KpiInstallWantedFmpmdMachine(fmpmdDir);
	if (online) {
		const BOOL dllBad =
			KpiInstallPmdWinNeedsReplace(localPmdDll, want)
			|| KpiInstallDllNeedsArchFix(localWinDll, want)
			|| KpiInstallDllNeedsArchFix(localPdzDll, want);
		/* arch 不一致は公式 ZIP（Plugins.zip 外）から補正 */
		if (dllBad) {
			if (KpiInstallFetchOfficialFmpmdDlls(fmpmdDir, want))
				did = TRUE;
		}
		if (did) {
			KpiInstallNormalizeFmpmdDllLayout(fmpmdDir, want);
			if (KpiInstallPmdWinNeedsReplace(localPmdDll, want)
				|| KpiInstallDllNeedsArchFix(localWinDll, want)
				|| KpiInstallDllNeedsArchFix(localPdzDll, want)) {
				KpiInstallFetchOfficialFmpmdDlls(fmpmdDir, want);
			}
		}
	}

	KpiInstallFinalizeFmpmdLayout(exeDir);
	return did;
}

/* バンドル → 既存 Plugins / x64\Plugins のみ更新（無いものは触らない）。供給は x64 のみ。 */
BOOL KpiInstall_SilentUpdateFmMonKpis(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0])
		return FALSE;

	/* gateRel: 非 NULL なら「同ツリー上のゲートファイルがあるときだけ」更新
	   （付属 DLL/BIN 用。ゲート自体が無くても dst があれば従来どおり更新） */
	struct Entry {
		const TCHAR* relDst;     /* under <root>\ */
		const TCHAR* bundleName; /* under Plugins\.ogg_kpi_fmmon\ */
		const TCHAR* gateRel;    /* under <root>\ ; optional */
	};
	static const Entry kTab[] = {
		/* --- FM/MIDI モニタ既存 --- */
		{ L"Kobarin\\kbfmmidi\\kbfmmidi.kpi", L"kbfmmidi.kpi", NULL },
		{ L"Kobarin\\fmpmd\\kbfmp.kpi", L"kbfmp.kpi", NULL },
		{ L"Kobarin\\fmpmd\\kbpmd.kpi", L"kbpmd.kpi", NULL },
		{ L"Kobarin\\fmpmd\\PMDWin.dll", L"PMDWin.dll", L"Kobarin\\fmpmd\\kbpmd.kpi" },
		{ L"Kobarin\\kbvgm\\kbvgm.kpi", L"kbvgm.kpi", NULL },
		{ L"kbvgm.kpi", L"kbvgm.kpi", NULL },
		{ L"Mamiya\\kbs98\\kbs98.kpi", L"kbs98.kpi", NULL },
		{ L"OK\\kbmsxplug\\kbmsxplug.kpi", L"kbmsxplug.kpi", NULL },
		/* MSXplug 付属ドライバ（DLLではないが FMPAC/OPX/MBM 再生に必要） */
		{ L"OK\\kbmsxplug\\FMPAC.ROM", L"FMPAC.ROM", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbmsxplug\\MPK.BIN", L"MPK.BIN", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbmsxplug\\MPK103.BIN", L"MPK103.BIN", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbmsxplug\\OPX4KSS.BIN", L"OPX4KSS.BIN", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbmsxplug\\MBR143.001", L"MBR143.001", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbmsxplug\\MBR143.000", L"MBR143.000", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbmsxplug\\MBR143.BAS", L"MBR143.BAS", L"OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"z_5_Plugins\\OK\\kbmsxplug\\FMPAC.ROM", L"FMPAC.ROM", L"z_5_Plugins\\OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"z_5_Plugins\\OK\\kbmsxplug\\MPK.BIN", L"MPK.BIN", L"z_5_Plugins\\OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"z_5_Plugins\\OK\\kbmsxplug\\MPK103.BIN", L"MPK103.BIN", L"z_5_Plugins\\OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"z_5_Plugins\\OK\\kbmsxplug\\OPX4KSS.BIN", L"OPX4KSS.BIN", L"z_5_Plugins\\OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"z_5_Plugins\\OK\\kbmsxplug\\MBR143.001", L"MBR143.001", L"z_5_Plugins\\OK\\kbmsxplug\\kbmsxplug.kpi" },
		{ L"OK\\kbemidi\\kbemidi.kpi", L"kbemidi.kpi", NULL },
		{ L"Kobarin\\kbemidi\\kbemidi.kpi", L"kbemidi.kpi", NULL },
		{ L"Kobarin\\kbgme\\kbgme.kpi", L"kbgme.kpi", NULL },
		{ L"OK\\kbgme\\kbgme.kpi", L"kbgme.kpi", NULL },
		/* --- keys-only / チップ拡張（引用リスト） --- */
		{ L"Mamiya\\kbmdx\\kbmdx.kpi", L"kbmdx.kpi", NULL },
		{ L"Kobarin\\kbfmoplmidi\\kbfmoplmidi.kpi", L"kbfmoplmidi.kpi", NULL },
		{ L"Kobarin\\kbsc68\\kbsc68.kpi", L"kbsc68.kpi", NULL },
		/* --- dump 系: NSF → SPC → PSF / PSF2 --- */
		{ L"Kobarin\\kbnsfplug\\kbnsfplug.kpi", L"kbnsfplug.kpi", NULL },
		{ L"Kobarin\\kbsnesapu\\kbsnesapu.kpi", L"kbsnesapu.kpi", NULL },
		{ L"Kobarin\\kbsnesapu\\snesapu.dll", L"snesapu.dll", L"Kobarin\\kbsnesapu\\kbsnesapu.kpi" },
		{ L"Audio\\kbsnesapu.kpi", L"kbsnesapu.kpi", NULL },
		{ L"S_Kino\\kbspc\\kbspc.kpi", L"kbsnesapu.kpi", NULL }, /* 旧名配置でも更新 */
		/* PSF 系は Kobarin 内のみ更新（Plugins\kbpsf / kbpsf2 は触らない） */
		{ L"Kobarin\\kbpsf\\kbpsf.kpi", L"kbpsf.kpi", NULL },
		{ L"Kobarin\\kbpsf\\viopsf.bin", L"viopsf.bin", L"Kobarin\\kbpsf\\kbpsf.kpi" },
		{ L"Kobarin\\kbpsf\\kbzlib.dll", L"kbzlib.dll", L"Kobarin\\kbpsf\\kbpsf.kpi" },
		{ L"Kobarin\\kbpsf\\spuPeopsSound.dll", L"spuPeopsSound.dll", L"Kobarin\\kbpsf\\kbpsf.kpi" },
		{ L"Kobarin\\kbpsf\\spumednafen.dll", L"spumednafen.dll", L"Kobarin\\kbpsf\\kbpsf.kpi" },
		{ L"Kobarin\\kbpsf\\spumame.dll", L"spumame.dll", L"Kobarin\\kbpsf\\kbpsf.kpi" },
		{ L"Kobarin\\kbpsf2\\kbpsf2.kpi", L"kbpsf2.kpi", NULL },
		{ L"Kobarin\\kbpsf2\\viopsf2.bin", L"viopsf2.bin", L"Kobarin\\kbpsf2\\kbpsf2.kpi" },
		{ L"Kobarin\\kbpsf2\\kbzlib.dll", L"kbzlib.dll", L"Kobarin\\kbpsf2\\kbpsf2.kpi" },
		/* --- MIDI っぽく出せる / pitch+KON（対応中の枠） --- */
		{ L"Kobarin\\kbncsf\\kbncsf.kpi", L"kbncsf.kpi", NULL },
		{ L"Kobarin\\kbxsf\\kbncsf\\kbncsf.kpi", L"kbncsf.kpi", NULL },
		{ L"Kobarin\\kbum\\kbum.kpi", L"kbum.kpi", NULL },
		{ L"Kobarin\\kbmod\\kbmod.kpi", L"kbmod.kpi", NULL },
		{ L"Kobarin\\kbpxtone\\kbpxtone.kpi", L"kbpxtone.kpi", NULL },
		{ L"Kobarin\\kb2sf\\kb2sf.kpi", L"kb2sf.kpi", NULL },
		{ L"kbvio2sf.kpi", L"kbvio2sf.kpi", NULL },
		{ L"Kobarin\\kbvio2sf\\kbvio2sf.kpi", L"kbvio2sf.kpi", NULL },
		{ L"Kobarin\\kbqsf\\kbqsf.kpi", L"kbqsf.kpi", NULL },
		{ L"Kobarin\\kbusf\\kbusf.kpi", L"kbusf.kpi", NULL },
		{ L"Kobarin\\kbssf\\kbssf.kpi", L"kbssf.kpi", NULL },
		{ L"Kobarin\\kbdsf\\kbdsf.kpi", L"kbdsf.kpi", NULL },
		{ L"Kobarin\\kbsid\\kbsid.kpi", L"kbsid.kpi", NULL },
		{ L"Mamiya\\kbsid\\kbsid.kpi", L"kbsid.kpi", NULL },
		{ L"Mamiya\\kpisidplay\\sidplay.kpi", L"kbsid.kpi", NULL }, /* 旧配置の別称 */
		{ L"Kobarin\\kbgsf\\kbgsf.kpi", L"kbgsf.kpi", NULL },
		{ L"Kobarin\\kbgsf\\viogsf.bin", L"viogsf.bin", L"Kobarin\\kbgsf\\kbgsf.kpi" },
		{ L"Kobarin\\kbxsf\\kbgsf\\kbgsf.kpi", L"kbgsf.kpi", NULL },
		{ L"Kobarin\\kbxsf\\kbgsf\\viogsf.bin", L"viogsf.bin", L"Kobarin\\kbxsf\\kbgsf\\kbgsf.kpi" },
		{ L"Kobarin\\kbsap\\kbsap.kpi", L"kbsap.kpi", NULL },
		{ L"Kobarin\\kbwsr\\kbwsr.kpi", L"kbwsr.kpi", NULL },
		{ L"Kobarin\\kbnezplug\\kbnezplug.kpi", L"kbnezplug.kpi", NULL },
		{ L"Audio\\nezplug\\nezplug.kpi", L"kbnezplug.kpi", NULL },
		{ L"Mamiya\\nezplug\\nezplug.kpi", L"kbnezplug.kpi", NULL },
		{ L"Mamiya\\kbgym\\kbgym.kpi", L"kbgym.kpi", NULL },
		{ L"OK\\kbgym\\kbgym.kpi", L"kbgym.kpi", NULL },
		/* z_5 退避先 */
		{ L"z_5_Plugins\\Kobarin\\kbfmmidi\\kbfmmidi.kpi", L"kbfmmidi.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\fmpmd\\kbfmp.kpi", L"kbfmp.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\fmpmd\\kbpmd.kpi", L"kbpmd.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbvgm\\kbvgm.kpi", L"kbvgm.kpi", NULL },
		{ L"z_5_Plugins\\OK\\kbmsxplug\\kbmsxplug.kpi", L"kbmsxplug.kpi", NULL },
		{ L"z_5_Plugins\\OK\\kbemidi\\kbemidi.kpi", L"kbemidi.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbgme\\kbgme.kpi", L"kbgme.kpi", NULL },
		{ L"z_5_Plugins\\Mamiya\\kbs98\\kbs98.kpi", L"kbs98.kpi", NULL },
		{ L"z_5_Plugins\\Mamiya\\kbmdx\\kbmdx.kpi", L"kbmdx.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbfmoplmidi\\kbfmoplmidi.kpi", L"kbfmoplmidi.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbsc68\\kbsc68.kpi", L"kbsc68.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbnsfplug\\kbnsfplug.kpi", L"kbnsfplug.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbsnesapu\\kbsnesapu.kpi", L"kbsnesapu.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbpsf\\kbpsf.kpi", L"kbpsf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbpsf2\\kbpsf2.kpi", L"kbpsf2.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbpsf2\\viopsf2.bin", L"viopsf2.bin", L"z_5_Plugins\\Kobarin\\kbpsf2\\kbpsf2.kpi" },
		{ L"z_5_Plugins\\Kobarin\\kbpsf2\\kbzlib.dll", L"kbzlib.dll", L"z_5_Plugins\\Kobarin\\kbpsf2\\kbpsf2.kpi" },
		{ L"z_5_Plugins\\Kobarin\\kbncsf\\kbncsf.kpi", L"kbncsf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbum\\kbum.kpi", L"kbum.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbmod\\kbmod.kpi", L"kbmod.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbpxtone\\kbpxtone.kpi", L"kbpxtone.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kb2sf\\kb2sf.kpi", L"kb2sf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbqsf\\kbqsf.kpi", L"kbqsf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbusf\\kbusf.kpi", L"kbusf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbssf\\kbssf.kpi", L"kbssf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbdsf\\kbdsf.kpi", L"kbdsf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbsid\\kbsid.kpi", L"kbsid.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbgsf\\kbgsf.kpi", L"kbgsf.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbsap\\kbsap.kpi", L"kbsap.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbwsr\\kbwsr.kpi", L"kbwsr.kpi", NULL },
		{ L"z_5_Plugins\\Kobarin\\kbnezplug\\kbnezplug.kpi", L"kbnezplug.kpi", NULL },
		{ L"z_5_Plugins\\Mamiya\\kbgym\\kbgym.kpi", L"kbgym.kpi", NULL },
		{ L"z_5_Plugins\\OK\\kbgym\\kbgym.kpi", L"kbgym.kpi", NULL },
	};

	/* Plugins と x64\Plugins。供給は x64 バンドルのみ（x86 は plugins.zip 側で廃止予定） */
	static const TCHAR* kRoots[] = {
		L"Plugins",
		L"x64\\Plugins",
	};

	TCHAR bundleDir[MAX_PATH * 2] = {};
	_sntprintf_s(bundleDir, _TRUNCATE, L"%sPlugins\\.ogg_kpi_fmmon", exeDir);
	if (GetFileAttributes(bundleDir) == INVALID_FILE_ATTRIBUTES)
		return FALSE;

	BOOL did = FALSE;
	for (int r = 0; r < (int)(sizeof(kRoots) / sizeof(kRoots[0])); r++) {
		for (int i = 0; i < (int)(sizeof(kTab) / sizeof(kTab[0])); i++) {
			TCHAR dst[MAX_PATH * 2] = {};
			TCHAR src[MAX_PATH * 2] = {};
			_sntprintf_s(dst, _TRUNCATE, L"%s%s\\%s", exeDir, kRoots[r], kTab[i].relDst);

			/* 常に x64: .ogg_kpi_fmmon\x64\ → 無ければ直下 */
			{
				TCHAR prefer[MAX_PATH * 2] = {};
				_sntprintf_s(prefer, _TRUNCATE, L"%s\\x64\\%s", bundleDir, kTab[i].bundleName);
				if (KpiInstallFileExists(prefer))
					_tcscpy_s(src, prefer);
				else
					_sntprintf_s(src, _TRUNCATE, L"%s\\%s", bundleDir, kTab[i].bundleName);
			}

			if (kTab[i].gateRel) {
				TCHAR gate[MAX_PATH * 2] = {};
				_sntprintf_s(gate, _TRUNCATE, L"%s%s\\%s", exeDir, kRoots[r], kTab[i].gateRel);
				if (!KpiInstallFileExists(gate))
					continue; /* 親 KPI が無い付属は触らない */
			} else if (!KpiInstallFileExists(dst)) {
				continue; /* KPI 本体は所持分のみ更新 */
			}

			if (!KpiInstallFileExists(src))
				continue;
			const time_t tDst = KpiInstallFileExists(dst) ? KpiInstallFileMtimeUtc(dst) : 0;
			const time_t tSrc = KpiInstallFileMtimeUtc(src);
			if (tSrc != 0 && tDst != 0 && tSrc <= tDst + 2)
				continue; /* 既に同等以上 */

			{
				TCHAR dir[MAX_PATH * 2] = {};
				_tcsncpy_s(dir, dst, _TRUNCATE);
				TCHAR* slash = _tcsrchr(dir, L'\\');
				if (slash) {
					*slash = 0;
					KpiInstallMkDirDeep(dir);
				}
			}
			if (CopyFile(src, dst, FALSE))
				did = TRUE;
		}
	}
	return did;
}

/* ---- KPI 一覧用: 改造 dump の有無（同名ストックと区別） ---- */

static BOOL KpiProbeMemHas(const BYTE* base, size_t n, const void* needle, size_t needleLen)
{
	if (!base || !needle || needleLen == 0 || n < needleLen) return FALSE;
	const BYTE* nd = (const BYTE*)needle;
	const BYTE* p = base;
	const BYTE* end = base + (n - needleLen + 1);
	while (p < end) {
		p = (const BYTE*)memchr(p, nd[0], (size_t)(end - p));
		if (!p) return FALSE;
		if (memcmp(p, nd, needleLen) == 0) return TRUE;
		++p;
	}
	return FALSE;
}

static BOOL KpiProbeStemIsKeysOnlyMid(LPCTSTR path)
{
	if (!path || !path[0]) return FALSE;
	const TCHAR* base = path;
	for (const TCHAR* p = path; *p; ++p) {
		if (*p == L'\\' || *p == L'/')
			base = p + 1;
	}
	TCHAR stem[128] = {};
	_tcsncpy_s(stem, base, _TRUNCATE);
	TCHAR* dot = _tcsrchr(stem, L'.');
	if (dot) *dot = 0;
	static const TCHAR* kKeys[] = {
		L"kbfmmidi", L"kbfmoplmidi", L"kbemidi", L"kbmdx", L"kbncsf", L"kbnsfplug",
		L"kbsnesapu", L"kbspc", L"kbsid", L"sidplay", L"kbpsf", L"kbpsf2",
		L"kbgsf", L"kbum", L"kbmod", L"kbpxtone", L"kb2sf", L"kbvio2sf",
		L"kbqsf", L"kbusf", L"kbssf", L"kbdsf", L"kbsap", L"kbwsr", L"kbnezplug",
		L"nezplug",
	};
	for (int i = 0; i < (int)_countof(kKeys); ++i) {
		if (_tcsicmp(stem, kKeys[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

static void KpiProbeOneFile(LPCTSTR path, BOOL* hasDump, BOOL* tagFm, BOOL* tagMid)
{
	if (!path || !path[0]) return;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	LARGE_INTEGER sz = {};
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (LONGLONG)(64 * 1024 * 1024)) {
		CloseHandle(h);
		return;
	}
	HANDLE map = CreateFileMapping(h, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!map) { CloseHandle(h); return; }
	const BYTE* base = (const BYTE*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
	if (base) {
		const size_t n = (size_t)sz.QuadPart;
		/* UTF-16LE "fmmon_live" — dump 改造の共通しるし */
		static const BYTE kFmLiveUtf16[] = {
			'f',0,'m',0,'m',0,'o',0,'n',0,'_',0,'l',0,'i',0,'v',0,'e',0
		};
		static const char kTagFm[] = "ogg.FMmon";
		static const char kTagMid[] = "ogg.MIDmon";
		static const char kPmdSnap[] = "pmdwin_fmmon_snapshot";
		if (KpiProbeMemHas(base, n, kFmLiveUtf16, sizeof(kFmLiveUtf16)))
			*hasDump = TRUE;
		if (KpiProbeMemHas(base, n, kTagFm, sizeof(kTagFm) - 1))
			*tagFm = TRUE;
		if (KpiProbeMemHas(base, n, kTagMid, sizeof(kTagMid) - 1))
			*tagMid = TRUE;
		if (KpiProbeMemHas(base, n, kPmdSnap, sizeof(kPmdSnap) - 1)) {
			*hasDump = TRUE;
			*tagFm = TRUE;
		}
		UnmapViewOfFile(base);
	}
	CloseHandle(map);
	CloseHandle(h);
}

void KpiPlugin_ProbeMonitorCaps(LPCTSTR path, BOOL* outFmMon, BOOL* outMidMon)
{
	if (outFmMon) *outFmMon = FALSE;
	if (outMidMon) *outMidMon = FALSE;
	if (!path || !path[0]) return;

	/* path + mtime キャッシュ（フィルタ再描画で毎回全 PE を踏まない） */
	struct CapEnt {
		TCHAR path[MAX_PATH];
		FILETIME ft;
		BOOL fm, mid;
		BOOL used;
	};
	static CapEnt s_cache[96];
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	const BOOL haveFt = GetFileAttributesEx(path, GetFileExInfoStandard, &fad);
	if (haveFt) {
		for (int i = 0; i < (int)_countof(s_cache); ++i) {
			if (!s_cache[i].used) continue;
			if (_tcsicmp(s_cache[i].path, path) != 0) continue;
			if (CompareFileTime(&s_cache[i].ft, &fad.ftLastWriteTime) != 0) break;
			if (outFmMon) *outFmMon = s_cache[i].fm;
			if (outMidMon) *outMidMon = s_cache[i].mid;
			return;
		}
	}

	BOOL hasDump = FALSE, tagFm = FALSE, tagMid = FALSE;
	KpiProbeOneFile(path, &hasDump, &tagFm, &tagMid);

	/* fmpmd.kpi 本体に dump が無くても同フォルダ PMDWin.dll を見る */
	if (!hasDump) {
		TCHAR dir[MAX_PATH] = {};
		_tcsncpy_s(dir, path, _TRUNCATE);
		TCHAR* slash = _tcsrchr(dir, L'\\');
		if (slash) {
			*slash = 0;
			TCHAR sib[MAX_PATH];
			_sntprintf_s(sib, _TRUNCATE, L"%s\\PMDWin.dll", dir);
			KpiProbeOneFile(sib, &hasDump, &tagFm, &tagMid);
			if (!hasDump) {
				_sntprintf_s(sib, _TRUNCATE, L"%s\\PMDWinx86\\PMDWin.dll", dir);
				KpiProbeOneFile(sib, &hasDump, &tagFm, &tagMid);
			}
		}
	}

	BOOL fm = FALSE, mid = FALSE;
	if (tagFm || tagMid) {
		fm = tagFm;
		mid = tagMid;
		/* タグ無しの旧改造バイナリ向け: dump ありなら少なくとも片方は立てる */
		if (hasDump && !fm && !mid)
			fm = TRUE;
	} else if (hasDump) {
		/* 旧: fmmon_live のみ。keys-only 系は [MIDmon]、レジスタ系は [FMmon] */
		if (KpiProbeStemIsKeysOnlyMid(path))
			mid = TRUE;
		else
			fm = TRUE;
	}

	if (outFmMon) *outFmMon = fm;
	if (outMidMon) *outMidMon = mid;

	if (haveFt) {
		static int s_next = 0;
		CapEnt& e = s_cache[s_next++ % (int)_countof(s_cache)];
		_tcsncpy_s(e.path, path, _TRUNCATE);
		e.ft = fad.ftLastWriteTime;
		e.fm = fm;
		e.mid = mid;
		e.used = TRUE;
	}
}
