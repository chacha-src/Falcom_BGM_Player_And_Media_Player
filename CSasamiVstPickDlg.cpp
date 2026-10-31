#include "stdafx.h"
#include "ogg.h"
#include "CSasamiVstPickDlg.h"
#include "VstMidiEngine.h"
#include "CSasamiStaffCore.h"

IMPLEMENT_DYNAMIC(CSasamiVstPickDlg, CCustomBlurDialogExBase)

CSasamiVstPickDlg::CSasamiVstPickDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_VST_PICK, pParent)
	, m_isVst3(0), m_part(1), m_effectsOnly(0), m_vst2Only(0), m_scroll(0), m_sel(0), m_count(0)
{
	m_path[0] = 0;
}

int CSasamiVstPickDlg::PickForPart(CWnd* owner, int part1to32, wchar_t* outPath, int outCch, int* outIs3)
{
	/* Always show the instrument list. Opening HALion/MediaBay editor here hid the
	   picker behind the score and forced MediaBay dialogs the user did not ask for. */
	CSasamiVstPickDlg dlg(owner);
	dlg.m_part = part1to32;
	dlg.m_effectsOnly = 0;
	const INT_PTR r = dlg.DoModal();
	if (r != IDOK) return (int)r;
	if (outPath && outCch > 0)
		wcsncpy_s(outPath, outCch, dlg.m_path, _TRUNCATE);
	if (outIs3) *outIs3 = dlg.m_isVst3;
	return IDOK;
}

int CSasamiVstPickDlg::PickVst2ForPart(CWnd* owner, int part1to32, wchar_t* outPath, int outCch, int* outIs3)
{
	CSasamiVstPickDlg dlg(owner);
	dlg.m_part = part1to32;
	dlg.m_effectsOnly = 0;
	dlg.m_vst2Only = 1;
	const INT_PTR r = dlg.DoModal();
	if (r != IDOK) return (int)r;
	if (outPath && outCch > 0)
		wcsncpy_s(outPath, outCch, dlg.m_path, _TRUNCATE);
	if (outIs3) *outIs3 = 0;
	return IDOK;
}

int CSasamiVstPickDlg::PickEffect(CWnd* owner, wchar_t* outPath, int outCch, int* outIs3)
{
	CSasamiVstPickDlg dlg(owner);
	dlg.m_part = 0;
	dlg.m_effectsOnly = 1;
	const INT_PTR r = dlg.DoModal();
	if (r != IDOK) return (int)r;
	if (outPath && outCch > 0)
		wcsncpy_s(outPath, outCch, dlg.m_path, _TRUNCATE);
	if (outIs3) *outIs3 = dlg.m_isVst3;
	return IDOK;
}

void CSasamiVstPickDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_VST_OK, m_ok);
	DDX_Control(pDX, IDC_SASAMI_VST_CANCEL, m_cancel);
	DDX_Control(pDX, IDC_SASAMI_VST_RESCAN, m_rescan);
	DDX_Control(pDX, IDC_SASAMI_VST_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CSasamiVstPickDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_BN_CLICKED(IDC_SASAMI_VST_OK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_SASAMI_VST_CANCEL, OnBnClickedCancel)
	ON_BN_CLICKED(IDC_SASAMI_VST_RESCAN, OnBnClickedRescan)
END_MESSAGE_MAP()

void CSasamiVstPickDlg::ApplyLang()
{
	if (m_effectsOnly) {
		SetWindowText(LL14(L"インサートFXを選択", L"Select VST Insert FX", L"Choisir un effet VST", L"Scegli FX VST", L"Elegir FX VST",
			L"인서트 FX 선택", L"选择 VST 插入效果", L"اختر تأثير VST", L"Выбор вставного FX", L"VST-Insert wählen",
			L"Escolher FX VST", L"VST-insert kiezen", L"Wybierz FX VST", L"VST ekleme efekti seç"));
		m_hint.SetWindowText(LL14(
			L"スキャン済みの効果だけ（音源ではない）。ダブルクリックで決定。",
			L"Scanned effects only (not instruments). Double-click to choose.",
			L"Effets déjà analysés seulement (pas d'instruments). Double-clic pour choisir.",
			L"Solo effetti già analizzati (non strumenti). Doppio clic per scegliere.",
			L"Solo efectos ya analizados (no instrumentos). Doble clic para elegir.",
			L"스캔된 효과만 (음원 아님). 더블클릭으로 결정.",
			L"仅已扫描的效果（不是音源）。双击确定。",
			L"تأثيرات ممسوحة فقط (ليست آلات). نقرة مزدوجة للاختيار.",
			L"Только просканированные эффекты (не инструменты). Двойной щелчок.",
			L"Nur gescannte Effekte (keine Instrumente). Doppelklick zum Wählen.",
			L"Só efeitos já analisados (não instrumentos). Duplo clique para escolher.",
			L"Alleen gescande effecten (geen instrumenten). Dubbelklik om te kiezen.",
			L"Tylko zeskanowane efekty (nie instrumenty). Podwójne kliknięcie.",
			L"Yalnız taranmış efektler (çalgı değil). Seçmek için çift tık."));
	} else if (m_vst2Only) {
		SetWindowText(LL14(L"VST2音色を選択", L"Select VST2", L"Choisir VST2", L"Scegli VST2", L"Elegir VST2",
			L"VST2 선택", L"选择 VST2", L"اختر VST2", L"Выбор VST2", L"VST2 wählen",
			L"Escolher VST2", L"VST2 kiezen", L"Wybierz VST2", L"VST2 seç"));
		CString h;
		h.Format(LL14(
			L"パート %d — VST2（.dll）だけ。ダブルクリックで決定",
			L"Part %d — VST2 (.dll) only. Double-click to choose",
			L"Partie %d — VST2 (.dll) seulement. Double-clic pour choisir",
			L"Parte %d — solo VST2 (.dll). Doppio clic per scegliere",
			L"Parte %d — solo VST2 (.dll). Doble clic para elegir",
			L"파트 %d — VST2(.dll)만. 더블클릭으로 결정",
			L"声部 %d — 仅 VST2（.dll）。双击确定",
			L"الجزء %d — VST2 ‏(.dll) فقط. نقرة مزدوجة للاختيار",
			L"Партия %d — только VST2 (.dll). Двойной щелчок",
			L"Part %d — nur VST2 (.dll). Doppelklick zum Wählen",
			L"Parte %d — só VST2 (.dll). Duplo clique para escolher",
			L"Deel %d — alleen VST2 (.dll). Dubbelklik om te kiezen",
			L"Partia %d — tylko VST2 (.dll). Podwójne kliknięcie",
			L"Parti %d — yalnız VST2 (.dll). Seçmek için çift tık"), m_part);
		m_hint.SetWindowText(h);
	} else {
		SetWindowText(LL14(L"VST音色を選択", L"Select VST", L"Choisir VST", L"Scegli VST", L"Elegir VST",
			L"VST 선택", L"选择 VST", L"اختر VST", L"Выбор VST", L"VST wählen",
			L"Escolher VST", L"VST kiezen", L"Wybierz VST", L"VST seç"));
		CString h;
		h.Format(LL14(
			L"パート %d — スキャン済み音源（ダブルクリックで決定）。HALion等の音色数0は正常（MediaBayは設定画面内）",
			L"Part %d — scanned instruments (double-click). HALion 0 programs is normal (MediaBay in editor)",
			L"Partie %d — instruments analysés (double-clic). 0 programme HALion est normal (MediaBay dans l'éditeur)",
			L"Parte %d — strumenti analizzati (doppio clic). 0 programmi HALion è normale (MediaBay nell'editor)",
			L"Parte %d — instrumentos analizados (doble clic). 0 programas HALion es normal (MediaBay en el editor)",
			L"파트 %d — 스캔된 음원(더블클릭). HALion 프로그램 0은 정상(MediaBay는 설정 화면)",
			L"声部 %d — 已扫描音源（双击）。HALion 音色数为 0 是正常的（MediaBay 在设置画面）",
			L"الجزء %d — آلات ممسوحة (نقرة مزدوجة). صفر برامج HALion طبيعي (MediaBay في المحرر)",
			L"Партия %d — просканированные инструменты (двойной щелчок). 0 программ HALion — норма (MediaBay в редакторе)",
			L"Part %d — gescannte Instrumente (Doppelklick). 0 HALion-Programme ist normal (MediaBay im Editor)",
			L"Parte %d — instrumentos analisados (duplo clique). 0 programas HALion é normal (MediaBay no editor)",
			L"Deel %d — gescande instrumenten (dubbelklik). 0 HALion-programma's is normaal (MediaBay in de editor)",
			L"Partia %d — zeskanowane instrumenty (podwójne kliknięcie). 0 programów HALion jest normalne (MediaBay w edytorze)",
			L"Parti %d — taranmış çalgılar (çift tık). HALion'da 0 program normaldir (MediaBay düzenleyicide)"), m_part);
		m_hint.SetWindowText(h);
	}
	m_ok.SetWindowText(LL14(L"決定", L"OK", L"OK", L"OK", L"Aceptar", L"결정", L"确定", L"موافق", L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	m_cancel.SetWindowText(LL14(L"取消", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
	m_rescan.SetWindowText(LL14(L"再スキャン", L"Rescan", L"Nouvelle analyse", L"Nuova scansione", L"Volver a explorar",
		L"다시 스캔", L"重新扫描", L"إعادة المسح", L"Сканировать снова", L"Neu scannen",
		L"Analisar de novo", L"Opnieuw scannen", L"Skanuj ponownie", L"Yeniden tara"));
}

void CSasamiVstPickDlg::RebuildList()
{
	m_count = 0;
	VstScanEnsure(m_hWnd);
	const int n = VstScanGetCount();
	for (int i = 0; i < n && m_count < 512; i++) {
		const VstPluginInfo* p = VstScanGet(i);
		if (!p) continue;
		if (m_effectsOnly) {
			if (p->isInstrument) continue;
		} else {
			if (!p->isInstrument) continue;
			if (m_vst2Only && p->isVst3) continue;
			if (p->isLiveOk == 0) continue; /* prefer verified */
		}
		m_idx[m_count++] = i;
	}
	/* if none verified, show all instruments (tone mode only) */
	if (!m_effectsOnly && m_count == 0) {
		for (int i = 0; i < n && m_count < 512; i++) {
			const VstPluginInfo* p = VstScanGet(i);
			if (!p || !p->isInstrument) continue;
			if (m_vst2Only && p->isVst3) continue;
			m_idx[m_count++] = i;
		}
	}
	if (m_sel >= m_count) m_sel = m_count - 1;
	if (m_sel < 0) m_sel = 0;
}

BOOL CSasamiVstPickDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	m_ok.SetAeroMode(FALSE);
	m_cancel.SetAeroMode(FALSE);
	m_rescan.SetAeroMode(FALSE);
	m_hint.SetAeroMode(FALSE);
	ApplyLang();
	RebuildList();
	LayoutChrome();
	CCC_BringDialogToForeground(this);
	if (!ScRestoreWndGeom(this, savedata.sasamiVstPickX, savedata.sasamiVstPickY,
		savedata.sasamiVstPickW, savedata.sasamiVstPickH, 420, 320))
		SetWindowPos(NULL, 0, 0, 520, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
	return TRUE;
}

void CSasamiVstPickDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	m_hint.MoveWindow(8, cap + 6, rc.Width() - 16, 48);
	const int rescanW = ScChromeButtonWidth(&m_rescan, 96, 140);
	const int cancelW = ScChromeButtonWidth(&m_cancel, 72, 120);
	const int okW = ScChromeButtonWidth(&m_ok, 56, 80);
	m_rescan.MoveWindow(8, rc.Height() - 34, rescanW, 26);
	m_cancel.MoveWindow(rc.Width() - 8 - cancelW, rc.Height() - 34, cancelW, 26);
	m_ok.MoveWindow(rc.Width() - 16 - cancelW - okW, rc.Height() - 34, okW, 26);
	m_listRc.SetRect(8, cap + 58, rc.Width() - 8, rc.Height() - 44);
	Invalidate(FALSE);
}

void CSasamiVstPickDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_ok.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiVstPickDlg::OnEraseBkgnd(CDC* pDC) { return CCustomBlurDialogExBase::OnEraseBkgnd(pDC); }

int CSasamiVstPickDlg::HitRow(CPoint pt) const
{
	if (!m_listRc.PtInRect(pt)) return -1;
	const int rowH = 22;
	int r = (pt.y - m_listRc.top) / rowH + m_scroll;
	if (r < 0 || r >= m_count) return -1;
	return r;
}

void CSasamiVstPickDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(rc.left, cap, rc.right, rc.bottom);
	CDC mem; mem.CreateCompatibleDC(&dc);
	CBitmap bmp; bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(248, 248, 252));
	CRect list = m_listRc; list.OffsetRect(-body.left, -body.top);
	mem.FillSolidRect(list, RGB(255, 255, 255));
	mem.Draw3dRect(list, RGB(160, 160, 170), RGB(160, 160, 170));
	mem.SetBkMode(TRANSPARENT);
	CFont* of = mem.SelectObject(GetFont());
	const int rowH = 22;
	int y = list.top + 2;
	for (int i = m_scroll; i < m_count && y + rowH < list.bottom; i++) {
		const VstPluginInfo* p = VstScanGet(m_idx[i]);
		if (!p) continue;
		CRect row(list.left + 2, y, list.right - 2, y + rowH);
		if (i == m_sel) mem.FillSolidRect(row, RGB(200, 215, 245));
		mem.SetTextColor(RGB(20, 20, 40));
		wchar_t line[600];
		_snwprintf_s(line, _TRUNCATE, L"%s  [%s %d]", p->name, p->isVst3 ? L"VST3" : L"VST2", p->arch);
		mem.DrawText(line, row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		y += rowH;
	}
	mem.SelectObject(of);
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiVstPickDlg::AcceptSel()
{
	if (m_sel < 0 || m_sel >= m_count) return;
	const VstPluginInfo* p = VstScanGet(m_idx[m_sel]);
	if (!p) return;
	wcsncpy_s(m_path, p->path, _TRUNCATE);
	m_isVst3 = p->isVst3;
	EndDialog(IDOK);
}

void CSasamiVstPickDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	int r = HitRow(point);
	if (r >= 0) { m_sel = r; InvalidateRect(m_listRc, FALSE); }
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiVstPickDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (HitRow(point) >= 0) AcceptSel();
	else CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CSasamiVstPickDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	m_scroll -= (zDelta / WHEEL_DELTA) * 3;
	if (m_scroll < 0) m_scroll = 0;
	if (m_scroll > m_count - 1) m_scroll = max(0, m_count - 1);
	InvalidateRect(m_listRc, FALSE);
	return TRUE;
}

void CSasamiVstPickDlg::OnBnClickedOk() {
	ScSaveWndGeom(this, &savedata.sasamiVstPickX, &savedata.sasamiVstPickY,
		&savedata.sasamiVstPickW, &savedata.sasamiVstPickH); AcceptSel(); }
void CSasamiVstPickDlg::OnBnClickedCancel() {
	ScSaveWndGeom(this, &savedata.sasamiVstPickX, &savedata.sasamiVstPickY,
		&savedata.sasamiVstPickW, &savedata.sasamiVstPickH); EndDialog(IDCANCEL); }

void CSasamiVstPickDlg::OnBnClickedRescan()
{
	/* Folder re-enumerate only. Do NOT call VstScanVerifyLiveList here — that
	   LoadPart-probes free slots and races a live HALion editor / MediaBay. */
	m_hint.SetWindowText(LL14(
		L"フォルダを再スキャン中…（検証ロードなし・再生中スロットは触らない）",
		L"Rescanning folders… (no verify-load; live slots untouched)",
		L"Rescan dossiers…", L"Rescan cartelle…", L"Reescaneando…",
		L"폴더 재스캔…", L"正在重新扫描…", L"Rescan…", L"Rescan…", L"Ordner neu…",
		L"A reexaminar…", L"Mappen opnieuw…", L"Ponowne skanowanie…", L"Yeniden tarama…"));
	VstScanInvalidate();
	VstScanEnsure(m_hWnd);
	RebuildList();
	ApplyLang();
	CString h;
	h.Format(LL14(L"再スキャン完了 — %d 件（HALionの音色0は正常）",
		L"Rescan done — %d instruments (HALion 0 programs is normal)",
		L"Rescan OK — %d", L"Rescan OK — %d", L"Rescan OK — %d",
		L"재스캔 완료 — %d", L"完成 — %d", L"%d", L"%d", L"Fertig — %d",
		L"OK — %d", L"Klaar — %d", L"Gotowe — %d", L"Bitti — %d"), m_count);
	m_hint.SetWindowText(h);
	InvalidateRect(m_listRc, FALSE);
}
