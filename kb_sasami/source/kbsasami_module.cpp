#include <windows.h>
#include "kbsasami_module.h"
#include "kbsasami_decoder.h"
#include "kpi.h"

HINSTANCE g_hKpi = NULL;

#ifdef _DEBUG
static const DWORD kPluginVersion = 0x7FFFFFFF;
#define KBSASAMI_VERSION_STR L"0x7FFFFFFF(Debug)"
#else
static const DWORD kPluginVersion = 1;
#define KBSASAMI_VERSION_STR L"1.20"
#endif

static const wchar_t kDescription[] = L"SASAMI and MIDI Decoder v" KBSASAMI_VERSION_STR L" (FPY / MPY / RCP / EUP / SNG / ZMS)";
static const wchar_t kCopyright[] =
	L"kbsasami.kpi SASAMI(「ささみ☆ミ」) player\n"
	L"FM: ymfm YM2608 (OPNA / OPN=SCH-off) + soft BEEP\n"
	L"MIDI PCM path: fmmidi (yuno) via SMF conversion\n"
	L"RCP/EUP/SNG/ZMS/SMF: convert inside the plugin if the file is not already SMF\n"
	L"raira=1: vst 0/1 swapped, louder FM drums. raira=0: stock drums, MIDI vol/2\n"
	L"raira does not skip conversion\n"
	L"Commands ported from SASAMI / SASAMI11 / SASAMIM";

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
		kDescription,
		kCopyright,
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
	// Help text is ASCII to keep MSVC CP932 source clean (no /utf-8 on this project).
	static const KPI_CFG_SECTION sec[] = {
		{ SEC_KBSASAMI, L"kbsasami",
			L"kbsasami.kpi options.\r\n"
			L"Original KbMedia Player: kbsasami.vst=0 means FM MIDI (fmmidi).\r\n"
			L"When kbsasami.raira=1, this app swaps vst 0/1 internally." },
		{ NULL, NULL, NULL }
	};
	static const KPI_CFG_KEY key[] = {
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_VST, L"kbsasami.vst",
			L"0", NULL, NULL, NULL, NULL,
			L"false(0): FM MIDI mode (fmmidi / programs.txt inside plugin)\r\n"
			L"true(1): VST. raira=1 leaves it to this app.\r\n"
			L"raira=0 plays kbsasami.vstfullpath_gs / _xg via kbsasami_host32/64.\r\n"
			L"\r\n"
			L"With kbsasami.raira=1, 0 and 1 meanings are swapped.\r\n"
			L"Default for original player is false (FM MIDI)." },
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_RAIRA, L"kbsasami.raira",
			L"0", NULL, NULL, NULL, NULL,
			L"false(0): original KbMedia Player (interpret vst as-is, stock drums, MIDI vol/2)\r\n"
			L"true(1): this app. Swaps vst 0/1 internally, louder FM drums.\r\n"
			L"This app always writes raira=1." },
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_FMMIDIMONITOR, L"kbsasami.fmmidimonitor",
			L"1", NULL, NULL, NULL, NULL,
			L"false(0): do not open the FM/MIDI monitor in kbsasami_host.\r\n"
			L"true(1): kbsasami_host shows that monitor (default).\r\n"
			L"Read only when kbsasami.raira=0 (original KbMedia Player).\r\n"
			L"When raira=1 this app uses its own monitor and does not read this key.\r\n"
			L"Turn it off in the original player if you do not want the extra window." },
		{ KPI_CFG_TYPE_INT, SEC_KBSASAMI, KEY_MIDIMODE, L"kbsasami.midimode",
			L"0", NULL, NULL, NULL, NULL,
			L"MIDI map for .mpy/.mpw2/.mid SMF (same as monitor mapForce).\r\n"
			L"0=Auto. .mpy defaults to 88, .mpw2/.mpsmv to 88Pro.\r\n"
			L"A file may hold 4 modes; Auto picks the best one present.\r\n"
			L"If CRender GS VST is empty, Auto uses XG.\r\n"
			L"1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
			L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC maps. Per-file override in playlist." },
		{ KPI_CFG_TYPE_STR, SEC_KBSASAMI, L"vstfullpath_gs", L"kbsasami.vstfullpath_gs",
			L"", NULL, L"520", NULL, NULL,
			L"Full path of the GS VST2/VST3 DLL. Used only when raira=0 and vst=1.\r\n"
			L"kbsasami.kpi starts kbsasami_host32.exe or kbsasami_host64.exe to match the DLL.\r\n"
			L"This app (raira=1) ignores the path and uses its own VST host." },
		{ KPI_CFG_TYPE_STR, SEC_KBSASAMI, L"vstfullpath_xg", L"kbsasami.vstfullpath_xg",
			L"", NULL, L"520", NULL, NULL,
			L"Full path of the XG VST2/VST3 DLL. Used only when raira=0 and vst=1.\r\n"
			L"An XG song does not fall back to the GS DLL. juicysf / sfplugin paths work\r\n"
			L"the same way as this app's VST MIDI engine." },
		{ KPI_CFG_TYPE_INT, SEC_KBSASAMI, KEY_FMMODE, L"kbsasami.fmmode",
			L"2", NULL, NULL, NULL, NULL,
			L"FM sound source for .fpy (like SASAMI /B /N).\r\n"
			L"0=BEEP (PC-speaker style square)\r\n"
			L"1=OPN (YM2608 SCH-off, FM3+SSG3; same SSG path as OPNA)\r\n"
			L"2=OPNA (YM2608, 6/10ch from file header; default)\r\n"
			L"Per-file override in playlist context menu." },
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

