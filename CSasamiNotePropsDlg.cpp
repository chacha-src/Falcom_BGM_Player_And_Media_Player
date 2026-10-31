#include "stdafx.h"
#include "ogg.h"
#include "CSasamiNotePropsDlg.h"
#include "CSasamiVstPartMenu.h"
#include "CSasamiMidiScoreDlg.h"
#include "SasamiComposerDoc.h"
#include "SasamiToneNames.h"
#include "CSasamiStaffCore.h"
#include "VstMidiEngine.h"
#include "OfflineHelp.h"

CSasamiNotePropsDlg* CSasamiNotePropsDlg::s_inst = NULL;

CSasamiNotePropsDlg* CSasamiNotePropsDlg::Instance()
{
	return (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) ? s_inst : NULL;
}

IMPLEMENT_DYNAMIC(CSasamiNotePropsDlg, CCustomBlurDialogExBase)

CSasamiNotePropsDlg::CSasamiNotePropsDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiNotePropsDlg::IDD, pParent)
	, m_ev(NULL), m_isFm(0), m_part(1), m_notify(NULL)
{
}

CSasamiNotePropsDlg* CSasamiNotePropsDlg::OpenForEvent(CWnd* owner, ScEvent* ev, int isFm, int part1to32)
{
	if (!ev) return NULL;
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_ev = ev;
		s_inst->m_isFm = isFm;
		s_inst->m_part = part1to32;
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
		s_inst->LoadFromEv();
		s_inst->ShowWindow(SW_SHOW);
		s_inst->BringWindowToTop();
		return s_inst;
	}
	s_inst = new CSasamiNotePropsDlg(owner);
	s_inst->m_ev = ev;
	s_inst->m_isFm = isFm;
	s_inst->m_part = part1to32;
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	/* Parent = main frame so score DestroyWindow does not cascade into a stuck child. */
	CWnd* parent = AfxGetMainWnd();
	if (!parent) parent = owner;
	if (!s_inst->Create(IDD_SASAMI_NOTE_PROPS, parent)) {
		delete s_inst;
		s_inst = NULL;
		return NULL;
	}
	s_inst->ShowWindow(SW_SHOW);
	return s_inst;
}

void CSasamiNotePropsDlg::CloseOpen(void)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd()))
		s_inst->DestroyWindow();
}

void CSasamiNotePropsDlg::RefreshVstHint()
{
	if (!m_hint.GetSafeHwnd()) return;
	if (m_isFm) {
		ApplyLang();
		RefreshChromeOpaque();
		return;
	}
	if (m_part < 1 || m_part > 32) {
		m_hint.SetWindowText(LL14(L"パートが不正です", L"Invalid part", L"Partie invalide", L"Parte non valida", L"Parte no válida", L"파트가 올바르지 않습니다", L"声部无效", L"جزء غير صالح", L"Неверная партия", L"Ungültiger Part", L"Parte inválida", L"Ongeldig deel", L"Nieprawidłowa partia", L"Geçersiz parti"));
		RefreshChromeOpaque();
		return;
	}
	wchar_t path[520];
	path[0] = 0;
	const int loaded = VstLivePartIsLoaded(m_part);
	if (loaded)
		VstLivePartGetPath(m_part, path, 520);
	const wchar_t* base = L"";
	if (path[0]) {
		base = wcsrchr(path, L'\\');
		if (!base) base = wcsrchr(path, L'/');
		base = base ? base + 1 : path;
	}
	wchar_t progName[128];
	progName[0] = 0;
	const int multi = loaded && (VstLivePartIsMulti(m_part) ||
		(path[0] && VstDetectMultiTimbral(path)));
	/* Multi/remote SC-VA: never Program* IPC — freezes Host32. */
	const int prog = (loaded && !multi) ? VstLiveProgramCurrent(m_part) : -1;
	if (prog >= 0)
		VstLiveProgramName(m_part, prog, progName, 128);
	if (!progName[0] && prog >= 0) {
		int isDrum = (m_part == 10 || m_part == 26) ? 1 : 0;
		if (CSasamiMidiScoreDlg* sc = CSasamiMidiScoreDlg::Instance())
			isDrum = ScMidiPartIsDrum(sc->Doc()->ev, sc->Doc()->evCount, m_part - 1, 0);
		SasamiToneLookupAuto(0, 0, prog, isDrum, progName, 128);
	}

	CString h;
	if (loaded) {
		if (multi && base[0])
			h.Format(LL14(
				L"VST: %s（パート %d）— トーンマップ / GS·XG",
				L"VST: %s (part %d) — tone map / GS·XG",
				L"VST : %s (partie %d) — table de sons / GS·XG",
				L"VST: %s (parte %d) — mappa timbri / GS·XG",
				L"VST: %s (parte %d) — mapa de tonos / GS·XG",
				L"VST: %s (파트 %d) — 톤 맵 / GS·XG",
				L"VST：%s（声部 %d）— 音色表 / GS·XG",
				L"VST: %s (الجزء %d) — خريطة النغمات / GS·XG",
				L"VST: %s (партия %d) — карта тембров / GS·XG",
				L"VST: %s (Part %d) — Klangzuordnung / GS·XG",
				L"VST: %s (parte %d) — mapa de timbres / GS·XG",
				L"VST: %s (deel %d) — toonkaart / GS·XG",
				L"VST: %s (partia %d) — mapa brzmień / GS·XG",
				L"VST: %s (parti %d) — ton haritası / GS·XG"), base, m_part);
		else if (progName[0])
			h.Format(LL14(
				L"VST: %s | %s（パート %d PC#%d）",
				L"VST: %s | %s (part %d PC#%d)",
				L"VST : %s | %s (partie %d PC#%d)",
				L"VST: %s | %s (parte %d PC#%d)",
				L"VST: %s | %s (parte %d PC#%d)",
				L"VST: %s | %s (파트 %d PC#%d)",
				L"VST：%s | %s（声部 %d PC#%d）",
				L"VST: %s | %s (الجزء %d PC#%d)",
				L"VST: %s | %s (партия %d PC#%d)",
				L"VST: %s | %s (Part %d PC#%d)",
				L"VST: %s | %s (parte %d PC#%d)",
				L"VST: %s | %s (deel %d PC#%d)",
				L"VST: %s | %s (partia %d PC#%d)",
				L"VST: %s | %s (parti %d PC#%d)"),
				base[0] ? base : LL14(L"（プラグイン）", L"(plugin)", L"(plug-in)", L"(plug-in)", L"(complemento)", L"(플러그인)", L"（插件）", L"(إضافة)", L"(плагин)", L"(Plug-in)", L"(plug-in)", L"(plug-in)", L"(wtyczka)", L"(eklenti)"),
				progName, m_part, prog + 1);
		else if (base[0]) {
			const int nProg = VstLiveProgramCount(m_part);
			if (nProg <= 0)
				h.Format(LL14(
					L"VST: %s（パート %d）— MediaBay／内部パッチ（一覧APIなし）",
					L"VST: %s (part %d) — MediaBay / internal patch (no list API)",
					L"VST : %s (partie %d) — MediaBay / patch interne (pas d'API de liste)",
					L"VST: %s (parte %d) — MediaBay / patch interna (niente API elenco)",
					L"VST: %s (parte %d) — MediaBay / parche interno (sin API de lista)",
					L"VST: %s (파트 %d) — MediaBay/내부 패치(목록 API 없음)",
					L"VST：%s（声部 %d）— MediaBay／内部音色（无列表 API）",
					L"VST: %s (الجزء %d) — MediaBay / رقعة داخلية (بلا واجهة قائمة)",
					L"VST: %s (партия %d) — MediaBay / внутренний патч (нет API списка)",
					L"VST: %s (Part %d) — MediaBay / interner Patch (keine Listen-API)",
					L"VST: %s (parte %d) — MediaBay / patch interno (sem API de lista)",
					L"VST: %s (deel %d) — MediaBay / interne patch (geen lijst-API)",
					L"VST: %s (partia %d) — MediaBay / łatka wewnętrzna (bez API listy)",
					L"VST: %s (parti %d) — MediaBay / iç yama (liste API'si yok)"),
					base, m_part);
			else
				h.Format(LL14(
					L"VST: %s（パート %d）— トーンマップ / MediaBay で選択",
					L"VST: %s (part %d) — choose in the tone map or MediaBay",
					L"VST : %s (partie %d) — choisir dans la table de sons ou MediaBay",
					L"VST: %s (parte %d) — scegli nella mappa timbri o in MediaBay",
					L"VST: %s (parte %d) — elija en el mapa de tonos o en MediaBay",
					L"VST: %s (파트 %d) — 톤 맵 또는 MediaBay에서 선택",
					L"VST：%s（声部 %d）— 在音色表或 MediaBay 中选择",
					L"VST: %s (الجزء %d) — اختر في خريطة النغمات أو MediaBay",
					L"VST: %s (партия %d) — выбор в карте тембров или MediaBay",
					L"VST: %s (Part %d) — in Klangzuordnung oder MediaBay wählen",
					L"VST: %s (parte %d) — escolha no mapa de timbres ou no MediaBay",
					L"VST: %s (deel %d) — kies op de toonkaart of in MediaBay",
					L"VST: %s (partia %d) — wybierz na mapie brzmień lub w MediaBay",
					L"VST: %s (parti %d) — ton haritası veya MediaBay'den seçin"),
					base, m_part);
		} else
			h.Format(LL14(
				L"パート %d にVSTを読み込みました。音色を選んでください",
				L"VST loaded on part %d — pick a tone",
				L"VST chargé sur la partie %d — choisissez un son",
				L"VST caricato sulla parte %d — scegli un timbro",
				L"VST cargado en la parte %d — elija un tono",
				L"파트 %d에 VST를 불러왔습니다. 음색을 고르세요",
				L"已在声部 %d 载入 VST。请选择音色",
				L"تم تحميل VST على الجزء %d — اختر نغمة",
				L"VST загружен на партию %d — выберите тембр",
				L"VST auf Part %d geladen — Klang wählen",
				L"VST carregado na parte %d — escolha um timbre",
				L"VST geladen op deel %d — kies een klank",
				L"VST wczytany na partię %d — wybierz brzmienie",
				L"Parti %d üzerine VST yüklendi — bir ton seçin"), m_part);
		m_hint.SetWindowText(h);
	} else {
		ApplyLang();
	}
	RefreshChromeOpaque();
}

void CSasamiNotePropsDlg::NotifyVstResult(int part1to32, int ok)
{
	if (!s_inst || !::IsWindow(s_inst->GetSafeHwnd())) return;
	if (part1to32 > 0)
		s_inst->m_part = part1to32;
	if (ok) {
		/* Path-only — RefreshVstHint does PROGRAMS IPC and races HALion Home. */
		wchar_t path[520];
		path[0] = 0;
		if (VstLivePartIsLoaded(s_inst->m_part))
			VstLivePartGetPath(s_inst->m_part, path, 520);
		const wchar_t* base = path[0] ? wcsrchr(path, L'\\') : NULL;
		base = base ? base + 1 : (path[0] ? path : L"(plugin)");
		CString h;
		h.Format(LL14(
			L"VST: %s（パート %d）— 設定画面を開いています（Home/MediaBay）",
			L"VST: %s (part %d) — editor open (Home/MediaBay)",
			L"VST : %s (partie %d) — éditeur ouvert (Home/MediaBay)",
			L"VST: %s (parte %d) — editor aperto (Home/MediaBay)",
			L"VST: %s (parte %d) — editor abierto (Home/MediaBay)",
			L"VST: %s (파트 %d) — 설정 화면 열림 (Home/MediaBay)",
			L"VST：%s（声部 %d）— 设置画面已打开（Home/MediaBay）",
			L"VST: %s (الجزء %d) — المحرر مفتوح (Home/MediaBay)",
			L"VST: %s (партия %d) — редактор открыт (Home/MediaBay)",
			L"VST: %s (Part %d) — Editor offen (Home/MediaBay)",
			L"VST: %s (parte %d) — editor aberto (Home/MediaBay)",
			L"VST: %s (deel %d) — editor open (Home/MediaBay)",
			L"VST: %s (partia %d) — edytor otwarty (Home/MediaBay)",
			L"VST: %s (parti %d) — düzenleyici açık (Home/MediaBay)"),
			base, s_inst->m_part);
		s_inst->m_hint.SetWindowText(h);
		s_inst->RefreshChromeOpaque();
	} else
		s_inst->m_hint.SetWindowText(LL14(
			L"VSTの割り当てを取り消したか、失敗しました",
			L"VST assign cancelled or failed",
			L"Attribution VST annulée ou échouée",
			L"Assegnazione VST annullata o fallita",
			L"Asignación VST cancelada o fallida",
			L"VST 할당을 취소했거나 실패했습니다",
			L"VST 分配已取消或失败",
			L"تعيين VST أُلغي أو فشل",
			L"Назначение VST отменено или не удалось",
			L"VST-Zuweisung abgebrochen oder fehlgeschlagen",
			L"Atribuição VST cancelada ou falhou",
			L"VST-toewijzing geannuleerd of mislukt",
			L"Przypisanie VST anulowane lub nieudane",
			L"VST ataması iptal edildi veya başarısız"));
	RaiseSelf();
}

void CSasamiNotePropsDlg::RaiseSelf(void)
{
	if (!s_inst || !::IsWindow(s_inst->GetSafeHwnd())) return;
	s_inst->ShowWindow(SW_SHOW);
	s_inst->SetWindowPos(&wndTop, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
	s_inst->RefreshChromeOpaque();
}

void CSasamiNotePropsDlg::RefreshChromeOpaque()
{
	if (!::IsWindow(m_hWnd)) return;
	auto flatBtn = [](CCustomStandardButton& b) {
		if (!b.GetSafeHwnd()) return;
		b.SetAeroMode(FALSE);
		b.SetFlat(TRUE);
		b.Invalidate(FALSE);
	};
	flatBtn(m_btnApply); flatBtn(m_btnVst); flatBtn(m_btnEq); flatBtn(m_btnClose);
	auto solidEd = [](CCustomEdit& e) {
		if (!e.GetSafeHwnd()) return;
		e.SetAeroMode(FALSE);
		e.Invalidate(FALSE);
	};
	solidEd(m_edNote); solidEd(m_edGt); solidEd(m_edVel); solidEd(m_edEq);
	if (m_hint.GetSafeHwnd()) {
		m_hint.SetAeroMode(FALSE);
		m_hint.Invalidate(FALSE);
	}
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, cap, rc.right, rc.bottom);
	InvalidateRect(&body, FALSE);
	RedrawWindow(&body, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void CSasamiNotePropsDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SASAMI_NP_NOTE, m_edNote);
	DDX_Control(pDX, IDC_SASAMI_NP_GT, m_edGt);
	DDX_Control(pDX, IDC_SASAMI_NP_VEL, m_edVel);
	DDX_Control(pDX, IDC_SASAMI_NP_EQ, m_edEq);
	DDX_Control(pDX, IDC_SASAMI_NP_APPLY, m_btnApply);
	DDX_Control(pDX, IDC_SASAMI_NP_VST, m_btnVst);
	DDX_Control(pDX, IDC_SASAMI_NP_EQBTN, m_btnEq);
	DDX_Control(pDX, IDC_SASAMI_NP_CLOSE, m_btnClose);
	DDX_Control(pDX, IDC_SASAMI_NP_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CSasamiNotePropsDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_SASAMI_NP_APPLY, OnBnClickedApply)
	ON_BN_CLICKED(IDC_SASAMI_NP_VST, OnBnClickedVst)
	ON_BN_CLICKED(IDC_SASAMI_NP_EQBTN, OnBnClickedEq)
	ON_BN_CLICKED(IDC_SASAMI_NP_CLOSE, OnBnClickedClose)
	ON_MESSAGE(WM_SASAMI_NP_OPEN_VST, OnDeferredOpenVst)
END_MESSAGE_MAP()

void CSasamiNotePropsDlg::ApplyLang()
{
	SetWindowText(LL14(
		L"音符プロパティ", L"Note Properties", L"Propriétés note", L"Proprietà nota", L"Propiedades nota",
		L"음표 속성", L"音符属性", L"خصائص النغمة", L"Свойства ноты", L"Noteneigenschaften",
		L"Propriedades da nota", L"Nooteigenschappen", L"Właściwości nuty", L"Nota özellikleri"));
	m_btnApply.SetWindowText(LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar", L"적용", L"应用", L"تطبيق", L"Применить", L"Übernehmen", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));
	m_btnVst.SetWindowText(m_isFm
		? CString(LL14(L"FM音色…", L"FM Voice…", L"Timbre FM…", L"Voce FM…", L"Voz FM…", L"FM 음색…", L"FM音色…", L"صوت FM…", L"Тембр FM…", L"FM-Klang…", L"Voz FM…", L"FM-klank…", L"Głos FM…", L"FM ses…"))
		: CString(LL14(L"音色/VST…", L"Timbre/VST…", L"Timbre/VST…", L"Timbro/VST…", L"Timbre/VST…", L"음색/VST…", L"音色/VST…", L"طابع/VST…", L"Тембр/VST…", L"Klang/VST…", L"Timbre/VST…", L"Klank/VST…", L"Barwa/VST…", L"Tını/VST…")));
	m_btnEq.SetWindowText(LL14(L"EQを書く", L"Write EQ", L"Écrire l'EQ", L"Scrivi EQ", L"Escribir EQ", L"EQ 쓰기", L"写入EQ", L"كتابة EQ", L"Записать EQ", L"EQ schreiben", L"Gravar EQ", L"EQ schrijven", L"Zapisz EQ", L"EQ yaz"));
	m_btnClose.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(
		L"音=MIDI番号、長さ=ゲート%。\n音色はパートのVST。EQを書くとコマンドロールが付きます。",
		L"Note=MIDI# / GT=gate% / Vel / Timbre via part VST. EQ writes to command-roll (auto ON).",
		L"Note=n°MIDI / GT=gate% / Vel / Timbre via VST. EQ → command-roll (ON auto).",
		L"Nota=n°MIDI / GT=gate% / Vel / Timbro via VST. EQ → command-roll (ON auto).",
		L"Nota=n°MIDI / GT=gate% / Vel / Timbre vía VST. EQ → command-roll (ON auto).",
		L"음표=MIDI# / GT=게이트% / Vel / 음색=파트 VST. EQ는 커맨드롤 자동 ON.",
		L"音符=MIDI号 / GT=门限% / Vel / 音色=声部VST。EQ写入命令卷并自动ON。",
		L"نغمة=MIDI / GT=gate% / Vel / الطابع عبر VST. EQ إلى command-roll.",
		L"Нота=MIDI# / GT=gate% / Vel / Тембр — VST партии. EQ → command-roll (авто ON).",
		L"Note=MIDI# / GT=Gate% / Vel / Klang über Part-VST. EQ → Command-Roll (auto ON).",
		L"Nota=MIDI# / GT=gate% / Vel / Timbre via VST. EQ → command-roll (ON auto).",
		L"Noot=MIDI# / GT=gate% / Vel / Klank via VST. EQ → command-roll (auto ON).",
		L"Nuta=MIDI# / GT=gate% / Vel / Barwa przez VST. EQ → command-roll (auto ON).",
		L"Nota=MIDI# / GT=gate% / Vel / Tını parça VST. EQ → command-roll (otomatik ON)."));
}

void CSasamiNotePropsDlg::LoadFromEv()
{
	if (!m_ev) return;
	ApplyLang();
	CString s;
	if (m_isFm) {
		int note = ((m_ev->a >> 4) & 0x0F) * 12 + (m_ev->a & 0x0F) + 12;
		s.Format(L"%d", note);
	} else {
		s.Format(L"%d", (int)m_ev->a);
	}
	m_edNote.SetWindowText(s);
	int gate = (m_ev->c >= 1 && m_ev->c <= 100) ? (int)m_ev->c : 100;
	s.Format(L"%d", gate);
	m_edGt.SetWindowText(s);
	s.Format(L"%d", (int)m_ev->b);
	m_edVel.SetWindowText(s);
	m_edEq.SetWindowText(L"100");
	RefreshVstHint();
}

BOOL CSasamiNotePropsDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_edNote.SetAeroMode(FALSE);
	m_edGt.SetAeroMode(FALSE);
	m_edVel.SetAeroMode(FALSE);
	m_edEq.SetAeroMode(FALSE);
	m_btnApply.SetAeroMode(FALSE);
	m_btnVst.SetAeroMode(FALSE);
	m_btnEq.SetAeroMode(FALSE);
	m_btnClose.SetAeroMode(FALSE);
	m_btnApply.SetFlat(TRUE);
	m_btnVst.SetFlat(TRUE);
	m_btnEq.SetFlat(TRUE);
	m_btnClose.SetFlat(TRUE);
	m_hint.SetAeroMode(FALSE);
	ApplyLang();
	LoadFromEv();
	if (!ScRestoreWndGeom(this, savedata.sasamiNotePropsX, savedata.sasamiNotePropsY,
		savedata.sasamiNotePropsW, savedata.sasamiNotePropsH, 520, 220))
		SetWindowPos(NULL, 0, 0, 640, 280, SWP_NOMOVE | SWP_NOZORDER);
	LayoutChrome();
	RefreshChromeOpaque();
	SetTimer(NP_HINT_TIMER, 800, NULL);
	return TRUE;
}

void CSasamiNotePropsDlg::OnTimer(UINT_PTR id)
{
	if (id == NP_HINT_TIMER && !m_isFm)
		RefreshVstHint();
	CCustomBlurDialogExBase::OnTimer(id);
}

void CSasamiNotePropsDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 12;
	const int edH = 24;
	const int btnH = 28;
	int y = cap + 14;
	/* Row1: Note / GT% / Vel — labels painted in OnPaint at these x */
	if (m_edNote.GetSafeHwnd()) m_edNote.MoveWindow(pad + 52, y, 56, edH);
	if (m_edGt.GetSafeHwnd()) m_edGt.MoveWindow(pad + 172, y, 56, edH);
	if (m_edVel.GetSafeHwnd()) m_edVel.MoveWindow(pad + 292, y, 56, edH);
	y += edH + 12;
	/* Row2: EQ value alone (not jammed into button row) */
	if (m_edEq.GetSafeHwnd()) m_edEq.MoveWindow(pad + 52, y, 56, edH);
	y += edH + 14;
	/* Row3: four buttons with gaps — no overlap */
	const int gap = 8;
	const int btnW = max(72, (rc.Width() - pad * 2 - gap * 3) / 4);
	int x = pad;
	if (m_btnApply.GetSafeHwnd()) m_btnApply.MoveWindow(x, y, btnW, btnH);
	x += btnW + gap;
	if (m_btnVst.GetSafeHwnd()) m_btnVst.MoveWindow(x, y, btnW, btnH);
	x += btnW + gap;
	if (m_btnEq.GetSafeHwnd()) m_btnEq.MoveWindow(x, y, btnW, btnH);
	x += btnW + gap;
	if (m_btnClose.GetSafeHwnd()) m_btnClose.MoveWindow(x, y, btnW, btnH);
	y += btnH + 12;
	if (m_hint.GetSafeHwnd())
		m_hint.MoveWindow(pad, y, max(80, rc.Width() - pad * 2), max(40, rc.Height() - y - 10));
}

void CSasamiNotePropsDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_btnApply.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiNotePropsDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, cap, rc.right, rc.bottom);
	if (body.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(pDC->GetSafeHdc(), body, RGB(245, 246, 250), 255);
#else
		pDC->FillSolidRect(&body, RGB(245, 246, 250));
#endif
	}
	return TRUE;
}

void CSasamiNotePropsDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(0, cap, rc.right, rc.bottom);
	if (body.Height() > 0) {
#if CCUSTOM_AERO_SUPPORT
		CCC_FillRectAlpha(dc.GetSafeHdc(), body, RGB(245, 246, 250), 255);
#else
		dc.FillSolidRect(&body, RGB(245, 246, 250));
#endif
	}
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(40, 40, 55));
	CFont* old = dc.SelectObject(GetFont());
	auto lab = [&](CWnd& ed, const wchar_t* text) {
		if (!ed.GetSafeHwnd()) return;
		CRect er; ed.GetWindowRect(&er); ScreenToClient(&er);
		CRect lr(er.left - 48, er.top, er.left - 4, er.bottom);
		dc.DrawText(text, lr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	};
	lab(m_edNote, L"音");
	lab(m_edGt, L"長さ");
	lab(m_edVel, L"強さ");
	lab(m_edEq, L"EQ");
	dc.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiNotePropsDlg::OnBnClickedApply()
{
	if (!m_ev) return;
	CString s;
	m_edNote.GetWindowText(s);
	int note = _wtoi(s);
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	m_edGt.GetWindowText(s);
	int gate = _wtoi(s);
	if (gate < 1) gate = 1;
	if (gate > 100) gate = 100;
	m_edVel.GetWindowText(s);
	int vel = _wtoi(s);
	if (vel < 1) vel = 1;
	if (vel > 127) vel = 127;
	if (m_isFm) {
		int oct = (note - 12) / 12;
		int nn = (note - 12) % 12;
		if (oct < 0) oct = 0;
		if (oct > 7) oct = 7;
		if (nn < 0) nn = 0;
		m_ev->a = (uint8_t)((oct << 4) | (nn & 0x0F));
	} else {
		m_ev->a = (uint8_t)note;
	}
	m_ev->b = (uint8_t)vel;
	m_ev->c = (uint8_t)gate;
	if (m_notify && ::IsWindow(m_notify))
		::PostMessage(m_notify, WM_SASAMI_NOTE_PROPS, 1, 0);
}

void CSasamiNotePropsDlg::OnBnClickedVst()
{
	if (m_isFm) {
		if (m_notify && ::IsWindow(m_notify))
			::PostMessage(m_notify, WM_SASAMI_NOTE_PROPS, 3, (LPARAM)m_part);
		return;
	}
	if (m_part < 1 || m_part > 32) return;

	/* Score Assign: GS/XG multi → tone map (+ audition); dedicated → editor.
	   Do not force editor here — that skipped the tone map for SC-VA. */
	if (m_notify && ::IsWindow(m_notify)) {
		m_hint.SetWindowText(LL14(
			L"音色 / VSTの割り当てを開いています…",
			L"Opening tone / VST assign…",
			L"Ouverture du son / attribution VST…",
			L"Apertura timbro / assegnazione VST…",
			L"Abriendo tono / asignación VST…",
			L"음색 / VST 할당을 여는 중…",
			L"正在打开音色 / VST 分配…",
			L"جارٍ فتح النغمة / تعيين VST…",
			L"Открытие тембра / назначения VST…",
			L"Klang / VST-Zuweisung wird geöffnet…",
			L"A abrir timbre / atribuição VST…",
			L"Klank / VST-toewijzing openen…",
			L"Otwieranie brzmienia / przypisania VST…",
			L"Ton / VST ataması açılıyor…"));
		RefreshChromeOpaque();
		::PostMessage(m_notify, WM_SASAMI_NOTE_PROPS, 4, (LPARAM)m_part);
		return;
	}
	ScMidiVstBind bind;
	memset(&bind, 0, sizeof(bind));
	for (int i = 0; i < 32; ++i) {
		bind.vstProg[i] = -1;
		bind.vstBankMsb[i] = -1;
		bind.vstBankLsb[i] = -1;
		bind.vstForceCh[i] = -1;
	}
	if (ScVstAssignToneForPart(this, m_part, &bind)) {
		/* Program IPC after Assign races Host32 createView — hint from path only. */
		wchar_t path[520];
		path[0] = 0;
		if (VstLivePartIsLoaded(m_part))
			VstLivePartGetPath(m_part, path, 520);
		const wchar_t* base = path[0] ? wcsrchr(path, L'\\') : NULL;
		base = base ? base + 1 : (path[0] ? path : L"(plugin)");
		CString h;
		h.Format(LL14(
			L"VST: %s（パート %d）— MediaBay／内部パッチ（一覧APIなし）",
			L"VST: %s (part %d) — MediaBay / internal patch (no list API)",
			L"VST : %s (partie %d) — MediaBay / patch interne (pas d'API de liste)",
			L"VST: %s (parte %d) — MediaBay / patch interna (niente API elenco)",
			L"VST: %s (parte %d) — MediaBay / parche interno (sin API de lista)",
			L"VST: %s (파트 %d) — MediaBay/내부 패치(목록 API 없음)",
			L"VST：%s（声部 %d）— MediaBay／内部音色（无列表 API）",
			L"VST: %s (الجزء %d) — MediaBay / رقعة داخلية (بلا واجهة قائمة)",
			L"VST: %s (партия %d) — MediaBay / внутренний патч (нет API списка)",
			L"VST: %s (Part %d) — MediaBay / interner Patch (keine Listen-API)",
			L"VST: %s (parte %d) — MediaBay / patch interno (sem API de lista)",
			L"VST: %s (deel %d) — MediaBay / interne patch (geen lijst-API)",
			L"VST: %s (partia %d) — MediaBay / łatka wewnętrzna (bez API listy)",
			L"VST: %s (parti %d) — MediaBay / iç yama (liste API'si yok)"), base, m_part);
		m_hint.SetWindowText(h);
		RefreshChromeOpaque();
	} else
		m_hint.SetWindowText(LL14(
			L"VSTの割り当てを取り消したか、失敗しました",
			L"VST assign cancelled or failed",
			L"Attribution VST annulée ou échouée",
			L"Assegnazione VST annullata o fallita",
			L"Asignación VST cancelada o fallida",
			L"VST 할당을 취소했거나 실패했습니다",
			L"VST 分配已取消或失败",
			L"تعيين VST أُلغي أو فشل",
			L"Назначение VST отменено или не удалось",
			L"VST-Zuweisung abgebrochen oder fehlgeschlagen",
			L"Atribuição VST cancelada ou falhou",
			L"VST-toewijzing geannuleerd of mislukt",
			L"Przypisanie VST anulowane lub nieudane",
			L"VST ataması iptal edildi veya başarısız"));
}

LRESULT CSasamiNotePropsDlg::OnDeferredOpenVst(WPARAM w, LPARAM l)
{
	const int part = (int)w;
	if (part < 1 || part > 32 || !l) return 0;
	if (::GetCapture()) ::ReleaseCapture();
	/* Never block UI — remote editor open used to freeze the whole app. */
	VstLiveEditorOpenAsync(part);
	m_hint.SetWindowText(LL14(
		L"VSTエディタを開いています。HALionは先に Single Instrument（Don't Show Home 推奨）→ MediaBay。",
		L"Opening VST editor. HALion: Single Instrument first (check Don't Show Home), then MediaBay.",
		L"Ouverture éditeur. HALion: Single Instrument d'abord, puis MediaBay.",
		L"Apertura editor. HALion: prima Single Instrument, poi MediaBay.",
		L"Abriendo editor. HALion: primero Single Instrument, luego MediaBay.",
		L"VST 편집기 여는 중. HALion: 먼저 Single Instrument → MediaBay.",
		L"正在打开编辑器。HALion请先选 Single Instrument，再 MediaBay。",
		L"فتح محرر VST. HALion: Single ثم MediaBay.",
		L"Открытие редактора. HALion: сначала Single Instrument.",
		L"VST-Editor öffnen. HALion: zuerst Single Instrument, dann MediaBay.",
		L"Abrindo editor. HALion: Single Instrument, depois MediaBay.",
		L"VST-editor openen. HALion: eerst Single Instrument.",
		L"Otwieranie edytora. HALion: najpierw Single Instrument.",
		L"VST editörü açılıyor. HALion: önce Single Instrument."));
	RaiseSelf();
	return 0;
}

void CSasamiNotePropsDlg::OnBnClickedEq()
{
	if (!m_ev) return;
	CString s; m_edEq.GetWindowText(s);
	int v = _wtoi(s);
	if (v < 0) v = 0;
	if (v > 200) v = 200;
	double t0 = ScStaffSecFromTick(m_ev->tick, 13000);
	double t1 = ScStaffSecFromTick(m_ev->tick + (m_ev->dur ? m_ev->dur : SC_PPQN), 13000);
	if (ScStaffWriteCmdRollEq(t0, t1, v, v)) {
		m_hint.SetWindowText(LL14(
			L"コマンドロールへEQを書き込み、自動ONしました（UIは開きません）。",
			L"Wrote EQ to command-roll and auto-ON (UI stays closed).",
			L"EQ écrit dans command-roll, ON auto (UI fermée).",
			L"EQ scritto su command-roll, ON auto (UI chiusa).",
			L"EQ escrito en command-roll, ON auto (UI cerrada).",
			L"커맨드롤에 EQ 기록, 자동 ON (UI 미표시).",
			L"已写入命令卷并自动ON（不打开UI）。",
			L"كُتب EQ إلى command-roll مع ON تلقائي.",
			L"EQ записан в command-roll, авто ON (UI закрыт).",
			L"EQ in Command-Roll geschrieben, auto ON (UI bleibt zu).",
			L"EQ gravado no command-roll, ON auto (UI fechada).",
			L"EQ naar command-roll, auto ON (UI dicht).",
			L"EQ zapisano do command-roll, auto ON (UI zamknięte).",
			L"EQ command-roll'a yazıldı, otomatik ON (UI kapalı)."));
	}
}

void CSasamiNotePropsDlg::OnBnClickedClose() {
	ScSaveWndGeom(this, &savedata.sasamiNotePropsX, &savedata.sasamiNotePropsY,
		&savedata.sasamiNotePropsW, &savedata.sasamiNotePropsH);
	KillTimer(NP_HINT_TIMER); DestroyWindow();
}
void CSasamiNotePropsDlg::OnClose() {
	ScSaveWndGeom(this, &savedata.sasamiNotePropsX, &savedata.sasamiNotePropsY,
		&savedata.sasamiNotePropsW, &savedata.sasamiNotePropsH);
	KillTimer(NP_HINT_TIMER); DestroyWindow();
}
void CSasamiNotePropsDlg::PostNcDestroy()
{
	ScSaveWndGeom(this, &savedata.sasamiNotePropsX, &savedata.sasamiNotePropsY,
		&savedata.sasamiNotePropsW, &savedata.sasamiNotePropsH);
	KillTimer(NP_HINT_TIMER);
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
