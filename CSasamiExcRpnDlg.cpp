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
	ON_CBN_SELCHANGE(IDC_SASAMI_EXC_PRESET, &CSasamiExcRpnDlg::OnCbnPreset)
	ON_BN_CLICKED(IDC_SASAMI_EXC_RPN, &CSasamiExcRpnDlg::OnBnClickedRpn)
	ON_BN_CLICKED(IDC_SASAMI_EXC_NRPN, &CSasamiExcRpnDlg::OnBnClickedNrpn)
	ON_BN_CLICKED(IDC_SASAMI_EXC_SYSEX, &CSasamiExcRpnDlg::OnBnClickedSysex)
	ON_BN_CLICKED(IDC_SASAMI_EXC_CLOSE, &CSasamiExcRpnDlg::OnBnClickedClose)
END_MESSAGE_MAP()

void CSasamiExcRpnDlg::ApplyLang()
{
	SetWindowText(L"SASAMI Exc / RPN");
	m_btnRpn.SetWindowText(L"Insert RPN");
	m_btnNrpn.SetWindowText(L"Insert NRPN");
	m_btnSysex.SetWindowText(L"Insert SysEx");
	m_btnClose.SetWindowText(L"Close");
	RefreshHint(L"Preset → fill fields. RPN presets use Insert RPN. SysEx presets use Insert SysEx.");
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
		RefreshHint(L"SysEx preset ready — press Insert SysEx.");
	} else {
		m_hex.SetWindowText(L"");
	}
	if (p.rpnA >= 0) {
		CString s;
		s.Format(L"%d", p.rpnA); m_msb.SetWindowText(s);
		s.Format(L"%d", p.rpnB); m_lsb.SetWindowText(s);
		s.Format(L"%d", p.rpnC); m_data.SetWindowText(s);
		RefreshHint(L"RPN preset ready — press Insert RPN (not SysEx).");
	}
}

void CSasamiExcRpnDlg::RefreshHint(const wchar_t* msg)
{
	if (!m_hint.GetSafeHwnd()) return;
	CString s;
	s.Format(L"%s\r\nTarget: MIDI %d, tick %u. SysEx uses SASAMI cmd36 body bytes.",
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
		RefreshHint(L"Inserted RPN.");
		NotifyChanged();
	} else RefreshHint(L"RPN insert failed.");
}

void CSasamiExcRpnDlg::OnBnClickedNrpn()
{
	int a, b, c;
	ReadTriplet(&a, &b, &c);
	if (ScMidiAddNrpn(m_doc, m_tick, m_ch, a, b, c)) {
		ExcPreviewShort(m_ch, 0xB0, 0x63, a);
		ExcPreviewShort(m_ch, 0xB0, 0x62, b);
		ExcPreviewShort(m_ch, 0xB0, 0x06, c);
		RefreshHint(L"Inserted NRPN.");
		NotifyChanged();
	} else RefreshHint(L"NRPN insert failed.");
}

void CSasamiExcRpnDlg::OnBnClickedSysex()
{
	uint8_t b[ScMidiDoc::SC_SYSEX_BYTES];
	int n = ReadHex(b, ScMidiDoc::SC_SYSEX_BYTES);
	if (n <= 0) {
		RefreshHint(L"SysEx insert failed — Hex/body is empty. Pick a GM/GS/XG preset first, or paste hex.");
		return;
	}
	if (ScMidiAddSysex(m_doc, m_tick, m_ch, b, n)) {
		VstLiveMidiSysex(0, b, n);
		RefreshHint(L"Inserted SysEx.");
		NotifyChanged();
	} else RefreshHint(L"SysEx insert failed.");
}

void CSasamiExcRpnDlg::OnBnClickedClose() { DestroyWindow(); }
void CSasamiExcRpnDlg::OnClose() { DestroyWindow(); }
void CSasamiExcRpnDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
