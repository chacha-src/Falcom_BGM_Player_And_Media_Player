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
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#endif

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

struct S3MFloat4 { float x, y, z, w; };
struct S3MMat { float m[16]; };
struct S3MVertex { float x,y,z, nx,ny,nz, u,v, r,g,b,a; };
struct S3MHudVertex { float x,y, r,g,b,a; };
struct S3MFrameCB {
	S3MMat viewProj;
	S3MMat lightVP;
	S3MMat reflectVP;
	S3MMat reflectFloorVP;
	S3MFloat4 eyePos, fogParams, dofParams, screenSize, misc, lightDir;
};

static S3MMat S3mMatMul(const S3MMat& a, const S3MMat& b)
{
	S3MMat r = {};
	for (int i=0;i<4;i++) for (int j=0;j<4;j++)
		for (int k=0;k<4;k++) r.m[i*4+j] += a.m[i*4+k]*b.m[k*4+j];
	return r;
}
static S3MMat S3mPerspective(float fovy, float aspect, float zn, float zf)
{
	S3MMat r = {};
	const float y = 1.f / tanf(fovy*.5f), x = y / aspect;
	r.m[0]=x; r.m[5]=y; r.m[10]=zf/(zf-zn); r.m[11]=1.f; r.m[14]=-zn*zf/(zf-zn);
	return r;
}
static S3MMat S3mOrtho(float l,float rgt,float b,float t,float zn,float zf)
{
	S3MMat r = {};
	r.m[0]=2.f/(rgt-l); r.m[5]=2.f/(t-b); r.m[10]=1.f/(zf-zn);
	r.m[12]=-(rgt+l)/(rgt-l); r.m[13]=-(t+b)/(t-b); r.m[14]=-zn/(zf-zn); r.m[15]=1.f;
	return r;
}
static S3MMat S3mLookAt(float ex,float ey,float ez,float ax,float ay,float az,float ux,float uy,float uz)
{
	float zx=ax-ex, zy=ay-ey, zz=az-ez;
	float zl=sqrtf(zx*zx+zy*zy+zz*zz); zx/=zl; zy/=zl; zz/=zl;
	float xx=uy*zz-uz*zy, xy=uz*zx-ux*zz, xz=ux*zy-uy*zx;
	float xl=sqrtf(xx*xx+xy*xy+xz*xz); xx/=xl; xy/=xl; xz/=xl;
	float yx=zy*xz-zz*xy, yy=zz*xx-zx*xz, yz=zx*xy-zy*xx;
	S3MMat r = {};
	r.m[0]=xx; r.m[1]=yx; r.m[2]=zx;
	r.m[4]=xy; r.m[5]=yy; r.m[6]=zy;
	r.m[8]=xz; r.m[9]=yz; r.m[10]=zz;
	r.m[12]=-(ex*xx+ey*xy+ez*xz);
	r.m[13]=-(ex*yx+ey*yy+ez*yz);
	r.m[14]=-(ex*zx+ey*zy+ez*zz); r.m[15]=1.f;
	return r;
}

#define S3M_RELEASE(p) do { if (p) { (p)->Release(); (p)=NULL; } } while(0)

static const int kPresets[] = { 10, 20, 30, 50, 80, 100, 150, 200, 300, 400, 600, 800, 1000, 1500, 2000, 3000 };
static const int kPresetCnt = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
enum { S3M_MENU_SIZE0 = 1000 };
static const int kLegacySizes[] = { 11, 15, 21, 31 };

#if _UNICODE
#define S3M_RUN_LEAF L"oggYSEDbgmu_s3mrun.dat"
#else
#define S3M_RUN_LEAF "oggYSEDbgm_s3mrun.dat"
#endif
static const DWORD S3M_RUN_MAGIC = 0x53334D32u; // '2M3S'（多階層。旧'1M3S'は読み捨て＝新規生成）

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
	line(LL14(L"大きさは 10〜3000（コンボで選択、または数字を直接入力）。右上ミニマップは進行方向が上、通過マスは着色。",
		L"Size is 10–3000 (combo or type a number). Top-right minimap: forward-up; visited cells tinted.",
		L"Taille 10–3000 (liste ou saisie). Minimap : avant en haut ; cases visitées teintées.",
		L"Dimensione 10–3000 (elenco o digita). Minimap: avanti in alto; celle visitate colorate.",
		L"Tamaño 10–3000 (lista o escribe). Minimapa: adelante arriba; celdas visitadas teñidas.",
		L"크기 10–3000(콤보 또는 직접 입력). 우측 미니맵은 진행이 위, 방문 칸 색칠.",
		L"尺寸 10–3000（下拉或手输）。右上小地图前进朝上，走过的格子着色。",
		L"الحجم 10–3000 (قائمة أو اكتب). الخريطة: الأمام أعلى؛ الخلايا المزورة ملوّنة.",
		L"Размер 10–3000 (список или ввод). Мини-карта: вперёд вверх; посещённые окрашены.",
		L"Größe 10–3000 (Liste oder Tippen). Minimap: Vorwärts oben; besuchte Felder getönt.",
		L"Tamanho 10–3000 (lista ou digite). Minimapa: frente cima; visitados coloridos.",
		L"Grootte 10–3000 (lijst of typen). Minimapa: vooruit omhoog; bezocht getint.",
		L"Rozmiar 10–3000 (lista lub wpisz). Minimapa: przód u góry; odwiedzone zabarwione.",
		L"Boyut 10–3000 (liste veya yazın). Harita: ileri yukarı; ziyaret edilen boyalı."));
	line(LL14(L"SPACE 押しっぱなし = 全体マップ（確認のみ）。ホイールクリック = 全体マップ切替（再クリック／左クリックで閉じる）。その間は移動不可。",
		L"Hold SPACE = full map (view only). Wheel-click = toggle map (again or left-click to close). No move while open.",
		L"Espace maintenu = carte. Clic molette = bascule (reclic / clic gauche pour fermer). Pas de déplacement.",
		L"Tieni SPAZIO = mappa. Clic rotella = attiva/disattiva (di nuovo o clic sinistro). Niente movimento.",
		L"Mantén Espacio = mapa. Clic rueda = alternar (otra vez o clic izq. cierra). Sin mover.",
		L"SPACE 유지 = 전체 맵. 휠 클릭 = 토글(다시/좌클릭으로 닫기). 표시 중 이동 불가.",
		L"按住空格 = 全图。滚轮点击 = 开关（再点/左键关闭）。显示中不可移动。",
		L"استمر مسافة = خريطة. نقر العجلة = تبديل (مرة أخرى/يسار للإغلاق). بلا حركة.",
		L"Удерживайте Пробел = карта. Клик колёсиком = вкл/выкл (ещё раз или ЛКМ). Без движения.",
		L"Leertaste halten = Karte. Radklick = umschalten (nochmals / Linksklick schließt). Keine Bewegung.",
		L"Segure Espaço = mapa. Clique da roda = liga/desliga (de novo ou esquerdo fecha). Sem mover.",
		L"Houd Spatie = kaart. Wielklik = aan/uit (opnieuw of linksklik sluit). Geen bewegen.",
		L"Trzymaj Spację = mapa. Klik kółkiem = włącz/wyłącz (ponownie lub LPM zamyka). Bez ruchu.",
		L"SPACE basılı = harita. Teker tık = aç/kapa (tekrar veya sol tık kapatır). Hareket yok."));
	line(LL14(L"「地下」で地下1〜3Fを追加。橙の階段=下り／水色=上り、ゴールは最下層。全体マップ中は ←→ かホイールで階層を確認。",
		L"\"Basement\" adds 1–3 lower floors. Orange stairs go down, cyan up; the goal sits on the deepest floor. In the full map, ←→ or wheel changes the shown floor.",
		L"« Sous-sol » ajoute 1–3 étages. Escaliers orange : descendre, cyan : monter ; but au plus profond. Dans la carte, ←→ ou molette change d'étage.",
		L"«Sotterraneo» aggiunge 1–3 piani. Scale arancioni giù, ciano su; traguardo nel piano più profondo. Nella mappa ←→ o rotella cambia piano.",
		L"«Sótano» añade 1–3 plantas. Escaleras naranjas bajan, cian suben; la meta está en la más profunda. En el mapa ←→ o rueda cambia planta.",
		L"「지하」로 지하 1~3층 추가. 주황 계단=하강, 하늘색=상승, 골은 최하층. 전체 맵에서 ←→ 또는 휠로 층 확인.",
		L"“地下”可添加 1–3 层。橙色楼梯下行，水色上行，终点在最深层。全图中 ←→ 或滚轮切换层。",
		L"«القبو» يضيف 1–3 طوابق. السلالم البرتقالية للأسفل والسماوية للأعلى، والهدف في الأعمق. في الخريطة ←→ أو العجلة تغيّر الطابق.",
		L"«Подвал» добавляет 1–3 этажа. Оранжевые лестницы вниз, голубые вверх; цель на нижнем этаже. На карте ←→ или колесо меняют этаж.",
		L"„Keller“ ergänzt 1–3 Etagen. Orange Treppen abwärts, Cyan aufwärts; Ziel in der tiefsten Etage. In der Karte wechselt ←→ oder das Rad die Etage.",
		L"“Subsolo” adiciona 1–3 pisos. Escadas laranja descem, ciano sobem; o gol fica no mais profundo. No mapa, ←→ ou roda muda o piso.",
		L"'Kelder' voegt 1–3 verdiepingen toe. Oranje trappen omlaag, cyaan omhoog; doel op de diepste. In de kaart wisselt ←→ of het wiel de verdieping.",
		L"„Piwnica” dodaje 1–3 poziomy. Pomarańczowe schody w dół, cyjanowe w górę; cel na najniższym. Na mapie ←→ lub kółko zmienia piętro.",
		L"“Bodrum” 1–3 kat ekler. Turuncu merdiven aşağı, camgöbeği yukarı; hedef en alt katta. Haritada ←→ veya teker katı değiştirir."));
	line(LL14(L"右クリック: リスタート / サイズ / ミニマップ / アイテム種類。進行は自動保存（再オープンで続きから）。",
		L"Right-click: restart / size / minimap / item types. Progress auto-saves (resume on reopen).",
		L"Clic droit : redémarrer / taille / minimap / objets. Progression auto-sauvegardée.",
		L"Clic destro: riavvio / dimensione / minimap / oggetti. Progresso salvato automaticamente.",
		L"Clic derecho: reinicio / tamaño / minimapa / objetos. Progreso se guarda solo.",
		L"우클릭: 재시작 / 크기 / 미니맵 / 아이템. 진행 자동 저장(다시 열면 이어하기).",
		L"右键：重启 / 尺寸 / 小地图 / 道具。进度自动保存（再开可续玩）。",
		L"يمين: إعادة / حجم / خريطة / عناصر. التقدم يُحفظ تلقائياً.",
		L"ПКМ: рестарт / размер / мини-карта / предметы. Прогресс сохраняется.",
		L"Rechtsklick: Neustart / Größe / Minimap / Items. Fortschritt wird gespeichert.",
		L"Direito: reiniciar / tamanho / minimapa / itens. Progresso salva sozinho (continuar).",
		L"Rechtsklik: herstart / grootte / minimap / items. Voortgang bewaart zich (hervatten).",
		L"PPM: restart / rozmiar / minimapa / przedmioty. Postęp zapisuje się (wznów).",
		L"Sağ tık: yeniden / boyut / harita / öğeler. İlerleme otomatik kaydolur."));
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
	if (sz & 1) { // 奇数は切り上げ（偶数格子と整合）
		sz++;
		if (sz > CSoft3DMazeDlg::S3M_MAX) sz = CSoft3DMazeDlg::S3M_MAX & ~1;
	}
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
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_MBUTTONDOWN()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CS3mView::CS3mView()
	: m_ready(FALSE), m_vw(0), m_vh(0), m_dev(NULL), m_imm(NULL), m_swap(NULL), m_bbRtv(NULL)
	, m_dsTex(NULL), m_dsv(NULL), m_dsSrv(NULL), m_sceneTex(NULL), m_sceneRtv(NULL), m_sceneSrv(NULL)
	, m_postTex(NULL), m_postRtv(NULL), m_postSrv(NULL), m_shadowTex(NULL), m_shadowDsv(NULL), m_shadowSrv(NULL)
	, m_mirrorDs(NULL), m_mirrorDsv(NULL)
	, m_vsTess(NULL), m_hsTess(NULL), m_dsTess(NULL)
	, m_psWall(NULL), m_vsSolid(NULL), m_psSolid(NULL), m_vsHud(NULL), m_psHud(NULL), m_vsPost(NULL)
	, m_psSsr(NULL), m_psDof(NULL), m_psFinal(NULL), m_ilPatch(NULL), m_ilSolid(NULL), m_ilHud(NULL)
	, m_cbFrame(NULL), m_vbDyn(NULL), m_vbHud(NULL), m_vbDynBytes(6*1024*1024), m_vbHudBytes(512*1024)
	, m_texEnv(NULL), m_srvEnv(NULL)
	, m_texClear(NULL), m_srvClear(NULL), m_clearTexW(0), m_clearTexH(0), m_texMap(NULL), m_srvMap(NULL), m_texTip(NULL), m_srvTip(NULL), m_tipW(0), m_tipH(0), m_texBadge(NULL), m_srvBadge(NULL), m_badgeW(0), m_badgeH(0), m_sampLin(NULL), m_sampPoint(NULL), m_sampCmp(NULL)
	, m_rsSolid(NULL), m_rsShadow(NULL), m_dssWrite(NULL), m_dssRead(NULL), m_dssOff(NULL), m_bsOpaque(NULL), m_bsAlpha(NULL)
	, m_bsAdd(NULL), m_dragging(0), m_dragTurnAcc(0)
{
	for (int i = 0; i < S3M_THEME_N; i++) {
		m_texBrick[i] = NULL; m_srvBrick[i] = NULL;
		m_texFloor[i] = NULL; m_srvFloor[i] = NULL;
	}
	for (int i = 0; i < S3M_MIRROR_N; i++) {
		m_mirrorTex[i] = NULL;
		m_mirrorRtv[i] = NULL;
		m_mirrorSrv[i] = NULL;
	}
}

CS3mView::~CS3mView() { ReleaseDx(); }

BOOL CS3mView::CreateShaders()
{
	static const char* hlsl =
		"cbuffer F:register(b0){row_major float4x4 VP;row_major float4x4 LightVP;row_major float4x4 ReflectVP;row_major float4x4 ReflectFloorVP;float4 Eye;float4 Fog;float4 Dof;float4 Screen;float4 Misc;float4 LightDir;}"
		"Texture2D T0:register(t0);Texture2D T1:register(t1);Texture2D Depth:register(t2);"
		"TextureCube Env:register(t3);Texture2D ShadowMap:register(t4);Texture2D MirrorMap:register(t5);Texture2D MirrorFloor:register(t6);"
		"SamplerState SL:register(s0);SamplerState SP:register(s1);SamplerComparisonState SCmp:register(s2);"
		"struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"struct P{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"struct D{float4 p:SV_POSITION;float3 w:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float4 c:TEXCOORD3;};"
		"P VST(V x){P o;o.p=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;return o;}"
		"struct HC{float e[4]:SV_TessFactor;float i[2]:SV_InsideTessFactor;};"
		"HC HPC(InputPatch<P,4> p,uint id:SV_PrimitiveID){HC o;float3 c=(p[0].p+p[1].p+p[2].p+p[3].p)*.25;float d=distance(c,Eye.xyz);float tf=clamp(42.-d*1.35,14.,36.);o.e[0]=o.e[1]=o.e[2]=o.e[3]=tf;o.i[0]=o.i[1]=tf;return o;}"
		"[domain(\"quad\")][partitioning(\"fractional_even\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"HPC\")]"
		"P HST(InputPatch<P,4> p,uint i:SV_OutputControlPointID,uint id:SV_PrimitiveID){return p[i];}"
		"[domain(\"quad\")]D DST(HC h,float2 q:SV_DomainLocation,const OutputPatch<P,4> p){"
		"P a,b,o;a.p=lerp(p[0].p,p[1].p,q.x);b.p=lerp(p[3].p,p[2].p,q.x);o.p=lerp(a.p,b.p,q.y);"
		"a.n=lerp(p[0].n,p[1].n,q.x);b.n=lerp(p[3].n,p[2].n,q.x);o.n=normalize(lerp(a.n,b.n,q.y));"
		"a.uv=lerp(p[0].uv,p[1].uv,q.x);b.uv=lerp(p[3].uv,p[2].uv,q.x);o.uv=lerp(a.uv,b.uv,q.y);o.c=p[0].c;"
		"float ht=T0.SampleLevel(SL,o.uv*2.5,0).a-.5;float bump=(ht*ht)*sign(ht);o.p+=o.n*bump*.11;D z;z.w=o.p;z.n=o.n;z.uv=o.uv;z.c=o.c;z.p=mul(float4(o.p,1),VP);return z;}"
		"float ShadowAt(float3 w){float4 lp=mul(float4(w,1),LightVP);float3 ndc=lp.xyz/max(lp.w,1e-5);"
		"float2 uv=float2(ndc.x*.5+.5,.5-ndc.y*.5);float z=ndc.z-0.0015;if(any(uv<0)||any(uv>1)||z<0||z>1)return 1;"
		"float s=0;const float t=1.0/1024.0;[unroll]for(int i=-1;i<=1;i++)[unroll]for(int j=-1;j<=1;j++)"
		"s+=ShadowMap.SampleCmpLevelZero(SCmp,uv+float2(i,j)*t,z);return s/9.0;}"
		"float4 PlanarMir(float3 w,row_major float4x4 RVP,Texture2D M){float4 rp=mul(float4(w,1),RVP);float iw=max(rp.w,1e-5);float2 muv=rp.xy/iw*float2(.5,-.5)+.5;"
		"float mb=(rp.w>0)*saturate(min(min(muv.x,1-muv.x),min(muv.y,1-muv.y))*6);return float4(M.Sample(SL,saturate(muv)).rgb,mb);}"
		"float4 PSW(D i):SV_Target{float4 a=T0.Sample(SL,i.uv*2.5)*i.c;float h=T0.Sample(SL,i.uv*2.5).a;"
		"float hx=T0.Sample(SL,i.uv*2.5+float2(.004,0)).a-h;float hy=T0.Sample(SL,i.uv*2.5+float2(0,.004)).a-h;"
		"float3 n=normalize(i.n+float3(hx,hy,0)*4.2);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w);"
		"float nd=.42+.58*saturate(dot(n,l))*lerp(.55,1,sh);"
		"float3 v=normalize(Eye.xyz-i.w);float3 r=reflect(-l,n);float rv=saturate(dot(r,v));"
		"float sp=pow(rv,56)*sh;float spark=pow(rv,180)*sh;float3 sun=float3(1.0,.94,.78);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float fr=pow(1-saturate(dot(n,v)),3);"
		"float metal=saturate((i.c.a-1.01)*8);float useMir=LightDir.w;float4 mir=PlanarMir(i.w,ReflectVP,MirrorMap);"
		"float mw=metal*useMir*max(mir.a,.25);env=lerp(env,mir.rgb,mw);"
		"float3 col=lerp(a.rgb*nd,mir.rgb*(.35+.65*a.rgb),mw*.85)+sun*(sp*.45+spark*.9)+env*(.06+fr*.12)*(1-mw*.5);"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=saturate(fg+max(0,Fog.w-i.w.y)*Fog.z);"
		"return float4(lerp(col,float3(.45,.58,.72),fg*.22),1);}"
		"D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}"
		"float4 PSS(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w);"
		"float3 v=normalize(Eye.xyz-i.w);float nd=.35+.65*saturate(dot(n,l))*lerp(.55,1,sh);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),64)*sh;float3 env=Env.Sample(SL,reflect(-v,n)).rgb;"
		"float fr=pow(1-saturate(dot(n,v)),2.5);float mirror=saturate((i.c.a-1.01)*8);float useMir=LightDir.w;"
		"float3 rd=reflect(-v,n);float4 mir=PlanarMir(i.w,ReflectFloorVP,MirrorFloor);float4 mir2=PlanarMir(i.w+rd*1.25,ReflectFloorVP,MirrorFloor);"
		"if(mir2.a>mir.a)mir=mir2;float mw=mirror*useMir*max(mir.a,.4);"
		"float3 lit=i.c.rgb*(.4+.6*nd);float3 c=lerp(lit,mir.rgb*(.5+.5*i.c.rgb),mw*.95)+env*((.2+fr*.3)*(1-mw))+float3(1,.96,.82)*sp*(.5+mirror);"
		"float al=mirror>0?lerp(.78,.58,mw):saturate(i.c.a);float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));"
		"return float4(lerp(c,float3(.45,.58,.72),fg*.2),al);}"
		"struct HV{float2 p:POSITION;float4 c:TEXCOORD0;};struct HO{float4 p:SV_POSITION;float4 c:TEXCOORD0;};"
		"HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.c=x.c;return o;}float4 PSH(HO i):SV_Target{return i.c;}"
		"struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}"
		"float4 SSR(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);float z=Depth.Sample(SP,i.uv).r;float2 dir=float2((i.uv.x-.5)*.03,-.018);"
		"float3 r=0;float hit=0;[loop]for(int k=1;k<20;k++){float2 u=i.uv+dir*k;if(any(u<0)||any(u>1))break;float dz=Depth.Sample(SP,u).r;if(dz+0.001<z){r=T0.Sample(SL,u).rgb;hit=1;break;}}"
		"float metal=saturate((z-.10)*2.5)*0.12;return float4(lerp(c.rgb,lerp(c.rgb,r,hit),metal),1);}"
		"float4 DOFP(Q i):SV_Target{float z=Depth.Sample(SP,i.uv).r;float b=saturate(abs(z-Dof.x)/max(.001,Dof.y))*Dof.z;float2 d=Screen.zw*b;"
		"float4 c=T0.Sample(SL,i.uv)*.28;c+=(T0.Sample(SL,i.uv+float2(d.x,0))+T0.Sample(SL,i.uv-float2(d.x,0))+T0.Sample(SL,i.uv+float2(0,d.y))+T0.Sample(SL,i.uv-float2(0,d.y)))*.18;return c;}"
		"float4 FIN(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);if(Misc.z>8.f)return c;float v=saturate(1-dot((i.uv-.5)*1.05,(i.uv-.5)*1.05));c.rgb*=lerp(.88,1.06,v);return c;}";
	ID3DBlob *b[11]={0}, *err=NULL;
	const char* entries[11]={"VST","HST","DST","PSW","VSS","PSS","VSH","PSH","VSQ","SSR","DOFP"};
	const char* profiles[11]={"vs_5_0","hs_5_0","ds_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","ps_5_0"};
	for(int i=0;i<11;i++) {
		if(FAILED(D3DCompile(hlsl,strlen(hlsl),NULL,NULL,NULL,entries[i],profiles[i],D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b[i],&err))) {
			S3M_RELEASE(err); for(int j=0;j<11;j++) S3M_RELEASE(b[j]); return FALSE;
		}
		S3M_RELEASE(err);
	}
	ID3DBlob* bf=NULL;
	if(FAILED(D3DCompile(hlsl,strlen(hlsl),NULL,NULL,NULL,"FIN","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bf,&err))) {
		for(int i=0;i<11;i++) S3M_RELEASE(b[i]); S3M_RELEASE(err); return FALSE;
	}
	HRESULT hr=S_OK;
	hr|=m_dev->CreateVertexShader(b[0]->GetBufferPointer(),b[0]->GetBufferSize(),NULL,&m_vsTess);
	hr|=m_dev->CreateHullShader(b[1]->GetBufferPointer(),b[1]->GetBufferSize(),NULL,&m_hsTess);
	hr|=m_dev->CreateDomainShader(b[2]->GetBufferPointer(),b[2]->GetBufferSize(),NULL,&m_dsTess);
	hr|=m_dev->CreatePixelShader(b[3]->GetBufferPointer(),b[3]->GetBufferSize(),NULL,&m_psWall);
	hr|=m_dev->CreateVertexShader(b[4]->GetBufferPointer(),b[4]->GetBufferSize(),NULL,&m_vsSolid);
	hr|=m_dev->CreatePixelShader(b[5]->GetBufferPointer(),b[5]->GetBufferSize(),NULL,&m_psSolid);
	hr|=m_dev->CreateVertexShader(b[6]->GetBufferPointer(),b[6]->GetBufferSize(),NULL,&m_vsHud);
	hr|=m_dev->CreatePixelShader(b[7]->GetBufferPointer(),b[7]->GetBufferSize(),NULL,&m_psHud);
	hr|=m_dev->CreateVertexShader(b[8]->GetBufferPointer(),b[8]->GetBufferSize(),NULL,&m_vsPost);
	hr|=m_dev->CreatePixelShader(b[9]->GetBufferPointer(),b[9]->GetBufferSize(),NULL,&m_psSsr);
	hr|=m_dev->CreatePixelShader(b[10]->GetBufferPointer(),b[10]->GetBufferSize(),NULL,&m_psDof);
	hr|=m_dev->CreatePixelShader(bf->GetBufferPointer(),bf->GetBufferSize(),NULL,&m_psFinal);
	D3D11_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
	D3D11_INPUT_ELEMENT_DESC ih[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
	hr|=m_dev->CreateInputLayout(il,4,b[0]->GetBufferPointer(),b[0]->GetBufferSize(),&m_ilPatch);
	hr|=m_dev->CreateInputLayout(il,4,b[4]->GetBufferPointer(),b[4]->GetBufferSize(),&m_ilSolid);
	hr|=m_dev->CreateInputLayout(ih,2,b[6]->GetBufferPointer(),b[6]->GetBufferSize(),&m_ilHud);
	for(int i=0;i<11;i++) S3M_RELEASE(b[i]); S3M_RELEASE(bf);
	return SUCCEEDED(hr);
}

BOOL CS3mView::CreateProcTextures()
{
	// 階テーマごと 512² アトラス（4×4=16種）＋床テクスチャ
	const int AW=512,AH=512,TW=128,TH=128;
	auto hash=[&](int x,int y,int s)->int{return ((x*73856093)^(y*19349663)^(s*83492791))&255;};
	auto genWall=[&](int theme,DWORD* atlas){
		for(int ty=0;ty<4;ty++)for(int tx=0;tx<4;tx++){
			const int vid=ty*4+tx;
			for(int ly=0;ly<TH;ly++)for(int lx=0;lx<TW;lx++){
				int x=tx*TW+lx,y=ty*TH+ly;
				int row=ly/16,bx=(lx+((row&1)?32:0))&63,by=ly&15;BOOL mortar=by<2||bx<2;
				int n=hash(lx,ly,vid+theme*97)-128;
				BYTE r,g,b,a;
				if(theme==0){ // 地上：草木・苔・花
					if(vid==0){r=(BYTE)(mortar?110:190+n/8);g=(BYTE)(mortar?90:125+n/10);b=(BYTE)(mortar?70:85+n/12);a=(BYTE)(mortar?70:150+n/6);}
					else if(vid<=3){r=(BYTE)(mortar?90:140+n/10);g=(BYTE)(mortar?100:160+n/8);b=(BYTE)(mortar?70:90+n/12);a=(BYTE)(mortar?90:170+(n>40?40:0));}
					else if(vid<=6){r=(BYTE)(70+n/12);g=(BYTE)(130+n/6);b=(BYTE)(55+n/14);BOOL blade=((lx+ly*3+vid*17)&15)<3;if(blade){r=50;g=180;b=40;}a=(BYTE)(blade?210:140);}
					else if(vid<=9){r=(BYTE)(120+n/10);g=(BYTE)(110+n/10);b=(BYTE)(90+n/12);BOOL fl=((hash(lx/4,ly/4,vid)&31)==0);if(fl){r=220;g=80;b=140;a=200;}else a=150;}
					else if(vid<=12){r=(BYTE)(150+n/8);g=(BYTE)(145+n/8);b=(BYTE)(120+n/10);BOOL crack=((lx*3+ly)&31)<2;if(crack){r=80;g=70;b=60;}a=(BYTE)(crack?100:165);}
					else{r=(BYTE)(40+n/14);g=(BYTE)(90+n/6);b=(BYTE)(35+n/14);a=(BYTE)(160+abs(n)/4);}
				}else if(theme==1){ // 地下1：湿った青灰の石
					r=(BYTE)(mortar?55:90+n/10);g=(BYTE)(mortar?70:110+n/8);b=(BYTE)(mortar?85:140+n/7);
					BOOL drip=((lx+ly*2+vid)&47)<2;if(drip){r=70;g=120;b=170;}
					a=(BYTE)(mortar?100:175+(n>30?20:0));
				}else if(theme==2){ // 地下2：錆びた金属・銅板
					r=(BYTE)(mortar?70:150+n/9);g=(BYTE)(mortar?55:95+n/11);b=(BYTE)(mortar?45:60+n/14);
					BOOL rivet=((lx&31)<4&&(ly&31)<4);if(rivet){r=180;g=160;b=120;}
					BOOL rust=((hash(lx/3,ly/3,vid)&15)==0);if(rust){r=170;g=80;b=40;}
					a=(BYTE)(mortar?90:185);
				}else{ // 地下3：暗い岩＋赤熱のひび
					r=(BYTE)(mortar?35:55+n/12);g=(BYTE)(mortar?30:48+n/14);b=(BYTE)(mortar?40:58+n/14);
					BOOL crack=((lx*5+ly*3+vid)&63)<3;if(crack){r=220;g=90;b=30;a=220;}else a=(BYTE)(mortar?80:170);
				}
				atlas[y*AW+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
			}
		}
	};
	auto genFloor=[&](int theme,DWORD* p,int W,int H){
		for(int y=0;y<H;y++)for(int x=0;x<W;x++){
			int n=((x*17+y*29+(x*y)%31+theme*13)&31)-15;BYTE r,g,b;
			if(theme==0){BYTE v=(BYTE)(145+n);r=v;g=(BYTE)(v*4/5);b=(BYTE)(v*2/3);} // 土っぽい床
			else if(theme==1){r=(BYTE)(70+n);g=(BYTE)(90+n);b=(BYTE)(110+n/2);} // 湿った石床
			else if(theme==2){r=(BYTE)(55+n/2);g=(BYTE)(58+n/2);b=(BYTE)(62+n/2);if(((x^y)&7)==0){r=90;g=70;b=40;}} // 金属グレー＋継ぎ目
			else{r=(BYTE)(40+n/2);g=(BYTE)(32+n/3);b=(BYTE)(36+n/3);if(((x*3+y)&31)<2){r=160;g=50;b=20;}} // 炭＋赤脈
			p[y*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;
		}
	};
	D3D11_TEXTURE2D_DESC d={}; d.Width=AW;d.Height=AH;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	DWORD* atlas=new DWORD[AW*AH];
	for(int th=0;th<S3M_THEME_N;th++){
		genWall(th,atlas);
		D3D11_SUBRESOURCE_DATA sd={atlas,AW*4,0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texBrick[th]))||FAILED(m_dev->CreateShaderResourceView(m_texBrick[th],NULL,&m_srvBrick[th]))){delete[] atlas;return FALSE;}
	}
	delete[] atlas;
	const int W=128,H=128; DWORD* p=new DWORD[W*H];
	d.Width=W;d.Height=H;
	for(int th=0;th<S3M_THEME_N;th++){
		genFloor(th,p,W,H);
		D3D11_SUBRESOURCE_DATA sd={p,W*4,0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texFloor[th]))||FAILED(m_dev->CreateShaderResourceView(m_texFloor[th],NULL,&m_srvFloor[th]))){delete[] p;return FALSE;}
	}
	D3D11_SUBRESOURCE_DATA sd={};d.ArraySize=6;d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;D3D11_SUBRESOURCE_DATA cs[6]={};
	DWORD* cube=new DWORD[W*H*6];
	for(int f=0;f<6;f++){for(int y=0;y<H;y++)for(int x=0;x<W;x++){float t=(float)y/(H-1);BYTE r=(BYTE)(110+70*(1-t)),g=(BYTE)(145+55*(1-t)),b=(BYTE)(175+40*(1-t));if(f==2){r=(BYTE)(85+35*t);g=(BYTE)(120+45*t);b=(BYTE)(60+25*t);}cube[(f*H+y)*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;}cs[f].pSysMem=cube+f*W*H;cs[f].SysMemPitch=W*4;}
	if(FAILED(m_dev->CreateTexture2D(&d,cs,&m_texEnv))||FAILED(m_dev->CreateShaderResourceView(m_texEnv,NULL,&m_srvEnv))){delete[] cube;delete[] p;return FALSE;}
	delete[] cube;delete[] p;
	{
		D3D11_TEXTURE2D_DESC md={};md.Width=S3M_MAP_SIZE;md.Height=S3M_MAP_SIZE;md.MipLevels=1;md.ArraySize=1;
		md.Format=DXGI_FORMAT_B8G8R8A8_UNORM;md.SampleDesc.Count=1;md.Usage=D3D11_USAGE_DYNAMIC;
		md.BindFlags=D3D11_BIND_SHADER_RESOURCE;md.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
		if(FAILED(m_dev->CreateTexture2D(&md,NULL,&m_texMap))||FAILED(m_dev->CreateShaderResourceView(m_texMap,NULL,&m_srvMap)))
			return FALSE;
	}
	return TRUE;
}

BOOL CS3mView::InitDx()
{
	ReleaseDx();
	UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	flags|=D3D11_CREATE_DEVICE_DEBUG;
#endif
	D3D_FEATURE_LEVEL req=D3D_FEATURE_LEVEL_11_0, got=(D3D_FEATURE_LEVEL)0;
	HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,flags,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr)) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,flags&~D3D11_CREATE_DEVICE_DEBUG,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr)||got!=D3D_FEATURE_LEVEL_11_0) return FALSE;
	IDXGIDevice* xd=NULL;IDXGIAdapter* xa=NULL;IDXGIFactory2* f2=NULL;IDXGIFactory* f1=NULL;
	if(FAILED(m_dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(xd->GetAdapter(&xa))) {S3M_RELEASE(xd);return FALSE;}
	xa->GetParent(__uuidof(IDXGIFactory2),(void**)&f2);
	DXGI_SWAP_CHAIN_DESC1 s={};s.Width=0;s.Height=0;s.Format=DXGI_FORMAT_B8G8R8A8_UNORM;s.SampleDesc.Count=1;s.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;s.BufferCount=2;s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;s.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
	if(f2){IDXGISwapChain1* sc1=NULL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);if(SUCCEEDED(hr))m_swap=sc1;}else hr=E_FAIL;
	if(FAILED(hr)){xa->GetParent(__uuidof(IDXGIFactory),(void**)&f1);DXGI_SWAP_CHAIN_DESC o={};o.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;o.SampleDesc.Count=1;o.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;o.BufferCount=1;o.OutputWindow=m_hWnd;o.Windowed=TRUE;o.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;hr=f1?f1->CreateSwapChain(m_dev,&o,&m_swap):E_FAIL;}
	S3M_RELEASE(f1);S3M_RELEASE(f2);S3M_RELEASE(xa);S3M_RELEASE(xd);if(FAILED(hr))return FALSE;
	if(!CreateShaders()||!CreateProcTextures())return FALSE;
	D3D11_BUFFER_DESC bd={};bd.ByteWidth=sizeof(S3MFrameCB);bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;if(FAILED(m_dev->CreateBuffer(&bd,NULL,&m_cbFrame)))return FALSE;
	bd.ByteWidth=m_vbDynBytes;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;if(FAILED(m_dev->CreateBuffer(&bd,NULL,&m_vbDyn)))return FALSE;bd.ByteWidth=m_vbHudBytes;if(FAILED(m_dev->CreateBuffer(&bd,NULL,&m_vbHud)))return FALSE;
	D3D11_SAMPLER_DESC ss={};ss.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;ss.MaxLOD=D3D11_FLOAT32_MAX;m_dev->CreateSamplerState(&ss,&m_sampLin);ss.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;m_dev->CreateSamplerState(&ss,&m_sampPoint);
	ss.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;ss.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;ss.BorderColor[0]=ss.BorderColor[1]=ss.BorderColor[2]=ss.BorderColor[3]=1.f;m_dev->CreateSamplerState(&ss,&m_sampCmp);
	D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;m_dev->CreateRasterizerState(&rs,&m_rsSolid);
	rs.DepthBias=2500;rs.SlopeScaledDepthBias=1.75f;rs.DepthBiasClamp=0.f;m_dev->CreateRasterizerState(&rs,&m_rsShadow);
	D3D11_DEPTH_STENCIL_DESC ds={};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;m_dev->CreateDepthStencilState(&ds,&m_dssWrite);ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;m_dev->CreateDepthStencilState(&ds,&m_dssRead);ds.DepthEnable=FALSE;m_dev->CreateDepthStencilState(&ds,&m_dssOff);
	D3D11_BLEND_DESC bl={};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;m_dev->CreateBlendState(&bl,&m_bsOpaque);bl.RenderTarget[0].BlendEnable=TRUE;bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bl.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bl.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;m_dev->CreateBlendState(&bl,&m_bsAlpha);bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_ONE;m_dev->CreateBlendState(&bl,&m_bsAdd);
	{
		D3D11_TEXTURE2D_DESC td={};td.Width=S3M_SHADOW_SIZE;td.Height=S3M_SHADOW_SIZE;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
		if(FAILED(m_dev->CreateTexture2D(&td,NULL,&m_shadowTex)))return FALSE;
		D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(m_dev->CreateDepthStencilView(m_shadowTex,&dd,&m_shadowDsv)))return FALSE;
		D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
		if(FAILED(m_dev->CreateShaderResourceView(m_shadowTex,&sd,&m_shadowSrv)))return FALSE;
	}
	{
		D3D11_TEXTURE2D_DESC td={};td.Width=S3M_MIRROR_SIZE;td.Height=S3M_MIRROR_SIZE;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
		for(int i=0;i<S3M_MIRROR_N;i++){
			if(FAILED(m_dev->CreateTexture2D(&td,NULL,&m_mirrorTex[i])))return FALSE;
			if(FAILED(m_dev->CreateRenderTargetView(m_mirrorTex[i],NULL,&m_mirrorRtv[i])))return FALSE;
			if(FAILED(m_dev->CreateShaderResourceView(m_mirrorTex[i],NULL,&m_mirrorSrv[i])))return FALSE;
		}
		td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
		if(FAILED(m_dev->CreateTexture2D(&td,NULL,&m_mirrorDs)))return FALSE;
		D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(m_dev->CreateDepthStencilView(m_mirrorDs,&dd,&m_mirrorDsv)))return FALSE;
	}
	CRect rc;GetClientRect(&rc);return ResizeDx(max(8,rc.Width()),max(8,rc.Height()));
}

BOOL CS3mView::EnsureSceneTargets(int w,int h)
{
	if(m_sceneTex&&w==m_vw&&h==m_vh)return TRUE;
	ID3D11RenderTargetView* nullrt=NULL;m_imm->OMSetRenderTargets(1,&nullrt,NULL);
	S3M_RELEASE(m_dsSrv);S3M_RELEASE(m_dsv);S3M_RELEASE(m_dsTex);S3M_RELEASE(m_sceneSrv);S3M_RELEASE(m_sceneRtv);S3M_RELEASE(m_sceneTex);S3M_RELEASE(m_postSrv);S3M_RELEASE(m_postRtv);S3M_RELEASE(m_postTex);
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
	if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_sceneTex))||FAILED(m_dev->CreateRenderTargetView(m_sceneTex,NULL,&m_sceneRtv))||FAILED(m_dev->CreateShaderResourceView(m_sceneTex,NULL,&m_sceneSrv)))return FALSE;
	if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_postTex))||FAILED(m_dev->CreateRenderTargetView(m_postTex,NULL,&m_postRtv))||FAILED(m_dev->CreateShaderResourceView(m_postTex,NULL,&m_postSrv)))return FALSE;
	d.Format=DXGI_FORMAT_R24G8_TYPELESS;d.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_dsTex)))return FALSE;
	D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;if(FAILED(m_dev->CreateDepthStencilView(m_dsTex,&dd,&m_dsv)))return FALSE;
	D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;if(FAILED(m_dev->CreateShaderResourceView(m_dsTex,&sd,&m_dsSrv)))return FALSE;
	m_vw=w;m_vh=h;return TRUE;
}

BOOL CS3mView::ResizeDx(int w,int h)
{
	if(!m_swap||w<1||h<1)return FALSE;m_ready=FALSE;m_imm->OMSetRenderTargets(0,NULL,NULL);S3M_RELEASE(m_bbRtv);
	HRESULT hr=m_swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))return FALSE;ID3D11Texture2D* bb=NULL;hr=m_swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);if(SUCCEEDED(hr))hr=m_dev->CreateRenderTargetView(bb,NULL,&m_bbRtv);S3M_RELEASE(bb);
	if(FAILED(hr)||!EnsureSceneTargets(w,h))return FALSE;m_ready=TRUE;return TRUE;
}

void CS3mView::PresentFrame(){if(m_swap&&m_ready)m_swap->Present(0,0);}
void CS3mView::ReleaseClearTexture(){S3M_RELEASE(m_srvClear);S3M_RELEASE(m_texClear);m_clearTexW=m_clearTexH=0;}
void CS3mView::ReleaseTipTexture(){S3M_RELEASE(m_srvTip);S3M_RELEASE(m_texTip);m_tipW=m_tipH=0;}
void CS3mView::ReleaseBadgeTexture(){S3M_RELEASE(m_srvBadge);S3M_RELEASE(m_texBadge);m_badgeW=m_badgeH=0;}

BOOL CS3mView::BakeBadgeTexture(const wchar_t* text)
{
	if (!m_dev || !text) return FALSE;
	ReleaseBadgeTexture();
	const int w = 220, h = 36;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = NULL;
	HDC dc = CreateCompatibleDC(NULL);
	HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	HGDIOBJ old = SelectObject(dc, bm);
	memset(bits, 0, w * h * 4);
	{
		Gdiplus::Graphics g(dc);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::SolidBrush bg(Gdiplus::Color(170, 6, 8, 14));
		g.FillRectangle(&bg, 0, 0, w, h);
		Gdiplus::FontFamily ff(L"Segoe UI");
		Gdiplus::Font font(&ff, 15.f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::StringFormat sf;
		sf.SetAlignment(Gdiplus::StringAlignmentCenter);
		sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		Gdiplus::SolidBrush sh(Gdiplus::Color(180, 0, 0, 0));
		Gdiplus::SolidBrush fg(Gdiplus::Color(240, 235, 240, 250));
		Gdiplus::RectF r(2.f, 1.f, (Gdiplus::REAL)(w - 2), (Gdiplus::REAL)(h - 2));
		g.DrawString(text, -1, &font, r, &sf, &sh);
		r.X -= 1.f; r.Y -= 1.f;
		g.DrawString(text, -1, &font, r, &sf, &fg);
	}
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, (UINT)w * 4, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texBadge);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texBadge, NULL, &m_srvBadge);
	SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc);
	m_badgeW = w; m_badgeH = h;
	return SUCCEEDED(hr);
}

BOOL CS3mView::BakeClearTexture(const wchar_t* text,float alpha)
{
	ReleaseClearTexture();const int w=max(256,m_vw),h=max(96,m_vh/4);BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
	void* bits=NULL;HDC dc=CreateCompatibleDC(NULL);HBITMAP bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,NULL,0);HGDIOBJ old=SelectObject(dc,bm);memset(bits,0,w*h*4);
	{Gdiplus::Graphics g(dc);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);Gdiplus::FontFamily ff(L"Segoe UI");Gdiplus::Font font(&ff,(Gdiplus::REAL)max(32,h/2),Gdiplus::FontStyleBold,Gdiplus::UnitPixel);Gdiplus::StringFormat sf;sf.SetAlignment(Gdiplus::StringAlignmentCenter);sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);Gdiplus::SolidBrush sh(Gdiplus::Color((BYTE)(alpha*150),0,0,0)),fg(Gdiplus::Color((BYTE)(alpha*255),255,230,110));Gdiplus::RectF r(3,3,(Gdiplus::REAL)w,(Gdiplus::REAL)h);g.DrawString(text,-1,&font,r,&sf,&sh);r.X-=3;r.Y-=3;g.DrawString(text,-1,&font,r,&sf,&fg);}
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA sd={bits,(UINT)w*4,0};HRESULT hr=m_dev->CreateTexture2D(&d,&sd,&m_texClear);if(SUCCEEDED(hr))hr=m_dev->CreateShaderResourceView(m_texClear,NULL,&m_srvClear);SelectObject(dc,old);DeleteObject(bm);DeleteDC(dc);m_clearTexW=w;m_clearTexH=h;return SUCCEEDED(hr);
}

BOOL CS3mView::BakeTipTexture(const wchar_t* text)
{
	if (!m_dev || !text) return FALSE;
	ReleaseTipTexture();
	const int w = 560, h = 84;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = NULL;
	HDC dc = CreateCompatibleDC(NULL);
	HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	HGDIOBJ old = SelectObject(dc, bm);
	memset(bits, 0, w * h * 4);
	{
		Gdiplus::Graphics g(dc);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::SolidBrush bg(Gdiplus::Color(150, 8, 10, 16));
		g.FillRectangle(&bg, 0, 0, w, h);
		Gdiplus::FontFamily ff(L"Segoe UI");
		Gdiplus::Font font(&ff, 13.f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::StringFormat sf;
		sf.SetAlignment(Gdiplus::StringAlignmentNear);
		sf.SetLineAlignment(Gdiplus::StringAlignmentNear);
		Gdiplus::SolidBrush sh(Gdiplus::Color(160, 0, 0, 0));
		Gdiplus::SolidBrush fg(Gdiplus::Color(235, 230, 235, 245));
		Gdiplus::RectF r(9.f, 7.f, (Gdiplus::REAL)(w - 14), (Gdiplus::REAL)(h - 10));
		g.DrawString(text, -1, &font, r, &sf, &sh);
		r.X -= 1.f; r.Y -= 1.f;
		g.DrawString(text, -1, &font, r, &sf, &fg);
	}
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, (UINT)w * 4, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texTip);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texTip, NULL, &m_srvTip);
	SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc);
	m_tipW = w; m_tipH = h;
	return SUCCEEDED(hr);
}

void CS3mView::ReleaseDx()
{
	m_ready=FALSE;if(m_imm){m_imm->ClearState();m_imm->Flush();}
	ReleaseClearTexture();ReleaseTipTexture();ReleaseBadgeTexture();S3M_RELEASE(m_srvMap);S3M_RELEASE(m_texMap);S3M_RELEASE(m_srvEnv);S3M_RELEASE(m_texEnv);
	for(int i=0;i<S3M_THEME_N;i++){S3M_RELEASE(m_srvFloor[i]);S3M_RELEASE(m_texFloor[i]);S3M_RELEASE(m_srvBrick[i]);S3M_RELEASE(m_texBrick[i]);}
	S3M_RELEASE(m_bsAdd);S3M_RELEASE(m_bsAlpha);S3M_RELEASE(m_bsOpaque);S3M_RELEASE(m_dssOff);S3M_RELEASE(m_dssRead);S3M_RELEASE(m_dssWrite);S3M_RELEASE(m_rsShadow);S3M_RELEASE(m_rsSolid);S3M_RELEASE(m_sampCmp);S3M_RELEASE(m_sampPoint);S3M_RELEASE(m_sampLin);
	S3M_RELEASE(m_vbHud);S3M_RELEASE(m_vbDyn);S3M_RELEASE(m_cbFrame);S3M_RELEASE(m_ilHud);S3M_RELEASE(m_ilSolid);S3M_RELEASE(m_ilPatch);
	S3M_RELEASE(m_psFinal);S3M_RELEASE(m_psDof);S3M_RELEASE(m_psSsr);S3M_RELEASE(m_vsPost);S3M_RELEASE(m_psHud);S3M_RELEASE(m_vsHud);S3M_RELEASE(m_psSolid);S3M_RELEASE(m_vsSolid);S3M_RELEASE(m_psWall);S3M_RELEASE(m_dsTess);S3M_RELEASE(m_hsTess);S3M_RELEASE(m_vsTess);
	S3M_RELEASE(m_shadowSrv);S3M_RELEASE(m_shadowDsv);S3M_RELEASE(m_shadowTex);
	for(int i=0;i<S3M_MIRROR_N;i++){S3M_RELEASE(m_mirrorSrv[i]);S3M_RELEASE(m_mirrorRtv[i]);S3M_RELEASE(m_mirrorTex[i]);}
	S3M_RELEASE(m_mirrorDsv);S3M_RELEASE(m_mirrorDs);
	S3M_RELEASE(m_postSrv);S3M_RELEASE(m_postRtv);S3M_RELEASE(m_postTex);S3M_RELEASE(m_sceneSrv);S3M_RELEASE(m_sceneRtv);S3M_RELEASE(m_sceneTex);S3M_RELEASE(m_dsSrv);S3M_RELEASE(m_dsv);S3M_RELEASE(m_dsTex);S3M_RELEASE(m_bbRtv);S3M_RELEASE(m_swap);S3M_RELEASE(m_imm);S3M_RELEASE(m_dev);m_vw=m_vh=0;
}

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
	if(m_swap&&cx>0&&cy>0)ResizeDx(cx,cy);
}

void CS3mView::OnPaint()
{
	CPaintDC dc(this);
	ValidateRect(NULL);
}

LRESULT CS3mView::OnPrintClient(WPARAM, LPARAM){return 0;}
void CS3mView::OnDestroy(){ReleaseDx();CCustomStatic::OnDestroy();}

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
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (dlg && dlg->ConsumeOverviewClick())
		return;
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
	if (dlg) {
		// 全体マップ中はホイールで表示階層を切替
		if (dlg->IsOverviewActive()) {
			dlg->InputOverviewFloorDelta(zDelta > 0 ? 1 : -1);
			return TRUE;
		}
		dlg->InputTurn(zDelta > 0 ? -1 : 1);
	}
	return TRUE;
}

void CS3mView::OnMButtonDown(UINT nFlags, CPoint point)
{
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (dlg)
		dlg->ToggleMapOverlay();
	CCustomStatic::OnMButtonDown(nFlags, point);
}

// ---- dialog ----
IMPLEMENT_DYNAMIC(CSoft3DMazeDlg, CCustomBlurDialogBase)

CSoft3DMazeDlg::CSoft3DMazeDlg(CWnd* p)
	: CCustomBlurDialogBase(IDD, p)
	, m_grid(NULL), m_visit(NULL)
	, m_n(0), m_nFloors(1), m_floor(0), m_mapViewFloor(0)
	, m_px(1.5f), m_pz(1.5f), m_yaw((float)M_PI)
	, m_yawTarget((float)M_PI), m_pxTarget(1.5f), m_pzTarget(1.5f)
	, m_turning(0), m_turnHeld(0), m_moving(0), m_moveHeld(0)
	, m_bob(0.f), m_anim(0.f), m_won(0)
	, m_clearPhase(CLEAR_IDLE), m_clearT(0.f), m_clearTextA(0.f), m_clearScreenA(0.f)
	, m_clearTextAPrev(-1.f)
	, m_floorFx(FLOORFX_IDLE), m_floorFxT(0.f), m_floorTextA(0.f), m_floorScreenA(0.f)
	, m_floorTextAPrev(-1.f)
	, m_stairFrom(0), m_stairTo(0), m_stairSwapDone(0)
	, m_stairShiftX(0.f), m_stairShiftZ(0.f), m_stairCamY(0.f)
	, m_miniFade(1.f), m_miniFadeFrom(0), m_miniFadeTo(0)
	, m_itemsLeft(0)
	, m_baseTempoPos(200), m_basePitchPos(200)
	, m_lastTick(0), m_rng(GetTickCount()), m_genSeed(GetTickCount())
	, m_lastAutosave(0), m_runDirty(0), m_mapBakeDirty(1), m_mapToggle(0)
	, m_overviewFloorHeld(0)
{
	for (int f = 0; f < S3M_MAX_FLOORS; f++) {
		m_grids[f] = NULL;
		m_visits[f] = NULL;
	}
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
	DDX_Control(pDX, IDC_S3M_BASE_L, m_baseL);
	DDX_Control(pDX, IDC_S3M_BASE, m_base);
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
	ON_CBN_SELCHANGE(IDC_S3M_BASE, &CSoft3DMazeDlg::OnBaseChanged)
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
	// SPACE = 全体マップ（押しっぱなし）。コンボ／編集中は入力を妨げない。MPの再生トグルへ渡さない
	if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_KEYUP || pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_SYSKEYUP) {
		if (pMsg->wParam == VK_SPACE) {
			CWnd* f = GetFocus();
			const BOOL inEdit = (f && (f == &m_size || m_size.IsChild(f) || f->IsKindOf(RUNTIME_CLASS(CEdit))));
			if (!inEdit)
				return TRUE;
		}
		// 全体マップ中の ←→ は階層切替に使うので、地下コンボの選択を動かさない
		if ((pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT) && IsOverviewActive()) {
			CWnd* f = GetFocus();
			if (f && (f == &m_base || m_base.IsChild(f)))
				return TRUE;
		}
	}
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
		m_size.SetWindowPos(NULL, m + 52, y, 118, 200, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_baseL.GetSafeHwnd())
		m_baseL.SetWindowPos(NULL, m + 178, y + 3, 32, 14, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_base.GetSafeHwnd())
		m_base.SetWindowPos(NULL, m + 212, y, 104, 200, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_gen.GetSafeHwnd())
		m_gen.SetWindowPos(NULL, m + 324, y, 64, 20, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_hint.GetSafeHwnd())
		m_hint.SetWindowPos(NULL, m + 396, y + 3, max(40, cx - (m + 396) - m), 14, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + 2;

	const int btnY = cy - m - btnH;
	int viewBottom = btnY - 8;
	if (viewBottom < y + 80) viewBottom = y + 80;
	m_view.SetWindowPos(NULL, m, y, max(40, cx - 2 * m), max(40, viewBottom - y), SWP_NOZORDER | SWP_NOACTIVATE);

	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, cx - m - 80, btnY, 80, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowPos(NULL, m, btnY + 3, max(40, cx - m - 80 - 12 - m), 16, SWP_NOZORDER | SWP_NOACTIVATE);

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
}

void CSoft3DMazeDlg::FreeGrid()
{
	for (int f = 0; f < S3M_MAX_FLOORS; f++) {
		delete[] m_grids[f];
		delete[] m_visits[f];
		m_grids[f] = NULL;
		m_visits[f] = NULL;
	}
	m_grid = NULL;
	m_visit = NULL;
	m_n = 0;
	m_nFloors = 1;
	m_floor = 0;
	m_mapViewFloor = 0;
}

BOOL CSoft3DMazeDlg::AllocGrid(int n, int nFloors)
{
	if (n < S3M_MIN || n > S3M_MAX)
		return FALSE;
	if (nFloors < 1) nFloors = 1;
	if (nFloors > S3M_MAX_FLOORS) nFloors = S3M_MAX_FLOORS;
	FreeGrid();
	const size_t nn = (size_t)n * (size_t)n;
	for (int f = 0; f < nFloors; f++) {
		m_grids[f] = new BYTE[nn];
		m_visits[f] = new BYTE[nn];
		if (!m_grids[f] || !m_visits[f]) {
			FreeGrid();
			return FALSE;
		}
		memset(m_grids[f], 0, nn);
		memset(m_visits[f], 0, nn);
	}
	m_n = n;
	m_nFloors = nFloors;
	BindFloor(0);
	m_mapViewFloor = 0;
	m_mapBakeDirty = 1;
	return TRUE;
}

void CSoft3DMazeDlg::BindFloor(int f)
{
	if (f < 0) f = 0;
	if (f >= m_nFloors) f = m_nFloors - 1;
	if (f < 0 || !m_grids[f] || !m_visits[f]) {
		m_floor = 0;
		m_grid = m_grids[0];
		m_visit = m_visits[0];
		m_mapBakeDirty = 1;
		return;
	}
	m_floor = f;
	m_grid = m_grids[f];
	m_visit = m_visits[f];
	m_mapBakeDirty = 1;
}

int CSoft3DMazeDlg::ReadSizeFromUi()
{
	CString s;
	if (m_size.GetSafeHwnd())
		m_size.GetWindowText(s);
	int n = _ttoi(s);
	// コンボが空／取得失敗時は設定値→現在迷路サイズへフォールバック（クリア後に10へ落ちるのを防ぐ）
	if (n < S3M_MIN) {
		if (savedata.s3m_size >= S3M_MIN && savedata.s3m_size <= S3M_MAX)
			n = savedata.s3m_size;
		else if (m_n >= S3M_MIN && m_n <= S3M_MAX)
			n = m_n;
		else
			n = S3M_MIN;
	}
	if (n > S3M_MAX) n = S3M_MAX;
	if (n & 1) {
		n++;
		if (n > S3M_MAX) n = S3M_MAX & ~1;
	}
	return n;
}

void CSoft3DMazeDlg::SetSizeToUi(int n)
{
	if (n < S3M_MIN) n = S3M_MIN;
	if (n > S3M_MAX) n = S3M_MAX;
	if (n & 1) {
		n++;
		if (n > S3M_MAX) n = S3M_MAX & ~1;
	}
	savedata.s3m_size = n;
	CString s;
	s.Format(_T("%d"), n);
	if (!m_size.GetSafeHwnd())
		return;
	// OWNERDRAW の DROPDOWN は描画が GetWindowText 依存 → 編集欄を先に同期
	m_size.SetWindowText(s);
	// FindStringExact は物理 index。CCustomComboBox::SetCurSel は論理 index なので基底を使う
	const int found = m_size.FindStringExact(-1, s);
	m_size.CComboBox::SetCurSel(found == CB_ERR ? -1 : found);
	m_size.Invalidate(FALSE);
}

int CSoft3DMazeDlg::ReadBasementsFromUi()
{
	int b = -1;
	if (m_base.GetSafeHwnd()) {
		const int sel = m_base.GetCurSel();
		if (sel != CB_ERR) b = sel;
	}
	if (b < 0) b = savedata.s3m_basements;
	if (b < 0) b = 0;
	if (b > S3M_MAX_FLOORS - 1) b = S3M_MAX_FLOORS - 1;
	return b;
}

void CSoft3DMazeDlg::SetBasementsToUi(int b)
{
	if (b < 0) b = 0;
	if (b > S3M_MAX_FLOORS - 1) b = S3M_MAX_FLOORS - 1;
	savedata.s3m_basements = b;
	if (m_base.GetSafeHwnd())
		m_base.SetCurSel(b);
}

void CSoft3DMazeDlg::PersistUi()
{
	savedata.s3m_size = ReadSizeFromUi();
	savedata.s3m_basements = ReadBasementsFromUi();
	savedata.s3m_minimap = S3mClampMapSize(savedata.s3m_minimap);
	if (savedata.s3m_show_map != 0) savedata.s3m_show_map = 1;
	savedata.s3m_item_mask = S3mItemMask();
	savedata.s3m_bob = savedata.s3m_bob ? 1 : 0;
	if (savedata.s3m_fov < 0 || savedata.s3m_fov > 2) savedata.s3m_fov = 1;
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
		f.Write(&m_nFloors, sizeof(m_nFloors));
		f.Write(&m_floor, sizeof(m_floor));
		f.Write(&m_px, sizeof(m_px));
		f.Write(&m_pz, sizeof(m_pz));
		f.Write(&m_yaw, sizeof(m_yaw));
		f.Write(&m_won, sizeof(m_won));
		const size_t nn = (size_t)m_n * (size_t)m_n;
		for (int fl = 0; fl < m_nFloors; fl++) {
			f.Write(m_grids[fl], (UINT)nn);
			f.Write(m_visits[fl], (UINT)nn);
		}
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
	int nFloors = 1;
	int floor0 = 0;
	try {
		if (f.Read(&magic, sizeof(magic)) != sizeof(magic) || magic != S3M_RUN_MAGIC) {
			f.Close();
			return FALSE;
		}
		if (f.Read(&n, sizeof(n)) != sizeof(n) || n < S3M_MIN || n > S3M_MAX) {
			f.Close();
			return FALSE;
		}
		if (f.Read(&nFloors, sizeof(nFloors)) != sizeof(nFloors) || nFloors < 1 || nFloors > S3M_MAX_FLOORS) {
			f.Close();
			return FALSE;
		}
		if (f.Read(&floor0, sizeof(floor0)) != sizeof(floor0) || floor0 < 0 || floor0 >= nFloors) {
			f.Close();
			return FALSE;
		}
		if (!AllocGrid(n, nFloors)) {
			f.Close();
			return FALSE;
		}
		if (f.Read(&m_px, sizeof(m_px)) != sizeof(m_px)) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(&m_pz, sizeof(m_pz)) != sizeof(m_pz)) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(&m_yaw, sizeof(m_yaw)) != sizeof(m_yaw)) { FreeGrid(); f.Close(); return FALSE; }
		if (f.Read(&m_won, sizeof(m_won)) != sizeof(m_won)) { FreeGrid(); f.Close(); return FALSE; }
		const size_t nn = (size_t)n * (size_t)n;
		for (int fl = 0; fl < nFloors; fl++) {
			if (f.Read(m_grids[fl], (UINT)nn) != nn) { FreeGrid(); f.Close(); return FALSE; }
			if (f.Read(m_visits[fl], (UINT)nn) != nn) { FreeGrid(); f.Close(); return FALSE; }
		}
		BindFloor(floor0);
		m_mapViewFloor = m_floor;
		ResetFloorFx();
		m_mapBakeDirty = 1;
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
	for (int fl = 0; fl < m_nFloors; fl++) {
		for (int z = 0; z < m_n; z++) {
			for (int x = 0; x < m_n; x++) {
				const BYTE c = CellAtF(fl, x, z);
				if (c >= CELL_TEMPO && c <= CELL_EQ)
					m_itemsLeft++;
			}
		}
	}
	if (IsBlocked(m_px, m_pz)) {
		FreeGrid();
		return FALSE;
	}
	MarkVisited();
	UpdateStatus();
	SetSizeToUi(m_n);
	SetBasementsToUi(m_nFloors - 1);
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

// 階段（上／下）は通行可。壁と窓のみ不可
BOOL CSoft3DMazeDlg::IsBlockedF(int f, float x, float z) const
{
	if (m_n <= 0 || f < 0 || f >= m_nFloors || !m_grids[f]) return TRUE;
	const int ix = (int)floorf(x);
	const int iz = (int)floorf(z);
	if (ix < 0 || iz < 0 || ix >= m_n || iz >= m_n)
		return TRUE;
	const BYTE c = CellAtF(f, ix, iz);
	return (c == CELL_WALL || c == CELL_WINDOW) ? TRUE : FALSE;
}

BOOL CSoft3DMazeDlg::IsBlocked(float x, float z) const
{
	return IsBlockedF(m_floor, x, z);
}

// 偶数スロット＝壁帯（通路の1/10）、奇数＝通路帯。ワールドも同じ比率で圧縮
float CSoft3DMazeDlg::AxisSpan(int i) const
{
	return (i & 1) ? 1.f : 0.10f;
}
float CSoft3DMazeDlg::AxisOrigin(int i) const
{
	if (i <= 0) return 0.f;
	// [0,i) の偶数個=((i+1)/2)、奇数個=(i/2)
	return (float)((i + 1) / 2) * 0.10f + (float)(i / 2) * 1.f;
}
float CSoft3DMazeDlg::GridToWorldX(float gx) const
{
	if (m_n <= 0) return 0.f;
	if (gx <= 0.f) return gx * AxisSpan(0);
	if (gx >= (float)m_n) return AxisOrigin(m_n) + (gx - (float)m_n) * AxisSpan(m_n - 1);
	const int i = (int)floorf(gx);
	return AxisOrigin(i) + (gx - (float)i) * AxisSpan(i);
}
float CSoft3DMazeDlg::GridToWorldZ(float gz) const
{
	return GridToWorldX(gz); // 正方グリッド同一規則
}
int CSoft3DMazeDlg::WorldToGridAxis(float w) const
{
	if (m_n <= 0) return 0;
	if (w <= 0.f) return 0;
	int lo = 0, hi = m_n;
	while (lo < hi) {
		const int mid = (lo + hi + 1) / 2;
		if (AxisOrigin(mid) <= w) lo = mid;
		else hi = mid - 1;
	}
	if (lo >= m_n) return m_n - 1;
	return lo;
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
	ex = GridToWorldX(m_px) + m_stairShiftX;
	ez = GridToWorldZ(m_pz) + m_stairShiftZ;
}

float CSoft3DMazeDlg::GetRenderEyeY() const
{
	return .50f + (savedata.s3m_bob ? .015f * sinf(m_bob) : 0.f) + m_stairCamY;
}

int CSoft3DMazeDlg::ThemeOfFloor(int f) const
{
	if (f < 0) f = 0;
	if (f >= CS3mView::S3M_THEME_N) f = CS3mView::S3M_THEME_N - 1;
	return f;
}

void CSoft3DMazeDlg::WorldToCam(float wx, float wz, float& lx, float& lz) const
{
	float ex, ez;
	GetRenderEye(ex, ez);
	const float dx = wx - ex;
	const float dz = wz - ez;
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
		m_mapBakeDirty = 1;
	}
}

void CSoft3DMazeDlg::GenerateMaze()
{
	DWORD seed=GetTickCount()^((DWORD)ReadSizeFromUi()*2654435761u);
	if(savedata.s3m_seed)seed=(DWORD)savedata.s3m_seed;
	GenerateMazeWithSeed(seed);
}

// 1フロア分の迷路（DFS＋幅広げ＋広間）を f 面へ生成する
void CSoft3DMazeDlg::GenerateOneFloor(int f)
{
	if (m_n <= 0 || f < 0 || f >= m_nFloors || !m_grids[f])
		return;

	for (int z = 0; z < m_n; z++)
		for (int x = 0; x < m_n; x++)
			CellF(f, x, z) = CELL_WALL;

	const size_t stackCap = (size_t)((m_n + 1) / 2) * (size_t)((m_n + 1) / 2) + 8;
	int* stackX = new int[stackCap];
	int* stackY = new int[stackCap];
	if (!stackX || !stackY) {
		delete[] stackX;
		delete[] stackY;
		return;
	}

	int sp = 0;
	stackX[sp] = 1; stackY[sp] = 1; sp++;
	CellF(f, 1, 1) = CELL_FLOOR;

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
			if (nx > 0 && ny > 0 && nx < m_n - 1 && ny < m_n - 1 && CellAtF(f, nx, ny) == CELL_WALL)
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
		CellF(f, x + dx4[d] / 2, y + dy4[d] / 2) = CELL_FLOOR;
		CellF(f, nx, ny) = CELL_FLOOR;
		if (sp < (int)stackCap) {
			stackX[sp] = nx;
			stackY[sp] = ny;
			sp++;
		}
	}
	delete[] stackX;
	delete[] stackY;

	// 通路幅広げ＋広間（見た目の「壁床壁床」単調さを崩す）
	{
		auto isOpen = [&](int x, int z) -> BOOL {
			if (x < 0 || z < 0 || x >= m_n || z >= m_n) return FALSE;
			const BYTE c = CellAtF(f, x, z);
			return (c != CELL_WALL && c != CELL_WINDOW) ? TRUE : FALSE;
		};
		auto floorN = [&](int x, int z) -> int {
			int n = 0;
			static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
			for (int i = 0; i < 4; i++)
				if (isOpen(x + d[i][0], z + d[i][1])) n++;
			return n;
		};
		auto inInner = [&](int x, int z) -> BOOL {
			return (x > 0 && z > 0 && x < m_n - 1 && z < m_n - 1) ? TRUE : FALSE;
		};

		// 1) 行き止まり壁を少しだけ削る（通路をわずかに太く）
		int widen = max(6, m_n / 2);
		if (widen > 40) widen = 40;
		const int widenTries = max(widen * 48, 120);
		for (int t = 0; t < widenTries && widen > 0; t++) {
			m_rng = m_rng * 1664525u + 1013904223u;
			const int x = 1 + (int)(m_rng % (DWORD)(m_n - 2));
			m_rng = m_rng * 1664525u + 1013904223u;
			const int z = 1 + (int)(m_rng % (DWORD)(m_n - 2));
			if (!inInner(x, z) || CellAtF(f, x, z) != CELL_WALL) continue;
			if (floorN(x, z) != 1) continue;
			CellF(f, x, z) = CELL_FLOOR;
			widen--;
		}

		// 2) 矩形の広間はごく少数・小さめ（通路主体を崩しすぎない）
		int roomsLeft = max(1, m_n / 40);
		if (roomsLeft > 4) roomsLeft = 4;
		for (int attempt = 0; attempt < roomsLeft * 80 && roomsLeft > 0; attempt++) {
			m_rng = m_rng * 1664525u + 1013904223u;
			const int cx = 1 + 2 * (int)(m_rng % (DWORD)max(1, (m_n - 1) / 2));
			m_rng = m_rng * 1664525u + 1013904223u;
			const int cz = 1 + 2 * (int)(m_rng % (DWORD)max(1, (m_n - 1) / 2));
			if (!inInner(cx, cz) || !isOpen(cx, cz)) continue;
			// 半幅1固定 → 3×3 程度の小広間
			const int half = 1;
			const int x0 = max(1, cx - half), x1 = min(m_n - 2, cx + half);
			const int z0 = max(1, cz - half), z1 = min(m_n - 2, cz + half);
			int carved = 0;
			for (int z = z0; z <= z1; z++) {
				for (int x = x0; x <= x1; x++) {
					const BYTE c = CellAtF(f, x, z);
					if (c == CELL_WALL) {
						CellF(f, x, z) = CELL_FLOOR;
						carved++;
					}
				}
			}
			if (carved >= 4)
				roomsLeft--;
		}

		// 3) 広間内の完全孤立柱のみ落とす（4方向床。連鎖で開けすぎない）
		for (int z = 1; z < m_n - 1; z++) {
			for (int x = 1; x < m_n - 1; x++) {
				if (CellAtF(f, x, z) != CELL_WALL) continue;
				if (floorN(x, z) < 4) continue;
				CellF(f, x, z) = CELL_FLOOR;
			}
		}
	}
}

// スタート（地上）／ゴール（最下層）／階層をつなぐ階段を配置する
void CSoft3DMazeDlg::PlaceStairsAndGoal()
{
	if (m_n <= 0 || m_nFloors <= 0 || !m_grids[0])
		return;

	// 通路マス（奇数×奇数）のみ。壁スロットは幅1/10で飛ばす
	auto isPassCell = [&](int x, int z) -> BOOL {
		return ((x & 1) != 0 && (z & 1) != 0 && x > 0 && z > 0 && x < m_n - 1 && z < m_n - 1) ? TRUE : FALSE;
	};
	auto findPassNear = [&](int f, int preferX, int preferZ, int avoidX, int avoidZ, int& outX, int& outZ) -> BOOL {
		int bestD = 0x7fffffff;
		outX = -1; outZ = -1;
		for (int z = 1; z < m_n - 1; z += 2) {
			for (int x = 1; x < m_n - 1; x += 2) {
				if (CellAtF(f, x, z) != CELL_FLOOR) continue;
				if (x == avoidX && z == avoidZ) continue;
				const int d = abs(x - preferX) + abs(z - preferZ);
				if (d < bestD) { bestD = d; outX = x; outZ = z; }
			}
		}
		return outX >= 0;
	};

	const int basements = m_nFloors - 1;
	const int gf = m_nFloors - 1;

	int sx = 1, sz = ((m_n - 2) & 1) ? (m_n - 2) : (m_n - 3);
	if (!isPassCell(sx, sz) || CellAtF(0, sx, sz) != CELL_FLOOR) {
		if (!findPassNear(0, 1, m_n - 2, -1, -1, sx, sz)) return;
	}
	CellF(0, sx, sz) = CELL_START;

	int gx = ((m_n - 2) & 1) ? (m_n - 2) : (m_n - 3), gz = 1;
	const int avoidX = (gf == 0) ? sx : -1;
	const int avoidZ = (gf == 0) ? sz : -1;
	if (!isPassCell(gx, gz) || CellAtF(gf, gx, gz) != CELL_FLOOR || (gx == avoidX && gz == avoidZ)) {
		if (!findPassNear(gf, m_n - 2, 1, avoidX, avoidZ, gx, gz)) return;
	}
	CellF(gf, gx, gz) = CELL_GOAL;

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

	if (basements <= 0)
		return;

	// 想定経路パターン数 1+3*basements を階段本数へ配分（各リンク 2〜4 本）
	int per = (int)(pow((double)(1 + 3 * basements), 1.0 / (double)basements) + 0.5);
	if (per < 2) per = 2;
	if (per > 4) per = 4;

	const int halfSlots = max(1, (m_n - 1) / 2);
	for (int f = 0; f + 1 < m_nFloors; f++) {
		int placed = 0;
		for (int tries = 0; tries < 4000 && placed < per; tries++) {
			m_rng = m_rng * 1664525u + 1013904223u;
			const int x = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
			m_rng = m_rng * 1664525u + 1013904223u;
			const int z = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
			if (!isPassCell(x, z)) continue;
			if (CellAtF(f, x, z) != CELL_FLOOR || CellAtF(f + 1, x, z) != CELL_FLOOR) continue;
			CellF(f, x, z) = CELL_STAIRS_DOWN;
			CellF(f + 1, x, z) = CELL_STAIRS_UP;
			placed++;
		}
		// 乱数で置けなかった場合は走査して最低1本を確保（行き止まりの階層を作らない）
		for (int z = 1; z < m_n - 1 && placed < 1; z += 2) {
			for (int x = 1; x < m_n - 1 && placed < 1; x += 2) {
				if (CellAtF(f, x, z) != CELL_FLOOR || CellAtF(f + 1, x, z) != CELL_FLOOR) continue;
				CellF(f, x, z) = CELL_STAIRS_DOWN;
				CellF(f + 1, x, z) = CELL_STAIRS_UP;
				placed++;
			}
		}
	}
}

void CSoft3DMazeDlg::GenerateMazeWithSeed(DWORD seed, int forceSize)
{
	ResetClearFx();
	int n = (forceSize >= S3M_MIN) ? forceSize : ReadSizeFromUi();
	if (n < S3M_MIN) n = S3M_MIN;
	if (n > S3M_MAX) n = S3M_MAX;
	if (n & 1) {
		n++;
		if (n > S3M_MAX) n = S3M_MAX & ~1;
	}
	SetSizeToUi(n);
	const int basements = ReadBasementsFromUi();
	const int nFloors = 1 + basements;
	PersistUi();
	if (!AllocGrid(n, nFloors))
		return;
	ResetFloorFx();

	m_won = 0;
	m_itemsLeft = 0;
	m_genSeed=seed;
	m_rng=seed;

	for (int f = 0; f < m_nFloors; f++)
		GenerateOneFloor(f);
	PlaceStairsAndGoal();
	BindFloor(0);
	m_mapViewFloor = 0;

	// 窓とアイテムは地上のみ（地下は階段探索に集中させる）
	const int mask = S3mItemMask();
	if (mask & ITEM_WINDOW) {
		auto isSolidWall = [&](int x, int z) -> BOOL {
			if (x < 0 || z < 0 || x >= m_n || z >= m_n) return TRUE; // 外周は壁扱い
			const BYTE c = CellAt(x, z);
			return (c == CELL_WALL || c == CELL_WINDOW) ? TRUE : FALSE;
		};
		int winBudget = m_n / 3;
		for (int z = 1; z < m_n - 1 && winBudget > 0; z++) {
			for (int x = 1; x < m_n - 1 && winBudget > 0; x++) {
				if (CellAt(x, z) != CELL_WALL) continue;
				// 交差点の柱マス（偶×偶）には窓を置かない
				if (((x & 1) == 0) && ((z & 1) == 0)) continue;
				// 縦横どちらにも壁が続く角・T字も柱扱い
				const BOOL wallNS = isSolidWall(x, z - 1) || isSolidWall(x, z + 1);
				const BOOL wallEW = isSolidWall(x - 1, z) || isSolidWall(x + 1, z);
				if (wallNS && wallEW) continue;
				int openN = 0;
				if (!isSolidWall(x - 1, z)) openN++;
				if (!isSolidWall(x + 1, z)) openN++;
				if (!isSolidWall(x, z - 1)) openN++;
				if (!isSolidWall(x, z + 1)) openN++;
				// 廊下の薄い壁面のみ（片面〜両面）
				if (openN < 1 || openN > 2) continue;
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
	for (int tries = 0; tries < 2000 && itemBudget > 0 && nk > 0; tries++) {
		m_rng = m_rng * 1664525u + 1013904223u;
		const int x = 1 + 2 * (int)(m_rng % (DWORD)max(1, (m_n - 1) / 2));
		m_rng = m_rng * 1664525u + 1013904223u;
		const int z = 1 + 2 * (int)(m_rng % (DWORD)max(1, (m_n - 1) / 2));
		if (x <= 0 || z <= 0 || x >= m_n - 1 || z >= m_n - 1) continue;
		if (CellAt(x, z) != CELL_FLOOR) continue; // スタート／ゴール／階段は除外される
		Cell(x, z) = kinds[m_rng % (DWORD)nk];
		itemBudget--;
		m_itemsLeft++;
	}

	MarkVisited();
	m_runDirty = 1;
	PersistRun();
	UpdateStatus();
	RenderScene();
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
	} else if (c == CELL_STAIRS_DOWN) {
		if (m_clearPhase == CLEAR_IDLE && m_floorFx == FLOORFX_IDLE && m_floor + 1 < m_nFloors)
			BeginFloorChange(m_floor + 1);
	} else if (c == CELL_STAIRS_UP) {
		if (m_clearPhase == CLEAR_IDLE && m_floorFx == FLOORFX_IDLE && m_floor > 0)
			BeginFloorChange(m_floor - 1);
	} else if (c == CELL_GOAL && !m_won && m_clearPhase == CLEAR_IDLE) {
		BeginClearSequence();
		PersistRun();
	}
}

void CSoft3DMazeDlg::ResetFloorFx()
{
	m_floorFx = FLOORFX_IDLE;
	m_floorFxT = 0.f;
	m_floorTextA = 0.f;
	m_floorScreenA = 0.f;
	m_floorTextAPrev = -1.f;
	m_stairCamY = 0.f;
	m_stairShiftX = 0.f;
	m_stairShiftZ = 0.f;
	m_stairSwapDone = 0;
	m_miniFade = 1.f;
	m_miniFadeFrom = m_floor;
	m_miniFadeTo = m_floor;
	m_mapBakeDirty = 1;
}

CString CSoft3DMazeDlg::FloorLabel(int f) const
{
	if (f <= 0)
		return LL14(L"地上", L"Ground", L"Rez-de-chaussée", L"Piano terra", L"Planta baja",
			L"지상", L"地面", L"الطابق الأرضي", L"Поверхность", L"Erdgeschoss", L"Térreo", L"Begane grond", L"Parter", L"Zemin kat");
	const CString fmt = LL14(L"地下%dF", L"B%d", L"S-%d", L"S%d", L"S%d",
		L"지하%dF", L"地下%dF", L"ق%d", L"Подземный %d", L"UG%d", L"S%d", L"K%d", L"P-%d", L"B%d");
	CString s;
	s.Format(fmt, f);
	return s;
}

// 階層切替: 斜め移動で階をまたぐ。画面フェードはしない（ラベルのみ）
void CSoft3DMazeDlg::BeginFloorChange(int newFloor)
{
	if (m_n <= 0 || newFloor < 0 || newFloor >= m_nFloors || newFloor == m_floor)
		return;
	m_moving = 0;
	m_turning = 0;
	m_pxTarget = m_px;
	m_pzTarget = m_pz;
	m_stairFrom = m_floor;
	m_stairTo = newFloor;
	m_stairSwapDone = 0;
	m_stairCamY = 0.f;
	m_stairShiftX = 0.f;
	m_stairShiftZ = 0.f;
	m_miniFadeFrom = m_floor;
	m_miniFadeTo = newFloor;
	m_miniFade = 0.f;
	// BindFloor は移動の中盤で行う（描画は from/to を明示）
	m_mapViewFloor = newFloor;
	m_mapBakeDirty = 1;
	m_floorFx = FLOORFX_IN;
	m_floorFxT = 0.f;
	m_floorTextA = 0.f;
	m_floorScreenA = 0.f;
	m_floorTextAPrev = -1.f;
	m_runDirty = 1;
	RefreshFloorTex();
	UpdateStatus();
}

void CSoft3DMazeDlg::TickFloorFx(float dt)
{
	if (m_miniFade < 1.f) {
		m_miniFade += dt / 0.5f;
		if (m_miniFade > 1.f) m_miniFade = 1.f;
		m_mapBakeDirty = 1;
	}
	if (m_floorFx == FLOORFX_IDLE)
		return;

	m_floorFxT += dt;
	const float kDur = 0.92f;
	float t = m_floorFxT / kDur;
	if (t > 1.f) t = 1.f;
	const float s = t * t * (3.f - 2.f * t); // smoothstep
	const int dir = (m_stairTo > m_stairFrom) ? 1 : -1; // 下り=+1
	const float story = 1.35f;
	m_stairCamY = -dir * story * s;
	float fwx, fwz, rwx, rwz;
	CamBasisYaw(m_yaw, fwx, fwz, rwx, rwz);
	const float lean = sinf(t * (float)M_PI);
	m_stairShiftX = (fwx * 0.45f + rwx * 0.12f) * lean;
	m_stairShiftZ = (fwz * 0.45f + rwz * 0.12f) * lean;

	if (!m_stairSwapDone && t >= 0.5f) {
		BindFloor(m_stairTo);
		m_mapViewFloor = m_stairTo;
		m_stairSwapDone = 1;
		MarkVisited();
		m_runDirty = 1;
		UpdateStatus();
	}

	// ラベルのみ（画面暗転なし）
	if (t < 0.18f) m_floorTextA = t / 0.18f;
	else if (t < 0.72f) m_floorTextA = 1.f;
	else m_floorTextA = (1.f - t) / 0.28f;
	m_floorScreenA = 0.f;
	RefreshFloorTex();

	if (m_floorFxT >= kDur) {
		if (!m_stairSwapDone) {
			BindFloor(m_stairTo);
			m_mapViewFloor = m_stairTo;
			MarkVisited();
			UpdateStatus();
		}
		ResetFloorFx();
	}
}

void CSoft3DMazeDlg::RefreshFloorTex()
{
	if (!m_view.m_ready || m_floorTextA <= .01f) {
		if (m_floorFx == FLOORFX_IDLE)
			m_view.ReleaseClearTexture();
		m_floorTextAPrev = m_floorTextA;
		return;
	}
	if (fabsf(m_floorTextA - m_floorTextAPrev) < .08f && m_view.m_srvClear)
		return;
	const int labF = (m_floorFx != FLOORFX_IDLE) ? m_stairTo : m_floor;
	const CStringW label(FloorLabel(labF));
	m_view.BakeClearTexture((LPCWSTR)label, m_floorTextA);
	m_floorTextAPrev = m_floorTextA;
	m_clearTextAPrev = -1.f; // クリア表示時に必ず再ベイクさせる
}

void CSoft3DMazeDlg::OverviewFloorDelta(int d)
{
	if (d == 0 || m_nFloors <= 1 || !IsOverviewActive())
		return;
	int f = m_mapViewFloor + d;
	if (f < 0) f = 0;
	if (f >= m_nFloors) f = m_nFloors - 1;
	if (f == m_mapViewFloor)
		return;
	m_mapViewFloor = f;
	m_mapBakeDirty = 1;
}

void CSoft3DMazeDlg::ResetClearFx()
{
	m_clearPhase = CLEAR_IDLE;
	m_clearT = 0.f;
	m_clearTextA = 0.f;
	m_clearScreenA = 0.f;
	m_clearTextAPrev = -1.f;
	m_view.ReleaseClearTexture();
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
	m_clearTextAPrev = -1.f;
	m_runDirty = 1;
	RefreshClearTex();
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
			int n = m_n + 10;
			if (n > S3M_MAX) n = S3M_MAX;
			if (n & 1) {
				n++;
				if (n > S3M_MAX) n = S3M_MAX & ~1;
			}
			DWORD seed = GetTickCount() ^ ((DWORD)n * 2654435761u);
			if (savedata.s3m_seed) seed = (DWORD)savedata.s3m_seed;
			GenerateMazeWithSeed(seed, n);
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

void CSoft3DMazeDlg::RefreshClearTex()
{
	// 階層表示中は同じテクスチャを共有しているので触らない
	if(m_floorFx!=FLOORFX_IDLE||m_floorTextA>.01f)return;
	if(!m_view.m_ready||m_clearTextA<=.01f){m_view.ReleaseClearTexture();m_clearTextAPrev=m_clearTextA;return;}
	if(fabsf(m_clearTextA-m_clearTextAPrev)<.08f&&m_view.m_srvClear)return;
	const CString msg = LL14(
		L"クリア！", L"Clear!", L"Réussi !", L"Completato!", L"¡Completado!",
		L"클리어!", L"通关！", L"تم!", L"Пройдено!", L"Geschafft!",
		L"Concluído!", L"Gehaald!", L"Ukończono!", L"Temiz!");
	m_view.BakeClearTexture(msg,m_clearTextA);
	m_clearTextAPrev=m_clearTextA;
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
		CString fl;
		fl.Format(_T("  [%s]"), (LPCTSTR)FloorLabel(m_floor));
		s += fl;
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
	const int a1x = cx + gx, a1z = cz + gz;
	const int a2x = cx + gx * 2, a2z = cz + gz * 2;

	auto standable = [&](int x, int z) -> BOOL {
		if (x < 0 || z < 0 || x >= m_n || z >= m_n) return FALSE;
		const BYTE c = CellAt(x, z);
		// 壁・窓には絶対に立てない
		return (c != CELL_WALL && c != CELL_WINDOW) ? TRUE : FALSE;
	};

	int tx = -1, tz = -1;
	if ((cx & 1) && (cz & 1)) {
		// 奇数×奇数の通路: 2マス進むには「1マス先も床」が必須（壁すり抜け禁止）
		// 1マス先だけ床なら広間へ1マス
		if (standable(a1x, a1z) && standable(a2x, a2z)) {
			tx = a2x;
			tz = a2z;
		} else if (standable(a1x, a1z)) {
			tx = a1x;
			tz = a1z;
		} else {
			return FALSE;
		}
	} else {
		// 広間・壁帯上など: 1マスのみ（壁へは進めない）
		if (!standable(a1x, a1z))
			return FALSE;
		tx = a1x;
		tz = a1z;
	}

	if (!standable(tx, tz))
		return FALSE;

	m_pxTarget = (float)tx + 0.5f;
	m_pzTarget = (float)tz + 0.5f;
	m_moving = 1;
	return TRUE;
}

BOOL CSoft3DMazeDlg::ConsumeOverviewClick()
{
	if (!IsOverviewActive())
		return FALSE;
	// トグル表示はクリックで閉じる（SPACE押しっぱなし中はSPACE側が残る）
	if (m_mapToggle)
		m_mapToggle = 0;
	return TRUE;
}

void CSoft3DMazeDlg::TickMove(float dt)
{
	if (m_n <= 0) return;
	m_anim += dt;
	TickFloorFx(dt);
	if (m_clearPhase != CLEAR_IDLE) {
		if(savedata.s3m_bob)m_bob += dt * 3.f;
		return;
	}
	// 階層切替の演出中は移動・旋回しない
	if (m_floorFx != FLOORFX_IDLE) {
		m_moveHeld = 1;
		m_turnHeld = 1;
		if (savedata.s3m_bob) m_bob += dt * 2.f;
		return;
	}
	// SPACE 押下中／全体マップ表示中は確認のみ（移動・旋回しない）。←→ / A D で表示階層を切替
	if (IsOverviewActive()) {
		m_moveHeld = 1;
		m_turnHeld = 1;
		const bool fl = (GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000);
		const bool fr = (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000);
		int floorReq = 0;
		if (fl && !fr) floorReq = -1;
		else if (fr && !fl) floorReq = 1;
		if (floorReq == 0)
			m_overviewFloorHeld = 0;
		else if (!m_overviewFloorHeld) {
			OverviewFloorDelta(floorReq);
			m_overviewFloorHeld = 1;
		}
		if (savedata.s3m_bob) m_bob += dt * 2.f;
		return;
	}
	m_overviewFloorHeld = 0;
	if (m_mapViewFloor != m_floor) {
		m_mapViewFloor = m_floor;
		m_mapBakeDirty = 1;
	}

	const bool turnL = (GetAsyncKeyState('Q') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000);
	const bool turnR = (GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000);
	int turnReq = 0;
	if (turnL && !turnR) turnReq = -1;
	else if (turnR && !turnL) turnReq = 1;

	// 押下エッジのみ（押しっぱなしで連続旋回しない）
	if (turnReq == 0) {
		m_turnHeld = 0;
	} else if (!m_turning && !m_moving && !m_turnHeld) {
		if (TryTurn(turnReq))
			m_turnHeld = 1;
	}

	if (m_turning) {
		const float d = S3mAngleDelta(m_yaw, m_yawTarget);
		// 約 0.22s で 90°（数フレームのスムーズ旋回）
		const float step = ((float)(M_PI * 0.5) / 0.22f) * dt;
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

	// 押下エッジで 1 マスだけ進む（押しっぱなし／到着フレームの連続移動なし）
	if (mx == 0 && mz == 0) {
		m_moveHeld = 0;
	} else if (!m_moveHeld && !m_moving && !m_turning) {
		if (TryStep(mx, mz))
			m_moveHeld = 1;
		else
			m_moveHeld = 1; // 壁でもキーを離すまで再試行しない
	}

	if (m_moving) {
		const float dx = m_pxTarget - m_px;
		const float dz = m_pzTarget - m_pz;
		const float dist = sqrtf(dx * dx + dz * dz);
		// 2マス移動を約 0.22s
		const float spd = 2.f / 0.22f;
		const float step = spd * dt;
		if (dist <= step || dist < 1e-4f) {
			const int ox = (int)floorf(m_px), oz = (int)floorf(m_pz);
			m_px = m_pxTarget;
			m_pz = m_pzTarget;
			// 壁・窓の上には絶対に止まらない
			if (IsBlocked(m_px, m_pz)) {
				m_px = (float)ox + 0.5f;
				m_pz = (float)oz + 0.5f;
				if (IsBlocked(m_px, m_pz)) {
					m_px = 1.5f;
					m_pz = 1.5f;
				}
				m_pxTarget = m_px;
				m_pzTarget = m_pz;
			}
			m_moving = 0;
			MarkVisited();
			{
				const int nx = (int)floorf(m_px), nz = (int)floorf(m_pz);
				const int mx = (ox + nx) / 2, mz = (oz + nz) / 2;
				if (mx >= 0 && mz >= 0 && mx < m_n && mz < m_n && !VisitAt(mx, mz)
					&& !IsBlocked((float)mx + 0.5f, (float)mz + 0.5f)) {
					Visit(mx, mz) = 1;
					m_runDirty = 1;
					m_mapBakeDirty = 1;
				}
			}
			TryPickup();
			m_runDirty = 1;
		} else {
			m_px += dx / dist * step;
			m_pz += dz / dist * step;
			// 壁セルを通過中なら目標へ飛ばす（壁の上に留まらない）
			if (IsBlocked(m_px, m_pz) && !IsBlocked(m_pxTarget, m_pzTarget)) {
				m_px = m_pxTarget;
				m_pz = m_pzTarget;
			}
			if(savedata.s3m_bob)m_bob += dt * 18.f;
			m_runDirty = 1;
		}
	} else {
		if(savedata.s3m_bob)m_bob += dt * (m_won ? 4.f : 2.f);
	}
}

void CSoft3DMazeDlg::RenderScene()
{
	if(!m_view.m_ready||m_n<=0||!m_grid)return;
	ID3D11DeviceContext* dc=m_view.m_imm;const int w=m_view.m_vw,h=m_view.m_vh;if(w<8||h<8)return;
	RefreshClearTex();
	float ex,ez;GetRenderEye(ex,ez);float fx,fz,rx,rz;CamBasisYaw(m_yaw,fx,fz,rx,rz);
	const float eyeY=GetRenderEyeY();
	const float fov=(savedata.s3m_fov==0?55.f:(savedata.s3m_fov==2?90.f:70.f))*(float)(M_PI/180.0);
	const float zNear=.05f,zFar=80.f;
	S3MFrameCB cb={};cb.viewProj=S3mMatMul(S3mLookAt(ex,eyeY,ez,ex+fx,eyeY,ez+fz,0,1,0),S3mPerspective(fov,(float)w/(float)h,zNear,zFar));
	float lx=-.35f,ly=.88f,lz=-.28f;float llen=sqrtf(lx*lx+ly*ly+lz*lz);lx/=llen;ly/=llen;lz/=llen;cb.lightDir={lx,ly,lz,0};
	const float lDist=24.f;cb.lightVP=S3mMatMul(S3mLookAt(ex+lx*lDist,16.f,ez+lz*lDist,ex,0.f,ez,0,1,0),S3mOrtho(-18.f,18.f,-18.f,18.f,1.f,55.f));
	cb.eyePos={ex,eyeY,ez,1};cb.fogParams={18.f,48.f,.02f,-.5f};cb.dofParams={.55f,.42f,.8f,m_bob};cb.screenSize={(float)w,(float)h,1.f/w,1.f/h};cb.misc={m_clearScreenA,m_clearTextA,1.f/tanf(fov*.5f),(float)savedata.s3m_bob};
	D3D11_MAPPED_SUBRESOURCE map={};if(FAILED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map)))return;memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);
	const S3MMat vpMat=cb.viewProj;
	const UINT maxV=m_view.m_vbDynBytes/sizeof(S3MVertex);S3MVertex* v=new S3MVertex[maxV];UINT floorBeg=0,nFloor=0,wallBeg=0,nWall=0,transBeg=0,nTrans=0;int phase=0;
	auto put=[&](float x,float y,float z,float nx,float ny,float nz,float u,float vv,float r,float g,float b,float a)->BOOL{UINT n=nFloor+nWall+nTrans;if(n>=maxV)return FALSE;v[n]={x,y,z,nx,ny,nz,u,vv,r,g,b,a};if(phase==2)nTrans++;else if(phase==1)nWall++;else nFloor++;return TRUE;};
	auto tri=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float u2,float v2,float r,float g,float b,float a){put(x0,y0,z0,nx,ny,nz,u0,v0,r,g,b,a);put(x1,y1,z1,nx,ny,nz,u1,v1,r,g,b,a);put(x2,y2,z2,nx,ny,nz,u2,v2,r,g,b,a);};
	auto quadUV=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
		tri(x0,y0,z0,x1,y1,z1,x2,y2,z2,nx,ny,nz,u0,v1,u1,v1,u1,v0,r,g,b,a);tri(x0,y0,z0,x2,y2,z2,x3,y3,z3,nx,ny,nz,u0,v1,u1,v0,u0,v0,r,g,b,a);};
	auto quad=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float r,float g,float b,float a){
		// 背面は出さない（カメラ向きの面だけ）
		const float cx=(x0+x1+x2+x3)*.25f,cy=(y0+y1+y2+y3)*.25f,cz=(z0+z1+z2+z3)*.25f;
		if(nx*(ex-cx)+ny*(eyeY-cy)+nz*(ez-cz)<=0.f)return;
		quadUV(x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3,nx,ny,nz,0,0,1,1,r,g,b,a);
	};
	auto patch1=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
		put(x0,y0,z0,nx,ny,nz,u0,v1,r,g,b,a);put(x1,y1,z1,nx,ny,nz,u1,v1,r,g,b,a);put(x2,y2,z2,nx,ny,nz,u1,v0,r,g,b,a);put(x3,y3,z3,nx,ny,nz,u0,v0,r,g,b,a);};
	auto patch=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
		const float cx=(x0+x1+x2+x3)*.25f,cy=(y0+y1+y2+y3)*.25f,cz=(z0+z1+z2+z3)*.25f;
		if(nx*(ex-cx)+ny*(eyeY-cy)+nz*(ez-cz)<=0.f)return; // 背面カリング
		// CPU 細分化は近い壁だけ（遠い壁まで N=3 だと大マップで頂点バッファが床・壁で溢れる）
		const float dx=cx-ex,dz=cz-ez;const float dist2=dx*dx+dz*dz;
		const int N=(dist2>14.f*14.f)?1:((dist2>7.f*7.f)?2:3);
		for(int j=0;j<N;j++)for(int i=0;i<N;i++){
			float su0=(float)i/N,su1=(float)(i+1)/N,sv0=(float)j/N,sv1=(float)(j+1)/N;
			auto L=[&](float u,float vv,float& X,float& Y,float& Z){float ax=x0+(x1-x0)*u,ay=y0+(y1-y0)*u,az=z0+(z1-z0)*u;float bx=x3+(x2-x3)*u,by=y3+(y2-y3)*u,bz=z3+(z2-z3)*u;X=ax+(bx-ax)*vv;Y=ay+(by-ay)*vv;Z=az+(bz-az)*vv;};
			float p00x,p00y,p00z,p10x,p10y,p10z,p11x,p11y,p11z,p01x,p01y,p01z;L(su0,sv0,p00x,p00y,p00z);L(su1,sv0,p10x,p10y,p10z);L(su1,sv1,p11x,p11y,p11z);L(su0,sv1,p01x,p01y,p01z);
			float uu0=u0+(u1-u0)*su0,uu1=u0+(u1-u0)*su1,vv0=v0+(v1-v0)*sv0,vv1=v0+(v1-v0)*sv1;
			patch1(p00x,p00y,p00z,p10x,p10y,p10z,p11x,p11y,p11z,p01x,p01y,p01z,nx,ny,nz,uu0,vv0,uu1,vv1,r,g,b,a);
		}
	};
	// 通路立方体（低い箱）— 階段など厚みが必要なとき用
	auto passCube=[&](float x0,float z0,float x1,float z1,float y0,float y1,float r,float g,float b,float a){
		quad(x0,y1,z0,x1,y1,z0,x1,y1,z1,x0,y1,z1,0,1,0,r,g,b,a);
		quad(x0,y0,z1,x1,y0,z1,x1,y0,z0,x0,y0,z0,0,-1,0,r*.7f,g*.7f,b*.7f,a);
		quad(x0,y0,z0,x0,y0,z1,x0,y1,z1,x0,y1,z0,-1,0,0,r,g,b,a);
		quad(x1,y0,z1,x1,y0,z0,x1,y1,z0,x1,y1,z1,1,0,0,r,g,b,a);
		quad(x0,y0,z0,x1,y0,z0,x1,y1,z0,x0,y1,z0,0,0,-1,r,g,b,a);
		quad(x1,y0,z1,x0,y0,z1,x0,y1,z1,x1,y1,z1,0,0,1,r,g,b,a);
	};
	const float wallH=1.20f,passH=0.08f,drawDist=28.f,rad=drawDist+2.f;
	// 遠クリップ相当までNDCでは落とさないが、頂点溢れ防止のため走査半径は drawDist に制限
	const int ix0=max(0,WorldToGridAxis(ex-rad)-1),ix1=min(m_n-1,WorldToGridAxis(ex+rad)+1);
	const int iz0=max(0,WorldToGridAxis(ez-rad)-1),iz1=min(m_n-1,WorldToGridAxis(ez+rad)+1);
	auto cellX0=[&](int x)->float{return AxisOrigin(x);};
	auto cellZ0=[&](int z)->float{return AxisOrigin(z);};
	auto cellW=[&](int x)->float{return AxisSpan(x);};
	auto cellD=[&](int z)->float{return AxisSpan(z);};
	auto cellCX=[&](int x)->float{return AxisOrigin(x)+AxisSpan(x)*.5f;};
	auto cellCZ=[&](int z)->float{return AxisOrigin(z)+AxisSpan(z)*.5f;};
	// NDC: 画面外XYは描画しない。カメラ後方は除外。奥行きは drawDist まで（それ以上は頂点バッファが床で埋まる）
	auto projectNdc=[&](float wx,float wy,float wz,float& nx,float& ny,float& nz,float& cw)->BOOL{
		const float cx=wx*vpMat.m[0]+wy*vpMat.m[4]+wz*vpMat.m[8]+vpMat.m[12];
		const float cy=wx*vpMat.m[1]+wy*vpMat.m[5]+wz*vpMat.m[9]+vpMat.m[13];
		const float cz=wx*vpMat.m[2]+wy*vpMat.m[6]+wz*vpMat.m[10]+vpMat.m[14];
		cw=wx*vpMat.m[3]+wy*vpMat.m[7]+wz*vpMat.m[11]+vpMat.m[15];
		if(cw<=1e-4f)return FALSE;
		const float iw=1.f/cw;nx=cx*iw;ny=cy*iw;nz=cz*iw;return TRUE;
	};
	auto vis=[&](int x,int z)->BOOL{
		const float cxw=cellCX(x),czw=cellCZ(z);
		const float dx=cxw-ex,dz=czw-ez;
		const float lzv=dx*fx+dz*fz;
		if(lzv<-3.f||lzv>drawDist)return FALSE;
		const float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
		const float ys[2]={0.f,wallH};
		const float xs[2]={x0,x1},zs[2]={z0,z1};
		float minX=1e9f,maxX=-1e9f,minY=1e9f,maxY=-1e9f;
		int inFront=0;
		for(int iy=0;iy<2;iy++)for(int ix=0;ix<2;ix++)for(int iz=0;iz<2;iz++){
			float nx,ny,nz,cw;
			if(!projectNdc(xs[ix],ys[iy],zs[iz],nx,ny,nz,cw))continue;
			inFront++;
			if(nx<minX)minX=nx;if(nx>maxX)maxX=nx;
			if(ny<minY)minY=ny;if(ny>maxY)maxY=ny;
		}
		if(inFront==0)return FALSE;
		const float m=.20f;
		if(maxX<-1.f-m||minX>1.f+m||maxY<-1.f-m||minY>1.f+m)return FALSE;
		return TRUE;
	};
	auto wallVid=[&](int x,int z)->int{return ((x*17+z*31+x*z)&15);};
	auto atlasUV=[&](int vid,float& u0,float& v0,float& u1,float& v1){u0=(float)(vid&3)*.25f;v0=(float)(vid>>2)*.25f;u1=u0+.25f;v1=v0+.25f;};
	auto isMirrorWall=[&](int x,int z)->BOOL{return ((x*13+z*29)&7)==0;};
	auto isMirrorFloor=[&](int x,int z)->BOOL{return ((x+z*3)&5)==0;};
	struct MirPick{float score;float nx,ny,nz,px,py,pz;};
	MirPick bestW={-1.f,0,0,0,0,0,0},bestF={-1.f,0,0,0,0,0,0};
	const float storyH=1.35f;
	const BOOL stairMove=(m_floorFx!=FLOORFX_IDLE);
	const int stairDir=stairMove?((m_stairTo>m_stairFrom)?1:-1):0;
	struct Layer{UINT fBeg,nF,wBeg,nW;int th;};
	Layer layers[4];int nLay=0;
	auto themeFloorRGB=[&](int th,float k,float& r,float& g,float& b){
		if(th<=0){r=.72f*k;g=.58f*k;b=.42f*k;}
		else if(th==1){r=.48f*k;g=.58f*k;b=.68f*k;}
		else if(th==2){r=.55f*k;g=.45f*k;b=.38f*k;}
		else{r=.38f*k;g=.30f*k;b=.32f*k;}
	};
	// 指定階の壁を先に、床・天井を後に積む（頂点溢れ時も壁が残る）
	auto emitLayer=[&](int f,float yBias,int ax0,int ax1,int az0,int az1,BOOL fullVis){
		if(f<0||f>=m_nFloors||!m_grids[f]||nLay>=4)return;
		Layer& L=layers[nLay];
		L.th=ThemeOfFloor(f);
		L.wBeg=nFloor+nWall+nTrans;
		phase=1;
		UINT w0=nWall;
		for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
			BYTE c=CellAtF(f,x,z);if(c!=CELL_WALL)continue;
			if(fullVis&&!vis(x,z))continue;
			const int vid=wallVid(x,z);float u0,v0,u1,v1;atlasUV(vid,u0,v0,u1,v1);
			float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z),cx=cellCX(x),cz=cellCZ(z);
			float y0=yBias,y1=yBias+wallH;
			float rr=1,gg=1,bb=1,aa=1;if(isMirrorWall(x,z)&&fabsf(yBias)<.01f){
				rr=.75f;gg=.82f;bb=.92f;aa=1.15f;
				float mnx=0,mny=0,mnz=0,mpx=cx,mpy=y0+wallH*.45f,mpz=cz;
				if(cellW(x)<=cellD(z)+1e-4f){mnx=(ex>=cx)?1.f:-1.f;mpx=(mnx>0)?x1:x0;}
				else{mnz=(ez>=cz)?1.f:-1.f;mpz=(mnz>0)?z1:z0;}
				float tox=ex-mpx,toy=eyeY-mpy,toz=ez-mpz;float facing=mnx*tox+mny*toy+mnz*toz;
				if(facing>.05f){float dist=sqrtf(tox*tox+toy*toy+toz*toz)+.01f;float sc=facing/dist;if(sc>bestW.score)bestW={sc,mnx,mny,mnz,mpx,mpy,mpz};}
			}
			auto face=[&](float ax0,float ay0,float az0,float ax1,float ay1,float az1,float ax2,float ay2,float az2,float ax3,float ay3,float az3,float nx,float ny,float nz){
				patch(ax0,ay0,az0,ax1,ay1,az1,ax2,ay2,az2,ax3,ay3,az3,nx,ny,nz,u0,v0,u1,v1,rr,gg,bb,aa);
			};
			face(x0,y0,z0,x0,y0,z1,x0,y1,z1,x0,y1,z0,-1,0,0);
			face(x1,y0,z1,x1,y0,z0,x1,y1,z0,x1,y1,z1,1,0,0);
			face(x1,y0,z0,x0,y0,z0,x0,y1,z0,x1,y1,z0,0,0,-1);
			face(x0,y0,z1,x1,y0,z1,x1,y1,z1,x0,y1,z1,0,0,1);
			face(x0,y1,z0,x1,y1,z0,x1,y1,z1,x0,y1,z1,0,1,0);
		}
		L.nW=nWall-w0;
		L.fBeg=nFloor+nWall+nTrans;
		phase=0;
		UINT f0=nFloor;
		for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
			BYTE c=CellAtF(f,x,z);if(c==CELL_WALL||c==CELL_WINDOW)continue;
			if(fullVis&&!vis(x,z))continue;
			float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
			// 床上面のみ（側面立方体は頂点を食い過ぎる）
			if(c!=CELL_STAIRS_DOWN&&c!=CELL_STAIRS_UP){
				float k=VisitAtF(f,x,z)?1.f:.82f;float r,g,b;themeFloorRGB(L.th,k,r,g,b);
				float a=isMirrorFloor(x,z)?1.12f:1.f;
				const float y1=yBias+passH;
				quad(x0,y1,z0,x1,y1,z0,x1,y1,z1,x0,y1,z1,0,1,0,r,g,b,a);
			}
			// 地下の天井（上り階段の穴だけ開ける）
			if(f>0&&c!=CELL_STAIRS_UP){
				float cr,cg,cb;themeFloorRGB(L.th,.42f,cr,cg,cb);
				const float yc=yBias+wallH;
				quad(x0,yc,z0,x0,yc,z1,x1,yc,z1,x1,yc,z0,0,-1,0,cr,cg,cb,1.f);
			}
		}
		L.nF=nFloor-f0;
		nLay++;
	};

	if(stairMove){
		// 移動中: from を Y=0、to を -dir*storyH（カメラが斜めに降りる／昇る）
		emitLayer(m_stairFrom,0.f,ix0,ix1,iz0,iz1,TRUE);
		emitLayer(m_stairTo,-(float)stairDir*storyH,ix0,ix1,iz0,iz1,TRUE);
	}else{
		emitLayer(m_floor,0.f,ix0,ix1,iz0,iz1,TRUE);
		// 階段穴から隣接階を垣間見る
		const int peekR=3;
		int seenU=-1,seenD=-1;
		for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){
			if(!vis(x,z))continue;
			BYTE c=CellAt(x,z);
			if(c==CELL_STAIRS_DOWN&&m_floor+1<m_nFloors&&seenD!=m_floor+1){
				seenD=m_floor+1;
				emitLayer(m_floor+1,-storyH,max(0,x-peekR),min(m_n-1,x+peekR),max(0,z-peekR),min(m_n-1,z+peekR),FALSE);
			}else if(c==CELL_STAIRS_UP&&m_floor>0&&seenU!=m_floor-1){
				seenU=m_floor-1;
				emitLayer(m_floor-1,storyH,max(0,x-peekR),min(m_n-1,x+peekR),max(0,z-peekR),min(m_n-1,z+peekR),FALSE);
			}
		}
	}
	const UINT mainFloorN=layers[0].nF,mainWallBeg=layers[0].wBeg,mainWallN=layers[0].nW; // 互換用（影など）
	(void)mainFloorN;(void)mainWallBeg;(void)mainWallN;
	struct XL{float d;int x,z;BYTE c;};XL xl[1536];int nc=0;
	const int fxFloor=stairMove?m_stairFrom:m_floor;
	for(int z=iz0;z<=iz1&&nc<1536;z++)for(int x=ix0;x<=ix1&&nc<1536;x++){BYTE c=CellAtF(fxFloor,x,z);if(!vis(x,z))continue;
		if(c==CELL_WINDOW||c==CELL_GOAL||c==CELL_START||c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP||(c>=CELL_TEMPO&&c<=CELL_EQ)){
			float dx=cellCX(x)-ex,dz=cellCZ(z)-ez;xl[nc++]={dx*fx+dz*fz,x,z,c};}}
	for(int i=1;i<nc;i++){XL q=xl[i];int j=i-1;while(j>=0&&xl[j].d<q.d){xl[j+1]=xl[j];j--;}xl[j+1]=q;}
	transBeg=nFloor+nWall;phase=2;
	auto spinOcta=[&](float cx,float cy,float cz,float s,float ang,float rr,float gg,float bb,float a){
		float cs=cosf(ang),sn=sinf(ang);float ox[4],oz[4];
		auto R=[&](float px,float pz,int i){ox[i]=cx+px*cs-pz*sn;oz[i]=cz+px*sn+pz*cs;};
		R(s,0,0);R(0,s,1);R(-s,0,2);R(0,-s,3);float top=cy+s*1.15f,bot=cy-s*.95f;
		auto face=[&](float ax,float ay,float az,float bx,float by,float bz,float cx2,float cy2,float cz2){
			float nx=(by-ay)*(cz2-az)-(bz-az)*(cy2-ay),ny=(bz-az)*(cx2-ax)-(bx-ax)*(cz2-az),nz=(bx-ax)*(cy2-ay)-(by-ay)*(cx2-ax);float nl=sqrtf(nx*nx+ny*ny+nz*nz)+1e-6f;nx/=nl;ny/=nl;nz/=nl;
			tri(ax,ay,az,bx,by,bz,cx2,cy2,cz2,nx,ny,nz,0,0,1,0,1,1,rr,gg,bb,a);};
		for(int i=0;i<4;i++){int j=(i+1)&3;face(ox[i],cy,oz[i],ox[j],cy,oz[j],cx,top,cz);face(ox[j],cy,oz[j],ox[i],cy,oz[i],cx,bot,cz);}
	};
	struct FxRec{UINT beg,n;float cx,cy,cz,d;};FxRec fxObj[48];int nFx=0;int fxMirOf[48];
	for(int i=0;i<48;i++)fxMirOf[i]=-1;
	UINT winBeg=nFloor+nWall,nWin=0;
	for(int i=0;i<nc;i++){int x=xl[i].x,z=xl[i].z;BYTE c=xl[i].c;float ocx=cellCX(x),ocz=cellCZ(z),x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
		if(c==CELL_WINDOW){
			float rr=.35f,gg=.78f,bb=.95f,a=.38f;
			quad(x0,.02f,z0,x1,.02f,z0,x1,wallH-.02f,z0,x0,wallH-.02f,z0,0,0,-1,rr,gg,bb,a);
			quad(x1,.02f,z1,x0,.02f,z1,x0,wallH-.02f,z1,x1,wallH-.02f,z1,0,0,1,rr,gg,bb,a);
			quad(x0,.02f,z1,x0,.02f,z0,x0,wallH-.02f,z0,x0,wallH-.02f,z1,-1,0,0,rr,gg,bb,a);
			quad(x1,.02f,z0,x1,.02f,z1,x1,wallH-.02f,z1,x1,wallH-.02f,z0,1,0,0,rr,gg,bb,a);
			float fr=.55f,fg=.52f,fb=.48f,fa=.92f,t=.018f;
			if(cellW(x)<cellD(z)){
				quad(x0,0,z0,x1,0,z0,x1,t,z0,x0,t,z0,0,0,-1,fr,fg,fb,fa);
				quad(x0,wallH-t,z0,x1,wallH-t,z0,x1,wallH,z0,x0,wallH,z0,0,0,-1,fr,fg,fb,fa);
				quad(x1,0,z1,x0,0,z1,x0,t,z1,x1,t,z1,0,0,1,fr,fg,fb,fa);
				quad(x1,wallH-t,z1,x0,wallH-t,z1,x0,wallH,z1,x1,wallH,z1,0,0,1,fr,fg,fb,fa);
			}else{
				quad(x0,0,z0,x0,0,z1,x0,t,z1,x0,t,z0,-1,0,0,fr,fg,fb,fa);
				quad(x0,wallH-t,z0,x0,wallH-t,z1,x0,wallH,z1,x0,wallH,z0,-1,0,0,fr,fg,fb,fa);
				quad(x1,0,z1,x1,0,z0,x1,t,z0,x1,t,z1,1,0,0,fr,fg,fb,fa);
				quad(x1,wallH-t,z1,x1,wallH-t,z0,x1,wallH,z0,x1,wallH,z1,1,0,0,fr,fg,fb,fa);
			}
		}else if(c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP){
			if(nFx>=48)continue;
			// 斜め段：穴へ続くステップ（下りは下へ／上りは上へ）
			const UINT b=nFloor+nWall+nTrans;const BOOL dn=(c==CELL_STAIRS_DOWN);
			const float rr=dn?1.f:.22f,gg=dn?.58f:.86f,bb=dn?.16f:1.f;
			const float cw=x1-x0,cd=z1-z0;
			for(int st=0;st<5;st++){
				const float t0=(float)st/5.f,t1=(float)(st+1)/5.f;
				const float in0=.06f+.14f*t0,in1=.06f+.14f*t1;
				const float yTop=dn?(passH-t0*storyH*.9f):(passH+t0*storyH*.9f);
				const float yBot=dn?(passH-t1*storyH*.9f):(passH+t1*storyH*.9f);
				const float ya=min(yTop,yBot),yb=max(yTop,yBot);
				passCube(x0+cw*in0,z0+cd*in0,x1-cw*in0,z1-cd*in0,ya,yb+.02f,rr,gg,bb,1.08f);
			}
			spinOcta(ocx,passH+(dn?-.15f:.55f)+.04f*sinf(m_anim*2.2f+(float)x),ocz,.12f,m_anim*(dn?1.9f:-1.9f),rr,gg,bb,1.18f);
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,passH+.2f,ocz,xl[i].d};
		}else if(nFx<48){
			const UINT b=nFloor+nWall+nTrans;float cy,s,ang,rr,gg,bb;
			if(c==CELL_GOAL){cy=.48f+.05f*sinf(m_anim*2.6f);s=.30f;ang=m_anim*2.1f;rr=1.f;gg=.88f;bb=.25f;}
			else if(c==CELL_START){cy=.42f+.04f*sinf(m_anim*2.1f+1.f);s=.24f;ang=-m_anim*1.7f;rr=.30f;gg=1.f;bb=.55f;}
			else{cy=.40f+.07f*sinf(m_anim*2.4f+x*.7f+z);s=.17f;ang=m_anim*(1.8f+(c&3)*.3f)+x;rr=1;gg=.5f;bb=.8f;
				if(c==CELL_TEMPO){rr=.3f;gg=1;bb=.5f;}else if(c==CELL_PITCH_UP){rr=1;gg=.65f;bb=.25f;}else if(c==CELL_PITCH_DN){rr=.35f;gg=.55f;bb=1;}else if(c==CELL_NEXT){rr=1;gg=.2f;bb=.35f;}else if(c==CELL_EQ){rr=.7f;gg=.35f;bb=1;}}
			spinOcta(ocx,cy,ocz,s,ang,rr,gg,bb,1.20f);
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,cy,ocz,xl[i].d};
		}
	}
	nWin=(nFloor+nWall+nTrans)-winBeg;
	{int ord[48];for(int i=0;i<nFx;i++)ord[i]=i;
	for(int i=1;i<nFx;i++){int q=ord[i],j=i-1;while(j>=0&&fxObj[ord[j]].d>fxObj[q].d){ord[j+1]=ord[j];j--;}ord[j+1]=q;}
	const int nUse=min((int)CS3mView::S3M_MIRROR_FX_N,nFx);
	for(int i=0;i<nUse;i++)fxMirOf[ord[i]]=CS3mView::S3M_MIRROR_FX0+i;}
	UINT plateBeg=nFloor+nWall+nTrans;
	// 訪問床：青の半透明板＋鏡床（現在／移動元の階）
	for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){
		BYTE c=CellAtF(fxFloor,x,z);if(c==CELL_WALL||c==CELL_WINDOW||c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP||!vis(x,z))continue;
		float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
		if(VisitAtF(fxFloor,x,z))quad(x0,passH+.01f,z0,x0,passH+.01f,z1,x1,passH+.01f,z1,x1,passH+.01f,z0,0,1,0,.25f,.55f,1.f,.35f);
		if(isMirrorFloor(x,z)){
			quad(x0,passH+.02f,z0,x0,passH+.02f,z1,x1,passH+.02f,z1,x1,passH+.02f,z0,0,1,0,.55f,.62f,.72f,1.15f);
			float mpx=cellCX(x),mpy=passH+.02f,mpz=cellCZ(z);float tox=ex-mpx,toy=eyeY-mpy,toz=ez-mpz;
			if(toy>.05f){float dist=sqrtf(tox*tox+toy*toy+toz*toz)+.01f;float sc=toy/dist;if(sc>bestF.score)bestF={sc,0.f,1.f,0.f,mpx,mpy,mpz};}
		}
	}
	UINT nPlate=(nFloor+nWall+nTrans)-plateBeg;
	if(FAILED(dc->Map(m_view.m_vbDyn,0,D3D11_MAP_WRITE_DISCARD,0,&map))){delete[] v;return;}memcpy(map.pData,v,(nFloor+nWall+nTrans)*sizeof(S3MVertex));dc->Unmap(m_view.m_vbDyn,0);delete[] v;
	UINT stride=sizeof(S3MVertex),off=0;ID3D11ShaderResourceView* ns[7]={NULL,NULL,NULL,NULL,NULL,NULL,NULL};ID3D11RenderTargetView* nullRtv=NULL;
	auto bindCB=[&](){dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->HSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->DSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);};
	auto drawFloorWall=[&](BOOL colorPass){
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);dc->DSSetSamplers(0,1,&m_view.m_sampLin);dc->OMSetDepthStencilState(m_view.m_dssWrite,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);
		if(colorPass){dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);dc->PSSetShaderResources(3,1,&m_view.m_srvEnv);}
		for(int li=0;li<nLay;li++){
			const Layer& L=layers[li];
			dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
			dc->PSSetShaderResources(0,1,&m_view.m_srvFloor[L.th]);if(L.nF)dc->Draw(L.nF,L.fBeg);
			dc->IASetInputLayout(m_view.m_ilPatch);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
			dc->VSSetShader(m_view.m_vsTess,NULL,0);dc->HSSetShader(m_view.m_hsTess,NULL,0);dc->DSSetShader(m_view.m_dsTess,NULL,0);dc->PSSetShader(colorPass?m_view.m_psWall:NULL,NULL,0);
			dc->DSSetShaderResources(0,1,&m_view.m_srvBrick[L.th]);dc->PSSetShaderResources(0,1,&m_view.m_srvBrick[L.th]);if(L.nW)dc->Draw(L.nW,L.wBeg);
		}
		dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->DSSetShaderResources(0,1,ns);dc->PSSetShaderResources(0,5,ns);
	};
	auto drawTrans=[&](BOOL colorPass){
		if(!nTrans)return;
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		if(colorPass){dc->OMSetDepthStencilState(m_view.m_dssRead,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&m_view.m_srvEnv);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);}
		dc->Draw(nTrans,transBeg);dc->PSSetShaderResources(0,5,ns);
	};
	auto drawTransRange=[&](UINT beg,UINT n,BOOL colorPass){
		if(!n)return;
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		if(colorPass){dc->OMSetDepthStencilState(m_view.m_dssRead,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&m_view.m_srvEnv);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);}
		dc->Draw(n,beg);
	};
	S3MMat fxRefVP[CS3mView::S3M_MIRROR_FX_N];
	{S3MMat camVP=cb.viewProj;cb.viewProj=cb.lightVP;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
	D3D11_VIEWPORT svp={0,0,(float)CS3mView::S3M_SHADOW_SIZE,(float)CS3mView::S3M_SHADOW_SIZE,0,1};dc->RSSetViewports(1,&svp);dc->RSSetState(m_view.m_rsShadow);
	dc->OMSetRenderTargets(1,&nullRtv,m_view.m_shadowDsv);dc->ClearDepthStencilView(m_view.m_shadowDsv,D3D11_CLEAR_DEPTH,1.f,0);
	drawFloorWall(FALSE);dc->OMSetDepthStencilState(m_view.m_dssWrite,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);drawTrans(FALSE);
	dc->OMSetRenderTargets(1,&nullRtv,NULL);cb.viewProj=camVP;
	S3MMat idM={};idM.m[0]=idM.m[5]=idM.m[10]=idM.m[15]=1.f;cb.reflectVP=idM;cb.reflectFloorVP=idM;
	for(int i=0;i<CS3mView::S3M_MIRROR_FX_N;i++)fxRefVP[i]=idM;
	auto makeReflectVP=[&](const MirPick& m, float fovMul)->S3MMat{
		const float sd=m.nx*ex+m.ny*eyeY+m.nz*ez-(m.nx*m.px+m.ny*m.py+m.nz*m.pz);
		const float rex=ex-2.f*sd*m.nx,rey=eyeY-2.f*sd*m.ny,rez=ez-2.f*sd*m.nz;
		const float uy=(fabsf(m.ny)>.9f)?-1.f:1.f;
		return S3mMatMul(S3mLookAt(rex,rey,rez,m.px,m.py,m.pz,0.f,uy,0.f),S3mPerspective(fov*fovMul,1.f,.05f,40.f));
	};
	cb.lightDir.w=0.f;dc->PSSetShaderResources(0,7,ns);
	auto drawMirrorSlot=[&](int slot,const MirPick& pick,BOOL ok,S3MMat* storeVP,float fovMul){
		float mbg[4]={.48f,.64f,.82f,1};D3D11_VIEWPORT mvp={0,0,(float)CS3mView::S3M_MIRROR_SIZE,(float)CS3mView::S3M_MIRROR_SIZE,0,1};
		dc->RSSetViewports(1,&mvp);dc->RSSetState(m_view.m_rsSolid);
		dc->OMSetRenderTargets(1,&m_view.m_mirrorRtv[slot],m_view.m_mirrorDsv);
		dc->ClearRenderTargetView(m_view.m_mirrorRtv[slot],mbg);dc->ClearDepthStencilView(m_view.m_mirrorDsv,D3D11_CLEAR_DEPTH,1.f,0);
		if(!ok||!m_view.m_mirrorRtv[slot])return;
		const S3MMat rVP=makeReflectVP(pick,fovMul);
		if(slot==0)cb.reflectVP=rVP;else if(slot==1)cb.reflectFloorVP=rVP;
		if(storeVP)*storeVP=rVP;
		cb.viewProj=rVP;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		drawFloorWall(TRUE);dc->PSSetShaderResources(0,7,ns);
	};
	drawMirrorSlot(0,bestW,bestW.score>0.f,NULL,1.f);drawMirrorSlot(1,bestF,bestF.score>0.f,NULL,1.f);
	for(int i=0;i<nFx;i++){
		const int slot=fxMirOf[i];if(slot<0)continue;
		float nx=ex-fxObj[i].cx,ny=eyeY-fxObj[i].cy,nz=ez-fxObj[i].cz;float nl=sqrtf(nx*nx+ny*ny+nz*nz)+1e-5f;nx/=nl;ny/=nl;nz/=nl;
		MirPick mp={1.f,nx,ny,nz,fxObj[i].cx,fxObj[i].cy,fxObj[i].cz};
		drawMirrorSlot(slot,mp,TRUE,&fxRefVP[slot-CS3mView::S3M_MIRROR_FX0],1.35f);
	}
	dc->OMSetRenderTargets(1,&nullRtv,NULL);cb.viewProj=camVP;cb.lightDir.w=1.f;cb.reflectFloorVP=(bestF.score>0.f)?cb.reflectFloorVP:idM;
	if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}}
	D3D11_VIEWPORT vp={0,0,(float)w,(float)h,0,1};dc->RSSetViewports(1,&vp);dc->RSSetState(m_view.m_rsSolid);
	// 地下は青空を出さない（天井＋暗いクリア）
	const int clearF=stairMove?((m_stairCamY<0.f)?max(m_stairFrom,m_stairTo):m_stairFrom):m_floor;
	float bg[4]={.48f,.64f,.82f,1};if(clearF>0){bg[0]=.07f;bg[1]=.08f;bg[2]=.10f;}
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,m_view.m_dsv);dc->ClearRenderTargetView(m_view.m_sceneRtv,bg);dc->ClearDepthStencilView(m_view.m_dsv,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
	dc->PSSetShaderResources(5,1,&m_view.m_mirrorSrv[0]);dc->PSSetShaderResources(6,1,&m_view.m_mirrorSrv[1]);
	drawFloorWall(TRUE);
	dc->PSSetShaderResources(5,2,ns+5);
	dc->OMSetDepthStencilState(m_view.m_dssOff,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetSamplers(1,1,&m_view.m_sampPoint);
	dc->OMSetRenderTargets(1,&m_view.m_postRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_sceneSrv);dc->PSSetShaderResources(2,1,&m_view.m_dsSrv);dc->PSSetShader(m_view.m_psSsr,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_postSrv);dc->PSSetShaderResources(2,1,&m_view.m_dsSrv);dc->PSSetShader(m_view.m_psDof,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_sceneSrv);dc->PSSetShader(m_view.m_psFinal,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	if(nTrans){
		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,m_view.m_dsv);
		S3MMat floorVP=cb.reflectFloorVP;
		drawTransRange(winBeg,nWin,TRUE);
		for(int i=0;i<nFx;i++){
			const int slot=fxMirOf[i];
			if(slot>=0){
				cb.reflectFloorVP=fxRefVP[slot-CS3mView::S3M_MIRROR_FX0];
				cb.lightDir.w=1.f;
				if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
				dc->PSSetShaderResources(6,1,&m_view.m_mirrorSrv[slot]);
			}else{
				dc->PSSetShaderResources(6,1,ns+6);
			}
			drawTransRange(fxObj[i].beg,fxObj[i].n,TRUE);
		}
		cb.reflectFloorVP=floorVP;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->PSSetShaderResources(5,1,&m_view.m_mirrorSrv[0]);dc->PSSetShaderResources(6,1,&m_view.m_mirrorSrv[1]);
		drawTransRange(plateBeg,nPlate,TRUE);
		dc->PSSetShaderResources(5,2,ns+5);
	}
	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);dc->OMSetDepthStencilState(m_view.m_dssOff,0);
	const UINT maxH=m_view.m_vbHudBytes/sizeof(S3MHudVertex);S3MHudVertex* hv=new S3MHudVertex[maxH];UINT hn=0;
	auto hp=[&](float px,float py,float r,float g,float b,float a){if(hn<maxH)hv[hn++]={(px/(float)w)*2.f-1.f,1.f-(py/(float)h)*2.f,r,g,b,a};};
	auto hq=[&](float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,float r,float g,float b,float a){hp(ax,ay,r,g,b,a);hp(bx,by,r,g,b,a);hp(cx,cy,r,g,b,a);hp(ax,ay,r,g,b,a);hp(cx,cy,r,g,b,a);hp(dx,dy,r,g,b,a);};
	const BOOL overview=IsOverviewActive();
	float badgeX=0,badgeY=0,badgeMaxW=120.f;int badgeFloor=m_floor;BOOL drawBadge=FALSE;
	if(overview){
		hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,.02f,.03f,.05f,.58f);
	}else if(savedata.s3m_show_map){
		// スケール完全固定: ワールド距離一定（端でもセル数でズームが変わらない）
		const float kMapViewWorld=14.f;
		const float mpix=(float)min(w,h)*.30f;
		const float cs=mpix/kMapViewWorld;
		const float mcx=w-10.f-mpix*.5f,mcy=10.f+mpix*.5f,pad=mpix*.5f+3.f;
		const float L=mcx-pad,R=mcx+pad,T=mcy-pad,B=mcy+pad;
		hq(L,T,R,T,R,B,L,B,.04f,.05f,.08f,.72f);
		auto inBox=[&](float qx,float qy)->BOOL{return qx>=L&&qx<=R&&qy>=T&&qy<=B;};
		const float rad=kMapViewWorld*.75f;
		const int gx0=max(0,WorldToGridAxis(ex-rad)-1),gx1=min(m_n,WorldToGridAxis(ex+rad)+2);
		const int gz0=max(0,WorldToGridAxis(ez-rad)-1),gz1=min(m_n,WorldToGridAxis(ez+rad)+2);
		auto mapQuad=[&](float wx0,float wz0,float wx1,float wz1,float wx2,float wz2,float wx3,float wz3,float rr,float gg,float bb,float a){
			float qx[4],qy[4],mx,my;
			WorldToMap(wx0,wz0,mx,my);qx[0]=mcx+mx*cs;qy[0]=mcy+my*cs;
			WorldToMap(wx1,wz1,mx,my);qx[1]=mcx+mx*cs;qy[1]=mcy+my*cs;
			WorldToMap(wx2,wz2,mx,my);qx[2]=mcx+mx*cs;qy[2]=mcy+my*cs;
			WorldToMap(wx3,wz3,mx,my);qx[3]=mcx+mx*cs;qy[3]=mcy+my*cs;
			const float acx=(qx[0]+qx[1]+qx[2]+qx[3])*.25f,acy=(qy[0]+qy[1]+qy[2]+qy[3])*.25f;
			if(!inBox(acx,acy))return;
			hq(qx[0],qy[0],qx[1],qy[1],qx[2],qy[2],qx[3],qy[3],rr,gg,bb,a);
		};
		// 階層切替中は旧階層→新階層をクロスフェード
		auto drawMapFloor=[&](int mf,float am){
			if(mf<0||mf>=m_nFloors||!m_grids[mf]||am<=.01f)return;
			for(int z=gz0;z<gz1;z++)for(int x=gx0;x<gx1;x++){
				BYTE c=CellAtF(mf,x,z);if(c==CELL_WALL||c==CELL_WINDOW)continue;
				float x0=AxisOrigin(x),x1=x0+AxisSpan(x),z0=AxisOrigin(z),z1=z0+AxisSpan(z);
				float rr=.22f,gg=.40f,bb=.28f,a=.55f;
				if(c==CELL_GOAL){rr=1;gg=.8f;bb=.18f;a=.9f;}
				else if(c==CELL_START){rr=.2f;gg=.9f;bb=.45f;a=.9f;}
				else if(c==CELL_STAIRS_DOWN){rr=1;gg=.58f;bb=.16f;a=.9f;}
				else if(c==CELL_STAIRS_UP){rr=.22f;gg=.86f;bb=1.f;a=.9f;}
				else if(VisitAtF(mf,x,z)){rr=.30f;gg=.55f;bb=.85f;a=.7f;}
				mapQuad(x0,z0,x1,z0,x1,z1,x0,z1,rr,gg,bb,a*am);
			}
			for(int z=gz0;z<gz1;z++)for(int x=gx0;x<gx1;x++){
				BYTE c=CellAtF(mf,x,z);if(c!=CELL_WALL&&c!=CELL_WINDOW)continue;
				float x0=AxisOrigin(x),x1=x0+AxisSpan(x),z0=AxisOrigin(z),z1=z0+AxisSpan(z);
				float rr=.55f,gg=.37f,bb=.25f,a=.95f;if(c==CELL_WINDOW){rr=.25f;gg=.58f;bb=.86f;}
				mapQuad(x0,z0,x1,z0,x1,z1,x0,z1,rr,gg,bb,a*am);
			}
		};
		if(m_miniFade<.999f){drawMapFloor(m_miniFadeFrom,1.f-m_miniFade);drawMapFloor(m_miniFadeTo,m_miniFade);}
		else drawMapFloor(m_floor,1.f);
		float ps=max(2.f,min(4.5f,cs*.18f));
		auto clipPt2=[&](float& qx,float& qy){qx=max(L+1.f,min(R-1.f,qx));qy=max(T+1.f,min(B-1.f,qy));};
		float p0x=mcx,p0y=mcy-ps,p1x=mcx-ps*.55f,p1y=mcy+ps*.4f,p2x=mcx+ps*.55f,p2y=mcy+ps*.4f;
		clipPt2(p0x,p0y);clipPt2(p1x,p1y);clipPt2(p2x,p2y);
		hp(p0x,p0y,1,.92f,.3f,1);hp(p1x,p1y,1,.92f,.3f,1);hp(p2x,p2y,1,.92f,.3f,1);
		float nx,ny,xx,xy;WorldToMap(ex+fx,ez+fz,nx,ny);WorldToMap(ex+rx,ez+rz,xx,xy);
		float ccx=min(R-8.f,max(L+8.f,mcx+pad-12)),ccy=min(B-8.f,max(T+8.f,mcy-pad+12));
		hp(ccx+nx*8,ccy+ny*8,1,.2f,.2f,1);hp(ccx-nx*2-xx*4,ccy-ny*2-xy*4,1,.2f,.2f,1);hp(ccx-nx*2+xx*4,ccy-ny*2+xy*4,1,.2f,.2f,1);
		drawBadge=TRUE;badgeFloor=m_floor;badgeX=L+6.f;badgeY=T+5.f;badgeMaxW=max(72.f,(R-L)*.55f);
	}
	if(m_clearScreenA>.01f)hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,0,0,0,m_clearScreenA);
	else if(m_floorScreenA>.01f)hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,0,0,0,m_floorScreenA);
	if(hn&&SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);UINT hs=sizeof(S3MHudVertex);dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);dc->IASetInputLayout(m_view.m_ilHud);dc->VSSetShader(m_view.m_vsHud,NULL,0);dc->PSSetShader(m_view.m_psHud,NULL,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(hn,0);}
	if(overview&&m_view.m_srvMap&&m_view.m_texMap){
		const int TS=CS3mView::S3M_MAP_SIZE;
		const float worldSpan=AxisOrigin(m_n);
		int bakeFloor=m_mapViewFloor;
		if(bakeFloor<0||bakeFloor>=m_nFloors||!m_grids[bakeFloor])bakeFloor=m_floor;
		if(m_mapBakeDirty){
			D3D11_MAPPED_SUBRESOURCE mm={};
			if(SUCCEEDED(dc->Map(m_view.m_texMap,0,D3D11_MAP_WRITE_DISCARD,0,&mm))&&mm.pData){
				BYTE* row=(BYTE*)mm.pData;
				const int pitch=(int)mm.RowPitch;
				const float invTS=worldSpan/ (float)TS;
				for(int ty=0;ty<TS;ty++){
					const float wz=((float)ty+.5f)*invTS;
					DWORD* dst=(DWORD*)(row+ty*pitch);
					for(int tx=0;tx<TS;tx++){
						const float wx=((float)tx+.5f)*invTS;
						const int gx=WorldToGridAxis(wx),gz=WorldToGridAxis(wz);
						const BYTE c=CellAtF(bakeFloor,gx,gz);
						BYTE r=28,g=34,b=42,a=170;
						if(c==CELL_WALL){r=92;g=62;b=44;a=220;}
						else if(c==CELL_WINDOW){r=48;g=130;b=190;a=210;}
						else if(c==CELL_GOAL){r=255;g=210;b=40;a=235;}
						else if(c==CELL_START){r=55;g=220;b=120;a=235;}
						else if(c==CELL_STAIRS_DOWN){r=255;g=148;b=40;a=240;}
						else if(c==CELL_STAIRS_UP){r=60;g=220;b=255;a=240;}
						else if(c>=CELL_TEMPO&&c<=CELL_EQ){r=210;g=90;b=200;a=225;}
						else if(VisitAtF(bakeFloor,gx,gz)){r=70;g=130;b=200;a=200;}
						else{r=42;g=78;b=55;a=170;}
						dst[tx]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
					}
				}
				dc->Unmap(m_view.m_texMap,0);
				m_mapBakeDirty=0;
			}
		}
		const float side=(float)min(w,h)*.86f;
		const float ox=((float)w-side)*.5f,oy=((float)h-side)*.5f;
		D3D11_VIEWPORT mvp={ox,oy,side,side,0,1};
		cb.misc.z=99.f;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->RSSetViewports(1,&mvp);
		dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetSamplers(0,1,&m_view.m_sampPoint);
		dc->PSSetShaderResources(0,1,&m_view.m_srvMap);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,ns);
		// 自機マーカー（ワールド圧縮座標・カメラ押し出し後）
		hn=0;
		const float pcx=ox+(ex/max(.001f,worldSpan))*side,pcy=oy+(ez/max(.001f,worldSpan))*side;
		float ffx,ffz,rrx,rrz;CamBasisYaw(m_yaw,ffx,ffz,rrx,rrz);
		const float ps=max(5.f,side*.012f);
		float t0x=pcx+ffx*ps,t0y=pcy+ffz*ps,t1x=pcx-ffx*ps*.4f-rrx*ps*.5f,t1y=pcy-ffz*ps*.4f-rrz*ps*.5f,t2x=pcx-ffx*ps*.4f+rrx*ps*.5f,t2y=pcy-ffz*ps*.4f+rrz*ps*.5f;
		auto clampMap=[&](float& qx,float& qy){qx=max(ox+2.f,min(ox+side-2.f,qx));qy=max(oy+2.f,min(oy+side-2.f,qy));};
		clampMap(t0x,t0y);clampMap(t1x,t1y);clampMap(t2x,t2y);
		hp(t0x,t0y,1.f,.92f,.25f,1.f);hp(t1x,t1y,1.f,.92f,.25f,1.f);hp(t2x,t2y,1.f,.92f,.25f,1.f);
		hq(ox-2,oy-2,ox+side+2,oy-2,ox+side+2,oy+side+2,ox-2,oy+side+2,.9f,.92f,1.f,.22f);
		if(hn&&SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);UINT hs=sizeof(S3MHudVertex);dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);dc->RSSetViewports(1,&vp);dc->IASetInputLayout(m_view.m_ilHud);dc->VSSetShader(m_view.m_vsHud,NULL,0);dc->PSSetShader(m_view.m_psHud,NULL,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(hn,0);}
		dc->RSSetViewports(1,&vp);
		drawBadge=TRUE;badgeFloor=bakeFloor;badgeX=ox+12.f;badgeY=oy+12.f;badgeMaxW=max(100.f,side*.28f);
	}
	auto blitFloorBadge=[&](){
		if(!drawBadge||!m_view.m_ready)return;
		const CStringW lab(FloorLabel(badgeFloor));
		if(lab!=m_mapBadgeText||!m_view.m_srvBadge)
			m_view.BakeBadgeTexture((LPCWSTR)lab),m_mapBadgeText=lab;
		if(!m_view.m_srvBadge||m_view.m_badgeW<=0)return;
		float tw=(float)m_view.m_badgeW,th=(float)m_view.m_badgeH;
		if(tw>badgeMaxW){const float s=badgeMaxW/tw;tw*=s;th*=s;}
		D3D11_VIEWPORT bvp={badgeX,badgeY,tw,th,0,1};
		cb.misc.z=99.f;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->RSSetViewports(1,&bvp);
		dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		dc->PSSetShaderResources(0,1,&m_view.m_srvBadge);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,ns);
		dc->RSSetViewports(1,&vp);
	};
	blitFloorBadge();
	if(m_view.m_srvTip&&m_view.m_tipW>0&&m_view.m_tipH>0){
		const float maxW=(float)w*.48f;
		float tw=(float)m_view.m_tipW,th=(float)m_view.m_tipH;
		if(tw>maxW){const float s=maxW/tw;tw*=s;th*=s;}
		D3D11_VIEWPORT tipVp={8.f,8.f,tw,th,0,1};
		cb.misc.z=99.f;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->RSSetViewports(1,&tipVp);
		dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		dc->PSSetShaderResources(0,1,&m_view.m_srvTip);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,ns);
		dc->RSSetViewports(1,&vp);
	}
	delete[] hv;
	if(m_clearTextA<=.01f&&m_floorTextA>.01f&&m_view.m_srvClear){
		cb.misc.z=99.f;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		D3D11_VIEWPORT tvp={0,(float)h*.375f,(float)w,(float)h*.25f,0,1};dc->RSSetViewports(1,&tvp);dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetShaderResources(0,1,&m_view.m_srvClear);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);dc->PSSetShaderResources(0,1,ns);
	}
	if(m_clearTextA>.01f&&m_view.m_srvClear){
		cb.misc.z=99.f;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		D3D11_VIEWPORT tvp={0,(float)h*.375f,(float)w,(float)h*.25f,0,1};dc->RSSetViewports(1,&tvp);dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetShaderResources(0,1,&m_view.m_srvClear);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);dc->PSSetShaderResources(0,1,ns);
	}
	dc->RSSetViewports(1,&vp);m_view.PresentFrame();
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
	menu.AddCommand(2, LL14(L"同じシードで再生成", L"Regenerate with same seed", L"Régénérer avec la même graine", L"Rigenera con lo stesso seed",
		L"Regenerar con la misma semilla", L"같은 시드로 재생성", L"用相同种子重新生成", L"إعادة التوليد بنفس البذرة",
		L"Создать с тем же зерном", L"Mit gleichem Seed erzeugen", L"Gerar com a mesma semente", L"Opnieuw met dezelfde seed",
		L"Generuj z tym samym ziarnem", L"Aynı tohumla yeniden oluştur"));
	menu.AddSeparator();
	{
		CString sizeFmt = LL14(L"大きさ: %d", L"Size: %d", L"Taille: %d", L"Dimensione: %d", L"Tamaño: %d",
			L"크기: %d", L"大小：%d", L"الحجم: %d", L"Размер: %d", L"Größe: %d", L"Tamanho: %d", L"Grootte: %d", L"Rozmiar: %d", L"Boyut: %d");
		for (int i = 0; i < kPresetCnt; i++) {
			CString lab;
			lab.Format(sizeFmt, kPresets[i]);
			menu.AddCheck(S3M_MENU_SIZE0 + i, lab, curSz == kPresets[i]);
		}
	}
	menu.AddSeparator();
	menu.AddCheck(20, LL14(L"ミニマップ表示", L"Show minimap", L"Afficher la minimap", L"Mostra minimap", L"Mostrar minimapa",
		L"미니맵 표시", L"显示小地图", L"إظهار الخريطة المصغّرة", L"Показать мини-карту", L"Minimap anzeigen", L"Mostrar minimapa", L"Minimapa tonen", L"Pokaż minimapę", L"Minimapi göster"),
		savedata.s3m_show_map != 0);
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
	menu.AddSeparator();
	menu.AddCheck(50,LL14(L"歩行時の揺れ",L"Walking bob",L"Balancement de marche",L"Oscillazione camminata",L"Balanceo al caminar",L"걷기 흔들림",L"行走晃动",L"تمايل المشي",L"Покачивание при ходьбе",L"Kamerawippen",L"Balanço ao andar",L"Loopbeweging",L"Kołysanie chodu",L"Yürüme sallantısı"),savedata.s3m_bob!=0);
	menu.AddCheck(51,L"FOV 55°",savedata.s3m_fov==0);
	menu.AddCheck(52,L"FOV 70°",savedata.s3m_fov==1);
	menu.AddCheck(53,L"FOV 90°",savedata.s3m_fov==2);

	UINT cmd = menu.Track(screenPt, this);
	if (cmd == 1) {
		GenerateMaze();
		return;
	}
	if(cmd==2){GenerateMazeWithSeed(m_genSeed);return;}
	if (cmd >= S3M_MENU_SIZE0 && cmd < S3M_MENU_SIZE0 + kPresetCnt) {
		SetSizeToUi(kPresets[cmd - S3M_MENU_SIZE0]);
		PersistUi();
		GenerateMaze();
		return;
	}
	if (cmd == 20) {
		savedata.s3m_show_map = savedata.s3m_show_map ? 0 : 1;
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
	if(cmd==50){savedata.s3m_bob=savedata.s3m_bob?0:1;PersistUi();return;}
	if(cmd>=51&&cmd<=53){savedata.s3m_fov=(int)cmd-51;PersistUi();return;}
}

BOOL CSoft3DMazeDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_size.SetAeroMode(FALSE);
	m_base.SetAeroMode(FALSE);
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
	if(savedata.s3m_bob!=0&&savedata.s3m_bob!=1)savedata.s3m_bob=1;
	if(savedata.s3m_fov<0||savedata.s3m_fov>2)savedata.s3m_fov=1;
	if(savedata.s3m_basements<0||savedata.s3m_basements>S3M_MAX_FLOORS-1)savedata.s3m_basements=0;

	SetWindowText(LL14(L"Soft3D 迷路", L"Soft3D maze", L"Labyrinthe Soft3D", L"Labirinto Soft3D", L"Laberinto Soft3D",
		L"Soft3D 미로", L"Soft3D 迷宫", L"متاهة Soft3D", L"Лабиринт Soft3D", L"Soft3D-Labyrinth",
		L"Labirinto Soft3D", L"Soft3D-doolhof", L"Labirynt Soft3D", L"Soft3D labirent"));
	m_sizeL.SetWindowText(LL14(L"大きさ", L"Size", L"Taille", L"Dimensione", L"Tamaño",
		L"크기", L"大小", L"الحجم", L"Размер", L"Größe", L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"));
	m_baseL.SetWindowText(LL14(L"地下", L"Basement", L"Sous-sol", L"Sotterraneo", L"Sótano",
		L"지하", L"地下", L"قبو", L"Подвал", L"Keller", L"Subsolo", L"Kelder", L"Piwnica", L"Bodrum"));
	m_gen.SetWindowText(LL14(L"生成", L"Generate", L"Générer", L"Genera", L"Generar",
		L"생성", L"生成", L"توليد", L"Создать", L"Erzeugen", L"Gerar", L"Genereren", L"Generuj", L"Oluştur"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(L"WASD / QE · SPACE or ホイールクリック=全体マップ（+←→/ホイールで階層） · 階段で地下へ · 右クリック設定", L"WASD / QE · hold SPACE or wheel-click=map (+←→/wheel for floor) · stairs lead down · right-click settings", L"WASD / QE · Espace ou clic molette=carte (+←→/molette=étage) · escaliers · clic droit", L"WASD / QE · SPAZIO o clic rotella=mappa (+←→/rotella=piano) · scale · clic destro",
		L"WASD / QE · Espacio o clic rueda=mapa (+←→/rueda=planta) · escaleras · clic derecho", L"WASD / QE · SPACE/휠클릭=맵(+←→/휠=층) · 계단으로 지하 · 우클릭 설정", L"WASD / QE · 空格或滚轮点击=地图（+←→/滚轮换层） · 楼梯通地下 · 右键设置", L"WASD / QE · مسافة أو نقر العجلة=خريطة (+←→/العجلة=طابق) · سلالم · يمين",
		L"WASD / QE · Пробел или клик колёсиком=карта (+←→/колесо=этаж) · лестницы · ПКМ", L"WASD / QE · Leertaste/Radklick=Karte (+←→/Rad=Etage) · Treppen · Rechtsklick", L"WASD / QE · Espaço ou roda=mapa (+←→/roda=piso) · escadas · direito", L"WASD / QE · Spatie of wielklik=kaart (+←→/wiel=verdieping) · trappen · rechtsklik",
		L"WASD / QE · Spacja lub kółko=mapa (+←→/kółko=piętro) · schody · PPM", L"WASD / QE · SPACE/teker tık=harita (+←→/teker=kat) · merdivenler · sağ tık"));

	for (int i = 0; i < kPresetCnt; i++) {
		CString s;
		s.Format(_T("%d"), kPresets[i]);
		m_size.AddString(s);
	}
	SetSizeToUi(S3mNormalizeSavedSize(savedata.s3m_size));

	m_base.AddString(LL14(L"地下なし", L"No basement", L"Aucun sous-sol", L"Nessun sotterraneo", L"Sin sótano",
		L"지하 없음", L"无地下", L"بلا قبو", L"Без подвала", L"Kein Keller", L"Sem subsolo", L"Geen kelder", L"Bez piwnicy", L"Bodrum yok"));
	m_base.AddString(LL14(L"地下1F", L"1 basement", L"1 sous-sol", L"1 sotterraneo", L"1 sótano",
		L"지하 1층", L"地下1层", L"قبو واحد", L"1 подземный", L"1 Kellergeschoss", L"1 subsolo", L"1 kelder", L"1 piwnica", L"1 bodrum"));
	m_base.AddString(LL14(L"地下2F", L"2 basements", L"2 sous-sols", L"2 sotterranei", L"2 sótanos",
		L"지하 2층", L"地下2层", L"قبوان", L"2 подземных", L"2 Kellergeschosse", L"2 subsolos", L"2 kelders", L"2 piwnice", L"2 bodrum"));
	m_base.AddString(LL14(L"地下3F", L"3 basements", L"3 sous-sols", L"3 sotterranei", L"3 sótanos",
		L"지하 3층", L"地下3层", L"ثلاثة أقبية", L"3 подземных", L"3 Kellergeschosse", L"3 subsolos", L"3 kelders", L"3 piwnice", L"3 bodrum"));
	SetBasementsToUi(savedata.s3m_basements);

	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tooltip.Activate(TRUE);
		m_tooltip.AddTool(&m_gen, LL14(L"新しい迷路を生成します", L"Generate a new maze", L"Générer un nouveau labyrinthe", L"Genera un nuovo labirinto", L"Generar un nuevo laberinto",
			L"새 미로 생성", L"生成新迷宫", L"إنشاء متاهة جديدة", L"Создать новый лабиринт", L"Neues Labyrinth erzeugen", L"Gerar um novo labirinto", L"Nieuw doolhof genereren", L"Wygeneruj nowy labirynt", L"Yeni labirent oluştur"));
		m_tooltip.AddTool(&m_view, LL14(L"右クリックで設定。ホイールクリックで全体マップ切替。SPACE押しっぱなしでも全体マップ。全体マップ中は ←→ / ホイールで表示階層を変更。橙の階段=下り／水色=上り。", L"Right-click: settings. Wheel-click: toggle full map. Hold SPACE: full map. In map, ←→ or wheel changes floor. Orange stairs go down, cyan up.", L"Clic droit: réglages. Clic molette: carte. Espace maintenu: carte. Dans la carte, ←→/molette change d'étage. Orange=descendre, cyan=monter.", L"Clic destro: impostazioni. Clic rotella: mappa. Tieni SPAZIO: mappa. Nella mappa ←→/rotella cambia piano. Arancio=giù, ciano=su.", L"Clic derecho: ajustes. Clic rueda: mapa. Mantén Espacio: mapa. En el mapa ←→/rueda cambia planta. Naranja=bajar, cian=subir.",
			L"우클릭: 설정. 휠 클릭: 전체 맵 토글. SPACE 유지: 전체 맵. 맵에서 ←→/휠로 층 변경. 주황 계단=하강, 하늘색=상승.", L"右键：设置。滚轮点击：全图开关。按住空格：全图。全图中 ←→/滚轮切换层。橙色楼梯下行，水色上行。", L"يمين: إعدادات. نقر العجلة: خريطة. مسافة: خريطة. في الخريطة ←→/العجلة تغيّر الطابق. برتقالي=نزول، سماوي=صعود.", L"ПКМ: настройки. Клик колёсиком: карта. Пробел: карта. На карте ←→/колесо меняют этаж. Оранжевые лестницы вниз, голубые вверх.", L"Rechtsklick: Einstellungen. Radklick: Karte. Leertaste halten: Karte. In der Karte ←→/Rad wechselt die Etage. Orange=abwärts, Cyan=aufwärts.", L"Direito: definições. Clique roda: mapa. Segure Espaço: mapa. No mapa ←→/roda muda o piso. Laranja=descer, ciano=subir.", L"Rechtsklik: instellingen. Wielklik: kaart. Houd Spatie: kaart. In de kaart wisselt ←→/wiel de verdieping. Oranje=omlaag, cyaan=omhoog.", L"PPM: ustawienia. Klik kółkiem: mapa. Trzymaj Spację: mapa. Na mapie ←→/kółko zmienia piętro. Pomarańcz=w dół, cyjan=w górę.", L"Sağ tık: ayarlar. Teker tık: harita. SPACE basılı: harita. Haritada ←→/teker katı değiştirir. Turuncu=aşağı, camgöbeği=yukarı."));
		m_tooltip.AddTool(&m_base, LL14(L"地下の階数（0〜3）。地下があると階段でつながり、ゴールは最下層に置かれます。", L"Number of basements (0–3). Stairs link the floors and the goal sits on the deepest floor.", L"Nombre de sous-sols (0–3). Les escaliers relient les étages ; le but est au plus profond.", L"Numero di sotterranei (0–3). Le scale collegano i piani; il traguardo è nel più profondo.", L"Número de sótanos (0–3). Las escaleras unen las plantas; la meta está en la más profunda.",
			L"지하 층수(0~3). 계단으로 연결되고 골은 최하층에 놓입니다.", L"地下层数（0–3）。楼梯连接各层，终点在最深层。", L"عدد الأقبية (0–3). السلالم تربط الطوابق والهدف في الأعمق.", L"Число подземных этажей (0–3). Лестницы связывают этажи, цель на самом нижнем.", L"Anzahl Kellergeschosse (0–3). Treppen verbinden die Etagen, das Ziel liegt unten.", L"Número de subsolos (0–3). Escadas ligam os pisos; o gol fica no mais profundo.", L"Aantal kelders (0–3). Trappen verbinden de verdiepingen; het doel ligt onderaan.", L"Liczba piwnic (0–3). Schody łączą piętra, cel jest na najniższym.", L"Bodrum sayısı (0–3). Merdivenler katları bağlar, hedef en alttadır."));
	}

	CaptureAudioBaseline();
	LayoutAll();
	if(!m_view.InitDx()){
		MessageBox(LL14(L"DirectX 11 の初期化に失敗しました。",L"DirectX 11 initialization failed.",L"Échec de l'initialisation de DirectX 11.",L"Inizializzazione DirectX 11 non riuscita.",L"Error al iniciar DirectX 11.",L"DirectX 11 초기화에 실패했습니다.",L"DirectX 11 初始化失败。",L"فشل تهيئة DirectX 11.",L"Не удалось инициализировать DirectX 11.",L"DirectX 11 konnte nicht initialisiert werden.",L"Falha ao iniciar o DirectX 11.",L"Initialisatie van DirectX 11 mislukt.",L"Nie udało się zainicjować DirectX 11.",L"DirectX 11 başlatılamadı."),NULL,MB_OK|MB_ICONERROR);
		DestroyWindow();
		return FALSE;
	}
	m_view.BakeTipTexture(LL14(
		L"WASD / QE：移動・旋回\nSPACE押しっぱなし／ホイールクリック：全体マップ（←→ or ホイールで階層）\n橙の階段=地下へ／水色=地上へ。ゴールは最下層",
		L"WASD / QE: move / turn\nHold SPACE or wheel-click: full map (←→ or wheel = floor)\nOrange stairs go down, cyan up. Goal on the deepest floor",
		L"WASD / QE : bouger / tourner\nEspace / clic molette : carte (←→ ou molette = étage)\nEscaliers orange : descendre, cyan : monter. But au plus profond",
		L"WASD / QE: muovi / gira\nSPAZIO o clic rotella: mappa (←→ o rotella = piano)\nScale arancioni giù, ciano su. Traguardo nel piano più profondo",
		L"WASD / QE: mover / girar\nEspacio o clic rueda: mapa (←→ o rueda = planta)\nEscaleras naranjas bajan, cian suben. Meta en la planta más profunda",
		L"WASD / QE: 이동 / 선회\nSPACE 유지·휠 클릭: 전체 맵 (←→ 또는 휠로 층)\n주황 계단=지하로, 하늘색=지상으로. 골은 최하층",
		L"WASD / QE：移动 / 转向\n按住空格或滚轮点击：全图（←→ 或滚轮换层）\n橙色楼梯下行，水色上行。终点在最深层",
		L"WASD / QE: حركة / دوران\nمسافة أو نقر العجلة: خريطة (←→ أو العجلة = طابق)\nالسلالم البرتقالية للأسفل والسماوية للأعلى. الهدف في الأعمق",
		L"WASD / QE: ход / поворот\nПробел или клик колёсиком: карта (←→ или колесо = этаж)\nОранжевые лестницы вниз, голубые вверх. Цель на нижнем этаже",
		L"WASD / QE: bewegen / drehen\nLeertaste oder Radklick: Karte (←→ oder Rad = Etage)\nOrange Treppen abwärts, Cyan aufwärts. Ziel in der tiefsten Etage",
		L"WASD / QE: mover / girar\nEspaço ou clique da roda: mapa (←→ ou roda = piso)\nEscadas laranja descem, ciano sobem. Gol no piso mais profundo",
		L"WASD / QE: bewegen / draaien\nSpatie of wielklik: kaart (←→ of wiel = verdieping)\nOranje trappen omlaag, cyaan omhoog. Doel op de diepste verdieping",
		L"WASD / QE: ruch / obrót\nSpacja lub klik kółkiem: mapa (←→ lub kółko = piętro)\nPomarańczowe schody w dół, cyjanowe w górę. Cel na najniższym piętrze",
		L"WASD / QE: hareket / dönüş\nSPACE veya teker tık: harita (←→ veya teker = kat)\nTuruncu merdiven aşağı, camgöbeği yukarı. Hedef en alt katta"));
	if (!LoadRun())
		GenerateMaze();
	else {
		UpdateStatus();
		if (m_won)
			BeginClearSequence();
		RenderScene();
	}
	m_lastTick = GetTickCount();
	m_lastAutosave = m_lastTick;
	SetTimer(S3M_TIMER, 8, NULL);
	return TRUE;
}

void CSoft3DMazeDlg::OnGen() { GenerateMaze(); }
void CSoft3DMazeDlg::OnSizeChanged()
{
	// CBN_SELCHANGE 時点では編集欄がまだ旧値のことがある → リスト選択を優先
	// GetCurSel は論理 index、GetLBText は物理 index（取り違えると常に先頭付近の値になる）
	int n = -1;
	if (m_size.GetSafeHwnd()) {
		const int phys = m_size.GetCurSelPhysical();
		if (phys != CB_ERR) {
			CString s;
			m_size.GetLBText(phys, s);
			n = _ttoi(s);
		}
	}
	if (n < S3M_MIN)
		n = ReadSizeFromUi();
	SetSizeToUi(n);
	PersistUi();
}
void CSoft3DMazeDlg::OnSizeEditChange()
{
	if (m_size.GetSafeHwnd())
		m_size.Invalidate(FALSE);
}
void CSoft3DMazeDlg::OnBaseChanged()
{
	// 次に生成する迷路へ反映（現在の迷路はそのまま）
	PersistUi();
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
