#pragma once

#include "kpi_decoder.h"
#include "kpi_impl.h"
#include "kbs98_config.h"

extern const wchar_t SEC_GENERAL[];// = L"General";
extern const wchar_t KEY_RHYTHM[];// = L"Rhythm";

class s98File;
class KbS98Decoder : public KbKpiUnknownImpl<IKpiDecoder>
{
private:
    s98File       *m_pS98File;
    KPI_MEDIAINFO  m_MediaInfo;
    UINT64         m_qwCurSample;
    UINT64         m_qwEndSample;
public:
    static KbS98Decoder *s_pActive;
    kbs98_Settings m_Settings;
    DWORD  __fastcall Open(const KPI_MEDIAINFO *cpRequest, 
                           IKpiFile   *pFile, 
                           IKpiFolder *pFolder,
                           IKpiConfig *pConfig);
    DWORD  WINAPI Select(DWORD dwNumber,
                         const KPI_MEDIAINFO **ppMediaInfo,
                         IKpiTagInfo *pTagInfo,
                         DWORD dwTagGetFlags);
    DWORD  WINAPI Render(BYTE *pBuffer, DWORD dwSizeSample);
    UINT64 WINAPI Seek(UINT64 qwPosSample, DWORD dwFlag);
    DWORD  WINAPI UpdateConfig(void *pvReserved){return 0;}
    void __fastcall GetTagInfo(IKpiTagInfo *pTagInfo, DWORD dwTagGetFlags);
    KbS98Decoder(IKpiConfig *pConfig);
    ~KbS98Decoder(void);
};
