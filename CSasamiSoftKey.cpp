#include "stdafx.h"
#include "CSasamiSoftKey.h"
#include "CSasamiScoreMidiIn.h"
#include "VstMidiEngine.h"

namespace {

enum { kWhites = 19, kOctMin = 1, kOctMax = 7 };

struct Bind {
	UINT vk;
	int white; /* 0..kWhites-1, or the white to the left of a black key */
	int black;
	const wchar_t* cap;
};

/* Bottom row then top row, the way a JIS/US overlay sits on the keys. */
static const Bind kBinds[] = {
	{ 'Z', 0, 0, L"Z" }, { 'X', 1, 0, L"X" }, { 'C', 2, 0, L"C" },
	{ 'V', 3, 0, L"V" }, { 'B', 4, 0, L"B" }, { 'N', 5, 0, L"N" },
	{ 'M', 6, 0, L"M" }, { VK_OEM_COMMA, 7, 0, L"<" }, { VK_OEM_PERIOD, 8, 0, L">" },
	{ 'Q', 9, 0, L"Q" }, { 'W', 10, 0, L"W" }, { 'E', 11, 0, L"E" },
	{ 'R', 12, 0, L"R" }, { 'T', 13, 0, L"T" }, { 'Y', 14, 0, L"Y" },
	{ 'U', 15, 0, L"U" }, { 'I', 16, 0, L"I" }, { 'O', 17, 0, L"O" },
	{ 'P', 18, 0, L"P" },
	{ 'S', 0, 1, L"S" }, { 'D', 1, 1, L"D" },
	{ 'G', 3, 1, L"G" }, { 'H', 4, 1, L"H" }, { 'J', 5, 1, L"J" },
	{ '2', 7, 1, L"2" }, { '3', 8, 1, L"3" },
	{ '5', 10, 1, L"5" }, { '6', 11, 1, L"6" }, { '7', 12, 1, L"7" },
	{ '9', 14, 1, L"9" }, { '0', 15, 1, L"0" },
};

class CSoftKeyWnd;
static CSoftKeyWnd* g_key = NULL;

static int WhiteSemi(int white)
{
	const int oct = white / 7;
	static const int k[7] = { 0, 2, 4, 5, 7, 9, 11 };
	return oct * 12 + k[white % 7];
}

static int BlackAfter(int white)
{
	const int a = WhiteSemi(white);
	const int b = WhiteSemi(white + 1);
	return (b - a) == 2;
}

class CSoftKeyWnd : public CWnd {
public:
	int m_oct;
	int m_ch;
	int m_port;
	int m_downNote;
	UINT m_heldVk;
	HWND m_stepTarget;
	int m_step;
	CRect m_minus, m_plus, m_chRc, m_stepRc;

	CSoftKeyWnd() : m_oct(4), m_ch(0), m_port(0), m_downNote(-1), m_heldVk(0),
		m_stepTarget(NULL), m_step(0) {}

	int StepLive() const
	{
		return m_step && m_stepTarget && ::IsWindow(m_stepTarget);
	}

	void PostStep(int kind, DWORD msg)
	{
		if (StepLive()) ::PostMessage(m_stepTarget, WM_SASAMI_SCORE_MIDI, (WPARAM)msg, (LPARAM)kind);
	}

	int NoteOf(int semi) const
	{
		int n = m_oct * 12 + semi;
		if (n < 0) n = 0;
		if (n > 127) n = 127;
		return n;
	}

	void SendNote(int note, int on)
	{
		if (note < 0 || note > 127) return;
		const DWORD msg = (DWORD)((on ? 0x90 : 0x80) | (m_ch & 15)) |
			((DWORD)note << 8) | (on ? (100u << 16) : 0);
		if (StepLive()) {
			/* The score auditions the step note itself on the part's own route. */
			if (on) PostStep(SC_SOFTKEY_STEP_NOTE, msg);
			return;
		}
		VstLiveMidiShort(m_port, msg);
	}

	void NoteOff()
	{
		if (m_downNote >= 0) SendNote(m_downNote, 0);
		m_downNote = -1;
		m_heldVk = 0;
	}

	void NoteOn(int note, UINT vk)
	{
		if (note == m_downNote && vk == m_heldVk) return;
		NoteOff();
		m_downNote = note;
		m_heldVk = vk;
		SendNote(note, 1);
	}

	CRect WhiteRect(int i, const CRect& rc) const
	{
		const int top = rc.top + 36;
		const int bot = rc.bottom - 8;
		const int gap = 2;
		const int w = (rc.Width() - 16 - gap * (kWhites - 1)) / kWhites;
		const int x = rc.left + 8 + i * (w + gap);
		return CRect(x, top, x + w, bot);
	}

	int Hit(CPoint pt, int* black) const
	{
		CRect rc; GetClientRect(&rc);
		if (black) *black = 0;
		for (int i = 0; i < kWhites - 1; i++) {
			if (!BlackAfter(i)) continue;
			CRect w = WhiteRect(i, rc);
			CRect b(w.right - w.Width() / 3, w.top, w.right + w.Width() / 3, w.top + (w.Height() * 58) / 100);
			if (b.PtInRect(pt)) { if (black) *black = 1; return i; }
		}
		for (int i = 0; i < kWhites; i++)
			if (WhiteRect(i, rc).PtInRect(pt)) return i;
		return -1;
	}

	const wchar_t* CapFor(int white, int black) const
	{
		for (int i = 0; i < (int)(sizeof(kBinds) / sizeof(kBinds[0])); i++)
			if (kBinds[i].white == white && kBinds[i].black == black) return kBinds[i].cap;
		return L"";
	}

	int SemiForVk(UINT vk, int* black) const
	{
		if (vk >= 'a' && vk <= 'z') vk = (UINT)(vk - 'a' + 'A');
		for (int i = 0; i < (int)(sizeof(kBinds) / sizeof(kBinds[0])); i++) {
			if (kBinds[i].vk != vk) continue;
			if (black) *black = kBinds[i].black;
			int semi = WhiteSemi(kBinds[i].white);
			if (kBinds[i].black) semi += 1;
			return semi;
		}
		return -1;
	}

	void Paint(CDC& dc)
	{
		CRect rc; GetClientRect(&rc);
		dc.FillSolidRect(rc, RGB(92, 28, 48));
		dc.SetBkMode(TRANSPARENT);
		CFont f;
		f.CreateFont(15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		CFont* of = dc.SelectObject(&f);
		dc.SetTextColor(RGB(230, 210, 214));
		dc.TextOut(10, 8, L"OCT");
		m_minus.SetRect(48, 6, 78, 28);
		m_plus.SetRect(118, 6, 148, 28);
		dc.FillSolidRect(m_minus, RGB(120, 48, 68));
		dc.FillSolidRect(m_plus, RGB(120, 48, 68));
		dc.Draw3dRect(m_minus, RGB(180, 120, 140), RGB(50, 16, 28));
		dc.Draw3dRect(m_plus, RGB(180, 120, 140), RGB(50, 16, 28));
		dc.SetTextColor(RGB(255, 230, 236));
		dc.DrawText(L"-", m_minus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		dc.DrawText(L"+", m_plus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		wchar_t oct[8];
		_snwprintf_s(oct, _TRUNCATE, L"%d", m_oct);
		dc.TextOut(88, 8, oct);
		wchar_t chs[16];
		if (m_port > 0) _snwprintf_s(chs, _TRUNCATE, L"%c%d", L'A' + m_port, m_ch + 1);
		else _snwprintf_s(chs, _TRUNCATE, L"ch %d", m_ch + 1);
		m_chRc.SetRect(168, 6, 230, 28);
		dc.FillSolidRect(m_chRc, RGB(70, 22, 38));
		dc.Draw3dRect(m_chRc, RGB(160, 100, 120), RGB(40, 12, 22));
		dc.DrawText(chs, m_chRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (m_stepTarget && ::IsWindow(m_stepTarget)) {
			m_stepRc.SetRect(244, 6, 314, 28);
			dc.FillSolidRect(m_stepRc, m_step ? RGB(200, 60, 70) : RGB(70, 22, 38));
			dc.Draw3dRect(m_stepRc, RGB(200, 140, 150), RGB(40, 12, 22));
			dc.SetTextColor(RGB(255, 236, 240));
			dc.DrawText(L"STEP", m_stepRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			dc.SetTextColor(RGB(230, 210, 214));
			CRect hint(326, 6, rc.right - 8, 28);
			dc.DrawText(m_step
				? LL14(L"鍵で入力→マーカー前進 / Space 休符 / BS 戻る", L"Key = enter note / Space = rest / BS = back", L"Touche = note / Espace = silence / Retour = annuler", L"Tasto = nota / Spazio = pausa / BS = indietro", L"Tecla = nota / Espacio = silencio / Retroceso = atrás", L"건반=입력 / Space=쉼표 / BS=되돌림", L"按键=输入 / Space=休止 / BS=后退", L"مفتاح=نوتة / Space=سكتة / BS=رجوع", L"Клавиша = нота / Space = пауза / BS = назад", L"Taste = Note / Leer = Pause / Rück = zurück", L"Tecla = nota / Espaço = pausa / BS = voltar", L"Toets = noot / Spatie = rust / BS = terug", L"Klawisz = nuta / Spacja = pauza / BS = wstecz", L"Tuş = nota / Space = es / BS = geri")
				: LL14(L"STEP で楽譜へステップ入力", L"STEP = step-enter into the score", L"STEP = saisie pas à pas", L"STEP = inserimento passo", L"STEP = entrada por pasos", L"STEP = 악보에 스텝 입력", L"STEP = 分步输入到乐谱", L"STEP = إدخال خطوة", L"STEP = пошаговый ввод", L"STEP = Schritteingabe", L"STEP = entrada passo a passo", L"STEP = stapinvoer", L"STEP = wprowadzanie krokowe", L"STEP = adım girişi"),
				hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		} else {
			m_stepRc.SetRectEmpty();
		}

		CFont lab;
		lab.CreateFont(13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		for (int i = 0; i < kWhites; i++) {
			CRect w = WhiteRect(i, rc);
			const int semi = WhiteSemi(i);
			const int note = NoteOf(semi);
			const BOOL hot = (m_downNote == note);
			dc.FillSolidRect(w, hot ? RGB(190, 220, 255) : RGB(245, 245, 248));
			dc.Draw3dRect(w, RGB(40, 40, 48), RGB(180, 180, 188));
			dc.SelectObject(&lab);
			dc.SetTextColor(RGB(40, 160, 70));
			CRect t = w; t.top = w.bottom - 22;
			dc.DrawText(CapFor(i, 0), t, DT_CENTER | DT_TOP | DT_SINGLELINE);
		}
		for (int i = 0; i < kWhites - 1; i++) {
			if (!BlackAfter(i)) continue;
			CRect w = WhiteRect(i, rc);
			CRect b(w.right - w.Width() / 3, w.top, w.right + w.Width() / 3, w.top + (w.Height() * 58) / 100);
			const int note = NoteOf(WhiteSemi(i) + 1);
			const BOOL hot = (m_downNote == note);
			dc.FillSolidRect(b, hot ? RGB(70, 90, 120) : RGB(22, 22, 26));
			dc.Draw3dRect(b, RGB(8, 8, 10), RGB(60, 60, 68));
			dc.SetTextColor(RGB(70, 220, 90));
			dc.DrawText(CapFor(i, 1), b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		dc.SelectObject(of);
	}

	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect r; GetClientRect(&r);
		CDC mem; CBitmap bmp;
		mem.CreateCompatibleDC(&dc);
		bmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
		CBitmap* old = mem.SelectObject(&bmp);
		Paint(mem);
		dc.BitBlt(0, 0, r.Width(), r.Height(), &mem, 0, 0, SRCCOPY);
		mem.SelectObject(old);
	}
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnLButtonDown(UINT, CPoint pt)
	{
		SetFocus();
		if (m_minus.PtInRect(pt)) {
			if (m_oct > kOctMin) m_oct--;
			NoteOff(); Invalidate(FALSE); return;
		}
		if (m_plus.PtInRect(pt)) {
			if (m_oct < kOctMax) m_oct++;
			NoteOff(); Invalidate(FALSE); return;
		}
		if (m_chRc.PtInRect(pt)) {
			m_ch = (m_ch + 1) & 15;
			NoteOff(); Invalidate(FALSE); return;
		}
		if (!m_stepRc.IsRectEmpty() && m_stepRc.PtInRect(pt)) {
			NoteOff();
			m_step = !m_step;
			Invalidate(FALSE); return;
		}
		int black = 0;
		const int w = Hit(pt, &black);
		if (w < 0) return;
		int semi = WhiteSemi(w);
		if (black) semi += 1;
		NoteOn(NoteOf(semi), 0);
		SetCapture();
		Invalidate(FALSE);
	}
	afx_msg void OnLButtonUp(UINT, CPoint)
	{
		if (GetCapture() == this) ReleaseCapture();
		NoteOff();
		Invalidate(FALSE);
	}
	afx_msg void OnKeyDown(UINT nChar, UINT, UINT flags)
	{
		if (flags & 0x4000) return;
		if (nChar == VK_OEM_MINUS || nChar == VK_SUBTRACT) {
			if (m_oct > kOctMin) m_oct--;
			Invalidate(FALSE); return;
		}
		if (nChar == VK_OEM_PLUS || nChar == VK_ADD) {
			if (m_oct < kOctMax) m_oct++;
			Invalidate(FALSE); return;
		}
		if (StepLive() && nChar == VK_SPACE) { PostStep(SC_SOFTKEY_STEP_REST, 0); return; }
		if (StepLive() && nChar == VK_BACK) { PostStep(SC_SOFTKEY_STEP_BACK, 0); return; }
		int black = 0;
		const int semi = SemiForVk(nChar, &black);
		if (semi < 0) return;
		NoteOn(NoteOf(semi), nChar);
		Invalidate(FALSE);
	}
	afx_msg void OnKeyUp(UINT nChar, UINT, UINT)
	{
		if (nChar == m_heldVk || (nChar >= 'a' && nChar <= 'z' && (UINT)(nChar - 'a' + 'A') == m_heldVk)) {
			NoteOff();
			Invalidate(FALSE);
		}
	}
	afx_msg void OnDestroy()
	{
		NoteOff();
		if (g_key == this) g_key = NULL;
		CWnd::OnDestroy();
	}
	virtual void PostNcDestroy() { delete this; }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CSoftKeyWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_KEYDOWN()
	ON_WM_KEYUP()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

} // namespace

void ScSoftKeySetPart(int part0)
{
	if (!g_key || !g_key->GetSafeHwnd() || part0 < 0) return;
	g_key->NoteOff();
	g_key->m_ch = part0 & 15;
	g_key->m_port = (part0 / 16) & 3;
	g_key->Invalidate(FALSE);
}

void ScSoftKeyDetach(CWnd* stepTarget)
{
	if (!g_key || !g_key->GetSafeHwnd() || !stepTarget) return;
	if (g_key->m_stepTarget != stepTarget->GetSafeHwnd()) return;
	g_key->m_stepTarget = NULL;
	g_key->m_step = 0;
	g_key->Invalidate(FALSE);
}

void ScSoftKeyOpen(CWnd* owner, CWnd* stepTarget, int part0)
{
	if (g_key && g_key->GetSafeHwnd()) {
		if (stepTarget) g_key->m_stepTarget = stepTarget->GetSafeHwnd();
		ScSoftKeySetPart(part0);
		g_key->ShowWindow(SW_SHOW);
		g_key->SetForegroundWindow();
		return;
	}
	g_key = new CSoftKeyWnd();
	if (stepTarget) g_key->m_stepTarget = stepTarget->GetSafeHwnd();
	if (part0 >= 0) {
		g_key->m_ch = part0 & 15;
		g_key->m_port = (part0 / 16) & 3;
	}
	CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)(COLOR_WINDOW + 1), NULL);
	CRect rc(80, 80, 980, 250);
	if (owner && owner->GetSafeHwnd()) {
		CRect o; owner->GetWindowRect(&o);
		rc.OffsetRect(o.left + 40 - rc.left, o.top + 80 - rc.top);
	}
	g_key->CreateEx(WS_EX_TOOLWINDOW, cls, L"キーボード MIDI",
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		rc, owner, 0);
}
