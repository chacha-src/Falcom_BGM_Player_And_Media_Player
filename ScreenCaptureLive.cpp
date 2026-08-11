// ScreenCaptureLive.cpp
// YouTube / Nico / Custom RTMP live UI + OAuth + Live Streaming API

#include "stdafx.h"
#include "ogg.h"
#include "ScreenCaptureDlg.h"
#include "ScLiveSettingsDlg.h"

#include <wininet.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include <shellapi.h>

#include "minizip/unzip.h"
#include "minizip/iowin32.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

#include "YtOAuthDefaults.inc"
#if __has_include("YtOAuthDefaults.local.inc")
#include "YtOAuthDefaults.local.inc"
#endif

extern void MpPersistSavedataQuick();

namespace {

static CString ScLiveResolveClientId()
{
	// 製品埋め込みがあるときはそれを優先（古い savedata 上書きで 401 になるのを防ぐ）
	const CString def = CString(YT_OAUTH_DEFAULT_CLIENT_ID);
	if (!def.IsEmpty())
		return def;
	if (savedata.yt_client_id[0])
		return CString(savedata.yt_client_id);
	return CString();
}

static CString ScLiveResolveClientSecret()
{
	const CString def = CString(YT_OAUTH_DEFAULT_CLIENT_SECRET);
	if (!def.IsEmpty())
		return def;
	if (savedata.yt_client_secret[0])
		return CString(savedata.yt_client_secret);
	return CString();
}

static BOOL ScLiveHaveClientCreds()
{
	return !ScLiveResolveClientId().IsEmpty() && !ScLiveResolveClientSecret().IsEmpty();
}

static CString ScLiveMissingClientCredsMsg()
{
	return LL14(
		L"Client ID / Secret が未設定です。「詳細設定」に Google Cloud の OAuth クライアントを入力してから、もう一度「Googleでログイン」を押してください。",
		L"Client ID / Secret is not set. Enter your Google Cloud OAuth client under Advanced, then click Sign in with Google again.",
		L"Client ID / Secret non definis. Saisissez le client OAuth Google Cloud dans Avance, puis reconnectez-vous avec Google.",
		L"Client ID / Secret non impostati. Inserisci il client OAuth Google Cloud in Avanzate, poi Accedi con Google di nuovo.",
		L"Client ID / Secret no estan definidos. Introduzca el cliente OAuth de Google Cloud en Avanzado y vuelva a iniciar sesion con Google.",
		L"Client ID / Secret이 없습니다. 고급에 Google Cloud OAuth 클라이언트를 입력한 뒤 Google로 로그인을 다시 누르세요.",
		L"未设置 Client ID / Secret。请在「高级」中填写 Google Cloud OAuth 客户端，然后再次点击「使用 Google 登录」。",
		L"لم يُعيَّن Client ID / Secret. أدخل عميل OAuth في Google Cloud ضمن متقدم ثم سجّل الدخول عبر Google مجددًا.",
		L"Client ID / Secret не заданы. Введите OAuth-клиент Google Cloud в «Дополнительно» и снова нажмите «Войти через Google».",
		L"Client-ID / Secret fehlen. Geben Sie den Google-Cloud-OAuth-Client unter Erweitert ein und melden Sie sich erneut mit Google an.",
		L"Client ID / Secret nao definidos. Introduza o cliente OAuth do Google Cloud em Avancado e entre com o Google novamente.",
		L"Client ID / Secret ontbreken. Vul de Google Cloud OAuth-client in onder Geavanceerd en log opnieuw in met Google.",
		L"Brak Client ID / Secret. Wprowadz klienta OAuth Google Cloud w Zaawansowane i ponownie kliknij Zaloguj przez Google.",
		L"Client ID / Secret ayarlanmadi. Gelismis altina Google Cloud OAuth istemcisini girip Google ile oturum ac'a tekrar basin.");
}

static CString ScLiveNeedLoginMsg()
{
	return LL14(
		L"YouTube ログインが必要です。「Googleでログイン」を実行してください。",
		L"YouTube login required. Click Sign in with Google.",
		L"Connexion YouTube requise. Cliquez Se connecter avec Google.",
		L"Accesso YouTube richiesto. Clicca Accedi con Google.",
		L"Se requiere iniciar sesion en YouTube. Pulse Iniciar sesion con Google.",
		L"YouTube 로그인이 필요합니다. Google로 로그인을 실행하세요.",
		L"需要登录 YouTube。请点击「使用 Google 登录」。",
		L"مطلوب تسجيل الدخول إلى YouTube. انقر تسجيل الدخول عبر Google.",
		L"Нужен вход в YouTube. Нажмите «Войти через Google».",
		L"YouTube-Anmeldung noetig. Auf Mit Google anmelden klicken.",
		L"Login no YouTube necessario. Clique Entrar com o Google.",
		L"YouTube-login vereist. Klik Inloggen met Google.",
		L"Wymagane logowanie YouTube. Kliknij Zaloguj przez Google.",
		L"YouTube girisi gerekli. Google ile oturum ac'a tiklayin.");
}

static void ScLiveCopyField(TCHAR* dst, int dstCch, const CString& src)
{
	if (!dst || dstCch <= 0) return;
	_tcsncpy(dst, src, dstCch - 1);
	dst[dstCch - 1] = 0;
}

static CStringA ScLiveWideToUtf8(const CString& w)
{
	if (w.IsEmpty()) return CStringA();
	const int n = WideCharToMultiByte(CP_UTF8, 0, w, w.GetLength(), NULL, 0, NULL, NULL);
	if (n <= 0) return CStringA();
	CStringA out;
	LPSTR buf = out.GetBuffer(n);
	WideCharToMultiByte(CP_UTF8, 0, w, w.GetLength(), buf, n, NULL, NULL);
	out.ReleaseBuffer(n);
	return out;
}

static CString ScLiveUtf8ToWide(const CStringA& u)
{
	if (u.IsEmpty()) return CString();
	const int n = MultiByteToWideChar(CP_UTF8, 0, u, u.GetLength(), NULL, 0);
	if (n <= 0) return CString();
	CString out;
	LPWSTR buf = out.GetBuffer(n);
	MultiByteToWideChar(CP_UTF8, 0, u, u.GetLength(), buf, n);
	out.ReleaseBuffer(n);
	return out;
}

static CStringA ScLiveJsonEscapeUtf8(const CStringA& in)
{
	CStringA out;
	out.Preallocate(in.GetLength() + 16);
	for (int i = 0; i < in.GetLength(); ++i) {
		const unsigned char c = (unsigned char)in[i];
		switch (c) {
		case '\"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char tmp[8];
				sprintf_s(tmp, "\\u%04x", c);
				out += tmp;
			} else {
				out += (char)c;
			}
			break;
		}
	}
	return out;
}

static CStringA ScLiveUrlEncodeUtf8(const CStringA& in)
{
	CStringA out;
	out.Preallocate(in.GetLength() * 3 + 4);
	for (int i = 0; i < in.GetLength(); ++i) {
		const unsigned char c = (unsigned char)in[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
			|| c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			char tmp[8];
			sprintf_s(tmp, "%%%02X", c);
			out += tmp;
		}
	}
	return out;
}

static BOOL ScLiveJsonGetString(const char* json, const char* key, char* out, int outCch)
{
	if (!json || !key || !out || outCch <= 0) return FALSE;
	out[0] = 0;
	char pat[96];
	sprintf_s(pat, "\"%s\"", key);
	const char* p = strstr(json, pat);
	if (!p) return FALSE;
	p = strchr(p + (int)strlen(pat), ':');
	if (!p) return FALSE;
	++p;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
	if (*p != '\"') return FALSE;
	++p;
	int o = 0;
	while (*p && *p != '\"' && o + 1 < outCch) {
		if (*p == '\\' && p[1]) {
			++p;
			switch (*p) {
			case 'n': out[o++] = '\n'; break;
			case 'r': out[o++] = '\r'; break;
			case 't': out[o++] = '\t'; break;
			case '\"': out[o++] = '\"'; break;
			case '\\': out[o++] = '\\'; break;
			case '/': out[o++] = '/'; break;
			default: out[o++] = *p; break;
			}
			++p;
			continue;
		}
		out[o++] = *p++;
	}
	out[o] = 0;
	return o > 0;
}

static BOOL ScLiveJsonGetInt(const char* json, const char* key, int* outVal)
{
	if (!json || !key || !outVal) return FALSE;
	char pat[96];
	sprintf_s(pat, "\"%s\"", key);
	const char* p = strstr(json, pat);
	if (!p) return FALSE;
	p = strchr(p + (int)strlen(pat), ':');
	if (!p) return FALSE;
	++p;
	while (*p == ' ' || *p == '\t') ++p;
	*outVal = atoi(p);
	return TRUE;
}

static void ScLiveMsgError(CWnd* owner, const CString& detail)
{
	CString msg = LL14(
		L"ライブ配信エラー", L"Live streaming error", L"Erreur de diffusion", L"Errore live",
		L"Error de transmisión", L"라이브 오류", L"直播错误", L"خطأ البث",
		L"Ошибка трансляции", L"Live-Fehler", L"Erro de transmissão", L"Livefout",
		L"Błąd transmisji", L"Canlı yayın hatası");
	if (!detail.IsEmpty()) {
		msg += L"\n\n";
		msg += detail;
	}
	::MessageBox(owner ? owner->GetSafeHwnd() : NULL, msg, LL14(
		L"画面キャプチャ", L"Screen capture", L"Capture d'écran", L"Cattura schermo",
		L"Captura de pantalla", L"화면 캡처", L"屏幕捕获", L"التقاط الشاشة",
		L"Захват экрана", L"Bildschirmaufnahme", L"Captura de ecrã", L"Schermopname",
		L"Przechwytywanie ekranu", L"Ekran yakalama"), MB_OK | MB_ICONERROR);
}

static BOOL ScLiveHttpRequest(
	const wchar_t* host,
	INTERNET_PORT port,
	const wchar_t* method,
	const wchar_t* object,
	const wchar_t* extraHeaders,
	const void* body,
	DWORD bodyLen,
	CStringA& responseOut,
	DWORD* httpStatusOut)
{
	responseOut.Empty();
	if (httpStatusOut) *httpStatusOut = 0;

	HINTERNET hInet = InternetOpen(L"oggScreenCaptureLive/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInet) return FALSE;
	DWORD timeout = 30000;
	InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

	HINTERNET hConn = InternetConnect(hInet, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
	if (!hConn) {
		InternetCloseHandle(hInet);
		return FALSE;
	}

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
	if (port == INTERNET_DEFAULT_HTTPS_PORT)
		flags |= INTERNET_FLAG_SECURE;

	HINTERNET hReq = HttpOpenRequest(hConn, method, object, NULL, NULL, NULL, flags, 0);
	if (!hReq) {
		InternetCloseHandle(hConn);
		InternetCloseHandle(hInet);
		return FALSE;
	}

	BOOL ok = HttpSendRequest(hReq, extraHeaders, extraHeaders ? (DWORD)-1 : 0,
		(LPVOID)body, bodyLen);
	if (!ok) {
		InternetCloseHandle(hReq);
		InternetCloseHandle(hConn);
		InternetCloseHandle(hInet);
		return FALSE;
	}

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	HttpQueryInfo(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, NULL);
	if (httpStatusOut) *httpStatusOut = status;

	char chunk[4096];
	DWORD read = 0;
	while (InternetReadFile(hReq, chunk, sizeof(chunk) - 1, &read) && read > 0) {
		chunk[read] = 0;
		responseOut.Append(chunk, (int)read);
	}

	InternetCloseHandle(hReq);
	InternetCloseHandle(hConn);
	InternetCloseHandle(hInet);
	return TRUE;
}

static BOOL ScLiveFileExists(const TCHAR* path)
{
	if (!path || !path[0]) return FALSE;
	const DWORD a = GetFileAttributes(path);
	return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL ScLiveEnsureAccessToken(CString& errOut)
{
	errOut.Empty();
	const time_t now = time(NULL);
	if (savedata.yt_access_token[0]
		&& savedata.yt_token_exp > 0
		&& (time_t)savedata.yt_token_exp > now + 60) {
		return TRUE;
	}
	if (!savedata.yt_refresh_token[0]) {
		errOut = ScLiveNeedLoginMsg();
		return FALSE;
	}
	if (!ScLiveHaveClientCreds()) {
		errOut = ScLiveMissingClientCredsMsg();
		return FALSE;
	}

	const CString clientId = ScLiveResolveClientId();
	const CString clientSecret = ScLiveResolveClientSecret();

	CStringA body;
	body = "client_id=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(clientId));
	body += "&client_secret=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(clientSecret));
	body += "&refresh_token=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(savedata.yt_refresh_token));
	body += "&grant_type=refresh_token";

	CStringA resp;
	DWORD status = 0;
	const wchar_t* hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
	if (!ScLiveHttpRequest(L"oauth2.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, L"POST",
		L"/token", hdr, (const void*)(LPCSTR)body, (DWORD)body.GetLength(), resp, &status)
		|| status < 200 || status >= 300) {
		errOut.Format(LL14(
			L"トークン更新に失敗しました (HTTP %lu)。再認証してください。",
			L"Token refresh failed (HTTP %lu). Re-authenticate.",
			L"Échec du rafraîchissement (HTTP %lu). Réauth.",
			L"Aggiornamento token non riuscito (HTTP %lu). Riautentica.",
			L"Fallo al renovar token (HTTP %lu). Reautentique.",
			L"토큰 갱신 실패 (HTTP %lu). 재인증하세요.",
			L"令牌刷新失败 (HTTP %lu)。请重新认证。",
			L"فشل تحديث الرمز (HTTP %lu). أعد المصادقة.",
			L"Не удалось обновить токен (HTTP %lu). Повторите вход.",
			L"Token-Aktualisierung fehlgeschlagen (HTTP %lu). Neu anmelden.",
			L"Falha ao renovar token (HTTP %lu). Reautentique.",
			L"Tokenvernieuwing mislukt (HTTP %lu). Opnieuw authenticeren.",
			L"Odświeżenie tokenu nie powiodło się (HTTP %lu). Uwierzytelnij ponownie.",
			L"Jeton yenileme başarısız (HTTP %lu). Yeniden kimlik doğrulayın."),
			(unsigned long)status);
		return FALSE;
	}

	char access[512] = {};
	int expiresIn = 0;
	if (!ScLiveJsonGetString(resp, "access_token", access, (int)_countof(access))) {
		errOut = LL14(
			L"トークン応答を解析できませんでした。",
			L"Could not parse token response.",
			L"Réponse token illisible.",
			L"Risposta token non analizzabile.",
			L"No se pudo analizar la respuesta del token.",
			L"토큰 응답을 해석할 수 없습니다.",
			L"无法解析令牌响应。",
			L"تعذر تحليل استجابة الرمز.",
			L"Не удалось разобрать ответ токена.",
			L"Token-Antwort nicht lesbar.",
			L"Não foi possível analisar a resposta do token.",
			L"Tokenantwoord kon niet worden gelezen.",
			L"Nie można sparsować odpowiedzi tokenu.",
			L"Jeton yanıtı ayrıştırılamadı.");
		return FALSE;
	}
	ScLiveJsonGetInt(resp, "expires_in", &expiresIn);
	if (expiresIn < 60) expiresIn = 3600;

	ScLiveCopyField(savedata.yt_access_token, _countof(savedata.yt_access_token), ScLiveUtf8ToWide(access));
	savedata.yt_token_exp = (int)(time(NULL) + expiresIn);
	MpPersistSavedataQuick();
	return TRUE;
}

static BOOL ScLiveYtApiJson(
	const wchar_t* method,
	const wchar_t* objectWithQuery,
	const CStringA& jsonBody,
	CStringA& respOut,
	CString& errOut)
{
	errOut.Empty();
	respOut.Empty();
	if (!ScLiveEnsureAccessToken(errOut))
		return FALSE;

	CString hdr;
	hdr.Format(L"Authorization: Bearer %s\r\nContent-Type: application/json\r\n",
		savedata.yt_access_token);

	DWORD status = 0;
	const void* bodyPtr = jsonBody.IsEmpty() ? NULL : (const void*)(LPCSTR)jsonBody;
	const DWORD bodyLen = jsonBody.IsEmpty() ? 0 : (DWORD)jsonBody.GetLength();
	if (!ScLiveHttpRequest(L"www.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, method,
		objectWithQuery, hdr, bodyPtr, bodyLen, respOut, &status)) {
		errOut = LL14(
			L"YouTube API 通信に失敗しました。",
			L"YouTube API request failed.",
			L"Échec de la requête YouTube API.",
			L"Richiesta YouTube API non riuscita.",
			L"Falló la solicitud a la API de YouTube.",
			L"YouTube API 요청 실패.",
			L"YouTube API 请求失败。",
			L"فشل طلب YouTube API.",
			L"Сбой запроса YouTube API.",
			L"YouTube-API-Anfrage fehlgeschlagen.",
			L"Falha na solicitação da API do YouTube.",
			L"YouTube API-verzoek mislukt.",
			L"Żądanie YouTube API nie powiodło się.",
			L"YouTube API isteği başarısız.");
		return FALSE;
	}
	if (status < 200 || status >= 300) {
		char apiErr[256] = {};
		ScLiveJsonGetString(respOut, "message", apiErr, (int)_countof(apiErr));
		CString detail = ScLiveUtf8ToWide(apiErr);
		errOut.Format(LL14(
			L"YouTube API エラー (HTTP %lu)%s%s",
			L"YouTube API error (HTTP %lu)%s%s",
			L"Erreur YouTube API (HTTP %lu)%s%s",
			L"Errore YouTube API (HTTP %lu)%s%s",
			L"Error de API de YouTube (HTTP %lu)%s%s",
			L"YouTube API 오류 (HTTP %lu)%s%s",
			L"YouTube API 错误 (HTTP %lu)%s%s",
			L"خطأ YouTube API (HTTP %lu)%s%s",
			L"Ошибка YouTube API (HTTP %lu)%s%s",
			L"YouTube-API-Fehler (HTTP %lu)%s%s",
			L"Erro da API do YouTube (HTTP %lu)%s%s",
			L"YouTube API-fout (HTTP %lu)%s%s",
			L"Błąd YouTube API (HTTP %lu)%s%s",
			L"YouTube API hatası (HTTP %lu)%s%s"),
			(unsigned long)status,
			detail.IsEmpty() ? L"" : L": ",
			(LPCTSTR)detail);
		return FALSE;
	}
	return TRUE;
}

static BOOL ScLiveCreateBindBroadcast(CString& errOut, BOOL updateUi)
{
	errOut.Empty();
	if (!ScLiveEnsureAccessToken(errOut))
		return FALSE;

	CString title = savedata.cap_live_title;
	title.Trim();
	if (title.IsEmpty())
		title = LL14(L"配信", L"Live", L"Live", L"Live", L"En vivo", L"라이브", L"直播", L"بث",
			L"Эфир", L"Live", L"Ao vivo", L"Live", L"Na żywo", L"Canlı");
	CString desc = savedata.cap_live_desc;
	const wchar_t* privacy = L"public";
	if (savedata.cap_live_privacy == 1) privacy = L"unlisted";
	else if (savedata.cap_live_privacy == 2) privacy = L"private";

	SYSTEMTIME st = {};
	GetSystemTime(&st);
	// すぐ開始できるよう、開始時刻を少し過去にして「公開予約」滞留を避ける
	FILETIME ft = {};
	SystemTimeToFileTime(&st, &ft);
	ULARGE_INTEGER uli = {};
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	uli.QuadPart -= 120ULL * 10000000ULL; // 2 minutes
	ft.dwLowDateTime = uli.LowPart;
	ft.dwHighDateTime = uli.HighPart;
	FileTimeToSystemTime(&ft, &st);
	char startIso[64];
	sprintf_s(startIso, "%04u-%02u-%02uT%02u:%02u:%02u.000Z",
		(unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
		(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);

	CStringA titleEsc = ScLiveJsonEscapeUtf8(ScLiveWideToUtf8(title));
	CStringA descEsc = ScLiveJsonEscapeUtf8(ScLiveWideToUtf8(desc));
	CStringA privacyA = ScLiveWideToUtf8(privacy);

	CStringA bodyBc;
	bodyBc.Format(
		"{\"snippet\":{\"title\":\"%s\",\"description\":\"%s\",\"scheduledStartTime\":\"%s\"},"
		"\"status\":{\"privacyStatus\":\"%s\",\"selfDeclaredMadeForKids\":false},"
		"\"contentDetails\":{\"enableAutoStart\":true,\"enableAutoStop\":true,"
		"\"enableDvr\":true,\"recordFromStart\":true,\"startWithSlate\":false,"
		"\"monitorStream\":{\"enableMonitorStream\":true,\"broadcastStreamDelayMs\":0}}}",
		(LPCSTR)titleEsc, (LPCSTR)descEsc, startIso, (LPCSTR)privacyA);

	CStringA respBc;
	if (!ScLiveYtApiJson(L"POST",
		L"/youtube/v3/liveBroadcasts?part=snippet,status,contentDetails",
		bodyBc, respBc, errOut))
		return FALSE;

	char broadcastId[64] = {};
	if (!ScLiveJsonGetString(respBc, "id", broadcastId, (int)_countof(broadcastId))) {
		errOut = LL14(
			L"broadcast id を取得できませんでした。",
			L"Could not get broadcast id.",
			L"Impossible d'obtenir l'id broadcast.",
			L"Impossibile ottenere l'id broadcast.",
			L"No se pudo obtener el id de broadcast.",
			L"broadcast id를 가져오지 못했습니다.",
			L"无法获取 broadcast id。",
			L"تعذر الحصول على معرف البث.",
			L"Не удалось получить id трансляции.",
			L"Broadcast-ID nicht erhalten.",
			L"Não foi possível obter o id do broadcast.",
			L"Kon broadcast-id niet ophalen.",
			L"Nie udało się pobrać id broadcast.",
			L"Broadcast kimliği alınamadı.");
		return FALSE;
	}

	CStringA bodySt =
		"{\"snippet\":{\"title\":\"ogg-capture\"},"
		"\"cdn\":{\"frameRate\":\"variable\",\"ingestionType\":\"rtmp\",\"resolution\":\"variable\"},"
		"\"contentDetails\":{\"isReusable\":true}}";
	CStringA respSt;
	if (!ScLiveYtApiJson(L"POST",
		L"/youtube/v3/liveStreams?part=snippet,cdn,contentDetails,status",
		bodySt, respSt, errOut))
		return FALSE;

	char streamId[64] = {};
	char streamName[256] = {};
	char ingest[512] = {};
	char rtmps[512] = {};
	if (!ScLiveJsonGetString(respSt, "id", streamId, (int)_countof(streamId))) {
		errOut = LL14(
			L"stream id を取得できませんでした。",
			L"Could not get stream id.",
			L"Impossible d'obtenir l'id stream.",
			L"Impossibile ottenere l'id stream.",
			L"No se pudo obtener el id de stream.",
			L"stream id를 가져오지 못했습니다.",
			L"无法获取 stream id。",
			L"تعذر الحصول على معرف التدفق.",
			L"Не удалось получить id потока.",
			L"Stream-ID nicht erhalten.",
			L"Não foi possível obter o id do stream.",
			L"Kon stream-id niet ophalen.",
			L"Nie udało się pobrać id stream.",
			L"Stream kimliği alınamadı.");
		return FALSE;
	}
	ScLiveJsonGetString(respSt, "streamName", streamName, (int)_countof(streamName));
	ScLiveJsonGetString(respSt, "ingestionAddress", ingest, (int)_countof(ingest));
	ScLiveJsonGetString(respSt, "rtmpsIngestionAddress", rtmps, (int)_countof(rtmps));
	if (!streamName[0] || (!ingest[0] && !rtmps[0])) {
		errOut = LL14(
			L"RTMP 接続情報を取得できませんでした。",
			L"Could not get RTMP ingestion info.",
			L"Infos RTMP introuvables.",
			L"Info RTMP non trovate.",
			L"No se pudo obtener info RTMP.",
			L"RTMP 접속 정보를 가져오지 못했습니다.",
			L"无法获取 RTMP 接入信息。",
			L"تعذر الحصول على معلومات RTMP.",
			L"Не удалось получить данные RTMP.",
			L"RTMP-Infos nicht erhalten.",
			L"Não foi possível obter info RTMP.",
			L"Kon RTMP-info niet ophalen.",
			L"Nie udało się pobrać info RTMP.",
			L"RTMP bilgisi alınamadı.");
		return FALSE;
	}

	CString objBind;
	objBind.Format(L"/youtube/v3/liveBroadcasts/bind?id=%s&streamId=%s&part=id,contentDetails",
		ScLiveUtf8ToWide(broadcastId), ScLiveUtf8ToWide(streamId));
	CStringA respBind;
	if (!ScLiveYtApiJson(L"POST", objBind, CStringA(), respBind, errOut))
		return FALSE;

	const CStringA urlA = ingest[0] ? CStringA(ingest) : CStringA(rtmps);
	const CString urlW = ScLiveUtf8ToWide(urlA);
	const CString keyW = ScLiveUtf8ToWide(streamName);

	ScLiveCopyField(savedata.yt_broadcast_id, _countof(savedata.yt_broadcast_id), ScLiveUtf8ToWide(broadcastId));
	ScLiveCopyField(savedata.yt_stream_id, _countof(savedata.yt_stream_id), ScLiveUtf8ToWide(streamId));
	ScLiveCopyField(savedata.cap_live_url, _countof(savedata.cap_live_url), urlW);
	ScLiveCopyField(savedata.cap_live_key, _countof(savedata.cap_live_key), keyW);
	MpPersistSavedataQuick();

	if (updateUi)
		ScLiveSettingsApplyUrlKey(urlW, keyW);
	return TRUE;
}

static BOOL ScLiveOAuthLoopback(CWnd* owner, CString& errOut)
{
	errOut.Empty();
	if (!ScLiveHaveClientCreds()) {
		errOut = ScLiveMissingClientCredsMsg();
		return FALSE;
	}

	const CString clientId = ScLiveResolveClientId();
	const CString clientSecret = ScLiveResolveClientSecret();

	WSADATA wsa = {};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		errOut = LL14(
			L"ネットワーク初期化に失敗しました。",
			L"Network init failed.",
			L"Échec d'init réseau.",
			L"Init rete non riuscita.",
			L"Fallo al iniciar red.",
			L"네트워크 초기화 실패.",
			L"网络初始化失败。",
			L"فشل تهيئة الشبكة.",
			L"Сбой инициализации сети.",
			L"Netzwerk-Init fehlgeschlagen.",
			L"Falha na init de rede.",
			L"Netwerkinit mislukt.",
			L"Inicjalizacja sieci nie powiodła się.",
			L"Ağ başlatma başarısız.");
		return FALSE;
	}

	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock == INVALID_SOCKET) {
		WSACleanup();
		errOut = LL14(L"ソケットを作成できません。", L"Cannot create socket.", L"Socket impossible.",
			L"Socket non creato.", L"No se puede crear socket.", L"소켓을 만들 수 없습니다.",
			L"无法创建套接字。", L"تعذر إنشاء المقبس.", L"Не удалось создать сокет.", L"Socket nicht erstellbar.",
			L"Não é possível criar socket.", L"Kan socket niet maken.", L"Nie można utworzyć gniazda.", L"Soket oluşturulamadı.");
		return FALSE;
	}

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) != 0
		|| listen(listenSock, 1) != 0) {
		closesocket(listenSock);
		WSACleanup();
		errOut = LL14(L"ループバック待受に失敗しました。", L"Loopback listen failed.", L"Écoute loopback échouée.",
			L"Listen loopback non riuscita.", L"Falló listen loopback.", L"루프백 수신 실패.",
			L"环回监听失败。", L"فشل الاستماع المحلي.", L"Сбой прослушивания loopback.", L"Loopback-Listen fehlgeschlagen.",
			L"Falha no listen loopback.", L"Loopback-listen mislukt.", L"Listen loopback nie powiodło się.", L"Loopback dinleme başarısız.");
		return FALSE;
	}

	sockaddr_in bound = {};
	int boundLen = sizeof(bound);
	getsockname(listenSock, (sockaddr*)&bound, &boundLen);
	const int port = (int)ntohs(bound.sin_port);

	CStringA redirectA;
	redirectA.Format("http://localhost:%d/", port);
	CStringA scopeA = "https://www.googleapis.com/auth/youtube.force-ssl";
	CStringA authUrl;
	authUrl.Format(
		"https://accounts.google.com/o/oauth2/v2/auth?client_id=%s&redirect_uri=%s"
		"&response_type=code&scope=%s&access_type=offline&prompt=consent",
		(LPCSTR)ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(clientId)),
		(LPCSTR)ScLiveUrlEncodeUtf8(redirectA),
		(LPCSTR)ScLiveUrlEncodeUtf8(scopeA));

	CString authUrlW = ScLiveUtf8ToWide(authUrl);
	if ((INT_PTR)ShellExecute(owner ? owner->GetSafeHwnd() : NULL, L"open", authUrlW, NULL, NULL, SW_SHOWNORMAL) <= 32) {
		closesocket(listenSock);
		WSACleanup();
		errOut = LL14(
			L"ブラウザを開けませんでした。",
			L"Could not open the browser.",
			L"Impossible d'ouvrir le navigateur.",
			L"Impossibile aprire il browser.",
			L"No se pudo abrir el navegador.",
			L"브라우저를 열 수 없습니다.",
			L"无法打开浏览器。",
			L"تعذر فتح المتصفح.",
			L"Не удалось открыть браузер.",
			L"Browser konnte nicht geöffnet werden.",
			L"Não foi possível abrir o navegador.",
			L"Kon de browser niet openen.",
			L"Nie można otworzyć przeglądarki.",
			L"Tarayıcı açılamadı.");
		return FALSE;
	}

	// 非ブロッキング待受 + タイムアウト（ユーザーがブラウザで許可する）
	DWORD mode = 1;
	ioctlsocket(listenSock, FIONBIO, &mode);
	SOCKET client = INVALID_SOCKET;
	const DWORD t0 = GetTickCount();
	while (GetTickCount() - t0 < 180000) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(listenSock, &fds);
		timeval tv = {};
		tv.tv_sec = 1;
		const int sel = select(0, &fds, NULL, NULL, &tv);
		if (sel > 0 && FD_ISSET(listenSock, &fds)) {
			client = accept(listenSock, NULL, NULL);
			if (client != INVALID_SOCKET) break;
		}
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	closesocket(listenSock);

	if (client == INVALID_SOCKET) {
		WSACleanup();
		errOut = LL14(
			L"認証がタイムアウトしました（ブラウザで許可してください）。",
			L"Auth timed out (approve in the browser).",
			L"Auth expirée (approuvez dans le navigateur).",
			L"Auth scaduta (autorizza nel browser).",
			L"Auth agotada (apruebe en el navegador).",
			L"인증 시간 초과(브라우저에서 허용하세요).",
			L"认证超时（请在浏览器中允许）。",
			L"انتهت مهلة المصادقة (وافق في المتصفح).",
			L"Таймаут авторизации (разрешите в браузере).",
			L"Auth-Timeout (im Browser zustimmen).",
			L"Auth expirou (aprove no navegador).",
			L"Auth time-out (goedkeuren in de browser).",
			L"Limit czasu auth (zatwierdź w przeglądarce).",
			L"Kimlik doğrulama zaman aşımı (tarayıcıda onaylayın).");
		return FALSE;
	}

	char reqBuf[4096] = {};
	int total = 0;
	DWORD mode0 = 0;
	ioctlsocket(client, FIONBIO, &mode0);
	while (total < (int)sizeof(reqBuf) - 1) {
		const int n = recv(client, reqBuf + total, (int)sizeof(reqBuf) - 1 - total, 0);
		if (n <= 0) break;
		total += n;
		reqBuf[total] = 0;
		if (strstr(reqBuf, "\r\n\r\n")) break;
	}

	char code[512] = {};
	const char* q = strstr(reqBuf, "GET ");
	if (q) {
		const char* codeKey = strstr(q, "code=");
		if (codeKey) {
			codeKey += 5;
			int i = 0;
			while (codeKey[i] && codeKey[i] != '&' && codeKey[i] != ' ' && codeKey[i] != '\r'
				&& i + 1 < (int)_countof(code)) {
				code[i] = codeKey[i];
				++i;
			}
			code[i] = 0;
		}
	}

	const char* okPage =
		"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
		"<html><body><h2>YouTube auth OK</h2><p>You can close this window.</p></body></html>";
	send(client, okPage, (int)strlen(okPage), 0);
	closesocket(client);

	if (!code[0]) {
		WSACleanup();
		errOut = LL14(
			L"認証コードを取得できませんでした。",
			L"Could not get authorization code.",
			L"Code d'auth introuvable.",
			L"Codice auth non trovato.",
			L"No se obtuvo el código de autorización.",
			L"인증 코드를 받지 못했습니다.",
			L"未获得授权码。",
			L"تعذر الحصول على رمز التفويض.",
			L"Не получен код авторизации.",
			L"Auth-Code nicht erhalten.",
			L"Não foi possível obter o código de autorização.",
			L"Kon autorisatiecode niet ophalen.",
			L"Nie uzyskano kodu autoryzacji.",
			L"Yetkilendirme kodu alınamadı.");
		return FALSE;
	}

	CStringA body;
	body = "code=";
	body += ScLiveUrlEncodeUtf8(CStringA(code));
	body += "&client_id=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(clientId));
	body += "&client_secret=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(clientSecret));
	body += "&redirect_uri=";
	body += ScLiveUrlEncodeUtf8(redirectA);
	body += "&grant_type=authorization_code";

	CStringA resp;
	DWORD status = 0;
	const wchar_t* hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
	const BOOL httpOk = ScLiveHttpRequest(L"oauth2.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, L"POST",
		L"/token", hdr, (const void*)(LPCSTR)body, (DWORD)body.GetLength(), resp, &status);
	WSACleanup();

	if (!httpOk || status < 200 || status >= 300) {
		char errKey[64] = {};
		char errDesc[256] = {};
		ScLiveJsonGetString(resp, "error", errKey, (int)_countof(errKey));
		ScLiveJsonGetString(resp, "error_description", errDesc, (int)_countof(errDesc));
		CString detail;
		if (errKey[0] || errDesc[0]) {
			detail = ScLiveUtf8ToWide(CStringA(errKey));
			if (errDesc[0]) {
				if (!detail.IsEmpty()) detail += L": ";
				detail += ScLiveUtf8ToWide(CStringA(errDesc));
			}
		}
		if (detail.IsEmpty()) {
			errOut.Format(LL14(
				L"トークン交換に失敗しました (HTTP %lu)。",
				L"Token exchange failed (HTTP %lu).",
				L"Échange de jeton échoué (HTTP %lu).",
				L"Scambio token non riuscito (HTTP %lu).",
				L"Intercambio de token fallido (HTTP %lu).",
				L"토큰 교환 실패 (HTTP %lu).",
				L"令牌交换失败 (HTTP %lu)。",
				L"فشل تبادل الرمز (HTTP %lu).",
				L"Обмен токена не удался (HTTP %lu).",
				L"Token-Austausch fehlgeschlagen (HTTP %lu).",
				L"Troca de token falhou (HTTP %lu).",
				L"Tokenuitwisseling mislukt (HTTP %lu).",
				L"Wymiana tokenu nie powiodła się (HTTP %lu).",
				L"Jeton değişimi başarısız (HTTP %lu)."),
				(unsigned long)status);
		} else {
			errOut.Format(LL14(
				L"トークン交換に失敗しました (HTTP %lu)。\n%s",
				L"Token exchange failed (HTTP %lu).\n%s",
				L"Échange de jeton échoué (HTTP %lu).\n%s",
				L"Scambio token non riuscito (HTTP %lu).\n%s",
				L"Intercambio de token fallido (HTTP %lu).\n%s",
				L"토큰 교환 실패 (HTTP %lu).\n%s",
				L"令牌交换失败 (HTTP %lu)。\n%s",
				L"فشل تبادل الرمز (HTTP %lu).\n%s",
				L"Обмен токена не удался (HTTP %lu).\n%s",
				L"Token-Austausch fehlgeschlagen (HTTP %lu).\n%s",
				L"Troca de token falhou (HTTP %lu).\n%s",
				L"Tokenuitwisseling mislukt (HTTP %lu).\n%s",
				L"Wymiana tokenu nie powiodła się (HTTP %lu).\n%s",
				L"Jeton değişimi başarısız (HTTP %lu).\n%s"),
				(unsigned long)status, (LPCWSTR)detail);
		}
		return FALSE;
	}

	char access[512] = {};
	char refresh[512] = {};
	int expiresIn = 0;
	if (!ScLiveJsonGetString(resp, "access_token", access, (int)_countof(access))) {
		errOut = LL14(
			L"トークン応答を解析できませんでした。",
			L"Could not parse token response.",
			L"Réponse token illisible.",
			L"Risposta token non analizzabile.",
			L"No se pudo analizar la respuesta del token.",
			L"토큰 응답을 해석할 수 없습니다.",
			L"无法解析令牌响应。",
			L"تعذر تحليل استجابة الرمز.",
			L"Не удалось разобрать ответ токена.",
			L"Token-Antwort nicht lesbar.",
			L"Não foi possível analisar a resposta do token.",
			L"Tokenantwoord kon niet worden gelezen.",
			L"Nie można sparsować odpowiedzi tokenu.",
			L"Jeton yanıtı ayrıştırılamadı.");
		return FALSE;
	}
	ScLiveJsonGetString(resp, "refresh_token", refresh, (int)_countof(refresh));
	ScLiveJsonGetInt(resp, "expires_in", &expiresIn);
	if (expiresIn < 60) expiresIn = 3600;

	ScLiveCopyField(savedata.yt_access_token, _countof(savedata.yt_access_token), ScLiveUtf8ToWide(access));
	if (refresh[0])
		ScLiveCopyField(savedata.yt_refresh_token, _countof(savedata.yt_refresh_token), ScLiveUtf8ToWide(refresh));
	savedata.yt_token_exp = (int)(time(NULL) + expiresIn);
	MpPersistSavedataQuick();
	return TRUE;
}

} // namespace

BOOL CScreenCaptureDlg::ResolveFfmpegPath(TCHAR* outPath, int outCch) const
{
	if (!outPath || outCch <= 0) return FALSE;
	outPath[0] = 0;

	TCHAR mod[MAX_PATH] = {};
	if (!GetModuleFileName(NULL, mod, MAX_PATH))
		return FALSE;
	TCHAR* slash = _tcsrchr(mod, L'\\');
	if (!slash) return FALSE;
	*slash = 0;
	const CString exeDir = mod;

	TCHAR cand[MAX_PATH] = {};
	_sntprintf_s(cand, _TRUNCATE, L"%s\\ffmpeg.exe", (LPCTSTR)exeDir);
	if (ScLiveFileExists(cand)) {
		_tcsncpy(outPath, cand, outCch - 1);
		outPath[outCch - 1] = 0;
		return TRUE;
	}

	_sntprintf_s(cand, _TRUNCATE, L"%s\\..\\ogg_binary\\ffmpeg.exe", (LPCTSTR)exeDir);
	TCHAR full[MAX_PATH] = {};
	if (GetFullPathName(cand, MAX_PATH, full, NULL) && ScLiveFileExists(full)) {
		_tcsncpy(outPath, full, outCch - 1);
		outPath[outCch - 1] = 0;
		return TRUE;
	}

	_sntprintf_s(cand, _TRUNCATE, L"%s\\..\\..\\ogg_binary\\ffmpeg.exe", (LPCTSTR)exeDir);
	if (GetFullPathName(cand, MAX_PATH, full, NULL) && ScLiveFileExists(full)) {
		_tcsncpy(outPath, full, outCch - 1);
		outPath[outCch - 1] = 0;
		return TRUE;
	}

	// PATH 上の ffmpeg.exe
	if (SearchPath(NULL, L"ffmpeg.exe", NULL, MAX_PATH, full, NULL) && ScLiveFileExists(full)) {
		_tcsncpy(outPath, full, outCch - 1);
		outPath[outCch - 1] = 0;
		return TRUE;
	}

	WIN32_FIND_DATA fd = {};
	CString pattern;
	pattern.Format(L"%s\\oggYSEDbgm*.exe", (LPCTSTR)exeDir);
	HANDLE h = FindFirstFile(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		FindClose(h);
		_sntprintf_s(cand, _TRUNCATE, L"%s\\ffmpeg.exe", (LPCTSTR)exeDir);
		if (ScLiveFileExists(cand)) {
			_tcsncpy(outPath, cand, outCch - 1);
			outPath[outCch - 1] = 0;
			return TRUE;
		}
	}

	// 親ディレクトリ側の oggYSEDbgm* 隣も探索
	TCHAR parent[MAX_PATH] = {};
	_tcsncpy(parent, exeDir, MAX_PATH - 1);
	slash = _tcsrchr(parent, L'\\');
	if (slash) {
		*slash = 0;
		pattern.Format(L"%s\\oggYSEDbgm*.exe", parent);
		h = FindFirstFile(pattern, &fd);
		if (h != INVALID_HANDLE_VALUE) {
			FindClose(h);
			_sntprintf_s(cand, _TRUNCATE, L"%s\\ffmpeg.exe", parent);
			if (ScLiveFileExists(cand)) {
				_tcsncpy(outPath, cand, outCch - 1);
				outPath[outCch - 1] = 0;
				return TRUE;
			}
		}
	}

	return FALSE;
}

static void ScLivePumpUi()
{
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			PostQuitMessage((int)msg.wParam);
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static BOOL ScLiveIsMzPe(const TCHAR* path, ULONGLONG minBytes)
{
	if (!path || !path[0]) return FALSE;
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
		return FALSE;
	ULARGE_INTEGER sz;
	sz.LowPart = fad.nFileSizeLow;
	sz.HighPart = fad.nFileSizeHigh;
	if (sz.QuadPart < minBytes)
		return FALSE;
	HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return FALSE;
	unsigned char mz[2] = {};
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, mz, 2, &rd, NULL);
	CloseHandle(h);
	return ok && rd == 2 && mz[0] == 'M' && mz[1] == 'Z';
}

static BOOL ScLiveDownloadUrlToFile(const TCHAR* url, const TCHAR* localPath, HWND hBusy, CString& errOut)
{
	errOut.Empty();
	HINTERNET hInet = InternetOpen(L"oggScreenCaptureLive/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
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

	const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
		| INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_UI;
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

	DeleteFile(localPath);
	HANDLE hFile = CreateFile(localPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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
		if (hBusy && (now - lastUi) >= 400) {
			lastUi = now;
			CString t;
			t.Format(LL14(
				L"ffmpeg をダウンロード中…\n%.1f MB",
				L"Downloading ffmpeg…\n%.1f MB",
				L"Telechargement de ffmpeg…\n%.1f Mo",
				L"Download di ffmpeg…\n%.1f MB",
				L"Descargando ffmpeg…\n%.1f MB",
				L"ffmpeg 다운로드 중…\n%.1f MB",
				L"正在下载 ffmpeg…\n%.1f MB",
				L"جارٍ تنزيل ffmpeg…\n%.1f MB",
				L"Загрузка ffmpeg…\n%.1f МБ",
				L"ffmpeg wird heruntergeladen…\n%.1f MB",
				L"A descarregar ffmpeg…\n%.1f MB",
				L"ffmpeg downloaden…\n%.1f MB",
				L"Pobieranie ffmpeg…\n%.1f MB",
				L"ffmpeg indiriliyor…\n%.1f MB"),
				(double)total / (1024.0 * 1024.0));
			SetWindowText(hBusy, t);
			ScLivePumpUi();
		}
	}
	CloseHandle(hFile);
	InternetCloseHandle(hUrl);
	InternetCloseHandle(hInet);

	if (!ok || total < 1000000ULL) {
		DeleteFile(localPath);
		errOut = LL14(L"ダウンロードが完了しませんでした。", L"Download incomplete.", L"Telechargement incomplet.",
			L"Download incompleto.", L"Descarga incompleta.", L"다운로드가 완료되지 않았습니다.",
			L"下载未完成。", L"التنزيل غير مكتمل.", L"Загрузка не завершена.", L"Download unvollstandig.",
			L"Download incompleto.", L"Download onvolledig.", L"Pobieranie niekompletne.", L"Indirme tamamlanmadi.");
		return FALSE;
	}
	return TRUE;
}

static BOOL ScLiveExtractFfmpegFromZip(const TCHAR* zipPath, const TCHAR* destDir, CString& errOut)
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

	BOOL found = FALSE;
	for (ZPOS64_T i = 0; i < gi.number_entry; i++) {
		char filename_inzip[1024] = {};
		unz_file_info64 fi = {};
		if (unzGetCurrentFileInfo64(uf, &fi, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) != UNZ_OK)
			break;
		const char* fileNameOnly = filename_inzip;
		for (const char* p = filename_inzip; *p; ++p) {
			if (*p == '/' || *p == '\\')
				fileNameOnly = p + 1;
		}
		if (!fileNameOnly[0]) {
			if ((ZPOS64_T)(i + 1) < gi.number_entry) unzGoToNextFile(uf);
			continue;
		}
		CString currentFileName = CA2T(fileNameOnly, CP_UTF8);
		if (currentFileName.CompareNoCase(L"ffmpeg.exe") != 0) {
			if ((ZPOS64_T)(i + 1) < gi.number_entry) unzGoToNextFile(uf);
			continue;
		}
		if (unzOpenCurrentFile(uf) != UNZ_OK)
			break;
		CString outPath;
		outPath.Format(L"%s\\ffmpeg.exe", destDir);
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
		const ULONGLONG minBytes = 1000000ULL;
		if (writeOk && written == (ULONGLONG)fi.uncompressed_size
			&& written >= minBytes && ScLiveIsMzPe(outPath, minBytes)) {
			found = TRUE;
		} else {
			DeleteFile(outPath);
		}
		break;
	}
	unzClose(uf);
	if (!found) {
		errOut = LL14(L"ZIP 内に ffmpeg.exe が見つかりません。", L"ffmpeg.exe not found in ZIP.",
			L"ffmpeg.exe introuvable dans le ZIP.", L"ffmpeg.exe non trovato nello ZIP.",
			L"No se encontro ffmpeg.exe en el ZIP.", L"ZIP에 ffmpeg.exe가 없습니다.",
			L"ZIP 中找不到 ffmpeg.exe。", L"لم يُعثر على ffmpeg.exe في ZIP.",
			L"В ZIP нет ffmpeg.exe.", L"ffmpeg.exe nicht in ZIP gefunden.",
			L"ffmpeg.exe nao encontrado no ZIP.", L"ffmpeg.exe niet in ZIP gevonden.",
			L"Brak ffmpeg.exe w ZIP.", L"ZIP icinde ffmpeg.exe yok.");
		return FALSE;
	}
	return TRUE;
}

static HWND ScLiveCreateBusyWnd(CWnd* owner, const CString& text)
{
	HWND parent = owner && owner->GetSafeHwnd() ? owner->GetSafeHwnd() : NULL;
	RECT pr = { 100, 100, 520, 220 };
	if (parent) {
		GetWindowRect(parent, &pr);
		const int cx = (pr.left + pr.right) / 2;
		const int cy = (pr.top + pr.bottom) / 2;
		pr.left = cx - 210;
		pr.top = cy - 60;
		pr.right = cx + 210;
		pr.bottom = cy + 60;
	}
	HWND h = CreateWindowEx(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME,
		L"STATIC", (LPCTSTR)text,
		WS_POPUP | WS_VISIBLE | WS_BORDER | SS_CENTER,
		pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top,
		parent, NULL, AfxGetInstanceHandle(), NULL);
	if (h) {
		HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
		SendMessage(h, WM_SETFONT, (WPARAM)font, TRUE);
		UpdateWindow(h);
	}
	return h;
}

static BOOL ScLiveInstallFfmpegFromOfficial(CWnd* owner, CString& errOut)
{
	errOut.Empty();
	TCHAR mod[MAX_PATH] = {};
	if (!GetModuleFileName(NULL, mod, MAX_PATH)) {
		errOut = LL14(L"実行フォルダを取得できません。", L"Could not resolve exe folder.",
			L"Dossier exe introuvable.", L"Cartella exe non trovata.", L"No se pudo obtener la carpeta.",
			L"실행 폴더를 알 수 없습니다.", L"无法取得程序目录。", L"تعذر معرفة مجلد البرنامج.",
			L"Не удалось получить папку exe.", L"Exe-Ordner unbekannt.", L"Pasta do exe desconhecida.",
			L"Exe-map onbekend.", L"Nieznany folder exe.", L"Exe klasoru bilinmiyor.");
		return FALSE;
	}
	TCHAR* slash = _tcsrchr(mod, L'\\');
	if (!slash) return FALSE;
	*slash = 0;
	const CString exeDir = mod;

	TCHAR tmp[MAX_PATH] = {};
	GetTempPath(MAX_PATH, tmp);
	TCHAR zipPath[MAX_PATH] = {};
	_sntprintf_s(zipPath, _TRUNCATE, L"%sogg_ffmpeg_essentials.zip", tmp);

	const CString busy0 = LL14(
		L"ffmpeg をダウンロード中…\nしばらくお待ちください",
		L"Downloading ffmpeg…\nPlease wait",
		L"Telechargement de ffmpeg…\nVeuillez patienter",
		L"Download di ffmpeg…\nAttendere",
		L"Descargando ffmpeg…\nEspere",
		L"ffmpeg 다운로드 중…\n잠시만 기다려 주세요",
		L"正在下载 ffmpeg…\n请稍候",
		L"جارٍ تنزيل ffmpeg…\nيرجى الانتظار",
		L"Загрузка ffmpeg…\nПодождите",
		L"ffmpeg wird heruntergeladen…\nBitte warten",
		L"A descarregar ffmpeg…\nAguarde",
		L"ffmpeg downloaden…\nEven geduld",
		L"Pobieranie ffmpeg…\nProsze czekac",
		L"ffmpeg indiriliyor…\nLutfen bekleyin");
	HWND hBusy = ScLiveCreateBusyWnd(owner, busy0);
	if (owner) owner->EnableWindow(FALSE);

	// gyan.dev = ffmpeg.org が案内する Windows 公式ビルド（essentials ZIP）
	const TCHAR* url = L"https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip";
	BOOL ok = ScLiveDownloadUrlToFile(url, zipPath, hBusy, errOut);
	if (ok) {
		if (hBusy) {
			SetWindowText(hBusy, LL14(
				L"ffmpeg を配置中…",
				L"Installing ffmpeg…",
				L"Installation de ffmpeg…",
				L"Installazione di ffmpeg…",
				L"Instalando ffmpeg…",
				L"ffmpeg 배치 중…",
				L"正在安装 ffmpeg…",
				L"جارٍ تثبيت ffmpeg…",
				L"Установка ffmpeg…",
				L"ffmpeg wird installiert…",
				L"A instalar ffmpeg…",
				L"ffmpeg installeren…",
				L"Instalowanie ffmpeg…",
				L"ffmpeg kuruluyor…"));
			ScLivePumpUi();
		}
		ok = ScLiveExtractFfmpegFromZip(zipPath, exeDir, errOut);
	}
	DeleteFile(zipPath);

	if (owner) owner->EnableWindow(TRUE);
	if (hBusy) DestroyWindow(hBusy);
	return ok;
}

BOOL CScreenCaptureDlg::EnsureFfmpegAvailable(BOOL promptIfMissing)
{
	TCHAR path[MAX_PATH] = {};
	if (ResolveFfmpegPath(path, MAX_PATH))
		return TRUE;
	if (!promptIfMissing)
		return FALSE;

	const int ans = AfxMessageBox(LL14(
		L"ffmpeg.exe が見つかりません。\nライブ配信に必要です。\n公式ビルドをダウンロードして、このプログラムと同じフォルダに配置しますか？",
		L"ffmpeg.exe was not found.\nIt is required for live streaming.\nDownload the official build and place it next to this program?",
		L"ffmpeg.exe introuvable.\nNecessaire pour le live.\nTelecharger la build officielle a cote de ce programme ?",
		L"ffmpeg.exe non trovato.\nServe per la diretta.\nScaricare la build ufficiale accanto a questo programma?",
		L"No se encontro ffmpeg.exe.\nEs necesario para el directo.\n¿Descargar la build oficial junto a este programa?",
		L"ffmpeg.exe를 찾을 수 없습니다.\n라이브에 필요합니다.\n공식 빌드를 받아 이 프로그램과 같은 폴더에 둘까요?",
		L"找不到 ffmpeg.exe。\n直播需要它。\n是否下载官方构建并放到本程序同目录？",
		L"لم يُعثر على ffmpeg.exe.\nمطلوب للبث.\nتنزيل البناء الرسمي ووضعه بجانب البرنامج؟",
		L"ffmpeg.exe не найден.\nНужен для эфира.\nСкачать официальную сборку рядом с программой?",
		L"ffmpeg.exe nicht gefunden.\nFur Live noetig.\nOffizielle Build herunterladen und neben dieses Programm legen?",
		L"ffmpeg.exe nao encontrado.\nNecessario para ao vivo.\nDescarregar a build oficial ao lado deste programa?",
		L"ffmpeg.exe niet gevonden.\nNodig voor live.\nOfficiele build downloaden en naast dit programma zetten?",
		L"Nie znaleziono ffmpeg.exe.\nWymagane do live.\nPobrac oficjalny build i umiescic obok programu?",
		L"ffmpeg.exe bulunamadi.\nCanli yayin icin gerekli.\nResmi surumu indirip bu programla ayni klasore koyayim mi?"),
		MB_YESNO | MB_ICONQUESTION);
	if (ans != IDYES)
		return FALSE;

	CString err;
	if (!ScLiveInstallFfmpegFromOfficial(this, err)) {
		if (err.IsEmpty()) {
			err = LL14(L"ffmpeg の導入に失敗しました。", L"Failed to install ffmpeg.",
				L"Echec de l'installation de ffmpeg.", L"Installazione di ffmpeg non riuscita.",
				L"Fallo la instalacion de ffmpeg.", L"ffmpeg 설치 실패.", L"安装 ffmpeg 失败。",
				L"فشل تثبيت ffmpeg.", L"Не удалось установить ffmpeg.", L"Installation von ffmpeg fehlgeschlagen.",
				L"Falha ao instalar ffmpeg.", L"Installatie van ffmpeg mislukt.", L"Instalacja ffmpeg nieudana.",
				L"ffmpeg kurulumu basarisiz.");
		}
		AfxMessageBox(err, MB_ICONERROR);
		return FALSE;
	}

	path[0] = 0;
	if (!ResolveFfmpegPath(path, MAX_PATH)) {
		AfxMessageBox(LL14(
			L"配置後も ffmpeg.exe を認識できません。",
			L"ffmpeg.exe still not recognized after install.",
			L"ffmpeg.exe toujours non reconnu.",
			L"ffmpeg.exe ancora non riconosciuto.",
			L"ffmpeg.exe sigue sin reconocerse.",
			L"배치 후에도 ffmpeg.exe를 찾지 못했습니다.",
			L"安装后仍无法识别 ffmpeg.exe。",
			L"ما زال ffmpeg.exe غير معروف.",
			L"ffmpeg.exe всё ещё не распознан.",
			L"ffmpeg.exe nach Installation nicht erkannt.",
			L"ffmpeg.exe ainda nao reconhecido.",
			L"ffmpeg.exe nog niet herkend.",
			L"ffmpeg.exe nadal nierozpoznany.",
			L"ffmpeg.exe hala taninamadi."), MB_ICONERROR);
		return FALSE;
	}

	AfxMessageBox(LL14(
		L"ffmpeg の配置が完了しました。ライブ配信が可能です。",
		L"ffmpeg is ready. Live streaming is available.",
		L"ffmpeg est pret. Le live est disponible.",
		L"ffmpeg e pronto. La diretta e disponibile.",
		L"ffmpeg listo. El directo esta disponible.",
		L"ffmpeg 배치 완료. 라이브 방송이 가능합니다.",
		L"ffmpeg 已就绪。可以开始直播。",
		L"ffmpeg جاهز. البث المباشر متاح.",
		L"ffmpeg готов. Эфир доступен.",
		L"ffmpeg ist bereit. Live-Streaming ist verfugbar.",
		L"ffmpeg pronto. Transmissao ao vivo disponivel.",
		L"ffmpeg is klaar. Live-streaming is beschikbaar.",
		L"ffmpeg gotowy. Live jest dostepny.",
		L"ffmpeg hazir. Canli yayin kullanilabilir."), MB_ICONINFORMATION);
	return TRUE;
}

void CScreenCaptureDlg::PersistLiveFieldsFromUi()
{
	if (!GetSafeHwnd()) return;
	savedata.cap_live_mode = (m_live.GetSafeHwnd() && m_live.GetCheck()) ? 1 : 0;
	// 詳細フィールドは専用UI側。開いていればそこから flush
	SyncScLiveSettingsIfOpen();
}

void CScreenCaptureDlg::ApplyLiveFieldsToUi()
{
	if (!GetSafeHwnd()) return;
	if (m_live.GetSafeHwnd())
		m_live.SetCheck(savedata.cap_live_mode ? BST_CHECKED : BST_UNCHECKED);
	SyncLiveUiEnable();
}

void CScreenCaptureDlg::SyncLiveUiEnable()
{
	if (!GetSafeHwnd()) return;
	const BOOL liveOn = m_live.GetSafeHwnd() && m_live.GetCheck();

	if (m_live.GetSafeHwnd()) {
		m_live.SetWindowText(LL14(
			L"ライブ配信", L"Live stream", L"Diffusion live", L"Diretta",
			L"Transmisión en vivo", L"라이브 방송", L"直播", L"بث مباشر",
			L"Прямой эфир", L"Livestream", L"Transmissão ao vivo", L"Livestream",
			L"Transmisja na żywo", L"Canlı yayın"));
	}
	if (m_liveCfg.GetSafeHwnd()) {
		m_liveCfg.SetWindowText(LL14(
			L"設定…", L"Settings…", L"Réglages…", L"Impostazioni…",
			L"Ajustes…", L"설정…", L"设置…", L"إعدادات…",
			L"Настройки…", L"Einstellungen…", L"Definições…", L"Instellingen…",
			L"Ustawienia…", L"Ayarlar…"));
		m_liveCfg.EnableWindow(TRUE);
	}

	if (!InterlockedCompareExchange(&m_run, 0, 0)) {
		if (liveOn) {
			m_start.SetWindowText(LL14(
				L"配信開始", L"Go live", L"Diffuser", L"Vai in diretta",
				L"Emitir", L"방송 시작", L"开始直播", L"بدء البث",
				L"В эфир", L"Live starten", L"Entrar ao vivo", L"Live starten",
				L"Rozpocznij transmisję", L"Yayına başla"));
		} else {
			m_start.SetWindowText(LL14(
				L"録画開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء",
				L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
		}
	}
	RefreshOpaqueUi();
}

BOOL CScreenCaptureDlg::PrepareYouTubeLiveBeforeStart(CString& errOut)
{
	SyncScLiveSettingsIfOpen();
	if (savedata.yt_refresh_token[0]) {
		// dat / セッション保持トークンで再ログイン省略
		if (!ScLiveEnsureAccessToken(errOut)) {
			// refresh 失効時のみ再認証
			if (!ScLiveOAuthLoopback(this, errOut))
				return FALSE;
		}
	} else {
		if (!ScLiveOAuthLoopback(this, errOut))
			return FALSE;
	}
	MpPersistSavedataQuick();
	return ScLiveCreateBindBroadcast(errOut, TRUE);
}

void CScreenCaptureDlg::FinishYouTubeLiveAfterStop()
{
	if (!savedata.yt_broadcast_id[0])
		return;
	CString err;
	if (!ScLiveEnsureAccessToken(err))
		return;

	CString obj;
	obj.Format(L"/youtube/v3/liveBroadcasts/transition?broadcastStatus=complete&id=%s&part=status",
		savedata.yt_broadcast_id);
	CStringA resp;
	ScLiveYtApiJson(L"POST", obj, CStringA(), resp, err);
	savedata.yt_broadcast_id[0] = 0;
	savedata.yt_stream_id[0] = 0;
	MpPersistSavedataQuick();
}

void CScreenCaptureDlg::TryYouTubeGoLiveTransition()
{
	if (!savedata.yt_broadcast_id[0] || !savedata.yt_stream_id[0]) {
		InterlockedExchange(&m_ytLivePhase, 5); // 枠IDなし
		return;
	}
	if (m_ytLiveTransitionDone)
		return;

	CString err;
	if (!ScLiveEnsureAccessToken(err)) {
		_tcsncpy(m_ytStreamStatus, L"token_err", _countof(m_ytStreamStatus) - 1);
		InterlockedExchange(&m_ytLivePhase, 4);
		return;
	}

	auto jsonStatus = [](const CStringA& json, const char* key, CStringA& out) -> BOOL {
		out.Empty();
		const char* p = strstr((const char*)json, key);
		if (!p) return FALSE;
		const char* q = strchr(p, ':');
		if (!q) return FALSE;
		++q;
		while (*q == ' ' || *q == '\t') ++q;
		if (*q != '\"') return FALSE;
		++q;
		const char* e = strchr(q, '\"');
		if (!e || e <= q) return FALSE;
		out = CStringA(q, (int)(e - q));
		return TRUE;
	};

	// streamStatus
	CString listObj;
	listObj.Format(L"/youtube/v3/liveStreams?part=status&id=%s", savedata.yt_stream_id);
	CStringA listResp;
	if (!ScLiveYtApiJson(L"GET", listObj, CStringA(), listResp, err)) {
		_tcsncpy(m_ytStreamStatus, L"stream_api_err", _countof(m_ytStreamStatus) - 1);
		InterlockedExchange(&m_ytLivePhase, 4);
		return;
	}
	CStringA streamStatus;
	jsonStatus(listResp, "\"streamStatus\"", streamStatus);
	const BOOL active = (streamStatus == "active");

	// lifeCycleStatus
	CString bcObj;
	bcObj.Format(L"/youtube/v3/liveBroadcasts?part=status&id=%s", savedata.yt_broadcast_id);
	CStringA bcResp;
	CStringA life;
	if (ScLiveYtApiJson(L"GET", bcObj, CStringA(), bcResp, err))
		jsonStatus(bcResp, "\"lifeCycleStatus\"", life);

	// 通常時は API 生ステータス（ready 等）を UI に出さない
	m_ytStreamStatus[0] = 0;

	if (life == "live" || life == "liveStarting") {
		m_ytLiveTransitionDone = TRUE;
		InterlockedExchange(&m_ytLivePhase, 3);
		return;
	}

	if (!active) {
		InterlockedExchange(&m_ytLivePhase, 1); // RTMP待ち
		return;
	}

	InterlockedExchange(&m_ytLivePhase, 2); // 開始処理中

	auto tryTransition = [&](const wchar_t* status, CStringA& errBody) -> BOOL {
		errBody.Empty();
		CString obj;
		obj.Format(L"/youtube/v3/liveBroadcasts/transition?broadcastStatus=%s&id=%s&part=status",
			status, savedata.yt_broadcast_id);
		CStringA resp;
		CString localErr;
		if (ScLiveYtApiJson(L"POST", obj, CStringA(), resp, localErr))
			return TRUE;
		errBody = resp;
		if (errBody.GetLength() > 180)
			errBody = errBody.Left(180);
		return FALSE;
	};

	// YouTube: ready/created → testing → live（同一ティックで連打しない）
	if (life == "testing") {
		CStringA errBody;
		if (tryTransition(L"live", errBody)) {
			m_ytLiveTransitionDone = TRUE;
			InterlockedExchange(&m_ytLivePhase, 3);
			return;
		}
		CString w;
		w.Format(L"live_fail/%s", errBody.IsEmpty() ? L"?" : CString(errBody));
		_tcsncpy(m_ytStreamStatus, w, _countof(m_ytStreamStatus) - 1);
		m_ytStreamStatus[_countof(m_ytStreamStatus) - 1] = 0;
		InterlockedExchange(&m_ytLivePhase, 4);
		return;
	}

	if (!m_ytTestingRequested) {
		CStringA errBody;
		if (tryTransition(L"testing", errBody) || life == "testStarting") {
			m_ytTestingRequested = TRUE;
			InterlockedExchange(&m_ytLivePhase, 2);
			return; // 次ポーリングで live へ
		}
		// testing 不可でも live を試す（一部アカウント）
		if (tryTransition(L"live", errBody)) {
			m_ytLiveTransitionDone = TRUE;
			InterlockedExchange(&m_ytLivePhase, 3);
			return;
		}
		CString w;
		w.Format(L"trans_fail/%s", errBody.IsEmpty() ? L"?" : CString(errBody));
		_tcsncpy(m_ytStreamStatus, w, _countof(m_ytStreamStatus) - 1);
		m_ytStreamStatus[_countof(m_ytStreamStatus) - 1] = 0;
		InterlockedExchange(&m_ytLivePhase, 4);
		return;
	}

	{
		CStringA errBody;
		if (tryTransition(L"live", errBody)) {
			m_ytLiveTransitionDone = TRUE;
			InterlockedExchange(&m_ytLivePhase, 3);
			return;
		}
		CString w;
		w.Format(L"live_fail/%s", errBody.IsEmpty() ? L"?" : CString(errBody));
		_tcsncpy(m_ytStreamStatus, w, _countof(m_ytStreamStatus) - 1);
		m_ytStreamStatus[_countof(m_ytStreamStatus) - 1] = 0;
		InterlockedExchange(&m_ytLivePhase, 4);
	}
}

void CScreenCaptureDlg::OnBnClickedLive()
{
	if (m_uiLocked) return;
	PersistLiveFieldsFromUi();
	SyncLiveUiEnable();
	PersistUiToSavedata();
	const BOOL liveOn = m_live.GetSafeHwnd() && m_live.GetCheck();
	if (liveOn) {
		EnsureFfmpegAvailable(TRUE);
		OpenScLiveSettingsModeless(this);
	} else
		CloseScLiveSettingsFromOwner();
}

void CScreenCaptureDlg::OnBnClickedLiveCfg()
{
	if (m_uiLocked) return;
	if (m_live.GetSafeHwnd() && !m_live.GetCheck()) {
		m_live.SetCheck(BST_CHECKED);
		OnBnClickedLive();
		return;
	}
	EnsureFfmpegAvailable(TRUE);
	OpenScLiveSettingsModeless(this);
}

BOOL ScLiveRunOAuth(CWnd* owner, CString& errOut)
{
	const BOOL ok = ScLiveOAuthLoopback(owner, errOut);
	if (ok)
		MpPersistSavedataQuick();
	return ok;
}

BOOL ScLiveIsLoggedIn()
{
	return savedata.yt_refresh_token[0] != 0;
}

BOOL ScLiveRunCreateBroadcast(CWnd* owner, CString& errOut)
{
	SyncScLiveSettingsIfOpen();
	if (savedata.yt_refresh_token[0]) {
		if (!ScLiveEnsureAccessToken(errOut)) {
			if (!ScLiveOAuthLoopback(owner, errOut))
				return FALSE;
		}
	} else {
		if (!ScLiveOAuthLoopback(owner, errOut))
			return FALSE;
	}
	MpPersistSavedataQuick();
	return ScLiveCreateBindBroadcast(errOut, TRUE);
}

BOOL ScLiveHaveOAuthClientCreds()
{
	return ScLiveHaveClientCreds();
}
