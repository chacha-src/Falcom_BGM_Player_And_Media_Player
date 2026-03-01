// Nishi.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Nishi.h"


// CNishi ダイアログ

IMPLEMENT_DYNAMIC(CNishi, CCustomDialog)

CNishi::CNishi(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CNishi::IDD, pParent)
{

}

CNishi::~CNishi()
{
}

void CNishi::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CNishi, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CNishi);


// CNishi メッセージ ハンドラ
extern CString fnn;

char tinishi[][128]={
"z001 Brieging(ゼフィールファルコン作戦会議)",
"z002 COOL FIELD(フィールド山岳地帯)",
"z003 ディアブロ(ディアブロ第二段階)",
"z004 ゲイシルシティ(街)",
"z005 破壊神の鼓動(ディアブロ第一段階)",
"z006 インフェルノ(インフェルノ監獄(開放作戦))",
"z007 イオリーン(アンタリア創世記ヒロインのテーマ)",
"z008 Waltz de…(回想～バーンスタイン邸)",
"z009 街 Part1(村のテーマ)",
"z010 お別れイベントシーン(メルセデス)",
"z011 街 Part2(町のテーマ",
"z012 犠牲(悲しいイベント)",
"z013 …ドレイク？(海賊の襲撃)",
"z014 戦闘(通常戦闘)",
"z015 Zephyr Falcon(解放軍ゼフィールファルコン)",
"z016 The wind of Memory(シラノとメルセデスのピアノ曲)",
"z017 砂漠(砂漠)",
"z018 Aquamarine(最終ダンジョン～海底遺跡)",
"z019 魔剣阿修羅(阿修羅との戦い)",
"z020 Decisive Battle(ボスバトル)",
"z021 陰謀(イベントシーン(悪役専用))",
"z022 Force Field(解放戦争フィールド)",
"z023 FUZZY(スタッフロール)",
"z024 It's time to BATTLE!(ボスバトル2)",
"z025 PEACE(村)",
"z026 Precious Memories(シラノの想い出)",
"z027 Sorrowful…(イベント(大体が、誰かが死んだあと))",
"z028 Subway Crisis(ダンジョン)",
"z029 軍都市(緊迫した町のテーマ)",
"z030 目覚め(最終バトル直前)",
"z031 b-e natural(ダンジョン)",
"z032 チェザレ -part1-(チェザレのテーマ(オルガン))",
"z033 チェザレ -part2-(ラストバトル)",
"z034 Fireroad(契約)",
"z035 Forth step towards plain(フィールド・森など)",
"z036 THE GREAT REPEAT(ゲームタイトル画面)",
"z037 闇(インフェルノ地下)",
"z038 Jungle 2 Jungle(バース島)",
"z039 Not natural but natural(無限ループダンジョン)",
"z041 The wind of Memory（オーケストラ・ミックス）(エンディングテーマ)",
"z042 迷い(公爵屋敷)",
"z050 The wind of Memory（フレーズ）(ピアノ弾き)"
};

char tinishi_en[][128]={
"z001 Briefing(Zephyr Falcon War Council)",
"z002 COOL FIELD(Field mountain area)",
"z003 Diablo(Diablo second phase)",
"z004 Gayl City(Town)",
"z005 Pulse of the Destroyer(Diablo first phase)",
"z006 Inferno(Inferno Prison(liberation))",
"z007 Ioleen(Antaria Genesis heroine theme)",
"z008 Waltz de...(Recollection~Bernstein residence)",
"z009 Town Part1(Village theme)",
"z010 Farewell event scene(Mercedes)",
"z011 Town Part2(Town theme)",
"z012 Sacrifice(Sad event)",
"z013 ...Drake?(Pirate attack)",
"z014 Battle(Normal battle)",
"z015 Zephyr Falcon(Liberation Army Zephyr Falcon)",
"z016 The wind of Memory(Silvano and Mercedes piano piece)",
"z017 Desert(Desert)",
"z018 Aquamarine(Final dungeon~underwater ruins)",
"z019 Demon Sword Asura(Battle with Asura)",
"z020 Decisive Battle(Boss battle)",
"z021 Conspiracy(Event scene(villain))",
"z022 Force Field(Liberation war field)",
"z023 FUZZY(Staff roll)",
"z024 It's time to BATTLE!(Boss battle 2)",
"z025 PEACE(Village)",
"z026 Precious Memories(Silvano's memory)",
"z027 Sorrowful...(Event(usually after someone dies))",
"z028 Subway Crisis(Dungeon)",
"z029 Military city(Urgent town theme)",
"z030 Awakening(Just before final battle)",
"z031 b-e natural(Dungeon)",
"z032 Cesare -part1-(Cesare theme(organ))",
"z033 Cesare -part2-(Last battle)",
"z034 Fireroad(Contract)",
"z035 Forth step towards plain(Field, forest, etc)",
"z036 THE GREAT REPEAT(Game title screen)",
"z037 Darkness(Inferno underground)",
"z038 Jungle 2 Jungle(Birth island)",
"z039 Not natural but natural(Infinite loop dungeon)",
"z041 The wind of Memory(Orchestra mix)(Ending theme)",
"z042 Hesitation(Duke residence)",
"z050 The wind of Memory(Phrase)(Piano)"
};


CString CNishi::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tinishi_en[a] : tinishi[a];
	ss=s.Left(4);ss.TrimRight();
	fnn=s.Mid(5);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CNishi::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int i=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tinishi_en[i] : tinishi[i];
	ret=s.Left(4); ret.TrimRight();
	ret2=m_list.GetCurSel();
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
	}else{
		fnn=s.Mid(5);
	}
	EndDialog(1567);
}

BOOL CNishi::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"西風の狂詩曲(ラプソディー)", L"Rhapsody of the West Wind"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(42);i++)
	{
		CString s;
		s=savedata.lang ? tinishi_en[i] : tinishi[i];
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
