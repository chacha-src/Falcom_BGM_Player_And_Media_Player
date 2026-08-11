#include "stdafx.h"
#include "ogg.h"
#include "ScLiveSettingsDlg.h"
#include "ScreenCaptureDlg.h"

extern void MpPersistSavedataQuick();
extern CScreenCaptureDlg* g_screenCaptureDlg;

static CScLiveSettingsDlg* g_scLiveSettings = NULL;

IMPLEMENT_DYNAMIC(CScLiveSettingsDlg, CCustomBlurDialogBase)

BEGIN_MESSAGE_MAP(CScLiveSettingsDlg, CCustomBlurDialogBase)
	ON_WM_DESTROY()
	ON_CBN_SELCHANGE(IDC_SC_LIVE_SVC, &CScLiveSettingsDlg::OnCbnSelchangeSvc)
	ON_CBN_SELCHANGE(IDC_SC_LIVE_PRIV, &CScLiveSettingsDlg::OnCbnSelchangePriv)
	ON_BN_CLICKED(IDC_SC_LIVE_AUTH, &CScLiveSettingsDlg::OnBnClickedAuth)
	ON_BN_CLICKED(IDC_SC_LIVE_CREATE, &CScLiveSettingsDlg::OnBnClickedCreate)
	ON_BN_CLICKED(IDC_SC_LIVE_ADV, &CScLiveSettingsDlg::OnBnClickedAdv)
	ON_EN_CHANGE(IDC_SC_LIVE_TITLE, &CScLiveSettingsDlg::OnEnChangeField)
	ON_EN_CHANGE(IDC_SC_LIVE_DESC, &CScLiveSettingsDlg::OnEnChangeField)
	ON_EN_CHANGE(IDC_SC_LIVE_URL, &CScLiveSettingsDlg::OnEnChangeField)
	ON_EN_CHANGE(IDC_SC_LIVE_KEY, &CScLiveSettingsDlg::OnEnChangeField)
	ON_EN_CHANGE(IDC_SC_LIVE_CID, &CScLiveSettingsDlg::OnEnChangeField)
	ON_EN_CHANGE(IDC_SC_LIVE_CSEC, &CScLiveSettingsDlg::OnEnChangeField)
END_MESSAGE_MAP()

CScLiveSettingsDlg::CScLiveSettingsDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CScLiveSettingsDlg::IDD, pParent)
	, m_closingFromOwner(FALSE)
{
}

CScLiveSettingsDlg::~CScLiveSettingsDlg()
{
}

void CScLiveSettingsDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SC_LIVE_SVC_L, m_svcLabel);
	DDX_Control(pDX, IDC_SC_LIVE_SVC, m_svc);
	DDX_Control(pDX, IDC_SC_LIVE_PRIV_L, m_privLabel);
	DDX_Control(pDX, IDC_SC_LIVE_PRIV, m_priv);
	DDX_Control(pDX, IDC_SC_LIVE_AUTH, m_auth);
	DDX_Control(pDX, IDC_SC_LIVE_CREATE, m_create);
	DDX_Control(pDX, IDC_SC_LIVE_TITLE_L, m_titleLabel);
	DDX_Control(pDX, IDC_SC_LIVE_TITLE, m_title);
	DDX_Control(pDX, IDC_SC_LIVE_DESC_L, m_descLabel);
	DDX_Control(pDX, IDC_SC_LIVE_DESC, m_desc);
	DDX_Control(pDX, IDC_SC_LIVE_URL_L, m_urlLabel);
	DDX_Control(pDX, IDC_SC_LIVE_URL, m_url);
	DDX_Control(pDX, IDC_SC_LIVE_KEY_L, m_keyLabel);
	DDX_Control(pDX, IDC_SC_LIVE_KEY, m_key);
	DDX_Control(pDX, IDC_SC_LIVE_ADV, m_adv);
	DDX_Control(pDX, IDC_SC_LIVE_CID_L, m_cidLabel);
	DDX_Control(pDX, IDC_SC_LIVE_CID, m_cid);
	DDX_Control(pDX, IDC_SC_LIVE_CSEC_L, m_csecLabel);
	DDX_Control(pDX, IDC_SC_LIVE_CSEC, m_csec);
	DDX_Control(pDX, IDC_SC_LIVE_HINT, m_hint);
	DDX_Control(pDX, IDCANCEL, m_close);
}

BOOL CScLiveSettingsDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(
		L"ライブ配信設定", L"Live stream settings", L"Reglages diffusion live", L"Impostazioni diretta",
		L"Ajustes de transmision", L"라이브 방송 설정", L"直播设置", L"إعدادات البث المباشر",
		L"Настройки эфира", L"Livestream-Einstellungen", L"Definicoes de transmissao", L"Livestream-instellingen",
		L"Ustawienia transmisji", L"Canli yayin ayarlari"));

	m_svc.SetAeroMode(FALSE);
	m_priv.SetAeroMode(FALSE);
	m_auth.SetAeroMode(FALSE);
	m_create.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);

	m_svc.ResetContent();
	m_svc.AddString(L"YouTube");
	m_svc.AddString(LL14(L"ニコニコ", L"Niconico", L"Niconico", L"Niconico", L"Niconico", L"니코니코", L"Niconico", L"نيكونيكو",
		L"Niconico", L"Niconico", L"Niconico", L"Niconico", L"Niconico", L"Niconico"));
	m_svc.AddString(LL14(L"カスタム RTMP", L"Custom RTMP", L"RTMP perso", L"RTMP personalizzato", L"RTMP personalizado", L"사용자 RTMP", L"自定义 RTMP", L"RTMP مخصص",
		L"Свой RTMP", L"Eigenes RTMP", L"RTMP personalizado", L"Aangepaste RTMP", L"Wlasny RTMP", L"Ozel RTMP"));

	m_priv.ResetContent();
	m_priv.AddString(LL14(L"公開", L"Public", L"Public", L"Pubblico", L"Publico", L"공개", L"公开", L"عام",
		L"Открытый", L"Offentlich", L"Publico", L"Openbaar", L"Publiczny", L"Herkese acik"));
	m_priv.AddString(LL14(L"限定公開", L"Unlisted", L"Non repertorie", L"Non in elenco", L"No listado", L"한정 공개", L"不列出", L"غير مدرج",
		L"По ссылке", L"Unlisted", L"Nao listado", L"Unlisted", L"Unlisted", L"Listelenmemis"));
	m_priv.AddString(LL14(L"非公開", L"Private", L"Prive", L"Privato", L"Privado", L"비공개", L"私密", L"خاص",
		L"Частный", L"Privat", L"Privado", L"Prive", L"Prywatny", L"Ozel"));

	m_svcLabel.SetWindowText(LL14(
		L"配信先", L"Service", L"Service", L"Servizio", L"Servicio", L"서비스", L"服务", L"الخدمة",
		L"Сервис", L"Dienst", L"Servico", L"Dienst", L"Usluga", L"Servis"));
	m_privLabel.SetWindowText(LL14(
		L"公開", L"Privacy", L"Visibilite", L"Privacy", L"Privacidad", L"공개", L"公开", L"الخصوصية",
		L"Доступ", L"Sichtbarkeit", L"Privacidade", L"Privacy", L"Prywatnosc", L"Gizlilik"));
	m_titleLabel.SetWindowText(LL14(
		L"タイトル", L"Title", L"Titre", L"Titolo", L"Titulo", L"제목", L"标题", L"العنوان",
		L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik"));
	m_descLabel.SetWindowText(LL14(
		L"説明", L"Description", L"Description", L"Descrizione", L"Descripcion", L"설명", L"说明", L"الوصف",
		L"Описание", L"Beschreibung", L"Descricao", L"Beschrijving", L"Opis", L"Aciklama"));
	m_urlLabel.SetWindowText(L"RTMP URL");
	m_keyLabel.SetWindowText(LL14(
		L"ストリームキー", L"Stream key", L"Cle de flux", L"Chiave stream", L"Clave de stream", L"스트림 키", L"串流密钥", L"مفتاح البث",
		L"Ключ потока", L"Stream-Schlussel", L"Chave de stream", L"Streamkey", L"Klucz streamu", L"Yayin anahtari"));
	m_cidLabel.SetWindowText(L"Client ID");
	m_csecLabel.SetWindowText(L"Secret");
	if (m_adv.GetSafeHwnd()) {
		m_adv.SetWindowText(LL14(
			L"詳細設定（Client ID/Secret）", L"Advanced (Client ID/Secret)", L"Avance (Client ID/Secret)",
			L"Avanzate (Client ID/Secret)", L"Avanzado (Client ID/Secret)", L"고급(Client ID/Secret)",
			L"高级（Client ID/Secret）", L"متقدم (Client ID/Secret)", L"Дополнительно (Client ID/Secret)",
			L"Erweitert (Client-ID/Secret)", L"Avancado (Client ID/Secret)", L"Geavanceerd (Client ID/Secret)",
			L"Zaawansowane (Client ID/Secret)", L"Gelismis (Client ID/Secret)"));
		m_adv.SetCheck(BST_UNCHECKED);
	}
	m_auth.SetWindowText(LL14(
		L"Googleでログイン", L"Sign in with Google", L"Se connecter avec Google", L"Accedi con Google",
		L"Iniciar sesion con Google", L"Google로 로그인", L"使用 Google 登录", L"تسجيل الدخول عبر Google",
		L"Войти через Google", L"Mit Google anmelden", L"Entrar com o Google", L"Inloggen met Google",
		L"Zaloguj przez Google", L"Google ile oturum ac"));
	m_create.SetWindowText(LL14(
		L"配信枠作成", L"Create broadcast", L"Creer diffusion", L"Crea diretta",
		L"Crear emision", L"방송 생성", L"创建直播", L"إنشاء بث",
		L"Создать эфир", L"Broadcast anlegen", L"Criar transmissao", L"Broadcast maken",
		L"Utworz transmisje", L"Yayin olustur"));
	m_close.SetWindowText(LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(
		L"YouTube: Googleでログイン → 配信枠作成 → 本体で配信開始。Nico/カスタム: URL とキーを手入力。",
		L"YouTube: Sign in with Google → Create → Go live. Nico/Custom: enter URL and key.",
		L"YouTube: Google → Creer → Diffuser. Nico/Perso: saisissez URL et cle.",
		L"YouTube: Google → Crea → Diretta. Nico/Custom: inserisci URL e chiave.",
		L"YouTube: Google → Crear → Emitir. Nico/Pers.: introduzca URL y clave.",
		L"YouTube: Google 로그인 → 방송 생성 → 시작. Nico/사용자: URL·키 입력.",
		L"YouTube：Google 登录 → 创建 → 开播。Nico/自定义：手动填写 URL 和密钥。",
		L"YouTube: تسجيل Google → إنشاء → بث. Nico/مخصص: أدخل الرابط والمفتاح.",
		L"YouTube: Google → Создать → Эфир. Nico/свой: введите URL и ключ.",
		L"YouTube: Google → Anlegen → Live. Nico/Custom: URL und Key eingeben.",
		L"YouTube: Google → Criar → Ao vivo. Nico/Pers.: introduza URL e chave.",
		L"YouTube: Google → Maken → Live. Nico/Custom: vul URL en key in.",
		L"YouTube: Google → Utworz → Live. Nico/Wlasne: wpisz URL i klucz.",
		L"YouTube: Google → Olustur → Canli. Nico/Ozel: URL ve anahtar girin."));

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	m_tooltip.AddTool(&m_svc, LL14(
		L"YouTube / ニコニコ / カスタムRTMP", L"YouTube / Niconico / Custom RTMP",
		L"YouTube / Niconico / RTMP perso", L"YouTube / Niconico / RTMP personalizzato",
		L"YouTube / Niconico / RTMP personalizado", L"YouTube / 니코니코 / 사용자 RTMP",
		L"YouTube / Niconico / 自定义 RTMP", L"YouTube / Niconico / RTMP مخصص",
		L"YouTube / Niconico / свой RTMP", L"YouTube / Niconico / Eigenes RTMP",
		L"YouTube / Niconico / RTMP personalizado", L"YouTube / Niconico / Aangepaste RTMP",
		L"YouTube / Niconico / Wlasny RTMP", L"YouTube / Niconico / Ozel RTMP"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 10000);

	ApplyFieldsFromSavedata();
	SyncYtVisibility();

	if (g_screenCaptureDlg && ::IsWindow(g_screenCaptureDlg->GetSafeHwnd())) {
		CRect scr, wr;
		g_screenCaptureDlg->GetWindowRect(&scr);
		GetWindowRect(&wr);
		const int w = wr.Width(), h = wr.Height();
		int x = scr.right + 8;
		int y = scr.top;
		HMONITOR mon = MonitorFromWindow(g_screenCaptureDlg->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(mon, &mi)) {
			if (x + w > mi.rcWork.right)
				x = scr.left - w - 8;
			if (x < mi.rcWork.left) x = mi.rcWork.left;
			if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
			if (y < mi.rcWork.top) y = mi.rcWork.top;
		}
		SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	return TRUE;
}

BOOL CScLiveSettingsDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CScLiveSettingsDlg::PersistFieldsToSavedata()
{
	if (!GetSafeHwnd()) return;
	int svc = m_svc.GetSafeHwnd() ? m_svc.GetCurSel() : savedata.cap_live_service;
	if (svc < 0 || svc > 2) svc = 0;
	savedata.cap_live_service = svc;
	int priv = m_priv.GetSafeHwnd() ? m_priv.GetCurSel() : savedata.cap_live_privacy;
	if (priv < 0 || priv > 2) priv = 0;
	savedata.cap_live_privacy = priv;

	auto copyEd = [](CWnd& ed, TCHAR* dst, int cch) {
		if (!ed.GetSafeHwnd() || !dst || cch <= 0) return;
		CString s;
		ed.GetWindowText(s);
		_tcsncpy(dst, s, cch - 1);
		dst[cch - 1] = 0;
	};
	copyEd(m_title, savedata.cap_live_title, _countof(savedata.cap_live_title));
	copyEd(m_desc, savedata.cap_live_desc, _countof(savedata.cap_live_desc));
	copyEd(m_url, savedata.cap_live_url, _countof(savedata.cap_live_url));
	copyEd(m_key, savedata.cap_live_key, _countof(savedata.cap_live_key));
	copyEd(m_cid, savedata.yt_client_id, _countof(savedata.yt_client_id));
	copyEd(m_csec, savedata.yt_client_secret, _countof(savedata.yt_client_secret));
}

void CScLiveSettingsDlg::ApplyFieldsFromSavedata()
{
	if (!GetSafeHwnd()) return;
	int svc = savedata.cap_live_service;
	if (svc < 0 || svc > 2) svc = 0;
	if (m_svc.GetCount() > 0) m_svc.SetCurSel(svc);
	int priv = savedata.cap_live_privacy;
	if (priv < 0 || priv > 2) priv = 0;
	if (m_priv.GetCount() > 0) m_priv.SetCurSel(priv);
	if (m_title.GetSafeHwnd()) m_title.SetWindowText(savedata.cap_live_title);
	if (m_desc.GetSafeHwnd()) m_desc.SetWindowText(savedata.cap_live_desc);
	if (m_url.GetSafeHwnd()) m_url.SetWindowText(savedata.cap_live_url);
	if (m_key.GetSafeHwnd()) m_key.SetWindowText(savedata.cap_live_key);
	if (m_cid.GetSafeHwnd()) m_cid.SetWindowText(savedata.yt_client_id);
	if (m_csec.GetSafeHwnd()) m_csec.SetWindowText(savedata.yt_client_secret);
	RefreshAuthButtonUi();
}

void CScLiveSettingsDlg::RefreshAuthButtonUi()
{
	if (!m_auth.GetSafeHwnd()) return;
	if (ScLiveIsLoggedIn()) {
		m_auth.SetWindowText(LL14(
			L"Googleログイン済み（再認証）", L"Signed in (re-auth)", L"Connecte (re-auth)", L"Accesso OK (riauth)",
			L"Sesion OK (reauth)", L"Google 로그인됨(재인증)", L"已登录 Google（重新认证）", L"تم الدخول (إعادة)",
			L"Вход выполнен (снова)", L"Angemeldet (neu)", L"Logado (reauth)", L"Ingelogd (opnieuw)",
			L"Zalogowano (ponownie)", L"Giris yapildi (yeniden)"));
	} else {
		m_auth.SetWindowText(LL14(
			L"Googleでログイン", L"Sign in with Google", L"Se connecter avec Google", L"Accedi con Google",
			L"Iniciar sesion con Google", L"Google로 로그인", L"使用 Google 登录", L"تسجيل الدخول عبر Google",
			L"Войти через Google", L"Mit Google anmelden", L"Entrar com o Google", L"Inloggen met Google",
			L"Zaloguj przez Google", L"Google ile oturum ac"));
	}
}

void CScLiveSettingsDlg::SyncYtVisibility()
{
	if (!GetSafeHwnd()) return;
	const int svc = m_svc.GetSafeHwnd() ? m_svc.GetCurSel() : 0;
	const BOOL isYt = (svc == 0);
	const int showYt = isYt ? SW_SHOW : SW_HIDE;
	if (m_privLabel.GetSafeHwnd()) m_privLabel.ShowWindow(showYt);
	if (m_priv.GetSafeHwnd()) m_priv.ShowWindow(showYt);
	if (m_auth.GetSafeHwnd()) m_auth.ShowWindow(showYt);
	if (m_create.GetSafeHwnd()) m_create.ShowWindow(showYt);
	if (m_adv.GetSafeHwnd()) m_adv.ShowWindow(showYt);

	const BOOL showCreds = isYt && m_adv.GetSafeHwnd() && m_adv.GetCheck() == BST_CHECKED;
	const int showCid = showCreds ? SW_SHOW : SW_HIDE;
	if (m_cidLabel.GetSafeHwnd()) m_cidLabel.ShowWindow(showCid);
	if (m_cid.GetSafeHwnd()) m_cid.ShowWindow(showCid);
	if (m_csecLabel.GetSafeHwnd()) m_csecLabel.ShowWindow(showCid);
	if (m_csec.GetSafeHwnd()) m_csec.ShowWindow(showCid);
	RefreshAuthButtonUi();
}

void CScLiveSettingsDlg::RevealAdvancedCreds()
{
	if (!GetSafeHwnd()) return;
	if (m_adv.GetSafeHwnd())
		m_adv.SetCheck(BST_CHECKED);
	SyncYtVisibility();
	if (m_cid.GetSafeHwnd()) {
		m_cid.SetFocus();
		m_cid.SetSel(0, -1);
	}
}

void CScLiveSettingsDlg::SetUrlKey(const CString& url, const CString& key)
{
	if (m_url.GetSafeHwnd()) m_url.SetWindowText(url);
	if (m_key.GetSafeHwnd()) m_key.SetWindowText(key);
}

void CScLiveSettingsDlg::OnCbnSelchangeSvc()
{
	PersistFieldsToSavedata();
	SyncYtVisibility();
	MpPersistSavedataQuick();
}

void CScLiveSettingsDlg::OnCbnSelchangePriv()
{
	PersistFieldsToSavedata();
	MpPersistSavedataQuick();
}

void CScLiveSettingsDlg::OnBnClickedAdv()
{
	SyncYtVisibility();
}

void CScLiveSettingsDlg::OnEnChangeField()
{
	PersistFieldsToSavedata();
}

void CScLiveSettingsDlg::OnBnClickedAuth()
{
	PersistFieldsToSavedata();
	if (m_svc.GetCurSel() != 0) return;
	CString err;
	const CString cap = LL14(L"画面キャプチャ", L"Screen capture", L"Capture d'ecran", L"Cattura schermo",
		L"Captura de pantalla", L"화면 캡처", L"屏幕捕获", L"التقاط الشاشة",
		L"Захват экрана", L"Bildschirmaufnahme", L"Captura de ecran", L"Schermopname",
		L"Przechwytywanie ekranu", L"Ekran yakalama");
	if (!ScLiveHaveOAuthClientCreds()) {
		ScLiveSettingsRevealAdvancedCreds();
		::MessageBox(m_hWnd, LL14(
			L"Client ID / Secret が未設定です。「詳細設定」に入力してから、もう一度「Googleでログイン」を押してください。",
			L"Client ID / Secret is not set. Enter them under Advanced, then click Sign in with Google again.",
			L"Client ID / Secret non definis. Saisissez-les dans Avance, puis reconnectez-vous avec Google.",
			L"Client ID / Secret non impostati. Inseriscili in Avanzate, poi Accedi con Google di nuovo.",
			L"Client ID / Secret no estan definidos. Introduzcalos en Avanzado y vuelva a iniciar sesion con Google.",
			L"Client ID / Secret이 없습니다. 고급에 입력한 뒤 Google로 로그인을 다시 누르세요.",
			L"未设置 Client ID / Secret。请在「高级」中填写后，再次点击「使用 Google 登录」。",
			L"لم يُعيَّن Client ID / Secret. أدخلهما ضمن متقدم ثم سجّل الدخول عبر Google مجددًا.",
			L"Client ID / Secret не заданы. Введите их в «Дополнительно» и снова нажмите «Войти через Google».",
			L"Client-ID / Secret fehlen. Unter Erweitert eingeben und erneut Mit Google anmelden.",
			L"Client ID / Secret nao definidos. Introduza-os em Avancado e entre com o Google novamente.",
			L"Client ID / Secret ontbreken. Vul ze in onder Geavanceerd en log opnieuw in met Google.",
			L"Brak Client ID / Secret. Wprowadz je w Zaawansowane i ponownie kliknij Zaloguj przez Google.",
			L"Client ID / Secret ayarlanmadi. Gelismis altina girip Google ile oturum ac'a tekrar basin."),
			cap, MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (!ScLiveRunOAuth(this, err)) {
		::MessageBox(m_hWnd, err, cap, MB_OK | MB_ICONWARNING);
		return;
	}
	RefreshAuthButtonUi();
	::MessageBox(m_hWnd, LL14(
		L"Google ログインに成功しました。次回起動・画面キャプチャ再オープンでもログインは保持されます。",
		L"Signed in with Google. Login is kept across restarts and reopening Screen Capture.",
		L"Connexion Google reussie. Conservee au redemarrage.",
		L"Accesso Google riuscito. Conservato al riavvio.",
		L"Inicio de sesion con Google correcto. Se conserva al reiniciar.",
		L"Google 로그인에 성공했습니다. 재시작·다시 열어도 유지됩니다.",
		L"已使用 Google 登录。重启或重新打开屏幕捕获后仍会保留。",
		L"تم تسجيل الدخول عبر Google. يُحفظ بعد إعادة التشغيل.",
		L"Вход через Google выполнен. Сохраняется после перезапуска.",
		L"Mit Google angemeldet. Bleibt nach Neustart erhalten.",
		L"Login com Google concluido. Mantido apos reiniciar.",
		L"Inloggen met Google geslaagd. Blijft bewaard na herstart.",
		L"Zalogowano przez Google. Zachowane po restarcie.",
		L"Google ile oturum acildi. Yeniden baslatmada korunur."),
		cap, MB_OK | MB_ICONINFORMATION);
}

void CScLiveSettingsDlg::OnBnClickedCreate()
{
	PersistFieldsToSavedata();
	const CString cap = LL14(L"画面キャプチャ", L"Screen capture", L"Capture d'ecran", L"Cattura schermo",
		L"Captura de pantalla", L"화면 캡처", L"屏幕捕获", L"التقاط الشاشة",
		L"Захват экрана", L"Bildschirmaufnahme", L"Captura de ecran", L"Schermopname",
		L"Przechwytywanie ekranu", L"Ekran yakalama");
	if (m_svc.GetCurSel() != 0) {
		::MessageBox(m_hWnd, LL14(
			L"配信枠作成は YouTube のみです。Nico/カスタムは URL とキーを手入力してください。",
			L"Create broadcast is YouTube-only. For Nico/Custom, enter URL and key.",
			L"Creation reservee a YouTube. Nico/Perso: saisissez URL et cle.",
			L"Crea diretta solo YouTube. Nico/Custom: inserisci URL e chiave.",
			L"Crear emision solo YouTube. Nico/Personalizado: introduzca URL y clave.",
			L"방송 생성은 YouTube 전용. Nico/사용자 지정은 URL·키를 입력하세요.",
			L"创建直播仅限 YouTube。Nico/自定义请手动填写 URL 和密钥。",
			L"إنشاء البث لـ YouTube فقط. لـ Nico/مخصص أدخل الرابط والمفتاح.",
			L"Создание эфира только для YouTube. Для Nico/своего — введите URL и ключ.",
			L"Broadcast anlegen nur fur YouTube. Nico/Custom: URL und Key eingeben.",
			L"Criar transmissao so YouTube. Nico/Personalizado: introduza URL e chave.",
			L"Broadcast maken alleen YouTube. Nico/Custom: vul URL en key in.",
			L"Tworzenie transmisji tylko YouTube. Nico/Wlasne: wpisz URL i klucz.",
			L"Yayin olusturma yalnizca YouTube. Nico/Ozel icin URL ve anahtar girin."),
			cap, MB_OK | MB_ICONINFORMATION);
		return;
	}
	CString err;
	if (!ScLiveHaveOAuthClientCreds()) {
		ScLiveSettingsRevealAdvancedCreds();
	}
	if (!ScLiveRunCreateBroadcast(this, err)) {
		if (!ScLiveHaveOAuthClientCreds())
			ScLiveSettingsRevealAdvancedCreds();
		::MessageBox(m_hWnd, err.IsEmpty()
			? LL14(L"YouTube 配信の準備に失敗しました。", L"YouTube live prepare failed.",
				L"Preparation YouTube echouee.", L"Preparazione YouTube non riuscita.",
				L"Fallo la preparacion de YouTube.", L"YouTube 방송 준비 실패.",
				L"YouTube 直播准备失败。", L"فشل تجهيز بث YouTube.",
				L"Не удалось подготовить эфир YouTube.", L"YouTube-Live-Vorbereitung fehlgeschlagen.",
				L"Falha ao preparar YouTube ao vivo.", L"Voorbereiding YouTube-live mislukt.",
				L"Przygotowanie YouTube nie powiodlo sie.", L"YouTube canli hazirligi basarisiz.")
			: err, cap, MB_OK | MB_ICONWARNING);
		return;
	}
	ApplyFieldsFromSavedata();
	if (m_hint.GetSafeHwnd()) {
		m_hint.SetWindowText(LL14(
			L"配信枠を作成し、RTMP URL/キーを設定しました。本体で「配信開始」を押してください。",
			L"Broadcast created; RTMP URL/key set. Press Go live on the capture window.",
			L"Diffusion creee; URL/cle prets. Appuyez sur Diffuser dans la capture.",
			L"Diretta creata; URL/chiave pronti. Premi Vai in diretta nella cattura.",
			L"Emision creada; URL/clave listos. Pulse Emitir en la captura.",
			L"방송을 만들고 RTMP URL/키를 설정했습니다. 캡처 창에서 방송 시작을 누르세요.",
			L"已创建直播并填入 RTMP。请在捕获窗口按开始直播。",
			L"تم إنشاء البث وتعيين RTMP. اضغط بدء البث في نافذة الالتقاط.",
			L"Эфир создан; RTMP задан. Нажмите «В эфир» в окне захвата.",
			L"Broadcast angelegt; RTMP gesetzt. Live starten im Aufnahmefenster.",
			L"Transmissao criada; RTMP pronto. Clique em Ao vivo na captura.",
			L"Broadcast gemaakt; RTMP gezet. Druk Live starten in het opnamevenster.",
			L"Utworzono transmisje; ustawiono RTMP. W oknie przechwytywania nacisnij Start.",
			L"Yayin olusturuldu; RTMP ayarlandi. Yakalama penceresinde Yayina basla."));
	}
}

void CScLiveSettingsDlg::OnOK()
{
	OnCancel();
}

void CScLiveSettingsDlg::OnCancel()
{
	PersistFieldsToSavedata();
	MpPersistSavedataQuick();
	if (GetSafeHwnd())
		DestroyWindow();
}

void CScLiveSettingsDlg::OnDestroy()
{
	PersistFieldsToSavedata();
	MpPersistSavedataQuick();
	const BOOL fromOwner = m_closingFromOwner;
	CCustomBlurDialogBase::OnDestroy();
	if (!fromOwner) {
		savedata.cap_live_mode = 0;
		MpPersistSavedataQuick();
		if (g_screenCaptureDlg && ::IsWindow(g_screenCaptureDlg->GetSafeHwnd())) {
			if (g_screenCaptureDlg->m_live.GetSafeHwnd())
				g_screenCaptureDlg->m_live.SetCheck(BST_UNCHECKED);
			g_screenCaptureDlg->SyncLiveUiEnable();
		}
	}
}

void CScLiveSettingsDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_scLiveSettings == this)
		g_scLiveSettings = NULL;
	delete this;
}

void OpenScLiveSettingsModeless(CWnd* parent)
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd())) {
		g_scLiveSettings->ShowWindow(SW_SHOW);
		g_scLiveSettings->SetForegroundWindow();
		g_scLiveSettings->ApplyFieldsFromSavedata();
		g_scLiveSettings->SyncYtVisibility();
		return;
	}
	CWnd* p = parent;
	if (!p || !::IsWindow(p->GetSafeHwnd()))
		p = NULL;
	g_scLiveSettings = new CScLiveSettingsDlg(p);
	if (!g_scLiveSettings->Create(CScLiveSettingsDlg::IDD, p)) {
		delete g_scLiveSettings;
		g_scLiveSettings = NULL;
		return;
	}
	g_scLiveSettings->ShowWindow(SW_SHOW);
	g_scLiveSettings->SetForegroundWindow();
}

void CloseScLiveSettingsFromOwner()
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd())) {
		g_scLiveSettings->m_closingFromOwner = TRUE;
		g_scLiveSettings->PersistFieldsToSavedata();
		g_scLiveSettings->DestroyWindow();
	}
}

void CloseScLiveSettingsIfOpen()
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd())) {
		g_scLiveSettings->PersistFieldsToSavedata();
		g_scLiveSettings->DestroyWindow();
	}
}

void SyncScLiveSettingsIfOpen()
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd()))
		g_scLiveSettings->PersistFieldsToSavedata();
}

BOOL IsScLiveSettingsOpen()
{
	return (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd())) ? TRUE : FALSE;
}

HWND GetScLiveSettingsHwnd()
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd()))
		return g_scLiveSettings->GetSafeHwnd();
	return NULL;
}

void ScLiveSettingsApplyUrlKey(const CString& url, const CString& key)
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd()))
		g_scLiveSettings->SetUrlKey(url, key);
}

void ScLiveSettingsRevealAdvancedCreds()
{
	if (g_scLiveSettings && ::IsWindow(g_scLiveSettings->GetSafeHwnd()))
		g_scLiveSettings->RevealAdvancedCreds();
}

void ScLiveSettingsSetStreamingUi(BOOL streaming)
{
	if (!g_scLiveSettings || !::IsWindow(g_scLiveSettings->GetSafeHwnd()))
		return;
	HWND hwnd = g_scLiveSettings->GetSafeHwnd();
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
	// 配信中も画面には残す（状態確認用）。キャプチャ／画面合成への写り込みだけ防ぐ。
	if (streaming) {
		::SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
		if (!::IsWindowVisible(hwnd))
			g_scLiveSettings->ShowWindow(SW_SHOWNOACTIVATE);
		if (CWnd* hint = g_scLiveSettings->GetDlgItem(IDC_SC_LIVE_HINT)) {
			hint->SetWindowText(LL14(
				L"配信中: この窓は画面に出ますがキャプチャには写しません。本体ステータスも確認してください。",
				L"Streaming: this window stays visible but is excluded from capture. Check main status too.",
				L"Diffusion: cette fenetre reste visible mais est exclue de la capture.",
				L"In diretta: questa finestra resta visibile ma esclusa dalla cattura.",
				L"En vivo: esta ventana sigue visible pero no se captura.",
				L"방송 중: 이 창은 보이지만 캡처에는 안 나옵니다. 본체 상태도 확인하세요.",
				L"直播中：此窗口仍显示，但不会被截入。请同时查看主窗口状态。",
				L"أثناء البث: تبقى النافذة ظاهرة لكنها مستبعدة من الالتقاط.",
				L"В эфире: окно видно, но не попадает в захват. Смотрите статус основного окна.",
				L"Live: Fenster bleibt sichtbar, wird aber nicht erfasst. Status im Hauptfenster pruefen.",
				L"Ao vivo: janela fica visivel mas fora da captura. Veja tambem o status principal.",
				L"Live: venster blijft zichtbaar maar valt buiten capture. Check ook hoofdstatus.",
				L"Na zywo: okno widoczne, ale poza przechwytywaniem. Sprawdz tez status glownego okna.",
				L"Canlida: pencere gorunur kalir ama yakalamaya girmez. Ana durumuna da bakin."));
		}
	} else {
		::SetWindowDisplayAffinity(hwnd, WDA_NONE);
	}
}

void ScLiveSettingsSetStatusText(const CString& text)
{
	if (!g_scLiveSettings || !::IsWindow(g_scLiveSettings->GetSafeHwnd()))
		return;
	if (CWnd* hint = g_scLiveSettings->GetDlgItem(IDC_SC_LIVE_HINT))
		hint->SetWindowText(text);
}
