#pragma once

#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include <mmsystem.h>

class CVstHostDlg;

class CVstWireCtrl : public CStatic
{
	DECLARE_DYNAMIC(CVstWireCtrl)
public:
	CVstWireCtrl();
	virtual ~CVstWireCtrl();

	void SetOwner(CVstHostDlg* owner) { m_owner = owner; }
	void SetAeroMode(BOOL b) { m_bAeroMode = b; if (GetSafeHwnd()) Invalidate(FALSE); }
	void SetPlugins(const CString* names, const int* scanIndices, int count);
	void SetSlots(const int* scanIndices);
	void GetSlots(int* scanIndices) const;
	void ClearSlot(int slot);
	int HitSlot(CPoint pt) const;
	void PaintToDC(CDC& dc);

protected:
	enum { MAX_VISIBLE_PLUGINS = 100, PART_COUNT = 32 };
	CVstHostDlg* m_owner;
	BOOL m_bAeroMode;
	CString m_names[MAX_VISIBLE_PLUGINS];
	int m_scanIndices[MAX_VISIBLE_PLUGINS];
	int m_pluginCount;
	int m_slots[PART_COUNT];
	BOOL m_dragging;
	int m_dragScanIndex;
	CPoint m_dragPt;
	int m_hoverPlugin;
	int m_hoverSlot;
	BOOL m_trackLeave;

	CRect PaletteRect(int i) const;
	CRect SlotRect(int i) const;
	int HitPalette(CPoint pt) const;
	void NotifyChanged(int slot);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
};

class CVstHostDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CVstHostDlg)
	friend class CVstWireCtrl;
public:
	CVstHostDlg(CWnd* pParent = NULL);
	virtual ~CVstHostDlg();
	enum { IDD = IDD_VSTHOST };

	void OnWireChanged(int slot);
	CString PluginName(int scanIndex) const;

	struct Preset {
		wchar_t name[64];
		int midiIn[3];
		int outDev;
		int partPluginIndex[32];
		wchar_t path[32][520];
		BYTE isVst3[32];
	};

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnCancel();
	virtual void OnOK();

	void LayoutHelpBtn();
	void LayoutChildren(int cx, int cy);
	void FillDevices();
	void RebuildPluginList();
	void RefreshPresetCombo(int select);
	void CaptureCurrent(Preset& p, LPCTSTR name);
	void ApplyPreset(int index);
	void LoadPresets();
	BOOL SavePresets();
	CString DataPath() const;
	BOOL PromptName(CString& name, LPCTSTR title);
	void StartMidi();
	void StopMidi();
	BOOL StartAudio();
	void StopAudio();
	void RestartIo();
	void ShowHelpSheet();
	void SetStatus(LPCTSTR text);
	static void CALLBACK MidiInProc(HMIDIIN hmi, UINT msg, DWORD_PTR instance,
		DWORD_PTR param1, DWORD_PTR param2);
	static UINT __stdcall AudioThreadProc(void* p);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPresetSelChange();
	afx_msg void OnPluginFilterChange();
	afx_msg void OnRename();
	afx_msg void OnDelete();
	afx_msg void OnSave();
	afx_msg void OnRescan();
	afx_msg void OnHelp();
	afx_msg void OnCloseButton();
	afx_msg void OnDeviceChange();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnMidiShort(WPARAM wParam, LPARAM lParam);

	CCustomComboBox m_preset;
	CCustomComboBox m_midiIn[3];
	CCustomComboBox m_speakerOut;
	CCustomComboBox m_pluginFilter;
	CCustomStandardButton m_help;
	CCustomStandardButton m_close;
	CCustomStandardButton m_rescan;
	CCustomStandardButton m_rename;
	CCustomStandardButton m_del;
	CCustomStandardButton m_save;
	CVstWireCtrl m_wire;
	CToolTipCtrl m_tooltip;

	Preset m_presets[100];
	int m_presetCount;
	int m_slots[32];
	HMIDIIN m_midiHandles[3];
	HWAVEOUT m_waveOut;
	HANDLE m_audioEvent;
	HANDLE m_audioStop;
	HANDLE m_audioThread;
	volatile LONG m_audioRunning;
};

extern CVstHostDlg* g_vstHostDlg;
void OpenVstHostModeless(CWnd* parent);

