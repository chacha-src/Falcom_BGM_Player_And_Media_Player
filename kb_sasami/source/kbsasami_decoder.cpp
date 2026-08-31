#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "kbsasami_decoder.h"

extern HINSTANCE g_hKpi;

static const wchar_t SEC_KBSASAMI[] = L"kbsasami";
static const wchar_t KEY_VST[] = L"vst";
static const wchar_t KEY_RAIRA[] = L"raira";
static const wchar_t KEY_MIDIMODE[] = L"midimode";
static const wchar_t KEY_MAP_LEGACY[] = L"map";
static const wchar_t KEY_FMMODE[] = L"fmmode";

static uint8_t s_fileBuf[SASAMI_MAX_FILE];

int KbSasamiDecoder::MemGetc(void* fp)
{
	MemFile* f = (MemFile*)fp;
	if (f->pos < f->size) return (int)f->p[f->pos++];
	return -1;
}

KbSasamiDecoder::KbSasamiDecoder(IKpiConfig* pConfig)
	: m_synthesizer(&m_note_factory)
	, m_pConfig(pConfig)
{
	kpi_InitMediaInfo(&m_MediaInfo);
	if (m_pConfig) m_pConfig->AddRef();
	m_kind = SASAMI_KIND_UNKNOWN;
	m_nPorts = 0;
	m_curSample = 0;
	m_lastSample = 0;
	m_endSample = 0;
	m_silentSample = 0;
	m_seeking = false;
	m_fmMode = false;
	m_raira = 0;
	m_vst = 0;
	m_mapDefault = 4;
	m_fmModeDefault = 2;
	m_titleSjis[0] = 0;
	m_loopStart = -1.0;
	m_loopEnd = -1.0;
	m_smfSize = 0;
	m_synths[0] = &m_synthesizer;
	for (int i = 1; i < MAX_PORTS; i++) m_synths[i] = NULL;
}

KbSasamiDecoder::~KbSasamiDecoder()
{
	for (int i = 1; i < MAX_PORTS; i++) {
		delete m_synths[i];
		m_synths[i] = NULL;
	}
	m_fm.Close();
	if (m_pConfig) {
		m_pConfig->Release();
		m_pConfig = NULL;
	}
}

void KbSasamiDecoder::ReadOptions()
{
	m_raira = 0;
	m_vst = 0;
	m_mapDefault = 4;
	m_fmModeDefault = 2;
	if (m_pConfig) {
		m_raira = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_RAIRA, 0);
		m_vst = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_VST, 0);
		m_mapDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_MIDIMODE, -1);
		if (m_mapDefault < 0)
			m_mapDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_MAP_LEGACY, 4);
		m_fmModeDefault = (int)m_pConfig->GetInt(SEC_KBSASAMI, KEY_FMMODE, 2);
	}
	if (m_mapDefault < 0 || m_mapDefault > 19) m_mapDefault = 4;
	if (m_fmModeDefault < 0 || m_fmModeDefault > 2) m_fmModeDefault = 2;
	if (m_raira)
		m_vst = m_vst ? 0 : 1;
}

void KbSasamiDecoder::LoadProgramsTxt()
{
	wchar_t sz[MAX_PATH];
	GetModuleFileNameW(g_hKpi, sz, MAX_PATH);
	wchar_t* slash = wcsrchr(sz, L'\\');
	if (slash) slash[1] = 0;
	wcsncat_s(sz, L"programs.txt", _TRUNCATE);
	FILE* fp = NULL;
	_wfopen_s(&fp, sz, L"rt");
	if (!fp) return;
	while (!feof(fp)) {
		int c = getc(fp);
		if (c == '@') {
			int prog = 0;
			FMPARAMETER p;
			if (fscanf_s(fp, "%d%d%d%d", &prog, &p.ALG, &p.FB, &p.LFO) == 4
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op1.AR, &p.op1.DR, &p.op1.SR, &p.op1.RR, &p.op1.SL, &p.op1.TL, &p.op1.KS, &p.op1.ML, &p.op1.DT, &p.op1.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op2.AR, &p.op2.DR, &p.op2.SR, &p.op2.RR, &p.op2.SL, &p.op2.TL, &p.op2.KS, &p.op2.ML, &p.op2.DT, &p.op2.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op3.AR, &p.op3.DR, &p.op3.SR, &p.op3.RR, &p.op3.SL, &p.op3.TL, &p.op3.KS, &p.op3.ML, &p.op3.DT, &p.op3.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op4.AR, &p.op4.DR, &p.op4.SR, &p.op4.RR, &p.op4.SL, &p.op4.TL, &p.op4.KS, &p.op4.ML, &p.op4.DT, &p.op4.AMS) == 10) {
				m_note_factory.set_program(prog, p);
			}
		} else if (c == '*') {
			int prog = 0;
			DRUMPARAMETER p;
			if (fscanf_s(fp, "%d%d%d%d%d%d%d", &prog, &p.ALG, &p.FB, &p.LFO, &p.key, &p.panpot, &p.assign) == 7
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op1.AR, &p.op1.DR, &p.op1.SR, &p.op1.RR, &p.op1.SL, &p.op1.TL, &p.op1.KS, &p.op1.ML, &p.op1.DT, &p.op1.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op2.AR, &p.op2.DR, &p.op2.SR, &p.op2.RR, &p.op2.SL, &p.op2.TL, &p.op2.KS, &p.op2.ML, &p.op2.DT, &p.op2.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op3.AR, &p.op3.DR, &p.op3.SR, &p.op3.RR, &p.op3.SL, &p.op3.TL, &p.op3.KS, &p.op3.ML, &p.op3.DT, &p.op3.AMS) == 10
				&& fscanf_s(fp, "%d%d%d%d%d%d%d%d%d%d", &p.op4.AR, &p.op4.DR, &p.op4.SR, &p.op4.RR, &p.op4.SL, &p.op4.TL, &p.op4.KS, &p.op4.ML, &p.op4.DT, &p.op4.AMS) == 10) {
				m_note_factory.set_drum_program(prog, p);
			}
		}
	}
	fclose(fp);
}

void KbSasamiDecoder::midi_message(int port, uint_least32_t message)
{
	if (m_seeking) return;
	if (port < 0 || port >= m_nPorts) port = 0;
	m_synths[port]->midi_event(message);
}

void KbSasamiDecoder::sysex_message(int port, const void* data, std::size_t size)
{
	if (m_seeking) return;
	if (port < 0 || port >= m_nPorts) port = 0;
	m_synths[port]->sysex_message(data, size);
}

void KbSasamiDecoder::meta_event(int, const void*, std::size_t) {}

void KbSasamiDecoder::reset()
{
	for (int i = 0; i < m_nPorts; i++)
		m_synths[i]->reset();
}

DWORD WINAPI KbSasamiDecoder::UpdateConfig(void*)
{
	ReadOptions();
	return 0;
}

DWORD __fastcall KbSasamiDecoder::Open(const KPI_MEDIAINFO* cpRequest, IKpiFile* pFile, IKpiFolder* pFolder)
{
	(void)pFolder;
	if (!pFile) return 0;
	ReadOptions();
	pFile->AddRef();
	UINT64 sz = pFile->GetSize();
	if (sz == 0 || sz == (UINT64)-1 || sz > SASAMI_MAX_FILE) {
		pFile->Release();
		return 0;
	}
	pFile->Seek(0, FILE_BEGIN);
	DWORD n = pFile->Read(s_fileBuf, (DWORD)sz);
	wchar_t name[MAX_PATH];
	name[0] = 0;
	pFile->GetFileName(name, MAX_PATH);
	const wchar_t* real = NULL;
	pFile->GetRealFileW(&real);
	pFile->Release();
	if (n == 0) return 0;

	const wchar_t* pathForKind = (real && real[0]) ? real : name;
	SasamiKind hint = SasamiKindFromPath(pathForKind);
	if (hint == SASAMI_KIND_UNKNOWN) {
		if (n >= 3 && s_fileBuf[0] == 0xEE && s_fileBuf[1] == 0xEE && s_fileBuf[2] == 0xEE)
			hint = SASAMI_KIND_MPW2;
		else if (n >= 4 && s_fileBuf[1] == 0 && (s_fileBuf[0] + s_fileBuf[1] * 256) >= 0x1000)
			hint = SASAMI_KIND_FPY;
		else
			hint = SASAMI_KIND_MPY;
	}

	static SasamiSong s_song;
	if (!SasamiLoadMemory(s_fileBuf, n, hint, &s_song)) return 0;
	m_kind = s_song.kind;
	strncpy_s(m_titleSjis, s_song.titleSjis, _TRUNCATE);

	DWORD rate = 44100;
	if (cpRequest && cpRequest->dwSampleRate >= 8000 && cpRequest->dwSampleRate <= 192000)
		rate = cpRequest->dwSampleRate;

	if (s_song.kind == SASAMI_KIND_FPY) {
		m_fmMode = true;
		wchar_t plugDir[MAX_PATH];
		GetModuleFileNameW(g_hKpi, plugDir, MAX_PATH);
		wchar_t* sl = wcsrchr(plugDir, L'\\');
		if (sl) *sl = 0;
		else plugDir[0] = 0;
		const int fmMode = SasamiResolveFmModeW(pathForKind, m_fmModeDefault);
		if (!m_fm.Open(s_song, rate, plugDir, fmMode)) return 0;
		/* FMモニタ dump は OPN/OPNA なら常時 ON。
		   以前は m_raira 依存だったが、IKpiConfig が NullConfig になると
		   raira が 0 のまま音声だけ再生され、live が更新されずモニタが固まる。 */
		if (fmMode == 1 || fmMode == 2)
			m_fm.SetFmMonDump(1, pathForKind);
		else
			m_fm.SetFmMonDump(0, pathForKind);
		m_MediaInfo.dwSampleRate = m_fm.SampleRate();
		m_MediaInfo.dwChannels = 2;
		m_MediaInfo.nBitsPerSample = 16;
		m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
		m_MediaInfo.dwUnitSample = m_MediaInfo.dwSampleRate / 100;
		{
			const UINT64 samples = m_fm.TotalSamples();
			const DWORD sr = m_MediaInfo.dwSampleRate;
			UINT64 ns = 0;
			if (sr > 0)
				ns = (samples * 10000000ull + (UINT64)sr - 1ull) / (UINT64)sr;
			m_MediaInfo.qwLength = ns;
		}
		m_MediaInfo.dwCount = 1;
		m_MediaInfo.dwNumber = 1;
		m_lastSample = m_fm.TotalSamples();
		m_curSample = 0;
		return 1;
	}

	// MIDI: 解釈後 vst=1 なら fmmidi を起動せず失敗 (ホストが VST 経路へ)
	if (m_vst != 0)
		return 0;

	m_fmMode = false;
	m_smfSize = 0;
	const wchar_t* pathForMap = (real && real[0]) ? real : name;
	const int mapForce = SasamiResolveMapForceW(pathForMap, m_mapDefault);
	SasamiMidiMap map = SASAMI_MAP_GS88;
	int gsLsb = 2;
	SasamiMapForceToSel(mapForce, &map, &gsLsb);
	if (!SasamiConvertToSmf(s_song, map, gsLsb, m_smf, SASAMI_MAX_SMF, &m_smfSize)) return 0;
	LoadProgramsTxt();
	MemFile mf;
	mf.p = m_smf;
	mf.size = (DWORD)m_smfSize;
	mf.pos = 0;
	if (!m_sequencer.load(&mf, MemGetc)) return 0;
	m_nPorts = m_sequencer.get_num_ports();
	if (m_nPorts < 1) m_nPorts = 1;
	if (m_nPorts > MAX_PORTS) m_nPorts = MAX_PORTS;
	for (int i = 1; i < m_nPorts; i++)
		m_synths[i] = new synthesizer(&m_note_factory);
	reset();
	m_loopStart = m_sequencer.find_marker("loopStart");
	m_loopEnd = m_sequencer.find_marker("loopEnd");
	m_MediaInfo.dwSampleRate = rate;
	m_MediaInfo.dwChannels = 2;
	m_MediaInfo.nBitsPerSample = 16;
	m_MediaInfo.dwSeekableFlags = KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE;
	m_MediaInfo.dwUnitSample = rate / 100;
	double totalSec = m_sequencer.get_total_time();
	if (m_loopEnd > m_loopStart && m_loopStart >= 0.0)
		totalSec = m_loopEnd;
	m_MediaInfo.qwLength = (UINT64)(totalSec * 1000.0 * 10000.0);
	m_MediaInfo.dwCount = 1;
	m_MediaInfo.dwNumber = 1;
	m_lastSample = kpi_100nsToSample(m_MediaInfo.qwLength, rate);
	m_curSample = 0;
	return 1;
}

DWORD WINAPI KbSasamiDecoder::Select(DWORD dwNumber, const KPI_MEDIAINFO** ppMediaInfo, IKpiTagInfo* pTagInfo, DWORD dwTagGetFlags)
{
	if (ppMediaInfo) *ppMediaInfo = NULL;
	if (dwNumber > m_MediaInfo.dwCount) return 0;
	if (!ppMediaInfo) return 1;
	*ppMediaInfo = &m_MediaInfo;
	(void)pTagInfo;
	(void)dwTagGetFlags;
	return 1;
}

DWORD WINAPI KbSasamiDecoder::Render(BYTE* pBuffer, DWORD dwSizeSample)
{
	if (!pBuffer || dwSizeSample == 0) return 0;
	if (m_fmMode)
		return m_fm.Render((int16_t*)pBuffer, dwSizeSample);

	const double rate = (double)m_MediaInfo.dwSampleRate;
	const int looping = (m_loopEnd > m_loopStart && m_loopStart >= 0.0) ? 1 : 0;
	const UINT64 loopStartSamp = looping ? (UINT64)(m_loopStart * rate + 0.5) : 0;
	const UINT64 loopEndSamp = looping ? (UINT64)(m_loopEnd * rate + 0.5) : 0;

	DWORD remain = dwSizeSample;
	BYTE* p = pBuffer;
	while (remain) {
		if (looping && loopEndSamp > loopStartSamp && m_curSample > loopEndSamp) {
			m_sequencer.set_position(m_loopStart);
			m_curSample = loopStartSamp;
		}
		DWORD chunk = remain;
		if (chunk > MIX_FRAMES) chunk = MIX_FRAMES;
		if (looping && loopEndSamp > loopStartSamp && m_curSample <= loopEndSamp
			&& m_curSample + chunk > loopEndSamp + 1)
			chunk = (DWORD)(loopEndSamp + 1 - m_curSample);
		if (chunk == 0) break;
		const double tEnd = (double)(m_curSample + chunk) / rate;
		m_sequencer.play_forward(tEnd, this);
		for (DWORD i = 0; i < chunk * 2; i++) m_mix[i] = 0.0;
		for (int i = 0; i < m_nPorts; i++)
			m_synths[i]->synthesize_mixing(m_mix, chunk, m_MediaInfo.dwSampleRate);
		int16_t* out = (int16_t*)p;
		for (DWORD i = 0; i < chunk * 2; i++) {
			int v = (int)(m_mix[i] * 32767.0);
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[i] = (int16_t)v;
		}
		m_curSample += chunk;
		remain -= chunk;
		p += chunk * 4;
	}

	if (!looping && m_sequencer.is_play_end()) {
		const double limit = 0.001;
		int16_t* out = (int16_t*)pBuffer;
		for (DWORD i = 0; i < dwSizeSample; i++) {
			if (m_endSample++ >= m_MediaInfo.dwSampleRate * 5) return i;
			if (m_silentSample++ >= m_MediaInfo.dwSampleRate / 2) return i;
			const double l = out[i * 2] / 32767.0;
			const double r = out[i * 2 + 1] / 32767.0;
			if (l < -limit || l > limit || r < -limit || r > limit)
				m_silentSample = 0;
		}
	}
	return dwSizeSample;
}

UINT64 WINAPI KbSasamiDecoder::Seek(UINT64 qwPosSample, DWORD)
{
	if (m_fmMode)
		return m_fm.SeekSample(qwPosSample);
	m_seeking = true;
	m_sequencer.play(0, this);
	reset();
	UINT64 pos = qwPosSample;
	if (m_loopEnd > m_loopStart && m_loopStart >= 0.0) {
		const double rate = (double)m_MediaInfo.dwSampleRate;
		const UINT64 ls = (UINT64)(m_loopStart * rate + 0.5);
		const UINT64 le = (UINT64)(m_loopEnd * rate + 0.5);
		if (le > ls && pos >= le)
			pos = ls + ((pos - ls) % (le - ls));
	}
	if (pos > 0) {
		const double t = (double)pos / (double)m_MediaInfo.dwSampleRate;
		m_sequencer.play(t, this);
	}
	m_seeking = false;
	m_curSample = pos;
	m_endSample = 0;
	m_silentSample = 0;
	return pos;
}
