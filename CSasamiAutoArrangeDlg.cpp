#include "stdafx.h"
#include "ogg.h"
#include "CSasamiAutoArrange.h"
#include "CCustomControl.h"

namespace {

enum {
	IDC_SAA_LSTYLE = 4920,
	IDC_SAA_LPITCH = 4921,
	IDC_SAA_LINTRO = 4922,
	IDC_SAA_LBREAK = 4923,
	IDC_SAA_LEND = 4924
};

class CSasamiAutoArrangeDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiAutoArrangeDlg)
public:
	CSasamiAutoArrangeDlg(const ScAutoArrangeHost* host, CWnd* parent);
	virtual ~CSasamiAutoArrangeDlg();

protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPreview();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	DECLARE_MESSAGE_MAP()

	void LayoutChrome();
	void ReadOpt(ScAutoArrangeOpt* o);
	void Restore();
	int ApplyNow(ScAutoArrangeResult* r);
	void ShowResult(const ScAutoArrangeResult& r);

	const ScAutoArrangeHost* m_host;
	ScEvent* m_snap;
	int m_snapN;
	int m_keep;
	int m_uiSnap;
	int m_vis[32], m_mute[32], m_solo[32], m_clef[32];
	CCustomStatic m_styleLbl, m_pitchLbl, m_introLbl, m_breakLbl, m_endLbl, m_hint, m_status;
	CCustomComboBox m_style, m_pitch, m_introBars, m_breakBars, m_endBars;
	CCustomCheckBox m_drum, m_bass, m_chord, m_melVol, m_melPitch, m_intro, m_brk, m_ending;
	CCustomStandardButton m_preview, m_ok, m_cancel;
};

IMPLEMENT_DYNAMIC(CSasamiAutoArrangeDlg, CCustomBlurDialogExBase)

BEGIN_MESSAGE_MAP(CSasamiAutoArrangeDlg, CCustomBlurDialogExBase)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_BN_CLICKED(IDC_SAA_PREVIEW, OnPreview)
	ON_BN_CLICKED(IDOK, OnOK)
	ON_BN_CLICKED(IDCANCEL, OnCancel)
END_MESSAGE_MAP()

CSasamiAutoArrangeDlg::CSasamiAutoArrangeDlg(const ScAutoArrangeHost* host, CWnd* parent)
	: CCustomBlurDialogExBase(IDD_SASAMI_AUTO_ARRANGE, parent)
	, m_host(host), m_snap(NULL), m_snapN(0), m_keep(0), m_uiSnap(0)
{
}

CSasamiAutoArrangeDlg::~CSasamiAutoArrangeDlg()
{
	if (!m_keep) Restore();
	if (m_snap) HeapFree(GetProcessHeap(), 0, m_snap);
}

void CSasamiAutoArrangeDlg::ReadOpt(ScAutoArrangeOpt* o)
{
	memset(o, 0, sizeof(*o));
	o->style = m_style.GetCurSel();
	if (o->style < 0 || o->style >= SC_AS_COUNT) o->style = SC_AS_16BEAT;
	if (m_drum.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_DRUM;
	if (m_bass.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_BASS;
	if (m_chord.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_CHORD;
	if (m_melVol.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_MEL_VOL;
	if (m_melPitch.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_MEL_PITCH;
	if (m_intro.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_INTRO;
	if (m_brk.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_BREAK;
	if (m_ending.GetCheck() == BST_CHECKED) o->jobs |= SC_AJ_ENDING;
	static const int kIntro[] = { 2, 4, 8 };
	static const int kBreak[] = { 2, 4 };
	static const int kEnd[] = { 1, 2, 4 };
	int ii = m_introBars.GetCurSel();
	int bi = m_breakBars.GetCurSel();
	int ei = m_endBars.GetCurSel();
	o->introBars = kIntro[(ii >= 0 && ii < 3) ? ii : 1];
	o->breakBars = kBreak[(bi >= 0 && bi < 2) ? bi : 0];
	o->endBars = kEnd[(ei >= 0 && ei < 3) ? ei : 1];
	int pm = m_pitch.GetCurSel();
	if (pm < 0) pm = 1;
	o->pitchMode = pm + 1;
	o->isFm = m_host->isFm;
	o->curPart = m_host->curPart;
	o->defMeterN = m_host->defMeterN;
	o->defMeterD = m_host->defMeterD;
	o->defKey = m_host->defKey;
	o->ui = m_host->ui;
}

void CSasamiAutoArrangeDlg::Restore()
{
	if (!m_host || !m_snap || !m_host->ev || !m_host->evCount) return;
	if (m_snapN > 0)
		memcpy(m_host->ev, m_snap, sizeof(ScEvent) * (size_t)m_snapN);
	*m_host->evCount = m_snapN;
	if (m_uiSnap && m_host->ui) {
		memcpy(m_host->ui->visible, m_vis, sizeof(m_vis));
		memcpy(m_host->ui->mute, m_mute, sizeof(m_mute));
		memcpy(m_host->ui->solo, m_solo, sizeof(m_solo));
		memcpy(m_host->ui->clef, m_clef, sizeof(m_clef));
	}
}

int CSasamiAutoArrangeDlg::ApplyNow(ScAutoArrangeResult* r)
{
	Restore();
	ScAutoArrangeOpt opt;
	ReadOpt(&opt);
	return ScAutoArrangeApply(m_host->ev, m_host->evCount, m_host->evMax, &opt, r);
}

void CSasamiAutoArrangeDlg::ShowResult(const ScAutoArrangeResult& r)
{
	if (m_status.GetSafeHwnd())
		m_status.SetWindowText(r.msg);
}

BOOL CSasamiAutoArrangeDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(L"自動伴奏", L"Auto accompaniment", L"Accompagnement auto", L"Accompagnamento auto", L"Acompañamiento auto",
		L"자동 반주", L"自动伴奏", L"مرافقة تلقائية", L"Автоаккомпанемент", L"Auto-Begleitung", L"Acompanhamento auto", L"Auto-begeleiding", L"Automatyczny akompaniament", L"Otomatik eşlik"));
	if (!m_host || !m_host->ev || !m_host->evCount || *m_host->evCount < 0) {
		EndDialog(IDCANCEL);
		return TRUE;
	}
	m_snapN = *m_host->evCount;
	m_snap = (ScEvent*)HeapAlloc(GetProcessHeap(), 0, sizeof(ScEvent) * (size_t)(m_snapN > 0 ? m_snapN : 1));
	if (!m_snap) {
		EndDialog(IDCANCEL);
		return TRUE;
	}
	if (m_snapN > 0)
		memcpy(m_snap, m_host->ev, sizeof(ScEvent) * (size_t)m_snapN);
	if (m_host->ui) {
		memcpy(m_vis, m_host->ui->visible, sizeof(m_vis));
		memcpy(m_mute, m_host->ui->mute, sizeof(m_mute));
		memcpy(m_solo, m_host->ui->solo, sizeof(m_solo));
		memcpy(m_clef, m_host->ui->clef, sizeof(m_clef));
		m_uiSnap = 1;
	}

	CRect z(0, 0, 10, 10);
	m_styleLbl.Create(LL14(L"パターン", L"Pattern", L"Motif", L"Schema", L"Patrón", L"패턴", L"型", L"نمط", L"Рисунок", L"Muster", L"Padrão", L"Patroon", L"Wzór", L"Kalıp"),
		WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_LSTYLE);
	m_style.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 200), this, IDC_SAA_STYLE);
	for (int i = 0; i < SC_AS_COUNT; i++)
		m_style.AddString(ScAutoStyleName(i));
	m_style.SetCurSel(0);

	m_drum.Create(LL14(L"ドラム", L"Drums", L"Batterie", L"Batteria", L"Batería", L"드럼", L"鼓", L"طبول", L"Ударные", L"Drums", L"Bateria", L"Drums", L"Perkusja", L"Davul"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_DRUM);
	m_bass.Create(LL14(L"ベース", L"Bass", L"Basse", L"Basso", L"Bajo", L"베이스", L"贝斯", L"باص", L"Бас", L"Bass", L"Baixo", L"Bas", L"Bas", L"Bas"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_BASS);
	m_chord.Create(LL14(L"コード", L"Chords", L"Accords", L"Accordi", L"Acordes", L"코드", L"和弦", L"أكورد", L"Аккорды", L"Akkorde", L"Acordes", L"Akkoorden", L"Akordy", L"Akorlar"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_CHORD);
	m_melVol.Create(LL14(L"メロディの音量", L"Melody volume", L"Volume mélodie", L"Volume melodia", L"Volumen melodía", L"멜로디 음량", L"旋律音量", L"شدة اللحن", L"Громкость мелодии", L"Melodielautstärke", L"Volume da melodia", L"Melodievolume", L"Głośność melodii", L"Melodi sesi"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_MELVOL);
	m_melPitch.Create(LL14(L"メロディのピッチ", L"Melody pitch", L"Hauteur mélodie", L"Intonazione melodia", L"Altura melodía", L"멜로디 피치", L"旋律音高", L"طبقة اللحن", L"Высота мелодии", L"Melodietonhöhe", L"Altura da melodia", L"Melodiehoogte", L"Wysokość melodii", L"Melodi perdesi"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_MELPITCH);
	m_drum.SetCheck(BST_CHECKED);
	m_bass.SetCheck(BST_CHECKED);
	m_chord.SetCheck(BST_CHECKED);
	m_melVol.SetCheck(BST_CHECKED);
	m_melPitch.SetCheck(BST_CHECKED);

	m_intro.Create(LL14(L"イントロ", L"Intro", L"Intro", L"Intro", L"Intro", L"인트로", L"前奏", L"مقدمة", L"Вступление", L"Intro", L"Intro", L"Intro", L"Intro", L"Giriş"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_INTRO);
	m_brk.Create(LL14(L"間奏", L"Interlude", L"Interlude", L"Interludio", L"Interludio", L"간주", L"间奏", L"فاصل", L"Проигрыш", L"Zwischenspiel", L"Interlúdio", L"Tussenspel", L"Przerywnik", L"Ara"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_BREAK);
	m_ending.Create(LL14(L"エンディング", L"Ending", L"Fin", L"Finale", L"Final", L"엔딩", L"结尾", L"خاتمة", L"Концовка", L"Schluss", L"Final", L"Slot", L"Zakończenie", L"Bitiş"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_ENDING);
	m_intro.SetCheck(BST_CHECKED);
	m_brk.SetCheck(BST_CHECKED);
	m_ending.SetCheck(BST_CHECKED);

	m_introLbl.Create(LL14(L"イントロ小節", L"Intro bars", L"Mesures d'intro", L"Battute intro", L"Compases de intro", L"인트로 마디", L"前奏小节", L"موازير المقدمة", L"Такты вступления", L"Intro-Takte", L"Compassos de intro", L"Intromaten", L"Takty intra", L"Giriş ölçüleri"),
		WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_LINTRO);
	m_breakLbl.Create(LL14(L"間奏小節", L"Interlude bars", L"Mesures d'interlude", L"Battute interludio", L"Compases de interludio", L"간주 마디", L"间奏小节", L"موازير الفاصل", L"Такты проигрыша", L"Zwischenspiel-Takte", L"Compassos do interlúdio", L"Tussenspelmaten", L"Takty przerywnika", L"Ara ölçüleri"),
		WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_LBREAK);
	m_endLbl.Create(LL14(L"終わり小節", L"Ending bars", L"Mesures de fin", L"Battute finali", L"Compases finales", L"엔딩 마디", L"结尾小节", L"موازير الخاتمة", L"Такты концовки", L"Schluss-Takte", L"Compassos finais", L"Slotmaten", L"Takty zakończenia", L"Bitiş ölçüleri"),
		WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_LEND);
	m_introBars.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 160), this, IDC_SAA_INTROBARS);
	m_breakBars.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 160), this, IDC_SAA_BREAKBARS);
	m_endBars.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 160), this, IDC_SAA_ENDBARS);
	m_introBars.AddString(L"2"); m_introBars.AddString(L"4"); m_introBars.AddString(L"8");
	m_breakBars.AddString(L"2"); m_breakBars.AddString(L"4");
	m_endBars.AddString(L"1"); m_endBars.AddString(L"2"); m_endBars.AddString(L"4");
	m_introBars.SetCurSel(1);
	m_breakBars.SetCurSel(0);
	m_endBars.SetCurSel(1);

	m_pitchLbl.Create(LL14(L"ピッチの足し方", L"Pitch treatment", L"Traitement de hauteur", L"Trattamento altezza", L"Tratamiento de altura", L"피치 처리", L"音高处理", L"معالجة الطبقة", L"Обработка высоты", L"Tonhöhenbehandlung", L"Tratamento de altura", L"Toonhoogte", L"Obróbka wysokości", L"Perde işlemi"),
		WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_LPITCH);
	m_pitch.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 160), this, IDC_SAA_PITCHMODE);
	m_pitch.AddString(LL14(L"ハモリ", L"Harmony", L"Harmonie", L"Armonia", L"Armonía", L"하모니", L"和声", L"هارموني", L"Гармония", L"Harmonie", L"Harmonia", L"Harmonie", L"Harmonia", L"Armoni"));
	m_pitch.AddString(LL14(L"ハモリ＋しゃくり", L"Harmony + scoop", L"Harmonie + glissando", L"Armonia + scoop", L"Armonía + caída", L"하모니+스쿱", L"和声＋滑音", L"هارموني + سحب", L"Гармония + подъезд", L"Harmonie + Scoop", L"Harmonia + scoop", L"Harmonie + scoop", L"Harmonia + glissando", L"Armoni + kaydırma"));
	m_pitch.AddString(LL14(L"ハモリ＋しゃくり＋オクターブ", L"Harmony + scoop + octave", L"Harmonie + glissando + octave", L"Armonia + scoop + ottava", L"Armonía + caída + octava", L"하모니+스쿱+옥타브", L"和声＋滑音＋八度", L"هارموني + سحب + أوكتاف", L"Гармония + подъезд + октава", L"Harmonie + Scoop + Oktave", L"Harmonia + scoop + oitava", L"Harmonie + scoop + octaaf", L"Harmonia + glissando + oktawa", L"Armoni + kaydırma + oktav"));
	m_pitch.SetCurSel(1);

	m_hint.Create(LL14(
		L"既存の音符は消しません。イントロと間奏は後ろへずらして、空いた小節に書きます。音量はメロディの強弱を書き換えます。元に戻せます。音符が無い曲は作れません。",
		L"Existing notes stay. Intro and interlude shift later music into the gap. Volume rewrites melody dynamics. Undo restores it. A score with no notes cannot be arranged.",
		L"Les notes restent. Intro et interlude décalent la suite. Le volume réécrit la dynamique. Annulation possible. Sans notes, pas d'arrangement.",
		L"Le note restano. Intro e interludio spostano il seguito. Il volume riscrive la dinamica. Si può annullare. Senza note non si arrangia.",
		L"Las notas se quedan. Intro e interludio desplazan lo que sigue. El volumen reescribe la dinámica. Se puede deshacer. Sin notas no hay arreglo.",
		L"기존 음표는 남깁니다. 인트로와 간주는 뒤를 밀어 빈 마디에 씁니다. 음량은 멜로디 강약을 바꿉니다. 실행 취소됩니다. 음표가 없으면 만들 수 없습니다.",
		L"原有音符保留。前奏和间奏把后面的内容后移，写在空出的小节。音量会改写旋律强弱。可以撤销。没有音符就无法编曲。",
		L"النغمات الحالية تبقى. المقدمة والفاصل يزيحان ما بعدهما إلى الفراغ. الشدة تعيد كتابة ديناميكية اللحن. يمكن التراجع. بلا نغمات لا توزيع.",
		L"Исходные ноты остаются. Вступление и проигрыш сдвигают дальше и пишутся в зазор. Громкость меняет динамику мелодии. Отмена вернёт. Без нот аранжировки нет.",
		L"Vorhandene Noten bleiben. Intro und Zwischenspiel schieben nach hinten. Lautstärke schreibt die Melodiedynamik um. Rückgängig möglich. Ohne Noten kein Arrangement.",
		L"As notas existentes ficam. Intro e interlúdio empurram o resto. O volume reescreve a dinâmica. Dá para desfazer. Sem notas não há arranjo.",
		L"Bestaande noten blijven. Intro en tussenspel schuiven de rest op. Volume herschrijft de dynamiek. Ongedaan maken kan. Zonder noten geen arrangement.",
		L"Istniejące nuty zostają. Intro i przerywnik przesuwają resztę. Głośność zmienia dynamikę melodii. Można cofnąć. Bez nut nie ma aranżu.",
		L"Var olan notalar durur. Giriş ve ara, sonrayı kaydırıp boşluğa yazar. Ses, melodi dinamiğini değiştirir. Geri alınır. Nota yoksa düzenleme yok."),
		WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_HINT);
	m_status.Create(L"", WS_CHILD | WS_VISIBLE, z, this, IDC_SAA_STATUS);

	m_preview.Create(LL14(L"試し聴き", L"Preview", L"Écouter", L"Ascolta", L"Escuchar", L"들어보기", L"试听", L"استماع", L"Слушать", L"Anhören", L"Ouvir", L"Beluister", L"Posłuchaj", L"Dinle"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, z, this, IDC_SAA_PREVIEW);
	m_ok.Create(LL14(L"決定", L"Apply", L"Valider", L"Applica", L"Aplicar", L"결정", L"确定", L"تطبيق", L"Применить", L"Übernehmen", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, z, this, IDOK);
	m_cancel.Create(LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"Vazgeç"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, z, this, IDCANCEL);
	m_preview.SetFlat(TRUE);
	m_ok.SetFlat(TRUE);
	m_cancel.SetFlat(TRUE);
	m_style.SetAeroMode(FALSE);
	m_pitch.SetAeroMode(FALSE);
	m_introBars.SetAeroMode(FALSE);
	m_breakBars.SetAeroMode(FALSE);
	m_endBars.SetAeroMode(FALSE);

	CFont* font = GetFont();
	CWnd* kids[] = { &m_styleLbl, &m_style, &m_drum, &m_bass, &m_chord, &m_melVol, &m_melPitch,
		&m_intro, &m_brk, &m_ending, &m_introLbl, &m_breakLbl, &m_endLbl, &m_introBars, &m_breakBars, &m_endBars,
		&m_pitchLbl, &m_pitch, &m_hint, &m_status, &m_preview, &m_ok, &m_cancel };
	for (int i = 0; i < (int)(sizeof(kids) / sizeof(kids[0])); i++)
		if (kids[i]->GetSafeHwnd() && font) kids[i]->SetFont(font);

	SetWindowPos(NULL, 0, 0, 560, 560, SWP_NOMOVE | SWP_NOZORDER);
	LayoutChrome();
	return TRUE;
}

void CSasamiAutoArrangeDlg::LayoutChrome()
{
	if (!m_style.GetSafeHwnd()) return;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int x = 16;
	int y = cap + 10;
	const int w = rc.Width() - 32;
	if (w < 80) return;
	const int col = w / 2;
	m_styleLbl.MoveWindow(x, y, w, 16);
	y += 18;
	m_style.MoveWindow(x, y, w, 220);
	y += 30;
	m_drum.MoveWindow(x, y, col - 8, 22);
	m_bass.MoveWindow(x + col, y, col, 22);
	y += 26;
	m_chord.MoveWindow(x, y, col - 8, 22);
	m_melVol.MoveWindow(x + col, y, col, 22);
	y += 26;
	m_melPitch.MoveWindow(x, y, w, 22);
	y += 30;
	m_intro.MoveWindow(x, y, 140, 22);
	m_brk.MoveWindow(x + 150, y, 140, 22);
	m_ending.MoveWindow(x + 300, y, w - 300, 22);
	y += 28;
	const int lw = (w - 16) / 3;
	m_introLbl.MoveWindow(x, y, lw, 16);
	m_breakLbl.MoveWindow(x + lw + 8, y, lw, 16);
	m_endLbl.MoveWindow(x + (lw + 8) * 2, y, lw, 16);
	y += 18;
	m_introBars.MoveWindow(x, y, lw, 140);
	m_breakBars.MoveWindow(x + lw + 8, y, lw, 140);
	m_endBars.MoveWindow(x + (lw + 8) * 2, y, lw, 140);
	y += 32;
	m_pitchLbl.MoveWindow(x, y, w, 16);
	y += 18;
	m_pitch.MoveWindow(x, y, w, 140);
	y += 32;
	m_hint.MoveWindow(x, y, w, 72);
	y += 76;
	m_status.MoveWindow(x, y, w, 36);
	const int bw = 110, bh = 28, gap = 8;
	int bx = rc.Width() - 16 - (bw * 3 + gap * 2);
	if (bx < 16) bx = 16;
	int by = rc.Height() - 16 - bh;
	m_preview.MoveWindow(bx, by, bw, bh);
	m_ok.MoveWindow(bx + bw + gap, by, bw, bh);
	m_cancel.MoveWindow(bx + (bw + gap) * 2, by, bw, bh);
}

void CSasamiAutoArrangeDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	LayoutChrome();
}

BOOL CSasamiAutoArrangeDlg::OnEraseBkgnd(CDC* pDC)
{
	return CCustomBlurDialogExBase::OnEraseBkgnd(pDC);
}

void CSasamiAutoArrangeDlg::OnPaint()
{
	CPaintDC dc(this);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiAutoArrangeDlg::OnPreview()
{
	if (m_host && m_host->stop) m_host->stop(m_host->ctx);
	ScAutoArrangeResult r;
	memset(&r, 0, sizeof(r));
	int err = ApplyNow(&r);
	ShowResult(r);
	if (err != SC_AE_OK) {
		Restore();
		if (m_host && m_host->refresh) m_host->refresh(m_host->ctx);
		return;
	}
	if (m_host && m_host->refresh) m_host->refresh(m_host->ctx);
	if (m_host && m_host->preview) m_host->preview(m_host->ctx);
}

void CSasamiAutoArrangeDlg::OnOK()
{
	if (m_host && m_host->stop) m_host->stop(m_host->ctx);
	ScAutoArrangeResult r;
	memset(&r, 0, sizeof(r));
	int err = ApplyNow(&r);
	ShowResult(r);
	if (err != SC_AE_OK) {
		Restore();
		if (m_host && m_host->refresh) m_host->refresh(m_host->ctx);
		return;
	}
	if (m_host && m_host->hist)
		ScScoreHistPush(m_host->hist, m_snap, m_snapN);
	if (m_host && m_host->refresh) m_host->refresh(m_host->ctx);
	m_keep = 1;
	EndDialog(IDOK);
}

void CSasamiAutoArrangeDlg::OnCancel()
{
	if (m_host && m_host->stop) m_host->stop(m_host->ctx);
	Restore();
	if (m_host && m_host->refresh) m_host->refresh(m_host->ctx);
	EndDialog(IDCANCEL);
}

} // namespace

int ScAutoArrangeDlgRun(const ScAutoArrangeHost* host)
{
	if (!host || !host->ev || !host->evCount) return IDCANCEL;
	CWnd* parent = host->ctx ? (CWnd*)host->ctx : NULL;
	CSasamiAutoArrangeDlg dlg(host, parent);
	return (int)dlg.DoModal();
}
