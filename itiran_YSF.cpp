// itiran_YSF.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "itiran_YSF.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Citiran_YSF ダイアログ

extern CString fnn;

Citiran_YSF::Citiran_YSF(CWnd* pParent /*=NULL*/)
	: CCustomDialog(Citiran_YSF::IDD, pParent)
{
	//{{AFX_DATA_INIT(Citiran_YSF)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void Citiran_YSF::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(Citiran_YSF)
	DDX_Control(pDX, IDC_LIST1, m_list);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(Citiran_YSF, CCustomDialog)
	//{{AFX_MSG_MAP(Citiran_YSF)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	//}}AFX_MSG_MAP
	cmn(Citiran_YSF);

/////////////////////////////////////////////////////////////////////////////
// Citiran_YSF メッセージ ハンドラ

char tiys3[][128]={
"01.Dancing on the road(不明)",
"02.予感 ＝スティクス＝(メニュー)",
"03.貿易の街レドモント(レドモントの町)",
"04.静かな刻(長老などの家)",
"05.Welcome!!(お店)",
"06.冒険への序曲(マップ)",
"07.翼を持った少年(フィールドマップ)",
"08.Be careful(ティグレーの採石場)",
"09.漆黒の魔獣(ボス戦)",
"10.イルバーンズの遺跡(イルバーンズの遺跡)",
"11.灼熱の死闘(溶岩地帯)",
"12.暗黒の罠(ティグレーの採石場奥)",
"13.死神の電撃(ボス戦)",
"14.いっときの夢(ゲームオーバー)",
"15.厳格なる闘志(エルダーム山脈)",
"16.哀愁のトワイライト(レドモントの町 危機後)",
"17.バレスタイン城(バレスタイン城内)",
"18.慈愛の祈り(バレスタイン城聖堂)",
"19.光りの鍵(エンディング後)",
"20.時の封印(バレスタイン城時計塔)",
"21.破滅への鼓動(ジェノス島)",
"22.運命の塔(ジェノス島深部)",
"23.これを見よ！！(ニコラス戦)",
"24.最強の敵(ガルバラン戦)",
"25.旅立ちの朝(クリア後のレドモントの町)",
"26.Wanderers from Ys(エンディング(動画))",
"27.Dear My Brother(エレナ、チェスターとのイベント)",
"28.愛しのエレナ(エレナのテーマ)",
"29.Introduction!!(レドモントの町でのイベント)",
"30.The Theme of Chester(チェスターのテーマ)",
"31.Chop!!(ボス戦)",
"32.Believe in my heart(クリア直前のレドモントの町)",
"33.予感 ＝スティクス＝(オープニング(動画))",
"34.愛しのエレナ(ガルバラン島崩壊(動画))"
};

char tiys3_en[][128]={
"01.Dancing on the road(unknown)",
"02.Omen =Styx=(Menu)",
"03.Trade town Redmont(Redmont town)",
"04.Quiet moment(Elder's house)",
"05.Welcome!!(Shop)",
"06.Prelude to adventure(Map)",
"07.The Boy with Wings(Field map)",
"08.Be careful(Tigray Quarry)",
"09.The Black Beast(Boss battle)",
"10.Ruins of Ilburnz(Ilburnz ruins)",
"11.Blazing death battle(Lava area)",
"12.Darkness trap(Deep Tigray Quarry)",
"13.Reaper's lightning(Boss battle)",
"14.Momentary dream(Game over)",
"15.Strict fighting spirit(Eldarm Mountains)",
"16.Melancholy Twilight(Redmont after crisis)",
"17.Barestayn Castle(Barestayn Castle)",
"18.Prayer of mercy(Barestayn Cathedral)",
"19.Key of light(After ending)",
"20.Seal of time(Barestayn Clock Tower)",
"21.Pulse of destruction(Genos Island)",
"22.Tower of fate(Deep Genos Island)",
"23.Behold!!(Nicholas battle)",
"24.Strongest enemy(Galbalan battle)",
"25.Morning of departure(Redmont after clear)",
"26.Wanderers from Ys(Ending(video))",
"27.Dear My Brother(Elena, Chester event)",
"28.Beloved Elena(Elena theme)",
"29.Introduction!!(Redmont event)",
"30.The Theme of Chester(Chester theme)",
"31.Chop!!(Boss battle)",
"32.Believe in my heart(Redmont just before clear)",
"33.Omen =Styx=(Opening(video))",
"34.Beloved Elena(Galbalan island collapse(video))"
};

void Citiran_YSF::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s,ss;	s=savedata.lang ? tiys3_en[idx] : tiys3[idx];
	ss=s.Left(2);ret=_tstoi(ss)-1;
	fnn=s.Mid(3);
	EndDialog(1567);
}

void Citiran_YSF::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tiys3_en[a] : tiys3[a];
	ss=s.Left(6);ss.TrimRight();
	fnn=s.Mid(3);
}

BOOL Citiran_YSF::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"イース -フェルガナの誓い-", L"Ys -Felghana no Chikai-"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(34);i++)
	{
		CString s;
		s=savedata.lang ? tiys3_en[i] : tiys3[i];
		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}

	m_list.SetCurSel(0);
	if(ret2!=0) 
		m_list.SetCurSel(ret2);
	m_list.SetFocus();
	m_list.SetCurSel(0);
	if(ret!=0) m_list.SetCurSel(ret);

	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
