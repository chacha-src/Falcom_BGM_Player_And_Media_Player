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
#define KBSASAMI_VERSION_STR L"1.00"
#endif

static const wchar_t kDescription[] = L"SASAMI FM/MIDI Decoder v" KBSASAMI_VERSION_STR L" (FPY / MPY / MPW2)";
static const wchar_t kCopyright[] =
	L"kbsasami.kpi SASAMI player\n"
	L"FM: ymfm (Aaron Giles) YM2608/OPNA\n"
	L"MIDI PCM path: fmmidi (yuno) via SMF conversion\n"
	L"Commands ported from SASAMI / SASAMI11 / SASAMIM";

// {A7C3E91F-4B2D-4E6A-9C18-8F5D2A1B7E03}
static const GUID kGuid =
{ 0xa7c3e91f, 0x4b2d, 0x4e6a, { 0x9c, 0x18, 0x8f, 0x5d, 0x2a, 0x1b, 0x7e, 0x03 } };

static const wchar_t kExts[] = L".fpy/.mpy/.mpw2";

static const wchar_t SEC_KBSASAMI[] = L"kbsasami";
static const wchar_t KEY_VST[] = L"vst";
static const wchar_t KEY_RAIRA[] = L"raira";
static const wchar_t KEY_MAP[] = L"map";

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
			L"true(1): expect VST-side playback\r\n"
			L"\r\n"
			L"With kbsasami.raira=1, 0 and 1 meanings are swapped.\r\n"
			L"Default for original player is false (FM MIDI)." },
		{ KPI_CFG_TYPE_BOOL, SEC_KBSASAMI, KEY_RAIRA, L"kbsasami.raira",
			L"0", NULL, NULL, NULL, NULL,
			L"false(0): original KbMedia Player (interpret vst as-is)\r\n"
			L"true(1): this app. Swaps vst 0/1 internally.\r\n"
			L"This app always writes raira=1." },
		{ KPI_CFG_TYPE_INT, SEC_KBSASAMI, KEY_MAP, L"kbsasami.map",
			L"4", NULL, NULL, NULL, NULL,
			L"MIDI map for .mpy/.mpw2 SMF conversion (same as monitor mapForce).\r\n"
			L"0=Auto(use this default) 1=GS 2=XG 3=55map 4=88map 5=88Promap 6=8820map\r\n"
			L"7=GMmap 8=SDmap 9=LAmap 10..19=ETC maps. Per-file override in playlist." },
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
		return KPI_CFGRET_NOTIFY;
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
