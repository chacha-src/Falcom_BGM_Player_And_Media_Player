// Kpilist.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Kpilist.h"


// CKpilist ダイアログ

IMPLEMENT_DYNAMIC(CKpilist, CCustomDialog)

CKpilist::CKpilist(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CKpilist::IDD, pParent)
{

}

CKpilist::~CKpilist()
{
}

void CKpilist::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_lc);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CKpilist, CCustomDialog)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CKpilist::OnLvnItemchangedList1)
	ON_BN_CLICKED(IDOK, &CKpilist::OnBnClickedOk)
cmn(CKpilist);

// CKpilist メッセージ ハンドラ
extern CString ext[150][300];
extern int kpicnt;
extern CString kpif[400];
extern TCHAR kpifs[200][64];
extern BOOL kpichk[200];
extern BYTE kpiarch[150];
extern TCHAR karento2[1024];

BOOL CKpilist::OnInitDialog()
{
	CCustomDialog::OnInitDialog();

	SetWindowText(LL14(L"kpi一覧", L"kpi list", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""));
	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL14(L"閉じます", L"Close", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""));
	m_tooltip.SetDelayTime( TTDT_AUTOPOP, 10000 );
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);


	Init();
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

BOOL kpichks[300];
extern BYTE kvar[150][300];

void CKpilist::Init()
{
	DWORD dwExStyle = m_lc.GetExtendedStyle();
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP | LVS_EX_CHECKBOXES;
	m_lc.SetExtendedStyle(dwExStyle);
	m_lc.ModifyStyle(0, LVS_REPORT);
	m_lc.InsertColumn(0, LL14(L"kpi", L"kpi", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""), LVCFMT_LEFT, 100, 0);
	m_lc.InsertColumn(1, L"Ver", LVCFMT_CENTER, 50, 0);
	m_lc.InsertColumn(2, L"Arch", LVCFMT_CENTER, 60, 0);
	m_lc.InsertColumn(3, LL14(L"拡張子", L"Extensions", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""), LVCFMT_LEFT, 340, 0);

	//ファイル読み込み kpilist
	CString ss, sss;
	for (int i = 0; i < kpicnt; i++) {
		kpichk[i] = kpichks[i] = 1;
		ss = kpif[i].Right(kpif[i].GetLength() - kpif[i].ReverseFind('\\') - 1);
		_tcscpy(kpifs[i], ss);
	}

	CFile ff;
	int cnt = kpicnt;
	ss = karento2;
	ss += "kpilist.dat";
	if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
		ff.Read(&cnt, sizeof(cnt));
		for (int i = 0; i < cnt; i++) {
			ff.Read(&kpichk[i], sizeof(BOOL));
			ff.Read(&kpifs[i], 64*sizeof(TCHAR));
		}
		ff.Close();
	}

	TCHAR *buf;
	buf = (TCHAR*)calloc(10000, 2);
	LV_ITEM LvItem;
	int      idItem;
	m_lc.DeleteAllItems();
	int Lindex = -1;
	for (int j = 0; j<kpicnt; j++) {//選択されているものをピックアップ
		CString s; s = "";
		try {
			if (status == 0) {
			for (int i = 0;; i++) {
				if (ext[j][i] == "") break;
				s += ext[j][i]; s += "/";
			}
			s = s.Left(s.GetLength() - 1);
			int a1, a2, a3;
			a1 = kpif[j].GetLength() * 2;
			a2 = kpif[j].ReverseFind(L'\\');
			a3 = a1 - a2;
			_tcscpy(buf, kpif[j].Right(a3));	LvItem.pszText = buf;
			
			LvItem.iItem = m_lc.GetItemCount();
			LvItem.mask = LVIF_TEXT | LVIF_STATE;
			LvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
			LvItem.state = 0;
			LvItem.iSubItem = 0;
			LvItem.cchTextMax = _tcslen(LvItem.pszText);
			idItem = m_lc.InsertItem(&LvItem);
			// InsertItem() によって item ID (行番号) が返される
			LvItem.iItem = idItem;
			CString sss; sss.Format(L"%d", kvar[j][0]);	LvItem.iSubItem = 1;
			_tcscpy(buf, sss);
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);
			CString arch = L"?";
			if (kpiarch[j] == 32) arch = L"x86";
			else if (kpiarch[j] == 64) arch = L"x64";
			_tcscpy(buf, arch); LvItem.iSubItem = 2;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);
			_tcscpy(buf, s);	LvItem.iSubItem = 3;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);
			ss = kpif[j].Right(kpif[j].GetLength() - kpif[j].ReverseFind('\\') - 1);
			m_lc.SetCheck(j, TRUE);
			for (int i = 0; i < cnt; i++) {
				sss = kpifs[i];
				if (ss == sss) {
					if (kpichk[i] == 0) {
						m_lc.SetCheck(j, FALSE);
					}
					else {
						m_lc.SetCheck(j, TRUE);
					}
				}
			}
			}
		}
		catch (...) {
			break;
		}
		try {
			if (status == 1) {
				ss = kpif[j].Right(kpif[j].GetLength() - kpif[j].ReverseFind('\\') - 1);
				for (int i = 0; i < cnt; i++) {
					sss = kpifs[i];
					if (ss == sss) {
						if (kpichk[i] == 0) {
							kpichks[j] = 0;
						}
						else {
							kpichks[j] = 1;
						}
					}
				}
			}
		}
		catch (...) {
				break;
			}

	}
	free(buf);
	RECT r;
	GetWindowRect(&r);
	r.top += 600;
	r.bottom += 600;
	MoveWindow(&r);
}

void CKpilist::Save()
{
	CFile ff;
	TCHAR tc[64];
	CString ss;
	ss = karento2;
	ss += "kpilist.dat";
	if (ff.Open(ss, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
		ff.Write(&kpicnt, sizeof(int));
		for (int i = 0; i < kpicnt; i++) {
			if (status == 1) {
				kpichk[i] = kpichks[i];
			}
			else {
				kpichk[i] = m_lc.GetCheck(i);
			}
			ff.Write(&kpichk[i], sizeof(BOOL));
			_tcscpy(tc, kpif[i].Right(kpif[i].GetLength() - kpif[i].ReverseFind('\\') - 1));
			ff.Write(tc, 64 * sizeof(TCHAR));
		}
		ff.Close();
	}
}

void CKpilist::OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;
}

BOOL CKpilist::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomDialog::PreTranslateMessage(pMsg);
}

void CKpilist::OnBnClickedOk()
{
	CCustomDialog::OnOK();
}

