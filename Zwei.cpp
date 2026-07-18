// Zwei.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Zwei.h"


// CZwei ダイアログ

IMPLEMENT_DYNAMIC(CZwei, CCustomBlurDialogBase)

CZwei::CZwei(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CZwei::IDD, pParent)
{

}

CZwei::~CZwei()
{
}

void CZwei::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CZwei, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	ON_BN_CLICKED(IDOK, &CZwei::OnBnClickedOk)
cmn(CZwei);

// CZwei メッセージ ハンドラ
extern CString fnn;

char tiz1[][128]={
"bgm44 浮遊大陸アルジェス　-Instroduction-",
"bgm35 永劫の夢、大空の記憶 -Zwei!! 2人で大冒険-",
"bgm03 プック村",
"bgm01 浮遊大陸アルジェス -Main Theme-",
"bgm13 パーヴェル庭園",
"bgm12 カヤパの森",
"bgm06 クロップ洞窟",
"bgm07 ケノーピ火山",
"bgm70 浮遊大陸アルジェス -夢見る秘宝-",
"bgm72 コルベットのテーマ　-ネコ言うニャーッ!-",
"bgm27 Fight!! -壊してポックル-",
"bgm60 ひとときの休息を",
"bgm16 ダプネ砂漠",
"bgm18 ヒポリタの丘",
"bgm19 トリポカ湖",
"bgm21 プシュケの屋敷",
"bgm50 おやすみ",
"bgm10 妖精たちの村",
"bgm26 スピリ古代迷宮",
"bgm71 竜の眠る道",
"bgm14 アプリエス神殿",
"bgm15 エスピナ暗黒神殿",
"bgm08 幻の大地 セルペンティナ",
"bgm22 魔王の両腕 -ハンド-",
"bgm09 最後の闘い -魔王ヴェスパー",
"bgm74 安堵のメロディ",
"bgm36 花と風のうた",
"bgm30 ムービー1 -光への誘い-",
"bgm31 ムービー2 -闇への誘い-",
"bgm32 ムービー3 -降臨-",
"bgm33 ムービー4 -大樹-",
"bgm34 ムービー5 -崩壊-",
"bgm77 Zwei!!シューティング -遊んでピピロ-",
"bgm75 Theme of Adol 2001",
"bgm76 Mona Mona",
"boss  "
};

char tiz1_en[][128]={
"bgm44 Floating Continent Arges -Introduction-",
"bgm35 Eternal Dream, Sky Memory -Zwei!! Adventure for Two-",
"bgm03 Puck Village",
"bgm01 Floating Continent Arges -Main Theme-",
"bgm13 Pavel Garden",
"bgm12 Kayapa Forest",
"bgm06 Crop Cave",
"bgm07 Kenopy Volcano",
"bgm70 Floating Continent Arges -Dreaming Treasure-",
"bgm72 Corvette's Theme -Cat Says Meow!-",
"bgm27 Fight!! -Destroy Puckle-",
"bgm60 A Moment of Rest",
"bgm16 Daphne Desert",
"bgm18 Hypolita Hill",
"bgm19 Tripoka Lake",
"bgm21 Psyche Mansion",
"bgm50 Good Night",
"bgm10 Village of Fairies",
"bgm26 Spirit Ancient Labyrinth",
"bgm71 Path of the Sleeping Dragon",
"bgm14 Apries Temple",
"bgm15 Espina Dark Temple",
"bgm08 Phantom Land Serpentina",
"bgm22 Demon Lord's Arms -HAND-",
"bgm09 Final Battle -Demon Lord Vesper",
"bgm74 Melody of Relief",
"bgm36 Song of Flowers and Wind",
"bgm30 Movie 1 -Invitation to Light-",
"bgm31 Movie 2 -Invitation to Darkness-",
"bgm32 Movie 3 -Advent-",
"bgm33 Movie 4 -Great Tree-",
"bgm34 Movie 5 -Collapse-",
"bgm77 Zwei!! Shooting -Play Pipiro-",
"bgm75 Theme of Adol 2001",
"bgm76 Mona Mona",
"boss  "
};

static inline CString ZweiTrack(int i) {
	switch (savedata.lang) {
		case 0: return GameTrackTitle(tiz1[i]);
		case 1: return GameTrackTitle(tiz1_en[i]);
		default: return GameTrackTitle(tiz1_en[i]);
	}
}

CString CZwei::Gett(int a){
	CString s,ss;
	s = ZweiTrack(a);
	ss=s.Left(5);ss.TrimRight();
	fnn=s.Mid(6);
	return ss;
}

CString ZweiFolFromIndex(int idx)
{
	if (idx < 0 || idx >= 36) return CString();
	CString s = ZweiTrack(idx);
	CString ss = s.Left(5);
	ss.TrimRight();
	if (ss.IsEmpty()) return CString();
	CString fol;
	fol.Format(_T("%s(wav.dat)"), (LPCTSTR)ss);
	return fol;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CZwei::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s = ZweiTrack(idx);
	ret=s.Left(5); ret.TrimRight();
	ret2=m_list.GetCurSel();
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
	}else{
		fnn=s.Mid(6);
	}
	EndDialog(1567);
}

BOOL CZwei::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(L"Zwei!!");
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(36);i++)
	{
		CString s = ZweiTrack(i);
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

void CZwei::OnBnClickedOk()
{
	CCustomBlurDialogBase::OnOK();
}
