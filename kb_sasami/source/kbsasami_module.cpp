#include <windows.h>
#include "kbsasami_module.h"
#include "kbsasami_decoder.h"
#include "kbsasami_lang.h"
#include "kpi.h"

HINSTANCE g_hKpi = NULL;

#ifdef _DEBUG
static const DWORD kPluginVersion = 0x7FFFFFFF;
#define KBSASAMI_VERSION_STR L"0x7FFFFFFF(Debug)"
#else
static const DWORD kPluginVersion = 1;
#define KBSASAMI_VERSION_STR L"1.25"
#endif

static wchar_t g_desc[384];

static void FillAbout(KPI_DECODER_MODULEINFO* info)
{
	const wchar_t* fmt = KbsPick14(
		L"SASAMI / MIDI デコーダ v%s（FPY / MPY / MID / RCP / EUP / SNG / ZMS）",
		L"SASAMI and MIDI Decoder v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"Décodeur SASAMI et MIDI v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"Decoder SASAMI e MIDI v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"Decodificador SASAMI y MIDI v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"SASAMI / MIDI 디코더 v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"SASAMI / MIDI 解码器 v%s（FPY / MPY / MID / RCP / EUP / SNG / ZMS）",
		L"مفكك SASAMI و MIDI الإصدار %s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"Декодер SASAMI и MIDI v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"SASAMI- und MIDI-Decoder v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"Decodificador SASAMI e MIDI v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"SASAMI- en MIDI-decoder v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"Dekoder SASAMI i MIDI v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)",
		L"SASAMI ve MIDI çözücü v%s (FPY / MPY / MID / RCP / EUP / SNG / ZMS)");
	_snwprintf_s(g_desc, _TRUNCATE, fmt, KBSASAMI_VERSION_STR);
	info->cszDescription = g_desc;
	info->cszCopyright = KbsPick14(
		L"kbsasami.kpi  SASAMI（「ささみ☆ミ」）プレーヤ\n"
		L"FM: ymfm YM2608（OPNA / OPN=SCHオフ）+ ソフト BEEP\n"
		L"MIDI の PCM: SMF へ変換して fmmidi（yuno）\n"
		L"RCP/EUP/SNG/ZMS/SMF: もともと SMF でなければプラグイン内で変換\n"
		L"raira=1: vst の 0/1 を入れ替え、FM ドラムを大きくする。raira=0: 本来のドラム、MIDI 音量は半分\n"
		L"raira は変換を省かない\n"
		L"コマンドは SASAMI / SASAMI11 / SASAMIM から移植",
		L"kbsasami.kpi SASAMI player\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH-off) + soft BEEP\n"
		L"MIDI PCM path: fmmidi (yuno) via SMF conversion\n"
		L"RCP/EUP/SNG/ZMS/SMF: convert inside the plugin if the file is not already SMF\n"
		L"raira=1: vst 0/1 swapped, louder FM drums. raira=0: stock drums, MIDI vol/2\n"
		L"raira does not skip conversion\n"
		L"Commands ported from SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  lecteur SASAMI\n"
		L"FM : ymfm YM2608 (OPNA / OPN=SCH coupé) + BEEP logiciel\n"
		L"PCM MIDI : fmmidi (yuno) après conversion SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF : conversion dans le plugin si ce n'est pas déjà du SMF\n"
		L"raira=1 : vst 0/1 inversés, batteries FM plus fortes. raira=0 : batteries d'origine, volume MIDI /2\n"
		L"raira ne saute pas la conversion\n"
		L"Commandes portées depuis SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  lettore SASAMI\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH spento) + BEEP software\n"
		L"PCM MIDI: fmmidi (yuno) dopo conversione SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: conversione nel plugin se non è già SMF\n"
		L"raira=1: vst 0/1 scambiati, batteria FM più forte. raira=0: batteria originale, volume MIDI /2\n"
		L"raira non salta la conversione\n"
		L"Comandi portati da SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  reproductor SASAMI\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH apagado) + BEEP por software\n"
		L"PCM MIDI: fmmidi (yuno) tras convertir a SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: se convierte dentro del plugin si aún no es SMF\n"
		L"raira=1: se intercambian vst 0/1 y la batería FM suena más fuerte. raira=0: batería original, volumen MIDI /2\n"
		L"raira no omite la conversión\n"
		L"Comandos adaptados de SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  SASAMI 플레이어\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH 끔) + 소프트 BEEP\n"
		L"MIDI PCM: SMF로 변환한 뒤 fmmidi (yuno)\n"
		L"RCP/EUP/SNG/ZMS/SMF: 이미 SMF가 아니면 플러그인 안에서 변환\n"
		L"raira=1: vst 0/1을 바꾸고 FM 드럼을 크게. raira=0: 원래 드럼, MIDI 음량 /2\n"
		L"raira는 변환을 건너뛰지 않음\n"
		L"명령은 SASAMI / SASAMI11 / SASAMIM 에서 이식",
		L"kbsasami.kpi  SASAMI 播放器\n"
		L"FM: ymfm YM2608（OPNA / OPN=SCH 关）+ 软 BEEP\n"
		L"MIDI PCM: 先转成 SMF，再经 fmmidi（yuno）\n"
		L"RCP/EUP/SNG/ZMS/SMF: 若还不是 SMF，就在插件内转换\n"
		L"raira=1: 对调 vst 的 0/1，FM 鼓更响。raira=0: 原装鼓，MIDI 音量减半\n"
		L"raira 不会跳过转换\n"
		L"命令移植自 SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  مشغّل SASAMI\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH مطفأ) + BEEP برمجي\n"
		L"PCM لMIDI: fmmidi (yuno) بعد التحويل إلى SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: يُحوَّل داخل الإضافة إن لم يكن SMF\n"
		L"raira=1: تبديل vst 0/1 وطبول FM أعلى. raira=0: الطبول الأصلية، مستوى MIDI /2\n"
		L"raira لا يتخطى التحويل\n"
		L"الأوامر منقولة من SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  проигрыватель SASAMI\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH выкл.) + программный BEEP\n"
		L"PCM MIDI: fmmidi (yuno) после преобразования в SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: если это ещё не SMF, преобразование внутри плагина\n"
		L"raira=1: vst 0/1 меняются местами, ударные FM громче. raira=0: штатные ударные, громкость MIDI /2\n"
		L"raira не пропускает преобразование\n"
		L"Команды перенесены из SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  SASAMI-Player\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH aus) + Software-BEEP\n"
		L"MIDI-PCM: fmmidi (yuno) nach SMF-Umwandlung\n"
		L"RCP/EUP/SNG/ZMS/SMF: im Plugin umwandeln, wenn es noch kein SMF ist\n"
		L"raira=1: vst 0/1 vertauscht, FM-Drums lauter. raira=0: Original-Drums, MIDI-Lautstärke /2\n"
		L"raira überspringt die Umwandlung nicht\n"
		L"Befehle übertragen aus SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  leitor SASAMI\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH desligado) + BEEP em software\n"
		L"PCM MIDI: fmmidi (yuno) após conversão para SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: converte no plugin se ainda não for SMF\n"
		L"raira=1: troca vst 0/1, bateria FM mais alta. raira=0: bateria original, volume MIDI /2\n"
		L"raira não pula a conversão\n"
		L"Comandos portados de SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  SASAMI-speler\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH uit) + software-BEEP\n"
		L"MIDI-PCM: fmmidi (yuno) na omzetting naar SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: in de plugin omzetten als het nog geen SMF is\n"
		L"raira=1: vst 0/1 verwisseld, hardere FM-drums. raira=0: originele drums, MIDI-volume /2\n"
		L"raira slaat de omzetting niet over\n"
		L"Opdrachten overgenomen uit SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  odtwarzacz SASAMI\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH wyłączone) + programowy BEEP\n"
		L"PCM MIDI: fmmidi (yuno) po konwersji do SMF\n"
		L"RCP/EUP/SNG/ZMS/SMF: konwersja w pluginie, jeśli to jeszcze nie SMF\n"
		L"raira=1: zamiana vst 0/1, głośniejsze perkusja FM. raira=0: oryginalna perkusja, głośność MIDI /2\n"
		L"raira nie pomija konwersji\n"
		L"Polecenia przeniesione z SASAMI / SASAMI11 / SASAMIM",
		L"kbsasami.kpi  SASAMI oynatıcı\n"
		L"FM: ymfm YM2608 (OPNA / OPN=SCH kapalı) + yazılım BEEP\n"
		L"MIDI PCM: SMF dönüşümünden sonra fmmidi (yuno)\n"
		L"RCP/EUP/SNG/ZMS/SMF: henüz SMF değilse eklenti içinde dönüştürülür\n"
		L"raira=1: vst 0/1 yer değiştirir, FM davullar daha yüksek. raira=0: özgün davullar, MIDI sesi /2\n"
		L"raira dönüşümü atlamaz\n"
		L"Komutlar SASAMI / SASAMI11 / SASAMIM kaynaklıdır");
}

// {A7C3E91F-4B2D-4E6A-9C18-8F5D2A1B7E03}
static const GUID kGuid =
{ 0xa7c3e91f, 0x4b2d, 0x4e6a, { 0x9c, 0x18, 0x8f, 0x5d, 0x2a, 0x1b, 0x7e, 0x03 } };

static const wchar_t kExts[] = L".fpy/.fpy2/.mpy/.mpw2/.mpsmv/.mid/.midi/.kar/.rmi/.smf/.rcp/.r36/.g36/.g18/.mcp/.mtd/.mff/.seq/.eup/.sng/.zms";

static const wchar_t SEC_KBSASAMI[] = L"kbsasami";
static const wchar_t KEY_VST[] = L"vst";
static const wchar_t KEY_RAIRA[] = L"raira";
static const wchar_t KEY_FMMIDIMONITOR[] = L"fmmidimonitor";
static const wchar_t KEY_MIDIMODE[] = L"midimode";
static const wchar_t KEY_FMMODE[] = L"fmmode";

KbSasamiDecoderModule::KbSasamiDecoderModule(IKpiConfig* pConfig)
	: m_ModuleInfo{
		sizeof(KPI_DECODER_MODULEINFO),
		KPI_DECODER_MODULE_VERSION,
		kPluginVersion,
		KPI_MULTINST_INFINITE,
		kGuid,
		L"",
		L"",
		kExts,
		L"",
		NULL,
		NULL,
		0,
		1,
		{ 0, 0, 0, 0 }
	}
	, m_pConfig(pConfig)
{
	FillAbout(&m_ModuleInfo);
	if (m_pConfig) m_pConfig->AddRef();
}

KbSasamiDecoderModule::~KbSasamiDecoderModule()
{
	if (m_pConfig) {
		m_pConfig->Release();
		m_pConfig = NULL;
	}
}

void WINAPI KbSasamiDecoderModule::GetModuleInfo(const KPI_DECODER_MODULEINFO** ppInfo)
{
	FillAbout(&m_ModuleInfo);
	*ppInfo = &m_ModuleInfo;
}

DWORD WINAPI KbSasamiDecoderModule::Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder, IKpiDecoder** ppDecoder)
{
	KbSasamiDecoder* dec = new KbSasamiDecoder(m_pConfig);
	DWORD n = dec->Open(cpRequest, pFile, pFolder);
	if (n == 0) {
		*ppDecoder = NULL;
		delete dec;
		return 0;
	}
	*ppDecoder = dec;
	return n;
}

BOOL WINAPI KbSasamiDecoderModule::EnumConfig(IKpiConfigEnumerator* pEnumerator)
{
	if (!pEnumerator) return FALSE;
	const KPI_CFG_SECTION sec[] = {
		{ SEC_KBSASAMI,
			KbsPick14(
				L"SASAMI の設定", L"SASAMI options", L"Options SASAMI", L"Opzioni SASAMI",
				L"Opciones de SASAMI", L"SASAMI 설정", L"SASAMI 设置", L"إعدادات SASAMI",
				L"Параметры SASAMI", L"SASAMI-Optionen", L"Opções SASAMI", L"SASAMI-opties",
				L"Opcje SASAMI", L"SASAMI seçenekleri"),
			KbsPick14(
				L"kbsasami.kpi の設定です。\r\n"
				L"KbMedia Player 本来では kbsasami.vst=0 が FM MIDI（fmmidi）です。\r\n"
				L"このアプリは kbsasami.raira=1 のとき、vst の 0 と 1 を内部で入れ替えます。",
				L"kbsasami.kpi options.\r\n"
				L"Original KbMedia Player: kbsasami.vst=0 means FM MIDI (fmmidi).\r\n"
				L"When kbsasami.raira=1, this app swaps vst 0/1 internally.",
				L"Options de kbsasami.kpi.\r\n"
				L"KbMedia Player d'origine : kbsasami.vst=0 signifie FM MIDI (fmmidi).\r\n"
				L"Avec kbsasami.raira=1, cette application inverse vst 0/1 en interne.",
				L"Opzioni di kbsasami.kpi.\r\n"
				L"KbMedia Player originale: kbsasami.vst=0 significa FM MIDI (fmmidi).\r\n"
				L"Con kbsasami.raira=1 questa app scambia vst 0/1 internamente.",
				L"Opciones de kbsasami.kpi.\r\n"
				L"KbMedia Player original: kbsasami.vst=0 significa FM MIDI (fmmidi).\r\n"
				L"Con kbsasami.raira=1 esta aplicación intercambia vst 0/1 internamente.",
				L"kbsasami.kpi 설정입니다.\r\n"
				L"원래 KbMedia Player에서는 kbsasami.vst=0 이 FM MIDI(fmmidi)입니다.\r\n"
				L"이 앱은 kbsasami.raira=1 일 때 vst 0과 1을 내부에서 바꿉니다.",
				L"kbsasami.kpi 的设置。\r\n"
				L"原来的 KbMedia Player 里，kbsasami.vst=0 表示 FM MIDI（fmmidi）。\r\n"
				L"本程序在 kbsasami.raira=1 时，会在内部对调 vst 的 0 和 1。",
				L"إعدادات kbsasami.kpi.\r\n"
				L"في KbMedia Player الأصلي: kbsasami.vst=0 يعني FM MIDI (fmmidi).\r\n"
				L"عندما يكون kbsasami.raira=1 يبدّل هذا البرنامج vst 0/1 داخليًا.",
				L"Параметры kbsasami.kpi.\r\n"
				L"В исходном KbMedia Player kbsasami.vst=0 означает FM MIDI (fmmidi).\r\n"
				L"При kbsasami.raira=1 это приложение меняет местами vst 0 и 1.",
				L"Optionen von kbsasami.kpi.\r\n"
				L"Ursprünglicher KbMedia Player: kbsasami.vst=0 bedeutet FM-MIDI (fmmidi).\r\n"
				L"Bei kbsasami.raira=1 vertauscht diese App vst 0/1 intern.",
				L"Opções do kbsasami.kpi.\r\n"
				L"No KbMedia Player original, kbsasami.vst=0 significa FM MIDI (fmmidi).\r\n"
				L"Com kbsasami.raira=1 este aplicativo troca vst 0/1 internamente.",
				L"Opties van kbsasami.kpi.\r\n"
				L"Oorspronkelijke KbMedia Player: kbsasami.vst=0 betekent FM-MIDI (fmmidi).\r\n"
				L"Bij kbsasami.raira=1 verwisselt deze app vst 0/1 intern.",
				L"Opcje kbsasami.kpi.\r\n"
				L"W oryginalnym KbMedia Player kbsasami.vst=0 oznacza FM MIDI (fmmidi).\r\n"
				L"Gdy kbsasami.raira=1, ta aplikacja zamienia wewnętrznie vst 0 i 1.",
				L"kbsasami.kpi seçenekleri.\r\n"
				L"Özgün KbMedia Player: kbsasami.vst=0 FM MIDI (fmmidi) demektir.\r\n"
				L"kbsasami.raira=1 iken bu uygulama vst 0/1 değerlerini içeride yer değiştirir.") },
		{ NULL, NULL, NULL }
	};
	const KPI_CFG_KEY key[] = {
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_VST, L"kbsasami.vst",
			L"0", NULL, NULL, NULL, NULL,
			KbsPick14(
				L"false(0): FM MIDI（プラグイン内の fmmidi / programs.txt）\r\n"
				L"true(1): VST。raira=1 のときはこのアプリに任せる。\r\n"
				L"raira=0 では kbsasami.vstfullpath_gs / _xg を kbsasami_host32/64 で鳴らす。\r\n"
				L"\r\n"
				L"kbsasami.raira=1 のとき、0 と 1 の意味は逆になる。\r\n"
				L"本来のプレーヤの初期値は false（FM MIDI）。",
				L"false(0): FM MIDI mode (fmmidi / programs.txt inside plugin)\r\n"
				L"true(1): VST. raira=1 leaves it to this app.\r\n"
				L"raira=0 plays kbsasami.vstfullpath_gs / _xg via kbsasami_host32/64.\r\n"
				L"\r\n"
				L"With kbsasami.raira=1, 0 and 1 meanings are swapped.\r\n"
				L"Default for original player is false (FM MIDI).",
				L"false(0) : FM MIDI (fmmidi / programs.txt dans le plugin)\r\n"
				L"true(1) : VST. Avec raira=1, c'est cette application qui décide.\r\n"
				L"raira=0 joue kbsasami.vstfullpath_gs / _xg via kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Avec kbsasami.raira=1, les sens de 0 et 1 sont inversés.\r\n"
				L"Le lecteur d'origine démarre à false (FM MIDI).",
				L"false(0): FM MIDI (fmmidi / programs.txt nel plugin)\r\n"
				L"true(1): VST. Con raira=1 decide questa app.\r\n"
				L"raira=0 suona kbsasami.vstfullpath_gs / _xg tramite kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Con kbsasami.raira=1 i significati di 0 e 1 si invertono.\r\n"
				L"Il lettore originale parte da false (FM MIDI).",
				L"false(0): FM MIDI (fmmidi / programs.txt dentro del plugin)\r\n"
				L"true(1): VST. Con raira=1 lo decide esta aplicación.\r\n"
				L"raira=0 reproduce kbsasami.vstfullpath_gs / _xg con kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Con kbsasami.raira=1, el sentido de 0 y 1 se invierte.\r\n"
				L"El reproductor original empieza en false (FM MIDI).",
				L"false(0): FM MIDI (플러그인 안의 fmmidi / programs.txt)\r\n"
				L"true(1): VST. raira=1 이면 이 앱이 맡습니다.\r\n"
				L"raira=0 은 kbsasami.vstfullpath_gs / _xg 를 kbsasami_host32/64 로 재생합니다.\r\n"
				L"\r\n"
				L"kbsasami.raira=1 이면 0과 1의 의미가 바뀝니다.\r\n"
				L"원래 플레이어의 초기값은 false(FM MIDI)입니다.",
				L"false(0): FM MIDI（插件内的 fmmidi / programs.txt）\r\n"
				L"true(1): VST。raira=1 时交给本程序。\r\n"
				L"raira=0 时用 kbsasami_host32/64 播放 kbsasami.vstfullpath_gs / _xg。\r\n"
				L"\r\n"
				L"kbsasami.raira=1 时，0 和 1 的含义对调。\r\n"
				L"原来的播放器默认是 false（FM MIDI）。",
				L"false(0): وضع FM MIDI (fmmidi / programs.txt داخل الإضافة)\r\n"
				L"true(1): VST. مع raira=1 يقرره هذا البرنامج.\r\n"
				L"raira=0 يشغّل kbsasami.vstfullpath_gs / _xg عبر kbsasami_host32/64.\r\n"
				L"\r\n"
				L"مع kbsasami.raira=1 ينعكس معنى 0 و 1.\r\n"
				L"الافتراضي في المشغّل الأصلي هو false (FM MIDI).",
				L"false(0): режим FM MIDI (fmmidi / programs.txt внутри плагина)\r\n"
				L"true(1): VST. При raira=1 решает это приложение.\r\n"
				L"raira=0 играет kbsasami.vstfullpath_gs / _xg через kbsasami_host32/64.\r\n"
				L"\r\n"
				L"При kbsasami.raira=1 смысл 0 и 1 меняется местами.\r\n"
				L"У исходного плеера по умолчанию false (FM MIDI).",
				L"false(0): FM-MIDI (fmmidi / programs.txt im Plugin)\r\n"
				L"true(1): VST. Bei raira=1 entscheidet diese App.\r\n"
				L"raira=0 spielt kbsasami.vstfullpath_gs / _xg über kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Bei kbsasami.raira=1 sind die Bedeutungen von 0 und 1 vertauscht.\r\n"
				L"Der ursprüngliche Player startet mit false (FM-MIDI).",
				L"false(0): FM MIDI (fmmidi / programs.txt dentro do plugin)\r\n"
				L"true(1): VST. Com raira=1 este aplicativo decide.\r\n"
				L"raira=0 toca kbsasami.vstfullpath_gs / _xg via kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Com kbsasami.raira=1, o sentido de 0 e 1 se inverte.\r\n"
				L"O leitor original começa em false (FM MIDI).",
				L"false(0): FM-MIDI (fmmidi / programs.txt in de plugin)\r\n"
				L"true(1): VST. Bij raira=1 beslist deze app.\r\n"
				L"raira=0 speelt kbsasami.vstfullpath_gs / _xg via kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Bij kbsasami.raira=1 zijn de betekenissen van 0 en 1 verwisseld.\r\n"
				L"De oorspronkelijke speler start op false (FM-MIDI).",
				L"false(0): FM MIDI (fmmidi / programs.txt w pluginie)\r\n"
				L"true(1): VST. Przy raira=1 decyduje ta aplikacja.\r\n"
				L"raira=0 odtwarza kbsasami.vstfullpath_gs / _xg przez kbsasami_host32/64.\r\n"
				L"\r\n"
				L"Przy kbsasami.raira=1 znaczenia 0 i 1 są zamienione.\r\n"
				L"Oryginalny odtwarzacz startuje od false (FM MIDI).",
				L"false(0): FM MIDI (eklentideki fmmidi / programs.txt)\r\n"
				L"true(1): VST. raira=1 iken bu uygulama karar verir.\r\n"
				L"raira=0, kbsasami.vstfullpath_gs / _xg yolunu kbsasami_host32/64 ile çalar.\r\n"
				L"\r\n"
				L"kbsasami.raira=1 iken 0 ve 1'in anlamı yer değiştirir.\r\n"
				L"Özgün oynatıcının varsayılanı false (FM MIDI).") },
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_RAIRA, L"kbsasami.raira",
			L"0", NULL, NULL, NULL, NULL,
			KbsPick14(
				L"false(0): KbMedia Player 本来（vst はそのまま、標準ドラム、MIDI 音量は半分）\r\n"
				L"true(1): このアプリ。vst の 0/1 を内部で入れ替え、FM ドラムを大きくする。\r\n"
				L"このアプリは常に raira=1 を書く。",
				L"false(0): original KbMedia Player (interpret vst as-is, stock drums, MIDI vol/2)\r\n"
				L"true(1): this app. Swaps vst 0/1 internally, louder FM drums.\r\n"
				L"This app always writes raira=1.",
				L"false(0) : KbMedia Player d'origine (vst tel quel, batteries d'origine, volume MIDI /2)\r\n"
				L"true(1) : cette application. Inverse vst 0/1 en interne, batteries FM plus fortes.\r\n"
				L"Cette application écrit toujours raira=1.",
				L"false(0): KbMedia Player originale (vst così com'è, batteria originale, volume MIDI /2)\r\n"
				L"true(1): questa app. Scambia vst 0/1 internamente, batteria FM più forte.\r\n"
				L"Questa app scrive sempre raira=1.",
				L"false(0): KbMedia Player original (vst tal cual, batería original, volumen MIDI /2)\r\n"
				L"true(1): esta aplicación. Intercambia vst 0/1 internamente y sube la batería FM.\r\n"
				L"Esta aplicación siempre escribe raira=1.",
				L"false(0): 원래 KbMedia Player (vst 그대로, 기본 드럼, MIDI 음량 /2)\r\n"
				L"true(1): 이 앱. vst 0/1을 내부에서 바꾸고 FM 드럼을 크게 합니다.\r\n"
				L"이 앱은 항상 raira=1 을 기록합니다.",
				L"false(0): 原来的 KbMedia Player（vst 原样、原装鼓、MIDI 音量减半）\r\n"
				L"true(1): 本程序。内部对调 vst 0/1，FM 鼓更响。\r\n"
				L"本程序始终写入 raira=1。",
				L"false(0): KbMedia Player الأصلي (vst كما هو، طبول أصلية، مستوى MIDI /2)\r\n"
				L"true(1): هذا البرنامج. يبدّل vst 0/1 داخليًا ويجعل طبول FM أعلى.\r\n"
				L"هذا البرنامج يكتب دائمًا raira=1.",
				L"false(0): исходный KbMedia Player (vst как есть, штатные ударные, громкость MIDI /2)\r\n"
				L"true(1): это приложение. Меняет vst 0/1 местами, ударные FM громче.\r\n"
				L"Это приложение всегда записывает raira=1.",
				L"false(0): ursprünglicher KbMedia Player (vst unverändert, Original-Drums, MIDI-Lautstärke /2)\r\n"
				L"true(1): diese App. Vertauscht vst 0/1 intern, FM-Drums lauter.\r\n"
				L"Diese App schreibt immer raira=1.",
				L"false(0): KbMedia Player original (vst como está, bateria original, volume MIDI /2)\r\n"
				L"true(1): este aplicativo. Troca vst 0/1 internamente e aumenta a bateria FM.\r\n"
				L"Este aplicativo sempre grava raira=1.",
				L"false(0): oorspronkelijke KbMedia Player (vst ongewijzigd, originele drums, MIDI-volume /2)\r\n"
				L"true(1): deze app. Verwisselt vst 0/1 intern, hardere FM-drums.\r\n"
				L"Deze app schrijft altijd raira=1.",
				L"false(0): oryginalny KbMedia Player (vst bez zmian, oryginalna perkusja, głośność MIDI /2)\r\n"
				L"true(1): ta aplikacja. Zamienia vst 0/1 wewnętrznie, głośniejsza perkusja FM.\r\n"
				L"Ta aplikacja zawsze zapisuje raira=1.",
				L"false(0): özgün KbMedia Player (vst olduğu gibi, özgün davullar, MIDI sesi /2)\r\n"
				L"true(1): bu uygulama. vst 0/1 yer değiştirir, FM davullar daha yüksek.\r\n"
				L"Bu uygulama her zaman raira=1 yazar.") },
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_FMMIDIMONITOR, L"kbsasami.fmmidimonitor",
			L"1", NULL, NULL, NULL, NULL,
			KbsPick14(
				L"false(0): kbsasami_host の FM/MIDI モニタを開かない。\r\n"
				L"true(1): kbsasami_host がそのモニタを出す（初期値）。\r\n"
				L"kbsasami.raira=0（KbMedia Player 本来）のときだけ読む。\r\n"
				L"raira=1 のこのアプリは自分のモニタを使い、この項目は読まない。\r\n"
				L"余分な窓が要らなければ、本来のプレーヤ側で切る。",
				L"false(0): do not open the FM/MIDI monitor in kbsasami_host.\r\n"
				L"true(1): kbsasami_host shows that monitor (default).\r\n"
				L"Read only when kbsasami.raira=0 (original KbMedia Player).\r\n"
				L"When raira=1 this app uses its own monitor and does not read this key.\r\n"
				L"Turn it off in the original player if you do not want the extra window.",
				L"false(0) : ne pas ouvrir le moniteur FM/MIDI de kbsasami_host.\r\n"
				L"true(1) : kbsasami_host affiche ce moniteur (défaut).\r\n"
				L"Lu seulement si kbsasami.raira=0 (KbMedia Player d'origine).\r\n"
				L"Avec raira=1 cette application utilise son propre moniteur et ignore cette clé.\r\n"
				L"Coupez-la dans le lecteur d'origine si la fenêtre en trop gêne.",
				L"false(0): non aprire il monitor FM/MIDI di kbsasami_host.\r\n"
				L"true(1): kbsasami_host mostra quel monitor (predefinito).\r\n"
				L"Letto solo con kbsasami.raira=0 (KbMedia Player originale).\r\n"
				L"Con raira=1 questa app usa il proprio monitor e non legge questa chiave.\r\n"
				L"Disattivala nel lettore originale se la finestra in più non serve.",
				L"false(0): no abrir el monitor FM/MIDI de kbsasami_host.\r\n"
				L"true(1): kbsasami_host muestra ese monitor (predeterminado).\r\n"
				L"Solo se lee con kbsasami.raira=0 (KbMedia Player original).\r\n"
				L"Con raira=1 esta aplicación usa su propio monitor y no lee esta clave.\r\n"
				L"Desactívala en el reproductor original si no quieres la ventana extra.",
				L"false(0): kbsasami_host 의 FM/MIDI 모니터를 열지 않습니다.\r\n"
				L"true(1): kbsasami_host 가 그 모니터를 엽니다(초기값).\r\n"
				L"kbsasami.raira=0 (원래 KbMedia Player)일 때만 읽습니다.\r\n"
				L"raira=1 인 이 앱은 자기 모니터를 쓰고 이 항목을 읽지 않습니다.\r\n"
				L"여분 창이 필요 없으면 원래 플레이어에서 끄세요.",
				L"false(0): 不打开 kbsasami_host 的 FM/MIDI 监视器。\r\n"
				L"true(1): 由 kbsasami_host 显示该监视器（默认）。\r\n"
				L"仅在 kbsasami.raira=0（原来的 KbMedia Player）时读取。\r\n"
				L"raira=1 时本程序用自己的监视器，不读此项。\r\n"
				L"不想要多余窗口时，在原来的播放器里关掉。",
				L"false(0): لا تفتح مراقب FM/MIDI في kbsasami_host.\r\n"
				L"true(1): يعرض kbsasami_host ذلك المراقب (الافتراضي).\r\n"
				L"يُقرأ فقط عند kbsasami.raira=0 (KbMedia Player الأصلي).\r\n"
				L"مع raira=1 يستخدم هذا البرنامج مراقبَه ولا يقرأ هذا المفتاح.\r\n"
				L"عطّله في المشغّل الأصلي إن لم ترد النافذة الإضافية.",
				L"false(0): не открывать монитор FM/MIDI в kbsasami_host.\r\n"
				L"true(1): kbsasami_host показывает этот монитор (по умолчанию).\r\n"
				L"Читается только при kbsasami.raira=0 (исходный KbMedia Player).\r\n"
				L"При raira=1 это приложение использует свой монитор и не читает этот ключ.\r\n"
				L"Выключите его в исходном плеере, если лишнее окно не нужно.",
				L"false(0): den FM/MIDI-Monitor in kbsasami_host nicht öffnen.\r\n"
				L"true(1): kbsasami_host zeigt diesen Monitor (Vorgabe).\r\n"
				L"Nur gelesen bei kbsasami.raira=0 (ursprünglicher KbMedia Player).\r\n"
				L"Bei raira=1 nutzt diese App den eigenen Monitor und liest diesen Schlüssel nicht.\r\n"
				L"Im ursprünglichen Player ausschalten, wenn das Extrafenster stört.",
				L"false(0): não abrir o monitor FM/MIDI do kbsasami_host.\r\n"
				L"true(1): o kbsasami_host mostra esse monitor (padrão).\r\n"
				L"Lido só com kbsasami.raira=0 (KbMedia Player original).\r\n"
				L"Com raira=1 este aplicativo usa o próprio monitor e não lê esta chave.\r\n"
				L"Desligue no leitor original se não quiser a janela extra.",
				L"false(0): de FM/MIDI-monitor in kbsasami_host niet openen.\r\n"
				L"true(1): kbsasami_host toont die monitor (standaard).\r\n"
				L"Alleen gelezen bij kbsasami.raira=0 (oorspronkelijke KbMedia Player).\r\n"
				L"Bij raira=1 gebruikt deze app de eigen monitor en leest deze sleutel niet.\r\n"
				L"Zet hem uit in de oorspronkelijke speler als het extra venster niet nodig is.",
				L"false(0): nie otwierać monitora FM/MIDI w kbsasami_host.\r\n"
				L"true(1): kbsasami_host pokazuje ten monitor (domyślnie).\r\n"
				L"Czytane tylko przy kbsasami.raira=0 (oryginalny KbMedia Player).\r\n"
				L"Przy raira=1 ta aplikacja używa własnego monitora i nie czyta tego klucza.\r\n"
				L"Wyłącz w oryginalnym odtwarzaczu, jeśli dodatkowe okno przeszkadza.",
				L"false(0): kbsasami_host içindeki FM/MIDI izleyiciyi açma.\r\n"
				L"true(1): kbsasami_host o izleyiciyi gösterir (varsayılan).\r\n"
				L"Yalnızca kbsasami.raira=0 (özgün KbMedia Player) iken okunur.\r\n"
				L"raira=1 iken bu uygulama kendi izleyicisini kullanır ve bu anahtarı okumaz.\r\n"
				L"Fazladan pencere istemezseniz özgün oynatıcıda kapatın.") },
		{ KPI_CFG_TYPE_INT, SEC_KBSASAMI, KEY_MIDIMODE, L"kbsasami.midimode",
			L"0", NULL, NULL, NULL, NULL,
			KbsPick14(
				L".mpy / .mpw2 / .mid の SMF に使う MIDI マップ（モニタの mapForce と同じ）。\r\n"
				L"0=自動。.mpy の初期は 88、.mpw2 / .mpsmv は 88Pro。\r\n"
				L"ファイルには最大 4 つのモードが入り、自動は入っているものから最適なものを選ぶ。\r\n"
				L"CRender の GS VST が空なら、自動は XG を使う。\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC マップ。曲ごとの指定はプレイリストから。",
				L"MIDI map for .mpy/.mpw2/.mid SMF (same as monitor mapForce).\r\n"
				L"0=Auto. .mpy defaults to 88, .mpw2/.mpsmv to 88Pro.\r\n"
				L"A file may hold 4 modes; Auto picks the best one present.\r\n"
				L"If CRender GS VST is empty, Auto uses XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC maps. Per-file override in playlist.",
				L"Carte MIDI pour les SMF .mpy/.mpw2/.mid (identique à mapForce du moniteur).\r\n"
				L"0=Auto. .mpy vaut 88 par défaut, .mpw2/.mpsmv vaut 88Pro.\r\n"
				L"Un fichier peut contenir 4 modes ; Auto choisit le meilleur présent.\r\n"
				L"Si le VST GS de CRender est vide, Auto utilise XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=cartes ETC. Réglage par morceau dans la liste.",
				L"Mappa MIDI per SMF .mpy/.mpw2/.mid (uguale a mapForce del monitor).\r\n"
				L"0=Auto. .mpy parte da 88, .mpw2/.mpsmv da 88Pro.\r\n"
				L"Un file può avere 4 modalità; Auto sceglie la migliore presente.\r\n"
				L"Se il VST GS di CRender è vuoto, Auto usa XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=mappe ETC. Scelta per brano nella playlist.",
				L"Mapa MIDI para SMF .mpy/.mpw2/.mid (igual que mapForce del monitor).\r\n"
				L"0=Auto. .mpy empieza en 88, .mpw2/.mpsmv en 88Pro.\r\n"
				L"Un archivo puede tener 4 modos; Auto elige el mejor presente.\r\n"
				L"Si el VST GS de CRender está vacío, Auto usa XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=mapas ETC. Ajuste por tema en la lista.",
				L".mpy/.mpw2/.mid SMF 의 MIDI 맵입니다(모니터의 mapForce 와 같음).\r\n"
				L"0=자동. .mpy 기본은 88, .mpw2/.mpsmv 는 88Pro.\r\n"
				L"파일에 모드가 최대 4개 들어가며, 자동은 있는 것 중 가장 알맞은 것을 고릅니다.\r\n"
				L"CRender 의 GS VST 가 비어 있으면 자동은 XG 를 씁니다.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC 맵. 곡별 지정은 재생목록에서.",
				L".mpy/.mpw2/.mid 的 SMF 所用 MIDI 映射（与监视器的 mapForce 相同）。\r\n"
				L"0=自动。.mpy 默认 88，.mpw2/.mpsmv 默认 88Pro。\r\n"
				L"一个文件最多可含 4 种模式；自动会从已有的里面选最合适的。\r\n"
				L"若 CRender 的 GS VST 为空，自动改用 XG。\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC 映射。单曲指定在播放列表里。",
				L"خريطة MIDI لملفات SMF من نوع .mpy/.mpw2/.mid (مثل mapForce في المراقب).\r\n"
				L"0=تلقائي. .mpy الافتراضي 88، و.mpw2/.mpsmv هو 88Pro.\r\n"
				L"قد يحمل الملف 4 أوضاع؛ التلقائي يختار الأفضل الموجود.\r\n"
				L"إذا كان GS VST في CRender فارغًا، يستخدم التلقائي XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=خرائط ETC. التجاوز لكل ملف من قائمة التشغيل.",
				L"Карта MIDI для SMF .mpy/.mpw2/.mid (как mapForce монитора).\r\n"
				L"0=Авто. Для .mpy по умолчанию 88, для .mpw2/.mpsmv — 88Pro.\r\n"
				L"В файле может быть до 4 режимов; Авто выбирает лучший из имеющихся.\r\n"
				L"Если GS VST в CRender пуст, Авто берёт XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=карты ETC. Свой выбор — в плейлисте.",
				L"MIDI-Map für SMF .mpy/.mpw2/.mid (wie mapForce des Monitors).\r\n"
				L"0=Auto. .mpy startet mit 88, .mpw2/.mpsmv mit 88Pro.\r\n"
				L"Eine Datei kann 4 Modi enthalten; Auto wählt den besten vorhandenen.\r\n"
				L"Ist das GS-VST von CRender leer, nimmt Auto XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC-Maps. Pro Titel in der Playlist.",
				L"Mapa MIDI para SMF .mpy/.mpw2/.mid (igual ao mapForce do monitor).\r\n"
				L"0=Auto. .mpy começa em 88, .mpw2/.mpsmv em 88Pro.\r\n"
				L"Um arquivo pode ter 4 modos; Auto escolhe o melhor presente.\r\n"
				L"Se o VST GS do CRender estiver vazio, Auto usa XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=mapas ETC. Ajuste por faixa na playlist.",
				L"MIDI-kaart voor SMF .mpy/.mpw2/.mid (zelfde als mapForce van de monitor).\r\n"
				L"0=Auto. .mpy start op 88, .mpw2/.mpsmv op 88Pro.\r\n"
				L"Een bestand kan 4 modi bevatten; Auto kiest de beste die erin zit.\r\n"
				L"Is de GS-VST van CRender leeg, dan gebruikt Auto XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC-kaarten. Per nummer in de afspeellijst.",
				L"Mapa MIDI dla SMF .mpy/.mpw2/.mid (jak mapForce monitora).\r\n"
				L"0=Auto. .mpy domyślnie 88, .mpw2/.mpsmv — 88Pro.\r\n"
				L"Plik może mieć 4 tryby; Auto wybiera najlepszy z obecnych.\r\n"
				L"Gdy GS VST w CRender jest puste, Auto używa XG.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=mapy ETC. Wybór utworu w playliście.",
				L".mpy/.mpw2/.mid SMF için MIDI haritası (izleyicideki mapForce ile aynı).\r\n"
				L"0=Otomatik. .mpy varsayılanı 88, .mpw2/.mpsmv 88Pro.\r\n"
				L"Bir dosyada en çok 4 kip olabilir; Otomatik, olanların en uygununu seçer.\r\n"
				L"CRender GS VST boşsa Otomatik XG kullanır.\r\n"
				L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
				L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC haritaları. Parça seçimi çalma listesinden.") },
		{ KPI_CFG_TYPE_STR, SEC_KBSASAMI, L"vstfullpath_gs", L"kbsasami.vstfullpath_gs",
			L"", NULL, L"520", NULL, NULL,
			KbsPick14(
				L"GS の VST2/VST3 DLL のフルパス。raira=0 かつ vst=1 のときだけ使う。\r\n"
				L"kbsasami.kpi は DLL に合わせて kbsasami_host32.exe か kbsasami_host64.exe を起動する。\r\n"
				L"このアプリ（raira=1）はパスを無視し、自分の VST ホストを使う。",
				L"Full path of the GS VST2/VST3 DLL. Used only when raira=0 and vst=1.\r\n"
				L"kbsasami.kpi starts kbsasami_host32.exe or kbsasami_host64.exe to match the DLL.\r\n"
				L"This app (raira=1) ignores the path and uses its own VST host.",
				L"Chemin complet de la DLL VST2/VST3 GS. Utilisé seulement si raira=0 et vst=1.\r\n"
				L"kbsasami.kpi lance kbsasami_host32.exe ou kbsasami_host64.exe selon la DLL.\r\n"
				L"Cette application (raira=1) ignore le chemin et utilise son propre hôte VST.",
				L"Percorso completo della DLL VST2/VST3 GS. Usato solo con raira=0 e vst=1.\r\n"
				L"kbsasami.kpi avvia kbsasami_host32.exe o kbsasami_host64.exe in base alla DLL.\r\n"
				L"Questa app (raira=1) ignora il percorso e usa il proprio host VST.",
				L"Ruta completa de la DLL VST2/VST3 GS. Solo se usa con raira=0 y vst=1.\r\n"
				L"kbsasami.kpi inicia kbsasami_host32.exe o kbsasami_host64.exe según la DLL.\r\n"
				L"Esta aplicación (raira=1) ignora la ruta y usa su propio host VST.",
				L"GS VST2/VST3 DLL 의 전체 경로. raira=0 이고 vst=1 일 때만 사용합니다.\r\n"
				L"kbsasami.kpi 는 DLL 에 맞춰 kbsasami_host32.exe 또는 kbsasami_host64.exe 를 띄웁니다.\r\n"
				L"이 앱(raira=1)은 경로를 무시하고 자기 VST 호스트를 씁니다.",
				L"GS 的 VST2/VST3 DLL 全路径。仅在 raira=0 且 vst=1 时使用。\r\n"
				L"kbsasami.kpi 会按 DLL 启动 kbsasami_host32.exe 或 kbsasami_host64.exe。\r\n"
				L"本程序（raira=1）忽略该路径，使用自己的 VST 宿主。",
				L"المسار الكامل لملف GS VST2/VST3 DLL. يُستخدم فقط عند raira=0 و vst=1.\r\n"
				L"يشغّل kbsasami.kpi إما kbsasami_host32.exe أو kbsasami_host64.exe وفق DLL.\r\n"
				L"هذا البرنامج (raira=1) يتجاهل المسار ويستخدم مضيف VST الخاص به.",
				L"Полный путь к DLL GS VST2/VST3. Только при raira=0 и vst=1.\r\n"
				L"kbsasami.kpi запускает kbsasami_host32.exe или kbsasami_host64.exe под эту DLL.\r\n"
				L"Это приложение (raira=1) игнорирует путь и использует свой хост VST.",
				L"Vollständiger Pfad der GS-VST2/VST3-DLL. Nur bei raira=0 und vst=1.\r\n"
				L"kbsasami.kpi startet passend zur DLL kbsasami_host32.exe oder kbsasami_host64.exe.\r\n"
				L"Diese App (raira=1) ignoriert den Pfad und nutzt den eigenen VST-Host.",
				L"Caminho completo da DLL VST2/VST3 GS. Usado só com raira=0 e vst=1.\r\n"
				L"O kbsasami.kpi inicia kbsasami_host32.exe ou kbsasami_host64.exe conforme a DLL.\r\n"
				L"Este aplicativo (raira=1) ignora o caminho e usa o próprio host VST.",
				L"Volledig pad van de GS VST2/VST3-DLL. Alleen bij raira=0 en vst=1.\r\n"
				L"kbsasami.kpi start kbsasami_host32.exe of kbsasami_host64.exe passend bij de DLL.\r\n"
				L"Deze app (raira=1) negeert het pad en gebruikt de eigen VST-host.",
				L"Pełna ścieżka DLL GS VST2/VST3. Tylko przy raira=0 i vst=1.\r\n"
				L"kbsasami.kpi uruchamia kbsasami_host32.exe albo kbsasami_host64.exe zgodnie z DLL.\r\n"
				L"Ta aplikacja (raira=1) ignoruje ścieżkę i używa własnego hosta VST.",
				L"GS VST2/VST3 DLL dosyasının tam yolu. Yalnızca raira=0 ve vst=1 iken kullanılır.\r\n"
				L"kbsasami.kpi, DLL'e göre kbsasami_host32.exe veya kbsasami_host64.exe başlatır.\r\n"
				L"Bu uygulama (raira=1) yolu yok sayar ve kendi VST sunucusunu kullanır.") },
		{ KPI_CFG_TYPE_STR, SEC_KBSASAMI, L"vstfullpath_xg", L"kbsasami.vstfullpath_xg",
			L"", NULL, L"520", NULL, NULL,
			KbsPick14(
				L"XG の VST2/VST3 DLL のフルパス。raira=0 かつ vst=1 のときだけ使う。\r\n"
				L"XG の曲は GS の DLL にフォールバックしない。juicysf / sfplugin のパスは\r\n"
				L"このアプリの VST MIDI エンジンと同じ扱い。",
				L"Full path of the XG VST2/VST3 DLL. Used only when raira=0 and vst=1.\r\n"
				L"An XG song does not fall back to the GS DLL. juicysf / sfplugin paths work\r\n"
				L"the same way as this app's VST MIDI engine.",
				L"Chemin complet de la DLL VST2/VST3 XG. Utilisé seulement si raira=0 et vst=1.\r\n"
				L"Un morceau XG ne retombe pas sur la DLL GS. Les chemins juicysf / sfplugin\r\n"
				L"fonctionnent comme le moteur MIDI VST de cette application.",
				L"Percorso completo della DLL VST2/VST3 XG. Usato solo con raira=0 e vst=1.\r\n"
				L"Un brano XG non ricade sulla DLL GS. I percorsi juicysf / sfplugin funzionano\r\n"
				L"come il motore MIDI VST di questa app.",
				L"Ruta completa de la DLL VST2/VST3 XG. Solo se usa con raira=0 y vst=1.\r\n"
				L"Una pieza XG no cae a la DLL GS. Las rutas juicysf / sfplugin funcionan\r\n"
				L"igual que el motor MIDI VST de esta aplicación.",
				L"XG VST2/VST3 DLL 의 전체 경로. raira=0 이고 vst=1 일 때만 사용합니다.\r\n"
				L"XG 곡은 GS DLL 로 넘어가지 않습니다. juicysf / sfplugin 경로는\r\n"
				L"이 앱의 VST MIDI 엔진과 같습니다.",
				L"XG 的 VST2/VST3 DLL 全路径。仅在 raira=0 且 vst=1 时使用。\r\n"
				L"XG 曲不会回退到 GS DLL。juicysf / sfplugin 的路径与\r\n"
				L"本程序的 VST MIDI 引擎相同。",
				L"المسار الكامل لملف XG VST2/VST3 DLL. يُستخدم فقط عند raira=0 و vst=1.\r\n"
				L"مقطوعة XG لا تعود إلى DLL الخاصة بـ GS. مسارات juicysf / sfplugin تعمل\r\n"
				L"كما في محرك MIDI VST لهذا البرنامج.",
				L"Полный путь к DLL XG VST2/VST3. Только при raira=0 и vst=1.\r\n"
				L"Пьеса XG не откатывается на DLL GS. Пути juicysf / sfplugin работают\r\n"
				L"так же, как MIDI-движок VST этого приложения.",
				L"Vollständiger Pfad der XG-VST2/VST3-DLL. Nur bei raira=0 und vst=1.\r\n"
				L"Ein XG-Stück fällt nicht auf die GS-DLL zurück. juicysf- / sfplugin-Pfade\r\n"
				L"funktionieren wie die VST-MIDI-Engine dieser App.",
				L"Caminho completo da DLL VST2/VST3 XG. Usado só com raira=0 e vst=1.\r\n"
				L"Uma faixa XG não recai na DLL GS. Os caminhos juicysf / sfplugin funcionam\r\n"
				L"como o motor MIDI VST deste aplicativo.",
				L"Volledig pad van de XG VST2/VST3-DLL. Alleen bij raira=0 en vst=1.\r\n"
				L"Een XG-stuk valt niet terug op de GS-DLL. Paden van juicysf / sfplugin werken\r\n"
				L"hetzelfde als de VST-MIDI-engine van deze app.",
				L"Pełna ścieżka DLL XG VST2/VST3. Tylko przy raira=0 i vst=1.\r\n"
				L"Utwór XG nie spada do DLL GS. Ścieżki juicysf / sfplugin działają\r\n"
				L"tak jak silnik MIDI VST tej aplikacji.",
				L"XG VST2/VST3 DLL dosyasının tam yolu. Yalnızca raira=0 ve vst=1 iken kullanılır.\r\n"
				L"XG parçası GS DLL'ine düşmez. juicysf / sfplugin yolları\r\n"
				L"bu uygulamanın VST MIDI motoruyla aynı çalışır.") },
		{ KPI_CFG_TYPE_INT, SEC_KBSASAMI, KEY_FMMODE, L"kbsasami.fmmode",
			L"2", NULL, NULL, NULL, NULL,
			KbsPick14(
				L".fpy の FM 音源（SASAMI の /B /N と同じ）。\r\n"
				L"0=BEEP（PC スピーカ風の矩形波）\r\n"
				L"1=OPN（YM2608 の SCH オフ、FM3+SSG3。SSG は OPNA と同じ経路）\r\n"
				L"2=OPNA（YM2608。ファイルヘッダから 6/10ch。初期値）\r\n"
				L"曲ごとの指定はプレイリストのコンテキストメニュー。",
				L"FM sound source for .fpy (like SASAMI /B /N).\r\n"
				L"0=BEEP (PC-speaker style square)\r\n"
				L"1=OPN (YM2608 SCH-off, FM3+SSG3; same SSG path as OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10ch from file header; default)\r\n"
				L"Per-file override in playlist context menu.",
				L"Source FM des .fpy (comme /B /N de SASAMI).\r\n"
				L"0=BEEP (carré façon haut-parleur PC)\r\n"
				L"1=OPN (YM2608 SCH coupé, FM3+SSG3 ; même chemin SSG que l'OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 canaux selon l'en-tête ; défaut)\r\n"
				L"Choix par morceau dans le menu contextuel de la liste.",
				L"Sorgente FM per i .fpy (come /B /N di SASAMI).\r\n"
				L"0=BEEP (onda quadra in stile altoparlante PC)\r\n"
				L"1=OPN (YM2608 con SCH spento, FM3+SSG3; stesso percorso SSG dell'OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 canali dall'intestazione; predefinito)\r\n"
				L"Scelta per brano nel menu contestuale della playlist.",
				L"Fuente FM de los .fpy (como /B /N de SASAMI).\r\n"
				L"0=BEEP (onda cuadrada al estilo del altavoz del PC)\r\n"
				L"1=OPN (YM2608 con SCH apagado, FM3+SSG3; mismo camino SSG que OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 canales según la cabecera; predeterminado)\r\n"
				L"Ajuste por tema en el menú contextual de la lista.",
				L".fpy 의 FM 음원입니다(SASAMI 의 /B /N 과 같음).\r\n"
				L"0=BEEP (PC 스피커풍 구형파)\r\n"
				L"1=OPN (YM2608 SCH 끔, FM3+SSG3. SSG 는 OPNA 와 같은 경로)\r\n"
				L"2=OPNA (YM2608. 파일 헤더에서 6/10ch. 초기값)\r\n"
				L"곡별 지정은 재생목록의 컨텍스트 메뉴에서.",
				L".fpy 的 FM 音源（与 SASAMI 的 /B /N 相同）。\r\n"
				L"0=BEEP（PC 扬声器式方波）\r\n"
				L"1=OPN（YM2608 关闭 SCH，FM3+SSG3；SSG 与 OPNA 同一路径）\r\n"
				L"2=OPNA（YM2608，由文件头决定 6/10 声道；默认）\r\n"
				L"单曲指定在播放列表的右键菜单里。",
				L"مصدر FM لملفات .fpy (مثل /B /N في SASAMI).\r\n"
				L"0=BEEP (موجة مربعة بأسلوب سماعة الحاسب)\r\n"
				L"1=OPN (YM2608 مع SCH مطفأ، FM3+SSG3؛ مسار SSG كـ OPNA)\r\n"
				L"2=OPNA (YM2608، ‏6/10 قنوات من ترويسة الملف؛ الافتراضي)\r\n"
				L"التجاوز لكل ملف من قائمة السياق لقائمة التشغيل.",
				L"Источник FM для .fpy (как /B /N в SASAMI).\r\n"
				L"0=BEEP (меандр в духе динамика ПК)\r\n"
				L"1=OPN (YM2608, SCH выкл., FM3+SSG3; тот же путь SSG, что у OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 каналов из заголовка; по умолчанию)\r\n"
				L"Свой выбор для файла — в контекстном меню плейлиста.",
				L"FM-Klangquelle für .fpy (wie /B /N bei SASAMI).\r\n"
				L"0=BEEP (Rechteck wie ein PC-Lautsprecher)\r\n"
				L"1=OPN (YM2608, SCH aus, FM3+SSG3; gleicher SSG-Weg wie OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 Kanäle aus dem Dateikopf; Vorgabe)\r\n"
				L"Pro Datei im Kontextmenü der Playlist.",
				L"Fonte FM dos .fpy (como /B /N do SASAMI).\r\n"
				L"0=BEEP (onda quadrada no estilo do alto-falante do PC)\r\n"
				L"1=OPN (YM2608 com SCH desligado, FM3+SSG3; mesmo caminho SSG do OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 canais pelo cabeçalho; padrão)\r\n"
				L"Ajuste por arquivo no menu de contexto da playlist.",
				L"FM-bron voor .fpy (zoals /B /N van SASAMI).\r\n"
				L"0=BEEP (blokgolf in de stijl van een pc-luidspreker)\r\n"
				L"1=OPN (YM2608, SCH uit, FM3+SSG3; hetzelfde SSG-pad als OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 kanalen uit de bestandskop; standaard)\r\n"
				L"Per bestand in het contextmenu van de afspeellijst.",
				L"Źródło FM dla .fpy (jak /B /N w SASAMI).\r\n"
				L"0=BEEP (fala prostokątna jak głośnik PC)\r\n"
				L"1=OPN (YM2608, SCH wyłączone, FM3+SSG3; ta sama ścieżka SSG co OPNA)\r\n"
				L"2=OPNA (YM2608, 6/10 kanałów z nagłówka; domyślnie)\r\n"
				L"Wybór dla pliku w menu kontekstowym playlisty.",
				L".fpy için FM ses kaynağı (SASAMI /B /N gibi).\r\n"
				L"0=BEEP (PC hoparlörü tarzı kare dalga)\r\n"
				L"1=OPN (YM2608 SCH kapalı, FM3+SSG3; SSG yolu OPNA ile aynı)\r\n"
				L"2=OPNA (YM2608, dosya başlığından 6/10 kanal; varsayılan)\r\n"
				L"Parça seçimi çalma listesinin bağlam menüsünden.") },
		{ 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
	};
	for (int i = 0; sec[i].cszSection; i++)
		pEnumerator->EnumSection(&sec[i]);
	for (int i = 0; key[i].cszSection; i++)
		pEnumerator->EnumKey(&key[i]);
	return TRUE;
}

DWORD WINAPI KbSasamiDecoderModule::ApplyConfig(const wchar_t* cszSection, const wchar_t* cszKey, INT64 nValue, double dValue, const wchar_t* cszValue)
{
	(void)dValue;
	if (!m_pConfig) return KPI_CFGRET_OK;
	if (cszKey && cszKey[0]) {
		if (cszValue)
			m_pConfig->SetStr(cszSection, cszKey, cszValue);
		else
			m_pConfig->SetInt(cszSection, cszKey, nValue);
		/* vst 切替は位置がずれるので RELOAD_DATA で先頭から開き直す。 */
		if (cszKey && (_wcsicmp(cszKey, KEY_VST) == 0
			|| _wcsicmp(cszKey, L"kbsasami.vst") == 0)) {
			KbSasamiRequestRestartHead();
			return KPI_CFGRET_RELOAD_DATA;
		}
		return KPI_CFGRET_RELOAD_DATA;
	}
	return KPI_CFGRET_OK;
}

HRESULT WINAPI kpi_CreateInstance(REFIID riid, void** ppvObject, IKpiUnknown* pUnknown)
{
	*ppvObject = NULL;
	if (!IsEqualIID(riid, IID_IKpiDecoderModule))
		return E_NOINTERFACE;
	IKpiConfig* pConfig = NULL;
	kpi_CreateConfig(pUnknown, &kGuid, NULL, &pConfig);
	KbSasamiDecoderModule* mod = new KbSasamiDecoderModule(pConfig);
	if (pConfig) pConfig->Release();
	*ppvObject = (IKpiDecoderModule*)mod;
	return S_OK;
}

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		g_hKpi = hModule;
		DisableThreadLibraryCalls(hModule);
	}
	return TRUE;
}

