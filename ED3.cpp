// ED3.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "ED3.h"


// CED3 ダイアログ

IMPLEMENT_DYNAMIC(CED3, CCustomDialog)

CED3::CED3(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CED3::IDD, pParent)
{

}

CED3::~CED3()
{
}

void CED3::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CED3, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CED3);


// CED3 メッセージ ハンドラ
extern CString fnn;

char tied3[][128]={
"ED3940DA 白き魔女ゲルド",
"ED3940DA もうひとつの英雄たちの物語",
"ED3200DA 愛はきらめきのなかに",
"ED3201DA 小さな英雄 -オルゴール-",
"ED3104DA 俺様ライオン、おまえを喰らうぞ-Original-",
"ED3106DA WIN!! -Original-",
"ED3111DA シャーラ＆グース",
"ED3101DA MOUNTAIN PATH",
"ED3112DA FEEL SO GOOD",
"ED3110DA 関所",
"ED3103DA 洞窟",
"ED3119DA シャリネ",
"ED3102DA 浜辺の午後",
"ED3107DA しあわせな一日",
"ED3203DA 気分はキャプテン・トーマス",
"ED3120DA 犯人は誰だ?!",
"ED3113DA 愉快な仲間たち",
"ED3108DA 風といっしょに",
"ED3121DA 小さな英雄 -ジュリオとクリスの大冒険-",
"ED3100DA LET'S START, OK?",
"ED3302DA 炭坑",
"ED3300DA ガルガのつめ痕",
"ED3116DA ローディ",
"ED3403DA GAMBLER",
"ED3400DA 毒沼",
"ED3117DA 白き魔女ゲルド -軌跡-",
"ED3402DA ダーツ",
"ED3115DA 哀しみのメロディ",
"ED3401DA それ見よ我が元気",
"ED3405DA 嵐の前",
"ED3404DA ボルト大決戦",
"ED3105DA 強敵!!",
"ED3118DA あ、負けちゃった",
"ED3000DA 不思議なお話を",
"ED3501DA 俺様ライオン、おまえを喰らうぞ-Arrange-",
"ED3502DA WIN!! -Arrange-",
"ED3109DA 円舞曲",
"ED3700DA 男ひとり旅",
"ED3506DA フラダンシング・オールナイト",
"ED3500DA 迷いの森 -Original-",
"ED3601DA 迷いの森 -Arrange-",
"ED3604DA オルドス",
"ED3600DA オルドス大聖堂",
"ED3602DA まろうどの賛歌",
"ED3603DA ラウアール",
"ED3800DA ギドナ",
"ED3801DA 赤い瞳の誘惑",
"ED3803DA バダット",
"ED3802DA ガガーブの記憶",
"ED3114DA 夢",
"ED3900DA INVASION",
"ED3504DA ゲルドへの路",
"ED3902DA 回想 -レクイエム-",
"ED3920DA ルード城",
"ED3923DA 魔獣出現",
"ED3924DA 天儀室",
"ED3503DA 王妃イザベル 戦闘前",
"ED3921DA 王妃イザベル 戦闘",
"ED3925DA 白き魔女ゲルド -尊き魂-",
"ED3926DA 小さな英雄 -青空のむこうに-",
"ED3927DA 終焉",
"ED3928DA デュルゼルの手紙",
"ED3505DA ラグピック村エンディングVer.",
"ED3507DA Leone Fredrik Richter",
"ED3930DA HEROES2",
"ED3931DA 小さな英雄 -冬の到来-",
"ED3932DA ティラスイールの白き魔女"
};

char tied3_en[][128]={
"ED3940DA White Witch Gerd",
"ED3940DA Another Heroes' Tale",
"ED3200DA Love in the Radiance",
"ED3201DA Little Hero -Music Box-",
"ED3104DA I Am the Lion, I Shall Devour You-Original-",
"ED3106DA WIN!! -Original-",
"ED3111DA Shala & Goose",
"ED3101DA MOUNTAIN PATH",
"ED3112DA FEEL SO GOOD",
"ED3110DA Checkpoint",
"ED3103DA Cave",
"ED3119DA Shalenne",
"ED3102DA Afternoon at the Beach",
"ED3107DA Happy Day",
"ED3203DA Captain Thomas Mood",
"ED3120DA Who's the Culprit?!",
"ED3113DA Merry Companions",
"ED3108DA With the Wind",
"ED3121DA Little Hero -Julio and Chris' Great Adventure-",
"ED3100DA LET'S START, OK?",
"ED3302DA Coal Mine",
"ED3300DA Garga's Claw Mark",
"ED3116DA Rody",
"ED3403DA GAMBLER",
"ED3400DA Poison Swamp",
"ED3117DA White Witch Gerd -Traces-",
"ED3402DA Darts",
"ED3115DA Melody of Sorrow",
"ED3401DA Behold My Vigor",
"ED3405DA Before the Storm",
"ED3404DA Bolt's Great Battle",
"ED3105DA Strong Foe!!",
"ED3118DA Oh, I Lost",
"ED3000DA Strange Tale",
"ED3501DA I Am the Lion, I Shall Devour You-Arrange-",
"ED3502DA WIN!! -Arrange-",
"ED3109DA Waltz",
"ED3700DA Man's Lone Journey",
"ED3506DA Fladancing All Night",
"ED3500DA Forest of Illusion -Original-",
"ED3601DA Forest of Illusion -Arrange-",
"ED3604DA Ordos",
"ED3600DA Ordos Cathedral",
"ED3602DA Hymn of the Pilgrim",
"ED3603DA Rauar",
"ED3800DA Gidona",
"ED3801DA Temptation of Red Eyes",
"ED3803DA Badat",
"ED3802DA Gagharv's Memory",
"ED3114DA Dream",
"ED3900DA INVASION",
"ED3504DA Path to Gerd",
"ED3902DA Recollection -Requiem-",
"ED3920DA Rood Castle",
"ED3923DA Monster Appearance",
"ED3924DA Heavenly Room",
"ED3503DA Queen Isabel Before Battle",
"ED3921DA Queen Isabel Battle",
"ED3925DA White Witch Gerd -Precious Soul-",
"ED3926DA Little Hero -Beyond the Blue Sky-",
"ED3927DA End",
"ED3928DA Durzel's Letter",
"ED3505DA Lagpick Village Ending Ver.",
"ED3507DA Leone Fredrik Richter",
"ED3930DA HEROES2",
"ED3931DA Little Hero -Winter's Arrival-",
"ED3932DA White Witch of Tilaxia"
};

CString CED3::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tied3_en[a] : tied3[a];
	ss=s.Left(8);ss.TrimRight();
	fnn=s.Mid(9);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CED3::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tied3_en[idx] : tied3[idx];
	ret=s.Left(8); ret.TrimRight();
	ret2=m_list.GetCurSel();
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
	}else{
		fnn=s.Mid(9);
	}
	EndDialog(1567);
}

BOOL CED3::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"英雄伝説III 新・白き魔女", L"Legend of Heroes III White Witch"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(67);i++)
	{
		CString s;
		s=savedata.lang ? tied3_en[i] : tied3[i];
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
