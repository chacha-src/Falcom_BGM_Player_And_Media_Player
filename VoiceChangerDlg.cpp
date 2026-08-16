#include "stdafx.h"
#include "ogg.h"
#include "VoiceChangerDlg.h"
#include "AudioDevSync.h"
#include "CCustomPopupMenu.h"
#include "resource.h"
#include "VcVocalTract.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <process.h>
#include <math.h>
#include <vector>

#pragma comment(lib,"Ole32.lib")
extern void MpPersistSavedataQuick();

namespace {

static const GUID vcFloat = { 3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71} };
static const GUID vcPcm = { 1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71} };

static const int kPctVals[] = {
	50,55,60,65,70,75,80,85,90,95,100,105,110,115,120,125,130,135,140,150,160,170,175,180,185,190,195,200,
	210,220,230,240,250
};
static const int kBrightVals[] = { 50,60,70,80,90,100,110,120,130,140,150 };
static const int kBreathVals[] = { 0,5,10,15,20,30,40,50,70,100 };

static void VcRead(const BYTE* p, const WAVEFORMATEX* f, float& l, float& r)
{
	l = r = 0; if (!p || !f) return;
	int ch = f->nChannels, b = f->wBitsPerSample;
	BOOL fl = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && b == 32 && ((WAVEFORMATEXTENSIBLE*)f)->SubFormat == vcFloat);
	BOOL pc = f->wFormatTag == WAVE_FORMAT_PCM || (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ((WAVEFORMATEXTENSIBLE*)f)->SubFormat == vcPcm);
	if (fl) { const float* q = (const float*)p; l = q[0]; r = ch > 1 ? q[1] : l; }
	else if (pc && b == 16) { const short* q = (const short*)p; l = q[0] / 32768.f; r = ch > 1 ? q[1] / 32768.f : l; }
	else if (pc && b == 24) {
		int x = p[0] | p[1] << 8 | p[2] << 16; if (x & 0x800000) x |= ~0xffffff; l = x / 8388608.f;
		if (ch > 1) { p += 3; x = p[0] | p[1] << 8 | p[2] << 16; if (x & 0x800000) x |= ~0xffffff; r = x / 8388608.f; }
		else r = l;
	}
	else if (pc && b == 32) { const int* q = (const int*)p; l = q[0] / 2147483648.f; r = ch > 1 ? q[1] / 2147483648.f : l; }
}

static LPCTSTR VcPresetName(int n)
{
	switch (n) {
	case 1: return LL14(L"男→女（標準）", L"Male → Female (std)", L"Homme → Femme (std)", L"Uomo → Donna (std)", L"Hombre → Mujer (std)", L"남→여(표준)", L"男→女（标准）", L"ذكر→أنثى (قياسي)", L"М→Ж (стандарт)", L"Mann → Frau (std)", L"Homem → Mulher (padrão)", L"Man → Vrouw (std)", L"Mężczyzna → Kobieta (std)", L"Erkek → Kadın (std)");
	case 2: return LL14(L"女→男（標準）", L"Female → Male (std)", L"Femme → Homme (std)", L"Donna → Uomo (std)", L"Mujer → Hombre (std)", L"여→남(표준)", L"女→男（标准）", L"أنثى→ذكر (قياسي)", L"Ж→М (стандарт)", L"Frau → Mann (std)", L"Mulher → Homem (padrão)", L"Vrouw → Man (std)", L"Kobieta → Mężczyzna (std)", L"Kadın → Erkek (std)");
	case 3: return LL14(L"少女ボイス", L"Young girl", L"Jeune fille", L"Ragazza", L"Chica", L"소녀", L"少女", L"فتاة", L"Девочка", L"Mädchen", L"Menina", L"Meisje", L"Dziewczyna", L"Genç kız");
	case 4: return LL14(L"少年ボイス", L"Young boy", L"Jeune garçon", L"Ragazzo", L"Chico", L"소년", L"少年", L"فتى", L"Мальчик", L"Junge", L"Menino", L"Jongen", L"Chłopiec", L"Genç erkek");
	case 5: return LL14(L"低音ボイス", L"Deep male", L"Voix grave", L"Voce grave", L"Voz grave", L"저음", L"低沉男声", L"صوت عميق", L"Низкий", L"Tiefe Stimme", L"Voz grave", L"Diepe stem", L"Niski głos", L"Kalın ses");
	case 6: return LL14(L"ロボット", L"Robot", L"Robot", L"Robot", L"Robot", L"로봇", L"机器人", L"روبوت", L"Робот", L"Roboter", L"Robô", L"Robot", L"Robot", L"Robot");
	case 7: return LL14(L"ラジオ/電話", L"Radio / Phone", L"Radio / Téléphone", L"Radio / Telefono", L"Radio / Teléfono", L"라디오/전화", L"电台/电话", L"راديو/هاتف", L"Радио/телефон", L"Radio / Telefon", L"Rádio / Telefone", L"Radio / Telefoon", L"Radio / Telefon", L"Radyo / Telefon");
	case 8: return LL14(L"チップマンク", L"Chipmunk", L"Tamia", L"Scoiattolo", L"Ardilla", L"다람쥐", L"花栗鼠", L"سنجاب", L"Бурундук", L"Eichhörnchen", L"Esquilo", L"Eekhoorn", L"Wiewiórka", L"Sincap");
	case 9: return LL14(L"男→女子高生", L"Male → High-school girl", L"Homme → Lycéenne", L"Uomo → Liceale", L"Hombre → Estudiante HS", L"남→여고생", L"男→女高中生", L"ذكر→طالبة ثانوية", L"М→школьница", L"Mann → Schülerin", L"Homem → Colegial", L"Man → Scholiere", L"M→uczennica LO", L"Erkek → Lise kızı");
	case 10: return LL14(L"男→女子大生", L"Male → College girl", L"Homme → Étudiante", L"Uomo → Universitaria", L"Hombre → Universitaria", L"남→여대생", L"男→女大学生", L"ذكر→طالبة جامعية", L"М→студентка", L"Mann → Studentin", L"Homem → Universitária", L"Man → Studente", L"M→studentka", L"Erkek → Üniversiteli");
	case 11: return LL14(L"男→高め女性", L"Male → Higher female", L"Homme → Femme aiguë", L"Uomo → Donna acuta", L"Hombre → Mujer aguda", L"남→높은 여", L"男→偏高女声", L"ذكر→أنثى أعلى", L"М→высокий Ж", L"Mann → höhere Frau", L"Homem → Mulher aguda", L"Man → Hogere vrouw", L"M→wyższa K", L"Erkek → Yüksek kadın");
	case 12: return LL14(L"男→低め女性", L"Male → Lower female", L"Homme → Femme grave", L"Uomo → Donna grave", L"Hombre → Mujer grave", L"남→낮은 여", L"男→偏低女声", L"ذكر→أنثى أعمق", L"М→низкий Ж", L"Mann → tiefere Frau", L"Homem → Mulher grave", L"Man → Lagere vrouw", L"M→niższa K", L"Erkek → Alçak kadın");
	case 13: return LL14(L"男→年配女性", L"Male → Mature female", L"Homme → Femme mûre", L"Uomo → Donna matura", L"Hombre → Mujer madura", L"남→중년 여", L"男→年长女声", L"ذكر→أنثى ناضجة", L"М→зрелая Ж", L"Mann → reife Frau", L"Homem → Mulher madura", L"Man → Rijpe vrouw", L"M→dojrzała K", L"Erkek → Olgun kadın");
	case 14: return LL14(L"男→小学生女子", L"Male → Grade-school girl", L"Homme → Fillette", L"Uomo → Bambina", L"Hombre → Niña", L"남→초등 여", L"男→小学女生", L"ذكر→تلميذة", L"М→девочка", L"Mann → Grundschülerin", L"Homem → Menina", L"Man → Schoolmeisje", L"M→dziewczynka", L"Erkek → İlkokul kızı");
	case 15: return LL14(L"男→中学生女子", L"Male → Middle-school girl", L"Homme → Collégienne", L"Uomo → Media", L"Hombre → Secundaria", L"남→중등 여", L"男→初中女生", L"ذكر→إعدادية", L"М→подросток Ж", L"Mann → Mittelschülerin", L"Homem → Adolescente", L"Man → Middelbare scholiere", L"M→nastolatka", L"Erkek → Ortaokul kızı");
	case 16: return LL14(L"女→高め男性", L"Female → Higher male", L"Femme → Homme aigu", L"Donna → Uomo acuto", L"Mujer → Hombre agudo", L"여→높은 남", L"女→偏高男声", L"أنثى→ذكر أعلى", L"Ж→высокий М", L"Frau → höherer Mann", L"Mulher → Homem agudo", L"Vrouw → Hogere man", L"K→wyższy M", L"Kadın → Yüksek erkek");
	case 17: return LL14(L"女→低め男性", L"Female → Lower male", L"Femme → Homme grave", L"Donna → Uomo grave", L"Mujer → Hombre grave", L"여→낮은 남", L"女→偏低男声", L"أنثى→ذكر أعمق", L"Ж→низкий М", L"Frau → tieferer Mann", L"Mulher → Homem grave", L"Vrouw → Lagere man", L"K→niższy M", L"Kadın → Alçak erkek");
	case 18: return LL14(L"女→青年", L"Female → Young man", L"Femme → Jeune homme", L"Donna → Giovane", L"Mujer → Joven", L"여→청년", L"女→青年", L"أنثى→شاب", L"Ж→юноша", L"Frau → junger Mann", L"Mulher → Jovem", L"Vrouw → Jonge man", L"K→młody mężczyzna", L"Kadın → Genç erkek");
	case 19: return LL14(L"女→年配男性", L"Female → Mature male", L"Femme → Homme mûr", L"Donna → Uomo maturo", L"Mujer → Hombre maduro", L"여→중년 남", L"女→年长男声", L"أنثى→رجل ناضج", L"Ж→зрелый М", L"Frau → reifer Mann", L"Mulher → Homem maduro", L"Vrouw → Rijpe man", L"K→dojrzały M", L"Kadın → Olgun erkek");
	case 20: return LL14(L"女→小学生男子", L"Female → Grade-school boy", L"Femme → Garçonnet", L"Donna → Bambino", L"Mujer → Niño", L"여→초등 남", L"女→小学男生", L"أنثى→تلميذ", L"Ж→мальчик", L"Frau → Grundschüler", L"Mulher → Menino", L"Vrouw → Schooljongen", L"K→chłopiec", L"Kadın → İlkokul erkeği");
	case 21: return LL14(L"女→中学生男子", L"Female → Middle-school boy", L"Femme → Collégien", L"Donna → Ragazzo media", L"Mujer → Adolescente", L"여→중등 남", L"女→初中男生", L"أنثى→إعدادي", L"Ж→подросток М", L"Frau → Mittelschüler", L"Mulher → Adolescente", L"Vrouw → Middelbare scholier", L"K→nastolatek", L"Kadın → Ortaokul erkeği");
	case 22: return LL14(L"カスタム", L"Custom", L"Perso", L"Personalizzato", L"Personalizado", L"사용자", L"自定义", L"مخصص", L"Свой", L"Benutzerdefiniert", L"Personalizado", L"Aangepast", L"Własny", L"Özel");
	default: return LL14(L"標準（原音）", L"Normal", L"Normal", L"Normale", L"Normal", L"표준", L"标准", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal");
	}
}

static LPCTSTR VcStyleName(int n)
{
	switch (n) {
	case 1: return LL14(L"ロボット", L"Robot", L"Robot", L"Robot", L"Robot", L"로봇", L"机器人", L"روبوت", L"Робот", L"Roboter", L"Robô", L"Robot", L"Robot", L"Robot");
	case 2: return LL14(L"ラジオ/電話", L"Radio / Phone", L"Radio / Téléphone", L"Radio / Telefono", L"Radio / Teléfono", L"라디오/전화", L"电台/电话", L"راديو/هاتف", L"Радио/телефон", L"Radio / Telefon", L"Rádio / Telefone", L"Radio / Telefoon", L"Radio / Telefon", L"Radyo / Telefon");
	default: return LL14(L"なし", L"None", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"لا شيء", L"Нет", L"Keine", L"Nenhum", L"Geen", L"Brak", L"Yok");
	}
}

// pitch, formant, gain, bright, breath, quality, style
// 0..8 は旧カタログ互換。9..21 が男女バリエーション、末尾がカスタム。
// 男→女: 普通トーンの男声でも女域に届くよう F0 を強め（上限250%）。
// 女→男: 同様に下げ方向を維持。
static const int kPresetVals[CVoiceChangerDlg::VC_PRESET_N][7] = {
	{ 100, 100, 100, 100, 0, 1, 0 }, // 0 標準
	{ 215, 116, 108, 106, 0, 1, 0 }, // 1 男→女（標準）
	{  58,  88, 106,  90, 0, 1, 0 }, // 2 女→男（標準）
	{ 235, 124, 106, 114, 4, 1, 0 }, // 3 少女
	{ 145, 112, 100, 108, 0, 1, 0 }, // 4 少年
	{  52,  80, 112,  78, 0, 1, 0 }, // 5 低音
	{ 100,  75, 110, 130, 0, 0, 1 }, // 6 ロボット
	{ 100, 100, 115,  75, 0, 0, 2 }, // 7 ラジオ
	{ 240, 100,  90, 115, 4, 0, 0 }, // 8 チップマンク
	{ 225, 118, 106, 114, 6, 1, 0 }, // 9 男→女子高生
	{ 210, 114, 108, 106, 0, 1, 0 }, // 10 男→女子大生
	{ 230, 118, 105, 116, 2, 1, 0 }, // 11 男→高め女性
	{ 195, 112, 110, 102, 0, 1, 0 }, // 12 男→低め女性
	{ 185, 108, 108,  92, 0, 1, 0 }, // 13 男→年配女性
	{ 240, 126, 104, 118, 10, 1, 0 }, // 14 男→小学生女子
	{ 225, 122, 105, 116, 4, 1, 0 }, // 15 男→中学生女子
	{  68,  92, 104,  96, 0, 1, 0 }, // 16 女→高め男性
	{  52,  84, 110,  82, 0, 1, 0 }, // 17 女→低め男性
	{  62,  90, 105,  94, 0, 1, 0 }, // 18 女→青年
	{  50,  82, 112,  76, 0, 1, 0 }, // 19 女→年配男性
	{ 108, 112, 100, 108, 2, 1, 0 }, // 20 女→小学生男子
	{  88, 102, 102, 100, 0, 1, 0 }, // 21 女→中学生男子
	{ 100, 100, 100, 100, 0, 1, 0 }, // 22 カスタム
};

static int VcNearest(const int* vals, int n, int v)
{
	int best = 0;
	for (int i = 1; i < n; ++i)
		if (abs(vals[i] - v) < abs(vals[best] - v)) best = i;
	return best;
}

static HRESULT VcOpenRender(IMMDevice* dev, IAudioClient** c, IAudioRenderClient** r, UINT32* frames)
{
	*c = NULL; *r = NULL; *frames = 0; if (!dev) return E_POINTER;
	WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0 };
	HRESULT h = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)c);
	if (SUCCEEDED(h)) h = (*c)->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, &f, NULL);
	if (SUCCEEDED(h)) h = (*c)->GetBufferSize(frames);
	if (SUCCEEDED(h)) h = (*c)->GetService(__uuidof(IAudioRenderClient), (void**)r);
	return h;
}

static void VcWrite(IAudioClient* c, IAudioRenderClient* r, UINT32 total, const short* pcm, int frames)
{
	if (!c || !r || !pcm || frames <= 0) return;
	UINT32 pad = 0; if (FAILED(c->GetCurrentPadding(&pad))) return;
	UINT32 n = total > pad ? total - pad : 0;
	if (n > (UINT32)frames) n = frames;
	if (!n) return;
	BYTE* b = NULL;
	if (SUCCEEDED(r->GetBuffer(n, &b))) { memcpy(b, pcm, n * 4); r->ReleaseBuffer(n, 0); }
}

class CVcHelp : public CDialog {
public:
	CVcHelp(CWnd* p) : CDialog(IDD_VC_HELP, p) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void OnOK() { DestroyWindow(); }
	virtual void OnCancel() { DestroyWindow(); }
	virtual void PostNcDestroy();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* d) { CRect r; GetClientRect(r); d->FillSolidRect(r, RGB(248, 248, 252)); return TRUE; }
	afx_msg void OnClose() { DestroyWindow(); }
	DECLARE_MESSAGE_MAP()
};
static CVcHelp* vcHelp = NULL;
BEGIN_MESSAGE_MAP(CVcHelp, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CVcHelp::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetWindowText(LL14(L"ボイスチェンジャーガイド", L"Voice changer guide", L"Guide changeur de voix", L"Guida cambia voce", L"Guía del cambiador de voz", L"보이스 체인저 가이드", L"变声器指南", L"دليل مغير الصوت", L"Гид по изменению голоса", L"Stimmenwandler-Hilfe", L"Guia do modificador de voz", L"Gids stemvervormer", L"Przewodnik zmiany głosu", L"Ses değiştirici rehberi"));
	return TRUE;
}
void CVcHelp::PostNcDestroy() { CDialog::PostNcDestroy(); if (vcHelp == this) vcHelp = NULL; delete this; }
void CVcHelp::OnPaint()
{
	CPaintDC p(this);
	CCC_GdiHelpPaint h;
	if (!CCC_GdiHelpBeginPaint(this, p, h)) return;
	CDC& d = h.mem;
	d.SetBkMode(TRANSPARENT);
	d.SelectObject(GetFont());
	d.SetTextColor(RGB(55, 45, 85));
	int y = 10;
	d.TextOut(10, y, LL14(L"リアルタイム・ボイスチェンジャー", L"Real-time voice changer", L"Changeur de voix temps réel", L"Cambia voce in tempo reale", L"Cambiador de voz en tiempo real", L"실시간 보이스 체인저", L"实时变声器", L"مغير صوت آني", L"Изменение голоса в реальном времени", L"Echtzeit-Stimmenwandler", L"Modificador de voz em tempo real", L"Realtime stemvervormer", L"Zmiana głosu w czasie rzeczywistym", L"Gerçek zamanlı ses değiştirici"));
	y += 28;
	d.SetTextColor(RGB(65, 65, 80));
	LPCTSTR a[] = {
		LL14(L"エンジン: LPCで声道を分離し、残差(声帯)を常にF0変換。有声ゲートで素通ししません。", L"Engine: LPC splits tract; residual F0 always applied (no voiced bypass).", L"Moteur: LPC + F0 toujours sur résidu.", L"Motore: LPC + F0 sempre sul residuo.", L"Motor: LPC + F0 siempre en residuo.", L"엔진: LPC 성도 분리, 잔차 F0 항상 적용.", L"引擎：LPC分离声道，残差F0始终施加。", L"المحرك: LPC وF0 دائماً على المتبقي.", L"Движок: LPC, F0 всегда на остатке.", L"Engine: LPC, F0 immer auf Residual.", L"Motor: LPC, F0 sempre no residual.", L"Engine: LPC, F0 altijd op residu.", L"Silnik: LPC, F0 zawsze na residuum.", L"Motor: LPC, F0 her zaman residüelde."),
		LL14(L"女声は F0 を上げ、Formant は控えめ（声道やや短）。同じ%だと変装ボイスになります。", L"Female: raise F0, keep Formant modest. Matching % sounds like a disguise.", L"Femme: F0↑, Formant modéré. Même % = voix déguisée.", L"Donna: F0↑, Formant moderato. Stessi % = travestimento.", L"Mujer: F0↑, Formant moderado. Mismo % = disfraz.", L"여声: F0↑, Formant은 소폭. 동일%는 변장음.", L"女声：提高 F0，Formant 适中。同%会像伪装音。", L"أنثى: ارفع F0 وFormant معتدل.", L"Женский: F0↑, Formant умеренно.", L"Weiblich: F0↑, Formant moderat. Gleiche % = Verkleidung.", L"Feminino: F0↑, Formant moderado. Mesmo % = disfarce.", L"Vrouwelijk: F0↑, Formant bescheiden.", L"Żeński: F0↑, Formant umiarkowany.", L"Kadın: F0↑, Formant ılımlı. Aynı % maske sesi."),
		LL14(L"プリセットで男→女／女→男を細かく選べます（JK・高低・年配・小中学生など）。", L"Presets cover Male↔Female variants (JK, high/low, mature, school ages).", L"Presets Homme↔Femme détaillés (lycée, aigu/grave, âge scolaire).", L"Preset Uomo↔Donna dettagliati (liceo, acuto/grave, età scolare).", L"Presets Hombre↔Mujer detallados (instituto, agudo/grave, edad escolar).", L"남↔여 세부 프리셋(여고·고저·중년·초중등).", L"预设含男↔女细项（高中生、高低、年长、中小学）。", L"إعدادات ذكر↔أنثى مفصّلة (ثانوية، حاد/عميق، أعمار).", L"Пресеты М↔Ж подробно (школа, высоко/низко, возраст).", L"Presets Mann↔Frau detailliert (Schule, hoch/tief, Alter).", L"Presets Homem↔Mulher detalhados (escola, agudo/grave, idade).", L"Presets Man↔Vrouw (school, hoog/laag, leeftijd).", L"Presety M↔K (szkoła, wysoki/niski, wiek).", L"Erkek↔Kadın önayarları (lise, yüksek/alçak, yaş)."),
		LL14(L"Bright=存在感、Breath=息成分。Quality=低遅延/高音質。", L"Bright=presence, Breath=air. Quality=latency/quality.", L"Bright=présence, Breath=souffle. Quality=latence.", L"Bright=presenza, Breath=fiato. Quality=latenza.", L"Bright=presencia, Breath=aire. Quality=latencia.", L"Bright=존재감, Breath=숨. Quality=지연/음질.", L"Bright=存在感，Breath=气息。Quality=延迟/音质。", L"Bright=حضور، Breath=نفس. Quality=زمن/جودة.", L"Bright=яркость, Breath=дыхание. Quality=задержка.", L"Bright=Präsenz, Breath=Atem. Quality=Latenz.", L"Bright=presença, Breath=ar. Quality=latência.", L"Bright=aanwezigheid, Breath=adem. Quality=latentie.", L"Bright=obecność, Breath=oddech. Quality=opóźnienie.", L"Bright=varlık, Breath=nefes. Quality=gecikme."),
		LL14(L"MP／画面キャプチャは「VC適用」ONで同じプリセットがマイクに乗ります。", L"MP / screen capture use the same preset when Apply VC is ON.", L"MP / capture: case Appliquer VC = même preset micro.", L"MP / cattura: spunta Applica VC = stesso preset micro.", L"MP / captura: casilla Aplicar VC = mismo preset de mic.", L"MP/화면캡처는 VC적용 ON 시 같은 프리셋.", L"MP/画面捕获在勾选应用VC时用同一预设。", L"MP/التقاط: تأشير تطبيق VC = نفس الإعداد للميك.", L"MP/захват: галочка VC — тот же пресет на микрофон.", L"MP/Aufnahme: VC-Haken = gleiches Preset am Mic.", L"MP/captura: marca Aplicar VC = mesmo preset no micro.", L"MP/opname: VC-vink = zelfde preset op micro.", L"MP/nagranie: zaznaczenie VC = ten sam preset na mik.", L"MP/ekran: VC işaretliyse aynı önayar mikrofona gider."),
		LL14(L"出力は仮想ケーブル推奨。Discord 等の入力にその端末を指定します。", L"Use a virtual cable output; set it as Discord mic input.", L"Sortie câble virtuel; micro Discord = ce périphérique.", L"Usa cavo virtuale; impostalo come microfono Discord.", L"Use cable virtual; póngalo como micrófono de Discord.", L"가상 케이블 출력 권장. Discord 입력으로 지정.", L"建议虚拟线缆输出，并设为 Discord 麦克风。", L"يُفضل كابل افتراضي كخرج لمكبر Discord.", L"Рекомендуется виртуальный кабель как вход Discord.", L"Virtuelles Kabel als Discord-Mikrofon nutzen.", L"Use cabo virtual como microfone do Discord.", L"Gebruik virtuele kabel als Discord-microfoon.", L"Użyj kabla wirtualnego jako mikrofonu Discord.", L"Discord mikrofonu olarak sanal kablo kullanın."),
	};
	for (int i = 0; i < _countof(a); ++i) { d.TextOut(10, y, a[i]); y += 22; }
	CCC_GdiHelpEndPaint(h);
}

} // namespace

IMPLEMENT_DYNAMIC(CVoiceChangerDlg, CCustomBlurDialogBase)
static CVoiceChangerDlg* g_vc = NULL;

CVoiceChangerDlg::CVoiceChangerDlg(CWnd* p)
	: CCustomBlurDialogBase(IDD, p)
	, m_micCnt(0), m_outCnt(0)
	, m_stop(0), m_run(0), m_peak(0), m_lastHr(0)
	, m_thread(NULL), m_devLocked(FALSE)
	, m_pitchPct(100), m_formPct(100), m_gainPct(100)
	, m_brightPct(100), m_breathPct(0), m_quality(1), m_fxStyle(0), m_monitorOn(0)
{
	memset(m_micIds, 0, sizeof(m_micIds));
	memset(m_outIds, 0, sizeof(m_outIds));
}
CVoiceChangerDlg::~CVoiceChangerDlg() { StopAudio(); }

void CVoiceChangerDlg::DoDataExchange(CDataExchange* p)
{
	CCustomBlurDialogBase::DoDataExchange(p);
	DDX_Control(p, IDC_VC_HELP, m_help);
	DDX_Control(p, IDC_VC_MIC_L, m_micL);
	DDX_Control(p, IDC_VC_MIC, m_mic);
	DDX_Control(p, IDC_VC_MIC_REFRESH, m_micRefresh);
	DDX_Control(p, IDC_VC_OUT_L, m_outL);
	DDX_Control(p, IDC_VC_OUT, m_out);
	DDX_Control(p, IDC_VC_OUT_REFRESH, m_outRefresh);
	DDX_Control(p, IDC_VC_PITCH_L, m_pitchL);
	DDX_Control(p, IDC_VC_PITCH, m_pitch);
	DDX_Control(p, IDC_VC_FORM_L, m_formL);
	DDX_Control(p, IDC_VC_FORM, m_form);
	DDX_Control(p, IDC_VC_GAIN_L, m_gainL);
	DDX_Control(p, IDC_VC_GAIN, m_gain);
	DDX_Control(p, IDC_VC_BRIGHT_L, m_brightL);
	DDX_Control(p, IDC_VC_BRIGHT, m_bright);
	DDX_Control(p, IDC_VC_BREATH_L, m_breathL);
	DDX_Control(p, IDC_VC_BREATH, m_breath);
	DDX_Control(p, IDC_VC_QUAL_L, m_qualL);
	DDX_Control(p, IDC_VC_QUAL, m_qual);
	DDX_Control(p, IDC_VC_PRESET_L, m_presetL);
	DDX_Control(p, IDC_VC_PRESET, m_preset);
	DDX_Control(p, IDC_VC_STYLE_L, m_styleL);
	DDX_Control(p, IDC_VC_STYLE, m_fx);
	DDX_Control(p, IDC_VC_MONITOR, m_monitor);
	DDX_Control(p, IDC_VC_METER_L, m_meterL);
	DDX_Control(p, IDC_VC_METER, m_meter);
	DDX_Control(p, IDC_VC_START, m_start);
	DDX_Control(p, IDC_VC_CLOSE, m_close);
	DDX_Control(p, IDC_VC_STATUS, m_status);
}

BEGIN_MESSAGE_MAP(CVoiceChangerDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_VC_START, OnStart)
	ON_BN_CLICKED(IDC_VC_CLOSE, OnCloseBtn)
	ON_BN_CLICKED(IDC_VC_HELP, OnHelp)
	ON_BN_CLICKED(IDC_VC_MONITOR, OnChanged)
	ON_BN_CLICKED(IDC_VC_MIC_REFRESH, OnMicDevRefresh)
	ON_BN_CLICKED(IDC_VC_OUT_REFRESH, OnMicDevRefresh)
	ON_MESSAGE(WM_AUDIODEV_CHANGED, OnAudioDevChanged)
	ON_CBN_SELCHANGE(IDC_VC_PRESET, OnPreset)
	ON_CBN_SELCHANGE(IDC_VC_MIC, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_OUT, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_PITCH, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_FORM, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_GAIN, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_BRIGHT, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_BREATH, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_QUAL, OnChanged)
	ON_CBN_SELCHANGE(IDC_VC_STYLE, OnChanged)
	// m_fx = FX style combo
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

BOOL CVoiceChangerDlg::PreTranslateMessage(MSG* p)
{
	if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(p);
	return CCustomBlurDialogBase::PreTranslateMessage(p);
}
void CVoiceChangerDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_vc == this) g_vc = NULL;
	delete this;
}
void CVoiceChangerDlg::LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }

void CVoiceChangerDlg::FillDevices()
{
	AudioMicDevRefresh();
	m_mic.ResetContent();
	int sel = 0; m_micCnt = 0;
	for (int i = 0; i < AudioMicDevCount() && m_micCnt < VC_MAX; i++) {
		LPCTSTR id = AudioMicDevId(i);
		// i==0 は既定（空ID）も載せる
		if (i > 0 && (!id || !*id)) continue;
		_tcsncpy(m_micIds[m_micCnt], id ? id : L"", 255); m_micIds[m_micCnt][255] = 0;
		m_mic.AddString(AudioMicDevName(i));
		if (savedata.vc_mic_device[0] ? !_tcsicmp(id ? id : L"", savedata.vc_mic_device) : (i == 0))
			sel = m_micCnt;
		m_micCnt++;
	}
	if (m_micCnt) m_mic.SetCurSel(sel);
	AudioLoopDevRefresh();
	m_out.ResetContent();
	sel = 0; m_outCnt = 0;
	for (int i = 0; i < AudioLoopDevCount() && m_outCnt < VC_MAX; i++) {
		LPCTSTR id = AudioLoopDevId(i);
		if (i > 0 && (!id || !*id)) continue;
		_tcsncpy(m_outIds[m_outCnt], id ? id : L"", 255); m_outIds[m_outCnt][255] = 0;
		m_out.AddString(AudioLoopDevName(i));
		if (savedata.vc_out_device[0] ? !_tcsicmp(id ? id : L"", savedata.vc_out_device) : (i == 0))
			sel = m_outCnt;
		m_outCnt++;
	}
	if (m_outCnt) m_out.SetCurSel(sel);
}

void CVoiceChangerDlg::SelectComboPct(CCustomComboBox& cb, int pct, const int* vals, int nVals)
{
	cb.SetCurSel(VcNearest(vals, nVals, pct));
}
int CVoiceChangerDlg::ComboPct(CCustomComboBox& cb, const int* vals, int nVals) const
{
	int i = cb.GetCurSel();
	if (i < 0) i = 0;
	if (i >= nVals) i = nVals - 1;
	return vals[i];
}

void CVoiceChangerDlg::FillValues()
{
	auto fillPct = [&](CCustomComboBox& cb, const int* vals, int n, int cur) {
		cb.ResetContent();
		for (int i = 0; i < n; ++i) {
			CString x; x.Format(L"%d%%", vals[i]);
			cb.AddString(x);
		}
		SelectComboPct(cb, cur, vals, n);
	};
	fillPct(m_pitch, kPctVals, _countof(kPctVals), savedata.vc_pitch);
	fillPct(m_form, kPctVals, _countof(kPctVals), savedata.vc_formant);
	fillPct(m_gain, kPctVals, _countof(kPctVals), savedata.vc_gain);
	fillPct(m_bright, kBrightVals, _countof(kBrightVals), savedata.vc_bright);
	m_breath.ResetContent();
	for (int i = 0; i < _countof(kBreathVals); ++i) {
		CString x; x.Format(L"%d", kBreathVals[i]);
		m_breath.AddString(x);
	}
	SelectComboPct(m_breath, savedata.vc_breath, kBreathVals, _countof(kBreathVals));
	m_qual.ResetContent();
	m_qual.AddString(LL14(L"低遅延", L"Low latency", L"Faible latence", L"Bassa latenza", L"Baja latencia", L"저지연", L"低延迟", L"كمون منخفض", L"Низкая задержка", L"Niedrige Latenz", L"Baixa latência", L"Lage latentie", L"Niskie opóźnienie", L"Düşük gecikme"));
	m_qual.AddString(LL14(L"高音質", L"High quality", L"Haute qualité", L"Alta qualità", L"Alta calidad", L"고음질", L"高音质", L"جودة عالية", L"Высокое качество", L"Hohe Qualität", L"Alta qualidade", L"Hoge kwaliteit", L"Wysoka jakość", L"Yüksek kalite"));
	m_qual.SetCurSel(savedata.vc_quality ? 1 : 0);
	m_fx.ResetContent();
	for (int i = 0; i < 3; ++i) m_fx.AddString(VcStyleName(i));
	m_fx.SetCurSel(max(0, min(2, savedata.vc_style)));
	m_preset.ResetContent();
	for (int i = 0; i < VC_PRESET_N; ++i) m_preset.AddString(VcPresetName(i));
	m_preset.SetCurSel(max(0, min(VC_PRESET_N - 1, savedata.vc_preset)));
	m_monitor.SetCheck(savedata.vc_monitor ? BST_CHECKED : BST_UNCHECKED);
	SyncLiveParams();
}

void CVoiceChangerDlg::SyncLiveParams()
{
	InterlockedExchange(&m_pitchPct, savedata.vc_pitch);
	InterlockedExchange(&m_formPct, savedata.vc_formant);
	InterlockedExchange(&m_gainPct, savedata.vc_gain);
	InterlockedExchange(&m_brightPct, savedata.vc_bright);
	InterlockedExchange(&m_breathPct, savedata.vc_breath);
	InterlockedExchange(&m_quality, savedata.vc_quality ? 1 : 0);
	InterlockedExchange(&m_fxStyle, max(0, min(2, savedata.vc_style)));
	InterlockedExchange(&m_monitorOn, savedata.vc_monitor ? 1 : 0);
}

void CVoiceChangerDlg::PersistUi(BOOL markCustom)
{
	int i = m_mic.GetCurSel();
	if (i >= 0 && i < m_micCnt) { _tcsncpy(savedata.vc_mic_device, m_micIds[i], 255); savedata.vc_mic_device[255] = 0; }
	i = m_out.GetCurSel();
	if (i >= 0 && i < m_outCnt) { _tcsncpy(savedata.vc_out_device, m_outIds[i], 255); savedata.vc_out_device[255] = 0; }
	savedata.vc_pitch = ComboPct(m_pitch, kPctVals, _countof(kPctVals));
	savedata.vc_formant = ComboPct(m_form, kPctVals, _countof(kPctVals));
	savedata.vc_gain = ComboPct(m_gain, kPctVals, _countof(kPctVals));
	savedata.vc_bright = ComboPct(m_bright, kBrightVals, _countof(kBrightVals));
	savedata.vc_breath = ComboPct(m_breath, kBreathVals, _countof(kBreathVals));
	savedata.vc_quality = m_qual.GetCurSel() > 0 ? 1 : 0;
	savedata.vc_style = max(0, min(2, m_fx.GetCurSel()));
	savedata.vc_monitor = m_monitor.GetCheck() == BST_CHECKED;
	if (markCustom) {
		savedata.vc_preset = VC_PRESET_N - 1;
		if (m_preset.GetCurSel() != VC_PRESET_N - 1) m_preset.SetCurSel(VC_PRESET_N - 1);
	} else {
		savedata.vc_preset = max(0, min(VC_PRESET_N - 1, m_preset.GetCurSel()));
	}
	SyncLiveParams();
	MpPersistSavedataQuick();
}

void CVoiceChangerDlg::ApplyPreset(int n)
{
	if (n < 0 || n >= VC_PRESET_N) return;
	if (n == VC_PRESET_N - 1) { m_preset.SetCurSel(n); PersistUi(FALSE); return; }
	const int* p = kPresetVals[n];
	SelectComboPct(m_pitch, p[0], kPctVals, _countof(kPctVals));
	SelectComboPct(m_form, p[1], kPctVals, _countof(kPctVals));
	SelectComboPct(m_gain, p[2], kPctVals, _countof(kPctVals));
	SelectComboPct(m_bright, p[3], kBrightVals, _countof(kBrightVals));
	SelectComboPct(m_breath, p[4], kBreathVals, _countof(kBreathVals));
	m_qual.SetCurSel(p[5] ? 1 : 0);
	m_fx.SetCurSel(max(0, min(2, p[6])));
	m_preset.SetCurSel(n);
	PersistUi(FALSE);
}

BOOL CVoiceChangerDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_mic.SetAeroMode(FALSE); m_out.SetAeroMode(FALSE); m_pitch.SetAeroMode(FALSE); m_form.SetAeroMode(FALSE);
	m_gain.SetAeroMode(FALSE); m_bright.SetAeroMode(FALSE); m_breath.SetAeroMode(FALSE); m_qual.SetAeroMode(FALSE);
	m_preset.SetAeroMode(FALSE); m_fx.SetAeroMode(FALSE);
	m_monitor.SetAeroMode(FALSE);
	m_meter.SetAeroMode(FALSE);
	SetWindowText(LL14(L"ボイスチェンジャー", L"Voice changer", L"Changeur de voix", L"Cambia voce", L"Cambiador de voz", L"보이스 체인저", L"变声器", L"مغير الصوت", L"Изменение голоса", L"Stimmenwandler", L"Modificador de voz", L"Stemvervormer", L"Zmiana głosu", L"Ses değiştirici"));
	m_micL.SetWindowText(LL14(L"入力", L"Input", L"Entrée", L"Ingresso", L"Entrada", L"입력", L"输入", L"الإدخال", L"Вход", L"Eingang", L"Entrada", L"Invoer", L"Wejście", L"Giriş"));
	m_outL.SetWindowText(LL14(L"出力", L"Output", L"Sortie", L"Uscita", L"Salida", L"출력", L"输出", L"الخرج", L"Выход", L"Ausgang", L"Saída", L"Uitvoer", L"Wyjście", L"Çıkış"));
	m_pitchL.SetWindowText(L"Pitch");
	m_formL.SetWindowText(L"Formant");
	m_gainL.SetWindowText(L"Gain");
	m_brightL.SetWindowText(LL14(L"明るさ", L"Bright", L"Brillance", L"Brillantezza", L"Brillo", L"밝기", L"亮度", L"سطوع", L"Яркость", L"Helligkeit", L"Brilho", L"Helderheid", L"Jasność", L"Parlaklık"));
	m_breathL.SetWindowText(LL14(L"息", L"Breath", L"Souffle", L"Fiato", L"Aire", L"숨", L"气息", L"نفس", L"Дыхание", L"Atem", L"Ar", L"Adem", L"Oddech", L"Nefes"));
	m_qualL.SetWindowText(LL14(L"品質", L"Quality", L"Qualité", L"Qualità", L"Calidad", L"품질", L"品质", L"جودة", L"Качество", L"Qualität", L"Qualidade", L"Kwaliteit", L"Jakość", L"Kalite"));
	m_presetL.SetWindowText(LL14(L"プリセット", L"Preset", L"Préréglage", L"Preset", L"Preajuste", L"프리셋", L"预设", L"إعداد مسبق", L"Пресет", L"Vorgabe", L"Predefinição", L"Voorinstelling", L"Ustawienie", L"Ön ayar"));
	m_styleL.SetWindowText(L"FX");
	m_monitor.SetWindowText(LL14(L"自分にもモニタ", L"Local monitor", L"Moniteur local", L"Monitor locale", L"Monitor local", L"로컬 모니터", L"本地监听", L"مراقبة محلية", L"Локальный монитор", L"Lokaler Monitor", L"Monitor local", L"Lokale monitor", L"Monitor lokalny", L"Yerel monitör"));
	m_meterL.SetWindowText(L"Out");
	m_start.SetWindowText(LL14(L"開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_status.SetWindowText(LL14(L"男→女／女→男の細かいプリセットから選んでください（JK・高低・年配・小中学生など）。", L"Pick a detailed Male↔Female preset (JK, high/low, mature, school ages).", L"Choisissez un preset Homme↔Femme détaillé.", L"Scegli un preset Uomo↔Donna dettagliato.", L"Elija un preset Hombre↔Mujer detallado.", L"남↔여 세부 프리셋을 고르세요.", L"请选择详细的男↔女预设。", L"اختر إعداد ذكر↔أنثى مفصّل.", L"Выберите подробный пресет М↔Ж.", L"Wählen Sie ein detailliertes Mann↔Frau-Preset.", L"Escolha um preset Homem↔Mulher detalhado.", L"Kies een gedetailleerd Man↔Vrouw-preset.", L"Wybierz szczegółowy preset M↔K.", L"Ayrıntılı Erkek↔Kadın önayarı seçin."));
	FillDevices();
	FillValues();
	// 名前付きプリセットは起動時に最新チューニングを再適用
	if (savedata.vc_preset >= 1 && savedata.vc_preset < VC_PRESET_N - 1)
		ApplyPreset(savedata.vc_preset);
	AudioDevApplyRescanButton(&m_micRefresh);
	AudioDevApplyRescanButton(&m_outRefresh);
	AudioDevRegisterNotifyHwnd(m_hWnd);
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_form, LL14(L"声道長の逆比（フォルマント）。女声は 110〜120% 程度。Pitch と同値にしない", L"Vocal-tract (formant). Female ~110–120%. Don't match Pitch %", L"Conduit (formant). Femme ~110–120%. Pas égal à Pitch", L"Tratto (formante). Donna ~110–120%. Non uguale a Pitch", L"Tracto (formante). Mujer ~110–120%. No igual a Pitch", L"성도(포먼트). 여~110–120%. Pitch와 동일 금지", L"声道（共振峰）。女声约110–120%。勿与Pitch同%", L"المسار (formant). أنثى ~110–120%", L"Тракт (форманта). Женский ~110–120%", L"Trakt (Formant). Weiblich ~110–120%. Nicht = Pitch", L"Trato (formante). Feminino ~110–120%. Não = Pitch", L"Tractus (formant). Vrouw ~110–120%. Niet = Pitch", L"Trakt (formant). Żeński ~110–120%. Nie = Pitch", L"Kanal (formant). Kadın ~110–120%. Pitch ile aynı olmasın"));
		m_tooltip.AddTool(&m_out, LL14(L"処理音を送る再生端末（仮想ケーブル推奨）", L"Playback endpoint for processed audio (virtual cable recommended)", L"Sortie du son traité (câble virtuel conseillé)", L"Uscita audio elaborato (cavo virtuale consigliato)", L"Salida procesada (se recomienda cable virtual)", L"처리음을 보낼 출력(가상 케이블 권장)", L"处理音频输出（建议虚拟线缆）", L"خرج الصوت المعالج (ينصح بكابل افتراضي)", L"Выход обработанного звука (рекомендуется виртуальный кабель)", L"Ausgang für verarbeiteten Ton (virtuelles Kabel empfohlen)", L"Saída do áudio processado (cabo virtual recomendado)", L"Uitvoer voor verwerkt geluid (virtuele kabel aanbevolen)", L"Wyjście dźwięku (zalecany kabel wirtualny)", L"İşlenmiş ses çıkışı (sanal kablo önerilir)"));
		m_tooltip.AddTool(&m_start, LL14(L"リアルタイム処理を開始または停止（実行中もパラメータ変更可）", L"Start/stop processing (params editable while running)", L"Démarrer/arrêter (réglages en cours OK)", L"Avvia/ferma (parametri modificabili)", L"Iniciar/detener (parámetros editables)", L"시작/중지(실행 중 파라미터 변경 가능)", L"开始/停止（运行中可改参数）", L"بدء/إيقاف (يمكن التعديل أثناء التشغيل)", L"Старт/стоп (параметры можно менять)", L"Start/Stop (Parameter während Lauf änderbar)", L"Iniciar/parar (parâmetros editáveis)", L"Start/stop (parameters tijdens run)", L"Start/stop (parametry w trakcie)", L"Başlat/durdur (çalışırken parametre değişir)"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 9000);
	}
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
	CCC_RefreshKids(m_hWnd);
	return TRUE;
}

void CVoiceChangerDlg::OnPreset() { ApplyPreset(m_preset.GetCurSel()); }
void CVoiceChangerDlg::OnChanged()
{
	CWnd* f = GetFocus();
	const BOOL param =
		f == (CWnd*)&m_pitch || f == (CWnd*)&m_form || f == (CWnd*)&m_gain ||
		f == (CWnd*)&m_bright || f == (CWnd*)&m_breath || f == (CWnd*)&m_fx ||
		f == (CWnd*)&m_qual;
	PersistUi(param);
}

void CVoiceChangerDlg::SetRunningUi(BOOL b)
{
	m_devLocked = b;
	m_mic.EnableWindow(!b);
	m_out.EnableWindow(!b);
	m_micRefresh.EnableWindow(!b);
	m_outRefresh.EnableWindow(!b);
	m_qual.EnableWindow(!b); // quality needs RB recreate
	m_start.SetWindowText(b
		? LL14(L"停止", L"Stop", L"Arrêter", L"Ferma", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stop", L"Stop", L"Durdur")
		: LL14(L"開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
}

BOOL CVoiceChangerDlg::StartAudio()
{
	PersistUi(FALSE);
	if (savedata.mpMirrorOut && savedata.mpMirrorDevice[0] && !_tcsicmp(savedata.vc_out_device, savedata.mpMirrorDevice)) {
		MessageBox(
			LL14(L"選択出力はメディアプレイヤーのミラー出力で使用中です。", L"The selected output is already used by media-player mirroring.", L"La sortie est utilisée par le miroir du lecteur.", L"L'uscita è usata dal mirror del lettore.", L"La salida está usada por el espejo del reproductor.", L"선택 출력은 미디어 플레이어 미러에서 사용 중입니다.", L"所选输出正被媒体播放器镜像使用。", L"الخرج مستخدم لنسخ مشغل الوسائط.", L"Выход занят зеркалированием проигрывателя.", L"Ausgang wird vom Player-Mirror verwendet.", L"A saída está em uso pelo espelho do leitor.", L"Uitvoer wordt gebruikt door speler-mirroring.", L"Wyjście jest używane przez mirror odtwarzacza.", L"Çıkış medya oynatıcı aynalamasında kullanılıyor."),
			LL14(L"出力競合", L"Output conflict", L"Conflit de sortie", L"Conflitto uscita", L"Conflicto de salida", L"출력 충돌", L"输出冲突", L"تعارض الخرج", L"Конфликт выхода", L"Ausgabekonflikt", L"Conflito de saída", L"Uitvoerconflict", L"Konflikt wyjścia", L"Çıkış çakışması"),
			MB_ICONWARNING);
		return FALSE;
	}
	SyncLiveParams();
	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_lastHr, S_OK);
	uintptr_t t = _beginthreadex(NULL, 0, AudioThread, this, 0, NULL);
	if (!t) return FALSE;
	m_thread = (HANDLE)t;
	SetRunningUi(TRUE);
	SetTimer(VC_TIMER, 50, NULL);
	m_status.SetWindowText(LL14(L"処理中… LPC（声帯＋声道）で変換中。Pitch と Formant は別々に調整。", L"Processing… LPC (glottis+tract). Tune Pitch and Formant separately.", L"Traitement… LPC (glotte+conduit). Pitch et Formant séparés.", L"Elaborazione… LPC (glottide+tratto). Pitch e Formant separati.", L"Procesando… LPC (glotis+tracto). Pitch y Formant por separado.", L"처리 중… LPC(성대+성도). Pitch/Formant 분리 조정.", L"处理中… LPC（声带+声道）。请分开调节 Pitch 与 Formant。", L"جارٍ المعالجة… LPC. اضبط Pitch وFormant منفصلين.", L"Обработка… LPC (щель+тракт). Pitch и Formant отдельно.", L"Verarbeitung… LPC (Glottis+Trakt). Pitch und Formant getrennt.", L"A processar… LPC (glote+trato). Pitch e Formant separados.", L"Verwerken… LPC (glottis+tractus). Pitch en Formant apart.", L"Przetwarzanie… LPC (głośnia+trakt). Pitch i Formant osobno.", L"İşleniyor… LPC (glotis+kanal). Pitch ve Formant ayrı ayarlayın."));
	return TRUE;
}

void CVoiceChangerDlg::StopAudio()
{
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 6000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	InterlockedExchange(&m_run, 0);
	if (GetSafeHwnd()) {
		KillTimer(VC_TIMER);
		SetRunningUi(FALSE);
	}
}

void CVoiceChangerDlg::OnStart() { if (m_thread) StopAudio(); else StartAudio(); }

void CVoiceChangerDlg::OnTimer(UINT_PTR id)
{
	if (id == VC_TIMER) {
		LONG p = InterlockedCompareExchange(&m_peak, 0, 0);
		InterlockedExchange(&m_peak, p * 85 / 100);
		m_meter.SetLevel(min(1000, (int)(sqrt((double)p / 1000.0) * 1100)));
		if (m_thread && WaitForSingleObject(m_thread, 0) == WAIT_OBJECT_0) {
			HRESULT h = (HRESULT)InterlockedCompareExchange(&m_lastHr, 0, 0);
			CloseHandle(m_thread); m_thread = NULL;
			KillTimer(VC_TIMER); SetRunningUi(FALSE);
			CString x;
			x.Format(LL14(L"出力デバイスを初期化できません (0x%08X)。", L"Cannot initialize output device (0x%08X).", L"Impossible d'initialiser la sortie (0x%08X).", L"Impossibile inizializzare l'uscita (0x%08X).", L"No se puede inicializar la salida (0x%08X).", L"출력 장치를 초기화할 수 없습니다 (0x%08X).", L"无法初始化输出设备 (0x%08X)。", L"تعذر تهيئة جهاز الخرج (0x%08X).", L"Не удалось инициализировать выход (0x%08X).", L"Ausgabegerät nicht initialisierbar (0x%08X).", L"Não foi possível iniciar a saída (0x%08X).", L"Uitvoerapparaat kan niet initialiseren (0x%08X).", L"Nie można zainicjować wyjścia (0x%08X).", L"Çıkış aygıtı başlatılamıyor (0x%08X)."), (UINT)h);
			m_status.SetWindowText(x);
		}
	}
	CCustomBlurDialogBase::OnTimer(id);
}

UINT __stdcall CVoiceChangerDlg::AudioThread(void* q)
{
	CVoiceChangerDlg* s = (CVoiceChangerDlg*)q;
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	IMMDeviceEnumerator* e = NULL;
	IMMDevice* mi = NULL, * od = NULL, * md = NULL;
	IAudioClient* cc = NULL, * oc = NULL, * mc = NULL;
	IAudioCaptureClient* cp = NULL;
	IAudioRenderClient* renOut = NULL, * mr = NULL;
	WAVEFORMATEX* f = NULL;
	UINT32 ob = 0, mb = 0;
	HRESULT h = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&e);
	if (SUCCEEDED(h) && savedata.vc_mic_device[0])
		h = e->GetDevice(savedata.vc_mic_device, &mi);
	if (FAILED(h) && e) h = e->GetDefaultAudioEndpoint(eCapture, eConsole, &mi);
	if (SUCCEEDED(h)) h = mi->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&cc);
	if (SUCCEEDED(h)) h = cc->GetMixFormat(&f);
	if (SUCCEEDED(h)) h = cc->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, 2000000, 0, f, NULL);
	if (SUCCEEDED(h)) h = cc->GetService(__uuidof(IAudioCaptureClient), (void**)&cp);
	if (SUCCEEDED(h) && savedata.vc_out_device[0])
		h = e->GetDevice(savedata.vc_out_device, &od);
	if (FAILED(h) && e) h = e->GetDefaultAudioEndpoint(eRender, eConsole, &od);
	if (SUCCEEDED(h)) h = VcOpenRender(od, &oc, &renOut, &ob);
	if (SUCCEEDED(h) && InterlockedCompareExchange(&s->m_monitorOn, 0, 0)) {
		HRESULT z = e->GetDefaultAudioEndpoint(eRender, eConsole, &md);
		if (SUCCEEDED(z)) {
			LPWSTR a = NULL, b = NULL;
			od->GetId(&a); md->GetId(&b);
			if (a && b && !_wcsicmp(a, b)) { md->Release(); md = NULL; }
			if (a) CoTaskMemFree(a); if (b) CoTaskMemFree(b);
		}
		if (md) h = VcOpenRender(md, &mc, &mr, &mb);
	}
	const int rate = (f && f->nSamplesPerSec >= 8000) ? (int)f->nSamplesPerSec : 48000;
	const int hq = InterlockedCompareExchange(&s->m_quality, 0, 0) ? 1 : 0;
	VcVocalTract tract;
	tract.Reset(rate, hq);
	if (SUCCEEDED(h)) {
		h = cc->Start();
		if (SUCCEEDED(h)) h = oc->Start();
		if (SUCCEEDED(h) && mc) h = mc->Start();
	}
	InterlockedExchange(&s->m_lastHr, h);
	if (FAILED(h)) goto done;
	InterlockedExchange(&s->m_run, 1);

	{
		std::vector<float> inMono, outMono;
		inMono.reserve(4096); outMono.reserve(4096);
		short pcm[4096 * 2];
		int lastQ = hq;
		double outPhase = 0.0;

		while (!InterlockedCompareExchange(&s->m_stop, 0, 0)) {
			UINT32 n = 0;
			if (FAILED(cp->GetNextPacketSize(&n))) break;
			if (!n) { Sleep(1); continue; }
			BYTE* d = NULL; DWORD fl = 0;
			if (FAILED(cp->GetBuffer(&d, &n, &fl, NULL, NULL))) break;

			const int pitchPct = (int)InterlockedCompareExchange(&s->m_pitchPct, 0, 0);
			const int formPct = (int)InterlockedCompareExchange(&s->m_formPct, 0, 0);
			const int gainPct = (int)InterlockedCompareExchange(&s->m_gainPct, 0, 0);
			const int brightPct = (int)InterlockedCompareExchange(&s->m_brightPct, 0, 0);
			const int breathPct = (int)InterlockedCompareExchange(&s->m_breathPct, 0, 0);
			const int style = (int)InterlockedCompareExchange(&s->m_fxStyle, 0, 0);
			const int quality = InterlockedCompareExchange(&s->m_quality, 0, 0) ? 1 : 0;
			if (quality != lastQ) {
				tract.Reset(rate, quality);
				lastQ = quality;
			}

			VcVocalParams vp;
			vp.pitch = max(0.5f, min(2.0f, pitchPct / 100.f));
			vp.formant = max(0.5f, min(2.0f, formPct / 100.f));
			vp.gain = max(0.f, min(2.0f, gainPct / 100.f));
			vp.bright = (float)brightPct;
			vp.breath = (float)breathPct;
			vp.style = style;
			vp.quality = quality;

			inMono.resize(n);
			outMono.resize(n);
			for (UINT32 i = 0; i < n; ++i) {
				float L, R;
				if (fl & AUDCLNT_BUFFERFLAGS_SILENT) L = R = 0;
				else VcRead(d + (SIZE_T)i * f->nBlockAlign, f, L, R);
				inMono[i] = (L + R) * 0.5f;
			}
			cp->ReleaseBuffer(n);

			const bool nearUnity = (fabsf(vp.pitch - 1.f) < 0.008f && fabsf(vp.formant - 1.f) < 0.008f
				&& style == 0 && breathPct == 0 && brightPct == 100 && fabsf(vp.gain - 1.f) < 0.008f);
			if (nearUnity) {
				outMono = inMono;
			} else {
				tract.Process(inMono.data(), outMono.data(), (int)n, vp);
			}

			for (UINT32 i = 0; i < n; ++i) {
				float x = outMono[i];
				LONG pk = (LONG)(fabsf(x) * 1000.f);
				LONG old = InterlockedCompareExchange(&s->m_peak, 0, 0);
				if (pk > old) InterlockedExchange(&s->m_peak, pk);
			}

			int made = 0;
			double phase = outPhase;
			while ((int)phase < (int)n && made < 4096) {
				int ix = (int)phase;
				float x = outMono[(UINT32)ix];
				pcm[made * 2] = pcm[made * 2 + 1] = (short)(x * 32767.f);
				made++;
				phase += (double)rate / 48000.0;
			}
			outPhase = phase - (double)n;
			if (made > 0) {
				VcWrite(oc, renOut, ob, pcm, made);
				VcWrite(mc, mr, mb, pcm, made);
			}
		}
	}

done:
	if (cc) cc->Stop();
	if (oc) oc->Stop();
	if (mc) mc->Stop();
	if (mr) mr->Release();
	if (renOut) renOut->Release();
	if (cp) cp->Release();
	if (mc) mc->Release();
	if (oc) oc->Release();
	if (cc) cc->Release();
	if (md) md->Release();
	if (od) od->Release();
	if (mi) mi->Release();
	if (e) e->Release();
	if (f) CoTaskMemFree(f);
	InterlockedExchange(&s->m_run, 0);
	CoUninitialize();
	return 0;
}

void CVoiceChangerDlg::OnContextMenu(CWnd*, CPoint p)
{
	CCustomPopupMenu m;
	const int cur = m_preset.GetCurSel();
	{
		CCustomPopupMenu* basic = m.AddSubMenu(
			LL14(L"基本", L"Basic", L"Base", L"Base", L"Basico", L"기본", L"基本", L"أساسي",
				L"Базовые", L"Basis", L"Basico", L"Basis", L"Podstawowe", L"Temel"),
			LL14(L"標準・男女標準・年齢・特殊効果・カスタム", L"Normal, gender std, age, FX, and custom",
				L"Normal, genre std, age, FX et perso", L"Normale, genere std, eta, FX e personalizzato",
				L"Normal, genero std, edad, FX y personalizado", L"표준·성별 표준·나이·특수효과·사용자",
				L"标准、性别标准、年龄、特效与自定义", L"عادي وجنس قياسي وعمر وتأثيرات ومخصص",
				L"Обычный, пол std, возраст, FX и свой", L"Normal, Geschlecht std, Alter, FX und benutzerdefiniert",
				L"Normal, genero padrao, idade, FX e personalizado", L"Normaal, geslacht std, leeftijd, FX en aangepast",
				L"Normalny, plec std, wiek, FX i wlasny", L"Normal, cinsiyet std, yas, FX ve ozel"));
		if (basic) {
			static const int kBasic[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 22 };
			for (int bi = 0; bi < (int)(sizeof(kBasic) / sizeof(kBasic[0])); ++bi) {
				const int i = kBasic[bi];
				basic->AddCheck(100 + i, VcPresetName(i), cur == i);
			}
		}
	}
	{
		CCustomPopupMenu* m2f = m.AddSubMenu(
			LL14(L"男→女バリエ", L"Male→Female variants", L"Variantes H→F", L"Varianti U→D", L"Variantes H→M",
				L"남→여 변형", L"男→女变体", L"تنويعات ذكر→أنثى", L"Варианты М→Ж", L"M→F Varianten",
				L"Variantes H→M", L"M→V varianten", L"Warianty M→K", L"E→K varyantlari"),
			LL14(L"男声から女声へのバリエーション", L"Male-to-female voice variants",
				L"Variantes homme vers femme", L"Varianti da uomo a donna",
				L"Variantes de hombre a mujer", L"남성의 여성 변형",
				L"男声转女声的变体", L"تنويعات من ذكر إلى أنثى",
				L"Варианты мужского в женский", L"Varianten von Mann zu Frau",
				L"Variantes de homem para mulher", L"Varianten van man naar vrouw",
				L"Warianty z meskiego na zenski", L"Erkekten kadina varyantlar"));
		if (m2f) {
			for (int i = 9; i <= 15; ++i)
				m2f->AddCheck(100 + i, VcPresetName(i), cur == i);
		}
	}
	{
		CCustomPopupMenu* f2m = m.AddSubMenu(
			LL14(L"女→男バリエ", L"Female→Male variants", L"Variantes F→H", L"Varianti D→U", L"Variantes M→H",
				L"여→남 변형", L"女→男变体", L"تنويعات أنثى→ذكر", L"Варианты Ж→М", L"F→M Varianten",
				L"Variantes M→H", L"V→M varianten", L"Warianty K→M", L"K→E varyantlari"),
			LL14(L"女声から男声へのバリエーション", L"Female-to-male voice variants",
				L"Variantes femme vers homme", L"Varianti da donna a uomo",
				L"Variantes de mujer a hombre", L"여성의 남성 변형",
				L"女声转男声的变体", L"تنويعات من أنثى إلى ذكر",
				L"Варианты женского в мужской", L"Varianten von Frau zu Mann",
				L"Variantes de mulher para homem", L"Varianten van vrouw naar man",
				L"Warianty z zenskiego na meski", L"Kadindan erkege varyantlar"));
		if (f2m) {
			for (int i = 16; i <= 21; ++i)
				f2m->AddCheck(100 + i, VcPresetName(i), cur == i);
		}
	}
	if (p.x < 0) { CRect r; GetWindowRect(r); p = r.TopLeft() + CPoint(40, 40); }
	UINT c = m.Track(p, this);
	if (c >= 100 && c < 100 + (UINT)VC_PRESET_N) ApplyPreset((int)c - 100);
}

void CVoiceChangerDlg::ShowHelpSheet()
{
	if (vcHelp && vcHelp->GetSafeHwnd()) { vcHelp->SetForegroundWindow(); return; }
	vcHelp = new CVcHelp(this);
	if (!vcHelp->Create(IDD_VC_HELP, this)) { delete vcHelp; vcHelp = NULL; return; }
	CCC_PresentOwnedHelp(this, vcHelp);
}

void CVoiceChangerDlg::OnDestroy()
{
	AudioDevUnregisterNotifyHwnd(m_hWnd);
	PersistUi(FALSE);
	StopAudio();
	CCustomBlurDialogBase::OnDestroy();
}
void CVoiceChangerDlg::OnMicDevRefresh() { AudioDevRebuildAll(); FillDevices(); }
LRESULT CVoiceChangerDlg::OnAudioDevChanged(WPARAM, LPARAM) { FillDevices(); return 0; }
void CVoiceChangerDlg::OnHelp() { ShowHelpSheet(); }
void CVoiceChangerDlg::OnCloseBtn() { DestroyWindow(); }
void CVoiceChangerDlg::OnOK() {}
void CVoiceChangerDlg::OnCancel() { DestroyWindow(); }
void CVoiceChangerDlg::OnSize(UINT t, int x, int y)
{
	CCustomBlurDialogBase::OnSize(t, x, y);
	if (GetSafeHwnd()) { CCC_CaptionLayout(m_hWnd); LayoutHelpBtn(); }
}

void OpenVoiceChangerModeless(CWnd* p)
{
	if (g_vc && g_vc->GetSafeHwnd()) { g_vc->SetForegroundWindow(); return; }
	g_vc = new CVoiceChangerDlg(p);
	if (!g_vc->Create(IDD_VOICECHANGER, p)) { delete g_vc; g_vc = NULL; return; }
	g_vc->ShowWindow(SW_SHOW);
}
void CloseVoiceChangerIfOpen()
{
	if (g_vc && g_vc->GetSafeHwnd()) g_vc->DestroyWindow();
}
