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


	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), _T("閉じます"));
	m_tooltip.AddTool(GetDlgItem(IDOK3), _T("イコライザーの値をリセットします"));
	m_tooltip.AddTool(GetDlgItem(IDOK4), _T("グローバルの値をリセットします"));
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
	m_t.SetGradation(RGB(0, 0, 0), COLOR_RANGE_SELECTION, 135, TRUE);
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

	// 環境音響プリセット51種
	
	m_env.AddString(L"--[[基本空間 0-10]]--", TRUE);

	m_env.AddString(L"なし");
	m_env.AddString(L"風呂場 (超短く超明るい、ピーキーな金属反射)");
	m_env.AddString(L"ホール (中程度だがはっきり響く、バランス良好)");
	m_env.AddString(L"教会 (超長く超重厚、圧倒的な残響)");
	m_env.AddString(L"洞窟 (長く暗く湿った、こもった響き)");
	m_env.AddString(L"スタジオ (極めてドライ、ほぼ無響)");
	m_env.AddString(L"ライブハウス (パンチがあって賑やか、エネルギッシュ)");
	m_env.AddString(L"森 (拡散的で柔らかい、包み込む自然)");
	m_env.AddString(L"山 (超長いエコー、遠くまではっきり響く)");
	m_env.AddString(L"広場 (開放的、空気を感じる広がり)");
	m_env.AddString(L"カテドラル (超巨大空間、圧倒的な残響と重厚感)");

	m_env.AddString(L"--[[公共施設 11-20]]--", TRUE);

	m_env.AddString(L"体育館 (硬く金属的、バスケコート的な響き)");
	m_env.AddString(L"峡谷 (複数の明確なエコー、両側から反響)");
	m_env.AddString(L"地下室 (狭く圧迫感、密度の高い反射)");
	m_env.AddString(L"劇場 (音響設計された空間、明瞭でバランス良好)");
	m_env.AddString(L"水中 (特殊な密度、こもった独特の響き)");
	m_env.AddString(L"トンネル/地下道 (フラッターエコー、平行壁面の連続反射)");
	m_env.AddString(L"アリーナ/ドーム (超巨大スポーツ施設、観客席の吸音と長残響)");
	m_env.AddString(L"小部屋/クローゼット (超小空間デッド、ほぼ無反射)");
	m_env.AddString(L"階段室 (縦方向の特殊反射、螺旋的な響き)");
	m_env.AddString(L"地下鉄ホーム (都市的コンクリート、硬質な反射)");

	m_env.AddString(L"--[[産業・商業 21-30]]--", TRUE);

	m_env.AddString(L"倉庫 (大きく空っぽ、高天井で硬い床)");
	m_env.AddString(L"廊下 (長く狭い直線的、方向性のある反響)");
	m_env.AddString(L"工場 (金属的産業的、複雑な反響)");
	m_env.AddString(L"寺社仏閣 (木造の温かみ、柔らかい反射)");
	m_env.AddString(L"宇宙空間 (SF特殊空間、無重力感と極端な残響)");
	m_env.AddString(L"野球場/サッカー場 (屋外超大型、遠距離反射と開放感)");
	m_env.AddString(L"図書館 (静寂で吸音的、控えめな反射)");
	m_env.AddString(L"プール(室内) (タイル水面反射、独特の明るい響き)");
	m_env.AddString(L"エレベーター (超小金属空間、密閉された短い反射)");
	m_env.AddString(L"駐車場 (広い低天井コンクリート、硬質な反響)");

	m_env.AddString(L"--[[文化施設 31-40]]--", TRUE);

	m_env.AddString(L"コンサートホール (クラシック用最高峰、精密な音響設計)");
	m_env.AddString(L"ジャズクラブ (親密で温かい、程よい残響)");
	m_env.AddString(L"カラオケボックス (小密室エンタメ、明るく賑やか)");
	m_env.AddString(L"映画館 (THX規格的、臨場感のある音響)");
	m_env.AddString(L"地下鉄車内 (揺れる密室、硬質で圧迫感)");
	m_env.AddString(L"空港ターミナル (巨大公共空間、高天井と複雑な反射)");
	m_env.AddString(L"ショッピングモール (賑やか商業施設、適度な吸音)");
	m_env.AddString(L"病院 (静かで清潔、吸音材による落ち着いた空間)");
	m_env.AddString(L"レコーディングブース (プロ用極ドライ、完全無響に近い)");
	m_env.AddString(L"オペラハウス (劇場の最高峰、豊かで美しい残響)");

	m_env.AddString(L"--[[生活空間 41-50]]--", TRUE);

	m_env.AddString(L"喫茶店/カフェ (適度な賑わいと吸音、リラックスした空間)");
	m_env.AddString(L"バー/ラウンジ (暗く落ち着いた雰囲気、中域重視)");
	m_env.AddString(L"居酒屋 (賑やか木材吸音、温かみのある響き)");
	m_env.AddString(L"美術館/博物館 (静かで広い高天井、上品な残響)");
	m_env.AddString(L"講堂/大学教室 (教育施設の反射、明瞭な音響)");
	m_env.AddString(L"竹林 (和風自然音響、独特の拡散と風の音)");
	m_env.AddString(L"渓谷/滝 (水の反射と濡れた岩肌、躍動感ある響き)");
	m_env.AddString(L"砂漠 (超開放的反射極小、乾いた空気感)");
	m_env.AddString(L"ガレージ (車庫硬質空間、コンクリートと金属)");
	m_env.AddString(L"展望台 (高所開放感、風と遠距離エコー)");

	m_env.AddString(L"--[[拡張空間 51-60]]--", TRUE);

	m_env.AddString(L"小さな礼拝堂 (教会より親密で温かい)");
	m_env.AddString(L"大型ショッピングセンター (モールより巨大)");
	m_env.AddString(L"地下洞窟(深層) (より深く神秘的)");
	m_env.AddString(L"古城の大広間 (石造り中世的)");
	m_env.AddString(L"野外音楽堂 (半開放的ステージ)");
	m_env.AddString(L"鍾乳洞 (複雑な水滴反射)");
	m_env.AddString(L"廃墟工場 (荒廃した金属空間)");
	m_env.AddString(L"和室(畳) (日本的柔らかい吸音)");
	m_env.AddString(L"温泉施設 (湿度高めタイル反射)");
	m_env.AddString(L"屋根裏部屋 (斜め天井の特殊空間)");

	m_env.AddString(L"--[[特殊空間 61-70]]--", TRUE);

	m_env.AddString(L"地下駐車場(多層) (階層的複雑反射)");
	m_env.AddString(L"古い劇場(木造) (温かみある音響設計)");
	m_env.AddString(L"大型倉庫(空) (極端な空虚感)");
	m_env.AddString(L"小さな教会 (カテドラルより親密)");
	m_env.AddString(L"ガラス温室 (硬質ガラス反射)");
	m_env.AddString(L"石造りトンネル (硬く長い残響)");
	m_env.AddString(L"コンクリート階段 (硬質縦方向反射)");
	m_env.AddString(L"大浴場 (広いタイル反射)");
	m_env.AddString(L"洗面所 (極小タイル空間)");
	m_env.AddString(L"廊下(カーペット) (吸音的柔らかい)");

	m_env.AddString(L"--[[専門空間 71-80]]--", TRUE);

	m_env.AddString(L"会議室(大) (ビジネス空間)");
	m_env.AddString(L"会議室(小) (より密閉的)");
	m_env.AddString(L"防音室 (極端なデッド空間)");
	m_env.AddString(L"エントランスホール (高天井開放的)");
	m_env.AddString(L"書斎 (本による吸音)");
	m_env.AddString(L"キッチン (硬質多反射)");
	m_env.AddString(L"屋外駐車場 (開放的反射少)");
	m_env.AddString(L"地下道(狭) (圧迫的狭小空間)");
	m_env.AddString(L"展示室 (美術館より吸音的)");
	m_env.AddString(L"アトリエ (創作空間の独特さ)");
	m_env.SetCurSel(savedata.eqsoundenv);

	m_pre.AddString(L"デフォルト");
	m_pre.AddString(L"低音ブースト");
	m_pre.AddString(L"高音ブースト");
	m_pre.AddString(L"ボーカル強調");
	m_pre.AddString(L"低音カット");
	m_pre.AddString(L"高音カット");
	m_pre.AddString(L"ラウドネス");
	m_pre.AddString(L"クラシック");
	m_pre.AddString(L"ロック");
	m_pre.AddString(L"カスタム");
	m_pre.AddString(L"ジャズ");
	m_pre.AddString(L"ポップ");
	m_pre.AddString(L"EDM");
	m_pre.AddString(L"メタル");
	m_pre.AddString(L"ヒップホップ");
	m_pre.AddString(L"アコースティック");
	m_pre.AddString(L"V字型(ドンシャリ)");
	m_pre.AddString(L"逆V字型");
	m_pre.AddString(L"スマイルカーブ");
	m_pre.AddString(L"ラジオ/Podcast");
	m_pre.AddString(L"映画/ドラマ");
	m_pre.AddString(L"ゲーミング");
	m_pre.AddString(L"ライブ録音");
	m_pre.AddString(L"トレブルブースト");
	m_pre.AddString(L"ベースブースト");
	m_pre.AddString(L"小音量用");
	m_pre.AddString(L"ヘッドホン用");
	m_pre.AddString(L"ボーカル除去");
	m_pre.AddString(L"重低音強化");
	m_pre.AddString(L"ラジオAM");
	m_pre.AddString(L"ラジオFM");
	m_pre.AddString(L"テレビ音声");
	m_pre.AddString(L"電話音声");
	m_pre.AddString(L"ビンテージ");
	m_pre.AddString(L"モダン");
	m_pre.AddString(L"ウォーム");
	m_pre.AddString(L"ブライト");
	m_pre.AddString(L"フラット+");
	m_pre.AddString(L"スーパーベース");
	m_pre.AddString(L"クリスタル");
	m_pre.AddString(L"パーフェクト");
	m_pre.AddString(L"ダンス/クラブ");
	m_pre.AddString(L"R&B/ソウル");
	m_pre.AddString(L"レゲエ");
	m_pre.AddString(L"ブルース");
	m_pre.AddString(L"カントリー");
	m_pre.AddString(L"ファンク");
	m_pre.AddString(L"エレクトロニカ");
	m_pre.AddString(L"アンビエント");
	m_pre.AddString(L"インストゥルメンタル");
	m_pre.AddString(L"ナレーション/オーディオブック");
	m_pre.SetCurSel(savedata.eqsoundeq);

	if(savedata.eqx != -1)
		SetWindowPos(&CWnd::wndTop, savedata.eqx, savedata.eqy, 0, 0, SWP_NOSIZE| SWP_NOZORDER| SWP_NOOWNERZORDER);

	SetTimer(1, 100, NULL);
	return TRUE;
}
void CEqualizer::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	int a = m_env.GetCurSel();
	int c = 0;
	for (int b = 0; b < (int)ceill(a / 10.0f); b++) c++;
	a -= c;
	savedata.eqsoundenv = a;
}

void equaliser(void* data, int len, BOOL reset);

void CEqualizer::OnCbnSelchangeCombo5()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	KillTimer(1);
	savedata.eqsoundeq = m_pre.GetCurSel();
	extern int eqflg;
	if (eqflg == TRUE) {
		equaliser(0, 0, 2);
	}

	SetTimer(1, 100, NULL);
}


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
