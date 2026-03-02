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

#define YSF_ARR(SUF) static const WCHAR tiys3_##SUF[][128]
#define YSF_ARR_END ;

YSF_ARR(fr)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(it)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(es)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(ko)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(zh)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(ar)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(ru)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(de)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(pt)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(nl)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(pl)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END
YSF_ARR(tr)={L"01.Dancing on the road(unknown)",L"02.Omen =Styx=(Menu)",L"03.Trade town Redmont(Redmont town)",L"04.Quiet moment(Elder's house)",L"05.Welcome!!(Shop)",L"06.Prelude to adventure(Map)",L"07.The Boy with Wings(Field map)",L"08.Be careful(Tigray Quarry)",L"09.The Black Beast(Boss battle)",L"10.Ruins of Ilburnz(Ilburnz ruins)",L"11.Blazing death battle(Lava area)",L"12.Darkness trap(Deep Tigray Quarry)",L"13.Reaper's lightning(Boss battle)",L"14.Momentary dream(Game over)",L"15.Strict fighting spirit(Eldarm Mountains)",L"16.Melancholy Twilight(Redmont after crisis)",L"17.Barestayn Castle(Barestayn Castle)",L"18.Prayer of mercy(Barestayn Cathedral)",L"19.Key of light(After ending)",L"20.Seal of time(Barestayn Clock Tower)",L"21.Pulse of destruction(Genos Island)",L"22.Tower of fate(Deep Genos Island)",L"23.Behold!!(Nicholas battle)",L"24.Strongest enemy(Galbalan battle)",L"25.Morning of departure(Redmont after clear)",L"26.Wanderers from Ys(Ending(video))",L"27.Dear My Brother(Elena, Chester event)",L"28.Beloved Elena(Elena theme)",L"29.Introduction!!(Redmont event)",L"30.The Theme of Chester(Chester theme)",L"31.Chop!!(Boss battle)",L"32.Believe in my heart(Redmont just before clear)",L"33.Omen =Styx=(Opening(video))",L"34.Beloved Elena(Galbalan island collapse(video))"} YSF_ARR_END

#undef YSF_ARR
#undef YSF_ARR_END

static inline CString YSF_TRACK(int i){ switch(savedata.lang){ case 0: return CString(CStringA(tiys3[i])); case 1: return CString(CStringA(tiys3_en[i])); case 2: return CString(CStringA(tiys3_fr[i])); case 3: return CString(CStringA(tiys3_it[i])); case 4: return CString(CStringA(tiys3_es[i])); case 5: return CString(CStringA(tiys3_ko[i])); case 6: return CString(CStringA(tiys3_zh[i])); case 7: return CString(CStringA(tiys3_ar[i])); case 8: return CString(CStringA(tiys3_ru[i])); case 9: return CString(CStringA(tiys3_de[i])); case 10: return CString(CStringA(tiys3_pt[i])); case 11: return CString(CStringA(tiys3_nl[i])); case 12: return CString(CStringA(tiys3_pl[i])); case 13: return CString(CStringA(tiys3_tr[i])); default: return CString(CStringA(tiys3_en[i])); }}

void Citiran_YSF::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s,ss;	s=YSF_TRACK(idx);
	ss=s.Left(2);ret=_tstoi(ss)-1;
	fnn=s.Mid(3);
	EndDialog(1567);
}

void Citiran_YSF::Gett(int a){
	CString s,ss;
	s=YSF_TRACK(a);
	ss=s.Left(6);ss.TrimRight();
	fnn=s.Mid(3);
}

BOOL Citiran_YSF::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL14(L"イース -フェルガナの誓い-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-", L"Ys -Felghana no Chikai-"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(34);i++)
	{
		CString s;
		s=YSF_TRACK(i);
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
