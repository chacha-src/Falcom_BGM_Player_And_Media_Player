#pragma once
#include "afxcmn.h"
#include "ListCtrlA.h"
#include "CCustomControl.h"

// KPI 一覧用リストビュー（行ツールチップ：パス／Ver／CPU／拡張子）
class CKpiListCtrl : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CKpiListCtrl)
public:
	CKpiListCtrl() = default;
protected:
	void BuildToolTipText(int row, int col, CString& out) override;
	DECLARE_MESSAGE_MAP()
};

// 情報ペイン。値が長い行だけツールチップ（設定は下のヘルプ欄で足りる）
class CKpiInfoList : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CKpiInfoList)
public:
	CKpiInfoList() = default;
protected:
	void BuildToolTipText(int row, int col, CString& out) override;
	DECLARE_MESSAGE_MAP()
};

// 開いているプラグイン一覧。無ければ NULL。
HWND KpiListFind();

class CKpilist;
// 設定リスト。値の列は常時エディット（CCustomListCtrl::SetLiveEditColumn）。
class CKpiCfgList : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CKpiCfgList)
public:
	CKpiCfgList() = default;
protected:
	BOOL WantLiveEditRow(int row) override;
	void OnLiveEditText(int row, LPCTSTR text) override;
};

// 設定グリッドの特別行。Winamp の Config を開くだけでレジストリには書かない。
enum { KPI_ROW_WINUI = 100 };

// CKpilist ダイアログ

class CKpilist : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CKpilist)

public:
	CKpilist(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CKpilist();
	void Init();
	void Save();
	int status;
// ダイアログ データ
	enum { IDD = IDD_KPI };
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	CToolTipCtrl m_tooltip;

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CKpiListCtrl m_lc;
	CKpiInfoList m_info;
	CKpiCfgList m_cfg;
	CCustomEdit m_extFilter;
	CCustomEdit m_cfgHelp;
	CCustomStatic m_extFilterLbl;
	CCustomStatic m_infoLbl;
	CCustomStatic m_cfgLbl;
	afx_msg void OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnItemchangedCfg(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnCfgDblClk(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnCfgRClick(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnInfoRClick(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnEnChangeExtFilter();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnDestroy();
	CCustomStandardButton m_okdummy;
	CCustomStandardButton m_help;
	CCustomStatic m_desc;
private:
	// リサイズ時に子コントロールを再配置し、kpi/拡張子 列を自動フィットさせる
	void LayoutControls();
	void LayoutKpiColumns();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	// savedata に記録したウィンドウのサイズ・位置を復元/保存する
	void RestoreSavedPlacement();
	void SaveSavedPlacement();
	// 表示中行のチェック → kpichk[実index] へ吸い上げ(フィルタ再構築前に必須)
	void SyncChecksFromList();
	// 拡張子フィルタに合う行だけリストへ載せ直す。ItemData に実 KPI index を持つ。
	void FillKpiList();
	// 選択プラグインのモジュール情報と KPI 設定を下段へ出す。同じ index なら何もしない。
	void ShowPluginDetail(int idx);
	// 設定行をレジストリ（KpiV5）へ書く。kbsasami.raira は 1 固定。vst は midPlayPrefer と連動。
	void SaveCfgRow(int row);
	friend class CKpiCfgList;
	int m_minW = 0;   // 最小ウィンドウ幅(初期サイズ)
	int m_minH = 0;   // 最小ウィンドウ高さ(初期サイズ)
	BOOL m_bFillingList = FALSE; // Fill 中の LVN_ITEMCHANGED を無視
	BOOL m_bFillingDetail = FALSE;
	int m_detailIdx = -1;
	int m_inN = 0;
	int m_cfgN = 0;
	wchar_t m_inName[32][48];
	wchar_t m_inVal[32][512];
	int m_cfgType[96];
	BYTE m_cfgLock[96];
	wchar_t m_cfgSec[96][96];
	wchar_t m_cfgKey[96][96];
	wchar_t m_cfgDesc[96][192];
	wchar_t m_cfgVal[96][512];
	wchar_t m_cfgHelpTxt[96][768];
	wchar_t m_cfgList[96][512];
	wchar_t m_cfgDef[96][256];
};
