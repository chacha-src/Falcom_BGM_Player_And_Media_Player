// Soft3DRaceDlg.cpp — Soft3D aerial race (Catmull-Rom power band / cute bird-ships)
#include "stdafx.h"
#include "ogg.h"
#include "Soft3DRaceDlg.h"
#include "Soft3DMazeDlg.h"
#include "CMediaPlayerDlg.h"
#include "CCustomPopupMenu.h"
#include "oggDlg.h"
#include <math.h>
#include <new>
#include <gdiplus.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "Soft3DRaceNames.inc"

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

struct S3RFloat4 { float x, y, z, w; };
struct S3RMat { float m[16]; };
struct S3RVertex { float x,y,z, nx,ny,nz, u,v, r,g,b,a; };
struct S3RHudVertex { float x,y, u,v, r,g,b,a; }; // uv.x<0 = solid color only
struct S3RFrameCB {
	S3RMat viewProj;
	S3RMat lightVP;
	S3RFloat4 eyePos, fogParams, dofParams, screenSize, misc, lightDir;
};

static S3RMat S3rMatMul(const S3RMat& a, const S3RMat& b)
{
	S3RMat r = {};
	for (int i=0;i<4;i++) for (int j=0;j<4;j++)
		for (int k=0;k<4;k++) r.m[i*4+j] += a.m[i*4+k]*b.m[k*4+j];
	return r;
}
static S3RMat S3rPerspective(float fovy, float aspect, float zn, float zf)
{
	S3RMat r = {};
	const float y = 1.f / tanf(fovy*.5f), x = y / aspect;
	r.m[0]=x; r.m[5]=y; r.m[10]=zf/(zf-zn); r.m[11]=1.f; r.m[14]=-zn*zf/(zf-zn);
	return r;
}
static S3RMat S3rOrtho(float l,float rgt,float b,float t,float zn,float zf)
{
	S3RMat r = {};
	r.m[0]=2.f/(rgt-l); r.m[5]=2.f/(t-b); r.m[10]=1.f/(zf-zn);
	r.m[12]=-(rgt+l)/(rgt-l); r.m[13]=-(t+b)/(t-b); r.m[14]=-zn/(zf-zn); r.m[15]=1.f;
	return r;
}
static S3RMat S3rLookAt(float ex,float ey,float ez,float ax,float ay,float az,float ux,float uy,float uz)
{
	float zx=ax-ex, zy=ay-ey, zz=az-ez;
	float zl=sqrtf(zx*zx+zy*zy+zz*zz); if(zl<1e-6f) zl=1e-6f; zx/=zl; zy/=zl; zz/=zl;
	float xx=uy*zz-uz*zy, xy=uz*zx-ux*zz, xz=ux*zy-uy*zx;
	float xl=sqrtf(xx*xx+xy*xy+xz*xz); if(xl<1e-6f) xl=1e-6f; xx/=xl; xy/=xl; xz/=xl;
	float yx=zy*xz-zz*xy, yy=zz*xx-zx*xz, yz=zx*xy-zy*xx;
	S3RMat r = {};
	r.m[0]=xx; r.m[1]=yx; r.m[2]=zx;
	r.m[4]=xy; r.m[5]=yy; r.m[6]=zy;
	r.m[8]=xz; r.m[9]=yz; r.m[10]=zz;
	r.m[12]=-(ex*xx+ey*xy+ez*xz);
	r.m[13]=-(ex*yx+ey*yy+ez*yz);
	r.m[14]=-(ex*zx+ey*zy+ez*zz); r.m[15]=1.f;
	return r;
}

#define S3R_RELEASE(p) do { if (p) { (p)->Release(); (p)=NULL; } } while(0)

static float S3rNormAngle(float a)
{
	while (a > (float)M_PI) a -= (float)(M_PI * 2.0);
	while (a < -(float)M_PI) a += (float)(M_PI * 2.0);
	return a;
}
static float S3rClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float S3rLerp(float a, float b, float t) { return a + (b - a) * t; }
static float S3rSaturate(float v) { return S3rClamp(v, 0.f, 1.f); }
static void S3rNorm3(float& x, float& y, float& z)
{
	float l = sqrtf(x*x+y*y+z*z); if (l < 1e-6f) { x=0;y=1;z=0; return; }
	x/=l;y/=l;z/=l;
}
static DWORD S3rRand(DWORD& rng)
{
	rng = rng * 1664525u + 1013904223u;
	return rng;
}
static float S3rRand01(DWORD& rng) { return (S3rRand(rng) & 0xFFFFFF) / 16777215.f; }

static void S3rEqBump(int band, int delta)
{
	if (band < 0 || band > 14) return;
	int v = savedata.eq[band] + delta;
	if (v < 0) v = 0; if (v > 200) v = 200;
	savedata.eq[band] = v; savedata.eqsoundeq = 9;
}
static void S3rEqFlatten(int step)
{
	for (int i = 0; i < 15; i++) {
		int v = savedata.eq[i];
		if (v > 100) { v -= step; if (v < 100) v = 100; }
		else if (v < 100) { v += step; if (v > 100) v = 100; }
		savedata.eq[i] = v;
	}
	savedata.eqsoundeq = 9;
}
static void S3rSetPitchPos(int pos)
{
	if (pos < 0) pos = 0; if (pos > 400) pos = 400;
	pitch = pos;
	if (og && ::IsWindow(og->GetSafeHwnd())) og->m_pitch_sl.SetPos(pos);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_pitch.GetSafeHwnd()) mp->m_pitch.SetPos(pos);
}
static void S3rNudgeVolPct(int delta)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int p = og->m_sl.GetPos() + delta * 1000;
	if (p < 0) p = 0; if (p > 100000) p = 100000;
	og->m_sl.SetPos(p);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_vol.GetSafeHwnd()) {
		int v = p / 1000; if (v < 0) v = 0; if (v > 100) v = 100;
		mp->m_vol.SetPos(v, FALSE);
	}
}
static void S3rNudgeReverb(int delta)
{
	int v = savedata.eq_reverb + delta;
	if (v < 0) v = 0; if (v > 100) v = 100;
	savedata.eq_reverb = v;
}

// ---- DirectInput joypad (Qullusrent3-style UpdateJoypadState) ----
struct S3rJoyState {
	float lx, ly, rx, ry;
	float lt, rt;
	int buttons; // bit0=A bit1=B bit2=X bit3=Y bit4=LB bit5=RB bit6=Back bit7=Start
	int hat; // -1 none, 0 up, 1 ur, 2 r, ...
	int connected;
};
static IDirectInput8W* g_s3rDi = NULL;
static IDirectInputDevice8W* g_s3rPad = NULL;
static BOOL g_s3rDiTried = FALSE;

static BOOL CALLBACK S3rEnumPads(const DIDEVICEINSTANCEW* inst, void*)
{
	if (FAILED(g_s3rDi->CreateDevice(inst->guidInstance, &g_s3rPad, NULL)))
		return DIENUM_CONTINUE;
	return DIENUM_STOP;
}
static void S3rEnsureJoypad()
{
	if (g_s3rDiTried) return;
	g_s3rDiTried = TRUE;
	if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&g_s3rDi, NULL)))
		return;
	g_s3rDi->EnumDevices(DI8DEVCLASS_GAMECTRL, S3rEnumPads, NULL, DIEDFL_ATTACHEDONLY);
	if (!g_s3rPad) return;
	g_s3rPad->SetDataFormat(&c_dfDIJoystick2);
	HWND hw = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : NULL; g_s3rPad->SetCooperativeLevel(hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
	DIPROPRANGE range = {};
	range.diph.dwSize = sizeof(range);
	range.diph.dwHeaderSize = sizeof(range.diph);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1000; range.lMax = 1000;
	range.diph.dwObj = DIJOFS_X; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_Y; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RX; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RY; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	DIPROPDWORD dead = {};
	dead.diph.dwSize = sizeof(dead);
	dead.diph.dwHeaderSize = sizeof(dead.diph);
	dead.diph.dwHow = DIPH_DEVICE;
	dead.dwData = 1200;
	g_s3rPad->SetProperty(DIPROP_DEADZONE, &dead.diph);
	g_s3rPad->Acquire();
}
static float S3rAxisN(LONG v)
{
	float f = (float)v / 1000.f;
	if (fabsf(f) < 0.18f) return 0.f;
	return S3rClamp(f, -1.f, 1.f);
}
static void UpdateJoypadState(S3rJoyState& out)
{
	memset(&out, 0, sizeof(out));
	out.hat = -1;
	S3rEnsureJoypad();
	if (!g_s3rPad) return;
	HRESULT hr = g_s3rPad->Poll();
	if (FAILED(hr)) { g_s3rPad->Acquire(); hr = g_s3rPad->Poll(); }
	DIJOYSTATE2 st = {};
	hr = g_s3rPad->GetDeviceState(sizeof(st), &st);
	if (FAILED(hr)) return;
	out.connected = 1;
	out.lx = S3rAxisN(st.lX);
	out.ly = S3rAxisN(st.lY);
	out.rx = S3rAxisN(st.lRx);
	out.ry = S3rAxisN(st.lRy);
	out.lt = S3rClamp((float)st.lZ / 1000.f, 0.f, 1.f);
	// Some pads report triggers on slider / Rz
	float rt = S3rClamp((float)st.lRz / 1000.f, 0.f, 1.f);
	if (rt < 0.01f && st.rglSlider[0] > 0) rt = S3rClamp(st.rglSlider[0] / 1000.f, 0.f, 1.f);
	out.rt = rt;
	for (int i = 0; i < 8; i++) if (st.rgbButtons[i] & 0x80) out.buttons |= (1 << i);
	DWORD pov = st.rgdwPOV[0];
	if (pov != 0xFFFFFFFF && (LOWORD(pov) != 0xFFFF))
		out.hat = (int)((pov + 2250) / 4500) % 8;
}
static void S3rReleaseJoypad()
{
	if (g_s3rPad) { g_s3rPad->Unacquire(); g_s3rPad->Release(); g_s3rPad = NULL; }
	if (g_s3rDi) { g_s3rDi->Release(); g_s3rDi = NULL; }
	g_s3rDiTried = FALSE;
}

static const float kCraftColors[12][3] = {
	{1.00f,.55f,.70f},{.45f,.85f,1.00f},{1.00f,.85f,.40f},{.55f,1.00f,.65f},
	{.85f,.55f,1.00f},{1.00f,.65f,.40f},{.70f,.90f,1.00f},{1.00f,.45f,.55f},
	{.60f,.95f,.80f},{.95f,.75f,.95f},{.40f,.70f,1.00f},{1.00f,.95f,.70f}
};

class CS3rHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_S3R_HELP };
	explicit CS3rHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
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
static CS3rHelpDlg* g_s3rHelp = nullptr;

BEGIN_MESSAGE_MAP(CS3rHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CS3rHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE); SetIcon(nullptr, FALSE);
	SetWindowText(LL14(
		L"Soft3D空中レースガイド", L"Soft3D aerial race guide", L"Guide course aérienne Soft3D", L"Guida gara aerea Soft3D",
		L"Guía carrera aérea Soft3D", L"Soft3D 공중 레이스 가이드", L"Soft3D 空中竞速指南", L"دليل سباق Soft3D الجوي",
		L"Руководство Soft3D-гонки", L"Soft3D-Luftrennen-Anleitung", L"Guia corrida aérea Soft3D", L"Soft3D-luchtracegids",
		L"Przewodnik Soft3D wyścigu", L"Soft3D hava yarışı kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CS3rHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_s3rHelp == this) g_s3rHelp = nullptr;
	delete this;
}
BOOL CS3rHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CS3rHelpDlg::OnPaint()
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

	dc.SetTextColor(RGB(55, 45, 95));
	dc.TextOut(L, y, LL14(L"Soft3D 空中レース — 操作と画面の見方", L"Soft3D aerial race — controls and visuals",
		L"Course aérienne Soft3D — commandes et visuels", L"Gara aerea Soft3D — comandi e visuali",
		L"Carrera aérea Soft3D — controles y visuales", L"Soft3D 공중 레이스 — 조작과 화면", L"Soft3D 空中竞速 — 操作与画面",
		L"سباق Soft3D الجوي — التحكم والعرض", L"Воздушная гонка Soft3D — управление и вид", L"Soft3D-Luftrennen — Steuerung und Ansicht",
		L"Corrida aérea Soft3D — controles e visuais", L"Soft3D-luchtrace — bediening en beeld",
		L"Wyścig powietrzny Soft3D — sterowanie i widok", L"Soft3D hava yarışı — kontroller ve görüntü"));
	y += lh + 6;

	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, contentW, min(160, max(120, rc.Height() / 4)),
		CCC_HELPDEMO_KRACE);

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"凡例（Soft3D と説明）", L"Legend (Soft3D & description)", L"Légende (Soft3D et description)", L"Legenda (Soft3D e descrizione)", L"Leyenda (Soft3D y descripción)",
		L"범례(Soft3D와 설명)", L"图例（Soft3D 与说明）", L"Legend (Soft3D & description)", L"Легенда (Soft3D и описание)", L"Legende (Soft3D & Beschreibung)", L"Legenda (Soft3D e descrição)", L"Legenda (Soft3D en uitleg)",
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
	legendRow(RGB(140, 220, 255),
		LL14(L"パワーバンド（光帯）", L"Power band (light ribbon)", L"Bande de puissance", L"Fascia di potenza", L"Banda de potencia", L"파워 밴드", L"能量光带", L"Power band", L"Силовая лента", L"Powerband", L"Faixa de potência", L"Powerband", L"Pas mocy", L"Güç bandı"),
		LL14(L"コース本体。帯上で推進力／中央でHP回復。帯外フリー走行可。推進力0で離脱地点へ復帰", L"On-band: thrust regen / center HP. Off-band free flight; thrust 0 = reset to exit", L"Sur bande: poussée; hors bande libre; poussée 0 = retour", L"Sulla fascia: spinta; fuori libero; spinta 0 = reset", L"En banda: empuje; fuera libre; empuje 0 = reinicio", L"밴드 위 추진력/중앙 HP. 밖 자유비행. 추진력0이면 복귀", L"带上恢复推进力/中央回HP；带外自由飞；推进力0回脱离点", L"Thrust on-band; free off-band; thrust 0 = reset", L"Тяга на ленте; вне свободно; тяга 0 = возврат", L"Schub auf Band; off-band frei; Schub 0 = Reset", L"Empuxo na faixa; fora livre; empuxo 0 = reset", L"Stuwkracht op band; buiten vrij; 0 = reset", L"Ciąg na pasie; poza wolno; ciąg 0 = reset", L"Bantta itki; dışı serbest; itki 0 = dönüş"));
	legendRow(RGB(55, 120, 70),
		LL14(L"地形（床）", L"Terrain (ground)", L"Terrain (sol)", L"Terreno (suolo)", L"Terreno (suelo)", L"지형(바닥)", L"地形（地面）", L"Terrain", L"Рельеф", L"Gelände", L"Terreno", L"Terrein", L"Teren", L"Arazi"),
		LL14(L"緑などのうねる地面。接触するとダメージ", L"Rolling ground; contact damages HP", L"Sol vallonné; contact = dégâts", L"Suolo; contatto = danni", L"Suelo; contacto = daño", L"기복 바닥, 접촉 시 데미지", L"起伏地面，碰触受伤", L"Hit ground = damage", L"Касание = урон", L"Bodenkontakt = Schaden", L"Contato = dano", L"Raak = schade", L"Dotyk = obrażenia", L"Temas = hasar"));
	legendRow(RGB(50, 160, 70),
		LL14(L"樹木・建造物／コース障害", L"Trees / buildings / on-path hazards", L"Arbres / bâtiments / obstacles", L"Alberi / edifici / ostacoli", L"Árboles / edificios / obstáculos", L"나무·건물·코스 장애", L"树木/建筑/赛道障碍", L"Trees/buildings/hazards", L"Деревья/здания/препятствия", L"Bäume/Gebäude/Hindernisse", L"Árvores/prédios/obstáculos", L"Bomen/gebouwen/hindernissen", L"Drzewa/budynki/przeszkody", L"Ağaç/bina/engel"),
		LL14(L"コース外の景色＋帯上のスラローム／通過枠。衝突でHP減少", L"Off-course décor + on-band slalom/gates; collisions cost HP", L"Décor hors piste + slalom/portiques; collisions = HP", L"Decorazioni + slalom/cancelli; collisioni = HP", L"Decoración + slalom/arcos; colisiones = HP", L"코스 밖 장식 + 밴드 슬라럼/게이트, 충돌 시 HP↓", L"赛道外装饰＋带上绕桩/门框，碰撞扣HP", L"Decor + on-path gates; collisions hurt", L"Декор + ворота на трассе; столкновения", L"Dekor + Tore auf Kurs; Kollisionen", L"Decor + portões na pista; colisões", L"Decor + poorten op baan; botsingen", L"Dekor + bramki na trasie; kolizje", L"Dekor + pistteki kapılar; çarpışma"));
	legendRow(RGB(255, 170, 90),
		LL14(L"自機（鳥型クラフト）", L"Your craft (bird-ship)", L"Votre appareil", L"Il tuo craft", L"Tu nave", L"기체(새형)", L"自机（鸟型）", L"Your craft", L"Ваш корабль", L"Ihr Craft", L"Sua nave", L"Jouw craft", L"Twój craft", L"Aracınız"),
		LL14(L"斜め上カメラが追従。マウスで向き、LMB加速／RMBブレーキ", L"Chase cam from above-behind; mouse steers; LMB accel / RMB brake", L"Caméra chase; souris = direction", L"Camera chase; mouse = sterzo", L"Cámara chase; ratón = dirección", L"斜め위 카메라 추종, 마우스로 조향", L"斜上方追从相机；鼠标转向", L"Chase cam; mouse steer", L"Камера сзади-сверху", L"Chase-Kamera", L"Câmera chase", L"Chase-camera", L"Kamera z góry", L"Üst-arka kamera"));
	legendRow(RGB(80, 230, 130),
		LL14(L"アイテム球", L"Item orbs", L"Sphères d'objets", L"Sfere oggetto", L"Orbes de objeto", L"아이템 구", L"道具球", L"Item orbs", L"Сферы предметов", L"Item-Kugeln", L"Orbes de item", L"Item-bollen", L"Kule przedmiotów", L"Öğe küreleri"),
		LL14(L"触れると再生テンポ／ピッチ等＋レース効果（加速・霧など）", L"Touch to tweak playback + race buffs (boost, fog, …)", L"Toucher = lecture + buffs course", L"Tocco = riproduzione + buff", L"Tocar = reproducción + buffs", L"접촉 시 재생+레이스 버프", L"触碰改播放并带竞速效果", L"Playback + race effects", L"Воспроизведение + баффы", L"Playback + Buffs", L"Reprodução + buffs", L"Weergave + buffs", L"Odtwarzanie + buffy", L"Oynatma + buff"));
	legendRow(RGB(140, 200, 255),
		LL14(L"デモ走行（生成〜スタート前）", L"Demo (after generate, before Start)", L"Démo (après Générer)", L"Demo (dopo Genera)", L"Demo (tras Generar)", L"데모(생성~시작 전)", L"演示（生成到开始前）", L"Demo until Start", L"Демо до Старта", L"Demo bis Start", L"Demo até Iniciar", L"Demo tot Start", L"Demo do Start", L"Demo → Başlat"),
		LL14(L"俯瞰でコース全体を見せ、自機もAI走行。マウスで視点。スタートで本番", L"Overview of whole course; your craft is AI too. Mouse pans. Start = race", L"Vue d'ensemble; votre craft en IA. Souris=vue. Démarrer=course", L"Vista d'insieme; anche il tuo craft in IA. Mouse=vista. Avvia=gara", L"Vista general; tu nave también IA. Ratón=vista. Iniciar=carrera", L"전체 조감+자기 AI. 마우스 시점. 시작=본경기", L"俯瞰全图，自机也AI跑。鼠标转视角。开始进正式赛", L"Overview + AI you; mouse pans; Start races", L"Обзор; ваш аппарат ИИ; мышь; Старт=гонка", L"Überblick; Ihr Craft KI; Maus; Start=Rennen", L"Visão geral; sua nave IA; mouse; Iniciar=corrida", L"Overzicht; jouw craft AI; muis; Start=race", L"Przegląd; Twój craft AI; mysz; Start=wyścig", L"Genel bakış; gemin AI; fare; Başlat=yarış"));
	legendRow(RGB(255, 230, 140),
		LL14(L"5カウント／GO", L"5-count / GO", L"Compte à rebours / GO", L"Conto alla rovescia / VIA", L"Cuenta atrás / ¡YA!", L"5카운트/GO", L"5倒计时/开始", L"Countdown / GO", L"Отсчёт / СТАРТ", L"Countdown / LOS", L"Contagem / JÁ", L"Aftellen / START", L"Odliczanie / START", L"Geri sayım / BAŞLA"),
		LL14(L"スタート後に大きく表示。GOでレース開始", L"Large overlay after Start; GO begins the race", L"Grand overlay après Démarrer", L"Overlay grande dopo Avvia", L"Overlay grande tras Iniciar", L"시작 후 크게 표시, GO로 레이스", L"开始后大字显示，GO开赛", L"Big overlay; GO starts", L"Крупный оверлей", L"Großes Overlay", L"Overlay grande", L"Grote overlay", L"Duży overlay", L"Büyük kaplama"));
	y += 6;

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"操作", L"Controls", L"Commandes", L"Comandi", L"Controles",
		L"조작", L"操作", L"التحكم", L"Управление", L"Steuerung", L"Controles", L"Bediening", L"Sterowanie", L"Kontroller"));
	y += lh + 2;
	dc.SetTextColor(RGB(65, 65, 80));
	line(LL14(L"マウス: 移動=機体のヨー／ピッチ　LMB=加速　RMB=ブレーキ　ホイール=ズーム　MMB=後方視",
		L"Mouse: move=craft yaw/pitch  LMB=accel  RMB=brake  wheel=zoom  MMB=lookback",
		L"Souris : move=lacet/tangage  LMB=accél  RMB=frein  molette=zoom  MMB=recul",
		L"Mouse: move=yaw/pitch  LMB=accel  RMB=freno  rotella=zoom  MMB=dietro",
		L"Ratón: move=yaw/pitch  LMB=acel  RMB=freno  rueda=zoom  MMB=atrás",
		L"마우스: 이동=요/피치  LMB=가속  RMB=브레이크  휠=줌  MMB=후방",
		L"鼠标：移动=偏航/俯仰  左键=加速  右键=刹车  滚轮=缩放  中键=后视",
		L"Mouse: move=yaw/pitch  LMB=accel  RMB=brake  wheel=zoom  MMB=lookback",
		L"Мышь: движение=yaw/pitch  ЛКМ=газ  ПКМ=тормоз  колесо=зум  СКМ=назад",
		L"Maus: Bewegen=Gier/Nick  LMB=Gas  RMB=Bremse  Rad=Zoom  MMB=Rückblick",
		L"Mouse: mover=yaw/pitch  LMB=acel  RMB=freio  roda=zoom  MMB=olhar atrás",
		L"Muis: bewegen=yaw/pitch  LMB=gas  RMB=rem  wiel=zoom  MMB=achteruit",
		L"Mysz: ruch=yaw/pitch  LMB=gaz  RMB=hamulec  kółko=zoom  MMB=wstecz",
		L"Fare: hareket=yaw/pitch  LMB=gaz  RMB=fren  teker=zoom  MMB=geri bak"));
	line(LL14(L"パッド: 左ステック=操舵　RT/A=加速　LT/B=ブレーキ　ハット=代替操舵　右ステック=カメラオフセット",
		L"Pad: left stick=steer  RT/A=accel  LT/B=brake  hat=alt steer  right stick=camera offset",
		L"Manette : stick G=diriger  RT/A=accél  LT/B=frein  chapeau=alt  stick D=caméra",
		L"Pad: stick SX=sterzo  RT/A=accel  LT/B=freno  hat=alt  stick DX=camera",
		L"Mando: stick izq=girar  RT/A=acel  LT/B=freno  hat=alt  stick der=cámara",
		L"패드: 왼쪽 스틱=조향  RT/A=가속  LT/B=브레이크  햇=대체  오른쪽 스틱=카메라",
		L"手柄：左摇杆=转向  RT/A=加速  LT/B=刹车  十字=备用  右摇杆=相机偏移",
		L"Pad: left stick=steer  RT/A=accel  LT/B=brake  hat=alt  right stick=camera",
		L"Геймпад: левый стик=руль  RT/A=газ  LT/B=тормоз  hat=alt  правый стик=камера",
		L"Pad: linker Stick=Lenken  RT/A=Gas  LT/B=Bremse  Hut=alt  rechter Stick=Kamera",
		L"Controle: stick esq=dirigir  RT/A=acel  LT/B=freio  hat=alt  stick dir=câmera",
		L"Pad: linker stick=sturen  RT/A=gas  LT/B=rem  hat=alt  rechter stick=camera",
		L"Pad: lewy stick=ster  RT/A=gaz  LT/B=hamulec  hat=alt  prawy stick=kamera",
		L"Pad: sol çubuk=direksiyon  RT/A=gaz  LT/B=fren  hat=alt  sağ çubuk=kamera"));
	line(LL14(L"上下反転コンボ: マウス上下とパッド上下のピッチを反転（savedataに保存）",
		L"Invert-Y combo: flips mouse/pad pitch (saved in savedata)",
		L"Combo inversion Y : inverse le tangage souris/manette (sauvé)",
		L"Combo inverti Y: inverte pitch mouse/pad (salvato)",
		L"Combo invertir Y: invierte pitch ratón/mando (guardado)",
		L"상하 반전 콤보: 마우스/패드 피치 반전(저장됨)",
		L"上下翻转下拉：翻转鼠标/手柄俯仰（会保存）",
		L"Invert-Y combo flips mouse/pad pitch (persisted)",
		L"Комбо Invert-Y: инверсия pitch мыши/пада (сохраняется)",
		L"Invert-Y-Kombo: Maus/Pad-Pitch umkehren (gespeichert)",
		L"Combo inverter Y: inverte pitch mouse/pad (salvo)",
		L"Invert-Y-combo: muis/pad-pitch omkeren (opgeslagen)",
		L"Combo Invert-Y: odwraca pitch myszy/pada (zapisywane)",
		L"Y ters çevir: fare/pad pitch ters (kaydedilir)"));
	line(LL14(L"帯外はフリー走行でショートカット可。推進力ゲージが0になると動けず離脱地点へ復帰（HPはそのまま）。障害物はHPのみ減らす。",
		L"Off-band free flight for shortcuts. Thrust gauge 0 = reset to exit (HP unchanged). Obstacles only cut HP.",
		L"Hors bande libre. Poussée 0 = retour (PV inchangé). Obstacles = PV seulement.",
		L"Fuori libero. Spinta 0 = reset (HP invariato). Ostacoli = solo HP.",
		L"Fuera libre. Empuje 0 = reinicio (HP igual). Obstáculos = solo HP.",
		L"밴드 밖 자유 비행. 추진력 0이면 복귀(HP 유지). 장애물은 HP만 감소.",
		L"带外可抄近路；推进力0回脱离点（HP不变）。障碍只扣HP。",
		L"Off-band shortcuts; thrust 0 = reset (HP kept). Obstacles hit HP only.",
		L"Вне ленты свободно; тяга 0 = возврат (HP без изменений).",
		L"Off-band frei; Schub 0 = Reset (HP bleibt). Hindernisse nur HP.",
		L"Fora livre; empuxo 0 = reset (HP igual). Obstáculos só HP.",
		L"Buiten vrij; stuwkracht 0 = reset (HP blijft). Obstakels alleen HP.",
		L"Poza pasem wolno; ciąg 0 = reset (HP bez zmian). Przeszkody tylko HP.",
		L"Bant dışı serbest; itki 0 = dönüş (HP aynı). Engeller yalnız HP."));
	line(LL14(L"右のミニマップ下に順位パネル（名前・順位・直近LAPタイム）。自機行だけ色が違う。ゴール後は表彰台で1〜3位を表示。",
		L"Standings under the minimap (name/rank/recent LAP times). Your row is tinted. After finish: podium 1–3.",
		L"Classement sous la minimap (nom/rang/tours). Votre ligne est teintée. Après: podium 1–3.",
		L"Classifica sotto minimap (nome/grado/giri). La tua riga è evidenziata. Poi podio 1–3.",
		L"Clasificación bajo el minimapa (nombre/puesto/vueltas). Tu fila resalta. Luego podio 1–3.",
		L"미니맵 아래 순위판(이름·순위·최근 랩). 자행만 색 다름. 종료 후 시상대 1~3.",
		L"小地图下为排名板（名字/名次/近几圈）。自机行异色。完赛后领奖台1–3名。",
		L"Standings under minimap; your row highlighted. Podium 1–3 after finish.",
		L"Таблица под миникартой; ваша строка выделена. Подиум 1–3 после финиша.",
		L"Rangliste unter der Minimap; Ihre Zeile hervorgehoben. Danach Podium 1–3.",
		L"Classificação sob o minimapa; sua linha destacada. Pódio 1–3 após a chegada.",
		L"Stand onder minimap; jouw rij gekleurd. Daarna podium 1–3.",
		L"Tabela pod minimapą; Twój wiersz wyróżniony. Potem podium 1–3.",
		L"Minimapi altında sıralama; satırın farklı. Bitince podyum 1–3."));
	line(LL14(L"カメラは左右・上下とも遅れて追従し、3D酔いを抑えます。コース上下は所々の突起のみ。",
		L"Camera lags on both axes to reduce motion sickness. Course height uses sparse bumps only.",
		L"Caméra en retard sur les axes pour moins de mal des transports. Relief par bosses locales.",
		L"Camera in ritardo sugli assi per ridurre il motion sickness. Altezze a bump sparsi.",
		L"Cámara con retraso en ambos ejes contra el mareo. Altura con protuberancias puntuales.",
		L"카메라는 좌우·상하 모두 지연 추종해 멀미를 줄입니다. 고저는 드문 돌기만.",
		L"相机左右上下均延迟跟随以减轻晕动。赛道高低仅为局部凸起。",
		L"Lagging chase cam reduces motion sickness; sparse elevation bumps only.",
		L"Камера с запаздыванием снижает укачивание; редкие перепады высоты.",
		L"Nachlaufende Kamera mindert Motion Sickness; nur lokale Höhenhügel.",
		L"Câmera atrasada reduz enjoo; relevo só com saliências pontuais.",
		L"Nalopende camera vermindert misselijkheid; alleen lokale bobbels.",
		L"Opóźniona kamera zmniejsza mdłości; tylko lokalne wypukłości.",
		L"Gecikmeli kamera mide bulantısını azaltır; seyrek tümsekler."));
	line(LL14(L"テーマで森〜雲の庭まで変化。コースは枝下／トンネル／開けた区間を交互に通る。生成で再抽選。",
		L"Themes from forest to cloud garden. Course weaves under canopy/tunnels then opens up. Gen reshuffles.",
		L"Thèmes forêt→jardin de nuages. Course sous branches/tunnels puis ouvert. Générer = nouveau.",
		L"Temi foresta→giardino di nubi. Pista tra rami/tunnel poi aperta. Genera = nuovo.",
		L"Temas bosque→jardín de nubes. Pista bajo ramas/túneles y tramos abiertos. Generar = nuevo.",
		L"테마: 숲~구름 정원. 코스는 가지 아래/터널/개방 구간 교차. 생성=재추첨.",
		L"主题从森林到云之庭。赛道穿枝下/隧道与开阔段。生成可重抽。",
		L"Themes forest→cloud garden; weave then open. Gen reshuffles.",
		L"Темы лес→облачный сад. Трасса под кронами/туннелями. Создать = новый.",
		L"Themen Wald→Wolkengarten. Strecke unter Kronen/Tunneln. Erzeugen = neu.",
		L"Temas floresta→jardim de nuvens. Curso sob copas/túneis. Gerar = novo.",
		L"Thema's bos→wolken tuin. Parcours onder kronen/tunnels. Genereren = nieuw.",
		L"Motywy las→ogród chmur. Tor pod koronami/tunelami. Generuj = nowy.",
		L"Temalar orman→bulut bahçesi. Parkur dal/tünel ve açık. Oluştur = yeni."));
	line(LL14(L"右クリック（ステータス／枠）: リスタート・ミニマップ・アイテム種類など。ビュー上はRMB=ブレーキのみ。",
		L"Right-click (status/chrome): restart, minimap, item masks. On the view, RMB=brake only.",
		L"Clic droit (statut/cadre) : redémarrer, minimap, objets. Sur la vue, RMB=frein.",
		L"Clic destro (stato/cornice): riavvio, minimap, oggetti. Sulla vista RMB=freno.",
		L"Clic derecho (estado/marco): reinicio, minimapa, objetos. En la vista RMB=freno.",
		L"우클릭(상태/프레임): 재시작·미니맵·아이템. 뷰에서는 RMB=브레이크만.",
		L"右键（状态/边框）：重启、小地图、道具。视图内右键仅为刹车。",
		L"Right-click chrome: restart/minimap/items. On view RMB=brake.",
		L"ПКМ по рамке: рестарт/мини-карта/предметы. На виде ПКМ=тормоз.",
		L"Rechtsklick am Rahmen: Neustart/Minimap/Items. In der Ansicht RMB=Bremse.",
		L"Clique direito no quadro: reiniciar/minimapa/itens. Na vista RMB=freio.",
		L"Rechtsklik op kader: herstart/minimap/items. Op view RMB=rem.",
		L"PPM na ramce: restart/minimapa/przedmioty. Na widoku RMB=hamulec.",
		L"Çerçevede sağ tık: yeniden/minimapi/öğeler. Görüntüde RMB=fren."));
	line(LL14(L"迷路 Soft3D と同時には開けません。AI／敵機数／長さ／周回／テーマはコンボで変更（保存されます）。",
		L"Cannot open with Soft3D maze at once. AI/opponents/length/laps/theme combos persist.",
		L"Pas avec le labyrinthe Soft3D. Combos AI/adversaires/longueur/tours/thème sauvés.",
		L"Non insieme al labirinto Soft3D. Combo AI/avversari/lunghezza/giri/tema salvate.",
		L"No a la vez con el laberinto Soft3D. Combos IA/rivales/largo/vueltas/tema se guardan.",
		L"미로 Soft3D와 동시 불가. AI/적/길이/랩/테마 콤보 저장.",
		L"不可与 Soft3D 迷宫同时开。AI/对手/长度/圈数/主题会保存。",
		L"Not with Soft3D maze. Combos persist.",
		L"Не вместе с лабиринтом Soft3D. Комбо сохраняются.",
		L"Nicht zusammen mit Soft3D-Labyrinth. Kombos werden gespeichert.",
		L"Não junto com o labirinto Soft3D. Combos são salvos.",
		L"Niet samen met Soft3D-doolhof. Combo's worden bewaard.",
		L"Nie razem z labiryntem Soft3D. Combo są zapisywane.",
		L"Soft3D labirent ile birlikte değil. Kombolar kaydedilir."));
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

static CSoft3DRaceDlg* g_s3r = NULL;

// ---- view ----
IMPLEMENT_DYNAMIC(CS3rView, CCustomStatic)

BEGIN_MESSAGE_MAP(CS3rView, CCustomStatic)
	ON_WM_PAINT()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
END_MESSAGE_MAP()

CS3rView::CS3rView()
	: m_ready(FALSE), m_vw(0), m_vh(0), m_dev(NULL), m_imm(NULL), m_swap(NULL), m_bbRtv(NULL)
	, m_dsTex(NULL), m_dsv(NULL), m_dsSrv(NULL), m_sceneTex(NULL), m_sceneRtv(NULL), m_sceneSrv(NULL)
	, m_postTex(NULL), m_postRtv(NULL), m_postSrv(NULL), m_shadowTex(NULL), m_shadowDsv(NULL), m_shadowSrv(NULL)
	, m_vsTess(NULL), m_hsTess(NULL), m_dsTess(NULL)
	, m_psBand(NULL), m_vsSolid(NULL), m_psSolid(NULL), m_psCraft(NULL), m_vsHud(NULL), m_psHud(NULL), m_vsPost(NULL)
	, m_psSsr(NULL), m_psDof(NULL), m_psFinal(NULL), m_csNoise(NULL), m_ilPatch(NULL), m_ilSolid(NULL), m_ilHud(NULL)
	, m_cbFrame(NULL), m_vbDyn(NULL), m_vbHud(NULL), m_vbDynBytes(24*1024*1024), m_vbHudBytes(512*1024)
	, m_cpuDynScratch(NULL), m_cpuDynScratchBytes(0), m_cpuHudScratch(NULL), m_cpuHudScratchBytes(0)
	, m_texBand(NULL), m_srvBand(NULL), m_texEnv(NULL), m_srvEnv(NULL), m_texCraft(NULL), m_srvCraft(NULL)
	, m_texNoise(NULL), m_srvNoise(NULL), m_uavNoise(NULL)
	, m_texClear(NULL), m_srvClear(NULL), m_clearTexW(0), m_clearTexH(0)
	, m_texHud(NULL), m_srvHud(NULL), m_hudTexW(0), m_hudTexH(0)
	, m_texStand(NULL), m_srvStand(NULL), m_standTexW(0), m_standTexH(0)
	, m_sampLin(NULL), m_sampPoint(NULL), m_sampCmp(NULL)
	, m_rsSolid(NULL), m_rsShadow(NULL), m_rsNoCull(NULL), m_dssWrite(NULL), m_dssRead(NULL), m_dssOff(NULL)
	, m_bsOpaque(NULL), m_bsAlpha(NULL), m_bsAdd(NULL)
	, m_dxFailStage(0), m_dxFailHr(S_OK)
{
	for (int i = 0; i < S3R_THEME_N; i++) { m_texTheme[i] = NULL; m_srvTheme[i] = NULL; }
}
CS3rView::~CS3rView() { ReleaseDx(); }

BOOL CS3rView::CreateShaders()
{
	static const char* hlsl =
		"cbuffer F:register(b0){row_major float4x4 VP;row_major float4x4 LightVP;float4 Eye;float4 Fog;float4 Dof;float4 Screen;float4 Misc;float4 LightDir;}"
		"Texture2D T0:register(t0);Texture2D T1:register(t1);Texture2D Depth:register(t2);TextureCube Env:register(t3);Texture2D ShadowMap:register(t4);Texture2D NoiseMap:register(t5);"
		"SamplerState SL:register(s0);SamplerState SP:register(s1);SamplerComparisonState SCmp:register(s2);"
		"struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"struct P{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"struct D{float4 p:SV_POSITION;float3 w:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float4 c:TEXCOORD3;};"
		"P VST(V x){P o;o.p=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;return o;}"
		"struct HC{float e[4]:SV_TessFactor;float i[2]:SV_InsideTessFactor;};"
		"HC HPC(InputPatch<P,4> p,uint id:SV_PrimitiveID){HC o;float3 c=(p[0].p+p[1].p+p[2].p+p[3].p)*.25;float d=distance(c,Eye.xyz);"
		"float tf=(LightDir.w<.5)?16.:lerp(26.,2.,saturate((d-2.)/18.));tf=clamp(tf,1.,26.);"
		"o.e[0]=o.e[1]=o.e[2]=o.e[3]=tf;o.i[0]=o.i[1]=tf;return o;}"
		"[domain(\"quad\")][partitioning(\"fractional_even\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"HPC\")]"
		"P HST(InputPatch<P,4> p,uint i:SV_OutputControlPointID,uint id:SV_PrimitiveID){return p[i];}"
		"[domain(\"quad\")]D DST(HC h,float2 q:SV_DomainLocation,const OutputPatch<P,4> p){"
		"P a,b,o;a.p=lerp(p[0].p,p[1].p,q.x);b.p=lerp(p[3].p,p[2].p,q.x);o.p=lerp(a.p,b.p,q.y);"
		"a.n=lerp(p[0].n,p[1].n,q.x);b.n=lerp(p[3].n,p[2].n,q.x);o.n=normalize(lerp(a.n,b.n,q.y));"
		"a.uv=lerp(p[0].uv,p[1].uv,q.x);b.uv=lerp(p[3].uv,p[2].uv,q.x);o.uv=lerp(a.uv,b.uv,q.y);o.c=p[0].c;"
		"float nse=NoiseMap.SampleLevel(SL,o.uv*3.+Misc.w*.02,0).r;float bump=(nse-.45);"
		"float d=distance(o.p,Eye.xyz);float amp=lerp(.18,.02,saturate((d-3.)/20.));"
		"o.p+=o.n*bump*amp;float3 nn=normalize(o.n+float3(bump,bump*.5,bump)*.8*amp);"
		"D z;z.w=o.p;z.n=nn;z.uv=o.uv;z.c=o.c;z.p=mul(float4(o.p,1),VP);return z;}"
		"float ShadowAt(float3 w,float3 n){float3 nn=normalize(n);float3 l=normalize(LightDir.xyz);float ndl=saturate(dot(nn,l));"
		"w+=nn*(0.02+(1-ndl)*0.03);float4 sp=mul(float4(w,1),LightVP);float iw=1.0/max(sp.w,1e-5);"
		"float2 uv=sp.xy*iw*float2(.5,-.5)+.5;uv+=float2(2,-2)/1024.;float z=sp.z*iw-0.003;"
		"if(any(uv<0)||any(uv>1)||z<=0||z>=1)return 1;"
		"const float2 o[8]={float2(-.326,-.406),float2(-.84,-.074),float2(-.696,.457),float2(-.203,.621),float2(.962,-.195),float2(.473,-.48),float2(.519,.767),float2(.185,-.893)};"
		"float s=0;const float t=2./1024.;[unroll]for(int k=0;k<8;k++)s+=ShadowMap.SampleCmpLevelZero(SCmp,uv+o[k]*t,z);return pow(saturate(s*.125),1.25);}"
		"float4 PSB(D i):SV_Target{float4 a=T0.Sample(SL,i.uv)*i.c;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float nd=lerp(.12,max(saturate(dot(n,l)),.2),sh);float3 v=normalize(Eye.xyz-i.w);float fr=pow(1-saturate(dot(n,v)),2.8);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float pulse=.55+.45*sin(Misc.w*2.+i.uv.x*12.);"
		"float3 col=a.rgb*nd+env*(.08+fr*.22)+float3(1,.95,.8)*pow(saturate(dot(reflect(-l,n),v)),48)*sh*.45;"
		"col*=lerp(0.55,1.0,sh);col+=a.rgb*pulse*.12;float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"float al=saturate(.22+a.a*.45+fr*.15);return float4(lerp(col,float3(.55,.72,.95),fg*.55),al);}"
		"D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}"
		"struct HV{float2 p:POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};struct HO{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.uv=x.uv;o.c=x.c;return o;}"
		"float4 PSH(HO i):SV_Target{if(i.uv.x<-0.5)return i.c;float4 t=T0.Sample(SL,i.uv);return float4(t.rgb*i.c.rgb,t.a*i.c.a);}"
		"float4 PSS(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float3 v=normalize(Eye.xyz-i.w);float nd=lerp(.32,max(saturate(dot(n,l)),.24),sh);"
		"float3 tex=T0.Sample(SL,i.w.xz*0.00115).rgb;"
		"float3 base=i.c.rgb*(0.9+0.1*tex);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),56)*sh;float3 env=Env.Sample(SL,reflect(-v,n)).rgb;"
		"float fr=pow(1-saturate(dot(n,v)),2.2);float3 c=base*nd+env*(.07+fr*.18)+float3(1,.96,.88)*sp*.3;"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"return float4(lerp(c,float3(.55,.7,.92),fg*.45),saturate(i.c.a));}"
		"float4 PSC(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float3 v=normalize(Eye.xyz-i.w);float nd=lerp(.48,max(saturate(dot(n,l)),.34),sh);"
		"float3 albedo=T0.Sample(SL,i.uv).rgb;float3 base=saturate(i.c.rgb*albedo*1.12);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float fr=pow(1-saturate(dot(n,v)),2.4);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),52)*sh;"
		"float3 c=base*(0.42+0.58*nd)+env*(.1+fr*.26)+float3(1,.96,.9)*sp*.55;"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"return float4(lerp(c,float3(.55,.7,.92),fg*.3),saturate(i.c.a));}"
		"struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}"
		"float4 SSR(Q i):SV_Target{return T0.Sample(SL,i.uv);}"
		"float4 DOFP(Q i):SV_Target{float zd=Depth.Sample(SP,i.uv).r;const float zn=.08,zf=220.;"
		"float eyeZ=zn*zf/max(1e-4,zf-zd*(zf-zn));float coc=saturate((eyeZ-Dof.x)/max(.05,Dof.y));coc=coc*coc*(3.-2.*coc);"
		"float b=coc*Dof.z*(1.+Dof.w);if(b<1.15)return T0.Sample(SL,i.uv);float2 px=float2(b,b)*Screen.zw;"
		"float4 c=T0.Sample(SL,i.uv)*.28;c+=(T0.Sample(SL,i.uv+float2(px.x,0))+T0.Sample(SL,i.uv-float2(px.x,0))+T0.Sample(SL,i.uv+float2(0,px.y))+T0.Sample(SL,i.uv-float2(0,px.y)))*.13;"
		"c+=(T0.Sample(SL,i.uv+px)+T0.Sample(SL,i.uv-px)+T0.Sample(SL,i.uv+float2(px.x,-px.y))+T0.Sample(SL,i.uv+float2(-px.x,px.y)))*.05;return c;}"
		"float4 FIN(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);if(Misc.z>8.f)return c;"
		"float v=saturate(1-dot((i.uv-.5)*1.1,(i.uv-.5)*1.1));c.rgb*=lerp(.86,1.08,v);"
		"float th=Eye.w;float3 tone=float3(1.02,.98,.96);if(th>6.5)tone=float3(1.05,.92,.85);else if(th>5.5)tone=float3(.9,1.02,1.08);"
		"else if(th>4.5)tone=float3(.88,.92,1.08);else if(th>3.5)tone=float3(.95,.95,1.02);else if(th>2.5)tone=float3(1.0,.9,.85);"
		"else if(th>1.5)tone=float3(.95,1.02,.92);c.rgb=saturate(c.rgb*tone);if(Misc.x>0.01)c.rgb=lerp(c.rgb,1,Misc.x*.35);return c;}"
		"RWTexture2D<float4> NoiseOut:register(u0);[numthreads(8,8,1)]void CSNoise(uint3 id:SV_DispatchThreadID){"
		"uint2 p=id.xy;if(p.x>=64||p.y>=64)return;float2 uv=(float2(p)+.5)/64.;"
		"float n=frac(sin(dot(uv,float2(12.9898,78.233))+Misc.w)*43758.5453);"
		"float n2=frac(sin(dot(uv*2.7,float2(39.7,11.3))+n)*23421.6);"
		"float d=length(uv-.5);NoiseOut[p]=float4(n,n2,saturate(1.-d*1.4),1);}";

	ID3DBlob *b[12]={0}, *err=NULL;
	const char* entries[12]={"VST","HST","DST","PSB","VSS","PSS","VSH","PSH","VSQ","SSR","DOFP","FIN"};
	const char* profiles[12]={"vs_5_0","hs_5_0","ds_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","ps_5_0","ps_5_0"};
	for(int i=0;i<12;i++){
		if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,entries[i],profiles[i],D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b[i],&err))){
			if (err) {
				OutputDebugStringA((const char*)err->GetBufferPointer());
				OutputDebugStringA("\n");
			}
			m_dxFailHr = E_FAIL;
			S3R_RELEASE(err); for(int j=0;j<12;j++) S3R_RELEASE(b[j]); return FALSE;
		}
		S3R_RELEASE(err);
	}
	ID3DBlob* bcs=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"CSNoise","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bcs,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(err); return FALSE;
	}
	ID3DBlob* bcraft=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"PSC","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bcraft,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(err); return FALSE;
	}
	S3R_RELEASE(err);
	HRESULT hr=S_OK;
	if(FAILED(hr=m_dev->CreateVertexShader(b[0]->GetBufferPointer(),b[0]->GetBufferSize(),NULL,&m_vsTess))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateHullShader(b[1]->GetBufferPointer(),b[1]->GetBufferSize(),NULL,&m_hsTess))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateDomainShader(b[2]->GetBufferPointer(),b[2]->GetBufferSize(),NULL,&m_dsTess))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[3]->GetBufferPointer(),b[3]->GetBufferSize(),NULL,&m_psBand))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(b[4]->GetBufferPointer(),b[4]->GetBufferSize(),NULL,&m_vsSolid))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[5]->GetBufferPointer(),b[5]->GetBufferSize(),NULL,&m_psSolid))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(bcraft->GetBufferPointer(),bcraft->GetBufferSize(),NULL,&m_psCraft))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(b[6]->GetBufferPointer(),b[6]->GetBufferSize(),NULL,&m_vsHud))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[7]->GetBufferPointer(),b[7]->GetBufferSize(),NULL,&m_psHud))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(b[8]->GetBufferPointer(),b[8]->GetBufferSize(),NULL,&m_vsPost))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[9]->GetBufferPointer(),b[9]->GetBufferSize(),NULL,&m_psSsr))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[10]->GetBufferPointer(),b[10]->GetBufferSize(),NULL,&m_psDof))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[11]->GetBufferPointer(),b[11]->GetBufferSize(),NULL,&m_psFinal))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateComputeShader(bcs->GetBufferPointer(),bcs->GetBufferSize(),NULL,&m_csNoise))) goto fail_sh;
	D3D11_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
	D3D11_INPUT_ELEMENT_DESC ih[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
	if(FAILED(hr=m_dev->CreateInputLayout(il,4,b[0]->GetBufferPointer(),b[0]->GetBufferSize(),&m_ilPatch))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateInputLayout(il,4,b[4]->GetBufferPointer(),b[4]->GetBufferSize(),&m_ilSolid))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateInputLayout(ih,3,b[6]->GetBufferPointer(),b[6]->GetBufferSize(),&m_ilHud))) goto fail_sh;
	for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft);
	return TRUE;
fail_sh:
	m_dxFailHr = hr;
	for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft);
	return FALSE;
}

BOOL CS3rView::CreateProcTextures()
{
	auto hash=[&](int x,int y,int s)->int{return ((x*73856093)^(y*19349663)^(s*83492791))&255;};
	// 低周波のみ（画素単位ノイズは機体/地形の「UV破綻」に見える）
	auto softN=[&](int x,int y,int s)->int{
		int a=hash(x/6,y/6,s)-128, b=hash(x/12,y/12,s+17)-128, c=hash(x/24,y/24,s+41)-128;
		return (a*2+b*3+c)/6;
	};
	const int W=256,H=256;
	DWORD* pix=new (std::nothrow) DWORD[W*H];
	if(!pix) return FALSE;
	D3D11_TEXTURE2D_DESC d={}; d.Width=W;d.Height=H;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	for(int th=0;th<S3R_THEME_N;th++){
		for(int y=0;y<H;y++)for(int x=0;x<W;x++){
			int n=softN(x,y,th+3); BYTE r,g,b,a=255;
			if(th==0){ // forest
				float f1=(float)((hash(x/8,y/8,1)&255)/255.f), f2=(float)((hash(x/14,y/14,2)&255)/255.f);
				float f3=(float)((hash(x/28,y/28,3)&255)/255.f);
				r=(BYTE)(48+n/14+(int)(f2*28)); g=(BYTE)(118+n/8+(int)(f1*55)); b=(BYTE)(42+n/16+(int)(f3*20));
				if(((hash(x/10,y/10,4)&31)==0)){r=78;g=150;b=60;}
				if(((hash(x/16,y/16,5)&63)==0)){r=220;g=160;b=190;a=230;}
			}else if(th==1){ // ruins
				r=(BYTE)(148+n/10); g=(BYTE)(138+n/11); b=(BYTE)(118+n/12);
				if(((x&63)<3)||((y&63)<3)){r=118;g=108;b=98;}
				if(((hash(x/12,y/12,2)&31)==0)){r=170;g=185;b=125;}
			}else if(th==2){ // oil factory
				r=(BYTE)(72+n/12); g=(BYTE)(76+n/12); b=(BYTE)(82+n/12);
				if(((x&31)<2)||((y&31)<2)){r=48;g=48;b=52;}
				if(((hash(x/8,y/8,4)&31)==0)){r=230;g=150;b=50;}
			}else if(th==3){ // night city
				r=(BYTE)(32+n/16); g=(BYTE)(36+n/14); b=(BYTE)(72+n/10);
				if(((x&15)==0)||((y&15)==0)){r=22;g=26;b=52;}
				if(((hash(x/8,y/8,5)&15)==0)){r=240;g=210;b=130;}
			}else if(th==4){ // underwater
				r=(BYTE)(34+n/14); g=(BYTE)(92+n/10); b=(BYTE)(142+n/7);
				if(((hash(x/12,y/12,6)&31)==0)){r=230;g=130;b=175;a=210;}
				a=(BYTE)(190+abs(n)/6);
			}else if(th==5){ // grassland
				r=(BYTE)(92+n/10); g=(BYTE)(175+n/7); b=(BYTE)(72+n/12);
				if(((hash(x/14,y/14,7)&15)==0)){r=240;g=220;b=100;}
			}else if(th==6){ // sunset mesa
				r=(BYTE)(198+n/10); g=(BYTE)(112+n/12); b=(BYTE)(72+n/14);
				if(((hash(x/12,y/12,10)&31)==0)){r=165;g=85;b=55;}
			}else{ // cloud garden
				r=(BYTE)(222+n/16); g=(BYTE)(232+n/16); b=(BYTE)(250);
				if(((hash(x/18,y/18,9)&7)==0)){r=245;g=195;b=225;}
				a=(BYTE)(210+abs(n)/7);
			}
			pix[y*W+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
		}
		D3D11_SUBRESOURCE_DATA sd={pix,W*4,0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texTheme[th]))||FAILED(m_dev->CreateShaderResourceView(m_texTheme[th],NULL,&m_srvTheme[th]))){delete[] pix;return FALSE;}
	}
	// 機体専用スキン（メッシュ UV 用）— パネル／ストライプ／キャノピ用グラデ
	for(int y=0;y<H;y++)for(int x=0;x<W;x++){
		float u=(float)x/(W-1), v=(float)y/(H-1);
		float panel = 0.88f + 0.08f*sinf(u*18.f) + 0.06f*sinf(v*10.f);
		float seamU = (fmodf(u*6.f,1.f)<0.04f) ? 0.72f : 1.f;
		float seamV = (fmodf(v*4.f,1.f)<0.045f) ? 0.78f : 1.f;
		float nose = 1.f - 0.22f*S3rSaturate((v-0.78f)/0.22f);
		float belly = 1.f + 0.12f*(1.f-v);
		float wingStripe = (v>0.35f && v<0.55f && (u<0.18f || u>0.82f)) ? 1.18f : 1.f;
		float gloss = 0.92f + 0.1f*powf(sinf((u+v)*3.14159f),2.f);
		float m = panel*seamU*seamV*nose*belly*wingStripe*gloss;
		BYTE r=(BYTE)S3rClamp(210*m, 40, 255);
		BYTE g=(BYTE)S3rClamp(215*m, 40, 255);
		BYTE b=(BYTE)S3rClamp(230*m, 50, 255);
		// キャノピ帯（高 v・中央 u は青みがかった半ツヤ）
		if (v>0.55f && v<0.92f && u>0.28f && u<0.72f) {
			r=(BYTE)S3rClamp(120+40*m,0,255); g=(BYTE)S3rClamp(170+50*m,0,255); b=(BYTE)S3rClamp(220+30*m,0,255);
		}
		pix[y*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;
	}
	{D3D11_SUBRESOURCE_DATA sd={pix,W*4,0}; if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texCraft))||FAILED(m_dev->CreateShaderResourceView(m_texCraft,NULL,&m_srvCraft))){delete[] pix;return FALSE;}}
	// power band ribbon texture (glowing pastel)
	for(int y=0;y<H;y++)for(int x=0;x<W;x++){
		float u=(float)x/(W-1), v=(float)y/(H-1);
		float edge=powf(1.f-fabsf(v-.5f)*2.f,1.6f);
		float pulse=.55f+.45f*sinf(u*40.f);
		BYTE r=(BYTE)S3rClamp(140+90*edge*pulse,0,255);
		BYTE g=(BYTE)S3rClamp(200+55*edge,0,255);
		BYTE b=(BYTE)S3rClamp(255,0,255);
		BYTE a=(BYTE)S3rClamp(90+140*edge,0,255);
		pix[y*W+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
	}
	{D3D11_SUBRESOURCE_DATA sd={pix,W*4,0}; if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texBand))||FAILED(m_dev->CreateShaderResourceView(m_texBand,NULL,&m_srvBand))){delete[] pix;return FALSE;}}
	// env cube
	d.ArraySize=6; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
	DWORD* cube=new (std::nothrow) DWORD[W*H*6];
	if(!cube){delete[] pix;return FALSE;}
	D3D11_SUBRESOURCE_DATA cs[6]={};
	for(int f=0;f<6;f++){
		for(int y=0;y<H;y++)for(int x=0;x<W;x++){
			float t=(float)y/(H-1);
			BYTE r=(BYTE)(140+70*(1-t)), g=(BYTE)(180+50*(1-t)), b=(BYTE)(220+30*(1-t));
			if(f==2){r=(BYTE)(90+40*t);g=(BYTE)(150+40*t);b=(BYTE)(80+20*t);}
			if(f==3){r=(BYTE)(200+30*(1-t));g=(BYTE)(140+20*(1-t));b=(BYTE)(160+40*(1-t));}
			cube[(f*H+y)*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;
		}
		cs[f].pSysMem=cube+f*W*H; cs[f].SysMemPitch=W*4;
	}
	if(FAILED(m_dev->CreateTexture2D(&d,cs,&m_texEnv))||FAILED(m_dev->CreateShaderResourceView(m_texEnv,NULL,&m_srvEnv))){delete[] cube;delete[] pix;return FALSE;}
	delete[] cube; delete[] pix;
	// noise UAV target 64x64
	{
		D3D11_TEXTURE2D_DESC nd={}; nd.Width=64; nd.Height=64; nd.MipLevels=1; nd.ArraySize=1;
		nd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; nd.SampleDesc.Count=1;
		nd.Usage=D3D11_USAGE_DEFAULT; nd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
		if(FAILED(m_dev->CreateTexture2D(&nd,NULL,&m_texNoise))) return FALSE;
		if(FAILED(m_dev->CreateShaderResourceView(m_texNoise,NULL,&m_srvNoise))) return FALSE;
		if(FAILED(m_dev->CreateUnorderedAccessView(m_texNoise,NULL,&m_uavNoise))) return FALSE;
	}
	return BakeNoiseCS();
}

BOOL CS3rView::BakeNoiseCS()
{
	if(!m_imm||!m_csNoise||!m_uavNoise||!m_cbFrame) return FALSE;
	S3RFrameCB cb={}; cb.misc={0,0,0,(float)(GetTickCount()&0xffff)*.01f};
	D3D11_MAPPED_SUBRESOURCE map={};
	if(FAILED(m_imm->Map(m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return FALSE;
	memcpy(map.pData,&cb,sizeof(cb)); m_imm->Unmap(m_cbFrame,0);
	m_imm->CSSetShader(m_csNoise,NULL,0);
	m_imm->CSSetConstantBuffers(0,1,&m_cbFrame);
	m_imm->CSSetUnorderedAccessViews(0,1,&m_uavNoise,NULL);
	m_imm->Dispatch(8,8,1);
	ID3D11UnorderedAccessView* nullu=NULL; m_imm->CSSetUnorderedAccessViews(0,1,&nullu,NULL);
	m_imm->CSSetShader(NULL,NULL,0);
	return TRUE;
}

BOOL CS3rView::InitDx()
{
	ReleaseDx();
	m_dxFailStage = 0; m_dxFailHr = S_OK;
	if (!m_hWnd || !::IsWindow(m_hWnd)) { m_dxFailStage = 1; return FALSE; }
	UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	flags|=D3D11_CREATE_DEVICE_DEBUG;
#endif
	D3D_FEATURE_LEVEL req=D3D_FEATURE_LEVEL_11_0, got=(D3D_FEATURE_LEVEL)0;
	HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,flags,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr)) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,flags&~D3D11_CREATE_DEVICE_DEBUG,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr) || got < D3D_FEATURE_LEVEL_11_0) { m_dxFailStage = 2; m_dxFailHr = hr; return FALSE; }
	IDXGIDevice* xd=NULL;IDXGIAdapter* xa=NULL;IDXGIFactory2* f2=NULL;IDXGIFactory* f1=NULL;
	if(FAILED(hr=m_dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(hr=xd->GetAdapter(&xa))) { m_dxFailStage = 3; m_dxFailHr = hr; S3R_RELEASE(xd); return FALSE; }
	xa->GetParent(__uuidof(IDXGIFactory2),(void**)&f2);
	// Width/Height=0: DXGI が HWND クライアントサイズを使う（迷路側と同じ）
	DXGI_SWAP_CHAIN_DESC1 s={};s.Width=0;s.Height=0;s.Format=DXGI_FORMAT_B8G8R8A8_UNORM;s.SampleDesc.Count=1;s.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;s.BufferCount=2;s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;s.AlphaMode=DXGI_ALPHA_MODE_IGNORE;s.Scaling=DXGI_SCALING_STRETCH;
	if(f2){IDXGISwapChain1* sc1=NULL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);if(FAILED(hr)){s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);}if(FAILED(hr)){s.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;s.BufferCount=1;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);}if(SUCCEEDED(hr))m_swap=sc1;}else hr=E_FAIL;
	if(FAILED(hr)){xa->GetParent(__uuidof(IDXGIFactory),(void**)&f1);DXGI_SWAP_CHAIN_DESC o={};o.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;o.SampleDesc.Count=1;o.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;o.BufferCount=1;o.OutputWindow=m_hWnd;o.Windowed=TRUE;o.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;hr=f1?f1->CreateSwapChain(m_dev,&o,&m_swap):E_FAIL;}
	S3R_RELEASE(f1);S3R_RELEASE(f2);S3R_RELEASE(xa);S3R_RELEASE(xd);
	if(FAILED(hr)||!m_swap){ m_dxFailStage = 4; m_dxFailHr = hr; return FALSE; }
	D3D11_BUFFER_DESC bd={};bd.ByteWidth=((sizeof(S3RFrameCB)+15)/16)*16;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_cbFrame))){ m_dxFailStage = 5; m_dxFailHr = hr; return FALSE; }
	if(!CreateShaders()){ m_dxFailStage = 6; if(m_dxFailHr==S_OK) m_dxFailHr = E_FAIL; return FALSE; }
	if(!CreateProcTextures()){ m_dxFailStage = 7; m_dxFailHr = E_FAIL; return FALSE; }
	bd.ByteWidth=m_vbDynBytes;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_vbDyn))){
		static const UINT kVbTry[]={16u*1024u*1024u,12u*1024u*1024u,8u*1024u*1024u,4u*1024u*1024u};
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
	D3D11_RASTERIZER_DESC rss={};rss.FillMode=D3D11_FILL_SOLID;rss.CullMode=D3D11_CULL_NONE;rss.DepthClipEnable=TRUE;rss.DepthBias=1000;rss.SlopeScaledDepthBias=1.5f;m_dev->CreateRasterizerState(&rss,&m_rsShadow);
	D3D11_DEPTH_STENCIL_DESC ds={};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;m_dev->CreateDepthStencilState(&ds,&m_dssWrite);ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;m_dev->CreateDepthStencilState(&ds,&m_dssRead);ds.DepthEnable=FALSE;m_dev->CreateDepthStencilState(&ds,&m_dssOff);
	D3D11_BLEND_DESC bl={};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;m_dev->CreateBlendState(&bl,&m_bsOpaque);bl.RenderTarget[0].BlendEnable=TRUE;bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bl.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bl.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;m_dev->CreateBlendState(&bl,&m_bsAlpha);bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_ONE;m_dev->CreateBlendState(&bl,&m_bsAdd);
	{
		D3D11_TEXTURE2D_DESC td={};td.Width=S3R_SHADOW_SIZE;td.Height=S3R_SHADOW_SIZE;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
		if(FAILED(hr=m_dev->CreateTexture2D(&td,NULL,&m_shadowTex))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
		D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(hr=m_dev->CreateDepthStencilView(m_shadowTex,&dd,&m_shadowDsv))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
		D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
		if(FAILED(hr=m_dev->CreateShaderResourceView(m_shadowTex,&sd,&m_shadowSrv))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
	}
	CRect rc;GetClientRect(&rc);
	if(!ResizeDx(max(8,rc.Width()),max(8,rc.Height()))){ m_dxFailStage = 11; m_dxFailHr = E_FAIL; return FALSE; }
	return TRUE;
}

BOOL CS3rView::EnsureSceneTargets(int w,int h)
{
	if(m_sceneTex&&w==m_vw&&h==m_vh)return TRUE;
	ID3D11RenderTargetView* nullrt=NULL;m_imm->OMSetRenderTargets(1,&nullrt,NULL);
	S3R_RELEASE(m_dsSrv);S3R_RELEASE(m_dsv);S3R_RELEASE(m_dsTex);S3R_RELEASE(m_sceneSrv);S3R_RELEASE(m_sceneRtv);S3R_RELEASE(m_sceneTex);S3R_RELEASE(m_postSrv);S3R_RELEASE(m_postRtv);S3R_RELEASE(m_postTex);
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
	if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_sceneTex))||FAILED(m_dev->CreateRenderTargetView(m_sceneTex,NULL,&m_sceneRtv))||FAILED(m_dev->CreateShaderResourceView(m_sceneTex,NULL,&m_sceneSrv)))return FALSE;
	if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_postTex))||FAILED(m_dev->CreateRenderTargetView(m_postTex,NULL,&m_postRtv))||FAILED(m_dev->CreateShaderResourceView(m_postTex,NULL,&m_postSrv)))return FALSE;
	d.Format=DXGI_FORMAT_R24G8_TYPELESS;d.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_dsTex)))return FALSE;
	D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;if(FAILED(m_dev->CreateDepthStencilView(m_dsTex,&dd,&m_dsv)))return FALSE;
	D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;if(FAILED(m_dev->CreateShaderResourceView(m_dsTex,&sd,&m_dsSrv)))return FALSE;
	m_vw=w;m_vh=h;return TRUE;
}
BOOL CS3rView::ResizeDx(int w,int h)
{
	if(!m_swap||w<1||h<1)return FALSE;m_ready=FALSE;m_imm->OMSetRenderTargets(0,NULL,NULL);S3R_RELEASE(m_bbRtv);
	HRESULT hr=m_swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))return FALSE;ID3D11Texture2D* bb=NULL;hr=m_swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);if(SUCCEEDED(hr))hr=m_dev->CreateRenderTargetView(bb,NULL,&m_bbRtv);S3R_RELEASE(bb);
	if(FAILED(hr)||!EnsureSceneTargets(w,h))return FALSE;m_ready=TRUE;return TRUE;
}
void CS3rView::PresentFrame(){if(m_swap&&m_ready)m_swap->Present(0,0);}
void CS3rView::ReleaseClearTexture(){S3R_RELEASE(m_srvClear);S3R_RELEASE(m_texClear);m_clearTexW=m_clearTexH=0;}
void CS3rView::ReleaseHudTexture(){S3R_RELEASE(m_srvHud);S3R_RELEASE(m_texHud);m_hudTexW=m_hudTexH=0;}
void CS3rView::ReleaseStandingsTexture(){S3R_RELEASE(m_srvStand);S3R_RELEASE(m_texStand);m_standTexW=m_standTexH=0;}

BOOL CS3rView::BakeClearTexture(const wchar_t* text,float alpha)
{
	ReleaseClearTexture();const int w=max(256,m_vw),h=max(96,m_vh/4);BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
	void* bits=NULL;HDC dc=CreateCompatibleDC(NULL);HBITMAP bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,NULL,0);HGDIOBJ old=SelectObject(dc,bm);memset(bits,0,w*h*4);
	{
		Gdiplus::Graphics g(dc);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		Gdiplus::FontFamily ffJp(L"Yu Gothic UI");
		Gdiplus::FontFamily ffMe(L"Meiryo UI");
		Gdiplus::FontFamily ffEn(L"Segoe UI");
		const Gdiplus::FontFamily* fam = &ffEn;
		if (ffJp.GetLastStatus() == Gdiplus::Ok) fam = &ffJp;
		else if (ffMe.GetLastStatus() == Gdiplus::Ok) fam = &ffMe;
		const int tlen = text ? (int)wcslen(text) : 0;
		float fontPx = (float)max(40, h / 2);
		if (tlen > 10) fontPx = (float)max(32, h / 3);
		if (tlen > 16) fontPx = (float)max(26, h / 4);
		Gdiplus::Font font(fam, fontPx, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::StringFormat sf;
		sf.SetAlignment(Gdiplus::StringAlignmentCenter);
		sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		BYTE a = (BYTE)min(255, (int)(alpha * 255.f + 0.5f));
		Gdiplus::SolidBrush sh(Gdiplus::Color((BYTE)min(220, (int)a), 0, 0, 0));
		Gdiplus::SolidBrush fg(Gdiplus::Color(a, 255, 236, 150));
		Gdiplus::RectF r(6.f, 6.f, (Gdiplus::REAL)(w - 12), (Gdiplus::REAL)(h - 12));
		g.DrawString(text, -1, &font, r, &sf, &sh);
		r.X -= 3.f; r.Y -= 3.f;
		g.DrawString(text, -1, &font, r, &sf, &fg);
	}
	// HDC 経由だと α が落ちることがあるので輝度から復元（迷路 Clear と同じ経路を安定化）
	{
		DWORD* p=(DWORD*)bits; const int n=w*h;
		for(int i=0;i<n;i++){
			DWORD c=p[i]; BYTE r=(BYTE)((c>>16)&255), g=(BYTE)((c>>8)&255), b=(BYTE)(c&255);
			int lum=(int)r+(int)g+(int)b; BYTE a=(BYTE)((c>>24)&255);
			if(a<8 && lum>8){ a=(BYTE)min(255, lum); p[i]=((DWORD)a<<24)|(c&0x00FFFFFFu); }
		}
	}
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA sd={bits,(UINT)w*4,0};HRESULT hr=m_dev->CreateTexture2D(&d,&sd,&m_texClear);if(SUCCEEDED(hr))hr=m_dev->CreateShaderResourceView(m_texClear,NULL,&m_srvClear);SelectObject(dc,old);DeleteObject(bm);DeleteDC(dc);m_clearTexW=w;m_clearTexH=h;return SUCCEEDED(hr);
}
BOOL CS3rView::BakeHudTexture(const wchar_t* text)
{
	if(!m_dev||!text) return FALSE; ReleaseHudTexture();
	const int w=640,h=220; BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
	void* bits=NULL;HDC dc=CreateCompatibleDC(NULL);HBITMAP bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,NULL,0);HGDIOBJ old=SelectObject(dc,bm);memset(bits,0,w*h*4);
	{Gdiplus::Graphics g(dc);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);Gdiplus::SolidBrush bg(Gdiplus::Color(140,8,10,18));g.FillRectangle(&bg,0,0,w,h);Gdiplus::FontFamily ff(L"Segoe UI");Gdiplus::Font font(&ff,22.f,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);Gdiplus::StringFormat sf;sf.SetAlignment(Gdiplus::StringAlignmentNear);Gdiplus::SolidBrush sh(Gdiplus::Color(160,0,0,0)),fg(Gdiplus::Color(255,245,250,255));Gdiplus::RectF r(14,12,(Gdiplus::REAL)(w-24),(Gdiplus::REAL)(h-20));g.DrawString(text,-1,&font,r,&sf,&sh);r.X-=1.5f;r.Y-=1.5f;g.DrawString(text,-1,&font,r,&sf,&fg);}
	{
		DWORD* p=(DWORD*)bits; const int n=w*h;
		for(int i=0;i<n;i++){
			DWORD c=p[i]; BYTE a=(BYTE)((c>>24)&255);
			if(a<8){ BYTE r=(BYTE)((c>>16)&255),g=(BYTE)((c>>8)&255),b=(BYTE)(c&255); int lum=r+g+b; if(lum>12) a=(BYTE)min(255,lum+40); }
			if(a<8) continue;
			p[i]=((DWORD)a<<24)|(c&0x00FFFFFFu);
		}
	}
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA sd={bits,(UINT)w*4,0};HRESULT hr=m_dev->CreateTexture2D(&d,&sd,&m_texHud);if(SUCCEEDED(hr))hr=m_dev->CreateShaderResourceView(m_texHud,NULL,&m_srvHud);SelectObject(dc,old);DeleteObject(bm);DeleteDC(dc);m_hudTexW=w;m_hudTexH=h;return SUCCEEDED(hr);
}
BOOL CS3rView::BakeStandingsTexture(const S3rStandRow* rows, int nRows)
{
	if (!m_dev || !rows || nRows < 1) return FALSE;
	ReleaseStandingsTexture();
	if (nRows > 12) nRows = 12;
	// 画面側は常に「12枠分」で行高が決まるので、ベイクも12枠前提の固定行高
	const int rowH = 96;
	const int namePx = 24;
	const int lapPx = 15;
	const int w = 380, h = nRows * rowH + 8;
	BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h;
	bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
	void* bits=NULL; HDC dc=CreateCompatibleDC(NULL); HBITMAP bm=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,NULL,0);
	HGDIOBJ old=SelectObject(dc,bm); memset(bits,0,w*h*4);
	{
		Gdiplus::Graphics g(dc);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::FontFamily ffJp(L"Yu Gothic UI");
		Gdiplus::FontFamily ffMe(L"Meiryo UI");
		Gdiplus::FontFamily ffEn(L"Segoe UI");
		const Gdiplus::FontFamily* fam = &ffEn;
		if (ffJp.GetLastStatus() == Gdiplus::Ok) fam = &ffJp;
		else if (ffMe.GetLastStatus() == Gdiplus::Ok) fam = &ffMe;
		Gdiplus::Font fName(fam, (Gdiplus::REAL)namePx, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::Font fLap(fam, (Gdiplus::REAL)lapPx, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::StringFormat sf; sf.SetAlignment(Gdiplus::StringAlignmentNear);
		Gdiplus::StringFormat sfRank; sfRank.SetAlignment(Gdiplus::StringAlignmentFar);
		const float lapStep = (float)(rowH - 46) / 3.f;
		for (int i = 0; i < nRows; i++) {
			const S3rStandRow& r = rows[i];
			float y0 = (float)(6 + i * rowH);
			BYTE aBg = r.isPlayer ? (BYTE)230 : (BYTE)185;
			BYTE br = r.isPlayer ? 28 : 10, bg = r.isPlayer ? 48 : 14, bb = r.isPlayer ? 72 : 22;
			Gdiplus::SolidBrush rowBg(Gdiplus::Color(aBg, br, bg, bb));
			g.FillRectangle(&rowBg, 3.f, y0, (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(rowH - 5));
			BYTE cr=(BYTE)(r.cr*255.f), cg=(BYTE)(r.cg*255.f), cb=(BYTE)(r.cb*255.f);
			Gdiplus::SolidBrush craftBr(Gdiplus::Color(255, cr, cg, cb));
			float cx = 30.f, cy = y0 + 24.f;
			g.FillEllipse(&craftBr, cx - 12.f, cy - 10.f, 24.f, 20.f);
			Gdiplus::PointF wing[3] = { {cx-16.f,cy}, {cx-2.f,cy-8.f}, {cx-2.f,cy+8.f} };
			g.FillPolygon(&craftBr, wing, 3);
			wing[0]={cx+16.f,cy}; wing[1]={cx+2.f,cy-8.f}; wing[2]={cx+2.f,cy+8.f};
			g.FillPolygon(&craftBr, wing, 3);
			wchar_t rankBuf[24];
			if (r.rank == 1) wcscpy_s(rankBuf, LL14(L"1st", L"1st", L"1er", L"1°", L"1.º", L"1위", L"第1", L"1", L"1-й", L"1.", L"1º", L"1e", L"1.", L"1."));
			else if (r.rank == 2) wcscpy_s(rankBuf, LL14(L"2nd", L"2nd", L"2e", L"2°", L"2.º", L"2위", L"第2", L"2", L"2-й", L"2.", L"2º", L"2e", L"2.", L"2."));
			else if (r.rank == 3) wcscpy_s(rankBuf, LL14(L"3rd", L"3rd", L"3e", L"3°", L"3.º", L"3위", L"第3", L"3", L"3-й", L"3.", L"3º", L"3e", L"3.", L"3."));
			else swprintf_s(rankBuf, L"%d%s", r.rank, LL14(L"位", L"th", L"e", L"°", L".º", L"위", L"名", L"", L"-й", L".", L"º", L"e", L".", L"."));
			Gdiplus::SolidBrush fg(Gdiplus::Color(255, r.isPlayer ? 255 : 250, r.isPlayer ? 252 : 250, r.isPlayer ? 215 : 255));
			Gdiplus::SolidBrush dim(Gdiplus::Color(255, 210, 230, 245));
			Gdiplus::SolidBrush sh(Gdiplus::Color(200, 0, 0, 0));
			Gdiplus::RectF nameR(52.f, y0 + 4.f, 220.f, (Gdiplus::REAL)(namePx + 6));
			g.DrawString(r.name, -1, &fName, nameR, &sf, &sh);
			nameR.X -= 1.5f; nameR.Y -= 1.5f;
			g.DrawString(r.name, -1, &fName, nameR, &sf, &fg);
			Gdiplus::RectF rankR(260.f, y0 + 4.f, 128.f, (Gdiplus::REAL)(namePx + 6));
			g.DrawString(rankBuf, -1, &fName, rankR, &sfRank, &sh);
			rankR.X -= 1.5f; rankR.Y -= 1.5f;
			g.DrawString(rankBuf, -1, &fName, rankR, &sfRank, &fg);
			for (int k = 0; k < 3; k++) {
				int lapNo = r.lapNo[k] > 0 ? r.lapNo[k] : (k + 1);
				float sec = r.lapSec[k];
				wchar_t one[40];
				if (sec >= 0.f) swprintf_s(one, L"LAP%d  %.2f", lapNo, sec);
				else swprintf_s(one, L"LAP%d  ----", lapNo);
				Gdiplus::RectF lapR(52.f, y0 + 40.f + lapStep * (float)k, 330.f, lapStep);
				g.DrawString(one, -1, &fLap, lapR, &sf, &sh);
				lapR.X -= 1.2f; lapR.Y -= 1.2f;
				g.DrawString(one, -1, &fLap, lapR, &sf, &dim);
			}
		}
	}
	{
		DWORD* p=(DWORD*)bits; const int n=w*h;
		for (int i=0;i<n;i++) {
			DWORD c=p[i]; BYTE a=(BYTE)((c>>24)&255);
			if (a<8) {
				BYTE r=(BYTE)((c>>16)&255), g=(BYTE)((c>>8)&255), b=(BYTE)(c&255);
				int lum=r+g+b; if (lum>12) a=(BYTE)min(255,lum+50);
			}
			if (a<8) continue;
			p[i]=((DWORD)a<<24)|(c&0x00FFFFFFu);
		}
	}
	D3D11_TEXTURE2D_DESC d={}; d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
	d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_IMMUTABLE; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd={bits,(UINT)w*4,0};
	HRESULT hr=m_dev->CreateTexture2D(&d,&sd,&m_texStand);
	if (SUCCEEDED(hr)) hr=m_dev->CreateShaderResourceView(m_texStand,NULL,&m_srvStand);
	SelectObject(dc,old); DeleteObject(bm); DeleteDC(dc);
	m_standTexW=w; m_standTexH=h;
	return SUCCEEDED(hr);
}
void CS3rView::ReleaseDx()
{
	m_ready=FALSE;if(m_imm){m_imm->ClearState();m_imm->Flush();}
	ReleaseClearTexture();ReleaseHudTexture();ReleaseStandingsTexture();
	S3R_RELEASE(m_uavNoise);S3R_RELEASE(m_srvNoise);S3R_RELEASE(m_texNoise);
	S3R_RELEASE(m_srvEnv);S3R_RELEASE(m_texEnv);S3R_RELEASE(m_srvCraft);S3R_RELEASE(m_texCraft);S3R_RELEASE(m_srvBand);S3R_RELEASE(m_texBand);
	for(int i=0;i<S3R_THEME_N;i++){S3R_RELEASE(m_srvTheme[i]);S3R_RELEASE(m_texTheme[i]);}
	S3R_RELEASE(m_bsAdd);S3R_RELEASE(m_bsAlpha);S3R_RELEASE(m_bsOpaque);S3R_RELEASE(m_dssOff);S3R_RELEASE(m_dssRead);S3R_RELEASE(m_dssWrite);S3R_RELEASE(m_rsShadow);S3R_RELEASE(m_rsNoCull);S3R_RELEASE(m_rsSolid);S3R_RELEASE(m_sampCmp);S3R_RELEASE(m_sampPoint);S3R_RELEASE(m_sampLin);
	S3R_RELEASE(m_vbHud);S3R_RELEASE(m_vbDyn);S3R_RELEASE(m_cbFrame);S3R_RELEASE(m_ilHud);S3R_RELEASE(m_ilSolid);S3R_RELEASE(m_ilPatch);
	delete[] m_cpuDynScratch; m_cpuDynScratch=NULL; m_cpuDynScratchBytes=0;
	delete[] m_cpuHudScratch; m_cpuHudScratch=NULL; m_cpuHudScratchBytes=0;
	S3R_RELEASE(m_csNoise);S3R_RELEASE(m_psFinal);S3R_RELEASE(m_psDof);S3R_RELEASE(m_psSsr);S3R_RELEASE(m_vsPost);S3R_RELEASE(m_psHud);S3R_RELEASE(m_vsHud);S3R_RELEASE(m_psCraft);S3R_RELEASE(m_psSolid);S3R_RELEASE(m_vsSolid);S3R_RELEASE(m_psBand);S3R_RELEASE(m_dsTess);S3R_RELEASE(m_hsTess);S3R_RELEASE(m_vsTess);
	S3R_RELEASE(m_shadowSrv);S3R_RELEASE(m_shadowDsv);S3R_RELEASE(m_shadowTex);
	S3R_RELEASE(m_postSrv);S3R_RELEASE(m_postRtv);S3R_RELEASE(m_postTex);S3R_RELEASE(m_sceneSrv);S3R_RELEASE(m_sceneRtv);S3R_RELEASE(m_sceneTex);S3R_RELEASE(m_dsSrv);S3R_RELEASE(m_dsv);S3R_RELEASE(m_dsTex);S3R_RELEASE(m_bbRtv);S3R_RELEASE(m_swap);S3R_RELEASE(m_imm);S3R_RELEASE(m_dev);m_vw=m_vh=0;
}
void CS3rView::OnSize(UINT nType,int cx,int cy){CCustomStatic::OnSize(nType,cx,cy);if(m_swap&&cx>0&&cy>0)ResizeDx(cx,cy);}
void CS3rView::OnPaint(){CPaintDC dc(this);ValidateRect(NULL);}
LRESULT CS3rView::OnPrintClient(WPARAM,LPARAM){return 0;}
void CS3rView::OnDestroy(){ReleaseDx();CCustomStatic::OnDestroy();}
void CS3rView::OnContextMenu(CWnd*,CPoint){ /* view: no context — RMB is brake */ }
void CS3rView::OnLButtonDown(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputAccel(TRUE);SetCapture();}
void CS3rView::OnLButtonUp(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputAccel(FALSE);if(GetCapture()==this)ReleaseCapture();}
void CS3rView::OnRButtonDown(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputBrake(TRUE);SetCapture();}
void CS3rView::OnRButtonUp(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputBrake(FALSE);if(GetCapture()==this)ReleaseCapture();}
void CS3rView::OnMButtonDown(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputLookback(TRUE);}
void CS3rView::OnMButtonUp(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputLookback(FALSE);}
void CS3rView::OnMouseMove(UINT nFlags,CPoint point)
{
	CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent(); if(!d) return;
	if(d->m_mouseLook){
		float dx=(float)(point.x-d->m_lastMouse.x), dy=(float)(point.y-d->m_lastMouse.y);
		// 初回や UI から入ったときの巨大ジャンプは無視（真下へ振り切れる主因）
		if (fabsf(dx) < 64.f && fabsf(dy) < 64.f)
			d->InputSteerDelta(dx*0.0032f, (savedata.s3r_invert_y ? 1.f : -1.f) * dy*0.0024f);
	}
	d->m_lastMouse=point; d->m_mouseLook=1;
}
BOOL CS3rView::OnMouseWheel(UINT,short zDelta,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputZoom(zDelta>0?1:-1);return TRUE;}

// ---- dialog ----
IMPLEMENT_DYNAMIC(CSoft3DRaceDlg, CCustomBlurDialogBase)

BEGIN_MESSAGE_MAP(CSoft3DRaceDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_S3R_START, &CSoft3DRaceDlg::OnStart)
	ON_BN_CLICKED(IDC_S3R_GEN, &CSoft3DRaceDlg::OnGen)
	ON_BN_CLICKED(IDC_S3R_CLOSE, &CSoft3DRaceDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_S3R_HELP, &CSoft3DRaceDlg::OnHelp)
	ON_CBN_SELCHANGE(IDC_S3R_AI, &CSoft3DRaceDlg::OnAiChanged)
	ON_CBN_SELCHANGE(IDC_S3R_OPP, &CSoft3DRaceDlg::OnOppChanged)
	ON_CBN_SELCHANGE(IDC_S3R_LEN, &CSoft3DRaceDlg::OnLenChanged)
	ON_CBN_SELCHANGE(IDC_S3R_LAPS, &CSoft3DRaceDlg::OnLapsChanged)
	ON_CBN_SELCHANGE(IDC_S3R_THEME, &CSoft3DRaceDlg::OnThemeChanged)
	ON_CBN_SELCHANGE(IDC_S3R_INVERT, &CSoft3DRaceDlg::OnInvertChanged)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CSoft3DRaceDlg::CSoft3DRaceDlg(CWnd* p)
	: CCustomBlurDialogBase(IDD, p)
	, m_knotN(0), m_pathLen(0), m_craftN(0), m_obsN(0), m_itemN(0)
	, m_craftNv(0), m_craftNi(0), m_obsNv(0), m_obsNi(0)
	, m_phase(PHASE_IDLE), m_countT(0), m_countShown(-1), m_podiumT(0)
	, m_themeActive(THEME_FOREST), m_lapsTarget(3), m_bandHalf(6.f)
	, m_demoCamT(0.f), m_demoCamElev(0.f), m_demoMidX(0), m_demoMidY(0), m_demoMidZ(0), m_demoRad(80.f)
	, m_camYawOff(0), m_camPitchOff(0.22f), m_camZoom(1.f)
	, m_camSx(0), m_camSy(0), m_camSz(0), m_camAx(0), m_camAy(0), m_camAz(0), m_camSmoothInit(0)
	, m_lookback(0), m_accelHeld(0), m_brakeHeld(0), m_mouseLook(0)
	, m_lastTick(0), m_rng(1), m_genSeed(1), m_spaceToggleTick(0)
	, m_baseTempoPos(100), m_basePitchPos(200), m_anim(0), m_raceClock(0), m_playerSpdEma(0), m_playerAccel(0)
	, m_wrongWay(0), m_overlayHold(0)
	, m_clearBakeA(0), m_hudDirty(1), m_clearDirty(1), m_standDirty(1)
	, m_reverbFogBoost(0), m_eqDofBoost(0)
	, m_podiumBaseX(0), m_podiumBaseY(0), m_podiumBaseZ(0)
{
	memset(m_knots,0,sizeof(m_knots));
	memset(m_pathSampleXYZ,0,sizeof(m_pathSampleXYZ));
	memset(m_pathSampleT,0,sizeof(m_pathSampleT));
	memset(m_pathCumLen,0,sizeof(m_pathCumLen));
	memset(m_crafts,0,sizeof(m_crafts));
	memset(m_obs,0,sizeof(m_obs));
	memset(m_items,0,sizeof(m_items));
	memset(m_craftVert,0,sizeof(m_craftVert));
	memset(m_craftIdx,0,sizeof(m_craftIdx));
	memset(m_obsVert,0,sizeof(m_obsVert));
	memset(m_obsIdx,0,sizeof(m_obsIdx));
	memset(m_podiumOrder,0,sizeof(m_podiumOrder));
	memset(m_confetti,0,sizeof(m_confetti));
	m_lastMouse=CPoint(0,0);
}
CSoft3DRaceDlg::~CSoft3DRaceDlg() {}

void CSoft3DRaceDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_S3R_HELP, m_help);
	DDX_Control(pDX, IDC_S3R_AI_L, m_aiL);
	DDX_Control(pDX, IDC_S3R_AI, m_ai);
	DDX_Control(pDX, IDC_S3R_OPP_L, m_oppL);
	DDX_Control(pDX, IDC_S3R_OPP, m_opp);
	DDX_Control(pDX, IDC_S3R_LEN_L, m_lenL);
	DDX_Control(pDX, IDC_S3R_LEN, m_len);
	DDX_Control(pDX, IDC_S3R_LAPS_L, m_lapsL);
	DDX_Control(pDX, IDC_S3R_LAPS, m_laps);
	DDX_Control(pDX, IDC_S3R_THEME_L, m_themeL);
	DDX_Control(pDX, IDC_S3R_THEME, m_theme);
	DDX_Control(pDX, IDC_S3R_INVERT_L, m_invertL);
	DDX_Control(pDX, IDC_S3R_INVERT, m_invert);
	DDX_Control(pDX, IDC_S3R_START, m_start);
	DDX_Control(pDX, IDC_S3R_GEN, m_gen);
	DDX_Control(pDX, IDC_S3R_HINT, m_hint);
	DDX_Control(pDX, IDC_S3R_VIEW, m_view);
	DDX_Control(pDX, IDC_S3R_STATUS, m_status);
	DDX_Control(pDX, IDC_S3R_CLOSE, m_close);
}

BOOL CSoft3DRaceDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
	if (HandleAccelMessage(pMsg)) return TRUE;
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}
BOOL CSoft3DRaceDlg::HandleAccelMessage(MSG* pMsg)
{
	if (!pMsg) return FALSE;
	const UINT msg = pMsg->message;
	if (msg != WM_KEYDOWN && msg != WM_KEYUP && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP)
		return FALSE;
	if (pMsg->wParam == VK_SPACE) {
		if (msg == WM_KEYDOWN && !(pMsg->lParam & (1 << 30))) {
			const DWORD now = GetTickCount();
			if (now != m_spaceToggleTick) m_spaceToggleTick = now;
		}
		return TRUE;
	}
	if (msg == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
		m_lookback = 0;
		return TRUE;
	}
	return FALSE;
}
void CSoft3DRaceDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_s3r == this) g_s3r = NULL;
	delete this;
}
void CSoft3DRaceDlg::LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }
void CSoft3DRaceDlg::LayoutAll()
{
	if (!GetSafeHwnd() || !m_view.GetSafeHwnd()) return;
	CRect rc; GetClientRect(&rc);
	const int cx = rc.Width(), cy = rc.Height();
	if (cx < 220 || cy < 200) return;
	int capH = CCC_GetCustomCaptionHeight(m_hWnd); if (capH < 0) capH = 0;
	const int m = 10, rowH = 36, btnH = 32, labH = 22;
	int y = capH + 8;
	const float baseW = 1080.f;
	const float sx = max(1.f, (float)(cx - 2 * m) / baseW);
	auto sw = [&](int w)->int { return max(w, (int)((float)w * sx + .5f)); };
	const int gap = max(4, (int)(6.f * sx + .5f));
	int x = m;
	auto placeL = [&](CWnd& w, int ww){ if(w.GetSafeHwnd()) w.SetWindowPos(NULL,x,y+6,ww,labH,SWP_NOZORDER|SWP_NOACTIVATE); x+=ww+2; };
	auto placeC = [&](CWnd& w, int ww){ if(w.GetSafeHwnd()) w.SetWindowPos(NULL,x,y,ww,280,SWP_NOZORDER|SWP_NOACTIVATE); x+=ww+gap; };
	auto placeB = [&](CWnd& w, int ww){ if(w.GetSafeHwnd()) w.SetWindowPos(NULL,x,y-1,ww,34,SWP_NOZORDER|SWP_NOACTIVATE); x+=ww+gap; };
	placeL(m_aiL, sw(36)); placeC(m_ai, sw(100));
	placeL(m_oppL, sw(36)); placeC(m_opp, sw(64));
	placeL(m_lenL, sw(36)); placeC(m_len, sw(84));
	placeL(m_lapsL, sw(36)); placeC(m_laps, sw(64));
	placeL(m_themeL, sw(40)); placeC(m_theme, sw(110));
	placeL(m_invertL, sw(36)); placeC(m_invert, sw(100));
	placeB(m_start, sw(80)); placeB(m_gen, sw(80));
	if (m_hint.GetSafeHwnd()) m_hint.SetWindowPos(NULL, x, y+6, max(40, cx-x-m), labH, SWP_NOZORDER|SWP_NOACTIVATE);
	y += rowH + 6;
	const int btnY = cy - m - btnH;
	int viewBottom = btnY - 10; if (viewBottom < y + 80) viewBottom = y + 80;
	m_view.SetWindowPos(NULL, m, y, max(40, cx-2*m), max(40, viewBottom-y), SWP_NOZORDER|SWP_NOACTIVATE);
	const int closeW = sw(100);
	if (m_close.GetSafeHwnd()) m_close.SetWindowPos(NULL, cx-m-closeW, btnY, closeW, btnH, SWP_NOZORDER|SWP_NOACTIVATE);
	if (m_status.GetSafeHwnd()) m_status.SetWindowPos(NULL, m, btnY+6, max(40, cx-m-closeW-14-m), 20, SWP_NOZORDER|SWP_NOACTIVATE);
	if (m_ai.GetSafeHwnd()) {int v=savedata.s3r_ai; if(v<0)v=0; if(v>4)v=4; m_ai.CComboBox::SetCurSel(v);}
	if (m_opp.GetSafeHwnd()) { int o=savedata.s3r_opponents; if(o<1)o=1; if(o>11)o=11; m_opp.CComboBox::SetCurSel(o-1); }
	if (m_len.GetSafeHwnd()) {int v=savedata.s3r_len; if(v<0)v=0; if(v>3)v=3; m_len.CComboBox::SetCurSel(v);}
	if (m_laps.GetSafeHwnd()) {int v=savedata.s3r_laps; if(v<0)v=0; if(v>10)v=10; m_laps.CComboBox::SetCurSel(v);}
	if (m_theme.GetSafeHwnd()) {int v=savedata.s3r_theme; if(v<0)v=0; if(v>8)v=8; m_theme.CComboBox::SetCurSel(v);}
	if (m_invert.GetSafeHwnd()) {int v=savedata.s3r_invert_y?1:0; m_invert.CComboBox::SetCurSel(v);}
	CCC_CaptionLayout(m_hWnd); LayoutHelpBtn();
}

int CSoft3DRaceDlg::ReadAiFromUi(){ int s=m_ai.GetSafeHwnd()?(int)m_ai.CComboBox::GetCurSel():savedata.s3r_ai; if(s<0||s>=AI_COUNT)s=AI_NORMAL; return s; }
void CSoft3DRaceDlg::SetAiToUi(int v){ if(v<0)v=0; if(v>=AI_COUNT)v=AI_COUNT-1; savedata.s3r_ai=v; if(m_ai.GetSafeHwnd()) m_ai.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadOppFromUi(){ int s=m_opp.GetSafeHwnd()?(int)m_opp.CComboBox::GetCurSel()+1:savedata.s3r_opponents; if(s<1)s=1; if(s>11)s=11; return s; }
void CSoft3DRaceDlg::SetOppToUi(int v){ if(v<1)v=1; if(v>11)v=11; savedata.s3r_opponents=v; if(m_opp.GetSafeHwnd()) m_opp.CComboBox::SetCurSel(v-1); }
int CSoft3DRaceDlg::ReadLenFromUi(){ int s=m_len.GetSafeHwnd()?(int)m_len.CComboBox::GetCurSel():savedata.s3r_len; if(s<0||s>3)s=0; return s; }
void CSoft3DRaceDlg::SetLenToUi(int v){ if(v<0)v=0; if(v>3)v=3; savedata.s3r_len=v; if(m_len.GetSafeHwnd()) m_len.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadLapsFromUi(){ int s=m_laps.GetSafeHwnd()?(int)m_laps.CComboBox::GetCurSel():savedata.s3r_laps; if(s<0||s>10)s=0; return s; }
void CSoft3DRaceDlg::SetLapsToUi(int v){ if(v<0)v=0; if(v>10)v=10; savedata.s3r_laps=v; if(m_laps.GetSafeHwnd()) m_laps.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadThemeFromUi(){ int s=m_theme.GetSafeHwnd()?(int)m_theme.CComboBox::GetCurSel():savedata.s3r_theme; if(s<0||s>8)s=0; return s; }
void CSoft3DRaceDlg::SetThemeToUi(int v){ if(v<0)v=0; if(v>8)v=8; savedata.s3r_theme=v; if(m_theme.GetSafeHwnd()) m_theme.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadInvertFromUi(){ int s=m_invert.GetSafeHwnd()?(int)m_invert.CComboBox::GetCurSel():savedata.s3r_invert_y; return (s!=0)?1:0; }
void CSoft3DRaceDlg::SetInvertToUi(int v){ v=v?1:0; savedata.s3r_invert_y=v; if(m_invert.GetSafeHwnd()) m_invert.CComboBox::SetCurSel(v); }

void CSoft3DRaceDlg::PersistUi()
{
	savedata.s3r_ai = ReadAiFromUi();
	savedata.s3r_opponents = ReadOppFromUi();
	savedata.s3r_len = ReadLenFromUi();
	savedata.s3r_laps = ReadLapsFromUi();
	savedata.s3r_theme = ReadThemeFromUi();
	savedata.s3r_invert_y = ReadInvertFromUi();
	if (savedata.s3r_show_map != 0) savedata.s3r_show_map = 1;
	int mask = savedata.s3r_item_mask; if (mask <= 0) mask = ITEM_ALL; savedata.s3r_item_mask = mask & ITEM_ALL;
	if (savedata.s3r_zoom < 50 || savedata.s3r_zoom > 250) savedata.s3r_zoom = 100;
	MpPersistSavedataQuick();
}
void CSoft3DRaceDlg::PersistWindowRect()
{
	if (!GetSafeHwnd() || IsIconic()) return;
	CRect r; GetWindowRect(&r);
	int w=r.Width(), h=r.Height(); if (w < 200 || h < 160) return;
	savedata.s3r_win_x=r.left; savedata.s3r_win_y=r.top; savedata.s3r_win_w=w; savedata.s3r_win_h=h;
	MpPersistSavedataQuick();
}
void CSoft3DRaceDlg::ApplySavedWindowRect()
{
	int w=savedata.s3r_win_w, h=savedata.s3r_win_h, x=savedata.s3r_win_x, y=savedata.s3r_win_y;
	if (w < 480 || h < 360) return;
	HMONITOR mon = MonitorFromPoint(CPoint(x+w/2,y+h/2), MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi={sizeof(mi)}; GetMonitorInfo(mon,&mi);
	if (x < mi.rcWork.left) x = mi.rcWork.left;
	if (y < mi.rcWork.top) y = mi.rcWork.top;
	if (x+w > mi.rcWork.right) x = mi.rcWork.right - w;
	if (y+h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
	SetWindowPos(NULL,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
}
void CSoft3DRaceDlg::CaptureAudioBaseline()
{
	m_baseTempoPos = tempo; m_basePitchPos = pitch;
	if (og && ::IsWindow(og->GetSafeHwnd())) { m_baseTempoPos = og->m_tempo_sl.GetPos(); m_basePitchPos = og->m_pitch_sl.GetPos(); }
}
void CSoft3DRaceDlg::RestoreAudioBaseline()
{
	// 動画(mode=-2)はテンポ／ピッチ変更で再生速度が動くので触らない（見た目バフだけ戻す）
	if (mode != -2) {
		if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->ApplyPracticeTempoPercent(m_baseTempoPos / 2);
		else { tempo = m_baseTempoPos; if (og && ::IsWindow(og->GetSafeHwnd())) og->m_tempo_sl.SetPos(m_baseTempoPos); }
		S3rSetPitchPos(m_basePitchPos);
	}
	m_reverbFogBoost = 0; m_eqDofBoost = 0;
}
int CSoft3DRaceDlg::ItemMask() const
{
	int m = savedata.s3r_item_mask; if (m <= 0) m = ITEM_ALL; return m & ITEM_ALL;
}
float CSoft3DRaceDlg::CourseScale() const
{
	int L = savedata.s3r_len; if (L == LEN_AUTO) L = LEN_NORMAL;
	if (L == LEN_SHORT) return 0.85f; if (L == LEN_LONG) return 1.45f; return 1.1f;
}
float CSoft3DRaceDlg::EffectiveLaps() const
{
	int laps = savedata.s3r_laps;
	if (laps > 0) return (float)laps;
	int L = savedata.s3r_len; if (L == LEN_AUTO) L = LEN_NORMAL;
	if (L == LEN_LONG) return 2.f; return 3.f;
}
int CSoft3DRaceDlg::EffectiveTheme() const
{
	int t = savedata.s3r_theme;
	if (t >= THEME_FOREST && t <= THEME_CLOUD) return t;
	return 1 + (int)(m_genSeed % 8u);
}
float CSoft3DRaceDlg::BandHalfWidth() const { return m_bandHalf; }
float CSoft3DRaceDlg::SpeedScale() const
{
	// コース規模に比例（通常≈1.1）
	return CourseScale();
}
float CSoft3DRaceDlg::RaceSpeedCap(int boosted) const
{
	// 体感の最高付近 120km/h。長いコースはわずかに余裕
	float u = 125.f / 3.6f;
	u *= (0.96f + 0.08f * (CourseScale() / 1.1f));
	if (boosted) u *= 1.12f; // ≈140km/h 一瞬
	return u;
}
float CSoft3DRaceDlg::SpeedToKmh(float vx, float vy, float vz) const
{
	return sqrtf(vx * vx + vy * vy + vz * vz) * 3.6f;
}
float CSoft3DRaceDlg::PathArcBetween(float t0, float t1) const
{
	if (m_pathLen < 1.f) return 0.f;
	while (t0 < 0.f) t0 += 1.f; while (t0 >= 1.f) t0 -= 1.f;
	while (t1 < 0.f) t1 += 1.f; while (t1 >= 1.f) t1 -= 1.f;
	int i0 = (int)(t0 * (float)S3R_PATH_SAMPLES) % S3R_PATH_SAMPLES; if (i0 < 0) i0 += S3R_PATH_SAMPLES;
	int i1 = (int)(t1 * (float)S3R_PATH_SAMPLES) % S3R_PATH_SAMPLES; if (i1 < 0) i1 += S3R_PATH_SAMPLES;
	float L0 = m_pathCumLen[i0], L1 = m_pathCumLen[i1];
	if (i1 >= i0) return max(0.f, L1 - L0);
	return max(0.f, (m_pathLen - L0) + L1);
}
void CSoft3DRaceDlg::RespawnCraftToCheckpoint(S3rCraft& c, float fuelAmt, float cool)
{
	// 常に帯中央へ（端座標復帰が COURSE OUT 連発の主因だった）
	c.aiCutT = -1.f; c.aiCutTimer = 0.f;
	c.aiCutCool = max(c.aiCutCool, 18.f); // 復帰後は通常ライン固定
	c.pathT = c.chkPathT;
	float cx,cy,cz,tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	c.x = cx; c.y = cy; c.z = cz;
	c.yaw = atan2f(tx, tz);
	c.pitch = S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -0.55f, 0.55f);
	float keep = 16.f / 3.6f;
	c.vx = tx * keep; c.vy = ty * keep; c.vz = tz * keep;
	c.fuel = fuelAmt;
	c.offBandT = 0.f;
	c.offBand = 0;
	c.courseOutCool = cool;
	c.chkX = cx; c.chkY = cy; c.chkZ = cz;
	c.chkYaw = c.yaw; c.chkPitch = c.pitch;
}
void CSoft3DRaceDlg::AbortAiToLine(S3rCraft& c, float lineLockSec)
{
	// 失敗カット等：COURSE OUT まで落とさず帯中央へ戻してライン走行へ
	c.aiCutT = -1.f; c.aiCutTimer = 0.f;
	c.aiCutCool = max(c.aiCutCool, lineLockSec);
	float cx,cy,cz,tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	c.x = cx; c.y = cy; c.z = cz;
	c.yaw = atan2f(tx, tz);
	c.pitch = S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -0.55f, 0.55f);
	float keep = 20.f / 3.6f;
	c.vx = tx * keep; c.vy = ty * keep; c.vz = tz * keep;
	c.fuel = max(c.fuel, 75.f);
	c.offBand = 0; c.offBandT = 0.f;
	c.courseOutCool = max(c.courseOutCool, 0.85f);
	c.chkX = cx; c.chkY = cy; c.chkZ = cz;
	c.chkYaw = c.yaw; c.chkPitch = c.pitch; c.chkPathT = c.pathT;
}

void CSoft3DRaceDlg::SplinePoint(float t, float& x, float& y, float& z) const
{
	if (m_knotN < 4) { x=y=z=0; return; }
	while (t < 0.f) t += 1.f; while (t >= 1.f) t -= 1.f;
	const float ft = t * (float)m_knotN;
	int i1 = (int)floorf(ft) % m_knotN; if (i1 < 0) i1 += m_knotN;
	int i0 = (i1 - 1 + m_knotN) % m_knotN;
	int i2 = (i1 + 1) % m_knotN;
	int i3 = (i1 + 2) % m_knotN;
	float u = ft - floorf(ft), u2=u*u, u3=u2*u;
	auto cr = [&](float p0,float p1,float p2,float p3)->float {
		return 0.5f*((2.f*p1)+(-p0+p2)*u+(2.f*p0-5.f*p1+4.f*p2-p3)*u2+(-p0+3.f*p1-3.f*p2+p3)*u3);
	};
	x = cr(m_knots[i0].x,m_knots[i1].x,m_knots[i2].x,m_knots[i3].x);
	y = cr(m_knots[i0].y,m_knots[i1].y,m_knots[i2].y,m_knots[i3].y);
	z = cr(m_knots[i0].z,m_knots[i1].z,m_knots[i2].z,m_knots[i3].z);
}
void CSoft3DRaceDlg::SplineTangent(float t, float& x, float& y, float& z) const
{
	float x0,y0,z0,x1,y1,z1; SplinePoint(t-0.002f,x0,y0,z0); SplinePoint(t+0.002f,x1,y1,z1);
	x=x1-x0; y=y1-y0; z=z1-z0; S3rNorm3(x,y,z);
}
void CSoft3DRaceDlg::SplineFrame(float t, float& px, float& py, float& pz, float& tx, float& ty, float& tz, float& nx, float& ny, float& nz, float& bx, float& by, float& bz) const
{
	SplinePoint(t,px,py,pz); SplineTangent(t,tx,ty,tz);
	float upx=0,upy=1,upz=0;
	bx = ty*upz - tz*upy; by = tz*upx - tx*upz; bz = tx*upy - ty*upx;
	if (bx*bx+by*by+bz*bz < 1e-6f) { upx=1; upy=0; upz=0; bx=ty*upz-tz*upy; by=tz*upx-tx*upz; bz=tx*upy-ty*upx; }
	S3rNorm3(bx,by,bz);
	nx = by*tz - bz*ty; ny = bz*tx - bx*tz; nz = bx*ty - by*tx; S3rNorm3(nx,ny,nz);
}
float CSoft3DRaceDlg::ClosestSplineT(float x, float y, float z, float hintT) const
{
	float bestT = hintT, bestD = 1e30f;
	// 狭い局所窓のみ（広い前方探索は pathT ジャンプ＝誤ラップの温床）
	int start = (int)(hintT * S3R_PATH_SAMPLES) - 36;
	for (int k = 0; k < 72; k++) {
		int i = start + k; while (i < 0) i += S3R_PATH_SAMPLES; i %= S3R_PATH_SAMPLES;
		float dx=x-m_pathSampleXYZ[i][0], dy=y-m_pathSampleXYZ[i][1], dz=z-m_pathSampleXYZ[i][2];
		float d=dx*dx+dy*dy+dz*dz; if (d < bestD) { bestD=d; bestT=m_pathSampleT[i]; }
	}
	float refineBestT = bestT, refineBestD = bestD;
	for (int k = -8; k <= 8; k++) {
		float t = bestT + (float)k * (0.5f / (float)S3R_PATH_SAMPLES);
		float px,py,pz; SplinePoint(t,px,py,pz);
		float dx=x-px, dy=y-py, dz=z-pz;
		float d=dx*dx+dy*dy+dz*dz;
		if (d < refineBestD) { refineBestD=d; refineBestT=t; }
	}
	while (refineBestT < 0.f) refineBestT += 1.f;
	while (refineBestT >= 1.f) refineBestT -= 1.f;
	return refineBestT;
}
float CSoft3DRaceDlg::AdvancePathT(float x, float y, float z, float prevT, float spd, float dt) const
{
	float raw = ClosestSplineT(x, y, z, prevT);
	float d = raw - prevT;
	while (d > 0.5f) d -= 1.f;
	while (d < -0.5f) d += 1.f;
	float plen = (m_pathLen > 1.f) ? m_pathLen : 800.f;
	// 実移動距離ベースのみ。大きな追いつきは禁止（ワープ周回の主因だった）
	float maxFwd = (spd * dt + 2.5f) / plen;
	if (maxFwd < 0.0008f) maxFwd = 0.0008f;
	if (maxFwd > 0.02f) maxFwd = 0.02f;
	const float maxBack = 0.006f;
	if (d > maxFwd) d = maxFwd;
	if (d < -maxBack) d = -maxBack;
	float t = prevT + d;
	while (t < 0.f) t += 1.f;
	while (t >= 1.f) t -= 1.f;
	return t;
}
float CSoft3DRaceDlg::DistToBand(float x, float y, float z, float t, float& outCx, float& outCy, float& outCz) const
{
	SplinePoint(t, outCx, outCy, outCz);
	float dx=x-outCx, dy=y-outCy, dz=z-outCz;
	return sqrtf(dx*dx+dy*dy+dz*dz);
}
void CSoft3DRaceDlg::BandLocal(float x, float y, float z, float t, float& lat, float& vert, float& cx, float& cy, float& cz) const
{
	float tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(t, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	float ox=x-cx, oy=y-cy, oz=z-cz;
	lat = ox*bx + oy*by + oz*bz;
	vert = ox*nx + oy*ny + oz*nz;
}
float CSoft3DRaceDlg::GroundY(float x, float z) const
{
	float h = 2.5f + 7.5f * sinf(x * 0.022f) + 6.2f * cosf(z * 0.019f) + 3.5f * sinf((x + z) * 0.013f)
		+ 2.2f * sinf(x * 0.041f) * cosf(z * 0.037f);
	if (m_themeActive == THEME_MESA) h += 5.f + 9.f * fabsf(sinf(x * 0.016f) * cosf(z * 0.016f));
	else if (m_themeActive == THEME_UNDER) h -= 4.f;
	else if (m_themeActive == THEME_CLOUD) h += 10.f + 5.f * sinf(x * 0.009f);
	else if (m_themeActive == THEME_RUINS) h += 2.5f * fabsf(sinf(x * 0.032f)) + 1.5f * fabsf(cosf(z * 0.028f));
	else if (m_themeActive == THEME_FOREST) h += 2.4f * sinf(x * 0.038f) * cosf(z * 0.033f) + 1.8f * sinf((x-z)*0.02f);
	else if (m_themeActive == THEME_NIGHT) h += 1.5f * sinf(x * 0.03f);
	else if (m_themeActive == THEME_OIL) h += 1.2f * fabsf(sinf(x * 0.05f));
	return h;
}

void CSoft3DRaceDlg::GenerateCourse()
{
	m_genSeed = GetTickCount() ^ (m_rng * 2654435761u);
	GenerateCourseWithSeed(m_genSeed);
}
void CSoft3DRaceDlg::GenerateCourseWithSeed(DWORD seed)
{
	m_genSeed = seed ? seed : 1; m_rng = m_genSeed;
	PersistUi();
	m_themeActive = EffectiveTheme();
	m_lapsTarget = (int)EffectiveLaps();
	const float sc = CourseScale();
	int knots = 56;
	int L = savedata.s3r_len; if (L == LEN_AUTO) L = LEN_NORMAL;
	if (L == LEN_SHORT) knots = 44; else if (L == LEN_LONG) knots = 80;
	if (knots > S3R_SPLINE_MAX) knots = S3R_SPLINE_MAX;
	m_knotN = knots;
	m_bandHalf = 7.2f * sc;
	// --- 平面レイアウト（XZ）---
	for (int i = 0; i < knots; i++) {
		float a = (float)i / (float)knots * (float)(M_PI * 2.0);
		float rad = (155.f + 38.f * sinf(a * 2.f + S3rRand01(m_rng))) * sc;
		float wob = 22.f * sc * sinf(a * 2.4f + S3rRand01(m_rng) * 1.5f);
		m_knots[i].x = cosf(a) * rad + cosf(a * 1.7f) * wob;
		m_knots[i].z = sinf(a) * rad + sinf(a * 1.3f) * wob;
		m_knots[i].y = 0.f; // 後で高さ付け
	}
	// --- 高さ：乱雑上下ではなく「巡航＋所々の突起／くぼみ」（3D酔い対策）---
	{
		float baseY[S3R_SPLINE_MAX];
		for (int i = 0; i < knots; i++) {
			float a = (float)i / (float)knots * (float)(M_PI * 2.0);
			float gy = GroundY(m_knots[i].x, m_knots[i].z);
			// ごく緩い長波長のみ（高周波のピッチ揺れを出さない）
			float cruise = gy + (19.f + 3.0f * sinf(a * 1.0f)) * sc;
			if (m_themeActive == THEME_CLOUD) cruise += 6.f * sc;
			if (m_themeActive == THEME_RUINS) cruise = gy + (14.f + 1.8f * sinf(a)) * sc;
			baseY[i] = cruise;
		}
		// 突起／くぼみを 3〜5 箇所だけ足す（ガウス丘で滑らか）— 少しだけ高め
		int nFeat = 3 + (int)(S3rRand01(m_rng) * 2.99f); // 3..5
		for (int f = 0; f < nFeat; f++) {
			int center = (int)(S3rRand01(m_rng) * (float)knots) % knots;
			float width = 3.8f + S3rRand01(m_rng) * 3.8f;
			int up = (S3rRand01(m_rng) > 0.32f) ? 1 : 0;
			float amp = (up ? (9.f + S3rRand01(m_rng) * 10.f) : -(6.f + S3rRand01(m_rng) * 7.f)) * sc;
			if (m_themeActive == THEME_CLOUD && up) amp *= 1.25f;
			if (m_themeActive == THEME_RUINS && !up) amp *= 1.15f;
			for (int i = 0; i < knots; i++) {
				int d = i - center;
				if (d > knots / 2) d -= knots;
				if (d < -knots / 2) d += knots;
				float u = (float)d / width;
				baseY[i] += amp * expf(-0.5f * u * u);
			}
		}
		// 円環ラプラシアン平滑（尖った勾配を落とす）
		for (int pass = 0; pass < 3; pass++) {
			float tmp[S3R_SPLINE_MAX];
			for (int i = 0; i < knots; i++) {
				float ym = baseY[(i - 1 + knots) % knots];
				float y0 = baseY[i];
				float yp = baseY[(i + 1) % knots];
				tmp[i] = ym * 0.22f + y0 * 0.56f + yp * 0.22f;
			}
			memcpy(baseY, tmp, sizeof(float) * knots);
		}
		// 隣接点の最大上昇を制限（基本は緩やか）
		const float maxStep = 3.2f * sc;
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < knots; i++) {
				int j = (i + 1) % knots;
				float dy = baseY[j] - baseY[i];
				if (dy > maxStep) baseY[j] = baseY[i] + maxStep;
				else if (dy < -maxStep) baseY[j] = baseY[i] - maxStep;
			}
		}
		// 急勾配を 1〜2 箇所だけ後付け（そういうコースもあってよい）
		int nSteep = 1 + ((S3rRand01(m_rng) > 0.45f) ? 1 : 0);
		for (int s = 0; s < nSteep; s++) {
			int center = (int)(S3rRand01(m_rng) * (float)knots) % knots;
			float width = 2.0f + S3rRand01(m_rng) * 1.6f;
			float amp = (14.f + S3rRand01(m_rng) * 12.f) * sc;
			if (S3rRand01(m_rng) < 0.35f) amp = -amp;
			for (int i = 0; i < knots; i++) {
				int d = i - center;
				if (d > knots / 2) d -= knots;
				if (d < -knots / 2) d += knots;
				float u = (float)d / width;
				baseY[i] += amp * expf(-0.5f * u * u);
			}
		}
		// 急勾配は潰しすぎないよう軽い平滑のみ
		{
			float tmp[S3R_SPLINE_MAX];
			for (int i = 0; i < knots; i++) {
				float ym = baseY[(i - 1 + knots) % knots];
				float y0 = baseY[i];
				float yp = baseY[(i + 1) % knots];
				tmp[i] = ym * 0.15f + y0 * 0.70f + yp * 0.15f;
			}
			memcpy(baseY, tmp, sizeof(float) * knots);
		}
		for (int i = 0; i < knots; i++) {
			float gy = GroundY(m_knots[i].x, m_knots[i].z);
			m_knots[i].y = max(baseY[i], gy + 8.f * sc);
		}
	}
	m_camSmoothInit = 0; // 新コースでカメラスムーズをリセット
	// sample path
	m_pathLen = 0.f;
	float px=0,py=0,pz=0; SplinePoint(0,px,py,pz);
	for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
		float t = (float)i / (float)S3R_PATH_SAMPLES;
		float x,y,z; SplinePoint(t,x,y,z);
		m_pathSampleT[i] = t;
		m_pathSampleXYZ[i][0]=x; m_pathSampleXYZ[i][1]=y; m_pathSampleXYZ[i][2]=z;
		if (i > 0) {
			float dx=x-px,dy=y-py,dz=z-pz;
			m_pathLen += sqrtf(dx*dx+dy*dy+dz*dz);
		}
		m_pathCumLen[i] = m_pathLen; px=x;py=y;pz=z;
	}
	// デモ俯瞰用：コース中心と水平半径
	{
		float sx = 0.f, sy = 0.f, sz = 0.f;
		for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
			sx += m_pathSampleXYZ[i][0]; sy += m_pathSampleXYZ[i][1]; sz += m_pathSampleXYZ[i][2];
		}
		const float inv = 1.f / (float)S3R_PATH_SAMPLES;
		m_demoMidX = sx * inv; m_demoMidY = sy * inv; m_demoMidZ = sz * inv;
		float r2 = 1.f;
		for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
			float dx = m_pathSampleXYZ[i][0] - m_demoMidX;
			float dz = m_pathSampleXYZ[i][2] - m_demoMidZ;
			float d2 = dx * dx + dz * dz;
			if (d2 > r2) r2 = d2;
		}
		m_demoRad = sqrtf(r2);
		if (m_demoRad < 40.f) m_demoRad = 40.f;
	}
	BuildCraftMeshes();
	BuildObstacleMesh(m_themeActive);
	PlaceObstaclesAndItems();
	ResetRaceState();
	BeginDemoPreview();
	if (m_view.m_ready) m_view.BakeNoiseCS();
	UpdateStatus();
	m_hudDirty = 1; m_clearDirty = 1;
	RenderScene();
}

void CSoft3DRaceDlg::BuildCraftMeshes()
{
	// Cute bird-ship: lathe body + wings + canopy. High subdivision.
	m_craftNv = 0; m_craftNi = 0;
	auto emitV = [&](float x,float y,float z,float nx,float ny,float nz,float u,float v,float r,float g,float b,float a){
		if (m_craftNv >= S3R_CRAFT_VMAX) return;
		float* p = m_craftVert + m_craftNv * 12;
		p[0]=x;p[1]=y;p[2]=z;p[3]=nx;p[4]=ny;p[5]=nz;p[6]=u;p[7]=v;p[8]=r;p[9]=g;p[10]=b;p[11]=a;
		m_craftNv++;
	};
	auto emitTri = [&](UINT a,UINT b,UINT c){
		if (m_craftNi + 3 > S3R_CRAFT_IMAX) return;
		m_craftIdx[m_craftNi++]=a; m_craftIdx[m_craftNi++]=b; m_craftIdx[m_craftNi++]=c;
	};
	const int rings = 14, segs = 18;
	const UINT base = 0;
	for (int i = 0; i <= rings; i++) {
		float t = (float)i / (float)rings;
		float yy = (t - 0.45f) * 1.7f;
		float rad = 0.22f + 0.38f * sinf(t * (float)M_PI) * (0.55f + 0.45f * cosf(t * 2.2f));
		if (t > 0.82f) rad *= (1.f - (t - 0.82f) / 0.18f);
		if (t < 0.12f) rad *= t / 0.12f;
		for (int j = 0; j <= segs; j++) {
			float a = (float)j / (float)segs * (float)(M_PI * 2.0);
			float x = cosf(a) * rad, z = sinf(a) * rad;
			float nx=cosf(a), ny=(t<0.5f?0.35f:-0.15f), nz=sinf(a); S3rNorm3(nx,ny,nz);
			emitV(x, yy, z, nx, ny, nz, (float)j/segs, t, 1,1,1,1);
		}
	}
	for (int i = 0; i < rings; i++) for (int j = 0; j < segs; j++) {
		UINT a = (UINT)(i * (segs + 1) + j);
		UINT b = a + 1, c = a + (UINT)(segs + 1), d = c + 1;
		emitTri(a,c,b); emitTri(b,c,d);
	}
	// wings
	auto wing = [&](float side){
		UINT w0 = (UINT)m_craftNv;
		const int wu=10, wv=6;
		for (int v=0;v<=wv;v++) for (int u=0;u<=wu;u++) {
			float uu=(float)u/wu, vv=(float)v/wv;
			float x = side * (0.25f + uu * 1.15f);
			float y = -0.05f + vv * 0.12f + sinf(uu*(float)M_PI)*0.08f;
			float z = (uu-0.2f)*0.55f + (vv-0.5f)*0.08f;
			float nx=0,ny=1,nz=0;
			emitV(x,y,z,nx,ny,nz,uu,vv,1,1,1,1);
		}
		for (int v=0;v<wv;v++) for (int u=0;u<wu;u++) {
			UINT a=w0+(UINT)(v*(wu+1)+u), b=a+1, c=a+(UINT)(wu+1), d=c+1;
			if (side>0){ emitTri(a,c,b); emitTri(b,c,d);} else { emitTri(a,b,c); emitTri(b,d,c);}
		}
	};
	wing(1.f); wing(-1.f);
	// canopy bubble
	UINT c0=(UINT)m_craftNv;
	const int cr=10, cs=14;
	for (int i=0;i<=cr;i++){
		float t=(float)i/cr; float ph=t*(float)M_PI*.55f;
		for (int j=0;j<=cs;j++){
			float a=(float)j/cs*(float)(M_PI*2.0);
			float rad=0.22f*sinf(ph);
			float x=cosf(a)*rad, y=0.25f+0.32f*cosf(ph), z=sinf(a)*rad*0.85f-0.05f;
			float nx=cosf(a)*sinf(ph), ny=cosf(ph), nz=sinf(a)*sinf(ph); S3rNorm3(nx,ny,nz);
			emitV(x,y,z,nx,ny,nz,(float)j/cs,t,.7f,.9f,1.f,.85f);
		}
	}
	for (int i=0;i<cr;i++) for (int j=0;j<cs;j++){
		UINT a=c0+(UINT)(i*(cs+1)+j), b=a+1, c=a+(UINT)(cs+1), d=c+1;
		emitTri(a,c,b); emitTri(b,c,d);
	}
	(void)base;
}

void CSoft3DRaceDlg::BuildObstacleMesh(int theme)
{
	m_obsNv=0; m_obsNi=0;
	auto emitV=[&](float x,float y,float z,float nx,float ny,float nz,float u,float v,float r,float g,float b,float a){
		if(m_obsNv>=S3R_OBS_VMAX)return; float* p=m_obsVert+m_obsNv*12;
		p[0]=x;p[1]=y;p[2]=z;p[3]=nx;p[4]=ny;p[5]=nz;p[6]=u;p[7]=v;p[8]=r;p[9]=g;p[10]=b;p[11]=a; m_obsNv++;
	};
	auto emitTri=[&](UINT a,UINT b,UINT c){ if(m_obsNi+3>S3R_OBS_IMAX)return; m_obsIdx[m_obsNi++]=a;m_obsIdx[m_obsNi++]=b;m_obsIdx[m_obsNi++]=c; };
	auto cyl=[&](float y0,float y1,float rad,float rr,float gg,float bb,int segsN,float ox,float oz){
		UINT baseV=(UINT)m_obsNv;
		for(int k=0;k<=segsN;k++){
			float a=(float)k/segsN*(float)(M_PI*2);
			float x=ox+cosf(a)*rad, z=oz+sinf(a)*rad, nx=cosf(a), nz=sinf(a);
			emitV(x,y0,z,nx,0,nz,(float)k/segsN,0,rr,gg,bb,1);
			emitV(x,y1,z,nx,0,nz,(float)k/segsN,1,rr,gg,bb,1);
		}
		for(int k=0;k<segsN;k++){
			UINT a=baseV+(UINT)(k*2), b=a+1, c=a+2, d=c+1;
			emitTri(a,c,b); emitTri(b,c,d);
		}
	};
	auto cone=[&](float y0,float y1,float rad0,float rad1,float rr,float gg,float bb,int segsN){
		UINT baseV=(UINT)m_obsNv;
		for(int k=0;k<=segsN;k++){
			float a=(float)k/segsN*(float)(M_PI*2);
			float c=cosf(a), s=sinf(a);
			emitV(c*rad0,y0,s*rad0,c,0.35f,s,(float)k/segsN,0,rr,gg,bb,1);
			emitV(c*rad1,y1,s*rad1,c,0.55f,s,(float)k/segsN,1,rr,gg,bb,1);
		}
		for(int k=0;k<segsN;k++){
			UINT a=baseV+(UINT)(k*2), b=a+1, c=a+2, d=c+1;
			emitTri(a,c,b); emitTri(b,c,d);
		}
	};
	// AABB は常に min/max 正規化（逆転座標で長針ポリゴンが出ないように）
	auto box=[&](float x0,float y0,float z0,float x1,float y1,float z1,float rr,float gg,float bb){
		float xa=(x0<x1)?x0:x1, xb=(x0<x1)?x1:x0;
		float ya=(y0<y1)?y0:y1, yb=(y0<y1)?y1:y0;
		float za=(z0<z1)?z0:z1, zb=(z0<z1)?z1:z0;
		if (xb-xa < 1e-4f || yb-ya < 1e-4f || zb-za < 1e-4f) return;
		UINT b0=(UINT)m_obsNv;
		auto v=[&](float x,float y,float z,float nx,float ny,float nz){emitV(x,y,z,nx,ny,nz,0,0,rr,gg,bb,1);};
		v(xa,ya,za,0,-1,0);v(xb,ya,za,0,-1,0);v(xb,ya,zb,0,-1,0);v(xa,ya,zb,0,-1,0);
		v(xa,yb,za,0,1,0);v(xa,yb,zb,0,1,0);v(xb,yb,zb,0,1,0);v(xb,yb,za,0,1,0);
		emitTri(b0,b0+1,b0+2);emitTri(b0,b0+2,b0+3);
		emitTri(b0+4,b0+5,b0+6);emitTri(b0+4,b0+6,b0+7);
		UINT s=(UINT)m_obsNv;
		v(xa,ya,za,-1,0,0);v(xa,yb,za,-1,0,0);v(xa,yb,zb,-1,0,0);v(xa,ya,zb,-1,0,0);
		emitTri(s,s+1,s+2);emitTri(s,s+2,s+3);
		s=(UINT)m_obsNv;
		v(xb,ya,za,1,0,0);v(xb,ya,zb,1,0,0);v(xb,yb,zb,1,0,0);v(xb,yb,za,1,0,0);
		emitTri(s,s+1,s+2);emitTri(s,s+2,s+3);
		s=(UINT)m_obsNv;
		v(xa,ya,za,0,0,-1);v(xb,ya,za,0,0,-1);v(xb,yb,za,0,0,-1);v(xa,yb,za,0,0,-1);
		emitTri(s,s+1,s+2);emitTri(s,s+2,s+3);
		s=(UINT)m_obsNv;
		v(xa,ya,zb,0,0,1);v(xa,yb,zb,0,0,1);v(xb,yb,zb,0,0,1);v(xb,ya,zb,0,0,1);
		emitTri(s,s+1,s+2);emitTri(s,s+2,s+3);
	};

	if (theme == THEME_FOREST) {
		cyl(0.f, 2.6f, 0.34f, 0.42f, 0.26f, 0.12f, 16, 0.f, 0.f);
		cone(1.7f, 4.4f, 1.45f, 0.2f, 0.32f, 0.78f, 0.3f, 18);
		cone(3.0f, 5.4f, 1.05f, 0.12f, 0.28f, 0.72f, 0.26f, 16);
		cone(4.1f, 6.2f, 0.65f, 0.05f, 0.4f, 0.88f, 0.35f, 14);
		box(-0.85f,0,-0.85f,0.85f,0.16f,0.85f,0.35f,0.55f,0.25f);
	} else if (theme == THEME_RUINS) {
		// 細切れアーチは非等方スケールで針状になるので、塔＋梁のソリッドに変更
		box(-0.85f,0,-0.85f,0.85f,0.35f,0.85f,0.62f,0.56f,0.46f);
		box(-0.55f,0.35f,-0.55f,0.55f,3.4f,0.55f,0.7f,0.64f,0.52f);
		box(-1.35f,3.15f,-0.4f,1.35f,3.55f,0.4f,0.68f,0.62f,0.5f);
		box(-1.25f,0,-0.28f,-0.85f,3.2f,0.28f,0.58f,0.52f,0.42f);
		box(0.85f,0,-0.28f,1.25f,3.2f,0.28f,0.58f,0.52f,0.42f);
		box(-0.35f,3.55f,-0.35f,0.35f,4.1f,0.35f,0.66f,0.6f,0.48f);
	} else if (theme == THEME_OIL) {
		cyl(0.f, 4.0f, 0.55f, 0.3f, 0.32f, 0.36f, 18, 0.f, 0.f);
		cyl(3.7f, 4.35f, 0.8f, 0.9f, 0.55f, 0.18f, 14, 0.f, 0.f);
		cyl(4.2f, 5.0f, 0.22f, 0.45f, 0.48f, 0.5f, 10, 0.f, 0.f);
		box(-1.4f,0,-0.45f,-0.65f,1.9f,0.45f,0.38f,0.4f,0.44f);
		box(0.65f,0,-0.4f,1.4f,1.35f,0.4f,0.42f,0.44f,0.48f);
		box(-0.35f,1.9f,-0.35f,0.35f,2.2f,0.35f,0.95f,0.7f,0.25f);
		for (int i=0;i<6;i++) {
			float a=(float)i/6.f*(float)(M_PI*2);
			cyl(0.f, 0.4f, 0.1f, 0.55f, 0.35f, 0.15f, 8, cosf(a)*0.95f, sinf(a)*0.95f);
		}
	} else if (theme == THEME_NIGHT) {
		box(-0.7f,0,-0.7f,0.7f,4.6f,0.7f,0.28f,0.32f,0.55f);
		box(-0.9f,0,-0.9f,0.9f,0.3f,0.9f,0.22f,0.25f,0.4f);
		for (int w=0;w<6;w++) {
			float yy=0.5f+w*0.65f;
			box(-0.5f,yy,-0.72f,-0.18f,yy+0.26f,-0.68f,1.f,0.92f,0.45f);
			box(0.18f,yy,0.68f,0.5f,yy+0.26f,0.72f,1.f,0.88f,0.4f);
		}
		box(-0.18f,4.6f,-0.18f,0.18f,5.15f,0.18f,0.9f,0.3f,0.35f);
	} else if (theme == THEME_UNDER) {
		cyl(0.f, 0.5f, 1.2f, 0.22f, 0.6f, 0.85f, 18, 0.f, 0.f);
		cone(0.35f, 2.4f, 1.0f, 0.1f, 0.35f, 0.82f, 0.95f, 16);
		cone(0.2f, 1.7f, 0.6f, 0.08f, 1.f, 0.5f, 0.75f, 12);
		for (int i=0;i<8;i++) {
			float a=(float)i/8.f*(float)(M_PI*2);
			cyl(0.f, 0.7f+0.25f*sinf(a*2.f), 0.07f, 0.3f, 0.9f, 0.7f, 6, cosf(a)*0.85f, sinf(a)*0.85f);
		}
	} else if (theme == THEME_GRASS) {
		cyl(0.f, 0.95f, 0.2f, 0.5f, 0.38f, 0.18f, 12, 0.f, 0.f);
		cone(0.55f, 2.7f, 1.15f, 0.12f, 0.5f, 0.9f, 0.32f, 16);
		cone(1.4f, 3.2f, 0.8f, 0.08f, 0.45f, 0.85f, 0.3f, 12);
		box(-0.9f,0,-0.9f,0.9f,0.18f,0.9f,0.65f,0.82f,0.38f);
	} else if (theme == THEME_MESA) {
		// 段丘は太いブロックのみ（薄い板や針を作らない）
		box(-1.4f,0,-1.4f,1.4f,0.7f,1.4f,1.f,0.52f,0.28f);
		box(-1.0f,0.7f,-1.0f,1.0f,1.8f,1.0f,0.98f,0.58f,0.32f);
		box(-0.65f,1.8f,-0.65f,0.65f,2.7f,0.65f,0.95f,0.5f,0.26f);
		box(-0.35f,2.7f,-0.35f,0.35f,3.25f,0.35f,0.9f,0.45f,0.22f);
		box(-1.9f,0,0.7f,-1.2f,1.0f,1.4f,0.85f,0.4f,0.22f);
		box(1.15f,0,-1.7f,1.85f,1.15f,-1.0f,0.9f,0.48f,0.25f);
	} else { // cloud garden
		cyl(0.4f, 1.4f, 1.05f, 0.95f, 0.95f, 1.f, 16, 0.f, 0.f);
		cyl(1.0f, 2.05f, 0.85f, 1.f, 0.92f, 0.98f, 14, 0.f, 0.f);
		cyl(1.6f, 2.55f, 0.6f, 1.f, 0.88f, 0.96f, 12, 0.f, 0.f);
		cyl(2.1f, 2.9f, 0.38f, 1.f, 0.85f, 0.95f, 10, 0.f, 0.f);
		box(-0.3f,0,-0.3f,0.3f,0.55f,0.3f,0.8f,0.95f,0.7f);
		for (int i=0;i<6;i++) {
			float a=(float)i/6.f*(float)(M_PI*2);
			cyl(0.7f, 1.35f, 0.28f, 0.95f, 0.9f, 1.f, 10, cosf(a)*0.7f, sinf(a)*0.7f);
		}
	}
}

void CSoft3DRaceDlg::PlaceObstaclesAndItems()
{
	m_obsN=0; m_itemN=0;
	DWORD rng=m_genSeed^0xA5A5u;
	auto addObs=[&](float x,float y,float z,float yaw,float pitch,float sx,float sy,float sz,int kind,float dmg,float pathT,int hazard){
		if (m_obsN >= S3R_MAX_OBS) return;
		S3rObs& o=m_obs[m_obsN++];
		o.x=x;o.y=y;o.z=z;o.yaw=yaw;o.pitch=pitch;o.sx=sx;o.sy=sy;o.sz=sz;o.kind=kind;o.damage=dmg;o.pathT=pathT;o.hazard=hazard;
	};

	// サイドゲート＋通過枠（テクニカル区間）
	for (int g=0;g<40 && m_obsN+6<S3R_MAX_OBS;g++){
		float t=(float)g/40.f;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float gy=GroundY(px,pz);
		int sec=((int)(t*m_knotN))%10;
		float half=m_bandHalf;
		float side = half * (2.1f + S3rRand01(rng)*0.55f);
		float hs= (m_themeActive==THEME_FOREST)? (2.2f+S3rRand01(rng)*1.8f) : (1.7f+S3rRand01(rng)*1.1f);
		float tall=(m_themeActive==THEME_FOREST)? (4.2f+S3rRand01(rng)*2.6f) : (3.0f+S3rRand01(rng)*2.0f);
		addObs(px+bx*side, gy, pz+bz*side, atan2f(tx,tz), 0, hs*0.5f, tall, hs*0.5f, 1, 7.f, t, 1);
		addObs(px-bx*side, gy, pz-bz*side, atan2f(tx,tz), 0, hs*0.5f, tall, hs*0.5f, 1, 7.f, t, 1);
		// 頭上の梁（潜る／越える）
		if ((sec==2||sec==3||sec==5||sec==7) && (g%2)==0) {
			float topY = py + half * (0.95f + 0.55f * S3rRand01(rng));
			addObs(px, topY, pz, atan2f(tx,tz), 0, half*1.05f, 0.4f, 0.5f, 2, 6.f, t, 1);
		}
		// 通過リング風の縦枠（帯内に左右柱）
		if ((g%5)==0) {
			float gate = half * 0.72f;
			float gh = 1.1f + S3rRand01(rng)*0.5f;
			addObs(px+bx*gate, py - half*0.15f, pz+bz*gate, atan2f(tx,tz), 0, 0.35f, gh*2.2f, 0.35f, 3, 8.f, t, 1);
			addObs(px-bx*gate, py - half*0.15f, pz-bz*gate, atan2f(tx,tz), 0, 0.35f, gh*2.2f, 0.35f, 3, 8.f, t, 1);
			addObs(px, py + gh*1.1f, pz, atan2f(tx,tz), 0, gate*1.1f, 0.32f, 0.4f, 2, 7.f, t, 1);
		}
	}

	// スラローム障害（帯のすぐ外側〜縁）— 織る必要あり
	int wantSlalom = 56;
	for (int i=0;i<wantSlalom && m_obsN<S3R_MAX_OBS;i++){
		float t = (float)i / (float)wantSlalom + S3rRand01(rng)*0.01f;
		if (t >= 1.f) t -= 1.f;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float side = ((i&1)?1.f:-1.f);
		float off = m_bandHalf * (1.05f + S3rRand01(rng)*0.55f);
		float up = (S3rRand01(rng)*0.55f - 0.1f) * m_bandHalf;
		float s = 0.55f + S3rRand01(rng)*0.7f;
		float tall = s * (1.1f + S3rRand01(rng)*1.4f);
		addObs(px + bx*side*off + nx*up*0.35f, py + ny*up*0.35f, pz + bz*side*off + nz*up*0.35f,
			atan2f(tx,tz) + (S3rRand01(rng)-0.5f)*0.4f, (S3rRand01(rng)-0.5f)*0.25f,
			s, tall, s, 4, 9.f, t, 1);
	}

	// 低空ブロック／浮遊障害（高度テク）
	int wantAir = 28;
	for (int i=0;i<wantAir && m_obsN<S3R_MAX_OBS;i++){
		float t = S3rRand01(rng);
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float lat = (S3rRand01(rng)*2.f-1.f) * m_bandHalf * 0.55f;
		float lift = (S3rRand01(rng) < 0.5f)
			? -m_bandHalf * (0.15f + S3rRand01(rng)*0.35f)  // 低空
			:  m_bandHalf * (0.55f + S3rRand01(rng)*0.7f); // 高空
		float s = 0.7f + S3rRand01(rng)*0.9f;
		addObs(px+bx*lat+nx*lift, py+by*lat+ny*lift, pz+bz*lat+nz*lift,
			atan2f(tx,tz), 0.f, s, s*0.7f, s, 5, 8.f, t, 1);
	}

	// 景色用（ダメージなし）— 障害の後に残り枠で埋める
	int wantFill = 320;
	if (wantFill > S3R_MAX_OBS - m_obsN) wantFill = S3R_MAX_OBS - m_obsN;
	for (int i=0;i<wantFill;i++){
		float t = S3rRand01(rng);
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float side = (S3rRand01(rng) < 0.5f) ? 1.f : -1.f;
		float ring = S3rRand01(rng);
		float dist = (ring < 0.55f)
			? m_bandHalf * (4.2f + S3rRand01(rng) * 8.f)
			: m_bandHalf * (12.f + S3rRand01(rng) * 22.f);
		float along = (S3rRand01(rng)*2.f-1.f)*(ring<0.55f?12.f:28.f);
		float ox = px + bx * side * dist + tx * along;
		float oz = pz + bz * side * dist + tz * along;
		float gy = GroundY(ox, oz);
		float s = 0.85f + S3rRand01(rng) * (ring<0.55f ? 1.5f : 2.4f);
		float tall = s * (1.05f + S3rRand01(rng) * (ring<0.55f ? 1.15f : 1.55f));
		if (m_themeActive == THEME_FOREST) tall *= 1.35f;
		if (m_themeActive == THEME_NIGHT) tall *= 1.4f;
		if (m_themeActive == THEME_RUINS) { s *= 1.1f; tall *= 1.15f; }
		if (m_themeActive == THEME_CLOUD) { gy += 2.f + S3rRand01(rng)*14.f; tall *= 0.95f; }
		if (m_themeActive == THEME_MESA) { s *= 1.2f; tall *= 1.05f; }
		if (m_themeActive == THEME_OIL) tall *= 1.25f;
		addObs(ox, gy, oz, S3rRand01(rng)*(float)(M_PI*2), 0.f, s, tall, s, i%8, 0.f, t, 0);
	}

	const int kinds[]={KIND_TEMPO,KIND_TEMPO_DN,KIND_PITCH_UP,KIND_PITCH_DN,KIND_NEXT,KIND_PREV,KIND_VOL_UP,KIND_VOL_DN,KIND_EQ,KIND_EQ_FLAT,KIND_REVERB,KIND_XFADE,KIND_RANDOM};
	const int masks[]={ITEM_TEMPO,ITEM_TEMPO_DN,ITEM_PITCH_UP,ITEM_PITCH_DN,ITEM_NEXT,ITEM_PREV,ITEM_VOL_UP,ITEM_VOL_DN,ITEM_EQ,ITEM_EQ_FLAT,ITEM_REVERB,ITEM_XFADE,ITEM_RANDOM};
	int mask=ItemMask();
	int wantItems=36; if(wantItems>S3R_MAX_ITEMS) wantItems=S3R_MAX_ITEMS;
	for(int i=0;i<wantItems;i++){
		int ki=i%13; if(!(mask&masks[ki])) continue;
		float t=S3rRand01(rng);
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float off=(S3rRand01(rng)*2.f-1.f)*m_bandHalf*0.4f;
		S3rItem& it=m_items[m_itemN++];
		it.x=px+bx*off; it.y=py+by*off+nx*0.35f; it.z=pz+bz*off;
		it.kind=kinds[ki]; it.taken=0; it.spin=S3rRand01(rng)*(float)(M_PI*2); it.pathT=t;
	}
}

void CSoft3DRaceDlg::ResetRaceState()
{
	m_phase=PHASE_IDLE; m_countT=0; m_countShown=-1; m_podiumT=0; m_raceClock=0;
	m_camYawOff=0; m_camPitchOff=0.22f;
	m_camSmoothInit=0;
	m_mouseLook=0;
	m_camZoom = (savedata.s3r_zoom>=50&&savedata.s3r_zoom<=250)? (savedata.s3r_zoom/100.f) : 1.f;
	m_lookback=0; m_accelHeld=0; m_brakeHeld=0;
	m_reverbFogBoost=0; m_eqDofBoost=0;
	m_playerSpdEma=0.f;
	m_wrongWay=0; m_overlayHold=0.f;
	m_standDirty=1;
	int opp=ReadOppFromUi(); if(opp<1)opp=1; if(opp>11)opp=11;
	m_craftN = 1 + opp;
	// 名前プールから重複なくランダム割当
	int namePick[100];
	for (int i=0;i<100;i++) namePick[i]=i;
	for (int i=99;i>0;i--) {
		int j = (int)(S3rRand01(m_rng) * (float)(i + 1)); if (j < 0) j = 0; if (j > i) j = i;
		int tmp=namePick[i]; namePick[i]=namePick[j]; namePick[j]=tmp;
	}
	// 超簡単〜強烈：やや易しめ（普通が楽しめる帯）
	float aiLineTable[5]={0.52f, 0.62f, 0.74f, 0.85f, 0.92f};
	int aiLv=ReadAiFromUi();
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i]; memset(&c,0,sizeof(c));
		c.isPlayer=(i==0); c.alive=1; c.colorIdx=i%12; c.hp=100.f; c.fuel=100.f;
		wcscpy_s(c.name, kS3rGirlNames[namePick[i % 100]]);
		c.lapTimesN = 0;
		c.pathT = (float)i * 0.012f; if(c.pathT>0.9f)c.pathT=0.01f;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(c.pathT,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float lane=(i-(m_craftN-1)*0.5f)*1.2f;
		c.x=px+bx*lane; c.y=py+by*lane; c.z=pz+bz*lane;
		AlignCraftToPath(c, 0.06f);
		if (c.isPlayer) {
			c.aiSkill = 0.f;
		} else {
			float tier = (m_craftN <= 2) ? 0.5f : (float)(i - 1) / (float)(m_craftN - 2);
			float base = aiLineTable[aiLv];
			c.aiSkill = S3rClamp(base + (tier - 0.5f) * 0.04f + (S3rRand01(m_rng) - 0.5f) * 0.03f, 0.45f, 0.95f);
		}
		// レーン好みは弱め（端寄り暴走→帯外ループを減らす）
		c.aiSteerBias=(S3rRand01(m_rng)*2.f-1.f) * 0.55f;
		c.bestLap=1e9f;
		// チェックポイントは帯中央（端で保存すると復帰ループの温床）
		c.chkX=px; c.chkY=py; c.chkZ=pz;
		c.chkYaw=c.yaw; c.chkPitch=c.pitch; c.chkPathT=c.pathT;
		c.offBandT=0.f; c.courseOutCool=0.f; c.offBand=0;
		c.aiCutT=-1.f; c.aiCutTimer=0.f; c.aiCutCool=0.f;
	}
	for (int i=0;i<m_itemN;i++) m_items[i].taken=0;
	for (int i=0;i<96;i++) memset(m_confetti[i],0,sizeof(m_confetti[i]));
	m_clearBakeText=L""; m_clearDirty=1; m_hudDirty=1;
}

void CSoft3DRaceDlg::BeginDemoPreview()
{
	if (m_knotN < 4 || m_craftN < 1) { m_phase = PHASE_IDLE; return; }
	m_phase = PHASE_DEMO;
	m_demoCamT = 0.35f;
	m_demoCamElev = 0.f;
	m_camSmoothInit = 0;
	m_wrongWay = 0; m_overlayHold = 0.f;
	// 自機もデモ中はAI（スタートで通常操作に戻る）
	m_crafts[0].aiSkill = 0.70f;
	m_crafts[0].aiCutCool = 2.f; // 序盤はライン走行で見やすく
	m_playerSpdEma = RaceSpeedCap(0) * 0.55f;
	m_clearBakeText = LL14(L"スタートで開始", L"Press Start", L"Démarrer", L"Avvia", L"Iniciar",
		L"시작", L"按开始", L"Start", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat");
	m_clearBakeA = 1.f; m_clearDirty = 1; m_standDirty = 1;
}

void CSoft3DRaceDlg::StartRace()
{
	if (m_knotN < 4) GenerateCourse();
	ResetRaceState();
	m_phase = PHASE_COUNTDOWN;
	m_countT = 0; m_countShown = -1;
	m_camYawOff = 0.f; m_camPitchOff = 0.22f; m_camSmoothInit = 0;
	m_crafts[0].aiSkill = 0.f; // 操作はプレイヤーへ
	for (int i = 0; i < m_craftN; i++) {
		if (m_crafts[i].alive) AlignCraftToPath(m_crafts[i], 0.06f);
	}
	m_clearBakeText = L"5"; m_clearBakeA = 1.f; m_clearDirty = 1;
	UpdateStatus();
}

void CSoft3DRaceDlg::AlignCraftToPath(S3rCraft& c, float lookAhead)
{
	if (lookAhead < 0.02f) lookAhead = 0.02f;
	float tAim = c.pathT + lookAhead;
	while (tAim >= 1.f) tAim -= 1.f;
	while (tAim < 0.f) tAim += 1.f;
	float ax, ay, az;
	SplinePoint(tAim, ax, ay, az);
	float dx = ax - c.x, dy = ay - c.y, dz = az - c.z;
	float lenXZ = sqrtf(dx * dx + dz * dz);
	float len = sqrtf(dx * dx + dy * dy + dz * dz);
	if (lenXZ < 0.05f || len < 0.05f) {
		// ほぼ真上／真下：接線フォールバック
		float tx, ty, tz;
		SplineTangent(c.pathT + lookAhead * 0.5f, tx, ty, tz);
		dx = tx; dy = ty; dz = tz;
		lenXZ = sqrtf(dx * dx + dz * dz);
		len = 1.f;
	}
	// 水平向きはライン前方、ピッチは3D狙い
	c.yaw = atan2f(dx, dz);
	float pitchSrc = (len > 1e-4f) ? (dy / len) : 0.f;
	c.pitch = S3rClamp(asinf(S3rClamp(pitchSrc, -1.f, 1.f)), -0.85f, 0.85f);
	c.chkYaw = c.yaw; c.chkPitch = c.pitch;
}

void CSoft3DRaceDlg::InputSteerDelta(float dyaw, float dpitch)
{
	if (m_phase != PHASE_RACE && m_phase != PHASE_COUNTDOWN && m_phase != PHASE_DEMO) return;
	S3rCraft& pl = m_crafts[0];
	if (m_phase != PHASE_DEMO && (!pl.alive || pl.finished)) return;
	// デモ／カウント中は機体を触らずカメラだけで周りを見る
	if (m_phase == PHASE_COUNTDOWN || m_phase == PHASE_DEMO) {
		if (m_phase == PHASE_DEMO) {
			m_demoCamT = S3rNormAngle(m_demoCamT + dyaw * 0.85f);
			float inv = savedata.s3r_invert_y ? -1.f : 1.f;
			m_demoCamElev = S3rClamp(m_demoCamElev + inv * dpitch * 18.f, -25.f, 55.f);
		} else {
			m_camYawOff = S3rNormAngle(m_camYawOff + dyaw * 1.15f);
			float inv = savedata.s3r_invert_y ? -1.f : 1.f;
			m_camPitchOff = S3rClamp(m_camPitchOff + inv * dpitch * 0.85f, -0.35f, 0.72f);
		}
		return;
	}
	float ag = (pl.agilityT > 0.f) ? 1.45f : 1.f;
	pl.yaw = S3rNormAngle(pl.yaw + dyaw * ag);
	pl.pitch = S3rClamp(pl.pitch + dpitch * ag, -1.05f, 1.05f);
	m_camYawOff *= 0.85f;
	m_camPitchOff = S3rLerp(m_camPitchOff, 0.22f, 0.12f);
}
void CSoft3DRaceDlg::InputCameraDelta(float dyaw, float dpitch)
{
	m_camYawOff = S3rNormAngle(m_camYawOff + dyaw);
	m_camPitchOff = S3rClamp(m_camPitchOff + dpitch, 0.05f, 0.42f);
}
void CSoft3DRaceDlg::InputZoom(int dir)
{
	m_camZoom = S3rClamp(m_camZoom + (dir>0?-0.06f:0.06f), 0.55f, 2.2f);
	savedata.s3r_zoom = (int)(m_camZoom * 100.f + 0.5f);
}

void CSoft3DRaceDlg::ApplyItem(int kind)
{
	S3rCraft& pl = m_crafts[0];
	// 動画(mode=-2)は音／再生操作を無視。ゲーム側バフだけ適用
	const BOOL skipAud = (mode == -2) ? TRUE : FALSE;
	switch (kind) {
	case KIND_TEMPO: {
		if (!skipAud) {
			int pct = tempo / 2 + 10; if (pct > 200) pct = 200;
			if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->ApplyPracticeTempoPercent(pct);
			else { tempo = pct * 2; if (og && ::IsWindow(og->GetSafeHwnd())) og->m_tempo_sl.SetPos(tempo); }
		}
		pl.boostT = max(pl.boostT, 4.5f); break;
	}
	case KIND_TEMPO_DN: {
		if (!skipAud) {
			int pct = tempo / 2 - 10; if (pct < 25) pct = 25;
			if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->ApplyPracticeTempoPercent(pct);
			else { tempo = pct * 2; if (og && ::IsWindow(og->GetSafeHwnd())) og->m_tempo_sl.SetPos(tempo); }
		}
		pl.slowT = max(pl.slowT, 4.0f); break;
	}
	case KIND_PITCH_UP: if (!skipAud) S3rSetPitchPos(pitch + 20); pl.agilityT = max(pl.agilityT, 5.f); break;
	case KIND_PITCH_DN: if (!skipAud) S3rSetPitchPos(pitch - 20); pl.agilityT = max(pl.agilityT, 5.f); break;
	case KIND_NEXT: if (!skipAud) MpTaskbarNextTrack(); break;
	case KIND_PREV: if (!skipAud) MpTaskbarPrevTrack(); break;
	case KIND_VOL_UP: if (!skipAud) S3rNudgeVolPct(5); pl.flashT = max(pl.flashT, 0.7f); break;
	case KIND_VOL_DN: if (!skipAud) S3rNudgeVolPct(-5); pl.flashT = max(pl.flashT, 0.7f); break;
	case KIND_REVERB:
		if (!skipAud) S3rNudgeReverb(12);
		m_reverbFogBoost = min(1.f, m_reverbFogBoost + 0.35f); pl.fogT = max(pl.fogT, 6.f); break;
	case KIND_EQ:
		if (!skipAud) {
			m_rng = m_rng * 1664525u + 1013904223u; S3rEqBump((int)(m_rng % 15u), 18);
			m_rng = m_rng * 1664525u + 1013904223u; S3rEqBump((int)(m_rng % 15u), -10);
		} else {
			m_rng = m_rng * 1664525u + 1013904223u;
			m_rng = m_rng * 1664525u + 1013904223u;
		}
		m_eqDofBoost = min(1.f, m_eqDofBoost + 0.4f); pl.dofT = max(pl.dofT, 6.f); break;
	case KIND_EQ_FLAT:
		if (!skipAud) S3rEqFlatten(12);
		m_eqDofBoost = max(0.f, m_eqDofBoost - 0.25f); break;
	case KIND_XFADE: {
		if (skipAud) break;
		savedata.play_xfade = savedata.play_xfade ? 0 : 1;
		if (savedata.play_xfade) { int s = savedata.play_xfade_sec100 + 100; if (s < 200) s = 200; if (s > 12000) s = 12000; savedata.play_xfade_sec100 = s; }
		if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->SyncPlayXfadeUi(TRUE);
		else if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_xfade.GetSafeHwnd())
			og->m_xfade.SetCheck(savedata.play_xfade ? BST_CHECKED : BST_UNCHECKED);
		MpPersistSavedataQuick(); break;
	}
	case KIND_RANDOM:
		if (skipAud) break;
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (savedata.random == 0) og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK6, BN_CLICKED), 0);
			else og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK5, BN_CLICKED), 0);
		} else { savedata.random = savedata.random ? 0 : 1; MpPersistSavedataQuick(); }
		break;
	default: break;
	}
	if (!skipAud && !playf && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
}

void CSoft3DRaceDlg::TryPickupCraft(int ci)
{
	if (ci < 0 || ci >= m_craftN) return;
	S3rCraft& c = m_crafts[ci];
	if (!c.alive || c.finished) return;
	for (int i = 0; i < m_itemN; i++) {
		S3rItem& it = m_items[i];
		if (it.taken) continue;
		float dx=c.x-it.x, dy=c.y-it.y, dz=c.z-it.z;
		if (dx*dx+dy*dy+dz*dz < 2.2f*2.2f) {
			it.taken = 1;
			if (c.isPlayer) ApplyItem(it.kind);
			else {
				if (it.kind==KIND_TEMPO) c.boostT=max(c.boostT,3.5f);
				else if (it.kind==KIND_TEMPO_DN) c.slowT=max(c.slowT,3.f);
				else if (it.kind==KIND_PITCH_UP||it.kind==KIND_PITCH_DN) c.agilityT=max(c.agilityT,4.f);
			}
		}
	}
}

void CSoft3DRaceDlg::UpdateRanks()
{
	float prog[S3R_MAX_CRAFT];
	int order[S3R_MAX_CRAFT];
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		c.lapProgress = (float)c.lap + c.pathT;
		prog[i]=c.alive?(c.finished?1000.f+c.finishTime:c.lapProgress):-1.f;
		order[i]=i;
	}
	for (int i=0;i<m_craftN;i++) for (int j=i+1;j<m_craftN;j++){
		BOOL swap=FALSE;
		if (m_crafts[order[i]].finished && m_crafts[order[j]].finished) swap = m_crafts[order[i]].finishTime > m_crafts[order[j]].finishTime;
		else if (m_crafts[order[i]].finished) swap=FALSE;
		else if (m_crafts[order[j]].finished) swap=TRUE;
		else swap = prog[order[i]] < prog[order[j]];
		if (swap) { int t=order[i]; order[i]=order[j]; order[j]=t; }
	}
	for (int i=0;i<m_craftN;i++) m_crafts[order[i]].rank = i+1;
	m_standDirty = 1;
}

void CSoft3DRaceDlg::TickCountdown(float dt)
{
	m_countT += dt;
	// カウント中は帯レーンに吸着しつつ、前方ラインを向かせる
	for (int i = 0; i < m_craftN; i++) {
		S3rCraft& c = m_crafts[i];
		if (!c.alive) continue;
		float px, py, pz, tx, ty, tz, nx, ny, nz, bx, by, bz;
		SplineFrame(c.pathT, px, py, pz, tx, ty, tz, nx, ny, nz, bx, by, bz);
		float lane = (i - (m_craftN - 1) * 0.5f) * 1.2f;
		c.x = px + bx * lane;
		c.y = py + by * lane;
		c.z = pz + bz * lane;
		c.chkX = px; c.chkY = py; c.chkZ = pz; c.chkPathT = c.pathT;
		AlignCraftToPath(c, 0.06f);
		c.vx = c.vy = c.vz = 0.f;
	}
	// スタート直前は機体向き＝カメラ基準（見回しオフセットは残すがGOで戻す）
	if (m_countT < 0.05f) {
		m_camYawOff = 0.f;
		m_camPitchOff = 0.22f;
		m_camSmoothInit = 0;
	}
	if (m_countT < 5.f) {
		int show = 5 - (int)floorf(m_countT);
		if (show < 1) show = 1;
		if (show != m_countShown) {
			m_countShown = show;
			wchar_t buf[16]; swprintf_s(buf, L"%d", show);
			m_clearBakeText = buf;
			m_clearBakeA = 1.f;
			m_clearDirty = 1;
		}
	} else {
		// GO を約1秒表示してからレース開始
		if (m_countShown != 0) {
			m_countShown = 0;
			m_clearBakeText = LL14(L"GO!", L"GO!", L"GO!", L"VIA!", L"¡YA!", L"GO!", L"开始!", L"انطلق!", L"СТАРТ!", L"LOS!", L"JÁ!", L"START!", L"START!", L"BAŞLA!");
			m_clearBakeA = 1.f;
			m_clearDirty = 1;
		}
		if (m_countT >= 6.0f) {
			for (int i = 0; i < m_craftN; i++) {
				if (!m_crafts[i].alive) continue;
				S3rCraft& c = m_crafts[i];
				float px, py, pz, tx, ty, tz, nx, ny, nz, bx, by, bz;
				SplineFrame(c.pathT, px, py, pz, tx, ty, tz, nx, ny, nz, bx, by, bz);
				float lane = (i - (m_craftN - 1) * 0.5f) * 1.2f;
				c.x = px + bx * lane; c.y = py + by * lane; c.z = pz + bz * lane;
				AlignCraftToPath(c, 0.06f);
				float ax, ay, az; SplinePoint(c.pathT + 0.06f, ax, ay, az);
				float dx = ax - c.x, dy = ay - c.y, dz = az - c.z;
				S3rNorm3(dx, dy, dz);
				float kick = 8.f / 3.6f;
				c.vx = dx * kick; c.vy = dy * kick; c.vz = dz * kick;
			}
			// 見回しオフセットを戻して進行方向視点へ
			m_camYawOff = 0.f;
			m_camPitchOff = 0.22f;
			m_camSmoothInit = 0;
			m_phase = PHASE_RACE; m_raceClock = 0;
			m_overlayHold = 1.0f;
			m_playerSpdEma = 0.f;
			m_standDirty = 1;
		}
	}
}

void CSoft3DRaceDlg::TickPodium(float dt)
{
	m_podiumT += dt;
	// 表彰台配置：中央1位・左2位・右3位
	float h1 = 4.2f, h2 = 2.8f, h3 = 1.8f;
	float px[3] = { 0.f, -5.2f, 5.2f };
	float ph[3] = { h1, h2, h3 };
	int slotCraft[3] = { m_podiumOrder[0], m_podiumOrder[1], m_podiumOrder[2] };
	for (int s = 0; s < 3; s++) {
		int ci = slotCraft[s];
		if (ci < 0 || ci >= m_craftN) continue;
		S3rCraft& c = m_crafts[ci];
		c.x = m_podiumBaseX + px[s];
		c.y = m_podiumBaseY + ph[s] + 1.15f;
		c.z = m_podiumBaseZ;
		c.vx = c.vy = c.vz = 0.f;
		c.yaw = 0.f; // カメラ（+Z）を向く
		c.pitch = 0.f;
		c.offBand = 0;
	}
	for (int i=0;i<96;i++){
		if (m_confetti[i][5] <= 0.f) {
			m_confetti[i][0]=m_podiumBaseX+(S3rRand01(m_rng)*2.f-1.f)*14.f;
			m_confetti[i][1]=m_podiumBaseY+8.f+S3rRand01(m_rng)*10.f;
			m_confetti[i][2]=m_podiumBaseZ+(S3rRand01(m_rng)*2.f-1.f)*10.f;
			m_confetti[i][3]=(S3rRand01(m_rng)-.5f)*4.f;
			m_confetti[i][4]=-1.f-S3rRand01(m_rng)*3.f;
			m_confetti[i][5]=2.f+S3rRand01(m_rng)*2.f;
		} else {
			m_confetti[i][0]+=m_confetti[i][3]*dt;
			m_confetti[i][1]+=m_confetti[i][4]*dt;
			m_confetti[i][5]-=dt;
		}
	}
}

void CSoft3DRaceDlg::TickAi(float dt)
{
	const float scv = SpeedScale();
	const int demo = (m_phase == PHASE_DEMO) ? 1 : 0;
	if (!demo && m_phase != PHASE_RACE) return;
	const S3rCraft& pl = m_crafts[0];
	const float plProg = (float)pl.lap + pl.pathT;
	const float plNow = sqrtf(pl.vx*pl.vx + pl.vy*pl.vy + pl.vz*pl.vz);
	const int plRank = (pl.rank > 0) ? pl.rank : 1;
	float paceRef = m_playerSpdEma;
	if (paceRef < plNow) paceRef = plNow;
	if (paceRef < 6.f) paceRef = 6.f;
	if (demo) {
		// デモは自機ペースに依存せず見やすい巡航速度で
		paceRef = RaceSpeedCap(0) * 0.58f;
	}

	const int i0 = demo ? 0 : 1;
	for (int i=i0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (!c.alive || c.finished) continue;
		if (c.courseOutCool > 0.f) continue;
		float half=BandHalfWidth();
		float lat,vert,cx,cy,cz; BandLocal(c.x,c.y,c.z,c.pathT,lat,vert,cx,cy,cz);
		float latAbs=fabsf(lat), vertAbs=fabsf(vert);

		// ライン取り精度 = 難易度基準 + 順位ボーナス
		float sk = c.aiSkill;
		{
			int aiRank = (c.rank > 0) ? c.rank : (i + 1);
			int behind = aiRank - plRank;
			if (behind > 0) sk = S3rClamp(sk + (float)behind * 0.05f, 0.f, 1.f);
		}

		if (c.aiCutCool > 0.f) c.aiCutCool = max(0.f, c.aiCutCool - dt);
		const int lineLock = (c.aiCutCool > 0.f) ? 1 : 0;

		// 計画ショートカットの維持／終了判定
		int cutting = (c.aiCutT >= 0.f && c.aiCutTimer > 0.f) ? 1 : 0;
		if (cutting) {
			c.aiCutTimer -= dt;
			float jx,jy,jz; SplinePoint(c.aiCutT, jx,jy,jz);
			float jdx=c.x-jx, jdy=c.y-jy, jdz=c.z-jz;
			float jdist = sqrtf(jdx*jdx+jdy*jdy+jdz*jdz);
			const int success = (jdist < half * 1.05f) ? 1 : 0;
			// 失敗：時間切れ／燃料減／帯外で合流できない → 帯中央へ戻し通常ライン固定
			const int fail = (!success && (c.aiCutTimer <= 0.f || c.fuel < 45.f || (c.offBand && c.aiCutTimer < 0.55f))) ? 1 : 0;
			if (success) {
				float dJoin = c.aiCutT - c.pathT;
				while (dJoin > 0.5f) dJoin -= 1.f;
				while (dJoin < -0.5f) dJoin += 1.f;
				if (dJoin > 0.f && dJoin < 0.35f) c.pathT = c.aiCutT;
				c.aiCutT = -1.f; c.aiCutTimer = 0.f; cutting = 0;
				c.aiCutCool = max(c.aiCutCool, 4.f);
			} else if (fail) {
				AbortAiToLine(c, 20.f); // 失敗→しばらく通常ライン→またカット可
				cutting = 0;
				continue; // このフレームの推力は付けない
			}
		}

		// ライン固定中はカット禁止。クール明け＆技能高め＆燃料余裕のときだけ再挑戦
		if (!cutting && !lineLock && !c.offBand && c.fuel > 75.f && sk >= 0.86f) {
			float spans[2] = { 0.055f + 0.025f * sk, 0.08f + 0.035f * sk };
			float bestSave = 0.f, bestT = -1.f;
			for (int s = 0; s < 2; s++) {
				float tJoin = c.pathT + spans[s];
				while (tJoin >= 1.f) tJoin -= 1.f;
				float jx,jy,jz; SplinePoint(tJoin, jx,jy,jz);
				float dx=jx-c.x, dy=jy-c.y, dz=jz-c.z;
				float chord = sqrtf(dx*dx+dy*dy+dz*dz);
				float arc = PathArcBetween(c.pathT, tJoin);
				if (arc < half * 3.5f || chord < 1.f) continue;
				float ratio = chord / arc;
				float save = arc - chord;
				int clear = 1;
				for (int k = 1; k <= 3; k++) {
					float u = (float)k / 4.f;
					float sx = c.x + dx * u, sy = c.y + dy * u, sz = c.z + dz * u;
					if (sy < GroundY(sx, sz) + 1.6f) { clear = 0; break; }
				}
				if (!clear) continue;
				float needRatio = 0.54f - 0.06f * sk;
				float needSave = half * (3.6f - 0.7f * sk);
				if (ratio < needRatio && save > needSave && save > bestSave) {
					bestSave = save; bestT = tJoin;
				}
			}
			if (bestT >= 0.f) {
				c.aiCutT = bestT;
				c.aiCutTimer = 0.85f + 0.45f * sk;
				cutting = 1;
			}
		}

		float phase = m_anim * (0.25f + (1.f - sk) * 0.4f) + (float)i * 1.1f;
		float wanderAmp = lineLock ? 0.f : ((1.f - sk) * 0.14f);
		float laneFrac = S3rClamp(sinf(phase) * wanderAmp + c.aiSteerBias * wanderAmp * 0.25f, -0.18f, 0.18f);
		float vertFrac = S3rClamp(cosf(phase * 0.7f) * wanderAmp * 0.45f, -0.12f, 0.12f);

		float look = c.pathT + 0.02f + 0.03f * sk;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		if (cutting) {
			SplineFrame(c.aiCutT, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
		} else {
			SplineFrame(look, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
		}
		float aimx = px + (cutting || lineLock ? 0.f : bx * (laneFrac * half * 0.45f));
		float aimy = py + (cutting || lineLock ? 0.f : by * (laneFrac * half * 0.45f));
		float aimz = pz + (cutting || lineLock ? 0.f : bz * (laneFrac * half * 0.45f));
		if (!cutting && !lineLock) {
			aimx += nx * (vertFrac * half * 0.30f);
			aimy += ny * (vertFrac * half * 0.30f);
			aimz += nz * (vertFrac * half * 0.30f);
		}

		// レールばね：ライン固定中は中央へ強く、カット中は弱め
		{
			float rail = cutting ? (1.0f + 1.5f * sk) : (lineLock ? (12.f + 8.f * sk) : (6.5f + 9.f * sk));
			if (c.offBand && !cutting) rail *= 2.2f;
			c.vx += (-bx * lat - nx * vert) * rail * dt;
			c.vy += (-by * lat - ny * vert) * rail * dt;
			c.vz += (-bz * lat - nz * vert) * rail * dt;
			if (!cutting) {
				float outV = c.vx*bx + c.vy*by + c.vz*bz;
				if ((lat > 0.f && outV > 0.f) || (lat < 0.f && outV < 0.f)) {
					float kill = min(1.f, (3.0f + 5.f * sk) * dt);
					c.vx -= bx * outV * kill; c.vy -= by * outV * kill; c.vz -= bz * outV * kill;
				}
				float outN = c.vx*nx + c.vy*ny + c.vz*nz;
				if ((vert > 0.f && outN > 0.f) || (vert < 0.f && outN < 0.f)) {
					float kill = min(1.f, (3.0f + 5.f * sk) * dt);
					c.vx -= nx * outN * kill; c.vy -= ny * outN * kill; c.vz -= nz * outN * kill;
				}
			}
		}

		float tangYaw = atan2f(tx, tz);
		float tangPitch = S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -1.0f, 1.0f);
		float txp = aimx - c.x, typ = aimy - c.y, tzp = aimz - c.z;
		S3rNorm3(txp, typ, tzp);
		float aimYaw = atan2f(txp, tzp);
		float aimPitch = S3rClamp(asinf(S3rClamp(typ, -1.f, 1.f)), -1.0f, 1.0f);
		float tangBlend = cutting ? 0.15f : (lineLock ? 0.88f : (0.66f + 0.28f * sk));
		if (c.offBand && !cutting) tangBlend = 0.94f;
		float wantYaw = S3rNormAngle(aimYaw + S3rNormAngle(tangYaw - aimYaw) * tangBlend);
		float wantPitch = S3rLerp(aimPitch, tangPitch, tangBlend);
		float turnRate = (1.1f + sk * 1.8f) * (c.agilityT > 0 ? 1.25f : 1.f);
		if (c.offBand && !cutting) turnRate *= 1.4f;
		c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(wantYaw - c.yaw) * min(1.f, turnRate * dt));
		c.pitch = S3rLerp(c.pitch, wantPitch, min(1.f, turnRate * dt));
		c.pitch = S3rClamp(c.pitch, -1.05f, 1.05f);

		float targetSpd = paceRef * (0.88f + 0.16f * sk);
		float hardCap = max(plNow, m_playerSpdEma) * (1.02f + 0.14f * sk);
		if (hardCap < 9.f) hardCap = 9.f;
		float raceCap = RaceSpeedCap(c.boostT > 0.f ? 1 : 0);
		float softFloor = raceCap * (0.20f + 0.28f * sk);
		if (targetSpd < softFloor) targetSpd = softFloor;
		if (hardCap < softFloor) hardCap = softFloor;
		if (hardCap > raceCap) hardCap = raceCap;
		if (targetSpd > hardCap) targetSpd = hardCap;

		float lead = ((float)c.lap + c.pathT) - plProg;
		if (lead > 0.08f) {
			float cut = S3rSaturate((lead - 0.08f) / 0.30f);
			targetSpd *= (1.f - cut * (0.40f - 0.10f * sk));
		} else if (lead < -0.08f) {
			float catchUp = S3rSaturate((-lead - 0.08f) / 0.35f);
			targetSpd *= (1.f + catchUp * (0.08f + 0.10f * sk));
			if (targetSpd > hardCap) targetSpd = hardCap;
		}

		float thrust = (44.f + 28.f * sk) * scv;
		if (cutting) {
			thrust *= 1.08f;
			targetSpd = min(max(targetSpd, raceCap * (0.62f + 0.16f * sk)), raceCap * 0.90f);
		} else if (c.offBand) {
			thrust *= 0.12f;
			targetSpd = min(targetSpd, 30.f / 3.6f);
		}
		float spdNow = sqrtf(c.vx*c.vx + c.vy*c.vy + c.vz*c.vz);
		if (spdNow > targetSpd) thrust *= 0.06f;
		else if (spdNow > targetSpd * 0.92f) thrust *= 0.35f;

		if (!cutting) {
			float t0x,t0y,t0z,t1x,t1y,t1z;
			SplineTangent(c.pathT + 0.01f, t0x,t0y,t0z);
			SplineTangent(c.pathT + 0.05f, t1x,t1y,t1z);
			float align = t0x*t1x + t0y*t1y + t0z*t1z;
			float bend = S3rSaturate(1.f - align);
			thrust *= (1.f - bend * (0.38f * (1.f - 0.55f * sk)));
		}

		float edgeDanger = max(latAbs / max(0.01f, half), vertAbs / max(0.01f, half * 0.85f));
		if (!cutting && edgeDanger > 0.72f) {
			float caution = S3rSaturate((edgeDanger - 0.72f) / 0.55f);
			thrust *= (1.f - caution * (0.28f + 0.12f * sk));
			targetSpd *= (1.f - caution * 0.28f);
			if (spdNow > targetSpd) {
				float s = targetSpd / max(0.01f, spdNow);
				c.vx *= s; c.vy *= s; c.vz *= s;
			}
		}
		if (c.boostT>0) thrust *= 1.06f;
		if (c.slowT>0) thrust *= 0.62f;
		if (c.fuel < 8.f) thrust *= 0.35f;
		if (c.fuel <= 0.01f) thrust = 0.f;

		float fx = sinf(c.yaw)*cosf(c.pitch), fy = sinf(c.pitch), fz = cosf(c.yaw)*cosf(c.pitch);
		if (cutting) {
			// カット中は合流点方向へ推進
			fx = txp; fy = typ; fz = tzp;
		} else {
			float tangW = 0.35f + 0.5f * sk;
			fx = fx * (1.f - tangW) + tx * tangW;
			fy = fy * (1.f - tangW) + ty * tangW;
			fz = fz * (1.f - tangW) + tz * tangW;
			S3rNorm3(fx, fy, fz);
		}
		c.vx += fx * thrust * dt; c.vy += fy * thrust * dt; c.vz += fz * thrust * dt;
		if (ty > 0.12f && !c.offBand && !cutting) {
			float climb = thrust * (0.15f + 0.35f * ty) * dt;
			c.vx += tx * climb; c.vy += ty * climb; c.vz += tz * climb;
		}
	}
}

void CSoft3DRaceDlg::TickItems(float dt)
{
	for (int i=0;i<m_itemN;i++) if (!m_items[i].taken) m_items[i].spin += dt * 2.2f;
	if (m_phase == PHASE_DEMO) {
		// デモ中は見た目の回転のみ（音響アイテム適用なし）
		if (m_reverbFogBoost>0) m_reverbFogBoost=max(0.f,m_reverbFogBoost-dt*0.05f);
		if (m_eqDofBoost>0) m_eqDofBoost=max(0.f,m_eqDofBoost-dt*0.05f);
		return;
	}
	for (int i=0;i<m_craftN;i++) {
		S3rCraft& c=m_crafts[i];
		if (c.boostT>0) c.boostT-=dt; if (c.slowT>0) c.slowT-=dt; if (c.agilityT>0) c.agilityT-=dt;
		if (c.fogT>0) c.fogT-=dt; if (c.dofT>0) c.dofT-=dt; if (c.flashT>0) c.flashT-=dt;
		if (c.smokeT>0) c.smokeT-=dt;
		TryPickupCraft(i);
	}
	if (m_reverbFogBoost>0) m_reverbFogBoost=max(0.f,m_reverbFogBoost-dt*0.05f);
	if (m_eqDofBoost>0) m_eqDofBoost=max(0.f,m_eqDofBoost-dt*0.05f);
}

void CSoft3DRaceDlg::TickPhysics(float dt)
{
	if (m_phase != PHASE_RACE && m_phase != PHASE_FINISH && m_phase != PHASE_DEMO) return;
	const int demo = (m_phase == PHASE_DEMO) ? 1 : 0;

	// player input + joypad（デモ中はAI任せ）
	S3rJoyState joy={}; UpdateJoypadState(joy);
	S3rCraft& pl = m_crafts[0];
	if (!demo && pl.alive && !pl.finished && m_phase==PHASE_RACE) {
		if (pl.courseOutCool > 0.f) {
			// 復帰クール中は入力推力を切って帯へ定着させる
			m_playerAccel = 0;
		} else {
		float steerX = joy.connected ? joy.lx : 0.f;
		float steerY = joy.connected ? -joy.ly : 0.f;
		if (joy.hat >= 0) {
			static const float hx[8]={0,0.7f,1,0.7f,0,-0.7f,-1,-0.7f};
			static const float hy[8]={1,0.7f,0,-0.7f,-1,-0.7f,0,0.7f};
			steerX += hx[joy.hat]; steerY += hy[joy.hat];
		}
		if (savedata.s3r_invert_y) steerY = -steerY;
		float ag = (pl.agilityT>0)?1.45f:1.f;
		pl.yaw = S3rNormAngle(pl.yaw + steerX * 1.9f * ag * dt);
		pl.pitch = S3rClamp(pl.pitch + steerY * 2.15f * ag * dt, -1.05f, 1.05f);
		// 入力が弱いときはコース接線へ追従（急勾配は許容。真下張り付きだけ防ぐ）
		if (fabsf(steerX) < 0.12f && fabsf(steerY) < 0.12f) {
			float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(pl.pathT+0.02f,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
			float want = S3rClamp(asinf(S3rClamp(ty,-1.f,1.f)), -0.95f, 0.95f);
			pl.pitch = S3rLerp(pl.pitch, want, min(1.f, 2.2f * dt));
		}
		if (joy.connected) {
			m_camYawOff = S3rNormAngle(m_camYawOff + joy.rx * 1.5f * dt);
			m_camPitchOff = S3rClamp(m_camPitchOff - joy.ry * 0.9f * dt, 0.05f, 0.42f);
		}
		BOOL accel = m_accelHeld || (joy.connected && (joy.rt > 0.15f || (joy.buttons & 1)));
		BOOL brake = m_brakeHeld || (joy.connected && (joy.lt > 0.15f || (joy.buttons & 2)));
		float fx=sinf(pl.yaw)*cosf(pl.pitch), fy=sinf(pl.pitch), fz=cosf(pl.yaw)*cosf(pl.pitch);
		const float scv = SpeedScale();
		// 直線で約120km/h付近に届く推力（キャップと対）
		float thrust = accel ? (72.f * scv) : 0.f;
		if (pl.boostT>0) thrust *= 1.25f;
		if (pl.slowT>0) thrust *= 0.55f;
		if (pl.fuel < 5.f) thrust *= 0.25f;
		if (pl.fuel <= 0.01f) thrust = 0.f;
		if (pl.offBand) thrust *= 0.55f;
		if (accel) { pl.vx+=fx*thrust*dt; pl.vy+=fy*thrust*dt; pl.vz+=fz*thrust*dt; }
		// 急勾配：アクセル中はコース接線の上昇成分を補助（ピッチ制限だけでは足りない対策）
		if (accel && thrust > 1.f && !pl.offBand) {
			float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(pl.pathT+0.03f,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
			if (ty > 0.12f) {
				float climb = thrust * (0.22f + 0.55f * ty) * dt;
				pl.vx += tx * climb; pl.vy += ty * climb; pl.vz += tz * climb;
			}
		}
		if (brake) { pl.vx*=(1.f-3.4f*dt); pl.vy*=(1.f-3.4f*dt); pl.vz*=(1.f-3.4f*dt); }
		// 旋回入力そのもので速度が落ちる（カーブの抵抗感）
		float turnIn = min(1.f, fabsf(steerX) + fabsf(steerY));
		if (turnIn > 0.08f) {
			float bleed = (0.55f + turnIn * 1.1f) * (1.f + SpeedToKmh(pl.vx,pl.vy,pl.vz) * 0.0015f);
			pl.vx*=(1.f-bleed*dt); pl.vy*=(1.f-bleed*dt); pl.vz*=(1.f-bleed*dt);
		}
		m_playerAccel = accel ? 1 : 0;
		}
	} else {
		m_playerAccel = 0;
	}
	// 自機の実速度を平滑化（AIのペース基準）。減速時は速めに落とす
	{
		float ps = sqrtf(pl.vx*pl.vx+pl.vy*pl.vy+pl.vz*pl.vz);
		float k = (ps > m_playerSpdEma) ? min(1.f, 4.0f*dt) : min(1.f, 3.2f*dt);
		m_playerSpdEma = S3rLerp(m_playerSpdEma, ps, k);
	}

	UpdateRanks(); // 順位ボーナス用に AI 前に確定
	TickAi(dt);

	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (!c.alive) { c.smokeT=max(c.smokeT,0.5f); continue; }
		if (c.finished) continue;

		// 進行方向へのグリップ＋横滑り抵抗（カーブで速度が流れる）
		float fx=sinf(c.yaw)*cosf(c.pitch), fy=sinf(c.pitch), fz=cosf(c.yaw)*cosf(c.pitch);
		float spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		float along=0.f, latSpd=0.f;
		if (spd>0.01f) {
			along=c.vx*fx+c.vy*fy+c.vz*fz;
			float lx=c.vx-fx*along, ly=c.vy-fy*along, lz=c.vz-fz*along;
			latSpd=sqrtf(lx*lx+ly*ly+lz*lz);
			// 完全固定せず、ある程度スライドを残す
			float grip=(c.offBand?0.55f:1.45f)*(c.agilityT>0?1.35f:1.f);
			c.vx-=lx*min(1.f,grip*dt); c.vy-=ly*min(1.f,grip*dt); c.vz-=lz*min(1.f,grip*dt);
			along=c.vx*fx+c.vy*fy+c.vz*fz;
			lx=c.vx-fx*along; ly=c.vy-fy*along; lz=c.vz-fz*along;
			latSpd=sqrtf(lx*lx+ly*ly+lz*lz);
			// 横速度が大きいほど前方速度が削られる（カーブ抵抗）
			if (along > 0.f && latSpd > 0.5f) {
				float slip = latSpd / (fabsf(along) + latSpd + 1.f);
				float cornerBleed = (0.7f + 0.012f * fabsf(along)) * slip * slip * (c.offBand?1.6f:1.f);
				along *= (1.f - min(0.85f, cornerBleed * dt * 3.2f));
				c.vx = fx*along + lx; c.vy = fy*along + ly; c.vz = fz*along + lz;
			}
		}
		// 空気抵抗＋コース曲率による衰退（自機・敵共通。難所で 70→30km/h 付近まで落ちる）
		{
			spd = sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
			float lin = c.offBand ? 2.4f : 0.22f;
			float quad = c.offBand ? 0.008f : 0.00135f;
			if (c.isPlayer && !m_playerAccel && !demo) lin += 0.7f;
			float t0x,t0y,t0z,t1x,t1y,t1z;
			SplineTangent(c.pathT, t0x,t0y,t0z);
			SplineTangent(c.pathT + 0.04f, t1x,t1y,t1z);
			float align = t0x*t1x + t0y*t1y + t0z*t1z;
			float bend = S3rSaturate(1.f - align); // 直線0、急カーブで大きく
			float curveDrag = bend * (1.15f + 0.045f * spd);
			float damp = lin + quad * spd + curveDrag;
			c.vx*=(1.f-damp*dt); c.vy*=(1.f-damp*dt); c.vz*=(1.f-damp*dt);
		}
		// 絶対上限 ≈120km/h（ブースト時やや上）。敵も同じ世界の法則
		float maxSpd = RaceSpeedCap(c.boostT > 0.f ? 1 : 0);
		if (!c.isPlayer || demo) {
			const float plNow = sqrtf(m_crafts[0].vx*m_crafts[0].vx + m_crafts[0].vy*m_crafts[0].vy + m_crafts[0].vz*m_crafts[0].vz);
			float pace = demo ? (RaceSpeedCap(0) * 0.58f) : m_playerSpdEma;
			if (!demo && pace < plNow) pace = plNow;
			if (pace < 6.f) pace = 6.f;
			float sk = c.aiSkill;
			if (demo && c.isPlayer && sk < 0.45f) sk = 0.70f;
			float aiCap = pace * (0.88f + 0.16f * sk);
			float hardCap = demo ? (pace * (1.05f + 0.12f * sk)) : (max(plNow, m_playerSpdEma) * (1.02f + 0.14f * sk));
			float softFloor = RaceSpeedCap(0) * (0.20f + 0.28f * sk);
			if (aiCap < softFloor) aiCap = softFloor;
			if (hardCap < softFloor) hardCap = softFloor;
			if (hardCap < 9.f) hardCap = 9.f;
			if (aiCap > hardCap) aiCap = hardCap;
			if (aiCap < maxSpd) maxSpd = aiCap;
		}
		if (c.offBand) {
			// 計画カット中だけ帯外でもレース速度を維持（事故帯外は≈30km/h）
			int cutting = ((!c.isPlayer || demo) && c.aiCutT >= 0.f && c.aiCutTimer > 0.f) ? 1 : 0;
			if (cutting) maxSpd = min(maxSpd, RaceSpeedCap(0) * (0.72f + 0.12f * c.aiSkill));
			else maxSpd = min(maxSpd * 0.45f, 30.f / 3.6f);
		}
		spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		if (spd>maxSpd){ float s=maxSpd/spd; c.vx*=s;c.vy*=s;c.vz*=s; }

		if (c.courseOutCool > 0.f) c.courseOutCool = max(0.f, c.courseOutCool - dt);

		c.x+=c.vx*dt; c.y+=c.vy*dt; c.z+=c.vz*dt;

		// 地形ヒット（当たるとダメージ）— コースアウト復帰とは別
		{
			float gy = GroundY(c.x, c.z);
			const float clearance = 1.35f;
			if (c.y < gy + clearance) {
				c.y = gy + clearance;
				if (c.vy < 0.f) c.vy = -c.vy * 0.25f;
				c.hp -= 16.f * dt;
				c.vx *= (1.f - 1.5f * dt); c.vz *= (1.f - 1.5f * dt);
			}
		}

		float prevT=c.pathT;
		spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		c.pathT = AdvancePathT(c.x,c.y,c.z,c.pathT,spd,dt);
		float lat, vert, cx, cy, cz;
		BandLocal(c.x, c.y, c.z, c.pathT, lat, vert, cx, cy, cz);
		float half=BandHalfWidth();
		float latAbs=fabsf(lat), vertAbs=fabsf(vert);
		// 許容マージン広め（トリッキーな操作・短い帯外を許容）
		const float onLat = half * 1.55f;
		const float onVert = half * 1.35f;
		if (latAbs <= onLat && vertAbs <= onVert) {
			c.offBand = 0;
			c.fuel = min(100.f, c.fuel + 28.f * dt);
			if (latAbs <= half * 0.42f && vertAbs <= half * 0.38f) {
				c.hp = min(100.f, c.hp + 14.f * dt);
			}
			// 帯中央寄りでのみチェックポイント更新（端保存→復帰ループ防止）
			if (latAbs <= half * 0.80f && vertAbs <= half * 0.70f) {
				float tcx, tcy, tcz; SplinePoint(c.pathT, tcx, tcy, tcz);
				c.chkX = tcx; c.chkY = tcy; c.chkZ = tcz;
				float ttx,tty,ttz; SplineTangent(c.pathT, ttx,tty,ttz);
				c.chkYaw = atan2f(ttx, ttz);
				c.chkPitch = S3rClamp(asinf(S3rClamp(tty, -1.f, 1.f)), -0.55f, 0.55f);
				c.chkPathT = c.pathT;
			}
			c.offBandT = 0.f;
			float tx,ty,tz,nx,ny,nz,bx,by,bz;
			SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
			float pull = min(1.f, 0.55f * dt);
			float rail = 0.08f;
			if (!c.isPlayer || demo) {
				float sk = c.aiSkill;
				if (demo && c.isPlayer && sk < 0.45f) sk = 0.70f;
				int plRank = (m_crafts[0].rank > 0) ? m_crafts[0].rank : 1;
				int aiRank = (c.rank > 0) ? c.rank : 2;
				if (!demo && aiRank > plRank) sk = S3rClamp(sk + (float)(aiRank - plRank) * 0.07f, 0.f, 1.f);
				rail = 0.22f + 0.28f * sk;
				// 計画カット中は帯内レールを弱めて弦へ出やすくする
				if (c.aiCutT >= 0.f && c.aiCutTimer > 0.f) rail = 0.04f;
			}
			c.x -= (bx * lat + nx * vert) * rail * pull;
			c.y -= (by * lat + ny * vert) * rail * pull;
			c.z -= (bz * lat + nz * vert) * rail * pull;
		} else if (c.courseOutCool <= 0.f) {
			c.offBand = 1;
			c.offBandT += dt;
			if (!c.isPlayer || demo) {
				const int cutting = (c.aiCutT >= 0.f && c.aiCutTimer > 0.f) ? 1 : 0;
				if (cutting) {
					// 計画ショートカット中：緩い燃料減＋合流点へ。危険なら即ライン復帰
					c.fuel = max(0.f, c.fuel - 9.f * dt);
					c.vx *= (1.f - 0.45f * dt);
					c.vy *= (1.f - 0.45f * dt);
					c.vz *= (1.f - 0.45f * dt);
					float jx,jy,jz; SplinePoint(c.aiCutT, jx,jy,jz);
					float hx = jx - c.x, hy = jy - c.y, hz = jz - c.z;
					float hd = sqrtf(hx*hx+hy*hy+hz*hz);
					if (hd > 0.01f) {
						float pull = 3.5f + 2.5f * c.aiSkill;
						c.vx += (hx / hd) * pull * dt;
						c.vy += (hy / hd) * pull * dt;
						c.vz += (hz / hd) * pull * dt;
						c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(atan2f(hx, hz) - c.yaw) * min(1.f, 2.8f*dt));
					}
					if (c.fuel < 48.f || c.offBandT > 1.8f) {
						AbortAiToLine(c, 20.f);
					}
				} else {
					// 事故帯外：早めにラインへ戻す（COURSE OUT 連発を避ける）
					c.fuel = max(0.f, c.fuel - 14.f * dt);
					float starve = 1.f + (1.f - c.fuel / 100.f) * 1.6f;
					c.vx *= (1.f - 2.0f * starve * dt);
					c.vy *= (1.f - 2.0f * starve * dt);
					c.vz *= (1.f - 2.0f * starve * dt);
					float tx,ty,tz,nx,ny,nz,bx,by,bz;
					SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
					float hx = cx - c.x, hy = cy - c.y, hz = cz - c.z;
					float hd = sqrtf(hx*hx+hy*hy+hz*hz);
					if (hd > 0.01f) {
						float pull = (3.5f + 4.f * c.aiSkill);
						c.vx += (hx / hd) * pull * dt;
						c.vy += (hy / hd) * pull * dt;
						c.vz += (hz / hd) * pull * dt;
					}
					c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(atan2f(tx,tz) - c.yaw) * min(1.f, 4.0f*dt));
					if (c.fuel < 55.f || c.offBandT > 1.25f) {
						AbortAiToLine(c, 16.f);
					}
				}
			} else {
				// 自機：帯外フリー走行。推進力0で離脱地点（帯中央）へ復帰
				c.fuel = max(0.f, c.fuel - 24.f * dt);
				float starve = 1.f + (1.f - c.fuel / 100.f) * 2.2f;
				c.vx *= (1.f - 2.2f * starve * dt); c.vy *= (1.f - 2.2f * starve * dt); c.vz *= (1.f - 2.2f * starve * dt);
				if (c.fuel <= 0.01f) {
					RespawnCraftToCheckpoint(c, 80.f, 2.0f);
					if (m_phase == PHASE_RACE) {
						m_clearBakeText = LL14(L"COURSE OUT", L"COURSE OUT", L"HORS PISTE", L"FUORI PISTA", L"FUERA DE PISTA",
							L"코스 아웃", L"冲出赛道", L"خارج المسار", L"ВНЕ ТРАССЫ", L"COURSE OUT", L"FORA DA PISTA", L"COURSE OUT", L"POZA TORU", L"KURS DIŞI");
						m_clearBakeA = 1.f; m_clearDirty = 1; m_overlayHold = 1.5f;
					}
				}
			}
		} else {
			// クール中：帯中央へ強吸着＋燃料回復（連発コースアウト防止）
			c.offBand = 0;
			c.pathT = c.chkPathT;
			float tx,ty,tz,nx,ny,nz,bx,by,bz;
			SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
			float snap = min(1.f, 8.f * dt);
			c.x = S3rLerp(c.x, cx, snap);
			c.y = S3rLerp(c.y, cy, snap);
			c.z = S3rLerp(c.z, cz, snap);
			c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(atan2f(tx, tz) - c.yaw) * snap);
			c.pitch = S3rLerp(c.pitch, S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -0.55f, 0.55f), snap);
			float along = c.vx*tx + c.vy*ty + c.vz*tz;
			if (along < 0.f) along = 0.f;
			float keep = min(along, 22.f / 3.6f);
			c.vx = tx * keep; c.vy = ty * keep; c.vz = tz * keep;
			c.fuel = min(100.f, c.fuel + 35.f * dt);
			c.chkX = cx; c.chkY = cy; c.chkZ = cz;
		}
		if (c.courseOutCool <= 0.f)
			c.fuel = max(0.f, c.fuel - (spd>40.f?3.5f:1.2f)*dt);

		// ラップ判定：前進でスタートを跨いだときのみ＋最低周回時間（誤ラップ防止）
		{
			float dT = c.pathT - prevT;
			while (dT > 0.5f) dT -= 1.f;
			while (dT < -0.5f) dT += 1.f;
			float minLap = (m_pathLen > 1.f) ? (m_pathLen / 220.f) : 12.f;
			if (minLap < 10.f) minLap = 10.f;
			BOOL crossed = (prevT > 0.82f && c.pathT < 0.18f && dT > 0.f && dT < 0.35f);
			// スタート付近に実在することも要求（pathTだけ飛んでも無効）
			float sx,sy,sz; SplinePoint(0.f, sx,sy,sz);
			float dx=c.x-sx, dy=c.y-sy, dz=c.z-sz;
			float nearStart = dx*dx+dy*dy+dz*dz;
			float startR = BandHalfWidth() * 3.5f;
			if (crossed && c.raceTime >= minLap && nearStart < startR*startR) {
				if (c.lapTimesN < 12) {
					c.lapTimes[c.lapTimesN] = c.raceTime;
					c.lapTimesN++;
				}
				c.lap++;
				if (c.raceTime > 0.5f && c.raceTime < c.bestLap) c.bestLap = c.raceTime;
				c.raceTime = 0.f;
				m_standDirty = 1;
				if (c.lap >= m_lapsTarget) {
					if (demo) {
						c.lap = 0; // デモは周回し続けてステージを見せる
					} else {
						c.finished = 1; c.finishTime = m_raceClock;
						if (c.isPlayer) {
							m_phase = PHASE_FINISH; m_podiumT = 0.f;
							m_clearBakeText = LL14(L"FINISH!", L"FINISH!", L"ARRIVÉE!", L"ARRIVO!", L"¡META!", L"피니시!", L"完赛!", L"نهاية!", L"ФИНИШ!", L"ZIEL!", L"CHEGADA!", L"FINISH!", L"META!", L"BİTİŞ!");
							m_clearBakeA=1.f; m_clearDirty=1; m_overlayHold = 99.f;
						}
					}
				} else if (c.isPlayer && m_phase == PHASE_RACE) {
					wchar_t lapBuf[32];
					swprintf_s(lapBuf, L"LAP %d/%d", min(m_lapsTarget, c.lap + 1), m_lapsTarget);
					m_clearBakeText = lapBuf;
					m_clearBakeA = 1.f; m_clearDirty = 1; m_overlayHold = 1.8f;
				}
			}
		}
		c.raceTime += dt;

		// 自機の逆走判定（コース接線と速度の内積）
		if (c.isPlayer && m_phase == PHASE_RACE && !c.finished) {
			float tx,ty,tz,nx,ny,nz,bx,by,bz,px,py,pz;
			SplineFrame(c.pathT + 0.01f, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
			float along = c.vx*tx + c.vy*ty + c.vz*tz;
			const int ww = (spd > 12.f && along < -0.2f * spd) ? 1 : 0;
			m_wrongWay = ww;
			if (ww && m_overlayHold <= 0.f) {
				m_clearBakeText = LL14(L"逆走中", L"WRONG WAY", L"SENS INVERSE", L"CONTROMANO", L"SENTIDO CONTRARIO",
					L"역주행", L"逆行中", L"اتجاه خاطئ", L"НЕ ТУДА", L"FALSCHE RICHTUNG", L"SENTIDO ERRADO", L"VERKEERDE KANT", L"ZŁY KIERUNEK", L"TERS YÖN");
				m_clearBakeA = 1.f; m_clearDirty = 1;
			} else if (!ww && m_overlayHold <= 0.f && m_clearBakeText.Find(L"逆走") >= 0) {
				m_clearBakeText = L""; m_clearBakeA = 0.f; m_clearDirty = 1;
			} else if (!ww && m_overlayHold <= 0.f && m_clearBakeText.Find(L"WRONG") >= 0) {
				m_clearBakeText = L""; m_clearBakeA = 0.f; m_clearDirty = 1;
			}
		}

		// obstacles — 見た目より小さい判定球＋景色はダメージなし
		for (int o=0;o<m_obsN;o++){
			if (!m_obs[o].hazard || m_obs[o].damage <= 0.f) continue;
			float dx=c.x-m_obs[o].x, dy=c.y-(m_obs[o].y+m_obs[o].sy*0.45f), dz=c.z-m_obs[o].z;
			float rr=0.42f*max(m_obs[o].sx,max(m_obs[o].sy*0.35f,m_obs[o].sz));
			if (rr < 0.55f) rr = 0.55f;
			if (dx*dx+dy*dy+dz*dz < rr*rr) {
				c.hp -= m_obs[o].damage * dt * 1.1f;
				c.vx-=dx*5.f*dt; c.vy-=dy*5.f*dt; c.vz-=dz*5.f*dt;
			}
		}
		// craft-craft bounce
		for (int j=i+1;j<m_craftN;j++){
			S3rCraft& o=m_crafts[j];
			if (!o.alive || o.finished) continue;
			float dx=c.x-o.x, dy=c.y-o.y, dz=c.z-o.z;
			float d2=dx*dx+dy*dy+dz*dz;
			const float rr=2.2f;
			if (d2 < rr*rr && d2 > 1e-4f) {
				float d=sqrtf(d2); float nx=dx/d, ny=dy/d, nz=dz/d;
				float push=(rr-d)*0.5f;
				c.x+=nx*push; c.y+=ny*push; c.z+=nz*push;
				o.x-=nx*push; o.y-=ny*push; o.z-=nz*push;
				c.vx+=nx*4.f*dt; c.vy+=ny*4.f*dt; c.vz+=nz*4.f*dt;
				o.vx-=nx*4.f*dt; o.vy-=ny*4.f*dt; o.vz-=nz*4.f*dt;
				c.hp-=2.f*dt; o.hp-=2.f*dt;
			}
		}
		if (c.hp <= 0.f) {
			c.hp=0; c.alive=0; c.smokeT=8.f;
			if (c.isPlayer) {
				m_phase = PHASE_FINISH; m_podiumT = 0.f;
				m_clearBakeText = LL14(L"GAME OVER", L"GAME OVER", L"GAME OVER", L"GAME OVER", L"GAME OVER", L"게임 오버", L"游戏结束", L"انتهت", L"КОНЕЦ", L"GAME OVER", L"FIM DE JOGO", L"GAME OVER", L"KONIEC", L"OYUN BİTTİ");
				m_clearBakeA=1.f; m_clearDirty=1;
			}
		}
	}

	if (m_phase == PHASE_RACE) m_raceClock += dt;
	else if (demo) m_raceClock += dt;
	UpdateRanks();
}

void CSoft3DRaceDlg::TickDemo(float dt)
{
	m_demoCamT = S3rNormAngle(m_demoCamT + dt * 0.18f);
	TickPhysics(dt);
	TickItems(dt);
	// デモ表示は薄く維持（スタート誘導）
	if (m_clearBakeA < 0.70f) {
		m_clearBakeText = LL14(L"スタートで開始", L"Press Start", L"Démarrer", L"Avvia", L"Iniciar",
			L"시작", L"按开始", L"Start", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat");
		m_clearBakeA = 0.95f; m_clearDirty = 1;
	} else {
		m_clearBakeA = max(0.70f, m_clearBakeA - dt * 0.08f);
		m_clearDirty = 1;
	}
}

void CSoft3DRaceDlg::UpdateStatus()
{
	if (!m_status.GetSafeHwnd()) return;
	CString s;
	S3rCraft& pl=m_crafts[0];
	float kmh = SpeedToKmh(pl.vx, pl.vy, pl.vz);
	if (m_phase == PHASE_DEMO) {
		s.Format(_T("DEMO  Lap %d  Rank %d/%d  %.0f km/h  — Start to race"),
			pl.lap + 1, pl.rank, m_craftN, kmh);
	} else {
		s.Format(_T("Lap %d/%d  Rank %d/%d  HP %.0f  Thrust %.0f  %.0f km/h  Time %.1fs"),
			min(m_lapsTarget, pl.lap+1), m_lapsTarget, pl.rank, m_craftN, pl.hp, pl.fuel, kmh, m_raceClock);
	}
	m_status.SetWindowText(s);
	m_hudDirty = 1;
}

void CSoft3DRaceDlg::EnsureHudBake()
{
	if (!m_view.m_ready) return;
	if (!m_hudDirty && m_view.m_srvHud) return;
	S3rCraft& pl=m_crafts[0];
	float kmh = SpeedToKmh(pl.vx, pl.vy, pl.vz);
	wchar_t buf[512];
	if (pl.bestLap<=1e8f) {
		swprintf_s(buf, L"Lap %d/%d   #%d/%d\nHP %.0f   Thrust %.0f   %.0f km/h\nTime %.2f   BestLap %.2f",
			min(m_lapsTarget, pl.lap+1), m_lapsTarget, pl.rank, m_craftN, pl.hp, pl.fuel, kmh, m_raceClock, pl.bestLap);
	} else {
		swprintf_s(buf, L"Lap %d/%d   #%d/%d\nHP %.0f   Thrust %.0f   %.0f km/h\nTime %.2f   BestLap --",
			min(m_lapsTarget, pl.lap+1), m_lapsTarget, pl.rank, m_craftN, pl.hp, pl.fuel, kmh, m_raceClock);
	}
	m_hudBakeText = buf;
	m_view.BakeHudTexture(m_hudBakeText);
	m_hudDirty = 0;
}

void CSoft3DRaceDlg::EnsureStandingsBake()
{
	if (!m_view.m_ready) return;
	if (!m_standDirty && m_view.m_srvStand) return;
	CS3rView::S3rStandRow rows[S3R_MAX_CRAFT];
	int order[S3R_MAX_CRAFT];
	for (int i = 0; i < m_craftN; i++) order[i] = i;
	for (int i = 0; i < m_craftN; i++) for (int j = i + 1; j < m_craftN; j++)
		if (m_crafts[order[i]].rank > m_crafts[order[j]].rank) { int t = order[i]; order[i] = order[j]; order[j] = t; }
	for (int i = 0; i < m_craftN; i++) {
		S3rCraft& c = m_crafts[order[i]];
		CS3rView::S3rStandRow& r = rows[i];
		memset(&r, 0, sizeof(r));
		wcsncpy_s(r.name, c.name, _TRUNCATE);
		r.rank = c.rank > 0 ? c.rank : (i + 1);
		r.isPlayer = c.isPlayer;
		r.cr = kCraftColors[c.colorIdx][0];
		r.cg = kCraftColors[c.colorIdx][1];
		r.cb = kCraftColors[c.colorIdx][2];
		// 常に直近3枠（未計測は ----）。4周目以降は枠が LAP2/3/4… とずれる
		int nDone = c.lapTimesN;
		int start = 0;
		if (nDone > 3) start = nDone - 3;
		r.lapShowN = 3;
		for (int k = 0; k < 3; k++) {
			r.lapNo[k] = start + k + 1;
			int src = start + k;
			r.lapSec[k] = (src < nDone) ? c.lapTimes[src] : -1.f;
		}
	}
	m_view.BakeStandingsTexture(rows, m_craftN);
	m_standDirty = 0;
}

void CSoft3DRaceDlg::RenderScene()
{
	if (!m_view.m_ready || m_knotN < 4) return;
	ID3D11DeviceContext* dc = m_view.m_imm;
	const int w = m_view.m_vw, h = m_view.m_vh; if (w < 8 || h < 8) return;
	if (m_clearDirty) {
		if (!m_clearBakeText.IsEmpty()) m_view.BakeClearTexture(m_clearBakeText, m_clearBakeA);
		else m_view.ReleaseClearTexture();
		m_clearDirty = 0;
	}
	EnsureHudBake();
	EnsureStandingsBake();

	S3rCraft& pl = m_crafts[0];
	float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(pl.pathT, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	float noseX=sinf(pl.yaw)*cosf(pl.pitch), noseY=sinf(pl.pitch), noseZ=cosf(pl.yaw)*cosf(pl.pitch);
	float back = m_lookback ? -1.f : 1.f;
	float camDist = 12.5f * m_camZoom;
	float camH = (5.8f + m_camPitchOff * 10.f) * m_camZoom;
	float yaw = pl.yaw + m_camYawOff + (m_lookback ? (float)M_PI : 0.f);
	float cx = pl.x - sinf(yaw)*camDist*back;
	float cy = pl.y + camH;
	float cz = pl.z - cosf(yaw)*camDist*back;
	float ax = pl.x + noseX * 9.0f * back;
	float ay = pl.y + noseY * 0.55f * back + 1.4f;
	float az = pl.z + noseZ * 9.0f * back;
	if (m_phase == PHASE_PODIUM) {
		// 表彰台正面からの遅延追従カメラ
		cx = m_podiumBaseX; cy = m_podiumBaseY + 7.5f; cz = m_podiumBaseZ + 16.f;
		ax = m_podiumBaseX; ay = m_podiumBaseY + 3.2f; az = m_podiumBaseZ;
		m_camSmoothInit = 0; // 次レースでリセット
	} else if (m_phase == PHASE_DEMO) {
		// コース全体を見渡すオービット＋パック方面へ少し視線
		float packX = 0.f, packY = 0.f, packZ = 0.f; int pn = 0;
		for (int i = 0; i < m_craftN; i++) if (m_crafts[i].alive) {
			packX += m_crafts[i].x; packY += m_crafts[i].y; packZ += m_crafts[i].z; pn++;
		}
		if (pn > 0) { packX /= (float)pn; packY /= (float)pn; packZ /= (float)pn; }
		else { packX = m_demoMidX; packY = m_demoMidY; packZ = m_demoMidZ; }
		float rad = m_demoRad * 1.12f + 36.f;
		float elev = m_demoMidY + m_demoRad * 0.42f + 22.f + m_demoCamElev;
		cx = m_demoMidX + cosf(m_demoCamT) * rad;
		cy = elev;
		cz = m_demoMidZ + sinf(m_demoCamT) * rad;
		ax = S3rLerp(m_demoMidX, packX, 0.55f);
		ay = S3rLerp(m_demoMidY + 3.5f, packY + 1.2f, 0.55f);
		az = S3rLerp(m_demoMidZ, packZ, 0.55f);
	}
	if (!m_camSmoothInit) {
		m_camSx = cx; m_camSy = cy; m_camSz = cz;
		m_camAx = ax; m_camAy = ay; m_camAz = az;
		m_camSmoothInit = 1;
	} else {
		// 左右も遅れて追う（急な視点振りで酔わない）
		m_camSx = S3rLerp(m_camSx, cx, 0.11f);
		m_camSy = S3rLerp(m_camSy, cy, 0.07f);
		m_camSz = S3rLerp(m_camSz, cz, 0.11f);
		m_camAx = S3rLerp(m_camAx, ax, 0.10f);
		m_camAy = S3rLerp(m_camAy, ay, 0.06f);
		m_camAz = S3rLerp(m_camAz, az, 0.10f);
	}
	cx = m_camSx; cy = m_camSy; cz = m_camSz;
	ax = m_camAx; ay = m_camAy; az = m_camAz;

	const float fov = ((m_phase == PHASE_DEMO) ? 68.f : 58.f) / m_camZoom * (float)(M_PI/180.0);
	const float zNear = 0.2f, zFar = 700.f;
	S3RFrameCB cb = {};
	cb.viewProj = S3rMatMul(S3rLookAt(cx,cy,cz, ax,ay,az, 0,1,0), S3rPerspective(fov, (float)w/(float)h, zNear, zFar));
	float lx=.45f, ly=1.f, lz=.2f; S3rNorm3(lx,ly,lz);
	cb.lightDir = {lx,ly,lz, 0.f};
	cb.lightVP = S3rMatMul(S3rLookAt(pl.x+lx*55.f, pl.y+ly*55.f, pl.z+lz*55.f, pl.x,pl.y,pl.z, 0,1,0), S3rOrtho(-48.f,48.f,-48.f,48.f, 4.f, 160.f));
	cb.eyePos = {cx,cy,cz, (float)(m_themeActive-1)};
	float fogNear=70.f, fogFar=320.f;
	if (m_themeActive==THEME_UNDER||m_themeActive==THEME_NIGHT){ fogNear=40.f; fogFar=200.f; }
	fogNear *= (1.f - 0.4f * m_reverbFogBoost - 0.25f * (pl.fogT>0?1.f:0.f));
	fogFar *= (1.f - 0.35f * m_reverbFogBoost);
	cb.fogParams = {fogNear, fogFar, 0.02f, 0.f};
	float dofStart=38.f, dofRise=55.f, dofAmp=1.6f + 2.5f*m_eqDofBoost + (pl.dofT>0?2.2f:0.f);
	cb.dofParams = {dofStart, dofRise, dofAmp, 0.f};
	cb.screenSize = {(float)w,(float)h,1.f/w,1.f/h};
	cb.misc = {pl.flashT>0?0.55f:0.f, m_clearBakeA, 1.f/tanf(fov*.5f), m_anim};

	D3D11_MAPPED_SUBRESOURCE map={};
	if (FAILED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return;
	memcpy(map.pData,&cb,sizeof(cb)); dc->Unmap(m_view.m_cbFrame,0);

	UINT maxV = m_view.m_vbDynBytes / sizeof(S3RVertex);
	if (maxV < 65536u) maxV = 65536u;
	{
		const UINT need = maxV * sizeof(S3RVertex);
		if (m_view.m_cpuDynScratchBytes < need) {
			delete[] m_view.m_cpuDynScratch;
			m_view.m_cpuDynScratch = new (std::nothrow) BYTE[need];
			m_view.m_cpuDynScratchBytes = m_view.m_cpuDynScratch ? need : 0;
		}
	}
	S3RVertex* v = m_view.m_cpuDynScratch ? (S3RVertex*)m_view.m_cpuDynScratch : NULL;
	if (!v) return;
	UINT nBand=0, nWorld=0, nCraft=0, nTrans=0; int phase=0;
	auto put=[&](float x,float y,float z,float nx,float ny,float nz,float u,float vv,float r,float g,float b,float a)->BOOL{
		UINT n=nBand+nWorld+nCraft+nTrans; if(n>=maxV) return FALSE;
		v[n]={x,y,z,nx,ny,nz,u,vv,r,g,b,a};
		if(phase==3)nTrans++; else if(phase==2)nCraft++; else if(phase==1)nWorld++; else nBand++;
		return TRUE;
	};
	auto patch=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
		put(x0,y0,z0,nx,ny,nz,u0,v1,r,g,b,a); put(x1,y1,z1,nx,ny,nz,u1,v1,r,g,b,a);
		put(x2,y2,z2,nx,ny,nz,u1,v0,r,g,b,a); put(x3,y3,z3,nx,ny,nz,u0,v0,r,g,b,a);
	};

	// course ribbon patches along spline
	phase=0;
	const int segs = 220;
	float half = BandHalfWidth();
	for (int i=0;i<segs;i++){
		float t0=(float)i/segs, t1=(float)(i+1)/segs;
		float p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z;
		float p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z;
		SplineFrame(t0,p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z);
		SplineFrame(t1,p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z);
		float u0=t0*6.f, u1=t1*6.f;
		float colR=.65f,colG=.9f,colB=1.f,colA=.42f;
		if (m_themeActive==THEME_FOREST){colR=.55f;colG=1.f;colB=.7f;}
		else if (m_themeActive==THEME_MESA){colR=1.f;colG=.75f;colB=.45f;}
		else if (m_themeActive==THEME_NIGHT){colR=.55f;colG=.7f;colB=1.f;}
		patch(p0x-b0x*half,p0y-b0y*half,p0z-b0z*half, p0x+b0x*half,p0y+b0y*half,p0z+b0z*half,
			  p1x+b1x*half,p1y+b1y*half,p1z+b1z*half, p1x-b1x*half,p1y-b1y*half,p1z-b1z*half,
			  n0x,n0y,n0z, u0,0,u1,1, colR,colG,colB,colA);
		float rh=half*1.04f;
		patch(p0x-b0x*rh+n0x*.35f,p0y-b0y*rh+n0y*.35f,p0z-b0z*rh+n0z*.35f,
			  p0x-b0x*half+n0x*.08f,p0y-b0y*half+n0y*.08f,p0z-b0z*half+n0z*.08f,
			  p1x-b1x*half+n1x*.08f,p1y-b1y*half+n1y*.08f,p1z-b1z*half+n1z*.08f,
			  p1x-b1x*rh+n1x*.35f,p1y-b1y*rh+n1y*.35f,p1z-b1z*rh+n1z*.35f,
			  -b0x,-b0y,-b0z, u0,0,u1,1, colR,colG,colB,.28f);
	}

	// world: terrain + obstacles（空カードは半透明パスへ — 不透明だと横から棒状破綻）
	phase=1;
	{
		float gr=.35f,gg=.55f,gb=.32f;
		if (m_themeActive==THEME_FOREST){gr=.28f;gg=.62f;gb=.28f;}
		else if (m_themeActive==THEME_RUINS){gr=.55f;gg=.5f;gb=.42f;}
		else if (m_themeActive==THEME_OIL){gr=.28f;gg=.3f;gb=.32f;}
		else if (m_themeActive==THEME_NIGHT){gr=.12f;gg=.14f;gb=.22f;}
		else if (m_themeActive==THEME_UNDER){gr=.15f;gg=.35f;gb=.5f;}
		else if (m_themeActive==THEME_GRASS){gr=.4f;gg=.7f;gb=.3f;}
		else if (m_themeActive==THEME_MESA){gr=.85f;gg=.45f;gb=.28f;}
		else {gr=.75f;gg=.8f;gb=.95f;}
		const int gN = 40;
		const float extent = 280.f;
		float step = extent * 2.f / (float)gN;
		float x0 = pl.x - extent, z0 = pl.z - extent;
		for (int gz=0; gz<gN; gz++) for (int gx=0; gx<gN; gx++) {
			float xa = x0 + gx * step, za = z0 + gz * step;
			float xb = xa + step, zb = za + step;
			float y00=GroundY(xa,za), y10=GroundY(xb,za), y11=GroundY(xb,zb), y01=GroundY(xa,zb);
			float nx=(y00-y10)+(y01-y11), nz=(y00-y01)+(y10-y11), ny=step*2.f; S3rNorm3(nx,ny,nz);
			float u0=(float)gx/(float)gN, v0=(float)gz/(float)gN, u1=(float)(gx+1)/(float)gN, v1=(float)(gz+1)/(float)gN;
			put(xa,y00,za,nx,ny,nz,u0,v0,gr,gg,gb,1.f);
			put(xb,y10,za,nx,ny,nz,u1,v0,gr,gg,gb,1.f);
			put(xb,y11,zb,nx,ny,nz,u1,v1,gr,gg,gb,1.f);
			put(xa,y00,za,nx,ny,nz,u0,v0,gr,gg,gb,1.f);
			put(xb,y11,zb,nx,ny,nz,u1,v1,gr,gg,gb,1.f);
			put(xa,y01,zb,nx,ny,nz,u0,v1,gr,gg,gb,1.f);
		}
	}
	auto xformEmitMesh=[&](const float* mv, const UINT* mi, int nv, int ni, float ox,float oy,float oz, float yaw,float pitch, float sx,float sy,float sz, float r,float g,float b,float a){
		float cy=cosf(yaw), syaw=sinf(yaw), cp=cosf(pitch), sp=sinf(pitch);
		auto xf=[&](float x,float y,float z, float& X,float& Y,float& Z){
			x*=sx;y*=sy;z*=sz;
			float y2=y*cp-z*sp, z2=y*sp+z*cp;
			X=ox + x*cy + z2*syaw; Y=oy + y2; Z=oz + -x*syaw + z2*cy;
		};
		auto xn=[&](float nx,float ny,float nz, float& NX,float& NY,float& NZ){
			float y2=ny*cp-nz*sp, z2=ny*sp+nz*cp;
			NX=nx*cy+z2*syaw; NY=y2; NZ=-nx*syaw+z2*cy; S3rNorm3(NX,NY,NZ);
		};
		UINT base = nBand+nWorld+nCraft+nTrans;
		for (int i=0;i<nv;i++){
			const float* p=mv+i*12; float X,Y,Z,NX,NY,NZ; xf(p[0],p[1],p[2],X,Y,Z); xn(p[3],p[4],p[5],NX,NY,NZ);
			if(!put(X,Y,Z,NX,NY,NZ,p[6],p[7], r*p[8],g*p[9],b*p[10], a*p[11])) return;
		}
		(void)base;(void)mi;(void)ni;
	};
	// Expand craft as indexed into triangles
	auto emitIndexed=[&](const float* mv,const UINT* mi,int nv,int ni,float ox,float oy,float oz,float yaw,float pitch,float sx,float sy,float sz,float r,float g,float b,float a)->BOOL{
		float cy=cosf(yaw), syaw=sinf(yaw), cp=cosf(pitch), sp=sinf(pitch);
		auto xf=[&](float x,float y,float z, float& X,float& Y,float& Z){
			x*=sx;y*=sy;z*=sz; float y2=y*cp-z*sp, z2=y*sp+z*cp;
			X=ox+x*cy+z2*syaw; Y=oy+y2; Z=oz+-x*syaw+z2*cy;
		};
		auto xn=[&](float nx,float ny,float nz, float& NX,float& NY,float& NZ){
			float y2=ny*cp-nz*sp, z2=ny*sp+nz*cp; NX=nx*cy+z2*syaw; NY=y2; NZ=-nx*syaw+z2*cy; S3rNorm3(NX,NY,NZ);
		};
		for (int t=0;t+2<ni;t+=3){
			for (int k=0;k<3;k++){
				UINT id=mi[t+k]; if((int)id>=nv) return FALSE;
				const float* p=mv+id*12; float X,Y,Z,NX,NY,NZ; xf(p[0],p[1],p[2],X,Y,Z); xn(p[3],p[4],p[5],NX,NY,NZ);
				if(!put(X,Y,Z,NX,NY,NZ,p[6],p[7],r*p[8],g*p[9],b*p[10],a*p[11])) return FALSE;
			}
		}
		return TRUE;
	};
	// 景色オブジェクト（VB残量から描画数を決める — 途中切断＝破綻を防ぐ）
	{
		int order[S3R_MAX_OBS]; float dist2[S3R_MAX_OBS]; int nCand=0;
		for (int i=0;i<m_obsN && nCand<S3R_MAX_OBS;i++){
			float odx=m_obs[i].x-cx,ody=m_obs[i].y-cy,odz=m_obs[i].z-cz;
			float d2=odx*odx+ody*ody+odz*odz;
			if (d2 > 420.f*420.f) continue;
			order[nCand]=i; dist2[nCand]=d2; nCand++;
		}
		const int craftReserve = (m_craftN > 0 ? m_craftN : 6) * (m_craftNi > 0 ? m_craftNi : 4000) + 8000;
		UINT usedNow = nBand + nWorld + nCraft + nTrans;
		UINT room = (maxV > usedNow + (UINT)craftReserve) ? (maxV - usedNow - (UINT)craftReserve) : 0;
		int perObs = m_obsNi > 0 ? m_obsNi : 1;
		int drawMax = (int)(room / (UINT)perObs);
		if (drawMax > 320) drawMax = 320;
		if (drawMax < 48) drawMax = 48;
		for (int a=0;a<nCand && a<drawMax;a++){
			int best=a;
			for (int b=a+1;b<nCand;b++) if (dist2[b] < dist2[best]) best=b;
			if (best!=a){ int ti=order[a]; order[a]=order[best]; order[best]=ti; float td=dist2[a]; dist2[a]=dist2[best]; dist2[best]=td; }
		}
		int drawN = nCand; if (drawN > drawMax) drawN = drawMax;
		for (int k=0;k<drawN;k++){
			S3rObs& o=m_obs[order[k]];
			if(!emitIndexed(m_obsVert, m_obsIdx, m_obsNv, m_obsNi, o.x,o.y,o.z, o.yaw,0.f, o.sx,o.sy,o.sz, 1,1,1,1)) break;
		}
	}
	// crafts — キャラ専用テクスチャパス
	phase=2;
	// 表彰台（1位中央・2位左・3位右）
	if (m_phase == PHASE_PODIUM) {
		auto box = [&](float x, float y0, float z, float hx, float hy, float hz, float r, float g, float b) {
			float y1 = y0 + hy;
			// top
			patch(x-hx,y1,z-hz, x+hx,y1,z-hz, x+hx,y1,z+hz, x-hx,y1,z+hz, 0,1,0, 0,0,1,1, r,g,b,1);
			// sides
			patch(x-hx,y0,z+hz, x+hx,y0,z+hz, x+hx,y1,z+hz, x-hx,y1,z+hz, 0,0,1, 0,0,1,1, r*.85f,g*.85f,b*.85f,1);
			patch(x+hx,y0,z-hz, x-hx,y0,z-hz, x-hx,y1,z-hz, x+hx,y1,z-hz, 0,0,-1, 0,0,1,1, r*.75f,g*.75f,b*.75f,1);
			patch(x-hx,y0,z-hz, x-hx,y0,z+hz, x-hx,y1,z+hz, x-hx,y1,z-hz, -1,0,0, 0,0,1,1, r*.7f,g*.7f,b*.7f,1);
			patch(x+hx,y0,z+hz, x+hx,y0,z-hz, x+hx,y1,z-hz, x+hx,y1,z+hz, 1,0,0, 0,0,1,1, r*.9f,g*.9f,b*.9f,1);
		};
		phase = 1;
		float y0 = m_podiumBaseY;
		box(m_podiumBaseX,      y0, m_podiumBaseZ, 1.6f, 4.2f, 1.6f, 1.f, .85f, .25f);   // 1st gold
		box(m_podiumBaseX-5.2f, y0, m_podiumBaseZ, 1.5f, 2.8f, 1.5f, .72f, .76f, .82f); // 2nd silver
		box(m_podiumBaseX+5.2f, y0, m_podiumBaseZ, 1.5f, 1.8f, 1.5f, .78f, .48f, .28f); // 3rd bronze
		phase = 2;
	}
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		float cr=kCraftColors[c.colorIdx][0], cg=kCraftColors[c.colorIdx][1], cb=kCraftColors[c.colorIdx][2];
		float a = c.alive ? 1.f : 0.35f;
		if(!emitIndexed(m_craftVert, m_craftIdx, m_craftNv, m_craftNi, c.x,c.y,c.z, c.yaw,c.pitch, 1.1f,1.1f,1.1f, cr,cg,cb,a)) break;
	}
	// sky cards + items（深度書き込みなし）
	phase=3;
	{
		float rx = ax - cx, ry = ay - cy, rz = az - cz; S3rNorm3(rx,ry,rz);
		float ux=0,uy=1,uz=0;
		float sx=uy*rz-uz*ry, sy=uz*rx-ux*rz, sz=ux*ry-uy*rx; S3rNorm3(sx,sy,sz);
		float ux2=ry*sz-rz*sy, uy2=rz*sx-rx*sz, uz2=rx*sy-ry*sx;
		float br=.78f,bg=.86f,bb=1.f;
		if (m_themeActive==THEME_NIGHT){br=.2f;bg=.24f;bb=.42f;}
		else if (m_themeActive==THEME_MESA){br=1.f;bg=.7f;bb=.45f;}
		else if (m_themeActive==THEME_FOREST){br=.58f;bg=.86f;bb=.68f;}
		else if (m_themeActive==THEME_CLOUD){br=.96f;bg=.96f;bb=1.f;}
		else if (m_themeActive==THEME_UNDER){br=.28f;bg=.58f;bb=.78f;}
		else if (m_themeActive==THEME_OIL){br=.48f;bg=.5f;bb=.54f;}
		for (int bi=0;bi<12;bi++){
			float ang = (float)bi * (float)(M_PI*2.0/12.0) + m_anim*0.025f;
			float dist = 190.f + 40.f*(bi%3);
			float bx0 = pl.x + cosf(ang)*dist;
			float by0 = pl.y + 42.f + 10.f*sinf(ang*1.5f+m_anim*0.07f);
			float bz0 = pl.z + sinf(ang)*dist;
			float hs=42.f, vs=24.f;
			float x0=bx0-sx*hs-ux2*vs, y0=by0-sy*hs-uy2*vs, z0=bz0-sz*hs-uz2*vs;
			float x1=bx0+sx*hs-ux2*vs, y1=by0+sy*hs-uy2*vs, z1=bz0+sz*hs-uz2*vs;
			float x2=bx0+sx*hs+ux2*vs, y2=by0+sy*hs+uy2*vs, z2=bz0+sz*hs+uz2*vs;
			float x3=bx0-sx*hs+ux2*vs, y3=by0-sy*hs+uy2*vs, z3=bz0-sz*hs+uz2*vs;
			patch(x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3, -rx,-ry,-rz, 0,0,1,1, br,bg,bb,.28f);
		}
	}
	for (int i=0;i<m_itemN;i++){
		S3rItem& it=m_items[i]; if (it.taken) continue;
		float r=1,g=1,b=1;
		switch(it.kind){
		case KIND_TEMPO: r=.3f;g=1;b=.45f; break; case KIND_TEMPO_DN: r=.1f;g=.55f;b=.25f; break;
		case KIND_PITCH_UP: r=1;g=.65f;b=.25f; break; case KIND_PITCH_DN: r=.35f;g=.55f;b=1; break;
		case KIND_NEXT: r=1;g=.2f;b=.35f; break; case KIND_PREV: r=.55f;g=.1f;b=.2f; break;
		case KIND_VOL_UP: r=1;g=.95f;b=.35f; break; case KIND_VOL_DN: r=.55f;g=.6f;b=.2f; break;
		case KIND_EQ: r=.75f;g=.35f;b=1; break; case KIND_EQ_FLAT: r=.6f;g=.55f;b=.8f; break;
		case KIND_REVERB: r=.2f;g=.85f;b=.95f; break; case KIND_XFADE: r=1;g=.45f;b=.75f; break;
		default: r=1;g=.55f;b=.2f; break;
		}
		float s=.55f; float cy=cosf(it.spin), sy=sinf(it.spin);
		float p[6][3]={{0,s,0},{0,-s,0},{s*cy,0,s*sy},{-s*cy,0,-s*sy},{s*sy,0,-s*cy},{-s*sy,0,s*cy}};
		int faces[8][3]={{0,2,4},{0,4,3},{0,3,5},{0,5,2},{1,4,2},{1,3,4},{1,5,3},{1,2,5}};
		for (int f=0;f<8;f++){
			float ax=it.x+p[faces[f][0]][0], ay=it.y+p[faces[f][0]][1], az=it.z+p[faces[f][0]][2];
			float bx=it.x+p[faces[f][1]][0], by=it.y+p[faces[f][1]][1], bz=it.z+p[faces[f][1]][2];
			float cx2=it.x+p[faces[f][2]][0], cy2=it.y+p[faces[f][2]][1], cz2=it.z+p[faces[f][2]][2];
			float nx=(by-ay)*(cz2-az)-(bz-az)*(cy2-ay), ny=(bz-az)*(cx2-ax)-(bx-ax)*(cz2-az), nz=(bx-ax)*(cy2-ay)-(by-ay)*(cx2-ax); S3rNorm3(nx,ny,nz);
			put(ax,ay,az,nx,ny,nz,0,0,r,g,b,.9f); put(bx,by,bz,nx,ny,nz,1,0,r,g,b,.9f); put(cx2,cy2,cz2,nx,ny,nz,.5f,1,r,g,b,.9f);
		}
	}
	// podium confetti
	if (m_phase==PHASE_PODIUM){
		for (int i=0;i<96;i++){
			if (m_confetti[i][5]<=0) continue;
			float x=m_confetti[i][0], y=m_confetti[i][1], z=m_confetti[i][2], s=.25f;
			float r=kCraftColors[i%12][0], g=kCraftColors[i%12][1], b=kCraftColors[i%12][2];
			put(x-s,y,z,0,1,0,0,0,r,g,b,1); put(x+s,y,z,0,1,0,1,0,r,g,b,1); put(x,y+s,z,0,1,0,.5f,1,r,g,b,1);
		}
	}

	const UINT total = nBand + nWorld + nCraft + nTrans;
	if (total > 0) {
		if (FAILED(dc->Map(m_view.m_vbDyn,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return;
		memcpy(map.pData, v, total * sizeof(S3RVertex));
		dc->Unmap(m_view.m_vbDyn,0);
	}

	float clearC[4]={0.45f,0.62f,0.92f,1.f};
	if (m_themeActive==THEME_NIGHT){clearC[0]=.08f;clearC[1]=.1f;clearC[2]=.2f;}
	else if (m_themeActive==THEME_UNDER){clearC[0]=.1f;clearC[1]=.25f;clearC[2]=.4f;}
	else if (m_themeActive==THEME_MESA){clearC[0]=.95f;clearC[1]=.55f;clearC[2]=.35f;}
	else if (m_themeActive==THEME_OIL){clearC[0]=.25f;clearC[1]=.28f;clearC[2]=.32f;}
	else if (m_themeActive==THEME_CLOUD){clearC[0]=.85f;clearC[1]=.9f;clearC[2]=1.f;}
	else if (m_themeActive==THEME_FOREST){clearC[0]=.42f;clearC[1]=.62f;clearC[2]=.48f;}
	else if (m_themeActive==THEME_GRASS){clearC[0]=.55f;clearC[1]=.72f;clearC[2]=.55f;}
	else if (m_themeActive==THEME_RUINS){clearC[0]=.55f;clearC[1]=.52f;clearC[2]=.48f;}

	const UINT nSolid = nWorld + nCraft;
	// shadow pass — VP を LightVP にして投影（カメラVPのままだと影が落ちない）
	{
		S3RFrameCB cbSh = cb;
		cbSh.viewProj = cb.lightVP;
		cbSh.lightDir.w = 0.f;
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cbSh,sizeof(cbSh)); dc->Unmap(m_view.m_cbFrame,0); }
		D3D11_VIEWPORT sp={0,0,(float)CS3rView::S3R_SHADOW_SIZE,(float)CS3rView::S3R_SHADOW_SIZE,0,1};
		dc->RSSetViewports(1,&sp); dc->OMSetRenderTargets(0,NULL,m_view.m_shadowDsv); dc->ClearDepthStencilView(m_view.m_shadowDsv,D3D11_CLEAR_DEPTH,1,0);
		dc->RSSetState(m_view.m_rsShadow); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->PSSetShader(NULL,NULL,0); dc->IASetInputLayout(m_view.m_ilSolid);
		dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		UINT stride=sizeof(S3RVertex), off=0; dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);
		if (nSolid) dc->Draw(nSolid, nBand);
	}

	// color pass
	D3D11_VIEWPORT vp={0,0,(float)w,(float)h,0,1}; dc->RSSetViewports(1,&vp);
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,m_view.m_dsv);
	dc->ClearRenderTargetView(m_view.m_sceneRtv, clearC);
	dc->ClearDepthStencilView(m_view.m_dsv, D3D11_CLEAR_DEPTH, 1, 0);
	cb.lightDir.w = 1.f;
	if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cb,sizeof(cb)); dc->Unmap(m_view.m_cbFrame,0); }

	ID3D11Buffer* cbs[]={m_view.m_cbFrame};
	ID3D11SamplerState* smp[]={m_view.m_sampLin,m_view.m_sampPoint,m_view.m_sampCmp};
	dc->VSSetConstantBuffers(0,1,cbs); dc->PSSetConstantBuffers(0,1,cbs); dc->HSSetConstantBuffers(0,1,cbs); dc->DSSetConstantBuffers(0,1,cbs);
	dc->PSSetSamplers(0,3,smp); dc->DSSetSamplers(0,1,&m_view.m_sampLin);
	ID3D11ShaderResourceView* bandSrv=m_view.m_srvBand;
	ID3D11ShaderResourceView* noiseSrv=m_view.m_srvNoise;
	ID3D11ShaderResourceView* envSrv=m_view.m_srvEnv;
	ID3D11ShaderResourceView* shSrv=m_view.m_shadowSrv;
	int thIdx=m_themeActive-1; if(thIdx<0)thIdx=0; if(thIdx>7)thIdx=7; ID3D11ShaderResourceView* themeSrv=m_view.m_srvTheme[thIdx];

	UINT stride=sizeof(S3RVertex), off=0; dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);
	// terrain / scenery first
	if (nWorld){
		dc->RSSetState(m_view.m_rsSolid); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0); dc->PSSetShader(m_view.m_psSolid,NULL,0);
		ID3D11ShaderResourceView* srvs[]={themeSrv,themeSrv,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		dc->Draw(nWorld, nBand);
	}
	// crafts — vertex color + lighting only（テーマテクスチャを当てない）
	if (nCraft && m_view.m_psCraft){
		dc->RSSetState(m_view.m_rsSolid); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0); dc->PSSetShader(m_view.m_psCraft,NULL,0);
		ID3D11ShaderResourceView* craftSrv = m_view.m_srvCraft ? m_view.m_srvCraft : themeSrv;
		ID3D11ShaderResourceView* srvs[]={craftSrv,craftSrv,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		dc->Draw(nCraft, nBand+nWorld);
	} else if (nCraft){
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
		ID3D11ShaderResourceView* srvs[]={themeSrv,themeSrv,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		dc->Draw(nCraft, nBand+nWorld);
	}
	// 半透明パワーバンド（深度書き込みなし → 自機が透けて見える）
	if (nBand>=4){
		dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssRead,0); dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff);
		dc->IASetInputLayout(m_view.m_ilPatch); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
		dc->VSSetShader(m_view.m_vsTess,NULL,0); dc->HSSetShader(m_view.m_hsTess,NULL,0); dc->DSSetShader(m_view.m_dsTess,NULL,0); dc->PSSetShader(m_view.m_psBand,NULL,0);
		ID3D11ShaderResourceView* srvs[]={bandSrv,themeSrv,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs); dc->DSSetShaderResources(5,1,&noiseSrv); dc->DSSetShaderResources(0,1,&bandSrv);
		dc->Draw(nBand - (nBand%4), 0);
		dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
	}
	if (nTrans){
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssRead,0);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->PSSetShader(m_view.m_psSolid,NULL,0);
		ID3D11ShaderResourceView* srvs[]={themeSrv,themeSrv,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		dc->Draw(nTrans, nBand+nWorld+nCraft);
	}

	// post SSR -> DOF -> FIN
	ID3D11ShaderResourceView* nulls[6]={}; dc->PSSetShaderResources(0,6,nulls);
	dc->OMSetRenderTargets(1,&m_view.m_postRtv,NULL);
	dc->OMSetDepthStencilState(m_view.m_dssOff,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
	dc->VSSetShader(m_view.m_vsPost,NULL,0); dc->PSSetShader(m_view.m_psSsr,NULL,0);
	ID3D11ShaderResourceView* p0[]={m_view.m_sceneSrv,NULL,m_view.m_dsSrv}; dc->PSSetShaderResources(0,3,p0);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); dc->Draw(3,0);

	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,NULL); dc->PSSetShaderResources(0,6,nulls);
	dc->PSSetShader(m_view.m_psDof,NULL,0);
	ID3D11ShaderResourceView* p1[]={m_view.m_postSrv,NULL,m_view.m_dsSrv}; dc->PSSetShaderResources(0,3,p1);
	dc->Draw(3,0);

	dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL); dc->PSSetShaderResources(0,6,nulls);
	dc->PSSetShader(m_view.m_psFinal,NULL,0);
	ID3D11ShaderResourceView* p2[]={m_view.m_sceneSrv}; dc->PSSetShaderResources(0,1,p2);
	dc->Draw(3,0);

	// HUD quads + baked countdown / status textures
	{
		UINT maxH = m_view.m_vbHudBytes / sizeof(S3RHudVertex);
		if (m_view.m_cpuHudScratchBytes < m_view.m_vbHudBytes) {
			delete[] m_view.m_cpuHudScratch;
			m_view.m_cpuHudScratch = new (std::nothrow) BYTE[m_view.m_vbHudBytes];
			m_view.m_cpuHudScratchBytes = m_view.m_cpuHudScratch ? m_view.m_vbHudBytes : 0;
		}
		S3RHudVertex* hv = m_view.m_cpuHudScratch ? (S3RHudVertex*)m_view.m_cpuHudScratch : NULL;
		UINT nh=0;
		auto hq=[&](float x0,float y0,float x1,float y1,float r,float g,float b,float a){
			if (!hv || nh+6>maxH) return;
			const float u=-1.f,v=-1.f;
			hv[nh++]={x0,y0,u,v,r,g,b,a}; hv[nh++]={x1,y0,u,v,r,g,b,a}; hv[nh++]={x1,y1,u,v,r,g,b,a};
			hv[nh++]={x0,y0,u,v,r,g,b,a}; hv[nh++]={x1,y1,u,v,r,g,b,a}; hv[nh++]={x0,y1,u,v,r,g,b,a};
		};
		auto ht=[&](float x0,float y0,float x1,float y1,float a){
			if (!hv || nh+6>maxH) return;
			// y0<y1: y1=画面上。GDI テクスチャ v=0 が上端なので UV を合わせる（反転表示の修正）
			hv[nh++]={x0,y1,0,0,1,1,1,a}; hv[nh++]={x1,y1,1,0,1,1,1,a}; hv[nh++]={x1,y0,1,1,1,1,1,a};
			hv[nh++]={x0,y1,0,0,1,1,1,a}; hv[nh++]={x1,y0,1,1,1,1,1,a}; hv[nh++]={x0,y0,0,1,1,1,1,a};
		};
		// minimap（右上）＋順位パネル（その下）
		if (savedata.s3r_show_map) {
			float mx0=0.62f, my0=0.62f, mx1=0.96f, my1=0.94f;
			hq(mx0,my0,mx1,my1, 0.05f,0.07f,0.12f,0.55f);
			const float s=1.f/220.f;
			for (int i=0;i<S3R_PATH_SAMPLES-1;i+=4){
				float ax=m_pathSampleXYZ[i][0], az=m_pathSampleXYZ[i][2];
				float x0=mx0+(mx1-mx0)*(ax*s*0.5f+0.5f), y0=my0+(my1-my0)*(az*s*0.5f+0.5f);
				hq(x0-.003f,y0-.003f,x0+.003f,y0+.003f, .5f,.85f,1.f,.9f);
			}
			for (int i=0;i<m_craftN;i++){
				float x=mx0+(mx1-mx0)*(m_crafts[i].x*s*0.5f+0.5f);
				float y=my0+(my1-my0)*(m_crafts[i].z*s*0.5f+0.5f);
				float r=kCraftColors[m_crafts[i].colorIdx][0], g=kCraftColors[m_crafts[i].colorIdx][1], b=kCraftColors[m_crafts[i].colorIdx][2];
				float rad=m_crafts[i].isPlayer?0.012f:0.008f;
				hq(x-rad,y-rad,x+rad,y+rad,r,g,b,1);
			}
		}
		// HP / 推進力バー（中央レーン中は HP バーを緑寄り）
		{
			float lat=0,vert=0,cx,cy,cz;
			BandLocal(pl.x,pl.y,pl.z,pl.pathT,lat,vert,cx,cy,cz);
			float half=BandHalfWidth();
			const int healing = (fabsf(lat)<=half*0.42f && fabsf(vert)<=half*0.38f && m_phase==PHASE_RACE) ? 1 : 0;
			if (healing)
				hq(-0.95f,0.88f,-0.95f+0.5f*(pl.hp/100.f),0.94f, .35f,1.f,.55f,.95f);
			else
				hq(-0.95f,0.88f,-0.95f+0.5f*(pl.hp/100.f),0.94f, 1.f,.35f,.45f,.85f);
		}
		// 推進力（帯外で減り、0でコースアウト復帰）
		{
			float fr = pl.fuel / 100.f;
			float r = (fr < 0.25f) ? 1.f : 0.35f;
			float g = (fr < 0.25f) ? 0.45f : 0.85f;
			float b = (fr < 0.25f) ? 0.25f : 1.f;
			hq(-0.95f,0.80f,-0.95f+0.5f*fr,0.86f, r,g,b,.9f);
		}

		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);
		// 直前の solid パスが CULL_BACK のままだと HUD 四角が消える
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssOff,0);
		dc->VSSetShader(m_view.m_vsHud,NULL,0); dc->PSSetShader(m_view.m_psHud,NULL,0); dc->IASetInputLayout(m_view.m_ilHud);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D11SamplerState* hsmp[]={m_view.m_sampLin,m_view.m_sampPoint,m_view.m_sampCmp};
		dc->PSSetSamplers(0,3,hsmp);

		auto flushHud=[&](ID3D11ShaderResourceView* srv){
			if (!nh || !hv) return;
			if (FAILED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return;
			memcpy(map.pData,hv,nh*sizeof(S3RHudVertex)); dc->Unmap(m_view.m_vbHud,0);
			ID3D11ShaderResourceView* srvs[]={srv?srv:m_view.m_srvBand,NULL,NULL,NULL,NULL,NULL};
			dc->PSSetShaderResources(0,6,srvs);
			UINT hs=sizeof(S3RHudVertex), ho=0; dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&ho);
			dc->Draw(nh,0); nh=0;
		};
		flushHud(NULL);

		if (m_view.m_srvHud) {
			ht(-0.96f, 0.42f, -0.28f, 0.78f, 0.92f);
			flushHud(m_view.m_srvHud);
		}
		// ミニマップ下にマージン → 残り縦を最大12台で等分。実台数ぶんだけ上から積む
		if (m_view.m_srvStand && (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH || m_phase == PHASE_PODIUM || m_phase == PHASE_DEMO)) {
			const float mapBot = 0.62f; // ミニマップ下端（描画と揃える）
			const float availTop = mapBot - 0.04f; // 少しマージン
			const float availBot = -0.96f;
			const float slotH = (availTop - availBot) / 12.f;
			const int n = max(1, min(m_craftN, 12));
			float panelH = slotH * (float)n;
			float sy1 = availTop;
			float sy0 = sy1 - panelH;
			ht(0.58f, sy0, 0.99f, sy1, 0.98f);
			flushHud(m_view.m_srvStand);
		}
	}

	// カウント／FINISH 等: 迷路 Clear! と同じフルスクリーン三角＋ビューポート経路
	if (m_view.m_srvClear && m_clearBakeA > 0.02f && !m_clearBakeText.IsEmpty()) {
		S3RFrameCB cbToast = cb;
		cbToast.misc.z = 99.f;
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))) {
			memcpy(map.pData,&cbToast,sizeof(cbToast)); dc->Unmap(m_view.m_cbFrame,0);
		}
		D3D11_VIEWPORT tvp={0,(float)h*.34f,(float)w,(float)h*.28f,0,1};
		dc->RSSetViewports(1,&tvp);
		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
		dc->OMSetDepthStencilState(m_view.m_dssOff,0);
		dc->IASetInputLayout(NULL);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);
		dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		dc->PSSetShaderResources(0,1,&m_view.m_srvClear);
		dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,nulls);
		dc->RSSetViewports(1,&vp);
	}

	(void)xformEmitMesh;
	m_view.PresentFrame();
}

void CSoft3DRaceDlg::ShowHelpSheet()
{
	if (g_s3rHelp && g_s3rHelp->GetSafeHwnd()) { g_s3rHelp->SetForegroundWindow(); return; }
	g_s3rHelp = new CS3rHelpDlg(this);
	if (!g_s3rHelp->Create(IDD_S3R_HELP, this)) { delete g_s3rHelp; g_s3rHelp = NULL; return; }
	CCC_PresentOwnedHelp(g_s3rHelp, this);
}

void CSoft3DRaceDlg::ShowContextMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.AddCommand(1, LL14(L"リスタート", L"Restart", L"Redémarrer", L"Riavvia", L"Reiniciar", L"재시작", L"重启", L"إعادة", L"Рестарт", L"Neustart", L"Reiniciar", L"Herstarten", L"Restart", L"Yeniden"));
	menu.AddCommand(2, LL14(L"コース再生成", L"Regenerate course", L"Régénérer", L"Rigenera", L"Regenerar", L"코스 재생성", L"重新生成赛道", L"إعادة توليد", L"Новый курс", L"Strecke neu", L"Gerar de novo", L"Opnieuw genereren", L"Generuj tor", L"Parkuru yenile"));
	menu.AddSeparator();
	menu.AddCheck(20, LL14(L"ミニマップ表示", L"Show minimap", L"Afficher minimap", L"Mostra minimap", L"Mostrar minimapa", L"미니맵", L"显示小地图", L"خريطة", L"Мини-карта", L"Minimap", L"Minimapa", L"Minimapa", L"Minimapa", L"Minimapi"), savedata.s3r_show_map != 0);
	menu.AddSeparator();
	const int mask = ItemMask();
	menu.AddCheck(30, LL14(L"アイテム: テンポ↑", L"Item: tempo↑", L"Objet: tempo↑", L"Oggetto: tempo↑", L"Objeto: tempo↑", L"아이템: 템포↑", L"道具：速度↑", L"Item: tempo↑", L"Item: tempo↑", L"Item: Tempo↑", L"Item: tempo↑", L"Item: tempo↑", L"Przedmiot: tempo↑", L"Öğe: tempo↑"), (mask & ITEM_TEMPO) != 0);
	menu.AddCheck(31, LL14(L"アイテム: テンポ↓", L"Item: tempo↓", L"Objet: tempo↓", L"Oggetto: tempo↓", L"Objeto: tempo↓", L"아이템: 템포↓", L"道具：速度↓", L"Item: tempo↓", L"Item: tempo↓", L"Item: Tempo↓", L"Item: tempo↓", L"Item: tempo↓", L"Przedmiot: tempo↓", L"Öğe: tempo↓"), (mask & ITEM_TEMPO_DN) != 0);
	menu.AddCheck(32, LL14(L"アイテム: ピッチ↑", L"Item: pitch↑", L"Objet: pitch↑", L"Oggetto: pitch↑", L"Objeto: tono↑", L"아이템: 피치↑", L"道具：音高↑", L"Item: pitch↑", L"Item: pitch↑", L"Item: Ton↑", L"Item: tom↑", L"Item: toon↑", L"Przedmiot: wys↑", L"Öğe: perde↑"), (mask & ITEM_PITCH_UP) != 0);
	menu.AddCheck(33, LL14(L"アイテム: ピッチ↓", L"Item: pitch↓", L"Objet: pitch↓", L"Oggetto: pitch↓", L"Objeto: tono↓", L"아이템: 피치↓", L"道具：音高↓", L"Item: pitch↓", L"Item: pitch↓", L"Item: Ton↓", L"Item: tom↓", L"Item: toon↓", L"Przedmiot: wys↓", L"Öğe: perde↓"), (mask & ITEM_PITCH_DN) != 0);
	menu.AddCheck(34, LL14(L"アイテム: 次の曲", L"Item: next", L"Objet: suivant", L"Oggetto: succ", L"Objeto: siguiente", L"아이템: 다음", L"道具：下一曲", L"Item: next", L"Item: next", L"Item: nächster", L"Item: próxima", L"Item: volgend", L"Przedmiot: nast", L"Öğe: sonraki"), (mask & ITEM_NEXT) != 0);
	menu.AddCheck(35, LL14(L"アイテム: 前の曲", L"Item: prev", L"Objet: préc", L"Oggetto: prec", L"Objeto: anterior", L"아이템: 이전", L"道具：上一曲", L"Item: prev", L"Item: prev", L"Item: vorheriger", L"Item: anterior", L"Item: vorig", L"Przedmiot: poprz", L"Öğe: önceki"), (mask & ITEM_PREV) != 0);
	menu.AddCheck(36, LL14(L"アイテム: 音量↑", L"Item: vol↑", L"Objet: vol↑", L"Oggetto: vol↑", L"Objeto: vol↑", L"아이템: 볼륨↑", L"道具：音量↑", L"Item: vol↑", L"Item: vol↑", L"Item: Laut↑", L"Item: vol↑", L"Item: vol↑", L"Przedmiot: głoś↑", L"Öğe: ses↑"), (mask & ITEM_VOL_UP) != 0);
	menu.AddCheck(37, LL14(L"アイテム: 音量↓", L"Item: vol↓", L"Objet: vol↓", L"Oggetto: vol↓", L"Objeto: vol↓", L"아이템: 볼륨↓", L"道具：音量↓", L"Item: vol↓", L"Item: vol↓", L"Item: Laut↓", L"Item: vol↓", L"Item: vol↓", L"Przedmiot: głoś↓", L"Öğe: ses↓"), (mask & ITEM_VOL_DN) != 0);
	menu.AddCheck(38, LL14(L"アイテム: EQ", L"Item: EQ", L"Objet: EQ", L"Oggetto: EQ", L"Objeto: EQ", L"아이템: EQ", L"道具：EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Przedmiot: EQ", L"Öğe: EQ"), (mask & ITEM_EQ) != 0);
	menu.AddCheck(39, LL14(L"アイテム: EQ平坦", L"Item: EQ flat", L"Objet: EQ plat", L"Oggetto: EQ flat", L"Objeto: EQ plano", L"아이템: EQ평탄", L"道具：EQ平坦", L"Item: EQ flat", L"Item: EQ flat", L"Item: EQ flach", L"Item: EQ flat", L"Item: EQ flat", L"Przedmiot: EQ flat", L"Öğe: EQ düz"), (mask & ITEM_EQ_FLAT) != 0);
	menu.AddCheck(40, LL14(L"アイテム: リバーブ", L"Item: reverb", L"Objet: réverb", L"Oggetto: reverb", L"Objeto: reverb", L"아이템: 리버브", L"道具：混响", L"Item: reverb", L"Item: reverb", L"Item: Hall", L"Item: reverb", L"Item: reverb", L"Przedmiot: pogłos", L"Öğe: reverb"), (mask & ITEM_REVERB) != 0);
	menu.AddCheck(41, LL14(L"アイテム: クロスフェード", L"Item: crossfade", L"Objet: fondu", L"Oggetto: crossfade", L"Objeto: fundido", L"아이템: 크로스페이드", L"道具：交叉淡化", L"Item: xfade", L"Item: xfade", L"Item: Crossfade", L"Item: xfade", L"Item: xfade", L"Przedmiot: xfade", L"Öğe: xfade"), (mask & ITEM_XFADE) != 0);
	menu.AddCheck(42, LL14(L"アイテム: ランダム", L"Item: random", L"Objet: aléatoire", L"Oggetto: casuale", L"Objeto: aleatorio", L"아이템: 랜덤", L"道具：随机", L"Item: random", L"Item: random", L"Item: Zufall", L"Item: aleatório", L"Item: willekeurig", L"Przedmiot: losowo", L"Öğe: rastgele"), (mask & ITEM_RANDOM) != 0);
	menu.AddSeparator();
	menu.AddCommand(45, LL14(L"テンポ／ピッチを開いた時に戻す", L"Reset tempo/pitch to open values", L"Remettre tempo/hauteur", L"Ripristina tempo/pitch", L"Restablecer tempo/tono", L"템포/피치 복원", L"恢复速度/音高", L"إعادة الإيقاع", L"Вернуть темп", L"Tempo zurück", L"Restaurar tempo", L"Tempo herstellen", L"Przywróć tempo", L"Tempo sıfırla"));
	menu.AddCommand(54, LL14(L"ズームをリセット", L"Reset zoom", L"Réinit. zoom", L"Reset zoom", L"Restablecer zoom", L"줌 리셋", L"重置缩放", L"إعادة التكبير", L"Сброс зума", L"Zoom reset", L"Redefinir zoom", L"Zoom resetten", L"Reset zoom", L"Zoom sıfırla"));

	UINT cmd = menu.Track(screenPt, this);
	if (cmd == 1) { StartRace(); return; }
	if (cmd == 2) { GenerateCourse(); return; }
	if (cmd == 20) { savedata.s3r_show_map = savedata.s3r_show_map ? 0 : 1; PersistUi(); return; }
	if (cmd >= 30 && cmd <= 42) {
		const int bits[] = { ITEM_TEMPO, ITEM_TEMPO_DN, ITEM_PITCH_UP, ITEM_PITCH_DN, ITEM_NEXT, ITEM_PREV, ITEM_VOL_UP, ITEM_VOL_DN, ITEM_EQ, ITEM_EQ_FLAT, ITEM_REVERB, ITEM_XFADE, ITEM_RANDOM };
		int idx = (int)cmd - 30; if (idx < 0 || idx > 12) return;
		int m = ItemMask(); int bit = bits[idx];
		if (m & bit) m &= ~bit; else m |= bit;
		if (m == 0) m = ITEM_TEMPO;
		savedata.s3r_item_mask = m; PersistUi(); return;
	}
	if (cmd == 45) RestoreAudioBaseline();
	if (cmd == 54) { m_camZoom = 1.f; savedata.s3r_zoom = 100; PersistUi(); }
}

BOOL CSoft3DRaceDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_ai.SetAeroMode(FALSE); m_opp.SetAeroMode(FALSE); m_len.SetAeroMode(FALSE); m_laps.SetAeroMode(FALSE); m_theme.SetAeroMode(FALSE); m_invert.SetAeroMode(FALSE);
	m_start.SetAeroMode(FALSE); m_gen.SetAeroMode(FALSE); m_close.SetAeroMode(FALSE); m_view.SetAeroMode(FALSE);

	if (!m_uiFont.GetSafeHandle()) {
		LOGFONT lf = {}; lf.lfHeight = -18; lf.lfWeight = FW_SEMIBOLD; lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = CLEARTYPE_QUALITY;
		_tcscpy_s(lf.lfFaceName, _T("Segoe UI")); m_uiFont.CreateFontIndirect(&lf);
	}
	if (m_uiFont.GetSafeHandle()) {
		CFont* pf = &m_uiFont;
		CWnd* ws[] = {&m_aiL,&m_oppL,&m_lenL,&m_lapsL,&m_themeL,&m_invertL,&m_hint,&m_status,&m_ai,&m_opp,&m_len,&m_laps,&m_theme,&m_invert,&m_start,&m_gen,&m_close};
		for (int i=0;i<(int)(sizeof(ws)/sizeof(ws[0]));i++) if (ws[i]->GetSafeHwnd()) ws[i]->SetFont(pf);
		CCustomComboBox* cbs[]={&m_ai,&m_opp,&m_len,&m_laps,&m_theme,&m_invert};
		for (int i=0;i<6;i++){ cbs[i]->SetItemHeight(-1,28); cbs[i]->SetItemHeight(0,26); }
	}

	if (savedata.s3r_ai < 0 || savedata.s3r_ai >= AI_COUNT) savedata.s3r_ai = AI_NORMAL;
	if (savedata.s3r_opponents < 1 || savedata.s3r_opponents > 11) savedata.s3r_opponents = 5;
	if (savedata.s3r_len < 0 || savedata.s3r_len > 3) savedata.s3r_len = 0;
	if (savedata.s3r_laps < 0 || savedata.s3r_laps > 10) savedata.s3r_laps = 0;
	if (savedata.s3r_theme < 0 || savedata.s3r_theme > 8) savedata.s3r_theme = 0;
	if (savedata.s3r_item_mask <= 0) savedata.s3r_item_mask = ITEM_ALL;
	if (savedata.s3r_show_map != 0) savedata.s3r_show_map = 1; else savedata.s3r_show_map = 1;
	if (savedata.s3r_zoom < 50 || savedata.s3r_zoom > 250) savedata.s3r_zoom = 100;
	if (savedata.s3r_invert_y != 0) savedata.s3r_invert_y = 1;

	SetWindowText(LL14(L"Soft3D 空中レース", L"Soft3D aerial race", L"Course aérienne Soft3D", L"Gara aerea Soft3D", L"Carrera aérea Soft3D",
		L"Soft3D 공중 레이스", L"Soft3D 空中竞速", L"سباق Soft3D الجوي", L"Воздушная гонка Soft3D", L"Soft3D-Luftrennen",
		L"Corrida aérea Soft3D", L"Soft3D-luchtrace", L"Wyścig powietrzny Soft3D", L"Soft3D hava yarışı"));
	m_aiL.SetWindowText(LL14(L"AI", L"AI", L"IA", L"IA", L"IA", L"AI", L"AI", L"AI", L"ИИ", L"KI", L"IA", L"AI", L"SI", L"YZ"));
	m_oppL.SetWindowText(LL14(L"台数", L"Opponents", L"Adversaires", L"Avversari", L"Rivales", L"대수", L"对手数", L"خصوم", L"Соперники", L"Gegner", L"Oponentes", L"Tegenstanders", L"Rywalе", L"Rakip"));
	m_lenL.SetWindowText(LL14(L"距離", L"Length", L"Longueur", L"Lunghezza", L"Longitud", L"거리", L"长度", L"طول", L"Длина", L"Länge", L"Comprimento", L"Lengte", L"Długość", L"Uzunluk"));
	m_lapsL.SetWindowText(LL14(L"周回", L"Laps", L"Tours", L"Giri", L"Vueltas", L"랩", L"圈数", L"لفات", L"Круги", L"Runden", L"Voltas", L"Ronden", L"Okrążenia", L"Tur"));
	m_themeL.SetWindowText(LL14(L"テーマ", L"Theme", L"Thème", L"Tema", L"Tema", L"테마", L"主题", L"سمة", L"Тема", L"Thema", L"Tema", L"Thema", L"Motyw", L"Tema"));
	m_invertL.SetWindowText(LL14(L"上下", L"Y-axis", L"Axe Y", L"Asse Y", L"Eje Y", L"상하", L"上下", L"محور Y", L"Ось Y", L"Y-Achse", L"Eixo Y", L"Y-as", L"Oś Y", L"Y ekseni"));
	m_start.SetWindowText(LL14(L"スタート", L"Start", L"Démarrer", L"Via", L"Iniciar", L"시작", L"开始", L"ابدأ", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	m_gen.SetWindowText(LL14(L"生成", L"Generate", L"Générer", L"Genera", L"Generar", L"생성", L"生成", L"توليد", L"Создать", L"Erzeugen", L"Gerar", L"Genereren", L"Generuj", L"Oluştur"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(L"マウス: 機体の向き / LMB加速 / RMBブレーキ / ホイールズーム / MMB後方　パッド対応　ステータス右クリック=設定",
		L"Mouse: steer craft / LMB accel / RMB brake / wheel zoom / MMB lookback  Pad OK  Right-click status=settings",
		L"Souris : braquer / LMB accél / RMB frein / molette zoom / MMB recul  Manette OK  Clic droit statut=réglages",
		L"Mouse: sterza / LMB accel / RMB freno / rotella zoom / MMB dietro  Pad OK  Clic destro stato=impostazioni",
		L"Ratón: dirigir / LMB acel / RMB freno / rueda zoom / MMB atrás  Mando OK  Clic derecho estado=ajustes",
		L"마우스: 기체 조종 / LMB가속 / RMB브레이크 / 휠줌 / MMB후방  패드 OK  상태 우클릭=설정",
		L"鼠标：转向机体 / 左键加速 / 右键刹车 / 滚轮缩放 / 中键后视  手柄可用  状态栏右键=设置",
		L"Mouse steer craft / LMB accel / RMB brake / wheel zoom / MMB lookback",
		L"Мышь: рулить / ЛКМ газ / ПКМ тормоз / колесо зум / СКМ назад",
		L"Maus: steuern / LMB Gas / RMB Bremse / Rad Zoom / MMB Rückblick",
		L"Mouse: dirigir / LMB acel / RMB freio / roda zoom / MMB atrás",
		L"Muis: sturen / LMB gas / RMB rem / wiel zoom / MMB achteruit",
		L"Mysz: steruj / LMB gaz / RMB hamulec / kółko zoom / MMB wstecz",
		L"Fare: yönlendir / LMB gaz / RMB fren / teker zoom / MMB geri"));

	m_ai.AddString(LL14(L"超簡単", L"Super easy", L"Très facile", L"Facilissimo", L"Muy fácil", L"매우 쉬움", L"超简单", L"سهل جداً", L"Очень легко", L"Sehr leicht", L"Muito fácil", L"Zeer makkelijk", L"Bardzo łatwy", L"Çok kolay"));
	m_ai.AddString(LL14(L"簡単", L"Easy", L"Facile", L"Facile", L"Fácil", L"쉬움", L"简单", L"سهل", L"Легко", L"Leicht", L"Fácil", L"Makkelijk", L"Łatwy", L"Kolay"));
	m_ai.AddString(LL14(L"普通", L"Normal", L"Normal", L"Normale", L"Normal", L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_ai.AddString(LL14(L"難しい", L"Hard", L"Difficile", L"Difficile", L"Difícil", L"어려움", L"困难", L"صعب", L"Сложно", L"Schwer", L"Difícil", L"Moeilijk", L"Trudny", L"Zor"));
	m_ai.AddString(LL14(L"強烈", L"Intense", L"Intense", L"Intenso", L"Intenso", L"강렬", L"强烈", L"شديد", L"Жёстко", L"Intensiv", L"Intenso", L"Intens", L"Intensywny", L"Yoğun"));
	SetAiToUi(savedata.s3r_ai);

	for (int i=1;i<=11;i++){ CString s; s.Format(_T("%d"), i); m_opp.AddString(s); }
	SetOppToUi(savedata.s3r_opponents);

	m_len.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	m_len.AddString(LL14(L"短い", L"Short", L"Court", L"Corto", L"Corto", L"짧음", L"短", L"قصير", L"Короткий", L"Kurz", L"Curto", L"Kort", L"Krótki", L"Kısa"));
	m_len.AddString(LL14(L"普通", L"Normal", L"Normal", L"Normale", L"Normal", L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_len.AddString(LL14(L"長い", L"Long", L"Long", L"Lungo", L"Largo", L"긴", L"长", L"طويل", L"Длинный", L"Lang", L"Longo", L"Lang", L"Długi", L"Uzun"));
	SetLenToUi(savedata.s3r_len);

	m_laps.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	for (int i=1;i<=10;i++){ CString s; s.Format(_T("%d"), i); m_laps.AddString(s); }
	SetLapsToUi(savedata.s3r_laps);

	m_theme.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	m_theme.AddString(LL14(L"森", L"Forest", L"Forêt", L"Foresta", L"Bosque", L"숲", L"森林", L"غابة", L"Лес", L"Wald", L"Floresta", L"Bos", L"Las", L"Orman"));
	m_theme.AddString(LL14(L"遺跡", L"Ruins", L"Ruines", L"Rovine", L"Ruinas", L"유적", L"遗迹", L"أطلال", L"Руины", L"Ruinen", L"Ruínas", L"Ruïnes", L"Ruiny", L"Kalıntılar"));
	m_theme.AddString(LL14(L"石油工場", L"Oil factory", L"Usine pétrolière", L"Fabbrica petrolio", L"Fábrica de petróleo", L"정유공장", L"炼油厂", L"مصفاة", L"Нефтезавод", L"Ölwerk", L"Refinaria", L"Olierefinaderij", L"Rafineria", L"Petrol fabrikası"));
	m_theme.AddString(LL14(L"夜の街", L"Night city", L"Ville nocturne", L"Città notturna", L"Ciudad nocturna", L"밤의 도시", L"夜城", L"مدينة ليلية", L"Ночной город", L"Nachtstadt", L"Cidade noturna", L"Nachtstad", L"Nocne miasto", L"Gece şehri"));
	m_theme.AddString(LL14(L"水中チューブ", L"Underwater tube", L"Tube sous-marin", L"Tubo subacqueo", L"Tubo submarino", L"수중 튜브", L"水下管道", L"أنبوب تحت الماء", L"Подводная труба", L"Unterwasser-Tube", L"Tubo submarino", L"Onderwaterbuis", L"Rura podwodna", L"Su altı tüp"));
	m_theme.AddString(LL14(L"草原", L"Grassland", L"Prairie", L"Prateria", L"Pradera", L"초원", L"草原", L"مروج", L"Луга", L"Grasland", L"Pradaria", L"Grasland", L"Łąka", L"Otlak"));
	m_theme.AddString(LL14(L"夕焼けメサ", L"Sunset mesa", L"Mesa au couchant", L"Mesa al tramonto", L"Mesa al atardecer", L"석양 메사", L"日落台地", L"هضبة الغروب", L"Закатная меса", L"Sonnenuntergang-Mesa", L"Mesa ao pôr do sol", L"Zonsondergang-mesa", L"Zachodząca mesa", L"Gün batımı mesa"));
	m_theme.AddString(LL14(L"雲の庭", L"Cloud garden", L"Jardin de nuages", L"Giardino di nuvole", L"Jardín de nubes", L"구름 정원", L"云之庭园", L"حديقة سحاب", L"Сад облаков", L"Wolkengarten", L"Jardim de nuvens", L"Wolkentuin", L"Ogród chmur", L"Bulut bahçesi"));
	SetThemeToUi(savedata.s3r_theme);

	m_invert.AddString(LL14(L"通常", L"Normal", L"Normal", L"Normale", L"Normal", L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_invert.AddString(LL14(L"上下反転", L"Invert Y", L"Inverser Y", L"Inverti Y", L"Invertir Y", L"상하 반전", L"上下翻转", L"عكس Y", L"Инверсия Y", L"Y umkehren", L"Inverter Y", L"Y omkeren", L"Odwróć Y", L"Y ters"));
	SetInvertToUi(savedata.s3r_invert_y);

	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tooltip.Activate(TRUE);
		m_tooltip.AddTool(&m_start, LL14(L"デモを終えてカウントダウン開始", L"End demo and start countdown", L"Fin démo + compte à rebours", L"Fine demo + conto alla rovescia", L"Fin demo + cuenta atrás", L"데모 종료 후 카운트다운", L"结束演示并倒计时", L"End demo, start countdown", L"Конец демо и отсчёт", L"Demo beenden und Countdown", L"Fim da demo e contagem", L"Demo uit, dan countdown", L"Koniec demo i odliczanie", L"Demoyu bitir, geri sayım"));
		m_tooltip.AddTool(&m_gen, LL14(L"コース再生成（直後にデモ走行）", L"Regenerate course (then demo lap)", L"Régénérer (puis démo)", L"Rigenera (poi demo)", L"Regenerar (luego demo)", L"코스 재생성(직후 데모)", L"重新生成（随后演示）", L"Regen then demo", L"Новая трасса → демо", L"Strecke neu → Demo", L"Gerar de novo → demo", L"Opnieuw → demo", L"Nowa trasa → demo", L"Yeniden oluştur → demo"));
		m_tooltip.AddTool(&m_invert, LL14(L"マウス上下とパッド上下のピッチを反転します", L"Invert mouse and pad pitch (up/down)", L"Inverser le tangage souris/manette", L"Inverte pitch mouse/pad", L"Invertir pitch de ratón/mando", L"마우스·패드 상하 피치 반전", L"翻转鼠标/手柄俯仰", L"Invert mouse/pad pitch", L"Инверсия pitch мыши/пада", L"Maus-/Pad-Pitch umkehren", L"Inverter pitch do mouse/pad", L"Muis-/pad-pitch omkeren", L"Odwróć pitch myszy/pada", L"Fare/pad pitch ters çevir"));
	}

	CaptureAudioBaseline();
	ApplySavedWindowRect();
	LayoutAll();
	if (!m_view.InitDx()) {
		CString msg;
		msg.Format(L"%s\n(stage=%d hr=0x%08X)",
			LL14(L"DirectX 11 の初期化に失敗しました。",L"DirectX 11 initialization failed.",L"Échec de l'initialisation de DirectX 11.",L"Inizializzazione DirectX 11 non riuscita.",L"Error al iniciar DirectX 11.",L"DirectX 11 초기화에 실패했습니다.",L"DirectX 11 初始化失败。",L"فشل تهيئة DirectX 11.",L"Не удалось инициализировать DirectX 11.",L"DirectX 11 konnte nicht initialisiert werden.",L"Falha ao iniciar o DirectX 11.",L"Initialisatie van DirectX 11 mislukt.",L"Nie udało się zainicjować DirectX 11.",L"DirectX 11 başlatılamadı."),
			m_view.m_dxFailStage, (unsigned)m_view.m_dxFailHr);
		MessageBox(msg, NULL, MB_OK|MB_ICONERROR);
		DestroyWindow();
		return FALSE;
	}
	GenerateCourse();
	m_lastTick = GetTickCount();
	// 描画・更新は og の timerp 経由。独自 SetTimer は使わない
	return TRUE;
}

void CSoft3DRaceDlg::OnStart() { StartRace(); }
void CSoft3DRaceDlg::OnGen() { GenerateCourse(); PersistUi(); }
void CSoft3DRaceDlg::OnCloseBtn() { DestroyWindow(); }
void CSoft3DRaceDlg::OnHelp() { ShowHelpSheet(); }
void CSoft3DRaceDlg::OnAiChanged() { savedata.s3r_ai = ReadAiFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnOppChanged() { savedata.s3r_opponents = ReadOppFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnLenChanged() { savedata.s3r_len = ReadLenFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnLapsChanged() { savedata.s3r_laps = ReadLapsFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnThemeChanged() { savedata.s3r_theme = ReadThemeFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnInvertChanged() { savedata.s3r_invert_y = ReadInvertFromUi(); PersistUi(); }

void CSoft3DRaceDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (pWnd == &m_view) return; // view: no context
	CPoint sp = point;
	if (sp.x < 0) { CRect rc; GetWindowRect(&rc); sp.x = rc.left + 40; sp.y = rc.top + 40; }
	ShowContextMenu(sp);
}

void CSoft3DRaceDlg::TickFrame()
{
	if (!GetSafeHwnd() || !m_view.m_ready) return;
	const DWORD now = GetTickCount();
	float dt = (float)(now - m_lastTick) * 0.001f;
	m_lastTick = now;
	if (dt < 0.f) dt = 0.f; if (dt > 0.05f) dt = 0.05f;
	m_anim += dt;

	if (m_phase == PHASE_COUNTDOWN) TickCountdown(dt);
	else if (m_phase == PHASE_DEMO) {
		TickDemo(dt);
	} else if (m_phase == PHASE_RACE || m_phase == PHASE_FINISH) {
		TickPhysics(dt);
		TickItems(dt);
		// transition to podium
		if (m_phase == PHASE_FINISH && (m_crafts[0].finished || !m_crafts[0].alive)) {
			m_podiumT += dt;
			if (m_podiumT > 2.2f) {
				m_podiumT = 0.f;
				m_phase = PHASE_PODIUM;
				int order[S3R_MAX_CRAFT]; for (int i=0;i<m_craftN;i++) order[i]=i;
				for (int i=0;i<m_craftN;i++) for (int j=i+1;j<m_craftN;j++) if (m_crafts[order[i]].rank > m_crafts[order[j]].rank) { int t=order[i]; order[i]=order[j]; order[j]=t; }
				m_podiumOrder[0]=order[0]; m_podiumOrder[1]=m_craftN>1?order[1]:order[0]; m_podiumOrder[2]=m_craftN>2?order[2]:order[0];
				SplinePoint(0.f, m_podiumBaseX, m_podiumBaseY, m_podiumBaseZ);
				m_podiumBaseY += 2.5f;
				m_camSmoothInit = 0;
				m_clearBakeText = LL14(L"表彰式", L"Podium", L"Podium", L"Podio", L"Podio", L"시상식", L"领奖台", L"منصة", L"Подиум", L"Podium", L"Pódio", L"Podium", L"Podium", L"Podyum");
				m_clearBakeA=1.f; m_clearDirty=1;
				m_standDirty=1;
				for (int i = 0; i < 96; i++) m_confetti[i][5] = 0.f;
			}
		}
	} else if (m_phase == PHASE_PODIUM) {
		TickPodium(dt);
	}

	if (m_phase == PHASE_RACE) {
		if (m_overlayHold > 0.f) m_overlayHold = max(0.f, m_overlayHold - dt);
		const int holdOverlay = (m_overlayHold > 0.f) ? 1 : 0;
		const int wrongOverlay = (m_wrongWay && !holdOverlay) ? 1 : 0;
		if (m_clearBakeA > 0.f && !m_clearBakeText.IsEmpty() && m_clearBakeText.Find(L"FINISH") < 0) {
			if (wrongOverlay) {
				m_clearBakeA = 1.f; // 逆走中は点灯維持
			} else if (!holdOverlay || m_overlayHold < 0.55f) {
				float fade = holdOverlay ? 1.6f : 0.85f;
				m_clearBakeA = max(0.f, m_clearBakeA - dt * fade);
				if (m_clearBakeA <= 0.02f) { m_clearBakeText = L""; m_clearBakeA = 0.f; }
			}
			m_clearDirty = 1;
		}
		if ((now % 100u) < 20u) UpdateStatus();
	} else if ((now % 200u) < 20u) {
		UpdateStatus();
	}
	RenderScene();
	m_view.RequestRedraw();
}

void CSoft3DRaceDlg::OnTimer(UINT_PTR id)
{
	CCustomBlurDialogBase::OnTimer(id);
}
void CSoft3DRaceDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) { LayoutAll(); PersistWindowRect(); }
}
void CSoft3DRaceDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow) LayoutAll();
}
void CSoft3DRaceDlg::OnDestroy()
{
	PersistUi(); PersistWindowRect();
	RestoreAudioBaseline();
	S3rReleaseJoypad();
	CCustomBlurDialogBase::OnDestroy();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void OpenSoft3DRaceModeless(CWnd* p)
{
	CloseSoft3DMazeIfOpen();
	if (g_s3r && g_s3r->GetSafeHwnd()) { g_s3r->SetForegroundWindow(); return; }
	g_s3r = new CSoft3DRaceDlg(p);
	if (!g_s3r->Create(IDD_SOFT3DRACE, p)) { delete g_s3r; g_s3r = NULL; return; }
	g_s3r->ShowWindow(SW_SHOW);
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::UnregisterHotKey(og->GetSafeHwnd(), 8000);
		::UnregisterHotKey(og->GetSafeHwnd(), 8001);
		::UnregisterHotKey(og->GetSafeHwnd(), 8002);
		::UnregisterHotKey(og->GetSafeHwnd(), 8003);
	}
}
void CloseSoft3DRaceIfOpen()
{
	if (g_s3r && g_s3r->GetSafeHwnd()) g_s3r->DestroyWindow();
}
BOOL IsSoft3DRaceOpen()
{
	return (g_s3r && g_s3r->GetSafeHwnd() && ::IsWindow(g_s3r->GetSafeHwnd()) && g_s3r->IsWindowVisible()) ? TRUE : FALSE;
}
BOOL IsSoft3DRaceActive()
{
	return IsSoft3DRaceOpen();
}
BOOL Soft3DRacePreTranslate(MSG* pMsg)
{
	if (!IsSoft3DRaceOpen() || !pMsg || !g_s3r) return FALSE;
	return g_s3r->HandleAccelMessage(pMsg);
}
void Soft3DRaceOnTimerp()
{
	if (!g_s3r || !g_s3r->GetSafeHwnd() || !::IsWindow(g_s3r->GetSafeHwnd())) return;
	if (!g_s3r->IsWindowVisible() || g_s3r->IsIconic()) return;
	g_s3r->TickFrame();
}
