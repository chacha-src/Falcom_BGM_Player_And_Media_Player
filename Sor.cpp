// Sor.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Sor.h"


// CSor ダイアログ

IMPLEMENT_DYNAMIC(CSor, CCustomDialog)

CSor::CSor(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CSor::IDD, pParent)
{

}

CSor::~CSor()
{
}

void CSor::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CSor, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CSor);


// CSor メッセージ ハンドラ
extern CString fnn;

char tisor[][128]={
"SSO001 オープニング",
"SSO002 城 (ここで逢えるね)",
"SSO003 町1",
"SSO004 町2",
"SSO008 トラベラーズ・イン",
"SSO059 シナリオクリア",
"SSO005 消えた王様の杖 ダンジョン",
"SSO006 消えた王様の杖 ヒドラ",
"SSO007 消えた王様の杖 生還",
"SSO009 失われたタリスマン 森",
"SSO010 失われたタリスマン 地下ダンジョン",
"SSO011 失われたタリスマン 神官テルヒチ",
"SSO012 失われたタリスマン サンド・マリボー",
"SSO013 ルシフェルの水門 地下ダンジョン",
"SSO014 ルシフェルの水門 クラーケン",
"SSO015 ルシフェルの水門 ブラディー・リバー",
"SSO016 呪われたオアシス 砂漠",
"SSO017 呪われたオアシス 砂の城",
"SSO018 呪われたオアシス ルワンとゴールド・ドラゴン",
"SSO019 盗賊達の塔 塔",
"SSO020 盗賊達の塔 地底",
"SSO021 盗賊達の塔 屋上",
"SSO022 盗賊達の塔 メジャー・デーモン",
"SSO023 盗賊達の塔 シャドー・ドラゴン",
"SSO024 盗賊達の塔 封印",
"SSO025 暗き沼の魔法使い 暗き沼",
"SSO026 暗き沼の魔法使い レッド・ドラゴン",
"SSO027 ロマンシア ロマンシア王国",
"SSO028 ロマンシア ロマンシア城",
"SSO029 ロマンシア アゾルバ王国",
"SSO030 ロマンシア ヴァイデス",
"SSO060 ロマンシア専用クリア",
"SSO031 紅玉の謎 森",
"SSO032 紅玉の謎 モス・ジャイアント",
"SSO033 紅玉の謎 平和な森",
"SSO034 暗黒の魔道士 ダンジョン",
"SSO035 暗黒の魔道士 ゲディス",
"SSO064 暗黒の魔道士 ゲディスII",
"SSO036 暗黒の魔道士 ブルー・ドラゴン",
"SSO037 呪われたクイーンマリー号 船内",
"SSO038 呪われたクイーンマリー号 上陸後",
"SSO039 呪われたクイーンマリー号 アーク・デーモン",
"SSO040 天の神々たち 村",
"SSO041 天の神々たち コンバット・シーン",
"SSO042 天の神々たち 天上界",
"SSO043 天の神々たち 竪琴",
"SSO044 天の神々たち エビル=シャーマン",
"SSO045 氷の洞窟 洞窟",
"SSO046 氷の洞窟 洞窟II",
"SSO047 氷の洞窟 エキム",
"SSO048 メデューサの首 森",
"SSO049 メデューサの首 村",
"SSO050 メデューサの首 メデューサ",
"SSO051 囚われた魔法使い 地下要塞",
"SSO052 囚われた魔法使い ファイヤー・エレメント",
"SSO053 不老長寿の水 生きている洞窟",
"SSO054 不老長寿の水 動く心臓",
"SSO055 不老長寿の水 ダブル=デビルス",
"SSO056 キング・ドラゴン",
"SSO057 エンディングI",
"SSO058 エンディングII",
"SSO066 新オープニング",
"SSO080 ある魔術師の失敗 鉱山の地底湖",
"SSO081 ある魔術師の失敗 アースエレメンタル",
"SSO083 真夜中に鐘は鳴る ランドル村",
"SSO084 真夜中に鐘は鳴る 教会",
"SSO085 真夜中に鐘は鳴る ザキュレイア",
"SSO086 真夜中に鐘は鳴る 魔術士ゲラン",
"SSO087 ドワーフの置き土産 ドワーフの迷宮",
"SSO088 ドワーフの置き土産 ラビリンス・ドラゴン",
"SSO089 妖精の大樹を救え 妖精たちの村",
"SSO090 妖精の大樹を救え 巨大樹木内部",
"SSO091 妖精の大樹を救え センティピード",
"SSO092 招かれざる来訪者 怪しげな島",
"SSO093 招かれざる来訪者 アーク・デーモン",
"SSO082 フォーエバー シナリオ１～５クリア"
};

char tisor_en[][128]={
"SSO001 Opening",
"SSO002 Castle (We'll Meet Here)",
"SSO003 Town 1",
"SSO004 Town 2",
"SSO008 Travelers' Inn",
"SSO059 Scenario Clear",
"SSO005 Lost King's Staff Dungeon",
"SSO006 Lost King's Staff Hydra",
"SSO007 Lost King's Staff Survival",
"SSO009 Lost Talisman Forest",
"SSO010 Lost Talisman Underground Dungeon",
"SSO011 Lost Talisman Priest Terhichi",
"SSO012 Lost Talisman Sand Marlborough",
"SSO013 Lucifer's Watergate Underground Dungeon",
"SSO014 Lucifer's Watergate Kraken",
"SSO015 Lucifer's Watergate Bloody River",
"SSO016 Cursed Oasis Desert",
"SSO017 Cursed Oasis Sand Castle",
"SSO018 Cursed Oasis Ruan and Gold Dragon",
"SSO019 Tower of Thieves Tower",
"SSO020 Tower of Thieves Underground",
"SSO021 Tower of Thieves Rooftop",
"SSO022 Tower of Thieves Major Demon",
"SSO023 Tower of Thieves Shadow Dragon",
"SSO024 Tower of Thieves Seal",
"SSO025 Wizard of Dark Marsh Dark Marsh",
"SSO026 Wizard of Dark Marsh Red Dragon",
"SSO027 Romancia Romancia Kingdom",
"SSO028 Romancia Romancia Castle",
"SSO029 Romancia Azolba Kingdom",
"SSO030 Romancia Vaides",
"SSO060 Romancia Exclusive Clear",
"SSO031 Mystery of the Ruby Forest",
"SSO032 Mystery of the Ruby Moss Giant",
"SSO033 Mystery of the Ruby Peaceful Forest",
"SSO034 Dark Mage Dungeon",
"SSO035 Dark Mage Gedis",
"SSO064 Dark Mage Gedis II",
"SSO036 Dark Mage Blue Dragon",
"SSO037 Cursed Queen Mary Ship Interior",
"SSO038 Cursed Queen Mary After Landing",
"SSO039 Cursed Queen Mary Arc Demon",
"SSO040 Gods of Heaven Village",
"SSO041 Gods of Heaven Combat Scene",
"SSO042 Gods of Heaven Heavenly Realm",
"SSO043 Gods of Heaven Harp",
"SSO044 Gods of Heaven Evil Shaman",
"SSO045 Ice Cave Cave",
"SSO046 Ice Cave Cave II",
"SSO047 Ice Cave Ekim",
"SSO048 Medusa's Head Forest",
"SSO049 Medusa's Head Village",
"SSO050 Medusa's Head Medusa",
"SSO051 Imprisoned Mage Underground Fortress",
"SSO052 Imprisoned Mage Fire Elemental",
"SSO053 Water of Eternal Youth Living Cave",
"SSO054 Water of Eternal Youth Beating Heart",
"SSO055 Water of Eternal Youth Double Devils",
"SSO056 King Dragon",
"SSO057 Ending I",
"SSO058 Ending II",
"SSO066 New Opening",
"SSO080 A Wizard's Failure Mine Underground Lake",
"SSO081 A Wizard's Failure Earth Elemental",
"SSO083 Midnight Bell Rings Randle Village",
"SSO084 Midnight Bell Rings Church",
"SSO085 Midnight Bell Rings Zaquria",
"SSO086 Midnight Bell Rings Mage Geran",
"SSO087 Dwarf's Legacy Dwarf Maze",
"SSO088 Dwarf's Legacy Labyrinth Dragon",
"SSO089 Save the Fairy Tree Fairy Village",
"SSO090 Save the Fairy Tree Giant Tree Interior",
"SSO091 Save the Fairy Tree Centipede",
"SSO092 Unwelcome Visitors Suspicious Island",
"SSO093 Unwelcome Visitors Arc Demon",
"SSO082 Forever Scenario 1~5 Clear"
};

CString CSor::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tisor_en[a] : tisor[a];
	ss=s.Left(6);ss.TrimRight();
	fnn=s.Mid(7);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CSor::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tisor_en[idx] : tisor[idx];
	ret=s.Left(6); ret.TrimRight();
	ret2=m_list.GetCurSel();
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
	}else{
		fnn=s.Mid(7);
	}
	EndDialog(1567);
}

BOOL CSor::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"ソーサリアン オリジナル", L"Sorcerian Original"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(76);i++)
	{
		CString s;
		s=savedata.lang ? tisor_en[i] : tisor[i];
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

