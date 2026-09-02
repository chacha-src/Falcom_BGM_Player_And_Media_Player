#include "stdafx.h"
#include "ogg.h"
#include "CSasamiLayoutPaletteDlg.h"
#include "CSasamiStaffCore.h"

extern void MpPersistSavedataQuick();

CSasamiLayoutPaletteDlg* CSasamiLayoutPaletteDlg::s_inst = NULL;

CSasamiLayoutPaletteDlg* CSasamiLayoutPaletteDlg::Instance()
{
	return (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) ? s_inst : NULL;
}

IMPLEMENT_DYNAMIC(CSasamiLayoutPaletteDlg, CCustomBlurDialogExBase)

CSasamiLayoutPaletteDlg::CSasamiLayoutPaletteDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_LAYOUT_PAL, pParent)
	, m_tab(0)
	, m_itemCount(0)
	, m_hoverItem(-1)
	, m_hoverSemi(0)
	, m_trScope(0)
	, m_trackMouse(0)
	, m_notify(NULL)
{
	memset(&m_st, 0, sizeof(m_st));
	m_st.previewNote = -1;
}

CSasamiLayoutPaletteDlg* CSasamiLayoutPaletteDlg::OpenNear(CWnd* owner, CPoint screenPt)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : s_inst->m_notify;
		int pw = savedata.sasamiLayPalW, ph = savedata.sasamiLayPalH;
		if (pw < 300 || ph < 360 || pw > 1200 || ph > 1200) { pw = 340; ph = 400; }
		const int havePos = (savedata.sasamiLayPalX > -30000 && savedata.sasamiLayPalY > -30000
			&& !(savedata.sasamiLayPalX == -1 && savedata.sasamiLayPalY == -1));
		if (havePos)
			s_inst->SetWindowPos(NULL, savedata.sasamiLayPalX, savedata.sasamiLayPalY, pw, ph, SWP_NOZORDER | SWP_NOACTIVATE);
		s_inst->ShowWindow(SW_SHOW);
		s_inst->Invalidate(FALSE);
		s_inst->BringWindowToTop();
		(void)screenPt;
		return s_inst;
	}
	s_inst = new CSasamiLayoutPaletteDlg(owner);
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	if (!s_inst->Create(IDD_SASAMI_LAYOUT_PAL, owner)) {
		delete s_inst; s_inst = NULL; return NULL;
	}
	{
		int pw = savedata.sasamiLayPalW, ph = savedata.sasamiLayPalH;
		if (pw < 300 || ph < 360 || pw > 1200 || ph > 1200) { pw = 340; ph = 400; }
		const int havePos = (savedata.sasamiLayPalX > -30000 && savedata.sasamiLayPalY > -30000
			&& !(savedata.sasamiLayPalX == -1 && savedata.sasamiLayPalY == -1));
		if (!havePos)
			s_inst->SetWindowPos(NULL, screenPt.x, screenPt.y, pw, ph, SWP_NOZORDER);
		else
			s_inst->SetWindowPos(NULL, savedata.sasamiLayPalX, savedata.sasamiLayPalY, pw, ph, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	s_inst->ShowWindow(SW_SHOW);
	s_inst->BringWindowToTop();
	return s_inst;
}

void CSasamiLayoutPaletteDlg::PostCmd(int cmdId)
{
	if (!m_notify) return;
	::PostMessage(m_notify, WM_SASAMI_PAL_DUR, 0, (LPARAM)(SASAMI_PAL_CMD | (cmdId & 0xFF)));
}

void CSasamiLayoutPaletteDlg::PostLayout(int kind, int a, int b)
{
	if (!m_notify) return;
	::PostMessage(m_notify, WM_SASAMI_PAL_LAYOUT, (WPARAM)a,
		(LPARAM)((kind & 0xFF) | ((b & 0xFFFF) << 16)));
}

void CSasamiLayoutPaletteDlg::QueryState()
{
	memset(&m_st, 0, sizeof(m_st));
	m_st.previewNote = -1;
	m_st.meterN = 4;
	m_st.meterD = 4;
	if (m_notify)
		::SendMessage(m_notify, WM_SASAMI_PAL_QUERY_STATE, 0, (LPARAM)&m_st);
}

static const wchar_t* kTabLabel(int tab)
{
	switch (tab) {
	case 0: return LL14(L"調", L"Key", L"Armure", L"Tonalità", L"Armadura", L"조", L"调", L"مفتاح", L"Тон", L"Ton", L"Tom", L"Sleutel", L"Ton", L"Ton");
	case 1: return LL14(L"拍", L"Meter", L"Mesure", L"Misura", L"Compás", L"박", L"拍", L"إيقاع", L"Размер", L"Takt", L"Compasso", L"Maat", L"Metr", L"Ölçü");
	case 2: return LL14(L"譜表", L"Clef", L"Clé", L"Chiave", L"Clave", L"보표", L"谱表", L"مفتاح", L"Ключ", L"Schlüssel", L"Clave", L"Sleutel", L"Klucz", L"Anahtar");
	default: return LL14(L"移調", L"Trans.", L"Transp.", L"Tras.", L"Trans.", L"이조", L"移调", L"نقل", L"Транс.", L"Trans.", L"Trans.", L"Trans.", L"Trans.", L"Trans.");
	}
}

void CSasamiLayoutPaletteDlg::AddItem(int kind, int value, int cmdId, const wchar_t* label)
{
	if (m_itemCount >= kItemMax) return;
	LayItem& it = m_items[m_itemCount++];
	it.kind = kind;
	it.value = value;
	it.cmdId = cmdId;
	wcsncpy_s(it.label, label ? label : L"", _TRUNCATE);
}

void CSasamiLayoutPaletteDlg::BuildTabItems()
{
	m_itemCount = 0;
	switch (m_tab) {
	case 0: {
		static const struct { int ks; const wchar_t* lab; } majL[] = {
			{ -1, L"F" }, { -2, L"Bb" }, { -3, L"Eb" }, { -4, L"Ab" }, { -5, L"Db" }, { -6, L"Gb" }, { -7, L"Cb" }
		};
		static const struct { int ks; const wchar_t* lab; } majR[] = {
			{ 1, L"G" }, { 2, L"D" }, { 3, L"A" }, { 4, L"E" }, { 5, L"B" }, { 6, L"F#" }, { 7, L"C#" }
		};
		static const struct { int ks; const wchar_t* lab; } minL[] = {
			{ -1, L"D" }, { -2, L"G" }, { -3, L"C" }, { -4, L"F" }, { -5, L"Bb" }, { -6, L"Eb" }, { -7, L"Ab" }
		};
		static const struct { int ks; const wchar_t* lab; } minR[] = {
			{ 1, L"E" }, { 2, L"B" }, { 3, L"F#" }, { 4, L"C#" }, { 5, L"G#" }, { 6, L"D#" }, { 7, L"A#" }
		};
		AddItem(LAY_ITEM_KEY, 0, SASAMI_PAL_CMD_KEY_BASE + 7, L"C");
		for (int i = 0; i < 7; i++)
			AddItem(LAY_ITEM_KEY, majL[i].ks, SASAMI_PAL_CMD_KEY_BASE + majL[i].ks + 7, majL[i].lab);
		for (int i = 0; i < 7; i++)
			AddItem(LAY_ITEM_KEY, majR[i].ks, SASAMI_PAL_CMD_KEY_BASE + majR[i].ks + 7, majR[i].lab);
		AddItem(LAY_ITEM_KEY, 0, SASAMI_PAL_CMD_KEY_BASE + 7, L"A");
		for (int i = 0; i < 7; i++)
			AddItem(LAY_ITEM_KEY, minL[i].ks, SASAMI_PAL_CMD_KEY_BASE + minL[i].ks + 7, minL[i].lab);
		for (int i = 0; i < 7; i++)
			AddItem(LAY_ITEM_KEY, minR[i].ks, SASAMI_PAL_CMD_KEY_BASE + minR[i].ks + 7, minR[i].lab);
		break;
	}
	case 1:
		for (int n = 1; n <= 16; n++) {
			wchar_t lab[8]; _snwprintf_s(lab, _TRUNCATE, L"%d", n);
			AddItem(LAY_ITEM_METER_N, n, 0, lab);
		}
		for (int d : { 2, 4, 8, 16 }) {
			wchar_t lab[8]; _snwprintf_s(lab, _TRUNCATE, L"%d", d);
			AddItem(LAY_ITEM_METER_D, d, 0, lab);
		}
		AddItem(LAY_ITEM_ACTION, 0, SASAMI_PAL_CMD_METER_DEL,
			LL14(L"拍削除", L"Delete", L"Suppr.", L"Rimuovi", L"Quitar", L"삭제", L"删拍号", L"حذف", L"Удал.", L"Löschen", L"Rem.", L"Verw.", L"Usuń", L"Sil"));
		break;
	case 2:
		AddItem(LAY_ITEM_CLEF, 0, SASAMI_PAL_CMD_CLEF_G, LL14(L"ト音記号 (G)", L"Treble (G)", L"Clé de Sol", L"Chiave di Sol", L"Clave de Sol",
			L"높은음자리", L"高音谱号", L"Sol", L"Скрипичный", L"Violinschl.", L"Clave de Sol", L"Violinsleutel", L"Klucz wiolinowy", L"Sol"));
		AddItem(LAY_ITEM_CLEF, 1, SASAMI_PAL_CMD_CLEF_F, LL14(L"ヘ音記号 (F)", L"Bass (F)", L"Clé de Fa", L"Chiave di Fa", L"Clave de Fa",
			L"낮은음자리", L"低音谱号", L"Fa", L"Басовый", L"Basschl.", L"Clave de Fa", L"Bassleutel", L"Klucz basowy", L"Fa"));
		AddItem(LAY_ITEM_CLEF, 2, SASAMI_PAL_CMD_CLEF_GF, LL14(L"大譜表 (G+F)", L"Grand staff", L"Grande portée", L"Grand staff", L"Grand staff",
			L"대보표", L"大谱表", L"Grand", L"Две строчки", L"Grand", L"Grand staff", L"Grand staff", L"Grand staff", L"Grand"));
		AddItem(LAY_ITEM_CLEF, 3, SASAMI_PAL_CMD_CLEF_DR, LL14(L"ドラム譜 (Dr)", L"Drum (Dr)", L"Batterie", L"Batteria", L"Batería",
			L"드럼", L"鼓谱", L"Drums", L"Ударные", L"Schlagzeug", L"Bateria", L"Drums", L"Perkusja", L"Davul"));
		break;
	default:
		AddItem(LAY_ITEM_TR_SCOPE, 0, 0, LL14(L"選択範囲", L"Selection", L"Sélection", L"Selezione", L"Selección",
			L"선택", L"选区", L"تحديد", L"Выделение", L"Auswahl", L"Seleção", L"Selectie", L"Zaznaczenie", L"Seçim"));
		{
			wchar_t partLab[24];
			_snwprintf_s(partLab, _TRUNCATE, LL14(L"パート Ch%d", L"Part Ch%d", L"Partie Ch%d", L"Parte Ch%d", L"Parte Ch%d",
				L"파트 Ch%d", L"声部 Ch%d", L"Part Ch%d", L"Партия Ch%d", L"Part Ch%d", L"Parte Ch%d", L"Partij Ch%d", L"Partia Ch%d", L"Parti Ch%d"),
				m_st.curCh + 1);
			AddItem(LAY_ITEM_TR_SCOPE, 1, 0, partLab);
		}
		AddItem(LAY_ITEM_TR_SCOPE, 2, 0, LL14(L"全パート", L"All parts", L"Toutes", L"Tutte", L"Todas",
			L"전체", L"全部", L"الكل", L"Все", L"Alle", L"Tudo", L"Alles", L"Wszystko", L"Tümü"));
		AddItem(LAY_ITEM_TR_ACT, 1, SASAMI_PAL_CMD_TR_PLUS, LL14(L"+1 半音", L"+1 semitone", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1", L"+1"));
		AddItem(LAY_ITEM_TR_ACT, -1, SASAMI_PAL_CMD_TR_MINUS, LL14(L"-1 半音", L"-1 semitone", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1", L"-1"));
		AddItem(LAY_ITEM_TR_ACT, 12, SASAMI_PAL_CMD_TR_SEL_P12, LL14(L"+12 (8va)", L"+12 (octave)", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12", L"+12"));
		AddItem(LAY_ITEM_TR_ACT, -12, SASAMI_PAL_CMD_TR_SEL_M12, LL14(L"-12 (8vb)", L"-12 (octave)", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12", L"-12"));
		break;
	}
}

void CSasamiLayoutPaletteDlg::LayoutKeyGrid(const CRect& area)
{
	if (m_itemCount < 30) return;
	const int mid = area.left + area.Width() / 2;
	m_keyMajorRc = CRect(area.left, area.top, mid - 2, area.bottom);
	m_keyMinorRc = CRect(mid + 2, area.top, area.right, area.bottom);
	const int rowH = 20;
	const int topY = area.top + 22;
	auto placeGroup = [&](const CRect& gr, int baseIdx) {
		const int colW = (gr.Width() - 8) / 2;
		const int lx = gr.left + 4;
		const int rx = gr.left + 4 + colW;
		m_items[baseIdx + 0].rc.SetRect(lx + colW / 2 - 24, gr.top + 2, lx + colW / 2 + 24, gr.top + 18);
		for (int i = 0; i < 7; i++)
			m_items[baseIdx + 1 + i].rc.SetRect(lx, topY + i * rowH, lx + colW - 2, topY + (i + 1) * rowH - 1);
		for (int i = 0; i < 7; i++)
			m_items[baseIdx + 8 + i].rc.SetRect(rx, topY + i * rowH, gr.right - 4, topY + (i + 1) * rowH - 1);
	};
	placeGroup(m_keyMajorRc, 0);
	placeGroup(m_keyMinorRc, 15);
}

void CSasamiLayoutPaletteDlg::LayoutMeterGrid(const CRect& area)
{
	int idxAct = -1;
	for (int i = 0; i < m_itemCount; i++)
		if (m_items[i].kind == LAY_ITEM_ACTION) idxAct = i;
	const int headH = 16;
	int y = area.top;
	const int cols = 4;
	const int cellW = max(36, area.Width() / cols);
	const int cellH = 22;
	int nIdx = 0;
	for (int i = 0; i < m_itemCount && nIdx < 16; i++) {
		if (m_items[i].kind != LAY_ITEM_METER_N) continue;
		const int c = nIdx % cols, r = nIdx / cols;
		m_items[i].rc.SetRect(area.left + c * cellW, y + headH + r * cellH,
			area.left + (c + 1) * cellW - 2, y + headH + (r + 1) * cellH - 1);
		nIdx++;
	}
	y += headH + 4 * cellH + 4;
	const int dCellW = max(48, area.Width() / 4);
	int dIdx = 0;
	for (int i = 0; i < m_itemCount; i++) {
		if (m_items[i].kind != LAY_ITEM_METER_D) continue;
		m_items[i].rc.SetRect(area.left + dIdx * dCellW, y + headH,
			area.left + (dIdx + 1) * dCellW - 2, y + headH + cellH);
		dIdx++;
	}
	if (idxAct >= 0)
		m_items[idxAct].rc.SetRect(area.left, area.bottom - cellH - 2, area.right, area.bottom - 2);
}

void CSasamiLayoutPaletteDlg::LayoutRowList(const CRect& area)
{
	int y = area.top;
	const int rowH = 22;
	for (int i = 0; i < m_itemCount; i++) {
		m_items[i].rc.SetRect(area.left, y, area.right, y + rowH);
		y += rowH + 1;
	}
}

int CSasamiLayoutPaletteDlg::ItemSelected(const LayItem& it) const
{
	switch (it.kind) {
	case LAY_ITEM_KEY: return it.value == m_st.keySig;
	case LAY_ITEM_METER_N: return it.value == m_st.meterN;
	case LAY_ITEM_METER_D: return it.value == m_st.meterD;
	case LAY_ITEM_CLEF: return it.value == m_st.clef;
	case LAY_ITEM_TR_SCOPE: return it.value == m_trScope;
	default: return 0;
	}
}

int CSasamiLayoutPaletteDlg::HitTab(CPoint pt) const
{
	for (int t = 0; t < kTabCount; t++)
		if (m_tabs[t].PtInRect(pt)) return t;
	return -1;
}

int CSasamiLayoutPaletteDlg::HitItem(CPoint pt) const
{
	for (int i = 0; i < m_itemCount; i++)
		if (!m_items[i].rc.IsRectEmpty() && m_items[i].rc.PtInRect(pt)) return i;
	return -1;
}

static void ScPalNoteNameShort(int midi, wchar_t* buf, int cch)
{
	static const wchar_t* names[] = { L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B" };
	if (!buf || cch <= 0) return;
	if (midi < 0 || midi > 127) { wcsncpy_s(buf, (size_t)cch, L"—", _TRUNCATE); return; }
	_snwprintf_s(buf, cch, _TRUNCATE, L"%s%d", names[midi % 12], midi / 12 - 1);
}

void CSasamiLayoutPaletteDlg::DrawHeader(CDC& dc, const CRect& rc, const wchar_t* text) const
{
	dc.FillSolidRect(rc, RGB(24, 28, 40));
	dc.SetBkMode(TRANSPARENT);
	CFont f; f.CreateFont(12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
	CFont* of = dc.SelectObject(&f);
	dc.SetTextColor(RGB(160, 175, 200));
	CRect tr = rc; tr.left += 6;
	dc.DrawText(text ? text : L"", tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	dc.SelectObject(of);
}

void CSasamiLayoutPaletteDlg::DrawKeyGroups(CDC& dc, const CRect& area) const
{
	(void)area;
	dc.Draw3dRect(m_keyMajorRc, RGB(90, 100, 130), RGB(40, 45, 60));
	dc.Draw3dRect(m_keyMinorRc, RGB(90, 100, 130), RGB(40, 45, 60));
	dc.SetBkMode(TRANSPARENT);
	CFont f; f.CreateFont(11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
	CFont* of = dc.SelectObject(&f);
	dc.SetTextColor(RGB(180, 190, 210));
	CRect tl = m_keyMajorRc; tl.bottom = tl.top + 16; dc.DrawText(L"Major", tl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	CRect tr = m_keyMinorRc; tr.bottom = tr.top + 16; dc.DrawText(L"Minor", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc.SelectObject(of);
}

void CSasamiLayoutPaletteDlg::DrawPreview(CDC& dc, const CRect& rc) const
{
	if (rc.IsRectEmpty()) return;
	if (m_tab == 3) {
		dc.FillSolidRect(rc, RGB(28, 32, 48));
		dc.Draw3dRect(rc, RGB(70, 85, 120), RGB(15, 18, 30));
		dc.SetBkMode(TRANSPARENT);
		CFont f; f.CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
		CFont* of = dc.SelectObject(&f);
		wchar_t line1[128], line2[128];
		_snwprintf_s(line1, _TRUNCATE, LL14(L"赤バー: %d/%d  %s  Ch%d=%s",
			L"Red bar: %d/%d  %s  Ch%d=%s", L"Barre: %d/%d  %s  Ch%d=%s", L"Battuta: %d/%d  %s  Ch%d=%s",
			L"Barra: %d/%d  %s  Ch%d=%s", L"빨간줄: %d/%d  %s  Ch%d=%s", L"红条: %d/%d  %s  Ch%d=%s",
			L"%d/%d  %s  Ch%d=%s", L"Красная: %d/%d  %s  Ch%d=%s", L"%d/%d  %s  Ch%d=%s",
			L"%d/%d  %s  Ch%d=%s", L"%d/%d  %s  Ch%d=%s", L"%d/%d  %s  Ch%d=%s", L"%d/%d  %s  Ch%d=%s"),
			m_st.meterN, m_st.meterD, ScStaffKeySigMajorName(m_st.keySig), m_st.curCh + 1,
			(m_st.clef == 3) ? L"Dr" : (m_st.clef == 2) ? L"G+F" : (m_st.clef == 1 ? L"F" : L"G"));
		line2[0] = 0;
		if (m_hoverItem >= 0 && m_hoverSemi != 0) {
			wchar_t n0[16], n1[16];
			if (m_trScope == 0 && m_st.nSel > 0 && m_st.previewNote >= 0) {
				ScPalNoteNameShort(m_st.previewNote, n0, 16);
				ScPalNoteNameShort(min(127, max(0, m_st.previewNote + m_hoverSemi)), n1, 16);
				_snwprintf_s(line2, _TRUNCATE, LL14(L"プレビュー: %d音 %s → %s (%+d)",
					L"Preview: %d notes %s → %s (%+d)", L"Aperçu: %d %s → %s (%+d)", L"Anteprima: %d %s → %s (%+d)",
					L"Vista: %d %s → %s (%+d)", L"미리보기: %d %s → %s (%+d)", L"预览: %d %s → %s (%+d)",
					L"%d %s → %s (%+d)", L"Просмотр: %d %s → %s (%+d)", L"Vorschau: %d %s → %s (%+d)",
					L"Pré-visual: %d %s → %s (%+d)", L"Voorbeeld: %d %s → %s (%+d)", L"Podgląd: %d %s → %s (%+d)", L"Önizleme: %d %s → %s (%+d)"),
					m_st.nSel, n0, n1, m_hoverSemi);
			} else {
				_snwprintf_s(line2, _TRUNCATE, LL14(L"プレビュー: %+d 半音", L"Preview: %+d semitones", L"Aperçu: %+d", L"Anteprima: %+d",
					L"Vista: %+d", L"미리보기: %+d", L"预览: %+d", L"%+d", L"Просмотр: %+d", L"Vorschau: %+d",
					L"Pré-visual: %+d", L"Voorbeeld: %+d", L"Podgląd: %+d", L"Önizleme: %+d"), m_hoverSemi);
			}
		} else {
			wcsncpy_s(line2, LL14(L"クリックで赤バー位置の小節に適用", L"Click to apply at red-bar measure", L"Cliquer pour appliquer",
				L"Clic per applicare", L"Clic para aplicar", L"클릭하여 적용", L"点击应用到红条小节", L"انقر للتطبيق",
				L"Щёлкните для применения", L"Klicken zum Anwenden", L"Clic para aplicar", L"Klik om toe te passen", L"Kliknij aby zastosować", L"Uygulamak için tıkla"), _TRUNCATE);
		}
		dc.SetTextColor(RGB(210, 220, 240));
		CRect r1 = rc; r1.DeflateRect(8, 4, 8, 0); r1.bottom = r1.top + rc.Height() / 2;
		dc.DrawText(line1, r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (line2[0]) {
			CRect r2 = rc; r2.DeflateRect(8, 0, 8, 4); r2.top = r1.bottom;
			dc.SetTextColor(m_hoverItem >= 0 ? RGB(255, 230, 140) : RGB(170, 185, 210));
			dc.DrawText(line2, r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		dc.SelectObject(of);
		return;
	}
	int clef = m_st.clef, keySig = m_st.keySig, meterN = m_st.meterN, meterD = m_st.meterD;
	if (m_hoverItem >= 0 && m_hoverItem < m_itemCount) {
		const LayItem& hi = m_items[m_hoverItem];
		if (m_tab == 0 && hi.kind == LAY_ITEM_KEY) keySig = hi.value;
		else if (m_tab == 1 && hi.kind == LAY_ITEM_METER_N) meterN = hi.value;
		else if (m_tab == 1 && hi.kind == LAY_ITEM_METER_D) meterD = hi.value;
		else if (m_tab == 2 && hi.kind == LAY_ITEM_CLEF) clef = hi.value;
	}
	ScStaffDrawLayoutPreview(dc, rc, clef, keySig, meterN, meterD);
}

const wchar_t* CSasamiLayoutPaletteDlg::ItemTip(int i) const
{
	if (i < 0 || i >= m_itemCount) return L"";
	return m_items[i].label;
}

void CSasamiLayoutPaletteDlg::SetupItemTips()
{
	if (m_tip.GetSafeHwnd()) m_tip.DestroyWindow();
	if (!m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) return;
	m_tip.Activate(TRUE);
	m_tip.SetMaxTipWidth(320);
	for (int i = 0; i < m_itemCount; i++) {
		if (m_items[i].rc.IsRectEmpty()) continue;
		TOOLINFO ti = {}; ti.cbSize = sizeof(ti); ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = m_hWnd; ti.uId = (UINT_PTR)(i + 1); ti.rect = m_items[i].rc;
		ti.lpszText = LPSTR_TEXTCALLBACK;
		m_tip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}
}

BOOL CSasamiLayoutPaletteDlg::OnTtnNeedText(UINT, NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!pNMHDR || !pResult) return FALSE;
	*pResult = 0;
	NMTTDISPINFOW* di = (NMTTDISPINFOW*)pNMHDR;
	di->lpszText = (LPWSTR)ItemTip((int)di->hdr.idFrom - 1);
	di->hinst = NULL;
	return TRUE;
}

BOOL CSasamiLayoutPaletteDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

BEGIN_MESSAGE_MAP(CSasamiLayoutPaletteDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT() ON_WM_ERASEBKGND() ON_WM_LBUTTONDOWN() ON_WM_MOUSEMOVE() ON_WM_MOUSELEAVE()
	ON_WM_SIZE() ON_WM_MOVE() ON_WM_CLOSE() ON_WM_DESTROY()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
END_MESSAGE_MAP()

void CSasamiLayoutPaletteDlg::ApplyLang()
{
	SetWindowText(LL14(L"譜表", L"Layout", L"Armature", L"Armatura", L"Armadura",
		L"보표", L"谱表", L"تخطيط", L"Разметка", L"Layout", L"Layout", L"Layout", L"Układ", L"Düzen"));
}

BOOL CSasamiLayoutPaletteDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	ApplyLang();
	BuildTabItems();
	LayoutChrome();
	SetupItemTips();
	return TRUE;
}

void CSasamiLayoutPaletteDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 4, tabH = 24;
	const int tabW = max(48, (rc.Width() - pad * 2) / kTabCount);
	for (int t = 0; t < kTabCount; t++)
		m_tabs[t].SetRect(pad + t * tabW, cap + pad, pad + (t + 1) * tabW - 1, cap + pad + tabH);
	const int contentTop = cap + pad + tabH + 2;
	m_contentRc = CRect(pad, contentTop, rc.right - pad, rc.bottom - pad);
	const int prevH = (m_tab == 3) ? 56 : 72;
	m_previewRc = CRect(m_contentRc.left, m_contentRc.top, m_contentRc.right, m_contentRc.top + prevH);
	CRect body = m_contentRc; body.top = m_previewRc.bottom + 2;
	for (int i = 0; i < kItemMax; i++) m_items[i].rc.SetRectEmpty();
	m_keyMajorRc.SetRectEmpty(); m_keyMinorRc.SetRectEmpty();
	BuildTabItems();
	if (m_tab == 0) LayoutKeyGrid(body);
	else if (m_tab == 1) LayoutMeterGrid(body);
	else LayoutRowList(body);
	if (m_tip.GetSafeHwnd()) SetupItemTips();
}

void CSasamiLayoutPaletteDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
	if (GetSafeHwnd() && IsWindowVisible()) {
		ScSaveWndGeom(this, &savedata.sasamiLayPalX, &savedata.sasamiLayPalY,
			&savedata.sasamiLayPalW, &savedata.sasamiLayPalH);
		MpPersistSavedataQuick();
	}
}

void CSasamiLayoutPaletteDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	LayoutChrome();
	if (GetSafeHwnd() && IsWindowVisible()) {
		ScSaveWndGeom(this, &savedata.sasamiLayPalX, &savedata.sasamiLayPalY,
			&savedata.sasamiLayPalW, &savedata.sasamiLayPalH);
		MpPersistSavedataQuick();
	}
	Invalidate(FALSE);
}

BOOL CSasamiLayoutPaletteDlg::OnEraseBkgnd(CDC* pDC) { return CCustomBlurDialogExBase::OnEraseBkgnd(pDC); }

static int TrCmdForScope(int scope, int semi)
{
	if (scope == 1) return (semi == 1) ? SASAMI_PAL_CMD_TR_PART_PLUS : (semi == -1) ? SASAMI_PAL_CMD_TR_PART_MINUS : 0;
	if (scope == 2) return (semi == 1) ? SASAMI_PAL_CMD_TR_ALL_PLUS : (semi == -1) ? SASAMI_PAL_CMD_TR_ALL_MINUS : 0;
	if (semi == 1) return SASAMI_PAL_CMD_TR_PLUS;
	if (semi == -1) return SASAMI_PAL_CMD_TR_MINUS;
	if (semi == 12) return SASAMI_PAL_CMD_TR_SEL_P12;
	if (semi == -12) return SASAMI_PAL_CMD_TR_SEL_M12;
	return 0;
}

void CSasamiLayoutPaletteDlg::ActivateItem(int i)
{
	if (i < 0 || i >= m_itemCount) return;
	const LayItem& it = m_items[i];
	switch (it.kind) {
	case LAY_ITEM_KEY: PostCmd(it.cmdId); break;
	case LAY_ITEM_METER_N: PostLayout(SASAMI_PAL_LAYOUT_METER, it.value, m_st.meterD); break;
	case LAY_ITEM_METER_D: PostLayout(SASAMI_PAL_LAYOUT_METER, m_st.meterN, it.value); break;
	case LAY_ITEM_CLEF: PostCmd(it.cmdId); break;
	case LAY_ITEM_TR_SCOPE: m_trScope = it.value; Invalidate(FALSE); break;
	case LAY_ITEM_TR_ACT: { const int cmd = TrCmdForScope(m_trScope, it.value); if (cmd) PostCmd(cmd); break; }
	case LAY_ITEM_ACTION: PostCmd(it.cmdId); break;
	default: break;
	}
}

void CSasamiLayoutPaletteDlg::OnPaint()
{
	QueryState();
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(rc.left, cap, rc.right, rc.bottom);
	CDC mem; mem.CreateCompatibleDC(&dc);
	CBitmap bmp; bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(38, 42, 58));
	for (int t = 0; t < kTabCount; t++) {
		CRect tr = m_tabs[t]; tr.OffsetRect(-body.left, -body.top);
		ScStaffDrawPalTab(mem, tr, kTabLabel(t), t == m_tab);
	}
	CRect prev = m_previewRc; prev.OffsetRect(-body.left, -body.top);
	DrawPreview(mem, prev);
	if (m_tab == 0) {
		CRect km = m_keyMajorRc, kn = m_keyMinorRc;
		km.OffsetRect(-body.left, -body.top); kn.OffsetRect(-body.left, -body.top);
		DrawKeyGroups(mem, CRect(km.left, km.top, kn.right, kn.bottom));
	}
	if (m_tab == 1) {
		CRect ar = m_contentRc; ar.top = m_previewRc.bottom + 2; ar.OffsetRect(-body.left, -body.top);
		CRect h1(ar.left, ar.top, ar.right, ar.top + 16);
		CRect h2(ar.left, ar.top + 16 + 4 * 22 + 4, ar.right, ar.top + 16 + 4 * 22 + 20);
		DrawHeader(mem, h1, LL14(L"分子", L"Numerator", L"Numérateur", L"Numeratore", L"Numerador",
			L"분자", L"分子", L"البسط", L"Числитель", L"Zähler", L"Numeratore", L"Teller", L"Licznik", L"Pay"));
		DrawHeader(mem, h2, LL14(L"分母", L"Denominator", L"Dénominateur", L"Denominatore", L"Denominador",
			L"분모", L"分母", L"المقام", L"Знаменатель", L"Nenner", L"Denominatore", L"Noemer", L"Mianownik", L"Payda"));
	}
	for (int i = 0; i < m_itemCount; i++) {
		CRect r = m_items[i].rc; if (r.IsRectEmpty()) continue;
		r.OffsetRect(-body.left, -body.top);
		const int sel = ItemSelected(m_items[i]);
		const int hot = (i == m_hoverItem);
		if (m_items[i].kind == LAY_ITEM_TR_ACT || m_items[i].kind == LAY_ITEM_ACTION) {
			ScStaffDrawPalCell(mem, r, hot);
			mem.SetBkMode(TRANSPARENT);
			CFont f; f.CreateFont(max(11, r.Height() - 8), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
			CFont* of = mem.SelectObject(&f);
			mem.SetTextColor(RGB(245, 248, 255));
			mem.DrawText(m_items[i].label, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			mem.SelectObject(of);
		} else {
			ScStaffDrawPalRadioRow(mem, r, m_items[i].label, sel, hot);
		}
	}
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiLayoutPaletteDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int tab = HitTab(point);
	if (tab >= 0 && tab != m_tab) {
		m_tab = tab; m_hoverItem = -1; m_hoverSemi = 0;
		LayoutChrome(); Invalidate(FALSE);
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	const int item = HitItem(point);
	if (item >= 0) ActivateItem(item);
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiLayoutPaletteDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_trackMouse) {
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		if (TrackMouseEvent(&tme)) m_trackMouse = 1;
	}
	int hover = HitItem(point), semi = 0;
	if (hover >= 0 && m_items[hover].kind == LAY_ITEM_TR_ACT) semi = m_items[hover].value;
	if (hover != m_hoverItem || semi != m_hoverSemi) {
		m_hoverItem = hover; m_hoverSemi = semi;
		InvalidateRect(m_previewRc, FALSE);
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CSasamiLayoutPaletteDlg::OnMouseLeave()
{
	m_trackMouse = 0;
	if (m_hoverItem >= 0) { m_hoverItem = -1; m_hoverSemi = 0; InvalidateRect(m_previewRc, FALSE); }
	CCustomBlurDialogExBase::OnMouseLeave();
}

void CSasamiLayoutPaletteDlg::OnClose()
{
	ScSaveWndGeom(this, &savedata.sasamiLayPalX, &savedata.sasamiLayPalY,
		&savedata.sasamiLayPalW, &savedata.sasamiLayPalH);
	MpPersistSavedataQuick();
	CCustomBlurDialogExBase::OnClose();
}

void CSasamiLayoutPaletteDlg::OnDestroy()
{
	ScSaveWndGeom(this, &savedata.sasamiLayPalX, &savedata.sasamiLayPalY,
		&savedata.sasamiLayPalW, &savedata.sasamiLayPalH);
	MpPersistSavedataQuick();
	CCustomBlurDialogExBase::OnDestroy();
}

void CSasamiLayoutPaletteDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (s_inst == this) s_inst = NULL;
	delete this;
}
