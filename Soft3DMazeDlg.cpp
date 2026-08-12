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
	, m_vsTess(NULL), m_hsTess(NULL), m_dsTess(NULL)
	, m_psWall(NULL), m_vsSolid(NULL), m_psSolid(NULL), m_vsHud(NULL), m_psHud(NULL), m_vsPost(NULL)
	, m_psSsr(NULL), m_psDof(NULL), m_psFinal(NULL), m_ilPatch(NULL), m_ilSolid(NULL), m_ilHud(NULL)
	, m_cbFrame(NULL), m_vbDyn(NULL), m_vbHud(NULL), m_vbDynBytes(6*1024*1024), m_vbHudBytes(512*1024)
	, m_texBrick(NULL), m_srvBrick(NULL), m_texFloor(NULL), m_srvFloor(NULL), m_texEnv(NULL), m_srvEnv(NULL)
	, m_texClear(NULL), m_srvClear(NULL), m_clearTexW(0), m_clearTexH(0), m_sampLin(NULL), m_sampPoint(NULL), m_sampCmp(NULL)
	, m_rsSolid(NULL), m_rsShadow(NULL), m_dssWrite(NULL), m_dssRead(NULL), m_dssOff(NULL), m_bsOpaque(NULL), m_bsAlpha(NULL)
	, m_bsAdd(NULL), m_dragging(0), m_dragTurnAcc(0)
{
}

CS3mView::~CS3mView() { ReleaseDx(); }

BOOL CS3mView::CreateShaders()
{
	static const char* hlsl =
		"cbuffer F:register(b0){row_major float4x4 VP;row_major float4x4 LightVP;float4 Eye;float4 Fog;float4 Dof;float4 Screen;float4 Misc;float4 LightDir;}"
		"Texture2D T0:register(t0);Texture2D T1:register(t1);Texture2D Depth:register(t2);"
		"TextureCube Env:register(t3);Texture2D ShadowMap:register(t4);"
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
		"float4 PSW(D i):SV_Target{float4 a=T0.Sample(SL,i.uv*2.5)*i.c;float h=T0.Sample(SL,i.uv*2.5).a;"
		"float hx=T0.Sample(SL,i.uv*2.5+float2(.004,0)).a-h;float hy=T0.Sample(SL,i.uv*2.5+float2(0,.004)).a-h;"
		"float3 n=normalize(i.n+float3(hx,hy,0)*4.2);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w);"
		"float nd=.42+.58*saturate(dot(n,l))*lerp(.55,1,sh);"
		"float3 v=normalize(Eye.xyz-i.w);float3 r=reflect(-l,n);float rv=saturate(dot(r,v));"
		"float sp=pow(rv,56)*sh;float spark=pow(rv,180)*sh;float3 sun=float3(1.0,.94,.78);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float fr=pow(1-saturate(dot(n,v)),3);"
		"float metal=saturate(i.c.a-1.01);float3 col=a.rgb*nd+sun*(sp*.55+spark*1.15)+env*(.10+fr*.22+metal*.55);"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=saturate(fg+max(0,Fog.w-i.w.y)*Fog.z);"
		"return float4(lerp(col,float3(.62,.78,.98),fg*.55),1);}"
		"D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}"
		"float4 PSS(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w);"
		"float3 v=normalize(Eye.xyz-i.w);float nd=.35+.65*saturate(dot(n,l))*lerp(.55,1,sh);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),64)*sh;float3 env=Env.Sample(SL,reflect(-v,n)).rgb;"
		"float fr=pow(1-saturate(dot(n,v)),2.5);float3 c=i.c.rgb*(.45+.55*nd)+env*(.35+fr*.45)+float3(1,.96,.82)*sp*1.2;"
		"float al=saturate(i.c.a);float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));"
		"return float4(lerp(c,float3(.62,.78,.98),fg*.25),al);}"
		"struct HV{float2 p:POSITION;float4 c:TEXCOORD0;};struct HO{float4 p:SV_POSITION;float4 c:TEXCOORD0;};"
		"HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.c=x.c;return o;}float4 PSH(HO i):SV_Target{return i.c;}"
		"struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}"
		"float4 SSR(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);float z=Depth.Sample(SP,i.uv).r;float2 dir=float2((i.uv.x-.5)*.04,-.025);"
		"float3 r=0;float hit=0;[loop]for(int k=1;k<28;k++){float2 u=i.uv+dir*k;if(any(u<0)||any(u>1))break;float dz=Depth.Sample(SP,u).r;if(dz+0.001<z){r=T0.Sample(SL,u).rgb;hit=1;break;}}"
		"float metal=saturate((z-.08)*3.5)*0.32;return float4(lerp(c.rgb,lerp(c.rgb,r,hit),metal),1);}"
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
	const int W=128,H=128; DWORD* p=new DWORD[W*H];
	D3D11_TEXTURE2D_DESC d={}; d.Width=W;d.Height=H;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd={p,W*4,0};
	for(int y=0;y<H;y++) for(int x=0;x<W;x++){int row=y/16,bx=(x+((row&1)?32:0))&63,by=y&15;BOOL mortar=by<2||bx<2;int n=((x*13+y*7)&15)-7;BYTE r=(BYTE)(mortar?118:198+n),g=(BYTE)(mortar?96:138+n/2),b=(BYTE)(mortar?78:92+n/3),a=(BYTE)(mortar?80:160+n*4);p[y*W+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;}
	if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texBrick))||FAILED(m_dev->CreateShaderResourceView(m_texBrick,NULL,&m_srvBrick))){delete[] p;return FALSE;}
	for(int y=0;y<H;y++) for(int x=0;x<W;x++){int n=((x*17+y*29+(x*y)%31)&31)-15;BYTE v=(BYTE)(145+n);p[y*W+x]=0xff000000|((DWORD)v<<16)|((DWORD)(v*4/5)<<8)|(DWORD)(v*2/3);}
	if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texFloor))||FAILED(m_dev->CreateShaderResourceView(m_texFloor,NULL,&m_srvFloor))){delete[] p;return FALSE;}
	d.ArraySize=6;d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;D3D11_SUBRESOURCE_DATA cs[6]={};
	DWORD* cube=new DWORD[W*H*6];
	for(int f=0;f<6;f++){for(int y=0;y<H;y++)for(int x=0;x<W;x++){float t=(float)y/(H-1);BYTE r=(BYTE)(140+95*(1-t)),g=(BYTE)(175+70*(1-t)),b=(BYTE)(210+45*(1-t));if(f==2){r=(BYTE)(95+40*t);g=(BYTE)(140+55*t);b=(BYTE)(70+30*t);}cube[(f*H+y)*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;}cs[f].pSysMem=cube+f*W*H;cs[f].SysMemPitch=W*4;}
	if(FAILED(m_dev->CreateTexture2D(&d,cs,&m_texEnv))||FAILED(m_dev->CreateShaderResourceView(m_texEnv,NULL,&m_srvEnv))){delete[] cube;delete[] p;return FALSE;}
	delete[] cube;delete[] p;return TRUE;
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

BOOL CS3mView::BakeClearTexture(const wchar_t* text,float alpha)
{
	ReleaseClearTexture();const int w=max(256,m_vw),h=max(96,m_vh/4);BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
	void* bits=NULL;HDC dc=CreateCompatibleDC(NULL);HBITMAP bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,NULL,0);HGDIOBJ old=SelectObject(dc,bm);memset(bits,0,w*h*4);
	{Gdiplus::Graphics g(dc);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);Gdiplus::FontFamily ff(L"Segoe UI");Gdiplus::Font font(&ff,(Gdiplus::REAL)max(32,h/2),Gdiplus::FontStyleBold,Gdiplus::UnitPixel);Gdiplus::StringFormat sf;sf.SetAlignment(Gdiplus::StringAlignmentCenter);sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);Gdiplus::SolidBrush sh(Gdiplus::Color((BYTE)(alpha*150),0,0,0)),fg(Gdiplus::Color((BYTE)(alpha*255),255,230,110));Gdiplus::RectF r(3,3,(Gdiplus::REAL)w,(Gdiplus::REAL)h);g.DrawString(text,-1,&font,r,&sf,&sh);r.X-=3;r.Y-=3;g.DrawString(text,-1,&font,r,&sf,&fg);}
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA sd={bits,(UINT)w*4,0};HRESULT hr=m_dev->CreateTexture2D(&d,&sd,&m_texClear);if(SUCCEEDED(hr))hr=m_dev->CreateShaderResourceView(m_texClear,NULL,&m_srvClear);SelectObject(dc,old);DeleteObject(bm);DeleteDC(dc);m_clearTexW=w;m_clearTexH=h;return SUCCEEDED(hr);
}

void CS3mView::ReleaseDx()
{
	m_ready=FALSE;if(m_imm){m_imm->ClearState();m_imm->Flush();}
	ReleaseClearTexture();S3M_RELEASE(m_srvEnv);S3M_RELEASE(m_texEnv);S3M_RELEASE(m_srvFloor);S3M_RELEASE(m_texFloor);S3M_RELEASE(m_srvBrick);S3M_RELEASE(m_texBrick);
	S3M_RELEASE(m_bsAdd);S3M_RELEASE(m_bsAlpha);S3M_RELEASE(m_bsOpaque);S3M_RELEASE(m_dssOff);S3M_RELEASE(m_dssRead);S3M_RELEASE(m_dssWrite);S3M_RELEASE(m_rsShadow);S3M_RELEASE(m_rsSolid);S3M_RELEASE(m_sampCmp);S3M_RELEASE(m_sampPoint);S3M_RELEASE(m_sampLin);
	S3M_RELEASE(m_vbHud);S3M_RELEASE(m_vbDyn);S3M_RELEASE(m_cbFrame);S3M_RELEASE(m_ilHud);S3M_RELEASE(m_ilSolid);S3M_RELEASE(m_ilPatch);
	S3M_RELEASE(m_psFinal);S3M_RELEASE(m_psDof);S3M_RELEASE(m_psSsr);S3M_RELEASE(m_vsPost);S3M_RELEASE(m_psHud);S3M_RELEASE(m_vsHud);S3M_RELEASE(m_psSolid);S3M_RELEASE(m_vsSolid);S3M_RELEASE(m_psWall);S3M_RELEASE(m_dsTess);S3M_RELEASE(m_hsTess);S3M_RELEASE(m_vsTess);
	S3M_RELEASE(m_shadowSrv);S3M_RELEASE(m_shadowDsv);S3M_RELEASE(m_shadowTex);
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
	, m_bob(0.f), m_anim(0.f), m_won(0)
	, m_clearPhase(CLEAR_IDLE), m_clearT(0.f), m_clearTextA(0.f), m_clearScreenA(0.f)
	, m_clearTextAPrev(-1.f)
	, m_itemsLeft(0)
	, m_baseTempoPos(200), m_basePitchPos(200)
	, m_lastTick(0), m_rng(GetTickCount()), m_genSeed(GetTickCount())
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
	// D3D LookAt ではマス中央＝論理位置のまま（旧 Soft3D の前方 0.8 は 1マスずれの原因）
	ex = m_px;
	ez = m_pz;
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
	DWORD seed=GetTickCount()^((DWORD)ReadSizeFromUi()*2654435761u);
	if(savedata.s3m_seed)seed=(DWORD)savedata.s3m_seed;
	GenerateMazeWithSeed(seed);
}

void CSoft3DMazeDlg::GenerateMazeWithSeed(DWORD seed)
{
	ResetClearFx();
	PersistUi();
	const int n = ReadSizeFromUi();
	if (!AllocGrid(n))
		return;

	m_won = 0;
	m_itemsLeft = 0;
	m_genSeed=seed;
	m_rng=seed;

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

void CSoft3DMazeDlg::RefreshClearTex()
{
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
	m_anim += dt;
	if (m_clearPhase != CLEAR_IDLE) {
		if(savedata.s3m_bob)m_bob += dt * 3.f;
		return;
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
		// 1マス約 0.18s（押しっぱなし連続はせず、移動アニメは付ける）
		const float spd = 1.f / 0.18f;
		const float step = spd * dt;
		if (dist <= step || dist < 1e-4f) {
			m_px = m_pxTarget;
			m_pz = m_pzTarget;
			m_moving = 0;
			MarkVisited();
			TryPickup();
			m_runDirty = 1;
		} else {
			m_px += dx / dist * step;
			m_pz += dz / dist * step;
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
	const float eyeY=.50f+(savedata.s3m_bob?.015f*sinf(m_bob):0.f);
	const float fov=(savedata.s3m_fov==0?55.f:(savedata.s3m_fov==2?90.f:70.f))*(float)(M_PI/180.0);
	S3MFrameCB cb={};cb.viewProj=S3mMatMul(S3mLookAt(ex,eyeY,ez,ex+fx,eyeY,ez+fz,0,1,0),S3mPerspective(fov,(float)w/(float)h,.05f,80.f));
	float lx=-.35f,ly=.88f,lz=-.28f;float llen=sqrtf(lx*lx+ly*ly+lz*lz);lx/=llen;ly/=llen;lz/=llen;cb.lightDir={lx,ly,lz,0};
	const float lDist=24.f;cb.lightVP=S3mMatMul(S3mLookAt(ex+lx*lDist,16.f,ez+lz*lDist,ex,0.f,ez,0,1,0),S3mOrtho(-18.f,18.f,-18.f,18.f,1.f,55.f));
	cb.eyePos={ex,eyeY,ez,1};cb.fogParams={14.f,42.f,.04f,-.2f};cb.dofParams={.55f,.38f,1.1f,m_bob};cb.screenSize={(float)w,(float)h,1.f/w,1.f/h};cb.misc={m_clearScreenA,m_clearTextA,1.f/tanf(fov*.5f),(float)savedata.s3m_bob};
	D3D11_MAPPED_SUBRESOURCE map={};if(FAILED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map)))return;memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);
	const UINT maxV=m_view.m_vbDynBytes/sizeof(S3MVertex);S3MVertex* v=new S3MVertex[maxV];UINT floorBeg=0,nFloor=0,wallBeg=0,nWall=0,transBeg=0,nTrans=0;int phase=0;
	auto put=[&](float x,float y,float z,float nx,float ny,float nz,float u,float vv,float r,float g,float b,float a)->BOOL{UINT n=nFloor+nWall+nTrans;if(n>=maxV)return FALSE;v[n]={x,y,z,nx,ny,nz,u,vv,r,g,b,a};if(phase==2)nTrans++;else if(phase==1)nWall++;else nFloor++;return TRUE;};
	auto tri=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float nx,float ny,float nz,float r,float g,float b,float a){put(x0,y0,z0,nx,ny,nz,0,0,r,g,b,a);put(x1,y1,z1,nx,ny,nz,1,0,r,g,b,a);put(x2,y2,z2,nx,ny,nz,1,1,r,g,b,a);};
	auto quad=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float r,float g,float b,float a){tri(x0,y0,z0,x1,y1,z1,x2,y2,z2,nx,ny,nz,r,g,b,a);tri(x0,y0,z0,x2,y2,z2,x3,y3,z3,nx,ny,nz,r,g,b,a);};
	auto patch1=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float r,float g,float b,float a){
		put(x0,y0,z0,nx,ny,nz,0,1,r,g,b,a);put(x1,y1,z1,nx,ny,nz,1,1,r,g,b,a);put(x2,y2,z2,nx,ny,nz,1,0,r,g,b,a);put(x3,y3,z3,nx,ny,nz,0,0,r,g,b,a);};
	auto patch=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float r,float g,float b,float a){
		const int N=2;for(int j=0;j<N;j++)for(int i=0;i<N;i++){
			float u0=(float)i/N,u1=(float)(i+1)/N,v0=(float)j/N,v1=(float)(j+1)/N;
			auto L=[&](float u,float vv,float& X,float& Y,float& Z){float ax=x0+(x1-x0)*u,ay=y0+(y1-y0)*u,az=z0+(z1-z0)*u;float bx=x3+(x2-x3)*u,by=y3+(y2-y3)*u,bz=z3+(z2-z3)*u;X=ax+(bx-ax)*vv;Y=ay+(by-ay)*vv;Z=az+(bz-az)*vv;};
			float p00x,p00y,p00z,p10x,p10y,p10z,p11x,p11y,p11z,p01x,p01y,p01z;L(u0,v0,p00x,p00y,p00z);L(u1,v0,p10x,p10y,p10z);L(u1,v1,p11x,p11y,p11z);L(u0,v1,p01x,p01y,p01z);
			patch1(p00x,p00y,p00z,p10x,p10y,p10z,p11x,p11y,p11z,p01x,p01y,p01z,nx,ny,nz,r,g,b,a);
		}
	};
	const float wallH=1.15f,drawDist=24.f,rad=drawDist+2.f;const int ix0=max(0,(int)(m_px-rad)),ix1=min(m_n-1,(int)(m_px+rad)),iz0=max(0,(int)(m_pz-rad)),iz1=min(m_n-1,(int)(m_pz+rad));
	auto open=[&](int x,int z)->BOOL{if(x<0||z<0||x>=m_n||z>=m_n)return FALSE;BYTE c=CellAt(x,z);return c!=CELL_WALL&&c!=CELL_WINDOW;};
	auto vis=[&](int x,int z)->BOOL{float dx=x+.5f-ex,dz=z+.5f-ez,lxv=dx*rx+dz*rz,lzv=dx*fx+dz*fz;return fabsf(lxv)<20.f&&lzv<drawDist&&lzv>-2.5f;};
	for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){BYTE c=CellAt(x,z);if((c==CELL_WALL||c==CELL_WINDOW)||!vis(x,z))continue;float k=VisitAt(x,z)?1.f:.78f;float mir=((x+z)&1)?1.08f:1.f;patch((float)x,0,(float)z,(float)x,0,z+1.f,x+1.f,0,z+1.f,x+1.f,0,(float)z,0,1,0,.78f*k,.62f*k,.48f*k,mir);}
	wallBeg=nFloor;phase=1;
	for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){if(CellAt(x,z)!=CELL_WALL||!vis(x,z))continue;float xa=(float)x,xb=x+1.f,za=(float)z,zb=z+1.f;
		if(open(x-1,z))patch(xa,0,za,xa,0,zb,xa,wallH,zb,xa,wallH,za,-1,0,0,1,1,1,1);
		if(open(x+1,z))patch(xb,0,zb,xb,0,za,xb,wallH,za,xb,wallH,zb,1,0,0,1,1,1,1);
		if(open(x,z-1))patch(xb,0,za,xa,0,za,xa,wallH,za,xb,wallH,za,0,0,-1,1,1,1,1);
		if(open(x,z+1))patch(xa,0,zb,xb,0,zb,xb,wallH,zb,xa,wallH,zb,0,0,1,1,1,1,1);
		patch(xa,wallH,za,xb,wallH,za,xb,wallH,zb,xa,wallH,zb,0,1,0,1,1,1,1);}
	struct XL{float d;int x,z;BYTE c;};XL xl[1536];int nc=0;
	for(int z=iz0;z<=iz1&&nc<1536;z++)for(int x=ix0;x<=ix1&&nc<1536;x++){BYTE c=CellAt(x,z);if(!vis(x,z))continue;
		if(c==CELL_WINDOW||c==CELL_GOAL||c==CELL_START||(c>=CELL_TEMPO&&c<=CELL_EQ)||(c==CELL_WALL&&((x*3+z*7)&7)==0))xl[nc++]={((x+.5f-ex)*fx+(z+.5f-ez)*fz),x,z,c};}
	for(int i=1;i<nc;i++){XL q=xl[i];int j=i-1;while(j>=0&&xl[j].d<q.d){xl[j+1]=xl[j];j--;}xl[j+1]=q;}
	transBeg=nFloor+nWall;phase=2;
	auto spinOcta=[&](float cx,float cy,float cz,float s,float ang,float rr,float gg,float bb,float a){
		float cs=cosf(ang),sn=sinf(ang);float ox[4],oz[4];
		auto R=[&](float px,float pz,int i){ox[i]=cx+px*cs-pz*sn;oz[i]=cz+px*sn+pz*cs;};
		R(s,0,0);R(0,s,1);R(-s,0,2);R(0,-s,3);float top=cy+s*1.15f,bot=cy-s*.95f;
		auto face=[&](float ax,float ay,float az,float bx,float by,float bz,float cx2,float cy2,float cz2){
			float nx=(by-ay)*(cz2-az)-(bz-az)*(cy2-ay),ny=(bz-az)*(cx2-ax)-(bx-ax)*(cz2-az),nz=(bx-ax)*(cy2-ay)-(by-ay)*(cx2-ax);float nl=sqrtf(nx*nx+ny*ny+nz*nz)+1e-6f;nx/=nl;ny/=nl;nz/=nl;
			tri(ax,ay,az,bx,by,bz,cx2,cy2,cz2,nx,ny,nz,rr,gg,bb,a);};
		for(int i=0;i<4;i++){int j=(i+1)&3;face(ox[i],cy,oz[i],ox[j],cy,oz[j],cx,top,cz);face(ox[j],cy,oz[j],ox[i],cy,oz[i],cx,bot,cz);}
	};
	for(int i=0;i<nc;i++){int x=xl[i].x,z=xl[i].z;BYTE c=xl[i].c;float xa=(float)x,xb=x+1.f,za=(float)z,zb=z+1.f;
		if(c==CELL_WINDOW){float rr=.55f,gg=.82f,bb=1.f,a=.28f;
			if(open(x-1,z))quad(xa+.02f,0.02f,zb,xa+.02f,0.02f,za,xa+.02f,wallH-.02f,za,xa+.02f,wallH-.02f,zb,-1,0,0,rr,gg,bb,a);
			if(open(x+1,z))quad(xb-.02f,0.02f,za,xb-.02f,0.02f,zb,xb-.02f,wallH-.02f,zb,xb-.02f,wallH-.02f,za,1,0,0,rr,gg,bb,a);
			if(open(x,z-1))quad(xa,0.02f,za+.02f,xb,0.02f,za+.02f,xb,wallH-.02f,za+.02f,xa,wallH-.02f,za+.02f,0,0,-1,rr,gg,bb,a);
			if(open(x,z+1))quad(xb,0.02f,zb-.02f,xa,0.02f,zb-.02f,xa,wallH-.02f,zb-.02f,xb,wallH-.02f,zb-.02f,0,0,1,rr,gg,bb,a);}
		else if(c==CELL_WALL){float rr=.78f,gg=.84f,bb=.92f,a=.26f,inset=.04f;
			if(open(x-1,z))quad(xa+inset,.12f,za+.18f,xa+inset,.12f,zb-.18f,xa+inset,wallH-.12f,zb-.18f,xa+inset,wallH-.12f,za+.18f,-1,0,0,rr,gg,bb,a);
			if(open(x+1,z))quad(xb-inset,.12f,zb-.18f,xb-inset,.12f,za+.18f,xb-inset,wallH-.12f,za+.18f,xb-inset,wallH-.12f,zb-.18f,1,0,0,rr,gg,bb,a);
			if(open(x,z-1))quad(xa+.18f,.12f,za+inset,xb-.18f,.12f,za+inset,xb-.18f,wallH-.12f,za+inset,xa+.18f,wallH-.12f,za+inset,0,0,-1,rr,gg,bb,a);
			if(open(x,z+1))quad(xb-.18f,.12f,zb-inset,xa+.18f,.12f,zb-inset,xa+.18f,wallH-.12f,zb-inset,xb-.18f,wallH-.12f,zb-inset,0,0,1,rr,gg,bb,a);}
		else if(c==CELL_GOAL){spinOcta(x+.5f,.48f+.05f*sinf(m_anim*2.6f),z+.5f,.30f,m_anim*2.1f,1.f,.88f,.25f,.38f);}
		else if(c==CELL_START){spinOcta(x+.5f,.42f+.04f*sinf(m_anim*2.1f+1.f),z+.5f,.24f,-m_anim*1.7f,.30f,1.f,.55f,.36f);}
		else{float rr=1,gg=.5f,bb=.8f;if(c==CELL_TEMPO){rr=.3f;gg=1;bb=.5f;}else if(c==CELL_PITCH_UP){rr=1;gg=.65f;bb=.25f;}else if(c==CELL_PITCH_DN){rr=.35f;gg=.55f;bb=1;}else if(c==CELL_NEXT){rr=1;gg=.2f;bb=.35f;}else if(c==CELL_EQ){rr=.7f;gg=.35f;bb=1;}
			spinOcta(x+.5f,.40f+.07f*sinf(m_anim*2.4f+x*.7f+z),z+.5f,.17f,m_anim*(1.8f+(c&3)*.3f)+x,rr,gg,bb,.42f);}
	}
	for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){BYTE c=CellAt(x,z);if(c==CELL_WALL||c==CELL_WINDOW||!vis(x,z))continue;if(((x+z)&1)==0)continue;
		quad((float)x,.015f,(float)z,(float)x,.015f,z+1.f,x+1.f,.015f,z+1.f,x+1.f,.015f,(float)z,0,1,0,.75f,.85f,.95f,.20f);}
	if(FAILED(dc->Map(m_view.m_vbDyn,0,D3D11_MAP_WRITE_DISCARD,0,&map))){delete[] v;return;}memcpy(map.pData,v,(nFloor+nWall+nTrans)*sizeof(S3MVertex));dc->Unmap(m_view.m_vbDyn,0);delete[] v;
	UINT stride=sizeof(S3MVertex),off=0;ID3D11ShaderResourceView* ns[5]={NULL,NULL,NULL,NULL,NULL};ID3D11RenderTargetView* nullRtv=NULL;
	auto bindCB=[&](){dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->HSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->DSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);};
	auto drawFloorWall=[&](BOOL colorPass){
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);dc->DSSetSamplers(0,1,&m_view.m_sampLin);dc->OMSetDepthStencilState(m_view.m_dssWrite,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);
		if(colorPass){dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);dc->PSSetShaderResources(3,1,&m_view.m_srvEnv);}
		dc->IASetInputLayout(m_view.m_ilPatch);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
		dc->VSSetShader(m_view.m_vsTess,NULL,0);dc->HSSetShader(m_view.m_hsTess,NULL,0);dc->DSSetShader(m_view.m_dsTess,NULL,0);dc->PSSetShader(colorPass?m_view.m_psWall:NULL,NULL,0);
		dc->DSSetShaderResources(0,1,&m_view.m_srvFloor);dc->PSSetShaderResources(0,1,&m_view.m_srvFloor);if(nFloor)dc->Draw(nFloor,floorBeg);
		dc->DSSetShaderResources(0,1,&m_view.m_srvBrick);dc->PSSetShaderResources(0,1,&m_view.m_srvBrick);if(nWall)dc->Draw(nWall,wallBeg);
		dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->DSSetShaderResources(0,1,ns);dc->PSSetShaderResources(0,5,ns);
	};
	auto drawTrans=[&](BOOL colorPass){
		if(!nTrans)return;
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		if(colorPass){
			dc->OMSetDepthStencilState(m_view.m_dssRead,0);
			dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
			dc->PSSetSamplers(2,1,&m_view.m_sampCmp);
			dc->PSSetShaderResources(3,1,&m_view.m_srvEnv);
			dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);
		}
		dc->Draw(nTrans,transBeg);dc->PSSetShaderResources(0,5,ns);
	};
	{S3MMat camVP=cb.viewProj;cb.viewProj=cb.lightVP;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
	D3D11_VIEWPORT svp={0,0,(float)CS3mView::S3M_SHADOW_SIZE,(float)CS3mView::S3M_SHADOW_SIZE,0,1};dc->RSSetViewports(1,&svp);dc->RSSetState(m_view.m_rsShadow);
	dc->OMSetRenderTargets(1,&nullRtv,m_view.m_shadowDsv);dc->ClearDepthStencilView(m_view.m_shadowDsv,D3D11_CLEAR_DEPTH,1.f,0);
	drawFloorWall(FALSE);dc->OMSetDepthStencilState(m_view.m_dssWrite,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);drawTrans(FALSE);
	dc->OMSetRenderTargets(1,&nullRtv,NULL);cb.viewProj=camVP;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}}
	D3D11_VIEWPORT vp={0,0,(float)w,(float)h,0,1};dc->RSSetViewports(1,&vp);dc->RSSetState(m_view.m_rsSolid);float bg[4]={.55f,.72f,.92f,1};dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,m_view.m_dsv);dc->ClearRenderTargetView(m_view.m_sceneRtv,bg);dc->ClearDepthStencilView(m_view.m_dsv,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
	drawFloorWall(TRUE);
	// ポスト → BB。半透明は α ブレンドが確実に効くよう Final の後に BB へ
	dc->OMSetDepthStencilState(m_view.m_dssOff,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetSamplers(1,1,&m_view.m_sampPoint);
	dc->OMSetRenderTargets(1,&m_view.m_postRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_sceneSrv);dc->PSSetShaderResources(2,1,&m_view.m_dsSrv);dc->PSSetShader(m_view.m_psSsr,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_postSrv);dc->PSSetShaderResources(2,1,&m_view.m_dsSrv);dc->PSSetShader(m_view.m_psDof,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_sceneSrv);dc->PSSetShader(m_view.m_psFinal,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	if(nTrans){dc->OMSetRenderTargets(1,&m_view.m_bbRtv,m_view.m_dsv);drawTrans(TRUE);}
	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);dc->OMSetDepthStencilState(m_view.m_dssOff,0);
	const UINT maxH=m_view.m_vbHudBytes/sizeof(S3MHudVertex);S3MHudVertex* hv=new S3MHudVertex[maxH];UINT hn=0;
	auto hp=[&](float px,float py,float r,float g,float b,float a){if(hn<maxH)hv[hn++]={(px/(float)w)*2.f-1.f,1.f-(py/(float)h)*2.f,r,g,b,a};};
	auto hq=[&](float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,float r,float g,float b,float a){hp(ax,ay,r,g,b,a);hp(bx,by,r,g,b,a);hp(cx,cy,r,g,b,a);hp(ax,ay,r,g,b,a);hp(cx,cy,r,g,b,a);hp(dx,dy,r,g,b,a);};
	if(savedata.s3m_show_map){int cells=S3mClampMapSize(savedata.s3m_minimap);float mpix=(float)min(w,h)*.28f,cs=mpix/cells,mcx=w-12-mpix*.5f,mcy=12+mpix*.5f,pad=mpix*.5f+6;hq(mcx-pad,mcy-pad,mcx+pad,mcy-pad,mcx+pad,mcy+pad,mcx-pad,mcy+pad,.04f,.05f,.08f,.72f);float half=cells*.5f;
		for(int z=max(0,(int)(m_pz-half-2));z<=min(m_n-1,(int)(m_pz+half+2));z++)for(int x=max(0,(int)(m_px-half-2));x<=min(m_n-1,(int)(m_px+half+2));x++){float qx[4],qy[4];for(int k=0;k<4;k++){float wx=x+((k==1||k==2)?1.f:0.f),wz=z+((k>=2)?1.f:0.f),mx,my;WorldToMap(wx,wz,mx,my);qx[k]=mcx+mx*cs;qy[k]=mcy+my*cs;}BYTE c=CellAt(x,z);float rr=.16f,gg=.19f,bb=.27f,a=.62f;if(c==CELL_WALL){rr=.55f;gg=.37f;bb=.25f;a=.86f;}else if(c==CELL_WINDOW){rr=.25f;gg=.58f;bb=.86f;}else if(c==CELL_GOAL){rr=1;gg=.8f;bb=.18f;}else if(c==CELL_START){rr=.2f;gg=.9f;bb=.45f;}else if(VisitAt(x,z)){rr=.35f;gg=.48f;bb=.3f;}hq(qx[0],qy[0],qx[1],qy[1],qx[2],qy[2],qx[3],qy[3],rr,gg,bb,a);}
		float ps=cs*.32f;hp(mcx,mcy-ps,1,.92f,.3f,1);hp(mcx-ps*.6f,mcy+ps*.5f,1,.92f,.3f,1);hp(mcx+ps*.6f,mcy+ps*.5f,1,.92f,.3f,1);float nx,ny,xx,xy;WorldToMap(m_px,m_pz-1,nx,ny);WorldToMap(m_px+1,m_pz,xx,xy);float ccx=mcx+pad-18,ccy=mcy-pad+18;hp(ccx+nx*11,ccy+ny*11,1,.2f,.2f,1);hp(ccx-nx*2-xx*5,ccy-ny*2-xy*5,1,.2f,.2f,1);hp(ccx-nx*2+xx*5,ccy-ny*2+xy*5,1,.2f,.2f,1);}
	if(m_clearScreenA>.01f)hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,0,0,0,m_clearScreenA);
	if(hn&&SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);UINT hs=sizeof(S3MHudVertex);dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);dc->IASetInputLayout(m_view.m_ilHud);dc->VSSetShader(m_view.m_vsHud,NULL,0);dc->PSSetShader(m_view.m_psHud,NULL,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(hn,0);}delete[] hv;
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
	if(!m_view.InitDx()){
		MessageBox(LL14(L"DirectX 11 の初期化に失敗しました。",L"DirectX 11 initialization failed.",L"Échec de l'initialisation de DirectX 11.",L"Inizializzazione DirectX 11 non riuscita.",L"Error al iniciar DirectX 11.",L"DirectX 11 초기화에 실패했습니다.",L"DirectX 11 初始化失败。",L"فشل تهيئة DirectX 11.",L"Не удалось инициализировать DirectX 11.",L"DirectX 11 konnte nicht initialisiert werden.",L"Falha ao iniciar o DirectX 11.",L"Initialisatie van DirectX 11 mislukt.",L"Nie udało się zainicjować DirectX 11.",L"DirectX 11 başlatılamadı."),NULL,MB_OK|MB_ICONERROR);
		DestroyWindow();
		return FALSE;
	}
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
