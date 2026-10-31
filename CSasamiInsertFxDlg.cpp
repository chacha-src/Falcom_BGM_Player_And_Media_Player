#include "stdafx.h"
#include "ogg.h"
#include "CSasamiInsertFxDlg.h"
#include "CSasamiVstPickDlg.h"
#include "VstMidiEngine.h"
#include "CSasamiStaffCore.h"

CSasamiInsertFxDlg* CSasamiInsertFxDlg::s_inst = NULL;

IMPLEMENT_DYNAMIC(CSasamiInsertFxDlg, CCustomBlurDialogExBase)

CSasamiInsertFxDlg::CSasamiInsertFxDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_INSERT_FX, pParent)
	, m_doc(NULL), m_part(1), m_notify(NULL)
	, m_dragKnob(-1), m_dragStartY(0), m_dragStartVal(0.0f), m_paramPage(0)
{
	for (int i = 0; i < 12; ++i) m_knobRc[i].SetRectEmpty();
}

CSasamiInsertFxDlg* CSasamiInsertFxDlg::OpenOwned(CWnd* owner, ScMidiDoc* doc, int part1to32)
{
	if (!doc) return NULL;
	if (part1to32 < 1) part1to32 = 1;
	if (part1to32 > 32) part1to32 = 32;
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_doc = doc;
		s_inst->m_part = part1to32;
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
		s_inst->m_paramPage = 0;
		s_inst->RefreshHint();
		s_inst->ShowWindow(SW_SHOW);
		s_inst->BringWindowToTop();
		return s_inst;
	}
	s_inst = new CSasamiInsertFxDlg(owner);
	s_inst->m_doc = doc;
	s_inst->m_part = part1to32;
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	CWnd* parent = AfxGetMainWnd();
	if (!parent) parent = owner;
	if (!s_inst->Create(IDD_SASAMI_INSERT_FX, parent)) {
		delete s_inst;
		s_inst = NULL;
		return NULL;
	}
	s_inst->ShowWindow(SW_SHOW);
	return s_inst;
}

void CSasamiInsertFxDlg::CloseOpen(void)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd()))
		s_inst->DestroyWindow();
}

void CSasamiInsertFxDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_FX_SLOT, m_slot);
	DDX_Control(pDX, IDC_SASAMI_FX_PICK, m_pick);
	DDX_Control(pDX, IDC_SASAMI_FX_BYPASS, m_bypass);
	DDX_Control(pDX, IDC_SASAMI_FX_EDITOR, m_editor);
	DDX_Control(pDX, IDC_SASAMI_FX_CLOSE, m_close);
	DDX_Control(pDX, IDC_SASAMI_FX_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CSasamiInsertFxDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_CLOSE()
	ON_CBN_SELCHANGE(IDC_SASAMI_FX_SLOT, OnCbnSlot)
	ON_BN_CLICKED(IDC_SASAMI_FX_PICK, OnBnClickedPick)
	ON_BN_CLICKED(IDC_SASAMI_FX_BYPASS, OnBnClickedBypass)
	ON_BN_CLICKED(IDC_SASAMI_FX_EDITOR, OnBnClickedEditor)
	ON_BN_CLICKED(IDC_SASAMI_FX_CLOSE, OnBnClickedClose)
END_MESSAGE_MAP()

void CSasamiInsertFxDlg::ApplyLang()
{
	SetWindowText(LL14(L"インサートFX", L"Insert FX", L"Effet d'insert", L"FX di insert", L"FX de inserción", L"인서트 FX", L"插入效果", L"تأثير إدراج", L"Вставной FX", L"Insert-FX", L"FX de inserção", L"Insert-FX", L"FX wstawiany", L"Ekleme FX"));
	m_pick.SetWindowText(LL14(L"選ぶ…", L"Choose…", L"Choisir…", L"Scegli…", L"Elegir…", L"선택…", L"选择…", L"اختر…", L"Выбрать…", L"Wählen…", L"Escolher…", L"Kiezen…", L"Wybierz…", L"Seç…"));
	m_bypass.SetWindowText(LL14(L"バイパス", L"Bypass", L"Contournement", L"Bypass", L"Omitir", L"바이패스", L"旁通", L"تجاوز", L"Обход", L"Bypass", L"Bypass", L"Bypass", L"Bypass", L"Baypas"));
	m_editor.SetWindowText(LL14(L"設定画面", L"Settings", L"Réglages", L"Impostazioni", L"Ajustes", L"설정 화면", L"设置画面", L"الإعدادات", L"Настройки", L"Einstellungen", L"Ajustes", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
}

BOOL CSasamiInsertFxDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_slot.SetAeroMode(FALSE);
	m_pick.SetAeroMode(FALSE); m_pick.SetFlat(TRUE);
	m_bypass.SetAeroMode(FALSE); m_bypass.SetFlat(TRUE);
	m_editor.SetAeroMode(FALSE); m_editor.SetFlat(TRUE);
	m_close.SetAeroMode(FALSE); m_close.SetFlat(TRUE);
	m_hint.SetAeroMode(FALSE);
	m_slot.ResetContent();
	m_slot.AddString(L"FX 1");
	m_slot.AddString(L"FX 2");
	m_slot.SetCurSel(0);
	m_paramPage = 0;
	ApplyLang();
	if (!ScRestoreWndGeom(this, savedata.sasamiVstPickX, savedata.sasamiVstPickY,
		savedata.sasamiVstPickW, savedata.sasamiVstPickH, 520, 320))
		SetWindowPos(NULL, 0, 0, 640, 420, SWP_NOMOVE | SWP_NOZORDER);
	LayoutChrome();
	RefreshHint();
	return TRUE;
}

int CSasamiInsertFxDlg::CurSlot() const
{
	int s = m_slot.GetCurSel();
	if (s < 0 || s > 1) s = 0;
	return s;
}

int CSasamiInsertFxDlg::KnobParamIndex(int knobI) const
{
	return m_paramPage * 12 + knobI;
}

void CSasamiInsertFxDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 10;
	int y = cap + pad;
	int x = pad;
	if (m_slot.GetSafeHwnd()) { m_slot.MoveWindow(x, y, 72, 90); x += 82; }
	auto place = [&](CWnd& w, int minW) {
		if (!w.GetSafeHwnd()) return;
		const int ww = ScChromeButtonWidth(&w, minW, 140);
		w.MoveWindow(x, y, ww, 28);
		x += ww + 8;
	};
	place(m_pick, 72);
	place(m_bypass, 80);
	place(m_editor, 88);
	place(m_close, 64);
	y += 36;
	if (m_hint.GetSafeHwnd()) m_hint.MoveWindow(pad, y, rc.Width() - pad * 2, 48);
	y += 58;
	m_bodyRc.SetRect(pad, y, rc.Width() - pad, rc.Height() - pad);
	const int cols = 4;
	const int rows = 3;
	const int cellW = max(70, m_bodyRc.Width() / cols);
	const int cellH = max(72, m_bodyRc.Height() / rows);
	for (int i = 0; i < 12; ++i) {
		int c = i % cols;
		int r = i / cols;
		m_knobRc[i].SetRect(m_bodyRc.left + c * cellW, m_bodyRc.top + r * cellH,
			min(m_bodyRc.right, m_bodyRc.left + (c + 1) * cellW - 4),
			min(m_bodyRc.bottom, m_bodyRc.top + (r + 1) * cellH - 4));
	}
}

void CSasamiInsertFxDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_close.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiInsertFxDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	pDC->FillSolidRect(0, cap, rc.Width(), rc.Height() - cap, RGB(28, 31, 38));
	return TRUE;
}

static void FxDrawKnob(CDC& dc, const CRect& rc, float val, const wchar_t* name, const wchar_t* disp)
{
	if (val < 0.0f) val = 0.0f;
	if (val > 1.0f) val = 1.0f;
	const int rad = max(9, min(rc.Width(), rc.Height() - 26) / 2 - 3);
	CPoint c(rc.CenterPoint().x, rc.top + rad + 5);
	CBrush br(RGB(42, 51, 66));
	CPen rim(PS_SOLID, 1, RGB(120, 155, 220));
	CBrush* ob = dc.SelectObject(&br);
	CPen* op = dc.SelectObject(&rim);
	dc.Ellipse(c.x - rad, c.y - rad, c.x + rad + 1, c.y + rad + 1);
	const double a = 3.1415926535 * (0.75 + 1.5 * (double)val);
	CPen needle(PS_SOLID, 2, RGB(245, 210, 100));
	dc.SelectObject(&needle);
	dc.MoveTo(c);
	dc.LineTo(c.x + (int)(cos(a) * (rad - 3)), c.y - (int)(sin(a) * (rad - 3)));
	dc.SelectObject(op);
	dc.SelectObject(ob);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(225, 232, 245));
	CString s;
	s.Format(L"%s %s", name ? name : L"", disp ? disp : L"");
	dc.DrawText(s, CRect(rc.left, c.y + rad + 2, rc.right, rc.bottom), DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

void CSasamiInsertFxDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	dc.FillSolidRect(0, cap, rc.Width(), rc.Height() - cap, RGB(28, 31, 38));
	CFont* old = dc.SelectObject(GetFont());
	const int slot = CurSlot();
	const int total = VstLiveFxParamCount(m_part, slot);
	int shown = 0;
	for (int i = 0; i < 12; ++i) {
		const int pi = KnobParamIndex(i);
		if (pi >= total) break;
		wchar_t name[64] = {};
		wchar_t disp[64] = {};
		VstLiveFxParamName(m_part, slot, pi, name, 64);
		VstLiveFxParamDisplay(m_part, slot, pi, disp, 64);
		FxDrawKnob(dc, m_knobRc[i], VstLiveFxGetParam(m_part, slot, pi), name, disp);
		++shown;
	}
	if (shown <= 0 && m_bodyRc.Width() > 0) {
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(210, 218, 232));
		dc.DrawText(LL14(
			L"インサート効果を選んでください（効果のみ）。ホイールでパラメータページ。",
			L"Pick an insert effect (effects only). Wheel = next/prev param page.",
			L"Choisissez un effet d'insert (effets seulement). Molette = page de paramètres.",
			L"Scegli un effetto di insert (solo effetti). Rotella = pagina parametri.",
			L"Elija un efecto de inserción (solo efectos). Rueda = página de parámetros.",
			L"인서트 효과를 고르세요(효과만). 휠로 파라미터 페이지.",
			L"请选择插入效果（仅效果）。滚轮切换参数页。",
			L"اختر تأثير إدراج (تأثيرات فقط). العجلة = صفحة المعاملات.",
			L"Выберите вставной эффект (только эффекты). Колесо — страница параметров.",
			L"Insert-Effekt wählen (nur Effekte). Rad = Parameterseite.",
			L"Escolha um efeito de inserção (só efeitos). Roda = página de parâmetros.",
			L"Kies een insert-effect (alleen effecten). Wiel = parameterpagina.",
			L"Wybierz efekt wstawiany (tylko efekty). Kółko = strona parametrów.",
			L"Bir ekleme efekti seçin (yalnız efekt). Tekerlek = parametre sayfası."),
			m_bodyRc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
	}
	dc.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiInsertFxDlg::RefreshHint()
{
	if (!m_hint.GetSafeHwnd()) return;
	const int slot = CurSlot();
	wchar_t path[520] = {};
	VstLiveFxGetPath(m_part, slot, path, 520);
	const wchar_t* base = path;
	if (const wchar_t* p = wcsrchr(path, L'\\')) base = p + 1;
	const int total = VstLiveFxParamCount(m_part, slot);
	const int from = total > 0 ? m_paramPage * 12 + 1 : 0;
	int to = m_paramPage * 12 + 12;
	if (to > total) to = total;
	CString s;
	s.Format(LL14(
		L"MIDIパート %d、FXスロット %d: %s%s — パラメータ %d–%d / %d（ホイールでページ）",
		L"MIDI part %d, FX slot %d: %s%s — params %d–%d / %d (wheel = page)",
		L"Partie MIDI %d, slot FX %d : %s%s — paramètres %d–%d / %d (molette = page)",
		L"Parte MIDI %d, slot FX %d: %s%s — parametri %d–%d / %d (rotella = pagina)",
		L"Parte MIDI %d, ranura FX %d: %s%s — parámetros %d–%d / %d (rueda = página)",
		L"MIDI 파트 %d, FX 슬롯 %d: %s%s — 파라미터 %d–%d / %d (휠로 페이지)",
		L"MIDI 声部 %d，FX 槽 %d：%s%s — 参数 %d–%d / %d（滚轮翻页）",
		L"جزء MIDI %d، خانة FX %d: %s%s — معاملات %d–%d / %d (العجلة = صفحة)",
		L"MIDI-партия %d, слот FX %d: %s%s — параметры %d–%d / %d (колесо — страница)",
		L"MIDI-Part %d, FX-Slot %d: %s%s — Parameter %d–%d / %d (Rad = Seite)",
		L"Parte MIDI %d, slot FX %d: %s%s — parâmetros %d–%d / %d (roda = página)",
		L"MIDI-deel %d, FX-slot %d: %s%s — parameters %d–%d / %d (wiel = pagina)",
		L"Partia MIDI %d, slot FX %d: %s%s — parametry %d–%d / %d (kółko = strona)",
		L"MIDI parti %d, FX yuvası %d: %s%s — parametre %d–%d / %d (tekerlek = sayfa)"),
		m_part, slot + 1,
		path[0] ? base : LL14(L"（なし）", L"(empty)", L"(vide)", L"(vuoto)", L"(vacío)", L"(없음)", L"（空）", L"(فارغ)", L"(пусто)", L"(leer)", L"(vazio)", L"(leeg)", L"(pusto)", L"(boş)"),
		VstLiveFxGetBypass(m_part, slot) ? LL14(L" ［バイパス］", L" [bypassed]", L" [contourné]", L" [bypass]", L" [omitido]", L" [바이패스]", L" ［旁通］", L" [متجاوز]", L" [обход]", L" [Bypass]", L" [bypass]", L" [bypass]", L" [bypass]", L" [baypas]") : L"",
		from, to, total);
	m_hint.SetWindowText(s);
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiInsertFxDlg::RefreshKnobs()
{
	InvalidateRect(m_bodyRc, FALSE);
}

void CSasamiInsertFxDlg::NotifyChanged()
{
	if (m_notify && ::IsWindow(m_notify))
		::PostMessage(m_notify, WM_SASAMI_INSERT_FX_CHANGED, (WPARAM)m_part, 0);
}

void CSasamiInsertFxDlg::CaptureStateToDoc()
{
	if (!m_doc) return;
	for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
		unsigned char* blob = NULL;
		int len = 0;
		if (VstLiveFxCaptureState(m_part, sl, &blob, &len) && blob && len > 0) {
			ScMidiFxBindSetState(&m_doc->fxBind, m_part - 1, sl, blob, (uint32_t)len);
			free(blob);
			m_doc->bind.isMpw3 = 1;
		}
	}
}

void CSasamiInsertFxDlg::OnCbnSlot()
{
	m_paramPage = 0;
	RefreshHint();
}

void CSasamiInsertFxDlg::OnBnClickedPick()
{
	wchar_t path[520] = {};
	int is3 = 0;
	if (CSasamiVstPickDlg::PickEffect(this, path, 520, &is3) != IDOK || !path[0]) {
		/* Fallback: raw file dialog if scan has no effects yet */
		CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST,
			LL14(
				L"VST効果 (*.dll;*.vst3)|*.dll;*.vst3|すべてのファイル (*.*)|*.*||",
				L"VST Effects (*.dll;*.vst3)|*.dll;*.vst3|All files (*.*)|*.*||",
				L"Effets VST (*.dll;*.vst3)|*.dll;*.vst3|Tous les fichiers (*.*)|*.*||",
				L"Effetti VST (*.dll;*.vst3)|*.dll;*.vst3|Tutti i file (*.*)|*.*||",
				L"Efectos VST (*.dll;*.vst3)|*.dll;*.vst3|Todos los archivos (*.*)|*.*||",
				L"VST 효과 (*.dll;*.vst3)|*.dll;*.vst3|모든 파일 (*.*)|*.*||",
				L"VST 效果 (*.dll;*.vst3)|*.dll;*.vst3|所有文件 (*.*)|*.*||",
				L"تأثيرات VST (*.dll;*.vst3)|*.dll;*.vst3|كل الملفات (*.*)|*.*||",
				L"Эффекты VST (*.dll;*.vst3)|*.dll;*.vst3|Все файлы (*.*)|*.*||",
				L"VST-Effekte (*.dll;*.vst3)|*.dll;*.vst3|Alle Dateien (*.*)|*.*||",
				L"Efeitos VST (*.dll;*.vst3)|*.dll;*.vst3|Todos os arquivos (*.*)|*.*||",
				L"VST-effecten (*.dll;*.vst3)|*.dll;*.vst3|Alle bestanden (*.*)|*.*||",
				L"Efekty VST (*.dll;*.vst3)|*.dll;*.vst3|Wszystkie pliki (*.*)|*.*||",
				L"VST efektleri (*.dll;*.vst3)|*.dll;*.vst3|Tüm dosyalar (*.*)|*.*||"), this);
		if (dlg.DoModal() != IDOK) return;
		wcsncpy_s(path, dlg.GetPathName(), _TRUNCATE);
		const size_t n = wcslen(path);
		is3 = (n >= 5 && _wcsicmp(path + n - 5, L".vst3") == 0) ? 1 : 0;
	}
	const int slot = CurSlot();
	if (VstLiveLoadFx(m_part, slot, path, is3) == 0) {
		wcsncpy_s(m_doc->fxBind.fxPath[m_part - 1][slot], path, _TRUNCATE);
		m_doc->fxBind.fxBypass[m_part - 1][slot] = 0;
		m_doc->bind.isMpw3 = 1;
		if (m_doc->fxBind.fxStateLen[m_part - 1][slot] && m_doc->fxBind.fxState[m_part - 1][slot])
			VstLiveFxApplyState(m_part, slot, m_doc->fxBind.fxState[m_part - 1][slot],
				(int)m_doc->fxBind.fxStateLen[m_part - 1][slot]);
		m_paramPage = 0;
		NotifyChanged();
		RefreshHint();
	} else {
		m_hint.SetWindowText(LL14(
			L"FXプラグインを読み込めませんでした。",
			L"Could not load the FX plug-in.",
			L"Impossible de charger le plug-in d'effet.",
			L"Impossibile caricare il plug-in FX.",
			L"No se pudo cargar el complemento de FX.",
			L"FX 플러그인을 불러오지 못했습니다.",
			L"无法加载 FX 插件。",
			L"تعذر تحميل إضافة التأثير.",
			L"Не удалось загрузить плагин FX.",
			L"FX-Plug-in konnte nicht geladen werden.",
			L"Não foi possível carregar o plug-in de FX.",
			L"De FX-plug-in kon niet worden geladen.",
			L"Nie udało się wczytać wtyczki FX.",
			L"FX eklentisi yüklenemedi."));
	}
}

void CSasamiInsertFxDlg::OnBnClickedBypass()
{
	const int slot = CurSlot();
	const int bypass = !VstLiveFxGetBypass(m_part, slot);
	VstLiveFxSetBypass(m_part, slot, bypass);
	m_doc->fxBind.fxBypass[m_part - 1][slot] = bypass;
	NotifyChanged();
	RefreshHint();
}

void CSasamiInsertFxDlg::OnBnClickedEditor()
{
	const int rc = VstLiveFxEditorOpen(m_part, CurSlot());
	if (rc != 0)
		m_hint.SetWindowText(LL14(
			L"このスロットにはFXの設定画面がありません。",
			L"This slot has no FX editor.",
			L"Ce slot n'a pas d'éditeur d'effet.",
			L"Questo slot non ha un editor FX.",
			L"Esta ranura no tiene editor de FX.",
			L"이 슬롯에는 FX 설정 화면이 없습니다.",
			L"此槽没有 FX 设置画面。",
			L"هذه الخانة بلا محرر تأثير.",
			L"У этого слота нет редактора FX.",
			L"Dieser Slot hat keinen FX-Editor.",
			L"Este slot não tem editor de FX.",
			L"Dit slot heeft geen FX-editor.",
			L"Ten slot nie ma edytora FX.",
			L"Bu yuvanın FX düzenleyicisi yok."));
}

void CSasamiInsertFxDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	for (int i = 0; i < 12; ++i) {
		if (m_knobRc[i].PtInRect(point)) {
			const int pi = KnobParamIndex(i);
			if (pi >= VstLiveFxParamCount(m_part, CurSlot())) break;
			m_dragKnob = i;
			m_dragStartY = point.y;
			m_dragStartVal = VstLiveFxGetParam(m_part, CurSlot(), pi);
			SetCapture();
			return;
		}
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiInsertFxDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if ((nFlags & MK_LBUTTON) && m_dragKnob >= 0) {
		float v = m_dragStartVal + (float)(m_dragStartY - point.y) / 140.0f;
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		VstLiveFxSetParam(m_part, CurSlot(), KnobParamIndex(m_dragKnob), v);
		RefreshKnobs();
		return;
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CSasamiInsertFxDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragKnob >= 0) {
		m_dragKnob = -1;
		ReleaseCapture();
		CaptureStateToDoc();
		NotifyChanged();
		RefreshHint();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

BOOL CSasamiInsertFxDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(pt);
	const int total = VstLiveFxParamCount(m_part, CurSlot());
	const int pages = total > 0 ? (total + 11) / 12 : 1;
	if (zDelta < 0) ++m_paramPage;
	else if (zDelta > 0) --m_paramPage;
	if (m_paramPage < 0) m_paramPage = 0;
	if (m_paramPage >= pages) m_paramPage = pages - 1;
	RefreshHint();
	return TRUE;
}

void CSasamiInsertFxDlg::OnBnClickedClose()
{
	CaptureStateToDoc();
	NotifyChanged();
	DestroyWindow();
}
void CSasamiInsertFxDlg::OnClose()
{
	CaptureStateToDoc();
	NotifyChanged();
	DestroyWindow();
}
void CSasamiInsertFxDlg::PostNcDestroy()
{
	ScSaveWndGeom(this, &savedata.sasamiVstPickX, &savedata.sasamiVstPickY,
		&savedata.sasamiVstPickW, &savedata.sasamiVstPickH);
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
