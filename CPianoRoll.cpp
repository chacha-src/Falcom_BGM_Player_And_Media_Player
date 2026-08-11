#include "stdafx.h"
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "CPianoRoll.h"
#include "MpPlayerAddons.h"
#include "CEqualizer.h"
#include "DeviceRecordDlg.h"
#include "oggDlg.h"
#include "CMediaPlayerDlg.h"

class COggDlg;
extern COggDlg* og;
#include "resource.h"
#include "NoteFundamentalPick.h"
#include "PianoRollPick.h"
#include "PianoRoll108Detect.h"
#include "PianoKeyTable.h"
#include "HarmonicProfile.h"
#include "PianoRollGoertzelAvx2.h"

extern save savedata;
extern int tempo;
extern int pitch;
void COggDlg_SyncPianoRollFast();
void COggDlg_ShowPianoRollTune();

static int PrTuneClampPct(int v)
{
	if (v <= 0) return 100;
	if (v < 25) return 25;
	if (v > 400) return 400;
	return v;
}

static float PrTuneF(int pct, float defVal)
{
	return defVal * (float)PrTuneClampPct(pct) / 100.0f;
}


namespace {

class CPrHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_PR_HELP };
	explicit CPrHelpDlg(CWnd* pParent = nullptr)
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

static CPrHelpDlg* g_prHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CPrHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CPrHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"ピアノロール操作ガイド", L"Piano Roll Guide", L"Guide piano roll", L"Guida piano roll",
		L"Guía de piano roll", L"피아노롤 가이드", L"钢琴卷帘指南", L"دليل لفة البيانو",
		L"Руководство piano roll", L"Piano-Roll-Anleitung", L"Guia piano roll", L"Piano roll-gids",
		L"Przewodnik piano roll", L"Piano roll kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CPrHelpDlg::OnOK() { DestroyWindow(); }
void CPrHelpDlg::OnCancel() { DestroyWindow(); }
void CPrHelpDlg::OnClose() { DestroyWindow(); }

void CPrHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_prHelpDlg == this)
		g_prHelpDlg = nullptr;
	delete this;
}

BOOL CPrHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CPrHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* baseFont = GetFont();
	CFont boldFont;
	{
		LOGFONT lf = {};
		if (baseFont && baseFont->GetSafeHandle())
			baseFont->GetLogFont(&lf);
		else {
			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
			lf = ncm.lfMessageFont;
		}
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
	}
	CFont* oldFont = dc.SelectObject(baseFont);

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 2;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		CFont* prev = dc.SelectObject(&boldFont);
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, t);
		dc.SelectObject(prev);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 120));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"簡易ピアノロール操作ガイド", L"Simple Piano Roll — Guide", L"Piano roll simple — Guide", L"Piano roll semplice — Guida",
		L"Piano roll simple — Guía", L"간이 피아노롤 — 가이드", L"简易钢琴卷帘 — 指南", L"لفة بيانو بسيطة — دليل",
		L"Простой piano roll — руководство", L"Einfache Klavierrolle — Guide", L"Piano roll simples — Guia", L"Eenvoudige pianorol — Gids",
		L"Prosta rolka — przewodnik", L"Basit piyano rulosu — kılavuz"));
	y += titleLh;
	muted(L, y, LL14(
		L"再生音声からノートを推定し、鍵盤とロールで同期表示します（鍵盤MIDI入力ではありません。録りは右クリック）。",
		L"Estimates notes from playback and syncs keys + roll (not keyboard MIDI in; capture via RMB).",
		L"Estime les notes depuis l'audio (pas entrée MIDI clavier ; enreg. via clic droit).",
		L"Stima le note dall'audio (non MIDI da tastiera; registrazione via destro).",
		L"Estima notas del audio (no es MIDI de teclado; captura con clic der.).",
		L"재생 오디오에서 노트 추정(건반 MIDI 입력 아님. 녹음은 우클릭).",
		L"从播放音频估计音符（非键盘 MIDI 输入；录制见右键）。",
		L"يقدّر النغمات من الصوت (ليس إدخال MIDI؛ التسجيل من اليمين).",
		L"Оценивает ноты из аудио (не MIDI-клавиатура; запись через ПКМ).",
		L"Schätzt Noten aus Audio (kein Tastatur-MIDI; Aufnahme per RMB).",
		L"Estima notas do áudio (não MIDI de teclado; captura no direito).",
		L"Schat noten uit audio (geen toetsenbord-MIDI; opname via RMB).",
		L"Szacuje nuty z audio (to nie MIDI klawiatury; zapis przez PPM).",
		L"Çalmadan nota tahmin eder (klavye MIDI değil; kayıt sağ tık)."));
	y += lh + 4;

	title(L, y, LL14(L"表示と速度", L"View & speed", L"Affichage et vitesse", L"Vista e velocità",
		L"Vista y velocidad", L"표시와 속도", L"显示与速度", L"العرض والسرعة",
		L"Вид и скорость", L"Ansicht & Tempo", L"Vista e velocidade", L"Weergave & snelheid",
		L"Widok i prędkość", L"Görünüm ve hız"));
	y += titleLh;
	body(L, y, LL14(
		L"・表示モード …… 通常(2D) / 簡易3D。3D はドラッグで視点、ホイールでズーム",
		L"· View …… Normal (2D) / Soft 3D. In 3D: drag to orbit, wheel to zoom",
		L"· Vue …… Normal (2D) / 3D. En 3D : glisser / molette",
		L"· Vista …… Normale (2D) / 3D. In 3D: trascina / rotella",
		L"· Vista …… Normal (2D) / 3D. En 3D: arrastrar / rueda",
		L"· 표시 …… 일반(2D) / 간이3D. 3D는 드래그·휠 줌",
		L"· 显示 …… 普通(2D) / 简易3D。3D 可拖动视角、滚轮缩放",
		L"· العرض …… عادي (2D) / 3D. في 3D: سحب / عجلة",
		L"· Вид …… обычный (2D) / 3D. В 3D: перетаскивание / колесо",
		L"· Ansicht …… Normal (2D) / 3D. In 3D: ziehen / Rad",
		L"· Vista …… Normal (2D) / 3D. Em 3D: arrastar / roda",
		L"· Weergave …… Normaal (2D) / 3D. In 3D: slepen / wiel",
		L"· Widok …… zwykły (2D) / 3D. W 3D: przeciągnij / kółko",
		L"· Görünüm …… Normal (2D) / 3B. 3B'de sürükle / tekerlek")); y += lh + 2;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KPIANO);

	title(L, y, LL14(L"簡易3D(Soft3D)", L"Soft 3D", L"3D simplifiée", L"3D semplificato",
		L"3D simple", L"간이 3D(Soft3D)", L"简易3D(Soft3D)", L"Soft 3D",
		L"Простой 3D", L"Soft 3D", L"Soft 3D", L"Eenvoudig 3D",
		L"Soft 3D", L"Soft 3B"));
	y += titleLh;
	body(L, y, LL14(
		L"・右クリック「表示モード」→ 簡易3D。CPU 描画のみ(OpenGL/Direct3D 不使用)。全面が1枚のシーン",
		L"· Right-click View → Soft 3D. CPU-only (no OpenGL/Direct3D). One full-client scene",
		L"· Clic droit Affichage → Soft 3D (CPU seul). Une scène plein client",
		L"· Destro Vista → Soft 3D (solo CPU). Una scena a tutto client",
		L"· Clic der. Vista → Soft 3D (solo CPU). Una escena a pantalla completa",
		L"· 우클릭 「표시 모드」→ 간이 3D. CPU만(OpenGL/Direct3D 없음). 클라이언트 전체 1장면",
		L"· 右键「显示模式」→ 简易3D。仅 CPU（无 OpenGL/Direct3D）。整窗一场景",
		L"· يمين ← العرض ← Soft 3D (معالج فقط). مشهد واحد لكامل النافذة",
		L"· ПКМ «Вид» → Soft 3D (только CPU). Одна сцена на всё окно",
		L"· Rechtsklick Ansicht → Soft 3D (nur CPU). Eine Szene fuer das ganze Fenster",
		L"· Direito Exibir → Soft 3D (so CPU). Uma cena em todo o cliente",
		L"· Rechtsklik Weergave → Soft 3D (alleen CPU). Een scene voor heel het venster",
		L"· PPM Widok → Soft 3D (tylko CPU). Jedna scena na całe okno",
		L"· Sağ tık Görünüm → Soft 3B (yalnızca CPU). Tüm pencere tek sahne")); y += lh;
	body(L, y, LL14(
		L"・左ドラッグで視点回転、ホイールでズーム。メニューの Yaw/Pitch/Zoom スライダーでも微調整",
		L"· Left-drag orbits, wheel zooms. Yaw/Pitch/Zoom sliders in the menu fine-tune too",
		L"· Glisser = orbite, molette = zoom. Curseurs Yaw/Pitch/Zoom aussi",
		L"· Trascina = orbita, rotella = zoom. Anche slider Yaw/Pitch/Zoom",
		L"· Arrastrar = órbita, rueda = zoom. También deslizadores Yaw/Pitch/Zoom",
		L"· 좌드래그=시점 회전, 휠=줌. 메뉴 Yaw/Pitch/Zoom 슬라이더로도 미세 조정",
		L"· 左键拖=旋转，滚轮=缩放。菜单 Yaw/Pitch/Zoom 滑块也可微调",
		L"· سحب أيسر=دوران، عجلة=تكبير. منزلقات Yaw/Pitch/Zoom أيضاً",
		L"· ЛКМ — облёт, колесо — зум. Также ползунки Yaw/Pitch/Zoom",
		L"· Linksziehen = Orbit, Rad = Zoom. Auch Yaw/Pitch/Zoom-Slider",
		L"· Arrastar esq. = órbita, roda = zoom. Também sliders Yaw/Pitch/Zoom",
		L"· Links slepen = orbit, wiel = zoom. Ook Yaw/Pitch/Zoom-sliders",
		L"· Przeciąganie LPM = orbita, kółko = zoom. Też suwaki Yaw/Pitch/Zoom",
		L"· Sol sürükle = yörünge, teker = zoom. Yaw/Pitch/Zoom kaydırıcıları da")); y += lh;
	body(L, y, LL14(
		L"・0 キーまたは「視点をリセット」で既定カメラへ。初回 ON 時は操作ヒントが出ます。重いときは 2D に",
		L"· Press 0 or Reset view for the default camera. First ON shows a tip. Switch to 2D if heavy",
		L"· 0 ou Réinitialiser la vue = caméra défaut. Astuce au 1er ON. Passez en 2D si lourd",
		L"· 0 o Reimposta vista = camera default. Suggerimento al 1° ON. Torna al 2D se pesante",
		L"· 0 o Restablecer vista = cámara pred. Pista al 1.er ON. Pase a 2D si va lento",
		L"· 0 키 또는 「시점 재설정」으로 기본 카메라. 최초 ON 시 힌트. 무거우면 2D로",
		L"· 按 0 或「重置视角」回默认相机。首次开启有提示。卡顿时切回 2D",
		L"· 0 أو إعادة العرض = الكاميرا الافتراضية. تلميح عند التشغيل الأول. عد إلى 2D إن ثقل",
		L"· 0 или «Сбросить вид» — камера по умолч. Подсказка при первом ON. Вернитесь в 2D если тяжело",
		L"· Taste 0 oder Ansicht zuruecksetzen = Standardkamera. Hinweis beim ersten ON. Bei Last zu 2D",
		L"· 0 ou Redefinir vista = camera padrao. Dica no 1.o ON. Volte ao 2D se pesado",
		L"· 0 of Weergave resetten = standaardcamera. Hint bij eerste ON. Naar 2D als zwaar",
		L"· 0 lub Resetuj widok = kamera domyślna. Podpowiedź przy pierwszym ON. Wróć do 2D gdy ciężko",
		L"· 0 veya Görünümü sıfırla = varsayılan kamera. İlk açılışta ipucu. Ağırsa 2B'ye dön")); y += lh;
	body(L, y, LL14(
		L"・ボタン等の CCustom にも Soft3D の小さな飾りが入ります（全面シーンとは別レイヤ）",
		L"· CCustom buttons also get tiny Soft 3D accents (separate from the full Soft 3D scene)",
		L"· Boutons CCustom ont aussi de petits accents Soft 3D (hors scène Soft 3D)",
		L"· Pulsanti CCustom hanno anche piccoli accenti Soft 3D (oltre alla scena Soft 3D)",
		L"· Botones CCustom también llevan Soft 3D pequeño (aparte de la escena Soft 3D)",
		L"· 버튼 등 CCustom에도 Soft3D 장식(전체 Soft3D 장면과 별개)",
		L"· 按钮等 CCustom 也有 Soft3D 装饰（与整窗 Soft3D 场景不同）",
		L"· أزرار CCustom لها أيضاً زخارف Soft3D (غير المشهد الكامل)",
		L"· Кнопки CCustom — мелкие Soft 3D-акценты (не полная сцена Soft 3D)",
		L"· CCustom-Buttons haben auch Soft-3D-Akzente (neben Soft-3D-Szene)",
		L"· Botões CCustom também têm Soft 3D (além da cena Soft 3D)",
		L"· CCustom-knoppen hebben ook Soft 3D (naast Soft 3D-scene)",
		L"· Przyciski CCustom mają też Soft 3D (oprócz sceny Soft 3D)",
		L"· CCustom düğmelerde Soft 3B süs (tam Soft 3B sahneden ayrı)")); y += lh + 2;

	body(L, y, LL14(
		L"・鍵盤レンジ …… 88鍵(A0〜) / 108鍵。ノート名の表示切替あり",
		L"· Key range …… 88 keys (A0–) / 108 keys. Toggle note names",
		L"· Clavier …… 88 touches (A0–) / 108. Noms de notes on/off",
		L"· Tastiera …… 88 tasti (A0–) / 108. Nomi note on/off",
		L"· Teclado …… 88 teclas (A0–) / 108. Nombres on/off",
		L"· 건반 …… 88건(A0~) / 108건. 음이름 표시 토글",
		L"· 键盘 …… 88键(A0–) / 108键。可开关音名",
		L"· المفاتيح …… 88 (A0–) / 108. إظهار أسماء النغمات",
		L"· Клавиши …… 88 (A0–) / 108. Имена нот вкл/выкл",
		L"· Tastatur …… 88 (A0–) / 108. Notennamen ein/aus",
		L"· Teclado …… 88 (A0–) / 108. Nomes das notas on/off",
		L"· Toetsen …… 88 (A0–) / 108. Notennamen aan/uit",
		L"· Klawiatura …… 88 (A0–) / 108. Nazwy nut wł./wył.",
		L"· Klavye …… 88 (A0–) / 108. Nota adları aç/kapa")); y += lh;
	body(L, y, LL14(
		L"・流れる速度 …… 右クリックで x0.25〜x2.0。フリーズでスクロールだけ止められます",
		L"· Scroll speed …… right-click x0.25–x2.0. Freeze stops scrolling only",
		L"· Vitesse …… clic droit x0.25–x2.0. Gel = arrêt du défilement",
		L"· Velocità …… destro x0.25–x2.0. Congela ferma lo scorrimento",
		L"· Velocidad …… clic der. x0.25–x2.0. Congelar detiene el scroll",
		L"· 스크롤 속도 …… 우클릭 x0.25~x2.0. 정지는 스크롤만 멈춤",
		L"· 滚动速度 …… 右键 x0.25–x2.0。冻结只停滚动",
		L"· السرعة …… يمين x0.25–x2.0. التجميد يوقف التمرير فقط",
		L"· Скорость …… ПКМ x0.25–x2.0. Заморозка останавливает прокрутку",
		L"· Tempo …… Rechtsklick x0.25–x2.0. Freeze stoppt nur Scroll",
		L"· Velocidade …… direito x0.25–x2.0. Congelar para só o scroll",
		L"· Snelheid …… rechtsklik x0.25–x2.0. Bevriezen stopt alleen scroll",
		L"· Prędkość …… PPM x0.25–x2.0. Zamroź zatrzymuje przewijanie",
		L"· Hız …… sağ tık x0.25–x2.0. Dondur yalnızca kaydırmayı durdurur")); y += lh + 2;

	// mini roll diagram
	{
		const int gx = L, gy = y, gw = min(300, rc.Width() - L * 2), gh = lh * 3 + 6;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
		for (int row = 0; row < 5; ++row) {
			const int yy = gy + 4 + row * (gh / 6);
			dc.FillSolidRect(gx + 20 + row * 12, yy, 36 + (row % 3) * 10, 6, RGB(90, 140, 200));
		}
		dc.FillSolidRect(gx + 4, gy + gh - lh - 2, gw - 8, lh, RGB(235, 235, 240));
		dc.SetTextColor(RGB(55, 55, 70));
		dc.TextOut(gx + 10, gy + gh - lh, L"A0  C  E  G  B …");
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		y = gy + gh + 4;
	}

	title(L, y, LL14(L"検出とチューニング", L"Detection & tuning", L"Détection et réglage", L"Rilevamento e regolazione",
		L"Detección y ajuste", L"검출과 튜닝", L"检测与调参", L"الكشف والضبط",
		L"Обнаружение и настройка", L"Erkennung & Feintuning", L"Detecção e ajuste", L"Detectie & afstemming",
		L"Wykrywanie i strojenie", L"Algılama ve ayar"));
	y += titleLh;
	body(L, y, LL14(
		L"・検出オプション …… 再アタック / 打撃音・倍音ゴースト抑制 / 音色プロファイル",
		L"· Detection …… re-attack / impulsive & harmonic ghost suppress / timbre profile",
		L"· Détection …… réattaque / fantômes / profil de timbre",
		L"· Rilevamento …… riattacco / fantasmi / profilo timbrico",
		L"· Detección …… reataque / fantasmas / perfil tímbrico",
		L"· 검출 …… 리어택 / 타격·배음 고스트 억제 / 음색 프로파일",
		L"· 检测 …… 再起音 / 打击与泛音幽灵抑制 / 音色轮廓",
		L"· الكشف …… إعادة هجوم / أشباح / ملف الطابع",
		L"· Обнаружение …… реатака / призраки / профиль тембра",
		L"· Erkennung …… Re-Attack / Geister / Klangprofil",
		L"· Detecção …… reataque / fantasmas / perfil tímbrico",
		L"· Detectie …… her-aanval / spoken / timbreprofiel",
		L"· Wykrywanie …… reatak / duchy / profil barwy",
		L"· Algılama …… yeniden saldırı / hayalet / timbre profili")); y += lh;
	body(L, y, LL14(
		L"・検出パラメータ調整… …… 感度を % で細かく調整する別ダイアログを開きます",
		L"· Detection parameter tuning… …… opens a dialog to fine-tune sensitivity (%)",
		L"· Paramètres de détection… …… dialogue de sensibilité (%)",
		L"· Parametri rilevamento… …… dialogo sensibilità (%)",
		L"· Parámetros de detección… …… diálogo de sensibilidad (%)",
		L"· 검출 파라미터 조정… …… 감도를 %로 미세 조정하는 대화상자",
		L"· 检测参数调整… …… 打开以 % 微调灵敏度的对话框",
		L"· معلمات الكشف… …… حوار لضبط الحساسية (%)",
		L"· Параметры… …… диалог тонкой настройки чувствительности (%)",
		L"· Erkennungsparameter… …… Dialog für Empfindlichkeit (%)",
		L"· Parâmetros… …… diálogo de sensibilidade (%)",
		L"· Detectieparameters… …… dialoog voor gevoeligheid (%)",
		L"· Parametry… …… okno czułości (%)",
		L"· Algılama parametreleri… …… hassasiyet (%) iletişim kutusu")); y += lh + 4;

	title(L, y, LL14(L"右クリックメニュー", L"Context menu", L"Menu contextuel", L"Menu contestuale",
		L"Menú contextual", L"우클릭 메뉴", L"右键菜单", L"قائمة السياق",
		L"Контекстное меню", L"Kontextmenü", L"Menu de contexto", L"Contextmenu",
		L"Menu kontekstowe", L"Bağlam menüsü"));
	y += titleLh;
	body(L, y, LL14(
		L"・記号凡例 / 表現記号 / レベルメーター / 常に手前 / 表示クリア",
		L"· Symbol legend / expression marks / level meter / always on top / clear",
		L"· Légende / expression / niveau / premier plan / effacer",
		L"· Legenda / espressione / livello / primo piano / cancella",
		L"· Leyenda / expresión / nivel / siempre visible / borrar",
		L"· 기호 범례 / 표현 기호 / 레벨 미터 / 항상 위 / 지우기",
		L"· 符号图例 / 奏法记号 / 电平表 / 置顶 / 清除",
		L"· دليل الرموز / التعبير / المستوى / دائماً أعلى / مسح",
		L"· Легенда / экспрессия / уровень / поверх / очистить",
		L"· Legende / Ausdruck / Pegel / immer oben / leeren",
		L"· Legenda / expressão / nível / sempre no topo / limpar",
		L"· Legenda / expressie / niveau / altijd boven / wissen",
		L"· Legenda / ekspresja / poziom / zawsze na wierzchu / wyczyść",
		L"· Sembol / ifade / seviye / her zaman üstte / temizle")); y += lh;
	body(L, y, LL14(
		L"・コード進行パネル …… 検出コードを時系列表示（推定・実験的）",
		L"· Chord panel …… timeline of detected chords (estimate / experimental)",
		L"· Panneau accords …… accords détectés (estimé / expérimental)",
		L"· Pannello accordi …… accordi rilevati (stima / sperimentale)",
		L"· Panel de acordes …… acordes detectados (estimación / experimental)",
		L"· 코드 진행 패널 …… 검출 코드 시계열(추정·실험적)",
		L"· 和弦进行面板 …… 检测和弦时序（估计/实验）",
		L"· لوحة التآلفات …… تآلفات مكتشفة (تقدير / تجريبي)",
		L"· Панель аккордов …… обнаруженные аккорды (оценка / эксперимент)",
		L"· Akkordpanel …… erkannte Akkorde (Schätzung / experimentell)",
		L"· Painel de acordes …… acordes detectados (estimativa / experimental)",
		L"· Akkoordenpaneel …… gedetecteerde akkoorden (schatting / experimenteel)",
		L"· Panel akordów …… wykryte akordy (szacunek / eksperymentalne)",
		L"· Akor paneli …… algılanan akorlar (tahmin / deneysel)")); y += lh;
	body(L, y, LL14(
		L"・PC音を譜面化 …… WASAPIループバックでPCの再生音をロールへ（無音時は止まります）",
		L"· Score from PC audio …… WASAPI loopback into the roll (stops when silent)",
		L"· Partition PC …… boucle WASAPI vers le roll (s'arrête si silence)",
		L"· Partitura PC …… loopback WASAPI nel roll (si ferma se silenzio)",
		L"· Partitura PC …… loopback WASAPI al roll (se detiene si silencio)",
		L"· PC 소리 악보화 …… WASAPI 루프백을 롤로(무음이면 멈춤)",
		L"· 从PC声音成谱 …… WASAPI 环回送入卷帘（静音则停）",
		L"· تدوين صوت الجهاز …… حلقة WASAPI إلى اللفة (يتوقف عند الصمت)",
		L"· Ноты с ПК …… WASAPI loopback в ролл (стоп при тишине)",
		L"· Partitur aus PC …… WASAPI-Loopback in die Rolle (stoppt bei Stille)",
		L"· Partitura do PC …… loopback WASAPI no roll (para se silêncio)",
		L"· Partituur van pc …… WASAPI-loopback naar roll (stopt bij stilte)",
		L"· Partytura z PC …… pętla WASAPI do rolki (stop przy ciszy)",
		L"· PC sesinden parti …… WASAPI loopback ruloya (sessizlikte durur)")); y += lh;
	body(L, y, LL14(
		L"・MIDI録り / MusicXML録り …… ONでバッファ蓄積、OFFで保存。再生中またはPC音連動",
		L"· MIDI / MusicXML capture …… ON buffers, OFF saves. During play or with PC-audio",
		L"· MIDI / MusicXML …… ON=tampon, OFF=sauver. Lecture ou audio PC",
		L"· MIDI / MusicXML …… ON=buffer, OFF=salva. In play o con audio PC",
		L"· MIDI / MusicXML …… ON=búfer, OFF=guardar. En play o con audio PC",
		L"· MIDI / MusicXML 녹음 …… ON=버퍼, OFF=저장. 재생 중 또는 PC 소리 연동",
		L"· MIDI / MusicXML 录制 …… ON 缓冲，OFF 保存。播放中或 PC 声音联动",
		L"· تسجيل MIDI / MusicXML …… ON=تخزين، OFF=حفظ. أثناء التشغيل أو صوت الجهاز",
		L"· MIDI / MusicXML …… ON=буфер, OFF=сохранить. Во время play или звук ПК",
		L"· MIDI / MusicXML …… ON=Puffer, OFF=speichern. Während Play oder PC-Audio",
		L"· MIDI / MusicXML …… ON=buffer, OFF=salvar. Em play ou com áudio PC",
		L"· MIDI / MusicXML …… ON=buffer, OFF=opslaan. Tijdens play of pc-audio",
		L"· MIDI / MusicXML …… ON=bufor, OFF=zapisz. Podczas play lub dźwięk PC",
		L"· MIDI / MusicXML …… ON=tampon, OFF=kaydet. Çalma veya PC sesi ile")); y += lh;
	body(L, y, LL14(
		L"・録り中はPC音譜面化を切れません。チェックを外したとき .mid / .musicxml を書き出します",
		L"· While capturing, PC-audio score stays on. Uncheck writes .mid / .musicxml",
		L"· Pendant l'enregistrement, audio PC reste ON. Décocher écrit .mid / .musicxml",
		L"· Durante la registrazione audio PC resta ON. Togliere check scrive .mid / .musicxml",
		L"· Durante la captura, audio PC sigue ON. Desmarcar escribe .mid / .musicxml",
		L"· 녹음 중 PC 소리 악보화 유지. 체크 해제 시 .mid / .musicxml 저장",
		L"· 录制中不可关 PC 成谱。取消勾选时写出 .mid / .musicxml",
		L"· أثناء التسجيل يبقى صوت الجهاز. إلغاء التحديد يكتب .mid / .musicxml",
		L"· Во время записи звук ПК остаётся. Снятие галочки пишет .mid / .musicxml",
		L"· Während Aufnahme bleibt PC-Audio an. Abwahl schreibt .mid / .musicxml",
		L"· Durante captura, áudio PC fica ON. Desmarcar grava .mid / .musicxml",
		L"· Tijdens opname blijft pc-audio aan. Uitvinken schrijft .mid / .musicxml",
		L"· Podczas zapisu dźwięk PC zostaje. Odznaczenie zapisuje .mid / .musicxml",
		L"· Kayıtta PC sesi açık kalır. İşareti kaldırmak .mid / .musicxml yazar")); y += lh;
	body(L, y, LL14(
		L"・ショートカット …… V=2D/3D切替、N=ノート名。ウィンドウは端でリサイズ可",
		L"· Shortcuts …… V=2D/3D, N=note names. Resize via window edges",
		L"· Raccourcis …… V=2D/3D, N=noms. Redimensionner aux bords",
		L"· Scorciatoie …… V=2D/3D, N=nomi. Ridimensiona ai bordi",
		L"· Atajos …… V=2D/3D, N=nombres. Redimensione en bordes",
		L"· 단축키 …… V=2D/3D, N=음이름. 창 가장자리로 크기 조절",
		L"· 快捷键 …… V=2D/3D，N=音名。可用窗口边缘缩放",
		L"· اختصارات …… V=2D/3D، N=أسماء. غيّر الحجم من الحواف",
		L"· Ярлыки …… V=2D/3D, N=имена. Размер — за края окна",
		L"· Kürzel …… V=2D/3D, N=Namen. Größe an Fensterrändern",
		L"· Atalhos …… V=2D/3D, N=nomes. Redimensione nas bordas",
		L"· Sneltoetsen …… V=2D/3D, N=namen. Formaat aan randen",
		L"· Skróty …… V=2D/3D, N=nazwy. Rozmiar na krawędziach",
		L"· Kısayollar …… V=2D/3B, N=adlar. Kenarlardan boyutlandır")); y += lh + 2;
	muted(L, y, LL14(
		L"キャプションの「?」または右クリック「操作ガイド」でもこの画面を開けます。録りは実験的です。",
		L"Open from caption \"?\" or right-click → Operation guide. Capture is experimental.",
		L"Ouvrir via « ? » ou clic droit → Guide. Enregistrement expérimental.",
		L"Apri da « ? » o destro → Guida. Registrazione sperimentale.",
		L"Ábralo con « ? » o clic der. → Guía. Captura experimental.",
		L"캡션「?」또는 우클릭「조작 가이드」로 열기. 녹음은 실험적.",
		L"也可通过标题栏「?」或右键「操作指南」打开。录制为实验性。",
		L"افتح من «؟» أو يمين ← دليل. التسجيل تجريبي.",
		L"Откройте через «?» или ПКМ → Руководство. Запись экспериментальна.",
		L"Öffnen über „?“ oder Rechtsklick → Guide. Aufnahme experimentell.",
		L"Abra pelo «?» ou direito → Guia. Captura experimental.",
		L"Open via «?» of rechtsklik → Handleiding. Opname is experimenteel.",
		L"Otwórz przez «?» lub PPM → Przewodnik. Zapis jest eksperymentalny.",
		L"Başlık «?» veya sağ tık → Kılavuz. Kayıt deneyseldir."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

IMPLEMENT_DYNAMIC(CPianoRoll, CCustomBlurDialogExBase)

// pick fix 5 ベースを低音/中高音(一体)の2帯に整理。// BAND_MID_END はレーン色用のみ。追加パッチ関数は置かない。
namespace Cfg
{
    // piano roll3: 基音ピック + NormalizeDisplayPeak + 包絡ホールド
    static constexpr float IIR_ALPHA = 0.40f;
    static constexpr float IIR_ALPHA_BASS = 0.28f;
    // 絶対無音ゲート。解析AGCを -3dBFS 目標にしたあとでも、
    // 弱い楽節(例: OST導入部)がここで全滅しないよう 0.002→0.0007 に下げた。
    static constexpr float SILENCE_ABS = 0.0007f;
    static constexpr float BAND_SILENCE_BASS = 0.00055f;
    static constexpr float BAND_SILENCE_MID = 0.00045f;
    static constexpr float BAND_SILENCE_TRE = 0.00045f;
    static constexpr int   ATTACK_FRAMES = 1;
    static constexpr int   ATTACK_FRAMES_EDGE = 2;
    // T130 の 16分(~115ms)/32分(~58ms)を分離できるようリリースを短く。
    // 旧5だと中域 TemporalFrames で最大20F(≈460ms)まで伸びて連打が1本に見える。
    static constexpr int   RELEASE_FRAMES = 3;
    static constexpr int   VIS_GAP_FRAMES = 2;
    static constexpr int   VIS_GAP_FRAMES_BASS = 3;
    static constexpr int   VIS_GAP_FRAMES_MID = 1;
    static constexpr int   VIS_GAP_FRAMES_TRE = 1;
    static constexpr int   VIS_GAP_SUSTAIN_BONUS = 1;
    static constexpr int   ATTACK_MISS_GRACE = 2;
    static constexpr float RETRIGGER_RATIO = 0.28f;
    static constexpr int   BAND_BASS_END = PianoRoll108::BASS_END;
    static constexpr int   BAND_MID_END = PianoRoll108::MID_END;
    static constexpr int   KEY_O2G = 43;   // G2
    static constexpr int   KEY_O5C = 72;   // C5
    static constexpr int   KEY_O7C = 96;   // C7
    // ノート有無: 調波サリエンスの局所ピークが領域ノイズ床×SNR を超えること。
    // 帯域max比は使わない（静音で高音が消え、派手な曲で砂になるため）。
    static constexpr float BASS_PICK_THRESH = 0.20f;
    static constexpr float UPPER_PICK_THRESH = 0.06f;
    static constexpr float PEAK_SNR = 3.0f;
    static constexpr float PEAK_REGION_REL = 0.055f;  // legacy pick helpers (unused path)
    // [検証のため元値へ復帰] 隣接ホッピング対策を単独の変数として切り分けて検証する。
    static constexpr float HOLD_ENV_BASS = 0.28f;
    static constexpr float HOLD_ENV_MID = 0.22f;  // 短音分離: 旧0.14は減衰残響で隙間を埋める
    static constexpr float HOLD_ENV_TRE = 0.24f;
    static constexpr float DISPLAY_PEAK_CAP = 5.0f;
    static constexpr int   ANALYZE_INTERVAL = 1024; // FeedPCM未使用。実ホップは SyncPianoRoll + ANALYZE_MIN_MS
    static constexpr int   ONSET_KEY_START = 62;  // D4（旧 index 41 + MIDI base 21）
    static constexpr float ONSET_DELTA_THRESH = 0.012f;
    static constexpr float ONSET_MIN_STRENGTH = 0.018f;
    static constexpr float BASS_ONSET_DELTA_THRESH = 0.034f;
    static constexpr float BASS_ONSET_MIN_STRENGTH = 0.055f;
    static constexpr float UPPER_ONSET_DELTA_THRESH = 0.040f;
    static constexpr float UPPER_ONSET_MIN_STRENGTH = 0.065f;

    // 解析専用の双方向AGC。再生用EQマスターとは独立に、窓ピークを目標へ揃える。
    // 旧: 上げのみ(+→-11dB、既に大きい入力は触らない) → 静かな曲は床に沈み、
    //     マスター上げで初めて載る／その分ゴーストが増える非対称が起きていた。
    // 新: 上げ下げとも -3dBFS 付近へ正規化し、音量スライダーに検出を依存させない。
    static constexpr float BUFWAV3_TARGET_PEAK_DB = -3.0f;
    static constexpr float BUFWAV3_GAIN_DB_MAX = 36.0f;   // 静かな曲への最大ブースト
    static constexpr float BUFWAV3_GAIN_DB_MIN = -24.0f;  // 大きい入力／マスター上げのカット
    static constexpr float BUFWAV3_PEAK_FLOOR_DB = -60.0f;
    // 絶対ノイズ床の基準(AGC後)。旧0.0015は -3dB 動作点では厳しすぎる。
    static constexpr float ABS_NOISE_FLOOR_BASE = 0.00055f;
    // スペクトル相対床: 下位パーセンタイル × 倍率 (マスター上げ時の砂粒ゴースト対策)
    static constexpr float SPECTRAL_NOISE_PERCENTILE = 0.35f;
    static constexpr float SPECTRAL_NOISE_FLOOR_MUL = 2.2f;

    static float PeakDbFs(const double* samples, int n)
    {
        if (!samples || n <= 0) return BUFWAV3_PEAK_FLOOR_DB;
        double peak = 0.0;
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i) {
            const double a = fabs(samples[i]);
            if (a > peak) peak = a;
            sumSq += a * a;
        }
        const double rms = sqrt(sumSq / (double)n);
        double level = peak;
        if (rms * 4.0 > level) level = rms * 4.0;
        if (level < 1e-9) return BUFWAV3_PEAK_FLOOR_DB;
        return (float)(20.0 * log10(level));
    }

    static float PeakDbFsWindows(const double* winLow, int nLow,
        const double* winBass, int nBass)
    {
        float db = PeakDbFs(winLow, nLow);
        if (winBass && nBass > 0) {
            const float dbB = PeakDbFs(winBass, nBass);
            if (dbB > db) db = dbB;
        }
        return db;
    }

    // フル窓ピークのみ。静かな半分を使うと音頭前の無音でゲインが跳ね、ノイズ床がノートになる。
    static float LevelDbForDynamics(const double* winLow, int nLow,
        const double* winBass, int nBass)
    {
        return PeakDbFsWindows(winLow, nLow, winBass, nBass);
    }

    // 解析窓を目標ピークへ双方向正規化するゲイン(dB)。再生音量には影響しない。
    static float MakeupGainDb(float peakDbFs)
    {
        if (peakDbFs <= BUFWAV3_PEAK_FLOOR_DB + 0.5f)
            return 0.0f;
        float g = BUFWAV3_TARGET_PEAK_DB - peakDbFs;
        if (g > BUFWAV3_GAIN_DB_MAX) g = BUFWAV3_GAIN_DB_MAX;
        if (g < BUFWAV3_GAIN_DB_MIN) g = BUFWAV3_GAIN_DB_MIN;
        return g;
    }

    // blend スペクトルの下位パーセンタイルをノイズ床推定に使う。
    // ノートが多いフレームでは相対床が上がり、砂粒ゴーストを押し下げる。
    static float SpectralNoiseEstimate(const float* blend, int count)
    {
        if (!blend || count <= 0) return 0.0f;
        float tmp[128];
        int n = 0;
        const int cap = (count < 128) ? count : 128;
        for (int i = 0; i < count && n < cap; ++i) {
            if (blend[i] > 1e-8f)
                tmp[n++] = blend[i];
        }
        if (n < 8) return 0.0f;
        int k = (int)((float)n * SPECTRAL_NOISE_PERCENTILE);
        if (k < 0) k = 0;
        if (k >= n) k = n - 1;
        // 部分選択（std::nth_element 禁止）
        for (int i = 0; i <= k; ++i) {
            int best = i;
            for (int j = i + 1; j < n; ++j)
                if (tmp[j] < tmp[best]) best = j;
            if (best != i) { float t = tmp[i]; tmp[i] = tmp[best]; tmp[best] = t; }
        }
        return tmp[k];
    }

    static float PickThreshScaleFromLevelDb(float levelDb)
    {
        if (levelDb < -32.0f) return 0.58f;
        if (levelDb < -26.0f) return 0.68f;
        if (levelDb < -22.0f) return 0.79f;
        if (levelDb < -18.0f) return 0.89f;
        if (levelDb < -14.0f) return 0.97f;
        if (levelDb < -11.0f) return 1.00f;
        return 1.02f;
    }

    static void ApplyGainDbInPlace(double* samples, int n, float gainDb)
    {
        if (!samples || n <= 0) return;
        if (gainDb > -0.001f && gainDb < 0.001f) return;
        const double g = pow(10.0, (double)gainDb / 20.0);
        for (int i = 0; i < n; ++i) {
            double v = samples[i] * g;
            if (v > 1.0) v = 1.0;
            else if (v < -1.0) v = -1.0;
            samples[i] = v;
        }
    }

    // C4 = key 39。弦/鍵盤の減衰は中音域で長く、高音ほど短い。
    static float HoldEnvRatio(int keyIndex)
    {
        if (keyIndex < BAND_BASS_END) return HOLD_ENV_BASS;
        if (keyIndex < BAND_MID_END) return HOLD_ENV_MID;
        return HOLD_ENV_TRE;
    }

    static int TemporalFrames(int keyIndex, int baseFrames)
    {
        int lo = 0, hi = PianoKey::COUNT;
        if (keyIndex < BAND_BASS_END) { lo = 0; hi = BAND_BASS_END; }
        else if (keyIndex < BAND_MID_END) { lo = BAND_BASS_END; hi = BAND_MID_END; }
        else { lo = BAND_MID_END; hi = PianoKey::COUNT; }
        const int span = hi - lo;
        if (span <= 1) return baseFrames;
        const float t = (float)(keyIndex - lo) / (float)(span - 1);
        float scale;
        if (keyIndex >= BAND_MID_END)
            // 高音ほど短いが、極端に潰さない（検出後すぐ消えるのを防ぐ）
            scale = 1.35f - t * 0.70f;
        else if (keyIndex >= BAND_BASS_END)
            scale = 1.50f - t * 0.55f;
        else
            scale = 1.90f - t * 1.45f;
        // O4C 付近は物理減衰が長いので release/gap を最大 +2 フレーム
        const float dc = (float)(keyIndex - 60) / 12.0f;
        const float bell = expf(-dc * dc);
        int f = (int)(baseFrames * scale + bell * 2.0f + 0.5f);
        if (keyIndex >= BAND_MID_END) {
            if (f < 2) f = 2;
            if (f > 5) f = 5;   // 高音リリース上限: 旧12F→5F (≈115ms@1024hop ≈16分)
        }
        else {
            if (keyIndex < BAND_BASS_END) {
                if (f < 1) f = 1;
                if (f > 8) f = 8;
            }
            else {
                if (f < 2) f = 2;
                if (f > 6) f = 6; // 中音上限: 旧20F→6F (≈140ms、16分連打を分離)
            }
        }
        return f;
    }

    static int AttackFramesForKey(int keyIndex)
    {
        if (keyIndex >= PianoRoll108::BASS_END && keyIndex < PianoRoll108::EDGE_HI)
            return 1;
        if (keyIndex < 12 || keyIndex >= PianoRoll108::EDGE_HI)
            return TemporalFrames(keyIndex, ATTACK_FRAMES_EDGE);
        return 1;
    }

    static int VisGapFrames(int keyIndex)
    {
        if (keyIndex >= PianoRoll108::BASS_END && keyIndex < PianoRoll108::MID_END)
            return VIS_GAP_FRAMES_MID;
        const int base = (keyIndex < BAND_BASS_END) ? VIS_GAP_FRAMES_BASS
            : (keyIndex >= BAND_MID_END) ? VIS_GAP_FRAMES_TRE : VIS_GAP_FRAMES;
        return TemporalFrames(keyIndex, base);
    }

    static void NormalizeBandPeak(float* values, int lo, int hi, float cap)
    {
        if (!values || lo >= hi || cap <= 0.0f) return;
        float maxV = 0.0f;
        for (int i = lo; i < hi; ++i)
            if (values[i] > maxV) maxV = values[i];
        if (maxV <= cap) return;
        const float scale = cap / maxV;
        for (int i = lo; i < hi; ++i)
            values[i] *= scale;
    }


}

int CPianoRoll::ScaleWinSamples(int refSamples, int sampleRate, int capSamples)
{
    if (refSamples <= 0) return 0;
    if (sampleRate < 8000) sampleRate = REF_SAMPLE_RATE;
    int64_t n = ((int64_t)refSamples * (int64_t)sampleRate + REF_SAMPLE_RATE / 2) / REF_SAMPLE_RATE;
    if (n < 64) n = 64;
    static const int kAbsMax = 131072;
    if (n > kAbsMax) n = kAbsMax;
    if (capSamples > 0 && n > capSamples) n = capSamples;
    return (int)n;
}

int CPianoRoll::CaptureFrameCount(int sampleRate, int capSamples)
{
    return ScaleWinSamples(WIN_BASS_REF, sampleRate, capSamples);
}

int CPianoRoll::MinAnalyzeFrameCount(int sampleRate, int capSamples)
{
    return ScaleWinSamples(WIN_BASS_REF, sampleRate, capSamples);
}

int CPianoRoll::CapAnalyzeSampleRate(int sampleRate)
{
    if (sampleRate < 8000) return REF_SAMPLE_RATE;
    // 伸縮中は解析を 44.1k 相当に落とし Goertzel 窓を抑える
    if (tempo != 200 || pitch != 200) {
        if (sampleRate > REF_SAMPLE_RATE) return REF_SAMPLE_RATE;
        return sampleRate;
    }
    if (sampleRate > ANALYZE_RATE_MAX) return ANALYZE_RATE_MAX;
    return sampleRate;
}

int CPianoRoll::SourceFramesForAnalyze(int analyzeFrames, int sourceRate, int analyzeRate)
{
    if (analyzeFrames <= 0) return 0;
    if (sourceRate < 8000) sourceRate = REF_SAMPLE_RATE;
    if (analyzeRate < 8000) analyzeRate = REF_SAMPLE_RATE;
    if (sourceRate <= analyzeRate) return analyzeFrames;
    int64_t n = ((int64_t)analyzeFrames * (int64_t)sourceRate + analyzeRate / 2) / analyzeRate;
    if (n < 64) n = 64;
    if (n > WIN_SAMPLES_MAX) n = WIN_SAMPLES_MAX;
    return (int)n;
}

DWORD CPianoRoll::EffectiveAnalyzeMinMs()
{
    // 解析を描画周期以上に速くしても、pending 上限で描画は間引かれ CPU だけ浪費する。
    DWORD ms = ANALYZE_MIN_MS;
    int drawMs = savedata.ms2;
    if (drawMs < 16) drawMs = 16;
    if (drawMs > 960) drawMs = 960;
    if ((DWORD)drawMs > ms)
        ms = (DWORD)drawMs;
    // スライダ中央 200 = 100%。伸縮中はさらに間引く。
    if (tempo != 200 || pitch != 200) {
        DWORD t = ANALYZE_MIN_MS_TEMPO;
        if ((DWORD)drawMs * 2u > t)
            t = (DWORD)drawMs * 2u;
        if (t > ms) ms = t;
    }
    return ms;
}

CPianoRoll::CPianoRoll(CWnd* pParent)
    : CCustomBlurDialogExBase(IDD_PIANOROLL, pParent)
{
    InitializeCriticalSection(&m_cs);
    InitializeCriticalSection(&m_jobCs);
    memset(m_jobMono, 0, sizeof(m_jobMono));
    m_analysisTablesReady = false;
    EnsureAnalysisTables(REF_SAMPLE_RATE);

    memset(m_activeKeys, 0, sizeof(m_activeKeys));
    memset(m_noteStrength, 0, sizeof(m_noteStrength));
    memset(m_rawStrengths, 0, sizeof(m_rawStrengths));
    memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
    memset(m_displayStrengths, 0, sizeof(m_displayStrengths));
    memset(m_displaySmoothed, 0, sizeof(m_displaySmoothed));
    memset(m_consecActive, 0, sizeof(m_consecActive));
    memset(m_consecSilent, 0, sizeof(m_consecSilent));
    memset(m_segmentId, 0, sizeof(m_segmentId));
    memset(m_envPeak, 0, sizeof(m_envPeak));
    memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
    memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
    memset(m_transientHold, 0, sizeof(m_transientHold));
    memset(m_bandMask, 0, sizeof(m_bandMask));
    memset(m_laneStrength, 0, sizeof(m_laneStrength));
    memset(m_prevBandMask, 0, sizeof(m_prevBandMask));
    memset(m_prevRawStrengths, 0, sizeof(m_prevRawStrengths));
    memset(m_onsetStrengths, 0, sizeof(m_onsetStrengths));
    memset(m_prevOnsetStrengths, 0, sizeof(m_prevOnsetStrengths));
    memset(m_prevActiveKeys, 0, sizeof(m_prevActiveKeys));
    memset(m_prevNoteStrength, 0, sizeof(m_prevNoteStrength));
    memset(m_noteAgeFrames, 0, sizeof(m_noteAgeFrames));
    memset(m_scoopLatch, 0, sizeof(m_scoopLatch));
    memset(m_exprFlags, 0, sizeof(m_exprFlags));
    memset(m_vibHist, 0, sizeof(m_vibHist));
    memset(m_vibHistCount, 0, sizeof(m_vibHistCount));
    memset(m_keySnapActive, 0, sizeof(m_keySnapActive));
    memset(m_keySnapBand, 0, sizeof(m_keySnapBand));
    // 音色エンベロープモデルは各要素が既定コンストラクタで初期化済み(ResetOff相当の値)。
    // 念のため明示的にもオフ状態へ揃えておく。
    for (int i = 0; i < KEY_COUNT; ++i)
        m_envModel[i].ResetOff();
    memset(m_reattackMark, 0, sizeof(m_reattackMark));
    memset(m_onsetBoostThisFrame, 0, sizeof(m_onsetBoostThisFrame));
    memset(m_onsetBoostStreak, 0, sizeof(m_onsetBoostStreak));
    memset(m_harmonicGhostStreak, 0, sizeof(m_harmonicGhostStreak));
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_historyCount = 0;
    m_historyHead = 0;
    m_rollSpeedCredit = 0;
    m_lastRollPushTick = 0;
    for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
        auto& f = m_historyRing[hi];
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
        memset(f.expr, 0, sizeof(f.expr));
        memset(f.dynLevel, 0, sizeof(f.dynLevel));
        memset(f.reattack, 0, sizeof(f.reattack));
    }
}

CPianoRoll::~CPianoRoll()
{
    StopAnalysisWorker();
    m_feedEnabled = false;
    ReleasePaintBuffers();
    DeleteCriticalSection(&m_jobCs);
    DeleteCriticalSection(&m_cs);
}

// stop/曲切替の先頭。DoEvent 再入で旧形式バッファを解析しない。
void CPianoRoll::PauseAnalysis()
{
    m_feedEnabled = false;
    InterlockedIncrement(&m_analysisEpoch);
    InterlockedExchange(&m_jobPending, 0);
    // Peek で SYNC/ANALYSIS_DONE を捨てる前に落とす。残ると以降の Post が永久に no-op になる。
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_analysisPresentDirty, 0);
    EnterCriticalSection(&m_jobCs);
    m_jobFrameCount = 0;
    LeaveCriticalSection(&m_jobCs);
    for (int i = 0; i < 1000; ++i) {
        if (InterlockedCompareExchange(&m_analysisBusy, 0, 0) == 0)
            break;
        Sleep(1);
    }
    InterlockedExchange(&m_jobPending, 0);
}

// flac⇔wav: 旧形式のワーカー/係数が残ると壊れる。
// 解析スレッドはここでは起動しない。ResumePlaybackFeed（DS 再生開始後）でのみ起動する。
void CPianoRoll::ResetPlaybackState()
{
    // Pause で busy=0 を待つので、表示状態のクリアはワーカー停止成否に依存しない。
    PauseAnalysis();

    if (::IsWindow(m_hWnd)) {
        MSG msg;
        while (PeekMessage(&msg, m_hWnd, WM_PIANOROLL_ANALYSIS_DONE, WM_PIANOROLL_ANALYSIS_DONE, PM_REMOVE)) {}
        while (PeekMessage(&msg, m_hWnd, WM_PIANOROLL_SYNC, WM_PIANOROLL_SYNC, PM_REMOVE)) {}
    }
    // Peek 後も必ずクリア（捨てたメッセージのフラグが残ると2曲目以降描画停止）
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_analysisPresentDirty, 0);

    m_chMeterCount = 0;
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        m_chMeterDb[i] = -60.0f;
        m_chMeterFill[i] = 0.0f;
        m_chMeterAutoPeak[i] = 0.02f;
    }
    m_meterDirty = true;

    if (TryEnterCriticalSection(&m_jobCs)) {
        m_jobFrameCount = 0;
        m_jobSampleRate = 44100;
        LeaveCriticalSection(&m_jobCs);
    }

    // 表示・ノート状態は必ず消す（曲切替で前曲のロールが残らないように）
    auto clearDisplayState = [this]() {
        memset(m_activeKeys, 0, sizeof(m_activeKeys));
        memset(m_noteStrength, 0, sizeof(m_noteStrength));
        memset(m_rawStrengths, 0, sizeof(m_rawStrengths));
        memset(m_prevRawStrengths, 0, sizeof(m_prevRawStrengths));
        memset(m_onsetStrengths, 0, sizeof(m_onsetStrengths));
        memset(m_prevOnsetStrengths, 0, sizeof(m_prevOnsetStrengths));
        memset(m_prevActiveKeys, 0, sizeof(m_prevActiveKeys));
        memset(m_prevNoteStrength, 0, sizeof(m_prevNoteStrength));
        memset(m_noteAgeFrames, 0, sizeof(m_noteAgeFrames));
        memset(m_scoopLatch, 0, sizeof(m_scoopLatch));
        memset(m_exprFlags, 0, sizeof(m_exprFlags));
        memset(m_vibHist, 0, sizeof(m_vibHist));
        memset(m_vibHistCount, 0, sizeof(m_vibHistCount));
        memset(m_smoothedStrengths, 0, sizeof(m_smoothedStrengths));
        memset(m_displayStrengths, 0, sizeof(m_displayStrengths));
        memset(m_displaySmoothed, 0, sizeof(m_displaySmoothed));
        memset(m_consecActive, 0, sizeof(m_consecActive));
        memset(m_consecSilent, 0, sizeof(m_consecSilent));
        memset(m_segmentId, 0, sizeof(m_segmentId));
        memset(m_envPeak, 0, sizeof(m_envPeak));
        memset(m_unpickedFrames, 0, sizeof(m_unpickedFrames));
        memset(m_strengthDipFrames, 0, sizeof(m_strengthDipFrames));
        memset(m_transientHold, 0, sizeof(m_transientHold));
        memset(m_bandMask, 0, sizeof(m_bandMask));
        memset(m_laneStrength, 0, sizeof(m_laneStrength));
        memset(m_prevBandMask, 0, sizeof(m_prevBandMask));
        memset(m_keySnapActive, 0, sizeof(m_keySnapActive));
        memset(m_keySnapBand, 0, sizeof(m_keySnapBand));
        // 曲切替時は音色エンベロープモデルも必ずリセット(前曲の減衰予測を持ち越さない)
        for (int i = 0; i < KEY_COUNT; ++i)
            m_envModel[i].ResetOff();
        memset(m_reattackMark, 0, sizeof(m_reattackMark));
        memset(m_onsetBoostStreak, 0, sizeof(m_onsetBoostStreak));
        memset(m_harmonicGhostStreak, 0, sizeof(m_harmonicGhostStreak));
        m_historyDirty = true;
        m_keyDirty = true;
        m_historyCount = 0;
        m_historyHead = 0;
        m_framesPending = 0;
        m_rollSpeedCredit = 0;
        m_lastRollPushTick = 0;
        m_rollScrollValid = false;
        m_rollReady = false;
        m_bufwav3LevelDb = -60.0f;
        for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
            auto& f = m_historyRing[hi];
            memset(f.active, 0, sizeof(f.active));
            memset(f.strength, 0, sizeof(f.strength));
            memset(f.segment, 0, sizeof(f.segment));
            memset(f.bandMask, 0, sizeof(f.bandMask));
            memset(f.laneStrength, 0, sizeof(f.laneStrength));
            memset(f.expr, 0, sizeof(f.expr));
            memset(f.dynLevel, 0, sizeof(f.dynLevel));
            memset(f.reattack, 0, sizeof(f.reattack));
        }
        };

    bool gotCs = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (TryEnterCriticalSection(&m_cs)) {
            gotCs = true;
            break;
        }
        Sleep(1);
    }
    if (gotCs) {
        clearDisplayState();
        LeaveCriticalSection(&m_cs);
    }
    else {
        // CS が取れなくても表示用メンバは UI スレッド専用なので消す
        clearDisplayState();
    }

    StopAnalysisWorker();
    InterlockedExchange(&m_analysisBusy, 0);
    InterlockedExchange(&m_jobPending, 0);
    InterlockedExchange(&m_syncPosted, 0);

    // ワーカー完全停止後だけ解析テーブルを破棄（稼働中に触ると UAF）
    bool workerGone = (m_hAnalysisThread == NULL);
    if (!workerGone) {
        const DWORD wr = WaitForSingleObject(m_hAnalysisThread, 0);
        if (wr == WAIT_OBJECT_0) {
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
            if (m_hAnalysisWake) {
                CloseHandle(m_hAnalysisWake);
                m_hAnalysisWake = NULL;
            }
            InterlockedExchange(&m_workerStop, 0);
            workerGone = true;
        }
    }

    if (workerGone && TryEnterCriticalSection(&m_cs)) {
        m_ringWrite = 0;
        m_ringCount = 0;
        m_samplesSinceAnalyze = 0;
        m_playbackDelaySamples = 0;
        m_lastAnalyzeTick = 0;
        m_inputSampleRate = 0;
        m_winLow = m_winBass = m_winHigh = m_winOnset = 0;
        m_analysisTablesReady = false;
        m_analysisHasBass = false;
        LeaveCriticalSection(&m_cs);
    }
    else {
        // ワーカー生存時もスロットルだけリセット（次曲の解析投入を止めない）
        m_lastAnalyzeTick = 0;
    }

    // 前曲の絵が残ったまま止まって見えないよう再描画を要求
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::ResumePlaybackFeed()
{
    if (!::IsWindow(m_hWnd) || m_paintDisabled)
        return;
    if (!EnsureAnalysisWorkerAlive())
        return;
    m_feedEnabled = true;
}


void CPianoRoll::DoDataExchange(CDataExchange* pDX)
{
    CCustomBlurDialogExBase::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PR_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CPianoRoll, CCustomBlurDialogExBase)
    ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_WM_MOVE()
    ON_WM_SHOWWINDOW()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_CONTEXTMENU()
    ON_BN_CLICKED(IDC_PR_HELP, &CPianoRoll::OnBnClickedHelp)
    ON_COMMAND(ID_HELP_SHOWSHEET, &CPianoRoll::OnBnClickedHelp)
    ON_COMMAND_RANGE(IDM_ROLL_SPEED_BASE, IDM_ROLL_SPEED_BASE + ROLL_SPEED_COUNT - 1, &CPianoRoll::OnRollSpeedCmd)
    ON_COMMAND(IDM_ROLL_FREEZE, &CPianoRoll::OnToggleFreeze)
    ON_COMMAND(IDM_ROLL_CLEAR, &CPianoRoll::OnClearDisplay)
    ON_COMMAND(IDM_ROLL_LEGEND, &CPianoRoll::OnToggleExprLegend)
    ON_COMMAND(IDM_ROLL_EXPR, &CPianoRoll::OnToggleExprMarks)
    ON_COMMAND(IDM_ROLL_METER, &CPianoRoll::OnToggleLevelMeter)
    ON_COMMAND(IDM_ROLL_TOPMOST, &CPianoRoll::OnToggleAlwaysOnTop)
    ON_COMMAND(IDM_ROLL_REATTACK, &CPianoRoll::OnToggleReattackDetect)
    ON_COMMAND(IDM_ROLL_IMPULSE, &CPianoRoll::OnToggleImpulsiveGhost)
    ON_COMMAND(IDM_ROLL_HARM_GHOST, &CPianoRoll::OnToggleHarmonicGhost)
    ON_COMMAND(IDM_ROLL_HARM_PROF, &CPianoRoll::OnToggleHarmonicProfile)
    ON_COMMAND(IDM_ROLL_TUNE, &CPianoRoll::OnOpenTuneDialog)
    ON_COMMAND_RANGE(IDM_ROLL_VIEW_BASE, IDM_ROLL_VIEW_BASE + IDM_ROLL_VIEW_COUNT - 1, &CPianoRoll::OnViewModeCmd)
    ON_COMMAND(IDM_ROLL_CAM_RESET, &CPianoRoll::OnCamResetCmd)
    ON_COMMAND_RANGE(IDM_ROLL_KEYS_BASE, IDM_ROLL_KEYS_BASE + IDM_ROLL_KEYS_COUNT - 1, &CPianoRoll::OnKeyRangeCmd)
    ON_COMMAND(IDM_ROLL_NOTENAME, &CPianoRoll::OnToggleNoteNames)
    ON_COMMAND(IDM_ROLL_CAPTURE_MIDI, &CPianoRoll::OnToggleCaptureMidi)
    ON_COMMAND(IDM_ROLL_CAPTURE_MUSICXML, &CPianoRoll::OnToggleCaptureMusicXml)
    ON_COMMAND(IDM_ROLL_CHORD_PANEL, &CPianoRoll::OnToggleChordPanel)
    ON_COMMAND(IDM_ROLL_LOOPBACK_SCORE, &CPianoRoll::OnToggleLoopbackScore)
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEWHEEL()
    ON_MESSAGE(WM_PIANOROLL_SYNC, &CPianoRoll::OnSyncRequest)
    ON_MESSAGE(WM_PIANOROLL_ANALYSIS_DONE, &CPianoRoll::OnAnalysisDone)
END_MESSAGE_MAP()

BOOL CPianoRoll::OnInitDialog()
{
    CCustomBlurDialogExBase::OnInitDialog();
    SetWindowText(LL14(
        L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Rotolo pianoforte semplice",
        L"Rollo de piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة",
        L"Простой пианоролл", L"Einfache Klavierrolle", L"Rolo de piano simples", L"Eenvoudige pianorol",
        L"Prosta rolka pianina", L"Basit piyano rulosu"));
    // [ビルド確認用タグ] この文字列がタイトルバーに出ていれば、この CPianoRoll.cpp が
    // 実際にビルド・実行されている証拠になる。出ていなければ、差し替え忘れ/
    // 別コピーのビルド/キャッシュ等、ファイルが反映されていない問題を疑うこと。
    // 動作確認が済んだら削除して構わない。
#if 0
    {
        CString curTitle;
        GetWindowText(curTitle);
        curTitle += L" [PR-DBG-v7]";
        SetWindowText(curTitle);
    }
#endif
    ModifyStyle(WS_MINIMIZEBOX, 0);
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
    // キャプションアイコンは付けない。WM_SETICON(NULL) だけでは DWM が
    // 既定アイコンへフォールバックするため、Aero 有効時も常に
    // WS_EX_DLGMODALFRAME を立ててフレーム再計算する（イコライザーと同じ見た目）。
    ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

    {
        int sp = savedata.pianorollscrollspeed;
        if (sp < 25 || sp > 200) sp = 100;
        m_rollSpeedPct = sp;
        m_rollSpeedCredit = 0;
        m_lastRollPushTick = 0;
    }
    m_showExprLegend = (savedata.pianorollexprlegend != 0);
    m_showExprMarks = (savedata.pianorollexprmarks != 0);
    m_showLevelMeter = (savedata.pianorolllevelmeter != 0);
    m_alwaysOnTop = (savedata.pianorolltopmost != 0);
    m_reattackDetectEnabled = (savedata.pianorollreattack != 0);
    m_impulsiveGhostSuppressEnabled = (savedata.pianorollimpulse != 0);
    m_harmonicGhostGuardEnabled = (savedata.pianorollharmghost != 0);
    m_harmonicProfileGuardEnabled = (savedata.pianorollharmprof != 0);
    m_viewMode = (savedata.pianorollviewmode == 1) ? 1 : 0;
    m_keyRange = (savedata.pianorollkeyrange == 88) ? 88 : 108;
    m_showNoteNames = (savedata.pianorollnotename != 0);
    if (savedata.mpLoopbackScore) {
        ResumePlaybackFeed();
        CWnd* parent = CCC_GetActiveMainWindow();
        if (!parent) parent = this;
        EnsureDeviceRecordLoopbackFeed(parent);
    }
    {
        float yaw = (float)savedata.pianoroll3dyaw / 10.0f;
        float pitch = (float)savedata.pianoroll3dpitch / 10.0f;
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
        if (pitch < kView3dPitchMin) pitch = kView3dPitchMin;
        if (pitch > kView3dPitchMax) pitch = kView3dPitchMax;
        m_view3dYawDeg = yaw;
        m_view3dPitchDeg = pitch;
        float zoom = (float)savedata.pianoroll3dzoom / 100.0f;
        if (zoom < kView3dZoomMin) zoom = kView3dZoomMin;
        if (zoom > kView3dZoomMax) zoom = kView3dZoomMax;
        m_view3dZoom = zoom;
    }
    memset(m_wall3D, 0, sizeof(m_wall3D));
    m_wall3DRows = 0;
    m_frozen = false;

    if (savedata.pianorollx != -1)
        SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
            savedata.pianorollx, savedata.pianorolly,
            savedata.pianorollw, savedata.pianorollh,
            SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));
    else
        SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
            100, 150, 800, 450,
            SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));


    EnsureAnalysisTables(m_inputSampleRate);
    StartAnalysisWorker();
    UpdatePianoRollTimer();
    m_feedEnabled = true;
    m_paintDisabled = false;
    m_historyDirty = true;

    m_help.SetWindowText(L"?");
    m_help.SetFlat(TRUE);
    m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
    LayoutHelpBtn();
    if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
        if (m_help.GetSafeHwnd())
            m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
        m_tooltip.SetDelayTime(TTDT_INITIAL, 400);
        m_tooltip.SetDelayTime(TTDT_RESHOW, 120);
        m_tooltip.SetDelayTime(TTDT_AUTOPOP, 12000);
        m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 460);
        m_tooltip.Activate(TRUE);
    }
    EnableToolTips(TRUE);

    EnableMainWindowLock(&savedata.pianorollMainLock, TRUE);
    CCC_CaptionLayout(m_hWnd);
    LayoutHelpBtn();
    return TRUE;
}
float CPianoRoll::MidiToFreq(int midi)
{
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

int CPianoRoll::KeyBandIndex(int keyIndex)
{
    return PianoRoll108::KeyBandIndex(keyIndex);
}

double CPianoRoll::ReadMonoSample(const uint8_t* sp, int bits)
{
    switch (bits)
    {
    case 8:
        return (double(*sp) - 128.0) / 128.0;
    case 16: {
        int16_t s16;
        memcpy(&s16, sp, 2);
        return s16 / 32768.0;
    }
    case 24: {
        int32_t s24 = (int32_t(sp[2]) << 16) | (int32_t(sp[1]) << 8) | sp[0];
        if (s24 & 0x800000) s24 |= 0xFF000000;
        return s24 / 8388608.0;
    }
    case 32: {
        int32_t s32;
        memcpy(&s32, sp, 4);
        return s32 / 2147483648.0;
    }
    default:
        return 0.0;
    }
}

// Goertzel 係数と各窓関数をサンプルレートに合わせて計算/再計算する。
// サンプルレートが変化しなければキャッシュを流用するため低コスト。
// FeedPCM の EnterCriticalSection 内から呼ばれる。
void CPianoRoll::EnsureAnalysisTables(int sampleRate, int capCaptureFrames)
{
    if (sampleRate < 8000) sampleRate = 44100;
    const int cap = (capCaptureFrames > 0) ? capCaptureFrames : 0;
    int winLow = ScaleWinSamples(WIN_LOW_REF, sampleRate, cap);
    int winBass = ScaleWinSamples(WIN_BASS_REF, sampleRate, cap);
    int winHigh = ScaleWinSamples(WIN_HIGH_REF, sampleRate, cap);
    int winOnset = ScaleWinSamples(WIN_ONSET_REF, sampleRate, cap);
    if (winBass < winLow) winBass = winLow;
    if (winHigh > winLow) winHigh = winLow;
    if (winOnset > winLow) winOnset = winLow;
    if (winLow > WIN_SAMPLES_MAX) winLow = WIN_SAMPLES_MAX;
    if (winBass > WIN_SAMPLES_MAX) winBass = WIN_SAMPLES_MAX;
    if (winHigh > WIN_SAMPLES_MAX) winHigh = WIN_SAMPLES_MAX;
    if (winOnset > WIN_SAMPLES_MAX) winOnset = WIN_SAMPLES_MAX;
    if (sampleRate == m_inputSampleRate &&
        winLow == m_winLow && winBass == m_winBass &&
        winHigh == m_winHigh && winOnset == m_winOnset &&
        m_analysisTablesReady)
        return;

    m_inputSampleRate = sampleRate;
    m_winLow = winLow;
    m_winBass = winBass;
    m_winHigh = winHigh;
    m_winOnset = winOnset;
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        const double freq = MidiToFreq(midi);
        m_goertzelCoeffs[i] = 2.0 * cos(2.0 * M_PI * freq / sampleRate);
    }

    for (int n = 0; n < m_winLow; ++n) {
        const double denom = (m_winLow > 1) ? (double)(m_winLow - 1) : 1.0;
        m_hannLow[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / denom);
    }

    for (int n = 0; n < m_winOnset; ++n) {
        const double denom = (m_winOnset > 1) ? (double)(m_winOnset - 1) : 1.0;
        m_hannOnset[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / denom);
    }

    for (int n = 0; n < m_winBass; ++n) {
        const double denom = (m_winBass > 1) ? (double)(m_winBass - 1) : 1.0;
        m_hannBass[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / denom);
    }

    for (int n = 0; n < m_winHigh; ++n) {
        const double denom = (m_winHigh > 1) ? (double)(m_winHigh - 1) : 1.0;
        m_blackmanHigh[n] = 0.42 - 0.5 * cos(2.0 * M_PI * n / denom)
            + 0.08 * cos(4.0 * M_PI * n / denom);
    }

    memset(m_windowedLow, 0, (size_t)m_winLow * sizeof(double));
    memset(m_windowedBass, 0, (size_t)m_winBass * sizeof(double));
    memset(m_windowedHigh, 0, (size_t)m_winHigh * sizeof(double));
    memset(m_windowedOnset, 0, (size_t)m_winOnset * sizeof(double));
    memset(m_analysisBuf, 0, (size_t)m_winLow * sizeof(double));
    memset(m_bassAnalysisBuf, 0, (size_t)m_winBass * sizeof(double));
    m_analysisTablesReady = true;
}

// Goertzel アルゴリズムで単一周波数の振幅(magnitude)を計算する。
// 係数 coefficient = 2*cos(2π*f/sr) は EnsureAnalysisTables で事前計算済み。
// window が非 null なら掛け算でサイドローブを抑制する(Hann, Blackman 等)。
// 戻り値は numSamples で正規化した振幅(0.0〜)。FFT 全帯域ではなく対象周波数だけ
// 計算するため 88 鍵 × O(N) の計算量で済む(FFT の O(N log N) より有利な用途)。
double CPianoRoll::GoertzelMagnitude(const double* samples, int numSamples,
    double coefficient, const double* window)
{
    double s_prev = 0.0, s_prev2 = 0.0;
    for (int n = 0; n < numSamples; ++n) {
        const double x = window ? (samples[n] * window[n]) : samples[n];
        const double s = x + coefficient * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }
    const double power = s_prev2 * s_prev2 + s_prev * s_prev - coefficient * s_prev * s_prev2;
    return sqrt(power > 0.0 ? power : 0.0) * 2.5 / numSamples;
}

float CPianoRoll::ApplyDisplayScale(float rawAmp, int keyIndex, int winSamples, int refWinSamples)
{
    if (rawAmp <= 1e-10f) return 0.0f;
    if (winSamples <= 0) winSamples = refWinSamples;
    if (refWinSamples <= 0) refWinSamples = WIN_LOW_REF;

    float amp = (float)rawAmp * ((float)winSamples / (float)refWinSamples);

    const float hz = PianoKey::KeyHz(keyIndex);
    float eq = sqrtf(440.0f / (hz > 20.0f ? hz : 20.0f));
    float eqMin = 0.60f;
    float eqMax = 1.8f;
    if (keyIndex < PianoRoll108::BASS_END)
        eqMax = 1.05f;
    else if (keyIndex < 12)
        eqMax = 1.6f;
    else if (keyIndex >= 84)
        eqMin = 0.72f;
    else if (keyIndex >= 72)
        eqMin = 0.65f;
    if (eq < eqMin) eq = eqMin;
    if (eq > eqMax) eq = eqMax;
    amp *= eq;

    const double x = (double)amp * 80.0;
    double out = x * x * 0.003;
    if (out > 10.0) out = 10.0;
    return (float)out;
}

float CPianoRoll::ApplyDetectScale(float rawAmp, int winSamples, int refWinSamples)
{
    if (rawAmp <= 1e-10f) return 0.0f;
    if (winSamples <= 0) winSamples = refWinSamples;
    if (refWinSamples <= 0) refWinSamples = WIN_LOW_REF;
    const float amp = (float)rawAmp * ((float)winSamples / (float)refWinSamples);
    return ScaleGoertzelAmpFlat(amp);
}




// 再生スレッドからデコード済み PCM を受け取り、モノラル double に変換して
// リングバッファへ書き込む。m_cs で保護されているためスレッドセーフ。
// ResetPlaybackState 後は m_feedEnabled=true に戻すまで書き込まれない。
void CPianoRoll::FeedPCM(const void* pData, int frames,
    int sampleRate, int bits, int channels, int playbackDelaySamples)
{
    // SyncPianoRollFromPlayCursor → AnalyzePlayCursorMono が解析の正本。
    // リングは誰も読まないのに再生スレッドが毎サンプル m_cs で書いており、
    // 解析/UI と争奪して滞留の一因になるため書き込みを停止する。
    (void)pData; (void)frames; (void)sampleRate; (void)bits;
    (void)channels; (void)playbackDelaySamples;
}

// bufwav3 の再生バッファ直後から呼ばれる。mono は既にモノラル変換済み。
// ANALYZE_MIN_MS(4ms)のスロットリングでワーカーを過負荷から守る。
// ジョブバッファ(m_jobMono)へコピーして SetEvent でワーカーを起こす。
// 前のジョブが完了していない場合も InterlockedExchange で上書きする
// (古い分析より最新フレームを優先する)。
void CPianoRoll::AnalyzePlayCursorMono(const double* mono, int frameCount, int sampleRate)
{
    if (!m_feedEnabled) return;
    if (!mono || frameCount < MinAnalyzeFrameCount(sampleRate, frameCount) || sampleRate < 8000) return;
    // 形式切替後にワーカーが死んだまま/stop のまま残ると解析が永久に止まる
    if (!EnsureAnalysisWorkerAlive()) return;
    if (!m_hAnalysisWake) return;

    const DWORD now = GetTickCount();
    const DWORD minMs = EffectiveAnalyzeMinMs();
    if (m_lastAnalyzeTick != 0 && (now - m_lastAnalyzeTick) < minMs)
        return;
    m_lastAnalyzeTick = now;

    EnterCriticalSection(&m_jobCs);
    int copyFrames = frameCount;
    if (copyFrames > WIN_SAMPLES_MAX) copyFrames = WIN_SAMPLES_MAX;
    memcpy(m_jobMono, mono, (size_t)copyFrames * sizeof(double));
    m_jobFrameCount = copyFrames;
    m_jobSampleRate = sampleRate;
    InterlockedExchange(&m_jobPending, 1);
    LeaveCriticalSection(&m_jobCs);
    SetEvent(m_hAnalysisWake);
}

bool CPianoRoll::ShouldCaptureAnalyzeJob()
{
    if (!m_feedEnabled) return false;
    if (InterlockedCompareExchange(&m_analysisBusy, 0, 0) != 0) return false;
    if (InterlockedCompareExchange(&m_jobPending, 0, 0) != 0) return false;
    if (m_lastAnalyzeTick != 0) {
        const DWORD now = GetTickCount();
        if ((now - m_lastAnalyzeTick) < EffectiveAnalyzeMinMs())
            return false;
    }
    return true;
}

void CPianoRoll::SetChannelMeterDb(const float* dbPerChannel, int channelCount)
{
    // UI スレッド専用。m_cs(Goertzel)を取ると解析中にメーターが遅延する。
    static constexpr float kPeakDecay = 0.994f;
    static constexpr float kFillAttack = 0.55f;
    static constexpr float kFillRelease = 0.18f;

    m_chMeterCount = channelCount;
    if (m_chMeterCount < 0) m_chMeterCount = 0;
    if (m_chMeterCount > PIANO_METER_CH_MAX) m_chMeterCount = PIANO_METER_CH_MAX;
    bool meterChanged = false;
    for (int i = 0; i < PIANO_METER_CH_MAX; ++i) {
        if (i < m_chMeterCount && dbPerChannel) {
            const float in = dbPerChannel[i];
            m_chMeterDb[i] = in;
            float lin = (in <= -59.0f) ? 0.0f : powf(10.0f, in / 20.0f);

            float& peak = m_chMeterAutoPeak[i];
            if (lin > peak)
                peak = lin;
            else
                peak = peak * kPeakDecay + lin * (1.0f - kPeakDecay);
            if (peak < 0.002f) peak = 0.002f;

            float norm = lin / peak;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            float& fill = m_chMeterFill[i];
            const float prevFill = fill;
            const float rate = (norm >= fill) ? kFillAttack : kFillRelease;
            fill += (norm - fill) * rate;
            if (!meterChanged && fabsf(fill - prevFill) > 0.02f)
                meterChanged = true;
        }
        else {
            const float prevFill = m_chMeterFill[i];
            m_chMeterDb[i] = -60.0f;
            m_chMeterFill[i] *= 0.85f;
            m_chMeterAutoPeak[i] = 0.02f;
            if (!meterChanged && m_chMeterFill[i] > 0.01f && prevFill > 0.01f)
                meterChanged = true;
        }
    }
    if (meterChanged)
        m_meterDirty = true;
}

// 窓掛け済みバッファから Goertzel 解析を実行し m_rawStrengths を更新する。
// 108鍵: 低〜中は 8192、高は 4096（低音だけ長窓にすると時間軸がずれるため統一）
void CPianoRoll::RunGoertzelFromBuffer(const double* winLow,
    const double* winBass, int bassWinLen)
{
    if (!winLow) return;

    for (int i = 0; i < m_winLow; ++i)
        m_analysisBuf[i] = winLow[i];

    const bool hasBass = (winBass && bassWinLen >= m_winBass);
    m_analysisHasBass = hasBass;
    if (hasBass) {
        for (int i = 0; i < m_winBass; ++i)
            m_bassAnalysisBuf[i] = winBass[i];
    }

    const float levelDb = Cfg::LevelDbForDynamics(
        m_analysisBuf, m_winLow,
        hasBass ? m_bassAnalysisBuf : nullptr,
        hasBass ? m_winBass : 0);

    const float gainDb = Cfg::MakeupGainDb(levelDb);
    // 絶対値ノイズフロア／ピック閾値は AGC「後」の動作点を基準にする。
    // 旧: 入力生レベルをそのまま使う → 静かな曲だけ相対閾値が緩み、
    //     マスター上げ後は床が下がってゴーストが増える非対称が残っていた。
    m_lastGainDb = gainDb;
    m_bufwav3LevelDb = levelDb + gainDb;
    Cfg::ApplyGainDbInPlace(m_analysisBuf, m_winLow, gainDb);
    if (hasBass)
        Cfg::ApplyGainDbInPlace(m_bassAnalysisBuf, m_winBass, gainDb);

    // 真の無音のみ早期リターン。AGC可能な静かな曲は通す。
    if (levelDb < -58.0f) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_noteStrength[i] = 0.0f;
            m_rawStrengths[i] = 0.0f;
            m_displayStrengths[i] = 0.0f;
            m_onsetStrengths[i] = 0.0f;
            m_smoothedStrengths[i] *= 0.5f;
            m_displaySmoothed[i] *= 0.5f;
            m_consecActive[i] = 0;
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            m_strengthDipFrames[i] = 0;
            m_transientHold[i] = 0;
            m_envPeak[i] = 0.0f;
            // 無音区間は音色エンベロープモデルもオフへ戻す(次の音を新規オンセットとして扱う)
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
            m_onsetBoostStreak[i] = 0;
            m_harmonicGhostStreak[i] = 0;
        }
        memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
        m_detectSilent = true;
        return;
    }

    m_detectSilent = false;

    for (int i = 0; i < m_winLow; ++i)
        m_windowedLow[i] = m_analysisBuf[i] * m_hannLow[i];
    for (int i = 0; i < m_winHigh; ++i)
        m_windowedHigh[i] = m_analysisBuf[i + (m_winLow - m_winHigh)] * m_blackmanHigh[i];
    const double* onsetSrc = m_analysisBuf + (m_winLow - m_winOnset);
    for (int i = 0; i < m_winOnset; ++i)
        m_windowedOnset[i] = onsetSrc[i] * m_hannOnset[i];

    const int splitLo = PianoRoll108::WIN_LONG_END;
    const int splitHi = PianoRoll108::WIN_MID_END;

    auto storeKey = [this](int i, float goertzel, int winSamples, int refWin) {
        m_rawStrengths[i] = ApplyDetectScale(goertzel, winSamples, refWin);
        m_displayStrengths[i] = ApplyDisplayScale(goertzel, i, winSamples, refWin);
    };

    if (hasBass) {
        for (int i = 0; i < m_winBass; ++i)
            m_windowedBass[i] = m_bassAnalysisBuf[i] * m_hannBass[i];
        PianoRollGoertzelBatchAvx2(
            m_windowedBass, m_winBass, m_goertzelCoeffs,
            0, splitLo, m_goertzelRawScratch);
        for (int i = 0; i < splitLo; ++i)
            storeKey(i, (float)m_goertzelRawScratch[i], m_winBass, WIN_BASS_REF);
    }
    else {
        PianoRollGoertzelBatchAvx2(
            m_windowedLow, m_winLow, m_goertzelCoeffs,
            0, splitLo, m_goertzelRawScratch);
        for (int i = 0; i < splitLo; ++i)
            storeKey(i, (float)m_goertzelRawScratch[i], m_winLow, WIN_BASS_REF);
    }

    PianoRollGoertzelBatchAvx2(
        m_windowedLow, m_winLow, m_goertzelCoeffs,
        splitLo, splitHi, m_goertzelRawScratch);
    for (int i = splitLo; i < splitHi; ++i)
        storeKey(i, (float)m_goertzelRawScratch[i - splitLo], m_winLow, WIN_LOW_REF);

    PianoRollGoertzelBatchAvx2(
        m_windowedHigh, m_winHigh, m_goertzelCoeffs,
        splitHi, KEY_COUNT, m_goertzelRawScratch);
    for (int i = splitHi; i < KEY_COUNT; ++i)
        storeKey(i, (float)m_goertzelRawScratch[i - splitHi], m_winHigh, WIN_HIGH_REF);

    PianoRollGoertzelBatchAvx2(
        m_windowedOnset, m_winOnset, m_goertzelCoeffs,
        0, KEY_COUNT, m_goertzelRawScratch);
    for (int i = 0; i < KEY_COUNT; ++i)
        m_onsetStrengths[i] = ApplyDetectScale(
            (float)m_goertzelRawScratch[i], m_winOnset, WIN_ONSET_REF);

    for (int i = 0; i < KEY_COUNT; ++i) {
        const float alpha = PianoRoll108::IirAlphaForKey(i);
        m_smoothedStrengths[i] =
            m_smoothedStrengths[i] * (1.0f - alpha) + m_rawStrengths[i] * alpha;
        m_displaySmoothed[i] =
            m_displaySmoothed[i] * (1.0f - alpha) + m_displayStrengths[i] * alpha;
    }
    // UpdateNoteStates / PushDisplayFrames は m_cs 下の PublishDetectResults で行う
}

namespace
{
    // 倍音ゴースト抑制用: candidate が「他の音の倍音として際どく通過した」かどうかを判定する。
    // PianoKeyTable.h の PassesFundamentalTest/PassesFundamentalTestSustain と同じ
    // 判定式(下側の潜在的な基音候補との比率が 0.78 を超えたら本来は倍音として棄却)
    // を土台にしているが、目的が異なるため単純化して再現している:
    //   - あちらは「棄却するかどうか」の最終判定(1回だけ実行)
    //   - こちらは「棄却ラインのすぐ近く(僅差)で生き残ったか」だけを見る予備検知
    // 明確に独立している音(下の潜在基音がほとんど鳴っていない)は対象外となり、
    // 実際にゴーストが観測された「閾値をまたいで一瞬だけ通過する」ケースだけを狙う。
    bool IsMarginalFund(const float* blend, int candidate, int count, float marginRatio, float rejectRatio)
    {
        if (!blend || candidate < 0 || candidate >= count) return false;
        const float sc = blend[candidate];
        if (sc <= 0.0f) return false;
        for (int n = PianoKey::HARMONIC_N_MIN; n <= PianoKey::HARMONIC_N_MAX; ++n) {
            const int lo = PianoKey::HarmonicDownKey(candidate, n);
            if (lo < 0 || lo >= count || lo >= candidate) continue;
            const float rejectAt = sc * rejectRatio;
            const float marginAt = rejectAt * marginRatio;
            if (blend[lo] >= marginAt && blend[lo] < rejectAt)
                return true;
        }
        return false;
    }
}

namespace
{
    // 隣接半音間のピック“ホッピング”対策。
    // 実音の周波数が2鍵のちょうど中間に近いと、IsLocalPeakInBand/SnapToLocalMaximum
    // (NoteFundamentalPick.h)の判定がフレームごとに僅差で入れ替わり、同じ1音が
    // 隣接する2鍵の間を毎フレーム飛び移ってしまう。m_activeKeys は鍵ごとに独立して
    // いるため、これがそのまま「隣接する2本のバーが交互にチラつく」症状になる。
    // 前フレームで鳴っていた鍵(prevActive)と、今フレーム僅差で勝った隣の鍵の値が
    // 近い(marginRatio以内)場合は、前フレームの鍵を優先して復活させ、
    // 隣の鍵は降ろすことでホッピングを抑える。
    // NoteFundamentalPick.h 側の判定式自体には一切手を入れない、後付けの安定化。
    void StabilizeBinHop(const float* blend, bool* picked,
        const bool* prevActive, int count, float marginRatio)
    {
        if (!blend || !picked || !prevActive || count <= 0) return;
        for (int i = 0; i < count; ++i) {
            if (!prevActive[i] || picked[i]) continue;
            const float bi = blend[i];
            if (bi <= 0.0f) continue;
            for (int d = -1; d <= 1; d += 2) {
                const int j = i + d;
                if (j < 0 || j >= count || !picked[j]) continue;
                const float bj = blend[j];
                if (bj <= 0.0f) continue;
                if (bi >= bj * marginRatio) {
                    picked[i] = true;
                    picked[j] = false;
                    break;
                }
            }
        }
    }
}

void CPianoRoll::UpdateNoteStates()
{
    using namespace Cfg;

    const float pickScale = PickThreshScaleFromLevelDb(m_bufwav3LevelDb);
    const float silenceAbs = PrTuneF(savedata.prTuneSilencePct, SILENCE_ABS);
    const float bandSilBass = PrTuneF(savedata.prTuneBandSilBassPct, BAND_SILENCE_BASS);
    const float bandSilMid = PrTuneF(savedata.prTuneBandSilMidPct, BAND_SILENCE_MID);
    const float bandSilTre = PrTuneF(savedata.prTuneBandSilTrePct, BAND_SILENCE_TRE);
    const float retriggerRatio = PrTuneF(savedata.prTuneRetrigPct, RETRIGGER_RATIO);
    const float harmGhostMargin = PrTuneF(savedata.prTuneHarmGhostPct, kHarmonicGhostMarginRatio);
    const float harmRejectRatio = PrTuneF(savedata.prTuneHarmRejectPct, 0.78f);
    const float harmProfMin = PrTuneF(savedata.prTuneHarmProfPct, kHarmonicProfileNoiseMinConfidence);
    const float pickBassRel = PrTuneF(savedata.prTunePickBassPct, 0.28f);
    const float pickLowMidRel = PrTuneF(savedata.prTunePickLowMidPct, 0.20f);
    const float pickMelodyRel = PrTuneF(savedata.prTunePickMelodyPct, 0.10f);
    const float pickTreRel = PrTuneF(savedata.prTunePickTrePct, 0.22f);
    const float onsetDeltaScale = PrTuneF(savedata.prTuneOnsetDeltaPct, 1.0f);

    float maxS = 0.0f;
    for (int i = 0; i < KEY_COUNT; ++i)
        if (m_smoothedStrengths[i] > maxS) maxS = m_smoothedStrengths[i];

    float blend[KEY_COUNT];
    PianoRoll108::BuildDetectionSpectrum(m_smoothedStrengths, m_rawStrengths, blend, KEY_COUNT);

    const float bassMax = BandMaxStrength(blend, 0, PianoRoll108::BASS_END);
    const float midMax = BandMaxStrength(blend, PianoRoll108::BASS_END, PianoRoll108::MID_END);
    const float treMax = BandMaxStrength(blend, PianoRoll108::MID_END, KEY_COUNT);
    const bool anyBandLive =
        bassMax >= bandSilBass ||
        midMax >= bandSilMid ||
        treMax >= bandSilTre;

    if (!anyBandLive || maxS < silenceAbs) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_activeKeys[i] = false;
            m_noteStrength[i] = 0.0f;
            m_consecActive[i] = 0;
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            m_strengthDipFrames[i] = 0;
            m_transientHold[i] = 0;
            m_envPeak[i] = 0.0f;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_smoothedStrengths[i] *= 0.4f;
            m_displaySmoothed[i] *= 0.4f;
            // 無音区間は音色エンベロープモデルもオフへ戻す
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
            m_onsetBoostStreak[i] = 0;
            m_harmonicGhostStreak[i] = 0;
        }
        memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
        return;
    }

    // ノイズ床: AGC後の固定基準 + スペクトル相対床。
    // 旧: absFloor = base * gainLinear → ブーストで床が上がり静かな曲が載らず、
    //     マスター上げ(gain≈0)で床が最低になりゴーストが増える。
    // 新: 双方向AGCで動作点を揃えたうえで、相対床で砂粒を抑える。
    // 大きなブースト時のみソースSNR悪化分を控えめに持ち上げる。
    const float baseFloor = PrTuneF(savedata.prTuneAbsFloorPct, ABS_NOISE_FLOOR_BASE);
    float absFloor = baseFloor;
    const float noiseEst = SpectralNoiseEstimate(blend, KEY_COUNT);
    if (noiseEst > 0.0f) {
        const float relFloor = noiseEst * SPECTRAL_NOISE_FLOOR_MUL;
        if (relFloor > absFloor) absFloor = relFloor;
    }
    if (m_lastGainDb > 8.0f) {
        const float extra = powf(10.0f, (m_lastGainDb - 8.0f) * 0.25f / 20.0f);
        const float boostFloor = baseFloor * extra;
        if (boostFloor > absFloor) absFloor = boostFloor;
    }

    bool picked[KEY_COUNT];
    PianoRoll108::BuildFramePicks(blend, picked, KEY_COUNT, pickScale, absFloor,
        m_onsetStrengths, m_prevOnsetStrengths,
        pickBassRel, pickLowMidRel, pickMelodyRel, pickTreRel, onsetDeltaScale);

#ifdef _DEBUG
    // [診断用] BuildFramePicks直後、picked[]そのものが絶対値フロアで
    // 本当に弾かれているかを確認する。ここで picked[i]==false なのに
    // 後段で active=1 になっているなら、原因はホールド系ロジック側にある。
    {
        static DWORD s_dbgPickLogTick = 0;
        const DWORD dbgNow2 = GetTickCount();
        if (dbgNow2 - s_dbgPickLogTick >= 500) {
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (picked[i] && blend[i] < absFloor * 0.5f) {
                    char buf[160];
                    sprintf_s(buf, "[PianoRollDbg][FLOOR-LEAK] key=%d picked=1 blend=%.5f floor=%.5f\n",
                        i, blend[i], absFloor);
                    OutputDebugStringA(buf);
                }
            }
            s_dbgPickLogTick = dbgNow2;
        }
    }
#endif

    // 隣接半音ホッピング対策(実験的、既定で常時有効)。
    // 前フレームでアクティブだった鍵(m_activeKeys、この時点ではまだ前フレームの値)を
    // 基準に、僅差で入れ替わっただけの隣接ピックを元に戻す。
    static constexpr float kAdjacentHoppingMarginRatio = 0.85f;
    StabilizeBinHop(blend, picked, m_activeKeys, KEY_COUNT, kAdjacentHoppingMarginRatio);

    for (int i = 0; i < KEY_COUNT; ++i) {
        const float sigStrength = blend[i];
        bool effective = picked[i];

        if (!effective && m_activeKeys[i]) {
            float holdRatio;
            if (i < BAND_BASS_END)
                holdRatio = PrTuneF(savedata.prTuneHoldBassPct, HOLD_ENV_BASS);
            else if (i < BAND_MID_END)
                holdRatio = PrTuneF(savedata.prTuneHoldMidPct, HOLD_ENV_MID);
            else
                holdRatio = PrTuneF(savedata.prTuneHoldTrePct, HOLD_ENV_TRE);
            if (holdRatio > 0.0f && m_envPeak[i] > 0.001f) {
                if (sigStrength >= m_envPeak[i] * holdRatio)
                    effective = true;
                else if (m_smoothedStrengths[i] >= m_envPeak[i] * holdRatio * 0.82f)
                    effective = true;
            }
        }

        // ホールド延長でもパッシブ倍音を残さない。
        // ただし C4〜C6 のメロディー帯は、ピック側の FinishPicks に任せ、
        // ここでの二次剪定でピアノを再キルしない（パッチ連鎖の主因だった）。
        if (effective &&
            (i < PianoRoll108::C4_KEY || i >= PianoRoll108::O5_HI) &&
            PianoKey::IsHarmonicGhostPartial(blend, i, KEY_COUNT, PianoRoll108::BASS_END)) {
            effective = false;
        }

        if (effective) {
            ++m_consecActive[i];
            m_consecSilent[i] = 0;
            m_unpickedFrames[i] = 0;
            if (sigStrength > m_envPeak[i])
                m_envPeak[i] = sigStrength;
            else
                m_envPeak[i] = m_envPeak[i] * 0.92f + sigStrength * 0.08f;
            m_strengthDipFrames[i] = 0;
        }
        else {
            ++m_consecSilent[i];
            if (!m_activeKeys[i]) {
                if (m_consecSilent[i] >= ATTACK_MISS_GRACE)
                    m_consecActive[i] = 0;
            }
            if (m_activeKeys[i]) {
                ++m_unpickedFrames[i];
                if (m_envPeak[i] > 0.001f &&
                    sigStrength < m_envPeak[i] * retriggerRatio)
                    ++m_strengthDipFrames[i];
                else
                    m_strengthDipFrames[i] = 0;
            }
        }

        const bool onsetBoost = picked[i] &&
            PianoRoll108::OnsetSupportsPick(m_onsetStrengths, m_prevOnsetStrengths, i, pickScale, onsetDeltaScale);
        // 再アタック判定(UpdateEnvelope)は本関数の後段で呼ばれるため、
        // ここで計算済みのオンセット判定結果を保存しておいて使い回す。
        m_onsetBoostThisFrame[i] = onsetBoost;
        if (onsetBoost) {
            if (m_onsetBoostStreak[i] < 255) ++m_onsetBoostStreak[i];
        }
        else {
            m_onsetBoostStreak[i] = 0;
        }
        const int attackNeed = onsetBoost ? 1 : AttackFramesForKey(i);

        bool cur = m_activeKeys[i];
        if (!cur) {
            if (effective && m_consecActive[i] >= attackNeed) {
                bool allowOn = true;
                if (m_harmonicGhostGuardEnabled) {
                    const bool suspect = IsMarginalFund(
                        blend, i, KEY_COUNT, harmGhostMargin, harmRejectRatio);
                    if (suspect) {
                        // [特性ベース判定] 振幅のしきい値では「本物の小さい音」と
                        // 「ゴースト(親音への追従に過ぎない漏れ込み)」は区別できない。
                        // 違いは振る舞いの「形」にある: ゴーストは親音の減衰に
                        // ただ追従するだけで、それ自体のアタック(短窓オンセットの
                        // 立ち上がり)を持たない。本物の音は音量が小さくても、
                        // 自分自身のアタック transient を持つはずである。
                        // そこで振幅の持続フレーム数ではなく、m_onsetBoostStreak
                        // (実測済みの短窓オンセット信号が連続で立っているか)を
                        // 直接の合否基準にする。
                        m_harmonicGhostStreak[i] = m_onsetBoostStreak[i];
                        allowOn = (m_onsetBoostStreak[i] >= kHarmonicGhostConfirmFrames);
                    }
                    else {
                        m_harmonicGhostStreak[i] = 0;
                    }
                }
                if (allowOn && m_harmonicProfileGuardEnabled) {
                    // メロディー帯(C4-C6)はプロファイル追加判定を掛けない。
                    // 未較正テンプレがピアノをノイズ/部分音と誤認し、拾えない主因になっていた。
                    const bool melodyBand =
                        (i >= PianoRoll108::C4_KEY && i < PianoRoll108::O5_HI);
                    if (!melodyBand) {
                        if (HarmonicProfile::LooksLikeNoiseProfile(
                            blend, i, KEY_COUNT, harmProfMin)) {
                            allowOn = false;
                        }
                        else if (HarmonicProfile::LooksLikePartialGhost(blend, i, KEY_COUNT)) {
                            allowOn = false;
                        }
                    }
                }
                if (allowOn) {
                    cur = true;
                    m_consecSilent[i] = 0;
                    ++m_segmentId[i];
                    m_envPeak[i] = sigStrength;
                    m_unpickedFrames[i] = 0;
                    m_strengthDipFrames[i] = 0;
                    m_harmonicGhostStreak[i] = 0;
                }
            }
            else {
                m_harmonicGhostStreak[i] = 0;
            }
        }
        else {
            if (effective) {
                m_unpickedFrames[i] = 0;
                m_strengthDipFrames[i] = 0;
            }
            int gapLimit = VisGapFrames(i);
            if (m_envPeak[i] > 0.12f)
                gapLimit += VIS_GAP_SUSTAIN_BONUS;
            const int releaseLimit = TemporalFrames(i, RELEASE_FRAMES);
            const bool gapDetected =
                m_unpickedFrames[i] >= gapLimit ||
                m_strengthDipFrames[i] >= gapLimit;
            if (gapDetected || m_consecSilent[i] >= releaseLimit) {
                cur = false;
                m_consecActive[i] = 0;
                m_envPeak[i] = 0.0f;
                m_unpickedFrames[i] = 0;
                m_strengthDipFrames[i] = 0;
            }
        }
        m_activeKeys[i] = cur;
    }

    // ホールド延長だけで隣半音が同時残った場合だけ落とす。
    // 両方とも本フレームのピックなら CollapseNearby 済みの正当な近接なので触らない
    // （全域 ForceUnique は和音・装飾を潰して昨日より悪化した）。
    for (int i = 0; i + 1 < KEY_COUNT; ++i) {
        if (!m_activeKeys[i] || !m_activeKeys[i + 1]) continue;
        const bool pi = picked[i];
        const bool pj = picked[i + 1];
        if (pi && pj) continue;
        int drop = -1;
        if (pi && !pj) drop = i + 1;
        else if (!pi && pj) drop = i;
        else drop = (blend[i] >= blend[i + 1]) ? (i + 1) : i;
        m_activeKeys[drop] = false;
        m_consecActive[drop] = 0;
        m_envPeak[drop] = 0.0f;
        m_unpickedFrames[drop] = 0;
        m_strengthDipFrames[drop] = 0;
        m_noteStrength[drop] = 0.0f;
        m_bandMask[drop] = 0;
        memset(m_laneStrength[drop], 0, sizeof(m_laneStrength[drop]));
    }

#ifdef _DEBUG
    // [診断用] 表示(スクロール描画)ではなく、検出(m_activeKeys)そのものが
    // 実際にどれだけ高頻度で点滅しているかを直接確認するためのログ。
    // 0.5秒間に3回以上オン/オフが切り替わった鍵だけを出力する。
    // DebugView やVisual Studioの出力ウィンドウで確認できる。
    {
        static bool s_dbgPrevActive[KEY_COUNT] = {};
        static int  s_dbgTransitions[KEY_COUNT] = {};
        static DWORD s_dbgLastLogTick = 0;
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (m_activeKeys[i] != s_dbgPrevActive[i]) {
                ++s_dbgTransitions[i];
                s_dbgPrevActive[i] = m_activeKeys[i];
            }
        }
        const DWORD dbgNow = GetTickCount();
        if (dbgNow - s_dbgLastLogTick >= 500) {
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (s_dbgTransitions[i] >= 3) {
                    char buf[160];
                    sprintf_s(buf, "[PianoRollDbg] key=%d transitions/0.5s=%d blend=%.4f envPeak=%.4f active=%d\n",
                        i, s_dbgTransitions[i], blend[i], m_envPeak[i], (int)m_activeKeys[i]);
                    OutputDebugStringA(buf);
                }
                s_dbgTransitions[i] = 0;
            }
            s_dbgLastLogTick = dbgNow;
        }
    }
#endif

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) {
            m_noteStrength[i] = 0.0f;
            continue;
        }
        // 描画強度は display 経路。検出 envPeak はホールド判定専用。
        float disp = m_displaySmoothed[i];
        if (disp <= 0.0f) disp = m_displayStrengths[i];
        if (disp <= 0.0f) disp = m_smoothedStrengths[i];
        if (disp <= 0.0f) disp = m_rawStrengths[i];
        if (disp > 10.0f) disp = 10.0f;
        m_noteStrength[i] = disp;
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_bandMask[i] = 0;
        memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
        if (!m_activeKeys[i]) continue;
        const int band = KeyBandIndex(i);
        m_bandMask[i] = (uint8_t)(1u << band);
        m_laneStrength[i][0] = m_noteStrength[i];
    }

    DetectExpressions();

    // 音色エンベロープ更新 + 再アタック(ゲート連結中の同鍵連打)検出。
    // DetectExpressions() の後で呼ぶこと(exprFlags をリセットせず ACCENT を追加するため)。
    UpdateEnvelope();

    memcpy(m_prevOnsetStrengths, m_onsetStrengths, sizeof(m_onsetStrengths));
}

// 音色エンベロープモデル(NoteEnvelopeModel.h)の更新。
// 各アクティブ鍵につき:
//   1) NoteEnvelope::Update() で「直近の谷からの実測リバウンド量」と
//      「このフレームの短窓オンセット判定(m_onsetBoostThisFrame)」の
//      両方が同時に成立した場合のみ再アタックと判定し、セグメントID
//      (見た目上のノート境界)を進めて ACCENT を立てる。
//      片方だけでは発火しないため、持続音の自然な揺らぎだけでは暴走しない。
//   2) m_impulsiveGhostSuppressEnabled が true の場合のみ、
//      打撃/ノイズ的な減衰形状(LooksImpulsive)と判定された鍵をオフに戻す。
//      既定は無効(opt-in)。誤検出で弱いスタッカートまで消す可能性があるため、
//      効果を確認しながら有効化すること。
void CPianoRoll::UpdateEnvelope()
{
    const float nowMs = (float)GetTickCount();

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) {
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
            continue;
        }

        if (!m_reattackDetectEnabled) {
            // 機能自体を無効化している場合でも、状態は前回オフのまま維持し
            // 従来通りの見た目(タイ検出なし)にする。
            m_reattackMark[i] = false;
            continue;
        }

        // 単発フレームのオンセット支持(picked[]のチラつき起因を含む)を除外するため、
        // 連続 kOnsetConfirmFrames フレーム以上支持が続いた場合のみ「本物の攻撃」として扱う。
        const bool confirmedOnset = m_onsetBoostStreak[i] >= kOnsetConfirmFrames;
        const bool reattack = NoteEnvelope::Update(m_envModel[i], nowMs, m_noteStrength[i],
            confirmedOnset);
        m_reattackMark[i] = reattack;
        if (reattack) {
            ++m_segmentId[i];
            m_exprFlags[i] |= NoteExpr::ACCENT;
        }

        if (m_impulsiveGhostSuppressEnabled &&
            NoteEnvelope::LooksImpulsive(m_envModel[i])) {
            // 打撃/ノイズ的な鍵だけをオフへ戻す。他の鍵の状態には触れない。
            m_activeKeys[i] = false;
            m_noteStrength[i] = 0.0f;
            m_envPeak[i] = 0.0f;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_envModel[i].ResetOff();
            m_reattackMark[i] = false;
        }
    }
}



namespace
{
    static bool HistDetectVibrato(const float* hist, int count, float envPeak)
    {
        // 判定が甘いと装飾音やトレモロ・わずかな揺れまでビブラート扱いになるため、
        // 「十分長く・はっきりした周期的振動」のみを通すよう厳しめにする。
        if (count < 12 || envPeak < 0.06f) return false;
        float mean = 0.0f;
        for (int k = 0; k < count; ++k) mean += hist[k];
        mean /= (float)count;
        float minV = mean, maxV = mean;
        int reversals = 0;
        int prevSign = 0;
        // ノイズ床: 平均からの偏差がこの値未満は「揺れていない」とみなす。
        const float dead = 0.05f * envPeak;
        for (int k = 0; k < count; ++k) {
            const float v = hist[k];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            const float det = v - mean;
            const int sign = (det > dead) ? 1 : ((det < -dead) ? -1 : 0);
            if (sign != 0 && prevSign != 0 && sign != prevSign)
                ++reversals;
            if (sign != 0) prevSign = sign;
        }
        const float swing = maxV - minV;
        // 反転4回以上(=おおむね2周期以上)かつ十分な振幅のみビブラートと判定。
        return reversals >= 4 && swing >= envPeak * 0.18f;
    }
}

namespace PianoExpr {
    static constexpr uint8_t ACCENT = 0x01;
    static constexpr uint8_t SCOOP = 0x02;
    static constexpr uint8_t VIBRATO = 0x04;
    static constexpr uint8_t SLIDE = 0x08;
    static constexpr uint8_t FALL = 0x10;
    static constexpr uint8_t SUSTAIN = 0x20;
    static constexpr uint8_t CRESC = 0x40;   // クレッシェンド(持続音が膨らむ)
    static constexpr uint8_t DECRESC = 0x80;   // デクレッシェンド(持続音がしぼむ)
    static constexpr uint8_t ALL_MASK = ACCENT | SCOOP | VIBRATO | SLIDE | FALL | SUSTAIN | CRESC | DECRESC;
}

// UpdateNoteStates の直後に呼ばれ、各アクティブノートへ表現記号フラグを付与する。
// 検出ロジック概要:
//   SCOOP   … 直前フレームで隣のキーがアクティブだった(音程が下から上がってきた)
//   SLIDE   … 上下隣キーからの遷移
//   FALL    … 上隣キーからの遷移(下降消音)
//   ACCENT  … ノートオン直後(age <= 3)に強度が急上昇、または本モジュールの再アタック検出
//   SUSTAIN … 12フレーム以上継続(持続音・ストリングス)
//   VIBRATO … 強度の周期的変動を VIB_HIST_LEN 分の自己相関で検出
void CPianoRoll::DetectExpressions()
{
    using namespace Cfg;
    // C6 以上はスキャッターが多く、遷移記号を付けるとノイズになるので抑える。
    static constexpr int kExprHiStart = PianoRoll108::O5_HI; // 84 = C6

    for (int i = 1; i < KEY_COUNT; ++i) {
        if (m_scoopLatch[i] > 0) --m_scoopLatch[i];
        if (m_activeKeys[i - 1] && i < kExprHiStart)
            m_scoopLatch[i] = 5;
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        m_exprFlags[i] = 0;
        const bool wasActive = m_prevActiveKeys[i];
        const bool nowActive = m_activeKeys[i];
        const bool hiScatter = (i >= kExprHiStart);

        if (!nowActive) {
            m_noteAgeFrames[i] = 0;
            m_vibHistCount[i] = 0;
            continue;
        }

        if (!wasActive)
            m_noteAgeFrames[i] = 0;
        else if (m_noteAgeFrames[i] < 255)
            ++m_noteAgeFrames[i];

        if (!wasActive && !hiScatter) {
            if (i > 0 && m_scoopLatch[i] >= 2)
                m_exprFlags[i] |= PianoExpr::SCOOP;
            if (i > 0 && m_prevActiveKeys[i - 1])
                m_exprFlags[i] |= PianoExpr::SLIDE;
            if (i + 1 < KEY_COUNT && m_prevActiveKeys[i + 1]) {
                m_exprFlags[i] |= PianoExpr::SLIDE;
                m_exprFlags[i] |= PianoExpr::FALL;
            }
        }

        if (nowActive && m_noteAgeFrames[i] >= 12)
            m_exprFlags[i] |= PianoExpr::SUSTAIN;

        if (!hiScatter && m_noteAgeFrames[i] <= 3) {
            const float prev = m_prevNoteStrength[i];
            const float cur = m_noteStrength[i];
            if (cur - prev > 0.18f || (prev > 0.03f && (cur - prev) / prev > 0.28f))
                m_exprFlags[i] |= PianoExpr::ACCENT;
        }

        if (m_vibHistCount[i] < VIB_HIST_LEN)
            ++m_vibHistCount[i];
        for (int k = 0; k < VIB_HIST_LEN - 1; ++k)
            m_vibHist[i][k] = m_vibHist[i][k + 1];
        m_vibHist[i][VIB_HIST_LEN - 1] = m_noteStrength[i];

        if (m_noteAgeFrames[i] >= 10 && m_vibHistCount[i] >= 12) {
            const int n = min((int)m_vibHistCount[i], VIB_HIST_LEN);
            if (HistDetectVibrato(m_vibHist[i] + (VIB_HIST_LEN - n), n,
                m_envPeak[i] > 0.01f ? m_envPeak[i] : 1.0f))
                m_exprFlags[i] |= PianoExpr::VIBRATO;
        }

        // クレッシェンド/デクレッシェンド: 持続(SUSTAIN)かつビブラートでないとき、
        // 強度履歴の前半と後半の平均差から緩やかな増減を判定する。
        if ((m_exprFlags[i] & PianoExpr::SUSTAIN) && !(m_exprFlags[i] & PianoExpr::VIBRATO)
            && m_noteAgeFrames[i] >= 16 && m_vibHistCount[i] >= VIB_HIST_LEN) {
            const float* h = m_vibHist[i];
            const int half = VIB_HIST_LEN / 2;
            float a = 0.0f, b = 0.0f;
            for (int k = 0; k < half; ++k) a += h[k];
            for (int k = half; k < VIB_HIST_LEN; ++k) b += h[k];
            a /= (float)half; b /= (float)(VIB_HIST_LEN - half);
            const float pk = m_envPeak[i] > 0.01f ? m_envPeak[i] : 1.0f;
            const float diff = b - a;
            if (diff > pk * 0.28f) m_exprFlags[i] |= PianoExpr::CRESC;
            else if (diff < -pk * 0.28f && b > pk * 0.18f) m_exprFlags[i] |= PianoExpr::DECRESC;
        }
    }

    memcpy(m_prevActiveKeys, m_activeKeys, sizeof(m_activeKeys));
    memcpy(m_prevBandMask, m_bandMask, sizeof(m_bandMask));
    for (int i = 0; i < KEY_COUNT; ++i)
        m_prevNoteStrength[i] = m_noteStrength[i];
}

namespace {
    static constexpr COLORREF PIANO_CHROMA_KEY = RGB(20, 20, 20);
}

void CPianoRoll::MarkKeyVisualDirty()
{
    m_keyDirty = true;
    memcpy(m_keySnapActive, m_activeKeys, sizeof(m_keySnapActive));
    memcpy(m_keySnapBand, m_bandMask, sizeof(m_keySnapBand));
    memcpy(m_keySnapExpr, m_exprFlags, sizeof(m_keySnapExpr));
}

void CPianoRoll::InvalidateRegions(bool roll, bool key)
{
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return;
    if (!roll && !key) return;
    CRect cr;
    GetClientRect(&cr);
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && cr.Height() > capH)
        cr.top = capH;
    const int w = cr.Width();
    const int h = cr.Height();
    if (w <= 0 || h <= 0) return;

    int keyH = h * 20 / 100;
    if (keyH < 50) keyH = 50;
    if (keyH > 100) keyH = 100;
    // 簡易3D はクライアント全面が1枚のシーン(鍵盤帯を分けない)
    const int rollH = IsView3D() ? h : (h - keyH);
    if (rollH <= 0) return;

    if ((roll && key) || IsView3D()) {
        CCC_InvalidateRectMinusOverlay(m_hWnd, cr);
        return;
    }
    if (roll)
        CCC_InvalidateRectMinusOverlay(m_hWnd, CRect(cr.left, cr.top, cr.left + w, cr.top + rollH));
    if (key)
        InvalidateRect(CRect(cr.left, cr.top + rollH, cr.left + w, cr.bottom), FALSE);
}

void CPianoRoll::BuildLiveNoteFrame(NoteFrame& frame) const
{
    // 太さは帯域内の表示強度比（低音支配時に中高音バーが消えないよう帯域別に正規化）
    // m_rawStrengths は Goertzel(ロック外)が書くため、ここでは m_cs 下で更新される
    // m_noteStrength を使う（UI と publish のレースを避ける）。
    float bandRawMax[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!m_activeKeys[i]) continue;
        const int band = PianoRoll108::KeyBandIndex(i);
        if (m_noteStrength[i] > bandRawMax[band])
            bandRawMax[band] = m_noteStrength[i];
    }

    for (int i = 0; i < KEY_COUNT; ++i) {
        frame.active[i] = m_activeKeys[i];
        frame.strength[i] = m_noteStrength[i];
        frame.segment[i] = m_segmentId[i];
        frame.bandMask[i] = m_bandMask[i];
        frame.expr[i] = m_exprFlags[i];
        memcpy(frame.laneStrength[i], m_laneStrength[i], sizeof(frame.laneStrength[i]));
        frame.reattack[i] = m_activeKeys[i] && m_reattackMark[i];
        if (m_activeKeys[i]) {
            const int band = PianoRoll108::KeyBandIndex(i);
            const float ref = bandRawMax[band];
            float dyn = (ref > 1e-6f) ? (m_noteStrength[i] / ref) : 0.5f;
            if (dyn < 0.06f) dyn = 0.0f;
            if (dyn > 1.0f) dyn = 1.0f;
            frame.dynLevel[i] = dyn;
        }
        else {
            frame.dynLevel[i] = 0.0f;
        }
    }
}

void CPianoRoll::PushFrame(bool requestUiInvalidate)
{
    if (!m_feedEnabled || m_paintDisabled || m_frozen) return;
    m_historyHead = (m_historyHead + (int)MAX_HISTORY - 1) % (int)MAX_HISTORY;
    BuildLiveNoteFrame(m_historyRing[m_historyHead]);
    if (m_historyCount < (int)MAX_HISTORY)
        ++m_historyCount;
    // 保留フレームは上限を設ける。描画が解析に追いつかない(特にアクリル時)と
    // 無制限に溜まり、OnPaint の追い付き再描画ループが UI スレッドを占有して
    // 他処理(モード切替など)が数十秒固まる原因になる。上限で頭打ちにして
    // 追いつけない分は間引く(可視化なので体感への影響は小さい)。
    if (m_framesPending < 3)
        ++m_framesPending;

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (m_activeKeys[i] != m_keySnapActive[i] ||
            m_exprFlags[i] != m_keySnapExpr[i]) {
            MarkKeyVisualDirty();
            break;
        }
    }

    if (requestUiInvalidate)
        InvalidateRegions(true, false);
}

void CPianoRoll::PushDisplayFrames()
{
    // フリーズ中は履歴スクロールを止め、鍵盤/ライブ行だけ最新化する。
    if (m_frozen) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (m_activeKeys[i] != m_keySnapActive[i] ||
                m_bandMask[i] != m_keySnapBand[i] ||
                m_exprFlags[i] != m_keySnapExpr[i]) {
                MarkKeyVisualDirty();
                break;
            }
        }
        return;
    }
    // 表示速度: 解析ホップとは独立に、壁時計で約60行/秒×速度% を目標にする。
    // （旧: 解析1回=1行 → ANALYZE_MIN_MS=3 のとき理論333行/秒でワーカーだけ過負荷）
    int pct = m_rollSpeedPct;
    if (pct < 25) pct = 25;
    if (pct > 200) pct = 200;

    const DWORD now = GetTickCount();
    if (m_lastRollPushTick == 0)
        m_lastRollPushTick = now;
    DWORD dt = now - m_lastRollPushTick;
    if (dt > 80) dt = 80;
    m_lastRollPushTick = now;

    // credit = Σ(dt_ms * pct)。100%・16.67ms で約 1667 → 1行。
    m_rollSpeedCredit += (int)dt * pct;
    static constexpr int kCreditPerRow = 1667;
    int pushed = 0;
    while (m_rollSpeedCredit >= kCreditPerRow && pushed < 4) {
        m_rollSpeedCredit -= kCreditPerRow;
        PushFrame(false);
        ++pushed;
    }
}

void CPianoRoll::SetRollSpeedPct(int pct)
{
    if (pct < 25) pct = 25;
    if (pct > 200) pct = 200;
    if (m_rollSpeedPct == pct) return;
    m_rollSpeedPct = pct;
    savedata.pianorollscrollspeed = pct;
    m_rollSpeedCredit = 0;
    m_lastRollPushTick = 0;
}

int CPianoRoll::RollSpeedIndex() const
{
    int nearest = 3;
    int best = 100000;
    for (int i = 0; i < ROLL_SPEED_COUNT; ++i) {
        const int d = abs(kRollSpeedPct[i] - m_rollSpeedPct);
        if (d < best) { best = d; nearest = i; }
    }
    return nearest;
}

void CPianoRoll::OnRollSpeedCmd(UINT nID)
{
    const int idx = (int)nID - (int)IDM_ROLL_SPEED_BASE;
    if (idx < 0 || idx >= ROLL_SPEED_COUNT) return;
    SetRollSpeedPct(kRollSpeedPct[idx]);
}

void CPianoRoll::RollSpeedSliderCb(void* ctx, int value)
{
    CPianoRoll* p = (CPianoRoll*)ctx;
    if (!p || !::IsWindow(p->GetSafeHwnd())) return;
    p->SetRollSpeedPct(value);
}

void CPianoRoll::YawSliderCb(void* ctx, int value)
{
    CPianoRoll* p = (CPianoRoll*)ctx;
    if (!p || !::IsWindow(p->GetSafeHwnd())) return;
    float yaw = (float)value / 10.0f;
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    if (yaw == p->m_view3dYawDeg) return;
    p->m_view3dYawDeg = yaw;
    p->Save3DAngles();
    p->m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
    p->m_chromaReady = false;
#endif
    p->Invalidate(FALSE);
}

void CPianoRoll::PitchSliderCb(void* ctx, int value)
{
    CPianoRoll* p = (CPianoRoll*)ctx;
    if (!p || !::IsWindow(p->GetSafeHwnd())) return;
    float pitch = (float)value / 10.0f;
    if (pitch < kView3dPitchMin) pitch = kView3dPitchMin;
    if (pitch > kView3dPitchMax) pitch = kView3dPitchMax;
    if (pitch == p->m_view3dPitchDeg) return;
    p->m_view3dPitchDeg = pitch;
    p->Save3DAngles();
    p->m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
    p->m_chromaReady = false;
#endif
    p->Invalidate(FALSE);
}

void CPianoRoll::ZoomSliderCb(void* ctx, int value)
{
    CPianoRoll* p = (CPianoRoll*)ctx;
    if (!p || !::IsWindow(p->GetSafeHwnd())) return;
    float zoom = (float)value / 100.0f;
    if (zoom < kView3dZoomMin) zoom = kView3dZoomMin;
    if (zoom > kView3dZoomMax) zoom = kView3dZoomMax;
    if (zoom == p->m_view3dZoom) return;
    p->m_view3dZoom = zoom;
    p->Save3DAngles();
    p->m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
    p->m_chromaReady = false;
#endif
    p->Invalidate(FALSE);
}

void CPianoRoll::RequestFullRollRedraw()
{
    m_historyDirty = true;
    m_rollScrollValid = false;
    m_framesPending = 0;
    m_keyDirty = true;
    if (::IsWindow(m_hWnd))
        InvalidateRegions(true, true);
}

void CPianoRoll::ClearRollHistory()
{
    m_historyCount = 0;
    m_historyHead = 0;
    m_rollSpeedCredit = 0;
    m_lastRollPushTick = 0;
    m_framesPending = 0;
    for (int hi = 0; hi < (int)MAX_HISTORY; ++hi) {
        auto& f = m_historyRing[hi];
        memset(f.active, 0, sizeof(f.active));
        memset(f.strength, 0, sizeof(f.strength));
        memset(f.segment, 0, sizeof(f.segment));
        memset(f.bandMask, 0, sizeof(f.bandMask));
        memset(f.laneStrength, 0, sizeof(f.laneStrength));
        memset(f.expr, 0, sizeof(f.expr));
        memset(f.dynLevel, 0, sizeof(f.dynLevel));
        memset(f.reattack, 0, sizeof(f.reattack));
    }
    RequestFullRollRedraw();
}

void CPianoRoll::OnToggleFreeze()
{
    m_frozen = !m_frozen;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnClearDisplay()
{
    ClearRollHistory();
}

void CPianoRoll::OnToggleExprLegend()
{
    m_showExprLegend = !m_showExprLegend;
    savedata.pianorollexprlegend = m_showExprLegend ? 1 : 0;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnToggleExprMarks()
{
    m_showExprMarks = !m_showExprMarks;
    savedata.pianorollexprmarks = m_showExprMarks ? 1 : 0;
    RequestFullRollRedraw();
}

void CPianoRoll::OnToggleLevelMeter()
{
    m_showLevelMeter = !m_showLevelMeter;
    savedata.pianorolllevelmeter = m_showLevelMeter ? 1 : 0;
    m_keyDirty = true;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnToggleAlwaysOnTop()
{
    m_alwaysOnTop = !m_alwaysOnTop;
    savedata.pianorolltopmost = m_alwaysOnTop ? 1 : 0;
    if (::IsWindow(m_hWnd)) {
        SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void CPianoRoll::OnToggleReattackDetect()
{
    m_reattackDetectEnabled = !m_reattackDetectEnabled;
    savedata.pianorollreattack = m_reattackDetectEnabled ? 1 : 0;
}

void CPianoRoll::OnToggleImpulsiveGhost()
{
    m_impulsiveGhostSuppressEnabled = !m_impulsiveGhostSuppressEnabled;
    savedata.pianorollimpulse = m_impulsiveGhostSuppressEnabled ? 1 : 0;
}

void CPianoRoll::OnToggleHarmonicGhost()
{
    m_harmonicGhostGuardEnabled = !m_harmonicGhostGuardEnabled;
    savedata.pianorollharmghost = m_harmonicGhostGuardEnabled ? 1 : 0;
}

void CPianoRoll::OnToggleHarmonicProfile()
{
    m_harmonicProfileGuardEnabled = !m_harmonicProfileGuardEnabled;
    savedata.pianorollharmprof = m_harmonicProfileGuardEnabled ? 1 : 0;
}

void CPianoRoll::OnOpenTuneDialog()
{
    COggDlg_ShowPianoRollTune();
}

// 表示モード/鍵盤レンジ/ノート名の切替は、レイアウトが変わるためバッファを
// 作り直す必要がある。アクリル(クロマキー)経路も下地が変わるので m_chromaReady を
// 落として次の OnPaint で全面を貼り直させる(アナライザーと同じ手順)。
void CPianoRoll::ApplyViewChangeRedraw()
{
    ReleasePaintBuffers();   // クロマキャッシュ/フォントもここで解放される
    RequestFullRollRedraw();
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::SetViewMode(int mode)
{
    const int m = (mode == 1) ? 1 : 0;
    if (m == m_viewMode) return;
    m_viewMode = m;
    savedata.pianorollviewmode = m;
    if (m == 1) {
        if (!(savedata.soft3dTourSeen & 8)) {
            savedata.soft3dTourSeen |= 8;
            m_soft3dTourUntil = GetTickCount() + 3000;
        }
    }
    if (m_rotDragging) {
        m_rotDragging = false;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
    }
    ApplyViewChangeRedraw();
}

void CPianoRoll::SetKeyRange(int keys)
{
    const int k = (keys == 88) ? 88 : 108;
    if (k == m_keyRange) return;
    m_keyRange = k;
    savedata.pianorollkeyrange = k;
    ApplyViewChangeRedraw();
}

void CPianoRoll::ToggleNoteNames()
{
    m_showNoteNames = !m_showNoteNames;
    savedata.pianorollnotename = m_showNoteNames ? 1 : 0;
    m_keyDirty = true;
    if (IsView3D())
        m_historyDirty = true;
    if (::IsWindow(m_hWnd))
        InvalidateRegions(IsView3D(), true);
}

void CPianoRoll::Save3DAngles()
{
    float yaw = m_view3dYawDeg;
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    m_view3dYawDeg = yaw;
    savedata.pianoroll3dyaw = (int)(yaw * 10.0f);
    savedata.pianoroll3dpitch = (int)(m_view3dPitchDeg * 10.0f);
    int z = (int)(m_view3dZoom * 100.0f + 0.5f);
    if (z < 35) z = 35;
    if (z > 400) z = 400;
    savedata.pianoroll3dzoom = z;
}

void CPianoRoll::OnViewModeCmd(UINT nID)
{
    SetViewMode((int)(nID - IDM_ROLL_VIEW_BASE));
}

void CPianoRoll::OnCamResetCmd()
{
    m_view3dYawDeg = -22.0f;
    m_view3dPitchDeg = 26.0f;
    m_view3dZoom = 1.0f;
    savedata.pianoroll3dyaw = -220;
    savedata.pianoroll3dpitch = 260;
    savedata.pianoroll3dzoom = 100;
    m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
    m_chromaReady = false;
#endif
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CPianoRoll::OnKeyRangeCmd(UINT nID)
{
    SetKeyRange((nID == IDM_ROLL_KEYS_BASE) ? 88 : 108);
}

void CPianoRoll::OnToggleNoteNames()
{
    ToggleNoteNames();
}

// ---- recovered addon implementations ----

void CPianoRoll::FeedLoopbackMono(const double* mono, int frameCount, int sampleRate)
{
    // PC音はWASAPIの短いパケットで来る。MinAnalyze(~8192@44.1k)未満だと
    // AnalyzePlayCursorMono が即 return し、メーターだけ一瞬動いてロールが流れない。
    if (!m_feedEnabled && !savedata.mpLoopbackScore) return;
    if (!mono || frameCount <= 0) return;
    if (sampleRate < 8000) sampleRate = 48000;
    if (sampleRate != m_loopbackAccumRate) {
        m_loopbackAccumN = 0;
        m_loopbackAccumRate = sampleRate;
    }
    for (int i = 0; i < frameCount; ++i) {
        if (m_loopbackAccumN >= LOOPBACK_ACCUM_MAX) {
            const int keep = LOOPBACK_ACCUM_MAX / 2;
            memmove(m_loopbackAccum, m_loopbackAccum + (LOOPBACK_ACCUM_MAX - keep),
                (size_t)keep * sizeof(double));
            m_loopbackAccumN = keep;
        }
        m_loopbackAccum[m_loopbackAccumN++] = mono[i];
    }
    const int needN = MinAnalyzeFrameCount(sampleRate, LOOPBACK_ACCUM_MAX);
    if (needN <= 0 || m_loopbackAccumN < needN) return;
    int use = CaptureFrameCount(sampleRate, m_loopbackAccumN);
    if (use < needN) use = needN;
    if (use > m_loopbackAccumN) use = m_loopbackAccumN;
    AnalyzePlayCursorMono(m_loopbackAccum + (m_loopbackAccumN - use), use, sampleRate);
}

void CPianoRoll::ResetScoreCaptureLocked()
{
    m_scoreCapEvN = 0;
    m_scoreCapPendingDelta = 0;
    m_scoreCapFrameN = 0;
    memset(m_scoreCapPrevActive, 0, sizeof(m_scoreCapPrevActive));
}

void CPianoRoll::AppendScoreCaptureLocked()
{
    if (!m_scoreCapMidi && !m_scoreCapXml)
        return;
    if (m_scoreCapFrameN > 0)
        m_scoreCapPendingDelta += SCORE_TICKS_PER_FRAME;

    for (int k = 0; k < KEY_COUNT; ++k) {
        const bool on = m_activeKeys[k];
        if (on == m_scoreCapPrevActive[k])
            continue;
        if (m_scoreCapEvN >= SCORE_CAP_EV_MAX)
            break;
        int vel = (int)(m_noteStrength[k] * 100.0f);
        if (vel < 1) vel = 1;
        if (vel > 127) vel = 127;
        ScoreCapEv& e = m_scoreCapEv[m_scoreCapEvN++];
        e.deltaTicks = m_scoreCapPendingDelta;
        e.status = on ? (BYTE)0x90 : (BYTE)0x80;
        e.note = (BYTE)k;
        e.vel = on ? (BYTE)vel : (BYTE)0x40;
        m_scoreCapPendingDelta = 0;
        m_scoreCapPrevActive[k] = on;
    }

    if (m_scoreCapFrameN < SCORE_CAP_FRAME_MAX) {
        uint8_t* bits = m_scoreCapFrames[m_scoreCapFrameN];
        memset(bits, 0, (KEY_COUNT + 7) / 8);
        for (int k = 0; k < KEY_COUNT; ++k) {
            if (m_activeKeys[k])
                bits[k >> 3] |= (uint8_t)(1u << (k & 7));
        }
        m_scoreCapFrameN++;
    }
}

void CPianoRoll::SaveCapturedMidi()
{
    ScoreCapEv ev[SCORE_CAP_EV_MAX];
    int evN = 0;
    EnterCriticalSection(&m_cs);
    evN = m_scoreCapEvN;
    if (evN > SCORE_CAP_EV_MAX) evN = SCORE_CAP_EV_MAX;
    if (evN > 0)
        memcpy(ev, m_scoreCapEv, (size_t)evN * sizeof(ScoreCapEv));
    int pending = m_scoreCapPendingDelta;
    bool prev[KEY_COUNT];
    memcpy(prev, m_scoreCapPrevActive, sizeof(prev));
    LeaveCriticalSection(&m_cs);

    for (int k = 0; k < KEY_COUNT; ++k) {
        if (!prev[k]) continue;
        if (evN >= SCORE_CAP_EV_MAX) break;
        ScoreCapEv& e = ev[evN++];
        e.deltaTicks = pending;
        e.status = 0x80;
        e.note = (BYTE)k;
        e.vel = 0x40;
        pending = 0;
    }

    if (evN < 1) {
        MessageBox(LL14(L"録ったノートがありません。再生(またはPC音譜面化)しながらチェックを入れてください。", L"No notes captured. Check the item while playing (or PC-audio score).", L"Aucune note. Cochez pendant la lecture.", L"Nessuna nota. Spunta durante la riproduzione.", L"Sin notas. Marque durante la reproduccion.", L"녹음된 음이 없습니다. 재생 중 체크하세요.", L"没有录到音符。请在播放时勾选。", L"لا نغمات. فعّل أثناء التشغيل.", L"Нет нот. Включите во время воспроизведения.", L"Keine Noten. Wahrend Wiedergabe aktivieren.", L"Sem notas. Marque durante a reproducao.", L"Geen noten. Vink aan tijdens afspelen.", L"Brak nut. Zaznacz podczas odtwarzania.", L"Nota yok. Calarken isaretleyin."),
            LL14(L"MIDI録り", L"MIDI capture", L"Enregistrement MIDI", L"Registrazione MIDI", L"Captura MIDI", L"MIDI 녹음", L"MIDI录制", L"تسجيل MIDI", L"Запись MIDI", L"MIDI-Aufnahme", L"Captura MIDI", L"MIDI-opname", L"Zapis MIDI", L"MIDI kayit"),
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    BYTE track[65536];
    int trackLen = 0;
    auto putVlq = [&](int v) {
        if (v < 0) v = 0;
        BYTE stack[5];
        int n = 0;
        stack[n++] = (BYTE)(v & 0x7f);
        while ((v >>= 7) > 0 && n < 5) {
            stack[n] = (BYTE)((v & 0x7f) | 0x80);
            n++;
        }
        for (int i = n - 1; i >= 0 && trackLen < (int)sizeof(track) - 1; --i)
            track[trackLen++] = stack[i];
    };
    for (int i = 0; i < evN; ++i) {
        putVlq(ev[i].deltaTicks);
        if (trackLen + 3 < (int)sizeof(track)) {
            track[trackLen++] = ev[i].status;
            track[trackLen++] = ev[i].note;
            track[trackLen++] = ev[i].vel;
        }
    }
    putVlq(0);
    if (trackLen + 3 < (int)sizeof(track)) {
        track[trackLen++] = 0xFF;
        track[trackLen++] = 0x2F;
        track[trackLen++] = 0x00;
    }

    CFileDialog dlg(FALSE, _T("mid"), _T("pianoroll.mid"),
        OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
        LL14(L"MIDI (*.mid)|*.mid|すべて (*.*)|*.*||", L"MIDI (*.mid)|*.mid|All (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Tous (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Tutti (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Todos (*.*)|*.*||", L"MIDI (*.mid)|*.mid|모두 (*.*)|*.*||", L"MIDI (*.mid)|*.mid|全部 (*.*)|*.*||", L"MIDI (*.mid)|*.mid|الكل (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Все (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Alle (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Todos (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Alles (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Wszystkie (*.*)|*.*||", L"MIDI (*.mid)|*.mid|Tumu (*.*)|*.*||"),
        this);
    if (dlg.DoModal() != IDOK) return;
    CFile f;
    if (!f.Open(dlg.GetPathName(), CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) return;
    const int tpq = SCORE_TPQ;
    BYTE hdr[14] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, (BYTE)(tpq >> 8), (BYTE)(tpq & 0xff)
    };
    f.Write(hdr, 14);
    BYTE th[8] = { 'M','T','r','k', 0,0,0,0 };
    th[4] = (BYTE)((trackLen >> 24) & 0xff);
    th[5] = (BYTE)((trackLen >> 16) & 0xff);
    th[6] = (BYTE)((trackLen >> 8) & 0xff);
    th[7] = (BYTE)(trackLen & 0xff);
    f.Write(th, 8);
    f.Write(track, trackLen);
    f.Close();
}

void CPianoRoll::SaveCapturedMusicXml()
{
    uint8_t frames[SCORE_CAP_FRAME_MAX][(KEY_COUNT + 7) / 8];
    int frameN = 0;
    EnterCriticalSection(&m_cs);
    frameN = m_scoreCapFrameN;
    if (frameN > SCORE_CAP_FRAME_MAX) frameN = SCORE_CAP_FRAME_MAX;
    if (frameN > 0)
        memcpy(frames, m_scoreCapFrames, (size_t)frameN * ((KEY_COUNT + 7) / 8));
    LeaveCriticalSection(&m_cs);

    if (frameN < 2) {
        MessageBox(LL14(L"録った譜面が足りません。再生(またはPC音譜面化)しながらチェックを入れてください。", L"Not enough score captured. Check while playing (or PC-audio score).", L"Partition insuffisante. Cochez pendant la lecture.", L"Partitura insufficiente. Spunta durante la riproduzione.", L"Partitura insuficiente. Marque durante la reproduccion.", L"녹음된 악보가 부족합니다. 재생 중 체크하세요.", L"录到的谱面不足。请在播放时勾选。", L"النوتة غير كافية. فعّل أثناء التشغيل.", L"Недостаточно партитуры. Включите во время воспроизведения.", L"Zu wenig Partitur. Wahrend Wiedergabe aktivieren.", L"Partitura insuficiente. Marque durante a reproducao.", L"Te weinig partituur. Vink aan tijdens afspelen.", L"Za malo partytury. Zaznacz podczas odtwarzania.", L"Parti yetersiz. Calarken isaretleyin."),
            LL14(L"MusicXML録り", L"MusicXML capture", L"Enregistrement MusicXML", L"Registrazione MusicXML", L"Captura MusicXML", L"MusicXML 녹음", L"MusicXML录制", L"تسجيل MusicXML", L"Запись MusicXML", L"MusicXML-Aufnahme", L"Captura MusicXML", L"MusicXML-opname", L"Zapis MusicXML", L"MusicXML kayit"),
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    CFileDialog dlg(FALSE, _T("musicxml"), _T("pianoroll.musicxml"),
        OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
        LL14(L"MusicXML (*.musicxml)|*.musicxml|すべて (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|All (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Tous (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Tutti (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Todos (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|모두 (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|全部 (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|الكل (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Все (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Alle (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Todos (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Alles (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Wszystkie (*.*)|*.*||", L"MusicXML (*.musicxml)|*.musicxml|Tumu (*.*)|*.*||"),
        this);
    if (dlg.DoModal() != IDOK) return;

    static const char* kStep[12] = { "C","C","D","D","E","F","F","G","G","A","A","B" };
    static const int kAlter[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
    CStringA xml;
    xml.Preallocate((frameN > 0 ? frameN : 1) * 512);
    auto append = [&](const char* s) {
        if (s && *s) xml += s;
    };
    append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    append("<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n");
    append("<score-partwise version=\"3.1\"><part-list><score-part id=\"P1\"><part-name>PianoRoll</part-name></score-part></part-list><part id=\"P1\">\n");

    int measure = 1;
    int divisionsInMeasure = 0;
    append("<measure number=\"1\"><attributes><divisions>1</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>G</sign><line>2</line></clef></attributes>\n");

    for (int fi = 0; fi < frameN; ++fi) {
        const uint8_t* bits = frames[fi];
        int written = 0;
        for (int k = 21; k < KEY_COUNT && written < 8; ++k) {
            if ((bits[k >> 3] & (uint8_t)(1u << (k & 7))) == 0)
                continue;
            const int pc = k % 12;
            const int oct = (k / 12) - 1;
            char nb[320];
            const char* alter = "";
            if (kAlter[pc] > 0) alter = "<alter>1</alter>";
            else if (kAlter[pc] < 0) alter = "<alter>-1</alter>";
            if (written == 0)
                sprintf_s(nb, "<note><pitch><step>%s</step>%s<octave>%d</octave></pitch><duration>1</duration><type>quarter</type></note>\n",
                    kStep[pc], alter, oct);
            else
                sprintf_s(nb, "<note><chord/><pitch><step>%s</step>%s<octave>%d</octave></pitch><duration>1</duration><type>quarter</type></note>\n",
                    kStep[pc], alter, oct);
            append(nb);
            written++;
        }
        if (written == 0)
            append("<note><rest/><duration>1</duration><type>quarter</type></note>\n");
        divisionsInMeasure++;
        if (divisionsInMeasure >= 4) {
            append("</measure>\n");
            measure++;
            divisionsInMeasure = 0;
            if (measure > 128)
                break;
            char mb[64];
            sprintf_s(mb, "<measure number=\"%d\">\n", measure);
            append(mb);
        }
    }
    if (divisionsInMeasure > 0)
        append("</measure>\n");
    append("</part></score-partwise>\n");

    CFile f;
    if (!f.Open(dlg.GetPathName(), CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
        MessageBox(LL14(L"ファイルを書けませんでした。", L"Could not write file.", L"Impossible d'ecrire le fichier.", L"Impossibile scrivere il file.", L"No se pudo escribir el archivo.", L"파일을 쓸 수 없습니다.", L"无法写入文件。", L"تعذر كتابة الملف.", L"Не удалось записать файл.", L"Datei konnte nicht geschrieben werden.", L"Nao foi possivel gravar o arquivo.", L"Kon bestand niet schrijven.", L"Nie udalo sie zapisac pliku.", L"Dosya yazilamadi."),
            LL14(L"MusicXML録り", L"MusicXML capture", L"Enregistrement MusicXML", L"Registrazione MusicXML", L"Captura MusicXML", L"MusicXML 녹음", L"MusicXML录制", L"تسجيل MusicXML", L"Запись MusicXML", L"MusicXML-Aufnahme", L"Captura MusicXML", L"MusicXML-opname", L"Zapis MusicXML", L"MusicXML kayit"),
            MB_OK | MB_ICONWARNING);
        return;
    }
    f.Write((LPCSTR)xml, (UINT)xml.GetLength());
    f.Close();
}

void CPianoRoll::OnToggleChordPanel()
{
    savedata.mpChordPanel = savedata.mpChordPanel ? 0 : 1;
    if (savedata.mpChordPanel) {
        m_chordHistCount = 0;
        m_chordHistHead = 0;
        m_chordLast[0] = 0;
    }
#if CCUSTOM_AERO_SUPPORT
    m_chromaReady = false;
#endif
    Invalidate(FALSE);
}

void CPianoRoll::OnToggleLoopbackScore()
{
    // 譜面録り中は PC 音を切れない（連動維持）
    if (savedata.mpLoopbackScore && (m_scoreCapMidi || m_scoreCapXml)) {
        MessageBox(LL14(L"MIDI/MusicXML録り中はPC音譜面化をオフにできません。先に録りを終えてください。", L"Cannot turn off PC-audio score while MIDI/MusicXML capture is on. Finish capture first.", L"Impossible de desactiver l'audio PC pendant l'enregistrement. Terminez d'abord.", L"Impossibile disattivare audio PC durante la registrazione. Termina prima.", L"No se puede desactivar audio PC durante la captura. Termine primero.", L"MIDI/MusicXML 녹음 중에는 PC 소리 악보화를 끌 수 없습니다. 먼저 녹음을 끝내세요.", L"MIDI/MusicXML录制中无法关闭PC声音成谱。请先结束录制。", L"لا يمكن إيقاف صوت الجهاز أثناء التسجيل. أنهِ التسجيل أولاً.", L"Нельзя выключить звук ПК во время записи. Сначала завершите запись.", L"PC-Audio-Partitur kann wahrend Aufnahme nicht aus. Zuerst Aufnahme beenden.", L"Nao e possivel desligar audio do PC durante a captura. Finalize antes.", L"Pc-audio kan niet uit tijdens opname. Beëindig eerst de opname.", L"Nie mozna wylaczyc dzwieku PC podczas zapisu. Najpierw zakoncz zapis.", L"Kayit sirasinda PC sesi kapatilamaz. Once kaydi bitirin."),
            LL14(L"PC音を譜面化", L"Score from PC audio", L"Partition depuis le PC", L"Partitura da audio PC", L"Partitura desde audio PC", L"PC 소리로 악보화", L"从PC声音成谱", L"تدوين من صوت الجهاز", L"Ноты с ПК-звука", L"Partitur aus PC-Audio", L"Partitura do audio do PC", L"Partituur van pc-audio", L"Partytura z dzwieku PC", L"PC sesinden parti"),
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    savedata.mpLoopbackScore = savedata.mpLoopbackScore ? 0 : 1;
    if (savedata.mpLoopbackScore) {
        MpPcAudioMarkUserOwned();
        ResumePlaybackFeed();
        extern void EnsureDeviceRecordLoopbackFeed(CWnd* parent);
        CWnd* parent = CCC_GetActiveMainWindow();
        if (!parent) parent = this;
        EnsureDeviceRecordLoopbackFeed(parent);
        // 初回は操作が分かるよう録音ダイアログも出す
        extern void OpenDeviceRecordModeless(CWnd* parent);
        OpenDeviceRecordModeless(parent);
    }
    else {
        MpPcAudioMarkUserOwned();
        extern void StopDeviceRecordLoopbackFeed();
        StopDeviceRecordLoopbackFeed();
        extern int playf;
        if (!playf)
            PauseAnalysis();
    }
}


void CPianoRoll::OnToggleCaptureMidi()
{
    const bool wasAny = m_scoreCapMidi || m_scoreCapXml;
    if (!m_scoreCapMidi) {
        EnterCriticalSection(&m_cs);
        if (!wasAny)
            ResetScoreCaptureLocked();
        m_scoreCapMidi = true;
        LeaveCriticalSection(&m_cs);
        HoldPcAudioForScoreCapture();
        Invalidate(FALSE);
        return;
    }
    EnterCriticalSection(&m_cs);
    m_scoreCapMidi = false;
    LeaveCriticalSection(&m_cs);
    SaveCapturedMidi();
    EnterCriticalSection(&m_cs);
    if (!m_scoreCapMidi && !m_scoreCapXml)
        ResetScoreCaptureLocked();
    LeaveCriticalSection(&m_cs);
    ReleasePcAudioForScoreCaptureIfHeld();
    Invalidate(FALSE);
}

void CPianoRoll::OnToggleCaptureMusicXml()
{
    const bool wasAny = m_scoreCapMidi || m_scoreCapXml;
    if (!m_scoreCapXml) {
        EnterCriticalSection(&m_cs);
        if (!wasAny)
            ResetScoreCaptureLocked();
        m_scoreCapXml = true;
        LeaveCriticalSection(&m_cs);
        HoldPcAudioForScoreCapture();
        Invalidate(FALSE);
        return;
    }
    EnterCriticalSection(&m_cs);
    m_scoreCapXml = false;
    LeaveCriticalSection(&m_cs);
    SaveCapturedMusicXml();
    EnterCriticalSection(&m_cs);
    if (!m_scoreCapMidi && !m_scoreCapXml)
        ResetScoreCaptureLocked();
    LeaveCriticalSection(&m_cs);
    ReleasePcAudioForScoreCaptureIfHeld();
    Invalidate(FALSE);
}


void CPianoRoll::OnPlayerFeedStopping(bool fullReset)
{
    if (savedata.mpLoopbackScore || m_scoreCapMidi || m_scoreCapXml) {
        if (!savedata.mpLoopbackScore && (m_scoreCapMidi || m_scoreCapXml) && !m_scoreCapHeldPcAudio) {
            MpPcAudioRetain();
            m_scoreCapHeldPcAudio = true;
        }
        ResumePlaybackFeed();
        return;
    }
    if (fullReset)
        ResetPlaybackState();
    else
        PauseAnalysis();
}

void CPianoRoll::HoldPcAudioForScoreCapture()
{
    extern int playf;
    if (m_scoreCapHeldPcAudio)
        return;
    // ローカル再生中は既存フィードで足りる。停止中のみ PC 音を確保。
    if (playf)
        return;
    MpPcAudioRetain();
    m_scoreCapHeldPcAudio = true;
}

void CPianoRoll::ReleasePcAudioForScoreCaptureIfHeld()
{
    if (!m_scoreCapHeldPcAudio)
        return;
    if (m_scoreCapMidi || m_scoreCapXml)
        return;
    MpPcAudioRelease();
    m_scoreCapHeldPcAudio = false;
}


void CPianoRoll::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
    CCustomPopupMenu menu;

    CCustomPopupMenu* subView = menu.AddSubMenu(
        LL14(L"表示モード", L"View mode", L"Mode d'affichage", L"Modalita di visualizzazione", L"Modo de visualizacion", L"표시 모드", L"显示模式", L"وضع العرض", L"Режим отображения", L"Anzeigemodus", L"Modo de exibicao", L"Weergavemodus", L"Tryb wyswietlania", L"Goruntuleme modu"),
        LL14(L"ロールの表示モード（通常2D／簡易3D）を選びます。", L"Choose roll view mode (normal 2D / soft 3D).", L"Choisir le mode d'affichage (2D normal / 3D simplifie).", L"Scegli la modalita di vista (2D normale / 3D semplificato).", L"Elegir el modo de vista (2D normal / 3D simple).", L"롤 표시 모드(일반 2D/간이 3D)를 고릅니다.", L"选择卷帘显示模式（普通2D/简易3D）。", L"اختر وضع عرض اللفة (2D عادي / 3D مبسط).", L"Выбрать режим отображения ролла (обычный 2D / простой 3D).", L"Anzeigemodus der Rolle wahlen (normal 2D / einfaches 3D).", L"Escolher o modo de vista do roll (2D normal / 3D simples).", L"Kies weergavemodus van de roll (normaal 2D / eenvoudig 3D).", L"Wybierz tryb widoku rolki (zwykly 2D / uproszczone 3D).", L"Roll goruntuleme modunu sec (normal 2D / basit 3B)."));
    if (subView) {
        subView->AddCheck(IDM_ROLL_VIEW_BASE + 0,
            LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"),
            m_viewMode == 0);
        subView->AddCheck(IDM_ROLL_VIEW_BASE + 1,
            LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"ثلاثي الأبعاد مبسط", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"),
            m_viewMode == 1);
        if (IsView3D()) {
            int yaw10 = (int)(m_view3dYawDeg * 10.0f);
            if (yaw10 < -1800) yaw10 = -1800;
            if (yaw10 > 1800) yaw10 = 1800;
            int pit10 = (int)(m_view3dPitchDeg * 10.0f);
            int pmin = (int)(kView3dPitchMin * 10.0f);
            int pmax = (int)(kView3dPitchMax * 10.0f);
            if (pit10 < pmin) pit10 = pmin;
            if (pit10 > pmax) pit10 = pmax;
            int zoomPct = (int)(m_view3dZoom * 100.0f + 0.5f);
            int zmin = (int)(kView3dZoomMin * 100.0f + 0.5f);
            int zmax = (int)(kView3dZoomMax * 100.0f + 0.5f);
            if (zoomPct < zmin) zoomPct = zmin;
            if (zoomPct > zmax) zoomPct = zmax;
            subView->AddSeparator();
            subView->AddSlider(
                LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)",
                    L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)",
                    L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
                -1800, 1800, yaw10, &CPianoRoll::YawSliderCb, this,
                LL14(L"水平回転（ドラッグ中に反映）", L"Horizontal rotation (live)", L"Rotation horizontale (direct)", L"Rotazione orizzontale (live)", L"Rotacion horizontal (en vivo)",
                    L"수평 회전(즉시)", L"水平旋转（即时）", L"دوران أفقي (مباشر)", L"Горизонтальный поворот (сразу)", L"Horizontale Drehung (live)",
                    L"Rotacao horizontal (ao vivo)", L"Horizontale rotatie (live)", L"Obrot poziomy (na zywo)", L"Yatay donus (anlik)"));
            subView->AddSlider(
                LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)",
                    L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)",
                    L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
                pmin, pmax, pit10, &CPianoRoll::PitchSliderCb, this,
                LL14(L"仰角（ドラッグ中に反映）", L"Elevation angle (live)", L"Angle d'elevation (direct)", L"Angolo di elevazione (live)", L"Angulo de elevacion (en vivo)",
                    L"앙각(즉시)", L"仰角（即时）", L"زاوية الارتفاع (مباشر)", L"Угол наклона (сразу)", L"Neigungswinkel (live)",
                    L"Angulo de elevacao (ao vivo)", L"Elevatiehoek (live)", L"Kat nachylenia (na zywo)", L"Yukselis acisi (anlik)"));
            subView->AddSlider(
                LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)",
                    L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)",
                    L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
                zmin, zmax, zoomPct, &CPianoRoll::ZoomSliderCb, this,
                LL14(L"拡大縮小（ドラッグ中に反映）", L"Zoom (live)", L"Zoom (direct)", L"Zoom (live)", L"Zoom (en vivo)",
                    L"확대/축소(즉시)", L"缩放（即时）", L"تكبير (مباشر)", L"Масштаб (сразу)", L"Zoom (live)",
                    L"Zoom (ao vivo)", L"Zoom (live)", L"Powiększenie (na zywo)", L"Yakinlastirma (anlik)"));
            subView->AddSeparator();
            subView->AddCommand(IDM_ROLL_CAM_RESET,
                LL14(L"視点をリセット", L"Reset view", L"Reinitialiser la vue", L"Reimposta vista", L"Restablecer vista",
                    L"시점 재설정", L"重置视角", L"إعادة ضبط العرض", L"Сбросить вид", L"Ansicht zurucksetzen",
                    L"Redefinir vista", L"Weergave resetten", L"Resetuj widok", L"Gorunumu sifirla"),
                LL14(L"カメラを既定の視点（Yaw/Pitch/Zoom）に戻します。", L"Restore the default camera (yaw / pitch / zoom).", L"Retablir la camera par defaut (lacet / tangage / zoom).", L"Ripristina la camera predefinita (yaw / pitch / zoom).", L"Restaurar la camara predeterminada (yaw / pitch / zoom).",
                    L"기본 카메라(요/피치/줌)로 되돌립니다.", L"将相机恢复为默认视角（偏航/俯仰/缩放）。", L"استعادة الكاميرا الافتراضية (الانحراف / الميل / التكبير).", L"Вернуть камеру к значениям по умолчанию (yaw / pitch / zoom).", L"Kamera auf Standardansicht (Yaw / Pitch / Zoom) zurucksetzen.",
                    L"Restaurar a camera padrao (yaw / pitch / zoom).", L"Herstel de standaardcamera (yaw / pitch / zoom).", L"Przywroc domyslna kamere (yaw / pitch / zoom).", L"Kamerayi varsayilan gorunume (yaw / pitch / zoom) dondur."));
        }
    }

    CCustomPopupMenu* subKeys = menu.AddSubMenu(
        LL14(L"鍵盤レンジ", L"Keyboard range", L"Etendue du clavier", L"Estensione tastiera", L"Rango del teclado", L"건반 범위", L"键盘范围", L"مدى لوحة المفاتيح", L"Диапазон клавиатуры", L"Tastaturumfang", L"Faixa do teclado", L"Toetsenbereik", L"Zakres klawiatury", L"Klavye araligi"),
        LL14(L"表示する鍵盤の範囲（88鍵／108鍵）を選びます。", L"Choose the visible keyboard range (88 / 108 keys).", L"Choisir l'etendue du clavier affichee (88 / 108 touches).", L"Scegli l'estensione tastiera visibile (88 / 108 tasti).", L"Elegir el rango de teclado visible (88 / 108 teclas).", L"표시할 건반 범위(88건/108건)를 고릅니다.", L"选择可见键盘范围（88键/108键）。", L"اختر مدى لوحة المفاتيح الظاهر (88 / 108 مفتاحا).", L"Выбрать видимый диапазон клавиатуры (88 / 108 клавиш).", L"Sichtbaren Tastaturumfang wahlen (88 / 108 Tasten).", L"Escolher a faixa de teclado visivel (88 / 108 teclas).", L"Kies zichtbaar toetsenbereik (88 / 108 toetsen).", L"Wybierz widoczny zakres klawiatury (88 / 108 klawiszy).", L"Gorunen klavye araligini sec (88 / 108 tus)."));
    if (subKeys) {
        subKeys->AddCheck(IDM_ROLL_KEYS_BASE + 0,
            LL14(L"88鍵 (A0～)", L"88 keys (A0-)", L"88 touches (A0-)", L"88 tasti (A0-)", L"88 teclas (A0-)", L"88건반 (A0-)", L"88键 (A0-)", L"88 مفتاحا (A0-)", L"88 клавиш (A0-)", L"88 Tasten (A0-)", L"88 teclas (A0-)", L"88 toetsen (A0-)", L"88 klawiszy (A0-)", L"88 tus (A0-)"),
            m_keyRange == 88);
        subKeys->AddCheck(IDM_ROLL_KEYS_BASE + 1,
            LL14(L"108鍵 (全域)", L"108 keys (full)", L"108 touches (complet)", L"108 tasti (completo)", L"108 teclas (completo)", L"108건반 (전체)", L"108键 (全域)", L"108 مفتاحا (كامل)", L"108 клавиш (полный)", L"108 Tasten (voll)", L"108 teclas (completo)", L"108 toetsen (volledig)", L"108 klawiszy (pelny)", L"108 tus (tam)"),
            m_keyRange != 88);
    }

    menu.AddCheck(IDM_ROLL_NOTENAME,
        LL14(L"ノート名を表示", L"Show note names", L"Afficher les noms de notes", L"Mostra nomi delle note", L"Mostrar nombres de notas", L"음이름 표시", L"显示音名", L"إظهار أسماء النغمات", L"Показывать названия нот", L"Notennamen anzeigen", L"Mostrar nomes das notas", L"Notennamen tonen", L"Pokazuj nazwy nut", L"Nota adlarini goster"),
        m_showNoteNames,
        LL14(L"鍵盤／ノートに音名（C, D…）を表示します。", L"Show note names (C, D…) on the keyboard/notes.", L"Afficher les noms de notes (C, D…) sur le clavier/notes.", L"Mostra i nomi delle note (C, D…) su tastiera/note.", L"Mostrar nombres de notas (C, D…) en teclado/notas.", L"건반/노트에 음이름(C, D…)을 표시합니다.", L"在键盘/音符上显示音名（C、D…）。", L"إظهار أسماء النغمات (C, D…) على المفاتيح/النغمات.", L"Показывать имена нот (C, D…) на клавиатуре/нотах.", L"Notennamen (C, D…) auf Tastatur/Noten anzeigen.", L"Mostrar nomes das notas (C, D…) no teclado/notas.", L"Toon notennamen (C, D…) op toetsen/noten.", L"Pokazuj nazwy nut (C, D…) na klawiaturze/nutach.", L"Klavye/notalarda nota adlarini (C, D…) goster."));
    menu.AddCheck(IDM_ROLL_CHORD_PANEL,
        LL14(L"コード進行パネル(実験)", L"Chord panel (experimental)", L"Panneau accords (exp.)", L"Pannello accordi (sper.)", L"Panel acordes (exp.)", L"코드 진행 패널(실험)", L"和弦进行面板(实验)", L"لوحة التآلفات (تجريبي)", L"Панель аккордов (эксп.)", L"Akkordpanel (exp.)", L"Painel de acordes (exp.)", L"Akkoordenpaneel (exp.)", L"Panel akordow (eksperymentalny)", L"Akor paneli (deneysel)"),
        savedata.mpChordPanel != 0,
        LL14(L"検出コード進行を別パネルで表示します（実験機能）。", L"Show detected chord progressions in a side panel (experimental).", L"Affiche les accords detectes dans un panneau (experimental).", L"Mostra gli accordi rilevati in un pannello (sperimentale).", L"Muestra acordes detectados en un panel (experimental).", L"감지된 코드 진행을 별도 패널에 표시합니다(실험).", L"在侧面板显示检测到的和弦进行（实验）。", L"عرض تقدم التآلفات المكتشفة في لوحة (تجريبي).", L"Показывать аккорды в панели (экспериментально).", L"Erkannte Akkordfolgen in einem Panel anzeigen (experimentell).", L"Mostra progressoes de acordes num painel (experimental).", L"Toon gedetecteerde akkoorden in een paneel (experimenteel).", L"Pokaz wykryte akordy w panelu (eksperymentalnie).", L"Algilanan akor ilerlemelerini panelde goster (deneysel)."));
    menu.AddCheck(IDM_ROLL_LOOPBACK_SCORE,
        LL14(L"PC音を譜面化", L"Score from PC audio", L"Partition depuis le PC", L"Partitura da audio PC", L"Partitura desde audio PC", L"PC 소리로 악보화", L"从PC声音成谱", L"تدوين من صوت الجهاز", L"Ноты с ПК-звука", L"Partitur aus PC-Audio", L"Partitura do audio do PC", L"Partituur van pc-audio", L"Partytura z dzwieku PC", L"PC sesinden parti"),
        savedata.mpLoopbackScore != 0,
        LL14(L"PCの再生音（ループバック）から譜面を生成します。", L"Build a score from PC loopback audio.", L"Creer une partition depuis l'audio PC (loopback).", L"Crea una partitura dall'audio PC (loopback).", L"Crear partitura desde audio PC (loopback).", L"PC 재생음(루프백)에서 악보를 만듭니다.", L"从 PC 环回音频生成乐谱。", L"إنشاء تدوين من صوت الجهاز (loopback).", L"Создавать ноты из звука ПК (loopback).", L"Partitur aus PC-Loopback-Audio erzeugen.", L"Gerar partitura do audio loopback do PC.", L"Maak partituur van pc-loopback-audio.", L"Tworz partyture z dzwieku PC (loopback).", L"PC loopback sesinden parti olustur."));
    menu.AddSeparator();
    menu.AddCheck(IDM_ROLL_CAPTURE_MIDI,
        LL14(L"MIDI録り (PC音連動)", L"MIDI capture (PC audio)", L"Enregistrement MIDI (audio PC)", L"Registrazione MIDI (audio PC)", L"Captura MIDI (audio PC)", L"MIDI 녹음 (PC 소리)", L"MIDI录制 (PC声音)", L"تسجيل MIDI (صوت الجهاز)", L"Запись MIDI (звук ПК)", L"MIDI-Aufnahme (PC-Audio)", L"Captura MIDI (audio PC)", L"MIDI-opname (pc-audio)", L"Zapis MIDI (dzwiek PC)", L"MIDI kayit (PC sesi)"),
        m_scoreCapMidi,
        LL14(L"PC音連動で検出ノートを MIDI として記録します。", L"Record detected notes as MIDI linked to PC audio.", L"Enregistrer les notes detectees en MIDI (audio PC).", L"Registra le note rilevate come MIDI (audio PC).", L"Grabar notas detectadas como MIDI (audio PC).", L"PC 소리 연동으로 감지 노트를 MIDI로 기록합니다.", L"将检测到的音符记录为 MIDI（联动 PC 声音）。", L"تسجيل النغمات المكتشفة كـ MIDI مع صوت الجهاز.", L"Записывать найденные ноты в MIDI вместе со звуком ПК.", L"Erkannte Noten als MIDI mit PC-Audio aufzeichnen.", L"Gravar notas detectadas como MIDI com audio PC.", L"Gedetecteerde noten als MIDI opnemen met pc-audio.", L"Zapisuj wykryte nuty jako MIDI z dzwiekiem PC.", L"Algilanan notalari PC sesiyle MIDI olarak kaydet."));
    menu.AddCheck(IDM_ROLL_CAPTURE_MUSICXML,
        LL14(L"MusicXML録り (PC音連動)", L"MusicXML capture (PC audio)", L"Enregistrement MusicXML (audio PC)", L"Registrazione MusicXML (audio PC)", L"Captura MusicXML (audio PC)", L"MusicXML 녹음 (PC 소리)", L"MusicXML录制 (PC声音)", L"تسجيل MusicXML (صوت الجهاز)", L"Запись MusicXML (звук ПК)", L"MusicXML-Aufnahme (PC-Audio)", L"Captura MusicXML (audio PC)", L"MusicXML-opname (pc-audio)", L"Zapis MusicXML (dzwiek PC)", L"MusicXML kayit (PC sesi)"),
        m_scoreCapXml,
        LL14(L"PC音連動で検出ノートを MusicXML として記録します。", L"Record detected notes as MusicXML linked to PC audio.", L"Enregistrer les notes detectees en MusicXML (audio PC).", L"Registra le note rilevate come MusicXML (audio PC).", L"Grabar notas detectadas como MusicXML (audio PC).", L"PC 소리 연동으로 감지 노트를 MusicXML로 기록합니다.", L"将检测到的音符记录为 MusicXML（联动 PC 声音）。", L"تسجيل النغمات المكتشفة كـ MusicXML مع صوت الجهاز.", L"Записывать найденные ноты в MusicXML вместе со звуком ПК.", L"Erkannte Noten als MusicXML mit PC-Audio aufzeichnen.", L"Gravar notas detectadas como MusicXML com audio PC.", L"Gedetecteerde noten als MusicXML opnemen met pc-audio.", L"Zapisuj wykryte nuty jako MusicXML z dzwiekiem PC.", L"Algilanan notalari PC sesiyle MusicXML olarak kaydet."));
    menu.AddSeparator();

    CCustomPopupMenu* subSpeed = menu.AddSubMenu(
        LL14(L"表示の流れる速度", L"Display scroll speed", L"Vitesse de defilement", L"Velocita scorrimento", L"Velocidad de desplazamiento", L"표시 스크롤 속도", L"显示滚动速度", L"سرعة التمرير", L"Скорость прокрутки", L"Anzeigegeschwindigkeit", L"Velocidade de rolagem", L"Weergavesnelheid", L"Predkosc przewijania", L"Goruntuleme hizi"),
        LL14(L"ロール表示の流れる速さを調整します（解析速度は変わりません）。", L"Adjust how fast the roll display scrolls (analysis rate is unchanged).", L"Regler la vitesse de defilement (l'analyse ne change pas).", L"Regola la velocita di scorrimento (l'analisi non cambia).", L"Ajustar la velocidad de desplazamiento (el analisis no cambia).", L"롤 표시 스크롤 속도를 조정합니다(분석 속도는 그대로).", L"调整卷帘显示滚动速度（分析速度不变）。", L"ضبط سرعة تمرير العرض (معدل التحليل لا يتغير).", L"Скорость прокрутки отображения (анализ не меняется).", L"Anzeigegeschwindigkeit anpassen (Analyse unverandert).", L"Ajustar a velocidade de rolagem (analise inalterada).", L"Pas weergavesnelheid aan (analyse blijft gelijk).", L"Dostosuj predkosc przewijania (analiza bez zmian).", L"Goruntuleme kaydirma hizini ayarla (analiz degismez)."));
    if (subSpeed) {
        subSpeed->AddSlider(
            LL14(L"速度 (%)", L"Speed (%)", L"Vitesse (%)", L"Velocita (%)", L"Velocidad (%)", L"속도 (%)", L"速度 (%)", L"السرعة (%)", L"Скорость (%)", L"Geschwindigkeit (%)", L"Velocidade (%)", L"Snelheid (%)", L"Predkosc (%)", L"Hiz (%)"),
            25, 200, m_rollSpeedPct, &CPianoRoll::RollSpeedSliderCb, this,
            LL14(L"25%=遅い … 100%=標準 … 200%=速い（ドラッグ中に即反映）", L"25%=slow … 100%=normal … 200%=fast (live while dragging)", L"25%=lent … 100%=normal … 200%=rapide (temps reel)", L"25%=lento … 100%=normale … 200%=veloce (in tempo reale)", L"25%=lento … 100%=normal … 200%=rapido (en vivo)", L"25%=느림 … 100%=표준 … 200%=빠름(드래그 중 즉시 반영)", L"25%=慢 … 100%=标准 … 200%=快（拖动时即时生效）", L"25%=بطيء … 100%=عادي … 200%=سريع (مباشر أثناء السحب)", L"25%=медленно … 100%=обычно … 200%=быстро (сразу при перетаскивании)", L"25%=langsam … 100%=normal … 200%=schnell (live beim Ziehen)", L"25%=lento … 100%=normal … 200%=rapido (ao vivo)", L"25%=traag … 100%=normaal … 200%=snel (live tijdens slepen)", L"25%=wolno … 100%=normalnie … 200%=szybko (na zywo)", L"25%=yavas … 100%=normal … 200%=hizli (suruklerken anlik)"));
        subSpeed->AddSeparator();
        subSpeed->AddCommand(IDM_ROLL_SPEED_BASE + 3,
            LL14(L"100% に戻す", L"Reset to 100%", L"Remettre a 100%", L"Ripristina a 100%", L"Restablecer a 100%", L"100%로 되돌리기", L"重置为 100%", L"إعادة إلى 100%", L"Сбросить на 100%", L"Auf 100% zurucksetzen", L"Redefinir para 100%", L"Terugzetten naar 100%", L"Przywroc 100%", L"100%'e sifirla"));
    }

    menu.AddSeparator();
    menu.AddCheck(IDM_ROLL_LEGEND,
        LL14(L"記号の凡例", L"Symbol legend", L"Legende des symboles", L"Legenda simboli", L"Leyenda de simbolos", L"기호 범례", L"符号图例", L"دليل الرموز", L"Легенда символов", L"Symbollegende", L"Legenda de simbolos", L"Symbollegenda", L"Legenda symboli", L"Sembol aciklamasi"),
        m_showExprLegend,
        LL14(L"表現記号の凡例パネルを表示します。", L"Show the expression-symbol legend panel.", L"Afficher le panneau legende des symboles d'expression.", L"Mostra il pannello legenda dei simboli espressivi.", L"Mostrar el panel de leyenda de simbolos de expresion.", L"표현 기호 범례 패널을 표시합니다.", L"显示奏法记号图例面板。", L"عرض لوحة دليل رموز التعبير.", L"Показывать панель легенды знаков экспрессии.", L"Legendenpanel fur Ausdruckszeichen anzeigen.", L"Mostrar o painel de legenda dos simbolos de expressao.", L"Toon het legenda-paneel voor expressietekens.", L"Pokaz panel legendy znakow ekspresji.", L"Ifade isareti aciklama panelini goster."));
    menu.AddCheck(IDM_ROLL_EXPR,
        LL14(L"表現記号を表示", L"Show expression marks", L"Afficher les symboles d'expression", L"Mostra simboli espressivi", L"Mostrar simbolos de expresion", L"표현 기호 표시", L"显示奏法记号", L"إظهار رموز التعبير", L"Показывать знаки экспрессии", L"Ausdruckszeichen anzeigen", L"Mostrar simbolos de expressao", L"Expressietekens tonen", L"Pokazuj znaki ekspresji", L"Ifade isaretlerini goster"),
        m_showExprMarks,
        LL14(L"検出した表現記号（ダイナミクスなど）をロール上に表示します。", L"Show detected expression marks (dynamics, etc.) on the roll.", L"Afficher les symboles d'expression detectes (dynamiques…) sur le roll.", L"Mostra i simboli espressivi rilevati (dinamiche…) sul roll.", L"Mostrar simbolos de expresion detectados (dinamicas…) en el roll.", L"감지된 표현 기호(다이내믹스 등)를 롤에 표시합니다.", L"在卷帘上显示检测到的奏法记号（力度等）。", L"عرض رموز التعبير المكتشفة (الديناميكيات…) على اللفة.", L"Показывать обнаруженные знаки экспрессии (динамика…) на ролле.", L"Erkannte Ausdruckszeichen (Dynamik usw.) auf der Rolle anzeigen.", L"Mostrar simbolos de expressao detectados (dinamicas…) no roll.", L"Toon gedetecteerde expressietekens (dynamiek enz.) op de roll.", L"Pokaz wykryte znaki ekspresji (dynamika itd.) na rolce.", L"Algilanan ifade isaretlerini (dinamikler vb.) roll uzerinde goster."));
    menu.AddCheck(IDM_ROLL_METER,
        LL14(L"レベルメーター", L"Level meter", L"Indicateur de niveau", L"Misuratore di livello", L"Medidor de nivel", L"레벨 미터", L"电平表", L"مقياس المستوى", L"Уровень сигнала", L"Pegelanzeige", L"Medidor de nivel", L"Niveaumeter", L"Miernik poziomu", L"Seviye olcer"),
        m_showLevelMeter,
        LL14(L"レベルメーターの表示／非表示を切り替えます。", L"Show or hide the level meters.", L"Afficher ou masquer les indicateurs de niveau.", L"Mostra o nasconde i misuratori di livello.", L"Mostrar u ocultar los medidores de nivel.", L"레벨 미터 표시/숨기기를 전환합니다.", L"显示或隐藏电平表。", L"إظهار أو إخفاء مقاييس المستوى.", L"Показать или скрыть измерители уровня.", L"Pegelanzeigen ein- oder ausblenden.", L"Mostrar ou ocultar os medidores de nivel.", L"Niveaumeters tonen of verbergen.", L"Pokaz lub ukryj mierniki poziomu.", L"Seviye olcerlerini goster veya gizle."));
    menu.AddCheck(IDM_ROLL_TOPMOST,
        LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위에 표시", L"始终置顶", L"دائما في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd op voorgrond", L"Zawsze na wierzchu", L"Her zaman ustte"),
        m_alwaysOnTop,
        LL14(L"ウィンドウを常に他のウィンドウの手前に表示します。", L"Keep this window always on top of others.", L"Garder cette fenetre toujours au premier plan.", L"Mantieni questa finestra sempre in primo piano.", L"Mantener esta ventana siempre delante de las demas.", L"이 창을 항상 다른 창 위에 표시합니다.", L"将此窗口始终置于其他窗口之上。", L"إبقاء هذه النافذة دائماً فوق النوافذ الأخرى.", L"Держать это окно поверх остальных.", L"Dieses Fenster immer im Vordergrund halten.", L"Manter esta janela sempre acima das outras.", L"Houd dit venster altijd boven andere.", L"Trzymaj to okno zawsze na wierzchu.", L"Bu pencereyi her zaman digerlerinin ustunde tut."));

    CCustomPopupMenu* subDetect = menu.AddSubMenu(
        LL14(L"検出オプション", L"Detection options", L"Options de detection", L"Opzioni di rilevamento", L"Opciones de deteccion", L"검출 옵션", L"检测选项", L"خيارات الكشف", L"Параметры обнаружения", L"Erkennungsoptionen", L"Opcoes de deteccao", L"Detectie-opties", L"Opcje wykrywania", L"Algilama secenekleri"),
        LL14(L"ノート検出のゴースト抑制や再アタック判定などを切り替えます。", L"Toggle ghost suppression, re-attack detection, and related options.", L"Activer/desactiver suppressions de fantomes et reattaque.", L"Attiva/disattiva soppressione fantasmi e riattacco.", L"Activar/desactivar supresion de fantasmas y reataque.", L"고스트 억제·리어택 등 검출 옵션을 전환합니다.", L"切换幽灵抑制、再起音检测等相关选项。", L"تبديل كبح الأشباح وكشف الهجوم المتكرر وغيرها.", L"Переключать подавление призраков и реатаку.", L"Geisterunterdruckung, Re-Attack u. a. umschalten.", L"Alternar supressao de fantasmas e reataque.", L"Schakel spookdemping, her-aanval e.d. om.", L"Przelaczaj tlumienie duchow i reatak.", L"Hayalet bastirma, yeniden saldiri vb. secenekleri ac/kapat."));
    if (subDetect) {
        subDetect->AddCheck(IDM_ROLL_REATTACK,
            LL14(L"再アタック検出", L"Re-attack detect", L"Detection de reattaque", L"Rilevamento riattacco", L"Deteccion de reataque", L"리어택 검출", L"再起音检测", L"كشف الهجوم المتكرر", L"Обнаружение реатаки", L"Re-Attack-Erkennung", L"Detectar reataque", L"Her-aanval detectie", L"Wykrywanie reataku", L"Yeniden saldiri algilama"),
            m_reattackDetectEnabled);
        subDetect->AddCheck(IDM_ROLL_IMPULSE,
            LL14(L"打撃音ゴースト抑制", L"Impulsive ghost suppress", L"Suppression fantomes impulsifs", L"Soppressione fantasmi impulsivi", L"Supresion de fantasmas impulsivos", L"타격음 고스트 억제", L"打击音幽灵抑制", L"كبح أشباح الإيقاع", L"Подавление импульсных призраков", L"Impulsiv-Geister unterdrucken", L"Suprimir fantasmas impulsivos", L"Impulsieve spoken dempen", L"Tlumienie duchow impulsywnych", L"Vurus hayaletini bastir"),
            m_impulsiveGhostSuppressEnabled);
        subDetect->AddCheck(IDM_ROLL_HARM_GHOST,
            LL14(L"倍音ゴースト抑制", L"Harmonic ghost suppress", L"Suppression fantomes harmoniques", L"Soppressione fantasmi armonici", L"Supresion de fantasmas armonicos", L"배음 고스트 억제", L"泛音幽灵抑制", L"كبح أشباح التوافقيات", L"Подавление гармонических призраков", L"Oberton-Geister unterdrucken", L"Suprimir fantasmas harmonicos", L"Harmonische spoken dempen", L"Tlumienie duchow harmonicznych", L"Armonik hayaletini bastir"),
            m_harmonicGhostGuardEnabled);
        subDetect->AddCheck(IDM_ROLL_HARM_PROF,
            LL14(L"音色プロファイル判定", L"Timbre profile guard", L"Garde profil de timbre", L"Protezione profilo timbrico", L"Guardia de perfil timbrico", L"음색 프로파일 판정", L"音色轮廓判定", L"حارس ملف الطابع", L"Профиль тембра", L"Klangprofil-Schutz", L"Guarda de perfil timbrico", L"Timbreprofiel-bewaking", L"Ochrona profilu barwy", L"Timbre profil korumasi"),
            m_harmonicProfileGuardEnabled);
    }

    menu.AddSeparator();
    menu.AddCommand(IDM_ROLL_TUNE,
        LL14(L"検出パラメータ調整...", L"Detection parameter tuning...", L"Regler les parametres...",
            L"Regola parametri rilevamento...", L"Ajustar parametros de deteccion...", L"검출 파라미터 조정...",
            L"检测参数调整...", L"ضبط معلمات الكشف...", L"Настройка параметров...", L"Erkennungsparameter...",
            L"Ajustar parametros...", L"Detectieparameters...", L"Dostosuj parametry...", L"Algilama parametreleri..."),
        LL14(L"検出感度などの詳細パラメータを開きます。", L"Open detailed detection parameter tuning.", L"Ouvrir le reglage detaille des parametres.", L"Apri la regolazione dettagliata dei parametri.", L"Abrir el ajuste detallado de parametros.", L"검출 감도 등 상세 파라미터를 엽니다.", L"打开检测灵敏度等详细参数。", L"فتح ضبط معلمات الكشف التفصيلية.", L"Открыть детальную настройку параметров.", L"Detaillierte Erkennungsparameter offnen.", L"Abrir ajuste detalhado de parametros.", L"Open gedetailleerde detectieparameters.", L"Otworz szczegolowe parametry wykrywania.", L"Ayrintili algilama parametrelerini ac."));

    menu.AddCheck(IDM_ROLL_FREEZE,
        LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"),
        m_frozen,
        LL14(L"ロールのスクロール表示だけを一時停止します（解析は継続）。", L"Freeze only the roll scroll display (analysis continues).", L"Geler uniquement le defilement du roll (l'analyse continue).", L"Congela solo lo scorrimento del roll (l'analisi continua).", L"Congelar solo el desplazamiento del roll (el analisis continua).", L"롤 스크롤 표시만 일시 정지합니다(분석은 계속).", L"仅冻结卷帘滚动显示（分析继续）。", L"تجميد تمرير اللفة فقط (التحليل يستمر).", L"Заморозить только прокрутку ролла (анализ продолжается).", L"Nur Scrollanzeige der Rolle einfrieren (Analyse lauft weiter).", L"Congelar so a rolagem do roll (a analise continua).", L"Bevries alleen de scrollweergave (analyse gaat door).", L"Zamroz tylko przewijanie rolki (analiza trwa).", L"Yalnizca roll kaydirma goruntusunu dondur (analiz surer)."));
    menu.AddCommand(IDM_ROLL_CLEAR,
        LL14(L"表示をクリア", L"Clear display", L"Effacer l'affichage", L"Cancella visualizzazione", L"Borrar pantalla", L"표시 지우기", L"清除显示", L"مسح العرض", L"Очистить экран", L"Anzeige leeren", L"Limpar exibicao", L"Weergave wissen", L"Wyczysc wyswietlacz", L"Goruntuyu temizle"),
        LL14(L"ロール上の表示履歴（ノート跡など）をクリアします。", L"Clear roll display history (note traces, etc.).", L"Effacer l'historique d'affichage du roll (traces de notes…).", L"Cancella la cronologia di visualizzazione del roll (tracce note…).", L"Borrar el historial de pantalla del roll (trazas de notas…).", L"롤 표시 이력(노트 흔적 등)을 지웁니다.", L"清除卷帘显示历史（音符轨迹等）。", L"مسح سجل عرض اللفة (آثار النغمات…).", L"Очистить историю отображения ролла (следы нот…).", L"Anzeigehistorie der Rolle leeren (Notenspuren usw.).", L"Limpar o historico de exibicao do roll (trilhas de notas…).", L"Wis weergavegeschiedenis van de roll (nootsporen enz.).", L"Wyczysc historie wyswietlania rolki (slady nut itd.).", L"Roll goruntu gecmisini temizle (nota izleri vb.)."));
    menu.AddSeparator();
    menu.AddCommand(ID_MP_OPEN_EQ,
        LL14(L"イコライザを開く", L"Open equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore", L"Abrir ecualizador",
            L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل", L"Открыть эквалайзер", L"Equalizer öffnen",
            L"Abrir equalizador", L"Equalizer openen", L"Otworz equalizer", L"Equalizeri ac"),
        LL14(L"イコライザウィンドウを開きます。", L"Open the equalizer window.", L"Ouvrir la fenetre de l'egaliseur.", L"Apri la finestra dell'equalizzatore.", L"Abrir la ventana del ecualizador.", L"이퀄라이저 창을 엽니다.", L"打开均衡器窗口。", L"فتح نافذة المعادل.", L"Открыть окно эквалайзера.", L"Equalizer-Fenster offnen.", L"Abrir a janela do equalizador.", L"Open het equalizer-venster.", L"Otworz okno equalizera.", L"Equalizer penceresini ac."));
    menu.AddCommand(ID_MP_OPEN_ANALYZER,
        LL14(L"アナライザを開く", L"Open analyzer", L"Ouvrir l'analyseur", L"Apri analizzatore", L"Abrir analizador",
            L"분석기 열기", L"打开分析器", L"فتح المحلل", L"Открыть анализатор", L"Analyzer öffnen",
            L"Abrir analisador", L"Analyzer openen", L"Otworz analizator", L"Analizoru ac"),
        LL14(L"アナライザウィンドウを開きます。", L"Open the analyzer window.", L"Ouvrir la fenetre de l'analyseur.", L"Apri la finestra dell'analizzatore.", L"Abrir la ventana del analizador.", L"분석기 창을 엽니다.", L"打开分析器窗口。", L"فتح نافذة المحلل.", L"Открыть окно анализатора.", L"Analysator-Fenster offnen.", L"Abrir a janela do analisador.", L"Open het analyser-venster.", L"Otworz okno analizatora.", L"Analizor penceresini ac."));
    menu.AddSeparator();
    menu.AddCommand(ID_HELP_SHOWSHEET,
        LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa",
            L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل",
            L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding",
            L"Przewodnik", L"İşlem kılavuzu"),
        LL14(L"簡易ピアノロールの操作ガイドを表示します。", L"Show the simple piano roll operation guide.", L"Afficher le guide d'utilisation du piano roll simple.", L"Mostra la guida operativa del piano roll semplice.", L"Mostrar la guia de operacion del piano roll simple.", L"간이 피아노 롤 조작 가이드를 표시합니다.", L"显示简易钢琴卷帘操作指南。", L"عرض دليل تشغيل لفافة البيانو البسيطة.", L"Показать руководство по простому пианороллу.", L"Bedienungsanleitung des einfachen Piano-Roll anzeigen.", L"Mostrar o guia de operacao do piano roll simples.", L"Toon de handleiding van de eenvoudige piano-roll.", L"Pokaz przewodnik po prostym piano roll.", L"Basit piyano roll islem kilavuzunu goster."));

    if (point.x == -1 && point.y == -1) {
        CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
        point = CPoint(rc.left + 8, rc.top + 8);
    }
    const UINT cmd = menu.Track(point, this);
    if (cmd == ID_MP_OPEN_EQ || cmd == ID_MP_OPEN_ANALYZER) {
        extern CMediaPlayerDlg* mp;
        if (mp && ::IsWindow(mp->GetSafeHwnd()))
            mp->PostMessage(WM_COMMAND, cmd);
        else if (og && ::IsWindow(og->GetSafeHwnd())) {
            if (cmd == ID_MP_OPEN_EQ)
                og->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
            else
                og->PostMessage(WM_OGG_TOGGLE_SUBUI, 2, 0);
        }
    }
    else if (cmd != 0)
        SendMessage(WM_COMMAND, cmd);
}
int CPianoRoll::HistoryCountLocked() const
{
    return m_historyCount;
}

const CPianoRoll::NoteFrame& CPianoRoll::HistoryAt(int indexFromNewest) const
{
    static NoteFrame s_empty;
    static bool s_emptyInit = false;
    if (!s_emptyInit) {
        memset(&s_empty, 0, sizeof(s_empty));
        s_emptyInit = true;
    }
    if (indexFromNewest < 0 || indexFromNewest >= m_historyCount)
        return s_empty;
    const int idx = (m_historyHead + indexFromNewest) % (int)MAX_HISTORY;
    return m_historyRing[idx];
}

void CPianoRoll::CopyHistorySnapshot(NoteFrame* out, int maxOut, int& outCount) const
{
    outCount = 0;
    if (!out || maxOut <= 0 || m_historyCount <= 0) return;
    const int n = (m_historyCount < maxOut) ? m_historyCount : maxOut;
    for (int i = 0; i < n; ++i)
        out[i] = HistoryAt(i);
    outCount = n;
}

void CPianoRoll::ExportRemoteSnapshot(BYTE keyLevels108[108], BYTE keyExpr108[108],
    BYTE* histBits, BYTE* histExpr, int maxRows, int& outRows, int& outExprOn,
    WCHAR* chordOut, int chordCch) const
{
    outRows = 0;
    outExprOn = m_showExprMarks ? 1 : 0;
    if (keyLevels108) {
        float mx = 0.0f;
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (m_activeKeys[i] && m_noteStrength[i] > mx)
                mx = m_noteStrength[i];
        }
        for (int i = 0; i < KEY_COUNT; ++i) {
            int v = 0;
            if (m_activeKeys[i] && mx > 1e-8f) {
                v = (int)(m_noteStrength[i] / mx * 100.0f + 0.5f);
                if (v < 1) v = 1;
                if (v > 100) v = 100;
            }
            keyLevels108[i] = (BYTE)v;
        }
        for (int i = KEY_COUNT; i < 108; ++i)
            keyLevels108[i] = 0;
    }
    if (keyExpr108) {
        memset(keyExpr108, 0, 108);
        if (outExprOn) {
            for (int i = 0; i < KEY_COUNT; ++i)
                keyExpr108[i] = m_activeKeys[i] ? m_exprFlags[i] : 0;
        }
    }
    if (histBits && maxRows > 0 && m_historyCount > 0) {
        const int n = (m_historyCount < maxRows) ? m_historyCount : maxRows;
        for (int r = 0; r < n; ++r) {
            BYTE* dst = histBits + r * 14;
            memset(dst, 0, 14);
            BYTE* ex = (histExpr && outExprOn) ? (histExpr + r * 108) : NULL;
            if (ex) memset(ex, 0, 108);
            const NoteFrame& fr = HistoryAt(r);
            for (int k = 0; k < KEY_COUNT; ++k) {
                if (!fr.active[k]) continue;
                dst[k >> 3] |= (BYTE)(1u << (k & 7));
                if (ex) ex[k] = fr.expr[k];
            }
        }
        outRows = n;
    }
    if (chordOut && chordCch > 0) {
        if (m_chordLast[0])
            wcsncpy_s(chordOut, chordCch, m_chordLast, _TRUNCATE);
        else
            wcsncpy_s(chordOut, chordCch, L"-", _TRUNCATE);
    }
}

bool CPianoRoll::IsBlackKey(int midiNote) const
{
    const int r = midiNote % 12;
    return (r == 1 || r == 3 || r == 6 || r == 8 || r == 10);
}

// 表示する鍵の範囲 [lo, hi)。
// 88鍵表示は「表示だけ」を A0(MIDI 21) 以上に絞る。Goertzel の解析鍵数は
// 常に KEY_COUNT(108) のままで、検出処理には一切影響しない。
void CPianoRoll::GetDisplayKeyRange(int& lo, int& hi) const
{
    if (m_keyRange == 88) {
        lo = 21;            // A0
        hi = KEY_COUNT;     // B7 まで(MIDI 108=C8 は解析範囲外)
    }
    else {
        lo = 0;
        hi = KEY_COUNT;
    }
}

// 描画ループから 1 鍵ごとに数え直すと UI スレッドを無駄に食うのでキャッシュする
void CPianoRoll::EnsureDisplayKeyCache() const
{
    if (m_dispWhiteTotal > 0 && m_dispCacheRange == m_keyRange) return;
    int lo, hi; GetDisplayKeyRange(lo, hi);
    int w = 0;
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (i < lo || i >= hi || IsBlackKey(MIDI_BASE + i)) {
            m_dispWhiteIdx[i] = -1;
            continue;
        }
        m_dispWhiteIdx[i] = w++;
    }
    m_dispWhiteTotal = (w > 0) ? w : 1;
    m_dispCacheRange = m_keyRange;
}

int CPianoRoll::DisplayWhiteKeyCount() const
{
    EnsureDisplayKeyCache();
    return m_dispWhiteTotal;
}

int CPianoRoll::DisplayWhiteKeyIndex(int midiNote) const
{
    const int idx = midiNote - MIDI_BASE;
    if (idx < 0 || idx >= KEY_COUNT) return -1;
    EnsureDisplayKeyCache();
    return m_dispWhiteIdx[idx];
}

void CPianoRoll::GetChromaticKeyRect(int keyIndex, int width, int& xL, int& xR) const
{
    if (width <= 0) { xL = xR = 0; return; }
    int lo, hi; GetDisplayKeyRange(lo, hi);
    if (keyIndex < lo || keyIndex >= hi) { xL = xR = 0; return; }  // 表示範囲外は描かせない
    const int span = hi - lo;
    xL = (int)(((keyIndex - lo) * (float)width) / (float)span);
    xR = (int)(((keyIndex - lo + 1) * (float)width) / (float)span);
    if (xR <= xL) xR = xL + 1;
}

int CPianoRoll::GetWhiteKeyIndex(int midiNote) const
{
    int w = 0;
    for (int m = MIDI_BASE; m < midiNote; ++m)
        if (!IsBlackKey(m)) ++w;
    return w;
}

void CPianoRoll::GetWhiteKeyRect52(int midi, int width, int& xL, int& xR) const
{
    if (width <= 0 || IsBlackKey(midi)) { xL = xR = 0; return; }
    const int w = DisplayWhiteKeyIndex(midi);
    if (w < 0) { xL = xR = 0; return; }
    const int total = DisplayWhiteKeyCount();
    xL = (int)(w * (float)width / (float)total);
    xR = (int)((w + 1) * (float)width / (float)total);
    if (xR <= xL) xR = xL + 1;
}

namespace PianoDraw
{
    static const wchar_t* WhiteKeyLabel(int midi)
    {
        switch (midi % 12) {
        case 0:  return L"C";
        case 2:  return L"D";
        case 4:  return L"E";
        case 5:  return L"F";
        case 7:  return L"G";
        case 9:  return L"A";
        case 11: return L"B";
        default: return nullptr;
        }
    }

    static int MidiOctaveNumber(int midi) { return (midi / 12) - 1; }

    static void LerpRgb(float t, int r0, int g0, int b0, int r1, int g1, int b1, int& r, int& g, int& b)
    {
        r = (int)(r0 + t * (r1 - r0));
        g = (int)(g0 + t * (g1 - g0));
        b = (int)(b0 + t * (b1 - b0));
    }

    static void SampleKeyGradient(float t, int& r, int& g, int& b)
    {
        static const struct { float pos; int r, g, b; } stops[] = {
            { 0.00f,  50, 110, 225 }, // 低音: 青
            { 0.20f,  30, 185, 215 }, // シアン
            { 0.40f,  55, 200,  80 }, // 緑
            { 0.60f, 215, 210,  45 }, // 黄
            { 0.80f, 235, 140,  40 }, // オレンジ
            { 1.00f, 225,  50,  50 }, // 高音: 赤
        };
        if (t <= stops[0].pos) { r = stops[0].r; g = stops[0].g; b = stops[0].b; return; }
        for (int i = 1; i < (int)(sizeof(stops) / sizeof(stops[0])); ++i) {
            if (t > stops[i].pos) continue;
            const float seg = (t - stops[i - 1].pos) / (stops[i].pos - stops[i - 1].pos);
            LerpRgb(seg, stops[i - 1].r, stops[i - 1].g, stops[i - 1].b,
                stops[i].r, stops[i].g, stops[i].b, r, g, b);
            return;
        }
        const int n = (int)(sizeof(stops) / sizeof(stops[0])) - 1;
        r = stops[n].r; g = stops[n].g; b = stops[n].b;
    }

    static COLORREF KeyNoteColorImpl(int keyIndex, float strength, bool blackKey)
    {
        static constexpr int kKeys = PianoKey::COUNT;
        if (keyIndex < 0) keyIndex = 0;
        if (keyIndex >= kKeys) keyIndex = kKeys - 1;
        const float t = (float)keyIndex / (float)(kKeys - 1);
        const float st = min(strength / 3.0f, 1.0f);
        int r, g, b;
        SampleKeyGradient(t, r, g, b);
        if (blackKey) {
            r = min(255, r + 25);
            g = max(0, g - 15);
            b = max(0, b - 10);
        }
        const int dim = (int)((1.0f - st * 0.65f) * 80.0f);
        r = max(0, min(255, r - dim));
        g = max(0, min(255, g - dim));
        b = max(0, min(255, b - dim));
        return RGB(r, g, b);
    }

    static void DrawBevelKey(CDC& dc, CRect rc, COLORREF fill, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed) rc.OffsetRect(0, min(2, rc.Height() / 5));
        dc.FillSolidRect(&rc, fill);
        const COLORREF topLeft = pressed ? RGB(45, 45, 50) : RGB(255, 255, 255);
        const COLORREF botRight = pressed ? RGB(190, 190, 195) : RGB(110, 110, 115);
        HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(dc.GetSafeHdc(), topLeft);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.left, rc.top); dc.LineTo(rc.right - 1, rc.top);
        ::SetDCPenColor(dc.GetSafeHdc(), botRight);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.right - 1, rc.bottom - 1); dc.LineTo(rc.right - 1, rc.top);
        dc.SelectObject(oldPen);
    }

    static COLORREF LocalKeyColor(int keyIndex, float strength, bool blackKey)
    {
        return KeyNoteColorImpl(keyIndex, strength, blackKey);
    }

    static void DrawLaneFill(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int keyIndex, float fallbackStrength, bool blackKey)
    {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;
        int laneCount = 0;
        for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) ++laneCount;
        if (laneCount <= 0) { dc.FillSolidRect(&rc, LocalKeyColor(keyIndex, fallbackStrength, blackKey)); return; }
        if (laneCount == 1) {
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            dc.FillSolidRect(&rc, LocalKeyColor(keyIndex, st, blackKey)); return;
        }
        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            const int w = rc.Width();
            sub.left = rc.left + (w * slot) / laneCount; sub.right = rc.left + (w * (slot + 1)) / laneCount;
            if (sub.right <= sub.left) sub.right = sub.left + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            dc.FillSolidRect(&sub, LocalKeyColor(keyIndex, st, blackKey)); ++slot;
        }
    }

    static void DrawLaneKey(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int keyIndex, float fallbackStrength, bool blackKey, bool pressed)
    {
        if (rc.Width() <= 1 || rc.Height() <= 1) return;
        if (pressed) rc.OffsetRect(0, min(2, rc.Height() / 5));
        int laneCount = 0;
        for (int b = 0; b < 3; ++b) if (bandMask & (1u << b)) ++laneCount;
        if (laneCount <= 1) {
            const float st = laneStr && laneStr[0] > 0.0f ? laneStr[0] : fallbackStrength;
            DrawBevelKey(dc, rc, LocalKeyColor(keyIndex, st, blackKey), pressed); return;
        }
        int slot = 0;
        for (int b = 0; b < 3; ++b) {
            if (!(bandMask & (1u << b))) continue;
            CRect sub = rc;
            sub.top = rc.top + (rc.Height() * slot) / laneCount; sub.bottom = rc.top + (rc.Height() * (slot + 1)) / laneCount;
            if (sub.bottom <= sub.top) sub.bottom = sub.top + 1;
            const float st = (laneStr && laneStr[slot] > 0.0f) ? laneStr[slot] : fallbackStrength;
            DrawBevelKey(dc, sub, LocalKeyColor(keyIndex, st, blackKey), pressed); ++slot;
        }
    }

    static CRect DynInsetRect(CRect rc, float dynLevel)
    {
        if (rc.Width() <= 1) return rc;
        if (dynLevel < 0.0f) dynLevel = 0.0f;
        if (dynLevel > 1.0f) dynLevel = 1.0f;
        const int w = rc.Width();
        int shrink = (int)((1.0f - dynLevel) * w * 0.44f);
        if (shrink > w / 2 - 1) shrink = w / 2 - 1;
        if (shrink < 0) shrink = 0;
        rc.left += shrink;
        rc.right -= shrink;
        if (rc.right <= rc.left) rc.right = rc.left + 1;
        return rc;
    }

    static COLORREF ExprColorForFlag(uint8_t flag)
    {
        switch (flag) {
        case PianoExpr::SCOOP:   return RGB(255, 220, 80);
        case PianoExpr::ACCENT:  return RGB(255, 100, 100);
        case PianoExpr::VIBRATO: return RGB(120, 255, 180);
        case PianoExpr::SLIDE:   return RGB(120, 160, 255);
        case PianoExpr::FALL:    return RGB(255, 170, 90);
        case PianoExpr::SUSTAIN: return RGB(190, 190, 210);
        case PianoExpr::CRESC:   return RGB(120, 230, 255);
        case PianoExpr::DECRESC: return RGB(200, 150, 255);
        default:                 return RGB(255, 255, 255);
        }
    }

    static COLORREF ExprPrimaryColor(uint8_t expr)
    {
        if (expr & PianoExpr::SCOOP) return ExprColorForFlag(PianoExpr::SCOOP);
        if (expr & PianoExpr::ACCENT) return ExprColorForFlag(PianoExpr::ACCENT);
        if (expr & PianoExpr::VIBRATO) return ExprColorForFlag(PianoExpr::VIBRATO);
        if (expr & PianoExpr::SLIDE) return ExprColorForFlag(PianoExpr::SLIDE);
        if (expr & PianoExpr::FALL) return ExprColorForFlag(PianoExpr::FALL);
        if (expr & PianoExpr::CRESC) return ExprColorForFlag(PianoExpr::CRESC);
        if (expr & PianoExpr::DECRESC) return ExprColorForFlag(PianoExpr::DECRESC);
        if (expr & PianoExpr::SUSTAIN) return ExprColorForFlag(PianoExpr::SUSTAIN);
        return RGB(255, 255, 255);
    }

    static float ExprDynLevel(uint8_t expr, float dyn)
    {
        if (dyn < 0.0f) dyn = 0.0f;
        if (dyn > 1.0f) dyn = 1.0f;
        if (expr & PianoExpr::ACCENT) dyn = max(dyn, 0.95f);
        if (expr & PianoExpr::VIBRATO) dyn = max(dyn, min(1.0f, dyn + 0.24f));
        if (expr & PianoExpr::SCOOP) dyn = max(dyn, min(1.0f, dyn + 0.14f));
        if (expr & PianoExpr::SLIDE) dyn = max(dyn, min(1.0f, dyn + 0.20f));
        if (expr & PianoExpr::FALL) dyn = max(dyn, min(1.0f, dyn + 0.12f));
        if (expr & PianoExpr::CRESC) dyn = max(dyn, min(1.0f, dyn + 0.16f));
        if (expr & PianoExpr::DECRESC) dyn = max(dyn, min(1.0f, dyn + 0.10f));
        if (expr & PianoExpr::SUSTAIN) dyn = max(dyn, min(1.0f, dyn + 0.08f));
        return dyn;
    }

    static const wchar_t* ExprGlyphForFlag(uint8_t flag)
    {
        switch (flag) {
        case PianoExpr::SCOOP:   return L"\x2197";
        case PianoExpr::ACCENT:  return L"\x25B8";
        case PianoExpr::VIBRATO: return L"~";
        case PianoExpr::SLIDE:   return L"\x2192";
        case PianoExpr::FALL:    return L"\x2198";
        case PianoExpr::SUSTAIN: return L"\x2015";
        case PianoExpr::CRESC:   return L"\x003C";   // '<' クレッシェンド
        case PianoExpr::DECRESC: return L"\x003E";   // '>' デクレッシェンド
        default:                 return L"?";
        }
    }

    static int CountExprFlags(uint8_t expr)
    {
        int n = 0;
        for (int f = 1; f <= (int)PianoExpr::DECRESC; f <<= 1)
            if (expr & (uint8_t)f) ++n;
        return n;
    }

    static void DrawExprBadgePanel(CDC& dc, CRect rc, uint8_t flag, CFont* pFont)
    {
        if (rc.Width() < 4 || rc.Height() < 4 || !pFont) return;
        const COLORREF fg = ExprColorForFlag(flag);
        dc.FillSolidRect(&rc, RGB(14, 14, 20));
        HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(dc.GetSafeHdc(), fg);
        dc.MoveTo(rc.left, rc.bottom - 1); dc.LineTo(rc.left, rc.top); dc.LineTo(rc.right - 1, rc.top);
        dc.LineTo(rc.right - 1, rc.bottom - 1); dc.LineTo(rc.left, rc.bottom - 1);
        dc.SelectObject(oldPen);

        CFont* pOld = dc.SelectObject(pFont);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(fg);
        dc.DrawText(ExprGlyphForFlag(flag), rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(pOld);
    }

    static void DrawExprGlyphOnNote(CDC& dc, CRect rc, uint8_t flag, CFont* pFont)
    {
        if (rc.Width() < 3 || rc.Height() < 3 || !pFont) return;
        const COLORREF fg = ExprColorForFlag(flag);
        CFont* pOld = dc.SelectObject(pFont);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 0, 0));
        CRect sh = rc; sh.OffsetRect(1, 1);
        dc.DrawText(ExprGlyphForFlag(flag), sh, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SetTextColor(fg);
        dc.DrawText(ExprGlyphForFlag(flag), rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(pOld);
    }

    static void DrawExprGlyphsAboveCell(CDC& dc, CRect cell, uint8_t expr,
        int clipTop, int clipBottom, CFont* pSymFont, int rowPitch)
    {
        if (!expr || cell.Width() < 2 || !pSymFont) return;

        const int flagCount = CountExprFlags(expr);
        if (flagCount <= 0) return;

        const int spaceAbove = cell.top - clipTop;
        if (spaceAbove < 5) return;

        const int laneW = cell.Width();
        static const uint8_t kOrder[] = {
            PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::FALL,
            PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::CRESC,
            PianoExpr::DECRESC, PianoExpr::SUSTAIN
        };

        uint8_t flags[8];
        int nFlags = 0;
        for (uint8_t flag : kOrder) {
            if (expr & flag)
                flags[nFlags++] = flag;
        }
        if (nFlags <= 0) return;

        const int gap = 1;
        const int needW = nFlags * 5 + gap * (nFlags - 1);
        const bool vertical = (nFlags > 1 && laneW < needW);

        // 記号高さもレーン幅・行高に比例して拡大（リサイズで見やすく）。
        int symH = vertical
            ? max(nFlags * 6, min(spaceAbove, nFlags * 9 + 2))
            : max(10, min(spaceAbove, max(laneW + 2, rowPitch + 4)));
        symH = min(symH, spaceAbove);

        CRect sym(cell.left, cell.top - symH, cell.right, cell.top);
        if (sym.bottom > cell.top) sym.bottom = cell.top;
        if (sym.bottom <= sym.top) return;

        if (vertical) {
            const int rowH = max(4, sym.Height() / nFlags);
            int y = sym.top;
            for (int i = 0; i < nFlags; ++i) {
                CRect badge(sym.left, y, sym.right, min(sym.bottom, y + rowH));
                if (badge.bottom > badge.top)
                    DrawExprGlyphOnNote(dc, badge, flags[i], pSymFont);
                y += rowH;
            }
        }
        else {
            int badgeW = (laneW - gap * (nFlags - 1)) / nFlags;
            if (badgeW < 3) badgeW = 3;
            int totalW = badgeW * nFlags + gap * (nFlags - 1);
            if (totalW > laneW) {
                badgeW = max(3, (laneW - gap * (nFlags - 1)) / nFlags);
                totalW = badgeW * nFlags + gap * (nFlags - 1);
            }
            int x0 = sym.left + (laneW - totalW) / 2;
            for (int i = 0; i < nFlags; ++i) {
                CRect badge(x0 + i * (badgeW + gap), sym.top,
                    x0 + i * (badgeW + gap) + badgeW, sym.bottom);
                DrawExprGlyphOnNote(dc, badge, flags[i], pSymFont);
            }
        }
    }

    static void DrawExprSymbolTop(CDC& dc, CRect cell, uint8_t expr,
        int clipTop, int clipBottom, CFont* pSymFont, int rowPitch)
    {
        DrawExprGlyphsAboveCell(dc, cell, expr, clipTop, clipBottom, pSymFont, rowPitch);
    }

    static void DrawVibratoWobble(CDC& dc, CRect rc, COLORREF col)
    {
        if (rc.Height() < 1 || rc.Width() < 2) return;
        const int amp = min(2, max(1, rc.Width() / 3));
        const int xBase = rc.right - 1;
        // SetPixel(1px毎=最も遅いGDI)を廃し、Polylineで一括描画する。
        // 縦に長い場合のスタック確保を避けるため上限を設ける。
        static const int MAXPTS = 4096;
        POINT pts[MAXPTS];
        int nPts = 0;
        for (int y = rc.top; y < rc.bottom && nPts < MAXPTS; ++y) {
            const int off = (int)(sin((y - rc.top) * 0.75) * amp);
            int x = xBase + off;
            if (x < rc.left) x = rc.left;
            if (x > rc.right - 1) x = rc.right - 1;
            pts[nPts].x = x;
            pts[nPts].y = y;
            ++nPts;
        }
        if (nPts < 2) {
            if (nPts == 1) dc.SetPixel(pts[0].x, pts[0].y, col);
            return;
        }
        HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(dc.GetSafeHdc(), col);
        dc.Polyline(pts, nPts);
        dc.SelectObject(oldPen);
    }

    static void DrawHistoryNote(CDC& dc, CRect rc, uint8_t bandMask, const float* laneStr,
        int keyIndex, float strength, float dynLevel, uint8_t expr, bool blackKey)
    {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;
        float dyn = dynLevel > 0.0f ? dynLevel : 0.5f;
        dyn = ExprDynLevel(expr, dyn);

        CRect bar = DynInsetRect(rc, dyn);
        if ((expr & PianoExpr::SLIDE) && bar.right < rc.right)
            bar.right = min(rc.right, bar.right + max(1, rc.Width() / 5));

        DrawLaneFill(dc, bar, bandMask, laneStr, keyIndex, strength, blackKey);

        if (expr & PianoExpr::VIBRATO)
            DrawVibratoWobble(dc, bar, ExprPrimaryColor(PianoExpr::VIBRATO));
        if ((expr & PianoExpr::SCOOP) && bar.Width() >= 2)
            dc.FillSolidRect(CRect(bar.left, bar.top, bar.left + max(1, bar.Width() / 3), bar.bottom),
                ExprPrimaryColor(PianoExpr::SCOOP));
        if ((expr & PianoExpr::ACCENT) && bar.Height() >= 1)
            dc.FillSolidRect(CRect(bar.left, bar.top, bar.right, bar.top + 1),
                ExprColorForFlag(PianoExpr::ACCENT));
        if (expr & PianoExpr::SLIDE)
            dc.FillSolidRect(CRect(bar.right - 1, bar.top, bar.right, bar.bottom),
                ExprColorForFlag(PianoExpr::SLIDE));
        if ((expr & PianoExpr::FALL) && bar.Width() >= 2)
            dc.FillSolidRect(CRect(bar.right - max(1, bar.Width() / 3), bar.top, bar.right, bar.bottom),
                ExprColorForFlag(PianoExpr::FALL));
        if ((expr & PianoExpr::SUSTAIN) && bar.Height() >= 3) {
            const int mid = bar.top + bar.Height() / 2;
            dc.FillSolidRect(CRect(bar.left, mid, bar.right, mid + 1),
                ExprColorForFlag(PianoExpr::SUSTAIN));
        }
    }
}

void CPianoRoll::DrawChannelDbBars(CDC& dc, const CRect& rc, const float* chFill, int chCount) const
{
    if (!chFill || chCount <= 0 || rc.Width() < 4 || rc.Height() < 2) return;
    const int n = (chCount > PIANO_METER_CH_MAX) ? PIANO_METER_CH_MAX : chCount;
    CRect inner(rc.left + 2, rc.top + 1, rc.right - 2, rc.bottom - 1);
    if (inner.Width() < 4 || inner.Height() < 2) return;
    dc.FillSolidRect(inner, RGB(100, 100, 106));
    const int labelW = (n <= 2) ? min(14, inner.Width() / 4) : 0;
    const int barLeft = inner.left + labelW;
    const int barWMax = inner.right - barLeft;
    if (barWMax < 2) return;
    for (int c = 0; c < n; ++c) {
        CRect row(inner.left, inner.top, inner.right, inner.bottom);
        row.top = inner.top + (inner.Height() * c) / n;
        row.bottom = inner.top + (inner.Height() * (c + 1)) / n;
        if (c > 0) row.top += 1; if (c + 1 < n) row.bottom -= 1;
        if (row.bottom <= row.top) row.bottom = row.top + 1;
        CRect track(barLeft, row.top, inner.right, row.bottom);
        dc.FillSolidRect(track, RGB(62, 62, 68));
        float fill = chFill[c]; if (fill < 0.0f)fill = 0.0f; if (fill > 1.0f)fill = 1.0f;
        int barW = (int)(barWMax * fill + 0.5f);
        if (fill > 0.02f && barW < 2) barW = 2;
        if (barW > 0) {
            COLORREF col = RGB(70, 175, 95);
            if (n == 2 && c == 1) col = RGB(175, 120, 70);
            else if (n > 2) col = RGB(90 + c * 12, 140, 180 - c * 8);
            dc.FillSolidRect(CRect(track.left, track.top, track.left + barW, track.bottom), col);
        }
        if (labelW > 0 && row.Height() >= 4 && m_fontMeterTag.GetSafeHandle()) {
            CFont* pOld = dc.SelectObject(const_cast<CFont*>(&m_fontMeterTag));
            dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(230, 230, 235));
            CRect lr(row.left, row.top, barLeft, row.bottom);
            const wchar_t* tag = (n == 1) ? L"M" : ((c == 0) ? L"L" : L"R");
            dc.DrawText(tag, -1, lr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dc.SelectObject(pOld);
        }
    }
}

void CPianoRoll::DetachForDestroy()
{
    KillTimer(1);
    m_feedEnabled = false;
    m_paintDisabled = true;
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_analysisPresentDirty, 0);
    StopAnalysisWorker();
    // 破棄前に投稿済みメッセージを捨てる(アナライザと同様。閉じてすぐ開き直すと稀に落ちる)
    if (::IsWindow(m_hWnd)) {
        MSG msg;
        while (PeekMessage(&msg, m_hWnd, WM_PIANOROLL_ANALYSIS_DONE, WM_PIANOROLL_ANALYSIS_DONE, PM_REMOVE)) {}
        while (PeekMessage(&msg, m_hWnd, WM_PIANOROLL_SYNC, WM_PIANOROLL_SYNC, PM_REMOVE)) {}
    }
    InterlockedExchange(&m_syncPosted, 0);
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_analysisPresentDirty, 0);
    EnterCriticalSection(&m_cs);
    m_framesPending = 0;
    LeaveCriticalSection(&m_cs);
    ReleasePaintBuffers();
}

void CPianoRoll::ReleasePaintBuffers()
{
    if (m_rollScratchDC.GetSafeHdc()) {
        if (m_rollScratchOldBmp) m_rollScratchDC.SelectObject(m_rollScratchOldBmp);
        m_rollScratchDC.DeleteDC();
    }
    m_rollScratchBmp.DeleteObject();
    m_rollScratchOldBmp = nullptr;

    if (m_rollDC.GetSafeHdc()) {
        if (m_rollOldBmp) m_rollDC.SelectObject(m_rollOldBmp);
        m_rollDC.DeleteDC();
    }
    m_rollBmp.DeleteObject();
    m_rollOldBmp = nullptr;
    m_rollW = 0;
    m_rollH = 0;
    m_rollReady = false;
    m_rollScrollValid = false;

    if (m_keyDC.GetSafeHdc()) {
        if (m_keyOldBmp) m_keyDC.SelectObject(m_keyOldBmp);
        m_keyDC.DeleteDC();
    }
    m_keyBmp.DeleteObject();
    m_keyOldBmp = nullptr;
    m_keyW = 0;
    m_keyH = 0;
    m_keyBufReady = false;

    if (m_frameDC.GetSafeHdc()) {
        if (m_frameOldBmp) m_frameDC.SelectObject(m_frameOldBmp);
        m_frameDC.DeleteDC();
    }
    m_frameBmp.DeleteObject();
    m_frameOldBmp = nullptr;
    m_frameW = 0;
    m_frameH = 0;

#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
    m_chromaReady = false;
    m_chromaW = 0;
    m_chromaH = 0;
#endif

    if (m_fontKeyNote.GetSafeHandle()) m_fontKeyNote.DeleteObject();
    if (m_fontKeyOct.GetSafeHandle()) m_fontKeyOct.DeleteObject();
    if (m_fontMeterTag.GetSafeHandle()) m_fontMeterTag.DeleteObject();
    if (m_fontExprSymbol.GetSafeHandle()) m_fontExprSymbol.DeleteObject();
    if (m_fontExprSymbolCompact.GetSafeHandle()) m_fontExprSymbolCompact.DeleteObject();
    if (m_fontExprLegend.GetSafeHandle()) m_fontExprLegend.DeleteObject();
    m_paintFontsReady = false;
    m_fontCacheClientW = 0;
    m_fontCacheKeyH = 0;
    m_fontCacheRollH = 0;

    ReleaseExprLegendCache();
}

bool CPianoRoll::EnsureRollBuffer(CDC& refDC, int width, int rollH)
{
    if (width <= 0 || rollH <= 0) return false;
    if (m_rollW == width && m_rollH == rollH && m_rollDC.GetSafeHdc())
        return true;

    if (m_rollDC.GetSafeHdc()) {
        if (m_rollOldBmp) m_rollDC.SelectObject(m_rollOldBmp);
        m_rollDC.DeleteDC();
    }
    m_rollBmp.DeleteObject();
    m_rollOldBmp = nullptr;

    if (m_rollScratchDC.GetSafeHdc()) {
        if (m_rollScratchOldBmp) m_rollScratchDC.SelectObject(m_rollScratchOldBmp);
        m_rollScratchDC.DeleteDC();
    }
    m_rollScratchBmp.DeleteObject();
    m_rollScratchOldBmp = nullptr;

    if (!m_rollDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_rollScratchDC.CreateCompatibleDC(&refDC)) {
        m_rollDC.DeleteDC();
        return false;
    }
    if (!m_rollBmp.CreateCompatibleBitmap(&refDC, width, rollH)) {
        m_rollScratchDC.DeleteDC();
        m_rollDC.DeleteDC();
        return false;
    }
    if (!m_rollScratchBmp.CreateCompatibleBitmap(&refDC, width, rollH)) {
        m_rollBmp.DeleteObject();
        m_rollScratchDC.DeleteDC();
        m_rollDC.DeleteDC();
        return false;
    }
    m_rollOldBmp = m_rollDC.SelectObject(&m_rollBmp);
    m_rollScratchOldBmp = m_rollScratchDC.SelectObject(&m_rollScratchBmp);
    m_rollW = width;
    m_rollH = rollH;
    m_rollReady = false;
    m_rollScrollValid = false;
    return true;
}

bool CPianoRoll::EnsureKeyBuffer(CDC& refDC, int width, int keySectionH)
{
    if (width <= 0 || keySectionH <= 0) return false;
    if (m_keyW == width && m_keyH == keySectionH && m_keyDC.GetSafeHdc())
        return true;

    if (m_keyDC.GetSafeHdc()) {
        if (m_keyOldBmp) m_keyDC.SelectObject(m_keyOldBmp);
        m_keyDC.DeleteDC();
    }
    m_keyBmp.DeleteObject();
    m_keyOldBmp = nullptr;

    if (!m_keyDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_keyBmp.CreateCompatibleBitmap(&refDC, width, keySectionH)) {
        m_keyDC.DeleteDC();
        return false;
    }
    m_keyOldBmp = m_keyDC.SelectObject(&m_keyBmp);
    m_keyW = width;
    m_keyH = keySectionH;
    m_keyBufReady = false;
    return true;
}

void CPianoRoll::EnsurePaintFonts(int clientW, int keyH, int rollH)
{
    if (m_paintFontsReady &&
        m_fontCacheClientW == clientW &&
        m_fontCacheKeyH == keyH &&
        m_fontCacheRollH == rollH)
        return;

    const int rowPitch = HistoryRowPitch(rollH);
    const int lanePx = max(5, clientW / 88);
    const int notePx = max(9, min(14, clientW / 52));
    const int octPx = max(8, min(12, clientW / 52));
    const int tagPx = max(7, min(11, keyH / 4));
    // 表現記号: ウィンドウを広げたらレーン幅(lanePx)・行高(rowPitch)に比例して拡大。
    // 以前は上限20/10で頭打ちだったので引き上げ、リサイズで見やすくなるようにする。
    const int symPx = max(11, min(30, max(lanePx * 2, rowPitch + 6)));
    const int symCompactPx = max(7, min(18, lanePx + 2));
    const int legPx = max(7, min(11, min(clientW / 58, rollH / 15)));

    if (m_fontKeyNote.GetSafeHandle()) m_fontKeyNote.DeleteObject();
    if (m_fontKeyOct.GetSafeHandle()) m_fontKeyOct.DeleteObject();
    if (m_fontMeterTag.GetSafeHandle()) m_fontMeterTag.DeleteObject();
    if (m_fontExprSymbol.GetSafeHandle()) m_fontExprSymbol.DeleteObject();
    if (m_fontExprSymbolCompact.GetSafeHandle()) m_fontExprSymbolCompact.DeleteObject();
    if (m_fontExprLegend.GetSafeHandle()) m_fontExprLegend.DeleteObject();

    m_fontKeyNote.CreateFont(-notePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontKeyOct.CreateFont(-octPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontMeterTag.CreateFont(-tagPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m_fontExprSymbol.CreateFont(-symPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    m_fontExprSymbolCompact.CreateFont(-symCompactPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    m_fontExprLegend.CreateFont(-legPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    m_fontCacheClientW = clientW;
    m_fontCacheKeyH = keyH;
    m_fontCacheRollH = rollH;
    m_paintFontsReady = true;
    // フォント差し替え後は凡例キャッシュを作り直す
    ReleaseExprLegendCache();
}

void CPianoRoll::GetExprLegendPanelRect(int rollW, int rollH, CRect& panel) const
{
    panel.SetRectEmpty();
    if (rollW < 72 || rollH < 48) return;

    static const int kItemCount = 8;
    const int pad = max(3, min(6, rollW / 90));
    int lineH = max(9, min(16, rollH / 14));
    int titleH = max(10, min(14, lineH + 1));
    int badgeW = max(10, min(15, rollW / 28));

    int cols = 1;
    const int fullH = pad * 2 + titleH + kItemCount * lineH;
    if (rollW >= 210 && rollH < fullH - 8)
        cols = 2;

    const int rows = (kItemCount + cols - 1) / cols;
    int panelW = min(rollW - 8, max(88, rollW * 55 / 100));
    if (cols == 2)
        panelW = min(panelW, min(240, rollW - 8));
    else
        panelW = min(panelW, min(200, rollW - 8));

    int panelH = pad * 2 + titleH + rows * lineH;
    const int maxH = rollH * 42 / 100;
    if (panelH > maxH) {
        lineH = max(8, (maxH - pad * 2 - titleH) / rows);
        panelH = pad * 2 + titleH + rows * lineH;
    }
    if (rollH < 100) {
        titleH = 0;
        lineH = max(8, min(12, rollH / 8));
        badgeW = max(8, min(12, rollW / 40));
        panelH = pad * 2 + lineH;
        panelW = min(rollW - 8, pad * 2 + kItemCount * (badgeW + 2));
    }

    panel.SetRect(4, 4, 4 + panelW, 4 + panelH);
}

void CPianoRoll::ReleaseExprLegendCache() const
{
    if (m_legendDC.GetSafeHdc()) {
        if (m_legendOldBmp) m_legendDC.SelectObject(m_legendOldBmp);
        m_legendDC.DeleteDC();
    }
    m_legendBmp.DeleteObject();
    m_legendOldBmp = nullptr;
    m_legendW = m_legendH = 0;
    m_legendReady = false;
    m_legendCacheRollW = m_legendCacheRollH = -1;

    if (m_legendBgDC.GetSafeHdc()) {
        if (m_legendBgOldBmp) m_legendBgDC.SelectObject(m_legendBgOldBmp);
        m_legendBgDC.DeleteDC();
    }
    m_legendBgBmp.DeleteObject();
    m_legendBgOldBmp = nullptr;
    m_legendBgW = m_legendBgH = 0;
}

bool CPianoRoll::EnsureExprLegendCache(CDC& refDC, int rollW, int rollH) const
{
    CRect panel;
    GetExprLegendPanelRect(rollW, rollH, panel);
    if (panel.IsRectEmpty()) return false;
    if (!m_paintFontsReady || !m_fontExprLegend.GetSafeHandle() || !m_fontExprSymbol.GetSafeHandle())
        return false;

    const int pw = panel.Width();
    const int ph = panel.Height();
    if (pw <= 0 || ph <= 0) return false;

    // サイズ(=フォントサイズ依存)が変わったら作り直す。内容は静的なので一度だけ描画。
    if (m_legendReady && m_legendCacheRollW == rollW && m_legendCacheRollH == rollH
        && m_legendW == pw && m_legendH == ph && m_legendDC.GetSafeHdc())
        return true;

    // legendBg（焼き込み退避用）は消さない。オーバーレイキャッシュだけ作り直す。
    if (m_legendDC.GetSafeHdc()) {
        if (m_legendOldBmp) m_legendDC.SelectObject(m_legendOldBmp);
        m_legendDC.DeleteDC();
    }
    m_legendBmp.DeleteObject();
    m_legendOldBmp = nullptr;
    m_legendW = m_legendH = 0;
    m_legendReady = false;
    m_legendCacheRollW = m_legendCacheRollH = -1;

    if (!m_legendDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_legendBmp.CreateCompatibleBitmap(&refDC, pw, ph)) {
        m_legendDC.DeleteDC();
        return false;
    }
    m_legendOldBmp = m_legendDC.SelectObject(&m_legendBmp);
    // 文字・バッジ・枠線のみキャッシュ。下地はパネル色キーで抜き、毎フレームの半透明塗りと合成する。
    // （旧マゼンタキーはピンク文字・ClearType縁を抜いて穴が開いていた）
    m_legendDC.FillSolidRect(0, 0, pw, ph, RGB(14, 14, 20));
    DrawExprLegendContent(m_legendDC, rollW, rollH, CRect(0, 0, pw, ph), false);

    m_legendW = pw;
    m_legendH = ph;
    m_legendCacheRollW = rollW;
    m_legendCacheRollH = rollH;
    m_legendReady = true;
    return true;
}

// 凡例パネル背景を半透明で塗る(下のバーを透かす)。1x1のソースを引き伸ばして
// AlphaBlend する軽量実装。DC/ビットマップは再利用して分単位の GDI 断片化を防ぐ。
static void PianoFillRectAlpha(CDC& dc, const CRect& rc, COLORREF clr, BYTE alpha)
{
    if (rc.Width() <= 0 || rc.Height() <= 0) return;
    static CDC s_mem;
    static CBitmap s_bmp;
    static CBitmap* s_old = nullptr;
    static bool s_ready = false;
    static COLORREF s_clr = (COLORREF)-1;
    if (!s_ready) {
        if (!s_mem.CreateCompatibleDC(&dc)) return;
        if (!s_bmp.CreateCompatibleBitmap(&dc, 1, 1)) { s_mem.DeleteDC(); return; }
        s_old = s_mem.SelectObject(&s_bmp);
        s_ready = true;
        s_clr = (COLORREF)-1;
    }
    if (s_clr != clr) {
        s_mem.SetPixelV(0, 0, clr);
        s_clr = clr;
    }
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    dc.AlphaBlend(rc.left, rc.top, rc.Width(), rc.Height(), &s_mem, 0, 0, 1, 1, bf);
}

void CPianoRoll::DrawExprLegend(CDC& dc, int rollW, int rollH, bool blitToDest) const
{
    CRect panel;
    GetExprLegendPanelRect(rollW, rollH, panel);
    if (panel.IsRectEmpty()) return;
    const int pw = panel.Width(), ph = panel.Height();
    if (pw <= 0 || ph <= 0) return;

    // 毎呼び出し CreateCompatibleBitmap すると長時間で GDI が断片化する。
    // 退避用 m_legendBgDC を合成バッファとして再利用し、最終面へ1回 BitBlt する。
    bool bgOk = (m_legendBgDC.GetSafeHdc() && m_legendBgW == pw && m_legendBgH == ph);
    if (!bgOk) {
        if (m_legendBgDC.GetSafeHdc()) {
            if (m_legendBgOldBmp) m_legendBgDC.SelectObject(m_legendBgOldBmp);
            m_legendBgDC.DeleteDC();
        }
        m_legendBgBmp.DeleteObject();
        m_legendBgOldBmp = nullptr;
        m_legendBgW = m_legendBgH = 0;
        if (m_legendBgDC.CreateCompatibleDC(&dc) && m_legendBgBmp.CreateCompatibleBitmap(&dc, pw, ph)) {
            m_legendBgOldBmp = m_legendBgDC.SelectObject(&m_legendBgBmp);
            m_legendBgW = pw;
            m_legendBgH = ph;
            bgOk = true;
        }
    }
    if (!bgOk) {
        if (blitToDest)
            DrawExprLegendContent(dc, rollW, rollH, panel);
        return;
    }

    if (m_rollDC.GetSafeHdc())
        m_legendBgDC.BitBlt(0, 0, pw, ph, const_cast<CDC*>(&m_rollDC), panel.left, panel.top, SRCCOPY);
    else
        m_legendBgDC.FillSolidRect(0, 0, pw, ph, RGB(20, 20, 20));

    PianoFillRectAlpha(m_legendBgDC, CRect(0, 0, pw, ph), RGB(14, 14, 20), 170);
    if (EnsureExprLegendCache(dc, rollW, rollH) && m_legendDC.GetSafeHdc()) {
        m_legendBgDC.TransparentBlt(0, 0, pw, ph,
            const_cast<CDC*>(&m_legendDC), 0, 0, pw, ph, RGB(14, 14, 20));
    }
    else {
        DrawExprLegendContent(m_legendBgDC, rollW, rollH, CRect(0, 0, pw, ph), false);
    }
    if (blitToDest)
        dc.BitBlt(panel.left, panel.top, pw, ph, const_cast<CDC*>(&m_legendBgDC), 0, 0, SRCCOPY);
}

void CPianoRoll::DrawExprLegendContent(CDC& dc, int rollW, int rollH, const CRect& panel, bool fillPanelBg) const
{
    using namespace PianoDraw;
    if (panel.IsRectEmpty()) return;
    if (!m_paintFontsReady || !m_fontExprLegend.GetSafeHandle() || !m_fontExprSymbol.GetSafeHandle())
        return;

    static const uint8_t kFlags[] = {
        PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::FALL,
        PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::CRESC,
        PianoExpr::DECRESC, PianoExpr::SUSTAIN
    };
    static const wchar_t* kLabels[] = {
        LL14(L"アクセント", L"Accent", L"Accent", L"Accento", L"Acento", L"액센트", L"重音", L"نبرة", L"Акцент", L"Akzent", L"Acento", L"Accent", L"Akcent", L"Aksan"),
        LL14(L"スクープ", L"Scoop", L"Scoop", L"Scoop", L"Scoop", L"스쿱", L"滑音(上)", L"Scoop", L"Скуп", L"Scoop", L"Scoop", L"Scoop", L"Scoop", L"Scoop"),
        LL14(L"フォール", L"Fall", L"Chute", L"Fall", L"Caída", L"하강", L"滑音(下)", L"Fall", L"Падение", L"Fall", L"Queda", L"Fall", L"Spadek", L"Düşüş"),
        LL14(L"スライド", L"Slide", L"Glissé", L"Slide", L"Desliz", L"슬라이드", L"滑音", L"Slide", L"Слайд", L"Slide", L"Slide", L"Slide", L"Slide", L"Slide"),
        LL14(L"ビブラート", L"Vibrato", L"Vibrato", L"Vibrato", L"Vibrato", L"비브라토", L"颤音", L"Vibrato", L"Вибрато", L"Vibrato", L"Vibrato", L"Vibrato", L"Wibrato", L"Vibrato"),
        LL14(L"クレッシェンド", L"Cresc.", L"Cresc.", L"Cresc.", L"Cresc.", L"크레셴도", L"渐强", L"Cresc.", L"Крещ.", L"Cresc.", L"Cresc.", L"Cresc.", L"Cresc.", L"Cresc."),
        LL14(L"デクレッシェンド", L"Decresc.", L"Decresc.", L"Decresc.", L"Decresc.", L"데크레셴도", L"渐弱", L"Decresc.", L"Дим.", L"Decresc.", L"Decresc.", L"Decresc.", L"Decresc.", L"Decresc."),
        LL14(L"サステイン", L"Sustain", L"Sustain", L"Sustain", L"Sustain", L"서스테인", L"延音", L"Sustain", L"Длит.", L"Sustain", L"Sustain", L"Sustain", L"Sustain", L"Sustain")
    };
    const int n = (int)(sizeof(kFlags) / sizeof(kFlags[0]));
    const int pad = max(3, min(6, rollW / 90));
    const int lineH = max(8, (panel.Height() - pad * 2) / (rollH < 100 ? 1 : (n + 1)));
    const int titleH = (rollH < 100) ? 0 : max(10, min(14, lineH + 1));
    const int badgeW = max(8, min(15, rollW / 28));
    const bool iconsOnly = (rollH < 100);
    int cols = 1;
    if (!iconsOnly && rollW >= 210 && panel.Height() < pad * 2 + titleH + n * lineH - 4)
        cols = 2;
    const int rows = iconsOnly ? 1 : (n + cols - 1) / cols;

    // 背景は半透明で塗り、下を流れるバーがうっすら透ける(可読性は保つ濃さ)。
    if (fillPanelBg)
        PianoFillRectAlpha(dc, panel, RGB(14, 14, 20), 170);
    HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
    ::SetDCPenColor(dc.GetSafeHdc(), RGB(70, 70, 82));
    dc.MoveTo(panel.left, panel.bottom - 1); dc.LineTo(panel.left, panel.top);
    dc.LineTo(panel.right - 1, panel.top); dc.LineTo(panel.right - 1, panel.bottom - 1);
    dc.LineTo(panel.left, panel.bottom - 1);
    dc.SelectObject(oldPen);

    CFont* pLeg = CFont::FromHandle((HFONT)m_fontExprLegend.GetSafeHandle());
    CFont* pSym = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
    CFont* pOldF = dc.SelectObject(pLeg);
    dc.SetBkMode(TRANSPARENT);

    int y = panel.top + pad;
    if (!iconsOnly) {
        dc.SetTextColor(RGB(210, 210, 220));
        CRect titleR(panel.left + pad, y, panel.right - pad, y + titleH);
        dc.DrawText(LL14(L"記号の意味", L"Symbol legend", L"Légende", L"Legenda simboli", L"Leyenda", L"기호 설명", L"符号说明", L"دليل الرموز", L"Обозначения", L"Symbollegende", L"Legenda", L"Symbolen", L"Legenda symboli", L"Semboller"),
            titleR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        y += titleH;
    }

    if (iconsOnly) {
        const int gap = 2;
        int bw = (panel.Width() - pad * 2 - gap * (n - 1)) / n;
        if (bw < 8) bw = 8;
        int x = panel.left + pad;
        for (int i = 0; i < n; ++i) {
            CRect badgeR(x, y, min(panel.right - pad, x + bw), y + lineH);
            DrawExprBadgePanel(dc, badgeR, kFlags[i], pSym);
            x += bw + gap;
        }
    }
    else if (cols == 2) {
        const int colW = (panel.Width() - pad * 2) / 2;
        for (int i = 0; i < n; ++i) {
            const int col = i / rows;
            const int row = i % rows;
            const int x0 = panel.left + pad + col * colW;
            const int y0 = y + row * lineH;
            CRect badgeR(x0, y0 + 1, x0 + badgeW, y0 + lineH - 1);
            DrawExprBadgePanel(dc, badgeR, kFlags[i], pSym);
            CRect labelR(badgeR.right + 4, y0, x0 + colW - 2, y0 + lineH);
            dc.SetTextColor(ExprColorForFlag(kFlags[i]));
            dc.DrawText(kLabels[i], labelR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }
    else {
        for (int i = 0; i < n; ++i) {
            CRect badgeR(panel.left + pad, y + 1, panel.left + pad + badgeW, y + lineH - 1);
            DrawExprBadgePanel(dc, badgeR, kFlags[i], pSym);
            CRect labelR(badgeR.right + 4, y, panel.right - pad, y + lineH);
            dc.SetTextColor(ExprColorForFlag(kFlags[i]));
            dc.DrawText(kLabels[i], labelR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            y += lineH;
        }
    }
    dc.SelectObject(pOldF);
}

void CPianoRoll::DrawHistoryGrid(CDC& dc, int width, int yFrom, int yTo) const
{
    if (yFrom < 0) yFrom = 0;
    if (yFrom >= yTo) return;
    // 毎フレーム CreatePen すると長時間で GDI が断片化し EQ 描画まで重くなる
    HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
    ::SetDCPenColor(dc.GetSafeHdc(), RGB(34, 34, 34));
    for (int i = 1; i < KEY_COUNT; ++i) {
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        if (xR <= xL) continue;   // 表示範囲外(88鍵表示時の下端など)
        dc.MoveTo(xL, yFrom);
        dc.LineTo(xL, yTo);
    }
    dc.SelectObject(oldPen);
}

int CPianoRoll::HistoryRowPitch(int rollH) const
{
    int yTop, yBot;
    GetHistoryRowBounds(rollH, 0, yTop, yBot);
    return max(1, yBot - yTop);
}

int CPianoRoll::HistoryScrollPx(int rollH, int rowsToScroll) const
{
    if (rowsToScroll <= 0 || rollH <= 0) return 0;
    const int maxRows = (int)MAX_HISTORY;
    if (rowsToScroll >= maxRows) return rollH;
    int yTop0, yBot0, yTopN, yBotN;
    GetHistoryRowBounds(rollH, 0, yTop0, yBot0);
    GetHistoryRowBounds(rollH, rowsToScroll, yTopN, yBotN);
    return yTop0 - yTopN;
}

void CPianoRoll::GetHistoryRowBounds(int rollH, int rowFromBottom, int& yTop, int& yBot) const
{
    if (rowFromBottom < 0) rowFromBottom = 0;
    if (rowFromBottom >= (int)MAX_HISTORY || rollH <= 0) {
        yTop = yBot = 0;
        return;
    }
    const int maxRows = (int)MAX_HISTORY;
    const int s = maxRows - 1 - rowFromBottom;
    yTop = s * rollH / maxRows;
    yBot = (s + 1) * rollH / maxRows;
    if (yTop < 0) yTop = 0;
    if (yBot > rollH) yBot = rollH;
    if (yBot <= yTop) yBot = yTop + 1;
}

void CPianoRoll::DrawHistoryRowAt(CDC& dc, int width, int yTop, int yBot, const NoteFrame& frame) const
{
    using namespace PianoDraw;
    if (yBot <= yTop) return;
    const int rowPitch = yBot - yTop;

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!frame.active[i]) continue;
        const int midi = MIDI_BASE + i;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        const uint8_t bMask = frame.bandMask[i] ? frame.bandMask[i] : (uint8_t)(1u << KeyBandIndex(i));
        DrawHistoryNote(dc, CRect(xL + 1, yTop, xR - 1, yBot), bMask, frame.laneStrength[i],
            i, frame.strength[i], frame.dynLevel[i], frame.expr[i], IsBlackKey(midi));
    }

    // 再アタック(タイ連結中の同鍵連打)が起きた鍵は、バーの左端に細い白線を
    // 一本引いて「ここでノートが切り替わった」ことを見た目でも分かるようにする。
    // ゲート100で沈黙区間がなくても、この線でO6L4CCCCCCのような連打を区別できる。
    HGDIOBJ oldPen = dc.SelectObject(::GetStockObject(DC_PEN));
    ::SetDCPenColor(dc.GetSafeHdc(), RGB(255, 255, 255));
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!frame.active[i] || !frame.reattack[i]) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        if (xR - xL < 2) continue;
        dc.MoveTo(xL + 1, yTop);
        dc.LineTo(xL + 1, yBot);
    }
    dc.SelectObject(oldPen);

    if (!m_paintFontsReady || !m_fontExprSymbol.GetSafeHandle()) return;
    CFont* pSymFont = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (!frame.active[i] || !frame.expr[i]) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        const int laneW = xR - xL - 2;
        if (laneW < 14 && m_fontExprSymbolCompact.GetSafeHandle())
            pSymFont = CFont::FromHandle((HFONT)m_fontExprSymbolCompact.GetSafeHandle());
        else
            pSymFont = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
        DrawExprSymbolTop(dc, CRect(xL + 1, yTop, xR - 1, yBot), frame.expr[i],
            0, yBot, pSymFont, rowPitch);
    }
}

void CPianoRoll::DrawHistoryRow(CDC& dc, int width, int rollH, size_t rowIndex, const NoteFrame& frame) const
{
    int yTop, yBot;
    GetHistoryRowBounds(rollH, (int)rowIndex, yTop, yBot);
    DrawHistoryRowAt(dc, width, yTop, yBot, frame);
}

void CPianoRoll::DrawHistoryArea(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const
{
    dc.FillSolidRect(0, 0, width, rollH, RGB(20, 20, 20));
    DrawHistoryGrid(dc, width, 0, rollH);
    // row0=live（DrawPlayheadRow）。row r>=1 には hist[r-1]（PushFrame で row0 に入った直前フレーム）
    if (!hist) return;
    for (int r = 1; r < histCount && r < (int)MAX_HISTORY; ++r)
        DrawHistoryRow(dc, width, rollH, r, hist[r - 1]);

    DrawPitchTransitions(dc, width, rollH, histCount, hist);
}

// 音階移行(スライド/フォール/スクープ)の斜め描画:
// 隣接フレーム間で音が隣接音階へ移った箇所を、行境界をまたいで斜めの帯で繋ぎ、
// 縦バーの段差ではなく滑らかな移行に見せる（既存の縦バーはそのまま＝互換性重視）。全音域。
void CPianoRoll::DrawPitchTransitions(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const
{
    if (!hist || histCount < 3) return;
    const int maxR = (histCount < (int)MAX_HISTORY) ? histCount : (int)MAX_HISTORY;
    const uint8_t kTransMask = PianoExpr::SLIDE | PianoExpr::FALL | PianoExpr::SCOOP;

    CGdiObject* pOldBrush = dc.SelectStockObject(DC_BRUSH);
    CGdiObject* pOldPen = dc.SelectStockObject(DC_PEN);
    for (int r = 1; r + 1 < maxR; ++r) {
        const NoteFrame& fNew = hist[r - 1]; // 下(新しい)行 = row r
        const NoteFrame& fOld = hist[r];     // 上(古い)行 = row r+1
        int yTopNew, yBotNew, yTopOld, yBotOld;
        GetHistoryRowBounds(rollH, r, yTopNew, yBotNew);
        GetHistoryRowBounds(rollH, r + 1, yTopOld, yBotOld);
        const int midNew = (yTopNew + yBotNew) / 2;
        const int midOld = (yTopOld + yBotOld) / 2;

        for (int j = 0; j < KEY_COUNT; ++j) {
            if (!fNew.active[j] || !(fNew.expr[j] & kTransMask)) continue;
            // 移行元(古い行)で隣接(±1〜±2半音)に鳴っていた音を探す
            int src = -1;
            for (int d = 1; d <= 2 && src < 0; ++d) {
                if (j - d >= 0 && fOld.active[j - d]) src = j - d;
                else if (j + d < KEY_COUNT && fOld.active[j + d]) src = j + d;
            }
            if (src < 0 || src == j) continue;

            int xLs, xRs, xLd, xRd;
            GetChromaticKeyRect(src, width, xLs, xRs);
            GetChromaticKeyRect(j, width, xLd, xRd);
            if (xRs <= xLs || xRd <= xLd) continue;

            const COLORREF col = PianoDraw::LocalKeyColor(j, fNew.strength[j], false);
            // DC_PEN/DC_BRUSH で色だけ差し替え(ループ内 CreatePen/Brush を避ける)
            ::SetDCBrushColor(dc.GetSafeHdc(), col);
            ::SetDCPenColor(dc.GetSafeHdc(), col);
            POINT pts[4] = {
                { xLs + 1, midOld }, { xRs - 1, midOld },
                { xRd - 1, midNew }, { xLd + 1, midNew }
            };
            dc.Polygon(pts, 4);
        }
    }
    if (pOldBrush) dc.SelectObject(pOldBrush);
    if (pOldPen) dc.SelectObject(pOldPen);
}

void CPianoRoll::ComposeRollBuffer(CDC& dc, int width, int rollH,
    int histCount, const NoteFrame* hist, const NoteFrame& live) const
{
    DrawHistoryArea(dc, width, rollH, histCount, hist);
    DrawPlayheadRow(dc, width, rollH, live);
    // 凡例(記号の意味)はロールバッファへ焼き込まず、OnPaint で最終画面へ
    // 半透明オーバーレイとして重ねる(背景の黒をアルファ化して下のバーを透かす)。
}

void CPianoRoll::DrawPlayheadRow(CDC& dc, int width, int rollH, const NoteFrame& live) const
{
    int yTop, yBot;
    GetHistoryRowBounds(rollH, 0, yTop, yBot);
    if (yBot <= yTop) return;

    dc.FillSolidRect(0, yTop, width, yBot - yTop, RGB(20, 20, 20));
    DrawHistoryGrid(dc, width, yTop, yBot);
    DrawHistoryRowAt(dc, width, yTop, yBot, live);
}

// pendingCount 行分を1回の BitBlt でスクロールし、空いた帯に履歴+live を描く。
// 旧: pending 回フルバッファ転送 → 遅延時に O(n) で重くなり EQ を圧迫した。
bool CPianoRoll::TryAdvanceRollBuffer(int width, int rollH, int histCount, const NoteFrame* hist,
    int pendingCount, const NoteFrame& live)
{
    m_lastScrollPx = 0;
    m_lastScrollHealTop = 0;
    if (!m_rollReady || rollH <= 0)
        return false;
    if (!m_rollDC.GetSafeHdc() || !m_rollScratchDC.GetSafeHdc())
        return false;

    int n = pendingCount;
    if (n < 1) n = 1;
    if (n > 3) n = 3;

    const int scrollPx = HistoryScrollPx(rollH, n);
    const int preserveH = rollH - scrollPx;
    if (scrollPx <= 0 || preserveH <= 0)
        return false;

    int yBandTop = 0, yBandBot = 0;
    GetHistoryRowBounds(rollH, n - 1, yBandTop, yBandBot);
    if (yBandTop < 0) yBandTop = 0;
    if (rollH - yBandTop <= 0)
        return false;

    // A) 履歴ピクセルを scrollPx 分まとめて繰り上げ（n 回分を1回の BitBlt）
    m_rollScratchDC.BitBlt(0, 0, width, preserveH, &m_rollDC, 0, scrollPx, SRCCOPY);

    // B) 空いた帯にグリッド + 各行
    m_rollScratchDC.FillSolidRect(0, yBandTop, width, rollH - yBandTop, RGB(20, 20, 20));
    DrawHistoryGrid(m_rollScratchDC, width, yBandTop, rollH);

    // 連続 TryAdvance と同じ並び: row r(r>=1) ← hist[r-1]、row0 ← live
    // （ComposeRollBuffer/DrawHistoryArea と同一。hist[r] だと1フレーム古く、
    //  hist 不足時は live を複数行に描いて縦に太く見える）
    for (int r = n - 1; r >= 1; --r) {
        const NoteFrame& fr = (hist && histCount >= r) ? hist[r - 1] : live;
        int yTop, yBot;
        GetHistoryRowBounds(rollH, r, yTop, yBot);
        if (yBot > yTop)
            DrawHistoryRowAt(m_rollScratchDC, width, yTop, yBot, fr);
    }
    DrawPlayheadRow(m_rollScratchDC, width, rollH, live);

    m_rollDC.BitBlt(0, 0, width, rollH, &m_rollScratchDC, 0, 0, SRCCOPY);
    m_lastScrollPx = scrollPx;
    m_lastScrollHealTop = yBandTop;
    return true;
}

void CPianoRoll::UpdatePianoRollTimer()
{
    KillTimer(1);
    int ms = savedata.ms2;
    if (ms < 16) ms = 16;
    if (ms > 960) ms = 960;
    SetTimer(1, (UINT)ms, nullptr);
}

void CPianoRoll::RequestSyncFromMainUi()
{
    if (!::IsWindow(m_hWnd)) return;
    const DWORD now = GetTickCount();
    // 提示フラグ固着の回復のみ。PCM/メーター同期は止めない（アナライザと同じ分離）。
    // 旧実装は analysisDonePosted 中に Sync 全体を return し、描画が重いほど
    // 供給が止まり、長時間後に 150ms UpdateWindow 回復サイクルで体感が落ちた。
    if (InterlockedCompareExchange(&m_analysisDonePosted, 0, 0) != 0) {
        if (m_lastAnalysisDonePostTick != 0 && (now - m_lastAnalysisDonePostTick) >= 150u) {
            MSG msg;
            while (::PeekMessage(&msg, m_hWnd, WM_PIANOROLL_ANALYSIS_DONE, WM_PIANOROLL_ANALYSIS_DONE, PM_REMOVE)) {}
            InterlockedExchange(&m_analysisDonePosted, 0);
            InterlockedExchange(&m_analysisPresentDirty, 1);
            ApplySyncInvalidate();
        }
    }
    if (InterlockedCompareExchange(&m_syncPosted, 0, 0) != 0) return;
    // 実時間スロットル（paint 遅延で ms2 が伸びても 60Hz 同期にしない）
    int minMs = savedata.ms2;
    if (minMs < 16) minMs = 16;
    if (minMs > 960) minMs = 960;
    if (m_lastSyncPostTick != 0 && (now - m_lastSyncPostTick) < (DWORD)minMs)
        return;
    if (InterlockedCompareExchange(&m_syncPosted, 1, 0) != 0) return;
    m_lastSyncPostTick = now;
    if (!PostMessage(WM_PIANOROLL_SYNC, 0, 0))
        InterlockedExchange(&m_syncPosted, 0);
}

void CPianoRoll::ApplySyncInvalidate()
{
    if (m_paintDisabled || !::IsWindow(m_hWnd)) return;
    // meterDirty→keyDirty はしない。OnPaint がメーター帯だけの差分更新に回す。
    // アナライザと同じ: ロック矩形を更新領域から除外する。
    // 全面 Invalidate だと BeginPaint 時点でアクリル面が透け、
    // その後 Blit するまでの間「メインに追従」がちらつく。
    // BlitFull/フレーム BitBlt はクリップを無視して追従も更新する。
    CRect cr;
    GetClientRect(&cr);
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && cr.Height() > capH)
        cr.top = capH;
    if (!cr.IsRectEmpty())
        CCC_InvalidateRectMinusOverlay(m_hWnd, cr);
}

LRESULT CPianoRoll::OnSyncRequest(WPARAM, LPARAM)
{
    // アナライザと同じ: Sync は PCM 供給のみ。ここで Invalidate すると
    // AnalysisDone と二重に全面 OnPaint し、UI/EQ が数秒で飢える。
    // syncPosted は供給完了で解放（描画背圧は analysisDonePosted が担う）。
    if (m_paintDisabled || !::IsWindow(m_hWnd)) {
        InterlockedExchange(&m_syncPosted, 0);
        return 0;
    }
    COggDlg_SyncPianoRollFast();
    InterlockedExchange(&m_syncPosted, 0);
    return 0;
}

LRESULT CPianoRoll::OnAnalysisDone(WPARAM, LPARAM)
{
    // ロール描画の唯一の起動点。
    // UpdateWindow を毎回来すとピアノ/アナライザが UI を占有し、
    // MP の GDI スクロール(Invalidate のみ)が余波で飢える。
    // 通常は Invalidate のみ。固着時は RequestSync 側 150ms 監視で UpdateWindow 回復。
    if (m_paintDisabled || !::IsWindow(m_hWnd)) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        return 0;
    }
    ApplySyncInvalidate();
    return 0;
}

DWORD WINAPI CPianoRoll::AnalysisWorkerThreadEntry(LPVOID param)
{
    return static_cast<CPianoRoll*>(param)->AnalysisWorkerLoop();
}

// 分析ワーカースレッドを起動する。OnInitDialog から呼ばれる。
// イベント(m_hAnalysisWake)で眠り、AnalyzePlayCursorMono が SetEvent で起こす。
void CPianoRoll::StartAnalysisWorker()
{
    if (m_hAnalysisThread) return;
    InterlockedExchange(&m_workerStop, 0);
    InterlockedExchange(&m_jobPending, 0);
    InterlockedExchange(&m_analysisBusy, 0);
    if (!m_hAnalysisWake) {
        m_hAnalysisWake = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!m_hAnalysisWake) return;
    }
    m_hAnalysisThread = CreateThread(
        NULL, 0, AnalysisWorkerThreadEntry, this, 0, NULL);
    if (!m_hAnalysisThread) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
}

// 死亡スレッドの回収、stop フラグの解除、必要なら再起動。
// 形式切替後に解析が永久停止する主因をここで潰す。
// 再生スレッドと UI から同時に呼ばれ得るため m_jobCs で直列化する。
bool CPianoRoll::EnsureAnalysisWorkerAlive()
{
    EnterCriticalSection(&m_jobCs);

    if (m_hAnalysisThread) {
        const DWORD wr = WaitForSingleObject(m_hAnalysisThread, 0);
        if (wr == WAIT_OBJECT_0) {
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
            if (m_hAnalysisWake) {
                CloseHandle(m_hAnalysisWake);
                m_hAnalysisWake = NULL;
            }
            InterlockedExchange(&m_workerStop, 0);
            InterlockedExchange(&m_analysisBusy, 0);
            InterlockedExchange(&m_jobPending, 0);
        }
        else if (wr == WAIT_TIMEOUT) {
            // 生きているが stop=1 のまま残っているとジョブを受け付けない
            if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0) {
                InterlockedExchange(&m_workerStop, 0);
                InterlockedExchange(&m_jobPending, 0);
                InterlockedExchange(&m_analysisBusy, 0);
                if (m_hAnalysisWake)
                    SetEvent(m_hAnalysisWake);
            }
            const bool alive = (m_hAnalysisWake != NULL);
            LeaveCriticalSection(&m_jobCs);
            return alive;
        }
        else {
            // WAIT_FAILED 等: ハンドルが壊れているので捨てて作り直す
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
            if (m_hAnalysisWake) {
                CloseHandle(m_hAnalysisWake);
                m_hAnalysisWake = NULL;
            }
            InterlockedExchange(&m_workerStop, 0);
            InterlockedExchange(&m_analysisBusy, 0);
            InterlockedExchange(&m_jobPending, 0);
        }
    }

    if (!m_hAnalysisThread)
        StartAnalysisWorker();
    const bool ok = (m_hAnalysisThread != NULL && m_hAnalysisWake != NULL);
    LeaveCriticalSection(&m_jobCs);
    return ok;
}

// m_workerStop フラグを立てて SetEvent でワーカーを起こし、終了を待つ。
// デストラクタと DetachForDestroy から呼ばれる。
void CPianoRoll::StopAnalysisWorker()
{
    if (!m_hAnalysisThread && !m_hAnalysisWake) return;
    InterlockedExchange(&m_workerStop, 1);
    InterlockedExchange(&m_jobPending, 0);
    if (m_hAnalysisWake)
        SetEvent(m_hAnalysisWake);
    if (m_hAnalysisThread) {
        // タイムアウト時にハンドルだけ閉じるとスレッドが生きたまま UAF する
        const DWORD wr = WaitForSingleObject(m_hAnalysisThread, 15000);
        if (wr == WAIT_OBJECT_0) {
            CloseHandle(m_hAnalysisThread);
            m_hAnalysisThread = NULL;
        }
        else if (wr == WAIT_TIMEOUT) {
            // 既に死んでいるのにシグナルされないケースは上で拾えないので再確認
            if (WaitForSingleObject(m_hAnalysisThread, 0) == WAIT_OBJECT_0) {
                CloseHandle(m_hAnalysisThread);
                m_hAnalysisThread = NULL;
            }
        }
    }
    if (m_hAnalysisThread == NULL && m_hAnalysisWake) {
        CloseHandle(m_hAnalysisWake);
        m_hAnalysisWake = NULL;
    }
    if (m_hAnalysisThread == NULL)
        InterlockedExchange(&m_workerStop, 0);
    InterlockedExchange(&m_analysisBusy, 0);
}

// ワーカースレッドのメインループ。イベント待ちで眠り、起こされたら
// m_jobPending を CAS で取得して ProcessAnalysisJob を実行する。
// 解析完了後、::IsWindow チェックを挟んでから PostMessage するのは
// ウィンドウが既に破棄されている場合の HWND 再利用バグを防ぐため。
DWORD CPianoRoll::AnalysisWorkerLoop()
{
    for (;;) {
        HANDLE wake = m_hAnalysisWake;
        if (!wake)
            break;
        const DWORD wait = WaitForSingleObject(wake, INFINITE);
        if (wait != WAIT_OBJECT_0)
            continue;
        if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0)
            break;

        bool didWork = false;
        while (InterlockedCompareExchange(&m_jobPending, 0, 1) == 1) {
            if (InterlockedCompareExchange(&m_workerStop, 0, 0) != 0)
                break;
            if (ProcessAnalysisJob())
                didWork = true;
        }

        // 多重 Post するとキューが空にならず WM_PAINT / EQ が飢餓する。
        // 表示キックは ms2（描画周期）に合わせる。解析自体は ANALYZE_MIN_MS のまま。
        // キックできない分は dirty に残し、OnPaint 完了後に1回だけ追い付き提示する。
        if (didWork && ::IsWindow(m_hWnd)) {
            InterlockedExchange(&m_analysisPresentDirty, 1);
            int minMs = savedata.ms2;
            if (minMs < 16) minMs = 16;
            if (minMs > 960) minMs = 960;
            const DWORD now = GetTickCount();
            if (m_lastAnalysisDonePostTick == 0 || (now - m_lastAnalysisDonePostTick) >= (DWORD)minMs) {
                if (InterlockedCompareExchange(&m_analysisDonePosted, 1, 0) == 0) {
                    m_lastAnalysisDonePostTick = now;
                    if (!PostMessage(WM_PIANOROLL_ANALYSIS_DONE, 0, 0))
                        InterlockedExchange(&m_analysisDonePosted, 0);
                }
            }
        }
    }
    return 0;
}

// ジョブバッファをローカルにコピーしてから m_jobCs を解放し、
// 長い Goertzel 演算中はジョブバッファを解放しておく(再生スレッドが
// 次のジョブを書き込める状態を保つ)。
// Goertzel は m_cs 外。UI OnPaint が結果スナップショットだけ短時間待つようにする。
// SEH で CS / busy を必ず解放し、例外でワーカーが死んでもロックを残さない。
bool CPianoRoll::ProcessAnalysisJob()
{
    InterlockedExchange(&m_analysisBusy, 1);
    const LONG epochAtStart = InterlockedCompareExchange(&m_analysisEpoch, 0, 0);

    int frameCount = 0;
    int sampleRate = 44100;

    EnterCriticalSection(&m_jobCs);
    frameCount = m_jobFrameCount;
    sampleRate = CapAnalyzeSampleRate(m_jobSampleRate);
    if (frameCount > 0) {
        if (frameCount > WIN_SAMPLES_MAX) frameCount = WIN_SAMPLES_MAX;
        memcpy(m_workerMonoScratch, m_jobMono, (size_t)frameCount * sizeof(double));
    }
    LeaveCriticalSection(&m_jobCs);

    bool ok = false;
    if (m_feedEnabled &&
        InterlockedCompareExchange(&m_analysisEpoch, 0, 0) == epochAtStart &&
        frameCount >= MinAnalyzeFrameCount(sampleRate, frameCount) &&
        sampleRate >= 8000) {
        const double* mono = m_workerMonoScratch;
        __try {
            ok = RunAnalysisJob(mono, frameCount, sampleRate, epochAtStart);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        if (ok &&
            m_feedEnabled &&
            InterlockedCompareExchange(&m_analysisEpoch, 0, 0) == epochAtStart) {
            EnterCriticalSection(&m_cs);
            __try {
                PublishDetectResults();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                ok = false;
            }
            LeaveCriticalSection(&m_cs);
        }
    }

    InterlockedExchange(&m_analysisBusy, 0);
    return ok;
}

void CPianoRoll::PublishDetectResults()
{
    if (m_detectSilent) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            m_activeKeys[i] = false;
            m_bandMask[i] = 0;
            memset(m_laneStrength[i], 0, sizeof(m_laneStrength[i]));
            m_exprFlags[i] = 0;
        }
        AppendScoreCaptureLocked();
        PushFrame(false);
        return;
    }
    UpdateNoteStates();
    AppendScoreCaptureLocked();
    PushDisplayFrames();
}

bool CPianoRoll::RunAnalysisJob(const double* mono, int frameCount, int sampleRate, LONG epochAtStart)
{
    if (!mono) return false;
    if (!m_feedEnabled ||
        InterlockedCompareExchange(&m_analysisEpoch, 0, 0) != epochAtStart)
        return false;

    try {
        EnsureAnalysisTables(sampleRate);
        if (frameCount < m_winLow || m_winLow <= 0 ||
            !m_analysisTablesReady ||
            m_winLow <= 0)
            return false;
        const double* lowWin = mono + (frameCount - m_winLow);
        const int bassLen = (frameCount >= m_winBass) ? m_winBass : m_winLow;
        const double* bassWin = (frameCount >= m_winBass)
            ? mono + (frameCount - m_winBass)
            : lowWin;

        RunGoertzelFromBuffer(lowWin, bassWin, bassLen);
        return true;
    }
    catch (...) {
        return false;
    }
}

// ============================================================================
// 簡易3D 表示 (GDI のみ。OpenGL/Direct3D は使わない)
//   ワールド座標: x=鍵盤の左右(-1..+1) / y=高さ(0=鍵盤面) / z=奥行き(0=手前)
//   ヨー(Y軸) → ピッチ(X軸) → 透視除算 の順で画面座標へ落とす。
//   Z バッファは持たず、奥の面から順に描く画家アルゴリズムで前後関係を作る。
//   描画先は従来どおりオフスクリーン(m_rollDC)なので、ちらつきは出ない。
// ============================================================================
namespace PianoDraw3D
{
    static constexpr float KEY_FRONT_Z = 0.00f;    // 白鍵の手前端
    static constexpr float KEY_BACK_Z = 0.55f;    // 鍵盤の奥端
    static constexpr float BLACK_FRONT_Z = 0.24f;    // 黒鍵の手前端
    static constexpr float BLACK_TOP_Y = 0.055f;   // 黒鍵の高さ
    static constexpr float BODY_Y = -0.07f;   // 鍵盤の手前側面(ボディ)
    static constexpr float WALL_GAP = 0.10f;    // 鍵盤奥端 〜 履歴ウォール先頭
    static constexpr float ROW_DEPTH = 0.09f;    // 履歴1行あたりの奥行き
    static constexpr float ROW_FILL = 0.80f;    // 行内でバーが占める割合
    static constexpr float BAR_MAX_Y = 0.42f;    // ノートバーの最大高さ
    static constexpr float METER_X0 = 1.06f;    // レベルメーターの開始位置(鍵盤の外側)
    static constexpr float METER_W = 0.11f;
    static constexpr float METER_MAX_Y = 0.80f;

    static COLORREF Shade(COLORREF c, float f)
    {
        int r = (int)(GetRValue(c) * f + 0.5f);
        int g = (int)(GetGValue(c) * f + 0.5f);
        int b = (int)(GetBValue(c) * f + 0.5f);
        if (r < 0) r = 0; else if (r > 255) r = 255;
        if (g < 0) g = 0; else if (g > 255) g = 255;
        if (b < 0) b = 0; else if (b > 255) b = 255;
        // 背景と同じ RGB(20,20,20) はアクリル時に透過キーとして抜けてしまう
        if (r == 20 && g == 20 && b == 20) b = 23;
        return RGB(r, g, b);
    }

    // ループ内で CreatePen/CreateSolidBrush はしない(長時間再生で GDI が痩せ、
    // EQ 描画まで巻き込んで重くなるため)。DC_BRUSH/DC_PEN の色差し替えで塗る。
    static void FillQuad(CDC& dc, POINT* pts, COLORREF fill)
    {
        ::SetDCBrushColor(dc.GetSafeHdc(), fill);
        ::SetDCPenColor(dc.GetSafeHdc(), fill);
        dc.Polygon(pts, 4);
    }
}

void CPianoRoll::ProjectView3D(const View3D& v, float x, float y, float z, POINT& out)
{
    const float rx = x * v.cosYaw - z * v.sinYaw;
    const float rz = x * v.sinYaw + z * v.cosYaw;
    const float ry2 = y * v.cosPitch + rz * v.sinPitch;
    const float rz2 = rz * v.cosPitch - y * v.sinPitch;
    float denom = v.camD + rz2;
    if (denom < 0.30f) denom = 0.30f;   // カメラ背後へ回り込ませない
    const float p = v.camD / denom;
    out.x = (long)floorf(v.originX + rx * p * v.scale + 0.5f);
    out.y = (long)floorf(v.originY - ry2 * p * v.scale + 0.5f);
}

void CPianoRoll::BuildView3D(int width, int height, View3D& v) const
{
    using namespace PianoDraw3D;
    const float yaw = m_view3dYawDeg * (float)(M_PI / 180.0);
    const float pit = m_view3dPitchDeg * (float)(M_PI / 180.0);
    v.cosYaw = cosf(yaw);
    v.sinYaw = sinf(yaw);
    v.cosPitch = cosf(pit);
    v.sinPitch = sinf(pit);
    v.camD = 3.2f;

    // 回すほどシーンの画面上の広がりが変わるので、固定倍率だと端が切れる。
    // シーンを内包する2つの直方体の頂点を基準倍率で投影し、その外接矩形が
    // クライアントに収まる倍率と原点を毎フレーム求める(自動フレーミング)。
    const float kProbeScale = 1024.0f;
    v.scale = kProbeScale;
    v.originX = 0.0f;
    v.originY = 0.0f;
    const float farZ = KEY_BACK_Z + WALL_GAP + ROW_DEPTH * (float)VIEW3D_DEPTH;
    int meterCh = m_showLevelMeter ? m_chMeterCount : 0;
    if (meterCh > PIANO_METER_CH_MAX) meterCh = PIANO_METER_CH_MAX;
    const int meterSlots = (meterCh + 1) / 2;
    const float meterX = (meterSlots > 0) ? (METER_X0 + METER_W * (float)meterSlots) : 1.0f;
    const float boxes[2][6] = {
        // { xMin, xMax, yMin, yMax, zMin, zMax }
        { -meterX, meterX, BODY_Y, METER_MAX_Y, KEY_FRONT_Z, KEY_BACK_Z },  // 鍵盤 + メーター
        { -1.0f,   1.0f,   0.0f,   BAR_MAX_Y,   KEY_BACK_Z + WALL_GAP, farZ },  // 履歴ウォール
    };
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int b = 0; b < 2; ++b) {
        for (int c = 0; c < 8; ++c) {
            POINT p;
            ProjectView3D(v,
                (c & 1) ? boxes[b][1] : boxes[b][0],
                (c & 2) ? boxes[b][3] : boxes[b][2],
                (c & 4) ? boxes[b][5] : boxes[b][4], p);
            const float fx = (float)p.x, fy = (float)p.y;
            if (fx < minX) minX = fx;
            if (fx > maxX) maxX = fx;
            if (fy < minY) minY = fy;
            if (fy > maxY) maxY = fy;
        }
    }
    float bw = maxX - minX; if (bw < 1.0f) bw = 1.0f;
    float bh = maxY - minY; if (bh < 1.0f) bh = 1.0f;
    float s = (float)width * 0.96f * kProbeScale / bw;
    const float sH = (float)height * 0.94f * kProbeScale / bh;
    if (sH < s) s = sH;
    if (s < 8.0f) s = 8.0f;
    float zoom = m_view3dZoom;
    if (zoom < kView3dZoomMin) zoom = kView3dZoomMin;
    if (zoom > kView3dZoomMax) zoom = kView3dZoomMax;
    s *= zoom;
    v.scale = s;
    v.originX = (float)width * 0.5f - (minX + maxX) * 0.5f / kProbeScale * s;
    v.originY = (float)height * 0.5f - (minY + maxY) * 0.5f / kProbeScale * s;
}

// 直方体を「天面 + 手前の面」の2枚で描く（ピアノロール従来どおり側面は省略）。
void CPianoRoll::DrawBox3D(GdiSoft3D::Context& ctx, float xL, float xR, float topY,
    float z0, float z1, COLORREF col, float frontShade, float baseY)
{
    ctx.DrawQuad(xL, topY, z0, xR, topY, z0, xR, topY, z1, xL, topY, z1, col);
    if (topY > baseY) {
        ctx.DrawQuad(xL, baseY, z0, xR, baseY, z0, xR, topY, z0, xL, topY, z0,
            PianoDraw3D::Shade(col, frontShade));
    }
}

// 3D 空間での鍵の左右端。白鍵は等幅、黒鍵は左隣の白鍵の境界へ寄せる。
void CPianoRoll::KeyXSpan3D(int keyIndex, float& xL, float& xR) const
{
    xL = xR = 0.0f;
    int lo, hi; GetDisplayKeyRange(lo, hi);
    if (keyIndex < lo || keyIndex >= hi) return;
    const int midi = MIDI_BASE + keyIndex;
    const float wKey = 2.0f / (float)DisplayWhiteKeyCount();
    if (!IsBlackKey(midi)) {
        const int w = DisplayWhiteKeyIndex(midi);
        if (w < 0) return;
        xL = -1.0f + wKey * (float)w;
        xR = xL + wKey;
        return;
    }
    const int leftWhite = DisplayWhiteKeyIndex(midi - 1);   // 黒鍵の左隣は必ず白鍵
    if (leftWhite < 0) return;
    const float center = -1.0f + wKey * (float)(leftWhite + 1);
    xL = center - wKey * 0.30f;
    xR = center + wKey * 0.30f;
}

// 履歴リングから奥行き方向のサンプルを作る(固定長。可変長コンテナは使わない)
void CPianoRoll::Capture3DWalls()
{
    EnterCriticalSection(&m_cs);
    const int avail = m_historyCount;
    int rows = 0;
    for (int r = 0; r < VIEW3D_DEPTH; ++r) {
        Wall3DRow& row = m_wall3D[r];
        const int idx = r * VIEW3D_STRIDE;
        if (idx >= avail) {
            memset(row.level, 0, sizeof(row.level));
            memset(row.band, 0, sizeof(row.band));
            continue;
        }
        const NoteFrame& f = HistoryAt(idx);
        for (int i = 0; i < KEY_COUNT; ++i) {
            if (!f.active[i]) {
                row.level[i] = 0;
                row.band[i] = 0;
                continue;
            }
            float dyn = f.dynLevel[i];
            if (dyn <= 0.0f) dyn = 0.45f;
            else if (dyn > 1.0f) dyn = 1.0f;
            int lv = (int)(dyn * 254.0f) + 1;
            if (lv > 255) lv = 255;
            row.level[i] = (uint8_t)lv;
            row.band[i] = f.bandMask[i] ? f.bandMask[i] : (uint8_t)(1u << KeyBandIndex(i));
        }
        rows = r + 1;
    }
    m_wall3DRows = rows;
    LeaveCriticalSection(&m_cs);
}

void CPianoRoll::Draw3DWalls(GdiSoft3D::Context& ctx, const View3D& v) const
{
    using namespace PianoDraw3D;
    int lo, hi; GetDisplayKeyRange(lo, hi);
    const float wallZ0 = KEY_BACK_Z + WALL_GAP;
    const float farZ = wallZ0 + ROW_DEPTH * (float)VIEW3D_DEPTH;

    // 奥行きの手掛かりに床のガイド線
    for (int i = lo; i < hi; ++i) {
        if ((MIDI_BASE + i) % 12 != 0) continue;
        float xL, xR; KeyXSpan3D(i, xL, xR);
        if (xR <= xL) continue;
        ctx.DrawLine(xL, 0.0f, wallZ0, xL, 0.0f, farZ, RGB(48, 48, 56));
    }
    for (int r = 0; r <= VIEW3D_DEPTH; r += 8) {
        const float z = wallZ0 + ROW_DEPTH * (float)r;
        ctx.DrawLine(-1.0f, 0.0f, z, 1.0f, 0.0f, z, RGB(38, 38, 46));
    }

    const bool descend = (v.sinYaw > 0.0f);
    const int rows = (m_wall3DRows > VIEW3D_DEPTH) ? VIEW3D_DEPTH : m_wall3DRows;
    const int span = hi - lo;
    for (int r = rows - 1; r >= 0; --r) {
        const Wall3DRow& row = m_wall3D[r];
        const float z0 = wallZ0 + ROW_DEPTH * (float)r;
        const float z1 = z0 + ROW_DEPTH * ROW_FILL;
        const float fade = 1.0f - 0.55f * ((float)r / (float)(VIEW3D_DEPTH - 1));
        for (int k = 0; k < span; ++k) {
            const int i = descend ? (hi - 1 - k) : (lo + k);
            const uint8_t lv = row.level[i];
            if (!lv) continue;
            float xL, xR; KeyXSpan3D(i, xL, xR);
            if (xR <= xL) continue;
            const float st = (float)lv / 255.0f;
            const COLORREF base = PianoDraw::LocalKeyColor(i, st * 3.0f, IsBlackKey(MIDI_BASE + i));
            DrawBox3D(ctx, xL, xR, 0.015f + st * BAR_MAX_Y, z0, z1, Shade(base, fade), 0.60f);
        }
    }
}

void CPianoRoll::Draw3DKeyboard(GdiSoft3D::Context& ctx, const View3D& v, CDC* textDC, const bool* actives) const
{
    using namespace PianoDraw3D;
    int lo, hi; GetDisplayKeyRange(lo, hi);
    const int span = hi - lo;
    const bool descend = (v.sinYaw > 0.0f);

    if (!textDC) {
        for (int k = 0; k < span; ++k) {
            const int i = descend ? (hi - 1 - k) : (lo + k);
            const int midi = MIDI_BASE + i;
            if (IsBlackKey(midi)) continue;
            float xL, xR; KeyXSpan3D(i, xL, xR);
            if (xR <= xL) continue;
            const bool on = (actives && actives[i]);
            const float gap = (xR - xL) * 0.06f;
            const float topY = on ? -0.012f : 0.0f;
            const COLORREF top = on ? PianoDraw::LocalKeyColor(i, 2.5f, false) : RGB(232, 232, 236);
            DrawBox3D(ctx, xL + gap, xR - gap, topY, KEY_FRONT_Z, KEY_BACK_Z, top, 0.72f, BODY_Y);
        }
        for (int k = 0; k < span; ++k) {
            const int i = descend ? (hi - 1 - k) : (lo + k);
            const int midi = MIDI_BASE + i;
            if (!IsBlackKey(midi)) continue;
            float xL, xR; KeyXSpan3D(i, xL, xR);
            if (xR <= xL) continue;
            const bool on = (actives && actives[i]);
            const float topY = on ? (BLACK_TOP_Y - 0.012f) : BLACK_TOP_Y;
            const COLORREF top = on ? PianoDraw::LocalKeyColor(i, 2.5f, true) : RGB(40, 40, 48);
            DrawBox3D(ctx, xL, xR, topY, BLACK_FRONT_Z, KEY_BACK_Z, top, 0.55f);
        }
        return;
    }

    if (!m_showNoteNames || !m_paintFontsReady || !m_fontKeyNote.GetSafeHandle()) return;
    CFont* pOld = textDC->SelectObject(CFont::FromHandle((HFONT)m_fontKeyNote.GetSafeHandle()));
    textDC->SetBkMode(TRANSPARENT);
    textDC->SetTextColor(RGB(70, 70, 78));
    for (int i = lo; i < hi; ++i) {
        const int midi = MIDI_BASE + i;
        const wchar_t* name = PianoDraw::WhiteKeyLabel(midi);
        if (!name) continue;
        float xL, xR; KeyXSpan3D(i, xL, xR);
        if (xR <= xL) continue;
        POINT a, b;
        ProjectView3D(v, xL, 0.0f, KEY_FRONT_Z + 0.10f, a);
        ProjectView3D(v, xR, 0.0f, KEY_FRONT_Z + 0.10f, b);
        int x0 = (int)a.x, x1 = (int)b.x;
        if (x1 < x0) { const int t = x0; x0 = x1; x1 = t; }
        if (x1 - x0 < 9) continue;
        CRect tr(x0, (int)a.y - 18, x1, (int)a.y + 2);
        textDC->DrawText(name, -1, &tr, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    }
    textDC->SelectObject(pOld);
}

void CPianoRoll::Draw3DMeters(GdiSoft3D::Context& ctx, const View3D& /*v*/, const float* chFill, int chCount) const
{
    using namespace PianoDraw3D;
    if (!m_showLevelMeter || !chFill || chCount <= 0) return;
    const int n = (chCount > PIANO_METER_CH_MAX) ? PIANO_METER_CH_MAX : chCount;
    for (int c = 0; c < n; ++c) {
        const bool right = ((c & 1) != 0);
        const int slot = c / 2;
        const float x0 = METER_X0 + METER_W * (float)slot;
        const float x1 = x0 + METER_W * 0.78f;
        const float xa = right ? x0 : -x1;
        const float xb = right ? x1 : -x0;
        float fill = chFill[c];
        if (fill < 0.0f) fill = 0.0f; else if (fill > 1.0f) fill = 1.0f;
        COLORREF col = RGB(70, 175, 95);
        if (n == 2 && c == 1) col = RGB(175, 120, 70);
        else if (n > 2) col = RGB(90 + c * 12, 140, 180 - c * 8);
        DrawBox3D(ctx, xa, xb, METER_MAX_Y, KEY_FRONT_Z, KEY_BACK_Z, RGB(50, 50, 58), 0.62f);
        DrawBox3D(ctx, xa, xb, 0.02f + fill * (METER_MAX_Y - 0.02f),
            KEY_FRONT_Z, KEY_BACK_Z, col, 0.62f);
    }
}

void CPianoRoll::Draw3DExprMarks(GdiSoft3D::Context& ctx, const View3D& v,
    const bool* activesCopy, const uint8_t* exprCopy) const
{
    using namespace PianoDraw3D;
    using namespace PianoDraw;
    if (!m_showExprMarks || !activesCopy || !exprCopy) return;
    static const uint8_t kPri[] = {
        PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::VIBRATO,
        PianoExpr::SLIDE, PianoExpr::FALL, PianoExpr::CRESC, PianoExpr::DECRESC, PianoExpr::SUSTAIN
    };
    int lo, hi; GetDisplayKeyRange(lo, hi);
    for (int i = lo; i < hi; ++i) {
        if (!activesCopy[i] || !exprCopy[i]) continue;
        uint8_t flag = 0;
        for (uint8_t f : kPri) { if (exprCopy[i] & f) { flag = f; break; } }
        if (!flag) continue;
        float xL, xR; KeyXSpan3D(i, xL, xR);
        if (xR <= xL) continue;
        const float cx = 0.5f * (xL + xR);
        const float half = (xR - xL) * 0.28f;
        const COLORREF col = ExprColorForFlag(flag);
        if (flag == PianoExpr::VIBRATO)
            ctx.DrawSphere(cx, 0.10f, KEY_FRONT_Z + 0.06f, 0.035f, col, 8, 6);
        else if (flag == PianoExpr::ACCENT)
            ctx.DrawNeonBox(cx - half, cx + half, 0.14f, KEY_FRONT_Z, KEY_FRONT_Z + 0.08f, col, 0.02f);
        else
            ctx.DrawBox(cx - half, cx + half, 0.11f, KEY_FRONT_Z, KEY_FRONT_Z + 0.07f, col, 0.02f);
    }
    (void)v;
}

void CPianoRoll::Draw3DExprLegend(GdiSoft3D::Context& ctx) const
{
    using namespace PianoDraw;
    if (!m_showExprLegend) return;
    static const uint8_t kFlags[] = {
        PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::FALL,
        PianoExpr::SLIDE, PianoExpr::VIBRATO, PianoExpr::CRESC,
        PianoExpr::DECRESC, PianoExpr::SUSTAIN
    };
    const int n = (int)(sizeof(kFlags) / sizeof(kFlags[0]));
    const float x0 = -1.12f;
    const float z0 = 0.05f;
    const float step = 0.085f;
    for (int i = 0; i < n; ++i) {
        const float z = z0 + step * (float)i;
        const COLORREF col = ExprColorForFlag(kFlags[i]);
        ctx.DrawNeonBox(x0, x0 + 0.10f, 0.07f, z, z + 0.055f, col, 0.0f);
    }
}

void CPianoRoll::Draw3DSceneToBuffer(CDC& dc, int width, int height,
    const bool* activesCopy, const float* chFillCopy, int chCountCopy, const uint8_t* exprCopy) const
{
    dc.FillSolidRect(0, 0, width, height, PIANO_CHROMA_KEY);
    if (width < 48 || height < 48) return;

    using namespace PianoDraw3D;
    View3D v;
    BuildView3D(width, height, v);

    GdiSoft3D::Context ctx;
    if (!ctx.Create(width, height)) return;
    ctx.view.cosYaw = v.cosYaw;
    ctx.view.sinYaw = v.sinYaw;
    ctx.view.cosPitch = v.cosPitch;
    ctx.view.sinPitch = v.sinPitch;
    ctx.view.camD = v.camD;
    ctx.view.scale = v.scale;
    ctx.view.originX = v.originX;
    ctx.view.originY = v.originY;
    ctx.depthTest = true;
    ctx.depthWrite = true;
    ctx.BeginFrame(PIANO_CHROMA_KEY);

    ctx.DrawMirrorFloor(-1.05f, 1.05f, KEY_BACK_Z, KEY_BACK_Z + ROW_DEPTH * (float)VIEW3D_DEPTH,
        RGB(90, 120, 180), 0.18f);
    Draw3DWalls(ctx, v);
    Draw3DMeters(ctx, v, chFillCopy, chCountCopy);
    Draw3DKeyboard(ctx, v, nullptr, activesCopy);
    Draw3DExprMarks(ctx, v, activesCopy, exprCopy);
    Draw3DExprLegend(ctx);

    ctx.Present(dc, 0, 0);
    // ノート名は GDI テキスト（Present 後）
    Draw3DKeyboard(ctx, v, &dc, activesCopy);
}

void CPianoRoll::DrawKeyboardToBuffer(CDC& memDC, int width, int keySectionH, int keyH,
    const bool* activesCopy, const uint8_t* bandMaskCopy, const float laneStrengthCopy[KEY_COUNT][3],
    const float* chFillCopy, int chCountCopy, const uint8_t* exprCopy) const
{
    using namespace PianoDraw;

    const int bkH = keyH * WHITE_KEY_COUNT / 100;
    const int labelH = min(16, keyH / 4);
    const int keyTop = labelH + 2;
    const int splitY = keyTop + bkH;
    const COLORREF whiteFace = RGB(238, 238, 238);
    const COLORREF blackFace = RGB(28, 28, 34);
    memDC.FillSolidRect(0, 0, width, keySectionH, RGB(150, 150, 155));

    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (IsBlackKey(midi)) continue;
        int xL, xR; GetWhiteKeyRect52(midi, width, xL, xR);
        CRect kc(xL, splitY, xR, keySectionH);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), bMask, laneStrengthCopy[i], i, 2.5f, false, on);
        }
        else { DrawBevelKey(memDC, CRect(kc.left + 1, kc.top, kc.right - 1, kc.bottom), whiteFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (IsBlackKey(midi)) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], i, 2.5f, false, on);
        }
        else { DrawBevelKey(memDC, kc, whiteFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i; if (!IsBlackKey(midi)) continue;
        int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
        CRect kc(xL + 1, keyTop, xR - 1, splitY);
        const bool on = activesCopy[i];
        if (on) {
            const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
            DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], i, 2.5f, true, on);
        }
        else { DrawBevelKey(memDC, kc, blackFace, false); }
    }
    for (int i = 0; i < KEY_COUNT; ++i) {
        const int midi = MIDI_BASE + i;
        if (!IsBlackKey(midi) || !activesCopy[i]) continue;
        const int parentMidi = midi - 1;
        int xL, xR; GetWhiteKeyRect52(parentMidi, width, xL, xR);
        if (xR <= xL) continue;
        CRect kc(xL + 1, keySectionH - labelH - 2, xR - 1, keySectionH - 2);
        if (kc.Height() < 2) continue;
        const uint8_t bMask = bandMaskCopy[i] ? bandMaskCopy[i] : (uint8_t)(1u << KeyBandIndex(i));
        DrawLaneKey(memDC, kc, bMask, laneStrengthCopy[i], i, 2.5f, false, true);
    }

    {
        HGDIOBJ oldPen = memDC.SelectObject(::GetStockObject(DC_PEN));
        ::SetDCPenColor(memDC.GetSafeHdc(), RGB(90, 90, 95));
        memDC.MoveTo(0, splitY); memDC.LineTo(width, splitY);
        memDC.MoveTo(0, 0); memDC.LineTo(width, 0);
        memDC.SelectObject(oldPen);
    }

    // アクティブキーに表現記号を重ねる（履歴バーと同じグリフをキー側にも表示）。
    // クロマチックキー上部(keyTop付近)に主要フラグのグリフを1つ描く。
    if (m_showExprMarks && exprCopy && m_paintFontsReady && bkH >= 8) {
        CFont* pSym = nullptr;
        if (m_fontExprSymbolCompact.GetSafeHandle())
            pSym = CFont::FromHandle((HFONT)m_fontExprSymbolCompact.GetSafeHandle());
        else if (m_fontExprSymbol.GetSafeHandle())
            pSym = CFont::FromHandle((HFONT)m_fontExprSymbol.GetSafeHandle());
        if (pSym) {
            // 主要フラグの優先順位（履歴の ExprPrimaryColor と同順）
            static const uint8_t kPri[] = {
                PianoExpr::ACCENT, PianoExpr::SCOOP, PianoExpr::VIBRATO,
                PianoExpr::SLIDE, PianoExpr::FALL, PianoExpr::SUSTAIN
            };
            const int glyphH = min(bkH, 22);
            for (int i = 0; i < KEY_COUNT; ++i) {
                if (!activesCopy[i] || !exprCopy[i]) continue;
                uint8_t flag = 0;
                for (uint8_t f : kPri) { if (exprCopy[i] & f) { flag = f; break; } }
                if (!flag) continue;
                int xL, xR; GetChromaticKeyRect(i, width, xL, xR);
                if (xR - xL < 3) continue;
                CRect gr(xL, keyTop + 1, xR, keyTop + 1 + glyphH);
                DrawExprGlyphOnNote(memDC, gr, flag, pSym);
            }
        }
    }

    if (m_showLevelMeter && chCountCopy > 0 && labelH >= 4) {
        CRect meterStrip(2, 1, width - 2, labelH + 1);
        DrawChannelDbBars(memDC, meterStrip, chFillCopy, chCountCopy);
    }

    if (m_paintFontsReady) {
        memDC.SetBkMode(TRANSPARENT);
        CFont* pOldFont = memDC.SelectObject(CFont::FromHandle((HFONT)m_fontKeyNote.GetSafeHandle()));
        memDC.SetTextColor(RGB(70, 70, 75));
        if (m_showNoteNames) {
            for (int i = 0; i < KEY_COUNT; ++i) {
                const int midi = MIDI_BASE + i;
                const wchar_t* name = WhiteKeyLabel(midi); if (!name) continue;
                int xL, xR; GetWhiteKeyRect52(midi, width, xL, xR);
                if (xR <= xL) continue;
                CRect tr(xL + 2, keySectionH - labelH - 2, xR - 2, keySectionH - 2);
                memDC.DrawText(name, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        memDC.SelectObject(CFont::FromHandle((HFONT)m_fontKeyOct.GetSafeHandle()));
        memDC.SetTextColor(RGB(100, 100, 110));
        for (int i = 0; i < KEY_COUNT; ++i) {
            const int midi = MIDI_BASE + i; if (midi % 12 != 0) continue;
            int xL, xR; GetWhiteKeyRect52(midi, width, xL, xR);
            if (xR <= xL) continue;
            CString oct; oct.Format(L"%d", MidiOctaveNumber(midi));
            CRect tr(xL + 2, 1, xR - 2, labelH + 1);
            memDC.DrawText(oct, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        memDC.SelectObject(pOldFont);
    }
}

bool CPianoRoll::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
    if (w <= 0 || h <= 0) return false;
    if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h)
        return true;
    if (m_frameDC.GetSafeHdc()) {
        if (m_frameOldBmp) m_frameDC.SelectObject(m_frameOldBmp);
        m_frameOldBmp = nullptr;
        m_frameBmp.DeleteObject();
        m_frameDC.DeleteDC();
    }
    if (!m_frameDC.CreateCompatibleDC(&refDC)) return false;
    if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) {
        m_frameDC.DeleteDC();
        return false;
    }
    m_frameOldBmp = m_frameDC.SelectObject(&m_frameBmp);
    m_frameW = w;
    m_frameH = h;
    return true;
}


int CPianoRoll::ChordPanelHeightPx()
{
    return savedata.mpChordPanel ? 28 : 0;
}

void CPianoRoll::UpdateChordHistoryFromKeyCodes()
{
    if (!savedata.mpChordPanel) return;
    CString lo, mid, hi, all;
    SnapshotEqKeyCodes(lo, mid, hi, all);
    CString u = all;
    int lt = u.ReverseFind(L'<');
    int gt = u.ReverseFind(L'>');
    CString chord;
    if (lt >= 0 && gt > lt)
        chord = u.Mid(lt + 1, gt - lt - 1);
    else
        chord = u;
    CString clean;
    for (int i = 0; i < chord.GetLength(); ++i) {
        if (chord[i] == L'!' && i + 1 < chord.GetLength() && chord[i + 1] == L'@') {
            i += 2;
            while (i < chord.GetLength() && ((chord[i] >= L'0' && chord[i] <= L'9')
                || (chord[i] >= L'a' && chord[i] <= L'z')
                || (chord[i] >= L'A' && chord[i] <= L'Z')
                || chord[i] == L'+' || chord[i] == L'-'))
                ++i;
            --i;
            continue;
        }
        if (chord[i] >= 32)
            clean += chord[i];
    }
    clean.Trim();
    if (clean.IsEmpty() || clean.Find(L"F-01") >= 0)
        clean = L"-";
    if (clean.GetLength() >= 24)
        clean = clean.Left(23);
    if (wcscmp(m_chordLast, clean) == 0)
        return;
    wcsncpy_s(m_chordLast, clean, _TRUNCATE);
    const int slot = m_chordHistHead % CHORD_HIST_MAX;
    wcsncpy_s(m_chordHist[slot], clean, _TRUNCATE);
    m_chordHistHead++;
    if (m_chordHistCount < CHORD_HIST_MAX)
        m_chordHistCount++;
}

void CPianoRoll::DrawChordPanel(CDC& dc, int x, int y, int w, int h) const
{
    if (w <= 0 || h <= 0) return;
    dc.FillSolidRect(x, y, w, h, RGB(24, 28, 40));
    dc.FillSolidRect(x, y, w, 1, RGB(70, 90, 120));
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(180, 200, 230));
    CFont* of = nullptr;
    if (m_fontMeterTag.GetSafeHandle())
        of = dc.SelectObject(const_cast<CFont*>(&m_fontMeterTag));
    dc.TextOut(x + 6, y + 2, LL14(L"コード", L"Chord", L"Accord", L"Accordo", L"Acorde",
        L"코드", L"和弦", L"تآلف", L"Аккорд", L"Akkord", L"Acorde", L"Akkoord", L"Akord", L"Akor"));
    const int n = m_chordHistCount;
    int cx = x + w - 8;
    for (int i = 0; i < n; ++i) {
        const int idx = (m_chordHistHead - 1 - i + CHORD_HIST_MAX * 8) % CHORD_HIST_MAX;
        const WCHAR* s = m_chordHist[idx];
        if (!s || !s[0]) continue;
        CSize sz = dc.GetTextExtent(s);
        cx -= sz.cx + 10;
        if (cx < x + 56) break;
        const bool newest = (i == 0);
        dc.SetTextColor(newest ? RGB(255, 220, 120) : RGB(150, 170, 200));
        dc.TextOut(cx, y + 6, s);
        if (!newest)
            dc.FillSolidRect(cx + sz.cx + 4, y + 8, 1, h - 12, RGB(55, 65, 85));
    }
    if (of) dc.SelectObject(of);
}

void CPianoRoll::PresentFinalFrame(CDC& dc, int w, int h, int rollH, int keySectionH, int chordH)
{
    // 凡例はスクロール用 m_rollDC に焼かない。最終面へだけ合成する。
    // （旧: 毎フレーム TransparentBlt→提示→書き戻しで GDI/DWM が分単位に劣化し EQ が飢える）
    CRect lgPanel;
    const bool wantLegend = m_showExprLegend && m_rollReady && m_rollDC.GetSafeHdc();
    if (wantLegend)
        GetExprLegendPanelRect(w, rollH, lgPanel);
    const bool haveLegend = wantLegend && !lgPanel.IsRectEmpty();

#if CCUSTOM_AERO_SUPPORT
    if (savedata.aero == 1 && CCC_IsWin11() && m_chromaReady && m_chromaCache.hdcDib) {
        // 追従オーバーレイのヘッダー復元が凡例矩形を潰すため、凡例は Bake の後に載せる。
        BakeMainFollowOverlayIntoChroma(w, h, rollH, keySectionH);
        if (haveLegend) {
            DrawExprLegend(dc, w, rollH, false);
            if (m_legendBgDC.GetSafeHdc() && lgPanel.Width() > 0 && lgPanel.Height() > 0) {
                m_chromaCache.UpdateRect(m_legendBgDC.GetSafeHdc(),
                    0, 0, lgPanel.left, lgPanel.top,
                    lgPanel.Width(), lgPanel.Height(), PIANO_CHROMA_KEY);
                m_chromaCache.MakeRectOpaque(lgPanel.left, lgPanel.top, lgPanel.Width(), lgPanel.Height());
            }
        }
        const int yOff = CCC_GetCustomCaptionHeight(m_hWnd);
        // 簡易3Dは鍵盤帯が無いので全面 Blit。2Dはロール+鍵盤が揃っていれば全面。
        if (m_rollReady && (m_keyBufReady || keySectionH <= 0) && chordH <= 0) {
            m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, yOff, w, h);
            CCC_CaptionPaint(dc, m_hWnd);
            return;
        }
        if (m_rollReady && (m_keyBufReady || keySectionH <= 0) && chordH > 0) {
            if (yOff <= 0) {
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, 0, w, rollH);
                if (m_keyBufReady)
                    m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, rollH + chordH, w, keySectionH);
            }
            else if (m_chromaCache.hdcDib) {
                CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, yOff, w, rollH, m_chromaCache.hdcDib, 0, 0, w, rollH);
                if (m_keyBufReady)
                    CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, yOff + rollH + chordH, w, keySectionH,
                        m_chromaCache.hdcDib, 0, rollH, w, keySectionH);
            }
            DrawChordPanel(dc, 0, yOff + rollH, w, chordH);
            CCC_CaptionPaint(dc, m_hWnd);
            return;
        }
        // BlitRect は dest=src 座標前提のため、キャプションオフセット時は Opaque 転送
        if (yOff <= 0) {
            if (m_rollReady)
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, 0, w, rollH);
            if (m_keyBufReady)
                m_chromaCache.BlitRect(dc.GetSafeHdc(), 0, rollH, w, keySectionH);
        }
        else if (m_chromaCache.hdcDib) {
            if (m_rollReady)
                CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, yOff, w, rollH,
                    m_chromaCache.hdcDib, 0, 0, w, rollH);
            if (m_keyBufReady)
                CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, yOff + rollH, w, keySectionH,
                    m_chromaCache.hdcDib, 0, rollH, w, keySectionH);
        }
        CCC_CaptionPaint(dc, m_hWnd);
        return;
    }
#endif
    // 非アクリル / クロマ失敗: フレームバッファへ完全合成 → 画面へ1回 BitBlt
    const int yOffFb = CCC_GetCustomCaptionHeight(m_hWnd);
    if (!EnsureFrameBuffer(dc, w, h) || !m_frameDC.GetSafeHdc()) {
        if (m_rollReady)
            dc.BitBlt(0, yOffFb, w, rollH, &m_rollDC, 0, 0, SRCCOPY);
        if (chordH > 0)
            DrawChordPanel(dc, 0, yOffFb + rollH, w, chordH);
        if (m_keyBufReady)
            dc.BitBlt(0, yOffFb + rollH + chordH, w, keySectionH, &m_keyDC, 0, 0, SRCCOPY);
        if (haveLegend)
            DrawExprLegend(dc, w, rollH);
        if (m_frozen) {
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(RGB(255, 180, 80));
            CFont* of = nullptr;
            if (m_fontMeterTag.GetSafeHandle())
                of = dc.SelectObject(&m_fontMeterTag);
            dc.TextOut(8, yOffFb + 4, LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
            if (of) dc.SelectObject(of);
        }
        CCC_MainLockPaintClient(dc, m_hWnd);
        CCC_CaptionPaint(dc, m_hWnd);
        return;
    }

    if (m_rollReady)
        m_frameDC.BitBlt(0, 0, w, rollH, &m_rollDC, 0, 0, SRCCOPY);
    else
        m_frameDC.FillSolidRect(0, 0, w, rollH, RGB(18, 18, 22));
    if (chordH > 0)
        DrawChordPanel(m_frameDC, 0, rollH, w, chordH);
    if (m_keyBufReady)
        m_frameDC.BitBlt(0, rollH + chordH, w, keySectionH, &m_keyDC, 0, 0, SRCCOPY);
    else
        m_frameDC.FillSolidRect(0, rollH + chordH, w, keySectionH, RGB(28, 28, 32));

    if (haveLegend)
        DrawExprLegend(m_frameDC, w, rollH);

    if (m_frozen) {
        m_frameDC.SetBkMode(TRANSPARENT);
        m_frameDC.SetTextColor(RGB(255, 180, 80));
        CFont* of = nullptr;
        if (m_fontMeterTag.GetSafeHandle())
            of = m_frameDC.SelectObject(&m_fontMeterTag);
        m_frameDC.TextOut(8, 4, LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
        if (of) m_frameDC.SelectObject(of);
    }
    CCC_MainLockPaintClient(m_frameDC, m_hWnd);
    const int yOff = CCC_GetCustomCaptionHeight(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    if (!CCC_IsAeroEnabled() && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11())
        CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, yOff, w, h, m_frameDC.GetSafeHdc(), 0, 0, w, h);
    else
#endif
        dc.BitBlt(0, yOff, w, h, &m_frameDC, 0, 0, SRCCOPY);
    CCC_CaptionPaint(dc, m_hWnd);
}

void CPianoRoll::PresentClientFromBuffers(CPaintDC& dc, int w, int h, int rollH, int keySectionH)
{
    PresentFinalFrame(dc, w, h, rollH, keySectionH, IsView3D() ? 0 : ChordPanelHeightPx());
    if (::IsWindow(m_hWnd)) {
        MSG msg;
        while (::PeekMessage(&msg, m_hWnd, WM_PIANOROLL_ANALYSIS_DONE, WM_PIANOROLL_ANALYSIS_DONE, PM_REMOVE)) {}
        while (::PeekMessage(&msg, m_hWnd, WM_PIANOROLL_SYNC, WM_PIANOROLL_SYNC, PM_REMOVE)) {}
    }
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_syncPosted, 0);
}

#if CCUSTOM_AERO_SUPPORT
void CPianoRoll::BakeMainFollowOverlayIntoChroma(int w, int h, int rollH, int keySectionH)
{
    if (!m_chromaCache.hdcDib || !m_chromaReady)
        return;

    // ScrollRows で前フレームのロック表示が上へ流れるため、現在の矩形だけでなく
    // ヘッダー行全幅を下地へ戻してから焼き直す（アナライザと同方針）。
    CRect lockRc;
    CCC_MainLockGetOverlayRect(m_hWnd, lockRc);
    if (!lockRc.IsRectEmpty()) {
        CRect headerRow(0, lockRc.top, w, lockRc.bottom);
        if (headerRow.top < 0)
            headerRow.top = 0;
        if (headerRow.bottom > h)
            headerRow.bottom = h;

        CRect rollPart = headerRow;
        if (rollPart.bottom > rollH)
            rollPart.bottom = rollH;
        if (rollPart.top < rollPart.bottom && m_rollReady && m_rollDC.GetSafeHdc()) {
            m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(),
                rollPart.left, rollPart.top, rollPart.left, rollPart.top,
                rollPart.Width(), rollPart.Height(), PIANO_CHROMA_KEY);
        }
        CRect keyPart = headerRow;
        if (keyPart.top < rollH)
            keyPart.top = rollH;
        if (keyPart.bottom > rollH + keySectionH)
            keyPart.bottom = rollH + keySectionH;
        if (keyPart.top < keyPart.bottom && m_keyBufReady && m_keyDC.GetSafeHdc()) {
            m_chromaCache.UpdateRect(m_keyDC.GetSafeHdc(),
                keyPart.left, keyPart.top - rollH, keyPart.left, keyPart.top,
                keyPart.Width(), keyPart.Height(), PIANO_CHROMA_KEY);
        }
    }

    CDC dcCache;
    dcCache.Attach(m_chromaCache.hdcDib);
    // メモリDCの GetClipBox が空/不正だと MainLockPaint が即 return するため、
    // 明示的に全面クリップを張る。
    CRgn fullRgn;
    fullRgn.CreateRectRgn(0, 0, w, h);
    dcCache.SelectClipRgn(&fullRgn);

    CRect frozenOpaque;
    if (m_frozen) {
        CString fr = LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد", L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu");
        CFont* of = nullptr;
        if (m_fontMeterTag.GetSafeHandle())
            of = dcCache.SelectObject(&m_fontMeterTag);
        CSize sz = dcCache.GetTextExtent(fr);
        const int bw = sz.cx + 12;
        const int bh = sz.cy + 8;
        frozenOpaque.SetRect(4, 2, 4 + bw, 2 + bh);
        dcCache.FillSolidRect(frozenOpaque, RGB(40, 32, 16));
        dcCache.SetBkMode(TRANSPARENT);
        dcCache.SetTextColor(RGB(255, 180, 80));
        dcCache.TextOut(8, 4, fr);
        if (of) dcCache.SelectObject(of);
    }
    CCC_MainLockPaintClient(dcCache, m_hWnd);
    dcCache.SelectClipRgn(NULL);
    dcCache.Detach();

    if (!lockRc.IsRectEmpty())
        m_chromaCache.MakeRectOpaque(lockRc.left, lockRc.top, lockRc.Width(), lockRc.Height());
    if (!frozenOpaque.IsRectEmpty())
        m_chromaCache.MakeRectOpaque(frozenOpaque.left, frozenOpaque.top, frozenOpaque.Width(), frozenOpaque.Height());
}
#endif

void CPianoRoll::OnPaint()
{
    CPaintDC dc(this);
    const DWORD paintStart = GetTickCount();
    if (m_paintDisabled) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }
    CRect rect;
    GetClientRect(&rect);
    const int w = rect.Width();
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    const int h = rect.Height() - capH;
    if (w <= 0 || h <= 0) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }

    // 簡易3D はクライアント全面を1枚のシーンとして扱う(鍵盤帯を分けない)。
    // 2D(既定)のときの分割・スクロール経路は従来のまま。
    // キャプション帯は除外した content 高さでレイアウト（食い込み防止）
    const bool view3D = IsView3D();
    int keyH = h * 20 / 100;
    if (keyH < 50) keyH = 50; if (keyH > 100) keyH = 100;
    const int chordH = view3D ? 0 : ChordPanelHeightPx();
    const int rollH = view3D ? h : (h - keyH - chordH);
    const int keySectionH = view3D ? 0 : keyH;
    if (rollH <= 0 || (!view3D && keySectionH <= 0)) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }

    // 追従ドラッグ中の軽量提示。保留フレーム/ダーティがあるときは通常経路で消化する。
    {
        int pendingQuick = 0;
        EnterCriticalSection(&m_cs);
        pendingQuick = m_framesPending;
        LeaveCriticalSection(&m_cs);
        const bool needBufUpdate = (pendingQuick > 0) || m_historyDirty || m_keyDirty || m_meterDirty
            || (InterlockedCompareExchange(&m_analysisPresentDirty, 0, 0) != 0);
        const bool keyBufOk = view3D || (m_keyBufReady && m_keyW == w && m_keyH == keySectionH);
        if (!needBufUpdate && CCC_MainLockPreferQuickPresent() && m_rollReady && keyBufOk
            && m_rollW == w && m_rollH == rollH) {
            PresentClientFromBuffers(dc, w, h, rollH, keySectionH);
            return;
        }
    }

    if (!view3D)
        UpdateChordHistoryFromKeyCodes();
    EnsurePaintFonts(w, keyH, rollH);
    if (!EnsureRollBuffer(dc, w, rollH) || (!view3D && !EnsureKeyBuffer(dc, w, keySectionH))) {
        InterlockedExchange(&m_analysisDonePosted, 0);
        InterlockedExchange(&m_syncPosted, 0);
        return;
    }

    NoteFrame liveSnap;
    bool activesCopy[KEY_COUNT];
    uint8_t bandMaskCopy[KEY_COUNT];
    float laneStrengthCopy[KEY_COUNT][3];
    float chFillCopy[PIANO_METER_CH_MAX];
    uint8_t exprCopy[KEY_COUNT];
    int chCountCopy = 0;
    chCountCopy = m_chMeterCount;
    memcpy(chFillCopy, m_chMeterFill, sizeof(chFillCopy));
    EnterCriticalSection(&m_cs);
    BuildLiveNoteFrame(liveSnap);
    memcpy(activesCopy, m_activeKeys, sizeof(m_activeKeys));
    memcpy(bandMaskCopy, m_bandMask, sizeof(m_bandMask));
    memcpy(laneStrengthCopy, m_laneStrength, sizeof(m_laneStrength));
    memcpy(exprCopy, m_exprFlags, sizeof(m_exprFlags));
    LeaveCriticalSection(&m_cs);

    // メーター変化だけで 108 鍵フル再描画すると UI スレッドを食いつぶし EQ が数秒に1回になる。
    // 鍵盤状態が汚れていないときはメーター帯だけ更新する。
    const bool meterOnlyDirty = m_meterDirty && !m_keyDirty && m_keyBufReady;

    int pending = 0;
    EnterCriticalSection(&m_cs);
    pending = m_framesPending;
    LeaveCriticalSection(&m_cs);

    const bool rollDirty = m_historyDirty;
    const bool needKeyDraw = !view3D && (m_keyDirty || !m_keyBufReady);
    bool didRollUpdate = false;
    bool didRollScroll = false;
    bool needAnotherRollFrame = false;
    bool didMeterOnly = false;

    // 簡易3D は差分スクロールを持たない。毎フレーム、シーンを丸ごと描き直す。
    if (view3D) {
        Capture3DWalls();
        Draw3DSceneToBuffer(m_rollDC, w, rollH, activesCopy, chFillCopy, chCountCopy, exprCopy);
        if (m_soft3dTourUntil != 0 && GetTickCount() < m_soft3dTourUntil) {
            m_rollDC.SetBkMode(TRANSPARENT);
            m_rollDC.SetTextColor(RGB(240, 245, 255));
            m_rollDC.TextOut(8, 8, LL14(
                L"ドラッグ=回転 ホイール=ズーム 0=リセット",
                L"Drag=rotate  Wheel=zoom  0=reset",
                L"Glisser=rotation  Molette=zoom  0=reinit.",
                L"Trascina=ruota  Rotella=zoom  0=reset",
                L"Arrastrar=rotar  Rueda=zoom  0=restablecer",
                L"드래그=회전  휠=줌  0=리셋",
                L"拖动=旋转  滚轮=缩放  0=重置",
                L"سحب=دوران  عجلة=تكبير  0=إعادة",
                L"Перетащ.=поворот  Колесо=масштаб  0=сброс",
                L"Ziehen=drehen  Rad=Zoom  0=Reset",
                L"Arrastar=girar  Roda=zoom  0=redefinir",
                L"Sleep=draaien  Wiel=zoom  0=reset",
                L"Przeciagnij=obrot  Kolo=zoom  0=reset",
                L"Surukle=don  Tekerlek=zoom  0=sifirla"));
        }
        else if (m_soft3dTourUntil != 0 && GetTickCount() >= m_soft3dTourUntil) {
            m_soft3dTourUntil = 0;
        }
        if ((savedata.soft3dPerfHintDismiss & 8) == 0) {
            static DWORD s_prSoftT0 = 0;
            static int s_prSlow = 0;
            static DWORD s_prHintUntil = 0;
            const DWORD now = GetTickCount();
            if (s_prSoftT0 != 0) {
                const DWORD spent = now - s_prSoftT0;
                if (spent >= 32) {
                    if (++s_prSlow >= 40) {
                        s_prSlow = 0;
                        s_prHintUntil = now + 4000;
                    }
                } else if (s_prSlow > 0) {
                    --s_prSlow;
                }
            }
            s_prSoftT0 = now;
            if (now < s_prHintUntil) {
                m_rollDC.SetBkMode(TRANSPARENT);
                m_rollDC.SetTextColor(RGB(255, 190, 160));
                m_rollDC.TextOut(8, 26, LL14(
                    L"重いときは右クリックから 2D に戻せます",
                    L"Feeling heavy? Switch back to 2D from the context menu",
                    L"Trop lourd ? Revenez en 2D via le menu contextuel",
                    L"Troppo pesante? Torna al 2D dal menu contestuale",
                    L"¿Va lento? Vuelva a 2D desde el menú contextual",
                    L"무거우면 우클릭에서 2D로 돌릴 수 있습니다",
                    L"若觉得卡，可从右键菜单切回 2D",
                    L"ثقيل؟ عد إلى 2D من قائمة السياق",
                    L"Тяжело? Вернитесь в 2D через контекстное меню",
                    L"Zu zäh? Über das Kontextmenü zurück zu 2D",
                    L"Pesado? Volte ao 2D pelo menu de contexto",
                    L"Te zwaar? Ga terug naar 2D via het contextmenu",
                    L"Za ciężko? Wróć do 2D z menu kontekstowego",
                    L"Ağır mı? Bağlam menüsünden 2B'ye dönün"));
            }
        }
        EnterCriticalSection(&m_cs);
        m_framesPending = 0;
        LeaveCriticalSection(&m_cs);
        m_rollScrollValid = false;
        m_rollReady = true;
        m_keyBufReady = false;
        didRollUpdate = true;
        didRollScroll = false;
    }
    // pending 分は1回の BitBlt スクロールで消化（n 回フル転送しない）。
    else if (pending > 0 && m_rollReady) {
        int n = pending;
        if (n > 3) n = 3;
        NoteFrame histSnap[3];
        int histCount = 0;
        if (n > 1) {
            EnterCriticalSection(&m_cs);
            const int avail = (m_historyCount < n) ? m_historyCount : n;
            for (int i = 0; i < avail; ++i)
                histSnap[i] = HistoryAt(i);
            histCount = avail;
            LeaveCriticalSection(&m_cs);
        }
        if (TryAdvanceRollBuffer(w, rollH, histCount, histSnap, n, liveSnap)) {
            EnterCriticalSection(&m_cs);
            m_framesPending -= n;
            if (m_framesPending < 0) m_framesPending = 0;
            needAnotherRollFrame = (m_framesPending > 0);
            LeaveCriticalSection(&m_cs);
            m_rollScrollValid = true;
            m_rollReady = true;
            didRollUpdate = true;
            didRollScroll = true;
        }
        else {
            NoteFrame histFull[MAX_HISTORY];
            int histFullCount = 0;
            EnterCriticalSection(&m_cs);
            CopyHistorySnapshot(histFull, MAX_HISTORY, histFullCount);
            LeaveCriticalSection(&m_cs);
            ComposeRollBuffer(m_rollDC, w, rollH, histFullCount, histFull, liveSnap);
            EnterCriticalSection(&m_cs);
            m_framesPending = 0;
            LeaveCriticalSection(&m_cs);
            m_rollScrollValid = true;
            m_rollReady = true;
            didRollUpdate = true;
            didRollScroll = false;
        }
    }
    else if (rollDirty || !m_rollReady) {
        NoteFrame histSnap[MAX_HISTORY];
        int histCount = 0;
        EnterCriticalSection(&m_cs);
        CopyHistorySnapshot(histSnap, MAX_HISTORY, histCount);
        LeaveCriticalSection(&m_cs);
        ComposeRollBuffer(m_rollDC, w, rollH, histCount, histSnap, liveSnap);
        EnterCriticalSection(&m_cs);
        m_framesPending = 0;
        LeaveCriticalSection(&m_cs);
        m_rollScrollValid = true;
        m_rollReady = true;
        didRollUpdate = true;
        didRollScroll = false;
    }
    else if (m_rollReady) {
        DrawPlayheadRow(m_rollDC, w, rollH, liveSnap);
        didRollUpdate = true;
    }

    if (needKeyDraw) {
        DrawKeyboardToBuffer(m_keyDC, w, keySectionH, keyH, activesCopy, bandMaskCopy, laneStrengthCopy, chFillCopy, chCountCopy, exprCopy);
        m_keyBufReady = true;
    }
    else if (meterOnlyDirty && m_keyDC.GetSafeHdc()) {
        // 鍵盤全体は触らず、上部メーター帯だけ差し替える（表示OFF時もダーティは落とす）。
        // オクターブ数字はメーターと同じ帯(y=1..labelH+1)に載るため、塗りつぶし後に必ず描き直す。
        // 描き忘れるとメーター更新のたびに数字が消え、点滅する。
        const int labelH = min(16, keyH / 4);
        if (labelH >= 4) {
            CRect meterStrip(2, 1, w - 2, labelH + 1);
            m_keyDC.FillSolidRect(meterStrip, RGB(150, 150, 155));
            if (m_showLevelMeter)
                DrawChannelDbBars(m_keyDC, meterStrip, chFillCopy, chCountCopy);
            if (m_paintFontsReady && m_fontKeyOct.GetSafeHandle()) {
                m_keyDC.SetBkMode(TRANSPARENT);
                CFont* pOldFont = m_keyDC.SelectObject(
                    CFont::FromHandle((HFONT)m_fontKeyOct.GetSafeHandle()));
                m_keyDC.SetTextColor(RGB(100, 100, 110));
                for (int i = 0; i < KEY_COUNT; ++i) {
                    const int midi = MIDI_BASE + i;
                    if (midi % 12 != 0) continue;
                    int xL, xR; GetWhiteKeyRect52(midi, w, xL, xR);
                    if (xR <= xL) continue;
                    CString oct; oct.Format(L"%d", PianoDraw::MidiOctaveNumber(midi));
                    CRect tr(xL + 2, 1, xR - 2, labelH + 1);
                    m_keyDC.DrawText(oct, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                m_keyDC.SelectObject(pOldFont);
            }
        }
        didMeterOnly = true;
    }

    // 凡例は m_rollDC に焼かない（PresentFinalFrame で最終面へ合成）。
    // 旧: 毎フレーム AlphaBlend+TransparentBlt+書き戻し → 長時間で GDI 劣化し EQ 飢餓。

#if CCUSTOM_AERO_SUPPORT
    if (savedata.aero == 1 && CCC_IsWin11()) {
        if (m_chromaW != w || m_chromaH != h) {
            m_chromaCache.Release();
            m_chromaReady = false;
            m_chromaW = w;
            m_chromaH = h;
        }
        if (m_chromaCache.Ensure(dc.GetSafeHdc(), w, h)) {
            if (m_rollReady && didRollUpdate) {
                if (didRollScroll && m_lastScrollPx > 0 && m_chromaReady
                    && m_lastScrollPx < rollH) {
                    // m_rollDC は凡例非含み。ScrollRows 前の凡例下地戻しは不要。
                    {
                        CRect lockRc;
                        CCC_MainLockGetOverlayRect(m_hWnd, lockRc);
                        if (!lockRc.IsRectEmpty() && m_rollDC.GetSafeHdc()) {
                            CRect headerRow(0, lockRc.top, w, lockRc.bottom);
                            if (headerRow.top < 0)
                                headerRow.top = 0;
                            if (headerRow.bottom > rollH)
                                headerRow.bottom = rollH;
                            if (headerRow.top < headerRow.bottom) {
                                m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(),
                                    headerRow.left, headerRow.top, headerRow.left, headerRow.top,
                                    headerRow.Width(), headerRow.Height(), PIANO_CHROMA_KEY);
                            }
                        }
                    }
                    m_chromaCache.ScrollRows(0, rollH, m_lastScrollPx);
                    int bandTop = m_lastScrollHealTop;
                    if (bandTop <= 0) {
                        const int rowPitch = HistoryRowPitch(rollH);
                        bandTop = rollH - m_lastScrollPx - rowPitch - 2;
                    }
                    if (bandTop < 0) bandTop = 0;
                    const int bandH = rollH - bandTop;
                    if (bandH > 0)
                        m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(), 0, bandTop, 0, bandTop, w, bandH, PIANO_CHROMA_KEY);
                }
                else {
                    m_chromaCache.UpdateRect(m_rollDC.GetSafeHdc(), 0, 0, 0, 0, w, rollH, PIANO_CHROMA_KEY);
                }
            }
            if (keySectionH <= 0) {
                // 簡易3D: 鍵盤帯を持たないので更新するのはロール面(=全面)だけ
            }
            else if (needKeyDraw || !m_chromaReady)
                m_chromaCache.UpdateRect(m_keyDC.GetSafeHdc(), 0, 0, 0, rollH, w, keySectionH, PIANO_CHROMA_KEY);
            else if (didMeterOnly) {
                const int labelH = min(16, keyH / 4);
                const int meterH = labelH + 2;
                if (meterH > 0)
                    m_chromaCache.UpdateRect(m_keyDC.GetSafeHdc(), 0, 0, 0, rollH, w, meterH, PIANO_CHROMA_KEY);
            }
            m_chromaReady = true;
        }
    }
#endif

    // 追従UI込みでオフスクリーン合成 → 画面へ1回だけ出す（凡例もここで最終面へ）
    PresentFinalFrame(dc, w, h, rollH, keySectionH, IsView3D() ? 0 : ChordPanelHeightPx());

    if (didRollUpdate)
        m_historyDirty = false;
    if (needKeyDraw || didMeterOnly || view3D) {
        // 簡易3D は鍵盤もメーターも同じシーンへ描いているのでここで落とす
        m_keyDirty = false;
        m_meterDirty = false;
    }
    // 描画完了後に ANALYSIS_DONE / SYNC を開放（背圧）。
    // 滞留メッセージがあれば破棄してポンプを空ける（次の EQ/timerp を通す）。
    if (::IsWindow(m_hWnd)) {
        MSG msg;
        while (::PeekMessage(&msg, m_hWnd, WM_PIANOROLL_ANALYSIS_DONE, WM_PIANOROLL_ANALYSIS_DONE, PM_REMOVE)) {}
        while (::PeekMessage(&msg, m_hWnd, WM_PIANOROLL_SYNC, WM_PIANOROLL_SYNC, PM_REMOVE)) {}
    }
    InterlockedExchange(&m_analysisDonePosted, 0);
    InterlockedExchange(&m_syncPosted, 0);

    // EQ コード更新を1件だけ先に捌く（ピアノ OnPaint 独占で g_eqKeyUiPosted が固まるのを防ぐ）
#ifndef WM_EQ_KEY_UPDATE
#define WM_EQ_KEY_UPDATE (WM_APP + 430)
#endif
    {
        MSG eqMsg;
        if (::PeekMessage(&eqMsg, NULL, WM_EQ_KEY_UPDATE, WM_EQ_KEY_UPDATE, PM_REMOVE)) {
            ::TranslateMessage(&eqMsg);
            ::DispatchMessage(&eqMsg);
        }
    }

    // 追い付き Post は「この OnPaint が軽かったとき」だけ。重い描画の直後に再キックすると
    // UI がピアノで埋まり、30分後に EQ が数回/秒・ロールがガクガクになる。
    const DWORD paintMs = GetTickCount() - paintStart;
    const bool presentDirty = (InterlockedExchange(&m_analysisPresentDirty, 0) != 0);
    if ((needAnotherRollFrame || presentDirty) && ::IsWindow(m_hWnd) && !m_paintDisabled) {
        const DWORD now = GetTickCount();
        int minMs = savedata.ms2;
        if (minMs < 16) minMs = 16;
        if (minMs > 960) minMs = 960;
        // 描画が周期以上かかった場合は dirty を残して次の解析/Sync に譲る
        const bool yieldUi = (paintMs >= (DWORD)minMs);
        const bool timingOk = (m_lastAnalysisDonePostTick == 0
            || (now - m_lastAnalysisDonePostTick) >= (DWORD)minMs);
        if (!yieldUi && timingOk) {
            if (InterlockedCompareExchange(&m_analysisDonePosted, 1, 0) == 0) {
                m_lastAnalysisDonePostTick = now;
                if (!PostMessage(WM_PIANOROLL_ANALYSIS_DONE, 0, 0)) {
                    InterlockedExchange(&m_analysisDonePosted, 0);
                    InterlockedExchange(&m_analysisPresentDirty, 1);
                }
            }
            else {
                InterlockedExchange(&m_analysisPresentDirty, 1);
            }
        }
        else {
            InterlockedExchange(&m_analysisPresentDirty, 1);
            // 重い直後だけ時刻を進め、ワーカーの直後 Post を抑止する
            if (yieldUi)
                m_lastAnalysisDonePostTick = now;
        }
    }

    // [デバッグ用] どの機能が有効な状態でビルド・実行されているかを画面に直接表示する。
    // これが表示されなければ、この CPianoRoll.cpp が実際には動いていない証拠になる。
    // 動作確認が済んだら、このブロックごと削除して構わない。
#if 0
    {
        CString dbg;
        dbg.Format(L"PR-DBG-v7  reattack=%s  ghostGuard=%s  impulsive=%s",
            m_reattackDetectEnabled ? L"ON" : L"off",
            m_harmonicGhostGuardEnabled ? L"ON" : L"off",
            m_impulsiveGhostSuppressEnabled ? L"ON" : L"off");
        CFont dbgFont;
        dbgFont.CreateFont(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        CFont* pOldDbg = dc.SelectObject(&dbgFont);
        dc.SetBkMode(OPAQUE);
        dc.SetBkColor(RGB(0, 0, 0));
        dc.SetTextColor(RGB(255, 255, 0));
        CRect dbgRect(4, 4, w - 4, 24);
        dc.DrawText(dbg, dbgRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(pOldDbg);
    }
#endif
}

void CPianoRoll::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1) {
        CRect rc; GetWindowRect(&rc);
        if (!IsIconic()) {
            savedata.pianorollx = rc.left; savedata.pianorolly = rc.top;
            savedata.pianorollw = rc.Width(); savedata.pianorollh = rc.Height();
        }
    }
    CCustomBlurDialogExBase::OnTimer(nIDEvent);
}


void CPianoRoll::OnSize(UINT nType, int cx, int cy)
{
    CCustomBlurDialogExBase::OnSize(nType, cx, cy);
    ReleasePaintBuffers();
    m_historyDirty = true;
    m_keyDirty = true;
    m_framesPending = 0;
#if CCUSTOM_AERO_SUPPORT
    // Finalize の再実行はしない。DWM 属性の軽い再適用のみ。
    if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
        CCC_RefreshDwmBlur(m_hWnd);
#endif
    if (nType != SIZE_MINIMIZED) {
        CCC_CaptionLayout(m_hWnd);
        LayoutHelpBtn();
    }
    InvalidateRegions(true, true);
}
void CPianoRoll::OnMove(int x, int y)
{
    CCustomBlurDialogExBase::OnMove(x, y);
    // 簡易ピアノロールは高頻度更新のため Move 毎の DWM 再合成は行わない（重い）
}

void CPianoRoll::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
#if CCUSTOM_AERO_SUPPORT
    // 基底側で Apply/Refresh 済み。内容の再同期のみ行う。
    if (bShow && CCC_IsAeroEnabled())
    {
        m_keyDirty = true;
        m_rollScrollValid = false;
        RequestSyncFromMainUi();
        Invalidate(FALSE);
    }
#endif
    if (bShow) {
        m_rollReady = false;
        m_rollScrollValid = false;
        m_historyDirty = true;
    }
}


void CPianoRoll::OnClose()
{
    DetachForDestroy();
    savedata.pianorollwindow = 0;
    DestroyWindow();
}

void CPianoRoll::OnDestroy()
{
    if (g_prHelpDlg && ::IsWindow(g_prHelpDlg->GetSafeHwnd()))
        g_prHelpDlg->DestroyWindow();
    CCustomBlurDialogExBase::OnDestroy();
}

void CPianoRoll::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CPianoRoll::ShowHelpSheet()
{
    if (g_prHelpDlg && ::IsWindow(g_prHelpDlg->GetSafeHwnd())) {
        CCC_PresentOwnedHelp(g_prHelpDlg, this);
        return;
    }
    if (g_prHelpDlg && !::IsWindow(g_prHelpDlg->GetSafeHwnd()))
        g_prHelpDlg = nullptr;
    CPrHelpDlg* dlg = new CPrHelpDlg(this);
    if (!dlg->Create(IDD_PR_HELP, this)) {
        delete dlg;
        return;
    }
    g_prHelpDlg = dlg;
    CCC_PresentOwnedHelp(dlg, this);
}

void CPianoRoll::OnBnClickedHelp()
{
    ShowHelpSheet();
}
// 簡易3D 表示中のドラッグで視点を回す。2D 表示中は何もせず基底へ渡すので、
// 従来のウィンドウドラッグ/「メインに追従」チェックの挙動は変わらない。
void CPianoRoll::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (IsView3D() && !CCC_MainLockOverlayHitTest(m_hWnd, point)) {
        m_rotDragging = true;
        m_rotDragOrigin = point;
        m_rotDragYaw0 = m_view3dYawDeg;
        m_rotDragPitch0 = m_view3dPitchDeg;
        SetCapture();
        return;   // 基底のウィンドウドラッグへは渡さない
    }
    CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CPianoRoll::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_rotDragging) {
        if (!(nFlags & MK_LBUTTON)) {
            m_rotDragging = false;
            if (::GetCapture() == m_hWnd) ::ReleaseCapture();
            Save3DAngles();
            return;
        }
        float yaw = m_rotDragYaw0 + (float)(point.x - m_rotDragOrigin.x) * 0.35f;
        float pitch = m_rotDragPitch0 - (float)(point.y - m_rotDragOrigin.y) * 0.30f;
        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
        if (pitch < kView3dPitchMin) pitch = kView3dPitchMin;
        if (pitch > kView3dPitchMax) pitch = kView3dPitchMax;
        if (yaw != m_view3dYawDeg || pitch != m_view3dPitchDeg) {
            m_view3dYawDeg = yaw;
            m_view3dPitchDeg = pitch;
            m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
            m_chromaReady = false;   // 下地が全面変わるのでクロマを貼り直す
#endif
            if (::IsWindow(m_hWnd))
                Invalidate(FALSE);
        }
        return;
    }
    CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CPianoRoll::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_rotDragging) {
        m_rotDragging = false;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
        Save3DAngles();
        return;
    }
    CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

BOOL CPianoRoll::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    UNREFERENCED_PARAMETER(nFlags);
    UNREFERENCED_PARAMETER(pt);
    if (!IsView3D())
        return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);

    // 上へ回す=拡大。1ノッチあたり約10%。
    float zoom = m_view3dZoom;
    if (zDelta > 0)
        zoom *= 1.10f;
    else if (zDelta < 0)
        zoom /= 1.10f;
    if (zoom < kView3dZoomMin) zoom = kView3dZoomMin;
    if (zoom > kView3dZoomMax) zoom = kView3dZoomMax;
    if (zoom != m_view3dZoom) {
        m_view3dZoom = zoom;
        Save3DAngles();
        m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
        m_chromaReady = false;
#endif
        if (::IsWindow(m_hWnd))
            Invalidate(FALSE);
    }
    return TRUE;
}

BOOL CPianoRoll::PreTranslateMessage(MSG* pMsg)
{
    if (m_tooltip.GetSafeHwnd())
        m_tooltip.RelayEvent(pMsg);
    // V=表示モード切替 / N=ノート名。修飾キー併用時と入力コントロール上では既定処理へ。
    bool textFocus = false;
    if (pMsg->message == WM_KEYDOWN) {
        wchar_t cls[32] = { 0 };
        HWND hFocus = ::GetFocus();
        if (hFocus && ::GetClassName(hFocus, cls, 32) > 0)
            textFocus = (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ComboBox") == 0);
    }
    // Ctrl+K = メディアプレイヤーのコマンドパレット
    if (pMsg->message == WM_KEYDOWN && !textFocus && pMsg->wParam == 'K'
        && (::GetKeyState(VK_CONTROL) & 0x8000)
        && !(::GetKeyState(VK_MENU) & 0x8000)) {
        extern CMediaPlayerDlg* mp;
        if (mp && ::IsWindow(mp->GetSafeHwnd())) {
            mp->OpenCommandPalette();
            return TRUE;
        }
    }
    if (pMsg->message == WM_KEYDOWN && !textFocus
        && !(::GetKeyState(VK_CONTROL) & 0x8000)
        && !(::GetKeyState(VK_MENU) & 0x8000)) {
        if ((pMsg->wParam == '0' || pMsg->wParam == VK_NUMPAD0) && IsView3D()) {
            m_view3dYawDeg = -22.0f;
            m_view3dPitchDeg = 26.0f;
            m_view3dZoom = 1.0f;
            savedata.pianoroll3dyaw = -220;
            savedata.pianoroll3dpitch = 260;
            savedata.pianoroll3dzoom = 100;
            m_historyDirty = true;
#if CCUSTOM_AERO_SUPPORT
            m_chromaReady = false;
#endif
            if (::IsWindow(m_hWnd))
                Invalidate(FALSE);
            return TRUE;
        }
        if (pMsg->wParam == 'V') {
            SetViewMode(IsView3D() ? 0 : 1);
            return TRUE;
        }
        if (pMsg->wParam == 'N') {
            ToggleNoteNames();
            return TRUE;
        }
    }
    return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}