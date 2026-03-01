// AudioSelect.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "AudioSelect.h"

extern CString streamname[40];
// CAudioSelect ダイアログ
extern IAMStreamSelect* iam;
extern int audionum;
extern int au;

IMPLEMENT_DYNAMIC(CAudioSelect, CCustomDialog)

CAudioSelect::CAudioSelect(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CAudioSelect::IDD, pParent)
{

}

CAudioSelect::~CAudioSelect()
{
}

void CAudioSelect::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_lb);
	DDX_Control(pDX, IDOK, m_okdummy);
}


BEGIN_MESSAGE_MAP(CAudioSelect, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, &CAudioSelect::OnLbnDblclkList1)
	ON_BN_CLICKED(IDOK, &CAudioSelect::OnBnClickedOk)
END_MESSAGE_MAP()


// CAudioSelect メッセージ ハンドラ

void CAudioSelect::OnLbnDblclkList1()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	select();
}

void CAudioSelect::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	select();
	OnOK();
}

void CAudioSelect::select()
{
	int cnt=m_lb.GetCurSel();
	no=cnt;
	EndDialog(cnt);
}

BOOL CAudioSelect::OnInitDialog()
{
	CCustomDialog::OnInitDialog();

	SetWindowText(LL2(L"再生ストリーム選択", L"Select Playback Stream"));
	SetDlgItemText(IDC_STATIC, LL2(L"複数の音声チャンネルがある時に\nこの画面が表示されます。\n再生したい音声チャンネルを\n選択して下さい。\n\n再生ウィンドウでの右クリック\nメニューからも選択できます。", L"When there are multiple audio channels,\nthis screen will be displayed.\nPlease select the audio channel\nyou want to play.\n\nYou can also select from the\nright-click menu on the playback window."));
	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL2(L"音声ストリームを決定します", L"Determine audio stream"));
	m_tooltip.SetDelayTime( TTDT_AUTOPOP, 10000 );
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);

	for(int i=0;i<no;i++){
		CString str;
		str.Format(LL2(L"音声%d:%s", L"Audio %d:%s"), i+1, streamname[i]);
		m_lb.AddString(str);
	}

	AM_MEDIA_TYPE* ppmt = NULL;
	DWORD* pdwFlags = NULL;
	for (int l = 0; l < audionum; l++) {
		int num = l + au;
		
		if (iam->Info(num, NULL, pdwFlags, NULL, NULL, NULL, NULL, NULL) == S_OK) {
			if (pdwFlags != nullptr && *pdwFlags == AMSTREAMSELECTINFO_ENABLED) {
				m_lb.SetCurSel(l);
			}
		}
	}


	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

BOOL CAudioSelect::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomDialog::PreTranslateMessage(pMsg);
}