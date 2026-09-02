#include "stdafx.h"
#include "ogg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiStaffCore.h"
#include "SasamiComposerDoc.h"

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::s_inst = NULL;

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::Instance()
{
	return (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) ? s_inst : NULL;
}

IMPLEMENT_DYNAMIC(CSasamiNotePaletteDlg, CCustomBlurDialogExBase)

CSasamiNotePaletteDlg::CSasamiNotePaletteDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_NOTE_PAL, pParent)
	, m_baseDur(SC_PPQN), m_durTicks(SC_PPQN), m_rest(0), m_accidental(0)
	, m_tuplet(0), m_dotted(0), m_markStack(0), m_notify(NULL)
{
}

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::OpenNear(CWnd* owner, CPoint screenPt)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : s_inst->m_notify;
		s_inst->m_markStack = savedata.sasamiMarkStack ? 1 : 0;
		/* Already open: keep saved/user position (do not jump to cursor). */
		s_inst->ShowWindow(SW_SHOW);
		s_inst->Invalidate(FALSE);
		s_inst->BringWindowToTop();
		(void)screenPt;
		return s_inst;
	}
	s_inst = new CSasamiNotePaletteDlg(owner);
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	if (!s_inst->Create(IDD_SASAMI_NOTE_PAL, owner)) {
		delete s_inst; s_inst = NULL; return NULL;
	}
	{
		int pw = savedata.sasamiNotePalW, ph = savedata.sasamiNotePalH;
		if (pw < 280 || ph < 320 || pw > 1200 || ph > 1200) { pw = 380; ph = 480; }
		/* Prefer last saved screen position; fall back to cursor/owner anchor. */
		const int havePos = (savedata.sasamiNotePalX > -30000 && savedata.sasamiNotePalY > -30000
			&& !(savedata.sasamiNotePalX == -1 && savedata.sasamiNotePalY == -1));
		if (!havePos
			|| !ScRestoreWndGeom(s_inst, savedata.sasamiNotePalX, savedata.sasamiNotePalY, pw, ph, 260, 280))
			s_inst->SetWindowPos(NULL, screenPt.x, screenPt.y, pw, ph, SWP_NOZORDER);
	}
	s_inst->ShowWindow(SW_SHOW);
	s_inst->BringWindowToTop();
	return s_inst;
}

void CSasamiNotePaletteDlg::NotifyParent()
{
	int d = m_baseDur;
	if (m_dotted) d += d / 2;
	if (m_tuplet == 3) d = (d * 2) / 3;
	else if (m_tuplet == 5) d = (d * 4) / 5;
	else if (m_tuplet == 6) d = (d * 4) / 6;
	else if (m_tuplet == 8) d = (d * 4) / 8;
	if (d < 1) d = 1;
	m_durTicks = d;
	if (!m_notify) return;
	/* Duration path: never set SASAMI_PAL_CMD. accidental masked to 8 bits. */
	LPARAM lp = (m_rest ? 1 : 0) | (m_dotted ? 2 : 0)
		| (((LPARAM)(m_tuplet & 0xF)) << 4)
		| (((LPARAM)(m_accidental & 0xFF)) << 8)
		| (((LPARAM)(m_baseDur & 0xFFFF)) << 16);
	::PostMessage(m_notify, WM_SASAMI_PAL_DUR, (WPARAM)m_durTicks, lp);
}

static void ScPalPostCmd(HWND notify, int cmdId)
{
	if (!notify) return;
	::PostMessage(notify, WM_SASAMI_PAL_DUR, 0,
		(LPARAM)(SASAMI_PAL_CMD | (cmdId & 0xFF)));
}

BEGIN_MESSAGE_MAP(CSasamiNotePaletteDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
END_MESSAGE_MAP()

void CSasamiNotePaletteDlg::ApplyLang()
{
	SetWindowText(LL14(L"音符", L"Notes", L"Notes", L"Note", L"Notas",
		L"음표", L"音符", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Notalar"));
}

/* kind >0 = note dur; <=-100 = rest; other negatives = commands (see OnLButtonDown). */
static const int kPalKind[CSasamiNotePaletteDlg::kCellCount] = {
	SC_PPQN * 4, SC_PPQN * 2, SC_PPQN, SC_PPQN / 2,
	SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16, -2,
	-(100 + SC_PPQN * 2), -(100 + SC_PPQN), -(100 + SC_PPQN / 2), -(100 + SC_PPQN / 4),
	-(100 + SC_PPQN / 8), -(100 + SC_PPQN / 16), -(100 + SC_PPQN / 32), -(100 + SC_PPQN / 64),
	-7, -71, -72, -73,
	-3, -4, -5, -52,
	-20, -21, -22, -23,
	-34, -35, -26, -8,
	-30, -31, -32, -33,
	-40, -41, -42, -43,
	-44, -45, -46, -10,
	-53, -6, 0, 0
};

static const wchar_t* ScPalTip(int i)
{
	switch (i) {
	case 0: return LL14(L"全音符 (1)", L"Whole note (1)", L"Ronde (1)", L"Semibreve (1)", L"Redonda (1)", L"온음표 (1)", L"全音符 (1)", L"مستديرة (1)", L"Целая (1)", L"Ganze (1)", L"Semibreve (1)", L"Hele (1)", L"Cała (1)", L"Birlik (1)");
	case 1: return LL14(L"2分音符 (1/2)", L"Half note (1/2)", L"Blanche (1/2)", L"Minima (1/2)", L"Blanca (1/2)", L"2분음표", L"二分音符", L"بيضاء", L"Половинная", L"Halbe", L"Mínima", L"Halve", L"Półnuta", L"İkilik");
	case 2: return LL14(L"4分音符 (1/4)", L"Quarter note (1/4)", L"Noire (1/4)", L"Semiminima (1/4)", L"Negra (1/4)", L"4분음표", L"四分音符", L"سوداء", L"Четверть", L"Viertel", L"Semínima", L"Kwart", L"Ćwierćnuta", L"Dörtlük");
	case 3: return LL14(L"8分音符 (1/8)", L"Eighth note (1/8)", L"Croche (1/8)", L"Croma (1/8)", L"Corchea (1/8)", L"8분음표", L"八分音符", L"علامة ثامنة", L"Восьмая", L"Achtel", L"Colcheia", L"Achtste", L"Ósemka", L"Sekizlik");
	case 4: return LL14(L"16分音符 (1/16)", L"16th note (1/16)", L"Double croche", L"Semicroma", L"Semicorchea", L"16분음표", L"十六分音符", L"16", L"1/16", L"16tel", L"Semicolcheia", L"16e", L"Szesnastka", L"Onaltılık");
	case 5: return LL14(L"32分音符 (1/32)", L"32nd note (1/32)", L"Triple croche", L"Biscroma", L"Fusa", L"32분음표", L"三十二分音符", L"32", L"1/32", L"32tel", L"Fusa", L"32e", L"32-ka", L"32’lik");
	case 6: return LL14(L"64分音符 (1/64)", L"64th note (1/64)", L"Quadruple croche", L"Semibiscroma", L"Semifusa", L"64분음표", L"六十四分音符", L"64", L"1/64", L"64tel", L"Semifusa", L"64e", L"64-ka", L"64’lük");
	case 7: return LL14(L"付点 (+½)", L"Dotted (+½)", L"Pointée (+½)", L"Puntata (+½)", L"Con puntillo (+½)", L"점음 (+½)", L"附点 (+½)", L"منقوطة (+½)", L"С точкой (+½)", L"Punktiert (+½)", L"Com pontilhado (+½)", L"Gepunt (+½)", L"Z kropką (+½)", L"Noktalı (+½)");
	case 8: return LL14(L"4分休符", L"Quarter rest", L"Soupir", L"Pausa di semiminima", L"Silencio de negra", L"4분쉼표", L"四分休止", L"سكتة سوداء", L"Пауза 1/4", L"Viertelpause", L"Pausa de semínima", L"Kwartsrust", L"Pauza ćwierćnutowa", L"Dörtlük es");
	case 9: return LL14(L"8分休符", L"Eighth rest", L"Demi-soupir", L"Pausa di croma", L"Silencio de corchea", L"8분쉼표", L"八分休止", L"سكتة ثامنة", L"Пауза 1/8", L"Achtelpause", L"Pausa de colcheia", L"Achtste rust", L"Pauza ósemkowa", L"Sekizlik es");
	case 10: return LL14(L"16分休符", L"16th rest", L"Quart de soupir", L"Pausa di semicroma", L"Silencio de semicorchea", L"16분쉼표", L"十六分休止", L"سكتة 16", L"Пауза 1/16", L"16tel-Pause", L"Pausa de semicolcheia", L"16e rust", L"Pauza 16", L"Onaltılık es");
	case 11: return LL14(L"32分休符", L"32nd rest", L"Huitième de soupir", L"Pausa di biscroma", L"Silencio de fusa", L"32분쉼표", L"三十二分休止", L"سكتة 32", L"Пауза 1/32", L"32tel-Pause", L"Pausa de fusa", L"32e rust", L"Pauza 32", L"32’lik es");
	case 12: return LL14(L"3連符", L"Triplet 3", L"Triolet", L"Terzina", L"Tresillo", L"셋잇단음표", L"三连音", L"ثلاثي", L"Триоль", L"Triole", L"Tercina", L"Triool", L"Triola", L"Üçleme");
	case 13: return LL14(L"5連符", L"Quintuplet 5", L"Quintolet", L"Quintina", L"Cinquillo", L"다섯잇단", L"五连音", L"خماسي", L"Квинтоль", L"Quintole", L"Quintina", L"Kwintool", L"Kwintola", L"Beşleme");
	case 14: return LL14(L"6連符", L"Sextuplet 6", L"Sextolet", L"Sestina", L"Seisillo", L"여섯잇단", L"六连音", L"سداسي", L"Секстоль", L"Sextole", L"Sextina", L"Sextool", L"Sekstola", L"Altılama");
	case 15: return LL14(L"8連符", L"Octuplet 8", L"Octolet", L"Ottina", L"Ochocillo", L"여덟잇단", L"八连音", L"ثماني", L"Октоль", L"Oktolé", L"Octina", L"Octool", L"Oktola", L"Sekizleme");
	case 16: return LL14(L"シャープ ♯", L"Sharp ♯", L"Dièse ♯", L"Diesis ♯", L"Sostenido ♯", L"샵 ♯", L"升号 ♯", L"دييز ♯", L"Диез ♯", L"Kreuz ♯", L"Sustenido ♯", L"Kruis ♯", L"Krzyżyk ♯", L"Diyez ♯");
	case 17: return LL14(L"ナチュラル ♮", L"Natural ♮", L"Bécarre ♮", L"Bequadro ♮", L"Becuadro ♮", L"제자리표 ♮", L"还原号 ♮", L"بيكار ♮", L"Бекар ♮", L"Auflösungszeichen ♮", L"Bequadro ♮", L"Herstellingsteken ♮", L"Kasownik ♮", L"Natura ♮");
	case 18: return LL14(L"フラット ♭", L"Flat ♭", L"Bémol ♭", L"Bemolle ♭", L"Bemol ♭", L"플랫 ♭", L"降号 ♭", L"بيمول ♭", L"Бемоль ♭", L"Be ♭", L"Bemol ♭", L"Mol ♭", L"Bemol ♭", L"Bemol ♭");
	case 19: return LL14(L"スナップ Fit", L"Snap fit", L"Aimantation Fit", L"Snap Fit", L"Ajuste Fit", L"스냅 Fit", L"吸附 Fit", L"التقاط Fit", L"Привязка Fit", L"Snap Fit", L"Encaixe Fit", L"Snap Fit", L"Przyciąganie Fit", L"Yapışma Fit");
	case 20: return ScStaffToolName(SC_TOOL_PENCIL);
	case 21: return LL14(L"消しゴム（|: :| 8va を五線で、または消しゴム+パレット）", L"Eraser (click |: :| 8va on staff, or Eraser+palette)", L"Gomme (|: :| 8va ou Gomme+palette)", L"Gomma (|: :| 8va o Gomma+tavolozza)", L"Borrar (|: :| 8va o Borrar+paleta)", L"지우개 (|: :| 8va 또는 지우개+팔레트)", L"橡皮（|: :| 8va，或橡皮+调色板）", L"ممحاة (|: :| 8va أو ممحاة+لوحة)", L"Ластик (|: :| 8va или ластик+палитра)", L"Radierer (|: :| 8va oder Radierer+Palette)", L"Borracha (|: :| 8va ou Borracha+paleta)", L"Gum (|: :| 8va of Gum+palet)", L"Gumka (|: :| 8va lub Gumka+paleta)", L"Silgi (|: :| 8va veya Silgi+palet)");
	case 22: return LL14(L"選択 + Delete でマーク削除", L"Select + Delete removes marks", L"Sélection + Suppr", L"Selezione + Canc", L"Seleccionar + Supr", L"선택 + Delete", L"选择 + Delete", L"تحديد + Delete", L"Выбор + Delete", L"Auswahl + Entf", L"Selecionar + Del", L"Selecteren + Del", L"Zaznacz + Del", L"Seç + Del");
	case 23: return ScStaffToolName(SC_TOOL_TEMPO);
	case 24: return LL14(L"マーカー", L"Marker", L"Marqueur", L"Marcatore", L"Marcador", L"마커", L"标记", L"علامة", L"Маркер", L"Markierung", L"Marcador", L"Markering", L"Znacznik", L"İşaret");
	case 25: return LL14(L"配置: 1重（置換）", L"Place mode: replace (1-deep)", L"Mode: remplacer", L"Modalità: sostituisci", L"Modo: reemplazar", L"배치: 1중(치환)", L"放置: 单层(替换)", L"وضع: استبدال", L"Режим: замена", L"Modus: ersetzen", L"Modo: substituir", L"Modus: vervangen", L"Tryb: zamień", L"Mod: değiştir");
	case 26: return LL14(L"配置: ネスト（積み上げ）", L"Place mode: nest/stack", L"Mode: nid/empiler", L"Modalità: nest/impila", L"Modo: anidar/apilar", L"배치: 중첩", L"放置: 嵌套", L"وضع: تداخل", L"Режим: вложение", L"Modus: nesten", L"Modo: ninho", L"Modus: nest", L"Tryb: zagnieżdż", L"Mod: yuva");
	case 27: return LL14(L"A-Bループ解除", L"Clear A-B loop", L"Effacer boucle A-B", L"Cancella loop A-B", L"Borrar bucle A-B", L"A-B 루프 해제", L"清除 A-B 循环", L"مسح حلقة A-B", L"Сброс цикла A-B", L"A-B-Loop löschen", L"Limpar loop A-B", L"A-B-lus wissen", L"Wyczyść pętlę A-B", L"A-B döngüsünü temizle");
	case 28: return LL14(L"|: を赤バーに（消しゴム+クリックで削除）", L"|: at red bar (Eraser+click deletes)", L"|: sur barre rouge", L"|: sulla barra rossa", L"|: en barra roja", L"|: 빨간 바에", L"|: 在红条", L"|: عند الشريط الأحمر", L"|: на красной метке", L"|: an roter Markierung", L"|: na barra vermelha", L"|: op rode balk", L"|: na czerwonym pasku", L"|: kırmızı çubukta");
	case 29: return LL14(L":| を赤バーに（消しゴムで削除）", L":| at red bar (Eraser deletes)", L":| sur barre rouge", L":| sulla barra rossa", L":| en barra roja", L":| 빨간 바에", L":| 在红条", L":| عند الشريط الأحمر", L":| на красной метке", L":| an roter Markierung", L":| na barra vermelha", L":| op rode balk", L":| na czerwonym pasku", L":| kırmızı çubukta");
	case 30: return LL14(L"ペダルON (Ped.)", L"Pedal ON (Ped.)", L"Pédale ON (Ped.)", L"Pedale ON (Ped.)", L"Pedal ON (Ped.)", L"페달 ON (Ped.)", L"踏板ON (Ped.)", L"دواسة ON (Ped.)", L"Педаль ON (Ped.)", L"Pedal ON (Ped.)", L"Pedal ON (Ped.)", L"Pedaal ON (Ped.)", L"Pedal ON (Ped.)", L"Pedal ON (Ped.)");
	case 31: return LL14(L"ペダルOFF (＊)", L"Pedal OFF (＊)", L"Pédale OFF (＊)", L"Pedale OFF (＊)", L"Pedal OFF (＊)", L"페달 OFF (＊)", L"踏板OFF (＊)", L"دواسة OFF (＊)", L"Педаль OFF (＊)", L"Pedal OFF (＊)", L"Pedal OFF (＊)", L"Pedaal OFF (＊)", L"Pedal OFF (＊)", L"Pedal OFF (＊)");
	case 32: return LL14(L"8va を赤バーに", L"8va at red bar", L"8va sur barre rouge", L"8va sulla barra rossa", L"8va en barra roja", L"8va 빨간 바에", L"8va 在红条", L"8va عند الشريط الأحمر", L"8va на красной метке", L"8va an roter Markierung", L"8va na barra vermelha", L"8va op rode balk", L"8va na czerwonym pasku", L"8va kırmızı çubukta");
	case 33: return LL14(L"8vb を赤バーに", L"8vb at red bar", L"8vb sur barre rouge", L"8vb sulla barra rossa", L"8vb en barra roja", L"8vb 빨간 바에", L"8vb 在红条", L"8vb عند الشريط الأحمر", L"8vb на красной метке", L"8vb an roter Markierung", L"8vb na barra vermelha", L"8vb op rode balk", L"8vb na czerwonym pasku", L"8vb kırmızı çubukta");
	case 34: return LL14(L"16va を赤バーに", L"16va at red bar", L"16va sur barre rouge", L"16va sulla barra rossa", L"16va en barra roja", L"16va 빨간 바에", L"16va 在红条", L"16va عند الشريط الأحمر", L"16va на красной метке", L"16va an roter Markierung", L"16va na barra vermelha", L"16va op rode balk", L"16va na czerwonym pasku", L"16va kırmızı çubukta");
	case 35: return LL14(L"16vb を赤バーに", L"16vb at red bar", L"16vb sur barre rouge", L"16vb sulla barra rossa", L"16vb en barra roja", L"16vb 빨간 바에", L"16vb 在红条", L"16vb عند الشريط الأحمر", L"16vb на красной метке", L"16vb an roter Markierung", L"16vb na barra vermelha", L"16vb op rode balk", L"16vb na czerwonym pasku", L"16vb kırmızı çubukta");
	case 36: return LL14(L"32va を赤バーに", L"32va at red bar", L"32va sur barre rouge", L"32va sulla barra rossa", L"32va en barra roja", L"32va 빨간 바에", L"32va 在红条", L"32va عند الشريط الأحمر", L"32va на красной метке", L"32va an roter Markierung", L"32va na barra vermelha", L"32va op rode balk", L"32va na czerwonym pasku", L"32va kırmızı çubukta");
	case 37: return LL14(L"32vb を赤バーに", L"32vb at red bar", L"32vb sur barre rouge", L"32vb sulla barra rossa", L"32vb en barra roja", L"32vb 빨간 바에", L"32vb 在红条", L"32vb عند الشريط الأحمر", L"32vb на красной метке", L"32vb an roter Markierung", L"32vb na barra vermelha", L"32vb op rode balk", L"32vb na czerwonym pasku", L"32vb kırmızı çubukta");
	case 38: return LL14(L"loco を赤バーに（オッターバ解除）", L"loco at red bar (cancels ottava)", L"loco sur barre rouge (annule ottava)", L"loco sulla barra rossa (annulla ottava)", L"loco en barra roja (cancela ottava)", L"loco 빨간 바에 (옥타바 해제)", L"loco 在红条（取消八度）", L"loco عند الشريط الأحمر (يلغي الأوكتافا)", L"loco на красной метке (снимает оттаву)", L"loco an roter Markierung (hebt Ottava auf)", L"loco na barra vermelha (cancela ottava)", L"loco op rode balk (heft ottava op)", L"loco na czerwonym pasku (anuluje ottavę)", L"loco kırmızı çubukta (ottavayı kaldırır)");
	case 39: return L"·";
	default: return L"";
	}
}

void CSasamiNotePaletteDlg::SetupCellTips()
{
	if (m_tip.GetSafeHwnd())
		m_tip.DestroyWindow();
	if (!m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
		return;
	m_tip.Activate(TRUE);
	m_tip.SetMaxTipWidth(280);
	for (int i = 0; i < kCellCount; i++) {
		if (!kPalKind[i]) continue;
		TOOLINFO ti = {};
		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = m_hWnd;
		ti.uId = (UINT_PTR)(i + 1);
		ti.rect = m_cells[i];
		ti.lpszText = LPSTR_TEXTCALLBACK;
		m_tip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}
}

BOOL CSasamiNotePaletteDlg::OnTtnNeedText(UINT, NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!pNMHDR || !pResult) return FALSE;
	*pResult = 0;
	NMTTDISPINFOW* di = (NMTTDISPINFOW*)pNMHDR;
	const int i = (int)di->hdr.idFrom - 1;
	if (i < 0 || i >= kCellCount) return FALSE;
	di->lpszText = (LPWSTR)ScPalTip(i);
	di->hinst = NULL;
	return TRUE;
}

BOOL CSasamiNotePaletteDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

BOOL CSasamiNotePaletteDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_markStack = savedata.sasamiMarkStack ? 1 : 0;
	ApplyLang();
	LayoutChrome();
	SetupCellTips();
	return TRUE;
}

void CSasamiNotePaletteDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 5, cols = 6, rows = 8;
	const int cw = (rc.Width() - pad * 2) / cols;
	const int ch = max(24, (rc.Height() - cap - pad * 2) / rows);
	int i = 0;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++)
			m_cells[i++].SetRect(pad + c * cw, cap + pad + r * ch,
				pad + (c + 1) * cw - 2, cap + pad + (r + 1) * ch - 2);
	if (m_tip.GetSafeHwnd())
		SetupCellTips();
}

void CSasamiNotePaletteDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
	if (GetSafeHwnd() && IsWindowVisible())
		ScSaveWndGeom(this, &savedata.sasamiNotePalX, &savedata.sasamiNotePalY,
			&savedata.sasamiNotePalW, &savedata.sasamiNotePalH);
}

void CSasamiNotePaletteDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	LayoutChrome();
	if (GetSafeHwnd() && IsWindowVisible())
		ScSaveWndGeom(this, &savedata.sasamiNotePalX, &savedata.sasamiNotePalY,
			&savedata.sasamiNotePalW, &savedata.sasamiNotePalH);
	Invalidate(FALSE);
}

BOOL CSasamiNotePaletteDlg::OnEraseBkgnd(CDC* pDC) { return CCustomBlurDialogExBase::OnEraseBkgnd(pDC); }

void CSasamiNotePaletteDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(rc.left, cap, rc.right, rc.bottom);
	CDC mem; mem.CreateCompatibleDC(&dc);
	CBitmap bmp; bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(38, 42, 58));

	for (int i = 0; i < kCellCount; i++) {
		if (!kPalKind[i]) continue;
		CRect c = m_cells[i];
		c.OffsetRect(-body.left, -body.top);
		const int k = kPalKind[i];
		if (k > 0) {
			int drawDur = k;
			if (m_dotted && !m_rest && m_baseDur == k)
				drawDur = k + k / 2;
			const int sel = (!m_rest && m_baseDur == k);
			const int acc = sel ? m_accidental : 0;
			ScStaffDrawNoteGlyphPal(mem, c, drawDur, 0, sel, acc);
		} else if (k <= -100) {
			int rd = -(k + 100);
			ScStaffDrawNoteGlyphPal(mem, c, rd, 1, m_rest && m_baseDur == rd, 0);
		} else {
			const int selCmd = (k == -2 && m_dotted) || (k == -7 && m_tuplet == 3) || (k == -71 && m_tuplet == 5)
				|| (k == -72 && m_tuplet == 6) || (k == -73 && m_tuplet == 8)
				|| (k == -3 && m_accidental == 1) || (k == -4 && m_accidental == 0)
				|| (k == -5 && m_accidental == -1) || (k == -52 && m_accidental == -2)
				|| (k == -53 && m_accidental == 2)
				|| (k == -34 && m_markStack == 0) || (k == -35 && m_markStack != 0);
			ScStaffDrawPalCell(mem, c, selCmd);
			mem.SetBkMode(TRANSPARENT);
			CFont font;
			font.CreateFont(max(12, c.Height() / 2), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
			CFont* of = mem.SelectObject(&font);
			mem.SetTextColor(RGB(245, 248, 255));
			const wchar_t* lab = L"";
			if (k == -2) lab = L"·";
			else if (k == -7) lab = L"3";
			else if (k == -71) lab = L"5";
			else if (k == -72) lab = L"6";
			else if (k == -73) lab = L"8";
			else if (k == -3) lab = L"♯";
			else if (k == -4) lab = L"♮";
			else if (k == -5) lab = L"♭";
			else if (k == -52) lab = L"♭♭";
			else if (k == -53) lab = L"♯♯";
			else if (k == -6) lab = L"·";
			else if (k == -8) lab = L"Fit";
			else if (k == -10) lab = L"♪=";
			else if (k == -20) lab = L"✎";
			else if (k == -21) lab = L"⌫";
			else if (k == -22) lab = L"▢";
			else if (k == -23) lab = L"▼";
			else if (k == -34) lab = L"1重";
			else if (k == -35) lab = L"ネスト";
			else if (k == -26) lab = L"∅";
			else if (k == -30) lab = L"|:";
			else if (k == -31) lab = L":|";
			else if (k == -32) lab = L"Ped.";
			else if (k == -33) lab = L"＊";
			else if (k == -40) lab = L"8va";
			else if (k == -41) lab = L"8vb";
			else if (k == -42) lab = L"16va";
			else if (k == -43) lab = L"16vb";
			else if (k == -44) lab = L"32va";
			else if (k == -45) lab = L"32vb";
			else if (k == -46) lab = L"loco";
			mem.DrawText(lab, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			mem.SelectObject(of);
		}
	}
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiNotePaletteDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	for (int i = 0; i < kCellCount; i++) {
		if (!kPalKind[i] || !m_cells[i].PtInRect(point)) continue;
		const int k = kPalKind[i];
		if (k > 0) { m_baseDur = k; m_rest = 0; NotifyParent(); }
		else if (k <= -100) { m_baseDur = -(k + 100); m_rest = 1; NotifyParent(); }
		else if (k == -2) { m_dotted ^= 1; NotifyParent(); }
		else if (k == -7) { m_tuplet = (m_tuplet == 3) ? 0 : 3; NotifyParent(); }
		else if (k == -71) { m_tuplet = (m_tuplet == 5) ? 0 : 5; NotifyParent(); }
		else if (k == -72) { m_tuplet = (m_tuplet == 6) ? 0 : 6; NotifyParent(); }
		else if (k == -73) { m_tuplet = (m_tuplet == 8) ? 0 : 8; NotifyParent(); }
		else if (k == -3) { m_accidental = 1; NotifyParent(); }
		else if (k == -4) { m_accidental = 0; NotifyParent(); }
		else if (k == -5) { m_accidental = -1; NotifyParent(); }
		else if (k == -52) { m_accidental = -2; NotifyParent(); }
		else if (k == -53) { m_accidental = 2; NotifyParent(); }
		else if (k == -6) {
			/* Stay open — palette is session-persistent. */
			Invalidate(FALSE);
			return;
		}
		else if (k == -8) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_FIT);
		else if (k == -10) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_TEMPO);
		else if (k == -11) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_GT);
		else if (k == -20) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_PENCIL);
		else if (k == -21) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_ERASE);
		else if (k == -22) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_SEL);
		else if (k == -23) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_MARK);
		else if (k == -34) {
			m_markStack = 0;
			savedata.sasamiMarkStack = 0;
			ScPalPostCmd(m_notify, SASAMI_PAL_CMD_MARK_REPLACE);
		}
		else if (k == -35) {
			m_markStack = 1;
			savedata.sasamiMarkStack = 1;
			ScPalPostCmd(m_notify, SASAMI_PAL_CMD_MARK_STACK);
		}
		else if (k == -26) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_LOOP_CLR);
		else if (k == -30) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_LOOP_START);
		else if (k == -31) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_LOOP_END);
		else if (k == -32) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_PED_ON);
		else if (k == -33) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_PED_OFF);
		else if (k == -40) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_8VA);
		else if (k == -41) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_8VB);
		else if (k == -42) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_16VA);
		else if (k == -43) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_16VB);
		else if (k == -44) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_32VA);
		else if (k == -45) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_32VB);
		else if (k == -46) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_OTTAVA_LOCO);
		Invalidate(FALSE);
		break;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiNotePaletteDlg::OnClose() {
	/* Keep floating palette open for the score session (× only saves geom). */
	ScSaveWndGeom(this, &savedata.sasamiNotePalX, &savedata.sasamiNotePalY,
		&savedata.sasamiNotePalW, &savedata.sasamiNotePalH);
	ShowWindow(SW_SHOW);
}

void CSasamiNotePaletteDlg::OnDestroy()
{
	ScSaveWndGeom(this, &savedata.sasamiNotePalX, &savedata.sasamiNotePalY,
		&savedata.sasamiNotePalW, &savedata.sasamiNotePalH);
	CCustomBlurDialogExBase::OnDestroy();
}

void CSasamiNotePaletteDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
