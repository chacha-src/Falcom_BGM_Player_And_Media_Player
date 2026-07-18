// XA.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "XA.h"

extern CString fnn;
// CXA ダイアログ

IMPLEMENT_DYNAMIC(CXA, CCustomBlurDialogBase)

CXA::CXA(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CXA::IDD, pParent)
{

}

CXA::~CXA()
{
}

void CXA::DoDataExchange(CDataExchange* pDX)
{
	DDX_Control(pDX, IDC_LIST1, m_list);
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CXA, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CXA);


// CXA メッセージ ハンドラ
TCHAR tixa[][128] = {
L"003 Two love",
L"000 LA VALSE POUR XANADU～XANADU NEXT OPENING",
L"004 Harlech",
L"010 LA VALSE POUR XANADU～XANADU NEXT FIELD",
L"030 The Eternal Maze",
L"020 Clover Ruins",
L"100 LA VALSE POUR XANADU～XANADU NEXT BATTLE",
L"040 Egret Mountains",
L"050 The Treacherous Woods",
L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMISSION",
L"070 Xanadu Labyrinth",
L"340 The One",
L"080 Time Crevice",
L"110 Bump in the road",
L"090 The Castle of Strange Rock",
L"320 Beginning of the Rock",
L"200 Devil adent",
L"210 evildoer",
L"310 True Intent",
L"001 Two love～Eternity",
L"002 White Lie in Black",
L"350 Two live～Requiem",
L"300 LA VALSE POUR XANADU～XANADU NEXT EVENT",
L"060 The Eternal Maze",
L"★ファルコムロゴ",
L"★オープニング"
};

TCHAR tixa_en[][128] = {
L"003 Two love",
L"000 LA VALSE POUR XANADU～XANADU NEXT OPENING",
L"004 Harlech",
L"010 LA VALSE POUR XANADU～XANADU NEXT FIELD",
L"030 The Eternal Maze",
L"020 Clover Ruins",
L"100 LA VALSE POUR XANADU～XANADU NEXT BATTLE",
L"040 Egret Mountains",
L"050 The Treacherous Woods",
L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMISSION",
L"070 Xanadu Labyrinth",
L"340 The One",
L"080 Time Crevice",
L"110 Bump in the road",
L"090 The Castle of Strange Rock",
L"320 Beginning of the Rock",
L"200 Devil adent",
L"210 evildoer",
L"310 True Intent",
L"001 Two love～Eternity",
L"002 White Lie in Black",
L"350 Two live～Requiem",
L"300 LA VALSE POUR XANADU～XANADU NEXT EVENT",
L"060 The Eternal Maze",
L"★FALCOM logo",
L"★Opening"
};

TCHAR tixa_fr[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT OUVERTURE",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT TERRAIN",L"030 The Eternal Maze",L"020 Ruines de Trèfle",L"100 LA VALSE POUR XANADU～XANADU NEXT BATAILLE",L"040 Montagnes Egret",L"050 Les Bois Traîtres",L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMISSION",L"070 Labyrinthe Xanadu",L"340 The One",L"080 Brèche Temporelle",L"110 Bump in the road",L"090 Le Château de Pierre Étrange",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Eternité",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT ÉVÉNEMENT",L"060 The Eternal Maze",L"★Logo FALCOM",L"★Ouverture"
};

TCHAR tixa_de[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT ERÖFFNUNG",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT FELD",L"030 The Eternal Maze",L"020 Klee-Ruinen",L"100 LA VALSE POUR XANADU～XANADU NEXT KAMPF",L"040 Egret-Berge",L"050 Der Tückische Wald",L"330 LA VALSE POUR XANADU～XANADU NEXT ZWISCHENAKT",L"070 Xanadu Labyrinth",L"340 The One",L"080 Zeit-Spalte",L"110 Bump in the road",L"090 Die Burg aus Seltsamem Fels",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Ewigkeit",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT EREIGNIS",L"060 The Eternal Maze",L"★FALCOM Logo",L"★Eröffnung"
};

TCHAR tixa_es[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT APERTURA",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT CAMPO",L"030 The Eternal Maze",L"020 Ruinas del Trébol",L"100 LA VALSE POUR XANADU～XANADU NEXT BATALLA",L"040 Montañas Egret",L"050 Los Bosques Traicioneros",L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMEDIO",L"070 Laberinto Xanadu",L"340 The One",L"080 Brecha Temporal",L"110 Bump in the road",L"090 El Castillo de Roca Extraña",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Eternidad",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT EVENTO",L"060 The Eternal Maze",L"★Logo FALCOM",L"★Apertura"
};

TCHAR tixa_it[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT APERTURA",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT CAMPO",L"030 The Eternal Maze",L"020 Rovine Trifoglio",L"100 LA VALSE POUR XANADU～XANADU NEXT BATTAGLIA",L"040 Montagne Egret",L"050 I Boschi Insidiosi",L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMEZZO",L"070 Labirinto Xanadu",L"340 The One",L"080 Fessura Temporale",L"110 Bump in the road",L"090 Il Castello di Roccia Strana",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Eternità",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT EVENTO",L"060 The Eternal Maze",L"★Logo FALCOM",L"★Apertura"
};

TCHAR tixa_ko[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT 오프닝",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT 필드",L"030 The Eternal Maze",L"020 클로버 유적",L"100 LA VALSE POUR XANADU～XANADU NEXT 전투",L"040 에그렛 산맥",L"050 treacherous Woods",L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMISSION",L"070 샤나두 미궁",L"340 The One",L"080 Time Crevice",L"110 Bump in the road",L"090 Strange Rock의 성",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～영원",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT 이벤트",L"060 The Eternal Maze",L"★팔콤 로고",L"★오프닝"
};

TCHAR tixa_zh[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT 片头",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT 野外",L"030 The Eternal Maze",L"020 三叶草遗迹",L"100 LA VALSE POUR XANADU～XANADU NEXT 战斗",L"040 白鹭山脉",L"050  treacherous Woods",L"330 LA VALSE POUR XANADU～XANADU NEXT 幕间",L"070 夏那都迷宫",L"340 The One",L"080 Time Crevice",L"110 Bump in the road",L"090 奇怪 rock城堡",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～永恒",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT 事件",L"060 The Eternal Maze",L"★FALCOM标志",L"★片头"
};

TCHAR tixa_ar[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT افتتاحية",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT حقل",L"030 The Eternal Maze",L"020 أطلال البرسيم",L"100 LA VALSE POUR XANADU～XANADU NEXT معركة",L"040 جبال إيغريت",L"050 الغابة الخادعة",L"330 LA VALSE POUR XANADU～XANADU NEXT استراحة",L"070 متاهة زانادو",L"340 The One",L"080 فجوة زمنية",L"110 Bump in the road",L"090 قلعة الصخر الغريب",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～الأبدية",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT حدث",L"060 The Eternal Maze",L"★شعار FALCOM",L"★افتتاحية"
};

TCHAR tixa_ru[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT Заставка",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT Поле",L"030 The Eternal Maze",L"020 Руины Клевера",L"100 LA VALSE POUR XANADU～XANADU NEXT Битва",L"040 Горы Эгрет",L"050 Коварный Лес",L"330 LA VALSE POUR XANADU～XANADU NEXT INTERMISSION",L"070 Лабиринт Ксанаду",L"340 The One",L"080 Временная трещина",L"110 Bump in the road",L"090 Замок Странной Скалы",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Вечность",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT Событие",L"060 The Eternal Maze",L"★Лого FALCOM",L"★Заставка"
};

TCHAR tixa_pt[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT Abertura",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT Campo",L"030 The Eternal Maze",L"020 Ruínas do Trevo",L"100 LA VALSE POUR XANADU～XANADU NEXT Batalha",L"040 Montanhas Egret",L"050 O Bosque Traiçoeiro",L"330 LA VALSE POUR XANADU～XANADU NEXT Intermissão",L"070 Labirinto Xanadu",L"340 The One",L"080 Fenda Temporal",L"110 Bump in the road",L"090 O Castelo de Rocha Estranha",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Eternidade",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT Evento",L"060 The Eternal Maze",L"★Logo FALCOM",L"★Abertura"
};

TCHAR tixa_nl[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT Opening",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT Veld",L"030 The Eternal Maze",L"020 Klaver Ruïnes",L"100 LA VALSE POUR XANADU～XANADU NEXT Gevecht",L"040 Egret Bergen",L"050 Het Verraderlijke Bos",L"330 LA VALSE POUR XANADU～XANADU NEXT Tussenspel",L"070 Xanadu Labyrinth",L"340 The One",L"080 Tijdkloof",L"110 Bump in the road",L"090 Het Kasteel van Vreemde Rots",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Eeuwigheid",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT Gebeurtenis",L"060 The Eternal Maze",L"★FALCOM logo",L"★Opening"
};

TCHAR tixa_pl[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT Odsłony",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT Pole",L"030 The Eternal Maze",L"020 Ruiny Koniczyny",L"100 LA VALSE POUR XANADU～XANADU NEXT Bitwa",L"040 Góry Egret",L"050 Zdradliwy Las",L"330 LA VALSE POUR XANADU～XANADU NEXT Przerwa",L"070 Labirynt Xanadu",L"340 The One",L"080 Szczelina Czasu",L"110 Bump in the road",L"090 Zamek Dziwnej Skały",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Wieczność",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT Wydarzenie",L"060 The Eternal Maze",L"★Logo FALCOM",L"★Odsłony"
};

TCHAR tixa_tr[][128] = {
L"003 Two love",L"000 LA VALSE POUR XANADU～XANADU NEXT Açılış",L"004 Harlech",L"010 LA VALSE POUR XANADU～XANADU NEXT Alan",L"030 The Eternal Maze",L"020 Yonca Harabeleri",L"100 LA VALSE POUR XANADU～XANADU NEXT Savaş",L"040 Egret Dağları",L"050 Hain Orman",L"330 LA VALSE POUR XANADU～XANADU NEXT Ara",L"070 Xanadu Labirenti",L"340 The One",L"080 Zaman Yarığı",L"110 Bump in the road",L"090 Garip Kaya Kalesi",L"320 Beginning of the Rock",L"200 Devil adent",L"210 evildoer",L"310 True Intent",L"001 Two love～Sonsuzluk",L"002 White Lie in Black",L"350 Two live～Requiem",L"300 LA VALSE POUR XANADU～XANADU NEXT Olay",L"060 The Eternal Maze",L"★FALCOM logosu",L"★Açılış"
};

static inline CString XATrack(int i) {
	switch (savedata.lang) {
		case 0: return CString(tixa[i]);
		case 1: return CString(tixa_en[i]);
		case 2: return CString(tixa_fr[i]);
		case 3: return CString(tixa_it[i]);
		case 4: return CString(tixa_es[i]);
		case 5: return CString(tixa_ko[i]);
		case 6: return CString(tixa_zh[i]);
		case 7: return CString(tixa_ar[i]);
		case 8: return CString(tixa_ru[i]);
		case 9: return CString(tixa_de[i]);
		case 10: return CString(tixa_pt[i]);
		case 11: return CString(tixa_nl[i]);
		case 12: return CString(tixa_pl[i]);
		case 13: return CString(tixa_tr[i]);
		default: return CString(tixa_en[i]);
	}
}

CString CXA::Gett(int a){
	CString s,ss;
	s = XATrack(a); ss=s.Left(3);
	switch(_tstoi(ss)){
		case 0: loop1=0; loop2=0; break;
		case 1: loop1=0; loop2=0; break;
		case 2: loop1=0; loop2=0; break;
		case 3: loop1=22016; loop2=5321728; break;
		case 4: loop1=472448; loop2=7228160; break;
		case 10: loop1=8704; loop2=6857728; break;
		case 20: loop1=3007904; loop2=8934944; break;
		case 30: loop1=284744; loop2=6593758; break;
		case 40: loop1=220584; loop2=8246875; break;
		case 50: loop1=654680; loop2=7419711; break;
		case 60: loop1=0; loop2=0; break;
		case 70: loop1=235472; loop2=6463247; break;
		case 80: loop1=1102500; loop2=3946951; break;
		case 90: loop1=789439; loop2=7420378; break;
		case 100: loop1=613590; loop2=5551813; break;
		case 110: loop1=203538; loop2=7140808; break;
		case 200: loop1=1130472; loop2=6374367; break;
		case 210: loop1=1310208; loop2=8083968; break;
		case 300: loop1=292864; loop2=5549568; break;
		case 310: loop1=2844816; loop2=8254598; break;
		case 320: loop1=271460; loop2=5728835; break;
		case 330: loop1=0; loop2=0; break;
		case 340: loop1=551531; loop2=5644011; break;
		case 350: loop1=21120; loop2=3056830; break;
		default: loop1=0; loop2=0; break;
	}
	fnn=s.Mid(4);
	return s;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CXA::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s = XATrack(idx);
	ret=_tstoi(s.Left(3));
	ret2=m_list.GetCurSel();
	switch(ret){
		case 0: loop1=0; loop2=0; break;
		case 1: loop1=0; loop2=0; break;
		case 2: loop1=0; loop2=0; break;
		case 3: loop1=22016; loop2=5321728; break;
		case 4: loop1=472448; loop2=7228160; break;
		case 10: loop1=8704; loop2=6857728; break;
		case 20: loop1=3007904; loop2=8934944; break;
		case 30: loop1=284744; loop2=6593758; break;
		case 40: loop1=220584; loop2=8246875; break;
		case 50: loop1=654680; loop2=7419711; break;
		case 60: loop1=0; loop2=0; break;
		case 70: loop1=235472; loop2=6463247; break;
		case 80: loop1=1102500; loop2=3946951; break;
		case 90: loop1=789439; loop2=7420378; break;
		case 100: loop1=613590; loop2=5551813; break;
		case 110: loop1=203538; loop2=7140808; break;
		case 200: loop1=1130472; loop2=6374367; break;
		case 210: loop1=1310208; loop2=8083968; break;
		case 300: loop1=292864; loop2=5549568; break;
		case 310: loop1=2844816; loop2=8254598; break;
		case 320: loop1=271460; loop2=5728835; break;
		case 330: loop1=0; loop2=0; break;
		case 340: loop1=551531; loop2=5644011; break;
		case 350: loop1=21120; loop2=3056830; break;
		default: loop1=0; loop2=0; break;
	}
	if(ret2>24)
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

BOOL CXA::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(L"XANADU NEXT");
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<26;i++)
	{
		CString s = XATrack(i);
		if(s.Left(2)!=_T("★")){ CString t=_T("XANA"); t+=s; s=t; }
		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}

	m_list.SetCurSel(0);
	if(ret!=0) 
//		if(ret>65) m_list.SetCurSel(ret);
//		else m_list.SetCurSel(ret-1);
		m_list.SetCurSel(ret);
	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
