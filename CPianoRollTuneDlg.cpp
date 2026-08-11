#include "stdafx.h"
#include "CPianoRollTuneDlg.h"

extern save savedata;
extern void MpPersistSavedataQuick();

namespace {

class CPrtHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_PRT_HELP };
	explicit CPrtHelpDlg(CWnd* pParent = nullptr)
		: CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CPrtHelpDlg* g_prtHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CPrtHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CPrtHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"検出パラメータ操作ガイド", L"Detection Tune Guide", L"Guide paramètres détection", L"Guida parametri rilevamento",
		L"Guía parámetros detección", L"검출 파라미터 가이드", L"检测参数指南", L"دليل معلمات الكشف",
		L"Руководство параметров обнаружения", L"Erkennungsparameter-Anleitung", L"Guia parâmetros detecção", L"Detectieparameters-gids",
		L"Przewodnik parametrów wykrywania", L"Algılama parametreleri kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CPrtHelpDlg::OnOK() { DestroyWindow(); }
void CPrtHelpDlg::OnCancel() { DestroyWindow(); }
void CPrtHelpDlg::OnClose() { DestroyWindow(); }

void CPrtHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_prtHelpDlg == this)
		g_prtHelpDlg = nullptr;
	delete this;
}

BOOL CPrtHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CPrtHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"ピアノロール検出パラメータ", L"Piano roll detection — Guide", L"Détection piano roll — Guide", L"Rilevamento piano roll — Guida",
		L"Detección piano roll — Guía", L"피아노롤 검출 — 가이드", L"钢琴卷帘检测 — 指南", L"كشف البيانو — دليل",
		L"Обнаружение piano roll — руководство", L"Piano-Roll-Erkennung — Guide", L"Detecção piano roll — Guia", L"Piano roll detectie — Gids",
		L"Wykrywanie piano roll — przewodnik", L"Piano roll algılama — kılavuz"));
	y += titleLh;
	muted(L, y, LL14(
		L"音からノートを拾う感度を % で調整します。100% が既定です（25〜400%）。",
		L"Tune note-pick sensitivity as %. 100% is default (25–400%).",
		L"Régler la sensibilité en %. 100% = défaut (25–400%).",
		L"Regola la sensibilità in %. 100% = default (25–400%).",
		L"Ajuste la sensibilidad en %. 100% = predeterminado (25–400%).",
		L"음에서 노트를 잡는 감도를 %로 조정. 100%가 기본(25~400%).",
		L"以 % 调整从音频拾取音符的灵敏度。100% 为默认（25–400%）。",
		L"اضبط حساسية التقاط النغمات كنسبة. 100% افتراضي (25–400%).",
		L"Чувствительность подбора нот в %. 100% — по умолчанию (25–400%).",
		L"Noten-Empfindlichkeit in %. 100% = Standard (25–400%).",
		L"Ajuste a sensibilidade em %. 100% = padrão (25–400%).",
		L"Stel gevoeligheid in als %. 100% = standaard (25–400%).",
		L"Czułość wykrywania nut w %. 100% = domyślnie (25–400%).",
		L"Nota yakalama hassasiyetini % ile ayarla. %100 varsayılan (25–400)."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KPIANO);


	title(L, y, LL14(L"スライダーの意味（概要）", L"Slider meanings (overview)", L"Signification des curseurs", L"Significato cursori",
		L"Significado de deslizadores", L"슬라이더 의미(개요)", L"滑块含义（概览）", L"معاني المنزلقات (نظرة عامة)",
		L"Смысл ползунков (обзор)", L"Bedeutung der Schieberegler", L"Significado dos controles", L"Betekenis van schuifregelaars",
		L"Znaczenie suwaków (przegląd)", L"Kaydırıcı anlamları (özet)"));
	y += titleLh;
	body(L, y, LL14(
		L"・無音閾値 / 帯域無音 …… 低いほど小さな音もノート扱い。高いほど厳格",
		L"· Silence / band silence …… lower = pick quieter notes; higher = stricter",
		L"· Silence / bande …… bas = plus sensible; haut = plus strict",
		L"· Silenzio / banda …… basso = più sensibile; alto = più stretto",
		L"· Silencio / banda …… bajo = más sensible; alto = más estricto",
		L"· 무음/대역 무음 …… 낮을수록 작은 음도 노트. 높을수록 엄격",
		L"· 静音/频带静音 …… 越低越拾取轻音；越高越严格",
		L"· صمت / نطاق …… أقل = أكثر حساسية؛ أعلى = أشد",
		L"· Тишина / полоса …… ниже = чувствительнее; выше = строже",
		L"· Stille / Band …… niedriger = empfindlicher; höher = strenger",
		L"· Silêncio / banda …… menor = mais sensível; maior = mais rigoroso",
		L"· Stilte / band …… lager = gevoeliger; hoger = strenger",
		L"· Cisza / pasmo …… niżej = czulsze; wyżej = ostrzejsze",
		L"· Sessizlik / bant …… düşük = daha hassas; yüksek = daha katı")); y += lh;
	body(L, y, LL14(
		L"・ホールド / 再トリガー …… ノートの持続と再発音のしやすさ",
		L"· Hold / retrigger …… how long notes sustain and how easily they re-fire",
		L"· Maintien / redeclenchement …… durée et redéclenchement",
		L"· Sostegno / retrigger …… durata e riattivazione",
		L"· Sostenido / retrigger …… duración y reactivación",
		L"· 홀드 / 재트리거 …… 노트 지속과 재발화의 쉬움",
		L"· 保持 / 再触发 …… 音符持续与再次触发难易",
		L"· ثبات / إعادة تشغيل …… مدة النغمة وسهولة إعادة الإطلاق",
		L"· Удержание / ретриггер …… длительность и повторный запуск",
		L"· Halten / Retrigger …… Haltedauer und erneutes Auslösen",
		L"· Sustentação / retrigger …… duração e novo disparo",
		L"· Vasthouden / retrigger …… duur en opnieuw afvuren",
		L"· Podtrzymanie / retrigger …… czas trwania i ponowne odpalenie",
		L"· Tutma / yeniden tetik …… nota süresi ve yeniden ateşleme")); y += lh;
	body(L, y, LL14(
		L"・ピック感度(低〜高/メロディ) …… 帯域ごとの拾いやすさ",
		L"· Pick sensitivity (bass…treble/melody) …… per-band pick ease",
		L"· Sensibilité (graves…aigus/mélodie) …… par bande",
		L"· Sensibilità (graves…acuti/melodia) …… per banda",
		L"· Sensibilidad (graves…agudos/melodía) …… por banda",
		L"· 픽 감도(저~고/멜로디) …… 대역별 잡기 쉬움",
		L"· 拾取灵敏度（低…高/旋律）…… 各频带拾取难易",
		L"· حساسية الاختيار (منخفض…عالي/لحن) …… لكل نطاق",
		L"· Чувствит. подбора (бас…верх/мелодия) …… по полосам",
		L"· Empfindlichkeit (Bass…Höhen/Melodie) …… pro Band",
		L"· Sensibilidade (graves…agudos/melodia) …… por banda",
		L"· Pick-gevoeligheid (bas…hoog/melodie) …… per band",
		L"· Czułość (bas…sopran/melodia) …… wg pasma",
		L"· Seçim hassasiyeti (bas…tiz/melodi) …… banda göre")); y += lh;
	body(L, y, LL14(
		L"・倍音/ノイズ/オンセット …… ゴースト抑制・床・アタック検出の厳しさ",
		L"· Harmonics / noise / onset …… ghost reject, floor, attack strictness",
		L"· Harmoniques / bruit / attaque …… fantômes, plancher, strictesse",
		L"· Armoniche / rumore / attacco …… fantasmi, piano, rigidità",
		L"· Armónicos / ruido / ataque …… fantasmas, suelo, rigor",
		L"· 배음/노이즈/온셋 …… 고스트 억제·바닥·어택 엄격도",
		L"· 泛音/噪声/起音 …… 幽灵抑制、底噪、起音严格度",
		L"· توافقيات / ضوضاء / بداية …… رفض الشبح والأرضية والهجوم",
		L"· Обертоны / шум / атака …… призраки, пол, строгость",
		L"· Obertöne / Rauschen / Onset …… Geister, Boden, Strenge",
		L"· Harmônicos / ruído / ataque …… fantasmas, piso, rigor",
		L"· Harmonischen / ruis / onset …… spoken, vloer, strengheid",
		L"· Harmoniczne / szum / onset …… duchy, podłoga, ostrość",
		L"· Armonik / gürültü / onset …… hayalet, taban, saldırı katılığı")); y += lh + 4;

	title(L, y, LL14(L"既定に戻す", L"Reset to defaults", L"Réinitialiser", L"Ripristina",
		L"Restablecer", L"기본값", L"恢复默认", L"إعادة ضبط",
		L"Сброс", L"Zurücksetzen", L"Redefinir", L"Reset",
		L"Reset", L"Sıfırla"));
	y += titleLh;
	body(L, y, LL14(
		L"・すべての検出パラメータを 100%（既定）へ戻します。即時反映されます",
		L"· Sets all detection parameters back to 100% (defaults). Applies immediately",
		L"· Remet tous les paramètres à 100%. Appliqué immédiatement",
		L"· Ripristina tutti i parametri al 100%. Applicato subito",
		L"· Restablece todos los parámetros al 100%. Se aplica al instante",
		L"· 모든 검출 파라미터를 100%(기본)으로. 즉시 반영",
		L"· 将所有检测参数恢复为 100%（默认）。立即生效",
		L"· يعيد كل المعلمات إلى 100%. يُطبَّق فوراً",
		L"· Сбрасывает все параметры на 100%. Применяется сразу",
		L"· Setzt alle Parameter auf 100%. Sofort wirksam",
		L"· Redefine todos os parâmetros para 100%. Aplica na hora",
		L"· Zet alle parameters terug op 100%. Meteen van kracht",
		L"· Przywraca wszystkie parametry do 100%. Natychmiast",
		L"· Tüm parametreleri %100'e döndürür. Hemen uygulanır")); y += lh + 4;

	title(L, y, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	y += titleLh;
	body(L, y, LL14(
		L"・現在の値を保存してウィンドウを閉じます（スライダー操作でも随時保存）",
		L"· Saves current values and closes (sliders also save as you move them)",
		L"· Enregistre et ferme (les curseurs sauvent aussi en temps réel)",
		L"· Salva e chiude (i cursori salvano anche in tempo reale)",
		L"· Guarda y cierra (los deslizadores también guardan al mover)",
		L"· 현재 값을 저장하고 닫습니다(슬라이더 조작 시에도 수시 저장)",
		L"· 保存当前值并关闭（移动滑块时也会随时保存）",
		L"· يحفظ القيم ويغلق (المنزلقات تحفظ أيضاً أثناء التحريك)",
		L"· Сохраняет и закрывает (ползунки тоже сохраняют сразу)",
		L"· Speichert und schließt (Schieberegler speichern auch laufend)",
		L"· Salva e fecha (controles também salvam ao mover)",
		L"· Slaat op en sluit (schuiven slaan ook meteen op)",
		L"· Zapisuje i zamyka (suwaki też zapisują na bieżąco)",
		L"· Değerleri kaydedip kapatır (kaydırıcılar da anında kaydeder)")); y += lh + 4;
	muted(L, y, LL14(
		L"各行のラベル/スライダーにマウスを置くと、個別の詳しい説明が出ます。",
		L"Hover a row label or slider for a detailed per-parameter tip.",
		L"Survolez un libellé/curseur pour le détail de chaque paramètre.",
		L"Passa su etichetta/cursore per il dettaglio di ogni parametro.",
		L"Pase el ratón por etiqueta/deslizador para el detalle de cada parámetro.",
		L"각 행 라벨/슬라이더에 마우스를 올리면 개별 설명이 나옵니다.",
		L"将鼠标悬停在各行标签/滑块上可查看单项详细说明。",
		L"مرّر على التسمية/المنزلق لعرض تفاصيل كل معلمة.",
		L"Наведите на подпись/ползунок для подробной подсказки.",
		L"Hover über Label/Schieberegler zeigt die Detail-Hilfe.",
		L"Passe o mouse no rótulo/controle para a dica detalhada.",
		L"Hover over label/schuif voor de gedetailleerde tip.",
		L"Najedź na etykietę/suwak, by zobaczyć szczegółową podpowiedź.",
		L"Satır etiketi/kaydırıcıya gelince ayrıntılı ipucu görünür."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

IMPLEMENT_DYNAMIC(CPianoRollTuneDlg, CCustomBlurDialogExBase)

CPianoRollTuneDlg::CPianoRollTuneDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_PIANOROLL_TUNE, pParent)
{
	ZeroMemory(m_pPct, sizeof(m_pPct));
}

CPianoRollTuneDlg::~CPianoRollTuneDlg()
{
	if (m_fontRow.GetSafeHandle())
		m_fontRow.DeleteObject();
}

void CPianoRollTuneDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PRT_RESET, m_reset);
	DDX_Control(pDX, IDC_PRT_OK, m_ok);
	DDX_Control(pDX, IDC_PRT_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CPianoRollTuneDlg, CCustomBlurDialogExBase)
	ON_WM_HSCROLL()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_BN_CLICKED(IDC_PRT_RESET, &CPianoRollTuneDlg::OnReset)
	ON_BN_CLICKED(IDC_PRT_OK, &CPianoRollTuneDlg::OnOk)
	ON_BN_CLICKED(IDC_PRT_HELP, &CPianoRollTuneDlg::OnBnClickedHelp)
END_MESSAGE_MAP()

static void PrTuneInitStatic(CCustomStatic& st, CFont* pFont, BOOL bAero)
{
	if (pFont)
		st.SetFont(pFont, FALSE);
	st.SetGradation(0, 0, 0, FALSE);
	st.SetDropShadow(0, 0, 0, 0, FALSE);
	st.SetPreferWideMode(FALSE);
	st.SetAeroMode(bAero);
}

static void PrTuneSetLabelText(CCustomStatic& st, LPCTSTR plain)
{
	if (!plain || !*plain) return;
	CString s;
	s.Format(_T("!@C404858%s"), plain);
	st.SetWindowText(s);
}

static void PrTuneSetValueText(CCustomStatic& st, int pct)
{
	CString s;
	s.Format(_T("!@C206088%d%%"), pct);
	st.SetWindowText(s);
}

int CPianoRollTuneDlg::ClampPct(int v)
{
	if (v < 25) return 25;
	if (v > 400) return 400;
	return v;
}

void CPianoRollTuneDlg::BuildRows()
{
	m_rowCount = 0;
	int* fields[] = {
		&savedata.prTuneSilencePct, &savedata.prTuneBandSilBassPct, &savedata.prTuneBandSilMidPct,
		&savedata.prTuneBandSilTrePct, &savedata.prTuneHoldBassPct, &savedata.prTuneHoldMidPct,
		&savedata.prTuneHoldTrePct, &savedata.prTuneRetrigPct, &savedata.prTunePickBassPct,
		&savedata.prTunePickLowMidPct, &savedata.prTunePickMelodyPct, &savedata.prTunePickTrePct,
		&savedata.prTuneHarmGhostPct, &savedata.prTuneHarmRejectPct, &savedata.prTuneHarmProfPct,
		&savedata.prTuneAbsFloorPct, &savedata.prTuneOnsetDeltaPct,
	};
	for (size_t fi = 0; fi < _countof(fields) && m_rowCount < kRowMax; ++fi)
		m_pPct[m_rowCount++] = fields[fi];
}

static LPCTSTR PrTuneRowLabel(int idx)
{
	switch (idx) {
	case 0:
		return LL14(L"無音閾値", L"Silence threshold", L"Seuil de silence", L"Soglia silenzio",
			L"Umbral de silencio", L"무음 임계값", L"静音阈值", L"عتبة الصمت", L"Порог тишины",
			L"Stille-Schwelle", L"Limiar de silencio", L"Stilte-drempel", L"Prog ciszy", L"Sessizlik esigi");
	case 1:
		return LL14(L"低音帯無音", L"Bass band silence", L"Silence grave", L"Silenzio graves",
			L"Silencio graves", L"저음역 무음", L"低音区静音", L"صمت الطبقة المنخفضة", L"Тишина баса",
			L"Bass-Stille", L"Silencio graves", L"Bas stilte", L"Cisza basu", L"Bas bandi sessizligi");
	case 2:
		return LL14(L"中音帯無音", L"Mid band silence", L"Silence medium", L"Silenzio medi",
			L"Silencio medios", L"중음역 무음", L"中音区静音", L"صمت الطبقة المتوسطة", L"Тишина середины",
			L"Mitten-Stille", L"Silencio medios", L"Midden stilte", L"Cisza srodka", L"Orta band sessizligi");
	case 3:
		return LL14(L"高音帯無音", L"Treble band silence", L"Silence aigu", L"Silenzio acuti",
			L"Silencio agudos", L"고음역 무음", L"高音区静音", L"صمت الطبقة العالية", L"Тишина верха",
			L"Hoehen-Stille", L"Silencio agudos", L"Hoge stilte", L"Cisza sopranu", L"Tiz band sessizligi");
	case 4:
		return LL14(L"低音ホールド", L"Bass hold envelope", L"Maintien graves", L"Sostegno graves",
			L"Sostenido graves", L"저음 홀드", L"低音保持", L"ثبات الطبقة المنخفضة", L"Удержание баса",
			L"Bass-Halte", L"Sustentacao graves", L"Bas vasthouden", L"Podtrzymanie basu", L"Bas tutma zarfı");
	case 5:
		return LL14(L"中音ホールド", L"Mid hold envelope", L"Maintien medium", L"Sostegno medi",
			L"Sostenido medios", L"중음 홀드", L"中音保持", L"ثبات الطبقة المتوسطة", L"Удержание середины",
			L"Mitten-Halte", L"Sustentacao medios", L"Midden vasthouden", L"Podtrzymanie srodka", L"Orta tutma zarfı");
	case 6:
		return LL14(L"高音ホールド", L"Treble hold envelope", L"Maintien aigu", L"Sostegno acuti",
			L"Sostenido agudos", L"고음 홀드", L"高音保持", L"ثبات الطبقة العالية", L"Удержание верха",
			L"Hoehen-Halte", L"Sustentacao agudos", L"Hoge vasthouden", L"Podtrzymanie sopranu", L"Tiz tutma zarfı");
	case 7:
		return LL14(L"再トリガー比率", L"Retrigger ratio", L"Ratio redeclenchement", L"Rapport retrigger",
			L"Relacion retrigger", L"재트리거 비율", L"再触发比率", L"نسبة إعادة التشغيل", L"Коэфф. ретриггера",
			L"Retrigger-Verhaeltnis", L"Proporcao retrigger", L"Retrigger-verhouding", L"Wspolczynnik retrigger", L"Yeniden tetikleme orani");
	case 8:
		return LL14(L"低音ピック感度", L"Bass pick sensitivity", L"Sensibilite grave", L"Sensibilita graves",
			L"Sensibilidad graves", L"저음 픽 감도", L"低音拾取灵敏度", L"حساسية اختيار المنخفض", L"Чувствит. баса",
			L"Bass-Empfindlichkeit", L"Sensibilidade graves", L"Bas pick-gevoeligheid", L"Czulosc basu", L"Bas secim hassasiyeti");
	case 9:
		return LL14(L"低中域ピック感度", L"Low-mid pick sensitivity", L"Sensibilite bas-medium", L"Sensibilita medio-bassi",
			L"Sensibilidad bajo-medios", L"저중역 픽 감도", L"低中域拾取灵敏度", L"حساسية اختيار متوسط-منخفض", L"Чувствит. низкой середины",
			L"Tief-Mitten-Empfindlichkeit", L"Sensibilidade medio-graves", L"Laag-midden pick-gevoeligheid", L"Czulosc niskiego srodka", L"Alt-orta secim hassasiyeti");
	case 10:
		return LL14(L"メロディピック感度", L"Melody pick sensitivity", L"Sensibilite melodie", L"Sensibilita melodia",
			L"Sensibilidad melodia", L"멜로디 픽 감도", L"旋律拾取灵敏度", L"حساسية اختيار اللحن", L"Чувствит. мелодии",
			L"Melodie-Empfindlichkeit", L"Sensibilidade melodia", L"Melodie pick-gevoeligheid", L"Czulosc melodii", L"Melodi secim hassasiyeti");
	case 11:
		return LL14(L"高音ピック感度", L"Treble pick sensitivity", L"Sensibilite aigu", L"Sensibilita acuti",
			L"Sensibilidad agudos", L"고음 픽 감도", L"高音拾取灵敏度", L"حساسية اختيار العالي", L"Чувствит. верха",
			L"Hoehen-Empfindlichkeit", L"Sensibilidade agudos", L"Hoge pick-gevoeligheid", L"Czulosc sopranu", L"Tiz secim hassasiyeti");
	case 12:
		return LL14(L"倍音ゴースト margin", L"Harmonic ghost margin", L"Marge fantome harmonique", L"Margine armonico fantasma",
			L"Margen fantasma armonico", L"배음 고스트 margin", L"泛音幽灵余量", L"هامش شبح التوافق", L"Запас гармон. призрака",
			L"Oberton-Geist-Marge", L"Margem fantasma harmonico", L"Harmonische spook-marge", L"Margines ducha harmonicznego", L"Armonik hayalet marjini");
	case 13:
		return LL14(L"倍音棄却比率", L"Harmonic reject ratio", L"Ratio rejet harmonique", L"Rapport scarto armonico",
			L"Relacion rechazo armonico", L"배음 기각 비율", L"泛音拒绝比率", L"نسبة رفض التوافق", L"Коэфф. отклонения обертонов",
			L"Oberton-Ablehnungsverhaeltnis", L"Proporcao rejeicao harmonica", L"Harmonische afwijzingsverhouding", L"Wspolczynnik odrzucenia harmonicznych", L"Armonik reddi orani");
	case 14:
		return LL14(L"音色プロファイル", L"Timbre profile confidence", L"Confiance profil timbre", L"Confidenza profilo timbrico",
			L"Confianza perfil timbrico", L"음색 프로파일", L"音色轮廓置信度", L"ثقة ملف الطابع", L"Уверенность профиля тембра",
			L"Klangprofil-Vertrauen", L"Confianca perfil timbrico", L"Timbreprofiel-vertrouwen", L"Pewnosc profilu barwy", L"Timbre profil guveni");
	case 15:
		return LL14(L"ノイズフロア", L"Noise floor base", L"Plancher de bruit", L"Piano del rumore",
			L"Suelo de ruido", L"노이즈 플로어", L"噪声底", L"أرضية الضوضاء", L"Базовый шумовой порог",
			L"Rauschuntergrenze", L"Piso de ruido", L"Ruisvloer", L"Prog szumu", L"Gurultu tabani");
	case 16:
		return LL14(L"オンセット delta", L"Onset delta threshold", L"Seuil delta attaque", L"Soglia delta attacco",
			L"Umbral delta ataque", L"온셋 delta", L"起音 delta 阈值", L"عتبة دلتا البداية", L"Порог delta атаки",
			L"Onset-Delta-Schwelle", L"Limiar delta ataque", L"Onset-delta-drempel", L"Prog delta ataku", L"Baslangic delta esigi");
	default:
		return L"";
	}
}

static LPCTSTR PrTuneRowTip(int idx)
{
	switch (idx) {
	case 0:
		return LL14(L"全体の無音判定しきい値。上げると弱い音を無音扱いにしやすくなります。",
			L"Overall silence threshold. Higher treats weaker sounds as silence.",
			L"Seuil de silence global. Plus haut = sons faibles traites comme silence.",
			L"Soglia silenzio globale. Piu alta = suoni deboli come silenzio.",
			L"Umbral de silencio global. Mas alto = sonidos debiles como silencio.",
			L"전체 무음 판정 임계값. 높일수록 약한 소리를 무음으로 봅니다.",
			L"整体静音阈值。越高越易将弱音判为静音。",
			L"عتبة الصمت العامة. الأعلى يعامل الأصوات الضعيفة كصمت.",
			L"Общий порог тишины. Выше — слабые звуки считаются тишиной.",
			L"Gesamt-Stille-Schwelle. Hoeher = schwache Toene als Stille.",
			L"Limiar de silencio global. Mais alto = sons fracos como silencio.",
			L"Algemene stilte-drempel. Hoger = zwakke geluiden als stilte.",
			L"Ogolny prog ciszy. Wyzej = slabe dzwieki jako cisza.",
			L"Genel sessizlik esigi. Yuksek = zayif sesler sessiz sayilir.");
	case 1:
		return LL14(L"低音域の無音判定。ベースや左-hand 低音の検出感度に影響します。",
			L"Bass-band silence threshold. Affects low-note detection.",
			L"Seuil de silence graves. Influence la detection des notes basses.",
			L"Soglia silenzio graves. Influenza il rilevamento delle note basse.",
			L"Umbral de silencio graves. Afecta la deteccion de notas graves.",
			L"저음역 무음 판정. 낮은 음 검출에 영향합니다.",
			L"低音区静音阈值。影响低音检测。",
			L"عتبة صمت الطبقة المنخفضة. تؤثر على كشف النوتات المنخفضة.",
			L"Порог тишины баса. Влияет на обнаружение низких нот.",
			L"Bass-Stille-Schwelle. Beeinflusst Tiefenerkennung.",
			L"Limiar de silencio graves. Afeta deteccao de notas graves.",
			L"Bas stilte-drempel. Beinvloedt lage nootdetectie.",
			L"Prog ciszy basu. Wplywa na wykrywanie niskich nut.",
			L"Bas bandi sessizlik esigi. Dusuk nota algilamayi etkiler.");
	case 2:
		return LL14(L"中音域の無音判定。コードやメロディ中域の検出に影響します。",
			L"Mid-band silence threshold. Affects mid-range note detection.",
			L"Seuil de silence medium. Influence le milieu du spectre.",
			L"Soglia silenzio medi. Influenza il rilevamento medio.",
			L"Umbral de silencio medios. Afecta deteccion media.",
			L"중음역 무음 판정. 중역 음 검출에 영향합니다.",
			L"中音区静音阈值。影响中音检测。",
			L"عتبة صمت الطبقة المتوسطة. تؤثر على النوتات المتوسطة.",
			L"Порог тишины середины. Влияет на средний диапазон.",
			L"Mitten-Stille-Schwelle. Beeinflusst Mitten-Erkennung.",
			L"Limiar de silencio medios. Afeta deteccao media.",
			L"Midden stilte-drempel. Beinvloedt middenbereik.",
			L"Prog ciszy srodka. Wplywa na srodkowy zakres.",
			L"Orta band sessizlik esigi. Orta aralik algilamayi etkiler.");
	case 3:
		return LL14(L"高音域の無音判定。装飾音や高い旋律の検出に影響します。",
			L"Treble-band silence threshold. Affects high-note detection.",
			L"Seuil de silence aigu. Influence les notes aigues.",
			L"Soglia silenzio acuti. Influenza le note alte.",
			L"Umbral de silencio agudos. Afecta notas agudas.",
			L"고음역 무음 판정. 높은 음 검출에 영향합니다.",
			L"高音区静音阈值。影响高音检测。",
			L"عتبة صمت الطبقة العالية. تؤثر على النوتات العالية.",
			L"Порог тишины верха. Влияет на высокие ноты.",
			L"Hoehen-Stille-Schwelle. Beeinflusst hohe Toene.",
			L"Limiar de silencio agudos. Afeta notas agudas.",
			L"Hoge stilte-drempel. Beinvloedt hoge noten.",
			L"Prog ciszy sopranu. Wplywa na wysokie nuty.",
			L"Tiz band sessizlik esigi. Yuksek notalari etkiler.");
	case 4:
		return LL14(L"低音ノートの持続(ホールド)時間。上げると短い音も長く表示されます。",
			L"Bass note hold envelope. Higher keeps short bass notes visible longer.",
			L"Maintien des notes graves. Plus haut = notes courtes plus longues.",
			L"Sostegno note basse. Piu alto = note corte piu lunghe.",
			L"Sostenido graves. Mas alto = notas cortas mas visibles.",
			L"저음 홀드. 높이면 짧은 저음도 길게 표시됩니다.",
			L"低音保持。越高短低音显示越久。",
			L"ثبات النوتات المنخفضة. الأعلى يُبقي النوتات القصيرة أطول.",
			L"Удержание басовых нот. Выше — короткие ноты дольше видны.",
			L"Bass-Halte. Hoeher = kurze Toene laenger sichtbar.",
			L"Sustentacao graves. Mais alto = notas curtas mais longas.",
			L"Bas vasthouden. Hoger = korte noten langer zichtbaar.",
			L"Podtrzymanie basu. Wyzej = krotsze nuty dluzej widoczne.",
			L"Bas tutma. Yuksek = kisa notalar daha uzun gorunur.");
	case 5:
		return LL14(L"中音ノートの持続時間。和音やメロディの余韻表示に影響します。",
			L"Mid note hold envelope. Affects sustain display for chords/melody.",
			L"Maintien des notes medium. Influence la tenue des accords.",
			L"Sostegno note medie. Influenza la sustain di accordi/melodia.",
			L"Sostenido medios. Afecta la duracion de acordes/melodia.",
			L"중음 홀드. 화음/멜로디 지속 표시에 영향합니다.",
			L"中音保持。影响和声/旋律的持续显示。",
			L"ثبات النوتات المتوسطة. يؤثر على استدامة الأكورد واللحن.",
			L"Удержание средних нот. Влияет на отображение аккордов.",
			L"Mitten-Halte. Beeinflusst Sustain von Akkorden/Melodie.",
			L"Sustentacao medios. Afeta sustain de acordes/melodia.",
			L"Midden vasthouden. Beinvloedt sustain van akkoorden.",
			L"Podtrzymanie srodka. Wplywa na sustain akordow.",
			L"Orta tutma. Akor/melodi sustain gosterimini etkiler.");
	case 6:
		return LL14(L"高音ノートの持続時間。短い装飾音の表示長に影響します。",
			L"Treble note hold envelope. Affects short high-note display length.",
			L"Maintien des notes aigues. Influence les notes courtes aigues.",
			L"Sostegno note alte. Influenza note alte brevi.",
			L"Sostenido agudos. Afecta notas agudas cortas.",
			L"고음 홀드. 짧은 고음 표시 길이에 영향합니다.",
			L"高音保持。影响短高音显示长度。",
			L"ثبات النوتات العالية. يؤثر على طول عرض النوتات القصيرة.",
			L"Удержание высоких нот. Влияет на короткие верхние ноты.",
			L"Hoehen-Halte. Beeinflusst kurze hohe Toene.",
			L"Sustentacao agudos. Afeta notas agudas curtas.",
			L"Hoge vasthouden. Beinvloedt korte hoge noten.",
			L"Podtrzymanie sopranu. Wplywa na krotkie wysokie nuty.",
			L"Tiz tutma. Kisa tiz notalarin gorunumunu etkiler.");
	case 7:
		return LL14(L"同じ音の再トリガー感度。上げると同音の再検出が起きやすくなります。",
			L"Retrigger ratio. Higher re-detects the same pitch more easily.",
			L"Ratio de redeclenchement. Plus haut = re-detection plus facile.",
			L"Rapporto retrigger. Piu alto = rilevamento ripetuto piu facile.",
			L"Relacion retrigger. Mas alto = re-detecta la misma nota.",
			L"재트리거 비율. 높이면 같은 음을 다시 검출하기 쉽습니다.",
			L"再触发比率。越高越易重复检测同音。",
			L"نسبة إعادة التشغيل. الأعلى يعيد كشف نفس النغمة بسهولة.",
			L"Коэфф. ретриггера. Выше — повторное обнаружение той же ноты.",
			L"Retrigger-Verhaeltnis. Hoeher = gleiche Tonhoehe oefter neu.",
			L"Proporcao retrigger. Mais alto = re-detecta mesma nota.",
			L"Retrigger-verhouding. Hoger = zelfde toon opnieuw detecteren.",
			L"Wspolczynnik retrigger. Wyzej = latwiej ponownie ta sama nuta.",
			L"Yeniden tetikleme. Yuksek = ayni perde tekrar algilanir.");
	case 8:
		return LL14(L"低音域のピッチ検出感度。上げると弱いベース音も拾いやすくなります。",
			L"Bass pick sensitivity. Higher detects weaker bass pitches.",
			L"Sensibilite de detection grave. Plus haut = basses faibles detectees.",
			L"Sensibilita rilevamento graves. Piu alta = bassi deboli.",
			L"Sensibilidad graves. Mas alta = detecta graves debiles.",
			L"저음 픽 감도. 높이면 약한 저음도 검출합니다.",
			L"低音拾取灵敏度。越高越易检测弱低音。",
			L"حساسية اختيار المنخفض. الأعلى يكشف النوتات المنخفضة الضعيفة.",
			L"Чувствительность баса. Выше — слабые низкие ноты.",
			L"Bass-Empfindlichkeit. Hoeher = schwache Basstoene.",
			L"Sensibilidade graves. Mais alta = graves fracos.",
			L"Bas pick-gevoeligheid. Hoger = zwakke basnoten.",
			L"Czulosc basu. Wyzej = slabe niskie nuty.",
			L"Bas secim hassasiyeti. Yuksek = zayif bas notalari.");
	case 9:
		return LL14(L"低中域のピッチ検出感度。ギター/ピアノ中低域の拾い方に影響します。",
			L"Low-mid pick sensitivity. Affects guitar/piano low-mid detection.",
			L"Sensibilite bas-medium. Influence le milieu-grave.",
			L"Sensibilita medio-bassi. Influenza chitarra/piano medio-bassi.",
			L"Sensibilidad bajo-medios. Afecta deteccion bajo-media.",
			L"저중역 픽 감도. 기타/피아노 저중역 검출에 영향합니다.",
			L"低中域拾取灵敏度。影响吉他/钢琴低中域。",
			L"حساسية اختيار متوسط-منخفض. تؤثر على الجيتار/البيانو.",
			L"Чувствительность низкой середины. Влияет на гитару/фортепiano.",
			L"Tief-Mitten-Empfindlichkeit. Beeinflusst Gitarre/Klavier.",
			L"Sensibilidade medio-graves. Afeta guitarra/piano.",
			L"Laag-midden pick-gevoeligheid. Beinvloedt gitaar/piano.",
			L"Czulosc niskiego srodka. Wplywa na gitare/pianino.",
			L"Alt-orta secim hassasiyeti. Gitar/piyano alt-ortasini etkiler.");
	case 10:
		return LL14(L"メロディ域のピッチ検出感度。主旋律ラインの拾い方に影響します。",
			L"Melody pick sensitivity. Affects main melody line detection.",
			L"Sensibilite melodie. Influence la ligne melodique principale.",
			L"Sensibilita melodia. Influenza la linea melodica principale.",
			L"Sensibilidad melodia. Afecta la linea melodica principal.",
			L"멜로디 픽 감도. 주선율 검출에 영향합니다.",
			L"旋律拾取灵敏度。影响主旋律检测。",
			L"حساسية اختيار اللحن. تؤثر على خط اللحن الرئيسي.",
			L"Чувствительность мелодии. Влияет на основную линию.",
			L"Melodie-Empfindlichkeit. Beeinflusst Hauptmelodie.",
			L"Sensibilidade melodia. Afeta linha melodica principal.",
			L"Melodie pick-gevoeligheid. Beinvloedt hoofdmelodie.",
			L"Czulosc melodii. Wplywa na glowna linie melodyczna.",
			L"Melodi secim hassasiyeti. Ana melodi hattini etkiler.");
	case 11:
		return LL14(L"高音域のピッチ検出感度。上げると弱い高音も拾いやすくなります。",
			L"Treble pick sensitivity. Higher detects weaker high pitches.",
			L"Sensibilite aigu. Plus haut = aigus faibles detectes.",
			L"Sensibilita acuti. Piu alta = acuti deboli.",
			L"Sensibilidad agudos. Mas alta = agudos debiles.",
			L"고음 픽 감도. 높이면 약한 고음도 검출합니다.",
			L"高音拾取灵敏度。越高越易检测弱高音。",
			L"حساسية اختيار العالي. الأعلى يكشف النوتات العالية الضعيفة.",
			L"Чувствительность верха. Выше — слабые высокие ноты.",
			L"Hoehen-Empfindlichkeit. Hoeher = schwache hohe Toene.",
			L"Sensibilidade agudos. Mais alta = agudos fracos.",
			L"Hoge pick-gevoeligheid. Hoger = zwakke hoge noten.",
			L"Czulosc sopranu. Wyzej = slabe wysokie nuty.",
			L"Tiz secim hassasiyeti. Yuksek = zayif tiz notalar.");
	case 12:
		return LL14(L"倍音ゴースト抑制の余裕。上げると倍音由来の誤検出(ゴースト)を減らしやすくなります。",
			L"Harmonic ghost margin. Higher reduces false harmonic detections.",
			L"Marge fantome harmonique. Plus haut = moins de fausses harmoniques.",
			L"Margine armonico fantasma. Piu alto = meno falsi armonici.",
			L"Margen fantasma armonico. Mas alto = menos falsos armonicos.",
			L"배음 고스트 margin. 높이면 오검출 고스트가 줄어듭니다.",
			L"泛音幽灵余量。越高越减少泛音误检。",
			L"هامش شبح التوافق. الأعلى يقلل الكشف الخاطئ.",
			L"Запас гармон. призрака. Выше — меньше ложных обертонов.",
			L"Oberton-Geist-Marge. Hoeher = weniger Fehl-Obertoene.",
			L"Margem fantasma harmonico. Mais alto = menos falsos harmonicos.",
			L"Harmonische spook-marge. Hoger = minder vals harmonisch.",
			L"Margines ducha harmonicznego. Wyzej = mniej falszywych.",
			L"Armonik hayalet marjini. Yuksek = yanlis armonik azalir.");
	case 13:
		return LL14(L"倍音棄却の厳しさ。上げると倍音候補をより積極的に除外します。",
			L"Harmonic reject ratio. Higher rejects more harmonic candidates.",
			L"Ratio rejet harmonique. Plus haut = plus de rejets.",
			L"Rapport scarto armonico. Piu alto = piu scarti.",
			L"Relacion rechazo armonico. Mas alto = mas rechazos.",
			L"배음 기각 비율. 높이면 배음 후보를 더 많이 제외합니다.",
			L"泛音拒绝比率。越高越积极排除泛音候选。",
			L"نسبة رفض التوافق. الأعلى يرفض مرشحي التوافق أكثر.",
			L"Коэфф. отклонения обертонов. Выше — больше отклонений.",
			L"Oberton-Ablehnungsverhaeltnis. Hoeher = mehr Ablehnung.",
			L"Proporcao rejeicao harmonica. Mais alto = mais rejeicoes.",
			L"Harmonische afwijzingsverhouding. Hoger = meer afwijzing.",
			L"Wspolczynnik odrzucenia harmonicznych. Wyzej = wiecej odrzucen.",
			L"Armonik reddi orani. Yuksek = daha cok aday elenir.");
	case 14:
		return LL14(L"音色プロファイル一致の厳しさ。上げると音色が合わない候補を除外しやすくなります。",
			L"Timbre profile confidence. Higher filters mismatched timbre candidates.",
			L"Confiance profil timbre. Plus haut = filtre les timbres incoherents.",
			L"Confidenza profilo timbrico. Piu alta = filtra timbri diversi.",
			L"Confianza perfil timbrico. Mas alta = filtra timbres distintos.",
			L"음색 프로파일. 높이면 다른 음색 후보를 더 잘 제외합니다.",
			L"音色轮廓置信度。越高越过滤音色不符的候选。",
			L"ثقة ملف الطابع. الأعلى يصفّي المرشحين غير المتطابقين.",
			L"Уверенность профиля тембра. Выше — фильтр несовпадений.",
			L"Klangprofil-Vertrauen. Hoeher = filtert falsche Klangfarbe.",
			L"Confianca perfil timbrico. Mais alto = filtra timbres.",
			L"Timbreprofiel-vertrouwen. Hoger = filtert verkeerde timbres.",
			L"Pewnosc profilu barwy. Wyzej = filtruje niedopasowane barwy.",
			L"Timbre profil guveni. Yuksek = uyumsuz adaylari eler.");
	case 15:
		return LL14(L"ノイズフロア基準。上げると小さなノイズを無視し、下げると微細な音も拾います。",
			L"Noise floor base. Higher ignores small noise; lower picks finer detail.",
			L"Plancher de bruit. Plus haut = ignore le bruit; plus bas = detail fin.",
			L"Piano del rumore. Piu alto = ignora rumore; piu basso = piu dettaglio.",
			L"Suelo de ruido. Mas alto = ignora ruido; mas bajo = mas detalle.",
			L"노이즈 플로어. 높이면 잡음 무시, 낮추면 미세한 음도 검출.",
			L"噪声底。越高越忽略噪声，越低越拾取细节。",
			L"أرضية الضوضاء. الأعلى يتجاهل الضوضاء؛ الأدنى يلتقط التفاصيل.",
			L"Базовый шумовой порог. Выше — игнор шума; ниже — детали.",
			L"Rauschuntergrenze. Hoeher = Rauschen ignorieren.",
			L"Piso de ruido. Mais alto = ignora ruido; mais baixo = detalhe.",
			L"Ruisvloer. Hoger = ruis negeren; lager = fijner detail.",
			L"Prog szumu. Wyzej = ignoruje szum; nizej = wiecej detali.",
			L"Gurultu tabani. Yuksek = gurultuyu yok sayar; dusuk = ince detay.");
	case 16:
		return LL14(L"オンセット(音の立ち上がり)判定。上げると急な立ち上がりのみ検出しやすくなります。",
			L"Onset delta threshold. Higher favors sharp note attacks only.",
			L"Seuil delta attaque. Plus haut = attaques nettes seulement.",
			L"Soglia delta attacco. Piu alta = solo attacchi netti.",
			L"Umbral delta ataque. Mas alto = solo ataques bruscos.",
			L"온셋 delta. 높이면 급한 시작만 검출하기 쉽습니다.",
			L"起音 delta 阈值。越高越只检测明显起音。",
			L"عتبة دلتا البداية. الأعلى يفضّل بدايات حادة فقط.",
			L"Порог delta атаки. Выше — только резкие атаки.",
			L"Onset-Delta-Schwelle. Hoeher = nur scharfe Ansaetze.",
			L"Limiar delta ataque. Mais alto = so ataques bruscos.",
			L"Onset-delta-drempel. Hoger = alleen scherpe attacks.",
			L"Prog delta ataku. Wyzej = tylko ostre ataki.",
			L"Baslangic delta esigi. Yuksek = sadece keskin baslangiclar.");
	default:
		return L"";
	}
}

void CPianoRollTuneDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this))
		return;
	for (int i = 0; i < m_rowCount; ++i) {
		LPCTSTR tip = PrTuneRowTip(i);
		if (!tip || !*tip) continue;
		if (m_lbl[i].GetSafeHwnd())
			m_tooltip.AddTool(&m_lbl[i], tip);
		if (m_slider[i].GetSafeHwnd())
			m_tooltip.AddTool(&m_slider[i], tip);
		if (m_val[i].GetSafeHwnd())
			m_tooltip.AddTool(&m_val[i], tip);
	}
	if (m_reset.GetSafeHwnd())
		m_tooltip.AddTool(&m_reset, LL14(L"すべての検出パラメータを100%（既定）に戻します。",
			L"Reset all detection parameters to 100% (defaults).",
			L"Reinitialiser tous les parametres a 100%.",
			L"Ripristina tutti i parametri al 100%.",
			L"Restablecer todos los parametros al 100%.",
			L"모든 검출 파라미터를 100%로 되돌립니다.",
			L"将所有检测参数重置为100%。",
			L"إعادة جميع معلمات الكشف إلى 100%.",
			L"Сбросить все параметры на 100%.",
			L"Alle Parameter auf 100% zuruecksetzen.",
			L"Redefinir todos os parametros para 100%.",
			L"Alle parameters resetten naar 100%.",
			L"Przywroc wszystkie parametry do 100%.",
			L"Tum algilama parametrelerini %100'e sifirla."));
	if (m_help.GetSafeHwnd())
		m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 480, 12000);
}

void CPianoRollTuneDlg::StyleRows()
{
#if CCUSTOM_AERO_SUPPORT
	const BOOL bAero = CCC_IsAeroEnabled();
#else
	const BOOL bAero = FALSE;
#endif
	for (int i = 0; i < m_rowCount; ++i) {
		if (m_lbl[i].GetSafeHwnd())
			PrTuneInitStatic(m_lbl[i], &m_fontRow, bAero);
		if (m_val[i].GetSafeHwnd())
			PrTuneInitStatic(m_val[i], &m_fontRow, bAero);
		if (m_slider[i].GetSafeHwnd()) {
			m_slider[i].SetMode(1);
			m_slider[i].SetAeroMode(bAero);
		}
	}
}

void CPianoRollTuneDlg::SaveWindowPos()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic())
		return;
	CRect rc;
	GetWindowRect(&rc);
	savedata.prTunex = rc.left;
	savedata.prTuney = rc.top;
}

void CPianoRollTuneDlg::ApplyDialogSize()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	CRect rcWin, rcClient;
	GetWindowRect(&rcWin);
	GetClientRect(&rcClient);
	const int ncH = rcWin.Height() - rcClient.Height();
	const int ncW = rcWin.Width() - rcClient.Width();
	const int w = kDlgClientW + ncW;
	const int h = kDlgClientH + ncH;
	const int x = savedata.prTunex;
	const int y = savedata.prTuney;
	if (x != -1 && x > -30000 && x < 30000 && y > -30000 && y < 30000)
		SetWindowPos(NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
	else {
		SetWindowPos(NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		CenterWindow();
	}
	LayoutRows();
}

BOOL CPianoRollTuneDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(L"ピアノロール検出パラメータ", L"Piano roll detection parameters",
		L"Parametres de detection piano roll", L"Parametri rilevamento piano roll",
		L"Parametros de deteccion piano roll", L"피아노롤 검출 파라미터", L"钢琴卷帘检测参数",
		L"معلمات كشف البيانو", L"Параметры обнаружения piano roll", L"Piano-Roll-Erkennungsparameter",
		L"Parametros de deteccao piano roll", L"Piano roll detectieparameters",
		L"Parametry wykrywania piano roll", L"Piano roll algilama parametreleri"));

	{
		LOGFONT lf{};
		if (CFont* pDef = GetFont()) {
			pDef->GetLogFont(&lf);
			if (lf.lfHeight < 0)
				lf.lfHeight = -(abs(lf.lfHeight) * 105 / 100);
			else if (lf.lfHeight > 0)
				lf.lfHeight = lf.lfHeight * 105 / 100;
			if (m_fontRow.GetSafeHandle()) m_fontRow.DeleteObject();
			m_fontRow.CreateFontIndirect(&lf);
		}
	}

	BuildRows();
	for (int i = 0; i < m_rowCount; ++i) {
		const UINT idLbl = (UINT)(3600 + i);
		const UINT idVal = (UINT)(3700 + i);
		const UINT idSl = (UINT)(IDC_PRT_SILENCE + i);
		m_lbl[i].Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
			CRect(0, 0, 1, 1), this, idLbl);
		PrTuneSetLabelText(m_lbl[i], PrTuneRowLabel(i));
		m_slider[i].Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
			CRect(0, 0, 1, 1), this, idSl);
		m_slider[i].SetRange(25, 400, TRUE);
		m_val[i].Create(_T(""), WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOTIFY,
			CRect(0, 0, 1, 1), this, idVal);
		PrTuneSetValueText(m_val[i], 100);
	}
	m_reset.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);
	m_ok.SetGradation(RGB(200, 240, 200), RGB(130, 205, 140), 0, TRUE);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	SetDlgItemText(IDC_PRT_RESET, LL14(L"既定に戻す", L"Reset to defaults", L"Reinitialiser", L"Ripristina", L"Restablecer", L"기본값", L"恢复默认", L"إعادة ضبط", L"Сброс", L"Zuruecksetzen", L"Redefinir", L"Reset", L"Reset", L"Sifirla"));
	SetDlgItemText(IDC_PRT_OK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	StyleRows();
	SyncSlidersFromSavedata();
	EnableMainWindowLock(&savedata.prTuneMainLock);
	ApplyDialogSize();
	LayoutHelpBtn();
	SetupTooltips();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

void CPianoRollTuneDlg::LayoutRows()
{
	if (!::IsWindow(GetSafeHwnd())) return;

	// 固定設計座標(クライアントサイズは ApplyDialogSize で kDlgClientW/H に合わせる)
	const int M = 16;
	const int topBarH = 44;
	const int btnW = 116;
	const int rowH = 36;
	const int lblW = 224;
	const int slW = 170;
	const int valW = 62;
	const int gapLS = 8;
	const int gapSV = 6;
	const int colGap = 18;
	const int colW = lblW + gapLS + slW + gapSV + valW;
	const int rowsPerCol = (m_rowCount + kCols - 1) / kCols;
	const int totalW = colW * kCols + colGap;
	const int xBase = M + max(0, (kDlgClientW - M * 2 - totalW) / 2);
	const int yRows = M + topBarH + 10;

	if (m_reset.GetSafeHwnd())
		m_reset.MoveWindow(M, M, btnW, topBarH);
	const int lockReserve = CCC_MainLockGetReserveWidth(m_hWnd);
	if (m_ok.GetSafeHwnd())
		m_ok.MoveWindow(kDlgClientW - M - btnW - lockReserve, M, btnW, topBarH);

	for (int i = 0; i < m_rowCount; ++i) {
		const int col = i / rowsPerCol;
		const int row = i % rowsPerCol;
		const int x0 = xBase + col * (colW + colGap);
		const int y = yRows + row * rowH;
		const int xSl = x0 + lblW + gapLS;
		const int xVal = xSl + slW + gapSV;
		if (m_lbl[i].GetSafeHwnd())
			m_lbl[i].MoveWindow(x0, y + 5, lblW, 24);
		if (m_slider[i].GetSafeHwnd())
			m_slider[i].MoveWindow(xSl, y + 3, slW, 28);
		if (m_val[i].GetSafeHwnd())
			m_val[i].MoveWindow(xVal, y + 5, valW, 24);
	}
	CCC_MainLockBringToFront(m_hWnd);
	LayoutHelpBtn();
}

void CPianoRollTuneDlg::SyncSlidersFromSavedata()
{
	for (int i = 0; i < m_rowCount; ++i) {
		if (!m_pPct[i] || !m_slider[i].GetSafeHwnd()) continue;
		int v = *m_pPct[i];
		if (v < 25 || v > 400) v = 100;
		m_slider[i].SetPos(v);
	}
	UpdateValueLabels();
}

void CPianoRollTuneDlg::SyncSavedataFromSliders()
{
	for (int i = 0; i < m_rowCount; ++i) {
		if (!m_pPct[i] || !m_slider[i].GetSafeHwnd()) continue;
		*m_pPct[i] = ClampPct(m_slider[i].GetPos());
	}
	MpPersistSavedataQuick();
}

void CPianoRollTuneDlg::UpdateValueLabels()
{
	for (int i = 0; i < m_rowCount; ++i) {
		if (!m_val[i].GetSafeHwnd() || !m_slider[i].GetSafeHwnd()) continue;
		PrTuneSetValueText(m_val[i], m_slider[i].GetPos());
	}
}

void CPianoRollTuneDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
	UpdateValueLabels();
	SyncSavedataFromSliders();
}

void CPianoRollTuneDlg::OnReset()
{
	savedata.prTuneSilencePct = 100;
	savedata.prTuneBandSilBassPct = 100;
	savedata.prTuneBandSilMidPct = 100;
	savedata.prTuneBandSilTrePct = 100;
	savedata.prTuneHoldBassPct = 100;
	savedata.prTuneHoldMidPct = 100;
	savedata.prTuneHoldTrePct = 100;
	savedata.prTuneRetrigPct = 100;
	savedata.prTunePickBassPct = 100;
	savedata.prTunePickLowMidPct = 100;
	savedata.prTunePickMelodyPct = 100;
	savedata.prTunePickTrePct = 100;
	savedata.prTuneHarmGhostPct = 100;
	savedata.prTuneHarmRejectPct = 100;
	savedata.prTuneHarmProfPct = 100;
	savedata.prTuneAbsFloorPct = 100;
	savedata.prTuneOnsetDeltaPct = 100;
	SyncSlidersFromSavedata();
	MpPersistSavedataQuick();
}

void CPianoRollTuneDlg::OnOk()
{
	SyncSavedataFromSliders();
	SaveWindowPos();
	savedata.prTunewindow = 0;
	DestroyWindow();
}

void CPianoRollTuneDlg::OnClose()
{
	SaveWindowPos();
	savedata.prTunewindow = 0;
	DestroyWindow();
}

void CPianoRollTuneDlg::OnDestroy()
{
	SaveWindowPos();
	// prTunewindow は落とさない（アプリ終了時も次回復元）。ユーザー閉じは OnClose/OnOk。
	if (g_prtHelpDlg && ::IsWindow(g_prtHelpDlg->GetSafeHwnd()))
		g_prtHelpDlg->DestroyWindow();
	CCustomBlurDialogExBase::OnDestroy();
}

void CPianoRollTuneDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CPianoRollTuneDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
	SaveWindowPos();
}

void CPianoRollTuneDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CPianoRollTuneDlg::ShowHelpSheet()
{
	if (g_prtHelpDlg && ::IsWindow(g_prtHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_prtHelpDlg, this);
		return;
	}
	if (g_prtHelpDlg && !::IsWindow(g_prtHelpDlg->GetSafeHwnd()))
		g_prtHelpDlg = nullptr;
	CPrtHelpDlg* dlg = new CPrtHelpDlg(this);
	if (!dlg->Create(IDD_PRT_HELP, this)) {
		delete dlg;
		return;
	}
	g_prtHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CPianoRollTuneDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

BOOL CPianoRollTuneDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
