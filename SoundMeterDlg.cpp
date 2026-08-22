// SoundMeterDlg.cpp — 相対 dBFS マイクメータ（校正 SPL ではない）

#include "stdafx.h"
#include "ogg.h"
#include "SoundMeterDlg.h"
#include "AudioDevSync.h"
#include "CCustomPopupMenu.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <FunctionDiscoveryKeys_devpkey.h>
#include <process.h>
#include <math.h>

#pragma comment(lib, "Ole32.lib")

extern void MpPersistSavedataQuick();

namespace {

static const GUID s_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
{ 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID s_KSDATAFORMAT_SUBTYPE_PCM =
{ 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static void SmSamplePeak(const BYTE* frame, const WAVEFORMATEX* fmt, float& pk)
{
	if (!frame || !fmt) return;
	const WORD tag = fmt->wFormatTag;
	const WORD bits = fmt->wBitsPerSample;
	const int ch = (int)fmt->nChannels;
	float L = 0.f, R = 0.f;
	const BOOL isFloat = (tag == WAVE_FORMAT_IEEE_FLOAT)
		|| (tag == WAVE_FORMAT_EXTENSIBLE && bits == 32
			&& ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == s_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
	const BOOL isPcmExt = (tag == WAVE_FORMAT_EXTENSIBLE
		&& ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == s_KSDATAFORMAT_SUBTYPE_PCM);
	if (isFloat) {
		const float* f = (const float*)frame;
		L = f[0];
		R = (ch >= 2) ? f[1] : L;
	} else if (bits == 16 || (isPcmExt && bits == 16) || (tag == WAVE_FORMAT_PCM && bits == 16)) {
		const short* s = (const short*)frame;
		L = (float)s[0] / 32768.f;
		R = (ch >= 2) ? ((float)s[1] / 32768.f) : L;
	} else if (bits == 24) {
		int v = (int)(frame[0] | (frame[1] << 8) | (frame[2] << 16));
		if (v & 0x800000) v |= ~0xFFFFFF;
		L = (float)v / 8388608.f;
		if (ch >= 2) {
			int v2 = (int)(frame[3] | (frame[4] << 8) | (frame[5] << 16));
			if (v2 & 0x800000) v2 |= ~0xFFFFFF;
			R = (float)v2 / 8388608.f;
		} else R = L;
	} else if (bits == 32) {
		const int* s = (const int*)frame;
		L = (float)s[0] / 2147483648.f;
		R = (ch >= 2) ? ((float)s[1] / 2147483648.f) : L;
	}
	const float aL = (L < 0.f) ? -L : L;
	const float aR = (R < 0.f) ? -R : R;
	if (aL > pk) pk = aL;
	if (aR > pk) pk = aR;
}

static int SmMeterUi(LONG peak)
{
	if (peak <= 0) return 0;
	if (peak > 1000) peak = 1000;
	const double n = (double)peak / 1000.0;
	int ui = (int)(sqrt(n) * 1000.0 * 1.15);
	if (ui < 1) ui = 1;
	if (ui > 1000) ui = 1000;
	return ui;
}

class CSmHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_SM_HELP };
	explicit CSmHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK() { DestroyWindow(); }
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose() { DestroyWindow(); }
	DECLARE_MESSAGE_MAP()
};
static CSmHelpDlg* g_smHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CSmHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CSmHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	SetWindowText(LL14(
		L"騒音計ガイド", L"Sound meter guide", L"Guide sonomètre", L"Guida fonometro",
		L"Guía medidor", L"소음계 가이드", L"声级计指南", L"دليل مقياس الصوت",
		L"Руководство шумомера", L"Schallpegel-Anleitung", L"Guia medidor", L"Geluidsmeter-gids",
		L"Przewodnik miernika", L"Ses ölçer kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CSmHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_smHelpDlg == this) g_smHelpDlg = nullptr;
	delete this;
}
BOOL CSmHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CSmHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp)) return;
	CDC& dc = hp.mem;
	dc.SetBkMode(TRANSPARENT);
	dc.SelectObject(GetFont());
	TEXTMETRIC tm{}; dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	int y = 8; const int L = 10;
	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"騒音計（相対 dBFS）", L"Sound meter (relative dBFS)", L"Sonomètre (dBFS relatif)", L"Fonometro (dBFS relativo)",
		L"Medidor (dBFS relativo)", L"소음계(상대 dBFS)", L"声级计（相对 dBFS）", L"مقياس صوت (dBFS نسبي)",
		L"Шумомер (относительный dBFS)", L"Schallpegel (relativ dBFS)", L"Medidor (dBFS relativo)", L"Geluidsmeter (relatief dBFS)",
		L"Miernik (względne dBFS)", L"Ses ölçer (göreli dBFS)"));
	y += lh + 4;
	dc.SetTextColor(RGB(65, 65, 80));
	dc.TextOut(L, y, LL14(L"マイク入力の相対レベルを dBFS で表示します。校正された SPL 騒音計ではありません。",
		L"Shows relative mic level in dBFS. Not a calibrated SPL meter.",
		L"Affiche le niveau micro relatif en dBFS. Pas un SPL calibré.",
		L"Mostra il livello micro relativo in dBFS. Non è un SPL calibrato.",
		L"Muestra el nivel de mic relativo en dBFS. No es un SPL calibrado.",
		L"마이크 상대 레벨을 dBFS로 표시합니다. 교정 SPL 소음계가 아닙니다.",
		L"以相对 dBFS 显示麦克风电平。不是校准 SPL 声级计。",
		L"يعرض مستوى الميكروفون النسبي بـ dBFS. ليس مقياس SPL معايرًا.",
		L"Показывает относительный уровень микрофона в dBFS. Не калиброванный SPL.",
		L"Zeigt relativen Mikrofonpegel in dBFS. Kein kalibrierter SPL-Messer.",
		L"Mostra o nível relativo do micro em dBFS. Não é um SPL calibrado.",
		L"Toont relatief microniveau in dBFS. Geen gekalibreerde SPL-meter.",
		L"Pokazuje względny poziom mikrofonu w dBFS. To nie skalibrowany SPL.",
		L"Mikrofon göreli seviyesini dBFS gösterir. Kalibre SPL ölçer değildir."));
	y += lh + 2;
	dc.TextOut(L, y, LL14(L"右クリックで応答速度とピークホールド解除を選べます。",
		L"Right-click to change response speed or clear peak hold.",
		L"Clic droit : vitesse de réponse ou raz du hold.",
		L"Clic destro: velocità risposta o reset hold.",
		L"Clic derecho: velocidad de respuesta o borrar hold.",
		L"우클릭으로 응답 속도와 피크 홀드 해제를 고릅니다.",
		L"右键可改响应速度或清除峰值保持。",
		L"انقر يمينًا لتغيير سرعة الاستجابة أو مسح الإمساك.",
		L"ПКМ: скорость реакции или сброс удержания пика.",
		L"Rechtsklick: Ansprechzeit oder Peak-Hold löschen.",
		L"Clique direito: velocidade de resposta ou limpar hold.",
		L"Rechtsklik: reactiesnelheid of peak-hold wissen.",
		L"PPM: szybkość odpowiedzi lub kasowanie hold.",
		L"Sağ tık: yanıt hızı veya tepe tutmayı temizle."));
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

static CSoundMeterDlg* g_soundMeterDlg = NULL;

IMPLEMENT_DYNAMIC(CSoundMeterDlg, CCustomBlurDialogBase)

CSoundMeterDlg::CSoundMeterDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CSoundMeterDlg::IDD, pParent)
	, m_devCnt(0), m_stop(0), m_run(0), m_peak(0), m_holdPeak(0), m_thread(NULL), m_response(1)
{
	memset(m_devIds, 0, sizeof(m_devIds));
}

CSoundMeterDlg::~CSoundMeterDlg()
{
	StopCapture();
}

void CSoundMeterDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SM_HELP, m_help);
	DDX_Control(pDX, IDC_SM_MIC_L, m_micL);
	DDX_Control(pDX, IDC_SM_MIC, m_mic);
	DDX_Control(pDX, IDC_SM_MIC_REFRESH, m_micRefresh);
	DDX_Control(pDX, IDC_SM_DBFS, m_dbfs);
	DDX_Control(pDX, IDC_SM_HOLD, m_hold);
	DDX_Control(pDX, IDC_SM_METER_L, m_meterL);
	DDX_Control(pDX, IDC_SM_METER, m_meter);
	DDX_Control(pDX, IDC_SM_NOTE, m_note);
	DDX_Control(pDX, IDC_SM_STATUS, m_status);
	DDX_Control(pDX, IDC_SM_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CSoundMeterDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_SM_CLOSE, &CSoundMeterDlg::OnBnClickedClose)
	ON_BN_CLICKED(IDC_SM_HELP, &CSoundMeterDlg::OnBnClickedHelp)
	ON_CBN_SELCHANGE(IDC_SM_MIC, &CSoundMeterDlg::OnCbnSelchangeMic)
	ON_BN_CLICKED(IDC_SM_MIC_REFRESH, &CSoundMeterDlg::OnMicDevRefresh)
	ON_MESSAGE(WM_AUDIODEV_CHANGED, &CSoundMeterDlg::OnAudioDevChanged)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

BOOL CSoundMeterDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CSoundMeterDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_soundMeterDlg == this) g_soundMeterDlg = NULL;
	delete this;
}

void CSoundMeterDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CSoundMeterDlg::RefreshOpaqueUi()
{
	if (m_mic.GetSafeHwnd()) m_mic.SetAeroMode(FALSE);
	if (m_meter.GetSafeHwnd()) m_meter.SetAeroMode(FALSE);
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
}

void CSoundMeterDlg::FillMicCombo()
{
	AudioMicDevRefresh();
	m_mic.ResetContent();
	m_devCnt = 0;
	const int n = AudioMicDevCount();
	int sel = 0;
	for (int i = 0; i < n && m_devCnt < SM_DEV_MAX; ++i) {
		LPCTSTR id = AudioMicDevId(i);
		if (!id || !id[0]) continue;
		_tcsncpy(m_devIds[m_devCnt], id, 255);
		m_devIds[m_devCnt][255] = 0;
		m_mic.AddString(AudioMicDevName(i));
		if (savedata.sm_mic_device[0] && _tcsicmp(savedata.sm_mic_device, id) == 0)
			sel = m_devCnt;
		m_devCnt++;
	}
	if (m_devCnt > 0) m_mic.SetCurSel(sel);
}

void CSoundMeterDlg::PersistUi()
{
	const int sel = m_mic.GetCurSel();
	if (sel >= 0 && sel < m_devCnt) {
		_tcsncpy(savedata.sm_mic_device, m_devIds[sel], _countof(savedata.sm_mic_device) - 1);
		savedata.sm_mic_device[_countof(savedata.sm_mic_device) - 1] = 0;
	}
	savedata.sm_response = m_response;
	MpPersistSavedataQuick();
}

void CSoundMeterDlg::ShowHelpSheet()
{
	if (g_smHelpDlg && g_smHelpDlg->GetSafeHwnd()) {
		g_smHelpDlg->SetForegroundWindow();
		return;
	}
	g_smHelpDlg = new CSmHelpDlg(this);
	if (!g_smHelpDlg->Create(IDD_SM_HELP, this)) {
		delete g_smHelpDlg;
		g_smHelpDlg = nullptr;
		return;
	}
	CCC_PresentOwnedHelp(this, g_smHelpDlg);
}

BOOL CSoundMeterDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	m_response = savedata.sm_response;
	if (m_response < 0 || m_response > 2) m_response = 1;

	SetWindowText(LL14(
		L"騒音計", L"Sound meter", L"Sonomètre", L"Fonometro",
		L"Medidor de sonido", L"소음계", L"声级计", L"مقياس الصوت",
		L"Шумомер", L"Schallpegelmesser", L"Medidor de som", L"Geluidsmeter",
		L"Miernik dźwięku", L"Ses ölçer"));
	m_micL.SetWindowText(LL14(L"マイク", L"Mic", L"Micro", L"Micro", L"Micro",
		L"마이크", L"麦克风", L"ميكروفون", L"Микрофон", L"Mikrofon",
		L"Microfone", L"Microfoon", L"Mikrofon", L"Mikrofon"));
	m_meterL.SetWindowText(LL14(L"レベル", L"Level", L"Niveau", L"Livello", L"Nivel",
		L"레벨", L"电平", L"المستوى", L"Уровень", L"Pegel",
		L"Nível", L"Niveau", L"Poziom", L"Seviye"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_note.SetWindowText(LL14(
		L"相対 dBFS（校正 SPL ではありません）",
		L"Relative dBFS (not calibrated SPL)",
		L"dBFS relatif (pas SPL calibré)",
		L"dBFS relativo (non SPL calibrato)",
		L"dBFS relativo (no SPL calibrado)",
		L"상대 dBFS(교정 SPL 아님)",
		L"相对 dBFS（非校准 SPL）",
		L"dBFS نسبي (ليس SPL معايرًا)",
		L"Относительный dBFS (не калибр. SPL)",
		L"Relativ dBFS (kein kalibrierter SPL)",
		L"dBFS relativo (não SPL calibrado)",
		L"Relatief dBFS (geen gekalibreerde SPL)",
		L"Względne dBFS (nie skalibrowany SPL)",
		L"Göreli dBFS (kalibre SPL değil)"));

	FillMicCombo();
	AudioDevApplyRescanButton(&m_micRefresh);
	AudioDevRegisterNotifyHwnd(m_hWnd);
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_mic, LL14(
			L"測定するマイク入力端末", L"Microphone input to measure", L"Micro à mesurer", L"Micro da misurare",
			L"Micrófono a medir", L"측정할 마이크", L"要测量的麦克风", L"الميكروفون للقياس",
			L"Микрофон для измерения", L"Zu messendes Mikrofon", L"Microfone a medir", L"Te meten microfoon",
			L"Mikrofon do pomiaru", L"Ölçülecek mikrofon"));
		m_tooltip.AddTool(&m_meter, LL14(
			L"入力レベル（リアルタイム）", L"Input level (live)", L"Niveau d'entrée (live)", L"Livello ingresso (live)",
			L"Nivel de entrada (en vivo)", L"입력 레벨(실시간)", L"输入电平（实时）", L"مستوى الإدخال (مباشر)",
			L"Уровень входа (live)", L"Eingangspegel (live)", L"Nível de entrada (ao vivo)", L"Ingangsniveau (live)",
			L"Poziom wejścia (na żywo)", L"Giriş seviyesi (canlı)"));
		m_tooltip.AddTool(&m_help, LL14(L"操作ガイド", L"Operation guide", L"Guide", L"Guida", L"Guía",
			L"조작 가이드", L"操作指南", L"الدليل", L"Руководство", L"Anleitung",
			L"Guia", L"Handleiding", L"Przewodnik", L"Kılavuz"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 320, 8000);
	}
	RefreshOpaqueUi();
	StartCapture();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

void CSoundMeterDlg::PaintUiFromPeaks()
{
	LONG peak = InterlockedCompareExchange(&m_peak, 0, 0);
	const int decay = (m_response == 0) ? 70 : (m_response == 2) ? 95 : 88;
	InterlockedExchange(&m_peak, peak * decay / 100);
	LONG hold = InterlockedCompareExchange(&m_holdPeak, 0, 0);
	if (peak > hold) {
		hold = peak;
		InterlockedExchange(&m_holdPeak, hold);
	} else {
		InterlockedExchange(&m_holdPeak, hold * 98 / 100);
		hold = InterlockedCompareExchange(&m_holdPeak, 0, 0);
	}
	if (m_meter.GetSafeHwnd())
		m_meter.SetLevel(SmMeterUi(peak));

	CString dbfs;
	if (peak <= 0) {
		dbfs = L"-inf dBFS";
	} else {
		const float n = (float)peak / 1000.f;
		float db = 20.f * log10f(n < 1e-6f ? 1e-6f : n);
		if (db > 0.f) db = 0.f;
		dbfs.Format(L"%.1f dBFS", db);
	}
	if (m_dbfs.GetSafeHwnd()) m_dbfs.SetWindowText(dbfs);

	CString hs;
	if (hold <= 0) hs = L"Hold -inf";
	else {
		const float n = (float)hold / 1000.f;
		float db = 20.f * log10f(n < 1e-6f ? 1e-6f : n);
		hs.Format(L"Hold %.1f", db);
	}
	if (m_hold.GetSafeHwnd()) m_hold.SetWindowText(hs);
}

void CSoundMeterDlg::StartCapture()
{
	StopCapture();
	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_run, 0);
	InterlockedExchange(&m_peak, 0);
	uintptr_t th = _beginthreadex(NULL, 0, CaptureThread, this, 0, NULL);
	if (!th) {
		m_status.SetWindowText(LL14(
			L"キャプチャを開始できません。", L"Cannot start capture.", L"Impossible de démarrer.", L"Avvio non riuscito.",
			L"No se puede iniciar.", L"캡처를 시작할 수 없습니다.", L"无法开始捕获。", L"تعذر بدء الالتقاط.",
			L"Не удалось начать захват.", L"Capture start fehlgeschlagen.", L"Não foi possível iniciar.", L"Starten mislukt.",
			L"Nie można uruchomić.", L"Yakalamaya başlanamadı."));
		return;
	}
	m_thread = (HANDLE)th;
	SetTimer(SM_TIMER, 50, NULL);
	m_status.SetWindowText(LL14(L"測定中…", L"Measuring…", L"Mesure…", L"Misura…", L"Midiendo…",
		L"측정 중…", L"测量中…", L"جارٍ القياس…", L"Измерение…", L"Messung…",
		L"A medir…", L"Meten…", L"Pomiar…", L"Ölçülüyor…"));
}

void CSoundMeterDlg::StopCapture()
{
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 3000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	InterlockedExchange(&m_run, 0);
	if (GetSafeHwnd()) KillTimer(SM_TIMER);
}

UINT __stdcall CSoundMeterDlg::CaptureThread(void* p)
{
	CSoundMeterDlg* self = (CSoundMeterDlg*)p;
	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	IMMDeviceEnumerator* enumer = NULL;
	IMMDevice* micDev = NULL;
	IAudioClient* micClient = NULL;
	IAudioCaptureClient* micCap = NULL;
	WAVEFORMATEX* micFmt = NULL;
	HANDLE hMicEvent = NULL;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumer);
	if (FAILED(hr) || !enumer) goto done;

	{
		TCHAR want[256];
		want[0] = 0;
		const int sel = self->m_mic.GetSafeHwnd() ? self->m_mic.GetCurSel() : -1;
		if (sel >= 0 && sel < self->m_devCnt)
			_tcsncpy(want, self->m_devIds[sel], 255);
		else if (savedata.sm_mic_device[0])
			_tcsncpy(want, savedata.sm_mic_device, 255);
		want[255] = 0;
		if (want[0]) {
			hr = enumer->GetDevice(want, &micDev);
			if (FAILED(hr) || !micDev) {
				if (micDev) { micDev->Release(); micDev = NULL; }
				hr = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
			}
		} else {
			hr = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
		}
	}
	if (FAILED(hr) || !micDev) goto done;

	hr = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
	if (FAILED(hr) || !micClient) goto done;
	hr = micClient->GetMixFormat(&micFmt);
	if (FAILED(hr) || !micFmt) goto done;

	hMicEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	BOOL ok = FALSE;
	if (hMicEvent) {
		hr = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
			2000000, 0, micFmt, NULL);
		if (SUCCEEDED(hr)) hr = micClient->SetEventHandle(hMicEvent);
		if (SUCCEEDED(hr)) {
			hr = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
			if (SUCCEEDED(hr) && micCap) ok = TRUE;
		}
	}
	if (!ok) {
		if (hMicEvent) { CloseHandle(hMicEvent); hMicEvent = NULL; }
		if (micCap) { micCap->Release(); micCap = NULL; }
		if (micClient) { micClient->Release(); micClient = NULL; }
		if (micFmt) { CoTaskMemFree(micFmt); micFmt = NULL; }
		hr = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
		if (FAILED(hr) || !micClient) goto done;
		hr = micClient->GetMixFormat(&micFmt);
		if (FAILED(hr) || !micFmt) goto done;
		hr = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST,
			2000000, 0, micFmt, NULL);
		if (FAILED(hr)) goto done;
		hr = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
		if (FAILED(hr) || !micCap) goto done;
	}

	hr = micClient->Start();
	if (FAILED(hr)) goto done;
	InterlockedExchange(&self->m_run, 1);

	while (InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
		if (hMicEvent) WaitForSingleObject(hMicEvent, 50);
		else Sleep(10);
		UINT32 packet = 0;
		HRESULT hm = micCap->GetNextPacketSize(&packet);
		while (SUCCEEDED(hm) && packet > 0 && InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
			BYTE* data = NULL;
			UINT32 frames = 0;
			DWORD flags = 0;
			hm = micCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
			if (FAILED(hm)) break;
			if (frames > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
				float pk = 0.f;
				for (UINT32 i = 0; i < frames; ++i)
					SmSamplePeak(data + i * micFmt->nBlockAlign, micFmt, pk);
				const LONG v = (LONG)(pk * 1000.f);
				LONG cur = InterlockedCompareExchange(&self->m_peak, 0, 0);
				if (v > cur) InterlockedExchange(&self->m_peak, v > 1000 ? 1000 : v);
			}
			micCap->ReleaseBuffer(frames);
			hm = micCap->GetNextPacketSize(&packet);
		}
	}

done:
	if (micClient) micClient->Stop();
	if (micCap) micCap->Release();
	if (micClient) micClient->Release();
	if (micFmt) CoTaskMemFree(micFmt);
	if (hMicEvent) CloseHandle(hMicEvent);
	if (micDev) micDev->Release();
	if (enumer) enumer->Release();
	InterlockedExchange(&self->m_run, 0);
	if (SUCCEEDED(hrCo)) CoUninitialize();
	return 0;
}

void CSoundMeterDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == SM_TIMER)
		PaintUiFromPeaks();
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

void CSoundMeterDlg::OnCbnSelchangeMic()
{
	PersistUi();
	StartCapture();
}

void CSoundMeterDlg::OnBnClickedHelp() { ShowHelpSheet(); }
void CSoundMeterDlg::OnBnClickedClose() { DestroyWindow(); }
void CSoundMeterDlg::OnCancel() { DestroyWindow(); }
void CSoundMeterDlg::OnOK() {}

void CSoundMeterDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (m_hWnd) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CSoundMeterDlg::OnDestroy()
{
	AudioDevUnregisterNotifyHwnd(m_hWnd);
	PersistUi();
	StopCapture();
	CCustomBlurDialogBase::OnDestroy();
}

void CSoundMeterDlg::OnMicDevRefresh()
{
	AudioDevRebuildAll();
	FillMicCombo();
}

LRESULT CSoundMeterDlg::OnAudioDevChanged(WPARAM, LPARAM)
{
	FillMicCombo();
	return 0;
}

void CSoundMeterDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CCustomPopupMenu menu;
	menu.AddCheck(1, LL14(L"応答: 速い", L"Response: fast", L"Réponse: rapide", L"Risposta: veloce",
		L"Respuesta: rápida", L"응답: 빠름", L"响应：快", L"استجابة: سريعة",
		L"Отклик: быстрый", L"Ansprechzeit: schnell", L"Resposta: rápida", L"Reactie: snel",
		L"Odpowiedź: szybka", L"Yanıt: hızlı"), m_response == 0);
	menu.AddCheck(2, LL14(L"応答: 標準", L"Response: normal", L"Réponse: normale", L"Risposta: normale",
		L"Respuesta: normal", L"응답: 표준", L"响应：标准", L"استجابة: عادية",
		L"Отклик: обычный", L"Ansprechzeit: normal", L"Resposta: normal", L"Reactie: normaal",
		L"Odpowiedź: zwykła", L"Yanıt: normal"), m_response == 1);
	menu.AddCheck(3, LL14(L"応答: 遅い", L"Response: slow", L"Réponse: lente", L"Risposta: lenta",
		L"Respuesta: lenta", L"응답: 느림", L"响应：慢", L"استجابة: بطيئة",
		L"Отклик: медленный", L"Ansprechzeit: langsam", L"Resposta: lenta", L"Reactie: traag",
		L"Odpowiedź: wolna", L"Yanıt: yavaş"), m_response == 2);
	menu.AddSeparator();
	menu.AddCommand(4, LL14(L"ピークホールド解除", L"Clear peak hold", L"Effacer le hold", L"Azzera hold",
		L"Borrar hold", L"피크 홀드 해제", L"清除峰值保持", L"مسح الإمساك",
		L"Сбросить удержание", L"Peak-Hold löschen", L"Limpar hold", L"Peak-hold wissen",
		L"Wyczyść hold", L"Tepe tutmayı temizle"));
	CPoint sp = point;
	if (sp.x == -1 && sp.y == -1) {
		CRect rc; GetWindowRect(&rc);
		sp.x = rc.left + 40; sp.y = rc.top + 40;
	}
	UINT cmd = menu.Track(sp, this);
	if (cmd == 1) { m_response = 0; PersistUi(); }
	else if (cmd == 2) { m_response = 1; PersistUi(); }
	else if (cmd == 3) { m_response = 2; PersistUi(); }
	else if (cmd == 4) InterlockedExchange(&m_holdPeak, 0);
}

void OpenSoundMeterModeless(CWnd* parent)
{
	if (g_soundMeterDlg && g_soundMeterDlg->GetSafeHwnd()) {
		g_soundMeterDlg->ShowWindow(SW_SHOW);
		g_soundMeterDlg->SetForegroundWindow();
		return;
	}
	g_soundMeterDlg = new CSoundMeterDlg(parent);
	if (!g_soundMeterDlg->Create(IDD_SOUNDMETER, parent)) {
		delete g_soundMeterDlg;
		g_soundMeterDlg = NULL;
		return;
	}
	g_soundMeterDlg->ShowWindow(SW_SHOW);
}

void CloseSoundMeterIfOpen()
{
	if (g_soundMeterDlg && g_soundMeterDlg->GetSafeHwnd())
		g_soundMeterDlg->DestroyWindow();
}
