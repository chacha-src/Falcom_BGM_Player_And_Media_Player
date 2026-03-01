// itiran_YSO.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "itiran_YSO.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Citiran_YSO ダイアログ

extern CString fnn;

Citiran_YSO::Citiran_YSO(CWnd* pParent /*=NULL*/)
	: CCustomDialog(Citiran_YSO::IDD, pParent)
{
	//{{AFX_DATA_INIT(Citiran_YSO)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void Citiran_YSO::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(Citiran_YSO)
	DDX_Control(pDX, IDC_LIST1, m_list);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(Citiran_YSO, CCustomDialog)
	//{{AFX_MSG_MAP(Citiran_YSO)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	//}}AFX_MSG_MAP
	cmn(Citiran_YSO);


char tiyso[][128]={
"001 タイトル<THE GUIDANCE OF A WHITE TOWER>",
"002 イベント0<BONDS WITH COMPANION>",
"003 ダームの塔24-25F<BEYOND THE BEGINNING>",
"004 ダームの塔01-05F<TOWER OF THE SHADOW OF DEATH>",
"005 ダームの塔06-09F<WATER PRISON>",
"006 ダームの塔10-13F<SCARLET TEMPEST>",
"007 ダームの塔14-17F<SILENT DESERT>",
"008 ダームの塔18-21F<SAMSARA AND PARAMNESIA>",
"009 ダームの塔22-24F<PRELUDE TO THE OMEN>",
"010 ダームの塔最上階<THE LAST MOMENT OF THE DARK>(トールのみ)",
"011 ボス戦1<TENSION>(ジェノクレス)",
"012 ラドの塔/鏡の間<DREAMING>",
"013 魔の通路 <DEVIL'S WIND>",
"014 ボス戦2<SCARS OF THE DIVINE WING>(ベラガンダー/ニグティルガー/ゲラルディ/ピクティモス/コンスクラード)",
"015 ボス戦3<OBORO>(ヴァジュリオン/その他<魔>の一族)",
"016 ボス戦4<MY LORD, OUR BRAVE>(ザバ/ヨグレクス＆オムルガン)",
"017 ダレス戦<OVER DRIVE>",
"018 魔王ダーム戦<TERMINATION>(トールのみ)",
"019 イントロ(動画用)<PROLOGUE -Ys ORIGIN->",
"020 ロゴ(動画用)",
"021 ロダの樹<RODA>",
"022 <魔>の一族のテーマ<THE ROOT OF DARKNESS>",
"023 イベント1<CONFRONTATION>",
"024 イベント2<THE PAIN OF SEPARATION>",
"025 イベント3<MEMORY OF SALMON>",
"026 イベント4<FEENA>",
"027 銀のハーモニカ1",
"028 銀のハーモニカ2",
"029 銀のハーモニカ2'ルーVer",
"030 イベント5<FAIR SKIES>",
"031 イベント6<DETERMINATION>",
"032 オープニング(動画用)<GENESIS BEYOND THE BEGINNING OPENING VERSION>",
"033 エンディング1(動画用)<TO THE NEXT GENERATION>ユニカ・ユーゴ",
"034 エンディング2(動画用)<A NEW LEGENDARY OPENING>トール",
"035 イベント7<BELIEVING>",
"036 ゲームオーバー<SO MUCH FOR TODAY>",
"037 インストラクション1<MAGNIFICENCE>",
"038 インストラクション2<MOVEMENT OF WICKED ENERGY>",
"039 インストラクション3<DISAPPEARANCE OF THREAT>",
"040 ダームの塔最上階イベント<-Bonus track- GENESIS BEYOND THE BEGINNING>(トールのみ)",
"★FALCOMロゴ動画",
"★プロローグ動画",
"★オープニング動画",
"★インストラクション1",
"★インストラクション2",
"★インストラクション3",
"★エンディング動画1",
"★エンディング動画2"
};

char tiyso_en[][128]={
"001 Title<THE GUIDANCE OF A WHITE TOWER>",
"002 Event 0<BONDS WITH COMPANION>",
"003 Darm Tower 24-25F<BEYOND THE BEGINNING>",
"004 Darm Tower 01-05F<TOWER OF THE SHADOW OF DEATH>",
"005 Darm Tower 06-09F<WATER PRISON>",
"006 Darm Tower 10-13F<SCARLET TEMPEST>",
"007 Darm Tower 14-17F<SILENT DESERT>",
"008 Darm Tower 18-21F<SAMSARA AND PARAMNESIA>",
"009 Darm Tower 22-24F<PRELUDE TO THE OMEN>",
"010 Darm Tower top floor<THE LAST MOMENT OF THE DARK>(Toal only)",
"011 Boss battle 1<TENSION>(Genocrates)",
"012 Rado Tower/Mirror Room<DREAMING>",
"013 Path of demons<DEVIL'S WIND>",
"014 Boss battle 2<SCARS OF THE DIVINE WING>(Belgarnder/Nigtilgar/Gerard/Pictimos/Consklard)",
"015 Boss battle 3<OBORO>(Vajurion/other demon clan)",
"016 Boss battle 4<MY LORD, OUR BRAVE>(Zava/Yogrex&Omurg)",
"017 Dalles battle<OVER DRIVE>",
"018 Demon King Darm battle<TERMINATION>(Toal only)",
"019 Intro(video)<PROLOGUE -Ys ORIGIN->",
"020 Logo(video)",
"021 Tree of Roda<RODA>",
"022 Demon clan theme<THE ROOT OF DARKNESS>",
"023 Event 1<CONFRONTATION>",
"024 Event 2<THE PAIN OF SEPARATION>",
"025 Event 3<MEMORY OF SALMON>",
"026 Event 4<FEENA>",
"027 Silver Harmonica 1",
"028 Silver Harmonica 2",
"029 Silver Harmonica 2 Lute Ver",
"030 Event 5<FAIR SKIES>",
"031 Event 6<DETERMINATION>",
"032 Opening(video)<GENESIS BEYOND THE BEGINNING OPENING VERSION>",
"033 Ending 1(video)<TO THE NEXT GENERATION> Yunika/Hugo",
"034 Ending 2(video)<A NEW LEGENDARY OPENING> Toal",
"035 Event 7<BELIEVING>",
"036 Game over<SO MUCH FOR TODAY>",
"037 Instruction 1<MAGNIFICENCE>",
"038 Instruction 2<MOVEMENT OF WICKED ENERGY>",
"039 Instruction 3<DISAPPEARANCE OF THREAT>",
"040 Darm Tower top floor event<-Bonus track- GENESIS BEYOND THE BEGINNING>(Toal only)",
"★FALCOM logo video",
"★Prologue video",
"★Opening video",
"★Instruction 1",
"★Instruction 2",
"★Instruction 3",
"★Ending video 1",
"★Ending video 2"
};

CString Citiran_YSO::Gett(int a){
	CString s,ss;
	s=savedata.lang ? tiyso_en[a] : tiyso[a];
	ss=s.Left(3);
	fnn=s.Mid(4);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// Citiran_YSO メッセージ ハンドラ
void Citiran_YSO::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=savedata.lang ? tiyso_en[idx] : tiyso[idx];
	ret=_tstoi(s.Left(3));
	ret2=m_list.GetCurSel();
	if(ret2>39)
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

BOOL Citiran_YSO::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL2(L"イース・オリジン", L"Ys Origin"));
	SetDlgItemText(IDOK, LL2(L"閉じる", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<48;i++)
	{
		CString s;
		s=savedata.lang ? tiyso_en[i] : tiyso[i];
		if(s.Left(2)!=_T("★")){ CString t=_T("YSO_"); t+=s; s=t; }
		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}

	m_list.SetCurSel(0);
	if(ret!=0) 
//		if(ret>39) m_list.SetCurSel(ret);
//		else m_list.SetCurSel(ret-1);
		m_list.SetCurSel(ret);
	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
