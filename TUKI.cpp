// TUKI.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "TUKI.h"


// CTUKI ダイアログ

IMPLEMENT_DYNAMIC(CTUKI, CCustomDialog)

CTUKI::CTUKI(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CTUKI::IDD, pParent)
{

}

CTUKI::~CTUKI()
{
}

void CTUKI::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CTUKI, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CTUKI);


// CTUKI メッセージ ハンドラ
extern CString fnn;

char tituki[][128]={
"Mc000 愛的廃墟",
"Mc001 幽謐",
"Mc003 歡潮",
"Mc011 暢游",
"Mc002 詭影",
"Mc013 月泉",
"Mc014 山雨欲來",
"Mc004 絲路（鼓鈴）",
"Mc015 踏春",
"Mc005 快板小調／滑稽小調",
"Mc016 夢魘",
"Mc006 追思",
"Mc017 暮歸",
"Mc007 離情",
"Mc018 空山",
"Mc008 秋夜",
"Mc019 塵思",
"Mc020 密殺",
"Mc021 埋伏",
"Mc022 決戰",
"Mc012 嬉戲",
"Mc023 忘憂",
"Mc024 天國梵音",
"Mc025 激殺",
"Mc026 怒殺",
"Mc027 血戰",
"Mc028 刃敵",
"Mc075 (不明)",
"★ファルコムロゴ",
"★オープニング",
"★スタッフロール",
"★エンディング１",
"★エンディング２",
"★エンディング３",
"★動画１",
"★動画２",
"★動画３",
"★動画４",
"★動画５"
};

char tituki_en[][128]={
"Mc000 Ruins of Love",
"Mc001 Serenity",
"Mc003 Joyful Tide",
"Mc011 Free Roaming",
"Mc002 Phantom Shadow",
"Mc013 Moon Spring",
"Mc014 Storm Approaching",
"Mc004 Silk Road(Drum Bell)",
"Mc015 Spring Walk",
"Mc005 Allegro/Comic Tune",
"Mc016 Nightmare",
"Mc006 Remembrance",
"Mc017 Evening Return",
"Mc007 Parting",
"Mc018 Empty Mountain",
"Mc008 Autumn Night",
"Mc019 Dust of Thought",
"Mc020 Secret Assassin",
"Mc021 Ambush",
"Mc022 Decisive Battle",
"Mc012 Play",
"Mc023 Forget Sorrow",
"Mc024 Heavenly Chant",
"Mc025 Fierce Kill",
"Mc026 Rage Kill",
"Mc027 Blood Battle",
"Mc028 Blade Enemy",
"Mc075 (Unknown)",
"★FALCOM logo",
"★Opening",
"★Staff roll",
"★Ending 1",
"★Ending 2",
"★Ending 3",
"★Video 1",
"★Video 2",
"★Video 3",
"★Video 4",
"★Video 5"
};

CString CTUKI::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tituki_en[a] : tituki[a];
	ss=s.Left(5);ss.TrimRight();
	fnn=s.Mid(6);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CTUKI::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tituki_en[idx] : tituki[idx];
	ret=s.Left(5); ret.TrimRight();
	ret2=m_list.GetCurSel();
#if UNICODE
	if(s.Left(1)=="★"){
		fnn=s.Mid(1);
#else
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
#endif
	}else{
		fnn=s.Mid(6);
	}
	EndDialog(1567);
}

BOOL CTUKI::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"月影のデスティニー", L"TSUKI no Destiny"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(39);i++)
	{
		CString s;
		s=savedata.lang ? tituki_en[i] : tituki[i];
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
