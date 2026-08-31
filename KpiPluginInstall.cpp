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

static BOOL KpiInstallExtractZip(const TCHAR* zipPath, const TCHAR* destDir, CString& errOut)
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
		if (!KpiInstallSafeRelPath(filename_inzip, rel)) {
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
		}
		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	if (extracted <= 0) {
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
	ok = KpiInstallExtractZip(zipPath, exeDir, errOut);
	DeleteFile(zipPath);
	if (progress) {
		progress(ok ? 100 : 92, ctx);
		KpiInstallPump();
	}
	return ok;
}

// ---------------------------------------------------------------------------
// kbsasami サイレント更新
// ZIP: https://ppp.oohara.jp/download/kbsasami.zip
// 中身: kbsasami\kbsasami.kpi (+txt) / kbsasami\x64\kbsasami.kpi
// 配置: exeDir\Plugins\kbsasami\ …
// ---------------------------------------------------------------------------

static const TCHAR* KBSASAMI_ZIP_URL = L"https://ppp.oohara.jp/download/kbsasami.zip";

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

BOOL KpiInstall_SilentUpdateKbsasami(LPCTSTR exeDir)
{
	if (!exeDir || !exeDir[0])
		return FALSE;

	DWORD netFlags = 0;
	if (!InternetGetConnectedState(&netFlags, 0))
		return FALSE;

	TCHAR pluginsDir[MAX_PATH * 2] = {};
	_sntprintf_s(pluginsDir, _TRUNCATE, L"%sPlugins", exeDir);
	TCHAR localX86[MAX_PATH * 2] = {};
	TCHAR localX64[MAX_PATH * 2] = {};
	_sntprintf_s(localX86, _TRUNCATE, L"%s\\kbsasami\\kbsasami.kpi", pluginsDir);
	_sntprintf_s(localX64, _TRUNCATE, L"%s\\kbsasami\\x64\\kbsasami.kpi", pluginsDir);

	const time_t t86 = KpiInstallFileMtimeUtc(localX86);
	const time_t t64 = KpiInstallFileMtimeUtc(localX64);
	const BOOL missing = (t86 == 0 || t64 == 0);
	const time_t localNewest = (t86 > t64) ? t86 : t64;

	const time_t serverMod = KpiInstallHttpLastModified(KBSASAMI_ZIP_URL);
	/* Have both and already at/newer than server → skip. */
	if (!missing && serverMod != 0 && serverMod <= localNewest + 120)
		return FALSE;
	/* Have local but Last-Modified unknown → don't re-fetch every launch. */
	if (!missing && serverMod == 0)
		return FALSE;

	CreateDirectory(pluginsDir, NULL);

	TCHAR tmp[MAX_PATH] = {};
	GetTempPath(MAX_PATH, tmp);
	TCHAR zipPath[MAX_PATH] = {};
	_sntprintf_s(zipPath, _TRUNCATE, L"%sogg_kpi_kbsasami.zip", tmp);
	if (!KpiInstallHttpDownloadFile(KBSASAMI_ZIP_URL, zipPath))
		return FALSE;

	CString err;
	// ZIP ルートが kbsasami\… なので Plugins\ 直下へ展開する
	const BOOL ok = KpiInstallExtractZip(zipPath, pluginsDir, err);
	DeleteFile(zipPath);
	return ok;
}
