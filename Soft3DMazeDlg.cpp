// Soft3DMazeDlg.cpp — Soft3D 迷路（ミニマップ／訪問／コンテキスト設定／曲連動）

#include "stdafx.h"
#include "ogg.h"
#include "Soft3DMazeDlg.h"
#include "CMediaPlayerDlg.h"
#include "CCustomPopupMenu.h"
#include "oggDlg.h"
#include "DatArchive.h"
#include <math.h>
#include <new>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "Soft3DTextD2D.h"
#include "Soft3DGameSfx.h"
#include "Soft3DTexRes.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void MpPersistSavedataQuick();
extern void MpTaskbarNextTrack();
extern void MpTaskbarPrevTrack();
extern CMediaPlayerDlg* mp;
extern COggDlg* og;
extern int tempo;
extern int pitch;
extern int playf;
extern int mode;

namespace {

struct S3MFloat4 { float x, y, z, w; };
struct S3MMat { float m[16]; };
struct S3MVertex { float x,y,z, nx,ny,nz, u,v, r,g,b,a; };
struct S3MHudVertex { float x,y, u,v, r,g,b,a; };
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

static BOOL S3mWorldToNdc(const S3MMat& vp, float x, float y, float z, float& ndcX, float& ndcY, float& clipW)
{
	const float ox = x*vp.m[0] + y*vp.m[4] + z*vp.m[8]  + vp.m[12];
	const float oy = x*vp.m[1] + y*vp.m[5] + z*vp.m[9]  + vp.m[13];
	const float ow = x*vp.m[3] + y*vp.m[7] + z*vp.m[11] + vp.m[15];
	if (ow <= 0.06f) return FALSE;
	ndcX = ox / ow;
	ndcY = oy / ow;
	clipW = ow;
	if (ndcX < -1.3f || ndcX > 1.3f || ndcY < -1.25f || ndcY > 1.25f) return FALSE;
	return TRUE;
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
	CCC_ApplyWindowIconFromTemplate(this, IDD);
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
	dc.TextOut(L, y, LL14(L"凡例（Soft3D と説明）", L"Legend (Soft3D & description)", L"Légende (Soft3D et description)", L"Legenda (Soft3D e descrizione)", L"Leyenda (Soft3D y descripción)",
		L"범례(Soft3D와 설명)", L"图例（Soft3D 与说明）", L"Legend (Soft3D & description)", L"Legend (Soft3D & description)", L"Legende (Soft3D & Beschreibung)", L"Legenda (Soft3D e descrição)", L"Legenda (Soft3D en uitleg)",
		L"Legenda (Soft3D i opis)", L"Gösterge (Soft3D ve açıklama)"));
	y += lh + 2;

	const int sw = max(16, lh + 2);
	const int softColW = max(150, contentW * 38 / 100);
	const int descX = L + softColW + 8;
	const int rowH = lh + 4;
	auto line = [&](LPCTSTR t) { dc.TextOut(L, y, t); y += lh; };
	auto legendHead = [&]() {
		dc.FillSolidRect(L, y, contentW, rowH, RGB(232, 230, 242));
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(L + 4, y + 2, L"Soft3D");
		dc.TextOut(descX, y + 2, LL14(L"説明", L"Description", L"Description", L"Descrizione", L"Descripción",
			L"설명", L"说明", L"Description", L"Описание", L"Beschreibung", L"Descrição", L"Beschrijving", L"Opis", L"Açıklama"));
		y += rowH;
	};
	auto legendRow = [&](COLORREF col, LPCTSTR softName, LPCTSTR desc) {
		const BOOL alt = ((y / rowH) & 1) != 0;
		if (alt) dc.FillSolidRect(L, y, contentW, rowH, RGB(244, 244, 250));
		CRect swR(L + 3, y + 2, L + 3 + sw, y + rowH - 2);
		dc.FillSolidRect(swR, col);
		CBrush fr(RGB(90, 90, 110));
		dc.FrameRect(swR, &fr);
		dc.SetTextColor(RGB(40, 40, 55));
		dc.TextOut(L + 3 + sw + 6, y + 2, softName);
		dc.SetTextColor(RGB(70, 70, 85));
		CRect dr(descX, y + 2, L + contentW - 2, y + rowH);
		dc.DrawText(desc, -1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		y += rowH;
	};

	legendHead();
	legendRow(RGB(42, 78, 55),
		LL14(L"床（未訪問）", L"Floor (unvisited)", L"Sol (non visité)", L"Pavimento", L"Suelo", L"바닥(미방문)", L"地板（未走）", L"Floor", L"Пол", L"Boden", L"Chão", L"Vloer", L"Podłoga", L"Zemin"),
		LL14(L"歩ける通路。ミニマップでは暗い緑", L"Walkable path; dark green on maps", L"Passage praticable", L"Corridoio", L"Pasillo", L"통로", L"可走通道", L"Walkable", L"Проход", L"Gang", L"Corredor", L"Pad", L"Korytarz", L"Yol"));
	legendRow(RGB(70, 130, 200),
		LL14(L"床（訪問済み）", L"Floor (visited)", L"Sol (visité)", L"Visitato", L"Visitado", L"바닥(방문)", L"地板（已走）", L"Visited", L"Посещено", L"Besucht", L"Visitado", L"Bezocht", L"Odwiedzone", L"Gezildi"),
		LL14(L"通ったマス。青く着色＋3Dでは半透明の板", L"Visited cells: blue tint; translucent plate in 3D", L"Cases visitées", L"Celle visitate", L"Celdas visitadas", L"방문 칸", L"走过的格子", L"Visited tint", L"Посещённые", L"Besuchte", L"Visitados", L"Bezocht", L"Odwiedzone", L"Gezilen"));
	legendRow(RGB(170, 215, 245),
		LL14(L"鏡床", L"Mirror floor", L"Sol miroir", L"Pavimento specchio", L"Suelo espejo", L"거울 바닥", L"镜面地板", L"Mirror floor", L"Зеркальный пол", L"Spiegelboden", L"Chão espelho", L"Spiegelvloer", L"Lustrzana podłoga", L"Ayna zemin"),
		LL14(L"銀シアンの通路。歩ける。天井の雲／水滴を映す（地図では通常の床）", L"Silver-cyan path; walkable; reflects clouds/drips (normal floor on maps)", L"Passage miroir; reflète", L"Corridoio specchio", L"Pasillo espejo", L"은청 통로, 걸을 수 있음(지도는 일반)", L"银青通道，可走（地图为普通地板）", L"Walkable mirror floor", L"Проходимо", L"Begehbar", L"Andável", L"Begaanbaar", L"Przechodnie", L"Yürünür"));
	legendRow(RGB(90, 170, 220),
		LL14(L"鏡壁", L"Mirror wall", L"Mur miroir", L"Muro specchio", L"Pared espejo", L"거울 벽", L"镜面墙", L"Mirror wall", L"Зеркальная стена", L"Spiegelwand", L"Parede espelho", L"Spiegelmuur", L"Ściana lustrzana", L"Ayna duvar"),
		LL14(L"銀シアンの壁。通れない。反射あり", L"Silver-cyan wall; blocked; reflective", L"Mur miroir; bloqué", L"Muro specchio; bloccato", L"Pared espejo; bloquea", L"은청 벽, 통과 불가·반사", L"银青墙，不可走，有反射", L"Blocked mirror", L"Непроходимо", L"Nicht begehbar", L"Bloqueia", L"Blokkeert", L"Nieprzechodnie", L"Geçilmez"));
	legendRow(RGB(200, 90, 255),
		LL14(L"ポータル", L"Portal", L"Portail", L"Portale", L"Portal", L"포털", L"传送门", L"Portal", L"Портал", L"Portal", L"Portal", L"Portaal", L"Portal", L"Portal"),
		LL14(L"紫の輪。同階の対になるポータルへ瞬間移動", L"Purple ring; teleports to its paired portal on the same floor", L"Anneau violet; téléporte vers le portail jumelé", L"Anello viola; teletrasporto al portale gemello", L"Anillo violeta; teletransporta al portal pareja", L"보라 고리, 같은 층의 짝 포털로 이동", L"紫环，传送到同层配对门", L"Same-floor warp pair", L"Телепорт на той же этаж", L"Teleport auf dieselbe Etage", L"Teleporta no mesmo piso", L"Teleport op dezelfde verdieping", L"Teleport na to samo piętro", L"Aynı kata ışınlar"));
	legendRow(RGB(255, 210, 70),
		LL14(L"鍵", L"Key", L"Clé", L"Chiave", L"Llave", L"열쇠", L"钥匙", L"Key", L"Ключ", L"Schlüssel", L"Chave", L"Sleutel", L"Klucz", L"Anahtar"),
		LL14(L"金〜黄緑。拾うと所持。扉を開けるために使う（1扉で1消費）", L"Gold–chartreuse; pick up to hold. Spends 1 to open a door", L"Or–vert; ramasser. 1 pour ouvrir une porte", L"Oro–verde; raccogli. 1 per una porta", L"Oro–verde; recoger. 1 por puerta", L"금~연두. 줍면 소지. 문 1개에 1개 소비", L"金～黄绿。拾取后持有。开一扇门消耗1把", L"Pick up; spends 1 per door", L"Поднять; 1 на дверь", L"Aufheben; 1 pro Tür", L"Pegar; 1 por porta", L"Oppakken; 1 per deur", L"Podnieś; 1 na drzwi", L"Al; kapı başına 1"));
	legendRow(RGB(160, 120, 90),
		LL14(L"扉", L"Door", L"Porte", L"Porta", L"Puerta", L"문", L"门", L"Door", L"Дверь", L"Tür", L"Porta", L"Deur", L"Drzwi", L"Kapı"),
		LL14(L"金属の扉。鍵が無いと通れない。開くと床になり再入場可", L"Metal door; blocked without a key. Opens to floor (stays open)", L"Porte métallique; clé requise. Reste ouverte", L"Porta metallica; serve chiave. Resta aperta", L"Puerta metálica; necesita llave. Queda abierta", L"금속 문. 열쇠 없으면 통과 불가. 열리면 바닥", L"金属门。无钥匙不可过。打开后变地板", L"Needs key; stays open", L"Нужен ключ; остаётся открытой", L"Schlüssel nötig; bleibt offen", L"Precisa de chave; fica aberta", L"Sleutel nodig; blijft open", L"Potrzebny klucz; zostaje otwarte", L"Anahtar gerekir; açık kalır"));
	legendRow(RGB(92, 62, 44),
		LL14(L"壁", L"Wall", L"Mur", L"Muro", L"Pared", L"벽", L"墙", L"Wall", L"Стена", L"Wand", L"Parede", L"Muur", L"Ściana", L"Duvar"),
		LL14(L"灰茶の箱。通れない。地図では太めの線で表示", L"Grey-brown boxes; blocked. Drawn thicker on maps", L"Boîtes; impassables", L"Scatole; bloccano", L"Cajas; bloquean", L"회색 상자, 통과 불가", L"灰盒，不可走", L"Blocked walls", L"Непроходимо", L"Nicht begehbar", L"Bloqueia", L"Blokkeert", L"Nieprzechodnie", L"Geçilmez"));
	legendRow(RGB(48, 130, 190),
		LL14(L"窓", L"Window", L"Fenêtre", L"Finestra", L"Ventana", L"창", L"窗", L"Window", L"Окно", L"Fenster", L"Janela", L"Raam", L"Okno", L"Pencere"),
		LL14(L"水色ネオン。壁と同様通れない装飾（扉ではない）", L"Cyan neon; blocked like walls (not a door)", L"Néon cyan; bloqué", L"Neon ciano; bloccata", L"Neón cian; bloquea", L"하늘색 네온, 통과 불가(문이 아님)", L"水色霓虹，不可走（不是门）", L"Blocked accent", L"Не дверь", L"Kein Tor", L"Não é porta", L"Geen deur", L"Nie drzwi", L"Kapı değil"));
	legendRow(RGB(55, 220, 120),
		LL14(L"スタート", L"Start", L"Départ", L"Partenza", L"Inicio", L"시작", L"起点", L"Start", L"Старт", L"Start", L"Início", L"Start", L"Start", L"Başlangıç"),
		LL14(L"緑のネオン。地上の開始位置", L"Green neon; starting cell on ground floor", L"Néon vert; départ", L"Neon verde; partenza", L"Neón verde; inicio", L"초록 네온, 시작", L"绿色霓虹，起点", L"Start cell", L"Старт", L"Startfeld", L"Início", L"Start", L"Start", L"Başlangıç"));
	legendRow(RGB(255, 210, 40),
		LL14(L"ゴール", L"Goal", L"But", L"Traguardo", L"Meta", L"골", L"终点", L"Goal", L"Цель", L"Ziel", L"Gol", L"Doel", L"Cel", L"Hedef"),
		LL14(L"金ネオン。到達でクリア（階は難易度次第）", L"Gold neon; reach to clear (floor depends on difficulty)", L"Néon or; but", L"Neon oro; traguardo", L"Neón dorado; meta", L"금 네온, 클리어", L"金色霓虹，通关", L"Clear goal", L"Цель", L"Ziel", L"Gol", L"Doel", L"Cel", L"Hedef"));
	legendRow(RGB(255, 148, 40),
		LL14(L"階段（下り）", L"Stairs (down)", L"Escaliers ↓", L"Scale ↓", L"Escaleras ↓", L"계단 ↓", L"楼梯↓", L"Stairs ↓", L"Лестница ↓", L"Treppe ↓", L"Escadas ↓", L"Trap ↓", L"Schody ↓", L"Merdiven ↓"),
		LL14(L"橙色。矢印方向へ斜めに2マス下へ（壁1マス跨ぎ）", L"Orange; diagonal down 2 cells along the arrow", L"Orange; descendre en diagonale", L"Arancio; scendere in diagonale", L"Naranja; bajar en diagonal", L"주황, 화살표 방향 대각 2칸 하강", L"橙色，沿箭头斜向2格下楼", L"Diagonal down", L"По диагонали вниз", L"Diagonal abwärts", L"Diagonal descer", L"Diagonaal omlaag", L"Po przekątnej w dół", L"Çapraz aşağı"));
	legendRow(RGB(60, 220, 255),
		LL14(L"階段（上り）", L"Stairs (up)", L"Escaliers ↑", L"Scale ↑", L"Escaleras ↑", L"계단 ↑", L"楼梯↑", L"Stairs ↑", L"Лестница ↑", L"Treppe ↑", L"Escadas ↑", L"Trap ↑", L"Schody ↑", L"Merdiven ↑"),
		LL14(L"水色。矢印方向へ斜めに2マス上へ。半透明で向こうが見える", L"Cyan; diagonal up 2 cells. Semi-transparent to see through", L"Cyan; monter en diagonale; semi-transparent", L"Ciano; salire in diagonale", L"Cian; subir en diagonal", L"하늘색, 대각 2칸 상승·반투명", L"水色，斜向2格上楼，半透明", L"Diagonal up", L"По диагонали вверх", L"Diagonal aufwärts", L"Diagonal subir", L"Diagonaal omhoog", L"Po przekątnej w górę", L"Çapraz yukarı"));
	legendRow(RGB(255, 235, 80),
		LL14(L"あなた（プレイヤー）", L"You (player)", L"Vous", L"Tu", L"Tú", L"당신", L"你", L"You", L"Вы", L"Sie", L"Você", L"Jij", L"Ty", L"Siz"),
		LL14(L"黄マーク。進行方向がミニマップの上", L"Yellow mark; forward is up on the minimap", L"Marque jaune; avant en haut", L"Marca gialla", L"Marca amarilla", L"노란 표시, 진행=위", L"黄标，前进朝上", L"Forward = up", L"Вперёд вверх", L"Vorwärts oben", L"Frente cima", L"Vooruit omhoog", L"Przód u góry", L"İleri yukarı"));
	legendRow(RGB(160, 120, 80),
		LL14(L"地上テーマ", L"Ground theme", L"Thème sol", L"Tema terra", L"Tema planta", L"지상 테마", L"地面主题", L"Ground", L"Поверхность", L"Erdgeschoss", L"Térreo", L"Begane grond", L"Parter", L"Zemin"),
		LL14(L"レンガ壁＋草木。地上のみ", L"Brick walls with plants (ground only)", L"Murs de briques", L"Muri di mattoni", L"Ladrillos", L"벽돌+풀", L"砖墙与草木", L"Brick+plants", L"Кирпич", L"Ziegel", L"Tijolo", L"Baksteen", L"Cegła", L"Tuğla"));
	legendRow(RGB(90, 130, 170),
		LL14(L"地下1テーマ", L"B1 theme", L"Thème S1", L"Tema S1", L"Tema S1", L"지하1 테마", L"地下1主题", L"B1", L"B1", L"UG1", L"S1", L"K1", L"P1", L"B1"),
		LL14(L"湿った大割り石（レンガではない）", L"Wet dungeon stone slabs (not brick)", L"Pierre humide", L"Pietra umida", L"Piedra húmeda", L"젖은 돌", L"潮湿大石块", L"Wet stone", L"Камень", L"Nasser Stein", L"Pedra úmida", L"Nat gesteente", L"Mokry kamień", L"Islak taş"));
	legendRow(RGB(150, 95, 60),
		LL14(L"地下2テーマ", L"B2 theme", L"Thème S2", L"Tema S2", L"Tema S2", L"지하2 테마", L"地下2主题", L"B2", L"B2", L"UG2", L"S2", L"K2", L"P2", L"B2"),
		LL14(L"錆びた金属パネル＋リベット", L"Rusty metal panels with rivets", L"Panneaux métal rouillés", L"Pannelli metallici", L"Paneles metálicos", L"녹슨 금속", L"锈蚀金属板", L"Rusty metal", L"Металл", L"Rostiges Metall", L"Metal enferrujado", L"Roestig metaal", L"Zardziały metal", L"Paslı metal"));
	legendRow(RGB(120, 50, 35),
		LL14(L"地下3テーマ", L"B3 theme", L"Thème S3", L"Tema S3", L"Tema S3", L"지하3 테마", L"地下3主题", L"B3", L"B3", L"UG3", L"S3", L"K3", L"P3", L"B3"),
		LL14(L"火山岩＋赤熱の割れ目", L"Volcanic rock with glowing cracks", L"Roche volcanique", L"Roccia vulcanica", L"Roca volcánica", L"화산암", L"火山岩与赤裂", L"Volcanic", L"Вулкан", L"Vulkanstein", L"Rocha vulcânica", L"Vulkanisch", L"Wulkaniczna", L"Volkanik"));
	legendRow(RGB(60, 200, 80),
		LL14(L"トラップ: 粘液", L"Trap: slime", L"Piège: slime", L"Trappola: slime", L"Trampa: limo", L"트랩: 슬라임", L"陷阱：粘液", L"Trap: slime", L"Ловушка: слизь", L"Falle: Schleim", L"Armadilha: limo", L"Val: slijm", L"Pułapka: szlam", L"Tuzak: balçık"),
		LL14(L"半透明緑。踏むと移動が遅くなる（消えない）", L"Translucent green; slows move (stays)", L"Ralentit; semi-transparent", L"Rallenta", L"Ralentiza", L"이동 지연", L"减速，不消失", L"Slows you", L"Замедляет", L"Verlangsamt", L"Atrasa", L"Vertraagt", L"Spowalnia", L"Yavaşlatır"));
	legendRow(RGB(220, 60, 50),
		LL14(L"トラップ: 棘", L"Trap: spikes", L"Piège: pointes", L"Trappola: spine", L"Trampa: pinchos", L"트랩: 가시", L"陷阱：尖刺", L"Trap: spikes", L"Ловушка: шипы", L"Falle: Stacheln", L"Armadilha: espinhos", L"Val: stekels", L"Pułapka: kolce", L"Tuzak: diken"),
		LL14(L"半透明赤。踏むと直前マスへ跳ね返る", L"Translucent red; bounces you back one cell", L"Rebondit d'une case", L"Rimbalza", L"Rebota", L"이전 칸으로 튕김", L"弹回上一格", L"Bounce back", L"Отбрасывает", L"Zurückstoßen", L"Rebate", L"Stuiter terug", L"Odbija", L"Geri seker"));
	legendRow(RGB(100, 200, 255),
		LL14(L"トラップ: 氷", L"Trap: ice", L"Piège: glace", L"Trappola: ghiaccio", L"Trampa: hielo", L"트랩: 얼음", L"陷阱：冰", L"Trap: ice", L"Ловушка: лёд", L"Falle: Eis", L"Armadilha: gelo", L"Val: ijs", L"Pułapka: lód", L"Tuzak: buz"),
		LL14(L"半透明水色。進入方向へさらに1マス滑る", L"Translucent cyan; slides you 1 more cell", L"Glisse d'une case", L"Scivola", L"Resbala", L"1칸 미끄러짐", L"再滑1格", L"Slide 1 cell", L"Скольжение", L"Rutscht", L"Desliza", L"Glijdt", L"Ślizg", L"Kayma"));
	legendRow(RGB(50, 40, 90),
		LL14(L"トラップ: 闇", L"Trap: darkness", L"Piège: obscurité", L"Trappola: buio", L"Trampa: oscuridad", L"트랩: 암흑", L"陷阱：黑暗", L"Trap: dark", L"Ловушка: тьма", L"Falle: Dunkel", L"Armadilha: escuro", L"Val: duister", L"Pułapka: mrok", L"Tuzak: karanlık"),
		LL14(L"半透明紫黒。しばらく霧が濃く視界が狭まる", L"Translucent dark purple; thickens fog briefly", L"Brouillard dense un moment", L"Nebbia densa", L"Niebla densa", L"잠시 안개 짙어짐", L"短暂浓雾", L"Thick fog briefly", L"Густой туман", L"Dichter Nebel", L"Névoa densa", L"Dichte mist", L"Gęsta mgła", L"Yoğun sis"));
	legendRow(RGB(80, 255, 130),
		LL14(L"アイテム: テンポ↑", L"Item: tempo↑", L"Objet: tempo↑", L"Oggetto: tempo↑", L"Objeto: tempo↑", L"아이템: 템포↑", L"道具：速度↑", L"Item: tempo↑", L"Item: tempo↑", L"Item: Tempo↑", L"Item: tempo↑", L"Item: tempo↑", L"Przedmiot: tempo↑", L"Öğe: tempo↑"),
		LL14(L"緑の浮遊球。再生テンポを上げる", L"Green orb; raise playback tempo", L"Sphère verte; tempo↑", L"Sfera verde", L"Orbe verde", L"초록 구, 템포↑", L"绿球，速度↑", L"Tempo up", L"Темп↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑"));
	legendRow(RGB(30, 140, 70),
		LL14(L"アイテム: テンポ↓", L"Item: tempo↓", L"Objet: tempo↓", L"Oggetto: tempo↓", L"Objeto: tempo↓", L"아이템: 템포↓", L"道具：速度↓", L"Item: tempo↓", L"Item: tempo↓", L"Item: Tempo↓", L"Item: tempo↓", L"Item: tempo↓", L"Przedmiot: tempo↓", L"Öğe: tempo↓"),
		LL14(L"深緑の球。テンポを下げる", L"Dark-green orb; lower tempo", L"Vert foncé; tempo↓", L"Verde scuro", L"Verde oscuro", L"진녹 구, 템포↓", L"深绿球，速度↓", L"Tempo down", L"Темп↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓"));
	legendRow(RGB(255, 165, 65),
		LL14(L"アイテム: ピッチ↑", L"Item: pitch↑", L"Objet: hauteur↑", L"Oggetto: pitch↑", L"Objeto: tono↑", L"아이템: 피치↑", L"道具：音高↑", L"Item: pitch↑", L"Item: pitch↑", L"Item: Ton↑", L"Item: tom↑", L"Item: toon↑", L"Przedmiot: wys.↑", L"Öğe: perde↑"),
		LL14(L"橙の球。ピッチを上げる", L"Orange orb; raise pitch", L"Orange; hauteur↑", L"Arancio", L"Naranja", L"주황 구, 피치↑", L"橙球，音高↑", L"Pitch up", L"Высота↑", L"Ton↑", L"Tom↑", L"Toon↑", L"Wysokość↑", L"Perde↑"));
	legendRow(RGB(90, 140, 255),
		LL14(L"アイテム: ピッチ↓", L"Item: pitch↓", L"Objet: hauteur↓", L"Oggetto: pitch↓", L"Objeto: tono↓", L"아이템: 피치↓", L"道具：音高↓", L"Item: pitch↓", L"Item: pitch↓", L"Item: Ton↓", L"Item: tom↓", L"Item: toon↓", L"Przedmiot: wys.↓", L"Öğe: perde↓"),
		LL14(L"青の球。ピッチを下げる", L"Blue orb; lower pitch", L"Bleu; hauteur↓", L"Blu", L"Azul", L"파랑 구, 피치↓", L"蓝球，音高↓", L"Pitch down", L"Высота↓", L"Ton↓", L"Tom↓", L"Toon↓", L"Wysokość↓", L"Perde↓"));
	legendRow(RGB(255, 50, 90),
		LL14(L"アイテム: 次の曲", L"Item: next track", L"Objet: piste suivante", L"Oggetto: successivo", L"Objeto: siguiente", L"아이템: 다음 곡", L"道具：下一曲", L"Item: next", L"Item: next", L"Item: nächster", L"Item: próxima", L"Item: volgend", L"Przedmiot: następny", L"Öğe: sonraki"),
		LL14(L"赤の球。次の曲へ", L"Red orb; next track", L"Rouge; piste suivante", L"Rosso", L"Rojo", L"빨강 구, 다음 곡", L"红球，下一曲", L"Next track", L"Следующий", L"Nächster", L"Próxima", L"Volgend", L"Następny", L"Sonraki"));
	legendRow(RGB(140, 20, 55),
		LL14(L"アイテム: 前の曲", L"Item: previous track", L"Objet: piste préc.", L"Oggetto: precedente", L"Objeto: anterior", L"아이템: 이전 곡", L"道具：上一曲", L"Item: prev", L"Item: prev", L"Item: vorheriger", L"Item: anterior", L"Item: vorig", L"Przedmiot: poprzedni", L"Öğe: önceki"),
		LL14(L"暗紅の球。前の曲へ", L"Dark-red orb; previous track", L"Rouge foncé; préc.", L"Rosso scuro", L"Rojo oscuro", L"검홍 구, 이전 곡", L"暗红球，上一曲", L"Previous track", L"Предыдущий", L"Vorheriger", L"Anterior", L"Vorig", L"Poprzedni", L"Önceki"));
	legendRow(RGB(255, 235, 90),
		LL14(L"アイテム: 音量↑", L"Item: volume↑", L"Objet: volume↑", L"Oggetto: volume↑", L"Objeto: volumen↑", L"아이템: 볼륨↑", L"道具：音量↑", L"Item: vol↑", L"Item: vol↑", L"Item: Lautstärke↑", L"Item: volume↑", L"Item: volume↑", L"Przedmiot: głośność↑", L"Öğe: ses↑"),
		LL14(L"黄の球。音量を上げる", L"Yellow orb; raise volume", L"Jaune; volume↑", L"Giallo", L"Amarillo", L"노랑 구, 볼륨↑", L"黄球，音量↑", L"Volume up", L"Громкость↑", L"Lautstärke↑", L"Volume↑", L"Volume↑", L"Głośność↑", L"Ses↑"));
	legendRow(RGB(140, 158, 55),
		LL14(L"アイテム: 音量↓", L"Item: volume↓", L"Objet: volume↓", L"Oggetto: volume↓", L"Objeto: volumen↓", L"아이템: 볼륨↓", L"道具：音量↓", L"Item: vol↓", L"Item: vol↓", L"Item: Lautstärke↓", L"Item: volume↓", L"Item: volume↓", L"Przedmiot: głośność↓", L"Öğe: ses↓"),
		LL14(L"オリーブの球。音量を下げる", L"Olive orb; lower volume", L"Olive; volume↓", L"Oliva", L"Oliva", L"올리브 구, 볼륨↓", L"橄榄球，音量↓", L"Volume down", L"Громкость↓", L"Lautstärke↓", L"Volume↓", L"Volume↓", L"Głośność↓", L"Ses↓"));
	legendRow(RGB(180, 90, 255),
		LL14(L"アイテム: EQ", L"Item: EQ", L"Objet: EQ", L"Oggetto: EQ", L"Objeto: EQ", L"아이템: EQ", L"道具：EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Przedmiot: EQ", L"Öğe: EQ"),
		LL14(L"紫の球。EQバンドをランダムに上下", L"Purple orb; nudge EQ bands randomly", L"Violet; EQ aléatoire", L"Viola; EQ", L"Morado; EQ", L"보라 구, EQ 변경", L"紫球，随机推EQ", L"Nudge EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ"));
	legendRow(RGB(158, 140, 200),
		LL14(L"アイテム: EQ平坦化", L"Item: EQ flatten", L"Objet: EQ plat", L"Oggetto: EQ flat", L"Objeto: EQ plano", L"아이템: EQ 평탄", L"道具：EQ平坦", L"Item: EQ flat", L"Item: EQ flat", L"Item: EQ flach", L"Item: EQ flat", L"Item: EQ flat", L"Przedmiot: EQ flat", L"Öğe: EQ düz"),
		LL14(L"灰紫の球。EQをフラット寄りへ", L"Grey-purple orb; flatten EQ toward neutral", L"Gris-violet; EQ plat", L"Grigio-viola", L"Gris-morado", L"회보라 구, EQ 평탄", L"灰紫球，EQ趋平", L"Flatten EQ", L"Выровнять EQ", L"EQ flach", L"EQ plano", L"EQ vlak", L"EQ płaski", L"EQ düz"));
	legendRow(RGB(50, 215, 240),
		LL14(L"アイテム: リバーブ", L"Item: reverb", L"Objet: réverb", L"Oggetto: reverb", L"Objeto: reverb", L"아이템: 리버브", L"道具：混响", L"Item: reverb", L"Item: reverb", L"Item: Hall", L"Item: reverb", L"Item: reverb", L"Przedmiot: pogłos", L"Öğe: reverb"),
		LL14(L"水色の球。リバーブを強める", L"Cyan orb; increase reverb", L"Cyan; réverb↑", L"Ciano; reverb", L"Cian; reverb", L"시안 구, 리버브↑", L"青球，混响↑", L"Reverb up", L"Реверб↑", L"Hall↑", L"Reverb↑", L"Reverb↑", L"Pogłos↑", L"Reverb↑"));
	legendRow(RGB(255, 115, 190),
		LL14(L"アイテム: クロスフェード", L"Item: crossfade", L"Objet: fondu", L"Oggetto: crossfade", L"Objeto: fundido", L"아이템: 크로스페이드", L"道具：交叉淡化", L"Item: crossfade", L"Item: crossfade", L"Item: Crossfade", L"Item: crossfade", L"Item: crossfade", L"Przedmiot: crossfade", L"Öğe: crossfade"),
		LL14(L"桃の球。曲間クロスフェードのON/OFF", L"Pink orb; toggle track crossfade", L"Rose; fondu on/off", L"Rosa; crossfade", L"Rosa; fundido", L"분홍 구, 크로스페이드 전환", L"粉球，交叉淡化开关", L"Toggle crossfade", L"Кроссфейд", L"Crossfade", L"Crossfade", L"Crossfade", L"Crossfade", L"Crossfade"));
	legendRow(RGB(240, 140, 50),
		LL14(L"アイテム: ランダム再生", L"Item: random play", L"Objet: aléatoire", L"Oggetto: casuale", L"Objeto: aleatorio", L"아이템: 랜덤 재생", L"道具：随机播放", L"Item: random", L"Item: random", L"Item: Zufall", L"Item: aleatório", L"Item: willekeurig", L"Przedmiot: losowo", L"Öğe: rastgele"),
		LL14(L"虹寄りの球。ランダム／順次を切替", L"Warm multicolor orb; toggle random/sequential", L"Multicolore; aléatoire/séquentiel", L"Multicolore; casuale", L"Multicolor; aleatorio", L"다색 구, 랜덤/순차", L"多彩球，随机/顺序切换", L"Toggle random", L"Случайно", L"Zufall", L"Aleatório", L"Willekeurig", L"Losowo", L"Rastgele"));
	y += 6;

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"操作", L"Controls", L"Commandes", L"Comandi", L"Controles",
		L"조작", L"操作", L"التحكم", L"Управление", L"Steuerung", L"Controles", L"Bediening", L"Sterowanie", L"Kontroller"));
	y += lh + 2;
	dc.SetTextColor(RGB(65, 65, 80));
	line(LL14(L"WASD / 矢印 / パッド左スティック・十字 = 移動　Q・E / ←→ / 右スティック・LB・RB = 旋回　A・Start = 全体マップ　生成 = 新規迷路",
		L"WASD / arrows / pad L-stick+hat = move  Q/E / ←→ / R-stick+LB/RB = turn  A/Start = map  Generate = new maze",
		L"WASD / flèches / pad stick G+croix = déplacement  Q/E / ←→ / stick D+LB/RB = tourner  A/Start = carte  Générer = nouveau",
		L"WASD / frecce / pad stick SX+croce = movimento  Q/E / ←→ / stick DX+LB/RB = gira  A/Start = mappa  Genera = nuovo",
		L"WASD / flechas / pad stick izq+cruz = movimiento  Q/E / ←→ / stick der+LB/RB = girar  A/Start = mapa  Generar = nuevo",
		L"WASD / 화살표 / 패드 좌스틱·십자 = 이동  Q/E / ←→ / 우스틱·LB/RB = 회전  A/Start = 맵  생성 = 새 미로",
		L"WASD / 方向键 / 手柄左摇杆+十字 = 移动  Q/E / ←→ / 右摇杆+LB/RB = 转向  A/Start = 地图  生成 = 新迷宫",
		L"WASD / arrows / pad L-stick+hat = move  Q/E / ←→ / R-stick+LB/RB = turn  A/Start = map  Generate = new",
		L"WASD / стрелки / пад L-стик+крест = ход  Q/E / ←→ / R-стик+LB/RB = поворот  A/Start = карта  Создать = новый",
		L"WASD / Pfeile / Pad L-Stick+Hut = Bewegung  Q/E / ←→ / R-Stick+LB/RB = Drehen  A/Start = Karte  Erzeugen = neu",
		L"WASD / setas / pad stick Esq+cruz = mover  Q/E / ←→ / stick Dir+LB/RB = girar  A/Start = mapa  Gerar = novo",
		L"WASD / pijlen / pad L-stick+hat = bewegen  Q/E / ←→ / R-stick+LB/RB = draaien  A/Start = kaart  Genereren = nieuw",
		L"WASD / strzałki / pad L-stick+hat = ruch  Q/E / ←→ / R-stick+LB/RB = obrót  A/Start = mapa  Generuj = nowy",
		L"WASD / oklar / pad sol çubuk+çapraz = hareket  Q/E / ←→ / sağ çubuk+LB/RB = dönüş  A/Start = harita  Oluştur = yeni"));
	line(LL14(L"ホイール = 視点の拡大縮小（狭いFOVで拡大）。Shift+ホイール = 旋回。ミニマップ上のホイール = 地図ズーム。",
		L"Wheel = zoom view (narrower FOV). Shift+wheel = turn. Wheel on minimap = map zoom.",
		L"Molette = zoom (FOV plus étroit). Maj+molette = tourner. Molette sur minimap = zoom carte.",
		L"Rotella = zoom (FOV più stretto). Maiusc+rotella = gira. Rotella sulla minimap = zoom mappa.",
		L"Rueda = zoom (FOV más estrecho). Mayús+rueda = girar. Rueda en minimapa = zoom mapa.",
		L"휠 = 시점 확대/축소(좁은 FOV). Shift+휠 = 회전. 미니맵 위 휠 = 지도 줌.",
		L"滚轮 = 视角缩放（更窄FOV）。Shift+滚轮 = 转向。小地图上滚轮 = 地图缩放。",
		L"العجلة = تكبير العرض (FOV أضيق). Shift+عجلة = دوران. عجلة على الخريطة = تكبير الخريطة.",
		L"Колесо = зум (уже FOV). Shift+колесо = поворот. Колесо на мини-карте = зум карты.",
		L"Rad = Zoom (engeres FOV). Umschalt+Rad = Drehen. Rad auf Minimap = Kartenzoom.",
		L"Roda = zoom (FOV mais estreito). Shift+roda = girar. Roda no minimapa = zoom do mapa.",
		L"Wiel = zoom (nauwere FOV). Shift+wiel = draaien. Wiel op minimap = kaartzoom.",
		L"Kółko = zoom (węższe FOV). Shift+kółko = obrót. Kółko na minimapie = zoom mapy.",
		L"Teker = yakınlaştırma (daha dar FOV). Shift+teker = dönüş. Minimapi üzerinde teker = harita zoom."));
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
	line(LL14(L"SPACE / ホイールクリック = 全体マップ表示切替（Escでも閉じる）。表示中は移動不可。ホイールで地図ズーム、ドラッグでスクロール。Shift+ホイール／←→／A D で階層切替。",
		L"SPACE / wheel-click = toggle full map (Esc closes). No move while open. Wheel zooms map; drag to scroll. Shift+wheel / ←→ / A D change floor.",
		L"Espace / clic molette = carte (Échap ferme). Pas de déplacement. Molette = zoom, glisser = défiler. Maj+molette / ←→ / A D = étage.",
		L"SPAZIO / clic rotella = mappa (Esc chiude). Niente movimento. Rotella = zoom, trascina = scorri. Maiusc+rotella / ←→ / A D = piano.",
		L"Espacio / clic rueda = mapa (Esc cierra). Sin mover. Rueda = zoom, arrastrar = desplazar. Mayús+rueda / ←→ / A D = planta.",
		L"SPACE / 휠 클릭 = 전체 맵 토글(Esc 닫기). 표시 중 이동 불가. 휠=지도 줌, 드래그=스크롤. Shift+휠 / ←→ / A D = 층.",
		L"空格 / 滚轮点击 = 全图开关（Esc关闭）。显示中不可移动。滚轮缩放，拖动滚动。Shift+滚轮 / ←→ / A D 换层。",
		L"مسافة / نقر العجلة = خريطة (Esc يغلق). بلا حركة. العجلة=تكبير، السحب=تمرير. Shift+عجلة / ←→ / A D = طابق.",
		L"Пробел / клик колёсиком = карта (Esc закрывает). Без движения. Колесо = зум, перетаскивание = прокрутка. Shift+колесо / ←→ / A D = этаж.",
		L"Leertaste / Radklick = Karte (Esc schließt). Keine Bewegung. Rad = Zoom, Ziehen = Scrollen. Umschalt+Rad / ←→ / A D = Etage.",
		L"Espaço / clique roda = mapa (Esc fecha). Sem mover. Roda = zoom, arrastar = rolar. Shift+roda / ←→ / A D = piso.",
		L"Spatie / wielklik = kaart (Esc sluit). Geen bewegen. Wiel = zoom, slepen = scrollen. Shift+wiel / ←→ / A D = verdieping.",
		L"Spacja / klik kółkiem = mapa (Esc zamyka). Bez ruchu. Kółko = zoom, przeciąganie = przewijanie. Shift+kółko / ←→ / A D = piętro.",
		L"SPACE / teker tık = harita (Esc kapatır). Hareket yok. Teker = zoom, sürükle = kaydır. Shift+teker / ←→ / A D = kat."));
	line(LL14(L"「地下」で地下1〜3Fを追加。橙の階段=下り／水色=上り。ゴールは難易度に応じてどこかの階（必ずしも最下層ではない）。",
		L"\"Basement\" adds 1–3 lower floors. Orange stairs go down, cyan up. Goal floor depends on difficulty (not always deepest).",
		L"« Sous-sol » ajoute 1–3 étages. Escaliers orange : descendre, cyan : monter ; but selon difficulté.",
		L"«Sotterraneo» aggiunge 1–3 piani. Scale arancioni giù, ciano su; traguardo secondo difficoltà.",
		L"«Sótano» añade 1–3 plantas. Escaleras naranjas bajan, cian suben; la meta depende de la dificultad.",
		L"「지하」로 지하 1~3층 추가. 주황 계단=하강, 하늘색=상승. 골 층은 난이도에 따라 다름.",
		L"“地下”可添加 1–3 层。橙色楼梯下行，水色上行。终点层随难度变化。",
		L"«القبو» يضيف 1–3 طوابق. السلالم البرتقالية للأسفل والسماوية للأعلى. الطابق الهدف يعتمد على الصعوبة.",
		L"«Подвал» добавляет 1–3 этажа. Оранжевые лестницы вниз, голубые вверх; этаж цели зависит от сложности.",
		L"„Keller“ ergänzt 1–3 Etagen. Orange Treppen abwärts, Cyan aufwärts; Zieletage hängt von der Schwierigkeit ab.",
		L"“Subsolo” adiciona 1–3 pisos. Escadas laranja descem, ciano sobem; o piso do gol depende da dificuldade.",
		L"'Kelder' voegt 1–3 verdiepingen toe. Oranje trappen omlaag, cyaan omhoog; doelverdieping hangt af van moeilijkheid.",
		L"„Piwnica” dodaje 1–3 poziomy. Pomarańczowe schody w dół, cyjanowe w górę; piętro celu zależy od trudności.",
		L"“Bodrum” 1–3 kat ekler. Turuncu merdiven aşağı, camgöbeği yukarı; hedef kat zorluğa göre değişir."));
	line(LL14(L"難易度（超簡単〜超難しい）: 通路・広間（マップサイズ比例）・階段・ゴールへの別ルート数（普通は200マスごと×階層）に影響。難しいほど細くループ少・階段多。アイテムは効果用で多め。",
		L"Difficulty (very easy–very hard): corridor width, rooms (scale with map size), stairs, alternate routes to the goal (Normal: +1 route per 200 cells × floors), and 3D path length. Harder = thinner maze, fewer loops, more stairs. Items stay plentiful.",
		L"Difficulté : couloirs, salles, plus d'escaliers (va-et-vient) et but plus loin. Objets nombreux.",
		L"Difficoltà: corridoi più stretti, più scale (su/giù) e traguardo lontano. Molti oggetti.",
		L"Dificultad: pasillos estrechos, más escaleras (sube/baja) y meta lejana. Muchos objetos.",
		L"난이도: 좁은 통로·계단 많음(층 오르내림)·먼 골. 아이템은 효과용으로 많음.",
		L"难度：更窄通道、更多楼梯（上下往返）、跨层更远终点。道具偏多（音效用）。",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items.",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items.",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items.",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items.",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items.",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items.",
		L"Harder: thinner maze, more stairs, multi-floor zigzags, plentiful items."));
	line(LL14(L"鍵と扉: 難易度・サイズに応じて扉が置かれ、手前側に鍵がある。鍵を拾って扉を通る（1扉で鍵1消費）。ナビはゴール不通時に最寄りの鍵へ誘導する。",
		L"Keys & doors: doors scale with size/difficulty; a key sits on the near side. Pick up a key to open a door (1 key each). Navi guides to the nearest key if the goal is locked.",
		L"Clés et portes : selon taille/difficulté. Ramassez une clé pour ouvrir (1 par porte). Nav vers la clé si le but est bloqué.",
		L"Chiavi e porte: secondo dimensione/difficoltà. Raccogli chiave per aprire (1 a porta). Navi verso la chiave se il traguardo è chiuso.",
		L"Llaves y puertas: según tamaño/dificultad. Recoge llave para abrir (1 por puerta). Navi a la llave si la meta está cerrada.",
		L"열쇠와 문: 크기·난이도에 따라 배치. 열쇠를 주워 문 개방(문당 1). 골이 막히면 내비는 가까운 열쇠로.",
		L"钥匙与门：随尺寸/难度放置。拾钥匙开门（一扇一门）。终点被锁时导航指向最近钥匙。",
		L"Keys & doors: pick up keys to open doors. Navi points to a key if the goal is locked.",
		L"Ключи и двери: подберите ключ, чтобы открыть дверь. Нави ведёт к ключу, если цель закрыта.",
		L"Schlüssel & Türen: Schlüssel aufheben zum Öffnen. Navi zeigt zum Schlüssel, wenn das Ziel versperrt ist.",
		L"Chaves e portas: pegue chaves para abrir. Navi aponta à chave se o gol estiver trancado.",
		L"Sleutels & deuren: pak sleutel om te openen. Navi wijst naar sleutel als doel geblokkeerd is.",
		L"Klucze i drzwi: podnieś klucz, by otworzyć. Nawi wskazuje klucz, gdy cel jest zablokowany.",
		L"Anahtar ve kapılar: kapıyı açmak için anahtar alın. Hedef kilitliyse navi anahtara yönlendirir."));
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
	line(LL14(L"PCM効果音: 歩行・アイテム・鍵・扉・階段・ポータルに加え、壁衝突、鍵なし扉、棘／粘液／氷／闇、ゴールを合成。曲の再生とは別経路。表示メニューでON/OFF。",
		L"PCM SFX: steps, items, keys, doors, stairs, portals, plus bumps, locked doors, spike/slime/ice/dark traps and the goal (separate from music). Toggle in View.",
		L"SFX PCM : pas, objets, clés, portes, escaliers, portails, chocs, portes fermées, pièges, arrivée. Menu Vue.",
		L"SFX PCM: passi, oggetti, chiavi, porte, scale, portali, urti, porte chiuse, trappole, arrivo. Menu Vista.",
		L"SFX PCM: pasos, objetos, llaves, puertas, escaleras, portales, golpes, puertas cerradas, trampas, meta. Menú Vista.",
		L"PCM 효과음: 발소리·아이템·열쇠·문·계단·포털+벽충돌·잠긴문·함정·골. 보기 메뉴 ON/OFF.",
		L"PCM效果音：脚步、道具、钥匙、门、楼梯、传送门，以及撞墙、无钥匙门、尖刺/粘液/冰/暗、终点。显示菜单开关。",
		L"PCM SFX: steps, items, keys, doors, stairs, portals, bumps, locks, traps, goal. View menu toggle.",
		L"PCM SFX: шаги, предметы, ключи, двери, лестницы, порталы, удары, замки, ловушки, цель.",
		L"PCM-SFX: Schritte, Items, Schlüssel, Türen, Treppen, Portale, Stöße, Schlösser, Fallen, Ziel. Ansicht-Menü.",
		L"SFX PCM: passos, itens, chaves, portas, escadas, portais, impactos, portas trancadas, armadilhas, chegada. Menu Vista.",
		L"PCM-SFX: stappen, items, sleutels, deuren, trappen, portalen, stoten, sloten, vallen, doel. Weergave-menu.",
		L"SFX PCM: kroki, przedmioty, klucze, drzwi, schody, portale, uderzenia, zamki, pułapki, cel. Menu Widok.",
		L"PCM SFX: adım, öğe, anahtar, kapı, merdiven, portal, çarpma, kilit, tuzak, hedef. Görünüm menüsü."));
	line(LL14(L"壁・床・鏡・アイテム・ギミックはライセンスフリーの写真テクスチャ（50枚・256）をexeに埋め込んでいます（別ファイル不要）。階テーマ（レンガ／湿った石／錆び鉄／火山岩）に細部を重ね、地下は室内キューブで壁が見える明るさにし、反射と乱反射があります。",
		L"Walls, floors, mirrors, items and gizmos use 50 license-free photo textures (256px) embedded in the exe (no extra files): albedo plus detail per floor theme (brick, wet stone, rusted metal, volcanic), a brighter indoor cube in the basement, and reflection plus wrap lighting.",
		L"Murs, sols, miroirs, objets : 50 textures photo 256 dans l'exe (albédo+détail, cube intérieur plus clair en sous-sol, réflexion).",
		L"Muri, pavimenti, specchi, oggetti: 50 texture foto 256 nell'exe (albedo+dettaglio, cubo interno più chiaro in cantina, riflessione).",
		L"Paredes, suelos, espejos, objetos: 50 texturas foto 256 en el exe (albedo+detalle, cubo interior más claro en sótano, reflexión).",
		L"벽·바닥·거울·아이템·기믹은 라이선스 프리 사진 텍스처 50장(256)을 exe에 내장. 층 테마에 세부, 지하는 실내 큐브로 밝게, 반사·난반사.",
		L"墙、地板、镜子、道具、机关：50张许可证自由照片纹理（256）嵌入exe。各层主题叠加细节，地下用室内立方体提亮，含反射与漫反射。",
		L"Walls/floors/mirrors/items: 50 photo textures 256 in the exe (albedo+detail, brighter indoor cube below, reflection).",
		L"Стены, полы, зеркала, предметы: 50 фототекстур 256 в exe (альбедо+деталь, более светлый внутренний куб в подвале, отражение).",
		L"Wände, Böden, Spiegel, Items: 50 Fototexturen 256 in der exe (Albedo+Detail, hellerer Innenwürfel im Keller, Reflexion).",
		L"Paredes, chãos, espelhos, itens: 50 texturas foto 256 no exe (albedo+detalhe, cubo interno mais claro no subsolo, reflexão).",
		L"Muren, vloeren, spiegels, items: 50 fototexturen 256 in de exe (albedo+detail, helderdere binnenkubus in de kelder, reflectie).",
		L"Ściany, podłogi, lustra, przedmioty: 50 tekstur zdjęciowych 256 w exe (albedo+szczegół, jaśniejszy sześcian wewnątrz w piwnicy, odbicie).",
		L"Duvar, zemin, ayna, öğe: 50 adet 256 fotoğraf dokusu exe içinde (albedo+ayrıntı, bodrumda daha aydınlık iç küp, yansıma)."));
	line(LL14(L"アイテムを拾うと再生パラメータが変わります（テンポ↑↓・ピッチ・前後曲・音量・EQ／平坦・リバーブ・クロスフェード・ランダム切替）。閉じるとテンポ/ピッチは復帰。",
		L"Pickups tweak playback (tempo↑↓, pitch, prev/next, volume, EQ/flatten, reverb, crossfade, random). Tempo/pitch restore on close.",
		L"Les objets changent lecture (tempo, hauteur, pistes, volume, EQ, réverb, fondu, aléatoire). Tempo/hauteur à la fermeture.",
		L"I pickup cambiano riproduzione (tempo, pitch, brani, volume, EQ, reverb, crossfade, casuale).",
		L"Recoger cambia reproducción (tempo, tono, pistas, volumen, EQ, reverb, fundido, aleatorio).",
		L"아이템으로 재생 파라미터 변경(템포·피치·곡·볼륨·EQ·리버브·크로스페이드·랜덤). 닫으면 템포/피치 복원.",
		L"拾取会改播放参数（速度、音高、前后曲、音量、EQ、混响、交叉淡化、随机）。关闭时恢复速度/音高。",
		L"Pickups tweak playback params; tempo/pitch restore on close.",
		L"Pickups tweak playback params; tempo/pitch restore on close.",
		L"Pickups tweak playback params; tempo/pitch restore on close.",
		L"Pickups tweak playback params; tempo/pitch restore on close.",
		L"Pickups tweak playback params; tempo/pitch restore on close.",
		L"Pickups tweak playback params; tempo/pitch restore on close.",
		L"Pickups tweak playback params; tempo/pitch restore on close."));
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

static void S3mEqFlatten(int step)
{
	for (int i = 0; i < 15; i++) {
		int v = savedata.eq[i];
		if (v > 100) { v -= step; if (v < 100) v = 100; }
		else if (v < 100) { v += step; if (v > 100) v = 100; }
		savedata.eq[i] = v;
	}
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

static void S3mNudgeVolPct(int delta)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int p = og->m_sl.GetPos() + delta * 1000;
	if (p < 0) p = 0;
	if (p > 100000) p = 100000;
	og->m_sl.SetPos(p);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_vol.GetSafeHwnd()) {
		int v = p / 1000;
		if (v < 0) v = 0; if (v > 100) v = 100;
		mp->m_vol.SetPos(v, FALSE);
	}
}

static void S3mNudgeReverb(int delta)
{
	int v = savedata.eq_reverb + delta;
	if (v < 0) v = 0;
	if (v > 200) v = 200;
	savedata.eq_reverb = v;
}

// ---- DirectInput joypad（レースUIと同系） ----
struct S3mJoyState {
	float lx, ly, rx, ry;
	float lt, rt;
	int buttons; // bit0=A bit1=B bit2=X bit3=Y bit4=LB bit5=RB bit6=Back bit7=Start
	int hat; // -1 none, 0 up … 7
	int connected;
};
static IDirectInput8W* g_s3mDi = NULL;
static IDirectInputDevice8W* g_s3mPad = NULL;
static BOOL g_s3mDiTried = FALSE;

static BOOL CALLBACK S3mEnumPads(const DIDEVICEINSTANCEW* inst, void*)
{
	if (FAILED(g_s3mDi->CreateDevice(inst->guidInstance, &g_s3mPad, NULL)))
		return DIENUM_CONTINUE;
	return DIENUM_STOP;
}
static void S3mEnsureJoypad()
{
	if (g_s3mDiTried) return;
	g_s3mDiTried = TRUE;
	if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&g_s3mDi, NULL)))
		return;
	g_s3mDi->EnumDevices(DI8DEVCLASS_GAMECTRL, S3mEnumPads, NULL, DIEDFL_ATTACHEDONLY);
	if (!g_s3mPad) return;
	g_s3mPad->SetDataFormat(&c_dfDIJoystick2);
	HWND hw = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : NULL;
	g_s3mPad->SetCooperativeLevel(hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
	DIPROPRANGE range = {};
	range.diph.dwSize = sizeof(range);
	range.diph.dwHeaderSize = sizeof(range.diph);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1000; range.lMax = 1000;
	range.diph.dwObj = DIJOFS_X; g_s3mPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_Y; g_s3mPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RX; g_s3mPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RY; g_s3mPad->SetProperty(DIPROP_RANGE, &range.diph);
	DIPROPDWORD dead = {};
	dead.diph.dwSize = sizeof(dead);
	dead.diph.dwHeaderSize = sizeof(dead.diph);
	dead.diph.dwHow = DIPH_DEVICE;
	dead.dwData = 1200;
	g_s3mPad->SetProperty(DIPROP_DEADZONE, &dead.diph);
	g_s3mPad->Acquire();
}
static float S3mAxisN(LONG v)
{
	float f = (float)v / 1000.f;
	if (fabsf(f) < 0.18f) return 0.f;
	if (f < -1.f) f = -1.f;
	if (f > 1.f) f = 1.f;
	return f;
}
static void S3mUpdateJoypadState(S3mJoyState& out)
{
	memset(&out, 0, sizeof(out));
	out.hat = -1;
	S3mEnsureJoypad();
	if (!g_s3mPad) return;
	HRESULT hr = g_s3mPad->Poll();
	if (FAILED(hr)) { g_s3mPad->Acquire(); hr = g_s3mPad->Poll(); }
	DIJOYSTATE2 st = {};
	hr = g_s3mPad->GetDeviceState(sizeof(st), &st);
	if (FAILED(hr)) return;
	out.connected = 1;
	out.lx = S3mAxisN(st.lX);
	out.ly = S3mAxisN(st.lY);
	out.rx = S3mAxisN(st.lRx);
	out.ry = S3mAxisN(st.lRy);
	out.lt = (float)st.lZ / 1000.f;
	if (out.lt < 0.f) out.lt = 0.f;
	if (out.lt > 1.f) out.lt = 1.f;
	float rt = (float)st.lRz / 1000.f;
	if (rt < 0.f) rt = 0.f;
	if (rt > 1.f) rt = 1.f;
	if (rt < 0.01f && st.rglSlider[0] > 0) {
		rt = st.rglSlider[0] / 1000.f;
		if (rt < 0.f) rt = 0.f;
		if (rt > 1.f) rt = 1.f;
	}
	out.rt = rt;
	for (int i = 0; i < 8; i++) if (st.rgbButtons[i] & 0x80) out.buttons |= (1 << i);
	DWORD pov = st.rgdwPOV[0];
	if (pov != 0xFFFFFFFF && (LOWORD(pov) != 0xFFFF))
		out.hat = (int)((pov + 2250) / 4500) % 8;
}
static void S3mReleaseJoypad()
{
	if (g_s3mPad) { g_s3mPad->Unacquire(); g_s3mPad->Release(); g_s3mPad = NULL; }
	if (g_s3mDi) { g_s3mDi->Release(); g_s3mDi = NULL; }
	g_s3mDiTried = FALSE;
}

static BOOL S3mIsPickupCell(BYTE c)
{
	return (c >= CSoft3DMazeDlg::CELL_TEMPO && c <= CSoft3DMazeDlg::CELL_EQ)
		|| (c >= CSoft3DMazeDlg::CELL_TEMPO_DN && c <= CSoft3DMazeDlg::CELL_RANDOM);
}

static BOOL S3mIsTrapCell(BYTE c)
{
	return c >= CSoft3DMazeDlg::CELL_SLIME && c <= CSoft3DMazeDlg::CELL_DARK;
}
static int S3mCalloutSlot(BYTE c)
{
	using C = CSoft3DMazeDlg;
	switch (c) {
	case C::CELL_KEY: return 0;
	case C::CELL_DOOR: return 1;
	case C::CELL_TEMPO: return 2;
	case C::CELL_TEMPO_DN: return 3;
	case C::CELL_PITCH_UP: return 4;
	case C::CELL_PITCH_DN: return 5;
	case C::CELL_NEXT: return 6;
	case C::CELL_PREV: return 7;
	case C::CELL_VOL_UP: return 8;
	case C::CELL_VOL_DN: return 9;
	case C::CELL_EQ: return 10;
	case C::CELL_EQ_FLAT: return 11;
	case C::CELL_REVERB: return 12;
	case C::CELL_XFADE: return 13;
	case C::CELL_RANDOM: return 14;
	case C::CELL_STAIRS_DOWN: return 15;
	case C::CELL_STAIRS_UP: return 16;
	case C::CELL_PORTAL: return 17;
	case C::CELL_GOAL: return 18;
	default: return -1;
	}
}
static void S3mCalloutRgb(BYTE c, float& r, float& g, float& b)
{
	using C = CSoft3DMazeDlg;
	r = .95f; g = .85f; b = .55f;
	if (c == C::CELL_DOOR) { r = .95f; g = .42f; b = .22f; }
	else if (c == C::CELL_KEY) { r = 1.f; g = .9f; b = .35f; }
	else if (c == C::CELL_GOAL) { r = 1.f; g = .86f; b = .28f; }
	else if (c == C::CELL_STAIRS_DOWN) { r = 1.f; g = .6f; b = .22f; }
	else if (c == C::CELL_STAIRS_UP) { r = .35f; g = .9f; b = 1.f; }
	else if (c == C::CELL_PORTAL) { r = .85f; g = .5f; b = 1.f; }
	else if (S3mIsPickupCell(c)) { r = .95f; g = .55f; b = .9f; }
}
static BOOL S3mIsSolidWall(BYTE c)
{
	return c == CSoft3DMazeDlg::CELL_WALL
		|| c == CSoft3DMazeDlg::CELL_WINDOW
		|| c == CSoft3DMazeDlg::CELL_MIRROR_WALL;
}
static BOOL S3mIsMirrorWallCell(BYTE c) { return c == CSoft3DMazeDlg::CELL_MIRROR_WALL; }
static BOOL S3mIsMirrorFloorCell(BYTE c) { return c == CSoft3DMazeDlg::CELL_MIRROR_FLOOR; }

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
	if (m < 0) return 0; // 明示的に全OFF
	if (m == 0) m = CSoft3DMazeDlg::ITEM_ALL;
	// 旧マスク(0x3F=全旧アイテム)は新種も有効にする
	if ((m & 0x3F) == 0x3F && (m & ~0x3F) == 0)
		m = CSoft3DMazeDlg::ITEM_ALL;
	return m & CSoft3DMazeDlg::ITEM_ALL;
}
static void S3mSetItemMask(int m)
{
	m &= CSoft3DMazeDlg::ITEM_ALL;
	savedata.s3m_item_mask = (m == 0) ? -1 : m;
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
	: m_ready(FALSE), m_vw(0), m_vh(0), m_dxFailStage(0), m_dxFailHr(S_OK)
	, m_mirrorSize(S3M_MIRROR_SIZE)
	, m_dev(NULL), m_imm(NULL), m_swap(NULL), m_bbRtv(NULL)
	, m_dsTex(NULL), m_dsv(NULL), m_dsSrv(NULL), m_sceneTex(NULL), m_sceneRtv(NULL), m_sceneSrv(NULL)
	, m_postTex(NULL), m_postRtv(NULL), m_postSrv(NULL), m_shadowTex(NULL), m_shadowDsv(NULL), m_shadowSrv(NULL)
	, m_mirrorDs(NULL), m_mirrorDsv(NULL)
	, m_vsTess(NULL), m_hsTess(NULL), m_dsTess(NULL)
	, m_psWall(NULL), m_vsSolid(NULL), m_psSolid(NULL), m_vsHud(NULL), m_psHud(NULL), m_psHudLine(NULL), m_vsPost(NULL)
	, m_psSsr(NULL), m_psDof(NULL), m_psFinal(NULL), m_ilPatch(NULL), m_ilSolid(NULL), m_ilHud(NULL)
	, m_cbFrame(NULL), m_vbDyn(NULL), m_vbHud(NULL), m_vbDynBytes(6*1024*1024), m_vbHudBytes(512*1024)
	, m_cpuDynScratch(NULL), m_cpuDynScratchBytes(0), m_cpuHudScratch(NULL), m_cpuHudScratchBytes(0)
	, m_texEnv(NULL), m_srvEnv(NULL), m_texEnvIn(NULL), m_srvEnvIn(NULL)
	, m_texItem(NULL), m_srvItem(NULL), m_texGimmick(NULL), m_srvGimmick(NULL)
	, m_texGlass(NULL), m_srvGlass(NULL), m_texBrick2(NULL), m_srvBrick2(NULL)
	, m_texMirrorWall(NULL), m_srvMirrorWall(NULL), m_texMirrorFloor(NULL), m_srvMirrorFloor(NULL)
	, m_texClear(NULL), m_srvClear(NULL), m_clearTexW(0), m_clearTexH(0), m_texMap(NULL), m_srvMap(NULL), m_texTip(NULL), m_srvTip(NULL), m_tipW(0), m_tipH(0), m_texBadge(NULL), m_srvBadge(NULL), m_badgeW(0), m_badgeH(0)
	, m_texCallout(NULL), m_srvCallout(NULL), m_calloutW(0), m_calloutH(0), m_calloutN(0)
	, m_texFloorBar(NULL), m_srvFloorBar(NULL), m_floorBarW(0), m_floorBarH(0), m_floorBarRecX0(0), m_floorBarRecX1(0)
	, m_sampLin(NULL), m_sampPoint(NULL), m_sampCmp(NULL)
	, m_rsSolid(NULL), m_rsShadow(NULL), m_rsNoCull(NULL), m_dssWrite(NULL), m_dssRead(NULL), m_dssOff(NULL), m_bsOpaque(NULL), m_bsAlpha(NULL)
	, m_bsAdd(NULL), m_dragging(0), m_dragTurnAcc(0)
{
	for (int i = 0; i < S3M_THEME_N; i++) {
		m_texBrick[i] = NULL; m_srvBrick[i] = NULL;
		m_texBrickD[i] = NULL; m_srvBrickD[i] = NULL;
		m_texFloor[i] = NULL; m_srvFloor[i] = NULL;
		m_texFloorD[i] = NULL; m_srvFloorD[i] = NULL;
	}
	for (int i = 0; i < S3M_MIRROR_N; i++) {
		m_mirrorTex[i] = NULL;
		m_mirrorRtv[i] = NULL;
		m_mirrorSrv[i] = NULL;
	}
	for (int i = 0; i < 4; i++) {
		m_floorBarTabX0[i] = 0.f;
		m_floorBarTabX1[i] = 0.f;
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
		"HC HPC(InputPatch<P,4> p,uint id:SV_PrimitiveID){HC o;float3 c=(p[0].p+p[1].p+p[2].p+p[3].p)*.25;float d=distance(c,Eye.xyz);"
		"float tf=(LightDir.w<.5)?64.:lerp(64.,2.,saturate((d-1.2)/11.));tf=clamp(tf,1.,64.);"
		"o.e[0]=o.e[1]=o.e[2]=o.e[3]=tf;o.i[0]=o.i[1]=tf;return o;}"
		"[domain(\"quad\")][partitioning(\"fractional_even\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"HPC\")]"
		"P HST(InputPatch<P,4> p,uint i:SV_OutputControlPointID,uint id:SV_PrimitiveID){return p[i];}"
		"float hash(float2 p){return frac(sin(dot(p,float2(12.9898,78.233)))*43758.5453);}"
		"float noise(float2 p){float2 i=floor(p),f=frac(p);float a=hash(i),b=hash(i+float2(1,0)),c=hash(i+float2(0,1)),d=hash(i+float2(1,1));"
		"float2 u=f*f*(3.-2.*f);return lerp(a,b,u.x)+(c-a)*u.y*(1.-u.x)+(d-b)*u.x*u.y;}"
		"float fbm(float2 p){float f=0.,a=0.5;for(int i=0;i<4;i++){f+=a*noise(p);p*=2.;a*=.5;}return f;}"
		"[domain(\"quad\")]D DST(HC h,float2 q:SV_DomainLocation,const OutputPatch<P,4> p){"
		"P a,b,o;a.p=lerp(p[0].p,p[1].p,q.x);b.p=lerp(p[3].p,p[2].p,q.x);o.p=lerp(a.p,b.p,q.y);"
		"a.n=lerp(p[0].n,p[1].n,q.x);b.n=lerp(p[3].n,p[2].n,q.x);o.n=normalize(lerp(a.n,b.n,q.y));"
		"a.uv=lerp(p[0].uv,p[1].uv,q.x);b.uv=lerp(p[3].uv,p[2].uv,q.x);o.uv=lerp(a.uv,b.uv,q.y);o.c=p[0].c;"
		"float ht=T0.SampleLevel(SL,o.uv*2.5,0).a-.42;float bump=max(0,ht);bump=bump*bump*(3.-2.*bump);"
		"float nz=fbm(o.p.xz*15.+Misc.w*.02)*0.015;"
		"float d=distance(o.p,Eye.xyz);float amp=lerp(.38,.015,saturate((d-1.5)/10.));"
		"o.p+=o.n*(bump+nz)*amp;float3 nn=normalize(o.n+float3(bump*.8,0,bump*.8)*amp*2.+float3(nz,nz,nz));"
		"D z;z.w=o.p;z.n=nn;z.uv=o.uv;z.c=o.c;z.p=mul(float4(o.p,1),VP);return z;}"
		"float ShadowAt(float3 w,float3 n){float3 nn=normalize(n);float3 l=normalize(LightDir.xyz);float ndl=saturate(dot(nn,l));"
		"w+=nn*(0.015+(1-ndl)*0.025);float4 sp=mul(float4(w,1),LightVP);float iw=1.0/max(sp.w,1e-5);"
		"float2 uv=sp.xy*iw*float2(.5,-.5)+.5;"
		"uv+=float2(2.0,-2.0)/1024.0;"
		"float z=sp.z*iw-0.003;"
		"if(any(uv<0)||any(uv>1)||z<=0||z>=1)return 1;"
		"const float2 o[12]={float2(-0.326,-0.406),float2(-0.840,-0.074),float2(-0.696,0.457),float2(-0.203,0.621),"
		"float2(0.962,-0.195),float2(0.473,-0.480),float2(0.519,0.767),float2(0.185,-0.893),"
		"float2(0.507,0.064),float2(0.896,0.412),float2(-0.322,-0.933),float2(-0.792,-0.598)};"
		"float s=0;const float t=2.0/1024.0;[unroll]for(int k=0;k<12;k++)s+=ShadowMap.SampleCmpLevelZero(SCmp,uv+o[k]*t,z);"
		"s/=12.0;return pow(saturate(s),1.12);}"
		"float ShadeLit(float ndl,float sh){float d=saturate(ndl);float wrap=saturate(ndl*.52+.48);float amb=.48+.11*saturate(Eye.w*.5);"
		"float lit=lerp(amb,max(d,amb*.92),sh);return saturate(lit*.62+wrap*wrap*.48);}"
		"float4 PlanarMir(float3 w,row_major float4x4 RVP,Texture2D M){float4 rp=mul(float4(w,1),RVP);float iw=max(rp.w,1e-5);float2 muv=rp.xy/iw*float2(.5,-.5)+.5;"
		"float mb=(rp.w>0)*saturate(min(min(muv.x,1-muv.x),min(muv.y,1-muv.y))*6);return float4(M.Sample(SL,saturate(muv)).rgb,mb);}"
		"float4 PSW(D i):SV_Target{float4 a=T0.Sample(SL,i.uv*2.5)*i.c;float h=T0.Sample(SL,i.uv*2.5).a;"
		"float3 det=T1.Sample(SL,i.uv*8.2).rgb;a.rgb=lerp(a.rgb,saturate(a.rgb*det*1.28),0.46);"
		"float hx=T0.Sample(SL,i.uv*2.5+float2(.004,0)).a-h;float hy=T0.Sample(SL,i.uv*2.5+float2(0,.004)).a-h;"
		"float nz=fbm(i.uv*250.+Misc.w*.02)*0.1;"
		"float3 n=normalize(i.n+float3(hx+nz,hy+nz,0)*4.2);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float nd=ShadeLit(dot(n,l),sh);"
		"float3 v=normalize(Eye.xyz-i.w);float3 r=reflect(-l,n);float rv=saturate(dot(r,v));"
		"float sp=pow(rv,56)*sh;float spark=pow(rv,180)*sh;float3 sun=float3(1.0,.94,.78);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float fr=pow(1-saturate(dot(n,v)),3);"
		"float metal=saturate((i.c.a-1.01)*8);float doorM=saturate(1-abs(i.c.a-1.05)*50);float keyM=saturate(1-abs(i.c.a-1.12)*40);"
		"float useMir=LightDir.w;float3 rd=reflect(-v,n);"
		"float4 mir=PlanarMir(i.w,ReflectVP,MirrorMap);float4 mir2=PlanarMir(i.w+rd*1.35,ReflectVP,MirrorMap);if(mir2.a>mir.a)mir=mir2;"
		"float mw=metal*useMir*max(mir.a,.35);env=lerp(env,mir.rgb,mw);"
		"float pulse=.5+.5*sin(Misc.w*1.65+i.w.x*.4+i.w.z*.3);"
		"float occ=lerp(0.84,1.08,a.a);if(Eye.w>0.5)occ=lerp(0.94,1.14,a.a);"
		"float3 col=lerp(a.rgb*nd*occ,mir.rgb*(.3+.7*a.rgb),mw*.92)+sun*(sp*.5+spark*.9)+env*(.12+fr*.28)*(1-mw*.55)*lerp(.55,1,sh);"
		"col+=sun*(sp*.55+spark*1.1)*doorM*(.55+.45*pulse);col+=float3(1,.92,.35)*(spark*1.6+sp*.4)*keyM*(.6+.4*pulse);"
		"col=lerp(col,env*(.4+.6*a.rgb)+mir.rgb*.5,keyM*.35*useMir);"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=saturate(fg+max(0,Fog.w-i.w.y)*Fog.z);fg=fg*fg*(3-2*fg);"
		"return float4(lerp(col,float3(.52,.66,.84),fg*.72),1);}"
		"D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}"
		"float4 PSS(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float nz=fbm(i.w.xz*18.+i.w.y*14.+Misc.w*.02)*0.1;"
		"n=normalize(n+float3(nz,nz*0.5,nz)*0.8);"
		"float3 v=normalize(Eye.xyz-i.w);float nd=ShadeLit(dot(n,l),sh);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),64)*sh;float3 env=Env.Sample(SL,reflect(-v,n)).rgb;"
		"float fr=pow(1-saturate(dot(n,v)),2.5);float mirror=saturate((i.c.a-1.01)*8);float glass=saturate((i.c.a-1.18)*10);float useMir=LightDir.w;"
		"float doorM=saturate(1-abs(i.c.a-1.05)*50);float keyM=saturate(1-abs(i.c.a-1.12)*40);"
		"float3 rd=reflect(-v,n);float4 mir=PlanarMir(i.w,ReflectFloorVP,MirrorFloor);float4 mir2=PlanarMir(i.w+rd*1.55,ReflectFloorVP,MirrorFloor);"
		"float4 mir3=PlanarMir(i.w+n*.45,ReflectFloorVP,MirrorFloor);if(mir2.a>mir.a)mir=mir2;if(mir3.a>mir.a)mir=mir3;"
		// 鏡床/ギミック: 平面反射。鍵・金属小物は UV が外れても env で写り込みを補う
		"float mw=mirror*useMir*saturate(max(mir.a,.82)+mirror*.15);"
		"mw=max(mw,keyM*useMir*saturate(max(mir.a,.7)+.25));"
		"float3 chrome=mir.rgb*1.15+env*.35;"
		"float trapK=saturate(1.-abs(i.c.a-.44)*16.);float itemK=saturate(1.-abs(i.c.a-1.15)*18.);"
		"float glassK=saturate((i.c.a-1.18)*8.);float woodK=saturate(1.-abs(i.c.a-1.05)*40.);"
		"float prop=saturate(trapK+itemK+glassK+woodK);"
		"float3 tex=T0.Sample(SL,lerp(i.uv*4.2,i.uv,prop)).rgb;float3 det=T1.Sample(SL,lerp(i.uv*11.0,i.uv,prop)).rgb;"
		"float tm=saturate(dot(tex,tex)*3.);float dm=saturate(dot(det,det)*3.);"
		"float3 albedo=lerp(i.c.rgb,saturate(tex*lerp(float3(1,1,1),det*1.32,0.42*dm)),0.64*tm);"
		"albedo=lerp(albedo,saturate(lerp(tex,det,trapK)*i.c.rgb*1.15),prop);"
		"float3 lit=albedo*nd;"
		"float wet=.5+.5*sin(Misc.w*1.4+i.w.x+i.w.z);float wetSp=pow(saturate(dot(reflect(-l,n),v)),90)*sh*(.10+.08*wet)*(1-mirror);"
		"float glowP=.55+.45*sin(Misc.w*2.1+i.w.y*3);"
		"float3 c=lerp(lit,chrome*(.12+.88*saturate(i.c.rgb+.25)),mw*(.98-.18*glass))+env*((.14+fr*.42)*(1-mw*.9)+mirror*.28*(1-glass))*lerp(.5,1,sh)+float3(1,.96,.9)*sp*(.42+mirror*1.4)+float3(.7,.9,1)*pow(fr,1.2)*mirror*.5;"
		"c=lerp(c,env*(.45+.55*saturate(i.c.rgb+.2))+mir.rgb*.55,keyM*(.42+.28*fr)*useMir);"
		"c+=float3(.85,.95,1)*wetSp;c+=float3(1,.9,.35)*sp*keyM*(.5+.5*glowP)*1.2;c+=float3(.75,.7,.65)*sp*doorM*.8;"
		"c+=i.c.rgb*(.04+.05*glowP)*saturate(i.c.a-.35)*(1-mirror)*.35;"
		"float al=mirror>0?lerp(lerp(.96,.90,mw),lerp(.84,.74,mw),glass):saturate(i.c.a);float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));"
		"fg=saturate(fg+max(0,Fog.w-i.w.y)*Fog.z);fg=fg*fg*(3-2*fg);"
		"return float4(lerp(c,float3(.52,.66,.84),fg*.55*(1-mw*.7)),al);}"
		"struct HV{float2 p:POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};struct HO{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.uv=x.uv;o.c=x.c;return o;}"
		"float4 PSH(HO i):SV_Target{if(i.uv.x<-0.5)return i.c;float4 t=T0.Sample(SL,i.uv);return float4(t.rgb*i.c.rgb,t.a*i.c.a);}"
		"float4 PSLINE(HO i):SV_Target{float t=saturate(1.-abs(i.uv.y-.5)*2.4);float cap=saturate(min(i.uv.x,1.-i.uv.x)*10.);return float4(i.c.rgb,i.c.a*t*cap);}"
		"struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}"
		"float4 SSR(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);float z=Depth.Sample(SP,i.uv).r;float2 dir=float2((i.uv.x-.5)*.03,-.018);"
		"float3 r=0;float hit=0;[loop]for(int k=1;k<20;k++){float2 u=i.uv+dir*k;if(any(u<0)||any(u>1))break;float dz=Depth.Sample(SP,u).r;if(dz+0.001<z){r=T0.Sample(SL,u).rgb;hit=1;break;}}"
		"float metal=saturate((z-.10)*2.5)*0.12;return float4(lerp(c.rgb,lerp(c.rgb,r,hit),metal),1);}"
		"float4 DOFP(Q i):SV_Target{float zd=Depth.Sample(SP,i.uv).r;const float zn=.05,zf=80.;"
		"float eyeZ=zn*zf/max(1e-4,zf-zd*(zf-zn));"
		// Dof.x=ぼけ開始距離(ワールド≒マス), Dof.y=立ち上がり幅, Dof.z=最大ぼけ(px) — 手前はぼかさない
		"float coc=saturate((eyeZ-Dof.x)/max(.05,Dof.y));coc=coc*coc*(3.-2.*coc);"
		"float b=coc*Dof.z;if(b<0.35)return T0.Sample(SL,i.uv);float2 px=float2(b,b)*Screen.zw;"
		"float4 c=T0.Sample(SL,i.uv)*.28;"
		"c+=(T0.Sample(SL,i.uv+float2(px.x,0))+T0.Sample(SL,i.uv-float2(px.x,0))+T0.Sample(SL,i.uv+float2(0,px.y))+T0.Sample(SL,i.uv-float2(0,px.y)))*.13;"
		"c+=(T0.Sample(SL,i.uv+px)+T0.Sample(SL,i.uv-px)+T0.Sample(SL,i.uv+float2(px.x,-px.y))+T0.Sample(SL,i.uv+float2(-px.x,px.y)))*.05;"
		"return c;}"
		"float4 FIN(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);if(Misc.z>8.f){c.a*=saturate(Misc.y);return c;}float v=saturate(1-dot((i.uv-.5)*1.05,(i.uv-.5)*1.05));c.rgb*=lerp(.92,1.08,v);"
		"float th=Eye.w;float3 tone=float3(1.05,1.02,.98);if(th>2.5)tone=float3(1.12,1.04,.98);else if(th>1.5)tone=float3(1.10,1.05,1.00);else if(th>.5)tone=float3(1.08,1.10,1.14);"
		"c.rgb=saturate(c.rgb*tone);return c;}";
	ID3DBlob *b[11]={0}, *err=NULL;
	const char* entries[11]={"VST","HST","DST","PSW","VSS","PSS","VSH","PSH","VSQ","SSR","DOFP"};
	const char* profiles[11]={"vs_5_0","hs_5_0","ds_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","ps_5_0"};
	auto compile=[&](const char* entry,const char* prof,ID3DBlob** out)->HRESULT{
		S3M_RELEASE(err);
		HRESULT chr=D3DCompile(hlsl,strlen(hlsl),NULL,NULL,NULL,entry,prof,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,out,&err);
		if(FAILED(chr)){ S3M_RELEASE(err); chr=D3DCompile(hlsl,strlen(hlsl),NULL,NULL,NULL,entry,prof,D3DCOMPILE_OPTIMIZATION_LEVEL1,0,out,&err); }
		if(FAILED(chr)) m_dxFailHr=chr;
		S3M_RELEASE(err);
		return chr;
	};
	for(int i=0;i<11;i++) {
		if(FAILED(compile(entries[i],profiles[i],&b[i]))) {
			for(int j=0;j<11;j++) S3M_RELEASE(b[j]); return FALSE;
		}
	}
	ID3DBlob* bf=NULL;
	if(FAILED(compile("FIN","ps_5_0",&bf))) {
		for(int i=0;i<11;i++) S3M_RELEASE(b[i]); return FALSE;
	}
	ID3DBlob* bline=NULL;
	if(FAILED(compile("PSLINE","ps_5_0",&bline))) {
		for(int i=0;i<11;i++) S3M_RELEASE(b[i]); S3M_RELEASE(bf); return FALSE;
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
	hr|=m_dev->CreatePixelShader(bline->GetBufferPointer(),bline->GetBufferSize(),NULL,&m_psHudLine);
	hr|=m_dev->CreateVertexShader(b[8]->GetBufferPointer(),b[8]->GetBufferSize(),NULL,&m_vsPost);
	hr|=m_dev->CreatePixelShader(b[9]->GetBufferPointer(),b[9]->GetBufferSize(),NULL,&m_psSsr);
	hr|=m_dev->CreatePixelShader(b[10]->GetBufferPointer(),b[10]->GetBufferSize(),NULL,&m_psDof);
	hr|=m_dev->CreatePixelShader(bf->GetBufferPointer(),bf->GetBufferSize(),NULL,&m_psFinal);
	D3D11_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
	D3D11_INPUT_ELEMENT_DESC ih[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
	hr|=m_dev->CreateInputLayout(il,4,b[0]->GetBufferPointer(),b[0]->GetBufferSize(),&m_ilPatch);
	hr|=m_dev->CreateInputLayout(il,4,b[4]->GetBufferPointer(),b[4]->GetBufferSize(),&m_ilSolid);
	hr|=m_dev->CreateInputLayout(ih,3,b[6]->GetBufferPointer(),b[6]->GetBufferSize(),&m_ilHud);
	for(int i=0;i<11;i++) S3M_RELEASE(b[i]); S3M_RELEASE(bf); S3M_RELEASE(bline);
	return SUCCEEDED(hr);
}

BOOL CS3mView::CreateProcTextures()
{
	// 階テーマごと 512² アトラス（4×4=16種）＋床テクスチャ
	const int AW=512,AH=512,TW=128,TH=128;
	auto hash=[&](int x,int y,int s)->int{return ((x*73856093)^(y*19349663)^(s*83492791))&255;};
	auto noise = [&](float x, float y, int s) -> float {
		int ix = (int)floorf(x); float fx = x - ix;
		int iy = (int)floorf(y); float fy = y - iy;
		float u = fx * fx * (3.f - 2.f * fx);
		float v = fy * fy * (3.f - 2.f * fy);
		auto h = [&](int i, int j) { return (float)hash(i, j, s) * (1.f / 255.f); };
		float n00 = h(ix, iy), n10 = h(ix + 1, iy);
		float n01 = h(ix, iy + 1), n11 = h(ix + 1, iy + 1);
		float nx0 = n00 + u * (n10 - n00);
		float nx1 = n01 + u * (n11 - n01);
		return nx0 + v * (nx1 - nx0);
	};
	auto fbm = [&](float x, float y, int s, int oct) -> float {
		float val = 0.f; float amp = 1.f; float maxAmp = 0.f;
		for (int i = 0; i < oct; i++) {
			val += noise(x, y, s + i) * amp;
			maxAmp += amp;
			amp *= 0.5f;
			x *= 2.f; y *= 2.f;
		}
		return val / maxAmp;
	};
	auto genWall=[&](int theme,DWORD* atlas){
		for(int ty=0;ty<4;ty++)for(int tx=0;tx<4;tx++){
			const int vid=ty*4+tx;
			for(int ly=0;ly<TH;ly++)for(int lx=0;lx<TW;lx++){
				int x=tx*TW+lx,y=ty*TH+ly;
				int n=(int)(fbm((float)lx * 0.15f, (float)ly * 0.15f, vid+theme*97, 4) * 255.f) - 128;
				BYTE r,g,b,a;
				if(theme==0){ // 地上：レンガ＋草木・苔・花（αでテッセ変位＝ふんわり凹凸）
					int row=ly/16,bx=(lx+((row&1)?32:0))&63,by=ly&15;BOOL mortar=by<2||bx<2;
					if(vid==0){r=(BYTE)(mortar?110:190+n/8);g=(BYTE)(mortar?90:125+n/10);b=(BYTE)(mortar?70:85+n/12);a=(BYTE)(mortar?70:128+n/8);}
					else if(vid<=3){r=(BYTE)(mortar?90:140+n/10);g=(BYTE)(mortar?100:160+n/8);b=(BYTE)(mortar?70:90+n/12);a=(BYTE)(mortar?90:145+n/6);}
					else if(vid<=6){ // 苔・葉の塊（ソフトなαブロブ）
						int blob=hash(lx/5,ly/5,vid+3);int soft=255-min(255,((lx%20-10)*(lx%20-10)+(ly%20-10)*(ly%20-10))*3);
						r=(BYTE)(55+n/14);g=(BYTE)(140+n/5+(blob&31));b=(BYTE)(45+n/16);
						a=(BYTE)(110+soft/3+(n>0?n/4:0));
					}else if(vid<=9){r=(BYTE)(120+n/10);g=(BYTE)(110+n/10);b=(BYTE)(90+n/12);BOOL fl=((hash(lx/4,ly/4,vid)&31)==0);if(fl){r=220;g=80;b=140;a=165;}else a=140;}
					else if(vid<=12){r=(BYTE)(150+n/8);g=(BYTE)(145+n/8);b=(BYTE)(120+n/10);BOOL crack=((lx*3+ly)&31)<2;if(crack){r=80;g=70;b=60;}a=(BYTE)(crack?100:150);}
					else{ // 濃い植生
						int v2=hash(lx/4,ly/4,vid+9);r=(BYTE)(40+n/14);g=(BYTE)(100+n/5+(v2&40));b=(BYTE)(35+n/14);
						a=(BYTE)(150+abs(n)/3+((v2&15)<<1));
					}
				}else if(theme==1){ // 地下1：湿ったダンジョン石（大割り・レンガではない）
					const int tw=28+(vid&3)*4,th=22+((vid>>1)&3)*3;
					const int ox=lx%tw,oy=ly%th;
					BOOL seam=ox<3||oy<3;
					int grain=hash(lx/2,ly/2,vid+11)-128;
					r=(BYTE)(seam?48:85+n/9+grain/14);
					g=(BYTE)(seam?62:105+n/8+grain/12);
					b=(BYTE)(seam?78:135+n/7+grain/10);
					BOOL moss=((hash(lx/5,ly/5,vid)&15)==0)&&!seam;if(moss){r=55;g=120;b=90;}
					BOOL drip=((lx+ly*3+vid*9)&53)<2;if(drip){r=65;g=130;b=185;}
					a=(BYTE)(seam?95:180+(n>40?15:0));
				}else if(theme==2){ // 地下2：錆びた金属パネル（格子・リベット）
					const int pw=32,ph=32;
					const int ox=lx%pw,oy=ly%ph;
					BOOL seam=ox<2||oy<2||ox>=pw-1||oy>=ph-1;
					r=(BYTE)(seam?55:120+n/10);g=(BYTE)(seam?50:78+n/12);b=(BYTE)(seam?48:58+n/14);
					BOOL rivet=(ox>=4&&ox<=8&&oy>=4&&oy<=8)||(ox>=pw-9&&ox<=pw-5&&oy>=4&&oy<=8)
						||(ox>=4&&ox<=8&&oy>=ph-9&&oy<=ph-5)||(ox>=pw-9&&ox<=pw-5&&oy>=ph-9&&oy<=ph-5);
					if(rivet&&!seam){r=200;g=175;b=130;}
					BOOL rust=((hash(lx/4,ly/4,vid+3)&7)==0)&&!seam;if(rust){r=165;g=75;b=35;}
					BOOL scratch=((lx*7+ly)&63)<1;if(scratch){r=170;g=170;b=160;}
					a=(BYTE)(seam?110:200);
				}else{ // 地下3：火山岩＋赤熱の割れ目（有機ノイズ）
					int v1=hash(lx/6,ly/6,vid),v2=hash(lx/3+3,ly/3+5,vid+4);
					int base=40+(v1&31)+(n/10);
					r=(BYTE)(base);g=(BYTE)(base*3/4);b=(BYTE)(base*4/5);
					BOOL crack=((lx*5+ly*3+vid*11+v2)&47)<2||((hash(lx,ly,7)&63)==0);
					if(crack){r=230;g=95;b=28;a=230;}
					else{
						BOOL ember=((hash(lx/2,ly/2,vid+19)&31)==0);if(ember){r=180;g=60;b=20;}
						a=(BYTE)(165+abs(n)/5);
					}
				}
				atlas[y*AW+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
			}
		}
	};
	auto genFloor=[&](int theme,DWORD* p,int W,int H){
		for(int y=0;y<H;y++)for(int x=0;x<W;x++){
			int n=(int)(fbm((float)x * 0.1f, (float)y * 0.1f, theme * 13, 3) * 255.f) - 128;BYTE r,g,b;
			if(theme==0){ // 土っぽい石畳
				BYTE v=(BYTE)(145+n);r=v;g=(BYTE)(v*4/5);b=(BYTE)(v*2/3);
				if(((x&15)<1)||((y&15)<1)){r=(BYTE)(r*3/4);g=(BYTE)(g*3/4);b=(BYTE)(b*3/4);}
			}else if(theme==1){ // 湿った大きな石床
				const int tw=20,th=16;BOOL seam=((x%tw)<2)||((y%th)<2);
				r=(BYTE)(seam?50:72+n);g=(BYTE)(seam?65:95+n);b=(BYTE)(seam?80:118+n/2);
				if(((x*3+y)&41)<1){r=60;g=110;b=150;}
			}else if(theme==2){ // 金属グレー＋継ぎ目・錆
				r=(BYTE)(58+n/2);g=(BYTE)(60+n/2);b=(BYTE)(64+n/2);
				if(((x&15)<1)||((y&15)<1)){r=90;g=70;b=40;}
				if(((x^y)&31)==0){r=150;g=85;b=45;}
			}else{ // 炭＋赤脈
				r=(BYTE)(38+n/2);g=(BYTE)(30+n/3);b=(BYTE)(34+n/3);
				if(((x*3+y)&31)<2){r=170;g=55;b=22;}
				if(((x+y*2)&63)<1){r=90;g=40;b=25;}
			}
			p[y*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;
		}
	};
	D3D11_TEXTURE2D_DESC d={}; d.Width=AW;d.Height=AH;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	DWORD* atlas=new DWORD[AW*AH];
	static const int kWallRes[S3M_THEME_N]={IDR_S3TEX_M_WALL0,IDR_S3TEX_M_WALL1,IDR_S3TEX_M_WALL2,IDR_S3TEX_M_WALL3};
	for(int th=0;th<S3M_THEME_N;th++){
		if(!Soft3DTexLoadPngRes(kWallRes[th],atlas,AW,AH))
			genWall(th,atlas);
		D3D11_SUBRESOURCE_DATA sd={atlas,AW*4,0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texBrick[th]))||FAILED(m_dev->CreateShaderResourceView(m_texBrick[th],NULL,&m_srvBrick[th]))){delete[] atlas;return FALSE;}
	}
	delete[] atlas;
	const int W=256,H=256; DWORD* p=new DWORD[W*H];
	d.Width=W;d.Height=H;
	static const int kFloorRes[S3M_THEME_N]={IDR_S3TEX_M_FLOOR0,IDR_S3TEX_M_FLOOR1,IDR_S3TEX_M_FLOOR2,IDR_S3TEX_M_FLOOR3};
	for(int th=0;th<S3M_THEME_N;th++){
		if(!Soft3DTexLoadPngRes(kFloorRes[th],p,W,H))
			genFloor(th,p,W,H);
		D3D11_SUBRESOURCE_DATA sd={p,W*4,0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texFloor[th]))||FAILED(m_dev->CreateShaderResourceView(m_texFloor[th],NULL,&m_srvFloor[th]))){delete[] p;return FALSE;}
	}
	static const int kWallDetRes[S3M_THEME_N]={IDR_S3TEX_M_WALL0_D,IDR_S3TEX_M_WALL1_D,IDR_S3TEX_M_WALL2_D,IDR_S3TEX_M_WALL3_D};
	static const int kFloorDetRes[S3M_THEME_N]={IDR_S3TEX_M_FLOOR0_D,IDR_S3TEX_M_FLOOR1_D,IDR_S3TEX_M_FLOOR2_D,IDR_S3TEX_M_FLOOR3_D};
	for(int th=0;th<S3M_THEME_N;th++){
		if(Soft3DTexLoadPngRes(kWallDetRes[th],p,W,H)){
			D3D11_SUBRESOURCE_DATA sd={p,W*4,0};
			if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texBrickD[th]))||FAILED(m_dev->CreateShaderResourceView(m_texBrickD[th],NULL,&m_srvBrickD[th]))){delete[] p;return FALSE;}
		}
		if(Soft3DTexLoadPngRes(kFloorDetRes[th],p,W,H)){
			D3D11_SUBRESOURCE_DATA sd={p,W*4,0};
			if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texFloorD[th]))||FAILED(m_dev->CreateShaderResourceView(m_texFloorD[th],NULL,&m_srvFloorD[th]))){delete[] p;return FALSE;}
		}
	}
	auto up2=[&](int id, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv){
		if(!Soft3DTexLoadPngRes(id,p,W,H)) return;
		D3D11_SUBRESOURCE_DATA sd={p,(UINT)(W*4),0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,tex))||FAILED(m_dev->CreateShaderResourceView(*tex,NULL,srv))){
			S3M_RELEASE(*srv); S3M_RELEASE(*tex);
		}
	};
	up2(IDR_S3TEX_M_ITEM,&m_texItem,&m_srvItem);
	up2(IDR_S3TEX_M_GIMMICK,&m_texGimmick,&m_srvGimmick);
	up2(IDR_S3TEX_M_GLASS,&m_texGlass,&m_srvGlass);
	up2(IDR_S3TEX_M_BRICK2,&m_texBrick2,&m_srvBrick2);
	{
		const int EW=256,EH=256;
		DWORD* cube=new (std::nothrow) DWORD[EW*EH*6];
		if(!cube){delete[] p;return FALSE;}
		BOOL photo=Soft3DTexLoadPngRes(IDR_S3TEX_R_ENV,cube,EW,EH*6);
		int cw=photo?EW:128, ch=photo?EH:128;
		if(!photo){
			delete[] cube;
			cube=new (std::nothrow) DWORD[128*128*6];
			if(!cube){delete[] p;return FALSE;}
			for(int f=0;f<6;f++){for(int y=0;y<128;y++)for(int x=0;x<128;x++){float t=(float)y/127.f;BYTE r=(BYTE)(110+70*(1-t)),g=(BYTE)(145+55*(1-t)),b=(BYTE)(175+40*(1-t));if(f==2){r=(BYTE)(85+35*t);g=(BYTE)(120+45*t);b=(BYTE)(60+25*t);}cube[(f*128+y)*128+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;}}
		}
		d.Width=cw;d.Height=ch;d.ArraySize=6;d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
		D3D11_SUBRESOURCE_DATA cs[6]={};
		for(int f=0;f<6;f++){cs[f].pSysMem=cube+f*cw*ch;cs[f].SysMemPitch=cw*4;cs[f].SysMemSlicePitch=cw*ch*4;}
		if(FAILED(m_dev->CreateTexture2D(&d,cs,&m_texEnv))||FAILED(m_dev->CreateShaderResourceView(m_texEnv,NULL,&m_srvEnv))){delete[] cube;delete[] p;return FALSE;}
		{
			DWORD* cubeIn=new (std::nothrow) DWORD[256*256*6];
			if(cubeIn && Soft3DTexLoadPngRes(IDR_S3TEX_M_ENV,cubeIn,256,256*6)){
				D3D11_TEXTURE2D_DESC ed=d; ed.Width=256; ed.Height=256; ed.ArraySize=6; ed.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
				D3D11_SUBRESOURCE_DATA ics[6]={};
				for(int f=0;f<6;f++){ics[f].pSysMem=cubeIn+f*256*256;ics[f].SysMemPitch=256*4;ics[f].SysMemSlicePitch=256*256*4;}
				if(FAILED(m_dev->CreateTexture2D(&ed,ics,&m_texEnvIn))||FAILED(m_dev->CreateShaderResourceView(m_texEnvIn,NULL,&m_srvEnvIn))){
					S3M_RELEASE(m_srvEnvIn); S3M_RELEASE(m_texEnvIn);
				}
			}
			delete[] cubeIn;
		}
		delete[] cube;
	}
	delete[] p;
	// 鏡壁／鏡床：滑らかな銀シアン鏡＋細い傷（割れすぎない）
	{
		const int MW=256,MH=256;
		DWORD* mir=new DWORD[MW*MH];
		auto putMir=[&](BOOL wall){
			for(int y=0;y<MH;y++)for(int x=0;x<MW;x++){
				const float u=(float)x/(float)(MW-1),v=(float)y/(float)(MH-1);
				const float band=.55f+.45f*sinf(v*9.f+u*1.2f);
				const float gloss=powf(max(0.f,1.f-fabsf(u-v)*.85f),3.2f);
				const float fres=powf(max(fabsf(u-.5f),fabsf(v-.5f))*2.f,1.6f);
				const int qx=x/(MW/3),qy=y/(MH/3);
				const int lx=x%(MW/3),ly=y%(MH/3);
				const BOOL seam=lx<1||ly<1||lx>=(MW/3)-1||ly>=(MH/3)-1;
				const BOOL scratch=((x*13+y*7)&127)<1||((x+y*3)&255)==0;
				float base=wall?168.f:185.f;
				float r=base+38.f*band+55.f*gloss+30.f*fres;
				float g=base+52.f*band+60.f*gloss+35.f*fres+18.f;
				float b=base+70.f*band+70.f*gloss+40.f*fres+35.f;
				if(seam){r*=.72f;g*=.78f;b*=.85f;}
				if(scratch&&!seam){r=min(255.f,r+40.f);g=min(255.f,g+45.f);b=min(255.f,b+50.f);}
				if(((qx+qy)&1)==0){r*=.96f;g*=.97f;b*=.98f;}
				if(r>255.f)r=255.f;if(g>255.f)g=255.f;if(b>255.f)b=255.f;
				if(r<40.f)r=40.f;if(g<50.f)g=50.f;if(b<70.f)b=70.f;
				const BYTE a=wall?(BYTE)(seam?160:230):255;
				mir[y*MW+x]=((DWORD)a<<24)|(((DWORD)(BYTE)r)<<16)|(((DWORD)(BYTE)g)<<8)|(BYTE)b;
			}
		};
		D3D11_TEXTURE2D_DESC md={};md.Width=MW;md.Height=MH;md.MipLevels=1;md.ArraySize=1;
		md.Format=DXGI_FORMAT_B8G8R8A8_UNORM;md.SampleDesc.Count=1;md.Usage=D3D11_USAGE_IMMUTABLE;md.BindFlags=D3D11_BIND_SHADER_RESOURCE;
		if(!Soft3DTexLoadPngRes(IDR_S3TEX_M_MIRWALL,mir,MW,MH))
			putMir(TRUE);
		D3D11_SUBRESOURCE_DATA sd={mir,MW*4,0};
		if(FAILED(m_dev->CreateTexture2D(&md,&sd,&m_texMirrorWall))||FAILED(m_dev->CreateShaderResourceView(m_texMirrorWall,NULL,&m_srvMirrorWall))){delete[] mir;return FALSE;}
		if(!Soft3DTexLoadPngRes(IDR_S3TEX_M_MIRFLOOR,mir,MW,MH))
			putMir(FALSE);
		sd.pSysMem=mir;
		if(FAILED(m_dev->CreateTexture2D(&md,&sd,&m_texMirrorFloor))||FAILED(m_dev->CreateShaderResourceView(m_texMirrorFloor,NULL,&m_srvMirrorFloor))){delete[] mir;return FALSE;}
		delete[] mir;
	}
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
	m_dxFailStage = 0; m_dxFailHr = S_OK;
	if (!m_hWnd || !::IsWindow(m_hWnd)) { m_dxFailStage = 1; return FALSE; }
	UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	flags|=D3D11_CREATE_DEVICE_DEBUG;
#endif
	// レース同様: 11_0 要求、戻りは >=11_0 で受理（旧コードの !=11_0 は環境差で誤爆しうる）
	D3D_FEATURE_LEVEL req=D3D_FEATURE_LEVEL_11_0, got=(D3D_FEATURE_LEVEL)0;
	HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,flags,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
		flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,flags,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	}
	if(FAILED(hr)) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,flags&~D3D11_CREATE_DEVICE_DEBUG,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr) || got < D3D_FEATURE_LEVEL_11_0) { m_dxFailStage = 2; m_dxFailHr = hr; return FALSE; }
	IDXGIDevice* xd=NULL;IDXGIAdapter* xa=NULL;IDXGIFactory2* f2=NULL;IDXGIFactory* f1=NULL;
	if(FAILED(hr=m_dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(hr=xd->GetAdapter(&xa))) { m_dxFailStage = 3; m_dxFailHr = hr; S3M_RELEASE(xd); return FALSE; }
	xa->GetParent(__uuidof(IDXGIFactory2),(void**)&f2);
	// FLIP_DISCARD 非対応環境向けにレース同様フォールバック
	DXGI_SWAP_CHAIN_DESC1 s={};s.Width=0;s.Height=0;s.Format=DXGI_FORMAT_B8G8R8A8_UNORM;s.SampleDesc.Count=1;s.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;s.BufferCount=2;s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;s.AlphaMode=DXGI_ALPHA_MODE_IGNORE;s.Scaling=DXGI_SCALING_STRETCH;
	if(f2){IDXGISwapChain1* sc1=NULL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);if(FAILED(hr)){s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);}if(FAILED(hr)){s.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;s.BufferCount=1;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);}if(SUCCEEDED(hr))m_swap=sc1;}else hr=E_FAIL;
	if(FAILED(hr)){xa->GetParent(__uuidof(IDXGIFactory),(void**)&f1);DXGI_SWAP_CHAIN_DESC o={};o.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;o.SampleDesc.Count=1;o.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;o.BufferCount=1;o.OutputWindow=m_hWnd;o.Windowed=TRUE;o.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;hr=f1?f1->CreateSwapChain(m_dev,&o,&m_swap):E_FAIL;}
	S3M_RELEASE(f1);S3M_RELEASE(f2);S3M_RELEASE(xa);S3M_RELEASE(xd);
	if(FAILED(hr)||!m_swap){ m_dxFailStage = 4; m_dxFailHr = hr; return FALSE; }
	D3D11_BUFFER_DESC bd={};bd.ByteWidth=((sizeof(S3MFrameCB)+15)/16)*16;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_cbFrame))){ m_dxFailStage = 5; m_dxFailHr = hr; return FALSE; }
	if(!CreateShaders()){ m_dxFailStage = 6; if(m_dxFailHr==S_OK) m_dxFailHr = E_FAIL; return FALSE; }
	if(!CreateProcTextures()){ m_dxFailStage = 7; m_dxFailHr = E_FAIL; return FALSE; }
	bd.ByteWidth=m_vbDynBytes;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_vbDyn))){
		static const UINT kVbTry[]={6u*1024u*1024u,4u*1024u*1024u,3u*1024u*1024u,2u*1024u*1024u};
		hr=E_FAIL;
		for(int ti=0;ti<4 && FAILED(hr);ti++){
			m_vbDynBytes=kVbTry[ti]; bd.ByteWidth=m_vbDynBytes;
			hr=m_dev->CreateBuffer(&bd,NULL,&m_vbDyn);
		}
		if(FAILED(hr)){ m_dxFailStage = 8; m_dxFailHr = hr; return FALSE; }
	}
	bd.ByteWidth=m_vbHudBytes;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_vbHud))){ m_dxFailStage = 9; m_dxFailHr = hr; return FALSE; }
	D3D11_SAMPLER_DESC ss={};ss.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;ss.MaxLOD=D3D11_FLOAT32_MAX;m_dev->CreateSamplerState(&ss,&m_sampLin);ss.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;m_dev->CreateSamplerState(&ss,&m_sampPoint);
	ss.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;ss.ComparisonFunc=D3D11_COMPARISON_LESS;ss.BorderColor[0]=ss.BorderColor[1]=ss.BorderColor[2]=ss.BorderColor[3]=1.f;m_dev->CreateSamplerState(&ss,&m_sampCmp);
	D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_BACK;rs.DepthClipEnable=TRUE;m_dev->CreateRasterizerState(&rs,&m_rsSolid);
	rs.CullMode=D3D11_CULL_NONE;m_dev->CreateRasterizerState(&rs,&m_rsNoCull);
	D3D11_RASTERIZER_DESC rss={};rss.FillMode=D3D11_FILL_SOLID;rss.CullMode=D3D11_CULL_NONE;rss.DepthClipEnable=TRUE;
	rss.DepthBias=1000;rss.SlopeScaledDepthBias=1.5f;rss.DepthBiasClamp=0.f;m_dev->CreateRasterizerState(&rss,&m_rsShadow);
	D3D11_DEPTH_STENCIL_DESC ds={};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;m_dev->CreateDepthStencilState(&ds,&m_dssWrite);ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;m_dev->CreateDepthStencilState(&ds,&m_dssRead);ds.DepthEnable=FALSE;m_dev->CreateDepthStencilState(&ds,&m_dssOff);
	D3D11_BLEND_DESC bl={};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;m_dev->CreateBlendState(&bl,&m_bsOpaque);bl.RenderTarget[0].BlendEnable=TRUE;bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bl.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bl.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;m_dev->CreateBlendState(&bl,&m_bsAlpha);bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_ONE;m_dev->CreateBlendState(&bl,&m_bsAdd);
	{
		D3D11_TEXTURE2D_DESC td={};td.Width=S3M_SHADOW_SIZE;td.Height=S3M_SHADOW_SIZE;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
		if(FAILED(hr=m_dev->CreateTexture2D(&td,NULL,&m_shadowTex))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
		D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(hr=m_dev->CreateDepthStencilView(m_shadowTex,&dd,&m_shadowDsv))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
		D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
		if(FAILED(hr=m_dev->CreateShaderResourceView(m_shadowTex,&sd,&m_shadowSrv))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
	}
	{
		// 鏡用 RT（レースに無い迷路専用）。連番 RT が弾かれる環境向けにサイズ縮退
		static const int kMirSz[] = { S3M_MIRROR_SIZE, 256, 192, 128, 64 };
		HRESULT mirHr = E_FAIL;
		m_mirrorSize = S3M_MIRROR_SIZE;
		for (int si = 0; si < 5; si++) {
			const int mirSz = kMirSz[si];
			for (int i = 0; i < S3M_MIRROR_N; i++) {
				S3M_RELEASE(m_mirrorSrv[i]); S3M_RELEASE(m_mirrorRtv[i]); S3M_RELEASE(m_mirrorTex[i]);
			}
			S3M_RELEASE(m_mirrorDsv); S3M_RELEASE(m_mirrorDs);
			D3D11_TEXTURE2D_DESC td={};td.Width=mirSz;td.Height=mirSz;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
			mirHr = S_OK;
			for(int i=0;i<S3M_MIRROR_N;i++){
				if(FAILED(mirHr=m_dev->CreateTexture2D(&td,NULL,&m_mirrorTex[i]))) break;
				if(FAILED(mirHr=m_dev->CreateRenderTargetView(m_mirrorTex[i],NULL,&m_mirrorRtv[i]))) break;
				if(FAILED(mirHr=m_dev->CreateShaderResourceView(m_mirrorTex[i],NULL,&m_mirrorSrv[i]))) break;
			}
			if (FAILED(mirHr)) continue;
			td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
			if(FAILED(mirHr=m_dev->CreateTexture2D(&td,NULL,&m_mirrorDs))) continue;
			D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
			if(FAILED(mirHr=m_dev->CreateDepthStencilView(m_mirrorDs,&dd,&m_mirrorDsv))) continue;
			m_mirrorSize = mirSz;
			break;
		}
		if(FAILED(mirHr)){ m_dxFailStage = 11; m_dxFailHr = mirHr; return FALSE; }
	}
	CRect rc;GetClientRect(&rc);
	if(!ResizeDx(max(8,rc.Width()),max(8,rc.Height()))){ m_dxFailStage = 12; m_dxFailHr = E_FAIL; return FALSE; }
	return TRUE;
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
void CS3mView::ReleaseCalloutTexture()
{
	S3M_RELEASE(m_srvCallout);S3M_RELEASE(m_texCallout);m_calloutW=m_calloutH=0;m_calloutN=0;
}

BOOL CS3mView::BakeBadgeTexture(const wchar_t* text)
{
	if (!m_dev || !text) return FALSE;
	ReleaseBadgeTexture();
	const int w = 640, h = 96;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	Soft3DTextD2D_FillRect(cv, 0, 0, (float)w, (float)h, 110, 6, 8, 14);
	Soft3DTextD2D_DrawTextShadow(cv, text, 6.f, 4.f, (float)(w - 12), (float)(h - 8),
		52.f, 1, 1, 1, 250, 245, 248, 255, 2.f, 2.f, 160, 0, 0, 0);
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texBadge);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texBadge, NULL, &m_srvBadge);
	Soft3DTextD2D_Release(cv);
	m_badgeW = w; m_badgeH = h;
	return SUCCEEDED(hr);
}

void CS3mView::ReleaseFloorBarTexture()
{
	S3M_RELEASE(m_srvFloorBar);S3M_RELEASE(m_texFloorBar);m_floorBarW=m_floorBarH=0;
}

BOOL CS3mView::BakeFloorBarTexture(int nFloors, int viewFloor, int playerFloor, const wchar_t* const* labels)
{
	if (!m_dev || nFloors < 1 || !labels) return FALSE;
	if (nFloors > 4) nFloors = 4;
	ReleaseFloorBarTexture();
	const int tabW = 220, recW = 180, gap = 10, pad = 10, h = 80;
	const int w = pad * 2 + nFloors * tabW + (nFloors - 1) * gap + gap + recW;
	static const BYTE kTheme[4][4] = {
		{230, 48, 95, 58}, {230, 42, 68, 110}, {230, 110, 62, 38}, {230, 120, 40, 36}
	};
	static const BYTE kThemeSel[4][4] = {
		{245, 78, 150, 90}, {245, 70, 115, 175}, {245, 170, 95, 55}, {245, 185, 65, 55}
	};
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	float x = (float)pad;
	for (int i = 0; i < nFloors; i++) {
		const int th = (i <= 0) ? 0 : ((i == 1) ? 1 : ((i == 2) ? 2 : 3));
		const BOOL sel = (i == viewFloor);
		const BYTE* col = sel ? kThemeSel[th] : kTheme[th];
		if (sel) Soft3DTextD2D_FillRect(cv, x, 4.f, (float)tabW, (float)(h - 8), 220, 255, 255, 255);
		Soft3DTextD2D_FillRect(cv, x + (sel ? 2.f : 0.f), 4.f + (sel ? 2.f : 0.f),
			(float)tabW - (sel ? 4.f : 0.f), (float)(h - 8) - (sel ? 4.f : 0.f),
			col[0], col[1], col[2], col[3]);
		if (i == playerFloor)
			Soft3DTextD2D_FillRect(cv, x + 3.f, 7.f, (float)tabW - 6.f, 3.f, 255, 255, 210, 70);
		Soft3DTextD2D_DrawTextShadow(cv, labels[i] ? labels[i] : L"?", x + 4.f, 8.f, (float)(tabW - 8), (float)(h - 16),
			36.f, 1, 1, 1, 250, 250, 252, 255, 1.8f, 1.8f, 140, 0, 0, 0);
		m_floorBarTabX0[i] = x / (float)w;
		m_floorBarTabX1[i] = (x + tabW) / (float)w;
		x += tabW + gap;
	}
	for (int i = nFloors; i < 4; i++) { m_floorBarTabX0[i] = 0; m_floorBarTabX1[i] = 0; }
	x += gap;
	Soft3DTextD2D_FillRect(cv, x, 4.f, (float)recW, (float)(h - 8), 220, 255, 255, 255);
	Soft3DTextD2D_FillRect(cv, x + 2.f, 6.f, (float)recW - 4.f, (float)(h - 12), 235, 28, 32, 44);
	Soft3DTextD2D_DrawTextShadow(cv,
		LL14(L"◎ 自機へ", L"◎ You", L"◎ Vous", L"◎ Tu", L"◎ Tú", L"◎ 나", L"◎ 自己", L"◎ You", L"◎ Вы", L"◎ Du", L"◎ Você", L"◎ Jij", L"◎ Ty", L"◎ Sen"),
		x, 6.f, (float)recW, (float)(h - 12), 36.f, 1, 1, 1, 250, 250, 252, 255, 1.8f, 1.8f, 140, 0, 0, 0);
	m_floorBarRecX0 = x / (float)w;
	m_floorBarRecX1 = (x + recW) / (float)w;
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texFloorBar);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texFloorBar, NULL, &m_srvFloorBar);
	Soft3DTextD2D_Release(cv);
	m_floorBarW = w; m_floorBarH = h;
	return SUCCEEDED(hr);
}

BOOL CS3mView::BakeClearTexture(const wchar_t* text,float /*alpha*/)
{
	// αはシェーダ Misc.y。ここは文字変化時のみ焼く
	ReleaseClearTexture();
	if (!m_dev || !text || !text[0]) return FALSE;
	const int w = max(256, m_vw), h = max(96, m_vh / 4);
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	const int tlen = (int)wcslen(text);
	float fontPx = (float)max(32, h / 2);
	if (tlen > 10) fontPx = (float)max(28, h / 3);
	if (tlen > 16) fontPx = (float)max(24, h / 4);
	Soft3DTextD2D_DrawTextShadow(cv, text, 6.f, 6.f, (float)(w - 12), (float)(h - 12),
		fontPx, 1, 1, 1, 255, 255, 230, 110, 3.f, 3.f, 180, 0, 0, 0);
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texClear);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texClear, NULL, &m_srvClear);
	Soft3DTextD2D_Release(cv);
	m_clearTexW = w; m_clearTexH = h;
	return SUCCEEDED(hr);
}

BOOL CS3mView::BakeTipTexture(const wchar_t* text)
{
	if (!m_dev || !text) return FALSE;
	ReleaseTipTexture();
	const int w = 960, h = 320;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	Soft3DTextD2D_FillRect(cv, 0, 0, (float)w, (float)h, 100, 8, 10, 16);
	Soft3DTextD2D_DrawTextShadow(cv, text, 16.f, 12.f, (float)(w - 28), (float)(h - 22),
		30.f, 1, 0, 0, 250, 240, 245, 255, 2.f, 2.f, 150, 0, 0, 0, 1);
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texTip);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texTip, NULL, &m_srvTip);
	Soft3DTextD2D_Release(cv);
	m_tipW = w; m_tipH = h;
	return SUCCEEDED(hr);
}

BOOL CS3mView::BakeCalloutAtlas(const wchar_t* const* labels, int nLabels)
{
	ReleaseCalloutTexture();
	if (!m_dev || !labels || nLabels < 1) return FALSE;
	if (nLabels > S3M_CALLOUT_MAX) nLabels = S3M_CALLOUT_MAX;
	const int cellW = 320, cellH = 72, cols = 4;
	const int rowsN = (nLabels + cols - 1) / cols;
	const int w = cellW * cols, h = cellH * rowsN;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	for (int i = 0; i < nLabels; i++) {
		const int col = i % cols, row = i / cols;
		const float x = (float)(col * cellW), y = (float)(row * cellH);
		Soft3DTextD2D_DrawTextShadow(cv, labels[i] ? labels[i] : L"", x + 6.f, y + 4.f, (float)(cellW - 12), (float)(cellH - 8),
			40.f, 1, 1, 1, 255, 250, 248, 255, 2.f, 2.f, 200, 0, 0, 0);
		const float pad = 2.f;
		m_calloutU0[i] = (x + pad) / (float)w;
		m_calloutV0[i] = (y + pad) / (float)h;
		m_calloutU1[i] = (x + cellW - pad) / (float)w;
		m_calloutV1[i] = (y + cellH - pad) / (float)h;
	}
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texCallout);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texCallout, NULL, &m_srvCallout);
	Soft3DTextD2D_Release(cv);
	m_calloutW = w; m_calloutH = h;
	m_calloutN = nLabels;
	return SUCCEEDED(hr);
}

void CS3mView::ReleaseDx()
{
	m_ready=FALSE;if(m_imm){m_imm->ClearState();m_imm->Flush();}
	ReleaseClearTexture();ReleaseTipTexture();ReleaseBadgeTexture();ReleaseFloorBarTexture();ReleaseCalloutTexture();S3M_RELEASE(m_srvMap);S3M_RELEASE(m_texMap);
	S3M_RELEASE(m_srvBrick2);S3M_RELEASE(m_texBrick2);S3M_RELEASE(m_srvGlass);S3M_RELEASE(m_texGlass);
	S3M_RELEASE(m_srvGimmick);S3M_RELEASE(m_texGimmick);S3M_RELEASE(m_srvItem);S3M_RELEASE(m_texItem);
	S3M_RELEASE(m_srvEnvIn);S3M_RELEASE(m_texEnvIn);S3M_RELEASE(m_srvEnv);S3M_RELEASE(m_texEnv);
	S3M_RELEASE(m_srvMirrorWall);S3M_RELEASE(m_texMirrorWall);S3M_RELEASE(m_srvMirrorFloor);S3M_RELEASE(m_texMirrorFloor);
	for(int i=0;i<S3M_THEME_N;i++){S3M_RELEASE(m_srvFloorD[i]);S3M_RELEASE(m_texFloorD[i]);S3M_RELEASE(m_srvFloor[i]);S3M_RELEASE(m_texFloor[i]);S3M_RELEASE(m_srvBrickD[i]);S3M_RELEASE(m_texBrickD[i]);S3M_RELEASE(m_srvBrick[i]);S3M_RELEASE(m_texBrick[i]);}
	S3M_RELEASE(m_bsAdd);S3M_RELEASE(m_bsAlpha);S3M_RELEASE(m_bsOpaque);S3M_RELEASE(m_dssOff);S3M_RELEASE(m_dssRead);S3M_RELEASE(m_dssWrite);S3M_RELEASE(m_rsShadow);S3M_RELEASE(m_rsNoCull);S3M_RELEASE(m_rsSolid);S3M_RELEASE(m_sampCmp);S3M_RELEASE(m_sampPoint);S3M_RELEASE(m_sampLin);
	S3M_RELEASE(m_vbHud);S3M_RELEASE(m_vbDyn);S3M_RELEASE(m_cbFrame);S3M_RELEASE(m_ilHud);S3M_RELEASE(m_ilSolid);S3M_RELEASE(m_ilPatch);
	delete[] m_cpuDynScratch; m_cpuDynScratch = NULL; m_cpuDynScratchBytes = 0;
	delete[] m_cpuHudScratch; m_cpuHudScratch = NULL; m_cpuHudScratchBytes = 0;
	S3M_RELEASE(m_psFinal);S3M_RELEASE(m_psDof);S3M_RELEASE(m_psSsr);S3M_RELEASE(m_vsPost);S3M_RELEASE(m_psHudLine);S3M_RELEASE(m_psHud);S3M_RELEASE(m_vsHud);S3M_RELEASE(m_psSolid);S3M_RELEASE(m_vsSolid);S3M_RELEASE(m_psWall);S3M_RELEASE(m_dsTess);S3M_RELEASE(m_hsTess);S3M_RELEASE(m_vsTess);
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
	if (dlg && dlg->IsOverviewActive()) {
		int act = -1;
		if (dlg->HitOverviewFloorUi(point, act)) {
			dlg->BeginOverviewUiPress(act, point);
			SetCapture();
			return;
		}
		dlg->BeginOverviewUiPress(-1, point);
		dlg->BeginMapPan(point);
		SetCapture();
		return;
	}
	m_dragging = 1;
	m_dragOrigin = point;
	m_dragTurnAcc = 0;
	SetCapture();
	CCustomStatic::OnLButtonDown(nFlags, point);
}

void CS3mView::OnLButtonUp(UINT nFlags, CPoint point)
{
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (dlg && dlg->HasOverviewUiPress()) {
		dlg->FinishOverviewUiPress(point);
		if (GetCapture() == this)
			ReleaseCapture();
		return;
	}
	if (dlg && dlg->EndMapPan()) {
		if (GetCapture() == this)
			ReleaseCapture();
		return;
	}
	if (m_dragging) {
		const int dx = point.x - m_dragOrigin.x;
		const int dy = point.y - m_dragOrigin.y;
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
	CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
	if (dlg && dlg->IsMapPanning()) {
		dlg->UpdateMapPan(point);
		return;
	}
	// カーソルは OnSetCursor で IDC_C* を適用
	CCustomStatic::OnMouseMove(nFlags, point);
}

BOOL CS3mView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		CSoft3DMazeDlg* dlg = (CSoft3DMazeDlg*)GetParent();
		if (dlg && dlg->IsOverviewActive()) {
			CPoint pt;
			::GetCursorPos(&pt);
			ScreenToClient(&pt);
			int act = -1;
			if (dlg->HitOverviewFloorUi(pt, act) && act >= 0) {
				if (CCC_SetUiCursor(IDC_UI_HAND))
					return TRUE;
			}
			if (CCC_SetUiCursor(IDC_UI_GRAB))
				return TRUE;
		}
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
		const int dir = (zDelta > 0) ? 1 : -1;
		const BOOL shift = (nFlags & MK_SHIFT) != 0;
		// 全体マップ: ホイール=地図ズーム / Shift+ホイール=階層
		if (dlg->IsOverviewActive()) {
			if (shift)
				dlg->InputOverviewFloorDelta(dir);
			else
				dlg->InputMapZoom(dir);
			return TRUE;
		}
		// Shift+ホイール = 従来どおり旋回（Q/E・←→でも可）
		if (shift) {
			dlg->InputTurn(dir > 0 ? -1 : 1);
			return TRUE;
		}
		// ミニマップ上: 近傍の拡大縮小／それ以外: 視点FOVズーム
		CPoint c = pt;
		ScreenToClient(&c);
		if (savedata.s3m_show_map && dlg->HitTestMinimap(c))
			dlg->InputMapZoom(dir);
		else
			dlg->InputFovZoom(dir);
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
	, m_goalX(-1), m_goalZ(-1), m_goalFloor(0), m_lastMapBakeTick(0)
	, m_passCells(0), m_visitPassCells(0)
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
	, m_stairStartX(0.f), m_stairStartZ(0.f), m_stairLandX(0.f), m_stairLandZ(0.f)
	, m_miniFade(1.f), m_miniFadeFrom(0), m_miniFadeTo(0)
	, m_itemsLeft(0), m_keysHeld(0), m_doorMsgT(0.f), m_toastKind(0)
	, m_trapCellX(-1), m_trapCellZ(-1), m_slowT(0.f), m_sfxBumpCool(0.f)
	, m_lastStepMx(0), m_lastStepMz(0), m_iceSlideLeft(0)
	, m_stepFromX(1.5f), m_stepFromZ(1.5f), m_darkT(0.f)
	, m_portalFx(PORTALFX_IDLE), m_portalFxT(0.f), m_portalToX(0.f), m_portalToZ(0.f)
	, m_portalIgnoreX(-1), m_portalIgnoreZ(-1), m_portalFlashA(0.f)
	, m_navCount(0), m_navWaiting(0)
	, m_baseTempoPos(200), m_basePitchPos(200)
	, m_lastTick(0), m_rng(GetTickCount()), m_genSeed(GetTickCount())
	, m_lastAutosave(0), m_runDirty(0), m_mapBakeDirty(1), m_mapToggle(0)
	, m_overviewFloorHeld(0)
	, m_mapPanX(0.f), m_mapPanY(0.f), m_mapPanDrag(0)
	, m_overviewZoomPct(100)
	, m_spaceToggleTick(0), m_tipIsOverview(-1)
	, m_ovBarX(0), m_ovBarY(0), m_ovBarW(0), m_ovBarH(0)
	, m_ovRecX0(0), m_ovRecX1(0), m_ovTabN(0), m_ovPressAction(-1)
	, m_floorBarDirty(1)
{
	for (int f = 0; f < S3M_MAX_FLOORS; f++) {
		m_grids[f] = NULL;
		m_visits[f] = NULL;
		m_ovTabX0[f] = m_ovTabX1[f] = 0;
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
	DDX_Control(pDX, IDC_S3M_DIFF_L, m_diffL);
	DDX_Control(pDX, IDC_S3M_DIFF, m_diff);
	DDX_Control(pDX, IDC_S3M_GEN, m_gen);
	DDX_Control(pDX, IDC_S3M_NAVI, m_navi);
	DDX_Control(pDX, IDC_S3M_HINT, m_hint);
	DDX_Control(pDX, IDC_S3M_VIEW, m_view);
	DDX_Control(pDX, IDC_S3M_STATUS, m_status);
	DDX_Control(pDX, IDC_S3M_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CSoft3DMazeDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_S3M_GEN, &CSoft3DMazeDlg::OnGen)
	ON_BN_CLICKED(IDC_S3M_NAVI, &CSoft3DMazeDlg::OnNavi)
	ON_BN_CLICKED(IDC_S3M_CLOSE, &CSoft3DMazeDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_S3M_HELP, &CSoft3DMazeDlg::OnHelp)
	ON_CBN_SELCHANGE(IDC_S3M_SIZE, &CSoft3DMazeDlg::OnSizeChanged)
	ON_CBN_EDITCHANGE(IDC_S3M_SIZE, &CSoft3DMazeDlg::OnSizeEditChange)
	ON_CBN_SELCHANGE(IDC_S3M_BASE, &CSoft3DMazeDlg::OnBaseChanged)
	ON_CBN_SELCHANGE(IDC_S3M_DIFF, &CSoft3DMazeDlg::OnDiffChanged)
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
	if (HandleAccelMessage(pMsg))
		return TRUE;
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

BOOL CSoft3DMazeDlg::HandleAccelMessage(MSG* pMsg)
{
	if (!pMsg) return FALSE;
	const UINT msg = pMsg->message;
	if (msg != WM_KEYDOWN && msg != WM_KEYUP && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP)
		return FALSE;
	CWnd* f = GetFocus();
	const BOOL inEdit = (f && (f == &m_size || m_size.IsChild(f) || f->IsKindOf(RUNTIME_CLASS(CEdit))));
	if (pMsg->wParam == VK_SPACE) {
		if (inEdit) return FALSE;
		// 押しっぱなしリピートは無視。同一キーイベントの二重処理も防ぐ
		if (msg == WM_KEYDOWN && !(pMsg->lParam & (1 << 30))) {
			const DWORD now = GetTickCount();
			if (now != m_spaceToggleTick) {
				m_spaceToggleTick = now;
				ToggleMapOverlay();
			}
		}
		return TRUE;
	}
	if (msg == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE && IsOverviewActive()) {
		m_mapToggle = 0;
		m_mapPanDrag = 0;
		return TRUE;
	}
	// 全体マップ中の ←→ は階層切替に使うので、地下コンボの選択を動かさない
	if ((pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT) && IsOverviewActive()) {
		if (f && (f == &m_base || m_base.IsChild(f)))
			return TRUE;
	}
	return FALSE;
}

void CSoft3DMazeDlg::ToggleMapOverlay()
{
	m_mapToggle = m_mapToggle ? 0 : 1;
	m_mapPanDrag = 0;
	if (m_mapToggle) {
		m_mapViewFloor = m_floor;
		m_mapBakeDirty = 1;
		m_floorBarDirty = 1;
		FocusOverviewOnPlayer();
	}
	EnsureTipTexture(m_mapToggle != 0);
}

void CSoft3DMazeDlg::FocusOverviewOnPlayer()
{
	m_mapPanX = 0.f;
	m_mapPanY = 0.f;
	m_overviewZoomPct = 100;
	if (!m_view.GetSafeHwnd() || m_n <= 0)
		return;
	CRect rc;
	m_view.GetClientRect(&rc);
	const int vw = rc.Width(), vh = rc.Height();
	if (vw < 8 || vh < 8)
		return;
	const float worldSpan = max(1e-3f, AxisOrigin(m_n));
	// 開いた直後は自機周辺（通路マス換算でおおよそ30前後）を見せる
	float targetWorld = 34.f;
	if (targetWorld > worldSpan)
		targetWorld = worldSpan;
	const float baseSide = OverviewBaseSide(vw, vh);
	const float tipBand = 210.f, topPad = 68.f;
	const float areaH = max(96.f, (float)vh - tipBand - topPad);
	const float viewSpan = min((float)vw, areaH) * 0.92f;
	const float needSide = viewSpan * worldSpan / max(1e-3f, targetWorld);
	int z = (int)(needSide / max(1.f, baseSide) * 100.f + 0.5f);
	const int zMax = OverviewZoomMaxPct();
	if (worldSpan <= targetWorld * 1.05f)
		z = 100;
	else {
		if (z < 120) z = 120;
		if (z > zMax) z = zMax;
	}
	m_overviewZoomPct = z;
	const float side = baseSide * OverviewZoomScale();
	const float u = GridToWorldX(m_px) / worldSpan;
	const float v = GridToWorldZ(m_pz) / worldSpan;
	m_mapPanX = side * (0.5f - u);
	m_mapPanY = side * (0.5f - v);
	ClampMapPan(vw, vh, side);
}

void CSoft3DMazeDlg::EnsureTipTexture(BOOL overviewTip)
{
	if (!m_view.m_ready) return;
	const int want = overviewTip ? 1 : 0;
	if (m_tipIsOverview == want && m_view.m_srvTip)
		return;
	const CStringW& text = overviewTip ? m_overviewTipText : m_playTipText;
	if (text.IsEmpty()) return;
	if (m_view.BakeTipTexture((LPCWSTR)text))
		m_tipIsOverview = want;
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

	const int m = 10;
	const int rowH = 36;
	const int btnH = 32;
	const int comboH = 32;
	const int labH = 22;
	int y = capH + 8;

	// 基準幅（狭いときも下限）。広いほどコンボ／ボタンを横に伸ばす
	const float baseW = 900.f;
	const float sx = max(1.f, (float)(cx - 2 * m) / baseW);
	auto sw = [&](int w)->int { return max(w, (int)((float)w * sx + .5f)); };

	const int sizeLW = sw(56), sizeW = sw(110);
	const int baseLW = sw(40), baseWgt = sw(120);
	const int diffLW = sw(52), diffW = sw(130);
	const int genW = sw(96), naviW = sw(80);
	const int gap = max(6, (int)(8.f * sx + .5f));

	int x = m;
	if (m_sizeL.GetSafeHwnd())
		m_sizeL.SetWindowPos(NULL, x, y + 6, sizeLW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
	x += sizeLW + 2;
	if (m_size.GetSafeHwnd())
		m_size.SetWindowPos(NULL, x, y, sizeW, 280, SWP_NOZORDER | SWP_NOACTIVATE);
	x += sizeW + gap;
	if (m_baseL.GetSafeHwnd())
		m_baseL.SetWindowPos(NULL, x, y + 6, baseLW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
	x += baseLW + 2;
	if (m_base.GetSafeHwnd())
		m_base.SetWindowPos(NULL, x, y, baseWgt, 280, SWP_NOZORDER | SWP_NOACTIVATE);
	x += baseWgt + gap;
	if (m_diffL.GetSafeHwnd())
		m_diffL.SetWindowPos(NULL, x, y + 6, diffLW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
	x += diffLW + 2;
	if (m_diff.GetSafeHwnd())
		m_diff.SetWindowPos(NULL, x, y, diffW, 280, SWP_NOZORDER | SWP_NOACTIVATE);
	x += diffW + gap;
	if (m_gen.GetSafeHwnd())
		m_gen.SetWindowPos(NULL, x, y - 1, genW, comboH + 2, SWP_NOZORDER | SWP_NOACTIVATE);
	x += genW + gap;
	if (m_navi.GetSafeHwnd())
		m_navi.SetWindowPos(NULL, x, y - 1, naviW, comboH + 2, SWP_NOZORDER | SWP_NOACTIVATE);
	x += naviW + gap;
	if (m_hint.GetSafeHwnd())
		m_hint.SetWindowPos(NULL, x, y + 6, max(40, cx - x - m), labH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + 6;

	const int btnY = cy - m - btnH;
	int viewBottom = btnY - 10;
	if (viewBottom < y + 80) viewBottom = y + 80;
	m_view.SetWindowPos(NULL, m, y, max(40, cx - 2 * m), max(40, viewBottom - y), SWP_NOZORDER | SWP_NOACTIVATE);

	const int closeW = sw(100);
	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, cx - m - closeW, btnY, closeW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowPos(NULL, m, btnY + 6, max(40, cx - m - closeW - 14 - m), 20, SWP_NOZORDER | SWP_NOACTIVATE);

	// SetWindowPos で DROPDOWNLIST の選択が 0 に戻ることがある → savedata から復元
	if (m_base.GetSafeHwnd()) {
		int b = savedata.s3m_basements;
		if (b < 0) b = 0;
		if (b > S3M_MAX_FLOORS - 1) b = S3M_MAX_FLOORS - 1;
		m_base.CComboBox::SetCurSel(b);
	}
	if (m_diff.GetSafeHwnd()) {
		int d = savedata.s3m_difficulty;
		if (d < 0) d = DIFF_NORMAL;
		if (d >= DIFF_COUNT) d = DIFF_COUNT - 1;
		m_diff.CComboBox::SetCurSel(d);
	}
	if (m_size.GetSafeHwnd() && savedata.s3m_size >= S3M_MIN)
		SetSizeToUi(savedata.s3m_size);

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	// SetWindowPos 後にアクリル穴が開くので opaque fixer を遅延再適用
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
}

void CSoft3DMazeDlg::FreeGrid()
{
	m_passCells = 0;
	m_visitPassCells = 0;
	m_goalX = -1; m_goalZ = -1; m_goalFloor = 0;
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
	int found = m_size.FindStringExact(-1, s);
	if (found == CB_ERR) {
		// クリア+10 等でプリセット外になった値はリストへ追加（SetCurSel(-1) だけだと編集欄が空になる）
		found = (int)m_size.AddString(s);
	}
	if (found != CB_ERR)
		m_size.CComboBox::SetCurSel(found);
	// SetCurSel 後も編集欄を再同期（DROPDOWN で選択解除時に消える対策）
	m_size.SetWindowText(s);
	m_size.Invalidate(FALSE);
}

int CSoft3DMazeDlg::ReadBasementsFromUi()
{
	int b = -1;
	if (m_base.GetSafeHwnd()) {
		// 物理 index（全項目選択可）。論理 GetCurSel はリサイズ後にずれることがある
		const int sel = m_base.CComboBox::GetCurSel();
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
		m_base.CComboBox::SetCurSel(b);
}

int CSoft3DMazeDlg::ReadDifficultyFromUi()
{
	int d = -1;
	if (m_diff.GetSafeHwnd()) {
		const int sel = m_diff.CComboBox::GetCurSel();
		if (sel != CB_ERR) d = sel;
	}
	if (d < 0) d = savedata.s3m_difficulty;
	if (d < 0) d = DIFF_NORMAL;
	if (d >= DIFF_COUNT) d = DIFF_COUNT - 1;
	return d;
}

void CSoft3DMazeDlg::SetDifficultyToUi(int d)
{
	if (d < 0) d = DIFF_NORMAL;
	if (d >= DIFF_COUNT) d = DIFF_COUNT - 1;
	savedata.s3m_difficulty = d;
	if (m_diff.GetSafeHwnd())
		m_diff.CComboBox::SetCurSel(d);
}

void CSoft3DMazeDlg::PersistUi()
{
	savedata.s3m_size = ReadSizeFromUi();
	savedata.s3m_basements = ReadBasementsFromUi();
	savedata.s3m_difficulty = ReadDifficultyFromUi();
	savedata.s3m_minimap = S3mClampMapSize(savedata.s3m_minimap);
	if (savedata.s3m_show_map != 0) savedata.s3m_show_map = 1;
	savedata.s3m_item_mask = S3mItemMask();
	if (savedata.s3m_item_mask == 0) savedata.s3m_item_mask = -1;
	savedata.s3m_bob = savedata.s3m_bob ? 1 : 0;
	if (savedata.s3m_fov < 0 || savedata.s3m_fov > 2) savedata.s3m_fov = 1;
	if (savedata.s3m_zoom < 50 || savedata.s3m_zoom > 250) savedata.s3m_zoom = 100;
	if (savedata.s3m_map_zoom < 50 || savedata.s3m_map_zoom > 400) savedata.s3m_map_zoom = 100;
	if (savedata.s3m_difficulty < 0 || savedata.s3m_difficulty >= DIFF_COUNT) savedata.s3m_difficulty = DIFF_NORMAL;
	PersistWindowRect();
	MpPersistSavedataQuick();
}

void CSoft3DMazeDlg::PersistWindowRect()
{
	// Create 直後(未表示)の OnSize で既定サイズを .dat に書いて復元値を潰さない
	if (!GetSafeHwnd() || !IsWindowVisible() || IsIconic())
		return;
	WINDOWPLACEMENT wp = {};
	wp.length = sizeof(wp);
	if (!GetWindowPlacement(&wp))
		return;
	const RECT& r = wp.rcNormalPosition;
	const int w = r.right - r.left;
	const int h = r.bottom - r.top;
	if (w < 320 || h < 240)
		return;
	savedata.s3m_win_x = r.left;
	savedata.s3m_win_y = r.top;
	savedata.s3m_win_w = w;
	savedata.s3m_win_h = h;
	MpPersistSavedataQuick();
}

void CSoft3DMazeDlg::ApplySavedWindowRect()
{
	if (!GetSafeHwnd())
		return;
	int w = savedata.s3m_win_w, h = savedata.s3m_win_h;
	int x = savedata.s3m_win_x, y = savedata.s3m_win_y;
	if (w < 320 || h < 240)
		return;
	RECT r = { x, y, x + w, y + h };
	HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (mon && GetMonitorInfo(mon, &mi)) {
		const RECT& wa = mi.rcWork;
		if (w > wa.right - wa.left) w = wa.right - wa.left;
		if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
		if (x < wa.left) x = wa.left;
		if (y < wa.top) y = wa.top;
		if (x + w > wa.right) x = wa.right - w;
		if (y + h > wa.bottom) y = wa.bottom - h;
	}
	SetWindowPos(NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
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
		f.Write(&m_keysHeld, sizeof(m_keysHeld));
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
		m_keysHeld = 0;
		{
			int kh = 0;
			if (f.Read(&kh, sizeof(kh)) == sizeof(kh) && kh >= 0 && kh < 256)
				m_keysHeld = kh;
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
	RefreshGoalCache();
	RecountExploreStats();
	for (int fl = 0; fl < m_nFloors; fl++) {
		for (int z = 0; z < m_n; z++) {
			for (int x = 0; x < m_n; x++) {
				const BYTE c = CellAtF(fl, x, z);
				if (S3mIsPickupCell(c))
					m_itemsLeft++;
			}
		}
	}
	if (IsBlocked(m_px, m_pz)) {
		FreeGrid();
		return FALSE;
	}
	// 旧セーブ（同一マス階段など）は親切に作り直す
	if (!StairsLayoutModern()) {
		FreeGrid();
		savedata.s3m_have_run = 0;
		return FALSE;
	}
	ResetPortalFx();
	ClearNavPath();
	EnsureGoalReachable();
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
	// 動画(mode=-2)はテンポ／ピッチ変更で再生速度が動くので触らない
	if (mode == -2) return;
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
	if (c == CELL_DOOR)
		return (m_keysHeld > 0) ? FALSE : TRUE;
	return (c == CELL_WALL || c == CELL_WINDOW || c == CELL_MIRROR_WALL) ? TRUE : FALSE;
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
BOOL CSoft3DMazeDlg::DoorThinX(int f, int x, int z) const
{
	if (m_n <= 0) return TRUE;
	const float sx = AxisSpan(x), sz = AxisSpan(z);
	if (sx + 1e-4f < sz) return TRUE;
	if (sz + 1e-4f < sx) return FALSE;
	auto wall = [&](int nx, int nz) -> BOOL {
		if (nx < 0 || nz < 0 || nx >= m_n || nz >= m_n) return TRUE;
		if (f < 0 || f >= m_nFloors || !m_grids[f]) return TRUE;
		const BYTE c = CellAtF(f, nx, nz);
		return (c == CELL_WALL || c == CELL_WINDOW || c == CELL_MIRROR_WALL) ? TRUE : FALSE;
	};
	const BOOL wallNS = wall(x, z - 1) && wall(x, z + 1);
	const BOOL wallEW = wall(x - 1, z) && wall(x + 1, z);
	if (wallNS && !wallEW) return TRUE;
	if (wallEW && !wallNS) return FALSE;
	return TRUE;
}
void CSoft3DMazeDlg::DoorWorldRect(int f, int x, int z, float pad, float& x0, float& z0, float& x1, float& z1) const
{
	x0 = AxisOrigin(x); x1 = x0 + AxisSpan(x);
	z0 = AxisOrigin(z); z1 = z0 + AxisSpan(z);
	const float wallT = 0.10f + pad * 2.f;
	if (DoorThinX(f, x, z)) {
		const float cx = (x0 + x1) * .5f;
		x0 = cx - wallT * .5f; x1 = cx + wallT * .5f;
		z0 -= pad; z1 += pad;
	} else {
		const float cz = (z0 + z1) * .5f;
		z0 = cz - wallT * .5f; z1 = cz + wallT * .5f;
		x0 -= pad; x1 += pad;
	}
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

void CSoft3DMazeDlg::RecountExploreStats()
{
	m_passCells = 0;
	m_visitPassCells = 0;
	for (int f = 0; f < m_nFloors; f++) {
		if (!m_grids[f] || !m_visits[f]) continue;
		for (int z = 1; z < m_n; z += 2) for (int x = 1; x < m_n; x += 2) {
			const BYTE c = CellAtF(f, x, z);
			if (c == CELL_WALL || c == CELL_WINDOW || c == CELL_MIRROR_WALL) continue;
			m_passCells++;
			if (VisitAtF(f, x, z)) m_visitPassCells++;
		}
	}
}

void CSoft3DMazeDlg::NoteVisitCell(int ix, int iz)
{
	if (ix < 0 || iz < 0 || ix >= m_n || iz >= m_n || !m_visit) return;
	if (VisitAt(ix, iz)) return;
	Visit(ix, iz) = 1;
	m_runDirty = 1;
	if (IsOverviewActive())
		m_mapBakeDirty = 1;
	// 探索率は奇数通路のみ（UpdateStatus と同一基準）
	if ((ix & 1) && (iz & 1)) {
		const BYTE c = CellAt(ix, iz);
		if (c != CELL_WALL && c != CELL_WINDOW && c != CELL_MIRROR_WALL)
			m_visitPassCells++;
	}
}

void CSoft3DMazeDlg::MarkVisited()
{
	NoteVisitCell((int)floorf(m_px), (int)floorf(m_pz));
}

void CSoft3DMazeDlg::RefreshGoalCache()
{
	m_goalX = -1; m_goalZ = -1; m_goalFloor = 0;
	for (int f = 0; f < m_nFloors; f++) {
		if (!m_grids[f]) continue;
		for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++) {
			if (CellAtF(f, x, z) != CELL_GOAL) continue;
			m_goalX = x; m_goalZ = z; m_goalFloor = f;
			return;
		}
	}
}

int CSoft3DMazeDlg::TargetGoalRoutes() const
{
	const int diff = (savedata.s3m_difficulty < 0 || savedata.s3m_difficulty >= DIFF_COUNT)
		? DIFF_NORMAL : savedata.s3m_difficulty;
	// 普通=200。簡単ほど間隔小＝ルート多／難しいほど間隔大＝ルート少
	static const int kChunk[DIFF_COUNT] = { 100, 150, 200, 300, 450 };
	const int chunk = kChunk[diff];
	int perSize = m_n / chunk;
	if (perSize < 1) perSize = 1;
	int routes = perSize * max(1, m_nFloors);
	if (routes > 99) routes = 99;
	return routes;
}

void CSoft3DMazeDlg::GenerateMaze()
{
	DWORD seed=GetTickCount()^((DWORD)ReadSizeFromUi()*2654435761u);
	if(savedata.s3m_seed)seed=(DWORD)savedata.s3m_seed;
	GenerateMazeWithSeed(seed);
}

// 1フロア分の迷路（DFS＋難易度に応じた幅広げ／広間）を f 面へ生成する
void CSoft3DMazeDlg::GenerateOneFloor(int f)
{
	if (m_n <= 0 || f < 0 || f >= m_nFloors || !m_grids[f])
		return;

	const int diff = (savedata.s3m_difficulty < 0 || savedata.s3m_difficulty >= DIFF_COUNT)
		? DIFF_NORMAL : savedata.s3m_difficulty;
	// 簡単ほど通路を太く・広間多め、難しいほど細い迷路寄り（大マップでも広間はサイズ比例）
	static const float kWidenScale[DIFF_COUNT] = { 2.2f, 1.5f, 1.0f, 0.45f, 0.15f };
	static const int kRoomDiv[DIFF_COUNT] = { 28, 36, 48, 80, 140 };

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

	{
		auto isOpen = [&](int x, int z) -> BOOL {
			if (x < 0 || z < 0 || x >= m_n || z >= m_n) return FALSE;
			const BYTE c = CellAtF(f, x, z);
			return (c != CELL_WALL && c != CELL_WINDOW && c != CELL_MIRROR_WALL) ? TRUE : FALSE;
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

		int widen = (int)((float)max(6, m_n / 2) * kWidenScale[diff] + 0.5f);
		if (widen > 400) widen = 400;
		if (widen < 0) widen = 0;
		const int widenTries = max(widen * 48, 80);
		for (int t = 0; t < widenTries && widen > 0; t++) {
			m_rng = m_rng * 1664525u + 1013904223u;
			const int x = 1 + (int)(m_rng % (DWORD)(m_n - 2));
			m_rng = m_rng * 1664525u + 1013904223u;
			const int z = 1 + (int)(m_rng % (DWORD)(m_n - 2));
			if (!inInner(x, z) || CellAtF(f, x, z) != CELL_WALL) continue;
			// 簡単: 隣が1〜2でも削る／難しい: 行き止まり壁のみ
			const int fn = floorN(x, z);
			if (diff <= DIFF_EASY) { if (fn < 1 || fn > 2) continue; }
			else if (fn != 1) continue;
			CellF(f, x, z) = CELL_FLOOR;
			widen--;
		}

		// 広間数: マップ辺長に比例（旧 kRoomMax=1〜8 だと大マップで増えない）
		int roomsLeft = m_n / kRoomDiv[diff];
		if (diff >= DIFF_VERY_HARD) roomsLeft = max(0, roomsLeft / 2);
		else if (roomsLeft < 1) roomsLeft = 1;
		if (roomsLeft > 160) roomsLeft = 160;
		for (int attempt = 0; attempt < roomsLeft * 80 && roomsLeft > 0; attempt++) {
			m_rng = m_rng * 1664525u + 1013904223u;
			const int cx = 1 + 2 * (int)(m_rng % (DWORD)max(1, (m_n - 1) / 2));
			m_rng = m_rng * 1664525u + 1013904223u;
			const int cz = 1 + 2 * (int)(m_rng % (DWORD)max(1, (m_n - 1) / 2));
			if (!inInner(cx, cz) || !isOpen(cx, cz)) continue;
			int half = 1;
			if (diff <= DIFF_EASY) {
				if (m_n >= 600 && (m_rng & 3u) == 0) half = 3;
				else if ((m_rng & 3u) == 0) half = 2;
			} else if (diff == DIFF_NORMAL && m_n >= 800 && (m_rng & 7u) == 0) {
				half = 2;
			}
			const int x0 = max(1, cx - half), x1 = min(m_n - 2, cx + half);
			const int z0 = max(1, cz - half), z1 = min(m_n - 2, cz + half);
			int carved = 0;
			for (int z = z0; z <= z1; z++) {
				for (int x = x0; x <= x1; x++) {
					if (CellAtF(f, x, z) == CELL_WALL) {
						CellF(f, x, z) = CELL_FLOOR;
						carved++;
					}
				}
			}
			if (carved >= 4)
				roomsLeft--;
		}

		if (diff <= DIFF_NORMAL) {
			for (int z = 1; z < m_n - 1; z++) {
				for (int x = 1; x < m_n - 1; x++) {
					if (CellAtF(f, x, z) != CELL_WALL) continue;
					if (floorN(x, z) < 4) continue;
					CellF(f, x, z) = CELL_FLOOR;
				}
			}
		}

		// 交差柱だけ残った孤立セルを除去（広間／幅広げの消し忘れ）
		for (int z = 2; z < m_n - 2; z += 2) {
			for (int x = 2; x < m_n - 2; x += 2) {
				if (CellAtF(f, x, z) != CELL_WALL) continue;
				auto solid = [&](int sx, int sz) -> BOOL {
					if (sx < 0 || sz < 0 || sx >= m_n || sz >= m_n) return TRUE;
					const BYTE c = CellAtF(f, sx, sz);
					return (c == CELL_WALL || c == CELL_WINDOW || c == CELL_MIRROR_WALL) ? TRUE : FALSE;
				};
				const int segs = (solid(x - 1, z) ? 1 : 0) + (solid(x + 1, z) ? 1 : 0)
					+ (solid(x, z - 1) ? 1 : 0) + (solid(x, z + 1) ? 1 : 0);
				if (segs == 0)
					CellF(f, x, z) = CELL_FLOOR;
			}
		}
	}
}

BOOL CSoft3DMazeDlg::MazeWalkable(int f, int x, int z) const
{
	if (f < 0 || f >= m_nFloors || x < 0 || z < 0 || x >= m_n || z >= m_n || !m_grids[f])
		return FALSE;
	const BYTE c = CellAtF(f, x, z);
	if (c == CELL_DOOR)
		return (m_keysHeld > 0) ? TRUE : FALSE;
	return (c != CELL_WALL && c != CELL_WINDOW && c != CELL_MIRROR_WALL) ? TRUE : FALSE;
}

// 下り／上り階段の相手マス（通路は奇数列なので ±2＝壁1マスを挟んだ斜め接続）
BOOL CSoft3DMazeDlg::FindStairPartner(int f, int x, int z, int& outF, int& outX, int& outZ) const
{
	outF = f; outX = x; outZ = z;
	if (f < 0 || f >= m_nFloors || x < 0 || z < 0 || x >= m_n || z >= m_n || !m_grids[f])
		return FALSE;
	const BYTE c = CellAtF(f, x, z);
	// 先に2マスずれ、最後に同一マス（旧セーブ互換）
	static const int odx[5] = { 2, -2, 0, 0, 0 };
	static const int odz[5] = { 0, 0, 2, -2, 0 };
	if (c == CELL_STAIRS_DOWN && f + 1 < m_nFloors) {
		outF = f + 1;
		for (int i = 0; i < 5; i++) {
			const int nx = x + odx[i], nz = z + odz[i];
			if (nx < 0 || nz < 0 || nx >= m_n || nz >= m_n) continue;
			if (CellAtF(outF, nx, nz) == CELL_STAIRS_UP) {
				outX = nx; outZ = nz;
				return TRUE;
			}
		}
	} else if (c == CELL_STAIRS_UP && f > 0) {
		outF = f - 1;
		for (int i = 0; i < 5; i++) {
			const int nx = x + odx[i], nz = z + odz[i];
			if (nx < 0 || nz < 0 || nx >= m_n || nz >= m_n) continue;
			if (CellAtF(outF, nx, nz) == CELL_STAIRS_DOWN) {
				outX = nx; outZ = nz;
				return TRUE;
			}
		}
	}
	return FALSE;
}

// 斜め2マス階段のみ許可（同一マス上下は旧データ → 再生成対象）
BOOL CSoft3DMazeDlg::StairsLayoutModern() const
{
	if (m_n <= 0 || m_nFloors <= 0)
		return FALSE;
	int links = 0;
	for (int f = 0; f < m_nFloors; f++) {
		if (!m_grids[f]) return FALSE;
		for (int z = 0; z < m_n; z++) {
			for (int x = 0; x < m_n; x++) {
				const BYTE c = CellAtF(f, x, z);
				if (c != CELL_STAIRS_DOWN && c != CELL_STAIRS_UP)
					continue;
				// 同一XYに相手があると旧レイアウト
				if (c == CELL_STAIRS_DOWN && f + 1 < m_nFloors
					&& CellAtF(f + 1, x, z) == CELL_STAIRS_UP)
					return FALSE;
				if (c == CELL_STAIRS_UP && f > 0
					&& CellAtF(f - 1, x, z) == CELL_STAIRS_DOWN)
					return FALSE;
				int pf = 0, px = 0, pz = 0;
				if (!FindStairPartner(f, x, z, pf, px, pz))
					return FALSE;
				const int man = abs(px - x) + abs(pz - z);
				if (man != 2)
					return FALSE;
				if (c == CELL_STAIRS_DOWN)
					links++;
			}
		}
	}
	if (m_nFloors > 1 && links < 1)
		return FALSE;
	return TRUE;
}

// スタートから3D BFSし、難易度に応じた距離のマスをゴール候補にする
int CSoft3DMazeDlg::PickGoalByDifficulty(int sx, int sz, int& outX, int& outZ, int& outF)
{
	outX = -1; outZ = -1; outF = 0;
	if (m_n <= 0 || m_nFloors <= 0 || !MazeWalkable(0, sx, sz))
		return 0;

	const int diff = (savedata.s3m_difficulty < 0 || savedata.s3m_difficulty >= DIFF_COUNT)
		? DIFF_NORMAL : savedata.s3m_difficulty;
	const size_t stride = (size_t)m_n * (size_t)m_n;
	const size_t cap = stride * (size_t)m_nFloors;
	BYTE* seen = new BYTE[cap];
	if (!seen) return 0;
	memset(seen, 0, cap);

	struct Qn { int x, z, f, d, tr; };
	// 大マップでも全セル探索できるようキューを切らない（1500² では 50万上限だと不通判定になる）
	const size_t qMax = cap;
	Qn* q = new (std::nothrow) Qn[qMax];
	if (!q) { delete[] seen; return 0; }

	auto id = [&](int f, int x, int z) -> size_t {
		return (size_t)f * stride + (size_t)z * (size_t)m_n + (size_t)x;
	};

	size_t qh = 0, qt = 0;
	q[qt++] = { sx, sz, 0, 0, 0 };
	seen[id(0, sx, sz)] = 1;

	int farX = sx, farZ = sz, farF = 0, farD = 0, farTr = 0;
	static const int kMinMul[DIFF_COUNT] = { 1, 2, 3, 5, 8 };
	const int wantMin = max(4, (m_n * kMinMul[diff]) / 4 + m_nFloors * (diff + 1) * 2);

	const int kCandMax = 256;
	Qn cand[256];
	int nCand = 0;

	const int dx4[4] = { 1, -1, 0, 0 };
	const int dz4[4] = { 0, 0, 1, -1 };

	while (qh < qt) {
		const Qn cur = q[qh++];
		const int score = cur.d + cur.tr * (2 + diff);
		const int farScore = farD + farTr * (2 + diff);
		if (score > farScore || (score == farScore && cur.tr > farTr)) {
			farD = cur.d; farTr = cur.tr; farX = cur.x; farZ = cur.z; farF = cur.f;
		}
		if (cur.d >= wantMin && !(cur.f == 0 && cur.x == sx && cur.z == sz)) {
			// ゴールは奇数×奇数の通路のみ（壁帯・交差点に置かない）
			if (((cur.x & 1) != 0) && ((cur.z & 1) != 0)
				&& cur.x > 0 && cur.z > 0 && cur.x < m_n - 1 && cur.z < m_n - 1) {
				const BYTE c = CellAtF(cur.f, cur.x, cur.z);
				if (c == CELL_FLOOR) {
					if (nCand < kCandMax)
						cand[nCand++] = cur;
					else {
						m_rng = m_rng * 1664525u + 1013904223u;
						cand[m_rng % (DWORD)kCandMax] = cur;
					}
				}
			}
		}

		auto push = [&](int nf, int nx, int nz, int nd, int ntr) {
			if (!MazeWalkable(nf, nx, nz)) return;
			const size_t i = id(nf, nx, nz);
			if (seen[i] || qt >= qMax) return;
			seen[i] = 1;
			q[qt++] = { nx, nz, nf, nd, ntr };
		};

		for (int i = 0; i < 4; i++)
			push(cur.f, cur.x + dx4[i], cur.z + dz4[i], cur.d + 1, cur.tr);

		const BYTE c = CellAtF(cur.f, cur.x, cur.z);
		if (c == CELL_STAIRS_DOWN || c == CELL_STAIRS_UP) {
			int pf = 0, px = 0, pz = 0;
			if (FindStairPartner(cur.f, cur.x, cur.z, pf, px, pz))
				push(pf, px, pz, cur.d + 1, cur.tr + 1);
		}
	}

	delete[] q;
	delete[] seen;

	if (nCand > 0) {
		if (diff >= DIFF_HARD) {
			// 階の上がり下がりが多い／遠い候補を優先
			int best = 0;
			auto sc = [&](const Qn& a) { return a.d + a.tr * (4 + diff); };
			for (int i = 1; i < nCand; i++)
				if (sc(cand[i]) > sc(cand[best])) best = i;
			outX = cand[best].x; outZ = cand[best].z; outF = cand[best].f;
			return cand[best].d;
		}
		if (diff <= DIFF_EASY) {
			int best = 0;
			for (int i = 1; i < nCand; i++) {
				if (cand[i].tr < cand[best].tr) best = i;
				else if (cand[i].tr == cand[best].tr && cand[i].d < cand[best].d) best = i;
			}
			outX = cand[best].x; outZ = cand[best].z; outF = cand[best].f;
			return cand[best].d;
		}
		m_rng = m_rng * 1664525u + 1013904223u;
		const int pick = (int)(m_rng % (DWORD)nCand);
		outX = cand[pick].x; outZ = cand[pick].z; outF = cand[pick].f;
		return cand[pick].d;
	}

	if (farD > 0 && !(farF == 0 && farX == sx && farZ == sz)
		&& ((farX & 1) != 0) && ((farZ & 1) != 0)
		&& CellAtF(farF, farX, farZ) == CELL_FLOOR) {
		outX = farX; outZ = farZ; outF = farF;
		return farD;
	}
	return 0;
}

// スタート（地上）／ゴール（難易度に応じた階層）／階層をつなぐ階段
void CSoft3DMazeDlg::PlaceStairsAndGoal()
{
	if (m_n <= 0 || m_nFloors <= 0 || !m_grids[0])
		return;

	const int diff = (savedata.s3m_difficulty < 0 || savedata.s3m_difficulty >= DIFF_COUNT)
		? DIFF_NORMAL : savedata.s3m_difficulty;
	static const int kStairsPer[DIFF_COUNT] = { 1, 2, 3, 5, 8 };

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

	int sx = 1, sz = ((m_n - 2) & 1) ? (m_n - 2) : (m_n - 3);
	if (!isPassCell(sx, sz) || CellAtF(0, sx, sz) != CELL_FLOOR) {
		if (!findPassNear(0, 1, m_n - 2, -1, -1, sx, sz)) return;
	}
	CellF(0, sx, sz) = CELL_START;

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

	const int basements = m_nFloors - 1;
	if (basements > 0) {
		static const int kChunk[DIFF_COUNT] = { 100, 150, 200, 300, 450 };
		int per = kStairsPer[diff] + max(0, m_n / kChunk[diff] - 1);
		if (per < 1) per = 1;
		if (per > 16) per = 16;
		// 簡単: 階段を離して迷いを減らす／難しい: 密に置いて上がり下がりが増える
		const int minSep = (diff <= DIFF_EASY) ? max(6, m_n / 6)
			: (diff == DIFF_NORMAL ? max(2, m_n / 14) : 0);
		const int halfSlots = max(1, (m_n - 1) / 2);
		int prevPx[32], prevPz[32], nPrev = 0;

		for (int f = 0; f + 1 < m_nFloors; f++) {
			int placed = 0;
			int px[32], pz[32];
			static const int sdx[4] = { 2, -2, 0, 0 };
			static const int sdz[4] = { 0, 0, 2, -2 };
			for (int tries = 0; tries < 12000 && placed < per; tries++) {
				m_rng = m_rng * 1664525u + 1013904223u;
				const int x = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
				m_rng = m_rng * 1664525u + 1013904223u;
				const int z = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
				m_rng = m_rng * 1664525u + 1013904223u;
				const int dir = (int)(m_rng % 4u);
				if (!isPassCell(x, z)) continue;
				const int lx = x + sdx[dir], lz = z + sdz[dir];
				if (!isPassCell(lx, lz)) continue;
				if (CellAtF(f, x, z) != CELL_FLOOR) continue;
				if (CellAtF(f + 1, lx, lz) != CELL_FLOOR) continue;
				if ((x == sx && z == sz && f == 0)) continue;
				BOOL farOk = TRUE;
				for (int i = 0; i < placed && minSep > 0; i++) {
					if (abs(x - px[i]) + abs(z - pz[i]) < minSep) { farOk = FALSE; break; }
					if (abs(lx - px[i]) + abs(lz - pz[i]) < minSep) { farOk = FALSE; break; }
				}
				if (farOk && diff >= DIFF_HARD && nPrev > 0) {
					const int stagger = max(4, m_n / 10);
					for (int i = 0; i < nPrev; i++) {
						if (abs(x - prevPx[i]) + abs(z - prevPz[i]) < stagger) { farOk = FALSE; break; }
					}
				}
				if (!farOk) continue;
				CellF(f, x, z) = CELL_STAIRS_DOWN;
				CellF(f + 1, lx, lz) = CELL_STAIRS_UP;
				if (placed < 32) { px[placed] = x; pz[placed] = z; }
				placed++;
			}
			for (int z = 1; z < m_n - 1 && placed < 1; z += 2) {
				for (int x = 1; x < m_n - 1 && placed < 1; x += 2) {
					if (CellAtF(f, x, z) != CELL_FLOOR) continue;
					for (int dir = 0; dir < 4 && placed < 1; dir++) {
						const int lx = x + sdx[dir], lz = z + sdz[dir];
						if (!isPassCell(lx, lz)) continue;
						if (CellAtF(f + 1, lx, lz) != CELL_FLOOR) continue;
						CellF(f, x, z) = CELL_STAIRS_DOWN;
						CellF(f + 1, lx, lz) = CELL_STAIRS_UP;
						if (placed < 32) { px[placed] = x; pz[placed] = z; }
						placed++;
					}
				}
			}
			nPrev = 0;
			for (int i = 0; i < placed && nPrev < 32; i++) {
				prevPx[nPrev] = px[i]; prevPz[nPrev] = pz[i]; nPrev++;
			}
		}
	}

	int gx = -1, gz = -1, gf = 0;
	if (PickGoalByDifficulty(sx, sz, gx, gz, gf) <= 0 || gx < 0) {
		// フォールバック: 遠い通路
		gf = (diff >= DIFF_HARD && m_nFloors > 1) ? (m_nFloors - 1) : 0;
		gx = ((m_n - 2) & 1) ? (m_n - 2) : (m_n - 3);
		gz = 1;
		const int avoidX = (gf == 0) ? sx : -1;
		const int avoidZ = (gf == 0) ? sz : -1;
		if (!isPassCell(gx, gz) || CellAtF(gf, gx, gz) == CELL_WALL
			|| CellAtF(gf, gx, gz) == CELL_STAIRS_DOWN || CellAtF(gf, gx, gz) == CELL_STAIRS_UP
			|| (gx == avoidX && gz == avoidZ)) {
			if (!findPassNear(gf, m_n - 2, 1, avoidX, avoidZ, gx, gz)) return;
		}
	}
	{
		const BYTE c = CellAtF(gf, gx, gz);
		if (!isPassCell(gx, gz) || c != CELL_FLOOR) {
			if (!findPassNear(gf, (gx >= 0 ? gx : m_n - 2), (gz >= 0 ? gz : 1), sx, sz, gx, gz)) {
				BOOL ok = FALSE;
				for (int f = m_nFloors - 1; f >= 0 && !ok; f--) {
					int tx = -1, tz = -1;
					if (findPassNear(f, m_n - 2, 1, (f == 0) ? sx : -1, (f == 0) ? sz : -1, tx, tz)) {
						gf = f; gx = tx; gz = tz; ok = TRUE;
					}
				}
				if (!ok) return;
			}
		}
	}
	if (!isPassCell(gx, gz) || CellAtF(gf, gx, gz) != CELL_FLOOR)
		return;
	CellF(gf, gx, gz) = CELL_GOAL;
	m_goalX = gx; m_goalZ = gz; m_goalFloor = gf;
}

// DFS 迷路は通路が1本。壁を抜いてループを足し、ゴールへの別ルートを増やす
void CSoft3DMazeDlg::AddAlternateRoutes()
{
	if (m_n <= 0 || m_nFloors <= 0 || !m_grids[0])
		return;
	const int want = TargetGoalRoutes();
	int need = want - 1; // 1本は生成時の一意経路
	if (need <= 0) return;

	auto isOpen = [&](int f, int x, int z) -> BOOL {
		if (x < 0 || z < 0 || x >= m_n || z >= m_n || !m_grids[f]) return FALSE;
		const BYTE c = CellAtF(f, x, z);
		return (c != CELL_WALL && c != CELL_WINDOW && c != CELL_MIRROR_WALL) ? TRUE : FALSE;
	};

	const int triesMax = need * 200 + 2000;
	for (int tries = 0; tries < triesMax && need > 0; tries++) {
		m_rng = m_rng * 1664525u + 1013904223u;
		const int f = (int)(m_rng % (DWORD)max(1, m_nFloors));
		m_rng = m_rng * 1664525u + 1013904223u;
		const int x = 1 + (int)(m_rng % (DWORD)(m_n - 2));
		m_rng = m_rng * 1664525u + 1013904223u;
		const int z = 1 + (int)(m_rng % (DWORD)(m_n - 2));
		if (!m_grids[f] || CellAtF(f, x, z) != CELL_WALL) continue;
		// 東西または南北の通路を結ぶ壁だけ削る（交差柱や外枠は触らない）
		const BOOL ew = isOpen(f, x - 1, z) && isOpen(f, x + 1, z)
			&& !isOpen(f, x, z - 1) && !isOpen(f, x, z + 1);
		const BOOL ns = isOpen(f, x, z - 1) && isOpen(f, x, z + 1)
			&& !isOpen(f, x - 1, z) && !isOpen(f, x + 1, z);
		if (!ew && !ns) continue;
		CellF(f, x, z) = CELL_FLOOR;
		need--;
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
	savedata.s3m_difficulty = ReadDifficultyFromUi();
	PersistUi();
	if (!AllocGrid(n, nFloors))
		return;
	ResetFloorFx();

	m_won = 0;
	m_itemsLeft = 0;
	m_genSeed=seed;
	m_rng=seed;

	const int diff = savedata.s3m_difficulty;
	// アイテムは効果用。過密にならないよう難易度差は小さめ
	static const float kItemScale[DIFF_COUNT] = { 0.85f, 0.95f, 1.0f, 1.05f, 1.1f };
	static const float kWinScale[DIFF_COUNT] = { 0.5f, 0.75f, 1.0f, 1.2f, 1.4f };
	const float itemSc = (diff >= 0 && diff < DIFF_COUNT) ? kItemScale[diff] : 1.f;
	const float winSc = (diff >= 0 && diff < DIFF_COUNT) ? kWinScale[diff] : 1.f;

	for (int f = 0; f < m_nFloors; f++)
		GenerateOneFloor(f);
	PlaceStairsAndGoal();
	AddAlternateRoutes();
	BindFloor(0);
	m_mapViewFloor = 0;

	const int mask = S3mItemMask();
	if (mask & ITEM_WINDOW) {
		auto isSolidWallF = [&](int f, int x, int z) -> BOOL {
			if (x < 0 || z < 0 || x >= m_n || z >= m_n) return TRUE;
			const BYTE c = CellAtF(f, x, z);
			return (c == CELL_WALL || c == CELL_WINDOW || c == CELL_MIRROR_WALL) ? TRUE : FALSE;
		};
		for (int f = 0; f < m_nFloors; f++) {
			int winBudget = (int)((float)(m_n / 3) * winSc + 0.5f);
			if (f > 0) winBudget = max(0, winBudget / 2);
			if (winBudget < 0) winBudget = 0;
			for (int z = 1; z < m_n - 1 && winBudget > 0; z++) {
				for (int x = 1; x < m_n - 1 && winBudget > 0; x++) {
					if (CellAtF(f, x, z) != CELL_WALL) continue;
					if (((x & 1) == 0) && ((z & 1) == 0)) continue;
					const BOOL wallNS = isSolidWallF(f, x, z - 1) || isSolidWallF(f, x, z + 1);
					const BOOL wallEW = isSolidWallF(f, x - 1, z) || isSolidWallF(f, x + 1, z);
					if (wallNS && wallEW) continue;
					int openN = 0;
					if (!isSolidWallF(f, x - 1, z)) openN++;
					if (!isSolidWallF(f, x + 1, z)) openN++;
					if (!isSolidWallF(f, x, z - 1)) openN++;
					if (!isSolidWallF(f, x, z + 1)) openN++;
					if (openN < 1 || openN > 2) continue;
					m_rng = m_rng * 1664525u + 1013904223u;
					if ((m_rng % 17u) != 0) continue;
					CellF(f, x, z) = CELL_WINDOW;
					winBudget--;
				}
			}
		}
	}

	BYTE kinds[16];
	int nk = 0;
	if (mask & ITEM_TEMPO) kinds[nk++] = CELL_TEMPO;
	if (mask & ITEM_PITCH_UP) kinds[nk++] = CELL_PITCH_UP;
	if (mask & ITEM_PITCH_DN) kinds[nk++] = CELL_PITCH_DN;
	if (mask & ITEM_NEXT) kinds[nk++] = CELL_NEXT;
	if (mask & ITEM_EQ) kinds[nk++] = CELL_EQ;
	if (mask & ITEM_TEMPO_DN) kinds[nk++] = CELL_TEMPO_DN;
	if (mask & ITEM_PREV) kinds[nk++] = CELL_PREV;
	if (mask & ITEM_VOL_UP) kinds[nk++] = CELL_VOL_UP;
	if (mask & ITEM_VOL_DN) kinds[nk++] = CELL_VOL_DN;
	if (mask & ITEM_REVERB) kinds[nk++] = CELL_REVERB;
	if (mask & ITEM_XFADE) kinds[nk++] = CELL_XFADE;
	if (mask & ITEM_EQ_FLAT) kinds[nk++] = CELL_EQ_FLAT;
	if (mask & ITEM_RANDOM) kinds[nk++] = CELL_RANDOM;
	// 通路マスの約2〜4%（全階層合計）。小マップで全マス埋めにならないよう上限も通路比率で
	const int halfSlots = max(1, (m_n - 1) / 2);
	const int passApprox = max(1, halfSlots * halfSlots);
	const int passAll = passApprox * max(1, m_nFloors);
	int itemBudget = 0;
	if (nk > 0) {
		itemBudget = (int)((float)passAll * 0.028f * itemSc + 0.5f);
		if (itemBudget < min(3, nk)) itemBudget = min(3, nk);
		const int capSoft = max(3, (int)((float)passAll * 0.04f + 0.5f));
		if (itemBudget > capSoft) itemBudget = capSoft;
		if (itemBudget > 64) itemBudget = 64;
	}
	// スタート側の外周帯（x/z≤2）は壁に埋まるので禁止。反対側の端-2は隅配置用に許可
	auto isAwayFromOuter = [&](int x, int z) -> BOOL {
		return (x >= 3 && z >= 3 && x <= m_n - 2 && z <= m_n - 2) ? TRUE : FALSE;
	};
	auto isPassOdd = [&](int x, int z) -> BOOL {
		return ((x & 1) != 0 && (z & 1) != 0 && x > 0 && z > 0 && x < m_n - 1 && z < m_n - 1) ? TRUE : FALSE;
	};
	auto canPlaceSpecial = [&](int x, int z) -> BOOL {
		return (isPassOdd(x, z) && isAwayFromOuter(x, z)) ? TRUE : FALSE;
	};
	for (int tries = 0; tries < 8000 && itemBudget > 0 && nk > 0; tries++) {
		m_rng = m_rng * 1664525u + 1013904223u;
		const int f = (int)(m_rng % (DWORD)max(1, m_nFloors));
		m_rng = m_rng * 1664525u + 1013904223u;
		const int x = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
		m_rng = m_rng * 1664525u + 1013904223u;
		const int z = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
		if (!canPlaceSpecial(x, z)) continue;
		if (CellAtF(f, x, z) != CELL_FLOOR) continue;
		CellF(f, x, z) = kinds[m_rng % (DWORD)nk];
		itemBudget--;
		m_itemsLeft++;
	}

	// トラップ（半透明・消えず。ワープなし）。難しいほど多め／地下寄り
	{
		static const float kTrapScale[DIFF_COUNT] = { 0.35f, 0.55f, 0.85f, 1.2f, 1.55f };
		const float tsc = (diff >= 0 && diff < DIFF_COUNT) ? kTrapScale[diff] : 1.f;
		int trapBudget = (int)((float)passAll * 0.015f * tsc + 0.5f);
		if (trapBudget < 1) trapBudget = (diff >= DIFF_NORMAL) ? 1 : 0;
		if (trapBudget > 36) trapBudget = 36;
		BYTE traps[4] = { CELL_SLIME, CELL_SPIKE, CELL_ICE, CELL_DARK };
		for (int tries = 0; tries < 6000 && trapBudget > 0; tries++) {
			m_rng = m_rng * 1664525u + 1013904223u;
			int f = (int)(m_rng % (DWORD)max(1, m_nFloors));
			// 地下をやや優先
			if (m_nFloors > 1 && (m_rng % 5u) != 0)
				f = 1 + (int)(m_rng % (DWORD)(m_nFloors - 1));
			m_rng = m_rng * 1664525u + 1013904223u;
			const int x = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
			m_rng = m_rng * 1664525u + 1013904223u;
			const int z = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
			if (!canPlaceSpecial(x, z)) continue;
			if (CellAtF(f, x, z) != CELL_FLOOR) continue;
			CellF(f, x, z) = traps[m_rng % 4u];
			trapBudget--;
		}
	}
	// 通路以外に乗ったアイテム／トラップを除去（壁帯・交差点に残さない）
	for (int f = 0; f < m_nFloors; f++) {
		if (!m_grids[f]) continue;
		for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++) {
			const BYTE c = CellAtF(f, x, z);
			if (!S3mIsPickupCell(c) && !S3mIsTrapCell(c)) continue;
			if (canPlaceSpecial(x, z)) continue;
			CellF(f, x, z) = isPassOdd(x, z) ? CELL_FLOOR : CELL_WALL;
			if (S3mIsPickupCell(c) && m_itemsLeft > 0) m_itemsLeft--;
		}
	}

	// 鏡壁／鏡床を生成データとして配置（見た目ハッシュではなくセル値）
	{
		auto isSolid = [&](int f, int x, int z) -> BOOL {
			if (x < 0 || z < 0 || x >= m_n || z >= m_n) return TRUE;
			return S3mIsSolidWall(CellAtF(f, x, z)) ? TRUE : FALSE;
		};
		for (int f = 0; f < m_nFloors; f++) {
			if (!m_grids[f]) continue;
			int wallBudget = max(2, m_n / 5);
			int floorBudget = max(2, m_n / 6);
			if (f > 0) { wallBudget = max(1, wallBudget / 2); floorBudget = max(1, floorBudget / 2); }
			for (int z = 1; z < m_n - 1 && wallBudget > 0; z++) {
				for (int x = 1; x < m_n - 1 && wallBudget > 0; x++) {
					if (CellAtF(f, x, z) != CELL_WALL) continue;
					if (((x & 1) == 0) && ((z & 1) == 0)) continue; // 交差柱は除外
					int openN = 0;
					if (!isSolid(f, x - 1, z)) openN++;
					if (!isSolid(f, x + 1, z)) openN++;
					if (!isSolid(f, x, z - 1)) openN++;
					if (!isSolid(f, x, z + 1)) openN++;
					if (openN < 1) continue;
					m_rng = m_rng * 1664525u + 1013904223u;
					if ((m_rng % 11u) != 0) continue;
					CellF(f, x, z) = CELL_MIRROR_WALL;
					wallBudget--;
				}
			}
			for (int tries = 0; tries < 6000 && floorBudget > 0; tries++) {
				m_rng = m_rng * 1664525u + 1013904223u;
				const int x = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
				m_rng = m_rng * 1664525u + 1013904223u;
				const int z = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
				if (!canPlaceSpecial(x, z)) continue;
				if (CellAtF(f, x, z) != CELL_FLOOR) continue;
				CellF(f, x, z) = CELL_MIRROR_FLOOR;
				floorBudget--;
			}
		}
	}

	// 同階ワープポータル（サイズ／難易度で組数増加＝別ルート）
	{
		static const int kChunk[DIFF_COUNT] = { 100, 150, 200, 300, 450 };
		const int chunk = (diff >= 0 && diff < DIFF_COUNT) ? kChunk[diff] : 200;
		int pairsPerFloor = max(1, m_n / chunk);
		if (pairsPerFloor > 8) pairsPerFloor = 8;
		for (int f = 0; f < m_nFloors; f++) {
			if (!m_grids[f]) continue;
			int placed = 0;
			for (int p = 0; p < pairsPerFloor; p++) {
				int ax = -1, az = -1, bx = -1, bz = -1;
				int best = -1;
				for (int tries = 0; tries < 4000; tries++) {
					m_rng = m_rng * 1664525u + 1013904223u;
					const int x1 = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
					m_rng = m_rng * 1664525u + 1013904223u;
					const int z1 = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
					m_rng = m_rng * 1664525u + 1013904223u;
					const int x2 = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
					m_rng = m_rng * 1664525u + 1013904223u;
					const int z2 = 1 + 2 * (int)(m_rng % (DWORD)halfSlots);
					if (!canPlaceSpecial(x1, z1) || !canPlaceSpecial(x2, z2)) continue;
					if (CellAtF(f, x1, z1) != CELL_FLOOR || CellAtF(f, x2, z2) != CELL_FLOOR) continue;
					const int d = abs(x1 - x2) + abs(z1 - z2);
					if (d < m_n / 4) continue;
					if (d > best) {
						best = d;
						ax = x1; az = z1; bx = x2; bz = z2;
					}
				}
				if (best > 0 && ax >= 0 && bx >= 0) {
					CellF(f, ax, az) = CELL_PORTAL;
					CellF(f, bx, bz) = CELL_PORTAL;
					placed++;
				}
			}
			(void)placed;
		}
	}
	m_trapCellX = -1; m_trapCellZ = -1; m_slowT = 0.f; m_iceSlideLeft = 0; m_darkT = 0.f;
	ResetPortalFx();
	ClearNavPath();
	m_keysHeld = 0;
	EnsureGoalReachable();
	// 鍵／扉：ゴール確定後に配置（鍵なしでは扉＝壁。鍵はスタート側の同階寄り通路へ）
	{
		// ベース：サイズ／階層で増やす（以前より多め）。超簡単はたまに1、他は難易度で上乗せ
		int nDoors = max(0, m_n / 140 + m_nFloors);
		if (diff == DIFF_VERY_EASY) {
			m_rng = m_rng * 1664525u + 1013904223u;
			const unsigned thr = (m_n >= 60) ? 50u : 35u; // 大マップほど出やすい／約35〜50%
			nDoors = ((m_rng % 100u) < thr) ? 1 : 0;
		} else if (diff == DIFF_EASY) {
			nDoors = max(1, (nDoors * 3 + 2) / 4);
		} else if (diff == DIFF_NORMAL) {
			nDoors = max(1, nDoors + 1);
		} else if (diff == DIFF_HARD) {
			nDoors = nDoors + 1 + max(0, m_nFloors - 1);
		} else { // VERY_HARD
			nDoors = nDoors + 2 + max(0, m_nFloors);
		}
		if (nDoors > 40) nDoors = 40;
		int sx = 1, sz = 1;
		for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++)
			if (CellAtF(0, x, z) == CELL_START) { sx = x; sz = z; }
		if (nDoors > 0 && MazeWalkable(0, sx, sz)) {
			const size_t stride = (size_t)m_n * (size_t)m_n;
			const size_t cap = stride * (size_t)m_nFloors;
			BYTE* seenBits = new (std::nothrow) BYTE[(cap + 7) / 8];
			UINT* q = new (std::nothrow) UINT[min(cap, (size_t)1 << 20)];
			UINT* dist = new (std::nothrow) UINT[cap];
			const size_t qCap = q ? min(cap, (size_t)1 << 20) : 0;
			static const int dx4[4] = { 1, -1, 0, 0 }, dz4[4] = { 0, 0, 1, -1 };
			auto idOf = [&](int f, int x, int z) -> size_t {
				return (size_t)f * stride + (size_t)z * (size_t)m_n + (size_t)x;
			};
			if (seenBits && q && dist && qCap > 0) {
				for (int di = 0; di < nDoors; di++) {
					BOOL placed = FALSE;
					for (int attempt = 0; attempt < 120 && !placed; attempt++) {
						m_rng = m_rng * 1664525u + 1013904223u;
						const int f = (int)(m_rng % (DWORD)m_nFloors);
						m_rng = m_rng * 1664525u + 1013904223u;
						const int x = 1 + (int)(m_rng % (DWORD)(m_n - 2));
						m_rng = m_rng * 1664525u + 1013904223u;
						const int z = 1 + (int)(m_rng % (DWORD)(m_n - 2));
						if (!isAwayFromOuter(x, z)) continue;
						if (CellAtF(f, x, z) != CELL_FLOOR) continue;
						int openN = 0;
						for (int i = 0; i < 4; i++) {
							const int nx = x + dx4[i], nz = z + dz4[i];
							if (nx < 0 || nz < 0 || nx >= m_n || nz >= m_n) continue;
							const BYTE nc = CellAtF(f, nx, nz);
							if (nc != CELL_WALL && nc != CELL_WINDOW && nc != CELL_MIRROR_WALL && nc != CELL_DOOR)
								openN++;
						}
						if (openN != 2) continue;
						CellF(f, x, z) = CELL_DOOR;
						memset(seenBits, 0, (cap + 7) / 8);
						size_t qh = 0, qt = 0;
						const UINT start = (UINT)idOf(0, sx, sz);
						seenBits[start >> 3] = (BYTE)(seenBits[start >> 3] | (BYTE)(1u << (start & 7)));
						dist[start] = 0; q[qt++] = start;
						int bestKx = -1, bestKz = -1, bestKf = -1, bestScore = -1;
						const int minMan = max(4, m_n / 10);
						while (qh < qt) {
							const UINT cur = q[qh++];
							const int cf = (int)(cur / stride);
							const size_t rem = cur - (size_t)cf * stride;
							const int cz = (int)(rem / (size_t)m_n);
							const int cx = (int)(rem - (size_t)cz * (size_t)m_n);
							if (cf == f && CellAtF(cf, cx, cz) == CELL_FLOOR && isAwayFromOuter(cx, cz)) {
								const int man = abs(cx - x) + abs(cz - z);
								if (man >= minMan) {
									const int sc = man + (int)dist[cur];
									if (sc > bestScore) { bestScore = sc; bestKx = cx; bestKz = cz; bestKf = cf; }
								}
							}
							auto tryPush = [&](int nf, int nx, int nz) {
								if (!MazeWalkable(nf, nx, nz)) return;
								const UINT nid = (UINT)idOf(nf, nx, nz);
								if ((seenBits[nid >> 3] >> (nid & 7)) & 1) return;
								if (qt >= qCap) return;
								seenBits[nid >> 3] = (BYTE)(seenBits[nid >> 3] | (BYTE)(1u << (nid & 7)));
								dist[nid] = dist[cur] + 1;
								q[qt++] = nid;
							};
							for (int i = 0; i < 4; i++)
								tryPush(cf, cx + dx4[i], cz + dz4[i]);
							const BYTE cc = CellAtF(cf, cx, cz);
							if (cc == CELL_STAIRS_DOWN || cc == CELL_STAIRS_UP) {
								int pf = 0, px = 0, pz = 0;
								if (FindStairPartner(cf, cx, cz, pf, px, pz))
									tryPush(pf, px, pz);
							}
						}
						if (bestKx < 0) {
							CellF(f, x, z) = CELL_FLOOR;
							continue;
						}
						CellF(bestKf, bestKx, bestKz) = CELL_KEY;
						placed = TRUE;
					}
				}
			}
			delete[] seenBits;
			delete[] q;
			delete[] dist;
		}
	}
	// ゴール到達ルート必須：鍵所持想定で確認。不通なら扉・鍵を外して再保証
	{
		const int kh = m_keysHeld;
		m_keysHeld = 1000;
		if (!EnsureGoalReachable()) {
			for (int f = 0; f < m_nFloors; f++) {
				if (!m_grids[f]) continue;
				for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++) {
					const BYTE c = CellAtF(f, x, z);
					if (c == CELL_KEY || c == CELL_DOOR)
						CellF(f, x, z) = CELL_FLOOR;
				}
			}
			EnsureGoalReachable();
		}
		m_keysHeld = 0;
		(void)kh;
	}
	// 外周帯に残った特殊マスを内側へ退避（埋まり防止の最終掃除）
	{
		auto clearOuterSpecial = [&](int f, int x, int z, BYTE c) {
			if (isAwayFromOuter(x, z)) return;
			if (c == CELL_KEY || c == CELL_DOOR || c == CELL_PORTAL || c == CELL_MIRROR_FLOOR
				|| S3mIsPickupCell(c) || S3mIsTrapCell(c)) {
				CellF(f, x, z) = isPassOdd(x, z) ? CELL_FLOOR : CELL_WALL;
				if (S3mIsPickupCell(c) && m_itemsLeft > 0) m_itemsLeft--;
			}
		};
		for (int f = 0; f < m_nFloors; f++) {
			if (!m_grids[f]) continue;
			for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++)
				clearOuterSpecial(f, x, z, CellAtF(f, x, z));
		}
	}
	RefreshGoalCache();
	RecountExploreStats();

	MarkVisited();
	m_runDirty = 1;
	PersistRun();
	UpdateStatus();
	RenderScene();
}


// 3000x3000x4 向け省メモリ BFS（seen=bit / parent=UINT / queue 成長）
// 巨大な構造体キューをフル確保すると Win32 で new 失敗しナビが全滅する
struct S3mCompactBfs {
	CSoft3DMazeDlg* d;
	size_t stride, cap;
	BYTE* seenBits;
	UINT* parent;
	UINT* q;
	size_t qCap, qh, qt;
	struct P { int x, z, f; };
	P* portals;
	int nPortal;
	int dx4[4];
	int dz4[4];

	S3mCompactBfs()
		: d(NULL), stride(0), cap(0), seenBits(NULL), parent(NULL), q(NULL)
		, qCap(0), qh(0), qt(0), portals(NULL), nPortal(0)
	{
		dx4[0]=1; dx4[1]=-1; dx4[2]=0; dx4[3]=0;
		dz4[0]=0; dz4[1]=0; dz4[2]=1; dz4[3]=-1;
	}
	~S3mCompactBfs() { Destroy(); }

	void Destroy() {
		delete[] seenBits; seenBits = NULL;
		delete[] parent; parent = NULL;
		delete[] q; q = NULL;
		delete[] portals; portals = NULL;
		qCap = qh = qt = 0; nPortal = 0; cap = 0;
	}

	BOOL Init(CSoft3DMazeDlg* dlg) {
		Destroy();
		d = dlg;
		if (!d || d->m_n <= 0 || d->m_nFloors <= 0) return FALSE;
		stride = (size_t)d->m_n * (size_t)d->m_n;
		cap = stride * (size_t)d->m_nFloors;
		seenBits = new (std::nothrow) BYTE[(cap + 7) / 8];
		parent = new (std::nothrow) UINT[cap];
		if (!seenBits || !parent) { Destroy(); return FALSE; }
		nPortal = 0;
		for (int f = 0; f < d->m_nFloors; f++) {
			if (!d->m_grids[f]) continue;
			for (int z = 1; z < d->m_n - 1; z += 2)
				for (int x = 1; x < d->m_n - 1; x += 2)
					if (d->CellAtF(f, x, z) == CSoft3DMazeDlg::CELL_PORTAL) nPortal++;
		}
		if (nPortal > 0) {
			portals = new (std::nothrow) P[nPortal];
			if (!portals) { Destroy(); return FALSE; }
			int w = 0;
			for (int f = 0; f < d->m_nFloors; f++) {
				if (!d->m_grids[f]) continue;
				for (int z = 1; z < d->m_n - 1; z += 2)
					for (int x = 1; x < d->m_n - 1; x += 2)
						if (d->CellAtF(f, x, z) == CSoft3DMazeDlg::CELL_PORTAL)
							portals[w++] = { x, z, f };
			}
		}
		return TRUE;
	}

	void ClearSearch() {
		if (seenBits) memset(seenBits, 0, (cap + 7) / 8);
		qh = qt = 0;
	}

	size_t Id(int f, int x, int z) const {
		return (size_t)f * stride + (size_t)z * (size_t)d->m_n + (size_t)x;
	}
	BOOL Seen(size_t i) const { return (seenBits[i >> 3] >> (i & 7)) & 1; }
	void Mark(size_t i) { seenBits[i >> 3] = (BYTE)(seenBits[i >> 3] | (BYTE)(1u << (i & 7))); }

	BOOL EnsureQ(size_t liveNeed) {
		if (liveNeed <= qCap) return TRUE;
		size_t nc = qCap ? qCap : ((size_t)1 << 18);
		while (nc < liveNeed) {
			if (nc >= cap) { nc = cap; break; }
			nc *= 2;
			if (nc > cap) nc = cap;
		}
		if (liveNeed > nc) return FALSE;
		UINT* nq = new (std::nothrow) UINT[nc];
		if (!nq) return FALSE;
		if (q && qt > qh)
			memcpy(nq, q + qh, (qt - qh) * sizeof(UINT));
		qt -= qh; qh = 0;
		delete[] q; q = nq; qCap = nc;
		return TRUE;
	}

	BOOL Push(UINT id, UINT from) {
		if (Seen(id)) return TRUE;
		if (qt >= qCap) {
			const size_t live = qt - qh;
			if (qh > 0 && live + 1 <= qCap) {
				memmove(q, q + qh, live * sizeof(UINT));
				qt = live; qh = 0;
			} else if (!EnsureQ(live + 1)) {
				return FALSE;
			}
		}
		if (qt >= qCap) return FALSE;
		Mark(id);
		parent[id] = from;
		q[qt++] = id;
		return TRUE;
	}

	void Decode(UINT id, int& f, int& x, int& z) const {
		f = (int)(id / stride);
		const size_t rem = id - (size_t)f * stride;
		z = (int)(rem / (size_t)d->m_n);
		x = (int)(rem - (size_t)z * (size_t)d->m_n);
	}

	BOOL Pump() {
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				::PostQuitMessage((int)msg.wParam);
				return FALSE;
			}
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		return TRUE;
	}

	// 1=到達, 0=不通, -1=失敗
	int Reach(int sf, int sx, int sz, int tf, int tx, int tz, UINT* distOut) {
		if (!d->MazeWalkable(sf, sx, sz) || !d->MazeWalkable(tf, tx, tz)) return 0;
		ClearSearch();
		const UINT start = (UINT)Id(sf, sx, sz);
		const UINT goal = (UINT)Id(tf, tx, tz);
		if (!EnsureQ(1)) return -1;
		Mark(start);
		parent[start] = 0xFFFFFFFFu;
		if (distOut) distOut[start] = 0;
		q[qt++] = start;
		size_t steps = 0;
		while (qh < qt) {
			if ((++steps & 0xFFFFu) == 0 && !Pump()) return -1;
			const UINT cur = q[qh++];
			if (cur == goal) return 1;
			int f, x, z; Decode(cur, f, x, z);
			auto tryPush = [&](int nf, int nx, int nz) -> BOOL {
				if (!d->MazeWalkable(nf, nx, nz)) return TRUE;
				const UINT id = (UINT)Id(nf, nx, nz);
				if (Seen(id)) return TRUE;
				if (!Push(id, cur)) return FALSE;
				if (distOut) distOut[id] = distOut[cur] + 1;
				return TRUE;
			};
			for (int i = 0; i < 4; i++)
				if (!tryPush(f, x + dx4[i], z + dz4[i])) return -1;
			const BYTE c = d->CellAtF(f, x, z);
			if (c == CSoft3DMazeDlg::CELL_STAIRS_DOWN || c == CSoft3DMazeDlg::CELL_STAIRS_UP) {
				int pf = 0, px = 0, pz = 0;
				if (d->FindStairPartner(f, x, z, pf, px, pz))
					if (!tryPush(pf, px, pz)) return -1;
			}
			if (c == CSoft3DMazeDlg::CELL_PORTAL && portals) {
				for (int pi = 0; pi < nPortal; pi++) {
					if (portals[pi].f != f) continue;
					if (portals[pi].x == x && portals[pi].z == z) continue;
					if (!tryPush(portals[pi].f, portals[pi].x, portals[pi].z)) return -1;
				}
			}
		}
		return 0;
	}

	BOOL FarthestPass(int sx, int sz, int& bestX, int& bestZ, int& bestF, int& bestD) {
		bestX = -1; bestZ = -1; bestF = 0; bestD = -1;
		if (!d->MazeWalkable(0, sx, sz)) return FALSE;
		UINT* dist = new (std::nothrow) UINT[cap];
		if (!dist) return FALSE;
		ClearSearch();
		const UINT start = (UINT)Id(0, sx, sz);
		if (!EnsureQ(1)) { delete[] dist; return FALSE; }
		Mark(start); parent[start] = 0xFFFFFFFFu; dist[start] = 0; q[qt++] = start;
		const int n = d->m_n;
		size_t steps = 0;
		while (qh < qt) {
			if ((++steps & 0xFFFFu) == 0 && !Pump()) { delete[] dist; return FALSE; }
			const UINT cur = q[qh++];
			int f, x, z; Decode(cur, f, x, z);
			const int dd = (int)dist[cur];
			const BOOL pass = ((x & 1) != 0 && (z & 1) != 0 && x > 0 && z > 0 && x < n - 1 && z < n - 1);
			if (pass && d->CellAtF(f, x, z) == CSoft3DMazeDlg::CELL_FLOOR
				&& !(f == 0 && x == sx && z == sz) && dd > bestD) {
				bestD = dd; bestX = x; bestZ = z; bestF = f;
			}
			auto tryPush = [&](int nf, int nx, int nz) -> BOOL {
				if (!d->MazeWalkable(nf, nx, nz)) return TRUE;
				const UINT id = (UINT)Id(nf, nx, nz);
				if (Seen(id)) return TRUE;
				if (!Push(id, cur)) return FALSE;
				dist[id] = dist[cur] + 1;
				return TRUE;
			};
			for (int i = 0; i < 4; i++)
				if (!tryPush(f, x + dx4[i], z + dz4[i])) { delete[] dist; return FALSE; }
			const BYTE c = d->CellAtF(f, x, z);
			if (c == CSoft3DMazeDlg::CELL_STAIRS_DOWN || c == CSoft3DMazeDlg::CELL_STAIRS_UP) {
				int pf = 0, px = 0, pz = 0;
				if (d->FindStairPartner(f, x, z, pf, px, pz))
					if (!tryPush(pf, px, pz)) { delete[] dist; return FALSE; }
			}
			if (c == CSoft3DMazeDlg::CELL_PORTAL && portals) {
				for (int pi = 0; pi < nPortal; pi++) {
					if (portals[pi].f != f) continue;
					if (portals[pi].x == x && portals[pi].z == z) continue;
					if (!tryPush(portals[pi].f, portals[pi].x, portals[pi].z)) { delete[] dist; return FALSE; }
				}
			}
		}
		delete[] dist;
		return bestX >= 0;
	}

	// 経路の先頭 maxPts 点（連続）を outPts へ。*outCount に格納数、*outFullLen に全長
	int PathPrefix(int sf, int sx, int sz, int tf, int tx, int tz,
		short* outX, short* outZ, short* outF, int maxPts, int* outCount, int* outFullLen) {
		if (outCount) *outCount = 0;
		if (outFullLen) *outFullLen = 0;
		const int r = Reach(sf, sx, sz, tf, tx, tz, NULL);
		if (r != 1) return r;
		const UINT start = (UINT)Id(sf, sx, sz);
		const UINT goal = (UINT)Id(tf, tx, tz);
		int nFull = 0;
		for (UINT i = goal; ; ) {
			nFull++;
			if (i == start || parent[i] == 0xFFFFFFFFu) break;
			i = parent[i];
			if (nFull > (int)cap) return -1;
		}
		if (outFullLen) *outFullLen = nFull;
		if (!outX || maxPts <= 0) return 1;
		const int nOut = (nFull < maxPts) ? nFull : maxPts;
		// ゴール側から辿り、スタートからの index < nOut の点だけ拾う（全経路配列不要）
		int pos = nFull - 1;
		for (UINT i = goal; ; ) {
			if (pos < nOut) {
				int f, x, z; Decode(i, f, x, z);
				outX[pos] = (short)x; outZ[pos] = (short)z; outF[pos] = (short)f;
			}
			if (i == start || parent[i] == 0xFFFFFFFFu) break;
			i = parent[i];
			pos--;
			if (pos < 0) break;
		}
		if (outCount) *outCount = nOut;
		return 1;
	}
};

BOOL CSoft3DMazeDlg::EnsureGoalReachable()
{
	if (m_n <= 0 || m_nFloors <= 0 || !m_grids[0])
		return FALSE;
	auto isPass = [&](int x, int z) -> BOOL {
		return ((x & 1) != 0 && (z & 1) != 0 && x > 0 && z > 0 && x < m_n - 1 && z < m_n - 1) ? TRUE : FALSE;
	};
	int gx = -1, gz = -1, gf = -1;
	int sx = -1, sz = -1;
	for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++) {
		if (CellAtF(0, x, z) == CELL_START) { sx = x; sz = z; }
	}
	if (sx < 0) { sx = 1; sz = ((m_n - 2) & 1) ? (m_n - 2) : (m_n - 3); }
	for (int f = 0; f < m_nFloors; f++) {
		if (!m_grids[f]) continue;
		for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++) {
			if (CellAtF(f, x, z) != CELL_GOAL) continue;
			if (!isPass(x, z) || !MazeWalkable(f, x, z)) {
				CellF(f, x, z) = CELL_FLOOR;
				continue;
			}
			gx = x; gz = z; gf = f;
		}
	}

	S3mCompactBfs bfs;
	if (!bfs.Init(this))
		return FALSE; // OOM 時は既存ゴールを壊さない

	if (gf >= 0) {
		const int r = bfs.Reach(0, sx, sz, gf, gx, gz, NULL);
		if (r == 1) { RefreshGoalCache(); return TRUE; }
		if (r < 0) return FALSE;
	}

	for (int f = 0; f < m_nFloors; f++) {
		if (!m_grids[f]) continue;
		for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++)
			if (CellAtF(f, x, z) == CELL_GOAL)
				CellF(f, x, z) = CELL_FLOOR;
	}
	int bestX = -1, bestZ = -1, bestF = 0, bestD = -1;
	if (bfs.FarthestPass(sx, sz, bestX, bestZ, bestF, bestD) && bestX >= 0)
		CellF(bestF, bestX, bestZ) = CELL_GOAL;
	m_mapBakeDirty = 1;
	RefreshGoalCache();
	gx = m_goalX; gz = m_goalZ; gf = m_goalFloor;
	if (gx < 0 || gz < 0 || gf < 0 || gf >= m_nFloors || !m_grids[gf]
		|| CellAtF(gf, gx, gz) != CELL_GOAL)
		return FALSE;
	const int r2 = bfs.Reach(0, sx, sz, gf, gx, gz, NULL);
	return (r2 == 1) ? TRUE : FALSE;
}

void CSoft3DMazeDlg::ClearNavPath()
{
	m_navCount = 0;
}

BOOL CSoft3DMazeDlg::ComputeNavHint()
{
	ClearNavPath();
	if (m_n <= 0 || m_nFloors <= 0 || !m_grids[0])
		return FALSE;
	{
		const int kh = m_keysHeld;
		m_keysHeld = 1000;
		EnsureGoalReachable();
		m_keysHeld = kh;
	}
	const int sx = (int)floorf(m_px), sz = (int)floorf(m_pz);
	if (!MazeWalkable(m_floor, sx, sz))
		return FALSE;
	int gx = m_goalX, gz = m_goalZ, gf = m_goalFloor;
	if (gx < 0 || gz < 0 || gf < 0 || gf >= m_nFloors
		|| !m_grids[gf] || CellAtF(gf, gx, gz) != CELL_GOAL) {
		RefreshGoalCache();
		gx = m_goalX; gz = m_goalZ; gf = m_goalFloor;
	}
	if (gx < 0) return FALSE;

	S3mCompactBfs bfs;
	if (!bfs.Init(this))
		return FALSE;

	short ox[S3M_NAV_MAX], oz[S3M_NAV_MAX], of[S3M_NAV_MAX];
	int nOut = 0, nFull = 0;
	int r = bfs.PathPrefix(m_floor, sx, sz, gf, gx, gz, ox, oz, of, S3M_NAV_MAX, &nOut, &nFull);
	// ゴール不通（鍵不足で扉）→ 到達可能な最短の鍵へ（1回のBFSで探索）
	if (r != 1) {
		UINT* dist = new (std::nothrow) UINT[bfs.cap];
		if (!dist) return FALSE;
		bfs.ClearSearch();
		const UINT start = (UINT)bfs.Id(m_floor, sx, sz);
		if (!bfs.EnsureQ(1)) { delete[] dist; return FALSE; }
		bfs.Mark(start); bfs.parent[start] = 0xFFFFFFFFu; dist[start] = 0; bfs.q[bfs.qt++] = start;
		int bestF = -1, bestX = -1, bestZ = -1; UINT bestD = 0xFFFFFFFFu;
		while (bfs.qh < bfs.qt) {
			const UINT cur = bfs.q[bfs.qh++];
			int f, x, z; bfs.Decode(cur, f, x, z);
			if (CellAtF(f, x, z) == CELL_KEY && dist[cur] < bestD) {
				bestD = dist[cur]; bestF = f; bestX = x; bestZ = z;
			}
			auto tryPush = [&](int nf, int nx, int nz) {
				if (!MazeWalkable(nf, nx, nz)) return;
				const UINT id = (UINT)bfs.Id(nf, nx, nz);
				if (bfs.Seen(id)) return;
				if (!bfs.Push(id, cur)) return;
				dist[id] = dist[cur] + 1;
			};
			for (int i = 0; i < 4; i++)
				tryPush(f, x + bfs.dx4[i], z + bfs.dz4[i]);
			const BYTE c = CellAtF(f, x, z);
			if (c == CELL_STAIRS_DOWN || c == CELL_STAIRS_UP) {
				int pf = 0, px = 0, pz = 0;
				if (FindStairPartner(f, x, z, pf, px, pz))
					tryPush(pf, px, pz);
			}
			if (c == CELL_PORTAL && bfs.portals) {
				for (int pi = 0; pi < bfs.nPortal; pi++) {
					if (bfs.portals[pi].f != f) continue;
					if (bfs.portals[pi].x == x && bfs.portals[pi].z == z) continue;
					tryPush(bfs.portals[pi].f, bfs.portals[pi].x, bfs.portals[pi].z);
				}
			}
		}
		delete[] dist;
		if (bestF < 0) return FALSE;
		r = bfs.PathPrefix(m_floor, sx, sz, bestF, bestX, bestZ, ox, oz, of, S3M_NAV_MAX, &nOut, &nFull);
		if (r != 1 || nOut < 2) return FALSE;
	}
	m_navCount = nOut;
	for (int i = 0; i < nOut; i++) {
		m_navPath[i].x = ox[i];
		m_navPath[i].z = oz[i];
		m_navPath[i].f = of[i];
	}
	(void)nFull;
	return TRUE;
}

void CSoft3DMazeDlg::UpdateNavProgress()
{
	if (m_navCount <= 1) return;
	const int px = (int)floorf(m_px), pz = (int)floorf(m_pz);
	const S3mNavPt& dest = m_navPath[m_navCount - 1];
	if (m_floor == dest.f && px == dest.x && pz == dest.z)
		ClearNavPath();
}

void CSoft3DMazeDlg::ApplyItem(int kind)
{
	// 動画再生中は音／再生操作を無視（動画が動く・再生開始しない）
	if (mode == -2) {
		m_runDirty = 1;
		return;
	}
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
	case CELL_TEMPO_DN: {
		int pct = tempo / 2 - 10;
		if (pct < 25) pct = 25;
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
	case CELL_PREV:
		MpTaskbarPrevTrack();
		break;
	case CELL_VOL_UP:
		S3mNudgeVolPct(5);
		break;
	case CELL_VOL_DN:
		S3mNudgeVolPct(-5);
		break;
	case CELL_REVERB:
		S3mNudgeReverb(12);
		break;
	case CELL_XFADE: {
		savedata.play_xfade = savedata.play_xfade ? 0 : 1;
		if (savedata.play_xfade) {
			int s = savedata.play_xfade_sec100 + 100;
			if (s < 200) s = 200;
			if (s > 12000) s = 12000;
			savedata.play_xfade_sec100 = s;
		}
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SyncPlayXfadeUi(TRUE);
		else if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (og->m_xfade.GetSafeHwnd())
				og->m_xfade.SetCheck(savedata.play_xfade ? BST_CHECKED : BST_UNCHECKED);
		}
		MpPersistSavedataQuick();
		break;
	}
	case CELL_EQ:
		m_rng = m_rng * 1664525u + 1013904223u;
		S3mEqBump((int)(m_rng % 15u), 18);
		m_rng = m_rng * 1664525u + 1013904223u;
		S3mEqBump((int)(m_rng % 15u), -10);
		break;
	case CELL_EQ_FLAT:
		S3mEqFlatten(12);
		break;
	case CELL_RANDOM:
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			// savedata.random==0 → ランダム再生中。トグルする
			if (savedata.random == 0)
				og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK6, BN_CLICKED), 0);
			else
				og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK5, BN_CLICKED), 0);
		} else {
			savedata.random = savedata.random ? 0 : 1;
			MpPersistSavedataQuick();
		}
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
	if (m_portalIgnoreX >= 0 && (ix != m_portalIgnoreX || iz != m_portalIgnoreZ)) {
		m_portalIgnoreX = -1;
		m_portalIgnoreZ = -1;
	}
	const BYTE c = CellAt(ix, iz);
	if (S3mIsPickupCell(c)) {
		ApplyItem(c);
		Cell(ix, iz) = CELL_FLOOR;
		if (m_itemsLeft > 0) m_itemsLeft--;
		Soft3DSfxOneShot(S3SFX_ITEM, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
		UpdateStatus();
	} else if (c == CELL_KEY) {
		m_keysHeld++;
		Cell(ix, iz) = CELL_FLOOR;
		m_runDirty = 1;
		if (IsOverviewActive()) m_mapBakeDirty = 1;
		m_toastKind = 2;
		m_doorMsgT = 2.0f;
		m_floorTextAPrev = -1.f;
		Soft3DSfxOneShot(S3SFX_KEY, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
		UpdateStatus();
	} else if (c == CELL_DOOR) {
		if (m_keysHeld > 0) {
			m_keysHeld--;
			Cell(ix, iz) = CELL_FLOOR;
			m_runDirty = 1;
			if (IsOverviewActive()) m_mapBakeDirty = 1;
			m_toastKind = 3;
			m_doorMsgT = 1.8f;
			m_floorTextAPrev = -1.f;
			Soft3DSfxOneShot(S3SFX_DOOR, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
			UpdateStatus();
		}
	} else if (c == CELL_PORTAL && m_portalFx == PORTALFX_IDLE && m_clearPhase == CLEAR_IDLE
		&& m_floorFx == FLOORFX_IDLE
		&& !(ix == m_portalIgnoreX && iz == m_portalIgnoreZ)) {
		int ox = -1, oz = -1;
		for (int z = 0; z < m_n; z++) for (int x = 0; x < m_n; x++) {
			if (x == ix && z == iz) continue;
			if (CellAt(x, z) == CELL_PORTAL) { ox = x; oz = z; break; }
		}
		if (ox >= 0)
			BeginPortalWarp(ox, oz);
	} else if (c == CELL_STAIRS_DOWN || c == CELL_STAIRS_UP) {
		int pf = 0, px = 0, pz = 0;
		if (m_clearPhase == CLEAR_IDLE && m_floorFx == FLOORFX_IDLE && m_portalFx == PORTALFX_IDLE
			&& FindStairPartner(m_floor, ix, iz, pf, px, pz))
			BeginFloorChange(pf, px, pz);
	} else if (c == CELL_GOAL && !m_won && m_clearPhase == CLEAR_IDLE) {
		BeginClearSequence();
		PersistRun();
	}
	TryTrapEnter();
}

void CSoft3DMazeDlg::ResetPortalFx()
{
	m_portalFx = PORTALFX_IDLE;
	m_portalFxT = 0.f;
	m_portalFlashA = 0.f;
	m_portalToX = m_px;
	m_portalToZ = m_pz;
	m_portalIgnoreX = -1;
	m_portalIgnoreZ = -1;
}

void CSoft3DMazeDlg::BeginPortalWarp(int toX, int toZ)
{
	if (toX < 0 || toZ < 0 || toX >= m_n || toZ >= m_n) return;
	if (m_portalFx != PORTALFX_IDLE) return;
	m_moving = 0;
	m_turning = 0;
	m_portalToX = (float)toX + 0.5f;
	m_portalToZ = (float)toZ + 0.5f;
	m_portalFx = PORTALFX_OUT;
	m_portalFxT = 0.f;
	m_portalFlashA = 0.f;
	m_runDirty = 1;
	Soft3DSfxOneShot(S3SFX_PORTAL, GridToWorldX(m_px), GetRenderEyeY(), GridToWorldZ(m_pz));
}

void CSoft3DMazeDlg::TickPortalFx(float dt)
{
	if (m_portalFx == PORTALFX_IDLE) return;
	m_portalFxT += dt;
	const float kOut = 0.42f;
	const float kIn = 0.55f;
	if (m_portalFx == PORTALFX_OUT) {
		const float t = min(1.f, m_portalFxT / kOut);
		// 吸い込み：急速に明るく→白飛び
		m_portalFlashA = t * t * (0.55f + 0.45f * t);
		if (m_portalFxT >= kOut) {
			m_px = m_portalToX;
			m_pz = m_portalToZ;
			m_pxTarget = m_px;
			m_pzTarget = m_pz;
			m_portalIgnoreX = (int)floorf(m_px);
			m_portalIgnoreZ = (int)floorf(m_pz);
			m_trapCellX = m_portalIgnoreX;
			m_trapCellZ = m_portalIgnoreZ;
			MarkVisited();
			m_mapBakeDirty = 1;
			m_portalFx = PORTALFX_IN;
			m_portalFxT = 0.f;
			m_portalFlashA = 1.f;
			UpdateNavProgress();
			UpdateStatus();
		}
	} else if (m_portalFx == PORTALFX_IN) {
		const float t = min(1.f, m_portalFxT / kIn);
		// 着地バースト→減衰
		m_portalFlashA = (1.f - t) * (1.f - t) * (0.85f + 0.4f * sinf(t * 18.f));
		if (m_portalFlashA < 0.f) m_portalFlashA = 0.f;
		if (m_portalFxT >= kIn) {
			m_portalFx = PORTALFX_IDLE;
			m_portalFxT = 0.f;
			m_portalFlashA = 0.f;
			m_runDirty = 1;
		}
	}
}

void CSoft3DMazeDlg::TryTrapEnter()
{
	const int ix = (int)floorf(m_px);
	const int iz = (int)floorf(m_pz);
	if (ix < 0 || iz < 0 || ix >= m_n || iz >= m_n) return;
	if (ix == m_trapCellX && iz == m_trapCellZ) {
		// 粘液の上にいる間は遅延を維持
		if (CellAt(ix, iz) == CELL_SLIME && m_slowT < 0.35f)
			m_slowT = 0.85f;
		return;
	}
	m_trapCellX = ix;
	m_trapCellZ = iz;
	const BYTE c = CellAt(ix, iz);
	if (c == CELL_SLIME) {
		m_slowT = 2.2f;
		Soft3DSfxOneShot(S3SFX_SLIME, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
	} else if (c == CELL_SPIKE) {
		Soft3DSfxOneShot(S3SFX_SPIKE, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
		// 進入前のマスへ戻る（ワープではなく跳ね返り）
		if (!m_moving && !m_turning
			&& !IsBlocked(m_stepFromX, m_stepFromZ)) {
			m_pxTarget = m_stepFromX;
			m_pzTarget = m_stepFromZ;
			m_moving = 1;
			m_lastStepMx = (int)floorf(m_stepFromX) - ix;
			m_lastStepMz = (int)floorf(m_stepFromZ) - iz;
		}
	} else if (c == CELL_ICE) {
		if (m_lastStepMx != 0 || m_lastStepMz != 0)
			m_iceSlideLeft = 1;
		Soft3DSfxOneShot(S3SFX_ICE, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
	} else if (c == CELL_DARK) {
		m_darkT = 3.2f;
		Soft3DSfxOneShot(S3SFX_DARK, GridToWorldX((float)ix + 0.5f), GetRenderEyeY(), GridToWorldZ((float)iz + 0.5f));
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
	m_stairStartX = m_px; m_stairStartZ = m_pz;
	m_stairLandX = m_px; m_stairLandZ = m_pz;
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

// 階層切替: 斜め2マスの階段に沿って着地マスへ移動（画面フェードなし）
void CSoft3DMazeDlg::BeginFloorChange(int newFloor, int landX, int landZ)
{
	if (m_n <= 0 || newFloor < 0 || newFloor >= m_nFloors || newFloor == m_floor)
		return;
	if (landX < 0 || landZ < 0 || landX >= m_n || landZ >= m_n)
		return;
	m_moving = 0;
	m_turning = 0;
	m_stairFrom = m_floor;
	m_stairTo = newFloor;
	m_stairSwapDone = 0;
	m_stairCamY = 0.f;
	m_stairShiftX = 0.f;
	m_stairShiftZ = 0.f;
	m_stairStartX = m_px;
	m_stairStartZ = m_pz;
	m_stairLandX = (float)landX + 0.5f;
	m_stairLandZ = (float)landZ + 0.5f;
	m_trapCellX = -1;
	m_trapCellZ = -1;
	m_pxTarget = m_stairLandX;
	m_pzTarget = m_stairLandZ;
	m_miniFadeFrom = m_floor;
	m_miniFadeTo = newFloor;
	m_miniFade = 0.f;
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
	Soft3DSfxOneShot(S3SFX_STAIR, GridToWorldX(m_px), GetRenderEyeY(), GridToWorldZ(m_pz));
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

	// 階段移動後：ラベルだけ長く残す（操作は再開）
	if (m_floorFx == FLOORFX_HOLD) {
		m_floorFxT += dt;
		const float kHold = 1.55f;
		const float kFade = 0.65f;
		if (m_floorFxT < kHold) m_floorTextA = 1.f;
		else if (m_floorFxT < kHold + kFade) m_floorTextA = 1.f - (m_floorFxT - kHold) / kFade;
		else { ResetFloorFx(); return; }
		m_floorScreenA = 0.f;
		RefreshFloorTex();
		return;
	}

	m_floorFxT += dt;
	const float kDur = 0.92f;
	float t = m_floorFxT / kDur;
	if (t > 1.f) t = 1.f;
	const float s = t * t * (3.f - 2.f * t); // smoothstep
	const int dir = (m_stairTo > m_stairFrom) ? 1 : -1; // 下り=+1
	const float story = 1.35f;
	m_stairCamY = -dir * story * s;

	// グリッド上を相手マスへ斜め移動（壁1マスをまたぐ）
	m_px = m_stairStartX + (m_stairLandX - m_stairStartX) * s;
	m_pz = m_stairStartZ + (m_stairLandZ - m_stairStartZ) * s;
	m_pxTarget = m_px;
	m_pzTarget = m_pz;
	m_stairShiftX = 0.f;
	m_stairShiftZ = 0.f;

	if (!m_stairSwapDone && t >= 0.5f) {
		BindFloor(m_stairTo);
		m_mapViewFloor = m_stairTo;
		m_stairSwapDone = 1;
		MarkVisited();
		m_runDirty = 1;
		UpdateNavProgress();
		UpdateStatus();
	}

	// 移動中はフェードイン→表示維持（消灯はHOLD側）
	if (t < 0.15f) m_floorTextA = t / 0.15f;
	else m_floorTextA = 1.f;
	m_floorScreenA = 0.f;
	RefreshFloorTex();

	if (m_floorFxT >= kDur) {
		if (!m_stairSwapDone) {
			BindFloor(m_stairTo);
			m_mapViewFloor = m_stairTo;
			MarkVisited();
			UpdateNavProgress();
			UpdateStatus();
		}
		m_px = m_stairLandX;
		m_pz = m_stairLandZ;
		m_pxTarget = m_px;
		m_pzTarget = m_pz;
		m_stairCamY = 0.f;
		m_stairShiftX = 0.f;
		m_stairShiftZ = 0.f;
		m_stairSwapDone = 0;
		m_floorFx = FLOORFX_HOLD;
		m_floorFxT = 0.f;
		m_floorTextA = 1.f;
		m_floorTextAPrev = -1.f;
		RefreshFloorTex();
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
	CStringW label;
	if (m_toastKind && m_floorFx == FLOORFX_IDLE && m_doorMsgT > .01f) {
		if (m_toastKind == 1)
			label = LL14(L"鍵がないので開きません", L"No key — won't open", L"Pas de clé — ne s'ouvre pas", L"Senza chiave — non si apre", L"Sin llave — no abre",
				L"열쇠가 없어 열리지 않습니다", L"没有钥匙，打不开", L"لا مفتاح — لن يفتح", L"Нет ключа — не откроется", L"Kein Schlüssel — öffnet nicht",
				L"Sem chave — não abre", L"Geen sleutel — opent niet", L"Brak klucza — nie otworzy", L"Anahtar yok — açılmaz");
		else if (m_toastKind == 2)
			label = LL14(L"鍵を手に入れた", L"Got a key", L"Clé obtenue", L"Chiave presa", L"Llave obtenida",
				L"열쇠를 얻었다", L"得到了钥匙", L"حصلت على مفتاح", L"Ключ получен", L"Schlüssel erhalten",
				L"Chave obtida", L"Sleutel gekregen", L"Zdobyto klucz", L"Anahtar alındı");
		else
			label = LL14(L"扉を開けた", L"Door unlocked", L"Porte ouverte", L"Porta aperta", L"Puerta abierta",
				L"문을 열었다", L"门已打开", L"فُتح الباب", L"Дверь открыта", L"Tür geöffnet",
				L"Porta aberta", L"Deur geopend", L"Otwarto drzwi", L"Kapı açıldı");
	} else {
		const int labF = (m_floorFx == FLOORFX_IN || m_floorFx == FLOORFX_HOLD) ? m_stairTo : m_floor;
		label = FloorLabel(labF);
	}
	// αはシェーダ Misc.y — 文字が同じなら再焼きしない
	if (m_view.m_srvClear && label == m_toastBakeText) {
		m_floorTextAPrev = m_floorTextA;
		return;
	}
	m_view.BakeClearTexture((LPCWSTR)label, 1.f);
	m_toastBakeText = label;
	m_floorTextAPrev = m_floorTextA;
	m_clearTextAPrev = -1.f;
}

void CSoft3DMazeDlg::RefreshClearTex()
{
	// 階層表示中は同じテクスチャを共有しているので触らない
	if(m_floorFx!=FLOORFX_IDLE||m_floorTextA>.01f)return;
	if(!m_view.m_ready||m_clearTextA<=.01f){m_view.ReleaseClearTexture();m_clearTextAPrev=m_clearTextA;m_toastBakeText.Empty();return;}
	const CStringW msg = LL14(
		L"クリア！", L"Clear!", L"Réussi !", L"Completato!", L"¡Completado!",
		L"클리어!", L"通关！", L"تم!", L"Пройдено!", L"Geschafft!",
		L"Concluído!", L"Gehaald!", L"Ukończono!", L"Temiz!");
	if (m_view.m_srvClear && msg == m_toastBakeText) { m_clearTextAPrev = m_clearTextA; return; }
	m_view.BakeClearTexture(msg, 1.f);
	m_toastBakeText = msg;
	m_clearTextAPrev = m_clearTextA;
}

void CSoft3DMazeDlg::OverviewFloorDelta(int d)
{
	if (d == 0 || m_nFloors <= 1 || !IsOverviewActive())
		return;
	int f = m_mapViewFloor + d;
	if (f < 0) f = 0;
	if (f >= m_nFloors) f = m_nFloors - 1;
	OverviewFloorSet(f);
}

void CSoft3DMazeDlg::OverviewFloorSet(int f)
{
	if (!IsOverviewActive())
		return;
	if (f < 0) f = 0;
	if (f >= m_nFloors) f = m_nFloors - 1;
	if (f == m_mapViewFloor)
		return;
	m_mapViewFloor = f;
	m_mapBakeDirty = 1;
	m_floorBarDirty = 1;
}

void CSoft3DMazeDlg::LayoutOverviewFloorUi(int viewW, int viewH)
{
	m_ovBarX = m_ovBarY = m_ovBarW = m_ovBarH = 0.f;
	m_ovRecX0 = m_ovRecX1 = 0.f;
	m_ovTabN = 0;
	for (int i = 0; i < S3M_MAX_FLOORS; i++)
		m_ovTabX0[i] = m_ovTabX1[i] = 0.f;
	if (viewW < 8 || viewH < 8 || !m_view.m_srvFloorBar || m_view.m_floorBarW <= 0)
		return;
	float tw = (float)m_view.m_floorBarW, th = (float)m_view.m_floorBarH;
	const float pad = 10.f;
	float tipReserve = 72.f;
	if (m_view.m_srvTip && m_view.m_tipH > 0 && m_view.m_tipW > 0) {
		float tw = (float)m_view.m_tipW, th = (float)m_view.m_tipH;
		const float maxW = (float)viewW - pad * 2.f;
		if (tw > maxW) { const float s = maxW / tw; tw *= s; th *= s; }
		if (th > 180.f) th = 180.f;
		tipReserve = th + 8.f;
	}
	const float maxW = (float)viewW - pad * 2.f;
	if (tw > maxW) {
		const float s = maxW / tw;
		tw *= s;
		th *= s;
	}
	m_ovBarW = tw;
	m_ovBarH = th;
	m_ovBarX = ((float)viewW - tw) * .5f;
	m_ovBarY = (float)viewH - tipReserve - th - pad;
	if (m_ovBarY < pad) m_ovBarY = pad;
	m_ovTabN = min(m_nFloors, 4);
	for (int i = 0; i < m_ovTabN; i++) {
		m_ovTabX0[i] = m_view.m_floorBarTabX0[i];
		m_ovTabX1[i] = m_view.m_floorBarTabX1[i];
	}
	m_ovRecX0 = m_view.m_floorBarRecX0;
	m_ovRecX1 = m_view.m_floorBarRecX1;
}

BOOL CSoft3DMazeDlg::HitOverviewFloorUi(CPoint clientPt, int& outAction) const
{
	outAction = -1;
	if (!IsOverviewActive() || m_ovBarW < 1.f || m_ovBarH < 1.f)
		return FALSE;
	if (clientPt.x < (int)m_ovBarX || clientPt.x > (int)(m_ovBarX + m_ovBarW)
		|| clientPt.y < (int)m_ovBarY || clientPt.y > (int)(m_ovBarY + m_ovBarH))
		return FALSE;
	const float u = ((float)clientPt.x - m_ovBarX) / m_ovBarW;
	if (u >= m_ovRecX0 && u <= m_ovRecX1) {
		outAction = 100;
		return TRUE;
	}
	for (int i = 0; i < m_ovTabN; i++) {
		if (u >= m_ovTabX0[i] && u <= m_ovTabX1[i]) {
			outAction = i;
			return TRUE;
		}
	}
	outAction = -1;
	return TRUE; // バー上だがタブ外：パン開始を抑止
}

BOOL CSoft3DMazeDlg::FinishOverviewUiPress(CPoint pt)
{
	const int press = m_ovPressAction;
	m_ovPressAction = -1;
	if (press < 0)
		return FALSE;
	int act = -1;
	const int dx = pt.x - m_ovPressPt.x;
	const int dy = pt.y - m_ovPressPt.y;
	if (abs(dx) >= 12 || abs(dy) >= 12)
		return TRUE;
	if (!HitOverviewFloorUi(pt, act) || act != press)
		return TRUE;
	if (act == 100) {
		OverviewFloorSet(m_floor);
		FocusOverviewOnPlayer();
	} else if (act >= 0 && act < m_nFloors)
		OverviewFloorSet(act);
	return TRUE;
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
	Soft3DSfxUi(S3SFX_GOAL, 0);
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

void CSoft3DMazeDlg::UpdateStatus()
{
	CString s;
	if (m_won || m_clearPhase != CLEAR_IDLE) {
		s = LL14(L"ゴール！曲に合わせて迷路クリア。", L"Goal! Maze cleared to the music.", L"But ! Labyrinthe terminé en musique.", L"Traguardo! Labirinto completato a ritmo.",
			L"¡Meta! Laberinto completado al ritmo.", L"골! 음악에 맞춰 미로 클리어.", L"终点！跟着音乐通关迷宫。", L"الهدف! أُكملت المتاهة مع الموسيقى.",
			L"Цель! Лабиринт пройден под музыку.", L"Ziel! Labyrinth im Takt geschafft.", L"Gol! Labirinto concluído com a música.", L"Doel! Doolhof gehaald op de muziek.",
			L"Cel! Labirynt ukończony przy muzyce.", L"Hedef! Labirent müzikle tamamlandı.");
	} else {
		if (m_passCells <= 0)
			RecountExploreStats();
		const int explorePct = (m_passCells > 0) ? (m_visitPassCells * 100 / m_passCells) : 0;
		s.Format(LL14(L"サイズ %d  残りアイテム %d  鍵 %d  探索 %d%%  （右クリックで設定）", L"Size %d  items left %d  keys %d  explore %d%%  (right-click settings)", L"Taille %d  objets %d  clés %d  exploré %d%%  (clic droit)", L"Dimensione %d  oggetti %d  chiavi %d  esplorato %d%%  (clic destro)",
			L"Tamaño %d  objetos %d  llaves %d  explorado %d%%  (clic derecho)", L"크기 %d  남은 아이템 %d  열쇠 %d  탐험 %d%%  (우클릭 설정)", L"尺寸 %d  剩余道具 %d  钥匙 %d  探索 %d%%  （右键设置）", L"الحجم %d  العناصر %d  المفاتيح %d  استكشاف %d%%  (انقر يمينًا)",
			L"Размер %d  предметов %d  ключей %d  исследовано %d%%  (ПКМ)", L"Größe %d  Items %d  Schlüssel %d  Erkundung %d%%  (Rechtsklick)", L"Tamanho %d  itens %d  chaves %d  explorado %d%%  (clique direito)", L"Grootte %d  items %d  sleutels %d  verkend %d%%  (rechtsklik)",
			L"Rozmiar %d  przedmioty %d  klucze %d  eksploracja %d%%  (PPM)", L"Boyut %d  öğe %d  anahtar %d  keşif %d%%  (sağ tık)"),
			m_n, m_itemsLeft, m_keysHeld, explorePct);
		CString fl;
		fl.Format(_T("  [%s]"), (LPCTSTR)FloorLabel(m_floor));
		s += fl;
		if (HasNavPath()) {
			CString nv;
			const S3mNavPt& dest = m_navPath[m_navCount - 1];
			const BYTE dc = (dest.f >= 0 && dest.f < m_nFloors) ? CellAtF(dest.f, dest.x, dest.z) : (BYTE)0;
			if (dc == CELL_KEY) {
				nv.Format(LL14(L"  ナビ…鍵へ%d歩", L"  Navi…key %d steps", L"  Nav…clé %d", L"  Navi…chiave %d", L"  Navi…llave %d",
					L"  내비…열쇠 %d", L"  导航…钥匙%d步", L"  Navi…key %d", L"  Нави…ключ %d", L"  Navi…Schlüssel %d", L"  Navi…chave %d", L"  Navi…sleutel %d", L"  Nawi…klucz %d", L"  Navi…anahtar %d"),
					m_navCount - 1);
			} else {
				nv.Format(LL14(L"  ナビ…ゴールへ%d歩", L"  Navi…goal %d steps", L"  Nav…but %d", L"  Navi…traguardo %d", L"  Navi…meta %d",
					L"  내비…골 %d", L"  导航…终点%d步", L"  Navi…goal %d", L"  Нави…цель %d", L"  Navi…Ziel %d", L"  Navi…gol %d", L"  Navi…doel %d", L"  Nawi…cel %d", L"  Navi…hedef %d"),
					m_navCount - 1);
			}
			s += nv;
		}
		if (savedata.s3m_zoom != 100) {
			CString z;
			z.Format(_T("  ×%.2f"), savedata.s3m_zoom / 100.f);
			s += z;
		}
	}
	if (m_status.GetSafeHwnd())
		m_status.SetWindowText(s);
}

float CSoft3DMazeDlg::EffectiveFovDeg() const
{
	const float base = (savedata.s3m_fov == 0) ? 55.f : ((savedata.s3m_fov == 2) ? 90.f : 70.f);
	int z = savedata.s3m_zoom;
	if (z < 50) z = 50;
	if (z > 250) z = 250;
	float deg = base * (100.f / (float)z); // ズーム大＝狭い視野
	if (deg < 32.f) deg = 32.f;
	if (deg > 110.f) deg = 110.f;
	return deg;
}

float CSoft3DMazeDlg::MapZoomScale() const
{
	int z = savedata.s3m_map_zoom;
	if (z < 50) z = 50;
	if (z > 400) z = 400;
	return (float)z / 100.f;
}

int CSoft3DMazeDlg::OverviewZoomMaxPct() const
{
	// 最大でおおよそ 36px/セルまで寄れる（ズーム制限を緩め）
	int basePx = 720;
	if (m_view.GetSafeHwnd()) {
		CRect rc;
		m_view.GetClientRect(&rc);
		basePx = (int)(OverviewBaseSide(rc.Width(), rc.Height()) + .5f);
	}
	if (basePx < 64) basePx = 64;
	const int n = max(1, m_n);
	const int need = (int)((36.f * (float)n / (float)basePx) * 100.f + .5f);
	if (need < 2000) return 2000; // 最低でも20倍まで
	if (need > 100000) return 100000;
	return need;
}

float CSoft3DMazeDlg::OverviewZoomScale() const
{
	int z = m_overviewZoomPct;
	const int mx = OverviewZoomMaxPct();
	if (z < 50) z = 50;
	if (z > mx) z = mx;
	return (float)z / 100.f;
}

float CSoft3DMazeDlg::OverviewBaseSide(int viewW, int viewH) const
{
	// 下に操作説明・上に階層ラベル用の余白を残す
	const float tipBand = 156.f;
	const float topPad = 68.f;
	const float aw = (float)viewW;
	const float ah = max(96.f, (float)viewH - tipBand - topPad);
	return min(aw, ah) * .92f;
}

void CSoft3DMazeDlg::ClampMapPan(int viewW, int viewH, float side)
{
	const float margin = 16.f;
	const float tipBand = 156.f;
	const float topPad = 68.f;
	const float areaTop = topPad;
	const float areaH = max(96.f, (float)viewH - tipBand - topPad);
	auto clampAxis = [&](float& pan, float view0, float viewSpan, float span) {
		const float centered = view0 + (viewSpan - span) * .5f;
		float minO, maxO;
		if (span <= viewSpan - margin * 2.f) {
			minO = view0 + margin;
			maxO = view0 + viewSpan - span - margin;
		} else {
			minO = view0 + viewSpan - span - margin;
			maxO = view0 + margin;
		}
		if (minO > maxO) { const float t = minO; minO = maxO; maxO = t; }
		float o = centered + pan;
		if (o < minO) o = minO;
		if (o > maxO) o = maxO;
		pan = o - centered;
	};
	clampAxis(m_mapPanX, 0.f, (float)viewW, side);
	clampAxis(m_mapPanY, areaTop, areaH, side);
}

void CSoft3DMazeDlg::InputFovZoom(int dir)
{
	if (dir == 0 || IsOverviewActive())
		return;
	int z = savedata.s3m_zoom;
	if (z < 50) z = 50;
	if (z > 250) z = 250;
	// ノッチ約 8%（ホイール1コマ）
	const float f = (dir > 0) ? (z * 1.08f) : (z / 1.08f);
	z = (int)(f + 0.5f);
	if (z < 50) z = 50;
	if (z > 250) z = 250;
	if (z == savedata.s3m_zoom)
		return;
	savedata.s3m_zoom = z;
	PersistUi();
	UpdateStatus();
}

void CSoft3DMazeDlg::InputMapZoom(int dir)
{
	if (dir == 0)
		return;
	if (IsOverviewActive()) {
		int z = m_overviewZoomPct;
		const int mx = OverviewZoomMaxPct();
		if (z < 50) z = 50;
		if (z > mx) z = mx;
		const float f = (dir > 0) ? (z * 1.12f) : (z / 1.12f);
		z = (int)(f + 0.5f);
		if (z < 50) z = 50;
		if (z > mx) z = mx;
		if (z == m_overviewZoomPct)
			return;
		m_overviewZoomPct = z;
		if (m_view.GetSafeHwnd()) {
			CRect rc; m_view.GetClientRect(&rc);
			const float side = OverviewBaseSide(rc.Width(), rc.Height()) * OverviewZoomScale();
			ClampMapPan(rc.Width(), rc.Height(), side);
		}
		return;
	}
	int z = savedata.s3m_map_zoom;
	if (z < 50) z = 50;
	if (z > 400) z = 400;
	const float f = (dir > 0) ? (z * 1.10f) : (z / 1.10f);
	z = (int)(f + 0.5f);
	if (z < 50) z = 50;
	if (z > 400) z = 400;
	if (z == savedata.s3m_map_zoom)
		return;
	savedata.s3m_map_zoom = z;
	PersistUi();
}

BOOL CSoft3DMazeDlg::BeginMapPan(CPoint clientPt)
{
	if (!IsOverviewActive()) return FALSE;
	m_mapPanDrag = 1;
	m_mapPanLast = clientPt;
	return TRUE;
}

BOOL CSoft3DMazeDlg::UpdateMapPan(CPoint clientPt)
{
	if (!m_mapPanDrag || !IsOverviewActive()) return FALSE;
	m_mapPanX += (float)(clientPt.x - m_mapPanLast.x);
	m_mapPanY += (float)(clientPt.y - m_mapPanLast.y);
	m_mapPanLast = clientPt;
	if (m_view.GetSafeHwnd()) {
		CRect rc; m_view.GetClientRect(&rc);
		const float side = OverviewBaseSide(rc.Width(), rc.Height()) * OverviewZoomScale();
		ClampMapPan(rc.Width(), rc.Height(), side);
	}
	return TRUE;
}

BOOL CSoft3DMazeDlg::EndMapPan()
{
	if (!m_mapPanDrag) return FALSE;
	m_mapPanDrag = 0;
	return TRUE;
}

BOOL CSoft3DMazeDlg::HitTestMinimap(CPoint clientPt) const
{
	if (!savedata.s3m_show_map || !m_view.GetSafeHwnd())
		return FALSE;
	CRect rc;
	m_view.GetClientRect(&rc);
	const int w = rc.Width(), h = rc.Height();
	if (w < 16 || h < 16)
		return FALSE;
	const float mpix = (float)min(w, h) * .30f;
	const float mcx = (float)w - 10.f - mpix * .5f;
	const float mcy = 10.f + mpix * .5f;
	const float pad = mpix * .5f + 3.f;
	return clientPt.x >= (int)(mcx - pad) && clientPt.x <= (int)(mcx + pad)
		&& clientPt.y >= (int)(mcy - pad) && clientPt.y <= (int)(mcy + pad);
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
		// 扉は鍵所持時のみ（MazeWalkable と同規則）。壁帯すり抜け禁止のため1マス先も必ず見る
		return MazeWalkable(m_floor, x, z);
	};

	int tx = -1, tz = -1;
	auto noteNoKeyDoor = [&](int x, int z) {
		if (x < 0 || z < 0 || x >= m_n || z >= m_n) return;
		if (CellAt(x, z) == CELL_DOOR && m_keysHeld <= 0) {
			m_toastKind = 1;
			m_doorMsgT = 2.2f;
			m_floorTextAPrev = -1.f;
		}
	};
	auto failMove = [&](int x, int z) {
		noteNoKeyDoor(x, z);
		if (x >= 0 && z >= 0 && x < m_n && z < m_n && CellAt(x, z) == CELL_DOOR && m_keysHeld <= 0)
			Soft3DSfxOneShot(S3SFX_LOCKED, GridToWorldX((float)x + 0.5f), GetRenderEyeY(), GridToWorldZ((float)z + 0.5f));
		else if (m_sfxBumpCool <= 0.f) {
			Soft3DSfxOneShot(S3SFX_BUMP, GridToWorldX(m_px), GetRenderEyeY(), GridToWorldZ(m_pz));
			m_sfxBumpCool = 0.14f;
		}
	};
	// 2マス移動では扉マスに着地しないため、通過マスで開錠する
	auto tryOpenDoorAt = [&](int x, int z) {
		if (x < 0 || z < 0 || x >= m_n || z >= m_n) return;
		if (CellAt(x, z) != CELL_DOOR || m_keysHeld <= 0) return;
		m_keysHeld--;
		Cell(x, z) = CELL_FLOOR;
		m_runDirty = 1;
		if (IsOverviewActive()) m_mapBakeDirty = 1;
		m_toastKind = 3;
		m_doorMsgT = 1.8f;
		m_floorTextAPrev = -1.f;
		UpdateStatus();
		Soft3DSfxOneShot(S3SFX_DOOR, GridToWorldX((float)x + 0.5f), GetRenderEyeY(), GridToWorldZ((float)z + 0.5f));
	};
	if ((cx & 1) && (cz & 1)) {
		// 奇数×奇数の通路: 2マス進むには「1マス先も通行可」が必須（壁・鍵なし扉のすり抜け禁止）
		// 1マス先だけ通れるなら広間へ1マス
		if (standable(a1x, a1z) && standable(a2x, a2z)) {
			tx = a2x;
			tz = a2z;
			tryOpenDoorAt(a1x, a1z);
			tryOpenDoorAt(a2x, a2z);
		} else if (standable(a1x, a1z)) {
			tx = a1x;
			tz = a1z;
			tryOpenDoorAt(a1x, a1z);
		} else {
			failMove(a1x, a1z);
			return FALSE;
		}
	} else {
		// 広間・壁帯上など: 1マスのみ（壁／鍵なし扉へは進めない）
		if (!standable(a1x, a1z)) {
			failMove(a1x, a1z);
			return FALSE;
		}
		tx = a1x;
		tz = a1z;
		tryOpenDoorAt(a1x, a1z);
	}

	if (!standable(tx, tz)) {
		failMove(tx, tz);
		return FALSE;
	}

	m_pxTarget = (float)tx + 0.5f;
	m_pzTarget = (float)tz + 0.5f;
	m_stepFromX = m_px;
	m_stepFromZ = m_pz;
	m_lastStepMx = mx;
	m_lastStepMz = mz;
	m_moving = 1;
	Soft3DSfxOneShot(S3SFX_STEP, GridToWorldX(m_px), GetRenderEyeY(), GridToWorldZ(m_pz));
	return TRUE;
}

void CSoft3DMazeDlg::TickMove(float dt)
{
	if (m_n <= 0) return;
	if (m_sfxBumpCool > 0.f) m_sfxBumpCool = max(0.f, m_sfxBumpCool - dt);
	m_anim += dt;
	TickFloorFx(dt);
	// 階層ラベル／クリア／ナビ待ちと共有する中央トースト（同じ BakeClearTexture 経路）
	if (m_toastKind && m_floorFx == FLOORFX_IDLE && m_clearPhase == CLEAR_IDLE && !m_navWaiting) {
		if (m_doorMsgT > 0.f) {
			m_doorMsgT -= dt;
			if (m_doorMsgT < 0.f) m_doorMsgT = 0.f;
			const float T = (m_toastKind == 1) ? 2.2f : ((m_toastKind == 2) ? 2.0f : 1.8f);
			const float left = m_doorMsgT;
			const float elapsed = T - left;
			if (elapsed < 0.22f) m_floorTextA = elapsed / 0.22f;
			else if (left > 0.38f) m_floorTextA = 1.f;
			else m_floorTextA = left / 0.38f;
			RefreshFloorTex();
		} else {
			m_toastKind = 0;
			m_floorTextA = 0.f;
			RefreshFloorTex();
		}
	}
	if (m_portalFx != PORTALFX_IDLE) {
		TickPortalFx(dt);
		m_moveHeld = 1;
		m_turnHeld = 1;
		if (savedata.s3m_bob) m_bob += dt * 4.f;
		m_anim += dt;
		return;
	}
	if (m_clearPhase != CLEAR_IDLE) {
		if(savedata.s3m_bob)m_bob += dt * 3.f;
		return;
	}

	// パッド（レースと同系）: 左スティック／十字=移動、右スティックX／LB・RB=旋回、A／Start=全体マップ、B=マップ解除
	S3mJoyState joy = {};
	S3mUpdateJoypadState(joy);
	static int s_padBtnPrev = 0;
	const int padEdge = joy.connected ? (joy.buttons & ~s_padBtnPrev) : 0;
	s_padBtnPrev = joy.connected ? joy.buttons : 0;
	if (padEdge & ((1 << 0) | (1 << 7)))
		ToggleMapOverlay();
	if ((padEdge & (1 << 1)) && IsOverviewActive()) {
		m_mapToggle = 0;
		m_mapPanDrag = 0;
	}
	float padMoveX = 0.f, padMoveZ = 0.f;
	float padTurnX = 0.f;
	if (joy.connected) {
		padMoveX = joy.lx;
		padMoveZ = -joy.ly;
		if (joy.hat >= 0) {
			static const float hx[8] = { 0, 0.7f, 1, 0.7f, 0, -0.7f, -1, -0.7f };
			static const float hy[8] = { 1, 0.7f, 0, -0.7f, -1, -0.7f, 0, 0.7f };
			padMoveX += hx[joy.hat];
			padMoveZ += hy[joy.hat];
		}
		padTurnX = joy.rx;
		if (joy.buttons & (1 << 4)) padTurnX -= 1.f; // LB
		if (joy.buttons & (1 << 5)) padTurnX += 1.f; // RB
	}

	// 階層切替の演出中は移動・旋回しない（ラベル残表示HOLDは操作可）
	if (m_floorFx == FLOORFX_IN) {
		m_moveHeld = 1;
		m_turnHeld = 1;
		if (savedata.s3m_bob) m_bob += dt * 2.f;
		return;
	}
	// 全体マップ表示中は確認のみ（移動・旋回しない）。←→ / A D / パッド左右で表示階層を切替
	if (IsOverviewActive()) {
		m_moveHeld = 1;
		m_turnHeld = 1;
		const bool fl = (GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000) || (padMoveX < -0.45f);
		const bool fr = (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000) || (padMoveX > 0.45f);
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
	if (m_slowT > 0.f) {
		m_slowT -= dt;
		if (m_slowT < 0.f) m_slowT = 0.f;
	}
	if (m_darkT > 0.f) {
		m_darkT -= dt;
		if (m_darkT < 0.f) m_darkT = 0.f;
	}

	const bool turnL = (GetAsyncKeyState('Q') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000) || (padTurnX < -0.45f);
	const bool turnR = (GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (padTurnX > 0.45f);
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
	if (mx == 0 && mz == 0) {
		if (padMoveZ > 0.45f) mz = 1;
		else if (padMoveZ < -0.45f) mz = -1;
		if (mz == 0) {
			if (padMoveX < -0.45f) mx = -1;
			else if (padMoveX > 0.45f) mx = 1;
		}
	}
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
		static float s_stepAcc = 0.f;
		s_stepAcc += dt;
		const float per = (m_slowT > 0.f) ? 0.40f : 0.24f;
		if (s_stepAcc >= per) {
			s_stepAcc -= per;
			Soft3DSfxOneShot(S3SFX_STEP, GridToWorldX(m_px), GetRenderEyeY(), GridToWorldZ(m_pz));
		}
		const float dx = m_pxTarget - m_px;
		const float dz = m_pzTarget - m_pz;
		const float dist = sqrtf(dx * dx + dz * dz);
		// 2マス移動を約 0.22s（粘液トラップ中は遅延）
		float spd = 2.f / 0.22f;
		if (m_slowT > 0.f) spd *= 0.38f;
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
			s_stepAcc = 0.f;
			MarkVisited();
			UpdateNavProgress();
			UpdateStatus();
			{
				const int nx = (int)floorf(m_px), nz = (int)floorf(m_pz);
				const int mx = (ox + nx) / 2, mz = (oz + nz) / 2;
				if (mx >= 0 && mz >= 0 && mx < m_n && mz < m_n
					&& !IsBlocked((float)mx + 0.5f, (float)mz + 0.5f)) {
					NoteVisitCell(mx, mz);
				}
			}
			TryPickup();
			if (m_iceSlideLeft > 0 && !m_moving && !m_turning && m_floorFx != FLOORFX_IN) {
				const int smx = m_lastStepMx, smz = m_lastStepMz;
				m_iceSlideLeft = 0;
				TryStep(smx, smz);
			}
			m_runDirty = 1;
		} else {
			m_px += dx / dist * step;
			m_pz += dz / dist * step;
			// 薄い壁帯の上に留まらないよう飛ばす（鍵なし扉はすり抜けない）
			if (IsBlocked(m_px, m_pz) && !IsBlocked(m_pxTarget, m_pzTarget)) {
				const BYTE mid = CellAt((int)floorf(m_px), (int)floorf(m_pz));
				if (mid == CELL_WALL || mid == CELL_WINDOW || mid == CELL_MIRROR_WALL) {
					m_px = m_pxTarget;
					m_pz = m_pzTarget;
				}
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
	const float fov=EffectiveFovDeg()*(float)(M_PI/180.0);
	const float zNear=.05f,zFar=80.f;
	S3MFrameCB cb={};cb.viewProj=S3mMatMul(S3mLookAt(ex,eyeY,ez,ex+fx,eyeY,ez+fz,0,1,0),S3mPerspective(fov,(float)w/(float)h,zNear,zFar));
	// Qullusrent流: LightDir＝面→光源、ライト位置＝注視点＋LightDir*距離（向きを完全一致）
	float lx=.40f,ly=.88f,lz=.22f;float llen=sqrtf(lx*lx+ly*ly+lz*lz);lx/=llen;ly/=llen;lz/=llen;
	cb.lightDir={lx,ly,lz,0.f}; // w=0: シャドウ生成中フラグ（テッセ固定用）
	const float lDist=28.f;const float focusY=.40f;
	const float lpx=ex+lx*lDist,lpy=focusY+ly*lDist,lpz=ez+lz*lDist;
	cb.lightVP=S3mMatMul(S3mLookAt(lpx,lpy,lpz,ex,focusY,ez,0,1,0),S3mOrtho(-12.f,12.f,-12.f,12.f,2.f,60.f));
	cb.eyePos={ex,eyeY,ez,0.f};
	{
		const int thFog=ThemeOfFloor((m_floorFx==FLOORFX_IN)?m_stairFrom:m_floor);
		float fogNear=5.5f,fogFar=16.f,fogH=.04f,fogY=.15f;
		if(thFog<=0){fogNear=5.5f;fogFar=16.f;fogH=.04f;fogY=.15f;}
		else if(thFog==1){fogNear=6.2f;fogFar=19.f;fogH=.028f;fogY=.10f;}
		else if(thFog==2){fogNear=6.5f;fogFar=20.f;fogH=.025f;fogY=.08f;}
		else{fogNear=5.8f;fogFar=17.5f;fogH=.035f;fogY=.10f;}
		if(m_darkT>0.f){
			const float k=min(1.f,m_darkT/1.2f);
			fogNear=max(2.f,fogNear*(1.f-k*.72f));
			fogFar=max(fogNear+3.f,fogFar*(1.f-k*.55f));
			fogH+=.08f*k;
		}
		cb.fogParams={fogNear,fogFar,fogH,fogY};
		cb.eyePos.w=(float)thFog;
	}
	// DOF: 約6マス以遠からぼけ開始（通路マス≈1ワールド単位）。手前はシャープ
	cb.dofParams={6.0f,7.0f,4.0f,m_bob};cb.screenSize={(float)w,(float)h,1.f/w,1.f/h};cb.misc={m_clearScreenA,m_clearTextA,1.f/tanf(fov*.5f),m_anim};
	D3D11_MAPPED_SUBRESOURCE map={};if(FAILED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map)))return;memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);
	const S3MMat vpMat=cb.viewProj;
	UINT maxV=m_view.m_vbDynBytes/sizeof(S3MVertex);
	if(maxV<65536u)maxV=65536u;
	{
		const UINT needDynBytes=maxV*sizeof(S3MVertex);
		if(m_view.m_cpuDynScratchBytes<needDynBytes){
			delete[] m_view.m_cpuDynScratch;
			m_view.m_cpuDynScratch=new (std::nothrow) BYTE[needDynBytes];
			m_view.m_cpuDynScratchBytes=m_view.m_cpuDynScratch?needDynBytes:0;
		}
	}
	S3MVertex* v=m_view.m_cpuDynScratch?(S3MVertex*)m_view.m_cpuDynScratch:NULL;
	if(!v)return;
	UINT floorBeg=0,nFloor=0,wallBeg=0,nWall=0,transBeg=0,nTrans=0;int phase=0;
	// 頂点が足りなければCPUスクラッチを倍々拡張（描画欠け防止）。上限≈64MB
	const UINT kDynVertCap=(64u*1024u*1024u)/sizeof(S3MVertex);
	auto ensureDynCap=[&](UINT needVerts)->BOOL{
		if(needVerts<=maxV)return TRUE;
		if(needVerts>kDynVertCap)return FALSE;
		UINT neu=maxV?maxV:65536u;
		while(neu<needVerts){
			if(neu>=kDynVertCap)return FALSE;
			UINT n2=neu*2u;if(n2<=neu)return FALSE;neu=n2;
			if(neu>kDynVertCap)neu=kDynVertCap;
		}
		const UINT bytes=neu*sizeof(S3MVertex);
		BYTE* nb=new (std::nothrow) BYTE[bytes];
		if(!nb)return FALSE;
		const UINT used=nFloor+nWall+nTrans;
		if(v&&used)memcpy(nb,v,used*sizeof(S3MVertex));
		delete[] m_view.m_cpuDynScratch;
		m_view.m_cpuDynScratch=nb;
		m_view.m_cpuDynScratchBytes=bytes;
		v=(S3MVertex*)nb;
		maxV=neu;
		return TRUE;
	};
	auto put=[&](float x,float y,float z,float nx,float ny,float nz,float u,float vv,float r,float g,float b,float a)->BOOL{
		UINT n=nFloor+nWall+nTrans;
		if(n>=maxV&&!ensureDynCap(n+1u))return FALSE;
		v[n]={x,y,z,nx,ny,nz,u,vv,r,g,b,a};if(phase==2)nTrans++;else if(phase==1)nWall++;else nFloor++;return TRUE;
	};
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
		// CPU細分化はしない（HSテッセに任せる）。N>=2 だと床・天井・VFXが溢れる
		patch1(x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3,nx,ny,nz,u0,v0,u1,v1,r,g,b,a);
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
	// 小物用：CPU背面カリングなし（細い直方体が縦線だけになるのを防ぐ）
	auto passCubeAll=[&](float x0,float z0,float x1,float z1,float y0,float y1,float r,float g,float b,float a){
		float xa=x0,xb=x1,za=z0,zb=z1,ya=y0,yb=y1;
		if(xa>xb){float t=xa;xa=xb;xb=t;} if(za>zb){float t=za;za=zb;zb=t;} if(ya>yb){float t=ya;ya=yb;yb=t;}
		quadUV(xa,yb,za,xb,yb,za,xb,yb,zb,xa,yb,zb,0,1,0,0,0,1,1,r,g,b,a);
		quadUV(xa,ya,zb,xb,ya,zb,xb,ya,za,xa,ya,za,0,-1,0,0,0,1,1,r*.7f,g*.7f,b*.7f,a);
		quadUV(xa,ya,za,xa,ya,zb,xa,yb,zb,xa,yb,za,-1,0,0,0,0,1,1,r,g,b,a);
		quadUV(xb,ya,zb,xb,ya,za,xb,yb,za,xb,yb,zb,1,0,0,0,0,1,1,r,g,b,a);
		quadUV(xa,ya,za,xb,ya,za,xb,yb,za,xa,yb,za,0,0,-1,0,0,1,1,r,g,b,a);
		quadUV(xb,ya,zb,xa,ya,zb,xa,yb,zb,xb,yb,zb,0,0,1,0,0,1,1,r,g,b,a);
	};
	auto emitSphere=[&](float cx,float cy,float cz,float rx,float ry,float rz,int nl,int nb,float r,float g,float b,float a,float u0,float v0,float u1,float v1){
		if(nl<3)nl=3; if(nb<2)nb=2;
		for(int i=0;i<nl;i++){
			const float a0=(float)i*(6.2831853f/(float)nl), a1=(float)(i+1)*(6.2831853f/(float)nl);
			for(int j=0;j<nb;j++){
				const float b0=-1.5707963f+(float)j*(3.14159265f/(float)nb);
				const float b1=b0+(3.14159265f/(float)nb);
				auto P=[&](float an,float bn,float& x,float& y,float& z,float& nx,float& ny,float& nz,float& u,float& vv){
					const float cb=cosf(bn),sb=sinf(bn),ca=cosf(an),sa=sinf(an);
					nx=cb*ca; ny=sb; nz=cb*sa;
					x=cx+nx*rx; y=cy+ny*ry; z=cz+nz*rz;
					u=u0+(u1-u0)*an*(1.f/6.2831853f);
					vv=v0+(v1-v0)*(bn+1.5707963f)*(1.f/3.14159265f);
				};
				float x0,y0,z0,n0x,n0y,n0z,uA,vA, x1,y1,z1,n1x,n1y,n1z,uB,vB, x2,y2,z2,n2x,n2y,n2z,uC,vC, x3,y3,z3,n3x,n3y,n3z,uD,vD;
				P(a0,b0,x0,y0,z0,n0x,n0y,n0z,uA,vA); P(a1,b0,x1,y1,z1,n1x,n1y,n1z,uB,vB);
				P(a1,b1,x2,y2,z2,n2x,n2y,n2z,uC,vC); P(a0,b1,x3,y3,z3,n3x,n3y,n3z,uD,vD);
				tri(x0,y0,z0,x1,y1,z1,x2,y2,z2,(n0x+n1x+n2x)/3.f,(n0y+n1y+n2y)/3.f,(n0z+n1z+n2z)/3.f,uA,vA,uB,vB,uC,vC,r,g,b,a);
				tri(x0,y0,z0,x2,y2,z2,x3,y3,z3,(n0x+n2x+n3x)/3.f,(n0y+n2y+n3y)/3.f,(n0z+n2z+n3z)/3.f,uA,vA,uC,vC,uD,vD,r,g,b,a);
			}
		}
	};
	auto emitCone=[&](float cx,float cy,float cz,float rBase,float h,int n,float r,float g,float b,float a,float u0,float v0,float u1,float v1){
		if(n<3)n=3;
		const float tx=cx,ty=cy+h,tz=cz;
		for(int i=0;i<n;i++){
			const float a0=(float)i*(6.2831853f/(float)n), a1=(float)(i+1)*(6.2831853f/(float)n);
			const float x0=cx+cosf(a0)*rBase, z0=cz+sinf(a0)*rBase;
			const float x1=cx+cosf(a1)*rBase, z1=cz+sinf(a1)*rBase;
			float nx=(x0+x1)*.5f-cx, nz=(z0+z1)*.5f-cz; const float nl=sqrtf(nx*nx+h*h+nz*nz)+1e-5f;
			const float uA=u0+(u1-u0)*(float)i/(float)n, uB=u0+(u1-u0)*(float)(i+1)/(float)n, uM=(uA+uB)*.5f;
			tri(x0,cy,z0,x1,cy,z1,tx,ty,tz,nx/nl,h/nl,nz/nl,uA,v1,uB,v1,uM,v0,r,g,b,a);
		}
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
	struct MirPick{float score;float nx,ny,nz,px,py,pz;};
	MirPick bestW={-1.f,0,0,0,0,0,0},bestF={-1.f,0,0,0,0,0,0};
	const float storyH=1.35f;
	const BOOL stairMove=(m_floorFx==FLOORFX_IN);
	const int stairDir=stairMove?((m_stairTo>m_stairFrom)?1:-1):0;
	struct Layer{UINT fBeg,nF,mfBeg,nMF,wBeg,nW,mwBeg,nMW;int th;};
	Layer layers[4];int nLay=0;
	auto themeFloorRGB=[&](int th,float k,float& r,float& g,float& b){
		if(th<=0){r=.78f*k;g=.64f*k;b=.48f*k;}
		else if(th==1){r=.66f*k;g=.76f*k;b=.90f*k;}
		else if(th==2){r=.82f*k;g=.70f*k;b=.58f*k;}
		else{r=.74f*k;g=.52f*k;b=.44f*k;}
	};
	auto themeWallTint=[&](int th,float& r,float& g,float& b){
		if(th<=0){r=1.04f;g=1.04f;b=1.02f;}
		else if(th==1){r=.94f;g=1.02f;b=1.16f;}
		else if(th==2){r=1.14f;g=1.00f;b=.90f;}
		else{r=1.16f;g=.92f;b=.82f;}
	};
	auto emitWallCell=[&](int f,float yBias,int x,int z,BOOL fullVis,BOOL mirror,float& wr,float& wg,float& wb){
		if(fullVis){
			const float dx=cellCX(x)-ex,dz=cellCZ(z)-ez;
			if(dx*dx+dz*dz>rad*rad)return;
			if(!vis(x,z)&&(dx*fx+dz*fz)<-2.5f){}
			else if(!vis(x,z)&&(dx*dx+dz*dz)>10.f*10.f)return;
		}
		const int vid=wallVid(x,z);float u0,v0,u1,v1;atlasUV(vid,u0,v0,u1,v1);
		if(mirror){u0=0;v0=0;u1=1;v1=1;}
		float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z),cx=cellCX(x),cz=cellCZ(z);
		float y0=yBias,y1=yBias+wallH;
		float mnx=0,mny=0,mnz=0,mpx=cx,mpy=y0+wallH*.45f,mpz=cz;
		if(mirror&&fabsf(yBias)<.01f){
			if(cellW(x)<=cellD(z)+1e-4f){mnx=(ex>=cx)?1.f:-1.f;mpx=(mnx>0)?x1:x0;}
			else{mnz=(ez>=cz)?1.f:-1.f;mpz=(mnz>0)?z1:z0;}
			float tox=ex-mpx,toy=eyeY-mpy,toz=ez-mpz;float facing=mnx*tox+mny*toy+mnz*toz;
			if(facing>.05f){float dist=sqrtf(tox*tox+toy*toy+toz*toz)+.01f;float sc=facing/(dist*.35f+1.f);if(sc>bestW.score)bestW={sc,mnx,mny,mnz,mpx,mpy,mpz};}
		}
		auto face=[&](float ax0,float ay0,float az0,float ax1,float ay1,float az1,float ax2,float ay2,float az2,float ax3,float ay3,float az3,float nx,float ny,float nz,float rr,float gg,float bb,float aa){
			patch(ax0,ay0,az0,ax1,ay1,az1,ax2,ay2,az2,ax3,ay3,az3,nx,ny,nz,u0,v0,u1,v1,rr,gg,bb,aa);
		};
		auto faceA=[&](float nx,float ny,float nz,float ax0,float ay0,float az0,float ax1,float ay1,float az1,float ax2,float ay2,float az2,float ax3,float ay3,float az3){
			float rr=wr,gg=wg,bb=wb,aa=1.f;
			if(mirror&&fabsf(nx-mnx)+fabsf(nz-mnz)<.1f){rr=.75f;gg=.88f;bb=1.f;aa=1.16f;}
			else if(mirror){rr=.82f;gg=.90f;bb=.98f;aa=1.f;}
			face(ax0,ay0,az0,ax1,ay1,az1,ax2,ay2,az2,ax3,ay3,az3,nx,ny,nz,rr,gg,bb,aa);
		};
		faceA(-1,0,0,x0,y0,z0,x0,y0,z1,x0,y1,z1,x0,y1,z0);
		faceA(1,0,0,x1,y0,z1,x1,y0,z0,x1,y1,z0,x1,y1,z1);
		faceA(0,0,-1,x1,y0,z0,x0,y0,z0,x0,y1,z0,x1,y1,z0);
		faceA(0,0,1,x0,y0,z1,x1,y0,z1,x1,y1,z1,x0,y1,z1);
		face(x0,y1,z0,x1,y1,z0,x1,y1,z1,x0,y1,z1,0,1,0,wr,wg,wb,1.f);
	};
	// 床・天井を先に、壁は後（溢れ時も床天井優先）。地下天井は壁パッチ（レンガ）で埋める
	auto emitLayer=[&](int f,float yBias,int ax0,int ax1,int az0,int az1,BOOL fullVis){
		if(f<0||f>=m_nFloors||!m_grids[f]||nLay>=4)return;
		Layer& L=layers[nLay];
		L.th=ThemeOfFloor(f);
		float wr,wg,wb;themeWallTint(L.th,wr,wg,wb);
		// --- 床（ソリッド）---
		L.fBeg=nFloor+nWall+nTrans;
		phase=0;
		UINT f0=nFloor;
		for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
			BYTE c=CellAtF(f,x,z);
			if(S3mIsSolidWall(c)||c==CELL_MIRROR_FLOOR)continue;
			if(fullVis&&!vis(x,z))continue;
			float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
			if(c!=CELL_STAIRS_DOWN&&c!=CELL_STAIRS_UP){
				float k=VisitAtF(f,x,z)?1.f:.90f;float r,g,b;themeFloorRGB(L.th,k,r,g,b);
				const float y1=yBias+passH;
				quad(x0,y1,z0,x1,y1,z0,x1,y1,z1,x0,y1,z1,0,1,0,r,g,b,1.f);
			}
		}
		L.nF=nFloor-f0;
		L.mfBeg=nFloor+nWall+nTrans;
		UINT mf0=nFloor;
		for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
			if(CellAtF(f,x,z)!=CELL_MIRROR_FLOOR)continue;
			if(fullVis&&!vis(x,z))continue;
			float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
			float k=VisitAtF(f,x,z)?1.f:.92f;
			float r=.88f*k+.10f,g=.94f*k+.06f,b=1.f*k;
			const float y1=yBias+passH;
			quad(x0,y1,z0,x1,y1,z0,x1,y1,z1,x0,y1,z1,0,1,0,r,g,b,1.16f);
			if(fabsf(yBias)<.01f){
				float cx=cellCX(x),cz=cellCZ(z);
				float tox=ex-cx,toz=ez-cz;float dist=sqrtf(tox*tox+toz*toz)+.01f;
				float sc=(passH+.02f)/(dist*.4f+1.f);
				if(sc>bestF.score)bestF={sc,0.f,1.f,0.f,cx,passH+.02f,cz};
			}
		}
		L.nMF=nFloor-mf0;
		// --- 壁＋地下天井（パッチ／レンガ）---
		L.wBeg=nFloor+nWall+nTrans;
		phase=1;
		UINT w0=nWall;
		// 地下: 通路上の天井を壁テクスチャの下向きパッチで埋める（先に確保）
		if(f>0){
			for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
				BYTE c=CellAtF(f,x,z);
				if(S3mIsSolidWall(c))continue;
				if(c==CELL_STAIRS_UP)continue;
				if(fullVis&&!vis(x,z))continue;
				float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
				const float yc=yBias+wallH;
				const int vid=wallVid(x,z);float u0,v0,u1,v1;atlasUV(vid,u0,v0,u1,v1);
				// 下向き（室内から見る天井）
				patch(x0,yc,z0,x0,yc,z1,x1,yc,z1,x1,yc,z0,0,-1,0,u0,v0,u1,v1,wr*.72f,wg*.72f,wb*.72f,1.f);
			}
		}
		for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
			if(CellAtF(f,x,z)!=CELL_WALL)continue;
			emitWallCell(f,yBias,x,z,fullVis,FALSE,wr,wg,wb);
		}
		L.nW=nWall-w0;
		L.mwBeg=nFloor+nWall+nTrans;
		UINT mw0=nWall;
		float mr=.85f,mg=.92f,mb=1.f;
		for(int z=az0;z<=az1;z++)for(int x=ax0;x<=ax1;x++){
			if(CellAtF(f,x,z)!=CELL_MIRROR_WALL)continue;
			emitWallCell(f,yBias,x,z,fullVis,TRUE,mr,mg,mb);
		}
		L.nMW=nWall-mw0;
		nLay++;
	};

	if(stairMove){
		// 移動中: from を Y=0、to を -dir*storyH（カメラが斜めに降りる／昇る）
		emitLayer(m_stairFrom,0.f,ix0,ix1,iz0,iz1,TRUE);
		emitLayer(m_stairTo,-(float)stairDir*storyH,ix0,ix1,iz0,iz1,TRUE);
	}else{
		emitLayer(m_floor,0.f,ix0,ix1,iz0,iz1,TRUE);
		// 階段穴から隣接階を垣間見る（着地マス周辺）
		const int peekR=4;
		int seenU=-1,seenD=-1;
		for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){
			if(!vis(x,z))continue;
			BYTE c=CellAt(x,z);
			if(c!=CELL_STAIRS_DOWN&&c!=CELL_STAIRS_UP)continue;
			int pf=0,px=0,pz=0;
			if(!FindStairPartner(m_floor,x,z,pf,px,pz))continue;
			const int cx=(x+px)/2,cz=(z+pz)/2;
			if(c==CELL_STAIRS_DOWN&&m_floor+1<m_nFloors&&seenD!=m_floor+1){
				seenD=m_floor+1;
				emitLayer(m_floor+1,-storyH,max(0,cx-peekR),min(m_n-1,cx+peekR),max(0,cz-peekR),min(m_n-1,cz+peekR),FALSE);
			}else if(c==CELL_STAIRS_UP&&m_floor>0&&seenU!=m_floor-1){
				seenU=m_floor-1;
				emitLayer(m_floor-1,storyH,max(0,cx-peekR),min(m_n-1,cx+peekR),max(0,cz-peekR),min(m_n-1,cz+peekR),FALSE);
			}
		}
	}
	const UINT mainFloorN=layers[0].nF,mainWallBeg=layers[0].wBeg,mainWallN=layers[0].nW; // 互換用（影など）
	(void)mainFloorN;(void)mainWallBeg;(void)mainWallN;
	struct XL{float d;int x,z;BYTE c;};XL xl[1536];int nc=0;
	const int fxFloor=stairMove?m_stairFrom:m_floor;
	for(int z=iz0;z<=iz1&&nc<1536;z++)for(int x=ix0;x<=ix1&&nc<1536;x++){BYTE c=CellAtF(fxFloor,x,z);if(!vis(x,z))continue;
		if(c==CELL_WINDOW||c==CELL_GOAL||c==CELL_START||c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP||c==CELL_PORTAL||c==CELL_KEY||c==CELL_DOOR||S3mIsPickupCell(c)||S3mIsTrapCell(c)){
			float dx=cellCX(x)-ex,dz=cellCZ(z)-ez;xl[nc++]={dx*fx+dz*fz,x,z,c};}}
	for(int i=1;i<nc;i++){XL q=xl[i];int j=i-1;while(j>=0&&xl[j].d<q.d){xl[j+1]=xl[j];j--;}xl[j+1]=q;}
	transBeg=nFloor+nWall;phase=2;
	struct FxRec{UINT beg,n;float cx,cy,cz,d;float nx,ny,nz;BOOL plane;BOOL wantMir;int texKind;};FxRec fxObj[64];int nFx=0;int fxMirOf[64];
	struct CallRec{float wx,wy,wz,d;BYTE c;};CallRec calls[64];int nCall=0;
	for(int i=0;i<64;i++)fxMirOf[i]=-1;
	const int plx=(int)floorf(m_px),plz=(int)floorf(m_pz);
	for(int i=0;i<nc;i++){int x=xl[i].x,z=xl[i].z;BYTE c=xl[i].c;float ocx=cellCX(x),ocz=cellCZ(z),x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
		if(nCall<64 && S3mCalloutSlot(c)>=0) calls[nCall++]={ocx,passH+0.62f,ocz,xl[i].d,c};
		if(c==CELL_WINDOW){
			if(nFx>=64)continue;
			const UINT b=nFloor+nWall+nTrans;
			// ガラス：反射ありだが通れると誤解しないよう濃いアルファ（a>1.18 → glass）
			float rr=.22f,gg=.42f,bb=.58f,a=1.28f;
			float wnx=0.f,wnz=0.f;float wpx=ocx,wpz=ocz;
			float fr=.40f,fg=.38f,fb=.36f,fa=.98f,t=.022f;
			if(cellW(x)<cellD(z)){
				// 南北壁（Xに薄い）→ ±X 面
				wnx=(ex>=ocx)?1.f:-1.f;wpx=(wnx>0)?x1:x0;
				quad(x0,.02f,z0,x0,.02f,z1,x0,wallH-.02f,z1,x0,wallH-.02f,z0,-1,0,0,rr,gg,bb,a);
				quad(x1,.02f,z1,x1,.02f,z0,x1,wallH-.02f,z0,x1,wallH-.02f,z1,1,0,0,rr,gg,bb,a);
				quad(x0,0,z0,x0,0,z1,x0,t,z1,x0,t,z0,-1,0,0,fr,fg,fb,fa);
				quad(x0,wallH-t,z0,x0,wallH-t,z1,x0,wallH,z1,x0,wallH,z0,-1,0,0,fr,fg,fb,fa);
				quad(x1,0,z1,x1,0,z0,x1,t,z0,x1,t,z1,1,0,0,fr,fg,fb,fa);
				quad(x1,wallH-t,z1,x1,wallH-t,z0,x1,wallH,z0,x1,wallH,z1,1,0,0,fr,fg,fb,fa);
			}else{
				// 東西壁（Zに薄い）→ ±Z 面
				wnz=(ez>=ocz)?1.f:-1.f;wpz=(wnz>0)?z1:z0;
				quad(x1,.02f,z0,x0,.02f,z0,x0,wallH-.02f,z0,x1,wallH-.02f,z0,0,0,-1,rr,gg,bb,a);
				quad(x0,.02f,z1,x1,.02f,z1,x1,wallH-.02f,z1,x0,wallH-.02f,z1,0,0,1,rr,gg,bb,a);
				quad(x1,0,z0,x0,0,z0,x0,t,z0,x1,t,z0,0,0,-1,fr,fg,fb,fa);
				quad(x1,wallH-t,z0,x0,wallH-t,z0,x0,wallH,z0,x1,wallH,z0,0,0,-1,fr,fg,fb,fa);
				quad(x0,0,z1,x1,0,z1,x1,t,z1,x0,t,z1,0,0,1,fr,fg,fb,fa);
				quad(x0,wallH-t,z1,x1,wallH-t,z1,x1,wallH,z1,x0,wallH,z1,0,0,1,fr,fg,fb,fa);
			}
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,wpx,wallH*.5f,wpz,xl[i].d,wnx,0.f,wnz,TRUE,TRUE,2};
		}else if(c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP){
			if(nFx>=64)continue;
			// 斜め段：相手マス方向へ伸びる半透明ステップ（下／上が見えやすい）
			const UINT b=nFloor+nWall+nTrans;const BOOL dn=(c==CELL_STAIRS_DOWN);
			const float rr=dn?1.f:.22f,gg=dn?.58f:.86f,bb=dn?.16f:1.f;
			const float stairA=.70f;
			int pf=fxFloor,px=x,pz=z;
			if(!FindStairPartner(fxFloor,x,z,pf,px,pz)){px=x;pz=z;}
			const float ax=cellCX(x),az=cellCZ(z),bx=cellCX(px),bz=cellCZ(pz);
			const float span=sqrtf((bx-ax)*(bx-ax)+(bz-az)*(bz-az))+1e-5f;
			const float hx=(-(bz-az)/span)*.18f,hz=((bx-ax)/span)*.18f; // 段の幅
			const float nearDx=ocx-ex,nearDz=ocz-ez;const BOOL nearSt=(nearDx*nearDx+nearDz*nearDz)<(10.f*10.f);
			for(int st=0;st<6;st++){
				const float t0=(float)st/6.f,t1=(float)(st+1)/6.f;
				const float u0=t0*.92f,u1=t1*.92f;
				const float sx0=ax+(bx-ax)*u0,sz0=az+(bz-az)*u0;
				const float sx1=ax+(bx-ax)*u1,sz1=az+(bz-az)*u1;
				const float yTop=dn?(passH-t0*storyH*.95f):(passH+t0*storyH*.95f);
				const float yBot=dn?(passH-t1*storyH*.95f):(passH+t1*storyH*.95f);
				const float ya=min(yTop,yBot),yb=max(yTop,yBot)+(nearSt?.038f:.025f);
				passCube(min(sx0,sx1)-fabsf(hx),min(sz0,sz1)-fabsf(hz),
					max(sx0,sx1)+fabsf(hx),max(sz0,sz1)+fabsf(hz),ya,yb,rr,gg,bb,stairA);
			}
			if(nearSt){
				// 簡易手すり（両側1本ずつのレール）
				for(int side=0;side<2;side++){
					const float s=(side?1.f:-1.f);
					const float rx=hx*s*1.55f,rz=hz*s*1.55f;
					const float y0r=dn?passH:passH;
					const float y1r=dn?(passH-storyH*.88f):(passH+storyH*.88f);
					passCube(ax+rx-.015f,az+rz-.015f,ax+(bx-ax)*.9f+rx+.015f,az+(bz-az)*.9f+rz+.015f,
						min(y0r,y1r)+.18f,min(y0r,y1r)+.22f,rr*.85f,gg*.85f,bb*.85f,stairA+.12f);
				}
			}
			// 方向マーカー（矢印）
			const float tipX=ax+(bx-ax)*.55f,tipZ=az+(bz-az)*.55f;
			const float tipY=passH+(dn?-.25f:.45f)+.04f*sinf(m_anim*2.2f+(float)x);
			const float adx=(bx-ax)/span,adz=(bz-az)/span;
			const float sx=-adz*.07f,sz=adx*.07f;
			passCube(tipX-adx*.10f+sx,tipZ-adz*.10f+sz,tipX+adx*.02f-sx,tipZ+adz*.02f-sz,tipY,tipY+.05f,rr,gg,bb,stairA+.15f);
			passCube(tipX+adx*.00f-sx*1.6f,tipZ+adz*.00f-sz*1.6f,tipX+adx*.14f+sx*1.6f,tipZ+adz*.14f+sz*1.6f,tipY-.01f,tipY+.06f,rr,gg,bb,stairA+.18f);
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,passH+.2f,ocz,xl[i].d,0,0,0,FALSE,FALSE,4};
		}else if(c==CELL_PORTAL){
			if(nFx>=64)continue;
			const UINT b=nFloor+nWall+nTrans;
			const float rr=.72f,gg=.28f,bb=1.f,a=.55f;
			const float pad=.12f;
			const float pulse=.04f+.03f*sinf(m_anim*4.2f+(float)(x+z));
			quad(x0+pad,passH+.04f+pulse,z0+pad,x0+pad,passH+.04f+pulse,z1-pad,x1-pad,passH+.04f+pulse,z1-pad,x1-pad,passH+.04f+pulse,z0+pad,0,1,0,rr,gg,bb,a);
			const float bob=.06f*sinf(m_anim*3.1f+(float)x*.7f);
			// ポータル核：縦柱＋回転リング（◇廃止）
			passCubeAll(ocx-.05f,ocz-.05f,ocx+.05f,ocz+.05f,passH+.12f+bob,passH+.55f+bob,.9f,.5f,1.f,.9f);
			passCubeAll(ocx-.10f,ocz-.04f,ocx+.10f,ocz+.04f,passH+.28f+bob,passH+.40f+bob,.95f,.55f,1.f,.85f);
			passCubeAll(ocx-.04f,ocz-.10f,ocx+.04f,ocz+.10f,passH+.28f+bob,passH+.40f+bob,.95f,.55f,1.f,.85f);
			const float nearDx=ocx-ex,nearDz=ocz-ez;const BOOL nearP=(nearDx*nearDx+nearDz*nearDz)<(8.f*8.f);
			const int nRing=nearP?4:3;const int nSeg=10;
			for(int ri=0;ri<nRing;ri++){
				const float rad=.16f+ri*.065f;
				const float y=passH+.16f+ri*.11f+.03f*sinf(m_anim*5.f+ri);
				const float ang0=m_anim*(2.2f+ri*.7f);
				const float thick=nearP?.05f:.04f;
				for(int s=0;s<nSeg;s++){
					const float a0=ang0+(float)s*(6.2832f/(float)nSeg),a1=a0+.4f;
					const float px0=ocx+cosf(a0)*rad,pz0=ocz+sinf(a0)*rad;
					const float px1=ocx+cosf(a1)*rad,pz1=ocz+sinf(a1)*rad;
					passCubeAll(min(px0,px1)-.025f,min(pz0,pz1)-.025f,max(px0,px1)+.025f,max(pz0,pz1)+.025f,y,y+thick,
						.85f,.45f,1.f,.55f);
				}
			}
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,passH+.3f,ocz,xl[i].d,0,0,0,FALSE,FALSE,0};
		}else if(c==CELL_DOOR){
			if(nFx>=64)continue;
			const UINT b=nFloor+nWall+nTrans;
			const float dr=.72f,dg=.50f,db=.28f,da=1.05f;
			const float fr=.30f,fg=.24f,fb=.20f;
			const BOOL thinX=DoorThinX(fxFloor,x,z);
			const float wallT=0.10f;
			float dx0,dx1,dz0,dz1;
			if(thinX){dx0=ocx-wallT*.5f;dx1=ocx+wallT*.5f;dz0=z0;dz1=z1;}
			else{dz0=ocz-wallT*.5f;dz1=ocz+wallT*.5f;dx0=x0;dx1=x1;}
			passCube(dx0,dz0,dx1,dz1,passH,wallH*.92f,dr,dg,db,da);
			const float frameT=.05f;
			if(thinX){
				passCube(dx0-frameT*.2f,dz0,dx1+frameT*.2f,dz0+frameT,passH,wallH*.95f,fr,fg,fb,1.f);
				passCube(dx0-frameT*.2f,dz1-frameT,dx1+frameT*.2f,dz1,passH,wallH*.95f,fr,fg,fb,1.f);
			}else{
				passCube(dx0,dz0-frameT*.2f,dx0+frameT,dz1+frameT*.2f,passH,wallH*.95f,fr,fg,fb,1.f);
				passCube(dx1-frameT,dz0-frameT*.2f,dx1,dz1+frameT*.2f,passH,wallH*.95f,fr,fg,fb,1.f);
			}
			passCube(dx0,dz0,dx1,dz1,wallH*.88f,wallH*.95f,fr,fg,fb,1.f);
			if(thinX){
				const float mz=(dz0+dz1)*.5f;
				passCubeAll(dx0-.006f,mz-.014f,dx1+.006f,mz+.014f,passH+.08f,wallH*.86f,.22f,.16f,.12f,1.f);
				passCubeAll(dx0-.01f,dz0+.05f,dx1+.01f,dz1-.05f,passH+.50f,passH+.58f,.42f,.32f,.22f,1.f);
			}else{
				const float mx=(dx0+dx1)*.5f;
				passCubeAll(mx-.014f,dz0-.006f,mx+.014f,dz1+.006f,passH+.08f,wallH*.86f,.22f,.16f,.12f,1.f);
				passCubeAll(dx0+.05f,dz0-.01f,dx1-.05f,dz1+.01f,passH+.50f,passH+.58f,.42f,.32f,.22f,1.f);
			}
			const float hy0=passH+.40f,hy1=passH+.62f;
			const float hr=1.f,hg=.84f,hb=.30f,ha=1.12f;
			const float hoff=.20f;
			if(thinX){
				const float hz=ocz+hoff;
				passCubeAll(dx0-.014f,hz-.07f,dx0+.004f,hz+.07f,hy0-.05f,hy1+.05f,.48f,.38f,.24f,1.05f);
				passCubeAll(dx1-.004f,hz-.07f,dx1+.014f,hz+.07f,hy0-.05f,hy1+.05f,.48f,.38f,.24f,1.05f);
				passCubeAll(dx0-.055f,hz-.04f,dx0-.010f,hz+.04f,hy0,hy1,hr,hg,hb,ha);
				passCubeAll(dx1+.010f,hz-.04f,dx1+.055f,hz+.04f,hy0,hy1,hr,hg,hb,ha);
			}else{
				const float hx=ocx+hoff;
				passCubeAll(hx-.07f,dz0-.014f,hx+.07f,dz0+.004f,hy0-.05f,hy1+.05f,.48f,.38f,.24f,1.05f);
				passCubeAll(hx-.07f,dz1-.004f,hx+.07f,dz1+.014f,hy0-.05f,hy1+.05f,.48f,.38f,.24f,1.05f);
				passCubeAll(hx-.04f,dz0-.055f,hx+.04f,dz0-.010f,hy0,hy1,hr,hg,hb,ha);
				passCubeAll(hx-.04f,dz1+.010f,hx+.04f,dz1+.055f,hy0,hy1,hr,hg,hb,ha);
			}
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,passH+.45f,ocz,xl[i].d,0,0,0,FALSE,FALSE,3};
		}else if(c==CELL_KEY){
			if(nFx>=64)continue;
			const UINT b=nFloor+nWall+nTrans;
			passCubeAll(ocx-.14f,ocz-.14f,ocx+.14f,ocz+.14f,passH+.02f,passH+.09f,.35f,.32f,.28f,1.f);
			const float bob=.05f*sinf(m_anim*2.6f+(float)x);
			const float ky=passH+.22f+bob;
			const float ang=m_anim*1.6f;
			const float cs=cosf(ang),sn=sinf(ang);
			const float kr=1.f,kg=.88f,kb=.28f,ka=1.12f; // 金属帯（写り込み）＋鍵スパークル
			// 縦向き鍵：輪は XY 面（縦）、軸は下向き Y、歯は横へ
			auto kPut=[&](float lx,float ly0,float ly1,float lz,float hx,float hz){
				const float wx=ocx+lx*cs-lz*sn,wz=ocz+lx*sn+lz*cs;
				const float sx=fabsf(hx*cs)+fabsf(hz*sn)+.002f;
				const float sz=fabsf(hx*sn)+fabsf(hz*cs)+.002f;
				passCubeAll(wx-sx,wz-sz,wx+sx,wz+sz,ky+ly0,ky+ly1,kr,kg,kb,ka);
			};
			const float bowY=.28f,br=.10f;
			for(int i=0;i<8;i++){
				const float a0=(float)i*(6.2832f/8.f);
				const float rx=cosf(a0)*br,ry=bowY+sinf(a0)*br;
				kPut(rx,ry-.045f,ry+.045f,0.f,.045f,.045f);
			}
			kPut(0.f,bowY-.04f,bowY+.04f,0.f,.04f,.04f);
			kPut(0.f,-.02f,bowY-.06f,0.f,.045f,.045f); // 軸（下へ）
			kPut(.08f,-.02f,.08f,0.f,.06f,.04f); // 歯1
			kPut(.08f,-.10f,-.02f,0.f,.06f,.04f); // 歯2
			kPut(0.f,-.14f,-.06f,0.f,.04f,.04f); // 先端
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,ky+bowY*.5f,ocz,xl[i].d,0,0,0,FALSE,TRUE,0};
		}else if(S3mIsTrapCell(c)){
			if(nFx>=64)continue;
			const UINT b=nFloor+nWall+nTrans;
			float rr=.3f,gg=.85f,bb=.35f,a=.44f;
			if(c==CELL_SPIKE){rr=1.f;gg=.28f;bb=.22f;}
			else if(c==CELL_ICE){rr=.45f;gg=.85f;bb=1.f;}
			else if(c==CELL_DARK){rr=.25f;gg=.2f;bb=.45f;}
			const float pad=.08f;
			quad(x0+pad,passH+.03f,z0+pad,x0+pad,passH+.03f,z1-pad,x1-pad,passH+.03f,z1-pad,x1-pad,passH+.03f,z0+pad,0,1,0,rr,gg,bb,a);
			const float bob=.03f*sinf(m_anim*2.8f+x+z);
			const float ty=passH+.18f+bob;
			if(c==CELL_SPIKE){
				for(int si=0;si<5;si++){
					const float ox=((si%3)-1)*.12f,oz=((si/3)-.5f)*.14f;
					const float h=.16f+(si&1)*.10f;
					emitCone(ocx+ox,passH+.03f,ocz+oz,.055f,h,6,rr,gg,bb,a, .50f,.0f,1.f,.50f);
				}
			}else if(c==CELL_ICE){
				emitSphere(ocx,ty+.08f,ocz,.12f,.14f,.12f,7,4,rr,gg,bb,a, 0.f,.50f,.50f,1.f);
				emitSphere(ocx-.10f,ty+.02f,ocz,.06f,.10f,.06f,5,3,.7f,.95f,1.f,a, 0.f,.50f,.50f,1.f);
				emitSphere(ocx+.09f,ty+.00f,ocz,.05f,.09f,.05f,5,3,.7f,.95f,1.f,a, 0.f,.50f,.50f,1.f);
			}else if(c==CELL_DARK){
				emitSphere(ocx,ty+.10f,ocz,.12f,.16f,.12f,7,4,rr,gg,bb,a, .50f,.50f,1.f,1.f);
				emitSphere(ocx,ty+.22f,ocz,.07f,.10f,.07f,5,3,.15f,.1f,.35f,a, .50f,.50f,1.f,1.f);
			}else{ // slime
				emitSphere(ocx,ty+.02f,ocz,.16f,.07f,.14f,8,4,rr,gg,bb,a, 0.f,0.f,.50f,.50f);
				emitSphere(ocx,ty+.12f,ocz,.10f,.08f,.10f,6,3,rr*.85f,gg*.85f,bb*.85f,a, 0.f,0.f,.50f,.50f);
			}
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,passH+.25f,ocz,xl[i].d,0,0,0,FALSE,FALSE,1};
		}else if(nFx<64){
			// 自機マスと重なるスタート旗は書かない
			if(c==CELL_START&&x==plx&&z==plz&&!stairMove)continue;
			const UINT b=nFloor+nWall+nTrans;float cy,rr,gg,bb;
			if(c==CELL_GOAL){cy=.48f+.05f*sinf(m_anim*2.6f);rr=1.f;gg=.88f;bb=.25f;}
			else if(c==CELL_START){cy=.42f+.04f*sinf(m_anim*2.1f+1.f);rr=.30f;gg=1.f;bb=.55f;}
			else{cy=.40f+.07f*sinf(m_anim*2.4f+x*.7f+z);rr=1;gg=.5f;bb=.8f;
				if(c==CELL_TEMPO){rr=.3f;gg=1;bb=.5f;}
				else if(c==CELL_TEMPO_DN){rr=.12f;gg=.55f;bb=.28f;}
				else if(c==CELL_PITCH_UP){rr=1;gg=.65f;bb=.25f;}
				else if(c==CELL_PITCH_DN){rr=.35f;gg=.55f;bb=1;}
				else if(c==CELL_NEXT){rr=1;gg=.2f;bb=.35f;}
				else if(c==CELL_PREV){rr=.55f;gg=.08f;bb=.22f;}
				else if(c==CELL_VOL_UP){rr=1;gg=.92f;bb=.35f;}
				else if(c==CELL_VOL_DN){rr=.55f;gg=.62f;bb=.22f;}
				else if(c==CELL_EQ){rr=.7f;gg=.35f;bb=1;}
				else if(c==CELL_EQ_FLAT){rr=.62f;gg=.55f;bb=.78f;}
				else if(c==CELL_REVERB){rr=.2f;gg=.85f;bb=.95f;}
				else if(c==CELL_XFADE){rr=1;gg=.45f;bb=.75f;}
				else if(c==CELL_RANDOM){rr=.95f;gg=.55f;bb=.2f+0.55f*(0.5f+0.5f*sinf(m_anim*3.f+x));}
			}
			const float ka=1.15f; // アイテム／スタート／ゴール：平面反射スロット用の金属アルファ
			if(c!=CELL_START) emitSphere(ocx,passH+.22f+(cy-passH)*.35f,ocz,.11f,.13f,.11f,8,5,rr,gg,bb,ka,0.f,0.f,1.f,1.f);
			if(c==CELL_GOAL){
				passCubeAll(ocx-.22f,ocz-.22f,ocx+.22f,ocz+.22f,passH+.02f,passH+.10f,.55f,.45f,.22f,1.f);
				passCubeAll(ocx-.20f,ocz-.07f,ocx-.12f,ocz+.07f,passH+.10f,passH+.55f,.9f,.75f,.25f,1.05f);
				passCubeAll(ocx+.12f,ocz-.07f,ocx+.20f,ocz+.07f,passH+.10f,passH+.55f,.9f,.75f,.25f,1.05f);
				passCubeAll(ocx-.20f,ocz-.07f,ocx+.20f,ocz+.07f,passH+.50f,passH+.58f,1.f,.85f,.3f,1.05f);
				passCubeAll(ocx-.06f,ocz-.06f,ocx+.06f,ocz+.06f,passH+.10f,cy-.02f,.75f,.65f,.2f,ka);
				passCubeAll(ocx-.13f,ocz-.09f,ocx+.13f,ocz+.09f,cy-.02f,cy+.10f,rr,gg,bb,ka);
				passCubeAll(ocx-.09f,ocz-.09f,ocx-.03f,ocz+.09f,cy+.02f,cy+.16f,rr,gg,bb,ka);
				passCubeAll(ocx+.03f,ocz-.09f,ocx+.09f,ocz+.09f,cy+.02f,cy+.16f,rr,gg,bb,ka);
				passCubeAll(ocx-.07f,ocz-.07f,ocx+.07f,ocz+.07f,cy+.16f,cy+.22f,1.f,.95f,.4f,ka);
			}else if(c==CELL_START){
				// 旗：ポール縦＋布は縦面（寝かさない）
				passCubeAll(ocx-.04f,ocz-.04f,ocx+.04f,ocz+.04f,passH+.02f,cy+.32f,.55f,.5f,.45f,1.f);
				passCubeAll(ocx+.03f,ocz-.02f,ocx+.22f,ocz+.02f,cy+.12f,cy+.30f,rr,gg,bb,ka);
				passCubeAll(ocx+.03f,ocz-.02f,ocx+.16f,ocz+.02f,cy+.04f,cy+.12f,rr*.85f,gg*.85f,bb*.85f,ka);
				passCubeAll(ocx-.07f,ocz-.07f,ocx+.07f,ocz+.07f,passH+.02f,passH+.07f,.4f,.38f,.35f,1.f);
			}else{
				// アイテム：縦長シルエット（床に寝かさない）
				passCubeAll(ocx-.10f,ocz-.10f,ocx+.10f,ocz+.10f,passH+.02f,passH+.08f,.28f,.26f,.3f,1.f);
				if(c==CELL_TEMPO||c==CELL_TEMPO_DN){
					passCubeAll(ocx-.05f,ocz-.05f,ocx+.05f,ocz+.05f,passH+.08f,cy+.22f,rr,gg,bb,ka);
					const float tip=(c==CELL_TEMPO)?1.f:-1.f;
					const float ty=cy+.12f+tip*.10f;
					passCubeAll(ocx-.10f,ocz-.05f,ocx+.10f,ocz+.05f,ty-.04f,ty+.04f,rr,gg,bb,ka);
					passCubeAll(ocx-.06f,ocz-.05f,ocx+.06f,ocz+.05f,ty+tip*.04f,ty+tip*.12f,rr,gg,bb,ka);
				}else if(c==CELL_PITCH_UP||c==CELL_PITCH_DN){
					passCubeAll(ocx-.05f,ocz-.05f,ocx+.05f,ocz+.05f,passH+.08f,cy+.24f,rr,gg,bb,ka);
					const float uy=(c==CELL_PITCH_UP)?1.f:-1.f;
					passCubeAll(ocx-.11f,ocz-.05f,ocx+.11f,ocz+.05f,cy+.08f,cy+.14f,rr,gg,bb,ka);
					passCubeAll(ocx-.07f,ocz-.05f,ocx+.07f,ocz+.05f,cy+.14f,cy+.14f+uy*.10f,rr,gg,bb,ka);
					passCubeAll(ocx-.04f,ocz-.05f,ocx+.04f,ocz+.05f,cy+.14f+uy*.08f,cy+.14f+uy*.16f,rr,gg,bb,ka);
				}else if(c==CELL_NEXT||c==CELL_PREV){
					const float s=(c==CELL_NEXT)?1.f:-1.f;
					passCubeAll(ocx-.05f,ocz-.05f,ocx+.05f,ocz+.05f,passH+.08f,cy+.20f,rr,gg,bb,ka);
					passCubeAll(ocx-s*.02f,ocz-.06f,ocx+s*.12f,ocz+.06f,cy+.04f,cy+.16f,rr,gg,bb,ka);
					passCubeAll(ocx+s*.06f,ocz-.10f,ocx+s*.16f,ocz+.10f,cy+.02f,cy+.18f,rr,gg,bb,ka);
				}else if(c==CELL_VOL_UP||c==CELL_VOL_DN){
					passCubeAll(ocx-.08f,ocz-.05f,ocx-.01f,ocz+.05f,passH+.08f,cy+.20f,rr,gg,bb,ka);
					const int nArc=(c==CELL_VOL_UP)?3:1;
					for(int ai=0;ai<nArc;ai++){
						const float rad=.06f+ai*.05f;
						passCubeAll(ocx+rad*.15f,ocz-rad*.35f,ocx+rad*.15f+.05f,ocz+rad*.35f,
							cy+.02f+ai*.04f,cy+.14f+ai*.04f,rr,gg,bb,ka);
					}
				}else if(c==CELL_EQ||c==CELL_EQ_FLAT){
					for(int bi=0;bi<4;bi++){
						const float hx=-.13f+bi*.08f;
						const float h=(c==CELL_EQ_FLAT)?.18f:(.10f+((bi*3+x)&3)*.07f);
						passCubeAll(ocx+hx,ocz-.05f,ocx+hx+.06f,ocz+.05f,passH+.08f,passH+.08f+h,rr,gg,bb,ka);
					}
				}else if(c==CELL_REVERB){
					passCubeAll(ocx-.06f,ocz-.06f,ocx+.06f,ocz+.06f,passH+.08f,cy+.22f,rr,gg,bb,ka);
					passCubeAll(ocx-.12f,ocz-.12f,ocx+.12f,ocz+.12f,cy+.04f,cy+.10f,rr,gg,bb,.75f);
					passCubeAll(ocx-.16f,ocz-.16f,ocx+.16f,ocz+.16f,cy+.12f,cy+.16f,rr,gg,bb,.55f);
				}else if(c==CELL_XFADE){
					passCubeAll(ocx-.14f,ocz-.05f,ocx-.04f,ocz+.05f,passH+.08f,cy+.22f,rr,gg,bb,ka);
					passCubeAll(ocx+.04f,ocz-.05f,ocx+.14f,ocz+.05f,passH+.08f,cy+.22f,rr*.7f,gg*.7f,bb,ka);
					passCubeAll(ocx-.05f,ocz-.05f,ocx+.05f,ocz+.05f,cy+.06f,cy+.14f,1.f,1.f,1.f,ka);
				}else{ // RANDOM 他
					passCubeAll(ocx-.08f,ocz-.08f,ocx+.08f,ocz+.08f,passH+.08f,cy+.16f,rr,gg,bb,ka);
					passCubeAll(ocx-.05f,ocz-.05f,ocx+.05f,ocz+.05f,cy+.16f,cy+.28f,rr,gg,bb,ka);
					passCubeAll(ocx-.12f,ocz-.05f,ocx+.12f,ocz+.05f,cy+.08f,cy+.14f,rr,gg,bb,ka);
				}
			}
			fxObj[nFx++]={b,nFloor+nWall+nTrans-b,ocx,cy,ocz,xl[i].d,0,0,0,FALSE,TRUE,0};
		}
	}
	{int ord[64];for(int i=0;i<nFx;i++)ord[i]=i;
	// 反射が必要な窓・クリスタルを優先してRT割当（近い順）
	for(int i=1;i<nFx;i++){int q=ord[i],j=i-1;while(j>=0){
		const BOOL prefer=((int)fxObj[ord[j]].wantMir<(int)fxObj[q].wantMir)||(fxObj[ord[j]].wantMir==fxObj[q].wantMir&&fxObj[ord[j]].d>fxObj[q].d);
		if(!prefer)break;ord[j+1]=ord[j];j--;}ord[j+1]=q;}
	const int nUse=min((int)CS3mView::S3M_MIRROR_FX_N,nFx);
	for(int i=0;i<nUse;i++)fxMirOf[ord[i]]=CS3mView::S3M_MIRROR_FX0+i;}
	UINT plateBeg=nFloor+nWall+nTrans;
	// 訪問床：青の半透明板（鏡床の反射はソリッド床側）
	for(int z=iz0;z<=iz1;z++)for(int x=ix0;x<=ix1;x++){
		BYTE c=CellAtF(fxFloor,x,z);if(S3mIsSolidWall(c)||c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP||!vis(x,z))continue;
		float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z);
		if(VisitAtF(fxFloor,x,z))quad(x0,passH+.01f,z0,x0,passH+.01f,z1,x1,passH+.01f,z1,x1,passH+.01f,z0,0,1,0,.25f,.55f,1.f,.35f);
	}
	UINT nPlate=(nFloor+nWall+nTrans)-plateBeg;
	// ナビ導線（3D）：現在階の経路を1本の細長い金矢印メッシュとして一括生成
	UINT navBeg=nFloor+nWall+nTrans;UINT nNav=0;
	if(HasNavPath()){
		const float pulse=.55f+.45f*sinf(m_anim*3.2f);
		const float yBase=passH+.06f; // 地面から少し上
		const float halfW=.028f;   // 軸の半幅（細め）
		const float halfH=.022f;
		const float tipLen=.55f;
		const float tipHalf=.14f;
		float gr=.95f,gg=.72f,gb=.12f;
		{
			const S3mNavPt& tip=m_navPath[m_navCount-1];
			if(CellAtF(tip.f,tip.x,tip.z)==CELL_KEY){gr=.72f;gg=.98f;gb=.22f;}
		}
		const float aBody=.88f+.10f*pulse;
		auto navQuad=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float rr,float gg2,float bb,float a){
			quadUV(x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3,nx,ny,nz,0,0,1,1,rr,gg2,bb,a);
		};
		auto emitShaftRing=[&](float ax,float ay,float az,float bx,float by,float bz,float tx,float tz,float hx,float hz){
			// 断面ひし形を進行方向に押し出し（上面・下面・側面）
			float aL[3]={ax-tx*halfW,ay,az-tz*halfW},aR[3]={ax+tx*halfW,ay,az+tz*halfW};
			float aU[3]={ax,ay+halfH,az},aD[3]={ax,ay-halfH,az};
			float bL[3]={bx-tx*halfW,by,bz-tz*halfW},bR[3]={bx+tx*halfW,by,bz+tz*halfW};
			float bU[3]={bx,by+halfH,bz},bD[3]={bx,by-halfH,bz};
			navQuad(aL[0],aU[1],aL[2],aR[0],aU[1],aR[2],bR[0],bU[1],bR[2],bL[0],bU[1],bL[2],0,1,0,gr,gg,gb,aBody);
			navQuad(aL[0],aD[1],aL[2],bL[0],bD[1],bL[2],bR[0],bD[1],bR[2],aR[0],aD[1],aR[2],0,-1,0,gr*.85f,gg*.85f,gb,aBody);
			navQuad(aL[0],aU[1],aL[2],bL[0],bU[1],bL[2],bL[0],bD[1],bL[2],aL[0],aD[1],aL[2],-tx,0,-tz,gr*.9f,gg*.9f,gb,aBody);
			navQuad(aR[0],aU[1],aR[2],aR[0],aD[1],aR[2],bR[0],bD[1],bR[2],bR[0],bU[1],bR[2],tx,0,tz,gr*.9f,gg*.9f,gb,aBody);
			(void)hx;(void)hz;
		};
		auto emitTip=[&](float tipX,float tipY,float tipZ,float dx,float dz){
			float len=sqrtf(dx*dx+dz*dz)+1e-5f;dx/=len;dz/=len;
			float px=-dz,pz=dx;
			float baseX=tipX-dx*tipLen,baseZ=tipZ-dz*tipLen;
			float lX=baseX+px*tipHalf,lZ=baseZ+pz*tipHalf;
			float rX=baseX-px*tipHalf,rZ=baseZ-pz*tipHalf;
			const float aTip=.95f;
			// 水平・垂直の両面＋側面で立体矢じり
			tri(tipX,tipY,tipZ,lX,tipY,lZ,rX,tipY,rZ,0,1,0,0.f,0.f,1.f,0.f,.5f,1.f,gr,gg,gb,aTip);
			tri(tipX,tipY,tipZ,rX,tipY,rZ,lX,tipY,lZ,0,-1,0,0.f,0.f,1.f,0.f,.5f,1.f,gr*.9f,gg*.9f,gb,aTip);
			tri(tipX,tipY,tipZ,baseX,tipY+tipHalf*.7f,baseZ,baseX,tipY-tipHalf*.7f,baseZ,px,0,pz,0.f,0.f,1.f,0.f,.5f,1.f,1.f,.82f,.2f,aTip);
			tri(tipX,tipY,tipZ,baseX,tipY-tipHalf*.7f,baseZ,baseX,tipY+tipHalf*.7f,baseZ,-px,0,-pz,0.f,0.f,1.f,0.f,.5f,1.f,1.f,.82f,.2f,aTip);
			tri(tipX,tipY,tipZ,lX,tipY,lZ,baseX,tipY+tipHalf*.55f,baseZ,0,1,0,0.f,0.f,1.f,0.f,.5f,1.f,gr,gg*.95f,gb,aTip*.95f);
			tri(tipX,tipY,tipZ,baseX,tipY+tipHalf*.55f,baseZ,rX,tipY,rZ,0,1,0,0.f,0.f,1.f,0.f,.5f,1.f,gr,gg*.95f,gb,aTip*.95f);
		};
		int run0=-1;
		auto flushRun=[&](int a,int b){
			if(a<0||b<=a)return;
			// セル中心列 → 軽くスムーズした連続中心線（1本の矢）
			float rawX[64],rawZ[64];int nRaw=0;
			for(int i=a;i<=b&&nRaw<64;i++){
				rawX[nRaw]=GridToWorldX((float)m_navPath[i].x+.5f);
				rawZ[nRaw]=GridToWorldZ((float)m_navPath[i].z+.5f);
				nRaw++;
			}
			if(nRaw<2)return;
			const int maxSamp=96;
			float sx[96],sy[96],sz[96];int ns=0;
			auto sampleAt=[&](float t,float& ox,float& oy,float& oz){
				// t in [0,1] along polyline with slight aerial wave as one ribbon
				const float total=(float)(nRaw-1);
				float u=t*total;int i0=(int)floorf(u);if(i0>=nRaw-1)i0=nRaw-2;if(i0<0)i0=0;
				float f=u-(float)i0;
				float x=rawX[i0]+(rawX[i0+1]-rawX[i0])*f;
				float z=rawZ[i0]+(rawZ[i0+1]-rawZ[i0])*f;
				// 角を少し丸める（前後平均）
				if(i0>0&&i0+1<nRaw-1&&f>.2f&&f<.8f){
					float xm=(rawX[i0]+rawX[i0+1])*.5f,zm=(rawZ[i0]+rawZ[i0+1])*.5f;
					x=x*.65f+xm*.35f;z=z*.65f+zm*.35f;
				}
				float wave=.012f*sinf(t*5.5f+m_anim*1.4f);
				ox=x;oz=z;oy=yBase+wave;
			};
			const int nSeg=min(maxSamp-1,max(8,(nRaw-1)*3));
			for(int i=0;i<=nSeg&&ns<maxSamp;i++){
				float t=(float)i/(float)nSeg;
				sampleAt(t,sx[ns],sy[ns],sz[ns]);
				ns++;
			}
			if(ns<2)return;
			// 軸：終端手前まで押し出し（矢じり分を残す）
			float tipCut=tipLen*.92f;
			float acc=0.f;
			for(int i=ns-1;i>0;i--){
				float d=sqrtf((sx[i]-sx[i-1])*(sx[i]-sx[i-1])+(sz[i]-sz[i-1])*(sz[i]-sz[i-1]));
				acc+=d;
				if(acc>=tipCut){
					float over=acc-tipCut;float seg=d+1e-5f;float u=over/seg;
					sx[i]=sx[i]+(sx[i-1]-sx[i])*u;
					sz[i]=sz[i]+(sz[i-1]-sz[i])*u;
					sy[i]=sy[i]+(sy[i-1]-sy[i])*u;
					ns=i+1;
					break;
				}
			}
			for(int i=0;i+1<ns;i++){
				float dx=sx[i+1]-sx[i],dz=sz[i+1]-sz[i];
				float len=sqrtf(dx*dx+dz*dz)+1e-5f;dx/=len;dz/=len;
				float tx=-dz,tz=dx;
				emitShaftRing(sx[i],sy[i],sz[i],sx[i+1],sy[i+1],sz[i+1],tx,tz,0,0);
			}
			// 終端の大きな矢じり1つ
			{
				float dx=sx[ns-1]-sx[max(0,ns-2)],dz=sz[ns-1]-sz[max(0,ns-2)];
				if(ns>=2){dx=rawX[nRaw-1]-sx[ns-1];dz=rawZ[nRaw-1]-sz[ns-1];}
				float tipX=rawX[nRaw-1],tipZ=rawZ[nRaw-1],tipY=yBase+.01f+.01f*pulse;
				if(fabsf(dx)+fabsf(dz)<1e-4f&&ns>=2){dx=sx[ns-1]-sx[ns-2];dz=sz[ns-1]-sz[ns-2];}
				emitTip(tipX,tipY,tipZ,dx,dz);
			}
		};
		for(int i=0;i<m_navCount;i++){
			if(m_navPath[i].f==fxFloor){
				if(run0<0)run0=i;
			}else if(run0>=0){
				flushRun(run0,i-1);
				run0=-1;
			}
		}
		if(run0>=0)flushRun(run0,m_navCount-1);
		nNav=(nFloor+nWall+nTrans)-navBeg;
	}
	// 環境エフェクト：壁＋階層別の天井／空中（雲・水滴等）＋鏡床への吸い込み
	UINT vfxBeg=nFloor+nWall+nTrans;UINT nVfxAlpha=0;
	{
		const int thFx=ThemeOfFloor(fxFloor);
		auto frac01=[&](float t)->float{return t-floorf(t);};
		auto emitBill=[&](float px,float py,float pz,float hs,float vs,float rr,float gg,float bb,float a){
			float qx=px-rx*hs,qz=pz-rz*hs,sx=px+rx*hs,sz=pz+rz*hs;
			quad(qx,py-vs,qz,sx,py-vs,sz,sx,py+vs,sz,qx,py+vs,qz,fx,0,fz,rr,gg,bb,a);
		};
		auto emitStreak=[&](float px,float py0,float pz,float py1,float hw,float rr,float gg,float bb,float a){
			float qx=px-rx*hw,qz=pz-rz*hw,sx=px+rx*hw,sz=pz+rz*hw;
			quad(qx,py0,qz,sx,py0,sz,sx,py1,sz,qx,py1,qz,fx,0,fz,rr,gg,bb,a);
		};
		auto emitPuddle=[&](float px,float pz,float rad,float rr,float gg,float bb,float a){
			const float y=passH+.025f;
			quad(px-rad,y,pz-rad,px-rad,y,pz+rad,px+rad,y,pz+rad,px+rad,y,pz-rad,0,1,0,rr,gg,bb,a);
		};
		auto emitMirrorAbsorb=[&](float px,float pz,float s){
			// 着水→鏡に吸い込まれるリング（明るいシアン）
			const float y=passH+.03f;
			const float rad=.04f+s*.16f;
			const float a=.55f*(1.f-s);
			emitPuddle(px,pz,rad,.55f,.9f,1.f,a*.45f);
			emitPuddle(px,pz,rad*.55f,.85f,.95f,1.f,a*.7f);
			emitBill(px,y+.02f+s*.08f,pz,.03f*(1.f-s),.02f,.7f,.95f,1.f,a);
		};
		int nEmit=0;const int kMaxEmit=72;
		// --- 壁際エフェクト（従来） ---
		for(int z=iz0;z<=iz1&&nEmit<kMaxEmit;z++)for(int x=ix0;x<=ix1&&nEmit<kMaxEmit;x++){
			if(!vis(x,z))continue;
			BYTE c=CellAtF(fxFloor,x,z);if(c!=CELL_WALL&&c!=CELL_MIRROR_WALL)continue;
			float x0=cellX0(x),x1=x0+cellW(x),z0=cellZ0(z),z1=z0+cellD(z),cx=cellCX(x),cz=cellCZ(z);
			float dx=cx-ex,dz=cz-ez;if(dx*dx+dz*dz>14.f*14.f)continue;
			auto openN=[&](int ox,int oz)->BOOL{
				if(ox<0||oz<0||ox>=m_n||oz>=m_n)return FALSE;
				BYTE oc=CellAtF(fxFloor,ox,oz);return S3mIsSolidWall(oc)?FALSE:TRUE;
			};
			auto sideDrip=[&](float nx,float nz,float fx0,float fz0,float fx1,float fz1){
				const int seed=x*47+z*13+(int)(nx*3+nz*5)+thFx*91;
				const int nDrop=1+((seed>>3)&1);
				for(int k=0;k<nDrop&&nEmit<kMaxEmit;k++){
					float u=((seed+k*37)&255)/255.f;
					float px=fx0+(fx1-fx0)*(0.15f+0.7f*u),pz=fz0+(fz1-fz0)*(0.15f+0.7f*u);
					px+=nx*.02f;pz+=nz*.02f;
					float ph=frac01(m_anim*(thFx==3?0.55f:0.85f)+((seed+k*19)&255)/255.f);
					if(thFx==0||thFx==1){
						float yTop=wallH*(0.55f+((seed+k)&7)*.04f);
						float y=yTop+(passH+.02f-yTop)*ph;
						float len=.035f+.05f*ph;
						float a=.55f*(1.f-ph*.35f);
						if(thFx==0)emitStreak(px,y,pz,y-len,.007f,.55f,.78f,.95f,a);
						else emitStreak(px,y,pz,y-len,.008f,.45f,.75f,.70f,a);
						if(ph>.88f){
							float s=(ph-.88f)/.12f;
							emitPuddle(px+nx*.04f,pz+nz*.04f,.02f+s*.10f,.40f,.62f,.78f,.28f*(1.f-s));
						}
						if(((seed+k)&7)==0){
							float burst=frac01(m_anim*1.4f+u);
							float by=wallH*(.35f+((seed)&3)*.1f);
							emitBill(px+nx*(.01f+burst*.06f),by,pz+nz*(.01f+burst*.06f),.012f,.01f,.6f,.82f,.95f,.35f*(1.f-burst));
						}
					}else if(thFx==2){
						float yTop=wallH*(0.3f+((seed+k)&5)*.08f);
						float y=yTop-ph*wallH*.55f;
						emitBill(px+nx*.03f,y,pz+nz*.03f,.012f,.012f,1.f,.72f,.28f,.55f*(1.f-ph));
						if(((seed+k)&3)==0){
							float sy=wallH*(.4f+((seed)&3)*.12f)+ph*.15f;
							emitBill(px+nx*.05f,sy,pz+nz*.05f,.04f,.025f,.55f,.58f,.62f,.18f*(1.f-ph));
						}
					}else{
						float y=passH+.05f+ph*(wallH*.9f);
						float flicker=.7f+.3f*sinf(m_anim*9.f+seed+k);
						emitBill(px+nx*.04f,y,pz+nz*.04f,.01f,.016f,1.f,.45f*flicker,.12f,.5f*(1.f-ph*.6f));
					}
					nEmit++;
				}
			};
			if(openN(x-1,z))sideDrip(-1,0,x0,z0,x0,z1);
			if(openN(x+1,z))sideDrip(1,0,x1,z1,x1,z0);
			if(openN(x,z-1))sideDrip(0,-1,x1,z0,x0,z0);
			if(openN(x,z+1))sideDrip(0,1,x0,z1,x1,z1);
		}
		// --- 天井／空中エフェクト（階層テーマ別） ---
		for(int z=iz0;z<=iz1&&nEmit<kMaxEmit;z++)for(int x=ix0;x<=ix1&&nEmit<kMaxEmit;x++){
			if(!vis(x,z))continue;
			BYTE c=CellAtF(fxFloor,x,z);
			if(S3mIsSolidWall(c)||c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP)continue;
			float cx=cellCX(x),cz=cellCZ(z);
			float dx=cx-ex,dz=cz-ez;if(dx*dx+dz*dz>16.f*16.f)continue;
			const int seed=x*73+z*31+thFx*101+fxFloor*17;
			const BOOL onMirror=(c==CELL_MIRROR_FLOOR);
			if(thFx==0){
				// 地上：漂う雲（空中）— 鏡床が映し込む
				const int nCloud=1+((seed>>2)&1);
				for(int k=0;k<nCloud&&nEmit<kMaxEmit;k++){
					float u=((seed+k*41)&255)/255.f,v=((seed*3+k*17)&255)/255.f;
					float drift=frac01(m_anim*.08f+u);
					float px=cx+(u-.5f)*AxisSpan(x)*1.4f+sinf(m_anim*.35f+v)*0.35f;
					float pz=cz+(v-.5f)*AxisSpan(z)*1.4f+cosf(m_anim*.28f+u)*0.35f;
					float py=wallH*(0.85f+0.45f*v)+sinf(m_anim*.5f+u*6.f)*.08f;
					float hs=.22f+.18f*((seed+k)&3)/3.f;
					float vs=.08f+.05f*v;
					float a=.18f+.14f*sinf(drift*(float)M_PI);
					emitBill(px,py,pz,hs,vs,.92f,.94f,1.f,a);
					emitBill(px+hs*.3f,py+.04f,pz-hs*.2f,hs*.7f,vs*.8f,.88f,.91f,.98f,a*.75f);
					nEmit++;
				}
			}else{
				// 地下：天井からの滴下（テーマで見た目変更）→ 鏡床なら吸い込み
				const int nDrop=1+((seed>>1)&1)+(thFx>=2?1:0);
				for(int k=0;k<nDrop&&nEmit<kMaxEmit;k++){
					float u=((seed+k*53)&255)/255.f,v=((seed*5+k*11)&255)/255.f;
					float speed=(thFx==3)?0.7f:((thFx==2)?0.95f:1.15f);
					float ph=frac01(m_anim*speed*((.55f+.45f*u))+v);
					float px=cx+(u-.5f)*AxisSpan(x)*.7f;
					float pz=cz+(v-.5f)*AxisSpan(z)*.7f;
					float yTop=wallH-.02f;
					float yBot=passH+.03f;
					float y=yTop+(yBot-yTop)*ph;
					if(thFx==1){
						// 湿った石：水色の水滴
						float len=.04f+.06f*ph;
						emitStreak(px,y,pz,y-len,.008f,.5f,.82f,.78f,.6f*(1.f-ph*.3f));
						if(ph>.86f){
							float s=(ph-.86f)/.14f;
							if(onMirror)emitMirrorAbsorb(px,pz,s);
							else emitPuddle(px,pz,.025f+s*.09f,.42f,.68f,.72f,.3f*(1.f-s));
						}
					}else if(thFx==2){
						// 金属：結露＋油滴
						float len=.03f+.05f*ph;
						emitStreak(px,y,pz,y-len,.007f,.7f,.75f,.55f,.5f*(1.f-ph*.4f));
						if(((seed+k)&5)==0)
							emitBill(px,yTop-ph*wallH*.25f,pz,.035f,.02f,.6f,.62f,.58f,.2f*(1.f-ph));
						if(ph>.88f){
							float s=(ph-.88f)/.12f;
							if(onMirror)emitMirrorAbsorb(px,pz,s);
							else emitPuddle(px,pz,.02f+s*.07f,.55f,.5f,.35f,.25f*(1.f-s));
						}
					}else{
						// 火山：天井割れ目から落ちる赤熱のしずく／灰
						float flicker=.65f+.35f*sinf(m_anim*11.f+seed+k);
						emitBill(px,y,pz,.01f,.018f,1.f,.4f*flicker,.1f,.55f*(1.f-ph*.5f));
						if(ph>.9f){
							float s=(ph-.9f)/.1f;
							if(onMirror)emitMirrorAbsorb(px,pz,s);
							else emitPuddle(px,pz,.015f+s*.05f,.9f,.35f,.1f,.3f*(1.f-s));
						}
					}
					nEmit++;
				}
			}
			// 鏡床上の常時ハイライト（雲／滴の映り込みを強調）
			if(onMirror&&nEmit<kMaxEmit){
				float shimmer=.5f+.5f*sinf(m_anim*3.2f+(float)(x+z));
				emitPuddle(cx,cz,.12f+.04f*shimmer,.75f,.92f,1.f,.12f+.1f*shimmer);
				nEmit++;
			}
		}
		nVfxAlpha=(nFloor+nWall+nTrans)-vfxBeg;
	}
	{
		const UINT used=nFloor+nWall+nTrans;
		const UINT needBytes=used*sizeof(S3MVertex);
		UINT gpuBytes=m_view.m_vbDynBytes;
		if(!m_view.m_vbDyn||gpuBytes<needBytes){
			gpuBytes=m_view.m_cpuDynScratchBytes;
			if(gpuBytes<needBytes)gpuBytes=needBytes;
			// 1MB 単位に切り上げ（再生成回数を抑える）
			gpuBytes=(gpuBytes+0xFFFFFu)&~0xFFFFFu;
			if(gpuBytes<needBytes)gpuBytes=needBytes;
			S3M_RELEASE(m_view.m_vbDyn);
			D3D11_BUFFER_DESC bd={};
			bd.ByteWidth=gpuBytes;
			bd.Usage=D3D11_USAGE_DYNAMIC;
			bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
			if(FAILED(m_view.m_dev->CreateBuffer(&bd,NULL,&m_view.m_vbDyn)))return;
			m_view.m_vbDynBytes=gpuBytes;
		}
	}
	if(FAILED(dc->Map(m_view.m_vbDyn,0,D3D11_MAP_WRITE_DISCARD,0,&map)))return;memcpy(map.pData,v,(nFloor+nWall+nTrans)*sizeof(S3MVertex));dc->Unmap(m_view.m_vbDyn,0);
	UINT stride=sizeof(S3MVertex),off=0;ID3D11ShaderResourceView* ns[7]={NULL,NULL,NULL,NULL,NULL,NULL,NULL};ID3D11RenderTargetView* nullRtv=NULL;
	ID3D11ShaderResourceView* envUse=m_view.m_srvEnv;
	{const int thE=ThemeOfFloor((m_floorFx==FLOORFX_IN)?m_stairFrom:m_floor); if(thE>=1&&m_view.m_srvEnvIn) envUse=m_view.m_srvEnvIn;}
	auto bindCB=[&](){dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->HSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->DSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);};
	auto drawFloorWall=[&](BOOL colorPass){
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);dc->DSSetSamplers(0,1,&m_view.m_sampLin);dc->OMSetDepthStencilState(m_view.m_dssWrite,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);
		if(colorPass){dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);dc->PSSetShaderResources(3,1,&envUse);}
		for(int li=0;li<nLay;li++){
			const Layer& L=layers[li];
			dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
			dc->PSSetShaderResources(0,1,&m_view.m_srvFloor[L.th]);
			if(colorPass){ID3D11ShaderResourceView* fd=m_view.m_srvFloorD[L.th]?m_view.m_srvFloorD[L.th]:m_view.m_srvFloor[L.th];dc->PSSetShaderResources(1,1,&fd);}
			if(L.nF)dc->Draw(L.nF,L.fBeg);
			if(L.nMF&&m_view.m_srvMirrorFloor){dc->PSSetShaderResources(0,1,&m_view.m_srvMirrorFloor);dc->Draw(L.nMF,L.mfBeg);}
			dc->IASetInputLayout(m_view.m_ilPatch);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
			dc->VSSetShader(m_view.m_vsTess,NULL,0);dc->HSSetShader(m_view.m_hsTess,NULL,0);dc->DSSetShader(m_view.m_dsTess,NULL,0);dc->PSSetShader(colorPass?m_view.m_psWall:NULL,NULL,0);
			dc->DSSetShaderResources(0,1,&m_view.m_srvBrick[L.th]);dc->PSSetShaderResources(0,1,&m_view.m_srvBrick[L.th]);
			if(colorPass){ID3D11ShaderResourceView* bd=m_view.m_srvBrickD[L.th]?m_view.m_srvBrickD[L.th]:m_view.m_srvBrick[L.th];if(L.th==0&&m_view.m_srvBrick2)bd=m_view.m_srvBrick2;dc->PSSetShaderResources(1,1,&bd);}
			if(L.nW)dc->Draw(L.nW,L.wBeg);
			if(L.nMW&&m_view.m_srvMirrorWall){
				dc->DSSetShaderResources(0,1,&m_view.m_srvMirrorWall);dc->PSSetShaderResources(0,1,&m_view.m_srvMirrorWall);
				dc->Draw(L.nMW,L.mwBeg);
			}
		}
		dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->DSSetShaderResources(0,1,ns);dc->PSSetShaderResources(0,5,ns);
	};
	auto drawTrans=[&](BOOL colorPass){
		if(!nTrans)return;
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		if(colorPass){
			dc->OMSetDepthStencilState(m_view.m_dssRead,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
			dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&envUse);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);
		}else{
			// シャドウマップ：半透明も深度書き込み（キャスタとして全部）
			dc->OMSetDepthStencilState(m_view.m_dssWrite,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);
		}
		dc->Draw(nTrans,transBeg);dc->PSSetShaderResources(0,5,ns);
	};
	auto bindProp=[&](int kind){
		ID3D11ShaderResourceView* t0=m_view.m_srvItem?m_view.m_srvItem:ns[0];
		ID3D11ShaderResourceView* t1=m_view.m_srvGimmick?m_view.m_srvGimmick:t0;
		if(kind==1){ if(m_view.m_srvGimmick) t0=m_view.m_srvGimmick; if(m_view.m_srvItem) t1=m_view.m_srvItem; }
		else if(kind==2){ if(m_view.m_srvGlass) t0=m_view.m_srvGlass; if(m_view.m_srvItem) t1=m_view.m_srvItem; }
		else if(kind==3){ if(m_view.m_srvBrick2){ t0=m_view.m_srvBrick2; t1=m_view.m_srvBrick2; } }
		else if(kind==4){ t0=ns[0]; t1=ns[0]; }
		dc->PSSetShaderResources(0,1,&t0); dc->PSSetShaderResources(1,1,&t1);
	};
	auto drawTransRange=[&](UINT beg,UINT n,BOOL colorPass,int kind){
		if(!n)return;
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(colorPass?m_view.m_psSolid:NULL,NULL,0);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		if(colorPass){dc->OMSetDepthStencilState(m_view.m_dssRead,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&envUse);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);bindProp(kind);}
		dc->Draw(n,beg);
	};
	S3MMat fxRefVP[CS3mView::S3M_MIRROR_FX_N];
	// --- 1パス目: シャドウマップ（不透明＋半透明すべて深度描画）---
	{S3MMat camVP=cb.viewProj;
	// Eyeはカメラのまま（変位量を色パスと一致）。LightDir.w=0 でテッセだけ固定細密
	cb.viewProj=cb.lightVP;cb.lightDir.w=0.f;
	if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
	D3D11_VIEWPORT svp={0,0,(float)CS3mView::S3M_SHADOW_SIZE,(float)CS3mView::S3M_SHADOW_SIZE,0,1};dc->RSSetViewports(1,&svp);dc->RSSetState(m_view.m_rsShadow);
	dc->OMSetRenderTargets(1,&nullRtv,m_view.m_shadowDsv);dc->ClearDepthStencilView(m_view.m_shadowDsv,D3D11_CLEAR_DEPTH,1.f,0);
	drawFloorWall(FALSE);
	drawTrans(FALSE);
	dc->OMSetRenderTargets(1,&nullRtv,NULL);cb.viewProj=camVP;cb.lightDir.w=1.f;
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
		float mbg[4]={.42f,.58f,.78f,1};if(fxFloor>0){mbg[0]=.06f;mbg[1]=.07f;mbg[2]=.09f;}
		D3D11_VIEWPORT mvp={0,0,(float)m_view.m_mirrorSize,(float)m_view.m_mirrorSize,0,1};
		dc->RSSetViewports(1,&mvp);dc->RSSetState(m_view.m_rsSolid);
		dc->OMSetRenderTargets(1,&m_view.m_mirrorRtv[slot],m_view.m_mirrorDsv);
		dc->ClearRenderTargetView(m_view.m_mirrorRtv[slot],mbg);dc->ClearDepthStencilView(m_view.m_mirrorDsv,D3D11_CLEAR_DEPTH,1.f,0);
		if(!ok||!m_view.m_mirrorRtv[slot])return;
		const S3MMat rVP=makeReflectVP(pick,fovMul);
		if(slot==0)cb.reflectVP=rVP;else if(slot==1)cb.reflectFloorVP=rVP;
		if(storeVP)*storeVP=rVP;
		cb.viewProj=rVP;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		drawFloorWall(TRUE);
		// 鏡床反射に天井／雲／水滴を焼き込む
		if(slot==1&&nVfxAlpha){
			const int thV=ThemeOfFloor(fxFloor);
			dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
			dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(m_view.m_psSolid,NULL,0);
			dc->PSSetSamplers(0,1,&m_view.m_sampLin);dc->OMSetDepthStencilState(m_view.m_dssRead,0);
			dc->OMSetBlendState(thV>=2?m_view.m_bsAdd:m_view.m_bsAlpha,NULL,~0u);
			dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&envUse);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);
			dc->PSSetShaderResources(5,2,ns+5);dc->PSSetShaderResources(6,1,ns+6);
			dc->Draw(nVfxAlpha,vfxBeg);
			dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);
		}
		dc->PSSetShaderResources(0,7,ns);
	};
	drawMirrorSlot(0,bestW,bestW.score>0.f,NULL,1.f);drawMirrorSlot(1,bestF,bestF.score>0.f,NULL,1.f);
	for(int i=0;i<nFx;i++){
		const int slot=fxMirOf[i];if(slot<0)continue;
		MirPick mp;
		if(fxObj[i].plane){
			mp={1.f,fxObj[i].nx,fxObj[i].ny,fxObj[i].nz,fxObj[i].cx,fxObj[i].cy,fxObj[i].cz};
		}else{
			// 回転ギミック：カメラ向き平面で反射（面がカメラを向いているように見える）
			float nx=ex-fxObj[i].cx,ny=eyeY-fxObj[i].cy,nz=ez-fxObj[i].cz;float nl=sqrtf(nx*nx+ny*ny+nz*nz)+1e-5f;nx/=nl;ny/=nl;nz/=nl;
			mp={1.f,nx,ny,nz,fxObj[i].cx,fxObj[i].cy,fxObj[i].cz};
		}
		drawMirrorSlot(slot,mp,TRUE,&fxRefVP[slot-CS3mView::S3M_MIRROR_FX0],fxObj[i].plane?1.15f:1.35f);
	}
	dc->OMSetRenderTargets(1,&nullRtv,NULL);cb.viewProj=camVP;cb.lightDir.w=1.f;
	cb.reflectVP=(bestW.score>0.f)?cb.reflectVP:idM;
	cb.reflectFloorVP=(bestF.score>0.f)?cb.reflectFloorVP:idM;
	if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}}
	D3D11_VIEWPORT vp={0,0,(float)w,(float)h,0,1};dc->RSSetViewports(1,&vp);dc->RSSetState(m_view.m_rsSolid);
	// 地下は青空を出さない（天井＋暗いクリア）
	const int clearF=stairMove?((m_stairCamY<0.f)?max(m_stairFrom,m_stairTo):m_stairFrom):m_floor;
	float bg[4]={.48f,.64f,.82f,1};if(clearF>0){bg[0]=.07f;bg[1]=.08f;bg[2]=.10f;}
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,m_view.m_dsv);dc->ClearRenderTargetView(m_view.m_sceneRtv,bg);dc->ClearDepthStencilView(m_view.m_dsv,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
	dc->PSSetShaderResources(5,1,&m_view.m_mirrorSrv[0]);dc->PSSetShaderResources(6,1,&m_view.m_mirrorSrv[1]);
	// --- 2パス目: 不透明（PCFでセルフシャドウ／投射影）---
	drawFloorWall(TRUE);
	// ナビをシーンRTへ先に焼いておく（ポスト後パスが落ちても見える）
	if(nNav){
		dc->RSSetState(m_view.m_rsNoCull);
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
		dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(m_view.m_psSolid,NULL,0);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);dc->OMSetDepthStencilState(m_view.m_dssRead,0);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
		dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&envUse);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);
		dc->PSSetShaderResources(5,2,ns+5);dc->PSSetShaderResources(6,1,ns+6);
		dc->Draw(nNav,navBeg);
		dc->RSSetState(m_view.m_rsSolid);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);
		dc->PSSetShaderResources(0,7,ns);
	}
	dc->PSSetShaderResources(5,2,ns+5);
	dc->OMSetDepthStencilState(m_view.m_dssOff,0);dc->OMSetBlendState(m_view.m_bsOpaque,NULL,~0u);dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetSamplers(1,1,&m_view.m_sampPoint);
	dc->OMSetRenderTargets(1,&m_view.m_postRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_sceneSrv);dc->PSSetShaderResources(2,1,&m_view.m_dsSrv);dc->PSSetShader(m_view.m_psSsr,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_postSrv);dc->PSSetShaderResources(2,1,&m_view.m_dsSrv);dc->PSSetShader(m_view.m_psDof,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);dc->PSSetShaderResources(0,1,&m_view.m_sceneSrv);dc->PSSetShader(m_view.m_psFinal,NULL,0);dc->Draw(3,0);dc->PSSetShaderResources(0,5,ns);
	if(nTrans){
		// --- 3パス目: 半透明（PCFでセルフシャドウ／投射影）---
		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,m_view.m_dsv);
		S3MMat floorVP=cb.reflectFloorVP;
		// 奥から手前へ1回だけ（二重描画しない）。窓・アイテムに個別RTを割当
		int ordDraw[64];for(int i=0;i<nFx;i++)ordDraw[i]=i;
		for(int i=1;i<nFx;i++){int q=ordDraw[i],j=i-1;while(j>=0&&fxObj[ordDraw[j]].d<fxObj[q].d){ordDraw[j+1]=ordDraw[j];j--;}ordDraw[j+1]=q;}
		for(int oi=0;oi<nFx;oi++){
			const int i=ordDraw[oi];
			const int slot=fxMirOf[i];
			if(slot>=0){
				cb.reflectFloorVP=fxRefVP[slot-CS3mView::S3M_MIRROR_FX0];
				cb.lightDir.w=1.f;
				if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
				dc->PSSetShaderResources(6,1,&m_view.m_mirrorSrv[slot]);
			}else{
				dc->PSSetShaderResources(6,1,ns+6);
			}
			drawTransRange(fxObj[i].beg,fxObj[i].n,TRUE,fxObj[i].texKind);
		}
		cb.reflectFloorVP=floorVP;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->PSSetShaderResources(5,1,&m_view.m_mirrorSrv[0]);dc->PSSetShaderResources(6,1,&m_view.m_mirrorSrv[1]);
		drawTransRange(plateBeg,nPlate,TRUE,4);
		if(nVfxAlpha){
			const int thV=ThemeOfFloor(fxFloor);
			dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);bindCB();
			dc->IASetInputLayout(m_view.m_ilSolid);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsSolid,NULL,0);dc->HSSetShader(NULL,NULL,0);dc->DSSetShader(NULL,NULL,0);dc->PSSetShader(m_view.m_psSolid,NULL,0);
			dc->PSSetSamplers(0,1,&m_view.m_sampLin);dc->OMSetDepthStencilState(m_view.m_dssRead,0);
			dc->OMSetBlendState(thV>=2?m_view.m_bsAdd:m_view.m_bsAlpha,NULL,~0u);
			dc->PSSetSamplers(2,1,&m_view.m_sampCmp);dc->PSSetShaderResources(3,1,&envUse);dc->PSSetShaderResources(4,1,&m_view.m_shadowSrv);
			dc->PSSetShaderResources(6,1,ns+6);
			dc->Draw(nVfxAlpha,vfxBeg);
			dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
		}
		dc->PSSetShaderResources(5,2,ns+5);
	}
	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);dc->OMSetDepthStencilState(m_view.m_dssOff,0);
	dc->RSSetState(m_view.m_rsNoCull); // HUDは画面空間CCWのためカリングなし
	const UINT maxH=m_view.m_vbHudBytes/sizeof(S3MHudVertex);
	const UINT needHudBytes=maxH*sizeof(S3MHudVertex);
	if(m_view.m_cpuHudScratchBytes<needHudBytes){
		delete[] m_view.m_cpuHudScratch;
		m_view.m_cpuHudScratch=new (std::nothrow) BYTE[needHudBytes];
		m_view.m_cpuHudScratchBytes=m_view.m_cpuHudScratch?needHudBytes:0;
	}
	S3MHudVertex* hv=m_view.m_cpuHudScratch?(S3MHudVertex*)m_view.m_cpuHudScratch:NULL;
	if(!hv){m_view.PresentFrame();return;}
	UINT hn=0;
	auto hp=[&](float px,float py,float r,float g,float b,float a){if(hn<maxH)hv[hn++]={(px/(float)w)*2.f-1.f,1.f-(py/(float)h)*2.f,-1.f,-1.f,r,g,b,a};};
	auto hq=[&](float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,float r,float g,float b,float a){hp(ax,ay,r,g,b,a);hp(bx,by,r,g,b,a);hp(cx,cy,r,g,b,a);hp(ax,ay,r,g,b,a);hp(cx,cy,r,g,b,a);hp(dx,dy,r,g,b,a);};
	const BOOL overview=IsOverviewActive();
	float badgeX=0,badgeY=0,badgeMaxW=220.f;int badgeFloor=m_floor;BOOL drawBadge=FALSE;
	BOOL badgeRightEdge=FALSE,badgeAboveMap=FALSE;
	float mapOx=0,mapOy=0,mapSide=0;
	float miniL=0,miniT=0,miniR=0,miniB=0;BOOL haveMini=FALSE;
	if(overview){
		hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,.02f,.03f,.05f,.58f);
	}else if(savedata.s3m_show_map){
		// ミニマップ: 枠なし。見えるワールド範囲は従来の約3倍。白▲は画面ピクセル固定
		const float mapZ=MapZoomScale();
		const float kMapViewWorld=42.f/max(.5f,mapZ);
		const float mpix=(float)min(w,h)*.30f;
		const float cs=mpix/kMapViewWorld;
		const float mcx=w-10.f-mpix*.5f,mcy=10.f+mpix*.5f,pad=mpix*.5f;
		const float L=mcx-pad,R=mcx+pad,T=mcy-pad,B=mcy+pad;
		haveMini=TRUE;miniL=L;miniT=T;miniR=R;miniB=B;
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
		// 壁帯はワールドでは細いので、地図上だけわずかに太く（通路幅の約1/5程度）
		const float kMapWallPad = 0.05f;
		auto drawMapFloor=[&](int mf,float am){
			if(mf<0||mf>=m_nFloors||!m_grids[mf]||am<=.01f)return;
			for(int z=gz0;z<gz1;z++)for(int x=gx0;x<gx1;x++){
				BYTE c=CellAtF(mf,x,z);if(S3mIsSolidWall(c))continue;
				float x0=AxisOrigin(x),x1=x0+AxisSpan(x),z0=AxisOrigin(z),z1=z0+AxisSpan(z);
				float rr=.22f,gg=.40f,bb=.28f,a=.55f;
				if(c==CELL_GOAL){rr=1;gg=.8f;bb=.18f;a=.9f;}
				else if(c==CELL_START){rr=.2f;gg=.9f;bb=.45f;a=.9f;}
				else if(c==CELL_STAIRS_DOWN){rr=1;gg=.58f;bb=.16f;a=.9f;}
				else if(c==CELL_STAIRS_UP){rr=.22f;gg=.86f;bb=1.f;a=.9f;}
				else if(S3mIsPickupCell(c)){rr=.82f;gg=.35f;bb=.78f;a=.85f;}
				else if(c==CELL_SLIME){rr=.25f;gg=.8f;bb=.3f;a=.7f;}
				else if(c==CELL_SPIKE){rr=.95f;gg=.25f;bb=.2f;a=.75f;}
				else if(c==CELL_ICE){rr=.4f;gg=.8f;bb=1.f;a=.7f;}
				else if(c==CELL_DARK){rr=.2f;gg=.15f;bb=.4f;a=.75f;}
				else if(c==CELL_PORTAL){rr=.78f;gg=.35f;bb=1.f;a=.92f;}
				else if(c==CELL_KEY){rr=1.f;gg=.85f;bb=.25f;a=.92f;}
				else if(VisitAtF(mf,x,z)){rr=.30f;gg=.55f;bb=.85f;a=.7f;}
				mapQuad(x0,z0,x1,z0,x1,z1,x0,z1,rr,gg,bb,a*am);
			}
			for(int z=gz0;z<gz1;z++)for(int x=gx0;x<gx1;x++){
				BYTE c=CellAtF(mf,x,z);if(c!=CELL_WALL&&c!=CELL_WINDOW&&c!=CELL_MIRROR_WALL)continue;
				float x0=AxisOrigin(x)-kMapWallPad,x1=AxisOrigin(x)+AxisSpan(x)+kMapWallPad;
				float z0=AxisOrigin(z)-kMapWallPad,z1=AxisOrigin(z)+AxisSpan(z)+kMapWallPad;
				float rr=.55f,gg=.37f,bb=.25f,a=.95f;
				if(c==CELL_WINDOW){rr=.25f;gg=.58f;bb=.86f;}
				else if(c==CELL_MIRROR_WALL){rr=.45f;gg=.75f;bb=.95f;}
				mapQuad(x0,z0,x1,z0,x1,z1,x0,z1,rr,gg,bb,a*am);
			}
			for(int z=gz0;z<gz1;z++)for(int x=gx0;x<gx1;x++){
				if(CellAtF(mf,x,z)!=CELL_DOOR)continue;
				float x0,z0,x1,z1;DoorWorldRect(mf,x,z,kMapWallPad,x0,z0,x1,z1);
				mapQuad(x0,z0,x1,z0,x1,z1,x0,z1,.95f,.42f,.22f,.98f*am);
				const float cx=(x0+x1)*.5f,cz=(z0+z1)*.5f,ks=.07f;
				if(DoorThinX(mf,x,z))
					mapQuad(cx+.03f,cz-ks,cx+.11f,cz-ks,cx+.11f,cz+ks,cx+.03f,cz+ks,1.f,.82f,.28f,.95f*am);
				else
					mapQuad(cx-ks,cz+.03f,cx+ks,cz+.03f,cx+ks,cz+.11f,cx-ks,cz+.11f,1.f,.82f,.28f,.95f*am);
			}
		};
		if(m_miniFade<.999f){drawMapFloor(m_miniFadeFrom,1.f-m_miniFade);drawMapFloor(m_miniFadeTo,m_miniFade);}
		else drawMapFloor(m_floor,1.f);
		// 白▲はズームアウトしても従来サイズ（mpix基準・cs非依存）
		const float ps=max(7.f,min(14.f,mpix*.034f));
		auto clipPt2=[&](float& qx,float& qy){qx=max(L+1.f,min(R-1.f,qx));qy=max(T+1.f,min(B-1.f,qy));};
		// 階段の上下方向矢印（相手マス向き）
		auto drawStairArrows=[&](int mf,float am){
			if(mf<0||mf>=m_nFloors||!m_grids[mf]||am<=.01f)return;
			for(int z=gz0;z<gz1;z++)for(int x=gx0;x<gx1;x++){
				BYTE c=CellAtF(mf,x,z);
				if(c!=CELL_STAIRS_DOWN&&c!=CELL_STAIRS_UP)continue;
				int pf=0,px=0,pz=0;
				if(!FindStairPartner(mf,x,z,pf,px,pz))continue;
				float ax=cellCX(x),az=cellCZ(z),bx=cellCX(px),bz=cellCZ(pz);
				float mx0,my0,mx1,my1;
				WorldToMap(ax,az,mx0,my0);WorldToMap(bx,bz,mx1,my1);
				float qx0=mcx+mx0*cs,qy0=mcy+my0*cs,qx1=mcx+mx1*cs,qy1=mcy+my1*cs;
				if(!inBox((qx0+qx1)*.5f,(qy0+qy1)*.5f))continue;
				float dx=qx1-qx0,dy=qy1-qy0;float len=sqrtf(dx*dx+dy*dy)+1e-5f;dx/=len;dy/=len;
				const float asz=max(3.5f,min(7.f,cs*.22f));
				float tx=qx0+dx*len*.58f,ty=qy0+dy*len*.58f;
				float bx2=tx-dx*asz*1.35f,by2=ty-dy*asz*1.35f;
				float ox2=-dy*asz*.75f,oy2=dx*asz*.75f;
				float rr=1.f,gg=.55f,bb=.15f;if(c==CELL_STAIRS_UP){rr=.2f;gg=.85f;bb=1.f;}
				float t0x=tx,t0y=ty,t1x=bx2+ox2,t1y=by2+oy2,t2x=bx2-ox2,t2y=by2-oy2;
				clipPt2(t0x,t0y);clipPt2(t1x,t1y);clipPt2(t2x,t2y);
				hp(t0x,t0y,rr,gg,bb,am);hp(t1x,t1y,rr,gg,bb,am);hp(t2x,t2y,rr,gg,bb,am);
			}
		};
		if(m_miniFade<.999f){drawStairArrows(m_miniFadeFrom,1.f-m_miniFade);drawStairArrows(m_miniFadeTo,m_miniFade);}
		else drawStairArrows(m_floor,1.f);
		// ナビ導線（ミニマップ）
		if(HasNavPath()){
			auto drawNavSeg=[&](int mf,float am){
				if(mf<0||am<=.01f)return;
				for(int i=0;i+1<m_navCount;i++){
					if(m_navPath[i].f!=mf||m_navPath[i+1].f!=mf)continue;
					float ax=cellCX(m_navPath[i].x),az=cellCZ(m_navPath[i].z);
					float bx=cellCX(m_navPath[i+1].x),bz=cellCZ(m_navPath[i+1].z);
					float mx0,my0,mx1,my1;WorldToMap(ax,az,mx0,my0);WorldToMap(bx,bz,mx1,my1);
					float qx0=mcx+mx0*cs,qy0=mcy+my0*cs,qx1=mcx+mx1*cs,qy1=mcy+my1*cs;
					if(!inBox((qx0+qx1)*.5f,(qy0+qy1)*.5f))continue;
					float dx=qx1-qx0,dy=qy1-qy0;float len=sqrtf(dx*dx+dy*dy)+1e-5f;dx/=len;dy/=len;
					float ox=-dy*1.4f,oy=dx*1.4f;
					hq(qx0+ox,qy0+oy,qx1+ox,qy1+oy,qx1-ox,qy1-oy,qx0-ox,qy0-oy,.95f,.72f,.12f,.9f*am);
				}
				const S3mNavPt& d=m_navPath[m_navCount-1];
				if(d.f==mf){
					float mx,my;WorldToMap(cellCX(d.x),cellCZ(d.z),mx,my);
					float qx=mcx+mx*cs,qy=mcy+my*cs;
					if(inBox(qx,qy)){
						const float r=max(3.5f,cs*.18f);
						float tr=1.f,tg=.85f,tb=.15f;
						if(CellAtF(d.f,d.x,d.z)==CELL_KEY){tr=.75f;tg=1.f;tb=.28f;}
						hq(qx-r,qy-r,qx+r,qy-r,qx+r,qy+r,qx-r,qy+r,tr,tg,tb,.95f*am);
					}
				}
			};
			if(m_miniFade<.999f){drawNavSeg(m_miniFadeFrom,1.f-m_miniFade);drawNavSeg(m_miniFadeTo,m_miniFade);}
			else drawNavSeg(m_floor,1.f);
		}
		// 現在地▲（大きめ＋黒縁で視認性）
		float p0x=mcx,p0y=mcy-ps,p1x=mcx-ps*.62f,p1y=mcy+ps*.48f,p2x=mcx+ps*.62f,p2y=mcy+ps*.48f;
		clipPt2(p0x,p0y);clipPt2(p1x,p1y);clipPt2(p2x,p2y);
		const float os=1.35f;
		float o0x=mcx,o0y=mcy-ps*os,o1x=mcx-ps*.62f*os,o1y=mcy+ps*.48f*os,o2x=mcx+ps*.62f*os,o2y=mcy+ps*.48f*os;
		clipPt2(o0x,o0y);clipPt2(o1x,o1y);clipPt2(o2x,o2y);
		hp(o0x,o0y,.05f,.05f,.08f,1);hp(o1x,o1y,.05f,.05f,.08f,1);hp(o2x,o2y,.05f,.05f,.08f,1);
		hp(p0x,p0y,1.f,1.f,1.f,1);hp(p1x,p1y,1.f,1.f,1.f,1);hp(p2x,p2y,1.f,1.f,1.f,1);
		// ゴール方位計：ミニマップより外側・黄色＋縁取り＋「この方向」演出
		{
			int gx = m_goalX, gz = m_goalZ;
			if (gx < 0 || gz < 0 || m_goalFloor < 0 || m_goalFloor >= m_nFloors
				|| !m_grids[m_goalFloor] || CellAtF(m_goalFloor, gx, gz) != CELL_GOAL) {
				RefreshGoalCache();
				gx = m_goalX; gz = m_goalZ;
			}
			if (gx >= 0) {
				float gmx,gmy;WorldToMap(GridToWorldX((float)gx+.5f),GridToWorldZ((float)gz+.5f),gmx,gmy);
				float glen=sqrtf(gmx*gmx+gmy*gmy);
				if(glen>1e-4f){
					gmx/=glen;gmy/=glen;
					const float rim=pad+10.f; // ミニマップ枠の外側
					const float pulseG=.7f+.3f*sinf(m_anim*3.6f);
					float ox=-gmy,oy=gmx;
					float tipX=mcx+gmx*rim,tipY=mcy+gmy*rim;
					float midX=tipX-gmx*13.f,midY=tipY-gmy*13.f;
					float baseX=tipX-gmx*22.f,baseY=tipY-gmy*22.f;
					// 外側グロー（黄）
					for(int k=2;k>=0;k--){
						float s=1.f+(float)k*.22f;
						float a=.18f*(3-k)*pulseG;
						hp(tipX+gmx*2.f*s,tipY+gmy*2.f*s,1.f,1.f,.35f,a);
						hp(baseX-ox*10.f*s,baseY-oy*10.f*s,1.f,1.f,.35f,a);
						hp(baseX+ox*10.f*s,baseY+oy*10.f*s,1.f,1.f,.35f,a);
					}
					// 黒縁
					hp(tipX+gmx*1.6f,tipY+gmy*1.6f,.05f,.05f,.02f,1);
					hp(baseX-ox*8.2f,baseY-oy*8.2f,.05f,.05f,.02f,1);
					hp(baseX+ox*8.2f,baseY+oy*8.2f,.05f,.05f,.02f,1);
					// 黄色本体
					hp(tipX,tipY,1.f,.92f,.12f,pulseG);
					hp(baseX-ox*7.f,baseY-oy*7.f,1.f,.88f,.08f,pulseG);
					hp(baseX+ox*7.f,baseY+oy*7.f,1.f,.88f,.08f,pulseG);
					// 白いハイライト縁
					hp(tipX-gmx*.5f,tipY-gmy*.5f,1.f,1.f,.85f,.9f);
					hp(midX-ox*3.2f,midY-oy*3.2f,1.f,1.f,.85f,.75f);
					hp(midX+ox*3.2f,midY+oy*3.2f,1.f,1.f,.85f,.75f);
					// 「この方向」：矢印後方のパルスシェブロン
					for(int c=0;c<3;c++){
						float ph=fmodf(m_anim*1.8f+(float)c*.33f,1.f);
						float d=8.f+ph*16.f;
						float cx=tipX-gmx*d,cy=tipY-gmy*d;
						float w=5.5f*(1.f-ph*.55f);
						float aa=.55f*(1.f-ph)*pulseG;
						float c0x=cx+gmx*3.5f,c0y=cy+gmy*3.5f;
						float c1x=cx-gmx*2.f-ox*w,c1y=cy-gmy*2.f-oy*w;
						float c2x=cx-gmx*2.f+ox*w,c2y=cy-gmy*2.f+oy*w;
						hp(c0x,c0y,1.f,.95f,.2f,aa);hp(c1x,c1y,1.f,.9f,.1f,aa);hp(c2x,c2y,1.f,.9f,.1f,aa);
					}
					// 根元の短いステム
					hq(baseX-ox*2.2f,baseY-oy*2.2f,baseX+ox*2.2f,baseY+oy*2.2f,
						baseX+ox*2.2f-gmx*7.f,baseY+oy*2.2f-gmy*7.f,
						baseX-ox*2.2f-gmx*7.f,baseY-oy*2.2f-gmy*7.f,
						1.f,.85f,.1f,.75f*pulseG);
					if(m_goalFloor!=m_floor){
						// 他階：小さな二重矢印で階差を示唆
						float s2=0.62f;
						float t2x=tipX-gmx*4.f,t2y=tipY-gmy*4.f;
						hp(t2x,t2y,1.f,.75f,.05f,.85f);
						hp(t2x-gmx*9.f*s2-ox*5.f*s2,t2y-gmy*9.f*s2-oy*5.f*s2,1.f,.7f,.05f,.85f);
						hp(t2x-gmx*9.f*s2+ox*5.f*s2,t2y-gmy*9.f*s2+oy*5.f*s2,1.f,.7f,.05f,.85f);
					}
				}
			}
		}
		// コンパス：ワールド北(-Z)をミニマップ上で指す（カメラ前方だと常に上向きになる）
		float nx,ny;WorldToMap(ex+0.f,ez-1.f,nx,ny);
		float nlen=sqrtf(nx*nx+ny*ny)+1e-5f;nx/=nlen;ny/=nlen;
		float xx=-ny,xy=nx;
		float ccx=min(R-8.f,max(L+8.f,mcx+pad-12)),ccy=min(B-8.f,max(T+8.f,mcy-pad+12));
		const float crs=12.f;
		hp(ccx+nx*crs,ccy+ny*crs,1,.2f,.2f,1);hp(ccx-nx*2.5f-xx*5.5f,ccy-ny*2.5f-xy*5.5f,1,.2f,.2f,1);hp(ccx-nx*2.5f+xx*5.5f,ccy-ny*2.5f+xy*5.5f,1,.2f,.2f,1);
		// 階層ラベルはミニマップ左側（地図に重ねない・読みやすい大きさ）
		drawBadge=TRUE;badgeFloor=m_floor;badgeRightEdge=TRUE;
		badgeX=L-8.f;badgeY=T+2.f;badgeMaxW=min(480.f,max(240.f,L-16.f));
	}
	if(m_clearScreenA>.01f)hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,0,0,0,m_clearScreenA);
	else if(m_floorScreenA>.01f)hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,0,0,0,m_floorScreenA);
	else if(m_portalFx!=PORTALFX_IDLE&&m_portalFlashA>.01f){
		const float a=min(1.f,m_portalFlashA);
		hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,.55f,.12f,.95f,a*.72f);
		hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,1.f,.85f,1.f,a*.35f);
		// 放射リング＋火花
		const float cx=(float)w*.5f,cy=(float)h*.5f;
		const float phase=(m_portalFx==PORTALFX_OUT)?(m_portalFxT/.42f):(1.f-m_portalFxT/.55f);
		for(int ring=0;ring<5;ring++){
			const float rad=(40.f+ring*55.f)*(0.35f+phase*1.4f)+sinf(m_anim*8.f+(float)ring)*8.f;
			const float ra=a*(1.f-(float)ring/5.f)*.9f;
			const int segs=28;
			for(int s=0;s<segs;s++){
				const float a0=(float)s/(float)segs*(float)(M_PI*2);
				const float a1=(float)(s+1)/(float)segs*(float)(M_PI*2);
				const float rw=6.f+ring*1.5f;
				float x0=cx+cosf(a0)*rad,y0=cy+sinf(a0)*rad;
				float x1=cx+cosf(a1)*rad,y1=cy+sinf(a1)*rad;
				float x2=cx+cosf(a1)*(rad+rw),y2=cy+sinf(a1)*(rad+rw);
				float x3=cx+cosf(a0)*(rad+rw),y3=cy+sinf(a0)*(rad+rw);
				hq(x0,y0,x1,y1,x2,y2,x3,y3,.9f,.4f+ring*.08f,1.f,ra);
			}
		}
		for(int sp=0;sp<36;sp++){
			const float ang=(float)sp/(float)36*(float)(M_PI*2)+m_anim*6.f;
			const float dist=(30.f+phase*280.f)*(0.4f+0.6f*((sp*17)&7)/7.f);
			const float px=cx+cosf(ang)*dist,py=cy+sinf(ang)*dist;
			const float ps=4.f+3.f*(1.f-phase)+((sp*3)&3);
			hq(px-ps,py-ps,px+ps,py-ps,px+ps,py+ps,px-ps,py+ps,1.f,.7f,1.f,a*.85f);
		}
	}
	else if(m_darkT>.01f){
		const float da=min(.55f,m_darkT*.18f);
		hq(0,0,(float)w,0,(float)w,(float)h,0,(float)h,.02f,.01f,.06f,da);
	}
	if(hn&&SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);UINT hs=sizeof(S3MHudVertex);dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);dc->IASetInputLayout(m_view.m_ilHud);dc->VSSetShader(m_view.m_vsHud,NULL,0);dc->PSSetShader(m_view.m_psHud,NULL,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(hn,0);}
	if(!overview && nCall>0 && m_view.m_psHudLine){
		if(!m_view.m_srvCallout){
			const wchar_t* labs[19]={
				LL14(L"鍵", L"Key", L"Clé", L"Chiave", L"Llave", L"열쇠", L"钥匙", L"Key", L"Ключ", L"Schlüssel", L"Chave", L"Sleutel", L"Klucz", L"Anahtar"),
				LL14(L"扉", L"Door", L"Porte", L"Porta", L"Puerta", L"문", L"门", L"Door", L"Дверь", L"Tür", L"Porta", L"Deur", L"Drzwi", L"Kapı"),
				LL14(L"テンポ↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"템포↑", L"速度↑", L"Tempo↑", L"Темп↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑"),
				LL14(L"テンポ↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"템포↓", L"速度↓", L"Tempo↓", L"Темп↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓"),
				LL14(L"ピッチ↑", L"Pitch↑", L"Hauteur↑", L"Pitch↑", L"Tono↑", L"피치↑", L"音高↑", L"Pitch↑", L"Высота↑", L"Ton↑", L"Tom↑", L"Toon↑", L"Wys.↑", L"Perde↑"),
				LL14(L"ピッチ↓", L"Pitch↓", L"Hauteur↓", L"Pitch↓", L"Tono↓", L"피치↓", L"音高↓", L"Pitch↓", L"Высота↓", L"Ton↓", L"Tom↓", L"Toon↓", L"Wys.↓", L"Perde↓"),
				LL14(L"次の曲", L"Next", L"Suivant", L"Succ.", L"Siguiente", L"다음", L"下一曲", L"Next", L"След.", L"Nächster", L"Próxima", L"Volgend", L"Nast.", L"Sonraki"),
				LL14(L"前の曲", L"Prev", L"Préc.", L"Prec.", L"Anterior", L"이전", L"上一曲", L"Prev", L"Пред.", L"Vorher", L"Anterior", L"Vorig", L"Poprz.", L"Önceki"),
				LL14(L"音量↑", L"Vol↑", L"Vol↑", L"Vol↑", L"Vol↑", L"볼륨↑", L"音量↑", L"Vol↑", L"Громк.↑", L"Laut↑", L"Vol↑", L"Vol↑", L"Głoś↑", L"Ses↑"),
				LL14(L"音量↓", L"Vol↓", L"Vol↓", L"Vol↓", L"Vol↓", L"볼륨↓", L"音量↓", L"Vol↓", L"Громк.↓", L"Laut↓", L"Vol↓", L"Vol↓", L"Głoś↓", L"Ses↓"),
				LL14(L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ"),
				LL14(L"EQ平坦", L"EQ flat", L"EQ plat", L"EQ flat", L"EQ plano", L"EQ평탄", L"EQ平坦", L"EQ flat", L"EQ flat", L"EQ flach", L"EQ flat", L"EQ vlak", L"EQ płaski", L"EQ düz"),
				LL14(L"リバーブ", L"Reverb", L"Réverb", L"Reverb", L"Reverb", L"리버브", L"混响", L"Reverb", L"Реверб", L"Hall", L"Reverb", L"Reverb", L"Pogłos", L"Reverb"),
				LL14(L"クロスフェード", L"Crossfade", L"Fondu", L"Crossfade", L"Fundido", L"크로스페이드", L"交叉淡化", L"Xfade", L"Кроссфейд", L"Crossfade", L"Crossfade", L"Crossfade", L"Xfade", L"Xfade"),
				LL14(L"ランダム", L"Random", L"Aléatoire", L"Casuale", L"Aleatorio", L"랜덤", L"随机", L"Random", L"Случ.", L"Zufall", L"Aleatório", L"Willekeurig", L"Losowo", L"Rastgele"),
				LL14(L"階段↓", L"Stairs↓", L"Esc.↓", L"Scale↓", L"Esc.↓", L"계단↓", L"楼梯↓", L"Stairs↓", L"Лестн.↓", L"Treppe↓", L"Escadas↓", L"Trap↓", L"Schody↓", L"Merdiven↓"),
				LL14(L"階段↑", L"Stairs↑", L"Esc.↑", L"Scale↑", L"Esc.↑", L"계단↑", L"楼梯↑", L"Stairs↑", L"Лестн.↑", L"Treppe↑", L"Escadas↑", L"Trap↑", L"Schody↑", L"Merdiven↑"),
				LL14(L"ポータル", L"Portal", L"Portail", L"Portale", L"Portal", L"포털", L"传送门", L"Portal", L"Портал", L"Portal", L"Portal", L"Portaal", L"Portal", L"Portal"),
				LL14(L"ゴール", L"Goal", L"But", L"Traguardo", L"Meta", L"골", L"终点", L"Goal", L"Цель", L"Ziel", L"Gol", L"Doel", L"Cel", L"Hedef")
			};
			m_view.BakeCalloutAtlas(labs, 19);
		}
		struct BubDraw { int slot; float nx, ny, cw; BYTE c; };
		BubDraw bd[64]; int nBd=0;
		for(int i=0;i<nCall;i++){
			int slot=S3mCalloutSlot(calls[i].c);
			if(slot<0||slot>=m_view.m_calloutN) continue;
			float nx,ny,cw;
			if(!S3mWorldToNdc(vpMat, calls[i].wx, calls[i].wy, calls[i].wz, nx, ny, cw)) continue;
			if(nx<-1.02f||nx>1.02f||ny<-1.0f||ny>1.0f) continue;
			bd[nBd++]={slot,nx,ny,cw,calls[i].c};
		}
		for(int a=1;a<nBd;a++){BubDraw q=bd[a];int j=a-1;while(j>=0&&bd[j].cw<q.cw){bd[j+1]=bd[j];j--;}bd[j+1]=q;}
		auto htuv=[&](float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,float a){
			if(hn+6>maxH)return;
			hv[hn++]={x0,y1,u0,v0,1,1,1,a}; hv[hn++]={x1,y1,u1,v0,1,1,1,a}; hv[hn++]={x1,y0,u1,v1,1,1,1,a};
			hv[hn++]={x0,y1,u0,v0,1,1,1,a}; hv[hn++]={x1,y0,u1,v1,1,1,1,a}; hv[hn++]={x0,y0,u0,v1,1,1,1,a};
		};
		auto hline=[&](float x0,float y0,float x1,float y1,float r,float g,float b,float a){
			if(hn+6>maxH)return;
			hv[hn++]={x0,y0,0,0,r,g,b,a}; hv[hn++]={x1,y0,1,0,r,g,b,a}; hv[hn++]={x1,y1,1,1,r,g,b,a};
			hv[hn++]={x0,y0,0,0,r,g,b,a}; hv[hn++]={x1,y1,1,1,r,g,b,a}; hv[hn++]={x0,y1,0,1,r,g,b,a};
		};
		auto callA=[&](float cw)->float{
			if(cw<=4.5f)return 1.f;
			if(cw>=14.f)return .20f;
			return 1.f-(cw-4.5f)/(14.f-4.5f)*.80f;
		};
		hn=0;
		for(int k=0;k<nBd;k++){
			const float sc2=(bd[k].cw>8.f)?0.45f:((bd[k].cw>4.f)?0.7f:1.f);
			const float aa=callA(bd[k].cw);
			const float bw=0.32f*sc2, bh=0.085f*sc2;
			const float x0=bd[k].nx-bw*.5f, x1=bd[k].nx+bw*.5f;
			const float y0=bd[k].ny+0.02f*sc2, y1=y0+bh;
			float rr,gg,bb; S3mCalloutRgb(bd[k].c,rr,gg,bb);
			const float ly1=y0-0.003f*sc2, ly0=ly1-0.01f*sc2;
			hline(x0+0.012f*sc2,ly0,x1-0.012f*sc2,ly1,rr,gg,bb,.92f*aa);
			const float stemW=0.004f*sc2;
			hline(bd[k].nx-stemW, bd[k].ny+0.002f, bd[k].nx+stemW, ly0, rr,gg,bb,.8f*aa);
		}
		if(hn){
			if(SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);}
			UINT hs=sizeof(S3MHudVertex);
			dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);
			dc->IASetInputLayout(m_view.m_ilHud);
			dc->VSSetShader(m_view.m_vsHud,NULL,0);
			dc->PSSetShader(m_view.m_psHudLine,NULL,0);
			dc->PSSetSamplers(0,1,&m_view.m_sampLin);
			dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
			dc->Draw(hn,0);
		}
		hn=0;
		if(m_view.m_srvCallout){
			for(int k=0;k<nBd;k++){
				const float sc2=(bd[k].cw>8.f)?0.45f:((bd[k].cw>4.f)?0.7f:1.f);
				const float aa=callA(bd[k].cw);
				const float bw=0.32f*sc2, bh=0.085f*sc2;
				const float x0=bd[k].nx-bw*.5f, x1=bd[k].nx+bw*.5f;
				const float y0=bd[k].ny+0.02f*sc2, y1=y0+bh;
				const int s=bd[k].slot;
				htuv(x0,y0,x1,y1,m_view.m_calloutU0[s],m_view.m_calloutV0[s],m_view.m_calloutU1[s],m_view.m_calloutV1[s],.96f*aa);
			}
			if(hn){
				if(SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);}
				UINT hs=sizeof(S3MHudVertex);
				dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);
				dc->PSSetShader(m_view.m_psHud,NULL,0);
				dc->PSSetShaderResources(0,1,&m_view.m_srvCallout);
				dc->PSSetSamplers(0,1,&m_view.m_sampLin);
				dc->Draw(hn,0);
				dc->PSSetShaderResources(0,1,ns);
			}
		}
		hn=0;
	}
	if(overview&&m_view.m_srvMap&&m_view.m_texMap){
		const int TS=CS3mView::S3M_MAP_SIZE;
		int bakeFloor=m_mapViewFloor;
		if(bakeFloor<0||bakeFloor>=m_nFloors||!m_grids[bakeFloor])bakeFloor=m_floor;
		if(m_mapBakeDirty){
			const DWORD nowBake=GetTickCount();
			// 移動中の毎歩フル再ベイクは大マップで重い → 停止後 or 間引き
			const BOOL allowBake=(!m_moving && (nowBake-m_lastMapBakeTick)>80)
				|| (nowBake-m_lastMapBakeTick)>450;
			if(allowBake){
			m_lastMapBakeTick=nowBake;
			D3D11_MAPPED_SUBRESOURCE mm={};
			if(SUCCEEDED(dc->Map(m_view.m_texMap,0,D3D11_MAP_WRITE_DISCARD,0,&mm))&&mm.pData){
				BYTE* row=(BYTE*)mm.pData;
				const int pitch=(int)mm.RowPitch;
				// ワールド比率でベイク（壁帯は細い／通路は広い＝ミニマップと同じ見た目）
				auto rgba=[&](BYTE r,BYTE g,BYTE b,BYTE a)->DWORD{
					return ((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
				};
				const DWORD colWall=rgba(140,95,64,255);   // ミニマップ壁に近い茶
				const DWORD colWin=rgba(64,148,220,255);
				const DWORD colVisit=rgba(76,140,217,230);
				const DWORD colFloor=rgba(56,102,71,255);  // ミニマップ床に近い緑
				const DWORD colBlack=rgba(20,24,22,255);
				auto cellColor=[&](BYTE c,BOOL visited)->DWORD{
					if(c==CELL_GOAL)return rgba(255,210,40,235);
					if(c==CELL_START)return rgba(55,220,120,235);
					if(c==CELL_STAIRS_DOWN)return rgba(255,148,40,240);
					if(c==CELL_STAIRS_UP)return rgba(60,220,255,240);
					if(S3mIsPickupCell(c))return rgba(210,90,200,225);
					if(c==CELL_SLIME)return rgba(60,200,80,210);
					if(c==CELL_SPIKE)return rgba(220,60,50,215);
					if(c==CELL_ICE)return rgba(100,200,255,210);
					if(c==CELL_DARK)return rgba(50,40,90,220);
					if(c==CELL_MIRROR_WALL)return rgba(120,200,230,255);
					if(c==CELL_MIRROR_FLOOR)return rgba(100,180,210,230);
					if(c==CELL_PORTAL)return rgba(200,90,255,245);
					if(c==CELL_KEY)return rgba(255,215,60,245);
					if(c==CELL_DOOR)return visited?colVisit:colFloor;
					if(c==CELL_WALL)return colWall;
					if(c==CELL_WINDOW)return colWin;
					if(visited)return colVisit;
					return colFloor;
				};
				const float worldSpan=max(1e-3f,AxisOrigin(m_n));
				// ミニマップ相当の細さ（パッドなし＋細い軸をスリム化）
				const float kWallSlim=.42f;
				const float uScale=(float)TS/worldSpan;
				auto fillWorld=[&](float x0,float z0,float x1,float z1,DWORD col,BOOL minPx){
					int tx0=(int)floorf(x0*uScale),tx1=(int)ceilf(x1*uScale);
					int ty0=(int)floorf(z0*uScale),ty1=(int)ceilf(z1*uScale);
					if(minPx){
						if(tx1<=tx0){tx0=max(0,(int)((x0+x1)*.5f*uScale));tx1=tx0+1;}
						if(ty1<=ty0){ty0=max(0,(int)((z0+z1)*.5f*uScale));ty1=ty0+1;}
					}
					if(tx0<0)tx0=0;if(ty0<0)ty0=0;
					if(tx1>TS)tx1=TS;if(ty1>TS)ty1=TS;
					for(int ty=ty0;ty<ty1;ty++){
						DWORD* dst=(DWORD*)(row+ty*pitch);
						for(int tx=tx0;tx<tx1;tx++)dst[tx]=col;
					}
				};
				auto fillWorldMin=[&](float x0,float z0,float x1,float z1,DWORD col,int minT){
					int tx0=(int)floorf(x0*uScale),tx1=(int)ceilf(x1*uScale);
					int ty0=(int)floorf(z0*uScale),ty1=(int)ceilf(z1*uScale);
					int cx=(tx0+tx1)/2,cy=(ty0+ty1)/2;
					if(tx1-tx0<minT){tx0=cx-minT/2;tx1=tx0+minT;}
					if(ty1-ty0<minT){ty0=cy-minT/2;ty1=ty0+minT;}
					if(tx0<0)tx0=0;if(ty0<0)ty0=0;
					if(tx1>TS)tx1=TS;if(ty1>TS)ty1=TS;
					for(int ty=ty0;ty<ty1;ty++){
						DWORD* dst=(DWORD*)(row+ty*pitch);
						for(int tx=tx0;tx<tx1;tx++)dst[tx]=col;
					}
				};
				for(int ty=0;ty<TS;ty++){
					DWORD* dst=(DWORD*)(row+ty*pitch);
					for(int tx=0;tx<TS;tx++)dst[tx]=colBlack;
				}
				for(int z=0;z<m_n;z++)for(int x=0;x<m_n;x++){
					const BYTE c=CellAtF(bakeFloor,x,z);
					if(S3mIsSolidWall(c))continue;
					DWORD col=cellColor(c,VisitAtF(bakeFloor,x,z)?TRUE:FALSE);
					fillWorld(AxisOrigin(x),AxisOrigin(z),AxisOrigin(x)+AxisSpan(x),AxisOrigin(z)+AxisSpan(z),col,FALSE);
				}
				for(int z=0;z<m_n;z++)for(int x=0;x<m_n;x++){
					const BYTE c=CellAtF(bakeFloor,x,z);
					if(c!=CELL_WALL&&c!=CELL_WINDOW&&c!=CELL_MIRROR_WALL)continue;
					float x0=AxisOrigin(x),x1=x0+AxisSpan(x);
					float z0=AxisOrigin(z),z1=z0+AxisSpan(z);
					const float sx=x1-x0,sz=z1-z0;
					if(sx<=sz){
						const float cx=(x0+x1)*.5f,half=sx*.5f*kWallSlim;
						x0=cx-half;x1=cx+half;
					}
					if(sz<=sx){
						const float cz=(z0+z1)*.5f,half=sz*.5f*kWallSlim;
						z0=cz-half;z1=cz+half;
					}
					fillWorld(x0,z0,x1,z1,cellColor(c,FALSE),TRUE);
				}
				// 特殊マスを上書き（ズームアウトでも色が見えるよう最小テクスチャサイズを確保）
				for(int z=0;z<m_n;z++)for(int x=0;x<m_n;x++){
					const BYTE c=CellAtF(bakeFloor,x,z);
					const BOOL special=c==CELL_GOAL||c==CELL_START||c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP
						||c==CELL_PORTAL||c==CELL_KEY||c==CELL_MIRROR_FLOOR||c==CELL_MIRROR_WALL||c==CELL_WINDOW
						||S3mIsPickupCell(c)||S3mIsTrapCell(c);
					if(c==CELL_DOOR){
						float x0,z0,x1,z1;DoorWorldRect(bakeFloor,x,z,0.05f,x0,z0,x1,z1);
						fillWorldMin(x0,z0,x1,z1,rgba(242,108,48,255),4);
						const float cx=(x0+x1)*.5f,cz=(z0+z1)*.5f,ks=.08f;
						if(DoorThinX(bakeFloor,x,z))
							fillWorldMin(cx+.03f,cz-ks,cx+.12f,cz+ks,rgba(255,210,70,255),3);
						else
							fillWorldMin(cx-ks,cz+.03f,cx+ks,cz+.12f,rgba(255,210,70,255),3);
						continue;
					}
					if(!special)continue;
					float x0=AxisOrigin(x),x1=x0+AxisSpan(x);
					float z0=AxisOrigin(z),z1=z0+AxisSpan(z);
					const int minT=(c==CELL_GOAL||c==CELL_START)?6:((c==CELL_STAIRS_DOWN||c==CELL_STAIRS_UP||c==CELL_PORTAL)?5:4);
					fillWorldMin(x0,z0,x1,z1,cellColor(c,FALSE),minT);
				}
				dc->Unmap(m_view.m_texMap,0);
				m_mapBakeDirty=0;
			}
			} // allowBake
		}
		const float tipBand=210.f;
		const float topPad=68.f;
		const float areaTop=topPad;
		const float areaH=max(96.f,(float)h-tipBand-topPad);
		const float baseSide=OverviewBaseSide(w,h);
		const float side=baseSide*OverviewZoomScale();
		ClampMapPan(w,h,side);
		const float ox=((float)w-side)*.5f+m_mapPanX;
		const float oy=areaTop+(areaH-side)*.5f+m_mapPanY;
		D3D11_VIEWPORT mvp={ox,oy,side,side,0,1};
		cb.misc.y=1.f;cb.misc.z=99.f;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->RSSetViewports(1,&mvp);
		dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		// CLAMP+点サンプリング（LinはWRAPのためタイル格子に見える）
		dc->PSSetSamplers(0,1,&m_view.m_sampPoint);
		dc->PSSetShaderResources(0,1,&m_view.m_srvMap);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,ns);
		// 自機マーカー＋階段（ワールド正規化＝ベイクと一致）
		hn=0;
		const float worldSpan=max(1e-3f,AxisOrigin(m_n));
		const float invW=1.f/worldSpan;
		auto w2s=[&](float wx,float wz,float& sx,float& sy){
			sx=ox+wx*invW*side;
			sy=oy+wz*invW*side;
		};
		auto cellCX=[&](int x)->float{return AxisOrigin(x)+AxisSpan(x)*.5f;};
		auto cellCZ=[&](int z)->float{return AxisOrigin(z)+AxisSpan(z)*.5f;};
		for(int z=0;z<m_n;z++)for(int x=0;x<m_n;x++){
			BYTE c=CellAtF(bakeFloor,x,z);
			if(c!=CELL_STAIRS_DOWN&&c!=CELL_STAIRS_UP)continue;
			int pf=0,px=0,pz=0;
			if(!FindStairPartner(bakeFloor,x,z,pf,px,pz))continue;
			float qx0,qy0,qx1,qy1;w2s(cellCX(x),cellCZ(z),qx0,qy0);w2s(cellCX(px),cellCZ(pz),qx1,qy1);
			float dx=qx1-qx0,dy=qy1-qy0;float len=sqrtf(dx*dx+dy*dy)+1e-5f;dx/=len;dy/=len;
			const float asz=max(8.f,side*.018f);
			float tx=qx0+dx*len*.58f,ty=qy0+dy*len*.58f;
			float bx2=tx-dx*asz*1.35f,by2=ty-dy*asz*1.35f;
			float ox2=-dy*asz*.75f,oy2=dx*asz*.75f;
			float rr=1.f,gg=.55f,bb=.15f;if(c==CELL_STAIRS_UP){rr=.2f;gg=.85f;bb=1.f;}
			hp(tx,ty,rr,gg,bb,1.f);hp(bx2+ox2,by2+oy2,rr,gg,bb,1.f);hp(bx2-ox2,by2-oy2,rr,gg,bb,1.f);
		}
		// ナビ導線（全体マップ）
		if(HasNavPath()){
			for(int i=0;i+1<m_navCount;i++){
				if(m_navPath[i].f!=bakeFloor||m_navPath[i+1].f!=bakeFloor)continue;
				float sx0,sy0,sx1,sy1;
				w2s(cellCX(m_navPath[i].x),cellCZ(m_navPath[i].z),sx0,sy0);
				w2s(cellCX(m_navPath[i+1].x),cellCZ(m_navPath[i+1].z),sx1,sy1);
				float dx=sx1-sx0,dy=sy1-sy0;float len=sqrtf(dx*dx+dy*dy)+1e-5f;dx/=len;dy/=len;
				float oxN=-dy*max(1.5f,side*.0025f),oyN=dx*max(1.5f,side*.0025f);
				hq(sx0+oxN,sy0+oyN,sx1+oxN,sy1+oyN,sx1-oxN,sy1-oyN,sx0-oxN,sy0-oyN,.95f,.72f,.12f,.85f);
			}
			const S3mNavPt& d=m_navPath[m_navCount-1];
			if(d.f==bakeFloor){
				float sx,sy;w2s(cellCX(d.x),cellCZ(d.z),sx,sy);
				const float r=max(5.f,side*.01f);
				float tr=1.f,tg=.85f,tb=.15f;
				if(CellAtF(d.f,d.x,d.z)==CELL_KEY){tr=.75f;tg=1.f;tb=.28f;}
				hq(sx-r,sy-r,sx+r,sy-r,sx+r,sy+r,sx-r,sy+r,tr,tg,tb,.95f);
			}
		}
		float ffx,ffz,rrx,rrz;CamBasisYaw(m_yaw,ffx,ffz,rrx,rrz);
		// 自機▲：通路1マス程度を上限に、画面ピクセルでサイズ固定（ズームで暴走しない）
		float pcx,pcy;w2s(GridToWorldX(m_px),GridToWorldZ(m_pz),pcx,pcy);
		const float cellPx=1.f*invW*side; // 通路マスの画面サイズ
		const float arrowPx=max(7.f,min(16.f,cellPx*.75f));
		float t0x=pcx+ffx*arrowPx,t0y=pcy+ffz*arrowPx;
		float t1x=pcx-ffx*arrowPx*.42f-rrx*arrowPx*.55f,t1y=pcy-ffz*arrowPx*.42f-rrz*arrowPx*.55f;
		float t2x=pcx-ffx*arrowPx*.42f+rrx*arrowPx*.55f,t2y=pcy-ffz*arrowPx*.42f+rrz*arrowPx*.55f;
		// 注: 画面Yは下方向＋なので、ワールドforwardのz成分はそのまま画面Δyに使う（従来w2s差分と同じ向き）
		auto clampMap=[&](float& qx,float& qy){qx=max(ox+2.f,min(ox+side-2.f,qx));qy=max(oy+2.f,min(oy+side-2.f,qy));};
		clampMap(t0x,t0y);clampMap(t1x,t1y);clampMap(t2x,t2y);
		{
			float cx=(t0x+t1x+t2x)*(1.f/3.f),cy=(t0y+t1y+t2y)*(1.f/3.f);
			const float os=1.22f;
			hp(cx+(t0x-cx)*os,cy+(t0y-cy)*os,.05f,.05f,.08f,1.f);
			hp(cx+(t1x-cx)*os,cy+(t1y-cy)*os,.05f,.05f,.08f,1.f);
			hp(cx+(t2x-cx)*os,cy+(t2y-cy)*os,.05f,.05f,.08f,1.f);
		}
		hp(t0x,t0y,1.f,.95f,.2f,1.f);hp(t1x,t1y,1.f,.95f,.2f,1.f);hp(t2x,t2y,1.f,.95f,.2f,1.f);
		hq(ox-2,oy-2,ox+side+2,oy-2,ox+side+2,oy+side+2,ox-2,oy+side+2,.9f,.92f,1.f,.22f);
		if(hn&&SUCCEEDED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,hv,hn*sizeof(S3MHudVertex));dc->Unmap(m_view.m_vbHud,0);UINT hs=sizeof(S3MHudVertex);dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&off);dc->RSSetViewports(1,&vp);dc->IASetInputLayout(m_view.m_ilHud);dc->VSSetShader(m_view.m_vsHud,NULL,0);dc->PSSetShader(m_view.m_psHud,NULL,0);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(hn,0);}
		dc->RSSetViewports(1,&vp);
		mapOx=ox;mapOy=oy;mapSide=side;
		drawBadge=TRUE;badgeFloor=bakeFloor;
		badgeRightEdge=FALSE;badgeAboveMap=FALSE;
		badgeX=14.f;badgeY=12.f;badgeMaxW=380.f;
	}
	auto blitFloorBadge=[&](){
		if(!drawBadge||!m_view.m_ready)return;
		const CStringW keyWord=LL14(L"鍵", L"Keys", L"Clés", L"Chiavi", L"Llaves",
			L"열쇠", L"钥匙", L"مفاتيح", L"Ключи", L"Schlüssel", L"Chaves", L"Sleutels", L"Klucze", L"Anahtar");
		CStringW lab;
		lab.Format(L"%s　%s×%d", (LPCWSTR)CStringW(FloorLabel(badgeFloor)), (LPCWSTR)keyWord, m_keysHeld);
		if(lab!=m_mapBadgeText||!m_view.m_srvBadge)
			m_view.BakeBadgeTexture((LPCWSTR)lab),m_mapBadgeText=lab;
		if(!m_view.m_srvBadge||m_view.m_badgeW<=0)return;
		float tw=(float)m_view.m_badgeW,th=(float)m_view.m_badgeH;
		if(tw>badgeMaxW){const float s=badgeMaxW/tw;tw*=s;th*=s;}
		float bx=badgeX,by=badgeY;
		if(badgeRightEdge){
			bx=badgeX-tw;
			if(bx<4.f)bx=4.f;
		}else if(badgeAboveMap){
			bx=mapOx+(mapSide-tw)*.5f;
			by=mapOy-th-6.f;
			if(by<4.f)by=4.f;
			if(bx<4.f)bx=4.f;
			if(bx+tw>(float)w-4.f)bx=max(4.f,(float)w-4.f-tw);
		}
		D3D11_VIEWPORT bvp={bx,by,tw,th,0,1};
		cb.misc.y=1.f;cb.misc.z=99.f;
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
	EnsureTipTexture(overview);
	float tipX=0.f,tipY=0.f,tipTw=0.f,tipTh=0.f;BOOL haveTip=FALSE;
	if(m_view.m_srvTip&&m_view.m_tipW>0&&m_view.m_tipH>0){
		float tw=(float)m_view.m_tipW,th=(float)m_view.m_tipH;
		tipX=8.f;tipY=8.f;
		if(overview){
			// 下帯：操作ヒント。その上に階層タブバーを置く
			const float pad=10.f;
			const float maxW=(float)w-pad*2.f;
			const float maxH=180.f;
			if(tw>maxW){const float s=maxW/tw;tw*=s;th*=s;}
			if(th>maxH){const float s=maxH/th;tw*=s;th*=s;}
			tipX=((float)w-tw)*.5f;
			tipY=(float)h-th-pad;
			if(tipX<pad)tipX=pad;
			if(tipY<pad)tipY=pad;
		}else{
			// 通常: ミニマップ／階層ラベルの左側までに収める。狭いときはミニマップ下へ
			const float pad=8.f;
			const float maxH=220.f;
			float badgeW=0.f;
			if(drawBadge&&m_view.m_badgeW>0){
				badgeW=(float)m_view.m_badgeW;
				if(badgeW>badgeMaxW)badgeW=badgeMaxW;
			}
			float maxW=min((float)w*.72f,780.f);
			if(haveMini){
				const float stopX=miniL-(badgeW>0.f?badgeW+12.f:8.f)-pad;
				maxW=min(maxW,max(180.f,stopX-pad));
			}
			if(tw>maxW){const float s=maxW/tw;tw*=s;th*=s;}
			if(th>maxH){const float s=maxH/th;tw*=s;th*=s;}
			tipX=pad;
			tipY=pad;
			if(haveMini){
				const float badgeLeft=miniL-(badgeW>0.f?badgeW+8.f:0.f);
				if(tipX+tw>badgeLeft-6.f){
					// 横に並ぶと被るのでミニマップ下へ退避
					tipY=miniB+10.f;
					maxW=min((float)w-pad*2.f,780.f);
					tw=(float)m_view.m_tipW;th=(float)m_view.m_tipH;
					if(tw>maxW){const float s=maxW/tw;tw*=s;th*=s;}
					if(th>maxH){const float s=maxH/th;tw*=s;th*=s;}
					tipX=pad;
				}
			}
			if(tipY+th>(float)h-pad)tipY=max(pad,(float)h-pad-th);
		}
		haveTip=TRUE;tipTw=tw;tipTh=th;
		D3D11_VIEWPORT tipVp={tipX,tipY,tw,th,0,1};
		cb.misc.y=1.f;cb.misc.z=99.f;
		if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		dc->RSSetViewports(1,&tipVp);
		dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		dc->PSSetShaderResources(0,1,&m_view.m_srvTip);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,ns);
		dc->RSSetViewports(1,&vp);
	}
	if(overview&&m_nFloors>=1&&m_view.m_ready){
		static int s_bakedView=-99,s_bakedPlayer=-99,s_bakedN=-99;
		if(m_floorBarDirty||!m_view.m_srvFloorBar
			||s_bakedView!=m_mapViewFloor||s_bakedPlayer!=m_floor||s_bakedN!=m_nFloors){
			CStringW labStore[4];
			const wchar_t* labs[4]={};
			for(int i=0;i<m_nFloors&&i<4;i++){
				labStore[i]=FloorLabel(i);
				labs[i]=(LPCWSTR)labStore[i];
			}
			m_view.BakeFloorBarTexture(m_nFloors,m_mapViewFloor,m_floor,labs);
			m_floorBarDirty=0;
			s_bakedView=m_mapViewFloor;s_bakedPlayer=m_floor;s_bakedN=m_nFloors;
		}
		if(m_view.m_srvFloorBar&&m_view.m_floorBarW>0){
			float bw=(float)m_view.m_floorBarW,bh=(float)m_view.m_floorBarH;
			const float pad=10.f;
			const float maxW=(float)w-pad*2.f;
			if(bw>maxW){const float s=maxW/bw;bw*=s;bh*=s;}
			float bx=((float)w-bw)*.5f;
			float by=haveTip?(tipY-bh-8.f):((float)h-bh-pad);
			if(by<pad)by=pad;
			m_ovBarX=bx;m_ovBarY=by;m_ovBarW=bw;m_ovBarH=bh;
			m_ovTabN=min(m_nFloors,4);
			for(int i=0;i<m_ovTabN;i++){m_ovTabX0[i]=m_view.m_floorBarTabX0[i];m_ovTabX1[i]=m_view.m_floorBarTabX1[i];}
			m_ovRecX0=m_view.m_floorBarRecX0;m_ovRecX1=m_view.m_floorBarRecX1;
			D3D11_VIEWPORT bvp={bx,by,bw,bh,0,1};
			cb.misc.y=1.f;cb.misc.z=99.f;
			if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
			dc->RSSetViewports(1,&bvp);
			dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);
			dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetSamplers(0,1,&m_view.m_sampLin);
			dc->PSSetShaderResources(0,1,&m_view.m_srvFloorBar);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);
			dc->PSSetShaderResources(0,1,ns);
			dc->RSSetViewports(1,&vp);
		}
	}else{
		m_ovBarW=m_ovBarH=0.f;
	}
	// hv はビューの再利用スクラッチ（delete しない）
	if(m_clearTextA<=.01f&&m_floorTextA>.01f&&m_view.m_srvClear){
		cb.misc.y=m_floorTextA; cb.misc.z=99.f;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
		D3D11_VIEWPORT tvp={0,(float)h*.375f,(float)w,(float)h*.25f,0,1};dc->RSSetViewports(1,&tvp);dc->IASetInputLayout(NULL);dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->VSSetShader(m_view.m_vsPost,NULL,0);dc->PSSetShader(m_view.m_psFinal,NULL,0);dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);dc->PSSetShaderResources(0,1,&m_view.m_srvClear);dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);dc->Draw(3,0);dc->PSSetShaderResources(0,1,ns);
	}
	if((m_clearTextA>.01f||m_navWaiting)&&m_view.m_srvClear){
		cb.misc.y=m_navWaiting?1.f:m_clearTextA; cb.misc.z=99.f;if(SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){memcpy(map.pData,&cb,sizeof(cb));dc->Unmap(m_view.m_cbFrame,0);}
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
	if (CCustomPopupMenu* sizeSub = menu.AddSubMenu(LL14(L"大きさ", L"Size", L"Taille", L"Dimensione", L"Tamaño",
		L"크기", L"大小", L"الحجم", L"Размер", L"Größe", L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"))) {
		CString sizeFmt = LL14(L"大きさ: %d", L"Size: %d", L"Taille: %d", L"Dimensione: %d", L"Tamaño: %d",
			L"크기: %d", L"大小：%d", L"الحجم: %d", L"Размер: %d", L"Größe: %d", L"Tamanho: %d", L"Grootte: %d", L"Rozmiar: %d", L"Boyut: %d");
		for (int i = 0; i < kPresetCnt; i++) {
			CString lab;
			lab.Format(sizeFmt, kPresets[i]);
			sizeSub->AddCheck(S3M_MENU_SIZE0 + i, lab, curSz == kPresets[i]);
		}
	}
	menu.AddSeparator();
	if (CCustomPopupMenu* viewSub = menu.AddSubMenu(LL14(L"表示・視点", L"View", L"Vue", L"Vista", L"Vista",
		L"보기/시점", L"显示/视角", L"عرض", L"Вид", L"Ansicht", L"Vista", L"Weergave", L"Widok", L"Görünüm"))) {
		viewSub->AddCheck(20, LL14(L"ミニマップ表示", L"Show minimap", L"Afficher la minimap", L"Mostra minimap", L"Mostrar minimapa",
			L"미니맵 표시", L"显示小地图", L"إظهار الخريطة المصغّرة", L"Показать мини-карту", L"Minimap anzeigen", L"Mostrar minimapa", L"Minimapa tonen", L"Pokaż minimapę", L"Minimapi göster"),
			savedata.s3m_show_map != 0);
		viewSub->AddCheck(56, LL14(L"PCM効果音（足音／罠／ゴール）", L"PCM SFX (steps / traps / goal)", L"SFX PCM (pas / pièges / arrivée)", L"SFX PCM (passi / trappole / arrivo)", L"SFX PCM (pasos / trampas / meta)", L"PCM 효과음(발소리/함정/골)", L"PCM效果音（脚步／陷阱／终点）", L"PCM SFX (steps / traps)", L"PCM-эффекты (шаги/ловушки)", L"PCM-SFX (Schritte / Fallen)", L"SFX PCM (passos / armadilhas)", L"PCM-SFX (stappen / vallen)", L"SFX PCM (kroki / pułapki)", L"PCM SFX (adım / tuzak)"), savedata.s3_pcm_sfx != 0);
		viewSub->AddSeparator();
		viewSub->AddCheck(50,LL14(L"歩行時の揺れ",L"Walking bob",L"Balancement de marche",L"Oscillazione camminata",L"Balanceo al caminar",L"걷기 흔들림",L"行走晃动",L"تمايل المشي",L"Покачивание при ходьбе",L"Kamerawippen",L"Balanço ao andar",L"Loopbeweging",L"Kołysanie chodu",L"Yürüme sallantısı"),savedata.s3m_bob!=0);
		viewSub->AddCheck(51,L"FOV 55°",savedata.s3m_fov==0);
		viewSub->AddCheck(52,L"FOV 70°",savedata.s3m_fov==1);
		viewSub->AddCheck(53,L"FOV 90°",savedata.s3m_fov==2);
		viewSub->AddCommand(54,LL14(L"ズームをリセット（視点・地図）",L"Reset zoom (view & maps)",L"Réinitialiser le zoom",L"Reimposta zoom",L"Restablecer zoom",
			L"줌 리셋(시점·지도)",L"重置缩放（视角与地图）",L"إعادة التكبير",L"Сбросить зум",L"Zoom zurücksetzen",L"Redefinir zoom",L"Zoom resetten",L"Resetuj zoom",L"Zoomu sıfırla"));
	}
	menu.AddSeparator();
	const int mask = S3mItemMask();
	if (CCustomPopupMenu* itemSub = menu.AddSubMenu(LL14(L"アイテム", L"Items", L"Objets", L"Oggetti", L"Objetos",
		L"아이템", L"道具", L"عناصر", L"Предметы", L"Items", L"Itens", L"Items", L"Przedmioty", L"Öğeler"))) {
		itemSub->AddCommand(28, LL14(L"アイテムをすべてON", L"Enable all items", L"Activer tous les objets", L"Attiva tutti gli oggetti", L"Activar todos los objetos",
			L"아이템 모두 ON", L"全部道具开启", L"تفعيل كل العناصر", L"Включить все предметы", L"Alle Items ein", L"Ativar todos os itens", L"Alle items aan", L"Włącz wszystkie przedmioty", L"Tüm öğeleri aç"));
		itemSub->AddCommand(29, LL14(L"アイテムをすべてOFF", L"Disable all items", L"Désactiver tous les objets", L"Disattiva tutti gli oggetti", L"Desactivar todos los objetos",
			L"아이템 모두 OFF", L"全部道具关闭", L"تعطيل كل العناصر", L"Выключить все предметы", L"Alle Items aus", L"Desativar todos os itens", L"Alle items uit", L"Wyłącz wszystkie przedmioty", L"Tüm öğeleri kapat"));
		itemSub->AddSeparator();
		// 関連性順: テンポ→ピッチ→音量→曲送り→EQ→FX（窓は別扱い）
		itemSub->AddCheck(30, LL14(L"アイテム: テンポ↑", L"Item: tempo↑", L"Objet: tempo↑", L"Oggetto: tempo↑", L"Objeto: tempo↑", L"아이템: 템포↑", L"道具：速度↑", L"عنصر: إيقاع↑", L"Предмет: темп↑", L"Item: Tempo↑", L"Item: tempo↑", L"Item: tempo↑", L"Przedmiot: tempo↑", L"Öğe: tempo↑"), (mask & ITEM_TEMPO) != 0);
		itemSub->AddCheck(36, LL14(L"アイテム: テンポ↓", L"Item: tempo↓", L"Objet: tempo↓", L"Oggetto: tempo↓", L"Objeto: tempo↓", L"아이템: 템포↓", L"道具：速度↓", L"Item: tempo↓", L"Item: tempo↓", L"Item: Tempo↓", L"Item: tempo↓", L"Item: tempo↓", L"Przedmiot: tempo↓", L"Öğe: tempo↓"), (mask & ITEM_TEMPO_DN) != 0);
		itemSub->AddCheck(31, LL14(L"アイテム: ピッチ↑", L"Item: pitch↑", L"Objet: hauteur↑", L"Oggetto: pitch↑", L"Objeto: tono↑", L"아이템: 피치↑", L"道具：音高↑", L"عنصر: طبقة↑", L"Предмет: высота↑", L"Item: Tonhöhe↑", L"Item: tom↑", L"Item: toon↑", L"Przedmiot: wysokość↑", L"Öğe: perde↑"), (mask & ITEM_PITCH_UP) != 0);
		itemSub->AddCheck(32, LL14(L"アイテム: ピッチ↓", L"Item: pitch↓", L"Objet: hauteur↓", L"Oggetto: pitch↓", L"Objeto: tono↓", L"아이템: 피치↓", L"道具：音高↓", L"عنصر: طبقة↓", L"Предмет: высота↓", L"Item: Tonhöhe↓", L"Item: tom↓", L"Item: toon↓", L"Przedmiot: wysokość↓", L"Öğe: perde↓"), (mask & ITEM_PITCH_DN) != 0);
		itemSub->AddCheck(38, LL14(L"アイテム: 音量↑", L"Item: volume↑", L"Objet: volume↑", L"Oggetto: volume↑", L"Objeto: volumen↑", L"아이템: 볼륨↑", L"道具：音量↑", L"Item: vol↑", L"Item: vol↑", L"Item: Lautstärke↑", L"Item: volume↑", L"Item: volume↑", L"Przedmiot: głośność↑", L"Öğe: ses↑"), (mask & ITEM_VOL_UP) != 0);
		itemSub->AddCheck(39, LL14(L"アイテム: 音量↓", L"Item: volume↓", L"Objet: volume↓", L"Oggetto: volume↓", L"Objeto: volumen↓", L"아이템: 볼륨↓", L"道具：音量↓", L"Item: vol↓", L"Item: vol↓", L"Item: Lautstärke↓", L"Item: volume↓", L"Item: volume↓", L"Przedmiot: głośność↓", L"Öğe: ses↓"), (mask & ITEM_VOL_DN) != 0);
		itemSub->AddCheck(33, LL14(L"アイテム: 次の曲", L"Item: next track", L"Objet: piste suivante", L"Oggetto: brano successivo", L"Objeto: pista siguiente", L"아이템: 다음 곡", L"道具：下一曲", L"عنصر: المسار التالي", L"Предмет: следующий трек", L"Item: nächster Titel", L"Item: próxima faixa", L"Item: volgend nummer", L"Przedmiot: następny utwór", L"Öğe: sonraki parça"), (mask & ITEM_NEXT) != 0);
		itemSub->AddCheck(37, LL14(L"アイテム: 前の曲", L"Item: previous track", L"Objet: piste précédente", L"Oggetto: brano precedente", L"Objeto: pista anterior", L"아이템: 이전 곡", L"道具：上一曲", L"Item: prev", L"Item: prev", L"Item: vorheriger Titel", L"Item: faixa anterior", L"Item: vorig nummer", L"Przedmiot: poprzedni", L"Öğe: önceki"), (mask & ITEM_PREV) != 0);
		itemSub->AddCheck(34, LL14(L"アイテム: EQ", L"Item: EQ", L"Objet: EQ", L"Oggetto: EQ", L"Objeto: EQ", L"아이템: EQ", L"道具：EQ", L"عنصر: EQ", L"Предмет: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Przedmiot: EQ", L"Öğe: EQ"), (mask & ITEM_EQ) != 0);
		itemSub->AddCheck(42, LL14(L"アイテム: EQ平坦化", L"Item: EQ flatten", L"Objet: EQ plat", L"Oggetto: EQ flat", L"Objeto: EQ plano", L"아이템: EQ 평탄", L"道具：EQ平坦", L"Item: EQ flat", L"Item: EQ flat", L"Item: EQ flach", L"Item: EQ flat", L"Item: EQ flat", L"Przedmiot: EQ flat", L"Öğe: EQ düz"), (mask & ITEM_EQ_FLAT) != 0);
		itemSub->AddCheck(40, LL14(L"アイテム: リバーブ", L"Item: reverb", L"Objet: réverb", L"Oggetto: reverb", L"Objeto: reverb", L"아이템: 리버브", L"道具：混响", L"Item: reverb", L"Item: reverb", L"Item: Hall", L"Item: reverb", L"Item: reverb", L"Przedmiot: pogłos", L"Öğe: reverb"), (mask & ITEM_REVERB) != 0);
		itemSub->AddCheck(41, LL14(L"アイテム: クロスフェード", L"Item: crossfade", L"Objet: fondu croisé", L"Oggetto: crossfade", L"Objeto: fundido", L"아이템: 크로스페이드", L"道具：交叉淡化", L"Item: crossfade", L"Item: crossfade", L"Item: Crossfade", L"Item: crossfade", L"Item: crossfade", L"Przedmiot: crossfade", L"Öğe: crossfade"), (mask & ITEM_XFADE) != 0);
		itemSub->AddCheck(43, LL14(L"アイテム: ランダム再生切替", L"Item: toggle random play", L"Objet: aléatoire on/off", L"Oggetto: casuale on/off", L"Objeto: aleatorio on/off", L"아이템: 랜덤 재생 전환", L"道具：随机播放切换", L"Item: random", L"Item: random", L"Item: Zufall umschalten", L"Item: aleatório", L"Item: willekeurig", L"Przedmiot: losowo", L"Öğe: rastgele"), (mask & ITEM_RANDOM) != 0);
		itemSub->AddSeparator();
		itemSub->AddCheck(35, LL14(L"窓を配置", L"Place windows", L"Placer des fenêtres", L"Posiziona finestre", L"Colocar ventanas",
			L"창 배치", L"放置窗户", L"وضع نوافذ", L"Размещать окна", L"Fenster platzieren", L"Colocar janelas", L"Ramen plaatsen", L"Umieść okna", L"Pencere yerleştir"), (mask & ITEM_WINDOW) != 0);
	}
	menu.AddSeparator();
	menu.AddCommand(45, LL14(L"テンポ／ピッチを開いた時に戻す", L"Reset tempo/pitch to opening values", L"Remettre tempo/hauteur d'ouverture", L"Ripristina tempo/pitch iniziali", L"Restablecer tempo/tono iniciales",
		L"템포/피치를 열 때 값으로", L"将速度/音高恢复为打开时", L"إعادة الإيقاع/الطبقة لقيم الفتح", L"Вернуть темп/высоту к открытию", L"Tempo/Tonhöhe auf Öffnungswerte", L"Restaurar tempo/tom de abertura", L"Tempo/toonhoogte naar openingswaarden", L"Przywróć tempo/wysokość z otwarcia", L"Tempo/perdeyi açılış değerine al"));
	menu.AddSeparator();
	if (CCustomPopupMenu* viewSub = menu.AddSubMenu(LL14(L"視点", L"View", L"Vue", L"Vista", L"Vista",
		L"시점", L"视角", L"منظور", L"Вид", L"Ansicht", L"Vista", L"Weergave", L"Widok", L"Görünüm"))) {
		viewSub->AddCheck(50,LL14(L"歩行時の揺れ",L"Walking bob",L"Balancement de marche",L"Oscillazione camminata",L"Balanceo al caminar",L"걷기 흔들림",L"行走晃动",L"تمايل المشي",L"Покачивание при ходьбе",L"Kamerawippen",L"Balanço ao andar",L"Loopbeweging",L"Kołysanie chodu",L"Yürüme sallantısı"),savedata.s3m_bob!=0);
		viewSub->AddCheck(51,L"FOV 55°",savedata.s3m_fov==0);
		viewSub->AddCheck(52,L"FOV 70°",savedata.s3m_fov==1);
		viewSub->AddCheck(53,L"FOV 90°",savedata.s3m_fov==2);
		viewSub->AddCommand(54,LL14(L"ズームをリセット（視点・地図）",L"Reset zoom (view & maps)",L"Réinitialiser le zoom",L"Reimposta zoom",L"Restablecer zoom",
			L"줌 리셋(시점·지도)",L"重置缩放（视角与地图）",L"إعادة التكبير",L"Сбросить зум",L"Zoom zurücksetzen",L"Redefinir zoom",L"Zoom resetten",L"Resetuj zoom",L"Zoomu sıfırla"));
	}

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
	if (cmd == 56) {
		savedata.s3_pcm_sfx = savedata.s3_pcm_sfx ? 0 : 1;
		PersistUi();
		return;
	}
	if (cmd == 28) {
		S3mSetItemMask(ITEM_ALL);
		PersistUi();
		return;
	}
	if (cmd == 29) {
		S3mSetItemMask(0);
		PersistUi();
		return;
	}
	if (cmd >= 30 && cmd <= 43) {
		const int bit = 1 << (cmd - 30);
		int m = S3mItemMask();
		if (m & bit) m &= ~bit; else m |= bit;
		S3mSetItemMask(m);
		PersistUi();
		return;
	}
	if (cmd == 45)
		RestoreAudioBaseline();
	if(cmd==50){savedata.s3m_bob=savedata.s3m_bob?0:1;PersistUi();return;}
	if(cmd>=51&&cmd<=53){savedata.s3m_fov=(int)cmd-51;PersistUi();return;}
	if(cmd==54){savedata.s3m_zoom=100;savedata.s3m_map_zoom=100;PersistUi();UpdateStatus();return;}
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
	m_diff.SetAeroMode(FALSE);
	m_gen.SetAeroMode(FALSE);
	m_navi.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);
	m_view.SetAeroMode(FALSE);

	if (!m_uiFont.GetSafeHandle()) {
		LOGFONT lf = {};
		lf.lfHeight = -18;
		lf.lfWeight = FW_SEMIBOLD;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfQuality = CLEARTYPE_QUALITY;
		_tcscpy_s(lf.lfFaceName, _T("Segoe UI"));
		m_uiFont.CreateFontIndirect(&lf);
	}
	if (m_uiFont.GetSafeHandle()) {
		CFont* pf = &m_uiFont;
		if (m_sizeL.GetSafeHwnd()) m_sizeL.SetFont(pf);
		if (m_baseL.GetSafeHwnd()) m_baseL.SetFont(pf);
		if (m_diffL.GetSafeHwnd()) m_diffL.SetFont(pf);
		if (m_hint.GetSafeHwnd()) m_hint.SetFont(pf);
		if (m_status.GetSafeHwnd()) m_status.SetFont(pf);
		if (m_size.GetSafeHwnd()) { m_size.SetFont(pf); m_size.SetItemHeight(-1, 28); m_size.SetItemHeight(0, 26); }
		if (m_base.GetSafeHwnd()) { m_base.SetFont(pf); m_base.SetItemHeight(-1, 28); m_base.SetItemHeight(0, 26); }
		if (m_diff.GetSafeHwnd()) { m_diff.SetFont(pf); m_diff.SetItemHeight(-1, 28); m_diff.SetItemHeight(0, 26); }
		if (m_gen.GetSafeHwnd()) m_gen.SetFont(pf);
		if (m_navi.GetSafeHwnd()) m_navi.SetFont(pf);
		if (m_close.GetSafeHwnd()) m_close.SetFont(pf);
	}

	if (savedata.s3m_minimap < 8 || savedata.s3m_minimap > 16)
		savedata.s3m_minimap = 10;
	savedata.s3m_minimap = S3mClampMapSize(savedata.s3m_minimap);
	if (savedata.s3m_item_mask < 0)
		savedata.s3m_item_mask = -1;
	else if (savedata.s3m_item_mask == 0)
		savedata.s3m_item_mask = ITEM_ALL;
	if (savedata.s3m_show_map != 0)
		savedata.s3m_show_map = 1;
	else if (!savedata.s3m_have_run)
		savedata.s3m_show_map = 1;
	if(savedata.s3m_bob!=0&&savedata.s3m_bob!=1)savedata.s3m_bob=1;
	if(savedata.s3m_fov<0||savedata.s3m_fov>2)savedata.s3m_fov=1;
	if(savedata.s3m_basements<0||savedata.s3m_basements>S3M_MAX_FLOORS-1)savedata.s3m_basements=0;
	if(savedata.s3m_difficulty<0||savedata.s3m_difficulty>=DIFF_COUNT)savedata.s3m_difficulty=DIFF_NORMAL;
	if(savedata.s3m_zoom<50||savedata.s3m_zoom>250)savedata.s3m_zoom=100;
	if(savedata.s3m_map_zoom<50||savedata.s3m_map_zoom>400)savedata.s3m_map_zoom=100;

	SetWindowText(LL14(L"Soft3D 迷路", L"Soft3D maze", L"Labyrinthe Soft3D", L"Labirinto Soft3D", L"Laberinto Soft3D",
		L"Soft3D 미로", L"Soft3D 迷宫", L"متاهة Soft3D", L"Лабиринт Soft3D", L"Soft3D-Labyrinth",
		L"Labirinto Soft3D", L"Soft3D-doolhof", L"Labirynt Soft3D", L"Soft3D labirent"));
	m_sizeL.SetWindowText(LL14(L"大きさ", L"Size", L"Taille", L"Dimensione", L"Tamaño",
		L"크기", L"大小", L"الحجم", L"Размер", L"Größe", L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"));
	m_baseL.SetWindowText(LL14(L"地下", L"Basement", L"Sous-sol", L"Sotterraneo", L"Sótano",
		L"지하", L"地下", L"قبو", L"Подвал", L"Keller", L"Subsolo", L"Kelder", L"Piwnica", L"Bodrum"));
	m_diffL.SetWindowText(LL14(L"難易度", L"Difficulty", L"Difficulté", L"Difficoltà", L"Dificultad",
		L"난이도", L"难度", L"الصعوبة", L"Сложность", L"Schwierigkeit", L"Dificuldade", L"Moeilijkheid", L"Trudność", L"Zorluk"));
	m_gen.SetWindowText(LL14(L"生成", L"Generate", L"Générer", L"Genera", L"Generar",
		L"생성", L"生成", L"توليد", L"Создать", L"Erzeugen", L"Gerar", L"Genereren", L"Generuj", L"Oluştur"));
	m_navi.SetWindowText(LL14(L"ナビ", L"Navi", L"Nav", L"Navi", L"Navi",
		L"내비", L"导航", L"توجيه", L"Нави", L"Navi", L"Navi", L"Navi", L"Nawi", L"Navi"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(L"WASD/QE · パッド(左=移動/右·LB·RB=旋回/A·Start=マップ) · ホイール=拡大縮小 · SPACE/ホイールクリック=全体マップ（ホイール=地図ズーム・ドラッグ=スクロール・Shift+ホイール/←→=階層） · 右クリック", L"WASD/QE · pad(L=move/R·LB·RB=turn/A·Start=map) · wheel=zoom · SPACE/wheel-click=map (wheel=map zoom · drag=scroll · Shift+wheel/←→=floor) · right-click", L"WASD/QE · pad(G=bouger/D·LB·RB=tourner/A·Start=carte) · molette=zoom · Espace/clic molette=carte · clic droit", L"WASD/QE · pad(SX=muovi/DX·LB·RB=gira/A·Start=mappa) · rotella=zoom · SPAZIO/clic rotella=mappa · clic destro",
		L"WASD/QE · pad(izq=mover/der·LB·RB=girar/A·Start=mapa) · rueda=zoom · Espacio/clic rueda=mapa · clic derecho", L"WASD/QE · 패드(좌=이동/우·LB·RB=회전/A·Start=맵) · 휠=줌 · SPACE/휠클릭=맵 · 우클릭", L"WASD/QE · 手柄(左=移动/右·LB·RB=转向/A·Start=地图) · 滚轮=缩放 · 空格/滚轮点击=地图 · 右键", L"WASD/QE · pad(L=move/R·LB·RB=turn/A·Start=map) · wheel=zoom · SPACE=map · right-click",
		L"WASD/QE · пад(L=ход/R·LB·RB=поворот/A·Start=карта) · колесо=зум · Пробел=карта · ПКМ", L"WASD/QE · Pad(L=Bewegen/R·LB·RB=Drehen/A·Start=Karte) · Rad=Zoom · Leertaste=Karte · Rechtsklick", L"WASD/QE · pad(Esq=mover/Dir·LB·RB=girar/A·Start=mapa) · roda=zoom · Espaço=mapa · direito", L"WASD/QE · pad(L=bewegen/R·LB·RB=draaien/A·Start=kaart) · wiel=zoom · Spatie=kaart · rechtsklik",
		L"WASD/QE · pad(L=ruch/R·LB·RB=obrót/A·Start=mapa) · kółko=zoom · Spacja=mapa · PPM", L"WASD/QE · pad(sol=hareket/sağ·LB·RB=dönüş/A·Start=harita) · teker=zoom · SPACE=harita · sağ tık"));

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

	m_diff.AddString(LL14(L"超簡単", L"Very easy", L"Très facile", L"Molto facile", L"Muy fácil",
		L"매우 쉬움", L"非常简单", L"سهل جداً", L"Очень легко", L"Sehr leicht", L"Muito fácil", L"Zeer makkelijk", L"Bardzo łatwy", L"Çok kolay"));
	m_diff.AddString(LL14(L"簡単", L"Easy", L"Facile", L"Facile", L"Fácil",
		L"쉬움", L"简单", L"سهل", L"Легко", L"Leicht", L"Fácil", L"Makkelijk", L"Łatwy", L"Kolay"));
	m_diff.AddString(LL14(L"普通", L"Normal", L"Normal", L"Normale", L"Normal",
		L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_diff.AddString(LL14(L"難しい", L"Hard", L"Difficile", L"Difficile", L"Difícil",
		L"어려움", L"困难", L"صعب", L"Сложно", L"Schwer", L"Difícil", L"Moeilijk", L"Trudny", L"Zor"));
	m_diff.AddString(LL14(L"超難しい", L"Very hard", L"Très difficile", L"Molto difficile", L"Muy difícil",
		L"매우 어려움", L"非常困难", L"صعب جداً", L"Очень сложно", L"Sehr schwer", L"Muito difícil", L"Zeer moeilijk", L"Bardzo trudny", L"Çok zor"));
	SetDifficultyToUi(savedata.s3m_difficulty);

	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tooltip.Activate(TRUE);
		m_tooltip.AddTool(&m_gen, LL14(L"新しい迷路を生成します", L"Generate a new maze", L"Générer un nouveau labyrinthe", L"Genera un nuovo labirinto", L"Generar un nuevo laberinto",
			L"새 미로 생성", L"生成新迷宫", L"إنشاء متاهة جديدة", L"Создать новый лабиринт", L"Neues Labyrinth erzeugen", L"Gerar um novo labirinto", L"Nieuw doolhof genereren", L"Wygeneruj nowy labirynt", L"Yeni labirent oluştur"));
		m_tooltip.AddTool(&m_navi, LL14(L"ゴールへ最大50歩のナビ導線をミニマップ／全体マップ／3Dに表示。鍵不足で不通なら最寄りの鍵へ。到達で消えます（何度でも可）", L"Show up to 50 steps toward the goal on minimap/full map/3D. If locked, guides to nearest key. Clears on arrival (reusable).", L"Affiche jusqu'à 50 pas vers le but (ou la clé). Disparaît à l'arrivée.", L"Fino a 50 passi verso il traguardo (o chiave). Scompare all'arrivo.", L"Hasta 50 pasos hacia la meta (o llave). Desaparece al llegar.",
			L"골까지 최대 50보(막히면 열쇠). 도착 시 소멸(재사용 가능)", L"最多50步导航至终点（不通则指向钥匙）。到达后消失（可反复使用）", L"Up to 50 steps to goal or key. Clears on arrival.", L"До 50 шагов к цели или ключу. Сбрасывается по прибытии.", L"Bis 50 Schritte zum Ziel oder Schlüssel. Verschwindet bei Ankunft.", L"Até 50 passos à meta ou chave. Some ao chegar.", L"Tot 50 stappen naar doel of sleutel. Verdwijnt bij aankomst.", L"Do 50 kroków do celu lub klucza. Znika po dotarciu.", L"Hedefe veya anahtara 50 adıma kadar. Varınca silinir."));
		m_tooltip.AddTool(&m_base, LL14(L"地下の階数（0〜3）。階段で階層がつながり、ゴールは難易度に応じてどこかの階に置かれます。", L"Number of basements (0–3). Stairs link floors; goal floor depends on difficulty.", L"Nombre de sous-sols (0–3). Les escaliers relient ; le but dépend de la difficulté.", L"Numero di sotterranei (0–3). Le scale collegano; il traguardo dipende dalla difficoltà.", L"Número de sótanos (0–3). Escaleras unen; la meta depende de la dificultad.",
			L"지하 층수(0~3). 계단으로 연결되고 골 위치는 난이도에 따라 달라집니다.", L"地下层数（0–3）。楼梯连接各层，终点位置随难度变化。", L"عدد الأقبية (0–3). السلالم تربط والطابق الهدف يعتمد على الصعوبة.", L"Число подземных этажей (0–3). Лестницы связывают; этаж цели зависит от сложности.", L"Anzahl Kellergeschosse (0–3). Treppen verbinden; Zielétage hängt von der Schwierigkeit ab.", L"Número de subsolos (0–3). Escadas ligam; o piso do gol depende da dificuldade.", L"Aantal kelders (0–3). Trappen verbinden; doelverdieping hangt van moeilijkheid af.", L"Liczba piwnic (0–3). Schody łączą; piętro celu zależy od trudności.", L"Bodrum sayısı (0–3). Merdivenler bağlar; hedef katı zorluğa göre değişir."));
		m_tooltip.AddTool(&m_diff, LL14(L"難易度。難しいほど通路が細く、階段が多く上下往復し、ゴールは階をまたいだ遠い位置になります。アイテムは多めです。", L"Difficulty. Harder = thinner corridors, more stairs (floor zigzags), farther multi-floor goal. Items stay plentiful.", L"Difficulté. Plus difficile = couloirs fins, plus d'escaliers, but lointain. Objets nombreux.", L"Difficoltà. Più difficile = corridoi stretti, più scale, traguardo lontano. Molti oggetti.", L"Dificultad. Más difícil = pasillos estrechos, más escaleras, meta lejana. Muchos objetos.",
			L"난이도. 어려울수록 좁은 통로·계단 많음(층 왕복)·먼 골. 아이템은 많음.", L"难度。越难通道越窄、楼梯越多（上下往返）、终点越远。道具偏多。", L"Harder: thinner corridors, more stairs, farther goal, plentiful items.", L"Harder: thinner corridors, more stairs, farther goal, plentiful items.", L"Harder: thinner corridors, more stairs, farther goal, plentiful items.", L"Harder: thinner corridors, more stairs, farther goal, plentiful items.", L"Harder: thinner corridors, more stairs, farther goal, plentiful items.", L"Harder: thinner corridors, more stairs, farther goal, plentiful items.", L"Harder: thinner corridors, more stairs, farther goal, plentiful items."));
		if (m_hint.GetSafeHwnd()) {
			CString ht;
			m_hint.GetWindowText(ht);
			if (!ht.IsEmpty())
				m_tooltip.AddTool(&m_hint, ht);
		}
	}

	CaptureAudioBaseline();
	ApplySavedWindowRect();
	LayoutAll();
	if(!m_view.InitDx()){
		CString msg;
		msg.Format(L"%s\n(stage=%d hr=0x%08X)",
			LL14(L"DirectX 11 の初期化に失敗しました。",L"DirectX 11 initialization failed.",L"Échec de l'initialisation de DirectX 11.",L"Inizializzazione DirectX 11 non riuscita.",L"Error al iniciar DirectX 11.",L"DirectX 11 초기화에 실패했습니다.",L"DirectX 11 初始化失败。",L"فشل تهيئة DirectX 11.",L"Не удалось инициализировать DirectX 11.",L"DirectX 11 konnte nicht initialisiert werden.",L"Falha ao iniciar o DirectX 11.",L"Initialisatie van DirectX 11 mislukt.",L"Nie udało się zainicjować DirectX 11.",L"DirectX 11 başlatılamadı."),
			m_view.m_dxFailStage, (unsigned)m_view.m_dxFailHr);
		MessageBox(msg, NULL, MB_OK|MB_ICONERROR);
		DestroyWindow();
		return FALSE;
	}
	Soft3DSfxEnsure(m_hWnd);
	m_playTipText = LL14(
		L"WASD / QE / パッド：移動・旋回\nホイール：拡大縮小（Shift+で旋回）\nSPACE／A・Start／ホイールクリック：全体マップ切替\n（ホイール=ズーム・ドラッグ=スクロール・Shift+ホイール/←→=階層）\n橙の階段=地下へ／水色=地上へ。ゴールは難易度で階が変わる",
		L"WASD / QE / pad: move / turn\nWheel: zoom (Shift+=turn)\nSPACE / A·Start / wheel-click: toggle map\n(wheel=zoom · drag=scroll · Shift+wheel/←→=floor)\nOrange stairs down, cyan up. Goal floor depends on difficulty",
		L"WASD / QE : bouger / tourner\nMolette : zoom (Maj+=tourner)\nEspace / clic molette : carte\n(molette=zoom · glisser=défiler · Maj+molette/←→=étage)\nEscaliers orange : descendre, cyan : monter. But selon difficulté",
		L"WASD / QE: muovi / gira\nRotella: zoom (Maiusc+=gira)\nSPAZIO / clic rotella: mappa\n(rotella=zoom · trascina=scorri · Maiusc+rotella/←→=piano)\nScale arancioni giù, ciano su. Traguardo secondo difficoltà",
		L"WASD / QE: mover / girar\nRueda: zoom (Mayús+=girar)\nEspacio / clic rueda: mapa\n(rueda=zoom · arrastrar=desplazar · Mayús+rueda/←→=planta)\nEscaleras naranjas bajan, cian suben. Meta según dificultad",
		L"WASD / QE: 이동 / 선회\n휠: 줌 (Shift+=회전)\nSPACE / 휠 클릭: 전체 맵 토글\n(휠=줌 · 드래그=스크롤 · Shift+휠/←→=층)\n주황 계단=지하로, 하늘색=지상으로. 골 층은 난이도에 따라",
		L"WASD / QE：移动 / 转向\n滚轮：缩放（Shift+=转向）\n空格 / 滚轮点击：全图开关\n（滚轮=缩放 · 拖动=滚动 · Shift+滚轮/←→=层）\n橙色楼梯下行，水色上行。终点层随难度变化",
		L"WASD / QE: حركة / دوران\nعجلة: تكبير (Shift+=دوران)\nمسافة / نقر عجلة: خريطة\n(عجلة=تكبير · سحب=تمرير · Shift+عجلة/←→=طابق)\nالسلالم البرتقالية للأسفل والسماوية للأعلى",
		L"WASD / QE: ход / поворот\nКолесо: зум (Shift+=поворот)\nПробел / клик колёсиком: карта\n(колесо=зум · перетаскивание=прокрутка · Shift+колесо/←→=этаж)\nОранжевые лестницы вниз, голубые вверх",
		L"WASD / QE: bewegen / drehen\nRad: Zoom (Umschalt+=drehen)\nLeertaste / Radklick: Karte\n(Rad=Zoom · Ziehen=Scrollen · Umschalt+Rad/←→=Etage)\nOrange Treppen abwärts, Cyan aufwärts",
		L"WASD / QE: mover / girar\nRoda: zoom (Shift+=girar)\nEspaço / clique da roda: mapa\n(roda=zoom · arrastar=rolar · Shift+roda/←→=piso)\nEscadas laranja descem, ciano sobem",
		L"WASD / QE: bewegen / draaien\nWiel: zoom (Shift+=draaien)\nSpatie / wielklik: kaart\n(wiel=zoom · slepen=scrollen · Shift+wiel/←→=verdieping)\nOranje trappen omlaag, cyaan omhoog",
		L"WASD / QE: ruch / obrót\nKółko: zoom (Shift+=obrót)\nSpacja / klik kółkiem: mapa\n(kółko=zoom · przeciąganie=przewijanie · Shift+kółko/←→=piętro)\nPomarańczowe schody w dół, cyjanowe w górę",
		L"WASD / QE: hareket / dönüş\nTeker: zoom (Shift+=dönüş)\nSPACE / teker tık: harita\n(teker=zoom · sürükle=kaydır · Shift+teker/←→=kat)\nTuruncu merdiven aşağı, camgöbeği yukarı");
	m_overviewTipText = LL14(
		L"全体マップ\n下の階層タブをクリック／◎自機へ\nShift+ホイール / ←→ / A D / パッド左右：階層\nホイール：ズーム　ドラッグ：スクロール\nSPACE / A・Start / Esc / B：閉じる",
		L"Full map\nClick floor tabs / ◎ recenter\nShift+wheel / ←→ / A D / pad L-R: floor\nWheel: zoom  Drag: scroll\nSPACE / A·Start / Esc / B: close",
		L"Carte\nOnglets d'étage / ◎ recentrer\nMaj+molette / ←→ / A D : étage\nMolette : zoom  Glisser : défiler\nEspace / Échap : fermer",
		L"Mappa\nSchede piano / ◎ centra\nMaiusc+rotella / ←→ / A D: piano\nRotella: zoom  Trascina: scorri\nSPAZIO / Esc: chiudi",
		L"Mapa\nPestañas de planta / ◎ centrar\nMayús+rueda / ←→ / A D: planta\nRueda: zoom  Arrastrar: desplazar\nEspacio / Esc: cerrar",
		L"전체 맵\n아래 층 탭 클릭 / ◎ 자기로\nShift+휠 / ←→ / A D: 층\n휠: 줌  드래그: 스크롤\nSPACE / Esc: 닫기",
		L"全图\n点击下方层标签 / ◎回自身\nShift+滚轮 / ←→ / A D：换层\n滚轮：缩放  拖动：滚动\n空格 / Esc：关闭",
		L"الخريطة\nانقر تبويبات الطابق / ◎ إعادة التمركز\nShift+عجلة / ←→ / A D: طابق\nعجلة: تكبير  سحب: تمرير\nمسافة / Esc: إغلاق",
		L"Карта\nВкладки этажа / ◎ к себе\nShift+колесо / ←→ / A D: этаж\nКолесо: зум  Перетаскивание: прокрутка\nПробел / Esc: закрыть",
		L"Karte\nEtagen-Tabs / ◎ zentrieren\nUmschalt+Rad / ←→ / A D: Etage\nRad: Zoom  Ziehen: Scrollen\nLeertaste / Esc: schließen",
		L"Mapa\nAbas de piso / ◎ recentrar\nShift+roda / ←→ / A D: piso\nRoda: zoom  Arrastar: rolar\nEspaço / Esc: fechar",
		L"Kaart\nVerdiepingstabbladen / ◎ centreren\nShift+wiel / ←→ / A D: verdieping\nWiel: zoom  Slepen: scrollen\nSpatie / Esc: sluiten",
		L"Mapa\nKarty pięter / ◎ wyśrodkuj\nShift+kółko / ←→ / A D: piętro\nKółko: zoom  Przeciąganie: przewijanie\nSpacja / Esc: zamknij",
		L"Harita\nKat sekmeleri / ◎ ortala\nShift+teker / ←→ / A D: kat\nTeker: zoom  Sürükle: kaydır\nSPACE / Esc: kapat");
	m_tipIsOverview = -1;
	m_mapBadgeText.Empty();
	EnsureTipTexture(FALSE);
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
	// 描画・更新は og の timerp（TheadLoop≈60Hz）経由。独自 SetTimer は使わない
	return TRUE;
}

void CSoft3DMazeDlg::OnGen() { GenerateMaze(); }
void CSoft3DMazeDlg::OnNavi()
{
	if (m_n <= 0 || m_navWaiting) return;
	if (m_won || m_clearPhase != CLEAR_IDLE) {
		if (m_status.GetSafeHwnd())
			m_status.SetWindowText(LL14(L"ナビ：クリア中は使えません", L"Navi: unavailable while clearing", L"Nav: indisponible", L"Navi: non disponibile", L"Navi: no disponible",
				L"내비: 클리어 중 불가", L"导航：结算中不可用", L"Navi: unavailable", L"Нави: недоступно", L"Navi: nicht verfügbar", L"Navi: indisponível", L"Navi: niet beschikbaar", L"Nawi: niedostępne", L"Navi: kullanılamaz"));
		return;
	}

	m_navWaiting = 1;
	if (m_navi.GetSafeHwnd()) m_navi.EnableWindow(FALSE);
	{
		CStringW wait = LL14(L"少々お待ちください…", L"Please wait…", L"Veuillez patienter…", L"Attendere…", L"Espere…",
			L"잠시만 기다려 주세요…", L"请稍候…", L"يرجى الانتظار…", L"Подождите…", L"Bitte warten…", L"Aguarde…", L"Even geduld…", L"Proszę czekać…", L"Lütfen bekleyin…");
		if (m_view.m_ready)
			m_view.BakeClearTexture((LPCWSTR)wait, 1.f);
		if (m_status.GetSafeHwnd())
			m_status.SetWindowText(LL14(L"ナビ探索中…", L"Searching path…", L"Recherche…", L"Ricerca…", L"Buscando…",
				L"경로 탐색 중…", L"正在寻路…", L"جاري البحث…", L"Поиск пути…", L"Suche Weg…", L"Buscando rota…", L"Pad zoeken…", L"Szukanie…", L"Yol aranıyor…"));
		RenderScene();
		if (m_hWnd) UpdateWindow();
		if (m_view.GetSafeHwnd()) m_view.UpdateWindow();
	}

	const BOOL ok = ComputeNavHint();

	m_navWaiting = 0;
	if (m_navi.GetSafeHwnd()) m_navi.EnableWindow(TRUE);
	if (m_clearPhase == CLEAR_IDLE && m_floorFx == FLOORFX_IDLE)
		m_view.ReleaseClearTexture();

	if (!ok) {
		if (m_status.GetSafeHwnd())
			m_status.SetWindowText(LL14(L"ナビ：経路が見つかりません", L"Navi: no path found", L"Nav: aucun chemin", L"Navi: nessun percorso", L"Navi: sin ruta",
				L"내비: 경로 없음", L"导航：找不到路径", L"Navi: no path", L"Нави: путь не найден", L"Navi: kein Weg", L"Navi: sem caminho", L"Navi: geen pad", L"Nawi: brak ścieżki", L"Navi: yol yok"));
		RenderScene();
		return;
	}
	UpdateStatus();
	RenderScene();
}
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
	savedata.s3m_basements = ReadBasementsFromUi();
	PersistUi();
}
void CSoft3DMazeDlg::OnDiffChanged()
{
	// UI→savedata を先に確定（リサイズで選択が0に戻っても値が残る）
	savedata.s3m_difficulty = ReadDifficultyFromUi();
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

void CSoft3DMazeDlg::TickFrame()
{
	if (!GetSafeHwnd() || !m_view.m_ready)
		return;
	const DWORD now = GetTickCount();
	float dt = (float)(now - m_lastTick) * 0.001f;
	m_lastTick = now;
	if (dt < 0.f) dt = 0.f;
	if (dt > 0.05f) dt = 0.05f;
	TickClear(dt);
	TickMove(dt);
	{
		float ex, ez;
		GetRenderEye(ex, ez);
		Soft3DSfxSetListener(ex, GetRenderEyeY(), ez, m_yaw);
		Soft3DSfxPump();
	}
	RenderScene();
	m_view.RequestRedraw();
	if (m_runDirty && !m_moving) {
		DWORD interval = 4000;
		if (m_n >= 200) {
			const unsigned long long cells = (unsigned long long)m_n * (unsigned long long)m_n * (unsigned long long)max(1, m_nFloors);
			interval = (DWORD)min(120000ull, max(8000ull, cells / 5000ull));
		}
		if ((now - m_lastAutosave) > interval) {
			PersistRun();
			m_lastAutosave = now;
		}
	}
}

void CSoft3DMazeDlg::OnTimer(UINT_PTR id)
{
	// 迷路フレームは Soft3DMazeOnTimerp → TickFrame。ここではアクリル等の基底タイマーのみ
	CCustomBlurDialogBase::OnTimer(id);
}

void CSoft3DMazeDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		LayoutAll();
		PersistWindowRect();
	}
}

void CSoft3DMazeDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow)
		LayoutAll();
}

void CSoft3DMazeDlg::OnDestroy()
{
	PersistUi();
	PersistRun();
	Soft3DSfxShutdown();
	RestoreAudioBaseline();
	FreeGrid();
	S3mReleaseJoypad();
	CCustomBlurDialogBase::OnDestroy();
	// 閉じたら矢印ホットキー再登録を促す
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void OpenSoft3DMazeModeless(CWnd* p)
{
	extern void CloseSoft3DRaceIfOpen();
	CloseSoft3DRaceIfOpen();
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

BOOL IsSoft3DMazeOpen()
{
	return (g_s3m && g_s3m->GetSafeHwnd() && ::IsWindow(g_s3m->GetSafeHwnd())
		&& g_s3m->IsWindowVisible()) ? TRUE : FALSE;
}

BOOL IsSoft3DMazeActive()
{
	// 迷路ウィンドウが開いていれば操作中扱い（SPACE再生トグル防止・矢印ホットキー抑制）
	return IsSoft3DMazeOpen();
}

BOOL Soft3DMazePreTranslate(MSG* pMsg)
{
	if (!IsSoft3DMazeOpen() || !pMsg || !g_s3m)
		return FALSE;
	return g_s3m->HandleAccelMessage(pMsg);
}

void Soft3DMazeOnTimerp()
{
	if (!g_s3m || !g_s3m->GetSafeHwnd() || !::IsWindow(g_s3m->GetSafeHwnd()))
		return;
	if (!g_s3m->IsWindowVisible() || g_s3m->IsIconic())
		return;
	g_s3m->TickFrame();
}
