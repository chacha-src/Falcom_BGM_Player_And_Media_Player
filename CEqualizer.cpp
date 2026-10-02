// CEqualizer.cpp : 実装ファイル
//

#include "stdafx.h"
#include "XfadePlayback.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CEqualizer.h"
#include "ProAudio.h"
#include "CPromptEngine.h"
#include "oggDlg.h"

extern COggDlg* og;



// CEqualizer ダイアログ

namespace {

	enum {
		IDM_EQ_SUGGEST_KEY = 42220,
		IDM_EQ_KEY_AUTO = 42221,
		IDM_EQ_ABA = 42222,
		IDM_EQ_ABB = 42223,
		IDM_EQ_ABTOG = 42224,
		IDM_EQ_EFF_SLIDER = 42225,
		IDM_EQ_MASTER_SLIDER = 42226,
		IDM_EQ_REVERB_SLIDER = 42227,
		IDM_EQ_PRESET_BASE = 42300, // +preset index (m_pre)
		IDM_EQ_OPEN_ANALYZER = 42390,
		IDM_EQ_OPEN_PIANO = 42391,
		IDM_EQ_OPEN_MIDIMON = 42392
	};

	static void EqEffSliderCb(void* ctx, int value)
	{
		CEqualizer* p = (CEqualizer*)ctx;
		if (value < 0) value = 0;
		if (value > 200) value = 200;
		savedata.eqsoundeffect = value / 2;
		if (p && ::IsWindow(p->GetSafeHwnd()))
			p->SyncSlidersFromSavedata();
	}

	static void EqMasterSliderCb(void* ctx, int value)
	{
		CEqualizer* p = (CEqualizer*)ctx;
		if (value < 0) value = 0;
		if (value > 200) value = 200;
		// 本体は SetPos(200-eq[15]) なので、メニューは表示値=savedata を直接扱う
		savedata.eq[15] = value;
		if (p && ::IsWindow(p->GetSafeHwnd()))
			p->SyncSlidersFromSavedata();
	}

	static void EqReverbSliderCb(void* ctx, int value)
	{
		CEqualizer* p = (CEqualizer*)ctx;
		if (value < 0) value = 0;
		if (value > 200) value = 200;
		// 本体は SetPos(200-eq_reverb)／ラベルは eq_reverb。メニュー表示もラベルに合わせる
		savedata.eq_reverb = value;
		if (p && ::IsWindow(p->GetSafeHwnd()))
			p->SyncSlidersFromSavedata();
	}

	static void EqChorusSliderCb(void* ctx, int value)
	{
		CEqualizer* p = (CEqualizer*)ctx;
		if (value < 0) value = 0;
		if (value > 200) value = 200;
		savedata.eq_chorus = value;
		if (p && ::IsWindow(p->GetSafeHwnd()))
			p->SyncSlidersFromSavedata();
	}

	static void EqDelaySliderCb(void* ctx, int value)
	{
		CEqualizer* p = (CEqualizer*)ctx;
		if (value < 0) value = 0;
		if (value > 200) value = 200;
		savedata.eq_delay = value;
		if (p && ::IsWindow(p->GetSafeHwnd()))
			p->SyncSlidersFromSavedata();
	}

	static int EqPresetIndexFromKeyCodes(const CString& keyAll)
	{
		CString u = keyAll;
		u.MakeUpper();
		int lt = u.ReverseFind(L'<');
		int gt = u.ReverseFind(L'>');
		CString chord;
		if (lt >= 0 && gt > lt)
			chord = u.Mid(lt + 1, gt - lt - 1);
		else
			chord = u;
		chord.Trim();
		bool minor = false;
		if (chord.Find(L"MIN") >= 0 || chord.Find(L"DIM") >= 0 || chord.Find(L"M7B5") >= 0)
			minor = true;
		else if (chord.GetLength() >= 2 && chord[1] == L'M') {
			if (chord.GetLength() == 2)
				minor = true;
			else if (chord[2] != L'A' && chord[2] != L'J')
				minor = true;
		}
		if (minor) return 31;
		return 11;
	}

class CEqHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_EQ_HELP };
	explicit CEqHelpDlg(CWnd* pParent = nullptr)
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

static CEqHelpDlg* g_eqHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CEqHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CEqHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"イコライザー操作ガイド", L"Equalizer Guide", L"Guide égaliseur", L"Guida equalizzatore",
		L"Guía del ecualizador", L"이퀄라이저 가이드", L"均衡器指南", L"دليل المعادل",
		L"Руководство эквалайзера", L"Equalizer-Anleitung", L"Guia do equalizador", L"Equalizer-gids",
		L"Przewodnik korektora", L"Ekolayzer kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CEqHelpDlg::OnOK() { DestroyWindow(); }
void CEqHelpDlg::OnCancel() { DestroyWindow(); }
void CEqHelpDlg::OnClose() { DestroyWindow(); }

void CEqHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_eqHelpDlg == this)
		g_eqHelpDlg = nullptr;
	delete this;
}

BOOL CEqHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CEqHelpDlg::OnPaint()
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
	title(L, y, LL14(L"イコライザー操作ガイド", L"Equalizer — Guide", L"Égaliseur — Guide", L"Equalizzatore — Guida",
		L"Ecualizador — Guía", L"이퀄라이저 — 가이드", L"均衡器 — 指南", L"المعادل — دليل",
		L"Эквалайзер — руководство", L"Equalizer — Guide", L"Equalizador — Guia", L"Equalizer — Gids",
		L"Korektor — przewodnik", L"Ekolayzer — kılavuz"));
	y += titleLh;
	muted(L, y, LL14(
		L"15 バンド EQ・プリセット・グローバル調整・空間 FX・A/B 比較をまとめたパネルです。",
		L"15-band EQ, presets, global controls, spatial FX, and A/B compare in one panel.",
		L"EQ 15 bandes, préréglages, globales, FX spatiaux et A/B.",
		L"EQ a 15 bande, preset, globali, FX spaziali e A/B.",
		L"EQ de 15 bandas, presets, globales, FX espaciales y A/B.",
		L"15밴드 EQ·프리셋·전역·공간 FX·A/B 비교를 한 패널에.",
		L"15 段 EQ、预设、全局、空间效果与 A/B 对比合一面板。",
		L"معادل 15 نطاقاً وإعدادات وقيم عامة وFX وأ/ب.",
		L"15-полосный EQ, пресеты, глобальные, FX и A/B.",
		L"15-Band-EQ, Presets, Global, Raum-FX und A/B.",
		L"EQ 15 bandas, presets, globais, FX e A/B.",
		L"15-bands EQ, presets, globaal, FX en A/B.",
		L"EQ 15-pasmowy, presety, globalne, FX i A/B.",
		L"15 bant EQ, ön ayarlar, global, FX ve A/B."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KEQ);

	title(L, y, LL14(L"バンドとプリセット", L"Bands & presets", L"Bandes et préréglages", L"Bande e preset",
		L"Bandas y presets", L"밴드와 프리셋", L"频段与预设", L"النطاقات والإعدادات",
		L"Полосы и пресеты", L"Bänder & Presets", L"Bandas e presets", L"Banden & presets",
		L"Pasma i presety", L"Bantlar ve ön ayarlar"));
	y += titleLh;
	body(L, y, LL14(
		L"・縦スライダー …… 25Hz〜16kHz の各帯域ゲイン。左の数値が現在値",
		L"· Vertical sliders …… gain per band (25 Hz–16 kHz). Left number = value",
		L"· Curseurs …… gain par bande (25 Hz–16 kHz). Nombre à gauche",
		L"· Cursori …… gain per banda (25 Hz–16 kHz). Numero a sinistra",
		L"· Deslizadores …… ganancia por banda (25 Hz–16 kHz). Número a la izq.",
		L"· 세로 슬라이더 …… 25Hz~16kHz 대역 게인. 왼쪽 숫자=현재값",
		L"· 竖滑块 …… 25Hz–16kHz 各频段增益；左侧数字为当前值",
		L"· منزلقات …… كسب لكل نطاق (25 هرتز–16 كيلوهرتز). الرقم يساراً",
		L"· Ползунки …… усиление полос (25 Гц–16 кГц). Число слева",
		L"· Schieberegler …… Bandgain (25 Hz–16 kHz). Zahl links",
		L"· Controles …… ganho por banda (25 Hz–16 kHz). Número à esquerda",
		L"· Schuiven …… bandgain (25 Hz–16 kHz). Getal links",
		L"· Suwaki …… wzmocnienie pasm (25 Hz–16 kHz). Liczba po lewej",
		L"· Kaydırıcılar …… bant kazancı (25 Hz–16 kHz). Soldaki sayı")); y += lh;
	body(L, y, LL14(
		L"・プリセット …… ジャンル別カーブを一括適用。手動調整後もそのまま再生に反映",
		L"· Preset …… apply genre curves at once. Manual tweaks still apply live",
		L"· Préréglage …… courbes de genre. Ajustements manuels en direct",
		L"· Preset …… curve per genere. Regolazioni manuali in tempo reale",
		L"· Preset …… curvas por género. Ajustes manuales en vivo",
		L"· 프리셋 …… 장르 커브를 일괄 적용. 수동 조정도 즉시 반영",
		L"· 预设 …… 一键应用曲风曲线；手动调整也会即时生效",
		L"· إعداد …… منحنيات الأنواع. التعديل اليدوي فوري",
		L"· Пресет …… кривые жанров сразу. Ручная правка тоже сразу",
		L"· Preset …… Genre-Kurven auf einmal. Manuell gilt live",
		L"· Preset …… curvas de gênero de uma vez. Ajustes manuais ao vivo",
		L"· Preset …… genre-curves in één keer. Handmatig geldt live",
		L"· Preset …… krzywe gatunków naraz. Ręczne też na żywo",
		L"· Ön ayar …… tür eğrilerini toplu uygula. Elle ayar da anında")); y += lh;
	body(L, y, LL14(
		L"・環境 …… 部屋の響きプリセット。「かかり具合」でウェット量を調整",
		L"· Environment …… room acoustic presets. Effect slider = wet amount",
		L"· Environnement …… salles. Curseur d'effet = wet",
		L"· Ambiente …… stanze. Cursore effetto = wet",
		L"· Entorno …… salas. Deslizador de efecto = wet",
		L"· 환경 …… 방 음향 프리셋. 효과 슬라이더=웨트량",
		L"· 环境 …… 房间混响预设；效果滑块控制 wet 量",
		L"· البيئة …… إعدادات الغرف. شريط التأثير = الرطوبة",
		L"· Среда …… пресеты комнат. Ползунок эффекта = wet",
		L"· Umgebung …… Raum-Presets. Effektregler = Wet",
		L"· Ambiente …… salas. Controle de efeito = wet",
		L"· Omgeving …… kamerpresets. Effectschuif = wet",
		L"· Środowisko …… presety pomieszczeń. Suwak efektu = wet",
		L"· Ortam …… oda ön ayarları. Efekt kaydırıcısı = wet")); y += lh + 2;

	// mini EQ curve diagram
	{
		const int gx = L, gy = y, gw = min(280, rc.Width() - L * 2), gh = lh * 2 + 8;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
		const int midY = gy + gh / 2;
		dc.FillSolidRect(gx + 4, midY, gw - 8, 1, RGB(180, 180, 190));
		POINT pts[8];
		const int levels[] = { 20, 35, 55, 70, 60, 45, 30, 25 };
		for (int i = 0; i < 8; ++i) {
			pts[i].x = gx + 12 + i * ((gw - 24) / 7);
			pts[i].y = gy + gh - 6 - levels[i] * (gh - 12) / 100;
		}
		CPen pen(PS_SOLID, 2, RGB(70, 120, 180));
		CPen* oldPen = dc.SelectObject(&pen);
		dc.Polyline(pts, 8);
		dc.SelectObject(oldPen);
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		y = gy + gh + 4;
	}

	title(L, y, LL14(L"グローバル / FX / A-B", L"Global / FX / A-B", L"Global / FX / A-B", L"Globali / FX / A-B",
		L"Global / FX / A-B", L"전역 / FX / A-B", L"全局 / 效果 / A-B", L"عام / FX / أ-ب",
		L"Глобальные / FX / A-B", L"Global / FX / A-B", L"Global / FX / A-B", L"Globaal / FX / A-B",
		L"Globalne / FX / A-B", L"Global / FX / A-B"));
	y += titleLh;
	body(L, y, LL14(
		L"・マスター / 明瞭 / バランス / 密度 / 立体 …… 全体トーンの仕上げ用",
		L"· Master / Clarity / Balance / Density / 3D …… overall tone finish",
		L"· Maître / Clarté / Balance / Densité / 3D …… finition globale",
		L"· Master / Chiarezza / Bilancio / Densità / 3D …… finitura",
		L"· Máster / Claridad / Balance / Densidad / 3D …… acabado general",
		L"· 마스터 / 명료 / 밸런스 / 밀도 / 입체 …… 전체 톤 마무리",
		L"· 主控 / 清晰 / 平衡 / 密度 / 立体 …… 整体音色收尾",
		L"· رئيسي / وضوح / توازن / كثافة / 3D …… إنهاء النغمة",
		L"· Мастер / Чёткость / Баланс / Плотность / 3D …… общий тон",
		L"· Master / Klarheit / Balance / Dichte / 3D …… Gesamtton",
		L"· Mestre / Clareza / Balanço / Densidade / 3D …… tom geral",
		L"· Master / Helderheid / Balans / Dichtheid / 3D …… eindtoon",
		L"· Master / Klarność / Balans / Gęstość / 3D …… ogólny ton",
		L"· Ana / Netlik / Denge / Yoğunluk / 3B …… genel ton")); y += lh;
	body(L, y, LL14(
		L"・リバーブ / コーラス / ディレイ …… 空間系 FX（即時反映）。0=オフ / 1-100=通常 / 101-200=別モード",
		L"· Reverb / Chorus / Delay …… spatial FX (live). 0=off / 1-100=normal / 101-200=alt mode",
		L"· Réverb / Chorus / Delay …… FX spatiaux (direct). 0=off / 1-100=normal / 101-200=autre",
		L"· Riverbero / Chorus / Delay …… FX spaziali (live). 0=off / 1-100=normale / 101-200=altro",
		L"· Reverb / Chorus / Delay …… FX espaciales (en vivo). 0=off / 1-100=normal / 101-200=otro",
		L"· 리버브 / 코러스 / 딜레이 …… 공간 FX(즉시). 0=끔 / 1-100=기본 / 101-200=다른 모드",
		L"· 混响 / 合唱 / 延迟 …… 空间效果（即时）。0=关 / 1-100=普通 / 101-200=另一模式",
		L"· صدى / كورس / تأخير …… FX مكاني فوري. 0=إيقاف / 1-100=عادي / 101-200=وضع آخر",
		L"· Реверб / Хорус / Дилей …… FX сразу. 0=выкл / 1-100=обычный / 101-200=другой",
		L"· Hall / Chorus / Delay …… Raum-FX (sofort). 0=aus / 1-100=normal / 101-200=anderer Modus",
		L"· Reverb / Chorus / Delay …… FX ao vivo. 0=off / 1-100=normal / 101-200=outro",
		L"· Galm / Chorus / Delay …… ruimte-FX (meteen). 0=uit / 1-100=normaal / 101-200=andere modus",
		L"· Pogłos / Chorus / Delay …… FX natychmiast. 0=wył / 1-100=zwykły / 101-200=inny tryb",
		L"· Yankı / Koro / Gecikme …… mekansal FX (anında). 0=kapalı / 1-100=normal / 101-200=diğer")); y += lh;
	body(L, y, LL14(
		L"・A / B / 切替 …… 現在の EQ+グローバルをスロットに保存し、聴き比べ",
		L"· A / B / Toggle …… store current EQ+global to a slot and A/B compare",
		L"· A / B / Basculer …… enregistrer EQ+global et comparer",
		L"· A / B / Alterna …… salva EQ+global e confronta",
		L"· A / B / Alternar …… guardar EQ+global y comparar",
		L"· A / B / 전환 …… 현재 EQ+전역을 슬롯에 저장해 비교",
		L"· A / B / 切换 …… 将当前 EQ+全局存入槽位并对比试听",
		L"· أ / ب / تبديل …… احفظ EQ+العام وقارن",
		L"· A / B / Перекл. …… сохранить EQ+глобальные и сравнить",
		L"· A / B / Umsch. …… EQ+Global speichern und vergleichen",
		L"· A / B / Alternar …… salvar EQ+global e comparar",
		L"· A / B / Wisselen …… EQ+globaal opslaan en vergelijken",
		L"· A / B / Przełącz …… zapisz EQ+globalne i porównaj",
		L"· A / B / Geç …… mevcut EQ+globali kaydedip karşılaştır")); y += lh;
	body(L, y, LL14(
		L"・イコライザーリセット / グローバルリセット …… 帯域のみ、または全体を戻す",
		L"· EQ reset / Global reset …… restore bands only, or everything",
		L"· Reset EQ / global …… bandes seules, ou tout",
		L"· Reset EQ / globale …… solo bande, o tutto",
		L"· Reset EQ / global …… solo bandas, o todo",
		L"· EQ 리셋 / 전역 리셋 …… 대역만 또는 전체 복원",
		L"· EQ 重置 / 全局重置 …… 仅频段，或全部恢复",
		L"· إعادة EQ / عامة …… النطاقات فقط أو الكل",
		L"· Сброс EQ / глобальный …… только полосы или всё",
		L"· EQ-/Global-Reset …… nur Bänder oder alles",
		L"· Reset EQ / global …… só bandas ou tudo",
		L"· EQ-/globaal reset …… alleen banden of alles",
		L"· Reset EQ / globalny …… tylko pasma lub wszystko",
		L"· EQ sıfırla / genel sıfırla …… yalnızca bantlar veya tümü")); y += lh + 4;

	title(L, y, LL14(L"Soft3D（CCustom の飾り）", L"Soft 3D (CCustom accents)", L"Soft 3D (accents CCustom)", L"Soft 3D (accenti CCustom)",
		L"Soft 3D (acentos CCustom)", L"Soft3D (CCustom 장식)", L"Soft3D（CCustom 装饰）", L"Soft3D (زخارف CCustom)",
		L"Soft 3D (акценты CCustom)", L"Soft 3D (CCustom-Akzente)", L"Soft 3D (acentos CCustom)", L"Soft 3D (CCustom-accenten)",
		L"Soft 3D (akcenty CCustom)", L"Soft 3B (CCustom süs)"));
	y += titleLh;
	body(L, y, LL14(
		L"・スライダーつまみ／ボタン／チェックなどに小さな Soft3D 飾りが入ります（CPU のみ）",
		L"· Slider thumbs, buttons and checks get tiny Soft 3D accents (CPU only)",
		L"· Curseurs, boutons et cases ont de petits accents Soft 3D (CPU seul)",
		L"· Slider, pulsanti e check hanno piccoli accenti Soft 3D (solo CPU)",
		L"· Deslizadores, botones y casillas llevan Soft 3D pequeño (solo CPU)",
		L"· 슬라이더 손잡이·버튼·체크에 작은 Soft3D 장식(CPU만)",
		L"· 滑块拇指、按钮、复选有细小 Soft3D 装饰（仅 CPU）",
		L"· منزلقات وأزرار ومربعات لها زخارف Soft3D صغيرة (معالج فقط)",
		L"· Ползунки, кнопки и флажки — мелкие Soft 3D-акценты (только CPU)",
		L"· Slider, Buttons und Checks mit kleinen Soft-3D-Akzenten (nur CPU)",
		L"· Thumbs, botões e checks têm Soft 3D pequeno (só CPU)",
		L"· Sliderknoppen, knoppen en checks hebben Soft 3D (alleen CPU)",
		L"· Suwaki, przyciski i checkboxy mają Soft 3D (tylko CPU)",
		L"· Kaydırıcı, düğme ve onaylarda Soft 3B süs (yalnızca CPU)")); y += lh;
	muted(L, y, LL14(
		L"視点付きの全面 Soft3D は MP／アナライザー／ピアノロール／コマンドロール側です。",
		L"Full interactive Soft 3D views are on MP, Analyzer, Piano Roll and Command Roll.",
		L"Les vues Soft 3D interactives = MP, analyseur, piano roll, command roll.",
		L"Le viste Soft 3D interattive = MP, analizzatore, piano roll, command roll.",
		L"Las vistas Soft 3D interactivas = MP, analizador, piano roll, command roll.",
		L"시점 Soft3D는 MP·애널라이저·피아노롤·커맨드롤 쪽입니다.",
		L"可操作 Soft3D 在 MP、分析器、钢琴卷帘、命令卷帘。",
		L"مشاهد Soft3D التفاعلية في MP والمحلل والبيانو وCommand Roll.",
		L"Интерактивные Soft 3D — MP, анализатор, piano roll, command roll.",
		L"Interaktive Soft-3D-Ansichten: MP, Analyzer, Piano Roll, Command Roll.",
		L"Soft 3D interativo: MP, analisador, piano roll, command roll.",
		L"Interactieve Soft 3D: MP, analyser, pianorol, command roll.",
		L"Interaktywne Soft 3D: MP, analizator, piano roll, command roll.",
		L"Etkileşimli Soft 3B: MP, analizör, piyano roll, komut roll.")); y += lh + 2;

	muted(L, y, LL14(
		L"キャプションの「?」でこのガイドを開けます。各スライダーにマウスを置くと個別の説明が出ます。",
		L"Open this guide from caption \"?\". Hover sliders for per-control tips.",
		L"Ouvrir via « ? ». Survolez les curseurs pour les détails.",
		L"Apri da « ? ». Passa sui cursori per i dettagli.",
		L"Ábralo con « ? ». Pase el ratón por los deslizadores.",
		L"캡션「?」로 가이드를 엽니다. 슬라이더에 올리면 개별 설명이 나옵니다.",
		L"通过标题栏「?」打开本指南；悬停滑块可看单项说明。",
		L"افتح الدليل من «؟». مرّر على المنزلقات للتفاصيل.",
		L"Откройте через «?». Наведите на ползунки для подсказок.",
		L"Öffnen über „?“. Hover über Regler zeigt Tipps.",
		L"Abra pelo «?». Passe o mouse nos controles para dicas.",
		L"Open via «?». Hover over schuiven voor tips.",
		L"Otwórz przez «?». Najedź na suwaki, by zobaczyć podpowiedzi.",
		L"Başlık «?» ile açın. Kaydırıcılara gelince ayrıntılı ipucu çıkar."));
	y += lh + 2;
	body(L, y, LL14(
		L"・右クリック …… キーからEQを提案 / キー検出時に自動提案",
		L"· Right-click …… suggest EQ from key / auto-suggest on detect",
		L"· Clic droit …… EQ depuis la tonalite / suggestion auto",
		L"· Destro …… EQ dalla tonalita / suggerimento auto",
		L"· Clic der. …… EQ desde tonalidad / sugerencia auto",
		L"· 우클릭 …… 키에서 EQ 제안 / 검출 시 자동 제안",
		L"· 右键 …… 根据调性建议 EQ / 检测时自动建议",
		L"· يمين …… اقتراح EQ من المفتاح / اقتراح تلقائي",
		L"· ПКМ …… EQ по тональности / авто-предложение",
		L"· Rechtsklick …… EQ aus Tonart / Auto-Vorschlag",
		L"· Direito …… EQ pela tonalidade / sugestao auto",
		L"· Rechtsklik …… EQ uit toonsoort / auto-voorstel",
		L"· PPM …… EQ z tonacji / auto-propozycja",
		L"· Sag tik …… anahtardan EQ / otomatik oneri")); y += lh;

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

IMPLEMENT_DYNAMIC(CEqualizer, CCustomBlurDialogExBase)
CEqualizer::CEqualizer(CWnd* pParent /*=nullptr*/)
	: CCustomBlurDialogExBase(IDD_EQUALIZER, pParent)
{

}

CEqualizer::~CEqualizer()
{
}

void CEqualizer::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	// 欠落コントロールや二重 Subclass で CInvalidArgException
	// （「引数が正しくありません」）になり Init が途中終了するのを防ぐ
	auto bind = [](CDataExchange* dx, int id, CWnd& wnd) {
		if (!dx || !dx->m_pDlgWnd) return;
		if (wnd.GetSafeHwnd()) return;
		HWND hDlg = dx->m_pDlgWnd->GetSafeHwnd();
		if (!hDlg || !::GetDlgItem(hDlg, id)) return;
		DDX_Control(dx, id, wnd);
	};
	bind(pDX, IDC_SLIDER7, m_s0);
	bind(pDX, IDC_SLIDER9, m_s1);
	bind(pDX, IDC_SLIDER8, m_s2);
	bind(pDX, IDC_SLIDER10, m_s3);
	bind(pDX, IDC_SLIDER11, m_s4);
	bind(pDX, IDC_SLIDER12, m_s5);
	bind(pDX, IDC_SLIDER13, m_s6);
	bind(pDX, IDC_SLIDER14, m_s7);
	bind(pDX, IDC_SLIDER15, m_s8);
	bind(pDX, IDC_SLIDER16, m_s9);
	bind(pDX, IDC_STATIC_e0, m_v0);
	bind(pDX, IDC_STATIC_e1, m_v1);
	bind(pDX, IDC_STATIC_e2, m_v2);
	bind(pDX, IDC_STATIC_e3, m_v3);
	bind(pDX, IDC_STATIC_e4, m_v4);
	bind(pDX, IDC_STATIC_e5, m_v5);
	bind(pDX, IDC_STATIC_e6, m_v6);
	bind(pDX, IDC_STATIC_e7, m_v7);
	bind(pDX, IDC_STATIC_e8, m_v8);
	bind(pDX, IDC_STATIC_e9, m_v9);
	bind(pDX, IDC_COMBO1, m_env);
	bind(pDX, IDC_COMBO5, m_pre);
	bind(pDX, IDOK, m_ok);
	bind(pDX, IDOK3, dum);
	bind(pDX, IDC_STATIC_e10, m_v10);
	bind(pDX, IDC_STATIC_e11, m_v11);
	bind(pDX, IDC_STATIC_e12, m_v12);
	bind(pDX, IDC_STATIC_e13, m_v13);
	bind(pDX, IDC_STATIC_e14, m_v14);
	bind(pDX, IDC_SLIDER21, m_s14);
	bind(pDX, IDC_SLIDER20, m_s13);
	bind(pDX, IDC_SLIDER19, m_s12);
	bind(pDX, IDC_SLIDER18, m_s11);
	bind(pDX, IDC_SLIDER17, m_s10);
	bind(pDX, IDC_STATIC_eff, m_seff);
	bind(pDX, IDC_SLIDER22, m_eff);
	bind(pDX, IDC_STATIC_EQ_SURROUND, m_surroundLabel);
	bind(pDX, IDC_SLIDER_EQ_SURROUND, m_surround);
	bind(pDX, IDC_STATIC_EQ_SURROUND_VAL, m_surroundVal);
	bind(pDX, IDC_SLIDER23, m_smaster);
	bind(pDX, IDC_SLIDER24, m_ssenmei);
	bind(pDX, IDC_SLIDER25, m_skoutei);
	bind(pDX, IDC_SLIDER26, m_smitsudo);
	bind(pDX, IDC_SLIDER27, m_srittai);
	bind(pDX, IDC_STATIC_e15, m_vmaster);
	bind(pDX, IDC_STATIC_e16, m_vsenmei);
	bind(pDX, IDC_STATIC_e17, m_vkoutei);
	bind(pDX, IDC_STATIC_e18, m_vmitsudo);
	bind(pDX, IDC_STATIC_e19, m_vrittai);
	bind(pDX, IDOK4, sdasdsdadsd);
	bind(pDX, IDC_STATICf, m_t);
	bind(pDX, IDC_STATIC_key, m_keyLow);
	bind(pDX, IDC_STATIC_key2, m_keyMid);
	bind(pDX, IDC_STATIC_key3, m_keyHigh);
	bind(pDX, IDC_STATIC_key4, m_keyAll);
	bind(pDX, IDC_SLIDER28, m_reverb);
	bind(pDX, IDC_SLIDER29, m_chorus);
	bind(pDX, IDC_SLIDER30, m_delay);
	bind(pDX, IDC_STATIC_e20, m_reverbi);
	bind(pDX, IDC_STATIC_e21, m_chorusi);
	bind(pDX, IDC_STATIC_e22, m_delayi);

	bind(pDX, IDC_EQ_ABA, m_abA);
	bind(pDX, IDC_EQ_ABB, m_abB);
	bind(pDX, IDC_EQ_ABTOG, m_abTog);
	bind(pDX, IDC_EQ_HELP, m_help);
}


BEGIN_MESSAGE_MAP(CEqualizer, CCustomBlurDialogExBase)
	ON_CBN_SELCHANGE(IDC_COMBO1, OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO5, OnCbnSelchangeCombo5)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_MESSAGE(WM_EQ_KEY_UPDATE, OnEqKeyUpdate)
	ON_MESSAGE(WM_UITICK_VSYNC, OnUiTick)
	ON_BN_CLICKED(IDOK3, OnBnClickedOk3)
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDOK4, OnBnClickedOk4)
	ON_BN_CLICKED(IDC_EQ_ABA, OnBnClickedAbA)
	ON_BN_CLICKED(IDC_EQ_ABB, OnBnClickedAbB)
	ON_BN_CLICKED(IDC_EQ_ABTOG, OnBnClickedAbTog)
	ON_BN_CLICKED(IDC_EQ_HELP, OnBnClickedHelp)
	ON_WM_CONTEXTMENU()
	ON_COMMAND(IDM_EQ_SUGGEST_KEY, OnSuggestEqFromKey)
	ON_COMMAND(IDM_EQ_KEY_AUTO, OnToggleKeyEqAuto)
END_MESSAGE_MAP()
extern save savedata;
extern int stflg;

void CEqualizer::SyncSlidersFromSavedata()
{
	if (!GetSafeHwnd()) return;

	CString s;
	s.Format(L"%d", savedata.eq[0]);  m_v0.SetWindowText(s);
	s.Format(L"%d", savedata.eq[1]);  m_v1.SetWindowText(s);
	s.Format(L"%d", savedata.eq[2]);  m_v2.SetWindowText(s);
	s.Format(L"%d", savedata.eq[3]);  m_v3.SetWindowText(s);
	s.Format(L"%d", savedata.eq[4]);  m_v4.SetWindowText(s);
	s.Format(L"%d", savedata.eq[5]);  m_v5.SetWindowText(s);
	s.Format(L"%d", savedata.eq[6]);  m_v6.SetWindowText(s);
	s.Format(L"%d", savedata.eq[7]);  m_v7.SetWindowText(s);
	s.Format(L"%d", savedata.eq[8]);  m_v8.SetWindowText(s);
	s.Format(L"%d", savedata.eq[9]);  m_v9.SetWindowText(s);
	s.Format(L"%d", savedata.eq[10]); m_v10.SetWindowText(s);
	s.Format(L"%d", savedata.eq[11]); m_v11.SetWindowText(s);
	s.Format(L"%d", savedata.eq[12]); m_v12.SetWindowText(s);
	s.Format(L"%d", savedata.eq[13]); m_v13.SetWindowText(s);
	s.Format(L"%d", savedata.eq[14]); m_v14.SetWindowText(s);

	if (m_eff.GetSafeHwnd()) {
		m_eff.SetPos(savedata.eqsoundeffect * 2);
		s.Format(L"%d", savedata.eqsoundeffect * 2);
		m_seff.SetWindowText(s);
	}
	if (m_surround.GetSafeHwnd()) {
		int sv = savedata.surround;
		if (sv < 0) sv = 0;
		if (sv > 100) sv = 100;
		m_surround.SetPos(sv);
		s.Format(L"%d", sv);
		m_surroundVal.SetWindowText(s);
	}
	if (m_env.GetSafeHwnd())
		m_env.SetCurSel(savedata.eqsoundenv);
	if (m_smaster.GetSafeHwnd()) {
		m_smaster.SetPos(200 - savedata.eq[15]);
		s.Format(L"%d", savedata.eq[15]);
		m_vmaster.SetWindowText(s);
	}
	if (m_ssenmei.GetSafeHwnd()) {
		m_ssenmei.SetPos(200 - savedata.eq[16]);
		s.Format(L"%d", savedata.eq[16]);
		m_vsenmei.SetWindowText(s);
	}
	if (m_skoutei.GetSafeHwnd()) {
		m_skoutei.SetPos(200 - savedata.eq[17]);
		s.Format(L"%d", savedata.eq[17]);
		m_vkoutei.SetWindowText(s);
	}
	if (m_smitsudo.GetSafeHwnd()) {
		m_smitsudo.SetPos(200 - savedata.eq[18]);
		s.Format(L"%d", savedata.eq[18]);
		m_vmitsudo.SetWindowText(s);
	}
	if (m_srittai.GetSafeHwnd()) {
		m_srittai.SetPos(200 - savedata.eq[19]);
		s.Format(L"%d", savedata.eq[19]);
		m_vrittai.SetWindowText(s);
	}
	if (m_reverb.GetSafeHwnd()) {
		m_reverb.SetPos(200 - savedata.eq_reverb);
		s.Format(L"%d", savedata.eq_reverb);
		m_reverbi.SetWindowText(s);
	}
	if (m_chorus.GetSafeHwnd()) {
		m_chorus.SetPos(200 - savedata.eq_chorus);
		s.Format(L"%d", savedata.eq_chorus);
		m_chorusi.SetWindowText(s);
	}
	if (m_delay.GetSafeHwnd()) {
		m_delay.SetPos(200 - savedata.eq_delay);
		s.Format(L"%d", savedata.eq_delay);
		m_delayi.SetWindowText(s);
	}
	if (m_pre.GetSafeHwnd())
		m_pre.SetCurSel(savedata.eqsoundeq);
	mod = savedata.eqsoundeq;

	CCustomSliderCtrl* bands[] = {
		&m_s0, &m_s1, &m_s2, &m_s3, &m_s4, &m_s5, &m_s6, &m_s7, &m_s8, &m_s9,
		&m_s10, &m_s11, &m_s12, &m_s13, &m_s14
	};
	for (int i = 0; i < 15; ++i) {
		if (bands[i]->GetSafeHwnd())
			bands[i]->SetPos(200 - savedata.eq[i]);
	}
}

// CEqualizer メッセージ ハンドラー
BOOL CEqualizer::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	SetWindowText(LL14(L"イコライザー", L"Equalizer", L"Égaliseur", L"Equalizzatore", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDOK3, LL14(L"イコライザーリセット", L"Equalizer reset", L"Réinitialiser égaliseur", L"Reset equalizzatore", L"Restablecer ecualizador", L"이퀄라이저 초기화", L"均衡器重置", L"إعادة تعيين المعادل", L"Сброс эквалайзера", L"Equalizer zurücksetzen", L"Reset equalizador", L"Equalizer resetten", L"Reset korektora", L"Ekolayzeri sıfırla"));
	SetDlgItemText(IDOK4, LL14(L"グローバルリセット", L"Global reset", L"Réinitialisation globale", L"Reset globale", L"Restablecer global", L"전역 초기화", L"全局重置", L"إعادة تعيين عامة", L"Глобальный сброс", L"Global zurücksetzen", L"Reset global", L"Globaal resetten", L"Reset globalny", L"Genel sıfırlama"));
	SetDlgItemText(IDC_STATIC_EQ_DRY, LL14(L"環境", L"Environment", L"Environnement", L"Ambiente", L"Entorno", L"환경", L"环境", L"البيئة", L"Среда", L"Umgebung", L"Ambiente", L"Omgeving", L"Środowisko", L"Ortam"));
	SetDlgItemText(IDC_STATIC_EQ_WET, LL14(L"プリセット", L"Preset", L"Préréglage", L"Preset", L"Preajuste", L"프리셋", L"预设", L"إعداد مسبق", L"Пресет", L"Voreinstellung", L"Predefinição", L"Voorinstelling", L"Preset", L"Ön ayar"));
	SetDlgItemText(IDC_STATIC_EQ_ACOUSTIC, LL14(L"環境のかかり具合", L"Environment effect", L"Effet d'ambiance", L"Effetto ambiente", L"Efecto de entorno", L"환경 효과 강도", L"环境效果强度", L"قوة تأثير البيئة", L"Сила эффекта среды", L"Umgebungseffekt", L"Efeito de ambiente", L"Omgevingseffect", L"Efekt otoczenia", L"Ortam efekti"));
	SetDlgItemText(IDC_STATIC_EQ_SURROUND, LL14(L"サラウンド", L"Surround", L"Surround", L"Surround", L"Surround", L"서라운드", L"环绕声", L"محيطي", L"Объём", L"Surround", L"Surround", L"Surround", L"Surround", L"Surround"));
	// eq[15..19]: マスター / 明瞭 / バランス / 密度 / 立体（IDC名は旧称のまま）
	SetDlgItemText(IDC_STATIC_EQ_SPECTRUM, LL14(L"マスター", L"Master", L"Maître", L"Master", L"Máster", L"마스터", L"主控", L"رئيسي", L"Мастер", L"Master", L"Mestre", L"Master", L"Master", L"Ana"));
	SetDlgItemText(IDC_STATIC_EQ_FREQ, LL14(L"明瞭", L"Clarity", L"Clarté", L"Chiarezza", L"Claridad", L"명료", L"清晰", L"وضوح", L"Чёткость", L"Klarheit", L"Clareza", L"Helderheid", L"Klarość", L"Netlik"));
	SetDlgItemText(IDC_STATIC_EQ_BAND, LL14(L"バランス", L"Balance", L"Balance", L"Bilancio", L"Balance", L"밸런스", L"平衡", L"توازن", L"Баланс", L"Balance", L"Balanço", L"Balans", L"Balans", L"Denge"));
	SetDlgItemText(IDC_STATIC_EQ_LOUDNESS, LL14(L"密度", L"Density", L"Densité", L"Densità", L"Densidad", L"밀도", L"密度", L"كثافة", L"Плотность", L"Dichte", L"Densidade", L"Dichtheid", L"Gęstość", L"Yoğunluk"));
	SetDlgItemText(IDC_STATIC_EQ_WARMTH, LL14(L"立体", L"3D", L"3D", L"3D", L"3D", L"입체", L"立体", L"مجسم", L"3D", L"3D", L"3D", L"3D", L"3D", L"3D"));

	SetDlgItemText(IDC_STATIC_EQ_REVERB, LL14(L"リバーブ", L"Reverb", L"Réverb", L"Riverbero", L"Reverb", L"리버브", L"混响", L"صدى", L"Реверб", L"Hall", L"Reverb", L"Galm", L"Pogłos", L"Yankı"));
	SetDlgItemText(IDC_STATIC_EQ_CHORUS, LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"코러스", L"合唱", L"كورس", L"Хорус", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"Koro"));
	SetDlgItemText(IDC_STATIC_EQ_DELAY, LL14(L"ディレイ", L"Delay", L"Délai", L"Delay", L"Delay", L"딜레이", L"延迟", L"تأخير", L"Задержка", L"Delay", L"Delay", L"Delay", L"Delay", L"Gecikme"));


	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	// AddTool(NULL) は CInvalidArgException（「引数が正しくありません」）になる
	auto addTip = [this](int id, LPCTSTR text) {
		CWnd* w = GetDlgItem(id);
		if (w && w->GetSafeHwnd())
			m_tooltip.AddTool(w, text);
	};
	addTip(IDOK, LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	addTip(IDOK3, LL14(L"イコライザーの値をリセットします", L"Reset equalizer values", L"Réinitialiser les valeurs de l'égaliseur", L"Reimposta valori equalizer", L"Restablecer valores del ecualizador", L"이퀄라이저 값 초기화", L"重置均衡器数值", L"إعادة تعيين قيم المعادل", L"Сброс значений эквалайзера", L"Equalizerwerte zurücksetzen", L"Redefinir valores do equalizador", L"Equalizatorwaarden resetten", L"Resetuj wartości korektora", L"Ekolayzer değerlerini sıfırla"));
	addTip(IDOK4, LL14(L"グローバルの値をリセットします", L"Reset global values", L"Réinitialiser les valeurs globales", L"Reimposta valori globali", L"Restablecer valores globales", L"전역 값 초기화", L"重置全局数值", L"إعادة تعيين القيم العامة", L"Сброс глобальных значений", L"Globale Werte zurücksetzen", L"Redefinir valores globais", L"Globale waarden resetten", L"Resetuj wartości globalne", L"Genel değerleri sıfırla"));
	addTip(IDC_SLIDER22, LL14(L"サウンドエフェクトの強さを調整します（左の数値が現在値）", L"Adjust sound effect strength (number at left is current value)", L"Ajuster l'intensite de l'effet sonore (nombre a gauche = valeur actuelle)", L"Regola intensita effetto sonoro (numero a sinistra = valore attuale)", L"Ajustar intensidad del efecto de sonido (numero a la izquierda = valor actual)", L"사운드 이펙트 강도 조정(왼쪽 숫자가 현재값)", L"调整音效强度（左侧数字为当前值）", L"ضبط قوة المؤثر الصوتي (الرقم على اليسار = القيمة الحالية)", L"Настроить силу звукового эффекта (число слева — текущее значение)", L"Soundeffekt-Starke einstellen (Zahl links = aktueller Wert)", L"Ajustar intensidade do efeito sonoro (numero a esquerda = valor atual)", L"Sterkte geluidseffect instellen (getal links = huidige waarde)", L"Reguluj sile efektu dzwiekowego (liczba po lewej = biezaca wartosc)", L"Ses efekti gucunu ayarla (soldaki sayi guncel deger)"));
	addTip(IDC_SLIDER_EQ_SURROUND, LL14(L"サラウンド効き目 0=オフ〜100。LRでもマトリクス／位相で展開しやすくし、多ch時はリアを強調します。", L"Surround amount 0=off..100. Matrix/phase for LR expanders; boosts rears on multi-ch.", L"Niveau surround 0=off..100.", L"Intensita surround 0=off..100.", L"Intensidad surround 0=off..100.", L"서라운드 세기 0=끔..100.", L"环绕量 0=关..100。", L"مقدار المحيطي 0=إيقاف..100.", L"Уровень surround 0=выкл..100.", L"Surround-Staerke 0=aus..100.", L"Intensidade surround 0=off..100.", L"Surround-sterkte 0=uit..100.", L"Sila surround 0=wyl..100.", L"Surround seviyesi 0=kapali..100."));
	addTip(IDC_SLIDER23, LL14(L"マスター音量を調整します（左の数値が現在値）\n拡張音量・形式別倍率とは別です", L"Adjust master volume (number at left is current value)\nSeparate from extended/format volume", L"Regler le volume master (nombre a gauche = valeur actuelle)\nDistinct du volume etendu/format", L"Regola volume master (numero a sinistra = valore attuale)\nSeparato da volume esteso/formato", L"Ajustar volumen maestro (numero a la izquierda = valor actual)\nSeparado del volumen extendido/formato", L"마스터 볼륨 조정(왼쪽 숫자가 현재값)\n확장/형식별 볼륨과 별개", L"调整主音量（左侧数字为当前值）\n与扩展/格式音量分开", L"ضبط مستوى الصوت الرئيسي (الرقم على اليسار = القيمة الحالية)\nمنفصل عن الصوت الممتد/حسب التنسيق", L"Настроить общую громкость (число слева — текущее значение)\nОтдельно от доп. и форматной громкости", L"Master-Lautstarke einstellen (Zahl links = aktueller Wert)\nGetrennt von erweiterter/Format-Lautstarke", L"Ajustar volume mestre (numero a esquerda = valor atual)\nSeparado do volume estendido/formato", L"Hoofdvolume instellen (getal links = huidige waarde)\nAparte van uitgebreid/formaatvolume", L"Reguluj glosnosc glowna (liczba po lewej = biezaca wartosc)\nOsobno od rozszerzonej/formatowej", L"Ana ses seviyesini ayarla (soldaki sayi guncel deger)\nGenisletilmis/format sesinden ayri)"));
	addTip(IDC_SLIDER24, LL14(L"明瞭度（クリアさ）を調整します（左の数値が現在値）", L"Adjust clarity (number at left is current value)", L"Ajuster la clarte (nombre a gauche = valeur actuelle)", L"Regola chiarezza (numero a sinistra = valore attuale)", L"Ajustar claridad (numero a la izquierda = valor actual)", L"선명도 조정(왼쪽 숫자가 현재값)", L"调整清晰度（左侧数字为当前值）", L"ضبط الوضوح (الرقم على اليسار = القيمة الحالية)", L"Настроить четкость (число слева — текущее значение)", L"Klarheit einstellen (Zahl links = aktueller Wert)", L"Ajustar clareza (numero a esquerda = valor atual)", L"Helderheid instellen (getal links = huidige waarde)", L"Reguluj klarownosc (liczba po lewej = biezaca wartosc)", L"Netligi ayarla (soldaki sayi guncel deger)"));
	addTip(IDC_SLIDER25, LL14(L"バランス（左右・帯域バランス）を調整します（左の数値が現在値）", L"Adjust balance (L/R and band balance; number at left is current value)", L"Ajuster l'equilibre (gauche/droite et bandes; nombre a gauche = valeur actuelle)", L"Regola bilanciamento (L/R e bande; numero a sinistra = valore attuale)", L"Ajustar balance (I/D y bandas; numero a la izquierda = valor actual)", L"밸런스(좌우·대역) 조정(왼쪽 숫자가 현재값)", L"调整平衡（左右与频段平衡；左侧数字为当前值）", L"ضبط التوازن (يسار/يمين ونطاقات؛ الرقم على اليسار = القيمة الحالية)", L"Настроить баланс (Л/П и полосы; число слева — текущее значение)", L"Balance einstellen (L/R und Bänder; Zahl links = aktueller Wert)", L"Ajustar balanco (E/D e bandas; numero a esquerda = valor atual)", L"Balans instellen (L/R en banden; getal links = huidige waarde)", L"Reguluj balans (L/P i pasma; liczba po lewej = biezaca wartosc)", L"Dengeyi ayarla (L/R ve bant dengesi; soldaki sayi guncel deger)"));
	addTip(IDC_SLIDER26, LL14(L"密度（音の厚み）を調整します（左の数値が現在値）", L"Adjust density (number at left is current value)", L"Ajuster la densite (nombre a gauche = valeur actuelle)", L"Regola densita (numero a sinistra = valore attuale)", L"Ajustar densidad (numero a la izquierda = valor actual)", L"밀도 조정(왼쪽 숫자가 현재값)", L"调整密度（左侧数字为当前值）", L"ضبط الكثافة (الرقم على اليسار = القيمة الحالية)", L"Настроить плотность (число слева — текущее значение)", L"Dichte einstellen (Zahl links = aktueller Wert)", L"Ajustar densidade (numero a esquerda = valor atual)", L"Dichtheid instellen (getal links = huidige waarde)", L"Reguluj gestosc (liczba po lewej = biezaca wartosc)", L"Yogunlugu ayarla (soldaki sayi guncel deger)"));
	addTip(IDC_SLIDER27, LL14(L"立体感（空間感）を調整します（左の数値が現在値）", L"Adjust spatial width (number at left is current value)", L"Ajuster l'espace stereo (nombre a gauche = valeur actuelle)", L"Regola spazialita (numero a sinistra = valore attuale)", L"Ajustar amplitud espacial (numero a la izquierda = valor actual)", L"입체감 조정(왼쪽 숫자가 현재값)", L"调整立体感（左侧数字为当前值）", L"ضبط العرض المكاني (الرقم على اليسار = القيمة الحالية)", L"Настроить пространственность (число слева — текущее значение)", L"Raumlichkeit einstellen (Zahl links = aktueller Wert)", L"Ajustar espacialidade (numero a esquerda = valor atual)", L"Ruimtelijkheid instellen (getal links = huidige waarde)", L"Reguluj przestrzennosc (liczba po lewej = biezaca wartosc)", L"Mekansal genisligi ayarla (soldaki sayi guncel deger)"));
	// oggDlg_ds: 0=オフ / 1-100=モードA / 101-200=モードB（強さは各区間内で 0..1）
	addTip(IDC_SLIDER28, LL14(
		L"リバーブ量（左の数値が現在値）\n0=オフ / 1-100=リバーブ / 101-200=パンリバーブ",
		L"Reverb amount (number at left is current)\n0=off / 1-100=reverb / 101-200=panning reverb",
		L"Quantite de reverb (nombre a gauche)\n0=off / 1-100=reverb / 101-200=reverb panoramique",
		L"Quantita riverbero (numero a sinistra)\n0=off / 1-100=riverbero / 101-200=riverbero pan",
		L"Cantidad de reverb (numero a la izquierda)\n0=off / 1-100=reverb / 101-200=reverb panoramico",
		L"리버브 양(왼쪽 숫자가 현재값)\n0=끔 / 1-100=리버브 / 101-200=팬 리버브",
		L"混响量（左侧为当前值）\n0=关 / 1-100=混响 / 101-200=声像混响",
		L"مقدار الصدى (الرقم على اليسار)\n0=إيقاف / 1-100=صدى / 101-200=صدى بانورامي",
		L"Уровень реверба (число слева)\n0=выкл / 1-100=реверб / 101-200=панорамный реверб",
		L"Hall-Anteil (Zahl links)\n0=aus / 1-100=Hall / 101-200=Pan-Hall",
		L"Quantidade de reverb (numero a esquerda)\n0=off / 1-100=reverb / 101-200=reverb panoramico",
		L"Galmhoeveelheid (getal links)\n0=uit / 1-100=galm / 101-200=pan-galm",
		L"Ilosc poglosu (liczba po lewej)\n0=wył / 1-100=pogłos / 101-200=pogłos panoramiczny",
		L"Yankı miktarı (soldaki sayı)\n0=kapalı / 1-100=yankı / 101-200=pan yankı"));
	addTip(IDC_SLIDER29, LL14(
		L"コーラス量（左の数値が現在値）\n0=オフ / 1-100=コーラス / 101-200=コーラスディストーション",
		L"Chorus amount (number at left is current)\n0=off / 1-100=chorus / 101-200=chorus distortion",
		L"Quantite de chorus (nombre a gauche)\n0=off / 1-100=chorus / 101-200=chorus distortion",
		L"Quantita chorus (numero a sinistra)\n0=off / 1-100=chorus / 101-200=chorus distortion",
		L"Cantidad de chorus (numero a la izquierda)\n0=off / 1-100=chorus / 101-200=chorus distortion",
		L"코러스 양(왼쪽 숫자가 현재값)\n0=끔 / 1-100=코러스 / 101-200=코러스 디스토션",
		L"合唱量（左侧为当前值）\n0=关 / 1-100=合唱 / 101-200=合唱失真",
		L"مقدار الكورس (الرقم على اليسار)\n0=إيقاف / 1-100=كورس / 101-200=تشويه كورس",
		L"Уровень хоруса (число слева)\n0=выкл / 1-100=хорус / 101-200=хорус+дисторшн",
		L"Chorus-Anteil (Zahl links)\n0=aus / 1-100=Chorus / 101-200=Chorus-Distortion",
		L"Quantidade de chorus (numero a esquerda)\n0=off / 1-100=chorus / 101-200=chorus distortion",
		L"Chorushoeveelheid (getal links)\n0=uit / 1-100=chorus / 101-200=chorus-distortion",
		L"Ilosc chorusa (liczba po lewej)\n0=wył / 1-100=chorus / 101-200=chorus+distortion",
		L"Koro miktarı (soldaki sayı)\n0=kapalı / 1-100=koro / 101-200=koro distorsiyon"));
	addTip(IDC_SLIDER30, LL14(
		L"ディレイ量（左の数値が現在値）\n0=オフ / 1-100=ディレイ / 101-200=マルチディレイ（ピンポン）",
		L"Delay amount (number at left is current)\n0=off / 1-100=delay / 101-200=multi-delay (ping-pong)",
		L"Quantite de delay (nombre a gauche)\n0=off / 1-100=delay / 101-200=multi-delay (ping-pong)",
		L"Quantita delay (numero a sinistra)\n0=off / 1-100=delay / 101-200=multi-delay (ping-pong)",
		L"Cantidad de delay (numero a la izquierda)\n0=off / 1-100=delay / 101-200=multi-delay (ping-pong)",
		L"딜레이 양(왼쪽 숫자가 현재값)\n0=끔 / 1-100=딜레이 / 101-200=멀티 딜레이(핑퐁)",
		L"延迟量（左侧为当前值）\n0=关 / 1-100=延迟 / 101-200=多重延迟（乒乓）",
		L"مقدار التأخير (الرقم على اليسار)\n0=إيقاف / 1-100=تأخير / 101-200=تأخير متعدد (بينغ بونغ)",
		L"Уровень дилея (число слева)\n0=выкл / 1-100=дилей / 101-200=мультидилей (пинг-понг)",
		L"Delay-Anteil (Zahl links)\n0=aus / 1-100=Delay / 101-200=Multi-Delay (Ping-Pong)",
		L"Quantidade de delay (numero a esquerda)\n0=off / 1-100=delay / 101-200=multi-delay (pingue-pongue)",
		L"Delayhoeveelheid (getal links)\n0=uit / 1-100=delay / 101-200=multi-delay (pingpong)",
		L"Ilosc delayu (liczba po lewej)\n0=wył / 1-100=delay / 101-200=multi-delay (ping-pong)",
		L"Gecikme miktarı (soldaki sayı)\n0=kapalı / 1-100=gecikme / 101-200=çoklu gecikme (ping-pong)"));
	addTip(IDC_COMBO1, LL14(L"再生環境（部屋の響き）プリセットを選択します", L"Select acoustic environment preset", L"Choisir le preset d'environnement acoustique", L"Seleziona preset ambiente acustico", L"Seleccionar preset de entorno acustico", L"재생 환경(음향) 프리셋 선택", L"选择播放环境（混响）预设", L"اختر إعداد البيئة الصوتية", L"Выбрать пресет акустической среды", L"Akustische Umgebungsvoreinstellung wahlen", L"Selecionar preset de ambiente acustico", L"Akoestische omgevingspreset kiezen", L"Wybierz preset srodowiska akustycznego", L"Akustik ortam on ayarini sec"));
	addTip(IDC_COMBO5, LL14(L"イコライザープリセットを選択します", L"Select equalizer preset", L"Choisir un preset d'egaliseur", L"Seleziona preset equalizzatore", L"Seleccionar preset del ecualizador", L"이퀄라이저 프리셋 선택", L"选择均衡器预设", L"اختر إعداد المعادل", L"Выбрать пресет эквалайзера", L"Equalizer-Voreinstellung wahlen", L"Selecionar preset do equalizador", L"Equalizerpreset kiezen", L"Wybierz preset korektora", L"Ekolayzer on ayarini sec"));
	addTip(IDC_EQ_ABA, LL14(L"現在のEQ/グローバル値をスロットAに保存", L"Store current EQ/global values to slot A", L"Enregistrer EQ/global dans A", L"Salva EQ/global in A", L"Guardar EQ/global en A", L"현재 EQ/전역을 A에 저장", L"将当前EQ/全局存到A", L"حفظ EQ/العام في A", L"Сохранить EQ/глобальные в A", L"EQ/Global in A speichern", L"Salvar EQ/global em A", L"EQ/globaal in A opslaan", L"Zapisz EQ/globalne w A", L"EQ/global degerleri A'ya kaydet"));
	addTip(IDC_EQ_ABB, LL14(L"現在のEQ/グローバル値をスロットBに保存", L"Store current EQ/global values to slot B", L"Enregistrer EQ/global dans B", L"Salva EQ/global in B", L"Guardar EQ/global en B", L"현재 EQ/전역을 B에 저장", L"将当前EQ/全局存到B", L"حفظ EQ/العام في B", L"Сохранить EQ/глобальные в B", L"EQ/Global in B speichern", L"Salvar EQ/global em B", L"EQ/globaal in B opslaan", L"Zapisz EQ/globalne w B", L"EQ/global degerleri B'ye kaydet"));

	addTip(IDC_EQ_ABTOG, LL14(L"スロットA/Bを切り替え", L"Toggle between slots A and B", L"Basculer entre A et B", L"Alterna tra A e B", L"Alternar entre A y B", L"A/B 슬롯 전환", L"在A/B槽间切换", L"التبديل بين A و B", L"Переключить A/B", L"Zwischen A und B umschalten", L"Alternar entre A e B", L"Wissel tussen A en B", L"Przelacz A/B", L"A/B arasinda gec"));
	addTip(IDC_EQ_HELP, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	m_s0.SetMode(3);
	m_s1.SetMode(3);
	m_s2.SetMode(3);
	m_s3.SetMode(3);
	m_s4.SetMode(3);
	m_s5.SetMode(3);
	m_s6.SetMode(3);
	m_s7.SetMode(3);
	m_s8.SetMode(3);
	m_s9.SetMode(3);
	m_s10.SetMode(3);
	m_s11.SetMode(3);
	m_s12.SetMode(3);
	m_s13.SetMode(3);
	m_s14.SetMode(3);


	m_smaster.SetMode(2);
	m_ssenmei.SetMode(2);
	m_skoutei.SetMode(2);
	m_smitsudo.SetMode(2);
	m_srittai.SetMode(2);

	m_reverb.SetMode(2);
	m_chorus.SetMode(2);
	m_delay.SetMode(2);
	m_surround.SetMode(2);
	m_eff.SetMode(2);


	ApplyTitleFont();

	m_t.SetPreferWideMode(TRUE);
	m_t.SetGradation(COLOR_GRAD_DARK_GREEN, COLOR_RANGE_SELECTION, 135, TRUE); // 135 左上から右下
	m_t.SetDropShadow(RGB(0, 0, 0), 45, 18, 7, TRUE);

	// コード表示は頻繁更新のため親ぼかし Invalidate を抑える（時間がたつと UI が死ぬ対策）
	m_keyLow.SetNoParentInvalidate(TRUE);
	m_keyMid.SetNoParentInvalidate(TRUE);
	m_keyHigh.SetNoParentInvalidate(TRUE);
	m_keyAll.SetNoParentInvalidate(TRUE);

	m_s0.SetRange(0, 200);
	m_s1.SetRange(0, 200);
	m_s2.SetRange(0, 200);
	m_s3.SetRange(0, 200);
	m_s4.SetRange(0, 200);
	m_s5.SetRange(0, 200);
	m_s6.SetRange(0, 200);
	m_s7.SetRange(0, 200);
	m_s8.SetRange(0, 200);
	m_s9.SetRange(0, 200);
	m_s10.SetRange(0, 200);
	m_s11.SetRange(0, 200);
	m_s12.SetRange(0, 200);
	m_s13.SetRange(0, 200);
	m_s14.SetRange(0, 200);

	m_eff.SetRange(0, 200);
	m_surround.SetRange(0, 100);

	m_smaster.SetRange(0, 200);
	m_ssenmei.SetRange(0, 200);
	m_skoutei.SetRange(0, 200);
	m_smitsudo.SetRange(0, 200);
	m_srittai.SetRange(0, 200);
	m_reverb.SetRange(0, 200);
	m_chorus.SetRange(0, 200);
	m_delay.SetRange(0, 200);

	SyncSlidersFromSavedata();

	// 環境音響プリセット101種
	//0
	m_env.AddString(LL14(L"なし (処理なし)", L"None (no processing)", L"Aucun (aucun traitement)", L"Nessuno (nessun trattamento)", L"Ninguno (sin procesamiento)", L"없음 (처리 없음)", L"无 (无处理)", L"لا شيء (بدون معالجة)", L"Нет (без обработки)", L"Keiner (keine Verarbeitung)", L"Nenhum (sem processamento)", L"Geen (geen bewerking)", L"Brak (brak przetwarzania)", L"Yok (işlem yok)"));
	m_env.AddString(LL14(L"--[[基本空間 1-10]]--", L"--[[Basic space 1-10]]--", L"--[[Espace de base 1-10]]--", L"--[[Spazio base 1-10]]--", L"--[[Espacio básico 1-10]]--", L"--[[기본 공간 1-10]]--", L"--[[基本空间 1-10]]--", L"--[[المساحة الأساسية 1-10]]--", L"--[[Базовое пространство 1-10]]--", L"--[[Grundraum 1-10]]--", L"--[[Espaço básico 1-10]]--", L"--[[Basisruimte 1-10]]--", L"--[[Przestrzeń podstawowa 1-10]]--", L"--[[Temel alan 1-10]]--"), TRUE);
	//1
	m_env.AddString(LL14(L"風呂場 (タイルの狭い空間、短く明るい金属的な響き)", L"Bathroom (small tiled room, short bright metallic ring)", L"Salle de bain (petite pièce carrelée, résonance métallique courte et claire)", L"Bagno (piccolo locale piastrellato, risonanza metallica corta e brillante)", L"Baño (cuarto alicatado pequeño, resonancia metálica corta y brillante)", L"욕실 (타일로 된 좁은 공간, 짧고 밝은 금속 울림)", L"浴室 (瓷砖小空间，短促明亮的金属回响)", L"حمام (غرفة بلاط ضيقة، رنين معدني قصير ساطع)", L"Ванная (тесная кафельная комната, короткий яркий металлический звон)", L"Badezimmer (kleiner Fliesenraum, kurzer heller Metallklang)", L"Banheiro (quarto azulejado pequeno, tinido metálico curto e brilhante)", L"Badkamer (kleine betegelde ruimte, korte heldere metalen galm)", L"Łazienka (mała kafelkowa przestrzeń, krótki jasny metaliczny dźwięk)", L"Banyo (dar fayans oda, kısa parlak metalik tını)"));
	//2
	m_env.AddString(LL14(L"ホール (中規模でバランスの良い響き)", L"Hall (mid-size hall, even balanced reverb)", L"Salle (salle de taille moyenne, réverbération équilibrée)", L"Sala (sala di medie dimensioni, riverbero equilibrato)", L"Sala (sala mediana, reverberación equilibrada)", L"홀 (중규모로 균형 잡힌 울림)", L"大厅 (中等规模、均衡的混响)", L"قاعة (قاعة متوسطة، صدى متوازن)", L"Зал (зал среднего размера, ровный сбалансированный реверб)", L"Saal (mittelgroßer Saal, ausgewogener Nachhall)", L"Salão (salão médio, reverberação equilibrada)", L"Zaal (middelgrote zaal, evenwichtige nagalm)", L"Sala (średnia sala, zrównoważony pogłos)", L"Salon (orta boy salon, dengeli yankı)"));
	//3
	m_env.AddString(LL14(L"教会 (石造りの長く荘厳な残響)", L"Church (stone church, long solemn reverb)", L"Église (église de pierre, longue réverbération solennelle)", L"Chiesa (chiesa in pietra, lungo riverbero solenne)", L"Iglesia (iglesia de piedra, reverberación larga y solemne)", L"교회 (석조의 길고 장엄한 잔향)", L"教堂 (石造教堂，悠长庄严的混响)", L"كنيسة (كنيسة حجرية، صدى طويل مهيب)", L"Церковь (каменная церковь, длинный торжественный реверб)", L"Kirche (steinerne Kirche, langer feierlicher Nachhall)", L"Igreja (igreja de pedra, reverberação longa e solene)", L"Kerk (stenen kerk, lange plechtige nagalm)", L"Kościół (kamienny kościół, długi uroczysty pogłos)", L"Kilise (taş kilise, uzun ağırbaşlı yankı)"));
	//4
	m_env.AddString(LL14(L"洞窟 (暗くこもった岩の残響)", L"Cave (dark muffled rock-cave reverb)", L"Grotte (grotte rocheuse, réverbération sombre et étouffée)", L"Grotta (grotta di roccia, riverbero scuro e ovattato)", L"Cueva (cueva de roca, reverberación oscura y apagada)", L"동굴 (어둡고 먹먹한 바위 동굴의 잔향)", L"洞穴 (昏暗沉闷的岩石洞穴混响)", L"كهف (كهف صخري، صدى مظلم مكتوم)", L"Пещера (тёмный глухой реверб каменной пещеры)", L"Höhle (dunkler dumpfer Felsnachhall)", L"Caverna (caverna de rocha, reverberação escura e abafada)", L"Grot (donkere gedempte rotsgalm)", L"Jaskinia (ciemny stłumiony pogłos skalnej jaskini)", L"Mağara (karanlık boğuk kaya mağarası yankısı)"));
	//5
	m_env.AddString(LL14(L"スタジオ (吸音されたほぼ無響の乾いた空間)", L"Studio (treated studio, nearly silent and dry)", L"Studio (studio traité, presque silencieux et sec)", L"Studio (studio trattato, quasi silenzioso e asciutto)", L"Estudio (estudio tratado, casi silencioso y seco)", L"스튜디오 (흡음된 거의 무향의 마른 공간)", L"录音室 (吸音处理后近乎无响的干声空间)", L"استوديو (استوديو معالج، جاف شبه صامت)", L"Студия (заглушенная студия, почти сухая и без эха)", L"Studio (gedämmtes Studio, nahezu schalltot und trocken)", L"Estúdio (estúdio tratado, quase silencioso e seco)", L"Studio (behandelde studio, bijna stil en droog)", L"Studio (wygłuszone studio, niemal suche i bez echa)", L"Stüdyo (yalıtımlı stüdyo, neredeyse sessiz ve kuru)"));
	//6
	m_env.AddString(LL14(L"ライブハウス (木の温かみとパンチのある賑やかな響き)", L"Live house (punchy lively wood-room energy)", L"Salle de concert (salle en bois chaleureuse, énergie percutante et vive)", L"Live club (sala di legno calda, energia decisa e vivace)", L"Sala de conciertos (sala de madera cálida, energía contundente y viva)", L"라이브 하우스 (나무의 따뜻함과 펀치감 있는 활기찬 울림)", L"现场演出厅 (木质温暖、有力活泼的现场感)", L"صالة حفلات (قاعة خشبية دافئة، طاقة حية قوية)", L"Клуб (тёплое дерево, плотная живая отдача)", L"Live-Haus (warmes Holz, druckvolle lebendige Energie)", L"Casa de shows (madeira quente, energia viva e impactante)", L"Live venue (warm hout, krachtige levendige energie)", L"Klub koncertowy (ciepłe drewno, dynamiczna i żywa energia)", L"Canlı mekan (sıcak ahşap, güçlü ve canlı enerji)"));
	//7
	m_env.AddString(LL14(L"森 (葉に包まれた柔らかく拡散した響き)", L"Forest (soft diffuse sound wrapped in foliage)", L"Forêt (son doux et diffus, enveloppé de feuillage)", L"Foresta (suono morbido e diffuso, avvolto dal fogliame)", L"Bosque (sonido suave y difuso, envuelto en follaje)", L"숲 (잎에 감싸인 부드럽고 퍼지는 울림)", L"森林 (被树叶包裹的柔和扩散声)", L"غابة (صوت ناعم منتشر، ملفوف بالأوراق)", L"Лес (мягкий рассеянный звук в листве)", L"Wald (weicher diffuser Klang, in Laub gehüllt)", L"Floresta (som suave e difuso, envolto em folhagem)", L"Bos (zacht diffuus geluid, gehuld in bladeren)", L"Las (miękki rozproszony dźwięk wśród liści)", L"Orman (yapraklara sarılı yumuşak dağınık ses)"));
	//8
	m_env.AddString(LL14(L"山 (遠くからはっきり返る山彦)", L"Mountain (distant distinct mountain echoes)", L"Montagne (échos de montagne lointains et distincts)", L"Montagna (echi di montagna lontani e distinti)", L"Montaña (ecos de montaña lejanos y nítidos)", L"산 (멀리서 또렷이 돌아오는 메아리)", L"山岳 (远处清晰折返的回声)", L"جبل (صدى جبلي بعيد وواضح)", L"Гора (далёкое отчётливое горное эхо)", L"Berg (ferne klare Bergechos)", L"Montanha (ecos de montanha distantes e nítidos)", L"Berg (verre duidelijke bergecho's)", L"Góra (odległe wyraźne górskie echa)", L"Dağ (uzaktan net dağ yankıları)"));
	//9
	m_env.AddString(LL14(L"広場 (舗装の広場に広がる開放的な空気感)", L"Plaza (open paved square, airy reflections)", L"Place (place pavée ouverte, réflexions aérées)", L"Piazza (piazza lastricata aperta, riflessi ariosi)", L"Plaza (plaza pavimentada abierta, reflejos aireados)", L"광장 (포장된 광장에 퍼지는 개방적인 공기감)", L"广场 (铺装广场上开阔通透的空气感)", L"ساحة (ساحة مرصوفة مفتوحة، انعكاسات منعشة)", L"Площадь (открытая мостовая площадь, воздушные отражения)", L"Platz (offener Pflasterplatz, luftige Reflexionen)", L"Praça (praça pavimentada aberta, reflexos arejados)", L"Plein (open bestraat plein, luchtige reflecties)", L"Plac (otwarty brukowany plac, przewiewne odbicia)", L"Meydan (açık kaldırım meydanı, ferah yansımalar)"));
	//10
	m_env.AddString(LL14(L"カテドラル (巨大な石造りの圧倒的に長い残響)", L"Cathedral (huge stone space, overwhelming long reverb)", L"Cathédrale (immense espace de pierre, réverbération longue et écrasante)", L"Cattedrale (enorme spazio di pietra, riverbero lungo e schiacciante)", L"Catedral (enorme espacio de piedra, reverberación larga y abrumadora)", L"대성당 (거대한 석조의 압도적으로 긴 잔향)", L"大教堂 (巨大石造空间，压倒性的悠长混响)", L"كاتدرائية (فضاء حجري هائل، صدى طويل مهيمن)", L"Собор (огромное каменное пространство, подавляюще длинный реверб)", L"Kathedrale (riesiger Steinraum, überwältigend langer Nachhall)", L"Catedral (espaço de pedra enorme, reverberação longa e avassaladora)", L"Kathedraal (reusachtige steenruimte, overweldigend lange nagalm)", L"Katedra (ogromna kamienna przestrzeń, przytłaczająco długi pogłos)", L"Katedral (devasa taş mekân, ezici uzun yankı)"));
	m_env.AddString(LL14(L"--[[公共施設 11-20]]--", L"--[[Public 11-20]]--", L"--[[Public 11-20]]--", L"--[[Pubblico 11-20]]--", L"--[[Público 11-20]]--", L"--[[공공시설 11-20]]--", L"--[[公共设施 11-20]]--", L"--[[عامة 11-20]]--", L"--[[Публичное 11-20]]--", L"--[[Öffentlich 11-20]]--", L"--[[Público 11-20]]--", L"--[[Openbaar 11-20]]--", L"--[[Publiczny 11-20]]--", L"--[[Kamu 11-20]]--"), TRUE);
	//11
	m_env.AddString(LL14(L"体育館 (硬い金属的な反射が繰り返す)", L"Gymnasium (hard metallic reflections that bounce back)", L"Gymnase (réflexions métalliques dures qui rebondissent)", L"Palestra (riflessi metallici duri che rimbalzano)", L"Gimnasio (reflexiones metálicas duras que rebotan)", L"체육관 (단단한 금속적 반사가 되돌아오는)", L"体育馆 (坚硬金属反射反复弹回)", L"صالة رياضية (انعكاسات معدنية صلبة ترتد مراراً)", L"Спортзал (жёсткие металлические отражения, что отскакивают)", L"Turnhalle (harte metallische Reflexionen, die zurückprallen)", L"Ginásio (reflexos metálicos duros que ricocheteiam)", L"Sporthal (harde metalen reflecties die terugkaatsen)", L"Hala sportowa (twarde metaliczne odbicia, które wracają)", L"Spor salonu (sert metalik yansımalar geri seker)"));
	//12
	m_env.AddString(LL14(L"峡谷 (両岸から交互に返るエコー)", L"Canyon (echoes alternating from both canyon walls)", L"Canyon (échos alternés des deux parois du canyon)", L"Canyon (echi alternati da entrambe le pareti del canyon)", L"Cañón (ecos que alternan desde ambas paredes del cañón)", L"협곡 (양쪽 벽에서 번갈아 돌아오는 메아리)", L"峡谷 (两岸交替折返的回声)", L"وادٍ (أصداء تتناوب من جداري الوادي)", L"Каньон (эхо по очереди с обоих берегов каньона)", L"Schlucht (Echos abwechselnd von beiden Schluchtwänden)", L"Cânion (ecos que se alternam das duas paredes do cânion)", L"Canyon (echo's om beurten van beide canyonwanden)", L"Kanion (echa na przemian z obu ścian kanionu)", L"Kanyon (kanyonun iki duvarından dönüşümlü yankılar)"));
	//13
	m_env.AddString(LL14(L"地下室 (狭く湿ったコンクリートの圧迫感)", L"Basement (cramped damp concrete, close and heavy)", L"Sous-sol (béton humide et étroit, proche et lourd)", L"Seminterrato (cemento umido e angusto, vicino e pesante)", L"Sótano (hormigón húmedo y estrecho, cercano y pesado)", L"지하실 (좁고 축축한 콘크리트의 답답함)", L"地下室 (狭窄潮湿混凝土的压迫感)", L"قبو (خرسانة رطبة ضيقة، قريبة وثقيلة)", L"Подвал (тесный влажный бетон, близко и тяжело)", L"Keller (enger feuchter Beton, nah und schwer)", L"Porão (concreto úmido e apertado, próximo e pesado)", L"Kelder (krap vochtig beton, dichtbij en zwaar)", L"Piwnica (ciasny wilgotny beton, blisko i ciężko)", L"Bodrum (dar nemli beton, yakın ve ağır)"));
	//14
	m_env.AddString(LL14(L"劇場 (客席に吸われ台詞が通る明瞭な響き)", L"Theater (clear hall, seats soak up the sound)", L"Théâtre (salle claire, les sièges absorbent le son)", L"Teatro (sala chiara, i sedili assorbono il suono)", L"Teatro (sala clara, los asientos absorben el sonido)", L"극장 (객석에 흡수되어 대사가 통하는 맑은 울림)", L"剧院 (被座席吸收、台词清晰的厅堂)", L"مسرح (قاعة واضحة، المقاعد تمتص الصوت)", L"Театр (ясный зал, кресла впитывают звук)", L"Theater (klarer Saal, Sitze schlucken den Klang)", L"Teatro (sala clara, os assentos absorvem o som)", L"Theater (heldere zaal, stoelen vangen het geluid)", L"Teatr (wyraźna sala, fotele pochłaniają dźwięk)", L"Tiyatro (net salon, koltuklar sesi emer)"));
	//15
	m_env.AddString(LL14(L"水中 (こもって揺れる水の中の音)", L"Underwater (muffled wobbling underwater sound)", L"Sous l'eau (son sous-marin étouffé et ondulant)", L"Sott'acqua (suono sott'acqua ovattato e ondulante)", L"Bajo el agua (sonido submarino apagado y ondulante)", L"수중 (먹먹하고 흔들리는 물속의 소리)", L"水下 (沉闷摇晃的水下声)", L"تحت الماء (صوت تحت الماء مكتوم ومتموج)", L"Под водой (приглушённый колышущийся подводный звук)", L"Unter Wasser (gedämpfter wogender Unterwasserklang)", L"Subaquático (som subaquático abafado e ondulante)", L"Onder water (gedempt wiegend onderwatergeluid)", L"Pod wodą (stłumiony kołyszący dźwięk pod wodą)", L"Su altı (boğuk sallanan su altı sesi)"));
	//16
	m_env.AddString(LL14(L"トンネル/地下道 (平行な壁で何度も跳ね返る狭い響き)", L"Tunnel/Underpass (repeating echoes between parallel walls)", L"Tunnel/Souterrain (échos répétés entre parois parallèles, étroit)", L"Tunnel/Sottopasso (echi ripetuti tra pareti parallele, stretto)", L"Túnel/Paso subterráneo (ecos repetidos entre paredes paralelas, estrecho)", L"터널/지하도 (평행한 벽 사이의 좁은 플러터 반사)", L"隧道/地下通道 (平行墙壁间反复弹回的狭窄颤动回声)", L"نفق/ممر سفلي (صدى متكرر بين جدران متوازية، ضيق)", L"Туннель/Подземный переход (флаттер-эхо между параллельными стенами, узко)", L"Tunnel/Unterführung (Flatterecho zwischen parallelen Wänden, eng)", L"Túnel/Passagem subterrânea (ecos repetidos entre paredes paralelas, estreito)", L"Tunnel/Onderdoorgang (fladderecho tussen parallelle wanden, smal)", L"Tunel/Przejście podziemne (echa wielokrotne między równoległymi ścianami, wąsko)", L"Tünel/Alt geçit (paralel duvarlar arasında çoklu yankı, dar)"));
	//17
	m_env.AddString(LL14(L"アリーナ/ドーム (巨大ドームに観客が吸う広い残響)", L"Arena/Dome (huge dome, crowd-soaked wide reverb)", L"Arène/Dôme (immense dôme, large réverbération absorbée par le public)", L"Arena/Cupola (enorme cupola, ampio riverbero assorbito dal pubblico)", L"Arena/Cúpula (enorme cúpula, reverberación amplia absorbida por el público)", L"아레나/돔 (거대한 돔에서 관중이 흡수하는 넓은 잔향)", L"竞技场/穹顶 (巨大穹顶中被观众吸收的宽阔混响)", L"ساحة/قبة (قبة هائلة، صدى واسع يمتصه الجمهور)", L"Арена/Купол (огромный купол, широкий реверб, впитанный зрителями)", L"Arena/Kuppel (riesige Kuppel, weiter vom Publikum geschluckter Nachhall)", L"Arena/Cúpula (cúpula enorme, reverberação ampla absorvida pelo público)", L"Arena/Koepel (enorme koepel, wijde nagalm die het publiek opvangt)", L"Arena/Kopuła (ogromna kopuła, szeroki pogłos pochłaniany przez publiczność)", L"Arena/Kubbe (devasa kubbe, seyircinin emdiği geniş yankı)"));
	//18
	m_env.AddString(LL14(L"小部屋/クローゼット (衣類に吸われた極小の密閉空間)", L"Small room/Closet (tiny enclosed closet, almost no ring)", L"Petite pièce/Placard (minuscule placard fermé, presque sans sonnerie)", L"Stanzino/Armadio (minuscolo armadio chiuso, quasi senza squillo)", L"Cuartito/Armario (armario diminuto cerrado, casi sin timbre)", L"작은 방/옷장 (옷에 흡수된 아주 작은 밀폐 공간)", L"小房间/壁橱 (被衣物吸收的极小密闭空间)", L"غرفة صغيرة/خزانة (خزانة صغيرة مغلقة، بلا رنين تقريباً)", L"Каморка/Шкаф (крошечный закрытый шкаф, почти без звона)", L"Kleiner Raum/Schrank (winziger geschlossener Schrank, kaum Klang)", L"Quartinho/Armário (armário minúsculo fechado, quase sem tinido)", L"Kamertje/Kast (piepkleine gesloten kast, bijna geen galm)", L"Pokoik/Szafa (maleńka zamknięta szafa, niemal bez dźwięku)", L"Küçük oda/Dolap (minik kapalı dolap, neredeyse tını yok)"));
	//19
	m_env.AddString(LL14(L"階段室 (高い吹き抜けを螺旋状に巡る反射)", L"Stairwell (tall stairwell, reflections spiral upward)", L"Cage d'escalier (cage d'escalier haute, réflexions en spirale)", L"Vano scala (vano scala alto, riflessi a spirale)", L"Hueco de escalera (hueco de escalera alto, reflexiones en espiral)", L"계단실 (높은 계단실을 나선으로 도는 반사)", L"楼梯间 (高挑楼梯间里螺旋回转的反射)", L"بئر الدرج (بئر درج مرتفع، انعكاسات حلزونية)", L"Лестничная клетка (высокая лестница, отражения спиралью вверх)", L"Treppenhaus (hohes Treppenhaus, Reflexionen spiralen empor)", L"Caixa de escada (caixa de escada alta, reflexos em espiral)", L"Trappenhuis (hoog trappenhuis, reflecties in een spiraal)", L"Klatka schodowa (wysoka klatka, odbicia kręcą się w górę)", L"Merdiven boşluğu (yüksek merdiven boşluğu, sarmal yansımalar)"));
	//20
	m_env.AddString(LL14(L"地下鉄ホーム (硬いコンクリートとトンネルへ抜ける響き)", L"Subway platform (hard urban concrete, opening into a tunnel)", L"Quai de métro (béton urbain dur, qui s'ouvre vers un tunnel)", L"Banchina metro (cemento urbano duro, che si apre verso un tunnel)", L"Andén de metro (hormigón urbano duro, que se abre hacia un túnel)", L"지하철 승강장 (단단한 콘크리트와 터널로 빠지는 울림)", L"地铁站台 (坚硬都市混凝土，声音通向隧道)", L"رصيف مترو الأنفاق (خرسانة حضرية صلبة تنفتح نحو نفق)", L"Платформа метро (жёсткий городской бетон, уходящий в тоннель)", L"U-Bahn-Bahnsteig (harter Stadtbeton, der in einen Tunnel mündet)", L"Plataforma de metrô (concreto urbano duro, abrindo para um túnel)", L"Metroperron (hard stadsbeton, dat uitkomt in een tunnel)", L"Peron metra (twardy miejski beton, uchodzący w tunel)", L"Metro peronu (sert kentsel beton, tünele açılan yankı)"));
	m_env.AddString(LL14(L"--[[産業・商業 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industriel et commercial 21-30]]--", L"--[[Industriale e commerciale 21-30]]--", L"--[[Industrial y comercial 21-30]]--", L"--[[산업·상업 21-30]]--", L"--[[产业·商业 21-30]]--", L"--[[صناعي وتجاري 21-30]]--", L"--[[Промышленность и торговля 21-30]]--", L"--[[Industrie und Handel 21-30]]--", L"--[[Industrial e comercial 21-30]]--", L"--[[Industrieel en commercieel 21-30]]--", L"--[[Przemysł i handel 21-30]]--", L"--[[Endüstriyel ve ticari 21-30]]--"), TRUE);
	//21
	m_env.AddString(LL14(L"倉庫 (鉄骨とコンクリートの高い乾いた反射)", L"Warehouse (tall steel-and-concrete dry reflections)", L"Entrepôt (hautes réflexions sèches d'acier et de béton)", L"Magazzino (alti riflessi asciutti di acciaio e cemento)", L"Almacén (reflexiones altas y secas de acero y hormigón)", L"창고 (철골과 콘크리트의 높고 마른 반사)", L"仓库 (钢骨与混凝土的高挑干燥反射)", L"مستودع (انعكاسات جافة عالية من فولاذ وخرسانة)", L"Склад (высокие сухие отражения стали и бетона)", L"Lagerhalle (hohe trockene Reflexionen aus Stahl und Beton)", L"Armazém (reflexos altos e secos de aço e concreto)", L"Magazijn (hoge droge reflecties van staal en beton)", L"Magazyn (wysokie suche odbicia stali i betonu)", L"Depo (çelik ve betonun yüksek kuru yansımaları)"));
	//22
	m_env.AddString(LL14(L"廊下 (狭い平行壁の多重反射)", L"Corridor (repeating bounce between narrow walls)", L"Couloir (échos répétés entre murs étroits)", L"Corridoio (echi ripetuti tra muri stretti)", L"Pasillo (ecos repetidos entre paredes estrechas)", L"복도 (좁은 평행 벽의 플러터 반사)", L"走廊 (狭窄平行墙间的颤动回声)", L"ممر (صدى متكرر بين جدران ضيقة)", L"Коридор (флаттер-эхо между узкими стенами)", L"Flur (Flatterecho zwischen engen Wänden)", L"Corredor (ecos repetidos entre paredes estreitas)", L"Gang (fladderecho tussen smalle wanden)", L"Korytarz (echa wielokrotne między wąskimi ścianami)", L"Koridor (dar duvarlar arasında çoklu yankı)"));
	//23
	m_env.AddString(LL14(L"工場 (金属機械が共鳴する硬く明るい響き)", L"Factory (resonant metal machinery, hard and bright)", L"Usine (machines métalliques résonantes, dures et claires)", L"Fabbrica (macchinari metallici risonanti, duri e brillanti)", L"Fábrica (maquinaria metálica resonante, dura y brillante)", L"공장 (금속 기계가 울리는 단단하고 밝은 소리)", L"工厂 (金属机械共鸣，坚硬明亮)", L"مصنع (آلات معدنية رنانة، صلبة ساطعة)", L"Завод (резонирующие металлические машины, жёстко и ярко)", L"Fabrik (resonierende Metallmaschinen, hart und hell)", L"Fábrica (máquinas metálicas ressonantes, duras e brilhantes)", L"Fabriek (resonante metalen machines, hard en helder)", L"Fabryka (rezonujące metalowe maszyny, twarde i jasne)", L"Fabrika (rezonanslı metal makineler, sert ve parlak)"));
	//24
	m_env.AddString(LL14(L"寺社仏閣 (木造の温かい高い天井の拡散)", L"Temple/Shrine (warm wooden temple, tall diffuse reverb)", L"Temple/Sanctuaire (temple de bois chaud, haute réverbération diffuse)", L"Tempio/Santuario (tempio di legno caldo, alto riverbero diffuso)", L"Templo/Santuario (templo de madera cálido, reverberación alta y difusa)", L"사찰/신사 (목조의 따뜻하고 높은 천장의 퍼지는 울림)", L"寺庙神社 (木造温暖、高天花的扩散混响)", L"معبد/ضريح (معبد خشبي دافئ، صدى منتشر مرتفع)", L"Храм/Святилище (тёплый деревянный храм, высокий рассеянный реверб)", L"Tempel/Schrein (warmer Holztempel, hoher diffuser Nachhall)", L"Templo/Santuário (templo de madeira quente, reverberação alta e difusa)", L"Tempel/Heiligdom (warme houten tempel, hoge diffuse nagalm)", L"Świątynia/Sanktuarium (ciepła drewniana świątynia, wysoki rozproszony pogłos)", L"Tapınak/Türbe (sıcak ahşap tapınak, yüksek dağınık yankı)"));
	//25
	m_env.AddString(LL14(L"宇宙空間 (真空のように広く微かにきらめく)", L"Outer space (vacuum-wide, faint floating shimmer)", L"Espace (vaste comme le vide, léger scintillement flottant)", L"Spazio (vasto come il vuoto, lieve scintillio fluttuante)", L"Espacio (amplio como el vacío, destello flotante tenue)", L"우주 공간 (진공처럼 넓고 희미하게 반짝임)", L"宇宙空间 (如真空般开阔，微弱漂浮的粼光)", L"الفضاء الخارجي (واسع كالفراغ، وميض عائم خافت)", L"Космос (широко как вакуум, слабое плывущее мерцание)", L"Weltraum (weit wie ein Vakuum, zartes schwebendes Schimmern)", L"Espaço sideral (amplo como o vácuo, cintilação flutuante tênue)", L"Ruimte (wijd als vacuüm, vage zwevende schittering)", L"Kosmos (szeroko jak próżnia, słabe unoszące się migotanie)", L"Uzay (boşluk kadar geniş, hafif yüzen ışıltı)"));
	//26
	m_env.AddString(LL14(L"野球場/サッカー場 (屋外スタンドに吸われ遠く遅れて返る)", L"Stadium (outdoor stands soak sound, distant late bounce)", L"Stade (tribunes extérieures absorbent, rebond lointain tardif)", L"Stadio (tribune all'aperto assorbono, rimbalzo lontano e tardo)", L"Estadio (gradas al aire libre absorben, rebote lejano y tardío)", L"야구장/축구장 (야외 스탠드에 흡수되고 멀리서 늦게 돌아오는 소리)", L"棒球场/足球场 (被露天看台吸收，远处迟回)", L"ملعب (المدرجات الخارجية تمتص الصوت، ارتداد بعيد متأخر)", L"Стадион (уличные трибуны впитывают звук, далёкий поздний отзвук)", L"Stadion (Freilufttribünen schlucken den Klang, ferner später Rückwurf)", L"Estádio (arquibancadas ao ar livre absorvem, ricochete distante e tardio)", L"Stadion (tribunes buiten vangen het geluid, verre late kaats)", L"Stadion (trybuny na zewnątrz pochłaniają dźwięk, odległe późne odbicie)", L"Stadyum (açık tribünler sesi emer, uzak geç sekme)"));
	//27
	m_env.AddString(LL14(L"図書館 (書架に吸われた静かで柔らかい空間)", L"Library (quiet soft room, books swallow the sound)", L"Bibliothèque (salle calme et douce, les livres avalent le son)", L"Biblioteca (stanza quieta e morbida, i libri inghiottono il suono)", L"Biblioteca (sala quieta y suave, los libros tragan el sonido)", L"도서관 (책장에 흡수된 조용하고 부드러운 공간)", L"图书馆 (被书架吸收的安静柔软空间)", L"مكتبة (غرفة هادئة ناعمة، الكتب تبتلع الصوت)", L"Библиотека (тихая мягкая комната, книги глотают звук)", L"Bibliothek (stiller weicher Raum, Bücher schlucken den Klang)", L"Biblioteca (sala quieta e macia, os livros engolem o som)", L"Bibliotheek (stille zachte ruimte, boeken slokken het geluid)", L"Biblioteka (cichy miękki pokój, książki pochłaniają dźwięk)", L"Kütüphane (sessiz yumuşak oda, kitaplar sesi yutar)"));
	//28
	m_env.AddString(LL14(L"プール(室内) (タイルとガラスの湿った明るい響き)", L"Indoor pool (humid tiled pool, bright glassy ring)", L"Piscine couverte (piscine carrelée humide, sonnerie vitrée claire)", L"Piscina coperta (piscina piastrellata umida, squillo vetroso brillante)", L"Piscina cubierta (piscina alicatada húmeda, timbre vítreo brillante)", L"실내 수영장 (타일과 유리의 습하고 밝은 울림)", L"室内泳池 (瓷砖与玻璃的潮湿明亮回响)", L"مسبح داخلي (مسبح بلاط رطب، رنين زجاجي ساطع)", L"Крытый бассейн (влажный кафельный бассейн, яркий стеклянный звон)", L"Hallenbad (feuchtes Fliesenbecken, heller glasiger Klang)", L"Piscina coberta (piscina azulejada úmida, tinido vítreo brilhante)", L"Binnenbad (vochtig betegeld bad, heldere glasachtige galm)", L"Basen kryty (wilgotny kafelkowy basen, jasny szklany dźwięk)", L"Kapalı havuz (nemli fayans havuz, parlak camımsı tını)"));
	//29
	m_env.AddString(LL14(L"エレベーター (金属箱の密閉した箱鳴り)", L"Elevator (enclosed metal box, short boxy ring)", L"Ascenseur (boîte métallique close, sonnerie courte et caverneuse)", L"Ascensore (scatola metallica chiusa, squillo corto e incassato)", L"Ascensor (caja metálica cerrada, timbre corto y ahuecado)", L"엘리베이터 (금속 상자의 밀폐된 통울림)", L"电梯 (金属箱的密闭箱鸣)", L"مصعد (صندوق معدني مغلق، رنين قصير أجوف)", L"Лифт (закрытый металлический ящик, короткий гулкий звон)", L"Aufzug (geschlossener Metallkasten, kurzer hohler Klang)", L"Elevador (caixa metálica fechada, tinido curto e oco)", L"Lift (gesloten metalen kist, korte holle galm)", L"Winda (zamknięta metalowa skrzynia, krótki pusty dźwięk)", L"Asansör (kapalı metal kutu, kısa oyuk tını)"));
	//30
	m_env.AddString(LL14(L"駐車場 (低天井コンクリートの強い多重反射)", L"Parking garage (low concrete ceiling, strong repeating bounce)", L"Parking (plafond bas en béton, forts échos répétés)", L"Parcheggio (soffitto basso in cemento, forti echi ripetuti)", L"Aparcamiento (techo bajo de hormigón, fuertes ecos repetidos)", L"주차장 (낮은 천장 콘크리트의 강한 플러터 반사)", L"停车场 (低天花混凝土的强烈颤动回声)", L"مرآب (سقف خرساني منخفض، صدى متكرر قوي)", L"Парковка (низкий бетонный потолок, сильное флаттер-эхо)", L"Parkhaus (niedrige Betondecke, starkes Flatterecho)", L"Estacionamento (teto baixo de concreto, fortes ecos repetidos)", L"Parkeergarage (laag betonnen plafond, sterke fladderecho)", L"Parking (niski betonowy sufit, silne echa wielokrotne)", L"Otopark (alçak beton tavan, güçlü çoklu yankı)"));
	m_env.AddString(LL14(L"--[[文化施設 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Établissements culturels 31-40]]--", L"--[[Strutture culturali 31-40]]--", L"--[[Instalaciones culturales 31-40]]--", L"--[[문화시설 31-40]]--", L"--[[文化设施 31-40]]--", L"--[[مرافق ثقافية 31-40]]--", L"--[[Культурные объекты 31-40]]--", L"--[[Kultureinrichtungen 31-40]]--", L"--[[Instalações culturais 31-40]]--", L"--[[Culturele voorzieningen 31-40]]--", L"--[[Obiekty kulturalne 31-40]]--", L"--[[Kültürel tesisler 31-40]]--"), TRUE);
	//31
	m_env.AddString(LL14(L"コンサートホール (木の温かさと豊かな拡散の明瞭な響き)", L"Concert hall (rich warm wood, clear concert reverb)", L"Salle de concert (bois chaud et riche, réverbération de concert claire)", L"Sala da concerto (legno caldo e ricco, riverbero da concerto chiaro)", L"Sala de conciertos (madera cálida y rica, reverberación de concierto clara)", L"콘서트홀 (나무의 따뜻함과 풍부한 확산의 맑은 울림)", L"音乐厅 (木质温暖、丰富扩散的清晰混响)", L"قاعة حفلات (خشب دافئ غني، صدى حفلة واضح)", L"Концертный зал (тёплое богатое дерево, ясный концертный реверб)", L"Konzertsaal (reiches warmes Holz, klarer Konzertnachhall)", L"Sala de concertos (madeira quente e rica, reverberação de concerto clara)", L"Concertzaal (rijk warm hout, heldere concertnagalm)", L"Sala koncertowa (bogate ciepłe drewno, wyraźny koncertowy pogłos)", L"Konser salonu (zengin sıcak ahşap, net konser yankısı)"));
	//32
	m_env.AddString(LL14(L"ジャズクラブ (親密で木の温かい中低域)", L"Jazz club (intimate club, warm woody lows)", L"Club de jazz (club intime, graves chauds et boisés)", L"Jazz club (club intimo, basse calde e legnose)", L"Club de jazz (club íntimo, graves cálidos y maderosos)", L"재즈 클럽 (친밀하고 나무처럼 따뜻한 낮은 음)", L"爵士酒吧 (亲密空间，温暖木质的低音)", L"نادي جاز (نادٍ حميم، أصوات منخفضة دافئة خشبية)", L"Джаз-клуб (интимный клуб, тёплые древесные низы)", L"Jazzclub (intimer Club, warme holzige Tiefen)", L"Clube de jazz (clube íntimo, graves quentes e amadeirados)", L"Jazzclub (intieme club, warme houtachtige lage tonen)", L"Klub jazzowy (kameralny klub, ciepłe drewniane doły)", L"Caz kulübü (samimi kulüp, sıcak odunsu alçaklar)"));
	//33
	m_env.AddString(LL14(L"カラオケボックス (防音個室の柔らかく短い響き)", L"Karaoke box (padded booth, soft and short)", L"Box karaoké (cabine insonorisée, douce et courte)", L"Sala karaoke (cabina imbottita, morbida e breve)", L"Cabina de karaoke (cabina acolchada, suave y corta)", L"노래방 (방음 부스의 부드럽고 짧은 울림)", L"卡拉OK包厢 (隔音小间，柔软短促的回响)", L"غرفة كاريوكي (حجرة مبطنة، رنين ناعم وقصير)", L"Караоке-бокс (обитая кабина, мягко и коротко)", L"Karaoke-Box (gepolsterte Kabine, weich und kurz)", L"Sala de karaokê (cabine acolchoada, macia e curta)", L"Karaokehokje (gewatteerde cabine, zacht en kort)", L"Kabina karaoke (wyciszona kabina, miękko i krótko)", L"Karaoke odası (yalıtımlı kabin, yumuşak ve kısa)"));
	//34
	m_env.AddString(LL14(L"映画館 (吸音された広く制御された残響)", L"Cinema (treated cinema, controlled wide reverb)", L"Cinéma (cinéma traité, large réverbération maîtrisée)", L"Cinema (cinema trattato, ampio riverbero controllato)", L"Cine (cine tratado, reverberación amplia y controlada)", L"영화관 (흡음된 넓고 잘 다스려진 잔향)", L"电影院 (吸音处理后宽阔受控的混响)", L"سينما (سينما معالجة، صدى واسع مضبوط)", L"Кинотеатр (заглушенный кинозал, широкий контролируемый реверб)", L"Kino (gedämmtes Kino, kontrollierter weiter Nachhall)", L"Cinema (cinema tratado, reverberação ampla e controlada)", L"Bioscoop (behandelde bioscoop, beheerste wijde nagalm)", L"Kino (wygłuszone kino, szeroki kontrolowany pogłos)", L"Sinema (yalıtımlı sinema, kontrollü geniş yankı)"));
	//35
	m_env.AddString(LL14(L"地下鉄車内 (金属車体の走行感と低いランブル)", L"Subway car (metal car body, rumble and motion sweep)", L"Rame de métro (caisse métallique, ronflement et balayage du mouvement)", L"Vagone metro (scocca metallica, rombo e strisciata del moto)", L"Vagón de metro (carrocería metálica, retumbo y barrido del movimiento)", L"지하철 차내 (금속 차체의 주행감과 낮은 웅웅거림)", L"地铁车厢 (金属车体的行驶感与低沉隆隆)", L"عربة مترو (هيكل معدني، دوي وكنس الحركة)", L"Вагон метро (металлический вагон, гул и сдвиг движения)", L"U-Bahn-Wagen (Metallwagenkasten, Grollen und Bewegungszug)", L"Vagão de metrô (carroceria metálica, ronco e varredura do movimento)", L"Metrostel (metalen wagonbak, gerommel en bewegingszwaai)", L"Wagon metra (metalowy wagon, huk i przesuw ruchu)", L"Metro vagonu (metal vagon gövdesi, uğultu ve hareket süpürmesi)"));
	//36
	m_env.AddString(LL14(L"空港ターミナル (ガラスと鋼の明るく群衆に吸われる響き)", L"Airport terminal (bright glass and steel, crowd-soaked)", L"Terminal d'aéroport (verre et acier clairs, absorbés par la foule)", L"Terminal aeroportuale (vetro e acciaio brillanti, assorbiti dalla folla)", L"Terminal de aeropuerto (vidrio y acero brillantes, absorbidos por la muchedumbre)", L"공항 터미널 (유리와 강의 밝고 군중에 흡수되는 울림)", L"机场航站楼 (玻璃与钢的明亮回响，被人群吸收)", L"صالة مطار (زجاج وفولاذ ساطعان، يمتصهما الجمهور)", L"Терминал аэропорта (яркое стекло и сталь, впитанные толпой)", L"Flughafenterminal (helles Glas und Stahl, von der Menge geschluckt)", L"Terminal de aeroporto (vidro e aço brilhantes, absorvidos pela multidão)", L"Luchthaventerminal (helder glas en staal, opgezogen door de menigte)", L"Terminal lotniska (jasne szkło i stal, pochłaniane przez tłum)", L"Havalimanı terminali (parlak cam ve çelik, kalabalığın emdiği)"));
	//37
	m_env.AddString(LL14(L"ショッピングモール (吹き抜けガラスと群衆に吸われる響き)", L"Shopping mall (atrium glass, crowd-soaked mall reverb)", L"Centre commercial (atrium vitré, réverbération absorbée par la foule)", L"Centro commerciale (atrio di vetro, riverbero assorbito dalla folla)", L"Centro comercial (atrio de cristal, reverberación absorbida por la muchedumbre)", L"쇼핑몰 (유리 아트리움과 군중에 흡수되는 울림)", L"购物中心 (中庭玻璃与被人群吸收的商场混响)", L"مركز تسوق (ردهة زجاجية، صدى يمتصه الجمهور)", L"Торговый центр (стеклянный атриум, реверб, впитанный толпой)", L"Einkaufszentrum (Glasatrium, von der Menge geschluckter Nachhall)", L"Shopping center (átrio de vidro, reverberação absorvida pela multidão)", L"Winkelcentrum (glazen atrium, nagalm die de menigte opvangt)", L"Centrum handlowe (szklane atrium, pogłos pochłaniany przez tłum)", L"Alışveriş merkezi (cam atrium, kalabalığın emdiği AVM yankısı)"));
	//38
	m_env.AddString(LL14(L"病院 (清潔な硬い床の明るく無機質な響き)", L"Hospital (clean hard floors, bright clinical ring)", L"Hôpital (sols durs et propres, sonnerie clinique claire)", L"Ospedale (pavimenti duri e puliti, squillo clinico brillante)", L"Hospital (suelos duros y limpios, timbre clínico brillante)", L"병원 (깨끗한 단단한 바닥의 밝고 무기질적인 울림)", L"医院 (清洁硬地，明亮冷淡的回响)", L"مستشفى (أرضيات صلبة نظيفة، رنين سريري ساطع)", L"Больница (чистые жёсткие полы, яркий клинический звон)", L"Krankenhaus (saubere harte Böden, heller klinischer Klang)", L"Hospital (pisos duros e limpos, tinido clínico brilhante)", L"Ziekenhuis (schone harde vloeren, heldere klinische galm)", L"Szpital (czyste twarde podłogi, jasny kliniczny dźwięk)", L"Hastane (temiz sert zeminler, parlak klinik tını)"));
	//39
	m_env.AddString(LL14(L"レコーディングブース (全面吸音の極デッドな空間)", L"Recording booth (fully treated booth, extremely dead)", L"Cabine d'enregistrement (cabine entièrement traitée, extrêmement morte)", L"Cabina di registrazione (cabina interamente trattata, estremamente spenta)", L"Cabina de grabación (cabina totalmente tratada, extremadamente apagada)", L"녹음 부스 (전면 흡음의 극도로 죽은 공간)", L"录音室隔间 (全面吸音、极度干死的空间)", L"غرفة تسجيل (حجرة معالجة بالكامل، ميتة جداً)", L"Кабина записи (полностью заглушенная кабина, крайне сухая)", L"Aufnahmekabine (vollständig gedämmte Kabine, extrem tot)", L"Cabine de gravação (cabine totalmente tratada, extremamente morta)", L"Opnamecabine (volledig behandelde cabine, extreem dood)", L"Kabina nagraniowa (w pełni wygłuszona kabina, skrajnie martwa)", L"Kayıt kabini (tam yalıtımlı kabin, son derece ölü)"));
	//40
	m_env.AddString(LL14(L"オペラハウス (装飾木造の温かく豊麗な拡散)", L"Opera house (ornate wood, warm lush reverb)", L"Opéra (bois orné, réverbération chaude et somptueuse)", L"Teatro dell'opera (legno ornato, riverbero caldo e sontuoso)", L"Teatro de ópera (madera ornamentada, reverberación cálida y suntuosa)", L"오페라 하우스 (장식된 목조의 따뜻하고 화려한 퍼짐)", L"歌剧院 (装饰木造，温暖华美的扩散)", L"دار الأوبرا (خشب مزخرف، صدى دافئ فاخر)", L"Оперный театр (резное дерево, тёплый пышный реверб)", L"Opernhaus (verziertes Holz, warmer üppiger Nachhall)", L"Casa de ópera (madeira ornamentada, reverberação quente e suntuosa)", L"Operahuis (versierd hout, warme weelderige nagalm)", L"Opera (ozdobne drewno, ciepły bujny pogłos)", L"Opera binası (süslü ahşap, sıcak görkemli yankı)"));
	m_env.AddString(LL14(L"--[[生活空間 41-50]]--", L"--[[Living 41-50]]--", L"--[[Espace de vie 41-50]]--", L"--[[Spazio abitativo 41-50]]--", L"--[[Espacio cotidiano 41-50]]--", L"--[[생활 공간 41-50]]--", L"--[[生活空间 41-50]]--", L"--[[مساحة معيشة 41-50]]--", L"--[[Бытовое пространство 41-50]]--", L"--[[Wohnraum 41-50]]--", L"--[[Espaço de convivência 41-50]]--", L"--[[Leefruimte 41-50]]--", L"--[[Przestrzeń domowa 41-50]]--", L"--[[Yaşam alanı 41-50]]--"), TRUE);
	//41
	m_env.AddString(LL14(L"喫茶店/カフェ (木と布の温かく居心地良い響き)", L"Cafe (wood and fabric, warm and cozy)", L"Café (bois et tissu, chaud et confortable)", L"Caffè (legno e tessuto, caldo e accogliente)", L"Cafetería (madera y tela, cálido y acogedor)", L"카페 (나무와 천의 따뜻하고 아늑한 울림)", L"咖啡馆 (木与布，温暖惬意的回响)", L"مقهى (خشب وقماش، دافئ ومريح)", L"Кафе (дерево и ткань, тепло и уютно)", L"Café (Holz und Stoff, warm und gemütlich)", L"Café (madeira e tecido, quente e aconchegante)", L"Café (hout en stof, warm en knus)", L"Kawiarnia (drewno i tkanina, ciepło i przytulnie)", L"Kafe (ahşap ve kumaş, sıcak ve rahat)"));
	//42
	m_env.AddString(LL14(L"バー/ラウンジ (革と木の暗く落ち着いた低域)", L"Bar/Lounge (leather and wood, dark mellow lows)", L"Bar/Salon (cuir et bois, graves sombres et doux)", L"Bar/Lounge (pelle e legno, basse scure e morbide)", L"Bar/Salón (cuero y madera, graves oscuros y suaves)", L"바/라운지 (가죽과 나무의 어둡고 차분한 낮은 음)", L"酒吧/休息室 (皮革与木，暗沉柔和的低音)", L"بار/صالة (جلد وخشب، أصوات منخفضة داكنة ناعمة)", L"Бар/Лаундж (кожа и дерево, тёмные мягкие низы)", L"Bar/Lounge (Leder und Holz, dunkle sanfte Tiefen)", L"Bar/Lounge (couro e madeira, graves escuros e macios)", L"Bar/Lounge (leer en hout, donkere zachte lage tonen)", L"Bar/Lounge (skóra i drewno, ciemne łagodne doły)", L"Bar/Salon (deri ve ahşap, koyu yumuşak alçaklar)"));
	//43
	m_env.AddString(LL14(L"居酒屋 (木材主体の明るく活気ある響き)", L"Izakaya (wood-dominant, bright and lively)", L"Izakaya (taverne) (surtout du bois, clair et animé)", L"Izakaya (prevalenza di legno, brillante e vivace)", L"Izakaya (taberna) (predominio de madera, brillante y animado)", L"이자카야 (목재 위주의 밝고 활기찬 울림)", L"居酒屋 (以木材为主，明亮活泼的回响)", L"إيزاكايا (خشب غالب، ساطع وحيوي)", L"Идзакая (в основном дерево, ярко и живо)", L"Izakaya (Kneipe) (überwiegend Holz, hell und lebendig)", L"Izakaya (taberna) (predominância de madeira, brilhante e animado)", L"Izakaya (vooral hout, helder en levendig)", L"Izakaya (głównie drewno, jasno i żywo)", L"Izakaya (ağırlıklı ahşap, parlak ve canlı)"));
	//44
	m_env.AddString(LL14(L"美術館/博物館 (石とガラスの静かで高い空間)", L"Museum (stone and glass, quiet tall gallery)", L"Musée (pierre et verre, galerie haute et calme)", L"Museo (pietra e vetro, galleria alta e quieta)", L"Museo (piedra y vidrio, galería alta y silenciosa)", L"미술관/박물관 (돌과 유리의 조용하고 높은 공간)", L"美术馆/博物馆 (石与玻璃，安静高挑的展厅)", L"متحف (حجر وزجاج، رواق هادئ مرتفع)", L"Музей (камень и стекло, тихая высокая галерея)", L"Museum (Stein und Glas, stille hohe Galerie)", L"Museu (pedra e vidro, galeria alta e silenciosa)", L"Museum (steen en glas, stille hoge galerij)", L"Muzeum (kamień i szkło, cicha wysoka galeria)", L"Müze (taş ve cam, sessiz yüksek galeri)"));
	//45
	m_env.AddString(LL14(L"講堂/大学教室 (話が通る吸音と木の温かみ)", L"Auditorium/Lecture hall (speech-clear hall, absorbed warm wood)", L"Amphithéâtre (salle claire pour la parole, bois chaud absorbé)", L"Aula magna (sala chiara per la voce, legno caldo assorbito)", L"Auditorio/Aula (sala clara para el habla, madera cálida absorbida)", L"강당/대학 강의실 (말이 통하는 흡음과 나무의 따뜻함)", L"礼堂/大学教室 (利于说话的吸音与木质温暖)", L"قاعة محاضرات (قاعة واضحة للكلام، خشب دافئ ممتص)", L"Аудитория (зал с ясной речью, тёплое поглощённое дерево)", L"Hörsaal (sprachklarer Saal, warmes geschlucktes Holz)", L"Auditório/Sala de aula (sala clara para a fala, madeira quente absorvida)", L"Aula/Collegezaal (spraakheldere zaal, warm opgenomen hout)", L"Aula/Sala wykładowa (sala wyraźna dla mowy, ciepłe pochłonięte drewno)", L"Amfi/Ders salonu (konuşmanın geçtiği salon, emilmiş sıcak ahşap)"));
	//46
	m_env.AddString(LL14(L"竹林 (竹の中空な拡散とそよぐ風)", L"Bamboo grove (hollow bamboo diffusion, a light breeze)", L"Bambouseraie (diffusion creuse du bambou, une brise légère)", L"Bambuseto (diffusione cava del bambù, una brezza lieve)", L"Bosque de bambú (difusión hueca del bambú, una brisa ligera)", L"대나무 숲 (대나무의 속이 빈 퍼짐과 살랑이는 바람)", L"竹林 (竹林中空的扩散与轻风)", L"غابة الخيزران (انتشار أجوف من الخيزران، نسمة خفيفة)", L"Бамбуковая роща (полое бамбуковое рассеяние, лёгкий ветер)", L"Bambushain (hohle Bambusstreuung, eine leichte Brise)", L"Bambuzal (difusão oca do bambu, uma brisa leve)", L"Bamboebos (holle bamboediffusie, een lichte bries)", L"Gaj bambusowy (puste rozproszenie bambusa, lekka bryza)", L"Bambu ormanı (oyuk bambu yayılımı, hafif bir esinti)"));
	//47
	m_env.AddString(LL14(L"渓谷/滝 (濡れ岩から返る湿った水飛沫のエコー)", L"Gorge/Waterfall (wet-rock echoes, humid spray)", L"Gorge/Cascade (échos de roche mouillée, embruns humides)", L"Gola/Cascata (echi di roccia bagnata, spruzzi umidi)", L"Desfiladero/Cascada (ecos de roca mojada, rocio húmedo)", L"협곡/폭포 (젖은 바위에서 돌아오는 습한 물보라 메아리)", L"溪谷/瀑布 (湿岩折返、带水雾的潮湿回声)", L"وادٍ/شلال (صدى صخر مبلل، رذاذ رطب)", L"Ущелье/Водопад (эхо мокрых скал, влажные брызги)", L"Schlucht/Wasserfall (Nasse-Fels-Echos, feuchter Sprühnebel)", L"Desfiladeiro/Cachoeira (ecos de rocha molhada, spray úmido)", L"Kloof/Waterval (echo's van nat gesteente, vochtige nevel)", L"Wąwóz/Wodospad (echa mokrych skał, wilgotna mgiełka)", L"Boğaz/Şelale (ıslak kaya yankıları, nemli sprey)"));
	//48
	m_env.AddString(LL14(L"砂漠 (乾いた開放空間、風と空気に吸われる)", L"Desert (open dry desert, wind and air swallow sound)", L"Désert (désert ouvert et sec, le vent et l'air avalent le son)", L"Deserto (deserto aperto e asciutto, vento e aria inghiottono il suono)", L"Desierto (desierto abierto y seco, el viento y el aire tragan el sonido)", L"사막 (메마른 개방 공간, 바람과 공기가 소리를 삼킴)", L"沙漠 (干燥开阔的沙漠，风与空气吞没声音)", L"صحراء (صحراء مفتوحة جافة، الريح والهواء يبتلعان الصوت)", L"Пустыня (открытая сухая пустыня, ветер и воздух глотают звук)", L"Wüste (offene trockene Wüste, Wind und Luft schlucken den Klang)", L"Deserto (deserto aberto e seco, vento e ar engolem o som)", L"Woestijn (open droge woestijn, wind en lucht slokken het geluid)", L"Pustynia (otwarta sucha pustynia, wiatr i powietrze pochłaniają dźwięk)", L"Çöl (açık kuru çöl, rüzgâr ve hava sesi yutar)"));
	//49
	m_env.AddString(LL14(L"ガレージ (コンクリートと金属の多重反射)", L"Garage (concrete and metal, repeating bounce)", L"Garage (béton et métal, échos répétés)", L"Garage (cemento e metallo, echi ripetuti)", L"Garaje (hormigón y metal, ecos repetidos)", L"차고 (콘크리트와 금속의 플러터 반사)", L"车库 (混凝土与金属的颤动回声)", L"كراج (خرسانة ومعدن، صدى متكرر)", L"Гараж (бетон и металл, флаттер-эхо)", L"Garage (Beton und Metall, Flatterecho)", L"Garagem (concreto e metal, ecos repetidos)", L"Garage (beton en metaal, fladderecho)", L"Garaż (beton i metal, echa wielokrotne)", L"Garaj (beton ve metal, çoklu yankı)"));
	//50
	m_env.AddString(LL14(L"展望台 (高所の開放感と強い風)", L"Observation deck (high open deck, strong wind)", L"Belvédère (terrasse haute et ouverte, vent fort)", L"Terrazza panoramica (terrazza alta e aperta, vento forte)", L"Mirador (mirador alto y abierto, viento fuerte)", L"전망대 (높은 곳의 개방감과 강한 바람)", L"观景台 (高处的开阔感与强风)", L"منصة مراقبة (منصة مرتفعة مفتوحة، ريح قوية)", L"Смотровая площадка (высокая открытая площадка, сильный ветер)", L"Aussichtsplattform (hohe offene Plattform, starker Wind)", L"Mirante (deck alto e aberto, vento forte)", L"Uitkijkplatform (hoog open dek, sterke wind)", L"Taras widokowy (wysoki otwarty taras, silny wiatr)", L"Seyir terası (yüksek açık teras, kuvvetli rüzgâr)"));
	m_env.AddString(LL14(L"--[[拡張空間 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Espace étendu 51-60]]--", L"--[[Spazio esteso 51-60]]--", L"--[[Espacio ampliado 51-60]]--", L"--[[확장 공간 51-60]]--", L"--[[扩展空间 51-60]]--", L"--[[مساحة موسعة 51-60]]--", L"--[[Расширенное пространство 51-60]]--", L"--[[Erweiterter Raum 51-60]]--", L"--[[Espaço ampliado 51-60]]--", L"--[[Uitgebreide ruimte 51-60]]--", L"--[[Przestrzeń rozszerzona 51-60]]--", L"--[[Genişletilmiş alan 51-60]]--"), TRUE);
	//51
	m_env.AddString(LL14(L"小さな礼拝堂 (石と木の小さく温かい礼拝堂)", L"Small chapel (stone and wood, smaller warmer chapel)", L"Petite chapelle (pierre et bois, chapelle plus petite et plus chaude)", L"Piccola cappella (pietra e legno, cappella più piccola e più calda)", L"Capilla pequeña (piedra y madera, capilla más pequeña y cálida)", L"작은 예배당 (돌과 나무의 작고 따뜻한 예배당)", L"小礼拜堂 (石与木，更小更暖的礼拜堂)", L"كنيسة صغيرة (حجر وخشب، مصلى أصغر وأدفأ)", L"Маленькая часовня (камень и дерево, часовня меньше и теплее)", L"Kleine Kapelle (Stein und Holz, kleinere wärmere Kapelle)", L"Pequena capela (pedra e madeira, capela menor e mais quente)", L"Kleine kapel (steen en hout, kleinere warmere kapel)", L"Mała kaplica (kamień i drewno, mniejsza cieplejsza kaplica)", L"Küçük şapel (taş ve ahşap, daha küçük daha sıcak şapel)"));
	//52
	m_env.AddString(LL14(L"大型ショッピングセンター (巨大吹き抜けの複数階に広がる残響)", L"Large shopping center (giant atrium, reverb across many floors)", L"Grand centre commercial (immense atrium, réverbération sur plusieurs étages)", L"Grande centro commerciale (enorme atrio, riverbero su più piani)", L"Gran centro comercial (atrio gigante, reverberación por varios pisos)", L"대형 쇼핑센터 (거대한 아트리움이 여러 층으로 퍼지는 잔향)", L"大型购物中心 (巨大中庭跨多层扩散的混响)", L"مركز تسوق كبير (ردهة هائلة، صدى عبر طوابق عدة)", L"Крупный ТЦ (огромный атриум, реверб на много этажей)", L"Großes Einkaufszentrum (riesiges Atrium, Nachhall über viele Stockwerke)", L"Grande shopping (átrio gigante, reverberação por vários andares)", L"Groot winkelcentrum (reusachtig atrium, nagalm over vele verdiepingen)", L"Duże centrum handlowe (ogromne atrium, pogłos przez wiele pięter)", L"Büyük AVM (dev atrium, birçok kata yayılan yankı)"));
	//53
	m_env.AddString(LL14(L"地下洞窟(深層) (深く広大で重い低域のこもった残響)", L"Deep cave (deep vast cave, heavy muffled lows)", L"Grotte profonde (grotte profonde et vaste, graves lourds et étouffés)", L"Grotta profonda (grotta profonda e vasta, basse pesanti e ovattate)", L"Cueva profunda (cueva profunda y vasta, graves pesados y apagados)", L"심층 지하 동굴 (깊고 광활하며 무거운 저음이 먹먹한 잔향)", L"深层地下洞窟 (深广洞穴，沉重闷浊的低音混响)", L"كهف عميق (كهف عميق شاسع، أصوات منخفضة ثقيلة مكتومة)", L"Глубокая пещера (глубокая обширная пещера, тяжёлые глухие низы)", L"Tiefe Höhle (tiefe weite Höhle, schwere dumpfe Tiefen)", L"Caverna profunda (caverna profunda e vasta, graves pesados e abafados)", L"Diepe grot (diepe uitgestrekte grot, zware gedempte lage tonen)", L"Głęboka jaskinia (głęboka rozległa jaskinia, ciężkie stłumione doły)", L"Derin mağara (derin uçsuz mağara, ağır boğuk alçaklar)"));
	//54
	m_env.AddString(LL14(L"古城の大広間 (石壁と木梁の重厚な高天井)", L"Castle great hall (stone and timber, grand tall hall)", L"Grande salle de château (pierre et bois, grande salle haute et massive)", L"Grande sala del castello (pietra e travi, grande sala alta e solenne)", L"Gran salón del castillo (piedra y vigas, gran sala alta y solemne)", L"고성의 대연회장 (돌벽과 나무보의 중후한 높은 천장)", L"古堡大厅 (石壁与木梁，厚重高挑的大厅)", L"قاعة القلعة الكبرى (جدران حجرية وعوارض خشبية، قاعة شاهقة فخمة)", L"Замковый зал (камень и балки, величественный высокий зал)", L"Schlosssaal (Stein und Holzbalken, prächtiger hoher Saal)", L"Grande salão do castelo (pedra e vigas, grande salão alto e solene)", L"Kasteelzaal (steen en houtbalken, plechtige hoge zaal)", L"Wielka sala zamkowa (kamień i belki, wspaniała wysoka sala)", L"Kale büyük salonu (taş ve ahşap kiriş, görkemli yüksek salon)"));
	//55
	m_env.AddString(LL14(L"野外音楽堂 (反響板に集まる開放的な木の温かみ)", L"Outdoor bandshell (outdoor shell, open warm wood focus)", L"Kiosque à musique (coquille extérieure, bois chaud ouvert et concentré)", L"Palco all'aperto (conchiglia all'aperto, legno caldo aperto e raccolto)", L"Concha acústica (concha al aire libre, madera cálida abierta y concentrada)", L"야외 음악당 (반사판에 모이는 개방적인 나무의 따뜻함)", L"露天音乐台 (汇聚于反射板的开阔木质温暖)", L"صدفة موسيقية خارجية (صدفة خارجية، خشب دافئ مفتوح ومركز)", L"Летняя эстрада (открытая раковина, тёплое дерево в фокусе)", L"Freilichtbühne (Freiluftmuschel, offenes warmes Holz im Fokus)", L"Concha acústica (concha ao ar livre, madeira quente aberta e focada)", L"Openluchtpodium (buitenschelp, open warm hout in focus)", L"Muszla koncertowa (muszla na zewnątrz, otwarte ciepłe drewno w skupieniu)", L"Açık hava sahnesi (açık kabuk, odakta ferah sıcak ahşap)"));
	//56
	m_env.AddString(LL14(L"鍾乳洞 (鍾乳石と水滴の湿った洞窟)", L"Limestone cave (stalactites and drips, damp cave reverb)", L"Grotte calcaire (stalactites et gouttes, réverbération de grotte humide)", L"Grotta calcarea (stalattiti e gocce, riverbero di grotta umida)", L"Cueva de piedra caliza (estalactitas y goteos, reverberación de cueva húmeda)", L"석회 동굴 (종유석과 물방울의 습한 동굴)", L"钟乳石洞 (钟乳石与水滴的潮湿洞穴)", L"كهف جيري (هوابط وقطر، صدى كهف رطب)", L"Известняковая пещера (сталактиты и капли, влажный пещерный реверб)", L"Tropfsteinhöhle (Stalaktiten und Tropfen, feuchter Höhlennachhall)", L"Caverna calcária (estalactites e pingos, reverberação de caverna úmida)", L"Kalksteengrot (stalactieten en druppels, vochtige grotnagalm)", L"Jaskinia wapienna (stalaktyty i krople, wilgotny jaskiniowy pogłos)", L"Kireçtaşı mağarası (sarkıtlar ve damlalar, nemli mağara yankısı)"));
	//57
	m_env.AddString(LL14(L"廃墟工場 (錆びた金属と風の荒れた響き)", L"Ruined factory (rusted metal, wind and gritty ring)", L"Usine en ruine (métal rouillé, vent et sonnerie râpeuse)", L"Fabbrica in rovina (metallo arrugginito, vento e squillo ruvido)", L"Fábrica en ruinas (metal oxidado, viento y timbre áspero)", L"폐허 공장 (녹슨 금속과 바람의 거친 울림)", L"废弃工厂 (锈蚀金属与风的粗糙回响)", L"مصنع مهجور (معدن صدئ، ريح ورنين خشن)", L"Заброшенный завод (ржавый металл, ветер и шершавый звон)", L"Ruinenfabrik (rostiges Metall, Wind und rauer Klang)", L"Fábrica em ruínas (metal enferrujado, vento e tinido áspero)", L"Vervallen fabriek (verroest metaal, wind en ruwe galm)", L"Zrujnowana fabryka (zardzewiały metal, wiatr i szorstki dźwięk)", L"Harap fabrika (paslı metal, rüzgâr ve pürüzlü tını)"));
	//58
	m_env.AddString(LL14(L"和室(畳) (畳と障子の柔らかく短い空間)", L"Tatami room (tatami and shoji, soft and dead)", L"Pièce à tatami (tatami et shōji, doux et mort)", L"Stanza con tatami (tatami e shōji, morbido e spento)", L"Habitación de tatami (tatami y shoji, suave y apagado)", L"다다미 방 (다다미와 장지의 부드럽고 죽은 공간)", L"榻榻米房间 (榻榻米与障子，柔软短促的空间)", L"غرفة تاتامي (تاتامي وشوجي، ناعم وميت)", L"Комната с татами (татами и сёдзи, мягко и глухо)", L"Tatami-Zimmer (Tatami und Shoji, weich und tot)", L"Sala de tatame (tatami e shoji, macio e morto)", L"Tatamikamer (tatami en shoji, zacht en dood)", L"Pokój z tatami (tatami i shōji, miękko i martwo)", L"Tatami odası (tatami ve shoji, yumuşak ve ölü)"));
	//59
	m_env.AddString(LL14(L"温泉施設 (岩と木の湿って温かい響き)", L"Hot spring (rock and wood, very humid and warm)", L"Source thermale (roche et bois, très humide et chaud)", L"Terme (roccia e legno, molto umido e caldo)", L"Aguas termales (roca y madera, muy húmedo y cálido)", L"온천 시설 (바위와 나무의 습하고 따뜻한 울림)", L"温泉设施 (岩与木，潮湿温暖的回响)", L"منتجع مياه ساخنة (صخر وخشب، رطب جداً ودافئ)", L"Горячие источники (камень и дерево, очень влажно и тепло)", L"Thermalbad (Fels und Holz, sehr feucht und warm)", L"Fonte termal (rocha e madeira, muito úmido e quente)", L"Warmwaterbron (rots en hout, zeer vochtig en warm)", L"Gorące źródła (skała i drewno, bardzo wilgotno i ciepło)", L"Kaplıca (kaya ve ahşap, çok nemli ve sıcak)"));
	//60
	m_env.AddString(LL14(L"屋根裏部屋 (傾斜した木天井の埃っぽくこもる響き)", L"Attic (sloped wood attic, dusty and muffled)", L"Grenier (grenier de bois en pente, poussiéreux et étouffé)", L"Soffitta (soffitta di legno inclinata, polverosa e ovattata)", L"Ático (ático de madera inclinado, polvoriento y apagado)", L"다락방 (경사진 나무 천장의 먼지 나고 먹먹한 울림)", L"阁楼 (倾斜木天花，尘土沉闷的回响)", L"علية (علية خشبية مائلة، مغبرة ومكتومة)", L"Чердак (наклонный деревянный чердак, пыльный и глухой)", L"Dachboden (schräger Holzboden, staubig und dumpf)", L"Sótão (sótão de madeira inclinado, empoeirado e abafado)", L"Zolder (schuin houten zolder, stoffig en gedempt)", L"Strych (pochyły drewniany strych, zakurzony i stłumiony)", L"Çatı katı (eğik ahşap tavan arası, tozlu ve boğuk)"));
	m_env.AddString(LL14(L"--[[特殊空間 61-70]]--", L"--[[Special 61-70]]--", L"--[[Espace spécial 61-70]]--", L"--[[Spazio speciale 61-70]]--", L"--[[Espacio especial 61-70]]--", L"--[[특수 공간 61-70]]--", L"--[[特殊空间 61-70]]--", L"--[[مساحة خاصة 61-70]]--", L"--[[Особое пространство 61-70]]--", L"--[[Spezialraum 61-70]]--", L"--[[Espaço especial 61-70]]--", L"--[[Speciale ruimte 61-70]]--", L"--[[Przestrzeń specjalna 61-70]]--", L"--[[Özel alan 61-70]]--"), TRUE);
	//61
	m_env.AddString(LL14(L"地下駐車場(多層) (多層コンクリートの強い多重反射)", L"Underground parking (multi-level) (multi-level concrete, strong repeating bounce)", L"Parking souterrain à niveaux (béton à plusieurs niveaux, forts échos répétés)", L"Parcheggio sotterraneo a più livelli (cemento su più livelli, forti echi ripetuti)", L"Aparcamiento subterráneo de varios niveles (hormigón de varios niveles, fuertes ecos repetidos)", L"다층 지하주차장 (여러 층의 콘크리트, 강한 플러터 반사)", L"多层地下停车场 (多层混凝土的强烈颤动回声)", L"مرآب سفلي متعدد الطوابق (خرسانة متعددة الطوابق، صدى متكرر قوي)", L"Многоуровневая подземная парковка (многоуровневый бетон, сильное флаттер-эхо)", L"Mehrstöckige Tiefgarage (mehrstöckiger Beton, starkes Flatterecho)", L"Estacionamento subterrâneo de vários níveis (concreto em vários níveis, fortes ecos repetidos)", L"Ondergrondse parkeergarage (meerlaags) (beton op meerdere niveaus, sterke fladderecho)", L"Wielopoziomowy parking podziemny (wielopoziomowy beton, silne echa wielokrotne)", L"Çok katlı yeraltı otoparkı (çok katlı beton, güçlü çoklu yankı)"));
	//62
	m_env.AddString(LL14(L"古い劇場(木造) (木材が鳴る温かい古い劇場)", L"Old wooden theater (old wood theater, resonant timber)", L"Vieux théâtre en bois (vieux théâtre de bois, bois résonnant)", L"Vecchio teatro in legno (vecchio teatro di legno, legno risonante)", L"Antiguo teatro de madera (viejo teatro de madera, madera resonante)", L"오래된 목조 극장 (나무가 울리는 따뜻한 오래된 극장)", L"老式木造剧院 (木材共鸣的温暖旧剧场)", L"مسرح خشبي قديم (مسرح خشبي قديم، خشب رنان)", L"Старый деревянный театр (старый деревянный театр, резонирующий лес)", L"Altes Holztheater (altes Holztheater, mitschwingendes Holz)", L"Antigo teatro de madeira (velho teatro de madeira, madeira ressonante)", L"Oud houten theater (oud houten theater, meetrillend hout)", L"Stary drewniany teatr (stary drewniany teatr, rezonujące drewno)", L"Eski ahşap tiyatro (eski ahşap tiyatro, rezonanslı kereste)"));
	//63
	m_env.AddString(LL14(L"大型倉庫(空) (巨大で空虚な鉄骨の遠く遅れて返る響き)", L"Empty large warehouse (huge empty warehouse, steel and distant bounce)", L"Grand entrepôt vide (énorme entrepôt vide, acier et rebond lointain)", L"Grande magazzino vuoto (enorme magazzino vuoto, acciaio e rimbalzo lontano)", L"Gran almacén vacío (enorme almacén vacío, acero y rebote lejano)", L"텅 빈 대형 창고 (거대하고 텅 빈 철골이 멀리서 늦게 돌아오는 울림)", L"空旷大型仓库 (巨大空仓库，钢骨与远处迟回)", L"مستودع كبير فارغ (مستودع فارغ هائل، فولاذ وارتداد بعيد)", L"Пустой большой склад (огромный пустой склад, сталь и далёкий отзвук)", L"Leere große Lagerhalle (riesige leere Halle, Stahl und ferner Rückwurf)", L"Grande armazém vazio (enorme armazém vazio, aço e ricochete distante)", L"Leeg groot magazijn (enorm leeg magazijn, staal en verre kaats)", L"Pusty duży magazyn (ogromny pusty magazyn, stal i odległe odbicie)", L"Boş büyük depo (devasa boş depo, çelik ve uzak sekme)"));
	//64
	m_env.AddString(LL14(L"小さな教会 (石と木、礼拝堂と教会の中間)", L"Small church (stone and wood, between chapel and church)", L"Petite église (pierre et bois, entre chapelle et église)", L"Piccola chiesa (pietra e legno, tra cappella e chiesa)", L"Iglesia pequeña (piedra y madera, entre capilla e iglesia)", L"작은 교회 (돌과 나무, 예배당과 교회의 중간)", L"小教堂 (石与木，介于礼拜堂与教堂之间)", L"كنيسة صغيرة (حجر وخشب، بين المصلى والكنيسة)", L"Небольшая церковь (камень и дерево, между часовней и церковью)", L"Kleine Kirche (Stein und Holz, zwischen Kapelle und Kirche)", L"Pequena igreja (pedra e madeira, entre capela e igreja)", L"Kerkje (steen en hout, tussen kapel en kerk)", L"Mały kościół (kamień i drewno, między kaplicą a kościołem)", L"Küçük kilise (taş ve ahşap, şapel ile kilise arasında)"));
	//65
	m_env.AddString(LL14(L"ガラス温室 (全面ガラスの湿った明るい響き)", L"Glass greenhouse (all-glass greenhouse, humid bright ring)", L"Serre en verre (serre tout en verre, sonnerie humide et claire)", L"Serra di vetro (serra tutta di vetro, squillo umido e brillante)", L"Invernadero de vidrio (invernadero todo de cristal, timbre húmedo y brillante)", L"유리 온실 (온통 유리의 습하고 밝은 울림)", L"玻璃温室 (全玻璃温室，潮湿明亮的回响)", L"دفيئة زجاجية (دفيئة كلها زجاج، رنين رطب ساطع)", L"Стеклянная оранжерея (стеклянная теплица, влажный яркий звон)", L"Glasgewächshaus (Ganzglasgewächshaus, feuchter heller Klang)", L"Estufa de vidro (estufa toda de vidro, tinido úmido e brilhante)", L"Glazen kas (kas van enkel glas, vochtige heldere galm)", L"Szklarnia szklana (szklarnia ze szkła, wilgotny jasny dźwięk)", L"Cam sera (tam cam sera, nemli parlak tını)"));
	//66
	m_env.AddString(LL14(L"石造りトンネル (粗い石壁の湿った多重反射)", L"Stone tunnel (rough stone tunnel, damp repeating echoes)", L"Tunnel en pierre (tunnel de pierre brute, échos répétés humides)", L"Tunnel di pietra (tunnel di pietra grezza, echi ripetuti umidi)", L"Túnel de piedra (túnel de piedra tosca, ecos repetidos húmedos)", L"석조 터널 (거친 돌벽의 습한 플러터 반사)", L"石造隧道 (粗糙石壁的潮湿颤动回声)", L"نفق حجري (نفق حجر خشن، صدى متكرر رطب)", L"Каменный тоннель (грубый каменный тоннель, влажное флаттер-эхо)", L"Steintunnel (rauer Steintunnel, feuchtes Flatterecho)", L"Túnel de pedra (túnel de pedra bruta, ecos repetidos úmidos)", L"Stenen tunnel (ruwe stenen tunnel, vochtige fladderecho)", L"Kamienny tunel (surowy kamienny tunel, wilgotne echa wielokrotne)", L"Taş tünel (kaba taş tünel, nemli çoklu yankı)"));
	//67
	m_env.AddString(LL14(L"コンクリート階段 (剥き出しコンクリートの硬い縦反射)", L"Concrete stairwell (bare concrete stairs, hard vertical bounce)", L"Escalier en béton (escalier de béton nu, rebond vertical dur)", L"Scala in cemento (scale di cemento nudo, rimbalzo verticale duro)", L"Escalera de hormigón (escalera de hormigón desnudo, rebote vertical duro)", L"콘크리트 계단 (드러난 콘크리트의 단단한 세로 반사)", L"混凝土楼梯 (裸露混凝土楼梯，坚硬的纵向反射)", L"درج خرساني (درج خرساني مكشوف، ارتداد عمودي صلب)", L"Бетонная лестница (голый бетонный лестничный пролёт, жёсткий вертикальный отскок)", L"Betontreppe (nackte Betontreppe, harter senkrechter Rückwurf)", L"Escada de concreto (escada de concreto nu, ricochete vertical duro)", L"Betonnen trap (kale betonnen trap, harde verticale kaats)", L"Betonowe schody (gołe betonowe schody, twarde pionowe odbicie)", L"Beton merdiven (çıplak beton merdiven, sert dikey sekme)"));
	//68
	m_env.AddString(LL14(L"大浴場 (広いタイルの湿った明るい響き)", L"Large public bath (large tiled bath, humid bright ring)", L"Grand bain public (grand bain carrelé, sonnerie humide et claire)", L"Grandi terme pubbliche (grande bagno piastrellato, squillo umido e brillante)", L"Gran baño público (gran baño alicatado, timbre húmedo y brillante)", L"대욕장 (넓은 타일의 습하고 밝은 울림)", L"大浴场 (宽阔瓷砖浴场，潮湿明亮的回响)", L"حمام عام كبير (حمام بلاط واسع، رنين رطب ساطع)", L"Большая общественная баня (большая кафельная баня, влажный яркий звон)", L"Große öffentliche Badehalle (großes Fliesenbad, feuchter heller Klang)", L"Grande banho público (grande banho azulejado, tinido úmido e brilhante)", L"Groot badhuis (groot betegeld bad, vochtige heldere galm)", L"Duża łaźnia publiczna (duża kafelkowa łaźnia, wilgotny jasny dźwięk)", L"Büyük genel hamam (geniş fayans hamam, nemli parlak tını)"));
	//69
	m_env.AddString(LL14(L"洗面所 (狭いタイルの明るく密閉した響き)", L"Washroom (small tiled washroom, bright enclosed ring)", L"Cabinet de toilette (petit cabinet carrelé, sonnerie claire et close)", L"Bagno di servizio (piccolo bagno piastrellato, squillo brillante e chiuso)", L"Aseo (aseo alicatado pequeño, timbre brillante y cerrado)", L"세면실 (좁은 타일의 밝고 밀폐된 울림)", L"洗手间 (狭窄瓷砖盥洗室，明亮密闭的回响)", L"غرفة الغسيل (دورة مياه بلاط ضيقة، رنين ساطع مغلق)", L"Умывальная (тесная кафельная умывальная, яркий закрытый звон)", L"Waschraum (kleiner Fliesenwaschraum, heller geschlossener Klang)", L"Lavabo (lavabo azulejado pequeno, tinido brilhante e fechado)", L"Wasruimte (kleine betegelde wasruimte, heldere gesloten galm)", L"Umywalnia (mała kafelkowa umywalnia, jasny zamknięty dźwięk)", L"Lavabo odası (dar fayans lavabo, parlak kapalı tını)"));
	//70
	m_env.AddString(LL14(L"廊下(カーペット) (カーペットに吸われた柔らかく短い廊下)", L"Carpeted corridor (carpeted hall, soft and dead)", L"Couloir moquetté (couloir moquetté, doux et mort)", L"Corridoio con moquette (corridoio moquettato, morbido e spento)", L"Pasillo enmoquetado (pasillo alfombrado, suave y apagado)", L"카펫 복도 (카펫에 흡수된 부드럽고 죽은 복도)", L"地毯走廊 (被地毯吸收的柔软短促走廊)", L"ممر مفروش بالسجاد (ممر مفروش، ناعم وميت)", L"Ковровый коридор (ковровый коридор, мягко и глухо)", L"Teppichflur (Teppichflur, weich und tot)", L"Corredor acarpetado (corredor acarpetado, macio e morto)", L"Gang met tapijt (tapijtgang, zacht en dood)", L"Korytarz z wykładziną (dywanowy korytarz, miękko i martwo)", L"Halı kaplı koridor (halılı koridor, yumuşak ve ölü)"));
	m_env.AddString(LL14(L"--[[専門空間 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Espace professionnel 71-80]]--", L"--[[Spazio professionale 71-80]]--", L"--[[Espacio profesional 71-80]]--", L"--[[전문 공간 71-80]]--", L"--[[专业空间 71-80]]--", L"--[[مساحة مهنية 71-80]]--", L"--[[Профессиональное пространство 71-80]]--", L"--[[Professioneller Raum 71-80]]--", L"--[[Espaço profissional 71-80]]--", L"--[[Professionele ruimte 71-80]]--", L"--[[Przestrzeń profesjonalna 71-80]]--", L"--[[Profesyonel alan 71-80]]--"), TRUE);
	//71
	m_env.AddString(LL14(L"会議室(大) (吸音天井の明瞭で程よい拡散)", L"Large meeting room (large meeting room, clear moderate reverb)", L"Grande salle de réunion (grande salle de réunion, réverbération claire et mesurée)", L"Grande sala riunioni (grande sala riunioni, riverbero chiaro e misurato)", L"Sala de reuniones grande (gran sala de reuniones, reverberación clara y moderada)", L"대회의실 (흡음 천장의 맑고 적당한 퍼짐)", L"大会议室 (吸音天花，清晰适度的扩散)", L"قاعة اجتماعات كبيرة (غرفة اجتماعات كبيرة، صدى واضح معتدل)", L"Большой конференц-зал (большой зал совещаний, ясный умеренный реверб)", L"Großer Besprechungsraum (großer Sitzungsraum, klarer gemäßigter Nachhall)", L"Grande sala de reuniões (grande sala de reunião, reverberação clara e moderada)", L"Grote vergaderzaal (grote vergaderruimte, heldere gematigde nagalm)", L"Duża sala konferencyjna (duża sala narad, wyraźny umiarkowany pogłos)", L"Büyük toplantı odası (büyük toplantı odası, net ılımlı yankı)"));
	//72
	m_env.AddString(LL14(L"会議室(小) (狭く吸音された密閉の近接感)", L"Small meeting room (small absorbed room, enclosed and close)", L"Petite salle de réunion (petite pièce absorbée, close et proche)", L"Piccola sala riunioni (piccola stanza assorbita, chiusa e vicina)", L"Sala de reuniones pequeña (sala pequeña absorbida, cerrada y cercana)", L"소회의실 (좁게 흡음된 밀폐의 근접감)", L"小会议室 (狭窄吸音、密闭贴近的空间)", L"قاعة اجتماعات صغيرة (غرفة صغيرة ممتصة، مغلقة وقريبة)", L"Малый конференц-зал (маленькая поглощённая комната, замкнуто и близко)", L"Kleiner Besprechungsraum (kleiner geschluckter Raum, geschlossen und nah)", L"Pequena sala de reuniões (sala pequena absorvida, fechada e próxima)", L"Kleine vergaderruimte (kleine geabsorbeerde kamer, gesloten en dichtbij)", L"Mała sala konferencyjna (mały pochłonięty pokój, zamknięty i bliski)", L"Küçük toplantı odası (küçük emilmiş oda, kapalı ve yakın)"));
	//73
	m_env.AddString(LL14(L"防音室 (完全吸音の無響に近い最デッド)", L"Soundproof room (fully damped, near-silent deadest room)", L"Chambre insonorisée (entièrement amortie, pièce la plus morte, presque silencieuse)", L"Camera insonorizzata (completamente smorzata, stanza più spenta, quasi silenziosa)", L"Sala insonorizada (totalmente amortiguada, sala más apagada, casi silenciosa)", L"방음실 (완전 흡음의 무향에 가까운 가장 죽은 방)", L"隔音室 (完全吸音、近乎无响的最干空间)", L"غرفة عازلة للصوت (مخمدة بالكامل، أميت غرفة شبه صامتة)", L"Звукоизолированная комната (полностью заглушённая, почти беззвучная самая сухая комната)", L"Schallschutzraum (vollständig bedämpft, nahezu stille toteste Kammer)", L"Sala à prova de som (totalmente amortecida, sala mais morta, quase silenciosa)", L"Geluiddichte kamer (volledig gedempt, bijna stille doodste kamer)", L"Pokój dźwiękoszczelny (w pełni wytłumiona, niemal cicha najmartwiejsza izba)", L"Ses yalıtımlı oda (tam sönümlü, neredeyse sessiz en ölü oda)"));
	//74
	m_env.AddString(LL14(L"エントランスホール (大理石とガラスの明るく高い空間)", L"Entrance hall (marble and glass, bright tall lobby)", L"Hall d'entrée (marbre et verre, hall haut et clair)", L"Atrio d'ingresso (marmo e vetro, atrio alto e brillante)", L"Vestíbulo (mármol y vidrio, vestíbulo alto y brillante)", L"엔트런스 홀 (대리석과 유리의 밝고 높은 공간)", L"入口大厅 (大理石与玻璃，明亮高挑的门厅)", L"بهو المدخل (رخام وزجاج، ردهة ساطعة شاهقة)", L"Входной холл (мрамор и стекло, яркий высокий вестибюль)", L"Eingangshalle (Marmor und Glas, helle hohe Lobby)", L"Saguão de entrada (mármore e vidro, saguão alto e brilhante)", L"Entreehal (marmer en glas, heldere hoge lobby)", L"Hol wejściowy (marmur i szkło, jasny wysoki hol)", L"Giriş holü (mermer ve cam, parlak yüksek lobi)"));
	//75
	m_env.AddString(LL14(L"書斎 (書棚に吸われた温かく静かな部屋)", L"Study room (study with shelves, warm and quiet)", L"Bureau/Cabinet (bureau aux étagères, chaud et calme)", L"Studio (stanza) (studio con scaffali, caldo e quieto)", L"Estudio (habitación) (estudio con estantes, cálido y silencioso)", L"서재 (책장에 흡수된 따뜻하고 조용한 방)", L"书房 (被书架吸收的温暖安静的书房)", L"غرفة مكتب (مكتبة أرفف، دافئة وهادئة)", L"Кабинет (кабинет со стеллажами, тепло и тихо)", L"Arbeitszimmer (Arbeitszimmer mit Regalen, warm und still)", L"Escritório (estúdio com prateleiras, quente e silencioso)", L"Studeerkamer (studeerkamer met planken, warm en stil)", L"Gabinet (gabinet z półkami, ciepło i cicho)", L"Çalışma odası (raflı çalışma odası, sıcak ve sessiz)"));
	//76
	m_env.AddString(LL14(L"キッチン (タイルと金属の硬く明るい響き)", L"Kitchen (tile and metal kitchen, hard and bright)", L"Cuisine (cuisine carrelée et métallique, dure et claire)", L"Cucina (cucina di piastrelle e metallo, dura e brillante)", L"Cocina (cocina de azulejo y metal, dura y brillante)", L"주방 (타일과 금속의 단단하고 밝은 울림)", L"厨房 (瓷砖与金属厨房，坚硬明亮)", L"مطبخ (مطبخ بلاط ومعدن، صلب ساطع)", L"Кухня (кафельно-металлическая кухня, жёстко и ярко)", L"Küche (Fliesen-und-Metall-Küche, hart und hell)", L"Cozinha (cozinha de azulejo e metal, dura e brilhante)", L"Keuken (tegel-en-metaalkeuken, hard en helder)", L"Kuchnia (kuchenna z płytek i metalu, twardo i jasno)", L"Mutfak (fayans ve metal mutfak, sert ve parlak)"));
	//77
	m_env.AddString(LL14(L"屋外駐車場 (開けた舗装の微風、繰り返す反射はない)", L"Outdoor parking lot (open pavement, breeze, no repeating bounce)", L"Parking extérieur (parking ouvert, brise, sans échos répétés)", L"Parcheggio esterno (piazzale aperto, brezza, senza echi ripetuti)", L"Estacionamiento exterior (aparcamiento abierto, brisa, sin ecos repetidos)", L"야외 주차장 (열린 포장의 미풍, 반복 반사 없음)", L"露天停车场 (开阔铺装与微风，没有反复弹回)", L"موقف سيارات خارجي (رصيف مفتوح، نسمة، بلا صدى متكرر)", L"Открытая стоянка (открытая мостовая, ветерок, без флаттер-эха)", L"Außenparkplatz (offenes Pflaster, Brise, kein Flatterecho)", L"Estacionamento externo (pavimento aberto, brisa, sem ecos repetidos)", L"Buitenparkeerplaats (open bestrating, bries, geen fladderecho)", L"Parking zewnętrzny (otwarty bruk, bryza, bez ech wielokrotnych)", L"Açık otopark (açık kaldırım, esinti, tekrarlayan sekme yok)"));
	//78
	m_env.AddString(LL14(L"地下道(狭) (極狭の平行壁で強く繰り返す反射)", L"Narrow underpass (very narrow underpass, strong repeating bounce)", L"Passage souterrain étroit (passage très étroit, forts échos répétés)", L"Sottopasso stretto (sottopasso strettissimo, forti echi ripetuti)", L"Paso subterráneo estrecho (paso muy estrecho, fuertes ecos repetidos)", L"좁은 지하도 (아주 좁은 평행 벽의 강한 플러터 반사)", L"狭窄地下通道 (极窄平行墙间强烈的颤动回声)", L"ممر سفلي ضيق (ممر ضيق جداً، صدى متكرر قوي)", L"Узкий подземный переход (очень узкий переход, сильное флаттер-эхо)", L"Enge Unterführung (sehr enge Unterführung, starkes Flatterecho)", L"Passagem subterrânea estreita (passagem muito estreita, fortes ecos repetidos)", L"Smalle onderdoorgang (zeer smalle onderdoorgang, sterke fladderecho)", L"Wąskie przejście podziemne (bardzo wąskie przejście, silne echa wielokrotne)", L"Dar alt geçit (çok dar alt geçit, güçlü çoklu yankı)"));
	//79
	m_env.AddString(LL14(L"展示室 (中立的な中規模、程よい拡散と吸音)", L"Exhibition room (neutral mid-size room, moderate diffusion)", L"Salle d'exposition (salle moyenne neutre, diffusion mesurée)", L"Sala espositiva (sala media neutra, diffusione misurata)", L"Sala de exposiciones (sala mediana neutra, difusión moderada)", L"전시실 (중성적인 중규모, 적당한 퍼짐과 흡음)", L"展览室 (中性中等房间，适度扩散与吸音)", L"قاعة عرض (غرفة متوسطة محايدة، انتشار معتدل)", L"Выставочный зал (нейтральная комната среднего размера, умеренное рассеяние)", L"Ausstellungsraum (neutraler mittelgroßer Raum, gemäßigte Streuung)", L"Sala de exposição (sala média neutra, difusão moderada)", L"Expositieruimte (neutrale middelgrote kamer, gematigde diffusie)", L"Sala wystawowa (neutralny średni pokój, umiarkowane rozproszenie)", L"Sergi salonu (nötr orta oda, ılımlı yayılım)"));
	//80
	m_env.AddString(LL14(L"アトリエ (木床と大窓の明るく風通しの良い空間)", L"Atelier/Studio room (wood floor and glass, bright and airy)", L"Atelier d'artiste (plancher de bois et verre, clair et aéré)", L"Atelier (pavimento di legno e vetro, brillante e arioso)", L"Taller/Estudio (suelo de madera y cristal, brillante y aireado)", L"아틀리에 (나무 바닥과 큰 창의 밝고 바람 잘 통하는 공간)", L"画室/工作室 (木地板与大窗，明亮通风的空间)", L"مرسم (أرضية خشب وزجاج، ساطع ومتجدد الهواء)", L"Мастерская художника (деревянный пол и стекло, ярко и воздушно)", L"Atelier (Holzboden und Glas, hell und luftig)", L"Ateliê (piso de madeira e vidro, brilhante e arejado)", L"Atelier (houten vloer en glas, helder en luchtig)", L"Pracownia artystyczna (drewniana podłoga i szkło, jasno i przewiewnie)", L"Atölye (ahşap zemin ve cam, parlak ve ferah)"));
	m_env.AddString(LL14(L"--[[SFX/未来 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Futur 81-100]]--", L"--[[SFX/Futuro 81-100]]--", L"--[[SFX/Futuro 81-100]]--", L"--[[SFX/미래 81-100]]--", L"--[[SFX/未来 81-100]]--", L"--[[SFX/مستقبل 81-100]]--", L"--[[SFX/Будущее 81-100]]--", L"--[[SFX/Zukunft 81-100]]--", L"--[[SFX/Futuro 81-100]]--", L"--[[SFX/Toekomst 81-100]]--", L"--[[SFX/Przyszłość 81-100]]--", L"--[[SFX/Gelecek 81-100]]--"), TRUE);
	//81
	m_env.AddString(LL14(L"サイバーパンク路地 (濡れコンクリートと金属の湿った歪み)", L"Cyberpunk alley (wet concrete and metal, damp grit)", L"Ruelle cyberpunk (béton mouillé et métal, grain humide)", L"Vicolo cyberpunk (cemento bagnato e metallo, grana umida)", L"Callejón cyberpunk (hormigón mojado y metal, grano húmedo)", L"사이버펑크 골목 (젖은 콘크리트와 금속의 축축한 일그러짐)", L"赛博朋克小巷 (湿混凝土与金属的潮湿粗糙感)", L"زقاق سايبربانك (خرسانة مبللة ومعدن، خشونة رطبة)", L"Киберпанк-переулок (мокрый бетон и металл, влажная шероховатость)", L"Cyberpunk-Gasse (nasser Beton und Metall, feuchte Rauheit)", L"Beco cyberpunk (concreto molhado e metal, aspereza úmida)", L"Cyberpunk-steegje (nat beton en metaal, vochtige ruwheid)", L"Zaułek cyberpunk (mokry beton i metal, wilgotna szorstkość)", L"Siberpunk ara sokağı (ıslak beton ve metal, nemli pürüz)"));
	//82
	m_env.AddString(LL14(L"宇宙船ブリッジ (金属とガラスの清潔なフェイズときらめき)", L"Spaceship bridge (metal and glass, clean phase and shimmer)", L"Passerelle de vaisseau (métal et verre, phasage propre et scintillement)", L"Ponte dell'astronave (metallo e vetro, fasatura pulita e scintillio)", L"Puente de nave espacial (metal y vidrio, faseo limpio y destello)", L"우주선 브리지 (금속과 유리의 깨끗한 페이즈와 반짝임)", L"宇宙飞船舰桥 (金属与玻璃，干净的相位与粼光)", L"جسر سفينة فضاء (معدن وزجاج، طور نظيف ووميض)", L"Мостик космического корабля (металл и стекло, чистый фейзинг и мерцание)", L"Raumschiffbrücke (Metall und Glas, saubere Phase und Schimmern)", L"Ponte da nave espacial (metal e vidro, faseamento limpo e cintilação)", L"Ruimteschipbrug (metaal en glas, schoon fase-effect en schittering)", L"Mostek statku kosmicznego (metal i szkło, czyste fazowanie i migotanie)", L"Uzay gemisi köprüsü (metal ve cam, temiz faz ve ışıltı)"));
	//83
	m_env.AddString(LL14(L"ワープトンネル (強いドップラーと高速に揺れる超広い空間)", L"Warp tunnel (strong Doppler sweep, ultra-wide motion)", L"Tunnel de distorsion (fort balayage Doppler, mouvement ultra-large)", L"Tunnel di curvatura (forte strisciata Doppler, moto ultra-ampio)", L"Túnel de curvatura (fuerte barrido Doppler, movimiento ultra-ancho)", L"워프 터널 (강한 도플러와 빠르게 흔들리는 초광폭 공간)", L"曲速隧道 (强烈多普勒与高速摇晃的超宽空间)", L"نفق الانحناء (كنس دوبلر قوي، حركة فائقة الاتساع)", L"Варп-туннель (сильный доплеровский сдвиг, сверхширокое движение)", L"Warp-Tunnel (starker Dopplerzug, ultrubreite Bewegung)", L"Túnel de dobra (forte varredura Doppler, movimento ultra-amplo)", L"Warptunnel (sterke Dopplerveeg, ultrabrede beweging)", L"Tunel warp (silne przesunięcie Dopplera, ultraszeroki ruch)", L"Warp tüneli (güçlü Doppler süpürmesi, ultra geniş hareket)"));
	//84
	m_env.AddString(LL14(L"量子ホール (きらめくガラス質の超高密度な拡散)", L"Quantum hall (sparkling glassy, ultra-dense reverb)", L"Salle quantique (vitreux scintillant, réverbération ultra-dense)", L"Sala quantistica (vetroso scintillante, riverbero ultra-denso)", L"Sala cuántica (vítreo destellante, reverberación ultradensa)", L"양자 홀 (반짝이는 유리질의 초고밀도 퍼짐)", L"量子大厅 (闪烁玻璃质的超高密度扩散)", L"قاعة كمومية (زجاجي وامض، صدى فائق الكثافة)", L"Квантовый зал (искристое стекло, сверхплотный реверб)", L"Quantenhalle (funkelnd glasig, ultradichter Nachhall)", L"Salão quântico (vítreo cintilante, reverberação ultradensa)", L"Quantumzaal (fonkelend glasachtig, ultradichte nagalm)", L"Sala kwantowa (iskrzące szkło, ultragęsty pogłos)", L"Kuantum salonu (ışıltılı camımsı, ultra yoğun yankı)"));
	//85
	m_env.AddString(LL14(L"無限回廊 (終わらないエコーが連鎖する回廊)", L"Infinite corridor (endless chain of corridor echoes)", L"Couloir infini (chaîne sans fin d'échos de couloir)", L"Corridoio infinito (catena infinita di echi di corridoio)", L"Pasillo infinito (cadena sin fin de ecos de pasillo)", L"무한 회랑 (끝나지 않는 메아리가 이어지는 회랑)", L"无限回廊 (回廊里没完没了的连锁回声)", L"ممر لا نهائي (سلسلة لا تنتهي من أصداء الممر)", L"Бесконечный коридор (бесконечная цепь коридорного эха)", L"Endloser Korridor (endlose Kette von Flurechos)", L"Corredor infinito (cadeia sem fim de ecos de corredor)", L"Oneindige gang (eindeloze keten van gang-echo's)", L"Nieskończony korytarz (nieskończony łańcuch echa korytarza)", L"Sonsuz koridor (bitmeyen koridor yankıları zinciri)"));
	//86
	m_env.AddString(LL14(L"逆再生空間 (滑らかに盛り上がる明るい残響)", L"Reverse space (swelling smooth reverb, bright bloom)", L"Espace inversé (réverbération lisse qui gonfle, éclat clair)", L"Spazio invertito (riverbero liscio che cresce, fioritura brillante)", L"Espacio invertido (reverberación lisa que crece, floreo brillante)", L"역재생 공간 (부드럽게 부풀어 오르는 밝은 잔향)", L"倒放空间 (平滑涌起的明亮混响)", L"فضاء معكوس (صدى أملس يتضخم، إشراق ساطع)", L"Реверсивное пространство (набухающий гладкий реверб, яркий расцвет)", L"Reverse-Raum (anschwellender glatter Nachhall, helles Aufblühen)", L"Espaço reverso (reverberação lisa que incha, florescência brilhante)", L"Omgekeerde ruimte (zwelling gladde nagalm, heldere opbloei)", L"Przestrzeń odwrócona (pęczniejący gładki pogłos, jasny rozkwit)", L"Ters çalma alanı (şişen pürüzsüz yankı, parlak açılma)"));
	//87
	m_env.AddString(LL14(L"タイムストップ室 (凍った無響、固まった静止感)", L"Time-stop room (frozen silent room, locked still)", L"Salle d'arrêt du temps (pièce silencieuse figée, immobilité verrouillée)", L"Stanza del tempo fermo (stanza silenziosa congelata, immobilità bloccata)", L"Sala de tiempo detenido (sala silenciosa congelada, inmovilidad trabada)", L"시간정지 방 (얼어붙은 무향, 굳은 정지감)", L"时间停止室 (冻结的无响，锁死的静止感)", L"غرفة إيقاف الزمن (غرفة صامتة متجمدة، سكون مقفل)", L"Комната остановки времени (замёрзшая беззвучная комната, застывшая неподвижность)", L"Zeitstopp-Raum (gefrorene stille Kammer, festgerastete Starre)", L"Sala do tempo parado (sala silenciosa congelada, imobilidade travada)", L"Tijdstopkamer (bevroren stille kamer, vastgezette stilstand)", L"Pokój zatrzymanego czasu (zamarznięty cichy pokój, zablokowany bezruch)", L"Zaman durdurma odası (donmuş sessiz oda, kilitli durağanlık)"));
	//88
	m_env.AddString(LL14(L"データセンター (金属ラックの空調ハムと箱鳴り)", L"Data center (metal racks, cooling-fan hum and boxy ring)", L"Centre de données (racks métalliques, bourdonnement de ventilation et sonnerie caverneuse)", L"Data center (rack metallici, ronzio di raffreddamento e squillo incassato)", L"Centro de datos (racks metálicos, zumbido de ventilación y timbre ahuecado)", L"데이터 센터 (금속 랙의 냉각팬 웅웅거림과 통울림)", L"数据中心 (金属机架的空调嗡鸣与箱鸣)", L"مركز بيانات (رفوف معدنية، أزيز تبريد ورنين أجوف)", L"Дата-центр (металлические стойки, гул охлаждения и гулкий звон)", L"Rechenzentrum (Metallgestelle, Lüftersummen und hohler Klang)", L"Data center (racks metálicos, zumbido de ventilação e tinido oco)", L"Datacenter (metalen rekken, koelerzoem en holle galm)", L"Centrum danych (metalowe szafy, szum chłodzenia i pusty dźwięk)", L"Veri merkezi (metal raflar, soğutma vızıltısı ve oyuk tını)"));
	//89
	m_env.AddString(LL14(L"巨大機械内部 (鋼板の金属反射と歪んだ共鳴)", L"Inside giant machine (steel plates, driven metallic resonance)", L"Intérieur de machine géante (plaques d'acier, résonance métallique saturée)", L"Interno di macchina gigante (lamiere d'acciaio, risonanza metallica satura)", L"Interior de máquina gigante (placas de acero, resonancia metálica saturada)", L"거대 기계 내부 (강판의 금속 반사와 일그러진 공명)", L"巨型机械内部 (钢板金属反射与失真共鸣)", L"داخل آلة عملاقة (صفائح فولاذ، رنين معدني مشوه)", L"Внутри гигантской машины (стальные плиты, искажённый металлический резонанс)", L"Inneres einer Riesenmaschine (Stahlplatten, getriebene metallische Resonanz)", L"Interior de máquina gigante (chapas de aço, ressonância metálica saturada)", L"Binnenin reuzenmachine (staalplaten, aangedreven metalen resonantie)", L"Wnętrze gigantycznej maszyny (blachy stalowe, napędzony metaliczny rezonans)", L"Dev makine içi (çelik levhalar, bozulmuş metalik rezonans)"));
	//90
	m_env.AddString(LL14(L"AIホログラム室 (ガラス質の明るいきらめきとフェイズ)", L"AI hologram room (glassy bright shimmer and phasing)", L"Salle holographique IA (scintillement vitreux clair et phasage)", L"Sala ologramma IA (scintillio vetroso brillante e fasatura)", L"Sala de holograma IA (destello vítreo brillante y faseo)", L"AI 홀로그램 룸 (유리질의 밝은 반짝임과 페이즈)", L"AI全息室 (玻璃质的明亮粼光与相位)", L"غرفة هولوغرام الذكاء الاصطناعي (وميض زجاجي ساطع وتأثير طور)", L"ИИ голограммная комната (яркое стеклянное мерцание и фейзинг)", L"KI-Hologrammraum (glasiges helles Schimmern und Phase)", L"Sala de holograma de IA (cintilação vítrea brilhante e faseamento)", L"AI-hologramkamer (heldere glasachtige schittering en fase-effect)", L"Pokój hologramów AI (jasne szklane migotanie i fazowanie)", L"YZ hologram odası (camımsı parlak ışıltı ve faz)"));
	//91
	m_env.AddString(LL14(L"重力ゼロ船庫 (巨大金属の浮遊するフェイズとドップラー)", L"Zero-gravity hangar (huge metal hangar, floating phase and Doppler)", L"Hangar en apesanteur (immense hangar métallique, phasage flottant et Doppler)", L"Hangar a gravità zero (enorme hangar metallico, fasatura fluttuante e Doppler)", L"Hangar de gravedad cero (enorme hangar metálico, faseo flotante y Doppler)", L"무중력 격납고 (거대한 금속의 떠다니는 페이즈와 도플러)", L"零重力机库 (巨大金属机库，漂浮的相位与多普勒)", L"حظيرة انعدام الجاذبية (حظيرة معدنية هائلة، طور عائم ودوبلر)", L"Ангар невесомости (огромный металлический ангар, плывущий фейзинг и Доплер)", L"Schwerelos-Hangar (riesiger Metallhangar, schwebende Phase und Doppler)", L"Hangar de gravidade zero (enorme hangar metálico, faseamento flutuante e Doppler)", L"Zwaartekrachtloze hangar (enorm metalen hangar, zwevend fase-effect en Doppler)", L"Hangar w stanie nieważkości (ogromny metalowy hangar, unoszące się fazowanie i Doppler)", L"Yerçekimsiz hangar (devasa metal hangar, yüzen faz ve Doppler)"));
	//92
	m_env.AddString(LL14(L"惑星ドーム都市 (巨大ガラスドームの開放ときらめき)", L"Planetary dome city (huge glass dome, open faint shimmer)", L"Cité-dôme planétaire (immense dôme de verre, ouvert avec un léger scintillement)", L"Città a cupola planetaria (enorme cupola di vetro, aperta con lieve scintillio)", L"Ciudad cúpula planetaria (enorme cúpula de cristal, abierta con destello tenue)", L"행성 돔 도시 (거대한 유리 돔의 개방과 반짝임)", L"行星穹顶城市 (巨大玻璃穹顶的开阔与粼光)", L"مدينة قبة كوكبية (قبة زجاجية هائلة، انفتاح ووميض خافت)", L"Планетарный купольный город (огромный стеклянный купол, открытость и слабое мерцание)", L"Planetare Kuppelstadt (riesige Glaskuppel, offen mit zartem Schimmern)", L"Cidade-cúpula planetária (enorme cúpula de vidro, aberta com cintilação tênue)", L"Planetaire koepelstad (enorme glazen koepel, open met vage schittering)", L"Planetarne miasto pod kopułą (ogromna szklana kopuła, otwartość i słabe migotanie)", L"Gezegen kubbe şehri (devasa cam kubbe, açık hafif ışıltı)"));
	//93
	m_env.AddString(LL14(L"VRシミュレーター (変化する人工音場の速いフェイズ)", L"VR simulator (morphing artificial field, fast phase)", L"Simulateur VR (champ artificiel qui mute, phasage rapide)", L"Simulatore VR (campo artificiale che muta, fasatura rapida)", L"Simulador VR (campo artificial que muda, faseo rápido)", L"VR 시뮬레이터 (변하는 인공 음장의 빠른 페이즈)", L"VR模拟器 (变化的人工声场，快速相位)", L"محاكي الواقع الافتراضي (حقل اصطناعي متغير، طور سريع)", L"VR-симулятор (меняющееся искусственное поле, быстрый фейзинг)", L"VR-Simulator (wechselndes künstliches Feld, schnelle Phase)", L"Simulador de RV (campo artificial que muda, faseamento rápido)", L"VR-simulator (wisselend kunstmatig veld, snel fase-effect)", L"Symulator VR (zmienne sztuczne pole, szybkie fazowanie)", L"VR simülatörü (değişen yapay alan, hızlı faz)"));
	//94
	m_env.AddString(LL14(L"レーザー通路 (金属壁の多重反射ときらめき)", L"Laser corridor (metal walls, repeating bounce and shimmer)", L"Couloir laser (murs métalliques, échos répétés et scintillement)", L"Corridoio laser (pareti metalliche, echi ripetuti e scintillio)", L"Corredor láser (paredes metálicas, ecos repetidos y destello)", L"레이저 통로 (금속 벽의 플러터 반사와 반짝임)", L"激光走廊 (金属墙的颤动回声与粼光)", L"ممر ليزر (جدران معدنية، صدى متكرر ووميض)", L"Лазерный коридор (металлические стены, флаттер-эхо и мерцание)", L"Laserkorridor (Metallwände, Flatterecho und Schimmern)", L"Corredor de laser (paredes metálicas, ecos repetidos e cintilação)", L"Lasergang (metalen wanden, fladderecho en schittering)", L"Korytarz laserowy (metalowe ściany, echa wielokrotne i migotanie)", L"Lazer koridoru (metal duvarlar, çoklu yankı ve ışıltı)"));
	//95
	m_env.AddString(LL14(L"異次元裂け目 (混沌としたフェイズと不安定な残響)", L"Dimensional rift (chaotic phase and Doppler, unstable reverb)", L"Faille dimensionnelle (phasage et Doppler chaotiques, réverbération instable)", L"Frattura dimensionale (fasatura e Doppler caotici, riverbero instabile)", L"Grieta dimensional (faseo y Doppler caóticos, reverberación inestable)", L"차원의 균열 (혼돈스러운 페이즈와 불안정한 잔향)", L"异次元裂缝 (混乱的相位与多普勒，不稳定的混响)", L"صدع بُعدي (طور ودوبلر فوضويان، صدى غير مستقر)", L"Разлом измерений (хаотичный фейзинг и Доплер, нестабильный реверб)", L"Dimensionsriss (chaotische Phase und Doppler, unsteter Nachhall)", L"Fenda dimensional (faseamento e Doppler caóticos, reverberação instável)", L"Dimensiescheur (chaotisch fase-effect en Doppler, onstabiele nagalm)", L"Szczelina wymiarów (chaotyczne fazowanie i Doppler, niestabilny pogłos)", L"Boyutsal yarık (kaotik faz ve Doppler, kararsız yankı)"));
	//96
	m_env.AddString(LL14(L"夢の中 (霞んで柔らかく幻想的な高密度拡散)", L"In a dream (hazy soft dreamy dense reverb)", L"Dans un rêve (réverbération brumeuse, douce et onirique, très dense)", L"In un sogno (riverbero nebbioso, morbido e onirico, molto denso)", L"En un sueño (reverberación brumosa, suave y onírica, muy densa)", L"꿈속 (아지랑이처럼 부드럽고 몽환적인 고밀도 퍼짐)", L"梦境中 (朦胧柔软、梦幻般的高密度扩散)", L"في حلم (صدى ضبابي ناعم حالم، كثيف جداً)", L"Во сне (дымчатый мягкий сонный плотный реверб)", L"Im Traum (dunstiger weicher träumerischer dichter Nachhall)", L"Em um sonho (reverberação enevoada, macia e onírica, bem densa)", L"In een droom (wazige zachte dromerige dichte nagalm)", L"We śnie (mglisty miękki senny gęsty pogłos)", L"Bir rüyada (puslu yumuşak rüyamsı yoğun yankı)"));
	//97
	m_env.AddString(LL14(L"水晶洞 (ガラス質に明るく鳴り響く洞窟)", L"Crystal cave (glassy cave, bright ringing reverb)", L"Grotte de cristal (grotte vitrée, réverbération claire et sonnante)", L"Grotta di cristallo (grotta vetrosa, riverbero brillante e risonante)", L"Cueva de cristal (cueva vítrea, reverberación brillante y sonora)", L"수정 동굴 (유리질로 밝게 울리는 동굴)", L"水晶洞 (玻璃质明亮鸣响的洞穴)", L"كهف بلوري (كهف زجاجي، صدى ساطع رنان)", L"Хрустальная пещера (стеклянная пещера, яркий звенящий реверб)", L"Kristallhöhle (glasige Höhle, heller klingender Nachhall)", L"Caverna de cristal (caverna vítrea, reverberação brilhante e soante)", L"Kristalgrot (glasachtige grot, heldere klinkende nagalm)", L"Kryształowa jaskinia (szklana jaskinia, jasny dźwięczny pogłos)", L"Kristal mağara (camımsı mağara, parlak çınlayan yankı)"));
	//98
	m_env.AddString(LL14(L"廃宇宙ステーション (空虚な金属のフェイズと漏れる風)", L"Derelict space station (empty metal, phasing and leaking wind)", L"Station spatiale abandonnée (métal vide, phasage et vent qui fuit)", L"Stazione spaziale abbandonata (metallo vuoto, fasatura e vento che filtra)", L"Estación espacial abandonada (metal vacío, faseo y viento que se filtra)", L"폐우주 정거장 (텅 빈 금속의 페이즈와 새는 바람)", L"废弃太空站 (空虚金属的相位与漏进的风)", L"محطة فضاء مهجورة (معدن فارغ، طور وريح تتسرب)", L"Заброшенная космостанция (пустой металл, фейзинг и просачивающийся ветер)", L"Verlassene Raumstation (leeres Metall, Phase und entweichender Wind)", L"Estação espacial abandonada (metal vazio, faseamento e vento que vaza)", L"Verlaten ruimtestation (leeg metaal, fase-effect en lekkende wind)", L"Opuszczona stacja kosmiczna (pusty metal, fazowanie i przeciekający wiatr)", L"Terk edilmiş uzay istasyonu (boş metal, faz ve sızan rüzgâr)"));
	//99
	m_env.AddString(LL14(L"ブラックホール縁 (極端なドップラーと重い長い残響)", L"Black hole edge (extreme Doppler pitch-bend, heavy long reverb)", L"Bord de trou noir (pliage Doppler extrême, réverbération lourde et longue)", L"Bordo del buco nero (piega Doppler estrema, riverbero pesante e lungo)", L"Borde del agujero negro (curvatura Doppler extrema, reverberación pesada y larga)", L"블랙홀 가장자리 (극단적인 도플러와 무겁고 긴 잔향)", L"黑洞边缘 (极端多普勒音高弯曲，沉重悠长的混响)", L"حافة الثقب الأسود (انحناء دوبلر أقصى، صدى ثقيل طويل)", L"Край чёрной дыры (крайний доплеровский изгиб высоты, тяжёлый длинный реверб)", L"Rand des Schwarzen Lochs (extremer Doppler-Tonhöhenzug, schwerer langer Nachhall)", L"Borda do buraco negro (curvatura Doppler extrema, reverberação pesada e longa)", L"Rand van zwart gat (extreme Doppler-toonbuiging, zware lange nagalm)", L"Krawędź czarnej dziury (skrajne gięcie wysokości Dopplera, ciężki długi pogłos)", L"Kara delik kenarı (aşırı Doppler perde bükümü, ağır uzun yankı)"));
	//100
	m_env.AddString(LL14(L"サイバー聖堂 (超高密度のデジタルきらめき、重厚で幻想的)", L"Cyber cathedral (ultra-dense digital shimmer, grand and ethereal)", L"Cathédrale cyber (scintillement numérique ultra-dense, grandiose et éthéré)", L"Cattedrale cyber (scintillio digitale ultra-denso, grandioso ed etereo)", L"Catedral cibernética (destello digital ultradenso, grandioso y etéreo)", L"사이버 대성당 (초고밀도 디지털 반짝임, 중후하고 환상적)", L"赛博大教堂 (超高密度数字粼光，厚重而空灵)", L"كاتدرائية سايبر (وميض رقمي فائق الكثافة، فخم وأثيري)", L"Кибер-собор (сверхплотное цифровое мерцание, величественно и эфирно)", L"Cyber-Kathedrale (ultradichtes digitales Schimmern, wuchtig und ätherisch)", L"Catedral cibernética (cintilação digital ultradensa, grandiosa e etérea)", L"Cyberkathedraal (ultradichte digitale schittering, groots en etherisch)", L"Cyberkatedra (ultragęste cyfrowe migotanie, wzniosłe i eteryczne)", L"Siber katedral (ultra yoğun dijital ışıltı, görkemli ve ruhani)"));

	{
		/*const int l[81] =
		{   0,
		     2, 3, 4, 5, 6, 7, 8, 9,10,11,
		    13,14,15,16,17,18,19,20,21,22,
		    24,25,26,27,28,29,30,31,32,33,
		    35,36,37,38,39,40,41,42,43,44,
			46,47,48,49,50,51,52,53,54,55,
			57,58,59,60,61,62,63,64,65,66,
			68,69,70,71,72,73,74,75,76,77,
			79,80,81,82,83,84,85,86,87,88
		};
		const int a = l[savedata.eqsoundenv];
		*/
		m_env.SetCurSel(savedata.eqsoundenv);
	}
	m_pre.AddString(LL14(L"デフォルト", L"Default", L"Par défaut", L"Predefinito", L"Predeterminado", L"기본값", L"默认", L"افتراضي", L"По умолчанию", L"Standard", L"Padrão", L"Standaard", L"Domyślny", L"Varsayılan"));
	m_pre.AddString(LL14(L"低音ブースト", L"Bass Boost", L"Renfort basses", L"Potenziamento bassi", L"Refuerzo graves", L"베이스 부스트", L"低音增强", L"تعزيز الجهير", L"Усиление низких", L"Bassverstärkung", L"Reforço graves", L"Basversterking", L"Wzmocnienie basów", L"Bas güçlendirme"));
	m_pre.AddString(LL14(L"高音ブースト", L"Treble Boost", L"Renfort aigus", L"Potenziamento acuti", L"Refuerzo agudos", L"트레블 부스트", L"高音增强", L"تعزيز الطبقة العالية", L"Усиление высоких", L"Höhenverstärkung", L"Reforço agudos", L"Hoge versterking", L"Wzmocnienie wysokich", L"Tiz güçlendirme"));
	m_pre.AddString(LL14(L"ボーカル強調", L"Vocal Enhance", L"Renfort vocal", L"Miglioramento vocale", L"Mejora vocal", L"보컬 강조", L"人声增强", L"تحسين الصوت", L"Улучшение вокала", L"Gesangsverbesserung", L"Melhoria vocal", L"Vocaalverbetering", L"Wzmocnienie wokalu", L"Vokal iyileştirme"));
	m_pre.AddString(LL14(L"低音カット", L"Bass Cut", L"Coupe basses", L"Taglio bassi", L"Corte graves", L"베이스 컷", L"低音衰减", L"قطع الجهير", L"Обрез низких", L"Bassabsenkung", L"Corte graves", L"Basreductie", L"Obniżenie basów", L"Bas kesme"));
	m_pre.AddString(LL14(L"高音カット", L"Treble Cut", L"Coupe aigus", L"Taglio acuti", L"Corte agudos", L"트레블 컷", L"高音衰减", L"قطع الطبقة العالية", L"Обрез высоких", L"Höhenabsenkung", L"Corte agudos", L"Hoge reductie", L"Obniżenie wysokich", L"Tiz kesme"));
	m_pre.AddString(LL14(L"ラウドネス", L"Loudness", L"Sonorité", L"Loudness", L"Sonoridad", L"음량", L"响度", L"جهارة", L"Громкость", L"Lautstärke", L"Sonoridade", L"Luidheid", L"Głośność", L"Ses yüksekliği"));
	m_pre.AddString(LL14(L"クラシック", L"Classical", L"Classique", L"Classico", L"Clásico", L"클래식", L"古典", L"كلاسيكي", L"Классика", L"Klassik", L"Clássico", L"Klassiek", L"Klasyka", L"Klasik"));
	m_pre.AddString(LL14(L"ロック", L"Rock", L"Rock", L"Rock", L"Rock", L"록", L"摇滚", L"روك", L"Рок", L"Rock", L"Rock", L"Rock", L"Rock", L"Rock"));
	m_pre.AddString(LL14(L"カスタム", L"Custom", L"Personnalisé", L"Personalizzato", L"Personalizado", L"사용자 지정", L"自定义", L"مخصص", L"Пользовательский", L"Benutzerdefiniert", L"Personalizado", L"Aangepast", L"Niestandardowy", L"Özel"));
	m_pre.AddString(LL14(L"ジャズ", L"Jazz", L"Jazz", L"Jazz", L"Jazz", L"재즈", L"爵士", L"جاز", L"Джаз", L"Jazz", L"Jazz", L"Jazz", L"Jazz", L"Caz"));
	m_pre.AddString(LL14(L"ポップ", L"Pop", L"Pop", L"Pop", L"Pop", L"팝", L"流行", L"بوب", L"Поп", L"Pop", L"Pop", L"Pop", L"Pop", L"Pop"));
	m_pre.AddString(LL14(L"EDM", L"EDM", L"Musique dance", L"Musica dance", L"Música dance", L"EDM", L"电子舞曲", L"موسيقى رقص", L"Танцевальная", L"Dance-Musik", L"Música dance", L"Dancemuziek", L"Muzyka dance", L"Dans müziği"));
	m_pre.AddString(LL14(L"メタル", L"Metal", L"Metal", L"Metal", L"Metal", L"메탈", L"金属", L"ميتال", L"Метал", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal"));
	m_pre.AddString(LL14(L"ヒップホップ", L"Hip Hop", L"Hip-hop", L"Hip hop", L"Hip hop", L"힙합", L"嘻哈", L"هيب هوب", L"Хип-хоп", L"Hip-Hop", L"Hip hop", L"Hiphop", L"Hip-hop", L"Hip Hop"));
	m_pre.AddString(LL14(L"アコースティック", L"Acoustic", L"Acoustique", L"Acustico", L"Acústico", L"어쿠스틱", L"原声", L"أكوستيك", L"Акустика", L"Akustisch", L"Acústico", L"Akoestisch", L"Akustyczny", L"Akustik"));
	m_pre.AddString(LL14(L"V字型(ドンシャリ)", L"V-shape", L"Courbe en V", L"Forma a V", L"Forma en V", L"V형", L"V形", L"شكل V", L"V-образный", L"V-Form", L"Formato V", L"V-vorm", L"Kształt V", L"V şekli"));
	m_pre.AddString(LL14(L"逆V字型", L"Inverse V", L"V inversé", L"V invertita", L"V invertida", L"역 V", L"反V形", L"V معكوس", L"Обратная V", L"Umgekehrtes V", L"V invertido", L"Omgekeerde V", L"Odwrócone V", L"Ters V"));
	m_pre.AddString(LL14(L"スマイルカーブ", L"Smile curve", L"Courbe sourire", L"Curva sorriso", L"Curva sonrisa", L"스마일 커브", L"微笑曲线", L"منحنى الابتسامة", L"Улыбка-кривая", L"Smile-Kurve", L"Curva smile", L"Smile curve", L"Krzywa uśmiechu", L"Gülümseme eğrisi"));
	m_pre.AddString(LL14(L"ラジオ/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"라디오/팟캐스트", L"收音机/播客", L"راديو/بودكاست", L"Радио/Подкаст", L"Radio/Podcast", L"Rádio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radyo/Podcast"));
	m_pre.AddString(LL14(L"映画/ドラマ", L"Movie/Drama", L"Cinéma/Série", L"Cinema/Drama", L"Cine/Drama", L"영화/드라마", L"电影/剧集", L"فيلم/دراما", L"Кино/Сериал", L"Film/Drama", L"Filme/Drama", L"Film/Drama", L"Film/Dramat", L"Film/Dizi"));
	m_pre.AddString(LL14(L"ゲーミング", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"게이밍", L"游戏", L"ألعاب", L"Игры", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Oyun"));
	m_pre.AddString(LL14(L"ライブ録音", L"Live recording", L"Enregistrement live", L"Registrazione dal vivo", L"Grabación en vivo", L"라이브 녹음", L"现场录音", L"تسجيل مباشر", L"Живая запись", L"Live-Aufnahme", L"Gravação ao vivo", L"Live-opname", L"Nagranie na żywo", L"Canlı kayıt"));
	m_pre.AddString(LL14(L"トレブルブースト", L"Treble Boost", L"Renfort aigus", L"Potenziamento acuti", L"Refuerzo agudos", L"트레블 부스트", L"高音增强", L"تعزيز الطبقات العالية", L"Усиление высоких", L"Höhenverstärkung", L"Reforço agudos", L"Hoge versterking", L"Wzmocnienie wysokich", L"Tiz güçlendirme"));
	m_pre.AddString(LL14(L"ベースブースト", L"Bass Boost", L"Renfort basses", L"Potenziamento bassi", L"Refuerzo graves", L"베이스 부스트", L"低音增强", L"تعزيز الجهير", L"Усиление низких", L"Bassverstärkung", L"Reforço graves", L"Basversterking", L"Wzmocnienie basów", L"Bas güçlendirme"));
	m_pre.AddString(LL14(L"小音量用", L"For low volume", L"Pour petit volume", L"Per volume basso", L"Para volumen bajo", L"저음량용", L"小音量用", L"لصوت منخفض", L"Для малой громкости", L"Für leise Lautstärke", L"Para baixo volume", L"Voor laag volume", L"Dla cichej głośności", L"Düşük ses için"));
	m_pre.AddString(LL14(L"ヘッドホン用", L"For headphones", L"Pour casque", L"Per cuffie", L"Para auriculares", L"헤드폰용", L"耳机用", L"للسماعات", L"Для наушников", L"Für Kopfhörer", L"Para fones de ouvido", L"Voor koptelefoon", L"Dla słuchawek", L"Kulaklık için"));
	m_pre.AddString(LL14(L"ボーカル除去", L"Vocal remove", L"Suppression vocal", L"Rimozione vocale", L"Eliminar voz", L"보컬 제거", L"人声消除", L"إزالة الصوت", L"Удаление вокала", L"Gesangsentfernung", L"Remover vocal", L"Vocaal verwijderen", L"Usuwanie wokalu", L"Vokal kaldırma"));
	m_pre.AddString(LL14(L"重低音強化", L"Subwoofer boost", L"Renfort subgrave", L"Potenziamento subwoofer", L"Refuerzo subgrave", L"서브우퍼 부스트", L"重低音增强", L"تعزيز subgrave", L"Усиление сабвуфера", L"Subwoofer-Verstärkung", L"Reforço subgrave", L"Subwooferversterking", L"Wzmocnienie subwoofera", L"Sublow güçlendirme"));
	m_pre.AddString(LL14(L"ラジオAM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"라디오 AM", L"调幅收音机", L"راديو AM", L"Радио AM", L"Radio AM", L"Rádio AM", L"Radio AM", L"Radio AM", L"Radyo AM"));
	m_pre.AddString(LL14(L"ラジオFM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"라디오 FM", L"调频收音机", L"راديو FM", L"Радио FM", L"Radio FM", L"Rádio FM", L"Radio FM", L"Radio FM", L"Radyo FM"));
	m_pre.AddString(LL14(L"テレビ音声", L"TV audio", L"Audio TV", L"Audio TV", L"Audio de TV", L"TV 오디오", L"电视音频", L"صوت التلفاز", L"ТВ-звук", L"TV-Ton", L"Áudio de TV", L"TV-audio", L"Dźwięk TV", L"TV sesi"));
	m_pre.AddString(LL14(L"電話音声", L"Phone voice", L"Voix téléphonique", L"Voce telefonica", L"Voz telefónica", L"전화 음성", L"电话语音", L"صوت هاتف", L"Телефонный голос", L"Telefonstimme", L"Voz telefônica", L"Telefoonstem", L"Głos telefoniczny", L"Telefon sesi"));
	m_pre.AddString(LL14(L"ビンテージ", L"Vintage", L"Vintage", L"Vintage", L"Vintage", L"빈티지", L"复古", L"كلاسيكي", L"Винтаж", L"Vintage", L"Vintage", L"Vintage", L"Retro", L"Vintage"));
	m_pre.AddString(LL14(L"モダン", L"Modern", L"Moderne", L"Moderno", L"Moderno", L"모던", L"现代", L"حديث", L"Современный", L"Modern", L"Moderno", L"Modern", L"Nowoczesny", L"Modern"));
	m_pre.AddString(LL14(L"ウォーム", L"Warm", L"Chaud", L"Caldo", L"Cálido", L"웜", L"温暖", L"دافئ", L"Тёплый", L"Warm", L"Quente", L"Warm", L"Ciepły", L"Sıcak"));
	m_pre.AddString(LL14(L"ブライト", L"Bright", L"Brillant", L"Brillante", L"Brillante", L"브라이트", L"明亮", L"ساطع", L"Яркий", L"Hell", L"Brilhante", L"Helder", L"Jasny", L"Parlak"));
	m_pre.AddString(LL14(L"フラット+", L"Flat+", L"Plat+", L"Piatto+", L"Plano+", L"플랫+", L"平直+", L"مسطح+", L"Ровный+", L"Flach+", L"Plano+", L"Vlak+", L"Płaski+", L"Düz+"));
	m_pre.AddString(LL14(L"スーパーベース", L"Super bass", L"Super basses", L"Super bassi", L"Super bajos", L"슈퍼 베이스", L"超级低音", L"صوت جهير فائق", L"Супербас", L"Super-Bass", L"Super graves", L"Super bas", L"Super bas", L"Süper bas"));
	m_pre.AddString(LL14(L"クリスタル", L"Crystal", L"Cristal", L"Cristallo", L"Cristal", L"크리스탈", L"水晶", L"كريستال", L"Кристалл", L"Kristall", L"Cristal", L"Kristal", L"Kryształ", L"Kristal"));
	m_pre.AddString(LL14(L"パーフェクト", L"Perfect", L"Parfait", L"Perfetto", L"Perfecto", L"퍼펙트", L"完美", L"مثالي", L"Идеальный", L"Perfekt", L"Perfeito", L"Perfect", L"Idealny", L"Mükemmel"));
	m_pre.AddString(LL14(L"ダンス/クラブ", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"댄스/클럽", L"舞曲/俱乐部", L"رقص/نادي", L"Танцы/Клуб", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dans/Kulüp"));
	m_pre.AddString(LL14(L"R&&B/ソウル", L"R&&B/Soul", L"R&&B/Soul", L"R&&B/Soul", L"R&&B/Soul", L"R&&B/소울", L"R&&B/灵魂乐", L"R&&B/سول", L"R&&B/Соул", L"R&&B/Soul", L"R&&B/Soul", L"R&&B/Soul", L"R&&B/Soul", L"R&&B/Soul"));
	m_pre.AddString(LL14(L"レゲエ", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"레게", L"雷鬼", L"ريغي", L"Регги", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae"));
	m_pre.AddString(LL14(L"ブルース", L"Blues", L"Blues", L"Blues", L"Blues", L"블루스", L"蓝调", L"البلوز", L"Блюз", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues"));
	m_pre.AddString(LL14(L"カントリー", L"Country", L"Country", L"Country", L"Country", L"컨트리", L"乡村", L"كانتري", L"Кантри", L"Country", L"Country", L"Country", L"Country", L"Country"));
	m_pre.AddString(LL14(L"ファンク", L"Funk", L"Funk", L"Funk", L"Funk", L"펑크", L"放克", L"فانك", L"Фанк", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk"));
	m_pre.AddString(LL14(L"エレクトロニカ", L"Electronica", L"Électronica", L"Elettronica", L"Electrónica", L"일렉트로니카", L"电子音乐", L"إلكترونيكا", L"Электроника", L"Electronica", L"Eletrônica", L"Electronica", L"Elektronika", L"Elektronika"));
	m_pre.AddString(LL14(L"アンビエント", L"Ambient", L"Ambiant", L"Ambient", L"Ambiente", L"앰비언트", L"氛围", L"أمبيент", L"Эмбиент", L"Ambient", L"Ambiente", L"Ambient", L"Ambient", L"Ambiyans"));
	m_pre.AddString(LL14(L"インストゥルメンタル", L"Instrumental", L"Instrumental", L"Strumentale", L"Instrumental", L"연주곡", L"纯音乐", L"موسيقى", L"Инструментальная", L"Instrumental", L"Instrumental", L"Instrumentaal", L"Instrumentalny", L"Enstrümental"));
	m_pre.AddString(LL14(L"ナレーション/オーディオブック", L"Narration/Audiobook", L"Narration/Livre audio", L"Narrazione/Audiolibro", L"Narración/Audiolibro", L"내레이션/오디오북", L"旁白/有声书", L"رواية/كتاب صوتي", L"Рассказ/Аудиокнига", L"Erzählung/Hörbuch", L"Narração/Audiolivro", L"Vertelling/Luisterboek", L"Narracja/Audiobook", L"Anlatım/Sesli kitap"));
	m_pre.AddString(LL14(L"ディープベース(安全)", L"Deep Bass (Safe)", L"Basses profondes (Sûr)", L"Bassi profondi (Sicuro)", L"Bajos profundos (Seguro)", L"딥 베이스(안전)", L"深低音（安全）", L"جهير عميق (آمن)", L"Глубокий бас (безопасно)", L"Tiefer Bass (Sicher)", L"Graves profundos (Seguro)", L"Diepe bas (Veilig)", L"Głęboki bas (Bezpieczny)", L"Derin Bas (Güvenli)"));
	m_pre.AddString(LL14(L"ボーカルクリア2", L"Vocal Clear 2", L"Voix claire 2", L"Voce chiara 2", L"Voz clara 2", L"보컬 클리어 2", L"人声清晰2", L"وضوح الصوت 2", L"Чистый вокал 2", L"Klarer Gesang 2", L"Vocal claro 2", L"Heldere vocalen 2", L"Czysty wokal 2", L"Net Vokal 2"));
	m_pre.AddString(LL14(L"エアリートレブル", L"Airy Treble", L"Aigus aérés", L"Alti ariosi", L"Agudos aireados", L"에어리 트레블", L"空气感高音", L"طبقات حادة هوائية", L"Воздушные высокие", L"Luftige Höhen", L"Agudos arejados", L"Luchtige hoge tonen", L"Przestrzenne wysokie", L"Havadar Tiz"));
	m_pre.AddString(LL14(L"中域パンチ", L"Mid Punch", L"Punch médium", L"Impatto medi", L"Pegada media", L"중역 펀치", L"中频冲击", L"دفع الترددات الوسطى", L"Ударная середина", L"Mitten-Punch", L"Impacto médio", L"Mid-punch", L"Uderzenie środka", L"Orta Frekans Darbesi"));
	m_pre.AddString(LL14(L"EDM(安全)", L"EDM (Safe)", L"EDM (Sûr)", L"EDM (Sicuro)", L"EDM (Seguro)", L"EDM(안전)", L"EDM（安全）", L"EDM (آمن)", L"EDM (безопасно)", L"EDM (Sicher)", L"EDM (Seguro)", L"EDM (Veilig)", L"EDM (Bezpieczny)", L"EDM (Güvenli)"));
	m_pre.AddString(LL14(L"ロック(ワイド)", L"Rock Wide", L"Rock large", L"Rock ampio", L"Rock amplio", L"록(와이드)", L"摇滚（宽广）", L"روك (واسع)", L"Рок (широкий)", L"Rock breit", L"Rock amplo", L"Rock breed", L"Rock szeroki", L"Rock Geniş"));
	m_pre.AddString(LL14(L"メタル(タイト)", L"Metal Tight", L"Metal serré", L"Metal stretto", L"Metal ajustado", L"메탈(타이트)", L"金属（紧致）", L"ميتال (محكم)", L"Метал (плотный)", L"Metal straff", L"Metal firme", L"Metal strak", L"Metal zwarty", L"Metal Sıkı"));
	m_pre.AddString(LL14(L"ヒップホップ(クラブ)", L"Hip Hop Club", L"Hip-hop club", L"Hip hop club", L"Hip hop club", L"힙합(클럽)", L"嘻哈（俱乐部）", L"هيب هوب كلوب", L"Хип-хоп клуб", L"Hip-Hop Club", L"Hip hop club", L"Hiphop club", L"Hip-hop klub", L"Hip Hop Kulüp"));
	m_pre.AddString(LL14(L"アコースティック(ウォーム2)", L"Acoustic Warm 2", L"Acoustique chaud 2", L"Acustico caldo 2", L"Acústico cálido 2", L"어쿠스틱(웜2)", L"原声（温暖2）", L"أكوستيك دافئ 2", L"Акустика тёплая 2", L"Akustisch warm 2", L"Acústico quente 2", L"Akoestisch warm 2", L"Akustyczny ciepły 2", L"Akustik Sıcak 2"));
	m_pre.AddString(LL14(L"フラット(モニター)", L"Flat Monitor", L"Plat moniteur", L"Flat monitor", L"Plano monitor", L"플랫(모니터)", L"平直（监听）", L"مسطح (مراقبة)", L"Ровный монитор", L"Flat Monitor", L"Flat monitor", L"Vlak monitor", L"Płaski monitor", L"Düz Monitör"));
	m_pre.AddString(LL14(L"ブライトボーカル", L"Bright Vocal", L"Voix brillante", L"Voce brillante", L"Voz brillante", L"브라이트 보컬", L"明亮人声", L"صوت ساطع", L"Яркий вокал", L"Heller Gesang", L"Vocal brilhante", L"Heldere vocalen", L"Jasny wokal", L"Parlak Vokal"));
	m_pre.AddString(LL14(L"低音+空気感", L"Bass and Air", L"Basses et air", L"Bassi e aria", L"Bajos y aire", L"저음+공기감", L"低频+空气感", L"جهير وهواء", L"Бас и воздух", L"Bass und Luft", L"Graves e ar", L"Bas en lucht", L"Bas i powietrze", L"Bas ve Hava"));
	m_pre.AddString(LL14(L"ポッドキャスト(ソフト)", L"Podcast Soft", L"Podcast doux", L"Podcast morbido", L"Podcast suave", L"팟캐스트(소프트)", L"播客（柔和）", L"بودكاست ناعم", L"Подкаст мягкий", L"Podcast weich", L"Podcast suave", L"Podcast zacht", L"Podcast miękki", L"Podcast Yumuşak"));
	m_pre.AddString(LL14(L"レトロラジオ2", L"Retro Radio 2", L"Radio rétro 2", L"Radio retrò 2", L"Radio retro 2", L"레트로 라디오 2", L"复古收音机2", L"راديو كلاسيكي 2", L"Ретро-радио 2", L"Retro-Radio 2", L"Rádio retrô 2", L"Retro radio 2", L"Radio retro 2", L"Retro Radyo 2"));
	m_pre.AddString(LL14(L"TVダイアログ+", L"TV Dialog+", L"Dialogue TV+", L"Dialoghi TV+", L"Diálogo TV+", L"TV 대사+", L"电视对白+", L"حوار التلفاز+", L"ТВ-диалог+", L"TV-Dialog+", L"Diálogo TV+", L"TV-dialoog+", L"Dialog TV+", L"TV Diyalog+"));
	m_pre.AddString(LL14(L"電話(ナロー+)", L"Phone Narrow+", L"Téléphone étroit+", L"Telefono stretto+", L"Teléfono estrecho+", L"전화(협대역+)", L"电话（窄带+）", L"هاتف ضيق+", L"Телефон узкий+", L"Telefon schmal+", L"Telefone estreito+", L"Telefoon smal+", L"Telefon wąski+", L"Telefon Dar+"));
	m_pre.AddString(LL14(L"ラウドネス(安全)", L"Loudness Safe", L"Sonie sûre", L"Loudness sicuro", L"Sonoridad segura", L"라우드니스(안전)", L"响度（安全）", L"شدة الصوت آمنة", L"Громкость безопасно", L"Lautheit sicher", L"Sonoridade segura", L"Luidheid veilig", L"Głośność bezpieczna", L"Ses Yüksekliği Güvenli"));
	m_pre.AddString(LL14(L"小型スピーカー", L"Small Speaker", L"Petite enceinte", L"Piccolo altoparlante", L"Altavoz pequeño", L"소형 스피커", L"小型扬声器", L"مكبر صوت صغير", L"Малый динамик", L"Kleiner Lautsprecher", L"Alto-falante pequeno", L"Kleine luidspreker", L"Mały głośnik", L"Küçük Hoparlör"));
	m_pre.AddString(LL14(L"カーオーディオ", L"Car Audio", L"Audio voiture", L"Audio auto", L"Audio de coche", L"차량 오디오", L"车载音频", L"صوت السيارة", L"Автозвук", L"Auto-Audio", L"Áudio automotivo", L"Auto-audio", L"Car audio", L"Araç Sesi"));
	m_pre.AddString(LL14(L"ナイトリスニング", L"Night Listening", L"Écoute nocturne", L"Ascolto notturno", L"Escucha nocturna", L"나이트 리스닝", L"夜间聆听", L"استماع ليلي", L"Ночное прослушивание", L"Nachtmodus Hören", L"Audição noturna", L"Nacht luisteren", L"Słuchanie nocne", L"Gece Dinleme"));
	m_pre.AddString(LL14(L"スタジオニュートラル+", L"Studio Neutral+", L"Studio neutre+", L"Studio neutro+", L"Estudio neutro+", L"스튜디오 뉴트럴+", L"录音室中性+", L"استوديو محايد+", L"Студийный нейтральный+", L"Studio neutral+", L"Estúdio neutro+", L"Studio neutraal+", L"Studio neutralny+", L"Stüdyo Nötr+"));
	m_pre.AddString(LL14(L"シンバルスパークル", L"Cymbal Sparkle", L"Brillance cymbales", L"Brillio piatti", L"Brillo de platillos", L"심벌 스파클", L"镲片闪亮", L"بريق الصنج", L"Блеск тарелок", L"Becken-Glanz", L"Brilho de pratos", L"Cimbaalglans", L"Blask talerzy", L"Zil Parıltısı"));
	m_pre.AddString(LL14(L"ドラムアタック", L"Drum Attack", L"Attaque batterie", L"Attacco batteria", L"Ataque de batería", L"드럼 어택", L"鼓点冲击", L"هجوم الطبول", L"Атака барабанов", L"Drum-Attacke", L"Ataque de bateria", L"Drumaanval", L"Atak perkusji", L"Davul Atak"));
	m_pre.AddString(LL14(L"ピアノプレゼンス", L"Piano Presence", L"Présence piano", L"Presenza piano", L"Presencia de piano", L"피아노 프레즌스", L"钢琴存在感", L"حضور البيانو", L"Присутствие пианино", L"Piano-Präsenz", L"Presença de piano", L"Piano-aanwezigheid", L"Obecność fortepianu", L"Piyano Varlığı"));
	m_pre.AddString(LL14(L"ストリングススムース", L"Strings Smooth", L"Cordes douces", L"Archi morbidi", L"Cuerdas suaves", L"스트링 스무스", L"弦乐柔顺", L"أوتار ناعمة", L"Гладкие струны", L"Sanfte Streicher", L"Cordas suaves", L"Strijkers zacht", L"Smyczki łagodne", L"Yaylılar Yumuşak"));
	m_pre.AddString(LL14(L"ブラスフォーカス", L"Brass Focus", L"Focus cuivres", L"Focus ottoni", L"Enfoque metales", L"브라스 포커스", L"铜管聚焦", L"تركيز النحاسيات", L"Фокус на духовых", L"Blechbläser-Fokus", L"Foco em metais", L"Brassfocus", L"Skupienie blach", L"Bakır Nefes Odak"));
	m_pre.AddString(LL14(L"クワイアワイド", L"Choir Wide", L"Chœur large", L"Coro ampio", L"Coro amplio", L"합창 와이드", L"合唱宽广", L"جوقة واسعة", L"Хор широкий", L"Chor breit", L"Coro amplo", L"Koor breed", L"Chór szeroki", L"Koro Geniş"));
	m_pre.AddString(LL14(L"シネマインパクト", L"Cinema Impact", L"Impact cinéma", L"Impatto cinema", L"Impacto cine", L"시네마 임팩트", L"影院冲击", L"تأثير سينمائي", L"Кино-импакт", L"Cinema-Impact", L"Impacto cinema", L"Cinema-impact", L"Efekt kinowy", L"Sinematik Etki"));
	m_pre.AddString(LL14(L"FPS足音強調", L"FPS Footstep", L"FPS pas accentués", L"FPS passi in evidenza", L"FPS pasos resaltados", L"FPS 발소리 강조", L"FPS脚步强化", L"FPS إبراز الخطوات", L"FPS шаги акцент", L"FPS Schritte betont", L"FPS passos destacados", L"FPS voetstappen benadrukt", L"FPS kroki wzmocnione", L"FPS Ayak Sesi"));
	m_pre.AddString(LL14(L"RPG雰囲気", L"RPG Atmosphere", L"Ambiance RPG", L"Atmosfera RPG", L"Atmósfera RPG", L"RPG 분위기", L"RPG氛围", L"أجواء RPG", L"Атмосфера RPG", L"RPG-Atmosphäre", L"Atmosfera RPG", L"RPG-sfeer", L"Klimat RPG", L"RPG Atmosfer"));
	m_pre.AddString(LL14(L"オープンワールド", L"Open World", L"Monde ouvert", L"Mondo aperto", L"Mundo abierto", L"오픈 월드", L"开放世界", L"عالم مفتوح", L"Открытый мир", L"Offene Welt", L"Mundo aberto", L"Open wereld", L"Otwarty świat", L"Açık Dünya"));
	m_pre.AddString(LL14(L"レーシングV", L"Racing V", L"Course V", L"Corsa V", L"Carreras V", L"레이싱 V", L"竞速V", L"سباق V", L"Гонки V", L"Racing V", L"Corrida V", L"Racing V", L"Wyścigi V", L"Yarış V"));
	m_pre.AddString(LL14(L"ファイティングパンチ", L"Fighting Punch", L"Punch combat", L"Pugno combattimento", L"Golpe de pelea", L"파이팅 펀치", L"格斗冲击", L"لكمة قتالية", L"Боевой панч", L"Fighting-Punch", L"Soco de luta", L"Fighting punch", L"Uderzenie walki", L"Dövüş Darbesi"));
	m_pre.AddString(LL14(L"Lo-Fiマイルド", L"Lo-Fi Mild", L"Lo-Fi doux", L"Lo-Fi morbido", L"Lo-Fi suave", L"Lo-Fi 마일드", L"Lo-Fi柔和", L"لو-فاي ناعم", L"Lo-Fi мягкий", L"Lo-Fi mild", L"Lo-Fi suave", L"Lo-Fi mild", L"Lo-Fi łagodny", L"Lo-Fi Hafif"));
	m_pre.AddString(LL14(L"チルソフト", L"Chill Soft", L"Chill doux", L"Chill morbido", L"Chill suave", L"칠 소프트", L"舒缓柔和", L"تشيل ناعم", L"Chill мягкий", L"Chill weich", L"Chill suave", L"Chill zacht", L"Chill łagodny", L"Chill Yumuşak"));
	m_pre.AddString(LL14(L"K-POPシャイン", L"K-Pop Shine", L"K-Pop brillant", L"K-Pop brillante", L"K-Pop brillo", L"K-POP 샤인", L"K-POP闪耀", L"K-Pop لامع", L"K-Pop блеск", L"K-Pop Glanz", L"K-Pop brilho", L"K-Pop glans", L"K-Pop blask", L"K-Pop Parlak"));
	m_pre.AddString(LL14(L"J-POPエア", L"J-Pop Air", L"J-Pop aérien", L"J-Pop arioso", L"J-Pop aéreo", L"J-POP 에어", L"J-POP空气感", L"J-Pop هوائي", L"J-Pop воздушный", L"J-Pop luftig", L"J-Pop arejado", L"J-Pop luchtig", L"J-Pop przestrzenny", L"J-Pop Havadar"));
	m_pre.AddString(LL14(L"アニメソング", L"Anime Song", L"Chanson anime", L"Canzone anime", L"Canción anime", L"애니송", L"动漫歌曲", L"أغنية أنمي", L"Аниме-песня", L"Anime-Song", L"Música anime", L"Anime lied", L"Piosenka anime", L"Anime Şarkı"));
	m_pre.AddString(LL14(L"オーケストラホール", L"Orchestra Hall", L"Salle d'orchestre", L"Sala orchestra", L"Sala de orquesta", L"오케스트라 홀", L"管弦乐厅", L"قاعة الأوركسترا", L"Оркестровый зал", L"Orchesterhalle", L"Sala de orquestra", L"Orkestzaal", L"Sala orkiestry", L"Orkestra Salonu"));
	m_pre.AddString(LL14(L"ライブステージ2", L"Live Stage 2", L"Scène live 2", L"Palco live 2", L"Escenario en vivo 2", L"라이브 스테이지 2", L"现场舞台2", L"منصة حية 2", L"Живая сцена 2", L"Live-Bühne 2", L"Palco ao vivo 2", L"Live podium 2", L"Scena na żywo 2", L"Canlı Sahne 2"));
	m_pre.AddString(LL14(L"マスタリング(軽)", L"Mastering Light", L"Mastering léger", L"Mastering leggero", L"Masterización ligera", L"마스터링(라이트)", L"母带（轻量）", L"ماستر خفيف", L"Лёгкий мастеринг", L"Mastering leicht", L"Masterização leve", L"Mastering licht", L"Mastering lekki", L"Mastering Hafif"));
	m_pre.AddString(LL14(L"サブタイト", L"Sub Tight", L"Sub serré", L"Sub stretto", L"Sub ajustado", L"서브 타이트", L"低频紧致", L"جهير محكم", L"Саб плотный", L"Sub straff", L"Sub firme", L"Sub strak", L"Sub zwarty", L"Sub Sıkı"));
	m_pre.AddString(LL14(L"ディープハウス", L"Deep House", L"Deep House", L"Deep House", L"Deep House", L"딥하우스", L"深浩室", L"ديب هاوس", L"Дип-хаус", L"Deep House", L"Deep House", L"Deep House", L"Deep House", L"Deep House"));
	m_pre.AddString(LL14(L"トランスリフト", L"Trance Lift", L"Lift trance", L"Lift trance", L"Impulso trance", L"트랜스 리프트", L"Trance提升", L"ترانس رفع", L"Транс подъём", L"Trance Lift", L"Elevação trance", L"Trance lift", L"Trance lift", L"Trance Lift"));
	m_pre.AddString(LL14(L"テクノエッジ", L"Techno Edge", L"Techno tranchant", L"Techno incisivo", L"Techno afilado", L"테크노 엣지", L"Techno锋锐", L"تكنو حاد", L"Техно-острота", L"Techno Edge", L"Techno intenso", L"Techno edge", L"Techno krawędź", L"Techno Keskin"));
	m_pre.AddString(LL14(L"ドラムンベース", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"드럼 앤 베이스", L"鼓打贝斯", L"درَم آند بيس", L"Драм-н-бейс", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass"));
	m_pre.AddString(LL14(L"ソフトクラシック", L"Soft Classical", L"Classique doux", L"Classica soft", L"Clásico suave", L"소프트 클래식", L"柔和古典", L"كلاسيكي ناعم", L"Мягкая классика", L"Sanfte Klassik", L"Clássico suave", L"Zachte klassiek", L"Klasyka łagodna", L"Yumuşak Klasik"));
	m_pre.AddString(LL14(L"音声明瞭", L"Speech Intelligibility", L"Intelligibilité de la parole", L"Intelligibilità vocale", L"Inteligibilidad de voz", L"음성 명료", L"语音清晰", L"وضوح الكلام", L"Разборчивость речи", L"Sprachverständlichkeit", L"Inteligibilidade da fala", L"Spraakverstaanbaarheid", L"Zrozumiałość mowy", L"Konuşma Anlaşılırlığı"));
	m_pre.AddString(LL14(L"AM(安全ナロー)", L"AM Safe Narrow", L"AM étroit sûr", L"AM stretto sicuro", L"AM estrecho seguro", L"AM(안전 협대역)", L"AM安全窄带", L"AM ضيق آمن", L"AM безопасный узкий", L"AM sicher schmal", L"AM estreito seguro", L"AM veilig smal", L"AM bezpieczny wąski", L"AM Güvenli Dar"));
	m_pre.AddString(LL14(L"FM(Hi-Fi安全)", L"FM Hi-Fi Safe", L"FM Hi-Fi sûr", L"FM Hi-Fi sicuro", L"FM Hi-Fi seguro", L"FM(Hi-Fi 안전)", L"FM Hi-Fi安全", L"FM Hi-Fi آمن", L"FM Hi-Fi безопасно", L"FM Hi-Fi sicher", L"FM Hi-Fi seguro", L"FM Hi-Fi veilig", L"FM Hi-Fi bezpieczny", L"FM Hi-Fi Güvenli"));
	m_pre.SetCurSel(savedata.eqsoundeq);

	if(savedata.eqx != -1
		&& savedata.eqx > -30000 && savedata.eqx < 30000
		&& savedata.eqy > -30000 && savedata.eqy < 30000)
		SetWindowPos(&CWnd::wndTop, savedata.eqx, savedata.eqy, 0, 0, SWP_NOSIZE| SWP_NOZORDER| SWP_NOOWNERZORDER);

	m_cachedKeyLow.Empty();
	m_cachedKeyMid.Empty();
	m_cachedKeyHigh.Empty();
	m_cachedKeyAll.Empty();

	RegisterEqKeyUiHwnd(m_hWnd);
	ApplyKeyCodesUi();


	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	SetTimer(1, 50, NULL);
	EnableMainWindowLock(&savedata.eqMainLock, TRUE);
	CCC_MainLockSetHeaderRow(m_hWnd, 0, 18);
	CCC_MainLockBringToFront(m_hWnd);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	LayoutToneColumns();
	return TRUE;
}
void CEqualizer::ApplyTitleFont()
{
	if (!m_t.GetSafeHwnd())
		return;
	UINT dpi = 96;
	if (HDC hdc = ::GetDC(m_t.GetSafeHwnd())) {
		dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
		::ReleaseDC(m_t.GetSafeHwnd(), hdc);
	}
	if (dpi < 96) dpi = 96;
	LOGFONT lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = -MulDiv(12 * 4, (int)dpi, 96);
	lf.lfItalic = TRUE;
	if (m_titleFont.GetSafeHandle())
		m_titleFont.DeleteObject();
	if (m_titleFont.CreateFontIndirect(&lf))
		m_t.SetFont(&m_titleFont);
}

void CEqualizer::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
/*	int a = m_env.GetCurSel();
	int c = 0;
	int l[] =
	{
		0,1,
		1,2,3,4,5,6,7,8,9,10,
		11,
		11,12,13,14,15,16,17,18,19,20,
		21,
		21,22,23,24,25,26,27,28,29,30,
		31,
		31,32,33,34,35,36,37,38,39,40,
		41,
		41,42,43,44,45,46,47,48,49,50,
		51,
		51,52,53,54,55,56,57,58,59,60,
		61,
		61,62,63,64,65,66,67,68,69,70,
		71,
		71,72,73,74,75,76,77,78,79,80
	};
	*/
	savedata.eqsoundenv = m_env.GetCurSel();
	reset = TRUE;
}

void equaliser(void* data, int len, BOOL reset);

void CEqualizer::OnCbnSelchangeCombo5()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	KillTimer(1);
	savedata.eqsoundeq = m_pre.GetCurSel();
	equaliser(0, 0, 2);
	SetTimer(1, 50, NULL);
}

void CEqualizer::ApplyKeyCodesUi()
{
	extern int playf;
	CString keyLow, keyMid, keyHigh, keyAll;
	if (playf == 0) {
		keyLow = keyMid = keyHigh = keyAll =
			L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
	}
	else {
		SnapshotEqKeyCodes(keyLow, keyMid, keyHigh, keyAll);
	}

	// 接頭辞は固定。毎回 LL14+一時 CString を作ると長時間でヒープを汚す。
	static CString s_pfxLow, s_pfxMid, s_pfxHigh, s_pfxAll;
	static bool s_pfxInit = false;
	if (!s_pfxInit) {
		s_pfxLow = LL14(L"低音域：", L"Low: ", L"Graves : ", L"Bassi: ", L"Graves: ", L"저음: ", L"低音：", L"المنخفضة: ", L"Низкие: ", L"Bässe: ", L"Graves: ", L"Lage: ", L"Niskie: ", L"Bas: ");
		s_pfxMid = LL14(L"中音域：", L"Mid: ", L"Médiums : ", L"Medi: ", L"Medios: ", L"중음: ", L"中音：", L"المتوسطة: ", L"Средние: ", L"Mitten: ", L"Médios: ", L"Midden: ", L"Średnie: ", L"Orta: ");
		s_pfxHigh = LL14(L"高音域：", L"High: ", L"Aigus : ", L"Alti: ", L"Agudos: ", L"고음: ", L"高音：", L"العالية: ", L"Высокие: ", L"Höhen: ", L"Agudos: ", L"Hoge: ", L"Wysokie: ", L"Tiz: ");
		s_pfxAll = LL14(L"全音域：", L"All: ", L"Toutes : ", L"Tutte: ", L"Todas: ", L"전체: ", L"全频段：", L"الكل: ", L"Все: ", L"Alle: ", L"Todas: ", L"Alles: ", L"Wszystkie: ", L"Tümü: ");
		s_pfxInit = true;
	}

	// 接頭辞結合は1本の再利用バッファへ（operator+ の一時 CString を出さない）
	static CString s_line;
	if (m_cachedKeyLow != keyLow) {
		s_line = s_pfxLow;
		s_line += keyLow;
		m_keyLow.SetWindowText(s_line);
		m_cachedKeyLow = keyLow;
	}
	if (m_cachedKeyMid != keyMid) {
		s_line = s_pfxMid;
		s_line += keyMid;
		m_keyMid.SetWindowText(s_line);
		m_cachedKeyMid = keyMid;
	}
	if (m_cachedKeyHigh != keyHigh) {
		s_line = s_pfxHigh;
		s_line += keyHigh;
		m_keyHigh.SetWindowText(s_line);
		m_cachedKeyHigh = keyHigh;
	}
	if (m_cachedKeyAll != keyAll) {
		s_line = s_pfxAll;
		s_line += keyAll;
		m_keyAll.SetWindowText(s_line);
		m_cachedKeyAll = keyAll;
	}
}

LRESULT CEqualizer::OnEqKeyUpdate(WPARAM, LPARAM)
{
	// Ack は Apply 後。SETREDRAW は子コントロール全体の描画を止めて
	// スライダー等が消えるデグレになるため使わない。
	if (::IsWindow(m_hWnd))
		ApplyKeyCodesUi();
	if (savedata.mpKeyEqSuggest) {
		CString lo, mid, hi, all;
		SnapshotEqKeyCodes(lo, mid, hi, all);
		const int preset = EqPresetIndexFromKeyCodes(all);
		if (preset != savedata.eqsoundeq) {
			savedata.eqsoundeq = preset;
			if (m_pre.GetSafeHwnd()) m_pre.SetCurSel(preset);
			equaliser(0, 0, 2);
			mod = preset;
		}
	}
	AckEqKeyUiNotify();
	return 0;
}

LRESULT CEqualizer::OnUiTick(WPARAM, LPARAM)
{
	m_tickPump.Ack();
	return 0;
}

void CEqualizer::OnDestroy()
{
	m_tickPump.Stop();
	UnregisterEqKeyUiHwnd(m_hWnd);
	KillTimer(1);
	if (g_eqHelpDlg && ::IsWindow(g_eqHelpDlg->GetSafeHwnd()))
		g_eqHelpDlg->DestroyWindow();
	// eqwindow は落とさない: アプリ終了時の Destroy でも次回起動復元のため残す。
	// ユーザー閉じは OnClose / OK / トグル側で 0 にする（Piano/Analyzer と同じ）。
	CCustomBlurDialogExBase::OnDestroy();
}

void CEqualizer::OnClose()
{
	savedata.eqwindow = 0;
	DestroyWindow();
}

void CEqualizer::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
		LayoutToneColumns();
	}
}

void CEqualizer::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

// マスター〜ディレイ列: 帯域スライダーと同じ上端・高さに揃え、ラベル／数値も同 Y に統一する。
// ラベル幅を文字幅まで広げて横圧縮を止め、SS_CENTER + 十分な高さで縦中央寄せする。
// 「Hz」と「マスター」が重なると「マ」が欠けるので、Hz 右端より左へ出さない。
void CEqualizer::LayoutToneColumns()
{
	CWnd* pRefSl = GetDlgItem(IDC_SLIDER7);
	CWnd* pRefLb = GetDlgItem(IDC_EQ_FREQ_25);
	CWnd* pRefVl = GetDlgItem(IDC_STATIC_e0);
	if (!pRefSl || !::IsWindow(pRefSl->GetSafeHwnd())) return;

	CRect rs;
	pRefSl->GetWindowRect(&rs);
	ScreenToClient(&rs);

	int labTop = rs.top;
	int labH = max(8, rs.Height() / 12);
	if (pRefLb && ::IsWindow(pRefLb->GetSafeHwnd())) {
		CRect rl;
		pRefLb->GetWindowRect(&rl);
		ScreenToClient(&rl);
		labTop = rl.top;
		labH = max(rl.Height(), labH);
	}
	CRect rv;
	BOOL haveVal = FALSE;
	if (pRefVl && ::IsWindow(pRefVl->GetSafeHwnd())) {
		pRefVl->GetWindowRect(&rv);
		ScreenToClient(&rv);
		haveVal = TRUE;
	}

	int hzRight = 0;
	if (CWnd* pHz = GetDlgItem(IDC_EQ_UNIT_HZ)) {
		if (::IsWindow(pHz->GetSafeHwnd())) {
			CRect hz;
			pHz->GetWindowRect(&hz);
			ScreenToClient(&hz);
			hzRight = hz.right + 2;
		}
	}

	struct Col { int sliderId; int labelId; int valueId; };
	static const Col cols[] = {
		{ IDC_SLIDER23, IDC_STATIC_EQ_SPECTRUM, IDC_STATIC_e15 },
		{ IDC_SLIDER24, IDC_STATIC_EQ_FREQ,     IDC_STATIC_e16 },
		{ IDC_SLIDER25, IDC_STATIC_EQ_BAND,     IDC_STATIC_e17 },
		{ IDC_SLIDER26, IDC_STATIC_EQ_LOUDNESS, IDC_STATIC_e18 },
		{ IDC_SLIDER27, IDC_STATIC_EQ_WARMTH,   IDC_STATIC_e19 },
		{ IDC_SLIDER28, IDC_STATIC_EQ_REVERB,   IDC_STATIC_e20 },
		{ IDC_SLIDER29, IDC_STATIC_EQ_CHORUS,   IDC_STATIC_e21 },
		{ IDC_SLIDER30, IDC_STATIC_EQ_DELAY,    IDC_STATIC_e22 },
	};
	const int n = (int)(sizeof(cols) / sizeof(cols[0]));

	CClientDC dc(this);
	CFont* pFont = GetFont();
	CFont* pOld = pFont ? dc.SelectObject(pFont) : NULL;
	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	labH = max(labH, tm.tmHeight + 2);

	CRect colS[8];
	int colCx[8];
	int nOk = 0;
	for (int i = 0; i < n; ++i) {
		CWnd* ps = GetDlgItem(cols[i].sliderId);
		if (!ps || !::IsWindow(ps->GetSafeHwnd())) {
			colCx[i] = 0;
			continue;
		}
		CRect s;
		ps->GetWindowRect(&s);
		ScreenToClient(&s);
		ps->SetWindowPos(NULL, s.left, rs.top, s.Width(), rs.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		s.top = rs.top;
		s.bottom = rs.top + rs.Height();
		colS[i] = s;
		colCx[i] = (s.left + s.right) / 2;
		++nOk;
	}
	if (nOk <= 0) {
		if (pOld) dc.SelectObject(pOld);
		return;
	}

	for (int i = 0; i < n; ++i) {
		if (colCx[i] <= 0) continue;
		const int cx = colCx[i];
		const CRect& s = colS[i];

		CWnd* pl = GetDlgItem(cols[i].labelId);
		if (pl && ::IsWindow(pl->GetSafeHwnd())) {
			pl->ModifyStyle(SS_TYPEMASK, SS_CENTER);
			if (CCustomStatic* pcs = DYNAMIC_DOWNCAST(CCustomStatic, pl))
				pcs->SetPreferWideMode(TRUE);
			CString text;
			pl->GetWindowText(text);
			CSize sz = dc.GetTextExtent(text);
			int lw = max(s.Width() + 6, sz.cx + 8);
			int left = cx - lw / 2;
			int right = left + lw;
			if (i == 0 && hzRight > 0 && left < hzRight) {
				left = hzRight;
				right = max(right, left + sz.cx + 8);
				lw = right - left;
			}
			if (i + 1 < n && colCx[i + 1] > 0) {
				const int mid = (cx + colCx[i + 1]) / 2;
				if (right > mid + 4)
					right = mid + 4;
				if (right < left + sz.cx + 6)
					right = left + sz.cx + 6;
				lw = right - left;
			}
			pl->SetWindowPos(NULL, left, labTop, lw, labH,
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		}

		CWnd* pv = GetDlgItem(cols[i].valueId);
		if (haveVal && pv && ::IsWindow(pv->GetSafeHwnd())) {
			CRect v;
			pv->GetWindowRect(&v);
			ScreenToClient(&v);
			pv->SetWindowPos(NULL, cx - v.Width() / 2, rv.top, v.Width(), rv.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
		}
	}

	if (pOld) dc.SelectObject(pOld);
}

void CEqualizer::ShowHelpSheet()
{
	if (g_eqHelpDlg && ::IsWindow(g_eqHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_eqHelpDlg, this);
		return;
	}
	if (g_eqHelpDlg && !::IsWindow(g_eqHelpDlg->GetSafeHwnd()))
		g_eqHelpDlg = nullptr;
	CEqHelpDlg* dlg = new CEqHelpDlg(this);
	if (!dlg->Create(IDD_EQ_HELP, this)) {
		delete dlg;
		return;
	}
	g_eqHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CEqualizer::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CEqualizer::OnSuggestEqFromKey()
{
	CString lo, mid, hi, all;
	SnapshotEqKeyCodes(lo, mid, hi, all);
	const int preset = EqPresetIndexFromKeyCodes(all);
	savedata.eqsoundeq = preset;
	if (m_pre.GetSafeHwnd()) m_pre.SetCurSel(preset);
	equaliser(0, 0, 2);
	mod = preset;
	KillTimer(1);
	SetTimer(1, 50, NULL);
}

void CEqualizer::OnToggleKeyEqAuto()
{
	savedata.mpKeyEqSuggest = savedata.mpKeyEqSuggest ? 0 : 1;
}

void CEqualizer::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CCustomPopupMenu menu;
	{
		CCustomPopupMenu* keySub = menu.AddSubMenu(
			LL14(L"キー提案", L"Key suggest", L"Suggestion tonalite", L"Suggerimento tonalita", L"Sugerencia tonalidad",
				L"키 제안", L"调性建议", L"اقتراح المفتاح", L"Предложение тональности", L"Tonart-Vorschlag",
				L"Sugestao de tonalidade", L"Toonsoort-voorstel", L"Propozycja tonacji", L"Anahtar onerisi"),
			LL14(L"検出キーに合わせたEQ提案と自動提案。", L"EQ suggestions from detected key and auto-suggest.", L"Suggestions EQ selon la tonalite detectee.", L"Suggerimenti EQ dalla tonalita rilevata.", L"Sugerencias EQ segun la tonalidad detectada.", L"감지 키 기반 EQ 제안과 자동 제안.", L"按检测调性建议 EQ 及自动建议。", L"اقتراحات EQ حسب المفتاح المكتشف والاقتراح التلقائي.", L"Предложения EQ по ключу и авто-предложение.", L"EQ-Vorschlage aus erkannter Tonart und Auto.", L"Sugestoes de EQ pela tonalidade e auto.", L"EQ-voorstellen uit toonsoort en auto.", L"Propozycje EQ z tonacji i auto.", L"Algilanan anahtardan EQ onerisi ve otomatik."));
		if (keySub) {
			keySub->AddCommand(IDM_EQ_SUGGEST_KEY,
				LL14(L"キーからEQを提案", L"Suggest EQ from key", L"Suggérer EQ depuis la tonalité", L"Suggerisci EQ dalla tonalità", L"Sugerir EQ desde tonalidad", L"키에서 EQ 제안", L"根据调性建议 EQ", L"اقتراح EQ من المفتاح", L"Предложить EQ по тональности", L"EQ aus Tonart vorschlagen", L"Sugerir EQ pela tonalidade", L"EQ voorstellen uit toonsoort", L"Zaproponuj EQ z tonacji", L"Anahtardan EQ oner"),
				LL14(L"検出中のキーに合わせたEQカーブを提案する", L"Suggest an EQ curve matching the detected key",
					L"Proposer une courbe EQ selon la tonalité détectée", L"Suggerisci una curva EQ per la tonalità rilevata",
					L"Sugerir una curva EQ según la tonalidad detectada", L"감지된 키에 맞는 EQ 커브 제안",
					L"按检测到的调性建议 EQ 曲线", L"اقتراح منحنى EQ حسب المفتاح المكتشف",
					L"Предложить кривую EQ по обнаруженной тональности", L"EQ-Kurve zur erkannten Tonart vorschlagen",
					L"Sugerir curva EQ conforme a tonalidade detectada", L"EQ-curve voorstellen bij gedetecteerde toonsoort",
					L"Zaproponuj krzywą EQ dla wykrytej tonacji", L"Algılanan anahtara uygun EQ eğrisi öner"));
			keySub->AddCheck(IDM_EQ_KEY_AUTO,
				LL14(L"キー検出時に自動提案", L"Auto-suggest on key detect", L"Suggestion auto sur détection", L"Suggerimento auto su rilevamento", L"Sugerencia auto al detectar", L"키 검출 시 자동 제안", L"检测到调性时自动建议", L"اقتراح تلقائي عند الكشف", L"Авто-предложение по ключу", L"Auto-Vorschlag bei Erkennung", L"Sugestao auto na deteccao", L"Auto-voorstel bij detectie", L"Auto-propozycja przy wykryciu", L"Algilamada otomatik oneri"),
				savedata.mpKeyEqSuggest != 0,
				LL14(L"キーが変わると自動でEQを提案", L"Auto-suggest EQ when the key changes", L"Suggérer EQ auto si la tonalité change", L"Suggerisci EQ auto al cambio tonalità", L"Sugerir EQ auto al cambiar tonalidad", L"키가 바뀌면 EQ 자동 제안", L"调性变化时自动建议 EQ", L"اقتراح EQ تلقائياً عند تغير المفتاح", L"Авто-предлагать EQ при смене ключа", L"EQ auto vorschlagen bei Tonartwechsel", L"Sugerir EQ auto ao mudar tonalidade", L"EQ auto voorstellen bij toonsoortwissel", L"Auto-proponuj EQ przy zmianie tonacji", L"Anahtar degisince EQ otomatik oner"));
		}
	}
	menu.AddSeparator();
	{
		CCustomPopupMenu* abSub = menu.AddSubMenu(
			LL14(L"A/B", L"A/B", L"A/B", L"A/B", L"A/B",
				L"A/B", L"A/B", L"A/B", L"A/B", L"A/B",
				L"A/B", L"A/B", L"A/B", L"A/B"),
			LL14(L"EQカーブをA/Bスロットに保存して切替比較。", L"Store EQ curves in A/B slots and toggle to compare.", L"Enregistrer les courbes EQ en A/B et basculer.", L"Salva curve EQ in A/B e alterna.", L"Guardar curvas EQ en A/B y alternar.", L"EQ 커브를 A/B에 저장하고 전환 비교.", L"将 EQ 曲线存到 A/B 并切换对比。", L"حفظ منحنيات EQ في A/B والتبديل.", L"Сохранять EQ в A/B и переключать.", L"EQ-Kurven in A/B speichern und umschalten.", L"Salvar curvas EQ em A/B e alternar.", L"EQ-curves in A/B opslaan en wisselen.", L"Zapisz krzywe EQ w A/B i przelacz.", L"EQ egirilerini A/B'ye kaydedip karsilastir."));
		if (abSub) {
			abSub->AddCommand(IDM_EQ_ABA,
				LL14(L"A に保存", L"Store to A", L"Enregistrer dans A", L"Salva in A", L"Guardar en A",
					L"A에 저장", L"保存到 A", L"حفظ في A", L"Сохранить в A", L"In A speichern",
					L"Salvar em A", L"Opslaan in A", L"Zapisz w A", L"A'ya kaydet"),
				LL14(L"現在のEQカーブをスロットAに保存します。", L"Store the current EQ curve into slot A.",
					L"Enregistrer la courbe EQ actuelle dans A.", L"Salva la curva EQ attuale nello slot A.",
					L"Guardar la curva EQ actual en la ranura A.", L"현재 EQ 커브를 슬롯 A에 저장합니다.",
					L"将当前 EQ 曲线保存到插槽 A。", L"حفظ منحنى EQ الحالي في الفتحة A.",
					L"Сохранить текущую кривую EQ в слот A.", L"Aktuelle EQ-Kurve in Slot A speichern.",
					L"Salvar a curva EQ atual no slot A.", L"Huidige EQ-curve opslaan in slot A.",
					L"Zapisz biezaca krzywa EQ w slocie A.", L"Gecerli EQ egirisini A yuvasina kaydet"));
			abSub->AddCommand(IDM_EQ_ABB,
				LL14(L"B に保存", L"Store to B", L"Enregistrer dans B", L"Salva in B", L"Guardar en B",
					L"B에 저장", L"保存到 B", L"حفظ في B", L"Сохранить в B", L"In B speichern",
					L"Salvar em B", L"Opslaan in B", L"Zapisz w B", L"B'ye kaydet"),
				LL14(L"現在のEQカーブをスロットBに保存します。", L"Store the current EQ curve into slot B.",
					L"Enregistrer la courbe EQ actuelle dans B.", L"Salva la curva EQ attuale nello slot B.",
					L"Guardar la curva EQ actual en la ranura B.", L"현재 EQ 커브를 슬롯 B에 저장합니다.",
					L"将当前 EQ 曲线保存到插槽 B。", L"حفظ منحنى EQ الحالي في الفتحة B.",
					L"Сохранить текущую кривую EQ в слот B.", L"Aktuelle EQ-Kurve in Slot B speichern.",
					L"Salvar a curva EQ atual no slot B.", L"Huidige EQ-curve opslaan in slot B.",
					L"Zapisz biezaca krzywa EQ w slocie B.", L"Gecerli EQ egirisini B yuvasina kaydet"));
			abSub->AddCommand(IDM_EQ_ABTOG,
				LL14(L"A ↔ B 切替", L"Toggle A ↔ B", L"Basculer A ↔ B", L"Alterna A ↔ B", L"Alternar A ↔ B",
					L"A ↔ B 전환", L"切换 A ↔ B", L"تبديل A ↔ B", L"Переключить A ↔ B", L"A ↔ B umschalten",
					L"Alternar A ↔ B", L"Wissel A ↔ B", L"Przelacz A ↔ B", L"A ↔ B gec"),
				LL14(L"保存したAとBのEQを切り替えて聴き比べます。", L"Toggle between saved EQ slots A and B to compare.",
					L"Basculer entre les EQ A et B pour comparer.", L"Alterna tra EQ A e B per confrontare.",
					L"Alternar entre EQ A y B para comparar.", L"저장한 A/B EQ를 전환해 비교 청취합니다.",
					L"在已保存的 A/B EQ 之间切换以便对比试听。", L"التبديل بين EQ A وB للمقارنة.",
					L"Переключать сохранённые EQ A и B для сравнения.", L"Zwischen EQ A und B umschalten zum Vergleich.",
					L"Alternar entre EQ A e B para comparar.", L"Wissel tussen EQ A en B om te vergelijken.",
					L"Przelacz miedzy EQ A i B do porownania.", L"Kayitli A/B EQ arasinda gecis yapip karsilastir"));
		}
	}
	menu.AddSeparator();
	{
		static const int kQuickPresets[] = {
			0, 1, 2, 3, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16
		};
		CCustomPopupMenu* preSub = menu.AddSubMenu(
			LL14(L"プリセット", L"Preset", L"Preset", L"Preset", L"Preset",
				L"프리셋", L"预设", L"إعداد مسبق", L"Пресет", L"Preset",
				L"Preset", L"Preset", L"Preset", L"Onayar"),
			LL14(L"よく使うEQカーブをすぐ適用", L"Apply a common EQ curve quickly",
				L"Appliquer rapidement une courbe EQ courante", L"Applica subito una curva EQ comune",
				L"Aplicar rapido una curva EQ comun", L"자주 쓰는 EQ 커브를 바로 적용",
				L"快速应用常用 EQ 曲线", L"تطبيق منحنى EQ شائع بسرعة",
				L"Быстро применить частую кривую EQ", L"Häufige EQ-Kurve schnell anwenden",
				L"Aplicar rapidamente uma curva EQ comum", L"Snel een gangbare EQ-curve toepassen",
				L"Szybko zastosuj czesta krzywa EQ", L"Sik kullanilan EQ egirisini uygula"));
		if (preSub && m_pre.GetSafeHwnd()) {
			const int cur = m_pre.GetCurSel();
			const int n = m_pre.GetCount();
			for (int i = 0; i < (int)(sizeof(kQuickPresets) / sizeof(kQuickPresets[0])); ++i) {
				const int idx = kQuickPresets[i];
				if (idx < 0 || idx >= n) continue;
				CString name;
				m_pre.GetLBText(idx, name);
				if (name.IsEmpty()) continue;
				preSub->AddCheck(IDM_EQ_PRESET_BASE + idx, name, cur == idx);
			}
		}
	}
	menu.AddSeparator();
	{
		CCustomPopupMenu* fxSub = menu.AddSubMenu(
			LL14(L"FX", L"FX", L"FX", L"FX", L"FX",
				L"FX", L"FX", L"FX", L"FX", L"FX",
				L"FX", L"FX", L"FX", L"FX"),
			LL14(L"エフェクト／マスター／リバーブ／コーラス／ディレイ。", L"Effect / Master / Reverb / Chorus / Delay.", L"Effet / Master / Reverb / Chorus / Delay.", L"Effetto / Master / Riverbero / Chorus / Delay.", L"Efecto / Master / Reverb / Chorus / Delay.", L"이펙트/마스터/리버브/코러스/딜레이.", L"效果/主音量/混响/合唱/延迟。", L"تأثير / ماستر / صدى / كورس / تأخير.", L"Эффект / Мастер / Реверб / Хорус / Дилей.", L"Effekt / Master / Hall / Chorus / Delay.", L"Efeito / Master / Reverb / Chorus / Delay.", L"Effect / Master / Galm / Chorus / Delay.", L"Efekt / Master / Poglos / Chorus / Delay.", L"Efekt / Master / Reverb / Chorus / Delay."));
		if (fxSub) {
			int eff = savedata.eqsoundeffect * 2;
			if (eff < 0) eff = 0;
			if (eff > 200) eff = 200;
			fxSub->AddSlider(
				LL14(L"エフェクト", L"Effect", L"Effet", L"Effetto", L"Efecto",
					L"이펙트", L"效果", L"تأثير", L"Эффект", L"Effekt",
					L"Efeito", L"Effect", L"Efekt", L"Efekt"),
				0, 200, eff, EqEffSliderCb, this,
				LL14(L"エフェクト Wet 量（ドラッグ中に反映）", L"Effect wet amount (live)", L"Quantite wet effet (direct)", L"Quantita wet effetto (live)", L"Cantidad wet efecto (en vivo)",
					L"이펙트 Wet 양(즉시)", L"效果 Wet 量（即时）", L"مقدار Wet للتأثير (مباشر)", L"Wet эффекта (сразу)", L"Effekt-Wet (live)",
					L"Quantidade wet do efeito (ao vivo)", L"Effect-wet (live)", L"Wet efektu (na zywo)", L"Efekt wet (anlik)"));
			int masterUi = savedata.eq[15];
			if (masterUi < 0) masterUi = 0;
			if (masterUi > 200) masterUi = 200;
			fxSub->AddSlider(
				LL14(L"マスター", L"Master", L"Master", L"Master", L"Master",
					L"마스터", L"主音量", L"الماستر", L"Мастер", L"Master",
					L"Master", L"Master", L"Master", L"Master"),
				0, 200, masterUi, EqMasterSliderCb, this,
				LL14(L"マスター音量（ドラッグ中に反映）", L"Master volume (live)", L"Volume master (direct)", L"Volume master (live)", L"Volumen master (en vivo)",
					L"마스터 볼륨(즉시)", L"主音量（即时）", L"مستوى الماستر (مباشر)", L"Громкость мастера (сразу)", L"Masterlautstarke (live)",
					L"Volume master (ao vivo)", L"Mastervolume (live)", L"Glosnosc master (na zywo)", L"Master ses (anlik)"));
			int revUi = savedata.eq_reverb;
			if (revUi < 0) revUi = 0;
			if (revUi > 200) revUi = 200;
			fxSub->AddSlider(
				LL14(L"リバーブ", L"Reverb", L"Reverb", L"Riverbero", L"Reverb",
					L"리버브", L"混响", L"صدى", L"Реверб", L"Hall",
					L"Reverb", L"Galm", L"Poglos", L"Reverb"),
				0, 200, revUi, EqReverbSliderCb, this,
				LL14(L"リバーブ量（ドラッグ中に反映）", L"Reverb amount (live)", L"Quantite reverb (direct)", L"Quantita riverbero (live)", L"Cantidad reverb (en vivo)",
					L"리버브 양(즉시)", L"混响量（即时）", L"مقدار الصدى (مباشر)", L"Количество реверба (сразу)", L"Hallanteil (live)",
					L"Quantidade de reverb (ao vivo)", L"Galmhoeveelheid (live)", L"Ilosc poglosu (na zywo)", L"Reverb miktari (anlik)"));
			int chUi = savedata.eq_chorus;
			if (chUi < 0) chUi = 0;
			if (chUi > 200) chUi = 200;
			fxSub->AddSlider(
				LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus",
					L"코러스", L"合唱", L"كورس", L"Хорус", L"Chorus",
					L"Chorus", L"Chorus", L"Chorus", L"Chorus"),
				0, 200, chUi, EqChorusSliderCb, this,
				LL14(L"コーラス量（ドラッグ中に反映）", L"Chorus amount (live)", L"Quantite chorus (direct)", L"Quantita chorus (live)", L"Cantidad chorus (en vivo)",
					L"코러스 양(즉시)", L"合唱量（即时）", L"مقدار الكورس (مباشر)", L"Количество хоруса (сразу)", L"Chorusanteil (live)",
					L"Quantidade de chorus (ao vivo)", L"Chorushoeveelheid (live)", L"Ilosc chorus (na zywo)", L"Chorus miktari (anlik)"));
			int dlUi = savedata.eq_delay;
			if (dlUi < 0) dlUi = 0;
			if (dlUi > 200) dlUi = 200;
			fxSub->AddSlider(
				LL14(L"ディレイ", L"Delay", L"Delay", L"Delay", L"Delay",
					L"딜레이", L"延迟", L"تأخير", L"Дилей", L"Delay",
					L"Delay", L"Delay", L"Delay", L"Delay"),
				0, 200, dlUi, EqDelaySliderCb, this,
				LL14(L"ディレイ量（ドラッグ中に反映）", L"Delay amount (live)", L"Quantite delay (direct)", L"Quantita delay (live)", L"Cantidad delay (en vivo)",
					L"딜레이 양(즉시)", L"延迟量（即时）", L"مقدار التأخير (مباشر)", L"Количество дилея (сразу)", L"Delayanteil (live)",
					L"Quantidade de delay (ao vivo)", L"Delayhoeveelheid (live)", L"Ilosc delay (na zywo)", L"Delay miktari (anlik)"));
		}
	}
	menu.AddSeparator();
	{
		CCustomPopupMenu* openSub = menu.AddSubMenu(
			LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Offnen", L"Abrir", L"Openen", L"Otworz", L"Ac"),
			LL14(L"アナライザ／ピアノロール／操作ガイドを開きます。", L"Open analyzer, piano roll, or the operation guide.", L"Ouvrir analyseur, piano roll ou le guide.", L"Apri analizzatore, piano roll o la guida.", L"Abrir analizador, piano roll o la guia.", L"분석기/피아노 롤/조작 가이드를 엽니다.", L"打开分析器/钢琴卷帘/操作指南。", L"فتح المحلل أو لفافة البيانو أو الدليل.", L"Открыть анализатор, пианоролл или руководство.", L"Analyzer, Piano-Roll oder Bedienungsanleitung offnen.", L"Abrir analisador, piano roll ou o guia.", L"Open analyzer, piano-roll of handleiding.", L"Otworz analizator, piano roll lub przewodnik.", L"Analizor, piyano roll veya kilavuzu ac."));
		if (openSub) {
			openSub->AddCommand(IDM_EQ_OPEN_ANALYZER,
				LL14(L"アナライザを開く", L"Open analyzer", L"Ouvrir l'analyseur", L"Apri analizzatore", L"Abrir analizador",
					L"분석기 열기", L"打开分析器", L"فتح المحلل", L"Открыть анализатор", L"Analyzer öffnen",
					L"Abrir analisador", L"Analyzer openen", L"Otworz analizator", L"Analizoru ac"),
				LL14(L"アナライザウィンドウを開きます。", L"Open the analyzer window.", L"Ouvrir la fenetre de l'analyseur.", L"Apri la finestra dell'analizzatore.", L"Abrir la ventana del analizador.",
					L"분석기 창을 엽니다.", L"打开分析器窗口。", L"فتح نافذة المحلل.", L"Открыть окно анализатора.", L"Analyzer-Fenster öffnen.",
					L"Abrir a janela do analisador.", L"Open het analyzer-venster.", L"Otworz okno analizatora.", L"Analizor penceresini ac."));
			openSub->AddCommand(IDM_EQ_OPEN_PIANO,
				LL14(L"ピアノロールを開く", L"Open piano roll", L"Ouvrir le piano roll", L"Apri piano roll", L"Abrir piano roll",
					L"피아노 롤 열기", L"打开钢琴卷帘", L"فتح لفافة البيانو", L"Открыть пианоролл", L"Piano-Roll öffnen",
					L"Abrir piano roll", L"Piano-roll openen", L"Otworz piano roll", L"Piyano rolunu ac"),
				LL14(L"ピアノロールウィンドウを開きます。", L"Open the piano roll window.", L"Ouvrir la fenetre du piano roll.", L"Apri la finestra del piano roll.", L"Abrir la ventana del piano roll.",
					L"피아노 롤 창을 엽니다.", L"打开钢琴卷帘窗口。", L"فتح نافذة لفافة البيانو.", L"Открыть окно пианоролла.", L"Piano-Roll-Fenster öffnen.",
					L"Abrir a janela do piano roll.", L"Open het piano-roll-venster.", L"Otworz okno piano roll.", L"Piyano rulosu penceresini ac."));
			openSub->AddCommand(IDM_EQ_OPEN_MIDIMON,
				LL14(L"MIDIモニタを開く", L"Open MIDI monitor", L"Ouvrir le moniteur MIDI", L"Apri monitor MIDI", L"Abrir monitor MIDI",
					L"MIDI 모니터 열기", L"打开MIDI监视器", L"فتح مراقب MIDI", L"Открыть MIDI-монитор", L"MIDI-Monitor öffnen",
					L"Abrir monitor MIDI", L"MIDI-monitor openen", L"Otworz monitor MIDI", L"MIDI izleyiciyi ac"),
				LL14(L"MIDI 32パート・モニタを開きます。", L"Open the 32-part MIDI monitor.", L"Ouvrir le moniteur MIDI 32 parties.", L"Apri il monitor MIDI a 32 parti.", L"Abrir el monitor MIDI de 32 partes.",
					L"MIDI 32파트 모니터를 엽니다.", L"打开 MIDI 32 声部监视器。", L"فتح مراقب MIDI ذا 32 جزءاً.", L"Открыть MIDI-монитор на 32 партии.", L"32-Part-MIDI-Monitor öffnen.",
					L"Abrir o monitor MIDI de 32 partes.", L"Open de MIDI-monitor met 32 partijen.", L"Otworz monitor MIDI 32 partii.", L"32 part MIDI izleyiciyi ac."));
			openSub->AddCommand(ID_HELP_SHOWSHEET,
				LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa",
					L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل",
					L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding",
					L"Przewodnik", L"İşlem kılavuzu"),
				LL14(L"この画面の操作ガイドを表示します。", L"Show the operation guide for this window.", L"Afficher le guide d'utilisation de cette fenetre.", L"Mostra la guida operativa di questa finestra.", L"Mostrar la guia de operacion de esta ventana.",
					L"이 화면의 조작 가이드를 표시합니다.", L"显示此窗口的操作指南。", L"عرض دليل تشغيل هذه النافذة.", L"Показать руководство по этому окну.", L"Bedienungsanleitung für dieses Fenster anzeigen.",
					L"Mostrar o guia de operacao desta janela.", L"Toon de handleiding voor dit venster.", L"Pokaz przewodnik po tym oknie.", L"Bu pencerenin islem kilavuzunu goster."));
		}
	}
	if (point.x == -1 && point.y == -1) {
		CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
		point = CPoint(rc.left + 8, rc.top + 8);
	}
	const UINT cmd = menu.Track(point, this);
	if (cmd == IDM_EQ_ABA) OnBnClickedAbA();
	else if (cmd == IDM_EQ_ABB) OnBnClickedAbB();
	else if (cmd == IDM_EQ_ABTOG) OnBnClickedAbTog();
	else if (cmd >= IDM_EQ_PRESET_BASE && cmd < IDM_EQ_PRESET_BASE + 200) {
		const int idx = (int)cmd - IDM_EQ_PRESET_BASE;
		if (m_pre.GetSafeHwnd() && idx >= 0 && idx < m_pre.GetCount()) {
			m_pre.SetCurSel(idx);
			OnCbnSelchangeCombo5();
		}
	}
	else if (cmd == IDM_EQ_OPEN_ANALYZER) {
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->PostMessage(WM_OGG_TOGGLE_SUBUI, 2, 0);
	}
	else if (cmd == IDM_EQ_OPEN_PIANO) {
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->PostMessage(WM_OGG_TOGGLE_SUBUI, 1, 0);
	}
	else if (cmd == IDM_EQ_OPEN_MIDIMON) {
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->PostMessage(WM_OGG_TOGGLE_SUBUI, 3, 0);
	}
	else if (cmd == ID_HELP_SHOWSHEET) ShowHelpSheet();
	else if (cmd)
		SendMessage(WM_COMMAND, cmd);
}

int backms = 0;
void CEqualizer::OnTimer(UINT_PTR nIDEvent)
{
	// プロンプト実行中はスライダー→savedata の上書きをしない（実行側がオーナー）。
	// 表示だけ時々同期する。
	extern BOOL MpPromptIsActive();
	if (MpPromptIsActive()) {
		static DWORD s_lastPromptSync = 0;
		const DWORD now = GetTickCount();
		if (s_lastPromptSync == 0 || now - s_lastPromptSync >= 250) {
			s_lastPromptSync = now;
			SyncSlidersFromSavedata();
		}
		extern int playf;
		if (playf == 0)
			ApplyKeyCodesUi();
		CCustomBlurDialogExBase::OnTimer(nIDEvent);
		return;
	}
	// スライダー同期用。Kill/Set はしない（遅延が間隔に乗り WM_TIMER 飢餓を悪化させる）。
	// コード表示は再生中 WM_EQ_KEY_UPDATE、停止中のみここで更新。
	if (mod != savedata.eqsoundeq) {
		if (savedata.eqsoundeq != 9) {
			m_s0.SetPos(200 - savedata.eq[0]);
			m_s1.SetPos(200 - savedata.eq[1]);
			m_s2.SetPos(200 - savedata.eq[2]);
			m_s3.SetPos(200 - savedata.eq[3]);
			m_s4.SetPos(200 - savedata.eq[4]);
			m_s5.SetPos(200 - savedata.eq[5]);
			m_s6.SetPos(200 - savedata.eq[6]);
			m_s7.SetPos(200 - savedata.eq[7]);
			m_s8.SetPos(200 - savedata.eq[8]);
			m_s9.SetPos(200 - savedata.eq[9]);
			m_s10.SetPos(200 - savedata.eq[10]);
			m_s11.SetPos(200 - savedata.eq[11]);
			m_s12.SetPos(200 - savedata.eq[12]);
			m_s13.SetPos(200 - savedata.eq[13]);
			m_s14.SetPos(200 - savedata.eq[14]);
			CString s;
			s.Format(L"%d", savedata.eq[0]);
			m_v0.SetWindowText(s);
			s.Format(L"%d", savedata.eq[1]);
			m_v1.SetWindowText(s);
			s.Format(L"%d", savedata.eq[2]);
			m_v2.SetWindowText(s);
			s.Format(L"%d", savedata.eq[3]);
			m_v3.SetWindowText(s);
			s.Format(L"%d", savedata.eq[4]);
			m_v4.SetWindowText(s);
			s.Format(L"%d", savedata.eq[5]);
			m_v5.SetWindowText(s);
			s.Format(L"%d", savedata.eq[6]);
			m_v6.SetWindowText(s);
			s.Format(L"%d", savedata.eq[7]);
			m_v7.SetWindowText(s);
			s.Format(L"%d", savedata.eq[8]);
			m_v8.SetWindowText(s);
			s.Format(L"%d", savedata.eq[9]);
			m_v9.SetWindowText(s);
			s.Format(L"%d", savedata.eq[10]);
			m_v10.SetWindowText(s);
			s.Format(L"%d", savedata.eq[11]);
			m_v11.SetWindowText(s);
			s.Format(L"%d", savedata.eq[12]);
			m_v12.SetWindowText(s);
			s.Format(L"%d", savedata.eq[13]);
			m_v13.SetWindowText(s);
			s.Format(L"%d", savedata.eq[14]);
			m_v14.SetWindowText(s);
		}
		mod = savedata.eqsoundeq;
	}
	CString s;
	int vol;
	int flg = 0;
	vol = 200 - m_s0.GetPos();
	if (vol != savedata.eq[0]) { s.Format(L"%d", vol); m_v0.SetWindowText(s); flg = 1; }
	savedata.eq[0] = vol;
	vol = 200 - m_s1.GetPos();
	if (vol != savedata.eq[1]) { s.Format(L"%d", vol); m_v1.SetWindowText(s); flg = 1;}
	savedata.eq[1] = vol;
	vol = 200 - m_s2.GetPos();
	if (vol != savedata.eq[2]) { s.Format(L"%d", vol); m_v2.SetWindowText(s); flg = 1;	}
	savedata.eq[2] = vol;
	vol = 200 - m_s3.GetPos();
	if (vol != savedata.eq[3]) { s.Format(L"%d", vol); m_v3.SetWindowText(s); flg = 1;	}
	savedata.eq[3] = vol;
	vol = 200 - m_s4.GetPos();
	if (vol != savedata.eq[4]) { s.Format(L"%d", vol); m_v4.SetWindowText(s);  flg = 1;	}
	savedata.eq[4] = vol;
	vol = 200 - m_s5.GetPos();
	if (vol != savedata.eq[5]) { s.Format(L"%d", vol); m_v5.SetWindowText(s); flg = 1;	}
	savedata.eq[5] = vol;
	vol = 200 - m_s6.GetPos();
	if (vol != savedata.eq[6]) { s.Format(L"%d", vol); m_v6.SetWindowText(s); flg = 1;	}
	savedata.eq[6] = vol;
	vol = 200 - m_s7.GetPos();
	if (vol != savedata.eq[7]) { s.Format(L"%d", vol); m_v7.SetWindowText(s); flg = 1;	}
	savedata.eq[7] = vol;
	vol = 200 - m_s8.GetPos();
	if (vol != savedata.eq[8]) { s.Format(L"%d", vol); m_v8.SetWindowText(s); flg = 1;	}
	savedata.eq[8] = vol;
	vol = 200 - m_s9.GetPos();
	if (vol != savedata.eq[9]) { s.Format(L"%d", vol); m_v9.SetWindowText(s); flg = 1;	}
	savedata.eq[9] = vol;

	vol = 200 - m_s10.GetPos();
	if (vol != savedata.eq[10]) { s.Format(L"%d", vol); m_v10.SetWindowText(s); flg = 1; }
	savedata.eq[10] = vol;
	vol = 200 - m_s11.GetPos();
	if (vol != savedata.eq[11]) { s.Format(L"%d", vol); m_v11.SetWindowText(s); flg = 1; }
	savedata.eq[11] = vol;
	vol = 200 - m_s12.GetPos();
	if (vol != savedata.eq[12]) { s.Format(L"%d", vol); m_v12.SetWindowText(s); flg = 1; }
	savedata.eq[12] = vol;
	vol = 200 - m_s13.GetPos();
	if (vol != savedata.eq[13]) { s.Format(L"%d", vol); m_v13.SetWindowText(s); flg = 1; }
	savedata.eq[13] = vol;
	vol = 200 - m_s14.GetPos();
	if (vol != savedata.eq[14]) { s.Format(L"%d", vol); m_v14.SetWindowText(s); flg = 1; }
	savedata.eq[14] = vol;

	if (flg == 1) { m_pre.SetCurSel(9); savedata.eqsoundeq = 9; }


	vol = 200 - m_smaster.GetPos();
	if (vol != savedata.eq[15]) { s.Format(L"%d", vol); m_vmaster.SetWindowText(s); }
	savedata.eq[15] = vol;
	vol = 200 - m_ssenmei.GetPos();
	if (vol != savedata.eq[16]) { s.Format(L"%d", vol); m_vsenmei.SetWindowText(s); }
	savedata.eq[16] = vol;
	vol = 200 - m_skoutei.GetPos();
	if (vol != savedata.eq[17]) { s.Format(L"%d", vol); m_vkoutei.SetWindowText(s); }
	savedata.eq[17] = vol;
	vol = 200 - m_smitsudo.GetPos();
	if (vol != savedata.eq[18]) { s.Format(L"%d", vol); m_vmitsudo.SetWindowText(s); }
	savedata.eq[18] = vol;
	vol = 200 - m_srittai.GetPos();
	if (vol != savedata.eq[19]) { s.Format(L"%d", vol); m_vrittai.SetWindowText(s); }
	savedata.eq[19] = vol;

	vol = 200 - m_reverb.GetPos();
	if (vol != savedata.eq_reverb) { s.Format(L"%d", vol); m_reverbi.SetWindowText(s); }
	savedata.eq_reverb = vol;
	vol = 200 - m_chorus.GetPos();
	if (vol != savedata.eq_chorus) { s.Format(L"%d", vol); m_chorusi.SetWindowText(s); }
	savedata.eq_chorus = vol;
	vol = 200 - m_delay.GetPos();
	if (vol != savedata.eq_delay) { s.Format(L"%d", vol); m_delayi.SetWindowText(s); }
	savedata.eq_delay = vol;

	vol = m_eff.GetPos();
	if(vol / 2 != savedata.eqsoundeffect) { s.Format(L"%d", vol); m_seff.SetWindowText(s); }
	savedata.eqsoundeffect = vol / 2;

	vol = m_surround.GetPos();
	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;
	if (vol != savedata.surround) { s.Format(L"%d", vol); m_surroundVal.SetWindowText(s); }
	savedata.surround = vol;


	CRect rect;
	GetWindowRect(rect);
	savedata.eqx = rect.left;
	savedata.eqy = rect.top;

	extern int playf;
	if (playf == 0)
		ApplyKeyCodesUi();

	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CEqualizer::OnBnClickedOk3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eq[0] = 100;
	savedata.eq[1] = 100;
	savedata.eq[2] = 100;
	savedata.eq[3] = 100;
	savedata.eq[4] = 100;
	savedata.eq[5] = 100;
	savedata.eq[6] = 100;
	savedata.eq[7] = 100;
	savedata.eq[8] = 100;
	savedata.eq[9] = 100;
	savedata.eq[10] = 100;
	savedata.eq[11] = 100;
	savedata.eq[12] = 100;
	savedata.eq[13] = 100;
	savedata.eq[14] = 100;
	m_s0.SetPos(200 - savedata.eq[0]);
	m_s1.SetPos(200 - savedata.eq[1]);
	m_s2.SetPos(200 - savedata.eq[2]);
	m_s3.SetPos(200 - savedata.eq[3]);
	m_s4.SetPos(200 - savedata.eq[4]);
	m_s5.SetPos(200 - savedata.eq[5]);
	m_s6.SetPos(200 - savedata.eq[6]);
	m_s7.SetPos(200 - savedata.eq[7]);
	m_s8.SetPos(200 - savedata.eq[8]);
	m_s9.SetPos(200 - savedata.eq[9]);
	m_s10.SetPos(200 - savedata.eq[10]);
	m_s11.SetPos(200 - savedata.eq[11]);
	m_s12.SetPos(200 - savedata.eq[12]);
	m_s13.SetPos(200 - savedata.eq[13]);
	m_s14.SetPos(200 - savedata.eq[14]);
	CString s;
	s.Format(L"%d", savedata.eq[0]);
	m_v0.SetWindowText(s);
	s.Format(L"%d", savedata.eq[1]);
	m_v1.SetWindowText(s);
	s.Format(L"%d", savedata.eq[2]);
	m_v2.SetWindowText(s);
	s.Format(L"%d", savedata.eq[3]);
	m_v3.SetWindowText(s);
	s.Format(L"%d", savedata.eq[4]);
	m_v4.SetWindowText(s);
	s.Format(L"%d", savedata.eq[5]);
	m_v5.SetWindowText(s);
	s.Format(L"%d", savedata.eq[6]);
	m_v6.SetWindowText(s);
	s.Format(L"%d", savedata.eq[7]);
	m_v7.SetWindowText(s);
	s.Format(L"%d", savedata.eq[8]);
	m_v8.SetWindowText(s);
	s.Format(L"%d", savedata.eq[9]);
	m_v9.SetWindowText(s);
	s.Format(L"%d", savedata.eq[10]);
	m_v10.SetWindowText(s);
	s.Format(L"%d", savedata.eq[11]);
	m_v11.SetWindowText(s);
	s.Format(L"%d", savedata.eq[12]);
	m_v12.SetWindowText(s);
	s.Format(L"%d", savedata.eq[13]);
	m_v13.SetWindowText(s);
	s.Format(L"%d", savedata.eq[14]);
	m_v14.SetWindowText(s);
}

BOOL CEqualizer::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基底クラスを呼び出してください。
	m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CEqualizer::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eqwindow = 0;
	DestroyWindow();

}

void CEqualizer::OnBnClickedOk4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eq[15] = 100;
	savedata.eq[16] = 100;
	savedata.eq[17] = 100;
	savedata.eq[18] = 100;
	savedata.eq[19] = 100;
	savedata.eq_reverb = 0;   // 0 = オフ
	savedata.eq_chorus = 0;   // 0 = オフ
	savedata.eq_delay = 0;    // 0 = オフ
	m_smaster.SetPos(200 - savedata.eq[15]);
	m_ssenmei.SetPos(200 - savedata.eq[16]);
	m_skoutei.SetPos(200 - savedata.eq[17]);
	m_smitsudo.SetPos(200 - savedata.eq[18]);
	m_srittai.SetPos(200 - savedata.eq[19]);

	m_reverb.SetPos(200 - savedata.eq_reverb);
	m_chorus.SetPos(200 - savedata.eq_chorus);
	m_delay.SetPos(200 - savedata.eq_delay);
	CString s;
	s.Format(L"%d", savedata.eq[15]);
	m_vmaster.SetWindowText(s);
	s.Format(L"%d", savedata.eq[16]);
	m_vsenmei.SetWindowText(s);
	s.Format(L"%d", savedata.eq[17]);
	m_vkoutei.SetWindowText(s);
	s.Format(L"%d", savedata.eq[18]);
	m_vmitsudo.SetWindowText(s);
	s.Format(L"%d", savedata.eq[19]);
	m_vrittai.SetWindowText(s);
	s.Format(L"%d", savedata.eq_reverb);
	m_reverbi.SetWindowText(s);
	s.Format(L"%d", savedata.eq_chorus);
	m_chorusi.SetWindowText(s);
	s.Format(L"%d", savedata.eq_delay);
	m_delayi.SetWindowText(s);
}


void CEqualizer::OnBnClickedAbA()
{
	ProAudio_AbCapture(0);
}

void CEqualizer::OnBnClickedAbB()
{
	ProAudio_AbCapture(1);
}

void CEqualizer::OnBnClickedAbTog()
{
	ProAudio_AbToggle();
	SyncSlidersFromSavedata();
	if (m_pre.GetSafeHwnd()) m_pre.SetCurSel(savedata.eqsoundeq);
	if (m_env.GetSafeHwnd()) m_env.SetCurSel(savedata.eqsoundenv);
}
