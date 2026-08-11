#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include <atlimage.h>

class CPfPanel : public CCustomStatic
{
public:
	CPfPanel():m_alpha(255){}
	BOOL SetFirst(LPCTSTR path);
	BOOL BeginNext(LPCTSTR path);
	void SetBlend(int alpha){m_alpha=max(0,min(255,alpha));Invalidate(FALSE);}
	void CommitNext();
protected:
	CImage m_old,m_next;
	int m_alpha;
	void DrawFit(CDC& dc,CImage& im,const CRect& r);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*){return TRUE;}
	DECLARE_MESSAGE_MAP()
};

class CPhotoFrameDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CPhotoFrameDlg)
public:
	CPhotoFrameDlg(CWnd* p=NULL);
	virtual ~CPhotoFrameDlg();
	enum { IDD=IDD_PHOTOFRAME };
protected:
	virtual void DoDataExchange(CDataExchange*);
	virtual BOOL PreTranslateMessage(MSG*);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	void EnumerateImages();
	void PersistUi();
	void StartSlides();
	void StopSlides();
	void AdvanceSlide();
	void ApplyTopMost();
	void ApplyBgm();
	void LayoutHelpBtn();
	void LayoutAll();
	void ShowHelpSheet();
public:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnBrowse();
	afx_msg void OnStart();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnChanged();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT,int,int);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();

	CCustomStandardButton m_help,m_browse,m_start,m_close;
	CCustomStatic m_folderL,m_intervalL,m_status;
	CCustomEdit m_folder;
	CCustomComboBox m_interval;
	CCustomCheckBox m_shuffle,m_topmost,m_bgm;
	CPfPanel m_panel;
	CToolTipCtrl m_tooltip;
	enum { PF_IMAGE_MAX=512,PF_SLIDE_TIMER=94,PF_FADE_TIMER=95 };
	TCHAR m_paths[PF_IMAGE_MAX][1024];
	int m_count,m_index,m_running,m_fading,m_fade;
	DWORD m_rng;
};
void OpenPhotoFrameModeless(CWnd*);
void ClosePhotoFrameIfOpen();
