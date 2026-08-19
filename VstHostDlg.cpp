#include "stdafx.h"
#include "ogg.h"
#include "VstHostDlg.h"
#include "VstMidiEngine.h"
#include "CCustomPopupMenu.h"
#include <process.h>
#include <math.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace {

enum {
	WM_VST_MIDI_SHORT = WM_APP + 741,
	IDC_VST_FILTER = 0x7e10,
	ID_VST_POP_RESCAN = 0xe710,
	ID_VST_POP_CLEAR = 0xe711,
	ID_VST_POP_SAVE = 0xe712,
	VST_AUDIO_FRAMES = 512,
	VST_AUDIO_BUFFERS = 4
};

const DWORD VST_WIRE_MAGIC = 0x31525756; // "VWR1"

struct VstWireFile {
	DWORD magic;
	DWORD count;
	CVstHostDlg::Preset presets[100];
};

class CVstNameDlg : public CDialog
{
public:
	CVstNameDlg(CWnd* parent, LPCTSTR title, LPCTSTR value)
		: CDialog(IDD_VST_WAIT, parent), m_title(title), m_value(value) {}
	CString m_title, m_value;
	CEdit m_edit;
	CButton m_ok, m_cancel;
	BOOL OnInitDialog() override {
		CDialog::OnInitDialog();
		SetWindowText(m_title);
		if (CWnd* t = GetDlgItem(IDC_VST_WAIT_TXT))
			t->SetWindowText(LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم",
				L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"));
		CRect rc; GetClientRect(&rc);
		m_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
			CRect(10, 28, rc.right - 10, 50), this, 1);
		m_ok.Create(LL14(L"決定", L"OK", L"OK", L"OK", L"Aceptar", L"확인", L"确定", L"موافق",
			L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			CRect(rc.right - 132, 58, rc.right - 72, 80), this, IDOK);
		m_cancel.Create(LL14(L"取消", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء",
			L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			CRect(rc.right - 68, 58, rc.right - 8, 80), this, IDCANCEL);
		m_edit.SetFont(GetFont()); m_ok.SetFont(GetFont()); m_cancel.SetFont(GetFont());
		m_edit.SetWindowText(m_value);
		m_edit.SetSel(0, -1);
		m_edit.SetFocus();
		return FALSE;
	}
	void OnOK() override {
		m_edit.GetWindowText(m_value);
		m_value.Trim();
		if (m_value.IsEmpty()) {
			MessageBeep(MB_ICONWARNING);
			m_edit.SetFocus();
			return;
		}
		CDialog::OnOK();
	}
};

class CVstHelpDlg : public CDialog
{
public:
	CVstHelpDlg(CWnd* parent) : CDialog(IDD_VST_HELP, parent) {}
	BOOL OnInitDialog() override {
		CDialog::OnInitDialog();
		SetWindowText(LL14(L"VSTホスト ガイド", L"VST Host Guide", L"Guide hôte VST", L"Guida host VST",
			L"Guía host VST", L"VST 호스트 가이드", L"VST 主机指南", L"دليل مضيف VST",
			L"Руководство VST", L"VST-Host-Anleitung", L"Guia do host VST", L"VST-hostgids",
			L"Przewodnik hosta VST", L"VST ana bilgisayar kılavuzu"));
		if (CWnd* ok = GetDlgItem(IDOK))
			ok->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
				L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
		return TRUE;
	}
	afx_msg BOOL OnEraseBkgnd(CDC* dc) {
		CRect r; GetClientRect(&r); dc->FillSolidRect(r, RGB(248, 248, 252)); return TRUE;
	}
	afx_msg void OnPaint() {
		CPaintDC pdc(this);
		CCC_GdiHelpPaint hp;
		if (!CCC_GdiHelpBeginPaint(this, pdc, hp)) return;
		CDC& dc = hp.mem;
		dc.SetBkMode(TRANSPARENT);
		CFont* old = dc.SelectObject(GetFont());
		int y = 8, x = 12;
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, LL14(L"VSTホスト", L"VST Host", L"Hôte VST", L"Host VST", L"Host VST", L"VST 호스트",
			L"VST 主机", L"مضيف VST", L"VST-хост", L"VST-Host", L"Host VST", L"VST-host", L"Host VST", L"VST ana bilgisayarı"));
		y += 24;
		y = CCC_GdiHelpDrawSoftDemoPair(dc, x, y, hp.rc.Width() - x * 2, 110, CCC_HELPDEMO_KGENERIC);
		dc.SetTextColor(RGB(48, 48, 64));
		const CString lines[] = {
			LL14(L"・左のプラグインを右の Part 1～32 へドラッグします。", L"· Drag a plug-in from the left to Parts 1–32.", L"· Glissez un plug-in vers les parties 1–32.", L"· Trascina un plug-in nelle parti 1–32.", L"· Arrastre un plug-in a las partes 1–32.", L"· 왼쪽 플러그인을 파트 1~32로 드래그합니다.", L"· 将左侧插件拖到声部 1–32。", L"· اسحب إضافة إلى الأجزاء 1–32.", L"· Перетащите плагин в партии 1–32.", L"· Plugin links auf Part 1–32 ziehen.", L"· Arraste um plug-in para as partes 1–32.", L"· Sleep een plug-in naar partijen 1–32.", L"· Przeciągnij wtyczkę do partii 1–32.", L"· Eklentiyi Bölüm 1–32'ye sürükleyin."),
			LL14(L"・MIDI入力は最大3台。同じパートへリアルタイム送信します。", L"· Up to three MIDI inputs feed the live parts.", L"· Trois entrées MIDI maximum alimentent les parties.", L"· Fino a tre ingressi MIDI alimentano le parti.", L"· Hasta tres entradas MIDI alimentan las partes.", L"· 최대 3개의 MIDI 입력을 실시간 파트로 보냅니다.", L"· 最多三个 MIDI 输入发送到实时声部。", L"· حتى ثلاثة مداخل MIDI للأجزاء الحية.", L"· До трёх MIDI-входов подаются на партии.", L"· Bis zu drei MIDI-Eingänge speisen die Parts.", L"· Até três entradas MIDI alimentam as partes.", L"· Maximaal drie MIDI-ingangen voeden de partijen.", L"· Do trzech wejść MIDI zasila partie.", L"· En fazla üç MIDI girişi canlı bölümleri besler."),
			LL14(L"・配線とデバイス設定はプリセットへ保存できます。", L"· Wiring and device choices are stored in presets.", L"· Le câblage et les périphériques sont enregistrés.", L"· Cablaggio e dispositivi sono salvati nei preset.", L"· El cableado y los dispositivos se guardan.", L"· 배선과 장치 선택은 프리셋에 저장됩니다.", L"· 连线和设备选择可保存到预设。", L"· تُحفظ التوصيلات والأجهزة في الإعدادات.", L"· Схема и устройства сохраняются в пресетах.", L"· Verdrahtung und Geräte werden im Preset gespeichert.", L"· Ligações e dispositivos são guardados.", L"· Bedrading en apparaten worden opgeslagen.", L"· Okablowanie i urządzenia zapisują się w presetach.", L"· Bağlantılar ve aygıtlar ön ayarlara kaydedilir."),
			LL14(L"・右クリックで再スキャン、スロット解除、保存ができます。", L"· Right-click to rescan, clear a slot, or save.", L"· Clic droit: rescanner, effacer ou enregistrer.", L"· Clic destro: scansione, azzera o salva.", L"· Clic derecho: reescanear, borrar o guardar.", L"· 우클릭으로 재검색, 슬롯 해제, 저장합니다.", L"· 右键可重新扫描、清除插槽或保存。", L"· انقر يميناً للمسح أو الإزالة أو الحفظ.", L"· ПКМ: сканирование, очистка или сохранение.", L"· Rechtsklick: scannen, Slot leeren oder speichern.", L"· Clique direito: procurar, limpar ou guardar.", L"· Rechtsklik: scannen, wissen of opslaan.", L"· PPM: skanowanie, czyszczenie lub zapis.", L"· Sağ tık: tara, slotu temizle veya kaydet."),
			LL14(L"・SOUND Canvas VA / SGP2 等のマルチは1スロットで16ch。MIDIチャンネルはそのまま送られます。", L"· Multi-timbral plugs (SOUND Canvas VA / SGP2) take one slot for 16 channels.", L"· Les multi (SOUND Canvas VA / SGP2) utilisent 1 slot pour 16 canaux.", L"· I multi (SOUND Canvas VA / SGP2) usano 1 slot per 16 canali.", L"· Los multi (SOUND Canvas VA / SGP2) usan 1 ranura para 16 canales.", L"· SOUND Canvas VA/SGP2 멀티는 1슬롯으로 16ch.", L"· SOUND Canvas VA/SGP2 等多音色占1槽覆盖16声道。", L"· الآلات المتعددة (SOUND Canvas VA/SGP2) تشغل فتحة واحدة لـ16 قناة.", L"· Мульти (SOUND Canvas VA/SGP2) — один слот на 16 каналов.", L"· Multi (SOUND Canvas VA/SGP2): ein Slot für 16 Kanäle.", L"· Multi (SOUND Canvas VA/SGP2) usam 1 slot para 16 canais.", L"· Multi (SOUND Canvas VA/SGP2): één slot voor 16 kanalen.", L"· Multi (SOUND Canvas VA/SGP2): jeden slot na 16 kanałów.", L"· Multi (SOUND Canvas VA/SGP2): 16 kanal için tek slot.")
		};
		for (int i = 0; i < (int)_countof(lines); ++i) {
			CRect tr(x, y, hp.rc.right - x, y + 38);
			dc.DrawText(lines[i], &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
			y += 38;
		}
		dc.SelectObject(old);
		CCC_GdiHelpEndPaint(hp);
	}
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CVstHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

} // namespace

IMPLEMENT_DYNAMIC(CVstWireCtrl, CStatic)

CVstWireCtrl::CVstWireCtrl()
	: m_owner(NULL), m_bAeroMode(FALSE), m_pluginCount(0), m_dragging(FALSE),
	  m_dragScanIndex(-1), m_hoverPlugin(-1), m_hoverSlot(-1), m_trackLeave(FALSE)
{
	memset(m_scanIndices, -1, sizeof(m_scanIndices));
	memset(m_slots, -1, sizeof(m_slots));
}

CVstWireCtrl::~CVstWireCtrl() {}

BEGIN_MESSAGE_MAP(CVstWireCtrl, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_RBUTTONUP()
END_MESSAGE_MAP()

void CVstWireCtrl::SetPlugins(const CString* names, const int* indices, int count)
{
	if (count < 0) count = 0;
	if (count > MAX_VISIBLE_PLUGINS) count = MAX_VISIBLE_PLUGINS;
	m_pluginCount = count;
	for (int i = 0; i < count; ++i) {
		m_names[i] = names[i];
		m_scanIndices[i] = indices[i];
	}
	Invalidate(FALSE);
}

void CVstWireCtrl::SetSlots(const int* indices)
{
	for (int i = 0; i < PART_COUNT; ++i) m_slots[i] = indices ? indices[i] : -1;
	Invalidate(FALSE);
}

void CVstWireCtrl::GetSlots(int* indices) const
{
	if (indices) memcpy(indices, m_slots, sizeof(m_slots));
}

void CVstWireCtrl::ClearSlot(int slot)
{
	if (slot < 0 || slot >= PART_COUNT) return;
	m_slots[slot] = -1;
	NotifyChanged(slot);
	Invalidate(FALSE);
}

CRect CVstWireCtrl::PaletteRect(int i) const
{
	CRect rc; GetClientRect(&rc);
	const int leftW = max(150, rc.Width() * 38 / 100);
	const int cols = leftW > 300 ? 2 : 1;
	const int row = i / cols, col = i % cols;
	const int w = (leftW - 12 - (cols - 1) * 4) / cols;
	return CRect(6 + col * (w + 4), 27 + row * 24, 6 + col * (w + 4) + w, 49 + row * 24);
}

CRect CVstWireCtrl::SlotRect(int i) const
{
	CRect rc; GetClientRect(&rc);
	const int left = max(156, rc.Width() * 38 / 100) + 8;
	const int cols = 2, gap = 5;
	const int w = max(70, (rc.Width() - left - 10 - gap) / cols);
	const int row = i / cols, col = i % cols;
	const int avail = max(18, (rc.Height() - 32) / 16);
	const int h = min(25, avail - 2);
	return CRect(left + col * (w + gap), 27 + row * avail,
		left + col * (w + gap) + w, 27 + row * avail + h);
}

int CVstWireCtrl::HitPalette(CPoint pt) const
{
	for (int i = 0; i < m_pluginCount; ++i)
		if (PaletteRect(i).PtInRect(pt)) return i;
	return -1;
}

int CVstWireCtrl::HitSlot(CPoint pt) const
{
	for (int i = 0; i < PART_COUNT; ++i)
		if (SlotRect(i).PtInRect(pt)) return i;
	return -1;
}

void CVstWireCtrl::NotifyChanged(int slot)
{
	if (m_owner) m_owner->OnWireChanged(slot);
}

void CVstWireCtrl::PaintToDC(CDC& dc)
{
	CRect rc; GetClientRect(&rc);
	dc.FillSolidRect(rc, RGB(22, 24, 30));
	dc.SetBkMode(TRANSPARENT);
	CFont* old = dc.SelectObject(GetFont());
	dc.SetTextColor(RGB(190, 205, 230));
	dc.TextOut(8, 6, LL14(L"プラグイン", L"Plug-ins", L"Plug-ins", L"Plug-in", L"Plug-ins", L"플러그인", L"插件", L"الإضافات",
		L"Плагины", L"Plug-ins", L"Plug-ins", L"Plug-ins", L"Wtyczki", L"Eklentiler"));
	CRect cr = rc;
	const int split = max(156, rc.Width() * 38 / 100);
	dc.TextOut(split + 10, 6, LL14(L"パート 1～32", L"Parts 1–32", L"Parties 1–32", L"Parti 1–32", L"Partes 1–32",
		L"파트 1~32", L"声部 1–32", L"الأجزاء 1–32", L"Партии 1–32", L"Parts 1–32", L"Partes 1–32",
		L"Partijen 1–32", L"Partie 1–32", L"Bölümler 1–32"));
	dc.FillSolidRect(split + 2, 4, 1, rc.Height() - 8, RGB(75, 88, 112));

	for (int i = 0; i < m_pluginCount; ++i) {
		CRect r = PaletteRect(i);
		if (r.bottom > rc.bottom - 3) break;
		const BOOL hot = i == m_hoverPlugin || (m_dragging && m_scanIndices[i] == m_dragScanIndex);
		dc.FillSolidRect(r, hot ? RGB(55, 85, 130) : RGB(34, 42, 58));
		dc.Draw3dRect(r, hot ? RGB(150, 220, 255) : RGB(95, 130, 190), RGB(16, 20, 27));
		dc.SetTextColor(RGB(230, 236, 248));
		CRect t = r; t.DeflateRect(4, 1);
		dc.DrawText(m_names[i], t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	for (int i = 0; i < PART_COUNT; ++i) {
		CRect r = SlotRect(i);
		const BOOL full = m_slots[i] >= 0;
		const BOOL hot = i == m_hoverSlot;
		dc.FillSolidRect(r, hot ? RGB(48, 68, 94) : (full ? RGB(56, 48, 24) : RGB(34, 37, 46)));
		dc.Draw3dRect(r, hot ? RGB(130, 215, 255) : (full ? RGB(245, 190, 70) : RGB(82, 92, 112)), RGB(16, 18, 23));
		CString s, name;
		if (full && m_owner) name = m_owner->PluginName(m_slots[i]);
		s.Format(L"%02d  %s", i + 1, full ? (LPCTSTR)name : L"—");
		dc.SetTextColor(full ? RGB(255, 238, 180) : RGB(155, 164, 180));
		CRect t = r; t.DeflateRect(4, 1);
		dc.DrawText(s, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	if (m_dragging) {
		CRect g(m_dragPt.x - 80, m_dragPt.y - 10, m_dragPt.x + 80, m_dragPt.y + 10);
		dc.FillSolidRect(g, RGB(72, 58, 24));
		dc.Draw3dRect(g, RGB(255, 210, 90), RGB(28, 24, 14));
		dc.SetTextColor(RGB(255, 240, 190));
		CString n = m_owner ? m_owner->PluginName(m_dragScanIndex) : L"";
		dc.DrawText(n, g, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	dc.Draw3dRect(rc, RGB(72, 80, 96), RGB(15, 17, 22));
	dc.SelectObject(old);
}

void CVstWireCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect r; GetClientRect(&r);
	BP_PAINTPARAMS pp = { sizeof(pp) }; pp.dwFlags = BPPF_ERASE;
	HDC mem = NULL;
	HPAINTBUFFER bp = BeginBufferedPaint(dc, &r, BPBF_TOPDOWNDIB, &pp, &mem);
	if (bp && mem) {
		CDC b; b.Attach(mem); PaintToDC(b); b.Detach();
		BufferedPaintMakeOpaque(bp, &r); EndBufferedPaint(bp, TRUE);
	} else PaintToDC(dc);
}

BOOL CVstWireCtrl::OnEraseBkgnd(CDC*) { return TRUE; }

LRESULT CVstWireCtrl::OnPrintClient(WPARAM wp, LPARAM)
{
	CDC dc; dc.Attach((HDC)wp); PaintToDC(dc); dc.Detach(); return 0;
}

void CVstWireCtrl::OnLButtonDown(UINT flags, CPoint pt)
{
	const int p = HitPalette(pt);
	if (p >= 0) {
		m_dragging = TRUE; m_dragScanIndex = m_scanIndices[p]; m_dragPt = pt;
		SetCapture(); Invalidate(FALSE); return;
	}
	CStatic::OnLButtonDown(flags, pt);
}

void CVstWireCtrl::OnLButtonUp(UINT flags, CPoint pt)
{
	if (m_dragging) {
		ReleaseCapture();
		const int slot = HitSlot(pt);
		m_dragging = FALSE;
		if (slot >= 0 && m_dragScanIndex >= 0) {
			m_slots[slot] = m_dragScanIndex;
			NotifyChanged(slot);
		}
		m_dragScanIndex = -1; Invalidate(FALSE); return;
	}
	CStatic::OnLButtonUp(flags, pt);
}

void CVstWireCtrl::OnMouseMove(UINT flags, CPoint pt)
{
	if (!m_trackLeave) {
		TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
		m_trackLeave = TrackMouseEvent(&t);
	}
	m_dragPt = pt;
	m_hoverPlugin = m_dragging ? -1 : HitPalette(pt);
	m_hoverSlot = HitSlot(pt);
	Invalidate(FALSE);
	CStatic::OnMouseMove(flags, pt);
}

void CVstWireCtrl::OnMouseLeave()
{
	m_trackLeave = FALSE; m_hoverPlugin = m_hoverSlot = -1; Invalidate(FALSE);
}

void CVstWireCtrl::OnRButtonUp(UINT, CPoint pt)
{
	const int slot = HitSlot(pt);
	CCustomPopupMenu menu;
	menu.AddCommand(ID_VST_POP_RESCAN, LL14(L"プラグイン再スキャン", L"Rescan plug-ins", L"Réanalyser les plug-ins", L"Scansiona plug-in",
		L"Reescanear plug-ins", L"플러그인 다시 검색", L"重新扫描插件", L"إعادة مسح الإضافات", L"Пересканировать плагины",
		L"Plug-ins neu scannen", L"Procurar plug-ins", L"Plug-ins opnieuw scannen", L"Przeskanuj wtyczki", L"Eklentileri yeniden tara"));
	menu.AddCommand(ID_VST_POP_CLEAR, LL14(L"このスロットを解除", L"Clear this slot", L"Effacer ce slot", L"Azzera slot",
		L"Borrar ranura", L"이 슬롯 해제", L"清除此插槽", L"مسح هذه الفتحة", L"Очистить слот", L"Slot leeren",
		L"Limpar slot", L"Slot wissen", L"Wyczyść slot", L"Slotu temizle"), NULL, slot >= 0 && m_slots[slot] >= 0);
	menu.AddCommand(ID_VST_POP_SAVE, LL14(L"プリセットを保存", L"Save preset", L"Enregistrer", L"Salva preset", L"Guardar preset",
		L"프리셋 저장", L"保存预设", L"حفظ الإعداد", L"Сохранить пресет", L"Preset speichern", L"Guardar predefinição",
		L"Preset opslaan", L"Zapisz preset", L"Ön ayarı kaydet"));
	ClientToScreen(&pt);
	UINT cmd = menu.Track(pt, m_owner ? (CWnd*)m_owner : GetParent());
	if (!m_owner) return;
	if (cmd == ID_VST_POP_RESCAN) m_owner->OnRescan();
	else if (cmd == ID_VST_POP_CLEAR && slot >= 0) ClearSlot(slot);
	else if (cmd == ID_VST_POP_SAVE) m_owner->OnSave();
}

CVstHostDlg* g_vstHostDlg = NULL;
IMPLEMENT_DYNAMIC(CVstHostDlg, CCustomBlurDialogBase)

CVstHostDlg::CVstHostDlg(CWnd* parent)
	: CCustomBlurDialogBase(IDD, parent), m_presetCount(0), m_waveOut(NULL),
	  m_audioEvent(NULL), m_audioStop(NULL), m_audioThread(NULL), m_audioRunning(0)
{
	memset(m_presets, 0, sizeof(m_presets));
	memset(m_slots, -1, sizeof(m_slots));
	memset(m_midiHandles, 0, sizeof(m_midiHandles));
}

CVstHostDlg::~CVstHostDlg() {}

void CVstHostDlg::DoDataExchange(CDataExchange* dx)
{
	CCustomBlurDialogBase::DoDataExchange(dx);
	DDX_Control(dx, IDC_VST_PRESET, m_preset);
	DDX_Control(dx, IDC_VST_MIDI1, m_midiIn[0]);
	DDX_Control(dx, IDC_VST_MIDI2, m_midiIn[1]);
	DDX_Control(dx, IDC_VST_MIDI3, m_midiIn[2]);
	DDX_Control(dx, IDC_VST_OUT, m_speakerOut);
	DDX_Control(dx, IDC_VST_HELP, m_help);
	DDX_Control(dx, IDC_VST_CLOSE, m_close);
	DDX_Control(dx, IDC_VST_RESCAN, m_rescan);
	DDX_Control(dx, IDC_VST_RENAME, m_rename);
	DDX_Control(dx, IDC_VST_DEL, m_del);
	DDX_Control(dx, IDC_VST_SAVE, m_save);
	DDX_Control(dx, IDC_VST_WIRE, m_wire);
}

BEGIN_MESSAGE_MAP(CVstHostDlg, CCustomBlurDialogBase)
	ON_CBN_SELCHANGE(IDC_VST_PRESET, OnPresetSelChange)
	ON_CONTROL(CBN_EDITCHANGE, IDC_VST_FILTER, OnPluginFilterChange)
	ON_CONTROL(CBN_SELCHANGE, IDC_VST_FILTER, OnPluginFilterChange)
	ON_BN_CLICKED(IDC_VST_RENAME, OnRename)
	ON_BN_CLICKED(IDC_VST_DEL, OnDelete)
	ON_BN_CLICKED(IDC_VST_SAVE, OnSave)
	ON_BN_CLICKED(IDC_VST_RESCAN, OnRescan)
	ON_BN_CLICKED(IDC_VST_HELP, OnHelp)
	ON_BN_CLICKED(IDC_VST_CLOSE, OnCloseButton)
	ON_CBN_SELCHANGE(IDC_VST_MIDI1, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_MIDI2, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_MIDI3, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_OUT, OnDeviceChange)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_VST_MIDI_SHORT, OnMidiShort)
END_MESSAGE_MAP()

BOOL CVstHostDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	SetWindowText(LL14(L"VSTホスト", L"VST Host", L"Hôte VST", L"Host VST", L"Host VST", L"VST 호스트",
		L"VST 主机", L"مضيف VST", L"VST-хост", L"VST-Host", L"Host VST", L"VST-host", L"Host VST", L"VST ana bilgisayarı"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_rescan.SetWindowText(LL14(L"再スキャン", L"Rescan", L"Réanalyser", L"Scansiona", L"Reescanear", L"다시 검색", L"重新扫描",
		L"إعادة المسح", L"Сканировать", L"Neu scannen", L"Procurar", L"Scannen", L"Skanuj", L"Yeniden tara"));
	m_rename.SetWindowText(LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Renombrar", L"이름 변경", L"重命名",
		L"إعادة تسمية", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmień nazwę", L"Yeniden adlandır"));
	m_del.SetWindowText(LL14(L"削除", L"Delete", L"Supprimer", L"Elimina", L"Eliminar", L"삭제", L"删除", L"حذف",
		L"Удалить", L"Löschen", L"Eliminar", L"Verwijderen", L"Usuń", L"Sil"));
	m_save.SetWindowText(LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ",
		L"Сохранить", L"Speichern", L"Guardar", L"Opslaan", L"Zapisz", L"Kaydet"));

	for (int i = 0; i < 3; ++i) m_midiIn[i].SetAeroMode(FALSE);
	m_preset.SetAeroMode(FALSE);
	m_speakerOut.SetAeroMode(FALSE);
	m_wire.SetOwner(this);
	m_wire.SetAeroMode(FALSE);
	m_wire.ModifyStyle(WS_BORDER, 0, SWP_FRAMECHANGED);
	CRect rc; GetClientRect(&rc);
	m_pluginFilter.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
		CRect(8, 72, 210, 300), this, IDC_VST_FILTER);
	m_pluginFilter.SetFont(GetFont());
	m_pluginFilter.SetAeroMode(FALSE);
	m_pluginFilter.AddString(LL14(L"すべて", L"All plug-ins", L"Tous", L"Tutti", L"Todos", L"모든 플러그인", L"全部插件",
		L"كل الإضافات", L"Все плагины", L"Alle Plug-ins", L"Todos", L"Alle plug-ins", L"Wszystkie", L"Tüm eklentiler"));
	m_pluginFilter.SetCurSel(0);
	LayoutHelpBtn();
	CCC_CaptionLayout(m_hWnd);

	FillDevices();
	LoadPresets();
	VstScanEnsure(m_hWnd);
	RebuildPluginList();
	m_wire.SetSlots(m_slots);

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_wire, LL14(L"プラグインをパートへドラッグ。右クリックで操作", L"Drag plug-ins to parts; right-click for actions",
			L"Glissez les plug-ins; clic droit pour les actions", L"Trascina i plug-in; destro per azioni", L"Arrastre plug-ins; clic derecho para acciones",
			L"플러그인을 파트로 드래그하고 우클릭으로 조작", L"拖动插件到声部；右键操作", L"اسحب الإضافات؛ انقر يميناً للإجراءات",
			L"Перетащите плагины; ПКМ для действий", L"Plug-ins auf Parts ziehen; Rechtsklick für Aktionen", L"Arraste plug-ins; clique direito para ações",
			L"Sleep plug-ins naar partijen; rechtsklik voor acties", L"Przeciągnij wtyczki; PPM otwiera akcje", L"Eklentileri bölümlere sürükleyin; işlemler için sağ tık"));
		m_tooltip.AddTool(&m_save, LL14(L"現在の配線とデバイスをプリセットへ保存", L"Save current wiring and devices as a preset",
			L"Enregistrer câblage et périphériques", L"Salva cablaggio e dispositivi", L"Guardar cableado y dispositivos",
			L"현재 배선과 장치를 프리셋으로 저장", L"将当前连线和设备保存为预设", L"حفظ التوصيلات والأجهزة كإعداد",
			L"Сохранить схему и устройства", L"Aktuelle Verdrahtung und Geräte speichern", L"Guardar ligações e dispositivos",
			L"Huidige bedrading en apparaten opslaan", L"Zapisz okablowanie i urządzenia", L"Bağlantıları ve aygıtları ön ayar olarak kaydet"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 12000);
	}
	StartMidi();
	StartAudio();
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	GetClientRect(&rc);
	LayoutChildren(rc.Width(), rc.Height());
	SetStatus(LL14(L"準備完了", L"Ready", L"Prêt", L"Pronto", L"Listo", L"준비됨", L"就绪", L"جاهز",
		L"Готово", L"Bereit", L"Pronto", L"Gereed", L"Gotowe", L"Hazır"));
	return TRUE;
}

BOOL CVstHostDlg::PreTranslateMessage(MSG* msg)
{
	if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(msg);
	return CCustomBlurDialogBase::PreTranslateMessage(msg);
}

void CVstHostDlg::LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }

void CVstHostDlg::LayoutChildren(int cx, int cy)
{
	if (!GetSafeHwnd() || !m_wire.GetSafeHwnd()) return;
	const int capH = GetCustomCaptionHeight();
	const int pad = 8, rowH = 24, gap = 5;
	const int top = capH + pad;
	const int bottomH = 28;
	int x = pad;
	m_preset.SetWindowPos(NULL, x, top, 142, 220, SWP_NOZORDER); x += 142 + gap;
	m_rename.SetWindowPos(NULL, x, top, 64, rowH, SWP_NOZORDER); x += 64 + gap;
	m_del.SetWindowPos(NULL, x, top, 54, rowH, SWP_NOZORDER); x += 54 + gap;
	m_save.SetWindowPos(NULL, x, top, 58, rowH, SWP_NOZORDER); x += 58 + gap;
	m_rescan.SetWindowPos(NULL, cx - pad - 82, top, 82, rowH, SWP_NOZORDER);
	const int y2 = top + rowH + gap;
	const int comboW = max(90, (cx - pad * 2 - gap * 4) / 5);
	for (int i = 0; i < 3; ++i)
		m_midiIn[i].SetWindowPos(NULL, pad + i * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	m_speakerOut.SetWindowPos(NULL, pad + 3 * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	m_pluginFilter.SetWindowPos(NULL, pad + 4 * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	const int wireTop = y2 + rowH + gap;
	m_wire.SetWindowPos(NULL, pad, wireTop, max(10, cx - pad * 2), max(40, cy - wireTop - bottomH - pad), SWP_NOZORDER);
	if (CWnd* st = GetDlgItem(IDC_VST_STATUS))
		st->SetWindowPos(NULL, pad, cy - bottomH + 4, max(20, cx - 110), 18, SWP_NOZORDER);
	m_close.SetWindowPos(NULL, cx - pad - 82, cy - bottomH, 82, 22, SWP_NOZORDER);
}

void CVstHostDlg::OnSize(UINT type, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(type, cx, cy);
	if (type != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
		LayoutChildren(cx, cy);
	}
}

void CVstHostDlg::FillDevices()
{
	CString none = LL14(L"(なし)", L"(None)", L"(Aucun)", L"(Nessuno)", L"(Ninguno)", L"(없음)", L"(无)", L"(بلا)",
		L"(Нет)", L"(Keine)", L"(Nenhum)", L"(Geen)", L"(Brak)", L"(Yok)");
	for (int p = 0; p < 3; ++p) {
		m_midiIn[p].ResetContent();
		int n = m_midiIn[p].AddString(none); m_midiIn[p].SetItemData(n, (DWORD_PTR)-1);
		for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
			MIDIINCAPS caps = {};
			if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
				n = m_midiIn[p].AddString(caps.szPname); m_midiIn[p].SetItemData(n, i);
			}
		}
		m_midiIn[p].SetCurSel(0);
	}
	m_speakerOut.ResetContent();
	int n = m_speakerOut.AddString(LL14(L"(既定のスピーカー)", L"(Default speaker)", L"(Haut-parleur par défaut)",
		L"(Altoparlante predefinito)", L"(Altavoz predeterminado)", L"(기본 스피커)", L"(默认扬声器)", L"(مكبر الصوت الافتراضي)",
		L"(Динамик по умолчанию)", L"(Standardlautsprecher)", L"(Altifalante predefinido)", L"(Standaardluidspreker)",
		L"(Domyślny głośnik)", L"(Varsayılan hoparlör)"));
	m_speakerOut.SetItemData(n, WAVE_MAPPER);
	for (UINT i = 0; i < waveOutGetNumDevs(); ++i) {
		WAVEOUTCAPS caps = {};
		if (waveOutGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			n = m_speakerOut.AddString(caps.szPname); m_speakerOut.SetItemData(n, i);
		}
	}
	m_speakerOut.SetCurSel(0);
}

CString CVstHostDlg::PluginName(int scanIndex) const
{
	const VstPluginInfo* pi = VstScanGet(scanIndex);
	return pi ? CString(pi->name) : CString(L"?");
}

void CVstHostDlg::RebuildPluginList()
{
	CString filter;
	m_pluginFilter.GetWindowText(filter); filter.MakeLower();
	if (m_pluginFilter.GetCurSel() == 0) filter.Empty();
	CString names[100]; int indices[100]; int count = 0;
	for (int i = 0; i < VstScanGetCount() && count < 100; ++i) {
		const VstPluginInfo* pi = VstScanGet(i);
		if (!pi || !pi->isInstrument) continue;
		CString n(pi->name), low(n); low.MakeLower();
		if (!filter.IsEmpty() && low.Find(filter) < 0) continue;
		names[count] = n;
		if (pi->isMultiTimbral) names[count] = CString(L"[M] ") + n;
		indices[count] = i; count++;
	}
	m_wire.SetPlugins(names, indices, count);
}

CString CVstHostDlg::DataPath() const
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileName(NULL, path, _countof(path));
	wchar_t* slash = wcsrchr(path, L'\\');
	if (slash) slash[1] = 0;
	return CString(path) + L"vstwire.dat";
}

void CVstHostDlg::LoadPresets()
{
	m_presetCount = 0;
	CFile f;
	if (f.Open(DataPath(), CFile::modeRead | CFile::shareDenyWrite)) {
		VstWireFile data = {};
		if (f.Read(&data, sizeof(data)) >= sizeof(DWORD) * 2 && data.magic == VST_WIRE_MAGIC) {
			m_presetCount = min(100, (int)data.count);
			memcpy(m_presets, data.presets, sizeof(Preset) * m_presetCount);
		}
		f.Close();
	}
	RefreshPresetCombo(m_presetCount ? 0 : -1);
	if (m_presetCount) ApplyPreset(0);
}

BOOL CVstHostDlg::SavePresets()
{
	VstWireFile data = {};
	data.magic = VST_WIRE_MAGIC; data.count = m_presetCount;
	memcpy(data.presets, m_presets, sizeof(Preset) * m_presetCount);
	CFile f;
	if (!f.Open(DataPath(), CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive)) return FALSE;
	f.Write(&data, sizeof(DWORD) * 2 + sizeof(Preset) * m_presetCount);
	f.Close(); return TRUE;
}

void CVstHostDlg::RefreshPresetCombo(int select)
{
	m_preset.ResetContent();
	for (int i = 0; i < m_presetCount; ++i) m_preset.AddString(m_presets[i].name);
	if (select >= 0 && select < m_presetCount) m_preset.SetCurSel(select);
}

void CVstHostDlg::CaptureCurrent(Preset& p, LPCTSTR name)
{
	ZeroMemory(&p, sizeof(p));
	wcsncpy_s(p.name, name ? name : L"", _TRUNCATE);
	for (int i = 0; i < 3; ++i) {
		int sel = m_midiIn[i].GetCurSel();
		p.midiIn[i] = sel >= 0 ? (int)m_midiIn[i].GetItemData(sel) : -1;
	}
	int outSel = m_speakerOut.GetCurSel();
	p.outDev = outSel >= 0 ? (int)m_speakerOut.GetItemData(outSel) : WAVE_MAPPER;
	m_wire.GetSlots(m_slots);
	for (int i = 0; i < 32; ++i) {
		p.partPluginIndex[i] = m_slots[i];
		const VstPluginInfo* pi = VstScanGet(m_slots[i]);
		if (pi) {
			wcsncpy_s(p.path[i], pi->path, _TRUNCATE);
			p.isVst3[i] = pi->isVst3 ? 1 : 0;
		}
	}
}

void CVstHostDlg::ApplyPreset(int index)
{
	if (index < 0 || index >= m_presetCount) return;
	const Preset& p = m_presets[index];
	for (int port = 0; port < 3; ++port) {
		int sel = 0;
		for (int i = 0; i < m_midiIn[port].GetCount(); ++i)
			if ((int)m_midiIn[port].GetItemData(i) == p.midiIn[port]) { sel = i; break; }
		m_midiIn[port].SetCurSel(sel);
	}
	int outSel = 0;
	for (int i = 0; i < m_speakerOut.GetCount(); ++i)
		if ((int)m_speakerOut.GetItemData(i) == p.outDev) { outSel = i; break; }
	m_speakerOut.SetCurSel(outSel);
	for (int part = 0; part < 32; ++part) {
		m_slots[part] = -1;
		VstLiveUnloadPart(part + 1);
		if (!p.path[part][0]) continue;
		int scanIndex = -1;
		for (int i = 0; i < VstScanGetCount(); ++i) {
			const VstPluginInfo* pi = VstScanGet(i);
			if (pi && _wcsicmp(pi->path, p.path[part]) == 0) { scanIndex = i; break; }
		}
		if (VstLiveLoadPart(part + 1, p.path[part], p.isVst3[part]) == 0)
			m_slots[part] = scanIndex;
	}
	m_wire.SetSlots(m_slots);
	RestartIo();
}

BOOL CVstHostDlg::PromptName(CString& name, LPCTSTR title)
{
	CVstNameDlg dlg(this, title, name);
	if (dlg.DoModal() != IDOK) return FALSE;
	name = dlg.m_value; return TRUE;
}

void CVstHostDlg::OnPresetSelChange() { ApplyPreset(m_preset.GetCurSel()); }
void CVstHostDlg::OnPluginFilterChange() { RebuildPluginList(); }

void CVstHostDlg::OnRename()
{
	int i = m_preset.GetCurSel();
	if (i < 0 || i >= m_presetCount) return;
	CString name(m_presets[i].name);
	if (!PromptName(name, LL14(L"プリセット名の変更", L"Rename preset", L"Renommer le préréglage", L"Rinomina preset",
		L"Renombrar preset", L"프리셋 이름 변경", L"重命名预设", L"إعادة تسمية الإعداد", L"Переименовать пресет",
		L"Preset umbenennen", L"Renomear predefinição", L"Preset hernoemen", L"Zmień nazwę presetu", L"Ön ayarı yeniden adlandır"))) return;
	wcsncpy_s(m_presets[i].name, name, _TRUNCATE);
	SavePresets(); RefreshPresetCombo(i);
}

void CVstHostDlg::OnDelete()
{
	int i = m_preset.GetCurSel();
	if (i < 0 || i >= m_presetCount) return;
	if (MessageBox(LL14(L"このプリセットを削除しますか？", L"Delete this preset?", L"Supprimer ce préréglage ?", L"Eliminare questo preset?",
		L"¿Eliminar este preset?", L"이 프리셋을 삭제할까요?", L"删除此预设吗？", L"هل تريد حذف هذا الإعداد؟",
		L"Удалить этот пресет?", L"Dieses Preset löschen?", L"Eliminar esta predefinição?", L"Deze preset verwijderen?",
		L"Usunąć ten preset?", L"Bu ön ayar silinsin mi?"), NULL, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
	for (int n = i; n < m_presetCount - 1; ++n) m_presets[n] = m_presets[n + 1];
	m_presetCount--; SavePresets(); RefreshPresetCombo(min(i, m_presetCount - 1));
}

void CVstHostDlg::OnSave()
{
	int i = m_preset.GetCurSel();
	if (i >= 0 && i < m_presetCount) {
		CaptureCurrent(m_presets[i], m_presets[i].name);
		SavePresets();
		SetStatus(LL14(L"プリセットを更新しました", L"Preset updated", L"Préréglage mis à jour", L"Preset aggiornato",
			L"Preset actualizado", L"프리셋을 업데이트했습니다", L"预设已更新", L"تم تحديث الإعداد",
			L"Пресет обновлён", L"Preset aktualisiert", L"Predefinição atualizada", L"Preset bijgewerkt", L"Preset zaktualizowany", L"Ön ayar güncellendi"));
		return;
	}
	if (m_presetCount >= 100) return;
	CString name;
	name.Format(LL14(L"プリセット %d", L"Preset %d", L"Préréglage %d", L"Preset %d", L"Preset %d", L"프리셋 %d",
		L"预设 %d", L"إعداد %d", L"Пресет %d", L"Preset %d", L"Predefinição %d", L"Preset %d", L"Preset %d", L"Ön ayar %d"), m_presetCount + 1);
	if (!PromptName(name, LL14(L"プリセットを保存", L"Save preset", L"Enregistrer le préréglage", L"Salva preset", L"Guardar preset",
		L"프리셋 저장", L"保存预设", L"حفظ الإعداد", L"Сохранить пресет", L"Preset speichern", L"Guardar predefinição",
		L"Preset opslaan", L"Zapisz preset", L"Ön ayarı kaydet"))) return;
	CaptureCurrent(m_presets[m_presetCount], name);
	m_presetCount++; SavePresets(); RefreshPresetCombo(m_presetCount - 1);
}

void CVstHostDlg::OnRescan()
{
	SetStatus(LL14(L"スキャン中…", L"Scanning…", L"Analyse…", L"Scansione…", L"Escaneando…", L"검색 중…", L"扫描中…",
		L"جارٍ المسح…", L"Сканирование…", L"Scannen…", L"A procurar…", L"Scannen…", L"Skanowanie…", L"Taranıyor…"));
	VstScanInvalidate();
	VstScanEnsure(m_hWnd);
	RebuildPluginList();
	SetStatus(LL14(L"スキャン完了", L"Scan complete", L"Analyse terminée", L"Scansione completata", L"Escaneo completo",
		L"검색 완료", L"扫描完成", L"اكتمل المسح", L"Сканирование завершено", L"Scan abgeschlossen", L"Procura concluída",
		L"Scan voltooid", L"Skanowanie zakończone", L"Tarama tamamlandı"));
}

void CVstHostDlg::OnWireChanged(int slot)
{
	m_wire.GetSlots(m_slots);
	if (slot < 0 || slot >= 32) return;
	VstLiveUnloadPart(slot + 1);
	const VstPluginInfo* pi = VstScanGet(m_slots[slot]);
	if (pi && VstLiveLoadPart(slot + 1, pi->path, pi->isVst3) != 0) {
		m_slots[slot] = -1; m_wire.SetSlots(m_slots);
		SetStatus(LL14(L"プラグインを読み込めません", L"Could not load plug-in", L"Impossible de charger le plug-in",
			L"Impossibile caricare il plug-in", L"No se pudo cargar el plug-in", L"플러그인을 불러올 수 없습니다",
			L"无法加载插件", L"تعذر تحميل الإضافة", L"Не удалось загрузить плагин", L"Plugin konnte nicht geladen werden",
			L"Não foi possível carregar o plug-in", L"Kan plug-in niet laden", L"Nie można wczytać wtyczki", L"Eklenti yüklenemedi"));
	}
}

void CALLBACK CVstHostDlg::MidiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR)
{
	if (msg == MIM_DATA) {
		CVstHostDlg* self = (CVstHostDlg*)(instance & ~(DWORD_PTR)3);
		int port = (int)(instance & 3);
		if (self && self->GetSafeHwnd()) self->PostMessage(WM_VST_MIDI_SHORT, port, (LPARAM)(DWORD)p1);
	}
}

void CVstHostDlg::StartMidi()
{
	StopMidi();
	for (int p = 0; p < 3; ++p) {
		int sel = m_midiIn[p].GetCurSel();
		int dev = sel >= 0 ? (int)m_midiIn[p].GetItemData(sel) : -1;
		if (dev >= 0 && midiInOpen(&m_midiHandles[p], dev, (DWORD_PTR)&MidiInProc,
			((DWORD_PTR)this) | p, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
			midiInStart(m_midiHandles[p]);
	}
}

void CVstHostDlg::StopMidi()
{
	for (int i = 0; i < 3; ++i) if (m_midiHandles[i]) {
		midiInStop(m_midiHandles[i]); midiInReset(m_midiHandles[i]); midiInClose(m_midiHandles[i]); m_midiHandles[i] = NULL;
	}
}

LRESULT CVstHostDlg::OnMidiShort(WPARAM port, LPARAM msg)
{
	VstLiveMidiShort((int)port, (DWORD)msg); return 0;
}

UINT __stdcall CVstHostDlg::AudioThreadProc(void* ctx)
{
	CVstHostDlg* self = (CVstHostDlg*)ctx;
	WAVEHDR hdr[VST_AUDIO_BUFFERS] = {};
	short pcm[VST_AUDIO_BUFFERS][VST_AUDIO_FRAMES * 2] = {};
	float L[VST_AUDIO_FRAMES], R[VST_AUDIO_FRAMES];
	for (int b = 0; b < VST_AUDIO_BUFFERS; ++b) {
		hdr[b].lpData = (LPSTR)pcm[b]; hdr[b].dwBufferLength = sizeof(pcm[b]);
		waveOutPrepareHeader(self->m_waveOut, &hdr[b], sizeof(WAVEHDR));
		hdr[b].dwFlags |= WHDR_DONE;
	}
	InterlockedExchange(&self->m_audioRunning, 1);
	while (WaitForSingleObject(self->m_audioStop, 0) != WAIT_OBJECT_0) {
		BOOL queued = FALSE;
		for (int b = 0; b < VST_AUDIO_BUFFERS; ++b) if (hdr[b].dwFlags & WHDR_DONE) {
			ZeroMemory(L, sizeof(L)); ZeroMemory(R, sizeof(R));
			VstLiveRender(L, R, VST_AUDIO_FRAMES);
			for (int i = 0; i < VST_AUDIO_FRAMES; ++i) {
				float l = max(-1.f, min(1.f, L[i])), r = max(-1.f, min(1.f, R[i]));
				pcm[b][i * 2] = (short)(l * 32767.f);
				pcm[b][i * 2 + 1] = (short)(r * 32767.f);
			}
			hdr[b].dwBufferLength = sizeof(pcm[b]);
			hdr[b].dwFlags &= ~WHDR_DONE;
			waveOutWrite(self->m_waveOut, &hdr[b], sizeof(WAVEHDR));
			queued = TRUE;
		}
		if (!queued) WaitForSingleObject(self->m_audioEvent, 30);
	}
	waveOutReset(self->m_waveOut);
	for (int b = 0; b < VST_AUDIO_BUFFERS; ++b)
		waveOutUnprepareHeader(self->m_waveOut, &hdr[b], sizeof(WAVEHDR));
	InterlockedExchange(&self->m_audioRunning, 0);
	return 0;
}

BOOL CVstHostDlg::StartAudio()
{
	StopAudio();
	WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
	m_audioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_audioStop = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!m_audioEvent || !m_audioStop) { StopAudio(); return FALSE; }
	int sel = m_speakerOut.GetCurSel();
	UINT dev = sel >= 0 ? (UINT)m_speakerOut.GetItemData(sel) : WAVE_MAPPER;
	if (waveOutOpen(&m_waveOut, dev, &wf, (DWORD_PTR)m_audioEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		StopAudio(); return FALSE;
	}
	unsigned tid = 0;
	m_audioThread = (HANDLE)_beginthreadex(NULL, 0, AudioThreadProc, this, 0, &tid);
	return m_audioThread != NULL;
}

void CVstHostDlg::StopAudio()
{
	if (m_audioStop) SetEvent(m_audioStop);
	if (m_waveOut) waveOutReset(m_waveOut);
	if (m_audioThread) { WaitForSingleObject(m_audioThread, 3000); CloseHandle(m_audioThread); m_audioThread = NULL; }
	if (m_waveOut) { waveOutClose(m_waveOut); m_waveOut = NULL; }
	if (m_audioEvent) { CloseHandle(m_audioEvent); m_audioEvent = NULL; }
	if (m_audioStop) { CloseHandle(m_audioStop); m_audioStop = NULL; }
	InterlockedExchange(&m_audioRunning, 0);
}

void CVstHostDlg::RestartIo() { StartMidi(); StartAudio(); }
void CVstHostDlg::OnDeviceChange() { RestartIo(); }

void CVstHostDlg::SetStatus(LPCTSTR text)
{
	if (CWnd* s = GetDlgItem(IDC_VST_STATUS)) s->SetWindowText(text);
}

void CVstHostDlg::ShowHelpSheet()
{
	CVstHelpDlg dlg(this); dlg.DoModal();
}

void CVstHostDlg::OnHelp() { ShowHelpSheet(); }
void CVstHostDlg::OnCloseButton() { DestroyWindow(); }
void CVstHostDlg::OnCancel() { DestroyWindow(); }
void CVstHostDlg::OnOK() {}

void CVstHostDlg::OnDestroy()
{
	StopMidi(); StopAudio();
	for (int i = 1; i <= 32; ++i) VstLiveUnloadPart(i);
	CCustomBlurDialogBase::OnDestroy();
}

void CVstHostDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_vstHostDlg == this) g_vstHostDlg = NULL;
	delete this;
}

void OpenVstHostModeless(CWnd* parent)
{
	if (g_vstHostDlg && ::IsWindow(g_vstHostDlg->GetSafeHwnd())) {
		g_vstHostDlg->ShowWindow(SW_SHOW);
		g_vstHostDlg->SetForegroundWindow();
		return;
	}
	g_vstHostDlg = new CVstHostDlg(parent);
	if (!g_vstHostDlg->Create(IDD_VSTHOST, parent)) {
		delete g_vstHostDlg; g_vstHostDlg = NULL; return;
	}
	g_vstHostDlg->ShowWindow(SW_SHOW);
	g_vstHostDlg->SetForegroundWindow();
}

