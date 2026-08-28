#pragma once

#include "kpi_decoder.h"
#include "kpi_impl.h"
#include "sasami_file.h"
#include "sasami_midi.h"
#include "sasami_fm.h"
#include "sequencer.hpp"
#include "midisynth.hpp"

class KbSasamiDecoder : public KbKpiUnknownImpl<IKpiDecoder>, public output
{
private:
	enum { MAX_PORTS = 4, MIX_FRAMES = 8192 };
	KPI_MEDIAINFO m_MediaInfo;
	IKpiConfig* m_pConfig;
	SasamiKind m_kind;
	SasamiFmPlayer m_fm;
	fm_note_factory m_note_factory;
	synthesizer m_synthesizer;
	synthesizer* m_synths[MAX_PORTS];
	sequencer m_sequencer;
	int m_nPorts;
	UINT64 m_curSample;
	UINT64 m_lastSample;
	DWORD m_endSample;
	DWORD m_silentSample;
	bool m_seeking;
	bool m_fmMode;
	int m_raira; // 1=このアプリ専用経路
	int m_vst;   // 解釈後: 0=FM MIDI(fmmidi), 1=VST側に任せる
	int m_mapDefault; // kbsasami.map (0..19)
	char m_titleSjis[65];
	double m_loopStart;
	double m_loopEnd;
	double m_mix[MIX_FRAMES * 2];
	uint8_t m_smf[SASAMI_MAX_SMF];
	int m_smfSize;

	struct MemFile { const BYTE* p; DWORD size; DWORD pos; };
	static int MemGetc(void* fp);
	void LoadProgramsTxt();
	void ReadOptions();
	void midi_message(int port, uint_least32_t message) override;
	void sysex_message(int port, const void* data, std::size_t size) override;
	void meta_event(int type, const void* data, std::size_t size) override;
	void reset() override;

public:
	explicit KbSasamiDecoder(IKpiConfig* pConfig);
	~KbSasamiDecoder();
	DWORD __fastcall Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder);
	DWORD WINAPI Select(DWORD dwNumber, const KPI_MEDIAINFO** ppMediaInfo, IKpiTagInfo* pTagInfo, DWORD dwTagGetFlags);
	UINT64 WINAPI Seek(UINT64 qwPosSample, DWORD dwFlag);
	DWORD WINAPI Render(BYTE* pBuffer, DWORD dwSizeSample);
	DWORD WINAPI UpdateConfig(void*);
};
