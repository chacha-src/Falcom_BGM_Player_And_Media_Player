// ScreenCaptureLive.cpp
// YouTube / Nico / Custom RTMP live UI + OAuth + Live Streaming API

#include "stdafx.h"
#include "ogg.h"
#include "ScreenCaptureDlg.h"

#include <wininet.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>
#include <shellapi.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

extern void MpPersistSavedataQuick();

namespace {

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
		errOut = LL14(
			L"YouTube 認証が必要です。「認証」を実行してください。",
			L"YouTube authentication required. Click Auth.",
			L"Authentification YouTube requise. Cliquez Auth.",
			L"Autenticazione YouTube richiesta. Premi Auth.",
			L"Se requiere autenticación de YouTube. Pulse Auth.",
			L"YouTube 인증이 필요합니다. 인증을 실행하세요.",
			L"需要 YouTube 认证。请点击「认证」。",
			L"مطلوب مصادقة YouTube. انقر Auth.",
			L"Нужна авторизация YouTube. Нажмите Auth.",
			L"YouTube-Anmeldung nötig. Auth klicken.",
			L"Autenticação YouTube necessária. Clique Auth.",
			L"YouTube-authenticatie vereist. Klik Auth.",
			L"Wymagane uwierzytelnienie YouTube. Kliknij Auth.",
			L"YouTube kimlik doğrulaması gerekli. Auth'a tıklayın.");
		return FALSE;
	}
	if (!savedata.yt_client_id[0] || !savedata.yt_client_secret[0]) {
		errOut = LL14(
			L"Client ID / Secret を入力してください。",
			L"Enter Client ID / Secret.",
			L"Saisissez Client ID / Secret.",
			L"Inserisci Client ID / Secret.",
			L"Introduzca Client ID / Secret.",
			L"Client ID / Secret을 입력하세요.",
			L"请输入 Client ID / Secret。",
			L"أدخل Client ID / Secret.",
			L"Введите Client ID / Secret.",
			L"Client-ID / Secret eingeben.",
			L"Introduza Client ID / Secret.",
			L"Voer Client ID / Secret in.",
			L"Wprowadź Client ID / Secret.",
			L"Client ID / Secret girin.");
		return FALSE;
	}

	CStringA body;
	body = "client_id=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(savedata.yt_client_id));
	body += "&client_secret=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(savedata.yt_client_secret));
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

static BOOL ScLiveCreateBindBroadcast(CString& errOut, BOOL updateUi, CScreenCaptureDlg* dlg)
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
		"\"monitorStream\":{\"enableMonitorStream\":false}}}",
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

	const CStringA urlA = rtmps[0] ? CStringA(rtmps) : CStringA(ingest);
	const CString urlW = ScLiveUtf8ToWide(urlA);
	const CString keyW = ScLiveUtf8ToWide(streamName);

	ScLiveCopyField(savedata.yt_broadcast_id, _countof(savedata.yt_broadcast_id), ScLiveUtf8ToWide(broadcastId));
	ScLiveCopyField(savedata.yt_stream_id, _countof(savedata.yt_stream_id), ScLiveUtf8ToWide(streamId));
	ScLiveCopyField(savedata.cap_live_url, _countof(savedata.cap_live_url), urlW);
	ScLiveCopyField(savedata.cap_live_key, _countof(savedata.cap_live_key), keyW);
	MpPersistSavedataQuick();

	if (updateUi && dlg && dlg->GetSafeHwnd()) {
		if (dlg->m_liveUrl.GetSafeHwnd()) dlg->m_liveUrl.SetWindowText(urlW);
		if (dlg->m_liveKey.GetSafeHwnd()) dlg->m_liveKey.SetWindowText(keyW);
	}
	return TRUE;
}

static BOOL ScLiveOAuthLoopback(CWnd* owner, CString& errOut)
{
	errOut.Empty();
	if (!savedata.yt_client_id[0] || !savedata.yt_client_secret[0]) {
		errOut = LL14(
			L"Client ID / Secret を入力してください（Google Cloud の OAuth クライアント）。",
			L"Enter Client ID / Secret (Google Cloud OAuth client).",
			L"Saisissez Client ID / Secret (client OAuth Google Cloud).",
			L"Inserisci Client ID / Secret (client OAuth Google Cloud).",
			L"Introduzca Client ID / Secret (cliente OAuth de Google Cloud).",
			L"Client ID / Secret을 입력하세요(Google Cloud OAuth 클라이언트).",
			L"请输入 Client ID / Secret（Google Cloud OAuth 客户端）。",
			L"أدخل Client ID / Secret (عميل OAuth في Google Cloud).",
			L"Введите Client ID / Secret (OAuth-клиент Google Cloud).",
			L"Client-ID / Secret eingeben (Google-Cloud-OAuth-Client).",
			L"Introduza Client ID / Secret (cliente OAuth Google Cloud).",
			L"Voer Client ID / Secret in (Google Cloud OAuth-client).",
			L"Wprowadź Client ID / Secret (klient OAuth Google Cloud).",
			L"Client ID / Secret girin (Google Cloud OAuth istemcisi).");
		return FALSE;
	}

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
	redirectA.Format("http://127.0.0.1:%d/", port);
	CStringA scopeA = "https://www.googleapis.com/auth/youtube.force-ssl";
	CStringA authUrl;
	authUrl.Format(
		"https://accounts.google.com/o/oauth2/v2/auth?client_id=%s&redirect_uri=%s"
		"&response_type=code&scope=%s&access_type=offline&prompt=consent",
		(LPCSTR)ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(savedata.yt_client_id)),
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
	body += code;
	body += "&client_id=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(savedata.yt_client_id));
	body += "&client_secret=";
	body += ScLiveUrlEncodeUtf8(ScLiveWideToUtf8(savedata.yt_client_secret));
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

void CScreenCaptureDlg::PersistLiveFieldsFromUi()
{
	if (!GetSafeHwnd()) return;
	savedata.cap_live_mode = (m_live.GetSafeHwnd() && m_live.GetCheck()) ? 1 : 0;
	int svc = m_liveSvc.GetSafeHwnd() ? m_liveSvc.GetCurSel() : savedata.cap_live_service;
	if (svc < 0 || svc > 2) svc = 0;
	savedata.cap_live_service = svc;
	int priv = m_livePriv.GetSafeHwnd() ? m_livePriv.GetCurSel() : savedata.cap_live_privacy;
	if (priv < 0 || priv > 2) priv = 0;
	savedata.cap_live_privacy = priv;

	CString s;
	if (m_liveTitle.GetSafeHwnd()) { m_liveTitle.GetWindowText(s); ScLiveCopyField(savedata.cap_live_title, _countof(savedata.cap_live_title), s); }
	if (m_liveDesc.GetSafeHwnd()) { m_liveDesc.GetWindowText(s); ScLiveCopyField(savedata.cap_live_desc, _countof(savedata.cap_live_desc), s); }
	if (m_liveUrl.GetSafeHwnd()) { m_liveUrl.GetWindowText(s); ScLiveCopyField(savedata.cap_live_url, _countof(savedata.cap_live_url), s); }
	if (m_liveKey.GetSafeHwnd()) { m_liveKey.GetWindowText(s); ScLiveCopyField(savedata.cap_live_key, _countof(savedata.cap_live_key), s); }
	if (m_liveCid.GetSafeHwnd()) { m_liveCid.GetWindowText(s); ScLiveCopyField(savedata.yt_client_id, _countof(savedata.yt_client_id), s); }
	if (m_liveCsec.GetSafeHwnd()) { m_liveCsec.GetWindowText(s); ScLiveCopyField(savedata.yt_client_secret, _countof(savedata.yt_client_secret), s); }
}

void CScreenCaptureDlg::ApplyLiveFieldsToUi()
{
	if (!GetSafeHwnd()) return;
	if (m_live.GetSafeHwnd())
		m_live.SetCheck(savedata.cap_live_mode ? BST_CHECKED : BST_UNCHECKED);
	if (m_liveSvc.GetSafeHwnd()) {
		int svc = savedata.cap_live_service;
		if (svc < 0 || svc > 2) svc = 0;
		if (m_liveSvc.GetCount() > 0)
			m_liveSvc.SetCurSel(svc);
	}
	if (m_livePriv.GetSafeHwnd()) {
		int priv = savedata.cap_live_privacy;
		if (priv < 0 || priv > 2) priv = 0;
		if (m_livePriv.GetCount() > 0)
			m_livePriv.SetCurSel(priv);
	}
	if (m_liveTitle.GetSafeHwnd()) m_liveTitle.SetWindowText(savedata.cap_live_title);
	if (m_liveDesc.GetSafeHwnd()) m_liveDesc.SetWindowText(savedata.cap_live_desc);
	if (m_liveUrl.GetSafeHwnd()) m_liveUrl.SetWindowText(savedata.cap_live_url);
	if (m_liveKey.GetSafeHwnd()) m_liveKey.SetWindowText(savedata.cap_live_key);
	if (m_liveCid.GetSafeHwnd()) m_liveCid.SetWindowText(savedata.yt_client_id);
	if (m_liveCsec.GetSafeHwnd()) m_liveCsec.SetWindowText(savedata.yt_client_secret);
	SyncLiveUiEnable();
}

void CScreenCaptureDlg::SyncLiveUiEnable()
{
	if (!GetSafeHwnd()) return;
	const BOOL liveOn = m_live.GetSafeHwnd() && m_live.GetCheck();
	int svc = m_liveSvc.GetSafeHwnd() ? m_liveSvc.GetCurSel() : 0;
	if (svc < 0) svc = 0;
	const BOOL isYt = (svc == 0);
	const BOOL isNicoOrCustom = (svc == 1 || svc == 2);

	if (m_live.GetSafeHwnd()) {
		m_live.SetWindowText(LL14(
			L"ライブ配信", L"Live stream", L"Diffusion live", L"Diretta",
			L"Transmisión en vivo", L"라이브 방송", L"直播", L"بث مباشر",
			L"Прямой эфир", L"Livestream", L"Transmissão ao vivo", L"Livestream",
			L"Transmisja na żywo", L"Canlı yayın"));
	}
	if (m_liveSvcLabel.GetSafeHwnd()) {
		m_liveSvcLabel.SetWindowText(LL14(
			L"配信先", L"Service", L"Service", L"Servizio", L"Servicio", L"서비스", L"服务", L"الخدمة",
			L"Сервис", L"Dienst", L"Serviço", L"Dienst", L"Usługa", L"Servis"));
	}
	if (m_livePrivLabel.GetSafeHwnd()) {
		m_livePrivLabel.SetWindowText(LL14(
			L"公開", L"Privacy", L"Visibilité", L"Privacy", L"Privacidad", L"공개", L"公开", L"الخصوصية",
			L"Доступ", L"Sichtbarkeit", L"Privacidade", L"Privacy", L"Prywatność", L"Gizlilik"));
	}
	if (m_liveTitleLabel.GetSafeHwnd()) {
		m_liveTitleLabel.SetWindowText(LL14(
			L"タイトル", L"Title", L"Titre", L"Titolo", L"Título", L"제목", L"标题", L"العنوان",
			L"Название", L"Titel", L"Título", L"Titel", L"Tytuł", L"Başlık"));
	}
	if (m_liveDescLabel.GetSafeHwnd()) {
		m_liveDescLabel.SetWindowText(LL14(
			L"説明", L"Desc", L"Desc", L"Desc", L"Desc", L"설명", L"说明", L"الوصف",
			L"Опис", L"Beschr.", L"Desc", L"Beschr.", L"Opis", L"Açıkl."));
	}
	if (m_liveUrlLabel.GetSafeHwnd()) {
		m_liveUrlLabel.SetWindowText(LL14(
			L"RTMP URL", L"RTMP URL", L"URL RTMP", L"URL RTMP", L"URL RTMP", L"RTMP URL", L"RTMP URL", L"رابط RTMP",
			L"RTMP URL", L"RTMP-URL", L"URL RTMP", L"RTMP-URL", L"URL RTMP", L"RTMP URL"));
	}
	if (m_liveKeyLabel.GetSafeHwnd()) {
		m_liveKeyLabel.SetWindowText(LL14(
			L"ストリームキー", L"Stream key", L"Clé de flux", L"Chiave stream", L"Clave de stream", L"스트림 키", L"串流密钥", L"مفتاح البث",
			L"Ключ потока", L"Stream-Schlüssel", L"Chave de stream", L"Streamkey", L"Klucz streamu", L"Yayın anahtarı"));
	}
	if (m_liveCidLabel.GetSafeHwnd()) {
		m_liveCidLabel.SetWindowText(L"Client ID");
	}
	if (m_liveCsecLabel.GetSafeHwnd()) {
		m_liveCsecLabel.SetWindowText(L"Secret");
	}
	if (m_liveAuth.GetSafeHwnd()) {
		m_liveAuth.SetWindowText(LL14(
			L"YouTube認証", L"YouTube Auth", L"Auth YouTube", L"Auth YouTube",
			L"Auth YouTube", L"YouTube 인증", L"YouTube 认证", L"مصادقة YouTube",
			L"Вход YouTube", L"YouTube-Auth", L"Auth YouTube", L"YouTube-auth",
			L"Auth YouTube", L"YouTube Auth"));
	}
	if (m_liveCreate.GetSafeHwnd()) {
		m_liveCreate.SetWindowText(LL14(
			L"配信枠作成", L"Create broadcast", L"Créer diffusion", L"Crea diretta",
			L"Crear emisión", L"방송 생성", L"创建直播", L"إنشاء بث",
			L"Создать эфир", L"Broadcast anlegen", L"Criar transmissão", L"Broadcast maken",
			L"Utwórz transmisję", L"Yayın oluştur"));
	}

	const int showYt = (liveOn && isYt) ? SW_SHOW : SW_HIDE;
	const int showKeyEdit = liveOn ? SW_SHOW : SW_SHOW; // 常時表示（タスク: path 同様見える）
	(void)showKeyEdit;

	if (m_liveAuth.GetSafeHwnd()) m_liveAuth.ShowWindow(showYt);
	if (m_liveCreate.GetSafeHwnd()) m_liveCreate.ShowWindow(showYt);
	if (m_livePrivLabel.GetSafeHwnd()) m_livePrivLabel.ShowWindow(showYt);
	if (m_livePriv.GetSafeHwnd()) m_livePriv.ShowWindow(showYt);
	if (m_liveCidLabel.GetSafeHwnd()) m_liveCidLabel.ShowWindow(showYt);
	if (m_liveCid.GetSafeHwnd()) m_liveCid.ShowWindow(showYt);
	if (m_liveCsecLabel.GetSafeHwnd()) m_liveCsecLabel.ShowWindow(showYt);
	if (m_liveCsec.GetSafeHwnd()) m_liveCsec.ShowWindow(showYt);

	// YouTube: URL/Key は API 出力（編集可だが Create/Auth で上書き）。Nico/Custom: 手入力。
	if (m_liveUrl.GetSafeHwnd())
		m_liveUrl.SetReadOnly(FALSE);
	if (m_liveKey.GetSafeHwnd())
		m_liveKey.SetReadOnly(FALSE);

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

	(void)isNicoOrCustom;
	RefreshOpaqueUi();
}

BOOL CScreenCaptureDlg::PrepareYouTubeLiveBeforeStart(CString& errOut)
{
	PersistLiveFieldsFromUi();
	return ScLiveCreateBindBroadcast(errOut, TRUE, this);
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
	// 完了後は id をクリア（次回 Create 用）
	savedata.yt_broadcast_id[0] = 0;
	savedata.yt_stream_id[0] = 0;
	MpPersistSavedataQuick();
}

void CScreenCaptureDlg::TryYouTubeGoLiveTransition()
{
	if (!savedata.yt_broadcast_id[0] || !savedata.yt_stream_id[0])
		return;
	CString err;
	if (!ScLiveEnsureAccessToken(err))
		return;

	CString listObj;
	listObj.Format(L"/youtube/v3/liveStreams?part=status&id=%s", savedata.yt_stream_id);
	CStringA listResp;
	if (!ScLiveYtApiJson(L"GET", listObj, CStringA(), listResp, err))
		return;
	const char* p = strstr((const char*)listResp, "\"streamStatus\"");
	BOOL active = FALSE;
	if (p) {
		const char* q = strchr(p, ':');
		if (q && strstr(q, "active"))
			active = TRUE;
	}
	if (!active)
		return;

	CString obj;
	obj.Format(L"/youtube/v3/liveBroadcasts/transition?broadcastStatus=live&id=%s&part=status",
		savedata.yt_broadcast_id);
	CStringA resp;
	ScLiveYtApiJson(L"POST", obj, CStringA(), resp, err);
}

void CScreenCaptureDlg::OnBnClickedLive()
{
	if (m_uiLocked) return;
	PersistLiveFieldsFromUi();
	SyncLiveUiEnable();
	PersistUiToSavedata();
}

void CScreenCaptureDlg::OnCbnSelchangeLiveSvc()
{
	if (m_uiLocked) return;
	PersistLiveFieldsFromUi();
	SyncLiveUiEnable();
	PersistUiToSavedata();
}

void CScreenCaptureDlg::OnCbnSelchangeLivePriv()
{
	if (m_uiLocked) return;
	PersistLiveFieldsFromUi();
	PersistUiToSavedata();
}

void CScreenCaptureDlg::OnBnClickedLiveAuth()
{
	if (m_uiLocked) return;
	if (!m_live.GetCheck()) return;
	if (m_liveSvc.GetCurSel() != 0) {
		ScLiveMsgError(this, LL14(
			L"認証は YouTube 選択時のみです。",
			L"Auth is only for YouTube.",
			L"Auth uniquement pour YouTube.",
			L"Auth solo per YouTube.",
			L"Auth solo para YouTube.",
			L"인증은 YouTube 선택 시에만.",
			L"仅在选择 YouTube 时可认证。",
			L"المصادقة لـ YouTube فقط.",
			L"Авторизация только для YouTube.",
			L"Auth nur bei YouTube.",
			L"Auth só para YouTube.",
			L"Auth alleen voor YouTube.",
			L"Auth tylko dla YouTube.",
			L"Auth yalnızca YouTube için."));
		return;
	}
	PersistLiveFieldsFromUi();
	CString err;
	if (!ScLiveOAuthLoopback(this, err)) {
		ScLiveMsgError(this, err);
		return;
	}
	::MessageBox(m_hWnd, LL14(
		L"YouTube 認証に成功しました。",
		L"YouTube authentication succeeded.",
		L"Authentification YouTube réussie.",
		L"Autenticazione YouTube riuscita.",
		L"Autenticación de YouTube correcta.",
		L"YouTube 인증에 성공했습니다.",
		L"YouTube 认证成功。",
		L"نجحت مصادقة YouTube.",
		L"Авторизация YouTube выполнена.",
		L"YouTube-Anmeldung erfolgreich.",
		L"Autenticação YouTube concluída.",
		L"YouTube-authenticatie geslaagd.",
		L"Uwierzytelnienie YouTube powiodło się.",
		L"YouTube kimlik doğrulaması başarılı."),
		LL14(L"画面キャプチャ", L"Screen capture", L"Capture d'écran", L"Cattura schermo",
			L"Captura de pantalla", L"화면 캡처", L"屏幕捕获", L"التقاط الشاشة",
			L"Захват экрана", L"Bildschirmaufnahme", L"Captura de ecrã", L"Schermopname",
			L"Przechwytywanie ekranu", L"Ekran yakalama"),
		MB_OK | MB_ICONINFORMATION);
}

void CScreenCaptureDlg::OnBnClickedLiveCreate()
{
	if (m_uiLocked) return;
	if (!m_live.GetCheck()) return;
	if (m_liveSvc.GetCurSel() != 0) {
		ScLiveMsgError(this, LL14(
			L"配信枠作成は YouTube のみです。Nico/カスタムは URL とキーを手入力してください。",
			L"Create broadcast is YouTube-only. For Nico/Custom, enter URL and key.",
			L"Création réservée à YouTube. Nico/Perso: saisissez URL et clé.",
			L"Crea diretta solo YouTube. Nico/Custom: inserisci URL e chiave.",
			L"Crear emisión solo YouTube. Nico/Personalizado: introduzca URL y clave.",
			L"방송 생성은 YouTube 전용. Nico/사용자 지정은 URL·키를 입력하세요.",
			L"创建直播仅限 YouTube。Nico/自定义请手动填写 URL 和密钥。",
			L"إنشاء البث لـ YouTube فقط. لـ Nico/مخصص أدخل الرابط والمفتاح.",
			L"Создание эфира только для YouTube. Для Nico/своего — введите URL и ключ.",
			L"Broadcast anlegen nur für YouTube. Nico/Custom: URL und Key eingeben.",
			L"Criar transmissão só YouTube. Nico/Personalizado: introduza URL e chave.",
			L"Broadcast maken alleen YouTube. Nico/Custom: vul URL en key in.",
			L"Tworzenie transmisji tylko YouTube. Nico/Własne: wpisz URL i klucz.",
			L"Yayın oluşturma yalnızca YouTube. Nico/Özel için URL ve anahtar girin."));
		return;
	}
	PersistLiveFieldsFromUi();
	CString err;
	if (!PrepareYouTubeLiveBeforeStart(err)) {
		ScLiveMsgError(this, err);
		return;
	}
	m_status.SetWindowText(LL14(
		L"YouTube 配信枠を作成し、RTMP URL/キーを設定しました。",
		L"YouTube broadcast created; RTMP URL/key set.",
		L"Diffusion YouTube créée; URL/clé RTMP définis.",
		L"Diretta YouTube creata; URL/chiave RTMP impostati.",
		L"Emisión de YouTube creada; URL/clave RTMP listos.",
		L"YouTube 방송을 만들고 RTMP URL/키를 설정했습니다.",
		L"已创建 YouTube 直播并填入 RTMP URL/密钥。",
		L"تم إنشاء بث YouTube وتعيين رابط/مفتاح RTMP.",
		L"Эфир YouTube создан; RTMP URL/ключ заданы.",
		L"YouTube-Broadcast angelegt; RTMP-URL/Key gesetzt.",
		L"Transmissão YouTube criada; URL/chave RTMP definidos.",
		L"YouTube-broadcast gemaakt; RTMP-URL/key gezet.",
		L"Utworzono transmisję YouTube; ustawiono URL/klucz RTMP.",
		L"YouTube yayını oluşturuldu; RTMP URL/anahtar ayarlandı."));
}
