// itiran_YS6.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "itiran_YS6.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Citiran_YS6 ダイアログ

extern CString fnn;

Citiran_YS6::Citiran_YS6(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(Citiran_YS6::IDD, pParent)
{
	//{{AFX_DATA_INIT(Citiran_YS6)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void Citiran_YS6::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(Citiran_YS6)
	DDX_Control(pDX, IDC_LIST1, m_list);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(Citiran_YS6, CCustomBlurDialogBase)
	//{{AFX_MSG_MAP(Citiran_YS6)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	//}}AFX_MSG_MAP
	cmn(Citiran_YS6);

/////////////////////////////////////////////////////////////////////////////
// Citiran_YS6 メッセージ ハンドラ

char tiys6[][128]={
"01.タイトル/ロード",
"02.オープニング(動画あり)",
"03.Ｆｉｎ",
"04.レダの集落",
"05.ショップ",
"06.クアテラ樹海",
"07.中ボス一般",
"08.海底参道",
"09.リモージュの港町",
"10.カナン平原",
"11.グラナヴァリス",
"12.忘却の遺跡",
"13.ゼメスの聖地",
"14.鍾乳洞・地下湖",
"15.襲撃",
"16.占拠中",
"17.ロムン軍艦（通常）",
"18.ロムン軍艦（脱出）",
"19.アルマの墳墓",
"20.ガルヴァ＝ロア戦",
"21.エルンスト戦",
"22.ナピシュテムの匣,螺旋回廊",
"23.ナピ核戦<第１段階>（ラスボス）",
"24.ナピ核戦<第２段階>（ラスボス）",
"25.スタッフロール",
"26.ムービー1（渦消滅～ロムン襲来）(動画あり)",
"27.ムービー2（ナピ復活）(動画あり)",
"28.ムービー3（ナピ崩壊）(動画あり)",
"29.ムービー4（エンディング1）(動画あり)",
"30.ゲームオーバー",
"30.ゲームオーバー"
};

char tiys6_en[][198]={
"01.Title/Load",
"02.Opening(with video)",
"03.Fin",
"04.Ledah's village",
"05.Shop",
"06.Quatera Forest",
"07.Mid-boss general",
"08.Undersea approach",
"09.Limoges port town",
"10.Canaan Plains",
"11.Granvalis",
"12.Ruins of Oblivion",
"13.Sacred Ground of Zemeth",
"14.Stalactite cave/Underground lake",
"15.Assault",
"16.Occupied",
"17.Romun warship(normal)",
"18.Romun warship(escape)",
"19.Alma's tomb",
"20.Battle with Galbalan",
"21.Battle with Ernst",
"22.Napishtim's Ark, Spiral Corridor",
"23.Nap core battle<Phase 1>(Last boss)",
"24.Nap core battle<Phase 2>(Last boss)",
"25.Staff roll",
"26.Movie 1(vortex gone~Romun attack)(with video)",
"27.Movie 2(Nap revival)(with video)",
"28.Movie 3(Nap collapse)(with video)",
"29.Movie 4(Ending 1)(with video)",
"30.Game over",
"30.Game over"
};

#define YS6_ARR(SUF) static const char tiys6_##SUF[][128]
#define YS6_ARR_END ;
YS6_ARR(fr)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(it)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(es)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(ko)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(zh)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(ar)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(ru)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(de)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(pt)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(nl)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(pl)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
YS6_ARR(tr)={"01.Title/Load","02.Opening(with video)","03.Fin","04.Ledah's village","05.Shop","06.Quatera Forest","07.Mid-boss general","08.Undersea approach","09.Limoges port town","10.Canaan Plains","11.Granvalis","12.Ruins of Oblivion","13.Sacred Ground of Zemeth","14.Stalactite cave/Underground lake","15.Assault","16.Occupied","17.Romun warship(normal)","18.Romun warship(escape)","19.Alma's tomb","20.Battle with Galbalan","21.Battle with Ernst","22.Napishtim's Ark, Spiral Corridor","23.Nap core battle<Phase 1>(Last boss)","24.Nap core battle<Phase 2>(Last boss)","25.Staff roll","26.Movie 1(vortex gone~Romun attack)(with video)","27.Movie 2(Nap revival)(with video)","28.Movie 3(Nap collapse)(with video)","29.Movie 4(Ending 1)(with video)","30.Game over","30.Game over"} YS6_ARR_END
#undef YS6_ARR
#undef YS6_ARR_END

static inline CString YS6_TRACK(int i){ switch(savedata.lang){ case 0: return GameTrackTitle(tiys6[i]); case 1: return GameTrackTitle(tiys6_en[i]); case 2: return GameTrackTitle(tiys6_fr[i]); case 3: return GameTrackTitle(tiys6_it[i]); case 4: return GameTrackTitle(tiys6_es[i]); case 5: return GameTrackTitle(tiys6_ko[i]); case 6: return GameTrackTitle(tiys6_zh[i]); case 7: return GameTrackTitle(tiys6_ar[i]); case 8: return GameTrackTitle(tiys6_ru[i]); case 9: return GameTrackTitle(tiys6_de[i]); case 10: return GameTrackTitle(tiys6_pt[i]); case 11: return GameTrackTitle(tiys6_nl[i]); case 12: return GameTrackTitle(tiys6_pl[i]); case 13: return GameTrackTitle(tiys6_tr[i]); default: return GameTrackTitle(tiys6_en[i]); }}

void Citiran_YS6::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s,ss;	s=YS6_TRACK(idx);
	ss=s.Left(2);ret=_tstoi(ss)-1;
	fnn=s.Mid(3);
	EndDialog(1567);
}

void Citiran_YS6::Gett(int a){
	CString s,ss;
	s=YS6_TRACK(a);
	ss=s.Left(6);ss.TrimRight();
	fnn=s.Mid(3);
}

BOOL Citiran_YS6::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"イースⅥ -ナピシュテムの匣-", L"Ys VI -Napishtim no Hako-", L"Ys VI - Le Coffre de Napishtim", L"Ys VI - L'Arca di Napishtim", L"Ys VI - El Arca de Napishtim", L"이스 VI - 나피슈테임의 함", L"伊苏VI - 纳比斯汀的方舟", L"Ys VI - صندوق نابشتيم", L"Ys VI - Ковчег Напиштима", L"Ys VI - Die Tribute von Napishtim", L"Ys VI - A Arca de Napishtim", L"Ys VI - De Ark van Napishtim", L"Ys VI - Arka Napishtim", L"Ys VI - Napishtim'in Sandığı"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(30);i++)
	{
		CString s;
		s=YS6_TRACK(i);
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
