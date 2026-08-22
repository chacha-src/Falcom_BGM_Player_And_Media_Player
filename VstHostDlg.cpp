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
	IDC_VST_FILTER = 0x7e10,
	ID_VST_POP_RESCAN = 0xe710,
	ID_VST_POP_CLEAR = 0xe711,
	ID_VST_POP_SAVE = 0xe712,
	ID_VST_POP_EDITOR = 0xe713,
	// Send-channel picker: "as received" plus the 16 MIDI channels.
	ID_VST_POP_CH_ASIS = 0xe720,
	ID_VST_POP_CH_FIRST = 0xe721, // .. +15
	// Program picker; the list is capped at VST_PROG_MENU_MAX entries.
	ID_VST_POP_PROG_FIRST = 0xe740,
	VST_PROG_MENU_MAX = 64,
	VST_AUDIO_FRAMES = 512,
	VST_AUDIO_BUFFERS = 4,
	VST_ACTIVITY_TIMER = 1,
	MIDI_FIFO_CAP = 4096, // power of two
	SYSEX_POOL_SLOTS = 16,
	SYSEX_SLOT_BYTES = 4096,
	SYSEX_BUFS_PER_PORT = 4
};

// MIDI used to reach the engine through the dialog's message queue. Whenever
// the UI thread stalled (painting, menus, a drag) the events piled up and were
// then applied all at once, so a note-on and its note-off could land in the
// same audio block and that note was never heard. The MIDI callback now only
// queues, and the audio thread applies the events at block rate.
struct MidiFifo
{
	unsigned head;
	unsigned tail;
	DWORD msg[MIDI_FIFO_CAP];
	BYTE port[MIDI_FIFO_CAP];
	short sysexSlot[MIDI_FIFO_CAP]; // -1 for a plain short message
	int sysexLen[MIDI_FIFO_CAP];
	BYTE sysexPool[SYSEX_POOL_SLOTS][SYSEX_SLOT_BYTES];
	unsigned sysexNext;
	CRITICAL_SECTION cs;
	LONG init;
};

MidiFifo g_midiFifo = {};

// The driver hands a buffer back through the callback, but midiInAddBuffer is
// a multimedia call and must not be made from there, so the bytes are copied
// out and the buffer is recycled by the audio thread.
struct SysexBuf
{
	MIDIHDR hdr;
	BYTE data[SYSEX_SLOT_BYTES];
	HMIDIIN in;
	volatile LONG needAdd;
	volatile LONG prepared;
};

SysexBuf g_sysexBufs[3][SYSEX_BUFS_PER_PORT] = {};

void MidiFifoInit()
{
	if (InterlockedCompareExchange(&g_midiFifo.init, 1, 0) == 0) {
		InitializeCriticalSection(&g_midiFifo.cs);
		// Never deleted on purpose: a MIDI callback already in flight would
		// otherwise enter a destroyed section while the dialog closes.
		InterlockedExchange(&g_midiFifo.init, 2);
		return;
	}
	while (InterlockedCompareExchange(&g_midiFifo.init, 2, 2) != 2)
		Sleep(0);
}

void MidiFifoPush(int port, DWORD msg)
{
	if (InterlockedCompareExchange(&g_midiFifo.init, 2, 2) != 2) return;
	bool dropped = false;
	EnterCriticalSection(&g_midiFifo.cs);
	if (g_midiFifo.head - g_midiFifo.tail < MIDI_FIFO_CAP) {
		const unsigned i = g_midiFifo.head & (MIDI_FIFO_CAP - 1);
		g_midiFifo.msg[i] = msg;
		g_midiFifo.port[i] = (BYTE)port;
		g_midiFifo.sysexSlot[i] = -1;
		g_midiFifo.sysexLen[i] = 0;
		++g_midiFifo.head;
	} else {
		dropped = true;
	}
	LeaveCriticalSection(&g_midiFifo.cs);
}

// Queued alongside the short messages so a reset cannot overtake the notes
// that follow it.
void MidiFifoPushSysex(int port, const BYTE* data, int bytes)
{
	if (InterlockedCompareExchange(&g_midiFifo.init, 2, 2) != 2) return;
	if (!data || bytes <= 0) return;
	bool dropped = false;
	EnterCriticalSection(&g_midiFifo.cs);
	if (bytes > SYSEX_SLOT_BYTES || g_midiFifo.head - g_midiFifo.tail >= MIDI_FIFO_CAP) {
		dropped = true;
	} else {
		const unsigned slot = g_midiFifo.sysexNext++ % SYSEX_POOL_SLOTS;
		memcpy(g_midiFifo.sysexPool[slot], data, (size_t)bytes);
		const unsigned i = g_midiFifo.head & (MIDI_FIFO_CAP - 1);
		g_midiFifo.msg[i] = 0;
		g_midiFifo.port[i] = (BYTE)port;
		g_midiFifo.sysexSlot[i] = (short)slot;
		g_midiFifo.sysexLen[i] = bytes;
		++g_midiFifo.head;
	}
	LeaveCriticalSection(&g_midiFifo.cs);
}

void MidiFifoClear()
{
	if (InterlockedCompareExchange(&g_midiFifo.init, 2, 2) != 2) return;
	EnterCriticalSection(&g_midiFifo.cs);
	g_midiFifo.tail = g_midiFifo.head;
	LeaveCriticalSection(&g_midiFifo.cs);
}

void MidiFifoDrain()
{
	if (InterlockedCompareExchange(&g_midiFifo.init, 2, 2) != 2) return;
	for (;;) {
		DWORD msg = 0;
		int port = 0, slot = -1, len = 0;
		BYTE sysex[SYSEX_SLOT_BYTES];
		EnterCriticalSection(&g_midiFifo.cs);
		const bool empty = g_midiFifo.head == g_midiFifo.tail;
		if (!empty) {
			const unsigned i = g_midiFifo.tail & (MIDI_FIFO_CAP - 1);
			msg = g_midiFifo.msg[i];
			port = g_midiFifo.port[i];
			slot = g_midiFifo.sysexSlot[i];
			len = g_midiFifo.sysexLen[i];
			if (slot >= 0 && len > 0) memcpy(sysex, g_midiFifo.sysexPool[slot], (size_t)len);
			++g_midiFifo.tail;
		}
		LeaveCriticalSection(&g_midiFifo.cs);
		if (empty) return;
		if (slot >= 0) VstLiveMidiSysex(port, sysex, len);
		else VstLiveMidiShort(port, msg);
	}
}

// Recycles the buffers the driver returned. A multimedia call, so it belongs
// on the audio thread rather than in the MIDI callback.
void MidiSysexRecycle()
{
	for (int p = 0; p < 3; ++p)
		for (int b = 0; b < SYSEX_BUFS_PER_PORT; ++b) {
			SysexBuf& s = g_sysexBufs[p][b];
			if (InterlockedCompareExchange(&s.needAdd, 0, 1) != 1) continue;
			if (!s.in || !InterlockedCompareExchange(&s.prepared, 1, 1)) continue;
			s.hdr.dwBytesRecorded = 0;
			if (midiInAddBuffer(s.in, &s.hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR)
				InterlockedExchange(&s.needAdd, 1);
		}
}

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
		CCC_ApplyWindowIconFromTemplate(this, IDD_VST_WAIT);
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
		CCC_ApplyWindowIconFromTemplate(this, IDD_VST_HELP);
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
			LL14(L"・Z列＝C4～、Q列＝C5～のPC鍵盤（コンボ／入力中は無効。Spaceで全ノートオフ）。", L"· PC keys: Z-row = C4…, Q-row = C5… (off while typing in a combo; Space = all notes off).", L"· Clavier PC: rangée Z = C4…, Q = C5… (inactif dans les listes; Espace = all notes off).", L"· Tastiera PC: fila Z = C4…, Q = C5… (disattiva nei combo; Spazio = all notes off).", L"· Teclado PC: fila Z = C4…, Q = C5… (inactivo en combos; Espacio = all notes off).", L"· PC 건반: Z열=C4~, Q열=C5~(콤보 입력 중 비활성, Space=올 노트 오프).", L"· PC 键盘：Z 行=C4…，Q 行=C5…（组合框输入时无效；空格全音符关闭）。", L"· لوحة PC: صف Z=C4… وصف Q=C5… (معطّل أثناء الكتابة؛ المسافة = إيقاف كل النغمات).", L"· ПК-клавиатура: ряд Z = C4…, Q = C5… (не в комбо; Пробел = all notes off).", L"· PC-Tastatur: Z-Reihe = C4…, Q = C5… (nicht in Combos; Leertaste = All Notes Off).", L"· Teclado PC: fila Z = C4…, Q = C5… (inativo em combos; Espaço = all notes off).", L"· PC-toetsenbord: Z-rij = C4…, Q = C5… (uit in combo's; Spatie = all notes off).", L"· Klawiatura PC: rząd Z = C4…, Q = C5… (wył. w combo; Spacja = all notes off).", L"· PC klavye: Z satırı = C4…, Q = C5… (kombo yazarken kapalı; Boşluk = all notes off)."),
			LL14(L"・一覧にはドロップで実際に載るものだけが出ます（初回／再スキャンで確認）。", L"· The list shows only plug-ins that actually drop onto a part (checked on first open / rescan).", L"· La liste n'affiche que les plug-ins réellement déposables (vérifié à l'ouverture / rescannage).", L"· L'elenco mostra solo i plug-in che si possono trascinare (controllo all'apertura / scansione).", L"· La lista solo muestra plug-ins que se pueden soltar (comprobado al abrir / reescanear).", L"· 목록에는 실제로 드롭되는 플러그인만 표시됩니다(첫 실행/재검색 시 확인).", L"· 列表只显示可拖放到声部的插件（首次打开/重新扫描时检查）。", L"· تظهر القائمة فقط الإضافات القابلة للإفلات (تُفحص عند الفتح / إعادة المسح).", L"· В списке только плагины, которые реально ставятся на слот (проверка при открытии / скане).", L"· Die Liste zeigt nur Plug-ins, die sich ablegen lassen (Prüfung beim Öffnen / Neu scannen).", L"· A lista mostra só plug-ins que se podem largar (verificado ao abrir / procurar).", L"· De lijst toont alleen plug-ins die echt te droppen zijn (controle bij openen / scannen).", L"· Lista pokazuje tylko wtyczki, które da się upuścić (sprawdzane przy otwarciu / skanie).", L"· Liste yalnızca gerçekten bırakılabilen eklentileri gösterir (açılış / yeniden tarama kontrolü)."),
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
	  m_dragScanIndex(-1), m_hoverPlugin(-1), m_hoverSlot(-1), m_trackLeave(FALSE),
	  m_pressSlot(-1), m_pressBtn(-1), m_hintSlot(-1), m_hintBtn(-1)
{
	memset(m_scanIndices, -1, sizeof(m_scanIndices));
	memset(m_slots, -1, sizeof(m_slots));
	memset(m_actLevel, 0, sizeof(m_actLevel));
	memset(m_actMask, 0, sizeof(m_actMask));
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

CRect CVstWireCtrl::SlotCellRect(int i) const
{
	CRect rc; GetClientRect(&rc);
	const int left = max(156, rc.Width() * 38 / 100) + 8;
	const int cols = 2, gap = 5;
	const int w = max(70, (rc.Width() - left - 10 - gap) / cols);
	const int row = i / cols, col = i % cols;
	const int avail = max(18, (rc.Height() - 32) / 16);
	return CRect(left + col * (w + gap), 27 + row * avail,
		left + col * (w + gap) + w, 27 + row * avail + avail - 2);
}

// Two small buttons at the right end of a loaded slot's bar: the editor and
// the part/program menu, so neither needs a right-click to be found.
CRect CVstWireCtrl::SlotBtnRect(int i, int which) const
{
	CRect r = SlotRect(i);
	const int bw = 15;
	int bh = r.Height() - 6;
	if (bh > 15) bh = 15;
	if (bh < 8) bh = r.Height() - 2;
	if (bh < 6 || r.Width() < 3 * bw + 24) return CRect(0, 0, 0, 0);
	const int top = r.top + (r.Height() - bh) / 2;
	CRect b(r.right - 4 - bw, top, r.right - 4, top + bh);
	if (which == SLOT_BTN_EDIT) b.OffsetRect(-(bw + 2), 0);
	return b;
}

int CVstWireCtrl::HitSlotBtn(CPoint pt, int* outBtn) const
{
	for (int i = 0; i < PART_COUNT; ++i) {
		if (m_slots[i] < 0) continue;
		for (int b = 0; b < 2; ++b) {
			CRect r = SlotBtnRect(i, b);
			if (!r.IsRectEmpty() && r.PtInRect(pt)) {
				if (outBtn) *outBtn = b;
				return i;
			}
		}
	}
	return -1;
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
		if (SlotCellRect(i).PtInRect(pt)) return i;
	return -1;
}

void CVstWireCtrl::NotifyChanged(int slot)
{
	if (m_owner) m_owner->OnWireChanged(slot);
}

// A multi-timbral instance answers all 16 MIDI channels of its port block, so
// the slots above it hold no instance of their own yet are in use. Returns the
// slot that owns them, or -1 when this slot stands alone.
int CVstWireCtrl::CoveringMulti(int slot) const
{
	if (!m_owner || slot < 0 || slot >= PART_COUNT) return -1;
	if (m_slots[slot] >= 0) return -1;
	for (int i = slot - 1, blockStart = (slot / 16) * 16; i >= blockStart; --i) {
		if (m_slots[i] < 0) continue;
		return m_owner->PluginIsMulti(m_slots[i]) ? i : -1;
	}
	return -1;
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
		CRect cell = SlotCellRect(i);
		const BOOL full = m_slots[i] >= 0;
		const int cover = CoveringMulti(i);
		const BOOL hot = i == m_hoverSlot;
		const int lit = m_actLevel[i];
		// The area under the bar is part of the drop target, so show it while
		// dragging and use it for the notes that are sounding.
		if (hot && m_dragging) {
			dc.FillSolidRect(cell, RGB(30, 46, 66));
			dc.Draw3dRect(cell, RGB(120, 205, 250), RGB(60, 100, 140));
		}
		CRect info(cell.left + 6, r.bottom + 2, cell.right - 4, cell.bottom - 1);
		if (info.Height() >= 8) {
			const int strip = min(5, info.Height() - 1);
			CRect bar(info.left, info.bottom - strip, info.right, info.bottom);
			int shown = 0;
			for (int b = 0; b < 4; ++b) {
				unsigned bits = m_actMask[i][b];
				for (; bits; bits &= bits - 1) {
					unsigned low = bits & (~bits + 1u);
					int n = b * 32;
					while (!(low & 1u)) { low >>= 1; ++n; }
					// A0 to C8 laid out across the cell, so the ticks read like
					// a keyboard.
					int x = bar.left + (bar.Width() * (n - 21)) / 87;
					if (x < bar.left) x = bar.left;
					if (x > bar.right - 3) x = bar.right - 3;
					dc.FillSolidRect(x, bar.top, 3, strip, RGB(120, 240, 175));
					++shown;
				}
			}
			if (shown) dc.FillSolidRect(bar.left, bar.bottom - 1, bar.Width(), 1,
				RGB(52, 92, 76));
			if (!m_actNotes[i].IsEmpty()) {
				CRect t = info;
				t.bottom = bar.top - 1;
				if (t.Height() >= 9) {
					dc.SetTextColor(RGB(150, 232, 190));
					dc.DrawText(m_actNotes[i], t,
						DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
				}
			}
		}
		COLORREF bg = hot ? RGB(48, 68, 94)
			: (full ? RGB(56, 48, 24) : (cover >= 0 ? RGB(42, 39, 26) : RGB(34, 37, 46)));
		COLORREF edge = hot ? RGB(130, 215, 255)
			: (full ? RGB(245, 190, 70) : (cover >= 0 ? RGB(150, 124, 56) : RGB(82, 92, 112)));
		if (lit >= 3) { bg = RGB(30, 122, 78); edge = RGB(150, 255, 200); }
		else if (lit == 2) { bg = RGB(24, 88, 60); edge = RGB(120, 232, 175); }
		else if (lit == 1) { bg = RGB(26, 60, 50); edge = RGB(92, 186, 145); }
		dc.FillSolidRect(r, bg);
		dc.Draw3dRect(r, edge, RGB(16, 18, 23));
		CString s, name;
		if (full && m_owner) name = m_owner->PluginName(m_slots[i]);
		else if (cover >= 0 && m_owner) name = m_owner->PluginName(m_slots[cover]);
		// A forced send channel changes which sound the plug-in makes, so say so
		// on the bar instead of hiding it inside the slot menu.
		if (full) {
			const int send = VstLiveSendChannel(i + 1);
			if (send >= 0)
				s.Format(L"%02d→ch%d  %s", i + 1, send + 1, (LPCTSTR)name);
			else s.Format(L"%02d  %s", i + 1, (LPCTSTR)name);
		}
		else if (cover >= 0) s.Format(L"%02d  ch%d ← %s", i + 1, (i % 16) + 1, (LPCTSTR)name);
		else s.Format(L"%02d  —", i + 1);
		CRect t = r; t.DeflateRect(4, 1);
		CRect bEdit = full ? SlotBtnRect(i, SLOT_BTN_EDIT) : CRect(0, 0, 0, 0);
		CRect bMenu = full ? SlotBtnRect(i, SLOT_BTN_MENU) : CRect(0, 0, 0, 0);
		if (!bEdit.IsRectEmpty()) t.right = bEdit.left - 4;
		if (lit && !m_actText[i].IsEmpty()) {
			CRect nt = t;
			nt.left = max(t.left, t.right - 120);
			dc.SetTextColor(RGB(205, 255, 225));
			dc.DrawText(m_actText[i], nt, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			t.right = nt.left - 4;
		}
		dc.SetTextColor(lit ? RGB(228, 255, 238)
			: (full ? RGB(255, 238, 180) : (cover >= 0 ? RGB(198, 176, 124) : RGB(155, 164, 180))));
		dc.DrawText(s, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		for (int b = 0; b < 2; ++b) {
			CRect bt = b == SLOT_BTN_EDIT ? bEdit : bMenu;
			if (bt.IsRectEmpty()) continue;
			const BOOL down = m_pressSlot == i && m_pressBtn == b;
			dc.FillSolidRect(bt, down ? RGB(96, 130, 180) : RGB(46, 56, 76));
			dc.Draw3dRect(bt, down ? RGB(200, 232, 255) : RGB(132, 158, 198),
				RGB(20, 24, 32));
			const COLORREF ink = down ? RGB(255, 255, 255) : RGB(198, 220, 250);
			if (b == SLOT_BTN_EDIT) {
				// A little window with a title bar: this opens the plug-in's own UI.
				CRect g = bt; g.DeflateRect(4, 4);
				if (g.Width() >= 4 && g.Height() >= 4) {
					dc.Draw3dRect(g, ink, ink);
					dc.FillSolidRect(g.left, g.top, g.Width(), 2, ink);
				}
			} else {
				// Down arrow: the part / program menu.
				const int cx = bt.left + bt.Width() / 2;
				const int cy = bt.top + bt.Height() / 2 - 1;
				for (int k = 0; k < 4; ++k)
					dc.FillSolidRect(cx - 3 + k, cy + k, (4 - k) * 2 - 1, 1, ink);
			}
		}
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

// Which parts are hearing MIDI right now. Only the slots whose state moved get
// invalidated, so the 20 Hz refresh does not repaint the whole palette.
void CVstWireCtrl::RefreshActivity()
{
	static const wchar_t* kNames[12] = { L"C", L"C#", L"D", L"D#", L"E", L"F",
		L"F#", L"G", L"G#", L"A", L"A#", L"B" };
	if (!GetSafeHwnd()) return;
	for (int i = 0; i < PART_COUNT; ++i) {
		VstLiveActInfo a = {};
		int level = 0;
		CString text, notes;
		if (VstLiveActivity(i + 1, &a) && a.ageMs >= 0) {
			if (a.held > 0) level = a.ageMs < 120 ? 3 : 2;
			else if (a.ageMs < 400) level = 1;
			// The bar carries the newest note; every held note is listed under it.
			if (level && a.vel > 0)
				text.Format(L"%s%d v%d", kNames[a.note % 12], a.note / 12 - 1, a.vel);
			int listed = 0;
			for (int b = 0; b < 4 && listed < 10; ++b)
				for (int bit = 0; bit < 32 && listed < 10; ++bit) {
					if (!(a.mask[b] & (1u << bit))) continue;
					const int n = b * 32 + bit;
					CString one;
					one.Format(listed ? L" %s%d" : L"%s%d", kNames[n % 12], n / 12 - 1);
					notes += one;
					++listed;
				}
			if (a.held > listed) {
				CString more;
				more.Format(L" +%d", a.held - listed);
				notes += more;
			}
		}
		if (level == m_actLevel[i] && text == m_actText[i] && notes == m_actNotes[i] &&
			memcmp(m_actMask[i], a.mask, sizeof(a.mask)) == 0)
			continue;
		m_actLevel[i] = level;
		m_actText[i] = text;
		m_actNotes[i] = notes;
		memcpy(m_actMask[i], a.mask, sizeof(a.mask));
		CRect r = SlotCellRect(i);
		r.InflateRect(2, 2);
		InvalidateRect(&r, FALSE);
	}
}

void CVstWireCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect r;
	if (!dc.GetClipBox(&r) || r.IsRectEmpty()) GetClientRect(&r);
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
	if (m_owner) m_owner->SetFocus();
	int btn = -1;
	const int bslot = HitSlotBtn(pt, &btn);
	if (bslot >= 0) {
		m_pressSlot = bslot; m_pressBtn = btn;
		SetCapture(); Invalidate(FALSE); return;
	}
	const int p = HitPalette(pt);
	if (p >= 0) {
		m_dragging = TRUE; m_dragScanIndex = m_scanIndices[p]; m_dragPt = pt;
		SetCapture(); Invalidate(FALSE); return;
	}
	CStatic::OnLButtonDown(flags, pt);
}

void CVstWireCtrl::OnLButtonUp(UINT flags, CPoint pt)
{
	if (m_pressSlot >= 0) {
		ReleaseCapture();
		const int slot = m_pressSlot, btn = m_pressBtn;
		m_pressSlot = m_pressBtn = -1;
		Invalidate(FALSE);
		int hitBtn = -1;
		if (HitSlotBtn(pt, &hitBtn) != slot || hitBtn != btn) return;
		if (btn == SLOT_BTN_EDIT) {
			if (VstLiveEditorOpen(slot + 1) != 0 && m_owner)
				m_owner->SetStatus(LL14(L"このプラグインには設定画面がありません",
					L"This plug-in has no editor", L"Ce plug-in n'a pas d'éditeur",
					L"Questo plug-in non ha un editor", L"Este plug-in no tiene editor",
					L"이 플러그인에는 설정 화면이 없습니다", L"此插件没有设置界面",
					L"لا تحتوي هذه الإضافة على واجهة", L"У этого плагина нет редактора",
					L"Dieses Plug-in hat keinen Editor", L"Este plug-in não tem editor",
					L"Deze plug-in heeft geen editor", L"Ta wtyczka nie ma edytora",
					L"Bu eklentinin arayüzü yok"));
			return;
		}
		CRect b = SlotBtnRect(slot, SLOT_BTN_MENU);
		CPoint at(b.left, b.bottom);
		ClientToScreen(&at);
		ShowSlotMenu(slot, at);
		return;
	}
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
	// The two slot buttons are small, so say what they do while the pointer is
	// on them.
	int hoverBtn = -1;
	const int btnSlot = m_dragging ? -1 : HitSlotBtn(pt, &hoverBtn);
	if (btnSlot != m_hintSlot || hoverBtn != m_hintBtn) {
		m_hintSlot = btnSlot;
		m_hintBtn = hoverBtn;
		if (m_owner && btnSlot >= 0) {
			CString s;
			if (hoverBtn == SLOT_BTN_EDIT)
				s.Format(L"%02d: %s", btnSlot + 1,
					LL14(L"プラグインの設定画面を開く", L"Open the plug-in's editor",
						L"Ouvrir l'éditeur du plug-in", L"Apri l'editor del plug-in",
						L"Abrir el editor del plug-in", L"플러그인 설정 화면 열기",
						L"打开插件设置界面", L"فتح واجهة الإضافة",
						L"Открыть редактор плагина", L"Editor des Plug-ins öffnen",
						L"Abrir o editor do plug-in", L"Editor van de plug-in openen",
						L"Otwórz edytor wtyczki", L"Eklenti arayüzünü aç"));
			else
				s.Format(L"%02d: %s", btnSlot + 1,
					LL14(L"パート（渡すチャンネル）とプログラムを選ぶ",
						L"Pick the part channel and program", L"Choisir le canal et le programme",
						L"Scegli canale e programma", L"Elegir canal y programa",
						L"파트 채널과 프로그램 선택", L"选择声部通道与程序",
						L"اختر القناة والبرنامج", L"Выбор канала и программы",
						L"Kanal und Programm wählen", L"Escolher canal e programa",
						L"Kanaal en programma kiezen", L"Wybierz kanał i program",
						L"Kanal ve program seç"));
			m_owner->SetStatus(s);
		}
	}
	Invalidate(FALSE);
	CStatic::OnMouseMove(flags, pt);
}

void CVstWireCtrl::OnMouseLeave()
{
	m_trackLeave = FALSE; m_hoverPlugin = m_hoverSlot = -1;
	m_hintSlot = m_hintBtn = -1;
	Invalidate(FALSE);
}

// Everything that belongs to one loaded part: its editor, which MIDI channel
// it hands the plug-in, and its program list. Drum kits that only listen on
// ch1 can therefore sit in any slot, and HALion / SampleTank / Groove Agent
// parts are reachable without going through the plug-in's own window.
void CVstWireCtrl::ShowSlotMenu(int slot, CPoint screenPt)
{
	if (slot < 0 || slot >= PART_COUNT || m_slots[slot] < 0 || !m_owner) return;
	const int part = slot + 1;
	const int curCh = VstLiveSendChannel(part);
	const int curProg = VstLiveProgramCurrent(part);
	int progCount = VstLiveProgramCount(part);
	if (progCount > VST_PROG_MENU_MAX) progCount = VST_PROG_MENU_MAX;

	CCustomPopupMenu menu;
	menu.AddCommand(ID_VST_POP_EDITOR, LL14(L"設定画面を開く", L"Open editor", L"Ouvrir l'éditeur",
		L"Apri l'editor", L"Abrir el editor", L"설정 화면 열기", L"打开设置界面",
		L"فتح واجهة الإعدادات", L"Открыть редактор", L"Editor öffnen", L"Abrir o editor",
		L"Editor openen", L"Otwórz edytor", L"Arayüzü aç"));
	menu.AddSeparator();

	CCustomPopupMenu* chMenu = menu.AddSubMenu(LL14(L"プラグインに渡すチャンネル",
		L"Channel sent to the plug-in", L"Canal envoyé au plug-in", L"Canale inviato al plug-in",
		L"Canal enviado al plug-in", L"플러그인에 보낼 채널", L"发送到插件的通道",
		L"القناة المرسلة إلى الإضافة", L"Канал, отправляемый плагину",
		L"An das Plug-in gesendeter Kanal", L"Canal enviado ao plug-in",
		L"Kanaal naar de plug-in", L"Kanał wysyłany do wtyczki", L"Eklentiye gönderilen kanal"));
	if (chMenu) {
		chMenu->AddCheck(ID_VST_POP_CH_ASIS, LL14(L"受信したチャンネルのまま",
			L"Keep the received channel", L"Conserver le canal reçu", L"Mantieni il canale ricevuto",
			L"Mantener el canal recibido", L"수신한 채널 그대로", L"保持接收到的通道",
			L"الإبقاء على القناة المستلمة", L"Оставить принятый канал",
			L"Empfangenen Kanal beibehalten", L"Manter o canal recebido",
			L"Ontvangen kanaal behouden", L"Zachowaj odebrany kanał", L"Alınan kanalı koru"),
			curCh < 0);
		for (int c = 0; c < 16; ++c) {
			CString s;
			s.Format(L"ch %d%s", c + 1, c == 9 ? L"  (drums)" : L"");
			chMenu->AddCheck(ID_VST_POP_CH_FIRST + c, s, curCh == c);
		}
	}

	CCustomPopupMenu* progMenu = progCount > 0
		? menu.AddSubMenu(LL14(L"プログラム / キット", L"Program / kit", L"Programme / kit",
			L"Programma / kit", L"Programa / kit", L"프로그램 / 킷", L"程序 / 音色组",
			L"البرنامج / الطقم", L"Программа / кит", L"Programm / Kit", L"Programa / kit",
			L"Programma / kit", L"Program / zestaw", L"Program / kit"))
		: NULL;
	if (progMenu) {
		const int stride = 64;
		wchar_t buf[VST_PROG_MENU_MAX * 64] = {};
		const int got = VstLiveProgramNames(part, 0, progCount, buf, stride);
		for (int i = 0; i < got; ++i) {
			CString s;
			s.Format(L"%3d  %s", i + 1, buf + (size_t)i * stride);
			progMenu->AddCheck(ID_VST_POP_PROG_FIRST + i, s, i == curProg);
		}
	}

	menu.AddSeparator();
	menu.AddCommand(ID_VST_POP_CLEAR, LL14(L"このスロットを解除", L"Clear this slot", L"Effacer ce slot",
		L"Azzera slot", L"Borrar ranura", L"이 슬롯 해제", L"清除此插槽", L"مسح هذه الفتحة",
		L"Очистить слот", L"Slot leeren", L"Limpar slot", L"Slot wissen", L"Wyczyść slot",
		L"Slotu temizle"));

	const UINT cmd = menu.Track(screenPt, (CWnd*)m_owner);
	if (!cmd) return;
	if (cmd == ID_VST_POP_EDITOR) VstLiveEditorOpen(part);
	else if (cmd == ID_VST_POP_CLEAR) ClearSlot(slot);
	else if (cmd == ID_VST_POP_CH_ASIS) VstLiveSetSendChannel(part, -1);
	else if (cmd >= ID_VST_POP_CH_FIRST && cmd < ID_VST_POP_CH_FIRST + 16)
		VstLiveSetSendChannel(part, (int)(cmd - ID_VST_POP_CH_FIRST));
	else if (cmd >= ID_VST_POP_PROG_FIRST &&
		cmd < ID_VST_POP_PROG_FIRST + VST_PROG_MENU_MAX)
		VstLiveSetProgram(part, (int)(cmd - ID_VST_POP_PROG_FIRST));
	Invalidate(FALSE);
}

void CVstWireCtrl::OnRButtonUp(UINT, CPoint pt)
{
	if (m_owner) m_owner->SetFocus();
	const int slot = HitSlot(pt);
	if (slot >= 0 && m_slots[slot] >= 0) {
		CPoint at = pt;
		ClientToScreen(&at);
		ShowSlotMenu(slot, at);
		return;
	}
	// Right-clicking a channel that a multi-timbral part answers should reach
	// that part's editor, not nothing.
	const int owner = (slot >= 0 && m_slots[slot] < 0) ? CoveringMulti(slot) : slot;
	CCustomPopupMenu menu;
	menu.AddCommand(ID_VST_POP_EDITOR, LL14(L"プラグインの画面を開く", L"Open plug-in editor", L"Ouvrir l'éditeur du plug-in",
		L"Apri l'editor del plug-in", L"Abrir el editor del plug-in", L"플러그인 화면 열기", L"打开插件界面",
		L"فتح واجهة الإضافة", L"Открыть редактор плагина", L"Plug-in-Editor öffnen", L"Abrir o editor do plug-in",
		L"Plug-in-editor openen", L"Otwórz edytor wtyczki", L"Eklenti arayüzünü aç"),
		NULL, owner >= 0 && m_slots[owner] >= 0);
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
	else if (cmd == ID_VST_POP_EDITOR && owner >= 0 && m_slots[owner] >= 0)
		VstLiveEditorOpen(owner + 1);
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
	memset(m_pcHeldNote, 0, sizeof(m_pcHeldNote));
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
	ON_WM_TIMER()
	ON_WM_ACTIVATE()
	ON_WM_LBUTTONDOWN()
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

	const LPCTSTR labels[LABEL_COUNT] = {
		LL14(L"プリセット", L"Preset", L"Préréglage", L"Preset", L"Preajuste", L"프리셋", L"预设", L"إعداد مسبق",
			L"Пресет", L"Preset", L"Predefinição", L"Preset", L"Ustawienie", L"Ön ayar"),
		LL14(L"MIDI入力 1", L"MIDI In 1", L"Entrée MIDI 1", L"Ingresso MIDI 1", L"Entrada MIDI 1", L"MIDI 입력 1",
			L"MIDI 输入 1", L"دخل MIDI 1", L"MIDI-вход 1", L"MIDI-Eingang 1", L"Entrada MIDI 1", L"MIDI-ingang 1",
			L"Wejście MIDI 1", L"MIDI girişi 1"),
		LL14(L"MIDI入力 2", L"MIDI In 2", L"Entrée MIDI 2", L"Ingresso MIDI 2", L"Entrada MIDI 2", L"MIDI 입력 2",
			L"MIDI 输入 2", L"دخل MIDI 2", L"MIDI-вход 2", L"MIDI-Eingang 2", L"Entrada MIDI 2", L"MIDI-ingang 2",
			L"Wejście MIDI 2", L"MIDI girişi 2"),
		LL14(L"MIDI入力 3", L"MIDI In 3", L"Entrée MIDI 3", L"Ingresso MIDI 3", L"Entrada MIDI 3", L"MIDI 입력 3",
			L"MIDI 输入 3", L"دخل MIDI 3", L"MIDI-вход 3", L"MIDI-Eingang 3", L"Entrada MIDI 3", L"MIDI-ingang 3",
			L"Wejście MIDI 3", L"MIDI girişi 3"),
		LL14(L"音声出力", L"Audio out", L"Sortie audio", L"Uscita audio", L"Salida de audio", L"오디오 출력",
			L"音频输出", L"خرج الصوت", L"Аудиовыход", L"Audioausgang", L"Saída de áudio", L"Audio-uitgang",
			L"Wyjście audio", L"Ses çıkışı"),
		LL14(L"一覧の絞り込み", L"List filter", L"Filtre de liste", L"Filtro elenco", L"Filtro de lista", L"목록 필터",
			L"列表筛选", L"مرشّح القائمة", L"Фильтр списка", L"Listenfilter", L"Filtro da lista", L"Lijstfilter",
			L"Filtr listy", L"Liste filtresi")
	};
	for (int i = 0; i < LABEL_COUNT; ++i) {
		m_labels[i].Create(labels[i], WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
			CRect(8, 4, 120, 18), this);
		m_labels[i].SetFont(GetFont());
	}
	m_monitor.Create(L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
		CRect(8, 4, 120, 18), this);
	m_monitor.SetFont(GetFont());

	LayoutHelpBtn();
	CCC_CaptionLayout(m_hWnd);

	FillDevices();
	LoadPresets();
	VstScanEnsure(m_hWnd);
	VstScanVerifyLiveList(m_hWnd);
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
	SetTimer(VST_ACTIVITY_TIMER, 50, NULL);
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
	if (HandlePcKeyboardMidi(msg)) return TRUE;
	return CCustomBlurDialogBase::PreTranslateMessage(msg);
}

// Z-row = C4 (MIDI 60); Q-row = C5. Black keys on the home/number row.
// Edit and Combo focus is left alone so filter typing and device lists still work.
int CVstHostDlg::PcKeyToNote(UINT vk) const
{
	switch (vk) {
	case 'Z': return 60; case 'S': return 61; case 'X': return 62; case 'D': return 63;
	case 'C': return 64; case 'V': return 65; case 'G': return 66; case 'B': return 67;
	case 'H': return 68; case 'N': return 69; case 'J': return 70; case 'M': return 71;
	case VK_OEM_COMMA: return 72; case 'L': return 73; case VK_OEM_PERIOD: return 74;
	case VK_OEM_1: return 75; case VK_OEM_2: return 76;
	case 'Q': return 72; case '2': return 73; case 'W': return 74; case '3': return 75;
	case 'E': return 76; case 'R': return 77; case '5': return 78; case 'T': return 79;
	case '6': return 80; case 'Y': return 81; case '7': return 82; case 'U': return 83;
	case 'I': return 84; case '9': return 85; case 'O': return 86; case '0': return 87;
	case 'P': return 88;
	default: return -1;
	}
}

BOOL CVstHostDlg::PcFocusBlocksKeys() const
{
	CWnd* f = GetFocus();
	if (!f) return FALSE;
	HWND h = f->GetSafeHwnd();
	while (h) {
		wchar_t cls[64] = {};
		GetClassNameW(h, cls, 64);
		if (!_wcsicmp(cls, L"Edit") || !_wcsicmp(cls, L"ComboBox") ||
			!_wcsicmp(cls, L"ComboBoxEx32"))
			return TRUE;
		if (h == m_hWnd) break;
		h = ::GetParent(h);
	}
	return FALSE;
}

void CVstHostDlg::PcSendShort(DWORD msg)
{
	if (InterlockedCompareExchange(&m_audioRunning, 0, 0) != 0)
		MidiFifoPush(0, msg);
	else
		VstLiveMidiShort(0, msg);
}

void CVstHostDlg::PcKeyReleaseAll()
{
	for (int vk = 0; vk < 256; ++vk) {
		if (!m_pcHeldNote[vk]) continue;
		const int note = (int)m_pcHeldNote[vk] - 1;
		m_pcHeldNote[vk] = 0;
		PcSendShort((DWORD)(0x80 | (note << 8)));
	}
}

BOOL CVstHostDlg::HandlePcKeyboardMidi(MSG* msg)
{
	if (!msg || !GetSafeHwnd()) return FALSE;
	if (msg->message != WM_KEYDOWN && msg->message != WM_KEYUP &&
		msg->message != WM_SYSKEYDOWN && msg->message != WM_SYSKEYUP)
		return FALSE;
	// Ctrl / Alt belong to UI shortcuts; leave them alone.
	if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
		return FALSE;
	if (PcFocusBlocksKeys()) return FALSE;

	const UINT vk = (UINT)msg->wParam;
	if (vk == VK_SPACE && msg->message == WM_KEYDOWN && !(msg->lParam & (1 << 30))) {
		PcKeyReleaseAll();
		VstLiveAllNotesOff();
		return TRUE;
	}

	const int note = PcKeyToNote(vk);
	if (note < 0 || note > 127 || vk >= 256) return FALSE;

	if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) {
		if (msg->lParam & (1 << 30)) return TRUE; // auto-repeat
		if (m_pcHeldNote[vk]) return TRUE;
		m_pcHeldNote[vk] = (BYTE)(note + 1);
		PcSendShort((DWORD)(0x90 | (note << 8) | (100 << 16)));
		return TRUE;
	}
	if (m_pcHeldNote[vk]) {
		m_pcHeldNote[vk] = 0;
		PcSendShort((DWORD)(0x80 | (note << 8)));
	}
	return TRUE;
}

void CVstHostDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CCustomBlurDialogBase::OnActivate(nState, pWndOther, bMinimized);
	if (nState == WA_INACTIVE) PcKeyReleaseAll();
}

void CVstHostDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
}

void CVstHostDlg::LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }

void CVstHostDlg::LayoutChildren(int cx, int cy)
{
	if (!GetSafeHwnd() || !m_wire.GetSafeHwnd()) return;
	const int capH = GetCustomCaptionHeight();
	const int pad = 8, rowH = 24, gap = 5, lblGap = 4;
	// Sized from the real font metrics: a fixed height clipped the Japanese
	// labels and made them look glued to the combo below.
	int lblH = 16;
	{
		CClientDC dc(this);
		CFont* prev = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		if (dc.GetTextMetrics(&tm) && tm.tmHeight > 0) lblH = tm.tmHeight + 3;
		dc.SelectObject(prev);
	}
	const int top = capH + pad + lblH + lblGap;
	const int bottomH = 28;
	int x = pad;
	if (m_labels[0].GetSafeHwnd())
		m_labels[0].SetWindowPos(NULL, x + 1, top - lblH - lblGap, 142, lblH, SWP_NOZORDER);
	m_preset.SetWindowPos(NULL, x, top, 142, 220, SWP_NOZORDER); x += 142 + gap;
	m_rename.SetWindowPos(NULL, x, top, 64, rowH, SWP_NOZORDER); x += 64 + gap;
	m_del.SetWindowPos(NULL, x, top, 54, rowH, SWP_NOZORDER); x += 54 + gap;
	m_save.SetWindowPos(NULL, x, top, 58, rowH, SWP_NOZORDER); x += 58 + gap;
	m_rescan.SetWindowPos(NULL, cx - pad - 82, top, 82, rowH, SWP_NOZORDER);
	const int y2 = top + rowH + gap + lblH + lblGap;
	const int comboW = max(90, (cx - pad * 2 - gap * 4) / 5);
	for (int i = 0; i < 5; ++i)
		if (m_labels[i + 1].GetSafeHwnd())
			m_labels[i + 1].SetWindowPos(NULL, pad + i * (comboW + gap) + 1,
				y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	for (int i = 0; i < 3; ++i)
		m_midiIn[i].SetWindowPos(NULL, pad + i * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	m_speakerOut.SetWindowPos(NULL, pad + 3 * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	m_pluginFilter.SetWindowPos(NULL, pad + 4 * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	const int wireTop = y2 + rowH + gap;
	m_wire.SetWindowPos(NULL, pad, wireTop, max(10, cx - pad * 2), max(40, cy - wireTop - bottomH - pad), SWP_NOZORDER);
	const int statusW = max(20, (cx - 110) / 2);
	if (CWnd* st = GetDlgItem(IDC_VST_STATUS))
		st->SetWindowPos(NULL, pad, cy - bottomH + 4, statusW, 18, SWP_NOZORDER);
	if (m_monitor.GetSafeHwnd())
		m_monitor.SetWindowPos(NULL, pad + statusW + gap, cy - bottomH + 4,
			max(20, cx - pad * 2 - statusW - gap - 90), 18, SWP_NOZORDER);
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

BOOL CVstHostDlg::PluginIsMulti(int scanIndex) const
{
	const VstPluginInfo* pi = VstScanGet(scanIndex);
	return (pi && pi->isMultiTimbral) ? TRUE : FALSE;
}

void CVstHostDlg::RebuildPluginList()
{
	CString filter;
	m_pluginFilter.GetWindowText(filter); filter.MakeLower();
	if (m_pluginFilter.GetCurSel() == 0) filter.Empty();

	// A plug-in that stayed silent although it should have been ready to play is
	// a broken install, and offering it only wastes the user's time. Samplers
	// that are merely waiting for a patch stay listed. If the probe never ran at
	// all, listing nothing would be worse than listing unproven entries.
	int audibleKnown = 0;
	for (int i = 0; i < VstScanGetCount(); ++i) {
		const VstPluginInfo* pi = VstScanGet(i);
		if (pi && pi->isInstrument && pi->isLiveOk && pi->isAudible) {
			audibleKnown = 1;
			break;
		}
	}

	CString names[100]; int indices[100]; int count = 0;
	for (int i = 0; i < VstScanGetCount() && count < 100; ++i) {
		const VstPluginInfo* pi = VstScanGet(i);
		if (!pi || !pi->isInstrument || !pi->isLiveOk) continue;
		if (audibleKnown && pi->isAudible == 0) continue;
		CString n(pi->name), low(n); low.MakeLower();
		if (!filter.IsEmpty() && low.Find(filter) < 0) continue;
		names[count] = n;
		if (pi->isMultiTimbral) names[count] = CString(L"[M] ") + n;
		// Loads and takes notes, but needs a patch picked in its own browser
		// before anything is heard.
		if (pi->isAudible == 2) names[count] += L" (音色選択)";
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
	VstScanVerifyLiveList(m_hWnd);
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
	if (!pi) return;
	if (VstLiveLoadPart(slot + 1, pi->path, pi->isVst3) != 0) {
		m_slots[slot] = -1; m_wire.SetSlots(m_slots);
		SetStatus(LL14(L"プラグインを読み込めません", L"Could not load plug-in", L"Impossible de charger le plug-in",
			L"Impossibile caricare il plug-in", L"No se pudo cargar el plug-in", L"플러그인을 불러올 수 없습니다",
			L"无法加载插件", L"تعذر تحميل الإضافة", L"Не удалось загрузить плагин", L"Plugin konnte nicht geladen werden",
			L"Não foi possível carregar o plug-in", L"Kan plug-in niet laden", L"Nie można wczytać wtyczki", L"Eklenti yüklenemedi"));
		return;
	}
	// A multi-timbral instance answers all 16 channels of its port block, so
	// the remaining slots of that block must not look free.
	if (pi->isMultiTimbral) {
		const int blockEnd = (slot / 16) * 16 + 16;
		for (int i = slot + 1; i < blockEnd && i < 32; ++i) {
			if (m_slots[i] < 0) continue;
			VstLiveUnloadPart(i + 1);
			m_slots[i] = -1;
		}
		m_wire.SetSlots(m_slots);
	}
	SetStatus(pi->name);
}

void CALLBACK CVstHostDlg::MidiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR)
{
	CVstHostDlg* self = (CVstHostDlg*)(instance & ~(DWORD_PTR)3);
	const int port = (int)(instance & 3);
	const bool queued = self && InterlockedCompareExchange(&self->m_audioRunning, 0, 0) != 0;
	if (msg == MIM_DATA) {
		// Without an audio thread to drain the queue (output device missing)
		// keep the notes flowing rather than swallowing them.
		if (queued) MidiFifoPush(port, (DWORD)p1);
		else VstLiveMidiShort(port, (DWORD)p1);
		return;
	}
	if (msg == MIM_LONGDATA) {
		MIDIHDR* hdr = (MIDIHDR*)p1;
		if (!hdr) return;
		const int bytes = (int)hdr->dwBytesRecorded;
		if (bytes > 0) {
			if (queued) MidiFifoPushSysex(port, (const BYTE*)hdr->lpData, bytes);
			else VstLiveMidiSysex(port, (const unsigned char*)hdr->lpData, bytes);
			// Zero bytes means the driver is returning buffers on reset, and
			// re-adding then would fight midiInClose.
			for (int b = 0; b < SYSEX_BUFS_PER_PORT; ++b)
				if (&g_sysexBufs[port][b].hdr == hdr)
					InterlockedExchange(&g_sysexBufs[port][b].needAdd, 1);
		}
	}
}

void CVstHostDlg::StartMidi()
{
	StopMidi();
	MidiFifoInit();
	for (int p = 0; p < 3; ++p) {
		int sel = m_midiIn[p].GetCurSel();
		int dev = sel >= 0 ? (int)m_midiIn[p].GetItemData(sel) : -1;
		if (dev < 0 || midiInOpen(&m_midiHandles[p], dev, (DWORD_PTR)&MidiInProc,
			((DWORD_PTR)this) | p, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
			continue;
		// Without these buffers the driver has nowhere to put system
		// exclusive, so GS/XG resets and bank selects never arrive at all.
		for (int b = 0; b < SYSEX_BUFS_PER_PORT; ++b) {
			SysexBuf& s = g_sysexBufs[p][b];
			ZeroMemory(&s.hdr, sizeof(s.hdr));
			s.hdr.lpData = (LPSTR)s.data;
			s.hdr.dwBufferLength = SYSEX_SLOT_BYTES;
			s.in = m_midiHandles[p];
			InterlockedExchange(&s.needAdd, 0);
			if (midiInPrepareHeader(m_midiHandles[p], &s.hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR)
				continue;
			InterlockedExchange(&s.prepared, 1);
			midiInAddBuffer(m_midiHandles[p], &s.hdr, sizeof(MIDIHDR));
		}
		midiInStart(m_midiHandles[p]);
	}
}

void CVstHostDlg::StopMidi()
{
	for (int i = 0; i < 3; ++i) if (m_midiHandles[i]) {
		midiInStop(m_midiHandles[i]);
		midiInReset(m_midiHandles[i]); // returns every sysex buffer first
		for (int b = 0; b < SYSEX_BUFS_PER_PORT; ++b) {
			SysexBuf& s = g_sysexBufs[i][b];
			InterlockedExchange(&s.needAdd, 0);
			if (InterlockedExchange(&s.prepared, 0) == 1)
				midiInUnprepareHeader(m_midiHandles[i], &s.hdr, sizeof(MIDIHDR));
			s.in = NULL;
		}
		midiInClose(m_midiHandles[i]);
		m_midiHandles[i] = NULL;
	}
	MidiFifoClear();
	VstLiveAllNotesOff();
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
			MidiFifoDrain();
			MidiSysexRecycle();
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
	MidiFifoInit();
	m_audioThread = (HANDLE)_beginthreadex(NULL, 0, AudioThreadProc, this, 0, &tid);
	// This thread now carries the MIDI timing as well, and it only has four
	// 11 ms buffers of slack. TIME_CRITICAL was tried before and starved the
	// UI, so stay one step below it.
	if (m_audioThread) SetThreadPriority(m_audioThread, THREAD_PRIORITY_ABOVE_NORMAL);
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

void CVstHostDlg::OnTimer(UINT_PTR id)
{
	if (id == VST_ACTIVITY_TIMER) {
		m_wire.RefreshActivity();
		wchar_t sx[160] = {};
		int age = -1;
		CString line;
		if (VstLiveSysexInfo(sx, 160, &age) && age >= 0) {
			CString when;
			if (age < 1500)
				when = LL14(L"受信中", L"live", L"en cours", L"in corso", L"activo", L"수신 중",
					L"接收中", L"مباشر", L"сейчас", L"aktiv", L"ativo", L"actief", L"na żywo", L"canlı");
			else
				when.Format(L"%.1fs", age / 1000.0);
			line.Format(L"SysEx: %s  [%s]", sx, (LPCTSTR)when);
		}
		if (line != m_monitorText) {
			m_monitorText = line;
			if (m_monitor.GetSafeHwnd()) m_monitor.SetWindowText(line);
		}
		return;
	}
	CCustomBlurDialogBase::OnTimer(id);
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
	KillTimer(VST_ACTIVITY_TIMER);
	PcKeyReleaseAll();
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

