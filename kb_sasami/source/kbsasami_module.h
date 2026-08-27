#pragma once

#include "kpi_decoder.h"
#include "kpi_impl.h"

class KbSasamiDecoderModule : public KbKpiUnknownImpl<IKpiDecoderModule>
{
private:
	KPI_DECODER_MODULEINFO m_ModuleInfo;
	IKpiConfig* m_pConfig;
public:
	explicit KbSasamiDecoderModule(IKpiConfig* pConfig);
	~KbSasamiDecoderModule();
	void WINAPI GetModuleInfo(const KPI_DECODER_MODULEINFO** ppInfo);
	DWORD WINAPI Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder, IKpiDecoder** ppDecoder);
	BOOL WINAPI EnumConfig(IKpiConfigEnumerator* pEnumerator);
	DWORD WINAPI ApplyConfig(const wchar_t* cszSection, const wchar_t* cszKey, INT64 nValue, double dValue, const wchar_t* cszValue);
};
