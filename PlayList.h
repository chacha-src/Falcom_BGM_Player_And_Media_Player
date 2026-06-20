#pragma once
#include "afxwin.h"
#include "BtnST.h"
#include "afxcmn.h"
// CPlayList ダイアログ

struct playlistdata{
	TCHAR name[1024];
	TCHAR art[1024];
	TCHAR alb[1024];
	TCHAR fol[1024];
	int sub;
	int loop1;
	int loop2;
	int ret2;
	int time;
	int res2;
};

#include "ListCtrlA.h"
#include "CCustomControl.h"

class CPlayList : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CPlayList)

public:
	CPlayList(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CPlayList();
	CWnd* m_pParent;
// ダイアログ データ
	enum { IDD = IDD_PLAYLIST };
	CImageList il;
	playlistdata0 *pc;
	void OnList();
	int nnn;
	int pnt,pnt1;
	int playcnt;

	void SIcon(int i);
	void SIconTimer(int i);
	int Add(CString name,int sub,int loop1,int loop2,CString art,CString alb,CString fol,int ret,int time,BOOL f=TRUE,BOOL ff=TRUE);
	void Del();
	void Load();
	void Save();
	int chk(CString name,int sub,CString art,CString fol,int ret);
	void Fol(CString fname);
	void plug(CString ff,KMPMODULE *mod);
	void plugs(CString ext1,playlistdata *p,TCHAR* kpi, BYTE& kv);
	void Get(int i);

	void OnDrag(int x,int y);
	void OnEndDrag();
	void OnXCHG(int i,int j);
	int m_lDragTopItem = 0;
	int m_lDragTopItemt = 0;
	HIMAGELIST  m_hDragImage = 0;
	BOOL w_flg;
	CString GetModulePath();
	void loadplaylistname();

	CBrush m_brDlg;

	HICON m_hIcon;
	void RefreshNavControls();
	void ScheduleRefreshNavControls();
protected:
	afx_msg LRESULT OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam);
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	CToolTipCtrl m_tooltip;

	DECLARE_MESSAGE_MAP()
public:

	CString UTF8toSJIS(const char* a);
	CString UTF8toUNI(const TCHAR* a);

	virtual BOOL OnInitDialog();
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CButtonST m_lsup;
	CButtonST m_lup;
	CButtonST m_lsdown;
	CButtonST m_ldown;
	afx_msg void OnNcDestroy();
	virtual BOOL DestroyWindow();
	afx_msg int Create(CWnd *pWnd);
	afx_msg void OnClose();
	afx_msg void OnBnClickedOk();
	CCustomListCtrl m_lc;
	afx_msg void OnUP();
	afx_msg void OnSUP();
	afx_msg void OnSDOWN();
	afx_msg void OnDOWN();
	afx_msg void OnLvnKeydownList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnNMDblclkList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
#if WIN64
	afx_msg void OnTimer(UINT_PTR nIDEvent);
#else
	afx_msg void OnTimer(UINT nIDEvent);
#endif
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	CCustomEdit m_e;
	CCustomCheckBox m_renzoku;
	CCustomCheckBox m_loop;
	afx_msg void OnBnClickedCheck4();
	afx_msg void OnBnClickedCheck1();
	CCustomCheckBox m_tool;
	afx_msg void OnLvnBegindragList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLvnGetdispinfoList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnNMRclickList1(NMHDR *pNMHDR, LRESULT *pResult);
	CCustomCheckBox m_saisyo;
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnPop32787();
	afx_msg void OnPopWavExport();
	CCustomEdit m_find;
	afx_msg void OnFindUp();
	afx_msg void OnFindDown();
	CButtonST m_findup;
	CButtonST m_finddown;
	CCustomCheckBox m_savecheck;
	CCustomCheckBox m_save_mp3;
	CCustomCheckBox m_save_kpi;
	afx_msg void OnBnClickedCheck6mp3();
	afx_msg void OnBnClickedCheck7dshow();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg BOOL OnNcActivate(BOOL bActive);
	CCustomComboBox m_listchange;
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnBnClickedButton3();
	afx_msg void OnBnClickedPlaydelete();
	afx_msg void OnBnClickedPianoroll();
	CCustomStandardButton m_namechage;
	CCustomStandardButton m_listdelete;
	CCustomStandardButton m_pianorollBtn;
	CFont m_fontList;
};
