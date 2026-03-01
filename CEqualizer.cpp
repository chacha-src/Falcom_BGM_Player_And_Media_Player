// CEqualizer.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CEqualizer.h"


// CEqualizer ダイアログ

IMPLEMENT_DYNAMIC(CEqualizer, CCustomDialogEx)

CEqualizer::CEqualizer(CWnd* pParent /*=nullptr*/)
	: CCustomDialogEx(IDD_EQUALIZER, pParent)
{

}

CEqualizer::~CEqualizer()
{
}

void CEqualizer::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SLIDER7, m_s0);
	DDX_Control(pDX, IDC_SLIDER9, m_s1);
	DDX_Control(pDX, IDC_SLIDER8, m_s2);
	DDX_Control(pDX, IDC_SLIDER10, m_s3);
	DDX_Control(pDX, IDC_SLIDER11, m_s4);
	DDX_Control(pDX, IDC_SLIDER12, m_s5);
	DDX_Control(pDX, IDC_SLIDER13, m_s6);
	DDX_Control(pDX, IDC_SLIDER14, m_s7);
	DDX_Control(pDX, IDC_SLIDER15, m_s8);
	DDX_Control(pDX, IDC_SLIDER16, m_s9);
	DDX_Control(pDX, IDC_STATIC_e0, m_v0);
	DDX_Control(pDX, IDC_STATIC_e1, m_v1);
	DDX_Control(pDX, IDC_STATIC_e2, m_v2);
	DDX_Control(pDX, IDC_STATIC_e3, m_v3);
	DDX_Control(pDX, IDC_STATIC_e4, m_v4);
	DDX_Control(pDX, IDC_STATIC_e5, m_v5);
	DDX_Control(pDX, IDC_STATIC_e6, m_v6);
	DDX_Control(pDX, IDC_STATIC_e7, m_v7);
	DDX_Control(pDX, IDC_STATIC_e8, m_v8);
	DDX_Control(pDX, IDC_STATIC_e9, m_v9);
	DDX_Control(pDX, IDC_COMBO1, m_env);
	DDX_Control(pDX, IDC_COMBO5, m_pre);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDOK3, dum);
	DDX_Control(pDX, IDC_STATIC_e10, m_v10);
	DDX_Control(pDX, IDC_STATIC_e11, m_v11);
	DDX_Control(pDX, IDC_STATIC_e12, m_v12);
	DDX_Control(pDX, IDC_STATIC_e13, m_v13);
	DDX_Control(pDX, IDC_STATIC_e14, m_v14);
	DDX_Control(pDX, IDC_SLIDER21, m_s14);
	DDX_Control(pDX, IDC_SLIDER20, m_s13);
	DDX_Control(pDX, IDC_SLIDER19, m_s12);
	DDX_Control(pDX, IDC_SLIDER18, m_s11);
	DDX_Control(pDX, IDC_SLIDER17, m_s10);
	DDX_Control(pDX, IDC_STATIC_eff, m_seff);
	DDX_Control(pDX, IDC_SLIDER22, m_eff);
	DDX_Control(pDX, IDC_SLIDER23, m_smaster);
	DDX_Control(pDX, IDC_SLIDER24, m_ssenmei);
	DDX_Control(pDX, IDC_SLIDER25, m_skoutei);
	DDX_Control(pDX, IDC_SLIDER26, m_smitsudo);
	DDX_Control(pDX, IDC_SLIDER27, m_srittai);
	DDX_Control(pDX, IDC_STATIC_e15, m_vmaster);
	DDX_Control(pDX, IDC_STATIC_e16, m_vsenmei);
	DDX_Control(pDX, IDC_STATIC_e17, m_vkoutei);
	DDX_Control(pDX, IDC_STATIC_e18, m_vmitsudo);
	DDX_Control(pDX, IDC_STATIC_e19, m_vrittai);
	DDX_Control(pDX, IDOK4, sdasdsdadsd);
	DDX_Control(pDX, IDC_STATICf, m_t);
	DDX_Control(pDX, IDC_STATIC_key, m_keyLow);
	DDX_Control(pDX, IDC_STATIC_key2, m_keyMid);
	DDX_Control(pDX, IDC_STATIC_key3, m_keyHigh);
	DDX_Control(pDX, IDC_STATIC_key4, m_keyAll);
}


BEGIN_MESSAGE_MAP(CEqualizer, CCustomDialogEx)
	ON_CBN_SELCHANGE(IDC_COMBO1, &CEqualizer::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO5, &CEqualizer::OnCbnSelchangeCombo5)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDOK3, &CEqualizer::OnBnClickedOk3)
	ON_BN_CLICKED(IDOK, &CEqualizer::OnBnClickedOk)
	ON_BN_CLICKED(IDOK4, &CEqualizer::OnBnClickedOk4)
END_MESSAGE_MAP()

extern save savedata;
extern int stflg;

// CEqualizer メッセージ ハンドラー
BOOL CEqualizer::OnInitDialog()
{
	CCustomDialogEx::OnInitDialog();

	SetWindowText(LL2(L"イコライザー", L"Equalizer"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	SetDlgItemText(IDOK3, LL2(L"イコライザーリセット", L"Equalizer reset"));
	SetDlgItemText(IDOK4, LL2(L"グローバルリセット", L"Global reset"));
	SetDlgItemText(IDC_STATIC_EQ_DRY, LL2(L"乾", L"Dry"));
	SetDlgItemText(IDC_STATIC_EQ_WET, LL2(L"ウェット", L"Wet"));
	SetDlgItemText(IDC_STATIC_EQ_ACOUSTIC, LL2(L"音響空間の音響モデル", L"Acoustic space model"));
	SetDlgItemText(IDC_STATIC_EQ_SPECTRUM, LL2(L"スペクトル", L"Spectrum"));
	SetDlgItemText(IDC_STATIC_EQ_FREQ, LL2(L"周波数", L"Frequency"));
	SetDlgItemText(IDC_STATIC_EQ_BAND, LL2(L"帯", L"Band"));
	SetDlgItemText(IDC_STATIC_EQ_LOUDNESS, LL2(L"響度", L"Loudness"));
	SetDlgItemText(IDC_STATIC_EQ_WARMTH, LL2(L"温かさ", L"Warmth"));
	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL2(L"閉じます", L"Close"));
	m_tooltip.AddTool(GetDlgItem(IDOK3), LL2(L"イコライザーの値をリセットします", L"Reset equalizer values"));
	m_tooltip.AddTool(GetDlgItem(IDOK4), LL2(L"グローバルの値をリセットします", L"Reset global values"));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);


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
	
	CFont* pFont = m_t.GetFont();
	LOGFONT lf;
	CFont m_newFont;

	if (pFont != NULL)
	{
		// 現在のフォント設定をLOGFONT構造体にコピーします
		pFont->GetLogFont(&lf);
	}
	else
	{
		// フォントが取得できない場合のデフォルト設定（MS UI Gothicなど）
		memset(&lf, 0, sizeof(LOGFONT));
		lf.lfHeight = -12; // 標準的なサイズ
		_tcscpy_s(lf.lfFaceName, _T("MS UI Gothic"));
	}

	// 2. サイズを4倍に変更します
	// lfHeightは通常負の値（デバイス単位）で指定されているため、単純に4倍します
	lf.lfHeight *= 4;
	lf.lfItalic = TRUE;
	// 3. 以前に作成したフォントがあれば削除してから再作成します
	if (m_newFont.GetSafeHandle())
	{
		m_newFont.DeleteObject();
	}
	m_newFont.CreateFontIndirect(&lf);

	// 4. コントロールにフォントを適用します
	m_t.SetFont(&m_newFont);
	
	m_t.SetPreferWideMode(TRUE);
	m_t.SetGradation(COLOR_GRAD_DARK_GREEN, COLOR_RANGE_SELECTION, 135, TRUE); // 135 左上から右下
	m_t.SetDropShadow(RGB(0,0,0), 45, 15, 6, TRUE);

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
	m_eff.SetPos(savedata.eqsoundeffect * 2);
	s.Format(L"%d", savedata.eqsoundeffect * 2);
	m_seff.SetWindowText(s);

	m_smaster.SetRange(0, 200);
	m_smaster.SetPos(200 - savedata.eq[15]);
	s.Format(L"%d", savedata.eq[15]);
	m_vmaster.SetWindowText(s);

	m_ssenmei.SetRange(0, 200);
	m_ssenmei.SetPos(200 - savedata.eq[16]);
	s.Format(L"%d", savedata.eq[16]);
	m_vsenmei.SetWindowText(s);

	m_skoutei.SetRange(0, 200);
	m_skoutei.SetPos(200 - savedata.eq[17]);
	s.Format(L"%d", savedata.eq[17]);
	m_vkoutei.SetWindowText(s);

	m_smitsudo.SetRange(0, 200);
	m_smitsudo.SetPos(200 - savedata.eq[18]);
	s.Format(L"%d", savedata.eq[18]);
	m_vmitsudo.SetWindowText(s);

	m_srittai.SetRange(0, 200);
	m_srittai.SetPos(200 - savedata.eq[19]);
	s.Format(L"%d", savedata.eq[19]);
	m_vrittai.SetWindowText(s);

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

	// 環境音響プリセット101種
	
//0
	m_env.AddString(LL2(L"なし", L"None"));
	//1
	m_env.AddString(LL2(L"--[[基本空間 1-10]]--", L"--[[Basic space 1-10]]--"), TRUE);
	//2
	m_env.AddString(LL2(L"風呂場 (超短く超明るい、ピーキーな金属反射)", L"Bathroom (very short, bright, peaky metal reflection)"));
	m_env.AddString(LL2(L"ホール (中程度だがはっきり響く、バランス良好)", L"Hall (moderate but clear, balanced)"));
	m_env.AddString(LL2(L"教会 (超長く超重厚、圧倒的な残響)", L"Church (very long, massive reverb)"));
	m_env.AddString(LL2(L"洞窟 (長く暗く湿った、こもった響き)", L"Cave (long, dark, wet, muffled)"));
	m_env.AddString(LL2(L"スタジオ (極めてドライ、ほぼ無響)", L"Studio (very dry, nearly anechoic)"));
	m_env.AddString(LL2(L"ライブハウス (パンチがあって賑やか、エネルギッシュ)", L"Live house (punchy, lively, energetic)"));
	m_env.AddString(LL2(L"森 (拡散的で柔らかい、包み込む自然)", L"Forest (diffuse, soft, enveloping)"));
	m_env.AddString(LL2(L"山 (超長いエコー、遠くまではっきり響く)", L"Mountain (long echo, clear far)"));
	m_env.AddString(LL2(L"広場 (開放的、空気を感じる広がり)", L"Plaza (open, airy)"));
	m_env.AddString(LL2(L"カテドラル (超巨大空間、圧倒的な残響と重厚感)", L"Cathedral (huge space, massive reverb)"));
	//12
	m_env.AddString(LL2(L"--[[公共施設 11-20]]--", L"--[[Public 11-20]]--"), TRUE);
	//13
	m_env.AddString(LL2(L"体育館 (硬く金属的、バスケコート的な響き)", L"Gymnasium (hard, metallic, basketball-court style reflection)"));
	m_env.AddString(LL2(L"峡谷 (複数の明確なエコー、両側から反響)", L"Canyon (multiple distinct echoes, reflections from both sides)"));
	m_env.AddString(LL2(L"地下室 (狭く圧迫感、密度の高い反射)", L"Basement (cramped, dense reflections)"));
	m_env.AddString(LL2(L"劇場 (音響設計された空間、明瞭でバランス良好)", L"Theater (acoustically designed space, clear and balanced)"));
	m_env.AddString(LL2(L"水中 (特殊な密度、こもった独特の響き)", L"Underwater (unique density, muffled distinctive sound)"));
	m_env.AddString(LL2(L"トンネル/地下道 (フラッターエコー、平行壁面の連続反射)", L"Tunnel/Underground (flutter echo, parallel wall reflections)"));
	m_env.AddString(LL2(L"アリーナ/ドーム (超巨大スポーツ施設、観客席の吸音と長残響)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)"));
	m_env.AddString(LL2(L"小部屋/クローゼット (超小空間デッド、ほぼ無反射)", L"Small room/Closet (very dead space, nearly no reflection)"));
	m_env.AddString(LL2(L"階段室 (縦方向の特殊反射、螺旋的な響き)", L"Stairwell (vertical reflections, spiral-like reverb)"));
	m_env.AddString(LL2(L"地下鉄ホーム (都市的コンクリート、硬質な反射)", L"Subway platform (urban concrete, hard reflections)"));

	m_env.AddString(LL2(L"--[[産業・商業 21-30]]--", L"--[[Industrial 21-30]]--"), TRUE);

	m_env.AddString(LL2(L"倉庫 (大きく空っぽ、高天井で硬い床)", L"Warehouse (large and empty, high ceiling, hard floor)"));
	m_env.AddString(LL2(L"廊下 (長く狭い直線的、方向性のある反響)", L"Corridor (long narrow linear, directional reflections)"));
	m_env.AddString(LL2(L"工場 (金属的産業的、複雑な反響)", L"Factory (metallic industrial, complex reflections)"));
	m_env.AddString(LL2(L"寺社仏閣 (木造の温かみ、柔らかい反射)", L"Temple/Shrine (wooden warmth, soft reflections)"));
	m_env.AddString(LL2(L"宇宙空間 (SF特殊空間、無重力感と極端な残響)", L"Outer space (SF special space, zero-g feel and extreme reverb)"));
	m_env.AddString(LL2(L"野球場/サッカー場 (屋外超大型、遠距離反射と開放感)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)"));
	m_env.AddString(LL2(L"図書館 (静寂で吸音的、控えめな反射)", L"Library (quiet and absorbent, subtle reflections)"));
	m_env.AddString(LL2(L"プール(室内) (タイル水面反射、独特の明るい響き)", L"Pool (indoor) (tile and water reflections, unique bright sound)"));
	m_env.AddString(LL2(L"エレベーター (超小金属空間、密閉された短い反射)", L"Elevator (tiny metal space, confined short reflections)"));
	m_env.AddString(LL2(L"駐車場 (広い低天井コンクリート、硬質な反響)", L"Parking lot (wide low-ceiling concrete, hard reflections)"));

	m_env.AddString(LL2(L"--[[文化施設 31-40]]--", L"--[[Cultural 31-40]]--"), TRUE);

	m_env.AddString(LL2(L"コンサートホール (クラシック用最高峰、精密な音響設計)", L"Concert hall (classical pinnacle, precise acoustic design)"));
	m_env.AddString(LL2(L"ジャズクラブ (親密で温かい、程よい残響)", L"Jazz club (intimate and warm, moderate reverb)"));
	m_env.AddString(LL2(L"カラオケボックス (小密室エンタメ、明るく賑やか)", L"Karaoke box (small enclosed entertainment, bright and lively)"));
	m_env.AddString(LL2(L"映画館 (THX規格的、臨場感のある音響)", L"Movie theater (THX standard, immersive sound)"));
	m_env.AddString(LL2(L"地下鉄車内 (揺れる密室、硬質で圧迫感)", L"Subway car (shaking enclosed space, hard and oppressive)"));
	m_env.AddString(LL2(L"空港ターミナル (巨大公共空間、高天井と複雑な反射)", L"Airport terminal (vast public space, high ceiling and complex reflections)"));
	m_env.AddString(LL2(L"ショッピングモール (賑やか商業施設、適度な吸音)", L"Shopping mall (lively commercial facility, moderate absorption)"));
	m_env.AddString(LL2(L"病院 (静かで清潔、吸音材による落ち着いた空間)", L"Hospital (quiet and clean, calm space with absorption)"));
	m_env.AddString(LL2(L"レコーディングブース (プロ用極ドライ、完全無響に近い)", L"Recording booth (pro ultra-dry, nearly anechoic)"));
	m_env.AddString(LL2(L"オペラハウス (劇場の最高峰、豊かで美しい残響)", L"Opera house (theater pinnacle, rich and beautiful reverb)"));

	m_env.AddString(LL2(L"--[[生活空間 41-50]]--", L"--[[Living 41-50]]--"), TRUE);

	m_env.AddString(LL2(L"喫茶店/カフェ (適度な賑わいと吸音、リラックスした空間)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)"));
	m_env.AddString(LL2(L"バー/ラウンジ (暗く落ち着いた雰囲気、中域重視)", L"Bar/Lounge (dark calm atmosphere, mid-focused)"));
	m_env.AddString(LL2(L"居酒屋 (賑やか木材吸音、温かみのある響き)", L"Izakaya (lively wood absorption, warm sound)"));
	m_env.AddString(LL2(L"美術館/博物館 (静かで広い高天井、上品な残響)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)"));
	m_env.AddString(LL2(L"講堂/大学教室 (教育施設の反射、明瞭な音響)", L"Auditorium/University classroom (educational facility reflections, clear sound)"));
	m_env.AddString(LL2(L"竹林 (和風自然音響、独特の拡散と風の音)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)"));
	m_env.AddString(LL2(L"渓谷/滝 (水の反射と濡れた岩肌、躍動感ある響き)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)"));
	m_env.AddString(LL2(L"砂漠 (超開放的反射極小、乾いた空気感)", L"Desert (wide open, minimal reflections, dry air)"));
	m_env.AddString(LL2(L"ガレージ (車庫硬質空間、コンクリートと金属)", L"Garage (hard space, concrete and metal)"));
	m_env.AddString(LL2(L"展望台 (高所開放感、風と遠距離エコー)", L"Observation deck (elevated openness, wind and distant echo)"));

	m_env.AddString(LL2(L"--[[拡張空間 51-60]]--", L"--[[Extended 51-60]]--"), TRUE);

	m_env.AddString(LL2(L"小さな礼拝堂 (教会より親密で温かい)", L"Small chapel (more intimate and warm than church)"));
	m_env.AddString(LL2(L"大型ショッピングセンター (モールより巨大)", L"Large shopping center (bigger than mall)"));
	m_env.AddString(LL2(L"地下洞窟(深層) (より深く神秘的)", L"Underground cave (deep) (deeper and more mystical)"));
	m_env.AddString(LL2(L"古城の大広間 (石造り中世的)", L"Castle great hall (stone medieval)"));
	m_env.AddString(LL2(L"野外音楽堂 (半開放的ステージ)", L"Outdoor amphitheater (semi-open stage)"));
	m_env.AddString(LL2(L"鍾乳洞 (複雑な水滴反射)", L"Limestone cave (complex water droplet reflections)"));
	m_env.AddString(LL2(L"廃墟工場 (荒廃した金属空間)", L"Abandoned factory (decayed metal space)"));
	m_env.AddString(LL2(L"和室(畳) (日本的柔らかい吸音)", L"Japanese room (tatami) (Japanese soft absorption)"));
	m_env.AddString(LL2(L"温泉施設 (湿度高めタイル反射)", L"Hot spring facility (humid tile reflections)"));
	m_env.AddString(LL2(L"屋根裏部屋 (斜め天井の特殊空間)", L"Attic (angled ceiling special space)"));

	m_env.AddString(LL2(L"--[[特殊空間 61-70]]--", L"--[[Special 61-70]]--"), TRUE);

	m_env.AddString(LL2(L"地下駐車場(多層) (階層的複雑反射)", L"Underground parking (multi-level) (layered complex reflections)"));
	m_env.AddString(LL2(L"古い劇場(木造) (温かみある音響設計)", L"Old theater (wooden) (warm acoustic design)"));
	m_env.AddString(LL2(L"大型倉庫(空) (極端な空虚感)", L"Large warehouse (empty) (extreme emptiness)"));
	m_env.AddString(LL2(L"小さな教会 (カテドラルより親密)", L"Small church (more intimate than cathedral)"));
	m_env.AddString(LL2(L"ガラス温室 (硬質ガラス反射)", L"Glass greenhouse (hard glass reflections)"));
	m_env.AddString(LL2(L"石造りトンネル (硬く長い残響)", L"Stone tunnel (hard long reverb)"));
	m_env.AddString(LL2(L"コンクリート階段 (硬質縦方向反射)", L"Concrete stairs (hard vertical reflections)"));
	m_env.AddString(LL2(L"大浴場 (広いタイル反射)", L"Public bath (wide tile reflections)"));
	m_env.AddString(LL2(L"洗面所 (極小タイル空間)", L"Bathroom (tiny tile space)"));
	m_env.AddString(LL2(L"廊下(カーペット) (吸音的柔らかい)", L"Corridor (carpeted) (absorptive and soft)"));

	m_env.AddString(LL2(L"--[[専門空間 71-80]]--", L"--[[Professional 71-80]]--"), TRUE);

	m_env.AddString(LL2(L"会議室(大) (ビジネス空間)", L"Meeting room (large) (business space)"));
	m_env.AddString(LL2(L"会議室(小) (より密閉的)", L"Meeting room (small) (more enclosed)"));
	m_env.AddString(LL2(L"防音室 (極端なデッド空間)", L"Soundproof room (extreme dead space)"));
	m_env.AddString(LL2(L"エントランスホール (高天井開放的)", L"Entrance hall (high ceiling, open)"));
	m_env.AddString(LL2(L"書斎 (本による吸音)", L"Study (absorption from books)"));
	m_env.AddString(LL2(L"キッチン (硬質多反射)", L"Kitchen (hard multi-reflections)"));
	m_env.AddString(LL2(L"屋外駐車場 (開放的反射少)", L"Outdoor parking lot (open, fewer reflections)"));
	m_env.AddString(LL2(L"地下道(狭) (圧迫的狭小空間)", L"Underground passage (narrow) (oppressive confined space)"));
	m_env.AddString(LL2(L"展示室 (美術館より吸音的)", L"Exhibition room (more absorbent than gallery)"));
	m_env.AddString(LL2(L"アトリエ (創作空間の独特さ)", L"Atelier (unique creative space)"));

	m_env.AddString(LL2(L"--[[SFX/未来 81-100]]--", L"--[[SFX/Future 81-100]]--"), TRUE);
	m_env.AddString(LL2(L"サイバーパンク路地 (金属反射＋狭い空間、ネオン感)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)"));
	m_env.AddString(LL2(L"宇宙船ブリッジ (クリーンで硬質、短い反射)", L"Spaceship bridge (clean and hard, short reflections)"));
	m_env.AddString(LL2(L"ワープトンネル (揺らぎと長い残響、引き伸ばし)", L"Warp tunnel (fluctuation and long reverb, stretching)"));
	m_env.AddString(LL2(L"量子ホール (不安定拡散、浮遊感)", L"Quantum hall (unstable diffusion, floating feel)"));
	m_env.AddString(LL2(L"無限回廊 (規則的エコー、長く続く反射)", L"Infinite corridor (regular echo, long-lasting reflections)"));
	m_env.AddString(LL2(L"逆再生空間 (早い反射と遅い尾、異常な広がり)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)"));
	m_env.AddString(LL2(L"タイムストップ室 (ほぼ無響＋硬い反射)", L"Time-stop room (nearly anechoic + hard reflection)"));
	m_env.AddString(LL2(L"データセンター (低域振動、機械的反射)", L"Data center (low-frequency vibration, mechanical reflection)"));
	m_env.AddString(LL2(L"巨大機械内部 (金属共鳴、重い反射)", L"Inside giant machine (metal resonance, heavy reflection)"));
	m_env.AddString(LL2(L"AIホログラム室 (透明感、明るい反射)", L"AI hologram room (transparency, bright reflection)"));
	m_env.AddString(LL2(L"重力ゼロ船庫 (低密度で長残響)", L"Zero-gravity hangar (low density, long reverb)"));
	m_env.AddString(LL2(L"惑星ドーム都市 (超巨大＋ガラス反射)", L"Planet dome city (vast + glass reflection)"));
	m_env.AddString(LL2(L"VRシミュレーター (過剰ステレオ＋揺れ)", L"VR simulator (excessive stereo + sway)"));
	m_env.AddString(LL2(L"レーザー通路 (鋭いフラッター、硬質)", L"Laser corridor (sharp flutter, hard)"));
	m_env.AddString(LL2(L"異次元裂け目 (不規則ディレイ、崩れる残響)", L"Dimensional rift (irregular delay, crumbling reverb)"));
	m_env.AddString(LL2(L"夢の中 (柔らかく滲む、低コントラスト)", L"Dream space (soft bleeding, low contrast)"));
	m_env.AddString(LL2(L"水晶洞 (高域きらめき、長い余韻)", L"Crystal cave (high-frequency shimmer, long decay)"));
	m_env.AddString(LL2(L"廃宇宙ステーション (冷たく乾いた残響)", L"Abandoned space station (cold dry reverb)"));
	m_env.AddString(LL2(L"ブラックホール縁 (超長残響＋低域膨張)", L"Black hole edge (ultra-long reverb + bass expansion)"));
	m_env.AddString(LL2(L"サイバー聖堂 (金属×巨大空間、光沢残響)", L"Cyber cathedral (metal × vast space, glossy reverb)"));

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
	m_pre.AddString(LL2(L"デフォルト", L"Default"));
	m_pre.AddString(LL2(L"低音ブースト", L"Bass Boost"));
	m_pre.AddString(LL2(L"高音ブースト", L"Treble Boost"));
	m_pre.AddString(LL2(L"ボーカル強調", L"Vocal Enhance"));
	m_pre.AddString(LL2(L"低音カット", L"Bass Cut"));
	m_pre.AddString(LL2(L"高音カット", L"Treble Cut"));
	m_pre.AddString(LL2(L"ラウドネス", L"Loudness"));
	m_pre.AddString(LL2(L"クラシック", L"Classical"));
	m_pre.AddString(LL2(L"ロック", L"Rock"));
	m_pre.AddString(LL2(L"カスタム", L"Custom"));
	m_pre.AddString(LL2(L"ジャズ", L"Jazz"));
	m_pre.AddString(LL2(L"ポップ", L"Pop"));
	m_pre.AddString(LL2(L"EDM", L"EDM"));
	m_pre.AddString(LL2(L"メタル", L"Metal"));
	m_pre.AddString(LL2(L"ヒップホップ", L"Hip Hop"));
	m_pre.AddString(LL2(L"アコースティック", L"Acoustic"));
	m_pre.AddString(LL2(L"V字型(ドンシャリ)", L"V-shape"));
	m_pre.AddString(LL2(L"逆V字型", L"Inverse V"));
	m_pre.AddString(LL2(L"スマイルカーブ", L"Smile curve"));
	m_pre.AddString(LL2(L"ラジオ/Podcast", L"Radio/Podcast"));
	m_pre.AddString(LL2(L"映画/ドラマ", L"Movie/Drama"));
	m_pre.AddString(LL2(L"ゲーミング", L"Gaming"));
	m_pre.AddString(LL2(L"ライブ録音", L"Live recording"));
	m_pre.AddString(LL2(L"トレブルブースト", L"Treble Boost"));
	m_pre.AddString(LL2(L"ベースブースト", L"Bass Boost"));
	m_pre.AddString(LL2(L"小音量用", L"For low volume"));
	m_pre.AddString(LL2(L"ヘッドホン用", L"For headphones"));
	m_pre.AddString(LL2(L"ボーカル除去", L"Vocal remove"));
	m_pre.AddString(LL2(L"重低音強化", L"Subwoofer boost"));
	m_pre.AddString(LL2(L"ラジオAM", L"Radio AM"));
	m_pre.AddString(LL2(L"ラジオFM", L"Radio FM"));
	m_pre.AddString(LL2(L"テレビ音声", L"TV audio"));
	m_pre.AddString(LL2(L"電話音声", L"Phone voice"));
	m_pre.AddString(LL2(L"ビンテージ", L"Vintage"));
	m_pre.AddString(LL2(L"モダン", L"Modern"));
	m_pre.AddString(LL2(L"ウォーム", L"Warm"));
	m_pre.AddString(LL2(L"ブライト", L"Bright"));
	m_pre.AddString(LL2(L"フラット+", L"Flat+"));
	m_pre.AddString(LL2(L"スーパーベース", L"Super bass"));
	m_pre.AddString(LL2(L"クリスタル", L"Crystal"));
	m_pre.AddString(LL2(L"パーフェクト", L"Perfect"));
	m_pre.AddString(LL2(L"ダンス/クラブ", L"Dance/Club"));
	m_pre.AddString(LL2(L"R&&B/ソウル", L"R&B/Soul"));
	m_pre.AddString(LL2(L"レゲエ", L"Reggae"));
	m_pre.AddString(LL2(L"ブルース", L"Blues"));
	m_pre.AddString(LL2(L"カントリー", L"Country"));
	m_pre.AddString(LL2(L"ファンク", L"Funk"));
	m_pre.AddString(LL2(L"エレクトロニカ", L"Electronica"));
	m_pre.AddString(LL2(L"アンビエント", L"Ambient"));
	m_pre.AddString(LL2(L"インストゥルメンタル", L"Instrumental"));
	m_pre.AddString(LL2(L"ナレーション/オーディオブック", L"Narration/Audiobook"));
	m_pre.SetCurSel(savedata.eqsoundeq);

	if(savedata.eqx != -1)
		SetWindowPos(&CWnd::wndTop, savedata.eqx, savedata.eqy, 0, 0, SWP_NOSIZE| SWP_NOZORDER| SWP_NOOWNERZORDER);

	SetTimer(1, 50, NULL);
	return TRUE;
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

extern CString KeyCodeLow, KeyCodeMid, KeyCodeHigh, KeyCodeAll;
int backms = 0;
void CEqualizer::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
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


	vol = m_eff.GetPos();
	if(vol / 2 != savedata.eqsoundeffect) { s.Format(L"%d", vol); m_seff.SetWindowText(s); }
	savedata.eqsoundeffect = vol / 2;


	CRect rect;
	GetWindowRect(rect);
	savedata.eqx = rect.left;
	savedata.eqy = rect.top;


	// キーコード表示
	extern int playf;
	if (playf == 0) {
		KeyCodeLow  = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
		KeyCodeMid  = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
		KeyCodeHigh = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
		KeyCodeAll  = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
	}

	static CString bufLow = L"-", bufMid = L"-", bufHigh = L"-", bufAll = L"-";

	if (bufLow != KeyCodeLow) {
		m_keyLow.SetWindowText(CString(LL2(L"低音域：", L"Low: ")) + KeyCodeLow);
		bufLow = KeyCodeLow;
	}
	if (bufMid != KeyCodeMid) {
		m_keyMid.SetWindowText(CString(LL2(L"中音域：", L"Mid: ")) + KeyCodeMid);
		bufMid = KeyCodeMid;
	}
	if (bufHigh != KeyCodeHigh) {
		m_keyHigh.SetWindowText(CString(LL2(L"高音域：", L"High: ")) + KeyCodeHigh);
		bufHigh = KeyCodeHigh;
	}
	if (bufAll != KeyCodeAll) {
		m_keyAll.SetWindowText(CString(LL2(L"全音域：", L"All: ")) + KeyCodeAll);
		bufAll = KeyCodeAll;
	}


	CCustomDialogEx::OnTimer(nIDEvent);
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
	return CCustomDialogEx::PreTranslateMessage(pMsg);
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
	m_smaster.SetPos(200 - savedata.eq[15]);
	m_ssenmei.SetPos(200 - savedata.eq[16]);
	m_skoutei.SetPos(200 - savedata.eq[17]);
	m_smitsudo.SetPos(200 - savedata.eq[18]);
	m_srittai.SetPos(200 - savedata.eq[19]);
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
}

