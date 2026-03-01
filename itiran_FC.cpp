// itiran_FC.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "itiran_FC.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Citiran_FC ダイアログ

extern CString fnn;

Citiran_FC::Citiran_FC(CWnd* pParent /*=NULL*/)
	: CCustomDialog(Citiran_FC::IDD, pParent)
{
	//{{AFX_DATA_INIT(Citiran_FC)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void Citiran_FC::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(Citiran_FC)
	DDX_Control(pDX, IDC_LIST1, m_list);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(Citiran_FC, CCustomDialog)
	//{{AFX_MSG_MAP(Citiran_FC)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	//}}AFX_MSG_MAP
	cmn(Citiran_FC);

/////////////////////////////////////////////////////////////////////////////
// Citiran_FC メッセージ ハンドラ
char ti1[][47]={
"001 風を共に舞う気持ち",
"100 地方都市ロレント",
"101 商業都市ボース",
"102 海港都市ルーアン",
"103 工房都市ツァイス",        
"104 王都グランセル",        
"105 陽だまりにて和む猫",        
"106 国境警備も楽じゃない",        
"107 王城",        
"108 グランアリーナ",        
"200 リベールの歩き方",        
"201 Secret Green Passage",        
"202 Rock on the Road",        
"300 闇を彷徨う",        
"301 行く手をはばむ鋼の床",        
"302 暗がりがくれた安らぎ",        
"303 四輪の塔",        
"304 レイストン要塞",        
"305 虚ろなる光の封土",        
"400 Sophisticated Fight",        
"402 To be Suggestive",        
"403 銀の意志",        
"404 Challenger Invited",        
"405 Ancient Makes",        
"406 至宝を守護せしモノ",        
"407 撃破！！",        
"408 消え行く星",        
"410 ピンチ！！",        
"500 星の在り処 Harmonica short Ver.",        
"501 琥珀の愛 Hum Ver.",        
"502 琥珀の愛 Piano Ver.",        
"503 琥珀の愛 Lute Ver.",        
"504 星の在り処 Harmonica long Ver.",        
"505 賑やかに行こう",        
"510 去り行く決意",        
"511 暗躍する者たち",        
"512 奴を逃がすな！",        
"513 胸の中に",        
"514 月明りの下で",        
"516 忍び寄る危機",        
"517 俺達カプア一家！",        
"518 旅立ちの小径",        
"519 奪還",        
"520 呪縛からの解放、そして・・・",        
"521 告白",        
"522 黒のオーブメント",        
"523 リベールの誇り",        
"530 (劇)姫の悩み",        
"531 (劇)騎士達の嘆き",        
"532 (劇)それぞれの思惑",        
"533 (劇)城",        
"534 (劇)コロシアム",        
"535 (劇)決闘",        
"536 (劇)姫の死",        
"537 (劇)大団円",
"★FALCOMロゴ動画",
"★オープニング動画",
"★エンディング動画",
"★次作予告動画"
};

char ti1_en[][47]={
"001 Feelings Dancing with the Wind",
"100 Rolent - Provincial City",
"101 Bose - Commercial City",
"102 Ruan - Harbor City",
"103 Zeiss - Engineering City",
"104 Grancel - Royal Capital",
"105 Cat Relaxing in the Sun",
"106 Border Patrol Isn't Easy",
"107 Royal Castle",
"108 Grand Arena",
"200 How to Walk Around Liberl",
"201 Secret Green Passage",
"202 Rock on the Road",
"300 Wandering in the Darkness",
"301 Steel Floor Blocking the Path",
"302 Peace from the Darkness",
"303 Tetracyclic Towers",
"304 Leiston Fortress",
"305 Vacant Land of Light",
"400 Sophisticated Fight",
"402 To be Suggestive",
"403 Silver Will",
"404 Challenger Invited",
"405 Ancient Makes",
"406 Guardian of the Treasure",
"407 Crush!!",
"408 Fading Star",
"410 Pinch!!",
"500 Where the Stars Are Harmonica short Ver.",
"501 Amber Love Hum Ver.",
"502 Amber Love Piano Ver.",
"503 Amber Love Lute Ver.",
"504 Where the Stars Are Harmonica long Ver.",
"505 Let's Go Cheerfully",
"510 Determination to Leave",
"511 Those Who Move in the Shadows",
"512 Don't Let Him Escape!",
"513 In My Heart",
"514 Under the Moonlight",
"516 Creeping Crisis",
"517 We're the Capua Family!",
"518 Path of Departure",
"519 Recapture",
"520 Liberation from the Curse, and...",
"521 Confession",
"522 Black Orbment",
"523 Pride of Liberl",
"530 (Drama) Princess's Worry",
"531 (Drama) Knights' Lament",
"532 (Drama) Each One's Intentions",
"533 (Drama) Castle",
"534 (Drama) Colosseum",
"535 (Drama) Duel",
"536 (Drama) Princess's Death",
"537 (Drama) Grand Finale",
"★FALCOM logo video",
"★Opening video",
"★Ending video",
"★Next game preview"
};
/**/

CString Citiran_FC::Gett(int a){
	CString s;
	s=savedata.lang ? ti1_en[a] : ti1[a];
	fnn=s.Mid(4);
	return s;
}

void Citiran_FC::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? ti1_en[idx] : ti1[idx];
	ret=_tstoi(s.Left(3));
	ret2=m_list.GetCurSel();
	if(ret2>54)
		ret=ret2;
#if UNICODE
	if(s.Left(1)=="★"){
		fnn=s.Mid(1);
#else
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
#endif
	}else{
		fnn=s.Mid(4);
	}
	EndDialog(1567);
}

BOOL Citiran_FC::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();

	SetWindowText(LL2(L"空の軌跡 First Chapter", L"Sora no Kiseki First Chapter"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<55;i++)
	{
		CString s;
		s="ED6";
		s+=savedata.lang ? ti1_en[i] : ti1[i];
//		s+="";
/*		switch(i)
		{
		case 0:
			s+="▼(タイトル)";
			break;
		case 1:
			s+="▼(町↓)";
			break;
		case 10:
			s+="▼(フィールド↓)";
			break;
		case 13:
			s+="▼(ダンジョン↓)";
			break;
		case 19:
			s+="▼(戦闘↓)";
			break;
		case 28:
			s+="▼(イベント↓)";
			break;
		}
*/		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}
		dx= m_list.AddString(LL2(L"★FALCOMロゴ動画", L"★FALCOM logo video"));
		m_list.SetItemData(dx,55);	
		dx= m_list.AddString(LL2(L"★オープニング動画", L"★Opening video"));
		m_list.SetItemData(dx,56);	
		dx= m_list.AddString(LL2(L"★エンディング動画", L"★Ending video"));
		m_list.SetItemData(dx,57);	
		dx= m_list.AddString(LL2(L"★次作予告動画", L"★Next game preview"));
		m_list.SetItemData(dx,58);	

		m_list.SetCurSel(0);
	if(ret2!=0) m_list.SetCurSel(ret2);

	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
