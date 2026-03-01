// San1.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "San1.h"


// CSan1 ダイアログ

IMPLEMENT_DYNAMIC(CSan1, CCustomDialog)

CSan1::CSan1(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CSan1::IDD, pParent)
{

}

CSan1::~CSan1()
{
}

void CSan1::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CSan1, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CSan1);


// CSan1 メッセージ ハンドラ
extern CString fnn;

char tisan1[][128]={
"42 永遠的旋律",
"01 古松居",
"27 緊迫",
"12 風動",
"48 破軍",
"49 戰闘49",
"39 悲訣",
"38 暗思量",
"47 幽谷潤",
"05 陌上仙郷",
"51 戰闘51",
"08 蜘蛛洞",
"03 臨江雨",
"02 市井榮華",
"35 笑笑",
"36 采飛揚",
"29 征途",
"50 戰闘50",
"07 功名路",
"22 鬼界戰闘",
"45 激戰",
"13 背水一戰",
"53 祭旗",
"14 飲幽恨",
"15 秋瑟涙",
"46 天涯魂夢",
"11 雲中境",
"19 戰場",
"10 江湖行",
"04 朱樓春宴",
"31 衝陣",
"30 戰火",
"21 歩歩為營",
"41 衷情",
"37 回首前塵",
"54 玄機",
"09 幻霧星霜",
"20 水寒淵",
"06 烽火紅顔",
"16 月蒼茫",
"32 登天",
"23 華夜曲",
"34 決戰千里",
"★盡頭",
"40 遙相憶",
"★オープニング",
"★動画",
"★ファルコムロゴ"
};

char tisan1_en[][128]={
"42 Eternal Melody",
"01 Ancient Pine Residence",
"27 Tension",
"12 Wind Blown",
"48 Broken Army",
"49 Battle 49",
"39 Sorrowful Parting",
"38 Dark Thoughts",
"47 Valley Mist",
"05 Rural Paradise",
"51 Battle 51",
"08 Spider Cave",
"03 Riverside Rain",
"02 City Prosperity",
"35 Smile",
"36 High Spirits",
"29 Journey",
"50 Battle 50",
"07 Path of Glory",
"22 Ghost Realm Battle",
"45 Fierce Battle",
"13 Last Stand",
"53 Sacrificial Banner",
"14 Drink of Resentment",
"15 Autumn Tears",
"46 Dreams of the Ends of Earth",
"11 Realm in the Clouds",
"19 Battlefield",
"10 Roaming the Rivers",
"04 Spring Feast at Red Pavilion",
"31 Charge the Formation",
"30 Flames of War",
"21 Every Step a Fortress",
"41 True Feelings",
"37 Looking Back",
"54 Mystery",
"09 Mist and Frost",
"20 Cold Abyss",
"06 Beauty in the Flames",
"16 Pale Moon",
"32 Ascend to Heaven",
"23 Night Song",
"34 Decisive Battle",
"★End",
"40 Distant Memories",
"★Opening",
"★Video",
"★FALCOM Logo"
};


CString CSan1::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tisan1_en[a] : tisan1[a];
	ss=s.Left(2);ss.TrimRight();
	fnn=s.Mid(3);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CSan1::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tisan1_en[idx] : tisan1[idx];
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

BOOL CSan1::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"幻想三国志１", L"Fantasy Sanguo 1"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	SetDlgItemText(IDC_STATIC, LL2(L"動画(★印)を再生するにはBinkの環境が必要です\nreadme.txtを読んで導入してください。", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup."));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(41+3+4);i++)
	{
		CString s;
		s=savedata.lang ? tisan1_en[i] : tisan1[i];
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
