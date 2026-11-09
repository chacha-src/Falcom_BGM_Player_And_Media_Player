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
	IDC_SAA_LEND = 4924,
	/* v2 追加コントロール（ダイアログ内ローカル ID。resource.h は使わない） */
	IDC_SAA_LPROG = 4930,
	IDC_SAA_PROG = 4931,
	IDC_SAA_PROGDESC = 4932,
	IDC_SAA_MODE_OVER = 4933,
	IDC_SAA_MODE_RW = 4934,
	IDC_SAA_LTARGET = 4935,
	IDC_SAA_LDRUM = 4936,
	IDC_SAA_VARY = 4937,
	IDC_SAA_REROLL = 4938,
	IDC_SAA_LGROUP = 4939,
	IDC_SAA_LGEN = 4940,
	IDC_SAA_GEN = 4941,
	IDC_SAA_TGT_BASE = 4960,   /* +ch */
	IDC_SAA_DRM_BASE = 5000    /* +ch */
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
	afx_msg void OnStyleChanged();
	afx_msg void OnProgChanged();
	afx_msg void OnReroll();
	void RefillProg();
	void UpdateProgDesc();
	int NumCh() const;

	const ScAutoArrangeHost* m_host;
	ScEvent* m_snap;
	int m_snapN;
	int m_keep;
	int m_uiSnap;
	int m_seed;
	int m_vis[32], m_mute[32], m_solo[32], m_clef[32];
	CCustomStatic m_styleLbl, m_pitchLbl, m_introLbl, m_breakLbl, m_endLbl, m_hint, m_status;
	CCustomStatic m_progLbl, m_progDesc, m_targetLbl, m_drumLbl, m_groupLbl, m_genLbl;
	CCustomComboBox m_style, m_pitch, m_introBars, m_breakBars, m_endBars, m_prog, m_genBars;
	CCustomCheckBox m_drum, m_bass, m_chord, m_melVol, m_melPitch, m_intro, m_brk, m_ending, m_vary;
	CCustomRadioButton m_modeOver, m_modeRw;
	CCustomCheckBox m_tgt[32], m_drm[32];
	CCustomStandardButton m_preview, m_reroll, m_ok, m_cancel;
};

IMPLEMENT_DYNAMIC(CSasamiAutoArrangeDlg, CCustomBlurDialogExBase)

BEGIN_MESSAGE_MAP(CSasamiAutoArrangeDlg, CCustomBlurDialogExBase)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_BN_CLICKED(IDC_SAA_PREVIEW, OnPreview)
	ON_BN_CLICKED(IDC_SAA_REROLL, OnReroll)
	ON_CBN_SELCHANGE(IDC_SAA_STYLE, OnStyleChanged)
	ON_CBN_SELCHANGE(IDC_SAA_PROG, OnProgChanged)
	ON_BN_CLICKED(IDOK, OnOK)
	ON_BN_CLICKED(IDCANCEL, OnCancel)
END_MESSAGE_MAP()

CSasamiAutoArrangeDlg::CSasamiAutoArrangeDlg(const ScAutoArrangeHost* host, CWnd* parent)
	: CCustomBlurDialogExBase(IDD_SASAMI_AUTO_ARRANGE, parent)
	, m_host(host), m_snap(NULL), m_snapN(0), m_keep(0), m_uiSnap(0), m_seed(0)
{
}

int CSasamiAutoArrangeDlg::NumCh() const
{
	int n = (m_host && m_host->ui && m_host->ui->trackCount > 0) ? m_host->ui->trackCount : (m_host && m_host->isFm ? SC_FM_TOTAL : 16);
	if (n < 1) n = 1;
	if (n > 32) n = 32;
	return n;
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
	static const int kIntro[] = { 0, 2, 4, 8, 16 };
	static const int kBreak[] = { 0, 2, 4, 8 };
	static const int kEnd[] = { 0, 1, 2, 4, 8 };
	static const int kGen[] = { 0, 4, 8, 12, 16, 24, 32, 48, 64 };
	int ii = m_introBars.GetCurSel();
	int bi = m_breakBars.GetCurSel();
	int ei = m_endBars.GetCurSel();
	int gi = m_genBars.GetSafeHwnd() ? m_genBars.GetCurSel() : 0;
	o->introBars = kIntro[(ii >= 0 && ii < 5) ? ii : 2];
	o->breakBars = kBreak[(bi >= 0 && bi < 4) ? bi : 1];
	o->endBars = kEnd[(ei >= 0 && ei < 5) ? ei : 2];
	o->genBars = kGen[(gi >= 0 && gi < 9) ? gi : 0];
	int pm = m_pitch.GetCurSel();
	if (pm < 0) pm = 1;
	o->pitchMode = pm + 1;
	o->isFm = m_host->isFm;
	o->curPart = m_host->curPart;
	o->defMeterN = m_host->defMeterN;
	o->defMeterD = m_host->defMeterD;
	o->defKey = m_host->defKey;
	o->ui = m_host->ui;

	/* --- v2 --- */
	o->mode = (m_modeRw.GetSafeHwnd() && m_modeRw.GetCheck() == BST_CHECKED) ? SC_AM_REWRITE : SC_AM_OVERLAY;
	int pv = m_prog.GetSafeHwnd() ? m_prog.GetCurSel() : 0;
	o->variation = pv < 0 ? 0 : pv;
	o->useProg = 1;
	/* 「毎回少し変える」ON のとき種を 0（自動＝毎回違う）。OFF で固定し再現可能に。 */
	int vary = (m_vary.GetSafeHwnd() && m_vary.GetCheck() == BST_CHECKED);
	o->seed = vary ? 0 : (m_seed ? m_seed : 0x5A5A1234);
	const int nCh = NumCh();
	uint32_t tgt = 0, drm = 0;
	for (int ch = 0; ch < nCh && ch < 32; ch++) {
		if (m_tgt[ch].GetSafeHwnd() && m_tgt[ch].GetCheck() == BST_CHECKED) tgt |= (1u << ch);
		if (m_drm[ch].GetSafeHwnd() && m_drm[ch].GetCheck() == BST_CHECKED) drm |= (1u << ch);
	}
	/* ドラム ch も対象扱いにしておく（書き換え時に消して作り直すため）。 */
	o->targetMask = tgt | drm;
	o->drumMask = drm;
	/* 枠の自動割り当て: チェック順で 0,1,0,1…（枠0=本体, 枠1=シンバル/ドラム2）。 */
	memset(o->drumSlot, 0, sizeof(o->drumSlot));
	memset(o->partRole, 0, sizeof(o->partRole));
	int di = 0;
	for (int ch = 0; ch < nCh && ch < 32; ch++) {
		if (drm & (1u << ch)) { o->drumSlot[ch] = (uint8_t)(di & 1); di++; }
	}
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
	ModifyStyle(0, WS_CLIPCHILDREN);
	EnableAero(FALSE);
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
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_ENDELLIPSIS, z, this, IDC_SAA_LSTYLE);
	m_style.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_STYLE);
	for (int i = 0; i < SC_AS_COUNT; i++)
		m_style.AddString(ScAutoStyleName(i));
	m_style.SetCurSel(0);

	m_drum.Create(LL14(L"ドラム", L"Drums", L"Batterie", L"Batteria", L"Batería", L"드럼", L"鼓", L"طبول", L"Ударные", L"Drums", L"Bateria", L"Drums", L"Perkusja", L"Davul"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_DRUM);
	m_bass.Create(LL14(L"ベース", L"Bass", L"Basse", L"Basso", L"Bajo", L"베이스", L"贝斯", L"باص", L"Бас", L"Bass", L"Baixo", L"Bas", L"Bas", L"Bas"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_BASS);
	m_chord.Create(LL14(L"コード", L"Chords", L"Accords", L"Accordi", L"Acordes", L"코드", L"和弦", L"أكورد", L"Аккорды", L"Akkorde", L"Acordes", L"Akkoorden", L"Akordy", L"Akorlar"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_CHORD);
	m_melVol.Create(LL14(L"メロディの音量", L"Melody volume", L"Volume mélodie", L"Volume melodia", L"Volumen melodía", L"멜로디 음량", L"旋律音量", L"شدة اللحن", L"Громкость мелодии", L"Melodielautstärke", L"Volume da melodia", L"Melodievolume", L"Głośność melodii", L"Melodi sesi"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_MELVOL);
	m_melPitch.Create(LL14(L"メロディのピッチ", L"Melody pitch", L"Hauteur mélodie", L"Intonazione melodia", L"Altura melodía", L"멜로디 피치", L"旋律音高", L"طبقة اللحن", L"Высота мелодии", L"Melodietonhöhe", L"Altura da melodia", L"Melodiehoogte", L"Wysokość melodii", L"Melodi perdesi"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_MELPITCH);
	m_drum.SetCheck(BST_CHECKED);
	m_bass.SetCheck(BST_CHECKED);
	m_chord.SetCheck(BST_CHECKED);
	m_melVol.SetCheck(BST_CHECKED);
	m_melPitch.SetCheck(BST_CHECKED);

	m_intro.Create(LL14(L"イントロ", L"Intro", L"Intro", L"Intro", L"Intro", L"인트로", L"前奏", L"مقدمة", L"Вступление", L"Intro", L"Intro", L"Intro", L"Intro", L"Giriş"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_INTRO);
	m_brk.Create(LL14(L"間奏", L"Interlude", L"Interlude", L"Interludio", L"Interludio", L"간주", L"间奏", L"فاصل", L"Проигрыш", L"Zwischenspiel", L"Interlúdio", L"Tussenspel", L"Przerywnik", L"Ara"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_BREAK);
	m_ending.Create(LL14(L"エンディング", L"Ending", L"Fin", L"Finale", L"Final", L"엔딩", L"结尾", L"خاتمة", L"Концовка", L"Schluss", L"Final", L"Slot", L"Zakończenie", L"Bitiş"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_ENDING);
	m_intro.SetCheck(BST_CHECKED);
	m_brk.SetCheck(BST_CHECKED);
	m_ending.SetCheck(BST_CHECKED);

	const DWORD stStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_ENDELLIPSIS;
	auto addBarItem = [](CCustomComboBox& c, int n) {
		wchar_t s[40];
		if (n <= 0)
			wcsncpy_s(s, LL14(L"なし", L"None", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"بدون", L"Нет", L"Keine", L"Nenhum", L"Geen", L"Brak", L"Yok"), _TRUNCATE);
		else
			_snwprintf_s(s, _TRUNCATE, LL14(L"%d小節", L"%d bars", L"%d mesures", L"%d battute", L"%d compases", L"%d마디", L"%d小节", L"%d موازير", L"%d тактов", L"%d Takte", L"%d compassos", L"%d maten", L"%d takty", L"%d ölçü"), n);
		c.AddString(s);
	};

	m_genLbl.Create(LL14(L"本体の長さ（伴奏を何小節作るか）", L"Body length (how many bars to write)", L"Longueur du corps", L"Lunghezza corpo", L"Longitud del cuerpo", L"본체 길이(몇 마디)", L"主体长度（写几小节）", L"طول المتن", L"Длина основы", L"Länge des Hauptteils", L"Comprimento do corpo", L"Lengte van het lijf", L"Długość części głównej", L"Gövde uzunluğu"),
		stStyle, z, this, IDC_SAA_LGEN);
	m_genBars.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_GEN);
	m_genBars.SetAeroMode(FALSE);
	m_genBars.AddString(LL14(L"既存の長さに合わせる", L"Match existing length", L"Suivre la longueur existante", L"Usa la lunghezza esistente", L"Usar la longitud existente", L"기존 길이에 맞춤", L"按现有长度", L"مطابقة الطول الحالي", L"Как существующая длина", L"Vorhandene Länge", L"Usar o comprimento atual", L"Bestaande lengte", L"Dopasuj do istniejącej", L"Mevcut uzunluğa uydur"));
	{
		static const int kG[] = { 4, 8, 12, 16, 24, 32, 48, 64 };
		for (int i = 0; i < 8; i++) addBarItem(m_genBars, kG[i]);
	}
	{
		const int empty = (m_host && ScAutoArrangeCountNotes(m_host->ev, m_snapN) <= 0) ? 1 : 0;
		m_genBars.SetCurSel(empty ? 4 : 0); /* 空なら 16小節、音符があれば既存長さ */
	}

	m_introLbl.Create(LL14(L"イントロの小節数", L"Intro bars", L"Mesures d'intro", L"Battute intro", L"Compases de intro", L"인트로 마디", L"前奏小节数", L"موازير المقدمة", L"Такты вступления", L"Intro-Takte", L"Compassos de intro", L"Intromaten", L"Takty intra", L"Giriş ölçüleri"),
		stStyle, z, this, IDC_SAA_LINTRO);
	m_breakLbl.Create(LL14(L"間奏の小節数", L"Interlude bars", L"Mesures d'interlude", L"Battute interludio", L"Compases de interludio", L"간주 마디", L"间奏小节数", L"موازير الفاصل", L"Такты проигрыша", L"Zwischenspiel-Takte", L"Compassos do interlúdio", L"Tussenspelmaten", L"Takty przerywnika", L"Ara ölçüleri"),
		stStyle, z, this, IDC_SAA_LBREAK);
	m_endLbl.Create(LL14(L"エンディングの小節数", L"Ending bars", L"Mesures de fin", L"Battute finali", L"Compases finales", L"엔딩 마디", L"结尾小节数", L"موازير الخاتمة", L"Такты концовки", L"Schluss-Takte", L"Compassos finais", L"Slotmaten", L"Takty zakończenia", L"Bitiş ölçüleri"),
		stStyle, z, this, IDC_SAA_LEND);
	m_introBars.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_INTROBARS);
	m_breakBars.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_BREAKBARS);
	m_endBars.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_ENDBARS);
	addBarItem(m_introBars, 0); addBarItem(m_introBars, 2); addBarItem(m_introBars, 4); addBarItem(m_introBars, 8); addBarItem(m_introBars, 16);
	addBarItem(m_breakBars, 0); addBarItem(m_breakBars, 2); addBarItem(m_breakBars, 4); addBarItem(m_breakBars, 8);
	addBarItem(m_endBars, 0); addBarItem(m_endBars, 1); addBarItem(m_endBars, 2); addBarItem(m_endBars, 4); addBarItem(m_endBars, 8);
	m_introBars.SetCurSel(2);
	m_breakBars.SetCurSel(1);
	m_endBars.SetCurSel(2);

	m_pitchLbl.Create(LL14(L"メロディへの足し方（ハモリ／しゃくり）", L"Melody extras (harmony / scoop)", L"Ajouts mélodie", L"Aggiunte melodia", L"Añadidos de melodía", L"멜로디 추가(하모니/스쿱)", L"旋律加工（和声／滑音）", L"إضافات اللحن", L"Добавки к мелодии", L"Melodie-Zusätze", L"Extras da melodia", L"Melodie-toevoegingen", L"Dodatki melodii", L"Melodi ekleri"),
		stStyle, z, this, IDC_SAA_LPITCH);
	m_pitch.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_PITCHMODE);
	m_pitch.AddString(LL14(L"ハモリ", L"Harmony", L"Harmonie", L"Armonia", L"Armonía", L"하모니", L"和声", L"هارموني", L"Гармония", L"Harmonie", L"Harmonia", L"Harmonie", L"Harmonia", L"Armoni"));
	m_pitch.AddString(LL14(L"ハモリ＋しゃくり", L"Harmony + scoop", L"Harmonie + glissando", L"Armonia + scoop", L"Armonía + caída", L"하모니+스쿱", L"和声＋滑音", L"هارموني + سحب", L"Гармония + подъезд", L"Harmonie + Scoop", L"Harmonia + scoop", L"Harmonie + scoop", L"Harmonia + glissando", L"Armoni + kaydırma"));
	m_pitch.AddString(LL14(L"ハモリ＋しゃくり＋オクターブ", L"Harmony + scoop + octave", L"Harmonie + glissando + octave", L"Armonia + scoop + ottava", L"Armonía + caída + octava", L"하모니+스쿱+옥타브", L"和声＋滑音＋八度", L"هارموني + سحب + أوكتاف", L"Гармония + подъезд + октава", L"Harmonie + Scoop + Oktave", L"Harmonia + scoop + oitava", L"Harmonie + scoop + octaaf", L"Harmonia + glissando + oktawa", L"Armoni + kaydırma + oktav"));
	m_pitch.SetCurSel(1);

	m_hint.Create(LL14(
		L"本体の長さで伴奏の小節数を決めます。既存の音符は重ねでは消しません。イントロと間奏は後ろへずらして空いた小節に書きます。音符が無い曲は本体の長さを指定するとゼロから作れます。",
		L"Body length sets how many bars to write. Overlay keeps existing notes. Intro and interlude shift later music into the gap. With no notes, pick a body length to generate from scratch.",
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

	/* --- v2: ジャンル見出し / コード進行 / 書き換え・重ね / 対象パート / ドラム ch --- */
	m_groupLbl.Create(L"", stStyle, z, this, IDC_SAA_LGROUP);

	m_progLbl.Create(LL14(L"コード進行", L"Chord progression", L"Progression", L"Progressione", L"Progresión", L"코드 진행", L"和弦进行", L"تتابع الأكوردات", L"Последовательность", L"Akkordfolge", L"Progressão", L"Akkoordenschema", L"Progresja", L"Akor dizisi"),
		stStyle, z, this, IDC_SAA_LPROG);
	m_prog.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST, CRect(0, 0, 10, 24), this, IDC_SAA_PROG);
	m_prog.SetAeroMode(FALSE);
	m_progDesc.Create(L"", stStyle, z, this, IDC_SAA_PROGDESC);

	m_modeOver.Create(LL14(L"重ねる（空きを埋める）", L"Overlay (fill gaps)", L"Superposer (combler)", L"Sovrapponi (riempi)", L"Superponer (rellenar)", L"겹치기(빈곳 채움)", L"叠加(填空)", L"تركيب (ملء الفراغ)", L"Наложить (заполнить)", L"Überlagern (Lücken)", L"Sobrepor (preencher)", L"Overlay (gaten)", L"Nałóż (wypełnij)", L"Üst üste (boşluk)"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, z, this, IDC_SAA_MODE_OVER);
	m_modeRw.Create(LL14(L"書き換える（進行に沿って作り直す）", L"Rewrite (regen on progression)", L"Réécrire (sur la grille)", L"Riscrivi (sulla griglia)", L"Reescribir (sobre la grid)", L"재작성(진행대로)", L"改写(按进行重做)", L"إعادة كتابة (حسب التتابع)", L"Переписать (по сетке)", L"Neu schreiben (nach Folge)", L"Reescrever (pela grade)", L"Herschrijf (op schema)", L"Przepisz (wg progresji)", L"Yeniden yaz (diziye göre)"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTORADIOBUTTON, z, this, IDC_SAA_MODE_RW);
	m_modeOver.SetCheck(BST_CHECKED);

	m_vary.Create(LL14(L"毎回少し変える", L"Vary each time", L"Varier à chaque fois", L"Varia ogni volta", L"Variar cada vez", L"매번 조금 변화", L"每次略有变化", L"تغيير بسيط كل مرة", L"Менять каждый раз", L"Jedes Mal variieren", L"Variar sempre", L"Elke keer variëren", L"Zmieniaj za każdym razem", L"Her seferinde değiştir"),
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_VARY);
	m_vary.SetCheck(BST_CHECKED);

	m_targetLbl.Create(LL14(
		L"アレンジ対象パート（1〜32＝チャンネル。チェックしたパートに書く。未チェックなら自動）",
		L"Target parts (1–32 = channels. Checked parts get writing. None = auto)",
		L"Parties cibles (1–32 = canaux. Cochées = écriture. Aucune = auto)",
		L"Parti bersaglio (1–32 = canali. Spuntate = scrittura. Nessuna = auto)",
		L"Partes objetivo (1–32 = canales. Marcadas = escritura. Ninguna = auto)",
		L"대상 파트(1–32=채널. 체크한 파트에 기록. 미지정=자동)",
		L"编曲目标声部（1–32＝通道。勾选的声部会写入。未选＝自动）",
		L"الأجزاء الهدف (1–32 = قنوات. المحددة تُكتب. لا شيء = تلقائي)",
		L"Целевые партии (1–32 = каналы. Отмеченные — запись. Нет = авто)",
		L"Zielstimmen (1–32 = Kanäle. Angehakte werden beschrieben. Keine = auto)",
		L"Partes alvo (1–32 = canais. Marcadas recebem escrita. Nenhuma = auto)",
		L"Doelpartijen (1–32 = kanalen. Aangevinkt = schrijven. Geen = auto)",
		L"Partie docelowe (1–32 = kanały. Zaznaczone = zapis. Brak = auto)",
		L"Hedef partiler (1–32 = kanallar. İşaretli yazılır. Yok = oto)"),
		stStyle, z, this, IDC_SAA_LTARGET);
	m_drumLbl.Create(LL14(
		L"ドラムを出すチャンネル（1〜32。複数可。GM なら 10 が定番）",
		L"Drum output channels (1–32, multi. GM default is 10)",
		L"Canaux batterie (1–32, multi. GM = 10)",
		L"Canali batteria (1–32, multi. GM = 10)",
		L"Canales de batería (1–32, multi. GM = 10)",
		L"드럼 출력 채널(1–32, 복수 가능. GM은 10)",
		L"鼓输出通道（1–32，可多选。GM 一般为 10）",
		L"قنوات الطبل (1–32، متعدد. GM = 10)",
		L"Каналы ударных (1–32, неск. GM = 10)",
		L"Drum-Kanäle (1–32, mehr. GM = 10)",
		L"Canais de bateria (1–32, vários. GM = 10)",
		L"Drumkanalen (1–32, meer. GM = 10)",
		L"Kanały perkusji (1–32, wiele. GM = 10)",
		L"Davul kanalları (1–32, çoklu. GM = 10)"),
		stStyle, z, this, IDC_SAA_LDRUM);

	{
		const int nCh = NumCh();
		for (int ch = 0; ch < nCh && ch < 32; ch++) {
			wchar_t cap[8];
			_snwprintf_s(cap, _TRUNCATE, L"%d", ch + 1);
			m_tgt[ch].Create(cap, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_TGT_BASE + ch);
			m_drm[ch].Create(cap, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_AUTOCHECKBOX, z, this, IDC_SAA_DRM_BASE + ch);
		}
	}

	m_reroll.Create(LL14(L"再抽選", L"Re-roll", L"Relancer", L"Rigenera", L"Re-generar", L"다시 뽑기", L"重新生成", L"إعادة", L"Пересоздать", L"Neu würfeln", L"Re-gerar", L"Opnieuw", L"Losuj ponownie", L"Yeniden"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, z, this, IDC_SAA_REROLL);
	m_reroll.SetFlat(TRUE);

	RefillProg();

	CFont* font = GetFont();
	CWnd* kids[] = { &m_styleLbl, &m_style, &m_groupLbl, &m_progLbl, &m_prog, &m_progDesc,
		&m_modeOver, &m_modeRw, &m_vary, &m_targetLbl, &m_drumLbl,
		&m_drum, &m_bass, &m_chord, &m_melVol, &m_melPitch,
		&m_intro, &m_brk, &m_ending, &m_genLbl, &m_genBars,
		&m_introLbl, &m_breakLbl, &m_endLbl, &m_introBars, &m_breakBars, &m_endBars,
		&m_pitchLbl, &m_pitch, &m_hint, &m_status, &m_preview, &m_reroll, &m_ok, &m_cancel };
	for (int i = 0; i < (int)(sizeof(kids) / sizeof(kids[0])); i++)
		if (kids[i]->GetSafeHwnd() && font) kids[i]->SetFont(font);
	auto solidChk = [](CCustomCheckBox& c) {
		if (c.GetSafeHwnd()) c.SetAeroMode(FALSE);
	};
	solidChk(m_drum); solidChk(m_bass); solidChk(m_chord); solidChk(m_melVol); solidChk(m_melPitch);
	solidChk(m_intro); solidChk(m_brk); solidChk(m_ending); solidChk(m_vary);
	solidChk(m_modeOver); solidChk(m_modeRw);
	auto prepLbl = [](CCustomStatic& s) {
		if (!s.GetSafeHwnd()) return;
		s.SetAeroMode(FALSE);
		s.SetKeepFontHeight(TRUE);
		s.SetSolidFill(TRUE, COLOR_DIALOG_BG);
	};
	prepLbl(m_styleLbl); prepLbl(m_pitchLbl); prepLbl(m_introLbl); prepLbl(m_breakLbl); prepLbl(m_endLbl);
	prepLbl(m_hint); prepLbl(m_status); prepLbl(m_progLbl); prepLbl(m_progDesc);
	prepLbl(m_targetLbl); prepLbl(m_drumLbl); prepLbl(m_groupLbl); prepLbl(m_genLbl);
	{
		const int nCh = NumCh();
		for (int ch = 0; ch < nCh && ch < 32; ch++) {
			if (m_tgt[ch].GetSafeHwnd() && font) m_tgt[ch].SetFont(font);
			if (m_drm[ch].GetSafeHwnd() && font) m_drm[ch].SetFont(font);
			if (m_tgt[ch].GetSafeHwnd()) m_tgt[ch].SetAeroMode(FALSE);
			if (m_drm[ch].GetSafeHwnd()) m_drm[ch].SetAeroMode(FALSE);
		}
	}

	{
		const int nCh = NumCh();
		const int grows = (nCh + 7) / 8;
		int h = 780 + grows * 48;
		if (h < 820) h = 820;
		if (h > 980) h = 980;
		SetWindowPos(NULL, 0, 0, 720, h, SWP_NOMOVE | SWP_NOZORDER);
	}
	LayoutChrome();
	return TRUE;
}

/* 選択テーマのジャンル見出しと、使えるコード進行で進行コンボを埋め直す。 */
void CSasamiAutoArrangeDlg::RefillProg()
{
	if (!m_prog.GetSafeHwnd()) return;
	int style = m_style.GetCurSel();
	if (style < 0 || style >= SC_AS_COUNT) style = SC_AS_16BEAT;
	if (m_groupLbl.GetSafeHwnd())
		m_groupLbl.SetWindowText(ScAutoStyleGroupName(style));
	m_prog.ResetContent();
	int vc = ScAutoStyleVariationCount(style);
	if (vc < 1) vc = 1;
	int key = m_host ? m_host->defKey : 0;
	for (int v = 0; v < vc; v++) {
		wchar_t d[256];
		ScAutoStyleProgDesc(style, v, key, d, 256);
		m_prog.AddString(d[0] ? d : L"-");
	}
	m_prog.SetCurSel(0);
	UpdateProgDesc();
}

void CSasamiAutoArrangeDlg::UpdateProgDesc()
{
	if (!m_progDesc.GetSafeHwnd()) return;
	int style = m_style.GetCurSel();
	if (style < 0 || style >= SC_AS_COUNT) style = SC_AS_16BEAT;
	int v = m_prog.GetSafeHwnd() ? m_prog.GetCurSel() : 0;
	if (v < 0) v = 0;
	wchar_t d[256];
	ScAutoStyleProgDesc(style, v, m_host ? m_host->defKey : 0, d, 256);
	m_progDesc.SetWindowText(d);
}

void CSasamiAutoArrangeDlg::OnStyleChanged()
{
	RefillProg();
}

void CSasamiAutoArrangeDlg::OnProgChanged()
{
	UpdateProgDesc();
}

void CSasamiAutoArrangeDlg::OnReroll()
{
	/* 種を変えて試し聴き。決定時は現在の見た目どおりに適用される。 */
	m_seed = (int)(GetTickCount() | 1);
	if (m_vary.GetSafeHwnd()) m_vary.SetCheck(BST_UNCHECKED);
	OnPreview();
}

void CSasamiAutoArrangeDlg::LayoutChrome()
{
	if (!m_style.GetSafeHwnd()) return;
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int x = 16;
	int y = cap + 10;
	const int w = rc.Width() - 32;
	if (w < 120) return;
	const int col = w / 2;
	const int nCh = NumCh();
	const int gcols = 8;
	const int cellW = max(40, w / gcols);
	const int grows = (nCh + gcols - 1) / gcols;
	const int comboH = 24;
	const int bw = 110, bh = 28, gap = 8;
	const int footerTop = rc.Height() - 16 - bh;

	auto placeCombo = [&](CCustomComboBox& c, int cx, int cy, int cw) {
		if (!c.GetSafeHwnd()) return;
		c.ModifyStyle(0, WS_CLIPSIBLINGS);
		c.SetItemHeight(-1, 18);
		c.SetItemHeight(0, 20);
		if (cw > 0) c.SetDroppedWidth(max(cw, 180));
		c.MoveWindow(cx, cy, cw, comboH);
	};
	auto raise = [](CWnd& wnd) {
		if (wnd.GetSafeHwnd())
			wnd.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	};

	const int labH = 20;
	/* パターン＋ジャンル見出し */
	m_styleLbl.MoveWindow(x, y, 120, labH);
	m_groupLbl.MoveWindow(x + 124, y, max(40, w - 124), labH);
	y += labH + 2;
	placeCombo(m_style, x, y, w);
	y += comboH + 8;

	/* コード進行 */
	m_progLbl.MoveWindow(x, y, 120, labH);
	m_progDesc.MoveWindow(x + 124, y, max(40, w - 124), labH);
	y += labH + 2;
	placeCombo(m_prog, x, y, w);
	y += comboH + 8;

	/* 書き換え / 重ね + 毎回変える + 再抽選 */
	m_modeOver.MoveWindow(x, y, col, 22);
	m_modeRw.MoveWindow(x + col, y, col, 22);
	y += 26;
	m_vary.MoveWindow(x, y, col, 22);
	m_reroll.MoveWindow(x + col, y, 110, 24);
	y += 30;

	/* 生成ジョブ */
	m_drum.MoveWindow(x, y, col - 8, 22);
	m_bass.MoveWindow(x + col, y, col, 22);
	y += 26;
	m_chord.MoveWindow(x, y, col - 8, 22);
	m_melVol.MoveWindow(x + col, y, col, 22);
	y += 26;
	m_melPitch.MoveWindow(x, y, w, 22);
	y += 28;
	m_intro.MoveWindow(x, y, 120, 22);
	m_brk.MoveWindow(x + 130, y, 120, 22);
	m_ending.MoveWindow(x + 260, y, max(80, w - 260), 22);
	y += 28;
	m_genLbl.MoveWindow(x, y, w, labH);
	y += labH + 2;
	placeCombo(m_genBars, x, y, w);
	y += comboH + 10;
	const int lw = (w - 16) / 3;
	m_introLbl.MoveWindow(x, y, lw, labH);
	m_breakLbl.MoveWindow(x + lw + 8, y, lw, labH);
	m_endLbl.MoveWindow(x + (lw + 8) * 2, y, lw, labH);
	y += labH + 2;
	placeCombo(m_introBars, x, y, lw);
	placeCombo(m_breakBars, x + lw + 8, y, lw);
	placeCombo(m_endBars, x + (lw + 8) * 2, y, lw);
	y += comboH + 10;
	m_pitchLbl.MoveWindow(x, y, w, labH);
	y += labH + 2;
	placeCombo(m_pitch, x, y, w);
	y += comboH + 10;

	/* 対象パート格子 — 番号はチャンネル。スタティックで何のチェックかを示す */
	m_targetLbl.MoveWindow(x, y, w, labH);
	raise(m_targetLbl);
	y += labH + 4;
	for (int ch = 0; ch < nCh && ch < 32; ch++) {
		int r = ch / gcols, c = ch % gcols;
		if (m_tgt[ch].GetSafeHwnd())
			m_tgt[ch].MoveWindow(x + c * cellW, y + r * 22, cellW - 2, 20);
	}
	y += grows * 22 + 8;

	/* ドラム出力 ch 格子 */
	m_drumLbl.MoveWindow(x, y, w, labH);
	raise(m_drumLbl);
	y += labH + 4;
	for (int ch = 0; ch < nCh && ch < 32; ch++) {
		int r = ch / gcols, c = ch % gcols;
		if (m_drm[ch].GetSafeHwnd())
			m_drm[ch].MoveWindow(x + c * cellW, y + r * 22, cellW - 2, 20);
	}
	y += grows * 22 + 8;

	const int statusH = 28;
	int hintH = 56;
	if (y + hintH + 8 + statusH > footerTop - 8) {
		hintH = footerTop - 8 - statusH - 8 - y;
		if (hintH < 28) hintH = 28;
	}
	m_hint.MoveWindow(x, y, w, hintH);
	y += hintH + 6;
	m_status.MoveWindow(x, y, w, statusH);

	int bx = rc.Width() - 16 - (bw * 3 + gap * 2);
	if (bx < 16) bx = 16;
	int by = footerTop;
	if (by < y + statusH + 4) by = y + statusH + 4;
	m_preview.MoveWindow(bx, by, bw, bh);
	m_ok.MoveWindow(bx + bw + gap, by, bw, bh);
	m_cancel.MoveWindow(bx + (bw + gap) * 2, by, bw, bh);

	raise(m_styleLbl); raise(m_groupLbl); raise(m_progLbl); raise(m_progDesc);
	raise(m_genLbl); raise(m_introLbl); raise(m_breakLbl); raise(m_endLbl); raise(m_pitchLbl);
	raise(m_targetLbl); raise(m_drumLbl);
	ScStaffForceOpaqueKids(this);
}

void CSasamiAutoArrangeDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	LayoutChrome();
}

BOOL CSasamiAutoArrangeDlg::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	if (cap > 0 && cap < rc.bottom) rc.top = cap;
	if (rc.Height() <= 0) return TRUE;
	const int saved = pDC->SaveDC();
	ScStaffExcludeChildWindows(*pDC, this);
#if CCUSTOM_AERO_SUPPORT
	CCC_FillRectAlpha(pDC->GetSafeHdc(), rc, COLOR_DIALOG_BG, 255);
#else
	pDC->FillSolidRect(&rc, COLOR_DIALOG_BG);
#endif
	pDC->RestoreDC(saved);
	return TRUE;
}

void CSasamiAutoArrangeDlg::OnPaint()
{
	CPaintDC dc(this);
	ScStaffCaptionPaintThenExclude(dc, m_hWnd);
	ScStaffExcludeChildWindows(dc, this);
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
