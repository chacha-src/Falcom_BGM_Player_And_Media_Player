#include "stdafx.h"
#include "ogg.h"
#include "VstHostDlg.h"
#include "VstMidiEngine.h"
#include "oggDlg.h"
#include "CMidiMonitorDlg.h"
#include "CCustomPopupMenu.h"
#include <process.h>
#include <math.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

extern __int64 playb;
extern int wavbit_sample_Hz;
extern CString filen;


namespace {

enum {
	IDC_VST_FILTER = 0x7e10,
	IDC_VST_WAV = 0x7e11,
	IDC_VST_VOL = 0x7e12,
	IDC_VST_THRU = 0x7e13,
	ID_VST_POP_RESCAN = 0xe710,
	ID_VST_POP_CLEAR = 0xe711,
	ID_VST_POP_SAVE = 0xe712,
	ID_VST_POP_EDITOR = 0xe713,
	ID_VST_POP_WAV = 0xe714,
	ID_VST_POP_MIDIMON = 0xe716,
	ID_VST_POP_THRU = 0xe717,
	// MIDI In 1/2/3: (none) plus per-device 1-16ch / 17-32ch (max 32 devices).
	ID_VST_POP_MIDI1_NONE = 0xe800,
	ID_VST_POP_MIDI1_FIRST = 0xe801, // + dev*2+bank  (64 ids)
	ID_VST_POP_MIDI2_NONE = 0xe841,
	ID_VST_POP_MIDI2_FIRST = 0xe842,
	ID_VST_POP_MIDI3_NONE = 0xe882,
	ID_VST_POP_MIDI3_FIRST = 0xe883,
	VST_MIDI_MENU_DEVS = 32,
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
static volatile LONG g_midiStopRecycle = 0;

static int ComboSelData(const CCustomComboBox& cb)
{
	const int phys = cb.CComboBox::GetCurSel();
	if (phys < 0) return -1;
	return (int)cb.GetItemData(phys);
}

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
		if (slot >= 0) {
			VstLiveMidiSysex(port, sysex, len);
			VstLiveTapPushSysex(port, sysex, len);
		} else {
			VstLiveMidiShort(port, msg);
			VstLiveTapPushShort(port, msg);
		}
	}
}

// Recycles the buffers the driver returned. A multimedia call, so it belongs
// on the audio thread rather than in the MIDI callback.
void MidiSysexRecycle()
{
	if (InterlockedCompareExchange(&g_midiStopRecycle, 0, 0)) return;
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

const DWORD VST_WIRE_MAGIC1 = 0x31525756; // "VWR1"
const DWORD VST_WIRE_MAGIC2 = 0x32525756; // "VWR2" midiThru + MIDI In 3 hardware

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
			LL14(L"・左のプラグインを右の Part 1～32 へドラッグします。読み込み中は「VST初期化中です。お待ちください」が出ます。", L"· Drag a plug-in from the left to Parts 1–32. While it loads, “Initializing VST. Please wait…” is shown.", L"· Glissez un plug-in vers les parties 1–32. Pendant le chargement : « Initialisation VST. Veuillez patienter… ».", L"· Trascina un plug-in nelle parti 1–32. Durante il caricamento: “Inizializzazione VST. Attendere…”.", L"· Arrastre un plug-in a las partes 1–32. Mientras carga: “Inicializando VST. Espere…”.", L"· 왼쪽 플러그인을 파트 1~32로 드래그합니다. 읽는 동안 “VST 초기화 중입니다. 잠시 기다려 주세요”가 나옵니다.", L"· 将左侧插件拖到声部 1–32。加载时显示“正在初始化 VST，请稍候”。", L"· اسحب إضافة إلى الأجزاء 1–32. أثناء التحميل تظهر «جارٍ تهيئة VST».", L"· Перетащите плагин в партии 1–32. Пока грузится: «Инициализация VST. Подождите…».", L"· Plugin links auf Part 1–32 ziehen. Beim Laden: „VST wird initialisiert. Bitte warten…“.", L"· Arraste um plug-in para as partes 1–32. Ao carregar: “A inicializar VST. Aguarde…”.", L"· Sleep een plug-in naar partijen 1–32. Tijdens laden: “VST wordt gestart. Even geduld…”.", L"· Przeciągnij wtyczkę do partii 1–32. Podczas wczytywania: „Inicjalizacja VST. Proszę czekać…”.", L"· Eklentiyi Bölüm 1–32'ye sürükleyin. Yüklenirken “VST başlatılıyor. Lütfen bekleyin…” görünür."),
			LL14(L"・Z列＝C4～、Q列＝C5～のPC鍵盤（コンボ／入力中は無効。Spaceで全ノートオフ）。", L"· PC keys: Z-row = C4…, Q-row = C5… (off while typing in a combo; Space = all notes off).", L"· Clavier PC: rangée Z = C4…, Q = C5… (inactif dans les listes; Espace = all notes off).", L"· Tastiera PC: fila Z = C4…, Q = C5… (disattiva nei combo; Spazio = all notes off).", L"· Teclado PC: fila Z = C4…, Q = C5… (inactivo en combos; Espacio = all notes off).", L"· PC 건반: Z열=C4~, Q열=C5~(콤보 입력 중 비활성, Space=올 노트 오프).", L"· PC 键盘：Z 行=C4…，Q 行=C5…（组合框输入时无效；空格全音符关闭）。", L"· لوحة PC: صف Z=C4… وصف Q=C5… (معطّل أثناء الكتابة؛ المسافة = إيقاف كل النغمات).", L"· ПК-клавиатура: ряд Z = C4…, Q = C5… (не в комбо; Пробел = all notes off).", L"· PC-Tastatur: Z-Reihe = C4…, Q = C5… (nicht in Combos; Leertaste = All Notes Off).", L"· Teclado PC: fila Z = C4…, Q = C5… (inativo em combos; Espaço = all notes off).", L"· PC-toetsenbord: Z-rij = C4…, Q = C5… (uit in combo's; Spatie = all notes off).", L"· Klawiatura PC: rząd Z = C4…, Q = C5… (wył. w combo; Spacja = all notes off).", L"· PC klavye: Z satırı = C4…, Q = C5… (kombo yazarken kapalı; Boşluk = all notes off)."),
			LL14(L"・一覧にはドロップで実際に載るものだけが出ます。LoopMash FX のようなエフェクトは出ません。確認が終わるまで載らないものは出しません。初回／再スキャンの D&D・発音確認が通ったものから左側へ順に出ます。同じ音源のコピーは1つにまとめます。<x86><x64>はビット数、<音色選択>はプラグイン側で音色を選ぶ音源、[M16ch]は16chマルチです。", L"· The list shows only plug-ins that actually drop onto a part. Effects such as LoopMash FX are omitted. Nothing that will be removed later is shown in passing. Each one that passes the drop and sound check is added on the left as it is confirmed (first open / rescan). Copies of the same module are merged. <x86><x64> is bitness, <patch> needs a sound picked in the plug-in, [M16ch] is 16-ch multi.", L"· La liste n'affiche que les plug-ins réellement déposables. Les effets (LoopMash FX…) sont omis. Rien n'apparaît pour disparaître ensuite. Chaque plug-in validé (glisser / son) apparaît à gauche au fur et à mesure. Les copies du même module sont fusionnées. <x86><x64> = bits, <timbre> = choisir dans le plug-in, [M16ch] = multi 16 ch.", L"· L'elenco mostra solo i plug-in che si possono trascinare. Gli effetti (LoopMash FX…) sono omessi. Nulla compare per poi sparire. Ogni plug-in confermato (trascina / suono) compare a sinistra man mano. Le copie dello stesso modulo sono unite. <x86><x64> = bit, <patch> = scegliere nel plug-in, [M16ch] = multi 16 ch.", L"· La lista solo muestra plug-ins que se pueden soltar. Los efectos (LoopMash FX…) se omiten. Nada aparece para luego desaparecer. Cada uno que pasa el arrastre y el sonido se añade a la izquierda al confirmarse. Las copias del mismo módulo se unen. <x86><x64> = bits, <timbre> = elegir en el plug-in, [M16ch] = multi 16 ch.", L"· 목록에는 실제로 드롭되는 플러그인만 표시됩니다. LoopMash FX 같은 이펙트는 나오지 않습니다. 나중에 사라질 항목은 잠깐도 올리지 않습니다. D&D·발음 확인이 된 것부터 왼쪽에 차례로 나옵니다. 같은 모듈의 복사본은 하나로 합칩니다. <x86><x64>는 비트, <음색선택>은 플러그인에서 고름, [M16ch]은 16ch 멀티.", L"· 列表只显示可拖放到声部的插件。LoopMash FX 一类效果器不列出。不会先显示再删掉。通过拖放和发音确认的会从左侧依次出现。同一模块的副本会合为一条。<x86><x64>为位数，<选音色>需在插件里选，[M16ch]为 16 声道多音色。", L"· تظهر القائمة فقط الإضافات القابلة للإفلات. تُستثنى المؤثرات مثل LoopMash FX. لا يظهر شيء ليُحذف لاحقًا. كل إضافة تجتاز الإفلات والصوت تُضاف إلى اليسار فور التأكيد. تُدمج نسخ نفس الوحدة. <x86><x64> البتات، <رقعة> اختيار في الإضافة، [M16ch] متعدد 16 قناة.", L"· В списке только плагины, которые реально ставятся на слот. Эффекты вроде LoopMash FX не показываются. Ничего не мелькает, чтобы потом исчезнуть. Прошедшие проверку D&D и звука появляются слева по мере подтверждения. Копии одного модуля сливаются. <x86><x64> — разрядность, <патч> — выбрать в плагине, [M16ch] — 16-канальный мульти.", L"· Die Liste zeigt nur Plug-ins, die sich ablegen lassen. Effekte wie LoopMash FX fehlen. Nichts erscheint nur, um danach zu verschwinden. Jedes nach D&D- und Klangprüfung Bestätigte erscheint links der Reihe nach. Kopien desselben Moduls werden zusammengefasst. <x86><x64> = Bits, <Patch> = im Plug-in wählen, [M16ch] = 16-ch Multi.", L"· A lista mostra só plug-ins que se podem largar. Efeitos como LoopMash FX ficam de fora. Nada aparece para depois desaparecer. Cada um que passa no largar e no som entra à esquerda à medida que é confirmado. Cópias do mesmo módulo juntam-se. <x86><x64> = bits, <timbre> = escolher no plug-in, [M16ch] = multi 16 ch.", L"· De lijst toont alleen plug-ins die echt te droppen zijn. Effecten zoals LoopMash FX ontbreken. Niets verschijnt even om daarna te verdwijnen. Elke plug-in die D&D en geluid haalt, komt links bij bevestiging. Kopieën van dezelfde module worden samengevoegd. <x86><x64> = bits, <patch> = kiezen in de plug-in, [M16ch] = 16ch multi.", L"· Lista pokazuje tylko wtyczki, które da się upuścić. Efekty jak LoopMash FX nie wchodzą. Nic nie miga, by zaraz zniknąć. Każda po teście D&D i dźwięku pojawia się po lewej w miarę potwierdzenia. Kopie tego samego modułu są scalane. <x86><x64> = bity, <barwa> = wybór w wtyczce, [M16ch] = multi 16 ch.", L"· Liste yalnızca gerçekten bırakılabilen eklentileri gösterir. LoopMash FX gibi efektler çıkmaz. Sonra silinecek öğeler ara ara gösterilmez. D&D ve ses kontrolünden geçenler solda sırayla çıkar. Aynı modülün kopyaları birleşir. <x86><x64> bit, <yama> eklentide seçim, [M16ch] 16ch multi."),
			LL14(L"・MIDI入力は3台＋「メイン再生スルー」。プレイヤーで鳴っている音（形式不問）をホストへ回し、プレイヤー本体の出力は止まります。MIDIならSMFも鍵盤と合流します。各機器は「1-16ch」（パート1–16）と「17-32ch」（パート17–32）で選べます。",
				L"· Three MIDI inputs plus Main-playback thru. Player audio (any format) is routed here and the player's own speakers go silent. MIDI SMF also merges with the keyboards. Each device is listed as 1-16ch (parts 1–16) and 17-32ch (parts 17–32).",
				L"· Trois entrées MIDI + traversée lecture. Chaque périphérique : 1-16ch (parties 1–16) et 17-32ch (17–32).",
				L"· Tre ingressi MIDI + pass-through riproduzione. Ogni dispositivo: 1-16ch (parti 1–16) e 17-32ch (17–32).",
				L"· Tres entradas MIDI + paso de reproducción. Cada dispositivo: 1-16ch (partes 1–16) y 17-32ch (17–32).",
				L"· MIDI 입력 3대 + 메인 재생 스루. 각 장치는 1-16ch(파트 1–16)와 17-32ch(17–32).",
				L"· 三个 MIDI 输入加上「主播放直通」。每台设备有 1-16ch（声部1–16）和 17-32ch（17–32）。",
				L"· ثلاثة مداخل MIDI + تمرير التشغيل. كل جهاز: 1-16ch (1–16) و17-32ch (17–32).",
				L"· Три MIDI-входа + сквозн. воспр. Каждое устройство: 1-16ch (1–16) и 17-32ch (17–32).",
				L"· Drei MIDI-Eingänge plus Wiedergabe-Thru. Jedes Gerät: 1-16ch (Parts 1–16) und 17-32ch (17–32).",
				L"· Três entradas MIDI + passagem da reprodução. Cada dispositivo: 1-16ch (1–16) e 17-32ch (17–32).",
				L"· Drie MIDI-ingangen plus weergave-thru. Elk apparaat: 1-16ch (1–16) en 17-32ch (17–32).",
				L"· Trzy wejścia MIDI + przelot odtwarzania. Każde urządzenie: 1-16ch (1–16) i 17-32ch (17–32).",
				L"· Üç MIDI girişi + ana çalma thru. Her aygıt: 1-16ch (1–16) ve 17-32ch (17–32)."),
			LL14(L"・MIDIモニタとつながります。ホストの鍵盤・MIDI入力がモニタに出ます。モニタのミニ鍵盤やスライダーはここのプラグインへ送られます。右クリックからモニタを開けます。",
				L"· Linked to the MIDI monitor: host keyboard and MIDI in appear there. Monitor mini keys and sliders go to these plug-ins. Right-click to open the monitor.",
				L"· Lie au moniteur MIDI : clavier et MIDI in s'y affichent. Mini clavier et curseurs du moniteur vont aux plug-ins. Clic droit pour ouvrir le moniteur.",
				L"· Collegato al monitor MIDI: tastiera e MIDI in appaiono li. Mini tasti e slider del monitor vanno ai plug-in. Tasto destro per aprire il monitor.",
				L"· Enlazado al monitor MIDI: teclado y MIDI in salen alli. Mini teclas y deslizadores van a los plug-ins. Clic derecho para abrir el monitor.",
				L"· MIDI 모니터와 연결됩니다. 호스트 건반·MIDI 입력이 모니터에 나옵니다. 모니터 미니 건반과 슬라이더는 여기 플러그인으로 갑니다. 우클릭으로 모니터를 엽니다.",
				L"· 与 MIDI 监视器相连。主机键盘和 MIDI 输入显示在监视器上。监视器迷你键盘和滑块送到这里的插件。右键可打开监视器。",
				L"· مرتبط بمراقب MIDI. لوحة المضيف وMIDI تظهر هناك. المفاتيح الصغيرة والمنزلقات إلى الإضافات هنا. يمين لفتح المراقب.",
				L"· Связан с MIDI-монитором: клавиатура и MIDI in видны там. Мини-клавиши и ползунки монитора идут в плагины. ПКМ открывает монитор.",
				L"· Mit dem MIDI-Monitor verbunden: Tastatur und MIDI-In erscheinen dort. Mini-Tasten und Schieber gehen an die Plug-ins. Rechtsklick oeffnet den Monitor.",
				L"· Ligado ao monitor MIDI: teclado e MIDI in aparecem la. Mini teclas e controlos vao aos plug-ins. Clique direito abre o monitor.",
				L"· Gekoppeld aan de MIDI-monitor: toetsenbord en MIDI-in verschijnen daar. Mini-toetsen en schuiven gaan naar de plug-ins. Rechtsklik opent de monitor.",
				L"· Polaczony z monitorem MIDI: klawiatura i MIDI in sa tam. Mini klawisze i suwaki ida do wtyczek. PPM otwiera monitor.",
				L"· MIDI izleyiciye bagli: host klavye ve MIDI girisi orada gorunur. Mini tuslar ve kaydiricilar eklentilere gider. Sag tik izleyiciyi acar."),
			LL14(L"・配線とデバイス設定はプリセットへ保存できます。音量スライダーはこのホストの出力（WAV出力も含む）です。", L"· Wiring and device choices are stored in presets. The volume slider is this host's output (including WAV out).", L"· Le câblage et les périphériques sont enregistrés.", L"· Cablaggio e dispositivi sono salvati nei preset.", L"· El cableado y los dispositivos se guardan.", L"· 배선과 장치 선택은 프리셋에 저장됩니다.", L"· 连线和设备选择可保存到预设。", L"· تُحفظ التوصيلات والأجهزة في الإعدادات.", L"· Схема и устройства сохраняются в пресетах.", L"· Verdrahtung und Geräte werden im Preset gespeichert.", L"· Ligações e dispositivos são guardados.", L"· Bedrading en apparaten worden opgeslagen.", L"· Okablowanie i urządzenia zapisują się w presetach.", L"· Bağlantılar ve aygıtlar ön ayarlara kaydedilir."),
			LL14(L"・右クリックで再スキャン、スロット解除、保存ができます。", L"· Right-click to rescan, clear a slot, or save.", L"· Clic droit: rescanner, effacer ou enregistrer.", L"· Clic destro: scansione, azzera o salva.", L"· Clic derecho: reescanear, borrar o guardar.", L"· 우클릭으로 재검색, 슬롯 해제, 저장합니다.", L"· 右键可重新扫描、清除插槽或保存。", L"· انقر يميناً للمسح أو الإزالة أو الحفظ.", L"· ПКМ: сканирование, очистка или сохранение.", L"· Rechtsklick: scannen, Slot leeren oder speichern.", L"· Clique direito: procurar, limpar ou guardar.", L"· Rechtsklik: scannen, wissen of opslaan.", L"· PPM: skanowanie, czyszczenie lub zapis.", L"· Sağ tık: tara, slotu temizle veya kaydet."),
			LL14(L"・SOUND Canvas VA / SGP2 等のマルチは1スロットで16ch。MIDIチャンネルはそのまま送られます。", L"· Multi-timbral plugs (SOUND Canvas VA / SGP2) take one slot for 16 channels.", L"· Les multi (SOUND Canvas VA / SGP2) utilisent 1 slot pour 16 canaux.", L"· I multi (SOUND Canvas VA / SGP2) usano 1 slot per 16 canali.", L"· Los multi (SOUND Canvas VA / SGP2) usan 1 ranura para 16 canales.", L"· SOUND Canvas VA/SGP2 멀티는 1슬롯으로 16ch.", L"· SOUND Canvas VA/SGP2 等多音色占1槽覆盖16声道。", L"· الآلات المتعددة (SOUND Canvas VA/SGP2) تشغل فتحة واحدة لـ16 قناة.", L"· Мульти (SOUND Canvas VA/SGP2) — один слот на 16 каналов.", L"· Multi (SOUND Canvas VA/SGP2): ein Slot für 16 Kanäle.", L"· Multi (SOUND Canvas VA/SGP2) usam 1 slot para 16 canais.", L"· Multi (SOUND Canvas VA/SGP2): één slot voor 16 kanalen.", L"· Multi (SOUND Canvas VA/SGP2): jeden slot na 16 kanałów.", L"· Multi (SOUND Canvas VA/SGP2): 16 kanal için tek slot.")
		};
		for (int i = 0; i < (int)_countof(lines); ++i) {
			const int lh = (i <= 2) ? 52 : 38;
			CRect tr(x, y, hp.rc.right - x, y + lh);
			dc.DrawText(lines[i], &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
			y += lh;
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
	if (GetSafeHwnd())
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
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
			else if (m_owner && m_owner->PluginIsMulti(m_slots[i]))
				s.Format(L"%02d  ch%d  %s", i + 1, (i % 16) + 1, (LPCTSTR)name);
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
	CCC_DrawInwomanOnClient(&dc, m_hWnd);
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
	menu.AddSeparator();
	{
		const int cur0 = m_owner->m_midiIn[0].GetCurSel() >= 0
			? (int)m_owner->m_midiIn[0].GetItemData(m_owner->m_midiIn[0].GetCurSel()) : -1;
		const int cur1 = m_owner->m_midiIn[1].GetCurSel() >= 0
			? (int)m_owner->m_midiIn[1].GetItemData(m_owner->m_midiIn[1].GetCurSel()) : -1;
		const int cur2 = m_owner->m_midiIn[2].GetCurSel() >= 0
			? (int)m_owner->m_midiIn[2].GetItemData(m_owner->m_midiIn[2].GetCurSel()) : -1;
		const int thruOn = (m_owner->m_thru.GetSafeHwnd() && m_owner->m_thru.GetCurSel() >= 0
			&& (int)m_owner->m_thru.GetItemData(m_owner->m_thru.GetCurSel()) == -2) ? 1 : 0;
		const wchar_t* none = LL14(L"(なし)", L"(None)", L"(Aucun)", L"(Nessuno)", L"(Ninguno)", L"(없음)", L"(无)", L"(بلا)",
			L"(Нет)", L"(Keine)", L"(Nenhum)", L"(Geen)", L"(Brak)", L"(Yok)");
		CCustomPopupMenu* midiRoot = menu.AddSubMenu(LL14(L"MIDI入力", L"MIDI In", L"Entrée MIDI", L"Ingresso MIDI",
			L"Entrada MIDI", L"MIDI 입력", L"MIDI 输入", L"دخل MIDI", L"MIDI-вход", L"MIDI-Eingang",
			L"Entrada MIDI", L"MIDI-ingang", L"Wejście MIDI", L"MIDI girişi"));
		CCustomPopupMenu* in1 = midiRoot ? midiRoot->AddSubMenu(LL14(L"MIDI入力 1", L"MIDI In 1", L"Entrée MIDI 1", L"Ingresso MIDI 1",
			L"Entrada MIDI 1", L"MIDI 입력 1", L"MIDI 输入 1", L"دخل MIDI 1", L"MIDI-вход 1", L"MIDI-Eingang 1",
			L"Entrada MIDI 1", L"MIDI-ingang 1", L"Wejście MIDI 1", L"MIDI girişi 1"),
			LL14(L"パート1–16（1-16ch）か 17–32（17-32ch）へ送ります", L"Send to parts 1–16 (1-16ch) or 17–32 (17-32ch)",
				L"Envoie vers les parties 1–16 ou 17–32", L"Invia alle parti 1–16 o 17–32", L"Envía a las partes 1–16 o 17–32",
				L"파트 1–16 또는 17–32로 보냅니다", L"送到声部 1–16 或 17–32", L"يرسل إلى الأجزاء 1–16 أو 17–32",
				L"На партии 1–16 или 17–32", L"An Parts 1–16 oder 17–32", L"Envia para as partes 1–16 ou 17–32",
				L"Naar partijen 1–16 of 17–32", L"Do partii 1–16 lub 17–32", L"Bölüm 1–16 veya 17–32'ye gönderir")) : NULL;
		CCustomPopupMenu* in2 = midiRoot ? midiRoot->AddSubMenu(LL14(L"MIDI入力 2", L"MIDI In 2", L"Entrée MIDI 2", L"Ingresso MIDI 2",
			L"Entrada MIDI 2", L"MIDI 입력 2", L"MIDI 输入 2", L"دخل MIDI 2", L"MIDI-вход 2", L"MIDI-Eingang 2",
			L"Entrada MIDI 2", L"MIDI-ingang 2", L"Wejście MIDI 2", L"MIDI girişi 2"),
			LL14(L"パート1–16（1-16ch）か 17–32（17-32ch）へ送ります", L"Send to parts 1–16 (1-16ch) or 17–32 (17-32ch)",
				L"Envoie vers les parties 1–16 ou 17–32", L"Invia alle parti 1–16 o 17–32", L"Envía a las partes 1–16 o 17–32",
				L"파트 1–16 또는 17–32로 보냅니다", L"送到声部 1–16 或 17–32", L"يرسل إلى الأجزاء 1–16 أو 17–32",
				L"На партии 1–16 или 17–32", L"An Parts 1–16 oder 17–32", L"Envia para as partes 1–16 ou 17–32",
				L"Naar partijen 1–16 of 17–32", L"Do partii 1–16 lub 17–32", L"Bölüm 1–16 veya 17–32'ye gönderir")) : NULL;
		CCustomPopupMenu* in3 = midiRoot ? midiRoot->AddSubMenu(LL14(L"MIDI入力 3", L"MIDI In 3", L"Entrée MIDI 3", L"Ingresso MIDI 3",
			L"Entrada MIDI 3", L"MIDI 입력 3", L"MIDI 输入 3", L"دخل MIDI 3", L"MIDI-вход 3", L"MIDI-Eingang 3",
			L"Entrada MIDI 3", L"MIDI-ingang 3", L"Wejście MIDI 3", L"MIDI girişi 3"),
			LL14(L"3台目の鍵盤。1・2と同じ帯域（1-16ch / 17-32ch）へ送れます", L"Third keyboard. Same bands as 1 and 2 (1-16ch / 17-32ch)",
				L"3e clavier. Mêmes bandes que 1 et 2", L"Terza tastiera. Stesse fasce di 1 e 2", L"Tercer teclado. Mismas bandas que 1 y 2",
				L"세 번째 건반. 1·2와 같은 대역", L"第三键盘。与 1、2 相同的带", L"لوحة ثالثة. نفس نطاقَي 1 و2",
				L"Третья клавиатура. Те же полосы, что у 1 и 2", L"Dritte Tastatur. Gleiche Bänder wie 1 und 2",
				L"Terceiro teclado. Mesmas faixas que 1 e 2", L"Derde toetsenbord. Dezelfde banden als 1 en 2",
				L"Trzecia klawiatura. Te same pasma co 1 i 2", L"Üçüncü klavye. 1 ve 2 ile aynı bantlar")) : NULL;
		if (in1) in1->AddCheck(ID_VST_POP_MIDI1_NONE, none, cur0 == -1);
		if (in2) in2->AddCheck(ID_VST_POP_MIDI2_NONE, none, cur1 == -1);
		if (in3) in3->AddCheck(ID_VST_POP_MIDI3_NONE, none, cur2 == -1);
		UINT nDev = midiInGetNumDevs();
		if (nDev > (UINT)VST_MIDI_MENU_DEVS) nDev = (UINT)VST_MIDI_MENU_DEVS;
		for (UINT i = 0; i < nDev; ++i) {
			MIDIINCAPS caps = {};
			if (midiInGetDevCaps(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
			if (in1) {
				CCustomPopupMenu* d = in1->AddSubMenu(caps.szPname);
				if (d) {
					d->AddCheck(ID_VST_POP_MIDI1_FIRST + i * 2, L"1-16ch", cur0 == (int)i);
					d->AddCheck(ID_VST_POP_MIDI1_FIRST + i * 2 + 1, L"17-32ch", cur0 == (int)(i | 0x10000));
				}
			}
			if (in2) {
				CCustomPopupMenu* d = in2->AddSubMenu(caps.szPname);
				if (d) {
					d->AddCheck(ID_VST_POP_MIDI2_FIRST + i * 2, L"1-16ch", cur1 == (int)i);
					d->AddCheck(ID_VST_POP_MIDI2_FIRST + i * 2 + 1, L"17-32ch", cur1 == (int)(i | 0x10000));
				}
			}
			if (in3) {
				CCustomPopupMenu* d = in3->AddSubMenu(caps.szPname);
				if (d) {
					d->AddCheck(ID_VST_POP_MIDI3_FIRST + i * 2, L"1-16ch", cur2 == (int)i);
					d->AddCheck(ID_VST_POP_MIDI3_FIRST + i * 2 + 1, L"17-32ch", cur2 == (int)(i | 0x10000));
				}
			}
		}
		if (midiRoot) {
			midiRoot->AddSeparator();
			midiRoot->AddCheck(ID_VST_POP_THRU, LL14(L"メイン再生スルー", L"Main-playback thru", L"Traversée lecture",
				L"Pass-through riproduzione", L"Paso de reproducción", L"메인 재생 스루", L"主播放直通", L"تمرير التشغيل الرئيسي",
				L"Сквозн. осн. воспр.", L"Hauptwiedergabe-Thru", L"Passagem da reprodução", L"Hoofdweergave-thru",
				L"Przelot odtwarzania", L"Ana çalma thru"), thruOn != 0);
		}
	}

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
	else if (cmd == ID_VST_POP_THRU) {
		if (m_owner->m_thru.GetSafeHwnd()) {
			const int on = (m_owner->m_thru.GetCurSel() >= 0
				&& (int)m_owner->m_thru.GetItemData(m_owner->m_thru.GetCurSel()) == -2) ? 1 : 0;
			m_owner->m_thru.SetCurSel(on ? 0 : 1);
			m_owner->OnDeviceChange();
		}
	}
	else if (cmd == ID_VST_POP_MIDI1_NONE || cmd == ID_VST_POP_MIDI2_NONE || cmd == ID_VST_POP_MIDI3_NONE ||
		(cmd >= ID_VST_POP_MIDI1_FIRST && cmd < ID_VST_POP_MIDI1_FIRST + VST_MIDI_MENU_DEVS * 2) ||
		(cmd >= ID_VST_POP_MIDI2_FIRST && cmd < ID_VST_POP_MIDI2_FIRST + VST_MIDI_MENU_DEVS * 2) ||
		(cmd >= ID_VST_POP_MIDI3_FIRST && cmd < ID_VST_POP_MIDI3_FIRST + VST_MIDI_MENU_DEVS * 2)) {
		int combo = -1, data = -1;
		if (cmd == ID_VST_POP_MIDI1_NONE) combo = 0;
		else if (cmd == ID_VST_POP_MIDI2_NONE) combo = 1;
		else if (cmd == ID_VST_POP_MIDI3_NONE) combo = 2;
		else if (cmd >= ID_VST_POP_MIDI1_FIRST && cmd < ID_VST_POP_MIDI1_FIRST + VST_MIDI_MENU_DEVS * 2) {
			combo = 0;
			const int idx = (int)(cmd - ID_VST_POP_MIDI1_FIRST);
			data = (idx / 2) | ((idx & 1) << 16);
		} else if (cmd >= ID_VST_POP_MIDI2_FIRST && cmd < ID_VST_POP_MIDI2_FIRST + VST_MIDI_MENU_DEVS * 2) {
			combo = 1;
			const int idx = (int)(cmd - ID_VST_POP_MIDI2_FIRST);
			data = (idx / 2) | ((idx & 1) << 16);
		} else {
			combo = 2;
			const int idx = (int)(cmd - ID_VST_POP_MIDI3_FIRST);
			data = (idx / 2) | ((idx & 1) << 16);
		}
		int sel = 0;
		for (int i = 0; i < m_owner->m_midiIn[combo].GetCount(); ++i)
			if ((int)m_owner->m_midiIn[combo].GetItemData(i) == data) { sel = i; break; }
		m_owner->m_midiIn[combo].SetCurSel(sel);
		m_owner->OnDeviceChange();
	}
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
	menu.AddCommand(ID_VST_POP_WAV, LL14(L"WAVへ書き出す", L"Write to WAV", L"Écrire en WAV", L"Scrivi su WAV", L"Escribir a WAV",
		L"WAV로 쓰기", L"导出为 WAV", L"كتابة إلى WAV", L"Записать в WAV", L"Als WAV schreiben", L"Escrever para WAV",
		L"Naar WAV schrijven", L"Zapisz do WAV", L"WAV olarak yaz"));
	menu.AddCommand(ID_VST_POP_MIDIMON, LL14(L"MIDIモニタを開く", L"Open MIDI monitor", L"Ouvrir le moniteur MIDI", L"Apri monitor MIDI",
		L"Abrir monitor MIDI", L"MIDI 모니터 열기", L"打开 MIDI 监视器", L"فتح مراقب MIDI", L"Открыть MIDI-монитор",
		L"MIDI-Monitor öffnen", L"Abrir monitor MIDI", L"MIDI-monitor openen", L"Otwórz monitor MIDI", L"MIDI izleyiciyi aç"));
	menu.AddSeparator();
	{
		const int cur0 = m_owner->m_midiIn[0].GetCurSel() >= 0
			? (int)m_owner->m_midiIn[0].GetItemData(m_owner->m_midiIn[0].GetCurSel()) : -1;
		const int cur1 = m_owner->m_midiIn[1].GetCurSel() >= 0
			? (int)m_owner->m_midiIn[1].GetItemData(m_owner->m_midiIn[1].GetCurSel()) : -1;
		const int cur2 = m_owner->m_midiIn[2].GetCurSel() >= 0
			? (int)m_owner->m_midiIn[2].GetItemData(m_owner->m_midiIn[2].GetCurSel()) : -1;
		const int thruOn = (m_owner->m_thru.GetSafeHwnd() && m_owner->m_thru.GetCurSel() >= 0
			&& (int)m_owner->m_thru.GetItemData(m_owner->m_thru.GetCurSel()) == -2) ? 1 : 0;
		const wchar_t* none = LL14(L"(なし)", L"(None)", L"(Aucun)", L"(Nessuno)", L"(Ninguno)", L"(없음)", L"(无)", L"(بلا)",
			L"(Нет)", L"(Keine)", L"(Nenhum)", L"(Geen)", L"(Brak)", L"(Yok)");
		CCustomPopupMenu* midiRoot = menu.AddSubMenu(LL14(L"MIDI入力", L"MIDI In", L"Entrée MIDI", L"Ingresso MIDI",
			L"Entrada MIDI", L"MIDI 입력", L"MIDI 输入", L"دخل MIDI", L"MIDI-вход", L"MIDI-Eingang",
			L"Entrada MIDI", L"MIDI-ingang", L"Wejście MIDI", L"MIDI girişi"));
		CCustomPopupMenu* in1 = midiRoot ? midiRoot->AddSubMenu(LL14(L"MIDI入力 1", L"MIDI In 1", L"Entrée MIDI 1", L"Ingresso MIDI 1",
			L"Entrada MIDI 1", L"MIDI 입력 1", L"MIDI 输入 1", L"دخل MIDI 1", L"MIDI-вход 1", L"MIDI-Eingang 1",
			L"Entrada MIDI 1", L"MIDI-ingang 1", L"Wejście MIDI 1", L"MIDI girişi 1")) : NULL;
		CCustomPopupMenu* in2 = midiRoot ? midiRoot->AddSubMenu(LL14(L"MIDI入力 2", L"MIDI In 2", L"Entrée MIDI 2", L"Ingresso MIDI 2",
			L"Entrada MIDI 2", L"MIDI 입력 2", L"MIDI 输入 2", L"دخل MIDI 2", L"MIDI-вход 2", L"MIDI-Eingang 2",
			L"Entrada MIDI 2", L"MIDI-ingang 2", L"Wejście MIDI 2", L"MIDI girişi 2")) : NULL;
		CCustomPopupMenu* in3 = midiRoot ? midiRoot->AddSubMenu(LL14(L"MIDI入力 3", L"MIDI In 3", L"Entrée MIDI 3", L"Ingresso MIDI 3",
			L"Entrada MIDI 3", L"MIDI 입력 3", L"MIDI 输入 3", L"دخل MIDI 3", L"MIDI-вход 3", L"MIDI-Eingang 3",
			L"Entrada MIDI 3", L"MIDI-ingang 3", L"Wejście MIDI 3", L"MIDI girişi 3")) : NULL;
		if (in1) in1->AddCheck(ID_VST_POP_MIDI1_NONE, none, cur0 == -1);
		if (in2) in2->AddCheck(ID_VST_POP_MIDI2_NONE, none, cur1 == -1);
		if (in3) in3->AddCheck(ID_VST_POP_MIDI3_NONE, none, cur2 == -1);
		UINT nDev = midiInGetNumDevs();
		if (nDev > (UINT)VST_MIDI_MENU_DEVS) nDev = (UINT)VST_MIDI_MENU_DEVS;
		for (UINT i = 0; i < nDev; ++i) {
			MIDIINCAPS caps = {};
			if (midiInGetDevCaps(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
			if (in1) {
				CCustomPopupMenu* d = in1->AddSubMenu(caps.szPname);
				if (d) {
					d->AddCheck(ID_VST_POP_MIDI1_FIRST + i * 2, L"1-16ch", cur0 == (int)i);
					d->AddCheck(ID_VST_POP_MIDI1_FIRST + i * 2 + 1, L"17-32ch", cur0 == (int)(i | 0x10000));
				}
			}
			if (in2) {
				CCustomPopupMenu* d = in2->AddSubMenu(caps.szPname);
				if (d) {
					d->AddCheck(ID_VST_POP_MIDI2_FIRST + i * 2, L"1-16ch", cur1 == (int)i);
					d->AddCheck(ID_VST_POP_MIDI2_FIRST + i * 2 + 1, L"17-32ch", cur1 == (int)(i | 0x10000));
				}
			}
			if (in3) {
				CCustomPopupMenu* d = in3->AddSubMenu(caps.szPname);
				if (d) {
					d->AddCheck(ID_VST_POP_MIDI3_FIRST + i * 2, L"1-16ch", cur2 == (int)i);
					d->AddCheck(ID_VST_POP_MIDI3_FIRST + i * 2 + 1, L"17-32ch", cur2 == (int)(i | 0x10000));
				}
			}
		}
		if (midiRoot) {
			midiRoot->AddSeparator();
			midiRoot->AddCheck(ID_VST_POP_THRU, LL14(L"メイン再生スルー", L"Main-playback thru", L"Traversée lecture",
				L"Pass-through riproduzione", L"Paso de reproducción", L"메인 재생 스루", L"主播放直通", L"تمرير التشغيل الرئيسي",
				L"Сквозн. осн. воспр.", L"Hauptwiedergabe-Thru", L"Passagem da reprodução", L"Hoofdweergave-thru",
				L"Przelot odtwarzania", L"Ana çalma thru"), thruOn != 0);
		}
	}
	ClientToScreen(&pt);
	UINT cmd = menu.Track(pt, m_owner ? (CWnd*)m_owner : GetParent());
	if (!m_owner) return;
	if (cmd == ID_VST_POP_RESCAN) m_owner->OnRescan();
	else if (cmd == ID_VST_POP_CLEAR && slot >= 0) ClearSlot(slot);
	else if (cmd == ID_VST_POP_SAVE) m_owner->OnSave();
	else if (cmd == ID_VST_POP_WAV) m_owner->OnWavClick();
	else if (cmd == ID_VST_POP_MIDIMON) {
		extern COggDlg* og;
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (og->m_MidiMonitorDlg && ::IsWindow(og->m_MidiMonitorDlg->GetSafeHwnd())) {
				og->m_MidiMonitorDlg->ShowWindow(SW_SHOW);
				og->m_MidiMonitorDlg->SetForegroundWindow();
			} else
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 3, 0);
		}
	}
	else if (cmd == ID_VST_POP_EDITOR && owner >= 0 && m_slots[owner] >= 0)
		VstLiveEditorOpen(owner + 1);
	else if (cmd == ID_VST_POP_THRU) {
		if (m_owner->m_thru.GetSafeHwnd()) {
			const int on = (m_owner->m_thru.GetCurSel() >= 0
				&& (int)m_owner->m_thru.GetItemData(m_owner->m_thru.GetCurSel()) == -2) ? 1 : 0;
			m_owner->m_thru.SetCurSel(on ? 0 : 1);
			m_owner->OnDeviceChange();
		}
	}
	else if (cmd == ID_VST_POP_MIDI1_NONE || cmd == ID_VST_POP_MIDI2_NONE || cmd == ID_VST_POP_MIDI3_NONE ||
		(cmd >= ID_VST_POP_MIDI1_FIRST && cmd < ID_VST_POP_MIDI1_FIRST + VST_MIDI_MENU_DEVS * 2) ||
		(cmd >= ID_VST_POP_MIDI2_FIRST && cmd < ID_VST_POP_MIDI2_FIRST + VST_MIDI_MENU_DEVS * 2) ||
		(cmd >= ID_VST_POP_MIDI3_FIRST && cmd < ID_VST_POP_MIDI3_FIRST + VST_MIDI_MENU_DEVS * 2)) {
		int combo = -1, data = -1;
		if (cmd == ID_VST_POP_MIDI1_NONE) combo = 0;
		else if (cmd == ID_VST_POP_MIDI2_NONE) combo = 1;
		else if (cmd == ID_VST_POP_MIDI3_NONE) combo = 2;
		else if (cmd >= ID_VST_POP_MIDI1_FIRST && cmd < ID_VST_POP_MIDI1_FIRST + VST_MIDI_MENU_DEVS * 2) {
			combo = 0;
			const int idx = (int)(cmd - ID_VST_POP_MIDI1_FIRST);
			data = (idx / 2) | ((idx & 1) << 16);
		} else if (cmd >= ID_VST_POP_MIDI2_FIRST && cmd < ID_VST_POP_MIDI2_FIRST + VST_MIDI_MENU_DEVS * 2) {
			combo = 1;
			const int idx = (int)(cmd - ID_VST_POP_MIDI2_FIRST);
			data = (idx / 2) | ((idx & 1) << 16);
		} else {
			combo = 2;
			const int idx = (int)(cmd - ID_VST_POP_MIDI3_FIRST);
			data = (idx / 2) | ((idx & 1) << 16);
		}
		int sel = 0;
		for (int i = 0; i < m_owner->m_midiIn[combo].GetCount(); ++i)
			if ((int)m_owner->m_midiIn[combo].GetItemData(i) == data) { sel = i; break; }
		m_owner->m_midiIn[combo].SetCurSel(sel);
		m_owner->OnDeviceChange();
	}
}

CVstHostDlg* g_vstHostDlg = NULL;
IMPLEMENT_DYNAMIC(CVstHostDlg, CCustomBlurDialogBase)

CVstHostDlg::CVstHostDlg(CWnd* parent)
	: CCustomBlurDialogBase(IDD, parent), m_presetCount(0), m_waveOut(NULL),
	  m_audioEvent(NULL), m_audioStop(NULL), m_audioThread(NULL), m_audioRunning(0),
	  m_wavFile(INVALID_HANDLE_VALUE), m_wavOn(0), m_wavBytes(0), m_volLevel(100)
{
	memset(m_presets, 0, sizeof(m_presets));
	memset(m_slots, -1, sizeof(m_slots));
	memset(m_midiHandles, 0, sizeof(m_midiHandles));
	memset(m_midiDestMask, 0, sizeof(m_midiDestMask));
	memset(m_midiF5Port, 0xFF, sizeof(m_midiF5Port));
	memset(m_pcHeldNote, 0, sizeof(m_pcHeldNote));
	InitializeCriticalSection(&m_wavLock);
}

CVstHostDlg::~CVstHostDlg()
{
	DeleteCriticalSection(&m_wavLock);
}

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
	ON_BN_CLICKED(IDC_VST_WAV, OnWavClick)
	ON_CBN_SELCHANGE(IDC_VST_MIDI1, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_MIDI2, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_MIDI3, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_THRU, OnDeviceChange)
	ON_CBN_SELCHANGE(IDC_VST_OUT, OnDeviceChange)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_ACTIVATE()
	ON_WM_LBUTTONDOWN()
	ON_WM_HSCROLL()
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
	m_pluginFilter.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
		CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_OWNERDRAWVARIABLE | CBS_HASSTRINGS,
		CRect(8, 72, 210, 300), this, IDC_VST_FILTER);
	m_pluginFilter.SetFont(GetFont());
	m_pluginFilter.SetAeroMode(FALSE);
	m_pluginFilter.AddString(LL14(L"すべて", L"All plug-ins", L"Tous", L"Tutti", L"Todos", L"모든 플러그인", L"全部插件",
		L"كل الإضافات", L"Все плагины", L"Alle Plug-ins", L"Todos", L"Alle plug-ins", L"Wszystkie", L"Tüm eklentiler"));
	m_pluginFilter.SetCurSel(0);
	m_thru.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
		CBS_DROPDOWNLIST | CBS_OWNERDRAWVARIABLE | CBS_HASSTRINGS,
		CRect(8, 72, 210, 300), this, IDC_VST_THRU);
	m_thru.SetFont(GetFont());
	m_thru.SetAeroMode(FALSE);

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
		LL14(L"メイン再生", L"Main play", L"Lecture principale", L"Riproduzione principale", L"Reproducción principal", L"메인 재생",
			L"主播放", L"التشغيل الرئيسي", L"Основное воспр.", L"Hauptwiedergabe", L"Reprodução principal", L"Hoofdweergave",
			L"Odtwarzanie główne", L"Ana çalma"),
		LL14(L"音声出力", L"Audio out", L"Sortie audio", L"Uscita audio", L"Salida de audio", L"오디오 출력",
			L"音频输出", L"خرج الصوت", L"Аудиовыход", L"Audioausgang", L"Saída de áudio", L"Audio-uitgang",
			L"Wyjście audio", L"Ses çıkışı"),
		LL14(L"音量", L"Volume", L"Volume", L"Volume", L"Volumen", L"음량",
			L"音量", L"مستوى الصوت", L"Громкость", L"Lautstärke", L"Volume", L"Volume",
			L"Głośność", L"Ses"),
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
	m_wav.Create(LL14(L"WAV出力", L"WAV out", L"Sortie WAV", L"Uscita WAV", L"Salida WAV", L"WAV 출력",
		L"WAV 输出", L"خرج WAV", L"Выход WAV", L"WAV-Ausgabe", L"Saída WAV", L"WAV-uitgang",
		L"Wyjście WAV", L"WAV çıkışı"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(0, 0, 10, 10), this, IDC_VST_WAV);
	m_wav.SetFont(GetFont());
	m_wav.SetAeroMode(FALSE);
	m_wav.SetFlat(TRUE);
	m_wav.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_vol.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
		CRect(0, 0, 10, 10), this, IDC_VST_VOL);
	m_vol.SetAeroMode(FALSE);
	m_vol.SetRange(0, 100);
	{
		int v = savedata.vstHostVol;
		if (v < 0 || v > 100) v = 100;
		m_vol.SetPos(v);
		InterlockedExchange(&m_volLevel, v);
	}
	m_volPct.Create(L"100%", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
		CRect(0, 0, 40, 18), this);
	m_volPct.SetFont(GetFont());
	ApplyVolUi();

	LayoutHelpBtn();
	CCC_CaptionLayout(m_hWnd);

	FillDevices();
	LoadPresets();
	GetClientRect(&rc);
	LayoutChildren(rc.Width(), rc.Height());
	ShowWindow(SW_SHOW);
	UpdateWindow();
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
		m_tooltip.AddTool(&m_midiIn[0], LL14(L"鍵盤などの MIDI 入力。同じ機器でも「1-16ch」（パート1–16）と「17-32ch」（パート17–32）を選べます。両方に同じ機器を当てると Super-MPU のように 32 パートへ届きます",
			L"MIDI input. Each device is listed as 1-16ch (parts 1–16) and 17-32ch (parts 17–32). Assigning the same device to both reaches 32 parts like Super-MPU",
			L"Entrée MIDI. Chaque périphérique apparaît en 1-16ch (parties 1–16) et 17-32ch (17–32). Le même périphérique sur les deux atteint 32 parties comme Super-MPU",
			L"Ingresso MIDI. Ogni dispositivo è in 1-16ch (parti 1–16) e 17-32ch (17–32). Lo stesso su entrambi raggiunge 32 parti come Super-MPU",
			L"Entrada MIDI. Cada dispositivo sale como 1-16ch (partes 1–16) y 17-32ch (17–32). El mismo en ambos llega a 32 partes como Super-MPU",
			L"MIDI 입력. 같은 장치가 1-16ch(파트 1–16)와 17-32ch(17–32)로 나옵니다. 둘 다 같은 장치면 Super-MPU처럼 32파트로 갑니다",
			L"MIDI 输入。同一设备会列出 1-16ch（声部1–16）和 17-32ch（17–32）。两边选同一设备即可像 Super-MPU 一样送到 32 声部",
			L"دخل MIDI. يظهر كل جهاز كـ 1-16ch (الأجزاء 1–16) و17-32ch (17–32). تعيين نفس الجهاز للاثنين يصل إلى 32 جزءاً مثل Super-MPU",
			L"MIDI-вход. Каждое устройство — 1-16ch (партии 1–16) и 17-32ch (17–32). Одно устройство на оба входа даёт 32 партии, как Super-MPU",
			L"MIDI-Eingang. Jedes Gerät als 1-16ch (Parts 1–16) und 17-32ch (17–32). Dasselbe Gerät auf beiden erreicht 32 Parts wie Super-MPU",
			L"Entrada MIDI. Cada dispositivo aparece como 1-16ch (partes 1–16) e 17-32ch (17–32). O mesmo nos dois chega a 32 partes como Super-MPU",
			L"MIDI-ingang. Elk apparaat als 1-16ch (partijen 1–16) en 17-32ch (17–32). Hetzelfde op beide bereikt 32 partijen zoals Super-MPU",
			L"Wejście MIDI. Każde urządzenie jako 1-16ch (partie 1–16) i 17-32ch (17–32). To samo na obu daje 32 partie jak Super-MPU",
			L"MIDI girişi. Her aygıt 1-16ch (bölüm 1–16) ve 17-32ch (17–32) olarak listelenir. İkisinde de aynı aygıt Super-MPU gibi 32 bölüme gider"));
		m_tooltip.AddTool(&m_midiIn[1], LL14(L"2台目の MIDI 入力。1台目と違う帯域（1-16ch / 17-32ch）を選ぶと、鍵盤をパート帯で分けられます",
			L"Second MIDI input. Pick a different band (1-16ch / 17-32ch) from MIDI In 1 to split keyboards across part banks",
			L"2e entrée MIDI. Choisissez une autre bande (1-16ch / 17-32ch) pour séparer les claviers",
			L"Secondo ingresso MIDI. Scegli una fascia diversa (1-16ch / 17-32ch) per separare le tastiere",
			L"Segunda entrada MIDI. Elija otra banda (1-16ch / 17-32ch) para separar teclados",
			L"두 번째 MIDI 입력. 1번과 다른 대역(1-16ch / 17-32ch)을 고르면 건반을 파트 대역으로 나눕니다",
			L"第二 MIDI 输入。与 1 号选不同带（1-16ch / 17-32ch）即可把键盘分到不同声部带",
			L"دخل MIDI ثانٍ. اختر نطاقاً مختلفاً (1-16ch / 17-32ch) لفصل اللوحات",
			L"Второй MIDI-вход. Другая полоса (1-16ch / 17-32ch), чем у входа 1, разделяет клавиатуры по банкам партий",
			L"Zweiter MIDI-Eingang. Andere Band (1-16ch / 17-32ch) als Eingang 1 trennt Keyboards auf Part-Bänke",
			L"Segunda entrada MIDI. Escolha outra faixa (1-16ch / 17-32ch) para separar teclados",
			L"Tweede MIDI-ingang. Kies een andere band (1-16ch / 17-32ch) om toetsenborden over partbanken te verdelen",
			L"Drugie wejście MIDI. Inny pas (1-16ch / 17-32ch) niż wejście 1 rozdziela klawiatury na banki partii",
			L"İkinci MIDI girişi. 1'den farklı bant (1-16ch / 17-32ch) seçerek klavyeleri bölüm bantlarına ayırır"));
		m_tooltip.AddTool(&m_midiIn[2], LL14(L"3台目の MIDI 入力。1・2台目と同じく「1-16ch」（パート1–16）と「17-32ch」（パート17–32）から選びます。同じ機器を複数口に当てると Super-MPU のように 32 パートへ届きます",
			L"Third MIDI input. Same as In 1/2: pick 1-16ch (parts 1–16) or 17-32ch (parts 17–32). Assigning the same device to several ports reaches 32 parts like Super-MPU",
			L"3e entrée MIDI. Comme In 1/2 : 1-16ch (parties 1–16) ou 17-32ch (17–32). Le même périphérique sur plusieurs ports atteint 32 parties comme Super-MPU",
			L"Terzo ingresso MIDI. Come In 1/2: 1-16ch (parti 1–16) o 17-32ch (17–32). Lo stesso dispositivo su più porte raggiunge 32 parti come Super-MPU",
			L"Tercera entrada MIDI. Como In 1/2: 1-16ch (partes 1–16) o 17-32ch (17–32). El mismo dispositivo en varios puertos llega a 32 partes como Super-MPU",
			L"세 번째 MIDI 입력. 1·2번과 같이 1-16ch(파트 1–16) 또는 17-32ch(17–32)를 고릅니다. 같은 장치를 여러 포트에 두면 Super-MPU처럼 32파트로 갑니다",
			L"第三 MIDI 输入。与 1、2 号相同：选 1-16ch（声部1–16）或 17-32ch（17–32）。同一设备接到多个口即可像 Super-MPU 送到 32 声部",
			L"دخل MIDI ثالث. مثل 1/2: 1-16ch (الأجزاء 1–16) أو 17-32ch (17–32). نفس الجهاز على عدة منافذ يصل إلى 32 جزءاً مثل Super-MPU",
			L"Третий MIDI-вход. Как 1/2: 1-16ch (партии 1–16) или 17-32ch (17–32). Одно устройство на несколько портов даёт 32 партии, как Super-MPU",
			L"Dritter MIDI-Eingang. Wie In 1/2: 1-16ch (Parts 1–16) oder 17-32ch (17–32). Dasselbe Gerät auf mehreren Ports erreicht 32 Parts wie Super-MPU",
			L"Terceira entrada MIDI. Como In 1/2: 1-16ch (partes 1–16) ou 17-32ch (17–32). O mesmo dispositivo em várias portas chega a 32 partes como Super-MPU",
			L"Derde MIDI-ingang. Zoals In 1/2: 1-16ch (partijen 1–16) of 17-32ch (17–32). Hetzelfde apparaat op meerdere poorten bereikt 32 partijen zoals Super-MPU",
			L"Trzecie wejście MIDI. Jak 1/2: 1-16ch (partie 1–16) lub 17-32ch (17–32). To samo urządzenie na kilku portach daje 32 partie jak Super-MPU",
			L"Üçüncü MIDI girişi. 1/2 ile aynı: 1-16ch (bölüm 1–16) veya 17-32ch (17–32). Aynı aygıtı birkaç porta bağlamak Super-MPU gibi 32 bölüme gider"));
		m_tooltip.AddTool(&m_thru, LL14(L"プレイヤーで鳴っている音をこのホストへ回します。プレイヤー本体の出力は止まり、音はここの音声出力からだけ出ます。MIDIならSMFも鍵盤と合流します。MIDI入力 1–3 と同時に使えます", L"Routes the player's current audio into this host. Player speakers go silent; listen from this host's audio output. MIDI SMF also merges with the keyboards. Can be used together with MIDI In 1–3",
			L"Mélange le son du lecteur dans cet hôte. Le SMF MIDI rejoint aussi les claviers. Utilisable avec MIDI In 1–3", L"Mescola l'audio del lettore in questo host. Lo SMF MIDI si unisce anche alle tastiere. Usabile insieme a MIDI In 1–3", L"Mezcla el audio del reproductor en este host. El SMF MIDI también se une a los teclados. Se puede usar junto con MIDI In 1–3",
			L"플레이어에서 나는 소리를 이 호스트 믹스에 섞습니다. MIDI면 SMF도 건반과 합류합니다. MIDI 입력 1–3과 함께 쓸 수 있습니다", L"将播放器正在发出的声音混入本主机。若是 MIDI，SMF 也会与键盘合流。可与 MIDI 输入 1–3 同时使用", L"يمزج صوت المشغّل في هذا المضيف. إن كان MIDI ينضم SMF أيضاً إلى اللوحات. يمكن استخدامه مع دخل MIDI 1–3",
			L"Смешивает звук плеера в микс хоста. Для MIDI SMF тоже сливается с клавиатурами. Можно вместе с MIDI In 1–3", L"Mischt den Playerklang in diesen Host. Bei MIDI kommt das SMF zu den Keyboards. Zusammen mit MIDI In 1–3 nutzbar", L"Mistura o áudio do leitor neste host. Se for MIDI, o SMF junta-se também aos teclados. Pode usar junto com MIDI In 1–3",
			L"Mengt het geluid van de speler in deze host. Bij MIDI voegt de SMF zich bij de toetsenborden. Samen met MIDI In 1–3 te gebruiken", L"Miesza dźwięk odtwarzacza w ten host. Przy MIDI SMF też łączy się z klawiaturami. Można razem z MIDI In 1–3", L"Çalıcının sesini bu hosta karıştırır. MIDI ise SMF de klavyelerle birleşir. MIDI In 1–3 ile birlikte kullanılabilir"));
		m_tooltip.AddTool(&m_vol, LL14(L"このホストの出力音量（WAV出力にもかかります）", L"This host's output volume (also applied to WAV out)",
			L"Volume de sortie de cet hôte (aussi sur la sortie WAV)", L"Volume di uscita di questo host (vale anche per WAV)", L"Volumen de salida de este host (también en WAV)",
			L"이 호스트의 출력 음량(WAV 출력에도 적용)", L"本主机的输出音量（也作用于 WAV 输出）", L"مستوى صوت خرج هذا المضيف (يُطبَّق أيضاً على WAV)",
			L"Громкость выхода этого хоста (также на WAV)", L"Ausgangslautstärke dieses Hosts (gilt auch für WAV)", L"Volume de saída deste host (também no WAV)",
			L"Uitgangsvolume van deze host (ook op WAV)", L"Głośność wyjścia tego hosta (także WAV)", L"Bu hostun çıkış sesi (WAV çıkışına da uygulanır)"));
		m_tooltip.AddTool(&m_wav, LL14(L"このホストのミックスをWAVへ書き出します。メイン再生スルー中はプレイヤーの音も混ざります（もう一度押すと確定）", L"Write this host's mix to WAV. Main-playback thru also includes player audio (press again to finish)",
			L"Écrire le mix de cet hôte en WAV (reappuyer pour terminer)", L"Scrive il mix di questo host su WAV (premere di nuovo per chiudere)", L"Escribe la mezcla de este host a WAV (pulse otra vez para cerrar)",
			L"이 호스트의 믹스를 WAV로 씁니다(다시 누르면 확정)", L"将本主机的混音写入 WAV（再按一次结束）", L"يكتب مزيج هذا المضيف إلى WAV (اضغط مرة أخرى للإنهاء)",
			L"Пишет микс этого хоста в WAV (ещё раз — закрыть)", L"Schreibt den Mix dieses Hosts als WAV (nochmals drücken zum Abschluss)", L"Escreve o mix deste host para WAV (prima outra vez para fechar)",
			L"Schrijft de mix van deze host naar WAV (opnieuw drukken om af te ronden)", L"Zapisuje mix tego hosta do WAV (ponowne naciśnięcie zamyka)", L"Bu hostun miksini WAV'a yazar (bitirmek için tekrar basın)"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 12000);
	}
	if (m_presetCount)
		ApplyPreset(0);
	else {
		StartMidi();
		StartAudio();
	}
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
	else {
		VstLiveMidiShort(0, msg);
		VstLiveTapPushShort(0, msg);
	}
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
		m_labels[0].SetWindowPos(NULL, x + 1, top - lblH - lblGap, 180, lblH, SWP_NOZORDER);
	m_preset.SetWindowPos(NULL, x, top, 180, 220, SWP_NOZORDER); x += 180 + gap;
	m_rename.SetWindowPos(NULL, x, top, 80, rowH, SWP_NOZORDER); x += 80 + gap;
	m_del.SetWindowPos(NULL, x, top, 60, rowH, SWP_NOZORDER); x += 60 + gap;
	m_save.SetWindowPos(NULL, x, top, 64, rowH, SWP_NOZORDER); x += 64 + gap;
	m_rescan.SetWindowPos(NULL, cx - pad - 82, top, 82, rowH, SWP_NOZORDER);
	const int y2 = top + rowH + gap + lblH + lblGap;
	const int volW = 108, pctW = 36;
	const int comboW = max(72, (cx - pad * 2 - gap * 6 - volW - pctW) / 6);
	const int xMidi = pad;
	const int xThru = pad + 3 * (comboW + gap);
	const int xOut = pad + 4 * (comboW + gap);
	const int xVol = pad + 5 * (comboW + gap);
	const int xFilt = xVol + volW + pctW + gap;
	if (m_labels[1].GetSafeHwnd())
		m_labels[1].SetWindowPos(NULL, xMidi + 1, y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	if (m_labels[2].GetSafeHwnd())
		m_labels[2].SetWindowPos(NULL, xMidi + comboW + gap + 1, y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	if (m_labels[3].GetSafeHwnd())
		m_labels[3].SetWindowPos(NULL, xMidi + 2 * (comboW + gap) + 1, y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	if (m_labels[4].GetSafeHwnd())
		m_labels[4].SetWindowPos(NULL, xThru + 1, y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	if (m_labels[5].GetSafeHwnd())
		m_labels[5].SetWindowPos(NULL, xOut + 1, y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	if (m_labels[6].GetSafeHwnd())
		m_labels[6].SetWindowPos(NULL, xVol + 1, y2 - lblH - lblGap, volW + pctW, lblH, SWP_NOZORDER);
	if (m_labels[7].GetSafeHwnd())
		m_labels[7].SetWindowPos(NULL, xFilt + 1, y2 - lblH - lblGap, comboW, lblH, SWP_NOZORDER);
	for (int i = 0; i < 3; ++i)
		m_midiIn[i].SetWindowPos(NULL, xMidi + i * (comboW + gap), y2, comboW, 220, SWP_NOZORDER);
	if (m_thru.GetSafeHwnd())
		m_thru.SetWindowPos(NULL, xThru, y2, comboW, 220, SWP_NOZORDER);
	m_speakerOut.SetWindowPos(NULL, xOut, y2, comboW, 220, SWP_NOZORDER);
	if (m_vol.GetSafeHwnd())
		m_vol.SetWindowPos(NULL, xVol, y2, volW, rowH, SWP_NOZORDER);
	if (m_volPct.GetSafeHwnd())
		m_volPct.SetWindowPos(NULL, xVol + volW + 2, y2 + 4, pctW - 2, lblH, SWP_NOZORDER);
	m_pluginFilter.SetWindowPos(NULL, xFilt, y2, comboW, 220, SWP_NOZORDER);
	const int wireTop = y2 + rowH + gap;
	m_wire.SetWindowPos(NULL, pad, wireTop, max(10, cx - pad * 2), max(40, cy - wireTop - bottomH - pad), SWP_NOZORDER);
	const int wavW = 90, closeW = 82;
	const int statusW = max(20, (cx - pad * 2 - wavW - closeW - gap * 2) / 2);
	if (CWnd* st = GetDlgItem(IDC_VST_STATUS))
		st->SetWindowPos(NULL, pad, cy - bottomH + 4, statusW, 18, SWP_NOZORDER);
	if (m_monitor.GetSafeHwnd())
		m_monitor.SetWindowPos(NULL, pad + statusW + gap, cy - bottomH + 4,
			max(20, cx - pad * 2 - statusW - gap - wavW - gap - closeW), 18, SWP_NOZORDER);
	if (m_wav.GetSafeHwnd())
		m_wav.SetWindowPos(NULL, cx - pad - closeW - gap - wavW, cy - bottomH, wavW, 22, SWP_NOZORDER);
	m_close.SetWindowPos(NULL, cx - pad - closeW, cy - bottomH, closeW, 22, SWP_NOZORDER);
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
	const wchar_t* suf16 = LL14(L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch",
		L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch", L" 1-16ch");
	const wchar_t* suf32 = LL14(L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch",
		L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch", L" 17-32ch");
	for (int p = 0; p < 3; ++p) {
		m_midiIn[p].ResetContent();
		int n = m_midiIn[p].AddString(none); m_midiIn[p].SetItemData(n, (DWORD_PTR)-1);
		for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
			MIDIINCAPS caps = {};
			if (midiInGetDevCaps(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
			CString s16, s32;
			s16.Format(L"%s%s", caps.szPname, suf16);
			s32.Format(L"%s%s", caps.szPname, suf32);
			n = m_midiIn[p].AddString(s16); m_midiIn[p].SetItemData(n, (DWORD_PTR)i);
			n = m_midiIn[p].AddString(s32); m_midiIn[p].SetItemData(n, (DWORD_PTR)i | 0x10000);
		}
		m_midiIn[p].SetCurSel(0);
		{
			CClientDC dc(&m_midiIn[p]);
			int mw = 0;
			CFont* old = dc.SelectObject(m_midiIn[p].GetFont());
			for (int i = 0; i < m_midiIn[p].GetCount(); ++i) {
				CString s;
				m_midiIn[p].GetLBText(i, s);
				const int w = dc.GetTextExtent(s).cx;
				if (w > mw) mw = w;
			}
			if (old) dc.SelectObject(old);
			CRect wr;
			m_midiIn[p].GetWindowRect(&wr);
			mw += GetSystemMetrics(SM_CXVSCROLL) + 24;
			if (mw < wr.Width()) mw = wr.Width();
			m_midiIn[p].SetDroppedWidth(mw);
		}
	}
	m_thru.ResetContent();
	int n3 = m_thru.AddString(none); m_thru.SetItemData(n3, (DWORD_PTR)-1);
	n3 = m_thru.AddString(LL14(L"メイン再生スルー", L"Main-playback thru", L"Traversée lecture", L"Pass-through riproduzione",
		L"Paso de reproducción", L"메인 재생 스루", L"主播放直通", L"تمرير التشغيل الرئيسي",
		L"Сквозн. осн. воспр.", L"Hauptwiedergabe-Thru", L"Passagem da reprodução", L"Hoofdweergave-thru",
		L"Przelot odtwarzania", L"Ana çalma thru"));
	m_thru.SetItemData(n3, (DWORD_PTR)-2);
	m_thru.SetCurSel(0);
	{
		CClientDC dc(&m_thru);
		int mw = 0;
		CFont* old = dc.SelectObject(m_thru.GetFont());
		for (int i = 0; i < m_thru.GetCount(); ++i) {
			CString s;
			m_thru.GetLBText(i, s);
			const int w = dc.GetTextExtent(s).cx;
			if (w > mw) mw = w;
		}
		if (old) dc.SelectObject(old);
		CRect wr;
		m_thru.GetWindowRect(&wr);
		mw += GetSystemMetrics(SM_CXVSCROLL) + 24;
		if (mw < wr.Width()) mw = wr.Width();
		m_thru.SetDroppedWidth(mw);
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

void CVstHostDlg::PartPluginName(int part0to31, wchar_t* out, int outChars) const
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	if (part0to31 < 0 || part0to31 >= 32) return;
	int scan = m_slots[part0to31];
	if (scan < 0) {
		for (int i = part0to31 - 1, b0 = (part0to31 / 16) * 16; i >= b0; --i) {
			if (m_slots[i] < 0) continue;
			scan = PluginIsMulti(m_slots[i]) ? m_slots[i] : -1;
			break;
		}
	}
	if (scan >= 0) {
		CString s = PluginName(scan);
		wcsncpy_s(out, outChars, s, _TRUNCATE);
	}
}

void VstHostOnLiveListChanged()
{
	if (!g_vstHostDlg || !::IsWindow(g_vstHostDlg->GetSafeHwnd())) return;
	g_vstHostDlg->RebuildPluginList();
}

void CVstHostDlg::RebuildPluginList()
{
	CString filter;
	m_pluginFilter.GetWindowText(filter); filter.MakeLower();
	if (m_pluginFilter.GetCurSel() == 0) filter.Empty();

	// Silent after a real note probe is a broken install or an effect that
	// should not occupy a part slot. Samplers waiting for a patch stay listed.
	CString names[100]; int indices[100]; int count = 0;
	for (int i = 0; i < VstScanGetCount() && count < 100; ++i) {
		const VstPluginInfo* pi = VstScanGet(i);
		if (!pi || !pi->isInstrument || !pi->isLiveOk) continue;
		if (pi->isAudible == 0) continue;
		CString n(pi->name);
		CString tags;
		if (pi->arch == 64) tags += L"<x64>";
		else if (pi->arch == 32) tags += L"<x86>";
		if (pi->isMultiTimbral) tags += L"[M16ch]";
		if (pi->isAudible == 2)
			tags += LL14(L"<音色選択>", L"<patch>", L"<timbre>", L"<patch>",
				L"<timbre>", L"<음색선택>", L"<选音色>", L"<رقعة>",
				L"<патч>", L"<Patch>", L"<timbre>", L"<patch>",
				L"<barwa>", L"<yama>");
		CString shown = n;
		if (!tags.IsEmpty()) shown += L" " + tags;
		CString low(shown); low.MakeLower();
		if (!filter.IsEmpty() && low.Find(filter) < 0) continue;
		names[count] = shown;
		indices[count] = i; count++;
	}
	m_wire.SetPlugins(names, indices, count);
	if (m_wire.GetSafeHwnd())
		m_wire.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
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
	memset(m_presets, 0, sizeof(m_presets));
	CFile f;
	if (f.Open(DataPath(), CFile::modeRead | CFile::shareDenyWrite)) {
		DWORD magic = 0, count = 0;
		if (f.Read(&magic, sizeof(magic)) == sizeof(magic) &&
			f.Read(&count, sizeof(count)) == sizeof(count) &&
			(magic == VST_WIRE_MAGIC1 || magic == VST_WIRE_MAGIC2)) {
			m_presetCount = min(100, (int)count);
			if (magic == VST_WIRE_MAGIC2) {
				for (int i = 0; i < m_presetCount; ++i) {
					if (f.Read(&m_presets[i], sizeof(Preset)) != sizeof(Preset)) {
						m_presetCount = i;
						break;
					}
				}
			} else {
				const UINT oldSz = (UINT)offsetof(Preset, midiThru);
				for (int i = 0; i < m_presetCount; ++i) {
					if (f.Read(&m_presets[i], oldSz) != oldSz) {
						m_presetCount = i;
						break;
					}
					if (m_presets[i].midiIn[2] == -2) {
						m_presets[i].midiThru = 1;
						m_presets[i].midiIn[2] = -1;
					}
				}
			}
		}
		f.Close();
	}
	RefreshPresetCombo(m_presetCount ? 0 : -1);
	// ApplyPreset loads plug-ins and starts I/O. That must wait until
	// scan/verify has finished, or a probe CloseEffect / SetDllDirectory
	// leaves the already-loaded instance silent until the host is reopened.
}

BOOL CVstHostDlg::SavePresets()
{
	VstWireFile data = {};
	data.magic = VST_WIRE_MAGIC2; data.count = m_presetCount;
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
		p.midiIn[i] = ComboSelData(m_midiIn[i]);
	}
	{
		p.midiThru = (ComboSelData(m_thru) == -2) ? 1 : 0;
	}
	p.outDev = ComboSelData(m_speakerOut);
	if (p.outDev < 0) p.outDev = (int)WAVE_MAPPER;
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
		int want = p.midiIn[port];
		if (want == -2) want = -1;
		int sel = 0;
		for (int i = 0; i < m_midiIn[port].GetCount(); ++i)
			if ((int)m_midiIn[port].GetItemData(i) == want) { sel = i; break; }
		m_midiIn[port].CComboBox::SetCurSel(sel);
	}
	{
		const int wantThru = (p.midiThru || p.midiIn[2] == -2) ? -2 : -1;
		int sel = 0;
		for (int i = 0; i < m_thru.GetCount(); ++i)
			if ((int)m_thru.GetItemData(i) == wantThru) { sel = i; break; }
		m_thru.CComboBox::SetCurSel(sel);
	}
	int outSel = 0;
	for (int i = 0; i < m_speakerOut.GetCount(); ++i)
		if ((int)m_speakerOut.GetItemData(i) == p.outDev) { outSel = i; break; }
	m_speakerOut.CComboBox::SetCurSel(outSel);
	int anyLoad = 0;
	for (int part = 0; part < 32; ++part) {
		m_slots[part] = -1;
		VstLiveUnloadPart(part + 1);
		if (!p.path[part][0]) continue;
		int scanIndex = -1;
		const wchar_t* waitName = p.path[part];
		for (int i = 0; i < VstScanGetCount(); ++i) {
			const VstPluginInfo* pi = VstScanGet(i);
			if (pi && _wcsicmp(pi->path, p.path[part]) == 0) {
				scanIndex = i;
				waitName = pi->name;
				break;
			}
		}
		VstWaitShowLoad(m_hWnd, waitName);
		anyLoad = 1;
		if (VstLiveLoadPart(part + 1, p.path[part], p.isVst3[part]) == 0)
			m_slots[part] = scanIndex;
	}
	if (anyLoad) VstWaitHide();
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
	StopAudio();
	VstScanInvalidate();
	m_wire.SetPlugins(NULL, NULL, 0);
	VstScanEnsure(m_hWnd);
	VstScanVerifyLiveList(m_hWnd);
	RebuildPluginList();
	StartAudio();
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
	VstWaitShowLoad(m_hWnd, pi->name);
	const int loadRc = VstLiveLoadPart(slot + 1, pi->path, pi->isVst3);
	VstWaitHide();
	if (loadRc != 0) {
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

void CALLBACK CVstHostDlg::MidiInProc(HMIDIIN hmi, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR)
{
	CVstHostDlg* self = (CVstHostDlg*)instance;
	if (!self || !hmi) return;
	// Never pack the slot into the low bits of `this`: 32-bit ogg on 64-bit
	// Windows (and some virtual MIDI drivers) treat dwInstance as a pointer
	// and align it, which turned MIDI In 2 into In 1. Identify the port by
	// the HMIDIIN the driver actually delivered.
	int slot = -1;
	for (int i = 0; i < 3; ++i)
		if (self->m_midiHandles[i] == hmi) { slot = i; break; }
	if (slot < 0) return;
	const bool queued = InterlockedCompareExchange(&self->m_audioRunning, 0, 0) != 0;
	BYTE mask = self->m_midiDestMask[slot];
	if (msg == MIM_DATA) {
		const DWORD m = (DWORD)p1;
		if ((m & 0xFF) == 0xF5) {
			// Super-MPU: F5 selects A/B only when this device is already
			// bound to both banks. A combo set to 17-32ch must not be
			// stolen back to 1-16ch because the player sent F5 00.
			if ((mask & 3) == 3)
				self->m_midiF5Port[slot] = (BYTE)((m >> 8) & 1);
			return;
		}
		if ((mask & 3) == 3 && self->m_midiF5Port[slot] != 0xFF)
			mask = (BYTE)(1 << (self->m_midiF5Port[slot] & 1));
		if (!mask) mask = 1;
		for (int d = 0; d < 2; ++d) {
			if (!(mask & (1 << d))) continue;
			if (queued) MidiFifoPush(d, m);
			else {
				VstLiveMidiShort(d, m);
				VstLiveTapPushShort(d, m);
			}
		}
		return;
	}
	if (msg == MIM_LONGDATA) {
		MIDIHDR* hdr = (MIDIHDR*)p1;
		if (!hdr) return;
		const int bytes = (int)hdr->dwBytesRecorded;
		if (bytes > 0) {
			if (!mask) mask = 1;
			for (int d = 0; d < 2; ++d) {
				if (!(mask & (1 << d))) continue;
				if (queued) MidiFifoPushSysex(d, (const BYTE*)hdr->lpData, bytes);
				else {
					VstLiveMidiSysex(d, (const unsigned char*)hdr->lpData, bytes);
					VstLiveTapPushSysex(d, (const unsigned char*)hdr->lpData, bytes);
				}
			}
			// Zero bytes means the driver is returning buffers on reset, and
			// re-adding then would fight midiInClose.
			for (int b = 0; b < SYSEX_BUFS_PER_PORT; ++b)
				if (&g_sysexBufs[slot][b].hdr == hdr)
					InterlockedExchange(&g_sysexBufs[slot][b].needAdd, 1);
		}
	}
}

void CVstHostDlg::BindThruSong()
{
	int dev = ComboSelData(m_thru);
	if (dev != -2) {
		VstLiveThruSet(0);
		return;
	}
	if (!VstLiveThruIsOn())
		VstLiveThruSet(1);
	wchar_t mid[520] = {};
	wchar_t hints[8][128] = {};
	int hc = 0;
	if (!filen.IsEmpty())
		VstResolvePlayPath(filen, mid, 520, hints, 8, &hc);
	if (!mid[0] && !filen.IsEmpty() && VstIsMidiExt(filen))
		wcsncpy_s(mid, filen, _TRUNCATE);
	VstLiveThruBind(mid[0] ? mid : NULL);
}

void CVstHostDlg::StopWav()
{
	InterlockedExchange(&m_wavOn, 0);
	EnterCriticalSection(&m_wavLock);
	HANDLE h = m_wavFile;
	const LONG dataBytes = m_wavBytes;
	m_wavFile = INVALID_HANDLE_VALUE;
	m_wavBytes = 0;
	LeaveCriticalSection(&m_wavLock);
	if (h == INVALID_HANDLE_VALUE) return;
	const __int64 fileLen = 80 + (__int64)dataBytes;
	DWORD wr = 0;
	if (dataBytes >= 0 && dataBytes <= 0x7FFFFFFF) {
		BYTE riff[12];
		memcpy(riff, "RIFF", 4);
		*(DWORD*)(riff + 4) = (DWORD)(fileLen - 8);
		memcpy(riff + 8, "WAVE", 4);
		SetFilePointer(h, 0, NULL, FILE_BEGIN);
		WriteFile(h, riff, 12, &wr, NULL);
		SetFilePointer(h, 76, NULL, FILE_BEGIN);
		DWORD ds = (DWORD)dataBytes;
		WriteFile(h, &ds, 4, &wr, NULL);
	} else {
		BYTE rf[48];
		memcpy(rf, "RF64", 4);
		*(DWORD*)(rf + 4) = 0xFFFFFFFF;
		memcpy(rf + 8, "WAVE", 4);
		memcpy(rf + 12, "ds64", 4);
		*(DWORD*)(rf + 16) = 28;
		*(__int64*)(rf + 20) = fileLen - 8;
		*(__int64*)(rf + 28) = dataBytes;
		*(__int64*)(rf + 36) = dataBytes / 4;
		*(DWORD*)(rf + 44) = 0;
		SetFilePointer(h, 0, NULL, FILE_BEGIN);
		WriteFile(h, rf, 48, &wr, NULL);
		SetFilePointer(h, 76, NULL, FILE_BEGIN);
		DWORD ffff = 0xFFFFFFFF;
		WriteFile(h, &ffff, 4, &wr, NULL);
	}
	CloseHandle(h);
	if (m_wav.GetSafeHwnd())
		m_wav.SetWindowText(LL14(L"WAV出力", L"WAV out", L"Sortie WAV", L"Uscita WAV", L"Salida WAV", L"WAV 출력",
			L"WAV 输出", L"خرج WAV", L"Выход WAV", L"WAV-Ausgabe", L"Saída WAV", L"WAV-uitgang",
			L"Wyjście WAV", L"WAV çıkışı"));
}

void CVstHostDlg::OnWavClick()
{
	if (InterlockedCompareExchange(&m_wavOn, 0, 0) == 1) {
		StopWav();
		return;
	}
	CFileDialog dlg(FALSE, L"wav", L"vsthost.wav",
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		L"WAV (*.wav)|*.wav||", this);
	if (dlg.DoModal() != IDOK) return;
	HANDLE h = CreateFileW(dlg.GetPathName(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	BYTE hdr[80];
	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr + 0, "RIFF", 4);
	memcpy(hdr + 8, "WAVE", 4);
	memcpy(hdr + 12, "JUNK", 4);
	*(DWORD*)(hdr + 16) = 28;
	memcpy(hdr + 48, "fmt ", 4);
	*(DWORD*)(hdr + 52) = 16;
	*(WORD*)(hdr + 56) = 1;
	*(WORD*)(hdr + 58) = 2;
	*(DWORD*)(hdr + 60) = 44100;
	*(DWORD*)(hdr + 64) = 44100 * 4;
	*(WORD*)(hdr + 68) = 4;
	*(WORD*)(hdr + 70) = 16;
	memcpy(hdr + 72, "data", 4);
	DWORD wr = 0;
	WriteFile(h, hdr, 80, &wr, NULL);
	EnterCriticalSection(&m_wavLock);
	m_wavFile = h;
	m_wavBytes = 0;
	LeaveCriticalSection(&m_wavLock);
	InterlockedExchange(&m_wavOn, 1);
	m_wav.SetWindowText(LL14(L"記録中…", L"Recording…", L"Enregistrement…", L"Registrazione…", L"Grabando…", L"녹음 중…",
		L"正在录音…", L"جارٍ التسجيل…", L"Запись…", L"Aufnahme…", L"A gravar…", L"Opnemen…",
		L"Nagrywanie…", L"Kaydediliyor…"));
}

void CVstHostDlg::StartMidi()
{
	StopMidi();
	MidiFifoInit();
	memset(m_midiDestMask, 0, sizeof(m_midiDestMask));
	memset(m_midiF5Port, 0xFF, sizeof(m_midiF5Port));
	UINT openedDev[3];
	openedDev[0] = openedDev[1] = openedDev[2] = 0xFFFFFFFFu;
	for (int p = 0; p < 3; ++p) {
		int data = ComboSelData(m_midiIn[p]);
		if (data < 0) continue;
		const UINT dev = (UINT)(data & 0xFFFF);
		const int bank = (data >> 16) & 1;
		int mask = 1 << bank;
		MIDIINCAPS caps = {};
		if (midiInGetDevCaps(dev, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			wchar_t nm[MAXPNAMELEN];
			wcsncpy_s(nm, caps.szPname, _TRUNCATE);
			CharUpperBuffW(nm, (DWORD)wcslen(nm));
			// Super-MPU / MPU-401 32-part: 1-16ch also forwards to 17-32ch.
			if (bank == 0 && wcsstr(nm, L"MPU"))
				mask |= 2;
		}
		int slot = -1;
		for (int q = 0; q < 3; ++q)
			if (m_midiHandles[q] && openedDev[q] == dev) { slot = q; break; }
		if (slot >= 0) {
			m_midiDestMask[slot] = (BYTE)(m_midiDestMask[slot] | mask);
			continue;
		}
		for (int q = 0; q < 3; ++q)
			if (!m_midiHandles[q]) { slot = q; break; }
		if (slot < 0) continue;
		if (midiInOpen(&m_midiHandles[slot], dev, (DWORD_PTR)&MidiInProc,
			(DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
			continue;
		openedDev[slot] = dev;
		m_midiDestMask[slot] = (BYTE)mask;
		m_midiF5Port[slot] = 0xFF;
		// Without these buffers the driver has nowhere to put system
		// exclusive, so GS/XG resets and bank selects never arrive at all.
		for (int b = 0; b < SYSEX_BUFS_PER_PORT; ++b) {
			SysexBuf& s = g_sysexBufs[slot][b];
			ZeroMemory(&s.hdr, sizeof(s.hdr));
			s.hdr.lpData = (LPSTR)s.data;
			s.hdr.dwBufferLength = SYSEX_SLOT_BYTES;
			s.in = m_midiHandles[slot];
			InterlockedExchange(&s.needAdd, 0);
			if (midiInPrepareHeader(m_midiHandles[slot], &s.hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR)
				continue;
			InterlockedExchange(&s.prepared, 1);
			midiInAddBuffer(m_midiHandles[slot], &s.hdr, sizeof(MIDIHDR));
		}
		midiInStart(m_midiHandles[slot]);
	}
	InterlockedExchange(&g_midiStopRecycle, 0);
	BindThruSong();
}

void CVstHostDlg::StopMidi()
{
	InterlockedExchange(&g_midiStopRecycle, 1);
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
	memset(m_midiDestMask, 0, sizeof(m_midiDestMask));
	memset(m_midiF5Port, 0xFF, sizeof(m_midiF5Port));
	MidiFifoClear();
	VstLiveAllNotesOff();
	VstLiveThruSet(0);
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
			__int64 pb = playb;
			const int sr = wavbit_sample_Hz;
			if (sr >= 8000 && sr != 44100)
				pb = playb * 44100 / sr;
			VstLiveThruPoll(pb);
			VstLiveRender(L, R, VST_AUDIO_FRAMES);
			VstLiveThruPcmMix(L, R, VST_AUDIO_FRAMES);
			const int vol = (int)InterlockedCompareExchange(&self->m_volLevel, 0, 0);
			const float g = (vol <= 0) ? 0.f : ((vol >= 100) ? 1.f : (float)vol * 0.01f);
			for (int i = 0; i < VST_AUDIO_FRAMES; ++i) {
				float l = max(-1.f, min(1.f, L[i] * g)), r = max(-1.f, min(1.f, R[i] * g));
				pcm[b][i * 2] = (short)(l * 32767.f);
				pcm[b][i * 2 + 1] = (short)(r * 32767.f);
			}
			if (InterlockedCompareExchange(&self->m_wavOn, 1, 1) == 1) {
				EnterCriticalSection(&self->m_wavLock);
				if (self->m_wavFile != INVALID_HANDLE_VALUE) {
					DWORD wr = 0;
					WriteFile(self->m_wavFile, pcm[b], sizeof(pcm[b]), &wr, NULL);
					self->m_wavBytes += (LONG)wr;
				}
				LeaveCriticalSection(&self->m_wavLock);
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
	UINT dev = (UINT)ComboSelData(m_speakerOut);
	if ((int)dev < 0) dev = WAVE_MAPPER;
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
	if (m_audioEvent) SetEvent(m_audioEvent);
	if (m_audioThread) {
		DWORD w = WaitForSingleObject(m_audioThread, 8000);
		if (w != WAIT_OBJECT_0 && m_waveOut) {
			// Thread is still inside waveOutWrite; Reset is the only nudge
			// that is safe once the loop has already been asked to exit.
			waveOutReset(m_waveOut);
			w = WaitForSingleObject(m_audioThread, 3000);
		}
		CloseHandle(m_audioThread);
		m_audioThread = NULL;
		if (w != WAIT_OBJECT_0) {
			InterlockedExchange(&m_audioRunning, 0);
			return;
		}
	}
	if (m_waveOut) {
		waveOutClose(m_waveOut);
		m_waveOut = NULL;
	}
	if (m_audioEvent) { CloseHandle(m_audioEvent); m_audioEvent = NULL; }
	if (m_audioStop) { CloseHandle(m_audioStop); m_audioStop = NULL; }
	InterlockedExchange(&m_audioRunning, 0);
}

void CVstHostDlg::RestartIo()
{
	StopAudio();
	StartMidi();
	StartAudio();
}
void CVstHostDlg::OnDeviceChange() { RestartIo(); }

void CVstHostDlg::SetStatus(LPCTSTR text)
{
	if (CWnd* s = GetDlgItem(IDC_VST_STATUS)) s->SetWindowText(text);
}

void CVstHostDlg::OnTimer(UINT_PTR id)
{
	if (id == VST_ACTIVITY_TIMER) {
		BindThruSong();
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

void CVstHostDlg::ApplyVolUi()
{
	int v = m_vol.GetSafeHwnd() ? m_vol.GetPos() : 100;
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	InterlockedExchange(&m_volLevel, v);
	savedata.vstHostVol = v;
	if (m_volPct.GetSafeHwnd()) {
		CString s;
		s.Format(L"%d%%", v);
		m_volPct.SetWindowText(s);
	}
}

void CVstHostDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar && pScrollBar->m_hWnd == m_vol.m_hWnd)
		ApplyVolUi();
	CCustomBlurDialogBase::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CVstHostDlg::OnDestroy()
{
	KillTimer(VST_ACTIVITY_TIMER);
	PcKeyReleaseAll();
	StopWav();
	StopAudio();
	StopMidi();
	VstLiveShutdown();
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

