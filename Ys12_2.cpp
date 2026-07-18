// Ys12_2.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Ys12_2.h"


// CYs12_2 ダイアログ

IMPLEMENT_DYNAMIC(CYs12_2, CCustomBlurDialogBase)

CYs12_2::CYs12_2(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CYs12_2::IDD, pParent)
{

}

CYs12_2::~CYs12_2()
{
}

void CYs12_2::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CYs12_2, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CYs12_2);


// CYs12_2 メッセージ ハンドラ
extern CString fnn;

char tiy122[][128]={
"01ys248 TO MAKE THE END OF BATTLE",
"02ys236 LILIA",
"03ys205 TOO FULL WITH LOVE",
"04ys225 APATHETIC STORY",
"05ys226 MAY I HELP YOU",
"06ys228 FEEL BLUE",
"07ys201 RUINS OF MOONDORIA",
"08ys220 NOBLE DISTRICT OF TOAL",
"09ys249 REST IN PEACE",
"10ys241 CAVERN OF RASTEENIE",
"11ys244 PROTECTERS",
"12ys242 ICE RIDGE OF NOLTIA",
"13ys203 INSIDE OF THE ICE WALL",
"14ys243 MOAT OF BURNEBLESS",
"15ys232 TENDER PEOPLE",
"16ys240 PALACE OF SALMON",
"17ys233 SUBTERRANEAN CANAL",
"18ys245 COMPANILE OF LANE",
"19ys223 PRESSURE ROAD",
"20ys235 DON'T GO SO SMOOTHLY!",
"21ys234 FEENA",
"22ys246 TERMINATION",
"23ys221 A STILL TIME",
"24ys207 STAY WITH ME FOREVER",
"25ys218 SO MUCH FOR TODAY",
"26ysn001COLONY OF LAVA",
"27ysi001OPEN YOUR HEART",
"28yss003FEENA",
"29ys124 BATTLE GROUND",
"30ys121 OVER DRIVE",
"31ys116 FAIR WIND",
"★オープニング1",
"★オープニング2"
};

char tiy122_en[][128]={
"01ys248 TO MAKE THE END OF BATTLE",
"02ys236 LILIA",
"03ys205 TOO FULL WITH LOVE",
"04ys225 APATHETIC STORY",
"05ys226 MAY I HELP YOU",
"06ys228 FEEL BLUE",
"07ys201 RUINS OF MOONDORIA",
"08ys220 NOBLE DISTRICT OF TOAL",
"09ys249 REST IN PEACE",
"10ys241 CAVERN OF RASTEENIE",
"11ys244 PROTECTERS",
"12ys242 ICE RIDGE OF NOLTIA",
"13ys203 INSIDE OF THE ICE WALL",
"14ys243 MOAT OF BURNEBLESS",
"15ys232 TENDER PEOPLE",
"16ys240 PALACE OF SALMON",
"17ys233 SUBTERRANEAN CANAL",
"18ys245 COMPANILE OF LANE",
"19ys223 PRESSURE ROAD",
"20ys235 DON'T GO SO SMOOTHLY!",
"21ys234 FEENA",
"22ys246 TERMINATION",
"23ys221 A STILL TIME",
"24ys207 STAY WITH ME FOREVER",
"25ys218 SO MUCH FOR TODAY",
"26ysn001COLONY OF LAVA",
"27ysi001OPEN YOUR HEART",
"28yss003FEENA",
"29ys124 BATTLE GROUND",
"30ys121 OVER DRIVE",
"31ys116 FAIR WIND",
"★Opening 1",
"★Opening 2"
};

#define YS122_ARR(SUF) static const char tiy122_##SUF[][128]
#define YS122_ARR_END ;
#define YS122_INIT "01ys248 TO MAKE THE END OF BATTLE","02ys236 LILIA","03ys205 TOO FULL WITH LOVE","04ys225 APATHETIC STORY","05ys226 MAY I HELP YOU","06ys228 FEEL BLUE","07ys201 RUINS OF MOONDORIA","08ys220 NOBLE DISTRICT OF TOAL","09ys249 REST IN PEACE","10ys241 CAVERN OF RASTEENIE","11ys244 PROTECTERS","12ys242 ICE RIDGE OF NOLTIA","13ys203 INSIDE OF THE ICE WALL","14ys243 MOAT OF BURNEBLESS","15ys232 TENDER PEOPLE","16ys240 PALACE OF SALMON","17ys233 SUBTERRANEAN CANAL","18ys245 COMPANILE OF LANE","19ys223 PRESSURE ROAD","20ys235 DON'T GO SO SMOOTHLY!","21ys234 FEENA","22ys246 TERMINATION","23ys221 A STILL TIME","24ys207 STAY WITH ME FOREVER","25ys218 SO MUCH FOR TODAY","26ysn001COLONY OF LAVA","27ysi001OPEN YOUR HEART","28yss003FEENA","29ys124 BATTLE GROUND","30ys121 OVER DRIVE","31ys116 FAIR WIND","★Opening 1","★Opening 2"
YS122_ARR(fr)={YS122_INIT} YS122_ARR_END
YS122_ARR(it)={YS122_INIT} YS122_ARR_END
YS122_ARR(es)={YS122_INIT} YS122_ARR_END
YS122_ARR(ko)={YS122_INIT} YS122_ARR_END
YS122_ARR(zh)={YS122_INIT} YS122_ARR_END
YS122_ARR(ar)={YS122_INIT} YS122_ARR_END
YS122_ARR(ru)={YS122_INIT} YS122_ARR_END
YS122_ARR(de)={YS122_INIT} YS122_ARR_END
YS122_ARR(pt)={YS122_INIT} YS122_ARR_END
YS122_ARR(nl)={YS122_INIT} YS122_ARR_END
YS122_ARR(pl)={YS122_INIT} YS122_ARR_END
YS122_ARR(tr)={YS122_INIT} YS122_ARR_END
#undef YS122_INIT
#undef YS122_ARR
#undef YS122_ARR_END

static inline CString Ys122Track(int i){ switch(savedata.lang){ case 0: return GameTrackTitle(tiy122[i]); case 1: return GameTrackTitle(tiy122_en[i]); case 2: return GameTrackTitle(tiy122_fr[i]); case 3: return GameTrackTitle(tiy122_it[i]); case 4: return GameTrackTitle(tiy122_es[i]); case 5: return GameTrackTitle(tiy122_ko[i]); case 6: return GameTrackTitle(tiy122_zh[i]); case 7: return GameTrackTitle(tiy122_ar[i]); case 8: return GameTrackTitle(tiy122_ru[i]); case 9: return GameTrackTitle(tiy122_de[i]); case 10: return GameTrackTitle(tiy122_pt[i]); case 11: return GameTrackTitle(tiy122_nl[i]); case 12: return GameTrackTitle(tiy122_pl[i]); case 13: return GameTrackTitle(tiy122_tr[i]); default: return GameTrackTitle(tiy122_en[i]); }}

CString CYs12_2::Gett(int a){
	CString s,ss;
	s=Ys122Track(a);
	ss=s.Left(8);ss.TrimRight();
	fnn=s.Mid(8);fnn.TrimRight();
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CYs12_2::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=Ys122Track(idx);
	ret=s.Left(8); ret.TrimRight();
	ret2=m_list.GetCurSel();
#if UNICODE
	if(s.Left(1)=="★"){
		fnn=s.Mid(1);
#else
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
#endif
	}else{
		fnn=s.Mid(8);fnn.TrimRight();
	}
	EndDialog(1567);
}

BOOL CYs12_2::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"Ys 12 完全版 Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 完全版 Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2", L"Ys 12 Complete Ys2"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(33);i++)
	{
		CString s;
		s=Ys122Track(i);
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

