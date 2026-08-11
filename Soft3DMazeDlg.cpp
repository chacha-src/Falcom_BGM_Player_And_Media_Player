// Soft3DMazeDlg.cpp — Soft3D 迷路（ミニマップ／訪問／コンテキスト設定／曲連動）

#include "stdafx.h"
#include "ogg.h"
#include "Soft3DMazeDlg.h"
#include "CMediaPlayerDlg.h"
#include "CCustomPopupMenu.h"
#include "oggDlg.h"
#include "DatArchive.h"
#include <math.h>
#include <gdiplus.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void MpPersistSavedataQuick();
extern void MpTaskbarNextTrack();
extern CMediaPlayerDlg* mp;
extern COggDlg* og;
extern int tempo;
extern int pitch;
extern int playf;

namespace {

static const int kPresets[] = { 10, 20, 30, 50, 80, 100, 150, 200, 300, 400 };
static const int kPresetCnt = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
static const int kMapSizes[] = { 8, 10, 12, 14, 16 };
static const int kLegacySizes[] = { 11, 15, 21, 31 };

#if _UNICODE
#define S3M_RUN_LEAF L"oggYSEDbgmu_s3mrun.dat"
#else
#define S3M_RUN_LEAF "oggYSEDbgm_s3mrun.dat"
#endif
static const DWORD S3M_RUN_MAGIC = 0x53334D31u; // '1M3S'

static float S3mNormAngle(float a)
{
	while (a > (float)M_PI) a -= (float)(M_PI * 2.0);
	while (a < -(float)M_PI) a += (float)(M_PI * 2.0);
	return a;
}
static float S3mSnapYaw(float y)
{
	const float q = (float)(M_PI * 0.5);
	return floorf(y / q + 0.5f) * q;
}
static float S3mAngleDelta(float from, float to)
{
	return S3mNormAngle(to - from);
}

// 茶色レンガの手続きテクスチャ（初回だけ生成）
static GdiSoft3D::Texture& S3mBrickTexture()
{
	static GdiSoft3D::Texture tex;
	static BOOL ready = FALSE;
	if (!ready) {
		const int W = 64, H = 64;
		tex.w = W;
		tex.h = H;
		tex.pixels.resize((size_t)W * (size_t)H);
		for (int y = 0; y < H; y++) {
			for (int x = 0; x < W; x++) {
				const int row = y / 8;
				const int xoff = (row & 1) ? 16 : 0;
				const int bx = (x + xoff) & 31;
				const int by = y & 7;
				const BOOL mortar = (by == 0 || by == 7 || bx == 0 || bx == 31);
				int r, g, b;
				if (mortar) {
					r = 92; g = 72; b = 55;
				} else {
					const int n = ((x * 13 + y * 7 + row * 3) & 15) - 7;
					r = 158 + n;
					g = 98 + n / 2;
					b = 62 + n / 3;
					if (r < 110) r = 110; if (r > 190) r = 190;
					if (g < 60) g = 60; if (g > 130) g = 130;
					if (b < 40) b = 40; if (b > 90) b = 90;
				}
				tex.pixels[(size_t)y * W + (size_t)x] =
					GdiSoftFB::PackBGRA(255, (BYTE)r, (BYTE)g, (BYTE)b);
			}
		}
		ready = TRUE;
	}
	return tex;
}

class CS3mHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_S3M_HELP };
	explicit CS3mHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK() { DestroyWindow(); }
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose() { DestroyWindow(); }
	DECLARE_MESSAGE_MAP()
};
static CS3mHelpDlg* g_s3mHelp = nullptr;

BEGIN_MESSAGE_MAP(CS3mHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CS3mHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	SetWindowText(LL14(
		L"Soft3D迷路ガイド", L"Soft3D maze guide", L"Guide labyrinthe Soft3D", L"Guida labirinto Soft3D",
		L"Guía laberinto Soft3D", L"Soft3D 미로 가이드", L"Soft3D 迷宫指南", L"دليل متاهة Soft3D",
		L"Руководство Soft3D-лабиринта", L"Soft3D-Labyrinth-Anleitung", L"Guia labirinto Soft3D", L"Soft3D-doolhofgids",
		L"Przewodnik labiryntu Soft3D", L"Soft3D labirent kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CS3mHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_s3mHelp == this) g_s3mHelp = nullptr;
	delete this;
}
BOOL CS3mHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CS3mHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp)) return;
	CDC& dc = hp.mem;
	dc.SetBkMode(TRANSPARENT);
	dc.SelectObject(GetFont());
	TEXTMETRIC tm{}; dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	CRect rc; GetClientRect(&rc);
	int y = 8; const int L = 10;
	const int contentW = max(200, rc.Width() - L * 2);

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"Soft3D 迷路 — 操作とパーツの見方", L"Soft3D maze — controls and parts", L"Labyrinthe Soft3D — commandes et pièces", L"Labirinto Soft3D — comandi e pezzi",
		L"Laberinto Soft3D — controles y piezas", L"Soft3D 미로 — 조작과 파트", L"Soft3D 迷宫 — 操作与部件", L"متاهة Soft3D — التحكم والأجزاء",
		L"Лабиринт Soft3D — управление и детали", L"Soft3D-Labyrinth — Steuerung und Teile", L"Labirinto Soft3D — controles e peças", L"Soft3D-doolhof — bediening en onderdelen",
		L"Labirynt Soft3D — sterowanie i elementy", L"Soft3D labirent — kontroller ve parçalar"));
	y += lh + 6;

	// Soft3D 実演（通路・壁・窓・アイテム・ゴールが動く）
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, contentW, min(150, max(120, rc.Height() / 4)),
		CCC_HELPDEMO_KMAZE);

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"画面のパーツ", L"On-screen parts", L"Éléments à l'écran", L"Parti a schermo", L"Piezas en pantalla",
		L"화면 파트", L"画面部件", L"أجزاء الشاشة", L"Элементы на экране", L"Teile auf dem Bildschirm", L"Peças na tela", L"Onderdelen op het scherm",
		L"Elementy na ekranie", L"Ekrandaki parçalar"));
	y += lh + 2;
	dc.SetTextColor(RGB(65, 65, 80));
	auto line = [&](LPCTSTR t) { dc.TextOut(L, y, t); y += lh; };
	line(LL14(L"・灰の箱 = 壁　・水色ネオン = 窓（通れないが見た目のアクセント）",
		L"· Grey boxes = walls  · Cyan neon = windows (blocked, visual accent)",
		L"· Boîtes grises = murs  · Néon cyan = fenêtres (bloquées, accent)",
		L"· Scatole grigie = muri  · Neon ciano = finestre (bloccate, accento)",
		L"· Cajas grises = paredes  · Neón cian = ventanas (bloquean, acento)",
		L"· 회색 상자 = 벽  · 하늘색 네온 = 창(통과 불가, 장식)",
		L"· 灰盒 = 墙  · 水色霓虹 = 窗（不可走，装饰）",
		L"· صناديق رمادية = جدران  · نيون سماوي = نوافذ (لا مرور، زينة)",
		L"· Серые блоки = стены  · Голубой неон = окна (не проходимы)",
		L"· Graue Boxen = Wände  · Cyan-Neon = Fenster (nicht begehbar)",
		L"· Caixas cinza = paredes  · Neon ciano = janelas (bloqueiam)",
		L"· Grijze dozen = muren  · Cyaan neon = ramen (niet door)",
		L"· Szare bloki = ściany  · Cyjan neon = okna (nieprzechodnie)",
		L"· Gri kutular = duvar  · Camgöbeği neon = pencere (geçilmez)"));
	line(LL14(L"・金ネオン = ゴール　・手前の黄マーク = あなた（進行方向が上）",
		L"· Gold neon = goal  · Front yellow mark = you (forward is up)",
		L"· Néon or = but  · Marque jaune avant = vous (avant en haut)",
		L"· Neon oro = traguardo  · Marca gialla = tu (avanti in alto)",
		L"· Neón dorado = meta  · Marca amarilla = tú (adelante arriba)",
		L"· 금 네온 = 골  · 앞쪽 노란 표시 = 당신(진행이 위)",
		L"· 金色霓虹 = 终点  · 前方黄标 = 你（前进朝上）",
		L"· نيون ذهبي = الهدف  · علامة صفراء = أنت (الأمام أعلى)",
		L"· Золотой неон = цель  · Жёлтая метка = вы (вперёд вверх)",
		L"· Gold-Neon = Ziel  · Gelbe Marke = Sie (Vorwärts oben)",
		L"· Neon ouro = gol  · Marca amarela = você (frente para cima)",
		L"· Gouden neon = doel  · Gele markering = jij (vooruit omhoog)",
		L"· Złoty neon = cel  · Żółty znacznik = ty (przód u góry)",
		L"· Altın neon = hedef  · Sarı işaret = siz (ileri yukarı)"));
	line(LL14(L"・浮遊球: 緑=テンポ↑ / 橙=ピッチ↑ / 青=ピッチ↓ / 赤=次曲 / 紫=EQ",
		L"· Floating orbs: green=tempo↑ / orange=pitch↑ / blue=pitch↓ / red=next / purple=EQ",
		L"· Sphères: vert=tempo↑ / orange=hauteur↑ / bleu=hauteur↓ / rouge=piste / violet=EQ",
		L"· Sfere: verde=tempo↑ / arancio=pitch↑ / blu=pitch↓ / rosso=brano / viola=EQ",
		L"· Orbes: verde=tempo↑ / naranja=tono↑ / azul=tono↓ / rojo=pista / morado=EQ",
		L"· 떠다니는 구: 녹=템포↑ / 주황=피치↑ / 파랑=피치↓ / 빨강=다음 / 보라=EQ",
		L"· 浮球：绿=速度↑ / 橙=音高↑ / 蓝=音高↓ / 红=下一曲 / 紫=EQ",
		L"· كرات: أخضر=إيقاع↑ / برتقالي=طبقة↑ / أزرق=طبقة↓ / أحمر=التالي / بنفسج=EQ",
		L"· Шары: зелёный=темп↑ / оранж.=высота↑ / синий=высота↓ / красный=трек / фиолет.=EQ",
		L"· Kugeln: grün=Tempo↑ / orange=Ton↑ / blau=Ton↓ / rot=Titel / violett=EQ",
		L"· Esferas: verde=tempo↑ / laranja=tom↑ / azul=tom↓ / vermelho=faixa / roxo=EQ",
		L"· Bollen: groen=tempo↑ / oranje=toon↑ / blauw=toon↓ / rood=nummer / paars=EQ",
		L"· Kule: zieleń=tempo↑ / pomarańcz=wys.↑ / nieb.=wys.↓ / czerwień=utwór / fiolet=EQ",
		L"· Küreler: yeşil=tempo↑ / turuncu=perde↑ / mavi=perde↓ / kırmızı=parça / mor=EQ"));
	y += 4;

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"操作", L"Controls", L"Commandes", L"Comandi", L"Controles",
		L"조작", L"操作", L"التحكم", L"Управление", L"Steuerung", L"Controles", L"Bediening", L"Sterowanie", L"Kontroller"));
	y += lh + 2;
	dc.SetTextColor(RGB(65, 65, 80));
	line(LL14(L"WASD / 矢印 = スムーズ移動　Q・E または ←→ = 旋回　生成 = 新規迷路",
		L"WASD / arrows = smooth move  Q/E or ←→ = turn  Generate = new maze",
		L"WASD / flèches = déplacement  Q/E ou ←→ = tourner  Générer = nouveau",
		L"WASD / frecce = movimento  Q/E o ←→ = gira  Genera = nuovo",
		L"WASD / flechas = movimiento  Q/E o ←→ = girar  Generar = nuevo",
		L"WASD / 화살표 = 이동  Q/E 또는 ←→ = 회전  생성 = 새 미로",
		L"WASD / 方向键 = 移动  Q/E 或 ←→ = 转向  生成 = 新迷宫",
		L"WASD / أسهم = حركة  Q/E أو ←→ = دوران  توليد = متاهة جديدة",
		L"WASD / стрелки = движение  Q/E или ←→ = поворот  Создать = новый",
		L"WASD / Pfeile = Bewegung  Q/E oder ←→ = Drehen  Erzeugen = neu",
		L"WASD / setas = mover  Q/E ou ←→ = girar  Gerar = novo",
		L"WASD / pijlen = bewegen  Q/E of ←→ = draaien  Genereren = nieuw",
		L"WASD / strzałki = ruch  Q/E lub ←→ = obrót  Generuj = nowy",
		L"WASD / oklar = hareket  Q/E veya ←→ = dönüş  Oluştur = yeni"));
	line(LL14(L"大きさは 10〜400（コンボで選択、または数字を直接入力）。右上ミニマップは進行方向が上、通過マスは着色。",
		L"Size is 10–400 (combo or type a number). Top-right minimap: forward-up; visited cells tinted.",
		L"Taille 10–400 (liste ou saisie). Minimap : avant en haut ; cases visitées teintées.",
		L"Dimensione 10–400 (elenco o digita). Minimap: avanti in alto; celle visitate colorate.",
		L"Tamaño 10–400 (lista o escribe). Minimapa: adelante arriba; celdas visitadas teñidas.",
		L"크기 10–400(콤보 또는 직접 입력). 우측 미니맵은 진행이 위, 방문 칸 색칠.",
		L"尺寸 10–400（下拉或手输）。右上小地图前进朝上，走过的格子着色。",
		L"الحجم 10–400 (قائمة أو اكتب). الخريطة: الأمام أعلى؛ الخلايا المزورة ملوّنة.",
		L"Размер 10–400 (список или ввод). Мини-карта: вперёд вверх; посещённые окрашены.",
		L"Größe 10–400 (Liste oder Tippen). Minimap: Vorwärts oben; besuchte Felder getönt.",
		L"Tamanho 10–400 (lista ou digite). Minimapa: frente cima; visitados coloridos.",
		L"Grootte 10–400 (lijst of typen). Minimapa: vooruit omhoog; bezocht getint.",
		L"Rozmiar 10–400 (lista lub wpisz). Minimapa: przód u góry; odwiedzone zabarwione.",
		L"Boyut 10–400 (liste veya yazın). Harita: ileri yukarı; ziyaret edilen boyalı."));
	line(LL14(L"右クリック: リスタート / サイズ / ミニマップ / アイテム種類。進行は自動保存（再オープンで続きから）。",
		L"Right-click: restart / size / minimap / item types. Progress auto-saves (resume on reopen).",
		L"Clic droit : redémarrer / taille / minimap / objets. Progression auto-sauvegardée.",
		L"Clic destro: riavvio / dimensione / minimap / oggetti. Progresso salvato automaticamente.",
		L"Clic derecho: reinicio / tamaño / minimapa / objetos. Progreso se guarda solo.",
		L"우클릭: 재시작 / 크기 / 미니맵 / 아이템. 진행 자동 저장(다시 열면 이어하기).",
		L"右键：重启 / 尺寸 / 小地图 / 道具。进度自动保存（再开可续玩）。",
		L"يمين: إعادة / حجم / خريطة / عناصر. يُحفظ التقدم تلقائيًا (الاستئناف عند الفتح).",
		L"ПКМ: перезапуск / размер / карта / предметы. Прогресс сохраняется (продолжить при открытии).",
		L"Rechtsklick: Neustart / Größe / Minimap / Items. Fortschritt speichert sich (Fortsetzen).",
		L"Direito: reiniciar / tamanho / minimapa / itens. Progresso salva sozinho (continuar).",
		L"Rechtsklik: herstart / grootte / minimap / items. Voortgang bewaart zich (hervatten).",
		L"PPM: restart / rozmiar / minimapa / przedmioty. Postęp zapisuje się (wznów).",
		L"Sağ tık: yeniden / boyut / harita / öğeler. İlerleme otomatik (açınca devam)."));
	line(LL14(L"アイテムを拾うと再生のテンポ・ピッチ・次曲・EQ が変わります（閉じるとテンポ/ピッチは復帰）。",
		L"Pickups change tempo, pitch, next track, EQ (tempo/pitch restore on close).",
		L"Les objets changent tempo, hauteur, piste, EQ (tempo/hauteur restaurés à la fermeture).",
		L"I pickup cambiano tempo, pitch, brano, EQ (tempo/pitch ripristinati alla chiusura).",
		L"Recoger cambia tempo, tono, pista, EQ (tempo/tono se restauran al cerrar).",
		L"아이템을 줍면 템포·피치·다음 곡·EQ가 바뀝니다(닫으면 템포/피치 복원).",
		L"拾取会改速度、音高、下一曲、EQ（关闭时恢复速度/音高）。",
		L"الالتقاط يغيّر الإيقاع والطبقة والمسار وEQ (يُعاد الإيقاع/الطبقة عند الإغلاق).",
		L"Подбор меняет темп, высоту, трек, EQ (темп/высота возвращаются при закрытии).",
		L"Aufsammeln ändert Tempo, Tonhöhe, Titel, EQ (Tempo/Tonhöhe beim Schließen zurück).",
		L"Coletar muda tempo, tom, faixa, EQ (tempo/tom voltam ao fechar).",
		L"Oppakken verandert tempo, toon, nummer, EQ (tempo/toon terug bij sluiten).",
		L"Podniesienie zmienia tempo, wysokość, utwór, EQ (tempo/wysokość wracają po zamknięciu).",
		L"Toplamak tempo, perde, parça, EQ değiştirir (kapanınca tempo/perde geri gelir)."));
	y += 2;
	dc.SetTextColor(RGB(90, 90, 110));
	line(LL14(L"奥は弱いフォグのみ。見える距離まで壁・窓・アイテムを描画します。",
		L"Distance uses only light fog; walls/windows/items draw as far as visible.",
		L"Au loin : léger brouillard seulement ; murs/fenêtres/objets jusqu'à la visibilité.",
		L"In lontananza solo lieve nebbia; muri/finestre/oggetti fin dove si vedono.",
		L"A lo lejos solo niebla suave; paredes/ventanas/objetos hasta lo visible.",
		L"먼 곳은 약한 포그만. 보이는 거리까지 벽·창·아이템을 그립니다.",
		L"远处仅弱雾；墙、窗、道具画到可见距离。",
		L"البعيد ضباب خفيف فقط؛ تُرسم الجدران والنوافذ والعناصر حتى المدى المرئي.",
		L"Вдали только лёгкий туман; стены/окна/предметы — насколько видно.",
		L"Weit weg nur leichter Nebel; Wände/Fenster/Items bis zur Sichtweite.",
		L"Ao longe só névoa leve; paredes/janelas/itens até o visível.",
		L"Ver weg alleen lichte mist; muren/ramen/items tot zichtafstand.",
		L"W dali tylko lekka mgła; ściany/okna/przedmioty do zasięgu wzroku.",
		L"Uzakta yalnızca hafif sis; duvar/pencere/öğeler görünür mesafeye kadar."));
	CCC_GdiHelpEndPaint(hp);
}

static void S3mEqBump(int band, int delta)
{
	if (band < 0 || band > 14) return;
	int v = savedata.eq[band] + delta;
	if (v < 0) v = 0;
	if (v > 200) v = 200;
	savedata.eq[band] = v;
	savedata.eqsoundeq = 9;
}

static void S3mSetPitchPos(int pos)
{
	if (pos < 0) pos = 0;
	if (pos > 400) pos = 400;
	pitch = pos;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->m_pitch_sl.SetPos(pos);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_pitch.GetSafeHwnd())
		mp->m_pitch.SetPos(pos);
}

static int S3mClampMapSize(int n)
{
	if (n < 8) n = 8;
	if (n > 16) n = 16;
	if (n & 1) n++;
	return n;
}

static int S3mItemMask()
{
	int m = savedata.s3m_item_mask;
	if (m <= 0) m = CSoft3DMazeDlg::ITEM_ALL;
	return m & CSoft3DMazeDlg::ITEM_ALL;
}

static int S3mNormalizeSavedSize(int sz)
{
	if (sz >= 0 && sz <= 3)
		sz = kLegacySizes[sz];
	if (sz < CSoft3DMazeDlg::S3M_MIN) sz = 20;
	if (sz > CSoft3DMazeDlg::S3M_MAX) sz = CSoft3DMazeDlg::S3M_MAX;
	return sz;
}

} // namespace

static CSoft3DMazeDlg* g_s3m = NULL;

// ---- view ----
IMPLEMENT_DYNAMIC(CS3mView, CCustomStatic)

BEGIN_MESSAGE_MAP(CS3mView, CCustomStatic)
	ON_WM_PAINT()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_MBUTTONDOWN()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CS3mView::CS3mView() : m_ready(FALSE), m_dragging(0), m_dragTurnAcc(0) {}

int CS3mView::HitMoveDir(CPoint pt) const
{
	CRect rc;
	const_cast<CS3mView*>(this)->GetClientRect(&rc);
	const int w = max(1, rc.Width());
	const int h = max(1, rc.Height());
	const int third = w / 3;
	if (pt.x < third) return 0; // left
	if (pt.x >= w - third) return 1; // right
	if (pt.y < h / 2) return 2; // up / forward
	return 3; // down / back
}

void CS3mView::OnSize(UINT nType, int cx, int cy)
{
	CCustomStatic::OnSize(nType, cx, cy);
	if (cx > 8 && cy > 8)
		m_ready = m_ctx.Create(cx, cy) ? TRUE : FALSE;
}

BOOL CS3mView::PaintCustomOpaque(CDC& dc)
{
	CRect rc;
	GetClientRect(&rc);
	if (!m_ready || rc.Width() < 8 || rc.Height() < 8) {
		dc.FillSolidRect(rc, RGB(18, 20, 28));
		return TRUE;
	}
	m_ctx.Present(dc, 0, 0);
	if (CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent())
		dlg->PaintClearOverlay(dc, rc);
	return TRUE;
}

void CS3mView::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() <= 0 || rc.Height() <= 0)
		return;

	// キャプションアクリル下: 素 BitBlt は α=0 で消えるため BufferedPaint で不透明化
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (hdcBuf && hBP) {
		CDC dcBuf;
		dcBuf.Attach(hdcBuf);
		PaintCustomOpaque(dcBuf);
		dcBuf.Detach();
		::BufferedPaintMakeOpaque(hBP, &rc);
		::EndBufferedPaint(hBP, TRUE);
		return;
	}
	PaintCustomOpaque(dc);
}

LRESULT CS3mView::OnPrintClient(WPARAM wParam, LPARAM)
{
	if (HDC hDC = (HDC)wParam) {
		CDC dc;
		dc.Attach(hDC);
		PaintCustomOpaque(dc);
		dc.Detach();
	}
	return 0;
}

void CS3mView::OnContextMenu(CWnd*, CPoint point)
{
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (!dlg) return;
	CPoint sp = point;
	if (sp.x < 0) {
		CRect rc; GetWindowRect(&rc);
		sp.x = rc.left + 40; sp.y = rc.top + 40;
	}
	dlg->ShowContextMenu(sp);
}

void CS3mView::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_dragging = 1;
	m_dragOrigin = point;
	m_dragTurnAcc = 0;
	SetCapture();
	CCustomStatic::OnLButtonDown(nFlags, point);
}

void CS3mView::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragging) {
		const int dx = point.x - m_dragOrigin.x;
		const int dy = point.y - m_dragOrigin.y;
		CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
		if (dlg && abs(dx) < 18 && abs(dy) < 18) {
			switch (HitMoveDir(point)) {
			case 0: dlg->InputTurn(-1); break;
			case 1: dlg->InputTurn(1); break;
			case 2: dlg->InputStep(0, 1); break;
			default: dlg->InputStep(0, -1); break;
			}
		}
		m_dragging = 0;
		m_dragTurnAcc = 0;
		if (GetCapture() == this)
			ReleaseCapture();
	}
	CCustomStatic::OnLButtonUp(nFlags, point);
}

void CS3mView::OnMouseMove(UINT nFlags, CPoint point)
{
	// カーソルは OnSetCursor で IDC_C* を適用
	CCustomStatic::OnMouseMove(nFlags, point);
}

BOOL CS3mView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		UINT id = IDC_CUP;
		switch (HitMoveDir(pt)) {
		case 0: id = IDC_CLEFT; break;
		case 1: id = IDC_CRIGHT; break;
		case 2: id = IDC_CUP; break;
		default: id = IDC_CDOWN; break;
		}
		HCURSOR h = AfxGetApp()->LoadCursor(id);
		if (h) {
			::SetCursor(h);
			return TRUE;
		}
	}
	return CCustomStatic::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CS3mView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (dlg)
		dlg->InputTurn(zDelta > 0 ? -1 : 1);
	return TRUE;
}

void CS3mView::OnMButtonDown(UINT nFlags, CPoint point)
{
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (dlg)
		dlg->InputStep(0, -1); // 後退
	CCustomStatic::OnMButtonDown(nFlags, point);
}

// ---- dialog ----
IMPLEMENT_DYNAMIC(CSoft3DMazeDlg, CCustomBlurDialogBase)

CSoft3DMazeDlg::CSoft3DMazeDlg(CWnd* p)
	: CCustomBlurDialogBase(IDD, p)
	, m_grid(NULL), m_visit(NULL)
	, m_n(0), m_px(1.5f), m_pz(1.5f), m_yaw((float)M_PI)
	, m_yawTarget((float)M_PI), m_pxTarget(1.5f), m_pzTarget(1.5f)
	, m_turning(0), m_turnHeld(0), m_moving(0), m_moveHeld(0)
	, m_bob(0.f), m_won(0)
	, m_clearPhase(CLEAR_IDLE), m_clearT(0.f), m_clearTextA(0.f), m_clearScreenA(0.f)
	, m_itemsLeft(0)
	, m_baseTempoPos(200), m_basePitchPos(200)
	, m_lastTick(0), m_rng(GetTickCount())
	, m_lastAutosave(0), m_runDirty(0)
{
}

CSoft3DMazeDlg::~CSoft3DMazeDlg()
{
	FreeGrid();
}

void CSoft3DMazeDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_S3M_HELP, m_help);
	DDX_Control(pDX, IDC_S3M_SIZE_L, m_sizeL);
	DDX_Control(pDX, IDC_S3M_SIZE, m_size);
	DDX_Control(pDX, IDC_S3M_GEN, m_gen);
	DDX_Control(pDX, IDC_S3M_HINT, m_hint);
	DDX_Control(pDX, IDC_S3M_VIEW, m_view);
	DDX_Control(pDX, IDC_S3M_STATUS, m_status);
	DDX_Control(pDX, IDC_S3M_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CSoft3DMazeDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_S3M_GEN, &CSoft3DMazeDlg::OnGen)
	ON_BN_CLICKED(IDC_S3M_CLOSE, &CSoft3DMazeDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_S3M_HELP, &CSoft3DMazeDlg::OnHelp)
	ON_CBN_SELCHANGE(IDC_S3M_SIZE, &CSoft3DMazeDlg::OnSizeChanged)
	ON_CBN_EDITCHANGE(IDC_S3M_SIZE, &CSoft3DMazeDlg::OnSizeEditChange)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

BOOL CSoft3DMazeDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CSoft3DMazeDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_s3m == this) g_s3m = NULL;
	delete this;
}

void CSoft3DMazeDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CSoft3DMazeDlg::LayoutAll()
{
	if (!GetSafeHwnd() || !m_view.GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	const int cx = rc.Width();
	const int cy = rc.Height();
	if (cx < 200 || cy < 180)
		return;

	int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH < 0) capH = 0;

	const int m = 8;
	const int rowH = 24;
	const int btnH = 22;
	int y = capH + 6;

	if (m_sizeL.GetSafeHwnd())
		m_sizeL.SetWindowPos(NULL, m, y + 3, 48, 14, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_size.GetSafeHwnd())
		m_size.SetWindowPos(NULL, m + 52, y, 130, 200, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_gen.GetSafeHwnd())
		m_gen.SetWindowPos(NULL, m + 192, y, 64, 20, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_hint.GetSafeHwnd())
		m_hint.SetWindowPos(NULL, m + 266, y + 3, max(40, cx - (m + 266) - m), 14, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + 2;

	const int btnY = cy - m - btnH;
	int viewBottom = btnY - 8;
	if (viewBottom < y + 80) viewBottom = y + 80;
	m_view.SetWindowPos(NULL, m, y, max(40, cx - 2 * m), max(40, viewBottom - y), SWP_NOZORDER | SWP_NOACTIVATE);
	{
		CRect vr;
		m_view.GetClientRect(&vr);
		if (vr.Width() > 8 && vr.Height() > 8)
			m_view.m_ready = m_view.m_ctx.Create(vr.Width(), vr.Height()) ? TRUE : FALSE;
	}

	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, cx - m - 80, btnY, 80, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowPos(NULL, m, btnY + 3, max(40, cx - m - 80 - 12 - m), 16, SWP_NOZORDER | SWP_NOACTIVATE);

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
}

void CSoft3DMazeDlg::FreeGrid()
{
	delete[] m_grid;
	delete[] m_visit;
	m_grid = NULL;
	m_visit = NULL;
	m_n = 0;
}

BOOL CSoft3DMazeDlg::AllocGrid(int n)
{
	if (n < S3M_MIN || n > S3M_MAX)
		return FALSE;
	FreeGrid();
	const size_t nn = (size_t)n * (size_t)n;
	m_grid = new BYTE[nn];
	m_visit = new BYTE[nn];
	if (!m_grid || !m_visit) {
		FreeGrid();
		return FALSE;
	}
	m_n = n;
	memset(m_grid, 0, nn);
	memset(m_visit, 0, nn);
	return TRUE;
}

int CSoft3DMazeDlg::ReadSizeFromUi()
{
	CString s;
	if (m_size.GetSafeHwnd())
		m_size.GetWindowText(s);
	int n = _ttoi(s);
	if (n < S3M_MIN) n = S3M_MIN;
	if (n > S3M_MAX) n = S3M_MAX;
	return n;
}

void CSoft3DMazeDlg::SetSizeToUi(int n)
{
	if (n < S3M_MIN) n = S3M_MIN;
	if (n > S3M_MAX) n = S3M_MAX;
	CString s;
	s.Format(_T("%d"), n);
	if (!m_size.GetSafeHwnd())
		return;
	m_size.SetWindowText(s);
	// プリセットに無い値は CurSel を外す（論理 SetCurSel ではなく物理インデックス）
	CComboBox& cb = m_size;
	const int found = cb.FindStringExact(-1, s);
	cb.SetCurSel(found == CB_ERR ? -1 : found);
}

void CSoft3DMazeDlg::PersistUi()
{
	savedata.s3m_size = ReadSizeFromUi();
	savedata.s3m_minimap = S3mClampMapSize(savedata.s3m_minimap);
	if (savedata.s3m_show_map != 0) savedata.s3m_show_map = 1;
	savedata.s3m_item_mask = S3mItemMask();
	MpPersistSavedataQuick();
}

void CSoft3DMazeDlg::PersistRun()
{
	if (m_n <= 0 || !m_grid || !m_visit) return;

	CFile f;
	CString path = DatArc_Path(S3M_RUN_LEAF);
	if (f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) != TRUE)
		return;
	try {
		DWORD magic = S3M_RUN_MAGIC;
		f.Write(&magic, sizeof(magic));
		f.Write(&m_n, sizeof(m_n));
		f.Write(&m_px, sizeof(m_px));
		f.Write(&m_pz, sizeof(m_pz));
		f.Write(&m_yaw, sizeof(m_yaw));
		f.Write(&m_won, sizeof(m_won));
		const size_t nn = (size_t)m_n * (size_t)m_n;
		f.Write(m_grid, (UINT)nn);
		f.Write(m_visit, (UINT)nn);
	}
	catch (...) {
	}
	f.Close();
	DatArc_Commit(S3M_RUN_LEAF);

	savedata.s3m_have_run = 1;
	savedata.s3m_run_n = m_n;
	savedata.s3m_run_px = m_px;
	savedata.s3m_run_pz = m_pz;
	savedata.s3m_run_yaw = m_yaw;
	savedata.s3m_run_won = m_won ? 1 : 0;
	m_runDirty = 0;
	m_lastAutosave = GetTickCount();
	MpPersistSavedataQuick();
}

BOOL CSoft3DMazeDlg::LoadRun()
{
	if (!savedata.s3m_have_run)
		return FALSE;

	CFile f;
	CString path = DatArc_Path(S3M_RUN_LEAF);
	if (f.Open(path, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE)
		return FALSE;

	DWORD magic = 0;
	int n = 0;
	try {
		if (f.Read(&magic, sizeof(magic)) != sizeof(magic) || magic != S3M_RUN_MAGIC) {
			f.Close();
			return FALSE;
		}
		if (f.Read(&n, sizeof(n)) != sizeof(n) || n < S3M_MIN || n > S3M_MAX) {
			f.Close();
			return FALSE;
		}
		if (!AllocGrid(n)) {
			f.Close();
			return FALSE;
		}
		if (f.Read(&m_px, sizeof(m_px)) != sizeof(m_px)) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(&m_pz, sizeof(m_pz)) != sizeof(m_pz)) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(&m_yaw, sizeof(m_yaw)) != sizeof(m_yaw)) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(&m_won, sizeof(m_won)) != sizeof(m_won)) { FreeGrid(); f.Close(); return FALSE; }
		const size_t nn = (size_t)n * (size_t)n;
		if (f.Read(m_grid, (UINT)nn) != nn) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(m_visit, (UINT)nn) != nn) { FreeGrid(); f.Close(); return FALSE; }
		m_yaw = S3mSnapYaw(m_yaw);
		m_yawTarget = m_yaw;
		m_px = floorf(m_px) + 0.5f;
		m_pz = floorf(m_pz) + 0.5f;
		m_pxTarget = m_px;
		m_pzTarget = m_pz;
		m_turning = 0;
		m_turnHeld = 0;
		m_moving = 0;
		m_moveHeld = 0;
	}
	catch (...) {
		FreeGrid();
		f.Close();
		return FALSE;
	}
	f.Close();

	if (savedata.s3m_run_n > 0 && savedata.s3m_run_n != n)
		return FALSE;
	if (m_px < 0.5f || m_pz < 0.5f || m_px >= m_n - 0.5f || m_pz >= m_n - 0.5f)
		return FALSE;

	m_won = m_won ? 1 : 0;
	m_itemsLeft = 0;
	for (int z = 0; z < m_n; z++) {
		for (int x = 0; x < m_n; x++) {
			const BYTE c = CellAt(x, z);
			if (c >= CELL_TEMPO && c <= CELL_EQ)
				m_itemsLeft++;
		}
	}
	if (IsBlocked(m_px, m_pz)) {
		FreeGrid();
		return FALSE;
	}
	MarkVisited();
	UpdateStatus();
	SetSizeToUi(m_n);
	return TRUE;
}

void CSoft3DMazeDlg::CaptureAudioBaseline()
{
	m_baseTempoPos = tempo;
	m_basePitchPos = pitch;
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		m_baseTempoPos = og->m_tempo_sl.GetPos();
		m_basePitchPos = og->m_pitch_sl.GetPos();
	}
}

void CSoft3DMazeDlg::RestoreAudioBaseline()
{
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->ApplyPracticeTempoPercent(m_baseTempoPos / 2);
	else {
		tempo = m_baseTempoPos;
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->m_tempo_sl.SetPos(m_baseTempoPos);
	}
	S3mSetPitchPos(m_basePitchPos);
}

BOOL CSoft3DMazeDlg::IsBlocked(float x, float z) const
{
	if (m_n <= 0 || !m_grid) return TRUE;
	const int ix = (int)floorf(x);
	const int iz = (int)floorf(z);
	if (ix < 0 || iz < 0 || ix >= m_n || iz >= m_n)
		return TRUE;
	const BYTE c = CellAt(ix, iz);
	return (c == CELL_WALL || c == CELL_WINDOW) ? TRUE : FALSE;
}

// Soft3D Project: rx=x*c-z*s, rz=x*s+z*c
// → カメラ前方(+Z)のワールド方向=(sin,cos)、右(+X)=(cos,-sin)
void CSoft3DMazeDlg::CamBasisYaw(float yaw, float& fwdX, float& fwdZ, float& rightX, float& rightZ) const
{
	const float c = cosf(yaw);
	const float s = sinf(yaw);
	fwdX = s;
	fwdZ = c;
	rightX = c;
	rightZ = -s;
}

void CSoft3DMazeDlg::GetRenderEye(float& ex, float& ez) const
{
	// 論理位置はマス中央（移動・ミニマップ）。3D だけ前方 0.8。
	// 中央のままだと全向きで「半マス後ろ／横に寄って壁が半分を占める」ように見える。
	float fx, fz, rx, rz;
	CamBasisYaw(m_yaw, fx, fz, rx, rz);
	(void)rx; (void)rz;
	ex = m_px + fx * 0.8f;
	ez = m_pz + fz * 0.8f;
}

void CSoft3DMazeDlg::WorldToCam(float wx, float wz, float& lx, float& lz) const
{
	// カリング・ミニマップ用＝論理位置基準（▲はマス中央）
	const float dx = wx - m_px;
	const float dz = wz - m_pz;
	float fx, fz, rx, rz;
	CamBasisYaw(m_yaw, fx, fz, rx, rz);
	lx = dx * rx + dz * rz;
	lz = dx * fx + dz * fz;
}

void CSoft3DMazeDlg::WorldToMap(float wx, float wz, float& mx, float& my) const
{
	float lx, lz;
	WorldToCam(wx, wz, lx, lz);
	mx = lx;
	my = -lz;
}

void CSoft3DMazeDlg::MarkVisited()
{
	const int ix = (int)floorf(m_px);
	const int iz = (int)floorf(m_pz);
	if (ix < 0 || iz < 0 || ix >= m_n || iz >= m_n) return;
	if (!VisitAt(ix, iz)) {
		Visit(ix, iz) = 1;
		m_runDirty = 1;
	}
}

void CSoft3DMazeDlg::GenerateMaze()
{
	ResetClearFx();
	PersistUi();
	const int n = ReadSizeFromUi();
	if (!AllocGrid(n))
		return;

	m_won = 0;
	m_itemsLeft = 0;
	m_rng = GetTickCount() ^ ((DWORD)m_n * 2654435761u);
	if (savedata.s3m_seed)
		m_rng = (DWORD)savedata.s3m_seed;

	for (int z = 0; z < m_n; z++)
		for (int x = 0; x < m_n; x++)
			Cell(x, z) = CELL_WALL;

	const size_t cap = (size_t)m_n * (size_t)m_n;
	int* stackX = new int[cap];
	int* stackY = new int[cap];
	if (!stackX || !stackY) {
		delete[] stackX;
		delete[] stackY;
		FreeGrid();
		return;
	}

	int sp = 0;
	stackX[sp] = 1; stackY[sp] = 1; sp++;
	Cell(1, 1) = CELL_FLOOR;

	const int dx4[4] = { 0, 0, -2, 2 };
	const int dy4[4] = { -2, 2, 0, 0 };

	while (sp > 0) {
		const int x = stackX[sp - 1];
		const int y = stackY[sp - 1];
		int dirs[4];
		int nd = 0;
		for (int d = 0; d < 4; d++) {
			const int nx = x + dx4[d];
			const int ny = y + dy4[d];
			if (nx > 0 && ny > 0 && nx < m_n - 1 && ny < m_n - 1 && CellAt(nx, ny) == CELL_WALL)
				dirs[nd++] = d;
		}
		if (nd == 0) {
			sp--;
			continue;
		}
		m_rng = m_rng * 1664525u + 1013904223u;
		const int d = dirs[m_rng % (DWORD)nd];
		const int nx = x + dx4[d];
		const int ny = y + dy4[d];
		Cell(x + dx4[d] / 2, y + dy4[d] / 2) = CELL_FLOOR;
		Cell(nx, ny) = CELL_FLOOR;
		stackX[sp] = nx;
		stackY[sp] = ny;
		sp++;
	}
	delete[] stackX;
	delete[] stackY;

	const int sx = 1, sz = m_n - 2;
	const int gx = m_n - 2, gz = 1;
	Cell(sx, sz) = CELL_START;
	Cell(gx, gz) = CELL_GOAL;
	m_px = (float)sx + 0.5f;
	m_pz = (float)sz + 0.5f;
	m_pxTarget = m_px;
	m_pzTarget = m_pz;
	m_yaw = (float)M_PI;
	m_yawTarget = m_yaw;
	m_turning = 0;
	m_turnHeld = 0;
	m_moving = 0;
	m_moveHeld = 0;

	const int mask = S3mItemMask();
	if (mask & ITEM_WINDOW) {
		int winBudget = m_n / 3;
		for (int z = 1; z < m_n - 1 && winBudget > 0; z++) {
			for (int x = 1; x < m_n - 1 && winBudget > 0; x++) {
				if (CellAt(x, z) != CELL_WALL) continue;
				int openN = 0;
				if (CellAt(x - 1, z) != CELL_WALL && CellAt(x - 1, z) != CELL_WINDOW) openN++;
				if (CellAt(x + 1, z) != CELL_WALL && CellAt(x + 1, z) != CELL_WINDOW) openN++;
				if (CellAt(x, z - 1) != CELL_WALL && CellAt(x, z - 1) != CELL_WINDOW) openN++;
				if (CellAt(x, z + 1) != CELL_WALL && CellAt(x, z + 1) != CELL_WINDOW) openN++;
				if (openN < 1) continue;
				m_rng = m_rng * 1664525u + 1013904223u;
				if ((m_rng % 17u) != 0) continue;
				Cell(x, z) = CELL_WINDOW;
				winBudget--;
			}
		}
	}

	BYTE kinds[5];
	int nk = 0;
	if (mask & ITEM_TEMPO) kinds[nk++] = CELL_TEMPO;
	if (mask & ITEM_PITCH_UP) kinds[nk++] = CELL_PITCH_UP;
	if (mask & ITEM_PITCH_DN) kinds[nk++] = CELL_PITCH_DN;
	if (mask & ITEM_NEXT) kinds[nk++] = CELL_NEXT;
	if (mask & ITEM_EQ) kinds[nk++] = CELL_EQ;
	int itemBudget = nk > 0 ? min(120, 6 + m_n / 8) : 0;
	for (int tries = 0; tries < 800 && itemBudget > 0 && nk > 0; tries++) {
		m_rng = m_rng * 1664525u + 1013904223u;
		const int x = 1 + (int)(m_rng % (DWORD)(m_n - 2));
		m_rng = m_rng * 1664525u + 1013904223u;
		const int z = 1 + (int)(m_rng % (DWORD)(m_n - 2));
		if (CellAt(x, z) != CELL_FLOOR) continue;
		if ((x == sx && z == sz) || (x == gx && z == gz)) continue;
		Cell(x, z) = kinds[m_rng % (DWORD)nk];
		itemBudget--;
		m_itemsLeft++;
	}

	MarkVisited();
	m_runDirty = 1;
	PersistRun();
	UpdateStatus();
	RenderScene();
	m_view.RequestRedraw();
}

void CSoft3DMazeDlg::ApplyItem(int kind)
{
	switch (kind) {
	case CELL_TEMPO: {
		int pct = tempo / 2 + 10;
		if (pct > 200) pct = 200;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->ApplyPracticeTempoPercent(pct);
		else {
			tempo = pct * 2;
			if (og && ::IsWindow(og->GetSafeHwnd()))
				og->m_tempo_sl.SetPos(tempo);
		}
		break;
	}
	case CELL_PITCH_UP:
		S3mSetPitchPos(pitch + 20);
		break;
	case CELL_PITCH_DN:
		S3mSetPitchPos(pitch - 20);
		break;
	case CELL_NEXT:
		MpTaskbarNextTrack();
		break;
	case CELL_EQ:
		m_rng = m_rng * 1664525u + 1013904223u;
		S3mEqBump((int)(m_rng % 15u), 18);
		m_rng = m_rng * 1664525u + 1013904223u;
		S3mEqBump((int)(m_rng % 15u), -10);
		break;
	default:
		break;
	}
	if (!playf && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
	m_runDirty = 1;
}

void CSoft3DMazeDlg::TryPickup()
{
	const int ix = (int)floorf(m_px);
	const int iz = (int)floorf(m_pz);
	if (ix < 0 || iz < 0 || ix >= m_n || iz >= m_n) return;
	const BYTE c = CellAt(ix, iz);
	if (c >= CELL_TEMPO && c <= CELL_EQ) {
		ApplyItem(c);
		Cell(ix, iz) = CELL_FLOOR;
		if (m_itemsLeft > 0) m_itemsLeft--;
		UpdateStatus();
	} else if (c == CELL_GOAL && !m_won && m_clearPhase == CLEAR_IDLE) {
		BeginClearSequence();
		PersistRun();
	}
}

void CSoft3DMazeDlg::ResetClearFx()
{
	m_clearPhase = CLEAR_IDLE;
	m_clearT = 0.f;
	m_clearTextA = 0.f;
	m_clearScreenA = 0.f;
}

void CSoft3DMazeDlg::BeginClearSequence()
{
	m_won = 1;
	m_moving = 0;
	m_turning = 0;
	m_clearPhase = CLEAR_TEXT_IN;
	m_clearT = 0.f;
	m_clearTextA = 0.f;
	m_clearScreenA = 0.f;
	m_runDirty = 1;
	UpdateStatus();
}

void CSoft3DMazeDlg::TickClear(float dt)
{
	if (m_clearPhase == CLEAR_IDLE) return;

	m_clearT += dt;
	const float kTextIn = 0.55f;
	const float kTextHold = 1.40f;
	const float kTextOut = 0.55f;
	const float kFadeOut = 0.85f;
	const float kFadeIn = 0.70f;

	switch (m_clearPhase) {
	case CLEAR_TEXT_IN:
		m_clearTextA = (m_clearT >= kTextIn) ? 1.f : (m_clearT / kTextIn);
		if (m_clearT >= kTextIn) {
			m_clearPhase = CLEAR_TEXT_HOLD;
			m_clearT = 0.f;
			m_clearTextA = 1.f;
		}
		break;
	case CLEAR_TEXT_HOLD:
		m_clearTextA = 1.f;
		if (m_clearT >= kTextHold) {
			m_clearPhase = CLEAR_TEXT_OUT;
			m_clearT = 0.f;
		}
		break;
	case CLEAR_TEXT_OUT:
		m_clearTextA = (m_clearT >= kTextOut) ? 0.f : (1.f - m_clearT / kTextOut);
		if (m_clearT >= kTextOut) {
			m_clearPhase = CLEAR_FADE_OUT;
			m_clearT = 0.f;
			m_clearTextA = 0.f;
		}
		break;
	case CLEAR_FADE_OUT:
		m_clearScreenA = (m_clearT >= kFadeOut) ? 1.f : (m_clearT / kFadeOut);
		if (m_clearT >= kFadeOut) {
			m_clearScreenA = 1.f;
			int n = m_n + 1;
			if (n > S3M_MAX) n = S3M_MAX;
			SetSizeToUi(n);
			GenerateMaze();
			m_clearPhase = CLEAR_FADE_IN;
			m_clearT = 0.f;
			m_clearScreenA = 1.f;
			m_clearTextA = 0.f;
		}
		break;
	case CLEAR_FADE_IN:
		m_clearScreenA = (m_clearT >= kFadeIn) ? 0.f : (1.f - m_clearT / kFadeIn);
		if (m_clearT >= kFadeIn)
			ResetClearFx();
		break;
	default:
		ResetClearFx();
		break;
	}
}

void CSoft3DMazeDlg::PaintClearOverlay(CDC& dc, const CRect& rc)
{
	if (m_clearTextA <= 0.01f || rc.Width() < 8 || rc.Height() < 8)
		return;

	const CString msg = LL14(
		L"クリア！", L"Clear!", L"Réussi !", L"Completato!", L"¡Completado!",
		L"클리어!", L"通关！", L"تم!", L"Пройдено!", L"Geschafft!",
		L"Concluído!", L"Gehaald!", L"Ukończono!", L"Temiz!");

	Gdiplus::Graphics g(dc.GetSafeHdc());
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

	const Gdiplus::REAL fs = (Gdiplus::REAL)max(28, min(96, rc.Height() / 7));
	const WCHAR* face = L"Segoe UI";
	{
		Gdiplus::FontFamily probe(face);
		if (probe.GetLastStatus() != Gdiplus::Ok)
			face = L"Arial";
	}
	Gdiplus::FontFamily family(face);
	Gdiplus::Font font(&family, fs, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	const BYTE a = (BYTE)max(0, min(255, (int)(m_clearTextA * 255.f + 0.5f)));
	Gdiplus::StringFormat fmt;
	fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
	fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	Gdiplus::RectF layout((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
		(Gdiplus::REAL)rc.Width(), (Gdiplus::REAL)rc.Height());

	Gdiplus::SolidBrush shadow(Gdiplus::Color((BYTE)(a * 160 / 255), 0, 0, 0));
	Gdiplus::RectF shadowLayout = layout;
	shadowLayout.X += 3.f;
	shadowLayout.Y += 3.f;
	g.DrawString(msg, -1, &font, shadowLayout, &fmt, &shadow);

	Gdiplus::SolidBrush gold(Gdiplus::Color(a, 255, 230, 110));
	g.DrawString(msg, -1, &font, layout, &fmt, &gold);
}

void CSoft3DMazeDlg::UpdateStatus()
{
	CString s;
	if (m_won || m_clearPhase != CLEAR_IDLE) {
		s = LL14(L"ゴール！曲に合わせて迷路クリア。", L"Goal! Maze cleared to the music.", L"But ! Labyrinthe terminé en musique.", L"Traguardo! Labirinto completato a ritmo.",
			L"¡Meta! Laberinto completado al ritmo.", L"골! 음악에 맞춰 미로 클리어.", L"终点！跟着音乐通关迷宫。", L"الهدف! أُكملت المتاهة مع الموسيقى.",
			L"Цель! Лабиринт пройден под музыку.", L"Ziel! Labyrinth im Takt geschafft.", L"Gol! Labirinto concluído com a música.", L"Doel! Doolhof gehaald op de muziek.",
			L"Cel! Labirynt ukończony przy muzyce.", L"Hedef! Labirent müzikle tamamlandı.");
	} else {
		s.Format(LL14(L"サイズ %d  残りアイテム %d  （右クリックで設定）", L"Size %d  items left %d  (right-click settings)", L"Taille %d  objets %d  (clic droit)", L"Dimensione %d  oggetti %d  (clic destro)",
			L"Tamaño %d  objetos %d  (clic derecho)", L"크기 %d  남은 아이템 %d  (우클릭 설정)", L"尺寸 %d  剩余道具 %d  （右键设置）", L"الحجم %d  العناصر %d  (انقر يمينًا)",
			L"Размер %d  предметов %d  (ПКМ)", L"Größe %d  Items %d  (Rechtsklick)", L"Tamanho %d  itens %d  (clique direito)", L"Grootte %d  items %d  (rechtsklik)",
			L"Rozmiar %d  przedmioty %d  (PPM)", L"Boyut %d  öğe %d  (sağ tık)"),
			m_n, m_itemsLeft);
	}
	if (m_status.GetSafeHwnd())
		m_status.SetWindowText(s);
}

BOOL CSoft3DMazeDlg::TryTurn(int dir)
{
	if (dir == 0 || m_n <= 0 || m_clearPhase != CLEAR_IDLE) return FALSE;
	if (m_turning || m_moving) return FALSE;
	m_yawTarget = S3mSnapYaw(m_yawTarget + (float)dir * (float)(M_PI * 0.5));
	m_turning = 1;
	return TRUE;
}

BOOL CSoft3DMazeDlg::TryStep(int mx, int mz)
{
	if (m_n <= 0 || m_clearPhase != CLEAR_IDLE) return FALSE;
	if (m_moving || m_turning) return FALSE;
	if (mx == 0 && mz == 0) return FALSE;
	if (mz != 0) mx = 0;

	float fx, fz, rx, rz;
	CamBasisYaw(m_yawTarget, fx, fz, rx, rz);
	const float wdx = (float)mx * rx + (float)mz * fx;
	const float wdz = (float)mx * rz + (float)mz * fz;
	int gx = 0, gz = 0;
	if (fabsf(wdx) >= fabsf(wdz))
		gx = (wdx >= 0.f) ? 1 : -1;
	else
		gz = (wdz >= 0.f) ? 1 : -1;

	const int cx = (int)floorf(m_px);
	const int cz = (int)floorf(m_pz);
	const int nx = cx + gx;
	const int nz = cz + gz;
	if (nx < 0 || nz < 0 || nx >= m_n || nz >= m_n)
		return FALSE;
	if (IsBlocked((float)nx + 0.5f, (float)nz + 0.5f))
		return FALSE;

	m_pxTarget = (float)nx + 0.5f;
	m_pzTarget = (float)nz + 0.5f;
	m_moving = 1;
	return TRUE;
}

void CSoft3DMazeDlg::TickMove(float dt)
{
	if (m_n <= 0) return;
	if (m_clearPhase != CLEAR_IDLE) {
		m_bob += dt * 3.f;
		return;
	}

	const bool turnL = (GetAsyncKeyState('Q') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000);
	const bool turnR = (GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000);
	int turnReq = 0;
	if (turnL && !turnR) turnReq = -1;
	else if (turnR && !turnL) turnReq = 1;

	// 押しっぱなし中のアニメ待ちでも、終われば再発火（放さなくてよい）
	if (turnReq == 0) {
		m_turnHeld = 0;
	} else if (!m_turning && !m_moving && !m_turnHeld) {
		if (TryTurn(turnReq))
			m_turnHeld = 1;
	}

	if (m_turning) {
		const float d = S3mAngleDelta(m_yaw, m_yawTarget);
		const float step = 18.0f * dt;
		if (fabsf(d) <= step) {
			m_yaw = m_yawTarget;
			m_turning = 0;
		} else {
			m_yaw += (d > 0.f) ? step : -step;
		}
	}

	int mx = 0, mz = 0;
	if (GetAsyncKeyState('W') & 0x8000 || GetAsyncKeyState(VK_UP) & 0x8000) mz += 1;
	if (GetAsyncKeyState('S') & 0x8000 || GetAsyncKeyState(VK_DOWN) & 0x8000) mz -= 1;
	if (GetAsyncKeyState('A') & 0x8000) mx -= 1;
	if (GetAsyncKeyState('D') & 0x8000) mx += 1;
	if (mz != 0) mx = 0;

	// 移動アニメ中は待つ。完了後は押しっぱなしで次マスへ（edge の食い打ちをやめる）
	if (!m_moving && !m_turning && (mx != 0 || mz != 0))
		TryStep(mx, mz);
	m_moveHeld = (mx != 0 || mz != 0) ? 1 : 0;

	if (m_moving) {
		const float dx = m_pxTarget - m_px;
		const float dz = m_pzTarget - m_pz;
		const float dist = sqrtf(dx * dx + dz * dz);
		const float spd = 26.0f;
		const float step = spd * dt;
		if (dist <= step || dist < 1e-4f) {
			m_px = m_pxTarget;
			m_pz = m_pzTarget;
			m_moving = 0;
			MarkVisited();
			TryPickup();
			m_runDirty = 1;
			// 到着フレームで次マスへ（マス間隔を短く感じさせる）
			if (!m_turning && (mx != 0 || mz != 0))
				TryStep(mx, mz);
		} else {
			m_px += dx / dist * step;
			m_pz += dz / dist * step;
			m_bob += dt * 18.f;
			m_runDirty = 1;
		}
	} else {
		m_bob += dt * (m_won ? 4.f : 2.f);
	}
}

void CSoft3DMazeDlg::DrawMinimap()
{
	if (!savedata.s3m_show_map || !m_view.m_ready || m_n <= 0 || !m_grid) return;
	GdiSoft3D::Context& ctx = m_view.m_ctx;
	const int fw = ctx.fb.w;
	const int fh = ctx.fb.h;
	int cells = S3mClampMapSize(savedata.s3m_minimap);
	const float half = cells * 0.5f;
	const float mapPx = (float)min(fw, fh) * 0.28f;
	const float cellPx = mapPx / (float)cells;
	const float cx = (float)fw - 12.f - mapPx * 0.5f;
	const float cy = 12.f + mapPx * 0.5f;
	const float pad = mapPx * 0.5f + 6.f;

	ctx.HudFillQuad(cx - pad, cy - pad, cx + pad, cy - pad, cx + pad, cy + pad, cx - pad, cy + pad,
		RGB(10, 12, 18), 170);
	ctx.HudLine(cx - pad, cy - pad, cx + pad, cy - pad, RGB(90, 110, 150), 200);
	ctx.HudLine(cx + pad, cy - pad, cx + pad, cy + pad, RGB(90, 110, 150), 200);
	ctx.HudLine(cx + pad, cy + pad, cx - pad, cy + pad, RGB(90, 110, 150), 200);
	ctx.HudLine(cx - pad, cy + pad, cx - pad, cy - pad, RGB(90, 110, 150), 200);

	const int ix0 = (int)floorf(m_px - half - 1.f);
	const int ix1 = (int)ceilf(m_px + half + 1.f);
	const int iz0 = (int)floorf(m_pz - half - 1.f);
	const int iz1 = (int)ceilf(m_pz + half + 1.f);

	auto rotCorner = [&](float wx, float wz, float& ox, float& oy) {
		float mx, my;
		WorldToMap(wx, wz, mx, my);
		ox = cx + mx * cellPx;
		oy = cy + my * cellPx;
	};

	for (int z = iz0; z <= iz1; z++) {
		for (int x = ix0; x <= ix1; x++) {
			if (x < 0 || z < 0 || x >= m_n || z >= m_n) continue;
			float mx, my;
			WorldToMap((float)x + 0.5f, (float)z + 0.5f, mx, my);
			if (fabsf(mx) > half + 0.6f || fabsf(my) > half + 0.6f) continue;

			float q0x, q0y, q1x, q1y, q2x, q2y, q3x, q3y;
			rotCorner((float)x, (float)z, q0x, q0y);
			rotCorner((float)x + 1.f, (float)z, q1x, q1y);
			rotCorner((float)x + 1.f, (float)z + 1.f, q2x, q2y);
			rotCorner((float)x, (float)z + 1.f, q3x, q3y);

			const BYTE c = CellAt(x, z);
			COLORREF col = RGB(35, 40, 55);
			BYTE a = 160;
			if (c == CELL_WALL) { col = RGB(140, 95, 65); a = 220; }
			else if (c == CELL_WINDOW) { col = RGB(70, 140, 200); a = 200; }
			else if (c == CELL_GOAL) { col = RGB(255, 210, 70); a = 230; }
			else if (c == CELL_START) { col = RGB(90, 160, 110); a = 200; }
			else if (c >= CELL_TEMPO && c <= CELL_EQ) {
				if (c == CELL_TEMPO) col = RGB(80, 220, 140);
				else if (c == CELL_PITCH_UP) col = RGB(255, 180, 90);
				else if (c == CELL_PITCH_DN) col = RGB(120, 160, 255);
				else if (c == CELL_NEXT) col = RGB(255, 100, 140);
				else col = RGB(200, 140, 255);
				a = 230;
			} else {
				col = RGB(40, 48, 68);
				a = 140;
			}
			if (VisitAt(x, z) && c != CELL_WALL && c != CELL_WINDOW) {
				col = RGB(min(255, GetRValue(col) + 55), min(255, GetGValue(col) + 70), min(255, GetBValue(col) + 40));
				a = (BYTE)min(255, (int)a + 40);
			}
			ctx.HudFillQuad(q0x, q0y, q1x, q1y, q2x, q2y, q3x, q3y, col, a);
		}
	}

	const float ps = cellPx * 0.28f;
	// ▲は常に上＝現在の視線方向（マップ自体が WorldToMap＝heading-up で回る）
	ctx.HudFillTri(cx, cy - ps, cx - ps * 0.55f, cy + ps * 0.45f, cx + ps * 0.55f, cy + ps * 0.45f,
		RGB(255, 240, 120), 240);

	// コンパス（絶対方位）。WorldToMap と同じ回転で北・東西を描く
	{
		const float ccx = cx + pad - 18.f;
		const float ccy = cy - pad + 18.f;
		float nmx, nmy, emx, emy;
		WorldToMap(m_px, m_pz - 1.f, nmx, nmy); // 北 = -Z
		WorldToMap(m_px + 1.f, m_pz, emx, emy); // 東 = +X
		auto norm2 = [](float& x, float& y) {
			const float L = sqrtf(x * x + y * y);
			if (L > 1e-6f) { x /= L; y /= L; }
		};
		norm2(nmx, nmy);
		norm2(emx, emy);
		ctx.HudFillTri(
			ccx + nmx * 11.f, ccy + nmy * 11.f,
			ccx - nmx * 2.f - emx * 5.f, ccy - nmy * 2.f - emy * 5.f,
			ccx - nmx * 2.f + emx * 5.f, ccy - nmy * 2.f + emy * 5.f,
			RGB(255, 90, 90), 240);
		ctx.HudLine(ccx - nmx * 2.f, ccy - nmy * 2.f, ccx + nmx * (-10.f), ccy + nmy * (-10.f), RGB(200, 210, 230), 220);
		ctx.HudLine(ccx - emx * 8.f, ccy - emy * 8.f, ccx + emx * 8.f, ccy + emy * 8.f, RGB(180, 190, 210), 200);
	}
}

void CSoft3DMazeDlg::RenderScene()
{
	if (!m_view.m_ready || m_n <= 0 || !m_grid) return;
	GdiSoft3D::Context& ctx = m_view.m_ctx;
	const int w = ctx.fb.w;
	const int h = ctx.fb.h;
	if (w < 8 || h < 8) return;

	ctx.BeginFrame(RGB(14, 16, 24));
	ctx.fillMode = GdiSoft3D::FillSolid;
	ctx.depthTest = true;
	ctx.depthWrite = true;
	ctx.alphaBlend = false;
	ctx.cullBack = false;
	ctx.SetFog(GdiSoft3D::FogLinear, RGB(14, 16, 24), 8.f, 22.f, 0.45f);
	ctx.SetDof(false, 1.0f, 5.5f, 0);
	ctx.postVignette = false;

	GdiSoft3D::View& v = ctx.view;
	// ピッチ無し・水平視線。見下ろしだと注視点が前方にずれ、旋回が楕円軌道に見える
	v.cosYaw = cosf(m_yaw);
	v.sinYaw = sinf(m_yaw);
	v.cosPitch = 1.f;
	v.sinPitch = 0.f;
	v.camD = 1.45f;
	v.scale = (float)h * 0.58f;
	v.originX = (float)w * 0.5f;
	v.originY = (float)h * 0.50f; // 地平を画面中央＝マス中央の eye と一致
	ctx.cam.yawDeg = m_yaw * (180.f / (float)M_PI);
	ctx.cam.pitchDeg = 0.f;
	ctx.cam.zoom = 1.f;

	const float eyeY = 0.50f + 0.015f * sinf(m_bob);
	const float wallH = 1.15f;
	const float drawDist = 7.0f;
	const float radius = drawDist + 1.0f;
	const float latCull = 5.0f;
	const float yBase = -eyeY;
	const float yTop = wallH - eyeY;
	GdiSoft3D::Texture& brickTex = S3mBrickTexture();

	float eyeX, eyeZ;
	GetRenderEye(eyeX, eyeZ);

	const int x0 = max(0, (int)floorf(m_px - radius));
	const int x1 = min(m_n - 1, (int)ceilf(m_px + radius));
	const int z0 = max(0, (int)floorf(m_pz - radius));
	const int z1 = min(m_n - 1, (int)ceilf(m_pz + radius));

	auto isOpenAt = [&](int x, int z) -> bool {
		if (x < 0 || z < 0 || x >= m_n || z >= m_n) return false;
		const BYTE c = CellAt(x, z);
		return c != CELL_WALL && c != CELL_WINDOW;
	};
	// 3D は描画 eye 基準（論理中央より前方 0.8）。ミニマップは WorldToMap＝論理中央
	auto relX = [&](float wx) { return wx - eyeX; };
	auto relZ = [&](float wz) { return wz - eyeZ; };
	auto cellVisible = [&](int x, int z) -> bool {
		float fx, fz, rx, rz;
		CamBasisYaw(m_yaw, fx, fz, rx, rz);
		const float dx = (float)x + 0.5f - eyeX;
		const float dz = (float)z + 0.5f - eyeZ;
		const float lx = dx * rx + dz * rz;
		const float lz = dx * fx + dz * fz;
		if (fabsf(lx) > latCull) return false;
		if (lz > drawDist) return false;
		if (lz < -1.6f && fabsf(lx) < 0.85f) return false;
		return true;
	};

	auto drawBrickFaceX = [&](float x, float za, float zb, float y0, float y1) {
		if (zb < za) { float t = za; za = zb; zb = t; }
		ctx.SetTexture(&brickTex);
		const float rx = relX(x), rz0 = relZ(za), rz1 = relZ(zb);
		ctx.DrawTexturedQuadUV(
			rx, y0, rz0, za, (y0 - yBase),
			rx, y0, rz1, zb, (y0 - yBase),
			rx, y1, rz1, zb, (y1 - yBase),
			rx, y1, rz0, za, (y1 - yBase));
		ctx.SetTexture(nullptr);
	};
	auto drawBrickFaceZ = [&](float z, float xa, float xb, float y0, float y1) {
		if (xb < xa) { float t = xa; xa = xb; xb = t; }
		ctx.SetTexture(&brickTex);
		const float rx0 = relX(xa), rx1 = relX(xb), rz = relZ(z);
		ctx.DrawTexturedQuadUV(
			rx0, y0, rz, xa, (y0 - yBase),
			rx1, y0, rz, xb, (y0 - yBase),
			rx1, y1, rz, xb, (y1 - yBase),
			rx0, y1, rz, xa, (y1 - yBase));
		ctx.SetTexture(nullptr);
	};
	auto drawBrickTop = [&](float xa, float xb, float za, float zb, float y) {
		if (xb < xa) { float t = xa; xa = xb; xb = t; }
		if (zb < za) { float t = za; za = zb; zb = t; }
		ctx.SetTexture(&brickTex);
		ctx.DrawTexturedQuadUV(
			relX(xa), y, relZ(za), xa, za, relX(xb), y, relZ(za), xb, za,
			relX(xb), y, relZ(zb), xb, zb, relX(xa), y, relZ(zb), xa, zb);
		ctx.SetTexture(nullptr);
	};
	auto drawSolidFaceX = [&](float x, float za, float zb, float y0, float y1, COLORREF col, BYTE alpha = 255) {
		if (zb < za) { float t = za; za = zb; zb = t; }
		const float rx = relX(x);
		ctx.DrawQuad(rx, y0, relZ(za), rx, y0, relZ(zb), rx, y1, relZ(zb), rx, y1, relZ(za), col, alpha);
	};
	auto drawSolidFaceZ = [&](float z, float xa, float xb, float y0, float y1, COLORREF col, BYTE alpha = 255) {
		if (xb < xa) { float t = xa; xa = xb; xb = t; }
		const float rz = relZ(z);
		ctx.DrawQuad(relX(xa), y0, rz, relX(xb), y0, rz, relX(xb), y1, rz, relX(xa), y1, rz, col, alpha);
	};
	auto camDepthOf = [&](int x, int z) -> float {
		float fx, fz, rx, rz;
		CamBasisYaw(m_yaw, fx, fz, rx, rz);
		(void)rx; (void)rz;
		return ((float)x + 0.5f - eyeX) * fx + ((float)z + 0.5f - eyeZ) * fz;
	};

	// ---- 1) 不透明: 床 ----
	for (int z = z0; z <= z1; z++) {
		for (int x = x0; x <= x1; x++) {
			if (!isOpenAt(x, z) || !cellVisible(x, z)) continue;
			COLORREF floorCol = VisitAt(x, z) ? RGB(92, 72, 52) : RGB(72, 55, 40);
			ctx.DrawQuad(
				relX((float)x), yBase, relZ((float)z),
				relX((float)x + 1.f), yBase, relZ((float)z),
				relX((float)x + 1.f), yBase, relZ((float)z + 1.f),
				relX((float)x), yBase, relZ((float)z + 1.f),
				floorCol);
		}
	}

	// ---- 2) 不透明: レンガ壁のみ（窓は半透明パスへ）----
	for (int z = z0; z <= z1; z++) {
		for (int x = x0; x <= x1; x++) {
			if (CellAt(x, z) != CELL_WALL || !cellVisible(x, z)) continue;
			const float xa = (float)x, xb = (float)x + 1.f;
			const float za = (float)z, zb = (float)z + 1.f;
			auto emitX = [&](float xf, int nx, int nz) {
				if (!isOpenAt(nx, nz)) return;
				drawBrickFaceX(xf, za, zb, yBase, yTop);
			};
			auto emitZ = [&](float zf, int nx, int nz) {
				if (!isOpenAt(nx, nz)) return;
				drawBrickFaceZ(zf, xa, xb, yBase, yTop);
			};
			emitX(xa, x - 1, z);
			emitX(xb, x + 1, z);
			emitZ(za, x, z - 1);
			emitZ(zb, x, z + 1);
			drawBrickTop(xa, xb, za, zb, yTop);
		}
	}

	// ---- 3) 半透明: 窓・ゴール・アイテム（奥→手前）----
	struct Xluc { float depth; int x, z; BYTE c; };
	Xluc xl[512];
	int nXl = 0;
	for (int z = z0; z <= z1 && nXl < 512; z++) {
		for (int x = x0; x <= x1 && nXl < 512; x++) {
			if (!cellVisible(x, z)) continue;
			const BYTE c = CellAt(x, z);
			if (c != CELL_WINDOW && c != CELL_GOAL && !(c >= CELL_TEMPO && c <= CELL_EQ))
				continue;
			const float d = camDepthOf(x, z);
			if (c != CELL_WINDOW && d < 0.15f) continue;
			xl[nXl].depth = d;
			xl[nXl].x = x;
			xl[nXl].z = z;
			xl[nXl].c = c;
			nXl++;
		}
	}
	for (int i = 1; i < nXl; i++) {
		Xluc key = xl[i];
		int j = i - 1;
		while (j >= 0 && xl[j].depth < key.depth) {
			xl[j + 1] = xl[j];
			j--;
		}
		xl[j + 1] = key;
	}

	ctx.alphaBlend = true;
	ctx.depthWrite = false; // 半透明は Z を汚さない（奥の半透明が消えない）
	for (int i = 0; i < nXl; i++) {
		const int x = xl[i].x, z = xl[i].z;
		const BYTE c = xl[i].c;
		const float xa = (float)x, xb = (float)x + 1.f;
		const float za = (float)z, zb = (float)z + 1.f;

		if (c == CELL_WINDOW) {
			const BYTE glassA = 120;
			const BYTE paneA = 160;
			// 天井と同系色の床（窓マスは通路ではないので不透明パスで床が出ない）
			ctx.DrawQuad(
				relX(xa), yBase, relZ(za), relX(xb), yBase, relZ(za),
				relX(xb), yBase, relZ(zb), relX(xa), yBase, relZ(zb),
				GdiSoft3D::Shade(RGB(120, 190, 255), 0.55f), glassA);
			auto emitX = [&](float xf, int nx, int nz) {
				if (!isOpenAt(nx, nz)) return;
				drawSolidFaceX(xf, za, zb, yBase, yTop, RGB(120, 190, 255), glassA);
			};
			auto emitZ = [&](float zf, int nx, int nz) {
				if (!isOpenAt(nx, nz)) return;
				drawSolidFaceZ(zf, xa, xb, yBase, yTop, RGB(120, 190, 255), glassA);
			};
			emitX(xa, x - 1, z);
			emitX(xb, x + 1, z);
			emitZ(za, x, z - 1);
			emitZ(zb, x, z + 1);
			ctx.DrawQuad(
				relX(xa), yTop, relZ(za), relX(xb), yTop, relZ(za),
				relX(xb), yTop, relZ(zb), relX(xa), yTop, relZ(zb),
				GdiSoft3D::Shade(RGB(120, 190, 255), 1.1f), glassA);
			const float y0 = 0.35f - eyeY, y1 = (wallH - 0.35f) - eyeY;
			drawSolidFaceX(xa + 0.12f, za + 0.12f, zb - 0.12f, y0, y1, RGB(40, 90, 140), paneA);
			drawSolidFaceX(xb - 0.12f, za + 0.12f, zb - 0.12f, y0, y1, RGB(40, 90, 140), paneA);
			drawSolidFaceZ(za + 0.12f, xa + 0.12f, xb - 0.12f, y0, y1, RGB(40, 90, 140), paneA);
			drawSolidFaceZ(zb - 0.12f, xa + 0.12f, xb - 0.12f, y0, y1, RGB(40, 90, 140), paneA);
		} else if (c == CELL_GOAL) {
			const BYTE a = 170;
			const float gxa = (float)x + 0.28f, gxb = (float)x + 0.72f;
			const float gza = (float)z + 0.28f, gzb = (float)z + 0.72f;
			const float yt = 0.85f - eyeY;
			ctx.DrawQuad(relX(gxa), yt, relZ(gza), relX(gxb), yt, relZ(gza), relX(gxb), yt, relZ(gzb), relX(gxa), yt, relZ(gzb), RGB(255, 210, 80), a);
			drawSolidFaceZ(gza, gxa, gxb, yBase, yt, GdiSoft3D::Shade(RGB(255, 210, 80), 0.75f), a);
			drawSolidFaceZ(gzb, gxa, gxb, yBase, yt, GdiSoft3D::Shade(RGB(255, 210, 80), 0.70f), a);
			drawSolidFaceX(gxa, gza, gzb, yBase, yt, GdiSoft3D::Shade(RGB(255, 210, 80), 0.60f), a);
			drawSolidFaceX(gxb, gza, gzb, yBase, yt, GdiSoft3D::Shade(RGB(255, 210, 80), 0.55f), a);
		} else {
			COLORREF col = RGB(255, 140, 200);
			if (c == CELL_TEMPO) col = RGB(80, 220, 140);
			else if (c == CELL_PITCH_UP) col = RGB(255, 180, 90);
			else if (c == CELL_PITCH_DN) col = RGB(120, 160, 255);
			else if (c == CELL_NEXT) col = RGB(255, 100, 140);
			else if (c == CELL_EQ) col = RGB(200, 140, 255);
			const float bob = 0.08f * sinf(m_bob * 1.7f + (float)(x + z));
			ctx.DrawSphere(relX((float)x + 0.5f), 0.35f - eyeY + bob, relZ((float)z + 0.5f), 0.18f, col, 6, 5, 190);
		}
	}
	ctx.alphaBlend = false;
	ctx.depthWrite = true;

	ctx.EndFrame();
	DrawMinimap();

	if (m_clearScreenA > 0.01f) {
		const BYTE a = (BYTE)max(0, min(255, (int)(m_clearScreenA * 255.f + 0.5f)));
		ctx.HudFillQuad(0.f, 0.f, (float)w, 0.f, (float)w, (float)h, 0.f, (float)h, RGB(0, 0, 0), a);
	}
}

void CSoft3DMazeDlg::ShowHelpSheet()
{
	if (g_s3mHelp && g_s3mHelp->GetSafeHwnd()) {
		g_s3mHelp->SetForegroundWindow();
		return;
	}
	g_s3mHelp = new CS3mHelpDlg(this);
	if (!g_s3mHelp->Create(IDD_S3M_HELP, this)) {
		delete g_s3mHelp;
		g_s3mHelp = NULL;
		return;
	}
	CCC_PresentOwnedHelp(g_s3mHelp, this);
}


void CSoft3DMazeDlg::ShowContextMenu(CPoint screenPt)
{
	const int curSz = ReadSizeFromUi();
	CCustomPopupMenu menu;
	menu.AddCommand(1, LL14(L"リスタート（同じ設定で再生成）", L"Restart (regenerate with same settings)", L"Redémarrer (mêmes réglages)", L"Riavvia (stesse impostazioni)",
		L"Reiniciar (mismos ajustes)", L"재시작(같은 설정으로 재생성)", L"重启（同设置重新生成）", L"إعادة التشغيل (نفس الإعدادات)",
		L"Перезапуск (те же настройки)", L"Neustart (gleiche Einstellungen)", L"Reiniciar (mesmas definições)", L"Herstarten (zelfde instellingen)",
		L"Restart (te same ustawienia)", L"Yeniden başlat (aynı ayarlar)"));
	menu.AddSeparator();
	menu.AddCheck(10, LL14(L"大きさ: 10", L"Size: 10", L"Taille: 10", L"Dimensione: 10", L"Tamaño: 10", L"크기: 10", L"大小：10", L"الحجم: 10", L"Размер: 10", L"Größe: 10", L"Tamanho: 10", L"Grootte: 10", L"Rozmiar: 10", L"Boyut: 10"), curSz == 10);
	menu.AddCheck(11, LL14(L"大きさ: 20", L"Size: 20", L"Taille: 20", L"Dimensione: 20", L"Tamaño: 20", L"크기: 20", L"大小：20", L"الحجم: 20", L"Размер: 20", L"Größe: 20", L"Tamanho: 20", L"Grootte: 20", L"Rozmiar: 20", L"Boyut: 20"), curSz == 20);
	menu.AddCheck(12, LL14(L"大きさ: 30", L"Size: 30", L"Taille: 30", L"Dimensione: 30", L"Tamaño: 30", L"크기: 30", L"大小：30", L"الحجم: 30", L"Размер: 30", L"Größe: 30", L"Tamanho: 30", L"Grootte: 30", L"Rozmiar: 30", L"Boyut: 30"), curSz == 30);
	menu.AddCheck(13, LL14(L"大きさ: 50", L"Size: 50", L"Taille: 50", L"Dimensione: 50", L"Tamaño: 50", L"크기: 50", L"大小：50", L"الحجم: 50", L"Размер: 50", L"Größe: 50", L"Tamanho: 50", L"Grootte: 50", L"Rozmiar: 50", L"Boyut: 50"), curSz == 50);
	menu.AddCheck(14, LL14(L"大きさ: 80", L"Size: 80", L"Taille: 80", L"Dimensione: 80", L"Tamaño: 80", L"크기: 80", L"大小：80", L"الحجم: 80", L"Размер: 80", L"Größe: 80", L"Tamanho: 80", L"Grootte: 80", L"Rozmiar: 80", L"Boyut: 80"), curSz == 80);
	menu.AddCheck(15, LL14(L"大きさ: 100", L"Size: 100", L"Taille: 100", L"Dimensione: 100", L"Tamaño: 100", L"크기: 100", L"大小：100", L"الحجم: 100", L"Размер: 100", L"Größe: 100", L"Tamanho: 100", L"Grootte: 100", L"Rozmiar: 100", L"Boyut: 100"), curSz == 100);
	menu.AddCheck(16, LL14(L"大きさ: 150", L"Size: 150", L"Taille: 150", L"Dimensione: 150", L"Tamaño: 150", L"크기: 150", L"大小：150", L"الحجم: 150", L"Размер: 150", L"Größe: 150", L"Tamanho: 150", L"Grootte: 150", L"Rozmiar: 150", L"Boyut: 150"), curSz == 150);
	menu.AddCheck(17, LL14(L"大きさ: 200", L"Size: 200", L"Taille: 200", L"Dimensione: 200", L"Tamaño: 200", L"크기: 200", L"大小：200", L"الحجم: 200", L"Размер: 200", L"Größe: 200", L"Tamanho: 200", L"Grootte: 200", L"Rozmiar: 200", L"Boyut: 200"), curSz == 200);
	menu.AddCheck(18, LL14(L"大きさ: 300", L"Size: 300", L"Taille: 300", L"Dimensione: 300", L"Tamaño: 300", L"크기: 300", L"大小：300", L"الحجم: 300", L"Размер: 300", L"Größe: 300", L"Tamanho: 300", L"Grootte: 300", L"Rozmiar: 300", L"Boyut: 300"), curSz == 300);
	menu.AddCheck(19, LL14(L"大きさ: 400", L"Size: 400", L"Taille: 400", L"Dimensione: 400", L"Tamaño: 400", L"크기: 400", L"大小：400", L"الحجم: 400", L"Размер: 400", L"Größe: 400", L"Tamanho: 400", L"Grootte: 400", L"Rozmiar: 400", L"Boyut: 400"), curSz == 400);
	menu.AddSeparator();
	menu.AddCheck(20, LL14(L"ミニマップ表示", L"Show minimap", L"Afficher la minimap", L"Mostra minimap", L"Mostrar minimapa",
		L"미니맵 표시", L"显示小地图", L"إظهار الخريطة المصغّرة", L"Показать мини-карту", L"Minimap anzeigen", L"Mostrar minimapa", L"Minimapa tonen", L"Pokaż minimapę", L"Minimapi göster"),
		savedata.s3m_show_map != 0);
	const int ms = S3mClampMapSize(savedata.s3m_minimap);
	menu.AddCheck(21, LL14(L"ミニマップ 8×8", L"Minimap 8×8", L"Minimap 8×8", L"Minimap 8×8", L"Minimapa 8×8", L"미니맵 8×8", L"小地图 8×8", L"خريطة 8×8", L"Мини-карта 8×8", L"Minimap 8×8", L"Minimapa 8×8", L"Minimapa 8×8", L"Minimapa 8×8", L"Minimapi 8×8"), ms == 8);
	menu.AddCheck(22, LL14(L"ミニマップ 10×10", L"Minimap 10×10", L"Minimap 10×10", L"Minimap 10×10", L"Minimapa 10×10", L"미니맵 10×10", L"小地图 10×10", L"خريطة 10×10", L"Мини-карта 10×10", L"Minimap 10×10", L"Minimapa 10×10", L"Minimapa 10×10", L"Minimapa 10×10", L"Minimapi 10×10"), ms == 10);
	menu.AddCheck(23, LL14(L"ミニマップ 12×12", L"Minimap 12×12", L"Minimap 12×12", L"Minimap 12×12", L"Minimapa 12×12", L"미니맵 12×12", L"小地图 12×12", L"خريطة 12×12", L"Мини-карта 12×12", L"Minimap 12×12", L"Minimapa 12×12", L"Minimapa 12×12", L"Minimapa 12×12", L"Minimapi 12×12"), ms == 12);
	menu.AddCheck(24, LL14(L"ミニマップ 14×14", L"Minimap 14×14", L"Minimap 14×14", L"Minimap 14×14", L"Minimapa 14×14", L"미니맵 14×14", L"小地图 14×14", L"خريطة 14×14", L"Мини-карта 14×14", L"Minimap 14×14", L"Minimapa 14×14", L"Minimapa 14×14", L"Minimapa 14×14", L"Minimapi 14×14"), ms == 14);
	menu.AddCheck(25, LL14(L"ミニマップ 16×16", L"Minimap 16×16", L"Minimap 16×16", L"Minimap 16×16", L"Minimapa 16×16", L"미니맵 16×16", L"小地图 16×16", L"خريطة 16×16", L"Мини-карта 16×16", L"Minimap 16×16", L"Minimapa 16×16", L"Minimapa 16×16", L"Minimapa 16×16", L"Minimapi 16×16"), ms == 16);
	menu.AddSeparator();
	const int mask = S3mItemMask();
	menu.AddCheck(30, LL14(L"アイテム: テンポ↑", L"Item: tempo↑", L"Objet: tempo↑", L"Oggetto: tempo↑", L"Objeto: tempo↑", L"아이템: 템포↑", L"道具：速度↑", L"عنصر: إيقاع↑", L"Предмет: темп↑", L"Item: Tempo↑", L"Item: tempo↑", L"Item: tempo↑", L"Przedmiot: tempo↑", L"Öğe: tempo↑"), (mask & ITEM_TEMPO) != 0);
	menu.AddCheck(31, LL14(L"アイテム: ピッチ↑", L"Item: pitch↑", L"Objet: hauteur↑", L"Oggetto: pitch↑", L"Objeto: tono↑", L"아이템: 피치↑", L"道具：音高↑", L"عنصر: طبقة↑", L"Предмет: высота↑", L"Item: Tonhöhe↑", L"Item: tom↑", L"Item: toon↑", L"Przedmiot: wysokość↑", L"Öğe: perde↑"), (mask & ITEM_PITCH_UP) != 0);
	menu.AddCheck(32, LL14(L"アイテム: ピッチ↓", L"Item: pitch↓", L"Objet: hauteur↓", L"Oggetto: pitch↓", L"Objeto: tono↓", L"아이템: 피치↓", L"道具：音高↓", L"عنصر: طبقة↓", L"Предмет: высота↓", L"Item: Tonhöhe↓", L"Item: tom↓", L"Item: toon↓", L"Przedmiot: wysokość↓", L"Öğe: perde↓"), (mask & ITEM_PITCH_DN) != 0);
	menu.AddCheck(33, LL14(L"アイテム: 次の曲", L"Item: next track", L"Objet: piste suivante", L"Oggetto: brano successivo", L"Objeto: pista siguiente", L"아이템: 다음 곡", L"道具：下一曲", L"عنصر: المسار التالي", L"Предмет: следующий трек", L"Item: nächster Titel", L"Item: próxima faixa", L"Item: volgend nummer", L"Przedmiot: następny utwór", L"Öğe: sonraki parça"), (mask & ITEM_NEXT) != 0);
	menu.AddCheck(34, LL14(L"アイテム: EQ", L"Item: EQ", L"Objet: EQ", L"Oggetto: EQ", L"Objeto: EQ", L"아이템: EQ", L"道具：EQ", L"عنصر: EQ", L"Предмет: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Przedmiot: EQ", L"Öğe: EQ"), (mask & ITEM_EQ) != 0);
	menu.AddCheck(35, LL14(L"窓を配置", L"Place windows", L"Placer des fenêtres", L"Posiziona finestre", L"Colocar ventanas",
		L"창 배치", L"放置窗户", L"وضع نوافذ", L"Размещать окна", L"Fenster platzieren", L"Colocar janelas", L"Ramen plaatsen", L"Umieść okna", L"Pencere yerleştir"), (mask & ITEM_WINDOW) != 0);
	menu.AddSeparator();
	menu.AddCommand(40, LL14(L"テンポ／ピッチを開いた時に戻す", L"Reset tempo/pitch to opening values", L"Remettre tempo/hauteur d'ouverture", L"Ripristina tempo/pitch iniziali", L"Restablecer tempo/tono iniciales",
		L"템포/피치를 열 때 값으로", L"将速度/音高恢复为打开时", L"إعادة الإيقاع/الطبقة لقيم الفتح", L"Вернуть темп/высоту к открытию", L"Tempo/Tonhöhe auf Öffnungswerte", L"Restaurar tempo/tom de abertura", L"Tempo/toonhoogte naar openingswaarden", L"Przywróć tempo/wysokość z otwarcia", L"Tempo/perdeyi açılış değerine al"));

	UINT cmd = menu.Track(screenPt, this);
	if (cmd == 1) {
		GenerateMaze();
		return;
	}
	if (cmd >= 10 && cmd <= 19) {
		SetSizeToUi(kPresets[cmd - 10]);
		PersistUi();
		GenerateMaze();
		return;
	}
	if (cmd == 20) {
		savedata.s3m_show_map = savedata.s3m_show_map ? 0 : 1;
		PersistUi();
		return;
	}
	if (cmd >= 21 && cmd <= 25) {
		savedata.s3m_minimap = kMapSizes[cmd - 21];
		PersistUi();
		return;
	}
	if (cmd >= 30 && cmd <= 35) {
		const int bit = 1 << (cmd - 30);
		int m = S3mItemMask();
		if (m & bit) m &= ~bit; else m |= bit;
		if (m == 0) m = ITEM_TEMPO;
		savedata.s3m_item_mask = m;
		PersistUi();
		return;
	}
	if (cmd == 40)
		RestoreAudioBaseline();
}

BOOL CSoft3DMazeDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_size.SetAeroMode(FALSE);
	m_gen.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);
	m_view.SetAeroMode(FALSE);

	if (savedata.s3m_minimap < 8 || savedata.s3m_minimap > 16)
		savedata.s3m_minimap = 10;
	savedata.s3m_minimap = S3mClampMapSize(savedata.s3m_minimap);
	if (savedata.s3m_item_mask <= 0)
		savedata.s3m_item_mask = ITEM_ALL;
	if (savedata.s3m_show_map != 0)
		savedata.s3m_show_map = 1;
	else if (!savedata.s3m_have_run)
		savedata.s3m_show_map = 1;

	SetWindowText(LL14(L"Soft3D 迷路", L"Soft3D maze", L"Labyrinthe Soft3D", L"Labirinto Soft3D", L"Laberinto Soft3D",
		L"Soft3D 미로", L"Soft3D 迷宫", L"متاهة Soft3D", L"Лабиринт Soft3D", L"Soft3D-Labyrinth",
		L"Labirinto Soft3D", L"Soft3D-doolhof", L"Labirynt Soft3D", L"Soft3D labirent"));
	m_sizeL.SetWindowText(LL14(L"大きさ", L"Size", L"Taille", L"Dimensione", L"Tamaño",
		L"크기", L"大小", L"الحجم", L"Размер", L"Größe", L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"));
	m_gen.SetWindowText(LL14(L"生成", L"Generate", L"Générer", L"Genera", L"Generar",
		L"생성", L"生成", L"توليد", L"Создать", L"Erzeugen", L"Gerar", L"Genereren", L"Generuj", L"Oluştur"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(L"WASD移動 / QE旋回 · 左・右クリックで旋回 · 上半分クリックで前進 · 下半分で後退 · 右クリック設定", L"WASD move / QE turn · side-click turn · top-click forward · bottom-click back · right-click settings", L"WASD / QE · clic côté = tourner · haut = avancer · bas = reculer · clic droit", L"WASD / QE · clic lato = gira · alto = avanti · basso = indietro · clic destro",
		L"WASD / QE · clic lateral = girar · arriba = avanzar · abajo = retroceder · clic derecho", L"WASD 이동 / QE 선회 · 좌우 클릭 선회 · 상단 전진 · 하단 후진 · 우클릭 설정", L"WASD 移动 / QE 转向 · 左右点击转向 · 上半前进 · 下半后退 · 右键设置", L"WASD / QE · يمين/يسار للالتفاف · أعلى للتقدم · أسفل للتراجع · يمين للإعدادات",
		L"WASD / QE · края = поворот · верх = вперёд · низ = назад · ПКМ", L"WASD / QE · Seitenklick drehen · oben vor · unten zurück · Rechtsklick", L"WASD / QE · lateral = girar · cima = avançar · baixo = recuar · direito", L"WASD / QE · zijkant = draaien · boven = vooruit · onder = achteruit · rechtsklik",
		L"WASD / QE · bok = obrót · góra = przód · dół = tył · PPM", L"WASD / QE · yan tık dönüş · üst ileri · alt geri · sağ tık ayar"));

	for (int i = 0; i < kPresetCnt; i++) {
		CString s;
		s.Format(_T("%d"), kPresets[i]);
		m_size.AddString(s);
	}
	SetSizeToUi(S3mNormalizeSavedSize(savedata.s3m_size));

	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tooltip.Activate(TRUE);
		m_tooltip.AddTool(&m_gen, LL14(L"新しい迷路を生成します", L"Generate a new maze", L"Générer un nouveau labyrinthe", L"Genera un nuovo labirinto", L"Generar un nuevo laberinto",
			L"새 미로 생성", L"生成新迷宫", L"إنشاء متاهة جديدة", L"Создать новый лабиринт", L"Neues Labyrinth erzeugen", L"Gerar um novo labirinto", L"Nieuw doolhof genereren", L"Wygeneruj nowy labirynt", L"Yeni labirent oluştur"));
		m_tooltip.AddTool(&m_view, LL14(L"右クリックで設定メニュー", L"Right-click for settings", L"Clic droit pour les réglages", L"Clic destro per impostazioni", L"Clic derecho para ajustes",
			L"우클릭으로 설정 메뉴", L"右键打开设置菜单", L"انقر يمينًا للإعدادات", L"ПКМ — меню настроек", L"Rechtsklick: Einstellungen", L"Clique direito para definições", L"Rechtsklik voor instellingen", L"PPM: menu ustawień", L"Ayarlar için sağ tık"));
	}

	CaptureAudioBaseline();
	LayoutAll();
	if (!LoadRun())
		GenerateMaze();
	else {
		UpdateStatus();
		if (m_won)
			BeginClearSequence();
		RenderScene();
		m_view.RequestRedraw();
	}
	m_lastTick = GetTickCount();
	m_lastAutosave = m_lastTick;
	SetTimer(S3M_TIMER, 16, NULL);
	return TRUE;
}

void CSoft3DMazeDlg::OnGen() { GenerateMaze(); }
void CSoft3DMazeDlg::OnSizeChanged() { PersistUi(); }
void CSoft3DMazeDlg::OnSizeEditChange()
{
	if (m_size.GetSafeHwnd())
		m_size.Invalidate(FALSE);
}
void CSoft3DMazeDlg::OnHelp() { ShowHelpSheet(); }
void CSoft3DMazeDlg::OnCloseBtn() { DestroyWindow(); }

void CSoft3DMazeDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CPoint sp = point;
	if (sp.x == -1 && sp.y == -1) {
		CRect rc; GetWindowRect(&rc);
		sp.x = rc.left + 40; sp.y = rc.top + 40;
	}
	ShowContextMenu(sp);
}

void CSoft3DMazeDlg::OnTimer(UINT_PTR id)
{
	if (id == S3M_TIMER) {
		const DWORD now = GetTickCount();
		float dt = (float)(now - m_lastTick) * 0.001f;
		m_lastTick = now;
		if (dt < 0.f) dt = 0.f;
		if (dt > 0.05f) dt = 0.05f;
		TickClear(dt);
		TickMove(dt);
		RenderScene();
		m_view.RequestRedraw();
		if (m_runDirty && (now - m_lastAutosave) > 2000)
			PersistRun();
	}
	CCustomBlurDialogBase::OnTimer(id);
}

void CSoft3DMazeDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED)
		LayoutAll();
}

void CSoft3DMazeDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow)
		LayoutAll();
}

void CSoft3DMazeDlg::OnDestroy()
{
	KillTimer(S3M_TIMER);
	PersistUi();
	PersistRun();
	RestoreAudioBaseline();
	FreeGrid();
	CCustomBlurDialogBase::OnDestroy();
	// 閉じたら矢印ホットキー再登録を促す
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void OpenSoft3DMazeModeless(CWnd* p)
{
	if (g_s3m && g_s3m->GetSafeHwnd()) {
		g_s3m->SetForegroundWindow();
		return;
	}
	g_s3m = new CSoft3DMazeDlg(p);
	if (!g_s3m->Create(IDD_SOFT3DMAZE, p)) {
		delete g_s3m;
		g_s3m = NULL;
		return;
	}
	g_s3m->ShowWindow(SW_SHOW);
	// 迷路操作中は矢印グローバルホットキー（音量／シーク）を止める
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::UnregisterHotKey(og->GetSafeHwnd(), 8000);
		::UnregisterHotKey(og->GetSafeHwnd(), 8001);
		::UnregisterHotKey(og->GetSafeHwnd(), 8002);
		::UnregisterHotKey(og->GetSafeHwnd(), 8003);
	}
}

void CloseSoft3DMazeIfOpen()
{
	if (g_s3m && g_s3m->GetSafeHwnd())
		g_s3m->DestroyWindow();
}

BOOL IsSoft3DMazeActive()
{
	if (!g_s3m || !g_s3m->GetSafeHwnd() || !::IsWindow(g_s3m->GetSafeHwnd()))
		return FALSE;
	const HWND maze = g_s3m->GetSafeHwnd();
	HWND fg = ::GetForegroundWindow();
	if (fg && (fg == maze || ::IsChild(maze, fg)))
		return TRUE;
	HWND focus = ::GetFocus();
	if (focus && (focus == maze || ::IsChild(maze, focus)))
		return TRUE;
	return FALSE;
}
