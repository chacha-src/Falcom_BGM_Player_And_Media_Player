#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CProToolsDlg.h"
#include "CMediaPlayerDlg.h"
#include "FileTagInfo.h"
#include "SongParams.h"
#include "PlayList.h"


extern save savedata;
extern COggDlg* og;
extern CPlayList* pl;
extern int plcnt;
extern __int64 playb;
extern int wavbit_sample_Hz;

CProToolsDlg* g_proToolsDlg = nullptr;

namespace {

class CPtHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_PT_HELP };
	explicit CPtHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
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

static CPtHelpDlg* g_ptHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CPtHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CPtHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"再生詳細操作ガイド", L"Playback Details Guide", L"Guide détails lecture", L"Guida dettagli riproduzione",
		L"Guía detalles reproducción", L"재생 상세 가이드", L"播放详情指南", L"دليل تفاصيل التشغيل",
		L"Руководство деталей воспроизведения", L"Wiedergabe-Details Guide", L"Guia detalhes reprodução", L"Afspeeldetails-gids",
		L"Przewodnik szczegółów odtwarzania", L"Oynatma ayrıntıları kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CPtHelpDlg::OnOK() { DestroyWindow(); }
void CPtHelpDlg::OnCancel() { DestroyWindow(); }
void CPtHelpDlg::OnClose() { DestroyWindow(); }

void CPtHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_ptHelpDlg == this)
		g_ptHelpDlg = nullptr;
	delete this;
}

BOOL CPtHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CPtHelpDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int footerH = 26;
	rc.bottom -= footerH;
	dc.FillSolidRect(CRect(0, 0, rc.right, rc.bottom + footerH), RGB(248, 248, 252));
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

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
	title(L, y, LL14(L"再生詳細操作ガイド", L"Playback Details — Guide", L"Guide détails lecture", L"Guida dettagli",
		L"Guía detalles", L"재생 상세 가이드", L"播放详情指南", L"دليل التشغيل",
		L"Руководство", L"Wiedergabe-Guide", L"Guia detalhes", L"Afspeeldetails",
		L"Przewodnik", L"Oynatma kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"ギャップレス・ReplayGain・ループ／キュー・タグをここでまとめます。多くの項目は即時反映されます。",
		L"Gapless, ReplayGain, loop/cues, and tags live here. Many options apply immediately.",
		L"Gapless, ReplayGain, boucles/cues et tags. Beaucoup s'appliquent tout de suite.",
		L"Gapless, ReplayGain, loop/cue e tag. Molte opzioni si applicano subito.",
		L"Gapless, ReplayGain, bucles/cues y etiquetas. Muchas se aplican al instante.",
		L"갭리스·ReplayGain·루프/큐·태그를 모읍니다. 많은 항목이 즉시 반영됩니다.",
		L"在此汇总无缝、ReplayGain、循环/标记与标签。多数项立即生效。",
		L"Gapless و ReplayGain والحلقة/الإشارات والوسوم هنا. كثير منها فوري.",
		L"Gapless, ReplayGain, цикл/метки и теги. Многие параметры применяются сразу.",
		L"Gapless, ReplayGain, Loop/Cues und Tags. Viele gelten sofort.",
		L"Gapless, ReplayGain, loop/cues e tags. Muitos aplicam de imediato.",
		L"Gapless, ReplayGain, loop/cues en tags. Veel opties meteen.",
		L"Gapless, ReplayGain, pętla/cue i tagi. Wiele działa od razu.",
		L"Gapless, ReplayGain, döngü/cue ve etiketler. Çoğu hemen uygulanır."));
	y += lh + 4;

	title(L, y, LL14(L"再生 / レベル", L"Playback / Level", L"Lecture / Niveau", L"Riproduzione / Livello",
		L"Reproducción / Nivel", L"재생 / 레벨", L"播放 / 电平", L"تشغيل / مستوى",
		L"Воспроизведение / Уровень", L"Wiedergabe / Pegel", L"Reprodução / Nível", L"Afspelen / Niveau",
		L"Odtwarzanie / Poziom", L"Oynatma / Seviye"));
	y += titleLh;
	body(L, y, LL14(L"・ギャップレス …… 連続再生で曲間の無音を詰めます", L"· Gapless …… tighten silence between tracks", L"· Gapless …… réduit le silence entre pistes", L"· Gapless …… riduce il silenzio tra brani",
		L"· Gapless …… reduce el silencio entre pistas", L"· 갭리스 …… 곡 사이 무음을 줄입니다", L"· 无缝 …… 缩短曲间静音", L"· Gapless …… يقلل الصمت بين المقاطع",
		L"· Gapless …… сокращает паузу между треками", L"· Gapless …… verkürzt Stille zwischen Titeln", L"· Gapless …… reduz silêncio entre faixas", L"· Gapless …… verkort stilte tussen nummers",
		L"· Gapless …… zmniejsza ciszę między utworami", L"· Gapless …… parçalar arası sessizliği kısaltır")); y += lh;
	body(L, y, LL14(L"・ReplayGain …… Off / Track / Album。目標LUで再生ゲインを補正", L"· ReplayGain …… Off / Track / Album. Target LU corrects level", L"· ReplayGain …… Off / Track / Album. LU cible", L"· ReplayGain …… Off / Track / Album. LU obiettivo",
		L"· ReplayGain …… Off / Track / Album. LU objetivo", L"· ReplayGain …… Off/Track/Album. 목표 LU로 보정", L"· ReplayGain …… Off/Track/Album。目标 LU 校正", L"· ReplayGain …… Off/Track/Album. تصحيح LU",
		L"· ReplayGain …… Off/Track/Album. Целевой LU", L"· ReplayGain …… Off/Track/Album. Ziel-LU", L"· ReplayGain …… Off/Track/Album. LU alvo", L"· ReplayGain …… Off/Track/Album. Doel-LU",
		L"· ReplayGain …… Off/Track/Album. Docelowe LU", L"· ReplayGain …… Off/Track/Album. Hedef LU")); y += lh;
	body(L, y, LL14(L"・M/S幅・モノ互換・エクスポート制限 …… 空間感と書き出し時の頭打ち", L"· M/S width / mono / export limit …… space and export ceiling", L"· Largeur M/S / mono / plafond export", L"· Larghezza M/S / mono / tetto export",
		L"· Ancho M/S / mono / techo de exportación", L"· M/S 폭·모노·내보내기 제한", L"· M/S 宽度 / 单声道 / 导出上限", L"· عرض M/S / مونو / سقف التصدير",
		L"· Ширина M/S / моно / потолок экспорта", L"· M/S-Breite / Mono / Export-Deckel", L"· Largura M/S / mono / teto de exportação", L"· M/S-breedte / mono / exportplafond",
		L"· Szerokość M/S / mono / limit eksportu", L"· M/S genişliği / mono / dışa aktarma tavanı")); y += lh + 4;

	title(L, y, LL14(L"ループ / キュー", L"Loop / Cues", L"Boucle / Cues", L"Loop / Cue", L"Bucle / Cues", L"루프 / 큐", L"循环 / 标记", L"حلقة / إشارات",
		L"Цикл / Метки", L"Loop / Cues", L"Loop / Cues", L"Loop / Cues", L"Pętla / Cue", L"Döngü / Cue"));
	y += titleLh;
	body(L, y, LL14(L"・波形クリック …… イン点、Shift+クリック …… アウト点", L"· Wave click …… in point, Shift+click …… out point", L"· Clic onde …… entrée, Maj+clic …… sortie", L"· Clic onda …… in, Maiusc+clic …… out",
		L"· Clic onda …… entrada, Mayús+clic …… salida", L"· 파형 클릭 …… 인, Shift+클릭 …… 아웃", L"· 波形点击 …… 入点，Shift+点击 …… 出点", L"· نقر الموجة …… دخول، Shift+نقر …… خروج",
		L"· Клик по волне …… вход, Shift+клик …… выход", L"· Wellenklick …… In, Umschalt+Klick …… Out", L"· Clique na onda …… in, Shift+clique …… out", L"· Golfklik …… in, Shift+klik …… out",
		L"· Klik fali …… in, Shift+klik …… out", L"· Dalga tık …… giriş, Shift+tık …… çıkış")); y += lh;
	body(L, y, LL14(L"・ループイン／アウト／フェードms …… 適用で反映。次ループから効きます", L"· Loop in/out/fade ms …… Apply. Takes effect from next loop", L"· Boucle in/out/fondu …… Appliquer. Dès la boucle suivante", L"· Loop in/out/fade …… Applica. Dal loop successivo",
		L"· Bucle in/out/fade …… Aplicar. Desde el siguiente bucle", L"· 루프 인/아웃/페이드 …… 적용. 다음 루프부터", L"· 循环入/出/淡化 …… 应用。下一循环起生效", L"· حلقة in/out/fade …… تطبيق. من الحلقة التالية",
		L"· Цикл in/out/fade …… Применить. Со следующего цикла", L"· Loop In/Out/Fade …… Anwenden. Ab nächster Schleife", L"· Loop in/out/fade …… Aplicar. No próximo loop", L"· Loop in/out/fade …… Toepassen. Vanaf volgende loop",
		L"· Pętla in/out/fade …… Zastosuj. Od następnej pętli", L"· Döngü in/out/fade …… Uygula. Sonraki döngüden")); y += lh;
	body(L, y, LL14(L"・キュー …… 現在位置を追加／削除／ジャンプ。曲ごとの目印", L"· Cues …… add/delete/jump at current position. Per-track marks", L"· Cues …… ajouter/supprimer/sauter. Repères par piste", L"· Cue …… aggiungi/elimina/vai. Segnalibri per brano",
		L"· Cues …… añadir/borrar/saltar. Marcas por pista", L"· 큐 …… 현재 위치 추가/삭제/점프. 곡별 표시", L"· 标记 …… 在当前位置添加/删除/跳转。按曲标记", L"· إشارات …… إضافة/حذف/قفز عند الموضع",
		L"· Метки …… добавить/удалить/перейти. На трек", L"· Cues …… hinzufügen/löschen/springen. Pro Titel", L"· Cues …… adicionar/apagar/saltar. Por faixa", L"· Cues …… toevoegen/wissen/springen. Per nummer",
		L"· Cue …… dodaj/usuń/skocz. Znaczniki utworu", L"· Cue …… ekle/sil/atla. Parça işaretleri")); y += lh + 4;

	const int gx = L, gy = y, gw = min(340, rc.Width() - L * 2), gh = lh * 2 + 12;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 52, gh - 12, RGB(70, 140, 90));
	dc.FillSolidRect(gx + 64, gy + 6, 44, gh - 12, RGB(180, 140, 60));
	dc.FillSolidRect(gx + 116, gy + 6, 44, gh - 12, RGB(70, 110, 160));
	dc.FillSolidRect(gx + 168, gy + 6, 50, gh - 12, RGB(150, 70, 70));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 10, gy + 8, L"Gapless");
	dc.TextOut(gx + 72, gy + 8, L"RG");
	dc.TextOut(gx + 124, gy + 8, L"Loop");
	dc.TextOut(gx + 176, gy + 8, L"Tags");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 6;

	title(L, y, LL14(L"タグ", L"Tags", L"Tags", L"Tag", L"Etiquetas", L"태그", L"标签", L"الوسوم",
		L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiketler"));
	y += titleLh;
	body(L, y, LL14(L"・タイトル／アーティスト／アルバム等 …… ファイルへ書き込みで保存", L"· Title / artist / album …… Write tags to save into the file", L"· Titre / artiste / album …… Écrire pour enregistrer", L"· Titolo / artista / album …… Scrivi per salvare",
		L"· Título / artista / álbum …… Escribir para guardar", L"· 제목/아티스트/앨범 …… 쓰기로 파일에 저장", L"· 标题/艺人/专辑 …… 写入以保存到文件", L"· عنوان/فنان/ألبوم …… اكتب للحفظ في الملف",
		L"· Название / исполнитель / альбом …… Записать в файл", L"· Titel / Artist / Album …… In Datei schreiben", L"· Título / artista / álbum …… Escrever no ficheiro", L"· Titel / artiest / album …… Naar bestand schrijven",
		L"· Tytuł / artysta / album …… Zapisz do pliku", L"· Başlık / sanatçı / albüm …… Dosyaya yaz")); y += lh;
	muted(L, y, LL14(
		L"適用でループ等を確定。OK／閉じるでウィンドウを閉じます（反映済みの値は保持）。",
		L"Apply commits loops etc. OK/Close closes the window (keeps applied values).",
		L"Appliquer valide. OK/Fermer ferme (valeurs appliquées gardées).",
		L"Applica conferma. OK/Chiudi chiude (valori applicati restano).",
		L"Aplicar confirma. OK/Cerrar cierra (se conservan aplicados).",
		L"적용으로 루프 등을 확정. OK/닫기로 창을 닫습니다(반영값 유지).",
		L"应用确认循环等。OK/关闭关闭窗口（保留已应用值）。",
		L"تطبيق يؤكد. موافق/إغلاق يغلق (تُحفظ القيم).",
		L"Применить фиксирует. OK/Закрыть закрывает (значения остаются).",
		L"Anwenden bestätigt. OK/Schliessen schliesst (Werte bleiben).",
		L"Aplicar confirma. OK/Fechar fecha (valores aplicados ficam).",
		L"Toepassen bevestigt. OK/Sluiten sluit (toegepaste waarden blijven).",
		L"Zastosuj zatwierdza. OK/Zamknij zamyka (wartości zostają).",
		L"Uygula onaylar. Tamam/Kapat kapatır (uygulanan değerler kalır)."));

	dc.SelectObject(oldFont);
}

} // namespace

IMPLEMENT_DYNAMIC(CProToolsDlg, CCustomBlurDialogBase)
CProToolsDlg::CProToolsDlg(CWnd* pParent)
	: CCustomBlurDialogBase(IDD_PROTOOLS, pParent)
	, hasTrack(false)
	, m_peakCount(0)
	, m_totalFrames(0)
	, m_loopIn(-1)
	, m_loopOut(-1)
{
	ZeroMemory(&pc, sizeof(pc));
	ZeroMemory(m_peaksL, sizeof(m_peaksL));
	ZeroMemory(m_peaksR, sizeof(m_peaksR));
}

CProToolsDlg::~CProToolsDlg()
{
}


void CProToolsDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PT_HELP, m_help);
	DDX_Control(pDX, IDC_PRO_GAPLESS, m_gapless);	DDX_Control(pDX, IDC_PRO_RGMODE, m_rgMode);
	DDX_Control(pDX, IDC_PRO_RGTARGET, m_rgTarget);
	DDX_Control(pDX, IDC_PRO_MSWIDTH, m_msWidth);
	DDX_Control(pDX, IDC_PRO_MSVAL, m_msVal);
	DDX_Control(pDX, IDC_PRO_MSMONO, m_msMono);
	DDX_Control(pDX, IDC_PRO_EXPLIMIT, m_expLimit);
	DDX_Control(pDX, IDC_PRO_EXPCEIL, m_expCeil);
	DDX_Control(pDX, IDC_PRO_EXPTP, m_expTp);
	DDX_Control(pDX, IDC_PRO_CORR, m_corr);
	DDX_Control(pDX, IDC_PRO_CUES, m_cues);
	DDX_Control(pDX, IDC_PRO_LOOPIN, m_loopInEdit);
	DDX_Control(pDX, IDC_PRO_LOOPOUT, m_loopOutEdit);
	DDX_Control(pDX, IDC_PRO_LOOPFADE, m_loopFadeEdit);
	DDX_Control(pDX, IDC_PRO_TAG_TITLE, m_tagTitle);
	DDX_Control(pDX, IDC_PRO_TAG_ARTIST, m_tagArtist);
	DDX_Control(pDX, IDC_PRO_TAG_ALBUM, m_tagAlbum);
	DDX_Control(pDX, IDC_PRO_TAG_YEAR, m_tagYear);
	DDX_Control(pDX, IDC_PRO_TAG_TRACK, m_tagTrack);
	DDX_Control(pDX, IDC_PRO_TAG_GENRE, m_tagGenre);
	DDX_Control(pDX, IDC_PRO_TAG_COMMENT, m_tagComment);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDC_PRO_APPLY, m_apply);
	DDX_Control(pDX, IDC_PRO_CUEADD, m_cueAdd);
	DDX_Control(pDX, IDC_PRO_CUEDEL, m_cueDel);
	DDX_Control(pDX, IDC_PRO_CUEJUMP, m_cueJump);
	DDX_Control(pDX, IDC_PRO_LOOPIN_BTN, m_loopInBtn);
	DDX_Control(pDX, IDC_PRO_LOOPOUT_BTN, m_loopOutBtn);
	DDX_Control(pDX, IDC_PRO_WRITETAG, m_writeTag);
}


BEGIN_MESSAGE_MAP(CProToolsDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_PRO_APPLY, &CProToolsDlg::OnBnClickedApply)
	ON_BN_CLICKED(IDC_PRO_CUEADD, &CProToolsDlg::OnBnClickedCueAdd)
	ON_BN_CLICKED(IDC_PRO_CUEDEL, &CProToolsDlg::OnBnClickedCueDel)
	ON_BN_CLICKED(IDC_PRO_CUEJUMP, &CProToolsDlg::OnBnClickedCueJump)
	ON_BN_CLICKED(IDC_PRO_LOOPIN_BTN, &CProToolsDlg::OnBnClickedLoopIn)
	ON_BN_CLICKED(IDC_PRO_LOOPOUT_BTN, &CProToolsDlg::OnBnClickedLoopOut)
	ON_BN_CLICKED(IDC_PRO_WRITETAG, &CProToolsDlg::OnBnClickedWriteTag)
	ON_BN_CLICKED(IDC_PT_HELP, &CProToolsDlg::OnBnClickedHelp)
	ON_BN_CLICKED(IDC_PRO_GAPLESS, &CProToolsDlg::OnBnClickedLiveFlag)
	ON_BN_CLICKED(IDC_PRO_MSMONO, &CProToolsDlg::OnBnClickedLiveFlag)
	ON_BN_CLICKED(IDC_PRO_CORR, &CProToolsDlg::OnBnClickedLiveFlag)
	ON_BN_CLICKED(IDC_PRO_EXPLIMIT, &CProToolsDlg::OnBnClickedLiveFlag)
	ON_BN_CLICKED(IDC_PRO_EXPTP, &CProToolsDlg::OnBnClickedLiveFlag)
	ON_CBN_SELCHANGE(IDC_PRO_RGMODE, &CProToolsDlg::OnCbnSelchangeRgMode)
	ON_WM_HSCROLL()
	ON_EN_KILLFOCUS(IDC_PRO_RGTARGET, &CProToolsDlg::OnEnKillfocusLiveEdit)
	ON_EN_KILLFOCUS(IDC_PRO_EXPCEIL, &CProToolsDlg::OnEnKillfocusLiveEdit)
	ON_WM_LBUTTONDOWN()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()
void CProToolsDlg::LoadFromSavedata()
{
	m_gapless.SetCheck(savedata.pro_gapless ? BST_CHECKED : BST_UNCHECKED);
	CString s;
	m_rgMode.ResetContent();
	m_rgMode.AddString(LL14(L"オフ", L"Off", L"Off", L"Off", L"Off", L"끔", L"关", L"إيقاف", L"Выкл", L"Aus", L"Desl.", L"Uit", L"Wył.", L"Kapalı"));
	m_rgMode.AddString(LL14(L"トラック", L"Track", L"Piste", L"Traccia", L"Pista", L"트랙", L"曲目", L"مسار", L"Трек", L"Track", L"Faixa", L"Track", L"Utwór", L"Parça"));
	m_rgMode.AddString(LL14(L"アルバム", L"Album", L"Album", L"Album", L"Álbum", L"앨범", L"专辑", L"ألبوم", L"Альбом", L"Album", L"Álbum", L"Album", L"Album", L"Albüm"));
	m_rgMode.SetCurSel(ProClampI(savedata.pro_rg_mode, 0, 2));
	s.Format(_T("%d"), savedata.pro_rg_target);
	m_rgTarget.SetWindowText(s);
	m_msWidth.SetRange(0, 200);
	m_msWidth.SetPos(ProClampI(savedata.pro_ms_width, 0, 200));
	s.Format(_T("%d"), m_msWidth.GetPos());
	m_msVal.SetWindowText(s);
	m_msMono.SetCheck(savedata.pro_ms_mono ? BST_CHECKED : BST_UNCHECKED);
	m_expLimit.SetCheck(savedata.pro_export_limit ? BST_CHECKED : BST_UNCHECKED);
	s.Format(_T("%d"), savedata.pro_export_ceiling);
	m_expCeil.SetWindowText(s);
	m_expTp.SetCheck(savedata.pro_export_tp ? BST_CHECKED : BST_UNCHECKED);
	m_corr.SetCheck(savedata.pro_corr_meter ? BST_CHECKED : BST_UNCHECKED);
}

void CProToolsDlg::SaveToSavedata()
{
	savedata.pro_gapless = m_gapless.GetCheck() ? 1 : 0;
	savedata.pro_xfade_ms = 0;
	CString s;
	savedata.pro_rg_mode = ProClampI(m_rgMode.GetCurSel(), 0, 2);
	m_rgTarget.GetWindowText(s);
	savedata.pro_rg_target = ProClampI(_tstoi(s), -30, -1);
	savedata.pro_ms_width = ProClampI(m_msWidth.GetPos(), 0, 200);
	savedata.pro_ms_mono = m_msMono.GetCheck() ? 1 : 0;
	savedata.pro_export_limit = m_expLimit.GetCheck() ? 1 : 0;
	m_expCeil.GetWindowText(s);
	savedata.pro_export_ceiling = ProClampI(_tstoi(s), 50, 100);
	savedata.pro_export_tp = m_expTp.GetCheck() ? 1 : 0;
	savedata.pro_corr_meter = m_corr.GetCheck() ? 1 : 0;
}

void CProToolsDlg::ApplyLiveFlags()
{
	SaveToSavedata();
	CString s;
	s.Format(_T("%d"), m_msWidth.GetPos());
	m_msVal.SetWindowText(s);
	if (savedata.pro_rg_mode == 2)
		ProAudio_ComputeAlbumGainsForList(SongParams_CurrentListName());
}

void CProToolsDlg::OnBnClickedLiveFlag()
{
	ApplyLiveFlags();
}

void CProToolsDlg::OnCbnSelchangeRgMode()
{
	ApplyLiveFlags();
}

void CProToolsDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar && pScrollBar->GetDlgCtrlID() == IDC_PRO_MSWIDTH)
		ApplyLiveFlags();
	CCustomBlurDialogBase::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CProToolsDlg::OnEnKillfocusLiveEdit()
{
	ApplyLiveFlags();
}

void CProToolsDlg::RefreshCueList()
{
	m_cues.ResetContent();
	for (int i = 0; i < ProAudio_CueCount(); ++i) {
		ProCue c;
		if (!ProAudio_CueGet(i, c)) continue;
		CString s;
		int sec = (wavbit_sample_Hz > 0) ? (c.frame / wavbit_sample_Hz) : 0;
		s.Format(_T("%d: %s (%d:%02d)"), i + 1, c.label[0] ? c.label : _T("Cue"), sec / 60, sec % 60);
		m_cues.AddString(s);
	}
}

void CProToolsDlg::ApplyLoopFromUi()
{
	CString s;
	m_loopInEdit.GetWindowText(s);
	m_loopIn = _tstoi(s);
	m_loopOutEdit.GetWindowText(s);
	m_loopOut = _tstoi(s);
	m_loopFadeEdit.GetWindowText(s);
	int fade = ProClampI(_tstoi(s), 0, 5000);
	ProAudio_SetLoopOverride(m_loopIn, m_loopOut, fade);
	if (hasTrack && pl && plcnt >= 0 && plcnt < pl->playcnt) {
		if (m_loopIn >= 0) pl->pc[plcnt].loop1 = m_loopIn;
		if (m_loopOut >= 0) pl->pc[plcnt].loop2 = m_loopOut;
	}
	// 再生中グローバルにも反映(次ループから効く)
	extern int loop1, loop2;
	if (m_loopIn >= 0) loop1 = m_loopIn;
	if (m_loopOut >= 0) loop2 = m_loopOut;
}


BOOL CProToolsDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"再生詳細", L"Playback details", L"Details lecture", L"Dettagli riproduzione",
		L"Detalles de reproduccion", L"재생 상세", L"播放详情", L"تفاصيل التشغيل",
		L"Параметры воспроизведения", L"Wiedergabedetails", L"Detalhes de reproducao", L"Afspeeldetails",
		L"Szczegoly odtwarzania", L"Oynatma ayrintilari"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	LoadFromSavedata();
	if (GetDlgItem(IDC_PRO_WAVE))
		GetDlgItem(IDC_PRO_WAVE)->GetWindowRect(&m_waveRc);
	ScreenToClient(&m_waveRc);
	// キャプション化は OnShowWindow。矩形は SyncWaveRect で再同期する
	SyncWaveRect();

	if (!hasTrack && pl && plcnt >= 0 && plcnt < pl->playcnt) {
		pc = pl->pc[plcnt];
		hasTrack = true;
	}
	if (hasTrack) {
		ProAudio_SetCurrentSongKey(SongParams_CurrentListName(), pc.fol, pc.sub, pc.ret2);
		m_peakCount = ProAudio_BuildWaveOverview(pc.fol, m_peaksL, m_peaksR, PRO_WAVE_PEAKS, m_totalFrames);
		int inF, outF, fadeMs;
		ProAudio_GetLoopOverride(inF, outF, fadeMs);
		if (inF < 0) inF = pc.loop1;
		if (outF < 0) outF = pc.loop2;
		m_loopIn = inF;
		m_loopOut = outF;
		CString s;
		s.Format(_T("%d"), m_loopIn);
		m_loopInEdit.SetWindowText(s);
		s.Format(_T("%d"), m_loopOut);
		m_loopOutEdit.SetWindowText(s);
		s.Format(_T("%d"), fadeMs);
		m_loopFadeEdit.SetWindowText(s);

		FileTagFields tags;
		ReadFileTagFields(pc.fol, tags);
		m_tagTitle.SetWindowText(tags.title.IsEmpty() ? pc.name : tags.title);
		m_tagArtist.SetWindowText(tags.artist.IsEmpty() ? pc.art : tags.artist);
		m_tagAlbum.SetWindowText(tags.album.IsEmpty() ? pc.alb : tags.album);
		m_tagYear.SetWindowText(tags.year);
		m_tagTrack.SetWindowText(tags.track);
		m_tagGenre.SetWindowText(tags.genre);
		m_tagComment.SetWindowText(tags.comment);
	}

	RefreshCueList();
	SetupToolTips();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

void CProToolsDlg::SetupToolTips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this))
		return;
	auto addTip = [this](int id, LPCTSTR text) {
		CWnd* w = GetDlgItem(id);
		if (w && w->GetSafeHwnd())
			m_tooltip.AddTool(w, text);
	};
	if (m_help.GetSafeHwnd())
		m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	addTip(IDC_PRO_GAPLESS, LL14(		L"連続再生時に曲間の無音ギャップを詰めます。",
		L"Tighten silence between tracks on continuous play.",
		L"Reduit le silence entre les pistes en lecture continue.",
		L"Riduce il silenzio tra i brani in riproduzione continua.",
		L"Reduce el silencio entre pistas en reproduccion continua.",
		L"연속 재생에서 곡 사이 무음 간격을 줄입니다.",
		L"连续播放时缩短曲间静音。",
		L"تقليل الصمت بين المقاطع في التشغيل المتصل.",
		L"Сокращает паузу между треками при непрерывном воспроизведении.",
		L"Verkuerzt Stille zwischen Titeln bei Dauerwiedergabe.",
		L"Reduz o silencio entre faixas na reproducao continua.",
		L"Verkort stilte tussen nummers bij doorlopend afspelen.",
		L"Zmniejsza cisze miedzy utworami przy ciaglej.",
		L"Surekli oynatmada parcalar arasindaki sessizligi kisaltir."));
	addTip(IDC_PRO_RGMODE, LL14(
		L"ReplayGain: Off / Track / Album。再生ゲインを自動補正します。",
		L"ReplayGain: Off / Track / Album. Auto level correction.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain：关 / 曲 / 专辑。",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Aus / Track / Album.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Uit / Track / Album.",
		L"ReplayGain: Off / Track / Album.",
		L"ReplayGain: Kapali / Track / Album."));
	addTip(IDC_PRO_RGTARGET, LL14(
		L"ReplayGain の目標ラウドネス(LUFS相当のdB)。",
		L"ReplayGain target loudness (dB, LUFS-like).",
		L"Loudness cible ReplayGain (dB).",
		L"Loudness obiettivo ReplayGain (dB).",
		L"Loudness objetivo ReplayGain (dB).",
		L"ReplayGain 목표 라우드니스(dB).",
		L"ReplayGain 目标响度(dB)。",
		L"هدف الـ loudness لـ ReplayGain (dB).",
		L"Целевая громкость ReplayGain (дБ).",
		L"ReplayGain-Ziel-Lautheit (dB).",
		L"Loudness alvo ReplayGain (dB).",
		L"ReplayGain-doelluidheid (dB).",
		L"Docelowa glosnosc ReplayGain (dB).",
		L"ReplayGain hedef sesliligi (dB)."));
	addTip(IDC_PRO_MSWIDTH, LL14(
		L"Mid/Side 幅。100=通常、小さくすると狭く、大きくすると広がります。0は中央寄せ。",
		L"Mid/Side width. 100=normal; lower=narrower; higher=wider. 0=center.",
		L"Largeur Mid/Side. 100=normal.",
		L"Larghezza Mid/Side. 100=normale.",
		L"Ancho Mid/Side. 100=normal.",
		L"Mid/Side 폭. 100=보통.",
		L"Mid/Side 宽度。100=正常。",
		L"عرض Mid/Side. 100=عادي.",
		L"Ширина Mid/Side. 100=норма.",
		L"Mid/Side-Breite. 100=normal.",
		L"Largura Mid/Side. 100=normal.",
		L"Mid/Side-breedte. 100=normaal.",
		L"Szerokosc Mid/Side. 100=normal.",
		L"Mid/Side genisligi. 100=normal."));
	addTip(IDC_PRO_MSMONO, LL14(
		L"モノ互換チェック用にサイドを抑えます(モノ再生で位相問題の確認)。",
		L"Reduce Side for mono-compatibility check.",
		L"Reduit le Side pour compatibilite mono.",
		L"Riduce Side per compatibilita mono.",
		L"Reduce Side para compatibilidad mono.",
		L"모노 호환 검사용으로 Side 억제.",
		L"压低 Side 以便单声道兼容检查。",
		L"تقليل Side للتوافق الأحادي.",
		L"Приглушить Side для проверки моно.",
		L"Side dämpfen fur Mono-Kompatibilitat.",
		L"Reduz Side para checagem mono.",
		L"Side dempen voor mono-compatibiliteit.",
		L"Tlumi Side do sprawdzenia mono.",
		L"Mono uyumluluk icin Side azalt."));
	addTip(IDC_PRO_CORR, LL14(
		L"左右相関メーター(φ)を表示します。メイン画面のメーターに反映。",
		L"Show L/R correlation (φ) meter on the main UI.",
		L"Affiche le compteur de correlation L/R (φ).",
		L"Mostra il misuratore di correlazione L/R (φ).",
		L"Muestra el medidor de correlacion I/D (φ).",
		L"좌우 상관(φ) 미터 표시.",
		L"显示左右相关(φ)表。",
		L"عرض مقياس الارتباط L/R (φ).",
		L"Показать корреляцию Л/П (φ).",
		L"L/R-Korrelationsmesser (φ) anzeigen.",
		L"Mostra medidor de correlacao E/D (φ).",
		L"Toon L/R-correlatiemeter (φ).",
		L"Pokaz miernik korelacji L/P (φ).",
		L"L/R korelasyon olcerini (φ) goster."));
	addTip(IDC_PRO_EXPLIMIT, LL14(
		L"書き出し時にピークリミッターをかけます。",
		L"Apply peak limiter on export.",
		L"Applique un limiteur de crete a l'export.",
		L"Applica un limitatore di picco in esportazione.",
		L"Aplica limitador de picos al exportar.",
		L"내보내기 시 피크 리미터 적용.",
		L"导出时应用峰值限制器。",
		L"تطبيق محدد الذروة عند التصدير.",
		L"Пиковый лимитер при экспорте.",
		L"Peak-Limiter beim Export.",
		L"Aplica limitador de pico na exportacao.",
		L"Peak-limiter bij export.",
		L"Limitator szczytu przy eksporcie.",
		L"Disa aktariminda tepe sinirlayici."));
	addTip(IDC_PRO_EXPCEIL, LL14(
		L"書き出しリミッターの天井(dB)。例: -1.0",
		L"Export limiter ceiling (dB), e.g. -1.0",
		L"Plafond du limiteur d'export (dB).",
		L"Soglia del limitatore di esportazione (dB).",
		L"Techo del limitador de exportacion (dB).",
		L"내보내기 리미터 천장(dB).",
		L"导出限制器天花板(dB)。",
		L"سقف محدد التصدير (dB).",
		L"Потолок лимитера экспорта (дБ).",
		L"Export-Limiter-Ceiling (dB).",
		L"Teto do limitador de exportacao (dB).",
		L"Export-limiterplafond (dB).",
		L"Sufit limitera eksportu (dB).",
		L"Disa sinirlayici tavani (dB)."));
	addTip(IDC_PRO_EXPTP, LL14(
		L"True Peak 推定でリミッターを制御します(補間ピーク)。",
		L"Drive the limiter from estimated True Peak (interpolated).",
		L"Pilote le limiteur via True Peak estime.",
		L"Guida il limitatore con True Peak stimato.",
		L"Controla el limitador con True Peak estimado.",
		L"추정 True Peak으로 리미터 제어.",
		L"用估计 True Peak 控制限制器。",
		L"التحكم بالمحدد عبر True Peak تقديري.",
		L"Управление лимитером по оценке True Peak.",
		L"Limiter uber geschatztes True Peak steuern.",
		L"Controla o limitador com True Peak estimado.",
		L"Limiter via geschat True Peak.",
		L"Steruj limiterem przez szacowany True Peak.",
		L"Tahmini True Peak ile sinirlayiciyi yonet."));
	addTip(IDC_PRO_WAVE, LL14(
		L"波形概要。クリック=In、Shift+クリック=Out。緑=In、赤=Out。",
		L"Wave overview. Click=In, Shift+click=Out. Green=In, red=Out.",
		L"Apercu d'onde. Clic=In, Maj+clic=Out.",
		L"Anteprima onda. Clic=In, Maiusc+clic=Out.",
		L"Vista de onda. Clic=In, Mayus+clic=Out.",
		L"파형 개요. 클릭=In, Shift+클릭=Out.",
		L"波形概览。单击=In，Shift+单击=Out。",
		L"نظرة على الموجة. نقر=In، Shift+نقر=Out.",
		L"Обзор волны. Клик=In, Shift+клик=Out.",
		L"Wellenuberblick. Klick=In, Umschalt+Klick=Out.",
		L"Visao da onda. Clique=In, Shift+clique=Out.",
		L"Golfoverzicht. Klik=In, Shift+klik=Out.",
		L"Podglad fali. Klik=In, Shift+klik=Out.",
		L"Dalga ozeti. Tik=In, Shift+tik=Out."));
	addTip(IDC_PRO_LOOPIN, LL14(L"ループ開始フレーム。", L"Loop start frame.", L"Frame de debut de boucle.", L"Frame inizio loop.", L"Frame de inicio de bucle.", L"루프 시작 프레임.", L"循环起始帧。", L"إطار بداية الحلقة.", L"Кадр начала цикла.", L"Loop-Startframe.", L"Frame de inicio do loop.", L"Loop-startframe.", L"Ramka poczatku petli.", L"Dongu baslangic karesi."));
	addTip(IDC_PRO_LOOPOUT, LL14(L"ループ終了フレーム。", L"Loop end frame.", L"Frame de fin de boucle.", L"Frame fine loop.", L"Frame de fin de bucle.", L"루프 종료 프레임.", L"循环结束帧。", L"إطار نهاية الحلقة.", L"Кадр конца цикла.", L"Loop-Endframe.", L"Frame de fim do loop.", L"Loop-eindframe.", L"Ramka konca petli.", L"Dongu bitis karesi."));
	addTip(IDC_PRO_LOOPFADE, LL14(L"ループ境界のフェード(ms)。", L"Fade at loop boundary (ms).", L"Fondu a la boucle (ms).", L"Fade al bordo del loop (ms).", L"Fundido en el bucle (ms).", L"루프 경계 페이드(ms).", L"循环边界淡化(ms)。", L"تلاشي عند حدود الحلقة (ms).", L"Фейд на границе цикла (мс).", L"Fade an der Loop-Grenze (ms).", L"Fade no limite do loop (ms).", L"Fade bij lusgrens (ms).", L"Fade na granicy petli (ms).", L"Dongu sinirinda fade (ms)."));
	addTip(IDC_PRO_LOOPIN_BTN, LL14(L"現在の再生位置を In に設定。", L"Set In to current playback position.", L"Definir In a la position actuelle.", L"Imposta In sulla posizione attuale.", L"Poner In en la posicion actual.", L"현재 재생 위치를 In으로.", L"将当前位置设为 In。", L"تعيين In إلى الموضع الحالي.", L"Поставить In на текущую позицию.", L"In auf aktuelle Position setzen.", L"Definir In na posicao atual.", L"Zet In op huidige positie.", L"Ustaw In na biezaca pozycje.", L"In'i mevcut konuma ayarla."));
	addTip(IDC_PRO_LOOPOUT_BTN, LL14(L"現在の再生位置を Out に設定。", L"Set Out to current playback position.", L"Definir Out a la position actuelle.", L"Imposta Out sulla posizione attuale.", L"Poner Out en la posicion actual.", L"현재 재생 위치를 Out으로.", L"将当前位置设为 Out。", L"تعيين Out إلى الموضع الحالي.", L"Поставить Out на текущую позицию.", L"Out auf aktuelle Position setzen.", L"Definir Out na posicao atual.", L"Zet Out op huidige positie.", L"Ustaw Out na biezaca pozycje.", L"Out'u mevcut konuma ayarla."));
	addTip(IDC_PRO_CUES, LL14(L"キューポイント一覧。Jump でシーク。", L"Cue list. Jump seeks to the cue.", L"Liste des cues. Jump seek.", L"Elenco cue. Jump cerca.", L"Lista de cues. Jump busca.", L"큐 목록. Jump로 시크.", L"标记列表。Jump 跳转。", L"قائمة الإشارات. Jump للانتقال.", L"Список меток. Jump — переход.", L"Cue-Liste. Jump springt hin.", L"Lista de cues. Jump busca.", L"Cue-lijst. Jump zoekt.", L"Lista cue. Jump przewija.", L"Cue listesi. Jump atlar."));
	addTip(IDC_PRO_CUEADD, LL14(L"現在位置にキューを追加。", L"Add cue at current position.", L"Ajouter un cue a la position actuelle.", L"Aggiungi cue alla posizione attuale.", L"Anadir cue en la posicion actual.", L"현재 위치에 큐 추가.", L"在当前位置添加标记。", L"إضافة إشارة في الموضع الحالي.", L"Добавить метку на текущей позиции.", L"Cue an aktueller Position hinzufugen.", L"Adicionar cue na posicao atual.", L"Cue toevoegen op huidige positie.", L"Dodaj cue na biezacej pozycji.", L"Mevcut konuma cue ekle."));
	addTip(IDC_PRO_CUEDEL, LL14(L"選択中のキューを削除。", L"Delete selected cue.", L"Supprimer le cue selectionne.", L"Elimina il cue selezionato.", L"Eliminar el cue seleccionado.", L"선택한 큐 삭제.", L"删除所选标记。", L"حذف الإشارة المحددة.", L"Удалить выбранную метку.", L"Ausgewahlten Cue loschen.", L"Excluir cue selecionado.", L"Geselecteerde cue verwijderen.", L"Usun zaznaczony cue.", L"Secili cue'yu sil."));
	addTip(IDC_PRO_CUEJUMP, LL14(L"選択キューへシーク。", L"Seek to selected cue.", L"Aller au cue selectionne.", L"Vai al cue selezionato.", L"Ir al cue seleccionado.", L"선택한 큐로 이동.", L"跳到所选标记。", L"الانتقال إلى الإشارة المحددة.", L"Перейти к выбранной метке.", L"Zum ausgewahlten Cue springen.", L"Ir ao cue selecionado.", L"Ga naar geselecteerde cue.", L"Przejdz do zaznaczonego cue.", L"Secili cue'ya git."));
	addTip(IDC_PRO_TAG_TITLE, LL14(L"タイトルタグ。", L"Title tag.", L"Balise titre.", L"Tag titolo.", L"Etiqueta titulo.", L"제목 태그.", L"标题标签。", L"وسم العنوان.", L"Тег названия.", L"Titel-Tag.", L"Tag de titulo.", L"Titel-tag.", L"Tag tytulu.", L"Baslik etiketi."));
	addTip(IDC_PRO_TAG_ARTIST, LL14(L"アーティストタグ。", L"Artist tag.", L"Balise artiste.", L"Tag artista.", L"Etiqueta artista.", L"아티스트 태그.", L"艺术家标签。", L"وسم الفنان.", L"Тег исполнителя.", L"Kunstler-Tag.", L"Tag de artista.", L"Artiest-tag.", L"Tag artysty.", L"Sanatci etiketi."));
	addTip(IDC_PRO_TAG_ALBUM, LL14(L"アルバムタグ。", L"Album tag.", L"Balise album.", L"Tag album.", L"Etiqueta album.", L"앨범 태그.", L"专辑标签。", L"وسم الألبوم.", L"Тег альбома.", L"Album-Tag.", L"Tag de album.", L"Album-tag.", L"Tag albumu.", L"Album etiketi."));
	addTip(IDC_PRO_TAG_YEAR, LL14(L"年タグ。", L"Year tag.", L"Balise annee.", L"Tag anno.", L"Etiqueta ano.", L"연도 태그.", L"年份标签。", L"وسم السنة.", L"Тег года.", L"Jahr-Tag.", L"Tag de ano.", L"Jaar-tag.", L"Tag roku.", L"Yil etiketi."));
	addTip(IDC_PRO_TAG_TRACK, LL14(L"トラック番号タグ。", L"Track number tag.", L"Balise numero de piste.", L"Tag numero traccia.", L"Etiqueta numero de pista.", L"트랙 번호 태그.", L"曲目标签。", L"وسم رقم المقطع.", L"Тег номера трека.", L"Titelnummer-Tag.", L"Tag de numero da faixa.", L"Tracknummer-tag.", L"Tag numeru utworu.", L"Parca no etiketi."));
	addTip(IDC_PRO_TAG_GENRE, LL14(L"ジャンルタグ。", L"Genre tag.", L"Balise genre.", L"Tag genere.", L"Etiqueta genero.", L"장르 태그.", L"流派标签。", L"وسم النوع.", L"Тег жанра.", L"Genre-Tag.", L"Tag de genero.", L"Genre-tag.", L"Tag gatunku.", L"Tur etiketi."));
	addTip(IDC_PRO_TAG_COMMENT, LL14(
		L"コメント。ループ上書きは LOOPSTART/END/LENGTH も書き込めます。",
		L"Comment. Loop overrides can also write LOOPSTART/END/LENGTH.",
		L"Commentaire. Peut aussi ecrire LOOPSTART/END/LENGTH.",
		L"Commento. Puo anche scrivere LOOPSTART/END/LENGTH.",
		L"Comentario. Tambien puede escribir LOOPSTART/END/LENGTH.",
		L"코멘트. LOOPSTART/END/LENGTH 도 기록 가능.",
		L"注释。也可写入 LOOPSTART/END/LENGTH。",
		L"تعليق. يمكن أيضاً كتابة LOOPSTART/END/LENGTH.",
		L"Комментарий. Также LOOPSTART/END/LENGTH.",
		L"Kommentar. Kann auch LOOPSTART/END/LENGTH schreiben.",
		L"Comentario. Tambem LOOPSTART/END/LENGTH.",
		L"Opmerking. Kan ook LOOPSTART/END/LENGTH schrijven.",
		L"Komentarz. Moze tez zapisac LOOPSTART/END/LENGTH.",
		L"Yorum. LOOPSTART/END/LENGTH de yazabilir."));
	addTip(IDC_PRO_WRITETAG, LL14(
		L"表示中のタグをファイルへ書き込みます(形式により対応範囲が異なります)。",
		L"Write displayed tags to the file (support varies by format).",
		L"Ecrire les tags affiches dans le fichier.",
		L"Scrive i tag visualizzati nel file.",
		L"Escribe las etiquetas mostradas en el archivo.",
		L"표시 태그를 파일에 기록(형식별 지원 범위 다름).",
		L"将显示的标签写入文件（因格式而异）。",
		L"كتابة الوسوم المعروضة إلى الملف.",
		L"Записать отображаемые теги в файл.",
		L"Angezeigte Tags in die Datei schreiben.",
		L"Gravar tags exibidas no arquivo.",
		L"Schrijf getoonde tags naar het bestand.",
		L"Zapisz wyswietlone tagi do pliku.",
		L"Gorunen etiketleri dosyaya yaz."));
	addTip(IDC_PRO_APPLY, LL14(
		L"設定を保存し、ループ上書きなどを即時反映します。",
		L"Save settings and apply loop overrides immediately.",
		L"Enregistre et applique (boucles etc.) tout de suite.",
		L"Salva e applica subito (loop ecc.).",
		L"Guarda y aplica de inmediato (bucles etc.).",
		L"설정 저장 및 루프 등 즉시 반영.",
		L"保存设置并立即应用（循环等）。",
		L"حفظ وتطبيق فوري (الحلقات وغيرها).",
		L"Сохранить и сразу применить (циклы и т.д.).",
		L"Einstellungen speichern und sofort anwenden.",
		L"Salvar e aplicar imediatamente.",
		L"Opslaan en meteen toepassen.",
		L"Zapisz i zastosuj od razu.",
		L"Ayarlari kaydet ve hemen uygula."));
	addTip(IDOK, LL14(
		L"閉じます(変更は適用済みなら保持されます)。",
		L"Close (keeps applied changes).",
		L"Fermer (conserve les changements appliques).",
		L"Chiudi (mantiene le modifiche applicate).",
		L"Cerrar (conserva los cambios aplicados).",
		L"닫기(적용된 변경은 유지).",
		L"关闭（保留已应用的更改）。",
		L"إغلاق (تُحفظ التغييرات المطبّقة).",
		L"Закрыть (применённые изменения сохраняются).",
		L"Schliessen (angewendete Anderungen bleiben).",
		L"Fechar (mantem alteracoes aplicadas).",
		L"Sluiten (toegepaste wijzigingen blijven).",
		L"Zamknij (zachowuje zastosowane zmiany).",
		L"Kapat (uygulanan degisiklikler kalir)."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
}

BOOL CProToolsDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CProToolsDlg::SyncWaveRect()
{
	CWnd* wave = GetDlgItem(IDC_PRO_WAVE);
	if (!wave || !::IsWindow(wave->GetSafeHwnd()))
		return;
	wave->GetWindowRect(&m_waveRc);
	ScreenToClient(&m_waveRc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	// キャプション帯に食い込まない（Install 前に取った座標の残骸対策）
	if (capH > 0 && m_waveRc.top < capH)
		m_waveRc.top = capH;
	if (m_waveRc.bottom <= m_waveRc.top + 8)
		m_waveRc.bottom = m_waveRc.top + 50;
	// 親が描くので枠 Static は隠す（帯アクリルと二重にならない）
	wave->ShowWindow(SW_HIDE);
}

void CProToolsDlg::DrawWave(CDC& dc, const CRect& rc)
{
	if (rc.Width() <= 0 || rc.Height() <= 0) return;

	CDC mem;
	CBitmap bmp;
	mem.CreateCompatibleDC(&dc);
	bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	CRect local(0, 0, rc.Width(), rc.Height());
	mem.FillSolidRect(local, RGB(24, 24, 28));
	if (m_peakCount > 1) {
		const int mid = local.Height() / 2;
		const int half = (local.Height() / 2) - 2;
		CPen penL(PS_SOLID, 1, RGB(80, 180, 255));
		CPen penR(PS_SOLID, 1, RGB(255, 140, 80));
		CPen* oldPen = mem.SelectObject(&penL);
		for (int i = 0; i < m_peakCount; ++i) {
			int x = i * local.Width() / m_peakCount;
			int h = (int)(m_peaksL[i] * half);
			mem.MoveTo(x, mid);
			mem.LineTo(x, mid - h);
		}
		mem.SelectObject(&penR);
		for (int i = 0; i < m_peakCount; ++i) {
			int x = i * local.Width() / m_peakCount;
			int h = (int)(m_peaksR[i] * half);
			mem.MoveTo(x, mid);
			mem.LineTo(x, mid + h);
		}
		mem.SelectObject(oldPen);
		if (m_totalFrames > 0) {
			auto mark = [&](int frame, COLORREF col) {
				if (frame < 0) return;
				int x = (int)((__int64)frame * local.Width() / m_totalFrames);
				CPen p(PS_SOLID, 2, col);
				CPen* o = mem.SelectObject(&p);
				mem.MoveTo(x, 0);
				mem.LineTo(x, local.bottom);
				mem.SelectObject(o);
			};
			mark(m_loopIn, RGB(80, 255, 120));
			mark(m_loopOut, RGB(255, 80, 80));
		}
	}
#if CCUSTOM_AERO_SUPPORT
	if (CCC_AcrylicCaption(m_hWnd) || CCC_IsAeroEnabled()) {
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), rc.left, rc.top, rc.Width(), rc.Height(),
			mem.GetSafeHdc(), 0, 0, rc.Width(), rc.Height());
	}
	else
#endif
	{
		dc.BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	}
	mem.SelectObject(old);
}

void CProToolsDlg::OnPaint()
{
	CPaintDC dc(this);
	SyncWaveRect();
	DrawWave(dc, m_waveRc);
	CCC_CaptionPaint(dc, m_hWnd);
}

BOOL CProToolsDlg::OnEraseBkgnd(CDC* pDC)
{
	return CCustomBlurDialogBase::OnEraseBkgnd(pDC);
}


void CProToolsDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	SyncWaveRect();
	Invalidate(FALSE);
}

void CProToolsDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CProToolsDlg::ShowHelpSheet()
{
	if (g_ptHelpDlg && ::IsWindow(g_ptHelpDlg->GetSafeHwnd())) {
		g_ptHelpDlg->ShowWindow(SW_SHOW);
		g_ptHelpDlg->SetForegroundWindow();
		return;
	}
	if (g_ptHelpDlg && !::IsWindow(g_ptHelpDlg->GetSafeHwnd()))
		g_ptHelpDlg = nullptr;
	CPtHelpDlg* dlg = new CPtHelpDlg(nullptr);
	if (!dlg->Create(IDD_PT_HELP, nullptr)) {
		delete dlg;
		return;
	}
	g_ptHelpDlg = dlg;
	dlg->ShowWindow(SW_SHOW);
	dlg->SetForegroundWindow();
}

void CProToolsDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CProToolsDlg::OnDestroy()
{
	if (g_ptHelpDlg && ::IsWindow(g_ptHelpDlg->GetSafeHwnd()))
		g_ptHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}
void CProToolsDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_waveRc.PtInRect(point) && m_totalFrames > 0) {
		int frame = (int)((__int64)(point.x - m_waveRc.left) * m_totalFrames / m_waveRc.Width());
		if (GetKeyState(VK_SHIFT) & 0x8000)
			m_loopOut = frame;
		else
			m_loopIn = frame;
		CString s;
		s.Format(_T("%d"), m_loopIn);
		m_loopInEdit.SetWindowText(s);
		s.Format(_T("%d"), m_loopOut);
		m_loopOutEdit.SetWindowText(s);
		InvalidateRect(m_waveRc, FALSE);
	}
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
}

void CProToolsDlg::OnBnClickedApply()
{
	ApplyLiveFlags();
	ApplyLoopFromUi();
}

void CProToolsDlg::CloseModeless()
{
	ApplyLiveFlags();
	ApplyLoopFromUi();
	if (::IsWindow(GetSafeHwnd()))
		DestroyWindow();
}

void CProToolsDlg::OnOK()
{
	CloseModeless();
}

void CProToolsDlg::OnCancel()
{
	CloseModeless();
}

void CProToolsDlg::OnClose()
{
	CloseModeless();
}

void CProToolsDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_proToolsDlg == this)
		g_proToolsDlg = nullptr;
	delete this;
}

void CProToolsDlg::OnBnClickedCueAdd()
{
	int frame = (int)playb;
	CString label;
	label.Format(_T("Cue%d"), ProAudio_CueCount() + 1);
	ProAudio_CueAdd(frame, label);
	RefreshCueList();
}

void CProToolsDlg::OnBnClickedCueDel()
{
	int i = m_cues.GetCurSel();
	if (i >= 0) {
		ProAudio_CueRemove(i);
		RefreshCueList();
	}
}

void CProToolsDlg::OnBnClickedCueJump()
{
	int i = m_cues.GetCurSel();
	ProCue c;
	if (!ProAudio_CueGet(i, c)) return;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_APP_PROAUDIO_CUESEEK, 0, (LPARAM)c.frame);
}

void CProToolsDlg::OnBnClickedLoopIn()
{
	m_loopIn = (int)playb;
	CString s;
	s.Format(_T("%d"), m_loopIn);
	m_loopInEdit.SetWindowText(s);
	InvalidateRect(m_waveRc, FALSE);
}

void CProToolsDlg::OnBnClickedLoopOut()
{
	m_loopOut = (int)playb;
	CString s;
	s.Format(_T("%d"), m_loopOut);
	m_loopOutEdit.SetWindowText(s);
	InvalidateRect(m_waveRc, FALSE);
}

void CProToolsDlg::OnBnClickedWriteTag()
{
	if (!hasTrack) return;
	FileTagFields tags;
	m_tagTitle.GetWindowText(tags.title);
	m_tagArtist.GetWindowText(tags.artist);
	m_tagAlbum.GetWindowText(tags.album);
	m_tagYear.GetWindowText(tags.year);
	m_tagTrack.GetWindowText(tags.track);
	m_tagGenre.GetWindowText(tags.genre);
	m_tagComment.GetWindowText(tags.comment);
	tags.loop1 = m_loopIn > 0 ? m_loopIn : 0;
	tags.loop2 = m_loopOut > 0 ? m_loopOut : 0;
	if (!WriteFileTagFields(pc.fol, tags)) {
		AfxMessageBox(LL14(
			L"タグの書き込みに失敗しました。\n対応: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Failed to write tags.\nSupported: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Echec ecriture tags.\nPris en charge: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Scrittura tag non riuscita.\nSupportati: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Error al escribir etiquetas.\nCompatible: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"태그 쓰기 실패.\n지원: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"写入标签失败。\n支持: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"فشل كتابة الوسوم.\nالمدعوم: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Не удалось записать теги.\nПоддержка: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Tag schreiben fehlgeschlagen.\nUnterstuetzt: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Falha ao gravar tags.\nSuportado: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Tags schrijven mislukt.\nOndersteund: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Nie udało się zapisać tagów.\nObsługa: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Etiket yazılamadı.\nDestek: MP3 / FLAC / WAV / M4A / Ogg Vorbis"), MB_ICONWARNING);
		return;
	}
	{
		CString s;
		m_loopFadeEdit.GetWindowText(s);
		int fade = ProClampI(_tstoi(s), 0, 5000);
		ProAudio_SetLoopOverride(m_loopIn, m_loopOut, fade);
		extern int loop1, loop2;
		if (m_loopIn >= 0) loop1 = m_loopIn;
		if (m_loopOut >= 0) loop2 = m_loopOut;
	}
	_tcsncpy(pc.name, tags.title, 1023);
	pc.name[1023] = 0;
	_tcsncpy(pc.art, tags.artist, 1023);
	pc.art[1023] = 0;
	_tcsncpy(pc.alb, tags.album, 1023);
	pc.alb[1023] = 0;
	if (pl && plcnt >= 0 && plcnt < pl->playcnt) {
		_tcscpy(pl->pc[plcnt].name, pc.name);
		_tcscpy(pl->pc[plcnt].art, pc.art);
		_tcscpy(pl->pc[plcnt].alb, pc.alb);
	}
	AfxMessageBox(LL14(L"タグを書き込みました。", L"Tags written.", L"Tags ecrits.", L"Tag scritti.",
		L"Etiquetas escritas.", L"태그 저장됨.", L"标签已写入。", L"تمت الكتابة.",
		L"Теги записаны.", L"Tags geschrieben.", L"Tags gravadas.", L"Tags geschreven.",
		L"Tagi zapisane.", L"Etiketler yazıldı."), MB_ICONINFORMATION);
}

void CProToolsDlg::LoadTrackFromSelection()
{
	hasTrack = false;
	ZeroMemory(&pc, sizeof(pc));
	extern CPlayList* pl;
	extern int plcnt;
	if (pl) {
		int idx = pl->m_lc.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
		if (idx < 0 && plcnt >= 0 && plcnt < pl->playcnt)
			idx = plcnt;
		if (idx >= 0 && idx < pl->playcnt) {
			pc = pl->pc[idx];
			hasTrack = true;
		}
	}
}

void CloseProToolsIfOpen()
{
	if (g_proToolsDlg && ::IsWindow(g_proToolsDlg->GetSafeHwnd()))
		g_proToolsDlg->DestroyWindow(); // PostNcDestroy で delete / nullptr
}

void OpenProToolsForSelection()
{
	CWnd* parent = NULL;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
		parent = mp;
	else if (og && ::IsWindow(og->GetSafeHwnd()))
		parent = og;
	else
		parent = AfxGetMainWnd();

	if (g_proToolsDlg && ::IsWindow(g_proToolsDlg->GetSafeHwnd())) {
		g_proToolsDlg->ShowWindow(SW_SHOW);
		g_proToolsDlg->SetForegroundWindow();
		return;
	}

	g_proToolsDlg = new CProToolsDlg(parent);
	g_proToolsDlg->LoadTrackFromSelection();
	if (!g_proToolsDlg->Create(IDD_PROTOOLS, parent)) {
		delete g_proToolsDlg;
		g_proToolsDlg = nullptr;
		return;
	}
	g_proToolsDlg->ShowWindow(SW_SHOW);
	g_proToolsDlg->SetForegroundWindow();
}
