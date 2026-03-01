// San2.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "San2.h"


// CSan2 ダイアログ

IMPLEMENT_DYNAMIC(CSan2, CCustomDialog)

CSan2::CSan2(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CSan2::IDD, pParent)
{

}

CSan2::~CSan2()
{
}

void CSan2::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CSan2, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CSan2);


// CSan2 メッセージ ハンドラ
extern CString fnn;

TCHAR tisan2[][128]={
_T("41 主題曲：界線(日本語Ver)"),
_T("41 主題曲：界線(中国Ver)"),
_T("45 懸圃：瓊樓玉宇"),
_T("44 大地圖：鴻鵠萬里"),
_T("16 白馬寒潭：碧水洞天"),
_T("12 無界縫細：流雲輕舟"),
_T("01 召徳村：暁風"),
_T("11 古皇宮藏道：寒冰夜"),
_T("19 飛凰宮：戰飛凰"),
_T("48 武將出場：萬夫莫敵"),
_T("36 一般戰鬥2：談笑論劍"),
_T("18 鬼界：黄泉夢地"),
_T("34 浪漫愛情曲：月下寄情"),
_T("08 沙暴廢坑"),
_T("09 潼關水路：空山新雨"),
_T("25 濮陽雷澤：暗夜驚雷"),
_T("22 蕭關：潛影"),
_T("20 射日坡：波瀾動"),
_T("21 壽春：一夫當關"),
_T("39 戰場頭目戰鬥：爭鋒"),
_T("30 緊張2(大)：危機四伏"),
_T("32 非慟：離愁恨"),
_T("13 浮月林：清月幽林"),
_T("33 哀愁：相思無盡"),
_T("47 百花樓"),

_T("15 臥雲榻：半日"),
_T("03 洛陽：秋霜"),
_T("23 下ヒ：英雄末路"),
_T("07 鎮江：美人含笑"),
#if _UNICODE
_T("27 輕鬆俏皮"),
#else
_T("27 輕鬆しょう皮"),
#endif
#if _UNICODE
_T("28 搞笑"),
#else
_T("28 こう笑"),
#endif
_T("29 小緊張"),
#if _UNICODE
_T("10 崆峴山：嵐舞"),
#else
_T("10 そうけん山：嵐舞"),
#endif
_T("14 洛陽西郊棧道：轟"),
_T("06 許都：征前曲"),
_T("02 長安：出巡"),
_T("38 戰場戰鬥：決勝"),
_T("24 官渡：路關險"),
_T("37 一般頭目戰鬥：對峙"),
_T("26 天若宮：天若仙韻"),
_T("04 小沛：春色"),
_T("17 幽妍島：霓裳"),
_T("46 皇宮：皇圖"),
_T("05 徐州：軍陣"),
_T("31 重大打撃：天倫夢斷"),
_T("40 最終頭目戰鬥：探雲霄"),
_T("42 插曲：情衷"),
_T("56 暗思量(幻想三国志1)"),
_T("60 笑笑(幻想三国志1)"),
_T("★ファルコムロゴ"),
_T("★宇峻奥汀ロゴ"),
_T("★オープニング"),
_T("35 (不明)")
};

TCHAR tisan2_en[][128]={
_T("41 Theme: Borderline (Japanese Ver)"),
_T("41 Theme: Borderline (Chinese Ver)"),
_T("45 Hanging Garden: Jade Palace"),
_T("44 World Map: Vast Horizon"),
_T("16 White Horse Cold Pool: Blue Waters"),
_T("12 Boundless: Light Clouds"),
_T("01 Zhaode Village: Dawn Wind"),
_T("11 Ancient Palace Path: Cold Ice Night"),
_T("19 Feihuang Palace: Flying Phoenix"),
_T("48 General Appearance: Invincible"),
_T("36 Normal Battle 2: Sword Talk"),
_T("18 Ghost Realm: Dream of the Underworld"),
_T("34 Romance: Moonlit Feelings"),
_T("08 Sandstorm Wasteland"),
_T("09 Tong Pass Waterway: Mountain Rain"),
_T("25 Puyang Lei Marsh: Night Thunder"),
_T("22 Xiaoguan: Hidden Shadow"),
_T("20 Sheri Slope: Waves"),
_T("21 Shouchun: One Against All"),
_T("39 Field Boss Battle: Contest"),
_T("30 Tension 2 (Major): Danger Everywhere"),
_T("32 Sorrow: Parting Grief"),
_T("13 Floating Moon Forest: Quiet Moon Woods"),
_T("33 Melancholy: Endless Longing"),
_T("47 Hundred Flowers Tower"),
_T("15 Cloud Rest: Half Day"),
_T("03 Luoyang: Autumn Frost"),
_T("23 Xiahou: Hero's End"),
_T("07 Zhenjiang: Beauty's Smile"),
_T("27 Light and Playful"),
_T("28 Comedy"),
_T("29 Minor Tension"),
_T("10 Kongxian Mountain: Storm Dance"),
_T("14 Luoyang Western Road: Roar"),
_T("06 Xudu: Marching Song"),
_T("02 Chang'an: Royal Progress"),
_T("38 Field Battle: Decisive"),
_T("24 Guandu: Dangerous Pass"),
_T("37 Normal Boss Battle: Confrontation"),
_T("26 Tianruo Palace: Heavenly Melody"),
_T("04 Xiaopei: Spring Colors"),
_T("17 Youyan Island: Rainbow Dress"),
_T("46 Imperial Palace: Imperial Dream"),
_T("05 Xuzhou: Military Array"),
_T("31 Major Blow: Broken Dreams"),
_T("40 Final Boss Battle: Reach the Clouds"),
_T("42 Interlude: True Feelings"),
_T("56 Dark Thoughts (Fantasy Sanguo 1)"),
_T("60 Smile (Fantasy Sanguo 1)"),
_T("★FALCOM Logo"),
_T("★UserJoy Logo"),
_T("★Opening"),
_T("35 (Unknown)")
};

CString CSan2::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tisan2_en[a] : tisan2[a];
	ss=s.Left(2);ss.TrimRight();
	fnn=s.Mid(3);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CSan2::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tisan2_en[idx] : tisan2[idx];
	ret=s.Left(2); ret.TrimRight();
	ret2=m_list.GetCurSel();
#if UNICODE
	if(s.Left(1)=="★"){
		fnn=s.Mid(1);
#else
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
#endif
	}else{
		fnn=s.Mid(3);
	}
	EndDialog(1567);
}

BOOL CSan2::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"幻想三国志２", L"Fantasy Sanguo 2"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	SetDlgItemText(IDC_STATIC, LL2(L"動画(★印)を再生するにはBinkの環境が必要です\nreadme.txtを読んで導入してください。", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup."));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(50+3);i++)
	{
		CString s;
		s=savedata.lang ? tisan2_en[i] : tisan2[i];
		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}

	m_list.SetCurSel(0);
	if(ret2!=0) 
//		if(ret>65) m_list.SetCurSel(ret);
//		else m_list.SetCurSel(ret-1);
		m_list.SetCurSel(ret2);
	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
