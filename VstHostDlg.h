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
	// Repaints only the parts whose incoming MIDI changed.
	void RefreshActivity();

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
	int m_actLevel[PART_COUNT];
	CString m_actText[PART_COUNT];
	CString m_actNotes[PART_COUNT];
	unsigned m_actMask[PART_COUNT][4];
	// Slot buttons: 0 = open the plug-in editor, 1 = part/program menu.
	enum { SLOT_BTN_EDIT = 0, SLOT_BTN_MENU = 1 };
	int m_pressSlot;
	int m_pressBtn;
	int m_hintSlot;
	int m_hintBtn;

	CRect PaletteRect(int i) const;
	CRect SlotRect(int i) const;
	// The whole grid cell, including the empty space under the slot bar: it is
	// the drop target and carries the held-note readout.
	CRect SlotCellRect(int i) const;
	CRect SlotBtnRect(int i, int which) const;
	int HitSlotBtn(CPoint pt, int* outBtn) const;
	int HitPalette(CPoint pt) const;
	void NotifyChanged(int slot);
	int CoveringMulti(int slot) const;
	void ShowSlotMenu(int slot, CPoint screenPt);

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
	friend void VstHostOnLiveListChanged();
public:
	CVstHostDlg(CWnd* pParent = NULL);
	virtual ~CVstHostDlg();
	enum { IDD = IDD_VSTHOST };

	void OnWireChanged(int slot);
	CString PluginName(int scanIndex) const;
	BOOL PluginIsMulti(int scanIndex) const;
	void PartPluginName(int part0to31, wchar_t* out, int outChars) const;

	struct Preset {
		wchar_t name[64];
		int midiIn[3];
		int outDev;
		int partPluginIndex[32];
		wchar_t path[32][520];
		BYTE isVst3[32];
		int midiThru; // 0=off 1=on。VWR1 は midiIn[2]==-2 がスルー
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
	void BindThruSong();
	void StopWav();
	BOOL StartAudio();
	void StopAudio();
	void RestartIo();
	void ShowHelpSheet();
	void SetStatus(LPCTSTR text);
	// PC keyboard as a one-row MIDI keyboard (Z=C4…). Skips Edit/Combo focus.
	int PcKeyToNote(UINT vk) const;
	void PcSendShort(DWORD msg);
	void PcKeyReleaseAll();
	BOOL PcFocusBlocksKeys() const;
	BOOL HandlePcKeyboardMidi(MSG* msg);
	static void CALLBACK MidiInProc(HMIDIIN hmi, UINT msg, DWORD_PTR instance,
		DWORD_PTR param1, DWORD_PTR param2);
	static UINT __stdcall AudioThreadProc(void* p);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnPresetSelChange();
	afx_msg void OnPluginFilterChange();
	afx_msg void OnRename();
	afx_msg void OnDelete();
	afx_msg void OnSave();
	afx_msg void OnWavClick();
	afx_msg void OnRescan();
	afx_msg void OnHelp();
	afx_msg void OnCloseButton();
	afx_msg void OnDeviceChange();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	void ApplyVolUi();

	CCustomComboBox m_preset;
	CCustomComboBox m_midiIn[3];
	CCustomComboBox m_thru;
	CCustomComboBox m_speakerOut;
	CCustomComboBox m_pluginFilter;
	CCustomStandardButton m_help;
	CCustomStandardButton m_close;
	CCustomStandardButton m_rescan;
	CCustomStandardButton m_rename;
	CCustomStandardButton m_del;
	CCustomStandardButton m_save;
	CCustomStandardButton m_wav;
	CVstWireCtrl m_wire;
	CToolTipCtrl m_tooltip;
	enum { LABEL_COUNT = 8 };
	CStatic m_labels[LABEL_COUNT];
	CStatic m_monitor;
	CStatic m_volPct;
	CCustomSliderCtrl m_vol;
	volatile LONG m_volLevel;
	CString m_monitorText;

	Preset m_presets[100];
	int m_presetCount;
	int m_slots[32];
	HMIDIIN m_midiHandles[3];
	// Per opened hardware handle (slots 0..2): bit0 = parts 1-16, bit1 = 17-32.
	BYTE m_midiDestMask[3];
	// 0xFF = no Super-MPU F5 yet; 0/1 = last F5 cable (overrides dest for shorts).
	BYTE m_midiF5Port[3];
	HWAVEOUT m_waveOut;
	HANDLE m_audioEvent;
	HANDLE m_audioStop;
	HANDLE m_audioThread;
	volatile LONG m_audioRunning;
	HANDLE m_wavFile;
	volatile LONG m_wavOn;
	LONG m_wavBytes;
	CRITICAL_SECTION m_wavLock;
	// vk -> held MIDI note+1 (0 = not held). Same path as MIDI In port 0.
	BYTE m_pcHeldNote[256];
};

extern CVstHostDlg* g_vstHostDlg;
void OpenVstHostModeless(CWnd* parent);

