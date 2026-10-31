#include "stdafx.h"
#include "ogg.h"
#include "CSasamiExcRpnDlg.h"
#include "VstMidiEngine.h"

CSasamiExcRpnDlg* CSasamiExcRpnDlg::s_inst = NULL;

struct SasamiExcPreset {
	const wchar_t* name;
	const uint8_t* bytes;
	int len;
	int rpnA, rpnB, rpnC;
};

static const uint8_t kSxGmOn[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
static const uint8_t kSxGm2On[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7 };
static const uint8_t kSxGsReset[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
static const uint8_t kSxXgOn[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };
static const uint8_t kSxGsMasterVol[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x64, 0x58, 0xF7 };
static const uint8_t kSxGsReverbMacro[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x30, 0x00, 0x0F, 0xF7 };
static const uint8_t kSxGsChorusMacro[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x38, 0x00, 0x07, 0xF7 };

static const SasamiExcPreset kPresets[] = {
	{ L"RPN: Pitch Bend Sens 2", NULL, 0, 0, 0, 2 },
	{ L"RPN: Fine Tune 64", NULL, 0, 0, 1, 64 },
	{ L"RPN: Coarse Tune 64", NULL, 0, 0, 2, 64 },
	{ L"GM System On", kSxGmOn, sizeof(kSxGmOn), -1, -1, -1 },
	{ L"GM2 System On", kSxGm2On, sizeof(kSxGm2On), -1, -1, -1 },
	{ L"GS Reset", kSxGsReset, sizeof(kSxGsReset), -1, -1, -1 },
	{ L"XG System On", kSxXgOn, sizeof(kSxXgOn), -1, -1, -1 },
	{ L"GS Master Vol 100", kSxGsMasterVol, sizeof(kSxGsMasterVol), -1, -1, -1 },
	{ L"GS Reverb Macro 0", kSxGsReverbMacro, sizeof(kSxGsReverbMacro), -1, -1, -1 },
	{ L"GS Chorus Macro 0", kSxGsChorusMacro, sizeof(kSxGsChorusMacro), -1, -1, -1 },
};

static void ExcPreviewShort(int ch0, int status, int d1, int d2)
{
	const int ch = ch0 & 15;
	const DWORD msg = (DWORD)((status | ch) | ((d1 & 0x7F) << 8) | ((d2 & 0x7F) << 16));
	VstLiveMidiShort(0, msg);
}

IMPLEMENT_DYNAMIC(CSasamiExcRpnDlg, CCustomBlurDialogExBase)

CSasamiExcRpnDlg::CSasamiExcRpnDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_EXC_RPN, pParent)
	, m_doc(NULL), m_ch(0), m_tick(0), m_notify(NULL)
{
}

CSasamiExcRpnDlg* CSasamiExcRpnDlg::OpenOwned(CWnd* owner, ScMidiDoc* doc, int ch0, uint32_t tick)
{
	if (!doc) return NULL;
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_doc = doc;
		s_inst->m_ch = ch0;
		s_inst->m_tick = tick;
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
		s_inst->ShowWindow(SW_SHOW);
		s_inst->BringWindowToTop();
		CString h; h.Format(L"MIDI %d tick %u", ch0 + 1, (unsigned)tick);
		s_inst->RefreshHint(h);
		return s_inst;
	}
	s_inst = new CSasamiExcRpnDlg(owner);
	s_inst->m_doc = doc;
	s_inst->m_ch = ch0;
	s_inst->m_tick = tick;
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	CWnd* parent = AfxGetMainWnd();
	if (!parent) parent = owner;
	if (!s_inst->Create(IDD_SASAMI_EXC_RPN, parent)) {
		delete s_inst;
		s_inst = NULL;
		return NULL;
	}
	s_inst->ShowWindow(SW_SHOW);
	return s_inst;
}

void CSasamiExcRpnDlg::CloseOpen(void)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd()))
		s_inst->DestroyWindow();
}

void CSasamiExcRpnDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_EXC_PRESET, m_preset);
	DDX_Control(pDX, IDC_SASAMI_EXC_HEX, m_hex);
	DDX_Control(pDX, IDC_SASAMI_EXC_MSB, m_msb);
	DDX_Control(pDX, IDC_SASAMI_EXC_LSB, m_lsb);
	DDX_Control(pDX, IDC_SASAMI_EXC_DATA, m_data);
	DDX_Control(pDX, IDC_SASAMI_EXC_RPN, m_btnRpn);
	DDX_Control(pDX, IDC_SASAMI_EXC_NRPN, m_btnNrpn);
	DDX_Control(pDX, IDC_SASAMI_EXC_SYSEX, m_btnSysex);
	DDX_Control(pDX, IDC_SASAMI_EXC_CLOSE, m_btnClose);
	DDX_Control(pDX, IDC_SASAMI_EXC_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CSasamiExcRpnDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_CBN_SELCHANGE(IDC_SASAMI_EXC_PRESET, OnCbnPreset)
	ON_BN_CLICKED(IDC_SASAMI_EXC_RPN, OnBnClickedRpn)
	ON_BN_CLICKED(IDC_SASAMI_EXC_NRPN, OnBnClickedNrpn)
	ON_BN_CLICKED(IDC_SASAMI_EXC_SYSEX, OnBnClickedSysex)
	ON_BN_CLICKED(IDC_SASAMI_EXC_CLOSE, OnBnClickedClose)
END_MESSAGE_MAP()

void CSasamiExcRpnDlg::ApplyLang()
{
	SetWindowText(LL14(L"SASAMI 制御 / RPN", L"SASAMI Exc / RPN", L"SASAMI contrôle / RPN", L"SASAMI controllo / RPN", L"SASAMI control / RPN", L"SASAMI 제어 / RPN", L"SASAMI 控制 / RPN", L"SASAMI تحكم / RPN", L"SASAMI упр. / RPN", L"SASAMI Steuerung / RPN", L"SASAMI controle / RPN", L"SASAMI besturing / RPN", L"SASAMI sterowanie / RPN", L"SASAMI kontrol / RPN"));
	m_btnRpn.SetWindowText(LL14(L"RPNを入れる", L"Insert RPN", L"Insérer RPN", L"Inserisci RPN", L"Insertar RPN", L"RPN 넣기", L"插入 RPN", L"إدراج RPN", L"Вставить RPN", L"RPN einfügen", L"Inserir RPN", L"RPN invoegen", L"Wstaw RPN", L"RPN ekle"));
	m_btnNrpn.SetWindowText(LL14(L"NRPNを入れる", L"Insert NRPN", L"Insérer NRPN", L"Inserisci NRPN", L"Insertar NRPN", L"NRPN 넣기", L"插入 NRPN", L"إدراج NRPN", L"Вставить NRPN", L"NRPN einfügen", L"Inserir NRPN", L"NRPN invoegen", L"Wstaw NRPN", L"NRPN ekle"));
	m_btnSysex.SetWindowText(LL14(L"SysExを入れる", L"Insert SysEx", L"Insérer SysEx", L"Inserisci SysEx", L"Insertar SysEx", L"SysEx 넣기", L"插入 SysEx", L"إدراج SysEx", L"Вставить SysEx", L"SysEx einfügen", L"Inserir SysEx", L"SysEx invoegen", L"Wstaw SysEx", L"SysEx ekle"));
	m_btnClose.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	RefreshHint(LL14(
		L"プリセットで欄を埋めます。RPNは「RPNを入れる」、SysExは「SysExを入れる」。",
		L"Preset fills the fields. RPN presets use Insert RPN. SysEx presets use Insert SysEx.",
		L"Le préréglage remplit les champs. RPN : Insérer RPN. SysEx : Insérer SysEx.",
		L"Il preset riempie i campi. RPN: Inserisci RPN. SysEx: Inserisci SysEx.",
		L"El preajuste llena los campos. RPN: Insertar RPN. SysEx: Insertar SysEx.",
		L"프리셋이 칸을 채웁니다. RPN은 「RPN 넣기」, SysEx는 「SysEx 넣기」.",
		L"预设会填入栏位。RPN 用「插入 RPN」，SysEx 用「插入 SysEx」。",
		L"الإعداد المسبق يملأ الحقول. RPN: إدراج RPN. SysEx: إدراج SysEx.",
		L"Пресет заполняет поля. RPN — «Вставить RPN», SysEx — «Вставить SysEx».",
		L"Preset füllt die Felder. RPN: RPN einfügen. SysEx: SysEx einfügen.",
		L"O preset preenche os campos. RPN: Inserir RPN. SysEx: Inserir SysEx.",
		L"De preset vult de velden. RPN: RPN invoegen. SysEx: SysEx invoegen.",
		L"Preset wypełnia pola. RPN: Wstaw RPN. SysEx: Wstaw SysEx.",
		L"Önayar alanları doldurur. RPN: RPN ekle. SysEx: SysEx ekle."));
}

BOOL CSasamiExcRpnDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_preset.SetAeroMode(FALSE);
	m_hex.SetAeroMode(FALSE);
	m_msb.SetAeroMode(FALSE);
	m_lsb.SetAeroMode(FALSE);
	m_data.SetAeroMode(FALSE);
	m_btnRpn.SetAeroMode(FALSE); m_btnRpn.SetFlat(TRUE);
	m_btnNrpn.SetAeroMode(FALSE); m_btnNrpn.SetFlat(TRUE);
	m_btnSysex.SetAeroMode(FALSE); m_btnSysex.SetFlat(TRUE);
	m_btnClose.SetAeroMode(FALSE); m_btnClose.SetFlat(TRUE);
	m_hint.SetAeroMode(FALSE);
	m_preset.ResetContent();
	for (int i = 0; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); ++i)
		m_preset.AddString(kPresets[i].name);
	m_preset.SetCurSel(0);
	m_msb.SetWindowText(L"0");
	m_lsb.SetWindowText(L"0");
	m_data.SetWindowText(L"2");
	ApplyLang();
	OnCbnPreset();
	if (!ScRestoreWndGeom(this, savedata.sasamiNotePropsX, savedata.sasamiNotePropsY,
		savedata.sasamiNotePropsW, savedata.sasamiNotePropsH, 460, 240))
		SetWindowPos(NULL, 0, 0, 620, 360, SWP_NOMOVE | SWP_NOZORDER);
	LayoutChrome();
	return TRUE;
}

void CSasamiExcRpnDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 10;
	int y = cap + pad;
	if (m_preset.GetSafeHwnd()) m_preset.MoveWindow(pad, y, min(320, rc.Width() - pad * 2), 200);
	y += 34;
	if (m_hex.GetSafeHwnd()) m_hex.MoveWindow(pad + 70, y, max(120, rc.Width() - pad * 2 - 70), 24);
	y += 34;
	if (m_msb.GetWindowTextLength() >= 0 && m_msb.GetSafeHwnd()) m_msb.MoveWindow(pad + 44, y, 54, 24);
	if (m_lsb.GetSafeHwnd()) m_lsb.MoveWindow(pad + 140, y, 54, 24);
	if (m_data.GetSafeHwnd()) m_data.MoveWindow(pad + 236, y, 54, 24);
	y += 38;
	int x = pad;
	if (m_btnRpn.GetSafeHwnd()) { m_btnRpn.MoveWindow(x, y, 104, 28); x += 112; }
	if (m_btnNrpn.GetSafeHwnd()) { m_btnNrpn.MoveWindow(x, y, 104, 28); x += 112; }
	if (m_btnSysex.GetSafeHwnd()) { m_btnSysex.MoveWindow(x, y, 104, 28); x += 112; }
	if (m_btnClose.GetSafeHwnd()) m_btnClose.MoveWindow(x, y, 86, 28);
	y += 40;
	if (m_hint.GetSafeHwnd()) m_hint.MoveWindow(pad, y, rc.Width() - pad * 2, max(50, rc.Height() - y - pad));
}

void CSasamiExcRpnDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_btnClose.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiExcRpnDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	pDC->FillSolidRect(0, cap, rc.Width(), rc.Height() - cap, RGB(245, 247, 251));
	return TRUE;
}

void CSasamiExcRpnDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	dc.FillSolidRect(0, cap, rc.Width(), rc.Height() - cap, RGB(245, 247, 251));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(45, 50, 65));
	CFont* old = dc.SelectObject(GetFont());
	dc.TextOut(12, cap + 48, L"Hex/body");
	dc.TextOut(12, cap + 82, L"MSB");
	dc.TextOut(108, cap + 82, L"LSB");
	dc.TextOut(204, cap + 82, L"Data");
	dc.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

static void BytesToHex(const uint8_t* b, int n, CCustomEdit& ed)
{
	CString s;
	for (int i = 0; i < n; ++i) {
		CString x;
		x.Format(i ? L" %02X" : L"%02X", (unsigned)b[i]);
		s += x;
	}
	ed.SetWindowText(s);
}

void CSasamiExcRpnDlg::OnCbnPreset()
{
	int sel = m_preset.GetCurSel();
	if (sel < 0 || sel >= (int)(sizeof(kPresets) / sizeof(kPresets[0]))) return;
	const SasamiExcPreset& p = kPresets[sel];
	if (p.bytes && p.len > 0) {
		BytesToHex(p.bytes, p.len, m_hex);
		RefreshHint(LL14(L"SysExプリセットの準備ができました。「SysExを入れる」を押してください。", L"SysEx preset ready — press Insert SysEx.", L"Préréglage SysEx prêt — appuyez sur Insérer SysEx.", L"Preset SysEx pronto — premi Inserisci SysEx.", L"Preajuste SysEx listo — pulse Insertar SysEx.", L"SysEx 프리셋 준비됨. 「SysEx 넣기」를 누르세요.", L"SysEx 预设已就绪。请按「插入 SysEx」。", L"إعداد SysEx جاهز — اضغط إدراج SysEx.", L"Пресет SysEx готов — нажмите «Вставить SysEx».", L"SysEx-Preset bereit — SysEx einfügen drücken.", L"Preset SysEx pronto — pressione Inserir SysEx.", L"SysEx-preset klaar — druk op SysEx invoegen.", L"Preset SysEx gotowy — naciśnij Wstaw SysEx.", L"SysEx önayarı hazır — SysEx ekle'ye basın."));
	} else {
		m_hex.SetWindowText(L"");
	}
	if (p.rpnA >= 0) {
		CString s;
		s.Format(L"%d", p.rpnA); m_msb.SetWindowText(s);
		s.Format(L"%d", p.rpnB); m_lsb.SetWindowText(s);
		s.Format(L"%d", p.rpnC); m_data.SetWindowText(s);
		RefreshHint(LL14(L"RPNプリセットの準備ができました。「RPNを入れる」を押してください（SysExではない）。", L"RPN preset ready — press Insert RPN (not SysEx).", L"Préréglage RPN prêt — appuyez sur Insérer RPN (pas SysEx).", L"Preset RPN pronto — premi Inserisci RPN (non SysEx).", L"Preajuste RPN listo — pulse Insertar RPN (no SysEx).", L"RPN 프리셋 준비됨. 「RPN 넣기」를 누르세요(SysEx 아님).", L"RPN 预设已就绪。请按「插入 RPN」（不是 SysEx）。", L"إعداد RPN جاهز — اضغط إدراج RPN (ليس SysEx).", L"Пресет RPN готов — нажмите «Вставить RPN» (не SysEx).", L"RPN-Preset bereit — RPN einfügen drücken (nicht SysEx).", L"Preset RPN pronto — pressione Inserir RPN (não SysEx).", L"RPN-preset klaar — druk op RPN invoegen (niet SysEx).", L"Preset RPN gotowy — naciśnij Wstaw RPN (nie SysEx).", L"RPN önayarı hazır — RPN ekle'ye basın (SysEx değil)."));
	}
}

void CSasamiExcRpnDlg::RefreshHint(const wchar_t* msg)
{
	if (!m_hint.GetSafeHwnd()) return;
	CString s;
	s.Format(LL14(
		L"%s\r\n対象: MIDI %d、tick %u。SysExは SASAMI cmd36 の本体バイト。",
		L"%s\r\nTarget: MIDI %d, tick %u. SysEx uses SASAMI cmd36 body bytes.",
		L"%s\r\nCible : MIDI %d, tick %u. SysEx utilise les octets du corps cmd36 SASAMI.",
		L"%s\r\nDestinazione: MIDI %d, tick %u. SysEx usa i byte del corpo cmd36 SASAMI.",
		L"%s\r\nDestino: MIDI %d, tick %u. SysEx usa los bytes del cuerpo cmd36 de SASAMI.",
		L"%s\r\n대상: MIDI %d, tick %u. SysEx는 SASAMI cmd36 본문 바이트.",
		L"%s\r\n对象：MIDI %d，tick %u。SysEx 使用 SASAMI cmd36 的本体字节。",
		L"%s\r\nالهدف: MIDI %d، tick %u. SysEx يستخدم بايتات جسم cmd36 في SASAMI.",
		L"%s\r\nЦель: MIDI %d, tick %u. SysEx — байты тела cmd36 SASAMI.",
		L"%s\r\nZiel: MIDI %d, Tick %u. SysEx nutzt die Körperbytes von SASAMI cmd36.",
		L"%s\r\nAlvo: MIDI %d, tick %u. SysEx usa os bytes do corpo cmd36 SASAMI.",
		L"%s\r\nDoel: MIDI %d, tick %u. SysEx gebruikt de bodybytes van SASAMI cmd36.",
		L"%s\r\nCel: MIDI %d, tick %u. SysEx używa bajtów treści cmd36 SASAMI.",
		L"%s\r\nHedef: MIDI %d, tick %u. SysEx, SASAMI cmd36 gövde baytlarını kullanır."),
		msg ? msg : L"", m_ch + 1, (unsigned)m_tick);
	m_hint.SetWindowText(s);
}

int CSasamiExcRpnDlg::ReadTriplet(int* a, int* b, int* c)
{
	CString s;
	m_msb.GetWindowText(s); int x = _wtoi(s);
	m_lsb.GetWindowText(s); int y = _wtoi(s);
	m_data.GetWindowText(s); int z = _wtoi(s);
	if (x < 0) x = 0; if (x > 127) x = 127;
	if (y < 0) y = 0; if (y > 127) y = 127;
	if (z < 0) z = 0; if (z > 127) z = 127;
	if (a) *a = x; if (b) *b = y; if (c) *c = z;
	return 1;
}

int CSasamiExcRpnDlg::ReadHex(uint8_t* out, int maxOut)
{
	if (!out || maxOut <= 0) return 0;
	CString s; m_hex.GetWindowText(s);
	const wchar_t* p = s;
	int n = 0;
	while (*p && n < maxOut) {
		while (*p == L' ' || *p == L'\t' || *p == L',' || *p == L':' || *p == L'{' || *p == L'}') ++p;
		if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) p += 2;
		int hi = -1, lo = -1;
		if (*p >= L'0' && *p <= L'9') hi = *p++ - L'0';
		else if (*p >= L'A' && *p <= L'F') hi = *p++ - L'A' + 10;
		else if (*p >= L'a' && *p <= L'f') hi = *p++ - L'a' + 10;
		else break;
		if (*p >= L'0' && *p <= L'9') lo = *p++ - L'0';
		else if (*p >= L'A' && *p <= L'F') lo = *p++ - L'A' + 10;
		else if (*p >= L'a' && *p <= L'f') lo = *p++ - L'a' + 10;
		else lo = 0;
		out[n++] = (uint8_t)((hi << 4) | lo);
	}
	return n;
}

void CSasamiExcRpnDlg::NotifyChanged()
{
	if (m_notify && ::IsWindow(m_notify))
		::PostMessage(m_notify, WM_SASAMI_EXC_RPN_CHANGED, 1, 0);
}

void CSasamiExcRpnDlg::OnBnClickedRpn()
{
	int a, b, c;
	ReadTriplet(&a, &b, &c);
	if (ScMidiAddRpn(m_doc, m_tick, m_ch, a, b, c)) {
		ExcPreviewShort(m_ch, 0xB0, 0x65, a);
		ExcPreviewShort(m_ch, 0xB0, 0x64, b);
		ExcPreviewShort(m_ch, 0xB0, 0x06, c);
		RefreshHint(LL14(L"RPNを入れました。", L"Inserted RPN.", L"RPN inséré.", L"RPN inserito.", L"RPN insertado.", L"RPN을 넣었습니다.", L"已插入 RPN。", L"تم إدراج RPN.", L"RPN вставлен.", L"RPN eingefügt.", L"RPN inserido.", L"RPN ingevoegd.", L"Wstawiono RPN.", L"RPN eklendi."));
		NotifyChanged();
	} else RefreshHint(LL14(L"RPNを入れられませんでした。", L"Could not insert RPN.", L"Impossible d'insérer le RPN.", L"Impossibile inserire l'RPN.", L"No se pudo insertar el RPN.", L"RPN을 넣지 못했습니다.", L"无法插入 RPN。", L"تعذر إدراج RPN.", L"Не удалось вставить RPN.", L"RPN konnte nicht eingefügt werden.", L"Não foi possível inserir o RPN.", L"RPN kon niet worden ingevoegd.", L"Nie udało się wstawić RPN.", L"RPN eklenemedi."));
}

void CSasamiExcRpnDlg::OnBnClickedNrpn()
{
	int a, b, c;
	ReadTriplet(&a, &b, &c);
	if (ScMidiAddNrpn(m_doc, m_tick, m_ch, a, b, c)) {
		ExcPreviewShort(m_ch, 0xB0, 0x63, a);
		ExcPreviewShort(m_ch, 0xB0, 0x62, b);
		ExcPreviewShort(m_ch, 0xB0, 0x06, c);
		RefreshHint(LL14(L"NRPNを入れました。", L"Inserted NRPN.", L"NRPN inséré.", L"NRPN inserito.", L"NRPN insertado.", L"NRPN을 넣었습니다.", L"已插入 NRPN。", L"تم إدراج NRPN.", L"NRPN вставлен.", L"NRPN eingefügt.", L"NRPN inserido.", L"NRPN ingevoegd.", L"Wstawiono NRPN.", L"NRPN eklendi."));
		NotifyChanged();
	} else RefreshHint(LL14(L"NRPNを入れられませんでした。", L"Could not insert NRPN.", L"Impossible d'insérer le NRPN.", L"Impossibile inserire l'NRPN.", L"No se pudo insertar el NRPN.", L"NRPN을 넣지 못했습니다.", L"无法插入 NRPN。", L"تعذر إدراج NRPN.", L"Не удалось вставить NRPN.", L"NRPN konnte nicht eingefügt werden.", L"Não foi possível inserir o NRPN.", L"NRPN kon niet worden ingevoegd.", L"Nie udało się wstawić NRPN.", L"NRPN eklenemedi."));
}

void CSasamiExcRpnDlg::OnBnClickedSysex()
{
	uint8_t b[ScMidiDoc::SC_SYSEX_BYTES];
	int n = ReadHex(b, ScMidiDoc::SC_SYSEX_BYTES);
	if (n <= 0) {
		RefreshHint(LL14(
			L"SysExを入れられません。中身が空です。先に GM/GS/XG プリセットを選ぶか、16進を貼ってください。",
			L"Could not insert SysEx — the body is empty. Pick a GM/GS/XG preset first, or paste hex.",
			L"SysEx impossible : corps vide. Choisissez un préréglage GM/GS/XG, ou collez l'hexa.",
			L"SysEx impossibile: corpo vuoto. Scegli prima un preset GM/GS/XG, o incolla l'hex.",
			L"SysEx imposible: cuerpo vacío. Elija antes un preajuste GM/GS/XG, o pegue el hex.",
			L"SysEx를 넣지 못했습니다. 내용이 비어 있습니다. GM/GS/XG 프리셋을 고르거나 16진을 붙이세요.",
			L"无法插入 SysEx，内容为空。请先选 GM/GS/XG 预设，或粘贴十六进制。",
			L"تعذر إدراج SysEx — الجسم فارغ. اختر إعداد GM/GS/XG أو الصق ست عشري.",
			L"Не удалось вставить SysEx — тело пусто. Сначала пресет GM/GS/XG или вставьте hex.",
			L"SysEx nicht eingefügt — Körper leer. Zuerst GM/GS/XG-Preset wählen oder Hex einfügen.",
			L"Não foi possível inserir SysEx — corpo vazio. Escolha um preset GM/GS/XG ou cole o hex.",
			L"SysEx niet ingevoegd — body leeg. Kies eerst een GM/GS/XG-preset of plak hex.",
			L"Nie wstawiono SysEx — treść pusta. Najpierw preset GM/GS/XG albo wklej hex.",
			L"SysEx eklenemedi — gövde boş. Önce bir GM/GS/XG önayarı seçin veya hex yapıştırın."));
		return;
	}
	if (ScMidiAddSysex(m_doc, m_tick, m_ch, b, n)) {
		VstLiveMidiSysex(0, b, n);
		RefreshHint(LL14(L"SysExを入れました。", L"Inserted SysEx.", L"SysEx inséré.", L"SysEx inserito.", L"SysEx insertado.", L"SysEx를 넣었습니다.", L"已插入 SysEx。", L"تم إدراج SysEx.", L"SysEx вставлен.", L"SysEx eingefügt.", L"SysEx inserido.", L"SysEx ingevoegd.", L"Wstawiono SysEx.", L"SysEx eklendi."));
		NotifyChanged();
	} else RefreshHint(LL14(L"SysExを入れられませんでした。", L"Could not insert SysEx.", L"Impossible d'insérer le SysEx.", L"Impossibile inserire il SysEx.", L"No se pudo insertar el SysEx.", L"SysEx를 넣지 못했습니다.", L"无法插入 SysEx。", L"تعذر إدراج SysEx.", L"Не удалось вставить SysEx.", L"SysEx konnte nicht eingefügt werden.", L"Não foi possível inserir o SysEx.", L"SysEx kon niet worden ingevoegd.", L"Nie udało się wstawić SysEx.", L"SysEx eklenemedi."));
}

void CSasamiExcRpnDlg::OnBnClickedClose() { DestroyWindow(); }
void CSasamiExcRpnDlg::OnClose() { DestroyWindow(); }
void CSasamiExcRpnDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
