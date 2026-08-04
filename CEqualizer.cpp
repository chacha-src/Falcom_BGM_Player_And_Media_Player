// CEqualizer.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CEqualizer.h"
#include "ProAudio.h"
#include "CPromptEngine.h"



// CEqualizer ダイアログ

namespace {

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
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
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
		L"· EQ sıfırla / genel sıfırla …… yalnızca bantlar veya tümü")); y += lh + 2;
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

	dc.SelectObject(oldFont);
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
	ON_CBN_SELCHANGE(IDC_COMBO1, &CEqualizer::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO5, &CEqualizer::OnCbnSelchangeCombo5)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_MESSAGE(WM_EQ_KEY_UPDATE, &CEqualizer::OnEqKeyUpdate)
	ON_BN_CLICKED(IDOK3, &CEqualizer::OnBnClickedOk3)
	ON_BN_CLICKED(IDOK, &CEqualizer::OnBnClickedOk)
	ON_BN_CLICKED(IDOK4, &CEqualizer::OnBnClickedOk4)
	ON_BN_CLICKED(IDC_EQ_ABA, &CEqualizer::OnBnClickedAbA)
	ON_BN_CLICKED(IDC_EQ_ABB, &CEqualizer::OnBnClickedAbB)
	ON_BN_CLICKED(IDC_EQ_ABTOG, &CEqualizer::OnBnClickedAbTog)
	ON_BN_CLICKED(IDC_EQ_HELP, &CEqualizer::OnBnClickedHelp)
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
	m_s0.SetMode(1);
	m_s1.SetMode(1);
	m_s2.SetMode(1);
	m_s3.SetMode(1);
	m_s4.SetMode(1);
	m_s5.SetMode(1);
	m_s6.SetMode(1);
	m_s7.SetMode(1);
	m_s8.SetMode(1);
	m_s9.SetMode(1);
	m_s10.SetMode(1);
	m_s11.SetMode(1);
	m_s12.SetMode(1);
	m_s13.SetMode(1);
	m_s14.SetMode(1);


	m_smaster.SetMode(2);
	m_ssenmei.SetMode(2);
	m_skoutei.SetMode(2);
	m_smitsudo.SetMode(2);
	m_srittai.SetMode(2);

	m_reverb.SetMode(2);
	m_chorus.SetMode(2);
	m_delay.SetMode(2);


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
	m_env.AddString(LL14(L"なし", L"None", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"لا شيء", L"Нет", L"Keiner", L"Nenhum", L"Geen", L"Brak", L"Yok"));
	//1
	m_env.AddString(LL14(L"--[[基本空間 1-10]]--", L"--[[Basic space 1-10]]--", L"--[[Espace de base 1-10]]--", L"--[[Spazio base 1-10]]--", L"--[[Espacio básico 1-10]]--", L"--[[기본 공간 1-10]]--", L"--[[基本空间 1-10]]--", L"--[[المساحة الأساسية 1-10]]--", L"--[[Базовое пространство 1-10]]--", L"--[[Grundraum 1-10]]--", L"--[[Espaço básico 1-10]]--", L"--[[Basisruimte 1-10]]--", L"--[[Przestrzeń podstawowa 1-10]]--", L"--[[Temel alan 1-10]]--"), TRUE);
	//2
	m_env.AddString(LL14(L"風呂場 (超短く超明るい、ピーキーな金属反射)", L"Bathroom (very short, bright, peaky metal reflection)", L"Salle de bain (très court, lumineux, réflexion métallique)", L"Bagno (molto corto, brillante, riflessione metallica)", L"Baño (muy corto, brillante, reflexión metálica)", L"욕실 (매우 짧고 밝은 금속 반사)", L"浴室（极短、明亮、金属反射）", L"حمام (قصير جداً، ساطع، انعكاس معدني)", L"Ванная (очень короткий, яркий, металлический отзвук)", L"Badezimmer (sehr kurz, hell, metallische Reflexion)", L"Banheiro (muito curto, brilhante, reflexão metálica)", L"Badkamer (zeer kort, helder, metalen reflectie)", L"Łazienka (bardzo krótki, jasny, metaliczna refleksja)", L"Banyo (çok kısa, parlak, metalik yansıma)"));
	m_env.AddString(LL14(L"ホール (中程度だがはっきり響く、バランス良好)", L"Hall (moderate but clear, balanced)", L"Salle (modérée mais claire, équilibrée)", L"Sala (moderata ma chiara, equilibrata)", L"Sala (moderada pero clara, equilibrada)", L"홀 (적당하지만 선명하고 균형 잡힘)", L"大厅（适度清晰、均衡）", L"قاعة (معتدلة لكن واضحة، متوازنة)", L"Зал (умеренный, чёткий, сбалансированный)", L"Saal (mäßig aber klar, ausgewogen)", L"Sala (moderado mas claro, equilibrado)", L"Zaal (gematigd maar helder, gebalanceerd)", L"Sala (umiarkowana, czysta, zbalansowana)", L"Salon (dengeli, net)"));
	m_env.AddString(LL14(L"教会 (超長く超重厚、圧倒的な残響)", L"Church (very long, massive reverb)", L"Église (très long, réverbération massive)", L"Chiesa (molto lunga, riverbero massiccio)", L"Iglesia (muy larga, reverb masiva)", L"교회 (매우 길고 웅장한 잔향)", L"教堂（超长厚重、强烈混响）", L"كنيسة (طويلة جداً، صدى ضخم)", L"Церковь (очень длинная, массивная реверберация)", L"Kirche (sehr lang, massive Hall)", L"Igreja (muito longa, reverb massiva)", L"Kerk (zeer lang, massieve nagalm)", L"Kościół (bardzo długi, masywna pogłos)", L"Kilise (çok uzun, yoğun yankı)"));
	m_env.AddString(LL14(L"洞窟 (長く暗く湿った、こもった響き)", L"Cave (long, dark, wet, muffled)", L"Grotte (longue, sombre, humide, étouffée)", L"Grotta (lunga, scura, umida, ovattata)", L"Cueva (larga, oscura, húmeda, apagada)", L"동굴 (길고 어둡고 축축한 웅웅거림)", L"洞穴（长、暗、湿、闷响）", L"كهف (طويل، مظلم، رطب، مكتوم)", L"Пещера (длинная, тёмная, влажная, глухая)", L"Höhle (lang, dunkel, nass, gedämpft)", L"Caverna (longa, escura, úmida, abafada)", L"Grot (lang, donker, nat, gedempt)", L"Jaskinia (długa, ciemna, mokra, stłumiona)", L"Mağara (uzun, karanlık, ıslak, boğuk)"));
	m_env.AddString(LL14(L"スタジオ (極めてドライ、ほぼ無響)", L"Studio (very dry, nearly anechoic)", L"Studio (très sec, quasi anéchoïque)", L"Studio (molto secco, quasi anecoico)", L"Estudio (muy seco, casi anecoico)", L"스튜디오 (매우 드라이, 거의 무향)", L"录音室（极干、近无响）", L"استوديو (جاف جداً، شبه خالٍ من الصدى)", L"Студия (очень сухая, почти без реверберации)", L"Studio (sehr trocken, fast schalltot)", L"Estúdio (muito seco, quase anecóico)", L"Studio (zeer droog, bijna echovrij)", L"Studio (bardzo suche, prawie bezechowe)", L"Stüdyo (çok kuru, neredeyse yankısız)"));
	m_env.AddString(LL14(L"ライブハウス (パンチがあって賑やか、エネルギッシュ)", L"Live house (punchy, lively, energetic)", L"Salle de concert (dynamique, vivant, énergique)", L"Live club (punchy, vivace, energico)", L"Sala de conciertos (punchy, animada, energética)", L"라이브 하우스 (펀치감 있고 활기참)", L"现场演出厅（有力、活泼、充满能量）", L"صالة حفلات (قوية، حية، نشطة)", L"Клуб (насыщенный, живой, энергичный)", L"Live-Haus (dynamisch, lebhaft, energisch)", L"Casa de shows (impactante, animada, energética)", L"Live venue (krachtig, levendig, energiek)", L"Sala koncertowa (dynamiczna, żywa, energetyczna)", L"Canlı mekan (güçlü, canlı, enerjik)"));
	m_env.AddString(LL14(L"森 (拡散的で柔らかい、包み込む自然)", L"Forest (diffuse, soft, enveloping)", L"Forêt (diffuse, douce, enveloppante)", L"Foresta (diffusa, morbida, avvolgente)", L"Bosque (difuso, suave, envolvente)", L"숲 (확산적이고 부드러운 자연)", L"森林（扩散、柔和、包围感）", L"غابة (منتشرة، ناعمة، محيطة)", L"Лес (рассеянный, мягкий, enveloping)", L"Wald (diffus, weich, einhüllend)", L"Floresta (difusa, suave, envolvente)", L"Bos (diffuus, zacht, omhullend)", L"Las (rozproszony, miękki, otulający)", L"Orman (dağınık, yumuşak, saran)"));
	m_env.AddString(LL14(L"山 (超長いエコー、遠くまではっきり響く)", L"Mountain (long echo, clear far)", L"Montagne (long écho, clair au loin)", L"Montagna (lungo eco, chiaro in lontananza)", L"Montaña (eco largo, claro a distancia)", L"산 (긴 에코, 먼 곳까지 선명)", L"山岳（超长回声、远传清晰）", L"جبل (صدى طويل، واضح من بعيد)", L"Гора (длинное эхо, чёткое вдали)", L"Berg (langes Echo, klar in der Ferne)", L"Montanha (eco longo, claro ao longe)", L"Berg (lang nagalm, helder ver)", L"Góra (długie echo, wyraźne w oddali)", L"Dağ (uzun yankı, uzakta net)"));
	m_env.AddString(LL14(L"広場 (開放的、空気を感じる広がり)", L"Plaza (open, airy)", L"Place (ouverte, aérée)", L"Piazza (aperta, ariosa)", L"Plaza (abierta, aireada)", L"광장 (열린, 통기성 좋음)", L"广场（开放、空气流通）", L"ساحة (مفتوحة، جيدة التهوية)", L"Площадь (открытая, воздушная)", L"Platz (offen, luftig)", L"Praça (aberta, arejada)", L"Plein (open, luchtig)", L"Plac (otwarty, przewiewny)", L"Meydan (açık, havadar)"));
	m_env.AddString(LL14(L"カテドラル (超巨大空間、圧倒的な残響と重厚感)", L"Cathedral (huge space, massive reverb)", L"Cathédrale (immense espace, réverb massive)", L"Cattedrale (spazio enorme, riverbero massiccio)", L"Catedral (espacio enorme, reverb masiva)", L"대성당 (거대한 공간, 압도적 잔향)", L"大教堂（巨大空间、强烈混响）", L"كاتدرائية (مساحة ضخمة، صدى ضخم)", L"Собор (огромное пространство, массивная реверберация)", L"Kathedrale (riesiger Raum, massiver Hall)", L"Catedral (espaço enorme, reverb massiva)", L"Kathedraal (enorme ruimte, massieve nagalm)", L"Katedra (ogromna przestrzeń, masywna pogłos)", L"Katedral (devasa alan, yoğun yankı)"));
	//12
	m_env.AddString(LL14(L"--[[公共施設 11-20]]--", L"--[[Public 11-20]]--", L"--[[Public 11-20]]--", L"--[[Pubblico 11-20]]--", L"--[[Público 11-20]]--", L"--[[공공시설 11-20]]--", L"--[[公共设施 11-20]]--", L"--[[عامة 11-20]]--", L"--[[Публичное 11-20]]--", L"--[[Öffentlich 11-20]]--", L"--[[Público 11-20]]--", L"--[[Openbaar 11-20]]--", L"--[[Publiczny 11-20]]--", L"--[[Kamu 11-20]]--"), TRUE);
	//13
	m_env.AddString(LL14(L"体育館 (硬く金属的、バスケコート的な響き)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnase (dur, métallique, réflexion type terrain de basket)", L"Palestra (dura, metallica, riflesso stile campo da basket)", L"Gimnasio (duro, metálico, reflexión estilo cancha de baloncesto)", L"체육관 (단단하고 금속적, 농구 코트식 반향)", L"体育馆（硬质金属感、篮球场式反射）", L"صالة رياضية (صلبة معدنية، انعكاس شبيه بملعب كرة سلة)", L"Спортзал (жёсткий, металлический, отражения как на баскетбольной площадке)", L"Turnhalle (hart, metallisch, Basketballplatz-Reflexion)", L"Ginásio (duro, metálico, reflexo estilo quadra de basquete)", L"Sporthal (hard, metalen, basketbalveld-reflectie)", L"Hala sportowa (twarda, metaliczna, odbicia jak na boisku koszykówki)", L"Spor salonu (sert, metalik, basket sahası tarzı yansıma)"));
	m_env.AddString(LL14(L"峡谷 (複数の明確なエコー、両側から反響)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (échos multiples distincts, réflexions des deux côtés)", L"Canyon (echi multipli distinti, riflessi da entrambi i lati)", L"Cañón (ecos múltiples distintos, reflexiones de ambos lados)", L"협곡 (여러 개의 뚜렷한 에코, 양쪽 반향)", L"峡谷（多个清晰回声、两侧反射）", L"وادي (صدى متعدد واضح، انعكاسات من الجانبين)", L"Каньон (множественные чёткие эхо, отражения с обеих сторон)", L"Schlucht (mehrere deutliche Echos, Reflexionen von beiden Seiten)", L"Cânion (ecos múltiplos distintos, reflexos de ambos os lados)", L"Canyon (meerdere duidelijke echo's, reflecties van beide kanten)", L"Kanion (wiele wyraźnych ech, odbicia z obu stron)", L"Kanyon (birden fazla belirgin yankı, her iki taraftan yansımalar)"));
	m_env.AddString(LL14(L"地下室 (狭く圧迫感、密度の高い反射)", L"Basement (cramped, dense reflections)", L"Sous-sol (étroit, réflexions denses)", L"Seminterrato (angusto, riflessioni dense)", L"Sótano (estrecho, reflexiones densas)", L"지하실 (좁고 압박감, 밀도 높은 반사)", L"地下室（狭窄压迫、密集反射）", L"قبو (ضيق، انعكاسات كثيفة)", L"Подвал (тесный, плотные отражения)", L"Keller (eng, dichte Reflexionen)", L"Porão (apertado, reflexos densos)", L"Kelder (krap, dichte reflecties)", L"Piwnica (ciasna, gęste odbicia)", L"Bodrum (dar, yoğun yansımalar)"));
	m_env.AddString(LL14(L"劇場 (音響設計された空間、明瞭でバランス良好)", L"Theater (acoustically designed space, clear and balanced)", L"Théâtre (espace acoustiquement conçu, clair et équilibré)", L"Teatro (spazio progettato acusticamente, chiaro ed equilibrato)", L"Teatro (espacio diseñado acústicamente, claro y equilibrado)", L"극장 (음향 설계된 공간, 선명하고 균형 잡힘)", L"剧院（声学设计空间、清晰均衡）", L"مسرح (مساحة مصممة صوتياً، واضحة ومتوازنة)", L"Театр (акустически спроектированное пространство, чёткое и сбалансированное)", L"Theater (akustisch gestalteter Raum, klar und ausgewogen)", L"Teatro (espaço projetado acusticamente, claro e equilibrado)", L"Theater (akoestisch ontworpen ruimte, helder en gebalanceerd)", L"Teatr (przestrzeń zaprojektowana akustycznie, czysta i zbalansowana)", L"Tiyatro (akustik tasarımlı alan, net ve dengeli)"));
	m_env.AddString(LL14(L"水中 (特殊な密度、こもった独特の響き)", L"Underwater (unique density, muffled distinctive sound)", L"Sous l'eau (densité unique, son étouffé distinctif)", L"Sott'acqua (densità unica, suono ovattato distintivo)", L"Bajo el agua (densidad única, sonido apagado distintivo)", L"수중 (특수한 밀도, 먹먹한 독특한 소리)", L"水下（特殊密度、闷而独特的声响）", L"تحت الماء (كثافة فريدة، صوت مكتوم مميز)", L"Под водой (уникальная плотность, приглушённый характерный звук)", L"Unter Wasser (einzigartige Dichte, gedämpfter charakteristischer Klang)", L"Subaquático (densidade única, som abafado distintivo)", L"Onder water (unieke dichtheid, gedempt onderscheidend geluid)", L"Pod wodą (unikalna gęstość, stłumiony charakterystyczny dźwięk)", L"Su altı (eşsiz yoğunluk, boğuk ayırt edici ses)"));
	m_env.AddString(LL14(L"トンネル/地下道 (フラッターエコー、平行壁面の連続反射)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Souterrain (flutter echo, réflexions de parois parallèles)", L"Tunnel/Sotterraneo (flutter echo, riflessi di pareti parallele)", L"Túnel/Subterráneo (flutter echo, reflexiones de paredes paralelas)", L"터널/지하도 (플러터 에코, 평행 벽면 연속 반사)", L"隧道/地下通道（颤动回声、平行壁面连续反射）", L"نفق/تحت الأرض (صدى رفرفة، انعكاسات جدران متوازية)", L"Туннель/Подземный переход (флаттер-эхо, отражения параллельных стен)", L"Tunnel/Untergrund (Flutter-Echo, parallele Wandreflexionen)", L"Túnel/Subterrâneo (flutter echo, reflexos de paredes paralelas)", L"Tunnel/Ondergronds (flutter-echo, reflecties van parallelle muren)", L"Tunel/Podziemie (flutter echo, odbicia równoległych ścian)", L"Tünel/Yeraltı (flutter yankı, paralel duvar yansımaları)"));
	m_env.AddString(LL14(L"アリーナ/ドーム (超巨大スポーツ施設、観客席の吸音と長残響)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arène/Dôme (immense installation sportive, absorption du public et longue réverb)", L"Arena/Domo (enorme impianto sportivo, assorbimento del pubblico e lungo riverbero)", L"Arena/Domo (enorme instalación deportiva, absorción del público y reverb larga)", L"아레나/돔 (초대형 스포츠 시설, 관중 흡음과 긴 잔향)", L"竞技场/穹顶（超大型体育设施、观众吸音与长混响）", L"ساحة/قبة (منشأة رياضية ضخمة، امتصاص الجمهور وصدى طويل)", L"Арена/Купол (огромный спортивный объект, поглощение зрителей и длинная реверберация)", L"Arena/Kuppel (riesige Sportanlage, Publikumsabsorption und langer Hall)", L"Arena/Cúpula (enorme instalação esportiva, absorção do público e reverb longa)", L"Arena/Koepel (enorme sportfaciliteit, publieksabsorptie en lange nagalm)", L"Arena/Kopuła (ogromny obiekt sportowy, absorpcja publiczności i długa pogłos)", L"Arena/Kubbe (dev spor tesisi, seyirci emilimi ve uzun yankı)"));
	m_env.AddString(LL14(L"小部屋/クローゼット (超小空間デッド、ほぼ無反射)", L"Small room/Closet (very dead space, nearly no reflection)", L"Petite pièce/Placard (espace très mort, presque sans réflexion)", L"Stanzetta/Armadio (spazio molto morto, quasi senza riflessi)", L"Habitación pequeña/Armario (espacio muy muerto, casi sin reflexión)", L"작은 방/옷장 (초소형 데드 공간, 거의 무반사)", L"小房间/壁橱（极干空间、几乎无反射）", L"غرفة صغيرة/خزانة (مساحة ميتة جداً، تقريباً بلا انعكاس)", L"Малая комната/Шкаф (очень мёртвое пространство, почти без отражений)", L"Kleiner Raum/Schrank (sehr toter Raum, fast keine Reflexion)", L"Quarto pequeno/Armário (espaço muito morto, quase sem reflexo)", L"Kleine kamer/Kast (zeer dode ruimte, bijna geen reflectie)", L"Mały pokój/Szafa (bardzo martwa przestrzeń, prawie bez odbić)", L"Küçük oda/Dolap (çok kuru alan, neredeyse yansımasız)"));
	m_env.AddString(LL14(L"階段室 (縦方向の特殊反射、螺旋的な響き)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Cage d'escalier (réflexions verticales, réverb en spirale)", L"Van scala (riflessi verticali, riverbero a spirale)", L"Hueco de escalera (reflexiones verticales, reverb en espiral)", L"계단실 (수직 반사, 나선형 잔향)", L"楼梯间（垂直反射、螺旋式混响）", L"بئر الدرج (انعكاسات عمودية، صدى حلزوني)", L"Лестничная клетка (вертикальные отражения, спиралевидная реверберация)", L"Treppenhaus (vertikale Reflexionen, spiralförmiger Hall)", L"Caixa de escada (reflexos verticais, reverb em espiral)", L"Trappenhuis (verticale reflecties, spiraalvormige nagalm)", L"Klatka schodowa (pionowe odbicia, spiralna pogłos)", L"Merdiven boşluğu (dikey yansımalar, spiral yankı)"));
	m_env.AddString(LL14(L"地下鉄ホーム (都市的コンクリート、硬質な反射)", L"Subway platform (urban concrete, hard reflections)", L"Quai de métro (béton urbain, réflexions dures)", L"Binario della metro (cemento urbano, riflessi duri)", L"Andén de metro (hormigón urbano, reflexiones duras)", L"지하철 승강장 (도시적 콘크리트, 경질 반사)", L"地铁站台（都市混凝土、硬质反射）", L"رصيف مترو (خرسانة حضرية، انعكاسات صلبة)", L"Платформа метро (городской бетон, жёсткие отражения)", L"U-Bahn-Bahnsteig (städtischer Beton, harte Reflexionen)", L"Plataforma de metrô (concreto urbano, reflexos duros)", L"Metroperron (stedelijk beton, harde reflecties)", L"Peron metra (miejski beton, twarde odbicia)", L"Metro platformu (kentsel beton, sert yansımalar)"));

	m_env.AddString(LL14(L"--[[産業・商業 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industriel et commercial 21-30]]--", L"--[[Industriale e commerciale 21-30]]--", L"--[[Industrial y comercial 21-30]]--", L"--[[산업·상업 21-30]]--", L"--[[产业·商业 21-30]]--", L"--[[صناعي وتجاري 21-30]]--", L"--[[Промышленность и торговля 21-30]]--", L"--[[Industrie und Handel 21-30]]--", L"--[[Industrial e comercial 21-30]]--", L"--[[Industrieel en commercieel 21-30]]--", L"--[[Przemysł i handel 21-30]]--", L"--[[Endüstriyel ve ticari 21-30]]--"), TRUE);

	m_env.AddString(LL14(L"倉庫 (大きく空っぽ、高天井で硬い床)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Entrepôt (grand et vide, plafond haut, sol dur)", L"Magazzino (grande e vuoto, soffitto alto, pavimento duro)", L"Almacén (grande y vacío, techo alto, suelo duro)", L"창고 (크고 비어 있음, 높은 천장과 단단한 바닥)", L"仓库（大而空、高天花板、硬地板）", L"مستودع (كبير وفارغ، سقف مرتفع، أرضية صلبة)", L"Склад (большой и пустой, высокий потолок, твёрдый пол)", L"Lager (groß und leer, hohe Decke, harter Boden)", L"Armazém (grande e vazio, teto alto, piso duro)", L"Magazijn (groot en leeg, hoog plafond, harde vloer)", L"Magazyn (duży i pusty, wysoki sufit, twarda podłoga)", L"Depo (büyük ve boş, yüksek tavan, sert zemin)"));
	m_env.AddString(LL14(L"廊下 (長く狭い直線的、方向性のある反響)", L"Corridor (long narrow linear, directional reflections)", L"Couloir (long et étroit, réflexions directionnelles)", L"Corridoio (lungo e stretto, riflessioni direzionali)", L"Pasillo (largo y estrecho, reflexiones direccionales)", L"복도 (길고 좁은 직선형, 방향성 반향)", L"走廊（狭长直线、方向性反射）", L"ممر (طويل ضيق خطي، انعكاسات اتجاهية)", L"Коридор (длинный узкий, направленные отражения)", L"Flur (lang schmal linear, gerichtete Reflexionen)", L"Corredor (longo e estreito, reflexos direcionais)", L"Gang (lang smal lineair, directionele reflecties)", L"Korytarz (długi wąski liniowy, kierunkowe odbicia)", L"Koridor (uzun dar doğrusal, yönlü yansımalar)"));
	m_env.AddString(LL14(L"工場 (金属的産業的、複雑な反響)", L"Factory (metallic industrial, complex reflections)", L"Usine (industrielle métallique, réflexions complexes)", L"Fabbrica (industriale metallica, riflessioni complesse)", L"Fábrica (industrial metálica, reflexiones complejas)", L"공장 (금속적 산업적, 복잡한 반향)", L"工厂（金属工业感、复杂反射）", L"مصنع (صناعي معدني، انعكاسات معقدة)", L"Завод (металлический промышленный, сложные отражения)", L"Fabrik (metallisch-industriell, komplexe Reflexionen)", L"Fábrica (industrial metálica, reflexos complexos)", L"Fabriek (metalen industrieel, complexe reflecties)", L"Fabryka (metaliczna przemysłowa, złożone odbicia)", L"Fabrika (metalik endüstriyel, karmaşık yansımalar)"));
	m_env.AddString(LL14(L"寺社仏閣 (木造の温かみ、柔らかい反射)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Sanctuaire (chaleur du bois, réflexions douces)", L"Tempio/Santuario (calore del legno, riflessi morbidi)", L"Templo/Santuario (calidez de madera, reflexiones suaves)", L"사찰/신사 (목조의 따뜻함, 부드러운 반사)", L"寺庙/神社（木制温暖、柔和反射）", L"معبد/ضريح (دفء خشبي، انعكاسات ناعمة)", L"Храм/Святилище (деревянное тепло, мягкие отражения)", L"Tempel/Schrein (holzige Wärme, weiche Reflexionen)", L"Templo/Santuário (calor de madeira, reflexos suaves)", L"Tempel/Heiligdom (houten warmte, zachte reflecties)", L"Świątynia/Kapliczka (drewniane ciepło, miękkie odbicia)", L"Tapınak/Türbe (ahşap sıcaklık, yumuşak yansımalar)"));
	m_env.AddString(LL14(L"宇宙空間 (SF特殊空間、無重力感と極端な残響)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Espace cosmique (espace SF spécial, sensation d'apesanteur et réverb extrême)", L"Spazio cosmico (spazio SF speciale, sensazione di assenza di gravità e riverbero estremo)", L"Espacio exterior (espacio SF especial, sensación de gravedad cero y reverb extrema)", L"우주 공간 (SF 특수 공간, 무중력감과 극단적 잔향)", L"宇宙空间（SF特殊空间、失重感与极端混响）", L"الفضاء الخارجي (مساحة SF خاصة، إحساس انعدام الجاذبية وصدى شديد)", L"Космос (особое SF-пространство, ощущение невесомости и экстремальная реверберация)", L"Weltraum (spezieller SF-Raum, Schwerelosigkeitsgefühl und extremer Hall)", L"Espaço sideral (espaço SF especial, sensação de gravidade zero e reverb extrema)", L"Buitenruimte (speciale SF-ruimte, gewichtloos gevoel en extreme nagalm)", L"Kosmos (specjalna przestrzeń SF, uczucie nieważkości i ekstremalna pogłos)", L"Uzay boşluğu (SF özel alan, sıfır yerçekimi hissi ve aşırı yankı)"));
	m_env.AddString(LL14(L"野球場/サッカー場 (屋外超大型、遠距離反射と開放感)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Terrain de baseball/football (grande échelle extérieure, réflexions lointaines et ouverture)", L"Campo da baseball/calcio (grande scala all'aperto, riflessi distanti e apertura)", L"Campo de béisbol/fútbol (gran escala al aire libre, reflexiones lejanas y apertura)", L"야구장/축구장 (야외 초대형, 원거리 반사와 개방감)", L"棒球场/足球场（户外超大型、远距离反射与开阔感）", L"ملعب بيسبول/كرة قدم (مساحة خارجية ضخمة، انعكاسات بعيدة وانفتاح)", L"Бейсбольное/Футбольное поле (огромный открытый масштаб, дальние отражения и простор)", L"Baseball-/Fußballplatz (großer Außenmaßstab, ferne Reflexionen und Offenheit)", L"Campo de beisebol/futebol (grande escala externa, reflexos distantes e abertura)", L"Honkbal-/Voetbalveld (grote buitenschaal, verre reflecties en openheid)", L"Boisko baseballowe/piłkarskie (duża skala na zewnątrz, odległe odbicia i otwartość)", L"Beyzbol/Futbol sahası (açık hava büyük ölçek, uzak yansımalar ve açıklık)"));
	m_env.AddString(LL14(L"図書館 (静寂で吸音的、控えめな反射)", L"Library (quiet and absorbent, subtle reflections)", L"Bibliothèque (calme et absorbante, réflexions subtiles)", L"Biblioteca (silenziosa e assorbente, riflessi sottili)", L"Biblioteca (silenciosa y absorbente, reflexiones sutiles)", L"도서관 (고요하고 흡음적, 은은한 반사)", L"图书馆（安静吸音、微妙反射）", L"مكتبة (هادئة وامتصاصية، انعكاسات خفيفة)", L"Библиотека (тихая и поглощающая, тонкие отражения)", L"Bibliothek (ruhig und absorbierend, subtile Reflexionen)", L"Biblioteca (silenciosa e absorvente, reflexos sutis)", L"Bibliotheek (rustig en absorberend, subtiele reflecties)", L"Biblioteka (cicha i chłonna, subtelne odbicia)", L"Kütüphane (sessiz ve emici, hafif yansımalar)"));
	m_env.AddString(LL14(L"プール(室内) (タイル水面反射、独特の明るい響き)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Piscine (intérieure) (réflexions carrelées et aquatiques, son lumineux unique)", L"Piscina (interna) (riflessi di piastrelle e acqua, suono luminoso unico)", L"Piscina (interior) (reflexiones de azulejos y agua, sonido brillante único)", L"수영장(실내) (타일과 수면 반사, 독특한 밝은 소리)", L"游泳池（室内）（瓷砖与水面反射、独特明亮声响）", L"مسبح (داخلي) (انعكاسات بلاط وماء، صوت مشرق فريد)", L"Бассейн (крытый) (плиточные и водные отражения, уникальный яркий звук)", L"Schwimmbecken (Innen) (Fliesen- und Wasserreflexionen, einzigartiger heller Klang)", L"Piscina (interna) (reflexos de azulejos e água, som brilhante único)", L"Zwembad (binnen) (tegel- en waterreflecties, uniek helder geluid)", L"Basen (wewnętrzny) (kaflowe i wodne odbicia, unikalny jasny dźwięk)", L"Havuz (kapalı) (fayans ve su yansımaları, eşsiz parlak ses)"));
	m_env.AddString(LL14(L"エレベーター (超小金属空間、密閉された短い反射)", L"Elevator (tiny metal space, confined short reflections)", L"Ascenseur (minuscule espace métallique, courtes réflexions confinées)", L"Ascensore (minuscolo spazio metallico, brevi riflessi confinati)", L"Ascensor (minúsculo espacio metálico, reflexiones cortas confinadas)", L"엘리베이터 (초소형 금속 공간, 밀폐된 짧은 반사)", L"电梯（极小金属空间、密闭短反射）", L"مصعد (مساحة معدنية صغيرة، انعكاسات قصيرة محصورة)", L"Лифт (крошечное металлическое пространство, короткие замкнутые отражения)", L"Aufzug (winziger Metallraum, eingeschlossene kurze Reflexionen)", L"Elevador (minúsculo espaço metálico, reflexos curtos confinados)", L"Lift (kleine metalen ruimte, ingesloten korte reflecties)", L"Winda (maleńka metalowa przestrzeń, zamknięte krótkie odbicia)", L"Asansör (minik metal alan, kapalı kısa yansımalar)"));
	m_env.AddString(LL14(L"駐車場 (広い低天井コンクリート、硬質な反響)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking (béton large à plafond bas, réflexions dures)", L"Parcheggio (cemento ampio a soffitto basso, riflessi duri)", L"Aparcamiento (hormigón amplio de techo bajo, reflexiones duras)", L"주차장 (넓은 저천장 콘크리트, 경질 반향)", L"停车场（宽阔低天花板混凝土、硬质反射）", L"موقف سيارات (خرسانة واسعة سقف منخفض، انعكاسات صلبة)", L"Парковка (широкий бетон с низким потолком, жёсткие отражения)", L"Parkplatz (breiter Beton mit niedriger Decke, harte Reflexionen)", L"Estacionamento (concreto amplo de teto baixo, reflexos duros)", L"Parkeerplaats (breed beton met laag plafond, harde reflecties)", L"Parking (szeroki beton z niskim sufitem, twarde odbicia)", L"Otopark (geniş alçak tavanlı beton, sert yansımalar)"));

	m_env.AddString(LL14(L"--[[文化施設 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Établissements culturels 31-40]]--", L"--[[Strutture culturali 31-40]]--", L"--[[Instalaciones culturales 31-40]]--", L"--[[문화시설 31-40]]--", L"--[[文化设施 31-40]]--", L"--[[مرافق ثقافية 31-40]]--", L"--[[Культурные объекты 31-40]]--", L"--[[Kultureinrichtungen 31-40]]--", L"--[[Instalações culturais 31-40]]--", L"--[[Culturele voorzieningen 31-40]]--", L"--[[Obiekty kulturalne 31-40]]--", L"--[[Kültürel tesisler 31-40]]--"), TRUE);

	m_env.AddString(LL14(L"コンサートホール (クラシック用最高峰、精密な音響設計)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Salle de concert (sommet classique, conception acoustique précise)", L"Sala da concerto (apice classico, progettazione acustica precisa)", L"Sala de conciertos (cúspide clásica, diseño acústico preciso)", L"콘서트홀 (클래식 최고급, 정밀한 음향 설계)", L"音乐厅（古典巅峰、精密声学设计）", L"قاعة حفلات (ذروة كلاسيكية، تصميم صوتي دقيق)", L"Концертный зал (вершина классики, точная акустическая конструкция)", L"Konzertsaal (klassische Spitze, präzises akustisches Design)", L"Sala de concertos (ápice clássico, design acústico preciso)", L"Concertzaal (klassiek toppunt, precies akoestisch ontwerp)", L"Sala koncertowa (szczyt klasyki, precyzyjna konstrukcja akustyczna)", L"Konser salonu (klasik zirve, hassas akustik tasarım)"));
	m_env.AddString(LL14(L"ジャズクラブ (親密で温かい、程よい残響)", L"Jazz club (intimate and warm, moderate reverb)", L"Club de jazz (intime et chaleureux, réverb modérée)", L"Jazz club (intimo e caldo, riverbero moderato)", L"Club de jazz (íntimo y cálido, reverb moderada)", L"재즈 클럽 (친밀하고 따뜻함, 적당한 잔향)", L"爵士俱乐部（亲密温暖、适度混响）", L"نادي جاز (حميمي ودافئ، صدى معتدل)", L"Джаз-клуб (интимный и тёплый, умеренная реверберация)", L"Jazzclub (intim und warm, mäßiger Hall)", L"Clube de jazz (íntimo e quente, reverb moderada)", L"Jazzclub (intiem en warm, gematigde nagalm)", L"Klub jazzowy (intymny i ciepły, umiarkowana pogłos)", L"Caz kulübü (samimi ve sıcak, orta düzey yankı)"));
	m_env.AddString(LL14(L"カラオケボックス (小密室エンタメ、明るく賑やか)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Box karaoké (petit divertissement clos, lumineux et animé)", L"Box karaoke (intrattenimento chiuso piccolo, luminoso e vivace)", L"Sala de karaoke (entretenimiento cerrado pequeño, brillante y animada)", L"노래방 (작은 밀폐 엔터테인먼트, 밝고 활기참)", L"卡拉OK包厢（小型密闭娱乐、明亮热闹）", L"غرفة كarاوke (ترفيه مغلق صغير، مشرق وحيوي)", L"Караоке-бокс (малое закрытое развлечение, яркое и оживлённое)", L"Karaoke-Box (kleine geschlossene Unterhaltung, hell und lebhaft)", L"Box de karaokê (entretenimento fechado pequeno, brilhante e animado)", L"Karaokebox (kleine afgesloten entertainment, helder en levendig)", L"Boks karaoke (mała zamknięta rozrywka, jasna i żywa)", L"Karaoke odası (küçük kapalı eğlence, parlak ve canlı)"));
	m_env.AddString(LL14(L"映画館 (THX規格的、臨場感のある音響)", L"Movie theater (THX standard, immersive sound)", L"Salle de cinéma (norme THX, son immersif)", L"Cinema (standard THX, suono immersivo)", L"Sala de cine (estándar THX, sonido envolvente)", L"영화관 (THX 규격, 몰입형 음향)", L"电影院（THX标准、沉浸式音响）", L"دار سينما (معيار THX، صوت غامر)", L"Кинотеатр (стандарт THX, объёмный звук)", L"Kino (THX-Standard, immersiver Klang)", L"Cinema (padrão THX, som imersivo)", L"Bioscoop (THX-standaard, meeslepend geluid)", L"Kino (standard THX, dźwięk immersyjny)", L"Sinema (THX standardı, sürükleyici ses)"));
	m_env.AddString(LL14(L"地下鉄車内 (揺れる密室、硬質で圧迫感)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Wagon de métro (espace clos secoué, dur et oppressant)", L"Vagone della metro (spazio chiuso tremolante, duro e opprimente)", L"Vagón de metro (espacio cerrado que tiembla, duro y opresivo)", L"지하철 차내 (흔들리는 밀폐 공간, 경질, 압박감)", L"地铁车厢（摇晃密闭空间、硬质压迫感）", L"عربة مترو (مساحة مغلقة مهتزة، صلبة وقهرية)", L"Вагон метро (трясущееся замкнутое пространство, жёсткое и угнетающее)", L"U-Bahn-Wagen (wackelnder geschlossener Raum, hart und bedrückend)", L"Vagão de metrô (espaço fechado tremendo, duro e opressivo)", L"Metrowagen (schuddende afgesloten ruimte, hard en benauwend)", L"Wagon metra (drgająca zamknięta przestrzeń, twarda i duszna)", L"Metro vagonu (sallanan kapalı alan, sert ve bunaltıcı)"));
	m_env.AddString(LL14(L"空港ターミナル (巨大公共空間、高天井と複雑な反射)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Terminal aéroportuaire (vaste espace public, plafond haut et réflexions complexes)", L"Terminal aeroportuale (vasto spazio pubblico, soffitto alto e riflessioni complesse)", L"Terminal de aeropuerto (amplio espacio público, techo alto y reflexiones complejas)", L"공항 터미널 (거대 공공 공간, 높은 천장과 복잡한 반사)", L"机场航站楼（巨大公共空间、高天花板与复杂反射）", L"مبنى مطار (مساحة عامة شاسعة، سقف مرتفع وانعكاسات معقدة)", L"Аэровокзал (огромное общественное пространство, высокий потолок и сложные отражения)", L"Flughafenterminal (weitläufiger öffentlicher Raum, hohe Decke und komplexe Reflexionen)", L"Terminal de aeroporto (vasto espaço público, teto alto e reflexos complexos)", L"Luchthaventerminal (uitgestrekte openbare ruimte, hoog plafond en complexe reflecties)", L"Terminal lotniskowy (ogromna przestrzeń publiczna, wysoki sufit i złożone odbicia)", L"Havalimanı terminali (geniş kamusal alan, yüksek tavan ve karmaşık yansımalar)"));
	m_env.AddString(LL14(L"ショッピングモール (賑やか商業施設、適度な吸音)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Centre commercial (installation commerciale animée, absorption modérée)", L"Centro commerciale (struttura commerciale vivace, assorbimento moderato)", L"Centro comercial (instalación comercial animada, absorción moderada)", L"쇼핑몰 (활기찬 상업 시설, 적당한 흡음)", L"购物中心（热闹商业设施、适度吸音）", L"مركز تسوق (منشأة تجارية حيوية، امتصاص معتدل)", L"Торговый центр (оживлённый коммерческий объект, умеренное поглощение)", L"Einkaufszentrum (lebhafte Handelsstätte, mäßige Absorption)", L"Shopping (instalação comercial animada, absorção moderada)", L"Winkelcentrum (levendige commerciële voorziening, matige absorptie)", L"Centrum handlowe (żywy obiekt handlowy, umiarkowana absorpcja)", L"Alışveriş merkezi (canlı ticari tesis, orta düzey emilim)"));
	m_env.AddString(LL14(L"病院 (静かで清潔、吸音材による落ち着いた空間)", L"Hospital (quiet and clean, calm space with absorption)", L"Hôpital (calme et propre, espace apaisant avec absorption)", L"Ospedale (silenzioso e pulito, spazio calmo con assorbimento)", L"Hospital (silencioso y limpio, espacio tranquilo con absorción)", L"병원 (조용하고 청결, 흡음으로 차분한 공간)", L"医院（安静清洁、吸音的平静空间）", L"مستشفى (هادئ ونظيف، مساحة هادئة مع امتصاص)", L"Больница (тихая и чистая, спокойное пространство с поглощением)", L"Krankenhaus (ruhig und sauber, ruhiger Raum mit Absorption)", L"Hospital (silencioso e limpo, espaço calmo com absorção)", L"Ziekenhuis (rustig en schoon, kalme ruimte met absorptie)", L"Szpital (cichy i czysty, spokojna przestrzeń z absorpcją)", L"Hastane (sessiz ve temiz, emilimli sakin alan)"));
	m_env.AddString(LL14(L"レコーディングブース (プロ用極ドライ、完全無響に近い)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Cabine d'enregistrement (ultra-sec pro, quasi anéchoïque)", L"Cabina di registrazione (ultra-secca pro, quasi anecoica)", L"Cabina de grabación (ultraseca pro, casi anecoica)", L"녹음 부스 (프로용 초건조, 거의 무향)", L"录音棚（专业极干、近无响）", L"غرفة تسجيل (فائق الجفاف للمحترفين، شبه خالٍ من الصدى)", L"Записывающая будка (профессионально ультрасухая, почти без реверберации)", L"Aufnahmebox (profi-ultratrocken, fast schalltot)", L"Cabine de gravação (ultrasseca pro, quase anecóica)", L"Opnamecabine (pro ultra-droog, bijna echovrij)", L"Kabina nagraniowa (pro ultra-sucha, prawie bezechowa)", L"Kayıt kabini (pro ultra kuru, neredeyse yankısız)"));
	m_env.AddString(LL14(L"オペラハウス (劇場の最高峰、豊かで美しい残響)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opéra (sommet théâtral, réverb riche et belle)", L"Teatro dell'opera (apice teatrale, riverbero ricco e bello)", L"Ópera (cúspide teatral, reverb rica y hermosa)", L"오페라하우스 (극장의 정점, 풍부하고 아름다운 잔향)", L"歌剧院（剧院巅峰、丰富优美混响）", L"دار الأوبرا (ذروة مسرحية، صدى غني وجميل)", L"Оперный театр (вершина театрального искусства, богатая красивая реверберация)", L"Opernhaus (theatralischer Höhepunkt, reicher schöner Hall)", L"Casa de ópera (ápice teatral, reverb rica e bela)", L"Opera (theaterhoogtepunt, rijke mooie nagalm)", L"Opera (szczyt teatralny, bogata piękna pogłos)", L"Opera binası (tiyatro zirvesi, zengin güzel yankı)"));

	m_env.AddString(LL14(L"--[[生活空間 41-50]]--", L"--[[Living 41-50]]--", L"--[[Espace de vie 41-50]]--", L"--[[Spazio abitativo 41-50]]--", L"--[[Espacio cotidiano 41-50]]--", L"--[[생활 공간 41-50]]--", L"--[[生活空间 41-50]]--", L"--[[مساحة معيشة 41-50]]--", L"--[[Бытовое пространство 41-50]]--", L"--[[Wohnraum 41-50]]--", L"--[[Espaço de convivência 41-50]]--", L"--[[Leefruimte 41-50]]--", L"--[[Przestrzeń domowa 41-50]]--", L"--[[Yaşam alanı 41-50]]--"), TRUE);

	m_env.AddString(LL14(L"喫茶店/カフェ (適度な賑わいと吸音、リラックスした空間)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café (animation modérée et absorption, espace détendu)", L"Caffè (vivacità moderata e assorbimento, spazio rilassato)", L"Cafetería (animación moderada y absorción, espacio relajado)", L"찻집/카페 (적당한 활기와 흡음, 편안한 공간)", L"咖啡馆（适度热闹与吸音、放松的空间）", L"مقهى (حيوية معتدلة وامتصاص، مساحة مريحة)", L"Кафе (умеренная оживлённость и поглощение, расслабленное пространство)", L"Café (mäßige Lebendigkeit und Absorption, entspannter Raum)", L"Café (animação moderada e absorção, espaço relaxado)", L"Café (matige levendigheid en absorptie, ontspannen ruimte)", L"Kawiarnia (umiarkowana żywotność i absorpcja, zrelaksowana przestrzeń)", L"Kafe (orta düzey canlılık ve emilim, rahat alan)"));
	m_env.AddString(LL14(L"バー/ラウンジ (暗く落ち着いた雰囲気、中域重視)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Salon (atmosphère sombre et calme, médiums privilégiés)", L"Bar/Lounge (atmosfera scura e calma, focus sui medi)", L"Bar/Salón (ambiente oscuro y tranquilo, enfoque en medios)", L"바/라운지 (어둡고 차분한 분위기, 중역 중심)", L"酒吧/休息室（暗色平静氛围、中频为主）", L"بار/صالة (جو مظلم هادئ، تركيز على الترددات المتوسطة)", L"Бар/лаунж (тёмная спокойная атмосфера, акцент на средних частотах)", L"Bar/Lounge (dunkle ruhige Atmosphäre, mittelfrequenzbetont)", L"Bar/Lounge (atmosfera escura e calma, foco nos médios)", L"Bar/Lounge (donkere rustige sfeer, middenfrequenties)", L"Bar/Lounge (ciemna spokojna atmosfera, nacisk na średnie)", L"Bar/Lounge (karanlık sakin atmosfer, orta frekans odaklı)"));
	m_env.AddString(LL14(L"居酒屋 (賑やか木材吸音、温かみのある響き)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (absorption boisée animée, son chaleureux)", L"Izakaya (assorbimento del legno vivace, suono caldo)", L"Izakaya (absorción de madera animada, sonido cálido)", L"이자카야 (활기찬 목재 흡음, 따뜻한 소리)", L"居酒屋（热闹木质吸音、温暖声响）", L"إزاكايا (امتصاص خشبي حيوي، صوت دافئ)", L"Иzakaya (оживлённое деревянное поглощение, тёплый звук)", L"Izakaya (lebhafte Holzabsorption, warmer Klang)", L"Izakaya (absorção de madeira animada, som quente)", L"Izakaya (levendige houtabsorptie, warm geluid)", L"Izakaya (żywa absorpcja drewna, ciepły dźwięk)", L"Izakaya (canlı ahşap emilimi, sıcak ses)"));
	m_env.AddString(LL14(L"美術館/博物館 (静かで広い高天井、上品な残響)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Musée/Galerie d'art (calme, spacieux, plafond haut, réverb élégante)", L"Museo/Galleria d'arte (silenzioso, spazioso, soffitto alto, riverbero elegante)", L"Museo/Galería de arte (silencioso, espacioso, techo alto, reverb elegante)", L"미술관/박물관 (조용하고 넓은 고천장, 우아한 잔향)", L"美术馆/博物馆（安静宽敞高天花板、优雅混响）", L"متحف/معرض فني (هادئ واسع سقف مرتفع، صدى أنيق)", L"Музей/Художественная галерея (тихое просторное помещение с высоким потолком, элегантная реверберация)", L"Museum/Kunstgalerie (ruhig, geräumig, hohe Decke, eleganter Hall)", L"Museu/Galeria de arte (silencioso, amplo, teto alto, reverb elegante)", L"Museum/Kunstgalerij (rustig, ruim, hoog plafond, elegante nagalm)", L"Muzeum/Galeria sztuki (ciche, przestronne, wysoki sufit, elegancka pogłos)", L"Müze/Sanat galerisi (sessiz geniş yüksek tavan, zarif yankı)"));
	m_env.AddString(LL14(L"講堂/大学教室 (教育施設の反射、明瞭な音響)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Amphithéâtre/Salle universitaire (réflexions d'établissement éducatif, son clair)", L"Auditorium/Aula universitaria (riflessioni di struttura educativa, suono chiaro)", L"Auditorio/Aula universitaria (reflexiones de instalación educativa, sonido claro)", L"강당/대학 교실 (교육 시설 반사, 선명한 음향)", L"礼堂/大学教室（教育设施反射、清晰音响）", L"قاعة محاضرات/فصل جامعي (انعكاسات مرفق تعليمي، صوت واضح)", L"Аудитория/Университетская аудитория (отражения учебного заведения, чёткий звук)", L"Auditorium/Hörsaal (Reflexionen einer Bildungseinrichtung, klarer Klang)", L"Auditório/Sala universitária (reflexos de instalação educacional, som claro)", L"Auditorium/Universiteitslokaal (reflecties van onderwijsinstelling, helder geluid)", L"Aula/Sala uniwersytecka (odbicia obiektu edukacyjnego, czysty dźwięk)", L"Konferans salonu/Üniversite sınıfı (eğitim tesisi yansımaları, net ses)"));
	m_env.AddString(LL14(L"竹林 (和風自然音響、独特の拡散と風の音)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Forêt de bambous (acoustique naturelle japonaise, diffusion unique et vent)", L"Foresta di bambù (acustica naturale giapponese, diffusione unica e vento)", L"Bosque de bambú (acústica natural japonesa, difusión única y viento)", L"대나무 숲 (일본풍 자연 음향, 독특한 확산과 바람)", L"竹林（日式自然声学、独特扩散与风声）", L"غابة خيزران (صوتيات طبيعية يابانية، انتشار فريد ورياح)", L"Бамбуковый лес (японская природная акустика, уникальная диффузия и ветер)", L"Bambuswald (japanische Naturakustik, einzigartige Diffusion und Wind)", L"Floresta de bambu (acústica natural japonesa, difusão única e vento)", L"Bamboebos (Japanse natuurlijke akoestiek, unieke diffusie en wind)", L"Las bambusowy (japońska naturalna akustyka, unikalna dyfuzja i wiatr)", L"Bambu ormanı (Japon tarzı doğal akustik, eşsiz yayılım ve rüzgar)"));
	m_env.AddString(LL14(L"渓谷/滝 (水の反射と濡れた岩肌、躍動感ある響き)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Cascade (réflexions d'eau et roche humide, son dynamique)", L"Gola/Cascata (riflessi d'acqua e roccia bagnata, suono dinamico)", L"Garganta/Cascada (reflexiones de agua y roca húmeda, sonido dinámico)", L"계곡/폭포 (물 반사와 젖은 바위, 역동적인 소리)", L"峡谷/瀑布（水面反射与湿岩、动感声响）", L"وادي/شلال (انعكاسات مائية وصخر رطب، صوت ديناميكي)", L"Ущелье/Водопад (водные отражения и мокрый камень, динамичный звук)", L"Schlucht/Wasserfall (Wasserreflexionen und nasser Fels, dynamischer Klang)", L"Desfiladeiro/Cachoeira (reflexos de água e rocha molhada, som dinâmico)", L"Kloof/Waterval (waterreflecties en natte rots, dynamisch geluid)", L"Wąwóz/Wodospad (odbicia wody i mokra skała, dynamiczny dźwięk)", L"Kanyon/Şelale (su yansımaları ve ıslak kaya, dinamik ses)"));
	m_env.AddString(LL14(L"砂漠 (超開放的反射極小、乾いた空気感)", L"Desert (wide open, minimal reflections, dry air)", L"Désert (grand ouvert, réflexions minimales, air sec)", L"Deserto (ampio aperto, riflessi minimi, aria secca)", L"Desierto (amplio abierto, reflexiones mínimas, aire seco)", L"사막 (매우 개방적, 반사 극소, 건조한 공기)", L"沙漠（极度开阔、反射极少、干燥空气）", L"صحراء (مفتوحة واسعة، انعكاسات ضئيلة، هواء جاف)", L"Пустыня (широкое открытое пространство, минимальные отражения, сухой воздух)", L"Wüste (weit offen, minimale Reflexionen, trockene Luft)", L"Deserto (amplamente aberto, reflexos mínimos, ar seco)", L"Woestijn (wijd open, minimale reflecties, droge lucht)", L"Pustynia (szeroko otwarta, minimalne odbicia, suche powietrze)", L"Çöl (geniş açık, minimal yansımalar, kuru hava)"));
	m_env.AddString(LL14(L"ガレージ (車庫硬質空間、コンクリートと金属)", L"Garage (hard space, concrete and metal)", L"Garage (espace dur, béton et métal)", L"Garage (spazio duro, cemento e metallo)", L"Garaje (espacio duro, hormigón y metal)", L"차고 (경질 공간, 콘크리트와 금속)", L"车库（硬质空间、混凝土与金属）", L"مرآب (مساحة صلبة، خرسانة ومعدن)", L"Гараж (жёсткое пространство, бетон и металл)", L"Garage (harter Raum, Beton und Metall)", L"Garagem (espaço duro, concreto e metal)", L"Garage (harde ruimte, beton en metaal)", L"Garaż (twarda przestrzeń, beton i metal)", L"Garaj (sert alan, beton ve metal)"));
	m_env.AddString(LL14(L"展望台 (高所開放感、風と遠距離エコー)", L"Observation deck (elevated openness, wind and distant echo)", L"Belvédère (ouverture en hauteur, vent et écho lointain)", L"Piattaforma panoramica (apertura elevata, vento ed eco distante)", L"Mirador (apertura elevada, viento y eco lejano)", L"전망대 (고지 개방감, 바람과 원거리 에코)", L"展望台（高处开阔感、风与远距离回声）", L"منصة مشاهدة (انفتاح مرتفع، رياح وصدى بعيد)", L"Смотровая площадка (высокая открытость, ветер и далёкое эхо)", L"Aussichtsplattform (erhöhte Offenheit, Wind und ferner Echo)", L"Mirante (abertura elevada, vento e eco distante)", L"Uitzichtplatform (verhoogde openheid, wind en verre echo)", L"Tarasy widokowe (podwyższona otwartość, wiatr i odległe echo)", L"Seyir terası (yüksek açıklık, rüzgar ve uzak yankı)"));

	m_env.AddString(LL14(L"--[[拡張空間 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Espace étendu 51-60]]--", L"--[[Spazio esteso 51-60]]--", L"--[[Espacio ampliado 51-60]]--", L"--[[확장 공간 51-60]]--", L"--[[扩展空间 51-60]]--", L"--[[مساحة موسعة 51-60]]--", L"--[[Расширенное пространство 51-60]]--", L"--[[Erweiterter Raum 51-60]]--", L"--[[Espaço ampliado 51-60]]--", L"--[[Uitgebreide ruimte 51-60]]--", L"--[[Przestrzeń rozszerzona 51-60]]--", L"--[[Genişletilmiş alan 51-60]]--"), TRUE);

	m_env.AddString(LL14(L"小さな礼拝堂 (教会より親密で温かい)", L"Small chapel (more intimate and warm than church)", L"Petite chapelle (plus intime et chaleureuse qu'une église)", L"Piccola cappella (più intima e calda di una chiesa)", L"Pequeña capilla (más íntima y cálida que una iglesia)", L"작은 예배당 (교회보다 친밀하고 따뜻함)", L"小礼拜堂（比教堂更亲密温暖）", L"مصلى صغير (أكثر حميمية ودفئاً من الكنيسة)", L"Малая часовня (более интимная и тёплая, чем церковь)", L"Kleine Kapelle (intimer und wärmer als Kirche)", L"Pequena capela (mais íntima e quente que igreja)", L"Kleine kapel (intiemer en warmer dan kerk)", L"Mała kaplica (bardziej intymna i ciepła niż kościół)", L"Küçük şapel (kiliseden daha samimi ve sıcak)"));
	m_env.AddString(LL14(L"大型ショッピングセンター (モールより巨大)", L"Large shopping center (bigger than mall)", L"Grand centre commercial (plus vaste qu'un mall)", L"Grande centro commerciale (più grande di un mall)", L"Gran centro comercial (más grande que un mall)", L"대형 쇼핑센터 (몰보다 거대)", L"大型购物中心（比商场更大）", L"مركز تسوق كبير (أضخم من المول)", L"Крупный торговый центр (больше торгового комплекса)", L"Großes Einkaufszentrum (größer als Mall)", L"Grande shopping center (maior que um mall)", L"Groot winkelcentrum (groter dan mall)", L"Duże centrum handlowe (większe niż mall)", L"Büyük alışveriş merkezi (AVM'den daha büyük)"));
	m_env.AddString(LL14(L"地下洞窟(深層) (より深く神秘的)", L"Underground cave (deep) (deeper and more mystical)", L"Grotte souterraine (profonde) (plus profonde et mystique)", L"Grotta sotterranea (profonda) (più profonda e mistica)", L"Cueva subterránea (profunda) (más profunda y mística)", L"지하 동굴(심층) (더 깊고 신비로움)", L"地下洞窟（深层）（更深更神秘）", L"كهف تحت الأرض (عميق) (أعمق وأكثر غموضاً)", L"Подземная пещера (глубокая) (глубже и более мистическая)", L"Unterirdische Höhle (tief) (tiefer und mystischer)", L"Caverna subterrânea (profunda) (mais profunda e mística)", L"Ondergrondse grot (diep) (dieper en mystieker)", L"Podziemna jaskinia (głęboka) (głębsza i bardziej mistyczna)", L"Yeraltı mağarası (derin) (daha derin ve mistik)"));
	m_env.AddString(LL14(L"古城の大広間 (石造り中世的)", L"Castle great hall (stone medieval)", L"Grande salle du château (pierre médiévale)", L"Grande sala del castello (pietra medievale)", L"Gran salón del castillo (piedra medieval)", L"고성 대홀 (석조 중세풍)", L"古城大厅（石造中世纪）", L"قاعة القلعة الكبرى (حجرية من العصور الوسطى)", L"Большой зал замка (каменный средневековый)", L"Große Burgeshalle (steinerne mittelalterliche)", L"Grande salão do castelo (pedra medieval)", L"Grote kasteelzaal (middeleeuws steen)", L"Wielka sala zamkowa (kamienna, średniowieczna)", L"Kale büyük salonu (taş ortaçağ)"));
	m_env.AddString(LL14(L"野外音楽堂 (半開放的ステージ)", L"Outdoor amphitheater (semi-open stage)", L"Amphithéâtre en plein air (scène semi-ouverte)", L"Anfiteatro all'aperto (palco semi-aperto)", L"Anfiteatro al aire libre (escenario semiabierto)", L"야외 음악당 (반개방 무대)", L"露天音乐堂（半开放舞台）", L"مدرج خارجي (مسرح شبه مفتوح)", L"Открытый амфитеатр (полуоткрытая сцена)", L"Freilichtamphitheater (halboffene Bühne)", L"Anfiteatro ao ar livre (palco semiaberto)", L"Buitenamfitheater (halfopen podium)", L"Amfiteatr na świeżym powietrzu (półotwarta scena)", L"Açık hava amfitiyatrosu (yarı açık sahne)"));
	m_env.AddString(LL14(L"鍾乳洞 (複雑な水滴反射)", L"Limestone cave (complex water droplet reflections)", L"Grotte calcaire (réflexions complexes de gouttes d'eau)", L"Grotta calcarea (complessi riflessi di gocce d'acqua)", L"Cueva de caliza (reflexiones complejas de gotas de agua)", L"석회동굴 (복잡한 물방울 반사)", L"石灰岩洞（复杂水滴反射）", L"كهف جيري (انعكاسات معقدة لقطرات الماء)", L"Известковая пещера (сложные отражения капель воды)", L"Kalksteinhöhle (komplexe Wassertropfenreflexionen)", L"Caverna de calcário (reflexos complexos de gotas de água)", L"Kalksteengrot (complexe waterdruppelreflecties)", L"Jaskinia wapienna (złożone odbicia kropli wody)", L"Kireçtaşı mağarası (karmaşık su damlası yansımaları)"));
	m_env.AddString(LL14(L"廃墟工場 (荒廃した金属空間)", L"Abandoned factory (decayed metal space)", L"Usine abandonnée (espace métallique délabré)", L"Fabbrica abbandonata (spazio metallico decadente)", L"Fábrica abandonada (espacio metálico deteriorado)", L"폐허 공장 (황폐한 금속 공간)", L"废弃工厂（荒废的金属空间）", L"مصنع مهجور (مساحة معدنية متدهورة)", L"Заброшенный завод (запущенное металлическое пространство)", L"Verlassene Fabrik (verfallener Metallraum)", L"Fábrica abandonada (espaço metálico decadente)", L"Verlaten fabriek (vervallen metalen ruimte)", L"Opuszczona fabryka (zrujnowana metalowa przestrzeń)", L"Terk edilmiş fabrika (harap metal alan)"));
	m_env.AddString(LL14(L"和室(畳) (日本的柔らかい吸音)", L"Japanese room (tatami) (Japanese soft absorption)", L"Pièce japonaise (tatami) (absorption douce japonaise)", L"Stanza giapponese (tatami) (assorbimento morbido giapponese)", L"Habitación japonesa (tatami) (absorción suave japonesa)", L"일본식 방(다다미) (일본식 부드러운 흡음)", L"和室（榻榻米）（日式柔和吸音）", L"غرفة يابانية (tatami) (امتصاص ياباني ناعم)", L"Японская комната (татами) (мягкое японское поглощение)", L"Japanischer Raum (Tatami) (weiche japanische Absorption)", L"Quarto japonês (tatami) (absorção suave japonesa)", L"Japanse kamer (tatami) (zachte Japanse absorptie)", L"Japoński pokój (tatami) (miękka japońska absorpcja)", L"Japon odası (tatami) (Japon tarzı yumuşak emilim)"));
	m_env.AddString(LL14(L"温泉施設 (湿度高めタイル反射)", L"Hot spring facility (humid tile reflections)", L"Établissement thermal (réflexions carrelées humides)", L"Struttura termale (riflessi piastrellati umidi)", L"Instalación termal (reflexiones de azulejos húmedos)", L"온천 시설 (습한 타일 반사)", L"温泉设施（潮湿瓷砖反射）", L"مرفق ينابيع حارة (انعكاسات بلاط رطبة)", L"Термальный комплекс (влажные плиточные отражения)", L"Thermalbad (feuchte Fliesenreflexionen)", L"Instalação termal (reflexos de azulejos úmidos)", L"Warmwaterbad (vochtige tegelreflecties)", L"Obiekt termalny (wilgotne kaflowe odbicia)", L"Kaplıca tesisi (nemli fayans yansımaları)"));
	m_env.AddString(LL14(L"屋根裏部屋 (斜め天井の特殊空間)", L"Attic (angled ceiling special space)", L"Grenier (espace spécial à plafond incliné)", L"Soffitta (spazio speciale con soffitto inclinato)", L"Ático (espacio especial con techo inclinado)", L"다락방 (경사 천장의 특수 공간)", L"阁楼（斜顶特殊空间）", L"علية (مساحة خاصة بسقف مائل)", L"Чердак (особое пространство с наклонным потолком)", L"Dachboden (Spezialraum mit schräger Decke)", L"Sótão (espaço especial com teto inclinado)", L"Zolder (speciale ruimte met schuin plafond)", L"Poddasze (specjalna przestrzeń ze skośnym sufitem)", L"Tavan arası (eğimli tavanlı özel alan)"));

	m_env.AddString(LL14(L"--[[特殊空間 61-70]]--", L"--[[Special 61-70]]--", L"--[[Espace spécial 61-70]]--", L"--[[Spazio speciale 61-70]]--", L"--[[Espacio especial 61-70]]--", L"--[[특수 공간 61-70]]--", L"--[[特殊空间 61-70]]--", L"--[[مساحة خاصة 61-70]]--", L"--[[Особое пространство 61-70]]--", L"--[[Spezialraum 61-70]]--", L"--[[Espaço especial 61-70]]--", L"--[[Speciale ruimte 61-70]]--", L"--[[Przestrzeń specjalna 61-70]]--", L"--[[Özel alan 61-70]]--"), TRUE);

	m_env.AddString(LL14(L"地下駐車場(多層) (階層的複雑反射)", L"Underground parking (multi-level) (layered complex reflections)", L"Parking souterrain (multi-niveaux) (réflexions complexes en couches)", L"Parcheggio sotterraneo (multilivello) (riflessi complessi a strati)", L"Aparcamiento subterráneo (multinivel) (reflexiones complejas en capas)", L"지하 주차장(다층) (층별 복잡한 반사)", L"地下停车场（多层）（分层复杂反射）", L"موقف سيارات تحت الأرض (متعدد الطوابق) (انعكاسات معقدة متعددة الطبقات)", L"Подземная парковка (многоуровневая) (многослойные сложные отражения)", L"Tiefgarage (mehrstöckig) (geschichtete komplexe Reflexionen)", L"Estacionamento subterrâneo (multinível) (reflexos complexos em camadas)", L"Ondergrondse parkeergarage (meerdere verdiepingen) (gelaagde complexe reflecties)", L"Parking podziemny (wielopoziomowy) (warstwowe złożone odbicia)", L"Yeraltı otoparkı (çok katlı) (katmanlı karmaşık yansımalar)"));
	m_env.AddString(LL14(L"古い劇場(木造) (温かみある音響設計)", L"Old theater (wooden) (warm acoustic design)", L"Ancien théâtre (bois) (conception acoustique chaleureuse)", L"Vecchio teatro (in legno) (progettazione acustica calda)", L"Teatro antiguo (de madera) (diseño acústico cálido)", L"오래된 극장(목조) (따뜻한 음향 설계)", L"旧剧院（木制）（温暖声学设计）", L"مسرح قديم (خشبي) (تصميم صوتي دافئ)", L"Старый театр (деревянный) (тёплая акустическая конструкция)", L"Altes Theater (Holz) (warmes akustisches Design)", L"Teatro antigo (de madeira) (design acústico quente)", L"Oud theater (houten) (warm akoestisch ontwerp)", L"Stary teatr (drewniany) (ciepła konstrukcja akustyczna)", L"Eski tiyatro (ahşap) (sıcak akustik tasarım)"));
	m_env.AddString(LL14(L"大型倉庫(空) (極端な空虚感)", L"Large warehouse (empty) (extreme emptiness)", L"Grand entrepôt (vide) (vide extrême)", L"Grande magazzino (vuoto) (estrema vacuità)", L"Gran almacén (vacío) (vacío extremo)", L"대형 창고(비어 있음) (극단적 공허감)", L"大型仓库（空）（极度空虚感）", L"مستودع كبير (فارغ) (فراغ شديد)", L"Большой склад (пустой) (крайняя пустота)", L"Großes Lager (leer) (extreme Leere)", L"Grande armazém (vazio) (vazio extremo)", L"Groot magazijn (leeg) (extreme leegte)", L"Duży magazyn (pusty) (skrajna pustka)", L"Büyük depo (boş) (aşırı boşluk)"));
	m_env.AddString(LL14(L"小さな教会 (カテドラルより親密)", L"Small church (more intimate than cathedral)", L"Petite église (plus intime qu'une cathédrale)", L"Piccola chiesa (più intima di una cattedrale)", L"Pequeña iglesia (más íntima que una catedral)", L"작은 교회 (대성당보다 친밀함)", L"小教堂（比大教堂更亲密）", L"كنيسة صغيرة (أكثر حميمية من الكاتدرائية)", L"Малая церковь (более интимная, чем собор)", L"Kleine Kirche (intimer als Kathedrale)", L"Pequena igreja (mais íntima que catedral)", L"Kleine kerk (intiemer dan kathedraal)", L"Mały kościół (bardziej intymny niż katedra)", L"Küçük kilise (katedralden daha samimi)"));
	m_env.AddString(LL14(L"ガラス温室 (硬質ガラス反射)", L"Glass greenhouse (hard glass reflections)", L"Serre vitrée (réflexions vitrées dures)", L"Serra di vetro (riflessi vetrosi duri)", L"Invernadero de cristal (reflexiones duras de vidrio)", L"유리 온실 (경질 유리 반사)", L"玻璃温室（硬质玻璃反射）", L"بيت زجاجي (انعكاسات زجاجية صلبة)", L"Стеклянная оранжерея (жёсткие стеклянные отражения)", L"Glasgewächshaus (harte Glasreflexionen)", L"Estufa de vidro (reflexos duros de vidro)", L"Glazen kas (harde glasreflecties)", L"Szklarnia (twarde szklane odbicia)", L"Cam sera (sert cam yansımaları)"));
	m_env.AddString(LL14(L"石造りトンネル (硬く長い残響)", L"Stone tunnel (hard long reverb)", L"Tunnel de pierre (réverb dure et longue)", L"Tunnel di pietra (riverbero duro e lungo)", L"Túnel de piedra (reverb dura y larga)", L"석조 터널 (경질 긴 잔향)", L"石造隧道（硬质长混响）", L"نفق حجري (صدى صلب وطويل)", L"Каменный туннель (жёсткая длинная реверберация)", L"Steintunnel (harter langer Hall)", L"Túnel de pedra (reverb dura e longa)", L"Stenen tunnel (harde lange nagalm)", L"Kamienny tunel (twarda długa pogłos)", L"Taş tünel (sert uzun yankı)"));
	m_env.AddString(LL14(L"コンクリート階段 (硬質縦方向反射)", L"Concrete stairs (hard vertical reflections)", L"Escalier en béton (réflexions verticales dures)", L"Scale di cemento (riflessioni verticali dure)", L"Escaleras de hormigón (reflexiones verticales duras)", L"콘크리트 계단 (경질 수직 반사)", L"混凝土楼梯（硬质垂直反射）", L"درج خرساني (انعكاسات عمودية صلبة)", L"Бетонная лестница (жёсткие вертикальные отражения)", L"Betontreppe (harte vertikale Reflexionen)", L"Escadas de concreto (reflexos verticais duros)", L"Betonnen trap (harde verticale reflecties)", L"Betonowe schody (twarde pionowe odbicia)", L"Beton merdiven (sert dikey yansımalar)"));
	m_env.AddString(LL14(L"大浴場 (広いタイル反射)", L"Public bath (wide tile reflections)", L"Bain public (larges réflexions carrelées)", L"Terme pubbliche (ampi riflessi piastrellati)", L"Baño público (amplias reflexiones de azulejos)", L"대욕장 (넓은 타일 반사)", L"大浴场（宽阔瓷砖反射）", L"حمام عام (انعكاسات بلاط واسعة)", L"Общественная баня (широкие плиточные отражения)", L"Öffentliches Bad (weite Fliesenreflexionen)", L"Banho público (amplos reflexos de azulejos)", L"Publiek bad (brede tegelreflecties)", L"Łaźnia publiczna (szerokie kaflowe odbicia)", L"Hamam (geniş fayans yansımaları)"));
	m_env.AddString(LL14(L"洗面所 (極小タイル空間)", L"Bathroom (tiny tile space)", L"Salle de bain (minuscule espace carrelé)", L"Bagno (minuscolo spazio piastrellato)", L"Baño (minúsculo espacio con azulejos)", L"욕실 (아주 작은 타일 공간)", L"浴室（极小瓷砖空间）", L"حمام (مساحة صغيرة مبلطة)", L"Ванная (крошечное плиточное пространство)", L"Badezimmer (winziger gefliester Raum)", L"Banheiro (minúsculo espaço com azulejos)", L"Badkamer (kleine betegelde ruimte)", L"Łazienka (maleńka kaflowa przestrzeń)", L"Banyo (minik fayanslı alan)"));
	m_env.AddString(LL14(L"廊下(カーペット) (吸音的柔らかい)", L"Corridor (carpeted) (absorptive and soft)", L"Couloir (moquette) (absorbant et doux)", L"Corridoio (con moquette) (assorbente e morbido)", L"Pasillo (alfombrado) (absorbente y suave)", L"복도(카펫) (흡음적이고 부드러움)", L"走廊（铺地毯）（吸音柔和）", L"ممر (مفروش) (امتصاصي وناعم)", L"Коридор (с ковром) (поглощающий и мягкий)", L"Flur (mit Teppich) (absorbierend und weich)", L"Corredor (com carpete) (absorvente e suave)", L"Gang (met tapijt) (absorberend en zacht)", L"Korytarz (wykładziny) (chłonny i miękki)", L"Koridor (halılı) (emici ve yumuşak)"));

	m_env.AddString(LL14(L"--[[専門空間 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Espace professionnel 71-80]]--", L"--[[Spazio professionale 71-80]]--", L"--[[Espacio profesional 71-80]]--", L"--[[전문 공간 71-80]]--", L"--[[专业空间 71-80]]--", L"--[[مساحة مهنية 71-80]]--", L"--[[Профессиональное пространство 71-80]]--", L"--[[Professioneller Raum 71-80]]--", L"--[[Espaço profissional 71-80]]--", L"--[[Professionele ruimte 71-80]]--", L"--[[Przestrzeń profesjonalna 71-80]]--", L"--[[Profesyonel alan 71-80]]--"), TRUE);

	m_env.AddString(LL14(L"会議室(大) (ビジネス空間)", L"Meeting room (large) (business space)", L"Salle de réunion (grande) (espace professionnel)", L"Sala riunioni (grande) (spazio business)", L"Sala de reuniones (grande) (espacio de negocios)", L"회의실(대) (비즈니스 공간)", L"会议室（大）（商务空间）", L"غرفة اجتماعات (كبيرة) (مساحة أعمال)", L"Переговорная (большая) (деловое пространство)", L"Besprechungsraum (groß) (Geschäftsraum)", L"Sala de reunião (grande) (espaço de negócios)", L"Vergaderzaal (groot) (zakelijke ruimte)", L"Sala konferencyjna (duża) (przestrzeń biznesowa)", L"Toplantı odası (büyük) (iş alanı)"));
	m_env.AddString(LL14(L"会議室(小) (より密閉的)", L"Meeting room (small) (more enclosed)", L"Salle de réunion (petite) (plus confinée)", L"Sala riunioni (piccola) (più chiusa)", L"Sala de reuniones (pequeña) (más cerrada)", L"회의실(소) (더 밀폐적)", L"会议室（小）（更密闭）", L"غرفة اجتماعات (صغيرة) (أكثر انغلاقاً)", L"Переговорная (малая) (более замкнутая)", L"Besprechungsraum (klein) (eingeschlossener)", L"Sala de reunião (pequena) (mais fechada)", L"Vergaderzaal (klein) (meer afgesloten)", L"Sala konferencyjna (mała) (bardziej zamknięta)", L"Toplantı odası (küçük) (daha kapalı)"));
	m_env.AddString(LL14(L"防音室 (極端なデッド空間)", L"Soundproof room (extreme dead space)", L"Salle insonorisée (espace extrêmement mort)", L"Stanza insonorizzata (spazio estremamente morto)", L"Sala insonorizada (espacio extremadamente muerto)", L"방음실 (극단적 데드 공간)", L"隔音室（极端干声空间）", L"غرفة عازلة للصوت (مساحة ميتة للغاية)", L"Звукоизолированная комната (крайне мёртвое пространство)", L"Schalldichter Raum (extrem toter Raum)", L"Sala insonorizada (espaço extremamente morto)", L"Geluiddichte ruimte (extreem dode ruimte)", L"Pokój dźwiękoszczelny (skrajnie martwa przestrzeń)", L"Ses yalıtımlı oda (aşırı kuru alan)"));
	m_env.AddString(LL14(L"エントランスホール (高天井開放的)", L"Entrance hall (high ceiling, open)", L"Hall d'entrée (plafond haut, ouvert)", L"Atrio d'ingresso (soffitto alto, aperto)", L"Vestíbulo de entrada (techo alto, abierto)", L"로비 (높은 천장, 개방적)", L"门厅（高天花板、开放）", L"ردهة المدخل (سقف مرتفع، مفتوحة)", L"Входной холл (высокий потолок, открытый)", L"Eingangshalle (hohe Decke, offen)", L"Hall de entrada (teto alto, aberto)", L"Entreehal (hoog plafond, open)", L"Hol wejściowy (wysoki sufit, otwarty)", L"Giriş holü (yüksek tavan, açık)"));
	m_env.AddString(LL14(L"書斎 (本による吸音)", L"Study (absorption from books)", L"Bureau (absorption des livres)", L"Studio (assorbimento dei libri)", L"Estudio (absorción de los libros)", L"서재 (책에 의한 흡음)", L"书房（书籍吸音）", L"مكتب دراسة (امتصاص من الكتب)", L"Кабинет (поглощение от книг)", L"Arbeitszimmer (Absorption durch Bücher)", L"Escritório (absorção dos livros)", L"Studeerkamer (absorptie door boeken)", L"Gabinet (absorpcja od książek)", L"Çalışma odası (kitaplardan emilim)"));
	m_env.AddString(LL14(L"キッチン (硬質多反射)", L"Kitchen (hard multi-reflections)", L"Cuisine (multiples réflexions dures)", L"Cucina (multi-riflessioni dure)", L"Cocina (multirreflexiones duras)", L"주방 (경질 다중 반사)", L"厨房（硬质多重反射）", L"مطبخ (انعكاسات متعددة صلبة)", L"Кухня (жёсткие множественные отражения)", L"Küche (harte Mehrfachreflexionen)", L"Cozinha (multirreflexos duros)", L"Keuken (harde meervoudige reflecties)", L"Kuchnia (twarde wielokrotne odbicia)", L"Mutfak (sert çoklu yansımalar)"));
	m_env.AddString(LL14(L"屋外駐車場 (開放的反射少)", L"Outdoor parking lot (open, fewer reflections)", L"Parking extérieur (ouvert, moins de réflexions)", L"Parcheggio all'aperto (aperto, meno riflessi)", L"Aparcamiento exterior (abierto, menos reflexiones)", L"야외 주차장 (개방적, 반사 적음)", L"室外停车场（开放、反射少）", L"موقف سيارات خارجي (مفتوح، انعكاسات أقل)", L"Открытая парковка (открытая, меньше отражений)", L"Außenparkplatz (offen, weniger Reflexionen)", L"Estacionamento externo (aberto, menos reflexos)", L"Buitenparkeerplaats (open, minder reflecties)", L"Parking na zewnątrz (otwarty, mniej odbić)", L"Açık otopark (açık, daha az yansıma)"));
	m_env.AddString(LL14(L"地下道(狭) (圧迫的狭小空間)", L"Underground passage (narrow) (oppressive confined space)", L"Passage souterrain (étroit) (espace confiné oppressant)", L"Passaggio sotterraneo (stretto) (spazio ristretto opprimente)", L"Pasaje subterráneo (estrecho) (espacio confinado opresivo)", L"지하도(좁음) (압박적인 좁은 공간)", L"地下通道（窄）（压迫性狭小空间）", L"ممر تحت الأرض (ضيق) (مساحة ضيقة قهرية)", L"Подземный проход (узкий) (угнетающее тесное пространство)", L"Unterirdischer Gang (schmal) (bedrückender enger Raum)", L"Passagem subterrânea (estreita) (espaço confinado opressivo)", L"Ondergrondse doorgang (smal) (benauwende enge ruimte)", L"Podziemne przejście (wąskie) (duszna ciasna przestrzeń)", L"Yeraltı geçidi (dar) (bunaltıcı dar alan)"));
	m_env.AddString(LL14(L"展示室 (美術館より吸音的)", L"Exhibition room (more absorbent than gallery)", L"Salle d'exposition (plus absorbante qu'une galerie)", L"Sala espositiva (più assorbente di una galleria)", L"Sala de exposición (más absorbente que una galería)", L"전시실 (미술관보다 흡음적)", L"展厅（比美术馆更吸音）", L"قاعة عرض (أكثر امتصاصاً من المعرض)", L"Выставочный зал (более поглощающий, чем галерея)", L"Ausstellungsraum (absorbierender als Galerie)", L"Sala de exposição (mais absorvente que galeria)", L"Tentoonstellingsruimte (absorberender dan galerij)", L"Sala wystawowa (bardziej chłonna niż galeria)", L"Sergi odası (galeriden daha emici)"));
	m_env.AddString(LL14(L"アトリエ (創作空間の独特さ)", L"Atelier (unique creative space)", L"Atelier (espace créatif unique)", L"Atelier (spazio creativo unico)", L"Taller (espacio creativo único)", L"아틀리에 (독특한 창작 공간)", L"工作室（独特的创作空间）", L"استوديو (مساحة إبداعية فريدة)", L"Ателье (уникальное творческое пространство)", L"Atelier (einzigartiger Kreativraum)", L"Ateliê (espaço criativo único)", L"Atelier (unieke creatieve ruimte)", L"Pracownia (unikalna przestrzeń twórcza)", L"Atölye (eşsiz yaratıcı alan)"));

	m_env.AddString(LL14(L"--[[SFX/未来 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Futur 81-100]]--", L"--[[SFX/Futuro 81-100]]--", L"--[[SFX/Futuro 81-100]]--", L"--[[SFX/미래 81-100]]--", L"--[[SFX/未来 81-100]]--", L"--[[SFX/مستقبل 81-100]]--", L"--[[SFX/Будущее 81-100]]--", L"--[[SFX/Zukunft 81-100]]--", L"--[[SFX/Futuro 81-100]]--", L"--[[SFX/Toekomst 81-100]]--", L"--[[SFX/Przyszłość 81-100]]--", L"--[[SFX/Gelecek 81-100]]--"), TRUE);
	m_env.AddString(LL14(L"サイバーパンク路地 (金属反射＋狭い空間、ネオン感)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Ruelle cyberpunk (réflexion métallique + espace étroit, ambiance néon)", L"Vicolo cyberpunk (riflesso metallico + spazio stretto, atmosfera neon)", L"Callejón cyberpunk (reflexión metálica + espacio estrecho, ambiente neón)", L"사이버펑크 골목 (금속 반사 + 좁은 공간, 네온 감)", L"赛博朋克小巷（金属反射＋狭窄空间、霓虹感）", L"زقاق سایبرپانک (انعكاس معدني + مساحة ضيقة، أجواء نيون)", L"Киберпанк-переулок (металлические отражения + узкое пространство, неоновая атмосфера)", L"Cyberpunk-Gasse (Metallreflexion + enger Raum, Neon-Feeling)", L"Beco cyberpunk (reflexo metálico + espaço estreito, clima neon)", L"Cyberpunksteeg (metaalreflectie + smalle ruimte, neongevoel)", L"Cyberpunkowa uliczka (metaliczne odbicia + wąska przestrzeń, klimat neonu)", L"Cyberpunk ara sokak (metal yansıma + dar alan, neon hissi)"));
	m_env.AddString(LL14(L"宇宙船ブリッジ (クリーンで硬質、短い反射)", L"Spaceship bridge (clean and hard, short reflections)", L"Pont de vaisseau spatial (propre et dur, courtes réflexions)", L"Ponte di astronave (pulito e duro, brevi riflessi)", L"Puente de nave espacial (limpio y duro, reflexiones cortas)", L"우주선 브릿지 (깨끗하고 경질, 짧은 반사)", L"宇宙飞船舰桥（干净硬质、短反射）", L"جسر سفينة فضائية (نظيف وصلب، انعكاسات قصيرة)", L"Мостик космического корабля (чистый и жёсткий, короткие отражения)", L"Raumschiffbrücke (sauber und hart, kurze Reflexionen)", L"Ponte de nave espacial (limpa e dura, reflexos curtos)", L"Ruimteschipbrug (schoon en hard, korte reflecties)", L"Mostek statku kosmicznego (czysty i twardy, krótkie odbicia)", L"Uzay gemisi köprüsü (temiz ve sert, kısa yansımalar)"));
	m_env.AddString(LL14(L"ワープトンネル (揺らぎと長い残響、引き伸ばし)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Tunnel de distorsion (fluctuation et longue réverb, étirement)", L"Tunnel warp (fluttuazione e lungo riverbero, allungamento)", L"Túnel warp (fluctuación y reverb larga, estiramiento)", L"워프 터널 (변동과 긴 잔향, 늘어남)", L"跃迁隧道（波动与长混响、拉伸感）", L"نفق warp (تذبذب وصدى طويل، تمدد)", L"Туннель варпа (флуктуация и длинная реверберация, растягивание)", L"Warp-Tunnel (Schwankung und langer Hall, Dehnung)", L"Túnel warp (flutuação e reverb longa, esticamento)", L"Warptunnel (fluctuatie en lange nagalm, uitrekken)", L"Tunel warp (fluktuacja i długa pogłos, rozciąganie)", L"Warp tüneli (dalgalanma ve uzun yankı, gerilme)"));
	m_env.AddString(LL14(L"量子ホール (不安定拡散、浮遊感)", L"Quantum hall (unstable diffusion, floating feel)", L"Salle quantique (diffusion instable, sensation de flottement)", L"Sala quantica (diffusione instabile, sensazione di fluttuazione)", L"Sala cuántica (difusión inestable, sensación flotante)", L"양자 홀 (불안정 확산, 부유감)", L"量子大厅（不稳定扩散、漂浮感）", L"قاعة كمية (انتشار غير مستقر، إحساس بالطفو)", L"Квантовый зал (нестабильная диффузия, ощущение парения)", L"Quantensaal (instabile Diffusion, Schwebgefühl)", L"Salão quântico (difusão instável, sensação flutuante)", L"Kwantumzaal (onstabiele diffusie, zweefgevoel)", L"Sala kwantowa (niestabilna dyfuzja, uczucie unoszenia)", L"Kuantum salonu (kararsız yayılım, süzülme hissi)"));
	m_env.AddString(LL14(L"無限回廊 (規則的エコー、長く続く反射)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Couloir infini (écho régulier, réflexions durables)", L"Corridoio infinito (eco regolare, riflessioni durature)", L"Pasillo infinito (eco regular, reflexiones duraderas)", L"무한 회랑 (규칙적 에코, 오래 지속되는 반사)", L"无限回廊（规律回声、持久反射）", L"ممر لا نهائي (صدى منتظم، انعكاسات طويلة الأمد)", L"Бесконечный коридор (регулярное эхо, долгие отражения)", L"Unendlicher Flur (regelmäßiges Echo, anhaltende Reflexionen)", L"Corredor infinito (eco regular, reflexos duradouros)", L"Oneindige gang (regelmatige echo, langdurige reflecties)", L"Nieskończony korytarz (regularne echo, trwałe odbicia)", L"Sonsuz koridor (düzenli yankı, uzun süren yansımalar)"));
	m_env.AddString(LL14(L"逆再生空間 (早い反射と遅い尾、異常な広がり)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Espace de lecture inversée (réflexion rapide, queue lente, diffusion anormale)", L"Spazio di riproduzione inversa (riflesso veloce, coda lenta, diffusione anomala)", L"Espacio de reproducción inversa (reflexión rápida, cola lenta, difusión anormal)", L"역재생 공간 (빠른 반사와 느린 꼬리, 비정상적 확산)", L"逆播放空间（快反射、慢尾音、异常扩散）", L"فضاء تشغيل عكسي (انعكاس سريع، ذيل بطيء، انتشار غير طبيعي)", L"Пространство обратного воспроизведения (быстрое отражение, медленный хвост, аномальное распространение)", L"Rückwärts-Wiedergaberaum (schnelle Reflexion, langsamer Ausklang, abnormale Ausbreitung)", L"Espaço de reprodução reversa (reflexo rápido, cauda lenta, difusão anormal)", L"Omgekeerde afspeelruimte (snelle reflectie, trage staart, abnormale spreiding)", L"Przestrzeń odtwarzania wstecznego (szybkie odbicie, wolny ogon, nienormalne rozprzestrzenianie)", L"Ters oynatma alanı (hızlı yansıma, yavaş kuyruk, anormal yayılım)"));
	m_env.AddString(LL14(L"タイムストップ室 (ほぼ無響＋硬い反射)", L"Time-stop room (nearly anechoic + hard reflection)", L"Salle d'arrêt du temps (quasi anéchoïque + réflexion dure)", L"Stanza time-stop (quasi anecoica + riflesso duro)", L"Sala de parada del tiempo (casi anecoica + reflexión dura)", L"타임스톱실 (거의 무향 + 경질 반사)", L"时间停止室（近无响＋硬反射）", L"غرفة إيقاف الزمن (شبه خالية من الصدى + انعكاس صلب)", L"Комната остановки времени (почти без реверберации + жёсткое отражение)", L"Zeitstopp-Raum (fast schalltot + harte Reflexion)", L"Sala de parada do tempo (quase anecóica + reflexo duro)", L"Tijdstopkamer (bijna echovrij + harde reflectie)", L"Pokój zatrzymania czasu (prawie bezechowy + twarde odbicie)", L"Zaman durdurma odası (neredeyse yankısız + sert yansıma)"));
	m_env.AddString(LL14(L"データセンター (低域振動、機械的反射)", L"Data center (low-frequency vibration, mechanical reflection)", L"Centre de données (vibration basse fréquence, réflexion mécanique)", L"Data center (vibrazione a bassa frequenza, riflesso meccanico)", L"Centro de datos (vibración de baja frecuencia, reflexión mecánica)", L"데이터 센터 (저역 진동, 기계적 반사)", L"数据中心（低频振动、机械反射）", L"مركز بيانات (اهتزاز منخفض التردد، انعكاس ميكانيكي)", L"Центр обработки данных (низкочастотная вибрация, механические отражения)", L"Rechenzentrum (Niederfrequenzvibration, mechanische Reflexion)", L"Centro de dados (vibração de baixa frequência, reflexo mecânico)", L"Datacenter (laagfrequente trilling, mechanische reflectie)", L"Centrum danych (wibracja niskich tonów, mechaniczne odbicia)", L"Veri merkezi (düşük frekans titreşimi, mekanik yansıma)"));
	m_env.AddString(LL14(L"巨大機械内部 (金属共鳴、重い反射)", L"Inside giant machine (metal resonance, heavy reflection)", L"Intérieur de machine géante (résonance métallique, réflexion lourde)", L"Interno di macchina gigante (risonanza metallica, riflesso pesante)", L"Interior de máquina gigante (resonancia metálica, reflexión pesada)", L"거대 기계 내부 (금속 공명, 무거운 반사)", L"巨型机械内部（金属共鸣、厚重反射）", L"داخل آلة عملاقة (رنين معدني، انعكاس ثقيل)", L"Внутри гигантской машины (металлический резонанс, тяжёлые отражения)", L"Innen in Riesenmaschine (Metallresonanz, schwere Reflexion)", L"Interior de máquina gigante (ressonância metálica, reflexo pesado)", L"Binnen in reuzemachine (metaalresonantie, zware reflectie)", L"Wnętrze ogromnej maszyny (metaliczna rezonans, ciężkie odbicia)", L"Dev makine içi (metal rezonans, ağır yansıma)"));
	m_env.AddString(LL14(L"AIホログラム室 (透明感、明るい反射)", L"AI hologram room (transparency, bright reflection)", L"Salle holographique IA (transparence, réflexion lumineuse)", L"Sala olografica IA (trasparenza, riflesso luminoso)", L"Sala holográfica IA (transparencia, reflexión brillante)", L"AI 홀로그램실 (투명감, 밝은 반사)", L"AI全息室（透明感、明亮反射）", L"غرفة هولوغرام ذكاء اصطناعي (شفافية، انعكاس ساطع)", L"Комната с ИИ-голограммой (прозрачность, яркие отражения)", L"KI-Hologrammraum (Transparenz, helle Reflexion)", L"Sala holográfica IA (transparência, reflexo brilhante)", L"AI-hologramkamer (transparantie, heldere reflectie)", L"Pokój hologramu AI (przezroczystość, jasna refleksja)", L"Yapay zeka hologram odası (şeffaflık, parlak yansıma)"));
	m_env.AddString(LL14(L"重力ゼロ船庫 (低密度で長残響)", L"Zero-gravity hangar (low density, long reverb)", L"Hangar en apesanteur (faible densité, longue réverb)", L"Hangar a gravità zero (bassa densità, lungo riverbero)", L"Hangar de gravedad cero (baja densidad, reverb larga)", L"무중력 격납고 (저밀도, 긴 잔향)", L"零重力机库（低密度、长混响）", L"حظيرة انعدام الجاذبية (كثافة منخفضة، صدى طويل)", L"Ангар невесомости (низкая плотность, длинная реверберация)", L"Schwerelosigkeitshangar (geringe Dichte, langer Hall)", L"Hangar de gravidade zero (baixa densidade, reverb longa)", L"Zwaarteloosheidshangar (lage dichtheid, lange nagalm)", L"Hangar nieważkości (niska gęstość, długa pogłos)", L"Sıfır yerçekimi hangarı (düşük yoğunluk, uzun yankı)"));
	m_env.AddString(LL14(L"惑星ドーム都市 (超巨大＋ガラス反射)", L"Planet dome city (vast + glass reflection)", L"Ville dôme planétaire (vaste + réflexion vitrée)", L"Città a cupola planetaria (vasta + riflesso vetroso)", L"Ciudad cúpula planetaria (vasta + reflexión de vidrio)", L"행성 돔 도시 (초거대 + 유리 반사)", L"行星穹顶城市（超巨大＋玻璃反射）", L"مدينة قبة كوكبية (شاسعة + انعكاس زجاجي)", L"Купольный город на планете (огромный + стеклянные отражения)", L"Planetare Kuppelstadt (riesig + Glasreflexion)", L"Cidade cúpula planetária (vasta + reflexo de vidro)", L"Planeetkoepelstad (uitgestrekt + glasreflectie)", L"Kopułowe miasto na planecie (ogromne + szklane odbicia)", L"Gezegen kubbe şehri (devasa + cam yansıması)"));
	m_env.AddString(LL14(L"VRシミュレーター (過剰ステレオ＋揺れ)", L"VR simulator (excessive stereo + sway)", L"Simulateur VR (stéréo excessif + balancement)", L"Simulatore VR (stereo eccessivo + oscillazione)", L"Simulador VR (estéreo excesivo + balanceo)", L"VR 시뮬레이터 (과도한 스테레오 + 흔들림)", L"VR模拟器（过度立体声＋摇摆）", L"محاكي VR (ستريو مفرط + تأرجح)", L"VR-симулятор (избыточное стерео + покачивание)", L"VR-Simulator (exzessives Stereo + Schwanken)", L"Simulador VR (estéreo excessivo + balanço)", L"VR-simulator (overmatig stereo + wiebelen)", L"Symulator VR (nadmierne stereo + kołysanie)", L"VR simülatörü (aşırı stereo + sallanma)"));
	m_env.AddString(LL14(L"レーザー通路 (鋭いフラッター、硬質)", L"Laser corridor (sharp flutter, hard)", L"Couloir laser (flutter aigu, dur)", L"Corridoio laser (flutter acuto, duro)", L"Pasillo láser (flutter agudo, duro)", L"레이저 통로 (날카로운 플러터, 경질)", L"激光通道（尖锐颤动、硬质）", L"ممر ليزر (رفرفة حادة، صلب)", L"Лазерный коридор (резкий флаттер, жёсткий)", L"Laserkorridor (scharfes Flutter, hart)", L"Corredor a laser (flutter agudo, duro)", L"Laserkorridor (scherpe flutter, hard)", L"Korytarz laserowy (ostry flutter, twardy)", L"Lazer koridoru (keskin flutter, sert)"));
	m_env.AddString(LL14(L"異次元裂け目 (不規則ディレイ、崩れる残響)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Faille dimensionnelle (délai irrégulier, réverb qui s'effondre)", L"Fenditura dimensionale (ritardo irregolare, riverbero che crolla)", L"Grieta dimensional (retardo irregular, reverb que se desmorona)", L"이차원 균열 (불규칙 딜레이, 무너지는 잔향)", L"异次元裂隙（不规则延迟、崩解混响）", L"شق بعدي (تأخير غير منتظم، صدى متهاوي)", L"Межмерный разлом (нерегулярная задержка, рушащаяся реверберация)", L"Dimensionsriss (unregelmäßiges Delay, zerfallender Hall)", L"Fissura dimensional (atraso irregular, reverb desmoronando)", L"Dimensionale scheur (onregelmatige vertraging, instortende nagalm)", L"Szczelina wymiarowa (nieregularne opóźnienie, kurząca się pogłos)", L"Boyutsal yarık (düzensiz gecikme, çöken yankı)"));
	m_env.AddString(LL14(L"夢の中 (柔らかく滲む、低コントラスト)", L"Dream space (soft bleeding, low contrast)", L"Espace onirique (fusion douce, faible contraste)", L"Spazio onirico (sfumatura morbida, basso contrasto)", L"Espacio onírico (difuminado suave, bajo contraste)", L"꿈속 (부드럽게 번지는, 낮은 대비)", L"梦境（柔和渗散、低对比）", L"فضاء حلم (تشوش ناعم، تباين منخفض)", L"Пространство сна (мягкое размывание, низкий контраст)", L"Traumraum (weiches Verschwimmen, geringer Kontrast)", L"Espaço onírico (difusão suave, baixo contraste)", L"Droomruimte (zachte vervaging, laag contrast)", L"Przestrzeń snu (miękkie rozmycie, niski kontrast)", L"Rüya alanı (yumuşak yayılma, düşük kontrast)"));
	m_env.AddString(LL14(L"水晶洞 (高域きらめき、長い余韻)", L"Crystal cave (high-frequency shimmer, long decay)", L"Grotte de cristal (scintillement aigu, longue décroissance)", L"Grotta di cristallo (scintillio ad alta frequenza, lungo decay)", L"Cueva de cristal (brillo agudo, larga decadencia)", L"수정 동굴 (고역 반짝임, 긴 여운)", L"水晶洞（高频闪烁、长余韵）", L"كهف بلوري (وميض عالي التردد، تلاشٍ طويل)", L"Кристальная пещера (мерцание высоких частот, длинный затух)", L"Kristallhöhle (Hochfrequenz-Schimmer, langer Ausklang)", L"Caverna de cristal (brilho agudo, longo decay)", L"Kristalgrot (hoge-frequentie glinster, lange uitklinking)", L"Krystalna jaskinia (migotanie wysokich tonów, długi zanik)", L"Kristal mağara (yüksek frekans parıltısı, uzun sönüm)"));
	m_env.AddString(LL14(L"廃宇宙ステーション (冷たく乾いた残響)", L"Abandoned space station (cold dry reverb)", L"Station spatiale abandonnée (réverb froide et sèche)", L"Stazione spaziale abbandonata (riverbero freddo e secco)", L"Estación espacial abandonada (reverb fría y seca)", L"폐허 우주 정거장 (차갑고 건조한 잔향)", L"废弃空间站（冷冽干燥的混响）", L"محطة فضائية مهجورة (صدى بارد وجاف)", L"Заброшенная космическая станция (холодная сухая реверберация)", L"Verlassene Raumstation (kalte trockene Hall)", L"Estação espacial abandonada (reverb fria e seca)", L"Verlaten ruimtestation (koude droge nagalm)", L"Opuszczona stacja kosmiczna (zimna sucha pogłos)", L"Terk edilmiş uzay istasyonu (soğuk kuru yankı)"));
	m_env.AddString(LL14(L"ブラックホール縁 (超長残響＋低域膨張)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Bord du trou noir (réverb ultra-longue + expansion des graves)", L"Bordo del buco nero (riverbero ultra-lungo + espansione dei bassi)", L"Borde del agujero negro (reverb ultralarga + expansión de graves)", L"블랙홀 가장자리 (초장 잔향 + 저역 확장)", L"黑洞边缘（超长混响＋低频扩展）", L"حافة الثقب الأسود (صدى فائق الطول + توسع للجهير)", L"Край чёрной дыры (сверхдлинная реверберация + расширение басов)", L"Rand eines Schwarzen Lochs (ultralanger Hall + Bassausdehnung)", L"Borda do buraco negro (reverb ultralonga + expansão de graves)", L"Rand van zwart gat (ultralange nagalm + basuitbreiding)", L"Krawędź czarnej dziury (ultradługa pogłos + rozszerzenie basów)", L"Kara delik kenarı (ultra uzun yankı + bas genişlemesi)"));
	m_env.AddString(LL14(L"サイバー聖堂 (金属×巨大空間、光沢残響)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cathédrale cyber (métal × vaste espace, réverb brillante)", L"Cattedrale cyber (metallo × vasto spazio, riverbero lucido)", L"Catedral ciber (metal × espacio vasto, reverb brillante)", L"사이버 성당 (금속×거대 공간, 광택 잔향)", L"赛博大教堂（金属×巨大空间、光泽混响）", L"كاتدرائية سيبر (معدن × مساحة شاسعة، صدى لامع)", L"Кибер-собор (металл × огромное пространство, блестящая реверберация)", L"Cyber-Kathedrale (Metall × riesiger Raum, glänzender Hall)", L"Catedral cibernética (metal × espaço vasto, reverb brilhante)", L"Cyberkathedraal (metaal × uitgestrekte ruimte, glanzende nagalm)", L"Cyberkatedra (metal × ogromna przestrzeń, błyszcząca pogłos)", L"Siber katedral (metal × geniş alan, parlak yankı)"));

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

extern BOOL reset;
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
	AckEqKeyUiNotify();
#if 0
	{
		static DWORD s_last = 0, s_count = 0;
		const DWORD now = GetTickCount();
		++s_count;
		if (s_last == 0) s_last = now;
		if (now - s_last >= 1000) {
			CString line;
			line.Format(L"[EQ-KEY] updates/sec=%u\n", s_count);
			OutputDebugString(line);
			s_count = 0;
			s_last = now;
		}
	}
#endif
	return 0;
}


void CEqualizer::OnDestroy()
{
	UnregisterEqKeyUiHwnd(m_hWnd);
	KillTimer(1);
	if (g_eqHelpDlg && ::IsWindow(g_eqHelpDlg->GetSafeHwnd()))
		g_eqHelpDlg->DestroyWindow();
	CCustomBlurDialogExBase::OnDestroy();
}

void CEqualizer::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CEqualizer::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CEqualizer::ShowHelpSheet()
{
	if (g_eqHelpDlg && ::IsWindow(g_eqHelpDlg->GetSafeHwnd())) {
		g_eqHelpDlg->ShowWindow(SW_SHOW);
		g_eqHelpDlg->SetForegroundWindow();
		return;
	}
	if (g_eqHelpDlg && !::IsWindow(g_eqHelpDlg->GetSafeHwnd()))
		g_eqHelpDlg = nullptr;
	CEqHelpDlg* dlg = new CEqHelpDlg(nullptr);
	if (!dlg->Create(IDD_EQ_HELP, nullptr)) {
		delete dlg;
		return;
	}
	g_eqHelpDlg = dlg;
	dlg->ShowWindow(SW_SHOW);
	dlg->SetForegroundWindow();
}

void CEqualizer::OnBnClickedHelp()
{
	ShowHelpSheet();
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
