#pragma once

#include "CCustomControl.h"
#include "resource.h"

// 画面キャプチャのライブ配信設定（モデルレス）。ON中は常時表示。
class CScLiveSettingsDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CScLiveSettingsDlg)
public:
	enum { IDD = IDD_SC_LIVESETTINGS };

	CScLiveSettingsDlg(CWnd* pParent = NULL);
	virtual ~CScLiveSettingsDlg();

	void PersistFieldsToSavedata();
	void ApplyFieldsFromSavedata();
	void SyncYtVisibility();
	void RefreshAuthButtonUi();
	void SetUrlKey(const CString& url, const CString& key);
	void RevealAdvancedCreds();

protected:
	CCustomStatic m_svcLabel;
	CCustomComboBox m_svc;
	CCustomStatic m_privLabel;
	CCustomComboBox m_priv;
	CCustomStandardButton m_auth;
	CCustomStandardButton m_create;
	CCustomStatic m_titleLabel;
	CCustomEdit m_title;
	CCustomStatic m_descLabel;
	CCustomEdit m_desc;
	CCustomStatic m_urlLabel;
	CCustomEdit m_url;
	CCustomStatic m_keyLabel;
	CCustomEdit m_key;
	CCustomCheckBox m_adv;
	CCustomStatic m_cidLabel;
	CCustomEdit m_cid;
	CCustomStatic m_csecLabel;
	CCustomEdit m_csec;
	CCustomStatic m_hint;
	CCustomStandardButton m_close;
	CToolTipCtrl m_tooltip;
	BOOL m_closingFromOwner; // SC 側から閉じるときはチェック連動しない

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnCancel();
	virtual void OnOK();

	afx_msg void OnDestroy();
	afx_msg void OnCbnSelchangeSvc();
	afx_msg void OnCbnSelchangePriv();
	afx_msg void OnBnClickedAuth();
	afx_msg void OnBnClickedCreate();
	afx_msg void OnBnClickedAdv();
	afx_msg void OnEnChangeField();
	DECLARE_MESSAGE_MAP()

	friend void CloseScLiveSettingsIfOpen();
	friend void CloseScLiveSettingsFromOwner();
};

void OpenScLiveSettingsModeless(CWnd* parent);
void CloseScLiveSettingsIfOpen();
void CloseScLiveSettingsFromOwner();
void SyncScLiveSettingsIfOpen();
BOOL IsScLiveSettingsOpen();
HWND GetScLiveSettingsHwnd();
void ScLiveSettingsApplyUrlKey(const CString& url, const CString& key);
void ScLiveSettingsRevealAdvancedCreds();
void ScLiveSettingsSetStreamingUi(BOOL streaming); // 配信中: キャプチャ除外（画面には残す）
void ScLiveSettingsSetStatusText(const CString& text);

BOOL ScLiveRunOAuth(CWnd* owner, CString& errOut);
BOOL ScLiveRunCreateBroadcast(CWnd* owner, CString& errOut);
BOOL ScLiveHaveOAuthClientCreds();
BOOL ScLiveIsLoggedIn();
