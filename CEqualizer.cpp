// CEqualizer.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CEqualizer.h"


// CEqualizer ダイアログ

IMPLEMENT_DYNAMIC(CEqualizer, CCustomDialogEx)

CEqualizer::CEqualizer(CWnd* pParent /*=nullptr*/)
	: CCustomDialogEx(IDD_EQUALIZER, pParent)
{

}

CEqualizer::~CEqualizer()
{
}

void CEqualizer::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SLIDER7, m_s0);
	DDX_Control(pDX, IDC_SLIDER9, m_s1);
	DDX_Control(pDX, IDC_SLIDER8, m_s2);
	DDX_Control(pDX, IDC_SLIDER10, m_s3);
	DDX_Control(pDX, IDC_SLIDER11, m_s4);
	DDX_Control(pDX, IDC_SLIDER12, m_s5);
	DDX_Control(pDX, IDC_SLIDER13, m_s6);
	DDX_Control(pDX, IDC_SLIDER14, m_s7);
	DDX_Control(pDX, IDC_SLIDER15, m_s8);
	DDX_Control(pDX, IDC_SLIDER16, m_s9);
	DDX_Control(pDX, IDC_STATIC_e0, m_v0);
	DDX_Control(pDX, IDC_STATIC_e1, m_v1);
	DDX_Control(pDX, IDC_STATIC_e2, m_v2);
	DDX_Control(pDX, IDC_STATIC_e3, m_v3);
	DDX_Control(pDX, IDC_STATIC_e4, m_v4);
	DDX_Control(pDX, IDC_STATIC_e5, m_v5);
	DDX_Control(pDX, IDC_STATIC_e6, m_v6);
	DDX_Control(pDX, IDC_STATIC_e7, m_v7);
	DDX_Control(pDX, IDC_STATIC_e8, m_v8);
	DDX_Control(pDX, IDC_STATIC_e9, m_v9);
	DDX_Control(pDX, IDC_COMBO1, m_env);
	DDX_Control(pDX, IDC_COMBO5, m_pre);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDOK3, dum);
}


BEGIN_MESSAGE_MAP(CEqualizer, CCustomDialogEx)
	ON_CBN_SELCHANGE(IDC_COMBO1, &CEqualizer::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO5, &CEqualizer::OnCbnSelchangeCombo5)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDOK3, &CEqualizer::OnBnClickedOk3)
END_MESSAGE_MAP()

extern save savedata;

// CEqualizer メッセージ ハンドラー
BOOL CEqualizer::OnInitDialog()
{
	CCustomDialogEx::OnInitDialog();


	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), _T("閉じます"));
	m_tooltip.AddTool(GetDlgItem(IDOK3), _T("イコライザーの値をリセットします"));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);


	CString s;
	s.Format(L"%d", savedata.eq[0]);
	m_v0.SetWindowText(s);
	s.Format(L"%d", savedata.eq[1]);
	m_v1.SetWindowText(s);
	s.Format(L"%d", savedata.eq[2]);
	m_v2.SetWindowText(s);
	s.Format(L"%d", savedata.eq[3]);
	m_v3.SetWindowText(s);
	s.Format(L"%d", savedata.eq[4]);
	m_v4.SetWindowText(s);
	s.Format(L"%d", savedata.eq[5]);
	m_v5.SetWindowText(s);
	s.Format(L"%d", savedata.eq[6]);
	m_v6.SetWindowText(s);
	s.Format(L"%d", savedata.eq[7]);
	m_v7.SetWindowText(s);
	s.Format(L"%d", savedata.eq[8]);
	m_v8.SetWindowText(s);
	s.Format(L"%d", savedata.eq[9]);
	m_v9.SetWindowText(s);

	m_s0.SetMode(1);
	m_s1.SetMode(1);
	m_s2.SetMode(1);
	m_s3.SetMode(1);
	m_s4.SetMode(1);
	m_s5.SetMode(1);
	m_s6.SetMode(1);
	m_s7.SetMode(1);
	m_s8.SetMode(1);
	m_s9.SetMode(1);

	m_s0.SetRange(0, 200);
	m_s1.SetRange(0, 200);
	m_s2.SetRange(0, 200);
	m_s3.SetRange(0, 200);
	m_s4.SetRange(0, 200);
	m_s5.SetRange(0, 200);
	m_s6.SetRange(0, 200);
	m_s7.SetRange(0, 200);
	m_s8.SetRange(0, 200);
	m_s9.SetRange(0, 200);

	m_s0.SetPos(200 - savedata.eq[0]);
	m_s1.SetPos(200 - savedata.eq[1]);
	m_s2.SetPos(200 - savedata.eq[2]);
	m_s3.SetPos(200 - savedata.eq[3]);
	m_s4.SetPos(200 - savedata.eq[4]);
	m_s5.SetPos(200 - savedata.eq[5]);
	m_s6.SetPos(200 - savedata.eq[6]);
	m_s7.SetPos(200 - savedata.eq[7]);
	m_s8.SetPos(200 - savedata.eq[8]);
	m_s9.SetPos(200 - savedata.eq[9]);

	m_env.AddString(L"なし");
	m_env.AddString(L"風呂場");
	m_env.AddString(L"ホール");
	m_env.AddString(L"教会");
	m_env.AddString(L"洞窟");
	m_env.AddString(L"スタジオ");
	m_env.AddString(L"ライブハウス");
	m_env.AddString(L"森");
	m_env.AddString(L"山");
	m_env.AddString(L"広場");
	m_env.AddString(L"カテドラル (大聖堂)");
	m_env.AddString(L"体育館");
	m_env.AddString(L"峡谷");
	m_env.AddString(L"地下室");
	m_env.AddString(L"劇場");
	m_env.AddString(L"水中");
	m_env.SetCurSel(savedata.eqsoundenv);

	m_pre.AddString(L"デフォルト");
	m_pre.AddString(L"低音ブースト");
	m_pre.AddString(L"高音ブースト");
	m_pre.AddString(L"ボーカル強調");
	m_pre.AddString(L"低音カット");
	m_pre.AddString(L"高音カット");
	m_pre.AddString(L"ラウドネス");
	m_pre.AddString(L"クラシック");
	m_pre.AddString(L"ロック");
	m_pre.AddString(L"カスタム");
	m_pre.SetCurSel(savedata.eqsoundeq);

	SetTimer(1, 100, NULL);
	return TRUE;
}
void CEqualizer::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eqsoundenv = m_env.GetCurSel();
}

void CEqualizer::OnCbnSelchangeCombo5()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	KillTimer(1);
	savedata.eqsoundeq = m_pre.GetCurSel();
	SetTimer(1, 100, NULL);
}

void CEqualizer::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	if (mod != savedata.eqsoundeq) {
		if (savedata.eqsoundeq != 9) {
			m_s0.SetPos(200 - savedata.eq[0]);
			m_s1.SetPos(200 - savedata.eq[1]);
			m_s2.SetPos(200 - savedata.eq[2]);
			m_s3.SetPos(200 - savedata.eq[3]);
			m_s4.SetPos(200 - savedata.eq[4]);
			m_s5.SetPos(200 - savedata.eq[5]);
			m_s6.SetPos(200 - savedata.eq[6]);
			m_s7.SetPos(200 - savedata.eq[7]);
			m_s8.SetPos(200 - savedata.eq[8]);
			m_s9.SetPos(200 - savedata.eq[9]);
		}
		mod = savedata.eqsoundeq;
	}
	CString s;
	int vol;
	int flg = 0;
	vol = 200 - m_s0.GetPos();
	if (vol != savedata.eq[0]) { s.Format(L"%d", vol); m_v0.SetWindowText(s); flg = 1; }
	savedata.eq[0] = vol;
	vol = 200 - m_s1.GetPos();
	if (vol != savedata.eq[1]) { s.Format(L"%d", vol); m_v1.SetWindowText(s); flg = 1;}
	savedata.eq[1] = vol;
	vol = 200 - m_s2.GetPos();
	if (vol != savedata.eq[2]) { s.Format(L"%d", vol); m_v2.SetWindowText(s); flg = 1;	}
	savedata.eq[2] = vol;
	vol = 200 - m_s3.GetPos();
	if (vol != savedata.eq[3]) { s.Format(L"%d", vol); m_v3.SetWindowText(s); flg = 1;	}
	savedata.eq[3] = vol;
	vol = 200 - m_s4.GetPos();
	if (vol != savedata.eq[4]) { s.Format(L"%d", vol); m_v4.SetWindowText(s);  flg = 1;	}
	savedata.eq[4] = vol;
	vol = 200 - m_s5.GetPos();
	if (vol != savedata.eq[5]) { s.Format(L"%d", vol); m_v5.SetWindowText(s); flg = 1;	}
	savedata.eq[5] = vol;
	vol = 200 - m_s6.GetPos();
	if (vol != savedata.eq[6]) { s.Format(L"%d", vol); m_v6.SetWindowText(s); flg = 1;	}
	savedata.eq[6] = vol;
	vol = 200 - m_s7.GetPos();
	if (vol != savedata.eq[7]) { s.Format(L"%d", vol); m_v7.SetWindowText(s); flg = 1;	}
	savedata.eq[7] = vol;
	vol = 200 - m_s8.GetPos();
	if (vol != savedata.eq[8]) { s.Format(L"%d", vol); m_v8.SetWindowText(s); flg = 1;	}
	savedata.eq[8] = vol;
	vol = 200 - m_s9.GetPos();
	if (vol != savedata.eq[9]) { s.Format(L"%d", vol); m_v9.SetWindowText(s); flg = 1;	}
	savedata.eq[9] = vol;
	
	if (flg == 1) { m_pre.SetCurSel(9); savedata.eqsoundeq = 9; }

	CCustomDialogEx::OnTimer(nIDEvent);
}

void CEqualizer::OnBnClickedOk3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eq[0] = 100;
	savedata.eq[1] = 100;
	savedata.eq[2] = 100;
	savedata.eq[3] = 100;
	savedata.eq[4] = 100;
	savedata.eq[5] = 100;
	savedata.eq[6] = 100;
	savedata.eq[7] = 100;
	savedata.eq[8] = 100;
	savedata.eq[9] = 100;
	m_s0.SetPos(200 - savedata.eq[0]);
	m_s1.SetPos(200 - savedata.eq[1]);
	m_s2.SetPos(200 - savedata.eq[2]);
	m_s3.SetPos(200 - savedata.eq[3]);
	m_s4.SetPos(200 - savedata.eq[4]);
	m_s5.SetPos(200 - savedata.eq[5]);
	m_s6.SetPos(200 - savedata.eq[6]);
	m_s7.SetPos(200 - savedata.eq[7]);
	m_s8.SetPos(200 - savedata.eq[8]);
	m_s9.SetPos(200 - savedata.eq[9]);

}
